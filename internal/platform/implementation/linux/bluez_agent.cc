#include "internal/platform/implementation/linux/bluez_agent.h"

#include <sdbus-c++/AdaptorInterfaces.h>

#include "internal/platform/implementation/linux/generated/dbus/bluez/agent_server.h"
#include "internal/platform/implementation/linux/generated/dbus/bluez/agentmanager_client.h"
#include "internal/platform/implementation/linux/bluez.h"
#include "internal/platform/logging.h"

namespace nearby {
namespace linux {

namespace {
// True iff the device path's MAC is a peer we currently expect (active session).
bool DevicePathAllowed(AgentSessionGate* gate, const sdbus::ObjectPath& device) {
  if (gate == nullptr) return false;
  auto mac = bluez::mac_from_device_object_path(device);
  if (!mac.has_value()) return false;
  return gate->IsAllowed(*mac);
}
}  // namespace

void Agent::Release() { LOG(INFO) << "[agent] Release()"; }

std::string Agent::RequestPinCode(const sdbus::ObjectPath& device) {
  LOG(WARNING) << "[agent] RequestPinCode(" << device << ") -> REJECT";
  throw sdbus::Error(sdbus::Error::Name("org.bluez.Error.Rejected"),
                     "PIN pairing not supported");
}

void Agent::DisplayPinCode(const sdbus::ObjectPath& device,
                           const std::string& pincode) {
  LOG(INFO) << "[agent] DisplayPinCode(" << device << ", " << pincode << ")";
}

uint32_t Agent::RequestPasskey(const sdbus::ObjectPath& device) {
  LOG(WARNING) << "[agent] RequestPasskey(" << device << ") -> REJECT";
  throw sdbus::Error(sdbus::Error::Name("org.bluez.Error.Rejected"),
                     "passkey pairing not supported");
}

void Agent::DisplayPasskey(const sdbus::ObjectPath& device,
                           const uint32_t& passkey,
                           const uint16_t& entered) {
  LOG(INFO) << "[agent] DisplayPasskey(" << device << ", " << passkey
            << ", entered=" << entered << ")";
}

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

void Agent::Cancel() { LOG(INFO) << "[agent] Cancel()"; }

bool AgentManager::AgentRegistered(absl::string_view agent_object_path) {
  registered_agents_mutex_.ReaderLock();
  bool registered = registered_agents_.count(std::string(agent_object_path)) == 1;
  registered_agents_mutex_.ReaderUnlock();
  return registered;
}

bool AgentManager::Register(std::optional<absl::string_view> capability,
                            const sdbus::ObjectPath& agent_object_path) {
  absl::MutexLock l(&registered_agents_mutex_);

  const std::string agent_path_str = std::string(agent_object_path);

  if (registered_agents_.count(agent_path_str) == 1) {
    LOG(WARNING) << __func__ << ": Trying to register agent " << agent_path_str
                 << " which was already registered.";
    return true;
  }

  auto agent = std::make_shared<Agent>(getProxy().getConnection(),
                                       sdbus::ObjectPath(agent_object_path),
                                       gate_);

  try {
    const std::string cap =
        capability.has_value() ? std::string(*capability) : "NoInputNoOutput";
    RegisterAgent(agent->getObject().getObjectPath(), cap);
    RequestDefaultAgent(agent->getObject().getObjectPath());
  } catch (const sdbus::Error& e) {
    LOG(ERROR) << __func__ << ": Got error '" << e.getName()
               << "' with message '" << e.getMessage()
               << "' while calling RegisterAgent/RequestDefaultAgent on object "
               << getProxy().getObjectPath();
    return false;
  }

  registered_agents_.emplace(agent_path_str, agent);

  LOG(INFO) << __func__ << ": Registered agent instance at path "
            << agent_path_str;

  return true;
}

bool AgentManager::EnsureDefaultAgent(
    std::optional<absl::string_view> capability,
    const sdbus::ObjectPath& agent_object_path) {
  // Not registered yet -> Register() also calls RequestDefaultAgent for us.
  if (!AgentRegistered(agent_object_path)) {
    return Register(capability, agent_object_path);
  }

  // Already registered: just reclaim the default-agent slot. BlueZ keeps a
  // single default agent, so the last requester wins; doing this as we start
  // listening puts us back in charge if something grabbed it after startup.
  try {
    RequestDefaultAgent(agent_object_path);
  } catch (const sdbus::Error& e) {
    LOG(WARNING) << __func__ << ": RequestDefaultAgent failed for "
                 << agent_object_path << ": " << e.getName() << " ("
                 << e.getMessage() << ")";
    return false;
  }
  LOG(INFO) << __func__ << ": Reasserted default agent " << agent_object_path;
  return true;
}

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

}  // namespace linux
}  // namespace nearby
