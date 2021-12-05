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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once

#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/table/internal_iterator.h"

namespace rocksdb {

struct ReadOptions;
class InternalKeyComparator;
class Arena;

struct TwoLevelIteratorState {
  explicit TwoLevelIteratorState(bool _check_prefix_may_match)
      : check_prefix_may_match(_check_prefix_may_match) {}

  virtual ~TwoLevelIteratorState() {}
  virtual InternalIterator* NewSecondaryIterator(const Slice& handle) = 0;
  virtual bool PrefixMayMatch(const Slice& internal_key) = 0;

  // If call PrefixMayMatch()
  bool check_prefix_may_match;
};


// Return a new two level iterator.  A two-level iterator contains an
// index iterator whose values point to a sequence of blocks where
// each block is itself a sequence of key,value pairs.  The returned
// two-level iterator yields the concatenation of all key/value pairs
// in the sequence of blocks.  Takes ownership of "index_iter" and
// will delete it when no longer needed.
//
// Uses a supplied function to convert an index_iter value into
// an iterator over the contents of the corresponding block.
// arena: If not null, the arena is used to allocate the Iterator.
//        When destroying the iterator, the destructor will destroy
//        all the states but those allocated in arena.
// need_free_iter_and_state: free `state` and `first_level_iter` if
//                           true. Otherwise, just call destructor.
extern InternalIterator* NewTwoLevelIterator(
    TwoLevelIteratorState* state, InternalIterator* first_level_iter, Arena* arena = nullptr,
    bool need_free_iter_and_state = true);

}  // namespace rocksdb
