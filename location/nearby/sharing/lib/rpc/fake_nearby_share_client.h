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

#ifndef LOCATION_NEARBY_SHARING_LIB_RPC_FAKE_NEARBY_SHARE_CLIENT_H_
#define LOCATION_NEARBY_SHARING_LIB_RPC_FAKE_NEARBY_SHARE_CLIENT_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "location/nearby/sharing/lib/rpc/sharing_rpc_client.h"

namespace nearby::sharing::api {

// A fake CertTransportClient for tests: records every UploadCertificates
// call (device id + blobs) and lets the test script a canned response for
// both Upload and Download. Defaults: Upload succeeds (OkStatus), Download
// returns an empty blob list.
class FakeCertTransportClient : public CertTransportClient {
 public:
  struct UploadCall {
    std::string device_id;
    std::vector<std::string> certificates;
  };

  void UploadCertificates(std::string device_id,
                          std::vector<std::string> certificates,
                          absl::Duration timeout,
                          UploadCallback callback) override {
    static_cast<void>(timeout);
    upload_calls_.push_back(
        UploadCall{std::move(device_id), certificates});
    callback(upload_response_);
  }

  void DownloadCertificates(std::string device_id, absl::Duration timeout,
                            DownloadCallback callback) override {
    static_cast<void>(timeout);
    download_requests_.push_back(std::move(device_id));
    callback(download_response_);
  }

  // Accessors for assertions.
  const std::vector<UploadCall>& upload_calls() const { return upload_calls_; }
  const std::vector<std::string>& download_requests() const {
    return download_requests_;
  }

  // Scripting for the next call(s).
  void SetUploadResponse(absl::Status status) {
    upload_response_ = std::move(status);
  }
  void SetDownloadResponse(
      absl::StatusOr<std::vector<std::string>> response) {
    download_response_ = std::move(response);
  }

 private:
  std::vector<UploadCall> upload_calls_;
  std::vector<std::string> download_requests_;
  absl::Status upload_response_ = absl::OkStatus();
  absl::StatusOr<std::vector<std::string>> download_response_ =
      std::vector<std::string>();
};

}  // namespace nearby::sharing::api

#endif  // LOCATION_NEARBY_SHARING_LIB_RPC_FAKE_NEARBY_SHARE_CLIENT_H_
