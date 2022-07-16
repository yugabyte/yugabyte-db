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

#ifndef YB_DOCDB_DOC_QL_SCANSPEC_H
#define YB_DOCDB_DOC_QL_SCANSPEC_H

#include <functional>

#include "yb/rocksdb/options.h"

#include "yb/common/ql_scanspec.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/key_bytes.h"

namespace yb {
namespace docdb {

using Option = std::vector<KeyEntryValue>;  // an option in an IN/EQ clause
using OptionList = std::vector<Option>;     // all the options in an IN/EQ clause

// DocDB variant of QL scanspec.
class DocQLScanSpec : public QLScanSpec {
 public:

  // Scan for the specified doc_key. If the doc_key specify a full primary key, the scan spec will
  // not include any static column for the primary key. If the static columns are needed, a separate
  // scan spec can be used to read just those static columns.
  DocQLScanSpec(const Schema& schema, const DocKey& doc_key,
      const rocksdb::QueryId query_id, const bool is_forward_scan = true);

  // Scan for the given hash key and a condition. If a start_doc_key is specified, the scan spec
  // will not include any static column for the start key. If the static columns are needed, a
  // separate scan spec can be used to read just those static columns.
  //
  // Note: std::reference_wrapper is used instead of raw lvalue reference to prevent
  // temporary objects usage. The following code wont compile:
  //
  // DocQLScanSpec spec(...{} /* hashed_components */,...);

  DocQLScanSpec(const Schema& schema, boost::optional<int32_t> hash_code,
                boost::optional<int32_t> max_hash_code,
                std::reference_wrapper<const std::vector<KeyEntryValue>> hashed_components,
                const QLConditionPB* req, const QLConditionPB* if_req,
                rocksdb::QueryId query_id, bool is_forward_scan = true,
                bool include_static_columns = false,
                const DocKey& start_doc_key = DefaultStartDocKey());

  // Return the inclusive lower and upper bounds of the scan.
  Result<KeyBytes> LowerBound() const;
  Result<KeyBytes> UpperBound() const;

  // Create file filter based on range components.
  std::shared_ptr<rocksdb::ReadFileFilter> CreateFileFilter() const;

  // Gets the query id.
  const rocksdb::QueryId QueryId() const {
    return query_id_;
  }

  const std::shared_ptr<std::vector<OptionList>>& range_options() const { return range_options_; }

  bool include_static_columns() const {
    return include_static_columns_;
  }

  const QLScanRange* range_bounds() const {
    return range_bounds_.get();
  }

  const Schema* schema() const override { return &schema_; }

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

  // Initialize range_options_ if hashed_components_ is set and all range columns have one or more
  // options (i.e. using EQ/IN conditions). Otherwise range_options_ will stay null and we will
  // only use the range_bounds for scanning.
  void InitRangeOptions(const QLConditionPB& condition);

  // Returns the lower/upper doc key based on the range components.
  KeyBytes bound_key(const bool lower_bound) const;

  // Returns the lower/upper range components of the key.
  std::vector<KeyEntryValue> range_components(const bool lower_bound,
                                              std::vector<bool> *inclusivities = nullptr,
                                              bool use_strictness = true) const;

  // The scan range within the hash key when a WHERE condition is specified.
  const std::unique_ptr<const QLScanRange> range_bounds_;

  // Ids of columns that have range bounds such as c2 < 4 AND c2 >= 1.
  std::vector<ColumnId> range_bounds_indexes_;

  // Schema of the columns to scan.
  const Schema& schema_;

  // Hash code to scan at (interpreted as lower bound if hashed_components_ are empty)
  // hash values are positive int16_t.
  const boost::optional<int32_t> hash_code_;

  // Max hash code to scan at (upper bound, only useful if hashed_components_ are empty)
  // hash values are positive int16_t.
  const boost::optional<int32_t> max_hash_code_;

  // The hashed_components are owned by the caller of QLScanSpec.
  const std::vector<KeyEntryValue>* hashed_components_;

  // The range value options if set. (possibly more than one due to IN conditions).
  std::shared_ptr<std::vector<OptionList>> range_options_;

  // Ids of columns that have range option filters such as c2 IN (1, 5, 6, 9).
  std::vector<ColumnId> range_options_indexes_;

  // Stores the number of columns involved in a range option filter.
  // For filter: A in (..) AND (C, D) in (...) AND E in (...) where A, B, C, D, E are
  // range columns, range_options_num_cols_ will contain [1, 0, 2, 2, 1].
  std::vector<size_t> range_options_num_cols_;

  // Does the scan include static columns also?
  const bool include_static_columns_;

  // Specific doc key to scan if not empty.
  const KeyBytes doc_key_;

  // Starting doc key when requested by the client.
  const KeyBytes start_doc_key_;

  // Lower/upper doc keys basing on the range.
  const KeyBytes lower_doc_key_;
  const KeyBytes upper_doc_key_;

  // Query ID of this scan.
  const rocksdb::QueryId query_id_;

  DISALLOW_COPY_AND_ASSIGN(DocQLScanSpec);
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_DOC_QL_SCANSPEC_H
