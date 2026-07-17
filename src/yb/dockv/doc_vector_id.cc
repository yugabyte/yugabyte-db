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

#include "yb/dockv/doc_vector_id.h"

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/ql_value.h"
#include "yb/common/value.messages.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/key_entry_value.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/value_packing_v2.h"
#include "yb/dockv/value_type.h"

namespace yb::dockv {

namespace {

// Vector Id encoding format:
// |-----------------------------------------------------|
// |                  encoded vector id                  |
// |-----------------------------------------------------|
// |          vector id value          |     size of     |
// |-----------------------------------|    vector id    |
// |   value type    |    vector id    |      value      |
// |-----------------------------------------------------|
// |     1 byte      | kUuidSize bytes |     1 byte      |
// |-----------------------------------------------------|

constexpr const size_t kEncodedVectorIdValueSize = 1 + kUuidSize;
constexpr const size_t kEncodedVectorIdSize = kEncodedVectorIdValueSize + 1;

constexpr const char kDocVectorMetaValueFormatVersion = 0x01;

// kVectorIndexMetadata (1 byte) + format version (1 byte).
constexpr const size_t kEncodedDocVectorMetaValueHeaderSize =
    1 + sizeof(kDocVectorMetaValueFormatVersion);
static_assert(kEncodedDocVectorMetaValueHeaderSize == 2);

char* GrowAtLeast(std::string* buffer, size_t size) {
  const auto current_size = buffer->size();
  buffer->resize(current_size + size);
  return buffer->data() + current_size;
}

char* GrowAtLeast(ValueBuffer* buffer, size_t size) {
  return buffer->GrowByAtLeast(size);
}

} // namespace

EncodedDocVectorValue EncodedDocVectorValue::FromSlice(Slice encoded) {
  if (encoded.empty()) {
    return {};
  }

  Slice original_encoded = encoded;

  // The last byte in the encoded value is mandatory. It contains the size of the encoded vector id
  // chunk. Having 0 in encoded vector id size means vector id is not specified.
  const auto vector_id_value_size = static_cast<uint8_t>(encoded.consume_byte_back());
  if (vector_id_value_size == 0) {
    return { .data = encoded, .id = {} };
  }

  CHECK_EQ(vector_id_value_size, kEncodedVectorIdValueSize)
      << "Source: " << original_encoded.ToDebugHexString();
  CHECK_LT(vector_id_value_size, encoded.size())
      << "Source: " << original_encoded.ToDebugHexString();
  auto id = encoded.Suffix(vector_id_value_size);

  CHECK_EQ(dockv::ConsumeValueEntryType(&id), dockv::ValueEntryType::kVectorId)
      << "Source: " << original_encoded.ToDebugHexString();
  return { .data = encoded.WithoutSuffix(vector_id_value_size), .id = id };
}

Result<vector_index::VectorId> EncodedDocVectorValue::DecodeId() const {
  return vector_index::FullyDecodeVectorId(id);
}

Result<EncodedDocVectorMetaValue> EncodedDocVectorMetaValue::Decode(Slice encoded) {
  if (encoded.empty()) {
    return EncodedDocVectorMetaValue{};
  }

  // Return tombstone or legacy value.
  if (encoded.starts_with(ValueEntryTypeAsChar::kTombstone) ||
      !encoded.starts_with(KeyEntryTypeAsChar::kVectorIndexMetadata)) {
    SCHECK(!encoded.starts_with(ValueEntryTypeAsChar::kTombstone) || encoded.size() == 1,
           Corruption, "kTombstone should have no extra data");
    return EncodedDocVectorMetaValue { .table_key_prefix = {}, .ybctid = encoded };
  }

  // Parse new format value.
  Slice original_encoded = encoded;

  // Drop kVectorIndexMetadata, which is guaranteed by the check above.
  encoded.consume_byte();

  SCHECK(encoded.TryConsumeByte(kDocVectorMetaValueFormatVersion), Corruption,
         Format("Expected v1 vector meta value format, got: $0",
                original_encoded.ToDebugHexString()));

  const auto sizes = VERIFY_RESULT(DocKey::EncodedPrefixAndDocKeySizes(encoded));
  SCHECK_GE(encoded.size(), sizes.doc_key_size, Corruption,
            Format("Truncated doc key in vector meta value: $0",
                   original_encoded.ToDebugHexString()));

  auto table_key_prefix = encoded.Prefix(sizes.prefix_size);
  auto ybctid = encoded.Prefix(sizes.doc_key_size).WithoutPrefix(sizes.prefix_size);
  encoded.remove_prefix(sizes.doc_key_size);

  SCHECK(encoded.starts_with(KeyEntryTypeAsChar::kColumnId), Corruption,
         Format("Expected column id subkey in vector meta value: $0",
                original_encoded.ToDebugHexString()));
  encoded.consume_byte();
  ColumnId column_id = VERIFY_RESULT(ColumnId::Decode(&encoded));
  SCHECK(encoded.empty(), Corruption,
         Format("Trailing bytes in vector meta value: $0",
                original_encoded.ToDebugHexString()));

  return EncodedDocVectorMetaValue {
    .table_key_prefix = table_key_prefix,
    .ybctid = ybctid,
    .column_id = column_id,
  };
}

template <class Buffer>
void DocVectorValue::AppendVectorId(Buffer* buffer) const {
  char* out = GrowAtLeast(buffer, kEncodedVectorIdSize);

  *out = ValueEntryTypeAsChar::kVectorId;
  ++out;
  id_.GetUuid().ToBytes(out);
  out += kUuidSize;
  *out = kEncodedVectorIdValueSize;
}

template <class Buffer>
void DocVectorValue::AppendEncodedVectorValue(Buffer* buffer) const {
  if (IsNull()) {
    return AppendEncodedNullValue(buffer);
  }

  AppendEncodedBinaryValue(value_type_prefix_, value_, buffer);
  AppendVectorId(buffer);
}

void DocVectorValue::EncodeTo(std::string* buffer) const {
  AppendEncodedVectorValue(buffer);
}

char DocVectorValue::ValueTypePrefix(VectorValueFormat format) {
  return format == VectorValueFormat::kTyped
      ? ValueEntryTypeAsChar::kVector : ValueEntryTypeAsChar::kString;
}

Slice DocVectorValue::SanitizeValue(Slice encoded) {
  return EncodedDocVectorValue::FromSlice(encoded).data;
}

Result<QLValuePB> EncodeVectorSchemaMissingValue(
    const QLValuePB& raw_pgvector_value, VectorValueFormat format) {
  QLValuePB result;
  if (IsNull(raw_pgvector_value)) {
    return result;
  }
  SCHECK_EQ(
      raw_pgvector_value.value_case(), QLValuePB::kBinaryValue, InvalidArgument,
      "Value calue should be QLValuePB::kBinaryValue");

  const char prefix = format == VectorValueFormat::kTyped
      ? ValueEntryTypeAsChar::kVector : ValueEntryTypeAsChar::kString;
  std::string encoded;
  AppendEncodedBinaryValue(prefix, raw_pgvector_value, &encoded);
  encoded.push_back(char{0});
  result.set_binary_value(std::move(encoded));
  return result;
}

Result<QLValuePB> DecodeVectorSchemaMissingValueForPgRow(const QLValuePB& docdb_missing_value) {
  if (IsNull(docdb_missing_value)) {
    return docdb_missing_value;
  }
  SCHECK_EQ(
      docdb_missing_value.value_case(), QLValuePB::kBinaryValue, InvalidArgument,
      Format(
          "Value case should be QLValuePB::kBinaryValue, instead it is $0",
          docdb_missing_value.value_case()));

  const auto& encoded = docdb_missing_value.binary_value();
  if (encoded.empty()) {
    return docdb_missing_value;
  }

  const auto value_type = static_cast<ValueEntryType>(encoded[0]);
  SCHECK_FORMAT(
      value_type == ValueEntryType::kVector || value_type == ValueEntryType::kString,
      InvalidArgument, "Value entry type should be kVector or kString, instead it is $0",
      value_type);
  if (value_type != ValueEntryType::kVector && value_type != ValueEntryType::kString) {
    return docdb_missing_value;
  }

  auto sanitized = DocVectorValue::SanitizeValue(encoded);
  Slice slice = sanitized;
  ConsumeValueEntryType(&slice);

  QLValuePB result;
  std::string pg_row_value(slice.cdata(), slice.size());
  pg_row_value.push_back(char{0});
  result.set_binary_value(std::move(pg_row_value));
  return result;
}

std::string DocVectorValue::ToString() const {
  return YB_CLASS_TO_STRING(value, id);
}

bool IsNull(const DocVectorValue& v) {
  return v.IsNull();
}

bool DocVectorValue::IsNull() const {
  return yb::IsNull(value_);
}

size_t DocVectorValue::PackedSizeV1() const {
  return EncodedValueSize(value_) + (IsNull() ? 0 : kEncodedVectorIdSize);
}

void DocVectorValue::PackToV1(ValueBuffer* buffer) const {
  AppendEncodedVectorValue(buffer);
}

size_t DocVectorValue::PackedSizeV2() const {
  return PackedQLValueSizeV2(value_, DataType::VECTOR) + (IsNull() ? 0 : kEncodedVectorIdSize);
}

void DocVectorValue::PackToV2(ValueBuffer* buffer) const {
  PackQLValueV2(value_, DataType::VECTOR, buffer);
  if (!IsNull()) {
    AppendVectorId(buffer);
  }
}

namespace {

// Vector Key is used for reverse entries only with the following format:
// |----------------------------------------------------------------------|
// |                       encoded full vector key                        |
// |----------------------------------------------------------------------|
// |               encoded vector id key                |                 |
// |----------------------------------------------------------------------|
// | kVectorIndexMetadata | kVectorId |    vector id    | doc hybrid time |
// |----------------------------------------------------------------------|
// |        1 byte        |  1 byte   | kUuidSize bytes |     N bytes     |
// |----------------------------------------------------------------------|

constexpr std::array<char, 2> kVectorIdKeyPrefix =
    { dockv::KeyEntryTypeAsChar::kVectorIndexMetadata, dockv::KeyEntryTypeAsChar::kVectorId };

std::string FormatVectorKey(const vector_index::VectorId& id, const std::string& ht) {
  return Format("MetaKey($0, [$1])", DocVectorIdToString(id), ht);
}

} // namespace

KeyBytes DocVectorKey(vector_index::VectorId vector_id) {
  KeyBytes key;
  key.AppendRawBytes(Slice(kVectorIdKeyPrefix));
  key.AppendRawBytes(vector_id.AsSlice());
  return key;
}

std::array<Slice, 3> DocVectorKeyAsParts(Slice id, Slice encoded_write_time) {
  return std::array<Slice, 3>{ Slice(kVectorIdKeyPrefix), id, encoded_write_time };
}

Status DecodeDocVectorKey(Slice* input, vector_index::VectorId* vector_id) {
  RETURN_NOT_OK(input->consume_byte(dockv::KeyEntryTypeAsChar::kVectorIndexMetadata));
  RETURN_NOT_OK(input->consume_byte(dockv::KeyEntryTypeAsChar::kVectorId));
  if (!vector_id) {
    RETURN_NOT_OK(vector_index::DecodeVectorId(input));
  } else {
    *vector_id = VERIFY_RESULT(vector_index::DecodeVectorId(input));
  }
  return Status::OK();
}

Result<vector_index::VectorId> DecodeDocVectorKey(Slice* input) {
  vector_index::VectorId vector_id;
  RETURN_NOT_OK(DecodeDocVectorKey(input, &vector_id));
  return vector_id;
}

Result<size_t> EncodedDocVectorKeySize(Slice key) {
  const auto key_begin = key.data();
  RETURN_NOT_OK(dockv::DecodeDocVectorKey(&key, /* vector_id */ nullptr));
  return key.data() - key_begin;
}

std::string DocVectorIdToString(const Uuid& vector_id) {
  return Format("VectorId($0)", vector_id.ToString());
}

std::string DocVectorIdToString(const vector_index::VectorId& vector_id) {
  return DocVectorIdToString(vector_id.GetUuid());
}

std::string DocVectorKeyToString(const vector_index::VectorId& vector_id) {
  return FormatVectorKey(vector_id, /* ht = */ "");
}

std::string DocVectorKeyToString(const vector_index::VectorId& vector_id, const DocHybridTime& ht) {
  return FormatVectorKey(vector_id, ht.ToString());
}

Result<std::string> DocVectorMetaKeyToString(Slice input) {
  auto vector_id = VERIFY_RESULT(DecodeDocVectorKey(&input));
  auto doc_ht = VERIFY_RESULT_PREPEND(
      DocHybridTime::DecodeFromEnd(input), DocVectorKeyToString(vector_id));
  return DocVectorKeyToString(vector_id, doc_ht);
}

Result<std::string> DocVectorMetaValueToString(Slice value) {
  if (value.starts_with(ValueEntryTypeAsChar::kTombstone)) {
    SCHECK_EQ(value.size(), 1, Corruption, "kTombstone should have no extra data");
    return PrimitiveValue::kTombstone.ToString();
  }

  // V1: kVectorIndexMetadata + version + SubDocKey without HT.
  if (value.starts_with(KeyEntryTypeAsChar::kVectorIndexMetadata)) {
    SCHECK_GE(value.size(), kEncodedDocVectorMetaValueHeaderSize, Corruption,
              "Truncated v1 vector meta value");
    SCHECK_EQ(value[1], kDocVectorMetaValueFormatVersion, Corruption,
              "Unexpected vector meta value format version");
    SubDocKey sub_doc_key;
    RETURN_NOT_OK(sub_doc_key.FullyDecodeFrom(
        value.WithoutPrefix(kEncodedDocVectorMetaValueHeaderSize), HybridTimeRequired::kFalse));
    return sub_doc_key.ToString(AutoDecodeKeys::kTrue);
  }

  // Legacy: raw ybctid (DocKey without coprefix).
  DocKey ybctid;
  RETURN_NOT_OK(ybctid.DecodeFrom(value));
  return ybctid.ToString(AutoDecodeKeys::kTrue);
}

KeyBytes DocVectorMetaValue(Slice table_key_prefix, Slice ybctid, ColumnId column_id) {
  // Sanity checks.
  DCHECK_NE(column_id, kInvalidColumnId) << "Invalid column id";
  DCHECK(!ybctid.empty()) << "Invalid ybctid";
  DCHECK(!ybctid.starts_with(dockv::ValueEntryTypeAsChar::kTombstone));

  // Using key bytes because vector meta value is a row sub doc key.
  KeyBytes key;

  key.AppendKeyEntryType(dockv::KeyEntryType::kVectorIndexMetadata);
  key.AppendRawBytes(Slice(&kDocVectorMetaValueFormatVersion, 1));
  key.AppendRawBytes(table_key_prefix);
  key.AppendRawBytes(ybctid);
  KeyEntryValue::MakeColumnId(column_id).AppendToKey(&key);

  return key;
}

} // namespace yb::dockv
