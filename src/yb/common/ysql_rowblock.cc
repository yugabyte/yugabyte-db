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

void YSQLRow::set_column(const size_t col_idx, YSQLValue&& v) {
  SetNull(col_idx, v.IsNull());
  if (!v.IsNull()) {
    values_[col_idx].Free(column_type(col_idx));
    new(&values_[col_idx]) YSQLValueCore(column_type(col_idx), v.IsNull(), &v);
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

int64_t YSQLRow::timestamp_value(const size_t col_idx) const {
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

void YSQLRow::set_timestamp_value(const size_t col_idx, const int64_t& v) {
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
  // TODO: is there a better way to report errors here?
  CHECK_OK(schema.CreateProjectionByIdsIgnoreMissing(column_ids, schema_.get()));
}

YSQLRowBlock::YSQLRowBlock(const Schema& schema) : schema_(new Schema(schema)) {
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
  if (!data->empty()) {
    return STATUS(Corruption, "Extra data at the end of row block");
  }
  return Status::OK();
}

namespace {

// Evaluate and return the value of an expression for the given row. Evaluate only column and
// literal values for now.
YSQLValue EvaluateValue(const YSQLExpressionPB& expr, const YSQLValueMap& row) {
  switch (expr.expr_case()) {
    case YSQLExpressionPB::ExprCase::kColumnId: {
      const auto it = row.find(ColumnId(expr.column_id()));
      CHECK(it != row.end()) << "Column value missing: " << expr.column_id();
      return it->second;
    }
    case YSQLExpressionPB::ExprCase::kValue:
      return YSQLValue::FromYSQLValuePB(expr.value());
    case YSQLExpressionPB::ExprCase::kCondition: FALLTHROUGH_INTENDED;
    case YSQLExpressionPB::ExprCase::EXPR_NOT_SET:
      break;
    // default: fall through
  }
  LOG(FATAL) << "Internal error: invalid column or value expression: " << expr.expr_case();
}

// Evaluate an IN (...) condition.
Status EvaluateInCondition(
    const google::protobuf::RepeatedPtrField<yb::YSQLExpressionPB>& operands,
    const YSQLValueMap& row, bool* result) {
  CHECK_GE(operands.size(), 1);
  *result = false;
  YSQLValue left = EvaluateValue(operands.Get(0), row);
  for (int i = 1; i < operands.size(); ++i) {
    YSQLValue right = EvaluateValue(operands.Get(i), row);
    if (!left.Comparable(right)) return STATUS(RuntimeError, "values not comparable");
    if (left == right) {
      *result = true;
      break;
    }
  }
  return Status::OK();
}

// Evaluate a BETWEEN(a, b) condition.
Status EvaluateBetweenCondition(
    const google::protobuf::RepeatedPtrField<yb::YSQLExpressionPB>& operands,
    const YSQLValueMap& row, bool* result) {
  CHECK_EQ(operands.size(), 3);
  YSQLValue v = EvaluateValue(operands.Get(0), row);
  YSQLValue lower_bound = EvaluateValue(operands.Get(1), row);
  YSQLValue upper_bound = EvaluateValue(operands.Get(2), row);
  if (!v.Comparable(lower_bound) || !v.Comparable(upper_bound)) {
    return STATUS(RuntimeError, "values not comparable");
  }
  *result = (lower_bound <= v && v <= upper_bound);
  return Status::OK();
}

} // namespace

// Evaluate a condition for the given row.
Status EvaluateCondition(const YSQLConditionPB& condition, const YSQLValueMap& row, bool* result) {
  const auto& operands = condition.operands();
  switch (condition.op()) {
    case YSQL_OP_NOT: {
      CHECK_EQ(operands.size(), 1);
      CHECK_EQ(operands.Get(0).expr_case(), YSQLExpressionPB::ExprCase::kCondition);
      RETURN_NOT_OK(EvaluateCondition(operands.Get(0).condition(), row, result));
      *result = !*result;
      return Status::OK();
    }
    case YSQL_OP_IS_NULL: {
      CHECK_EQ(operands.size(), 1);
      *result = EvaluateValue(operands.Get(0), row).IsNull();
      return Status::OK();
    }
    case YSQL_OP_IS_NOT_NULL: {
      CHECK_EQ(operands.size(), 1);
      *result = !EvaluateValue(operands.Get(0), row).IsNull();
      return Status::OK();
    }
    case YSQL_OP_IS_TRUE: {
      CHECK_EQ(operands.size(), 1);
      YSQLValue v = EvaluateValue(operands.Get(0), row);
      if (v.type() != BOOL) return STATUS(RuntimeError, "not a bool value");
      *result = (!v.IsNull() && v.bool_value());
      return Status::OK();
    }
    case YSQL_OP_IS_FALSE: {
      CHECK_EQ(operands.size(), 1);
      YSQLValue v = EvaluateValue(operands.Get(0), row);
      if (v.type() != BOOL) return STATUS(RuntimeError, "not a bool value");
      *result = (!v.IsNull() && !v.bool_value());
      return Status::OK();
    }

#define YSQL_EVALUATE_RELATIONAL_OP(op, operands, row, result)                             \
      do {                                                                                 \
        CHECK_EQ(operands.size(), 2);                                                      \
        const YSQLValue left = EvaluateValue(operands.Get(0), row);                        \
        const YSQLValue right = EvaluateValue(operands.Get(1), row);                       \
        if (!left.Comparable(right)) return STATUS(RuntimeError, "values not comparable"); \
        *result = (left op right);                                                         \
      } while (0)

    case YSQL_OP_EQUAL: {
      YSQL_EVALUATE_RELATIONAL_OP(==, operands, row, result);
      return Status::OK();
    }
    case YSQL_OP_LESS_THAN: {
      YSQL_EVALUATE_RELATIONAL_OP(<, operands, row, result);
      return Status::OK();
    }
    case YSQL_OP_LESS_THAN_EQUAL: {
      YSQL_EVALUATE_RELATIONAL_OP(<=, operands, row, result);
      return Status::OK();
    }
    case YSQL_OP_GREATER_THAN: {
      YSQL_EVALUATE_RELATIONAL_OP(>, operands, row, result);
      return Status::OK();
    }
    case YSQL_OP_GREATER_THAN_EQUAL: {
      YSQL_EVALUATE_RELATIONAL_OP(>=, operands, row, result);
      return Status::OK();
    }
    case YSQL_OP_NOT_EQUAL: {
      YSQL_EVALUATE_RELATIONAL_OP(!=, operands, row, result);
      return Status::OK();
    }

#undef YSQL_EVALUATE_RELATIONAL_OP

#define YSQL_EVALUATE_LOGICAL_OP(op, operands, row, result)                            \
      do {                                                                             \
        CHECK_EQ(operands.size(), 2);                                                  \
        CHECK_EQ(operands.Get(0).expr_case(), YSQLExpressionPB::ExprCase::kCondition); \
        CHECK_EQ(operands.Get(1).expr_case(), YSQLExpressionPB::ExprCase::kCondition); \
        bool left = false, right = false;                                              \
        RETURN_NOT_OK(EvaluateCondition(operands.Get(0).condition(), row, &left));     \
        RETURN_NOT_OK(EvaluateCondition(operands.Get(1).condition(), row, &right));    \
        *result = (left op right);                                                     \
      } while (0)

    case YSQL_OP_AND: {
      YSQL_EVALUATE_LOGICAL_OP(&&, operands, row, result);
      return Status::OK();
    }
    case YSQL_OP_OR: {
      YSQL_EVALUATE_LOGICAL_OP(||, operands, row, result);
      return Status::OK();
    }

#undef YSQL_EVALUATE_LOGICAL_OP

    case YSQL_OP_LIKE:     FALLTHROUGH_INTENDED;
    case YSQL_OP_NOT_LIKE:
      return STATUS(RuntimeError, "LIKE operator not supported yet");

    case YSQL_OP_IN: {
      return EvaluateInCondition(operands, row, result);
    }
    case YSQL_OP_NOT_IN: {
      RETURN_NOT_OK(EvaluateInCondition(operands, row, result));
      *result = !*result;
      return Status::OK();
    }

    case YSQL_OP_BETWEEN: {
      return EvaluateBetweenCondition(operands, row, result);
    }
    case YSQL_OP_NOT_BETWEEN: {
      RETURN_NOT_OK(EvaluateBetweenCondition(operands, row, result));
      *result = !*result;
      return Status::OK();
    }

    // When a row exists, the primary key columns are always populated in the row (value-map) by
    // DocRowwiseIterator and only when it exists. Therefore, the row exists if and only if
    // the row (value-map) is not empty.
    case YSQL_OP_EXISTS: {
      *result = !row.empty();
      return Status::OK();
    }
    case YSQL_OP_NOT_EXISTS: {
      *result = row.empty();
      return Status::OK();
    }

    case YSQL_OP_NOOP:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: illegal or unknown operator " << condition.op();
}

} // namespace yb
