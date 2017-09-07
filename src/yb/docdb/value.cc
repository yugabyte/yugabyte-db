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

Status Value::DecodeTTL(rocksdb::Slice* slice, MonoDelta* ttl) {

  const ValueType value_type = DecodeValueType(*slice);

  if (value_type != ValueType::kTtl) {
    *ttl = kMaxTtl;
    return Status::OK();
  }

  ConsumeValueType(slice);

  if (slice->size() < kBytesPerTtl) {
    return STATUS(Corruption, Substitute(
        "Failed to decode TTL from value, size too small: $0, need $1",
        slice->size(), kBytesPerTtl));
  }
  *ttl = MonoDelta::FromMilliseconds(BigEndian::Load64(slice->data()));
  slice->remove_prefix(kBytesPerTtl);
  return Status::OK();
}

Status Value::Decode(const rocksdb::Slice& rocksdb_value) {
  if (rocksdb_value.empty()) {
    return STATUS(Corruption, "Cannot decode a value from an empty slice");
  }

  rocksdb::Slice slice = rocksdb_value;

  RETURN_NOT_OK(DecodeTTL(&slice, &ttl_));
  return primitive_value_.DecodeFromValue(slice);
}

string Value::ToString() const {
  if (!ttl_.Equals(kMaxTtl)) {
    return primitive_value_.ToString() + "; ttl: " + ttl_.ToString();
  }
  return primitive_value_.ToString();
}

string Value::Encode() const {
  string result;
  EncodeAndAppend(&result);
  return result;
}

void Value::EncodeAndAppend(std::string *value_bytes) const {
  if (!ttl_.Equals(kMaxTtl)) {
    value_bytes->push_back(static_cast<char>(ValueType::kTtl));
    AppendBigEndianUInt64(ttl_.ToMilliseconds(), value_bytes);
  }
  value_bytes->append(primitive_value_.ToValue());
}

}  // namespace docdb
}  // namespace yb
