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

#include "yb/docdb/scan_choices.h"

#include "yb/common/ql_scanspec.h"
#include "yb/common/schema.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_path.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/doc_pgsql_scanspec.h"
#include "yb/docdb/doc_scanspec_util.h"
#include "yb/docdb/intent_aware_iterator_interface.h"
#include "yb/docdb/value_type.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {
namespace docdb {

bool ScanChoices::CurrentTargetMatchesKey(const Slice& curr) {
  VLOG(3) << __PRETTY_FUNCTION__ << " checking if acceptable ? "
          << (curr == current_scan_target_ ? "YEP" : "NOPE") << ": "
          << DocKey::DebugSliceToString(curr) << " vs "
          << DocKey::DebugSliceToString(current_scan_target_.AsSlice());
  return curr == current_scan_target_;
}

HybridScanChoices::HybridScanChoices(
  const Schema& schema,
  const KeyBytes& lower_doc_key,
  const KeyBytes& upper_doc_key,
  bool is_forward_scan,
  const std::vector<ColumnId>& range_options_col_ids,
  const std::shared_ptr<std::vector<OptionList>>& range_options,
  const std::vector<ColumnId>& range_bounds_col_ids,
  const QLScanRange* range_bounds,
  const ColGroupHolder& col_groups)
    : ScanChoices(is_forward_scan), lower_doc_key_(lower_doc_key),
      upper_doc_key_(upper_doc_key), col_groups_(col_groups) {
  size_t num_hash_cols = schema.num_hash_key_columns();

  for (size_t idx = num_hash_cols; idx < schema.num_key_columns(); idx++) {
    const ColumnId col_id = schema.column_id(idx);
    std::vector<OptionRange> current_options;
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
        lower,
        GetQLRangeBoundIsInclusive(range, col_sort_type, true),
        upper,
        GetQLRangeBoundIsInclusive(range, col_sort_type, false),
        current_options.size(),
        current_options.size() + 1);

        col_groups_.BeginNewGroup();
        col_groups_.AddToLatestGroup(idx - num_hash_cols);
    } else if (col_has_range_option) {
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
            KeyEntryValue(KeyEntryType::kHighest),
            true,
            KeyEntryValue(KeyEntryType::kLowest),
            true,
            current_options.size(),
            current_options.size() + 1);
      } else {
        auto last_option = *options.begin();
        size_t begin = 0;
        size_t current_ind = 0;
        auto opt_list_idx = idx - num_hash_cols;
        auto group = col_groups_.GetGroup(opt_list_idx);

        // We carry out run compression on all the options as described in the
        // comment for the OptionRange class.

        bool is_front = group.front() == (opt_list_idx);
        std::vector<OptionRange>::iterator prev_options_list_it;
        if (!is_front) {
          auto it = std::find(group.begin(), group.end(), opt_list_idx);
          --it;
          prev_options_list_it = range_cols_scan_options_[*it].begin();
        }

        for (auto option : options) {
          // If we're moving to a new option value or we are crossing boundaries
          // across options for the previous options list then we push a new
          // option for this list.
          if (option != last_option ||
              (!is_front && prev_options_list_it->end_idx() == current_ind)) {
            current_options.emplace_back(last_option, true, last_option,
                                          true, begin, current_ind);
            last_option = option;
            begin = current_ind;
            if (!is_front && prev_options_list_it->end_idx() == current_ind)
              prev_options_list_it++;
          }
          current_ind++;
        }
        current_options.emplace_back(last_option, true, last_option,
                                      true, begin, current_ind);
      }
    } else {
      // If no filter is specified, we just impose an artificial range
      // filter [kLowest, kHighest]
      col_groups_.BeginNewGroup();
      col_groups_.AddToLatestGroup(range_cols_scan_options_.size());
      current_options.emplace_back(
        KeyEntryValue(KeyEntryType::kLowest),
        true,
        KeyEntryValue(KeyEntryType::kHighest),
        true,
        current_options.size(),
        current_options.size() + 1);
    }
    range_cols_scan_options_.push_back(current_options);
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

HybridScanChoices::HybridScanChoices(
const Schema& schema,
const DocPgsqlScanSpec& doc_spec,
const KeyBytes& lower_doc_key,
const KeyBytes& upper_doc_key)
: HybridScanChoices(
  schema, lower_doc_key, upper_doc_key, doc_spec.is_forward_scan(),
  doc_spec.range_options_indexes(), doc_spec.range_options(),
  doc_spec.range_bounds_indexes(), doc_spec.range_bounds(),
  doc_spec.range_options_groups()) {}

HybridScanChoices::HybridScanChoices(
  const Schema& schema,
  const DocQLScanSpec& doc_spec,
  const KeyBytes& lower_doc_key,
  const KeyBytes& upper_doc_key)
  : HybridScanChoices(
    schema, lower_doc_key, upper_doc_key, doc_spec.is_forward_scan(),
    doc_spec.range_options_indexes(), doc_spec.range_options(),
    doc_spec.range_bounds_indexes(), doc_spec.range_bounds(),
    doc_spec.range_options_groups()) {}

std::vector<OptionRange>::const_iterator
HybridScanChoices::GetOptAtIndex(size_t opt_list_idx, size_t opt_index) {
  if (col_groups_.GetGroup(opt_list_idx).back() == opt_list_idx) {
    // There shouldn't be any run-compression for elements at the back of a group.
    return range_cols_scan_options_[opt_list_idx].begin() + opt_index;
  }

  auto current = current_scan_target_ranges_[opt_list_idx];
  if (current != range_cols_scan_options_[opt_list_idx].end() &&
      current->HasIndex(opt_index)) {
    return current;
  }

  // Find which options begin_idx, end_idx range contains opt_index.
  OptionRange target_value_range({}, true, {}, true, opt_index, opt_index);
  auto option_it = std::lower_bound(range_cols_scan_options_[opt_list_idx].begin(),
                                    range_cols_scan_options_[opt_list_idx].end(),
                                    target_value_range,
                                    OptionRange::end_idx_leq);
  return option_it;
}

std::vector<OptionRange>::const_iterator
HybridScanChoices::GetSearchSpaceLowerBound(size_t opt_list_idx) {
  auto group = col_groups_.GetGroup(opt_list_idx);
  if (group.front() == opt_list_idx)
    return range_cols_scan_options_[opt_list_idx].begin();

  auto it = std::find(group.begin(), group.end(), opt_list_idx);
  DCHECK(it != group.end());
  DCHECK(it != group.begin());
  --it;

  auto prev_col_option = current_scan_target_ranges_[*it];
  return GetOptAtIndex(opt_list_idx, prev_col_option->begin_idx());
}

std::vector<OptionRange>::const_iterator
HybridScanChoices::GetSearchSpaceUpperBound(size_t opt_list_idx) {
  auto group = col_groups_.GetGroup(opt_list_idx);
  if (group.front() == opt_list_idx)
    return range_cols_scan_options_[opt_list_idx].end();

  auto it = std::find(group.begin(), group.end(), opt_list_idx);
  DCHECK(it != group.end());
  DCHECK(it != group.begin());
  --it;

  auto prev_col_option = current_scan_target_ranges_[*it];
  return GetOptAtIndex(opt_list_idx, prev_col_option->end_idx());
}

void HybridScanChoices::SetOptToIndex(size_t opt_list_idx, size_t opt_index) {
  current_scan_target_ranges_[opt_list_idx] = GetOptAtIndex(opt_list_idx, opt_index);
  DCHECK_LT(current_scan_target_ranges_[opt_list_idx]
            - range_cols_scan_options_[opt_list_idx].begin(),
            range_cols_scan_options_[opt_list_idx].size());
}

void HybridScanChoices::SetGroup(size_t opt_list_idx, size_t opt_index) {
  auto group = col_groups_.GetGroup(opt_list_idx);
  for (auto elem : group) {
    SetOptToIndex(elem, opt_index);
  }
}

// Sets current_scan_target_ to the first tuple in the filter space
// that is >= new_target.
Result<bool> HybridScanChoices::SkipTargetsUpTo(const Slice& new_target) {
  VLOG(2) << __PRETTY_FUNCTION__
          << " Updating current target to be >= " << DocKey::DebugSliceToString(new_target);
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
    auto current_it = current_scan_target_ranges_[option_list_idx];
    DCHECK(current_it != options.end());

    KeyEntryValue target_value;
    auto decode_status = decoder.DecodeKeyEntryValue(&target_value);
    if (!decode_status.ok()) {
      VLOG(1) << "Failed to decode the key: " << decode_status;
      // We return false to give the caller a chance to validate and skip past any keys that scan
      // choices should not be aware of before calling this again.
      return false;
    }

    auto lower = current_it->lower();
    auto upper = current_it->upper();
    bool lower_incl = current_it->lower_inclusive();
    bool upper_incl = current_it->upper_inclusive();

    using kval_cmp_fn_t =
        std::function<bool(const KeyEntryValue&, const KeyEntryValue&)>;

    kval_cmp_fn_t lower_cmp_fn = lower_incl
                                     ? [](const KeyEntryValue& t1,
                                          const KeyEntryValue& t2) { return t1 >= t2; }
                                     : [](const KeyEntryValue& t1,
                                          const KeyEntryValue& t2) { return t1 > t2; };
    kval_cmp_fn_t upper_cmp_fn = upper_incl
                                     ? [](const KeyEntryValue& t1,
                                          const KeyEntryValue& t2) { return t1 <= t2; }
                                     : [](const KeyEntryValue& t1,
                                          const KeyEntryValue& t2) { return t1 < t2; };

    // If it's in range then good, continue after appending the target value
    // column.
    if (lower_cmp_fn(target_value, lower) && upper_cmp_fn(target_value, upper)) {
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

    auto it = options.begin();

    // Find an upper (lower) bound closest to target_value
    auto begin = GetSearchSpaceLowerBound(option_list_idx);
    auto end = GetSearchSpaceUpperBound(option_list_idx);

    OptionRange target_value_range(target_value, true, target_value, true);
    if (is_forward_scan_) {
      it = std::lower_bound(
          begin, end, target_value_range, OptionRange::upper_lt);
    } else {
      it = std::lower_bound(
          begin, end, target_value_range, OptionRange::lower_gt);
    }

    if (it == end) {
      // target value is higher than all range options and
      // we need to increment.
      RETURN_NOT_OK(IncrementScanTargetAtOptionList(static_cast<int>(option_list_idx) - 1));
      option_list_idx = current_scan_target_ranges_.size();
      break;
    }

    size_t idx = is_forward_scan_? it->begin_idx() : it->end_idx() - 1;

    SetGroup(option_list_idx, idx);

    // If we are within a range then target_value itself should work

    lower = it->lower();
    upper = it->upper();

    lower_incl = it->lower_inclusive();
    upper_incl = it->upper_inclusive();

    lower_cmp_fn = lower_incl ? [](const KeyEntryValue& t1,
                                   const KeyEntryValue& t2) { return t1 >= t2; }
                              : [](const KeyEntryValue& t1,
                                   const KeyEntryValue& t2) { return t1 > t2; };
    upper_cmp_fn = upper_incl ? [](const KeyEntryValue& t1,
                                   const KeyEntryValue& t2) { return t1 <= t2; }
                              : [](const KeyEntryValue& t1,
                                   const KeyEntryValue& t2) { return t1 < t2; };

    if (target_value >= lower && target_value <= upper) {
      target_value.AppendToKey(&current_scan_target_);
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
      lower.AppendToKey(&current_scan_target_);
      if (!lower_incl) {
        KeyEntryValue(KeyEntryType::kHighest).AppendToKey(&current_scan_target_);
      }
    } else {
      upper.AppendToKey(&current_scan_target_);
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
    auto begin = GetSearchSpaceLowerBound(i);
    SetGroup(i, begin->begin_idx());

    if (is_forward_scan_) {
      current_scan_target_ranges_[i]->lower().AppendToKey(&current_scan_target_);
    } else {
      current_scan_target_ranges_[i]->upper().AppendToKey(&current_scan_target_);
    }
  }

  current_scan_target_.AppendKeyEntryType(KeyEntryType::kGroupEnd);
  VLOG(2) << "After " << __PRETTY_FUNCTION__ << " current_scan_target_ is "
          << DocKey::DebugSliceToString(current_scan_target_);
  return true;
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
  using extremal_fn_t = std::function<const KeyEntryValue&(const OptionRange&)>;

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
    KeyEntryValue target_value;
    RETURN_NOT_OK(t_decoder.DecodeKeyEntryValue(&target_value));
    is_extremal.push_back(target_value == upper_extremal_fn(*current_scan_target_ranges_[i]));
  }

  // this variable tells us whether we start by appending
  // kHighest/kLowest at col_idx after the following for loop
  bool start_with_infinity = true;

  for (; option_list_idx >= 0; option_list_idx--) {
    if (!is_extremal[option_list_idx]) {
      option_list_idx++;
      start_with_infinity = true;
      break;
    }

    auto end = GetSearchSpaceUpperBound(option_list_idx);

    auto& it = current_scan_target_ranges_[option_list_idx];
    ++it;

    if (it != end) {
      // and if this value is at the extremal bound
      DCHECK(is_extremal[option_list_idx]);

      size_t idx = is_forward_scan_ ? it->begin_idx() : it->end_idx() - 1;
      SetGroup(option_list_idx, idx);
      DCHECK(it != range_cols_scan_options_[option_list_idx].end());
      // if we are AT the boundary of a strict bound then we
      // want to append an infinity after this column to satisfy
      // the strict bound requirement
      start_with_infinity = !lower_extremal_incl_fn(*it);
      if (start_with_infinity) {
        option_list_idx++;
      }
      break;
    }

    auto begin = GetSearchSpaceLowerBound(option_list_idx);

    // If it == end then we move onto incrementing the next column
    size_t idx = is_forward_scan_ ? begin->begin_idx() : begin->end_idx() - 1;
    SetGroup(option_list_idx, idx);
  }

  DocKeyDecoder decoder(current_scan_target_);
  RETURN_NOT_OK(decoder.DecodeToRangeGroup());
  for (int i = 0; i < option_list_idx; ++i) {
    RETURN_NOT_OK(decoder.DecodeKeyEntryValue());
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
    auto begin = GetSearchSpaceLowerBound(i);
    auto it_0 = i == option_list_idx ? current_scan_target_ranges_[i]
                                               : begin;
    // Potentially setting a group twice here but that can be easily dealt
    // with if necessary.
    SetGroup(i, it_0->begin_idx());

    lower_extremal_fn(*it_0).AppendToKey(&current_scan_target_);
    if (!lower_extremal_incl_fn(*it_0)) {
      if (is_forward_scan_) {
        KeyEntryValue(KeyEntryType::kHighest).AppendToKey(&current_scan_target_);
      } else {
        KeyEntryValue(KeyEntryType::kLowest).AppendToKey(&current_scan_target_);
      }
    }
  }

  VLOG_WITH_FUNC(2) << "Key after increment is "
                    << DocKey::DebugSliceToString(current_scan_target_);
  return Status::OK();
}

std::vector<OptionRange> HybridScanChoices::TEST_GetCurrentOptions() {
  std::vector<OptionRange> result;
  for (auto it : current_scan_target_ranges_) {
    result.push_back(*it);
  }
  return result;
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
  VLOG_WITH_FUNC(2) << "Current_scan_target_ is "
                    << DocKey::DebugSliceToString(current_scan_target_);
  VLOG_WITH_FUNC(2) << "Moving on to next target";

  DCHECK(!FinishedWithScanChoices());

  if (is_options_done_) {
    // It could be possible that we finished all our options but are not
    // done because we haven't hit the bound key yet. This would usually be
    // the case if we are moving onto the next hash key where we will
    // restart our range options.
    const KeyBytes& bound_key = is_forward_scan_ ? upper_doc_key_ : lower_doc_key_;
    finished_ = bound_key.empty()
                    ? false
                    : is_forward_scan_ == (current_scan_target_.CompareTo(bound_key) >= 0);
    VLOG(4) << "finished_ = " << finished_;
  }

  VLOG_WITH_FUNC(4) << "current_scan_target_ is "
                    << DocKey::DebugSliceToString(current_scan_target_)
                    << " and prev_scan_target_ is "
                    << DocKey::DebugSliceToString(prev_scan_target_);

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
Status HybridScanChoices::SeekToCurrentTarget(IntentAwareIteratorIf* db_iter) {
  VLOG(2) << __func__ << ", pos: " << db_iter->DebugPosToString();

  if (!FinishedWithScanChoices()) {
    // if current_scan_target_ is valid we use it to determine
    // what to seek to
    if (!current_scan_target_.empty()) {
      VLOG(3) << __PRETTY_FUNCTION__ << " current_scan_target_ is non-empty. "
              << DocKey::DebugSliceToString(current_scan_target_);
      if (is_forward_scan_) {
        VLOG(3) << __PRETTY_FUNCTION__ << " Seeking to "
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

ScanChoicesPtr ScanChoices::Create(
    const Schema& schema, const DocQLScanSpec& doc_spec,
    const KeyBytes& lower_doc_key, const KeyBytes& upper_doc_key) {
  if (doc_spec.range_options() || doc_spec.range_bounds()) {
    return std::make_unique<HybridScanChoices>(schema, doc_spec, lower_doc_key, upper_doc_key);
  }

  return nullptr;
}

ScanChoicesPtr ScanChoices::Create(
    const Schema& schema, const DocPgsqlScanSpec& doc_spec,
    const KeyBytes& lower_doc_key, const KeyBytes& upper_doc_key) {
  if (doc_spec.range_options() || doc_spec.range_bounds()) {
    return std::make_unique<HybridScanChoices>(schema, doc_spec, lower_doc_key, upper_doc_key);
  }

  return nullptr;
}

}  // namespace docdb
}  // namespace yb
