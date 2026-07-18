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

#ifndef THIRD_PARTY_GLOOP_UTIL_TIME_PROTOUTIL_H_
#define THIRD_PARTY_GLOOP_UTIL_TIME_PROTOUTIL_H_

#include <cstdint>

#include "google/protobuf/timestamp.pb.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

// Stub of Google-internal util_time proto helpers, matching the subset the
// open-source tree actually calls.
namespace util_time {

inline absl::StatusOr<google::protobuf::Timestamp> EncodeGoogleApiProto(
    absl::Time time) {
  google::protobuf::Timestamp proto;
  const int64_t seconds = absl::ToUnixSeconds(time);
  proto.set_seconds(seconds);
  proto.set_nanos(static_cast<int32_t>(
      absl::ToInt64Nanoseconds(time - absl::FromUnixSeconds(seconds))));
  return proto;
}

}  // namespace util_time

#endif  // THIRD_PARTY_GLOOP_UTIL_TIME_PROTOUTIL_H_
