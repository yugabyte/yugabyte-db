// Copyright (c) YugabyteDB, Inc.
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

#include <limits>
#include <string>

#include "yb/common/table_properties_constants.h"

#include "yb/dockv/value_type.h"


#include "yb/util/fast_varint.h"
#include "yb/util/flags.h"
#include "yb/util/kv_util.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

DEFINE_RUNTIME_PREVIEW_bool(ysql_enable_cdr_metadata, false,
  "Whether to persist the replication origin and source write hybrid time of the last writer "
  "alongside each row version, for conflict detection and resolution in logical replication.");

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

Result<uint64_t> DecodeBigEndianUInt64(Slice* slice, const char* field_name) {
  static constexpr int kBytesPerInt64 = sizeof(int64_t);
  RSTATUS_DCHECK_GE(
      slice->size(), kBytesPerInt64, Corruption,
      Format("Failed to decode $0 from value, size too small: $1, need $2",
      field_name, slice->size(), kBytesPerInt64));

  auto result = BigEndian::Load64(slice->data());
  slice->remove_prefix(kBytesPerInt64);
  return result;
}

Result<UserTimeMicros> DecodeTimestamp(Slice* slice) {
  return DecodeBigEndianUInt64(slice, "user timestamp");
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
  auto entry_type = DecodeKeyEntryType(*slice);
  if (!IsControlFieldMarker(entry_type)) {
    return result;
  }
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
        FastDecodeSignedVarInt(slice),
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
  if (entry_type == KeyEntryType::kCdrMetadata) {
    slice->consume_byte();
    auto origin = VERIFY_RESULT_PREPEND(
        FastDecodeUnsignedVarInt(slice),
        Format("Failed to decode source origin in $0", original.ToDebugHexString()));
    SCHECK_LE(
        origin, std::numeric_limits<uint32_t>::max(), Corruption,
        Format("Source origin out of range in $0", original.ToDebugHexString()));
    result.cdr_source_origin = narrow_cast<uint32_t>(origin);
    result.cdr_source_write_ht = HybridTime(VERIFY_RESULT_PREPEND(
        DecodeBigEndianUInt64(slice, "source write hybrid time"),
        Format("Failed to decode source write hybrid time in $0", original.ToDebugHexString())));
    entry_type = GetKeyEntryType(*slice);
  }
  return result;
}

template <class Out>
void DoAppendEncoded(const ValueControlFields& fields, Out* out) {
  const auto initial_size = out->size();
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
  if (fields.has_cdr_metadata()) {
    out->push_back(KeyEntryTypeAsChar::kCdrMetadata);
    FastAppendUnsignedVarInt(fields.cdr_source_origin, out);
    util::AppendBigEndianUInt64(fields.cdr_source_write_ht.ToUint64(), out);
  }

  DCHECK(
      out->size() == initial_size ||
      IsControlFieldMarker(DecodeKeyEntryType(
          Slice(out->data() + initial_size, out->size() - initial_size))))
      << "Control field encoded with a marker IsControlFieldMarker does not recognise";
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
  if (has_cdr_metadata()) {
    result += Format(
        "; cdr source origin: $0; cdr source write ht: $1", cdr_source_origin, cdr_source_write_ht);
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
