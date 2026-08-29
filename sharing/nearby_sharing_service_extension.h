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

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "internal/crypto_cros/ec_private_key.h"

namespace nearby {
namespace sharing {

// Holds the ephemeral EC key that backs the "share via QR code" session. The
// key is treated as a one-shot credential: a peer that scans the QR is
// auto-authorized to receive, so the UI rotates it (RefreshQrCodeSession) each
// time it shows a fresh QR and burns it once the QR is hidden / the share sheet
// closes — a photographed or stale QR must not stay usable. Rotation happens on
// the UI thread while the QR fields are read on the engine's service thread
// (MatchQrCodeToken, handshake signing), so all access is guarded by mu_.
class NearbySharingServiceExtension {
 public:
  NearbySharingServiceExtension();

  // Returns the QR Code URL for the current session (carries the sender public
  // key as a base64url compressed EC P-256 point).
  std::string GetQrCodeUrl() const ABSL_LOCKS_EXCLUDED(mu_);

  // Rotates to a fresh ephemeral key + URL, invalidating any previously shown
  // QR. Safe to call from any thread.
  void RefreshQrCodeSession() ABSL_LOCKS_EXCLUDED(mu_);

  // The 35-byte QR public blob for the current session ([0x00,0x00,0x02|0x03,
  // X(32)]) carried in the QR URL. This is the exact ikm a scanning peer uses
  // to derive its advertising token, so MatchQrCodeToken keys off it. Empty if
  // key generation failed. Returned by value so it stays valid across a
  // concurrent rotation.
  std::string qr_code_public_blob() const ABSL_LOCKS_EXCLUDED(mu_);

  // The ephemeral private key for the current QR session, or nullptr if key
  // generation failed. Not synchronised — for same-thread/test use only;
  // production handshake signing goes through SignQrHandshakeToken (which locks).
  const crypto::ECPrivateKey* qr_code_private_key() const
      ABSL_NO_THREAD_SAFETY_ANALYSIS {
    return qr_code_private_key_.get();
  }

  // Signs the UKEY2 authentication token with the current QR ephemeral private
  // key and returns the signature as IEEE-P1363 (raw R||S, 64 bytes for P-256).
  // This is the qr_code_handshake_data that lets a scanning peer skip its accept
  // prompt (Phase B silent auto-accept). Returns nullopt if there is no QR key
  // or signing fails. See grishka NearDrop PROTOCOL.md, QR-code session.
  std::optional<std::vector<uint8_t>> SignQrHandshakeToken(
      absl::Span<const uint8_t> ukey2_auth_token) const ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // Regenerates the key/url/blob. Caller must hold mu_.
  void RefreshQrCodeSessionLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  mutable absl::Mutex mu_;
  std::unique_ptr<crypto::ECPrivateKey> qr_code_private_key_ ABSL_GUARDED_BY(mu_);
  std::string qr_code_url_ ABSL_GUARDED_BY(mu_);
  std::string qr_code_public_blob_ ABSL_GUARDED_BY(mu_);
};

}  // namespace sharing
}  // namespace nearby

#endif  // THIRD_PARTY_NEARBY_SHARING_NEARBY_SHARING_SERVICE_EXTENSION_H_
