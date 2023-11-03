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

#pragma once

#include <stdint.h>

#include "yb/client/client.h"

#include "yb/common/common_fwd.h"
#include "yb/common/transaction.h"

#include "yb/docdb/conflict_data.h"
#include "yb/docdb/lock_batch.h"

#include "yb/server/server_fwd.h"

#include "yb/util/threadpool.h"

namespace yb {
namespace docdb {

class ScopedWaitingTxnRegistration {
 public:
  virtual Status Register(
    const TransactionId& waiting,
    std::shared_ptr<ConflictDataManager> blockers,
    const TabletId& status_tablet) = 0;
  virtual int64 GetDataUseCount() const = 0;
  virtual ~ScopedWaitingTxnRegistration() = default;
};

class WaitingTxnRegistry {
 public:
  virtual std::unique_ptr<ScopedWaitingTxnRegistration> Create() = 0;
  virtual ~WaitingTxnRegistry() = default;
};

// Callback used by the WaitQueue to signal the result of waiting. Can be used by conflict
// resolution to signal failure to client or retry conflict resolution.
using WaitDoneCallback = std::function<void(const Status&, HybridTime)>;

// This class is responsible for coordinating conflict transactions which are still running. A
// running transaction can enter the wait queue while blocking on other running transactions in
// order to be continued once blocking transactions are resolved.
class WaitQueue {
 public:
  WaitQueue(
      TransactionStatusManager* txn_status_manager,
      const std::string& permanent_uuid,
      WaitingTxnRegistry* waiting_txn_registry,
      const std::shared_future<client::YBClient*>& client_future,
      const server::ClockPtr& clock,
      const MetricEntityPtr& metrics,
      std::unique_ptr<ThreadPoolToken> thread_pool_token);

  ~WaitQueue();

  // Wait on the transactions in blockers on behalf of waiter. Once all transactions in blockers
  // are found to be resolved (committed or aborted), signal Status::OK to the provided callback.
  // If there is an error in the course of waiting, such as tablet shutdown, signal it to the
  // callback. If there is an error registering this waiter, return error status on this call. Once
  // waiting starts, unlock the provided LockBatch, and before signaling success to the provided
  // callback, re-lock the provided locks. If re-locking fails, signal failure to the provided
  // callback.
  Status WaitOn(
      const TransactionId& waiter, SubTransactionId subtxn_id, LockBatch* locks,
      std::shared_ptr<ConflictDataManager> blockers, const TabletId& status_tablet_id,
      uint64_t serial_no, int64_t txn_start_us, WaitDoneCallback callback);

  // Check the wait queue for any active blockers which would conflict with locks. This method
  // should be called as the first step in conflict resolution when processing a new request to
  // ensure incoming requests do not starve existing blocked requests which are about to resume.
  // Returns true if this call results in the request entering the wait queue, in which case the
  // provided callback is used as described in the comment of WaitOn() above. Returns false in case
  // the request is not entered into the wait queue and the callback is never invoked. Returns
  // status in case of some unresolvable error.
  Result<bool> MaybeWaitOnLocks(
      const TransactionId& waiter, SubTransactionId subtxn_id, LockBatch* locks,
      const TabletId& status_tablet_id, uint64_t serial_no, int64_t txn_start_us,
      WaitDoneCallback callback);

  void Poll(HybridTime now);

  void StartShutdown();

  void CompleteShutdown();

  // Accept a signal that the given transaction was committed at the given commit_ht.
  void SignalCommitted(const TransactionId& id, HybridTime commit_ht);

  // Accept a signal that the given transaction was aborted.
  void SignalAborted(const TransactionId& id);

  // Accept a signal that the given transaction was promoted.
  void SignalPromoted(const TransactionId& id, TransactionStatusResult&& res);

  // Provides access to a monotonically increasing serial number to be used by waiting requests to
  // enforce fairness in a best effort manner. Incoming requests should retain a serial number as
  // soon as they begin conflict resolution, and the same serial number should be used any time the
  // request enters the wait queue, to ensure it is resolved before any requests which arrived later
  // than it did to this tserver.
  uint64_t GetSerialNo();

  // Output html to display information to an admin page about the internal state of this wait
  // queue. Useful for debugging.
  void DumpStatusHtml(std::ostream& out);

  // Populate tablet_locks_info with awaiting lock information corresponding to waiter transactions
  // from this wait queue. If transactions is not empty, restrict returned information to locks
  // which are requested by the given set of transactions.
  Status GetLockStatus(const std::map<TransactionId, SubtxnSet>& transactions,
                       const TableInfoProvider& table_info_provider,
                       TransactionLockInfoManager* lock_info_manager) const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace docdb
} // namespace yb
