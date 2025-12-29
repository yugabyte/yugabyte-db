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

#include <future>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "yb/ash/ash_fwd.h"

#include "yb/common/consistent_read_point.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/client/client_fwd.h"
#include "yb/client/in_flight_op.h"
#include "yb/common/pg_types.h"

#include "yb/util/status_callback.h"
#include "yb/util/status_fwd.h"

namespace yb {

class HybridTime;

class Trace;

namespace client {

using Waiter = boost::function<void(const Status&)>;
using PrepareChildCallback = std::function<void(const Result<ChildTransactionDataPB>&)>;

struct ChildTransactionData {
  TransactionMetadata metadata;
  ReadHybridTime read_time;
  ConsistentReadPoint::HybridTimeMap local_limits;

  static Result<ChildTransactionData> FromPB(const ChildTransactionDataPB& data);
};

template<class T>
class ConstStaticWrapper {
 public:
  const T& Get() const {
    return ref_.get();
  }

  template<const T* U>
  static ConstStaticWrapper Build() {
    return ConstStaticWrapper(*U);
  }

 private:
  explicit ConstStaticWrapper(const T& ref)
      : ref_(ref) {}

  std::reference_wrapper<const T> ref_;
};

using LogPrefixName = ConstStaticWrapper<std::string>;

// SealOnly is a special commit mode.
// I.e. sealed transaction will be committed after seal record and all write batches are replicated.
YB_STRONGLY_TYPED_BOOL(SealOnly);

// YBTransaction is a representation of a single transaction.
// After YBTransaction is created, it could be used during construction of YBSession,
// to indicate that this session will send commands related to this transaction.
class YBTransaction : public std::enable_shared_from_this<YBTransaction> {
 private:
  class PrivateOnlyTag {};

 public:
  explicit YBTransaction(TransactionManager* manager,
                         TransactionFullLocality locality = TransactionFullLocality::Global());

  // Trick to allow std::make_shared with this ctor only from methods of this class.
  YBTransaction(TransactionManager* manager, const TransactionMetadata& metadata, PrivateOnlyTag);

  // Creates "child" transaction.
  // Child transaction shares same metadata as parent transaction, so all writes are done
  // as part of parent transaction.
  // But lifetime is controlled by parent transaction.
  // I.e. only parent transaction could be committed or aborted, also only parent transaction
  // sends heartbeats.
  YBTransaction(TransactionManager* manager, ChildTransactionData data);

  ~YBTransaction();

  Trace *trace();
  void EnsureTraceCreated();
  void SetPriority(uint64_t priority);

  uint64_t GetPriority() const;

  // Should be invoked to complete transaction creation.
  // Transaction is unusable before Init is called.
  Status Init(
      IsolationLevel isolation, const ReadHybridTime& read_time = ReadHybridTime());

  void RestartStartTime();

  void SetStartTimeIfNecessary();

  // Allows starting a transaction that reuses an existing read point.
  void InitWithReadPoint(IsolationLevel isolation, ConsistentReadPoint&& read_point);

  internal::TxnBatcherIf& batcher_if();

  // Commits this transaction.
  void Commit(CoarseTimePoint deadline, SealOnly seal_only, CommitCallback callback);

  void Commit(CoarseTimePoint deadline, CommitCallback callback);

  void Commit(CommitCallback callback);

  // Utility function for Commit.
  std::future<Status> CommitFuture(
      CoarseTimePoint deadline = CoarseTimePoint(), SealOnly seal_only = SealOnly::kFalse);

  // Aborts this transaction.
  void Abort(CoarseTimePoint deadline = CoarseTimePoint());

  // Make sure transaction is global.
  Status EnsureGlobal(CoarseTimePoint deadline = CoarseTimePoint());

  // Returns transaction ID.
  const TransactionId& id() const;

  const ConsistentReadPoint& read_point() const;
  ConsistentReadPoint& read_point();

  bool IsRestartRequired() const;

  // Creates restarted transaction, this transaction should be in the "restart required" state.
  Result<YBTransactionPtr> CreateRestartedTransaction();

  // Setup precreated transaction to be restarted version of this transaction.
  Status FillRestartedTransaction(const YBTransactionPtr& dest);

  // Prepares child data, so child transaction could be started in another server.
  // Should be async because status tablet could be not ready yet.
  void PrepareChild(
      ForceConsistentRead force_consistent_read, CoarseTimePoint deadline,
      PrepareChildCallback callback);

  std::future<Result<ChildTransactionDataPB>> PrepareChildFuture(
      ForceConsistentRead force_consistent_read, CoarseTimePoint deadline = CoarseTimePoint());

  // After we finish all child operations, we should finish child and send result to parent.
  Result<ChildTransactionResultPB> FinishChild();

  // Apply results from child to this parent transaction.
  // `result` should be prepared with FinishChild of child transaction.
  Status ApplyChildResult(const ChildTransactionResultPB& result);

  std::shared_future<Result<TransactionMetadata>> GetMetadata(CoarseTimePoint deadline) const;

  // Returns metadata iff the transaction is ready and hasn't failed yet.
  Result<TransactionMetadata> metadata() const;

  std::string ToString() const;

  IsolationLevel isolation() const;

  // Releases this transaction object returning its metadata.
  // So this transaction could be used by some other application instance.
  Result<TransactionMetadata> Release();

  void SetActiveSubTransaction(SubTransactionId id);

  std::optional<SubTransactionMetadataPB> GetSubTransactionMetadataPB() const;

  Status SetPgTxnStart(int64_t pg_txn_start_us, bool using_table_locks);

  Status RollbackToSubTransaction(SubTransactionId id, CoarseTimePoint deadline);

  bool HasSubTransaction(SubTransactionId id);

  void SetLogPrefixTag(const LogPrefixName& name, uint64_t value);

  void IncreaseMutationCounts(
      SubTransactionId subtxn_id, const TableId& table_id, uint64_t mutation_count);

  // Get aggregated mutations for each table across the whole transaction (exclude aborted
  // sub-transactions).
  std::vector<std::pair<TableId, uint64_t>> GetTableMutationCounts() const;

  bool OldTransactionAborted() const;

  // For docdb transactions of type PgClientSessionKind::kPgSession, initializes the metadata
  // necessary for deadlock detection etc. This info is plugged in into write rpcs and the target
  // tablet's wait-queue relies on this information to resume deadlocked session advisory lock reqs.
  void InitPgSessionRequestVersion();

  // For transactions of kind PgClientSessionKind::kPgSession, we record the background txn,
  // if any. This info is propagated to the status tablet, which then creates an internal wait-for
  // probe from the session level waiter -> active transaction if any which is necessary for
  // detection of deadlocks spanning advisory locks and row-level locks (same would apply for
  // detection of deadlocks spanning object locks, advisory locks and row locks in future).
  void SetBackgroundTransaction(const YBTransactionPtr& background_transaction);

  const ash::WaitStateInfoPtr wait_state();

  // Records the Async Write OpId. Returns true if the query was recorded, false if it already
  // existed.
  bool RecordAsyncWrite(const TabletId& tablet_id, const OpId& op_id);
  void RecordAsyncWriteCompletion(
      const TabletId& tablet_id, const OpId& op_id, const Status& status);
  std::optional<int64_t> GetPendingAsyncWriteTerm(const TabletId& tablet_id) const;
  void WaitForAsyncWrites(const TabletId& tablet_id, StdStatusCallback&& callback);
  void SetOriginId(uint32_t origin_id);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

class YBSubTransaction {
 public:
  bool active() const {
    return !sub_txn_.IsDefaultState();
  }

  void SetActiveSubTransaction(SubTransactionId id);

  Status RollbackToSubTransaction(SubTransactionId id);

  bool HasSubTransaction(SubTransactionId id) const;

  const SubTransactionMetadata& get() const;

  std::string ToString() const;

  bool operator==(const YBSubTransaction& other) const;

 private:
  SubTransactionMetadata sub_txn_;

  // Tracks the highest observed subtransaction_id. Used during "ROLLBACK TO s" to abort from s to
  // the highest live subtransaction_id.
  SubTransactionId highest_subtransaction_id_ = kMinSubTransactionId;
};

} // namespace client
} // namespace yb
