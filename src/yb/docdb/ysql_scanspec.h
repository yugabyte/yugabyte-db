// Copyright (c) YugaByte, Inc.
//
// This file contains YSQLScanSpec that implements a YSQL scan (SELECT) specification.

#ifndef YB_DOCDB_YSQL_SCANSPEC_H
#define YB_DOCDB_YSQL_SCANSPEC_H

#include <map>

#include "yb/common/schema.h"
#include "yb/common/ysql_value.h"
#include "yb/common/ysql_protocol.pb.h"
#include "yb/docdb/doc_key.h"

namespace yb {
namespace docdb {

// A class to determine the lower/upper-bound range components of a YSQL scan from its WHERE
// condition.
class YSQLScanRange {
 public:

  // Value range of a column
  struct YSQLRange {
    YSQLValue lower_bound;
    YSQLValue upper_bound;
    YSQLRange(const YSQLValue& lower_bound, const YSQLValue& upper_bound)
        : lower_bound(lower_bound), upper_bound(upper_bound) { }
  };

  YSQLScanRange(const Schema& schema, const YSQLConditionPB& condition);

  // Return the inclusive lower and upper range values to scan. If the full range group can be
  // determined, it will be returned. Otherwise, an empty group will be returned instead.
  // TODO(robert): allow only a subset (prefix) of range components to be specified as optimization.
  vector<YSQLValue> range_values(bool lower_bound) const;

  // Return the table schema of this scan range.
  const Schema& schema() const { return schema_; }

 private:
  // Table schema being scanned.
  const Schema& schema_;
  // Mapping of column id to the column value ranges (inclusive lower/upper bounds) to scan.
  std::unordered_map<ColumnId, YSQLRange> ranges_;
};


// A scan specification for a YSQL scan. It may be used to scan either a specified doc key
// or a hash key + optional WHERE condition clause.
class YSQLScanSpec {
 public:
  // Scan for the specified doc_key.
  explicit YSQLScanSpec(const DocKey& doc_key);
  // Scan for the given hash key and a condition.
  YSQLScanSpec(
      const Schema& schema, uint32_t hash_code,
      const std::vector<PrimitiveValue>& hashed_components, const YSQLConditionPB* condition,
      size_t row_count_limit);

  // Return the inclusive lower and upper bounds of the scan.
  DocKey lower_bound() const { return range_doc_key(true /* lower_bound */); }
  DocKey upper_bound() const { return range_doc_key(false /* lower_bound */); }

  // Evaluate the WHERE condition for the given row to decide if it is selected or not.
  CHECKED_STATUS Match(const YSQLValueMap& row, bool* match) const;

  // Return the max number of rows to return.
  size_t row_count_limit() const { return row_count_limit_; }

 private:
  // Return inclusive lower/upper range doc key
  DocKey range_doc_key(bool lower_bound) const;

  // Specific doc key to scan. The doc key is owned by the caller of YSQLScanSpec.
  const DocKey* doc_key_;

  // Hash code, hashed components and optional WHERE condition clause to scan.
  // The hashed_components and condition are owned by the caller of YSQLScanSpec.
  const uint32_t hash_code_;
  const std::vector<PrimitiveValue>* hashed_components_;
  const YSQLConditionPB* condition_;

  // Max number of rows to return.
  const size_t row_count_limit_;

  // The scan range within the hash key when a WHERE condition is specified.
  const std::unique_ptr<const YSQLScanRange> range_;
};

} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_YSQL_SCANSPEC_H
