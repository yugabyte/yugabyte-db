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

#include "yb/rocksdb/options.h"

#include "yb/qlexpr/ql_scanspec.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/dockv/key_bytes.h"

#include "yb/util/col_group.h"

namespace yb {
namespace docdb {

// DocDB variant of QL scanspec.
class DocQLScanSpec : public qlexpr::QLScanSpec {
 public:

  // Scan for the specified doc_key. If the doc_key specify a full primary key, the scan spec will
  // not include any static column for the primary key. If the static columns are needed, a separate
  // scan spec can be used to read just those static columns.
  DocQLScanSpec(const Schema& schema, const dockv::DocKey& doc_key, const rocksdb::QueryId query_id,
      const bool is_forward_scan = true, const size_t prefix_length = 0);

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
                std::reference_wrapper<const dockv::KeyEntryValues> hashed_components,
                const QLConditionPB* req, const QLConditionPB* if_req,
                rocksdb::QueryId query_id, bool is_forward_scan = true,
                bool include_static_columns = false,
                const dockv::DocKey& start_doc_key = DefaultStartDocKey(),
                const size_t prefix_length = 0);

  const std::shared_ptr<std::vector<qlexpr::OptionList>>& options() const override {
    return options_;
  }

  bool include_static_columns() const {
    return include_static_columns_;
  }

  const std::vector<ColumnId>& options_indexes() const override { return options_col_ids_; }

  const ColGroupHolder& options_groups() const override { return options_groups_; }

 private:
  static const dockv::DocKey& DefaultStartDocKey();

  // Complete initialising bounds used by this scan spec.
  void CompleteBounds();

  // Initialize options_ if range columns have one or more options (i.e. using EQ/IN
  // conditions). Otherwise options_ will stay null and we will only use the range_bounds for
  // scanning.
  void InitOptions(const QLConditionPB& condition);

  // Returns the lower/upper doc key based on the range components.
  dockv::KeyBytes bound_key(const bool lower_bound) const;

  // Returns the lower/upper range components of the key.
  dockv::KeyEntryValues RangeComponents(
      bool lower_bound,
      std::vector<bool>* inclusivities = nullptr) const override;

  // Hash code to scan at (interpreted as lower bound if hashed_components_ are empty)
  // hash values are positive int16_t.
  const boost::optional<int32_t> hash_code_;

  // Max hash code to scan at (upper bound, only useful if hashed_components_ are empty)
  // hash values are positive int16_t.
  const boost::optional<int32_t> max_hash_code_;

  // The hashed_components are owned by the caller of QLScanSpec.
  const dockv::KeyEntryValues* hashed_components_;

  // The range/hash value options if set (possibly more than one due to IN conditions).
  std::shared_ptr<std::vector<qlexpr::OptionList>> options_;

  // Ids of key columns that have filters such as h1 IN (1, 5, 6, 9) or r1 IN (5, 6, 7)
  std::vector<ColumnId> options_col_ids_;

  // Groups of column indexes found from the filters.
  // Eg: If we had an incoming filter of the form (r1, r3, r4) IN ((1,2,5), (5,4,3), ...)
  // AND r2 <= 5
  // where (r1,r2,r3,r4) is the primary key of this table, then
  // options_groups_ would contain the groups {0,2,3} and {1}.
  ColGroupHolder options_groups_;

  // Does the scan include static columns also?
  const bool include_static_columns_;

  // Specific doc key to scan if not empty.
  const dockv::KeyBytes doc_key_;

  // Starting doc key when requested by the client.
  const dockv::KeyBytes start_doc_key_;

  DISALLOW_COPY_AND_ASSIGN(DocQLScanSpec);
};

}  // namespace docdb
}  // namespace yb
