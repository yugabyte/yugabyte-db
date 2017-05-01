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
#ifndef YB_CLIENT_SCAN_BATCH_H
#define YB_CLIENT_SCAN_BATCH_H

#include <string>

#ifdef YB_HEADERS_NO_STUBS
#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"
#else
#include "yb/client/stubs.h"
#endif

#include "yb/util/yb_export.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"

namespace yb {
class Schema;

namespace tools {
class TsAdminClient;
} // namespace tools

namespace client {
class YBSchema;

// A batch of zero or more rows returned from a YBScanner.
//
// With C++11, you can iterate over the rows in the batch using a
// range-foreach loop:
//
//   for (YBScanBatch::RowPtr row : batch) {
//     ... row.GetInt(1, ...)
//     ...
//   }
//
// In C++03, you'll need to use a regular for loop:
//
//   for (int i = 0, num_rows = batch.NumRows();
//        i < num_rows;
//        i++) {
//     YBScanBatch::RowPtr row = batch.Row(i);
//     ...
//   }
//
// Note that, in the above example, NumRows() is only called once at the
// beginning of the loop to avoid extra calls to the non-inlined method.
class YB_EXPORT YBScanBatch {
 public:
  class RowPtr;
  class const_iterator;
  typedef RowPtr value_type;

  YBScanBatch();
  ~YBScanBatch();

  // Return the number of rows in this batch.
  int NumRows() const;

  // Return a reference to one of the rows in this batch.
  // The returned object is only valid for as long as this YBScanBatch.
  YBScanBatch::RowPtr Row(int idx) const;

  const_iterator begin() const;
  const_iterator end() const;

  // Returns the projection schema for this batch.
  // All YBScanBatch::RowPtr returned by this batch are guaranteed to have this schema.
  const YBSchema* projection_schema() const;

 private:
  class YB_NO_EXPORT Data;
  friend class YBScanner;
  friend class yb::tools::TsAdminClient;

  Data* data_;
  DISALLOW_COPY_AND_ASSIGN(YBScanBatch);
};

// A single row result from a scan. Note that this object acts as a pointer into
// a YBScanBatch, and therefore is valid only as long as the batch it was constructed
// from.
class YB_EXPORT YBScanBatch::RowPtr {
 public:
  // Construct an invalid RowPtr. Before use, you must assign
  // a properly-initialized value.
  RowPtr() : schema_(NULL), row_data_(NULL) {}

  bool IsNull(const Slice& col_name) const;
  bool IsNull(int col_idx) const;

  // These getters return a bad Status if the type does not match,
  // the value is unset, or the value is NULL. Otherwise they return
  // the current set value in *val.
  CHECKED_STATUS GetBool(const Slice& col_name, bool* val) const WARN_UNUSED_RESULT;

  CHECKED_STATUS GetInt8(const Slice& col_name, int8_t* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetInt16(const Slice& col_name, int16_t* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetInt32(const Slice& col_name, int32_t* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetInt64(const Slice& col_name, int64_t* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetTimestamp(const Slice& col_name, int64_t* micros_since_utc_epoch)
    const WARN_UNUSED_RESULT;

  CHECKED_STATUS GetFloat(const Slice& col_name, float* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetDouble(const Slice& col_name, double* val) const WARN_UNUSED_RESULT;

  // Same as above getters, but with numeric column indexes.
  // These are faster since they avoid a hashmap lookup, so should
  // be preferred in performance-sensitive code.
  CHECKED_STATUS GetBool(int col_idx, bool* val) const WARN_UNUSED_RESULT;

  CHECKED_STATUS GetInt8(int col_idx, int8_t* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetInt16(int col_idx, int16_t* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetInt32(int col_idx, int32_t* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetInt64(int col_idx, int64_t* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetTimestamp(int col_idx,
                              int64_t* micros_since_utc_epoch) const WARN_UNUSED_RESULT;

  CHECKED_STATUS GetFloat(int col_idx, float* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetDouble(int col_idx, double* val) const WARN_UNUSED_RESULT;

  // Gets the string/binary value but does not copy the value. Callers should
  // copy the resulting Slice if necessary.
  CHECKED_STATUS GetString(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetString(int col_idx, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetBinary(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetBinary(int col_idx, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetInet(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetInet(int col_idx, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetUuid(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetUuid(int col_idx, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetTimeUuid(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetTimeUuid(int col_idx, Slice* val) const WARN_UNUSED_RESULT;

  // Raw cell access. Should be avoided unless absolutely necessary.
  const void* cell(int col_idx) const;

  const YBSchema* row_schema() const;

  std::string ToString() const;

 private:
  friend class YBScanBatch;
  template<typename KeyTypeWrapper> friend struct SliceKeysTestSetup;
  template<typename KeyTypeWrapper> friend struct IntKeysTestSetup;

  // Only invoked by YBScanner.
  RowPtr(const Schema* schema,
         const YBSchema* client_projection,
         const uint8_t* row_data)
      : schema_(schema),
        client_schema_(client_projection),
        row_data_(row_data) {
  }

  template<typename T>
  CHECKED_STATUS Get(const Slice& col_name, typename T::cpp_type* val) const;

  template<typename T>
  CHECKED_STATUS Get(int col_idx, typename T::cpp_type* val) const;

  const Schema* schema_;
  const YBSchema* client_schema_;
  const uint8_t* row_data_;
};

// C++ forward iterator over the rows in a YBScanBatch.
//
// This iterator yields YBScanBatch::RowPtr objects which point inside the row batch
// itself. Thus, the iterator and any objects obtained from it are invalidated if the
// YBScanBatch is destroyed or used for a new NextBatch() call.
class YB_EXPORT YBScanBatch::const_iterator
    : public std::iterator<std::forward_iterator_tag, YBScanBatch::RowPtr> {
 public:
  ~const_iterator() {}

  YBScanBatch::RowPtr operator*() const {
    return batch_->Row(idx_);
  }

  void operator++() {
    idx_++;
  }

  bool operator==(const const_iterator& other) {
    return idx_ == other.idx_;
  }
  bool operator!=(const const_iterator& other) {
    return idx_ != other.idx_;
  }

 private:
  friend class YBScanBatch;
  const_iterator(const YBScanBatch* b, int idx)
      : batch_(b),
        idx_(idx) {
  }

  const YBScanBatch* batch_;
  int idx_;
};


inline YBScanBatch::const_iterator YBScanBatch::begin() const {
  return const_iterator(this, 0);
}

inline YBScanBatch::const_iterator YBScanBatch::end() const {
  return const_iterator(this, NumRows());
}

} // namespace client
} // namespace yb

#endif // YB_CLIENT_SCAN_BATCH_H
