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

#ifndef LOCATION_NEARBY_CPP_SHARING_CLIENTS_CPP_COMMON_NEARBY_SHARING_COMMON_H_
#define LOCATION_NEARBY_CPP_SHARING_CLIENTS_CPP_COMMON_NEARBY_SHARING_COMMON_H_

#include <fstream>
#include <string>

#include "proto/sharing_enums.pb.h"

// Stub of the Google-internal sharing common helpers, covering the subset the
// open-source tree calls. GetPowerStatus() feeds analytics only, but reading
// the real state from sysfs is cheap enough to do anyway.
namespace nearby::sharing::cpp::common {

inline location::nearby::proto::sharing::PowerStatus GetPowerStatus() {
  std::ifstream ac("/sys/class/power_supply/AC/online");
  if (!ac.is_open()) {
    ac.open("/sys/class/power_supply/ACAD/online");
  }
  if (!ac.is_open()) {
    ac.open("/sys/class/power_supply/ADP1/online");
  }
  if (ac.is_open()) {
    std::string value;
    ac >> value;
    return value == "1"
               ? location::nearby::proto::sharing::POWER_STATUS_AC
               : location::nearby::proto::sharing::POWER_STATUS_BATTERY;
  }
  return location::nearby::proto::sharing::POWER_STATUS_UNKNOWN;
}

}  // namespace nearby::sharing::cpp::common

#endif  // LOCATION_NEARBY_CPP_SHARING_CLIENTS_CPP_COMMON_NEARBY_SHARING_COMMON_H_
