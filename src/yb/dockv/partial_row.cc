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

#include "yb/dockv/partial_row.h"

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

namespace yb::dockv {

namespace {

inline Result<size_t> FindColumn(const Schema& schema, Slice col_name) {
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
Status YBPartialRow::Set(Slice col_name,
                         const typename T::cpp_type& val,
                         bool owned) {
  return Set<T>(VERIFY_RESULT(FindColumn(*schema_, col_name)), val, owned);
}

template<typename T>
Status YBPartialRow::Set(size_t col_idx,
                         const typename T::cpp_type& val,
                         bool owned) {
  const ColumnSchema& col = schema_->column(col_idx);
  if (PREDICT_FALSE(col.type_info()->type != T::type)) {
    // TODO: at some point we could allow type coercion here.
    return STATUS(InvalidArgument,
      Substitute("invalid type $0 provided for column '$1' (expected $2)",
                 T::name(),
                 col.name(), col.type_info()->name));
  }

  ContiguousRow row(schema_, row_data_);

  // If we're replacing an existing STRING/BINARY/INET value, deallocate the old value.
  if (col.type_info()->var_length()) {
    DeallocateStringIfSet(col_idx, col);
  }

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

  switch (column_schema.type_info()->type) {
    case DataType::BOOL: {
      RETURN_NOT_OK(SetBool(column_idx, *reinterpret_cast<const bool*>(val)));
      break;
    };
    case DataType::INT8: {
      RETURN_NOT_OK(SetInt8(column_idx, *reinterpret_cast<const int8_t*>(val)));
      break;
    };
    case DataType::INT16: {
      RETURN_NOT_OK(SetInt16(column_idx, *reinterpret_cast<const int16_t*>(val)));
      break;
    };
    case DataType::INT32: {
      RETURN_NOT_OK(SetInt32(column_idx, *reinterpret_cast<const int32_t*>(val)));
      break;
    };
    case DataType::INT64: {
      RETURN_NOT_OK(SetInt64(column_idx, *reinterpret_cast<const int64_t*>(val)));
      break;
    };
    case DataType::FLOAT: {
      RETURN_NOT_OK(SetFloat(column_idx, *reinterpret_cast<const float*>(val)));
      break;
    };
    case DataType::DOUBLE: {
      RETURN_NOT_OK(SetDouble(column_idx, *reinterpret_cast<const double*>(val)));
      break;
    };
    case DataType::STRING: {
      RETURN_NOT_OK(SetStringCopy(column_idx, *reinterpret_cast<const Slice*>(val)));
      break;
    };
    case DataType::BINARY: {
      RETURN_NOT_OK(SetBinaryCopy(column_idx, *reinterpret_cast<const Slice*>(val)));
      break;
    };
    case DataType::TIMESTAMP: {
      RETURN_NOT_OK(SetTimestamp(column_idx, *reinterpret_cast<const int64_t*>(val)));
      break;
    };
    case DataType::INET: {
      RETURN_NOT_OK(SetInet(column_idx, *reinterpret_cast<const Slice*>(val)));
      break;
    };
    case DataType::JSONB: {
      RETURN_NOT_OK(SetJsonb(column_idx, *reinterpret_cast<const Slice*>(val)));
      break;
    };
    case DataType::UUID: {
      RETURN_NOT_OK(SetUuidCopy(column_idx, *reinterpret_cast<const Slice*>(val)));
      break;
    };
    case DataType::TIMEUUID: {
      RETURN_NOT_OK(SetTimeUuidCopy(column_idx, *reinterpret_cast<const Slice*>(val)));
      break;
    }
    case DataType::FROZEN: {
      RETURN_NOT_OK(SetFrozenCopy(column_idx, *reinterpret_cast<const Slice*>(val)));
      break;
    };
    case DataType::DECIMAL: FALLTHROUGH_INTENDED;
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
    CHECK(col.type_info()->var_length());
    auto dst = row.CellSlice(col_idx);
    delete [] dst.data();
    BitmapClear(owned_strings_bitmap_, col_idx);
  }
}

void YBPartialRow::DeallocateOwnedStrings() {
  for (size_t i = 0; i < schema_->num_columns(); i++) {
    DeallocateStringIfSet(i, schema_->column(i));
  }
}

//------------------------------------------------------------
// Setters
//------------------------------------------------------------

Status YBPartialRow::SetBool(Slice col_name, bool val) {
  return Set<TypeTraits<DataType::BOOL>>(col_name, val);
}
Status YBPartialRow::SetInt8(Slice col_name, int8_t val) {
  return Set<TypeTraits<DataType::INT8>>(col_name, val);
}
Status YBPartialRow::SetInt16(Slice col_name, int16_t val) {
  return Set<TypeTraits<DataType::INT16>>(col_name, val);
}
Status YBPartialRow::SetInt32(Slice col_name, int32_t val) {
  return Set<TypeTraits<DataType::INT32>>(col_name, val);
}
Status YBPartialRow::SetInt64(Slice col_name, int64_t val) {
  return Set<TypeTraits<DataType::INT64>>(col_name, val);
}
Status YBPartialRow::SetTimestamp(Slice col_name, int64_t val) {
  return Set<TypeTraits<DataType::TIMESTAMP>>(col_name, val);
}
Status YBPartialRow::SetFloat(Slice col_name, float val) {
  return Set<TypeTraits<DataType::FLOAT>>(col_name, val);
}
Status YBPartialRow::SetDouble(Slice col_name, double val) {
  return Set<TypeTraits<DataType::DOUBLE>>(col_name, val);
}
Status YBPartialRow::SetString(Slice col_name, Slice val) {
  return Set<TypeTraits<DataType::STRING>>(col_name, val, false);
}
Status YBPartialRow::SetBinary(Slice col_name, Slice val) {
  return Set<TypeTraits<DataType::BINARY>>(col_name, val, false);
}
Status YBPartialRow::SetFrozen(Slice col_name, Slice val) {
  return Set<TypeTraits<DataType::FROZEN>>(col_name, val, false);
}
Status YBPartialRow::SetInet(Slice col_name, Slice val) {
  return SetSliceCopy<TypeTraits<DataType::INET>>(col_name, val);
}
Status YBPartialRow::SetUuid(Slice col_name, Slice val) {
  return Set<TypeTraits<DataType::UUID>>(col_name, val, false);
}
Status YBPartialRow::SetTimeUuid(Slice col_name, Slice val) {
  return Set<TypeTraits<DataType::TIMEUUID>>(col_name, val, false);
}
Status YBPartialRow::SetDecimal(Slice col_name, Slice val) {
  return Set<TypeTraits<DataType::DECIMAL>>(col_name, val, false);
}
Status YBPartialRow::SetBool(size_t col_idx, bool val) {
  return Set<TypeTraits<DataType::BOOL>>(col_idx, val);
}
Status YBPartialRow::SetInt8(size_t col_idx, int8_t val) {
  return Set<TypeTraits<DataType::INT8>>(col_idx, val);
}
Status YBPartialRow::SetInt16(size_t col_idx, int16_t val) {
  return Set<TypeTraits<DataType::INT16>>(col_idx, val);
}
Status YBPartialRow::SetInt32(size_t col_idx, int32_t val) {
  return Set<TypeTraits<DataType::INT32>>(col_idx, val);
}
Status YBPartialRow::SetInt64(size_t col_idx, int64_t val) {
  return Set<TypeTraits<DataType::INT64>>(col_idx, val);
}
Status YBPartialRow::SetTimestamp(size_t col_idx, int64_t val) {
  return Set<TypeTraits<DataType::TIMESTAMP>>(col_idx, val);
}
Status YBPartialRow::SetString(size_t col_idx, Slice val) {
  return Set<TypeTraits<DataType::STRING>>(col_idx, val, false);
}
Status YBPartialRow::SetBinary(size_t col_idx, Slice val) {
  return Set<TypeTraits<DataType::BINARY>>(col_idx, val, false);
}
Status YBPartialRow::SetFrozen(size_t col_idx, Slice val) {
  return Set<TypeTraits<DataType::FROZEN>>(col_idx, val, false);
}
Status YBPartialRow::SetInet(size_t col_idx, Slice val) {
  return SetSliceCopy<TypeTraits<DataType::INET>>(col_idx, val);
}
Status YBPartialRow::SetJsonb(size_t col_idx, Slice val) {
  return SetSliceCopy<TypeTraits<DataType::JSONB>>(col_idx, val);
}
Status YBPartialRow::SetUuid(size_t col_idx, Slice val) {
  return Set<TypeTraits<DataType::UUID>>(col_idx, val, false);
}
Status YBPartialRow::SetTimeUuid(size_t col_idx, Slice val) {
  return Set<TypeTraits<DataType::TIMEUUID>>(col_idx, val, false);
}
Status YBPartialRow::SetDecimal(size_t col_idx, Slice val) {
  return Set<TypeTraits<DataType::DECIMAL>>(col_idx, val, false);
}
Status YBPartialRow::SetFloat(size_t col_idx, float val) {
  return Set<TypeTraits<DataType::FLOAT>>(col_idx, util::CanonicalizeFloat(val));
}
Status YBPartialRow::SetDouble(size_t col_idx, double val) {
  return Set<TypeTraits<DataType::DOUBLE>>(col_idx, util::CanonicalizeDouble(val));
}

Status YBPartialRow::SetBinaryCopy(Slice col_name, Slice val) {
  return SetSliceCopy<TypeTraits<DataType::BINARY>>(col_name, val);
}
Status YBPartialRow::SetBinaryCopy(size_t col_idx, Slice val) {
  return SetSliceCopy<TypeTraits<DataType::BINARY>>(col_idx, val);
}
Status YBPartialRow::SetStringCopy(Slice col_name, Slice val) {
  return SetSliceCopy<TypeTraits<DataType::STRING>>(col_name, val);
}
Status YBPartialRow::SetStringCopy(size_t col_idx, Slice val) {
  return SetSliceCopy<TypeTraits<DataType::STRING>>(col_idx, val);
}
Status YBPartialRow::SetUuidCopy(Slice col_name, Slice val) {
  return SetSliceCopy<TypeTraits<DataType::UUID>>(col_name, val);
}
Status YBPartialRow::SetUuidCopy(size_t col_idx, Slice val) {
  return SetSliceCopy<TypeTraits<DataType::UUID>>(col_idx, val);
}
Status YBPartialRow::SetTimeUuidCopy(Slice col_name, Slice val) {
  return SetSliceCopy<TypeTraits<DataType::TIMEUUID>>(col_name, val);
}
Status YBPartialRow::SetTimeUuidCopy(size_t col_idx, Slice val) {
  return SetSliceCopy<TypeTraits<DataType::TIMEUUID>>(col_idx, val);
}
Status YBPartialRow::SetFrozenCopy(Slice col_name, Slice val) {
  return SetSliceCopy<TypeTraits<DataType::FROZEN>>(col_name, val);
}
Status YBPartialRow::SetFrozenCopy(size_t col_idx, Slice val) {
  return SetSliceCopy<TypeTraits<DataType::FROZEN>>(col_idx, val);
}

template<typename T>
Status YBPartialRow::SetSliceCopy(Slice col_name, Slice val) {
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
Status YBPartialRow::SetSliceCopy(size_t col_idx, Slice val) {
  auto relocated = new uint8_t[val.size()];
  memcpy(relocated, val.data(), val.size());
  Slice relocated_val(relocated, val.size());
  Status s = Set<T>(col_idx, relocated_val, true);
  if (!s.ok()) {
    delete [] relocated;
  }
  return s;
}

Status YBPartialRow::SetNull(Slice col_name) {
  return SetNull(VERIFY_RESULT(FindColumn(*schema_, col_name)));
}

Status YBPartialRow::SetNull(size_t col_idx) {
  const ColumnSchema& col = schema_->column(col_idx);
  if (PREDICT_FALSE(!col.is_nullable())) {
    return STATUS(InvalidArgument, "column not nullable", col.ToString());
  }

  if (col.type_info()->physical_type == DataType::BINARY) {
    DeallocateStringIfSet(col_idx, col);
  }

  ContiguousRow row(schema_, row_data_);
  row.set_null(col_idx, true);

  // Mark the column as set.
  BitmapSet(isset_bitmap_, col_idx);
  return Status::OK();
}

Status YBPartialRow::Unset(Slice col_name) {
  return Unset(VERIFY_RESULT(FindColumn(*schema_, col_name)));
}

Status YBPartialRow::Unset(size_t col_idx) {
  const ColumnSchema& col = schema_->column(col_idx);
  if (col.type_info()->physical_type == DataType::BINARY) {
    DeallocateStringIfSet(col_idx, col);
  }
  BitmapClear(isset_bitmap_, col_idx);
  return Status::OK();
}

//------------------------------------------------------------
// Template instantiations: We instantiate all possible templates to avoid linker issues.
// see: https://isocpp.org/wiki/faq/templates#separate-template-fn-defn-from-decl
// TODO We can probably remove this when we move to c++11 and can use "extern template"
//------------------------------------------------------------

template
Status YBPartialRow::SetSliceCopy<TypeTraits<DataType::STRING>>(size_t col_idx, Slice val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<DataType::BINARY>>(size_t col_idx, Slice val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<DataType::INET>>(size_t col_idx, Slice val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<DataType::JSONB>>(size_t col_idx, Slice val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<DataType::UUID>>(size_t col_idx, Slice val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<DataType::TIMEUUID>>(size_t col_idx, Slice val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<DataType::STRING>>(Slice col_name, Slice val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<DataType::BINARY>>(Slice col_name, Slice val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<DataType::INET>>(Slice col_name, Slice val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<DataType::JSONB>>(Slice col_name, Slice val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<DataType::UUID>>(Slice col_name, Slice val);

template
Status YBPartialRow::SetSliceCopy<TypeTraits<DataType::TIMEUUID>>(Slice col_name, Slice val);

template
Status YBPartialRow::Set<TypeTraits<DataType::INT8>>(size_t col_idx,
                                              const TypeTraits<DataType::INT8>::cpp_type& val,
                                              bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::INT16>>(size_t col_idx,
                                               const TypeTraits<DataType::INT16>::cpp_type& val,
                                               bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::INT32>>(size_t col_idx,
                                               const TypeTraits<DataType::INT32>::cpp_type& val,
                                               bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::INT64>>(size_t col_idx,
                                               const TypeTraits<DataType::INT64>::cpp_type& val,
                                               bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::TIMESTAMP>>(
    size_t col_idx,
    const TypeTraits<DataType::TIMESTAMP>::cpp_type& val,
    bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::STRING>>(size_t col_idx,
                                                const TypeTraits<DataType::STRING>::cpp_type& val,
                                                bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::BINARY>>(size_t col_idx,
                                                const TypeTraits<DataType::BINARY>::cpp_type& val,
                                                bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::FLOAT>>(size_t col_idx,
                                               const TypeTraits<DataType::FLOAT>::cpp_type& val,
                                               bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::DOUBLE>>(size_t col_idx,
                                                const TypeTraits<DataType::DOUBLE>::cpp_type& val,
                                                bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::BOOL>>(size_t col_idx,
                                              const TypeTraits<DataType::BOOL>::cpp_type& val,
                                              bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::INT8>>(Slice col_name,
                                              const TypeTraits<DataType::INT8>::cpp_type& val,
                                              bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::INT16>>(Slice col_name,
                                               const TypeTraits<DataType::INT16>::cpp_type& val,
                                               bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::INT32>>(Slice col_name,
                                               const TypeTraits<DataType::INT32>::cpp_type& val,
                                               bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::INT64>>(Slice col_name,
                                               const TypeTraits<DataType::INT64>::cpp_type& val,
                                               bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::TIMESTAMP>>(
    Slice col_name,
    const TypeTraits<DataType::TIMESTAMP>::cpp_type& val,
    bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::FLOAT>>(Slice col_name,
                                               const TypeTraits<DataType::FLOAT>::cpp_type& val,
                                               bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::DOUBLE>>(Slice col_name,
                                                const TypeTraits<DataType::DOUBLE>::cpp_type& val,
                                                bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::BOOL>>(Slice col_name,
                                              const TypeTraits<DataType::BOOL>::cpp_type& val,
                                              bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::STRING>>(Slice col_name,
                                                const TypeTraits<DataType::STRING>::cpp_type& val,
                                                bool owned);

template
Status YBPartialRow::Set<TypeTraits<DataType::BINARY>>(Slice col_name,
                                                const TypeTraits<DataType::BINARY>::cpp_type& val,
                                                bool owned);

//------------------------------------------------------------
// Getters
//------------------------------------------------------------
bool YBPartialRow::IsColumnSet(size_t col_idx) const {
  DCHECK_GE(col_idx, 0);
  DCHECK_LT(col_idx, schema_->num_columns());
  return BitmapTest(isset_bitmap_, col_idx);
}

bool YBPartialRow::IsColumnSet(Slice col_name) const {
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

bool YBPartialRow::IsNull(Slice col_name) const {
  return IsNull(CHECK_RESULT(FindColumn(*schema_, col_name)));
}

Status YBPartialRow::GetBool(Slice col_name, bool* val) const {
  return Get<TypeTraits<DataType::BOOL>>(col_name, val);
}
Status YBPartialRow::GetInt8(Slice col_name, int8_t* val) const {
  return Get<TypeTraits<DataType::INT8>>(col_name, val);
}
Status YBPartialRow::GetInt16(Slice col_name, int16_t* val) const {
  return Get<TypeTraits<DataType::INT16>>(col_name, val);
}
Status YBPartialRow::GetInt32(Slice col_name, int32_t* val) const {
  return Get<TypeTraits<DataType::INT32>>(col_name, val);
}
Status YBPartialRow::GetInt64(Slice col_name, int64_t* val) const {
  return Get<TypeTraits<DataType::INT64>>(col_name, val);
}
Status YBPartialRow::GetTimestamp(Slice col_name, int64_t* micros_since_utc_epoch) const {
  return Get<TypeTraits<DataType::TIMESTAMP>>(col_name, micros_since_utc_epoch);
}
Status YBPartialRow::GetFloat(Slice col_name, float* val) const {
  return Get<TypeTraits<DataType::FLOAT>>(col_name, val);
}
Status YBPartialRow::GetDouble(Slice col_name, double* val) const {
  return Get<TypeTraits<DataType::DOUBLE>>(col_name, val);
}
Status YBPartialRow::GetString(Slice col_name, Slice* val) const {
  return Get<TypeTraits<DataType::STRING>>(col_name, val);
}
Status YBPartialRow::GetBinary(Slice col_name, Slice* val) const {
  return Get<TypeTraits<DataType::BINARY>>(col_name, val);
}
Status YBPartialRow::GetInet(Slice col_name, Slice* val) const {
  return Get<TypeTraits<DataType::INET>>(col_name, val);
}
Status YBPartialRow::GetJsonb(Slice col_name, Slice* val) const {
  return Get<TypeTraits<DataType::JSONB>>(col_name, val);
}
Status YBPartialRow::GetUuid(Slice col_name, Slice* val) const {
  return Get<TypeTraits<DataType::UUID>>(col_name, val);
}
Status YBPartialRow::GetTimeUuid(Slice col_name, Slice* val) const {
  return Get<TypeTraits<DataType::TIMEUUID>>(col_name, val);
}
Status YBPartialRow::GetBool(size_t col_idx, bool* val) const {
  return Get<TypeTraits<DataType::BOOL>>(col_idx, val);
}
Status YBPartialRow::GetInt8(size_t col_idx, int8_t* val) const {
  return Get<TypeTraits<DataType::INT8>>(col_idx, val);
}
Status YBPartialRow::GetInt16(size_t col_idx, int16_t* val) const {
  return Get<TypeTraits<DataType::INT16>>(col_idx, val);
}
Status YBPartialRow::GetInt32(size_t col_idx, int32_t* val) const {
  return Get<TypeTraits<DataType::INT32>>(col_idx, val);
}
Status YBPartialRow::GetInt64(size_t col_idx, int64_t* val) const {
  return Get<TypeTraits<DataType::INT64>>(col_idx, val);
}
Status YBPartialRow::GetTimestamp(size_t col_idx, int64_t* micros_since_utc_epoch) const {
  return Get<TypeTraits<DataType::TIMESTAMP>>(col_idx, micros_since_utc_epoch);
}
Status YBPartialRow::GetFloat(size_t col_idx, float* val) const {
  return Get<TypeTraits<DataType::FLOAT>>(col_idx, val);
}
Status YBPartialRow::GetDouble(size_t col_idx, double* val) const {
  return Get<TypeTraits<DataType::DOUBLE>>(col_idx, val);
}
Status YBPartialRow::GetString(size_t col_idx, Slice* val) const {
  return Get<TypeTraits<DataType::STRING>>(col_idx, val);
}
Status YBPartialRow::GetBinary(size_t col_idx, Slice* val) const {
  return Get<TypeTraits<DataType::BINARY>>(col_idx, val);
}
Status YBPartialRow::GetInet(size_t col_idx, Slice* val) const {
  return Get<TypeTraits<DataType::INET>>(col_idx, val);
}
Status YBPartialRow::GetJsonb(size_t col_idx, Slice* val) const {
  return Get<TypeTraits<DataType::JSONB>>(col_idx, val);
}
Status YBPartialRow::GetUuid(size_t col_idx, Slice* val) const {
  return Get<TypeTraits<DataType::UUID>>(col_idx, val);
}
Status YBPartialRow::GetTimeUuid(size_t col_idx, Slice* val) const {
  return Get<TypeTraits<DataType::TIMEUUID>>(col_idx, val);
}

template<typename T>
Status YBPartialRow::Get(Slice col_name,
                         typename T::cpp_type* val) const {
  return Get<T>(VERIFY_RESULT(FindColumn(*schema_, col_name)), val);
}

template<typename T>
Status YBPartialRow::Get(size_t col_idx, typename T::cpp_type* val) const {
  const ColumnSchema& col = schema_->column(col_idx);
  if (PREDICT_FALSE(col.type_info()->type != T::type)) {
    // TODO: at some point we could allow type coercion here.
    return STATUS(InvalidArgument,
      Substitute("invalid type $0 provided for column '$1' (expected $2)",
                 T::name(),
                 col.name(), col.type_info()->name));
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
  for (size_t i = 0; i < schema_->num_columns(); i++) {
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

} // namespace yb::dockv
