#!/usr/bin/env python3
"""Hardware-in-the-loop transfer and BlueZ A/B runner for Scotty.

This program intentionally does not try to encode vendor-specific Android UI
coordinates.  It stages/verifies payloads with adb and either invokes supplied
UI-driver commands or pauses for a human to perform the two UI gestures.
"""

from __future__ import annotations

import argparse
import atexit
import dataclasses
import datetime as dt
import hashlib
import json
import os
import re
import shlex
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Callable, Iterable, Sequence


SCOTTY_SERVICE = "dev.scotty.Scotty"
SCOTTY_PATH = "/dev/scotty/Scotty"
SCOTTY_INTERFACE = "dev.scotty.Scotty"
BLUEZ_SERVICE = "org.bluez"
BLUEZ_ADAPTER_INTERFACE = "org.bluez.Adapter1"
MCP_DROPIN = Path("/etc/systemd/system/bluetooth.service.d/10-nearby-no-mcp.conf")
BLUEZ_MAIN_CONF = Path("/etc/bluetooth/main.conf")
# The real, user-visible prompt is SMP_CONSENT_REQ_EVT (LE bond consent). Note
# that btif_dm_ssp_cfm_req_evt with "just_works" fires SILENTLY (Auto-accept
# temporary pairing) for BOTH Pixel and Samsung and is NOT a prompt — matching it
# bare would false-FAIL a clean transfer, so it is excluded below via just_works.
CONSENT_TERMS = re.compile(
    r"SMP_CONSENT_REQ_EVT|"
    r"btif_dm_ssp_cfm_req(?!.*just_works)|"
    r"btm_ble_sec.*(?:consent|confirm|numeric|passkey)",
    re.IGNORECASE,
)
SECURITY_CANDIDATES = re.compile(
    r"SMP_CONSENT_REQ_EVT|btif_dm_ssp_cfm_req|btm_ble_sec", re.IGNORECASE
)
SUCCESS_CANDIDATES = re.compile(
    r"(?:incoming|outgoing|payload|transfer|send|receiv).{0,100}"
    r"(?:complete|success)|(?:complete|success).{0,100}"
    r"(?:incoming|outgoing|payload|transfer|send|receiv)",
    re.IGNORECASE,
)
CLASSIC_CANDIDATES = re.compile(
    r"RFCOMM|BluetoothClassic::Connect|ENCRYPTED_BLUETOOTH|"
    r"(?:current|upgrade|connection|endpoint).{0,80}medium.{0,20}BLUETOOTH|"
    r"medium\s*:\s*BLUETOOTH",
    re.IGNORECASE,
)
ADVERTISING_FAILURES = re.compile(
    r"(?:failed|unable|error).{0,100}(?:start|register|open).{0,40}advertis|"
    r"(?:start|register|open).{0,40}advertis.{0,100}(?:failed|unable|error)|"
    r"L2CAP server socket.{0,80}(?:failed to (?:open|listen)|invalid)",
    re.IGNORECASE,
)
ADVERTISING_SUCCESS = re.compile(
    r"started BLE Advertising with BleAdvertisement", re.IGNORECASE
)
MAC_RE = re.compile(r"(?i)(?:[0-9a-f]{2}:){5}[0-9a-f]{2}")


class HilError(RuntimeError):
    pass


def utc_stamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def shell_join(argv: Sequence[str]) -> str:
    return " ".join(shlex.quote(str(item)) for item in argv)


def systemd_join(argv: Sequence[str]) -> str:
    """Quote argv for an ExecStart= line (which is not parsed by a shell)."""
    quoted = []
    for item in argv:
        value = str(item)
        if "\n" in value or "\r" in value:
            raise HilError("newline in bluetoothd ExecStart argument")
        if re.search(r"\s|[\"\\]", value):
            value = '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'
        quoted.append(value)
    return " ".join(quoted)


class CommandRunner:
    def __init__(self, verbose: bool = True) -> None:
        self.verbose = verbose

    def run(
        self,
        argv: Sequence[str | os.PathLike[str]],
        *,
        check: bool = True,
        timeout: float | None = 60,
        input_text: str | None = None,
    ) -> subprocess.CompletedProcess[str]:
        args = [str(value) for value in argv]
        if self.verbose:
            print(f"+ {shell_join(args)}", flush=True)
        try:
            result = subprocess.run(
                args,
                check=False,
                text=True,
                input=input_text,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=timeout,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise HilError(f"command failed: {shell_join(args)}: {exc}") from exc
        if check and result.returncode != 0:
            raise HilError(
                f"command exited {result.returncode}: {shell_join(args)}\n"
                f"{result.stdout.rstrip()}"
            )
        return result


def require_commands(names: Iterable[str]) -> None:
    import shutil

    missing = [name for name in names if shutil.which(name) is None]
    if missing:
        raise HilError(f"missing required command(s): {', '.join(missing)}")


def sha256_path(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_busctl_scalar(output: str) -> str:
    match = re.match(r'^\s*\S+\s+"(.*)"\s*$', output, re.DOTALL)
    if match:
        return match.group(1).replace(r'\"', '"').replace(r"\\", "\\")
    fields = output.strip().split(maxsplit=1)
    if len(fields) != 2:
        raise HilError(f"unexpected busctl scalar: {output!r}")
    return fields[1].strip()


def set_experimental(text: str, enabled: bool) -> str:
    value = "true" if enabled else "false"
    lines = text.splitlines(keepends=True)
    section_start: int | None = None
    section_end = len(lines)
    for index, line in enumerate(lines):
        section = re.match(r"^\s*\[([^]]+)\]\s*(?:[#;].*)?(?:\r?\n)?$", line)
        if not section:
            continue
        if section_start is not None:
            section_end = index
            break
        if section.group(1).strip().lower() == "general":
            section_start = index
    if section_start is None:
        suffix = "" if not text or text.endswith(("\n", "\r")) else "\n"
        return f"{text}{suffix}[General]\nExperimental = {value}\n"

    setting = re.compile(r"^(\s*)Experimental\s*=.*?(\r?\n)?$", re.IGNORECASE)
    replaced = False
    for index in range(section_start + 1, section_end):
        match = setting.match(lines[index])
        if match:
            newline = match.group(2) or ""
            lines[index] = f"{match.group(1)}Experimental = {value}{newline}"
            replaced = True
    if replaced:
        return "".join(lines)
    lines.insert(section_end, f"Experimental = {value}\n")
    return "".join(lines)


def parse_bonded_macs(dumpsys: str) -> set[str]:
    """Extract only addresses for which dumpsys supplies bonded evidence."""
    bonded: set[str] = set()
    lines = dumpsys.splitlines()
    for index, line in enumerate(lines):
        lower = line.lower()
        macs = {mac.upper() for mac in MAC_RE.findall(line)}
        if macs and (
            "bondeddevice" in lower
            or "bonded_devices" in lower
            or "bonded devices" in lower
            or "mbondeddevices" in lower
        ):
            bonded.update(macs)
        if macs and re.search(r"bond(?:state)?\s*[=:]\s*(?:12|bonded)", lower):
            bonded.update(macs)
        if "bonded devices" in lower or "mbondeddevices" in lower:
            header_indent = len(line) - len(line.lstrip())
            for following in lines[index + 1 : index + 40]:
                following_indent = len(following) - len(following.lstrip())
                if following.strip() and following_indent <= header_indent:
                    break
                bonded.update(mac.upper() for mac in MAC_RE.findall(following))

    # Some Android releases put address and bond state on adjacent lines.
    for index, line in enumerate(lines):
        macs = {mac.upper() for mac in MAC_RE.findall(line)}
        if not macs:
            continue
        context = " ".join(lines[max(0, index - 2) : index + 3]).lower()
        if re.search(r"bond(?:state)?\s*[=:]\s*(?:12|bonded)", context):
            bonded.update(macs)
    return bonded


def has_bond_dump_schema(dumpsys: str) -> bool:
    return bool(
        re.search(
            r"bonded[ _]?devices|mbondeddevices|bond(?:state)?\s*[=:]",
            dumpsys,
            re.IGNORECASE,
        )
    )


def looks_rotating_alias(alias: str) -> bool:
    if len(alias) < 12 or " " in alias:
        return False
    return bool(re.fullmatch(r"[A-Za-z0-9+/_=-]+", alias))


@dataclasses.dataclass
class FileSnapshot:
    existed: bool
    content: bytes = b""
    mode: int = 0o644
    uid: int = 0
    gid: int = 0


class SystemFiles:
    def __init__(self, commands: CommandRunner, artifacts: Path) -> None:
        self.commands = commands
        self.artifacts = artifacts
        self.snapshots: dict[Path, FileSnapshot] = {}
        self.changed = False

    def _sudo(self, *args: str, **kwargs: Any) -> subprocess.CompletedProcess[str]:
        return self.commands.run(["sudo", *args], **kwargs)

    def snapshot(self, path: Path) -> FileSnapshot:
        if path in self.snapshots:
            return self.snapshots[path]
        if self._sudo("test", "-L", str(path), check=False).returncode == 0:
            raise HilError(f"refusing to replace symlinked system configuration: {path}")
        exists = self._sudo("test", "-e", str(path), check=False).returncode == 0
        if not exists:
            snapshot = FileSnapshot(False)
        else:
            if self._sudo("test", "-f", str(path), check=False).returncode != 0:
                raise HilError(f"system configuration is not a regular file: {path}")
            content_result = self._sudo("cat", str(path))
            metadata = self._sudo("stat", "-c", "%a %u %g", str(path)).stdout.strip()
            mode_text, uid_text, gid_text = metadata.split()
            snapshot = FileSnapshot(
                True,
                content_result.stdout.encode(),
                int(mode_text, 8),
                int(uid_text),
                int(gid_text),
            )
        self.snapshots[path] = snapshot
        backup_name = path.as_posix().strip("/").replace("/", "__") + ".original"
        backup_path = self.artifacts / backup_name
        backup_path.write_bytes(snapshot.content)
        manifest = {
            "path": str(path),
            "existed": snapshot.existed,
            "mode": oct(snapshot.mode),
            "uid": snapshot.uid,
            "gid": snapshot.gid,
            "backup": backup_path.name,
        }
        (self.artifacts / f"{backup_name}.json").write_text(
            json.dumps(manifest, indent=2) + "\n"
        )
        return snapshot

    def install(self, path: Path, content: bytes, snapshot: FileSnapshot) -> None:
        self.changed = True
        with tempfile.NamedTemporaryFile(dir=self.artifacts, delete=False) as temp:
            temp.write(content)
            temp_path = Path(temp.name)
        try:
            self._sudo("mkdir", "-p", str(path.parent))
            self._sudo(
                "install",
                f"--mode={snapshot.mode:o}",
                f"--owner={snapshot.uid}",
                f"--group={snapshot.gid}",
                str(temp_path),
                str(path),
            )
        finally:
            temp_path.unlink(missing_ok=True)

    def remove(self, path: Path) -> None:
        self.changed = True
        self._sudo("rm", "-f", "--", str(path))

    def restore_all(self) -> None:
        errors: list[str] = []
        for path, snapshot in reversed(list(self.snapshots.items())):
            try:
                if snapshot.existed:
                    self.install(path, snapshot.content, snapshot)
                else:
                    self.remove(path)
            except Exception as exc:  # restoration must attempt every file
                errors.append(f"{path}: {exc}")
        if errors:
            raise HilError("configuration restoration failed:\n" + "\n".join(errors))


@dataclasses.dataclass(frozen=True)
class MatrixCell:
    mcp: bool
    experimental: bool

    @property
    def label(self) -> str:
        return f"mcp-{'on' if self.mcp else 'off'}_experimental-{'true' if self.experimental else 'false'}"


@dataclasses.dataclass
class TransferResult:
    cell: str
    wifi: str
    direction: str
    payload: bool = False
    transfer_marker: bool = False
    marker_evidence: list[str] = dataclasses.field(default_factory=list)
    consent_events: list[str] = dataclasses.field(default_factory=list)
    new_laptop_bond: bool = False
    alias: str = ""
    alias_ok: bool = False
    classic_path: bool | None = None
    classic_evidence: list[str] = dataclasses.field(default_factory=list)
    error: str = ""

    @property
    def passed(self) -> bool:
        required = (
            self.payload
            and self.transfer_marker
            and not self.consent_events
            and not self.new_laptop_bond
            and self.alias_ok
        )
        if self.wifi == "off":
            required = required and self.classic_path is True
        return required and not self.error

    def as_dict(self) -> dict[str, Any]:
        value = dataclasses.asdict(self)
        value["passed"] = self.passed
        return value


class Adb:
    def __init__(self, commands: CommandRunner, serial: str) -> None:
        self.commands = commands
        self.serial = serial

    def run(self, *args: str, **kwargs: Any) -> subprocess.CompletedProcess[str]:
        return self.commands.run(["adb", "-s", self.serial, *args], **kwargs)

    def shell(self, *args: str, **kwargs: Any) -> subprocess.CompletedProcess[str]:
        return self.run("shell", *args, **kwargs)

    def bonded_dump(self) -> str:
        return self.shell("dumpsys", "bluetooth_manager", timeout=90).stdout

    def start_logcat(self, destination: Path) -> tuple[subprocess.Popen[str], Any]:
        self.run("logcat", "-c", timeout=30)
        output = destination.open("w")
        process = subprocess.Popen(
            ["adb", "-s", self.serial, "logcat", "-v", "threadtime"],
            text=True,
            stdout=output,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        return process, output

    def stop_logcat(self, process: subprocess.Popen[str], output: Any) -> None:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        output.close()


def stop_capture(process: subprocess.Popen[str], output: Any) -> None:
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
    output.close()


class Scotty:
    def __init__(self, args: argparse.Namespace, commands: CommandRunner) -> None:
        self.args = args
        self.commands = commands
        self.process: subprocess.Popen[Any] | None = None

    def bus_call(self, method: str, signature: str = "", *values: str) -> str:
        argv = [
            "busctl",
            "--user",
            "call",
            SCOTTY_SERVICE,
            SCOTTY_PATH,
            SCOTTY_INTERFACE,
            method,
        ]
        if signature:
            argv.extend([signature, *values])
        return self.commands.run(argv, timeout=15).stdout.strip()

    def is_on_bus(self) -> bool:
        return self.commands.run(
            ["busctl", "--user", "status", SCOTTY_SERVICE], check=False, timeout=10
        ).returncode == 0

    def start(self) -> None:
        if self.process is not None and self.process.poll() is None:
            return
        if self.is_on_bus():
            raise HilError(
                f"{SCOTTY_SERVICE} is already owned; exit the existing Scotty instance "
                "before running HIL (the runner refuses to kill an unowned process)"
            )
        self.process = subprocess.Popen(
            [str(self.args.scotty), "--background"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        deadline = time.monotonic() + self.args.start_timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise HilError(f"Scotty exited during startup with {self.process.returncode}")
            if self.is_on_bus():
                if not self.running():
                    time.sleep(0.5)
                    continue
                self.set_visibility(self.args.visibility)
                actual = self.get_visibility()
                if actual != self.args.visibility:
                    raise HilError(
                        f"SetVisibility({self.args.visibility}) read back as {actual}"
                    )
                return
            time.sleep(0.5)
        raise HilError(f"Scotty did not claim D-Bus within {self.args.start_timeout}s")

    def stop(self) -> None:
        process = self.process
        self.process = None
        if process is None:
            return
        pid = process.pid
        if process.poll() is None and self.is_on_bus():
            self.commands.run(
                [
                    "busctl",
                    "--user",
                    "call",
                    SCOTTY_SERVICE,
                    SCOTTY_PATH,
                    SCOTTY_INTERFACE,
                    "Quit",
                ],
                check=False,
                timeout=10,
            )
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            # Exact child PID only.  Never search by command line or process name.
            os.kill(pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.kill(pid, signal.SIGKILL)
                process.wait(timeout=5)

    def set_visibility(self, mode: int) -> None:
        self.bus_call("SetVisibility", "i", str(mode))

    def get_visibility(self) -> int:
        return int(parse_busctl_scalar(self.bus_call("GetVisibility")))

    def transfer_active(self) -> bool:
        return parse_busctl_scalar(self.bus_call("GetTransferActive")) == "true"

    def running(self) -> bool:
        return parse_busctl_scalar(self.bus_call("GetRunning")) == "true"

    def stage_outgoing(self, payload: Path) -> None:
        result = self.commands.run([str(self.args.scotty), "--send", str(payload)], timeout=20)
        if result.returncode != 0:
            raise HilError("Scotty rejected the outgoing payload")


class HilRunner:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.commands = CommandRunner(verbose=not args.quiet)
        self.adb = Adb(self.commands, args.serial)
        self.scotty = Scotty(args, self.commands)
        self.artifacts = args.output_dir / f"scotty-hil-{utc_stamp()}"
        self.artifacts.mkdir(parents=True, exist_ok=False)
        self.results: list[TransferResult] = []
        self.last_bluetooth_restart = 0.0
        self.initial_wifi: str | None = None
        self.system_files: SystemFiles | None = None
        self._cleanup_started = False
        self.cleanup_errors: list[str] = []
        self._install_cleanup()

    def _install_cleanup(self) -> None:
        atexit.register(self.cleanup)

        def handle_signal(signum: int, _frame: Any) -> None:
            print(f"\nreceived signal {signum}; restoring state", file=sys.stderr)
            self.cleanup()
            raise SystemExit(128 + signum)

        signal.signal(signal.SIGINT, handle_signal)
        signal.signal(signal.SIGTERM, handle_signal)

    def cleanup(self) -> None:
        if self._cleanup_started:
            return
        self._cleanup_started = True
        errors: list[str] = []
        try:
            self.scotty.stop()
        except Exception as exc:
            errors.append(f"stop Scotty: {exc}")
        try:
            self.restore_wifi()
        except Exception as exc:
            errors.append(f"restore Wi-Fi: {exc}")
        if self.system_files is not None and self.system_files.changed:
            try:
                self.system_files.restore_all()
                self.restart_bluetooth(force_spacing=True)
            except Exception as exc:
                errors.append(f"restore BlueZ configuration: {exc}")
        if errors:
            print("CLEANUP ERROR:\n  " + "\n  ".join(errors), file=sys.stderr)
        self.cleanup_errors = errors

    def preflight(self, matrix: bool) -> None:
        required = ["adb", "busctl", "nmcli"]
        if matrix:
            required.extend(["sudo", "systemctl", "hciconfig"])
        require_commands(required)
        if not self.args.scotty.is_file() or not os.access(self.args.scotty, os.X_OK):
            raise HilError(f"Scotty is not executable: {self.args.scotty}")
        if self.scotty.is_on_bus():
            raise HilError(
                "Scotty is already running. Quit it first; the HIL runner never uses pkill "
                "and will not take ownership of an unrelated PID."
            )
        state = self.commands.run(["adb", "-s", self.args.serial, "get-state"]).stdout.strip()
        if state != "device":
            raise HilError(f"adb serial {self.args.serial!r} is not ready: {state!r}")
        ldd = self.commands.run(["ldd", str(self.args.scotty)], check=False).stdout
        (self.artifacts / "ldd.txt").write_text(ldd)
        expected_lib = str(Path.home() / ".local/lib/libnearby_sharing_api_shared.so")
        if "libnearby_sharing_api_shared.so" not in ldd or expected_lib not in ldd:
            raise HilError(
                f"{self.args.scotty} is not resolving Nearby from {expected_lib}; see ldd.txt"
            )
        self.initial_wifi = self.wifi_state()
        if self.initial_wifi not in {"enabled", "disabled"}:
            raise HilError(f"unexpected `nmcli radio wifi` output: {self.initial_wifi!r}")
        if matrix:
            self.commands.run(["sudo", "-v"], timeout=None)

    def wifi_state(self) -> str:
        return self.commands.run(["nmcli", "radio", "wifi"], timeout=15).stdout.strip().lower()

    def set_wifi(self, enabled: bool) -> None:
        desired = "enabled" if enabled else "disabled"
        if self.wifi_state() == desired:
            return
        self.commands.run(["nmcli", "radio", "wifi", "on" if enabled else "off"])
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if self.wifi_state() == desired:
                time.sleep(self.args.radio_settle)
                return
            time.sleep(1)
        raise HilError(f"Wi-Fi did not become {desired}")

    def restore_wifi(self) -> None:
        if self.initial_wifi in {"enabled", "disabled"}:
            self.set_wifi(self.initial_wifi == "enabled")

    def adapter_path(self) -> str:
        tree = self.commands.run(
            ["busctl", "--system", "--list", "tree", BLUEZ_SERVICE]
        ).stdout
        paths = re.findall(r"(?m)^(/org/bluez/hci\d+)\s*$", tree)
        if not paths:
            paths = sorted(set(re.findall(r"/org/bluez/hci\d+", tree)))
        if not paths:
            raise HilError("BlueZ exposes no adapter path")
        for path in paths:
            if self.adapter_alias(path) == self.args.expected_alias:
                return path
        if len(paths) == 1:
            return paths[0]
        aliases = {path: self.adapter_alias(path) for path in paths}
        raise HilError(f"no adapter has alias {self.args.expected_alias!r}: {aliases}")

    def adapter_property(self, path: str, prop: str) -> str:
        output = self.commands.run(
            [
                "busctl",
                "--system",
                "get-property",
                BLUEZ_SERVICE,
                path,
                BLUEZ_ADAPTER_INTERFACE,
                prop,
            ]
        ).stdout
        return parse_busctl_scalar(output)

    def adapter_alias(self, path: str) -> str:
        return self.adapter_property(path, "Alias")

    def restart_bluetooth(self, force_spacing: bool = False) -> None:
        elapsed = time.monotonic() - self.last_bluetooth_restart
        delay = self.args.bluetooth_restart_spacing - elapsed
        if delay > 0 and (force_spacing or self.last_bluetooth_restart > 0):
            print(f"Spacing Bluetooth restart by {delay:.1f}s to protect MT7925 BLE", flush=True)
            time.sleep(delay)
        self.commands.run(["sudo", "systemctl", "daemon-reload"], timeout=60)
        self.commands.run(["sudo", "systemctl", "restart", "bluetooth"], timeout=90)
        self.last_bluetooth_restart = time.monotonic()
        time.sleep(self.args.bluetooth_settle)

    def recover_adapter(self, adapter_path: str) -> None:
        self.scotty.stop()
        self.restart_bluetooth(force_spacing=True)
        hci = adapter_path.rsplit("/", 1)[-1]
        self.commands.run(["sudo", "hciconfig", hci, "reset"], timeout=30)
        time.sleep(self.args.bluetooth_settle)
        self.scotty.start()

    def bluetoothd_exec(self) -> list[str]:
        if self.args.bluetoothd_exec:
            return shlex.split(self.args.bluetoothd_exec)
        output = self.commands.run(
            ["systemctl", "show", "bluetooth", "--property=ExecStart", "--value"]
        ).stdout.strip()
        match = re.search(r"argv\[\]=([^;}]*)", output)
        if match:
            argv = shlex.split(match.group(1).strip())
            if argv:
                return [arg for arg in argv if arg != "--noplugin=mcp"]
        match = re.search(r"path=([^ ;}]+)", output)
        if match:
            return [match.group(1)]
        raise HilError(
            "could not derive bluetoothd ExecStart; pass --bluetoothd-exec explicitly"
        )

    def configure_cell(self, cell: MatrixCell) -> None:
        assert self.system_files is not None
        main_snapshot = self.system_files.snapshot(BLUEZ_MAIN_CONF)
        drop_snapshot = self.system_files.snapshot(MCP_DROPIN)
        if not main_snapshot.existed:
            raise HilError(f"required BlueZ configuration is missing: {BLUEZ_MAIN_CONF}")
        main_content = set_experimental(main_snapshot.content.decode(), cell.experimental).encode()
        self.system_files.install(BLUEZ_MAIN_CONF, main_content, main_snapshot)

        if cell.mcp:
            self.system_files.remove(MCP_DROPIN)
        else:
            exec_start = self.bluetoothd_exec()
            line = systemd_join([*exec_start, "--noplugin=mcp"])
            dropin = f"[Service]\nExecStart=\nExecStart={line}\n".encode()
            self.system_files.install(MCP_DROPIN, dropin, drop_snapshot)
        self.scotty.stop()
        self.restart_bluetooth()
        self.verify_cell_configuration(cell)

    def verify_cell_configuration(self, cell: MatrixCell) -> None:
        main = self.commands.run(["sudo", "cat", str(BLUEZ_MAIN_CONF)]).stdout
        effective = re.findall(r"(?im)^\s*Experimental\s*=\s*(true|false)\s*$", main)
        expected = "true" if cell.experimental else "false"
        if not effective or effective[-1].lower() != expected:
            raise HilError(f"Experimental did not become {expected}")
        main_pid = self.commands.run(
            ["systemctl", "show", "bluetooth", "--property=MainPID", "--value"]
        ).stdout.strip()
        if not main_pid.isdigit() or main_pid == "0":
            raise HilError("bluetooth.service has no MainPID after restart")
        cmdline = self.commands.run(
            ["sudo", "tr", "\\0", " ", f"/proc/{main_pid}/cmdline"]
        ).stdout
        disabled = "--noplugin=mcp" in cmdline
        if disabled == cell.mcp:
            raise HilError(f"bluetoothd command line does not match MCP={cell.mcp}: {cmdline}")

    def invoke_hook(self, hook: str | None, values: dict[str, str], prompt: str) -> None:
        if hook:
            argv = [part.format_map(values) for part in shlex.split(hook)]
            self.commands.run(argv, timeout=self.args.transfer_timeout)
            return
        if not sys.stdin.isatty():
            raise HilError(f"interactive action required: {prompt}")
        input(f"\nACTION REQUIRED: {prompt}\nPress Enter once initiated... ")

    def create_payload(self, cell: MatrixCell, wifi: str, direction: str) -> Path:
        name = f"scotty-hil-{cell.label}-{wifi}-{direction}-{utc_stamp()}.txt"
        path = self.artifacts / name
        content = (
            f"Scotty HIL payload\ncell={cell.label}\nwifi={wifi}\n"
            f"direction={direction}\nnonce={os.urandom(24).hex()}\n"
        )
        path.write_text(content)
        return path

    def log_offset(self) -> int:
        try:
            return self.args.scotty_log.stat().st_size
        except FileNotFoundError:
            return 0

    def log_slice(self, offset: int) -> str:
        try:
            with self.args.scotty_log.open("rb") as source:
                source.seek(offset)
                return source.read().decode(errors="replace")
        except FileNotFoundError:
            return ""

    def wait_for_payload(
        self,
        checker: Callable[[], bool],
        *,
        deadline: float,
    ) -> tuple[bool, list[str]]:
        edges: list[str] = []
        last_active = False
        while time.monotonic() < deadline:
            try:
                active = self.scotty.transfer_active()
                if active != last_active:
                    edges.append(f"TransferActiveChanged({str(active).lower()})")
                    last_active = active
            except HilError:
                pass
            if checker():
                # Give final metadata and the false edge time to reach the app.
                time.sleep(1)
                try:
                    active = self.scotty.transfer_active()
                    if active != last_active:
                        edges.append(f"TransferActiveChanged({str(active).lower()})")
                except HilError:
                    pass
                return True, edges
            time.sleep(0.5)
        return False, edges

    def laptop_received(self, payload: Path) -> bool:
        expected = sha256_path(payload)
        root = self.args.laptop_receive_dir
        if not root.exists():
            return False
        for candidate in root.rglob("*"):
            try:
                if candidate.is_file() and candidate.name.startswith(payload.stem):
                    if sha256_path(candidate) == expected:
                        return True
            except OSError:
                continue
        return False

    def phone_received(self, payload: Path) -> bool:
        expected = sha256_path(payload)
        command = (
            "find /sdcard/Download -type f -name "
            + shlex.quote(payload.name)
            + " -exec sha256sum '{}' \\; 2>/dev/null"
        )
        result = self.adb.shell("sh", "-c", command, check=False, timeout=30)
        return any(line.split(maxsplit=1)[0] == expected for line in result.stdout.splitlines())

    def run_transfer(
        self, cell: MatrixCell, wifi: str, direction: str, adapter_path: str
    ) -> TransferResult:
        result = TransferResult(cell=cell.label, wifi=wifi, direction=direction)
        phase = self.artifacts / cell.label / wifi / direction
        phase.mkdir(parents=True, exist_ok=True)
        payload = self.create_payload(cell, wifi, direction)
        phase_payload = phase / payload.name
        phase_payload.write_bytes(payload.read_bytes())
        payload = phase_payload
        before_dump = self.adb.bonded_dump()
        (phase / "bluetooth-before.txt").write_text(before_dump)
        before_bonds = parse_bonded_macs(before_dump)
        logcat_process, logcat_output = self.adb.start_logcat(phase / "adb-logcat.txt")
        monitor_output = (phase / "dbus-monitor.txt").open("w")
        monitor_process = subprocess.Popen(
            ["busctl", "--user", "monitor", SCOTTY_SERVICE],
            text=True,
            stdout=monitor_output,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        offset = self.log_offset()
        hook_values = {
            "serial": self.args.serial,
            "payload": str(payload),
            "phone_payload": f"/sdcard/Download/{payload.name}",
            "laptop_name": self.args.expected_alias,
            "direction": direction,
            "wifi": wifi,
            "cell": cell.label,
        }
        try:
            if direction == "phone-to-laptop":
                self.adb.run("push", str(payload), hook_values["phone_payload"], timeout=60)
                self.invoke_hook(
                    self.args.phone_send_command,
                    hook_values,
                    f"On {self.args.serial}, Quick Share {hook_values['phone_payload']} to "
                    f"{self.args.expected_alias}. Accept on the laptop if prompted.",
                )
                checker = lambda: self.laptop_received(payload)
            else:
                self.scotty.stage_outgoing(payload)
                self.invoke_hook(
                    self.args.laptop_send_command,
                    hook_values,
                    f"In Scotty, select the test phone for {payload.name}; accept it on the phone.",
                )
                if self.args.phone_accept_command:
                    self.invoke_hook(
                        self.args.phone_accept_command,
                        hook_values,
                        "Accept the incoming Quick Share transfer on the phone.",
                    )
                checker = lambda: self.phone_received(payload)
            deadline = time.monotonic() + self.args.transfer_timeout
            result.payload, edges = self.wait_for_payload(checker, deadline=deadline)
            if not result.payload:
                result.error = f"payload was not verified within {self.args.transfer_timeout}s"
        except Exception as exc:
            result.error = str(exc)
            edges = []
        finally:
            self.adb.stop_logcat(logcat_process, logcat_output)
            stop_capture(monitor_process, monitor_output)

        log_text = self.log_slice(offset)
        (phase / "scotty.log").write_text(log_text)
        semantic_markers = [line for line in log_text.splitlines() if SUCCESS_CANDIDATES.search(line)]
        monitor_text = (phase / "dbus-monitor.txt").read_text(errors="replace")
        monitor_events = re.findall(
            r"Member=TransferActiveChanged[\s\S]{0,500}?BOOLEAN\s+(true|false)",
            monitor_text,
            re.IGNORECASE,
        )
        signal_edges = [f"TransferActiveChanged({value.lower()})" for value in monitor_events]
        observed_edges = [*signal_edges, *edges]
        # A completed active->inactive D-Bus lifecycle is also an allowed marker.
        true_index = next(
            (index for index, edge in enumerate(observed_edges) if "true" in edge), None
        )
        completed_edge = true_index is not None and any(
            "false" in edge for edge in observed_edges[true_index + 1 :]
        )
        result.marker_evidence = [*observed_edges, *semantic_markers[-10:]]
        result.transfer_marker = bool(semantic_markers) or completed_edge
        if result.payload and not result.transfer_marker:
            result.error = (
                (result.error + "; ") if result.error else ""
            ) + "payload arrived but no completion log line or D-Bus active lifecycle was observed"

        logcat_text = (phase / "adb-logcat.txt").read_text(errors="replace")
        candidates = [line for line in logcat_text.splitlines() if SECURITY_CANDIDATES.search(line)]
        (phase / "consent-candidates.txt").write_text("\n".join(candidates) + ("\n" if candidates else ""))
        result.consent_events = [
            line
            for line in candidates
            if CONSENT_TERMS.search(line)
            and "just_works" not in line.lower()
            and "auto-accept" not in line.lower()
        ]

        after_dump = self.adb.bonded_dump()
        (phase / "bluetooth-after.txt").write_text(after_dump)
        if not has_bond_dump_schema(before_dump) or not has_bond_dump_schema(after_dump):
            result.error = ((result.error + "; ") if result.error else "") + (
                "could not identify bonded-device data in bluetooth_manager dumps"
            )
        after_bonds = parse_bonded_macs(after_dump)
        laptop_mac = self.adapter_property(adapter_path, "Address").upper()
        result.new_laptop_bond = laptop_mac not in before_bonds and laptop_mac in after_bonds

        result.alias = self.adapter_alias(adapter_path)
        result.alias_ok = result.alias == self.args.expected_alias
        if not result.alias_ok and looks_rotating_alias(result.alias):
            result.error = ((result.error + "; ") if result.error else "") + (
                f"adapter Alias looks like a rotating Nearby name: {result.alias!r}"
            )

        classic_regex = (
            re.compile(self.args.classic_marker_regex, re.IGNORECASE)
            if self.args.classic_marker_regex
            else CLASSIC_CANDIDATES
        )
        result.classic_evidence = [
            line for line in log_text.splitlines() if classic_regex.search(line)
        ][-10:]
        result.classic_path = bool(result.classic_evidence) if wifi == "off" else None
        if wifi == "off" and not result.classic_path:
            result.error = ((result.error + "; ") if result.error else "") + (
                "no runtime RFCOMM/Bluetooth Classic marker was discovered"
            )
        (phase / "result.json").write_text(json.dumps(result.as_dict(), indent=2) + "\n")
        self.results.append(result)
        print(f"[{ 'PASS' if result.passed else 'FAIL' }] {cell.label} {wifi} {direction}")
        return result

    def check_advertising_health(self, adapter_path: str, log_offset: int) -> None:
        try:
            self.scotty.start()
            time.sleep(self.args.advertising_probe)
            log_text = self.log_slice(log_offset)
            failures = [line for line in log_text.splitlines() if ADVERTISING_FAILURES.search(line)]
            if not ADVERTISING_SUCCESS.search(log_text):
                detail = failures[-1] if failures else "no BLE advertising success marker"
                raise HilError("advertising probe failed: " + detail)
        except HilError as first_error:
            print(f"Advertising probe failed ({first_error}); applying MT7925 recovery", flush=True)
            retry_offset = self.log_offset()
            self.recover_adapter(adapter_path)
            time.sleep(self.args.advertising_probe)
            retry_log = self.log_slice(retry_offset)
            failures = [line for line in retry_log.splitlines() if ADVERTISING_FAILURES.search(line)]
            if not ADVERTISING_SUCCESS.search(retry_log) or not self.scotty.is_on_bus():
                detail = failures[-1:] or ["no BLE advertising success marker"]
                raise HilError(f"advertising did not recover: {detail}")

    def run_cell(self, cell: MatrixCell) -> None:
        adapter_path = self.adapter_path()
        log_offset = self.log_offset()
        self.check_advertising_health(adapter_path, log_offset)
        for wifi_enabled, wifi_label in ((True, "on"), (False, "off")):
            self.set_wifi(wifi_enabled)
            for direction in ("phone-to-laptop", "laptop-to-phone"):
                self.run_transfer(cell, wifi_label, direction, adapter_path)

    def record_cell_failure(self, cell: MatrixCell, error: Exception) -> None:
        existing = {(result.wifi, result.direction) for result in self.results if result.cell == cell.label}
        for wifi in ("on", "off"):
            for direction in ("phone-to-laptop", "laptop-to-phone"):
                if (wifi, direction) in existing:
                    continue
                result = TransferResult(
                    cell=cell.label,
                    wifi=wifi,
                    direction=direction,
                    classic_path=False if wifi == "off" else None,
                    error=f"cell could not run: {error}",
                )
                self.results.append(result)
        print(f"[CELL FAIL] {cell.label}: {error}", file=sys.stderr, flush=True)

    def write_reports(self) -> None:
        data = {
            "generated_at": dt.datetime.now(dt.timezone.utc).isoformat(),
            "serial": self.args.serial,
            "expected_alias": self.args.expected_alias,
            "results": [result.as_dict() for result in self.results],
        }
        (self.artifacts / "results.json").write_text(json.dumps(data, indent=2) + "\n")
        cell_header = (
            "| MCP | Experimental | Wi-Fi on P→L | Wi-Fi on L→P | "
            "Wi-Fi off P→L | Wi-Fi off L→P | Consent events | Cell result |\n"
            "|---|---|---:|---:|---:|---:|---:|---|\n"
        )
        cell_rows = []
        for cell_name in dict.fromkeys(result.cell for result in self.results):
            cell_results = [result for result in self.results if result.cell == cell_name]
            lookup = {(result.wifi, result.direction): result for result in cell_results}
            statuses = []
            for key in (
                ("on", "phone-to-laptop"),
                ("on", "laptop-to-phone"),
                ("off", "phone-to-laptop"),
                ("off", "laptop-to-phone"),
            ):
                item = lookup.get(key)
                statuses.append("PASS" if item is not None and item.passed else "FAIL")
            consent_count = sum(len(result.consent_events) for result in cell_results)
            cell_rows.append(
                f"| {'ON' if 'mcp-on' in cell_name else 'OFF'} | "
                f"{'true' if 'experimental-true' in cell_name else 'false'} | "
                f"{' | '.join(statuses)} | {consent_count} | "
                f"{'PASS' if all(result.passed for result in cell_results) and len(cell_results) == 4 else 'FAIL'} |"
            )

        detail_header = (
            "| MCP | Experimental | Wi-Fi | Direction | Payload | Completion marker | "
            "Consent requests | New bond | Alias | Classic path | Result | Error |\n"
            "|---|---|---|---|---:|---:|---:|---:|---|---:|---|---|\n"
        )
        rows = []
        for result in self.results:
            mcp = "ON" if "mcp-on" in result.cell else "OFF"
            experimental = "true" if "experimental-true" in result.cell else "false"
            classic = "n/a" if result.classic_path is None else ("PASS" if result.classic_path else "FAIL")
            error = (result.error or "—").replace("|", "\\|").replace("\n", " ")
            rows.append(
                f"| {mcp} | {experimental} | {result.wifi} | {result.direction} | "
                f"{'PASS' if result.payload else 'FAIL'} | "
                f"{'PASS' if result.transfer_marker else 'FAIL'} | "
                f"{len(result.consent_events)} | "
                f"{'FAIL' if result.new_laptop_bond else 'PASS'} | "
                f"{result.alias or 'unreadable'} | {classic} | "
                f"{'PASS' if result.passed else 'FAIL'} | {error} |"
            )
        report = (
            "# Cell summary\n\n"
            + cell_header
            + "\n".join(cell_rows)
            + "\n\n# Transfer details\n\n"
            + detail_header
            + "\n".join(rows)
            + "\n"
        )
        (self.artifacts / "results.md").write_text(report)

    def run(self, matrix: bool) -> int:
        self.preflight(matrix)
        cells = (
            [
                MatrixCell(True, True),
                MatrixCell(True, False),
                MatrixCell(False, False),
                MatrixCell(False, True),
            ]
            if matrix
            else [MatrixCell(self.args.current_mcp, self.args.current_experimental)]
        )
        if matrix:
            self.system_files = SystemFiles(self.commands, self.artifacts)
        try:
            for cell in cells:
                print(f"\n=== {cell.label} ===", flush=True)
                try:
                    if matrix:
                        self.configure_cell(cell)
                    self.run_cell(cell)
                except Exception as exc:
                    self.record_cell_failure(cell, exc)
        finally:
            self.write_reports()
            self.cleanup()
        if self.cleanup_errors:
            raise HilError("; ".join(self.cleanup_errors))
        failures = sum(not result.passed for result in self.results)
        print(f"\nArtifacts: {self.artifacts}")
        print(f"Transfers: {len(self.results) - failures} passed, {failures} failed")
        return 1 if failures else 0


def add_common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--serial", required=True, help="adb device serial")
    parser.add_argument(
        "--scotty", type=Path, default=Path.home() / ".local/bin/scotty"
    )
    parser.add_argument(
        "--scotty-log", type=Path, default=Path("/tmp/nearby_qml_file_tray.log")
    )
    parser.add_argument(
        "--laptop-receive-dir", type=Path, default=Path.home() / "Downloads/Scotty"
    )
    parser.add_argument("--expected-alias", default="laptop")
    parser.add_argument("--visibility", type=int, choices=range(5), default=0)
    parser.add_argument("--transfer-timeout", type=float, default=240)
    parser.add_argument("--start-timeout", type=float, default=30)
    parser.add_argument("--radio-settle", type=float, default=8)
    parser.add_argument("--advertising-probe", type=float, default=8)
    parser.add_argument("--bluetooth-settle", type=float, default=8)
    parser.add_argument("--bluetooth-restart-spacing", type=float, default=30)
    parser.add_argument("--output-dir", type=Path, default=Path("/tmp"))
    parser.add_argument(
        "--phone-send-command",
        help="argv template that initiates phone->laptop; placeholders documented in README",
    )
    parser.add_argument(
        "--laptop-send-command",
        help="argv template that selects the phone in Scotty (default: interactive)",
    )
    parser.add_argument(
        "--phone-accept-command",
        help="argv template that accepts laptop->phone (default: part of interactive action)",
    )
    parser.add_argument(
        "--classic-marker-regex",
        help="site-specific regex used to prove the off-Wi-Fi RFCOMM path",
    )
    parser.add_argument("--quiet", action="store_true")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    run_parser = subparsers.add_parser("run", help="test the current BlueZ configuration")
    add_common_arguments(run_parser)
    run_parser.add_argument(
        "--current-mcp", action=argparse.BooleanOptionalAction, default=True
    )
    run_parser.add_argument(
        "--current-experimental", action=argparse.BooleanOptionalAction, default=True
    )
    matrix_parser = subparsers.add_parser("matrix", help="run all MCP/Experimental cells")
    add_common_arguments(matrix_parser)
    matrix_parser.add_argument(
        "--bluetoothd-exec",
        help="stock bluetoothd ExecStart argv, used when auto-detection is insufficient",
    )
    matrix_parser.set_defaults(current_mcp=True, current_experimental=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return HilRunner(args).run(matrix=args.command == "matrix")
    except HilError as exc:
        print(f"HIL ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
