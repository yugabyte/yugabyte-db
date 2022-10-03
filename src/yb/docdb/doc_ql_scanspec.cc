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

#include "yb/docdb/doc_ql_scanspec.h"

#include "yb/common/common.pb.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/docdb/doc_expr.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_ql_filefilter.h"
#include "yb/docdb/doc_scanspec_util.h"
#include "yb/docdb/value_type.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

DECLARE_bool(disable_hybrid_scan);

using std::vector;

namespace yb {
namespace docdb {

DocQLScanSpec::DocQLScanSpec(const Schema& schema,
                             const DocKey& doc_key,
                             const rocksdb::QueryId query_id,
                             const bool is_forward_scan)
    : QLScanSpec(nullptr, nullptr, is_forward_scan, std::make_shared<DocExprExecutor>()),
      range_bounds_(nullptr),
      schema_(schema),
      hashed_components_(nullptr),
      include_static_columns_(false),
      doc_key_(doc_key.Encode()),
      query_id_(query_id) {
}

DocQLScanSpec::DocQLScanSpec(
    const Schema& schema,
    const boost::optional<int32_t> hash_code,
    const boost::optional<int32_t> max_hash_code,
    std::reference_wrapper<const std::vector<KeyEntryValue>> hashed_components,
    const QLConditionPB* condition,
    const QLConditionPB* if_condition,
    const rocksdb::QueryId query_id,
    const bool is_forward_scan,
    const bool include_static_columns,
    const DocKey& start_doc_key)
    : QLScanSpec(condition, if_condition, is_forward_scan, std::make_shared<DocExprExecutor>()),
      range_bounds_(condition ? new QLScanRange(schema, *condition) : nullptr),
      schema_(schema),
      hash_code_(hash_code),
      max_hash_code_(max_hash_code),
      hashed_components_(&hashed_components.get()),
      include_static_columns_(include_static_columns),
      start_doc_key_(start_doc_key.empty() ? KeyBytes() : start_doc_key.Encode()),
      lower_doc_key_(bound_key(true)),
      upper_doc_key_(bound_key(false)),
      query_id_(query_id) {

    if (range_bounds_) {
        range_bounds_indexes_ = range_bounds_->GetColIds();
    }

    // If the hash key is fixed and we have range columns with IN condition, try to construct the
    // exact list of range options to scan for.
    if (!hashed_components_->empty() && schema_.num_range_key_columns() > 0 && range_bounds_ &&
        range_bounds_->has_in_range_options()) {
      DCHECK(condition);
      range_options_ = std::make_shared<std::vector<OptionList>>(schema_.num_range_key_columns());
      range_options_num_cols_ = std::vector<size_t>(schema_.num_range_key_columns(), 0);
      InitRangeOptions(*condition);

      if (FLAGS_disable_hybrid_scan) {
        // Range options are only valid if all range columns
        // are set (i.e. have one or more options).
        for (size_t i = 0; i < schema_.num_range_key_columns(); i++) {
          if ((*range_options_)[i].empty()) {
            range_options_ = nullptr;
            break;
          }
          i = i + range_options_num_cols_[i] - 1;
        }
      }
    }
}

bool AreColumnsContinous(const std::vector<int>& col_idxs) {
  std::vector<int> copy = col_idxs;
  std::sort(copy.begin(), copy.end());
  int prev_idx = -1;
  for (auto const idx : copy) {
    if (prev_idx != -1 && idx != prev_idx + 1) {
      return false;
    }
    prev_idx = idx;
  }
  return true;
}

void DocQLScanSpec::InitRangeOptions(const QLConditionPB& condition) {
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
      if (lhs.expr_case() != QLExpressionPB::kColumnId &&
          lhs.expr_case() != QLExpressionPB::kTuple) {
        return;
      }

      // Skip any RHS expressions that are not evaluated yet.
      if (rhs.expr_case() != QLExpressionPB::kValue) {
        return;
      }

      if (lhs.has_column_id()) {
        ColumnId col_id = ColumnId(lhs.column_id());
        int col_idx = schema_.find_column_by_id(col_id);

        // Skip any non-range columns.
        if (!schema_.is_range_column(col_idx)) {
          return;
        }

        range_options_num_cols_[col_idx - num_hash_cols] = 1;
        SortingType sorting_type = schema_.column(col_idx).sorting_type();
        // TODO: confusing - name says indexes but stores ids
        range_options_indexes_.emplace_back(col_id);

        if (condition.op() == QL_OP_EQUAL) {
          auto pv = KeyEntryValue::FromQLValuePBForKey(rhs.value(), sorting_type);
          (*range_options_)[col_idx - num_hash_cols].push_back({pv});
        } else {  // QL_OP_IN
          DCHECK_EQ(condition.op(), QL_OP_IN);
          DCHECK(rhs.value().has_list_value());
          const auto& options = rhs.value().list_value();
          int opt_size = options.elems_size();
          (*range_options_)[col_idx - num_hash_cols].reserve(opt_size);

          // IN arguments should have been de-duplicated and ordered ascendingly by the executor.
          bool is_reverse_order = is_forward_scan_ ^ (sorting_type == SortingType::kAscending);
          for (int i = 0; i < opt_size; i++) {
            int elem_idx = is_reverse_order ? opt_size - i - 1 : i;
            const auto& elem = options.elems(elem_idx);
            auto pv = KeyEntryValue::FromQLValuePBForKey(elem, sorting_type);
            (*range_options_)[col_idx - num_hash_cols].push_back({pv});
          }
        }
      } else if (lhs.has_tuple()) {
        std::vector<ColumnId> col_ids;
        std::vector<int> col_idxs;
        size_t num_cols = lhs.tuple().elems_size();
        DCHECK_GT(num_cols, 0);

        for (const auto& elem : lhs.tuple().elems()) {
          DCHECK(elem.has_column_id());
          ColumnId col_id = ColumnId(elem.column_id());
          int col_idx = schema_.find_column_by_id(col_id);
          DCHECK(schema_.is_range_column(col_idx));
          col_ids.push_back(col_id);
          col_idxs.push_back(col_idx);
        }

        DCHECK(AreColumnsContinous(col_idxs));

        for (size_t i = 0; i < num_cols; i++) {
          range_options_indexes_.emplace_back(col_ids[i]);
          range_options_num_cols_[col_idxs[i] - num_hash_cols] = num_cols;
        }

        auto start_idx = *std::min_element(col_idxs.begin(), col_idxs.end());

        if (condition.op() == QL_OP_EQUAL) {
          DCHECK(rhs.value().has_list_value());
          const auto& value = rhs.value().list_value();
          DCHECK_EQ(num_cols, value.elems_size());
          Option option(num_cols);
          for (size_t i = 0; i < num_cols; i++) {
            SortingType sorting_type = schema_.column(col_idxs[i]).sorting_type();
            auto pv =
                KeyEntryValue::FromQLValuePBForKey(value.elems(static_cast<int>(i)), sorting_type);
            option.push_back(pv);
          }
          (*range_options_)[start_idx - num_hash_cols].push_back(std::move(option));
        } else if (condition.op() == QL_OP_IN) {
          DCHECK(rhs.value().has_list_value());
          const auto& options = rhs.value().list_value();
          int num_options = options.elems_size();
          // IN arguments should have been de-duplicated and ordered ascendingly by the
          // executor.

          std::vector<bool> reverse;
          for (size_t i = 0; i < num_cols; i++) {
            SortingType sorting_type = schema_.column(col_idxs[i]).sorting_type();
            bool is_reverse_order = is_forward_scan_ ^ (sorting_type == SortingType::kAscending);
            reverse.push_back(is_reverse_order);
          }

          vector<QLValuePB> sorted_options = SortTuplesbyOrdering(options, reverse);

          for (int i = 0; i < num_options; i++) {
            const auto& elem = sorted_options[i];
            DCHECK(elem.has_tuple_value());
            const auto& value = elem.tuple_value();
            DCHECK_EQ(num_cols, value.elems_size());

            Option option;
            for (size_t j = 0; j < num_cols; j++) {
              SortingType sorting_type = schema_.column(col_idxs[j]).sorting_type();
              auto pv = KeyEntryValue::FromQLValuePBForKey(
                  value.elems(static_cast<int>(j)), sorting_type);
              option.push_back(pv);
            }
            (*range_options_)[start_idx - num_hash_cols].push_back(std::move(option));
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

KeyBytes DocQLScanSpec::bound_key(const bool lower_bound) const {
  KeyBytes result;
  auto encoder = DocKeyEncoder(&result).CotableId(Uuid::Nil());

  // If no hashed_component use hash lower/upper bounds if set.
  if (hashed_components_->empty()) {
    // use lower bound hash code if set in request (for scans using token)
    if (lower_bound && hash_code_) {
      encoder.HashAndRange(*hash_code_, {KeyEntryValue(KeyEntryType::kLowest)}, {});
    }
    // use upper bound hash code if set in request (for scans using token)
    if (!lower_bound && max_hash_code_) {
      encoder.HashAndRange(*max_hash_code_, {KeyEntryValue(KeyEntryType::kHighest)}, {});
    }
    return result;
  }

  // If hash_components are non-empty then hash_code and max_hash_code must both be set and equal.
  DCHECK(hash_code_);
  DCHECK(max_hash_code_);
  DCHECK_EQ(*hash_code_, *max_hash_code_);
  auto hash_code = static_cast<DocKeyHash>(*hash_code_);
  encoder.HashAndRange(hash_code, *hashed_components_, range_components(lower_bound));
  return result;
}

std::vector<KeyEntryValue> DocQLScanSpec::range_components(const bool lower_bound,
                                                           std::vector<bool> *inclusivities,
                                                           bool use_strictness) const {
  return GetRangeKeyScanSpec(schema_,
                             nullptr /* prefixed_range_components */,
                             range_bounds_.get(),
                             inclusivities,
                             lower_bound,
                             include_static_columns_,
                             use_strictness);
}
namespace {

template <class Predicate>
bool KeySatisfiesBound(const KeyBytes& key, const KeyBytes& bound_key, const Predicate& predicate) {
  if (bound_key.empty()) {
    return true;
  }
  return predicate(bound_key, key);
}

bool KeyWithinRange(const KeyBytes& key, const KeyBytes& lower_key, const KeyBytes& upper_key) {
  // Verify that the key is within the lower/upper bound, which is either:
  // 1. the bound is empty,
  // 2. the key is <= or >= the fully-specified bound.
  return KeySatisfiesBound(key, lower_key, std::less_equal<>()) &&
         KeySatisfiesBound(key, upper_key, std::greater_equal<>());
}

} // namespace

Result<KeyBytes> DocQLScanSpec::Bound(const bool lower_bound) const {
  // If a full doc key is specified, that is the exactly doc to scan. Otherwise, compute the
  // lower/upper bound doc keys to scan from the range.
  if (!doc_key_.empty()) {
    if (lower_bound) {
      return doc_key_;
    }
    KeyBytes result = doc_key_;
    // We add +inf as an extra component to make sure this is greater than all keys in range.
    // For lower bound, this is true already, because dockey + suffix is > dockey
    result.AppendKeyEntryTypeBeforeGroupEnd(KeyEntryType::kHighest);
    return std::move(result);
  }

  // Otherwise, if we do not have a paging state (start_doc_key) just use the lower/upper bounds.
  if (start_doc_key_.empty()) {
    if (lower_bound) {
      // For lower-bound key, if static columns should be included in the scan, the lower-bound key
      // should be the hash key with no range components in order to include the static columns.
      if (!include_static_columns_) {
        return lower_doc_key_;
      }

      KeyBytes result = lower_doc_key_;

      // For lower-bound key, if static columns should be included in the scan, the lower-bound key
      // should be the hash key with no range components in order to include the static columns.
      RETURN_NOT_OK(ClearRangeComponents(&result, AllowSpecial::kTrue));

      return result;
    } else {
      return upper_doc_key_;
    }
  }

  // If we have a start_doc_key, we need to use it as a starting point (lower bound for forward
  // scan, upper bound for reverse scan).
  if (range_bounds_ != nullptr &&
        !KeyWithinRange(start_doc_key_, lower_doc_key_, upper_doc_key_)) {
      return STATUS_FORMAT(
          Corruption, "Invalid start_doc_key: $0. Range: $1, $2",
          start_doc_key_, lower_doc_key_, upper_doc_key_);
  }

  // Paging state + forward scan.
  if (is_forward_scan_) {
    return lower_bound ? start_doc_key_ : upper_doc_key_;
  }

  // Paging state + reverse scan.
  // For reverse scans static columns should be read by a separate iterator.
  DCHECK(!include_static_columns_);
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

std::shared_ptr<rocksdb::ReadFileFilter> DocQLScanSpec::CreateFileFilter() const {
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

Result<KeyBytes> DocQLScanSpec::LowerBound() const {
  return Bound(true /* lower_bound */);
}

Result<KeyBytes> DocQLScanSpec::UpperBound() const {
  return Bound(false /* upper_bound */);
}

const DocKey& DocQLScanSpec::DefaultStartDocKey() {
  static const DocKey result;
  return result;
}

}  // namespace docdb
}  // namespace yb
