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

#include "yb/client/client.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/common/entity_ids_types.h"
#include "yb/common/transaction.h"
#include "yb/docdb/wait_queue.h"

namespace yb {
namespace tablet {

using BlockerData = std::vector<BlockingTransactionData>;
struct WaiterData {
    HybridTime wait_start_time;
    BlockerData blockers;
};
using Waiters = std::unordered_map<TransactionId,
                                   std::shared_ptr<WaiterData>,
                                   TransactionIdHash>;

// Specification used by the deadlock detector to interact with the transaction coordinator to abort
// transactions or determine which transactions are no longer running.
class TransactionAbortController {
 public:
  virtual void Abort(const TransactionId& transaction_id, TransactionStatusCallback callback) = 0;
  virtual void RemoveInactiveTransactions(Waiters* waiters) = 0;
  virtual ~TransactionAbortController() = default;
};

using DeadlockDetectorRpcCallback = std::function<void(const Status&)>;

// Implement probe-based edge chasing deadlock detection inspired by the following paper:
//
// A Distributed Algorithm for Detecting Resource Deadlocks in Distributed Systems
// Chandy and Misra, 1982
// https://www.cs.utexas.edu/users/misra/scannedPdf.dir/ResourceDeadlock.pdf
//
// When a transaction coordinator receives an UpdateTransactionWaitingForStatusRequestPB request
// from a tserver, it forwards this directly to the deadlock detector. The deadlock detector then
// adds or overwrites information for each waiting transaction_id found in that request.
//
// On a regular interval (controlled by FLAGS_transaction_deadlock_detection_interval_usec), the
// deadlock detector will scan all waiting transactions and, for each, do the following:
// 1. for each blocker:
// 2.    probe_id = (probe_no++,detector_id)
// 3.    send probe{probe_id, waiter_id, blocker_id} to blocker's coordinator
//
// Upon receiving a ProbeTransactionDeadlockRequestPB, a coordinator forwards it directly to the
// deadlock detector which does the following:
// 1. if probe_id is one being tracked by this deadlock detector, and it was originated by the
//    blocker specified in this probe, then respond indicating that a deadlock has been detected
// 2. else if the blocker specified in the probe itself has blockers, forward the probe to each of
//    those coordinators with the same probe_id and waiter_id/blocker_id updated accordingly
//
// When a deadlock is detected, either locally or remotely, attach the waiter who triggered that
// probe to the response and pass it back to the caller. The originator of the probe should then
// receive an ordered list of all transactions involved in the deadlock, which can be used to
// determine how to resolve the deadlock. Currently, this deadlock is resolved by aborting the
// transaction from which the probe originated.
//
// TODO(pessimistic): We can improve resolution of deadlocks by applying some consistent strategy,
// e.g. always abort just the lexicographically smallest txn id, to ensure that we don't
// concurrently abort multiple transactions in a deadlock if the same deadlock is detected by
// multiple coordinators concurrently.
class DeadlockDetector {
 public:
  DeadlockDetector(
      const std::shared_future<client::YBClient*>& client_future,
      TransactionAbortController* controller,
      const TabletId& status_tablet_id);

  ~DeadlockDetector();

  void ProcessProbe(
      const tserver::ProbeTransactionDeadlockRequestPB& req,
      tserver::ProbeTransactionDeadlockResponsePB* resp,
      DeadlockDetectorRpcCallback&& callback);

  void ProcessWaitFor(
      const tserver::UpdateTransactionWaitingForStatusRequestPB& req,
      tserver::UpdateTransactionWaitingForStatusResponsePB* resp,
      DeadlockDetectorRpcCallback&& callback);

  void TriggerProbes();

  void Shutdown();

 private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace tablet
} // namespace yb
