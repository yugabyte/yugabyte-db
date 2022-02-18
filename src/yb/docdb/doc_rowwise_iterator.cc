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

#include "yb/common/common.pb.h"
#include "yb/common/partition.h"
#include "yb/common/transaction.h"
#include "yb/common/ql_expr.h"
#include "yb/common/ql_scanspec.h"
#include "yb/common/ql_value.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/doc_reader.h"
#include "yb/docdb/doc_scanspec_util.h"
#include "yb/docdb/doc_ttl_util.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/subdocument.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/util/flags.h"
#include "yb/rocksdb/db/compaction.h"
#include "yb/rocksutil/yb_rocksdb.h"

#include "yb/rocksdb/db.h"

#include "yb/util/flag_tags.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/strongly_typed_bool.h"

#include "yb/yql/pggate/util/pg_doc_data.h"

DEFINE_bool(disable_hybrid_scan, false,
            "If true, hybrid scan will be disabled");
TAG_FLAG(disable_hybrid_scan, runtime);

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
  virtual CHECKED_STATUS DoneWithCurrentTarget() = 0;

  // Go (directly) to the new target (or the one after if new_target does not
  // exist in the desired list/range). If the new_target is larger than all scan target options it
  // means we are done.
  virtual CHECKED_STATUS SkipTargetsUpTo(const Slice& new_target) = 0;

  // If the given doc_key isn't already at the desired target, seek appropriately to go to the
  // current target.
  virtual CHECKED_STATUS SeekToCurrentTarget(IntentAwareIterator* db_iter) = 0;

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
    range_cols_scan_options_ = doc_spec.range_options();
    current_scan_target_idxs_.resize(range_cols_scan_options_->size());
    for (int i = 0; i < range_cols_scan_options_->size(); i++) {
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
    current_scan_target_idxs_.resize(range_cols_scan_options_->size());
    for (int i = 0; i < range_cols_scan_options_->size(); i++) {
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

  CHECKED_STATUS DoneWithCurrentTarget() override;
  CHECKED_STATUS SkipTargetsUpTo(const Slice& new_target) override;
  CHECKED_STATUS SeekToCurrentTarget(IntentAwareIterator* db_iter) override;

 protected:
  // Utility function for (multi)key scans. Updates the target scan key by incrementing the option
  // index for one column. Will handle overflow by setting current column index to 0 and
  // incrementing the previous column instead. If it overflows at first column it means we are done,
  // so it clears the scan target idxs array.
  CHECKED_STATUS IncrementScanTargetAtColumn(size_t start_col);

  // Utility function for (multi)key scans to initialize the range portion of the current scan
  // target, scan target with the first option.
  // Only needed for scans that include the static row, otherwise Init will take care of this.
  Result<bool> InitScanTargetRangeGroupIfNeeded();

 private:
  // For (multi)key scans (e.g. selects with 'IN' condition on the range columns) we hold the
  // options for each range column as we iteratively seek to each target key.
  // e.g. for a query "h = 1 and r1 in (2,3) and r2 in (4,5) and r3 = 6":
  //  range_cols_scan_options_   [[2, 3], [4, 5], [6]] -- value options for each column.
  //  current_scan_target_idxs_  goes from [0, 0, 0] up to [1, 1, 0] -- except when including the
  //                             static row when it starts from [0, 0, -1] instead.
  //  current_scan_target_       goes from [1][2,4,6] up to [1][3,5,6] -- is the doc key containing,
  //                             for each range column, the value (option) referenced by the
  //                             corresponding index (updated along with current_scan_target_idxs_).
  std::shared_ptr<std::vector<std::vector<PrimitiveValue>>> range_cols_scan_options_;
  mutable std::vector<std::vector<PrimitiveValue>::const_iterator> current_scan_target_idxs_;
};

Status DiscreteScanChoices::IncrementScanTargetAtColumn(size_t start_col) {
  DCHECK_LE(start_col, current_scan_target_idxs_.size());

  // Increment start col, move backwards in case of overflow.
  int col_idx = start_col;
  for (; col_idx >= 0; col_idx--) {
    const auto& choices = range_cols_scan_options_->at(col_idx);
    auto& it = current_scan_target_idxs_[col_idx];

    if (++it != choices.end()) {
      break;
    }
    it = choices.begin();
  }

  if (col_idx < 0) {
    // If we got here we finished all the options and are done.
    finished_ = true;
    return Status::OK();
  }

  DocKeyDecoder decoder(current_scan_target_);
  RETURN_NOT_OK(decoder.DecodeToRangeGroup());
  for (int i = 0; i != col_idx; ++i) {
    RETURN_NOT_OK(decoder.DecodePrimitiveValue());
  }

  current_scan_target_.Truncate(
      decoder.left_input().cdata() - current_scan_target_.AsSlice().cdata());

  for (size_t i = col_idx; i <= start_col; ++i) {
    current_scan_target_idxs_[i]->AppendToKey(&current_scan_target_);
  }

  return Status::OK();
}

Result<bool> DiscreteScanChoices::InitScanTargetRangeGroupIfNeeded() {
  DocKeyDecoder decoder(current_scan_target_.AsSlice());
  RETURN_NOT_OK(decoder.DecodeToRangeGroup());

  // Initialize the range key values if needed (i.e. we scanned the static row until now).
  if (!VERIFY_RESULT(decoder.HasPrimitiveValue())) {
    current_scan_target_.mutable_data()->pop_back();
    for (size_t col_idx = 0; col_idx < range_cols_scan_options_->size(); col_idx++) {
      current_scan_target_idxs_[col_idx]->AppendToKey(&current_scan_target_);
    }
    current_scan_target_.AppendValueType(ValueType::kGroupEnd);
    return true;
  }
  return false;
}

Status DiscreteScanChoices::DoneWithCurrentTarget() {
  VLOG(2) << __PRETTY_FUNCTION__ << " moving on to next target";
  DCHECK(!FinishedWithScanChoices());

  // Initialize the first target/option if not done already, otherwise go to the next one.
  if (!VERIFY_RESULT(InitScanTargetRangeGroupIfNeeded())) {
    RETURN_NOT_OK(IncrementScanTargetAtColumn(range_cols_scan_options_->size() - 1));
    current_scan_target_.AppendValueType(ValueType::kGroupEnd);
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

  size_t col_idx = 0;
  PrimitiveValue target_value;
  while (col_idx < range_cols_scan_options_->size()) {
    RETURN_NOT_OK(decoder.DecodePrimitiveValue(&target_value));
    const auto& choices = (*range_cols_scan_options_)[col_idx];
    auto& it = current_scan_target_idxs_[col_idx];

    // Fast-path in case the existing value for this column already matches the new target.
    if (target_value == *it) {
      col_idx++;
      target_value.AppendToKey(&current_scan_target_);
      continue;
    }

    // Search for the option that matches new target value (for the current column).
    if (is_forward_scan_) {
      it = std::lower_bound(choices.begin(), choices.end(), target_value);
    } else {
      it = std::lower_bound(choices.begin(), choices.end(), target_value, std::greater<>());
    }

    // If we overflowed, the new target value for this column is larger than all our options, so
    // we go back and increment the previous column instead.
    if (it == choices.end()) {
      RETURN_NOT_OK(IncrementScanTargetAtColumn(col_idx - 1));
      break;
    }

    // Else, update the current target value for this column.
    it->AppendToKey(&current_scan_target_);

    // If we did not find an exact match we are already beyond the new target so we can stop.
    if (target_value != *it) {
      col_idx++;
      break;
    }

    col_idx++;
  }

  // If there are any columns left (i.e. we stopped early), it means we did not find an exact
  // match and we reached beyond the new target key. So we need to include all options for the
  // leftover columns (i.e. set all following indexes to 0).
  for (size_t i = col_idx; i < current_scan_target_idxs_.size(); i++) {
    current_scan_target_idxs_[i] = (*range_cols_scan_options_)[i].begin();
    current_scan_target_idxs_[i]->AppendToKey(&current_scan_target_);
  }

  current_scan_target_.AppendValueType(ValueType::kGroupEnd);

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
      tmp.AppendValueType(ValueType::kHighest);
      VLOG(2) << __PRETTY_FUNCTION__ << " Going to PrevDocKey " << tmp;
      db_iter->PrevDocKey(tmp);
    }
  }
  return Status::OK();
}

// This class combines the notions of option filters (col1 IN (1,2,3)) and
// singular range bound filters (col1 < 4 AND col1 >= 1) into a single notion of
// lists of ranges. So a filter for a column given in the
// Doc(QL/PGSQL)ScanSpec is converted into a range bound filter.
// In the end, each HybridScanChoices
// instance should have a sorted list of disjoint ranges to filter each column.
// Right now this supports a conjunction of range bound and discrete filters.
// Disjunctions are also supported but are UNTESTED.
// TODO: Test disjunctions when YSQL and YQL support pushing those down

class HybridScanChoices : public ScanChoices {
 public:

  // Constructs a list of ranges for each column from the given scanspec.
  // A filter of the form col1 IN (1,4,5) is converted to a filter
  // in the form col1 IN ([1, 1], [4, 4], [5, 5]).
  HybridScanChoices(const Schema& schema,
                    const KeyBytes &lower_doc_key,
                    const KeyBytes &upper_doc_key,
                    bool is_forward_scan,
                    const std::vector<ColumnId> &range_options_indexes,
                    const
                    std::shared_ptr<std::vector<std::vector<PrimitiveValue>>>&
                        range_options,
                    const std::vector<ColumnId> range_bounds_indexes,
                    const common::QLScanRange *range_bounds)
                    : ScanChoices(is_forward_scan),
                        lower_doc_key_(lower_doc_key),
                        upper_doc_key_(upper_doc_key) {
    auto range_cols_scan_options = range_options;
    size_t idx = 0;
    range_cols_scan_options_lower_.reserve(schema.num_range_key_columns());
    range_cols_scan_options_upper_.reserve(schema.num_range_key_columns());

    size_t num_hash_cols = schema.num_hash_key_columns();

    for (idx = schema.num_hash_key_columns();
            idx < schema.num_key_columns(); idx++) {
      const ColumnId col_idx = schema.column_id(idx);
      range_cols_scan_options_lower_.push_back({});
      range_cols_scan_options_upper_.push_back({});

      // If this is a range bound filter, we create a singular
      // list of the given range bound
      if ((std::find(range_bounds_indexes.begin(),
                        range_bounds_indexes.end(), col_idx)
                    != range_bounds_indexes.end())
            && (std::find(range_options_indexes.begin(),
                            range_options_indexes.end(), col_idx)
                        == range_options_indexes.end())) {
        const auto col_sort_type = schema.column(idx).sorting_type();
        const common::QLScanRange::QLRange range = range_bounds->RangeFor(col_idx);
        const auto lower = GetQLRangeBoundAsPVal(range, col_sort_type,
                                                    true /* lower_bound */);
        const auto upper = GetQLRangeBoundAsPVal(range, col_sort_type,
                                                    false /* upper_bound */);

        range_cols_scan_options_lower_[idx - num_hash_cols].push_back(lower);
        range_cols_scan_options_upper_[idx - num_hash_cols].push_back(upper);
      } else {

        // If this is an option filter, we turn each option into a
        // range bound to produce a list of singular range bounds
        if(std::find(range_options_indexes.begin(),
                        range_options_indexes.end(), col_idx)
                    != range_options_indexes.end()) {
          auto &options = (*range_cols_scan_options)[idx - num_hash_cols];

          if (options.empty()) {
            // If there is nothing specified in the IN list like in
            // SELECT * FROM ... WHERE c1 IN ();
            // then nothing should pass the filter.
            // To enforce this, we create a range bound (kHighest, kLowest)
            range_cols_scan_options_lower_[idx
              - num_hash_cols].push_back(PrimitiveValue(ValueType::kHighest));
            range_cols_scan_options_upper_[idx
              - num_hash_cols].push_back(PrimitiveValue(ValueType::kLowest));
          }

          for (auto val : options) {
            const auto lower = val;
            const auto upper = val;
            range_cols_scan_options_lower_[idx
              - num_hash_cols].push_back(lower);
            range_cols_scan_options_upper_[idx
              - num_hash_cols].push_back(upper);
          }

        } else {
            // If no filter is specified, we just impose an artificial range
            // filter [kLowest, kHighest]
            range_cols_scan_options_lower_[idx - num_hash_cols]
                                .push_back(PrimitiveValue(ValueType::kLowest));
            range_cols_scan_options_upper_[idx - num_hash_cols]
                                .push_back(PrimitiveValue(ValueType::kHighest));
        }
      }
    }

    current_scan_target_idxs_.resize(range_cols_scan_options_lower_.size());

    if (is_forward_scan_) {
      current_scan_target_ = lower_doc_key;
    } else {
      current_scan_target_ = upper_doc_key;
    }

  }

  HybridScanChoices(const Schema& schema,
                    const DocPgsqlScanSpec& doc_spec,
                    const KeyBytes &lower_doc_key,
                    const KeyBytes &upper_doc_key)
      : HybridScanChoices(schema, lower_doc_key, upper_doc_key,
      doc_spec.is_forward_scan(), doc_spec.range_options_indexes(),
      doc_spec.range_options(), doc_spec.range_bounds_indexes(),
      doc_spec.range_bounds()) {
  }

  HybridScanChoices(const Schema& schema,
                    const DocQLScanSpec& doc_spec,
                    const KeyBytes &lower_doc_key,
                    const KeyBytes &upper_doc_key)
      : HybridScanChoices(schema, lower_doc_key, upper_doc_key,
      doc_spec.is_forward_scan(), doc_spec.range_options_indexes(),
      doc_spec.range_options(), doc_spec.range_bounds_indexes(),
      doc_spec.range_bounds()) {
  }

  CHECKED_STATUS SkipTargetsUpTo(const Slice& new_target) override;
  CHECKED_STATUS DoneWithCurrentTarget() override;
  CHECKED_STATUS SeekToCurrentTarget(IntentAwareIterator* db_iter) override;

 protected:
  // Utility function for (multi)key scans. Updates the target scan key by
  // incrementing the option
  // index for one column. Will handle overflow by setting current column
  // index to 0 and incrementing the previous column instead. If it overflows
  // at first column it means we are done, so it clears the scan target idxs
  // array.
  CHECKED_STATUS IncrementScanTargetAtColumn(int start_col);

 private:
  KeyBytes prev_scan_target_;

  // The following encodes the list of ranges we are iterating over
  std::vector<std::vector<PrimitiveValue>> range_cols_scan_options_lower_;
  std::vector<std::vector<PrimitiveValue>> range_cols_scan_options_upper_;

  std::vector<ColumnId> range_options_indexes_;
  mutable std::vector<size_t> current_scan_target_idxs_;

  bool is_options_done_ = false;

  const KeyBytes lower_doc_key_;
  const KeyBytes upper_doc_key_;
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

    a b  0 d  -> a  b l_c  d

    a b  5 d  -> a  b  5   d
                  [ Will subsequently seek out of document on reading the subdoc]

    a b  7 d  -> a b l_c_(k+1) 0
                [ If there is another range bound filter that's higher than the
                  current one, effectively, moving this column to the next
                  range in the filter list.]
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
  */
  DocKeyDecoder decoder(new_target);
  RETURN_NOT_OK(decoder.DecodeToRangeGroup());
  current_scan_target_.Reset(Slice(new_target.data(),
                                decoder.left_input().data()));

  size_t col_idx = 0;
  PrimitiveValue target_value;
  for (col_idx = 0; col_idx < current_scan_target_idxs_.size(); col_idx++) {
    RETURN_NOT_OK(decoder.DecodePrimitiveValue(&target_value));
    const auto& lower_choices = (range_cols_scan_options_lower_)[col_idx];
    const auto& upper_choices = (range_cols_scan_options_upper_)[col_idx];
    auto current_ind = current_scan_target_idxs_[col_idx];
    DCHECK(current_ind < lower_choices.size());
    const auto& lower = lower_choices[current_ind];
    const auto& upper = upper_choices[current_ind];

    // If it's in range then good, continue after appending the target value
    // column.

    if (target_value >= lower && target_value <= upper) {
      target_value.AppendToKey(&current_scan_target_);
      continue;
    }

    // If target_value is not in the current range then we must find a range
    // that works for it.
    // If we are above all ranges then increment the index of the previous
    // column.
    // Else, target_value is below at least one range: find the lowest lower
    // bound above target_value and use that, this relies on the assumption
    // that all our filter ranges are disjoint.

    auto it = lower_choices.begin();
    size_t ind = 0;

    // Find an upper (lower) bound closest to target_value
    if (is_forward_scan_) {
      it = std::lower_bound(upper_choices.begin(),
                                upper_choices.end(), target_value);
      ind = it - upper_choices.begin();
    } else {
      it = std::lower_bound(lower_choices.begin(), lower_choices.end(),
              target_value, std::greater<>());
      ind = it - lower_choices.begin();
    }

    if (ind == lower_choices.size()) {
      // target value is higher than all range options and
      // we need to increment.
      RETURN_NOT_OK(IncrementScanTargetAtColumn(static_cast<int>(col_idx) - 1));
      col_idx = current_scan_target_idxs_.size();
      break;
    }

    current_scan_target_idxs_[col_idx] = ind;

    // If we are within a range then target_value itself should work.
    if (lower_choices[ind] <= target_value
        && upper_choices[ind] >= target_value) {
      target_value.AppendToKey(&current_scan_target_);
      continue;
    }

    // Otherwise we must set it to the next lower bound.
    // This only works as we are assuming all given ranges are
    // disjoint.

    DCHECK((is_forward_scan_ && lower_choices[ind] > target_value)
              || (!is_forward_scan_ && upper_choices[ind]
              < target_value));

    if (is_forward_scan_) {
      lower_choices[ind].AppendToKey(&current_scan_target_);
    } else {
      upper_choices[ind].AppendToKey(&current_scan_target_);
    }
    col_idx++;
    break;
  }

  // Reset the remaining range columns to lower bounds for forward scans
  // or upper bounds for backward scans.
  for (size_t i = col_idx; i < range_cols_scan_options_lower_.size(); i++) {
    current_scan_target_idxs_[i] = 0;
    if (is_forward_scan_) {
      range_cols_scan_options_lower_[i][0]
                    .AppendToKey(&current_scan_target_);
    } else {
      range_cols_scan_options_upper_[i][0]
                    .AppendToKey(&current_scan_target_);
    }
  }

  current_scan_target_.AppendValueType(ValueType::kGroupEnd);
  VLOG(2) << "After " << __PRETTY_FUNCTION__ << " current_scan_target_ is "
          << DocKey::DebugSliceToString(current_scan_target_);
  return Status::OK();
}

// Update the value at start column by setting it up for incrementing to the
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
Status HybridScanChoices::IncrementScanTargetAtColumn(int start_col) {

  VLOG(2) << __PRETTY_FUNCTION__
          << " Incrementing at " << start_col;

  // Increment start col, move backwards in case of overflow.
  int col_idx = start_col;
  // lower and upper here are taken relative to the scan order
  auto &lower_extremal_vector = is_forward_scan_
                          ? range_cols_scan_options_lower_
                            : range_cols_scan_options_upper_;
  auto &upper_extremal_vector = is_forward_scan_
                                ? range_cols_scan_options_upper_
                                  : range_cols_scan_options_lower_;
  DocKeyDecoder t_decoder(current_scan_target_);
  RETURN_NOT_OK(t_decoder.DecodeToRangeGroup());

  // refer to the documentation of this function to see what extremal
  // means here
  std::vector<bool> is_extremal;
  PrimitiveValue target_value;
  for (int i = 0; i <= col_idx; ++i) {
    RETURN_NOT_OK(t_decoder.DecodePrimitiveValue(&target_value));
    is_extremal.push_back(target_value ==
      upper_extremal_vector[i][current_scan_target_idxs_[i]]);
  }

  // this variable tells us whether we start by appending
  // kHighest/kLowest at col_idx after the following for loop
  bool start_with_infinity = true;

  for (; col_idx >= 0; col_idx--) {
    const auto& choices = lower_extremal_vector[col_idx];
    auto it = current_scan_target_idxs_[col_idx];

    if (!is_extremal[col_idx]) {
      col_idx++;
      start_with_infinity = true;
      break;
    }

    if (++it < choices.size()) {
      // and if this value is at the extremal bound
      if (is_extremal[col_idx]) {
        current_scan_target_idxs_[col_idx]++;
        start_with_infinity = false;
      }
      break;
    }

    current_scan_target_idxs_[col_idx] = 0;
  }

  DocKeyDecoder decoder(current_scan_target_);
  RETURN_NOT_OK(decoder.DecodeToRangeGroup());
  for (int i = 0; i < col_idx; ++i) {
    RETURN_NOT_OK(decoder.DecodePrimitiveValue());
  }

  if (col_idx < 0) {
    // If we got here we finished all the options and are done.
    col_idx++;
    start_with_infinity = true;
    is_options_done_ = true;
  }

  current_scan_target_.Truncate(
      decoder.left_input().cdata() - current_scan_target_.AsSlice().cdata());


  if (start_with_infinity &&
        (col_idx < static_cast<int64>(current_scan_target_idxs_.size()))) {
    if (is_forward_scan_) {
      PrimitiveValue(ValueType::kHighest).AppendToKey(&current_scan_target_);
    } else {
      PrimitiveValue(ValueType::kLowest).AppendToKey(&current_scan_target_);
    }
    col_idx++;
  }

  if (start_with_infinity) {
    // there's no point in appending anything after infinity
    return Status::OK();
  }

  for (int i = col_idx; i <= start_col; ++i) {
      lower_extremal_vector[i][current_scan_target_idxs_[i]]
                                      .AppendToKey(&current_scan_target_);
  }

  for (size_t i = start_col + 1; i < current_scan_target_idxs_.size(); ++i) {
    current_scan_target_idxs_[i] = 0;
    lower_extremal_vector[i][current_scan_target_idxs_[i]]
                                    .AppendToKey(&current_scan_target_);
  }

  return Status::OK();
}

// Method called when the scan target is done being used
Status HybridScanChoices::DoneWithCurrentTarget() {
  // prev_scan_target_ is necessary for backwards scans
  prev_scan_target_ = current_scan_target_;
  RETURN_NOT_OK(IncrementScanTargetAtColumn(
                                  static_cast<int>(current_scan_target_idxs_.size()) - 1));
  current_scan_target_.AppendValueType(ValueType::kGroupEnd);

  // if we we incremented the last index then
  // if this is a forward scan it doesn't matter what we do
  // if this is a backwards scan then dont clear current_scan_target and we
  // stay live
  VLOG(2) << "After " << __PRETTY_FUNCTION__ << " current_scan_target_ is "
          << DocKey::DebugSliceToString(current_scan_target_);

  VLOG(2) << __PRETTY_FUNCTION__ << " moving on to next target";
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


  VLOG(4) << "current_scan_target_ is "
          << DocKey::DebugSliceToString(current_scan_target_)
          << " and prev_scan_target_ is "
          << DocKey::DebugSliceToString(prev_scan_target_);

  // The below condition is either indicative of the special case
  // where IncrementScanTargetAtColumn didn't change the target due
  // to the case specified in the last section of the
  // documentation for IncrementScanTargetAtColumn or we have exhausted
  // all available range keys for the given hash key (indicated
  // by is_options_done_)
  // We clear the scan target in these cases to indicate that the
  // current_scan_target_ has been used and is invalid
  // In all other cases, IncrementScanTargetAtColumn has updated
  // current_scan_target_ to the new value that we want to seek to.
  // Hence, we shouldn't clear it in those cases
  if ((prev_scan_target_ == current_scan_target_) || is_options_done_) {
      current_scan_target_.Clear();
  }

  return Status::OK();
}

// Seeks the given iterator to the current target as specified by
// current_scan_target_ and prev_scan_target_ (relevant in backwards
// scans)
Status HybridScanChoices::SeekToCurrentTarget(IntentAwareIterator* db_iter) {
  VLOG(2) << __PRETTY_FUNCTION__ << " Advancing iterator towards target";

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
        PrimitiveValue(ValueType::kHighest).AppendToKey(&tmp);
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
    size_t idx = 0;
    for (idx = schema.num_hash_key_columns(); idx < schema.num_key_columns(); idx++) {
      const ColumnId col_idx = schema.column_id(idx);
      const auto col_sort_type = schema.column(idx).sorting_type();
      const common::QLScanRange::QLRange range = doc_spec.range_bounds()->RangeFor(col_idx);
      const auto lower = GetQLRangeBoundAsPVal(range, col_sort_type, true /* lower_bound */);
      const auto upper = GetQLRangeBoundAsPVal(range, col_sort_type, false /* upper_bound */);
      lower_.emplace_back(lower);
      upper_.emplace_back(upper);
    }
  }

  RangeBasedScanChoices(const Schema& schema, const DocPgsqlScanSpec& doc_spec)
      : ScanChoices(doc_spec.is_forward_scan()) {
    DCHECK(doc_spec.range_bounds());
    lower_.reserve(schema.num_range_key_columns());
    upper_.reserve(schema.num_range_key_columns());
    int idx = 0;
    for (idx = schema.num_hash_key_columns(); idx < schema.num_key_columns(); idx++) {
      const ColumnId col_idx = schema.column_id(idx);
      const auto col_sort_type = schema.column(idx).sorting_type();
      const common::QLScanRange::QLRange range = doc_spec.range_bounds()->RangeFor(col_idx);
      const auto lower = GetQLRangeBoundAsPVal(range, col_sort_type, true /* lower_bound */);
      const auto upper = GetQLRangeBoundAsPVal(range, col_sort_type, false /* upper_bound */);
      lower_.emplace_back(lower);
      upper_.emplace_back(upper);
    }
  }

  CHECKED_STATUS SkipTargetsUpTo(const Slice& new_target) override;
  CHECKED_STATUS DoneWithCurrentTarget() override;
  CHECKED_STATUS SeekToCurrentTarget(IntentAwareIterator* db_iter) override;

 private:
  std::vector<PrimitiveValue> lower_, upper_;
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

  int col_idx = 0;
  PrimitiveValue target_value;
  bool last_was_infinity = false;
  for (col_idx = 0; VERIFY_RESULT(decoder.HasPrimitiveValue()); col_idx++) {
    RETURN_NOT_OK(decoder.DecodePrimitiveValue(&target_value));
    VLOG(3) << "col_idx " << col_idx << " is " << target_value << " in ["
            << yb::ToString(lower_[col_idx]) << " , " << yb::ToString(upper_[col_idx]) << " ] ?";

    const auto& lower = lower_[col_idx];
    if (target_value < lower) {
      const auto tgt = (is_forward_scan_ ? lower : PrimitiveValue(ValueType::kLowest));
      tgt.AppendToKey(&current_scan_target_);
      last_was_infinity = tgt.IsInfinity();
      VLOG(3) << " Updating idx " << col_idx << " from " << target_value << " to " << tgt;
      break;
    }
    const auto& upper = upper_[col_idx];
    if (target_value > upper) {
      const auto tgt = (!is_forward_scan_ ? upper : PrimitiveValue(ValueType::kHighest));
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
  current_scan_target_.AppendValueType(ValueType::kGroupEnd);
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
        PrimitiveValue(ValueType::kHighest).AppendToKey(&tmp);
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
    const Schema &schema,
    const TransactionOperationContextOpt& txn_op_context,
    const DocDB& doc_db,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    RWOperationCounter* pending_op_counter)
    : projection_(projection),
      schema_(schema),
      txn_op_context_(txn_op_context),
      deadline_(deadline),
      read_time_(read_time),
      doc_db_(doc_db),
      has_bound_key_(false),
      pending_op_(pending_op_counter),
      done_(false) {
  projection_subkeys_.reserve(projection.num_columns() + 1);
  projection_subkeys_.push_back(PrimitiveValue::kLivenessColumn);
  for (size_t i = projection_.num_key_columns(); i < projection.num_columns(); i++) {
    projection_subkeys_.emplace_back(projection.column_id(i));
  }
  std::sort(projection_subkeys_.begin(), projection_subkeys_.end());
}

DocRowwiseIterator::~DocRowwiseIterator() {
}

Status DocRowwiseIterator::Init(TableType table_type) {
  db_iter_ = CreateIntentAwareIterator(
      doc_db_,
      BloomFilterMode::DONT_USE_BLOOM_FILTER,
      boost::none /* user_key_for_filter */,
      rocksdb::kDefaultQueryId,
      txn_op_context_,
      deadline_,
      read_time_);
  DocKeyEncoder(&iter_key_).Schema(schema_);
  row_key_ = iter_key_;
  row_hash_key_ = row_key_;
  VLOG(3) << __PRETTY_FUNCTION__ << " Seeking to " << row_key_;
  db_iter_->Seek(row_key_);
  row_ready_ = false;
  has_bound_key_ = false;
  if (table_type == TableType::PGSQL_TABLE_TYPE) {
    ignore_ttl_ = true;
  }

  return Status::OK();
}

Result<bool> DocRowwiseIterator::InitScanChoices(
    const DocQLScanSpec& doc_spec, const KeyBytes& lower_doc_key, const KeyBytes& upper_doc_key) {

  if (!FLAGS_disable_hybrid_scan) {
    if (doc_spec.range_options() || doc_spec.range_bounds()) {
        scan_choices_.reset(new HybridScanChoices(schema_, doc_spec,
                                    lower_doc_key, upper_doc_key));
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
    scan_choices_.reset(new RangeBasedScanChoices(schema_, doc_spec));
  }

  return false;
}

Result<bool> DocRowwiseIterator::InitScanChoices(
    const DocPgsqlScanSpec& doc_spec, const KeyBytes& lower_doc_key,
    const KeyBytes& upper_doc_key) {

  if (!FLAGS_disable_hybrid_scan) {
    if (doc_spec.range_options() || doc_spec.range_bounds()) {
        scan_choices_.reset(new HybridScanChoices(schema_, doc_spec,
                                    lower_doc_key, upper_doc_key));
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
    scan_choices_.reset(new RangeBasedScanChoices(schema_, doc_spec));
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

Status DocRowwiseIterator::Init(const common::QLScanSpec& spec) {
  return DoInit(dynamic_cast<const DocQLScanSpec&>(spec));
}

Status DocRowwiseIterator::Init(const common::PgsqlScanSpec& spec) {
  ignore_ttl_ = true;
  return DoInit(dynamic_cast<const DocPgsqlScanSpec&>(spec));
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
      has_next_status_ = key_data.status();
      return has_next_status_;
    }

    VLOG(4) << "*fetched_key is " << SubDocKey::DebugSliceToString(key_data->key);

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
    row_hash_key_ = iter_key_.AsSlice().Prefix(dockey_sizes->first);
    row_key_ = iter_key_.AsSlice().Prefix(dockey_sizes->second);

    if (!DocKeyBelongsTo(row_key_, schema_) || // e.g in cotable, row may point outside table bounds
        (has_bound_key_ && is_forward_scan_ == (row_key_.compare(bound_key_) >= 0))) {
      done_ = true;
      return false;
    }

    // Prepare the DocKey to get the SubDocument. Trim the DocKey to contain just the primary key.
    Slice sub_doc_key = row_key_;
    VLOG(4) << " sub_doc_key part of iter_key_ is " << DocKey::DebugSliceToString(sub_doc_key);

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
      doc_reader_ = std::make_unique<DocDBTableReader>(db_iter_.get(), deadline_);
      RETURN_NOT_OK(doc_reader_->UpdateTableTombstoneTime(sub_doc_key));
      if (!ignore_ttl_) {
        doc_reader_->SetTableTtl(schema_);
      }
    }

    row_ = SubDocument();
    auto doc_found_res = doc_reader_->Get(sub_doc_key, &projection_subkeys_, &row_);
    if (!doc_found_res.ok()) {
      has_next_status_ = doc_found_res.status();
      return has_next_status_;
    } else {
      doc_found = VERIFY_RESULT(doc_found_res);
    }
    if (scan_choices_ && !is_static_column) {
      has_next_status_ = scan_choices_->DoneWithCurrentTarget();
      RETURN_NOT_OK(has_next_status_);
    }
    has_next_status_ = AdvanceIteratorToNextDesiredRow();
    RETURN_NOT_OK(has_next_status_);
  }
  row_ready_ = true;
  return true;
}

string DocRowwiseIterator::ToString() const {
  return "DocRowwiseIterator";
}

namespace {

// Set primary key column values (hashed or range columns) in a QL row value map.
CHECKED_STATUS SetQLPrimaryKeyColumnValues(const Schema& schema,
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
  PrimitiveValue primitive_value;
  for (size_t i = 0, j = begin_index; i < column_count; i++, j++) {
    const auto ql_type = schema.column(j).type();
    QLTableColumn& column = table_row->AllocColumn(schema.column_id(j));
    RETURN_NOT_OK(decoder->DecodePrimitiveValue(&primitive_value));
    PrimitiveValue::ToQLValuePB(primitive_value, ql_type, &column.value);
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
  return schema_.has_statics() && row_hash_key_.end() + 1 == row_key_.end();
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
  RETURN_NOT_OK(decoder.DecodePgtableId());
  bool has_hash_components = VERIFY_RESULT(decoder.DecodeHashCode());

  // Populate the key column values from the doc key. The key column values in doc key were
  // written in the same order as in the table schema (see DocKeyFromQLKey). If the range columns
  // are present, read them also.
  if (has_hash_components) {
    RETURN_NOT_OK(SetQLPrimaryKeyColumnValues(
        schema_, 0, schema_.num_hash_key_columns(),
        "hash", &decoder, table_row));
  }
  if (!decoder.GroupEnded()) {
    RETURN_NOT_OK(SetQLPrimaryKeyColumnValues(
        schema_, schema_.num_hash_key_columns(), schema_.num_range_key_columns(),
        "range", &decoder, table_row));
  }

  for (size_t i = projection.num_key_columns(); i < projection.num_columns(); i++) {
    const auto& column_id = projection.column_id(i);
    const auto ql_type = projection.column(i).type();
    const SubDocument* column_value = row_.GetChild(PrimitiveValue(column_id));
    if (column_value != nullptr) {
      QLTableColumn& column = table_row->AllocColumn(column_id);
      SubDocument::ToQLValuePB(*column_value, ql_type, &column.value);
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
  const SubDocument* subdoc = row_.GetChild(PrimitiveValue::kLivenessColumn);
  return subdoc != nullptr && subdoc->value_type() != ValueType::kInvalid;
}

CHECKED_STATUS DocRowwiseIterator::GetNextReadSubDocKey(SubDocKey* sub_doc_key) const {
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
  // Return tuple id without cotable id / pgtable id if any.
  Slice tuple_id = row_key_;
  if (tuple_id.starts_with(ValueTypeAsChar::kTableId)) {
    tuple_id.remove_prefix(1 + kUuidSize);
  } else if (tuple_id.starts_with(ValueTypeAsChar::kPgTableOid)) {
    tuple_id.remove_prefix(1 + sizeof(PgTableOid));
  }
  return tuple_id;
}

Result<bool> DocRowwiseIterator::SeekTuple(const Slice& tuple_id) {
  // If cotable id / pgtable id is present in the table schema, then
  // we need to prepend it in the tuple key to seek.
  if (schema_.has_cotable_id() || schema_.has_pgtable_id()) {
    uint32_t size = schema_.has_pgtable_id() ? sizeof(PgTableOid) : kUuidSize;
    if (!tuple_key_) {
      tuple_key_.emplace();
      tuple_key_->Reserve(1 + size + tuple_id.size());

      if (schema_.has_cotable_id()) {
        std::string bytes;
        schema_.cotable_id().EncodeToComparable(&bytes);
        tuple_key_->AppendValueType(ValueType::kTableId);
        tuple_key_->AppendRawBytes(bytes);
      } else {
        tuple_key_->AppendValueType(ValueType::kPgTableOid);
        tuple_key_->AppendUInt32(schema_.pgtable_id());
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
