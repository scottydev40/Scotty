# Scotty manual HIL matrix — 2026-08-15

Phone: Samsung SM-S948U1 (`REDACTED-SERIAL`)  
Laptop adapter: `laptop` (`AA:BB:CC:DD:EE:FF`)  
Payload: `Screenshot From 2026-06-26 20-24-43.png`, 508,584 bytes  
SHA-256: `5d85cec84525f4b7bb029cf42e0f9113bd37dab6569b01bbd88e0dae3ae45771`

The operator stopped the matrix after the MCP-OFF / Experimental=true cell.
Rows without hardware evidence are explicitly `NOT RUN`; they are not inferred.

## All transfer cases

| MCP | Experimental | Laptop Wi-Fi | Direction | Status | Evidence / reason |
|---|---:|---|---|---|---|
| OFF | true | ON | phone → laptop | **PASS** | Hash-matched payload; `WIFI_LAN`; D-Bus active true→false; zero Android SSP markers; bond count unchanged; Alias `laptop`. |
| OFF | true | ON | laptop → phone | **PASS** | Two hash-matched screenshot sends over `WIFI_LAN`; zero Android SSP markers; bond count unchanged; Alias `laptop`. Capture also exposed stale-target/retry failures between successful attempts. |
| OFF | true | OFF | phone → laptop | **FAIL** | Hash-matched payload completed over RFCOMM/`BLUETOOTH`, but Android emitted one `btif_dm_ssp_cfm_req_evt` (`just_works:true`). Pairing was auto-accepted and temporary; no persistent bond remained. |
| OFF | true | OFF | laptop → phone | **INCOMPLETE** | A hash-matched Bluetooth/RFCOMM send succeeded and emitted one temporary Just Works SSP event, but the laptop Wi-Fi radio state at connection start was not captured. This is not accepted as the formal off-Wi-Fi row. |
| OFF | false | ON | phone → laptop | **NOT RUN** | Matrix stopped before Experimental=false. |
| OFF | false | ON | laptop → phone | **NOT RUN** | Matrix stopped before Experimental=false. |
| OFF | false | OFF | phone → laptop | **NOT RUN** | Matrix stopped before Experimental=false. |
| OFF | false | OFF | laptop → phone | **NOT RUN** | Matrix stopped before Experimental=false. |
| ON | true | ON | phone → laptop | **NOT RUN** | Matrix stopped before MCP-ON cell. |
| ON | true | ON | laptop → phone | **NOT RUN** | Matrix stopped before MCP-ON cell. |
| ON | true | OFF | phone → laptop | **NOT RUN** | Matrix stopped before MCP-ON cell. |
| ON | true | OFF | laptop → phone | **NOT RUN** | Matrix stopped before MCP-ON cell. |
| ON | false | ON | phone → laptop | **NOT RUN** | Matrix stopped before MCP-ON / Experimental=false cell. |
| ON | false | ON | laptop → phone | **NOT RUN** | Matrix stopped before MCP-ON / Experimental=false cell. |
| ON | false | OFF | phone → laptop | **NOT RUN** | Matrix stopped before MCP-ON / Experimental=false cell. |
| ON | false | OFF | laptop → phone | **NOT RUN** | Matrix stopped before MCP-ON / Experimental=false cell. |

Totals: **2 PASS, 1 FAIL, 1 INCOMPLETE, 12 NOT RUN**.

## Questions answered

### Is pairing behavior limited to BlueZ's MCP plugin?

**No.** MCP was disabled for every executed sample. Bluetooth/RFCOMM transfers
still caused Android to enter BONDING, emit `btif_dm_ssp_cfm_req_evt`, and
auto-accept temporary Just Works pairing. Earlier MCP-OFF attempts also showed
the operator “couldn't pair with laptop” toasts. Wi-Fi-LAN transfers emitted no
SSP events.

The temporary Bluetooth key was explicitly not stored; Android removed Athena
and returned BONDING→NONE. Persistent bond count remained unchanged.

### Does Scotty work with Experimental=false?

**Unresolved.** No Experimental=false cell was run. Do not infer support from
the Experimental=true results; the L2CAP-CoC and AdvertisementMonitor runtime
gates still need a dedicated check if this question becomes important.

## Stable findings

- MCP OFF / Experimental=true supports successful, hash-verified transfers in
  both directions over Wi-Fi LAN.
- The same configuration successfully carries a phone-to-laptop payload over
  Bluetooth Classic/RFCOMM with laptop Wi-Fi disabled, but violates the strict
  zero-SSP condition.
- No tested transfer added a persistent phone bond for Athena.
- BlueZ adapter Alias remained exactly `laptop` throughout; no rotating Nearby
  name leaked into the adapter Alias.
- The formal off-Wi-Fi receive payload completed at 23:40:56. GNOME Shell
  re-enabled laptop Wi-Fi at 23:40:59, after completion, and NetworkManager
  rejoined `1503vista` at 23:41:04.
- Scotty's advertising summary sometimes omitted `BLUETOOTH` even while it
  separately published an RFCOMM SDP record and successfully accepted a
  Classic connection. RFCOMM registration/acceptance is the authoritative
  runtime evidence.

## Reliability and UI observations

- Discovery was sometimes delayed; targets repeatedly disappeared.
- The send UI produced a nonfunctional “tap to retry” state.
- One outgoing action failed with `Unknown ShareTarget`; another remained
  active without payload completion until Scotty was restarted.
- Scotty repeatedly received BlueZ `ServiceData` `InvalidArgs` errors for
  rotating device objects. This is a candidate contributor to stale discovery.
- Signed-in self-share certificates worked with the Samsung locked.
- Product idea retained from the operator: split the signed-in device area into
  horizontal **My devices** and **Nearby devices** sections.

## Evidence index

- Formal Wi-Fi-on phone→laptop PASS:
  `/tmp/scotty-manual-20260815T233613-0500/mcp-off_experimental-true/wifi-on/phone-to-laptop/result.md`
- Formal Wi-Fi-off phone→laptop FAIL:
  `/tmp/scotty-manual-20260815T234006-0500/mcp-off_experimental-true/wifi-off/phone-to-laptop/result.md`
- Outgoing Wi-Fi samples and direction-mismatch capture:
  `/tmp/scotty-manual-20260815T225708-0500/mcp-off_experimental-true/wifi-on/phone-to-laptop/result.md`
- Partial early incoming capture:
  `/tmp/scotty-manual-20260815T230650-0500/mcp-off_experimental-true/wifi-on/phone-to-laptop/result.md`
- Consolidated operator chronology:
  `/tmp/scotty-hil-20260816T031038Z/operator-notes.md`

## Final machine state

- Wi-Fi: enabled and connected to `1503vista`.
- Bluetooth: powered.
- Adapter Alias: `laptop`.
- Captures: stopped; no ADB logcat or D-Bus monitor left running.
- BlueZ configuration: unchanged by the manual phase; current starting state
  remains MCP OFF and Experimental=true.
- Scotty: left running as exact PID `56778`; it was not killed because closing
  the matrix did not request closing the application.
