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

#include <optional>
#include <unordered_map>

#include <boost/function.hpp>
#include <boost/functional/hash.hpp>

#include <google/protobuf/repeated_field.h>

#include "yb/common/common_fwd.h"
#include "yb/common/column_id.h"

#include "yb/dockv/dockv_fwd.h"

#include "yb/util/byte_buffer.h"
#include "yb/util/kv_util.h"

namespace yb::dockv {

// The packed row V1 is encoded in the following format.
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

using SchemaVersionMapper = boost::function<Result<SchemaVersion>(SchemaVersion)>;

// Replaces the schema version in packed value with the provided schema version.
// Note: Value starts with the schema version (does not contain control fields, value type).
Status ReplaceSchemaVersionInPackedValue(Slice value,
                                         const ValueControlFields& control_fields,
                                         const SchemaVersionMapper& schema_versions_mapper,
                                         ValueBuffer *out);

class PackableValue {
 public:
  // limit - maximal number of bytes that could be packed. If packed value does not fit into this
  // limit PackTo should do nothing and return false.
  virtual bool PackTo(size_t limit, ValueBuffer* result) const = 0;
  virtual std::string ToString() const = 0;

  virtual ~PackableValue() = default;
};

class RowPackerBase {
 public:
  // packed_size_limit - don't pack column if packed row will be over limit after it.
  RowPackerBase(
      std::reference_wrapper<const SchemaPacking> packing, size_t packed_size_limit,
      const ValueControlFields& row_control_fields, std::reference_wrapper<const Schema> schema);

  RowPackerBase(
      std::reference_wrapper<const SchemaPacking> packing, size_t packed_size_limit,
      Slice control_fields, std::reference_wrapper<const Schema> schema);

  RowPackerBase(const RowPackerBase&) = delete;
  void operator=(const RowPackerBase&) = delete;

  bool Empty() const {
    return idx_ == 0;
  }

  bool Finished() const;

  const SchemaPacking& packing() const {
    return packing_;
  }

  const Schema& schema() const {
    return schema_;
  }

  ColumnId NextColumnId() const;
  Result<const ColumnPackingData&> NextColumnData() const;

 protected:
  template <class Traits, class Value>
  Result<bool> DoAddValueImpl(ColumnId column_id, const Value& value, ssize_t tail_size);

  // Schema packing used.
  const SchemaPacking& packing_;

  // Don't pack after this limit.
  const ssize_t packed_size_limit_;

  // Index of next column to be packed.
  size_t idx_ = 0;

  // Offset of variable header from start of the buffer.
  size_t var_header_start_;

  // The end prefix, i.e., beginning of column values.
  size_t prefix_end_;

  // Resulting buffer.
  ValueBuffer result_;

  const Schema& schema_;
};

// Packs the row with V1 encoding.
class RowPackerV1 : public RowPackerBase {
 public:
  using PackedValue = PackedValueV1;

  template <class... Args>
  RowPackerV1(SchemaVersion version, Args&&... args) : RowPackerBase(std::forward<Args>(args)...) {
    Init(version);
  }

  // AddValue returns false when unable to add value due to packed size limit.
  // tail_size is added to proposed encoded size, to make decision whether encoded value fits
  // into bounds or not. Useful during repacking, when we know size of packed columns after this
  // one.
  Result<bool> AddValue(ColumnId column_id, PackedValueV1 value, ssize_t tail_size);
  Result<bool> AddValue(ColumnId column_id, PackedValueV2 value, ssize_t tail_size);
  // Add value consisting of 2 parts - value_prefix+value_suffix.
  Result<bool> AddValue(
      ColumnId column_id, Slice value_prefix, PackedValueV1 value_suffix, ssize_t tail_size);
  Result<bool> AddValue(ColumnId column_id, const QLValuePB& value);
  Result<bool> AddValue(ColumnId column_id, const LWQLValuePB& value);
  Result<bool> AddValue(ColumnId column_id, Slice control_fields, const QLValuePB& value);

  Result<Slice> Complete();

  // TODO(packed_row) Remove after full support for packed row v2 is merged.
  Result<bool> AddValue(ColumnId column_id, Slice value, ssize_t tail_size);
  Result<bool> AddValue(
      ColumnId column_id, Slice value_prefix, Slice value_suffix, ssize_t tail_size);
  void Restart();

 private:
  void Init(SchemaVersion version);

  template <class Value>
  Result<bool> DoAddValue(ColumnId column_id, const Value& value, ssize_t tail_size);
};

// Packs the row with V2 encoding.
class RowPackerV2 : public RowPackerBase {
 public:
  using PackedValue = PackedValueV2;

  // Flat to mark whether packed row has nulls or not.
  // When row does not contain nulls, then we don't store null mask in it.
  static constexpr uint8_t kHasNullsFlag = 1;

  template <class... Args>
  RowPackerV2(SchemaVersion version, Args&&... args) : RowPackerBase(std::forward<Args>(args)...) {
    Init(version);
  }

  // Returns false when unable to add value due to packed size limit.
  // tail_size is added to proposed encoded size, to make decision whether encoded value fits
  // into bounds or not.
  Result<bool> AddValue(ColumnId column_id, PackedValueV2 value, ssize_t tail_size);
  Result<bool> AddValue(ColumnId column_id, PackedValueV1 value, ssize_t tail_size);
  // Add value consisting of 2 parts - value_prefix+value_suffix.
  Result<bool> AddValue(
      ColumnId column_id, Slice value_prefix, PackedValueV1 value_suffix, ssize_t tail_size);
  Result<bool> AddValue(ColumnId column_id, const QLValuePB& value);
  Result<bool> AddValue(ColumnId column_id, const LWQLValuePB& value);
  Result<bool> AddValue(ColumnId column_id, const PackableValue& value);

  Result<Slice> Complete();

  const Schema& GetSchema();

 private:
  void Init(SchemaVersion version);

  template <class Value>
  Result<bool> DoAddValue(ColumnId column_id, const Value& value, ssize_t tail_size);
};

using RowPackerVariant = std::variant<RowPackerV1, RowPackerV2>;

RowPackerBase& PackerBase(RowPackerVariant* packer_variant);

} // namespace yb::dockv
