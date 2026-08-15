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

#include "internal/platform/implementation/linux/bluez.h"

#include "gtest/gtest.h"
#include "internal/platform/mac_address.h"

namespace nearby {
namespace linux {
namespace {

TEST(BluezTest, MacFromDeviceObjectPath) {
  auto mac = bluez::mac_from_device_object_path(
      "/org/bluez/hci0/dev_74_F4_41_3F_12_D8");
  ASSERT_TRUE(mac.has_value());
  EXPECT_EQ(mac->ToString(), "74:F4:41:3F:12:D8");
}

TEST(BluezTest, MacFromDeviceObjectPathRejectsGarbage) {
  EXPECT_FALSE(bluez::mac_from_device_object_path("/org/bluez/hci0").has_value());
  EXPECT_FALSE(bluez::mac_from_device_object_path("").has_value());
  EXPECT_FALSE(
      bluez::mac_from_device_object_path("/org/bluez/hci0/dev_zz").has_value());
}

}  // namespace
}  // namespace linux
}  // namespace nearby
