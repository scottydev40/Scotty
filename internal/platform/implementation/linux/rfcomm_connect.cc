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

#include "internal/platform/implementation/linux/rfcomm_connect.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <optional>

#include "internal/platform/cancellation_flag.h"
#include "internal/platform/logging.h"

namespace nearby {
namespace linux {

namespace {

constexpr int kConnectPollTimeoutMillis = 250;
constexpr int kConnectTotalTimeoutMillis = 10000;

// Parses a dashed 128-bit UUID string, e.g.
// "0000fef3-0000-1000-8000-00805f9b34fb", into 16 big-endian bytes suitable
// for sdp_uuid128_create. Returns false if the string is malformed.
bool ParseUuid128(const std::string &uuid_str, uint8_t out[16]) {
  std::string hex;
  hex.reserve(32);
  for (char c : uuid_str) {
    if (c == '-') continue;
    hex.push_back(c);
  }
  if (hex.size() != 32) {
    return false;
  }
  for (int i = 0; i < 16; ++i) {
    std::string byte_str = hex.substr(i * 2, 2);
    char *end = nullptr;
    long value = std::strtol(byte_str.c_str(), &end, 16);
    if (end == byte_str.c_str() || *end != '\0') {
      return false;
    }
    out[i] = static_cast<uint8_t>(value);
  }
  return true;
}

bool SetNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool SetBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == 0;
}

// Queries the peer's SDP server for the RFCOMM channel serving
// `service_uuid`. Returns the channel number (1-30) or std::nullopt on
// failure.
std::optional<int> FindRfcommChannel(const bdaddr_t &target,
                                      const std::string &service_uuid) {
  uint8_t uuid_bytes[16];
  if (!ParseUuid128(service_uuid, uuid_bytes)) {
    LOG(WARNING) << __func__ << ": Malformed service UUID: " << service_uuid;
    return std::nullopt;
  }

  // BDADDR_ANY expands to the address of a C99 compound literal, which GCC
  // in C++20 mode refuses to take the address of directly (-fpermissive
  // error). Use an explicit local zeroed bdaddr_t instead.
  bdaddr_t any_addr {};
  sdp_session_t *session =
      sdp_connect(&any_addr, &target, SDP_RETRY_IF_BUSY);
  if (session == nullptr) {
    LOG(WARNING) << __func__
                 << ": sdp_connect failed: " << std::strerror(errno);
    return std::nullopt;
  }

  uuid_t svc_uuid;
  sdp_uuid128_create(&svc_uuid, uuid_bytes);
  sdp_list_t *search_list = sdp_list_append(nullptr, &svc_uuid);

  uint16_t attr = SDP_ATTR_PROTO_DESC_LIST;
  sdp_list_t *attrid_list = sdp_list_append(nullptr, &attr);

  sdp_list_t *response_list = nullptr;
  int err = sdp_service_search_attr_req(session, search_list,
                                         SDP_ATTR_REQ_INDIVIDUAL, attrid_list,
                                         &response_list);

  sdp_list_free(search_list, nullptr);
  sdp_list_free(attrid_list, nullptr);

  if (err != 0) {
    LOG(WARNING) << __func__ << ": sdp_service_search_attr_req failed: "
                 << std::strerror(errno);
    sdp_close(session);
    return std::nullopt;
  }

  std::optional<int> channel;
  for (sdp_list_t *r = response_list; r != nullptr; r = r->next) {
    sdp_record_t *rec = static_cast<sdp_record_t *>(r->data);
    if (rec == nullptr) continue;

    sdp_list_t *protos = nullptr;
    if (sdp_get_access_protos(rec, &protos) == 0 && protos != nullptr) {
      int port = sdp_get_proto_port(protos, RFCOMM_UUID);
      if (port > 0) {
        channel = port;
      }
      sdp_list_foreach(protos, reinterpret_cast<sdp_list_func_t>(sdp_list_free),
                        nullptr);
      sdp_list_free(protos, nullptr);
    }
    sdp_record_free(rec);

    if (channel.has_value()) break;
  }
  sdp_list_free(response_list, nullptr);
  sdp_close(session);

  if (!channel.has_value()) {
    LOG(WARNING) << __func__
                 << ": No RFCOMM channel found for service " << service_uuid;
    return std::nullopt;
  }

  LOG(INFO) << __func__ << ": Discovered RFCOMM channel " << *channel
            << " for service " << service_uuid;
  return channel;
}

}  // namespace

std::optional<int> ConnectInsecureRfcommByAddress(
    const std::string &remote_mac, const std::string &service_uuid,
    nearby::CancellationFlag *cancellation_flag) {
  bdaddr_t target;
  if (str2ba(remote_mac.c_str(), &target) < 0) {
    LOG(ERROR) << __func__ << ": Invalid Bluetooth address: " << remote_mac;
    return std::nullopt;
  }

  if (cancellation_flag != nullptr && cancellation_flag->Cancelled()) {
    LOG(INFO) << __func__ << ": Cancelled before SDP lookup for "
              << remote_mac;
    return std::nullopt;
  }

  std::optional<int> channel = FindRfcommChannel(target, service_uuid);
  if (!channel.has_value()) {
    return std::nullopt;
  }

  if (cancellation_flag != nullptr && cancellation_flag->Cancelled()) {
    LOG(INFO) << __func__ << ": Cancelled after SDP lookup for "
              << remote_mac;
    return std::nullopt;
  }

  int fd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
  if (fd < 0) {
    LOG(ERROR) << __func__
               << ": Failed to create RFCOMM socket: " << std::strerror(errno);
    return std::nullopt;
  }

  struct sockaddr_rc addr {};
  addr.rc_family = AF_BLUETOOTH;
  addr.rc_bdaddr = target;
  addr.rc_channel = static_cast<uint8_t>(*channel);

  if (!SetNonBlocking(fd)) {
    LOG(ERROR) << __func__ << ": Failed to set RFCOMM socket non-blocking: "
               << std::strerror(errno);
    close(fd);
    return std::nullopt;
  }

  LOG(INFO) << __func__ << ": Connecting to " << remote_mac
            << " on RFCOMM channel " << *channel;

  int ret = connect(fd, reinterpret_cast<struct sockaddr *>(&addr),
                     sizeof(addr));
  if (ret < 0 && errno != EINPROGRESS) {
    LOG(ERROR) << __func__ << ": Failed to connect RFCOMM socket to "
               << remote_mac << ": " << std::strerror(errno);
    close(fd);
    return std::nullopt;
  }

  if (ret < 0) {
    // Connection in progress; poll with a bounded timeout, checking for
    // cancellation between polls.
    int waited_ms = 0;
    bool connected = false;
    bool failed = false;
    while (waited_ms < kConnectTotalTimeoutMillis) {
      if (cancellation_flag != nullptr && cancellation_flag->Cancelled()) {
        LOG(INFO) << __func__ << ": Cancelled while connecting to "
                  << remote_mac;
        close(fd);
        return std::nullopt;
      }

      struct pollfd pfd {};
      pfd.fd = fd;
      pfd.events = POLLOUT;
      int poll_ret = poll(&pfd, 1, kConnectPollTimeoutMillis);
      waited_ms += kConnectPollTimeoutMillis;

      if (poll_ret < 0) {
        if (errno == EINTR) continue;
        LOG(ERROR) << __func__ << ": poll() failed while connecting to "
                   << remote_mac << ": " << std::strerror(errno);
        failed = true;
        break;
      }
      if (poll_ret == 0) {
        // Timed out this round; keep waiting until total timeout elapses.
        continue;
      }
      if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        failed = true;
        break;
      }
      if (pfd.revents & POLLOUT) {
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 ||
            so_error != 0) {
          LOG(ERROR) << __func__ << ": RFCOMM connect to " << remote_mac
                     << " failed: " << std::strerror(so_error);
          failed = true;
        } else {
          connected = true;
        }
        break;
      }
    }

    if (!connected || failed) {
      if (!failed) {
        LOG(ERROR) << __func__ << ": Timed out connecting to " << remote_mac;
      }
      close(fd);
      return std::nullopt;
    }
  }

  if (!SetBlocking(fd)) {
    LOG(ERROR) << __func__ << ": Failed to restore blocking mode on RFCOMM "
               << "socket to " << remote_mac << ": " << std::strerror(errno);
    close(fd);
    return std::nullopt;
  }

  LOG(INFO) << __func__ << ": Successfully connected RFCOMM socket to "
            << remote_mac << " on channel " << *channel;
  return fd;
}

}  // namespace linux
}  // namespace nearby
