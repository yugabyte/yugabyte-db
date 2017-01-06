// Copyright (c) YugaByte, Inc.
//
// This file contains YSQLScanSpec that implements a YSQL scan specification.

#include "yb/docdb/ysql_scanspec.h"

namespace yb {
namespace docdb {

using std::unordered_map;
using std::pair;

//-------------------------------------- YSQL scan range --------------------------------------
YSQLScanRange::YSQLScanRange(const Schema& schema, const YSQLConditionPB& condition)
    : schema_(schema) {

  // If there is no range column, return.
  if (schema_.num_range_key_columns() == 0) {
    return;
  }

  // Initialize the lower/upper bounds of each range column to null to mean it is unbounded.
  ranges_.reserve(schema_.num_range_key_columns());
  for (size_t i = 0; i < schema.num_key_columns(); i++) {
    const auto& column = schema.column(i);
    if (!column.is_hash_key()) {
      const auto data_type = column.type_info()->type();
      ranges_.emplace(schema.column_id(i), YSQLRange(YSQLValue(data_type), YSQLValue(data_type)));
    }
  }

  const auto& operands = condition.operands();
  const bool has_range_column = ProcessOperands(operands);

  switch (condition.op()) {

#define YSQL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN(col_expr, val_expr)                       \
      CHECK_EQ(operands.size(), 2);                                                      \
      YSQLExpressionPB const* col_expr = nullptr;                                        \
      YSQLExpressionPB const* val_expr = nullptr;                                        \
      if (operands.Get(0).expr_case() == YSQLExpressionPB::ExprCase::kColumnId &&        \
          operands.Get(1).expr_case() == YSQLExpressionPB::ExprCase::kValue) {           \
        col_expr = &operands.Get(0);                                                     \
        val_expr = &operands.Get(1);                                                     \
      } else if (operands.Get(1).expr_case() == YSQLExpressionPB::ExprCase::kColumnId && \
                 operands.Get(0).expr_case() == YSQLExpressionPB::ExprCase::kValue) {    \
        col_expr = &operands.Get(1);                                                     \
        val_expr = &operands.Get(0);                                                     \
      } else {                                                                           \
        return;                                                                          \
      }

    // For relational conditions, the bounds are as follows. If the column is not a range column,
    // just return since it doesn't impose a bound on a range column.
    //
    // We are not distinguishing between < and <= currently but treat the bound as inclusive lower
    // bound. After all, the bound is just a superset of the scan range and as a best-effort
    // measure. There may be a some ways to optimize and distinguish the two in future, like using
    // exclusive lower bound in DocRowwiseIterator or increment the bound value by "1" to become
    // inclusive bound. Same for > and >=.
    case YSQL_OP_EQUAL: {
      if (has_range_column) {
        // - <column> = <value> --> lower/upper bounds = <value>
        YSQL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN(col_expr, val_expr);
        const int32_t column_id = col_expr->column_id();
        YSQLValue value = YSQLValue::FromYSQLValuePB(val_expr->value());
        ranges_.at(column_id).lower_bound = value;
        ranges_.at(column_id).upper_bound = value;
      }
      return;
    }
    case YSQL_OP_LESS_THAN:
    case YSQL_OP_LESS_THAN_EQUAL: {
      if (has_range_column) {
        YSQL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN(col_expr, val_expr);
        const int32_t column_id = col_expr->column_id();
        YSQLValue value = YSQLValue::FromYSQLValuePB(val_expr->value());
        if (operands.Get(0).expr_case() == YSQLExpressionPB::ExprCase::kColumnId) {
          // - <column> <= <value> --> upper_bound = <value>
          ranges_.at(column_id).upper_bound = value;
        } else {
          // - <value> <= <column> --> lower_bound = <value>
          ranges_.at(column_id).lower_bound = value;
        }
      }
      return;
    }
    case YSQL_OP_GREATER_THAN:
    case YSQL_OP_GREATER_THAN_EQUAL: {
      if (has_range_column) {
        YSQL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN(col_expr, val_expr);
        const int32_t column_id = col_expr->column_id();
        YSQLValue value = YSQLValue::FromYSQLValuePB(val_expr->value());
        if (operands.Get(0).expr_case() == YSQLExpressionPB::ExprCase::kColumnId) {
          // - <column> >= <value> --> lower_bound = <value>
          ranges_.at(column_id).lower_bound = value;
        } else {
          // - <value> >= <column> --> upper_bound = <value>
          ranges_.at(column_id).upper_bound = value;
        }
      }
      return;
    }
    case YSQL_OP_BETWEEN: {
      if (has_range_column) {
        // <column> BETWEEN <value_1> <value_2>:
        // - lower_bound = <value_1>
        // - upper_bound = <value_2>
        CHECK_EQ(operands.size(), 3);
        if (operands.Get(0).expr_case() == YSQLExpressionPB::ExprCase::kColumnId) {
          const auto column_id = operands.Get(0).column_id();
          if (operands.Get(1).expr_case() == YSQLExpressionPB::ExprCase::kValue) {
            ranges_.at(column_id).lower_bound = YSQLValue::FromYSQLValuePB(operands.Get(1).value());
          }
          if (operands.Get(2).expr_case() == YSQLExpressionPB::ExprCase::kValue) {
            ranges_.at(column_id).upper_bound = YSQLValue::FromYSQLValuePB(operands.Get(2).value());
          }
        }
      }
      return;
    }

#undef YSQL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN

    // For logical conditions, the bounds are union/intersect/complement of the operands' ranges.
    case YSQL_OP_AND: {
      CHECK_EQ(operands.size(), 2);
      CHECK_EQ(operands.Get(0).expr_case(), YSQLExpressionPB::ExprCase::kCondition);
      CHECK_EQ(operands.Get(1).expr_case(), YSQLExpressionPB::ExprCase::kCondition);
      const YSQLScanRange left(schema_, operands.Get(0).condition());
      const YSQLScanRange right(schema_, operands.Get(1).condition());
      non_key_columns_.insert(left.non_key_columns_.begin(), left.non_key_columns_.end());
      non_key_columns_.insert(right.non_key_columns_.begin(), right.non_key_columns_.end());
      for (auto& elem : ranges_) {
        const auto column_id = elem.first;
        auto& range = elem.second;
        const auto& left_range = left.ranges_.at(column_id);
        const auto& right_range = right.ranges_.at(column_id);

        // <condition> AND <condition>:
        // - lower_bound = max(left_lower_bound, right_lower_bound)
        // - upper_bound = min(left_upper_bound, right_upper_bound)
        // - if only left or right lower/upper bound is defined, it is the resulting bound.
        if (!left_range.lower_bound.IsNull() && !right_range.lower_bound.IsNull()) {
          range.lower_bound = std::max(left_range.lower_bound, right_range.lower_bound);
        } else if (!left_range.lower_bound.IsNull()) {
          range.lower_bound = left_range.lower_bound;
        } else if (!right_range.lower_bound.IsNull()) {
          range.lower_bound = right_range.lower_bound;
        }

        if (!left_range.upper_bound.IsNull() && !right_range.upper_bound.IsNull()) {
          range.upper_bound = std::min(left_range.upper_bound, right_range.upper_bound);
        } else if (!left_range.upper_bound.IsNull()) {
          range.upper_bound = left_range.upper_bound;
        } else if (!right_range.upper_bound.IsNull()) {
          range.upper_bound = right_range.upper_bound;
        }
      }
      return;
    }
    case YSQL_OP_OR: {
      CHECK_EQ(operands.size(), 2);
      CHECK_EQ(operands.Get(0).expr_case(), YSQLExpressionPB::ExprCase::kCondition);
      CHECK_EQ(operands.Get(1).expr_case(), YSQLExpressionPB::ExprCase::kCondition);
      const YSQLScanRange left(schema_, operands.Get(0).condition());
      const YSQLScanRange right(schema_, operands.Get(1).condition());
      non_key_columns_.insert(left.non_key_columns_.begin(), left.non_key_columns_.end());
      non_key_columns_.insert(right.non_key_columns_.begin(), right.non_key_columns_.end());
      for (auto& elem : ranges_) {
        const auto column_id = elem.first;
        auto& range = elem.second;
        const auto& left_range = left.ranges_.at(column_id);
        const auto& right_range = right.ranges_.at(column_id);

        // <condition> OR <condition>:
        // - lower_bound = min(left_lower_bound, right_lower_bound)
        // - upper_bound = max(left_upper_bound, right_upper_bound)
        // - if either left or right (or both) lower/upper bound is undefined, there is no bound.
        if (!left_range.lower_bound.IsNull() && !right_range.lower_bound.IsNull()) {
          range.lower_bound = std::min(left_range.lower_bound, right_range.lower_bound);
        }

        if (!left_range.upper_bound.IsNull() && !right_range.upper_bound.IsNull()) {
          range.upper_bound = std::max(left_range.upper_bound, right_range.upper_bound);
        }
      }
      return;
    }
    case YSQL_OP_NOT: {
      CHECK_EQ(operands.size(), 1);
      CHECK_EQ(operands.Get(0).expr_case(), YSQLExpressionPB::ExprCase::kCondition);
      const YSQLScanRange other(schema_, operands.Get(0).condition());
      non_key_columns_.insert(other.non_key_columns_.begin(), other.non_key_columns_.end());
      for (auto& elem : ranges_) {
        const auto column_id = elem.first;
        auto& range = elem.second;
        const auto& other_range = other.ranges_.at(column_id);

        // NOT <condition>:
        if (!other_range.lower_bound.IsNull() && !other_range.upper_bound.IsNull()) {
          // If the condition's lower and upper bounds are defined, the negation of it will be
          // disjoint ranges at the two ends, which is not representable as a simple range. So
          // we will treat the result as unbounded.
          return;
        }

        // Otherwise, for one-sided range or unbounded range, the resulting lower/upper bounds are
        // just the reverse of the bounds.
        range.lower_bound = other_range.upper_bound;
        range.upper_bound = other_range.lower_bound;
      }
      return;
    }

    case YSQL_OP_IS_NULL:     FALLTHROUGH_INTENDED;
    case YSQL_OP_IS_NOT_NULL: FALLTHROUGH_INTENDED;
    case YSQL_OP_IS_TRUE:     FALLTHROUGH_INTENDED;
    case YSQL_OP_IS_FALSE:    FALLTHROUGH_INTENDED;
    case YSQL_OP_NOT_EQUAL:   FALLTHROUGH_INTENDED;
    case YSQL_OP_LIKE:        FALLTHROUGH_INTENDED;
    case YSQL_OP_NOT_LIKE:    FALLTHROUGH_INTENDED;
    case YSQL_OP_IN:          FALLTHROUGH_INTENDED;
    case YSQL_OP_NOT_IN:      FALLTHROUGH_INTENDED;
    case YSQL_OP_NOT_BETWEEN:
      // No simple range can be deduced from these conditions. So the range will be unbounded.
      return;

    // default: fall through
  }

  LOG(FATAL) << "Unknown op " << condition.op();
}

// Process operands. Save non-key columns and return true if there is a range column in the
// operands. Note that this method needs to take care of the non-key column ids in the current
// condition node only. The recursion is done in the constructor that recurses into the
// conditions under AND, OR, NOT and merges the non-key column IDs from the constituent conditions.
bool YSQLScanRange::ProcessOperands(
    const google::protobuf::RepeatedPtrField<yb::YSQLExpressionPB>& operands) {
  bool has_range_column = false;
  for (const auto& operand : operands) {
    if (operand.expr_case() == YSQLExpressionPB::ExprCase::kColumnId) {
      const auto id = ColumnId(operand.column_id());
      const size_t idx = schema_.find_column_by_id(id);
      if (!schema_.is_key_column(idx)) {
        non_key_columns_.insert(id);
      } else if (!schema_.column(idx).is_hash_key()) {
        has_range_column = true;
      }
    }
  }
  return has_range_column;
}

// Return the lower/upper range components for the scan. We can use the range group as the bounds
// in DocRowwiseIterator only when all the range columns have bounded values. So return an empty
// group if any of the range column does not have a bound.
vector<YSQLValue> YSQLScanRange::range_values(const bool lower_bound) const {
  vector<YSQLValue> range_values;
  range_values.reserve(schema_.num_range_key_columns());
  for (size_t i = 0; i < schema_.num_key_columns(); i++) {
    if (!schema_.column(i).is_hash_key()) {
      const auto& range = ranges_.at(schema_.column_id(i));
      const auto& value = lower_bound ? range.lower_bound : range.upper_bound;
      if (value.IsNull()) {
        range_values.clear();
        break;
      }
      range_values.emplace_back(value);
    }
  }
  return range_values;
}

//-------------------------------------- YSQL scan spec ---------------------------------------
YSQLScanSpec::YSQLScanSpec(
    const Schema& schema, const uint32_t hash_code,
    const std::vector<PrimitiveValue>& hashed_components, const YSQLConditionPB* condition,
    const size_t row_count_limit)
    : hash_code_(hash_code), hashed_components_(hashed_components),
      condition_(condition), row_count_limit_(row_count_limit),
      range_(condition != nullptr ? new YSQLScanRange(schema, *condition) : nullptr) {
}

namespace {

// Evaluate and return the value of an expression for the given row. Evaluate only column and
// literal values for now.
YSQLValue EvaluateValue(
    const YSQLExpressionPB& expr, const unordered_map<int32_t, YSQLValue>& row) {
  switch (expr.expr_case()) {
    case YSQLExpressionPB::ExprCase::kColumnId: {
      const auto it = row.find(expr.column_id());
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

Status EvaluateInCondition(
    const google::protobuf::RepeatedPtrField<yb::YSQLExpressionPB>& operands,
    const unordered_map<int32_t, YSQLValue>& row, bool* result) {
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

Status EvaluateBetweenCondition(
    const google::protobuf::RepeatedPtrField<yb::YSQLExpressionPB>& operands,
    const unordered_map<int32_t, YSQLValue>& row, bool* result) {
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

// Evaluate a condition for the given row.
Status EvaluateCondition(
    const YSQLConditionPB& condition, const unordered_map<int32_t, YSQLValue>& row, bool* result) {
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

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported operator " << condition.op();
}

} // namespace

DocKey YSQLScanSpec::range_doc_key(const bool lower_bound) const {
  vector<PrimitiveValue> range_components;
  if (range_.get() != nullptr) {
    const vector<YSQLValue> range_values = range_->range_values(lower_bound);
    range_components.reserve(range_values.size());
    for (const auto& value : range_values) {
      range_components.emplace_back(PrimitiveValue::FromYSQLValue(value));
    }
  }
  return DocKey(hash_code_, hashed_components_, range_components);
}

// Evaluate the WHERE condition for the given row.
Status YSQLScanSpec::Match(const unordered_map<int32_t, YSQLValue>& row, bool* match) const {
  if (condition_ != nullptr) {
    return EvaluateCondition(*condition_, row, match);
  }
  *match = true;
  return Status::OK();
}

} // namespace docdb
} // namespace yb
