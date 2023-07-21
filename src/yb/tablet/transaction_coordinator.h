//
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
//

#pragma once

#include <future>
#include <memory>

#include "yb/client/client_fwd.h"

#include "yb/common/hybrid_time.h"

#include "yb/docdb/deadlock_detector.h"

#include "yb/gutil/ref_counted.h"

#include "yb/server/server_fwd.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/metrics_fwd.h"
#include "yb/util/status_fwd.h"
#include "yb/util/enums.h"

namespace google {
namespace protobuf {
template <class T>
class RepeatedPtrField;
}
}


namespace yb {
namespace tablet {

// Get current transaction timeout.
std::chrono::microseconds GetTransactionTimeout(bool is_external);

// Context for transaction coordinator. I.e. access to external facilities required by
// transaction coordinator to do its job.
class TransactionCoordinatorContext {
 public:
  virtual const std::string& tablet_id() const = 0;
  virtual const std::shared_future<client::YBClient*>& client_future() const = 0;
  virtual int64_t LeaderTerm() const = 0;
  virtual const server::ClockPtr& clock_ptr() const = 0;
  virtual Result<HybridTime> LeaderSafeTime() const = 0;

  // Returns current hybrid time lease expiration.
  // Valid only if we are leader.
  virtual Result<HybridTime> HtLeaseExpiration() const = 0;

  virtual void UpdateClock(HybridTime hybrid_time) = 0;
  virtual std::unique_ptr<UpdateTxnOperation> CreateUpdateTransaction(
      std::shared_ptr<LWTransactionStatePB> request) = 0;
  virtual Status SubmitUpdateTransaction(
      std::unique_ptr<UpdateTxnOperation> operation, int64_t term) = 0;

  server::Clock& clock() const {
    return *clock_ptr();
  }

 protected:
  ~TransactionCoordinatorContext() {}
};

typedef std::function<void(Result<TransactionStatusResult>)> TransactionAbortCallback;

// Coordinates all transactions managed by specific tablet, i.e. all transactions
// that selected this tablet as status tablet for it.
// Also it handles running transactions, i.e. transactions that has intents in appropriate tablet.
// Each tablet has separate transaction coordinator.
class TransactionCoordinator {
 public:
  TransactionCoordinator(const std::string& permanent_uuid,
                         TransactionCoordinatorContext* context,
                         TabletMetrics* tablet_metrics,
                         const MetricEntityPtr& metrics);
  ~TransactionCoordinator();

  // Used to pass arguments to ProcessReplicated.
  struct ReplicatedData {
    int64_t leader_term;
    const LWTransactionStatePB& state;
    const OpId& op_id;
    HybridTime hybrid_time;

    std::string ToString() const;
  };

  // Process new transaction state.
  Status ProcessReplicated(const ReplicatedData& data);

  struct AbortedData {
    const LWTransactionStatePB& state;
    const OpId& op_id;

    std::string ToString() const;
  };

  // Process transaction state replication aborted.
  void ProcessAborted(const AbortedData& data);
  // Handles new request for transaction update.
  void Handle(std::unique_ptr<tablet::UpdateTxnOperation> request, int64_t term);

  // Prepares log garbage collection. Return min index that should be preserved.
  int64_t PrepareGC(std::string* details = nullptr);

  // Starts background processes of transaction coordinator.
  void Start();

  // Stop background processes of transaction coordinator.
  // And like most of other Shutdowns in our codebase it wait until shutdown completes.
  void Shutdown();

  // Prepares tablet for deletion. This waits until the transaction coordinator has stopped
  // accepting new transactions, all running transactions have finished, and all intents
  // for committed transactions have been applied. This does not ensure that all aborted
  // transactions' intents have been cleaned up.
  Status PrepareForDeletion(const CoarseTimePoint& deadline);

  Status GetStatus(const google::protobuf::RepeatedPtrField<std::string>& transaction_ids,
                   CoarseTimePoint deadline,
                   tserver::GetTransactionStatusResponsePB* response);

  void Abort(const TransactionId& transaction_id, int64_t term, TransactionAbortCallback callback);

  // CancelTransactionIfFound returns true if the transaction is found in the list of managed txns
  // at the coordinator, and invokes the callback with the cancelation status. If the txn isn't
  // found, it returns false and the callback is not invoked.
  bool CancelTransactionIfFound(
      const TransactionId& transaction_id, int64_t term, TransactionAbortCallback callback);

  // Populates old transactions and their metadata (involved tablets, active subtransactions) based
  // on the arguments in the request. Used by pg_client_service to determine which transactions to
  // display in pg_locks/yb_lock_status.
  Status GetOldTransactions(
      const tserver::GetOldTransactionsRequestPB* req,
      tserver::GetOldTransactionsResponsePB* resp,
      CoarseTimePoint deadline);

  std::string DumpTransactions();

  // Returns count of managed transactions. Used in tests.
  size_t test_count_transactions() const;

  void ProcessWaitForReport(
      const tserver::UpdateTransactionWaitingForStatusRequestPB& req,
      tserver::UpdateTransactionWaitingForStatusResponsePB* resp,
      DeadlockDetectorRpcCallback&& callback);

  void ProcessProbe(
      const tserver::ProbeTransactionDeadlockRequestPB& req,
      tserver::ProbeTransactionDeadlockResponsePB* resp,
      DeadlockDetectorRpcCallback&& callback);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace tablet
} // namespace yb
