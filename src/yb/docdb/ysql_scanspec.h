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

  // Return all non-key columns referenced in the condition.
  const std::unordered_set<ColumnId>& non_key_columns() const { return non_key_columns_; }

 private:
  // Process operands. Save non-key columns and return true if there is a range column in the
  // operands. Note that this method needs to take care of the non-key column ids in the current
  // condition node only. The recursion is done in the constructor that recurses into the conditions
  // under AND, OR, NOT and merges the non-key column IDs from the constituent conditions.
  bool ProcessOperands(const google::protobuf::RepeatedPtrField<yb::YSQLExpressionPB>& operands);

  // Table schema being scanned.
  const Schema& schema_;
  // Mapping of column id to the column value ranges (inclusive lower/upper bounds) to scan.
  std::unordered_map<int32_t, YSQLRange> ranges_;
  // Non-key column ids referenced in the condition.
  std::unordered_set<ColumnId> non_key_columns_;
};


// A scan specification for a YSQL scan (SELECT).
class YSQLScanSpec {
 public:
  YSQLScanSpec(
      const Schema& schema, uint32_t hash_code,
      const std::vector<PrimitiveValue>& hashed_components, const YSQLConditionPB* condition,
      size_t row_count_limit);

  // Return the inclusive lower and upper bounds of the scan.
  DocKey lower_bound() const { return range_doc_key(true /* lower_bound */); }
  DocKey upper_bound() const { return range_doc_key(false /* lower_bound */); }

  // Evaluate the WHERE condition for the given row to decide if it is selected or not.
  CHECKED_STATUS Match(const std::unordered_map<int32_t, YSQLValue>& row, bool* match) const;

  // Return all non-key columns referenced in the condition.
  const std::unordered_set<ColumnId>* non_key_columns() const {
    return range_.get() != nullptr ? &range_->non_key_columns() : nullptr;
  }

  // Return the max number of rows to return.
  size_t row_count_limit() const { return row_count_limit_; }

 private:
  // Return inclusive lower/upper range doc key
  DocKey range_doc_key(bool lower_bound) const;

  // Hash code and hashed components of the scan.
  const uint32_t hash_code_;
  const std::vector<PrimitiveValue>& hashed_components_;

  // The WHERE condition (clause) of the scan. Can be null.
  const YSQLConditionPB* condition_;

  // Max number of rows to return.
  const size_t row_count_limit_;

  // The scan range.
  const std::unique_ptr<const YSQLScanRange> range_;
};

} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_YSQL_SCANSPEC_H
