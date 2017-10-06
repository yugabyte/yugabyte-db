// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include <string>

#include "yb/common/table_properties_constants.h"
#include "yb/docdb/value.h"
#include "yb/gutil/strings/substitute.h"

namespace yb {
namespace docdb {

using std::string;
using strings::Substitute;

const MonoDelta Value::kMaxTtl = yb::common::kMaxTtl;
const int64_t Value::kInvalidUserTimestamp = yb::common::kInvalidUserTimestamp;

template <typename T>
bool Value::DecodeType(const ValueType& expected_value_type, const T& default_value,
                       rocksdb::Slice* slice, T* val) {
  const ValueType value_type = DecodeValueType(*slice);

  if (value_type != expected_value_type) {
    *val = default_value;
    return false;
  }

  ConsumeValueType(slice);
  return true;
}

Status Value::DecodeTTL(rocksdb::Slice* slice, MonoDelta* ttl) {
  if (DecodeType(ValueType::kTtl, kMaxTtl, slice, ttl)) {
    int64_t val;
    int decoded_size;
    RETURN_NOT_OK(yb::util::FastDecodeSignedVarInt(slice->data(), slice->size(),
                                                   &val, &decoded_size));
    slice->remove_prefix(decoded_size);
    *ttl = MonoDelta::FromMilliseconds(val);
  }
  return Status::OK();
}

Status Value::DecodeUserTimestamp(const rocksdb::Slice& rocksdb_value,
                                  UserTimeMicros* user_timestamp) {
  MonoDelta ttl;
  auto slice_copy = rocksdb_value;
  RETURN_NOT_OK(DecodeTTL(&slice_copy, &ttl));
  return DecodeUserTimestamp(&slice_copy, user_timestamp);
}

Status Value::DecodeUserTimestamp(rocksdb::Slice* slice, UserTimeMicros* user_timestamp) {
  if (DecodeType(ValueType::kUserTimestamp, kInvalidUserTimestamp, slice,
                 user_timestamp)) {
    if (slice->size() < kBytesPerInt64) {
      return STATUS(Corruption, Substitute(
          "Failed to decode TTL from value, size too small: $1, need $2",
          slice->size(), kBytesPerInt64));
    }

    *user_timestamp = BigEndian::Load64(slice->data());
    slice->remove_prefix(kBytesPerInt64);
  }
  return Status::OK();
}

Status Value::Decode(const rocksdb::Slice& rocksdb_value) {
  if (rocksdb_value.empty()) {
    return STATUS(Corruption, "Cannot decode a value from an empty slice");
  }

  rocksdb::Slice slice = rocksdb_value;

  RETURN_NOT_OK(DecodeTTL(&slice, &ttl_));
  RETURN_NOT_OK(DecodeUserTimestamp(&slice, &user_timestamp_));
  return primitive_value_.DecodeFromValue(slice);
}

string Value::ToString() const {
  string to_string = primitive_value_.ToString();
  if (!ttl_.Equals(kMaxTtl)) {
    to_string += "; ttl: " + ttl_.ToString();
  }
  if (user_timestamp_ != kInvalidUserTimestamp) {
    to_string += "; user_timestamp: " + std::to_string(user_timestamp_);
  }
  return to_string;
}

string Value::Encode() const {
  string result;
  EncodeAndAppend(&result);
  return result;
}

void Value::EncodeAndAppend(std::string *value_bytes) const {
  if (!ttl_.Equals(kMaxTtl)) {
    value_bytes->push_back(static_cast<char>(ValueType::kTtl));
    yb::util::FastAppendSignedVarIntToStr(ttl_.ToMilliseconds(), value_bytes);
  }
  if (user_timestamp_ != kInvalidUserTimestamp) {
    value_bytes->push_back(static_cast<char>(ValueType::kUserTimestamp));
    AppendBigEndianUInt64(user_timestamp_, value_bytes);
  }
  value_bytes->append(primitive_value_.ToValue());
}

Status Value::DecodePrimitiveValueType(const rocksdb::Slice& rocksdb_value,
                                       ValueType* value_type) {
  MonoDelta ttl;
  int64_t user_timestamp;
  auto slice_copy = rocksdb_value;
  RETURN_NOT_OK(DecodeTTL(&slice_copy, &ttl));
  RETURN_NOT_OK(DecodeUserTimestamp(&slice_copy, &user_timestamp));
  *value_type = DecodeValueType(slice_copy);
  return Status::OK();
}

}  // namespace docdb
}  // namespace yb
