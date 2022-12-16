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

#include "yb/docdb/doc_pgsql_scanspec.h"

#include <boost/optional/optional_io.hpp>

#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_ql_filefilter.h"
#include "yb/docdb/doc_scanspec_util.h"
#include "yb/docdb/value_type.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {
namespace docdb {

DocPgsqlScanSpec::DocPgsqlScanSpec(const Schema& schema,
                                   const rocksdb::QueryId query_id,
                                   const DocKey& doc_key,
                                   const boost::optional<int32_t> hash_code,
                                   const boost::optional<int32_t> max_hash_code,
                                   const DocKey& start_doc_key,
                                   bool is_forward_scan)
    : PgsqlScanSpec(nullptr),
      schema_(schema),
      query_id_(query_id),
      hashed_components_(nullptr),
      range_components_(nullptr),
      range_options_groups_(schema_.num_range_key_columns()),
      hash_code_(hash_code),
      max_hash_code_(max_hash_code),
      start_doc_key_(start_doc_key.empty() ? KeyBytes() : start_doc_key.Encode()),
      lower_doc_key_(doc_key.Encode()),
      is_forward_scan_(is_forward_scan) {

  // Compute lower and upper doc_key.
  // We add +inf as an extra component to make sure this is greater than all keys in range.
  // For lower bound, this is true already, because dockey + suffix is > dockey
  upper_doc_key_ = lower_doc_key_;

  if (hash_code && !doc_key.has_hash()) {
    DocKey lower_doc_key = DocKey(doc_key);
    lower_doc_key.set_hash(*hash_code);
    if (lower_doc_key.hashed_group().empty()) {
      lower_doc_key.hashed_group().emplace_back(KeyEntryType::kLowest);
    }
    lower_doc_key_ = lower_doc_key.Encode();
  }

  if (max_hash_code) {
    DocKey upper_doc_key = DocKey(doc_key);
    upper_doc_key.set_hash(*max_hash_code);
    if (upper_doc_key.hashed_group().empty()) {
      upper_doc_key.hashed_group().emplace_back(KeyEntryType::kHighest);
    }
    upper_doc_key_ = upper_doc_key.Encode();
  }

  upper_doc_key_.AppendKeyEntryTypeBeforeGroupEnd(KeyEntryType::kHighest);
}

DocPgsqlScanSpec::DocPgsqlScanSpec(
    const Schema& schema,
    const rocksdb::QueryId query_id,
    std::reference_wrapper<const std::vector<KeyEntryValue>> hashed_components,
    std::reference_wrapper<const std::vector<KeyEntryValue>> range_components,
    const PgsqlConditionPB* condition,
    const boost::optional<int32_t> hash_code,
    const boost::optional<int32_t> max_hash_code,
    const PgsqlExpressionPB *where_expr,
    const DocKey& start_doc_key,
    bool is_forward_scan,
    const DocKey& lower_doc_key,
    const DocKey& upper_doc_key)
    : PgsqlScanSpec(where_expr),
      range_bounds_(condition ? new QLScanRange(schema, *condition) : nullptr),
      schema_(schema),
      query_id_(query_id),
      hashed_components_(&hashed_components.get()),
      range_components_(&range_components.get()),
      range_options_groups_(schema_.num_range_key_columns()),
      hash_code_(hash_code),
      max_hash_code_(max_hash_code),
      start_doc_key_(start_doc_key.empty() ? KeyBytes() : start_doc_key.Encode()),
      lower_doc_key_(lower_doc_key.Encode()),
      upper_doc_key_(upper_doc_key.Encode()),
      is_forward_scan_(is_forward_scan) {

  auto lower_bound_key = bound_key(schema, true);
  lower_doc_key_ = lower_bound_key > lower_doc_key_
                    || lower_doc_key.empty()
                    ? lower_bound_key : lower_doc_key_;

  auto upper_bound_key = bound_key(schema, false);
  upper_doc_key_ = upper_bound_key < upper_doc_key_
                    || upper_doc_key.empty()
                    ? upper_bound_key : upper_doc_key_;

  if (where_expr_) {
    // Should never get here until WHERE clause is supported.
    LOG(FATAL) << "DEVELOPERS: Add support for condition (where clause)";
  }

  if (range_bounds_) {
    range_bounds_indexes_ = range_bounds_->GetColIds();
  }

  // If the hash key is fixed and we have range columns with IN condition, try to construct the
  // exact list of range options to scan for.
  if ((!hashed_components_->empty() || schema_.num_hash_key_columns() == 0) &&
      schema_.num_range_key_columns() > 0 &&
      range_bounds_ && range_bounds_->has_in_range_options()) {
    DCHECK(condition);
    range_options_ = std::make_shared<std::vector<OptionList>>(schema_.num_range_key_columns());
    InitRangeOptions(*condition);
  }
}

void DocPgsqlScanSpec::InitRangeOptions(const PgsqlConditionPB& condition) {
  size_t num_hash_cols = schema_.num_hash_key_columns();
  switch (condition.op()) {
    case QLOperator::QL_OP_AND:
      for (const auto& operand : condition.operands()) {
        DCHECK(operand.has_condition());
        InitRangeOptions(operand.condition());
      }
      break;

    case QLOperator::QL_OP_EQUAL:
    case QLOperator::QL_OP_IN: {
      DCHECK_EQ(condition.operands_size(), 2);
      // Skip any condition where LHS is not a column (e.g. subscript columns: 'map[k] = v')
      const auto& lhs = condition.operands(0);
      const auto& rhs = condition.operands(1);
      if (lhs.expr_case() != PgsqlExpressionPB::kColumnId &&
          lhs.expr_case() != PgsqlExpressionPB::kTuple) {
        return;
      }

      // Skip any RHS expressions that are not evaluated yet.
      if (rhs.expr_case() != PgsqlExpressionPB::kValue &&
          rhs.expr_case() != PgsqlExpressionPB::kTuple) {
        return;
      }

      DCHECK(condition.op() == QL_OP_IN ||
             condition.op() == QL_OP_EQUAL); // move this up

      if (lhs.has_column_id()) {

        size_t col_idx = schema_.find_column_by_id(ColumnId(lhs.column_id()));

        // Skip any non-range columns.
        if (!schema_.is_range_column(col_idx)) {
          return;
        }

        SortingType sortingType = schema_.column(col_idx).sorting_type();
        range_options_indexes_.emplace_back(condition.operands(0).column_id());

        range_options_groups_.BeginNewGroup();
        range_options_groups_.AddToLatestGroup(col_idx - num_hash_cols);
        if (condition.op() == QL_OP_EQUAL) {
          auto pv = KeyEntryValue::FromQLValuePBForKey(rhs.value(), sortingType);
          (*range_options_)[col_idx - num_hash_cols].push_back(pv);
        } else {
          DCHECK(rhs.value().has_list_value());
          const auto &options = rhs.value().list_value();
          int opt_size = options.elems_size();
          (*range_options_)[col_idx - num_hash_cols].reserve(opt_size);

          // IN arguments should have been de-duplicated and ordered ascendingly by the executor.
          bool is_reverse_order = is_forward_scan_ ^ (sortingType == SortingType::kAscending ||
              sortingType == SortingType::kAscendingNullsLast);
          for (int i = 0; i < opt_size; i++) {
            int elem_idx = is_reverse_order ? opt_size - i - 1 : i;
            const auto &elem = options.elems(elem_idx);
            auto pv = KeyEntryValue::FromQLValuePBForKey(elem, sortingType);
            (*range_options_)[col_idx - num_hash_cols].push_back(pv);
          }
        }
      } else if (lhs.has_tuple()) {
        // The lhs of this tuple IN condition might have a mix of hash and range columns.
        // The hash columns in the lhs are always expected to appear to the left of all the
        // range columns. We only take care to add the range components of the lhs to
        // range_options_groups_ and range_options_indexes_. We compute start_range_col_idx to
        // denote where in the lhs tuple range columns start appearing.
        size_t total_cols = lhs.tuple().elems_size();
        DCHECK_GT(total_cols, 0);

        int start_range_col_idx = 0;
        std::vector<int> col_idxs;
        col_idxs.reserve(lhs.tuple().elems_size());

        range_options_groups_.BeginNewGroup();

        for (const auto& elem : lhs.tuple().elems()) {
          DCHECK(elem.has_column_id());
          ColumnId col_id = ColumnId(elem.column_id());
          int col_idx = schema_.find_column_by_id(col_id);
          col_idxs.push_back(col_idx);
          if (!schema_.is_range_column(col_idx)) {
            start_range_col_idx++;
            DCHECK_EQ(start_range_col_idx, col_idxs.size());
            continue;
          }
          range_options_groups_.AddToLatestGroup(col_idx - num_hash_cols);
          range_options_indexes_.emplace_back(col_id);
        }

        if (start_range_col_idx >= lhs.tuple().elems_size()) {
          return;
        }

        if (condition.op() == QL_OP_EQUAL) {
          DCHECK(rhs.value().has_list_value());
          const auto& value = rhs.value().list_value();
          DCHECK_EQ(total_cols, value.elems_size());
          for (size_t i = start_range_col_idx; i < total_cols; i++) {
            SortingType sorting_type = schema_.column(col_idxs[i]).sorting_type();
            Option option =
                KeyEntryValue::FromQLValuePBForKey(
                    value.elems(static_cast<int>(i)), sorting_type);
            (*range_options_)[col_idxs[i] - num_hash_cols].push_back(std::move(option));
          }
        } else if (condition.op() == QL_OP_IN) {
          // There should be no range columns before start_range_col_idx in col_idxs
          // and there should be no hash columns after start_range_col_idx
          DCHECK(std::find_if(col_idxs.begin(), col_idxs.begin() + start_range_col_idx,
                              [this] (int idx) { return schema_.is_range_column(idx); })
                 == (col_idxs.begin() + start_range_col_idx));
          DCHECK(std::find_if(col_idxs.begin() + start_range_col_idx, col_idxs.end(),
                              [this] (int idx) { return schema_.is_hash_key_column(idx); })
                 == (col_idxs.end()));

          DCHECK(rhs.value().has_list_value());
          const auto& options = rhs.value().list_value();
          int num_options = options.elems_size();
          // IN arguments should have been de-duplicated and ordered ascendingly by the
          // executor.

          std::vector<bool> reverse;
          reverse.reserve(total_cols);
          for (size_t i = 0; i < total_cols; i++) {
            SortingType sorting_type = schema_.column(col_idxs[i]).sorting_type();
            bool is_reverse_order =
                is_forward_scan_ ^
                    (sorting_type == SortingType::kAscending ||
                        sorting_type == SortingType::kAscendingNullsLast);
            reverse.push_back(is_reverse_order);
          }

          std::vector<QLValuePB> sorted_options = SortTuplesbyOrdering(options, reverse);

          for (int i = 0; i < num_options; i++) {
            const auto& elem = sorted_options[i];
            DCHECK(elem.has_tuple_value());
            const auto& value = elem.tuple_value();

            for (size_t j = start_range_col_idx; j < total_cols; j++) {
              SortingType sorting_type = schema_.column(col_idxs[j]).sorting_type();
              Option option = KeyEntryValue::FromQLValuePBForKey(
                  value.elems(static_cast<int>(j)), sorting_type);
              (*range_options_)[col_idxs[j] - num_hash_cols].push_back(std::move(option));
            }
          }
        }
      }

      break;
    }

    default:
      // We don't support any other operators at this level.
      break;
  }
}

KeyBytes DocPgsqlScanSpec::bound_key(const Schema& schema, const bool lower_bound) const {
  KeyBytes result;
  auto encoder = DocKeyEncoder(&result).Schema(schema);

  bool has_hash_columns = schema_.num_hash_key_columns() > 0;
  bool hash_components_unset = has_hash_columns && hashed_components_->empty();
  if (hash_components_unset) {
    // use lower bound hash code if set in request (for scans using token)
    if (lower_bound && hash_code_) {
      encoder.HashAndRange(*hash_code_,
                           {KeyEntryValue(KeyEntryType::kLowest)},
                           {KeyEntryValue(KeyEntryType::kLowest)});
    }
    // use upper bound hash code if set in request (for scans using token)
    if (!lower_bound) {
      if (max_hash_code_) {
        encoder.HashAndRange(*max_hash_code_,
                             {KeyEntryValue(KeyEntryType::kHighest)},
                             {KeyEntryValue(KeyEntryType::kHighest)});
      } else {
        result.AppendKeyEntryTypeBeforeGroupEnd(KeyEntryType::kHighest);
      }
    }
    return result;
  }

  if (has_hash_columns) {
    uint16_t hash = lower_bound
        ? hash_code_.get_value_or(std::numeric_limits<DocKeyHash>::min())
        : max_hash_code_.get_value_or(std::numeric_limits<DocKeyHash>::max());

    encoder.HashAndRange(hash, *hashed_components_, range_components(lower_bound));
  } else {
    // If no hash columns use default hash code (0).
    encoder.Hash(false, 0, *hashed_components_).Range(range_components(lower_bound));
  }
  return result;
}

std::vector<KeyEntryValue> DocPgsqlScanSpec::range_components(const bool lower_bound,
                                                              std::vector<bool> *inclusivities,
                                                              bool use_strictness) const {
  return GetRangeKeyScanSpec(schema_,
                             range_components_,
                             range_bounds_.get(),
                             inclusivities,
                             lower_bound,
                             false,
                             use_strictness);
}

// Return inclusive lower/upper range doc key considering the start_doc_key.
Result<KeyBytes> DocPgsqlScanSpec::Bound(const bool lower_bound) const {
  if (start_doc_key_.empty()) {
    return lower_bound ? lower_doc_key_ : upper_doc_key_;
  }

  // When paging state is present, start_doc_key_ should have been provided, and the scan starting
  // point should be start_doc_key_ instead of the initial bounds.
  if (start_doc_key_ < lower_doc_key_ || start_doc_key_ > upper_doc_key_) {
    return STATUS_FORMAT(Corruption, "Invalid start_doc_key: $0. Range: $1, $2",
                         start_doc_key_, lower_doc_key_, upper_doc_key_);
  }

  // Paging state + forward scan.
  if (is_forward_scan_) {
    return lower_bound ? start_doc_key_ : upper_doc_key_;
  }

  // Paging state + reverse scan.
  if (lower_bound) {
    return lower_doc_key_;
  }

  // If using start_doc_key_ as upper bound append +inf as extra component to ensure it includes
  // the target start_doc_key itself (dockey + suffix < dockey + kHighest).
  // For lower bound, this is true already, because dockey + suffix is > dockey.
  KeyBytes result = start_doc_key_;
  result.AppendKeyEntryTypeBeforeGroupEnd(KeyEntryType::kHighest);
  return result;
}

std::shared_ptr<rocksdb::ReadFileFilter> DocPgsqlScanSpec::CreateFileFilter() const {
  std::vector<bool> lower_bound_incl;
  auto lower_bound = range_components(true, &lower_bound_incl, false);
  CHECK_EQ(lower_bound.size(), lower_bound_incl.size());

  std::vector<bool> upper_bound_incl;
  auto upper_bound = range_components(false, &upper_bound_incl, false);
  CHECK_EQ(upper_bound.size(), upper_bound_incl.size());
  if (lower_bound.empty() && upper_bound.empty()) {
    return std::shared_ptr<rocksdb::ReadFileFilter>();
  } else {
    return std::make_shared<QLRangeBasedFileFilter>(std::move(lower_bound),
                                                    std::move(lower_bound_incl),
                                                    std::move(upper_bound),
                                                    std::move(upper_bound_incl));
  }
}

Result<KeyBytes> DocPgsqlScanSpec::LowerBound() const {
  return Bound(true /* lower_bound */);
}

Result<KeyBytes> DocPgsqlScanSpec::UpperBound() const {
  return Bound(false /* upper_bound */);
}

const DocKey& DocPgsqlScanSpec::DefaultStartDocKey() {
  static const DocKey result;
  return result;
}

}  // namespace docdb
}  // namespace yb
