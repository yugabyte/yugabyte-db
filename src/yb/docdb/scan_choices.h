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

#include "yb/qlexpr/ql_scanspec.h"

#include "yb/dockv/doc_key.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/dockv/value.h"
#include "yb/docdb/docdb_fwd.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/casts.h"

#include "yb/util/slice.h"
#include "yb/util/status_fwd.h"

namespace yb {
namespace docdb {

class ScanChoices {
 public:
  ScanChoices() = default;

  ScanChoices(const ScanChoices&) = delete;
  void operator=(const ScanChoices&) = delete;

  virtual ~ScanChoices() = default;

  // Returns false if there are still target keys we need to scan, and true if we are done.
  virtual bool Finished() const = 0;

  // Check whether scan choices is interested in specified row.
  // Seek on specified iterator to the next row of interest.
  virtual Result<bool> InterestedInRow(dockv::KeyBytes* row_key, IntentAwareIteratorIf* iter) = 0;
  virtual Result<bool> AdvanceToNextRow(dockv::KeyBytes* row_key, IntentAwareIteratorIf* iter) = 0;

  static ScanChoicesPtr Create(
      const Schema& schema, const qlexpr::YQLScanSpec& doc_spec,
      const qlexpr::ScanBounds& bounds);

  static ScanChoicesPtr CreateEmpty();
};

// This class combines the notions of option filters (col1 IN (1,2,3)) and
// singular range bound filters (col1 < 4 AND col1 >= 1) into a single notion of
// lists of options of ranges, each encoded by an OptionRange instance as
// below. So a filter for a column given in the
// Doc(QL/PGSQL)ScanSpec is converted into an OptionRange.
// In the end, each HybridScanChoices
// instance should have a sorted list of disjoint ranges for every IN/EQ clause.
// Each OptionRange has a begin_idx_ and an end_idx_. This is used to implement
// a mini run-length encoding for duplicate OptionRange values. A particular
// OptionRange with Range [lower, upper] implies that that range applies for
// all option indexes in [begin_idx_, end_idx_).
// We illustrate here why this run-encoding might be useful with an example.
// Consider an options matrix on a table with primary key (r1,r2,r3) and filters
// (r1, r3) IN ((1,3), (1,7), (5,6), (5,7), (5,9)) AND r2 IN (7,9)
// The options matrix without run compression will look something like
// r1 | [1,1]  [1,1] ([5,5]) [5,5] [5,5]
// r2 | [7,7] ([9,9])
// r3 | [3,3]  [7,7] ([6,6]) [7,7] [9,9]
// Where r1 and r3 are in a group (see the comments for HybridScanChoices::col_groups_)
// Let us say our current scan target is (5,9,6). The active options are parenthesized above.
// Let us say we wish to invoke SkipTargetsUpTo(5,9,7). In that case we must
// move the active option for r1 to the fourth option. Usually, if r1 and r3 were
// not grouped then this move to the next option for r1 would necessitate that
// the options for r2 and r3 be reset to their first options. That should not be
// the case here as the option value of r1 is not changing here even though we are
// altering the active logical option. We do not want to reset r2 here.
// We see that the given target is satisfied by the currently active r2 option so we don't mess
// with that.
// Furthermore, when selecting the appropriate option for r3 we can only consider
// options that preserve the active option values for r1. That corresponds to the options between
// the third [6,6] and fifth [9,9]. If r3 was not in a group then we
// could have considered all the available r3 options as valid active candidates.
// This results in the following options matrix:
// r1 | [1,1]  [1,1] [5,5] ([5,5]) [5,5]
// r2 | [7,7] ([9,9])
// r3 | [3,3]  [7,7] [6,6] ([7,7]) [9,9]

// So there were two mechanics of note here:
// 1) We need to be able to identify when we're moving the active option for a key expression
// to another option that's identical. In these cases, we shouldn't reset the
// following key expression active options.
// 2) If a certain key expression is in a group then we need to be able to identify what the valid
// range of options we can consider is.
// Say we run-compressed the initial options matrix as follows by encoding
// each option into option "ranges". Each entry now has an option value and
// an affixed range to show which logical options this option value corresponds to:
// r1 | [1,1](r:[0,2)) ([5,5](r:[2,5)))
// r2 | [7,7](r:[0,1)) ([9,9](r:[1,2)))
// r3 | [3,3](r:[0,1)) [7,7](r:[1,2)) ([6,6](r:[2,3))) [7,7](r:[3,4)) [9,9](r:[4,5))

// Now each option range can contain a range of logical options. When moving from
// one logical option to another we can check to see if
// we are in the same option range to satisfy mechanic (1).
// In order to carry out (2) to find a valid set of option ranges to consider for r3,
// we can look at the currently active option range for the predecessor in r3's group, r1.
// That would be ([5,5](r:[2,5))) in this case.
// This tells us we can only consider logical options from index 2 to 4.
// This range is determined by GetSearchSpaceLowerBound and GetSearchSpaceUpperBound.
// Right now this supports a conjunction of range bound and discrete filters.
// Disjunctions are also supported but are UNTESTED.
// TODO: Test disjunctions when YSQL and YQL support pushing those down
// The lower and upper values are vectors to incorporate multi-column IN clause.
class OptionRange {
 public:
  OptionRange(
      dockv::KeyEntryValue lower, bool lower_inclusive,
      dockv::KeyEntryValue upper, bool upper_inclusive)
      : OptionRange(std::move(lower),
                    lower_inclusive,
                    std::move(upper),
                    upper_inclusive,
                    0 /* begin_idx_ */,
                    0 /* end_idx_ */) {}

  OptionRange(
      dockv::KeyEntryValue lower, bool lower_inclusive,
      dockv::KeyEntryValue upper, bool upper_inclusive,
      size_t begin_idx, size_t end_idx)
      : lower_(std::move(lower)),
        lower_inclusive_(lower_inclusive),
        upper_(std::move(upper)),
        upper_inclusive_(upper_inclusive),
        begin_idx_(begin_idx),
        end_idx_(end_idx) {}

  // Convenience constructors for testing
  OptionRange(int begin, int end, SortOrder sort_order = SortOrder::kAscending)
      : OptionRange(
            {dockv::KeyEntryValue::Int32(begin, sort_order)}, true,
            {dockv::KeyEntryValue::Int32(end, sort_order)}, true) {}

  OptionRange(int value, SortOrder sort_order = SortOrder::kAscending) // NOLINT
      : OptionRange(value, value, sort_order) {}

  OptionRange(int bound, bool upper, SortOrder sort_order = SortOrder::kAscending)
      : OptionRange(
            {upper ? dockv::KeyEntryValue(dockv::KeyEntryType::kNullLow)
                   : dockv::KeyEntryValue::Int32(bound, sort_order)},
            !upper,
            {upper ? dockv::KeyEntryValue::Int32(bound, sort_order)
                   : dockv::KeyEntryValue(dockv::KeyEntryType::kNullHigh)},
            upper) {}
  OptionRange()
      : OptionRange(
            {dockv::KeyEntryValue(dockv::KeyEntryType::kLowest)},
            false,
            {dockv::KeyEntryValue(dockv::KeyEntryType::kHighest)},
            false) {}

  const dockv::KeyEntryValue& lower() const { return lower_; }
  bool lower_inclusive() const { return lower_inclusive_; }
  const dockv::KeyEntryValue& upper() const { return upper_; }
  bool upper_inclusive() const { return upper_inclusive_; }

  size_t begin_idx() const { return begin_idx_; }
  size_t end_idx() const { return end_idx_; }

  bool HasIndex(size_t opt_index) const { return begin_idx_ <= opt_index && opt_index < end_idx_; }

  static bool upper_lt(const OptionRange& range1, const OptionRange& range2) {
    return range1.upper_ < range2.upper_;
  }

  static bool lower_gt(const OptionRange& range1, const OptionRange& range2) {
    return range1.lower_ > range2.lower_;
  }

  static bool end_idx_leq(const OptionRange& range1, const OptionRange& range2) {
    return range1.end_idx_ <= range2.end_idx_;
  }

  std::string ToString() const;

 private:
  dockv::KeyEntryValue lower_;
  bool lower_inclusive_;
  dockv::KeyEntryValue upper_;
  bool upper_inclusive_;

  size_t begin_idx_;
  size_t end_idx_;

  friend class ScanChoicesTest;
  bool operator==(const OptionRange& other) const {
    return lower_inclusive() == other.lower_inclusive() &&
           upper_inclusive() == other.upper_inclusive() && lower() == other.lower() &&
           upper() == other.upper();
  }
};

inline std::ostream& operator<<(std::ostream& str, const OptionRange& opt) {
  return str << opt.ToString();
}

class HybridScanChoices : public ScanChoices {
 public:
  // Constructs a list of ranges for each IN/EQ clause from the given scanspec.
  // A filter of the form col1 IN (1,2) is converted to col1 IN ([[1], [1]], [[2], [2]]).
  // And filter of the form (col2, col3) IN ((3,4), (5,6)) is converted to
  // (col2, col3) IN ([[3, 4], [3, 4]], [[5, 6], [5, 6]]).

  HybridScanChoices(
      const Schema& schema,
      const dockv::KeyBytes& lower_doc_key,
      const dockv::KeyBytes& upper_doc_key,
      bool is_forward_scan,
      const std::vector<ColumnId>& options_col_ids,
      const std::shared_ptr<std::vector<qlexpr::OptionList>>& options,
      const qlexpr::QLScanRange* range_bounds,
      const ColGroupHolder& col_groups,
      const size_t prefix_length);

  HybridScanChoices(
      const Schema& schema,
      const qlexpr::YQLScanSpec& doc_spec,
      const dockv::KeyBytes& lower_doc_key,
      const dockv::KeyBytes& upper_doc_key);

  bool Finished() const override {
    return finished_;
  }

  Result<bool> InterestedInRow(dockv::KeyBytes* row_key, IntentAwareIteratorIf* iter) override;
  Result<bool> AdvanceToNextRow(dockv::KeyBytes* row_key, IntentAwareIteratorIf* iter) override;

 private:
  friend class ScanChoicesTest;

  // Sets current_scan_target_ to the first tuple in the filter space that is >= new_target.
  Result<bool> SkipTargetsUpTo(Slice new_target);
  Result<bool> DoneWithCurrentTarget();
  void SeekToCurrentTarget(IntentAwareIteratorIf* db_iter);

  bool CurrentTargetMatchesKey(Slice curr);

  // Append KeyEntryValue to target. After every append, we need to check if it is the last hash key
  // column. Subsequently, we need to add a kGroundEnd after that if it is the last hash key column.
  // Hence, appending to scan target should always be done using this function.
  void AppendToScanTarget(const dockv::KeyEntryValue& target, size_t col_idx);
  void AppendInfToScanTarget(size_t col_idx);

  // Utility function for (multi)key scans. Updates the target scan key by
  // incrementing the option index for an OptionList. Will handle overflow by setting current
  // index to 0 and incrementing the previous index instead. If it overflows at first index
  // it means we are done, so it clears the scan target idxs array.
  Status IncrementScanTargetAtOptionList(ssize_t start_option_list_idx);

  // Utility function for testing
  std::vector<OptionRange> TEST_GetCurrentOptions();
  Result<bool> ValidateHashGroup(const dockv::KeyBytes& scan_target) const;

  // Utility method to return a column corresponding to idx in the schema.
  // This may be different from schema.column_id in the presence of the hash_code column.
  ColumnId GetColumnId(const Schema& schema, size_t idx) const;

  Status DecodeKey(
      dockv::DocKeyDecoder* decoder, dockv::KeyEntryValue* target_value = nullptr) const;

  // Returns an iterator reference to the lowest option in the current search
  // space of this option list index. See comment for OptionRange.
  std::vector<OptionRange>::const_iterator GetSearchSpaceLowerBound(size_t opt_list_idx) const;

  // Returns an iterator reference to the exclusive highest option range in the current search
  // space of this option list index. See comment for OptionRange.
  std::vector<OptionRange>::const_iterator GetSearchSpaceUpperBound(size_t opt_list_idx) const;

  // Gets the option range that corresponds to the given option index at the given
  // option list index.
  std::vector<OptionRange>::const_iterator GetOptAtIndex(size_t opt_list_idx,
                                                         size_t opt_index) const;

  // Sets the option that corresponds to the given option index at the given
  // logical option list index.
  void SetOptToIndex(size_t opt_list_idx, size_t opt_index);

  // Sets an entire group to a particular logical option index.
  void SetGroup(size_t opt_list_idx, size_t opt_index);

  const bool is_forward_scan_;
  dockv::KeyBytes current_scan_target_;
  bool finished_ = false;
  bool has_hash_columns_ = false;
  size_t num_hash_cols_;

  // True if CurrentTargetMatchesKey should return true all the time as
  // the filter this ScanChoices iterates over is trivial.
  bool is_trivial_filter_ = false;

  dockv::KeyBytes prev_scan_target_;

  // The following fields aid in the goal of iterating through all possible
  // scan key values based on given IN-lists and range filters.

  // The following encodes the list of option we are iterating over
  std::vector<std::vector<OptionRange>> scan_options_;

  // Vector of references to currently active elements being used
  // in range_cols_scan_options_
  // current_scan_target_ranges_[i] gives us the current OptionRange
  // column i is iterating over of the elements in
  // range_cols_scan_options_[i]
  mutable std::vector<std::vector<OptionRange>::const_iterator> current_scan_target_ranges_;

  bool is_options_done_ = false;

  const dockv::KeyBytes lower_doc_key_;
  const dockv::KeyBytes upper_doc_key_;

  // When we have tuple IN filters such as (r1,r3) IN ((1,3), (2,5) ...) where
  // (r1,r2,r3) is the index key, we cannot simply just populate
  // range_cols_scan_options_ with the values r1 -> {[1,1], [2,2]...} and
  // r3 -> {[3,3], [5,5]...}. This by itself implies that (r1,r3) = (1,5) is allowed.
  // In order to account for this case, we must introduce the possiblity of correlations amongst
  // key expressions. In order to achieve this, we put key expressions into "groups".
  // In the above example, we must put r1 and r3 into one such group and r2 into another by itself.
  // col_groups_ holds these groups. We maintain that all the key expressions in a group.
  // are on the same option index at any given time.
  // For example: Say (r1,r2,r3) is the index key.
  // If we received a filter of the form (r1,r3) IN ((1,3), (2,5)) AND r2 IN (4,6)
  // We would have groups [[0,2], [1]]
  // The scan keys we would iterate over would be
  // (1,4,3), (1,6,3), (2,4,3), (2,6,3)
  ColGroupHolder col_groups_;

  size_t prefix_length_ = 0;

  size_t schema_num_keys_;
};

}  // namespace docdb
}  // namespace yb
