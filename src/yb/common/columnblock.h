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

#include "yb/common/types.h"
#include "yb/util/bitmap.h"
#include "yb/util/memory/arena.h"
#include "yb/util/status_fwd.h"

namespace yb {

class ColumnBlockCell;

// A block of data all belonging to a single column.
// This is simply a view into a buffer - it does not have any associated
// storage in and of itself. It does, however, maintain its type
// information, which can be used for extra type safety in debug mode.
class ColumnBlock {
 public:
  typedef ColumnBlockCell Cell;

  ColumnBlock(const TypeInfo* type,
              uint8_t *null_bitmap,
              void *data,
              size_t nrows,
              Arena *arena)
    : type_(type),
      null_bitmap_(null_bitmap),
      data_(reinterpret_cast<uint8_t *>(data)),
      nrows_(nrows),
      arena_(arena) {
    DCHECK(data_) << "null data";
  }

  void SetCellIsNull(size_t idx, bool is_null) {
    DCHECK(is_nullable());
    BitmapChange(null_bitmap_, idx, !is_null);
  }

  void SetCellValue(size_t idx, const void *new_val) {
    strings::memcpy_inlined(mutable_cell_ptr(idx), new_val, type_->size());
  }

#ifndef NDEBUG
  void OverwriteWithPattern(size_t idx, GStringPiece pattern) {
    char *col_data = reinterpret_cast<char *>(mutable_cell_ptr(idx));
    yb::OverwriteWithPattern(col_data, type_->size(), pattern);
  }
#endif

  // Return a pointer to the given cell.
  const uint8_t *cell_ptr(size_t idx) const {
    DCHECK_LT(idx, nrows_);
    return data_ + type_->size() * idx;
  }

  // Returns a pointer to the given cell or NULL.
  const uint8_t *nullable_cell_ptr(size_t idx) const {
    return is_null(idx) ? NULL : cell_ptr(idx);
  }

  Cell cell(size_t idx) const;

  uint8_t *null_bitmap() const {
    return null_bitmap_;
  }

  bool is_nullable() const {
    return null_bitmap_ != NULL;
  }

  bool is_null(size_t idx) const {
    DCHECK(is_nullable());
    DCHECK_LT(idx, nrows_);
    return !BitmapTest(null_bitmap_, idx);
  }

  const size_t stride() const { return type_->size(); }
  const uint8_t * data() const { return data_; }
  uint8_t *data() { return data_; }
  const size_t nrows() const { return nrows_; }

  Arena *arena() { return arena_; }

  const TypeInfo* type_info() const {
    return type_;
  }

 private:
  friend class ColumnBlockCell;
  friend class ColumnDataView;

  // Return a pointer to the given cell.
  uint8_t *mutable_cell_ptr(size_t idx) {
    DCHECK_LT(idx, nrows_);
    return data_ + type_->size() * idx;
  }

  const TypeInfo *type_;
  uint8_t *null_bitmap_;

  uint8_t *data_;
  size_t nrows_;

  Arena *arena_;
};

// One of the cells in a ColumnBlock.
class ColumnBlockCell {
 public:
  ColumnBlockCell(ColumnBlock block, size_t row_idx)
      : block_(std::move(block)), row_idx_(row_idx) {}

  const TypeInfo* typeinfo() const { return block_.type_info(); }
  size_t size() const { return block_.type_info()->size(); }
  const void* ptr() const {
    return is_nullable() ? block_.nullable_cell_ptr(row_idx_)
      : block_.cell_ptr(row_idx_);
  }
  void* mutable_ptr() { return block_.mutable_cell_ptr(row_idx_); }
  bool is_nullable() const { return block_.is_nullable(); }
  bool is_null() const { return block_.is_null(row_idx_); }
  void set_null(bool is_null) { block_.SetCellIsNull(row_idx_, is_null); }
 protected:
  ColumnBlock block_;
  size_t row_idx_;
};

inline ColumnBlockCell ColumnBlock::cell(size_t idx) const {
  return ColumnBlockCell(*this, idx);
}

// Wrap the ColumnBlock to expose a directly raw block at the specified offset.
// Used by the reader and block encoders to read/write raw data.
class ColumnDataView {
 public:
  explicit ColumnDataView(ColumnBlock *column_block, size_t first_row_idx = 0)
    : column_block_(column_block), row_offset_(0) {
    Advance(first_row_idx);
  }

  void Advance(size_t skip) {
    // Check <= here, not <, since you can skip to
    // the very end of the data (leaving an empty block)
    DCHECK_LE(skip, column_block_->nrows());
    row_offset_ += skip;
  }

  size_t first_row_index() const {
    return row_offset_;
  }

  // Set 'nrows' bits of the null-bitmap to "value"
  // true if not null, false if null.
  void SetNullBits(size_t nrows, bool value) {
    BitmapChangeBits(column_block_->null_bitmap(), row_offset_, nrows, value);
  }

  uint8_t *data() {
    return column_block_->mutable_cell_ptr(row_offset_);
  }

  const uint8_t *data() const {
    return column_block_->cell_ptr(row_offset_);
  }

  Arena *arena() { return column_block_->arena(); }

  size_t nrows() const {
    return column_block_->nrows() - row_offset_;
  }

  const size_t stride() const {
    return column_block_->stride();
  }

  const TypeInfo* type_info() const {
    return column_block_->type_info();
  }

 private:
  ColumnBlock *column_block_;
  size_t row_offset_;
};

// Utility class which allocates temporary storage for a
// dense block of column data, freeing it when it goes
// out of scope.
//
// This is more useful in test code than production code,
// since it doesn't allocate from an arena, etc.
template<DataType type>
class ScopedColumnBlock : public ColumnBlock {
 public:
  typedef typename TypeTraits<type>::cpp_type cpp_type;

  explicit ScopedColumnBlock(size_t n_rows)
    : ColumnBlock(GetTypeInfo(type),
                  new uint8_t[BitmapSize(n_rows)],
                  new cpp_type[n_rows],
                  n_rows,
                  new Arena(1024, 1*1024*1024)),
      null_bitmap_(null_bitmap()),
      data_(reinterpret_cast<cpp_type *>(data())),
      arena_(arena()) {
  }

  const cpp_type &operator[](size_t idx) const {
    return data_[idx];
  }

  cpp_type &operator[](size_t idx) {
    return data_[idx];
  }

 private:
  std::unique_ptr<uint8_t[]> null_bitmap_;
  std::unique_ptr<cpp_type[]> data_;
  std::unique_ptr<Arena> arena_;

};

} // namespace yb
