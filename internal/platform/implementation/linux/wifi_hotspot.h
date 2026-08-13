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
  // Reactivates the station Wi-Fi connection that boost deactivated (if any),
  // then clears deactivated_station_connection_path_. No-op when boost did not
  // drop the station. Idempotent, so it is safe to call from both the hotspot
  // teardown and every StartWifiHotspot failure path — leaving the user with no
  // Wi-Fi after a failed boost hotspot is the bug this closes.
  void ReactivateStation();

  // The Wi-Fi upgrade needs the radio on. If the user has Wi-Fi switched off
  // (NetworkManager WirelessEnabled=false) a transfer stays on slow Bluetooth,
  // so enable the radio just for the transfer, the way Quick Share does on
  // Android. Returns true if the radio is usable (was already on, or we brought
  // it up); false if it could not be enabled — e.g. a hard rfkill block we
  // cannot override in software, detected by the device never becoming ready —
  // in which case the toggle is left as it was found and the caller falls back
  // to Bluetooth. Records whether we were the one to enable it.
  bool EnsureWifiRadioEnabled();
  // Turns the Wi-Fi radio back off iff EnsureWifiRadioEnabled turned it on, then
  // clears the flag. Idempotent no-op otherwise, so it is safe on every teardown
  // and failure path. Restores exactly the state the user set — never disables a
  // radio that was already on.
  void RestoreWifiRadio();
  // Brings the nearby-ap0 AP interface up (up=true) or down (up=false) on
  // demand by starting/stopping its systemd unit over D-Bus. A polkit rule
  // authorizes the local user for exactly that unit, so no password prompt.
  // Keeps the interface out of existence — and out of the Settings UI — except
  // while a hotspot transfer needs it. Best-effort: returns false if it could
  // not bring the interface up, and the caller falls back to the station.
  bool EnsureApInterface(bool up);

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
  // True when we started the nearby-ap0 unit for this transfer, so StopWifiHotspot
  // knows to tear the interface back down.
  bool ap_interface_started_by_us_ = false;

  // Boost hosts the AP on the station device, so it deactivates the station's
  // Wi-Fi connection first. NetworkManager will NOT auto-reconnect a manually-
  // deactivated connection, so we remember its settings path here and
  // reactivate it when the hotspot is torn down — otherwise Wi-Fi stays dead
  // after a boost transfer. Empty when boost didn't drop the station.
  sdbus::ObjectPath deactivated_station_connection_path_;
  // True when EnsureWifiRadioEnabled turned the Wi-Fi radio on for a transfer,
  // so RestoreWifiRadio knows to switch it back off afterwards. False when the
  // radio was already on (we leave the user's state untouched).
  bool wifi_radio_enabled_by_us_ = false;
  std::shared_ptr<networkmanager::NetworkManager> network_manager_;
};
}  // namespace linux
}  // namespace nearby

#endif
