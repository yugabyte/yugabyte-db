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


#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "yb/gutil/thread_annotations.h"

#include "yb/rocksdb/db/column_family.h"
#include "yb/rocksdb/db.h"
#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/util/arena.h"

namespace rocksdb {

class DBImpl;
struct SuperVersion;
class ColumnFamilyData;

/**
 * ManagedIterator is a special type of iterator that supports freeing the
 * underlying iterator and still being able to access the current key/value
 * pair.  This is done by copying the key/value pair so that clients can
 * continue to access the data without getting a SIGSEGV.
 * The underlying iterator can be freed manually through the  call to
 * ReleaseIter or automatically (as needed on space pressure or age.)
 * The iterator is recreated using the saved original arguments.
 */
class ManagedIterator final : public Iterator {
 public:
  ManagedIterator(DBImpl* db, const ReadOptions& read_options,
                  ColumnFamilyData* cfd);
  virtual ~ManagedIterator();

  const KeyValueEntry& SeekToLast() override;
  const KeyValueEntry& Prev() override;
  const KeyValueEntry& Entry() const override;
  const KeyValueEntry& SeekToFirst() override;
  const KeyValueEntry& Seek(Slice target) override;
  const KeyValueEntry& Next() override;

  Status status() const override;
  void ReleaseIter(bool only_old);
  void SetDropOld(bool only_old) {
    only_drop_old_ = read_options_.tailing || only_old;
  }

 private:
  void RebuildIterator();
  void UpdateCurrent();
  void SeekInternal(const Slice& user_key, bool seek_to_first);
  bool NeedToRebuild();
  void Lock() ACQUIRE();
  bool TryLock() TRY_ACQUIRE(true);
  void UnLock() RELEASE();
  DBImpl* const db_;
  ReadOptions read_options_;
  ColumnFamilyData* const cfd_;
  ColumnFamilyHandleInternal cfh_;

  uint64_t svnum_;
  std::unique_ptr<Iterator> mutable_iter_;
  // internal iterator status
  Status status_;

  KeyValueEntry entry_;
  IterKey cached_key_;
  IterKey cached_value_;

  bool only_drop_old_ = true;
  bool snapshot_created_;
  bool release_supported_;
  std::mutex in_use_;  // is managed iterator in use
};

}  // namespace rocksdb
