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

// Hand-rolled protobuf wire codec for the google.nearby.identity.v1 messages.
// The in-tree message classes (identity_rpc_types.h) are plain structs with no
// protobuf runtime, so the My-Devices D-Bus proxy serializes requests and parses
// responses here. Field numbers come from the reconstructed .proto (see
// scotty-mydevices/proto) and are verified against live wire captures.
//
// Only the fields the core actually uses are handled; unknown fields are
// skipped on parse and omitted on serialize.

#ifndef LOCATION_NEARBY_SHARING_LIB_RPC_IDENTITY_RPC_WIRE_H_
#define LOCATION_NEARBY_SHARING_LIB_RPC_IDENTITY_RPC_WIRE_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "location/nearby/sharing/lib/rpc/identity_rpc_types.h"

namespace google::nearby::identity::v1::wire {

// --- low-level protobuf wire primitives --------------------------------------

inline void WriteVarint(std::string& out, uint64_t v) {
  while (v >= 0x80) {
    out.push_back(static_cast<char>((v & 0x7f) | 0x80));
    v >>= 7;
  }
  out.push_back(static_cast<char>(v));
}
inline void WriteTag(std::string& out, int field, int wire) {
  WriteVarint(out, (static_cast<uint64_t>(field) << 3) | wire);
}
inline void WriteVarintField(std::string& out, int field, uint64_t v) {
  WriteTag(out, field, 0);
  WriteVarint(out, v);
}
inline void WriteBytesField(std::string& out, int field, std::string_view b) {
  WriteTag(out, field, 2);
  WriteVarint(out, b.size());
  out.append(b.data(), b.size());
}

inline bool ReadVarint(std::string_view& in, uint64_t* v) {
  *v = 0;
  int shift = 0;
  while (!in.empty()) {
    uint8_t b = static_cast<uint8_t>(in.front());
    in.remove_prefix(1);
    *v |= static_cast<uint64_t>(b & 0x7f) << shift;
    if ((b & 0x80) == 0) return true;
    shift += 7;
    if (shift > 63) return false;
  }
  return false;
}
inline bool ReadTag(std::string_view& in, int* field, int* wire) {
  uint64_t t;
  if (!ReadVarint(in, &t)) return false;
  *field = static_cast<int>(t >> 3);
  *wire = static_cast<int>(t & 0x7);
  return true;
}
inline bool ReadLenDelim(std::string_view& in, std::string_view* out) {
  uint64_t n;
  if (!ReadVarint(in, &n)) return false;
  if (n > in.size()) return false;
  *out = in.substr(0, n);
  in.remove_prefix(n);
  return true;
}
inline bool SkipField(std::string_view& in, int wire) {
  switch (wire) {
    case 0: {
      uint64_t v;
      return ReadVarint(in, &v);
    }
    case 2: {
      std::string_view b;
      return ReadLenDelim(in, &b);
    }
    case 5:
      if (in.size() < 4) return false;
      in.remove_prefix(4);
      return true;
    case 1:
      if (in.size() < 8) return false;
      in.remove_prefix(8);
      return true;
    default:
      return false;
  }
}

// --- messages ----------------------------------------------------------------

// SharedCredential{ id:1, data_type:2, data:3, expiration_time:5 }
inline std::string SerializeSharedCredential(const SharedCredential& c) {
  std::string out;
  if (c.id() != 0) WriteVarintField(out, 1, c.id());
  if (c.data_type() != SharedCredential::DATA_TYPE_UNKNOWN)
    WriteVarintField(out, 2, static_cast<uint64_t>(c.data_type()));
  if (!c.data().empty()) WriteBytesField(out, 3, c.data());
  return out;
}
inline bool ParseSharedCredential(std::string_view in, SharedCredential* c) {
  int f, w;
  while (!in.empty()) {
    if (!ReadTag(in, &f, &w)) return false;
    if (f == 1 && w == 0) {
      uint64_t v;
      if (!ReadVarint(in, &v)) return false;
      c->set_id(v);
    } else if (f == 2 && w == 0) {
      uint64_t v;
      if (!ReadVarint(in, &v)) return false;
      c->set_data_type(static_cast<SharedCredential::DataType>(v));
    } else if (f == 3 && w == 2) {
      std::string_view d;
      if (!ReadLenDelim(in, &d)) return false;
      c->set_data(std::string(d));
    } else if (!SkipField(in, w)) {
      return false;  // expiration_time etc. skipped
    }
  }
  return true;
}

// QuerySharedCredentialsRequest{ name:1, page_size:2, page_token:3 }
inline std::string Serialize(const QuerySharedCredentialsRequest& r) {
  std::string out;
  if (!r.name().empty()) WriteBytesField(out, 1, r.name());
  if (!r.page_token().empty()) WriteBytesField(out, 3, r.page_token());
  return out;
}

// QuerySharedCredentialsResponse{ shared_credentials:1, next_page_token:2 }
inline bool Parse(std::string_view in, QuerySharedCredentialsResponse* r) {
  int f, w;
  while (!in.empty()) {
    if (!ReadTag(in, &f, &w)) return false;
    if (f == 1 && w == 2) {
      std::string_view sc;
      if (!ReadLenDelim(in, &sc)) return false;
      if (!ParseSharedCredential(sc, r->add_shared_credentials())) return false;
    } else if (f == 2 && w == 2) {
      std::string_view t;
      if (!ReadLenDelim(in, &t)) return false;
      r->set_next_page_token(std::string(t));
    } else if (!SkipField(in, w)) {
      return false;
    }
  }
  return true;
}

// Device{ name:1, display_name:2, contact:3, per_visibility_shared_credentials:4 }
inline std::string SerializeDevice(const Device& d) {
  std::string out;
  if (!d.name().empty()) WriteBytesField(out, 1, d.name());
  if (!d.display_name().empty()) WriteBytesField(out, 2, d.display_name());
  if (d.contact() != Device::CONTACT_UNKNOWN)
    WriteVarintField(out, 3, static_cast<uint64_t>(d.contact()));
  for (const auto& pv : d.per_visibility_shared_credentials()) {
    // PerVisibilitySharedCredentials{ visibility:1, shared_credentials:2 }
    std::string pvs;
    if (pv.visibility() != PerVisibilitySharedCredentials::VISIBILITY_UNKNOWN)
      WriteVarintField(pvs, 1, static_cast<uint64_t>(pv.visibility()));
    for (const auto& sc : pv.shared_credentials())
      WriteBytesField(pvs, 2, SerializeSharedCredential(sc));
    WriteBytesField(out, 4, pvs);
  }
  return out;
}

// PublishDeviceRequest{ device:1 }
inline std::string Serialize(const PublishDeviceRequest& r) {
  std::string out;
  WriteBytesField(out, 1, SerializeDevice(r.device()));
  return out;
}

// PublishDeviceResponse{ contact_updates:1 (repeated enum) }
inline bool Parse(std::string_view in, PublishDeviceResponse* r) {
  int f, w;
  while (!in.empty()) {
    if (!ReadTag(in, &f, &w)) return false;
    if (f == 1 && w == 0) {
      uint64_t v;
      if (!ReadVarint(in, &v)) return false;
      r->add_contact_updates(static_cast<PublishDeviceResponse::ContactUpdate>(v));
    } else if (f == 1 && w == 2) {
      // packed repeated enum
      std::string_view packed;
      if (!ReadLenDelim(in, &packed)) return false;
      uint64_t v;
      while (ReadVarint(packed, &v))
        r->add_contact_updates(
            static_cast<PublishDeviceResponse::ContactUpdate>(v));
    } else if (!SkipField(in, w)) {
      return false;
    }
  }
  return true;
}

// GetAccountInfoRequest{} (empty)
inline std::string Serialize(const GetAccountInfoRequest&) { return std::string(); }

// GetAccountInfoResponse{ account_info:1 }; AccountInfo{ current_dusi:1,
// capabilities:2 } -- the core type keeps only capabilities.
inline bool Parse(std::string_view in, GetAccountInfoResponse* r) {
  int f, w;
  while (!in.empty()) {
    if (!ReadTag(in, &f, &w)) return false;
    if (f == 1 && w == 2) {
      std::string_view ai;
      if (!ReadLenDelim(in, &ai)) return false;
      AccountInfo* info = r->mutable_account_info();
      int f2, w2;
      while (!ai.empty()) {
        if (!ReadTag(ai, &f2, &w2)) return false;
        if (f2 == 2 && w2 == 0) {
          uint64_t v;
          if (!ReadVarint(ai, &v)) return false;
          info->add_capabilities(static_cast<AccountInfo::Capability>(v));
        } else if (f2 == 2 && w2 == 2) {
          std::string_view packed;
          if (!ReadLenDelim(ai, &packed)) return false;
          uint64_t v;
          while (ReadVarint(packed, &v))
            info->add_capabilities(static_cast<AccountInfo::Capability>(v));
        } else if (!SkipField(ai, w2)) {
          return false;  // current_dusi (field 1) skipped: not in the core type
        }
      }
    } else if (!SkipField(in, w)) {
      return false;
    }
  }
  return true;
}

}  // namespace google::nearby::identity::v1::wire

#endif  // LOCATION_NEARBY_SHARING_LIB_RPC_IDENTITY_RPC_WIRE_H_
