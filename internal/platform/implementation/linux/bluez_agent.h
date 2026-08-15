#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <sdbus-c++/sdbus-c++.h>

#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "internal/platform/implementation/linux/agent_session_gate.h"
#include "internal/platform/implementation/linux/generated/dbus/bluez/agent_server.h"
#include "internal/platform/implementation/linux/generated/dbus/bluez/agentmanager_client.h"
#include "internal/platform/implementation/linux/bluez.h"
#include "internal/platform/logging.h"
#include "internal/platform/mac_address.h"

namespace nearby::linux {

class Agent final
    : public sdbus::AdaptorInterfaces<org::bluez::Agent1_adaptor> {
 public:
  Agent(const Agent&) = delete;
  Agent(Agent&&) = delete;
  Agent& operator=(const Agent&) = delete;
  Agent& operator=(Agent&&) = delete;

  Agent(sdbus::IConnection& system_bus, sdbus::ObjectPath path,
        std::shared_ptr<AgentSessionGate> gate)
      : AdaptorInterfaces(system_bus, std::move(path)), gate_(std::move(gate)) {
    registerAdaptor();
    LOG(INFO) << "Created new Agent at path: " << getObject().getObjectPath();
  }

  ~Agent() { unregisterAdaptor(); }

 private:
  std::shared_ptr<AgentSessionGate> gate_;

  void Release() override;

  std::string RequestPinCode(const sdbus::ObjectPath& device) override;
  void DisplayPinCode(const sdbus::ObjectPath& device,
                      const std::string& pincode) override;

  uint32_t RequestPasskey(const sdbus::ObjectPath& device) override;
  void DisplayPasskey(const sdbus::ObjectPath& device, const uint32_t& passkey,
                      const uint16_t& entered) override;

  void RequestConfirmation(const sdbus::ObjectPath& device,
                           const uint32_t& passkey) override;

  void RequestAuthorization(const sdbus::ObjectPath& device) override;

  void AuthorizeService(const sdbus::ObjectPath& device,
                        const std::string& uuid) override;

  void Cancel() override;
};

class AgentManager final
    : public sdbus::ProxyInterfaces<org::bluez::AgentManager1_proxy> {
 public:
  AgentManager(const AgentManager&) = delete;
  AgentManager(AgentManager&&) = delete;
  AgentManager& operator=(const AgentManager&) = delete;
  AgentManager& operator=(AgentManager&&) = delete;

  explicit AgentManager(sdbus::IConnection& system_bus)
      : ProxyInterfaces(system_bus, sdbus::ServiceName(bluez::SERVICE_DEST),
        sdbus::ObjectPath("/org/bluez")) {
    registerProxy();
  }

  ~AgentManager() { unregisterProxy(); }

  // Install the shared gate and wire arm/disarm to it.
  void SetGate(std::shared_ptr<AgentSessionGate> gate);
  void BeginSession(const MacAddress& peer);
  void EndSession(const MacAddress& peer);

 private:
  void Arm();     // register agent (if needed) + RequestDefaultAgent
  void Disarm();  // UnregisterAgent

  std::shared_ptr<AgentSessionGate> gate_;
  sdbus::ObjectPath agent_path_{"/com/google/nearby/bluetooth/agent"};

  absl::Mutex registered_agents_mutex_;
  std::map<std::string, std::shared_ptr<Agent>> registered_agents_
      ABSL_GUARDED_BY(registered_agents_mutex_);
};

}  // namespace nearby::linux
