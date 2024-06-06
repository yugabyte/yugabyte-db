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

#include "yb/docdb/key_bytes.h"

namespace yb {

using std::vector;

//-------------------------------------- QL scan range --------------------------------------
QLScanRange::QLScanRange(const Schema& schema, const QLConditionPB& condition) {
  Init(schema, condition);
}

QLScanRange::QLScanRange(const Schema& schema, const PgsqlConditionPB& condition) {
  Init(schema, condition);
}

QLScanRange::QLScanRange(const Schema& schema, const LWPgsqlConditionPB& condition) {
  Init(schema, condition);
}

template <class Value>
struct ColumnValue {
  bool lhs_is_column = false;

  // single column
  ColumnId column_id;

  // grouped columns
  const std::vector<ColumnId> column_ids;

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
          .column_ids = {},
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
          .column_ids = {},
          .value = value,
      };
    }
    return ResultType();
  }
  if (it->expr_case() == decltype(it->expr_case())::kTuple) {
    std::vector<ColumnId> column_ids;
    column_ids.reserve(it->tuple().elems().size());
    for (const auto& elem : it->tuple().elems()) {
      DCHECK(elem.has_column_id());
      column_ids.emplace_back(ColumnId(elem.column_id()));
    }
    ++it;

    if (it->expr_case() == decltype(it->expr_case())::kValue) {
      DCHECK(!column_ids.empty());
      auto result = ResultType {
          .lhs_is_column = true,
          .column_id = kInvalidColumnId,
          .column_ids = column_ids,
          .value = &it->value(),
      };
      return result;
    }
    return ResultType();
  }
  return ResultType();
}

template <class Cond>
void QLScanRange::Init(const Schema& schema, const Cond& condition) {
  // Initialize the lower/upper bounds of each range column to null to mean it is unbounded.
  ranges_.reserve(schema.num_dockey_components());
  if (schema.has_yb_hash_code()) {
    ranges_.emplace(kYbHashCodeColId, QLRange());
  }

  for (size_t i = 0; i < schema.num_key_columns(); i++) {
    ranges_.emplace(schema.column_id(i), QLRange());
  }

  // Check if there are range and hash columns referenced in the operands.
  const auto& operands = condition.operands();
  bool has_range_column = false;
  bool has_hash_column = false;
  using ExprCase = decltype(operands.begin()->expr_case());
  for (const auto& operand : operands) {
    if (operand.expr_case() == ExprCase::kColumnId) {
      auto id = operand.column_id();
      has_range_column |= schema.is_range_column(ColumnId(id));
      has_hash_column |= (id == kYbHashCodeColId || schema.is_hash_key_column(ColumnId(id)));

    } else if (operand.expr_case() == ExprCase::kTuple) {
      for (auto const& elem : operand.tuple().elems()) {
        DCHECK(elem.has_column_id());
        auto id = elem.column_id();

        has_range_column |= schema.is_range_column(ColumnId(id));
        has_hash_column |= (id == kYbHashCodeColId || schema.is_hash_key_column(ColumnId(id)));
      }
    }

    if (has_range_column && has_hash_column) {
      break;
    }
  }

  bool is_inclusive = true;

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
          QLLowerBound lower_bound(*column_value.value, true);
          QLUpperBound upper_bound(*column_value.value, true);
          range.min_bound = lower_bound;
          range.max_bound = upper_bound;
        }
      }
      return;
    }
    case QL_OP_LESS_THAN:
      // We can only process strict inequalities if we're using hybridscan
      is_inclusive = false;
      FALLTHROUGH_INTENDED;
    case QL_OP_LESS_THAN_EQUAL: {
      if (has_range_column) {
        auto column_value = GetColumnValue(operands);
        if (column_value) {
          if (column_value.lhs_is_column) {
            // - <column> <= <value> --> max_bound = <value>
            QLUpperBound bound(*column_value.value, is_inclusive);
            ranges_[column_value.column_id].max_bound = bound;
          } else {
            // - <value> <= <column> --> min_bound = <value>
            QLLowerBound bound(*column_value.value, is_inclusive);
            ranges_[column_value.column_id].min_bound = bound;
          }
        }
      }
      return;
    }
    case QL_OP_GREATER_THAN:
      // We can only process strict inequalities if we're using hybridscan
      is_inclusive = false;
      FALLTHROUGH_INTENDED;
    case QL_OP_GREATER_THAN_EQUAL: {
      if (has_range_column) {
        auto column_value = GetColumnValue(operands);
        if (column_value) {
          if (column_value.lhs_is_column) {
            // - <column> >= <value> --> min_bound = <value>
            QLLowerBound bound(*column_value.value, is_inclusive);
            ranges_.at(column_value.column_id).min_bound = bound;
          } else {
            // - <value> >= <column> --> max_bound = <value>
            QLUpperBound bound(*column_value.value, is_inclusive);
            ranges_.at(column_value.column_id).max_bound = bound;
          }
        }
      }
      return;
    }
    case QL_OP_BETWEEN: {
      if (has_range_column) {
        // <column> BETWEEN <value_1> <value_2>:
        // - min_bound = <value_1>
        // - max_bound = <value_2>
        CHECK(operands.size() == 3
              || operands.size() == 5);
        auto it = operands.begin();
        bool lower_bound_inclusive = true;
        bool upper_bound_inclusive = true;
        if (it->expr_case() == ExprCase::kColumnId) {
          const ColumnId column_id(it->column_id());
          ++it;
          if (it->expr_case() == ExprCase::kValue) {
            QLLowerBound bound(it->value(), lower_bound_inclusive);
            ranges_[column_id].min_bound = bound;
          }
          ++it;
          if (it->expr_case() == ExprCase::kValue) {
            QLUpperBound bound(it->value(), upper_bound_inclusive);
            ranges_[column_id].max_bound = bound;
          }

          // We can only process strict inequalities if we're using hybridscan
          if (operands.size() == 5) {
            ++it;
            if (it->expr_case() == ExprCase::kValue) {
              lower_bound_inclusive = it->value().bool_value();
            }
            ++it;
            if (it->expr_case() == ExprCase::kValue) {
              upper_bound_inclusive = it->value().bool_value();
            }

            if (!lower_bound_inclusive) {
              QLLowerBound bound(ranges_[column_id].min_bound->GetValue(), lower_bound_inclusive);
              ranges_[column_id].min_bound = bound;
            }

            if (!upper_bound_inclusive) {
              QLUpperBound bound(ranges_[column_id].max_bound->GetValue(), upper_bound_inclusive);
              ranges_[column_id].max_bound = bound;
            }
          }
        }
      }
      return;
    }
    case QL_OP_IN: {
      if (has_range_column) {
        auto column_value = GetColumnValue(operands);
        if (column_value) {
          // - <column> IN (<value>) --> min/max bounds = <value>
          // IN arguments should have already been de-duplicated and ordered by the executor.
          auto in_size = column_value.value->list_value().elems().size();
          if (in_size > 0) {
            if (column_value.column_ids.empty()) {
              auto& range = ranges_[column_value.column_id];
              QLLowerBound lower_bound(*column_value.value->list_value().elems().begin(), true);
              range.min_bound = lower_bound;
              auto last = column_value.value->list_value().elems().end();
              --last;
              QLUpperBound upper_bound(*last, true);
              range.max_bound = upper_bound;
            } else {
              std::vector<ColumnId> col_ids = column_value.column_ids;
              const auto& options = column_value.value->list_value().elems();
              size_t num_cols = col_ids.size();
              auto options_itr = options.begin();

              std::vector<decltype(&*options.begin())> lower;
              std::vector<decltype(&*options.begin())> upper;
              // We are just setting default values for the upper and lower
              // bounds on the first iteration to populate the lower and upper
              // vectors.
              bool is_init_iteration = true;

              while(options_itr != options.end()) {
                DCHECK(options_itr->has_tuple_value());
                DCHECK_EQ(num_cols, options_itr->tuple_value().elems().size());
                auto tuple_itr = options_itr->tuple_value().elems().begin();
                auto l_itr = lower.begin();
                auto u_itr = upper.begin();
                while(tuple_itr != options_itr->tuple_value().elems().end()) {
                  if (PREDICT_FALSE(is_init_iteration)) {
                    lower.push_back(&*tuple_itr);
                    upper.push_back(&*tuple_itr);
                    ++tuple_itr;
                    continue;
                  }

                  if (**l_itr > *tuple_itr) {
                    *l_itr = &*tuple_itr;
                  }
                  if (**u_itr < *tuple_itr) {
                    *u_itr = &*tuple_itr;
                  }
                  ++tuple_itr;
                  ++l_itr;
                  ++u_itr;
                }
                is_init_iteration = false;
                ++options_itr;
              }

              auto l_itr = lower.begin();
              auto u_itr = upper.begin();
              for (size_t i = 0; i < col_ids.size(); ++i, ++l_itr, ++u_itr) {
                auto& range = ranges_[col_ids[i]];
                range.min_bound = QLLowerBound(**l_itr, true);
                range.max_bound = QLUpperBound(**u_itr, true);
              }
            }
          }
          has_in_range_options_ = true;
        }
      }

      // Check if there are hash columns as a part of IN options
      if(has_hash_column) {
        auto column_value = GetColumnValue(operands);
        if (column_value.column_ids.size() > 1) {
          for (const auto& col_id : column_value.column_ids) {
            if (col_id.ToUint64() == kYbHashCodeColId ||
                schema.is_hash_key_column(col_id)) {
              has_in_hash_options_ = true;
              break;
            }
          }
        }
      }
      return;
    }

      // For logical conditions, the ranges are union/intersect/complement of the operands' ranges.
    case QL_OP_AND: {
      CHECK_GT(operands.size(), 0);
      for (const auto& operand : operands) {
        CHECK_EQ(operand.expr_case(), ExprCase::kCondition);
        *this &= QLScanRange(schema, operand.condition());
      }
      return;
    }
    case QL_OP_OR: {
      CHECK_GT(operands.size(), 0);
      for (const auto& operand : operands) {
        CHECK_EQ(operand.expr_case(), ExprCase::kCondition);
        *this |= QLScanRange(schema, operand.condition());
      }
      return;
    }
    case QL_OP_NOT: {
      CHECK_EQ(operands.size(), 1);
      CHECK_EQ(operands.begin()->expr_case(), ExprCase::kCondition);
      *this = std::move(~QLScanRange(schema, operands.begin()->condition()));
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

QLScanRange::QLBound::QLBound(const QLValuePB &value, bool is_inclusive, bool is_lower_bound)
    : value_(value),
      is_inclusive_(is_inclusive),
      is_lower_bound_(is_lower_bound) {}

QLScanRange::QLBound::QLBound(const LWQLValuePB &value, bool is_inclusive, bool is_lower_bound)
    : value_(value.ToGoogleProtobuf()),
      is_inclusive_(is_inclusive),
      is_lower_bound_(is_lower_bound) {}

bool QLScanRange::QLBound::operator<(const QLBound &other) const {
  CHECK_EQ(is_lower_bound_, other.is_lower_bound_);
  if (value_ == other.value_) {
    if (is_lower_bound_) {
      return is_inclusive_ && !other.is_inclusive_;
    }
    return !is_inclusive_ && other.is_inclusive_;
  }
  return value_ < other.value_;
}

bool QLScanRange::QLBound::operator>(const QLBound &other) const {
  CHECK_EQ(is_lower_bound_, other.is_lower_bound_);
  if (value_ == other.value_) {
    if (is_lower_bound_) {
      return !is_inclusive_ && other.is_inclusive_;
    }
    return is_inclusive_ && !other.is_inclusive_;
  }
  return value_ > other.value_;
}

bool QLScanRange::QLBound::operator==(const QLBound &other) const {
  CHECK_EQ(is_lower_bound_, other.is_lower_bound_);
  return value_ == other.value_ && is_inclusive_ == other.is_inclusive_;
}

QLScanRange& QLScanRange::operator&=(const QLScanRange& other) {
  for (auto& elem : ranges_) {
    auto& range = elem.second;
    const auto& other_range = other.ranges_.at(elem.first);

    // Intersect operation:
    // - min_bound = max(min_bound, other_min_bound)
    // - max_bound = min(max_bound, other_max_bound)
    if (range.min_bound && other_range.min_bound) {
      range.min_bound = std::max(range.min_bound, other_range.min_bound);
    } else if (other_range.min_bound) {
      range.min_bound = other_range.min_bound;
    }

    if (range.max_bound && other_range.max_bound) {
      range.max_bound = std::min(range.max_bound, other_range.max_bound);
    } else if (other_range.max_bound) {
      range.max_bound = other_range.max_bound;
    }
  }
  has_in_range_options_ = has_in_range_options_ || other.has_in_range_options_;
  has_in_hash_options_ = has_in_hash_options_ || other.has_in_hash_options_;

  return *this;
}

QLScanRange& QLScanRange::operator|=(const QLScanRange& other) {
  for (auto& elem : ranges_) {
    auto& range = elem.second;
    const auto& other_range = other.ranges_.at(elem.first);

    // Union operation:
    // - min_bound = min(min_bound, other_min_bound)
    // - max_bound = max(max_bound, other_max_bound)
    if (range.min_bound && other_range.min_bound) {
      range.min_bound = std::min(range.min_bound, other_range.min_bound);
    } else if (!other_range.min_bound) {
      range.min_bound = boost::none;
    }

    if (range.max_bound && other_range.max_bound) {
      range.max_bound = std::max(range.max_bound, other_range.max_bound);
    } else if (!other_range.max_bound) {
      range.max_bound = boost::none;
    }
  }
  has_in_range_options_ = has_in_range_options_ && other.has_in_range_options_;
  has_in_hash_options_ = has_in_hash_options_ && other.has_in_hash_options_;

  return *this;
}

QLScanRange& QLScanRange::operator~() {
  for (auto& elem : ranges_) {
    auto& range = elem.second;

    // Complement operation:
    if (range.min_bound && range.max_bound) {
      // If the condition's min and max values are defined, the negation of it will be
      // disjoint ranges at the two ends, which is not representable as a simple range. So
      // we will treat the result as unbounded.
      range.min_bound = boost::none;
      range.max_bound = boost::none;
    } else {
      // Otherwise, for one-sided range or unbounded range, the resulting min/max bounds are
      // just the reverse of the bounds.
      // Any inclusiveness flags are flipped in this case
      if (range.min_bound) {
        QLUpperBound bound(range.min_bound->GetValue(),
                           !range.min_bound->IsInclusive());
        range.max_bound = bound;
      }

      if (range.max_bound) {
        QLLowerBound bound(range.max_bound->GetValue(),
                           !range.max_bound->IsInclusive());
        range.min_bound = bound;
      }
    }
  }
  has_in_range_options_ = false;

  return *this;
}

QLScanRange& QLScanRange::operator=(QLScanRange&& other) {
  ranges_ = std::move(other.ranges_);
  return *this;
}

//-------------------------------------- YQL scan spec ---------------------------------------
Result<KeyBytes> YQLScanSpec::LowerBound() const { return Bound(/* lower_bound = */ true); }

Result<KeyBytes> YQLScanSpec::UpperBound() const { return Bound(/* lower_bound = */ false); }

//-------------------------------------- QL scan spec ---------------------------------------

QLScanSpec::QLScanSpec(
    const Schema& schema, bool is_forward_scan, rocksdb::QueryId query_id,
    std::unique_ptr<const QLScanRange> range_bounds, size_t prefix_length,
    QLExprExecutorPtr executor)
    : QLScanSpec(
          schema, is_forward_scan, query_id, std::move(range_bounds), prefix_length, nullptr,
          nullptr, std::move(executor)) {}

QLScanSpec::QLScanSpec(
    const Schema& schema,
    bool is_forward_scan,
    rocksdb::QueryId query_id,
    std::unique_ptr<const QLScanRange>
        range_bounds,
    size_t prefix_length,
    const QLConditionPB* condition,
    const QLConditionPB* if_condition,
    QLExprExecutorPtr executor)
    : YQLScanSpec(
          YQL_CLIENT_CQL, schema, is_forward_scan, query_id, std::move(range_bounds),
          prefix_length),
      condition_(condition),
      if_condition_(if_condition),
      executor_(std::move(executor)) {
  if (executor_ == nullptr) {
    executor_ = std::make_shared<QLExprExecutor>();
  }
}

// Evaluate the WHERE condition for the given row.
Status QLScanSpec::Match(const QLTableRow& table_row, bool* match) const {
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
PgsqlScanSpec::PgsqlScanSpec(
    const Schema& schema,
    bool is_forward_scan,
    rocksdb::QueryId query_id,
    std::unique_ptr<const QLScanRange>
        range_bounds,
    size_t prefix_length,
    const PgsqlExpressionPB* where_expr,
    QLExprExecutor::SharedPtr executor)
    : YQLScanSpec(
          YQL_CLIENT_PGSQL, schema, is_forward_scan, query_id, std::move(range_bounds),
          prefix_length),
      where_expr_(where_expr),
      executor_(executor) {
  if (executor_ == nullptr) {
    executor_ = std::make_shared<QLExprExecutor>();
  }
}

PgsqlScanSpec::~PgsqlScanSpec() {
}

} // namespace yb
