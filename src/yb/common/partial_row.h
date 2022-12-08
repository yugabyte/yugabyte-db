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

  Status SetBool(const Slice& col_name, bool val) WARN_UNUSED_RESULT;

  Status SetInt8(const Slice& col_name, int8_t val) WARN_UNUSED_RESULT;
  Status SetInt16(const Slice& col_name, int16_t val) WARN_UNUSED_RESULT;
  Status SetInt32(const Slice& col_name, int32_t val) WARN_UNUSED_RESULT;
  Status SetInt64(const Slice& col_name, int64_t val) WARN_UNUSED_RESULT;
  Status SetTimestamp(const Slice& col_name,
                      int64_t micros_since_utc_epoch) WARN_UNUSED_RESULT;
  Status SetFloat(const Slice& col_name, float val) WARN_UNUSED_RESULT;
  Status SetDouble(const Slice& col_name, double val) WARN_UNUSED_RESULT;

  // Same as above setters, but with numeric column indexes.
  // These are faster since they avoid a hashmap lookup, so should
  // be preferred in performance-sensitive code (eg bulk loaders).
  Status SetBool(size_t col_idx, bool val) WARN_UNUSED_RESULT;

  Status SetInt8(size_t col_idx, int8_t val) WARN_UNUSED_RESULT;
  Status SetInt16(size_t col_idx, int16_t val) WARN_UNUSED_RESULT;
  Status SetInt32(size_t col_idx, int32_t val) WARN_UNUSED_RESULT;
  Status SetInt64(size_t col_idx, int64_t val) WARN_UNUSED_RESULT;
  Status SetTimestamp(size_t col_idx, int64_t micros_since_utc_epoch) WARN_UNUSED_RESULT;

  Status SetFloat(size_t col_idx, float val) WARN_UNUSED_RESULT;
  Status SetDouble(size_t col_idx, double val) WARN_UNUSED_RESULT;

  // Sets the string/binary value but does not copy the value. The slice
  // must remain valid until the call to AppendToPB().
  Status SetString(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  Status SetString(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  Status SetBinary(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  Status SetBinary(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  Status SetInet(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  Status SetInet(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  Status SetJsonb(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  Status SetJsonb(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  Status SetDecimal(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  Status SetDecimal(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  Status SetUuid(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  Status SetUuid(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  Status SetTimeUuid(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  Status SetTimeUuid(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  Status SetFrozen(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  Status SetFrozen(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;

  // Copies 'val' immediately.
  Status SetStringCopy(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  Status SetStringCopy(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  Status SetBinaryCopy(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  Status SetBinaryCopy(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  Status SetUuidCopy(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  Status SetUuidCopy(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  Status SetTimeUuidCopy(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  Status SetTimeUuidCopy(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  Status SetDecimalCopy(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  Status SetDecimalCopy(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;
  Status SetFrozenCopy(const Slice& col_name, const Slice& val) WARN_UNUSED_RESULT;
  Status SetFrozenCopy(size_t col_idx, const Slice& val) WARN_UNUSED_RESULT;

  // Set the given column to NULL. This will only succeed on nullable
  // columns. Use Unset(...) to restore a column to its default.
  Status SetNull(const Slice& col_name) WARN_UNUSED_RESULT;
  Status SetNull(size_t col_idx) WARN_UNUSED_RESULT;

  // Unsets the given column. Note that this is different from setting
  // it to NULL.
  Status Unset(const Slice& col_name) WARN_UNUSED_RESULT;
  Status Unset(size_t col_idx) WARN_UNUSED_RESULT;

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

  Status GetBool(const Slice& col_name, bool* val) const WARN_UNUSED_RESULT;

  Status GetInt8(const Slice& col_name, int8_t* val) const WARN_UNUSED_RESULT;
  Status GetInt16(const Slice& col_name, int16_t* val) const WARN_UNUSED_RESULT;
  Status GetInt32(const Slice& col_name, int32_t* val) const WARN_UNUSED_RESULT;
  Status GetInt64(const Slice& col_name, int64_t* val) const WARN_UNUSED_RESULT;
  Status GetTimestamp(const Slice& col_name,
                      int64_t* micros_since_utc_epoch) const WARN_UNUSED_RESULT;

  Status GetFloat(const Slice& col_name, float* val) const WARN_UNUSED_RESULT;
  Status GetDouble(const Slice& col_name, double* val) const WARN_UNUSED_RESULT;

  // Same as above getters, but with numeric column indexes.
  // These are faster since they avoid a hashmap lookup, so should
  // be preferred in performance-sensitive code.
  Status GetBool(size_t col_idx, bool* val) const WARN_UNUSED_RESULT;

  Status GetInt8(size_t col_idx, int8_t* val) const WARN_UNUSED_RESULT;
  Status GetInt16(size_t col_idx, int16_t* val) const WARN_UNUSED_RESULT;
  Status GetInt32(size_t col_idx, int32_t* val) const WARN_UNUSED_RESULT;
  Status GetInt64(size_t col_idx, int64_t* val) const WARN_UNUSED_RESULT;
  Status GetTimestamp(size_t col_idx,
                      int64_t* micros_since_utc_epoch) const WARN_UNUSED_RESULT;

  Status GetFloat(size_t col_idx, float* val) const WARN_UNUSED_RESULT;
  Status GetDouble(size_t col_idx, double* val) const WARN_UNUSED_RESULT;

  // Gets the string/binary value but does not copy the value. Callers should
  // copy the resulting Slice if necessary.
  Status GetString(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  Status GetString(size_t col_idx, Slice* val) const WARN_UNUSED_RESULT;
  Status GetBinary(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  Status GetBinary(size_t col_idx, Slice* val) const WARN_UNUSED_RESULT;
  Status GetInet(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  Status GetInet(size_t col_idx, Slice* val) const WARN_UNUSED_RESULT;
  Status GetJsonb(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  Status GetJsonb(size_t col_idx, Slice* val) const WARN_UNUSED_RESULT;
  Status GetDecimal(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  Status GetDecimal(size_t col_idx, Slice* val) const WARN_UNUSED_RESULT;
  Status GetUuid(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  Status GetUuid(size_t col_idx, Slice* val) const WARN_UNUSED_RESULT;
  Status GetTimeUuid(const Slice& col_name, Slice* val) const WARN_UNUSED_RESULT;
  Status GetTimeUuid(size_t col_idx, Slice* val) const WARN_UNUSED_RESULT;

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
  Status Set(const Slice& col_name, const typename T::cpp_type& val,
             bool owned = false);

  template<typename T>
  Status Set(size_t col_idx, const typename T::cpp_type& val, bool owned = false);

  // Runtime version of the generic setter.
  Status Set(size_t column_idx, const uint8_t* val);

  template<typename T>
  Status Get(const Slice& col_name, typename T::cpp_type* val) const;

  template<typename T>
  Status Get(size_t col_idx, typename T::cpp_type* val) const;

  template<typename T>
  Status SetSliceCopy(const Slice& col_name, const Slice& val);

  template<typename T>
  Status SetSliceCopy(size_t col_idx, const Slice& val);

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
