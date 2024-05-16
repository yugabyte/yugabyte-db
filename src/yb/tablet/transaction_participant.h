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

#include <stdint.h>

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>

#include <boost/optional/optional.hpp>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/opid.h"
#include "yb/common/opid.pb.h"
#include "yb/common/transaction.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/server/server_fwd.h"

#include "yb/tablet/operations.fwd.h"
#include "yb/tablet/tablet_fwd.h"

#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/mem_tracker.h"

namespace rocksdb {

class DB;
class WriteBatch;

}

namespace yb {

class MetricEntity;
class HybridTime;
class OneWayBitmap;
class RWOperationCounter;
class TransactionMetadataPB;

namespace tserver {

class GetTransactionStatusAtParticipantResponsePB;
class TransactionStatePB;

}

namespace tablet {

struct TransactionApplyData {
  int64_t leader_term = -1;
  TransactionId transaction_id = TransactionId::Nil();
  SubtxnSet aborted;
  OpId op_id;
  HybridTime commit_ht;
  HybridTime log_ht;
  bool sealed = false;
  TabletId status_tablet;
  // Owned by running transaction if non-null.
  const docdb::ApplyTransactionState* apply_state = nullptr;

  std::string ToString() const;
};

struct RemoveIntentsData {
  OpId op_id;
  HybridTime log_ht;
};

struct GetIntentsData {
  OpIdPB op_id;
  HybridTime log_ht;
};

struct TransactionalBatchData {
  // Write id of last strong write intent in transaction.
  IntraTxnWriteId next_write_id = 0;

  // Hybrid time of last replicated write in transaction.
  HybridTime hybrid_time;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(next_write_id, hybrid_time);
  }
};

// TransactionParticipant manages running transactions, i.e. transactions that have intents in
// appropriate tablet. Since this class manages transactions of tablet there is separate class
// instance per tablet.
class TransactionParticipant : public TransactionStatusManager {
 public:
  TransactionParticipant(
      TransactionParticipantContext* context, TransactionIntentApplier* applier,
      const scoped_refptr<MetricEntity>& entity, const std::shared_ptr<MemTracker>& parent);
  virtual ~TransactionParticipant();

  void SetWaitQueue(std::unique_ptr<docdb::WaitQueue> wait_queue);

  docdb::WaitQueue* wait_queue() const;

  // Notify participant that this context is ready and it could start performing its requests.
  void Start();

  // Adds new running transaction.
  // Returns true if transaction was added, false if transaction already present.
  Result<bool> Add(const TransactionMetadata& metadata);

  Result<TransactionMetadata> PrepareMetadata(const LWTransactionMetadataPB& pb) override;
  Result<TransactionMetadata> PrepareMetadata(const TransactionMetadataPB& pb);

  // Prepares batch data for specified transaction id.
  // I.e. adds specified batch idx to set of replicated batches and fills encoded_replicated_batches
  // with new state of replicated batch indexes. Encoding does not matter for user of this function,
  // he should just append it to appropriate value.
  //
  // Returns boost::none when transaction is unknown.
  Result<boost::optional<std::pair<IsolationLevel, TransactionalBatchData>>> PrepareBatchData(
      const TransactionId& id, size_t batch_idx,
      boost::container::small_vector_base<uint8_t>* encoded_replicated_batches);

  void BatchReplicated(const TransactionId& id, const TransactionalBatchData& data);

  HybridTime LocalCommitTime(const TransactionId& id) override;

  boost::optional<TransactionLocalState> LocalTxnData(const TransactionId& id) override;

  void RequestStatusAt(const StatusRequest& request) override;

  void Abort(const TransactionId& id, TransactionStatusCallback callback) override;

  void Handle(std::unique_ptr<tablet::UpdateTxnOperation> request, int64_t term);

  Status Cleanup(TransactionIdSet&& set) override;

  // Used to pass arguments to ProcessReplicated.
  struct ReplicatedData {
    int64_t leader_term = -1;
    const LWTransactionStatePB& state;
    const OpId& op_id;
    HybridTime hybrid_time;
    bool sealed = false;
    AlreadyAppliedToRegularDB already_applied_to_regular_db;

    std::string ToString() const;
  };

  Status ProcessReplicated(const ReplicatedData& data);

  Status SetDB(
      const docdb::DocDB& db,
      RWOperationCounter* pending_op_counter_blocking_rocksdb_shutdown_start);

  Status CheckAborted(const TransactionId& id);

  Status FillPriorities(
      boost::container::small_vector_base<std::pair<TransactionId, uint64_t>>* inout) override;

  Result<boost::optional<TabletId>> FindStatusTablet(const TransactionId& id) override;

  void GetStatus(const TransactionId& transaction_id,
                 size_t required_num_replicated_batches,
                 int64_t term,
                 tserver::GetTransactionStatusAtParticipantResponsePB* response,
                 rpc::RpcContext* context);

  TransactionParticipantContext* context() const;

  HybridTime MinRunningHybridTime() const override;

  Result<HybridTime> WaitForSafeTime(HybridTime safe_time, CoarseTimePoint deadline) override;

  // When minimal start hybrid time of running transaction will be at least `ht` applier
  // method `MinRunningHybridTimeSatisfied` will be invoked.
  void WaitMinRunningHybridTime(HybridTime ht);

  void StartShutdown();

  void CompleteShutdown();

  // Resolve all transactions that were committed or aborted at resolve_at.
  // After this function returns with success:
  // - All intents of committed transactions will have been applied.
  // - No transactions can be committed with commit time <= resolve_at from that point on..
  Status ResolveIntents(HybridTime resolve_at, CoarseTimePoint deadline);

  // Attempts to abort all transactions that started prior to cutoff time.
  // Waits until deadline, for txns to abort. If not, it returns a TimedOut.
  // After this call, there should be no active (non-aborted/committed) txn that
  // started before cutoff which is active on this tablet.
  Status StopActiveTxnsPriorTo(
      HybridTime cutoff, CoarseTimePoint deadline, TransactionId* exclude_txn_id = nullptr);

  void IgnoreAllTransactionsStartedBefore(HybridTime limit);

  // Update transaction metadata to change the status tablet for the given transaction.
  Result<TransactionMetadata> UpdateTransactionStatusLocation(
      const TransactionId& transaction_id, const TabletId& new_status_tablet);

  std::string DumpTransactions() const;

  void SetIntentRetainOpIdAndTime(const yb::OpId& op_id, const MonoDelta& cdc_sdk_op_id_expiration);

  OpId GetRetainOpId() const;

  CoarseTimePoint GetCheckpointExpirationTime() const;

  OpId GetLatestCheckPoint() const;

  HybridTime GetMinStartTimeAmongAllRunningTransactions() const;

  OpId GetHistoricalMaxOpId() const;

  const TabletId& tablet_id() const override;

  void RecordConflictResolutionKeysScanned(int64_t num_keys) override;

  void RecordConflictResolutionScanLatency(MonoDelta latency) override;

  size_t GetNumRunningTransactions() const;

  // Returns pair of number of intents and number of transactions.
  Result<std::pair<size_t, size_t>> TEST_CountIntents() const;

  OneWayBitmap TEST_TransactionReplicatedBatches(const TransactionId& id) const;

 private:
  Result<int64_t> RegisterRequest() override;
  void UnregisterRequest(int64_t request) override;

  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace tablet
} // namespace yb
