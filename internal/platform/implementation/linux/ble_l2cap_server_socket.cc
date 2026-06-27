// Copyright 2024 Google LLC
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

#include "internal/platform/implementation/linux/ble_l2cap_server_socket.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <utility>

#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>

#include "internal/platform/logging.h"

#include "internal/platform/prng.h"

namespace nearby {
namespace linux {
namespace {

constexpr int kAcceptPollTimeoutMillis = 250;

bool SetNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

}  // namespace

BleL2capServerSocket::BleL2capServerSocket() = default;

BleL2capServerSocket::BleL2capServerSocket(int psm, std::string service_id)
    : psm_(psm), service_id_(std::move(service_id)) {
  Open();
}

BleL2capServerSocket::~BleL2capServerSocket() {
  Close();
}

void BleL2capServerSocket::SetPSM(int psm) {
  absl::MutexLock lock(&mutex_);
  psm_ = psm;
}

int BleL2capServerSocket::GetPSM() const {
  absl::MutexLock lock(&mutex_);
  return psm_;
}

bool BleL2capServerSocket::IsValid() const {
  absl::MutexLock lock(&mutex_);
  return !closed_ && server_fd_ != -1;
}

bool BleL2capServerSocket::IsClosed() const {
  absl::MutexLock lock(&mutex_);
  return closed_;
}

bool BleL2capServerSocket::Open() {
  int server_fd = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
  if (server_fd < 0) {
    LOG(ERROR) << "Failed to create L2CAP server socket: "
               << std::strerror(errno);
    return false;
  }

  if (!SetNonBlocking(server_fd)) {
    LOG(WARNING) << "Failed to set L2CAP server socket non-blocking: "
                 << std::strerror(errno);
  }

  int psm = 0;
  {
    absl::MutexLock lock(&mutex_);
    psm = psm_;
  }
  if (psm < 0x80 || psm > 0xff) {
    Prng prng;
    psm = 0x80 + (prng.NextUint32() % 0x80);
  }

  sockaddr_l2 addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.l2_family = AF_BLUETOOTH;
  addr.l2_psm = htobs(psm);
  addr.l2_bdaddr_type = BDADDR_LE_PUBLIC;
  std::memset(&addr.l2_bdaddr, 0, sizeof(addr.l2_bdaddr));

  if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    LOG(ERROR) << "Failed to bind L2CAP server socket: "
               << std::strerror(errno) << " (errno: " << errno << ")";
    close(server_fd);
    return false;
  }

  constexpr uint16_t kReceiveMtu = 672;
  if (setsockopt(server_fd, SOL_BLUETOOTH, BT_RCVMTU, &kReceiveMtu,
                 sizeof(kReceiveMtu)) < 0) {
    LOG(WARNING) << "Failed to set receive MTU on L2CAP server socket: "
                 << std::strerror(errno);
  }

  if (listen(server_fd, 5) < 0) {
    LOG(ERROR) << "Failed to listen on L2CAP server socket: "
               << std::strerror(errno);
    close(server_fd);
    return false;
  }

  socklen_t addr_len = sizeof(addr);
  if (getsockname(server_fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) ==
      0) {
    psm = btohs(addr.l2_psm);
    LOG(INFO) << "L2CAP server socket listening on PSM: " << psm;
  } else {
    LOG(WARNING) << "Failed to get socket name: " << std::strerror(errno);
  }

  {
    absl::MutexLock lock(&mutex_);
    if (closed_) {
      close(server_fd);
      return false;
    }
    server_fd_ = server_fd;
    psm_ = psm;
  }
  return true;
}

std::unique_ptr<api::ble::BleL2capSocket> BleL2capServerSocket::Accept() {
  sockaddr_l2 client_addr;
  std::memset(&client_addr, 0, sizeof(client_addr));
  socklen_t client_len = sizeof(client_addr);

  int server_fd = -1;
  int psm = 0;
  {
    absl::MutexLock lock(&mutex_);
    if (closed_ || server_fd_ == -1) {
      LOG(INFO) << "L2CAP server socket closed, stopping accept";
      return nullptr;
    }
    server_fd = server_fd_;
    psm = psm_;
  }

  LOG(INFO) << "Waiting for L2CAP connection on PSM " << psm << "...";

  while (!IsClosed()) {
    pollfd pfd;
    pfd.fd = server_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int poll_result = poll(&pfd, 1, kAcceptPollTimeoutMillis);
    if (poll_result < 0) {
      if (errno == EINTR) {
        continue;
      }
      LOG(ERROR) << "Failed polling L2CAP server socket: "
                 << std::strerror(errno);
      return nullptr;
    }
    if (poll_result == 0) {
      continue;
    }
    if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
      if (IsClosed()) {
        LOG(INFO) << "L2CAP server socket closed, stopping accept";
      } else {
        LOG(ERROR) << "L2CAP server socket poll error: " << pfd.revents;
      }
      return nullptr;
    }

    int client_fd =
        accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr),
               &client_len);
    if (client_fd < 0) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      if (errno == EBADF || errno == EINVAL) {
        LOG(INFO) << "L2CAP server socket closed, stopping accept";
      } else {
        LOG(ERROR) << "Failed to accept L2CAP connection: "
                   << std::strerror(errno);
      }
      return nullptr;
    }

    char client_addr_str[18];
    ba2str(&client_addr.l2_bdaddr, client_addr_str);
    LOG(INFO) << "Accepted L2CAP connection from " << client_addr_str
              << " on PSM " << btohs(client_addr.l2_psm);

    api::ble::BlePeripheral::UniqueId peripheral_id = 0;
    for (int i = 0; i < 6; ++i) {
      peripheral_id = (peripheral_id << 8) |
                      static_cast<uint8_t>(client_addr.l2_bdaddr.b[i]);
    }

    std::string service_id;
    {
      absl::MutexLock lock(&mutex_);
      service_id = service_id_;
    }
    return std::make_unique<BleL2capSocket>(client_fd, peripheral_id,
                                            service_id);
  }

  LOG(INFO) << "L2CAP server socket closed, stopping accept";
  return nullptr;
}

Exception BleL2capServerSocket::Close() {
  int server_fd = -1;
  {
    absl::MutexLock lock(&mutex_);
    if (closed_) {
      return {Exception::kSuccess};
    }
    closed_ = true;
    server_fd = std::exchange(server_fd_, -1);
  }
  if (server_fd != -1) {
    shutdown(server_fd, SHUT_RDWR);
    close(server_fd);
  }
  return {Exception::kSuccess};
}

}  // namespace linux
}  // namespace nearby
