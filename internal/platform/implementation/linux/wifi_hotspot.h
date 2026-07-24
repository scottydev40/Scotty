// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef PLATFORM_IMPL_LINUX_WIFI_HOTSPOT_H_
#define PLATFORM_IMPL_LINUX_WIFI_HOTSPOT_H_

#include <arpa/inet.h>

#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/Types.h>

#include "internal/platform/implementation/linux/wifi_medium.h"
#include "internal/platform/implementation/wifi_hotspot.h"

namespace nearby {
namespace linux {
class NetworkManagerWifiHotspotMedium : public api::WifiHotspotMedium {
 public:
  NetworkManagerWifiHotspotMedium(
      std::shared_ptr<networkmanager::NetworkManager> network_manager,
      sdbus::ObjectPath wireless_device_object_path)
      : system_bus_(network_manager->GetConnection()),
        wireless_device_(std::make_unique<NetworkManagerWifiMedium>(
            network_manager, std::move(wireless_device_object_path))),
        network_manager_(std::move(network_manager)) {}
  NetworkManagerWifiHotspotMedium(
      std::shared_ptr<networkmanager::NetworkManager> network_manager,
      std::unique_ptr<NetworkManagerWifiMedium> wireless_device)
      : system_bus_(network_manager->GetConnection()),
        wireless_device_(std::move(wireless_device)),
        network_manager_(std::move(network_manager)) {}
  
  bool IsInterfaceValid() const override { return true; }
    virtual std::unique_ptr<api::WifiHotspotSocket> ConnectToService(
        const ServiceAddress& service_address,
        CancellationFlag* cancellation_flag) {
    if (service_address.address.size() != 4) {
      return nullptr;
    }
    char ip_address_buffer[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, service_address.address.data(),
                  ip_address_buffer, sizeof(ip_address_buffer)) == nullptr) {
      return nullptr;
    }

    return ConnectToService(ip_address_buffer, service_address.port,
                            cancellation_flag);
  };
    std::unique_ptr<api::WifiHotspotSocket> ConnectToService(
      absl::string_view ip_address, int port,
      CancellationFlag *cancellation_flag);
  std::unique_ptr<api::WifiHotspotServerSocket> ListenForService(
      int port) override;

  bool StartWifiHotspot(HotspotCredentials *hotspot_credentials) override;
  bool StartWifiHotspot(HotspotCredentials *hotspot_credentials, bool force_24ghz);
  bool StopWifiHotspot() override;

  bool ConnectWifiHotspot(const HotspotCredentials& hotspot_credentials) override;
  bool DisconnectWifiHotspot() override;

  absl::optional<std::pair<std::int32_t, std::int32_t>> GetDynamicPortRange()
      override {
    return absl::nullopt;
  }

 private:
  bool WifiHotspotActive();
  bool ConnectedToWifi();

  std::shared_ptr<sdbus::IConnection> system_bus_;
  std::unique_ptr<NetworkManagerWifiMedium> wireless_device_;
  // The hotspot's own active connection, so tearing it down never touches the
  // station's connection (they are separate devices once the AP has its own
  // interface). Empty when no hotspot was started by us.
  sdbus::ObjectPath hotspot_connection_path_;
  // The device actually hosting the AP (nearby-ap0 when we have it). Null when
  // no hotspot is up or it fell back to the station device. WifiHotspotActive()
  // and ListenForService() must query this, not wireless_device_ — once the AP
  // lives on its own interface the station stays in Infra mode on its own IP.
  std::unique_ptr<NetworkManagerWifiMedium> ap_device_;
  std::shared_ptr<networkmanager::NetworkManager> network_manager_;
};
}  // namespace linux
}  // namespace nearby

#endif
