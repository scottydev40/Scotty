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
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "absl/strings/escaping.h"
#include "absl/types/span.h"

namespace nearby {
namespace sharing {
namespace {

std::vector<uint8_t> Hex(absl::string_view hex) {
  std::string bytes = absl::HexStringToBytes(hex);
  return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

// Real capture (2026-08-24, cross-account QR session): a Pixel 4 showed this QR
// public blob; a Samsung S26 Ultra scanned it and advertised the token below,
// which the shower decoded to the device name. These are ground-truth vectors
// off the air — do not edit without a fresh capture.
constexpr char kPublicBlob[] =
    "000002cf1a6db8b7b06e14b2cb267f45c96a908ccf21c0653491b7275a586cba1c7006";
constexpr char kAdvertisedToken[] =
    "4baf70eb372c3a29cef1d5bf6f733a0b4e262b7d266904810a64de150307860243dfc333"
    "7027d10099d7e4100953";
constexpr char kExpectedDeviceName[] = "Harsha's S26 Ultra";
// The bare 16-byte match-tag derived from the same blob (info
// "advertisingContext"), used when the device name travels in plaintext.
constexpr char kMatchTag[] = "27de79f045711ffe806d08bd786675f2";

TEST(QrCodeSessionCryptoTest, DecodesRealAdvertisedTokenToDeviceName) {
  std::vector<uint8_t> blob = Hex(kPublicBlob);
  std::vector<uint8_t> token = Hex(kAdvertisedToken);

  QrCodeMatchResult result =
      MatchQrCodeToken(absl::MakeConstSpan(blob), absl::MakeConstSpan(token));

  EXPECT_TRUE(result.matched);
  ASSERT_TRUE(result.device_name.has_value());
  EXPECT_EQ(*result.device_name, kExpectedDeviceName);
}

TEST(QrCodeSessionCryptoTest, MatchesBareMatchTagWithoutDeviceName) {
  std::vector<uint8_t> blob = Hex(kPublicBlob);
  std::vector<uint8_t> tag = Hex(kMatchTag);

  QrCodeMatchResult result =
      MatchQrCodeToken(absl::MakeConstSpan(blob), absl::MakeConstSpan(tag));

  EXPECT_TRUE(result.matched);
  EXPECT_FALSE(result.device_name.has_value());
}

TEST(QrCodeSessionCryptoTest, RejectsTokenFromADifferentSession) {
  // Flip one byte of the blob: the derived key/AAD no longer match, so neither
  // the GCM open nor the match-tag comparison succeeds.
  std::vector<uint8_t> blob = Hex(kPublicBlob);
  blob.back() ^= 0x01;
  std::vector<uint8_t> token = Hex(kAdvertisedToken);

  QrCodeMatchResult result =
      MatchQrCodeToken(absl::MakeConstSpan(blob), absl::MakeConstSpan(token));

  EXPECT_FALSE(result.matched);
  EXPECT_FALSE(result.device_name.has_value());
}

TEST(QrCodeSessionCryptoTest, HandlesEmptyInputs) {
  std::vector<uint8_t> blob = Hex(kPublicBlob);
  std::vector<uint8_t> token = Hex(kAdvertisedToken);

  EXPECT_FALSE(MatchQrCodeToken(absl::MakeConstSpan(blob), {}).matched);
  EXPECT_FALSE(MatchQrCodeToken({}, absl::MakeConstSpan(token)).matched);
}

}  // namespace
}  // namespace sharing
}  // namespace nearby
