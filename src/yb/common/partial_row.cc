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

#include "yb/common/partial_row.h"

#include <algorithm>
#include <string>

#include "yb/common/common.pb.h"
#include "yb/common/key_encoder.h"
#include "yb/common/row.h"
#include "yb/common/schema.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/bitmap.h"
#include "yb/util/decimal.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"

using strings::Substitute;

namespace yb {

namespace {

inline Result<size_t> FindColumn(const Schema& schema, const Slice& col_name) {
  GStringPiece sp(col_name.cdata(), col_name.size());
  auto result = schema.find_column(sp);
  if (PREDICT_FALSE(result == Schema::kColumnNotFound)) {
    return STATUS(NotFound, "No such column", col_name);
  }
  return result;
}

} // anonymous namespace

YBPartialRow::YBPartialRow(const Schema* schema)
  : schema_(schema) {
  DCHECK(schema_->initialized());
  size_t column_bitmap_size = BitmapSize(schema_->num_columns());
  size_t row_size = ContiguousRowHelper::row_size(*schema);

  auto dst = new uint8_t[2 * column_bitmap_size + row_size];
  isset_bitmap_ = dst;
  owned_strings_bitmap_ = isset_bitmap_ + column_bitmap_size;

  memset(isset_bitmap_, 0, 2 * column_bitmap_size);

  row_data_ = owned_strings_bitmap_ + column_bitmap_size;
#ifndef NDEBUG
  OverwriteWithPattern(reinterpret_cast<char*>(row_data_),
                       row_size, "NEWNEWNEWNEWNEW");
#endif
  ContiguousRowHelper::InitNullsBitmap(
    *schema_, row_data_, ContiguousRowHelper::null_bitmap_size(*schema_));
}

YBPartialRow::~YBPartialRow() {
  DeallocateOwnedStrings();
  // Both the row data and bitmap came from the same allocation.
  // The bitmap is at the start of it.
  delete [] isset_bitmap_;
}

YBPartialRow::YBPartialRow(const YBPartialRow& other)
    : schema_(other.schema_) {
  size_t column_bitmap_size = BitmapSize(schema_->num_columns());
  size_t row_size = ContiguousRowHelper::row_size(*schema_);

  size_t len = 2 * column_bitmap_size + row_size;
  isset_bitmap_ = new uint8_t[len];
  owned_strings_bitmap_ = isset_bitmap_ + column_bitmap_size;
  row_data_ = owned_strings_bitmap_ + column_bitmap_size;

  // Copy all bitmaps and row data.
  memcpy(isset_bitmap_, other.isset_bitmap_, len);

  // Copy owned strings.
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); col_idx++) {
    if (BitmapTest(owned_strings_bitmap_, col_idx)) {
      ContiguousRow row(schema_, row_data_);
      Slice* slice = reinterpret_cast<Slice*>(row.mutable_cell_ptr(col_idx));
      auto data = new uint8_t[slice->size()];
      slice->relocate(data);
    }
  }
}

YBPartialRow& YBPartialRow::operator=(YBPartialRow other) {
  std::swap(schema_, other.schema_);
  std::swap(isset_bitmap_, other.isset_bitmap_);
  std::swap(owned_strings_bitmap_, other.owned_strings_bitmap_);
  std::swap(row_data_, other.row_data_);
  return *this;
}

template<typename T>
Status YBPartialRow::Set(const Slice& col_name,
                         const typename T::cpp_type& val,
                         bool owned) {
  return Set<T>(VERIFY_RESULT(FindColumn(*schema_, col_name)), val, owned);
}

template<typename T>
Status YBPartialRow::Set(size_t col_idx,
                         const typename T::cpp_type& val,
                         bool owned) {
  const ColumnSchema& col = schema_->column(col_idx);
  if (PREDICT_FALSE(col.type_info()->type() != T::type)) {
    // TODO: at some point we could allow type coercion here.
    return STATUS(InvalidArgument,
      Substitute("invalid type $0 provided for column '$1' (expected $2)",
                 T::name(),
                 col.name(), col.type_info()->name()));
  }

  ContiguousRow row(schema_, row_data_);

  // If we're replacing an existing STRING/BINARY/INET value, deallocate the old value.
  if (T::physical_type == BINARY) DeallocateStringIfSet(col_idx, col);

  // Mark the column as set.
  BitmapSet(isset_bitmap_, col_idx);

  if (col.is_nullable()) {
    row.set_null(col_idx, false);
  }

  ContiguousRowCell<ContiguousRow> dst(&row, col_idx);
  memcpy(dst.mutable_ptr(), &val, sizeof(val));
  if (owned) {
    BitmapSet(owned_strings_bitmap_, col_idx);
  }
  return Status::OK();
}

Status YBPartialRow::Set(size_t column_idx, const uint8_t* val) {
  const ColumnSchema& column_schema = schema()->column(column_idx);

  switch (column_schema.type_info()->type()) {
    case BOOL: {
      RETURN_NOT_OK(SetBool(column_idx, *reinterpret_cast<const bool*>(val)));
      break;
    };
    case INT8: {
      RETURN_NOT_OK(SetInt8(column_idx, *reinterpret_cast<const int8_t*>(val)));
      break;
    };
    case INT16: {
      RETURN_NOT_OK(SetInt16(column_idx, *reinterpret_cast<const int16_t*>(val)));
      break;
    };
    case INT32: {
      RETURN_NOT_OK(SetInt32(column_idx, *reinterpret_cast<const int32_t*>(val)));
      break;
    };
    case INT64: {
      RETURN_NOT_OK(SetInt64(column_idx, *reinterpret_cast<const int64_t*>(val)));
      break;
    };
    case FLOAT: {
      RETURN_NOT_OK(SetFloat(column_idx, *reinterpret_cast<const float*>(val)));
      break;
    };
    case DOUBLE: {
      RETURN_NOT_OK(SetDouble(column_idx, *reinterpret_cast<const double*>(val)));
      break;
    };
    case STRING: {
      RETURN_NOT_OK(SetStringCopy(column_idx, *reinterpret_cast<const Slice*>(val)));
      break;
    };
    case BINARY: {
      RETURN_NOT_OK(SetBinaryCopy(column_idx, *reinterpret_cast<const Slice*>(val)));
      break;
    };
    case TIMESTAMP: {
      RETURN_NOT_OK(SetTimestamp(column_idx, *reinterpret_cast<const int64_t*>(val)));
      break;
    };
    case INET: {
      RETURN_NOT_OK(SetInet(column_idx, *reinterpret_cast<const Slice*>(val)));
      break;
    };
    case JSONB: {
      RETURN_NOT_OK(SetJsonb(column_idx, *reinterpret_cast<const Slice*>(val)));
      break;
    };
    case UUID: {
      RETURN_NOT_OK(SetUuidCopy(column_idx, *reinterpret_cast<const Slice*>(val)));
      break;
    };
    case TIMEUUID: {
      RETURN_NOT_OK(SetTimeUuidCopy(column_idx, *reinterpret_cast<const Slice*>(val)));
      break;
    }
    case FROZEN: {
      RETURN_NOT_OK(SetFrozenCopy(column_idx, *reinterpret_cast<const Slice*>(val)));
      break;
    };
    case DECIMAL: FALLTHROUGH_INTENDED;
    default: {
      return STATUS(InvalidArgument, "Unknown column type in schema",
                                     column_schema.ToString());
    };
  }
  return Status::OK();
}

void YBPartialRow::DeallocateStringIfSet(size_t col_idx, const ColumnSchema& col) {
  if (BitmapTest(owned_strings_bitmap_, col_idx)) {
    ContiguousRow row(schema_, row_data_);
    const Slice* dst;
    if (col.type_info()->type() == BINARY) {
      dst = schema_->ExtractColumnFromRow<BINARY>(row, col_idx);
    } else if (col.type_info()->type() == INET) {
      dst = schema_->ExtractColumnFromRow<INET>(row, col_idx);
    } else if (col.type_info()->type() == JSONB) {
      dst = schema_->ExtractColumnFromRow<JSONB>(row, col_idx);
    } else if (col.type_info()->type() == UUID) {
      dst = schema_->ExtractColumnFromRow<UUID>(row, col_idx);
    } else if (col.type_info()->type() == TIMEUUID) {
      dst = schema_->ExtractColumnFromRow<TIMEUUID>(row, col_idx);
    } else if (col.type_info()->type() == FROZEN) {
      dst = schema_->ExtractColumnFromRow<FROZEN>(row, col_idx);
    } else {
      CHECK(col.type_info()->type() == STRING);
      dst = schema_->ExtractColumnFromRow<STRING>(row, col_idx);
    }
    delete [] dst->data();
    BitmapClear(owned_strings_bitmap_, col_idx);
  }
}

void YBPartialRow::DeallocateOwnedStrings() {
  for (int i = 0; i < schema_->num_columns(); i++) {
    DeallocateStringIfSet(i, schema_->column(i));
  }
}

//------------------------------------------------------------
// Setters
//------------------------------------------------------------

Status YBPartialRow::SetBool(const Slice& col_name, bool val) {
  return Set<TypeTraits<BOOL> >(col_name, val);
}
Status YBPartialRow::SetInt8(const Slice& col_name, int8_t val) {
  return Set<TypeTraits<INT8> >(col_name, val);
}
Status YBPartialRow::SetInt16(const Slice& col_name, int16_t val) {
  return Set<TypeTraits<INT16> >(col_name, val);
}
Status YBPartialRow::SetInt32(const Slice& col_name, int32_t val) {
  return Set<TypeTraits<INT32> >(col_name, val);
}
Status YBPartialRow::SetInt64(const Slice& col_name, int64_t val) {
  return Set<TypeTraits<INT64> >(col_name, val);
}
Status YBPartialRow::SetTimestamp(const Slice& col_name, int64_t val) {
  return Set<TypeTraits<TIMESTAMP> >(col_name, val);
}
Status YBPartialRow::SetFloat(const Slice& col_name, float val) {
  return Set<TypeTraits<FLOAT> >(col_name, val);
}
Status YBPartialRow::SetDouble(const Slice& col_name, double val) {
  return Set<TypeTraits<DOUBLE> >(col_name, val);
}
Status YBPartialRow::SetString(const Slice& col_name, const Slice& val) {
  return Set<TypeTraits<STRING> >(col_name, val, false);
}
Status YBPartialRow::SetBinary(const Slice& col_name, const Slice& val) {
  return Set<TypeTraits<BINARY> >(col_name, val, false);
}
Status YBPartialRow::SetFrozen(const Slice& col_name, const Slice& val) {
  return Set<TypeTraits<FROZEN> >(col_name, val, false);
}
Status YBPartialRow::SetInet(const Slice& col_name, const Slice& val) {
  return SetSliceCopy<TypeTraits<INET> >(col_name, val);
}
Status YBPartialRow::SetUuid(const Slice& col_name, const Slice& val) {
  return Set<TypeTraits<UUID> >(col_name, val, false);
}
Status YBPartialRow::SetTimeUuid(const Slice& col_name, const Slice& val) {
  return Set<TypeTraits<TIMEUUID> >(col_name, val, false);
}
Status YBPartialRow::SetDecimal(const Slice& col_name, const Slice& val) {
  return Set<TypeTraits<DECIMAL> >(col_name, val, false);
}
Status YBPartialRow::SetBool(size_t col_idx, bool val) {
  return Set<TypeTraits<BOOL> >(col_idx, val);
}
Status YBPartialRow::SetInt8(size_t col_idx, int8_t val) {
  return Set<TypeTraits<INT8> >(col_idx, val);
}
Status YBPartialRow::SetInt16(size_t col_idx, int16_t val) {
  return Set<TypeTraits<INT16> >(col_idx, val);
}
Status YBPartialRow::SetInt32(size_t col_idx, int32_t val) {
  return Set<TypeTraits<INT32> >(col_idx, val);
}
Status YBPartialRow::SetInt64(size_t col_idx, int64_t val) {
  return Set<TypeTraits<INT64> >(col_idx, val);
}
Status YBPartialRow::SetTimestamp(size_t col_idx, int64_t val) {
  return Set<TypeTraits<TIMESTAMP> >(col_idx, val);
}
Status YBPartialRow::SetString(size_t col_idx, const Slice& val) {
  return Set<TypeTraits<STRING> >(col_idx, val, false);
}
Status YBPartialRow::SetBinary(size_t col_idx, const Slice& val) {
  return Set<TypeTraits<BINARY> >(col_idx, val, false);
}
Status YBPartialRow::SetFrozen(size_t col_idx, const Slice& val) {
  return Set<TypeTraits<FROZEN> >(col_idx, val, false);
}
Status YBPartialRow::SetInet(size_t col_idx, const Slice& val) {
  return SetSliceCopy<TypeTraits<INET> >(col_idx, val);
}
Status YBPartialRow::SetJsonb(size_t col_idx, const Slice& val) {
  return SetSliceCopy<TypeTraits<JSONB> >(col_idx, val);
}
Status YBPartialRow::SetUuid(size_t col_idx, const Slice& val) {
  return Set<TypeTraits<UUID> >(col_idx, val, false);
}
Status YBPartialRow::SetTimeUuid(size_t col_idx, const Slice& val) {
  return Set<TypeTraits<TIMEUUID> >(col_idx, val, false);
}
Status YBPartialRow::SetDecimal(size_t col_idx, const Slice& val) {
  return Set<TypeTraits<DECIMAL> >(col_idx, val, false);
}
Status YBPartialRow::SetFloat(size_t col_idx, float val) {
  return Set<TypeTraits<FLOAT> >(col_idx, util::CanonicalizeFloat(val));
}
Status YBPartialRow::SetDouble(size_t col_idx, double val) {
  return Set<TypeTraits<DOUBLE> >(col_idx, util::CanonicalizeDouble(val));
}

Status YBPartialRow::SetBinaryCopy(const Slice& col_name, const Slice& val) {
  return SetSliceCopy<TypeTraits<BINARY> >(col_name, val);
}
Status YBPartialRow::SetBinaryCopy(size_t col_idx, const Slice& val) {
  return SetSliceCopy<TypeTraits<BINARY> >(col_idx, val);
}
Status YBPartialRow::SetStringCopy(const Slice& col_name, const Slice& val) {
  return SetSliceCopy<TypeTraits<STRING> >(col_name, val);
}
Status YBPartialRow::SetStringCopy(size_t col_idx, const Slice& val) {
  return SetSliceCopy<TypeTraits<STRING> >(col_idx, val);
}
Status YBPartialRow::SetUuidCopy(const Slice& col_name, const Slice& val) {
  return SetSliceCopy<TypeTraits<UUID> >(col_name, val);
}
Status YBPartialRow::SetUuidCopy(size_t col_idx, const Slice& val) {
  return SetSliceCopy<TypeTraits<UUID> >(col_idx, val);
}
Status YBPartialRow::SetTimeUuidCopy(const Slice& col_name, const Slice& val) {
  return SetSliceCopy<TypeTraits<TIMEUUID> >(col_name, val);
}
Status YBPartialRow::SetTimeUuidCopy(size_t col_idx, const Slice& val) {
  return SetSliceCopy<TypeTraits<TIMEUUID> >(col_idx, val);
}
Status YBPartialRow::SetFrozenCopy(const Slice& col_name, const Slice& val) {
  return SetSliceCopy<TypeTraits<FROZEN> >(col_name, val);
}
Status YBPartialRow::SetFrozenCopy(size_t col_idx, const Slice& val) {
  return SetSliceCopy<TypeTraits<FROZEN> >(col_idx, val);
}

template<typename T>
Status YBPartialRow::SetSliceCopy(const Slice& col_name, const Slice& val) {
  auto relocated = new uint8_t[val.size()];
  memcpy(relocated, val.data(), val.size());
  Slice relocated_val(relocated, val.size());
  Status s = Set<T>(col_name, relocated_val, true);
  if (!s.ok()) {
    delete [] relocated;
  }
  return s;
}

template<typename T>
Status YBPartialRow::SetSliceCopy(size_t col_idx, const Slice& val) {
  auto relocated = new uint8_t[val.size()];
  memcpy(relocated, val.data(), val.size());
  Slice relocated_val(relocated, val.size());
  Status s = Set<T>(col_idx, relocated_val, true);
  if (!s.ok()) {
    delete [] relocated;
  }
  return s;
}

Status YBPartialRow::SetNull(const Slice& col_name) {
  return SetNull(VERIFY_RESULT(FindColumn(*schema_, col_name)));
}

Status YBPartialRow::SetNull(size_t col_idx) {
  const ColumnSchema& col = schema_->column(col_idx);
  if (PREDICT_FALSE(!col.is_nullable())) {
    return STATUS(InvalidArgument, "column not nullable", col.ToString());
  }

  if (col.type_info()->physical_type() == BINARY) DeallocateStringIfSet(col_idx, col);

  ContiguousRow row(schema_, row_data_);
  row.set_null(col_idx, true);

  // Mark the column as set.
  BitmapSet(isset_bitmap_, col_idx);
  return Status::OK();
}

Status YBPartialRow::Unset(const Slice& col_name) {
  return Unset(VERIFY_RESULT(FindColumn(*schema_, col_name)));
}

Status YBPartialRow::Unset(size_t col_idx) {
  const ColumnSchema& col = schema_->column(col_idx);
  if (col.type_info()->physical_type() == BINARY) DeallocateStringIfSet(col_idx, col);
  BitmapClear(isset_bitmap_, col_idx);
  return Status::OK();
}

//------------------------------------------------------------
// Template instantiations: We instantiate all possible templates to avoid linker issues.
// see: https://isocpp.org/wiki/faq/templates#separate-template-fn-defn-from-decl
// TODO We can probably remove this when we move to c++11 and can use "extern template"
//------------------------------------------------------------

template
Status YBPartialRow::SetSliceCopy<TypeTraits<STRING> >(size_t col_idx, const Slice& val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<BINARY> >(size_t col_idx, const Slice& val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<INET> >(size_t col_idx, const Slice& val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<JSONB> >(size_t col_idx, const Slice& val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<UUID> >(size_t col_idx, const Slice& val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<TIMEUUID> >(size_t col_idx, const Slice& val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<STRING> >(const Slice& col_name, const Slice& val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<BINARY> >(const Slice& col_name, const Slice& val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<INET> >(const Slice& col_name, const Slice& val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<JSONB> >(const Slice& col_name, const Slice& val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<UUID> >(const Slice& col_name, const Slice& val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<TIMEUUID> >(const Slice& col_name, const Slice& val);

template
Status YBPartialRow::Set<TypeTraits<INT8> >(size_t col_idx,
                                              const TypeTraits<INT8>::cpp_type& val,
                                              bool owned);

template
Status YBPartialRow::Set<TypeTraits<INT16> >(size_t col_idx,
                                               const TypeTraits<INT16>::cpp_type& val,
                                               bool owned);

template
Status YBPartialRow::Set<TypeTraits<INT32> >(size_t col_idx,
                                               const TypeTraits<INT32>::cpp_type& val,
                                               bool owned);

template
Status YBPartialRow::Set<TypeTraits<INT64> >(size_t col_idx,
                                               const TypeTraits<INT64>::cpp_type& val,
                                               bool owned);

template
Status YBPartialRow::Set<TypeTraits<TIMESTAMP> >(
    size_t col_idx,
    const TypeTraits<TIMESTAMP>::cpp_type& val,
    bool owned);

template
Status YBPartialRow::Set<TypeTraits<STRING> >(size_t col_idx,
                                                const TypeTraits<STRING>::cpp_type& val,
                                                bool owned);

template
Status YBPartialRow::Set<TypeTraits<BINARY> >(size_t col_idx,
                                                const TypeTraits<BINARY>::cpp_type& val,
                                                bool owned);

template
Status YBPartialRow::Set<TypeTraits<FLOAT> >(size_t col_idx,
                                               const TypeTraits<FLOAT>::cpp_type& val,
                                               bool owned);

template
Status YBPartialRow::Set<TypeTraits<DOUBLE> >(size_t col_idx,
                                                const TypeTraits<DOUBLE>::cpp_type& val,
                                                bool owned);

template
Status YBPartialRow::Set<TypeTraits<BOOL> >(size_t col_idx,
                                              const TypeTraits<BOOL>::cpp_type& val,
                                              bool owned);

template
Status YBPartialRow::Set<TypeTraits<INT8> >(const Slice& col_name,
                                              const TypeTraits<INT8>::cpp_type& val,
                                              bool owned);

template
Status YBPartialRow::Set<TypeTraits<INT16> >(const Slice& col_name,
                                               const TypeTraits<INT16>::cpp_type& val,
                                               bool owned);

template
Status YBPartialRow::Set<TypeTraits<INT32> >(const Slice& col_name,
                                               const TypeTraits<INT32>::cpp_type& val,
                                               bool owned);

template
Status YBPartialRow::Set<TypeTraits<INT64> >(const Slice& col_name,
                                               const TypeTraits<INT64>::cpp_type& val,
                                               bool owned);

template
Status YBPartialRow::Set<TypeTraits<TIMESTAMP> >(
    const Slice& col_name,
    const TypeTraits<TIMESTAMP>::cpp_type& val,
    bool owned);

template
Status YBPartialRow::Set<TypeTraits<FLOAT> >(const Slice& col_name,
                                               const TypeTraits<FLOAT>::cpp_type& val,
                                               bool owned);

template
Status YBPartialRow::Set<TypeTraits<DOUBLE> >(const Slice& col_name,
                                                const TypeTraits<DOUBLE>::cpp_type& val,
                                                bool owned);

template
Status YBPartialRow::Set<TypeTraits<BOOL> >(const Slice& col_name,
                                              const TypeTraits<BOOL>::cpp_type& val,
                                              bool owned);

template
Status YBPartialRow::Set<TypeTraits<STRING> >(const Slice& col_name,
                                                const TypeTraits<STRING>::cpp_type& val,
                                                bool owned);

template
Status YBPartialRow::Set<TypeTraits<BINARY> >(const Slice& col_name,
                                                const TypeTraits<BINARY>::cpp_type& val,
                                                bool owned);

//------------------------------------------------------------
// Getters
//------------------------------------------------------------
bool YBPartialRow::IsColumnSet(size_t col_idx) const {
  DCHECK_GE(col_idx, 0);
  DCHECK_LT(col_idx, schema_->num_columns());
  return BitmapTest(isset_bitmap_, col_idx);
}

bool YBPartialRow::IsColumnSet(const Slice& col_name) const {
  return IsColumnSet(CHECK_RESULT(FindColumn(*schema_, col_name)));
}

bool YBPartialRow::IsNull(size_t col_idx) const {
  const ColumnSchema& col = schema_->column(col_idx);
  if (!col.is_nullable()) {
    return false;
  }

  if (!IsColumnSet(col_idx)) return false;

  ContiguousRow row(schema_, row_data_);
  return row.is_null(col_idx);
}

bool YBPartialRow::IsNull(const Slice& col_name) const {
  return IsNull(CHECK_RESULT(FindColumn(*schema_, col_name)));
}

Status YBPartialRow::GetBool(const Slice& col_name, bool* val) const {
  return Get<TypeTraits<BOOL> >(col_name, val);
}
Status YBPartialRow::GetInt8(const Slice& col_name, int8_t* val) const {
  return Get<TypeTraits<INT8> >(col_name, val);
}
Status YBPartialRow::GetInt16(const Slice& col_name, int16_t* val) const {
  return Get<TypeTraits<INT16> >(col_name, val);
}
Status YBPartialRow::GetInt32(const Slice& col_name, int32_t* val) const {
  return Get<TypeTraits<INT32> >(col_name, val);
}
Status YBPartialRow::GetInt64(const Slice& col_name, int64_t* val) const {
  return Get<TypeTraits<INT64> >(col_name, val);
}
Status YBPartialRow::GetTimestamp(const Slice& col_name, int64_t* micros_since_utc_epoch) const {
  return Get<TypeTraits<TIMESTAMP> >(col_name, micros_since_utc_epoch);
}
Status YBPartialRow::GetFloat(const Slice& col_name, float* val) const {
  return Get<TypeTraits<FLOAT> >(col_name, val);
}
Status YBPartialRow::GetDouble(const Slice& col_name, double* val) const {
  return Get<TypeTraits<DOUBLE> >(col_name, val);
}
Status YBPartialRow::GetString(const Slice& col_name, Slice* val) const {
  return Get<TypeTraits<STRING> >(col_name, val);
}
Status YBPartialRow::GetBinary(const Slice& col_name, Slice* val) const {
  return Get<TypeTraits<BINARY> >(col_name, val);
}
Status YBPartialRow::GetInet(const Slice& col_name, Slice* val) const {
  return Get<TypeTraits<INET> >(col_name, val);
}
Status YBPartialRow::GetJsonb(const Slice& col_name, Slice* val) const {
  return Get<TypeTraits<JSONB> >(col_name, val);
}
Status YBPartialRow::GetUuid(const Slice& col_name, Slice* val) const {
  return Get<TypeTraits<UUID> >(col_name, val);
}
Status YBPartialRow::GetTimeUuid(const Slice& col_name, Slice* val) const {
  return Get<TypeTraits<TIMEUUID> >(col_name, val);
}
Status YBPartialRow::GetBool(size_t col_idx, bool* val) const {
  return Get<TypeTraits<BOOL> >(col_idx, val);
}
Status YBPartialRow::GetInt8(size_t col_idx, int8_t* val) const {
  return Get<TypeTraits<INT8> >(col_idx, val);
}
Status YBPartialRow::GetInt16(size_t col_idx, int16_t* val) const {
  return Get<TypeTraits<INT16> >(col_idx, val);
}
Status YBPartialRow::GetInt32(size_t col_idx, int32_t* val) const {
  return Get<TypeTraits<INT32> >(col_idx, val);
}
Status YBPartialRow::GetInt64(size_t col_idx, int64_t* val) const {
  return Get<TypeTraits<INT64> >(col_idx, val);
}
Status YBPartialRow::GetTimestamp(size_t col_idx, int64_t* micros_since_utc_epoch) const {
  return Get<TypeTraits<TIMESTAMP> >(col_idx, micros_since_utc_epoch);
}
Status YBPartialRow::GetFloat(size_t col_idx, float* val) const {
  return Get<TypeTraits<FLOAT> >(col_idx, val);
}
Status YBPartialRow::GetDouble(size_t col_idx, double* val) const {
  return Get<TypeTraits<DOUBLE> >(col_idx, val);
}
Status YBPartialRow::GetString(size_t col_idx, Slice* val) const {
  return Get<TypeTraits<STRING> >(col_idx, val);
}
Status YBPartialRow::GetBinary(size_t col_idx, Slice* val) const {
  return Get<TypeTraits<BINARY> >(col_idx, val);
}
Status YBPartialRow::GetInet(size_t col_idx, Slice* val) const {
  return Get<TypeTraits<INET> >(col_idx, val);
}
Status YBPartialRow::GetJsonb(size_t col_idx, Slice* val) const {
  return Get<TypeTraits<JSONB> >(col_idx, val);
}
Status YBPartialRow::GetUuid(size_t col_idx, Slice* val) const {
  return Get<TypeTraits<UUID> >(col_idx, val);
}
Status YBPartialRow::GetTimeUuid(size_t col_idx, Slice* val) const {
  return Get<TypeTraits<TIMEUUID> >(col_idx, val);
}

template<typename T>
Status YBPartialRow::Get(const Slice& col_name,
                         typename T::cpp_type* val) const {
  return Get<T>(VERIFY_RESULT(FindColumn(*schema_, col_name)), val);
}

template<typename T>
Status YBPartialRow::Get(size_t col_idx, typename T::cpp_type* val) const {
  const ColumnSchema& col = schema_->column(col_idx);
  if (PREDICT_FALSE(col.type_info()->type() != T::type)) {
    // TODO: at some point we could allow type coercion here.
    return STATUS(InvalidArgument,
      Substitute("invalid type $0 provided for column '$1' (expected $2)",
                 T::name(),
                 col.name(), col.type_info()->name()));
  }

  if (PREDICT_FALSE(!IsColumnSet(col_idx))) {
    return STATUS(NotFound, "column not set");
  }
  if (col.is_nullable() && IsNull(col_idx)) {
    return STATUS(NotFound, "column is NULL");
  }

  ContiguousRow row(schema_, row_data_);
  memcpy(val, row.cell_ptr(col_idx), sizeof(*val));
  return Status::OK();
}

//------------------------------------------------------------
// Key-encoding related functions
//------------------------------------------------------------
Status YBPartialRow::EncodeRowKey(string* encoded_key) const {
  // Currently, a row key must be fully specified.
  // TODO: allow specifying a prefix of the key, and automatically
  // fill the rest with minimum values.
  for (int i = 0; i < schema_->num_key_columns(); i++) {
    if (PREDICT_FALSE(!IsColumnSet(i))) {
      return STATUS(InvalidArgument, "All key columns must be set",
                                     schema_->column(i).name());
    }
  }

  encoded_key->clear();
  ContiguousRow row(schema_, row_data_);

  for (int i = 0; i < schema_->num_key_columns(); i++) {
    bool is_last = i == schema_->num_key_columns() - 1;
    const TypeInfo* ti = schema_->column(i).type_info();
    GetKeyEncoder<string>(ti).Encode(row.cell_ptr(i), is_last, encoded_key);
  }

  return Status::OK();
}

string YBPartialRow::ToEncodedRowKeyOrDie() const {
  string ret;
  CHECK_OK(EncodeRowKey(&ret));
  return ret;
}

//------------------------------------------------------------
// Utility code
//------------------------------------------------------------

bool YBPartialRow::IsHashKeySet() const {
  return schema_->num_hash_key_columns() > 0 &&
         BitMapIsAllSet(isset_bitmap_, 0, schema_->num_hash_key_columns());
}

bool YBPartialRow::IsKeySet() const {
  return schema_->num_key_columns() > 0 &&
         BitMapIsAllSet(isset_bitmap_, 0, schema_->num_key_columns());
}

bool YBPartialRow::IsHashOrPrimaryKeySet() const {
  return IsHashKeySet() || IsKeySet();
}

bool YBPartialRow::AllColumnsSet() const {
  return schema_->num_columns() > 0 &&
         BitMapIsAllSet(isset_bitmap_, 0, schema_->num_columns());
}

std::string YBPartialRow::ToString() const {
  ContiguousRow row(schema_, row_data_);
  std::string ret;
  bool first = true;
  for (int i = 0; i < schema_->num_columns(); i++) {
    if (IsColumnSet(i)) {
      if (!first) {
        ret.append(", ");
      }
      schema_->column(i).DebugCellAppend(row.cell(i), &ret);
      first = false;
    }
  }
  return ret;
}

//------------------------------------------------------------
// Serialization/deserialization
//------------------------------------------------------------

} // namespace yb
