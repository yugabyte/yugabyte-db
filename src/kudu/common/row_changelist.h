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
// Row changelists are simply an encoded form of a list of updates to columns
// within a row. These are stored within the delta memstore and delta files.
#ifndef KUDU_COMMON_ROW_CHANGELIST_H
#define KUDU_COMMON_ROW_CHANGELIST_H

#include <gtest/gtest_prod.h>
#include <string>
#include <vector>

#include "kudu/common/row.h"
#include "kudu/gutil/casts.h"
#include "kudu/util/bitmap.h"

namespace kudu {

class faststring;

class Arena;
class ColumnBlock;
class RowBlockRow;
class Schema;

// A RowChangeList is a wrapper around a Slice which contains a "changelist".
//
// A changelist is a single mutation to a row -- it may be one of three types:
//  - UPDATE (set a new value for one or more columns)
//  - DELETE (remove the row)
//  - REINSERT (re-insert a "ghost" row, used only in the MemRowSet)
//
// RowChangeLists should be constructed using RowChangeListEncoder, and read
// using RowChangeListDecoder. NOTE that the schema passed to the Decoder must
// be the same one used by the Encoder.
//
// The actual serialization format is as follows:
//
//   The first byte indicates the RCL type. The values are specified by
//   ChangeType below.
//
//   If type == kDelete, then no further data follows. The row is deleted.
//
//   If type == kReinsert, then a "tuple-format" row follows. TODO: this will
//     be changed by http://gerrit.sjc.cloudera.com:8080/#/c/6318/ in the near future.
//
//   If type == kUpdate, then a sequence of column updates follow. Each update
//   has the format:
//
//     <column id>  -- varint32
//       The ID of the column to be updated.
//
//     <length+1>   -- varint32
//       If 0, then indicates that the column is to be set to NULL.
//       Otherwise, encodes the actual data length + 1.
//
//     <value>      -- length determined by previous field
//       The value to which to set the column. In the case of a STRING field,
//       this is the actual string value. Otherwise, this is a fixed-length
//       field whose length is determined by the type.
//
// A few examples follow:
//
// 1) UPDATE SET [col_id 2] = "hello"
//   0x01    0x02      0x06          h e l l o
//   UPDATE  col_id=2  len(hello)+1  <raw value>
//
// 2) UPDATE SET [col_id 3] = 33  (assuming INT32 column)
//   0x01    0x03      0x05              0x21 0x00 0x00 0x00
//   UPDATE  col_id=3  sizeof(int32)+1   <raw little-endian value>
//
// 3) UPDATE SET [col id 3] = NULL
//   0x01    0x03      0x00
//   UPDATE  col_id=3  NULL
//
class RowChangeList {
 public:
  RowChangeList() {}

  explicit RowChangeList(const faststring &fs)
    : encoded_data_(fs) {
  }

  explicit RowChangeList(Slice s) : encoded_data_(std::move(s)) {}

  // Create a RowChangeList which represents a delete.
  // This points to static (const) memory and should not be
  // mutated or freed.
  static RowChangeList CreateDelete() {
    return RowChangeList(Slice("\x02"));
  }

  const Slice &slice() const { return encoded_data_; }

  // Return a string form of this changelist.
  string ToString(const Schema &schema) const;

  bool is_reinsert() const {
    DCHECK_GT(encoded_data_.size(), 0);
    return encoded_data_[0] == kReinsert;
  }

  bool is_delete() const {
    DCHECK_GT(encoded_data_.size(), 0);
    return encoded_data_[0] == kDelete;
  }

  bool is_null() const {
    return encoded_data_.size() == 0;
  }

  enum ChangeType {
    ChangeType_min = 0,
    kUninitialized = 0,
    kUpdate = 1,
    kDelete = 2,
    kReinsert = 3,
    ChangeType_max = 3
  };

  Slice encoded_data_;
};

class RowChangeListEncoder {
 public:
  // Construct a new encoder.
  explicit RowChangeListEncoder(faststring *dst) :
    type_(RowChangeList::kUninitialized),
    dst_(dst)
  {}

  void Reset() {
    dst_->clear();
    type_ = RowChangeList::kUninitialized;
  }

  void SetToDelete() {
    SetType(RowChangeList::kDelete);
  }

  // TODO: This doesn't currently copy the indirected data, so
  // REINSERT deltas can't possibly work anywhere but in memory.
  // For now, there is an assertion in the DeltaFile flush code
  // that prevents us from accidentally depending on this anywhere
  // but in-memory.
  void SetToReinsert(const Slice &row_data) {
    SetType(RowChangeList::kReinsert);
    dst_->append(row_data.data(), row_data.size());
  }

  // Add a column update, given knowledge of the schema.
  //
  // If 'cell_ptr' is NULL, then 'col_schema' must refer to a nullable
  // column, and we encode SET [col]=NULL.
  //
  // Otherwise, 'cell_ptr' should point to the in-memory format for the
  // appropriate type. For example, for a STRING column, 'cell_ptr'
  // should be a Slice*.
  void AddColumnUpdate(const ColumnSchema& col_schema,
                       int col_id,
                       const void* cell_ptr);


  RowChangeList as_changelist() {
    DCHECK_GT(dst_->size(), 0);
    return RowChangeList(*dst_);
  }

  bool is_initialized() const {
    return type_ != RowChangeList::kUninitialized;
  }

  bool is_empty() {
    return dst_->size() == 0;
  }

 private:
  FRIEND_TEST(TestRowChangeList, TestInvalid_SetNullForNonNullableColumn);
  FRIEND_TEST(TestRowChangeList, TestInvalid_SetWrongSizeForIntColumn);
  friend class RowChangeListDecoder;

  void SetType(RowChangeList::ChangeType type) {
    DCHECK_EQ(type_, RowChangeList::kUninitialized);
    type_ = type;
    dst_->push_back(type);
  }

  // Add a column update by a raw value. This allows copying RCLs
  // from one file to another without having any awareness of schema.
  //
  // If 'is_null' is set, then encodes a SET [col_id]=NULL update.
  // Otherwise, SET [col_id] = 'new_val'.
  //
  // 'new_val' is the encoded form of the new value. In the case of
  // a STRING, this is the actual user-provided STRING. Otherwise,
  // it is the fixed-length representation of the type.
  void AddRawColumnUpdate(int col_id, bool is_null, Slice new_val);


  RowChangeList::ChangeType type_;
  faststring *dst_;
};


class RowChangeListDecoder {
 public:

  // Construct a new decoder.
  explicit RowChangeListDecoder(const RowChangeList &src)
    : remaining_(src.slice()),
      type_(RowChangeList::kUninitialized) {
  }

  // Initialize the decoder. This will return an invalid Status if the RowChangeList
  // appears to be corrupt/malformed.
  Status Init();

  // Like Init() above, but does not perform any safety checks in a release build.
  // This can be used when it's known that the source of the RowChangeList is
  // guaranteed to be non-corrupt (e.g. we created it and have kept it in memory).
  void InitNoSafetyChecks() {
#ifndef NDEBUG
    Status s = Init();
    DCHECK(s.ok()) << s.ToString();
#else
    type_ = static_cast<RowChangeList::ChangeType>(remaining_[0]);
    remaining_.remove_prefix(1);
#endif
  }

  bool HasNext() const {
    DCHECK(!is_delete());
    return !remaining_.empty();
  }

  bool is_update() const {
    return type_ == RowChangeList::kUpdate;
  }

  bool is_delete() const {
    return type_ == RowChangeList::kDelete;
  }

  bool is_reinsert() const {
    return type_ == RowChangeList::kReinsert;
  }

  // If this RCL is a REINSERT, then returns the reinserted row in
  // the contiguous in-memory row format.
  Status GetReinsertedRowSlice(const Schema& schema, Slice* s) const;

  // Append an entry to *column_ids for each column that is updated
  // in this RCL.
  // This 'consumes' the remainder of the encoded RowChangeList.
  Status GetIncludedColumnIds(std::vector<ColumnId>* column_ids) {
    column_ids->clear();
    DCHECK(is_update());
    while (HasNext()) {
      DecodedUpdate dec;
      RETURN_NOT_OK(DecodeNext(&dec));
      column_ids->push_back(dec.col_id);
    }
    return Status::OK();
  }

  // Applies changes in this decoder to the specified row and saves the old
  // state of the row into the undo_encoder.
  Status ApplyRowUpdate(RowBlockRow *dst_row,
                        Arena *arena, RowChangeListEncoder* undo_encoder);

  // Apply this UPDATE RowChangeList to row number 'row_idx' in 'dst_col', but only
  // any updates that correspond to column 'col_idx' of 'dst_schema'.
  // Any indirect data is copied into 'arena' if non-NULL.
  //
  // REQUIRES: is_update()
  Status ApplyToOneColumn(size_t row_idx, ColumnBlock* dst_col,
                          const Schema& dst_schema, int col_idx, Arena *arena);

  // If this changelist is a DELETE or REINSERT, twiddle '*deleted' to reference
  // the new state of the row. If it is an UPDATE, this call has no effect.
  //
  // This is used during mutation traversal, to keep track of whether a row is
  // deleted or not.
  void TwiddleDeleteStatus(bool *deleted) {
    if (is_delete()) {
      DCHECK(!*deleted);
      *deleted = true;
    } else if (is_reinsert()) {
      DCHECK(*deleted);
      *deleted = false;
    }
  }

  // Project the 'src' RowChangeList using the delta 'projector'
  // The projected RowChangeList will be encoded to specified 'buf'.
  // The buffer will be cleared before adding the result.
  static Status ProjectUpdate(const DeltaProjector& projector,
                              const RowChangeList& src,
                              faststring *buf);

  // If 'src' is an update, then only add changes for columns NOT
  // specified by 'column_indexes' to 'out'. Delete and Re-insert
  // changes are added to 'out' as-is. If an update only contained
  // changes for 'column_indexes', then out->is_initialized() will
  // return false.
  // 'column_ids' must be sorted; 'out' must be
  // valid for the duration of this method, but not have been
  // previously initialized.
  static Status RemoveColumnIdsFromChangeList(const RowChangeList& src,
                                              const std::vector<ColumnId>& column_ids,
                                              RowChangeListEncoder* out);

  struct DecodedUpdate {
    // The updated column ID.
    ColumnId col_id;

    // If true, this update sets the given column to NULL.
    bool null;

    // The "raw" value of the updated column.
    //   - in the case of a fixed length type such as an integer,
    //     the slice will point directly to the new little-endian value.
    //   - in the case of a variable length type such as a string,
    //     the slice will point to the new string value (i.e not to a
    //     "wrapper" slice.
    // 'raw_value' is only relevant in the case that 'null' is not true.
    Slice raw_value;

    // Resolve the decoded update against the given Schema.
    //
    // If the updated column is present in the schema, and the decoded
    // update has the correct length/type, then sets
    // *col_idx, and sets *valid_value to point to the validated
    // value.
    //
    // If the updated column is present, but the data provided is invalid,
    // returns Status::Corruption.
    //
    // If the updated column is not present, sets *col_idx to -1 and returns
    // Status::OK.
    Status Validate(const Schema& s,
                    int* col_idx,
                    const void** valid_value) const;
  };

  // Decode the next updated column into '*update'.
  // See the docs on DecodedUpdate above for field information.
  //
  // The update->raw_value slice points to memory within the buffer
  // being decoded by this object. No copies are made.
  //
  // REQUIRES: is_update()
  Status DecodeNext(DecodedUpdate* update);

 private:
  FRIEND_TEST(TestRowChangeList, TestEncodeDecodeUpdates);
  friend class RowChangeList;

  // Data remaining in the source buffer.
  // This slice is advanced forward as entries are decoded.
  Slice remaining_;

  RowChangeList::ChangeType type_;
};


} // namespace kudu

// Defined for tight_enum_test_cast<> -- has to be defined outside of any namespace.
MAKE_ENUM_LIMITS(kudu::RowChangeList::ChangeType,
                 kudu::RowChangeList::ChangeType_min,
                 kudu::RowChangeList::ChangeType_max);

#endif
