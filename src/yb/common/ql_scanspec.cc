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
// This file contains QLScanSpec that implements a QL scan specification.

#include "yb/common/ql_scanspec.h"

namespace yb {
namespace common {

using std::unordered_map;
using std::pair;
using std::vector;

//-------------------------------------- QL scan range --------------------------------------
QLScanRange::QLScanRange(const Schema& schema, const QLConditionPB& condition)
    : schema_(schema) {

  // If there is no range column, return.
  if (schema_.num_range_key_columns() == 0) {
    return;
  }

  // Initialize the lower/upper bounds of each range column to null to mean it is unbounded.
  ranges_.reserve(schema_.num_range_key_columns());
  for (size_t i = 0; i < schema.num_key_columns(); i++) {
    if (schema.is_range_column(i)) {
      ranges_.emplace(schema.column_id(i), QLRange());
    }
  }

  // Check if there is a range column referenced in the operands.
  const auto& operands = condition.operands();
  bool has_range_column = false;
  for (const auto& operand : operands) {
    if (operand.expr_case() == QLExpressionPB::ExprCase::kColumnId &&
        schema.is_range_column(ColumnId(operand.column_id()))) {
      has_range_column = true;
      break;
    }
  }

  switch (condition.op()) {

#define QL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN(col_expr, val_expr)                       \
      CHECK_EQ(operands.size(), 2);                                                     \
      QLExpressionPB const* col_expr = nullptr;                                        \
      QLExpressionPB const* val_expr = nullptr;                                        \
      if (operands.Get(0).expr_case() == QLExpressionPB::ExprCase::kColumnId &&        \
          operands.Get(1).expr_case() == QLExpressionPB::ExprCase::kValue) {           \
        col_expr = &operands.Get(0);                                                    \
        val_expr = &operands.Get(1);                                                    \
      } else if (operands.Get(1).expr_case() == QLExpressionPB::ExprCase::kColumnId && \
                 operands.Get(0).expr_case() == QLExpressionPB::ExprCase::kValue) {    \
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
    case QL_OP_EQUAL: {
      if (has_range_column) {
        // - <column> = <value> --> min/max values = <value>
        QL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN(col_expr, val_expr);
        const ColumnId column_id(col_expr->column_id());
        ranges_.at(column_id).min_value = val_expr->value();
        ranges_.at(column_id).max_value = val_expr->value();
      }
      return;
    }
    case QL_OP_LESS_THAN:
    case QL_OP_LESS_THAN_EQUAL: {
      if (has_range_column) {
        QL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN(col_expr, val_expr);
        const ColumnId column_id(col_expr->column_id());
        if (operands.Get(0).expr_case() == QLExpressionPB::ExprCase::kColumnId) {
          // - <column> <= <value> --> max_value = <value>
          ranges_.at(column_id).max_value = val_expr->value();
        } else {
          // - <value> <= <column> --> min_value = <value>
          ranges_.at(column_id).min_value = val_expr->value();
        }
      }
      return;
    }
    case QL_OP_GREATER_THAN:
    case QL_OP_GREATER_THAN_EQUAL: {
      if (has_range_column) {
        QL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN(col_expr, val_expr);
        const ColumnId column_id(col_expr->column_id());
        if (operands.Get(0).expr_case() == QLExpressionPB::ExprCase::kColumnId) {
          // - <column> >= <value> --> min_value = <value>
          ranges_.at(column_id).min_value = val_expr->value();
        } else {
          // - <value> >= <column> --> max_value = <value>
          ranges_.at(column_id).max_value = val_expr->value();
        }
      }
      return;
    }
    case QL_OP_BETWEEN: {
      if (has_range_column) {
        // <column> BETWEEN <value_1> <value_2>:
        // - min_value = <value_1>
        // - max_value = <value_2>
        CHECK_EQ(operands.size(), 3);
        if (operands.Get(0).expr_case() == QLExpressionPB::ExprCase::kColumnId) {
          const ColumnId column_id(operands.Get(0).column_id());
          if (operands.Get(1).expr_case() == QLExpressionPB::ExprCase::kValue) {
            ranges_.at(column_id).min_value = operands.Get(1).value();
          }
          if (operands.Get(2).expr_case() == QLExpressionPB::ExprCase::kValue) {
            ranges_.at(column_id).max_value = operands.Get(2).value();
          }
        }
      }
      return;
    }
    case QL_OP_IN: {
      if (has_range_column) {
        QL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN(col_expr, val_expr);
        // - <column> IN (<value>) --> min/max values = <value>
        // TODO(Mihnea) handle the other cases here too
        if (val_expr->value().list_value().elems_size() == 1) {
          QLValuePB value = val_expr->value().list_value().elems(0);
          const ColumnId column_id(col_expr->column_id());
          ranges_.at(column_id).min_value = value;
          ranges_.at(column_id).max_value = value;
        }
      }
      return;
    }

#undef QL_GET_COLUMN_VALUE_EXPR_ELSE_RETURN

      // For logical conditions, the ranges are union/intersect/complement of the operands' ranges.
    case QL_OP_AND: {
      CHECK_GT(operands.size(), 0);
      for (const auto& operand : operands) {
        CHECK_EQ(operand.expr_case(), QLExpressionPB::ExprCase::kCondition);
        *this &= QLScanRange(schema_, operand.condition());
      }
      return;
    }
    case QL_OP_OR: {
      CHECK_GT(operands.size(), 0);
      for (const auto& operand : operands) {
        CHECK_EQ(operand.expr_case(), QLExpressionPB::ExprCase::kCondition);
        *this |= QLScanRange(schema_, operand.condition());
      }
      return;
    }
    case QL_OP_NOT: {
      CHECK_EQ(operands.size(), 1);
      CHECK_EQ(operands.Get(0).expr_case(), QLExpressionPB::ExprCase::kCondition);
      *this = std::move(~QLScanRange(schema_, operands.Get(0).condition()));
      return;
    }

    case QL_OP_IS_NULL:     FALLTHROUGH_INTENDED;
    case QL_OP_IS_NOT_NULL: FALLTHROUGH_INTENDED;
    case QL_OP_IS_TRUE:     FALLTHROUGH_INTENDED;
    case QL_OP_IS_FALSE:    FALLTHROUGH_INTENDED;
    case QL_OP_NOT_EQUAL:   FALLTHROUGH_INTENDED;
    case QL_OP_LIKE:        FALLTHROUGH_INTENDED;
    case QL_OP_NOT_LIKE:    FALLTHROUGH_INTENDED;
    case QL_OP_NOT_IN:      FALLTHROUGH_INTENDED;
    case QL_OP_NOT_BETWEEN:
      // No simple range can be deduced from these conditions. So the range will be unbounded.
      return;

    case QL_OP_EXISTS:     FALLTHROUGH_INTENDED;
    case QL_OP_NOT_EXISTS: FALLTHROUGH_INTENDED;
    case QL_OP_NOOP:
      break;

      // default: fall through
  }

  LOG(FATAL) << "Internal error: illegal or unknown operator " << condition.op();
}

QLScanRange& QLScanRange::operator&=(const QLScanRange& other) {
  for (auto& elem : ranges_) {
    auto& range = elem.second;
    const auto& other_range = other.ranges_.at(elem.first);

    // Interact operation:
    // - min_value = max(min_value, other_min_value)
    // - max_value = min(max_value, other_max_value)
    if (BothNotNull(range.min_value, other_range.min_value)) {
      range.min_value = std::max(range.min_value, other_range.min_value);
    } else if (!IsNull(other_range.min_value)) {
      range.min_value = other_range.min_value;
    }

    if (BothNotNull(range.max_value, other_range.max_value)) {
      range.max_value = std::min(range.max_value, other_range.max_value);
    } else if (!IsNull(other_range.max_value)) {
      range.max_value = other_range.max_value;
    }
  }
  return *this;
}

QLScanRange& QLScanRange::operator|=(const QLScanRange& other) {
  for (auto& elem : ranges_) {
    auto& range = elem.second;
    const auto& other_range = other.ranges_.at(elem.first);

    // Union operation:
    // - min_value = min(min_value, other_min_value)
    // - max_value = max(max_value, other_max_value)
    if (BothNotNull(range.min_value, other_range.min_value)) {
      range.min_value = std::min(range.min_value, other_range.min_value);
    } else if (IsNull(other_range.min_value)) {
      SetNull(&range.min_value);
    }

    if (BothNotNull(range.max_value, other_range.max_value)) {
      range.max_value = std::max(range.max_value, other_range.max_value);
    } else if (IsNull(other_range.max_value)) {
      SetNull(&range.max_value);
    }
  }
  return *this;
}

QLScanRange& QLScanRange::operator~() {
  for (auto& elem : ranges_) {
    auto& range = elem.second;

    // Complement operation:
    if (BothNotNull(range.min_value, range.max_value)) {
      // If the condition's min and max values are defined, the negation of it will be
      // disjoint ranges at the two ends, which is not representable as a simple range. So
      // we will treat the result as unbounded.
      SetNull(&range.min_value);
      SetNull(&range.max_value);
    } else {
      // Otherwise, for one-sided range or unbounded range, the resulting min/max values are
      // just the reverse of the bounds.
      range.min_value.Swap(&range.max_value);
    }
  }
  return *this;
}

QLScanRange& QLScanRange::operator=(QLScanRange&& other) {
  ranges_ = std::move(other.ranges_);
  return *this;
}

// Return the lower/upper range components for the scan.
vector<QLValuePB> QLScanRange::range_values(const bool lower_bound) const {
  vector<QLValuePB> range_values;
  range_values.reserve(schema_.num_range_key_columns());
  for (size_t i = 0; i < schema_.num_key_columns(); i++) {
    if (!schema_.column(i).is_hash_key()) {
      const auto& range = ranges_.at(schema_.column_id(i));
      bool desc_col = schema_.column(i).sorting_type() == ColumnSchema::kDescending;
      // lower bound for ASC column and upper bound for DESC column -> min value
      // otherwise -> max value
      const auto& value = (lower_bound ^ desc_col) ? range.min_value : range.max_value;
      range_values.emplace_back(value);
    }
  }
  return range_values;
}

//-------------------------------------- QL scan spec ---------------------------------------

QLScanSpec::QLScanSpec(QLExprExecutor::SharedPtr executor) : QLScanSpec(nullptr, true, executor) {
}

QLScanSpec::QLScanSpec(const QLConditionPB* condition,
                       const bool is_forward_scan,
                       QLExprExecutor::SharedPtr executor)
    : condition_(condition), is_forward_scan_(is_forward_scan), executor_(executor) {
  if (executor_ == nullptr) {
    executor_ = std::make_shared<QLExprExecutor>();
  }
}

// Evaluate the WHERE condition for the given row.
CHECKED_STATUS QLScanSpec::Match(const QLTableRow::SharedPtr& table_row, bool* match) const {
  if (condition_ != nullptr) {
    return executor_->EvalCondition(*condition_, table_row, match);
  }
  *match = true;
  return Status::OK();
}

} // namespace common
} // namespace yb
