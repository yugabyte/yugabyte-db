// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "kudu/common/predicate_encoder.h"

#include <algorithm>

#include "kudu/common/partial_row.h"
#include "kudu/common/row.h"
#include "kudu/common/row_key-util.h"
#include "kudu/common/types.h"

namespace kudu {

RangePredicateEncoder::RangePredicateEncoder(const Schema* key_schema,
                                             Arena* arena)
  : key_schema_(key_schema),
    arena_(arena) {
}

void RangePredicateEncoder::EncodeRangePredicates(ScanSpec *spec, bool erase_pushed) {
  // Step 1) Simplify all predicates which apply to keys.
  //
  // First, we loop over all predicates, find those that apply to key columns,
  // and group them by the column they apply to. Bounds are simplified (i.e.
  // the tightest bounds are retained). In this step, we retain the original indexes
  // of the predicates that we've analyzed, so we can later remove them if necessary.
  vector<SimplifiedBounds> key_bounds;
  SimplifyBounds(*spec, &key_bounds);

  // Step 2) Determine the length of the "equality" part of the key.
  //
  // The following pattern of predicates can be converted into range scans:
  //
  //   k1 = a AND k2 = b AND ... AND kN BETWEEN c AND d
  //
  // In other words, we can have a sequence of equality conditions, followed by
  // a range predicate on one further column past that.
  //
  // In this step, we count how many key components have equality predicates applied.
  int equality_prefix = CountKeyPrefixEqualities(key_bounds);

  // We're only allowed to push the equality conditions and optionally one more.
  int max_push_len = std::min<int>(equality_prefix + 1, key_bounds.size());

  // Step 3) Prepare upper and lower bound key tuples
  //
  // Here we allocate tuples from the arena which will store the upper and lower
  // bound. We initialize all elements of these tuples to their minimum value.
  uint8_t* lower_buf = static_cast<uint8_t*>(
      CHECK_NOTNULL(arena_->AllocateBytes(key_schema_->key_byte_size())));
  uint8_t* upper_buf = static_cast<uint8_t*>(
      CHECK_NOTNULL(arena_->AllocateBytes(key_schema_->key_byte_size())));
  ContiguousRow lower_key(key_schema_, lower_buf);
  ContiguousRow upper_key(key_schema_, upper_buf);

  row_key_util::SetKeyToMinValues(&lower_key);
  row_key_util::SetKeyToMinValues(&upper_key);

  // Step 4) Construct upper/lower bound tuples
  //
  // We iterate through the predicates and copy the predicate bounds into
  // the tuples, while also keeping track of how many elements have been
  // set in each.
  //
  // For example, with a (year, month, day) primary key:
  //
  //   Predicate: year = 2015 AND month = 7 AND day <= 15
  //   upper_key: (2015, 7, 15)    (len=3)
  //   lower_key: (2015, 7, <min>) (len=2)
  //
  // Note that the 'day' component of the lower bound remains as '<min>'
  // here because there is no lower bound range predicate on the 'day' column.
  //
  // While iterating, we also keep track of which original predicates were
  // pushed down, so we can remove them later.
  int lower_len = 0;
  int upper_len = 0;
  vector<bool> was_pushed(spec->predicates().size());
  int n_pushed;
  for (n_pushed = 0; n_pushed < max_push_len; n_pushed++) {
    const ColumnSchema& col = key_schema_->column(n_pushed);
    int size = col.type_info()->size();
    const SimplifiedBounds& b = key_bounds[n_pushed];

    // If we're still in the "equality" part of the key, we expect both
    // the upper and lower bounds to be set.
    if (n_pushed < equality_prefix) {
      DCHECK(b.lower && b.upper);
    }

    if (b.lower) {
      memcpy(lower_key.mutable_cell_ptr(n_pushed), key_bounds[n_pushed].lower, size);
      lower_len++;
    }
    if (b.upper) {
      memcpy(upper_key.mutable_cell_ptr(n_pushed), key_bounds[n_pushed].upper, size);
      upper_len++;
    }
    for (int pred_idx : key_bounds[n_pushed].orig_predicate_indexes) {
      was_pushed[pred_idx] = true;
    }
  }

  // Step 4) Convert upper bound to exclusive
  //
  // Column range predicates are inclusive, but primary key predicates are exclusive.
  // Here, we increment the upper bound key to convert between the two.
  //
  // Handling prefix conditions on the upper bound is slightly subtle.
  // Consider, for example:
  //
  //   Predicate: year = 2015 AND month <= 7
  //   upper_key: (2015, 7, <min>)  (len=2)
  //
  // Conceptually, what we'd like to do is set upper_key <= (2015, 7, <max>),
  // and then increment it to: upper_key < (2015, 8, <min>). However, there is
  // no such concept of a "<max>" value for a column (strings can always be
  // incremented further). So, instead, we leave the remaining components
  // as "<min>", and increment the prefix, which yields the same result.
  if (upper_len) {
    if (!row_key_util::IncrementKeyPrefix(&upper_key, upper_len, arena_)) {
      // If the upper bound is already the very maximum key, we can't increment
      // it any more. In that case, it's equivalent to setting no bound at all,
      // so we reset the length back to 0.
      //
      // For example, consider:
      //   Predicate: year <= MAX_INT
      //   upper_key; (MAX_INT, <min>, <min>) (len=1)
      //
      // IncrementKeyPrefix(1) here will return false since MAX_INT cannot be
      // further incremented. However, the predicate is itself tautological, so
      // we can just remove it.
      upper_len = 0;
    }
  }

  VLOG(4) << "Lower: " << key_schema_->DebugRowKey(lower_key) << "(" << lower_len << ")";
  VLOG(4) << "Upper: " << key_schema_->DebugRowKey(upper_key) << "(" << upper_len << ")";

  // Step 5. Erase the pushed predicates from the ScanSpec.
  if (erase_pushed) {
    ErasePushedPredicates(spec, was_pushed);
  }

  // Step 6. Add the new range predicates to the spec.
  if (lower_len) {
    EncodedKey* lower = EncodedKey::FromContiguousRow(ConstContiguousRow(lower_key)).release();
    pool_.Add(lower);
    spec->SetLowerBoundKey(lower);
  }
  if (upper_len) {
    EncodedKey* upper = EncodedKey::FromContiguousRow(ConstContiguousRow(upper_key)).release();
    pool_.Add(upper);
    spec->SetExclusiveUpperBoundKey(upper);
  }
}

void RangePredicateEncoder::SimplifyBounds(const ScanSpec& spec,
                                           vector<SimplifiedBounds>* key_bounds) const {
  key_bounds->clear();
  key_bounds->resize(key_schema_->num_key_columns());

  for (int i = 0; i < spec.predicates().size(); i++) {
    const ColumnRangePredicate& pred = spec.predicates()[i];
    int idx = key_schema_->find_column(pred.column().name());
    if (idx == -1 || idx >= key_bounds->size()) {
      continue;
    }
    const ColumnSchema& col = key_schema_->column(idx);

    // Add to the list of pushable predicates for this column.
    CHECK(pred.range().has_lower_bound() || pred.range().has_upper_bound());
    (*key_bounds)[idx].orig_predicate_indexes.push_back(i);

    if (pred.range().has_upper_bound()) {
      // If we haven't seen any upper bound, or this upper bound is tighter than
      // (less than) the one we've seen already, replace it.
      if ((*key_bounds)[idx].upper == nullptr ||
          col.type_info()->Compare(pred.range().upper_bound(),
                                   (*key_bounds)[idx].upper) < 0) {
        (*key_bounds)[idx].upper = pred.range().upper_bound();
      }
    }

    if (pred.range().has_lower_bound()) {
      // If we haven't seen any lower bound, or this lower bound is tighter than
      // (greater than) the one we've seen already, replace it.
      if ((*key_bounds)[idx].lower == nullptr ||
          col.type_info()->Compare(pred.range().lower_bound(),
                                   (*key_bounds)[idx].lower) > 0) {
        (*key_bounds)[idx].lower = pred.range().lower_bound();
      }
    }
  }
}

int RangePredicateEncoder::CountKeyPrefixEqualities(
    const vector<SimplifiedBounds>& key_bounds) const {

  int i = 0;
  for (; i < key_schema_->num_key_columns(); i++) {
    if (!key_bounds[i].lower || !key_bounds[i].upper) {
      break;
    }
    ColumnRangePredicate pred(key_schema_->column(i),
                              key_bounds[i].lower,
                              key_bounds[i].upper);
    if (!pred.range().IsEquality()) break;
  }
  return i;
}

void RangePredicateEncoder::ErasePushedPredicates(
    ScanSpec *spec, const vector<bool>& should_erase) const {
  int num_preds = spec->predicates().size();
  CHECK_EQ(should_erase.size(), num_preds);

  vector<ColumnRangePredicate> new_preds;
  new_preds.reserve(num_preds);

  for (int i = 0; i < num_preds; i++) {
    if (!should_erase[i]) {
      new_preds.push_back(spec->predicates()[i]);
    }
  }
  spec->mutable_predicates()->swap(new_preds);
}

} // namespace kudu
