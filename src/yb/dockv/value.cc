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

#include "yb/dockv/value.h"

#include <string>

#include "yb/common/table_properties_constants.h"

#include "yb/dockv/value_type.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/util/fast_varint.h"
#include "yb/util/kv_util.h"
#include "yb/util/result.h"

namespace yb::dockv {

using std::string;

const MonoDelta ValueControlFields::kMaxTtl = common::kMaxTtl;
const MonoDelta ValueControlFields::kResetTtl = MonoDelta::FromNanoseconds(0);
const int64_t ValueControlFields::kInvalidTimestamp = common::kInvalidTimestamp;

template <typename T>
bool DecodeType(KeyEntryType expected_value_type, const T& default_value, Slice* slice, T* val) {
  if (!slice->TryConsumeByte(static_cast<char>(expected_value_type))) {
    *val = default_value;
    return false;
  }

  return true;
}

namespace {

Result<UserTimeMicros> DecodeTimestamp(Slice* slice) {
  static constexpr int kBytesPerInt64 = sizeof(int64_t);
  RSTATUS_DCHECK_GE(
      slice->size(), kBytesPerInt64, Corruption,
      Format("Failed to decode TTL from value, size too small: $1, need $2",
      slice->size(), kBytesPerInt64));

  auto result = BigEndian::Load64(slice->data());
  slice->remove_prefix(kBytesPerInt64);
  return result;
}

KeyEntryType GetKeyEntryType(const Slice& slice) {
  return !slice.empty() ? static_cast<KeyEntryType>(slice[0]) : KeyEntryType::kInvalid;
}

void Assign(Slice* intent_doc_ht, const Slice& value) {
  if (intent_doc_ht) {
    *intent_doc_ht = value;
  } else {
    LOG(DFATAL) << "intent_doc_ht should not be null";
  }
}

void Assign(std::nullptr_t intent_doc_ht, const Slice& value) {
}

// Use template to avoid superfluous null check when intent_doc_ht already nullptr.
template <class IntentDocHt>
Result<ValueControlFields> DecodeControlFields(Slice* slice, IntentDocHt intent_doc_ht) {
  Slice original = *slice;
  ValueControlFields result;
  if (slice->empty()) {
    return result;
  }
  auto entry_type = static_cast<KeyEntryType>((*slice)[0]);
  if (entry_type == KeyEntryType::kMergeFlags) {
    slice->consume_byte();
    result.merge_flags = VERIFY_RESULT_PREPEND(
        FastDecodeUnsignedVarInt(slice),
        Format("Failed to decode merge flags in $0", original.ToDebugHexString()));
    entry_type = GetKeyEntryType(*slice);
  }
  if (entry_type == KeyEntryType::kHybridTime) {
    slice->consume_byte();
    Assign(intent_doc_ht, VERIFY_RESULT_PREPEND(
        DocHybridTime::EncodedFromStart(slice),
        Format("Failed to decode intent ht in $0", original.ToDebugHexString())));
    entry_type = GetKeyEntryType(*slice);
  }
  if (entry_type == KeyEntryType::kTtl) {
    slice->consume_byte();
    result.ttl = MonoDelta::FromMilliseconds(VERIFY_RESULT_PREPEND(
        FastDecodeSignedVarIntUnsafe(slice),
        Format("Failed to decode TTL in $0", original.ToDebugHexString())));
    entry_type = GetKeyEntryType(*slice);
  }
  if (entry_type == KeyEntryType::kUserTimestamp) {
    slice->consume_byte();
    result.timestamp = VERIFY_RESULT_PREPEND(
        DecodeTimestamp(slice),
        Format("Failed to decode user timestamp in $0", original.ToDebugHexString()));
    entry_type = GetKeyEntryType(*slice);
  }
  return result;
}

template <class Out>
void DoAppendEncoded(const ValueControlFields& fields, Out* out) {
  if (fields.merge_flags) {
    out->push_back(KeyEntryTypeAsChar::kMergeFlags);
    FastAppendUnsignedVarInt(fields.merge_flags, out);
  }
  if (!fields.ttl.Equals(ValueControlFields::kMaxTtl)) {
    out->push_back(KeyEntryTypeAsChar::kTtl);
    FastAppendSignedVarIntToBuffer(fields.ttl.ToMilliseconds(), out);
  }
  if (fields.timestamp != ValueControlFields::kInvalidTimestamp) {
    out->push_back(KeyEntryTypeAsChar::kUserTimestamp);
    util::AppendBigEndianUInt64(fields.timestamp, out);
  }
}

} // namespace

Result<ValueControlFields> ValueControlFields::Decode(Slice* slice) {
  return DecodeControlFields(slice, nullptr);
}

Result<ValueControlFields> ValueControlFields::DecodeWithIntentDocHt(
    Slice* slice, Slice* intent_doc_ht) {
  return DecodeControlFields(slice, intent_doc_ht);
}

void ValueControlFields::AppendEncoded(std::string* out) const {
  DoAppendEncoded(*this, out);
}

void ValueControlFields::AppendEncoded(ValueBuffer* out) const {
  DoAppendEncoded(*this, out);
}

std::string ValueControlFields::ToString() const {
  std::string result;
  if (merge_flags) {
    result += Format("; merge flags: $0", merge_flags);
  }
  if (!ttl.Equals(kMaxTtl)) {
    result += Format("; ttl: $0", ttl);
  }
  if (timestamp != kInvalidTimestamp) {
    result += Format("; timestamp: $0", timestamp);
  }
  return result;
}

Status Value::Decode(const Slice& rocksdb_value, const ValueControlFields& control_fields) {
  control_fields_ = control_fields;
  RETURN_NOT_OK_PREPEND(
      primitive_value_.DecodeFromValue(rocksdb_value),
      Format("Failed to decode value after control fields $0 in $1",
             control_fields, rocksdb_value.ToDebugHexString()));
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

bool IsFullRowValue(Slice value) {
  if (value.empty()) {
    return false;
  }
  return GetPackedRowVersion(static_cast<dockv::ValueEntryType>(value[0])).has_value();
}

Status UnexpectedPackedRowVersionStatus(PackedRowVersion version) {
  return STATUS_FORMAT(Corruption, "Unexpected packed row version: $0", version);
}

}  // namespace yb::dockv
