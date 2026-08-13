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

#include <cstring>
#include <memory>

#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/Types.h>

#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "internal/base/observer_list.h"
#include "internal/platform/implementation/bluetooth_classic.h"
#include "internal/platform/implementation/linux/bluetooth_adapter.h"
#include "internal/platform/implementation/linux/bluez_agent.h"
#include "internal/platform/implementation/linux/bluetooth_bluez_profile.h"
#include "internal/platform/implementation/linux/bluetooth_classic_device.h"
#include "internal/platform/implementation/linux/bluetooth_classic_medium.h"

#include "internal/platform/implementation/linux/bluetooth_classic_server_socket.h"
#include "internal/platform/implementation/linux/bluetooth_classic_socket.h"
#include "internal/platform/implementation/linux/rfcomm_connect.h"
#include "internal/platform/implementation/linux/bluetooth_pairing.h"
#include "internal/platform/logging.h"

namespace nearby {
namespace linux {
namespace {

constexpr char kBluezAgentPath[] = "/com/google/nearby/bluetooth/agent";

// BlueZ ConnectProfile can fail transiently with 'br-connection-create-socket'
// when the peer's SDP record for the service UUID hasn't propagated yet — common
// right after a peer rotates its BLE endpoint or has just started serving over
// BR/EDR. Retry a few times with a short backoff before giving up, matching the
// TCP connect retry. A peer that is genuinely not serving classic BT still fails
// fast enough (3 * 700 ms) for the higher layer to fall through to other media.
constexpr int kConnectProfileMaxAttempts = 3;
constexpr absl::Duration kConnectProfileRetryBackoff = absl::Milliseconds(700);

}  // namespace

BluetoothClassicMedium::BluetoothClassicMedium(BluetoothAdapter &adapter)
    : system_bus_(adapter.GetConnection()),
      adapter_(adapter),
      observers_(nullptr),
      devices_(nullptr),
      device_watcher_(nullptr),
      agent_manager_(std::make_unique<AgentManager>(*system_bus_)),
      profile_manager_(nullptr) {
  auto shared =
      GetSharedBluetoothDevices(system_bus_, adapter_.GetObjectPath());
  observers_ = shared->observers;
  devices_ = shared->devices;
  profile_manager_ =
      std::make_unique<ProfileManager>(*system_bus_, *devices_);

  if (!agent_manager_->Register(
          /*capability=*/absl::string_view("NoInputNoOutput"),
          sdbus::ObjectPath(kBluezAgentPath))) {
    LOG(WARNING) << __func__
                 << ": Failed to register default BlueZ agent at "
                 << kBluezAgentPath;
  }
}

bool BluetoothClassicMedium::StartDiscovery(
    DiscoveryCallback discovery_callback) {
  device_watcher_ = std::make_unique<DeviceWatcher>(
      *system_bus_, adapter_.GetObjectPath(), adapter_, devices_,
      std::make_unique<DiscoveryCallback>(std::move(discovery_callback)),
      observers_);

  std::map<std::string, sdbus::Variant> filter;
  filter["Transport"] = sdbus::Variant("auto");
  auto &adapter = adapter_.GetBluezAdapterObject();

  try {
    adapter.SetDiscoveryFilter(filter);
  } catch (const sdbus::Error &e) {
    DBUS_LOG_METHOD_CALL_ERROR(&adapter, "SetDiscoveryFilter", e);
    device_watcher_ = nullptr;
    return false;
  }

  try {
    LOG(INFO) << __func__ << ": Starting BR/EDR discovery on "
                      << adapter_.GetObjectPath();
    adapter.StartDiscovery();
  } catch (const sdbus::Error &e) {
    if (e.getName() != "org.bluez.Error.InProgress") {
      DBUS_LOG_METHOD_CALL_ERROR(&adapter, "StartDiscovery", e);
      device_watcher_ = nullptr;
      return false;
    }
  }

  return true;
}

bool BluetoothClassicMedium::StopDiscovery() {
  auto &adapter = adapter_.GetBluezAdapterObject();
  LOG(INFO) << __func__ << "Stopping discovery on "
                    << adapter.getProxy().getObjectPath();
  auto ret = true;
  try {
    adapter.StopDiscovery();
  } catch (const sdbus::Error &e) {
    DBUS_LOG_METHOD_CALL_ERROR(&adapter, "StopDiscovery", e);
    ret = false;
  }
  device_watcher_ = nullptr;

  return ret;
}

std::unique_ptr<api::BluetoothSocket> BluetoothClassicMedium::ConnectToService(
    api::BluetoothDevice &remote_device, const std::string &service_uuid,
    CancellationFlag *cancellation_flag) {
  if (!profile_manager_->ProfileRegistered(service_uuid)) {
    if (!profile_manager_->Register(std::nullopt, service_uuid)) {
      LOG(ERROR) << __func__ << ": Could not register profile "
                         << service_uuid << " with Bluez";
      return nullptr;
    }
  }

  auto address = remote_device.GetMacAddress();
  auto device = devices_->get_device_by_address(address);
  if (device == nullptr) {
    // bluez has no Device1 for this MAC: the peer advertises over BLE only and
    // was never discovered over BR/EDR, but Nearby extracted its Bluetooth
    // identity MAC from the advert. Google Nearby connects such peers over an
    // INSECURE RFCOMM socket addressed directly (no bond, no bluez object) --
    // its preferred off-network medium. Mirror that here instead of failing;
    // the bluez ConnectProfile path below only works for known devices.
    LOG(INFO) << __func__ << ": " << address.ToString()
              << " unknown to bluez; connecting insecure RFCOMM by address for "
              << service_uuid;
    auto fd = ConnectInsecureRfcommByAddress(address.ToString(), service_uuid,
                                             cancellation_flag);
    if (!fd.has_value()) {
      LOG(ERROR) << __func__ << ": insecure RFCOMM connect to "
                 << address.ToString() << " failed";
      return nullptr;
    }
    return std::unique_ptr<api::BluetoothSocket>(new BluetoothSocket(
        GetOrCreateRemoteMacDevice(address),
        sdbus::UnixFd(*fd, sdbus::adopt_fd)));
  }
  if (!device->Bonded()) {
    // Expected: the Nearby RFCOMM profile is registered insecure
    // (RequireAuthentication/Authorization=false), so an outgoing connect does
    // not need a prior bond. Not an error — just note it.
    VLOG(1) << __func__ << ": Device " << address.ToString()
            << " is not bonded (insecure profile, continuing)";
  }
  // Mark as pending BEFORE calling ConnectToProfile to win the race
  profile_manager_->MarkPendingOutgoing(service_uuid, address);

  bool connected = false;
  for (int attempt = 0; attempt < kConnectProfileMaxAttempts; ++attempt) {
    if (cancellation_flag != nullptr && cancellation_flag->Cancelled()) {
      break;
    }
    if (device->ConnectToProfile(service_uuid)) {
      connected = true;
      break;
    }
    if (attempt + 1 < kConnectProfileMaxAttempts) {
      LOG(INFO) << __func__ << ": ConnectProfile to " << address.ToString()
                << " for " << service_uuid << " failed (attempt " << attempt + 1
                << "/" << kConnectProfileMaxAttempts << "), retrying in "
                << kConnectProfileRetryBackoff;
      absl::SleepFor(kConnectProfileRetryBackoff);
    }
  }
  if (!connected) {
    profile_manager_->ClearPendingOutgoing(service_uuid, address);
    return nullptr;
  }

  auto fd = profile_manager_->GetServiceRecordFD(remote_device, service_uuid,
                                                 cancellation_flag);
  if (!fd.has_value()) {
    LOG(WARNING) << __func__
                         << ": Failed to get a new connection for profile "
                         << service_uuid << " for device " << address.ToString();
    return nullptr;
  }

  return std::unique_ptr<api::BluetoothSocket>(
      new BluetoothSocket(device, fd.value()));
}

std::shared_ptr<api::BluetoothServerSocket>
BluetoothClassicMedium::ListenForService(const std::string &service_name,
                                         const std::string &service_uuid) {
  if (!profile_manager_->ProfileRegistered(service_uuid)) {
    if (!profile_manager_->Register(service_name, service_uuid)) {
      LOG(ERROR) << __func__ << ": Could not register profile "
                         << service_name << " " << service_uuid
                         << " with Bluez";
      return nullptr;
    }
  }

  // We are about to accept incoming connections; make sure our auto-accept
  // BlueZ agent still owns the default-agent slot so an incoming Just-Works
  // pairing is answered here instead of stalling (SMP_RSP_TIMEOUT) when e.g.
  // GNOME's Bluetooth panel has grabbed the default agent.
  if (agent_manager_ != nullptr) {
    agent_manager_->EnsureDefaultAgent(
        /*capability=*/absl::string_view("NoInputNoOutput"),
        sdbus::ObjectPath(kBluezAgentPath));
  }

  return std::shared_ptr<api::BluetoothServerSocket>(
      new BluetoothServerSocket(*profile_manager_, service_uuid));
}

std::shared_ptr<BluetoothDevice>
BluetoothClassicMedium::GetOrCreateRemoteMacDevice(const MacAddress &address) {
  absl::MutexLock l(&remote_mac_devices_mutex_);
  const std::string key = address.ToString();
  auto it = remote_mac_devices_.find(key);
  if (it != remote_mac_devices_.end()) return it->second;
  auto device = std::make_shared<BluetoothDevice>(address);
  remote_mac_devices_[key] = device;
  return device;
}

api::BluetoothDevice *BluetoothClassicMedium::GetRemoteDevice(
MacAddress mac_address) {
  // When BLE is discovering, it looks for remote devices to connect to using BT classic.
  auto device = devices_->get_device_by_address(mac_address);
  if (device != nullptr) return device.get();

  // bluez never discovered this peer (it advertises over BLE only), but Nearby
  // gave us its BR/EDR identity MAC from the advert. Expose a bare-MAC device so
  // the BLUETOOTH endpoint is created (AppendRemoteBluetoothMacAddressEndpoint);
  // ConnectToService then connects it via insecure RFCOMM by address, matching
  // how Google Nearby reaches an unpaired peer off-network.
  return GetOrCreateRemoteMacDevice(mac_address).get();
}

std::unique_ptr<api::BluetoothPairing> BluetoothClassicMedium::CreatePairing(
    api::BluetoothDevice &remote_device) {
  auto device = devices_->get_device_by_address(remote_device.GetMacAddress());
  if (device == nullptr) return nullptr;

  return std::unique_ptr<api::BluetoothPairing>(
      new BluetoothPairing(adapter_, device));
}

}  // namespace linux
}  // namespace nearby
