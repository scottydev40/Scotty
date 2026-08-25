// Copyright 2026 The Scotty Authors
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

#ifndef THIRD_PARTY_NEARBY_SHARING_QR_CODE_SESSION_CRYPTO_H_
#define THIRD_PARTY_NEARBY_SHARING_QR_CODE_SESSION_CRYPTO_H_

#include <cstdint>
#include <optional>
#include <string>

#include "absl/types/span.h"

namespace nearby {
namespace sharing {

// Result of matching an advertised QR-code token against a local QR session.
struct QrCodeMatchResult {
  // True when the token belongs to this QR session (its AEAD decrypts under the
  // session key, or it equals the bare match-tag).
  bool matched = false;
  // The decrypted device name, when the token carried one. Absent when the
  // token was the bare match-tag (the device name travels in the plaintext
  // advertisement field instead).
  std::optional<std::string> device_name;
};

// Matches a Nearby Sharing QR-code advertising token against a QR session
// identified by |public_blob| (the exact bytes carried in the QR URL: for a
// P-256 session, [0x00, 0x00, 0x02|0x03, X(32)] = 35 bytes), and, on a match,
// recovers any device name the token carried.
//
// The token is one of:
//   - the 16-byte match-tag T (device name is plaintext elsewhere), or
//   - IV(12) || AES-128-GCM ciphertext || tag(16), decrypting to the UTF-8
//     device name with T as the additional authenticated data.
//
// Key schedule (matches Google Nearby / Quick Share):
//   PRK = HMAC-SHA256(key = 0x00 * 32, msg = public_blob)
//   T   = HKDF-Expand-SHA256(PRK, info = "advertisingContext", len = 16)
//   K   = HKDF-Expand-SHA256(PRK, info = "encryptionKey",      len = 16)
// which is HKDF-SHA256(ikm = public_blob, salt = empty) for each info string.
QrCodeMatchResult MatchQrCodeToken(absl::Span<const uint8_t> public_blob,
                                   absl::Span<const uint8_t> token);

}  // namespace sharing
}  // namespace nearby

#endif  // THIRD_PARTY_NEARBY_SHARING_QR_CODE_SESSION_CRYPTO_H_
