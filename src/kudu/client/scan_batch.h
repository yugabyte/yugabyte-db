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
#ifndef KUDU_CLIENT_SCAN_BATCH_H
#define KUDU_CLIENT_SCAN_BATCH_H

#include <string>

#ifdef KUDU_HEADERS_NO_STUBS
#include "kudu/gutil/macros.h"
#include "kudu/gutil/port.h"
#else
#include "kudu/client/stubs.h"
#endif

#include "kudu/util/kudu_export.h"
#include "kudu/util/slice.h"

namespace kudu {
class Schema;

namespace tools {
class TsAdminClient;
} // namespace tools

namespace client {
class KuduSchema;

// A batch of zero or more rows returned from a KuduScanner.
//
// With C++11, you can iterate over the rows in the batch using a
// range-foreach loop:
//
//   for (KuduScanBatch::RowPtr row : batch) {
//     ... row.GetInt(1, ...)
//     ...
//   }
//
// In C++03, you'll need to use a regular for loop:
//
//   for (int i = 0, num_rows = batch.NumRows();
//        i < num_rows;
//        i++) {
//     KuduScanBatch::RowPtr row = batch.Row(i);
//     ...
//   }
//
// Note that, in the above example, NumRows() is only called once at the
// beginning of the loop to avoid extra calls to the non-inlined method.
class KUDU_EXPORT KuduScanBatch {
 public:
  class RowPtr;
  class const_iterator;
  typedef RowPtr value_type;

  KuduScanBatch();
  ~KuduScanBatch();

  // Return the number of rows in this batch.
  int NumRows() const;

  // Return a reference to one of the rows in this batch.
  // The returned object is only valid for as long as this KuduScanBatch.
  KuduScanBatch::RowPtr Row(int idx) const;

  const_iterator begin() const;
  const_iterator end() const;

  // Returns the projection schema for this batch.
  // All KuduScanBatch::RowPtr returned by this batch are guaranteed to have this schema.
  const KuduSchema* projection_schema() const;

 private:
  class KUDU_NO_EXPORT Data;
  friend class KuduScanner;
  friend class kudu::tools::TsAdminClient;

  Data* data_;
  DISALLOW_COPY_AND_ASSIGN(KuduScanBatch);
};

// A single row result from a scan. Note that this object acts as a pointer into
// a KuduScanBatch, and therefore is valid only as long as the batch it was constructed
// from.
class KUDU_EXPORT KuduScanBatch::RowPtr {
 public:
  // Construct an invalid RowPtr. Before use, you must assign
  // a properly-initialized value.
  RowPtr() : schema_(NULL), row_data_(NULL) {}

  bool IsNull(const Slice& col_name) const;
  bool IsNull(int col_idx) const;

  // These getters return a bad Status if the type does not match,
  // the value is unset, or the value is NULL. Otherwise they return
  // the current set value in *val.
  Status GetBool(const Slice& col_name, bool* val) const WARN_UNUSED_RESULT;

  Status GetInt8(const Slice& col_name, int8_t* val) const WARN_UNUSED_RESULT;
  Status GetInt16(const Slice& col_name, int16_t* val) const WARN_UNUSED_RESULT;
  Status GetInt32(const Slice& col_name, int32_t* val) const WARN_UNUSED_RESULT;
  Status GetInt64(const Slice& col_name, int64_t* val) const WARN_UNUSED_RESULT;
  Status GetTimestamp(const Slice& col_name, int64_t* micros_since_utc_epoch)
    const WARN_UNUSED_RESULT;

  Status GetFloat(const Slice& col_name, float* val) const WARN_UNUSED_RESULT;
  Status GetDouble(const Slice& col_name, double* val) const WARN_UNUSED_RESULT;

  // Same as above getters, but with numeric column indexes.
  // These are faster since they avoid a hashmap lookup, so should
  // be preferred in performance-sensitive code.
  Status GetBool(int col_idx, bool* val) const WARN_UNUSED_RESULT;

  Status GetInt8(int col_idx, int8_t* val) const WARN_UNUSED_RESULT;
  Status GetInt16(int col_idx, int16_t* val) const WARN_UNUSED_RESULT;
  Status GetInt32(int col_idx, int32_t* val) const WARN_UNUSED_RESULT;
  Status GetInt64(int col_idx, int64_t* val) const WARN_UNUSED_RESULT;
  Status GetTimestamp(int col_idx, int64_t* micros_since_utc_epoch) const WARN_UNUSED_RESULT;

  Status GetFloat(int col_idx, float* val) const WARN_UNUSED_RESULT;
  Status GetDouble(int col_idx, double* val) const WARN_UNUSED_RESULT;

  // Gets the string/binary value but does not copy the value. Callers should
  // copy the resulting Slice if necessary.
  Status GetString(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  Status GetString(int col_idx, Slice* val) const WARN_UNUSED_RESULT;
  Status GetBinary(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  Status GetBinary(int col_idx, Slice* val) const WARN_UNUSED_RESULT;

  // Raw cell access. Should be avoided unless absolutely necessary.
  const void* cell(int col_idx) const;

  const KuduSchema* row_schema() const;

  std::string ToString() const;

 private:
  friend class KuduScanBatch;
  template<typename KeyTypeWrapper> friend struct SliceKeysTestSetup;
  template<typename KeyTypeWrapper> friend struct IntKeysTestSetup;

  // Only invoked by KuduScanner.
  RowPtr(const Schema* schema,
         const KuduSchema* client_projection,
         const uint8_t* row_data)
      : schema_(schema),
        client_schema_(client_projection),
        row_data_(row_data) {
  }

  template<typename T>
  Status Get(const Slice& col_name, typename T::cpp_type* val) const;

  template<typename T>
  Status Get(int col_idx, typename T::cpp_type* val) const;

  const Schema* schema_;
  const KuduSchema* client_schema_;
  const uint8_t* row_data_;
};

// C++ forward iterator over the rows in a KuduScanBatch.
//
// This iterator yields KuduScanBatch::RowPtr objects which point inside the row batch
// itself. Thus, the iterator and any objects obtained from it are invalidated if the
// KuduScanBatch is destroyed or used for a new NextBatch() call.
class KUDU_EXPORT KuduScanBatch::const_iterator
    : public std::iterator<std::forward_iterator_tag, KuduScanBatch::RowPtr> {
 public:
  ~const_iterator() {}

  KuduScanBatch::RowPtr operator*() const {
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
  friend class KuduScanBatch;
  const_iterator(const KuduScanBatch* b, int idx)
      : batch_(b),
        idx_(idx) {
  }

  const KuduScanBatch* batch_;
  int idx_;
};


inline KuduScanBatch::const_iterator KuduScanBatch::begin() const {
  return const_iterator(this, 0);
}

inline KuduScanBatch::const_iterator KuduScanBatch::end() const {
  return const_iterator(this, NumRows());
}

} // namespace client
} // namespace kudu

#endif
