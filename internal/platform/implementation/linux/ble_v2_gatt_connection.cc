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

#include "internal/platform/implementation/linux/ble_v2_gatt_connection.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <sdbus-c++/sdbus-c++.h>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "internal/platform/logging.h"

namespace nearby {
namespace linux {

void BleV2GattConnection::Initialize(
    ::nearby::weave::ConnectionCallback callback) {
  absl::MutexLock lock(&mutex_);
  callback_ = std::move(callback);

  // Open the write channel (Weave -> remote).
  write_characteristic_ = gatt_discovery_->GetCharacteristic(
      device_object_path_, service_uuid_, write_characteristic_uuid_);
  if (write_characteristic_ == nullptr) {
    LOG(ERROR) << __func__ << ": write characteristic "
               << std::string(write_characteristic_uuid_)
               << " not found on " << device_object_path_;
  }

  // Subscribe to the indicate channel (remote -> Weave). Notifications are
  // forwarded up as received Weave packets.
  indicate_characteristic_ = gatt_discovery_->GetSubscribedCharacteristic(
      device_object_path_, service_uuid_, indicate_characteristic_uuid_,
      [this](absl::string_view value) {
        absl::MutexLock lock(&mutex_);
        if (closed_ || !callback_.on_remote_transmit_cb) {
          return;
        }
        callback_.on_remote_transmit_cb(std::string(value));
      });
  if (indicate_characteristic_ == nullptr) {
    LOG(ERROR) << __func__ << ": indicate characteristic "
               << std::string(indicate_characteristic_uuid_)
               << " not found on " << device_object_path_;
  }

  LOG(INFO) << __func__ << ": GATT Weave connection initialized on "
            << device_object_path_ << " (write="
            << (write_characteristic_ != nullptr) << ", indicate="
            << (indicate_characteristic_ != nullptr) << ")";
}

int BleV2GattConnection::GetMaxPacketSize() const {
  return kDefaultMaxPacketSize;
}

void BleV2GattConnection::Transmit(std::string packet) {
  absl::MutexLock lock(&mutex_);
  if (closed_ || write_characteristic_ == nullptr) {
    if (callback_.on_transmit_cb) {
      callback_.on_transmit_cb(
          absl::FailedPreconditionError("BLE GATT connection not writable"));
    }
    return;
  }

  std::vector<uint8_t> bytes(packet.begin(), packet.end());
  absl::Status status = absl::OkStatus();
  try {
    // The remote's write characteristic advertises only the "write" flag
    // (write-with-response), so request an acknowledged write ("request").
    write_characteristic_->WriteValue(
        bytes, {{sdbus::PropertyName("type"), sdbus::Variant("request")}});
  } catch (const sdbus::Error &e) {
    LOG(ERROR) << __func__ << ": WriteValue failed: " << e.getName() << ": "
               << e.getMessage();
    status = absl::InternalError(e.getMessage());
  }

  if (callback_.on_transmit_cb) {
    callback_.on_transmit_cb(status);
  }
}

void BleV2GattConnection::Close() {
  absl::MutexLock lock(&mutex_);
  if (closed_) {
    return;
  }
  closed_ = true;
  // Destroying the characteristic proxies unsubscribes and releases the
  // channels; the underlying ACL is torn down by the medium/GattClient.
  indicate_characteristic_.reset();
  write_characteristic_.reset();
  LOG(INFO) << __func__ << ": GATT Weave connection closed on "
            << device_object_path_;
}

bool BleV2GattConnection::IsValid() const {
  absl::MutexLock lock(&mutex_);
  return write_characteristic_ != nullptr && indicate_characteristic_ != nullptr;
}

}  // namespace linux
}  // namespace nearby
