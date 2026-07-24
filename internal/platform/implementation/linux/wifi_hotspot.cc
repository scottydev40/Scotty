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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>

#include <sdbus-c++/IProxy.h>

#include "internal/platform/implementation/linux/dbus.h"
#include "internal/platform/implementation/linux/linux_flags.h"
#include "internal/platform/implementation/linux/network_manager.h"
#include "internal/platform/implementation/linux/network_manager_access_point.h"
#include "internal/platform/implementation/linux/utils.h"
#include "internal/platform/implementation/linux/wifi_hotspot.h"
#include "internal/platform/implementation/linux/wifi_hotspot_server_socket.h"
#include "internal/platform/implementation/linux/wifi_hotspot_socket.h"
#include "internal/platform/implementation/linux/wifi_medium.h"
#include "internal/platform/implementation/wifi.h"
#include "internal/platform/logging.h"

namespace nearby {
namespace linux {
namespace {

// Prefer non-DFS channels that are broadly available in the 00 (world) regulatory
// domain for AP mode.
constexpr int kPreferred24GhzChannel = 6;
constexpr int kPreferred5GhzChannel = 36;

// Wi-Fi channel for a centre frequency in MHz, or 0 if it isn't one we know.
int ChannelForFrequency(uint32_t frequency_mhz) {
  if (frequency_mhz == 2484) {
    return 14;  // Japan; not on the regular 5 MHz spacing.
  }
  if (frequency_mhz >= 2412 && frequency_mhz <= 2472) {
    return static_cast<int>((frequency_mhz - 2407) / 5);
  }
  if (frequency_mhz >= 5160 && frequency_mhz <= 5885) {
    return static_cast<int>((frequency_mhz - 5000) / 5);
  }
  if (frequency_mhz >= 5955 && frequency_mhz <= 7115) {
    return static_cast<int>((frequency_mhz - 5950) / 5);  // 6 GHz
  }
  return 0;
}

bool Is24GhzChannel(int channel) { return channel >= 1 && channel <= 14; }

// Centre frequency in MHz for a channel, or -1 when it isn't one we know.
int FrequencyForChannel(int channel) {
  if (channel == 14) return 2484;
  if (channel >= 1 && channel <= 13) return 2407 + channel * 5;
  if (channel >= 32 && channel <= 177) return 5000 + channel * 5;
  return -1;
}

// Name of the AP-mode virtual interface the hotspot is hosted on. Must stay
// within the 15 character limit for network interface names.
constexpr char kApInterfaceName[] = "nearby-ap0";

// Hosting the AP on its own virtual interface is what lets the machine stay on
// its Wi-Fi network. NetworkManager allows one active connection per *device*,
// so activating the AP on the station's device tears the station down; a second
// AP-mode interface is a separate device and the two run side by side.
//
// The interface is not created here: that needs CAP_NET_ADMIN, which this
// process does not have. Deployments create it once, out of band — a systemd
// unit or udev rule running:
//
//     iw dev <wifi-interface> interface add nearby-ap0 type __ap
//
// (`__ap` is undocumented in `iw help` but is the AP-mode virtual interface
// type.) An idle, downed interface costs nothing. When it is absent the hotspot
// falls back to the station's device, which still works — it just disconnects
// the current Wi-Fi network for the duration of the transfer.
bool ApInterfaceAvailable() {
  std::error_code ec;
  return std::filesystem::exists(
      std::string("/sys/class/net/") + kApInterfaceName, ec);
}
// NM_SETTING_WIRELESS_CHANNEL_WIDTH_* (nm-settings: 40mhz=40, 80mhz=80).
constexpr int32_t k40MhzChannelWidth = 40;
constexpr int32_t k80MhzChannelWidth = 80;

}  // namespace

std::unique_ptr<api::WifiHotspotSocket>
NetworkManagerWifiHotspotMedium::ConnectToService(
    absl::string_view ip_address, int port,
    CancellationFlag *cancellation_flag) {
  if (!ConnectedToWifi()) {
    LOG(ERROR)
        << __func__
        << ": Cannot connect to service without an active WiFi hotspot";
    return nullptr;
  }

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    LOG(ERROR) << __func__
                       << ": Error opening socket: " << std::strerror(errno);
    return nullptr;
  }

  LOG(INFO) << __func__ << ": Connecting to " << ip_address << ":"
                       << port;
  struct sockaddr_in addr {};
  addr.sin_addr.s_addr = inet_addr(std::string(ip_address).c_str());
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  auto ret =
      connect(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
  if (ret < 0) {
    LOG(ERROR) << __func__ << ": Error connecting to socket: "
                       << std::strerror(errno);
    return nullptr;
  }

  return std::make_unique<WifiHotspotSocket>(sock);
}

std::unique_ptr<api::WifiHotspotServerSocket>
NetworkManagerWifiHotspotMedium::ListenForService(int port) {
  if (!WifiHotspotActive()) {
    LOG(ERROR)
        << __func__
        << ": Cannot connect to service without an active WiFi hotspot";
    return nullptr;
  }

  // Use the AP device's connection so the listen socket binds the hotspot IP
  // (10.42.0.x) and the server socket reports that IP to the peer. The station
  // device's connection is the network we are joined to, unreachable over the
  // hotspot.
  auto &host_device = ap_device_ ? ap_device_ : wireless_device_;
  auto active_connection = host_device->GetActiveConnection();
  if (active_connection == nullptr) {
    return nullptr;
  }

  auto ip4addresses = active_connection->GetIP4Addresses();
  if (ip4addresses.empty()) {
    LOG(ERROR)
        << __func__
        << "Could not find any IPv4 addresses for active connection "
        << active_connection->getProxy().getObjectPath();
    return nullptr;
  }

  auto sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    LOG(ERROR) << __func__
                       << ": Error opening socket: " << std::strerror(errno);
    return nullptr;
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(ip4addresses[0].c_str());
  addr.sin_port = htons(port);

  auto ret =
      bind(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
  if (ret < 0) {
    LOG(ERROR) << __func__
                       << ": Error binding to socket: " << std::strerror(errno);
    return nullptr;
  }

  LOG(INFO) << __func__ << ": Listening for services on "
                       << ip4addresses[0] << ":" << port << " on device "
                       << host_device->getProxy().getObjectPath();

  ret = listen(sock, 0);
  if (ret < 0) {
    LOG(ERROR) << __func__ << ": Error listening on socket: "
                       << std::strerror(errno);
    return nullptr;
  }

  return std::make_unique<NetworkManagerWifiHotspotServerSocket>(
      sock, std::move(active_connection), network_manager_);
}

bool NetworkManagerWifiHotspotMedium::EnsureApInterface(bool up) {
  static constexpr char kApUnit[] = "nearby-ap-interface.service";
  const char *method = up ? "StartUnit" : "StopUnit";
  try {
    auto systemd = sdbus::createProxy(
        *system_bus_, sdbus::ServiceName{"org.freedesktop.systemd1"},
        sdbus::ObjectPath{"/org/freedesktop/systemd1"});
    sdbus::ObjectPath job;
    systemd->callMethod(method)
        .onInterface("org.freedesktop.systemd1.Manager")
        .withArguments(std::string(kApUnit), std::string("replace"))
        .storeResultsTo(job);
  } catch (const sdbus::Error &e) {
    LOG(WARNING) << __func__ << ": systemd " << method << " " << kApUnit
                 << " failed (" << e.what()
                 << "); is the polkit rule installed? Falling back to the "
                    "station interface.";
    return false;
  }
  if (!up) return true;

  // StartUnit is asynchronous. Wait not just for the kernel interface to exist
  // (/sys) but for NetworkManager to register it as a device — otherwise
  // AddAndActivateConnection2 races and fails with "device is not available".
  for (int attempt = 0; attempt < 40; ++attempt) {  // up to ~4s
    if (ApInterfaceAvailable()) {
      try {
        auto dev = network_manager_->GetDeviceByIpIface(kApInterfaceName);
        if (!dev.empty() && static_cast<std::string>(dev) != "/") return true;
      } catch (const sdbus::Error &) {
        // NetworkManager has not picked up the new interface yet.
      }
    }
    absl::SleepFor(absl::Milliseconds(100));
  }
  LOG(WARNING) << __func__
               << ": nearby-ap0 did not become available after starting "
               << kApUnit;
  return false;
}

bool NetworkManagerWifiHotspotMedium::StartWifiHotspot(
    HotspotCredentials *hotspot_credentials) {
  return StartWifiHotspot(hotspot_credentials, false);
}

bool NetworkManagerWifiHotspotMedium::StartWifiHotspot(
    HotspotCredentials *hotspot_credentials, bool force_24ghz) {
  if (WifiHotspotActive()) {
    LOG(ERROR) << __func__ << ": " << wireless_device_->getProxy().getObjectPath()
                       << ": cannot start WiFi hotspot, a hotspot is already "
                          "active on this device";
    return false;
  }

  std::string ssid = RandSSID();
  hotspot_credentials->SetSSID(ssid);

  std::string password = RandWPAPassphrase();
  hotspot_credentials->SetPassword(password);

  auto connection_id = NewUuidStr();
  if (!connection_id.has_value()) {
    LOG(ERROR) << __func__ << ": could not generate a connection UUID";
    return false;
  }

  // Get device capabilities to select the best available band
  api::WifiCapability& capability = wireless_device_->GetCapability();
  std::string selected_band;
  int selected_channel = kPreferred24GhzChannel;
  const bool enable_5ghz_hotspot = !force_24ghz && Is5GhzHotspotEnabled();
  // Boost: give the AP the entire radio (best channel, full width, no station
  // coexistence) for maximum throughput, at the cost of dropping the current
  // Wi-Fi connection for the duration of the transfer.
  const bool boost = IsHotspotBoostEnabled();
  if (boost) {
    LOG(INFO) << __func__
              << ": Boost enabled — hosting on the station interface at full "
                 "bandwidth; the current Wi-Fi connection will drop";
  }
  if (enable_5ghz_hotspot &&
      (capability.supports_6_ghz || capability.supports_5_ghz)) {
    selected_band = "a";  // 5/6 GHz - NetworkManager uses "a" for both 5 GHz and 6 GHz
    selected_channel = kPreferred5GhzChannel;
    LOG(INFO) << __func__
              << ": Device supports 5/6 GHz, using 5/6 GHz band on channel "
              << selected_channel;
  } else {
    selected_band = "bg";  // 2.4 GHz
    selected_channel = kPreferred24GhzChannel;
    if (!enable_5ghz_hotspot &&
        (capability.supports_6_ghz || capability.supports_5_ghz)) {
      LOG(INFO) << __func__
                << ": 5/6 GHz hotspot is disabled by feature flag, using 2.4 "
                << "GHz band on channel " << selected_channel;
    } else {
      LOG(INFO) << __func__
                << ": Device supports only 2.4 GHz, using 2.4 GHz band on "
                << "channel " << selected_channel;
    }
  }

  // Most cards can only run an AP and a station at once if both sit on the
  // same channel ("#channels <= 1" in `iw list` interface combinations).
  // Hosting the hotspot on a fixed channel therefore knocks the machine off
  // its current Wi-Fi network for the duration of the transfer. Reuse the
  // channel we are already associated on so the two can coexist. Boost skips
  // this — it wants the best channel at full width, station be damned.
  if (!boost) {
    // The station's channel comes from its active access point. That D-Bus
    // property is momentarily "/" (NetworkManager's null path) around
    // association/scan even while the station stays connected. If we give up
    // then and host the AP on the default channel, it can differ from the
    // station's — which breaks the single-channel radio rule and the AP
    // activation fails ("device was disconnected"), dropping the transfer to
    // Bluetooth. Retry the read while the station is associated so a transient
    // miss does not force a channel conflict.
    auto is_valid_path = [](const sdbus::ObjectPath &p) {
      return !p.empty() && static_cast<std::string>(p) != "/";
    };
    sdbus::ObjectPath station_ap_path;
    for (int attempt = 0; attempt < 10; ++attempt) {
      try {
        station_ap_path = wireless_device_->ActiveAccessPoint();
      } catch (const sdbus::Error &e) {
        station_ap_path = sdbus::ObjectPath{};
      }
      if (is_valid_path(station_ap_path) || !ConnectedToWifi()) break;
      absl::SleepFor(absl::Milliseconds(200));
    }
    if (is_valid_path(station_ap_path)) try {
      NetworkManagerAccessPoint station_ap(*system_bus_, station_ap_path);
      const int station_channel = ChannelForFrequency(station_ap.Frequency());
      // Only when it is in the band we were going to use anyway: moving bands
      // would trade the disconnect for a slower or unreachable hotspot.
      if (station_channel != 0 &&
          Is24GhzChannel(station_channel) == (selected_band == "bg")) {
        if (station_channel != selected_channel) {
          LOG(INFO) << __func__ << ": Using channel " << station_channel
                    << " to match the current Wi-Fi connection instead of "
                    << selected_channel
                    << ", so the station link survives the hotspot";
        }
        selected_channel = station_channel;
      } else if (station_channel != 0) {
        LOG(INFO) << __func__ << ": Current Wi-Fi is on channel "
                  << station_channel
                  << ", a different band to the hotspot; the station link will "
                     "likely drop";
      }
    } catch (const sdbus::Error &e) {
      LOG(WARNING) << __func__
                   << ": Could not read the current Wi-Fi channel, using channel "
                   << selected_channel << ": " << e.what();
    } else if (ConnectedToWifi()) {
      LOG(WARNING) << __func__
                   << ": Wi-Fi is associated but its channel could not be read; "
                      "hosting on channel "
                   << selected_channel << " may drop the station link";
    }
  }

  const int32_t fallback_channel_width =
      selected_band == "a" ? k80MhzChannelWidth : k40MhzChannelWidth;

  // Host the AP on its own interface when we can, so the station connection
  // survives (see EnsureApInterface). Falls back to the station's own device,
  // which is what NetworkManager tears down.
  // On demand: bring nearby-ap0 up just for this transfer, unless boost (which
  // hosts on the station device and needs no AP interface). Kept out of
  // existence otherwise, so it never clutters the Settings UI or gets picked as
  // the station device.
  if (!boost && !ApInterfaceAvailable()) {
    ap_interface_started_by_us_ = EnsureApInterface(true);
  }

  sdbus::ObjectPath ap_device_path = wireless_device_->getProxy().getObjectPath();
  if (boost) {
    LOG(INFO) << __func__
              << ": Boost — hosting on the station device for full bandwidth";
  } else if (ApInterfaceAvailable()) {
    try {
      ap_device_path = network_manager_->GetDeviceByIpIface(kApInterfaceName);
      LOG(INFO) << __func__ << ": hosting the hotspot on " << kApInterfaceName
                << ", so the current Wi-Fi connection stays up";
    } catch (const sdbus::Error &e) {
      LOG(WARNING) << __func__ << ": " << kApInterfaceName
                   << " exists but NetworkManager has no device for it ("
                   << e.getMessage() << "); using the station interface";
    }
  } else {
    LOG(INFO) << __func__ << ": no " << kApInterfaceName
              << " interface; hosting on the station device, which will drop "
                 "the current Wi-Fi connection";
  }

  const bool using_ap_interface =
      ap_device_path != wireless_device_->getProxy().getObjectPath();

  // Boost hosts the AP on the station's own device. If the station is still
  // connected when we activate the AP, NetworkManager tears it down mid-
  // activation and the new connection is reported as failed ("the device it
  // was using was disconnected") — it only succeeds on a retry. Free the
  // device first so the AP comes up cleanly on the first try.
  if (boost && !using_ap_interface) {
    try {
      auto station_conn = wireless_device_->GetActiveConnection();
      if (station_conn != nullptr) {
        LOG(INFO) << __func__
                  << ": Boost — deactivating the station connection first so "
                     "the AP activates cleanly";
        network_manager_->DeactivateConnection(
            station_conn->getProxy().getObjectPath());
        // Give NetworkManager a moment to release the device before we ask it
        // to host the AP there.
        absl::SleepFor(absl::Milliseconds(600));
      }
    } catch (const sdbus::Error &e) {
      LOG(WARNING) << __func__
                   << ": Boost — could not pre-deactivate the station ("
                   << e.getMessage() << "); relying on activation retry";
    }
  }

  std::unique_ptr<networkmanager::ActiveConnection> active_conn;
  for (bool include_channel_width : {true, false}) {
    std::map<std::string, std::map<std::string, sdbus::Variant>>
        connection_settings{
            {
                "connection",
                {{"uuid", sdbus::Variant(*connection_id)},
                 {"id", sdbus::Variant("Google Nearby Hotspot")},
                 {"type", sdbus::Variant("802-11-wireless")},
                 {"zone", sdbus::Variant("Public")}},
            },
            {"802-11-wireless",
             {{"assigned-mac-address", sdbus::Variant("random")},
              {"ap-isolation", sdbus::Variant(networkmanager::constants::kNMTernaryFalse)},
              {"mode", sdbus::Variant("ap")},
              {"band", sdbus::Variant(selected_band)},
              {"channel", sdbus::Variant(selected_channel)},
              {"ssid", sdbus::Variant(std::vector<uint8_t>(ssid.begin(), ssid.end()))},
              {"security", sdbus::Variant("802-11-wireless-security")}}},
            {"802-11-wireless-security",
             {{"pmf", sdbus::Variant(networkmanager::constants::setting::kWirelessSecurityPMFDisable)},
              {"key-mgmt", sdbus::Variant("wpa-psk")},
              {"psk", sdbus::Variant(password)}}},
            {"ipv4", {{"method", sdbus::Variant("shared")}}},
            {"ipv6",
             {
                 {"addr-gen-mode",
                  sdbus::Variant(networkmanager::constants::setting::kIP6ConfigAddrGenModeStablePrivacy)},
                 {"method", sdbus::Variant("shared")},
             }}};
    // On the AP interface the radio is shared with the station, which already
    // fixes the channel and its width. Asking for a width as well makes the
    // driver refuse to bring the AP up, and NetworkManager reports that as
    // "Hotspot network creation took too long" 25s later.
    if (include_channel_width && using_ap_interface) {
      continue;  // the second pass, without a width, is the one that works
    }
    if (include_channel_width) {
      connection_settings["802-11-wireless"]["channel-width"] =
          sdbus::Variant(fallback_channel_width);
    }
    try {
      auto [path, active_path, result] =
          network_manager_->AddAndActivateConnection2(
              connection_settings,
              ap_device_path,
              sdbus::ObjectPath("/"),
              {{"persist", sdbus::Variant("volatile")},
               {"bind-activation", sdbus::Variant("dbus-client")}});
      active_conn = std::make_unique<networkmanager::ActiveConnection>(
          system_bus_, active_path);
      break;
    } catch (const sdbus::Error &e) {
      if (include_channel_width) {
        LOG(WARNING) << __func__
                     << ": Failed to set channel-width="
                     << fallback_channel_width << "mhz: "
                     << e.getName() << ": " << e.getMessage()
                     << ". Retrying without channel-width.";
        continue;
      }
      DBUS_LOG_METHOD_CALL_ERROR(network_manager_, "AddAndActivateConnection2",
                                 e);
      return false;
    }
  }

  if (active_conn == nullptr) {
    return false;
  }

  auto [reason, timeout] = active_conn->WaitForConnection(absl::Seconds(60));
  if (timeout) {
    LOG(ERROR)
        << __func__ << ": "
        << ": timed out while waiting for connection "
        << active_conn->getProxy().getObjectPath()
        << " to be activated, last NMActiveConnectionStateReason: "
        << reason->ToString();
    DisconnectWifiHotspot();
    return false;
  }

  // WaitForConnection only reports a reason when the activation *failed*;
  // success is signalled by no reason at all. Treating "not timed out" as
  // success meant a failed activation still marked the hotspot as started,
  // after which ListenForService could never find one and the upgrade retried
  // forever.
  if (reason.has_value()) {
    LOG(ERROR) << __func__ << ": connection "
               << active_conn->getProxy().getObjectPath()
               << " failed to activate: " << reason->ToString();
    DisconnectWifiHotspot();
    return false;
  }

  // Report the channel we asked for rather than reading the device's active
  // access point: when the AP is on its own interface, the station device's
  // access point is the network we are *connected to*, not the hotspot.
  hotspot_credentials->SetFrequency(FrequencyForChannel(selected_channel));
  LOG(INFO) << __func__ << ": Hotspot frequency set to "
            << FrequencyForChannel(selected_channel) << " MHz";

  // Remembered so StopWifiHotspot tears down this connection specifically. The
  // station's own connection must never be deactivated.
  hotspot_connection_path_ = active_conn->getProxy().getObjectPath();

  // Remember the hosting device so WifiHotspotActive()/ListenForService() read
  // the AP's mode and IP (10.42.0.x), not the station's. Null when we fell back
  // to the station device, in which case those helpers use wireless_device_.
  ap_device_ = using_ap_interface
                   ? std::make_unique<NetworkManagerWifiMedium>(network_manager_,
                                                                ap_device_path)
                   : nullptr;

  LOG(INFO) << __func__ << ": Started a WiFi hotspot on device "
                    << ap_device_path << " at "
                    << active_conn->getProxy().getObjectPath();
  return true;
}

bool NetworkManagerWifiHotspotMedium::StopWifiHotspot() {
  if (!WifiHotspotActive()) {
    LOG(ERROR)
        << __func__ << ": " << wireless_device_->getProxy().getObjectPath()
        << ": Cannot stop WiFi hotspot as a WiFi hotspot is not active";
  }

  // Tear down the connection we started, by path. Asking the wireless device
  // for its active connection would return whatever the station is associated
  // with once the hotspot lives on its own interface — deactivating that would
  // drop the user off their Wi-Fi network.
  ap_device_ = nullptr;
  if (!hotspot_connection_path_.empty()) {
    const sdbus::ObjectPath path = hotspot_connection_path_;
    hotspot_connection_path_ = {};
    LOG(INFO) << __func__ << ": Deactivating hotspot connection " << path;
    bool ok = true;
    try {
      network_manager_->DeactivateConnection(path);
    } catch (const sdbus::Error &e) {
      DBUS_LOG_METHOD_CALL_ERROR(network_manager_, "DeactivateConnection", e);
      ok = false;
    }
    // On-demand teardown: remove nearby-ap0 again if we brought it up, so it
    // doesn't linger in existence (or the Settings UI) between transfers.
    if (ap_interface_started_by_us_) {
      EnsureApInterface(false);
      ap_interface_started_by_us_ = false;
    }
    return ok;
  }

  // Get the active connection object for the hotspot AP.
  sdbus::ObjectPath active_ap_path;

  try {
    active_ap_path = wireless_device_->ActiveAccessPoint();
    if (active_ap_path.empty()) {
      LOG(ERROR) << __func__ << ": No active access points on "
                         << wireless_device_->getProxy().getObjectPath();
      return false;
    }
  } catch (const sdbus::Error &e) {
    DBUS_LOG_PROPERTY_GET_ERROR(wireless_device_, "ActiveAccessPoint", e);
  }

  auto object_manager = networkmanager::ObjectManager(system_bus_);
  auto active_connection = wireless_device_->GetActiveConnection();
  if (active_connection == nullptr) {
    LOG(ERROR)
        << __func__
        << ": Could not find an active connection using the access point "
        << active_ap_path;
    return false;
  }

  LOG(INFO) << __func__ << ": " << wireless_device_->getProxy().getObjectPath()
                    << ": Deactivating active connection "
                    << active_connection->getProxy().getObjectPath();

  try {
    network_manager_->DeactivateConnection(active_connection->getProxy().getObjectPath());
  } catch (const sdbus::Error &e) {
    DBUS_LOG_METHOD_CALL_ERROR(network_manager_, "DeactivateConnection", e);
    return false;
  }

  return true;
}

bool NetworkManagerWifiHotspotMedium::ConnectWifiHotspot(
    const HotspotCredentials& hotspot_credentials) {

  auto ssid = hotspot_credentials.GetSSID();
  auto password = hotspot_credentials.GetPassword();

  return wireless_device_->ConnectToNetwork(ssid, password,
                                            NetworkManagerWifiMedium::WifiAuthType::kWpaPsk) ==
         NetworkManagerWifiMedium::WifiConnectionStatus::kConnected;
}

bool NetworkManagerWifiHotspotMedium::DisconnectWifiHotspot() {
  if (!ConnectedToWifi()) {
    LOG(ERROR) << __func__ << ": Not connected to a WiFi hotspot";
    return false;
  }

  auto active_connection = wireless_device_->GetActiveConnection();
  if (active_connection == nullptr) {
    return false;
  }

  try {
    network_manager_->DeactivateConnection(active_connection->getProxy().getObjectPath());
  } catch (const sdbus::Error &e) {
    DBUS_LOG_METHOD_CALL_ERROR(network_manager_, "DeactivateConnection", e);
    return false;
  }

  return true;
}

bool NetworkManagerWifiHotspotMedium::WifiHotspotActive() {
  // Query the device that actually hosts the AP. When the hotspot is on its own
  // interface the station device stays in Infra mode, so checking it would
  // wrongly report "no hotspot" and abort ListenForService.
  auto &device = ap_device_ ? ap_device_ : wireless_device_;
  try {
    auto mode = device->Mode();
    return mode == networkmanager::constants::kNM80211ModeAP;
  } catch (const sdbus::Error &e) {
    DBUS_LOG_PROPERTY_GET_ERROR(device, "Mode", e);
    return false;
  }
}

bool NetworkManagerWifiHotspotMedium::ConnectedToWifi() {
  try {
    auto mode = wireless_device_->Mode();
    return mode == networkmanager::constants::kNM80211ModeInfra;
  } catch (const sdbus::Error &e) {
    DBUS_LOG_PROPERTY_GET_ERROR(wireless_device_, "Mode", e);
    return false;
  }
}

}  // namespace linux
}  // namespace nearby
