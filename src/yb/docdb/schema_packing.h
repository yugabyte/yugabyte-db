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

#include <boost/functional/hash.hpp>

#include <google/protobuf/repeated_field.h>

#include "yb/common/common_fwd.h"
#include "yb/common/column_id.h"

#include "yb/docdb/docdb.fwd.h"

#include "yb/util/slice.h"

namespace yb {
namespace docdb {

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
  explicit SchemaPacking(const Schema& schema);
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
  Slice GetValue(size_t idx, const Slice& packed) const;
  std::optional<Slice> GetValue(ColumnId column_id, const Slice& packed) const;
  void ToPB(SchemaPackingPB* out) const;

  std::string ToString() const;

  bool operator==(const SchemaPacking&) const = default;

 private:
  std::vector<ColumnPackingData> columns_;
  std::unordered_map<ColumnId, int64_t, boost::hash<ColumnId>> column_to_idx_;
  size_t varlen_columns_count_;
};

class SchemaPackingStorage {
 public:
  SchemaPackingStorage();
  explicit SchemaPackingStorage(const SchemaPackingStorage& rhs, SchemaVersion min_schema_version);

  Result<const SchemaPacking&> GetPacking(SchemaVersion schema_version) const;
  Result<const SchemaPacking&> GetPacking(Slice* packed_row) const;

  void AddSchema(SchemaVersion version, const Schema& schema);

  Status LoadFromPB(const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas);
  Status MergeWithRestored(
      SchemaVersion schema_version, const SchemaPB& schema,
      const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas);

  // Copy all schema packings except schema_version_to_skip to out.
  void ToPB(
      SchemaVersion schema_version_to_skip,
      google::protobuf::RepeatedPtrField<SchemaPackingPB>* out);

  size_t SchemaCount() const {
    return version_to_schema_packing_.size();
  }

  bool HasVersionBelow(SchemaVersion version) const;

  bool operator==(const SchemaPackingStorage&) const = default;

 private:
  // Set could_present to true when schemas could contain the same packings as already present.
  Status InsertSchemas(
      const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas, bool could_present);

  std::unordered_map<SchemaVersion, SchemaPacking> version_to_schema_packing_;
};

} // namespace docdb
} // namespace yb
