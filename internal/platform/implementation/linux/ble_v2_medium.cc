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

#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstring>

#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/Types.h>

#include <atomic>
#include <memory>
#include <regex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "internal/platform/implementation/ble.h"
#include "internal/platform/implementation/linux/ble_v2_medium.h"
#include "internal/platform/implementation/linux/ble_v2_gatt_connection.h"
#include "internal/platform/implementation/linux/ble_v2_socket.h"
#include "internal/platform/byte_array.h"
#include "internal/platform/count_down_latch.h"
#include "internal/platform/exception.h"
#include "internal/platform/uuid.h"
#include "internal/weave/connection.h"
#include "internal/weave/socket_callback.h"
#include "internal/weave/sockets/client_socket.h"

#include "connections/implementation/flags/nearby_connections_feature_flags.h"
#include "absl/types/span.h"
#include "absl/time/time.h"
#include "ble_l2cap_server_socket.h"
#include "ble_l2cap_socket.h"
#include "ble_gatt_server.h"
#include "internal/base/observer_list.h"
#include "internal/platform/implementation/linux/bluetooth_classic_device.h"
#include "internal/platform/implementation/linux/bluetooth_devices.h"
#include "internal/platform/implementation/linux/bluez.h"
#include "internal/platform/implementation/linux/bluez_advertisement_monitor.h"
#include "internal/platform/implementation/linux/utils.h"
#include "internal/platform/implementation/linux/bluez_advertisement_monitor_manager.h"
#include "internal/platform/implementation/linux/bluez_le_advertisement.h"
#include "internal/platform/implementation/linux/dbus.h"
#include "internal/platform/implementation/linux/generated/dbus/bluez/advertisement_monitor_server.h"
#include "internal/platform/implementation/linux/generated/dbus/bluez/le_advertisement_manager_client.h"
#include "internal/platform/mac_address.h"
#include "internal/platform/prng.h"
#include "internal/flags/nearby_flags.h"

namespace nearby {
namespace linux {

namespace {
using nearby::connections::config_package_nearby::nearby_connections_feature::kRefactorBleL2cap;

}  // namespace

BleV2Medium::BleV2Medium(BluetoothAdapter &adapter)
  : system_bus_(adapter.GetConnection()),
    adapter_(adapter),
  observers_(std::make_shared<ObserverList<api::BluetoothClassicMedium::Observer>>()),
  devices_(std::make_unique<BluetoothDevices>(
    system_bus_, adapter_.GetObjectPath(), *observers_)),
    gatt_discovery_(std::make_shared<BluezGattDiscovery>(system_bus_)),
    root_object_manager_(std::make_unique<RootObjectManager>(*system_bus_, sdbus::ObjectPath("/com/google/nearby/medium/ble/advertisement/monitor"))),
    adv_monitor_manager_(
      bluez::AdvertisementMonitorManager::
      DiscoverAdvertisementMonitorManager(*system_bus_, adapter_)),
    adv_manager_(std::make_unique<bluez::LEAdvertisementManager>(*system_bus_,
                                                                 adapter)),
    cur_adv_(nullptr) {
  if (!gatt_discovery_->InitializeKnownServices()) {
    LOG(WARNING) << __func__
                 << ": Failed to initialize known GATT services cache.";
  }


  if (adv_monitor_manager_) {
    LOG(INFO)
        << __func__
        << ": Registering path /com/google/nearby/medium/ble/advertisement/monitor with AdvertisementMonitorManager at "
        << adv_monitor_manager_->getProxy().getObjectPath();
    adv_monitor_manager_->SetRegisterMonitorReplyCallback(
        [this](std::optional<sdbus::Error> error) {
          OnRegisterMonitorReply(std::move(error));
        });
    adv_monitor_manager_->RegisterMonitor(
        root_object_manager_->getObject().getObjectPath());
  } else {
    adv_monitor_manager_ready_notification_.Notify();
  }
}

void BleV2Medium::OnRegisterMonitorReply(std::optional<sdbus::Error> error) {
  {
    absl::MutexLock lock(&adv_monitor_manager_ready_mutex_);
    adv_monitor_manager_ready_ = !error.has_value() || !error->isValid();
    if (error.has_value() && error->isValid()) {
      adv_monitor_manager_error_name_ = error->getName();
      adv_monitor_manager_error_message_ = error->getMessage();
    }
  }

  if (error.has_value() && error->isValid()) {
    LOG(ERROR) << __func__ << ": Got error '" << error->getName()
               << "' with message '" << error->getMessage()
               << "' while calling RegisterMonitor on object "
               << adv_monitor_manager_->getProxy().getObjectPath();
  }

  adv_monitor_manager_ready_notification_.Notify();
}

bool BleV2Medium::WaitForAdvertisementMonitorManager() {
  if (adv_monitor_manager_ == nullptr) {
    LOG(WARNING) << __func__ << ": Advertising monitor not supported by BlueZ";
    return false;
  }

  adv_monitor_manager_ready_notification_.WaitForNotification();

  absl::MutexLock lock(&adv_monitor_manager_ready_mutex_);
  if (adv_monitor_manager_ready_) {
    return true;
  }

  LOG(WARNING) << __func__
               << ": AdvertisementMonitorManager registration failed with name '"
               << adv_monitor_manager_error_name_ << "' and message '"
               << adv_monitor_manager_error_message_ << "'";
  return false;
}

  // sync api
  // called twice. Once with extended regular advertisement ( when IsExtendedAdvertisementsAvailable() == true )
  // and another for GATT-backed header advertisement for legacy devices
  bool BleV2Medium::StartAdvertising(
    const api::ble::BleAdvertisementData &advertising_data,
    api::ble::AdvertiseParameters advertise_set_parameters) {
    //if (!advertising_data.is_extended_advertisement)
    //{
    //  // can't send two LE advertisements at the same
    //  return true;
    //}
    if (!adapter_.IsEnabled()) {
      LOG(WARNING) << "BLE cannot start advertising because the "
                            "bluetooth adapter is not enabled.";
      return false;
    }

    if (advertising_data.service_data.empty()) {
      LOG(WARNING)
        << "BLE cannot start to advertise due to invalid service data.";
      return false;
    }

    absl::MutexLock l (&advs_mutex_);
    advs_.push_front(bluez::LEAdvertisement::CreateLEAdvertisement(
      *system_bus_, advertising_data, advertise_set_parameters));
    auto it = advs_.begin();


    LOG(INFO) << __func__ << ": Registering advertisement, is_extended: " << advertising_data.is_extended_advertisement
                  << " " << (*it) -> getObject().getObjectPath() << " on bluetooth adapter "
                    << adapter_.GetObjectPath();

    try {
      adv_manager_->RegisterAdvertisementSync((*it)->getObject().getObjectPath(), {});
    } catch (const sdbus::Error &e) {
      advs_.erase(it);
      DBUS_LOG_METHOD_CALL_ERROR(adv_manager_, "RegisterAdvertisementSync", e);
      return false;
    }

    return true;
  }

//async api
// runs with nearby presence
std::unique_ptr<api::ble::BleMedium::AdvertisingSession>
BleV2Medium::StartAdvertising(
  const api::ble::BleAdvertisementData &advertising_data,
  api::ble::AdvertiseParameters advertise_set_parameters,
  AdvertisingCallback callback) {
  if (!adapter_.IsEnabled()) {
    LOG(WARNING) << ": BLE cannot start advertising because the "
                            "bluetooth adapter is not enabled.";
    return nullptr;
  }

  if (advertising_data.service_data.empty()) {
    LOG(WARNING)
        << ": BLE cannot start to advertise due to invalid service data.";
    return nullptr;
  }

  std::shared_ptr<AdvertisingCallback> shared_cb =
    std::make_shared<AdvertisingCallback>(std::move(callback));

  absl::MutexLock lock(&advs_mutex_);
  advs_.push_front(bluez::LEAdvertisement::CreateLEAdvertisement(
    *system_bus_, advertising_data, advertise_set_parameters));
  auto adv_it = advs_.begin();

  // Keep async API surface, but register using the same typed DBus path as the
  // working sync implementation to avoid signature mismatch (oa{sv} vs sa{sv}).
  try {
    adv_manager_->RegisterAdvertisementSync((*adv_it)->getObject().getObjectPath(), {});
    shared_cb->start_advertising_result(absl::OkStatus());
  } catch (const sdbus::Error &e) {
    advs_.erase(adv_it);
    DBUS_LOG_METHOD_CALL_ERROR(adv_manager_, "RegisterAdvertisementSync", e);
    auto name = e.getName();
    std::string msg = e.getMessage();
    absl::Status status;
    if (name == "org.bluez.Error.InvalidArguments" ||
        name == "org.bluez.Error.InvalidLength") {
      status = absl::InvalidArgumentError(msg);
    } else if (name == "org.bluez.Error.AlreadyExists") {
      status = absl::AlreadyExistsError(msg);
    } else if (name == "org.bluez.Error.NotPermitted") {
      status = absl::ResourceExhaustedError(msg);
    } else {
      status = absl::UnknownError(msg);
    }
    shared_cb->start_advertising_result(std::move(status));
    return nullptr;
  }

  absl::AnyInvocable<absl::Status()> stop_adv = [&, adv_it]() {
    LOG(INFO) << __func__ << ": Unregistering advertisement object "
                         << (*adv_it)->getObject().getObjectPath();
    absl::MutexLock lock(&advs_mutex_);
    try {
      adv_manager_->UnregisterAdvertisementSync((*adv_it)->getObject().getObjectPath());
    } catch (const sdbus::Error &e) {
      DBUS_LOG_METHOD_CALL_ERROR(adv_manager_, "UnregisterAdvertisementSync", e);
      return absl::UnknownError(e.getMessage());
    }
    advs_.erase(adv_it);
    return absl::OkStatus();
  };
  return std::make_unique<api::ble::BleMedium::AdvertisingSession>(
    api::ble::BleMedium::AdvertisingSession{std::move(stop_adv)});
}

  bool BleV2Medium::StopAdvertising() {
    absl::MutexLock l(&advs_mutex_);
    LOG(INFO) << "StopAdvertising: advs_.size()=" << advs_.size();
    try {
      for (auto& adv: advs_)
      {
        LOG(INFO) << "StopAdvertising: unregistering " << adv->getObject().getObjectPath();
        adv_manager_->UnregisterAdvertisementSync(adv->getObject().getObjectPath());
      }
    } catch (const sdbus::Error &e) {
      DBUS_LOG_METHOD_CALL_ERROR(adv_manager_, "UnregisterAdvertisementSync", e);
      return false;
    }

    advs_.clear();
    return true;
  }

namespace {
using AdvFoundCb = absl::AnyInvocable<void(
    api::ble::BlePeripheral::UniqueId, api::ble::BleAdvertisementData)>;

// Build a general-discovery DiscoveryCallback that decodes a peer's Nearby
// (FEF3) ServiceData into a scan result. This is the controller-agnostic scan
// path: bluez's AdvertisementMonitor offload is unavailable here (RegisterMonitor
// is a no-op and the MT7925/kernel has no MSFT/adv-monitor offload), so the
// monitor's DeviceFound never fires. The same adverts still arrive via normal
// LE discovery with ServiceData populated on the bluez device object, so decode
// them here instead. Mirrors bluez_advertisement_monitor.cc DeviceFound().
std::unique_ptr<api::BluetoothClassicMedium::DiscoveryCallback>
MakeFef3ScanDiscoveryCallback(std::shared_ptr<AdvFoundCb> found_cb) {
  auto discovery_cb =
      std::make_unique<api::BluetoothClassicMedium::DiscoveryCallback>();
  discovery_cb->device_discovered_cb =
      [found_cb](api::BluetoothDevice &device) {
        auto &dev = static_cast<BluetoothDevice &>(device);
        auto service_data = dev.ServiceData();
        if (!service_data.has_value()) return;

        struct api::ble::BleAdvertisementData adv_data;
        for (const auto &[uuid_str, data] : *service_data) {
          auto uuid = UuidFromString(uuid_str);
          if (!uuid.has_value()) continue;
          std::vector<uint8_t> bytes = data.get<std::vector<uint8_t>>();
          adv_data.service_data.emplace(
              *uuid, std::string(bytes.begin(), bytes.end()));
        }
        if (adv_data.service_data.empty()) return;

        auto mac = dev.GetMacAddress().ToString();
        auto id = std::stoull(std::regex_replace(mac, std::regex("[:\\-]"), ""),
                              nullptr, 16);
        (*found_cb)(id, adv_data);
      };
  return discovery_cb;
}
}  // namespace

bool BleV2Medium::StartScanning(const Uuid &service_uuid,
                                api::ble::TxPowerLevel tx_power_level,
                                ScanCallback callback) {
  if (cur_monitored_service_uuid_.has_value()) {
    LOG(ERROR) << __func__
                       << ": A sync scanning session is already active for "
                       << std::string{*cur_monitored_service_uuid_};
    return false;
  }

  if (!WaitForAdvertisementMonitorManager()) {
    // TODO: Implement manual monitoring.
    return false;
  }

  if (!MonitorManagerSupportsOr()) {
    LOG(WARNING)
        << __func__
        << ": \"or_patterns\" not supported by AdvertisementMonitorManager";
    // TODO: Implement manual monitoring.
    return false;
  }

  absl::MutexLock lock(&active_adv_monitors_mutex_);
  if (active_adv_monitors_.count(service_uuid) == 1) {
    LOG(ERROR) << __func__ << ": an advertising session for service "
                       << std::string{service_uuid} << " already exists";
    return false;
  }

  // The AdvertisementMonitor offload path is dead on this platform (its
  // RegisterMonitor is a no-op and the controller has no adv-monitor offload,
  // so DeviceFound never fires). Keep the monitor object for its D-Bus presence
  // but hand the real scan callback to the DeviceWatcher below, which decodes
  // FEF3 ServiceData from devices found via normal LE discovery.
  auto found_cb = std::make_shared<AdvFoundCb>(
      std::move(callback.advertisement_found_cb));
  auto monitor = std::make_unique<bluez::AdvertisementMonitor>(
    *system_bus_, service_uuid, tx_power_level, "or_patterns", devices_,
    api::ble::BleMedium::ScanCallback{});
  try {
    // why is this emitted?
    monitor->emitInterfacesAddedSignal(
      {sdbus::InterfaceName(org::bluez::AdvertisementMonitor1_adaptor::INTERFACE_NAME)});

    // adv_monitor_manager_ -> RegisterMonitor(monitor -> getObject().getObjectPath());
    LOG(INFO)<< __func__ << ": Registered advertisement monitor with path " << monitor -> getObject().getObjectPath();
  } catch (const sdbus::Error &e) {
    LOG(ERROR)
        << __func__
        << ": error emitting InterfacesAdded signal for object path "
        << monitor->getObject().getObjectPath() << " with name '" << e.getName()
        << "' and message '" << e.getMessage() << "'";
    return false;
  }
  auto device_watcher = std::make_unique<DeviceWatcher>(
    *system_bus_, adapter_.GetObjectPath(), adapter_, devices_,
    MakeFef3ScanDiscoveryCallback(found_cb), observers_);
  if (!StartLEDiscovery()) {
    LOG(ERROR) << __func__
                       << ": Could not start LE discovery on adapter "
                       << adapter_.GetObjectPath();
    device_watcher = nullptr;
    try {
      monitor->emitInterfacesRemovedSignal(
        {sdbus::InterfaceName(org::bluez::AdvertisementMonitor1_adaptor::INTERFACE_NAME)});
    } catch (const sdbus::Error &e) {
      LOG(ERROR)
          << __func__
          << ": error emitting InterfacesRemoved signal for object path "
          << monitor->getObject().getObjectPath() << " with name '" << e.getName()
          << "' and message '" << e.getMessage() << "'";
    }
    return false;
  }
  LOG(INFO) << __func__ << " :Started monitoring for service UUID: " << std::string(service_uuid);

  active_adv_monitors_[service_uuid] =
    std::make_pair(std::move(monitor), std::move(device_watcher));
  cur_monitored_service_uuid_ = service_uuid;
  return true;
}

bool BleV2Medium::StopScanning() {
  if (!cur_monitored_service_uuid_.has_value()) {
    LOG(ERROR) << __func__
                       << ": No sync scanning session is currently active.";
    return false;
  }

  if (!WaitForAdvertisementMonitorManager()) {
    // TODO: Implement manual monitoring.
    return false;
  }

  auto &adapter = adapter_.GetBluezAdapterObject();
  LOG(INFO) << __func__ << ": Stopping discovery for adapter "
                       << adapter.getProxy().getObjectPath();
  try {
    adapter.StopDiscovery(); // this will stop bluetooth classic discovery as well. do we want this?
  } catch (const sdbus::Error &e) {
    DBUS_LOG_METHOD_CALL_ERROR(&adapter, "StopDiscovery", e);
  }

  absl::MutexLock lock(&active_adv_monitors_mutex_);
  auto monitor_it = active_adv_monitors_.find(*cur_monitored_service_uuid_);
  assert(monitor_it != active_adv_monitors_.end());
  {
    auto &[_uuid, session] = *monitor_it;
    auto &[adv_monitor, _watcher] = session;

    LOG(INFO) << __func__ << ": Removing advertising monitor "
                         << adv_monitor->getObject().getObjectPath();
    adv_monitor->emitInterfacesRemovedSignal(
      {sdbus::InterfaceName(org::bluez::AdvertisementMonitor1_adaptor::INTERFACE_NAME)});
  }
  active_adv_monitors_.erase(monitor_it);
  cur_monitored_service_uuid_ = std::nullopt;

  return true;
}
  std::unique_ptr<api::ble::BleMedium::ScanningSession>
  BleV2Medium::StartScanning(const Uuid &service_uuid,
                             api::ble::TxPowerLevel tx_power_level,
                             ScanningCallback callback) {
    if (!WaitForAdvertisementMonitorManager()) {
      // TODO: Implement manual monitoring.
      return nullptr;
    }

    absl::MutexLock lock(&active_adv_monitors_mutex_);
    if (active_adv_monitors_.count(service_uuid) == 1) {
      LOG(ERROR) << __func__ << ": Service " << std::string{service_uuid}
                       << " is already being advertised";
      return nullptr;
    }

    auto monitor = std::make_unique<bluez::AdvertisementMonitor>(
      *system_bus_, service_uuid, tx_power_level, "or_patterns", devices_,
      std::move(callback));
    try {
      monitor->emitInterfacesAddedSignal(
        {sdbus::InterfaceName(org::bluez::AdvertisementMonitor1_adaptor::INTERFACE_NAME)});
    } catch (const sdbus::Error &e) {
      LOG(ERROR)
        << __func__
        << ": error emitting InterfacesAdded signal for object path "
        << monitor->getObject().getObjectPath() << " with name '" << e.getName()
        << "' and message '" << e.getMessage() << "'";
      return nullptr;
    }

    auto device_watcher = std::make_unique<DeviceWatcher>(
      *system_bus_, adapter_.GetObjectPath(),adapter_, devices_);
    if (!StartLEDiscovery()) {
      LOG(ERROR) << __func__
                       << ": Could not start LE discovery on adapter "
                       << adapter_.GetObjectPath();
      try {
        monitor->emitInterfacesRemovedSignal(
          {sdbus::InterfaceName(org::bluez::AdvertisementMonitor1_adaptor::INTERFACE_NAME)});
      } catch (const sdbus::Error &e) {
        LOG(ERROR)
          << __func__
          << ": error emitting InterfacesRemoved signal for object path "
          << monitor->getObject().getObjectPath() << " with name '" << e.getName()
          << "' and message '" << e.getMessage() << "'";
      }
      return nullptr;
    }

    active_adv_monitors_[service_uuid] =
      std::make_pair(std::move(monitor), std::move(device_watcher));

    return std::make_unique<ScanningSession>(
      ScanningSession{.stop_scanning = [this, service_uuid]() {
        absl::MutexLock lock(&active_adv_monitors_mutex_);
        if (active_adv_monitors_.count(service_uuid) == 0) {
          LOG(ERROR)
              << __func__ << ": Advertising monitor for service "
              << std::string{service_uuid} << " does not exist anymore";
          return absl::NotFoundError(
            "Advertising monitor for this service does not exist");
        }

        auto &[monitor, watcher] = active_adv_monitors_[service_uuid];
        try {
          monitor->emitInterfacesRemovedSignal(
            {sdbus::InterfaceName(org::bluez::AdvertisementMonitor1_adaptor::INTERFACE_NAME)});
        } catch (const sdbus::Error &e) {
          LOG(ERROR)
              << __func__
              << ": error emitting InterfacesRemoved signal for object path "
              << monitor->getObject().getObjectPath() << " with name '" << e.getName()
              << "' and message '" << e.getMessage() << "'";
        }

        auto &adapter = adapter_.GetBluezAdapterObject();
        absl::Status status;
        try {
          adapter.StopDiscovery();
          status = absl::OkStatus();
        } catch (const sdbus::Error &e) {
          DBUS_LOG_METHOD_CALL_ERROR(&adapter, "StopDiscovery", e);
          status = absl::InternalError(e.getMessage());
        }
        active_adv_monitors_.erase(service_uuid);
        return status;
      }});
  }

std::unique_ptr<api::ble::GattServer> BleV2Medium::StartGattServer(
  api::ble::ServerGattConnectionCallback callback) {
  LOG(INFO) << __func__ << ": Starting Linux GATT server.";
  return std::make_unique<GattServer>(*system_bus_, adapter_, devices_,
                                      std::move(callback));
}

std::optional<sdbus::ObjectPath> BleV2Medium::ResolvePeripheralPath(
    api::ble::BlePeripheral::UniqueId peripheral_id,
    const Uuid &service_uuid) {
  // 1) A device already exposing the resolved Nearby (FEF3) service. Once bluez
  //    has resolved the peer's GATT tree this is the identity device, and it
  //    stays valid across the peer's NRPA rotation.
  if (auto resolved = gatt_discovery_->FindDeviceExposingService(service_uuid)) {
    return resolved;
  }
  // 2) Bonded devices (the user's own phone) survive RPA rotation even after
  //    bluez deletes the advertised device object. Connect() drives the GATT
  //    resolution a passive lookup misses; re-check for the service afterward
  //    and only accept a bonded device that actually exposes it.
  for (const auto &path : gatt_discovery_->GetBondedDevicePaths()) {
    if (auto d = devices_->get_device_by_path(path)) d->Connect();
    if (auto resolved =
            gatt_discovery_->FindDeviceExposingService(service_uuid)) {
      return resolved;
    }
  }
  // 3) Last resort: the advertised (RPA) device from the baked UniqueId. Only
  //    valid until the peer rotates its NRPA, but it's the best available when
  //    the peer isn't bonded and hasn't resolved the service yet.
  if (auto device = devices_->get_device_by_unique_id(peripheral_id)) {
    return device->GetObjectPath();
  }
  return std::nullopt;
}

std::unique_ptr<api::ble::GattClient> BleV2Medium::ConnectToGattServer(
  api::ble::BlePeripheral::UniqueId peripheral_id,
  api::ble::TxPowerLevel tx_power_level,
  api::ble::ClientGattConnectionCallback callback) {
  // The saved peripheral_id encodes the RPA MAC captured at discovery time; the
  // peer rotates its NRPA and bluez deletes the stale device object, so resolve
  // the current path (resolved-service -> bonded -> RPA) instead of a bare
  // get_device_by_unique_id() that misses after rotation.
  const Uuid service_uuid("0000FEF3-0000-1000-8000-00805F9B34FB");
  auto peripheral_object_path = ResolvePeripheralPath(peripheral_id, service_uuid);
  if (!peripheral_object_path) {
    LOG(ERROR) << __func__ << ": Failed to resolve device for unique ID "
               << peripheral_id;
    return nullptr;
  }

  // Tx power is not configurable for this Linux GATT path yet.
  (void)tx_power_level;

  LOG(INFO) << __func__ << ": Creating Linux GATT client for peripheral "
            << *peripheral_object_path;
  return std::make_unique<GattClient>(system_bus_, *peripheral_object_path,
                                      gatt_discovery_,
                                      std::move(callback.disconnected_cb));
}

std::unique_ptr<api::ble::BleServerSocket> BleV2Medium::OpenServerSocket(
  const std::string &service_id) {
  LOG(INFO) << __func__ << ": Opening BLE server socket for service "
            << service_id;
  return std::make_unique<BleV2ServerSocket>(service_id);
}

std::unique_ptr<api::ble::BleL2capServerSocket>
BleV2Medium::OpenL2capServerSocket(const std::string &service_id) {
  // return nullptr;
  LOG(INFO) << __func__ << ": Opening L2CAP server socket for service "
            << service_id;

  auto server_socket = std::make_unique<linux::BleL2capServerSocket>(
      psm_, service_id);
  if (!server_socket->IsValid()) {
    LOG(ERROR) << __func__
               << ": Failed to open L2CAP server socket for service "
               << service_id;
    return nullptr;
  }

  return server_socket;
}

// Outgoing BLE socket, framed with the Weave protocol over the remote's Nearby
// GATT socket characteristics (FEF3 copresence service). This is the client
// counterpart of the server-side ble_v2_socket_adapter and mirrors Apple's
// BleMedium::Connect: build a Connection over the GATT write/indicate chars,
// drive the core weave::ClientSocket handshake, then hand back a BleV2Socket.
std::unique_ptr<api::ble::BleSocket> BleV2Medium::Connect(
  const std::string &service_id, api::ble::TxPowerLevel tx_power_level,
  api::ble::BlePeripheral::UniqueId peripheral_id,
  CancellationFlag *cancellation_flag) {
  (void)tx_power_level;
  LOG(INFO) << __func__ << ": Weave-over-GATT connect to peripheral "
            << peripheral_id << " for service " << service_id;

  // Nearby copresence service + the remote's Weave socket characteristics.
  // The write/indicate UUIDs were confirmed by GATT introspection of an Android
  // Quick Share peer (write = ...101, indicate = ...102 under FEF3).
  const Uuid service_uuid("0000FEF3-0000-1000-8000-00805F9B34FB");
  const Uuid write_char_uuid("00000100-0004-1000-8000-001a11000101");
  const Uuid indicate_char_uuid("00000100-0004-1000-8000-001a11000102");

  // BLE peers advertise under a rotating private (RPA) address, so the
  // peripheral_id -> device mapping points at a transient RPA device object
  // that bluez often deletes on rotation (Connect => UnknownObject) and which
  // never hosts the GATT server. The resolved GATT tree lives on the bonded,
  // already-connected identity device. Actively try to discover the FEF3 socket
  // characteristics on each candidate (connected devices first, RPA last),
  // bounding each attempt so a wrong candidate can't stall the whole request.
  (void)cancellation_flag;
  const std::vector<Uuid> socket_char_uuids = {write_char_uuid,
                                               indicate_char_uuid};
  auto try_discover = [&](const sdbus::ObjectPath &path,
                          absl::Duration timeout) -> bool {
    CancellationFlag cancel;
    absl::Notification done;
    std::thread timer([&]() {
      if (!done.WaitForNotificationWithTimeout(timeout)) cancel.Cancel();
    });
    bool ok = gatt_discovery_->DiscoverServiceAndCharacteristics(
        path, service_uuid, socket_char_uuids, cancel);
    done.Notify();
    timer.join();
    return ok;
  };

  sdbus::ObjectPath device_object_path;
  bool discovered = false;

  // 1) Fast path: a device that already exposes a resolved FEF3 service.
  if (auto resolved = gatt_discovery_->FindDeviceExposingService(service_uuid)) {
    if (try_discover(*resolved, absl::Seconds(8))) {
      device_object_path = *resolved;
      discovered = true;
      LOG(INFO) << __func__ << ": using resolved GATT device "
                << device_object_path;
    }
  }

  // 2) Bonded devices (the user's own phone). Bonded device objects are
  //    persistent even though the phone's BLE link flaps and it advertises
  //    under a rotating private address, so this is the stable way to reach it:
  //    (re)establish the link with Connect(), which drives GATT resolution that
  //    a passive lookup misses, then actively discover the socket chars.
  if (!discovered) {
    for (const auto &path : gatt_discovery_->GetBondedDevicePaths()) {
      if (auto d = devices_->get_device_by_path(path)) d->Connect();
      if (try_discover(path, absl::Seconds(10))) {
        device_object_path = path;
        discovered = true;
        LOG(INFO) << __func__ << ": discovered Nearby GATT on bonded device "
                  << device_object_path;
        break;
      }
    }
  }

  // 3) Last resort: connect the advertised (RPA) device directly.
  if (!discovered) {
    if (auto device = devices_->get_device_by_unique_id(peripheral_id)) {
      auto rpa_path = device->GetObjectPath();
      if (device->Connect() && try_discover(rpa_path, absl::Seconds(8))) {
        device_object_path = rpa_path;
        discovered = true;
      }
    }
  }

  if (!discovered) {
    LOG(ERROR) << __func__
               << ": Failed to discover Nearby GATT socket characteristics for "
                  "peripheral "
               << peripheral_id;
    return nullptr;
  }

  auto connection = std::make_unique<BleV2GattConnection>(
      gatt_discovery_, device_object_path, service_uuid, write_char_uuid,
      indicate_char_uuid);

  auto ble_socket = std::make_unique<BleV2Socket>(peripheral_id);
  BleV2Socket *ble_socket_ptr = ble_socket.get();

  struct ConnectState {
    CountDownLatch latch{1};
    std::atomic<bool> connected{false};
  };
  auto state = std::make_shared<ConnectState>();

  weave::SocketCallback socket_callback;
  socket_callback.on_connected_cb = [state]() {
    state->connected = true;
    state->latch.CountDown();
  };
  socket_callback.on_disconnected_cb = [state]() { state->latch.CountDown(); };
  socket_callback.on_error_cb = [state](absl::Status status) {
    LOG(WARNING) << "Weave-over-GATT socket error: " << status;
    state->latch.CountDown();
  };
  socket_callback.on_receive_cb = [ble_socket_ptr](std::string message) {
    ble_socket_ptr->ReceiveData(ByteArray(std::move(message)));
  };

  auto weave_socket = std::make_unique<weave::ClientSocket>(
      *connection, std::move(socket_callback));
  weave::ClientSocket *weave_socket_ptr = weave_socket.get();

  // App-level writes go through Weave (framing + flow control) to the GATT
  // write characteristic.
  ble_socket->SetWriteCallback(
      [weave_socket_ptr](absl::string_view data) -> bool {
        weave_socket_ptr->Write(ByteArray(std::string(data)));
        return true;
      });

  weave_socket->Connect();

  ExceptionOr<bool> awaited = state->latch.Await(absl::Seconds(10));
  if (!awaited.ok() || !awaited.result() || !state->connected) {
    LOG(ERROR) << __func__ << ": Weave handshake did not complete for "
               << device_object_path;
    return nullptr;
  }

  ble_socket->AttachWeaveClient(std::move(connection), std::move(weave_socket));
  LOG(INFO) << __func__ << ": Weave-over-GATT socket established to "
            << device_object_path;
  return ble_socket;
}

bool BleV2Medium::IsExtendedAdvertisementsAvailable() {
  try {
    auto supported_channels = adv_manager_->SupportedSecondaryChannels();
    return !supported_channels.empty();
  } catch (const sdbus::Error &e) {
    DBUS_LOG_PROPERTY_GET_ERROR(adv_manager_, "SupportedSecondaryChannels", e);
    return false;
  }
}

bool BleV2Medium::StartLEDiscovery() {
  std::map<std::string, sdbus::Variant> filter;
  filter["Transport"] = sdbus::Variant("auto");
  filter["DuplicateData"] = sdbus::Variant(true);
  auto &adapter = adapter_.GetBluezAdapterObject();

  try {
    adapter.SetDiscoveryFilter(filter);
  } catch (const sdbus::Error &e) {
    DBUS_LOG_METHOD_CALL_ERROR(&adapter, "SetDiscoveryFilter", e);
    return false;
  }

  try {
    LOG(INFO) << __func__ << ": Starting LE discovery on "
                      << adapter.getProxy().getObjectPath();
    adapter.StartDiscovery();
  } catch (const sdbus::Error &e) {
    if (e.getName() != "org.bluez.Error.InProgress") {
      DBUS_LOG_METHOD_CALL_ERROR(&adapter, "StartDiscovery", e);
      return false;
    }
  }

  return true;
}

// std::unique_ptr<api::ble::BleSocket> BleV2Medium::Connect(
//     const std::string &service_id, api::ble::TxPowerLevel tx_power_level,
//     api::ble::BlePeripheral &peripheral,
//     CancellationFlag *cancellation_flag) {
//   LOG(WARNING) << __func__ << ": BLE socket connections not implemented on Linux";
//   return nullptr;
// }

std::unique_ptr<api::ble::BleL2capSocket> BleV2Medium::ConnectOverL2cap(
    int psm, const std::string &service_id,
    api::ble::TxPowerLevel tx_power_level,
    api::ble::BlePeripheral::UniqueId peripheral_id,
    CancellationFlag *cancellation_flag) {
  // The saved peripheral_id encodes the RPA MAC captured at discovery time; the
  // peer rotates its NRPA and bluez deletes the stale device object, so resolve
  // the current path (resolved-service -> bonded -> RPA) then take the live
  // device object from it for the L2CAP address/type.
  const Uuid service_uuid("0000FEF3-0000-1000-8000-00805F9B34FB");
  auto resolved_path = ResolvePeripheralPath(peripheral_id, service_uuid);
  if (!resolved_path) {
    LOG(ERROR) << __func__ << ": Failed to resolve device for unique ID "
               << peripheral_id;
    return nullptr;
  }
  auto device = devices_->get_device_by_path(*resolved_path);
  if (!device) {
    LOG(ERROR) << __func__ << ": Resolved path has no device object: "
               << *resolved_path;
    return nullptr;
  }

  LOG(INFO) << __func__ << ": Connecting to L2CAP PSM " << psm
            << " on device " << device->GetMacAddress().ToString();


  int fd = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
  if (fd < 0) {
    LOG(ERROR) << __func__ << ": Failed to create L2CAP socket: "
               << std::strerror(errno);
    return nullptr;
  }

  // Set receive MTU before connect (for LE CoC)


  struct sockaddr_l2 addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.l2_family = AF_BLUETOOTH;
  addr.l2_psm = htobs(psm);
  if (device -> GetAddressType() == "random") {
    addr.l2_bdaddr_type = BDADDR_LE_RANDOM;
  }else {
    addr.l2_bdaddr_type = BDADDR_LE_PUBLIC;
  }

  if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
    LOG(INFO) << "Failed to bind L2CAP socket";
  }

  struct l2cap_options opts;
  opts.omtu = 0;
  opts.imtu = 672;
  if (setsockopt(fd, SOL_BLUETOOTH, BT_RCVMTU, &opts.imtu, sizeof(opts.imtu)) < 0) {
    LOG(WARNING) << __func__ << ": Failed to set BT_RCVMTU: "
                 << std::strerror(errno);
  }
  std::string mac_addr = device->GetMacAddress().ToString();
  if (str2ba(mac_addr.c_str(), &addr.l2_bdaddr) < 0) {
    LOG(ERROR) << __func__ << ": Invalid Bluetooth address: " << mac_addr;
    close(fd);
    return nullptr;
  }

  if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    LOG(ERROR) << __func__ << ": Failed to connect to L2CAP socket: "
               << std::strerror(errno);
    close(fd);
    return nullptr;
  }

  LOG(INFO) << __func__ << ": Successfully connected to L2CAP socket";
  auto socket = std::make_unique<BleL2capSocket>(
      fd, peripheral_id, service_id);
  return socket;
}

bool BleV2Medium::StartMultipleServicesScanning(
    const std::vector<Uuid> &service_uuids,
    api::ble::TxPowerLevel tx_power_level, ScanCallback callback) {
  LOG(WARNING) << __func__
               << ": Multiple services scanning not implemented on Linux. "
               << "Use single service scanning instead.";
  return false;
}

bool BleV2Medium::PauseMediumScanning() {
  LOG(INFO) << __func__ << ": Pause scanning not implemented, returning success";
  return true;
}

bool BleV2Medium::ResumeMediumScanning() {
  LOG(INFO) << __func__ << ": Resume scanning not implemented, returning success";
  return true;
}

void BleV2Medium::AddAlternateUuidForService(uint16_t uuid,
                                             const std::string &service_id) {
  LOG(INFO) << __func__ << ": Alternate UUID mapping not implemented. UUID: "
            << uuid << ", service_id: " << service_id;
}

std::optional<api::ble::BlePeripheral::UniqueId>
BleV2Medium::RetrieveBlePeripheralIdFromNativeId(
    const std::string &ble_peripheral_native_id) {
  LOG(WARNING) << __func__
               << ": Retrieval from native ID not implemented. Native ID: "
               << ble_peripheral_native_id;
  return std::nullopt;
}

}  // namespace linux
}  // namespace nearby
