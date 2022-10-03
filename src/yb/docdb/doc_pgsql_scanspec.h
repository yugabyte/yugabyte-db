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

#ifndef YB_DOCDB_DOC_PGSQL_SCANSPEC_H
#define YB_DOCDB_DOC_PGSQL_SCANSPEC_H

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
                   bool is_forward_scan = true);

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
                   const DocKey& upper_doc_key = DefaultStartDocKey());

  //------------------------------------------------------------------------------------------------
  // Access funtions.
  const rocksdb::QueryId QueryId() const {
    return query_id_;
  }

  bool is_forward_scan() const {
    return is_forward_scan_;
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

  const std::shared_ptr<std::vector<OptionList>>& range_options() const { return range_options_; }

  const std::vector<ColumnId> range_options_indexes() const {
    return range_options_indexes_;
  }

  const std::vector<ColumnId> range_bounds_indexes() const {
    return range_bounds_indexes_;
  }

  const std::vector<size_t> range_options_num_cols() const {
    return range_options_num_cols_;
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

  // Initialize range_options_ if hashed_components_ in set and all range columns have one or more
  // options (i.e. using EQ/IN conditions). Otherwise range_options_ will stay null and we will
  // only use the range_bounds for scanning.
  void InitRangeOptions(const PgsqlConditionPB& condition);

  // The range value options if set. (possibly more than one due to IN conditions).
  std::shared_ptr<std::vector<OptionList>> range_options_;

  // Ids of columns that have range option filters such as c2 IN (1, 5, 6, 9).
  std::vector<ColumnId> range_options_indexes_;

  // Stores the number of columns involved in a range option filter.
  // For filter: A in (..) AND (C, D) in (...) AND E in (...) where A, B, C, D, E are
  // range columns, range_options_num_cols_ will contain [1, 0, 2, 2, 1]
  std::vector<size_t> range_options_num_cols_;

  // Schema of the columns to scan.
  const Schema& schema_;

  // Query ID of this scan.
  const rocksdb::QueryId query_id_;

  // The hashed_components are owned by the caller of QLScanSpec.
  const std::vector<KeyEntryValue> *hashed_components_;
  // The range_components are owned by the caller of QLScanSpec.
  const std::vector<KeyEntryValue> *range_components_;

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

  // Scan behavior.
  bool is_forward_scan_;

  DISALLOW_COPY_AND_ASSIGN(DocPgsqlScanSpec);
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_DOC_PGSQL_SCANSPEC_H
