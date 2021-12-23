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
#include "yb/docdb/doc_scanspec_util.h"
#include "yb/docdb/value_type.h"

#include "yb/rocksdb/db/compaction.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

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
    std::reference_wrapper<const std::vector<PrimitiveValue>> hashed_components,
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

  // If the hash key is fixed and we have range columns with IN condition, try to construct the
  // exact list of range options to scan for.
  if (!hashed_components_->empty() && schema_.num_range_key_columns() > 0 &&
      range_bounds_ && range_bounds_->has_in_range_options()) {
    DCHECK(condition);
    range_options_ =
        std::make_shared<std::vector<std::vector<PrimitiveValue>>>(schema_.num_range_key_columns());
    InitRangeOptions(*condition);

    // Range options are only valid if all range columns are set (i.e. have one or more options).
    for (int i = 0; i < schema_.num_range_key_columns(); i++) {
      if ((*range_options_)[i].empty()) {
        range_options_ = nullptr;
        break;
      }
    }
  }
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
      if (condition.operands(0).expr_case() != QLExpressionPB::kColumnId) {
        return;
      }

      // Skip any RHS expressions that are not evaluated yet.
      if (condition.operands(1).expr_case() != QLExpressionPB::kValue) {
        return;
      }

      ColumnId col_id = ColumnId(condition.operands(0).column_id());
      int col_idx = schema_.find_column_by_id(col_id);

      // Skip any non-range columns.
      if (!schema_.is_range_column(col_idx)) {
        return;
      }

      SortingType sortingType = schema_.column(col_idx).sorting_type();

      if (condition.op() == QL_OP_EQUAL) {
        auto pv = PrimitiveValue::FromQLValuePB(condition.operands(1).value(), sortingType);
        (*range_options_)[col_idx - num_hash_cols].push_back(std::move(pv));
      } else { // QL_OP_IN
        DCHECK_EQ(condition.op(), QL_OP_IN);
        DCHECK(condition.operands(1).value().has_list_value());
        const auto &options = condition.operands(1).value().list_value();
        int opt_size = options.elems_size();
        (*range_options_)[col_idx - num_hash_cols].reserve(opt_size);

        // IN arguments should have been de-duplicated and ordered ascendingly by the executor.
        bool is_reverse_order = is_forward_scan_ ^ (sortingType == SortingType::kAscending);
        for (int i = 0; i < opt_size; i++) {
          int elem_idx = is_reverse_order ? opt_size - i - 1 : i;
          const auto &elem = options.elems(elem_idx);
          auto pv = PrimitiveValue::FromQLValuePB(elem, sortingType);
          (*range_options_)[col_idx - num_hash_cols].push_back(std::move(pv));
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
      encoder.HashAndRange(*hash_code_, {PrimitiveValue(ValueType::kLowest)}, {});
    }
    // use upper bound hash code if set in request (for scans using token)
    if (!lower_bound && max_hash_code_) {
      encoder.HashAndRange(*max_hash_code_, {PrimitiveValue(ValueType::kHighest)}, {});
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

std::vector<PrimitiveValue> DocQLScanSpec::range_components(const bool lower_bound) const {
  return GetRangeKeyScanSpec(
      schema_, nullptr /* prefixed_range_components */,
      range_bounds_.get(), lower_bound, include_static_columns_);
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
    result.AppendValueTypeBeforeGroupEnd(ValueType::kHighest);
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
  result.AppendValueTypeBeforeGroupEnd(ValueType::kHighest);
  return result;
}

rocksdb::UserBoundaryTag TagForRangeComponent(size_t index);

namespace {

std::vector<KeyBytes> EncodePrimitiveValues(const std::vector<PrimitiveValue>& source,
    size_t min_size) {
  size_t size = source.size();
  std::vector<KeyBytes> result(std::max(min_size, size));
  for (size_t i = 0; i != size; ++i) {
    if (source[i].value_type() != ValueType::kTombstone) {
      source[i].AppendToKey(&result[i]);
    }
  }
  return result;
}

Slice ValueOrEmpty(const Slice* slice) { return slice ? *slice : Slice(); }

// Checks that lhs >= rhs, empty values means positive and negative infinity appropriately.
bool GreaterOrEquals(const Slice& lhs, const Slice& rhs) {
  if (lhs.empty() || rhs.empty()) {
    return true;
  }
  return lhs.compare(rhs) >= 0;
}

class RangeBasedFileFilter : public rocksdb::ReadFileFilter {
 public:
  RangeBasedFileFilter(const std::vector<PrimitiveValue>& lower_bounds,
      const std::vector<PrimitiveValue>& upper_bounds)
      : lower_bounds_(EncodePrimitiveValues(lower_bounds, upper_bounds.size())),
      upper_bounds_(EncodePrimitiveValues(upper_bounds, lower_bounds.size())) {
  }

  bool Filter(const rocksdb::FdWithBoundaries& file) const override {
    for (size_t i = 0; i != lower_bounds_.size(); ++i) {
      auto lower_bound = lower_bounds_[i].AsSlice();
      auto upper_bound = upper_bounds_[i].AsSlice();
      rocksdb::UserBoundaryTag tag = TagForRangeComponent(i);
      auto smallest = ValueOrEmpty(file.smallest.user_value_with_tag(tag));
      auto largest = ValueOrEmpty(file.largest.user_value_with_tag(tag));
      if (!GreaterOrEquals(upper_bound, smallest) || !GreaterOrEquals(largest, lower_bound)) {
        return false;
      }
    }
    return true;
  }
 private:
  std::vector<KeyBytes> lower_bounds_;
  std::vector<KeyBytes> upper_bounds_;
};

} // namespace

std::shared_ptr<rocksdb::ReadFileFilter> DocQLScanSpec::CreateFileFilter() const {
  auto lower_bound = range_components(true);
  auto upper_bound = range_components(false);
  if (lower_bound.empty() && upper_bound.empty()) {
    return std::shared_ptr<rocksdb::ReadFileFilter>();
  } else {
    return std::make_shared<RangeBasedFileFilter>(std::move(lower_bound), std::move(upper_bound));
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
