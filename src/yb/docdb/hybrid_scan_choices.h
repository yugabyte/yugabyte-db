// Copyright (c) YugabyteDB, Inc.
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

#include "yb/common/doc_hybrid_time.h"

#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/scan_choices.h"

#include "yb/dockv/value_type.h"

#include "yb/qlexpr/ql_scanspec.h"

#include "yb/util/algorithm_util.h"

namespace yb::docdb {

YB_STRONGLY_TYPED_BOOL(AddInfinity);

YB_DEFINE_ENUM(BoundComp, (kOut)(kIn)(kAtBound));

struct RangeMatch {
  BoundComp lower;
  BoundComp upper;

  BoundComp Last(bool is_forward) const {
    return is_forward ? upper : lower;
  }
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
      Slice lower, bool lower_inclusive,
      Slice upper, bool upper_inclusive,
      size_t begin_idx, size_t end_idx)
      : lower_(lower),
        lower_inclusive_(lower_inclusive),
        upper_(upper),
        upper_inclusive_(upper_inclusive),
        begin_idx_(begin_idx),
        end_idx_(end_idx),
        fixed_point_(lower_inclusive && upper_inclusive && lower == upper) {}

  OptionRange(
      Slice single, size_t begin_idx, size_t end_idx)
      : lower_(single),
        lower_inclusive_(true),
        upper_(single),
        upper_inclusive_(true),
        begin_idx_(begin_idx),
        end_idx_(end_idx),
        fixed_point_(true) {}

  Slice lower() const { return lower_; }
  bool lower_inclusive() const { return lower_inclusive_; }
  Slice upper() const { return upper_; }
  bool upper_inclusive() const { return upper_inclusive_; }

  bool fixed_point() const {
    return fixed_point_;
  }

  Slice first_bound(bool is_forward) const {
    return is_forward ? lower_ : upper_;
  }

  bool first_bound_inclusive(bool is_forward) const {
    return is_forward ? lower_inclusive_ : upper_inclusive_;
  }

  Slice last_bound(bool is_forward) const {
    return is_forward ? upper_ : lower_;
  }

  size_t first_idx(bool is_forward) const {
    return is_forward ? begin_idx_ : end_idx_ - 1;
  }

  size_t begin_idx() const { return begin_idx_; }
  size_t end_idx() const { return end_idx_; }

  bool HasIndex(size_t opt_index) const { return begin_idx_ <= opt_index && opt_index < end_idx_; }

  bool Match(Slice value) const;
  RangeMatch MatchEx(Slice value) const;
  bool SatisfiesInclusivity(RangeMatch match) const;

  bool Fixed() const;

  std::string ToString() const;

 private:
  Slice lower_;
  bool lower_inclusive_;
  Slice upper_;
  bool upper_inclusive_;
  size_t begin_idx_;
  size_t end_idx_;
  bool fixed_point_;
};

inline std::ostream& operator<<(std::ostream& str, const OptionRange& opt) {
  return str << opt.ToString();
}

class ScanTarget {
 public:
  ScanTarget(Slice table_key_prefix, bool is_forward, size_t last_hash_column, size_t num_keys);

  bool Match(Slice row_key) const;

  Slice AsSlice() const;
  Slice WithoutPrefix() const;
  Slice Prefix() const;
  size_t PrefixSize() const;
  bool IsExtremal(size_t column_idx) const;
  bool NeedGroupEnd(size_t column_idx) const;
  Slice SliceUpToColumn(size_t last_column_idx) const;

  std::string ToString() const;

  void Reset();
  void Truncate(size_t column_idx, bool add_inf);
  void Append(
      Slice target, BoundComp upper_match, AddInfinity add_infinity = AddInfinity::kFalse);

  void Append(
      const dockv::KeyEntryValue& target, BoundComp upper_match,
      AddInfinity add_infinity = AddInfinity::kFalse);

  void Append(const OptionRange& option);

  size_t num_columns() const {
    return columns_.size();
  }

 private:
  void FixTail(bool add_inf);
  template <class Value>
  void DoAppend(const Value& value, BoundComp upper_match, AddInfinity add_infinity);

  size_t table_key_prefix_size_;
  bool is_forward_;
  size_t last_hash_column_;
  size_t num_keys_;
  dockv::KeyBytes value_;

  bool has_inf_ = false;

  struct ColumnInfo {
    size_t end;
    bool extremal;

    std::string ToString() const {
      return YB_STRUCT_TO_STRING(end, extremal);
    }
  };

  boost::container::small_vector<ColumnInfo, 0x10> columns_;
};

using ScanOptions = std::vector<std::vector<OptionRange>>;

class HybridScanChoices : public ScanChoices {
 public:
  // Constructs a list of ranges for each IN/EQ clause from the given scanspec.
  // A filter of the form col1 IN (1,2) is converted to col1 IN ([[1], [1]], [[2], [2]]).
  // And filter of the form (col2, col3) IN ((3,4), (5,6)) is converted to
  // (col2, col3) IN ([[3, 4], [3, 4]], [[5, 6], [5, 6]]).
  HybridScanChoices(
      const Schema& schema,
      const qlexpr::YQLScanSpec& doc_spec,
      const dockv::KeyBytes& lower_doc_key,
      const dockv::KeyBytes& upper_doc_key,
      Slice table_key_prefix);

  Status Init(const DocReadContext& doc_read_context);

  bool Finished() const override {
    return finished_;
  }

  Result<bool> InterestedInRow(dockv::KeyBytes* row_key, IntentAwareIterator& iter) override;
  Result<bool> AdvanceToNextRow(dockv::KeyBytes* row_key,
                                IntentAwareIterator& iter,
                                bool current_fetched_row_skipped) override;
  Result<bool> PrepareIterator(IntentAwareIterator& iter, Slice table_key_prefix) override;

  docdb::BloomFilterOptions BloomFilterOptions() override {
    return bloom_filter_options_;
  }

 private:
  friend class ScanChoicesTest;

  using OptionRangeIterator = std::vector<OptionRange>::const_iterator;

  // Sets scan_target_ to the first tuple in the filter space that is >= new_target.
  Result<bool> SkipTargetsUpTo(Slice new_target);
  // not_found is true when the current scan choice is not found.
  // This can happen when the iterator's upperbound is set in variable bloom filter mode.
  // Moves to the next scan choice.
  Result<bool> DoneWithCurrentTarget(bool current_row_skipped, bool not_found);
  void SeekToCurrentTarget(IntentAwareIterator& db_iter);

  // Also updates the checkpoint to latest seen ht from iter.
  bool CurrentTargetMatchesKey(Slice curr, IntentAwareIterator* iter);

  // Utility function for (multi)key scans. Updates the target scan key by
  // incrementing the option index for an OptionList. Will handle overflow by setting current
  // index to 0 and incrementing the previous index instead. If it overflows at first index
  // it means we are done, so it clears the scan target idxs array.
  Status IncrementScanTargetAtOptionList(ssize_t start_option_list_idx);

  // Utility function for testing
  std::vector<OptionRange> TEST_GetCurrentOptions();
  Result<bool> ValidateHashGroup() const;

  // Utility method to return a column corresponding to idx in the schema.
  // This may be different from schema.column_id in the presence of the hash_code column.
  ColumnId GetColumnId(const Schema& schema, size_t idx) const;

  // Returns a pair of iterators to the lowest and exclusive highest option in the current search
  // space of this option list index. See comment for OptionRange.
  std::pair<OptionRangeIterator, OptionRangeIterator> GetSearchSpaceBounds(
      size_t opt_list_idx) const;

  // Gets the option range that corresponds to the given option index at the given
  // option list index.
  OptionRangeIterator GetOptAtIndex(size_t opt_list_idx, size_t opt_index) const;

  // Sets the option that corresponds to the given option index at the given
  // logical option list index.
  void SetOptToIndex(size_t opt_list_idx, size_t opt_index);

  // Sets an entire group to a particular logical option index.
  void SetGroup(size_t opt_list_idx, size_t opt_index);

  // Updates the bloom filter key and the upper bound to the current scan target when using
  // variable bloom filter.
  Status UpdateUpperBound(IntentAwareIterator* iterator);

  const bool is_forward_scan_;
  ScanTarget scan_target_;
  bool finished_ = false;
  bool has_hash_columns_ = false;
  size_t num_hash_cols_;
  size_t num_bloom_filter_cols_;

  // True if CurrentTargetMatchesKey should return true all the time as
  // the filter this ScanChoices iterates over is trivial.
  bool is_trivial_filter_ = false;

  // The following fields aid in the goal of iterating through all possible
  // scan key values based on given IN-lists and range filters.

  // The following encodes the list of option we are iterating over
  ScanOptions scan_options_;

  // Vector of references to currently active elements being used in scan_options_.
  // current_scan_target_ranges_[i] gives us the current OptionRange
  // column i is iterating over of the elements in scan_options_[i]
  mutable std::vector<OptionRangeIterator> current_scan_target_ranges_;

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
  // (1,4,3), (1,6,3), (2,4,5), (2,6,5)
  ColGroupHolder col_groups_;

  size_t prefix_length_ = 0;

  size_t schema_num_keys_;

  MaxSeenHtData max_seen_ht_checkpoint_ = {};

  docdb::BloomFilterOptions bloom_filter_options_;

  KeyBytes upper_bound_;
  std::optional<IntentAwareIteratorUpperboundScope> iterator_bound_scope_;

  ArenaPtr arena_;
};

} // namespace yb::docdb
