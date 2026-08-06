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

#ifndef PLATFORM_IMPL_LINUX_BLUETOOTH_ADAPTER_H_
#define PLATFORM_IMPL_LINUX_BLUETOOTH_ADAPTER_H_
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/ProxyInterfaces.h>
#include <sdbus-c++/StandardInterfaces.h>
#include <sdbus-c++/Types.h>

#include <atomic>
#include <map>
#include <vector>

#include "absl/strings/string_view.h"
#include "internal/platform/implementation/bluetooth_adapter.h"
#include "internal/platform/implementation/linux/bluez.h"
#include "internal/platform/implementation/linux/dbus.h"
#include "internal/platform/implementation/linux/generated/dbus/bluez/adapter_client.h"

namespace nearby {
namespace linux {
class BluezAdapter
    : public sdbus::ProxyInterfaces<org::bluez::Adapter1_proxy,
                                    sdbus::Properties_proxy> {
 public:
  BluezAdapter(sdbus::IConnection &system_bus,
               const sdbus::ObjectPath &adapter_object_path)
      : ProxyInterfaces(system_bus, sdbus::ServiceName(bluez::SERVICE_DEST),
                        adapter_object_path) {
    registerProxy();
    // Seed the cache with a single synchronous read at construction. No
    // transfer is in flight yet, so this one call can't contend with the send
    // path; thereafter PropertiesChanged keeps it fresh and PoweredCached()
    // never makes a blocking D-Bus call again.
    try {
      powered_.store(Powered(), std::memory_order_relaxed);
    } catch (const sdbus::Error &e) {
      DBUS_LOG_PROPERTY_GET_ERROR(this, "Powered", e);
    }
  }
  ~BluezAdapter() { unregisterProxy(); }

  // The adapter's "Powered" state, cached from PropertiesChanged. Reading it
  // does no D-Bus round-trip, so it is safe to call from the Nearby service
  // thread: a synchronous Powered() read there deadlocks against the sdbus
  // event-loop thread that delivers BLE scan callbacks (both contend on the
  // connection mutex + the BLE medium mutex). See BluetoothAdapter::IsEnabled.
  bool PoweredCached() const {
    return powered_.load(std::memory_order_relaxed);
  }

 protected:
  void onPropertiesChanged(
      const sdbus::InterfaceName &interfaceName,
      const std::map<sdbus::PropertyName, sdbus::Variant> &changedProperties,
      const std::vector<sdbus::PropertyName> & /*invalidatedProperties*/)
      override {
    if (interfaceName != bluez::ADAPTER_INTERFACE) {
      return;
    }
    for (const auto &[name, value] : changedProperties) {
      if (name == "Powered") {
        powered_.store(value.get<bool>(), std::memory_order_relaxed);
      }
    }
  }

 private:
  std::atomic<bool> powered_{false};
};

class BluetoothAdapter : public api::BluetoothAdapter {
 public:
  BluetoothAdapter(std::shared_ptr<sdbus::IConnection> system_bus,
                   const sdbus::ObjectPath &adapter_object_path)
      : system_bus_(std::move(system_bus)),
        bluez_adapter_(std::make_shared<BluezAdapter>(*system_bus_,
                                                      adapter_object_path)) {}

  ~BluetoothAdapter() override = default;

  bool SetStatus(Status status) override;
  bool IsEnabled() const override;

  ScanMode GetScanMode() const override;

  bool SetScanMode(ScanMode scan_mode) override;
  std::string GetName() const override;

  bool SetName(absl::string_view name) override;
  bool SetName(absl::string_view name, bool persist) override;
  MacAddress GetMacAddress() const override;

  bool RemoveDeviceByObjectPath(const sdbus::ObjectPath &device_object_path) {
    try {
      bluez_adapter_->RemoveDevice(device_object_path);
      return true;
    } catch (const sdbus::Error &e) {
      DBUS_LOG_METHOD_CALL_ERROR(bluez_adapter_, "RemoveDevice", e);
      return false;
    }
  }

  sdbus::ObjectPath GetObjectPath() const {
    return bluez_adapter_->getProxy().getObjectPath();
  }

  BluezAdapter &GetBluezAdapterObject() { return *bluez_adapter_; }
  std::shared_ptr<sdbus::IConnection> GetConnection() { return system_bus_; }

 private:
  std::shared_ptr<sdbus::IConnection> system_bus_;
  std::shared_ptr<BluezAdapter> bluez_adapter_;
};
}  // namespace linux
}  // namespace nearby

#endif  // PLATFORM_IMPL_LINUX_BLUETOOTH_ADAPTER_H_
