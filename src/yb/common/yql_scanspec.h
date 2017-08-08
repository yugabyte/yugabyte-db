// Copyright (c) YugaByte, Inc.
//
// This file contains YQLScanSpec that implements a YQL scan (SELECT) specification.

#ifndef YB_COMMON_YQL_SCANSPEC_H
#define YB_COMMON_YQL_SCANSPEC_H

#include <map>

#include "yb/common/schema.h"
#include "yb/common/yql_protocol.pb.h"
#include "yb/common/yql_rowblock.h"

namespace yb {
namespace common {

// A class to determine the lower/upper-bound range components of a YQL scan from its WHERE
// condition.
class YQLScanRange {
 public:

  // Value range of a column
  struct YQLRange {
    YQLValuePB min_value;
    YQLValuePB max_value;
  };

  YQLScanRange(const Schema& schema, const YQLConditionPB& condition);

  // Return the inclusive lower and upper range values to scan.
  // If allow_null is false and the full range group can be determined, it will be returned.
  // Otherwise, an empty group will be returned instead.
  // If allow_null is true, then returned range group could contain null values.
  // TODO(robert): allow only a subset (prefix) of range components to be specified as optimization.
  std::vector<YQLValuePB> range_values(bool lower_bound, bool allow_null = false) const;

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
  virtual ~YQLScanSpec() {}

  // Scan for the given hash key and a condition.
  explicit YQLScanSpec(const YQLConditionPB* condition);

  // Evaluate the WHERE condition for the given row to decide if it is selected or not.
  // virtual to make the class polymorphic.
  virtual CHECKED_STATUS Match(const YQLTableRow& table_row, bool* match) const;

 private:
  const YQLConditionPB* condition_;
};

} // namespace common
} // namespace yb

#endif // YB_COMMON_YQL_SCANSPEC_H
