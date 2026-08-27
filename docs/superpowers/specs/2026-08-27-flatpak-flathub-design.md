# Scotty on Flathub — design

Status: approved design, 2026-08-27. Target: publishable, source-built Flatpak
(`dev.scotty.Scotty`) suitable for Flathub, with silent background auto-update
(the reason to move off deb/AppImage — matches LocalSend/rquickshare/Packet).

## Background / what the spikes settled

- **Bluetooth in the sandbox works** — `--allow=bluetooth` lifts the seccomp block
  on `AF_BLUETOOTH`; RFCOMM + L2CAP both function. QR + on/off-wifi transfers
  proven in the earlier Flatpak spike (bundled-host-`.so` route).
- **Offline in-sandbox bazel build works** (2026-08-27 spike). `bazel vendor`
  produces a 594 MB vendor dir; a fresh-output_base build under a no-network
  namespace + dead proxy + empty repo cache completed all 1731 actions and
  produced the real 48 MB `libnearby_sharing_api_shared.so`. This means Flathub's
  "no network during build" rule is satisfiable — we do **not** need LocalSend's
  prebuilt-binary escape hatch.
- Building the `.so` **in-sandbox** links it against the runtime's own libs
  (`libcurl`, `libqrencode`, `libsystemd`, `libbluetooth` are all present in
  `org.kde.Platform//6.9`), which dissolves most of the spike's "bundle host
  libraries" list — that list was a host-ABI mismatch that disappears when the
  object is compiled against the runtime.

## Architecture

Two build artifacts, same as deb: the bazel engine `.so`, then the CMake/QML
tray app that loads it.

```
sources: repo tree + vendor dir (594MB tarball) + pinned bazel binary
  │
  ├─ module bazel            → install pinned bazel 9.2.0 static to /app/bin (build-only)
  ├─ module qrencode/…       → ONLY libs genuinely absent from the runtime (from source)
  ├─ module nearby-engine    → bazel build --vendor_dir --repository_cache= (NO network)
  │                            → /app/lib/libnearby_sharing_api_shared.so + header
  └─ module tray-app (cmake) → -DNEARBY_PREFIX=/app  → /app/bin/scotty + QML + desktop/icon/metainfo
```

### Manifest `dev.scotty.Scotty.yaml`
- `runtime: org.kde.Platform` (Qt6/QML). Build/test on **6.9** (installed); bump to
  Flathub's newest 6.x before submission. `sdk: org.kde.Sdk`.
- **finish-args** (scoped, no broad system-bus):
  - `--allow=bluetooth`
  - `--device=all` (BT adapter + GPU; tighten later if possible)
  - `--share=network`, `--share=ipc`
  - `--socket=wayland`, `--socket=fallback-x11`
  - `--system-talk-name=org.bluez`
  - `--system-talk-name=org.freedesktop.NetworkManager`
  - `--talk-name=org.freedesktop.Notifications`
  - `--talk-name=dev.scotty.MyDevices1`  (out-of-process plugin seam; plugin ships
    separately and is ToS-grey → never on Flathub, that is fine)
  - `--filesystem=xdg-download`  (save path)

### Decisions (approved)
1. **Vendor dir delivery** — tar the 594 MB vendor dir, attach to a GitHub release,
   reference by `url` + `sha256`. During local dev the manifest points at a local
   `path:` archive; swap to url+sha256 for submission.
2. **Bazel** — pinned 9.2.0 static binary as a checksummed source, plus a committed
   **`.bazelversion`** (its absence made bazelisk try to self-download in the spike;
   with it, the in-sandbox build never reaches out).

### Build command (engine module, proven offline)
```
bazel --output_base=$PWD/.bo build \
  --vendor_dir=vendor --repository_cache= \
  --check_visibility=false --spawn_strategy=standalone \
  --cxxopt=-std=c++20 --host_cxxopt=-std=c++20 \
  --@com_google_protobuf//bazel/toolchains:prefer_prebuilt_protoc=true \
  --copt=-fvisibility=hidden --cxxopt=-fvisibility-inlines-hidden \
  //sharing/linux:nearby_sharing_api_shared
```
`HOME` must be writable inside the module build dir; no `--share=network` in
build-args.

## Test plan
1. `flatpak-builder --force-clean --repo=repo build-dir dev.scotty.Scotty.yaml`
   — sources fetched, then **build runs with no network**. This is the one
   remaining unknown: bazel building against the **KDE SDK gcc** (spike used host
   gcc). Medium risk.
2. `flatpak-builder --run build-dir … scotty` / `flatpak install` + `flatpak run`
   — launch, verify tray renders (GPU), BlueZ + NetworkManager reachable.
3. Live: QR + off-wifi transfer to the Samsung S26U (needs the phone).

## Open items / risks
- **SDK-toolchain build** (#1 above) — the real thing to prove next.
- **594 MB vendor source** — chunky for Flathub review; acceptable but reviewers
  may ask. Alternative later: prune the vendor dir to only-what-is-fetched.
- **Missing runtime libs** — discovered during the build; add from-source modules
  only as needed (do not pre-bundle).
- **Runtime version bump** to Flathub-current before the submission PR.
- Not in scope here: the grey My-Devices plugin packaging (separate flatpak);
  multi-adapter/hotplug BT selection (separate feature).
