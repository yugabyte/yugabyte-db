// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
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

#ifndef ROCKSDB_LITE

#include <atomic>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/rocksdb/db/write_callback.h"
#include "yb/rocksdb/db.h"
#include "yb/util/slice.h"
#include "yb/rocksdb/snapshot.h"
#include "yb/rocksdb/status.h"
#include "yb/rocksdb/types.h"
#include "yb/rocksdb/utilities/transaction.h"
#include "yb/rocksdb/utilities/transaction_db.h"
#include "yb/rocksdb/utilities/write_batch_with_index.h"
#include "yb/rocksdb/utilities/transactions/transaction_base.h"
#include "yb/rocksdb/utilities/transactions/transaction_util.h"

namespace rocksdb {

using TransactionID = uint64_t;

class TransactionDBImpl;

class TransactionImpl : public TransactionBaseImpl {
 public:
  TransactionImpl(TransactionDB* db, const WriteOptions& write_options,
                  const TransactionOptions& txn_options);

  virtual ~TransactionImpl();

  void Reinitialize(TransactionDB* txn_db, const WriteOptions& write_options,
                    const TransactionOptions& txn_options);

  Status Commit() override;

  Status CommitBatch(WriteBatch* batch);

  void Rollback() override;

  Status RollbackToSavePoint() override;

  // Generate a new unique transaction identifier
  static TransactionID GenTxnID();

  TransactionID GetTxnID() const { return txn_id_; }

  // Returns the time (in microseconds according to Env->GetMicros())
  // that this transaction will be expired.  Returns 0 if this transaction does
  // not expire.
  uint64_t GetExpirationTime() const { return expiration_time_; }

  // returns true if this transaction has an expiration_time and has expired.
  bool IsExpired() const;

  // Returns the number of microseconds a transaction can wait on acquiring a
  // lock or -1 if there is no timeout.
  int64_t GetLockTimeout() const { return lock_timeout_; }
  void SetLockTimeout(int64_t timeout) override {
    lock_timeout_ = timeout * 1000;
  }

  // Returns true if locks were stolen successfully, false otherwise.
  bool TryStealingLocks();

 protected:
  Status TryLock(ColumnFamilyHandle* column_family, const Slice& key,
                 bool read_only, bool untracked = false) override;

 private:
  enum ExecutionStatus { STARTED, COMMITTING, LOCKS_STOLEN };

  TransactionDBImpl* txn_db_impl_;

  // Used to create unique ids for transactions.
  static std::atomic<TransactionID> txn_id_counter_;

  // Unique ID for this transaction
  TransactionID txn_id_;

  // If non-zero, this transaction should not be committed after this time (in
  // microseconds according to Env->NowMicros())
  uint64_t expiration_time_;

  // Timeout in microseconds when locking a key or -1 if there is no timeout.
  int64_t lock_timeout_;

  // Execution status of the transaction.
  std::atomic<ExecutionStatus> exec_status_;

  void Clear() override;

  void Initialize(const TransactionOptions& txn_options);

  Status ValidateSnapshot(ColumnFamilyHandle* column_family, const Slice& key,
                          SequenceNumber prev_seqno, SequenceNumber* new_seqno);

  Status LockBatch(WriteBatch* batch, TransactionKeyMap* keys_to_unlock);

  Status DoCommit(WriteBatch* batch);

  void RollbackLastN(size_t num);

  void UnlockGetForUpdate(ColumnFamilyHandle* column_family,
                          const Slice& key) override;

  // No copying allowed
  TransactionImpl(const TransactionImpl&);
  void operator=(const TransactionImpl&);
};

// Used at commit time to check whether transaction is committing before its
// expiration time.
class TransactionCallback : public WriteCallback {
 public:
  explicit TransactionCallback(TransactionImpl* txn) : txn_(txn) {}

  Status Callback(DB* db) override {
    if (txn_->IsExpired()) {
      return STATUS(Expired, "");
    } else {
      return Status::OK();
    }
  }

  bool AllowWriteBatching() override { return true; }

 private:
  TransactionImpl* txn_;
};

}  // namespace rocksdb

#endif  // ROCKSDB_LITE

