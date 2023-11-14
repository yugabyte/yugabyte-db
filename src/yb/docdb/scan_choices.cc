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

#include "yb/qlexpr/ql_scanspec.h"
#include "yb/common/schema.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_path.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/doc_pgsql_scanspec.h"
#include "yb/qlexpr/doc_scanspec_util.h"
#include "yb/docdb/intent_aware_iterator_interface.h"
#include "yb/dockv/value_type.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {
namespace docdb {

using dockv::DocKey;
using dockv::DocKeyDecoder;
using dockv::KeyBytes;
using dockv::KeyEntryType;
using dockv::KeyEntryValue;

bool HybridScanChoices::CurrentTargetMatchesKey(Slice curr) {
  VLOG(3) << __PRETTY_FUNCTION__ << " checking if acceptable ? "
          << (!current_scan_target_.empty() &&
              curr.starts_with(current_scan_target_) ? "YEP" : "NOPE")
          << ": " << DocKey::DebugSliceToString(curr) << " vs "
          << DocKey::DebugSliceToString(current_scan_target_.AsSlice());
  return is_trivial_filter_ ||
      (!current_scan_target_.empty() && curr.starts_with(current_scan_target_));
}

HybridScanChoices::HybridScanChoices(
    const Schema& schema,
    const KeyBytes& lower_doc_key,
    const KeyBytes& upper_doc_key,
    bool is_forward_scan,
    const std::vector<ColumnId>& options_col_ids,
    const std::shared_ptr<std::vector<qlexpr::OptionList>>& options,
    const qlexpr::QLScanRange* range_bounds,
    const ColGroupHolder& col_groups,
    const size_t prefix_length)
    : is_forward_scan_(is_forward_scan),
      lower_doc_key_(lower_doc_key),
      upper_doc_key_(upper_doc_key),
      col_groups_(col_groups),
      prefix_length_(prefix_length) {
  size_t last_filtered_idx = static_cast<size_t>(-1);
  has_hash_columns_ = schema.has_yb_hash_code();
  num_hash_cols_ = schema.num_hash_key_columns();

  for (size_t idx = 0; idx < schema.num_dockey_components(); ++idx) {
    const auto col_id = GetColumnId(schema, idx);

    std::vector<OptionRange> current_options;
    bool col_has_range_option =
        std::find(options_col_ids.begin(), options_col_ids.end(), col_id) !=
        options_col_ids.end();

    auto col_has_range_bound = range_bounds ? range_bounds->column_has_range_bound(col_id) : false;

    // If this is a range bound filter, we create a singular
    // list of the given range bound
    if (col_has_range_bound && !col_has_range_option) {
      const auto col_sort_type =
          col_id.rep() == kYbHashCodeColId ? SortingType::kAscending
              : schema.column(schema.find_column_by_id( col_id)).sorting_type();
      const auto range = range_bounds->RangeFor(col_id);
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
        col_groups_.AddToLatestGroup(scan_options_.size());
      if (!upper.IsInfinity() || !lower.IsInfinity()) {
        last_filtered_idx = idx;
      }
    } else if (col_has_range_option) {
      const auto& temp_options = (*options)[idx];
      current_options.reserve(temp_options.size());
      if (temp_options.empty()) {
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
        auto last_option = *temp_options.begin();
        size_t begin = 0;
        size_t current_ind = 0;
        auto opt_list_idx = idx;
        auto group = col_groups_.GetGroup(opt_list_idx);
        DCHECK(std::is_sorted(group.begin(), group.end()));

        // We carry out run compression on all the options as described in the
        // comment for the OptionRange class.

        bool is_front = (group.front() == opt_list_idx);
        std::vector<OptionRange>::iterator prev_options_list_it;
        if (!is_front) {
          auto it = std::find(group.begin(), group.end(), opt_list_idx);
          --it;
          prev_options_list_it = scan_options_[*it].begin();
        }

        for (const auto& option : temp_options) {
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
      last_filtered_idx = idx;
    } else {
      // If no filter is specified, we just impose an artificial range
      // filter [kLowest, kHighest]
      col_groups_.BeginNewGroup();
      col_groups_.AddToLatestGroup(scan_options_.size());
      current_options.emplace_back(
        KeyEntryValue(KeyEntryType::kLowest),
        true,
        KeyEntryValue(KeyEntryType::kHighest),
        true,
        current_options.size(),
        current_options.size() + 1);
    }
    scan_options_.push_back(current_options);
  }

  // We add 1 to a valid prefix_length_ if there are hash columns
  // to account for the hash code column
  prefix_length_ += (prefix_length_ && has_hash_columns_);

  size_t filter_length = std::max(last_filtered_idx + 1, prefix_length_);
  DCHECK_LE(filter_length, scan_options_.size());

  scan_options_.resize(filter_length);
  is_trivial_filter_ = scan_options_.empty();

  current_scan_target_ranges_.resize(scan_options_.size());

  current_scan_target_.Clear();

  // Initialize current_scan_target_ranges_
  for (size_t i = 0; i < scan_options_.size(); i++) {
    current_scan_target_ranges_[i] = scan_options_.at(i).begin();
  }

  schema_num_keys_ = schema.num_dockey_components();
}

HybridScanChoices::HybridScanChoices(
    const Schema& schema,
    const qlexpr::YQLScanSpec& doc_spec,
    const KeyBytes& lower_doc_key,
    const KeyBytes& upper_doc_key)
    : HybridScanChoices(
          schema, lower_doc_key, upper_doc_key, doc_spec.is_forward_scan(),
          doc_spec.options_indexes(), doc_spec.options(), doc_spec.range_bounds(),
          doc_spec.options_groups(), doc_spec.prefix_length()) {}

std::vector<OptionRange>::const_iterator
HybridScanChoices::GetOptAtIndex(size_t opt_list_idx, size_t opt_index) const {
  if (col_groups_.GetGroup(opt_list_idx).back() == opt_list_idx) {
    // There shouldn't be any run-compression for elements at the back of a group.
    return scan_options_[opt_list_idx].begin() + opt_index;
  }

  auto current = current_scan_target_ranges_[opt_list_idx];
  if (current != scan_options_[opt_list_idx].end() &&
      current->HasIndex(opt_index)) {
    return current;
  }

  // Find which options begin_idx, end_idx range contains opt_index.
  OptionRange target_value_range({}, true, {}, true, opt_index, opt_index);
  auto option_it = std::lower_bound(scan_options_[opt_list_idx].begin(),
                                    scan_options_[opt_list_idx].end(),
                                    target_value_range,
                                    OptionRange::end_idx_leq);
  return option_it;
}

ColumnId HybridScanChoices::GetColumnId(const Schema& schema, size_t idx) const {
  return idx == 0 && has_hash_columns_ ? ColumnId(kYbHashCodeColId)
      : schema.column_id(idx - has_hash_columns_);
}

Status HybridScanChoices::DecodeKey(DocKeyDecoder* decoder, KeyEntryValue* target_value) const {
  RETURN_NOT_OK(decoder->DecodeKeyEntryValue(target_value));

  // We make sure to consume the kGroupEnd character if any.
  if (!decoder->left_input().empty()) {
    VERIFY_RESULT(decoder->HasPrimitiveValue(dockv::AllowSpecial::kTrue));
  }
  return Status::OK();
}

std::vector<OptionRange>::const_iterator
HybridScanChoices::GetSearchSpaceLowerBound(size_t opt_list_idx) const {
  auto group = col_groups_.GetGroup(opt_list_idx);
  if (group.front() == opt_list_idx)
    return scan_options_[opt_list_idx].begin();

  auto it = std::find(group.begin(), group.end(), opt_list_idx);
  DCHECK(it != group.end());
  DCHECK(it != group.begin());
  --it;

  auto prev_col_option = current_scan_target_ranges_[*it];
  return GetOptAtIndex(opt_list_idx, prev_col_option->begin_idx());
}

std::vector<OptionRange>::const_iterator
HybridScanChoices::GetSearchSpaceUpperBound(size_t opt_list_idx) const {
  auto group = col_groups_.GetGroup(opt_list_idx);
  if (group.front() == opt_list_idx)
    return scan_options_[opt_list_idx].end();

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
            - scan_options_[opt_list_idx].begin(),
            scan_options_[opt_list_idx].size());
}

void HybridScanChoices::SetGroup(size_t opt_list_idx, size_t opt_index) {
  auto group = col_groups_.GetGroup(opt_list_idx);
  for (auto elem : group) {
    SetOptToIndex(elem, opt_index);
  }
}

Result<bool> HybridScanChoices::SkipTargetsUpTo(Slice new_target) {
  VLOG(2) << __PRETTY_FUNCTION__
          << " Updating current target to be >= " << DocKey::DebugSliceToString(new_target);
  DCHECK(!Finished());
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
  RETURN_NOT_OK(decoder.DecodeToKeys());
  current_scan_target_.Reset(Slice(new_target.data(), decoder.left_input().data()));

  size_t option_list_idx = 0;
  for (option_list_idx = 0; option_list_idx < current_scan_target_ranges_.size();
       option_list_idx++) {
    const auto& options = scan_options_[option_list_idx];
    auto current_it = current_scan_target_ranges_[option_list_idx];
    DCHECK(current_it != options.end());

    KeyEntryValue target_value;
    auto decode_status = DecodeKey(&decoder, &target_value);
    if (!decode_status.ok()) {
      VLOG(1) << "Failed to decode the key: " << decode_status;
      // We return false to give the caller a chance to validate and skip past any keys that scan
      // choices should not be aware of before calling this again.

      // current_scan_target_ is left in a corrupted state so we must clear it.
      current_scan_target_.Clear();
      return false;
    }

    const auto *lower = &current_it->lower();
    const auto *upper = &current_it->upper();
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
    if (lower_cmp_fn(target_value, *lower) && upper_cmp_fn(target_value, *upper)) {
        AppendToScanTarget(target_value, option_list_idx);
      continue;
    }

    // If target_value is not in the current option then we must find an option
    // that works for it.
    // If we are above all ranges then increment the index of the previous
    // column.
    // Else, target_value is below at least one range: find the lowest lower
    // bound above target_value and use that, this relies on the assumption
    // that all our filter ranges are disjoint.

    // Find an upper (lower) bound closest to target_value
    auto begin = GetSearchSpaceLowerBound(option_list_idx);
    auto end = GetSearchSpaceUpperBound(option_list_idx);

    OptionRange target_value_range(target_value, true, target_value, true);
    auto it = is_forward_scan_
        ? std::lower_bound(begin, end, target_value_range, OptionRange::upper_lt)
        : std::lower_bound(begin, end, target_value_range, OptionRange::lower_gt);

    if (it == end) {
      // Target value is higher than all range options, and we need to increment.
      // If it is the first value, then it means that there are not more suitable values.
      if (option_list_idx == 0) {
        finished_ = true;
        return true;
      }
      RETURN_NOT_OK(IncrementScanTargetAtOptionList(option_list_idx - 1));
      option_list_idx = current_scan_target_ranges_.size();
      break;
    }

    size_t idx = is_forward_scan_? it->begin_idx() : it->end_idx() - 1;

    SetGroup(option_list_idx, idx);

    // If we are within a range then target_value itself should work

    lower = &it->lower();
    upper = &it->upper();

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

    if (target_value >= *lower && target_value <= *upper) {
      AppendToScanTarget(target_value, option_list_idx);
      if (lower_cmp_fn(target_value, *lower) && upper_cmp_fn(target_value, *upper)) {
        // target_value satisfies the current range condition.
        // Let's move on.
        continue;
      }

      // We're here because the strictness part of a bound is broken

      // If a strict upper bound is broken then we can increment
      // and move on to the next target

      DCHECK(target_value == *upper || target_value == *lower);

      if (is_forward_scan_ && target_value == *upper) {
        RETURN_NOT_OK(IncrementScanTargetAtOptionList(option_list_idx));
        option_list_idx = current_scan_target_ranges_.size();
        break;
      }

      if (!is_forward_scan_ && target_value == *lower) {
        RETURN_NOT_OK(IncrementScanTargetAtOptionList(option_list_idx));
        option_list_idx = current_scan_target_ranges_.size();
        break;
      }

      // If a strict lower bound is broken then we can simply append
      // a kHighest (kLowest) to get a target that satisfies the strict
      // lower bound
      AppendInfToScanTarget(option_list_idx);
      option_list_idx++;
      break;
    }

    // Otherwise we must set it to the next lower bound.
    // This only works as we are assuming all given ranges are
    // disjoint.

    DCHECK((is_forward_scan_ && *lower > target_value) ||
           (!is_forward_scan_ && *upper < target_value));

    // Here we append the lower bound + kLowest or upper bound + kHighest. Generally appending
    // to scan targets are always followed by a check if it has reached the last hash column.
    // This is to add a kGroundEnd after the last hash column. However, here we append them
    // directly and check for hash columns in the end. This is because, the possible combinations
    // of appending them is complex and hence we append to key on a case by case basis.
    if (is_forward_scan_) {
      AppendToScanTarget(*lower, option_list_idx);
      if (!lower_incl) {
        AppendInfToScanTarget(option_list_idx);
      }
    } else {
      AppendToScanTarget(*upper, option_list_idx);
      if (!upper_incl) {
        AppendInfToScanTarget(option_list_idx);
      }
    }
    option_list_idx++;
    break;
  }

  // Reset the remaining range columns to lower bounds for forward scans
  // or upper bounds for backward scans.
  for (size_t i = option_list_idx; i < scan_options_.size(); i++) {
    auto begin = GetSearchSpaceLowerBound(i);
    SetGroup(i, begin->begin_idx());

    auto current_it = current_scan_target_ranges_[i];
    auto lower_incl = current_it->lower_inclusive();
    auto upper_incl = current_it->upper_inclusive();

    const auto& range_bound = is_forward_scan_ ? current_scan_target_ranges_[i]->lower()
        : current_scan_target_ranges_[i]->upper();

    AppendToScanTarget(range_bound, i);
    if (is_forward_scan_ ? !lower_incl : !upper_incl) {
      AppendInfToScanTarget(i);
    }
  }

  DCHECK(VERIFY_RESULT(ValidateHashGroup(current_scan_target_)))
    << "current_scan_target_ validation failed: "
    << DocKey::DebugSliceToString(current_scan_target_);
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
Status HybridScanChoices::IncrementScanTargetAtOptionList(ssize_t start_option_list_idx) {
  VLOG_WITH_FUNC(2) << "Incrementing at " << start_option_list_idx;

  // Increment start col, move backwards in case of overflow.
  ssize_t option_list_idx = start_option_list_idx;
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
  RETURN_NOT_OK(t_decoder.DecodeToKeys());

  // refer to the documentation of this function to see what extremal
  // means here
  std::vector<bool> is_extremal;
  for (int i = 0; i <= option_list_idx; ++i) {
    KeyEntryValue target_value;
    RETURN_NOT_OK(DecodeKey(&t_decoder, &target_value));
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
      DCHECK(it != scan_options_[option_list_idx].end());
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
  RETURN_NOT_OK(decoder.DecodeToKeys());
  for (int i = 0; i < option_list_idx; ++i) {
    RETURN_NOT_OK(DecodeKey(&decoder));
  }

  if (option_list_idx < 0) {
    // If we got here we finished all the options and are done.
    option_list_idx++;
    start_with_infinity = true;
    is_options_done_ = true;
  }

  current_scan_target_.Truncate(
      decoder.left_input().cdata() - current_scan_target_.AsSlice().cdata());

  if (start_with_infinity) {
    if (option_list_idx < make_signed(schema_num_keys_)) {
      AppendInfToScanTarget(option_list_idx);
    }
    // there's no point in appending anything after infinity
    return Status::OK();
  }

  // Reset all columns that are > col_idx
  // We don't want to necessarily reset col_idx as it may
  // have been the case that we got here via an increment on col_idx
  ssize_t current_scan_target_ranges_size = current_scan_target_ranges_.size();
  for (auto i = option_list_idx; i < current_scan_target_ranges_size; ++i) {
    auto begin = GetSearchSpaceLowerBound(i);
    auto it_0 = i == option_list_idx ? current_scan_target_ranges_[i]
                                               : begin;
    // Potentially setting a group twice here but that can be easily dealt
    // with if necessary.
    SetGroup(i, it_0->begin_idx());

    AppendToScanTarget(lower_extremal_fn(*it_0), i);
    if (!lower_extremal_incl_fn(*it_0)) {
      AppendInfToScanTarget(i);
    }
  }

  VLOG_WITH_FUNC(2) << "Key after increment is "
                    << DocKey::DebugSliceToString(current_scan_target_);
  DCHECK(VERIFY_RESULT(ValidateHashGroup(current_scan_target_)))
    << "current_scan_target_ validation failed: "
    << DocKey::DebugSliceToString(current_scan_target_);
  return Status::OK();
}

// Validating Scan targets by checking if they have yb_hash_code, hash components and group end in
// order. We do not check range components as sometimes they can end without group ends. Sometimes
// seeks happens with kHighest and a groupend for hash split tables. In such a situation decoding
// hash code is not possible and hence, we validate only fully formed keys.
Result<bool> HybridScanChoices::ValidateHashGroup(const KeyBytes& scan_target) const {
  if (is_options_done_) {
    return true;
  }

  DocKeyDecoder t_decoder(scan_target);
  RETURN_NOT_OK(t_decoder.DecodeCotableId());
  RETURN_NOT_OK(t_decoder.DecodeColocationId());
  if (has_hash_columns_) {
    if (!VERIFY_RESULT(t_decoder.DecodeHashCode(dockv::AllowSpecial::kTrue))) {
      return false;
    }
    for (size_t i = 0; i < num_hash_cols_; i++) {
      RETURN_NOT_OK(t_decoder.DecodeKeyEntryValue());
    }
    RETURN_NOT_OK(t_decoder.ConsumeGroupEnd());
  }
  return true;
}

std::vector<OptionRange> HybridScanChoices::TEST_GetCurrentOptions() {
  std::vector<OptionRange> result;
  for (auto it : current_scan_target_ranges_) {
    result.push_back(*it);
  }
  return result;
}

// Method called when the scan target is done being used
Result<bool> HybridScanChoices::DoneWithCurrentTarget(bool current_row_skipped) {
  bool result = false;
  if (schema_num_keys_ == scan_options_.size() ||
      (prefix_length_ > 0 && !current_row_skipped)) {

    ssize_t incr_idx =
        (prefix_length_ ? prefix_length_ : current_scan_target_ranges_.size()) - 1;
    RETURN_NOT_OK(IncrementScanTargetAtOptionList(incr_idx));
    current_scan_target_.AppendKeyEntryType(KeyEntryType::kGroupEnd);
    result = true;
  }

  // if we incremented the last index then
  // if this is a forward scan it doesn't matter what we do
  // if this is a backwards scan then don't clear current_scan_target and we
  // stay live
  VLOG_WITH_FUNC(2) << "Current_scan_target_ is "
                    << DocKey::DebugSliceToString(current_scan_target_);
  VLOG_WITH_FUNC(2) << "Moving on to next target";

  DCHECK(!finished_);

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

  return result;
}

// Seeks the given iterator to the current target as specified by
// current_scan_target_ and prev_scan_target_ (relevant in backwards
// scans)
void HybridScanChoices::SeekToCurrentTarget(IntentAwareIteratorIf* db_iter) {
  VLOG_WITH_FUNC(2) << "pos: " << db_iter->DebugPosToString();

  if (finished_ || current_scan_target_.empty()) {
    return;
  }

  VLOG_WITH_FUNC(3)
      << "current_scan_target_ is non-empty. " << DocKey::DebugSliceToString(current_scan_target_);
  if (is_forward_scan_) {
    VLOG_WITH_FUNC(3) << "Seeking to " << DocKey::DebugSliceToString(current_scan_target_);
    db_iter->SeekForward(current_scan_target_);
  } else {
    // seek to the highest key <= current_scan_target_
    // seeking to the highest key < current_scan_target_ + kHighest
    // is equivalent to seeking to the highest key <=
    // current_scan_target_
    auto tmp = current_scan_target_;
    KeyEntryValue(KeyEntryType::kHighest).AppendToKey(&tmp);
    VLOG_WITH_FUNC(3) << "Going to PrevDocKey " << tmp;
    db_iter->PrevDocKey(tmp);
  }
}

Result<bool> HybridScanChoices::InterestedInRow(
    dockv::KeyBytes* row_key, IntentAwareIteratorIf* iter) {
  auto row = row_key->AsSlice();
  if (CurrentTargetMatchesKey(row)) {
    return true;
  }
  // We must have seeked past the target key we are looking for (no result) so we can safely
  // skip all scan targets between the current target and row key (excluding row_key_ itself).
  // Update the target key and iterator and call HasNext again to try the next target.
  if (!VERIFY_RESULT(SkipTargetsUpTo(row))) {
    // SkipTargetsUpTo returns false when it fails to decode the key.
    RSTATUS_DCHECK(
        VERIFY_RESULT(dockv::IsColocatedTableTombstoneKey(row)), Corruption,
        "Key $0 is not table tombstone key.", row.ToDebugHexString());
    if (is_forward_scan_) {
      iter->SeekOutOfSubDoc(row_key);
    } else {
      iter->PrevDocKey(row);
    }
    return false;
  }

  if (finished_) {
    return false;
  }

  // We updated scan target above, if it goes past the row_key_ we will seek again, and
  // process the found key in the next loop.
  if (CurrentTargetMatchesKey(row)) {
    return true;
  }

  SeekToCurrentTarget(iter);
  return false;
}

Result<bool> HybridScanChoices::AdvanceToNextRow(
    dockv::KeyBytes* row_key, IntentAwareIteratorIf* iter, bool current_fetched_row_skipped) {

  if (!VERIFY_RESULT(DoneWithCurrentTarget(current_fetched_row_skipped)) ||
      CurrentTargetMatchesKey(row_key->AsSlice())) {
    return false;
  }
  SeekToCurrentTarget(iter);
  return true;
}

void HybridScanChoices::AppendToScanTarget(const dockv::KeyEntryValue& target, size_t col_idx) {
  target.AppendToKey(&current_scan_target_);
  if (has_hash_columns_ && col_idx == num_hash_cols_) {
    current_scan_target_.AppendKeyEntryType(dockv::KeyEntryType::kGroupEnd);
  }
}

void HybridScanChoices::AppendInfToScanTarget(size_t col_idx) {
  if (has_hash_columns_ && col_idx == num_hash_cols_) {
    current_scan_target_.RemoveLastByte();
  }

  current_scan_target_.AppendKeyEntryType(
      is_forward_scan_ ? dockv::KeyEntryType::kHighest : dockv::KeyEntryType::kLowest);
}

class EmptyScanChoices : public ScanChoices {
 public:
  bool Finished() const override {
    return false;
  }

  Result<bool> InterestedInRow(dockv::KeyBytes* row_key, IntentAwareIteratorIf* iter) override {
    return true;
  }

  Result<bool> AdvanceToNextRow(dockv::KeyBytes* row_key,
                                IntentAwareIteratorIf* iter,
                                bool current_fetched_row_live) override {
    return false;
  }
};

ScanChoicesPtr ScanChoices::Create(
    const Schema& schema, const qlexpr::YQLScanSpec& doc_spec, const KeyBytes& lower_doc_key,
    const KeyBytes& upper_doc_key) {
  auto prefixlen = doc_spec.prefix_length();
  auto num_hash_cols = schema.num_hash_key_columns();
  auto num_key_cols = schema.num_key_columns();
  auto valid_prefixlen =
    prefixlen > 0 && prefixlen >= num_hash_cols && prefixlen <= num_key_cols;
  // Prefix must span at least all the hash columns since the first column is a hash code of all
  // hash columns in a hash partitioned table. And the hash code column cannot be skip'ed without
  // skip'ing all hash columns as well.
  if (prefixlen > 0 && !valid_prefixlen) {
    LOG(WARNING) << "Prefix length: " << prefixlen << " is invalid for schema: "
                  << "num_hash_cols: " << num_hash_cols << ", num_key_cols: " << num_key_cols;
  }
  if (doc_spec.options() || doc_spec.range_bounds() || valid_prefixlen) {
    return std::make_unique<HybridScanChoices>(schema, doc_spec, lower_doc_key, upper_doc_key);
  }

  return CreateEmpty();
}

ScanChoicesPtr ScanChoices::CreateEmpty() {
  return std::make_unique<EmptyScanChoices>();
}

std::string OptionRange::ToString() const {
  return Format(
      "$0$1, $2$3", lower_inclusive_ ? "[" : "(", lower_, upper_, upper_inclusive_ ? "]" : ")");
}

}  // namespace docdb
}  // namespace yb
