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

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/client/client.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/common/entity_ids_types.h"
#include "yb/common/transaction.h"
#include "yb/docdb/wait_queue.h"

namespace yb {
namespace tablet {

// Structure holding the required data of each blocker transaction.
struct BlockerTransactionInfo {
    TransactionId id;
    TabletId status_tablet;
    std::shared_ptr<const SubtxnSetAndPB> blocking_subtxn_info = nullptr;
};

using BlockerData = std::vector<BlockerTransactionInfo>;
using BlockerDataPtr = std::shared_ptr<BlockerData>;

struct WaiterData {
    HybridTime wait_start_time;
    BlockerDataPtr blockers;
};

// WaiterInfoEntry stores the wait-for dependencies of a waiter transaction received from a
// TabletServer. For waiter transactions spanning across tablet servers, we create multiple
// WaiterInfoEntry objects.
class WaiterInfoEntry {
 public:
  WaiterInfoEntry(
      const TransactionId& txn_id, const std::string& tserver_uuid,
      const std::shared_ptr<WaiterData>& waiter_data) :
    txn_id_(txn_id), tserver_uuid_(tserver_uuid), waiter_data_(waiter_data) {}

  const TransactionId& txn_id() const {
    return txn_id_;
  }

  const std::string& tserver_uuid() const {
    return tserver_uuid_;
  }

  std::pair<const TransactionId, const std::string> txn_id_tserver_uuid_pair() const {
    return std::pair<const TransactionId, const std::string>(txn_id_, tserver_uuid_);
  }

  const std::shared_ptr<WaiterData>& waiter_data() const {
    return waiter_data_;
  }

  void ResetWaiterData(const std::shared_ptr<WaiterData>& waiter_data) {
    waiter_data_ = waiter_data;
  }

  const TransactionId txn_id_;
  const std::string tserver_uuid_;
  std::shared_ptr<WaiterData> waiter_data_;
};

// Waiters is a multi-indexed container storing WaiterInfoEntry records. The records are indexed
// on 3 aspects -
// 1. unique hash index for pair<txn_id, tserver_uuid>: necessary for inserting new wait-for
//      relations on partial updates from the local_waiting_txn_registry of a given tserver.
// 2. sorted on txn_id: useful for fetching blocker information for a particular transaction across
//      all tservers. Need to maintain sorted order so as to iterate through the container and prune
//      inactive transactions. Maintaining a sorted order helps visit each transaction id only once.
// 3. non-unique hash on tserver_uuid: On full updates from a given tserver, the index helps erase
//      all exisiting entries corresponding to the given tserver_uuid.
//
// The container stores WaiterInfoEntry for each waiter txn and each tserver at which it occurs.
// A waiter txn can have multiple records when it is waiting on tablets at different tservers.
//
// Note: local_waiting_txn_registry sends two kinds of updates to the transaction coordinator, one
// is a partial update where new txn(s) entering the wait-queue are reported. The other is a
// full update, where all exisiting waiters at the tserver across all tablets are reported.
struct TransactionIdTag;
struct TserverUuidTag;
typedef boost::multi_index_container<WaiterInfoEntry,
    boost::multi_index::indexed_by <
        boost::multi_index::hashed_unique <
            boost::multi_index::const_mem_fun<WaiterInfoEntry,
            std::pair<const TransactionId, const std::string>,
            &WaiterInfoEntry::txn_id_tserver_uuid_pair>
        >,
        boost::multi_index::ordered_non_unique <
            boost::multi_index::tag<TransactionIdTag>,
            boost::multi_index::const_mem_fun<WaiterInfoEntry,
            const TransactionId&,
            &WaiterInfoEntry::txn_id>
        >,
        boost::multi_index::hashed_non_unique <
            boost::multi_index::tag<TserverUuidTag>,
            boost::multi_index::const_mem_fun<WaiterInfoEntry,
            const std::string&,
            &WaiterInfoEntry::tserver_uuid>
        >
    >
> Waiters;

// Specification used by the deadlock detector to interact with the transaction coordinator to abort
// transactions or determine which transactions are no longer running.
class TransactionAbortController {
 public:
  virtual void Abort(const TransactionId& transaction_id, TransactionStatusCallback callback) = 0;
  virtual void RemoveInactiveTransactions(Waiters* waiters) = 0;
  virtual bool IsAnySubtxnActive(const TransactionId& transaction_id,
                                 const SubtxnSet& subtxn_set) = 0;
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
// TODO(wait-queues): We can improve resolution of deadlocks by applying some consistent strategy,
// e.g. always abort just the lexicographically smallest txn id, to ensure that we don't
// concurrently abort multiple transactions in a deadlock if the same deadlock is detected by
// multiple coordinators concurrently.
class DeadlockDetector {
 public:
  DeadlockDetector(
      const std::shared_future<client::YBClient*>& client_future,
      TransactionAbortController* controller,
      const TabletId& status_tablet_id,
      const MetricEntityPtr& metrics);

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

  // Returns the deadlock status if the given transaction could have been involved in a deadlock.
  // Returns Status::OK() in all other cases.
  Status GetTransactionDeadlockStatus(const TransactionId& txn_id);

 private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace tablet
} // namespace yb
