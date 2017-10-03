// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
#ifndef KUDU_COMMON_ROW_H
#define KUDU_COMMON_ROW_H

#include <glog/logging.h>
#include <string>
#include <utility>
#include <vector>

#include "kudu/common/types.h"
#include "kudu/common/schema.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/map-util.h"
#include "kudu/util/memory/arena.h"
#include "kudu/util/bitmap.h"

namespace kudu {

// A simple cell of data which directly corresponds to a pointer value.
// stack.
struct SimpleConstCell {
 public:
  // Both parameters must remain valid for the lifetime of the cell object.
  SimpleConstCell(const ColumnSchema* col_schema,
                  const void* value)
    : col_schema_(col_schema),
      value_(value) {
  }

  const TypeInfo* typeinfo() const { return col_schema_->type_info(); }
  size_t size() const { return col_schema_->type_info()->size(); }
  bool is_nullable() const { return col_schema_->is_nullable(); }
  const void* ptr() const { return value_; }
  bool is_null() const { return value_ == NULL; }

 private:
  const ColumnSchema* col_schema_;
  const void* value_;
};

// Copy the cell data from 'src' to 'dst'. This only copies the data, and not
// the null state. Use CopyCell() if you need to copy the null-ness.
//
// If dst_arena is non-NULL, relocates the data into the given arena.
template <class SrcCellType, class DstCellType, class ArenaType>
Status CopyCellData(const SrcCellType &src, DstCellType* dst, ArenaType *dst_arena) {
  DCHECK_EQ(src.typeinfo()->type(), dst->typeinfo()->type());

  if (src.typeinfo()->physical_type() == BINARY) {
    // If it's a Slice column, need to relocate the referred-to data
    // as well as the slice itself.
    // TODO: potential optimization here: if the new value is smaller than
    // the old value, we could potentially just overwrite in some cases.
    const Slice *src_slice = reinterpret_cast<const Slice *>(src.ptr());
    Slice *dst_slice = reinterpret_cast<Slice *>(dst->mutable_ptr());
    if (dst_arena != NULL) {
      if (PREDICT_FALSE(!dst_arena->RelocateSlice(*src_slice, dst_slice))) {
        return Status::IOError("out of memory copying slice", src_slice->ToString());
      }
    } else {
      // Just copy the slice without relocating.
      // This is used by callers who know that the source row's data is going
      // to stick around for the scope of the destination.
      *dst_slice = *src_slice;
    }
  } else {
    memcpy(dst->mutable_ptr(), src.ptr(), src.size()); // TODO: inline?
  }
  return Status::OK();
}

// Copy the cell from 'src' to 'dst'.
//
// This copies the data, and relocates indirect data into the given arena,
// if it is not NULL.
template <class SrcCellType, class DstCellType, class ArenaType>
Status CopyCell(const SrcCellType &src, DstCellType* dst, ArenaType *dst_arena) {
  if (src.is_nullable()) {
    // Copy the null state.
    dst->set_null(src.is_null());
    if (src.is_null()) {
      // no need to copy any data contents once we marked the destination
      // cell as null.
      return Status::OK();
    }
  }

  return CopyCellData(src, dst, dst_arena);
}

// Copy all of the cells from one row to another. The two rows must share
// the same Schema. If they do not, use ProjectRow() below.
// This can be used to translate between columnar and row-wise layout, for example.
//
// If 'dst_arena' is set, then will relocate any indirect data to that arena
// during the copy.
template<class RowType1, class RowType2, class ArenaType>
inline Status CopyRow(const RowType1 &src_row, RowType2 *dst_row, ArenaType *dst_arena) {
  DCHECK_SCHEMA_EQ(*src_row.schema(), *dst_row->schema());

  for (int i = 0; i < src_row.schema()->num_columns(); i++) {
    typename RowType1::Cell src = src_row.cell(i);
    typename RowType2::Cell dst = dst_row->cell(i);
    RETURN_NOT_OK(CopyCell(src, &dst, dst_arena));
  }

  return Status::OK();
}

// Projection mapping for the specified schemas.
// A projection may contain:
//  - columns that are present in the "base schema"
//  - columns that are present in the "base schema" but with different types.
//    In this case an adapter should be used (e.g. INT8 to INT64, INT8 to STRING, ...)
//  - columns that are not present in the "base schema".
//    In this case the default value of the projection column will be used.
//
// Example:
//  RowProjector projector.
//  projector.Init(base_schema, projection);
//  projector.ProjectRow(row_a, &row_b, &row_b_arena);
class RowProjector {
 public:
  typedef std::pair<size_t, size_t> ProjectionIdxMapping;

  // Construct a projector.
  // The two Schema pointers must remain valid for the lifetime of this object.
  RowProjector(const Schema* base_schema, const Schema* projection)
    : base_schema_(base_schema), projection_(projection),
      is_identity_(base_schema->Equals(*projection)) {
  }

  // Initialize the projection mapping with the specified base_schema and projection
  Status Init() {
    return projection_->GetProjectionMapping(*base_schema_, this);
  }

  Status Reset(const Schema* base_schema, const Schema* projection) {
    base_schema_ = base_schema;
    projection_ = projection;
    base_cols_mapping_.clear();
    adapter_cols_mapping_.clear();
    projection_defaults_.clear();
    is_identity_ = base_schema->Equals(*projection);
    return Init();
  }

  // Project a row from one schema into another, using the projection mapping.
  // Indirected data is copied into the provided dst arena.
  //
  // Use this method only on the read-path.
  // The col_schema.read_default_value() will be used.
  template<class RowType1, class RowType2, class ArenaType>
  Status ProjectRowForRead(const RowType1& src_row, RowType2 *dst_row, ArenaType *dst_arena) const {
    return ProjectRow<RowType1, RowType2, ArenaType, true>(src_row, dst_row, dst_arena);
  }

  // Project a row from one schema into another, using the projection mapping.
  // Indirected data is copied into the provided dst arena.
  //
  // Use this method only on the write-path.
  // The col_schema.write_default_value() will be used.
  template<class RowType1, class RowType2, class ArenaType>
  Status ProjectRowForWrite(const RowType1& src_row, RowType2 *dst_row,
                            ArenaType *dst_arena) const {
    return ProjectRow<RowType1, RowType2, ArenaType, false>(src_row, dst_row, dst_arena);
  }

  bool is_identity() const { return is_identity_; }
  const Schema* projection() const { return projection_; }
  const Schema* base_schema() const { return base_schema_; }

  // Returns the mapping between base schema and projection schema columns
  // first: is the projection column index, second: is the base_schema  index
  const vector<ProjectionIdxMapping>& base_cols_mapping() const { return base_cols_mapping_; }

  // Returns the mapping between base schema and projection schema columns
  // that requires a type adapter.
  // first: is the projection column index, second: is the base_schema  index
  const vector<ProjectionIdxMapping>& adapter_cols_mapping() const { return adapter_cols_mapping_; }

  // Returns the projection indexes of the columns to add with a default value.
  //
  // These are columns which are present in 'projection_' but not in 'base_schema',
  // and for which 'projection' has a default.
  const vector<size_t>& projection_defaults() const { return projection_defaults_; }

 private:
  friend class Schema;

  Status ProjectBaseColumn(size_t proj_col_idx, size_t base_col_idx) {
    base_cols_mapping_.push_back(ProjectionIdxMapping(proj_col_idx, base_col_idx));
    return Status::OK();
  }

  Status ProjectAdaptedColumn(size_t proj_col_idx, size_t base_col_idx) {
    adapter_cols_mapping_.push_back(ProjectionIdxMapping(proj_col_idx, base_col_idx));
    return Status::OK();
  }

  Status ProjectDefaultColumn(size_t proj_col_idx) {
    projection_defaults_.push_back(proj_col_idx);
    return Status::OK();
  }

  Status ProjectExtraColumn(size_t proj_col_idx) {
    return Status::InvalidArgument(
      "The column '" + projection_->column(proj_col_idx).name() +
      "' does not exist in the projection, and it does not have a "
      "default value or a nullable type");
  }

 private:
  // Project a row from one schema into another, using the projection mapping.
  // Indirected data is copied into the provided dst arena.
  template<class RowType1, class RowType2, class ArenaType, bool FOR_READ>
  Status ProjectRow(const RowType1& src_row, RowType2 *dst_row, ArenaType *dst_arena) const {
    DCHECK_SCHEMA_EQ(*base_schema_, *src_row.schema());
    DCHECK_SCHEMA_EQ(*projection_, *dst_row->schema());

    // Copy directly from base Data
    for (const auto& base_mapping : base_cols_mapping_) {
      typename RowType1::Cell src_cell = src_row.cell(base_mapping.second);
      typename RowType2::Cell dst_cell = dst_row->cell(base_mapping.first);
      RETURN_NOT_OK(CopyCell(src_cell, &dst_cell, dst_arena));
    }

    // TODO: Copy Adapted base Data
    DCHECK(adapter_cols_mapping_.size() == 0) << "Value Adapter not supported yet";

    // Fill with Defaults
    for (auto proj_idx : projection_defaults_) {
      const ColumnSchema& col_proj = projection_->column(proj_idx);
      const void *vdefault = FOR_READ ? col_proj.read_default_value() :
                                        col_proj.write_default_value();
      SimpleConstCell src_cell(&col_proj, vdefault);
      typename RowType2::Cell dst_cell = dst_row->cell(proj_idx);
      RETURN_NOT_OK(CopyCell(src_cell, &dst_cell, dst_arena));
    }

    return Status::OK();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(RowProjector);

  vector<ProjectionIdxMapping> base_cols_mapping_;
  vector<ProjectionIdxMapping> adapter_cols_mapping_;
  vector<size_t> projection_defaults_;

  const Schema* base_schema_;
  const Schema* projection_;
  bool is_identity_;
};

// Projection mapping from the schema used to encode a RowChangeList
// to the new specified schema. Used on the read/compaction path to
// project the deltas to the user/latest specified projection.
//
// A projection may contain:
//  - columns that are present in the "base schema"
//  - columns that are present in the "base schema" but with different types.
//    In this case an adapter should be used (e.g. INT8 to INT64, INT8 to STRING, ...)
//  - columns that are not present in the "base schema".
//    These columns are not considered since they cannot be in the delta.
class DeltaProjector {
 public:
  // The delta_schema and projection must remain valid for the lifetime
  // of the object.
  DeltaProjector(const Schema* delta_schema, const Schema* projection)
    : delta_schema_(delta_schema), projection_(projection),
      is_identity_(delta_schema->Equals(*projection)) {
  }

  Status Init() {
    // TODO: doesn't look like this uses the is_identity performance
    // shortcut
    return projection_->GetProjectionMapping(*delta_schema_, this);
  }

  bool is_identity() const { return is_identity_; }

  const Schema* projection() const { return projection_; }
  const Schema* delta_schema() const { return delta_schema_; }

  bool get_base_col_from_proj_idx(size_t proj_col_idx, size_t *base_col_idx) const {
    return FindCopy(base_cols_mapping_, proj_col_idx, base_col_idx);
  }

  bool get_adapter_col_from_proj_idx(size_t proj_col_idx, size_t *base_col_idx) const {
    return FindCopy(adapter_cols_mapping_, proj_col_idx, base_col_idx);
  }

  // TODO: Discourage the use of this. At the moment is only in RowChangeList::Project
  bool get_proj_col_from_base_id(size_t col_id, size_t *proj_col_idx) const {
    return FindCopy(rbase_cols_mapping_, col_id, proj_col_idx);
  }

  bool get_proj_col_from_adapter_id(size_t col_id, size_t *proj_col_idx) const {
    return FindCopy(radapter_cols_mapping_, col_id, proj_col_idx);
  }

 private:
  friend class ::kudu::Schema;

  Status ProjectBaseColumn(size_t proj_col_idx, size_t base_col_idx) {
    base_cols_mapping_[proj_col_idx] = base_col_idx;
    if (delta_schema_->has_column_ids()) {
      rbase_cols_mapping_[delta_schema_->column_id(base_col_idx)] = proj_col_idx;
    } else {
      rbase_cols_mapping_[proj_col_idx] = proj_col_idx;
    }
    return Status::OK();
  }

  Status ProjectAdaptedColumn(size_t proj_col_idx, size_t base_col_idx) {
    adapter_cols_mapping_[proj_col_idx] = base_col_idx;
    if (delta_schema_->has_column_ids()) {
      radapter_cols_mapping_[delta_schema_->column_id(base_col_idx)] = proj_col_idx;
    } else {
      radapter_cols_mapping_[proj_col_idx] = proj_col_idx;
    }
    return Status::OK();
  }

  Status ProjectDefaultColumn(size_t proj_col_idx) {
    // Not used, since deltas are update...
    // we don't have this column, so we don't have updates
    return Status::OK();
  }

  Status ProjectExtraColumn(size_t proj_col_idx) {
    return Status::InvalidArgument(
      "The column '" + delta_schema_->column(proj_col_idx).name() +
      "' does not exist in the projection, and it does not have a "
      "default value or a nullable type");
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(DeltaProjector);

  std::unordered_map<size_t, size_t> base_cols_mapping_;     // [proj_idx] = base_idx
  std::unordered_map<size_t, size_t> rbase_cols_mapping_;    // [id] = proj_idx

  std::unordered_map<size_t, size_t> adapter_cols_mapping_;  // [proj_idx] = base_idx
  std::unordered_map<size_t, size_t> radapter_cols_mapping_; // [id] = proj_idx

  const Schema* delta_schema_;
  const Schema* projection_;
  bool is_identity_;
};

// Copy any indirect (eg STRING) data referenced by the given row into the
// provided arena.
//
// The row itself is mutated so that the indirect data points to the relocated
// storage.
template <class RowType, class ArenaType>
inline Status RelocateIndirectDataToArena(RowType *row, ArenaType *dst_arena) {
  const Schema* schema = row->schema();
  // For any Slice columns, copy the sliced data into the arena
  // and update the pointers
  for (int i = 0; i < schema->num_columns(); i++) {
    typename RowType::Cell cell = row->cell(i);
    if (cell.typeinfo()->physical_type() == BINARY) {
      if (cell.is_nullable() && cell.is_null()) {
        continue;
      }

      Slice *slice = reinterpret_cast<Slice *>(cell.mutable_ptr());
      if (!dst_arena->RelocateSlice(*slice, slice)) {
        return Status::IOError("Unable to relocate slice");
      }
    }
  }
  return Status::OK();
}


class ContiguousRowHelper {
 public:
  static size_t null_bitmap_size(const Schema& schema) {
    return schema.has_nullables() ? BitmapSize(schema.num_columns()) : 0;
  }

  static uint8_t* null_bitmap_ptr(const Schema& schema, uint8_t* row_data) {
    return row_data + schema.byte_size();
  }

  static size_t row_size(const Schema& schema) {
    return schema.byte_size() + null_bitmap_size(schema);
  }

  static void InitNullsBitmap(const Schema& schema, Slice& row_data) {
    InitNullsBitmap(schema, row_data.mutable_data(), row_data.size() - schema.byte_size());
  }

  static void InitNullsBitmap(const Schema& schema, uint8_t *row_data, size_t bitmap_size) {
    uint8_t *null_bitmap = row_data + schema.byte_size();
    for (size_t i = 0; i < bitmap_size; ++i) {
      null_bitmap[i] = 0x00;
    }
  }

  static bool is_null(const Schema& schema, const uint8_t *row_data, size_t col_idx) {
    DCHECK(schema.column(col_idx).is_nullable());
    return BitmapTest(row_data + schema.byte_size(), col_idx);
  }

  static void SetCellIsNull(const Schema& schema, uint8_t *row_data, size_t col_idx, bool is_null) {
    uint8_t *null_bitmap = row_data + schema.byte_size();
    BitmapChange(null_bitmap, col_idx, is_null);
  }

  static const uint8_t *cell_ptr(const Schema& schema, const uint8_t *row_data, size_t col_idx) {
    return row_data + schema.column_offset(col_idx);
  }

  static const uint8_t *nullable_cell_ptr(const Schema& schema,
                                          const uint8_t *row_data,
                                          size_t col_idx) {
    return is_null(schema, row_data, col_idx) ? NULL : cell_ptr(schema, row_data, col_idx);
  }
};

template<class ContiguousRowType>
class ContiguousRowCell {
 public:
  ContiguousRowCell(const ContiguousRowType* row, int idx)
    : row_(row), col_idx_(idx) {
  }

  const TypeInfo* typeinfo() const { return type_info(); }
  size_t size() const { return type_info()->size(); }
  const void* ptr() const { return row_->cell_ptr(col_idx_); }
  void* mutable_ptr() const { return row_->mutable_cell_ptr(col_idx_); }
  bool is_nullable() const { return row_->schema()->column(col_idx_).is_nullable(); }
  bool is_null() const { return row_->is_null(col_idx_); }
  void set_null(bool is_null) const { row_->set_null(col_idx_, is_null); }

 private:
  const TypeInfo* type_info() const {
    return row_->schema()->column(col_idx_).type_info();
  }

  const ContiguousRowType* row_;
  int col_idx_;
};

// The row has all columns layed out in memory based on the schema.column_offset()
class ContiguousRow {
 public:
  typedef ContiguousRowCell<ContiguousRow> Cell;

  explicit ContiguousRow(const Schema* schema, uint8_t *row_data = NULL)
    : schema_(schema), row_data_(row_data) {
  }

  const Schema* schema() const {
    return schema_;
  }

  void Reset(uint8_t *row_data) {
    row_data_ = row_data;
  }

  bool is_null(size_t col_idx) const {
    return ContiguousRowHelper::is_null(*schema_, row_data_, col_idx);
  }

  void set_null(size_t col_idx, bool is_null) const {
    ContiguousRowHelper::SetCellIsNull(*schema_, row_data_, col_idx, is_null);
  }

  const uint8_t *cell_ptr(size_t col_idx) const {
    return ContiguousRowHelper::cell_ptr(*schema_, row_data_, col_idx);
  }

  uint8_t *mutable_cell_ptr(size_t col_idx) const {
    return const_cast<uint8_t*>(cell_ptr(col_idx));
  }

  const uint8_t *nullable_cell_ptr(size_t col_idx) const {
    return ContiguousRowHelper::nullable_cell_ptr(*schema_, row_data_, col_idx);
  }

  Cell cell(size_t col_idx) const {
    return Cell(this, col_idx);
  }

 private:
  friend class ConstContiguousRow;

  const Schema* schema_;
  uint8_t *row_data_;
};

// This is the same as ContiguousRow except it refers to a const area of memory that
// should not be mutated.
class ConstContiguousRow {
 public:
  typedef ContiguousRowCell<ConstContiguousRow> Cell;

  explicit ConstContiguousRow(const ContiguousRow &row)
    : schema_(row.schema_),
      row_data_(row.row_data_) {
  }

  ConstContiguousRow(const Schema* schema, const void *row_data)
    : schema_(schema), row_data_(reinterpret_cast<const uint8_t *>(row_data)) {
  }

  ConstContiguousRow(const Schema* schema, const Slice& row_slice)
    : schema_(schema), row_data_(row_slice.data()) {
  }

  const Schema* schema() const {
    return schema_;
  }

  const uint8_t *row_data() const {
    return row_data_;
  }

  size_t row_size() const {
    return ContiguousRowHelper::row_size(*schema_);
  }

  bool is_null(size_t col_idx) const {
    return ContiguousRowHelper::is_null(*schema_, row_data_, col_idx);
  }

  const uint8_t *cell_ptr(size_t col_idx) const {
    return ContiguousRowHelper::cell_ptr(*schema_, row_data_, col_idx);
  }

  const uint8_t *nullable_cell_ptr(size_t col_idx) const {
    return ContiguousRowHelper::nullable_cell_ptr(*schema_, row_data_, col_idx);
  }

  Cell cell(size_t col_idx) const {
    return Cell(this, col_idx);
  }

 private:
  const Schema* schema_;
  const uint8_t *row_data_;
};

// Delete functions from ContiguousRowCell that can mutate the cell by
// specializing for ConstContiguousRow.
template<>
void* ContiguousRowCell<ConstContiguousRow>::mutable_ptr() const;
template<>
void ContiguousRowCell<ConstContiguousRow>::set_null(bool null) const;


// Utility class for building rows corresponding to a given schema.
// This is used only by tests.
// TODO: move it into a test utility.
class RowBuilder {
 public:
  explicit RowBuilder(const Schema& schema)
    : schema_(schema),
      arena_(1024, 1024*1024),
      bitmap_size_(ContiguousRowHelper::null_bitmap_size(schema)) {
    Reset();
  }

  // Reset the RowBuilder so that it is ready to build
  // the next row.
  // NOTE: The previous row's data is invalidated. Even
  // if the previous row's data has been copied, indirected
  // entries such as strings may end up shared or deallocated
  // after Reset. So, the previous row must be fully copied
  // (eg using CopyRowToArena()).
  void Reset() {
    arena_.Reset();
    size_t row_size = schema_.byte_size() + bitmap_size_;
    buf_ = reinterpret_cast<uint8_t *>(arena_.AllocateBytes(row_size));
    CHECK(buf_) << "could not allocate " << row_size << " bytes for row builder";
    col_idx_ = 0;
    byte_idx_ = 0;
    ContiguousRowHelper::InitNullsBitmap(schema_, buf_, bitmap_size_);
  }

  void AddString(const Slice &slice) {
    CheckNextType(STRING);
    AddSlice(slice);
  }

  void AddString(const string &str) {
    CheckNextType(STRING);
    AddSlice(str);
  }

  void AddBinary(const Slice &slice) {
    CheckNextType(BINARY);
    AddSlice(slice);
  }

  void AddBinary(const string &str) {
    CheckNextType(BINARY);
    AddSlice(str);
  }

  void AddInt8(int8_t val) {
    CheckNextType(INT8);
    *reinterpret_cast<int8_t *>(&buf_[byte_idx_]) = val;
    Advance();
  }

  void AddUint8(uint8_t val) {
    CheckNextType(UINT8);
    *reinterpret_cast<uint8_t *>(&buf_[byte_idx_]) = val;
    Advance();
  }

  void AddInt16(int16_t val) {
    CheckNextType(INT16);
    *reinterpret_cast<int16_t *>(&buf_[byte_idx_]) = val;
    Advance();
  }

  void AddUint16(uint16_t val) {
    CheckNextType(UINT16);
    *reinterpret_cast<uint16_t *>(&buf_[byte_idx_]) = val;
    Advance();
  }

  void AddInt32(int32_t val) {
    CheckNextType(INT32);
    *reinterpret_cast<int32_t *>(&buf_[byte_idx_]) = val;
    Advance();
  }

  void AddUint32(uint32_t val) {
    CheckNextType(UINT32);
    *reinterpret_cast<uint32_t *>(&buf_[byte_idx_]) = val;
    Advance();
  }

  void AddInt64(int64_t val) {
    CheckNextType(INT64);
    *reinterpret_cast<int64_t *>(&buf_[byte_idx_]) = val;
    Advance();
  }

  void AddTimestamp(int64_t micros_utc_since_epoch) {
    CheckNextType(TIMESTAMP);
    *reinterpret_cast<int64_t *>(&buf_[byte_idx_]) = micros_utc_since_epoch;
    Advance();
  }

  void AddUint64(uint64_t val) {
    CheckNextType(UINT64);
    *reinterpret_cast<uint64_t *>(&buf_[byte_idx_]) = val;
    Advance();
  }

  void AddFloat(float val) {
    CheckNextType(FLOAT);
    *reinterpret_cast<float *>(&buf_[byte_idx_]) = val;
    Advance();
  }

  void AddDouble(double val) {
    CheckNextType(DOUBLE);
    *reinterpret_cast<double *>(&buf_[byte_idx_]) = val;
    Advance();
  }

  void AddNull() {
    CHECK(schema_.column(col_idx_).is_nullable());
    BitmapSet(buf_ + schema_.byte_size(), col_idx_);
    Advance();
  }

  // Retrieve the data slice from the current row.
  // The Add*() functions must have been called an appropriate
  // number of times such that all columns are filled in, or else
  // a crash will occur.
  //
  // The data slice returned by this is only valid until the next
  // call to Reset().
  // Note that the Slice may also contain pointers which refer to
  // other parts of the internal Arena, so even if the returned
  // data is copied, it is not safe to Reset() before also calling
  // CopyRowIndirectDataToArena.
  const Slice data() const {
    CHECK_EQ(byte_idx_, schema_.byte_size());
    return Slice(buf_, byte_idx_ + bitmap_size_);
  }

  const Schema& schema() const {
    return schema_;
  }

  ConstContiguousRow row() const {
    return ConstContiguousRow(&schema_, data());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(RowBuilder);

  void AddSlice(const Slice &slice) {
    Slice *ptr = reinterpret_cast<Slice *>(buf_ + byte_idx_);
    CHECK(arena_.RelocateSlice(slice, ptr)) << "could not allocate space in arena";

    Advance();
  }

  void AddSlice(const string &str) {
    uint8_t *in_arena = arena_.AddSlice(str);
    CHECK(in_arena) << "could not allocate space in arena";

    Slice *ptr = reinterpret_cast<Slice *>(buf_ + byte_idx_);
    *ptr = Slice(in_arena, str.size());

    Advance();
  }

  void CheckNextType(DataType type) {
    CHECK_EQ(schema_.column(col_idx_).type_info()->type(),
             type);
  }

  void Advance() {
    int size = schema_.column(col_idx_).type_info()->size();
    byte_idx_ += size;
    col_idx_++;
  }

  const Schema schema_;
  Arena arena_;
  uint8_t *buf_;

  size_t col_idx_;
  size_t byte_idx_;
  size_t bitmap_size_;
};

} // namespace kudu

#endif
