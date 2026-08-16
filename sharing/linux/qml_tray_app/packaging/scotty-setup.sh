#!/usr/bin/env bash
#
# Scotty privileged first-run setup.
#
# Nearby Share on Linux needs two system-level things that a normal (unprivileged)
# app install cannot do for itself:
#
#   1. Runtime services:  bluez (the Bluetooth daemon) + Avahi/nss-mdns (so the
#      WIFI_LAN mDNS path can resolve the phone).
#   2. The BlueZ "combined recipe" that makes Nearby work without a pairing prompt:
#        - /etc/bluetooth/main.conf  ->  Experimental = true
#          (required: BLE L2CAP CoC + AdvertisementMonitor, used by the
#           off-network laptop->phone-to-unknown-device path, are experimental-gated)
#        - a systemd drop-in that appends `--noplugin=mcp` to bluetoothd
#          (required: the experimental MCP plugin independently reconnects to the
#           phone, reads its GMCS Media Player Name, hits Insufficient Authentication
#           and initiates SMP -> a spurious "pair?" prompt on every transfer.
#           Removing only that one plugin keeps every Nearby feature and kills the prompt.)
#      NEITHER lever alone is correct: no Experimental -> off-network-to-unknown
#      breaks; Experimental without the drop-in -> the pairing prompt returns.
#
# This script is idempotent (safe to re-run) and reversible (--uninstall restores
# the original main.conf and removes the drop-in). It restarts bluetooth ONLY when
# it actually changed something, so re-runs don't churn the radio.
#
# Usage:
#   sudo ./scotty-setup.sh            # apply
#   sudo ./scotty-setup.sh --uninstall
#   sudo ./scotty-setup.sh --no-deps  # skip the apt step (config only)
#
set -euo pipefail

DROPIN_DIR=/etc/systemd/system/bluetooth.service.d
DROPIN="$DROPIN_DIR/10-nearby-no-mcp.conf"
MAIN_CONF=/etc/bluetooth/main.conf
BACKUP="$MAIN_CONF.scotty-backup"
VENDOR_UNIT=/usr/lib/systemd/system/bluetooth.service
DEPS=(bluez avahi-daemon libnss-mdns)

say()  { printf '\033[1;36m[scotty-setup]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[scotty-setup]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[scotty-setup]\033[0m %s\n' "$*" >&2; exit 1; }

[[ "$(id -u)" -eq 0 ]] || die "run me with sudo: sudo $0 ${*:-}"

changed=0

restart_bluetooth() {
  say "reloading systemd + restarting bluetooth"
  systemctl daemon-reload
  systemctl restart bluetooth
}

uninstall() {
  local touched=0
  if [[ -f "$BACKUP" ]]; then
    say "restoring original $MAIN_CONF"
    install -m0644 "$BACKUP" "$MAIN_CONF"
    rm -f "$BACKUP"
    touched=1
  else
    warn "no main.conf backup found; leaving $MAIN_CONF as-is"
  fi
  if [[ -f "$DROPIN" ]]; then
    say "removing $DROPIN"
    rm -f "$DROPIN"
    rmdir --ignore-fail-on-non-empty "$DROPIN_DIR" 2>/dev/null || true
    touched=1
  fi
  [[ "$touched" -eq 1 ]] && restart_bluetooth
  say "uninstall complete (Bluetooth restored; Nearby recipe removed)"
  exit 0
}

WITH_DEPS=1
case "${1:-}" in
  --uninstall) uninstall ;;
  --no-deps)   WITH_DEPS=0 ;;
  "" )         : ;;
  * )          die "unknown argument: $1" ;;
esac

# --- 1. runtime deps ---------------------------------------------------------
if [[ "$WITH_DEPS" -eq 1 ]]; then
  if command -v apt-get >/dev/null 2>&1; then
    missing=()
    for pkg in "${DEPS[@]}"; do
      dpkg -s "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
    done
    if [[ "${#missing[@]}" -gt 0 ]]; then
      say "installing runtime deps: ${missing[*]}"
      apt-get update -qq
      DEBIAN_FRONTEND=noninteractive apt-get install -y "${missing[@]}"
    else
      say "runtime deps already present: ${DEPS[*]}"
    fi
  else
    warn "not an apt system; install these yourself: ${DEPS[*]}"
  fi
fi

# --- 2. Experimental = true under [General] ----------------------------------
[[ -f "$MAIN_CONF" ]] || die "$MAIN_CONF missing — is bluez installed?"
[[ -f "$BACKUP" ]] || cp -a "$MAIN_CONF" "$BACKUP"   # one-time pristine backup

# Robust INI edit via python (matches the tested HIL set_experimental logic):
# set Experimental=true inside [General], adding the key or the section if absent.
new_main="$(MAIN_CONF="$MAIN_CONF" python3 - <<'PY'
import os, re, sys
p = os.environ["MAIN_CONF"]
text = open(p, encoding="utf-8").read()
lines = text.splitlines(keepends=True)
start = None; end = len(lines)
for i, line in enumerate(lines):
    m = re.match(r"^\s*\[([^]]+)\]\s*$", line)
    if not m:
        continue
    if start is not None:
        end = i; break
    if m.group(1).strip().lower() == "general":
        start = i
if start is None:
    suffix = "" if not text or text.endswith(("\n", "\r")) else "\n"
    sys.stdout.write(f"{text}{suffix}[General]\nExperimental = true\n"); raise SystemExit
setting = re.compile(r"^(\s*)Experimental\s*=.*?(\r?\n)?$", re.IGNORECASE)
done = False
for i in range(start + 1, end):
    m = setting.match(lines[i])
    if m:
        lines[i] = f"{m.group(1)}Experimental = true{m.group(2) or ''}"; done = True
if not done:
    lines.insert(end, "Experimental = true\n")
sys.stdout.write("".join(lines))
PY
)"
if [[ "$new_main" != "$(cat "$MAIN_CONF")" ]]; then
  say "setting Experimental = true in $MAIN_CONF"
  printf '%s' "$new_main" > "$MAIN_CONF"
  changed=1
else
  say "Experimental already true"
fi

# --- 3. --noplugin=mcp drop-in (append to the real ExecStart) ----------------
stock_exec="$(grep -m1 '^ExecStart=' "$VENDOR_UNIT" | sed 's/^ExecStart=//')"
[[ -n "$stock_exec" ]] || stock_exec="/usr/libexec/bluetooth/bluetoothd"
# strip any pre-existing flag so re-runs don't double it
stock_exec="$(printf '%s' "$stock_exec" | sed 's/ *--noplugin=mcp//g')"
want_dropin=$(printf '[Service]\nExecStart=\nExecStart=%s --noplugin=mcp\n' "$stock_exec")

if [[ ! -f "$DROPIN" || "$(cat "$DROPIN")" != "$want_dropin" ]]; then
  say "installing $DROPIN"
  install -d "$DROPIN_DIR"
  printf '%s\n' "$want_dropin" > "$DROPIN"
  chmod 0644 "$DROPIN"
  changed=1
else
  say "drop-in already current"
fi

# --- 4. apply if anything changed --------------------------------------------
if [[ "$changed" -eq 1 ]]; then
  restart_bluetooth
else
  say "nothing changed; not touching the radio"
fi

# --- 5. verify ---------------------------------------------------------------
main_pid="$(systemctl show bluetooth -p MainPID --value)"
cmdline="$(tr '\0' ' ' < "/proc/$main_pid/cmdline" 2>/dev/null || true)"
grep -qiE '^\s*Experimental\s*=\s*true' "$MAIN_CONF" \
  || die "verify failed: Experimental not true"
[[ "$cmdline" == *--noplugin=mcp* ]] \
  || die "verify failed: bluetoothd not running with --noplugin=mcp (cmdline: $cmdline)"
say "OK — Experimental=true and bluetoothd is running with --noplugin=mcp"
say "done. Launch Scotty (the AppImage or the 'Scotty' app entry)."
