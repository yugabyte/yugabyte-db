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
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/functional/hash.hpp>
#include <boost/optional/optional.hpp>

#include "yb/common/common_fwd.h"
#include "yb/common/column_id.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/schema.h"
#include "yb/common/value.pb.h"

#include "yb/dockv/dockv_fwd.h"
#include "yb/dockv/key_bytes.h"

#include "yb/qlexpr/qlexpr_fwd.h"

#include "yb/util/col_group.h"

#include "yb/rocksdb/options.h"

namespace yb::qlexpr {

using Option = dockv::KeyEntryValue;            // an option in an IN/EQ clause
using OptionList = std::vector<Option>;  // all the options in an IN/EQ clause

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
    bool is_lower_bound() const { return is_lower_bound_; }

    bool operator<(const QLBound& other) const;
    bool operator>(const QLBound& other) const;
    bool operator==(const QLBound& other) const;

    std::string ToString() const;

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
    // Set to true only for an IS NOT NULL query, in which case the bounds (min_bound and max_bound)
    // are not set.
    bool is_not_null = false;

    bool Active() const {
      return min_bound || max_bound || is_not_null;
    }

    std::string ToString() const;
  };

  QLScanRange(const Schema& schema, const QLConditionPB& condition);
  QLScanRange(const Schema& schema, const PgsqlConditionPB& condition);
  QLScanRange(const Schema& schema, const LWPgsqlConditionPB& condition);

  QLRange RangeFor(ColumnId col_id) const {
    const auto& iter = ranges_.find(col_id);
    return (iter == ranges_.end() ? QLRange() : iter->second);
  }

  bool column_has_range_bound(ColumnId col_id) const { return ranges_.count(col_id); }

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

  std::string ToString() const;

 private:
  template <class Cond>
  void Init(const Schema& schema, const Cond& cond);

  // Mapping of column id to the column value ranges (inclusive lower/upper bounds) to scan.
  std::unordered_map<ColumnId, QLRange, boost::hash<ColumnId>> ranges_;

  // Whether the condition has an IN condition on a range (clustering) column.
  // Used in doc_ql_scanspec to try to construct the set of options for a multi-point scan.
  bool has_in_range_options_ = false;
  bool has_in_hash_options_ = false;

  DISALLOW_COPY_AND_ASSIGN(QLScanRange);
};

struct ScanBounds {
  dockv::KeyBytes lower;
  dockv::KeyBytes upper;
  bool trivial = false;
};

//--------------------------------------------------------------------------------------------------
// YQL Scanspec.
// This class represents all scan specifications.
//--------------------------------------------------------------------------------------------------
class YQLScanSpec {
 public:
  YQLScanSpec(
      QLClient client_type,
      const Schema& schema,
      bool is_forward_scan,
      rocksdb::QueryId query_id,
      std::unique_ptr<const QLScanRange>
          range_bounds,
      size_t prefix_length)
      : client_type_(client_type),
        schema_(schema),
        is_forward_scan_(is_forward_scan),
        query_id_(query_id),
        range_bounds_(std::move(range_bounds)),
        prefix_length_(prefix_length) {}

  virtual ~YQLScanSpec() {}

  QLClient client_type() const { return client_type_; }

  // for a particular column, give a column id, this function tries to determine its sorting type
  SortingType get_sorting_type(size_t col_idx) {
    return col_idx == kYbHashCodeColId || schema().is_hash_key_column(col_idx)
               ? SortingType::kAscending
               : schema().column(col_idx).sorting_type();
  }

  // for a particular column, given a column id, this function tries to determine if it is
  // a forward scan or a reverse scan
  bool get_scan_direction(size_t col_idx) {
    auto sorting_type = get_sorting_type(col_idx);
    return is_forward_scan_ ^ (sorting_type == SortingType::kAscending ||
                               sorting_type == SortingType::kAscendingNullsLast);
  }

  // Returns scan order.
  bool is_forward_scan() const { return is_forward_scan_; }

  // Table schema being scanned.
  const Schema& schema() const { return schema_; }

  // Query of this scan.
  rocksdb::QueryId QueryId() const { return query_id_; }

  // Returns the range bounds of the individual columns present for this scan.
  const QLScanRange* range_bounds() const { return range_bounds_.get(); }

  // Return the inclusive lower and upper bounds of the scan.
  const ScanBounds& bounds() const {
    return bounds_;
  }

  // Used by distinct operator, default value is 0.
  size_t prefix_length() const { return prefix_length_; }

  // Returns list of options passed for this scan.
  virtual const std::shared_ptr<std::vector<OptionList>>& options() const = 0;

  // Column ids mapping for the options.
  virtual const std::vector<ColumnId>& options_indexes() const = 0;

  // Group details of different options.
  virtual const ColGroupHolder& options_groups() const = 0;

  // Returns the lower/upper range components of the key.
  virtual dockv::KeyEntryValues RangeComponents(
      const bool lower_bound,
      std::vector<bool>* inclusivities = nullptr) const = 0;

 protected:
  // Lower and upper keys for range condition.
  ScanBounds bounds_;

 private:
  // QLClient type of this scan.
  const QLClient client_type_;

  // Table schema being scanned.
  const Schema& schema_;

  // Scan order - forward/backward.
  const bool is_forward_scan_;

  // Query ID of this scan.
  const rocksdb::QueryId query_id_;

  // The scan range within the hash key when a WHERE condition is specified.
  const std::unique_ptr<const QLScanRange> range_bounds_;

  // Used by distinct query operator, default value is 0.
  const size_t prefix_length_;
};

//--------------------------------------------------------------------------------------------------
// CQL Support.
//--------------------------------------------------------------------------------------------------

// A scan specification for a QL scan. It may be used to scan either a specified doc key
// or a hash key + optional WHERE condition clause.
class QLScanSpec : public YQLScanSpec {
 public:
  QLScanSpec(
      const Schema& schema,
      bool is_forward_scan,
      rocksdb::QueryId query_id,
      std::unique_ptr<const QLScanRange>
          range_bounds,
      size_t prefix_length,
      QLExprExecutorPtr executor = nullptr);

  // Scan for the given hash key and a condition.
  QLScanSpec(
      const Schema& schema,
      bool is_forward_scan,
      rocksdb::QueryId query_id,
      std::unique_ptr<const QLScanRange>
          range_bounds,
      size_t prefix_length,
      const QLConditionPB* condition,
      const QLConditionPB* if_condition,
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
  PgsqlScanSpec(
      const Schema& schema,
      bool is_forward_scan,
      rocksdb::QueryId query_id,
      std::unique_ptr<const QLScanRange> range_bounds,
      size_t prefix_length);
};

using ColumnListVector = std::vector<int>;

std::vector<const QLValuePB*> GetTuplesSortedByOrdering(
    const QLSeqValuePB& options, const Schema& schema, bool is_forward_scan,
    const ColumnListVector& col_idxs);

}  // namespace yb::qlexpr
