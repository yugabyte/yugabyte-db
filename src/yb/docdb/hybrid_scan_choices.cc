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

#include "yb/docdb/hybrid_scan_choices.h"

#include "yb/docdb/docdb_filter_policy.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/intent_aware_iterator.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/key_bytes.h"
#include "yb/dockv/value_type.h"

#include "yb/qlexpr/doc_scanspec_util.h"

DEFINE_RUNTIME_bool(enable_scan_choices_variable_bloom_filter, true,
    "Whether to use variable bloom filter when possible in ScanChoices");

namespace yb::docdb {

using dockv::DocKeyDecoder;
using dockv::KeyBytes;
using dockv::KeyEntryType;
using dockv::KeyEntryValue;
using dockv::DocKey;

namespace {

BoundComp CompareBound(Slice lhs, Slice rhs) {
  int res = lhs.compare(rhs);
  return res < 0 ? BoundComp::kIn
                 : (res > 0 ? BoundComp::kOut : BoundComp::kAtBound);
}

bool SatisfiesInclusivity(BoundComp match, bool inclusive) {
  return match == BoundComp::kIn || (inclusive && match == BoundComp::kAtBound);
}

void AppendTarget(KeyBytes& value, const KeyEntryValue& target) {
  target.AppendToKey(&value);
}

void AppendTarget(KeyBytes& value, Slice target) {
  value.AppendRawBytes(target);
}

bool BloomFilterAllowed(size_t num_options, const ScanOptions& options) {
  if (options.size() < num_options) {
    return false;
  }
  for (size_t i = 0; i != num_options; ++i) {
    if (std::ranges::any_of(options[i], [](const auto& option) { return !option.Fixed(); })) {
      return false;
    }
  }
  return true;
}

} // namespace

RangeMatch OptionRange::MatchEx(Slice value) const {
  if (fixed_point_) {
    int cmp = lower_.compare(value);
    return cmp == 0 ? RangeMatch{BoundComp::kAtBound, BoundComp::kAtBound}
                    : (cmp < 0 ? RangeMatch{BoundComp::kIn, BoundComp::kOut}
                               : RangeMatch{BoundComp::kOut, BoundComp::kIn});
  }
  return {CompareBound(lower_, value), CompareBound(value, upper_)};
}

bool OptionRange::SatisfiesInclusivity(RangeMatch match) const {
  return docdb::SatisfiesInclusivity(match.lower, lower_inclusive_) &&
         docdb::SatisfiesInclusivity(match.upper, upper_inclusive_);
}

bool OptionRange::Match(Slice value) const {
  if (fixed_point_) {
    return lower_ == value;
  }
  return docdb::SatisfiesInclusivity(CompareBound(lower_, value), lower_inclusive_) &&
         docdb::SatisfiesInclusivity(CompareBound(value, upper_), upper_inclusive_);
}

ScanTarget::ScanTarget(
    Slice table_key_prefix, bool is_forward, size_t last_hash_column, size_t num_keys)
    : table_key_prefix_size_(table_key_prefix.size()),
      is_forward_(is_forward),
      last_hash_column_(last_hash_column),
      num_keys_(num_keys),
      value_(table_key_prefix) {
}

bool ScanTarget::Match(Slice row_key) const {
  DCHECK(row_key.starts_with(Prefix()))
      << ToString() << ", " << DocKey::DebugSliceToString(row_key);
  if (value_.size() == table_key_prefix_size_) {
    return false;
  }
  return row_key.WithoutPrefix(table_key_prefix_size_).starts_with(WithoutPrefix());
}

Slice ScanTarget::AsSlice() const {
  return value_.AsSlice();
}

Slice ScanTarget::WithoutPrefix() const {
  return Slice(value_.data().data() + table_key_prefix_size_, value_.data().end());
}

Slice ScanTarget::Prefix() const {
  return Slice(value_.data().data(), table_key_prefix_size_);
}

size_t ScanTarget::PrefixSize() const {
  return table_key_prefix_size_;
}

void ScanTarget::Reset() {
  value_.Truncate(table_key_prefix_size_);
  has_inf_ = false;
  columns_.clear();
}

Slice ScanTarget::SliceUpToColumn(size_t column_idx) const {
  CHECK_LT(column_idx, columns_.size());
  CHECK_LE(columns_[column_idx].end, value_.AsSlice().size());
  return value_.AsSlice().Prefix(columns_[column_idx].end);
}

void ScanTarget::Truncate(size_t column_idx, bool add_inf) {
  columns_.resize(column_idx);
  value_.Truncate(columns_.empty() ? table_key_prefix_size_ : columns_.back().end);
  FixTail(add_inf);
}

template <class Value>
void ScanTarget::DoAppend(
    const Value& target, BoundComp upper_match, AddInfinity add_inf) {
  if (has_inf_) {
    return;
  }
  AppendTarget(value_, target);
  columns_.push_back(ColumnInfo {
    .end = value_.size(),
    .extremal = upper_match == BoundComp::kAtBound,
  });
  FixTail(add_inf);
}

void ScanTarget::Append(
    const dockv::KeyEntryValue& target, BoundComp upper_match, AddInfinity add_inf) {
  DoAppend(target, upper_match, add_inf);
}

void ScanTarget::Append(Slice target, BoundComp upper_match, AddInfinity add_inf) {
  DoAppend(target, upper_match, add_inf);
}

void ScanTarget::Append(const OptionRange& option) {
  Append(
      option.first_bound(is_forward_),
      option.fixed_point() ? BoundComp::kAtBound : BoundComp::kOut,
      AddInfinity(!option.first_bound_inclusive(is_forward_)));
}

void ScanTarget::FixTail(bool add_inf) {
  if (add_inf) {
    value_.AppendKeyEntryType(
        is_forward_ ? dockv::KeyEntryType::kHighest : dockv::KeyEntryType::kLowest);
  } else if (NeedGroupEnd(columns_.size())) {
    value_.AppendKeyEntryType(dockv::KeyEntryType::kGroupEnd);
  }
  has_inf_ = add_inf;
}

bool ScanTarget::NeedGroupEnd(size_t column_idx) const {
  return column_idx == last_hash_column_ || column_idx == num_keys_;
}

// Returns a human readable string representation of scan_target_.
//
// Appends a kGroupEnd to a copy of scan_target_ if it does not
// already have that.
std::string ScanTarget::ToString() const {
  auto tmp = value_;
  // Trailing character can be kGroupEnd even if it is not a group end
  // but it is a rare occurence and this logic is only for debugging purposes.
  // We do this lousy check to skip reasoning when exactly a kGroupEnd should be
  // added.
  if (!tmp.AsSlice().ends_with(dockv::KeyEntryTypeAsChar::kGroupEnd)) {
    tmp.AppendGroupEnd();
  }
  return DocKey::DebugSliceToString(tmp);
}

bool ScanTarget::IsExtremal(size_t column_idx) const {
  return columns_[column_idx].extremal;
}

bool HybridScanChoices::CurrentTargetMatchesKey(Slice curr, IntentAwareIterator* iter) {
  VLOG_WITH_FUNC(3) << "Checking if acceptable ? "
          << (scan_target_.Match(curr) ? "YEP" : "NOPE")
          << ": " << DocKey::DebugSliceToString(curr) << " vs "
          << scan_target_.ToString();
  if (!is_trivial_filter_ && !scan_target_.Match(curr)) {
    return false;
  }

  // Read restart logic: Match found => update checkpoint to latest.
  if (iter) {
    max_seen_ht_checkpoint_ = iter->ObtainMaxSeenHtCheckpoint();
  }
  return true;
}

HybridScanChoices::HybridScanChoices(
    const Schema& schema,
    const qlexpr::YQLScanSpec& doc_spec,
    const KeyBytes& lower_doc_key,
    const KeyBytes& upper_doc_key,
    Slice table_key_prefix)
    : is_forward_scan_(doc_spec.is_forward_scan()),
      scan_target_(
          table_key_prefix, is_forward_scan_,
          schema.has_yb_hash_code() ? schema.num_hash_key_columns() + 1
                                    : std::numeric_limits<size_t>::max(),
          schema.num_dockey_components()),
      has_hash_columns_(schema.has_yb_hash_code()),
      num_hash_cols_(schema.num_hash_key_columns()),
      lower_doc_key_(lower_doc_key),
      upper_doc_key_(upper_doc_key),
      col_groups_(doc_spec.options_groups()),
      prefix_length_(doc_spec.prefix_length()),
      schema_num_keys_(schema.num_dockey_components()),
      bloom_filter_options_(BloomFilterOptions::Inactive()),
      arena_(doc_spec.arena_ptr()) {
  auto last_filtered_idx = std::numeric_limits<size_t>::max();

  const auto& options_col_ids = doc_spec.options_indexes();
  const auto& options = doc_spec.options();
  const auto* range_bounds = doc_spec.range_bounds();
  auto options_list_to_string = [](const auto& list) {
    return CollectionToString(list, [](Slice option) { return option.ToDebugHexString(); });
  };
  VLOG_WITH_FUNC(4)
      << "Options indexes: " << AsString(options_col_ids)
      << ", options: "
      << (options ? CollectionToString(*options, options_list_to_string) : "<NULL>")
      << ", bounds: " << AsString(range_bounds);

  for (size_t idx = 0; idx < schema.num_dockey_components(); ++idx) {
    DCHECK_EQ(scan_options_.size(), idx);
    const auto col_id = GetColumnId(schema, idx);

    std::vector<OptionRange> current_options;
    bool col_has_range_option =
        std::find(options_col_ids.begin(), options_col_ids.end(), col_id) !=
        options_col_ids.end();

    auto col_has_range_bound = range_bounds ? range_bounds->column_has_range_bound(col_id) : false;

    if (col_has_range_option) {
      const auto& temp_options = (*options)[idx];
      VLOG_WITH_FUNC(4)
          << "Column: " << idx << "/" << col_id << " options: "
          << options_list_to_string(temp_options);

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
            Slice(&dockv::KeyEntryTypeAsChar::kHighest, 1),
            true,
            Slice(&dockv::KeyEntryTypeAsChar::kLowest, 1),
            true,
            current_options.size(),
            current_options.size() + 1);
      } else {
        const auto& group = col_groups_.GetGroup(idx);
        DCHECK(std::is_sorted(group.begin(), group.end()));

        // We carry out run compression on all the options as described in the
        // comment for the OptionRange class.

        const OptionRange* prev_col_option;
        if (group.front() == idx) {
          prev_col_option = nullptr;
        } else {
          auto it = std::find(group.begin(), group.end(), idx);
          prev_col_option = &scan_options_[*--it].front();
        }

        auto* last_option = &temp_options.front();
        size_t begin = 0;
        size_t option_index = 0;
        for (const auto& option : temp_options) {
          // If we're moving to a new option value or we are crossing boundaries
          // across options for the previous options list then we push a new
          // option for this list.
          if (option != *last_option ||
              (prev_col_option && prev_col_option->end_idx() == option_index)) {
            current_options.emplace_back(*last_option, begin, option_index);
            last_option = &option;
            begin = option_index;
            if (prev_col_option && prev_col_option->end_idx() == option_index) {
              ++prev_col_option;
            }
          }
          ++option_index;
        }
        current_options.emplace_back(*last_option, begin, option_index);
      }
      last_filtered_idx = idx;
    } else if (col_has_range_bound) {
      // If this is a range bound filter, we create a singular
      // list of the given range bound
      const auto col_sort_type =
          col_id.rep() == kYbHashCodeColId ? SortingType::kAscending
              : schema.column(schema.find_column_by_id(col_id)).sorting_type();
      const auto range = range_bounds->RangeFor(col_id);
      auto lower = range.BoundAsSlice(doc_spec.arena(), col_sort_type, qlexpr::BoundType::kLower);
      const auto lower_inclusive = range.BoundIsInclusive(col_sort_type, qlexpr::BoundType::kLower);
      auto upper = range.BoundAsSlice(doc_spec.arena(), col_sort_type, qlexpr::BoundType::kUpper);
      const auto upper_inclusive = range.BoundIsInclusive(col_sort_type, qlexpr::BoundType::kUpper);

      VLOG_WITH_FUNC(4)
          << "Column: " << idx << "/" << col_id << " range: "
          << (lower_inclusive ? "[" : "(") << lower.ToString() << "-" << upper.ToString()
          << (upper_inclusive ? "]" : ")");
      current_options.emplace_back(
          lower,
          lower_inclusive,
          upper,
          upper_inclusive,
          current_options.size(),
          current_options.size() + 1);

      col_groups_.BeginNewGroup();
      col_groups_.AddToLatestGroup(scan_options_.size());
      if (!IsInfinity(dockv::DecodeKeyEntryType(upper)) ||
          !IsInfinity(dockv::DecodeKeyEntryType(lower))) {
        last_filtered_idx = idx;
      }
    } else {
      VLOG_WITH_FUNC(4)
          << "Column: " << idx << "/" << col_id << " no filter";

      // If no filter is specified, we just impose an artificial range
      // filter [kLowest, kHighest]
      col_groups_.BeginNewGroup();
      col_groups_.AddToLatestGroup(scan_options_.size());
      current_options.emplace_back(
          Slice(&dockv::KeyEntryTypeAsChar::kLowest, 1),
          true,
          Slice(&dockv::KeyEntryTypeAsChar::kHighest, 1),
          true,
          current_options.size(),
          current_options.size() + 1);
    }
    scan_options_.push_back(std::move(current_options));
  }

  VLOG_WITH_FUNC(3) << "Options: " << AsString(scan_options_) << ", schema: " << schema.ToString();

  // We add 1 to a valid prefix_length_ if there are hash columns
  // to account for the hash code column
  prefix_length_ += prefix_length_ && has_hash_columns_;

  size_t filter_length = std::max(last_filtered_idx + 1, prefix_length_);
  DCHECK_LE(filter_length, scan_options_.size());

  scan_options_.resize(filter_length);
  is_trivial_filter_ = scan_options_.empty();

  current_scan_target_ranges_.resize(scan_options_.size());

  // Initialize current_scan_target_ranges_
  for (size_t i = 0; i < scan_options_.size(); i++) {
    current_scan_target_ranges_[i] = scan_options_[i].begin();
  }
}

Status HybridScanChoices::Init(const DocReadContext& doc_read_context) {
  num_bloom_filter_cols_ = doc_read_context.NumColumnsUsedByBloomFilterKey();
  bloom_filter_options_ = VERIFY_RESULT(BloomFilterOptions::Make(
      doc_read_context, lower_doc_key_, upper_doc_key_,
      FLAGS_enable_scan_choices_variable_bloom_filter && is_forward_scan_ &&
          BloomFilterAllowed(num_bloom_filter_cols_, scan_options_)));
  VLOG_WITH_FUNC(3)
      << "lower: " << DocKey::DebugSliceToString(lower_doc_key_)
      << ", upper: " << DocKey::DebugSliceToString(upper_doc_key_)
      << ", scan options: " << AsString(scan_options_)
      << ", bloom filter: " << bloom_filter_options_.mode()
      << ", schema_num_keys: " << schema_num_keys_ << ", scan_options: " << scan_options_.size()
      << ", prefix_length: " << prefix_length_;
  return Status::OK();
}

HybridScanChoices::OptionRangeIterator HybridScanChoices::GetOptAtIndex(
    size_t opt_list_idx, size_t opt_index) const {
  if (col_groups_.GetGroup(opt_list_idx).back() == opt_list_idx) {
    // There shouldn't be any run-compression for elements at the back of a group.
    return scan_options_[opt_list_idx].begin() + opt_index;
  }

  auto current = current_scan_target_ranges_[opt_list_idx];
  if (current != scan_options_[opt_list_idx].end() && current->HasIndex(opt_index)) {
    return current;
  }

  // Find which options begin_idx, end_idx range contains opt_index.
  auto option_it = std::lower_bound(
      scan_options_[opt_list_idx].begin(),
      scan_options_[opt_list_idx].end(),
      opt_index,
      [](const OptionRange& lhs, size_t opt_index) {
        return lhs.end_idx() <= opt_index;
      });
  return option_it;
}

ColumnId HybridScanChoices::GetColumnId(const Schema& schema, size_t idx) const {
  return idx == 0 && has_hash_columns_ ? ColumnId(kYbHashCodeColId)
      : schema.column_id(idx - has_hash_columns_);
}

std::pair<HybridScanChoices::OptionRangeIterator, HybridScanChoices::OptionRangeIterator>
HybridScanChoices::GetSearchSpaceBounds(
    size_t opt_list_idx) const {
  auto group = col_groups_.GetGroup(opt_list_idx);
  if (group.front() == opt_list_idx) {
    return {scan_options_[opt_list_idx].begin(), scan_options_[opt_list_idx].end()};
  }

  auto it = std::find(group.begin(), group.end(), opt_list_idx);
  DCHECK(it != group.end());
  DCHECK(it != group.begin());
  --it;

  auto prev_col_option = current_scan_target_ranges_[*it];
  return {GetOptAtIndex(opt_list_idx, prev_col_option->begin_idx()),
          GetOptAtIndex(opt_list_idx, prev_col_option->end_idx())};
}

void HybridScanChoices::SetOptToIndex(size_t opt_list_idx, size_t opt_index) {
  current_scan_target_ranges_[opt_list_idx] = GetOptAtIndex(opt_list_idx, opt_index);
  DCHECK_LT(current_scan_target_ranges_[opt_list_idx] - scan_options_[opt_list_idx].begin(),
            scan_options_[opt_list_idx].size());
}

void HybridScanChoices::SetGroup(size_t opt_list_idx, size_t opt_index) {
  auto group = col_groups_.GetGroup(opt_list_idx);
  for (auto elem : group) {
    SetOptToIndex(elem, opt_index);
  }
}

Result<bool> HybridScanChoices::SkipTargetsUpTo(Slice new_target) {
  VLOG_WITH_FUNC(2)
      << "Updating current target to be >= " << DocKey::DebugSliceToString(new_target)
      << ", ranges size: " << current_scan_target_ranges_.size();
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
  DCHECK(new_target.starts_with(scan_target_.Prefix()));
  DocKeyDecoder decoder(new_target.WithoutPrefix(scan_target_.PrefixSize()));
  scan_target_.Reset();

  size_t option_list_idx = 0;
  for (option_list_idx = 0; option_list_idx < current_scan_target_ranges_.size();
       option_list_idx++) {
    const auto& options = scan_options_[option_list_idx];
    DCHECK(current_scan_target_ranges_[option_list_idx] != options.end());
    const auto& current_option = *current_scan_target_ranges_[option_list_idx];

    KeyEntryValue target_value;
    Status decode_status;
    if (scan_target_.NeedGroupEnd(option_list_idx)) {
      decode_status = decoder.ConsumeGroupEnd();
    }
    auto target_value_start = decoder.left_input().data();
    if (decode_status.ok()) {
      decode_status = decoder.DecodeKeyEntryValue();
    }
    Slice target_value_slice(target_value_start, decoder.left_input().data());
    if (!decode_status.ok()) {
      LOG_WITH_FUNC(DFATAL) << "Failed to decode the key: " << decode_status;
      // We return false to give the caller a chance to validate and skip past any keys that scan
      // choices should not be aware of before calling this again.

      // scan_target_ is left in a corrupted state so we must clear it.
      scan_target_.Reset();
      return false;
    }

    // If it's in range then good, continue after appending the target value
    // column.
    if (auto match = current_option.MatchEx(target_value_slice);
        current_option.SatisfiesInclusivity(match)) {
      scan_target_.Append(target_value_slice, match.Last(is_forward_scan_));
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
    auto [begin, end] = GetSearchSpaceBounds(option_list_idx);

    auto it = is_forward_scan_
        ? std::lower_bound(begin, end, target_value_slice,
                           [](const OptionRange& lhs, Slice value) {
                             return lhs.upper() < value;
                           })
        : std::lower_bound(begin, end, target_value_slice,
                           [](const OptionRange& lhs, Slice value) {
                             return lhs.lower() > value;
                           });

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

    SetGroup(option_list_idx, it->first_idx(is_forward_scan_));

    // If we are within a range then target_value itself should work

    auto match = it->MatchEx(target_value_slice);
    if (match.lower != BoundComp::kOut && match.upper != BoundComp::kOut) {
      if (it->SatisfiesInclusivity(match)) {
        scan_target_.Append(target_value_slice, match.Last(is_forward_scan_));
        // target_value satisfies the current range condition.
        // Let's move on.
        continue;
      }

      // We're here because the strictness part of a bound is broken

      // If a strict upper bound is broken then we can increment
      // and move on to the next target

      auto match_last = match.Last(is_forward_scan_);
      if (match_last == BoundComp::kAtBound) {
        scan_target_.Append(target_value_slice, BoundComp::kAtBound);
        RETURN_NOT_OK(IncrementScanTargetAtOptionList(option_list_idx));
        option_list_idx = current_scan_target_ranges_.size();
        break;
      }

      // If a strict lower bound is broken then we can simply append
      // a kHighest (kLowest) to get a target that satisfies the strict
      // lower bound
      scan_target_.Append(target_value_slice, match_last, AddInfinity::kTrue);
      option_list_idx++;
      break;
    }

    // Otherwise we must set it to the next lower bound.
    // This only works as we are assuming all given ranges are
    // disjoint.

    // Here we append the lower bound + kLowest or upper bound + kHighest. Generally appending
    // to scan targets are always followed by a check if it has reached the last
    // hash column.
    // This is to add a kGroupEnd after the last hash column. However, here we append them
    // directly and check for hash columns in the end. This is because, the possible combinations
    // of appending them is complex and hence we append to key on a case by case basis.
    scan_target_.Append(*it);
    option_list_idx++;
    break;
  }

  // Reset the remaining range columns to lower bounds for forward scans
  // or upper bounds for backward scans.
  for (size_t i = option_list_idx; i < scan_options_.size(); i++) {
    SetGroup(i, GetSearchSpaceBounds(i).first->begin_idx());
    scan_target_.Append(*current_scan_target_ranges_[i]);
  }

  DCHECK(VERIFY_RESULT(ValidateHashGroup()))
    << "scan_target_ validation failed: "
    << scan_target_.ToString();
  VLOG_WITH_FUNC(2) << "current_scan_target is " << scan_target_.ToString();
  if (is_options_done_ && bloom_filter_options_.mode() == BloomFilterMode::kVariable) {
    finished_ = true;
  }
  RETURN_NOT_OK(UpdateUpperBound(nullptr));
  return true;
}

// Update the value at start OptionList by setting it up for incrementing to the
// next allowed value in the filter space
// ---------------------------------------------------------------------------
// There are two important cases to consider here.
// Let's say the value of scan_target_ at start_col, c,
// is currently V and the current bounds for that column
// is l_c_k <= V <= u_c_k. In the usual case where V != u_c_k
// (or V != l_c_k for backwards scans) such that V_next is still in the given
// restriction, we set column c + 1 to kHighest (kLowest), such that the next
// invocation of GetNext() produces V_next at column similar to what is done
// in SkipTargetsUpTo. In this case, doing a SkipTargetsUpTo on the resulting
// scan_target_ should yield the next allowed value in the filter space
// In the case where V = u_c_k (V = l_c_k), or in other words V is at the
// EXTREMAL boundary of the current range, we know exactly what the next value
// of column C will be. So we move column c to the next
// range k+1 and set that column to the new value l_c_(k+1) (u_c_(k+1))
// while setting all columns, b > c to l_b_0 (u_b_0)
// In the case of overflow on a column c (we want to increment the
// restriction range of c to the next range bound for that column but there
// are no restriction ranges remaining), we set the
// current column to the 0th range and move on to increment c - 1
// Note that in almost all cases the resulting scan_target_ is strictly
// greater (lesser in the case of backwards scans) than the original
// scan_target_. This is necessary to allow the iterator seek out
// of the current scan target. The exception to this rule is below.
// ---------------------------------------------------------------------------
// This function leaves the scan target as is if the next tuple in the current
// scan direction is also the next tuple in the filter space and start_col
// is given as the last column
Status HybridScanChoices::IncrementScanTargetAtOptionList(ssize_t start_option_list_idx) {
  VLOG_WITH_FUNC(2)
      << "Incrementing at " << start_option_list_idx
      << ", current: " << scan_target_.ToString();

  // Increment start col, move backwards in case of overflow.
  ssize_t option_list_idx = start_option_list_idx;

  // this variable tells us whether we start by appending
  // kHighest/kLowest at col_idx after the following for loop
  bool start_with_infinity = true;

  for (; option_list_idx >= 0; option_list_idx--) {
    if (!scan_target_.IsExtremal(option_list_idx)) {
      option_list_idx++;
      start_with_infinity = true;
      break;
    }

    auto [begin, end] = GetSearchSpaceBounds(option_list_idx);

    auto& it = current_scan_target_ranges_[option_list_idx];
    ++it;

    if (it != end) {
      // and if this value is at the extremal bound
      SetGroup(option_list_idx, it->first_idx(is_forward_scan_));
      DCHECK(it != scan_options_[option_list_idx].end());
      // if we are AT the boundary of a strict bound then we
      // want to append an infinity after this column to satisfy
      // the strict bound requirement
      start_with_infinity = !it->first_bound_inclusive(is_forward_scan_);
      if (start_with_infinity) {
        option_list_idx++;
      }
      break;
    }

    // If it == end then we move onto incrementing the next column
    SetGroup(option_list_idx, begin->first_idx(is_forward_scan_));
  }

  if (option_list_idx < 0) {
    // If we got here we finished all the options and are done.
    option_list_idx++;
    start_with_infinity = true;
    is_options_done_ = true;
  }

  scan_target_.Truncate(option_list_idx, start_with_infinity);

  if (start_with_infinity) {
    // there's no point in appending anything after infinity
    VLOG_WITH_FUNC(2) << "Key after increment is " << scan_target_.ToString();
    return Status::OK();
  }

  // Reset all columns that are > col_idx
  // We don't want to necessarily reset col_idx as it may
  // have been the case that we got here via an increment on col_idx
  ssize_t current_scan_target_ranges_size = current_scan_target_ranges_.size();
  for (auto i = option_list_idx; i < current_scan_target_ranges_size; ++i) {
    auto begin = GetSearchSpaceBounds(i).first;
    auto it_0 = i == option_list_idx ? current_scan_target_ranges_[i] : begin;
    // Potentially setting a group twice here but that can be easily dealt
    // with if necessary.
    SetGroup(i, it_0->begin_idx());
    scan_target_.Append(*it_0);
  }

  VLOG_WITH_FUNC(2) << "Key after increment is " << scan_target_.ToString();
  DCHECK(VERIFY_RESULT(ValidateHashGroup()))
      << "scan_target_ validation failed: " << scan_target_.ToString();
  return Status::OK();
}

// Validating Scan targets by checking if they have yb_hash_code, hash components and group end in
// order. We do not check range components as sometimes they can end without group ends. Sometimes
// seeks happens with kHighest and a groupend for hash split tables. In such a situation decoding
// hash code is not possible and hence, we validate only fully formed keys.
Result<bool> HybridScanChoices::ValidateHashGroup() const {
  if (is_options_done_) {
    return true;
  }

  DocKeyDecoder t_decoder(scan_target_.WithoutPrefix());
  if (has_hash_columns_) {
    if (!VERIFY_RESULT(t_decoder.DecodeHashCode(dockv::AllowSpecial::kTrue))) {
      return false;
    }
    for (size_t i = 0; i < num_hash_cols_; i++) {
      RETURN_NOT_OK(t_decoder.DecodeKeyEntryValue());
    }
    if (!t_decoder.left_input().starts_with(dockv::KeyEntryTypeAsChar::kHighest)) {
      RETURN_NOT_OK(t_decoder.ConsumeGroupEnd());
    }
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
Result<bool> HybridScanChoices::DoneWithCurrentTarget(bool current_row_skipped, bool not_found) {
  bool result = false;
  if (schema_num_keys_ == scan_options_.size() ||
      (prefix_length_ > 0 && !current_row_skipped) ||
      (not_found && iterator_bound_scope_)) {
    auto incr_idx = not_found
        ? num_bloom_filter_cols_ - 1
        : (prefix_length_ ? prefix_length_ : current_scan_target_ranges_.size()) - 1;
    RETURN_NOT_OK(IncrementScanTargetAtOptionList(incr_idx));
    result = true;
  }

  // if we incremented the last index then
  // if this is a forward scan it doesn't matter what we do
  // if this is a backwards scan then don't clear current_scan_target and we
  // stay live
  VLOG_WITH_FUNC(2)
      << "Current_scan_target_ is " << scan_target_.ToString()
      << ", result: " << result;

  DCHECK(!finished_);

  if (is_options_done_) {
    if (bloom_filter_options_.mode() == BloomFilterMode::kVariable) {
      finished_ = true;
    } else {
      // It could be possible that we finished all our options but are not
      // done because we haven't hit the bound key yet. This would usually be
      // the case if we are moving onto the next hash key where we will
      // restart our range options.
      const KeyBytes& bound_key = is_forward_scan_ ? upper_doc_key_ : lower_doc_key_;
      finished_ = !bound_key.empty() &&
                  (is_forward_scan_ == (scan_target_.AsSlice().compare(bound_key) >= 0));
    }
  }

  if (result) {
    RETURN_NOT_OK(UpdateUpperBound(nullptr));
  }

  VLOG_WITH_FUNC(3)
      << "current_row_skipped: " << current_row_skipped
      << ", scan_target_: " << scan_target_.ToString()
      << ", is_options_done: " << is_options_done_ << ", finished: " << finished_
      << ", result: " << result;

  return result;
}

// Seeks the given iterator to the current target as specified by scan_target_.
void HybridScanChoices::SeekToCurrentTarget(IntentAwareIterator& db_iter) {
  VLOG_WITH_FUNC(2) << "pos: " << db_iter.DebugPosToString();

  if (finished_ || scan_target_.WithoutPrefix().empty()) {
    return;
  }

  if (is_forward_scan_) {
    VLOG_WITH_FUNC(3) << "Seeking to " << scan_target_.ToString();
    db_iter.SeekForward(scan_target_.AsSlice());
  } else {
    // seek to the highest key <= scan_target_
    // seeking to the highest key < scan_target_ + kHighest
    // is equivalent to seeking to the highest key <=
    // scan_target_
    KeyBytes tmp(scan_target_.AsSlice());
    tmp.AppendKeyEntryType(KeyEntryType::kHighest);
    // Append kGroupEnd marker to avoid Corruption error when debugging the key.
    tmp.AppendGroupEnd();
    VLOG_WITH_FUNC(3) << "Going to SeekPrevDocKey " << tmp.AsSlice().ToDebugHexString();
    db_iter.SeekPrevDocKey(tmp);
  }
}


Result<bool> HybridScanChoices::PrepareIterator(IntentAwareIterator& iter, Slice table_key_prefix) {
  VLOG_WITH_FUNC(3)
      << "Bloom filter: " << bloom_filter_options_.mode()
      << ", is_forward_scan: " << is_forward_scan_;
  if (!is_forward_scan_) {
    return false;
  }
  if (bloom_filter_options_.mode() != BloomFilterMode::kVariable) {
    return false;
  }
  // When lower doc key is specified, use it for initial position.
  // Otherwise, use min target provided by options.
  if (!lower_doc_key_.empty()) {
    RETURN_NOT_OK(SkipTargetsUpTo(lower_doc_key_.AsSlice()));
  } else {
    scan_target_.Reset();
    for (size_t option_list_idx = 0; option_list_idx < current_scan_target_ranges_.size();
         option_list_idx++) {
      const auto& options = scan_options_[option_list_idx];
      auto current_it = current_scan_target_ranges_[option_list_idx];
      DCHECK(current_it != options.end());
      scan_target_.Append(*current_it);
    }
  }

  VLOG_WITH_FUNC(3) << "current_scan_target: " << scan_target_.ToString();
  RETURN_NOT_OK(UpdateUpperBound(&iter));
  return true;
}

Result<bool> HybridScanChoices::InterestedInRow(
    dockv::KeyBytes* row_key, IntentAwareIterator& iter) {
  auto row = row_key->AsSlice();
  if (CurrentTargetMatchesKey(row, &iter)) {
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
      iter.SeekOutOfSubDoc(SeekFilter::kAll, row_key);
    } else {
      iter.SeekPrevDocKey(row);
    }
    return false;
  }

  if (finished_) {
    return false;
  }

  // We updated scan target above, if it goes past the row_key_ we will seek again, and
  // process the found key in the next loop.
  if (CurrentTargetMatchesKey(row, &iter)) {
    return true;
  }

  // Not interested in the row => Rollback to last seen ht checkpoint.
  iter.RollbackMaxSeenHt(max_seen_ht_checkpoint_);

  SeekToCurrentTarget(iter);
  return false;
}

Result<bool> HybridScanChoices::AdvanceToNextRow(
    dockv::KeyBytes* row_key, IntentAwareIterator& iter, bool current_fetched_row_skipped) {
  VLOG_WITH_FUNC(4)
      << "row_key: " << (row_key ? DocKey::DebugSliceToString(*row_key) : "<NULL>")
      << ", current_fetched_row_skipped: " << current_fetched_row_skipped
      << ", is_options_done: " << is_options_done_
      << ", iterator_bound_scope: " << iterator_bound_scope_.has_value()
      << ", current: " << scan_target_.ToString();
  if (!row_key) {
    if (!iterator_bound_scope_) {
      // Row was not found and we did not specify upper bound. It means that iteration finished.
      return false;
    }
    if (!VERIFY_RESULT(DoneWithCurrentTarget(current_fetched_row_skipped, true)) ||
        is_options_done_) {
      return false;
    }
  } else if (!VERIFY_RESULT(DoneWithCurrentTarget(current_fetched_row_skipped, false)) ||
             CurrentTargetMatchesKey(row_key->AsSlice(), &iter)) {
    return false;
  }
  SeekToCurrentTarget(iter);
  return true;
}

Status HybridScanChoices::UpdateUpperBound(IntentAwareIterator* iter) {
  if (finished_) {
    return Status::OK();
  }
  if (!iter) {
    if (!iterator_bound_scope_) {
      return Status::OK();
    }
    iter = &iterator_bound_scope_->iterator();
  }

  DCHECK_EQ(bloom_filter_options_.mode(), BloomFilterMode::kVariable);
  RSTATUS_DCHECK_LE(
      num_bloom_filter_cols_, scan_target_.num_columns(), IllegalState,
      Format("Wrong number of columns in scan target $0, while at least $1 required",
             scan_target_.num_columns(), num_bloom_filter_cols_));
  auto bloom_filter_key = scan_target_.SliceUpToColumn(num_bloom_filter_cols_ - 1);

  auto need_update =
      !iterator_bound_scope_ || bloom_filter_key != upper_bound_.AsSlice().WithoutSuffix(1);

  VLOG_WITH_FUNC(4)
      << "has iter: " << (iter != nullptr) << ", current_scan_target: "
      << scan_target_.ToString() << ", bloom_filter_key: "
      << bloom_filter_key.ToDebugHexString() << ", need update: " << need_update;

  if (!need_update) {
    return Status::OK();
  }

  iter->UpdateFilterKey(scan_target_.AsSlice(), scan_target_.AsSlice());
  upper_bound_.Reset(bloom_filter_key);
  upper_bound_.AppendKeyEntryType(KeyEntryType::kHighest);
  if (iterator_bound_scope_) {
    iterator_bound_scope_->Update(upper_bound_);
  } else {
    iterator_bound_scope_.emplace(upper_bound_, iter);
  }
  return Status::OK();
}

std::string OptionRange::ToString() const {
  return Format(
      "$0$1, $2$3", lower_inclusive_ ? "[" : "(", lower_.ToDebugHexString(),
      upper_.ToDebugHexString(), upper_inclusive_ ? "]" : ")");
}


bool OptionRange::Fixed() const {
  return lower_inclusive_ && upper_inclusive_ && lower_ == upper_;
}

} // namespace yb::docdb
