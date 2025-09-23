//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <limits>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/listener.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/status.h"
#include "yb/rocksdb/types.h"

namespace rocksdb {
class MergeContext;

class GetContext {
 public:
  enum GetState {
    kNotFound,
    kFound,
    kDeleted,
    kCorrupt,
    kMerge  // saver contains the current merge result (the operands)
  };

  GetContext(const Comparator* ucmp, const MergeOperator* merge_operator,
             Logger* logger, Statistics* statistics, GetState init_state,
             const Slice& user_key, std::string* ret_value, bool* value_found,
             MergeContext* merge_context, Env* env,
             SequenceNumber* seq = nullptr);

  void MarkKeyMayExist();

  // Records this key, value, and any meta-data (such as sequence number and
  // state) into this GetContext.
  //
  // Returns True if more keys need to be read (due to merges) or
  //         False if the complete value has been found.
  bool SaveValue(const ParsedInternalKey& parsed_key, const Slice& value);

  // Simplified version of the previous function. Should only be used when we
  // know that the operation is a Put.
  void SaveValue(const Slice& value, SequenceNumber seq);

  GetState State() const { return state_; }

  // If a non-null string is passed, all the SaveValue calls will be
  // logged into the string. The operations can then be replayed on
  // another GetContext with replayGetContextLog.
  void SetReplayLog(std::string* replay_log) { replay_log_ = replay_log; }

  // Do we need to fetch the SequenceNumber for this key?
  bool NeedToReadSequence() const { return (seq_ != nullptr); }

 private:
  const Comparator* ucmp_;
  const MergeOperator* merge_operator_;
  // the merge operations encountered;
  Logger* logger_;
  Statistics* statistics_;

  GetState state_;
  Slice user_key_;
  std::string* value_;
  bool* value_found_;  // Is value set correctly? Used by KeyMayExist
  MergeContext* merge_context_;
  Env* env_;
  // If a key is found, seq_ will be set to the SequenceNumber of most recent
  // write to the key or kMaxSequenceNumber if unknown
  SequenceNumber* seq_;
  std::string* replay_log_;
};

void replayGetContextLog(const Slice& replay_log, const Slice& user_key,
                         GetContext* get_context);

}  // namespace rocksdb
