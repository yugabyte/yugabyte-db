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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <string>
#include <utility>
#include <vector>

#include "yb/common/common_fwd.h"
#include "yb/common/schema.h"
#include "yb/common/types.h"

#include "yb/gutil/macros.h"

#include "yb/util/bitmap.h"
#include "yb/util/memory/arena.h"
#include "yb/util/status.h"

namespace yb {

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

  const TypeInfo* typeinfo() const;
  size_t size() const;
  bool is_nullable() const;
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

  if (src.typeinfo()->var_length()) {
    // If it's a Slice column, need to relocate the referred-to data
    // as well as the slice itself.
    // TODO: potential optimization here: if the new value is smaller than
    // the old value, we could potentially just overwrite in some cases.
    const Slice *src_slice = reinterpret_cast<const Slice *>(src.ptr());
    Slice *dst_slice = reinterpret_cast<Slice *>(dst->mutable_ptr());
    if (dst_arena != NULL) {
      if (PREDICT_FALSE(!dst_arena->RelocateSlice(*src_slice, dst_slice))) {
        return STATUS(IOError, "out of memory copying slice", src_slice->ToString());
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
  RowProjector(const Schema* base_schema, const Schema* projection);

  // Initialize the projection mapping with the specified base_schema and projection
  Status Init();

  Status Reset(const Schema* base_schema, const Schema* projection);

  // Project a row from one schema into another, using the projection mapping.
  // Indirected data is copied into the provided dst arena.
  //
  // Use this method only on the read-path.
  template<class RowType1, class RowType2, class ArenaType>
  Status ProjectRowForRead(
      const RowType1& src_row, RowType2 *dst_row, ArenaType *dst_arena) const {
    return ProjectRow<RowType1, RowType2, ArenaType, true>(src_row, dst_row, dst_arena);
  }

  // Project a row from one schema into another, using the projection mapping.
  // Indirected data is copied into the provided dst arena.
  //
  // Use this method only on the write-path.
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
  const std::vector<ProjectionIdxMapping>& base_cols_mapping() const { return base_cols_mapping_; }

  // Returns the mapping between base schema and projection schema columns
  // that requires a type adapter.
  // first: is the projection column index, second: is the base_schema  index
  const std::vector<ProjectionIdxMapping>& adapter_cols_mapping() const { return adapter_cols_mapping_; }

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

  Status ProjectExtraColumn(size_t proj_col_idx);

 private:
  // Project a row from one schema into another, using the projection mapping.
  // Indirected data is copied into the provided dst arena.
  template<class RowType1, class RowType2, class ArenaType, bool FOR_READ>
  Status ProjectRow(
      const RowType1& src_row, RowType2 *dst_row, ArenaType *dst_arena) const {
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

    return Status::OK();
  }

 private:
  std::vector<ProjectionIdxMapping> base_cols_mapping_;
  std::vector<ProjectionIdxMapping> adapter_cols_mapping_;

  const Schema* base_schema_;
  const Schema* projection_;
  bool is_identity_;

  DISALLOW_COPY_AND_ASSIGN(RowProjector);
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
  for (size_t i = 0; i < schema->num_columns(); i++) {
    typename RowType::Cell cell = row->cell(i);
    if (cell.typeinfo()->var_length()) {
      if (cell.is_nullable() && cell.is_null()) {
        continue;
      }

      Slice *slice = reinterpret_cast<Slice *>(cell.mutable_ptr());
      if (!dst_arena->RelocateSlice(*slice, slice)) {
        return STATUS(IOError, "Unable to relocate slice");
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

  static void InitNullsBitmap(const Schema& schema, Slice row_data) {
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

  static Slice CellSlice(const Schema& schema, const uint8_t *row_data, size_t col_idx) {
    const uint8_t* cell_data_ptr = cell_ptr(schema, row_data, col_idx);
    if (schema.column(col_idx).type_info()->var_length()) {
      return *(reinterpret_cast<const Slice*>(cell_data_ptr));
    } else {
      return Slice(cell_data_ptr, schema.column(col_idx).type_info()->size);
    }
  }
};

template<class ContiguousRowType>
class ContiguousRowCell {
 public:
  ContiguousRowCell(const ContiguousRowType* row, size_t idx)
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
  size_t col_idx_;
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

  Slice CellSlice(size_t col_idx) const {
    return ContiguousRowHelper::CellSlice(*schema_, row_data_, col_idx);
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

  Slice CellSlice(size_t col_idx) const {
    return ContiguousRowHelper::CellSlice(*schema_, row_data_, col_idx);
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

} // namespace yb
