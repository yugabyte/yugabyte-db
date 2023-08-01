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
#include "yb/common/id_mapping.h"

#include "yb/dockv/dockv.fwd.h"

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

  static ColumnPackingData FromPB(const ColumnPackingPB& pb);
  void ToPB(ColumnPackingPB* out) const;

  bool varlen() const {
    return size == 0;
  }

  std::string ToString() const;

  bool operator==(const ColumnPackingData&) const = default;
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

  size_t varlen_columns_count() const {
    return varlen_columns_count_;
  }

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

} // namespace yb::dockv
