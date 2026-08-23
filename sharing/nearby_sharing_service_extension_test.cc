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

#include "gtest/gtest.h"
#include "absl/strings/escaping.h"

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

}  // namespace
}  // namespace sharing
}  // namespace nearby
