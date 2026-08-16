# AppImage Self-Install as User Service — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On first run the Scotty AppImage installs itself as a per-user systemd background service (starts every login, auto-restarts, appears in the app grid), and a post-install launch pokes the service and returns the terminal instead of hanging.

**Architecture:** Extract the install logic from `AppRun` into a sourceable pure-function library (`scotty-install-lib.sh`) so it is unit-testable against a fake `$HOME`. `AppRun` sources it and calls `scotty_install_or_update` on first run, then branches: the service invocation (`SCOTTY_SKIP_SETUP=1`) falls through to `exec scotty`; a bare launch installs-then-exits. A plain shell test harness drives the library functions in a temp `$HOME` with `systemctl`/`update-desktop-database` stubbed on `PATH`.

**Tech Stack:** Bash, systemd `--user` units, XDG desktop entries, AppImage (`--appimage-mount`/`$APPIMAGE`), plain shell test runner (no bats dependency).

**Spec:** `docs/superpowers/specs/2026-08-16-appimage-self-install-service-design.md`

## Global Constraints

- All install actions are **user-level, no root** (`$HOME/.local`, `$HOME/.config`). Never call `pkexec`/`sudo` from the install path.
- Install location: `~/.local/lib/scotty/Scotty.AppImage`. Launcher: `~/.local/share/applications/dev.scotty.Scotty.desktop`. Icon: `~/.local/share/icons/hicolor/256x256/apps/dev.scotty.Scotty.png`. Unit: `~/.config/systemd/user/scotty.service`.
- The systemd unit's `ExecStart` runs the installed AppImage **with `Environment=SCOTTY_SKIP_SETUP=1`** — the service must never re-trigger first-run setup/pkexec.
- Reference the AppImage by **absolute path** in both `.desktop` `Exec=` and unit `ExecStart=` — never a bare name (`~/.local/bin` may not be on PATH).
- Update rule: newer source mtime wins → overwrite installed copy + `systemctl --user restart scotty`.
- Idempotent: re-running from anywhere with an equal-or-older mtime is a no-op (ensure service up, exit).
- The running AppImage's own path is `${APPIMAGE:-$(readlink -f "$0")}` — `$APPIMAGE` is set by the runtime and points at the real `.AppImage` file, not the FUSE mount.

---

### Task 1: Install library scaffold + path resolution

**Files:**
- Create: `sharing/linux/qml_tray_app/packaging/scotty-install-lib.sh`
- Create: `sharing/linux/qml_tray_app/packaging/tests/test-install.sh`
- Create: `sharing/linux/qml_tray_app/packaging/tests/stubs/systemctl`
- Create: `sharing/linux/qml_tray_app/packaging/tests/stubs/update-desktop-database`

**Interfaces:**
- Produces: `scotty_paths()` sets globals `SCOTTY_LIB_DIR`, `SCOTTY_APPIMG_DST`, `SCOTTY_DESKTOP_DST`, `SCOTTY_ICON_DST`, `SCOTTY_UNIT_DST` from `$HOME`. `scotty_self_path()` echoes the running AppImage absolute path (`${APPIMAGE:-$(readlink -f "$0")}`).

- [ ] **Step 1: Write the failing test**

```bash
# tests/test-install.sh
#!/usr/bin/env bash
set -u
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LIB="$HERE/../scotty-install-lib.sh"
PASS=0; FAIL=0
ok()  { if eval "$2"; then echo "ok   - $1"; PASS=$((PASS+1)); else echo "FAIL - $1"; FAIL=$((FAIL+1)); fi; }

setup_home() { TESTHOME="$(mktemp -d)"; export HOME="$TESTHOME"; PATH="$HERE/stubs:$PATH"; }
teardown()   { rm -rf "$TESTHOME"; }

# --- Task 1 ---
setup_home
source "$LIB"
scotty_paths
ok "lib dir under HOME"     '[ "$SCOTTY_LIB_DIR" = "$HOME/.local/lib/scotty" ]'
ok "unit path correct"      '[ "$SCOTTY_UNIT_DST" = "$HOME/.config/systemd/user/scotty.service" ]'
ok "desktop path correct"   '[ "$SCOTTY_DESKTOP_DST" = "$HOME/.local/share/applications/dev.scotty.Scotty.desktop" ]'
teardown

echo "== $PASS passed, $FAIL failed =="
[ "$FAIL" -eq 0 ]
```

Also create the two stubs so later tasks can assert calls:

```bash
# tests/stubs/systemctl
#!/usr/bin/env bash
echo "systemctl $*" >> "${SCOTTY_TEST_LOG:-/dev/null}"
exit 0
```
```bash
# tests/stubs/update-desktop-database
#!/usr/bin/env bash
echo "update-desktop-database $*" >> "${SCOTTY_TEST_LOG:-/dev/null}"
exit 0
```

- [ ] **Step 2: Run test to verify it fails**

Run: `chmod +x sharing/linux/qml_tray_app/packaging/tests/stubs/*; bash sharing/linux/qml_tray_app/packaging/tests/test-install.sh`
Expected: FAIL — `scotty-install-lib.sh` does not exist / `scotty_paths: command not found`.

- [ ] **Step 3: Write minimal implementation**

```bash
# scotty-install-lib.sh — sourceable; NO side effects at source time.
scotty_paths() {
  SCOTTY_LIB_DIR="$HOME/.local/lib/scotty"
  SCOTTY_APPIMG_DST="$SCOTTY_LIB_DIR/Scotty.AppImage"
  SCOTTY_DESKTOP_DST="$HOME/.local/share/applications/dev.scotty.Scotty.desktop"
  SCOTTY_ICON_DST="$HOME/.local/share/icons/hicolor/256x256/apps/dev.scotty.Scotty.png"
  SCOTTY_UNIT_DST="$HOME/.config/systemd/user/scotty.service"
}
scotty_self_path() { readlink -f "${APPIMAGE:-$0}"; }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `bash sharing/linux/qml_tray_app/packaging/tests/test-install.sh`
Expected: PASS (`3 passed, 0 failed`).

- [ ] **Step 5: Commit**

```bash
git add sharing/linux/qml_tray_app/packaging/scotty-install-lib.sh sharing/linux/qml_tray_app/packaging/tests/
git commit -m "feat(packaging): install-lib scaffold + path resolution"
```

---

### Task 2: Self-copy AppImage with newer-mtime update

**Files:**
- Modify: `sharing/linux/qml_tray_app/packaging/scotty-install-lib.sh`
- Modify: `sharing/linux/qml_tray_app/packaging/tests/test-install.sh`

**Interfaces:**
- Consumes: `scotty_paths` globals.
- Produces: `scotty_needs_copy SRC` → returns 0 if the installed copy is missing OR `SRC` is newer than `SCOTTY_APPIMG_DST`. `scotty_copy_appimage SRC` → installs `SRC` to `SCOTTY_APPIMG_DST` (0755), creating `SCOTTY_LIB_DIR`. Echoes `copied` on write, `current` on skip.

- [ ] **Step 1: Write the failing test**

```bash
# append to test-install.sh before the summary line
setup_home; source "$LIB"; scotty_paths
SRC="$(mktemp)"; printf 'v1' > "$SRC"
ok "needs copy when absent"  'scotty_needs_copy "$SRC"'
ok "copy reports copied"     '[ "$(scotty_copy_appimage "$SRC")" = "copied" ]'
ok "installed file exists"   '[ -x "$SCOTTY_APPIMG_DST" ]'
ok "no copy when same mtime" 'touch -r "$SCOTTY_APPIMG_DST" "$SRC"; ! scotty_needs_copy "$SRC"'
ok "newer src needs copy"    'touch -d "+1 hour" "$SRC"; scotty_needs_copy "$SRC"'
teardown
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash sharing/linux/qml_tray_app/packaging/tests/test-install.sh`
Expected: FAIL — `scotty_needs_copy: command not found`.

- [ ] **Step 3: Write minimal implementation**

```bash
scotty_needs_copy() {  # $1 = source path
  [ -f "$SCOTTY_APPIMG_DST" ] || return 0
  [ "$1" -nt "$SCOTTY_APPIMG_DST" ]
}
scotty_copy_appimage() {  # $1 = source path
  if scotty_needs_copy "$1"; then
    mkdir -p "$SCOTTY_LIB_DIR"
    install -m 0755 "$1" "$SCOTTY_APPIMG_DST"
    echo copied
  else
    echo current
  fi
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `bash sharing/linux/qml_tray_app/packaging/tests/test-install.sh`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A sharing/linux/qml_tray_app/packaging
git commit -m "feat(packaging): self-copy AppImage with newer-mtime update"
```

---

### Task 3: Desktop entry + icon install (Exec rewritten to abs path)

**Files:**
- Modify: `sharing/linux/qml_tray_app/packaging/scotty-install-lib.sh`
- Modify: `sharing/linux/qml_tray_app/packaging/tests/test-install.sh`

**Interfaces:**
- Consumes: `scotty_paths` globals; `SCOTTY_APPIMG_DST`.
- Produces: `scotty_install_desktop DESKTOP_SRC ICON_SRC` → installs the icon, writes the `.desktop` with `Exec=` set to `"$SCOTTY_APPIMG_DST" %U` and `Icon=` set to `SCOTTY_ICON_DST`, then runs `update-desktop-database` on the applications dir.

- [ ] **Step 1: Write the failing test**

```bash
setup_home; source "$LIB"; scotty_paths
mkdir -p "$SCOTTY_LIB_DIR"; : > "$SCOTTY_APPIMG_DST"
DSRC="$(mktemp)"; printf '[Desktop Entry]\nType=Application\nName=Scotty\nExec=scotty %%U\nIcon=dev.scotty.Scotty\n' > "$DSRC"
ISRC="$(mktemp)"; printf 'PNG' > "$ISRC"
export SCOTTY_TEST_LOG="$(mktemp)"
scotty_install_desktop "$DSRC" "$ISRC"
ok "desktop installed"    '[ -f "$SCOTTY_DESKTOP_DST" ]'
ok "Exec is abs path"     'grep -qxF "Exec=$SCOTTY_APPIMG_DST %U" "$SCOTTY_DESKTOP_DST"'
ok "Icon is abs path"     'grep -qxF "Icon=$SCOTTY_ICON_DST" "$SCOTTY_DESKTOP_DST"'
ok "icon copied"          '[ -f "$SCOTTY_ICON_DST" ]'
ok "desktop-db refreshed" 'grep -q update-desktop-database "$SCOTTY_TEST_LOG"'
teardown
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash sharing/linux/qml_tray_app/packaging/tests/test-install.sh`
Expected: FAIL — `scotty_install_desktop: command not found`.

- [ ] **Step 3: Write minimal implementation**

```bash
scotty_install_desktop() {  # $1 = .desktop template, $2 = icon png
  install -Dm 0644 "$2" "$SCOTTY_ICON_DST"
  mkdir -p "$(dirname "$SCOTTY_DESKTOP_DST")"
  sed -e "s|^Exec=.*|Exec=$SCOTTY_APPIMG_DST %U|" \
      -e "s|^Icon=.*|Icon=$SCOTTY_ICON_DST|" \
      "$1" > "$SCOTTY_DESKTOP_DST"
  chmod 0644 "$SCOTTY_DESKTOP_DST"
  update-desktop-database "$(dirname "$SCOTTY_DESKTOP_DST")" 2>/dev/null || true
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `bash sharing/linux/qml_tray_app/packaging/tests/test-install.sh`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A sharing/linux/qml_tray_app/packaging
git commit -m "feat(packaging): install desktop entry + icon with abs-path Exec"
```

---

### Task 4: systemd --user unit write + enable/restart

**Files:**
- Modify: `sharing/linux/qml_tray_app/packaging/scotty-install-lib.sh`
- Modify: `sharing/linux/qml_tray_app/packaging/tests/test-install.sh`

**Interfaces:**
- Consumes: `SCOTTY_APPIMG_DST`, `SCOTTY_UNIT_DST`.
- Produces: `scotty_write_unit` → writes the `scotty.service` unit (idempotent; returns 0 if content unchanged, 1 if it wrote/changed). `scotty_activate_unit CHANGED` → `daemon-reload` always; `enable --now scotty.service`; if `CHANGED`=`changed` also `restart scotty.service`. All `systemctl --user`.

- [ ] **Step 1: Write the failing test**

```bash
setup_home; source "$LIB"; scotty_paths
mkdir -p "$SCOTTY_LIB_DIR"; : > "$SCOTTY_APPIMG_DST"
export SCOTTY_TEST_LOG="$(mktemp)"
scotty_write_unit
ok "unit written"          '[ -f "$SCOTTY_UNIT_DST" ]'
ok "ExecStart abs path"    'grep -qF "ExecStart=$SCOTTY_APPIMG_DST" "$SCOTTY_UNIT_DST"'
ok "skip-setup env set"    'grep -qxF "Environment=SCOTTY_SKIP_SETUP=1" "$SCOTTY_UNIT_DST"'
ok "restart on-failure"    'grep -qxF "Restart=on-failure" "$SCOTTY_UNIT_DST"'
ok "wantedby default"      'grep -qxF "WantedBy=default.target" "$SCOTTY_UNIT_DST"'
ok "second write no-op"    '! scotty_write_unit'
scotty_activate_unit changed
ok "daemon-reload called"  'grep -q "systemctl --user daemon-reload" "$SCOTTY_TEST_LOG"'
ok "enable --now called"   'grep -q "enable --now scotty.service" "$SCOTTY_TEST_LOG"'
ok "restart on changed"    'grep -q "restart scotty.service" "$SCOTTY_TEST_LOG"'
teardown
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash sharing/linux/qml_tray_app/packaging/tests/test-install.sh`
Expected: FAIL — `scotty_write_unit: command not found`.

- [ ] **Step 3: Write minimal implementation**

```bash
scotty_write_unit() {
  mkdir -p "$(dirname "$SCOTTY_UNIT_DST")"
  local new; new="$(cat <<UNIT
[Unit]
Description=Scotty — Quick Share for Linux (background service)
After=graphical-session.target
PartOf=graphical-session.target

[Service]
Type=simple
Environment=SCOTTY_SKIP_SETUP=1
ExecStart=$SCOTTY_APPIMG_DST
Restart=on-failure
RestartSec=2

[Install]
WantedBy=default.target
UNIT
)"
  if [ -f "$SCOTTY_UNIT_DST" ] && [ "$new" = "$(cat "$SCOTTY_UNIT_DST")" ]; then
    return 1   # unchanged
  fi
  printf '%s\n' "$new" > "$SCOTTY_UNIT_DST"
  return 0
}
scotty_activate_unit() {  # $1 = "changed" | "unchanged"
  systemctl --user daemon-reload 2>/dev/null || true
  systemctl --user enable --now scotty.service 2>/dev/null || true
  [ "${1:-}" = changed ] && systemctl --user restart scotty.service 2>/dev/null || true
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `bash sharing/linux/qml_tray_app/packaging/tests/test-install.sh`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A sharing/linux/qml_tray_app/packaging
git commit -m "feat(packaging): systemd --user unit write + enable/restart"
```

---

### Task 5: Orchestrator + non-systemd autostart fallback

**Files:**
- Modify: `sharing/linux/qml_tray_app/packaging/scotty-install-lib.sh`
- Modify: `sharing/linux/qml_tray_app/packaging/tests/test-install.sh`

**Interfaces:**
- Consumes: all Task 2-4 functions.
- Produces: `scotty_have_systemd_user` → 0 when `systemctl --user` is usable (stubbable via `SCOTTY_FORCE_NO_SYSTEMD=1`). `scotty_install_autostart` → writes `~/.config/autostart/dev.scotty.Scotty.desktop` (Exec = installed AppImage) as the fallback. `scotty_install_or_update SELF DESKTOP_SRC ICON_SRC` → runs copy → desktop → (unit+activate | autostart); echoes `installed`, `updated`, or `current`.

- [ ] **Step 1: Write the failing test**

```bash
# systemd path: full install reports installed, unit present
setup_home; source "$LIB"; scotty_paths
export SCOTTY_TEST_LOG="$(mktemp)"
SELF="$(mktemp)"; printf 'v1' > "$SELF"
DSRC="$(mktemp)"; printf '[Desktop Entry]\nExec=scotty %%U\nIcon=x\n' > "$DSRC"
ISRC="$(mktemp)"; printf 'PNG' > "$ISRC"
ok "first run = installed"  '[ "$(scotty_install_or_update "$SELF" "$DSRC" "$ISRC")" = installed ]'
ok "unit present"           '[ -f "$SCOTTY_UNIT_DST" ]'
ok "rerun same = current"   '[ "$(scotty_install_or_update "$SELF" "$DSRC" "$ISRC")" = current ]'
ok "newer self = updated"   'touch -d "+1 hour" "$SELF"; [ "$(scotty_install_or_update "$SELF" "$DSRC" "$ISRC")" = updated ]'
teardown
# fallback path: no systemd → autostart file, no unit
setup_home; source "$LIB"; scotty_paths
export SCOTTY_FORCE_NO_SYSTEMD=1 SCOTTY_TEST_LOG="$(mktemp)"
SELF="$(mktemp)"; printf 'v1' > "$SELF"
DSRC="$(mktemp)"; printf '[Desktop Entry]\nExec=scotty %%U\nIcon=x\n' > "$DSRC"
ISRC="$(mktemp)"; printf 'PNG' > "$ISRC"
scotty_install_or_update "$SELF" "$DSRC" "$ISRC" >/dev/null
ok "autostart written"      '[ -f "$HOME/.config/autostart/dev.scotty.Scotty.desktop" ]'
ok "no unit in fallback"    '[ ! -f "$SCOTTY_UNIT_DST" ]'
unset SCOTTY_FORCE_NO_SYSTEMD
teardown
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash sharing/linux/qml_tray_app/packaging/tests/test-install.sh`
Expected: FAIL — `scotty_install_or_update: command not found`.

- [ ] **Step 3: Write minimal implementation**

```bash
scotty_have_systemd_user() {
  [ -n "${SCOTTY_FORCE_NO_SYSTEMD:-}" ] && return 1
  command -v systemctl >/dev/null 2>&1 || return 1
  systemctl --user is-system-running >/dev/null 2>&1 || \
    [ -n "${XDG_RUNTIME_DIR:-}" ]   # tolerate 'degraded'/'starting'
}
scotty_install_autostart() {
  local d="$HOME/.config/autostart"; mkdir -p "$d"
  cat > "$d/dev.scotty.Scotty.desktop" <<AUTO
[Desktop Entry]
Type=Application
Name=Scotty
Exec=$SCOTTY_APPIMG_DST
X-GNOME-Autostart-enabled=true
Terminal=false
AUTO
}
scotty_install_or_update() {  # $1=self $2=desktop_src $3=icon_src
  scotty_paths
  local state=current
  case "$(scotty_copy_appimage "$1")" in
    copied) [ -f "$SCOTTY_UNIT_DST" ] || [ -f "$HOME/.config/autostart/dev.scotty.Scotty.desktop" ] && state=updated || state=installed ;;
  esac
  scotty_install_desktop "$2" "$3"
  if scotty_have_systemd_user; then
    local changed=unchanged
    scotty_write_unit && changed=changed
    scotty_activate_unit "$changed"
  else
    scotty_install_autostart
  fi
  echo "$state"
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `bash sharing/linux/qml_tray_app/packaging/tests/test-install.sh`
Expected: PASS (all tasks' assertions).

- [ ] **Step 5: Commit**

```bash
git add -A sharing/linux/qml_tray_app/packaging
git commit -m "feat(packaging): install orchestrator + non-systemd autostart fallback"
```

---

### Task 6: Wire AppRun — source lib, install, hand-off, --uninstall

**Files:**
- Modify: `sharing/linux/qml_tray_app/packaging/AppRun`
- Modify: `sharing/linux/qml_tray_app/packaging/build-appimage.sh` (ensure `scotty-install-lib.sh` is copied into the AppDir next to AppRun)

**Interfaces:**
- Consumes: `scotty_install_or_update`, `scotty_paths` from the bundled lib.

- [ ] **Step 1: Edit AppRun to source the lib and branch**

After `HERE=…` and BEFORE the existing BlueZ block, add:

```bash
# shellcheck source=scotty-install-lib.sh
source "$HERE/scotty-install-lib.sh"

# The systemd service launches us with SCOTTY_SKIP_SETUP=1 → skip all
# first-run/install work and run the real foreground app.
if [[ -z "${SCOTTY_SKIP_SETUP:-}" && "${1:-}" == "--uninstall" ]]; then
  scotty_paths
  systemctl --user disable --now scotty.service 2>/dev/null || true
  rm -f "$SCOTTY_UNIT_DST" "$SCOTTY_DESKTOP_DST" "$SCOTTY_ICON_DST" \
        "$HOME/.config/autostart/dev.scotty.Scotty.desktop"
  rm -rf "$SCOTTY_LIB_DIR"
  systemctl --user daemon-reload 2>/dev/null || true
  update-desktop-database "$(dirname "$SCOTTY_DESKTOP_DST")" 2>/dev/null || true
  echo "[scotty] Uninstalled (BlueZ system config left in place)."
  exit 0
fi
```

Keep the existing BlueZ setup + tile blocks (both already gated by `SCOTTY_SKIP_SETUP`). After the tile block and BEFORE the `export LD_LIBRARY_PATH` line, add the install + hand-off:

```bash
if [[ -z "${SCOTTY_SKIP_SETUP:-}" ]]; then
  SELF="$(scotty_self_path)"
  DESKTOP_SRC="$HERE/usr/share/applications/dev.scotty.Scotty.desktop"
  ICON_SRC="$HERE/usr/share/icons/hicolor/256x256/apps/dev.scotty.Scotty.png"
  STATE="$(scotty_install_or_update "$SELF" "$DESKTOP_SRC" "$ICON_SRC")"
  case "$STATE" in
    installed) echo "[scotty] Installed as a background service. Open it from your app grid or the GNOME Quick-Settings tile." >&2 ;;
    updated)   echo "[scotty] Updated the installed copy and restarted the service." >&2 ;;
  esac
  # A bare launch hands off to the service and returns the terminal —
  # single-instance means running the app here would just exit anyway.
  systemctl --user start scotty.service 2>/dev/null || true
  exit 0
fi
```

The existing tail (`export …; exec "$HERE/usr/bin/scotty" "$@"`) now runs **only** under `SCOTTY_SKIP_SETUP=1` — i.e. the service invocation — which is exactly the real foreground app.

- [ ] **Step 2: Ensure the lib ships in the AppDir**

In `build-appimage.sh`, where `AppRun` is copied into the AppDir root, copy the lib beside it. Add next to the existing AppRun copy line:

```bash
cp "$PKG_DIR/scotty-install-lib.sh" "$APPDIR/scotty-install-lib.sh"
```

(Use the same variable names the script already uses for the packaging dir and AppDir; grep for the line that copies `AppRun`.)

- [ ] **Step 3: Static-check the scripts**

Run: `bash -n sharing/linux/qml_tray_app/packaging/AppRun && bash -n sharing/linux/qml_tray_app/packaging/scotty-install-lib.sh`
Expected: no output (syntax OK). If `shellcheck` is available: `shellcheck sharing/linux/qml_tray_app/packaging/AppRun` — no errors.

- [ ] **Step 4: Full lib test suite still green**

Run: `bash sharing/linux/qml_tray_app/packaging/tests/test-install.sh`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A sharing/linux/qml_tray_app/packaging
git commit -m "feat(packaging): AppRun self-installs service, hands off, --uninstall"
```

---

### Task 7: Live hardware verification (manual, on the dev laptop)

**Files:** none (verification task; record results in `tools/hil/results/`).

- [ ] **Step 1: Build the AppImage**

Run: `sharing/linux/qml_tray_app/packaging/build-appimage.sh`
Expected: `Scotty-x86_64.AppImage` in `sharing/linux/dist/`.

- [ ] **Step 2: Uninstall any prior install, kill stray instance**

Run: `~/Desktop/scotty/sharing/linux/dist/Scotty-x86_64.AppImage --uninstall`
Then confirm: `systemctl --user status scotty.service` → `not-found`/inactive; no `scotty` in `pgrep -a scotty`.

- [ ] **Step 3: First run installs**

Run the freshly built AppImage from `~/Downloads`. Expected: BlueZ pkexec prompt (or "already present"), tile install line, then `[scotty] Installed as a background service…`, and the **terminal returns** (no hang).
Verify: `systemctl --user is-active scotty.service` → `active`; `ls ~/.local/lib/scotty/Scotty.AppImage`; app grid shows **Scotty**.

- [ ] **Step 4: Reboot persistence (the user's acceptance test)**

Reboot. After login, without launching anything: `systemctl --user is-active scotty.service` → `active`. Open the GNOME Quick-Settings tile → send/receive a file end-to-end to a phone.

- [ ] **Step 5: Record + commit results**

Write pass/fail + `systemctl --user status scotty` excerpt to `tools/hil/results/2026-08-16-self-install-service.md`.

```bash
git add tools/hil/results/2026-08-16-self-install-service.md
git commit -m "test(hil): self-install service reboot-persistence results"
```

---

### Task 8: Documentation pass (scotty-authored md only)

**Files:**
- Modify: `README.md` (root)
- Modify: `sharing/linux/qml_tray_app/packaging/README.md`
- Modify: `sharing/linux/STATUS.md`
- Modify: `sharing/linux/ROADMAP.md`

**Do NOT touch** upstream files: `connections/`, `sharing/android/`, `CODE_OF_CONDUCT.md`, `CREDITS.md`, `CONTRIBUTING.md`, `.github/` templates.

- [ ] **Step 1: Root README — fix the surface + install story**

In `README.md`: change the "Native app" line so it does not lead with "system-tray icon" as the primary surface (invisible on GNOME Wayland). Replace with the tile-first framing and note the background service:

> **Runs in the background**: installs as a per-user service that starts at login and auto-restarts. On GNOME the **Quick-Settings tile** is the main surface (the system-tray icon is used where a tray exists). "Send with Scotty" from the file manager, live speed + per-file progress.

Add a short **Install** section above **Building**:

```markdown
## Install

Download `Scotty-x86_64.AppImage` from Releases and run it once. First run
(one password prompt) applies the Bluetooth setup, installs the GNOME
Quick-Settings tile, and installs Scotty as a background user service that
starts at every login. After that it's in your app grid; the terminal
returns immediately. Remove with `./Scotty-x86_64.AppImage --uninstall`.
```

- [ ] **Step 2: packaging/README.md — replace the stale AppRun + Status text**

Update the `AppRun` bullet to: "AppImage entrypoint; on first run applies the BlueZ setup, installs the GNOME tile, and self-installs Scotty as a systemd `--user` service (`scotty.service`) + app-grid entry, then hands off. Sets Qt plugin/QML paths for the service invocation. `--uninstall` removes the user install." Add a bullet for `scotty-install-lib.sh` ("sourceable install logic; unit-tested by `tests/test-install.sh`"). Replace the **Status** section's "hasn't been run end-to-end yet" with the current reality (builds and installs end-to-end; CI release workflow still a follow-up).

- [ ] **Step 3: STATUS.md / ROADMAP.md — move packaging to done**

In `ROADMAP.md` move "packaging" from pending to done (AppImage + self-install service shipped). In `STATUS.md` add a line under the app/deploy section describing the service install + `--uninstall`.

- [ ] **Step 4: Sanity-check links + build docs unaffected**

Run: `grep -rInE '\]\(sharing/linux/(ROADMAP|STATUS)\.md\)' README.md` — links still resolve. Read each edited file once to confirm no contradiction with the shipped behavior.

- [ ] **Step 5: Commit**

```bash
git add README.md sharing/linux/qml_tray_app/packaging/README.md sharing/linux/STATUS.md sharing/linux/ROADMAP.md
git commit -m "docs: install-as-service + tile-first surface; refresh packaging status"
```

---

## Self-Review

**Spec coverage:**
- Self-copy + newer-mtime update → Task 2. ✓
- Desktop entry (abs Exec) + icon → Task 3. ✓
- systemd --user unit + `SCOTTY_SKIP_SETUP=1` + enable-now + restart-on-update → Task 4. ✓
- Orchestration + idempotency (`installed`/`updated`/`current`) → Task 5. ✓
- Non-systemd XDG autostart fallback → Task 5. ✓
- AppRun hand-off (bare launch installs+exits; service invocation execs) → Task 6. ✓
- `--uninstall` → Task 6. ✓
- Testing (fake-HOME unit + live reboot HIL) → Tasks 1-5 + Task 7. ✓
- Doc pass (scotty md only, README surface fix) → Task 8. ✓
- BlueZ setup + tile unchanged → preserved in Task 6. ✓

**Placeholder scan:** No TBD/TODO; every code step has concrete content. ✓

**Type/name consistency:** `scotty_paths`, `SCOTTY_APPIMG_DST`, `SCOTTY_UNIT_DST`, `scotty_needs_copy`, `scotty_copy_appimage`, `scotty_install_desktop`, `scotty_write_unit`, `scotty_activate_unit`, `scotty_have_systemd_user`, `scotty_install_autostart`, `scotty_install_or_update`, `scotty_self_path` — used consistently across tasks and AppRun. ✓

**Note for executor:** `scotty_install_or_update`'s `installed` vs `updated` distinction keys off whether a unit/autostart file already existed at copy time — verify the Task 5 test covers the `updated` branch (it does, via the `+1 hour` touch after a prior install).
