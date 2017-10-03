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
#ifndef KUDU_COMMON_ROWBLOCK_H
#define KUDU_COMMON_ROWBLOCK_H

#include <vector>
#include "kudu/common/columnblock.h"
#include "kudu/common/schema.h"
#include "kudu/common/row.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/util/memory/arena.h"
#include "kudu/util/bitmap.h"

namespace kudu {

class RowBlockRow;

// Bit-vector representing the selection status of each row in a row block.
//
// When scanning through data, a 1 bit in the selection vector indicates that
// the given row is live and has passed all predicates.
class SelectionVector {
 public:
  // Construct a new vector. The bits are initially in an indeterminate state.
  // Call SetAllTrue() if you require all rows to be initially selected.
  explicit SelectionVector(size_t row_capacity);

  // Construct a vector which shares the underlying memory of another vector,
  // but only exposes up to a given prefix of the number of rows.
  //
  // Note that mutating the resulting bitmap may arbitrarily mutate the "extra"
  // bits of the 'other' bitmap.
  //
  // The underlying bytes must not be deallocated or else this object will become
  // invalid.
  SelectionVector(SelectionVector *other, size_t prefix_rows);

  // Resize the selection vector to the given number of rows.
  // This size must be <= the allocated capacity.
  //
  // Ensures that all rows for indices < n_rows are unmodified.
  void Resize(size_t n_rows);

  // Return the number of selected rows.
  size_t CountSelected() const;

  // Return true if any rows are selected, false if size 0.
  // This is equivalent to (CountSelected() > 0), but faster.
  bool AnySelected() const;

  bool IsRowSelected(size_t row) const {
    DCHECK_LT(row, n_rows_);
    return BitmapTest(&bitmap_[0], row);
  }

  void SetRowSelected(size_t row) {
    DCHECK_LT(row, n_rows_);
    BitmapSet(&bitmap_[0], row);
  }

  void SetRowUnselected(size_t row) {
    DCHECK_LT(row, n_rows_);
    BitmapClear(&bitmap_[0], row);
  }

  uint8_t *mutable_bitmap() {
    return &bitmap_[0];
  }

  const uint8_t *bitmap() const {
    return &bitmap_[0];
  }

  // Set all bits in the bitmap to 1
  void SetAllTrue() {
    // Initially all rows should be selected.
    memset(&bitmap_[0], 0xff, n_bytes_);
    // the last byte in the bitmap may have a few extra bits - need to
    // clear those

    int trailer_bits = 8 - (n_rows_ % 8);
    if (trailer_bits != 8) {
      bitmap_[n_bytes_ - 1] >>= trailer_bits;
    }
  }

  // Set all bits in the bitmap to 0
  void SetAllFalse() {
    memset(&bitmap_[0], 0, n_bytes_);
  }

  size_t nrows() const { return n_rows_; }

 private:
  DISALLOW_COPY_AND_ASSIGN(SelectionVector);

  // The number of allocated bytes in bitmap_
  size_t bytes_capacity_;

  size_t n_rows_;
  size_t n_bytes_;

  gscoped_array<uint8_t> bitmap_;
};

// A block of decoded rows.
// Wrapper around a buffer, which keeps the buffer's size, associated arena,
// and schema. Provides convenience accessors for indexing by row, column, etc.
//
// NOTE: TODO: We don't have any separate class for ConstRowBlock and ConstColumnBlock
// vs RowBlock and ColumnBlock. So, we use "const" in various places to either
// mean const-ness of the wrapper structure vs const-ness of the referred-to data.
// Typically in C++ this is done with separate wrapper classes for const vs non-const
// referred-to data, but that would require a lot of duplication elsewhere in the code,
// so for now, we just use convention: if you have a RowBlock or ColumnBlock parameter
// that you expect to be modifying, use a "RowBlock *param". Otherwise, use a
// "const RowBlock& param". Just because you _could_ modify the referred-to contents
// of the latter doesn't mean you _should_.
class RowBlock {
 public:
  RowBlock(const Schema &schema,
           size_t nrows,
           Arena *arena);
  ~RowBlock();

  // Resize the block to the given number of rows.
  // This size must be <= the the allocated capacity row_capacity().
  //
  // Ensures that all rows for indices < n_rows are unmodified.
  void Resize(size_t n_rows);

  size_t row_capacity() const {
    return row_capacity_;
  }

  RowBlockRow row(size_t idx) const;

  const Schema &schema() const { return schema_; }
  Arena *arena() const { return arena_; }

  ColumnBlock column_block(size_t col_idx) const {
    return column_block(col_idx, nrows_);
  }

  ColumnBlock column_block(size_t col_idx, size_t nrows) const {
    DCHECK_LE(nrows, nrows_);

    const ColumnSchema& col_schema = schema_.column(col_idx);
    uint8_t *col_data = columns_data_[col_idx];
    uint8_t *nulls_bitmap = column_null_bitmaps_[col_idx];

    return ColumnBlock(col_schema.type_info(), nulls_bitmap, col_data, nrows, arena_);
  }

  // Return the base pointer for the given column's data.
  //
  // This is used by the codegen code in preference to the "nicer" column_block APIs,
  // because the codegenned code knows the column's type sizes statically, and thus
  // computing the pointers by itself ends up avoiding a multiply instruction.
  uint8_t* column_data_base_ptr(size_t col_idx) const {
    DCHECK_LT(col_idx, columns_data_.size());
    return columns_data_[col_idx];
  }

  // Return the number of rows in the row block. Note that this includes
  // rows which were filtered out by the selection vector.
  size_t nrows() const { return nrows_; }

  // Zero the memory pointed to by this row block.
  // This physically zeros the memory, so is not efficient - mostly useful
  // from unit tests.
  void ZeroMemory() {
    size_t bitmap_size = BitmapSize(row_capacity_);
    for (size_t i = 0; i < schema_.num_columns(); ++i) {
      const ColumnSchema& col_schema = schema_.column(i);
      size_t col_size = col_schema.type_info()->size() * row_capacity_;
      memset(columns_data_[i], '\0', col_size);

      if (column_null_bitmaps_[i] != NULL) {
        memset(column_null_bitmaps_[i], '\0', bitmap_size);
      }
    }
  }

  // Return the selection vector which indicates which rows have passed
  // predicates so far during evaluation of this block of rows.
  //
  // At the beginning of each batch, the vector is set to all 1s, and
  // as predicates or deletions make rows invalid, they are set to 0s.
  // After a batch has completed, only those rows with associated true
  // bits in the selection vector are valid results for the scan.
  SelectionVector *selection_vector() {
    return &sel_vec_;
  }

  const SelectionVector *selection_vector() const {
    return &sel_vec_;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(RowBlock);

  static size_t RowBlockSize(const Schema& schema, size_t nrows) {
    size_t block_size = schema.num_columns() * sizeof(size_t);
    size_t bitmap_size = BitmapSize(nrows);
    for (size_t col = 0; col < schema.num_columns(); col++) {
      const ColumnSchema& col_schema = schema.column(col);
      block_size += nrows * col_schema.type_info()->size();
      if (col_schema.is_nullable())
        block_size += bitmap_size;
    }
    return block_size;
  }

  Schema schema_;
  std::vector<uint8_t *> columns_data_;
  std::vector<uint8_t *> column_null_bitmaps_;

  // The maximum number of rows that can be stored in our allocated buffer.
  size_t row_capacity_;

  // The number of rows currently being processed in this block.
  // nrows_ <= row_capacity_
  size_t nrows_;

  Arena *arena_;

  // The bitmap indicating which rows are valid in this block.
  // Deleted rows or rows which have failed to pass predicates will be zeroed
  // in the bitmap, and thus not returned to the end user.
  SelectionVector sel_vec_;
};

// Provides an abstraction to interact with a RowBlock row.
//  Example usage:
//    RowBlock row_block(schema, 10, NULL);
//    RowBlockRow row = row_block.row(5);       // Get row 5
//    void *cell_data = row.cell_ptr(3);        // Get column 3 of row 5
//    ...
class RowBlockRow {
 public:
  typedef ColumnBlock::Cell Cell;

  explicit RowBlockRow(const RowBlock *row_block = NULL, size_t row_index = 0)
    : row_block_(row_block), row_index_(row_index) {
  }

  RowBlockRow *Reset(const RowBlock *row_block, size_t row_index) {
    row_block_ = row_block;
    row_index_ = row_index;
    return this;
  }

  const RowBlock* row_block() const {
    return row_block_;
  }

  size_t row_index() const {
    return row_index_;
  }

  const Schema* schema() const {
    return &row_block_->schema();
  }

  bool is_null(size_t col_idx) const {
    return column_block(col_idx).is_null(row_index_);
  }

  uint8_t *mutable_cell_ptr(size_t col_idx) const {
    return const_cast<uint8_t*>(cell_ptr(col_idx));
  }

  const uint8_t *cell_ptr(size_t col_idx) const {
    return column_block(col_idx).cell_ptr(row_index_);
  }

  const uint8_t *nullable_cell_ptr(size_t col_idx) const {
    return column_block(col_idx).nullable_cell_ptr(row_index_);
  }

  Cell cell(size_t col_idx) const {
    return row_block_->column_block(col_idx).cell(row_index_);
  }

  ColumnBlock column_block(size_t col_idx) const {
    return row_block_->column_block(col_idx);
  }

  // Mark this row as unselected in the selection vector.
  void SetRowUnselected() {
    // TODO: const-ness issues since this class holds a const RowBlock *.
    // hack around this for now
    SelectionVector *vec = const_cast<SelectionVector *>(row_block_->selection_vector());
    vec->SetRowUnselected(row_index_);
  }

#ifndef NDEBUG
  void OverwriteWithPattern(StringPiece pattern) {
    const Schema& schema = row_block_->schema();
    for (size_t col = 0; col < schema.num_columns(); col++) {
      row_block_->column_block(col).OverwriteWithPattern(row_index_, pattern);
    }
  }
#endif

 private:
  const RowBlock *row_block_;
  size_t row_index_;
};

inline RowBlockRow RowBlock::row(size_t idx) const {
  return RowBlockRow(this, idx);
}

} // namespace kudu

#endif
