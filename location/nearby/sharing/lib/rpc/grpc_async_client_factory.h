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

#ifndef LOCATION_NEARBY_SHARING_LIB_RPC_GRPC_ASYNC_CLIENT_FACTORY_H_
#define LOCATION_NEARBY_SHARING_LIB_RPC_GRPC_ASYNC_CLIENT_FACTORY_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <sdbus-c++/Error.h>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/Types.h>

#include "absl/status/status.h"
#include "internal/platform/clock.h"
#include "location/nearby/sharing/lib/account/account_manager.h"
#include "location/nearby/sharing/lib/rpc/sharing_rpc_client.h"
#include "sharing/analytics/analytics_recorder.h"

namespace nearby::sharing::platform::common {
namespace internal {

class NoOpSharingRpcClient : public nearby::sharing::api::SharingRpcClient {
 public:
  void ListContactPeople(
      nearby::sharing::proto::ListContactPeopleRequest request,
      ListContactPeopleCallback callback) override {
    static_cast<void>(request);
    callback(nearby::sharing::proto::ListContactPeopleResponse());
  }
};

class NoOpCertTransportClient
    : public nearby::sharing::api::CertTransportClient {
 public:
  void UploadCertificates(std::string device_id,
                          std::vector<std::string> certificates,
                          absl::Duration timeout,
                          UploadCallback callback) override {
    static_cast<void>(device_id);
    static_cast<void>(certificates);
    static_cast<void>(timeout);
    callback(absl::UnavailableError("no plugin"));
  }

  void DownloadCertificates(std::string device_id, absl::Duration timeout,
                            DownloadCallback callback) override {
    static_cast<void>(device_id);
    static_cast<void>(timeout);
    callback(absl::UnavailableError("no plugin"));
  }
};

}  // namespace internal

// Routes certificate upload/download through the opt-in My-Devices plugin
// over session D-Bus (dev.scotty.MyDevices1): hand serialized
// nearby::sharing::proto::PublicCertificate blobs to the plugin (which makes
// the authenticated gRPC call to Google) and get blobs back. The core links
// no grpc/token/auth code. Only ever called when an account is present (i.e.
// the plugin is signed in); with no plugin the cert manager never reaches
// these paths (GetCurrentAccount() is nullopt).
class DBusCertTransportClient
    : public nearby::sharing::api::CertTransportClient {
 public:
  DBusCertTransportClient() {
    try {
      connection_ = sdbus::createSessionBusConnection();
    } catch (const sdbus::Error&) {
      connection_.reset();  // no session bus -> every call reports unavailable
    }
  }

  void UploadCertificates(std::string device_id,
                          std::vector<std::string> certificates,
                          absl::Duration timeout,
                          UploadCallback callback) override {
    static_cast<void>(timeout);
    if (connection_ == nullptr) {
      callback(absl::UnavailableError("no session bus for My-Devices plugin"));
      return;
    }
    try {
      auto proxy = sdbus::createProxy(
          *connection_, sdbus::ServiceName{"dev.scotty.MyDevices1"},
          sdbus::ObjectPath{"/dev/scotty/MyDevices1"});
      std::vector<std::vector<uint8_t>> certs;
      certs.reserve(certificates.size());
      for (auto& c : certificates) certs.emplace_back(c.begin(), c.end());
      bool ok = false;
      proxy->callMethod("UploadCertificates")
          .onInterface("dev.scotty.MyDevices1")
          .withArguments(device_id, certs)
          .storeResultsTo(ok);
      callback(ok ? absl::OkStatus()
                  : absl::UnavailableError("plugin rejected upload"));
    } catch (const sdbus::Error& e) {
      callback(absl::UnavailableError(e.getName() + ": " + e.getMessage()));
    }
  }

  void DownloadCertificates(std::string device_id, absl::Duration timeout,
                            DownloadCallback callback) override {
    static_cast<void>(timeout);
    if (connection_ == nullptr) {
      callback(absl::UnavailableError("no session bus for My-Devices plugin"));
      return;
    }
    try {
      auto proxy = sdbus::createProxy(
          *connection_, sdbus::ServiceName{"dev.scotty.MyDevices1"},
          sdbus::ObjectPath{"/dev/scotty/MyDevices1"});
      std::vector<std::vector<uint8_t>> out;
      proxy->callMethod("DownloadCertificates")
          .onInterface("dev.scotty.MyDevices1")
          .withArguments(device_id)
          .storeResultsTo(out);
      std::vector<std::string> certs;
      certs.reserve(out.size());
      for (auto& c : out) certs.emplace_back(c.begin(), c.end());
      callback(certs);
    } catch (const sdbus::Error& e) {
      callback(absl::UnavailableError(e.getName() + ": " + e.getMessage()));
    }
  }

 private:
  std::unique_ptr<sdbus::IConnection> connection_;
};

class GrpcAsyncClientFactory {
 public:
  GrpcAsyncClientFactory(AccountManager* account_manager, Clock* clock,
                         analytics::AnalyticsRecorder* analytics_recorder) {
    static_cast<void>(account_manager);
    static_cast<void>(clock);
    static_cast<void>(analytics_recorder);
  }

  std::unique_ptr<nearby::sharing::api::SharingRpcClient> CreateInstance() {
    return std::make_unique<internal::NoOpSharingRpcClient>();
  }

  std::unique_ptr<nearby::sharing::api::CertTransportClient>
  CreateCertTransportInstance() {
    return std::make_unique<DBusCertTransportClient>();
  }
};

}  // namespace nearby::sharing::platform::common

#endif  // LOCATION_NEARBY_SHARING_LIB_RPC_GRPC_ASYNC_CLIENT_FACTORY_H_

