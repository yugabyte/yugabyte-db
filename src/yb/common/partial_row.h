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
#ifndef YB_COMMON_PARTIAL_ROW_H
#define YB_COMMON_PARTIAL_ROW_H

#include <stdint.h>
#include <string>
#include <vector>

#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"

#include "yb/util/slice.h"
#include "yb/util/status_fwd.h"

namespace yb {
class ColumnSchema;
namespace client {
template<typename KeyTypeWrapper> struct SliceKeysTestSetup;
template<typename KeyTypeWrapper> struct IntKeysTestSetup;
} // namespace client

class Schema;

// A row which may only contain values for a subset of the columns.
// This type contains a normal contiguous row, plus a bitfield indicating
// which columns have been set. Additionally, this type may optionally own
// copies of indirect data for variable length columns.
class YBPartialRow {
 public:
  // The given Schema object must remain valid for the lifetime of this
  // row.
  explicit YBPartialRow(const Schema* schema);
  virtual ~YBPartialRow();

  YBPartialRow(const YBPartialRow& other);

  YBPartialRow& operator=(YBPartialRow other);

  //------------------------------------------------------------
  // Setters
  //------------------------------------------------------------

  CHECKED_STATUS SetBool(const Slice& col_name, bool val) WARN_UNUSED_RESULT;

  CHECKED_STATUS SetInt8(const Slice& col_name, int8_t val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetInt16(const Slice& col_name, int16_t val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetInt32(const Slice& col_name, int32_t val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetInt64(const Slice& col_name, int64_t val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetTimestamp(const Slice& col_name,
                              int64_t micros_since_utc_epoch) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetFloat(const Slice& col_name, float val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetDouble(const Slice& col_name, double val) WARN_UNUSED_RESULT;

  // Same as above setters, but with numeric column indexes.
  // These are faster since they avoid a hashmap lookup, so should
  // be preferred in performance-sensitive code (eg bulk loaders).
  CHECKED_STATUS SetBool(size_t col_idx, bool val) WARN_UNUSED_RESULT;

  CHECKED_STATUS SetInt8(size_t col_idx, int8_t val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetInt16(size_t col_idx, int16_t val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetInt32(size_t col_idx, int32_t val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetInt64(size_t col_idx, int64_t val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetTimestamp(size_t col_idx, int64_t micros_since_utc_epoch) WARN_UNUSED_RESULT;

  CHECKED_STATUS SetFloat(size_t col_idx, float val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetDouble(size_t col_idx, double val) WARN_UNUSED_RESULT;

  // Sets the string/binary value but does not copy the value. The slice
  // must remain valid until the call to AppendToPB().
  CHECKED_STATUS SetString(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetString(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetBinary(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetBinary(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetInet(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetInet(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetJsonb(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetJsonb(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetDecimal(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetDecimal(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetUuid(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetUuid(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetTimeUuid(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetTimeUuid(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetFrozen(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetFrozen(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;

  // Copies 'val' immediately.
  CHECKED_STATUS SetStringCopy(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetStringCopy(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetBinaryCopy(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetBinaryCopy(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetUuidCopy(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetUuidCopy(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetTimeUuidCopy(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetTimeUuidCopy(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetDecimalCopy(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetDecimalCopy(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetFrozenCopy(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetFrozenCopy(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;

  // Set the given column to NULL. This will only succeed on nullable
  // columns. Use Unset(...) to restore a column to its default.
  CHECKED_STATUS SetNull(const Slice& col_name) WARN_UNUSED_RESULT;
  CHECKED_STATUS SetNull(size_t col_idx) WARN_UNUSED_RESULT;

  // Unsets the given column. Note that this is different from setting
  // it to NULL.
  CHECKED_STATUS Unset(const Slice& col_name) WARN_UNUSED_RESULT;
  CHECKED_STATUS Unset(size_t col_idx) WARN_UNUSED_RESULT;

  //------------------------------------------------------------
  // Getters
  //------------------------------------------------------------
  // These getters return a bad Status if the type does not match,
  // the value is unset, or the value is NULL. Otherwise they return
  // the current set value in *val.

  // Return true if the given column has been specified.
  bool IsColumnSet(const Slice& col_name) const;
  bool IsColumnSet(size_t col_idx) const;

  bool IsNull(const Slice& col_name) const;
  bool IsNull(size_t col_idx) const;

  CHECKED_STATUS GetBool(const Slice& col_name, bool* val) const WARN_UNUSED_RESULT;

  CHECKED_STATUS GetInt8(const Slice& col_name, int8_t* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetInt16(const Slice& col_name, int16_t* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetInt32(const Slice& col_name, int32_t* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetInt64(const Slice& col_name, int64_t* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetTimestamp(const Slice& col_name,
                      int64_t* micros_since_utc_epoch) const WARN_UNUSED_RESULT;

  CHECKED_STATUS GetFloat(const Slice& col_name, float* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetDouble(const Slice& col_name, double* val) const WARN_UNUSED_RESULT;

  // Same as above getters, but with numeric column indexes.
  // These are faster since they avoid a hashmap lookup, so should
  // be preferred in performance-sensitive code.
  CHECKED_STATUS GetBool(size_t col_idx, bool* val) const WARN_UNUSED_RESULT;

  CHECKED_STATUS GetInt8(size_t col_idx, int8_t* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetInt16(size_t col_idx, int16_t* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetInt32(size_t col_idx, int32_t* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetInt64(size_t col_idx, int64_t* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetTimestamp(size_t col_idx,
                              int64_t* micros_since_utc_epoch) const WARN_UNUSED_RESULT;

  CHECKED_STATUS GetFloat(size_t col_idx, float* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetDouble(size_t col_idx, double* val) const WARN_UNUSED_RESULT;

  // Gets the string/binary value but does not copy the value. Callers should
  // copy the resulting Slice if necessary.
  CHECKED_STATUS GetString(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetString(size_t col_idx, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetBinary(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetBinary(size_t col_idx, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetInet(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetInet(size_t col_idx, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetJsonb(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetJsonb(size_t col_idx, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetDecimal(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetDecimal(size_t col_idx, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetUuid(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetUuid(size_t col_idx, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetTimeUuid(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  CHECKED_STATUS GetTimeUuid(size_t col_idx, Slice* val) const WARN_UNUSED_RESULT;

  //------------------------------------------------------------
  // Key-encoding related functions
  //------------------------------------------------------------

  // Encode a row key suitable for use as a tablet split key, an encoded
  // key range, etc.
  //
  // Requires that all of the key columns must be set; otherwise, returns
  // InvalidArgument.
  CHECKED_STATUS EncodeRowKey(std::string* encoded_key) const;

  // Convenience method which is equivalent to the above, but triggers a
  // FATAL error on failure.
  std::string ToEncodedRowKeyOrDie() const;

  //------------------------------------------------------------
  // Utility code
  //------------------------------------------------------------

  // Returns true if there are hash key columns and values for all of them have been specified.
  bool IsHashKeySet() const;

  // Returns true if there are primary key (hash plus range) columns and values for all of them
  // have been specified.
  bool IsKeySet() const;

  // Returns true if either hash key or primary key exist and are set.
  // In practice this means that either:
  //  - hash key columns exist and are set
  //  - no hash key column exist but primary key columns exist and are set (i.e. range portion only)
  bool IsHashOrPrimaryKeySet() const;

  // Return true if values for all columns have been specified.
  bool AllColumnsSet() const;

  std::string ToString() const;

  const Schema* schema() const { return schema_; }

 private:
  friend class RowKeyUtilTest;
  friend class PartitionSchema;
  template<typename KeyTypeWrapper> friend struct client::SliceKeysTestSetup;
  template<typename KeyTypeWrapper> friend struct client::IntKeysTestSetup;

  template<typename T>
  CHECKED_STATUS Set(const Slice& col_name, const typename T::cpp_type& val,
             bool owned = false);

  template<typename T>
  CHECKED_STATUS Set(size_t col_idx, const typename T::cpp_type& val, bool owned = false);

  // Runtime version of the generic setter.
  CHECKED_STATUS Set(size_t column_idx, const uint8_t* val);

  template<typename T>
  CHECKED_STATUS Get(const Slice& col_name, typename T::cpp_type* val) const;

  template<typename T>
  CHECKED_STATUS Get(size_t col_idx, typename T::cpp_type* val) const;

  template<typename T>
  CHECKED_STATUS SetSliceCopy(const Slice& col_name, const Slice& val);

  template<typename T>
  CHECKED_STATUS SetSliceCopy(size_t col_idx, const Slice& val);

  // If the given column is a variable length column whose memory is owned by this instance,
  // deallocates the value.
  // NOTE: Does not mutate the isset bitmap.
  // REQUIRES: col_idx must be a variable length column.
  void DeallocateStringIfSet(size_t col_idx, const ColumnSchema& col);

  // Deallocate any string/binary values whose memory is managed by this object.
  void DeallocateOwnedStrings();

  const Schema* schema_;

  // 1-bit set for any field which has been explicitly set. This is distinct
  // from NULL -- an "unset" field will take the server-side default on insert,
  // whereas a field explicitly set to NULL will override the default.
  uint8_t* isset_bitmap_;

  // 1-bit set for any variable length columns whose memory is managed by this instance.
  // These strings need to be deallocated whenever the value is reset,
  // or when the instance is destructed.
  uint8_t* owned_strings_bitmap_;

  // The normal "contiguous row" format row data. Any column whose data is unset
  // or NULL can have undefined bytes.
  uint8_t* row_data_;
};

} // namespace yb
#endif /* YB_COMMON_PARTIAL_ROW_H */
