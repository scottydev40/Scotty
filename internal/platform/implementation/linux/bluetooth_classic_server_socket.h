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

#ifndef PLATFORM_IMPL_LINUX_BLUETOOTH_SERVER_SOCKET_H_
#define PLATFORM_IMPL_LINUX_BLUETOOTH_SERVER_SOCKET_H_

#include <atomic>
#include <memory>
#include <string>

#include "absl/synchronization/mutex.h"
#include "internal/platform/exception.h"
#include "internal/platform/implementation/bluetooth_classic.h"
#include "internal/platform/implementation/linux/bluetooth_bluez_profile.h"
#include "internal/platform/implementation/linux/bluetooth_devices.h"
#include "internal/platform/mac_address.h"

namespace nearby {
namespace linux {
class BluetoothServerSocket final : public api::BluetoothServerSocket {
 public:
  static std::shared_ptr<BluetoothServerSocket> Create(
      ProfileManager &profile_manager, BluetoothDevices &devices,
      const MacAddress &local_address, const std::string &service_name,
      const std::string &service_uuid);

  ~BluetoothServerSocket() override { Close(); }

  // https://developer.android.com/reference/android/bluetooth/BluetoothServerSocket.html#accept()
  //
  // Blocks until either:
  // - at least one incoming connection request is available, or
  // - ServerSocket is closed.
  // On success, returns connected socket, ready to exchange data.
  // Returns nullptr on error.
  // Once error is reported, it is permanent, and ServerSocket has to be
  // closed.
  std::unique_ptr<api::BluetoothSocket> Accept() override;

  // https://developer.android.com/reference/android/bluetooth/BluetoothServerSocket.html#close()
  //
  // Returns Exception::kIo on error, Exception::kSuccess otherwise.
  Exception Close() override;

 private:
  BluetoothServerSocket(ProfileManager &profile_manager,
                        BluetoothDevices &devices, std::string service_uuid,
                        int listener_fd)
      : profile_manager_(profile_manager),
        devices_(devices),
        service_uuid_(std::move(service_uuid)),
        listener_fd_(listener_fd) {}

  std::atomic_bool closed_{false};
  ProfileManager &profile_manager_;
  BluetoothDevices &devices_;
  std::string service_uuid_;
  absl::Mutex listener_mutex_;
  int listener_fd_ ABSL_GUARDED_BY(listener_mutex_);
};
}  // namespace linux
}  // namespace nearby

#endif
