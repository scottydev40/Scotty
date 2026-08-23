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

#include "sharing/nearby_sharing_service_extension.h"

#include <memory>
#include <string>

#include "absl/strings/escaping.h"
#include "internal/crypto_cros/ec_private_key.h"

namespace nearby {
namespace sharing {
namespace {
constexpr char kQrCodeUrlPrefix[] = "https://quickshare.google/qrcode#key=";
}  // namespace

NearbySharingServiceExtension::NearbySharingServiceExtension() {
  RefreshQrCodeSession();
}

void NearbySharingServiceExtension::RefreshQrCodeSession() {
  qr_code_private_key_ = crypto::ECPrivateKey::Create();
  std::string compressed;
  if (qr_code_private_key_ == nullptr ||
      !qr_code_private_key_->ExportCompressedPublicKey(&compressed)) {
    qr_code_private_key_ = nullptr;
    qr_code_url_.clear();
    return;
  }
  // The on-wire QR key is a 2-byte framing (0x00 0x00) followed by the 33-byte
  // SEC1 compressed point: [0x00, 0x00, 0x02|0x03, X(32)] = 35 bytes.
  std::string key_blob;
  key_blob.reserve(2 + compressed.size());
  key_blob.push_back('\x00');
  key_blob.push_back('\x00');
  key_blob.append(compressed);
  qr_code_url_ = std::string(kQrCodeUrlPrefix) +
                 absl::WebSafeBase64Escape(key_blob);
}

}  // namespace sharing
}  // namespace nearby
