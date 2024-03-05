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

#include "yb/client/client_fwd.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/doc_operation.h"
#include "yb/docdb/lock_batch.h"

#include "yb/rpc/rpc_context.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/tserver/tserver.fwd.h"

#include "yb/util/operation_counter.h"

namespace yb {
namespace tablet {

struct UpdateQLIndexesTask;

class WriteQuery {
 public:
  WriteQuery(int64_t term,
             CoarseTimePoint deadline,
             WriteQueryContext* context,
             TabletPtr tablet,
             rpc::RpcContext* rpc_context,
             tserver::WriteResponsePB* response = nullptr);

  ~WriteQuery();

  WriteOperation& operation() {
    return *operation_;
  }

  LWWritePB& request();

  // Returns the prepared response to the client that will be sent when this
  // transaction is completed, if this transaction was started by a client.
  tserver::WriteResponsePB* response() {
    return response_;
  }

  static void Execute(std::unique_ptr<WriteQuery> query);

  // The QL write operations that return rowblocks that need to be returned as RPC sidecars
  // after the transaction completes.
  std::vector<std::unique_ptr<docdb::QLWriteOperation>>* ql_write_ops() {
    return &ql_write_ops_;
  }

  // Returns PGSQL write operations.
  // TODO(neil) These ops must report number of rows that was updated, deleted, or inserted.
  std::vector<std::unique_ptr<docdb::PgsqlWriteOperation>>* pgsql_write_ops() {
    return &pgsql_write_ops_;
  }

  void AdjustYsqlQueryTransactionality(size_t ysql_batch_size);

  HybridTime restart_read_ht() const {
    return restart_read_ht_;
  }

  CoarseTimePoint deadline() const {
    return deadline_;
  }

  docdb::DocOperations& doc_ops() {
    return doc_ops_;
  }

  static void StartSynchronization(std::unique_ptr<WriteQuery> query, const Status& status) {
    // We release here, because DoStartSynchronization takes ownership on this.
    query.release()->DoStartSynchronization(status);
  }

  void UseSubmitToken(ScopedRWOperation&& token) {
    submit_token_ = std::move(token);
  }

  void set_client_request(std::reference_wrapper<const tserver::WriteRequestPB> req);

  void set_client_request(std::unique_ptr<tserver::WriteRequestPB> req);

  void set_read_time(const ReadHybridTime& read_time) {
    read_time_ = read_time;
  }

  template <class Callback>
  void set_callback(Callback&& callback) {
    callback_ = std::forward<Callback>(callback);
  }

  // Cancel query even before sending underlying operation to the Raft.
  void Cancel(const Status& status);

  const tserver::WriteRequestPB* client_request() {
    return client_request_;
  }

  std::unique_ptr<WriteOperation> PrepareSubmit();

  void SetRequestStartUs(uint64_t request_start_us) { request_start_us_ = request_start_us; }

  uint64_t request_start_us() const { return request_start_us_; }

 private:
  friend struct UpdateQLIndexesTask;
  enum class ExecuteMode;

  // Actually starts the Mvcc transaction and assigns a hybrid_time to this transaction.
  void DoStartSynchronization(const Status& status);

  void Release();

  void Finished(WriteOperation* operation, const Status& status);

  void Complete(const Status& status);

  Status InitExecute(ExecuteMode mode);

  void ExecuteDone(const Status& status);

  Result<bool> PrepareExecute();
  Status DoExecute();

  void NonTransactionalConflictsResolved(HybridTime now, HybridTime result);

  void TransactionalConflictsResolved();

  Status DoTransactionalConflictsResolved();

  void CompleteExecute(HybridTime safe_time);

  Status DoCompleteExecute(HybridTime safe_time);

  Result<bool> SimplePrepareExecute();
  Result<bool> RedisPrepareExecute();
  Result<bool> CqlRePrepareExecuteIfNecessary();
  Result<bool> CqlPrepareExecute();
  Result<bool> PgsqlPrepareExecute();

  void SimpleExecuteDone(const Status& status);
  void RedisExecuteDone(const Status& status);
  void CqlExecuteDone(const Status& status);
  void PgsqlExecuteDone(const Status& status);

  using IndexOps = std::vector<std::pair<
      std::shared_ptr<client::YBqlWriteOp>, docdb::QLWriteOperation*>>;
  void UpdateQLIndexes();
  void UpdateQLIndexesFlushed(
      const client::YBSessionPtr& session, const client::YBTransactionPtr& txn,
      const IndexOps& index_ops, client::FlushStatus* flush_status);

  void CompleteQLWriteBatch(const Status& status);

  template <class Code, class Resp>
  void SchemaVersionMismatch(Code code, int size, Resp* resp);

  Result<bool> ExecuteSchemaVersionCheck();
  Result<bool> CqlCheckSchemaVersion();
  Result<bool> PgsqlCheckSchemaVersion();

  void CqlRespondSchemaVersionMismatch();
  void PgsqlRespondSchemaVersionMismatch();

  void IncrementActiveWriteQueryObjectsBy(int64_t value);

  Result<TabletPtr> tablet_safe() const;

  TabletWeakPtr tablet_;
  std::unique_ptr<WriteOperation> operation_;

  // The QL write operations that return rowblocks that need to be returned as RPC sidecars
  // after the operation completes.
  std::vector<std::unique_ptr<docdb::QLWriteOperation>> ql_write_ops_;

  // The PGSQL write operations that return rowblocks that need to be returned as RPC sidecars
  // after the transaction completes.
  std::vector<std::unique_ptr<docdb::PgsqlWriteOperation>> pgsql_write_ops_;

  // Store the ids that have been locked for DocDB operation. They need to be released on commit
  // or if an error happens.
  docdb::LockBatch docdb_locks_;

  // True if we know that this operation is on a transactional table so make sure we go through the
  // transactional codepath.
  bool force_txn_path_ = false;

  const int64_t term_;
  ScopedRWOperation submit_token_;
  const CoarseTimePoint deadline_;
  WriteQueryContext* const context_;
  rpc::RpcContext* const rpc_context_;

  // Pointers to the rpc context, request and response, lifecycle
  // is managed by the rpc subsystem. These pointers maybe nullptr if the
  // operation was not initiated by an RPC call.
  const tserver::WriteRequestPB* client_request_ = nullptr;
  ReadHybridTime read_time_;
  bool allow_immediate_read_restart_ = false;
  std::unique_ptr<tserver::WriteRequestPB> client_request_holder_;
  tserver::WriteResponsePB* response_;

  // this transaction's start time
  CoarseTimePoint start_time_;

  HybridTime restart_read_ht_;

  bool schema_version_mismatch_ = false;

  docdb::DocOperations doc_ops_;

  std::function<void(const Status&)> callback_;

  ScopedRWOperation scoped_read_operation_;
  ExecuteMode execute_mode_;
  IsolationLevel isolation_level_;
  docdb::PrepareDocWriteOperationResult prepare_result_;
  std::unique_ptr<WriteQuery> self_; // Keep self while Execute is performed.
  // Indicates whether this WriteQuery object is currently contributing to the
  // 'kActiveWriteQueryObjects' tablet metric.
  bool did_update_active_write_queries_metric_ = false;
  // Stores the start time of the underlying rpc request that created this WriteQuery.
  // The field is consistent across failed ReadRpc/WriteRpc retries.
  uint64_t request_start_us_ = 0;
};

}  // namespace tablet
}  // namespace yb
