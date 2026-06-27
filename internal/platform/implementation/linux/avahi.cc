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

#include "internal/platform/implementation/linux/avahi.h"
#include "internal/platform/implementation/linux/dbus.h"
#include "internal/platform/logging.h"
#include "internal/platform/nsd_service_info.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>

namespace nearby {
namespace linux {
namespace avahi {

namespace {
bool IsLocalAddress(const std::string& addr) {
  struct ifaddrs* ifaddr;
  if (getifaddrs(&ifaddr) != 0) return false;
  bool found = false;
  for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr) continue;
    char buf[INET6_ADDRSTRLEN] = {};
    if (ifa->ifa_addr->sa_family == AF_INET) {
      inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(ifa->ifa_addr)->sin_addr, buf, sizeof(buf));
    } else if (ifa->ifa_addr->sa_family == AF_INET6) {
      inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(ifa->ifa_addr)->sin6_addr, buf, sizeof(buf));
    } else {
      continue;
    }
    if (addr == buf) { found = true; break; }
  }
  freeifaddrs(ifaddr);
  return found;
}
}  // namespace
void Server::onResolveServiceReply(const int32_t& interface,
    const int32_t& protocol, const std::string& name,
    const std::string& type, const std::string& domain,
    const std::string& host, const int32_t& aprotocol,
    const std::string& address, const uint16_t& port,
    const std::vector<std::vector<uint8_t>>& txt, const uint32_t& flags,
    std::optional<sdbus::Error> error) {
  if (error.has_value()) {
    LOG(ERROR) << __func__ << ": ResolveService failed with error '"
               << error->getName() << "' message '" << error->getMessage()
               << "'";
    return;
  }

  LOG(INFO) << "Resolved reply received: name=" << name << " address=" << address << " port=" << port;
  if (IsLocalAddress(address)) {
    LOG(INFO) << "Resolved reply: skipping own IP " << address;
    return;
  }
  NsdServiceInfo info;

  info.SetServiceName(name);
  info.SetIPAddress(address);
  info.SetPort(port);
  info.SetServiceType(type + "."); // discovery callback expects an extra period at t
  for (auto &attr : txt) {
    auto attr_str = std::string(attr.begin(), attr.end());
    size_t pos = attr_str.find('=');
    if (pos == 0 || pos == std::string::npos || pos == attr_str.size() - 1) {
      LOG(WARNING) << " found invalid text attribute: " << attr_str;
      continue;
    }

    info.SetTxtRecord(attr_str.substr(0, pos), attr_str.substr(pos + 1));
  }
  discovery_cb_.service_discovered_cb(std::move(info));
};
void ServiceBrowser::onItemNew(const int32_t &interface,
                               const int32_t &protocol, const std::string &name,
                               const std::string &type,
                               const std::string &domain,
                               const uint32_t &flags) {
  LOG(INFO) << __func__ << ": " << getProxy().getObjectPath()
                       << ": Found new item through the ServiceBrowser: "
                       << "interface: " << interface << ", protocol: "
                       << protocol << ", name: '" << name << "', type: '"
                       << type << "', domain: '" << domain
                       << "', flags: " << flags;
  if (flags & kAvahiLookupResultLocal) {
    LOG(INFO) << __func__ << ": Ignoring local service.";
    return;
  }

  try {
    server_->ResolveService(interface, protocol, name, type, domain,
                            0,  // AVAHI_PROTO_INET
                            0);
  } catch (const sdbus::Error &e) {
    DBUS_LOG_METHOD_CALL_ERROR(server_, "ResolveService", e);
  }

}

void ServiceBrowser::onItemRemove(
    const int32_t &interface, const int32_t &protocol, const std::string &name,
    const std::string &type, const std::string &domain, const uint32_t &flags) {
  // // TODO: Can we even resolve removed items?
  // LOG(INFO) << __func__ << ": " << getObjectPath()
  //                      << ": Item removed through the ServiceBrowser: "
  //                      << "interface: " << interface << ", protocol: "
  //                      << protocol << ", name: '" << name << "', type: '"
  //                      << type << "', domain: '" << domain
  //                      << "', flags: " << flags;
  // if (flags & kAvahiLookupResultLocal) {
  //   LOG(INFO) << __func__ << ": Ignoring local service.";
  //   return;
  // }
  //
  // NsdServiceInfo info;
  // try {
  //   auto [r_iface, r_protocol, r_name, r_type, r_domain, r_host, r_aprotocol,
  //         r_address, r_port, r_txt, r_flags] =
  //       server_->ResolveService(interface, protocol, name, type, domain,
  //                               0,  // AVAHI_PROTO_INET
  //                               flags);
  //   info.SetServiceName(r_name);
  //   info.SetIPAddress(r_address);
  //   info.SetPort(r_port);
  //   info.SetServiceType(r_type);
  //   for (auto &attr : r_txt) {
  //     auto attr_str = std::string(attr.begin(), attr.end());
  //     size_t pos = attr_str.find('=');
  //     if (pos == 0 || pos == std::string::npos || pos == attr_str.size() - 1) {
  //       LOG(WARNING) << " found invalid text attribute: " << attr_str;
	 //      continue;
  //     }
  //
  //     info.SetTxtRecord(attr_str.substr(0, pos), attr_str.substr(pos + 1));
  //   }
  // } catch (const sdbus::Error &e) {
  //   DBUS_LOG_METHOD_CALL_ERROR(server_, "ResolveService", e);
  // }
  //
  // // discovery_cb_.service_lost_cb(std::move(info));
}

void ServiceBrowser::onFailure(const std::string &error) {
  LOG(ERROR) << __func__ << ": " << getProxy().getObjectPath()
                     << ": ServiceBrowser reported a failure: " << error;
}

void ServiceBrowser::onAllForNow() {
  LOG(INFO) << __func__ << ": " << getProxy().getObjectPath()
                       << ": notified via ServiceBrowser that all records have "
                          "been added for now";
}

void ServiceBrowser::onCacheExhausted() {
  LOG(INFO) << __func__ << ": " << getProxy().getObjectPath()
                       << ": notified via ServiceBrowser of cache exhaustion";
}

}  // namespace avahi
}  // namespace linux
}  // namespace nearby
