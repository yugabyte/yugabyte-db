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

#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/ql_expr.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

namespace yb {

using std::unordered_map;
using std::pair;
using std::vector;

//-------------------------------------- QL scan range --------------------------------------
QLScanRange::QLScanRange(const Schema& schema, const QLConditionPB& condition)
    : schema_(schema) {
  Init(condition);
}

template <class Value>
struct ColumnValue {
  bool lhs_is_column = false;
  ColumnId column_id;
  const Value* value = nullptr;

  explicit operator bool() const {
    return value != nullptr;
  }
};

template <class Col>
auto GetColumnValue(const Col& col) {
  CHECK_EQ(col.size(), 2);
  auto it = col.begin();
  using ResultType = ColumnValue<typename std::remove_reference<decltype(it->value())>::type>;
  if (it->expr_case() == decltype(it->expr_case())::kColumnId) {
    ColumnId column_id(it->column_id());
    ++it;
    if (it->expr_case() == decltype(it->expr_case())::kValue) {
      return ResultType {
        .lhs_is_column = true,
        .column_id = column_id,
        .value = &it->value(),
      };
    }
    return ResultType();
  }
  if (it->expr_case() == decltype(it->expr_case())::kValue) {
    auto* value = &it->value();
    ++it;
    if (it->expr_case() == decltype(it->expr_case())::kColumnId) {
      return ResultType {
        .lhs_is_column = false,
        .column_id = ColumnId(it->column_id()),
        .value = value,
      };
    }
    return ResultType();
  }
  return ResultType();
}

void AssignValue(const QLValuePB& value, boost::optional<QLValuePB>* out) {
  *out = value;
}

void AssignValue(const LWQLValuePB& value, boost::optional<QLValuePB>* out) {
  *out = value.ToGoogleProtobuf();
}

template <class Cond>
void QLScanRange::Init(const Cond& condition) {
  // If there is no range column, return.
  if (schema_.num_range_key_columns() == 0) {
    return;
  }

  // Initialize the lower/upper bounds of each range column to null to mean it is unbounded.
  ranges_.reserve(schema_.num_range_key_columns());
  for (size_t i = 0; i < schema_.num_key_columns(); i++) {
    if (schema_.is_range_column(i)) {
      ranges_.emplace(schema_.column_id(i), QLRange());
    }
  }

  // Check if there is a range column referenced in the operands.
  const auto& operands = condition.operands();
  bool has_range_column = false;
  using ExprCase = decltype(operands.begin()->expr_case());
  for (const auto& operand : operands) {
    if (operand.expr_case() == ExprCase::kColumnId &&
        schema_.is_range_column(ColumnId(operand.column_id()))) {
      has_range_column = true;
      break;
    }
  }

  switch (condition.op()) {
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
        auto column_value = GetColumnValue(operands);
        if (column_value) {
          auto& range = ranges_[column_value.column_id];
          AssignValue(*column_value.value, &range.min_value);
          AssignValue(*column_value.value, &range.max_value);
        }
      }
      return;
    }
    case QL_OP_LESS_THAN:
    case QL_OP_LESS_THAN_EQUAL: {
      if (has_range_column) {
        auto column_value = GetColumnValue(operands);
        if (column_value) {
          if (column_value.lhs_is_column) {
            // - <column> <= <value> --> max_value = <value>
            AssignValue(*column_value.value, &ranges_[column_value.column_id].max_value);
          } else {
            // - <value> <= <column> --> min_value = <value>
            AssignValue(*column_value.value, &ranges_[column_value.column_id].min_value);
          }
        }
      }
      return;
    }
    case QL_OP_GREATER_THAN:
    case QL_OP_GREATER_THAN_EQUAL: {
      if (has_range_column) {
        auto column_value = GetColumnValue(operands);
        if (column_value) {
          if (column_value.lhs_is_column) {
            // - <column> >= <value> --> min_value = <value>
            AssignValue(*column_value.value, &ranges_[column_value.column_id].min_value);
          } else {
            // - <value> >= <column> --> max_value = <value>
            AssignValue(*column_value.value, &ranges_[column_value.column_id].max_value);
          }
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
        auto it = operands.begin();
        if (it->expr_case() == ExprCase::kColumnId) {
          const ColumnId column_id(it->column_id());
          ++it;
          if (it->expr_case() == ExprCase::kValue) {
            AssignValue(it->value(), &ranges_[column_id].min_value);
          }
          ++it;
          if (it->expr_case() == ExprCase::kValue) {
            AssignValue(it->value(), &ranges_[column_id].max_value);
          }
        }
      }
      return;
    }
    case QL_OP_IN: {
      if (has_range_column) {
        auto column_value = GetColumnValue(operands);
        if (column_value) {
          // - <column> IN (<value>) --> min/max values = <value>
          // IN arguments should have already been de-duplicated and ordered by the executor.
          auto in_size = column_value.value->list_value().elems().size();
          if (in_size > 0) {
            auto& range = ranges_[column_value.column_id];
            AssignValue(*column_value.value->list_value().elems().begin(), &range.min_value);
            auto last = column_value.value->list_value().elems().end();
            --last;
            AssignValue(*last, &range.max_value);
          }
          has_in_range_options_ = true;
        }
      }
      return;
    }

      // For logical conditions, the ranges are union/intersect/complement of the operands' ranges.
    case QL_OP_AND: {
      CHECK_GT(operands.size(), 0);
      for (const auto& operand : operands) {
        CHECK_EQ(operand.expr_case(), ExprCase::kCondition);
        *this &= QLScanRange(schema_, operand.condition());
      }
      return;
    }
    case QL_OP_OR: {
      CHECK_GT(operands.size(), 0);
      for (const auto& operand : operands) {
        CHECK_EQ(operand.expr_case(), ExprCase::kCondition);
        *this |= QLScanRange(schema_, operand.condition());
      }
      return;
    }
    case QL_OP_NOT: {
      CHECK_EQ(operands.size(), 1);
      CHECK_EQ(operands.begin()->expr_case(), ExprCase::kCondition);
      *this = std::move(~QLScanRange(schema_, operands.begin()->condition()));
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

QLScanRange::QLScanRange(const Schema& schema, const PgsqlConditionPB& condition)
    : schema_(schema) {
  Init(condition);
}

QLScanRange::QLScanRange(const Schema& schema, const LWPgsqlConditionPB& condition)
    : schema_(schema) {
  Init(condition);
}

QLScanRange& QLScanRange::operator&=(const QLScanRange& other) {
  for (auto& elem : ranges_) {
    auto& range = elem.second;
    const auto& other_range = other.ranges_.at(elem.first);

    // Intersect operation:
    // - min_value = max(min_value, other_min_value)
    // - max_value = min(max_value, other_max_value)
    if (range.min_value && other_range.min_value) {
      range.min_value = std::max(range.min_value, other_range.min_value);
    } else if (other_range.min_value) {
      range.min_value = other_range.min_value;
    }

    if (range.max_value && other_range.max_value) {
      range.max_value = std::min(range.max_value, other_range.max_value);
    } else if (other_range.max_value) {
      range.max_value = other_range.max_value;
    }
  }
  has_in_range_options_ = has_in_range_options_ || other.has_in_range_options_;

  return *this;
}

QLScanRange& QLScanRange::operator|=(const QLScanRange& other) {
  for (auto& elem : ranges_) {
    auto& range = elem.second;
    const auto& other_range = other.ranges_.at(elem.first);

    // Union operation:
    // - min_value = min(min_value, other_min_value)
    // - max_value = max(max_value, other_max_value)
    if (range.min_value && other_range.min_value) {
      range.min_value = std::min(range.min_value, other_range.min_value);
    } else if (!other_range.min_value) {
      range.min_value = boost::none;
    }

    if (range.max_value && other_range.max_value) {
      range.max_value = std::max(range.max_value, other_range.max_value);
    } else if (!other_range.max_value) {
      range.max_value = boost::none;
    }
  }
  has_in_range_options_ = has_in_range_options_ && other.has_in_range_options_;

  return *this;
}

QLScanRange& QLScanRange::operator~() {
  for (auto& elem : ranges_) {
    auto& range = elem.second;

    // Complement operation:
    if (range.min_value && range.max_value) {
      // If the condition's min and max values are defined, the negation of it will be
      // disjoint ranges at the two ends, which is not representable as a simple range. So
      // we will treat the result as unbounded.
      range.min_value = boost::none;
      range.max_value = boost::none;
    } else {
      // Otherwise, for one-sided range or unbounded range, the resulting min/max values are
      // just the reverse of the bounds.

      range.min_value.swap(range.max_value);
    }
  }
  has_in_range_options_ = false;

  return *this;
}

QLScanRange& QLScanRange::operator=(QLScanRange&& other) {
  ranges_ = std::move(other.ranges_);
  return *this;
}

//-------------------------------------- QL scan spec ---------------------------------------

QLScanSpec::QLScanSpec(QLExprExecutorPtr executor)
    : QLScanSpec(nullptr, nullptr, true, std::move(executor)) {
}

QLScanSpec::QLScanSpec(const QLConditionPB* condition,
                       const QLConditionPB* if_condition,
                       const bool is_forward_scan,
                       QLExprExecutorPtr executor)
    : YQLScanSpec(YQL_CLIENT_CQL),
      condition_(condition),
      if_condition_(if_condition),
      is_forward_scan_(is_forward_scan),
      executor_(std::move(executor)) {
  if (executor_ == nullptr) {
    executor_ = std::make_shared<QLExprExecutor>();
  }
}

// Evaluate the WHERE condition for the given row.
CHECKED_STATUS QLScanSpec::Match(const QLTableRow& table_row, bool* match) const {
  bool cond = true;
  bool if_cond = true;
  if (condition_ != nullptr) {
    RETURN_NOT_OK(executor_->EvalCondition(*condition_, table_row, &cond));
  }
  if (if_condition_ != nullptr) {
    RETURN_NOT_OK(executor_->EvalCondition(*if_condition_, table_row, &if_cond));
  }
  *match = cond && if_cond;
  return Status::OK();
}

//-------------------------------------- QL scan spec ---------------------------------------
// Pgsql scan specification.
PgsqlScanSpec::PgsqlScanSpec(const PgsqlExpressionPB *where_expr,
                             QLExprExecutor::SharedPtr executor)
    : YQLScanSpec(YQL_CLIENT_PGSQL),
      where_expr_(where_expr),
      executor_(executor) {
  if (executor_ == nullptr) {
    executor_ = std::make_shared<QLExprExecutor>();
  }
}

PgsqlScanSpec::~PgsqlScanSpec() {
}

} // namespace yb
