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

#ifndef PLATFORM_IMPL_LINUX_RFCOMM_CONNECT_H_
#define PLATFORM_IMPL_LINUX_RFCOMM_CONNECT_H_

#include <cstdint>
#include <optional>
#include <string>

#include "internal/platform/cancellation_flag.h"

namespace nearby {
namespace linux {

// Open an INSECURE Bluetooth Classic RFCOMM socket to `remote_address` with no
// prior bonding and no bluez Device1 object (mirrors Android
// RFCOMM_CreateConnection). SDP-queries the peer for `service_uuid` to discover
// the RFCOMM channel, then raw-connects. Returns a connected, blocking socket fd
// (caller owns and must close) or std::nullopt on any failure / cancellation.
std::optional<int> ConnectInsecureRfcommByAddress(
    const std::string& remote_mac,       // "AA:BB:CC:DD:EE:FF"
    const std::string& service_uuid,     // e.g. "0000fef3-0000-1000-8000-00805f9b34fb"
    nearby::CancellationFlag* cancellation_flag);

}  // namespace linux
}  // namespace nearby

#endif  // PLATFORM_IMPL_LINUX_RFCOMM_CONNECT_H_
