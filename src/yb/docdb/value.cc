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

const MonoDelta Value::kMaxTtl = yb::common::kMaxTtl;
const MonoDelta Value::kResetTtl = MonoDelta::FromNanoseconds(0);
const int64_t Value::kInvalidUserTimestamp = yb::common::kInvalidUserTimestamp;

template <typename T>
bool DecodeType(ValueType expected_value_type, const T& default_value, Slice* slice,
                T* val) {
  if (!slice->TryConsumeByte(static_cast<char>(expected_value_type))) {
    *val = default_value;
    return false;
  }

  return true;
}

CHECKED_STATUS Value::DecodeMergeFlags(Slice* slice, uint64_t* merge_flags) {
  if (DecodeType(ValueType::kMergeFlags, (uint64_t) 0, slice, merge_flags)) {
    *merge_flags = VERIFY_RESULT(util::FastDecodeUnsignedVarInt(slice));
  }
  return Status::OK();
}

CHECKED_STATUS Value::DecodeMergeFlags(const rocksdb::Slice& slice, uint64_t* merge_flags) {
  rocksdb::Slice value_copy = slice;
  return DecodeMergeFlags(&value_copy, merge_flags);
}

CHECKED_STATUS DecodeIntentDocHT(Slice* slice, DocHybridTime* doc_ht) {
  if (!DecodeType(ValueType::kHybridTime, DocHybridTime::kInvalid, slice, doc_ht)) {
    return Status::OK();
  }
  return doc_ht->DecodeFrom(slice);
}

Status Value::DecodeTTL(rocksdb::Slice* slice, MonoDelta* ttl) {
  if (DecodeType(ValueType::kTtl, kMaxTtl, slice, ttl)) {
    *ttl = MonoDelta::FromMilliseconds(VERIFY_RESULT(util::FastDecodeSignedVarIntUnsafe(slice)));
  }
  return Status::OK();
}

Status Value::DecodeTTL(const rocksdb::Slice& rocksdb_value, MonoDelta* ttl) {
  rocksdb::Slice value_copy = rocksdb_value;
  uint64_t merge_flags;
  RETURN_NOT_OK(DecodeMergeFlags(&value_copy, &merge_flags));
  return DecodeTTL(&value_copy, ttl);
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

Status Value::DecodeControlFields(Slice* slice) {
  if (slice->empty()) {
    return STATUS(Corruption, "Cannot decode a value from an empty slice");
  }

  Slice original = *slice;
  RETURN_NOT_OK_PREPEND(
      DecodeMergeFlags(slice, &merge_flags_),
      Format("Failed to decode merge flags in $0", original.ToDebugHexString()));
  RETURN_NOT_OK_PREPEND(
      DecodeIntentDocHT(slice, &intent_doc_ht_),
      Format("Failed to decode intent ht in $0", original.ToDebugHexString()));
  RETURN_NOT_OK_PREPEND(
      DecodeTTL(slice, &ttl_),
      Format("Failed to decode TTL in $0", original.ToDebugHexString()));
  RETURN_NOT_OK_PREPEND(
      DecodeUserTimestamp(slice, &user_timestamp_),
      Format("Failed to decode user timestamp in $0", original.ToDebugHexString()));
  return Status::OK();
}

Status Value::Decode(const Slice& rocksdb_value) {
  Slice slice = rocksdb_value;
  RETURN_NOT_OK(DecodeControlFields(&slice));
  RETURN_NOT_OK_PREPEND(
      primitive_value_.DecodeFromValue(slice),
      Format("Failed to decode value in $0", rocksdb_value.ToDebugHexString()));
  return Status::OK();
}

std::string Value::ToString() const {
  std::string result = primitive_value_.ToString();
  if (merge_flags_) {
    result += Format("; merge flags: $0", merge_flags_);
  }
  if (intent_doc_ht_.is_valid()) {
    result += Format("; intent doc ht: $0", intent_doc_ht_);
  }
  if (!ttl_.Equals(kMaxTtl)) {
    result += Format("; ttl: $0", ttl_);
  }
  if (user_timestamp_ != kInvalidUserTimestamp) {
    result += Format("; user timestamp: $0", user_timestamp_);
  }
  return result;
}

std::string Value::DebugSliceToString(const Slice& encoded_value) {
  Value value;
  auto status = value.Decode(encoded_value);
  if (!status.ok()) {
    return status.ToString();
  }

  return value.ToString();
}

std::string Value::Encode(const Slice* external_value) const {
  std::string result;
  EncodeAndAppend(&result, external_value);
  return result;
}

void Value::EncodeAndAppend(std::string *value_bytes, const Slice* external_value) const {
  if (merge_flags_) {
    value_bytes->push_back(ValueTypeAsChar::kMergeFlags);
    util::FastAppendUnsignedVarIntToStr(merge_flags_, value_bytes);
  }
  if (intent_doc_ht_.is_valid()) {
    value_bytes->push_back(ValueTypeAsChar::kHybridTime);
    intent_doc_ht_.AppendEncodedInDocDbFormat(value_bytes);
  }
  if (!ttl_.Equals(kMaxTtl)) {
    value_bytes->push_back(ValueTypeAsChar::kTtl);
    util::FastAppendSignedVarIntToBuffer(ttl_.ToMilliseconds(), value_bytes);
  }
  if (user_timestamp_ != kInvalidUserTimestamp) {
    value_bytes->push_back(ValueTypeAsChar::kUserTimestamp);
    util::AppendBigEndianUInt64(user_timestamp_, value_bytes);
  }
  if (!external_value) {
    value_bytes->append(primitive_value_.ToValue());
  } else {
    value_bytes->append(external_value->cdata(), external_value->size());
  }
}

Status Value::DecodePrimitiveValueType(
    const rocksdb::Slice& rocksdb_value,
    ValueType* value_type,
    uint64_t* merge_flags,
    MonoDelta* ttl,
    int64_t* user_ts) {
  auto slice_copy = rocksdb_value;
  uint64_t local_merge_flags;
  DocHybridTime local_doc_ht;
  MonoDelta local_ttl;
  int64_t local_user_ts;
  RETURN_NOT_OK(DecodeMergeFlags(&slice_copy, merge_flags ? merge_flags : &local_merge_flags));
  RETURN_NOT_OK(DecodeIntentDocHT(&slice_copy, &local_doc_ht));
  RETURN_NOT_OK(DecodeTTL(&slice_copy, ttl ? ttl : &local_ttl));
  RETURN_NOT_OK(DecodeUserTimestamp(&slice_copy, user_ts ? user_ts : &local_user_ts));
  *value_type = DecodeValueType(slice_copy);
  return Status::OK();
}

const Value& Value::Tombstone() {
  static const auto kTombstone = Value(PrimitiveValue::kTombstone);
  return kTombstone;
}

const string& Value::EncodedTombstone() {
  static const string kEncodedTombstone = Tombstone().Encode();
  return kEncodedTombstone;
}

void Value::ClearIntentDocHt() {
  intent_doc_ht_ = DocHybridTime::kInvalid;
}

Result<bool> Value::IsTombstoned(const Slice& slice) {
  Value doc_value;
  Slice value = slice;
  RETURN_NOT_OK(doc_value.DecodeControlFields(&value));
  return value[0] == ValueTypeAsChar::kTombstone;
}

}  // namespace docdb
}  // namespace yb
