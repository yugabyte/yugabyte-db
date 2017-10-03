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

#include "kudu/common/partial_row.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "kudu/common/common.pb.h"
#include "kudu/common/row.h"
#include "kudu/common/schema.h"
#include "kudu/common/wire_protocol.pb.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/bitmap.h"
#include "kudu/util/status.h"

using strings::Substitute;

namespace kudu {

namespace {
inline Status FindColumn(const Schema& schema, const Slice& col_name, int* idx) {
  StringPiece sp(reinterpret_cast<const char*>(col_name.data()), col_name.size());
  *idx = schema.find_column(sp);
  if (PREDICT_FALSE(*idx == -1)) {
    return Status::NotFound("No such column", col_name);
  }
  return Status::OK();
}
} // anonymous namespace

KuduPartialRow::KuduPartialRow(const Schema* schema)
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

KuduPartialRow::~KuduPartialRow() {
  DeallocateOwnedStrings();
  // Both the row data and bitmap came from the same allocation.
  // The bitmap is at the start of it.
  delete [] isset_bitmap_;
}

KuduPartialRow::KuduPartialRow(const KuduPartialRow& other)
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
  for (int col_idx = 0; col_idx < schema_->num_columns(); col_idx++) {
    if (BitmapTest(owned_strings_bitmap_, col_idx)) {
      ContiguousRow row(schema_, row_data_);
      Slice* slice = reinterpret_cast<Slice*>(row.mutable_cell_ptr(col_idx));
      auto data = new uint8_t[slice->size()];
      slice->relocate(data);
    }
  }
}

KuduPartialRow& KuduPartialRow::operator=(KuduPartialRow other) {
  std::swap(schema_, other.schema_);
  std::swap(isset_bitmap_, other.isset_bitmap_);
  std::swap(owned_strings_bitmap_, other.owned_strings_bitmap_);
  std::swap(row_data_, other.row_data_);
  return *this;
}

template<typename T>
Status KuduPartialRow::Set(const Slice& col_name,
                           const typename T::cpp_type& val,
                           bool owned) {
  int col_idx;
  RETURN_NOT_OK(FindColumn(*schema_, col_name, &col_idx));
  return Set<T>(col_idx, val, owned);
}

template<typename T>
Status KuduPartialRow::Set(int col_idx,
                           const typename T::cpp_type& val,
                           bool owned) {
  const ColumnSchema& col = schema_->column(col_idx);
  if (PREDICT_FALSE(col.type_info()->type() != T::type)) {
    // TODO: at some point we could allow type coercion here.
    return Status::InvalidArgument(
      Substitute("invalid type $0 provided for column '$1' (expected $2)",
                 T::name(),
                 col.name(), col.type_info()->name()));
  }

  ContiguousRow row(schema_, row_data_);

  // If we're replacing an existing STRING/BINARY value, deallocate the old value.
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

Status KuduPartialRow::Set(int32_t column_idx, const uint8_t* val) {
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
    default: {
      return Status::InvalidArgument("Unknown column type in schema",
                                     column_schema.ToString());
    };
  }
  return Status::OK();
}

void KuduPartialRow::DeallocateStringIfSet(int col_idx, const ColumnSchema& col) {
  if (BitmapTest(owned_strings_bitmap_, col_idx)) {
    ContiguousRow row(schema_, row_data_);
    const Slice* dst;
    if (col.type_info()->type() == BINARY) {
      dst = schema_->ExtractColumnFromRow<BINARY>(row, col_idx);
    } else {
      CHECK(col.type_info()->type() == STRING);
      dst = schema_->ExtractColumnFromRow<STRING>(row, col_idx);
    }
    delete [] dst->data();
    BitmapClear(owned_strings_bitmap_, col_idx);
  }
}

void KuduPartialRow::DeallocateOwnedStrings() {
  for (int i = 0; i < schema_->num_columns(); i++) {
    DeallocateStringIfSet(i, schema_->column(i));
  }
}

//------------------------------------------------------------
// Setters
//------------------------------------------------------------

Status KuduPartialRow::SetBool(const Slice& col_name, bool val) {
  return Set<TypeTraits<BOOL> >(col_name, val);
}
Status KuduPartialRow::SetInt8(const Slice& col_name, int8_t val) {
  return Set<TypeTraits<INT8> >(col_name, val);
}
Status KuduPartialRow::SetInt16(const Slice& col_name, int16_t val) {
  return Set<TypeTraits<INT16> >(col_name, val);
}
Status KuduPartialRow::SetInt32(const Slice& col_name, int32_t val) {
  return Set<TypeTraits<INT32> >(col_name, val);
}
Status KuduPartialRow::SetInt64(const Slice& col_name, int64_t val) {
  return Set<TypeTraits<INT64> >(col_name, val);
}
Status KuduPartialRow::SetTimestamp(const Slice& col_name, int64_t val) {
  return Set<TypeTraits<TIMESTAMP> >(col_name, val);
}
Status KuduPartialRow::SetFloat(const Slice& col_name, float val) {
  return Set<TypeTraits<FLOAT> >(col_name, val);
}
Status KuduPartialRow::SetDouble(const Slice& col_name, double val) {
  return Set<TypeTraits<DOUBLE> >(col_name, val);
}
Status KuduPartialRow::SetString(const Slice& col_name, const Slice& val) {
  return Set<TypeTraits<STRING> >(col_name, val, false);
}
Status KuduPartialRow::SetBinary(const Slice& col_name, const Slice& val) {
  return Set<TypeTraits<BINARY> >(col_name, val, false);
}
Status KuduPartialRow::SetBool(int col_idx, bool val) {
  return Set<TypeTraits<BOOL> >(col_idx, val);
}
Status KuduPartialRow::SetInt8(int col_idx, int8_t val) {
  return Set<TypeTraits<INT8> >(col_idx, val);
}
Status KuduPartialRow::SetInt16(int col_idx, int16_t val) {
  return Set<TypeTraits<INT16> >(col_idx, val);
}
Status KuduPartialRow::SetInt32(int col_idx, int32_t val) {
  return Set<TypeTraits<INT32> >(col_idx, val);
}
Status KuduPartialRow::SetInt64(int col_idx, int64_t val) {
  return Set<TypeTraits<INT64> >(col_idx, val);
}
Status KuduPartialRow::SetTimestamp(int col_idx, int64_t val) {
  return Set<TypeTraits<TIMESTAMP> >(col_idx, val);
}
Status KuduPartialRow::SetString(int col_idx, const Slice& val) {
  return Set<TypeTraits<STRING> >(col_idx, val, false);
}
Status KuduPartialRow::SetBinary(int col_idx, const Slice& val) {
  return Set<TypeTraits<BINARY> >(col_idx, val, false);
}
Status KuduPartialRow::SetFloat(int col_idx, float val) {
  return Set<TypeTraits<FLOAT> >(col_idx, val);
}
Status KuduPartialRow::SetDouble(int col_idx, double val) {
  return Set<TypeTraits<DOUBLE> >(col_idx, val);
}

Status KuduPartialRow::SetBinaryCopy(const Slice& col_name, const Slice& val) {
  return SetSliceCopy<TypeTraits<BINARY> >(col_name, val);
}
Status KuduPartialRow::SetBinaryCopy(int col_idx, const Slice& val) {
  return SetSliceCopy<TypeTraits<BINARY> >(col_idx, val);
}
Status KuduPartialRow::SetStringCopy(const Slice& col_name, const Slice& val) {
  return SetSliceCopy<TypeTraits<STRING> >(col_name, val);
}
Status KuduPartialRow::SetStringCopy(int col_idx, const Slice& val) {
  return SetSliceCopy<TypeTraits<STRING> >(col_idx, val);
}

template<typename T>
Status KuduPartialRow::SetSliceCopy(const Slice& col_name, const Slice& val) {
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
Status KuduPartialRow::SetSliceCopy(int col_idx, const Slice& val) {
  auto relocated = new uint8_t[val.size()];
  memcpy(relocated, val.data(), val.size());
  Slice relocated_val(relocated, val.size());
  Status s = Set<T>(col_idx, relocated_val, true);
  if (!s.ok()) {
    delete [] relocated;
  }
  return s;
}

Status KuduPartialRow::SetNull(const Slice& col_name) {
  int col_idx;
  RETURN_NOT_OK(FindColumn(*schema_, col_name, &col_idx));
  return SetNull(col_idx);
}

Status KuduPartialRow::SetNull(int col_idx) {
  const ColumnSchema& col = schema_->column(col_idx);
  if (PREDICT_FALSE(!col.is_nullable())) {
    return Status::InvalidArgument("column not nullable", col.ToString());
  }

  if (col.type_info()->physical_type() == BINARY) DeallocateStringIfSet(col_idx, col);

  ContiguousRow row(schema_, row_data_);
  row.set_null(col_idx, true);

  // Mark the column as set.
  BitmapSet(isset_bitmap_, col_idx);
  return Status::OK();
}

Status KuduPartialRow::Unset(const Slice& col_name) {
  int col_idx;
  RETURN_NOT_OK(FindColumn(*schema_, col_name, &col_idx));
  return Unset(col_idx);
}

Status KuduPartialRow::Unset(int col_idx) {
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
Status KuduPartialRow::SetSliceCopy<TypeTraits<STRING> >(int col_idx, const Slice& val);

template
Status KuduPartialRow::SetSliceCopy<TypeTraits<BINARY> >(int col_idx, const Slice& val);

template
Status KuduPartialRow::SetSliceCopy<TypeTraits<STRING> >(const Slice& col_name, const Slice& val);

template
Status KuduPartialRow::SetSliceCopy<TypeTraits<BINARY> >(const Slice& col_name, const Slice& val);

template
Status KuduPartialRow::Set<TypeTraits<INT8> >(int col_idx,
                                              const TypeTraits<INT8>::cpp_type& val,
                                              bool owned);

template
Status KuduPartialRow::Set<TypeTraits<INT16> >(int col_idx,
                                               const TypeTraits<INT16>::cpp_type& val,
                                               bool owned);

template
Status KuduPartialRow::Set<TypeTraits<INT32> >(int col_idx,
                                               const TypeTraits<INT32>::cpp_type& val,
                                               bool owned);

template
Status KuduPartialRow::Set<TypeTraits<INT64> >(int col_idx,
                                               const TypeTraits<INT64>::cpp_type& val,
                                               bool owned);

template
Status KuduPartialRow::Set<TypeTraits<TIMESTAMP> >(
    int col_idx,
    const TypeTraits<TIMESTAMP>::cpp_type& val,
    bool owned);

template
Status KuduPartialRow::Set<TypeTraits<STRING> >(int col_idx,
                                                const TypeTraits<STRING>::cpp_type& val,
                                                bool owned);

template
Status KuduPartialRow::Set<TypeTraits<BINARY> >(int col_idx,
                                                const TypeTraits<BINARY>::cpp_type& val,
                                                bool owned);

template
Status KuduPartialRow::Set<TypeTraits<FLOAT> >(int col_idx,
                                               const TypeTraits<FLOAT>::cpp_type& val,
                                               bool owned);

template
Status KuduPartialRow::Set<TypeTraits<DOUBLE> >(int col_idx,
                                                const TypeTraits<DOUBLE>::cpp_type& val,
                                                bool owned);

template
Status KuduPartialRow::Set<TypeTraits<BOOL> >(int col_idx,
                                              const TypeTraits<BOOL>::cpp_type& val,
                                              bool owned);

template
Status KuduPartialRow::Set<TypeTraits<INT8> >(const Slice& col_name,
                                              const TypeTraits<INT8>::cpp_type& val,
                                              bool owned);

template
Status KuduPartialRow::Set<TypeTraits<INT16> >(const Slice& col_name,
                                               const TypeTraits<INT16>::cpp_type& val,
                                               bool owned);

template
Status KuduPartialRow::Set<TypeTraits<INT32> >(const Slice& col_name,
                                               const TypeTraits<INT32>::cpp_type& val,
                                               bool owned);

template
Status KuduPartialRow::Set<TypeTraits<INT64> >(const Slice& col_name,
                                               const TypeTraits<INT64>::cpp_type& val,
                                               bool owned);

template
Status KuduPartialRow::Set<TypeTraits<TIMESTAMP> >(
    const Slice& col_name,
    const TypeTraits<TIMESTAMP>::cpp_type& val,
    bool owned);

template
Status KuduPartialRow::Set<TypeTraits<FLOAT> >(const Slice& col_name,
                                               const TypeTraits<FLOAT>::cpp_type& val,
                                               bool owned);

template
Status KuduPartialRow::Set<TypeTraits<DOUBLE> >(const Slice& col_name,
                                                const TypeTraits<DOUBLE>::cpp_type& val,
                                                bool owned);

template
Status KuduPartialRow::Set<TypeTraits<BOOL> >(const Slice& col_name,
                                              const TypeTraits<BOOL>::cpp_type& val,
                                              bool owned);

template
Status KuduPartialRow::Set<TypeTraits<STRING> >(const Slice& col_name,
                                                const TypeTraits<STRING>::cpp_type& val,
                                                bool owned);

template
Status KuduPartialRow::Set<TypeTraits<BINARY> >(const Slice& col_name,
                                                const TypeTraits<BINARY>::cpp_type& val,
                                                bool owned);

//------------------------------------------------------------
// Getters
//------------------------------------------------------------
bool KuduPartialRow::IsColumnSet(int col_idx) const {
  DCHECK_GE(col_idx, 0);
  DCHECK_LT(col_idx, schema_->num_columns());
  return BitmapTest(isset_bitmap_, col_idx);
}

bool KuduPartialRow::IsColumnSet(const Slice& col_name) const {
  int col_idx;
  CHECK_OK(FindColumn(*schema_, col_name, &col_idx));
  return IsColumnSet(col_idx);
}

bool KuduPartialRow::IsNull(int col_idx) const {
  const ColumnSchema& col = schema_->column(col_idx);
  if (!col.is_nullable()) {
    return false;
  }

  if (!IsColumnSet(col_idx)) return false;

  ContiguousRow row(schema_, row_data_);
  return row.is_null(col_idx);
}

bool KuduPartialRow::IsNull(const Slice& col_name) const {
  int col_idx;
  CHECK_OK(FindColumn(*schema_, col_name, &col_idx));
  return IsNull(col_idx);
}

Status KuduPartialRow::GetBool(const Slice& col_name, bool* val) const {
  return Get<TypeTraits<BOOL> >(col_name, val);
}
Status KuduPartialRow::GetInt8(const Slice& col_name, int8_t* val) const {
  return Get<TypeTraits<INT8> >(col_name, val);
}
Status KuduPartialRow::GetInt16(const Slice& col_name, int16_t* val) const {
  return Get<TypeTraits<INT16> >(col_name, val);
}
Status KuduPartialRow::GetInt32(const Slice& col_name, int32_t* val) const {
  return Get<TypeTraits<INT32> >(col_name, val);
}
Status KuduPartialRow::GetInt64(const Slice& col_name, int64_t* val) const {
  return Get<TypeTraits<INT64> >(col_name, val);
}
Status KuduPartialRow::GetTimestamp(const Slice& col_name, int64_t* micros_since_utc_epoch) const {
  return Get<TypeTraits<TIMESTAMP> >(col_name, micros_since_utc_epoch);
}
Status KuduPartialRow::GetFloat(const Slice& col_name, float* val) const {
  return Get<TypeTraits<FLOAT> >(col_name, val);
}
Status KuduPartialRow::GetDouble(const Slice& col_name, double* val) const {
  return Get<TypeTraits<DOUBLE> >(col_name, val);
}
Status KuduPartialRow::GetString(const Slice& col_name, Slice* val) const {
  return Get<TypeTraits<STRING> >(col_name, val);
}
Status KuduPartialRow::GetBinary(const Slice& col_name, Slice* val) const {
  return Get<TypeTraits<BINARY> >(col_name, val);
}

Status KuduPartialRow::GetBool(int col_idx, bool* val) const {
  return Get<TypeTraits<BOOL> >(col_idx, val);
}
Status KuduPartialRow::GetInt8(int col_idx, int8_t* val) const {
  return Get<TypeTraits<INT8> >(col_idx, val);
}
Status KuduPartialRow::GetInt16(int col_idx, int16_t* val) const {
  return Get<TypeTraits<INT16> >(col_idx, val);
}
Status KuduPartialRow::GetInt32(int col_idx, int32_t* val) const {
  return Get<TypeTraits<INT32> >(col_idx, val);
}
Status KuduPartialRow::GetInt64(int col_idx, int64_t* val) const {
  return Get<TypeTraits<INT64> >(col_idx, val);
}
Status KuduPartialRow::GetTimestamp(int col_idx, int64_t* micros_since_utc_epoch) const {
  return Get<TypeTraits<TIMESTAMP> >(col_idx, micros_since_utc_epoch);
}
Status KuduPartialRow::GetFloat(int col_idx, float* val) const {
  return Get<TypeTraits<FLOAT> >(col_idx, val);
}
Status KuduPartialRow::GetDouble(int col_idx, double* val) const {
  return Get<TypeTraits<DOUBLE> >(col_idx, val);
}
Status KuduPartialRow::GetString(int col_idx, Slice* val) const {
  return Get<TypeTraits<STRING> >(col_idx, val);
}
Status KuduPartialRow::GetBinary(int col_idx, Slice* val) const {
  return Get<TypeTraits<BINARY> >(col_idx, val);
}

template<typename T>
Status KuduPartialRow::Get(const Slice& col_name,
                           typename T::cpp_type* val) const {
  int col_idx;
  RETURN_NOT_OK(FindColumn(*schema_, col_name, &col_idx));
  return Get<T>(col_idx, val);
}

template<typename T>
Status KuduPartialRow::Get(int col_idx, typename T::cpp_type* val) const {
  const ColumnSchema& col = schema_->column(col_idx);
  if (PREDICT_FALSE(col.type_info()->type() != T::type)) {
    // TODO: at some point we could allow type coercion here.
    return Status::InvalidArgument(
      Substitute("invalid type $0 provided for column '$1' (expected $2)",
                 T::name(),
                 col.name(), col.type_info()->name()));
  }

  if (PREDICT_FALSE(!IsColumnSet(col_idx))) {
    return Status::NotFound("column not set");
  }
  if (col.is_nullable() && IsNull(col_idx)) {
    return Status::NotFound("column is NULL");
  }

  ContiguousRow row(schema_, row_data_);
  memcpy(val, row.cell_ptr(col_idx), sizeof(*val));
  return Status::OK();
}


//------------------------------------------------------------
// Key-encoding related functions
//------------------------------------------------------------
Status KuduPartialRow::EncodeRowKey(string* encoded_key) const {
  // Currently, a row key must be fully specified.
  // TODO: allow specifying a prefix of the key, and automatically
  // fill the rest with minimum values.
  for (int i = 0; i < schema_->num_key_columns(); i++) {
    if (PREDICT_FALSE(!IsColumnSet(i))) {
      return Status::InvalidArgument("All key columns must be set",
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

string KuduPartialRow::ToEncodedRowKeyOrDie() const {
  string ret;
  CHECK_OK(EncodeRowKey(&ret));
  return ret;
}

//------------------------------------------------------------
// Utility code
//------------------------------------------------------------

bool KuduPartialRow::AllColumnsSet() const {
  return BitMapIsAllSet(isset_bitmap_, 0, schema_->num_columns());
}

bool KuduPartialRow::IsKeySet() const {
  return BitMapIsAllSet(isset_bitmap_, 0, schema_->num_key_columns());
}


std::string KuduPartialRow::ToString() const {
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


} // namespace kudu
