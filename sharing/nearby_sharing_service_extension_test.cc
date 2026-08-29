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

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <openssl/base.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/mem.h>
#include "gtest/gtest.h"
#include "absl/strings/escaping.h"
#include "absl/types/span.h"
#include "internal/crypto_cros/ec_private_key.h"
#include "internal/crypto_cros/nearby_base.h"
#include "internal/crypto_cros/signature_verifier.h"

namespace nearby {
namespace sharing {
namespace {

class NearbySharingServiceExtensionTest : public ::testing::Test {
 public:
  NearbySharingServiceExtensionTest() = default;

  void SetUp() override {
    service_extension_ = std::make_unique<NearbySharingServiceExtension>();
  }

  NearbySharingServiceExtension* service_extension() {
    return service_extension_.get();
  }

 private:
  std::unique_ptr<NearbySharingServiceExtension> service_extension_;
};

TEST_F(NearbySharingServiceExtensionTest, GetQrCodeUrlHasEphemeralKey) {
  const std::string url = service_extension()->GetQrCodeUrl();
  constexpr char kPrefix[] = "https://quickshare.google/qrcode#key=";
  ASSERT_EQ(url.rfind(kPrefix, 0), 0u) << "url=" << url;

  const std::string key_b64 = url.substr(std::string(kPrefix).size());
  std::string key_bytes;
  ASSERT_TRUE(absl::WebSafeBase64Unescape(key_b64, &key_bytes));
  // On-wire format: [0x00, 0x00, 0x02|0x03, X(32)] = 35 bytes.
  ASSERT_EQ(key_bytes.size(), 35u);
  EXPECT_EQ(static_cast<uint8_t>(key_bytes[0]), 0x00);
  EXPECT_EQ(static_cast<uint8_t>(key_bytes[1]), 0x00);
  EXPECT_TRUE(static_cast<uint8_t>(key_bytes[2]) == 0x02 ||
              static_cast<uint8_t>(key_bytes[2]) == 0x03);

  EXPECT_NE(service_extension()->qr_code_private_key(), nullptr);
}

TEST_F(NearbySharingServiceExtensionTest, RefreshQrCodeSessionRotatesKey) {
  const std::string first = service_extension()->GetQrCodeUrl();
  service_extension()->RefreshQrCodeSession();
  const std::string second = service_extension()->GetQrCodeUrl();
  EXPECT_NE(first, second);
}

namespace {
// Converts an IEEE-P1363 (raw R||S, 64 bytes) P-256 signature to the DER
// ECDSA-Sig-Value that SignatureVerifier expects.
std::vector<uint8_t> P1363ToDer(absl::Span<const uint8_t> p1363) {
  bssl::UniquePtr<BIGNUM> r(BN_bin2bn(p1363.data(), 32, nullptr));
  bssl::UniquePtr<BIGNUM> s(BN_bin2bn(p1363.data() + 32, 32, nullptr));
  bssl::UniquePtr<ECDSA_SIG> sig(ECDSA_SIG_new());
  ECDSA_SIG_set0(sig.get(), r.release(), s.release());
  uint8_t* der = nullptr;
  size_t der_len = 0;
  ECDSA_SIG_to_bytes(&der, &der_len, sig.get());
  std::vector<uint8_t> out(der, der + der_len);
  OPENSSL_free(der);
  return out;
}
}  // namespace

TEST_F(NearbySharingServiceExtensionTest,
       SignQrHandshakeTokenVerifiesUnderQrPublicKey) {
  const std::vector<uint8_t> token = {0x00, 0x01, 0x02, 0x03,
                                      0x04, 0x05, 0x06, 0x07};

  std::optional<std::vector<uint8_t>> signature =
      service_extension()->SignQrHandshakeToken(token);
  ASSERT_TRUE(signature.has_value());
  // P-256 IEEE-P1363 signature is exactly 64 bytes (raw R||S).
  ASSERT_EQ(signature->size(), 64u);

  // The scanning peer verifies this against the public key it read from the QR.
  std::vector<uint8_t> public_key_info;
  ASSERT_TRUE(service_extension()->qr_code_private_key()->ExportPublicKey(
      &public_key_info));

  crypto::SignatureVerifier verifier;
  ASSERT_TRUE(verifier.VerifyInit(crypto::SignatureVerifier::ECDSA_SHA256,
                                  P1363ToDer(*signature), public_key_info));
  verifier.VerifyUpdate(token);
  EXPECT_TRUE(verifier.VerifyFinal());
}

TEST_F(NearbySharingServiceExtensionTest,
       SignQrHandshakeTokenRejectsWrongToken) {
  const std::vector<uint8_t> token = {0x10, 0x11, 0x12, 0x13};
  const std::vector<uint8_t> other_token = {0x20, 0x21, 0x22, 0x23};

  std::optional<std::vector<uint8_t>> signature =
      service_extension()->SignQrHandshakeToken(token);
  ASSERT_TRUE(signature.has_value());

  std::vector<uint8_t> public_key_info;
  ASSERT_TRUE(service_extension()->qr_code_private_key()->ExportPublicKey(
      &public_key_info));

  crypto::SignatureVerifier verifier;
  ASSERT_TRUE(verifier.VerifyInit(crypto::SignatureVerifier::ECDSA_SHA256,
                                  P1363ToDer(*signature), public_key_info));
  verifier.VerifyUpdate(other_token);
  EXPECT_FALSE(verifier.VerifyFinal());
}

// The QR public blob (what MatchQrCodeToken keys off) must also change on
// rotation, not just the URL string — that is what makes an old/photographed QR
// stop matching.
TEST_F(NearbySharingServiceExtensionTest, RefreshQrCodeSessionRotatesPublicBlob) {
  const std::string blob_before = service_extension()->qr_code_public_blob();
  ASSERT_EQ(blob_before.size(), 35u);

  service_extension()->RefreshQrCodeSession();

  const std::string blob_after = service_extension()->qr_code_public_blob();
  ASSERT_EQ(blob_after.size(), 35u);
  EXPECT_NE(blob_before, blob_after);
}

}  // namespace
}  // namespace sharing
}  // namespace nearby
