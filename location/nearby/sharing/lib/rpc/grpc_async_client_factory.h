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
#include "location/nearby/sharing/lib/rpc/identity_rpc_wire.h"
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

class NoOpIdentityRpcClient : public nearby::sharing::api::IdentityRpcClient {
 public:
  void QuerySharedCredentials(
      google::nearby::identity::v1::QuerySharedCredentialsRequest request,
      absl::Duration timeout, QuerySharedCredentialsCallback callback) override {
    static_cast<void>(request);
    static_cast<void>(timeout);
    callback(google::nearby::identity::v1::QuerySharedCredentialsResponse());
  }

  void QuerySharedCredentialsWithBindingIds(
      google::nearby::identity::v1::QuerySharedCredentialsWithBindingIdsRequest
          request,
      absl::Duration timeout,
      QuerySharedCredentialsWithBindingIdsCallback callback) override {
    static_cast<void>(request);
    static_cast<void>(timeout);
    callback(google::nearby::identity::v1::
                 QuerySharedCredentialsWithBindingIdsResponse());
  }

  void PublishDevice(
      google::nearby::identity::v1::PublishDeviceRequest request,
      absl::Duration timeout, PublishDeviceCallback callback) override {
    static_cast<void>(request);
    static_cast<void>(timeout);
    callback(google::nearby::identity::v1::PublishDeviceResponse());
  }

  void GetAccountInfo(
      google::nearby::identity::v1::GetAccountInfoRequest request,
      absl::Duration timeout, GetAccountInfoCallback callback) override {
    static_cast<void>(request);
    static_cast<void>(timeout);
    callback(google::nearby::identity::v1::GetAccountInfoResponse());
  }
};

}  // namespace internal

namespace wire = ::google::nearby::identity::v1::wire;

// Routes the Identity RPCs through the opt-in My-Devices plugin over session
// D-Bus (dev.scotty.MyDevices1): serialize the request proto, hand the bytes to
// the plugin (which makes the authenticated gRPC call to Google), and parse the
// response bytes back. The core links no grpc/token/auth code. Only ever called
// when an account is present (i.e. the plugin is signed in); with no plugin the
// cert manager never reaches these paths (GetCurrentAccount() is nullopt).
class DBusIdentityRpcClient : public nearby::sharing::api::IdentityRpcClient {
 public:
  DBusIdentityRpcClient() {
    try {
      connection_ = sdbus::createSessionBusConnection();
    } catch (const sdbus::Error&) {
      connection_.reset();  // no session bus -> every call reports unavailable
    }
  }

  void QuerySharedCredentials(
      google::nearby::identity::v1::QuerySharedCredentialsRequest request,
      absl::Duration timeout,
      QuerySharedCredentialsCallback callback) override {
    static_cast<void>(timeout);
    std::string resp;
    if (absl::Status s = Call("QuerySharedCredentials",
                              wire::Serialize(request), &resp);
        !s.ok()) {
      callback(s);
      return;
    }
    google::nearby::identity::v1::QuerySharedCredentialsResponse out;
    if (!wire::Parse(resp, &out)) {
      callback(absl::InternalError("failed to parse QuerySharedCredentials"));
      return;
    }
    callback(out);
  }

  void QuerySharedCredentialsWithBindingIds(
      google::nearby::identity::v1::
          QuerySharedCredentialsWithBindingIdsRequest request,
      absl::Duration timeout,
      QuerySharedCredentialsWithBindingIdsCallback callback) override {
    static_cast<void>(timeout);
    std::string resp;
    if (absl::Status s = Call("QuerySharedCredentialsWithBindingIds",
                              wire::Serialize(request), &resp);
        !s.ok()) {
      callback(s);
      return;
    }
    google::nearby::identity::v1::QuerySharedCredentialsWithBindingIdsResponse
        out;
    if (!wire::Parse(resp, &out)) {
      callback(absl::InternalError("failed to parse WithBindingIds response"));
      return;
    }
    callback(out);
  }

  void PublishDevice(
      google::nearby::identity::v1::PublishDeviceRequest request,
      absl::Duration timeout, PublishDeviceCallback callback) override {
    static_cast<void>(timeout);
    std::string resp;
    if (absl::Status s = Call("PublishDevice", wire::Serialize(request), &resp);
        !s.ok()) {
      callback(s);
      return;
    }
    google::nearby::identity::v1::PublishDeviceResponse out;
    if (!wire::Parse(resp, &out)) {
      callback(absl::InternalError("failed to parse PublishDevice response"));
      return;
    }
    callback(out);
  }

  void GetAccountInfo(
      google::nearby::identity::v1::GetAccountInfoRequest request,
      absl::Duration timeout, GetAccountInfoCallback callback) override {
    static_cast<void>(request);
    static_cast<void>(timeout);
    // The identity-RPC GetAccountInfo (capabilities / advanced protection) is
    // not needed for publish/download and would collide with the seam's
    // account-state GetAccountInfo method. Return an empty response; the
    // capabilities default (advanced protection off) is correct.
    callback(google::nearby::identity::v1::GetAccountInfoResponse());
  }

 private:
  // Synchronous D-Bus method call to the plugin: send `request` bytes, receive
  // response bytes. Blocks on the plugin's gRPC round-trip (background schedule).
  absl::Status Call(const char* method, const std::string& request,
                    std::string* response) {
    if (connection_ == nullptr) {
      return absl::UnavailableError("no session bus for My-Devices plugin");
    }
    try {
      auto proxy = sdbus::createProxy(
          *connection_, sdbus::ServiceName{"dev.scotty.MyDevices1"},
          sdbus::ObjectPath{"/dev/scotty/MyDevices1"});
      std::vector<uint8_t> in(request.begin(), request.end());
      std::vector<uint8_t> out;
      proxy->callMethod(method)
          .onInterface("dev.scotty.MyDevices1")
          .withArguments(in)
          .storeResultsTo(out);
      response->assign(out.begin(), out.end());
      return absl::OkStatus();
    } catch (const sdbus::Error& e) {
      return absl::UnavailableError(e.getName() + ": " + e.getMessage());
    }
  }

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

  std::unique_ptr<nearby::sharing::api::IdentityRpcClient>
  CreateIdentityInstance() {
    return std::make_unique<DBusIdentityRpcClient>();
  }
};

}  // namespace nearby::sharing::platform::common

#endif  // LOCATION_NEARBY_SHARING_LIB_RPC_GRPC_ASYNC_CLIENT_FACTORY_H_

