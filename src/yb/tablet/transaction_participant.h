//
// Copyright (c) YugabyteDB, Inc.
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

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/opid.h"
#include "yb/common/opid.pb.h"
#include "yb/common/transaction.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/storage_set.h"

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
  SubtxnSet aborted = {};
  OpId op_id;
  HybridTime commit_ht;
  HybridTime log_ht = {};
  bool sealed = false;
  TabletId status_tablet = {};
  docdb::StorageSet apply_to_storages = docdb::StorageSet::All();

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

class FastModeTransactionScope;

YB_STRONGLY_TYPED_BOOL(OnlyAbortTxnsNotUsingTableLocks);

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
  Result<bool> Add(const TransactionMetadata& metadata, HybridTime batch_write_ht);

  Result<TransactionMetadata> PrepareMetadata(const LWTransactionMetadataPB& pb) override;
  Result<TransactionMetadata> PrepareMetadata(const TransactionMetadataPB& pb);

  // Prepares batch data for specified transaction id.
  // I.e. adds specified batch idx to set of replicated batches and fills encoded_replicated_batches
  // with new state of replicated batch indexes. Encoding does not matter for user of this function,
  // he should just append it to appropriate value.
  //
  // Returns std::nullopt when transaction is unknown.
  Result<std::optional<std::pair<IsolationLevel, TransactionalBatchData>>> PrepareBatchData(
      const TransactionId& id, size_t batch_idx,
      boost::container::small_vector_base<uint8_t>* encoded_replicated_batches,
      bool has_write_pairs);

  void BatchReplicated(const TransactionId& id, const TransactionalBatchData& data);

  HybridTime LocalCommitTime(const TransactionId& id) override;

  std::optional<TransactionLocalState> LocalTxnData(const TransactionId& id) override;

  void RequestStatusAt(const StatusRequest& request) override;

  void Abort(const TransactionId& id, TransactionStatusCallback callback) override;

  void Handle(std::unique_ptr<tablet::UpdateTxnOperation> request, int64_t term);

  Status Cleanup(TransactionIdApplyOpIdMap&& set) override;

  // Used to pass arguments to ProcessReplicated.
  struct ReplicatedData {
    int64_t leader_term = -1;
    const LWTransactionStatePB& state;
    const OpId& op_id;
    HybridTime hybrid_time;
    bool sealed = false;
    docdb::StorageSet apply_to_storages;

    std::string ToString() const;
  };

  Status ProcessReplicated(const ReplicatedData& data);

  Status SetDB(
      const docdb::DocDB& db,
      RWOperationCounter* pending_op_counter_blocking_rocksdb_shutdown_start);

  Status CheckAborted(const TransactionId& id);

  Status FillPriorities(
      boost::container::small_vector_base<std::pair<TransactionId, uint64_t>>* inout) override;

  Result<std::optional<TabletId>> FindStatusTablet(const TransactionId& id) override;

  void GetStatus(
      const TransactionId& transaction_id, size_t required_num_replicated_batches, int64_t term,
      tserver::GetTransactionStatusAtParticipantResponsePB* response, rpc::RpcContext* context);

  TransactionParticipantContext* context() const;

  void SetMinReplayTxnFirstWriteTimeLowerBound(HybridTime hybrid_time);

  HybridTime MinReplayTxnFirstWriteTime() const;

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
      OnlyAbortTxnsNotUsingTableLocks only_abort_txns_not_using_table_locks, HybridTime cutoff,
      CoarseTimePoint deadline, TransactionId* exclude_txn_id = nullptr);

  void IgnoreAllTransactionsStartedBefore(HybridTime limit);

  // Update transaction metadata to change the status tablet for the given transaction.
  Status UpdateTransactionStatusLocation(
      const TransactionId& transaction_id, const TabletId& new_status_tablet);

  std::string DumpTransactions() const;

  void SetIntentRetainOpIdAndTime(
      const yb::OpId& op_id, const MonoDelta& cdc_sdk_op_id_expiration,
      HybridTime min_start_ht_cdc_unstreamed_txns);

  OpId GetRetainOpId() const;

  CoarseTimePoint GetCheckpointExpirationTime() const;

  OpId GetLatestCheckPoint() const;

  HybridTime GetMinStartHTCDCUnstreamedTxns() const;

  OpId GetHistoricalMaxOpId() const;

  const TabletId& tablet_id() const override;

  void RecordConflictResolutionKeysScanned(int64_t num_keys) override;

  void RecordConflictResolutionScanLatency(MonoDelta latency) override;

  size_t GetNumRunningTransactions() const;

  void SetMinReplayTxnFirstWriteTimeUpdateCallback(std::function<void(HybridTime)> callback);

  struct CountIntentsResult {
    size_t num_intents;
    size_t num_transactions;
    size_t num_post_apply;
  };
  // Returns pair of number of intents, number of transactions, and number of post-apply
  // records.
  Result<CountIntentsResult> TEST_CountIntents() const;

  OneWayBitmap TEST_TransactionReplicatedBatches(const TransactionId& id) const;

  Result<HybridTime> SimulateProcessRecentlyAppliedTransactions(
      const OpId& retryable_requests_flushed_op_id);

  void SetRetryableRequestsFlushedOpId(const OpId& flushed_op_id);

  Status ProcessRecentlyAppliedTransactions();

  void ForceRefreshWaitersForBlocker(const TransactionId& txn_id);

  // Returns a 'FastModeTransactionScope' indicating whether the fast mode should be used. Based on
  // the isolation level and the returned value, we can determine whether to skip the prefix lock.
  Result<FastModeTransactionScope> ShouldUseFastMode(
      IsolationLevel isolation, bool skip_prefix_locks, const TransactionId& id);
  std::pair<uint64_t, uint64_t> GetNumFastModeTransactions();

 private:
  Result<int64_t> RegisterRequest() override;
  void UnregisterRequest(int64_t request) override;

  friend class FastModeTransactionScope;

  class Impl;
  std::shared_ptr<Impl> impl_;
};

class FastModeTransactionScope {
 public:
  FastModeTransactionScope() = default;
  FastModeTransactionScope(std::shared_ptr<TransactionParticipant::Impl> participant, size_t idx);

  FastModeTransactionScope(const FastModeTransactionScope& rhs);
  FastModeTransactionScope(FastModeTransactionScope&& rhs);

  void operator=(const FastModeTransactionScope& rhs);
  void operator=(FastModeTransactionScope&& rhs);

  ~FastModeTransactionScope() {
    Reset();
  }

  bool active() const {
    return participant_ != nullptr;
  }

  explicit operator bool() const {
    return active();
  }

  void Reset();

 private:
  std::shared_ptr<TransactionParticipant::Impl> participant_;
  size_t idx_ = 0;
};

} // namespace tablet
} // namespace yb
