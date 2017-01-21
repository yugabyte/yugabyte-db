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

  // Check if there is a range column referenced in the operands.
  const auto& operands = condition.operands();
  bool has_range_column = false;
  for (const auto& operand : operands) {
    if (operand.expr_case() == YSQLExpressionPB::ExprCase::kColumnId &&
        schema.is_range_column(ColumnId(operand.column_id()))) {
        has_range_column = true;
        break;
    }
  }

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
        const ColumnId column_id(col_expr->column_id());
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
        const ColumnId column_id(col_expr->column_id());
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
        const ColumnId column_id(col_expr->column_id());
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
          const ColumnId column_id(operands.Get(0).column_id());
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

    case YSQL_OP_EXISTS:     FALLTHROUGH_INTENDED;
    case YSQL_OP_NOT_EXISTS: FALLTHROUGH_INTENDED;
    case YSQL_OP_NOOP:
      break;
    // default: fall through
  }

  LOG(FATAL) << "Internal error: illegal or unknown operator " << condition.op();
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
YSQLScanSpec::YSQLScanSpec(const DocKey& doc_key)
    : doc_key_(&doc_key), hash_code_(0), hashed_components_(nullptr),
      condition_(nullptr), row_count_limit_(1),
      range_(nullptr) {
}

YSQLScanSpec::YSQLScanSpec(
    const Schema& schema, const uint32_t hash_code,
    const std::vector<PrimitiveValue>& hashed_components, const YSQLConditionPB* condition,
    const size_t row_count_limit)
    : doc_key_(nullptr), hash_code_(hash_code), hashed_components_(&hashed_components),
      condition_(condition), row_count_limit_(row_count_limit),
      range_(condition != nullptr ? new YSQLScanRange(schema, *condition) : nullptr) {
}

DocKey YSQLScanSpec::range_doc_key(const bool lower_bound) const {
  // If a full doc key is specify, that is the exactly doc to scan. Otherwise, compute the
  // lower/upper bound doc keys to scan from the range.
  if (doc_key_ != nullptr) {
    return *doc_key_;
  }
  vector<PrimitiveValue> range_components;
  if (range_.get() != nullptr) {
    const vector<YSQLValue> range_values = range_->range_values(lower_bound);
    range_components.reserve(range_values.size());
    for (const auto& value : range_values) {
      range_components.emplace_back(PrimitiveValue::FromYSQLValue(value));
    }
  }
  CHECK(hashed_components_ != nullptr) << "hashed primary key columns missing";
  return DocKey(hash_code_, *hashed_components_, range_components);
}

// Evaluate the WHERE condition for the given row.
Status YSQLScanSpec::Match(const YSQLValueMap& row, bool* match) const {
  if (condition_ != nullptr) {
    return EvaluateCondition(*condition_, row, match);
  }
  *match = true;
  return Status::OK();
}

} // namespace docdb
} // namespace yb
