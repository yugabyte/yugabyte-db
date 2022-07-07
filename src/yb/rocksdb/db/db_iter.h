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

#ifndef YB_ROCKSDB_DB_DB_ITER_H
#define YB_ROCKSDB_DB_DB_ITER_H

#pragma once

#include <stdint.h>

#include <string>

#include "yb/rocksdb/immutable_options.h"
#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/util/arena.h"

namespace rocksdb {

class Arena;
class DBIter;
class InternalIterator;

// Return a new iterator that converts internal keys (yielded by
// "*internal_iter") that were live at the specified "sequence" number
// into appropriate user keys.
extern Iterator* NewDBIterator(
    Env* env, const ImmutableCFOptions& options,
    const Comparator* user_key_comparator, InternalIterator* internal_iter,
    const SequenceNumber& sequence, uint64_t max_sequential_skip_in_iterations,
    uint64_t version_number, const Slice* iterate_upper_bound = nullptr,
    bool prefix_same_as_start = false, bool pin_data = false);

// A wrapper iterator which wraps DB Iterator and the arena, with which the DB
// iterator is supposed be allocated. This class is used as an entry point of
// an iterator hierarchy whose memory can be allocated inline. In that way,
// accessing the iterator tree can be more cache friendly. It is also faster
// to allocate.
class ArenaWrappedDBIter : public Iterator {
 public:
  virtual ~ArenaWrappedDBIter();

  // Get the arena to be used to allocate memory for DBIter to be wrapped,
  // as well as child iterators in it.
  virtual Arena* GetArena() { return &arena_; }

  // Set the DB Iterator to be wrapped

  virtual void SetDBIter(DBIter* iter);

  // Set the internal iterator wrapped inside the DB Iterator. Usually it is
  // a merging iterator.
  virtual void SetIterUnderDBIter(InternalIterator* iter);
  virtual bool Valid() const override;
  virtual void SeekToFirst() override;
  virtual void SeekToLast() override;
  virtual void Seek(const Slice& target) override;
  virtual void Next() override;
  virtual void Prev() override;
  virtual Slice key() const override;
  virtual Slice value() const override;
  virtual Status status() const override;

  void RegisterCleanup(CleanupFunction function, void* arg1, void* arg2);
  virtual Status PinData();
  virtual Status ReleasePinnedData();
  virtual Status GetProperty(std::string prop_name, std::string* prop) override;

  void RevalidateAfterUpperBoundChange() override;

 private:
  DBIter* db_iter_;
  Arena arena_;
};

// Generate the arena wrapped iterator class.
extern ArenaWrappedDBIter* NewArenaWrappedDbIterator(
    Env* env, const ImmutableCFOptions& options,
    const Comparator* user_key_comparator, const SequenceNumber& sequence,
    uint64_t max_sequential_skip_in_iterations, uint64_t version_number,
    const Slice* iterate_upper_bound = nullptr,
    bool prefix_same_as_start = false, bool pin_data = false);

}  // namespace rocksdb

#endif // YB_ROCKSDB_DB_DB_ITER_H
