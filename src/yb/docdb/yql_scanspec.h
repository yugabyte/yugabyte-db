// Copyright (c) YugaByte, Inc.
//
// This file contains YQLScanSpec that implements a YQL scan (SELECT) specification.

#ifndef YB_DOCDB_YQL_SCANSPEC_H
#define YB_DOCDB_YQL_SCANSPEC_H

#include <map>

#include "yb/common/schema.h"
#include "yb/common/yql_value.h"
#include "yb/common/yql_protocol.pb.h"
#include "yb/docdb/doc_key.h"

namespace yb {
namespace docdb {

// A class to determine the lower/upper-bound range components of a YQL scan from its WHERE
// condition.
class YQLScanRange {
 public:

  // Value range of a column
  struct YQLRange {
    YQLValuePB lower_bound;
    YQLValuePB upper_bound;
  };

  YQLScanRange(const Schema& schema, const YQLConditionPB& condition);

  // Return the inclusive lower and upper range values to scan. If the full range group can be
  // determined, it will be returned. Otherwise, an empty group will be returned instead.
  // TODO(robert): allow only a subset (prefix) of range components to be specified as optimization.
  vector<YQLValuePB> range_values(bool lower_bound) const;

 private:
  // Table schema being scanned.
  const Schema& schema_;
  // Mapping of column id to the column value ranges (inclusive lower/upper bounds) to scan.
  std::unordered_map<ColumnId, YQLRange> ranges_;
};


// A scan specification for a YQL scan. It may be used to scan either a specified doc key
// or a hash key + optional WHERE condition clause.
class YQLScanSpec {
 public:
  // Scan for the specified doc_key.
  explicit YQLScanSpec(const Schema& schema, const DocKey& doc_key);
  // Scan for the given hash key and a condition.
  YQLScanSpec(
      const Schema& schema, uint32_t hash_code,
      const std::vector<PrimitiveValue>& hashed_components, const YQLConditionPB* condition,
      size_t row_count_limit,
      const DocKey& start_doc_key = DocKey());

  // Return the inclusive lower and upper bounds of the scan.
  CHECKED_STATUS lower_bound(DocKey* key) const {
    return GetBoundKey(true /* lower_bound */, key);
  }

  CHECKED_STATUS upper_bound(DocKey* key) const  {
    return GetBoundKey(false /* upper_bound */, key);
  }

  // Evaluate the WHERE condition for the given row to decide if it is selected or not.
  CHECKED_STATUS Match(const YQLValueMap& row, bool* match) const;

  // Return the max number of rows to return.
  size_t row_count_limit() const { return row_count_limit_; }

 private:
  // Return inclusive lower/upper range doc key considering the start_doc_key.
  CHECKED_STATUS GetBoundKey(const bool lower_bound, DocKey* key) const;

  // Returns the lower/upper doc key based on the range components.
  DocKey bound_key(const bool lower_bound) const;

  // Schema of the columns to scan.
  const Schema& schema_;

  // Specific doc key to scan. The doc key is owned by the caller of YQLScanSpec.
  const DocKey* doc_key_;

  // Starting doc key when requested by the client.
  const DocKey start_doc_key_;

  // Hash code, hashed components and optional WHERE condition clause to scan.
  // The hashed_components and condition are owned by the caller of YQLScanSpec.
  const uint32_t hash_code_;
  const std::vector<PrimitiveValue>* hashed_components_;
  const YQLConditionPB* condition_;

  // Max number of rows to return.
  const size_t row_count_limit_;

  // The scan range within the hash key when a WHERE condition is specified.
  const std::unique_ptr<const YQLScanRange> range_;

  const DocKey lower_doc_key_;
  const DocKey upper_doc_key_;
};

} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_YQL_SCANSPEC_H
