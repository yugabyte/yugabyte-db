// Copyright (c) YugaByte, Inc.
//
// This file contains YQLScanSpec that implements a YQL scan specification.

#include "yb/common/yql_scanspec.h"

namespace yb {
namespace common {

using std::unordered_map;
using std::pair;
using std::vector;

//-------------------------------------- YQL scan range --------------------------------------
YQLScanRange::YQLScanRange(const Schema& schema, const YQLConditionPB& condition)
    : schema_(schema) {

  // If there is no range column, return.
  if (schema_.num_range_key_columns() == 0) {
    return;
  }

  // Initialize the lower/upper bounds of each range column to null to mean it is unbounded.
  ranges_.reserve(schema_.num_range_key_columns());
  for (size_t i = 0; i < schema.num_key_columns(); i++) {
    if (schema.is_range_column(i)) {
      ranges_.emplace(schema.column_id(i), YQLRange());
    }
  }

  // Check if there is a range column referenced in the operands.
  const auto& operands = condition.operands();
  bool has_range_column = false;
  for (const auto& operand : operands) {
    if (operand.expr_case() == YQLExpressionPB::ExprCase::kColumnId &&
        schema.is_range_column(ColumnId(operand.column_id()))) {
        has_range_column = true;
        break;
    }
  }

  switch (condition.op()) {

#define YQL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN(col_expr, val_expr)                       \
      CHECK_EQ(operands.size(), 2);                                                     \
      YQLExpressionPB const* col_expr = nullptr;                                        \
      YQLExpressionPB const* val_expr = nullptr;                                        \
      if (operands.Get(0).expr_case() == YQLExpressionPB::ExprCase::kColumnId &&        \
          operands.Get(1).expr_case() == YQLExpressionPB::ExprCase::kValue) {           \
        col_expr = &operands.Get(0);                                                    \
        val_expr = &operands.Get(1);                                                    \
      } else if (operands.Get(1).expr_case() == YQLExpressionPB::ExprCase::kColumnId && \
                 operands.Get(0).expr_case() == YQLExpressionPB::ExprCase::kValue) {    \
        col_expr = &operands.Get(1);                                                    \
        val_expr = &operands.Get(0);                                                    \
      } else {                                                                          \
        return;                                                                         \
      }

    // For relational conditions, the bounds are as follows. If the column is not a range column,
    // just return since it doesn't impose a bound on a range column.
    //
    // We are not distinguishing between < and <= currently but treat the bound as inclusive lower
    // bound. After all, the bound is just a superset of the scan range and as a best-effort
    // measure. There may be a some ways to optimize and distinguish the two in future, like using
    // exclusive lower bound in DocRowwiseIterator or increment the bound value by "1" to become
    // inclusive bound. Same for > and >=.
    case YQL_OP_EQUAL: {
      if (has_range_column) {
        // - <column> = <value> --> lower/upper bounds = <value>
        YQL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN(col_expr, val_expr);
        const ColumnId column_id(col_expr->column_id());
        ranges_.at(column_id).lower_bound = val_expr->value();
        ranges_.at(column_id).upper_bound = val_expr->value();
      }
      return;
    }
    case YQL_OP_LESS_THAN:
    case YQL_OP_LESS_THAN_EQUAL: {
      if (has_range_column) {
        YQL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN(col_expr, val_expr);
        const ColumnId column_id(col_expr->column_id());
        if (operands.Get(0).expr_case() == YQLExpressionPB::ExprCase::kColumnId) {
          // - <column> <= <value> --> upper_bound = <value>
          ranges_.at(column_id).upper_bound = val_expr->value();
        } else {
          // - <value> <= <column> --> lower_bound = <value>
          ranges_.at(column_id).lower_bound = val_expr->value();
        }
      }
      return;
    }
    case YQL_OP_GREATER_THAN:
    case YQL_OP_GREATER_THAN_EQUAL: {
      if (has_range_column) {
        YQL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN(col_expr, val_expr);
        const ColumnId column_id(col_expr->column_id());
        if (operands.Get(0).expr_case() == YQLExpressionPB::ExprCase::kColumnId) {
          // - <column> >= <value> --> lower_bound = <value>
          ranges_.at(column_id).lower_bound = val_expr->value();
        } else {
          // - <value> >= <column> --> upper_bound = <value>
          ranges_.at(column_id).upper_bound = val_expr->value();
        }
      }
      return;
    }
    case YQL_OP_BETWEEN: {
      if (has_range_column) {
        // <column> BETWEEN <value_1> <value_2>:
        // - lower_bound = <value_1>
        // - upper_bound = <value_2>
        CHECK_EQ(operands.size(), 3);
        if (operands.Get(0).expr_case() == YQLExpressionPB::ExprCase::kColumnId) {
          const ColumnId column_id(operands.Get(0).column_id());
          if (operands.Get(1).expr_case() == YQLExpressionPB::ExprCase::kValue) {
            ranges_.at(column_id).lower_bound = operands.Get(1).value();
          }
          if (operands.Get(2).expr_case() == YQLExpressionPB::ExprCase::kValue) {
            ranges_.at(column_id).upper_bound = operands.Get(2).value();
          }
        }
      }
      return;
    }

#undef YQL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN

    // For logical conditions, the bounds are union/intersect/complement of the operands' ranges.
    case YQL_OP_AND: {
      CHECK_EQ(operands.size(), 2);
      CHECK_EQ(operands.Get(0).expr_case(), YQLExpressionPB::ExprCase::kCondition);
      CHECK_EQ(operands.Get(1).expr_case(), YQLExpressionPB::ExprCase::kCondition);
      const YQLScanRange left(schema_, operands.Get(0).condition());
      const YQLScanRange right(schema_, operands.Get(1).condition());
      for (auto& elem : ranges_) {
        const auto column_id = elem.first;
        auto& range = elem.second;
        const auto& left_range = left.ranges_.at(column_id);
        const auto& right_range = right.ranges_.at(column_id);

        // <condition> AND <condition>:
        // - lower_bound = max(left_lower_bound, right_lower_bound)
        // - upper_bound = min(left_upper_bound, right_upper_bound)
        // - if only left or right lower/upper bound is defined, it is the resulting bound.
        if (YQLValue::BothNotNull(left_range.lower_bound, right_range.lower_bound)) {
          range.lower_bound = std::max(left_range.lower_bound, right_range.lower_bound);
        } else if (!YQLValue::IsNull(left_range.lower_bound)) {
          range.lower_bound = left_range.lower_bound;
        } else if (!YQLValue::IsNull(right_range.lower_bound)) {
          range.lower_bound = right_range.lower_bound;
        }

        if (YQLValue::BothNotNull(left_range.upper_bound, right_range.upper_bound)) {
          range.upper_bound = std::min(left_range.upper_bound, right_range.upper_bound);
        } else if (!YQLValue::IsNull(left_range.upper_bound)) {
          range.upper_bound = left_range.upper_bound;
        } else if (!YQLValue::IsNull(right_range.upper_bound)) {
          range.upper_bound = right_range.upper_bound;
        }
      }
      return;
    }
    case YQL_OP_OR: {
      CHECK_EQ(operands.size(), 2);
      CHECK_EQ(operands.Get(0).expr_case(), YQLExpressionPB::ExprCase::kCondition);
      CHECK_EQ(operands.Get(1).expr_case(), YQLExpressionPB::ExprCase::kCondition);
      const YQLScanRange left(schema_, operands.Get(0).condition());
      const YQLScanRange right(schema_, operands.Get(1).condition());
      for (auto& elem : ranges_) {
        const auto column_id = elem.first;
        auto& range = elem.second;
        const auto& left_range = left.ranges_.at(column_id);
        const auto& right_range = right.ranges_.at(column_id);

        // <condition> OR <condition>:
        // - lower_bound = min(left_lower_bound, right_lower_bound)
        // - upper_bound = max(left_upper_bound, right_upper_bound)
        // - if either left or right (or both) lower/upper bound is undefined, there is no bound.
        if (YQLValue::BothNotNull(left_range.lower_bound, right_range.lower_bound)) {
          range.lower_bound = std::min(left_range.lower_bound, right_range.lower_bound);
        }

        if (YQLValue::BothNotNull(left_range.upper_bound, right_range.upper_bound)) {
          range.upper_bound = std::max(left_range.upper_bound, right_range.upper_bound);
        }
      }
      return;
    }
    case YQL_OP_NOT: {
      CHECK_EQ(operands.size(), 1);
      CHECK_EQ(operands.Get(0).expr_case(), YQLExpressionPB::ExprCase::kCondition);
      const YQLScanRange other(schema_, operands.Get(0).condition());
      for (auto& elem : ranges_) {
        const auto column_id = elem.first;
        auto& range = elem.second;
        const auto& other_range = other.ranges_.at(column_id);

        // NOT <condition>:
        if (YQLValue::BothNotNull(other_range.lower_bound, other_range.upper_bound)) {
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

    case YQL_OP_IS_NULL:     FALLTHROUGH_INTENDED;
    case YQL_OP_IS_NOT_NULL: FALLTHROUGH_INTENDED;
    case YQL_OP_IS_TRUE:     FALLTHROUGH_INTENDED;
    case YQL_OP_IS_FALSE:    FALLTHROUGH_INTENDED;
    case YQL_OP_NOT_EQUAL:   FALLTHROUGH_INTENDED;
    case YQL_OP_LIKE:        FALLTHROUGH_INTENDED;
    case YQL_OP_NOT_LIKE:    FALLTHROUGH_INTENDED;
    case YQL_OP_IN:          FALLTHROUGH_INTENDED;
    case YQL_OP_NOT_IN:      FALLTHROUGH_INTENDED;
    case YQL_OP_NOT_BETWEEN:
      // No simple range can be deduced from these conditions. So the range will be unbounded.
      return;

    case YQL_OP_EXISTS:     FALLTHROUGH_INTENDED;
    case YQL_OP_NOT_EXISTS: FALLTHROUGH_INTENDED;
    case YQL_OP_NOOP:
      break;
    // default: fall through
  }

  LOG(FATAL) << "Internal error: illegal or unknown operator " << condition.op();
}

// Return the lower/upper range components for the scan. We can use the range group as the bounds
// in DocRowwiseIterator only when all the range columns have bounded values. So return an empty
// group if any of the range column does not have a bound.
vector<YQLValuePB> YQLScanRange::range_values(const bool lower_bound) const {
  vector<YQLValuePB> range_values;
  range_values.reserve(schema_.num_range_key_columns());
  for (size_t i = 0; i < schema_.num_key_columns(); i++) {
    if (!schema_.column(i).is_hash_key()) {
      const auto& range = ranges_.at(schema_.column_id(i));
      const auto& value = lower_bound ? range.lower_bound : range.upper_bound;
      if (YQLValue::IsNull(value)) {
        range_values.clear();
        break;
      }
      range_values.emplace_back(value);
    }
  }
  return range_values;
}

//-------------------------------------- YQL scan spec ---------------------------------------
YQLScanSpec::YQLScanSpec(const YQLConditionPB* condition)
      : condition_(condition) {
}

// Evaluate the WHERE condition for the given row.
Status YQLScanSpec::Match(const YQLValueMap& row, bool* match) const {
  if (condition_ != nullptr) {
    return EvaluateCondition(*condition_, row, match);
  }
  *match = true;
  return Status::OK();
}

} // namespace common
} // namespace yb
