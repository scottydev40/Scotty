// Copyright 2026 Google LLC
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

#ifndef PLATFORM_IMPL_LINUX_BLE_V2_GATT_CONNECTION_H_
#define PLATFORM_IMPL_LINUX_BLE_V2_GATT_CONNECTION_H_

#include <memory>
#include <string>

#include <sdbus-c++/sdbus-c++.h>

#include "absl/synchronization/mutex.h"
#include "internal/platform/implementation/linux/ble_gatt_client.h"
#include "internal/platform/implementation/linux/bluez_gatt_characteristic_client.h"
#include "internal/platform/uuid.h"
#include "internal/weave/connection.h"

namespace nearby {
namespace linux {

// A weave::Connection implemented over a remote GATT server's Nearby socket
// characteristics (the outgoing / client side). Data written by the Weave layer
// is sent to the remote's "write" characteristic; notifications from the
// remote's "indicate" characteristic are handed back up as received packets.
//
// This is the client counterpart of the server-side ble_v2_socket_adapter, and
// mirrors Apple's BleMedium::Connect / the core weave::ClientSocket contract:
//   Weave  ->  Transmit(pkt)            ->  WriteValue(write_char)
//   indicate notification               ->  on_remote_transmit_cb(pkt)  ->  Weave
class BleV2GattConnection : public ::nearby::weave::Connection {
 public:
  BleV2GattConnection(std::shared_ptr<BluezGattDiscovery> gatt_discovery,
                      sdbus::ObjectPath device_object_path, Uuid service_uuid,
                      Uuid write_characteristic_uuid,
                      Uuid indicate_characteristic_uuid)
      : gatt_discovery_(std::move(gatt_discovery)),
        device_object_path_(std::move(device_object_path)),
        service_uuid_(service_uuid),
        write_characteristic_uuid_(write_characteristic_uuid),
        indicate_characteristic_uuid_(indicate_characteristic_uuid) {}

  ~BleV2GattConnection() override { Close(); }

  // weave::Connection.
  void Initialize(::nearby::weave::ConnectionCallback callback) override
      ABSL_LOCKS_EXCLUDED(mutex_);
  int GetMaxPacketSize() const override;
  void Transmit(std::string packet) override ABSL_LOCKS_EXCLUDED(mutex_);
  void Close() override ABSL_LOCKS_EXCLUDED(mutex_);

  // Whether Initialize() successfully opened both characteristics.
  bool IsValid() const ABSL_LOCKS_EXCLUDED(mutex_);

 private:
  // Conservative Weave MTU: the BLE default ATT MTU is 23 (20 usable payload).
  // Weave negotiates the min of both peers' advertised sizes, so a small floor
  // is always safe; a larger negotiated value is used if the link supports it.
  static constexpr int kDefaultMaxPacketSize = 20;

  std::shared_ptr<BluezGattDiscovery> gatt_discovery_;
  const sdbus::ObjectPath device_object_path_;
  const Uuid service_uuid_;
  const Uuid write_characteristic_uuid_;
  const Uuid indicate_characteristic_uuid_;

  mutable absl::Mutex mutex_;
  ::nearby::weave::ConnectionCallback callback_ ABSL_GUARDED_BY(mutex_);
  bool closed_ ABSL_GUARDED_BY(mutex_) = false;
  // Held to keep the write channel and the notification subscription alive.
  std::unique_ptr<bluez::GattCharacteristicClient> write_characteristic_
      ABSL_GUARDED_BY(mutex_);
  std::unique_ptr<bluez::GattCharacteristicClient> indicate_characteristic_
      ABSL_GUARDED_BY(mutex_);
};

}  // namespace linux
}  // namespace nearby

#endif  // PLATFORM_IMPL_LINUX_BLE_V2_GATT_CONNECTION_H_
