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
  // Purges expired entries. If doing so drains the set while armed, clears
  // armed_ and returns on_disarm for the caller to invoke outside mu_.
  std::function<void()> PurgeExpiredAndMaybeDisarmLocked(absl::Time now)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  absl::Mutex mu_;
  std::map<std::string, absl::Time> deadline_by_mac_ ABSL_GUARDED_BY(mu_);
  AgentArmCallbacks callbacks_ ABSL_GUARDED_BY(mu_);
  bool armed_ ABSL_GUARDED_BY(mu_) = false;
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
