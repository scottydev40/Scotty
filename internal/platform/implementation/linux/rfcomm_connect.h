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

// Open a Bluetooth Classic RFCOMM socket to `remote_mac` with no prior bonding
// and no bluez Device1 object (mirrors Android RFCOMM_CreateConnection).
// SDP-queries the peer for `service_uuid` to discover the RFCOMM channel, then
// raw-connects. Tries an authenticated+encrypted (BT_SECURITY_MEDIUM) link
// first — required by Samsung Quick Share (Security Mode 4), also accepted by a
// Pixel — then falls back to an insecure (BT_SECURITY_LOW) link.
//
// For the secure attempt to complete without a pairing prompt on the peer, the
// caller must put the local adapter in NON-bondable mode for the duration of
// this call so SSP negotiates a No-Bonding temporary pairing (see
// BluetoothClassicMedium::ConnectToService).
//
// Returns a connected, blocking socket fd (caller owns and must close) or
// std::nullopt on any failure / cancellation.
std::optional<int> ConnectRfcommByAddress(
    const std::string& remote_mac,       // "AA:BB:CC:DD:EE:FF"
    const std::string& service_uuid,     // e.g. "0000fef3-0000-1000-8000-00805f9b34fb"
    nearby::CancellationFlag* cancellation_flag);

}  // namespace linux
}  // namespace nearby

#endif  // PLATFORM_IMPL_LINUX_RFCOMM_CONNECT_H_
