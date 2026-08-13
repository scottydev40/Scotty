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
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace nearby {
namespace linux {

namespace {

constexpr int kConnectPollTimeoutMillis = 100;
constexpr int kConnectTotalTimeoutMillis = 10000;

// sdp_connect can transiently fail with "Host is down" when the peer hasn't
// been BR/EDR-paged yet (it advertises over BLE, so paging is cold). Retry a
// few times with a short backoff so a cold peer doesn't fail the whole connect
// and force a slow (~20s) Nearby-layer retry.
constexpr int kSdpConnectAttempts = 3;
constexpr int kSdpConnectRetryBackoffMillis = 400;

// Cache of the last RFCOMM channel that worked per peer MAC. The Nearby stack
// calls ConnectToService repeatedly for the same peer (medium retries) and each
// SDP round-trip costs ~1s, so caching lets retries skip SDP. Self-healing: a
// failed connect on a cached channel invalidates it and re-runs SDP.
absl::Mutex g_channel_cache_mutex(absl::kConstInit);
absl::flat_hash_map<std::string, int> *g_channel_cache
    ABSL_GUARDED_BY(g_channel_cache_mutex) = nullptr;

std::optional<int> GetCachedChannel(const std::string &mac) {
  absl::MutexLock l(&g_channel_cache_mutex);
  if (g_channel_cache == nullptr) return std::nullopt;
  auto it = g_channel_cache->find(mac);
  if (it == g_channel_cache->end()) return std::nullopt;
  return it->second;
}
void PutCachedChannel(const std::string &mac, int channel) {
  absl::MutexLock l(&g_channel_cache_mutex);
  if (g_channel_cache == nullptr) {
    g_channel_cache = new absl::flat_hash_map<std::string, int>();
  }
  (*g_channel_cache)[mac] = channel;
}
void InvalidateCachedChannel(const std::string &mac) {
  absl::MutexLock l(&g_channel_cache_mutex);
  if (g_channel_cache != nullptr) g_channel_cache->erase(mac);
}

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
  sdp_session_t *session = nullptr;
  for (int attempt = 0; attempt < kSdpConnectAttempts; ++attempt) {
    bdaddr_t any_addr {};
    session = sdp_connect(&any_addr, &target, SDP_RETRY_IF_BUSY);
    if (session != nullptr) break;
    LOG(WARNING) << __func__ << ": sdp_connect attempt " << (attempt + 1) << "/"
                 << kSdpConnectAttempts << " failed: " << std::strerror(errno);
    if (attempt + 1 < kSdpConnectAttempts) {
      usleep(kSdpConnectRetryBackoffMillis * 1000);
    }
  }
  if (session == nullptr) {
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

// Opens a non-blocking RFCOMM socket to `target` on `channel`, honoring
// cancellation, restoring blocking mode on success. Returns the connected fd or
// std::nullopt.
std::optional<int> TryConnectRfcommChannel(
    const bdaddr_t &target, int channel, const std::string &remote_mac,
    uint8_t security_level, nearby::CancellationFlag *cancellation_flag) {
  int fd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
  if (fd < 0) {
    LOG(ERROR) << __func__
               << ": Failed to create RFCOMM socket: " << std::strerror(errno);
    return std::nullopt;
  }

  // Security level for the link. BT_SECURITY_LOW = no authentication, no
  // encryption, no bonding — Android's createInsecureRfcommSocketToServiceRecord
  // that Quick Share uses, and what avoids the "pair with laptop" prompt (the
  // peer only ever LE-bonded during discovery, so demanding a BR/EDR key would
  // prompt). A Pixel accepts this and transfers. The level is a parameter because
  // Samsung was investigated as a possible encryption requirement — it is NOT:
  // the Samsung RFCOMM server hangs up (POLLHUP) at every level, so the caller
  // stays on LOW. Best-effort: if the option is unavailable we still try.
  struct bt_security sec {};
  sec.level = security_level;
  if (setsockopt(fd, SOL_BLUETOOTH, BT_SECURITY, &sec, sizeof(sec)) < 0) {
    LOG(WARNING) << __func__ << ": Could not set BT_SECURITY level "
                 << static_cast<int>(security_level) << " on the RFCOMM socket to "
                 << remote_mac << " (" << std::strerror(errno) << ")";
  }

  struct sockaddr_rc addr {};
  addr.rc_family = AF_BLUETOOTH;
  addr.rc_bdaddr = target;
  addr.rc_channel = static_cast<uint8_t>(channel);

  if (!SetNonBlocking(fd)) {
    LOG(ERROR) << __func__ << ": Failed to set RFCOMM socket non-blocking: "
               << std::strerror(errno);
    close(fd);
    return std::nullopt;
  }

  LOG(INFO) << __func__ << ": Connecting to " << remote_mac
            << " on RFCOMM channel " << channel;

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
        LOG(WARNING) << __func__ << ": RFCOMM connect to " << remote_mac
                     << " on channel " << channel
                     << " hung up before connecting (revents=" << pfd.revents
                     << ") — peer likely rejected this security level";
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

  return fd;
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

  // Try up to twice: first with a cached channel (skips the ~1s SDP round-trip
  // on Nearby's medium-retries), then, if that stale channel fails to connect,
  // once more after a fresh SDP lookup.
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (cancellation_flag != nullptr && cancellation_flag->Cancelled()) {
      LOG(INFO) << __func__ << ": Cancelled for " << remote_mac;
      return std::nullopt;
    }

    std::optional<int> channel =
        (attempt == 0) ? GetCachedChannel(remote_mac) : std::nullopt;
    const bool from_cache = channel.has_value();
    if (from_cache) {
      LOG(INFO) << __func__ << ": Using cached RFCOMM channel " << *channel
                << " for " << remote_mac;
    } else {
      channel = FindRfcommChannel(target, service_uuid);
      if (!channel.has_value()) return std::nullopt;
    }

    if (cancellation_flag != nullptr && cancellation_flag->Cancelled()) {
      LOG(INFO) << __func__ << ": Cancelled after channel lookup for "
                << remote_mac;
      return std::nullopt;
    }

    // Insecure link: a Pixel accepts it and it avoids any pairing prompt. NOTE:
    // this does NOT work for Samsung Quick Share — the Samsung RFCOMM server
    // accepts the channel then immediately sends DISC (poll revents POLLHUP,
    // ~120ms), and it does so regardless of security level (BT_SECURITY_MEDIUM
    // was tried and hung up identically, too fast for any encryption negotiation
    // to matter). The Samsung appears to require the RFCOMM peer to already be
    // known from the BLE-side Nearby handshake; a bare RFCOMM-to-MAC is dropped.
    // Left insecure-only until that handshake gap is understood.
    std::optional<int> fd = TryConnectRfcommChannel(
        target, *channel, remote_mac, BT_SECURITY_LOW, cancellation_flag);
    if (fd.has_value()) {
      PutCachedChannel(remote_mac, *channel);
      LOG(INFO) << __func__ << ": Successfully connected RFCOMM socket to "
                << remote_mac << " on channel " << *channel;
      return fd;
    }

    // Connect failed. A stale cached channel is the likely cause: drop it and
    // retry once with a fresh SDP lookup. A fresh-SDP failure is terminal.
    if (from_cache) {
      InvalidateCachedChannel(remote_mac);
      continue;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace linux
}  // namespace nearby
