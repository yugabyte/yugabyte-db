// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOC_YQL_SCANSPEC_H
#define YB_DOCDB_DOC_YQL_SCANSPEC_H

#include "rocksdb/options.h"

#include "yb/common/yql_scanspec.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/primitive_value.h"

namespace yb {
namespace docdb {

// DocDB variant of YQL scanspec.
class DocYQLScanSpec : public common::YQLScanSpec {
 public:

  // Scan for the specified doc_key. If the doc_key specify a full primary key, the scan spec will
  // not include any static column for the primary key. If the static columns are needed, a separate
  // scan spec can be used to read just those static columns.
  DocYQLScanSpec(const Schema& schema, const DocKey& doc_key, const rocksdb::QueryId query_id);

  // Scan for the given hash key and a condition. If a start_doc_key is specified, the scan spec
  // will not include any static column for the start key. If the static columns are needed, a
  // separate scan spec can be used to read just those static columns.
  DocYQLScanSpec(const Schema& schema, int32_t hash_code, int32_t max_hash_code,
                 const std::vector<PrimitiveValue>& hashed_components, const YQLConditionPB* req,
                 const rocksdb::QueryId query_id,
                 bool include_static_columns = false, const DocKey& start_doc_key = DocKey());

  // Return the inclusive lower and upper bounds of the scan.
  CHECKED_STATUS lower_bound(DocKey* key) const {
    return GetBoundKey(true /* lower_bound */, key);
  }

  CHECKED_STATUS upper_bound(DocKey* key) const {
    return GetBoundKey(false /* upper_bound */, key);
  }

  // Create file filter based on range components.
  rocksdb::ReadFileFilter CreateFileFilter() const;

  // Gets the query id.
  const rocksdb::QueryId QueryId() const {
    return query_id_;
  }

 private:
  // Return inclusive lower/upper range doc key considering the start_doc_key.
  CHECKED_STATUS GetBoundKey(const bool lower_bound, DocKey* key) const;

  // Returns the lower/upper doc key based on the range components.
  DocKey bound_key(const bool lower_bound) const;

  // Returns the lower/upper range components of the key.
  std::vector<PrimitiveValue> range_components(const bool lower_bound,
                                               const bool allow_null) const;

  // The scan range within the hash key when a WHERE condition is specified.
  const std::unique_ptr<const common::YQLScanRange> range_;

  // Schema of the columns to scan.
  const Schema& schema_;

  // Hash code to scan at (interpreted as lower bound if hashed_components_ are empty)
  // hash values are positive int16_t, here -1 is default and means unset
  const int32_t hash_code_;

  // Max hash code to scan at (upper bound, only useful if hashed_components_ are empty)
  // hash values are positive int16_t, here -1 is default and means unset
  const int32_t max_hash_code_;

  // The hashed_components are owned by the caller of YQLScanSpec.
  const std::vector<PrimitiveValue>* hashed_components_;

  // Specific doc key to scan if not empty.
  const DocKey doc_key_;

  // Starting doc key when requested by the client.
  const DocKey start_doc_key_;

  // Lower/upper doc keys basing on the range.
  const DocKey lower_doc_key_;
  const DocKey upper_doc_key_;

  // Does the scan include static columns also?
  const bool include_static_columns_;

  // Query ID of this scan.
  const rocksdb::QueryId query_id_;
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_DOC_YQL_SCANSPEC_H
