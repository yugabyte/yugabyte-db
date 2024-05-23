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
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/client/client.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/common/entity_ids_types.h"
#include "yb/common/transaction.h"
#include "yb/docdb/wait_queue.h"

namespace yb {
namespace tablet {

// Structure holding the required data of each blocker transaction blocking the waiter request.
struct BlockerTransactionInfo {
  TransactionId id = TransactionId::Nil();
  TabletId status_tablet;
  // Using shared ptr here avoids SubtxnSet copy in WaiterInfoEntry::UpdateBlockingData.
  std::shared_ptr<const SubtxnSet> blocking_subtxn_set;

  bool operator==(const BlockerTransactionInfo& rhs) const {
    // Multiple requests of a waiter transaction could be blocked on different subtxn(s) of
    // a blocker transaction. Additionally a blocker transaction could be simultaneously
    // active at two status tablets in case of txn promotion. Hence compare all fields.
    const auto& lhs = *this;
    return YB_STRUCT_EQUALS(id, status_tablet) && *blocking_subtxn_set == *rhs.blocking_subtxn_set;
  }

  size_t hash_value() const {
    size_t seed = 0;
    boost::hash_combine(seed, id);
    boost::hash_combine(seed, status_tablet);
    boost::hash_combine(seed, *blocking_subtxn_set);
    return seed;
  }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(id, status_tablet, *blocking_subtxn_set);
  }
};

struct WaitingRequestsInfo {
  // Map of waiter rpc retryable request id and the time at which it was registered at the
  // local waiting txn registry.
  std::unordered_map<int64_t, HybridTime> waiting_requests;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(waiting_requests);
  }

  void UpdateRequests(const WaitingRequestsInfo& old_info) {
    for (const auto& [id, start_time] : old_info.waiting_requests) {
      // Ignore updating start time of exisiting request ids, since they are expected to be
      // the newer ones.
      waiting_requests.try_emplace(id, start_time);
    }
  }
};

// BlockingInfo struct captures the dependency info of a waiter transaction on a request level
// granularity. It helps avoids duplication of BlockerTransactionInfo(s) when multiple requests
// of a waiter could be blocked on the same (blocker_id, subtxn, status_tablet) tuple.
//
// Note: For read requests with explicit, conflict resolution code populates the request id as
// -1. Since we find/create the 'BlockingInfo' for each request based on hash of the tuple above,
// it wouldn't lead to ovewriting issues even if we receive multiple waiting requests of a waiter
// txn with id -1 from different tablets (as the request id would get appended to multiple keys
// of 'BlockingData').
struct BlockingInfo {
  BlockerTransactionInfo blocker_txn_info;
  WaitingRequestsInfo waiting_requests_info;
  // Indicates whether the blocker is still active i.e. if the blocker txn and the involved
  // subtxn(s) are still active.
  mutable std::atomic<bool> is_active{true};

  BlockingInfo(
      BlockerTransactionInfo&& blocker_txn_info_, WaitingRequestsInfo&& waiting_requests_info_)
          : blocker_txn_info(std::move(blocker_txn_info_)),
            waiting_requests_info(std::move(waiting_requests_info_)) {}

  BlockingInfo(const BlockingInfo& other) {
    blocker_txn_info = other.blocker_txn_info;
    waiting_requests_info = other.waiting_requests_info;
    is_active.store(other.is_active);
  }

  bool operator==(const BlockingInfo& info) const {
    return blocker_txn_info == info.blocker_txn_info;
  }

  size_t hash_value() const {
    return blocker_txn_info.hash_value();
  }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(blocker_txn_info, waiting_requests_info, is_active);
  }

  void UpdateWaitingRequestsInfo(const WaitingRequestsInfo& info) {
    waiting_requests_info.UpdateRequests(info);
  }
};

inline std::size_t hash_value(const BlockerTransactionInfo& blocker_txn_info) noexcept {
  return blocker_txn_info.hash_value();
}

using BlockingData = boost::multi_index_container<BlockingInfo,
    boost::multi_index::indexed_by <
        boost::multi_index::hashed_unique <
            BOOST_MULTI_INDEX_MEMBER(BlockingInfo, BlockerTransactionInfo, blocker_txn_info)
        >
    >
>;
using BlockingDataPtr = std::shared_ptr<BlockingData>;

// WaiterInfoEntry stores the wait-for dependencies of a waiter transaction received from a
// TabletServer. For waiter transactions spanning across tablet servers, we create multiple
// WaiterInfoEntry objects.
class WaiterInfoEntry {
 public:
  WaiterInfoEntry(
      const TransactionId& txn_id, const std::string& tserver_uuid,
      const BlockingDataPtr& blocking_data) :
    txn_id_(txn_id), tserver_uuid_(tserver_uuid), blocking_data_(blocking_data) {}

  const TransactionId& txn_id() const {
    return txn_id_;
  }

  const std::string& tserver_uuid() const {
    return tserver_uuid_;
  }

  std::pair<const TransactionId, const std::string> txn_id_tserver_uuid_pair() const {
    return std::pair<const TransactionId, const std::string>(txn_id_, tserver_uuid_);
  }

  const BlockingDataPtr& blocking_data() const {
    return blocking_data_;
  }

  void ResetBlockingData(const BlockingDataPtr& blocking_data) {
    blocking_data_ = blocking_data;
  }

  void UpdateBlockingData(const BlockingDataPtr& old_blocking_data);

  std::string ToString() const {
    return YB_CLASS_TO_STRING(txn_id, tserver_uuid, *blocking_data);
  }

  const TransactionId txn_id_;
  const std::string tserver_uuid_;
  BlockingDataPtr blocking_data_;
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
class TransactionStatusController {
 public:
  virtual void RemoveInactiveTransactions(Waiters* waiters) = 0;
  // Returns Aborted status if the blocking probe isn't active anymore, and need not be forwarded.
  // Else, returns Status::OK().
  virtual Status CheckProbeActive(
      const TransactionId& transaction_id, const SubtxnSet& subtxn_set) = 0;
  virtual std::optional<MicrosTime> GetTxnStart(const TransactionId& transaction_id) = 0;
  virtual const std::string& LogPrefix() = 0;
  virtual ~TransactionStatusController() = default;
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
      TransactionStatusController* controller,
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

 private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace tablet
} // namespace yb
