# QML Tray App — RQuickShare-style UI Overhaul

Single source of truth for the UI redesign of the Nearby Share (Quick Share) Linux
tray app, aligning it with **RQuickShare v0.11.5** (reference: `~/Desktop/rquickshare`)
while keeping our richer feature set and our animated blob. QML-only unless a change
is marked **[backend]** (touches the `.so` / bazel build).

All paths under `sharing/linux/qml_tray_app/`.

---

## 1. Goals & explicit decisions (from Harsha)

- **Keep our animated blob** (`AnimatedBlob.qml`) — it stays the centerpiece of the
  idle/receive screen (RQuickShare uses a plain dot; ours is nicer). Big blob is fine.
- **Send-file initiator lives in the LEFT sidebar**, beneath the visibility card
  (so the blob stays prominent in the main area). Not a big card in the main area.
- **Keep drop-to-send** (whole-window `DropArea`) — **multi-file** (`switchToSendModeWithFiles`).
- **Visibility control** must be real & working. Backend supports it; UI + wiring needed. **[backend]**
- **Keep the QR** somewhere small but sensible — non-functional now, wire up later.
- **Copy RQuickShare's icons** (device glyphs, gear, PIN lock, completion check) — they
  are inline **Material Symbols** SVGs; embed as our own assets.
- Version label: **omitted**. Send-mode action: single **Cancel**. QR panel: **not** in
  the main send column (moved small). Incoming rows: display-only until Accept/Decline
  backend exists.

---

## 2. Design system

| Token | Value |
|---|---|
| `bg` | `#f0fdf4` (window/header/sidebar) |
| `surface` | `#ffffff` (panel, cards, modal, avatars) |
| `rowFill` / hover | `#dcfce7` / `#c9f4d9` |
| `accent` | `#10b981` → `#059669` → `#047857` |
| `accentGreen` (success) | `#16a34a` |
| `danger` | `#ef4444` |
| `border` | `#bbf7d0` / `#e5e7eb` |
| `textPrimary` / `textMuted` | `#111827` / `#6b7280` |

Radii: panel top-left `48`; modal/cards `16–22`; rows `18`; controls `8–12`.
Fonts: device name `22 Medium`; titles `20 Medium`; row name `16 Medium`; body `13`;
caption `11–12`. Spacing unit `8`.

---

## 3. Layout

```
┌──────────────────────────────────────────────┐ bg
│ Device name                         ⚙(svg)    │ AppHeader
│ laptop                                         │
├───────────────┬───────────────────────────────┤
│ SideBar 280px │ Main panel (white, topLeft=48)│
│  green        │                               │
└───────────────┴───────────────────────────────┘
```

---

## 4. Header — `components/AppHeader.qml`
Device name + gear. Gear uses the copied **Material gear SVG** (from Heading.vue).
No version label.

---

## 5. Sidebar — `components/SideBar.qml`

### Receive/idle mode (top → bottom)
1. **Visibility** section — "Visibility state" label + a real selector row
   (`Everyone` / `Contacts` / `Hidden`) with chevron, opening a small menu.
   **[backend]** wire to `visibility` property (see §9).
2. Short description text for the current visibility.
3. **NEW: "Send files" initiator** (moved here from the main area) — a button /
   compact drop target that opens the multi-file dialog (`switchToSendModeWithFiles`).
   This is the primary way to start a send while the blob keeps the main area.
4. This-session transfer history (our addition) — unchanged.

### Send mode
- "Sharing N file(s)" + file thumb + name(s) + help text.
- **Small QR** (compact `SendUrlPanel`) — sensible, unobtrusive; non-functional
  placeholder for now.
- Single **Cancel** at the bottom (leaves send mode; clears finished if idle).

---

## 6. Main panel — `FileShareTray.qml`

### Idle (receive, no transfers)
- **`AnimatedBlob` (big)** + "Ready to receive". **Kept.** No drop card here anymore
  (send-initiator moved to sidebar). Whole-window `DropArea` (multi-file) still active.

### Send mode / active
- "Nearby devices" title + scrollable list of full-width **`DeviceRow`s**. QR removed
  from this column (now small in the sidebar).

---

## 7. Device/transfer row — `components/DeviceRow.qml` (NEW)

Full-width green row (radius 18): avatar (device SVG icon) + name + inline status +
trailing actions. State-driven (send target / outgoing / incoming). See table:

| State | Status text | Trailing |
|---|---|---|
| Send target idle | "Tap to send" | click row → send |
| Outgoing active | "Sending… N%" | — |
| Outgoing done | "Sent" | — |
| Outgoing failed | "Couldn't send — tap to retry" | — |
| Incoming active | "Receiving… N%" | — |
| Incoming done | "Received <file>" + "Saved to <path>" | Open · Clear |
| Incoming failed | "Unexpected disconnection" | Clear |

Wiring: `sendPendingFileToTarget(id)`, `openFileLocation(filePath)`, `clearTransfers()`.
Avatar icon: our `ShareTarget` model has **no deviceType** field, so all rows use the
generic **"devices" SVG**; per-type (laptop/phone/tablet) needs a `deviceType` on the
model **[backend, minor]**. Completion shows the **double-check SVG**.

---

## 8. Icons — copied from RQuickShare (Material Symbols, `viewBox="0 -960 960 960"`)

Store as SVG files under `qml_tray_app/icons/` and add to `resources_file_share.qrc`
(`/icons` prefix). Render via `Image { source: "qrc:/icons/<name>.svg" }` (Qt SVG),
recolored by wrapping/ colorization where needed.

| File | Source (rquickshare) | Use |
|---|---|---|
| `dev_laptop.svg` | ItemSide.vue Laptop path | device row (future per-type) |
| `dev_phone.svg` | ItemSide.vue Phone path | device row (future per-type) |
| `dev_tablet.svg` | ItemSide.vue Tablet path | device row (future per-type) |
| `dev_generic.svg` | ItemSide.vue fallback path | **device row avatar (default)** |
| `check_double.svg` | ItemSide.vue Finished path | completed transfer |
| `pin_lock.svg` | ItemSide.vue pin path | PIN display |
| `gear.svg` | Heading.vue settings path | header settings button |

Path data is captured verbatim from those Vue files (Apache/Material — attribution
kept in the SVG comment).

---

## 9. [backend] Visibility — real control

Backend fully supports it; only the Linux `.so`/controller don't expose it yet:
- `NearbySharingSettings::GetVisibility()/SetVisibility()`, `service->SetVisibility(vis, expiration, cb)`.
- `proto::DeviceVisibility`: `EVERYONE`, `ALL_CONTACTS`, `SELECTED_CONTACTS`, `HIDDEN`,
  `SELF_SHARE`, `UNSPECIFIED`.

Work:
1. **`NearbySharingApi`** (`sharing/linux/nearby_sharing_api.{h,cc}`): add
   `void SetVisibility(int)` + `int GetVisibility()` mapping to `proto::DeviceVisibility`
   (Everyone/Contacts/Hidden — SELECTED_CONTACTS/SELF_SHARE not needed for local UI).
   It already sets visibility internally on start; expose a setter/getter. Rebuild `.so`.
2. **Controller** (`file_share_tray_controller.{h,cc}`): `Q_PROPERTY(int visibility …)`
   + getter/setter delegating to the api; NOTIFY on change.
3. **QML** (`SideBar.qml`): visibility selector bound to `fileShareController.visibility`.

Until (1)+(2) land, the selector is inert UI.

---

## 10. [backend] Other deferred backend items
- **Accept/Decline** on incoming (`AwaitingLocalConfirmation`): controller auto-accepts;
  add `acceptTransfer(id)`/`declineTransfer(id)` invokables → render buttons on the row.
- **Per-transfer Cancel**: no invokable today.
- **`deviceType`** on `ShareTarget` → per-type row icon.

---

## 11. Implementation phases & status

| # | Item | Files | Status |
|---|---|---|---|
| 1 | Settings Drawer→modal | SettingsPanel.qml | **done** |
| 2 | DeviceRow component | DeviceRow.qml (new) | **done** |
| 3 | Send-mode rows, drop QR from main | FileShareTray.qml | **done** |
| 4 | Single Cancel | SideBar.qml | **done** |
| 5 | Keep big blob; move send-initiator to sidebar | FileShareTray.qml, SideBar.qml | **done** |
| 6 | Icons copied + wired (row avatar, gear) | icons/*.svg, qrc, DeviceRow, AppHeader | **done** |
| 7 | Small QR in sidebar send mode | SideBar.qml, SendUrlPanel.qml | **done** |
| 8 | **[backend]** Visibility control | nearby_sharing_api, controller, SideBar | **done** |
| 9 | Accept/Decline, per-type deviceType icons, per-transfer cancel | controller + state + QML | **done** |

**Phase 9 detail (done, built + deployed):** controller invokables `acceptTransfer`/
`declineTransfer`/`cancelTransfer` → existing `NearbySharingApi::Accept/Reject/Cancel`
(no `.so` change). `deviceType` plumbed: `AddOrUpdateTarget(..., int device_type=-1)`
(−1 preserves prior type), fed from `ShareTargetInfo::device_type`; DeviceRow picks
`dev_phone/tablet/laptop/generic.svg` by ShareTargetType (1/5 phone, 2 tablet, 3 laptop).
DeviceRow shows Accept/Decline (incoming `AwaitingLocalConfirmation`), Cancel (active),
Open/Clear (done). Incoming now renders as **rows**: main panel shows the row list when
`isSendMode || transfers.length>0`; blob only when idle+no transfers; each DeviceRow
self-hides in receive mode unless it has a transfer (`visible: canSend || hasTransfer`).
Accept/Decline only matter when auto-accept is OFF (Settings) — else the controller
auto-accepts at `AwaitingLocalConfirmation`.

**Phase 8 detail (done, built + deployed):** `NearbySharingApi::Set/GetVisibility(int)`
(0=Everyone/1=Contacts/2=Hidden) storing a mode used by `StartReceiveMode` (was
hardcoded EVERYONE) and re-applied live when receiving. Controller exposes
`Q_PROPERTY(int visibility)` (persisted via QSettings, applied on service init).
SideBar has a real selector (Everyone/Contacts/Hidden menu) bound to it.
**Note for §9:** `ShareTargetInfo` already carries `device_type`, and `Accept`/`Reject`/
`Cancel` already exist on `NearbySharingApi` — so those follow-ups are QML+controller
wiring only, no new `.so` surface.

**Remaining icon note:** `pin_lock.svg` and per-type `dev_laptop/phone/tablet.svg`
are committed as assets but not yet placed in the UI (PIN display + per-type row
icon are follow-ups; generic device icon is wired).

---

## 12. Build & deploy

QML-only phases: **no** `.so` rebuild.
```
cmake <repo>/sharing/linux/qml_tray_app -DNEARBY_PREFIX=~/Desktop/nearby-build-out/nearby-prefix
# build → cp <build>/nearby_qml_file_tray_app ~/.local/bin/  (md5-verify; pkill breaks chained cp)
```
Adding SVGs to `.qrc` needs a cmake re-run (resource regen). `.so` phases (§9/§10) =
`bazel build //sharing/linux:nearby_sharing_api_shared` + copy the `.so` and any changed
`nearby_sharing_api.h` into the prefix. **Launch/test is done by the user in their own
session** — never launch/kill GUI apps from the headless terminal.
```
