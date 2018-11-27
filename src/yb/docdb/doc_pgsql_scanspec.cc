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

#include "yb/docdb/doc_expr.h"
#include "yb/rocksdb/db/compaction.h"

namespace yb {
namespace docdb {

//--------------------------------------------------------------------------------------------------
extern rocksdb::UserBoundaryTag TagForRangeComponent(size_t index);

// TODO(neil) The following implementation is just a prototype. Need to complete the implementation
// and test accordingly.
class PgsqlRangeBasedFileFilter : public rocksdb::ReadFileFilter {
 public:
  PgsqlRangeBasedFileFilter(const std::vector<PrimitiveValue>& lower_bounds,
                            const std::vector<PrimitiveValue>& upper_bounds)
      : lower_bounds_(EncodePrimitiveValues(lower_bounds, upper_bounds.size())),
        upper_bounds_(EncodePrimitiveValues(upper_bounds, lower_bounds.size())) {
  }

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

  bool Filter(const rocksdb::FdWithBoundaries& file) const override {
    for (size_t i = 0; i != lower_bounds_.size(); ++i) {
      const Slice lower_bound = lower_bounds_[i].AsSlice();
      const Slice upper_bound = upper_bounds_[i].AsSlice();

      rocksdb::UserBoundaryTag tag = TagForRangeComponent(i);
      const Slice *smallest = file.smallest.user_value_with_tag(tag);
      const Slice *largest = file.largest.user_value_with_tag(tag);

      if (!GreaterOrEquals(&upper_bound, smallest) || !GreaterOrEquals(largest, &lower_bound)) {
        return false;
      }
    }
    return true;
  }

  bool GreaterOrEquals(const Slice *lhs, const Slice *rhs) const {
    // TODO(neil) Need to double check this NULL-equals-all logic or make the code clearer.
    if (lhs == nullptr || rhs == nullptr) {
      return true;
    }
    if (lhs->empty() || rhs->empty()) {
      return true;
    }
    return lhs->compare(*rhs) >= 0;
  }

 private:
  std::vector<KeyBytes> lower_bounds_;
  std::vector<KeyBytes> upper_bounds_;
};

//--------------------------------------------------------------------------------------------------

DocPgsqlScanSpec::DocPgsqlScanSpec(const Schema& schema,
                                   const rocksdb::QueryId query_id,
                                   const DocKey& doc_key,
                                   bool is_forward_scan)
    : PgsqlScanSpec(YQL_CLIENT_PGSQL, nullptr),
      query_id_(query_id),
      hashed_components_(nullptr),
      doc_key_(doc_key),
      start_doc_key_(DocKey(schema)),
      lower_doc_key_(DocKey(schema)),
      upper_doc_key_(DocKey(schema)),
      is_forward_scan_(is_forward_scan) {
}

DocPgsqlScanSpec::DocPgsqlScanSpec(const Schema& schema,
                                   const rocksdb::QueryId query_id,
                                   const std::vector<PrimitiveValue>& hashed_components,
                                   const boost::optional<int32_t> hash_code,
                                   const boost::optional<int32_t> max_hash_code,
                                   const PgsqlExpressionPB *where_expr,
                                   const DocKey& start_doc_key,
                                   bool is_forward_scan)
    : PgsqlScanSpec(YQL_CLIENT_PGSQL, where_expr),
      query_id_(query_id),
      hashed_components_(&hashed_components),
      hash_code_(hash_code),
      max_hash_code_(max_hash_code),
      doc_key_(),
      start_doc_key_(start_doc_key),
      lower_doc_key_(bound_key(schema, true)),
      upper_doc_key_(bound_key(schema, false)),
      is_forward_scan_(is_forward_scan) {
  if (where_expr_) {
    // Should never get here until WHERE clause is supported.
    LOG(FATAL) << "DEVELOPERS: Add support for condition (where clause)";
  }
}

DocKey DocPgsqlScanSpec::bound_key(const Schema& schema, const bool lower_bound) const {
  // If no hashed_component use hash lower/upper bounds if set.
  if (hashed_components_->empty()) {
    // use lower bound hash code if set in request (for scans using token)
    if (lower_bound && hash_code_) {
      return DocKey(schema, *hash_code_, {PrimitiveValue(ValueType::kLowest)}, {});
    }
    // use upper bound hash code if set in request (for scans using token)
    if (!lower_bound && max_hash_code_) {
      return DocKey(schema, *max_hash_code_, {PrimitiveValue(ValueType::kHighest)}, {});
    }
    return DocKey(schema);
  }

  DocKeyHash min_hash = hash_code_ ?
      static_cast<DocKeyHash> (*hash_code_) : std::numeric_limits<DocKeyHash>::min();
  DocKeyHash max_hash = max_hash_code_ ?
      static_cast<DocKeyHash> (*max_hash_code_) : std::numeric_limits<DocKeyHash>::max();


  // if hash_code not set (-1) default to 0 (start from the beginning)
  return DocKey(schema,
                lower_bound ? min_hash : max_hash,
                *hashed_components_,
                range_components(lower_bound));
}

std::vector<PrimitiveValue> DocPgsqlScanSpec::range_components(const bool lower_bound) const {
  std::vector<PrimitiveValue> result;
  if (!lower_bound) {
    // We add +inf as an extra component to make sure this is greater than all keys in range.
    // For lower bound, this is true already, because dockey + suffix is > dockey
    result.emplace_back(PrimitiveValue(ValueType::kHighest));
  }
  return result;
}

CHECKED_STATUS DocPgsqlScanSpec::GetBoundKey(const bool lower_bound, DocKey* key) const {
  // If a full doc key is specified, that is the exactly doc to scan. Otherwise, compute the
  // lower/upper bound doc keys to scan from the range.
  if (!doc_key_.empty()) {
    *key = doc_key_;
    if (!lower_bound) {
      // We add +inf as an extra component to make sure this is greater than all keys in range.
      // For lower bound, this is true already, because dockey + suffix is > dockey
      key->AddRangeComponent(PrimitiveValue(ValueType::kHighest));
    }
    return Status::OK();
  }

  // If start doc_key is set, that is the lower bound for the scan range.
  if (lower_bound) {
    if (!start_doc_key_.empty()) {
      *key = start_doc_key_;
    } else {
      *key = lower_doc_key_;
    }
  } else {
    *key = upper_doc_key_;
  }

  return Status::OK();
}

std::shared_ptr<rocksdb::ReadFileFilter> DocPgsqlScanSpec::CreateFileFilter() const {
  auto lower_bound = range_components(true);
  auto upper_bound = range_components(false);
  if (lower_bound.empty() && upper_bound.empty()) {
    return std::shared_ptr<rocksdb::ReadFileFilter>();
  } else {
    return std::make_shared<PgsqlRangeBasedFileFilter>(std::move(lower_bound),
                                                       std::move(upper_bound));
  }
}

}  // namespace docdb
}  // namespace yb
