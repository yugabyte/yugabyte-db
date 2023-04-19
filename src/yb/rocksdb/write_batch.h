// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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
// WriteBatch holds a collection of updates to apply atomically to a DB.
//
// The updates are applied in the order in which they are added
// to the WriteBatch.  For example, the value of "key" will be "v3"
// after the following batch is written:
//
//    batch.Put("key", "v1");
//    batch.Delete("key");
//    batch.Put("key", "v2");
//    batch.Put("key", "v3");
//
// Multiple threads can invoke const methods on a WriteBatch without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same WriteBatch must use
// external synchronization.

#pragma once

#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>
#include <optional>

#include "yb/rocksdb/status.h"
#include "yb/rocksdb/write_batch_base.h"

#include "yb/util/slice.h"
#include "yb/util/slice_parts.h"

namespace rocksdb {

class ColumnFamilyHandle;
struct SavePoints;
class UserFrontiers;

class DirectWriteHandler {
 public:
  // Returns slices to inserted key and value.
  virtual std::pair<Slice, Slice> Put(const SliceParts& key, const SliceParts& value) = 0;
  virtual void SingleDelete(const Slice& key) = 0;

  virtual ~DirectWriteHandler() = default;
};

// DirectWriter could be attached to a WriteBatch. In this case, when the write batch is written to
// RocksDB, the Apply method of the direct writer is called, and it is passed a DirectWriteHandler.
// The direct writer uses the methods of DirectWriteHandler such as Put and SingleDelete to write
// entries directly to the memtable.
class DirectWriter {
 public:
  virtual Status Apply(DirectWriteHandler* handler) = 0;

  virtual ~DirectWriter() = default;
};

class WriteBatch : public WriteBatchBase {
 public:
  explicit WriteBatch(size_t reserved_bytes = 0);
  ~WriteBatch();

  using WriteBatchBase::Put;
  // Store the mapping "key->value" in the database.
  void Put(ColumnFamilyHandle* column_family, const Slice& key,
           const Slice& value) override;
  void Put(const Slice& key, const Slice& value) override {
    Put(nullptr, key, value);
  }

  // Variant of Put() that gathers output like writev(2).  The key and value
  // that will be written to the database are concatentations of arrays of
  // slices.
  void Put(ColumnFamilyHandle* column_family, const SliceParts& key,
           const SliceParts& value) override;
  void Put(const SliceParts& key, const SliceParts& value) override {
    Put(nullptr, key, value);
  }

  using WriteBatchBase::Delete;
  // If the database contains a mapping for "key", erase it.  Else do nothing.
  void Delete(ColumnFamilyHandle* column_family, const Slice& key) override;
  void Delete(const Slice& key) override { Delete(nullptr, key); }

  // variant that takes SliceParts
  void Delete(ColumnFamilyHandle* column_family,
              const SliceParts& key) override;
  void Delete(const SliceParts& key) override { Delete(nullptr, key); }

  using WriteBatchBase::SingleDelete;
  // WriteBatch implementation of DB::SingleDelete().  See db.h.
  void SingleDelete(ColumnFamilyHandle* column_family,
                    const Slice& key) override;
  void SingleDelete(const Slice& key) override { SingleDelete(nullptr, key); }

  // variant that takes SliceParts
  void SingleDelete(ColumnFamilyHandle* column_family,
                    const SliceParts& key) override;
  void SingleDelete(const SliceParts& key) override {
    SingleDelete(nullptr, key);
  }

  using WriteBatchBase::Merge;
  // Merge "value" with the existing value of "key" in the database.
  // "key->merge(existing, value)"
  void Merge(ColumnFamilyHandle* column_family, const Slice& key,
             const Slice& value) override;
  void Merge(const Slice& key, const Slice& value) override {
    Merge(nullptr, key, value);
  }

  // variant that takes SliceParts
  void Merge(ColumnFamilyHandle* column_family, const SliceParts& key,
             const SliceParts& value) override;
  void Merge(const SliceParts& key, const SliceParts& value) override {
    Merge(nullptr, key, value);
  }

  using WriteBatchBase::PutLogData;
  // Append a blob of arbitrary size to the records in this batch. The blob will
  // be stored in the transaction log but not in any other file. In particular,
  // it will not be persisted to the SST files. When iterating over this
  // WriteBatch, WriteBatch::Handler::LogData will be called with the contents
  // of the blob as it is encountered. Blobs, puts, deletes, and merges will be
  // encountered in the same order in thich they were inserted. The blob will
  // NOT consume sequence number(s) and will NOT increase the count of the batch
  //
  // Example application: add timestamps to the transaction log for use in
  // replication.
  void PutLogData(const Slice& blob) override;

  using WriteBatchBase::Clear;
  // Clear all updates buffered in this batch.
  void Clear() override;

  // Records the state of the batch for future calls to RollbackToSavePoint().
  // May be called multiple times to set multiple save points.
  void SetSavePoint() override;

  // Remove all entries in this batch (Put, Merge, Delete, PutLogData) since the
  // most recent call to SetSavePoint() and removes the most recent save point.
  // If there is no previous call to SetSavePoint(), STATUS(NotFound, "")
  // will be returned.
  // Oterwise returns Status::OK().
  Status RollbackToSavePoint() override;

  // Support for iterating over the contents of a batch.
  class Handler {
   public:
    virtual ~Handler();
    // default implementation will just call Put without column family for
    // backwards compatibility. If the column family is not default,
    // the function is noop
    virtual Status PutCF(uint32_t column_family_id, const SliceParts& key,
                                 const SliceParts& value) {
      if (column_family_id == 0) {
        // Put() historically doesn't return status. We didn't want to be
        // backwards incompatible so we didn't change the return status
        // (this is a public API). We do an ordinary get and return Status::OK()
        Put(key.TheOnlyPart(), value.TheOnlyPart());
        return Status::OK();
      }
      return STATUS(InvalidArgument,
          "non-default column family and PutCF not implemented");
    }
    virtual void Put(const Slice& /*key*/, const Slice& /*value*/) {}

    virtual Status DeleteCF(uint32_t column_family_id, const Slice& key) {
      if (column_family_id == 0) {
        Delete(key);
        return Status::OK();
      }
      return STATUS(InvalidArgument,
          "non-default column family and DeleteCF not implemented");
    }
    virtual void Delete(const Slice& /*key*/) {}

    virtual Status SingleDeleteCF(uint32_t column_family_id, const Slice& key) {
      if (column_family_id == 0) {
        SingleDelete(key);
        return Status::OK();
      }
      return STATUS(InvalidArgument,
          "non-default column family and SingleDeleteCF not implemented");
    }
    virtual void SingleDelete(const Slice& /*key*/) {}

    // Merge and LogData are not pure virtual. Otherwise, we would break
    // existing clients of Handler on a source code level. The default
    // implementation of Merge does nothing.
    virtual Status MergeCF(uint32_t column_family_id, const Slice& key,
                           const Slice& value) {
      if (column_family_id == 0) {
        Merge(key, value);
        return Status::OK();
      }
      return STATUS(InvalidArgument,
          "non-default column family and MergeCF not implemented");
    }
    virtual void Merge(const Slice& /*key*/, const Slice& /*value*/) {}

    virtual Status Frontiers(const UserFrontiers&) {
      return STATUS(NotSupported, "UserFrontiers not implemented");
    }

    // The default implementation of LogData does nothing.
    virtual void LogData(const Slice& blob);

    // Continue is called by WriteBatch::Iterate. If it returns false,
    // iteration is halted. Otherwise, it continues iterating. The default
    // implementation always returns true.
    virtual bool Continue();
  };
  Status Iterate(Handler* handler) const;

  // Retrieve the serialized version of this batch.
  const std::string& Data() const { return rep_; }

  // Retrieve data size of the batch.
  size_t GetDataSize() const { return rep_.size(); }

  // Returns the number of updates in the batch
  uint32_t Count() const;

  // Returns true if PutCF will be called during Iterate
  bool HasPut() const;

  // Returns true if DeleteCF will be called during Iterate
  bool HasDelete() const;

  // Returns true if SingleDeleteCF will be called during Iterate
  bool HasSingleDelete() const;

  // Returns trie if MergeCF will be called during Iterate
  bool HasMerge() const;

  using WriteBatchBase::GetWriteBatch;
  WriteBatch* GetWriteBatch() override { return this; }

  // Constructor with a serialized string object
  explicit WriteBatch(const std::string& rep);

  WriteBatch(const WriteBatch& src);
  WriteBatch(WriteBatch&& src);
  WriteBatch& operator=(const WriteBatch& src);
  WriteBatch& operator=(WriteBatch&& src);

  void SetFrontiers(const UserFrontiers* value) { frontiers_ = value; }
  const UserFrontiers* Frontiers() const { return frontiers_; }

  void SetDirectWriter(DirectWriter* direct_writer) {
    direct_writer_ = direct_writer;
  }

  bool HasDirectWriter() const {
    return direct_writer_ != nullptr;
  }

  size_t DirectEntries() const {
    return direct_entries_;
  }

  void SetHandlerForLogging(Handler* handler_for_logging) {
    handler_for_logging_ = handler_for_logging;
  }

 private:
  friend class WriteBatchInternal;
  std::unique_ptr<SavePoints> save_points_;

  // For HasXYZ.  Mutable to allow lazy computation of results
  mutable std::atomic<uint32_t> content_flags_;

  // Performs deferred computation of content_flags if necessary
  uint32_t ComputeContentFlags() const;

 protected:
  std::string rep_;  // See comment in write_batch.cc for the format of rep_
  const UserFrontiers* frontiers_ = nullptr;
  DirectWriter* direct_writer_ = nullptr;
  mutable size_t direct_entries_ = 0;

  Handler* handler_for_logging_ = nullptr;
};

}  // namespace rocksdb
