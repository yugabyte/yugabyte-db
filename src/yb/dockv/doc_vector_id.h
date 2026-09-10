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

#pragma once

#include "yb/common/column_id.h"
#include "yb/common/value.messages.h"

#include "yb/dockv/dockv_fwd.h"
#include "yb/dockv/key_bytes.h"
#include "yb/dockv/packed_row.h"

#include "yb/util/uuid.h"

#include "yb/vector_index/vector_index_fwd.h"


namespace yb::dockv {

// |----------------------------------------------------|
// | kVectorIndexMetadata | kVectorId |    vector id    |
// |----------------------------------------------------|
// |        1 byte        |  1 byte   | kUuidSize bytes |
// |----------------------------------------------------|
// See DecodeDocVectorKey() for the details.
constexpr size_t kEncodedDocVectorKeyStaticSize = 2 + kUuidSize;

struct EncodedDocVectorValue final {
  Slice data;
  Slice id;

  Result<vector_index::VectorId> DecodeId() const;

  static EncodedDocVectorValue FromSlice(Slice encoded);
};

struct EncodedDocVectorMetaValue final {
  // Optional colocation prefix, always empty for tombstone entries.
  Slice table_key_prefix;

  // A tuple without colocation prefix or a tombstone marker.
  Slice ybctid;

  // Optional column id, always invalid for tombstone entries.
  ColumnId column_id = kInvalidColumnId;

  bool IsTombstone() const {
    return ybctid.starts_with(ValueEntryTypeAsChar::kTombstone);
  }

  static Result<EncodedDocVectorMetaValue> Decode(Slice encoded);
};

class DocVectorValue final : public PackableValue {
 public:
  DocVectorValue(
      VectorValueFormat format,
      std::reference_wrapper<const QLValueMsg> value,
      const vector_index::VectorId& id)
      : value_(value), id_(id), value_type_prefix_(ValueTypePrefix(format))
  {}

  bool IsNull() const override;

  void EncodeTo(std::string* out) const;

  size_t PackedSizeV1() const override;
  void PackToV1(ValueBuffer* result) const override;

  size_t PackedSizeV2() const override;
  void PackToV2(ValueBuffer* result) const override;

  const QLValueMsg& value() const {
    return value_;
  }

  static Slice SanitizeValue(Slice encoded);

  std::string ToString() const override;

 private:
  static char ValueTypePrefix(VectorValueFormat format);

  template <class Buffer>
  void AppendEncodedVectorValue(Buffer* buffer) const;

  template <class Buffer>
  void AppendVectorId(Buffer* buffer) const;

  const QLValueMsg& value_;
  vector_index::VectorId id_;
  char value_type_prefix_;
};

bool IsNull(const DocVectorValue& v);

// Encodes a raw pgvector binary value into DocDB format for schema missing_value storage.
// The result has no VectorId suffix (trailing 0 byte).
Result<QLValuePB> EncodeVectorSchemaMissingValue(
    const QLValuePB& raw_pgvector_value, VectorValueFormat format);

// Converts a DocDB-encoded vector schema missing_value into the format used by PgTableRow.
Result<QLValuePB> DecodeVectorSchemaMissingValueForPgRow(const QLValuePB& docdb_missing_value);

KeyBytes DocVectorKey(vector_index::VectorId vector_id);
std::array<Slice, 3> DocVectorKeyAsParts(Slice id, Slice encoded_write_time);

Status DecodeDocVectorKey(Slice* input, vector_index::VectorId* vector_id);
Result<vector_index::VectorId> DecodeDocVectorKey(Slice* input);

Result<size_t> EncodedDocVectorKeySize(Slice key);

std::string DocVectorIdToString(const Uuid& vector_id);
std::string DocVectorIdToString(const vector_index::VectorId& vector_id);

std::string DocVectorKeyToString(const vector_index::VectorId& vector_id);
std::string DocVectorKeyToString(const vector_index::VectorId& vector_id, const DocHybridTime& ht);

Result<std::string> DocVectorMetaKeyToString(Slice input);
Result<std::string> DocVectorMetaValueToString(Slice value);

// Encodes vector reverse entry value in V1 format (table_key_prefix + ybctid + column_id).
KeyBytes DocVectorMetaValue(Slice table_key_prefix, Slice ybctid, ColumnId column_id);

} // namespace yb::dockv
