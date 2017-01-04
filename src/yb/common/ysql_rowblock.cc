// Copyright (c) YugaByte, Inc.
//
// This file contains the classes that represent a YSQL row and a row block.

#include "yb/common/ysql_rowblock.h"

#include "yb/common/wire_protocol.h"

namespace yb {

using std::shared_ptr;

//----------------------------------------- YSQL row ----------------------------------------
YSQLRow::YSQLRow(const shared_ptr<const Schema>& schema)
    : schema_(schema),
      // Allocate just the YSQLValueCore array memory here. Explicit constructor call follows below.
      values_(reinterpret_cast<YSQLValueCore*>(
          ::operator new(sizeof(YSQLValueCore) * schema_->num_columns()))),
      is_nulls_(vector<bool>(schema_->num_columns(), true)) {
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    new(&values_[col_idx]) YSQLValueCore(column_type(col_idx));
  }
}

YSQLRow::YSQLRow(const YSQLRow& other)
    : schema_(other.schema_),
      // Allocate just the YSQLValueCore array memory here. Explicit constructor call follows below.
      values_(reinterpret_cast<YSQLValueCore*>(
          ::operator new(sizeof(YSQLValueCore) * schema_->num_columns()))),
      is_nulls_(other.is_nulls_) {
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    new(&values_[col_idx]) YSQLValueCore(
        column_type(col_idx), IsNull(col_idx), other.values_[col_idx]);
  }
}

YSQLRow::YSQLRow(YSQLRow&& other)
    : schema_(other.schema_),
      // Allocate just the YSQLValueCore array memory here. Explicit constructor call follows below.
      values_(reinterpret_cast<YSQLValueCore*>(
          ::operator new(sizeof(YSQLValueCore) * schema_->num_columns()))),
      is_nulls_(other.is_nulls_) {
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    new(&values_[col_idx]) YSQLValueCore(
        column_type(col_idx), IsNull(col_idx), &other.values_[col_idx]);
  }
}

YSQLRow::~YSQLRow() {
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    values_[col_idx].Free(column_type(col_idx));
  }
  ::operator delete(values_); // Just deallocate the YSQLValueCore array memory.
}

YSQLValue YSQLRow::column(const size_t col_idx) const {
  // Return a copy of the column value
  return YSQLValue(column_type(col_idx), IsNull(col_idx), values_[col_idx]);
}

void YSQLRow::set_column(const size_t col_idx, const YSQLValue& v) {
  SetNull(col_idx, v.IsNull());
  if (!v.IsNull()) {
    values_[col_idx].Free(column_type(col_idx));
    new(&values_[col_idx]) YSQLValueCore(column_type(col_idx), v.IsNull(), v);
  }
}

int8_t YSQLRow::int8_value(const size_t col_idx) const {
  return value(col_idx, INT8, values_[col_idx].int8_value_);
}

int16_t YSQLRow::int16_value(const size_t col_idx) const {
  return value(col_idx, INT16, values_[col_idx].int16_value_);
}

int32_t YSQLRow::int32_value(const size_t col_idx) const {
  return value(col_idx, INT32, values_[col_idx].int32_value_);
}

int64_t YSQLRow::int64_value(const size_t col_idx) const {
  return value(col_idx, INT64, values_[col_idx].int64_value_);
}

float YSQLRow::float_value(const size_t col_idx) const {
  return value(col_idx, FLOAT, values_[col_idx].float_value_);
}

double YSQLRow::double_value(const size_t col_idx) const {
  return value(col_idx, DOUBLE, values_[col_idx].double_value_);
}

string YSQLRow::string_value(const size_t col_idx) const {
  return value(col_idx, STRING, values_[col_idx].string_value_);
}

bool YSQLRow::bool_value(const size_t col_idx) const {
  return value(col_idx, BOOL, values_[col_idx].bool_value_);
}

Timestamp YSQLRow::timestamp_value(const size_t col_idx) const {
  return value(col_idx, TIMESTAMP, values_[col_idx].timestamp_value_);
}

void YSQLRow::set_int8_value(const size_t col_idx, const int8_t v) {
  set_value(col_idx, INT8, v, &values_[col_idx].int8_value_);
}

void YSQLRow::set_int16_value(const size_t col_idx, const int16_t v) {
  set_value(col_idx, INT16, v, &values_[col_idx].int16_value_);
}

void YSQLRow::set_int32_value(const size_t col_idx, const int32_t v) {
  set_value(col_idx, INT32, v, &values_[col_idx].int32_value_);
}

void YSQLRow::set_int64_value(const size_t col_idx, const int64_t v) {
  set_value(col_idx, INT64, v, &values_[col_idx].int64_value_);
}

void YSQLRow::set_float_value(const size_t col_idx, const float v) {
  set_value(col_idx, FLOAT, v, &values_[col_idx].float_value_);
}

void YSQLRow::set_double_value(const size_t col_idx, const double v) {
  set_value(col_idx, DOUBLE, v, &values_[col_idx].double_value_);
}

void YSQLRow::set_string_value(const size_t col_idx, const std::string& v) {
  set_value(col_idx, STRING, v, &values_[col_idx].string_value_);
}

void YSQLRow::set_bool_value(const size_t col_idx, const bool v) {
  set_value(col_idx, BOOL, v, &values_[col_idx].bool_value_);
}

void YSQLRow::set_timestamp_value(const size_t col_idx, const Timestamp& v) {
  set_value(col_idx, TIMESTAMP, v, &values_[col_idx].timestamp_value_);
}

void YSQLRow::Serialize(const YSQLClient client, faststring* buffer) const {
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    values_[col_idx].Serialize(column_type(col_idx), IsNull(col_idx), client, buffer);
  }
}

Status YSQLRow::Deserialize(const YSQLClient client, Slice* data) {
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    bool is_null = false;
    RETURN_NOT_OK(values_[col_idx].Deserialize(column_type(col_idx), client, data, &is_null));
    SetNull(col_idx, is_null);
  }
  return Status::OK();
}

string YSQLRow::ToString() const {
  string s = "{ ";
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    if (col_idx > 0) {
      s+= ", ";
    }
    s += values_[col_idx].ToString(column_type(col_idx), IsNull(col_idx));
  }
  s += " }";
  return s;
}

YSQLRow& YSQLRow::operator=(const YSQLRow& other) {
  this->~YSQLRow();
  new(this) YSQLRow(other);
  return *this;
}

YSQLRow& YSQLRow::operator=(YSQLRow&& other) {
  this->~YSQLRow();
  new(this) YSQLRow(other);
  return *this;
}

//-------------------------------------- YSQL row block --------------------------------------
YSQLRowBlock::YSQLRowBlock(const Schema& schema, const vector<ColumnId>& column_ids)
    : schema_(new Schema()) {
  schema.CreateProjectionByIdsIgnoreMissing(column_ids, schema_.get());
}

YSQLRowBlock::~YSQLRowBlock() {
}

YSQLRow& YSQLRowBlock::Extend() {
  rows_.emplace_back(schema_);
  return rows_.back();
}

string YSQLRowBlock::ToString() const {
  string s = "{ ";
  for (size_t i = 0; i < rows_.size(); i++) {
    if (i > 0) { s+= ", "; }
    s += rows_[i].ToString();
  }
  s += " }";
  return s;
}

void YSQLRowBlock::Serialize(const YSQLClient client, faststring* buffer) const {
  CHECK_EQ(client, YSQL_CLIENT_CQL);
  CQLEncodeLength(rows_.size(), buffer);
  for (const auto& row : rows_) {
    row.Serialize(client, buffer);
  }
}

Status YSQLRowBlock::Deserialize(const YSQLClient client, Slice* data) {
  CHECK_EQ(client, YSQL_CLIENT_CQL);
  int32_t count = 0;
  RETURN_NOT_OK(CQLDecodeNum(sizeof(count), NetworkByteOrder::Load32, data, &count));

  for (int32_t i = 0; i < count; ++i) {
    RETURN_NOT_OK(Extend().Deserialize(client, data));
  }
  return Status::OK();
}


} // namespace yb
