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

#include "yb/common/ql_scanspec.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/value.h"
#include "yb/docdb/docdb_fwd.h"

#include "yb/docdb/value_type.h"
#include "yb/util/slice.h"
#include "yb/util/status_fwd.h"

namespace yb {
namespace docdb {

class ScanChoices {
 public:
  explicit ScanChoices(bool is_forward_scan) : is_forward_scan_(is_forward_scan) {}
  virtual ~ScanChoices() {}

  bool CurrentTargetMatchesKey(const Slice& curr);

  // Returns false if there are still target keys we need to scan, and true if we are done.
  virtual bool FinishedWithScanChoices() const { return finished_; }

  virtual bool IsInitialPositionKnown() const { return false; }

  // Go to the next scan target if any.
  virtual Status DoneWithCurrentTarget() = 0;

  // Go (directly) to the new target (or the one after if new_target does not
  // exist in the desired list/range). If the new_target is larger than all scan target options it
  // means we are done.
  virtual Status SkipTargetsUpTo(const Slice& new_target) = 0;

  // If the given doc_key isn't already at the desired target, seek appropriately to go to the
  // current target.
  virtual Status SeekToCurrentTarget(IntentAwareIteratorIf* db_iter) = 0;

  static void AppendToKey(const std::vector<KeyEntryValue>& values, KeyBytes* key_bytes);

  static Result<std::vector<KeyEntryValue>> DecodeKeyEntryValue(
      DocKeyDecoder* decoder, size_t num_cols);

  static ScanChoicesPtr Create(
      const Schema& schema, const DocQLScanSpec& doc_spec,
      const KeyBytes& lower_doc_key, const KeyBytes& upper_doc_key);

  static ScanChoicesPtr Create(
      const Schema& schema, const DocPgsqlScanSpec& doc_spec,
      const KeyBytes& lower_doc_key, const KeyBytes& upper_doc_key);

 protected:
  const bool is_forward_scan_;
  KeyBytes current_scan_target_;
  bool finished_ = false;

 private:
  DISALLOW_COPY_AND_ASSIGN(ScanChoices);
};

// This class combines the notions of option filters (col1 IN (1,2,3)) and
// singular range bound filters (col1 < 4 AND col1 >= 1) into a single notion of
// lists of options of ranges, each encoded by an OptionRange instance as
// below. So a filter for a column given in the
// Doc(QL/PGSQL)ScanSpec is converted into an OptionRange.
// In the end, each HybridScanChoices
// instance should have a sorted list of disjoint ranges for every IN/EQ clause.
// Right now this supports a conjunction of range bound and discrete filters.
// Disjunctions are also supported but are UNTESTED.
// TODO: Test disjunctions when YSQL and YQL support pushing those down
// The lower and upper values are vectors to incorporate multi-column IN caluse.
class OptionRange {
 public:
  OptionRange(
      std::vector<KeyEntryValue> lower, bool lower_inclusive, std::vector<KeyEntryValue> upper,
      bool upper_inclusive)
      : lower_(lower),
        lower_inclusive_(lower_inclusive),
        upper_(upper),
        upper_inclusive_(upper_inclusive) {
    DCHECK(lower.size() == upper.size());
  }


  // Convenience constructors for testing
  OptionRange(int begin, int end)
    : OptionRange({KeyEntryValue::Int32(begin)}, true, {KeyEntryValue::Int32(end)}, true) {}

  OptionRange(int value) : OptionRange(value, value) {} // NOLINT

  OptionRange(int bound, bool upper)
    : OptionRange({upper ? KeyEntryValue(KeyEntryType::kLowest) : KeyEntryValue::Int32(bound)},
                    true,
                    {upper ? KeyEntryValue::Int32(bound) : KeyEntryValue(KeyEntryType::kHighest)},
                    true) {}
  OptionRange()
    : OptionRange({KeyEntryValue(KeyEntryType::kLowest)},
                  true,
                  {KeyEntryValue(KeyEntryType::kHighest)},
                  true) {}

  const std::vector<KeyEntryValue>& lower() const { return lower_; }
  bool lower_inclusive() const { return lower_inclusive_; }
  const std::vector<KeyEntryValue>& upper() const { return upper_; }
  bool upper_inclusive() const { return upper_inclusive_; }
  size_t size() const { return lower_.size(); }

  static bool upper_lt(const OptionRange& range1, const OptionRange& range2) {
    DCHECK(range1.size() == range2.size());
    return range1.upper_ < range2.upper_;
  }

  static bool lower_gt(const OptionRange &range1,
                       const OptionRange &range2) {
    DCHECK(range1.size() == range2.size());
    return range1.lower_ > range2.lower_;
  }

 private:
  std::vector<KeyEntryValue> lower_;
  bool lower_inclusive_;
  std::vector<KeyEntryValue> upper_;
  bool upper_inclusive_;


  friend class ScanChoicesTest;
  FRIEND_TEST(ScanChoicesTest, SimpleRangeFilterHybridScan);
  bool operator==(const OptionRange& other) const {
    if(size() != other.size()) {
        return false;
    }
    return lower_inclusive() == other.lower_inclusive() &&
            upper_inclusive() == other.upper_inclusive() &&
            !util::CompareVectors(lower(), other.lower()) &&
            !util::CompareVectors(upper(), other.upper());
  }
};

inline std::ostream& operator<<(std::ostream& str, const OptionRange& opt) {
  if (opt.lower_inclusive()) {
    str << "[";
  } else {
    str << "(";
  }

  for (auto it : opt.lower()) {
    str << it.ToString();
  }

  str << ", ";

  for (auto it : opt.upper()) {
    str << it.ToString();
  }

  if (opt.upper_inclusive()) {
    str << "]";
  } else {
    str << ")";
  }
  return str;
}


class HybridScanChoices : public ScanChoices {
 public:
  // Constructs a list of ranges for each IN/EQ clause from the given scanspec.
  // A filter of the form col1 IN (1,2) is converted to col1 IN ([[1], [1]], [[2], [2]]).
  // And filter of the form (col2, col3) IN ((3,4), (5,6)) is converted to
  // (col2, col3) IN ([[3, 4], [3, 4]], [[5, 6], [5, 6]]).

  HybridScanChoices(
    const Schema& schema,
    const KeyBytes& lower_doc_key,
    const KeyBytes& upper_doc_key,
    bool is_forward_scan,
    const std::vector<ColumnId>& range_options_col_ids,
    const std::shared_ptr<std::vector<OptionList>>& range_options,
    const std::vector<ColumnId>& range_bounds_col_ids,
    const QLScanRange* range_bounds,
    const std::vector<size_t>& range_options_num_cols);

  HybridScanChoices(
    const Schema& schema,
    const DocPgsqlScanSpec& doc_spec,
    const KeyBytes& lower_doc_key,
    const KeyBytes& upper_doc_key);

  HybridScanChoices(
    const Schema& schema,
    const DocQLScanSpec& doc_spec,
    const KeyBytes& lower_doc_key,
    const KeyBytes& upper_doc_key);

  Status SkipTargetsUpTo(const Slice& new_target) override;
  Status DoneWithCurrentTarget() override;
  Status SeekToCurrentTarget(IntentAwareIteratorIf* db_iter) override;

 protected:
  friend class ScanChoicesTest;
  // Utility function for (multi)key scans. Updates the target scan key by
  // incrementing the option index for an OptionList. Will handle overflow by setting current
  // index to 0 and incrementing the previous index instead. If it overflows at first index
  // it means we are done, so it clears the scan target idxs array.
  Status IncrementScanTargetAtOptionList(int start_option_list_idx);

  // Utility function for testing
  std::vector<OptionRange> TEST_GetCurrentOptions();

 private:
  KeyBytes prev_scan_target_;

  // The following encodes the list of ranges we are iterating over
  std::vector<std::vector<OptionRange>> range_cols_scan_options_;

  // Vector of references to currently active elements being used
  // in range_cols_scan_options_
  // current_scan_target_ranges_[i] gives us the current OptionRange
  // column i is iterating over of the elements in
  // range_cols_scan_options_[i]
  mutable std::vector<std::vector<OptionRange>::const_iterator> current_scan_target_ranges_;

  bool is_options_done_ = false;

  const KeyBytes lower_doc_key_;
  const KeyBytes upper_doc_key_;

  // For every set of range options, stores the number of columns involved
  // (the number of columns can be more than one with the support for multi-column operations)
  std::vector<size_t> range_options_num_cols_;
};

}  // namespace docdb
}  // namespace yb
