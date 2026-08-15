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

#include "internal/platform/implementation/linux/bluetooth_classic_server_socket.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <memory>

#include "internal/platform/exception.h"
#include "internal/platform/implementation/bluetooth_classic.h"
#include "internal/platform/implementation/linux/bluetooth_classic_device.h"
#include "internal/platform/implementation/linux/bluetooth_classic_socket.h"
#include "internal/platform/logging.h"

namespace nearby {
namespace linux {

namespace {

constexpr int kAcceptPollTimeoutMillis = 100;

bool SetNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool SetSecurityLow(int fd) {
  struct bt_security security {};
  security.level = BT_SECURITY_LOW;
  return setsockopt(fd, SOL_BLUETOOTH, BT_SECURITY, &security,
                    sizeof(security)) == 0;
}

}  // namespace

std::shared_ptr<BluetoothServerSocket> BluetoothServerSocket::Create(
    ProfileManager &profile_manager, BluetoothDevices &devices,
    const MacAddress &local_address, const std::string &service_name,
    const std::string &service_uuid) {
  int listener_fd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
  if (listener_fd < 0) {
    LOG(ERROR) << __func__ << ": Failed to create RFCOMM socket: "
               << std::strerror(errno);
    return nullptr;
  }

  // This is the receive-side equivalent of Windows PlainSocket: the kernel
  // must not require authentication or encryption for this RFCOMM listener.
  // Treat failure as fatal; falling back would reintroduce an SSP/bond prompt.
  if (!SetSecurityLow(listener_fd)) {
    LOG(ERROR) << __func__ << ": Failed to set BT_SECURITY_LOW: "
               << std::strerror(errno);
    close(listener_fd);
    return nullptr;
  }

  struct sockaddr_rc local {};
  local.rc_family = AF_BLUETOOTH;
  if (!local_address.IsSet() ||
      str2ba(local_address.ToString().c_str(), &local.rc_bdaddr) < 0) {
    LOG(ERROR) << __func__ << ": Invalid local Bluetooth address "
               << local_address.ToString();
    close(listener_fd);
    return nullptr;
  }
  // rc_channel=0 auto-allocation is unreliable on this kernel/driver (bind
  // succeeds but getsockname reports channel 0 / EAGAIN). Explicitly scan the
  // RFCOMM channel range and take the first that binds, matching how standard
  // Linux RFCOMM servers allocate a channel.
  bool bound = false;
  for (uint8_t channel = 1; channel <= 30; ++channel) {
    local.rc_channel = channel;
    if (bind(listener_fd, reinterpret_cast<struct sockaddr *>(&local),
             sizeof(local)) == 0) {
      bound = true;
      break;
    }
    if (errno != EADDRINUSE && errno != EADDRNOTAVAIL) {
      LOG(ERROR) << __func__ << ": Failed to bind RFCOMM socket: "
                 << std::strerror(errno);
      close(listener_fd);
      return nullptr;
    }
  }
  if (!bound || local.rc_channel == 0) {
    LOG(ERROR) << __func__ << ": No free RFCOMM channel available";
    close(listener_fd);
    return nullptr;
  }

  if (!SetNonBlocking(listener_fd) || listen(listener_fd, SOMAXCONN) < 0) {
    LOG(ERROR) << __func__ << ": Failed to listen on RFCOMM channel "
               << static_cast<unsigned int>(local.rc_channel) << ": "
               << std::strerror(errno);
    close(listener_fd);
    return nullptr;
  }

  if (!profile_manager.RegisterRawRfcommServer(
          service_name, service_uuid, local.rc_channel)) {
    LOG(ERROR) << __func__ << ": Failed to publish RFCOMM SDP record for "
               << service_uuid;
    close(listener_fd);
    return nullptr;
  }

  LOG(INFO) << __func__ << ": Listening with BT_SECURITY_LOW on RFCOMM channel "
            << static_cast<unsigned int>(local.rc_channel) << " for "
            << service_uuid;
  return std::shared_ptr<BluetoothServerSocket>(new BluetoothServerSocket(
      profile_manager, devices, service_uuid, listener_fd));
}

std::unique_ptr<api::BluetoothSocket> BluetoothServerSocket::Accept() {
  if (closed_.load()) {
    LOG(ERROR) << __func__ << ": server socket has been stopped";
    return nullptr;
  }

  LOG(INFO) << __func__
                       << ": accepting new connections for service uuid "
                       << service_uuid_;

  int accept_fd;
  {
    absl::MutexLock lock(&listener_mutex_);
    if (listener_fd_ < 0) return nullptr;
    accept_fd = dup(listener_fd_);
  }
  if (accept_fd < 0) {
    LOG(ERROR) << __func__ << ": Failed to duplicate RFCOMM listener: "
               << std::strerror(errno);
    return nullptr;
  }

  int client_fd = -1;
  struct sockaddr_rc remote {};
  while (!closed_.load()) {
    struct pollfd pfd {};
    pfd.fd = accept_fd;
    pfd.events = POLLIN;
    int result = poll(&pfd, 1, kAcceptPollTimeoutMillis);
    if (result < 0) {
      if (errno == EINTR) continue;
      LOG(ERROR) << __func__ << ": poll failed: " << std::strerror(errno);
      break;
    }
    if (result == 0) continue;
    if ((pfd.revents & POLLIN) == 0) break;

    socklen_t remote_len = sizeof(remote);
    client_fd = accept(accept_fd,
                       reinterpret_cast<struct sockaddr *>(&remote),
                       &remote_len);
    if (client_fd < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if (client_fd < 0) {
      LOG(ERROR) << __func__ << ": accept failed: " << std::strerror(errno);
    }
    break;
  }
  close(accept_fd);

  if (client_fd < 0) return nullptr;
  if (!SetSecurityLow(client_fd)) {
    LOG(ERROR) << __func__ << ": Failed to retain BT_SECURITY_LOW on accepted "
                              "RFCOMM socket: "
               << std::strerror(errno);
    close(client_fd);
    return nullptr;
  }

  char remote_address[18] = {};
  ba2str(&remote.rc_bdaddr, remote_address);
  MacAddress mac_address;
  if (!MacAddress::FromString(remote_address, mac_address)) {
    LOG(ERROR) << __func__ << ": Invalid remote Bluetooth address "
               << remote_address;
    close(client_fd);
    return nullptr;
  }

  auto device = devices_.get_device_by_address(mac_address);
  if (device == nullptr) {
    device = std::make_shared<BluetoothDevice>(mac_address);
  }
  LOG(INFO) << __func__ << ": accepted plain RFCOMM connection from "
            << remote_address << " for " << service_uuid_;
  return std::make_unique<BluetoothSocket>(
      std::move(device), sdbus::UnixFd(client_fd, sdbus::adopt_fd));
}

Exception BluetoothServerSocket::Close() {
  if (closed_.exchange(true)) return {Exception::kSuccess};

  LOG(INFO) << __func__ << ": closing raw RFCOMM server socket";
  {
    absl::MutexLock lock(&listener_mutex_);
    if (listener_fd_ >= 0) {
      close(listener_fd_);
      listener_fd_ = -1;
    }
  }
  profile_manager_.Unregister(service_uuid_);

  return {Exception::kSuccess};
}
}  // namespace linux
}  // namespace nearby
