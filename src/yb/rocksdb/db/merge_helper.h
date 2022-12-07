//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <memory>
#include <string>
#include <vector>

#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/db/version_edit.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/util/stop_watch.h"

#include "yb/util/slice.h"

namespace rocksdb {

class Comparator;
class Iterator;
class Logger;
class MergeOperator;
class Statistics;
class InternalIterator;

class MergeHelper {
 public:
  MergeHelper(Env* env, const Comparator* user_comparator,
              const MergeOperator* user_merge_operator,
              const CompactionFilter* compaction_filter, Logger* logger,
              unsigned min_partial_merge_operands,
              bool assert_valid_internal_key, SequenceNumber latest_snapshot,
              int level = 0, Statistics* stats = nullptr)
      : env_(env),
        user_comparator_(user_comparator),
        user_merge_operator_(user_merge_operator),
        compaction_filter_(compaction_filter),
        logger_(logger),
        min_partial_merge_operands_(min_partial_merge_operands),
        assert_valid_internal_key_(assert_valid_internal_key),
        latest_snapshot_(latest_snapshot),
        level_(level),
        keys_(),
        operands_(),
        filter_timer_(env_),
        stats_(stats) {
    assert(user_comparator_ != nullptr);
  }

  // Wrapper around MergeOperator::FullMerge() that records perf statistics.
  // Result of merge will be written to result if status returned is OK.
  // If operands is empty, the value will simply be copied to result.
  // Returns one of the following statuses:
  // - OK: Entries were successfully merged.
  // - Corruption: Merge operator reported unsuccessful merge.
  // - NotSupported: Merge operator is missing.
  static Status TimedFullMerge(const Slice& key, const Slice* value,
                               const std::deque<std::string>& operands,
                               const MergeOperator* merge_operator,
                               Statistics* statistics, Env* env, Logger* logger,
                               std::string* result);

  // Merge entries until we hit
  //     - a corrupted key
  //     - a Put/Delete,
  //     - a different user key,
  //     - a specific sequence number (snapshot boundary),
  //  or - the end of iteration
  // iter: (IN)  points to the first merge type entry
  //       (OUT) points to the first entry not included in the merge process
  // stop_before: (IN) a sequence number that merge should not cross.
  //                   0 means no restriction
  // at_bottom:   (IN) true if the iterator covers the bottem level, which means
  //                   we could reach the start of the history of this user key.
  //
  // Returns one of the following statuses:
  // - OK: Entries were successfully merged.
  // - MergeInProgress: Put/Delete not encountered and unable to merge operands.
  // - Corruption: Merge operator reported unsuccessful merge or a corrupted
  //   key has been encountered and not expected (applies only when compiling
  //   with asserts removed).
  //
  // REQUIRED: The first key in the input is not corrupted.
  Status MergeUntil(InternalIterator* iter,
                    const SequenceNumber stop_before = 0,
                    const bool at_bottom = false);

  // Filters a merge operand using the compaction filter specified
  // in the constructor. Returns true if the operand should be filtered out.
  bool FilterMerge(const Slice& user_key, const Slice& value_slice);

  // Query the merge result
  // These are valid until the next MergeUntil call
  // If the merging was successful:
  //   - keys() contains a single element with the latest sequence number of
  //     the merges. The type will be Put or Merge. See IMPORTANT 1 note, below.
  //   - values() contains a single element with the result of merging all the
  //     operands together
  //
  //   IMPORTANT 1: the key type could change after the MergeUntil call.
  //        Put/Delete + Merge + ... + Merge => Put
  //        Merge + ... + Merge => Merge
  //
  // If the merge operator is not associative, and if a Put/Delete is not found
  // then the merging will be unsuccessful. In this case:
  //   - keys() contains the list of internal keys seen in order of iteration.
  //   - values() contains the list of values (merges) seen in the same order.
  //              values() is parallel to keys() so that the first entry in
  //              keys() is the key associated with the first entry in values()
  //              and so on. These lists will be the same length.
  //              All of these pairs will be merges over the same user key.
  //              See IMPORTANT 2 note below.
  //
  //   IMPORTANT 2: The entries were traversed in order from BACK to FRONT.
  //                So keys().back() was the first key seen by iterator.
  // TODO: Re-style this comment to be like the first one
  const std::deque<std::string>& keys() const { return keys_; }
  const std::deque<std::string>& values() const { return operands_; }
  bool HasOperator() const { return user_merge_operator_ != nullptr; }

 private:
  Env* env_;
  const Comparator* user_comparator_;
  const MergeOperator* user_merge_operator_;
  const CompactionFilter* compaction_filter_;
  Logger* logger_;
  unsigned min_partial_merge_operands_;
  bool assert_valid_internal_key_; // enforce no internal key corruption?
  SequenceNumber latest_snapshot_;
  int level_;

  // the scratch area that holds the result of MergeUntil
  // valid up to the next MergeUntil call
  std::deque<std::string> keys_;    // Keeps track of the sequence of keys seen
  std::deque<std::string> operands_;  // Parallel with keys_; stores the values

  StopWatchNano filter_timer_;
  Statistics* stats_;
};

// MergeOutputIterator can be used to iterate over the result of a merge.
class MergeOutputIterator {
 public:
  // The MergeOutputIterator is bound to a MergeHelper instance.
  explicit MergeOutputIterator(const MergeHelper* merge_helper);

  // Seeks to the first record in the output.
  void SeekToFirst();
  // Advances to the next record in the output.
  void Next();

  Slice key() { return Slice(*it_keys_); }
  Slice value() { return Slice(*it_values_); }
  bool Valid() { return it_keys_ != merge_helper_->keys().rend(); }

 private:
  const MergeHelper* merge_helper_;
  std::deque<std::string>::const_reverse_iterator it_keys_;
  std::deque<std::string>::const_reverse_iterator it_values_;
};

} // namespace rocksdb
