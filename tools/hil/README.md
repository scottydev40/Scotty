# Scotty hardware-in-the-loop runner

`scotty_hil.py` runs real Quick Share payloads in both directions, once with
laptop Wi-Fi enabled and once with it disabled. `matrix` repeats that four-run
set for every MCP/BlueZ Experimental combination (16 transfers total).

The runner does not contain phone-model-specific tap coordinates. It stages and
verifies unique payloads with `adb`, drives Scotty through D-Bus and its `--send`
entry point, and pauses for the two UI gestures by default. Existing UI
automation can be supplied as argv templates.

Do not run this from `sudo`: Scotty and `busctl --user` must use the logged-in
desktop session. Matrix mode invokes `sudo` only for the BlueZ files and service.

## What each transfer asserts

Every result row requires all of the following:

1. The exact generated payload arrives and has the expected SHA-256. Incoming
   files are searched under `~/Downloads/Scotty` by default; outgoing files are
   checked under `/sdcard/Download` with `adb`.
2. Scotty supplies completion evidence. The runner captures the new portion of
   the live Scotty log and discovers completion/success lines rather than
   relying on a single hard-coded application string. It also captures
   `TransferActiveChanged(true)` followed by `TransferActiveChanged(false)` from
   D-Bus, which is valid completion evidence when the build does not emit a
   final log line.
3. The transfer's fresh `adb logcat` contains no consent request. All lines
   containing `SMP_CONSENT_REQ_EVT`, `btm_ble_sec`, or
   `btif_dm_ssp_cfm_req` are retained in `consent-candidates.txt`; SSP/consent,
   confirmation, numeric comparison, passkey, and pairing request forms fail
   the row.
4. The laptop adapter address is not newly present in the phone's bonded-device
   set. Full `adb shell dumpsys bluetooth_manager` output is saved before and
   after each transfer.
5. `org.bluez.Adapter1.Alias` is exactly `laptop` immediately after transfer. A
   long base64-looking value is called out as a rotating Nearby name.
6. With laptop Wi-Fi off, the captured Scotty log contains a runtime RFCOMM or
   Bluetooth Classic medium marker. A successful payload alone is insufficient
   for the off-Wi-Fi row.

## Prerequisites

- Run from the `bluez-agent-scoping` checkout as the desktop user.
- Install the test build as `~/.local/bin/scotty` and
  `~/.local/lib/libnearby_sharing_api_shared.so`.
- Quit any existing Scotty instance. The runner refuses to take over an
  unrelated process.
- Connect and authorize the phone with `adb`; note its serial from
  `adb devices`.
- Install `adb`, `busctl`, `nmcli`, and, for matrix mode, `systemctl`, `sudo`,
  and `hciconfig`.
- Make the phone visible to Quick Share for laptop-to-phone sends. Scotty uses
  visibility `0` (Everyone) unless `--visibility` is supplied.
- Ensure the laptop adapter Alias is `laptop` before starting.

The test payload is harmless text. The phone-to-laptop copy staged at
`/sdcard/Download/scotty-hil-...txt` and received copies are intentionally left
in place as transfer evidence.

## Current-configuration run

From the repository root:

```bash
python3 tools/hil/scotty_hil.py run --serial PHONE_SERIAL
```

For each direction, follow the printed action and press Enter after initiating
it. For laptop-to-phone, one prompt covers selecting the phone in Scotty and
accepting on Android.

Label the current configuration accurately in the report when it differs from
the defaults:

```bash
python3 tools/hil/scotty_hil.py run \
  --serial PHONE_SERIAL \
  --no-current-mcp \
  --no-current-experimental
```

If Scotty saves received files elsewhere, pass
`--laptop-receive-dir /actual/save/path`.

## A/B matrix

Warm the sudo credential, then start the matrix as the desktop user:

```bash
sudo -v
python3 tools/hil/scotty_hil.py matrix --serial PHONE_SERIAL
```

The cells run in Gray-code order to change one dimension at a time:

1. MCP ON, Experimental=true
2. MCP ON, Experimental=false
3. MCP OFF, Experimental=false
4. MCP OFF, Experimental=true

For MCP OFF, the runner creates
`/etc/systemd/system/bluetooth.service.d/10-nearby-no-mcp.conf`, clears
`ExecStart`, and repeats the detected stock command with `--noplugin=mcp`.
Pass the stock command explicitly if the systemd representation cannot be
detected:

```bash
python3 tools/hil/scotty_hil.py matrix \
  --serial PHONE_SERIAL \
  --bluetoothd-exec '/usr/lib/bluetooth/bluetoothd'
```

For Experimental, only the active `Experimental =` key in `[General]` is
changed. Each cell is applied with `systemctl daemon-reload` and
`systemctl restart bluetooth`; no reboot is used. The runner verifies the
effective file value and live bluetoothd command line before transferring.

Bluetooth restarts are separated by 30 seconds by default. If the startup log
reports advertising/L2CAP failure, the runner performs one recovery consisting
of a spaced Bluetooth restart followed by `hciconfig hciN reset`, then retries
the advertising probe.

## Automated UI hooks

The three hook options accept a shell-like string that is split into argv; it
is not passed to a shell. Available placeholders are:

- `{serial}` — adb serial
- `{payload}` — laptop payload path
- `{phone_payload}` — staged `/sdcard/Download/...` path
- `{laptop_name}`, `{direction}`, `{wifi}`, and `{cell}`

For example, if local scripts already drive UIAutomator:

```bash
python3 tools/hil/scotty_hil.py matrix \
  --serial PHONE_SERIAL \
  --phone-send-command './phone_quick_share.py --serial {serial} --file {phone_payload} --target {laptop_name}' \
  --laptop-send-command './select_scotty_target.py --name TestPhone' \
  --phone-accept-command './phone_accept_quick_share.py --serial {serial}'
```

The hook must return zero after it has initiated/completed its gesture. Vendor
UI details stay outside the HIL runner, while adb log capture, payload proof,
bond checks, D-Bus state, and BlueZ assertions remain identical.

## Result tables and artifacts

Each invocation creates `/tmp/scotty-hil-<UTC timestamp>/` by default. Change
the parent with `--output-dir`. The directory contains `results.json`,
`results.md`, and a directory per cell/Wi-Fi/direction with:

- the exact payload;
- the relevant Scotty log slice and D-Bus monitor capture;
- raw Android logcat and filtered consent candidates;
- Bluetooth manager dumps before and after;
- a per-transfer `result.json` including marker evidence and failure text.

`results.md` starts with one aggregate row per A/B cell, followed by transfer
details. The two tables use these formats:

| MCP | Experimental | Wi-Fi on P→L | Wi-Fi on L→P | Wi-Fi off P→L | Wi-Fi off L→P | Consent events | Cell result |
|---|---|---:|---:|---:|---:|---:|---|
| ON | true | PASS | PASS | PASS | PASS | 0 | PASS |

| MCP | Experimental | Wi-Fi | Direction | Payload | Completion marker | Consent requests | New bond | Alias | Classic path | Result | Error |
|---|---|---|---|---:|---:|---:|---:|---|---:|---|---|
| ON | true | on | phone-to-laptop | PASS | PASS | 0 | PASS | laptop | n/a | PASS | — |
| ON | true | off | laptop-to-phone | PASS | PASS | 0 | PASS | laptop | PASS | PASS | — |

Interpret the two A/B questions from complete cell rows:

- A consent failure only in MCP ON cells implicates MCP. The raw logcat evidence
  identifies the exact callback.
- All rows passing with Experimental=false proves this Scotty build's L2CAP-CoC
  and AdvertisementMonitor runtime works without the BlueZ Experimental flag.
  Startup failure, missing advertising, or transfer failure makes that cell
  fail rather than silently skipping it.

Use `--classic-marker-regex REGEX` if a local build logs a different exact
Bluetooth Classic/RFCOMM marker. The matched lines are recorded as evidence.

## Restoration and process safety

Matrix mode snapshots the exact original contents, ownership, and mode of both
BlueZ files before its first edit. Its `finally`, `atexit`, SIGINT, and SIGTERM
handlers restore both files, restore the original Wi-Fi radio state, run the
required Bluetooth daemon reload/restart, and stop only the exact Scotty child
PID it launched. The code never calls `pkill`, `pgrep`, or `killall`.

Original file copies and JSON manifests are also stored at the top of the
artifact directory. They provide recovery evidence if the runner is terminated
with SIGKILL or the machine loses power, the two cases in which no process can
run an exit handler. Restore the files described by those manifests, remove a
target whose manifest says `"existed": false`, then run:

```bash
sudo systemctl daemon-reload
sudo systemctl restart bluetooth
```

Do not delete an artifact directory until its matrix run has exited and printed
the final results path.

## Static tests (no hardware access)

These only test parsing and config transforms:

```bash
python3 -m unittest discover -s tools/hil -p 'test_*.py' -v
```
