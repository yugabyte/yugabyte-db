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

#include "yb/docdb/value.h"

#include <string>

#include "yb/common/table_properties_constants.h"

#include "yb/docdb/value_type.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/util/fast_varint.h"
#include "yb/util/kv_util.h"
#include "yb/util/result.h"

namespace yb {
namespace docdb {

using std::string;
using strings::Substitute;

const MonoDelta ValueControlFields::kMaxTtl = common::kMaxTtl;
const MonoDelta ValueControlFields::kResetTtl = MonoDelta::FromNanoseconds(0);
const int64_t ValueControlFields::kInvalidUserTimestamp = common::kInvalidUserTimestamp;

template <typename T>
bool DecodeType(KeyEntryType expected_value_type, const T& default_value, Slice* slice, T* val) {
  if (!slice->TryConsumeByte(static_cast<char>(expected_value_type))) {
    *val = default_value;
    return false;
  }

  return true;
}

namespace {

Result<uint64_t> DecodeMergeFlags(Slice* slice) {
  if (slice->TryConsumeByte(KeyEntryTypeAsChar::kMergeFlags)) {
    return util::FastDecodeUnsignedVarInt(slice);
  } else {
    return 0;
  }
}

Result<DocHybridTime> DecodeIntentDocHT(Slice* slice) {
  if (slice->TryConsumeByte(KeyEntryTypeAsChar::kHybridTime)) {
    return DocHybridTime::DecodeFrom(slice);
  } else {
    return DocHybridTime::kInvalid;
  }
}

Result<MonoDelta> DecodeTTL(Slice* slice) {
  if (slice->TryConsumeByte(KeyEntryTypeAsChar::kTtl)) {
    return MonoDelta::FromMilliseconds(VERIFY_RESULT(util::FastDecodeSignedVarIntUnsafe(slice)));
  } else {
    return ValueControlFields::kMaxTtl;
  }
}

Result<UserTimeMicros> DecodeUserTimestamp(Slice* slice) {
  if (slice->TryConsumeByte(KeyEntryTypeAsChar::kUserTimestamp)) {
    static constexpr int kBytesPerInt64 = sizeof(int64_t);
    if (slice->size() < kBytesPerInt64) {
      return STATUS(Corruption, Substitute(
          "Failed to decode TTL from value, size too small: $1, need $2",
          slice->size(), kBytesPerInt64));
    }

    slice->remove_prefix(kBytesPerInt64);
    return BigEndian::Load64(slice->data() - kBytesPerInt64);
  } else {
    return ValueControlFields::kInvalidUserTimestamp;
  }
}

/*Status Value::DecodeTTL(const rocksdb::Slice& rocksdb_value, MonoDelta* ttl) {
  rocksdb::Slice value_copy = rocksdb_value;
  uint64_t merge_flags;
  RETURN_NOT_OK(DecodeMergeFlags(&value_copy, &merge_flags));
  return DecodeTTL(&value_copy, ttl);
}

Result<UserTimeMicros> DecodeUserTimestamp(const Slice& rocksdb_value) {
  MonoDelta ttl;
  auto slice_copy = rocksdb_value;
  RETURN_NOT_OK(DecodeTTL(&slice_copy, &ttl));
  return DecodeUserTimestamp(&slice_copy, user_timestamp);
}*/

} // namespace

Result<ValueControlFields> ValueControlFields::Decode(Slice* slice) {
  Slice original = *slice;
  ValueControlFields result = {
    .merge_flags = VERIFY_RESULT_PREPEND(
        DecodeMergeFlags(slice),
        Format("Failed to decode merge flags in $0", original.ToDebugHexString())),
    .intent_doc_ht = VERIFY_RESULT_PREPEND(
        DecodeIntentDocHT(slice),
        Format("Failed to decode intent ht in $0", original.ToDebugHexString())),
    .ttl = VERIFY_RESULT_PREPEND(
        DecodeTTL(slice),
        Format("Failed to decode TTL in $0", original.ToDebugHexString())),
    .user_timestamp = VERIFY_RESULT_PREPEND(
        DecodeUserTimestamp(slice),
        Format("Failed to decode user timestamp in $0", original.ToDebugHexString())),
  };
  return result;
}

void ValueControlFields::AppendEncoded(std::string* out) const {
  if (merge_flags) {
    out->push_back(KeyEntryTypeAsChar::kMergeFlags);
    util::FastAppendUnsignedVarIntToStr(merge_flags, out);
  }
  if (intent_doc_ht.is_valid()) {
    out->push_back(KeyEntryTypeAsChar::kHybridTime);
    intent_doc_ht.AppendEncodedInDocDbFormat(out);
  }
  if (!ttl.Equals(kMaxTtl)) {
    out->push_back(KeyEntryTypeAsChar::kTtl);
    util::FastAppendSignedVarIntToBuffer(ttl.ToMilliseconds(), out);
  }
  if (user_timestamp != kInvalidUserTimestamp) {
    out->push_back(KeyEntryTypeAsChar::kUserTimestamp);
    util::AppendBigEndianUInt64(user_timestamp, out);
  }
}

std::string ValueControlFields::ToString() const {
  std::string result;
  if (merge_flags) {
    result += Format("; merge flags: $0", merge_flags);
  }
  if (intent_doc_ht.is_valid()) {
    result += Format("; intent doc ht: $0", intent_doc_ht);
  }
  if (!ttl.Equals(kMaxTtl)) {
    result += Format("; ttl: $0", ttl);
  }
  if (user_timestamp != kInvalidUserTimestamp) {
    result += Format("; user timestamp: $0", user_timestamp);
  }
  return result;
}

Status Value::Decode(const Slice& rocksdb_value, const ValueControlFields& control_fields) {
  Slice slice = rocksdb_value;
  control_fields_ = control_fields;
  RETURN_NOT_OK_PREPEND(
      primitive_value_.DecodeFromValue(slice),
      Format("Failed to decode value in $0", rocksdb_value.ToDebugHexString()));
  return Status::OK();
}

Status Value::Decode(const Slice& rocksdb_value) {
  Slice slice = rocksdb_value;
  control_fields_ = VERIFY_RESULT(ValueControlFields::Decode(&slice));
  RETURN_NOT_OK_PREPEND(
      primitive_value_.DecodeFromValue(slice),
      Format("Failed to decode value in $0", rocksdb_value.ToDebugHexString()));
  return Status::OK();
}

std::string Value::ToString() const {
  return primitive_value_.ToString() + control_fields_.ToString();
}

std::string Value::DebugSliceToString(const Slice& encoded_value) {
  Value value;
  auto status = value.Decode(encoded_value);
  if (!status.ok()) {
    return status.ToString();
  }

  return value.ToString();
}

Result<ValueEntryType> Value::DecodePrimitiveValueType(
    const Slice& rocksdb_value) {
  auto slice_copy = rocksdb_value;
  RETURN_NOT_OK(ValueControlFields::Decode(&slice_copy));
  return DecodeValueEntryType(slice_copy);
}

const Value& Value::Tombstone() {
  static const auto kTombstone = Value(PrimitiveValue::kTombstone);
  return kTombstone;
}

const string& Value::EncodedTombstone() {
  static const string kEncodedTombstone(1, ValueEntryTypeAsChar::kTombstone);
  return kEncodedTombstone;
}

Result<bool> Value::IsTombstoned(const Slice& slice) {
  return VERIFY_RESULT(DecodePrimitiveValueType(slice)) == ValueEntryType::kTombstone;
}

}  // namespace docdb
}  // namespace yb
