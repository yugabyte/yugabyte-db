// Copyright (c) YugaByte, Inc.
//
// This file contains the classes that represent a YQL row and a row block.

#include "yb/common/yql_rowblock.h"

#include "yb/common/wire_protocol.h"

namespace yb {

using std::shared_ptr;

//----------------------------------------- YQL row ----------------------------------------
YQLRow::YQLRow(const shared_ptr<const Schema>& schema)
    : schema_(schema),
      // Allocate just the YQLValueCore array memory here. Explicit constructor call follows below.
      values_(reinterpret_cast<YQLValueCore*>(
          ::operator new(sizeof(YQLValueCore) * schema_->num_columns()))),
      is_nulls_(vector<bool>(schema_->num_columns(), true)) {
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    new(&values_[col_idx]) YQLValueCore(column_type(col_idx));
  }
}

YQLRow::YQLRow(const YQLRow& other)
    : schema_(other.schema_),
      // Allocate just the YQLValueCore array memory here. Explicit constructor call follows below.
      values_(reinterpret_cast<YQLValueCore*>(
          ::operator new(sizeof(YQLValueCore) * schema_->num_columns()))),
      is_nulls_(other.is_nulls_) {
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    new(&values_[col_idx]) YQLValueCore(
        column_type(col_idx), IsNull(col_idx), other.values_[col_idx]);
  }
}

YQLRow::YQLRow(YQLRow&& other)
    : schema_(other.schema_),
      // Allocate just the YQLValueCore array memory here. Explicit constructor call follows below.
      values_(reinterpret_cast<YQLValueCore*>(
          ::operator new(sizeof(YQLValueCore) * schema_->num_columns()))),
      is_nulls_(other.is_nulls_) {
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    new(&values_[col_idx]) YQLValueCore(
        column_type(col_idx), IsNull(col_idx), &other.values_[col_idx]);
  }
}

YQLRow::~YQLRow() {
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    values_[col_idx].Free(column_type(col_idx));
  }
  ::operator delete(values_); // Just deallocate the YQLValueCore array memory.
}

YQLValue YQLRow::column(const size_t col_idx) const {
  // Return a copy of the column value
  return YQLValue(column_type(col_idx), IsNull(col_idx), values_[col_idx]);
}

void YQLRow::set_column(const size_t col_idx, const YQLValue& v) {
  SetNull(col_idx, v.IsNull());
  if (!v.IsNull()) {
    values_[col_idx].Free(column_type(col_idx));
    new(&values_[col_idx]) YQLValueCore(column_type(col_idx), v.IsNull(), v);
  }
}

void YQLRow::set_column(const size_t col_idx, YQLValue&& v) {
  SetNull(col_idx, v.IsNull());
  if (!v.IsNull()) {
    values_[col_idx].Free(column_type(col_idx));
    new(&values_[col_idx]) YQLValueCore(column_type(col_idx), v.IsNull(), &v);
  }
}

int8_t YQLRow::int8_value(const size_t col_idx) const {
  return value(col_idx, INT8, values_[col_idx].int8_value_);
}

int16_t YQLRow::int16_value(const size_t col_idx) const {
  return value(col_idx, INT16, values_[col_idx].int16_value_);
}

int32_t YQLRow::int32_value(const size_t col_idx) const {
  return value(col_idx, INT32, values_[col_idx].int32_value_);
}

int64_t YQLRow::int64_value(const size_t col_idx) const {
  return value(col_idx, INT64, values_[col_idx].int64_value_);
}

float YQLRow::float_value(const size_t col_idx) const {
  return value(col_idx, FLOAT, values_[col_idx].float_value_);
}

double YQLRow::double_value(const size_t col_idx) const {
  return value(col_idx, DOUBLE, values_[col_idx].double_value_);
}

string YQLRow::string_value(const size_t col_idx) const {
  return value(col_idx, STRING, values_[col_idx].string_value_);
}

bool YQLRow::bool_value(const size_t col_idx) const {
  return value(col_idx, BOOL, values_[col_idx].bool_value_);
}

int64_t YQLRow::timestamp_value(const size_t col_idx) const {
  return value(col_idx, TIMESTAMP, values_[col_idx].timestamp_value_);
}

void YQLRow::set_int8_value(const size_t col_idx, const int8_t v) {
  set_value(col_idx, INT8, v, &values_[col_idx].int8_value_);
}

void YQLRow::set_int16_value(const size_t col_idx, const int16_t v) {
  set_value(col_idx, INT16, v, &values_[col_idx].int16_value_);
}

void YQLRow::set_int32_value(const size_t col_idx, const int32_t v) {
  set_value(col_idx, INT32, v, &values_[col_idx].int32_value_);
}

void YQLRow::set_int64_value(const size_t col_idx, const int64_t v) {
  set_value(col_idx, INT64, v, &values_[col_idx].int64_value_);
}

void YQLRow::set_float_value(const size_t col_idx, const float v) {
  set_value(col_idx, FLOAT, v, &values_[col_idx].float_value_);
}

void YQLRow::set_double_value(const size_t col_idx, const double v) {
  set_value(col_idx, DOUBLE, v, &values_[col_idx].double_value_);
}

void YQLRow::set_string_value(const size_t col_idx, const std::string& v) {
  set_value(col_idx, STRING, v, &values_[col_idx].string_value_);
}

void YQLRow::set_bool_value(const size_t col_idx, const bool v) {
  set_value(col_idx, BOOL, v, &values_[col_idx].bool_value_);
}

void YQLRow::set_timestamp_value(const size_t col_idx, const int64_t& v) {
  set_value(col_idx, TIMESTAMP, v, &values_[col_idx].timestamp_value_);
}

void YQLRow::Serialize(const YQLClient client, faststring* buffer) const {
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    values_[col_idx].Serialize(column_type(col_idx), IsNull(col_idx), client, buffer);
  }
}

Status YQLRow::Deserialize(const YQLClient client, Slice* data) {
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    bool is_null = false;
    RETURN_NOT_OK(values_[col_idx].Deserialize(column_type(col_idx), client, data, &is_null));
    SetNull(col_idx, is_null);
  }
  return Status::OK();
}

string YQLRow::ToString() const {
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

YQLRow& YQLRow::operator=(const YQLRow& other) {
  this->~YQLRow();
  new(this) YQLRow(other);
  return *this;
}

YQLRow& YQLRow::operator=(YQLRow&& other) {
  this->~YQLRow();
  new(this) YQLRow(other);
  return *this;
}

//-------------------------------------- YQL row block --------------------------------------
YQLRowBlock::YQLRowBlock(const Schema& schema, const vector<ColumnId>& column_ids)
    : schema_(new Schema()) {
  // TODO: is there a better way to report errors here?
  CHECK_OK(schema.CreateProjectionByIdsIgnoreMissing(column_ids, schema_.get()));
}

YQLRowBlock::YQLRowBlock(const Schema& schema) : schema_(new Schema(schema)) {
}

YQLRowBlock::~YQLRowBlock() {
}

YQLRow& YQLRowBlock::Extend() {
  rows_.emplace_back(schema_);
  return rows_.back();
}

string YQLRowBlock::ToString() const {
  string s = "{ ";
  for (size_t i = 0; i < rows_.size(); i++) {
    if (i > 0) { s+= ", "; }
    s += rows_[i].ToString();
  }
  s += " }";
  return s;
}

void YQLRowBlock::Serialize(const YQLClient client, faststring* buffer) const {
  CHECK_EQ(client, YQL_CLIENT_CQL);
  CQLEncodeLength(rows_.size(), buffer);
  for (const auto& row : rows_) {
    row.Serialize(client, buffer);
  }
}

Status YQLRowBlock::Deserialize(const YQLClient client, Slice* data) {
  CHECK_EQ(client, YQL_CLIENT_CQL);
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
YQLValue EvaluateValue(
    const YQLExpressionPB& expr, const YQLValueMap& row, const Schema& schema) {
  switch (expr.expr_case()) {
    case YQLExpressionPB::ExprCase::kColumnId: {
      const auto column_id = ColumnId(expr.column_id());
      const auto it = row.find(column_id);
      return it != row.end() ?
          it->second : YQLValue(schema.column_by_id(column_id).type_info()->type());
    }
    case YQLExpressionPB::ExprCase::kValue:
      return YQLValue::FromYQLValuePB(expr.value());
    case YQLExpressionPB::ExprCase::kCondition: FALLTHROUGH_INTENDED;
    case YQLExpressionPB::ExprCase::EXPR_NOT_SET:
      break;
    // default: fall through
  }
  LOG(FATAL) << "Internal error: invalid column or value expression: " << expr.expr_case();
}

// Evaluate an IN (...) condition.
Status EvaluateInCondition(
    const google::protobuf::RepeatedPtrField<yb::YQLExpressionPB>& operands,
    const YQLValueMap& row, const Schema& schema, bool* result) {
  CHECK_GE(operands.size(), 1);
  *result = false;
  YQLValue left = EvaluateValue(operands.Get(0), row, schema);
  for (int i = 1; i < operands.size(); ++i) {
    YQLValue right = EvaluateValue(operands.Get(i), row, schema);
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
    const google::protobuf::RepeatedPtrField<yb::YQLExpressionPB>& operands,
    const YQLValueMap& row, const Schema& schema, bool* result) {
  CHECK_EQ(operands.size(), 3);
  YQLValue v = EvaluateValue(operands.Get(0), row, schema);
  YQLValue lower_bound = EvaluateValue(operands.Get(1), row, schema);
  YQLValue upper_bound = EvaluateValue(operands.Get(2), row, schema);
  if (!v.Comparable(lower_bound) || !v.Comparable(upper_bound)) {
    return STATUS(RuntimeError, "values not comparable");
  }
  *result = (lower_bound <= v && v <= upper_bound);
  return Status::OK();
}

} // namespace

// Evaluate a condition for the given row.
Status EvaluateCondition(
    const YQLConditionPB& condition, const YQLValueMap& row, const Schema& schema, bool* result) {
  const auto& operands = condition.operands();
  switch (condition.op()) {
    case YQL_OP_NOT: {
      CHECK_EQ(operands.size(), 1);
      CHECK_EQ(operands.Get(0).expr_case(), YQLExpressionPB::ExprCase::kCondition);
      RETURN_NOT_OK(EvaluateCondition(operands.Get(0).condition(), row, schema, result));
      *result = !*result;
      return Status::OK();
    }
    case YQL_OP_IS_NULL: {
      CHECK_EQ(operands.size(), 1);
      *result = EvaluateValue(operands.Get(0), row, schema).IsNull();
      return Status::OK();
    }
    case YQL_OP_IS_NOT_NULL: {
      CHECK_EQ(operands.size(), 1);
      *result = !EvaluateValue(operands.Get(0), row, schema).IsNull();
      return Status::OK();
    }
    case YQL_OP_IS_TRUE: {
      CHECK_EQ(operands.size(), 1);
      YQLValue v = EvaluateValue(operands.Get(0), row, schema);
      if (v.type() != BOOL) return STATUS(RuntimeError, "not a bool value");
      *result = (!v.IsNull() && v.bool_value());
      return Status::OK();
    }
    case YQL_OP_IS_FALSE: {
      CHECK_EQ(operands.size(), 1);
      YQLValue v = EvaluateValue(operands.Get(0), row, schema);
      if (v.type() != BOOL) return STATUS(RuntimeError, "not a bool value");
      *result = (!v.IsNull() && !v.bool_value());
      return Status::OK();
    }

#define YQL_EVALUATE_RELATIONAL_OP(op, operands, row, schema, result)                      \
      do {                                                                                 \
        CHECK_EQ(operands.size(), 2);                                                      \
        const YQLValue left = EvaluateValue(operands.Get(0), row, schema);                 \
        const YQLValue right = EvaluateValue(operands.Get(1), row, schema);                \
        if (!left.Comparable(right)) return STATUS(RuntimeError, "values not comparable"); \
        *result = (left op right);                                                         \
      } while (0)

    case YQL_OP_EQUAL: {
      YQL_EVALUATE_RELATIONAL_OP(==, operands, row, schema, result);
      return Status::OK();
    }
    case YQL_OP_LESS_THAN: {
      YQL_EVALUATE_RELATIONAL_OP(<, operands, row, schema, result);
      return Status::OK();
    }
    case YQL_OP_LESS_THAN_EQUAL: {
      YQL_EVALUATE_RELATIONAL_OP(<=, operands, row, schema, result);
      return Status::OK();
    }
    case YQL_OP_GREATER_THAN: {
      YQL_EVALUATE_RELATIONAL_OP(>, operands, row, schema, result);
      return Status::OK();
    }
    case YQL_OP_GREATER_THAN_EQUAL: {
      YQL_EVALUATE_RELATIONAL_OP(>=, operands, row, schema, result);
      return Status::OK();
    }
    case YQL_OP_NOT_EQUAL: {
      YQL_EVALUATE_RELATIONAL_OP(!=, operands, row, schema, result);
      return Status::OK();
    }

#undef YQL_EVALUATE_RELATIONAL_OP

// Evaluate a logical AND/OR operation. To see if we can short-circuit, we do
// "(left op true) ^ (left op false)" that applies the "left" result with both
// "true" and "false" and only if the answers are different (i.e. exclusive or ^)
// that we should evaluate the "right" result also.
#define YQL_EVALUATE_LOGICAL_OP(op, operands, row, schema, result)                            \
      do {                                                                                    \
        CHECK_EQ(operands.size(), 2);                                                         \
        CHECK_EQ(operands.Get(0).expr_case(), YQLExpressionPB::ExprCase::kCondition);         \
        CHECK_EQ(operands.Get(1).expr_case(), YQLExpressionPB::ExprCase::kCondition);         \
        bool left = false, right = false;                                                     \
        RETURN_NOT_OK(EvaluateCondition(operands.Get(0).condition(), row, schema, &left));    \
        if ((left op true) ^ (left op false)) {                                               \
          RETURN_NOT_OK(EvaluateCondition(operands.Get(1).condition(), row, schema, &right)); \
        }                                                                                     \
        *result = (left op right);                                                            \
      } while (0)

    case YQL_OP_AND: {
      YQL_EVALUATE_LOGICAL_OP(&&, operands, row, schema, result);
      return Status::OK();
    }
    case YQL_OP_OR: {
      YQL_EVALUATE_LOGICAL_OP(||, operands, row, schema, result);
      return Status::OK();
    }

#undef YQL_EVALUATE_LOGICAL_OP

    case YQL_OP_LIKE:     FALLTHROUGH_INTENDED;
    case YQL_OP_NOT_LIKE:
      return STATUS(RuntimeError, "LIKE operator not supported yet");

    case YQL_OP_IN: {
      return EvaluateInCondition(operands, row, schema, result);
    }
    case YQL_OP_NOT_IN: {
      RETURN_NOT_OK(EvaluateInCondition(operands, row, schema, result));
      *result = !*result;
      return Status::OK();
    }

    case YQL_OP_BETWEEN: {
      return EvaluateBetweenCondition(operands, row, schema, result);
    }
    case YQL_OP_NOT_BETWEEN: {
      RETURN_NOT_OK(EvaluateBetweenCondition(operands, row, schema, result));
      *result = !*result;
      return Status::OK();
    }

    // When a row exists, the primary key columns are always populated in the row (value-map) by
    // DocRowwiseIterator and only when it exists. Therefore, the row exists if and only if
    // the row (value-map) is not empty.
    case YQL_OP_EXISTS: {
      *result = !row.empty();
      return Status::OK();
    }
    case YQL_OP_NOT_EXISTS: {
      *result = row.empty();
      return Status::OK();
    }

    case YQL_OP_NOOP:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: illegal or unknown operator " << condition.op();
}

} // namespace yb
