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

#ifndef THIRD_PARTY_NEARBY_SHARING_NEARBY_SHARING_SERVICE_EXTENSION_H_
#define THIRD_PARTY_NEARBY_SHARING_NEARBY_SHARING_SERVICE_EXTENSION_H_

#include <memory>
#include <string>

#include "internal/crypto_cros/ec_private_key.h"

namespace nearby {
namespace sharing {

class NearbySharingServiceExtension {
 public:
  NearbySharingServiceExtension();

  // Returns the QR Code URL for the current session (carries the sender public
  // key as a base64url compressed EC P-256 point).
  std::string GetQrCodeUrl() const { return qr_code_url_; }

  // Rotates to a fresh ephemeral key + URL.
  void RefreshQrCodeSession();

  // The retained ephemeral private key for the current QR session, or nullptr
  // if key generation failed. Used to prove key possession during the QR
  // handshake (Phase B).
  const crypto::ECPrivateKey* qr_code_private_key() const {
    return qr_code_private_key_.get();
  }

  // The 35-byte QR public blob for the current session ([0x00,0x00,0x02|0x03,
  // X(32)]) carried in the QR URL. This is the exact ikm a scanning peer uses
  // to derive its advertising token, so MatchQrCodeToken keys off it. Empty if
  // key generation failed.
  const std::string& qr_code_public_blob() const {
    return qr_code_public_blob_;
  }

 private:
  std::unique_ptr<crypto::ECPrivateKey> qr_code_private_key_;
  std::string qr_code_url_;
  std::string qr_code_public_blob_;
};

}  // namespace sharing
}  // namespace nearby

#endif  // THIRD_PARTY_NEARBY_SHARING_NEARBY_SHARING_SERVICE_EXTENSION_H_
