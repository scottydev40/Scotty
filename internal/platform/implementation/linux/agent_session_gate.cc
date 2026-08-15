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

std::function<void()> AgentSessionGate::PurgeExpiredAndMaybeDisarmLocked(
    absl::Time now) {
  for (auto it = deadline_by_mac_.begin(); it != deadline_by_mac_.end();) {
    if (it->second <= now) {
      it = deadline_by_mac_.erase(it);
    } else {
      ++it;
    }
  }
  if (armed_ && deadline_by_mac_.empty()) {
    armed_ = false;
    return callbacks_.on_disarm;
  }
  return nullptr;
}

void AgentSessionGate::BeginSession(const MacAddress& peer) {
  std::function<void()> arm;
  {
    absl::MutexLock l(&mu_);
    const absl::Time now = clock_();
    // Purge without disarming: we're about to (re-)populate the set below,
    // so any transition here is superseded by the insert.
    for (auto it = deadline_by_mac_.begin(); it != deadline_by_mac_.end();) {
      if (it->second <= now) {
        it = deadline_by_mac_.erase(it);
      } else {
        ++it;
      }
    }
    deadline_by_mac_[peer.ToString()] = now + ttl_;
    if (!armed_) {
      armed_ = true;
      arm = callbacks_.on_arm;
    }
  }
  if (arm) arm();
}

void AgentSessionGate::EndSession(const MacAddress& peer) {
  std::function<void()> disarm;
  {
    absl::MutexLock l(&mu_);
    deadline_by_mac_.erase(peer.ToString());
    if (armed_ && deadline_by_mac_.empty()) {
      armed_ = false;
      disarm = callbacks_.on_disarm;
    }
  }
  if (disarm) disarm();
}

bool AgentSessionGate::IsAllowed(const MacAddress& peer) {
  std::function<void()> disarm;
  bool allowed;
  {
    absl::MutexLock l(&mu_);
    disarm = PurgeExpiredAndMaybeDisarmLocked(clock_());
    allowed = deadline_by_mac_.count(peer.ToString()) == 1;
  }
  if (disarm) disarm();
  return allowed;
}

void AgentSessionGate::SweepExpired() {
  std::function<void()> disarm;
  {
    absl::MutexLock l(&mu_);
    disarm = PurgeExpiredAndMaybeDisarmLocked(clock_());
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
