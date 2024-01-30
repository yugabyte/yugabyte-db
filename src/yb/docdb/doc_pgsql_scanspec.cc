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

#include <algorithm>
#include <limits>

#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/value_type.h"

#include "yb/qlexpr/doc_scanspec_util.h"

#include "yb/util/logging.h"
#include "yb/util/status_format.h"

namespace yb {
namespace docdb {

using dockv::DocKey;
using dockv::KeyBytes;
using dockv::KeyEntryType;
using dockv::KeyEntryValue;

DocPgsqlScanSpec::DocPgsqlScanSpec(
    const Schema& schema,
    const rocksdb::QueryId query_id,
    const DocKey& doc_key,
    const std::optional<int32_t> hash_code,
    const std::optional<int32_t> max_hash_code,
    const DocKey& start_doc_key,
    bool is_forward_scan,
    const size_t prefix_length)
    : PgsqlScanSpec(
          schema, is_forward_scan, query_id, /* range_bounds = */ nullptr, prefix_length),
      hashed_components_(nullptr),
      range_components_(nullptr),
      options_groups_(schema.num_dockey_components()),
      hash_code_(hash_code),
      max_hash_code_(max_hash_code),
      start_doc_key_(start_doc_key.empty() ? KeyBytes() : start_doc_key.Encode()) {
  bounds_.lower = doc_key.Encode();
  // Compute lower and upper doc_key.
  // We add +inf as an extra component to make sure this is greater than all keys in range.
  // For lower bound, this is true already, because dockey + suffix is > dockey
  bounds_.upper = bounds_.lower;

  if (hash_code && !doc_key.has_hash()) {
    DocKey lower_doc_key = DocKey(doc_key);
    lower_doc_key.set_hash(*hash_code);
    if (lower_doc_key.hashed_group().empty()) {
      lower_doc_key.hashed_group().emplace_back(KeyEntryType::kLowest);
    }
    bounds_.lower = lower_doc_key.Encode();
  }

  if (max_hash_code) {
    DocKey upper_doc_key = DocKey(doc_key);
    upper_doc_key.set_hash(*max_hash_code);
    if (upper_doc_key.hashed_group().empty()) {
      upper_doc_key.hashed_group().emplace_back(KeyEntryType::kHighest);
    }
    bounds_.upper = upper_doc_key.Encode();
  }

  bounds_.upper.AppendKeyEntryTypeBeforeGroupEnd(KeyEntryType::kHighest);

  CompleteBounds();
}

DocPgsqlScanSpec::DocPgsqlScanSpec(
    const Schema& schema,
    const rocksdb::QueryId query_id,
    std::reference_wrapper<const dockv::KeyEntryValues> hashed_components,
    std::reference_wrapper<const dockv::KeyEntryValues> range_components,
    const PgsqlConditionPB* condition,
    const std::optional<int32_t> hash_code,
    const std::optional<int32_t> max_hash_code,
    const DocKey& start_doc_key,
    bool is_forward_scan,
    const DocKey& lower_doc_key,
    const DocKey& upper_doc_key,
    const size_t prefix_length,
    AddHighestToUpperDocKey add_highest_to_upper_doc_key)
    : PgsqlScanSpec(
          schema, is_forward_scan, query_id,
          condition ? std::make_unique<qlexpr::QLScanRange>(schema, *condition) : nullptr,
          prefix_length),
      hashed_components_(&hashed_components.get()),
      range_components_(&range_components.get()),
      options_groups_(schema.num_dockey_components()),
      hash_code_(hash_code),
      max_hash_code_(max_hash_code),
      start_doc_key_(start_doc_key.empty() ? KeyBytes() : start_doc_key.Encode()) {
  bounds_.lower = lower_doc_key.Encode();
  bounds_.upper = upper_doc_key.Encode();
  if (add_highest_to_upper_doc_key) {
    bounds_.upper.AppendKeyEntryTypeBeforeGroupEnd(KeyEntryType::kHighest);
  }

  if (!hashed_components_->empty() && schema.num_hash_key_columns() > 0) {
    options_ = std::make_shared<std::vector<qlexpr::OptionList>>(schema.num_dockey_components());
    options_col_ids_.reserve(schema.num_dockey_components());

    // should come here if we are not batching hash keys as a part of IN condition
    options_groups_.BeginNewGroup();

    // dockeys contains elements in the format yb_hash_code, hk1, hk2, ... hkn followed by
    // rk1, rk2... rkn etc. As yb_hash_code is the first element and is not part of the schema
    // we add it manually.
    options_groups_.AddToLatestGroup(0);
    options_col_ids_.emplace_back(ColumnId(kYbHashCodeColId));

    (*options_)[0].push_back(KeyEntryValue::UInt16Hash(hash_code_.value()));
    DCHECK(hashed_components_->size() == schema.num_hash_key_columns());
    for (size_t col_idx = 0; col_idx < schema.num_hash_key_columns(); ++col_idx) {
      // Adding 1 to col_idx to account for hash_code column
      options_groups_.AddToLatestGroup(schema.get_dockey_component_idx(col_idx));
      options_col_ids_.emplace_back(schema.column_id(col_idx));

      (*options_)[schema.get_dockey_component_idx(col_idx)]
          .push_back(std::move((*hashed_components_)[col_idx]));
    }
  }

  // We have hash or range columns with IN condition, try to construct the exact list of options to
  // scan for.
  const auto rangebounds = range_bounds();
  if (rangebounds &&
      (rangebounds->has_in_range_options() || rangebounds->has_in_hash_options())) {
    DCHECK(condition);
    if (options_ == nullptr)
      options_ = std::make_shared<std::vector<qlexpr::OptionList>>(schema.num_dockey_components());
    InitOptions(*condition);
  }

  auto calculated_bounds = CalculateBounds(schema);
  if (lower_doc_key.empty() || calculated_bounds.lower > bounds_.lower) {
    bounds_.lower = std::move(calculated_bounds.lower);
  }

  if (upper_doc_key.empty() || calculated_bounds.upper < bounds_.upper) {
    bounds_.upper = std::move(calculated_bounds.upper);
  }
  bounds_.trivial = calculated_bounds.trivial;

  CompleteBounds();
}

void DocPgsqlScanSpec::InitOptions(const PgsqlConditionPB& condition) {
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

        auto col_id = ColumnId(lhs.column_id());
        auto col_idx = schema().find_column_by_id(col_id);

        // Skip any non-range columns.
        if (!schema().is_range_column(col_idx)) {
          // Hashed columns should always be sent as tuples along with their yb_hash_code.
          // Hence, for hashed columns lhs should never be a column id.
          YB_LOG_EVERY_N_SECS_OR_VLOG(DFATAL, 60, 1)
              << "Expected only range column: id=" << col_id << " idx=" << col_idx << THROTTLE_MSG;
          return;
        }

        auto sortingType = get_sorting_type(col_idx);

        // Adding the offset if yb_hash_code is present after schema usages. Schema does not know
        // about yb_hash_code_column
        auto key_idx = schema().get_dockey_component_idx(col_idx);

        options_col_ids_.emplace_back(col_id);
        options_groups_.BeginNewGroup();
        options_groups_.AddToLatestGroup(key_idx);

        if (condition.op() == QL_OP_EQUAL) {
          auto pv = KeyEntryValue::FromQLValuePBForKey(condition.operands(1).value(), sortingType);
          (*options_)[key_idx].push_back(std::move(pv));
        } else { // QL_OP_IN
          DCHECK_EQ(condition.op(), QL_OP_IN);
          DCHECK(rhs.value().has_list_value());
          const auto &options = rhs.value().list_value();
          int opt_size = options.elems_size();
          (*options_)[key_idx].reserve(opt_size);

          // IN arguments should have been de-duplicated and ordered ascendingly by the executor.
          bool is_reverse_order = get_scan_direction(col_idx);
          for (int i = 0; i < opt_size; i++) {
            int elem_idx = is_reverse_order ? opt_size - i - 1 : i;
            const auto &elem = options.elems(elem_idx);
            auto pv = KeyEntryValue::FromQLValuePBForKey(elem, sortingType);
            (*options_)[key_idx].push_back(std::move(pv));
          }
        }
      } else if (lhs.has_tuple()) {
        size_t total_cols = lhs.tuple().elems_size();
        DCHECK_GT(total_cols, 0);

        // Whenever you have a tuple as a part of IN array, the query is of two types:
        // 1. Range tuples SELECT * FROM table where (r1, r2) IN ((1, 1), (2, 2), (3,3));
        // 2. Hash tuples
        //    a. SELECT * FROM table where (h1, h2) IN ((1, 1), (2, 2), (3,3));
        //    b. SELECT * FROM table where h1 IN (1, 2, 3, 4) AND h2 IN (5, 6, 7, 8);
        // 3. Hash and range mix.
        // The hash columns in the lhs are always expected to appear to the left of all the
        // range columns. We only take care to add the range components of the lhs to
        // options_groups_ and options_col_ids_.
        // In each of these situations, the following steps have to be undertaken
        //
        // Step 1: Get the column ids of the elements
        // For range tuples its (r1, r2).
        // For hash tuples its (yb_hash_code, h1, h2), (yb_hash_code, h3, h4)
        // Push them into the options groups and options indexes as hybrid scan utilizes to match
        // target elements with their corresponding columns.
        int start_range_col_idx = 0;
        qlexpr::ColumnListVector col_idxs;
        options_groups_.BeginNewGroup();

        for (const auto& elem : lhs.tuple().elems()) {
          DCHECK(elem.has_column_id());
          ColumnId col_id = ColumnId(elem.column_id());
          auto col_idx = elem.column_id() == kYbHashCodeColId ? kYbHashCodeColId
              : schema().find_column_by_id(col_id);
          col_idxs.push_back(col_idx);
          if (!schema().is_range_column(col_idx)) {
            start_range_col_idx++;
          }
          options_col_ids_.emplace_back(col_id);
          // yb_hash_code takes the 0th group. If there exists a yb_hash_code column, then we offset
          // other columns by one position from what schema().find_column_by_id(col_id) provides us
          options_groups_.AddToLatestGroup(schema().get_dockey_component_idx(col_idx));
        }

        if (condition.op() == QL_OP_EQUAL) {
          DCHECK(rhs.value().has_list_value());
          const auto& value = rhs.value().list_value();
          DCHECK_EQ(total_cols, value.elems_size());
          for (size_t i = start_range_col_idx; i < total_cols; i++) {
            // hash codes are always sorted ascending.
            auto option = KeyEntryValue::FromQLValuePBForKey(value.elems(static_cast<int>(i)),
                                                             get_sorting_type(col_idxs[i]));
            auto options_idx = schema().get_dockey_component_idx(col_idxs[i]);
            (*options_)[options_idx].push_back(std::move(option));
          }
        } else if (condition.op() == QL_OP_IN) {
          // There should be no range columns before start_range_col_idx in col_idxs
          // and there should be no hash columns after start_range_col_idx
          DCHECK(std::find_if(col_idxs.begin(), col_idxs.begin() + start_range_col_idx,
                              [this] (int idx) { return schema().is_range_column(idx); })
                 == (col_idxs.begin() + start_range_col_idx));
          DCHECK(std::find_if(col_idxs.begin() + start_range_col_idx, col_idxs.end(),
                              [this] (int idx) { return schema().is_hash_key_column(idx); })
                 == (col_idxs.end()));

          // Obtain the list of tuples that contain the target values.
          DCHECK(rhs.value().has_list_value());
          const auto& options = rhs.value().list_value();

          // IN arguments should have been de-duplicated and ordered ascendingly by the executor.
          // For range columns, yb_scan sorts them according to the range key values at the pggate
          // layer itself. For hash key columns, we need to sort the options based on the
          // yb_hash_code value. This enables the docDB iterator to pursue a one pass scan on the
          // list of hash key columns
          //
          // Step 2: Obtain the sorting order for elements. For hash key columns its always
          // SortingType::kAscending based on the yb_hash_code, and then the individual hash key
          // components subsequently. For range key columns, we try to obtain it from the column
          // structure.
          std::vector<bool> reverse;
          reverse.reserve(total_cols);
          for (size_t i = 0; i < total_cols; i++) {
            reverse.push_back(get_scan_direction(col_idxs[i]));
          }

          const auto sorted_options = qlexpr::GetTuplesSortedByOrdering(
              options, schema(), is_forward_scan(), col_idxs);

          // Step 3: Add the sorted options into the options_ vector for HybridScan to use them to
          // perform seeks and nexts.
          // options_ array indexes into every key column. Here we append to every key column the
          // list of target elements that needs to be scanned.
          int num_options = options.elems_size();
          for (int i = 0; i < num_options; i++) {
            const auto& elem = sorted_options[i];
            DCHECK(elem->has_tuple_value());
            const auto& value = elem->tuple_value();
            DCHECK_EQ(total_cols, value.elems_size());

            for (size_t j = 0; j < total_cols; j++) {
              const auto sorting_type = get_sorting_type(col_idxs[j]);

              // For hash tuples, the first element always contains the yb_hash_code
              auto option = (j == 0 && col_idxs[j] == kYbHashCodeColId)
                ? KeyEntryValue::UInt16Hash(value.elems(static_cast<int>(j)).int32_value())
                : KeyEntryValue::FromQLValuePBForKey(value.elems(static_cast<int>(j)),
                                                     sorting_type);
              auto options_idx = schema().get_dockey_component_idx(col_idxs[j]);
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

qlexpr::ScanBounds DocPgsqlScanSpec::CalculateBounds(const Schema& schema) const {
  bool has_hash_columns = schema.num_hash_key_columns() > 0;

  // The first column in a hash partitioned table is the hash code column.
  bool has_in_hash_options = has_hash_columns && options_ && !options_->empty()
      && !(*options_)[schema.get_dockey_component_idx(0)].empty();
  dockv::KeyEntryValues hashed_components;
  hashed_components.reserve(schema.num_hash_key_columns());

  int32_t hash_code;
  int32_t max_hash_code;
  auto append_hashed_component = false;
  if (hashed_components_->empty() && has_in_hash_options) {
    DCHECK_GE(options_->size(),
              schema.num_hash_key_columns() + schema.has_yb_hash_code());
    append_hashed_component = true;
    hash_code = static_cast<int32_t>((*options_)[0].front().GetUInt16Hash());
    max_hash_code = static_cast<int32_t>((*options_)[0].back().GetUInt16Hash());
  } else {
    hash_code = hash_code_.value_or(std::numeric_limits<DocKeyHash>::min());
    max_hash_code = max_hash_code_.value_or(std::numeric_limits<DocKeyHash>::max());
    hashed_components = *hashed_components_;
  }

  qlexpr::ScanBounds bounds;
  auto lower_bound_encoder = dockv::DocKeyEncoder(&bounds.lower).Schema(schema);
  auto upper_bound_encoder = dockv::DocKeyEncoder(&bounds.upper).Schema(schema);

  bool hash_components_unset =
      has_hash_columns &&
      (hashed_components.empty() && hashed_components_->empty() && !append_hashed_component);
  if (hash_components_unset) {
    // use lower bound hash code if set in request (for scans using token)
    if (hash_code) {
      lower_bound_encoder.HashAndRange(hash_code,
                                       {KeyEntryValue(KeyEntryType::kLowest)},
                                       {KeyEntryValue(KeyEntryType::kLowest)});
    }
    // use upper bound hash code if set in request (for scans using token)
    if (max_hash_code_) {
      upper_bound_encoder.HashAndRange(max_hash_code,
                                       {KeyEntryValue(KeyEntryType::kHighest)},
                                       {KeyEntryValue(KeyEntryType::kHighest)});
    } else {
      bounds.upper.AppendKeyEntryTypeBeforeGroupEnd(KeyEntryType::kHighest);
    }
    return bounds;
  }

  bool single_hash = false;
  bool lower_trivial = false;
  bool upper_trivial = false;
  if (has_hash_columns) {
    single_hash = hash_code == max_hash_code;
    if (append_hashed_component) {
      for (size_t i = 0; i < schema.num_hash_key_columns(); ++i) {
        const auto& option = (*options_)[schema.get_dockey_component_idx(i)];
        hashed_components.push_back(option.front());
        single_hash = single_hash && (option.front() == option.back());
      }
    }
    lower_bound_encoder.
        Hash(hash_code, hashed_components).
        Range(DoRangeComponents(true, nullptr, &lower_trivial));

    if (append_hashed_component) {
      hashed_components.clear();
      for (size_t i = 0; i < schema.num_hash_key_columns(); ++i) {
        hashed_components.push_back((*options_)[schema.get_dockey_component_idx(i)].back());
      }
    }
    upper_bound_encoder.
        Hash(max_hash_code, hashed_components).
        Range(DoRangeComponents(false, nullptr, &upper_trivial));
  } else {
    single_hash = true;
    lower_bound_encoder.NoHash().Range(DoRangeComponents(true, nullptr, &lower_trivial));
    upper_bound_encoder.NoHash().Range(DoRangeComponents(false, nullptr, &upper_trivial));
  }
  bounds.trivial = single_hash && lower_trivial && upper_trivial;

  return bounds;
}

dockv::KeyEntryValues DocPgsqlScanSpec::RangeComponents(const bool lower_bound,
                                                        std::vector<bool>* inclusivities) const {
  return DoRangeComponents(lower_bound, inclusivities);
}

dockv::KeyEntryValues DocPgsqlScanSpec::DoRangeComponents(
    const bool lower_bound, std::vector<bool>* inclusivities, bool* trivial) const {
  return GetRangeKeyScanSpec(schema(),
                             range_components_,
                             range_bounds(),
                             inclusivities,
                             lower_bound,
                             false,
                             trivial);
}

void DocPgsqlScanSpec::CompleteBounds() {
  if (start_doc_key_.empty()) {
    return;
  }

  // When paging state is present, start_doc_key_ should have been provided, and the scan starting
  // point should be start_doc_key_ instead of the initial bounds.
  if (start_doc_key_ < bounds_.lower || start_doc_key_ > bounds_.upper) {
    LOG(DFATAL) << STATUS_FORMAT(Corruption, "Invalid start_doc_key: $0. Range: $1, $2",
                                 start_doc_key_, bounds_.lower, bounds_.upper);
    return;
  }

  // Paging state + forward scan.
  if (is_forward_scan()) {
    bounds_.lower = start_doc_key_;
    return;
  }

  // Paging state + reverse scan.
  // If using start_doc_key_ as upper bound append +inf as extra component to ensure it includes
  // the target start_doc_key itself (dockey + suffix < dockey + kHighest).
  // For lower bound, this is true already, because dockey + suffix is > dockey.
  bounds_.upper = start_doc_key_;
  bounds_.upper.AppendKeyEntryTypeBeforeGroupEnd(KeyEntryType::kHighest);
}

const DocKey& DocPgsqlScanSpec::DefaultStartDocKey() {
  static const DocKey result;
  return result;
}

}  // namespace docdb
}  // namespace yb
