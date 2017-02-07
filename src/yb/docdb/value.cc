// Copyright (c) YugaByte, Inc.

#include <string>

#include "yb/common/ttl_constants.h"
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
  if (!ttl_.Equals(kMaxTtl)) {
    result.push_back(static_cast<char>(ValueType::kTtl));
    AppendBigEndianUInt64(ttl_.ToMilliseconds(), &result);
  }
  result += primitive_value_.ToValue();
  return result;
}

}  // namespace docdb
}  // namespace yb
