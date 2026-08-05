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

#ifndef PLATFORM_IMPL_LINUX_TCP_SERVER_SOCKET_H_
#define PLATFORM_IMPL_LINUX_TCP_SERVER_SOCKET_H_

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <functional>

#include <sdbus-c++/Types.h>
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "internal/platform/exception.h"
#include "internal/platform/implementation/linux/stream.h"
#include "internal/platform/logging.h"

namespace nearby {
namespace linux {
class TCPSocket {
 public:
  explicit TCPSocket(const sdbus::UnixFd& fd)
      : closed_(false), output_stream_(fd), input_stream_(fd) {}

  static std::optional<TCPSocket> Connect(const std::string& ip_address,
                                          int port) {
    struct sockaddr_in addr;
    addr.sin_addr.s_addr = inet_addr(ip_address.c_str());
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    LOG(INFO) << __func__ << ": Connecting to " << ip_address << ":"
                         << port;

    // A peer that is on wifi power-save often fails to answer ARP for the
    // first attempt, and the kernel reports that as EHOSTUNREACH after a
    // couple of seconds. Retry those a few times rather than failing the
    // whole transfer on one missed frame. A refused connection gets the same
    // treatment: the peer advertised over mDNS, so its listener is usually
    // just not up yet.
    constexpr int kConnectAttempts = 3;
    constexpr absl::Duration kRetryDelay = absl::Milliseconds(500);

    for (int attempt = 1;; attempt++) {
      int sock = socket(AF_INET, SOCK_STREAM, 0);
      if (sock < 0) {
        LOG(ERROR) << __func__
                           << ": Error opening socket: " << std::strerror(errno);
        return std::nullopt;
      }

      auto ret = connect(sock, reinterpret_cast<struct sockaddr*>(&addr),
                         sizeof(addr));
      if (ret == 0) return TCPSocket(sdbus::UnixFd(sock));

      auto connect_errno = errno;
      // The socket is unusable once connect() has failed on it, retry or not.
      close(sock);

      bool transient = connect_errno == EHOSTUNREACH ||
                       connect_errno == ENETUNREACH ||
                       connect_errno == ETIMEDOUT ||
                       connect_errno == ECONNREFUSED;
      if (!transient || attempt >= kConnectAttempts) {
        LOG(ERROR) << __func__ << ": Error connecting to socket: "
                           << std::strerror(connect_errno);
        return std::nullopt;
      }

      LOG(WARNING) << __func__ << ": Error connecting to socket: "
                           << std::strerror(connect_errno) << ", retrying ("
                           << attempt << "/" << kConnectAttempts << ")";
      absl::SleepFor(kRetryDelay);
    }
  }

  InputStream& GetInputStream() { return input_stream_; }
  OutputStream& GetOutputStream() { return output_stream_; }

  Exception Close() {
    if (closed_) return {Exception::kFailed};

    closed_ = true;
    input_stream_.Close();
    output_stream_.Close();

    return {Exception::kSuccess};
  };

 private:
  bool closed_;

  OutputStream output_stream_;
  InputStream input_stream_;
};

class TCPServerSocket {
 public:
  explicit TCPServerSocket(int fd) : fd_(fd) {}

  static std::optional<TCPServerSocket> Listen(
      std::optional<const std::reference_wrapper<std::string>> ip_address,
      int port) {
    auto sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
      LOG(ERROR) << __func__
                         << ": Error opening socket: " << std::strerror(errno);
      return std::nullopt;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (ip_address.has_value())
      addr.sin_addr.s_addr = inet_addr(ip_address->get().c_str());
    else
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    auto ret =
        bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (ret < 0) {
      LOG(ERROR) << __func__ << ": Error binding to socket: "
                         << std::strerror(errno);
      return std::nullopt;
    }

    ret = listen(sock, 0);
    if (ret < 0) {
      LOG(ERROR) << __func__ << ": Error listening on socket: "
                         << std::strerror(errno);
      return std::nullopt;
    }

    return TCPServerSocket(sock);
  }
  std::optional<TCPSocket> Accept() {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    auto conn =
        accept(fd_.get(), reinterpret_cast<struct sockaddr*>(&addr), &len);
    if (conn < 0) {
      LOG(ERROR) << __func__
                         << ": Error accepting incoming connections on socket "
                         << fd_.get() << ": " << std::strerror(errno);
      return std::nullopt;
    }

    return TCPSocket(sdbus::UnixFd(conn));
  };

  Exception Close() {
    int fd = fd_.release();
    shutdown(fd, SHUT_RDWR);
    auto ret = close(fd);
    if (ret < 0) {
      LOG(ERROR) << __func__ << ": Error closing socket " << fd << ": "
                         << std::strerror(errno);
      return {Exception::kFailed};
    }

    return {Exception::kSuccess};
  };

  int GetPort() const {
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    auto ret =
        getsockname(fd_.get(), reinterpret_cast<struct sockaddr*>(&sin), &len);
    if (ret < 0) {
      LOG(ERROR) << __func__
                         << ": Error getting information for socket "
                         << fd_.get() << ": " << std::strerror(errno);
      return 0;
    }

    return ntohs(sin.sin_port);
  }

 private:
  sdbus::UnixFd fd_;
};
}  // namespace linux
}  // namespace nearby

#endif
