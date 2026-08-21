# Scotty — install / uninstall

Scotty ships as native Debian/Ubuntu packages. Two independent things:

- **Scotty** — the app (file sharing). Everyone installs this.
- **My Devices plugin** — optional, adds Google account sign-in so your own
  devices ("Your devices") and contacts see each other. Install only if you
  want account features.

Download the `.deb` files into one folder, then run the command for what you want.

## Scotty (the app)

**Install** — one command (pulls all system dependencies automatically):

```bash
sudo apt install ./scotty_*.deb ./scotty-bluez-compat_*.deb ./gnome-shell-extension-scotty_*.deb
```

**Uninstall** — one command:

```bash
sudo apt remove scotty scotty-bluez-compat gnome-shell-extension-scotty
```

Add `--purge` to also drop system config. Your signed-in account and cached
certificates live in your home directory and are left untouched (sign out in the
app first if you want them gone).

## My Devices plugin (optional)

**Install:**

```bash
sudo apt install ./scotty-mydevices_*.deb
```

**Uninstall:**

```bash
sudo apt remove scotty-mydevices
```

The app detects the plugin automatically — no restart needed. Removing it drops
Scotty back to no-account sharing.

## Notes

- Scotty runs as a per-user service and auto-starts on login. Launch it from the
  app grid ("Scotty") or `systemctl --user start scotty`.
- The GNOME Shell tile (from `gnome-shell-extension-scotty`) may need a
  logout/login the first time to appear.
- `scotty-bluez-compat` applies the BlueZ settings Scotty needs for off-network
  and locked-screen transfers.
