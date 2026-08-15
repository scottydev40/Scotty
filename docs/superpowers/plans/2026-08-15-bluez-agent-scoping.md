# BlueZ Agent Scoping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop Scotty's BlueZ agent from auto-authorizing every Bluetooth request; make it approve only the peer already in an active Nearby session, armed only around a transfer, and released afterward.

**Architecture:** A new pure-logic `AgentSessionGate` owns an allowlist of expected peer MACs (with expiry) and decides arm/disarm transitions; it invokes injected arm/disarm callbacks. `AgentManager` (D-Bus) supplies those callbacks (register + RequestDefaultAgent to arm; UnregisterAgent to disarm) and the `Agent` callbacks consult the gate's `IsAllowed`. Producers on the receive path call `BeginSession`/`EndSession`. The gate is shared per-adapter (mirroring `GetSharedBluetoothDevices`) so the BLE medium — where the pre-pairing signal lives — can drive it.

**Tech Stack:** C++17, sdbus-c++, Abseil (absl::Mutex/Time/Duration), GoogleTest, Bazel.

**Spec:** `docs/superpowers/specs/2026-08-15-bluez-agent-scoping-design.md`

## Global Constraints

- Platform: Linux only; files live under `internal/platform/implementation/linux/`.
- Follow existing repo idioms: `absl::Mutex` + `ABSL_GUARDED_BY`, `LOG(INFO)/LOG(WARNING)`, sdbus-c++ types (`sdbus::ObjectPath`, `sdbus::Error`).
- MAC canonical form is `MacAddress::ToString()` (`internal/platform/mac_address.h`).
- Reject an agent request by throwing `sdbus::Error(sdbus::Error::Name("org.bluez.Error.Rejected"), "<reason>")` (pattern: `bluez_gatt_characteristic_server.cc:93`).
- The send path (`bluetooth_classic_medium.cc:163-206`, `Pairable=false` + stale-bond-heal) MUST remain untouched.
- Unit tests build via the `impl_test` `cc_test` target (`internal/platform/implementation/linux/BUILD:305`); add new `*_test.cc` to its `srcs`.
- Build the shared lib with `bazel build //sharing/linux:nearby_sharing_api_shared`; run unit tests with `bazel test //internal/platform/implementation/linux:impl_test`.

---

### Task 1: `AgentSessionGate` — pure allowlist + arm/disarm logic

**Files:**
- Create: `internal/platform/implementation/linux/agent_session_gate.h`
- Create: `internal/platform/implementation/linux/agent_session_gate.cc`
- Test: `internal/platform/implementation/linux/agent_session_gate_test.cc`
- Modify: `internal/platform/implementation/linux/BUILD` (add sources to `:linux` library srcs/hdrs near `bluez_agent.*` at lines ~101/~210, and add the test to `impl_test` srcs at ~305)

**Interfaces:**
- Produces:
  - `struct AgentArmCallbacks { std::function<void()> on_arm; std::function<void()> on_disarm; };`
  - `class AgentSessionGate` with:
    - `AgentSessionGate(absl::Duration session_ttl, std::function<absl::Time()> clock = absl::Now);`
    - `void SetArmCallbacks(AgentArmCallbacks callbacks);`
    - `void BeginSession(const MacAddress& peer);`
    - `void EndSession(const MacAddress& peer);`
    - `bool IsAllowed(const MacAddress& peer);`
    - `void SweepExpired();`
  - `std::shared_ptr<AgentSessionGate> GetSharedAgentSessionGate(absl::string_view adapter_object_path, absl::Duration session_ttl);`

- [ ] **Step 1: Write the failing test**

Create `internal/platform/implementation/linux/agent_session_gate_test.cc`:

```cpp
#include "internal/platform/implementation/linux/agent_session_gate.h"

#include "gtest/gtest.h"
#include "absl/time/time.h"
#include "internal/platform/mac_address.h"

namespace nearby {
namespace linux {
namespace {

MacAddress Mac(absl::string_view s) {
  MacAddress m;
  EXPECT_TRUE(MacAddress::FromString(s, m));
  return m;
}

TEST(AgentSessionGateTest, ArmsOnFirstSessionDisarmsOnLast) {
  int arms = 0, disarms = 0;
  AgentSessionGate gate(absl::Seconds(30));
  gate.SetArmCallbacks({[&] { arms++; }, [&] { disarms++; }});

  gate.BeginSession(Mac("AA:BB:CC:DD:EE:01"));
  EXPECT_EQ(arms, 1);
  EXPECT_EQ(disarms, 0);

  // Second peer: already armed, no re-arm.
  gate.BeginSession(Mac("AA:BB:CC:DD:EE:02"));
  EXPECT_EQ(arms, 1);

  // First peer ends: still one active, no disarm.
  gate.EndSession(Mac("AA:BB:CC:DD:EE:01"));
  EXPECT_EQ(disarms, 0);

  // Last peer ends: disarm.
  gate.EndSession(Mac("AA:BB:CC:DD:EE:02"));
  EXPECT_EQ(disarms, 1);
}

TEST(AgentSessionGateTest, IsAllowedOnlyForActivePeers) {
  AgentSessionGate gate(absl::Seconds(30));
  gate.BeginSession(Mac("AA:BB:CC:DD:EE:01"));
  EXPECT_TRUE(gate.IsAllowed(Mac("AA:BB:CC:DD:EE:01")));
  EXPECT_FALSE(gate.IsAllowed(Mac("AA:BB:CC:DD:EE:99")));
}

TEST(AgentSessionGateTest, ExpiryRemovesPeerAndDisarms) {
  absl::Time now = absl::UnixEpoch();
  int disarms = 0;
  AgentSessionGate gate(absl::Seconds(30), [&] { return now; });
  gate.SetArmCallbacks({[] {}, [&] { disarms++; }});

  gate.BeginSession(Mac("AA:BB:CC:DD:EE:01"));
  now += absl::Seconds(31);
  EXPECT_FALSE(gate.IsAllowed(Mac("AA:BB:CC:DD:EE:01")));

  gate.SweepExpired();
  EXPECT_EQ(disarms, 1);
}

TEST(AgentSessionGateTest, SharedGateIsPerAdapterSingleton) {
  auto a = GetSharedAgentSessionGate("/org/bluez/hci0", absl::Seconds(30));
  auto b = GetSharedAgentSessionGate("/org/bluez/hci0", absl::Seconds(30));
  EXPECT_EQ(a.get(), b.get());
}

}  // namespace
}  // namespace linux
}  // namespace nearby
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bazel test //internal/platform/implementation/linux:impl_test`
Expected: FAIL — `agent_session_gate.h` not found / undefined symbols. (Add the test to `impl_test` srcs first; it will fail to compile.)

- [ ] **Step 3: Write the header**

Create `internal/platform/implementation/linux/agent_session_gate.h`:

```cpp
#ifndef PLATFORM_IMPL_LINUX_AGENT_SESSION_GATE_H_
#define PLATFORM_IMPL_LINUX_AGENT_SESSION_GATE_H_

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "internal/platform/mac_address.h"

namespace nearby {
namespace linux {

// Callbacks fired on the empty<->non-empty transitions of the active-session
// set. on_arm runs when the first session begins; on_disarm when the last ends
// (or expires). Both run OUTSIDE the gate's lock.
struct AgentArmCallbacks {
  std::function<void()> on_arm;
  std::function<void()> on_disarm;
};

// Pure logic (no D-Bus): tracks which peer MACs Scotty is currently expecting a
// Bluetooth pairing from, so the agent can approve only those. Thread-safe.
class AgentSessionGate {
 public:
  explicit AgentSessionGate(absl::Duration session_ttl,
                            std::function<absl::Time()> clock = absl::Now);

  void SetArmCallbacks(AgentArmCallbacks callbacks);

  // Add/refresh peer as expected (deadline = now + ttl). Arms if the set was
  // empty before this call.
  void BeginSession(const MacAddress& peer);

  // Remove peer. Disarms if the set becomes empty.
  void EndSession(const MacAddress& peer);

  // True iff peer is currently expected (expired entries are purged first).
  bool IsAllowed(const MacAddress& peer);

  // Purge expired entries; disarms if the set becomes empty as a result.
  void SweepExpired();

 private:
  bool PurgeExpiredLocked(absl::Time now) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  absl::Mutex mu_;
  std::map<std::string, absl::Time> deadline_by_mac_ ABSL_GUARDED_BY(mu_);
  AgentArmCallbacks callbacks_ ABSL_GUARDED_BY(mu_);
  const absl::Duration ttl_;
  const std::function<absl::Time()> clock_;
};

// Per-adapter shared instance (mirrors GetSharedBluetoothDevices). Callbacks are
// set later by whoever owns the D-Bus agent (AgentManager).
std::shared_ptr<AgentSessionGate> GetSharedAgentSessionGate(
    absl::string_view adapter_object_path, absl::Duration session_ttl);

}  // namespace linux
}  // namespace nearby

#endif  // PLATFORM_IMPL_LINUX_AGENT_SESSION_GATE_H_
```

- [ ] **Step 4: Write the implementation**

Create `internal/platform/implementation/linux/agent_session_gate.cc`:

```cpp
#include "internal/platform/implementation/linux/agent_session_gate.h"

#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace nearby {
namespace linux {

AgentSessionGate::AgentSessionGate(absl::Duration session_ttl,
                                   std::function<absl::Time()> clock)
    : ttl_(session_ttl), clock_(std::move(clock)) {}

void AgentSessionGate::SetArmCallbacks(AgentArmCallbacks callbacks) {
  absl::MutexLock l(&mu_);
  callbacks_ = std::move(callbacks);
}

bool AgentSessionGate::PurgeExpiredLocked(absl::Time now) {
  bool removed_any = false;
  for (auto it = deadline_by_mac_.begin(); it != deadline_by_mac_.end();) {
    if (it->second <= now) {
      it = deadline_by_mac_.erase(it);
      removed_any = true;
    } else {
      ++it;
    }
  }
  return removed_any;
}

void AgentSessionGate::BeginSession(const MacAddress& peer) {
  std::function<void()> arm;
  {
    absl::MutexLock l(&mu_);
    const absl::Time now = clock_();
    PurgeExpiredLocked(now);
    const bool was_empty = deadline_by_mac_.empty();
    deadline_by_mac_[peer.ToString()] = now + ttl_;
    if (was_empty && callbacks_.on_arm) arm = callbacks_.on_arm;
  }
  if (arm) arm();
}

void AgentSessionGate::EndSession(const MacAddress& peer) {
  std::function<void()> disarm;
  {
    absl::MutexLock l(&mu_);
    deadline_by_mac_.erase(peer.ToString());
    if (deadline_by_mac_.empty() && callbacks_.on_disarm) {
      disarm = callbacks_.on_disarm;
    }
  }
  if (disarm) disarm();
}

bool AgentSessionGate::IsAllowed(const MacAddress& peer) {
  absl::MutexLock l(&mu_);
  PurgeExpiredLocked(clock_());
  return deadline_by_mac_.count(peer.ToString()) == 1;
}

void AgentSessionGate::SweepExpired() {
  std::function<void()> disarm;
  {
    absl::MutexLock l(&mu_);
    const bool was_nonempty = !deadline_by_mac_.empty();
    PurgeExpiredLocked(clock_());
    if (was_nonempty && deadline_by_mac_.empty() && callbacks_.on_disarm) {
      disarm = callbacks_.on_disarm;
    }
  }
  if (disarm) disarm();
}

std::shared_ptr<AgentSessionGate> GetSharedAgentSessionGate(
    absl::string_view adapter_object_path, absl::Duration session_ttl) {
  static absl::Mutex mu(absl::kConstInit);
  static auto* gates =
      new absl::flat_hash_map<std::string, std::weak_ptr<AgentSessionGate>>();
  absl::MutexLock l(&mu);
  const std::string key(adapter_object_path);
  auto it = gates->find(key);
  if (it != gates->end()) {
    if (auto existing = it->second.lock()) return existing;
  }
  auto gate = std::make_shared<AgentSessionGate>(session_ttl);
  (*gates)[key] = gate;
  return gate;
}

}  // namespace linux
}  // namespace nearby
```

- [ ] **Step 5: Wire BUILD**

In `internal/platform/implementation/linux/BUILD`: add `"agent_session_gate.h"` to the `:linux` `hdrs` list (near `"bluez_agent.h"`, ~line 101) and `"agent_session_gate.cc"` to its `srcs` (near `"bluez_agent.cc"`, ~line 210). Add `"agent_session_gate_test.cc"` to the `impl_test` `srcs` (~line 305). Confirm `//internal/platform:mac_address` (or whichever target exports `mac_address.h`) is in `:linux` deps; add it if missing.

- [ ] **Step 6: Run test to verify it passes**

Run: `bazel test //internal/platform/implementation/linux:impl_test`
Expected: PASS (all four `AgentSessionGateTest` cases).

- [ ] **Step 7: Commit**

```bash
git add internal/platform/implementation/linux/agent_session_gate.h \
        internal/platform/implementation/linux/agent_session_gate.cc \
        internal/platform/implementation/linux/agent_session_gate_test.cc \
        internal/platform/implementation/linux/BUILD
git commit -m "feat(linux/bt): AgentSessionGate — scoped allowlist for the pairing agent"
```

---

### Task 2: `mac_from_device_object_path` helper

**Files:**
- Modify: `internal/platform/implementation/linux/bluez.h` (add declaration near `device_object_path`, ~line 55)
- Modify: `internal/platform/implementation/linux/bluez.cc` (add definition near `device_object_path`, ~line 25)
- Test: `internal/platform/implementation/linux/bluez_test.cc` (create) OR append to an existing linux unit test; add to `impl_test` srcs

**Interfaces:**
- Produces: `std::optional<MacAddress> mac_from_device_object_path(absl::string_view object_path);` in `namespace nearby::linux::bluez`.

- [ ] **Step 1: Write the failing test**

Create `internal/platform/implementation/linux/bluez_test.cc`:

```cpp
#include "internal/platform/implementation/linux/bluez.h"

#include "gtest/gtest.h"
#include "internal/platform/mac_address.h"

namespace nearby {
namespace linux {
namespace {

TEST(BluezTest, MacFromDeviceObjectPath) {
  auto mac = bluez::mac_from_device_object_path(
      "/org/bluez/hci0/dev_74_F4_41_3F_12_D8");
  ASSERT_TRUE(mac.has_value());
  EXPECT_EQ(mac->ToString(), "74:F4:41:3F:12:D8");
}

TEST(BluezTest, MacFromDeviceObjectPathRejectsGarbage) {
  EXPECT_FALSE(bluez::mac_from_device_object_path("/org/bluez/hci0").has_value());
  EXPECT_FALSE(bluez::mac_from_device_object_path("").has_value());
  EXPECT_FALSE(
      bluez::mac_from_device_object_path("/org/bluez/hci0/dev_zz").has_value());
}

}  // namespace
}  // namespace linux
}  // namespace nearby
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bazel test //internal/platform/implementation/linux:impl_test` (after adding `bluez_test.cc` to srcs)
Expected: FAIL — `mac_from_device_object_path` undefined.

- [ ] **Step 3: Add the declaration**

In `internal/platform/implementation/linux/bluez.h`, inside `namespace bluez`, near `device_object_path`:

```cpp
// Reverse of device_object_path: extract the peer MAC from a bluez Device1
// object path like "/org/bluez/hci0/dev_74_F4_41_3F_12_D8". Returns nullopt if
// the path has no valid dev_ MAC suffix.
std::optional<MacAddress> mac_from_device_object_path(
    absl::string_view object_path);
```

Ensure `#include <optional>`, `#include "absl/strings/string_view.h"`, and `#include "internal/platform/mac_address.h"` are present in `bluez.h`.

- [ ] **Step 4: Add the implementation**

In `internal/platform/implementation/linux/bluez.cc`:

```cpp
std::optional<MacAddress> mac_from_device_object_path(
    absl::string_view object_path) {
  constexpr absl::string_view kMarker = "/dev_";
  const auto pos = object_path.rfind(kMarker);
  if (pos == absl::string_view::npos) return std::nullopt;
  std::string mac(object_path.substr(pos + kMarker.size()));
  for (char& c : mac) {
    if (c == '_') c = ':';
  }
  MacAddress parsed;
  if (!MacAddress::FromString(mac, parsed)) return std::nullopt;
  return parsed;
}
```

Add `#include <string>` / `#include "absl/strings/str_replace.h"` only if not already present (the loop above avoids needing str_replace).

- [ ] **Step 5: Run test to verify it passes**

Run: `bazel test //internal/platform/implementation/linux:impl_test`
Expected: PASS (both `BluezTest` cases).

- [ ] **Step 6: Commit**

```bash
git add internal/platform/implementation/linux/bluez.h \
        internal/platform/implementation/linux/bluez.cc \
        internal/platform/implementation/linux/bluez_test.cc \
        internal/platform/implementation/linux/BUILD
git commit -m "feat(linux/bt): mac_from_device_object_path helper"
```

---

### Task 3: Gate the `Agent` callbacks and rewire `AgentManager` to arm/disarm

**Files:**
- Modify: `internal/platform/implementation/linux/bluez_agent.h`
- Modify: `internal/platform/implementation/linux/bluez_agent.cc`

**Interfaces:**
- Consumes: `AgentSessionGate` (Task 1), `bluez::mac_from_device_object_path` (Task 2), generated `AgentManager1_proxy::UnregisterAgent` (`agentmanager_client.h:44`).
- Produces (on `AgentManager`):
  - `void SetGate(std::shared_ptr<AgentSessionGate> gate);` — also installs arm/disarm callbacks on the gate.
  - `void BeginSession(const MacAddress& peer);` / `void EndSession(const MacAddress& peer);` — delegate to the gate.
  - private `void Arm();` (register agent if needed + `RequestDefaultAgent`) and `void Disarm();` (`UnregisterAgent`).
- Produces (on `Agent`): constructor now takes `std::shared_ptr<AgentSessionGate> gate`; callbacks consult it.

Note: this task's logic runs against live BlueZ, so its verification is a build + the live tests in Tasks 5 & 6, not a new unit test. The unit-testable decision logic already lives in Tasks 1–2.

- [ ] **Step 1: Give `Agent` a gate and gate its callbacks**

In `bluez_agent.h`, add `#include <memory>` and `#include "internal/platform/implementation/linux/agent_session_gate.h"`. Change the `Agent` constructor to accept and store a gate:

```cpp
Agent(sdbus::IConnection& system_bus, sdbus::ObjectPath path,
      std::shared_ptr<AgentSessionGate> gate)
    : AdaptorInterfaces(system_bus, std::move(path)), gate_(std::move(gate)) {
  registerAdaptor();
  LOG(INFO) << "Created new Agent at path: " << getObject().getObjectPath();
}
```

Add member: `std::shared_ptr<AgentSessionGate> gate_;`

In `bluez_agent.cc`, add `#include "internal/platform/implementation/linux/bluez.h"`. Add a helper and rewrite the decision callbacks:

```cpp
namespace {
// True iff the device path's MAC is a peer we currently expect (active session).
bool DevicePathAllowed(AgentSessionGate* gate, const sdbus::ObjectPath& device) {
  if (gate == nullptr) return false;
  auto mac = bluez::mac_from_device_object_path(device);
  if (!mac.has_value()) return false;
  return gate->IsAllowed(*mac);
}
}  // namespace

void Agent::RequestConfirmation(const sdbus::ObjectPath& device,
                                const uint32_t& passkey) {
  if (DevicePathAllowed(gate_.get(), device)) {
    LOG(INFO) << "[agent] RequestConfirmation(" << device << ") -> ACCEPT";
    return;  // no throw == accept
  }
  LOG(WARNING) << "[agent] RequestConfirmation(" << device
               << ") -> REJECT (not an active Nearby peer)";
  throw sdbus::Error(sdbus::Error::Name("org.bluez.Error.Rejected"),
                     "not an active Nearby peer");
}

void Agent::RequestAuthorization(const sdbus::ObjectPath& device) {
  if (DevicePathAllowed(gate_.get(), device)) {
    LOG(INFO) << "[agent] RequestAuthorization(" << device << ") -> ACCEPT";
    return;
  }
  LOG(WARNING) << "[agent] RequestAuthorization(" << device
               << ") -> REJECT (not an active Nearby peer)";
  throw sdbus::Error(sdbus::Error::Name("org.bluez.Error.Rejected"),
                     "not an active Nearby peer");
}

void Agent::AuthorizeService(const sdbus::ObjectPath& device,
                             const std::string& uuid) {
  if (DevicePathAllowed(gate_.get(), device)) {
    LOG(INFO) << "[agent] AuthorizeService(" << device << ", " << uuid
              << ") -> ACCEPT";
    return;
  }
  LOG(WARNING) << "[agent] AuthorizeService(" << device << ", " << uuid
               << ") -> REJECT (not an active Nearby peer)";
  throw sdbus::Error(sdbus::Error::Name("org.bluez.Error.Rejected"),
                     "not an active Nearby peer");
}
```

Rewrite the credential callbacks to always reject (never hand out fixed secrets):

```cpp
std::string Agent::RequestPinCode(const sdbus::ObjectPath& device) {
  LOG(WARNING) << "[agent] RequestPinCode(" << device << ") -> REJECT";
  throw sdbus::Error(sdbus::Error::Name("org.bluez.Error.Rejected"),
                     "PIN pairing not supported");
}

uint32_t Agent::RequestPasskey(const sdbus::ObjectPath& device) {
  LOG(WARNING) << "[agent] RequestPasskey(" << device << ") -> REJECT";
  throw sdbus::Error(sdbus::Error::Name("org.bluez.Error.Rejected"),
                     "passkey pairing not supported");
}
```

Leave `Release`, `DisplayPinCode`, `DisplayPasskey`, `Cancel` as logging-only.

- [ ] **Step 2: Rewire `AgentManager` for scoped arm/disarm**

In `bluez_agent.h`, on `AgentManager`, add members and methods:

```cpp
 public:
  // Install the shared gate and wire arm/disarm to it.
  void SetGate(std::shared_ptr<AgentSessionGate> gate);
  void BeginSession(const MacAddress& peer);
  void EndSession(const MacAddress& peer);

 private:
  void Arm();     // register agent (if needed) + RequestDefaultAgent
  void Disarm();  // UnregisterAgent

  std::shared_ptr<AgentSessionGate> gate_;
  sdbus::ObjectPath agent_path_{"/com/google/nearby/bluetooth/agent"};
```

Keep the existing `registered_agents_` map/mutex for the created `Agent` instance.

In `bluez_agent.cc`, add:

```cpp
void AgentManager::SetGate(std::shared_ptr<AgentSessionGate> gate) {
  gate_ = std::move(gate);
  if (gate_ == nullptr) return;
  gate_->SetArmCallbacks({[this] { Arm(); }, [this] { Disarm(); }});
}

void AgentManager::BeginSession(const MacAddress& peer) {
  if (gate_ != nullptr) gate_->BeginSession(peer);
}

void AgentManager::EndSession(const MacAddress& peer) {
  if (gate_ != nullptr) gate_->EndSession(peer);
}

void AgentManager::Arm() {
  // Register the agent object if we have not yet, passing the gate so its
  // callbacks can consult the allowlist; then (re)claim the default slot.
  {
    absl::MutexLock l(&registered_agents_mutex_);
    const std::string key = agent_path_;
    if (registered_agents_.count(key) == 0) {
      auto agent = std::make_shared<Agent>(getProxy().getConnection(),
                                           agent_path_, gate_);
      try {
        RegisterAgent(agent->getObject().getObjectPath(), "NoInputNoOutput");
      } catch (const sdbus::Error& e) {
        LOG(ERROR) << "Arm: RegisterAgent failed: " << e.getName() << " ("
                   << e.getMessage() << ")";
        return;
      }
      registered_agents_.emplace(key, agent);
    }
  }
  try {
    RequestDefaultAgent(agent_path_);
    LOG(INFO) << "Arm: agent armed + default-agent claimed at " << agent_path_;
  } catch (const sdbus::Error& e) {
    LOG(WARNING) << "Arm: RequestDefaultAgent failed: " << e.getName() << " ("
                 << e.getMessage() << ")";
  }
}

void AgentManager::Disarm() {
  std::shared_ptr<Agent> agent;
  {
    absl::MutexLock l(&registered_agents_mutex_);
    auto it = registered_agents_.find(std::string(agent_path_));
    if (it == registered_agents_.end()) return;
    agent = it->second;
    registered_agents_.erase(it);
  }
  try {
    UnregisterAgent(agent_path_);
    LOG(INFO) << "Disarm: agent unregistered at " << agent_path_
              << "; desktop agent restored";
  } catch (const sdbus::Error& e) {
    LOG(WARNING) << "Disarm: UnregisterAgent failed: " << e.getName() << " ("
                 << e.getMessage() << ")";
  }
}
```

Keep the old `Register`/`EnsureDefaultAgent`/`AgentRegistered` methods for now only if other callers exist; Task 4 removes their call sites. If nothing else references them after Task 4, delete them there.

- [ ] **Step 3: Build**

Run: `bazel build //sharing/linux:nearby_sharing_api_shared`
Expected: compiles clean. Fix any missing includes (`internal/platform/mac_address.h` in `bluez_agent.h`).

- [ ] **Step 4: Commit**

```bash
git add internal/platform/implementation/linux/bluez_agent.h \
        internal/platform/implementation/linux/bluez_agent.cc
git commit -m "feat(linux/bt): gate agent callbacks by active-session allowlist; scoped arm/disarm"
```

---

### Task 4: Stop auto-registering the agent in the medium constructor; hand the gate to `AgentManager`

**Files:**
- Modify: `internal/platform/implementation/linux/bluetooth_classic_medium.cc:57-79` (constructor)
- Modify: `internal/platform/implementation/linux/bluetooth_classic_medium.cc:273-281` (`ListenForService` reassert block)

**Interfaces:**
- Consumes: `GetSharedAgentSessionGate` (Task 1), `AgentManager::SetGate/BeginSession/EndSession/Arm/Disarm` (Task 3).

- [ ] **Step 1: Replace the constructor's unconditional agent registration**

Replace the `if (!agent_manager_->Register(...))` block (`bluetooth_classic_medium.cc:72-78`) with wiring the shared gate — no arming at construction:

```cpp
  auto gate = GetSharedAgentSessionGate(adapter_.GetObjectPath(),
                                        absl::Seconds(30));
  agent_manager_->SetGate(gate);
```

Add `#include "internal/platform/implementation/linux/agent_session_gate.h"` and `#include "absl/time/time.h"` to `bluetooth_classic_medium.cc`.

- [ ] **Step 2: Remove the `EnsureDefaultAgent` reassert in `ListenForService`**

Delete the `if (agent_manager_ != nullptr) { agent_manager_->EnsureDefaultAgent(...); }` block (`bluetooth_classic_medium.cc:277-281`). Arming is now driven by `BeginSession` from the producer (Task 6), not by starting to listen. Leave a comment:

```cpp
  // The auto-accept agent is no longer claimed here. It is armed per incoming
  // session via AgentManager::BeginSession (see the BLE receive producer), so
  // an idle listen never holds the system default-agent slot.
```

- [ ] **Step 3: Build**

Run: `bazel build //sharing/linux:nearby_sharing_api_shared`
Expected: compiles. If `EnsureDefaultAgent`/`Register`/`AgentRegistered` are now unreferenced, remove them from `bluez_agent.{h,cc}` and rebuild.

- [ ] **Step 4: Deploy and smoke-test that nothing armed at idle**

```bash
cp bazel-bin/sharing/linux/libnearby_sharing_api_shared.so ~/.local/lib/
kill $(pgrep -f bin/scotty); sleep 2
setsid ~/.local/bin/scotty >/tmp/scotty_stdout.log 2>&1 </dev/null & disown
sleep 5
busctl --system call org.bluez /org/bluez org.bluez.AgentManager1 \
  RequestDefaultAgent o /test/probe 2>&1 || true   # optional: observe current default
grep -c "\[agent\]" /tmp/nearby_qml_file_tray.log   # expect 0 while idle
```

Expected: no `[agent]` lines and no "agent armed" while Scotty sits idle with visibility on (agent not registered until a session begins).

- [ ] **Step 5: Commit**

```bash
git add internal/platform/implementation/linux/bluetooth_classic_medium.cc \
        internal/platform/implementation/linux/bluez_agent.h \
        internal/platform/implementation/linux/bluez_agent.cc
git commit -m "refactor(linux/bt): arm the agent per session, not at medium startup"
```

---

### Task 5: Live capture — identify the pre-pairing producer signal (bonded + unbonded)

This is an investigation task (needs the phone). Its output selects the exact call site wired in Task 6. Do NOT skip: Task 4 removed the old always-on agent, so until Task 6 wires a producer, receives will be REJECTED — expected at this stage.

**Files:** none (produces findings + a one-line decision recorded at the top of Task 6).

- [ ] **Step 1: Instrument the candidate producer sites with temporary logs**

Add a `LOG(INFO) << "[producer-probe] <site> mac=" << mac` at each candidate (revert after):
- BLE device resolution during an incoming session — near `ble_gatt_client.cc:465` (`Found service path .../dev_<MAC>/...`), extract the MAC via `bluez::mac_from_device_object_path(service_path)`.
- Scotty's GATT server receiving an incoming write from a peer — `bluez_gatt_characteristic_server.cc` `WriteValue`.
- `bluetooth_classic_device.cc:199` `DeviceConnectedStateChanged`.

Rebuild + deploy the `.so`.

- [ ] **Step 2: Capture a bonded own-device receive**

```bash
: > /tmp/nearby_qml_file_tray.log
adb logcat -c
# On the phone (bonded, same account): send a file to laptop.
adb logcat -v time > /tmp/adb.receive.log &
sleep 40; kill %1
grep -nE "\[producer-probe\]|\[agent\]|paired status|ble_gatt_client" /tmp/nearby_qml_file_tray.log
```

Expected: identify which `[producer-probe]` site fires, and confirm it precedes the `[agent]` callback with the SAME MAC.

- [ ] **Step 3: Capture an unbonded "Everyone" receive**

Set visibility to Everyone; from a phone NOT on the account, send a file. Repeat the capture. Record which agent callback fires (`RequestConfirmation` expected) and whether a `[producer-probe]` MAC precedes it.

- [ ] **Step 4: Record the decision**

Write one line at the top of Task 6: "Producer seam = <file:line>, fires ~<N>s before pairing for both bonded and unbonded." If NO site reliably precedes pairing for the unbonded path, note it — Task 6 then also arms on the earliest BLE incoming-advert/scan contact for that path.

- [ ] **Step 5: Revert the probe logs**

```bash
git checkout -- <the files you added [producer-probe] logs to>
```

No commit (probes reverted).

---

### Task 6: Wire the producer — BeginSession/EndSession at the confirmed seam

> Fill in from Task 5: Producer seam = __________ .

**Files:**
- Modify: the file identified in Task 5 (default: `internal/platform/implementation/linux/ble_gatt_client.cc` at the incoming device-resolution point).
- Modify: `internal/platform/implementation/linux/bluetooth_classic_medium.h/.cc` if the producer needs to reach `agent_manager_->BeginSession` (or fetch the shared gate directly in the producer's medium).

**Interfaces:**
- Consumes: `GetSharedAgentSessionGate` (Task 1), `AgentSessionGate::BeginSession/EndSession` (Task 1).

- [ ] **Step 1: Fetch the shared gate in the producer's medium**

In the producer file's owning class (e.g. `BleV2Medium`), obtain the same shared gate (keyed by adapter path) once:

```cpp
gate_ = GetSharedAgentSessionGate(adapter_.GetObjectPath(), absl::Seconds(30));
```

Store `std::shared_ptr<AgentSessionGate> gate_;` on the class. Include `agent_session_gate.h` + `absl/time/time.h`.

- [ ] **Step 2: Call BeginSession when an incoming peer is resolved**

At the confirmed seam (default: where the peer's Nearby GATT service/device path is resolved for an incoming session), add:

```cpp
if (gate_ != nullptr) {
  if (auto mac = bluez::mac_from_device_object_path(device_object_path);
      mac.has_value()) {
    LOG(INFO) << "Arming agent for incoming Nearby peer " << mac->ToString();
    gate_->BeginSession(*mac);
  }
}
```

- [ ] **Step 3: Call EndSession on teardown**

Where that incoming BLE/GATT session closes (disconnect / socket close for the peer), add the matching `gate_->EndSession(*mac);`. If a clean teardown point is not available, rely on the TTL expiry (Task 1) — but add a `gate_->SweepExpired()` call on any subsequent session event so a stale entry cannot keep the agent armed.

- [ ] **Step 4: Build + deploy**

```bash
bazel build //sharing/linux:nearby_sharing_api_shared
cp bazel-bin/sharing/linux/libnearby_sharing_api_shared.so ~/.local/lib/
kill $(pgrep -f bin/scotty); sleep 2
setsid ~/.local/bin/scotty >/tmp/scotty_stdout.log 2>&1 </dev/null & disown
```

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(linux/bt): arm the pairing agent on an incoming Nearby session"
```

---

### Task 7: Full live verification (spec Testing section)

**Files:** none (manual verification; fixes loop back into Tasks 3/6).

- [ ] **Step 1: Bonded own-device receive**

Send from the bonded S26U. Confirm in `/tmp/nearby_qml_file_tray.log`: `BeginSession` MAC → `[agent] ... -> ACCEPT` for that MAC → transfer completes → `Disarm: agent unregistered` after. Verify the default agent reverted: `busctl --system introspect org.bluez /org/bluez` shows no `/com/google/nearby/bluetooth/agent` as default while idle.

- [ ] **Step 2: Unbonded "Everyone" receive**

Visibility=Everyone, send from an off-account phone. Confirm the fired callback (`RequestConfirmation`) is ACCEPTed because its MAC is in the allowlist, and the transfer completes.

- [ ] **Step 3: Reject test (adversarial)**

While Scotty is idle-visible (no transfer), from an unrelated phone attempt to pair with the laptop over Bluetooth settings. Expected: no scotty `[agent]` line and the pairing is handled by the desktop (GNOME) agent — proving scotty is not the default agent when idle.

- [ ] **Step 4: Concurrent peripheral pairing while idle-visible**

With visibility on but no transfer, pair a real BT peripheral (mouse/headset). Expected: succeeds normally via the desktop agent.

- [ ] **Step 5: Send path regression**

Run a Pixel off-Wi-Fi send and a Samsung off-Wi-Fi send. Expected: both still complete with no pairing prompt (send path untouched).

- [ ] **Step 6: Record results**

Append a "Live verification" note to the spec doc with pass/fail per step and any follow-up. Commit:

```bash
git add docs/superpowers/specs/2026-08-15-bluez-agent-scoping-design.md
git commit -m "docs(spec): record live verification of the BlueZ agent scoping fix"
```

---

## Self-Review

**Spec coverage:**
- Step 1 (register only while receiving) → Tasks 3–4 + 6 (arm on BeginSession, no ctor register).
- Step 2 (claim default slot around handshake) → Task 3 `Arm()` (RequestDefaultAgent) driven per session.
- Step 3 (approve only expected peer) → Tasks 1 (gate) + 2 (mac) + 3 (callback gating; PIN/passkey reject).
- Step 4 (temporary non-bonding) → send path untouched (constraint); receive keeps `Pairable=true` (no `Pairable=false` added on receive) — verified Task 7 Step 5.
- Step 5 (release + restore desktop agent) → Task 3 `Disarm()` (UnregisterAgent).
- Evidence/only-RequestAuthorization, MAC-only filter, ~0.75s margin → Tasks 5–7.
- Overlapping sessions / expiry / arm-race fail-safe → Task 1 (set + TTL) + Task 6 Step 3.

**Placeholder scan:** Task 6's seam is intentionally selected by Task 5's live capture (the spec deferred it); the call-site *code* is concrete, only the file:line is an output of the prior task. No other placeholders.

**Type consistency:** `AgentSessionGate` methods (`BeginSession`/`EndSession`/`IsAllowed`/`SweepExpired`/`SetArmCallbacks`), `AgentArmCallbacks{on_arm,on_disarm}`, `GetSharedAgentSessionGate(path, ttl)`, `bluez::mac_from_device_object_path`, and `AgentManager::{SetGate,BeginSession,EndSession,Arm,Disarm}` are used consistently across Tasks 1–6.
