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
#include "yb/rocksdb/db/compaction.h"

namespace yb {
namespace docdb {

DocQLScanSpec::DocQLScanSpec(const Schema& schema, const DocKey& doc_key,
                               const rocksdb::QueryId query_id)
    : QLScanSpec(nullptr),
      range_(nullptr),
      schema_(schema),
      hash_code_(-1),
      max_hash_code_(-1),
      hashed_components_(nullptr),
      doc_key_(doc_key),
      start_doc_key_(DocKey()),
      lower_doc_key_(DocKey()),
      upper_doc_key_(DocKey()),
      include_static_columns_(false),
      query_id_(query_id) {
  }


DocQLScanSpec::DocQLScanSpec(const Schema& schema, const int32_t hash_code,
                               const int32_t max_hash_code,
                               const std::vector<PrimitiveValue>& hashed_components,
                               const QLConditionPB* condition,
                               const rocksdb::QueryId query_id,
                               const bool include_static_columns,
                               const DocKey& start_doc_key)
    : QLScanSpec(condition),
      range_(condition ? new common::QLScanRange(schema, *condition) : nullptr),
      schema_(schema),
      hash_code_(hash_code),
      max_hash_code_(max_hash_code),
      hashed_components_(&hashed_components),
      doc_key_(),
      start_doc_key_(start_doc_key),
      lower_doc_key_(bound_key(true)),
      upper_doc_key_(bound_key(false)),
      include_static_columns_(include_static_columns),
      query_id_(query_id) {
  // Initialize the upper and lower doc keys.
  CHECK(hashed_components_ != nullptr) << "hashed primary key columns missing";
}

DocKey DocQLScanSpec::bound_key(const bool lower_bound) const {
  // If no hashed_component use hash lower/upper bounds if set.
  if (hashed_components_->empty()) {
    // use lower bound hash code if set in request (for scans using token)
    if (lower_bound && hash_code_ != -1) {
      return DocKey(hash_code_, {}, {});
    }
    // use upper bound hash code if set in request (for scans using token)
    if (!lower_bound && max_hash_code_ != -1) {
      return DocKey(max_hash_code_, {}, {});
    }

    return DocKey();
  }

  // if hash_code not set (-1) default to 0 (start from the beginning)
  return DocKey(hash_code_ == - 1 ? 0 : hash_code_,
                *hashed_components_, range_components(lower_bound));
}

std::vector<PrimitiveValue> DocQLScanSpec::range_components(const bool lower_bound) const {
  std::vector<PrimitiveValue> result;
  if (range_ != nullptr) {
    const std::vector<QLValuePB> range_values = range_->range_values(lower_bound);
    result.reserve(range_values.size());
    size_t column_idx = schema_.num_hash_key_columns();
    for (const auto& value : range_values) {
      const auto& column = schema_.column(column_idx);
      if (QLValue::IsNull(value)) {
        result.emplace_back(PrimitiveValue(lower_bound ? ValueType::kLowest : ValueType::kHighest));
      } else {
        result.emplace_back(PrimitiveValue::FromQLValuePB(value, column.sorting_type()));
      }
      column_idx++;
    }
  }
  return result;
}

namespace {

bool KeyWithinRange(const DocKey& key, const DocKey& lower_key, const DocKey& upper_key) {
  // Verify that the key is within the lower/upper bound, which is either:
  // 1. the bound is empty,
  // 2. the bound has no range component and the key's hash components are the same as the bound's,
  // 3. the key is <= or >= the fully-specified bound.
  return ((lower_key.empty() ||
           lower_key.range_group().empty() && key.HashedComponentsEqual(lower_key) ||
           lower_key <= key) &&
          (upper_key.empty() ||
           upper_key.range_group().empty() && key.HashedComponentsEqual(upper_key) ||
           upper_key >= key));
}

} // namespace

Status DocQLScanSpec::GetBoundKey(const bool lower_bound, DocKey* key) const {
  // If a full doc key is specified, that is the exactly doc to scan. Otherwise, compute the
  // lower/upper bound doc keys to scan from the range.
  if (!doc_key_.empty()) {
    *key = doc_key_;
    return Status::OK();
  }

  // If start doc_key is set, that is the lower bound for the scan range.
  if (lower_bound && !start_doc_key_.empty()) {
    if (range_ != nullptr && !KeyWithinRange(start_doc_key_, lower_doc_key_, upper_doc_key_)) {
      return STATUS_SUBSTITUTE(Corruption,
                               "Invalid start_doc_key: $0. Range: $1, $2",
                               start_doc_key_.ToString(),
                               lower_doc_key_.ToString(),
                               upper_doc_key_.ToString());
    }
    *key = start_doc_key_;
    return Status::OK();
  }

  if (lower_bound) {
    *key = lower_doc_key_;

    // For lower-bound key, if static columns should be incldued in the scan, the lower-bound key
    // should be the hash key with no range components in order to include the static columns.
    if (include_static_columns_) {
      key->ClearRangeComponents();
    }

  } else {
    *key = upper_doc_key_;
  }
  return Status::OK();
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

}  // namespace docdb
}  // namespace yb
