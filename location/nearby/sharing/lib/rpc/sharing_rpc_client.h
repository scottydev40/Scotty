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

#ifndef LOCATION_NEARBY_SHARING_LIB_RPC_SHARING_RPC_CLIENT_H_
#define LOCATION_NEARBY_SHARING_LIB_RPC_SHARING_RPC_CLIENT_H_

#include <functional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "sharing/proto/contact_rpc.pb.h"

namespace nearby::sharing::api {

class SharingRpcClient {
 public:
  using ListContactPeopleCallback = std::function<void(
      const absl::StatusOr<nearby::sharing::proto::ListContactPeopleResponse>&)>;

  virtual ~SharingRpcClient() = default;

  virtual void ListContactPeople(
      nearby::sharing::proto::ListContactPeopleRequest request,
      ListContactPeopleCallback callback) = 0;
};

// Trades serialized nearby::sharing::proto::PublicCertificate blobs with the
// plugin over its dev.scotty.MyDevices1 D-Bus contract.
class CertTransportClient {
 public:
  static constexpr absl::Duration kTimeout = absl::Seconds(30);

  // Upload serialized nearby::sharing::proto::PublicCertificate blobs for
  // this device. success=false on any transport/auth failure.
  using UploadCallback = std::function<void(const absl::Status&)>;
  // Download serialized PublicCertificate blobs shared with this device.
  using DownloadCallback =
      std::function<void(const absl::StatusOr<std::vector<std::string>>&)>;

  virtual ~CertTransportClient() = default;

  virtual void UploadCertificates(std::string device_id,
                                   std::vector<std::string> certificates,
                                   absl::Duration timeout,
                                   UploadCallback callback) = 0;
  virtual void DownloadCertificates(std::string device_id,
                                     absl::Duration timeout,
                                     DownloadCallback callback) = 0;
};

}  // namespace nearby::sharing::api

#endif  // LOCATION_NEARBY_SHARING_LIB_RPC_SHARING_RPC_CLIENT_H_

