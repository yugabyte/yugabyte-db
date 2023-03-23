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
//
// This file contains QLScanSpec that implements a QL scan (SELECT) specification.

#pragma once

#include <map>

#include <boost/functional/hash.hpp>
#include <boost/optional/optional.hpp>

#include "yb/common/common_fwd.h"
#include "yb/common/column_id.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/schema.h"
#include "yb/common/value.pb.h"

namespace yb {

//--------------------------------------------------------------------------------------------------
// YQL Scanspec.
// This class represents all scan specifications.
//--------------------------------------------------------------------------------------------------
class YQLScanSpec {
 public:
  YQLScanSpec(QLClient client_type, const Schema& schema, bool is_forward_scan)
      : schema_(schema), is_forward_scan_(is_forward_scan), client_type_(client_type) {
  }

  virtual ~YQLScanSpec() {
  }

  QLClient client_type() const {
    return client_type_;
  }

  // for a particular column, give a column id, this function tries to determine its sorting type
  SortingType get_sorting_type(size_t col_idx) {
    return col_idx == kYbHashCodeColId || schema().is_hash_key_column(col_idx) ?
      SortingType::kAscending : schema().column(col_idx).sorting_type();
  }

  // for a particular column, given a column id, this function tries to determine if it is
  // a forward scan or a reverse scan
  bool get_scan_direction(size_t col_idx) {
    auto sorting_type = get_sorting_type(col_idx);
    return is_forward_scan_ ^ (sorting_type == SortingType::kAscending ||
      sorting_type == SortingType::kAscendingNullsLast);
  }

  bool is_forward_scan() const {
    return is_forward_scan_;
  }

  const Schema& schema() const { return schema_; }


 private:
  const Schema& schema_;
  const bool is_forward_scan_;
  const QLClient client_type_;
};

//--------------------------------------------------------------------------------------------------
// CQL Support.
//--------------------------------------------------------------------------------------------------

// A class to determine the lower/upper-bound range components of a QL scan from its WHERE
// condition.
class QLScanRange {
 public:

  // Bound of a range specification
  // This class includes information about the value
  // and inclusiveness of a bound.
  class QLBound {

   public:
    const QLValuePB& GetValue() const { return value_; }
    bool IsInclusive() const { return is_inclusive_; }
    bool operator<(const QLBound& other) const;
    bool operator>(const QLBound& other) const;
    bool operator==(const QLBound& other) const;

   protected:
    QLBound(const QLValuePB& value, bool is_inclusive, bool is_lower_bound);
    QLBound(const LWQLValuePB& value, bool is_inclusive, bool is_lower_bound);

    QLValuePB value_;
    bool is_inclusive_ = true;
    bool is_lower_bound_;
  };

  // Upper bound class
  class QLUpperBound : public QLBound {
   public:
    QLUpperBound(const QLValuePB& value, bool is_inclusive)
        : QLBound(value, is_inclusive, false) {}

    QLUpperBound(const LWQLValuePB& value, bool is_inclusive)
        : QLBound(value, is_inclusive, false) {}
  };

  // Lower bound class
  class QLLowerBound : public QLBound {
   public:
    QLLowerBound(const QLValuePB& value, bool is_inclusive)
        : QLBound(value, is_inclusive, true) {}

    QLLowerBound(const LWQLValuePB& value, bool is_inclusive)
        : QLBound(value, is_inclusive, true) {}
  };

  // Range filter specification on a column
  struct QLRange {
    boost::optional<QLBound> min_bound;
    boost::optional<QLBound> max_bound;
  };

  QLScanRange(const Schema& schema, const QLConditionPB& condition);
  QLScanRange(const Schema& schema, const PgsqlConditionPB& condition);
  QLScanRange(const Schema& schema, const LWPgsqlConditionPB& condition);

  QLRange RangeFor(ColumnId col_id) const {
    const auto& iter = ranges_.find(col_id);
    return (iter == ranges_.end() ? QLRange() : iter->second);
  }

  std::vector<ColumnId> GetColIds() const {
    std::vector<ColumnId> col_id_list;
    for (auto& it : ranges_) {
      col_id_list.push_back(it.first);
    }
    return col_id_list;
  }

  bool has_in_hash_options() const {
    return has_in_hash_options_;
  }

  bool has_in_range_options() const {
    return has_in_range_options_;
  }

  // Intersect / union / complement operators.
  QLScanRange& operator&=(const QLScanRange& other);
  QLScanRange& operator|=(const QLScanRange& other);
  QLScanRange& operator~();

  QLScanRange& operator=(QLScanRange&& other);

 private:
  template <class Cond>
  void Init(const Cond& cond);

  // Table schema being scanned.
  const Schema& schema_;

  // Mapping of column id to the column value ranges (inclusive lower/upper bounds) to scan.
  std::unordered_map<ColumnId, QLRange, boost::hash<ColumnId>> ranges_;

  // Whether the condition has an IN condition on a range (clustering) column.
  // Used in doc_ql_scanspec to try to construct the set of options for a multi-point scan.
  bool has_in_range_options_ = false;
  bool has_in_hash_options_ = false;
};

// A scan specification for a QL scan. It may be used to scan either a specified doc key
// or a hash key + optional WHERE condition clause.
class QLScanSpec : public YQLScanSpec {
 public:
  explicit QLScanSpec(const Schema& schema,
                      QLExprExecutorPtr executor = nullptr);

  // Scan for the given hash key and a condition.
  QLScanSpec(const Schema& schema,
             const QLConditionPB* condition,
             const QLConditionPB* if_condition,
             const bool is_forward_scan,
             QLExprExecutorPtr executor = nullptr);

  virtual ~QLScanSpec() {}

  // Evaluate the WHERE condition for the given row to decide if it is selected or not.
  // virtual to make the class polymorphic.
  virtual Status Match(const QLTableRow& table_row, bool* match) const;

 protected:
  const QLConditionPB* condition_;
  const QLConditionPB* if_condition_;
  QLExprExecutorPtr executor_;
};

//--------------------------------------------------------------------------------------------------
// PostgreSQL Support.
//--------------------------------------------------------------------------------------------------
class PgsqlScanSpec : public YQLScanSpec {
 public:
  typedef std::unique_ptr<PgsqlScanSpec> UniPtr;

  explicit PgsqlScanSpec(const Schema& schema,
                         const bool is_forward_scan,
                         const PgsqlExpressionPB* where_expr,
                         QLExprExecutorPtr executor = nullptr);

  virtual ~PgsqlScanSpec();

  const PgsqlExpressionPB* where_expr() {
    return where_expr_;
  }

 protected:
  const PgsqlExpressionPB* where_expr_;
  QLExprExecutorPtr executor_;
};

} // namespace yb
