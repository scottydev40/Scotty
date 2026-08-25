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

#include "sharing/qr_code_session_crypto.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include <openssl/aead.h>
#include <openssl/digest.h>
#include <openssl/hkdf.h>
#include <openssl/mem.h>

namespace nearby {
namespace sharing {
namespace {

constexpr size_t kKeyLen = 16;   // AES-128
constexpr size_t kTagLen = 16;   // GCM tag
constexpr size_t kIvLen = 12;    // GCM IV
constexpr size_t kMatchTagLen = 16;

// HKDF-SHA256 with an empty salt (BoringSSL substitutes a zero-filled salt of
// the hash length, which is the HMAC-SHA256(0x00*32, ikm) extract step).
bool HkdfSha256(absl::Span<const uint8_t> ikm, absl::string_view info,
                absl::Span<uint8_t> out) {
  return HKDF(out.data(), out.size(), EVP_sha256(), ikm.data(), ikm.size(),
              /*salt=*/nullptr, /*salt_len=*/0,
              reinterpret_cast<const uint8_t*>(info.data()), info.size()) == 1;
}

}  // namespace

QrCodeMatchResult MatchQrCodeToken(absl::Span<const uint8_t> public_blob,
                                   absl::Span<const uint8_t> token) {
  QrCodeMatchResult result;
  if (public_blob.empty() || token.empty()) {
    return result;
  }

  uint8_t match_tag[kMatchTagLen];
  uint8_t key[kKeyLen];
  if (!HkdfSha256(public_blob, "advertisingContext",
                  absl::MakeSpan(match_tag, kMatchTagLen)) ||
      !HkdfSha256(public_blob, "encryptionKey",
                  absl::MakeSpan(key, kKeyLen))) {
    return result;
  }

  // Bare match-tag: the device name is carried in the plaintext advert field.
  if (token.size() == kMatchTagLen &&
      CRYPTO_memcmp(token.data(), match_tag, kMatchTagLen) == 0) {
    result.matched = true;
    return result;
  }

  // Otherwise: IV(12) || ciphertext || tag(16), AAD = match_tag.
  if (token.size() < kIvLen + kTagLen) {
    return result;
  }

  bssl::ScopedEVP_AEAD_CTX ctx;
  if (!EVP_AEAD_CTX_init(ctx.get(), EVP_aead_aes_128_gcm(), key, kKeyLen,
                         kTagLen, /*engine=*/nullptr)) {
    return result;
  }

  const uint8_t* nonce = token.data();
  absl::Span<const uint8_t> sealed = token.subspan(kIvLen);  // ciphertext || tag
  std::vector<uint8_t> plaintext(sealed.size());
  size_t out_len = 0;
  if (EVP_AEAD_CTX_open(ctx.get(), plaintext.data(), &out_len, plaintext.size(),
                        nonce, kIvLen, sealed.data(), sealed.size(), match_tag,
                        kMatchTagLen) != 1) {
    return result;  // authentication failed -> not our QR session
  }

  result.matched = true;
  result.device_name =
      std::string(reinterpret_cast<const char*>(plaintext.data()), out_len);
  return result;
}

}  // namespace sharing
}  // namespace nearby
