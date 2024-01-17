// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
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

#include <boost/intrusive/list.hpp>

#include "yb/common/common_fwd.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/snapshot.h"
#include "yb/common/transaction.h"

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/consensus_types.pb.h"

#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/docdb_types.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/shared_lock_manager.h"

#include "yb/gutil/ref_counted.h"

#include "yb/qlexpr/qlexpr_fwd.h"

#include "yb/rocksdb/rocksdb_fwd.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/table.h"
#include "yb/rocksdb/types.h"

#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/abstract_tablet.h"
#include "yb/tablet/mvcc.h"
#include "yb/tablet/operations/operation.h"
#include "yb/tablet/operation_filter.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_options.h"
#include "yb/tablet/transaction_intent_applier.h"
#include "yb/tablet/tablet_retention_policy.h"

#include "yb/tserver/tserver_fwd.h"

#include "yb/util/status_fwd.h"
#include "yb/util/enums.h"
#include "yb/util/locks.h"
#include "yb/util/memory/arena_list.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/operation_counter.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/threadpool.h"

DECLARE_bool(TEST_docdb_log_write_batches);

namespace yb {

class FsManager;
class MemTracker;
class MetricEntity;

namespace server {
class Clock;
}

namespace tablet {

YB_STRONGLY_TYPED_BOOL(BlockingRocksDbShutdownStart);
YB_STRONGLY_TYPED_BOOL(FlushOnShutdown);
YB_STRONGLY_TYPED_BOOL(IncludeIntents);


inline FlushFlags operator|(FlushFlags lhs, FlushFlags rhs) {
  return static_cast<FlushFlags>(to_underlying(lhs) | to_underlying(rhs));
}

inline FlushFlags operator&(FlushFlags lhs, FlushFlags rhs) {
  return static_cast<FlushFlags>(to_underlying(lhs) & to_underlying(rhs));
}

inline bool HasFlags(FlushFlags lhs, FlushFlags rhs) {
  return (lhs & rhs) != FlushFlags::kNone;
}

class WriteOperation;

using AddTableListener = std::function<Status(const TableInfo&)>;

class TabletScopedIf : public RefCountedThreadSafe<TabletScopedIf> {
 public:
  virtual std::string Key() const = 0;
 protected:
  friend class RefCountedThreadSafe<TabletScopedIf>;
  virtual ~TabletScopedIf() { }
};

YB_STRONGLY_TYPED_BOOL(AllowBootstrappingState);
YB_STRONGLY_TYPED_BOOL(ResetSplit);

struct TabletScopedRWOperationPauses {
  ScopedRWOperationPause blocking_rocksdb_shutdown_start;
  ScopedRWOperationPause not_blocking_rocksdb_shutdown_start;

  std::array<ScopedRWOperationPause*, 2> AsArray() {
    return {&blocking_rocksdb_shutdown_start, &not_blocking_rocksdb_shutdown_start};
  }
};

class Tablet : public AbstractTablet,
               public TransactionIntentApplier,
               public docdb::TableInfoProvider {
 public:
  class CompactionFaultHooks;
  class FlushCompactCommonHooks;
  class FlushFaultHooks;

  // A function that returns the current majority-replicated hybrid time leader lease, or waits
  // until a hybrid time leader lease with at least the given hybrid time is acquired
  // (first argument), or a timeout occurs (second argument). HybridTime::kInvalid is returned
  // in case of a timeout.
  using HybridTimeLeaseProvider = std::function<Result<FixedHybridTimeLease>(
      HybridTime, CoarseTimePoint)>;
  using TransactionIdSet = std::unordered_set<TransactionId, TransactionIdHash>;

  // Create a new tablet.
  //
  // If 'metric_registry' is non-nullptr, then this tablet will create a 'tablet' entity
  // within the provided registry. Otherwise, no metrics are collected.
  explicit Tablet(const TabletInitData& data);

  ~Tablet();

  // Open the tablet.
  // Upon completion, the tablet enters the kBootstrapping state.
  Status Open();

  Status EnableCompactions(ScopedRWOperationPause* blocking_rocksdb_shutdown_start_ops_pause);

  // Performs backfill for the key range beginning from the row immediately after
  // <backfill_from>, until either it reaches the end of the tablet
  //    or the current time is past deadline.
  // *<number_of_rows_processed> will be set to the number of rows backfilled.
  // <backfilled_until> will be set to the first row that was not backfilled, so that the
  //    next API call can resume from where the backfill was left off.
  //    Note that <backfilled_until> only applies to the non-failing indexes.
  Status BackfillIndexesForYsql(
      const std::vector<qlexpr::IndexInfo>& indexes,
      const std::string& backfill_from,
      const CoarseTimePoint deadline,
      const HybridTime read_time,
      const HostPort& pgsql_proxy_bind_address,
      const std::string& database_name,
      const uint64_t postgres_auth_key,
      size_t* number_of_rows_processed,
      std::string* backfilled_until);

  Status VerifyIndexTableConsistencyForCQL(
      const std::vector<qlexpr::IndexInfo>& indexes,
      const std::string& start_key,
      const int num_rows,
      const CoarseTimePoint deadline,
      const HybridTime read_time,
      std::unordered_map<TableId, uint64>* consistency_stats,
      std::string* verified_until);

  Status VerifyMainTableConsistencyForCQL(
      const TableId& main_table_id,
      const std::string& start_key,
      const int num_rows,
      const CoarseTimePoint deadline,
      const HybridTime read_time,
      std::unordered_map<TableId, uint64>* consistency_stats,
      std::string* verified_until);

  Status VerifyTableConsistencyForCQL(
      const std::vector<TableId>& table_ids,
      const std::vector<ColumnId>& columns,
      const std::string& start_key,
      const int num_rows,
      const CoarseTimePoint deadline,
      const HybridTime read_time,
      const bool is_main_table,
      std::unordered_map<TableId, uint64>* consistency_stats,
      std::string* verified_until);

  Status VerifyTableInBatches(
      const qlexpr::QLTableRow& row,
      const std::vector<TableId>& table_ids,
      const HybridTime read_time,
      const CoarseTimePoint deadline,
      const bool is_main_table,
      std::vector<std::pair<const TableId, QLReadRequestPB>>* requests,
      CoarseTimePoint* last_flushed_at,
      std::unordered_set<TableId>* failed_indexes,
      std::unordered_map<TableId, uint64>* consistency_stats);

  Status FlushVerifyBatchIfRequired(
      const HybridTime read_time,
      const CoarseTimePoint deadline,
      std::vector<std::pair<const TableId, QLReadRequestPB>>* requests,
      CoarseTimePoint* last_flushed_at,
      std::unordered_set<TableId>* failed_indexes,
      std::unordered_map<TableId, uint64>* index_consistency_states);
  Status FlushVerifyBatch(
      const HybridTime read_time,
      const CoarseTimePoint deadline,
      std::vector<std::pair<const TableId, QLReadRequestPB>>* requests,
      CoarseTimePoint* last_flushed_at,
      std::unordered_set<TableId>* failed_indexes,
      std::unordered_map<TableId, uint64>* index_consistency_states);

  // Performs backfill for the key range beginning from the row <backfill_from>,
  // until either it reaches the end of the tablet
  //    or the current time is past deadline.
  // *<number_of_rows_processed> will be set to the number of rows backfilled.
  // <backfilled_until> will be set to the first row that was not backfilled, so that the
  //    next API call can resume from where the backfill was left off.
  //    Note that <backfilled_until> only applies to the non-failing indexes.
  // <failed_indexes> will be updated with the collection of index-ids for which any errors
  //    were encountered.
  Status BackfillIndexes(
      const std::vector<qlexpr::IndexInfo>& indexes,
      const std::string& backfill_from,
      const CoarseTimePoint deadline,
      const HybridTime read_time,
      size_t* number_of_rows_processed,
      std::string* backfilled_until,
      std::unordered_set<TableId>* failed_indexes);

  Status UpdateIndexInBatches(
      const qlexpr::QLTableRow& row,
      const std::vector<qlexpr::IndexInfo>& indexes,
      const HybridTime write_time,
      const CoarseTimePoint deadline,
      docdb::IndexRequests* index_requests,
      std::unordered_set<TableId>* failed_indexes);

  Result<std::shared_ptr<client::YBSession>> GetSessionForVerifyOrBackfill(
      const CoarseTimePoint deadline);

  Status FlushWriteIndexBatchIfRequired(
      const HybridTime write_time,
      const CoarseTimePoint deadline,
      docdb::IndexRequests* index_requests,
      std::unordered_set<TableId>* failed_indexes);
  Status FlushWriteIndexBatch(
      const HybridTime write_time,
      const CoarseTimePoint deadline,
      docdb::IndexRequests* index_requests,
      std::unordered_set<TableId>* failed_indexes);

  template <typename SomeYBqlOp>
  Status FlushWithRetries(
      std::shared_ptr<client::YBSession> session,
      const std::vector<std::shared_ptr<SomeYBqlOp>>& index_ops,
      int num_retries,
      std::unordered_set<TableId>* failed_indexes);

  // Mark that the tablet has finished bootstrapping.
  // This transitions from kBootstrapping to kOpen state.
  void MarkFinishedBootstrapping();

  // This can be called to proactively prevent new operations from being handled, even before
  // Shutdown() is called.
  // Returns true if it was the first call to StartShutdown.
  bool StartShutdown();
  bool IsShutdownRequested() const {
    return shutdown_requested_.load(std::memory_order::acquire);
  }

  // Complete the shutdown of this tablet. This includes shutdown of internal structures such as:
  // - transaction coordinator
  // - transaction participant
  // - RocksDB instances
  // - etc.
  // By default, RocksDB shutdown flushes the memtable. This behavior is overriden depending on the
  // provided value of disable_flush_on_shutdown.
  // If abort_ops is specified, aborts pending RocksDB operations that are abortable.
  void CompleteShutdown(DisableFlushOnShutdown disable_flush_on_shutdown, AbortOps abort_ops);

  Status ImportData(const std::string& source_dir);

  Result<docdb::ApplyTransactionState> ApplyIntents(const TransactionApplyData& data) override;

  Status RemoveIntents(
      const RemoveIntentsData& data, RemoveReason reason, const TransactionId& id) override;

  Status RemoveIntents(
      const RemoveIntentsData& data, RemoveReason reason,
      const TransactionIdSet& transactions) override;

  Status GetIntents(
      const TransactionId& id,
      std::vector<docdb::IntentKeyValueForCDC>* keyValueIntents,
      docdb::ApplyTransactionState* stream_state);

  // Apply all of the row operations associated with this transaction.
  Status ApplyRowOperations(
      WriteOperation* operation,
      AlreadyAppliedToRegularDB already_applied_to_regular_db = AlreadyAppliedToRegularDB::kFalse);

  Status ApplyOperation(
      const Operation& operation, int64_t batch_idx,
      const docdb::LWKeyValueWriteBatchPB& write_batch,
      AlreadyAppliedToRegularDB already_applied_to_regular_db = AlreadyAppliedToRegularDB::kFalse);

  // Apply a set of RocksDB row operations.
  // If rocksdb_write_batch is specified it could contain preencoded RocksDB operations.
  Status ApplyKeyValueRowOperations(
      int64_t batch_idx,  // index of this batch in its transaction
      const docdb::LWKeyValueWriteBatchPB& put_batch, docdb::ConsensusFrontiers* frontiers,
      HybridTime write_hybrid_time, HybridTime local_hybrid_time,
      AlreadyAppliedToRegularDB already_applied_to_regular_db = AlreadyAppliedToRegularDB::kFalse);

  void WriteToRocksDB(
      const rocksdb::UserFrontiers* frontiers,
      rocksdb::WriteBatch* write_batch,
      docdb::StorageDbType storage_db_type);

  //------------------------------------------------------------------------------------------------
  // Redis Request Processing.
  // Takes a Redis WriteRequestPB as input with its redis_write_batch.
  // Constructs a WriteRequestPB containing a serialized WriteBatch that will be
  // replicated by Raft. (Makes a copy, it is caller's responsibility to deallocate
  // write_request afterwards if it is no longer needed).
  // The operation acquires the necessary locks required to correctly serialize concurrent write
  // operations to same/conflicting part of the key/sub-key space. The locks acquired are returned
  // via the 'keys_locked' vector, so that they may be unlocked later when the operation has been
  // committed.
  void KeyValueBatchFromRedisWriteBatch(std::unique_ptr<WriteQuery> query);

  Status HandleRedisReadRequest(
      const docdb::ReadOperationData& read_operation_data,
      const RedisReadRequestPB& redis_read_request,
      RedisResponsePB* response) override;

  //------------------------------------------------------------------------------------------------
  // CQL Request Processing.
  Status HandleQLReadRequest(
      const docdb::ReadOperationData& read_operation_data,
      const QLReadRequestPB& ql_read_request,
      const TransactionMetadataPB& transaction_metadata,
      QLReadRequestResult* result,
      WriteBuffer* rows_data) override;

  Status CreatePagingStateForRead(
      const QLReadRequestPB& ql_read_request, const size_t row_count,
      QLResponsePB* response) const override;

  // The QL equivalent of KeyValueBatchFromRedisWriteBatch, works similarly.
  void KeyValueBatchFromQLWriteBatch(std::unique_ptr<WriteQuery> query);

  //------------------------------------------------------------------------------------------------
  // Postgres Request Processing.
  Status HandlePgsqlReadRequest(
      const docdb::ReadOperationData& read_operation_data,
      bool is_explicit_request_read_time,
      const PgsqlReadRequestPB& pgsql_read_request,
      const TransactionMetadataPB& transaction_metadata,
      const SubTransactionMetadataPB& subtransaction_metadata,
      PgsqlReadRequestResult* result) override;

  Status DoHandlePgsqlReadRequest(
      ScopedRWOperation* scoped_read_operation,
      docdb::DocDBStatistics* statistics,
      TabletMetrics* metrics,
      const docdb::ReadOperationData& read_operation_data,
      bool is_explicit_request_read_time,
      const PgsqlReadRequestPB& pgsql_read_request,
      const TransactionMetadataPB& transaction_metadata,
      const SubTransactionMetadataPB& subtransaction_metadata,
      PgsqlReadRequestResult* result);

  Status CreatePagingStateForRead(
      const PgsqlReadRequestPB& pgsql_read_request, const size_t row_count,
      PgsqlResponsePB* response) const override;

  Status PreparePgsqlWriteOperations(WriteQuery* query);
  void KeyValueBatchFromPgsqlWriteBatch(std::unique_ptr<WriteQuery> query);

  // Creates and returns a new RequestScope, which prevents intents from
  // being removed while the object is alive. It is necessary to hold
  // a RequestScope when scanning with IntentAwareIterator to prevent missing entries
  // due to intent removal.
  Result<RequestScope> CreateRequestScope();

  // Create a new row iterator which yields the rows as of the current MVCC
  // state of this tablet.
  // The returned iterator is not initialized and should be initialized by the caller before usage.
  Result<std::unique_ptr<docdb::DocRowwiseIterator>> NewUninitializedDocRowIterator(
      const dockv::ReaderProjection& projection,
      const ReadHybridTime& read_hybrid_time = {},
      const TableId& table_id = "",
      CoarseTimePoint deadline = CoarseTimePoint::max(),
      AllowBootstrappingState allow_bootstrapping_state = AllowBootstrappingState::kFalse) const;

  // The following functions create new row iterator that is already initialized.
  Result<std::unique_ptr<docdb::YQLRowwiseIteratorIf>> NewRowIterator(
      const dockv::ReaderProjection& projection,
      const ReadHybridTime& read_hybrid_time = {},
      const TableId& table_id = "",
      CoarseTimePoint deadline = CoarseTimePoint::max(),
      docdb::SkipSeek skip_seek = docdb::SkipSeek::kFalse) const;

  Result<std::unique_ptr<docdb::YQLRowwiseIteratorIf>> NewRowIterator(
      const TableId& table_id) const;

  Result<std::unique_ptr<docdb::YQLRowwiseIteratorIf>> CreateCDCSnapshotIterator(
      const dockv::ReaderProjection& projection,
      const ReadHybridTime& time,
      const std::string& next_key,
      const TableId& table_id = "");
  //------------------------------------------------------------------------------------------------
  // Makes RocksDB Flush.
  Status Flush(FlushMode mode,
               FlushFlags flags = FlushFlags::kAllDbs,
               int64_t ignore_if_flushed_after_tick = rocksdb::FlushOptions::kNeverIgnore);

  Status WaitForFlush();

  Status FlushSuperblock(OnlyIfDirty only_if_dirty);

  // Prepares the transaction context for the alter schema operation.
  // An error will be returned if the specified schema is invalid (e.g.
  // key mismatch, or missing IDs)
  Status CreatePreparedChangeMetadata(
      ChangeMetadataOperation* operation,
      const Schema* schema,
      IsLeaderSide is_leader_side);

  // Apply the Schema of the specified operation.
  Status AlterSchema(ChangeMetadataOperation* operation);

  // Used to update the tablets on the index table that the index has been backfilled.
  // This means that full compactions can now garbage collect delete markers.
  Status MarkBackfillDone(const OpId& op_id, const TableId& table_id = "");

  // Change wal_retention_secs in the metadata.
  Status AlterWalRetentionSecs(ChangeMetadataOperation* operation);

  // Apply replicated add table operation.
  Status AddTable(const TableInfoPB& table_info, const OpId& op_id);

  Status AddMultipleTables(
      const google::protobuf::RepeatedPtrField<TableInfoPB>& table_infos, const OpId& op_id);

  // Apply replicated remove table operation.
  Status RemoveTable(const std::string& table_id, const OpId& op_id);

  // Truncate this tablet by resetting the content of RocksDB.
  Status Truncate(TruncateOperation* operation);

  // Verbosely dump this entire tablet to the logs. This is only
  // really useful when debugging unit tests failures where the tablet
  // has a very small number of rows.
  Status DebugDump(std::vector<std::string>* lines = nullptr);

  const yb::SchemaPtr schema() const;

  // Returns a reference to the key projection of the tablet schema.
  // The schema keys are immutable.
  const Schema& key_schema() const { return *key_schema_; }

  // Return the MVCC manager for this tablet.
  MvccManager* mvcc_manager() { return &mvcc_; }

  docdb::SharedLockManager* shared_lock_manager() { return &shared_lock_manager_; }

  std::atomic<int64_t>* monotonic_counter() { return &monotonic_counter_; }

  // Set the conter to at least 'value'.
  void UpdateMonotonicCounter(int64_t value);

  const RaftGroupMetadata *metadata() const { return metadata_.get(); }
  RaftGroupMetadata *metadata() { return metadata_.get(); }

  rocksdb::Env& rocksdb_env() const;

  const std::string& tablet_id() const override;

  bool system() const override {
    return false;
  }

  // Return the metrics for this tablet.
  // May be nullptr in unit tests, etc.
  TabletMetrics* metrics() { return metrics_.get(); }

  // Return handle to the metric entity of this tablet/table.
  const scoped_refptr<MetricEntity>& GetTableMetricsEntity() const {
    return table_metrics_entity_;
  }
  const scoped_refptr<MetricEntity>& GetTabletMetricsEntity() const {
    return tablet_metrics_entity_;
  }

  // Returns a reference to this tablet's memory tracker.
  const std::shared_ptr<MemTracker>& mem_tracker() const { return mem_tracker_; }

  TableType table_type() const override { return table_type_; }

  // Returns true if a RocksDB-backed tablet has any SSTables.
  Result<bool> HasSSTables() const;

  // Returns the maximum persistent op id from all SSTables in RocksDB.
  // First for regular records and second for intents.
  // When invalid_if_no_new_data is true then function would return invalid op id when no new
  // data is present in corresponding db.
  Result<DocDbOpIds> MaxPersistentOpId(bool invalid_if_no_new_data = false) const;

  // Returns the maximum persistent hybrid_time across all SSTables in RocksDB.
  Result<HybridTime> MaxPersistentHybridTime() const;

  // Returns oldest mutable memtable write hybrid time in RocksDB or HybridTime::kMax if memtable
  // is empty.
  Result<HybridTime> OldestMutableMemtableWriteHybridTime() const;

  // For non-kudu table type fills key-value batch in transaction state request and updates
  // request in state. Due to acquiring locks it can block the thread.
  void AcquireLocksAndPerformDocOperations(std::unique_ptr<WriteQuery> query);

  // Given a propopsed "history cutoff" timestamp, returns either that value, if possible, or a
  // smaller value corresponding to the oldest active reader, whichever is smaller. This ensures
  // that data needed by active read operations is not compacted away.
  //
  // Also updates the "earliest allowed read time" of the tablet to be equal to the returned value,
  // (if it is still lower than the value about to be returned), so that new readers with timestamps
  // earlier than that will be rejected.
  HybridTime UpdateHistoryCutoff(HybridTime proposed_cutoff);

  const scoped_refptr<server::Clock> &clock() const {
    return clock_;
  }

  docdb::DocReadContextPtr GetDocReadContext() const override;
  Result<docdb::DocReadContextPtr> GetDocReadContext(const std::string& table_id) const override;

  Schema GetKeySchema(const std::string& table_id = "") const;


  // Provide a way for write operations to wait when tablet schema is
  // being changed.
  ScopedRWOperationPause PauseWritePermits(CoarseTimePoint deadline);
  ScopedRWOperation GetPermitToWrite(CoarseTimePoint deadline);

  // Used from tests
  const std::shared_ptr<rocksdb::Statistics>& regulardb_statistics() const {
    return regulardb_statistics_;
  }

  const std::shared_ptr<rocksdb::Statistics>& intentsdb_statistics() const {
    return intentsdb_statistics_;
  }

  TransactionCoordinator* transaction_coordinator() {
    return transaction_coordinator_.get();
  }

  TransactionParticipant* transaction_participant() const {
    return transaction_participant_.get();
  }

  // Returns true if the tablet was created after a split but it has not yet had data from it's
  // parent which are now outside of its key range removed.
  Result<bool> StillHasOrphanedPostSplitData();

  // Wrapper for StillHasOrphanedPostSplitData. Conservatively returns true if
  // StillHasOrphanedPostSplitData failed, otherwise returns the result value.
  bool MayHaveOrphanedPostSplitData();

  // If true, we should report, in our heartbeat to the master, that loadbalancer moves should be
  // disabled. We do so, for example, when StillHasOrphanedPostSplitData() returns true.
  bool ShouldDisableLbMove();

  Status ForceManualRocksDBCompact(docdb::SkipFlush skip_flush = docdb::SkipFlush::kFalse);

  Status ForceRocksDBCompact(
      rocksdb::CompactionReason compaction_reason,
      docdb::SkipFlush skip_flush = docdb::SkipFlush::kFalse);

  rocksdb::DB* regular_db() const {
    return regular_db_.get();
  }

  rocksdb::DB* intents_db() const {
    return intents_db_.get();
  }

  // The only way to make any conclusion that a tablet is a product of a split is to check its key
  // bounds are initialized as it is supposed that these key bounds are setup during tablet split.
  const docdb::KeyBounds& key_bounds() const {
    return key_bounds_;
  }

  docdb::DocDB doc_db(TabletMetrics* metrics = nullptr) const {
    return {
        regular_db_.get(),
        intents_db_.get(),
        &key_bounds_,
        retention_policy_.get(),
        metrics ? metrics : metrics_.get() };
  }

  // Returns approximate middle key for tablet split:
  // - for hash-based partitions: encoded hash code in order to split by hash code.
  // - for range-based partitions: encoded doc key in order to split by row.
  // If `partition_split_key` is specified it will be updated with partition middle key for
  // hash-based partitions only (to prevent additional memory copying), as partition middle key for
  // range-based partitions always matches the returned middle key.
  Result<std::string> GetEncodedMiddleSplitKey(std::string *partition_split_key = nullptr) const;

  std::string TEST_DocDBDumpStr(IncludeIntents include_intents = IncludeIntents::kFalse);

  void TEST_DocDBDumpToContainer(
      IncludeIntents include_intents, std::unordered_set<std::string>* out);

  // Dumps DocDB contents to log, every record as a separate log message, with the given prefix.
  void TEST_DocDBDumpToLog(IncludeIntents include_intents);

  Result<size_t> TEST_CountRegularDBRecords();

  Status CreateReadIntents(
      IsolationLevel level,
      const TransactionMetadataPB& transaction_metadata,
      const SubTransactionMetadataPB& subtransaction_metadata,
      const google::protobuf::RepeatedPtrField<QLReadRequestPB>& ql_batch,
      const google::protobuf::RepeatedPtrField<PgsqlReadRequestPB>& pgsql_batch,
      docdb::LWKeyValueWriteBatchPB* out);

  uint64_t GetCurrentVersionSstFilesSize() const;
  uint64_t GetCurrentVersionSstFilesUncompressedSize() const;
  std::pair<uint64_t, uint64_t> GetCurrentVersionSstFilesAllSizes() const;
  uint64_t GetCurrentVersionNumSSTFiles() const;

  void ListenNumSSTFilesChanged(std::function<void()> listener);

  // Returns the number of memtables in intents and regular db-s.
  std::pair<int, int> GetNumMemtables() const;

  void SetHybridTimeLeaseProvider(HybridTimeLeaseProvider provider) {
    ht_lease_provider_ = std::move(provider);
  }

  void SetMemTableFlushFilterFactory(std::function<rocksdb::MemTableFilter()> factory) {
    std::lock_guard lock(flush_filter_mutex_);
    mem_table_flush_filter_factory_ = std::move(factory);
  }

  // When a compaction starts with a particular "history cutoff" timestamp, it calls this function
  // to disallow reads at a time lower than that history cutoff timestamp, to avoid reading
  // invalid/incomplete data.
  //
  // Returns true if the new history cutoff timestamp was successfully registered, or false if
  // it can't be used because there are pending reads at lower timestamps.
  HybridTime Get(HybridTime lower_bound);

  bool ShouldApplyWrite();

  Status TEST_SwitchMemtable();

  // Initialize RocksDB's max persistent op id and hybrid time to that of the operation state.
  // Necessary for cases like truncate or restore snapshot when RocksDB is reset.
  Status ModifyFlushedFrontier(
      const docdb::ConsensusFrontier& value,
      rocksdb::FrontierModificationMode mode,
      FlushFlags flags = FlushFlags::kAllDbs);

  // Get the isolation level of the given transaction from the metadata stored in the provisional
  // records RocksDB.
  Result<IsolationLevel> GetIsolationLevel(const LWTransactionMetadataPB& transaction) override;
  Result<IsolationLevel> GetIsolationLevel(const TransactionMetadataPB& transaction) override;

  // Creates an on-disk sub tablet of this tablet with specified ID, partition and key bounds.
  // Flushes this tablet data onto disk before creating sub tablet.
  // Also updates flushed frontier for regular and intents DBs to match split_op_id and
  // split_op_hybrid_time.
  // In case of error sub-tablet could be partially persisted on disk.
  // NB! As of now the method is supposed to be used only for creation child tablets during
  // splitting operation. For any other type of usage, the method must be verified and possibly
  // updated to correctly handle split_op_id, split_op_hybrid_time, parent_data_compacted
  // and post_split_compaction_file_number_upper_bound.
  Result<RaftGroupMetadataPtr> CreateSubtablet(
      const TabletId& tablet_id, const dockv::Partition& partition,
      const docdb::KeyBounds& key_bounds, const OpId& split_op_id,
      const HybridTime& split_op_hybrid_time);

  // Scans the intent db. Potentially takes a long time. Used for testing/debugging.
  Result<int64_t> CountIntents();

  // Get all the entries of intent db.
  // Potentially takes a long time. Used for testing/debugging.
  Status ReadIntents(std::vector<std::string>* resp);

  // Flushed intents db if necessary.
  void FlushIntentsDbIfNecessary(const yb::OpId& lastest_log_entry_op_id);

  bool is_sys_catalog() const { return is_sys_catalog_; }
  bool IsTransactionalRequest(bool is_ysql_request) const override;

  void SetCleanupPool(ThreadPool* thread_pool);

  TabletSnapshots& snapshots() {
    return *snapshots_;
  }

  SnapshotCoordinator* snapshot_coordinator() {
    return snapshot_coordinator_;
  }

//------------------------------------------------------------------------------------------------
// CDC Related

  docdb::YQLRowwiseIteratorIf* cdc_iterator() {
    return cdc_iterator_;
  }

  Status SetAllCDCRetentionBarriersUnlocked(
      int64 cdc_wal_index, OpId cdc_sdk_intents_op_id, MonoDelta cdc_sdk_op_id_expiration,
      HybridTime cdc_sdk_history_cutoff, bool require_history_cutoff,
      bool initial_retention_barrier);

  Status SetAllInitialCDCRetentionBarriers(
      log::Log* log, int64 cdc_wal_index, OpId cdc_sdk_intents_op_id,
      HybridTime cdc_sdk_history_cutoff, bool require_history_cutoff);

  Status SetAllInitialCDCSDKRetentionBarriers(
      log::Log* log, OpId cdc_sdk_op_id, HybridTime cdc_sdk_history_cutoff,
      bool require_history_cutoff);

  Result<bool> MoveForwardAllCDCRetentionBarriers(
      log::Log* log, int64 cdc_wal_index, OpId cdc_sdk_intents_op_id,
      MonoDelta cdc_sdk_op_id_expiration, HybridTime cdc_sdk_history_cutoff,
      bool require_history_cutoff);

//------------------------------------------------------------------------------------------------

  // Allows us to add tablet-specific information that will get deref'd when the tablet does.
  void AddAdditionalMetadata(const std::string& key, std::shared_ptr<void> additional_metadata) {
    std::lock_guard lock(control_path_mutex_);
    additional_metadata_.emplace(key, std::move(additional_metadata));
  }

  std::shared_ptr<void> GetAdditionalMetadata(const std::string& key) {
    std::lock_guard lock(control_path_mutex_);
    auto val = additional_metadata_.find(key);
    return (val != additional_metadata_.end()) ? val->second : nullptr;
  }

  size_t RemoveAdditionalMetadata(const std::string& key) {
    std::lock_guard lock(control_path_mutex_);
    return additional_metadata_.erase(key);
  }

  void InitRocksDBOptions(
      rocksdb::Options* options, const std::string& log_prefix,
      rocksdb::BlockBasedTableOptions table_options = rocksdb::BlockBasedTableOptions());

  TabletRetentionPolicy* RetentionPolicy() override {
    return retention_policy_.get();
  }

  // Triggers a compaction on this tablet if it is the result of a tablet split but has not yet been
  // compacted. It is an error to call this method if a post-split compaction has been triggered
  // previously by this tablet.
  void TriggerPostSplitCompactionIfNeeded();

  // Triggers a manual compaction on this tablet (e.g. post tablet split, scheduled).
  // It is an error to call this function if it was called previously
  // and that compaction has not yet finished.
  Status TriggerManualCompactionIfNeeded(rocksdb::CompactionReason reason);

  // Triggers an admin full compaction on this tablet.
  Status TriggerAdminFullCompactionIfNeeded();
  // Triggers an admin full compaction on this tablet with a callback to execute once the compaction
  // completes.
  Status TriggerAdminFullCompactionWithCallbackIfNeeded(
      std::function<void()> on_compaction_completion);

  bool HasActiveFullCompaction();

  bool HasActiveFullCompactionUnlocked() const REQUIRES(full_compaction_token_mutex_) {
    bool has_active_scheduled = full_compaction_task_pool_token_ != nullptr
                                    ? !full_compaction_task_pool_token_->WaitFor(MonoDelta::kZero)
                                    : false;
    bool has_active_admin = admin_full_compaction_task_pool_token_ != nullptr
                                ? !admin_full_compaction_task_pool_token_->WaitFor(MonoDelta::kZero)
                                : false;
    return has_active_scheduled || has_active_admin;
  }

  bool HasActiveTTLFileExpiration();

  // Indicates whether this tablet can currently be compacted by any (non-admin) triggered
  // full compaction.
  bool IsEligibleForFullCompaction();

  // Verifies the data on this tablet for consistency. Returns status OK if checks pass.
  Status VerifyDataIntegrity();

  Status CheckOperationAllowed(const OpId& op_id, consensus::OperationType op_type)
      EXCLUDES(operation_filters_mutex_);

  void RegisterOperationFilter(OperationFilter* filter) EXCLUDES(operation_filters_mutex_);
  void UnregisterOperationFilter(OperationFilter* filter) EXCLUDES(operation_filters_mutex_);

  void SplitDone();
  Status RestoreStarted(const TxnSnapshotRestorationId& restoration_id);
  Status RestoreFinished(
      const TxnSnapshotRestorationId& restoration_id, HybridTime restoration_hybrid_time);
  Status CheckRestorations(const RestorationCompleteTimeMap& restoration_complete_time);

  bool txns_enabled() const {
    return txns_enabled_;
  }

  client::YBClient& client() {
    return *client_future_.get();
  }

  client::TransactionManager& transaction_manager() {
    return transaction_manager_provider_();
  }

  // Returns the cached metadata cache.
  client::YBMetaDataCache* YBMetaDataCache();

  // Resets the metadata cache if present.
  void ResetYBMetaDataCache();

  // Should only be used in code that could be executed from Raft operations apply or other
  // callsites that doesn't handle RocksDB shutdown start (without destroy, see
  // Tablet::StartShutdownRocksDBs, rocksdb::DB::StartShutdown) during operation, so tablet shutdown
  // will wait till their completion and don't fail Raft operation apply.
  ScopedRWOperation CreateScopedRWOperationBlockingRocksDbShutdownStart(
      const CoarseTimePoint deadline = CoarseTimePoint()) const;

  // Should NOT be used in code that might be executed from Raft operations apply or callsites that
  // doesn't handle RocksDB start shutdown during operation, so we can start shutdown of RocksDB
  // without waiting for these operations and they might be aborted during tablet shutdown.
  ScopedRWOperation CreateScopedRWOperationNotBlockingRocksDbShutdownStart(
      const CoarseTimePoint deadline = CoarseTimePoint()) const;

  Result<TransactionOperationContext> CreateTransactionOperationContext(
      const TransactionMetadataPB& transaction_metadata,
      bool is_ysql_catalog_table,
      const SubTransactionMetadataPB* subtransaction_metadata = nullptr) const;

  bool XClusterReplicationCaughtUpToTime(HybridTime txn_commit_ht);

  // Store the new AutoFlags config to disk and then applies it. Error Status is returned only for
  // critical failures.
  Status ProcessAutoFlagsConfigOperation(const AutoFlagsConfigPB& config);

  std::string LogPrefix() const;

  // Populate tablet_locks_info with lock information pertaining to locks persisted in intents_db of
  // this tablet. If transactions is not empty, restrict returned information to locks which are
  // held or requested by the given set of transactions and restrict locks to those which are not
  // present in the associated aborted subtxns.
  Status GetLockStatus(
      const std::map<TransactionId, SubtxnSet>& transactions,
      TabletLockInfoPB* tablet_lock_info,
      uint64_t max_single_shard_waiter_start_time_us) const;

  // The returned SchemaPackingProvider lives only as long as this.
  docdb::SchemaPackingProvider& GetSchemaPackingProvider();

  // 1. Pauses new read/write operations that block RocksDB shutdown and wait for all of those that
  // are pending to complete.
  // 2. Starts RocksDB shutdown (that might abort some of the rest of read/write operations).
  // 3. If abort_ops is specified, aborts pending RocksDB operations that are abortable.
  // 4. Pauses new read/write operations and wait for all of those that are pending to complete.
  // Returns TabletScopedRWOperationPauses that are preventing new read/write operations from being
  // started.
  TabletScopedRWOperationPauses StartShutdownRocksDBs(
      DisableFlushOnShutdown disable_flush_on_shutdown, AbortOps abort_ops,
      Stop stop = Stop::kFalse);

  // Returns DB paths for destructed in-memory RocksDBs objects, so caller can delete on-disk
  // directories if needed.
  std::vector<std::string> CompleteShutdownRocksDBs(
      const TabletScopedRWOperationPauses& ops_pauses);

  Status OpenKeyValueTablet();

  // Returns a pointer to the TableInfo corresponding to the colocated table. When called with
  // 'kColocationIdNotSet', returns the TableInfo of the parent/primary table.
  Result<TableInfoPtr> GetTableInfo(ColocationId colocation_id) const override;

  // Lock used to serialize the creation of RocksDB checkpoints.
  mutable std::mutex create_checkpoint_lock_;

  // Serializes access to setting/revising/releasing CDCSDK retention barriers
  mutable simple_spinlock cdcsdk_retention_barrier_lock_;
  MonoTime cdcsdk_block_barrier_revision_start_time = MonoTime::Now();

 private:
  friend class Iterator;
  friend class TabletPeerTest;
  friend class ScopedReadOperation;
  friend class TabletComponent;

  class RegularRocksDbListener;

  FRIEND_TEST(TestTablet, TestGetLogRetentionSizeForIndex);

  virtual Status CreateTabletDirectories(const std::string& db_dir, FsManager* fs);

  std::vector<ColumnId> GetColumnSchemasForIndex(
      const std::vector<qlexpr::IndexInfo>& indexes);

  void DocDBDebugDump(std::vector<std::string> *lines);

  Status WriteTransactionalBatch(
      int64_t batch_idx, // index of this batch in its transaction
      const docdb::LWKeyValueWriteBatchPB& put_batch,
      HybridTime hybrid_time,
      const rocksdb::UserFrontiers* frontiers);

  Result<TransactionOperationContext> CreateTransactionOperationContext(
      const boost::optional<TransactionId>& transaction_id,
      bool is_ysql_catalog_table,
      const SubTransactionMetadataPB* subtransaction_metadata = nullptr) const;

  // Pause new read/write operations that are blocking/not blocking start of RocksDB shutdown and
  // wait for all such pending read/write operations to finish.
  // If stop is false, ScopedRWOperation constructor will wait while ScopedRWOperationPause is
  // alive.
  // If stop is true, ScopedRWOperation constructor will create an instance with an error (see
  // ScopedRWOperation::ok()) while ScopedRWOperationPause is alive.
  ScopedRWOperationPause PauseReadWriteOperations(
      BlockingRocksDbShutdownStart blocking_rocksdb_shutdown_start, Stop stop = Stop::kFalse);

  // Attempt to delete on-disk RocksDBs from all provided db_paths, even if errors are encountered.
  // Return the first error encountered.
  Status DeleteRocksDBs(const std::vector<std::string>& db_paths);

  Status DoEnableCompactions();

  std::string LogPrefix(docdb::StorageDbType db_type) const;

  Result<bool> IsQueryOnlyForTablet(const PgsqlReadRequestPB& pgsql_read_request,
      size_t row_count) const;

  Result<bool> HasScanReachedMaxPartitionKey(
      const PgsqlReadRequestPB& pgsql_read_request,
      const std::string& partition_key,
      size_t row_count) const;

  void TriggerManualCompactionSync(rocksdb::CompactionReason reason);

  Status ForceRocksDBCompact(
      const rocksdb::CompactRangeOptions& regular_options,
      const rocksdb::CompactRangeOptions& intents_options);

  // Opens read-only rocksdb at the specified directory and checks for any file corruption.
  Status OpenDbAndCheckIntegrity(const std::string& db_dir);

  // Add or remove restoring operation filter if necessary.
  // If reset_split is true, also reset split state.
  void SyncRestoringOperationFilter(ResetSplit reset_split) EXCLUDES(operation_filters_mutex_);
  void UnregisterOperationFilterUnlocked(OperationFilter* filter)
    REQUIRES(operation_filters_mutex_);

  Status AddTableInMemory(const TableInfoPB& table_info, const OpId& op_id);

  // Returns true if the tablet was created after a split but it has not yet had data from it's
  // parent which are now outside of its key range removed.
  bool StillHasOrphanedPostSplitDataAbortable();

  template <class PB>
  Result<IsolationLevel> DoGetIsolationLevel(const PB& transaction);

  Status TriggerAdminFullCompactionIfNeededHelper(
      std::function<void()> on_compaction_completion = []() {});

  std::unique_ptr<const Schema> key_schema_;

  RaftGroupMetadataPtr metadata_;
  TableType table_type_;

  // Lock protecting access to the 'components_' member (i.e the rowsets in the tablet)
  //
  // Shared mode:
  // - Writers take this in shared mode at the same time as they obtain an MVCC hybrid_time
  //   and capture a reference to components_. This ensures that we can use the MVCC hybrid_time
  //   to determine which writers are writing to which components during compaction.
  // - Readers take this in shared mode while capturing their iterators. This ensures that
  //   they see a consistent view when racing against flush/compact.
  //
  // Exclusive mode:
  // - Flushes/compactions take this lock in order to lock out concurrent updates.
  //
  // NOTE: callers should avoid taking this lock for a long time, even in shared mode.
  // This is because the lock has some concept of fairness -- if, while a long reader
  // is active, a writer comes along, then all future short readers will be blocked.
  // TODO: now that this is single-threaded again, we should change it to rw_spinlock
  mutable rw_spinlock component_lock_;

  scoped_refptr<log::LogAnchorRegistry> log_anchor_registry_;
  std::shared_ptr<MemTracker> mem_tracker_;
  std::shared_ptr<MemTracker> block_based_table_mem_tracker_;
  std::shared_ptr<MemTracker> regulardb_mem_tracker_;
  std::shared_ptr<MemTracker> intentdb_mem_tracker_;

  MetricEntityPtr tablet_metrics_entity_;
  MetricEntityPtr table_metrics_entity_;
  MemTrackerPtr metric_mem_tracker_;
  std::unique_ptr<TabletMetrics> metrics_;
  std::shared_ptr<void> metric_detacher_;

  // A pointer to the server's clock.
  scoped_refptr<server::Clock> clock_;

  MvccManager mvcc_;

  enum State {
    kInitialized,
    kBootstrapping,
    kOpen,
    kShutdown
  };
  State state_ = kInitialized;

  // Fault hooks. In production code, these will always be nullptr.
  std::shared_ptr<CompactionFaultHooks> compaction_hooks_;
  std::shared_ptr<FlushFaultHooks> flush_hooks_;
  std::shared_ptr<FlushCompactCommonHooks> common_hooks_;

  // Statistics for the RocksDB database.
  std::shared_ptr<rocksdb::Statistics> regulardb_statistics_;
  std::shared_ptr<rocksdb::Statistics> intentsdb_statistics_;

  // RocksDB database instances for key-value tables.
  std::unique_ptr<rocksdb::DB> regular_db_;
  std::unique_ptr<rocksdb::DB> intents_db_;
  std::atomic<bool> rocksdb_shutdown_requested_{false};

  // Optional key bounds (see docdb::KeyBounds) served by this tablet.
  docdb::KeyBounds key_bounds_;

  std::unique_ptr<docdb::YQLStorageIf> ql_storage_;

  // This is for docdb fine-grained locking.
  docdb::SharedLockManager shared_lock_manager_;

  // For the block cache and memory manager shared across tablets
  const TabletOptions tablet_options_;

  // A lightweight way to reject new operations when the tablet is shutting down. This is used to
  // prevent race conditions between destructing the RocksDB in-memory instance and read/write
  // operations.
  std::atomic_bool shutdown_requested_{false};

  // This is a special atomic counter per tablet that increases monotonically.
  // It is like timestamp, but doesn't need locks to read or update.
  // This is raft replicated as well. Each replicate message contains the current number.
  // It is guaranteed to keep increasing for committed entries even across tablet server
  // restarts and leader changes.
  std::atomic<int64_t> monotonic_counter_{0};

  // Number of pending operations that block RocksDB shutdown start. We use this to make sure we
  // don't start RocksDB shutdown before all such pending operations are finished. We don't have a
  // strict definition of an "operation" for the purpose of this counter. We simply wait for this
  // counter to go to zero before starting RocksDB shutdown.
  // Note: as of 2021-06-28 applying of Raft operations could not handle errors that happened due to
  // RocksDB shutdown.
  //
  // This is marked mutable because read path member functions (which are const) are using this.
  mutable RWOperationCounter pending_op_counter_blocking_rocksdb_shutdown_start_;

  // Similar to pending_op_counter_blocking_rocksdb_shutdown_start_ but for operations that don't
  // block RocksDB shutdown start, i.e. operations that could handle start of RocksDB shutdown
  // during their execution, for example manual compactions.
  // We wait for this counter to go to zero after starting RocksDB shutdown and before destructing
  // RocksDB in-memory instance.
  mutable RWOperationCounter pending_op_counter_not_blocking_rocksdb_shutdown_start_;

  // Used to abort pending operations that are not blocking RocksDB shutdown start.
  StatusHolder abort_pending_op_status_holder_;

  // Used by Alter/Schema-change ops to pause new write ops from being submitted.
  RWOperationCounter write_ops_being_submitted_counter_;

  std::unique_ptr<TransactionCoordinator> transaction_coordinator_;

  std::unique_ptr<TransactionParticipant> transaction_participant_;

  std::shared_future<client::YBClient*> client_future_;

  // Expected to live while this object is alive.
  TransactionManagerProvider transaction_manager_provider_;

  // Pointer to shared metadata cache owned by TSTabletManager.
  client::YBMetaDataCache* metadata_cache_;

  std::atomic<int64_t> last_committed_write_index_{0};

  HybridTimeLeaseProvider ht_lease_provider_;

  Result<HybridTime> DoGetSafeTime(
      RequireLease require_lease, HybridTime min_allowed, CoarseTimePoint deadline) const override;

  Result<bool> IntentsDbFlushFilter(const rocksdb::MemTable& memtable, bool write_blocked);

  template <class Ids>
  Status RemoveIntentsImpl(const RemoveIntentsData& data, RemoveReason reason, const Ids& ids);

  // Tries to find intent .SST files that could be deleted and remove them.
  void CleanupIntentFiles();
  void DoCleanupIntentFiles();

  void RegularDbFilesChanged();

  HybridTime ApplierSafeTime(HybridTime min_allowed, CoarseTimePoint deadline) override;

  void MinRunningHybridTimeSatisfied() override {
    CleanupIntentFiles();
  }

  template <class F>
  auto GetRegularDbStat(const F& func, const decltype(func())& default_value) const;

  HybridTime DeleteMarkerRetentionTime(const std::vector<rocksdb::FileMetaData*>& inputs);

  mutable std::mutex flush_filter_mutex_;
  std::function<rocksdb::MemTableFilter()> mem_table_flush_filter_factory_
      GUARDED_BY(flush_filter_mutex_);

  client::LocalTabletFilter local_tablet_filter_;

  // This is typically "P <peer_id>", so we can get a log prefix "T <tablet_id> P <peer_id>: ".
  std::string log_prefix_suffix_;

  IsSysCatalogTablet is_sys_catalog_;
  TransactionsEnabled txns_enabled_;

  std::unique_ptr<ThreadPoolToken> cleanup_intent_files_token_;

  std::unique_ptr<TabletSnapshots> snapshots_;

  SnapshotCoordinator* snapshot_coordinator_ = nullptr;

  docdb::YQLRowwiseIteratorIf* cdc_iterator_ = nullptr;

  AutoFlagsManager* auto_flags_manager_ = nullptr;

  mutable std::mutex control_path_mutex_;
  std::unordered_map<std::string, std::shared_ptr<void>> additional_metadata_
    GUARDED_BY(control_path_mutex_);

  std::mutex num_sst_files_changed_listener_mutex_;
  std::function<void()> num_sst_files_changed_listener_
      GUARDED_BY(num_sst_files_changed_listener_mutex_);

  std::shared_ptr<TabletRetentionPolicy> retention_policy_;

  std::mutex full_compaction_token_mutex_;

  // Thread pool token for triggering full compactions for tablets via full compaction manager.
  // Once set, this token is reused, but only when not active (HasActiveFullCompaction()).
  std::unique_ptr<ThreadPoolToken> full_compaction_task_pool_token_
      GUARDED_BY(full_compaction_token_mutex_);

  // Thread pool token for triggering admin full compactions.
  std::unique_ptr<ThreadPoolToken> admin_full_compaction_task_pool_token_
      GUARDED_BY(full_compaction_token_mutex_);

  // Pointer to shared thread pool in TsTabletManager. Managed by the FullCompactionManager.
  ThreadPool* full_compaction_pool_ = nullptr;

  // Pointer to shared admin triggered thread pool in TsTabletManager.
  ThreadPool* admin_triggered_compaction_pool_ = nullptr;

  // Gauge to monitor post-split compactions that have been started.
  scoped_refptr<yb::AtomicGauge<uint64_t>> ts_post_split_compaction_added_;

  // Function to get min schema version for a table needed for xCluster.
  std::function<uint32_t(const TableId&, const ColocationId&)>
      get_min_xcluster_schema_version_ = nullptr;

  simple_spinlock operation_filters_mutex_;

  boost::intrusive::list<OperationFilter> operation_filters_ GUARDED_BY(operation_filters_mutex_);

  std::unique_ptr<OperationFilter> completed_split_operation_filter_
      GUARDED_BY(operation_filters_mutex_);
  std::unique_ptr<log::LogAnchor> completed_split_log_anchor_ GUARDED_BY(operation_filters_mutex_);

  std::unique_ptr<OperationFilter> restoring_operation_filter_ GUARDED_BY(operation_filters_mutex_);

  DISALLOW_COPY_AND_ASSIGN(Tablet);
};

// A helper class to manage read transactions. Grabs and registers a read point with the tablet
// when created, and deregisters the read point when this object is destructed.
// TODO: should reference the tablet as a shared pointer (make sure there are no reference cycles.)
class ScopedReadOperation {
 public:
  ScopedReadOperation() : tablet_(nullptr) {}
  ScopedReadOperation(ScopedReadOperation&& rhs)
      : tablet_(rhs.tablet_), read_time_(rhs.read_time_) {
    rhs.tablet_ = nullptr;
  }

  void operator=(ScopedReadOperation&& rhs);

  static Result<ScopedReadOperation> Create(
      AbstractTablet* tablet,
      RequireLease require_lease,
      ReadHybridTime read_time);

  ScopedReadOperation(const ScopedReadOperation&) = delete;
  void operator=(const ScopedReadOperation&) = delete;

  ~ScopedReadOperation();

  const ReadHybridTime& read_time() const { return read_time_; }

  Status status() const { return status_; }

  void Reset();

 private:
  explicit ScopedReadOperation(
      AbstractTablet* tablet, const ReadHybridTime& read_time);

  AbstractTablet* tablet_;
  ReadHybridTime read_time_;
  Status status_;
};

bool IsSchemaVersionCompatible(
    uint32_t current_version, uint32_t request_version, bool compatible_with_previous_version);

}  // namespace tablet
}  // namespace yb
