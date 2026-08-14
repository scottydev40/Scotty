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
#include <chrono>
#include <functional>
#include <optional>
#include <string>

#include <sdbus-c++/Types.h>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/substitute.h"
#include "absl/synchronization/mutex.h"
#include "internal/platform/implementation/linux/bluetooth_adapter.h"
#include "internal/platform/implementation/linux/bluetooth_classic_device.h"
#include "internal/platform/implementation/linux/bluetooth_devices.h"
#include "internal/platform/implementation/linux/bluez.h"
#include "internal/platform/implementation/linux/dbus.h"
#include "internal/platform/implementation/linux/generated/dbus/bluez/device_client.h"
#include "internal/platform/logging.h"

namespace nearby {
namespace linux {
static constexpr std::chrono::minutes kLostPeripheralsCleanupMinFreq(5);
absl::Mutex g_shared_devices_lock;
absl::flat_hash_map<std::string, std::weak_ptr<SharedBluetoothDevices>>
    g_shared_devices ABSL_GUARDED_BY(g_shared_devices_lock);

std::shared_ptr<SharedBluetoothDevices> GetSharedBluetoothDevices(
    std::shared_ptr<sdbus::IConnection> system_bus,
    const sdbus::ObjectPath& adapter_object_path) {
  const std::string key = adapter_object_path;
  absl::MutexLock lock(&g_shared_devices_lock);
  auto it = g_shared_devices.find(key);
  if (it != g_shared_devices.end()) {
    if (auto existing = it->second.lock()) {
      return existing;
    }
  }
  auto shared = std::make_shared<SharedBluetoothDevices>();
  shared->observers =
      std::make_shared<ObserverList<api::BluetoothClassicMedium::Observer>>();
  shared->devices = std::make_shared<BluetoothDevices>(
      std::move(system_bus), adapter_object_path, *shared->observers);
  g_shared_devices[key] = shared;
  return shared;
}

std::shared_ptr<BluetoothDevice> BluetoothDevices::get_device_by_path(
    const sdbus::ObjectPath &device_object_path) {
  absl::ReaderMutexLock l(&devices_by_path_lock_);

  if (devices_by_path_.count(device_object_path) == 0) {
    return nullptr;
  }

  return devices_by_path_[device_object_path];
}
std::shared_ptr<BluetoothDevice> BluetoothDevices::get_device_by_unique_id(
  api::ble::BlePeripheral::UniqueId id)
{
  // converting from stoull to mac again
  id &= 0x0000FFFFFFFFFFFFULL; // keep 48 bits
  std::ostringstream oss;
  oss << std::hex << std::setfill('0') << std::setw(12) << id;
  std::string hex = oss.str(); // e.g. "aabbccddeeff"

  MacAddress addr;
  {
    std::string mac;
    for (int i = 0; i < 6; ++i) {
      if (i) mac.push_back(':');
      mac.append(hex.substr(i * 2, 2));
    }
    MacAddress::FromString(mac, addr);
  }

  return get_device_by_address(addr);
}
std::shared_ptr<BluetoothDevice> BluetoothDevices::get_device_by_address(
    const MacAddress &addr) {
  auto device_object_path =
      bluez::device_object_path(adapter_object_path_, addr.ToString());
  return get_device_by_path(sdbus::ObjectPath(device_object_path));
}

void BluetoothDevices::remove_device_by_path(
    const sdbus::ObjectPath &device_object_path) {
  absl::MutexLock l(&devices_by_path_lock_);

  devices_by_path_.erase(device_object_path);
}

void BluetoothDevices::mark_peripheral_lost(
    const sdbus::ObjectPath &device_object_path) {
  absl::ReaderMutexLock lock(&devices_by_path_lock_);
  if (devices_by_path_.count(device_object_path) == 0) {
    LOG(ERROR) << __func__ << ": Device " << device_object_path
                       << " doesn't exist";
    return;
  }
  devices_by_path_[device_object_path]->MarkLost();
}

void BluetoothDevices::cleanup_lost_peripherals() {
  auto now = std::chrono::steady_clock::now();
  absl::MutexLock lock(&devices_by_path_lock_);
  if ((now - last_cleanup_) < kLostPeripheralsCleanupMinFreq) {
    return;
  }
  last_cleanup_ = now;

  for (auto it = devices_by_path_.begin(), end = devices_by_path_.end();
       it != end;) {
    auto copy = it++;
    if (copy->second->Lost()) devices_by_path_.erase(copy);
  }
}

std::shared_ptr<MonitoredBluetoothDevice> BluetoothDevices::add_new_device(
    sdbus::ObjectPath device_object_path) {
  absl::MutexLock l(&devices_by_path_lock_);
  auto [device_it, inserted] = devices_by_path_.emplace(
      std::string(device_object_path),
      std::make_shared<MonitoredBluetoothDevice>(
          system_bus_,
          std::make_shared<bluez::Device>(system_bus_, device_object_path),
          observers_));
  if (!inserted) device_it->second->UnmarkLost();
  return device_it->second;
}

void DeviceWatcher::onInterfacesAdded(
    const sdbus::ObjectPath &objectPath,
    const std::map<sdbus::InterfaceName,
                   std::map<sdbus::PropertyName, sdbus::Variant>>
        &interfacesAndProperties) {
  auto path_prefix = absl::Substitute("$0/dev_", adapter_object_path_);
  if (objectPath.find(path_prefix) != 0) {
    return;
  }

  if (interfacesAndProperties.count(sdbus::InterfaceName(org::bluez::Device1_proxy::INTERFACE_NAME)) ==
      0)
    return;

  auto device = devices_->add_new_device(objectPath);
  device->SetDiscoveryCallback(discovery_cb_);

  // We are on the sdbus event-loop thread here, holding the sdbus connection
  // mutex. The discovery callback (FEF3 decode -> BleMedium::StartScanning) and
  // the observer notifications take Nearby-side locks and issue blocking bluez
  // reads; running them inline holds the sdbus mutex across those locks and can
  // deadlock against another thread doing a D-Bus call under a Nearby lock.
  // Offload to the serial worker so the event loop returns immediately.
  // Shared-ptr copies keep everything alive independent of `this`.
  auto discovery_cb = discovery_cb_;
  auto observers = observers_;
  callback_executor_.Execute([device, discovery_cb, observers]() {
    if (discovery_cb != nullptr &&
        discovery_cb->device_discovered_cb != nullptr) {
      discovery_cb->device_discovered_cb(*device);
    }
    if (observers != nullptr) {
      for (const auto &observer : observers->GetObservers()) {
        observer->DeviceAdded(*device);
      }
    }
  });
}

void DeviceWatcher::onInterfacesRemoved(
    const sdbus::ObjectPath &objectPath,
    const std::vector<sdbus::InterfaceName> &interfaces) {
  auto path_prefix = absl::Substitute("$0/dev_", adapter_object_path_);
  if (objectPath.find(path_prefix) != 0) {
    return;
  }

  auto removed_device_it = std::find(interfaces.begin(), interfaces.end(),
                                     org::bluez::Device1_proxy::INTERFACE_NAME);
  if (removed_device_it != interfaces.end()) {
    // Offload to the same serial worker as onInterfacesAdded (see there for
    // why): keeps the sdbus event loop from blocking on Nearby locks, and the
    // single thread preserves add-before-remove ordering per device.
    auto devices = devices_;
    auto discovery_cb = discovery_cb_;
    auto observers = observers_;
    callback_executor_.Execute([devices, discovery_cb, observers, objectPath]() {
      auto device = devices->get_device_by_path(objectPath);
      if (device == nullptr) {
        LOG(WARNING) << "onInterfacesRemoved: received InterfacesRemoved for a "
                        "device we don't know about: "
                     << objectPath;
        return;
      }

      LOG(INFO) << "onInterfacesRemoved: Device " << objectPath
                << " has been removed";
      if (discovery_cb != nullptr && discovery_cb->device_lost_cb != nullptr) {
        discovery_cb->device_lost_cb(*device);
      }

      if (observers != nullptr) {
        for (const auto &observer : observers->GetObservers()) {
          observer->DeviceRemoved(*device);
        }
        devices->remove_device_by_path(objectPath);
      } else {
        devices->mark_peripheral_lost(objectPath);
      }
    });
  }
}

void DeviceWatcher::notifyExistingDevices() {
  std::map<sdbus::ObjectPath,
           std::map<sdbus::InterfaceName, std::map<sdbus::PropertyName, sdbus::Variant>>>
      objects;
  try {
    objects = GetManagedObjects();
  } catch (const sdbus::Error &e) {
    DBUS_LOG_METHOD_CALL_ERROR(this, "GetManagedObjects", e);
    return;
  }

  std::vector<sdbus::ObjectPath> existing_device_paths;

  for (const auto& [device_path, interfaces] : objects) {
    if (device_path.find(absl::Substitute("$0/dev_", adapter_object_path_)) == 0 &&
        interfaces.count(sdbus::InterfaceName(org::bluez::Device1_proxy::INTERFACE_NAME)) == 1) {
      
      // Don't remove bonded, paired, connected, or trusted devices
      bool should_skip = false;
      auto device_interface_it = interfaces.find(sdbus::InterfaceName(org::bluez::Device1_proxy::INTERFACE_NAME));
      if (device_interface_it != interfaces.end()) {
        const auto& properties = device_interface_it->second;
        
        auto check_bool_property = [&properties](const sdbus::PropertyName& prop_name) -> bool {
          auto it = properties.find(prop_name);
          if (it != properties.end()) {
            try {
              return it->second.get<bool>();
            } catch (...) {
              return false;
            }
          }
          return false;
        };
        
        if (check_bool_property(sdbus::PropertyName("Bonded")) ||
            check_bool_property(sdbus::PropertyName("Paired")) ||
            check_bool_property(sdbus::PropertyName("Connected")) ||
            check_bool_property(sdbus::PropertyName("Trusted"))) {
          should_skip = true;
          LOG(INFO) << __func__ << ": Skipping device " << device_path
                    << " (bonded/paired/connected/trusted)";
        }

        // Also keep active Nearby (Quick Share) peers, i.e. anything currently
        // advertising the FEF3 copresence service. The watcher is rebuilt every
        // scan cycle (~10s) and this refresh does a destructive bluez
        // RemoveDevice; on an unbonded peer that is the exact device object an
        // in-flight send is connecting to, so removing it here deletes the
        // connect target mid-handshake (surfaced as Connect ... UnknownObject).
        // Non-Nearby stale devices are still refreshed as before.
        if (!should_skip) {
          auto has_nearby_service = [&properties]() -> bool {
            auto contains_fef3 = [](const std::string &s) {
              return s.find("fef3") != std::string::npos ||
                     s.find("FEF3") != std::string::npos;
            };
            auto uuids_it = properties.find(sdbus::PropertyName("UUIDs"));
            if (uuids_it != properties.end()) {
              try {
                for (const auto &u :
                     uuids_it->second.get<std::vector<std::string>>()) {
                  if (contains_fef3(u)) return true;
                }
              } catch (...) {
              }
            }
            auto sd_it = properties.find(sdbus::PropertyName("ServiceData"));
            if (sd_it != properties.end()) {
              try {
                for (const auto &[key, value] :
                     sd_it->second
                         .get<std::map<std::string, sdbus::Variant>>()) {
                  if (contains_fef3(key)) return true;
                }
              } catch (...) {
              }
            }
            return false;
          };
          if (has_nearby_service()) {
            should_skip = true;
            LOG(INFO) << __func__ << ": Skipping active Nearby peer "
                      << device_path << " (advertising FEF3)";
          }
        }
      }
      
      if (!should_skip) {
        existing_device_paths.push_back(device_path);
      }
    }
  }

  // NOTE: we deliberately do NOT remove existing devices here anymore.
  //
  // This used to RemoveDevice() every non-bonded device on each watcher
  // construction to force a re-discovery (InterfacesAdded re-fires the discovery
  // callback). But the watcher is rebuilt every scan cycle (~10s), so this
  // destructively churned the bluez device objects continuously. An unbonded
  // Nearby peer advertises under a stable RPA for the whole session, but its
  // bluez object kept getting deleted and recreated -- and an in-flight send
  // that took ~20s to connect would call Connect() during a deleted window,
  // producing 'Connect ... Device1 doesn't exist (UnknownObject)' and failing
  // the transfer. Genuinely new peers still fire InterfacesAdded naturally, and
  // the FEF3 scan decode runs from that path, so the forced refresh is not
  // needed for discovery. Leaving objects in place keeps the connect target
  // alive across the ~20s handshake.
  (void)existing_device_paths;
}

}  // namespace linux
}  // namespace nearby
