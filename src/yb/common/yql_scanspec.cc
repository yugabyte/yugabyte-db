// Copyright (c) YugaByte, Inc.
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

    // For relational conditions, the ranges are as follows. If the column is not a range column,
    // just return since it doesn't impose a bound on a range column.
    //
    // We are not distinguishing between < and <= currently but treat the bound as inclusive lower
    // bound. After all, the bound is just a superset of the scan range and as a best-effort
    // measure. There may be a some ways to optimize and distinguish the two in future, like using
    // exclusive lower bound in DocRowwiseIterator or increment the bound value by "1" to become
    // inclusive bound. Same for > and >=.
    case YQL_OP_EQUAL: {
      if (has_range_column) {
        // - <column> = <value> --> min/max values = <value>
        YQL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN(col_expr, val_expr);
        const ColumnId column_id(col_expr->column_id());
        ranges_.at(column_id).min_value = val_expr->value();
        ranges_.at(column_id).max_value = val_expr->value();
      }
      return;
    }
    case YQL_OP_LESS_THAN:
    case YQL_OP_LESS_THAN_EQUAL: {
      if (has_range_column) {
        YQL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN(col_expr, val_expr);
        const ColumnId column_id(col_expr->column_id());
        if (operands.Get(0).expr_case() == YQLExpressionPB::ExprCase::kColumnId) {
          // - <column> <= <value> --> max_value = <value>
          ranges_.at(column_id).max_value = val_expr->value();
        } else {
          // - <value> <= <column> --> min_value = <value>
          ranges_.at(column_id).min_value = val_expr->value();
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
          // - <column> >= <value> --> min_value = <value>
          ranges_.at(column_id).min_value = val_expr->value();
        } else {
          // - <value> >= <column> --> max_value = <value>
          ranges_.at(column_id).max_value = val_expr->value();
        }
      }
      return;
    }
    case YQL_OP_BETWEEN: {
      if (has_range_column) {
        // <column> BETWEEN <value_1> <value_2>:
        // - min_value = <value_1>
        // - max_value = <value_2>
        CHECK_EQ(operands.size(), 3);
        if (operands.Get(0).expr_case() == YQLExpressionPB::ExprCase::kColumnId) {
          const ColumnId column_id(operands.Get(0).column_id());
          if (operands.Get(1).expr_case() == YQLExpressionPB::ExprCase::kValue) {
            ranges_.at(column_id).min_value = operands.Get(1).value();
          }
          if (operands.Get(2).expr_case() == YQLExpressionPB::ExprCase::kValue) {
            ranges_.at(column_id).max_value = operands.Get(2).value();
          }
        }
      }
      return;
    }
    case YQL_OP_IN: {
      if (has_range_column) {
        YQL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN(col_expr, val_expr);
        // - <column> IN (<value>) --> min/max values = <value>
        // TODO(Mihnea) handle the other cases here too
        if (val_expr->value().list_value().elems_size() == 1) {
          YQLValuePB value = val_expr->value().list_value().elems(0);
          const ColumnId column_id(col_expr->column_id());
          ranges_.at(column_id).min_value = value;
          ranges_.at(column_id).max_value = value;
        }
      }
      return;
    }

#undef YQL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN

    // For logical conditions, the ranges are union/intersect/complement of the operands' ranges.
    case YQL_OP_AND: {
      CHECK_GT(operands.size(), 0);
      for (const auto& operand : operands) {
        CHECK_EQ(operand.expr_case(), YQLExpressionPB::ExprCase::kCondition);
        *this &= YQLScanRange(schema_, operand.condition());
      }
      return;
    }
    case YQL_OP_OR: {
      CHECK_GT(operands.size(), 0);
      for (const auto& operand : operands) {
        CHECK_EQ(operand.expr_case(), YQLExpressionPB::ExprCase::kCondition);
        *this |= YQLScanRange(schema_, operand.condition());
      }
      return;
    }
    case YQL_OP_NOT: {
      CHECK_EQ(operands.size(), 1);
      CHECK_EQ(operands.Get(0).expr_case(), YQLExpressionPB::ExprCase::kCondition);
      *this = std::move(~YQLScanRange(schema_, operands.Get(0).condition()));
      return;
    }

    case YQL_OP_IS_NULL:     FALLTHROUGH_INTENDED;
    case YQL_OP_IS_NOT_NULL: FALLTHROUGH_INTENDED;
    case YQL_OP_IS_TRUE:     FALLTHROUGH_INTENDED;
    case YQL_OP_IS_FALSE:    FALLTHROUGH_INTENDED;
    case YQL_OP_NOT_EQUAL:   FALLTHROUGH_INTENDED;
    case YQL_OP_LIKE:        FALLTHROUGH_INTENDED;
    case YQL_OP_NOT_LIKE:    FALLTHROUGH_INTENDED;
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

YQLScanRange& YQLScanRange::operator&=(const YQLScanRange& other) {
  for (auto& elem : ranges_) {
    auto& range = elem.second;
    const auto& other_range = other.ranges_.at(elem.first);

    // Interact operation:
    // - min_value = max(min_value, other_min_value)
    // - max_value = min(max_value, other_max_value)
    if (YQLValue::BothNotNull(range.min_value, other_range.min_value)) {
      range.min_value = std::max(range.min_value, other_range.min_value);
    } else if (!YQLValue::IsNull(other_range.min_value)) {
      range.min_value = other_range.min_value;
    }

    if (YQLValue::BothNotNull(range.max_value, other_range.max_value)) {
      range.max_value = std::min(range.max_value, other_range.max_value);
    } else if (!YQLValue::IsNull(other_range.max_value)) {
      range.max_value = other_range.max_value;
    }
  }
  return *this;
}

YQLScanRange& YQLScanRange::operator|=(const YQLScanRange& other) {
  for (auto& elem : ranges_) {
    auto& range = elem.second;
    const auto& other_range = other.ranges_.at(elem.first);

    // Union operation:
    // - min_value = min(min_value, other_min_value)
    // - max_value = max(max_value, other_max_value)
    if (YQLValue::BothNotNull(range.min_value, other_range.min_value)) {
      range.min_value = std::min(range.min_value, other_range.min_value);
    } else if (YQLValue::IsNull(other_range.min_value)) {
      YQLValue::SetNull(&range.min_value);
    }

    if (YQLValue::BothNotNull(range.max_value, other_range.max_value)) {
      range.max_value = std::max(range.max_value, other_range.max_value);
    } else if (YQLValue::IsNull(other_range.max_value)) {
      YQLValue::SetNull(&range.max_value);
    }
  }
  return *this;
}

YQLScanRange& YQLScanRange::operator~() {
  for (auto& elem : ranges_) {
    auto& range = elem.second;

    // Complement operation:
    if (YQLValue::BothNotNull(range.min_value, range.max_value)) {
      // If the condition's min and max values are defined, the negation of it will be
      // disjoint ranges at the two ends, which is not representable as a simple range. So
      // we will treat the result as unbounded.
      YQLValue::SetNull(&range.min_value);
      YQLValue::SetNull(&range.max_value);
    } else {
      // Otherwise, for one-sided range or unbounded range, the resulting min/max values are
      // just the reverse of the bounds.
      range.min_value.Swap(&range.max_value);
    }
  }
  return *this;
}

YQLScanRange& YQLScanRange::operator=(YQLScanRange&& other) {
  ranges_ = std::move(other.ranges_);
  return *this;
}

// Return the lower/upper range components for the scan. We can use the range group as the bounds
// in DocRowwiseIterator only when all the range columns have bounded values. So return an empty
// group if any of the range column does not have a bound.
vector<YQLValuePB> YQLScanRange::range_values(const bool lower_bound,
                                              const bool allow_null) const {
  vector<YQLValuePB> range_values;
  range_values.reserve(schema_.num_range_key_columns());
  for (size_t i = 0; i < schema_.num_key_columns(); i++) {
    if (!schema_.column(i).is_hash_key()) {
      const auto& range = ranges_.at(schema_.column_id(i));
      bool desc_col = schema_.column(i).sorting_type() == ColumnSchema::kDescending;
      // lower bound for ASC column and upper bound for DESC column -> min value
      // otherwise -> max value
      const auto& value = lower_bound ^ desc_col ? range.min_value : range.max_value;
      if (!allow_null && YQLValue::IsNull(value)) {
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
Status YQLScanSpec::Match(const YQLTableRow& table_row, bool* match) const {
  if (condition_ != nullptr) {
    return EvaluateCondition(*condition_, table_row, match);
  }
  *match = true;
  return Status::OK();
}

} // namespace common
} // namespace yb
