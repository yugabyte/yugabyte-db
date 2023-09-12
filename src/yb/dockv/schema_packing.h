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

#pragma once

#include <unordered_map>

#include <boost/container/small_vector.hpp>
#include <boost/functional/hash.hpp>

#include <google/protobuf/repeated_field.h>

#include "yb/common/common_fwd.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/column_id.h"
#include "yb/common/doc_hybrid_time.h"
#include "yb/common/id_mapping.h"

#include "yb/dockv/dockv_fwd.h"
#include "yb/dockv/dockv.fwd.h"
#include "yb/dockv/value_type.h"

#include "yb/util/slice.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb::dockv {

YB_STRONGLY_TYPED_BOOL(OverwriteSchemaPacking);

struct ColumnPackingData {
  ColumnId id;

  // Number of varlen columns before this one.
  size_t num_varlen_columns_before;

  // Offset of this column from previous varlen column. I.e. sum of sizes of fixed length columns
  // after previous varlen column.
  size_t offset_after_prev_varlen_column;

  // Fixed size of this column, 0 if it is varlen column.
  size_t size;

  // Whether column is nullable.
  bool nullable;

  DataType data_type;

  static ColumnPackingData FromPB(const ColumnPackingPB& pb);
  void ToPB(ColumnPackingPB* out) const;

  bool varlen() const {
    return size == 0;
  }

  std::string ToString() const;

  bool operator==(const ColumnPackingData&) const = default;

  Slice FetchV1(const uint8_t* header, const uint8_t* body) const;
};

class SchemaPacking {
 public:
  // Used to mark column as skipped by packer. For instance in case of collection column.
  static constexpr int64_t kSkippedColumnIdx = IdMapping::kNoEntry;

  SchemaPacking(TableType table_type, const Schema& schema);
  explicit SchemaPacking(const SchemaPackingPB& pb);

  size_t columns() const {
    return columns_.size();
  }

  const ColumnPackingData& column_packing_data(size_t idx) const {
    return columns_[idx];
  }

  // Size of prefix before actual data.
  size_t prefix_len() const {
    return varlen_columns_count_ * sizeof(uint32_t);
  }

  size_t NullMaskSize() const {
    return (columns_.size() + 7) / 8;
  }

  size_t varlen_columns_count() const {
    return varlen_columns_count_;
  }

  // Whether this packing has information about data types.
  // true for all quite recent packings.
  bool HasDataType() const;

  bool SkippedColumn(ColumnId column_id) const;
  int64_t GetIndex(ColumnId column_id) const;
  Slice GetValue(size_t idx, Slice packed) const;
  std::optional<Slice> GetValue(ColumnId column_id, Slice packed) const;

  // Fills `bounds` with pointers of all packed columns in row represented by `packed`.
  void GetBounds(
      Slice packed, boost::container::small_vector_base<const uint8_t*>* bounds) const;
  void ToPB(SchemaPackingPB* out) const;

  bool CouldPack(const google::protobuf::RepeatedPtrField<QLColumnValuePB>& values) const;

  std::string ToString() const;

  bool operator==(const SchemaPacking&) const = default;

  bool SchemaContainsPacking(TableType table_type, const Schema& schema) const {
    SchemaPacking packing(table_type, schema);
    return packing == *this;
  }

 private:
  std::vector<ColumnPackingData> columns_;
  IdMapping column_to_idx_;
  size_t varlen_columns_count_;
};

class SchemaPackingStorage {
 public:
  explicit SchemaPackingStorage(TableType table_type);
  SchemaPackingStorage(const SchemaPackingStorage& rhs, SchemaVersion min_schema_version);

  Result<const SchemaPacking&> GetPacking(SchemaVersion schema_version) const;
  Result<const SchemaPacking&> GetPacking(Slice* packed_row) const;
  Result<SchemaVersion> GetSchemaPackingVersion(
      TableType table_type, const Schema& schema) const;

  void AddSchema(SchemaVersion version, const Schema& schema);

  Status LoadFromPB(const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas);
  Status MergeWithRestored(
      SchemaVersion schema_version, const SchemaPB& schema,
      const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas,
      OverwriteSchemaPacking overwrite);

  // Copy all schema packings except schema_version_to_skip to out.
  void ToPB(
      SchemaVersion schema_version_to_skip,
      google::protobuf::RepeatedPtrField<SchemaPackingPB>* out) const;

  size_t SchemaCount() const {
    return version_to_schema_packing_.size();
  }

  std::string VersionsToString() const;

  bool HasVersionBelow(SchemaVersion version) const;

  TableType table_type() const {
    return table_type_;
  }

  std::string ToString() const;

  bool operator==(const SchemaPackingStorage&) const = default;

 private:
  // Set could_present to true when schemas could contain the same packings as already present.
  Status InsertSchemas(
      const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas, bool could_present,
      OverwriteSchemaPacking overwrite);
  static Status InsertSchemas(
      const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas, bool could_present,
      OverwriteSchemaPacking overwrite, std::unordered_map<SchemaVersion, SchemaPacking>* out);

  Result<std::unordered_map<SchemaVersion, SchemaPacking>> GetMergedSchemaPackings(
      SchemaVersion schema_version, const SchemaPB& schema,
      const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas,
      OverwriteSchemaPacking overwrite) const;

  static void AddSchema(
      TableType table_type, SchemaVersion version, const Schema& schema,
      std::unordered_map<SchemaVersion, SchemaPacking>* out);

  TableType table_type_;
  std::unordered_map<SchemaVersion, SchemaPacking> version_to_schema_packing_;
};

class PackedRowDecoderBase {
 public:
  PackedRowDecoderBase() = default;
  explicit PackedRowDecoderBase(std::reference_wrapper<const SchemaPacking> packing)
      : packing_(&packing.get()) {}

  const SchemaPacking& packing() const {
    return *packing_;
  }

 protected:
  const SchemaPacking* packing_;
};

class PackedRowDecoderV1 : public PackedRowDecoderBase {
 public:
  static constexpr PackedRowVersion kVersion = PackedRowVersion::kV1;
  static constexpr ValueEntryType kValueEntryType = ValueEntryType::kPackedRowV1;

  PackedRowDecoderV1() = default;
  PackedRowDecoderV1(
      std::reference_wrapper<const SchemaPacking> packing, const uint8_t* data);

  PackedValueV1 FetchValue(size_t idx);
  PackedValueV1 FetchValue(ColumnId column_id);

  int64_t GetPackedIndex(ColumnId column_id);

  // Returns the pointer to the first byte after the specified column in input data.
  const uint8_t* GetEnd(ColumnId column_id);

 private:
  bool IsNull(size_t idx) const;
  Slice DoGetValue(size_t idx);

  // Memory should be retained by the user of this class.
  const uint8_t* header_ptr_;
  const uint8_t* data_;
};

class PackedRowDecoderV2 : public PackedRowDecoderBase {
 public:
  static constexpr PackedRowVersion kVersion = PackedRowVersion::kV2;
  static constexpr ValueEntryType kValueEntryType = ValueEntryType::kPackedRowV2;

  PackedRowDecoderV2() = default;
  PackedRowDecoderV2(std::reference_wrapper<const SchemaPacking> packing, const uint8_t* data);

  PackedValueV2 FetchValue(size_t idx);
  PackedValueV2 FetchValue(ColumnId column_id);

  int64_t GetPackedIndex(ColumnId column_id);

  // Returns the pointer to the first byte after the specified column in input data.
  const uint8_t* GetEnd(ColumnId column_id);

  static bool IsNull(const uint8_t* header, size_t idx);

 private:
  bool IsNull(size_t idx) const;
  Slice DoGetValue(size_t idx);

  // Memory should be retained by the user of this class.
  const uint8_t* header_ptr_;
  const uint8_t* data_;
  size_t next_idx_ = 0;
};

struct PackedColumnDecoderDataV1 {
  PackedRowDecoderV1 decoder;
  void* context;
};

struct PackedColumnDecoderEntry;

using PackedColumnDecoderV1 = UnsafeStatus(*)(
    PackedColumnDecoderDataV1* data, size_t projection_index,
    const PackedColumnDecoderEntry* chain);

class PackedRowDecoderFactory {
 public:
  virtual ~PackedRowDecoderFactory() = default;

  virtual PackedColumnDecoderEntry GetColumnDecoderV1(
      size_t projection_index, ssize_t packed_index, bool last) = 0;

  virtual PackedColumnDecoderEntry GetColumnDecoderV2(
      size_t projection_index, ssize_t packed_index, bool last) = 0;
};

using PackedColumnDecoderV2 = UnsafeStatus(*)(
    const uint8_t* header, const uint8_t* body, void* context, size_t projection_index,
    const PackedColumnDecoderEntry* chain);

struct PackedColumnDecodersV2 {
  PackedColumnDecoderV2 with_nulls;
  PackedColumnDecoderV2 no_nulls;
};

using PackedColumnRouter = UnsafeStatus(*)(
    const uint8_t* value, void* context, size_t projection_index,
    const PackedColumnDecoderEntry* chain);

union PackedColumnDecoderUnion {
  PackedColumnRouter router;
  PackedColumnDecoderV1 v1;
  PackedColumnDecodersV2 v2;
};

struct PackedColumnDecoderEntry {
  PackedColumnDecoderUnion decoder;
  size_t data;
};

class PackedRowDecoder {
 public:
  PackedRowDecoder();

  bool Valid() const {
    return schema_packing_ != nullptr;
  }

  void Reset() {
    schema_packing_ = nullptr;
  }

  void Init(
      PackedRowVersion version, const ReaderProjection& projection, const SchemaPacking& packing,
      PackedRowDecoderFactory* callback, const Schema& schema);

  Status Apply(Slice value, void* context);

 private:
  const SchemaPacking* schema_packing_ = nullptr;
  const Schema* schema_ = nullptr;
  size_t num_key_columns_ = 0;
  boost::container::small_vector<PackedColumnDecoderEntry, 0x10> decoders_;
};

using PackedRowDecoderVariant = std::variant<PackedRowDecoderV1, PackedRowDecoderV2>;

PackedRowDecoderBase& DecoderBase(PackedRowDecoderVariant* decoder);

template <bool kCheckNull, bool kLast, bool kIncrementProjectionIndex = true>
UnsafeStatus CallNextDecoderV2(
    const uint8_t* header, const uint8_t* body, void* context, size_t projection_index,
    const PackedColumnDecoderEntry* chain) {
  if (kLast) {
    return UnsafeStatus();
  }
  ++chain;
  if (kIncrementProjectionIndex) {
    ++projection_index;
  }
  auto decoder = kCheckNull ? chain->decoder.v2.with_nulls : chain->decoder.v2.no_nulls;
  return decoder(header, body, context, projection_index, chain);
}

template <bool kLast>
UnsafeStatus CallNextDecoderV1(
    PackedColumnDecoderDataV1* data, size_t projection_index,
    const PackedColumnDecoderEntry* chain) {
  if (kLast) {
    return UnsafeStatus();
  }
  ++chain;
  return chain->decoder.v1(data, ++projection_index, chain);
}

size_t VarLenColEndOffset(size_t idx, const uint8_t* data);

} // namespace yb::dockv
