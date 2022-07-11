// Copyright (c) YugaByte, Inc.
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

#ifndef YB_DOCDB_WAIT_QUEUE_H
#define YB_DOCDB_WAIT_QUEUE_H

#include "yb/common/common_fwd.h"
#include "yb/common/transaction.h"

#include "yb/docdb/lock_batch.h"

#include "yb/util/threadpool.h"

namespace yb {
namespace docdb {

// Callback used by the WaitQueue to signal the result of waiting. Can be used by conflict
// resolution to signal failure to client or retry conflict resolution.
using WaitDoneCallback = std::function<void(const Status&)>;

// This class is responsible for coordinating conflict transactions which are still running. A
// running transaction can enter the wait queue while blocking on other running transactions in
// order to be continued once blocking transactions are resolved.
class WaitQueue {
 public:
  WaitQueue(
      TransactionStatusManager* txn_status_manager,
      const std::string& permanent_uuid);

  ~WaitQueue();

  // Wait on the transactions in blockers on behalf of waiter. Once all transactions in blockers
  // are found to be resolved (committed or aborted), signal Status::OK to the provided callback.
  // If there is an error in the course of waiting, such as tablet shutdown, signal it to the
  // callback. If there is an error registering this waiter, return error status on this call. Once
  // waiting starts, unlock the provided LockBatch, and before signaling success to the provided
  // callback, re-lock the provided locks. If re-locking fails, signal failure to the provided
  // callback.
  Status WaitOn(
      const TransactionId& waiter, LockBatch* locks, const std::vector<TransactionId>& blockers,
      WaitDoneCallback callback);

  void PollBlockerStatus(HybridTime now);

  void StartShutdown();

  void CompleteShutdown();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_WAIT_QUEUE_H
