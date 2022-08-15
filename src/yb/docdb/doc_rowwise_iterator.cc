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

#include "yb/docdb/doc_rowwise_iterator.h"
#include <iterator>

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "yb/common/common.pb.h"
#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/ql_expr.h"
#include "yb/common/ql_scanspec.h"
#include "yb/common/ql_value.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_path.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_reader.h"
#include "yb/docdb/doc_scanspec_util.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_types.h"
#include "yb/docdb/expiration.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/value.h"
#include "yb/docdb/value_type.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/util/flags.h"
#include "yb/rocksdb/db/compaction.h"
#include "yb/rocksutil/yb_rocksdb.h"

#include "yb/rocksdb/db.h"

#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/strongly_typed_bool.h"

DECLARE_bool(disable_hybrid_scan);

using std::string;

namespace yb {
namespace docdb {

class ScanChoices {
 public:
  explicit ScanChoices(bool is_forward_scan) : is_forward_scan_(is_forward_scan) {}
  virtual ~ScanChoices() {}

  bool CurrentTargetMatchesKey(const Slice& curr) {
    VLOG(3) << __PRETTY_FUNCTION__ << " checking if acceptable ? "
            << (curr == current_scan_target_ ? "YEP" : "NOPE")
            << ": " << DocKey::DebugSliceToString(curr)
            << " vs " << DocKey::DebugSliceToString(current_scan_target_.AsSlice());
    return curr == current_scan_target_;
  }

  // Returns false if there are still target keys we need to scan, and true if we are done.
  virtual bool FinishedWithScanChoices() const { return finished_; }

  // Go to the next scan target if any.
  virtual Status DoneWithCurrentTarget() = 0;

  // Go (directly) to the new target (or the one after if new_target does not
  // exist in the desired list/range). If the new_target is larger than all scan target options it
  // means we are done.
  virtual Status SkipTargetsUpTo(const Slice& new_target) = 0;

  // If the given doc_key isn't already at the desired target, seek appropriately to go to the
  // current target.
  virtual Status SeekToCurrentTarget(IntentAwareIterator* db_iter) = 0;

  static void AppendToKey(const std::vector<KeyEntryValue>& values, KeyBytes* key_bytes) {
    for (const auto& value : values) {
      value.AppendToKey(key_bytes);
    }
    return;
  }

  static Result<std::vector<KeyEntryValue>> DecodeKeyEntryValue(
      DocKeyDecoder* decoder, size_t num_cols) {
    std::vector<KeyEntryValue> values(num_cols);
    for (size_t i = 0; i < num_cols; i++) {
      RETURN_NOT_OK(decoder->DecodeKeyEntryValue(&values[i]));
    }
    return values;
  }

 protected:
  const bool is_forward_scan_;
  KeyBytes current_scan_target_;
  bool finished_ = false;
};

class DiscreteScanChoices : public ScanChoices {
 public:
  DiscreteScanChoices(const DocQLScanSpec& doc_spec, const KeyBytes& lower_doc_key,
                      const KeyBytes& upper_doc_key)
      : ScanChoices(doc_spec.is_forward_scan()) {
    range_cols_scan_options_ = std::make_shared<std::vector<OptionList>>();
    auto& options = doc_spec.range_options();
    auto& num_cols = doc_spec.range_options_num_cols();
    for (size_t idx = 0; idx < options->size(); idx++) {
      if ((*options)[idx].empty()) {
        continue;
      }
      range_cols_scan_options_->push_back((*options)[idx]);
      range_options_num_cols_.push_back(num_cols[idx]);
    }
    current_scan_target_idxs_.resize(range_cols_scan_options_->size());
    for (size_t i = 0; i < range_cols_scan_options_->size(); i++) {
      current_scan_target_idxs_[i] = range_cols_scan_options_->at(i).begin();
    }

    // Initialize target doc key.
    if (is_forward_scan_) {
      current_scan_target_ = lower_doc_key;
      if (CHECK_RESULT(ClearRangeComponents(&current_scan_target_))) {
        CHECK_OK(SkipTargetsUpTo(lower_doc_key));
      }
    } else {
      current_scan_target_ = upper_doc_key;
      if (CHECK_RESULT(ClearRangeComponents(&current_scan_target_))) {
        CHECK_OK(SkipTargetsUpTo(upper_doc_key));
      }
    }
  }

  DiscreteScanChoices(const DocPgsqlScanSpec& doc_spec, const KeyBytes& lower_doc_key,
                      const KeyBytes& upper_doc_key)
      : ScanChoices(doc_spec.is_forward_scan()) {
    range_cols_scan_options_ = doc_spec.range_options();
    range_options_num_cols_ = doc_spec.range_options_num_cols();
    current_scan_target_idxs_.resize(range_cols_scan_options_->size());
    for (size_t i = 0; i < range_cols_scan_options_->size(); i++) {
      current_scan_target_idxs_[i] = (*range_cols_scan_options_)[i].begin();
    }

    // Initialize target doc key.
    if (is_forward_scan_) {
      current_scan_target_ = lower_doc_key;
      if (CHECK_RESULT(ClearRangeComponents(&current_scan_target_))) {
        CHECK_OK(SkipTargetsUpTo(lower_doc_key));
      }
    } else {
      current_scan_target_ = upper_doc_key;
      if (CHECK_RESULT(ClearRangeComponents(&current_scan_target_))) {
        CHECK_OK(SkipTargetsUpTo(upper_doc_key));
      }
    }
  }

  Status DoneWithCurrentTarget() override;
  Status SkipTargetsUpTo(const Slice& new_target) override;
  Status SeekToCurrentTarget(IntentAwareIterator* db_iter) override;

 protected:
  // Utility function for (multi)key scans. Updates the target scan key by incrementing the option
  // index for an OptionList. Will handle overflow by setting current index to 0 and
  // incrementing the previous index instead. If it overflows at first index
  // it means we are done, so it clears the scan target idxs array.
  Status IncrementScanTargetAtOptionList(size_t start_option_list_idx);

  // Utility function for (multi)key scans to initialize the range portion of the current scan
  // target, scan target with the first option.
  // Only needed for scans that include the static row, otherwise Init will take care of this.
  Result<bool> InitScanTargetRangeGroupIfNeeded();

 private:
  // For (multi)key scans (e.g. selects with 'IN' condition on the range columns) we hold the
  // options for each IN/EQ clause as we iteratively seek to each target key.
  // e.g. for a query "h = 1 and r1 in (2,3) and (r2, r3) in ((4,5), (6,7)) and r4 = 8":
  //  range_cols_scan_options_   [[[2], [3]], [[4, 5], [6,7]], [[8]]] --  value options for each
  //                                                                      IN/EQ clause.
  //  current_scan_target_idxs_  goes from [0, 0, 0] up to [1, 1, 0] -- except when including the
  //                             static row when it starts from [0, 0, -1] instead.
  //  current_scan_target_       goes from [1][2,4,5,8] up to [1][3,6,7,8] -- is the doc key
  //                             containing, for each option set, the value (option) referenced
  //                             by the corresponding index (updated along with
  //                             current_scan_target_idxs_).
  std::shared_ptr<std::vector<OptionList>> range_cols_scan_options_;
  mutable std::vector<OptionList::const_iterator> current_scan_target_idxs_;

  // For every set of range options, stores the number of columns involved
  // (the number of columns can be more than one with the support for multi-column operations)
  std::vector<size_t> range_options_num_cols_;
};

Status DiscreteScanChoices::IncrementScanTargetAtOptionList(size_t start_option_list_idx) {
  DCHECK_LE(start_option_list_idx, current_scan_target_idxs_.size());

  // Increment start option list, move backwards in case of overflow.
  ssize_t option_list_idx = start_option_list_idx;
  for (; option_list_idx >= 0; option_list_idx--) {
    const auto& choices = (*range_cols_scan_options_)[option_list_idx];
    auto& it = current_scan_target_idxs_[option_list_idx];

    if (++it != choices.end()) {
      break;
    }
    it = choices.begin();
  }

  if (option_list_idx < 0) {
    // If we got here we finished all the options and are done.
    finished_ = true;
    return Status::OK();
  }

  DocKeyDecoder decoder(current_scan_target_);
  RETURN_NOT_OK(decoder.DecodeToRangeGroup());
  for (int i = 0; i != option_list_idx; ++i) {
    VERIFY_RESULT(DecodeKeyEntryValue(&decoder, range_options_num_cols_[i]));
  }

  current_scan_target_.Truncate(
      decoder.left_input().cdata() - current_scan_target_.AsSlice().cdata());

  for (size_t i = option_list_idx; i <= start_option_list_idx; ++i) {
    AppendToKey(*current_scan_target_idxs_[i], &current_scan_target_);
  }

  return Status::OK();
}

Result<bool> DiscreteScanChoices::InitScanTargetRangeGroupIfNeeded() {
  DocKeyDecoder decoder(current_scan_target_.AsSlice());
  RETURN_NOT_OK(decoder.DecodeToRangeGroup());

  // Initialize the range key values if needed (i.e. we scanned the static row until now).
  if (!VERIFY_RESULT(decoder.HasPrimitiveValue())) {
    current_scan_target_.mutable_data()->pop_back();
    for (size_t option_list_idx = 0; option_list_idx < range_cols_scan_options_->size();
         option_list_idx++) {
      AppendToKey(*current_scan_target_idxs_[option_list_idx], &current_scan_target_);
    }
    current_scan_target_.AppendKeyEntryType(KeyEntryType::kGroupEnd);
    return true;
  }
  return false;
}

Status DiscreteScanChoices::DoneWithCurrentTarget() {
  VLOG(2) << __PRETTY_FUNCTION__ << " moving on to next target";
  DCHECK(!FinishedWithScanChoices());

  // Initialize the first target/option if not done already, otherwise go to the next one.
  if (!VERIFY_RESULT(InitScanTargetRangeGroupIfNeeded())) {
    RETURN_NOT_OK(IncrementScanTargetAtOptionList(range_cols_scan_options_->size() - 1));
    current_scan_target_.AppendKeyEntryType(KeyEntryType::kGroupEnd);
  }
  return Status::OK();
}

Status DiscreteScanChoices::SkipTargetsUpTo(const Slice& new_target) {
  VLOG(2) << __PRETTY_FUNCTION__
            << " Updating current target to be >= "
            << DocKey::DebugSliceToString(new_target);
  DCHECK(!FinishedWithScanChoices());
  RETURN_NOT_OK(InitScanTargetRangeGroupIfNeeded());
  DocKeyDecoder decoder(new_target);
  RETURN_NOT_OK(decoder.DecodeToRangeGroup());
  current_scan_target_.Reset(Slice(new_target.data(), decoder.left_input().data()));

  size_t option_list_idx = 0;
  std::vector<KeyEntryValue> target_value;
  while (option_list_idx < range_cols_scan_options_->size()) {
    target_value =
        VERIFY_RESULT(DecodeKeyEntryValue(&decoder, range_options_num_cols_[option_list_idx]));
    const auto& choices = (*range_cols_scan_options_)[option_list_idx];
    auto& it = current_scan_target_idxs_[option_list_idx];

    // Fast-path in case the existing value for this column already matches the new target.
    if (target_value == *it) {
      option_list_idx++;
      AppendToKey(target_value, &current_scan_target_);
      continue;
    }

    // Search for the option that matches new target value (for the current column).
    if (is_forward_scan_) {
      it = std::lower_bound(choices.begin(), choices.end(), target_value);
    } else {
      it = std::lower_bound(choices.begin(), choices.end(), target_value, std::greater<>());
    }

    // If we overflowed, the new target value for this option list is larger than all our options,
    // so we go back and increment the previous index instead.
    if (it == choices.end()) {
      RETURN_NOT_OK(IncrementScanTargetAtOptionList(option_list_idx - 1));
      break;
    }

    // Else, update the current target value for this column.
    AppendToKey(*it, &current_scan_target_);

    // If we did not find an exact match we are already beyond the new target so we can stop.
    if (target_value != *it) {
      option_list_idx++;
      break;
    }

    option_list_idx++;
  }

  // If there are any columns left (i.e. we stopped early), it means we did not find an exact
  // match and we reached beyond the new target key. So we need to include all options for the
  // leftover columns (i.e. set all following indexes to 0).
  for (size_t i = option_list_idx; i < current_scan_target_idxs_.size(); i++) {
    current_scan_target_idxs_[i] = (*range_cols_scan_options_)[i].begin();
    AppendToKey(*current_scan_target_idxs_[i], &current_scan_target_);
  }

  current_scan_target_.AppendKeyEntryType(KeyEntryType::kGroupEnd);

  VLOG(2) << "After " << __PRETTY_FUNCTION__ << " current_scan_target_ is "
          << DocKey::DebugSliceToString(current_scan_target_);

  return Status::OK();
}

Status DiscreteScanChoices::SeekToCurrentTarget(IntentAwareIterator* db_iter) {
  VLOG(2) << __PRETTY_FUNCTION__ << " Advancing iterator towards target";
  // Seek to the current target doc key if needed.
  if (!FinishedWithScanChoices()) {
    if (is_forward_scan_) {
      VLOG(2) << __PRETTY_FUNCTION__ << " Seeking to " << current_scan_target_;
      db_iter->Seek(current_scan_target_);
    } else {
      auto tmp = current_scan_target_;
      tmp.AppendKeyEntryType(KeyEntryType::kHighest);
      VLOG(2) << __PRETTY_FUNCTION__ << " Going to PrevDocKey " << tmp;
      db_iter->PrevDocKey(tmp);
    }
  }
  return Status::OK();
}

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
};

class HybridScanChoices : public ScanChoices {
 public:

  // Constructs a list of ranges for each IN/EQ clause from the given scanspec.
  // A filter of the form col1 IN (1,2) is converted to col1 IN ([[1], [1]], [[2], [2]]).
  // And filter of the form (col2, col3) IN ((3,4), (5,6)) is converted to
  // (col2, col3) IN ([[3, 4], [3, 4]], [[5, 6], [5, 6]]).

  HybridScanChoices(const Schema& schema,
                    const KeyBytes& lower_doc_key,
                    const KeyBytes& upper_doc_key,
                    bool is_forward_scan,
                    const std::vector<ColumnId>& range_options_col_ids,
                    const std::shared_ptr<std::vector<OptionList>>& range_options,
                    const std::vector<ColumnId>& range_bounds_col_ids,
                    const QLScanRange* range_bounds,
                    const std::vector<size_t>& range_options_num_cols)
      : ScanChoices(is_forward_scan), lower_doc_key_(lower_doc_key), upper_doc_key_(upper_doc_key) {
    size_t num_hash_cols = schema.num_hash_key_columns();

    for (size_t idx = num_hash_cols; idx < schema.num_key_columns(); idx++) {
      const ColumnId col_id = schema.column_id(idx);
      std::vector<OptionRange> current_options;
      size_t num_cols = 1;
      bool col_has_range_option =
          std::find(range_options_col_ids.begin(), range_options_col_ids.end(), col_id) !=
          range_options_col_ids.end();

      bool col_has_range_bound =
          std::find(range_bounds_col_ids.begin(), range_bounds_col_ids.end(), col_id) !=
          range_bounds_col_ids.end();
      // If this is a range bound filter, we create a singular
      // list of the given range bound
      if (col_has_range_bound && !col_has_range_option) {
        const auto col_sort_type = schema.column(idx).sorting_type();
        const QLScanRange::QLRange range = range_bounds->RangeFor(col_id);
        const auto lower = GetQLRangeBoundAsPVal(range, col_sort_type, true /* lower_bound */);
        const auto upper = GetQLRangeBoundAsPVal(range, col_sort_type, false /* upper_bound */);
        current_options.emplace_back(
            std::vector{lower},
            GetQLRangeBoundIsInclusive(range, col_sort_type, true),
            std::vector{upper},
            GetQLRangeBoundIsInclusive(range, col_sort_type, false));
      } else if (col_has_range_option) {
        num_cols = range_options_num_cols[idx - num_hash_cols];
        auto& options = (*range_options)[idx - num_hash_cols];

        if (options.empty()) {
          // If there is nothing specified in the IN list like in
          // SELECT * FROM ... WHERE c1 IN ();
          // then nothing should pass the filter.
          // To enforce this, we create a range bound (kHighest, kLowest)
          //
          // As of D15647 we do not send empty options.
          // This is kept for backward compatibility during rolling upgrades.
          current_options.emplace_back(
              std::vector(num_cols, KeyEntryValue(KeyEntryType::kHighest)),
              true,
              std::vector(num_cols, KeyEntryValue(KeyEntryType::kLowest)),
              true);
        }

        for (const auto& option : options) {
          current_options.emplace_back(option, true, option, true);
        }
        idx = idx + num_cols - 1;
      } else {
        // If no filter is specified, we just impose an artificial range
        // filter [kLowest, kHighest]
        current_options.emplace_back(
            std::vector{KeyEntryValue(KeyEntryType::kLowest)},
            true,
            std::vector{KeyEntryValue(KeyEntryType::kHighest)},
            true);
      }
      range_cols_scan_options_.push_back(current_options);
      // For IN/EQ clause at index i,
      // range_options_num_cols_[i] == range_cols_scan_options_[i][0].upper.size()
      range_options_num_cols_.push_back(num_cols);
    }

    current_scan_target_ranges_.resize(range_cols_scan_options_.size());
    for (size_t i = 0; i < range_cols_scan_options_.size(); i++) {
      current_scan_target_ranges_[i] = range_cols_scan_options_.at(i).begin();
    }

    if (is_forward_scan_) {
      current_scan_target_ = lower_doc_key;
    } else {
      current_scan_target_ = upper_doc_key;
    }
  }

  HybridScanChoices(const Schema& schema,
                    const DocPgsqlScanSpec& doc_spec,
                    const KeyBytes& lower_doc_key,
                    const KeyBytes& upper_doc_key)
      : HybridScanChoices(
            schema, lower_doc_key, upper_doc_key, doc_spec.is_forward_scan(),
            doc_spec.range_options_indexes(), doc_spec.range_options(),
            doc_spec.range_bounds_indexes(), doc_spec.range_bounds(),
            doc_spec.range_options_num_cols()) {}

  HybridScanChoices(const Schema& schema,
                    const DocQLScanSpec& doc_spec,
                    const KeyBytes& lower_doc_key,
                    const KeyBytes& upper_doc_key)
      : HybridScanChoices(
            schema, lower_doc_key, upper_doc_key, doc_spec.is_forward_scan(),
            doc_spec.range_options_indexes(), doc_spec.range_options(),
            doc_spec.range_bounds_indexes(), doc_spec.range_bounds(),
            doc_spec.range_options_num_cols()) {}

  Status SkipTargetsUpTo(const Slice& new_target) override;
  Status DoneWithCurrentTarget() override;
  Status SeekToCurrentTarget(IntentAwareIterator* db_iter) override;

 protected:
  // Utility function for (multi)key scans. Updates the target scan key by
  // incrementing the option index for an OptionList. Will handle overflow by setting current
  // index to 0 and incrementing the previous index instead. If it overflows at first index
  // it means we are done, so it clears the scan target idxs array.
  Status IncrementScanTargetAtOptionList(int start_option_list_idx);

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

// Sets current_scan_target_ to the first tuple in the filter space
// that is >= new_target.
Status HybridScanChoices::SkipTargetsUpTo(const Slice& new_target) {
  VLOG(2) << __PRETTY_FUNCTION__ << " Updating current target to be >= "
          << DocKey::DebugSliceToString(new_target);
  DCHECK(!FinishedWithScanChoices());
  is_options_done_ = false;

  /*
   Let's say we have a row key with (A B) as the hash part and C, D as the range part:
   ((A B) C D) E F

   Let's say our current constraints :
    l_c_k <= C <= u_c_k
     4            6

    l_d_j <= D <= u_d_j
      3           5

    a b  0 d  -> a  b l_c  0

    a b  5 d  -> a  b  5   d
                  [ Will subsequently seek out of document on reading the subdoc]

    a b  7 d  -> a b l_c_(k+1) 0
                [ If there is another range bound filter that's higher than the
                  current one, effectively, moving this column to the next
                  range in the filter list.] This is also only applicable if
                  l_c_(k+1) is a closed bound
              -> a b l_c_(k+1) Inf
                [ If in the above case + l_c_(k+1) is an open (strict) bound. ]
              -> a b Inf
                [ This will seek to <b_next> and on the next invocation update:
                   a <b_next> ? ? -> a <b_next> l_c_0 0 ]

    a b  c 6  -> a b c l_d_(j+1)
                [ If there is another range bound filter that's higher than the
                  d, effectively, moving column D to the next
                  range in the filter list.]
              -> a b c Inf
                [ If c_next is between l_c_k and u_c_k. This will seek to <a b
                   <c_next>> and on the next invocation update:
                   a b <c_next> ? -> a b <c_next> l_d_0 ]
              -> a b l_c_(k+1) l_d_0
                 [ If c_next is above u_c_k. We do this because we know
                   exactly what the next tuple in our filter space should be.]
              -> -> a b l_c_(k+1) Inf
                 [ If c_next is above u_c_k and l_c_(k+1) is a strict bound. ]

    Let's now say our current constraints are:
    l_c_k < C < u_c_k
     4            6

    l_d_j <= D < u_d_j
      3           5

    a b 4 d   -> a b 4 +Inf
    a b c 5   -> a b c l_d_(j+1)
                [ If there is another range bound filter that's higher than the
                  d, effectively, moving column D to the next
                  range in the filter list.]
              -> a b c Inf
                [ If c_next is between l_c_k and u_c_k. This will seek to <a b
                   <c_next>> and on the next invocation update:
                   a b <c_next> ? -> a b <c_next> l_d_0 ]
              -> a b l_c_(k+1) l_d_0
                 [ If c_next is = u_c_k. We do this because we know
                   exactly what the next tuple in our filter space should be.]
              -> a b l_c_(k+1) Inf
                 [ If c_next is = u_c_k and l_c_(k+1) is a strict bound. ]
  */
  DocKeyDecoder decoder(new_target);
  RETURN_NOT_OK(decoder.DecodeToRangeGroup());
  current_scan_target_.Reset(Slice(new_target.data(), decoder.left_input().data()));

  size_t option_list_idx = 0;
  for (option_list_idx = 0; option_list_idx < current_scan_target_ranges_.size();
       option_list_idx++) {
    const auto& options = range_cols_scan_options_[option_list_idx];
    size_t num_cols = range_options_num_cols_[option_list_idx];
    auto current_it = current_scan_target_ranges_[option_list_idx];
    DCHECK(current_it != options.end());

    std::vector<KeyEntryValue> target_value =
        VERIFY_RESULT(DecodeKeyEntryValue(&decoder, num_cols));

    auto lower = current_it->lower();
    auto upper = current_it->upper();
    bool lower_incl = current_it->lower_inclusive();
    bool upper_incl = current_it->upper_inclusive();

    using kval_cmp_fn_t =
        std::function<bool(const std::vector<KeyEntryValue>&, const std::vector<KeyEntryValue>&)>;

    kval_cmp_fn_t lower_cmp_fn = lower_incl
                                     ? [](const std::vector<KeyEntryValue>& t1,
                                          const std::vector<KeyEntryValue>& t2) { return t1 >= t2; }
                                     : [](const std::vector<KeyEntryValue>& t1,
                                          const std::vector<KeyEntryValue>& t2) { return t1 > t2; };
    kval_cmp_fn_t upper_cmp_fn = upper_incl
                                     ? [](const std::vector<KeyEntryValue>& t1,
                                          const std::vector<KeyEntryValue>& t2) { return t1 <= t2; }
                                     : [](const std::vector<KeyEntryValue>& t1,
                                          const std::vector<KeyEntryValue>& t2) { return t1 < t2; };

    // If it's in range then good, continue after appending the target value
    // column.
    if (lower_cmp_fn(target_value, lower) && upper_cmp_fn(target_value, upper)) {
      AppendToKey(target_value, &current_scan_target_);
      continue;
    }

    // If target_value is not in the current range then we must find a range
    // that works for it.
    // If we are above all ranges then increment the index of the previous
    // column.
    // Else, target_value is below at least one range: find the lowest lower
    // bound above target_value and use that, this relies on the assumption
    // that all our filter ranges are disjoint.

    auto it = options.begin();

    // Find an upper (lower) bound closest to target_value
    OptionRange target_value_range(target_value, true, target_value, true);
    if (is_forward_scan_) {
      it = std::lower_bound(
          options.begin(), options.end(), target_value_range, OptionRange::upper_lt);
    } else {
      it = std::lower_bound(
          options.begin(), options.end(), target_value_range, OptionRange::lower_gt);
    }

    if (it == options.end()) {
      // target value is higher than all range options and
      // we need to increment.
      RETURN_NOT_OK(IncrementScanTargetAtOptionList(static_cast<int>(option_list_idx) - 1));
      option_list_idx = current_scan_target_ranges_.size();
      break;
    }

    current_scan_target_ranges_[option_list_idx] = it;

    // If we are within a range then target_value itself should work

    lower = it->lower();
    upper = it->upper();

    lower_incl = it->lower_inclusive();
    upper_incl = it->upper_inclusive();

    lower_cmp_fn = lower_incl ? [](const std::vector<KeyEntryValue>& t1,
                                   const std::vector<KeyEntryValue>& t2) { return t1 >= t2; }
                              : [](const std::vector<KeyEntryValue>& t1,
                                   const std::vector<KeyEntryValue>& t2) { return t1 > t2; };
    upper_cmp_fn = upper_incl ? [](const std::vector<KeyEntryValue>& t1,
                                   const std::vector<KeyEntryValue>& t2) { return t1 <= t2; }
                              : [](const std::vector<KeyEntryValue>& t1,
                                   const std::vector<KeyEntryValue>& t2) { return t1 < t2; };

    if (target_value >= lower && target_value <= upper) {
      AppendToKey(target_value, &current_scan_target_);
      if (lower_cmp_fn(target_value, lower) && upper_cmp_fn(target_value, upper)) {
        // target_value satisfies the current range condition.
        // Let's move on.
        continue;
      }

      // We're here because the strictness part of a bound is broken

      // If a strict upper bound is broken then we can increment
      // and move on to the next target

      DCHECK(target_value == upper || target_value == lower);

      if (is_forward_scan_ && target_value == upper) {
        RETURN_NOT_OK(IncrementScanTargetAtOptionList(static_cast<int>(option_list_idx)));
        option_list_idx = current_scan_target_ranges_.size();
        break;
      }

      if (!is_forward_scan_ && target_value == lower) {
        RETURN_NOT_OK(IncrementScanTargetAtOptionList(static_cast<int>(option_list_idx)));
        option_list_idx = current_scan_target_ranges_.size();
        break;
      }

      // If a strict lower bound is broken then we can simply append
      // a kHighest (kLowest) to get a target that satisfies the strict
      // lower bound
      if (is_forward_scan_) {
        KeyEntryValue(KeyEntryType::kHighest).AppendToKey(&current_scan_target_);
      } else {
        KeyEntryValue(KeyEntryType::kLowest).AppendToKey(&current_scan_target_);
      }
      option_list_idx++;
      break;
    }

    // Otherwise we must set it to the next lower bound.
    // This only works as we are assuming all given ranges are
    // disjoint.

    DCHECK(
        (is_forward_scan_ && lower > target_value) || (!is_forward_scan_ && upper < target_value));

    if (is_forward_scan_) {
      AppendToKey(lower, &current_scan_target_);
      if (!lower_incl) {
        KeyEntryValue(KeyEntryType::kHighest).AppendToKey(&current_scan_target_);
      }
    } else {
      AppendToKey(upper, &current_scan_target_);
      if (!upper_incl) {
        KeyEntryValue(KeyEntryType::kLowest).AppendToKey(&current_scan_target_);
      }
    }
    option_list_idx++;
    break;
  }

  // Reset the remaining range columns to lower bounds for forward scans
  // or upper bounds for backward scans.
  for (size_t i = option_list_idx; i < range_cols_scan_options_.size(); i++) {
    current_scan_target_ranges_[i] = range_cols_scan_options_[i].begin();
    if (is_forward_scan_) {
      AppendToKey(current_scan_target_ranges_[i]->lower(), &current_scan_target_);
    } else {
      AppendToKey(current_scan_target_ranges_[i]->upper(), &current_scan_target_);
    }
  }

  current_scan_target_.AppendKeyEntryType(KeyEntryType::kGroupEnd);
  VLOG(2) << "After " << __PRETTY_FUNCTION__ << " current_scan_target_ is "
          << DocKey::DebugSliceToString(current_scan_target_);
  return Status::OK();
}

// Update the value at start OptionList by setting it up for incrementing to the
// next allowed value in the filter space
// ---------------------------------------------------------------------------
// There are two important cases to consider here.
// Let's say the value of current_scan_target_ at start_col, c,
// is currently V and the current bounds for that column
// is l_c_k <= V <= u_c_k. In the usual case where V != u_c_k
// (or V != l_c_k for backwards scans) such that V_next is still in the given
// restriction, we set column c + 1 to kHighest (kLowest), such that the next
// invocation of GetNext() produces V_next at column similar to what is done
// in SkipTargetsUpTo. In this case, doing a SkipTargetsUpTo on the resulting
// current_scan_target_ should yield the next allowed value in the filter space
// In the case where V = u_c_k (V = l_c_k), or in other words V is at the
// EXTREMAL boundary of the current range, we know exactly what the next value
// of column C will be. So we move column c to the next
// range k+1 and set that column to the new value l_c_(k+1) (u_c_(k+1))
// while setting all columns, b > c to l_b_0 (u_b_0)
// In the case of overflow on a column c (we want to increment the
// restriction range of c to the next range bound for that column but there
// are no restriction ranges remaining), we set the
// current column to the 0th range and move on to increment c - 1
// Note that in almost all cases the resulting current_scan_target_ is strictly
// greater (lesser in the case of backwards scans) than the original
// current_scan_target_. This is necessary to allow the iterator seek out
// of the current scan target. The exception to this rule is below.
// ---------------------------------------------------------------------------
// This function leaves the scan target as is if the next tuple in the current
// scan direction is also the next tuple in the filter space and start_col
// is given as the last column
Status HybridScanChoices::IncrementScanTargetAtOptionList(int start_option_list_idx) {
  VLOG_WITH_FUNC(2) << "Incrementing at " << start_option_list_idx;

  // Increment start col, move backwards in case of overflow.
  int option_list_idx = start_option_list_idx;
  // lower and upper here are taken relative to the scan order
  using extremal_fn_t = std::function<const std::vector<KeyEntryValue>&(const OptionRange&)>;

  using extremal_fn_incl_t = std::function<bool(const OptionRange &)>;

  extremal_fn_t lower_extremal_fn = is_forward_scan_ ? &OptionRange::lower
                                                     : &OptionRange::upper;

  extremal_fn_incl_t lower_extremal_incl_fn = is_forward_scan_ ? &OptionRange::lower_inclusive
                                                               : &OptionRange::upper_inclusive;

  extremal_fn_t upper_extremal_fn = is_forward_scan_ ? &OptionRange::upper
                                                     : &OptionRange::lower;

  DocKeyDecoder t_decoder(current_scan_target_);
  RETURN_NOT_OK(t_decoder.DecodeToRangeGroup());

  // refer to the documentation of this function to see what extremal
  // means here
  std::vector<bool> is_extremal;
  for (int i = 0; i <= option_list_idx; ++i) {
    size_t num_cols = range_options_num_cols_[i];
    std::vector<KeyEntryValue> target_value =
        VERIFY_RESULT(DecodeKeyEntryValue(&t_decoder, num_cols));
    is_extremal.push_back(target_value == upper_extremal_fn(*current_scan_target_ranges_[i]));
  }

  // this variable tells us whether we start by appending
  // kHighest/kLowest at col_idx after the following for loop
  bool start_with_infinity = true;

  for (; option_list_idx >= 0; option_list_idx--) {
    auto& it = current_scan_target_ranges_[option_list_idx];

    if (!is_extremal[option_list_idx]) {
      option_list_idx++;
      start_with_infinity = true;
      break;
    }

    if (++it != range_cols_scan_options_[option_list_idx].end()) {
      // and if this value is at the extremal bound
      DCHECK(is_extremal[option_list_idx]);
      // if we are AT the boundary of a strict bound then we
      // want to append an infinity after this column to satisfy
      // the strict bound requirement
      start_with_infinity = !lower_extremal_incl_fn(*it);
      if (start_with_infinity) {
        option_list_idx++;
      }
      break;
    }

    current_scan_target_ranges_[option_list_idx] =
        range_cols_scan_options_[option_list_idx].begin();
  }

  DocKeyDecoder decoder(current_scan_target_);
  RETURN_NOT_OK(decoder.DecodeToRangeGroup());
  for (int i = 0; i < option_list_idx; ++i) {
    size_t num_cols = range_options_num_cols_[i];
    VERIFY_RESULT(DecodeKeyEntryValue(&decoder, num_cols));
  }

  if (option_list_idx < 0) {
    // If we got here we finished all the options and are done.
    option_list_idx++;
    start_with_infinity = true;
    is_options_done_ = true;
  }

  current_scan_target_.Truncate(
      decoder.left_input().cdata() - current_scan_target_.AsSlice().cdata());

  if (start_with_infinity &&
      (option_list_idx < static_cast<int64>(current_scan_target_ranges_.size()))) {
    if (is_forward_scan_) {
      KeyEntryValue(KeyEntryType::kHighest).AppendToKey(&current_scan_target_);
    } else {
      KeyEntryValue(KeyEntryType::kLowest).AppendToKey(&current_scan_target_);
    }
    option_list_idx++;
  }

  if (start_with_infinity) {
    // there's no point in appending anything after infinity
    return Status::OK();
  }

  // Reset all columns that are > col_idx
  // We don't want to necessarily reset col_idx as it may
  // have been the case that we got here via an increment on col_idx
  int64 current_scan_target_ranges_size = static_cast<int64>(current_scan_target_ranges_.size());
  for (int i = option_list_idx; i < current_scan_target_ranges_size; ++i) {
    auto it_0 = i == option_list_idx ? current_scan_target_ranges_[option_list_idx]
                                     : range_cols_scan_options_[i].begin();
    current_scan_target_ranges_[i] = it_0;
    AppendToKey(lower_extremal_fn(*it_0), &current_scan_target_);
    if (!lower_extremal_incl_fn(*it_0)) {
      if (is_forward_scan_) {
        KeyEntryValue(KeyEntryType::kHighest)
            .AppendToKey(&current_scan_target_);
      } else {
        KeyEntryValue(KeyEntryType::kLowest)
            .AppendToKey(&current_scan_target_);
      }
    }
  }

  return Status::OK();
}

// Method called when the scan target is done being used
Status HybridScanChoices::DoneWithCurrentTarget() {
  // prev_scan_target_ is necessary for backwards scans
  prev_scan_target_ = current_scan_target_;
  RETURN_NOT_OK(
      IncrementScanTargetAtOptionList(static_cast<int>(current_scan_target_ranges_.size()) - 1));
  current_scan_target_.AppendKeyEntryType(KeyEntryType::kGroupEnd);

  // if we we incremented the last index then
  // if this is a forward scan it doesn't matter what we do
  // if this is a backwards scan then dont clear current_scan_target and we
  // stay live
  VLOG_WITH_FUNC(2)
      << "Current_scan_target_ is " << DocKey::DebugSliceToString(current_scan_target_);
  VLOG_WITH_FUNC(2) << "Moving on to next target";

  DCHECK(!FinishedWithScanChoices());

  if (is_options_done_) {
    // It could be possible that we finished all our options but are not
    // done because we haven't hit the bound key yet. This would usually be
    // the case if we are moving onto the next hash key where we will
    // restart our range options.
    const KeyBytes &bound_key = is_forward_scan_ ?
                                  upper_doc_key_ : lower_doc_key_;
    finished_ = bound_key.empty() ? false
                  : is_forward_scan_
                      == (current_scan_target_.CompareTo(bound_key) >= 0);
    VLOG(4) << "finished_ = " << finished_;
  }


  VLOG_WITH_FUNC(4)
      << "current_scan_target_ is " << DocKey::DebugSliceToString(current_scan_target_)
      << " and prev_scan_target_ is " << DocKey::DebugSliceToString(prev_scan_target_);

  // The below condition is either indicative of the special case
  // where IncrementScanTargetAtOptionList didn't change the target due
  // to the case specified in the last section of the
  // documentation for IncrementScanTargetAtOptionList or we have exhausted
  // all available range keys for the given hash key (indicated
  // by is_options_done_)
  // We clear the scan target in these cases to indicate that the
  // current_scan_target_ has been used and is invalid
  // In all other cases, IncrementScanTargetAtOptionList has updated
  // current_scan_target_ to the new value that we want to seek to.
  // Hence, we shouldn't clear it in those cases
  if (prev_scan_target_ == current_scan_target_ || is_options_done_) {
    current_scan_target_.Clear();
  }

  return Status::OK();
}

// Seeks the given iterator to the current target as specified by
// current_scan_target_ and prev_scan_target_ (relevant in backwards
// scans)
Status HybridScanChoices::SeekToCurrentTarget(IntentAwareIterator* db_iter) {
  VLOG(2) << __func__ << ", pos: " << db_iter->DebugPosToString();

  if (!FinishedWithScanChoices()) {
    // if current_scan_target_ is valid we use it to determine
    // what to seek to
    if (!current_scan_target_.empty()) {
      VLOG(3) << __PRETTY_FUNCTION__
              << " current_scan_target_ is non-empty. "
              << DocKey::DebugSliceToString(current_scan_target_);
      if (is_forward_scan_) {
        VLOG(3) << __PRETTY_FUNCTION__
                << " Seeking to "
                << DocKey::DebugSliceToString(current_scan_target_);
        db_iter->Seek(current_scan_target_);
      } else {
        // seek to the highest key <= current_scan_target_
        // seeking to the highest key < current_scan_target_ + kHighest
        // is equivalent to seeking to the highest key <=
        // current_scan_target_
        auto tmp = current_scan_target_;
        KeyEntryValue(KeyEntryType::kHighest).AppendToKey(&tmp);
        VLOG(3) << __PRETTY_FUNCTION__ << " Going to PrevDocKey " << tmp;
        db_iter->PrevDocKey(tmp);
      }
    } else {
      if (!is_forward_scan_ && !prev_scan_target_.empty()) {
        db_iter->PrevDocKey(prev_scan_target_);
      }
    }
  }

  return Status::OK();
}

class RangeBasedScanChoices : public ScanChoices {
 public:
  RangeBasedScanChoices(const Schema& schema, const DocQLScanSpec& doc_spec)
      : ScanChoices(doc_spec.is_forward_scan()) {
    DCHECK(doc_spec.range_bounds());
    lower_.reserve(schema.num_range_key_columns());
    upper_.reserve(schema.num_range_key_columns());
    for (size_t idx = schema.num_hash_key_columns(); idx < schema.num_key_columns(); idx++) {
      const ColumnId col_idx = schema.column_id(idx);
      const auto col_sort_type = schema.column(idx).sorting_type();
      const QLScanRange::QLRange range = doc_spec.range_bounds()->RangeFor(col_idx);
      auto lower = GetQLRangeBoundAsPVal(range, col_sort_type, true /* lower_bound */);
      lower_.push_back(lower);
      auto upper = GetQLRangeBoundAsPVal(range, col_sort_type, false /* upper_bound */);
      upper_.push_back(upper);
    }
  }

  RangeBasedScanChoices(const Schema& schema, const DocPgsqlScanSpec& doc_spec)
      : ScanChoices(doc_spec.is_forward_scan()) {
    DCHECK(doc_spec.range_bounds());
    lower_.reserve(schema.num_range_key_columns());
    upper_.reserve(schema.num_range_key_columns());
    for (auto idx = schema.num_hash_key_columns(); idx < schema.num_key_columns(); idx++) {
      const ColumnId col_idx = schema.column_id(idx);
      const auto col_sort_type = schema.column(idx).sorting_type();
      const QLScanRange::QLRange range = doc_spec.range_bounds()->RangeFor(col_idx);
      const auto lower = GetQLRangeBoundAsPVal(range, col_sort_type, true /* lower_bound */);
      const auto upper = GetQLRangeBoundAsPVal(range, col_sort_type, false /* upper_bound */);
      lower_.emplace_back(lower);
      upper_.emplace_back(upper);
    }
  }

  Status SkipTargetsUpTo(const Slice& new_target) override;
  Status DoneWithCurrentTarget() override;
  Status SeekToCurrentTarget(IntentAwareIterator* db_iter) override;

 private:
  std::vector<KeyEntryValue> lower_, upper_;
  KeyBytes prev_scan_target_;
};

Status RangeBasedScanChoices::SkipTargetsUpTo(const Slice& new_target) {
  VLOG(2) << __PRETTY_FUNCTION__ << " Updating current target to be >= "
          << DocKey::DebugSliceToString(new_target);
  DCHECK(!FinishedWithScanChoices());

  /*
   Let's say we have a row key with (A B) as the hash part and C, D as the range part:
   ((A B) C D) E F

   Let's say we have a range constraint :
    l_c < C < u_c
     4        6

    a b  0 d  -> a  b l_c  d

    a b  5 d  -> a  b  5   d
                  [ Will subsequently seek out of document on reading the subdoc]

    a b  7 d  -> a <b> MAX
                [ This will seek to <b_next> and on the next invocation update:
                   a <b_next> ? ? -> a <b_next> l_c d ]
  */
  DocKeyDecoder decoder(new_target);
  RETURN_NOT_OK(decoder.DecodeToRangeGroup());
  current_scan_target_.Reset(Slice(new_target.data(), decoder.left_input().data()));

  size_t col_idx = 0;
  KeyEntryValue target_value;
  bool last_was_infinity = false;
  for (col_idx = 0; VERIFY_RESULT(decoder.HasPrimitiveValue()); col_idx++) {
    RETURN_NOT_OK(decoder.DecodeKeyEntryValue(&target_value));
    VLOG(3) << "col_idx " << col_idx << " is " << target_value << " in ["
            << yb::ToString(lower_[col_idx]) << " , " << yb::ToString(upper_[col_idx]) << " ] ?";

    const auto& lower = lower_[col_idx];
    if (target_value < lower) {
      const auto tgt = (is_forward_scan_ ? lower : KeyEntryValue(KeyEntryType::kLowest));
      tgt.AppendToKey(&current_scan_target_);
      last_was_infinity = tgt.IsInfinity();
      VLOG(3) << " Updating idx " << col_idx << " from " << target_value << " to " << tgt;
      break;
    }
    const auto& upper = upper_[col_idx];
    if (target_value > upper) {
      const auto tgt = (!is_forward_scan_ ? upper : KeyEntryValue(KeyEntryType::kHighest));
      VLOG(3) << " Updating idx " << col_idx << " from " << target_value << " to " << tgt;
      tgt.AppendToKey(&current_scan_target_);
      last_was_infinity = tgt.IsInfinity();
      break;
    }
    target_value.AppendToKey(&current_scan_target_);
    last_was_infinity = target_value.IsInfinity();
  }

  // Reset the remaining range columns to kHighest/lower for forward scans
  // or kLowest/upper for backward scans.
  while (++col_idx < lower_.size()) {
    if (last_was_infinity) {
      // No point having more components after +/- Inf.
      break;
    }
    if (is_forward_scan_) {
      VLOG(3) << " Updating col_idx " << col_idx << " to " << lower_[col_idx];
      lower_[col_idx].AppendToKey(&current_scan_target_);
      last_was_infinity = lower_[col_idx].IsInfinity();
    } else {
      VLOG(3) << " Updating col_idx " << col_idx << " to " << upper_[col_idx];
      upper_[col_idx].AppendToKey(&current_scan_target_);
      last_was_infinity = upper_[col_idx].IsInfinity();
    }
  }
  current_scan_target_.AppendKeyEntryType(KeyEntryType::kGroupEnd);
  VLOG(2) << "After " << __PRETTY_FUNCTION__ << " current_scan_target_ is "
          << DocKey::DebugSliceToString(current_scan_target_);

  return Status::OK();
}

Status RangeBasedScanChoices::DoneWithCurrentTarget() {
  prev_scan_target_ = current_scan_target_;
  current_scan_target_.Clear();
  return Status::OK();
}

Status RangeBasedScanChoices::SeekToCurrentTarget(IntentAwareIterator* db_iter) {
  VLOG(2) << __PRETTY_FUNCTION__ << " Advancing iterator towards target";

  if (!FinishedWithScanChoices()) {
    if (!current_scan_target_.empty()) {
      VLOG(3) << __PRETTY_FUNCTION__
              << " current_scan_target_ is non-empty. "
              << current_scan_target_;
      if (is_forward_scan_) {
        VLOG(3) << __PRETTY_FUNCTION__
                << " Seeking to "
                << DocKey::DebugSliceToString(current_scan_target_);
        db_iter->Seek(current_scan_target_);
      } else {
        auto tmp = current_scan_target_;
        KeyEntryValue(KeyEntryType::kHighest).AppendToKey(&tmp);
        VLOG(3) << __PRETTY_FUNCTION__ << " Going to PrevDocKey " << tmp;  // Never seen.
        db_iter->PrevDocKey(tmp);
      }
    } else {
      if (!is_forward_scan_ && !prev_scan_target_.empty()) {
        db_iter->PrevDocKey(prev_scan_target_);
      }
    }
  }

  return Status::OK();
}

DocRowwiseIterator::DocRowwiseIterator(
    const Schema &projection,
    std::reference_wrapper<const DocReadContext> doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const DocDB& doc_db,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    RWOperationCounter* pending_op_counter)
    : projection_(projection),
      doc_read_context_(doc_read_context),
      txn_op_context_(txn_op_context),
      deadline_(deadline),
      read_time_(read_time),
      doc_db_(doc_db),
      has_bound_key_(false),
      pending_op_(pending_op_counter),
      done_(false) {
  projection_subkeys_.reserve(projection.num_columns() + 1);
  projection_subkeys_.push_back(KeyEntryValue::kLivenessColumn);
  for (size_t i = projection_.num_key_columns(); i < projection.num_columns(); i++) {
    projection_subkeys_.push_back(KeyEntryValue::MakeColumnId(projection.column_id(i)));
  }
  std::sort(projection_subkeys_.begin(), projection_subkeys_.end());
}

DocRowwiseIterator::~DocRowwiseIterator() {
}

Status DocRowwiseIterator::Init(TableType table_type, const Slice& sub_doc_key) {
  db_iter_ = CreateIntentAwareIterator(
      doc_db_,
      BloomFilterMode::DONT_USE_BLOOM_FILTER,
      boost::none /* user_key_for_filter */,
      rocksdb::kDefaultQueryId,
      txn_op_context_,
      deadline_,
      read_time_);
  if (!sub_doc_key.empty()) {
    row_key_ = sub_doc_key;
  } else {
    DocKeyEncoder(&iter_key_).Schema(doc_read_context_.schema);
    row_key_ = iter_key_;
  }
  row_hash_key_ = row_key_;
  VLOG(3) << __PRETTY_FUNCTION__ << " Seeking to " << row_key_;
  db_iter_->Seek(row_key_);
  row_ready_ = false;
  has_bound_key_ = false;
  table_type_ = table_type;
  if (table_type == TableType::PGSQL_TABLE_TYPE) {
    ignore_ttl_ = true;
  }

  return Status::OK();
}

Result<bool> DocRowwiseIterator::InitScanChoices(
    const DocQLScanSpec& doc_spec, const KeyBytes& lower_doc_key, const KeyBytes& upper_doc_key) {

  if (!FLAGS_disable_hybrid_scan) {
    if (doc_spec.range_options() || doc_spec.range_bounds()) {
      scan_choices_.reset(new HybridScanChoices(
          doc_read_context_.schema, doc_spec, lower_doc_key, upper_doc_key));
    }

    return false;
  }

  if (doc_spec.range_options()) {
    scan_choices_.reset(new DiscreteScanChoices(doc_spec, lower_doc_key, upper_doc_key));
    // Let's not seek to the lower doc key or upper doc key. We know exactly what we want.
    RETURN_NOT_OK(AdvanceIteratorToNextDesiredRow());
    return true;
  }

  if (doc_spec.range_bounds()) {
    scan_choices_.reset(new RangeBasedScanChoices(doc_read_context_.schema, doc_spec));
  }

  return false;
}

Result<bool> DocRowwiseIterator::InitScanChoices(
    const DocPgsqlScanSpec& doc_spec, const KeyBytes& lower_doc_key,
    const KeyBytes& upper_doc_key) {

  if (!FLAGS_disable_hybrid_scan) {
    if (doc_spec.range_options() || doc_spec.range_bounds()) {
      scan_choices_.reset(new HybridScanChoices(
          doc_read_context_.schema, doc_spec, lower_doc_key, upper_doc_key));
    }

    return false;
  }

  if (doc_spec.range_options()) {
    scan_choices_.reset(new DiscreteScanChoices(doc_spec, lower_doc_key, upper_doc_key));
    // Let's not seek to the lower doc key or upper doc key. We know exactly what we want.
    RETURN_NOT_OK(AdvanceIteratorToNextDesiredRow());
    return true;
  }

  if (doc_spec.range_bounds()) {
    scan_choices_.reset(new RangeBasedScanChoices(doc_read_context_.schema, doc_spec));
  }

  return false;
}

template <class T>
Status DocRowwiseIterator::DoInit(const T& doc_spec) {
  is_forward_scan_ = doc_spec.is_forward_scan();

  VLOG(4) << "Initializing iterator direction: " << (is_forward_scan_ ? "FORWARD" : "BACKWARD");

  auto lower_doc_key = VERIFY_RESULT(doc_spec.LowerBound());
  auto upper_doc_key = VERIFY_RESULT(doc_spec.UpperBound());
  VLOG(4) << "DocKey Bounds " << DocKey::DebugSliceToString(lower_doc_key.AsSlice())
          << ", " << DocKey::DebugSliceToString(upper_doc_key.AsSlice());

  // TODO(bogdan): decide if this is a good enough heuristic for using blooms for scans.
  const bool is_fixed_point_get =
      !lower_doc_key.empty() &&
      VERIFY_RESULT(HashedOrFirstRangeComponentsEqual(lower_doc_key, upper_doc_key));
  const auto mode = is_fixed_point_get ? BloomFilterMode::USE_BLOOM_FILTER
                                       : BloomFilterMode::DONT_USE_BLOOM_FILTER;

  db_iter_ = CreateIntentAwareIterator(
      doc_db_, mode, lower_doc_key.AsSlice(), doc_spec.QueryId(), txn_op_context_,
      deadline_, read_time_, doc_spec.CreateFileFilter());

  row_ready_ = false;

  if (is_forward_scan_) {
    has_bound_key_ = !upper_doc_key.empty();
    if (has_bound_key_) {
      bound_key_ = std::move(upper_doc_key);
      db_iter_->SetUpperbound(bound_key_);
    }
  } else {
    has_bound_key_ = !lower_doc_key.empty();
    if (has_bound_key_) {
      bound_key_ = std::move(lower_doc_key);
    }
  }

  if (!VERIFY_RESULT(InitScanChoices(doc_spec,
        !is_forward_scan_ && has_bound_key_ ? bound_key_ : lower_doc_key,
        is_forward_scan_ && has_bound_key_ ? bound_key_ : upper_doc_key))) {
    if (is_forward_scan_) {
      VLOG(3) << __PRETTY_FUNCTION__ << " Seeking to " << DocKey::DebugSliceToString(lower_doc_key);
      db_iter_->Seek(lower_doc_key);
    } else {
      // TODO consider adding an operator bool to DocKey to use instead of empty() here.
      if (!upper_doc_key.empty()) {
        db_iter_->PrevDocKey(upper_doc_key);
      } else {
        db_iter_->SeekToLastDocKey();
      }
    }
  }

  return Status::OK();
}

Status DocRowwiseIterator::Init(const QLScanSpec& spec) {
  table_type_ = TableType::YQL_TABLE_TYPE;
  return DoInit(down_cast<const DocQLScanSpec&>(spec));
}

Status DocRowwiseIterator::Init(const PgsqlScanSpec& spec) {
  table_type_ = TableType::PGSQL_TABLE_TYPE;
  ignore_ttl_ = true;
  return DoInit(down_cast<const DocPgsqlScanSpec&>(spec));
}

Status DocRowwiseIterator::AdvanceIteratorToNextDesiredRow() const {
  if (scan_choices_) {
    if (!IsNextStaticColumn()
        && !scan_choices_->CurrentTargetMatchesKey(row_key_)) {
      return scan_choices_->SeekToCurrentTarget(db_iter_.get());
    }
  } else {
    if (!is_forward_scan_) {
      VLOG(4) << __PRETTY_FUNCTION__ << " setting as PrevDocKey";
      db_iter_->PrevDocKey(row_key_);
    }
  }

  return Status::OK();
}

Result<bool> DocRowwiseIterator::HasNext() const {
  VLOG(4) << __PRETTY_FUNCTION__;

  // Repeated HasNext calls (without Skip/NextRow in between) should be idempotent:
  // 1. If a previous call failed we returned the same status.
  // 2. If a row is already available (row_ready_), return true directly.
  // 3. If we finished all target rows for the scan (done_), return false directly.
  RETURN_NOT_OK(has_next_status_);
  if (row_ready_) {
    // If row is ready, then HasNext returns true.
    return true;
  }
  if (done_) {
    return false;
  }

  bool doc_found = false;
  while (!doc_found) {
    if (!db_iter_->valid() || (scan_choices_ && scan_choices_->FinishedWithScanChoices())) {
      done_ = true;
      return false;
    }

    const auto key_data = db_iter_->FetchKey();
    if (!key_data.ok()) {
      VLOG(4) << __func__ << ", key data: " << key_data.status();
      has_next_status_ = key_data.status();
      return has_next_status_;
    }

    VLOG(4) << "*fetched_key is " << SubDocKey::DebugSliceToString(key_data->key);
    if (debug_dump_) {
      LOG(INFO) << __func__ << ", fetched key: " << SubDocKey::DebugSliceToString(key_data->key)
                << ", " << key_data->key.ToDebugHexString();
    }

    // The iterator is positioned by the previous GetSubDocument call (which places the iterator
    // outside the previous doc_key). Ensure the iterator is pushed forward/backward indeed. We
    // check it here instead of after GetSubDocument() below because we want to avoid the extra
    // expensive FetchKey() call just to fetch and validate the key.
    if (!iter_key_.data().empty() &&
        (is_forward_scan_ ? iter_key_.CompareTo(key_data->key) >= 0
                          : iter_key_.CompareTo(key_data->key) <= 0)) {
      // TODO -- could turn this check off in TPCC?
      has_next_status_ = STATUS_SUBSTITUTE(Corruption, "Infinite loop detected at $0",
                                           FormatSliceAsStr(key_data->key));
      return has_next_status_;
    }
    iter_key_.Reset(key_data->key);
    VLOG(4) << " Current iter_key_ is " << iter_key_;

    const auto dockey_sizes = DocKey::EncodedHashPartAndDocKeySizes(iter_key_);
    if (!dockey_sizes.ok()) {
      has_next_status_ = dockey_sizes.status();
      return has_next_status_;
    }
    row_hash_key_ = iter_key_.AsSlice().Prefix(dockey_sizes->hash_part_size);
    row_key_ = iter_key_.AsSlice().Prefix(dockey_sizes->doc_key_size);

    // e.g in cotable, row may point outside table bounds
    if (!DocKeyBelongsTo(row_key_, doc_read_context_.schema) ||
        (has_bound_key_ && is_forward_scan_ == (row_key_.compare(bound_key_) >= 0))) {
      done_ = true;
      return false;
    }

    // Prepare the DocKey to get the SubDocument. Trim the DocKey to contain just the primary key.
    Slice doc_key = row_key_;
    VLOG(4) << " sub_doc_key part of iter_key_ is " << DocKey::DebugSliceToString(doc_key);

    bool is_static_column = IsNextStaticColumn();
    if (scan_choices_ && !is_static_column) {
      if (!scan_choices_->CurrentTargetMatchesKey(row_key_)) {
        // We must have seeked past the target key we are looking for (no result) so we can safely
        // skip all scan targets between the current target and row key (excluding row_key_ itself).
        // Update the target key and iterator and call HasNext again to try the next target.
        RETURN_NOT_OK(scan_choices_->SkipTargetsUpTo(row_key_));

        // We updated scan target above, if it goes past the row_key_ we will seek again, and
        // process the found key in the next loop.
        if (!scan_choices_->CurrentTargetMatchesKey(row_key_)) {
          RETURN_NOT_OK(scan_choices_->SeekToCurrentTarget(db_iter_.get()));
          continue;
        }
      }
      // We found a match for the target key or a static column, so we move on to getting the
      // SubDocument.
    }
    if (doc_reader_ == nullptr) {
      doc_reader_ = std::make_unique<DocDBTableReader>(
          db_iter_.get(), deadline_, &projection_subkeys_, table_type_,
          doc_read_context_.schema_packing_storage);
      RETURN_NOT_OK(doc_reader_->UpdateTableTombstoneTime(doc_key));
      if (!ignore_ttl_) {
        doc_reader_->SetTableTtl(doc_read_context_.schema);
      }
    }

    row_ = SubDocument();
    auto doc_found_res = doc_reader_->Get(doc_key, &row_);
    if (!doc_found_res.ok()) {
      has_next_status_ = doc_found_res.status();
      return has_next_status_;
    } else {
      doc_found = *doc_found_res;
    }
    if (scan_choices_ && !is_static_column) {
      has_next_status_ = scan_choices_->DoneWithCurrentTarget();
      RETURN_NOT_OK(has_next_status_);
    }
    has_next_status_ = AdvanceIteratorToNextDesiredRow();
    RETURN_NOT_OK(has_next_status_);
    VLOG(4) << __func__ << ", iter: " << db_iter_->valid();
  }
  row_ready_ = true;
  return true;
}

string DocRowwiseIterator::ToString() const {
  return "DocRowwiseIterator";
}

namespace {

// Set primary key column values (hashed or range columns) in a QL row value map.
Status SetQLPrimaryKeyColumnValues(const Schema& schema,
                                           const size_t begin_index,
                                           const size_t column_count,
                                           const char* column_type,
                                           DocKeyDecoder* decoder,
                                           QLTableRow* table_row) {
  if (begin_index + column_count > schema.num_columns()) {
    return STATUS_SUBSTITUTE(
        Corruption,
        "$0 primary key columns between positions $1 and $2 go beyond table columns $3",
        column_type, begin_index, begin_index + column_count - 1, schema.num_columns());
  }
  KeyEntryValue key_entry_value;
  for (size_t i = 0, j = begin_index; i < column_count; i++, j++) {
    const auto ql_type = schema.column(j).type();
    QLTableColumn& column = table_row->AllocColumn(schema.column_id(j));
    RETURN_NOT_OK(decoder->DecodeKeyEntryValue(&key_entry_value));
    key_entry_value.ToQLValuePB(ql_type, &column.value);
  }
  return decoder->ConsumeGroupEnd();
}

} // namespace

void DocRowwiseIterator::SkipRow() {
  row_ready_ = false;
}

HybridTime DocRowwiseIterator::RestartReadHt() {
  auto max_seen_ht = db_iter_->max_seen_ht();
  if (max_seen_ht.is_valid() && max_seen_ht > db_iter_->read_time().read) {
    VLOG(4) << "Restart read: " << max_seen_ht << ", original: " << db_iter_->read_time();
    return max_seen_ht;
  }
  return HybridTime::kInvalid;
}

bool DocRowwiseIterator::IsNextStaticColumn() const {
  return doc_read_context_.schema.has_statics() && row_hash_key_.end() + 1 == row_key_.end();
}

Status DocRowwiseIterator::DoNextRow(const Schema& projection, QLTableRow* table_row) {
  VLOG(4) << __PRETTY_FUNCTION__;

  if (PREDICT_FALSE(done_)) {
    return STATUS(NotFound, "end of iter");
  }

  // Ensure row is ready to be read. HasNext() must be called before reading the first row, or
  // again after the previous row has been read or skipped.
  if (!row_ready_) {
    return STATUS(InternalError, "next row has not be prepared for reading");
  }

  DocKeyDecoder decoder(row_key_);
  RETURN_NOT_OK(decoder.DecodeCotableId());
  RETURN_NOT_OK(decoder.DecodeColocationId());
  bool has_hash_components = VERIFY_RESULT(decoder.DecodeHashCode());

  // Populate the key column values from the doc key. The key column values in doc key were
  // written in the same order as in the table schema (see DocKeyFromQLKey). If the range columns
  // are present, read them also.
  if (has_hash_components) {
    RETURN_NOT_OK(SetQLPrimaryKeyColumnValues(
        doc_read_context_.schema, 0, doc_read_context_.schema.num_hash_key_columns(),
        "hash", &decoder, table_row));
  }
  if (!decoder.GroupEnded()) {
    RETURN_NOT_OK(SetQLPrimaryKeyColumnValues(
        doc_read_context_.schema, doc_read_context_.schema.num_hash_key_columns(),
        doc_read_context_.schema.num_range_key_columns(), "range", &decoder, table_row));
  }

  for (size_t i = projection.num_key_columns(); i < projection.num_columns(); i++) {
    const auto& column_id = projection.column_id(i);
    const auto ql_type = projection.column(i).type();
    const SubDocument* column_value = row_.GetChild(KeyEntryValue::MakeColumnId(column_id));
    if (column_value != nullptr) {
      QLTableColumn& column = table_row->AllocColumn(column_id);
      column_value->ToQLValuePB(ql_type, &column.value);
      column.ttl_seconds = column_value->GetTtl();
      if (column_value->IsWriteTimeSet()) {
        column.write_time = column_value->GetWriteTime();
      }
    }
  }

  row_ready_ = false;
  return Status::OK();
}

bool DocRowwiseIterator::LivenessColumnExists() const {
  const SubDocument* subdoc = row_.GetChild(KeyEntryValue::kLivenessColumn);
  return subdoc != nullptr && subdoc->value_type() != ValueEntryType::kInvalid;
}

Status DocRowwiseIterator::GetNextReadSubDocKey(SubDocKey* sub_doc_key) const {
  if (db_iter_ == nullptr) {
    return STATUS(Corruption, "Iterator not initialized.");
  }

  // There are no more rows to fetch, so no next SubDocKey to read.
  if (!VERIFY_RESULT(HasNext())) {
    DVLOG(3) << "No Next SubDocKey";
    return Status::OK();
  }

  DocKey doc_key;
  RETURN_NOT_OK(doc_key.FullyDecodeFrom(row_key_));
  *sub_doc_key = SubDocKey(doc_key, read_time_.read);
  DVLOG(3) << "Next SubDocKey: " << sub_doc_key->ToString();
  return Status::OK();
}

Result<Slice> DocRowwiseIterator::GetTupleId() const {
  // Return tuple id without cotable id / colocation id if any.
  Slice tuple_id = row_key_;
  if (tuple_id.starts_with(KeyEntryTypeAsChar::kTableId)) {
    tuple_id.remove_prefix(1 + kUuidSize);
  } else if (tuple_id.starts_with(KeyEntryTypeAsChar::kColocationId)) {
    tuple_id.remove_prefix(1 + sizeof(ColocationId));
  }
  return tuple_id;
}

Result<bool> DocRowwiseIterator::SeekTuple(const Slice& tuple_id) {
  // If cotable id / colocation id is present in the table schema, then
  // we need to prepend it in the tuple key to seek.
  if (doc_read_context_.schema.has_cotable_id() || doc_read_context_.schema.has_colocation_id()) {
    uint32_t size = doc_read_context_.schema.has_colocation_id() ? sizeof(ColocationId) : kUuidSize;
    if (!tuple_key_) {
      tuple_key_.emplace();
      tuple_key_->Reserve(1 + size + tuple_id.size());

      if (doc_read_context_.schema.has_cotable_id()) {
        std::string bytes;
        doc_read_context_.schema.cotable_id().EncodeToComparable(&bytes);
        tuple_key_->AppendKeyEntryType(KeyEntryType::kTableId);
        tuple_key_->AppendRawBytes(bytes);
      } else {
        tuple_key_->AppendKeyEntryType(KeyEntryType::kColocationId);
        tuple_key_->AppendUInt32(doc_read_context_.schema.colocation_id());
      }
    } else {
      tuple_key_->Truncate(1 + size);
    }
    tuple_key_->AppendRawBytes(tuple_id);
    db_iter_->Seek(*tuple_key_);
  } else {
    db_iter_->Seek(tuple_id);
  }

  iter_key_.Clear();
  row_ready_ = false;

  return VERIFY_RESULT(HasNext()) && VERIFY_RESULT(GetTupleId()) == tuple_id;
}

}  // namespace docdb
}  // namespace yb
