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

#include <vector>

#include "yb/rocksdb/db/write_thread.h"
#include "yb/rocksdb/types.h"
#include "yb/rocksdb/util/autovector.h"
#include "yb/rocksdb/write_batch.h"

#include "yb/util/enums.h"

namespace rocksdb {

class MemTable;
class FlushScheduler;
class ColumnFamilyData;

class ColumnFamilyMemTables {
 public:
  virtual ~ColumnFamilyMemTables() {}
  virtual bool Seek(uint32_t column_family_id) = 0;
  // returns true if the update to memtable should be ignored
  // (useful when recovering from log whose updates have already
  // been processed)
  virtual uint64_t GetLogNumber() const = 0;
  virtual MemTable* GetMemTable() const = 0;
  virtual ColumnFamilyHandle* GetColumnFamilyHandle() = 0;
  virtual ColumnFamilyData* current() { return nullptr; }
};

class ColumnFamilyMemTablesDefault : public ColumnFamilyMemTables {
 public:
  explicit ColumnFamilyMemTablesDefault(MemTable* mem)
      : ok_(false), mem_(mem) {}

  bool Seek(uint32_t column_family_id) override {
    ok_ = (column_family_id == 0);
    return ok_;
  }

  uint64_t GetLogNumber() const override { return 0; }

  MemTable* GetMemTable() const override {
    assert(ok_);
    return mem_;
  }

  ColumnFamilyHandle* GetColumnFamilyHandle() override { return nullptr; }

 private:
  bool ok_;
  MemTable* mem_;
};

YB_DEFINE_ENUM(InsertFlag,
               (kFilterDeletes)            // ignore deletes of non-existing keys
               (kConcurrentMemtableWrites) // allow concurrent writes
               );
typedef yb::EnumBitSet<InsertFlag> InsertFlags;

// WriteBatchInternal provides static methods for manipulating a
// WriteBatch that we don't want in the public WriteBatch interface.
class WriteBatchInternal {
 public:
  // WriteBatch methods with column_family_id instead of ColumnFamilyHandle*
  static void Put(WriteBatch* batch, uint32_t column_family_id,
                  const Slice& key, const Slice& value);

  static void Put(WriteBatch* batch, uint32_t column_family_id,
                  const SliceParts& key, const SliceParts& value);

  static void Delete(WriteBatch* batch, uint32_t column_family_id,
                     const SliceParts& key);

  static void Delete(WriteBatch* batch, uint32_t column_family_id,
                     const Slice& key);

  static void SingleDelete(WriteBatch* batch, uint32_t column_family_id,
                           const SliceParts& key);

  static void SingleDelete(WriteBatch* batch, uint32_t column_family_id,
                           const Slice& key);

  static void Merge(WriteBatch* batch, uint32_t column_family_id,
                    const Slice& key, const Slice& value);

  static void Merge(WriteBatch* batch, uint32_t column_family_id,
                    const SliceParts& key, const SliceParts& value);

  // Return the number of entries in the batch.
  static uint32_t Count(const WriteBatch* batch);

  // Set the count for the number of entries in the batch.
  static void SetCount(WriteBatch* batch, uint32_t n);

  // Return the seqeunce number for the start of this batch.
  static SequenceNumber Sequence(const WriteBatch* batch);

  // Store the specified number as the sequence number for the start of
  // this batch.
  static void SetSequence(WriteBatch* batch, SequenceNumber seq);

  // Returns the offset of the first entry in the batch.
  // This offset is only valid if the batch is not empty.
  static size_t GetFirstOffset(WriteBatch* batch);

  static Slice Contents(const WriteBatch* batch) {
    return Slice(batch->rep_);
  }

  static size_t ByteSize(const WriteBatch* batch) {
    return batch->rep_.size();
  }

  static void SetContents(WriteBatch* batch, const Slice& contents);

  // Inserts batches[i] into memtable, for i in 0..num_batches-1 inclusive.
  //
  // If dont_filter_deletes is false AND options.filter_deletes is true
  // AND db->KeyMayExist is false, then a Delete won't modify the memtable.
  //
  // If ignore_missing_column_families == true. WriteBatch
  // referencing non-existing column family will be ignored.
  // If ignore_missing_column_families == false, processing of the
  // batches will be stopped if a reference is found to a non-existing
  // column family and InvalidArgument() will be returned.  The writes
  // in batches may be only partially applied at that point.
  //
  // If log_number is non-zero, the memtable will be updated only if
  // memtables->GetLogNumber() >= log_number.
  //
  // If flush_scheduler is non-null, it will be invoked if the memtable
  // should be flushed.
  //
  // Under concurrent use, the caller is responsible for making sure that
  // the memtables object itself is thread-local.
  static Status InsertInto(const autovector<WriteThread::Writer*>& batches,
                           SequenceNumber sequence,
                           ColumnFamilyMemTables* memtables,
                           FlushScheduler* flush_scheduler,
                           bool ignore_missing_column_families = false,
                           uint64_t log_number = 0, DB* db = nullptr,
                           InsertFlags insert_flags = InsertFlags());

  // Convenience form of InsertInto when you have only one batch
  static Status InsertInto(const WriteBatch* batch,
                           ColumnFamilyMemTables* memtables,
                           FlushScheduler* flush_scheduler,
                           bool ignore_missing_column_families = false,
                           uint64_t log_number = 0, DB* db = nullptr,
                           InsertFlags insert_flags = InsertFlags());

  static void Append(WriteBatch* dst, const WriteBatch* src);

  // Returns the byte size of appending a WriteBatch with ByteSize
  // leftByteSize and a WriteBatch with ByteSize rightByteSize
  static size_t AppendedByteSize(size_t leftByteSize, size_t rightByteSize);
};

}  // namespace rocksdb
