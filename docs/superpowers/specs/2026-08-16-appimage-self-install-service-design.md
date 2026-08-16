# AppImage self-install as a background user service

**Date:** 2026-08-16
**Status:** Approved (design), pending implementation plan
**Scope:** `sharing/linux/qml_tray_app/packaging/` (AppImage first-run path)

## Problem

The Scotty AppImage is a portable blob. Running it:

- Never "installs" anything (no launcher, no persistence). The user
  reads the first-run output and concludes "installation never
  completes".
- Ends in `exec scotty`, so a terminal launch runs the tray app in the
  **foreground and holds the terminal** — reads as a hang.
- Is a tray-only app on GNOME Wayland, which has no legacy system tray,
  so nothing visible appears. The GNOME Quick-Settings tile is the only
  surface, and it has **no backend unless the AppImage is manually
  running**.

Goal: on first run the AppImage should install itself as a proper
per-user background service (Windows Quick Share / Chromium model):
starts every login, auto-restarts on crash, appears in the app grid,
and gives the tile a persistent backend — all without root.

## Decisions (locked)

| Question | Decision |
|---|---|
| Install location | Register the blob: self-copy AppImage to `~/.local/lib/scotty/Scotty.AppImage`; launcher + icon under `~/.local/share`. Matches the project's existing `$HOME/.local` `--user` convention. Do **not** unpack the bundled Qt libs. |
| Service model | systemd **`--user`** unit, `WantedBy=default.target`, `Restart=on-failure`. Login-scoped, same context model as Windows/Chromium background autostart. |
| Process model | Run the **existing** `scotty` binary as the service. It already hosts the D-Bus backend the tile talks to; no split daemon exists in the lineage. `QLocalServer` single-instance already makes double-launch a no-op. |
| AppImage updates | Newer file wins: if the running AppImage's mtime is newer than the installed copy, overwrite it and restart the service. |
| Post-install launch | A manual/app-grid launch **pokes the service and exits** (returns the terminal), rather than foreground `exec`. Single-instance already forces exit-0 once the service holds the lock; this makes it clean and intentional. |

## What first-run installs (all user-level, no root)

1. **Self-copy** — if `$0` is not already the installed path, copy the
   AppImage to `~/.local/lib/scotty/Scotty.AppImage` (0755). Skip if an
   equal-or-newer copy already exists (mtime compare).
2. **Desktop entry** — write
   `~/.local/share/applications/dev.scotty.Scotty.desktop` from the
   packaged template, rewriting `Exec=` to the installed AppImage path
   (`…/Scotty.AppImage %U`) and `Icon=` to the installed icon. Install
   the icon to `~/.local/share/icons/hicolor/256x256/apps/`. Refresh
   with `update-desktop-database`.
3. **systemd --user unit** — write
   `~/.config/systemd/user/scotty.service`:
   - `ExecStart=%h/.local/lib/scotty/Scotty.AppImage` with
     `Environment=SCOTTY_SKIP_SETUP=1` (BT setup + tile already done;
     the service must never re-trigger pkexec).
   - `Restart=on-failure`, `RestartSec=2`.
   - `[Install] WantedBy=default.target`.
   - `systemctl --user daemon-reload` then `enable --now scotty.service`.
4. **BlueZ setup + GNOME tile** — unchanged (existing first-run steps).

Existing BlueZ `recipe_ok()` gate stays; add a parallel
`installed_ok()` gate so the whole block is idempotent and re-running
the AppImage from anywhere no-ops (except an update, which refreshes
the copy + restarts).

## AppRun control flow (post-change)

```
resolve SELF (readlink) and INSTALLED path
if SCOTTY_SKIP_SETUP set  → skip all install logic (service invocation)
else:
  run BlueZ first-run setup      (existing, pkexec, gated by recipe_ok)
  install tile                   (existing, gated)
  ensure_installed():
    if not installed OR SELF newer than INSTALLED:
        copy SELF → INSTALLED
        write/refresh .desktop + icon
        write/refresh scotty.service
        daemon-reload; enable --now; (restart if it was an update)
  # hand off:
  if running as the service (SCOTTY_SKIP_SETUP) → exec scotty   (foreground, correct)
  else  → ensure service is up, print where the app lives, EXIT (return terminal)
```

The service starts `Scotty.AppImage` **with** `SCOTTY_SKIP_SETUP=1`, so
that invocation falls through to `exec scotty` (the real foreground
app). A bare terminal/app-grid launch (no env) takes the
install-then-exit branch.

## Idempotency & lifecycle

- **Re-run from Downloads after install** → `installed_ok` true, same
  mtime → no-op, ensures service up, exits. No hang.
- **Run a newer download** → mtime newer → overwrite copy, rewrite
  unit/desktop, `systemctl --user restart scotty`.
- **Uninstall** → extend the existing `install.sh --uninstall` (and/or
  ship a `--uninstall` on AppRun): `systemctl --user disable --now
  scotty`; rm `~/.config/systemd/user/scotty.service`,
  `~/.local/lib/scotty/`, the `.desktop`, the icon; keep BlueZ config
  (shared, benign) unless `--purge`.

## Edge cases

- **No systemd --user** (rare non-systemd session): fall back to an XDG
  autostart `.desktop` in `~/.config/autostart/` (the path
  `install.sh` uninstall already anticipates). Detect via
  `systemctl --user is-system-running` reachability / `$XDG_RUNTIME_DIR`.
- **`~/.local/bin` not on PATH**: irrelevant — we reference the
  AppImage by absolute path in both `.desktop` and unit, never by bare
  name.
- **pkexec declined on first run**: BT setup fails but install of
  service/desktop/tile should still proceed (they need no root). The
  service runs; Nearby BT features degrade until setup is re-run.
- **Two AppImages of different versions launched**: single-instance
  (`QLocalServer`) keeps one process; the newer install-copy wins on
  next login/restart.

## Testing

- Unit-ish: run AppRun with a fake `$HOME`; assert copy, `.desktop`
  (Exec rewritten to abs path), and `scotty.service` land with correct
  contents; assert second run is a no-op; assert a touch-newer source
  triggers overwrite + restart call (mock `systemctl`).
- Idempotency: run twice, diff the tree — identical, no duplicate
  units.
- Live (HIL): first run → `systemctl --user status scotty` active;
  reboot/relogin → service back up; app grid shows Scotty; tile
  functional; terminal launch returns immediately.

## Non-goals

- No split receive-daemon (existing binary is the backend).
- No system-wide/multi-user install.
- No change to the transfer/BT stack, the tile UI, or the bundle
  `install.sh` path beyond the uninstall additions.
