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
#include "yb/dockv/doc_key.h"
#include "yb/dockv/value_type.h"

#include "yb/qlexpr/doc_scanspec_util.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

using std::vector;

namespace yb {
namespace docdb {

using dockv::KeyBytes;
using dockv::KeyEntryValue;

namespace {

bool AreColumnsContinous(qlexpr::ColumnListVector col_idxs) {
  std::sort(col_idxs.begin(), col_idxs.end());
  for (size_t i = 0; i < col_idxs.size() - 1; ++i) {
    if (col_idxs[i] == kYbHashCodeColId) {
      continue;
    } else if (col_idxs[i] + 1 != col_idxs[i + 1]) {
      return false;
    }
  }
  return true;
}

}  // namespace

DocQLScanSpec::DocQLScanSpec(
    const Schema& schema,
    const dockv::DocKey& doc_key,
    const rocksdb::QueryId query_id,
    const bool is_forward_scan,
    const size_t prefix_length)
    : qlexpr::QLScanSpec(
          schema, is_forward_scan, query_id, /* range_bounds = */ nullptr, prefix_length,
          std::make_shared<DocExprExecutor>()),
      hashed_components_(nullptr),
      options_groups_(0),
      include_static_columns_(false),
      doc_key_(doc_key.Encode()) {
  CompleteBounds();
}

DocQLScanSpec::DocQLScanSpec(
    const Schema& schema,
    const boost::optional<int32_t>
        hash_code,
    const boost::optional<int32_t>
        max_hash_code,
    std::reference_wrapper<const dockv::KeyEntryValues>
        hashed_components,
    const QLConditionPB* condition,
    const QLConditionPB* if_condition,
    const rocksdb::QueryId query_id,
    const bool is_forward_scan,
    const bool include_static_columns,
    const dockv::DocKey& start_doc_key,
    const size_t prefix_length)
    : qlexpr::QLScanSpec(
          schema, is_forward_scan, query_id,
          condition ? std::make_unique<qlexpr::QLScanRange>(schema, *condition) : nullptr,
          prefix_length,
          condition, if_condition, std::make_shared<DocExprExecutor>()),
      hash_code_(hash_code),
      max_hash_code_(max_hash_code),
      hashed_components_(&hashed_components.get()),
      options_groups_(schema.num_dockey_components()),
      include_static_columns_(include_static_columns),
      start_doc_key_(start_doc_key.empty() ? KeyBytes() : start_doc_key.Encode()) {
  bounds_.lower = bound_key(true);
  bounds_.upper = bound_key(false);
  if (!hashed_components_->empty() && schema.num_hash_key_columns()) {
    options_ = std::make_shared<std::vector<qlexpr::OptionList>>(schema.num_dockey_components());
    // should come here if we are not batching hash keys as a part of IN condition
    options_groups_.BeginNewGroup();
    options_groups_.AddToLatestGroup(0);
    options_col_ids_.emplace_back(ColumnId(kYbHashCodeColId));
    (*options_)[0].push_back(KeyEntryValue::UInt16Hash(hash_code_.value()));
    DCHECK(hashed_components_->size() == schema.num_hash_key_columns());

    for (size_t col_idx = 0; col_idx < schema.num_hash_key_columns(); ++col_idx) {
      options_groups_.AddToLatestGroup(schema.get_dockey_component_idx(col_idx));
      options_col_ids_.emplace_back(schema.column_id(col_idx));

      (*options_)[schema.get_dockey_component_idx(col_idx)].push_back(
          std::move((*hashed_components_)[col_idx]));
    }
  }

  // If the hash key is fixed and we have range columns with IN condition, try to construct the
  // exact list of range options to scan for.
  const auto rangebounds = range_bounds();
  if (!hashed_components_->empty() && schema.num_range_key_columns() > 0 && rangebounds &&
      rangebounds->has_in_range_options()) {
    DCHECK(condition);
    if (!options_) {
      options_ = std::make_shared<std::vector<qlexpr::OptionList>>(schema.num_dockey_components());
    }
    InitOptions(*condition);
  }

  CompleteBounds();
}

void DocQLScanSpec::InitOptions(const QLConditionPB& condition) {
  switch (condition.op()) {
    case QLOperator::QL_OP_AND:
      for (const auto& operand : condition.operands()) {
        DCHECK(operand.has_condition());
        InitOptions(operand.condition());
      }
      break;

    case QLOperator::QL_OP_EQUAL:
    case QLOperator::QL_OP_IN: {
      DCHECK_EQ(condition.operands_size(), 2);
      // Skip any condition where LHS is not a column (e.g. subscript columns: 'map[k] = v')
      // operands(0) always contains the column id.
      // operands(1) contains the corresponding value or a list values.
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
        size_t col_idx = schema().find_column_by_id(col_id);

        // Skip any non-range columns.
        if (!schema().is_range_column(col_idx)) {
          DCHECK(!schema().is_hash_key_column(col_idx));
          return;
        }

        auto sorting_type = get_sorting_type(col_idx);

        // Adding the offset if yb_hash_code is present after schema usages. Schema does not know
        // about yb_hash_code_column
        auto key_idx = schema().get_dockey_component_idx(col_idx);

        // TODO: confusing - name says indexes but stores ids
        options_col_ids_.emplace_back(col_id);
        options_groups_.BeginNewGroup();
        options_groups_.AddToLatestGroup(key_idx);

        if (condition.op() == QL_OP_EQUAL) {
          auto pv = KeyEntryValue::FromQLValuePBForKey(rhs.value(), sorting_type);
          (*options_)[key_idx].push_back(std::move(pv));
        } else {  // QL_OP_IN
          DCHECK_EQ(condition.op(), QL_OP_IN);
          DCHECK(rhs.value().has_list_value());
          const auto& options = rhs.value().list_value();
          int opt_size = options.elems_size();
          (*options_)[key_idx].reserve(opt_size);

          // IN arguments should have been de-duplicated and ordered ascendingly by the executor.
          auto is_reverse_order = get_scan_direction(col_idx);
          for (int i = 0; i < opt_size; i++) {
            int elem_idx = is_reverse_order ? opt_size - i - 1 : i;
            const auto& elem = options.elems(elem_idx);
            auto pv = KeyEntryValue::FromQLValuePBForKey(elem, sorting_type);
            (*options_)[key_idx].push_back(std::move(pv));
          }
        }
      } else if (lhs.has_tuple()) {
        size_t total_cols = lhs.tuple().elems_size();
        DCHECK_GT(total_cols, 0);

        qlexpr::ColumnListVector col_idxs;
        col_idxs.reserve(total_cols);
        options_groups_.BeginNewGroup();

        for (const auto& elem : lhs.tuple().elems()) {
          DCHECK(elem.has_column_id());
          ColumnId col_id(elem.column_id());
          auto col_idx = elem.column_id() == kYbHashCodeColId ? kYbHashCodeColId
                                                              : schema().find_column_by_id(col_id);
          col_idxs.push_back(col_idx);
          options_col_ids_.emplace_back(col_id);
          options_groups_.AddToLatestGroup(schema().get_dockey_component_idx(col_idx));
        }

        DCHECK(AreColumnsContinous(col_idxs));

        if (condition.op() == QL_OP_EQUAL) {
          DCHECK(rhs.value().has_list_value());
          const auto& value = rhs.value().list_value();
          DCHECK_EQ(total_cols, value.elems_size());
          for (size_t i = 0; i < total_cols; i++) {
            auto sorting_type = get_sorting_type(col_idxs[i]);
            auto option =
                KeyEntryValue::FromQLValuePBForKey(value.elems(static_cast<int>(i)), sorting_type);
            auto options_idx =
              schema().get_dockey_component_idx(col_idxs[i]);
            (*options_)[options_idx].push_back(std::move(option));
          }
        } else if (condition.op() == QL_OP_IN) {
          DCHECK(rhs.value().has_list_value());
          const auto& options = rhs.value().list_value();
          // IN arguments should have been de-duplicated and ordered ascendingly by the
          // executor.

          std::vector<bool> reverse;
          reverse.reserve(total_cols);

          for (size_t i = 0; i < total_cols; i++) {
            reverse.push_back(get_scan_direction(col_idxs[i]));
          }

          const auto sorted_options = qlexpr::GetTuplesSortedByOrdering(
              options, schema(), is_forward_scan(), col_idxs);

          int num_options = options.elems_size();
          for (int i = 0; i < num_options; i++) {
            const auto& elem = sorted_options[i];
            DCHECK(elem->has_tuple_value());
            const auto& value = elem->tuple_value();
            DCHECK_EQ(total_cols, value.elems_size());

            for (size_t j = 0; j < total_cols; j++) {
              const auto sorting_type = get_sorting_type(col_idxs[j]);
              // For hash tuples, the first element always contains the yb_hash_code
              DCHECK(col_idxs[j] != kYbHashCodeColId || j == 0);
              auto option = (col_idxs[j] == kYbHashCodeColId)
                  ? KeyEntryValue::UInt16Hash(value.elems(static_cast<int>(j)).int32_value())
                  : KeyEntryValue::FromQLValuePBForKey(value.elems(static_cast<int>(j)),
                                                       sorting_type);
              auto options_idx =
                schema().get_dockey_component_idx(col_idxs[j]);
              (*options_)[options_idx].push_back(std::move(option));
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

KeyBytes DocQLScanSpec::bound_key(const bool lower_bound) const {
  KeyBytes result;
  auto encoder = dockv::DocKeyEncoder(&result).CotableId(Uuid::Nil());

  // If no hashed_component use hash lower/upper bounds if set.
  if (hashed_components_->empty()) {
    // use lower bound hash code if set in request (for scans using token)
    if (lower_bound && hash_code_) {
      encoder.HashAndRange(*hash_code_, {KeyEntryValue(dockv::KeyEntryType::kLowest)}, {});
    }
    // use upper bound hash code if set in request (for scans using token)
    if (!lower_bound && max_hash_code_) {
      encoder.HashAndRange(*max_hash_code_, {KeyEntryValue(dockv::KeyEntryType::kHighest)}, {});
    }
    return result;
  }

  // If hash_components are non-empty then hash_code and max_hash_code must both be set and equal.
  DCHECK(hash_code_);
  DCHECK(max_hash_code_);
  DCHECK_EQ(*hash_code_, *max_hash_code_);
  auto hash_code = static_cast<DocKeyHash>(*hash_code_);
  encoder.HashAndRange(hash_code, *hashed_components_, RangeComponents(lower_bound));
  return result;
}

dockv::KeyEntryValues DocQLScanSpec::RangeComponents(bool lower_bound,
                                                     std::vector<bool>* inclusivities) const {
  return GetRangeKeyScanSpec(
      schema(),
      nullptr /* prefixed_range_components */,
      range_bounds(),
      inclusivities,
      lower_bound,
      include_static_columns_);
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

void DocQLScanSpec::CompleteBounds() {
  // If a full doc key is specified, that is the exactly doc to scan. Otherwise, compute the
  // lower/upper bound doc keys to scan from the range.
  if (!doc_key_.empty()) {
    bounds_.lower = doc_key_;
    bounds_.upper = doc_key_;
    // We add +inf as an extra component to make sure this is greater than all keys in range.
    // For lower bound, this is true already, because dockey + suffix is > dockey
    bounds_.upper.AppendKeyEntryTypeBeforeGroupEnd(dockv::KeyEntryType::kHighest);
    return;
  }

  // Otherwise, if we do not have a paging state (start_doc_key) just use the lower/upper bounds.
  if (start_doc_key_.empty()) {
    // For lower-bound key, if static columns should be included in the scan, the lower-bound key
    // should be the hash key with no range components in order to include the static columns.
    if (include_static_columns_) {
      // For lower-bound key, if static columns should be included in the scan, the lower-bound key
      // should be the hash key with no range components in order to include the static columns.
      CHECK_OK(ClearRangeComponents(&bounds_.lower, dockv::AllowSpecial::kTrue));
    }

    return;
  }

  // If we have a start_doc_key, we need to use it as a starting point (lower bound for forward
  // scan, upper bound for reverse scan).
  DCHECK(range_bounds() == nullptr || KeyWithinRange(start_doc_key_, bounds_.lower, bounds_.upper));

  // Paging state + forward scan.
  if (is_forward_scan()) {
    bounds_.lower = start_doc_key_;
    return;
  }

  // Paging state + reverse scan.
  // For reverse scans static columns should be read by a separate iterator.
  DCHECK(!include_static_columns_);

  // If using start_doc_key_ as upper bound append +inf as extra component to ensure it includes
  // the target start_doc_key itself (dockey + suffix < dockey + kHighest).
  // For lower bound, this is true already, because dockey + suffix is > dockey.
  bounds_.upper = start_doc_key_;
  bounds_.upper.AppendKeyEntryTypeBeforeGroupEnd(dockv::KeyEntryType::kHighest);
}

const dockv::DocKey& DocQLScanSpec::DefaultStartDocKey() {
  static const dockv::DocKey result;
  return result;
}

}  // namespace docdb
}  // namespace yb
