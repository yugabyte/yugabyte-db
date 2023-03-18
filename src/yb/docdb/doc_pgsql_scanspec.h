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

#pragma once

#include <functional>

#include "yb/common/ql_scanspec.h"

#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/key_bytes.h"

#include "yb/rocksdb/options.h"

namespace yb {
namespace docdb {

// DocDB variant of scanspec.
class DocPgsqlScanSpec : public PgsqlScanSpec {
 public:

  // Scan for the specified doc_key.
  DocPgsqlScanSpec(const Schema& schema,
                   const rocksdb::QueryId query_id,
                   const DocKey& doc_key,
                   const boost::optional<int32_t> hash_code = boost::none,
                   const boost::optional<int32_t> max_hash_code = boost::none,
                   const DocKey& start_doc_key = DefaultStartDocKey(),
                   bool is_forward_scan = true,
                   const size_t prefix_length = 0);

  // Scan for the given hash key, a condition, and optional doc_key.
  //
  // Note: std::reference_wrapper is used instead of raw lvalue reference to prevent
  // temporary objects usage. The following code wont compile:
  //
  // DocPgsqlScanSpec spec(...{} /* hashed_components */, {} /* range_components */...);
  DocPgsqlScanSpec(const Schema& schema,
                   const rocksdb::QueryId query_id,
                   std::reference_wrapper<const std::vector<KeyEntryValue>> hashed_components,
                   std::reference_wrapper<const std::vector<KeyEntryValue>> range_components,
                   const PgsqlConditionPB* condition,
                   boost::optional<int32_t> hash_code,
                   boost::optional<int32_t> max_hash_code,
                   const PgsqlExpressionPB *where_expr,
                   const DocKey& start_doc_key = DefaultStartDocKey(),
                   bool is_forward_scan = true,
                   const DocKey& lower_doc_key = DefaultStartDocKey(),
                   const DocKey& upper_doc_key = DefaultStartDocKey(),
                   const size_t prefix_length = 0);

  //------------------------------------------------------------------------------------------------
  // Access funtions.
  const rocksdb::QueryId QueryId() const {
    return query_id_;
  }

  const size_t prefix_length() const {
    return prefix_length_;
  }

  //------------------------------------------------------------------------------------------------
  // Filters.
  std::shared_ptr<rocksdb::ReadFileFilter> CreateFileFilter() const;

  // Return the inclusive lower and upper bounds of the scan.
  Result<KeyBytes> LowerBound() const;
  Result<KeyBytes> UpperBound() const;

  // Returns the lower/upper range components of the key.
  std::vector<KeyEntryValue> range_components(const bool lower_bound,
                                              std::vector<bool> *inclusivities = nullptr,
                                              bool use_strictness = true) const;

  const QLScanRange* range_bounds() const {
    return range_bounds_.get();
  }

  const std::shared_ptr<std::vector<OptionList>>& options() const { return options_; }

  const std::vector<ColumnId>& options_indexes() const {
    return options_col_ids_;
  }

  const std::vector<ColumnId>& range_bounds_indexes() const {
    return range_bounds_indexes_;
  }

  const ColGroupHolder& options_groups() const {
    return options_groups_;
  }

 private:
  static const DocKey& DefaultStartDocKey();

  // Return inclusive lower/upper range doc key considering the start_doc_key.
  Result<KeyBytes> Bound(const bool lower_bound) const;

  // Returns the lower/upper doc key based on the range components.
  KeyBytes bound_key(const Schema& schema, const bool lower_bound) const;

  // The scan range within the hash key when a WHERE condition is specified.
  const std::unique_ptr<const QLScanRange> range_bounds_;

  // Ids of columns that have range bounds such as c2 < 4 AND c2 >= 1.
  std::vector<ColumnId> range_bounds_indexes_;

  // Initialize options_ if range columns have one or more options (i.e. using EQ/IN
  // conditions). Otherwise options_ will stay null and we will only use the range_bounds for
  // scanning.
  void InitOptions(const PgsqlConditionPB& condition);

  // The range/hash value options if set (possibly more than one due to IN conditions).
  std::shared_ptr<std::vector<OptionList>> options_;

  // Ids of key columns that have filters such as h1 IN (1, 5, 6, 9) or r1 IN (5, 6, 7)
  std::vector<ColumnId> options_col_ids_;

  const rocksdb::QueryId query_id_;

  // The hashed_components are owned by the caller of QLScanSpec.
  const std::vector<KeyEntryValue> *hashed_components_;
  // The range_components are owned by the caller of QLScanSpec.
  const std::vector<KeyEntryValue> *range_components_;

  // Groups of column indexes found from the filters.
  // Eg: If we had an incoming filter of the form (r1, r3, r4) IN ((1,2,5), (5,4,3), ...)
  // AND r2 <= 5
  // where (r1,r2,r3,r4) is the primary key of this table, then
  // options_groups_ would contain the groups {0,2,3} and {1}.
  ColGroupHolder options_groups_;

  // Hash code is used if hashed_components_ vector is empty.
  // hash values are positive int16_t.
  const boost::optional<int32_t> hash_code_;

  // Max hash code is used if hashed_components_ vector is empty.
  // hash values are positive int16_t.
  const boost::optional<int32_t> max_hash_code_;

  // Starting doc key when requested by the client.
  const KeyBytes start_doc_key_;

  // Lower and upper keys for range condition.
  KeyBytes lower_doc_key_;
  KeyBytes upper_doc_key_;

  size_t prefix_length_ = 0;

  DISALLOW_COPY_AND_ASSIGN(DocPgsqlScanSpec);
};

}  // namespace docdb
}  // namespace yb
