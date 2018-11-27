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

#include "yb/common/ql_scanspec.h"
#include "yb/rocksdb/options.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/primitive_value.h"

namespace yb {
namespace docdb {

// DocDB variant of scanspec.
class DocPgsqlScanSpec : public common::PgsqlScanSpec {
 public:

  // Scan for the specified doc_key.
  DocPgsqlScanSpec(const Schema& schema,
                   const rocksdb::QueryId query_id,
                   const DocKey& doc_key,
                   bool is_forward_scan = true);

  // Scan for the given hash key, a condition, and optional doc_key.
  DocPgsqlScanSpec(const Schema& schema,
                   const rocksdb::QueryId query_id,
                   const std::vector<PrimitiveValue>& hashed_components,
                   boost::optional<int32_t> hash_code,
                   boost::optional<int32_t> max_hash_code,
                   const PgsqlExpressionPB *where_expr,
                   const DocKey& start_doc_key = DocKey(),
                   bool is_forward_scan = true);

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

  CHECKED_STATUS lower_bound(DocKey* key) const {
    return GetBoundKey(true /* lower_bound */, key);
  }

  CHECKED_STATUS upper_bound(DocKey* key) const {
    return GetBoundKey(false /* upper_bound */, key);
  }

  // Return inclusive lower/upper range doc key considering the start_doc_key.
  CHECKED_STATUS GetBoundKey(const bool lower_bound, DocKey* key) const;

  // Returns the lower/upper range components of the key.
  std::vector<PrimitiveValue> range_components(const bool lower_bound) const;

 private:
  // Returns the lower/upper doc key based on the range components.
  DocKey bound_key(const Schema& schema, const bool lower_bound) const;

  // Query ID of this scan.
  const rocksdb::QueryId query_id_;

  // The hashed_components are owned by the caller of QLScanSpec.
  const std::vector<PrimitiveValue> *hashed_components_;

  // Hash code is used if hashed_components_ vector is empty.
  // hash values are positive int16_t.
  const boost::optional<int32_t> hash_code_;

  // Max hash code is used if hashed_components_ vector is empty.
  // hash values are positive int16_t.
  const boost::optional<int32_t> max_hash_code_;

  // Specific doc key to scan if not empty.
  const DocKey doc_key_;

  // Starting doc key when requested by the client.
  const DocKey start_doc_key_;

  // Lower and upper keys for range condition.
  const DocKey lower_doc_key_;
  const DocKey upper_doc_key_;

  // Scan behavior.
  bool is_forward_scan_;
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_DOC_PGSQL_SCANSPEC_H
