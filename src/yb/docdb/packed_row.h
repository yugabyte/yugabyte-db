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

#ifndef YB_DOCDB_PACKED_ROW_H
#define YB_DOCDB_PACKED_ROW_H

#include <unordered_map>

#include <boost/functional/hash.hpp>
#include <boost/optional.hpp>

#include <google/protobuf/repeated_field.h>

#include "yb/common/common_fwd.h"
#include "yb/common/column_id.h"

#include "yb/docdb/docdb.fwd.h"

#include "yb/util/byte_buffer.h"
#include "yb/util/kv_util.h"

namespace yb {
namespace docdb {

// The packed row is encoded in the following format.
// Row packing/unpacking is accompanied by SchemaPacking class that is built from schema.
//
// We distinguish variable length columns from fixed length column.
// Fixed length column is non null columns with fixed length encoding, for instance int32.
// All other columns are variable length, for instance string or nullable int64.
//
// SchemaPacking contains ColumnPackingData for each column. See fields meaning below.
//
// For each variable length column we store offset of the end of his data.
// It allows us to easily find bytes range used by particular column.
// I.e. to find column data start we could just get end of previous varlen column and
// add offset_after_prev_varlen_column to it.
// Then in case of fixed column we could just add its size to find end of data.
// And in case of varlen column offset of the end of the data is just stored.
//
// The data is encoded as value type + actual encoded data.
//
// The serialized format:
// varint: schema_version
// uint32: end_of_column_data for the 1st varlen column
// ...
// uint32: end_of_column_data for the last varlen column
// bytes: data for the 1st column
// ...
// bytes: data for the last column
//
// NULL values are stored as columns with 0 length.
// Since we always embed ValueType in column data, we could easily distinguish NULL values
// from columns with empty value.
//
// The rationale for this format is to have ability to extract column value with O(1) complexity.
// Also it helps us to avoid storing common data for all rows, and put it to a single schema info.

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

  Slice GetValue(size_t idx, const Slice& packed) const;
  boost::optional<Slice> GetValue(ColumnId column, const Slice& packed) const;
  void ToPB(SchemaPackingPB* out) const;

  std::string ToString() const;

 private:
  std::vector<ColumnPackingData> columns_;
  std::unordered_map<ColumnId, size_t, boost::hash<ColumnId>> column_to_idx_;
  size_t varlen_columns_count_;
};

class SchemaPackingStorage {
 public:
  SchemaPackingStorage();
  explicit SchemaPackingStorage(const SchemaPackingStorage& rhs, SchemaVersion min_schema_version);

  Result<const SchemaPacking&> GetPacking(SchemaVersion schema_version) const;
  Result<const SchemaPacking&> GetPacking(Slice* packed_row) const;

  void AddSchema(SchemaVersion version, const Schema& schema);

  CHECKED_STATUS LoadFromPB(const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas);

  // Copy all schema packings except schema_version_to_skip to out.
  void ToPB(
      SchemaVersion schema_version_to_skip,
      google::protobuf::RepeatedPtrField<SchemaPackingPB>* out);

  size_t SchemaCount() const {
    return version_to_schema_packing_.size();
  }

  bool HasVersionBelow(SchemaVersion version) const;

 private:
  std::unordered_map<SchemaVersion, SchemaPacking> version_to_schema_packing_;
};

class RowPacker {
 public:
  RowPacker(SchemaVersion version, std::reference_wrapper<const SchemaPacking> packing);
  explicit RowPacker(const std::pair<SchemaVersion, const SchemaPacking&>& pair)
      : RowPacker(pair.first, pair.second) {
  }

  bool Empty() const {
    return idx_ == 0;
  }

  bool Finished() const {
    return idx_ == packing_.columns();
  }

  void Restart();

  ColumnId NextColumnId() const;
  Result<const ColumnPackingData&> NextColumnData() const;

  Status AddValue(ColumnId column, const QLValuePB& value);
  Status AddValue(ColumnId column, const Slice& value);

  Result<Slice> Complete();

 private:
  template <class Value>
  Status DoAddValue(ColumnId column, const Value& value);

  const SchemaPacking& packing_;
  size_t idx_ = 0;
  size_t prefix_end_;
  ValueBuffer result_;
  size_t varlen_write_pos_;
};

} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_PACKED_ROW_H
