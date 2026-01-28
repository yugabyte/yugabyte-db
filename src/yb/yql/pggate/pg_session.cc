//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_session.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "yb/client/table_info.h"

#include "yb/common/pg_types.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/row_mark.h"
#include "yb/common/schema.h"
#include "yb/common/tablespace_parser.h"

#include "yb/gutil/casts.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/lw_function.h"
#include "yb/util/oid_generator.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

#include "yb/yql/pggate/pg_client.h"
#include "yb/yql/pggate/pg_op.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pggate.h"

using namespace std::literals;

DEPRECATE_FLAG(int32, ysql_wait_until_index_permissions_timeout_ms, "11_2022");
DECLARE_int32(TEST_user_ddl_operation_timeout_sec);

DEFINE_UNKNOWN_bool(ysql_log_failed_docdb_requests, false, "Log failed docdb requests.");
DEFINE_test_flag(bool, generate_ybrowid_sequentially, false,
                 "For new tables without PK, make the ybrowid column ASC and generated using a"
                 " naive per-node sequential counter. This can fail with collisions for a"
                 " multi-node cluster, and the ordering can be inconsistent in case multiple"
                 " connections generate ybrowid at the same time. In case a SPLIT INTO clause is"
                 " provided, fall back to the old behavior. The primary use case of this flag is"
                 " for ported pg_regress tests that expect deterministic output ordering based on"
                 " ctid. This is a best-effort reproduction of that, but it still falls short in"
                 " case of UPDATEs because PG regenerates ctid while YB doesn't.");
DEFINE_test_flag(bool, ysql_log_perdb_allocated_new_objectid, false,
                 "Log new object id returned by per database oid allocator");

DEFINE_RUNTIME_PG_FLAG(int32, yb_invalidation_message_expiration_secs, 10,
                       "The function yb_increment_db_catalog_version_with_inval_messages or "
                       "yb_increment_all_db_catalog_versions_with_inval_messages will delete "
                       "invalidation messages older than this time");

DEFINE_RUNTIME_PG_FLAG(int32, yb_max_num_invalidation_messages, 4096,
                       "If a DDL statement generates more than this number of invalidation "
                       "messages we do not associate the messages with the new catalog version "
                       "caused by this DDL statement. This effetively turns off incremental "
                       "catalog cache refresh for this new catalog version.");

DEFINE_RUNTIME_uint32(ysql_max_invalidation_message_queue_size, 1024,
                      "Maximum number of invalidation messages we keep for a given database.");

namespace yb::pggate {
namespace {

template<class Container, class Key>
void Erase(Container* container, const Key& key) {
  auto it = container->find(key);
  if (it != container->end()) {
    container->erase(it);
  }
}

YB_DEFINE_ENUM(SessionType, (kRegular)(kTransactional)(kCatalog));

bool IsNeedTransaction(const PgsqlOp& op, bool non_ddl_txn_for_sys_tables_allowed) {
  // op.need_transaction will be false for write operation in case upper level decides that
  // it is single row transaction. But in case the non_ddl_txn_for_sys_tables_allowed flag is true
  // extra read operation is possible within current transaction.
  //
  // Example:
  // CREATE TABLE t (k INT PRIMARY KEY);
  // INSERT INTO t VALUES(1);
  // SET yb_non_ddl_txn_for_sys_tables_allowed = true;
  // INSERT INTO t VALUES(1);
  //
  // Last statement inserts row with k = 1 and this is a single row transaction.
  // But row with k = 1 already exists in the t table. As a result the
  // 'duplicate key value violates unique constraint "t_pkey"' will be raised.
  // But this error contains constraints name which is read from sys table pg_class (in case it
  // is not yet in the postgres's cache). And this read from sys table will be performed in context
  // of currently running transaction (single row) because the yb_non_ddl_txn_for_sys_tables_allowed
  // GUC variable is true. As a result there will be 2 operations in context of single row
  // transaction.
  // To handle this situation correctly is it necessary to start transaction in case write
  // operation doesn't require it, but the non_ddl_txn_for_sys_tables_allowed flag is true.
  return op.need_transaction() || (op.is_write() && non_ddl_txn_for_sys_tables_allowed);
}

Result<bool> ShouldHandleTransactionally(const PgTxnManager& txn_manager,
                                         const PgTableDesc& table,
                                         const PgsqlOp& op,
                                         bool non_ddl_txn_for_sys_tables_allowed) {
  if (!table.schema().table_properties().is_transactional() ||
      !IsNeedTransaction(op, yb_non_ddl_txn_for_sys_tables_allowed) ||
      YBCIsInitDbModeEnvVarSet()) {
    return false;
  }
  const auto has_non_ddl_txn = txn_manager.IsTxnInProgress();
  if (!table.schema().table_properties().is_ysql_catalog_table()) {
    SCHECK(has_non_ddl_txn, IllegalState, "Transactional operation requires transaction");
    return true;
  }
  if (txn_manager.IsDdlMode() || (non_ddl_txn_for_sys_tables_allowed && has_non_ddl_txn)) {
    return true;
  }
  if (op.is_write()) {
    // For consistent read from catalog tables all write operations must be done in transaction.
    return STATUS_FORMAT(IllegalState,
                         "Transaction for catalog table write operation '$0' not found",
                         table.table_name().table_name());
  }
  return false;
}

Result<SessionType> GetRequiredSessionType(const PgTxnManager& txn_manager,
                                           const PgTableDesc& table,
                                           const PgsqlOp& op,
                                           bool non_ddl_txn_for_sys_tables_allowed) {
  if (VERIFY_RESULT(ShouldHandleTransactionally(txn_manager, table, op,
                                                non_ddl_txn_for_sys_tables_allowed))) {
    return SessionType::kTransactional;
  }

  return op.is_read() &&
         table.schema().table_properties().is_ysql_catalog_table() &&
        !YBCIsInitDbModeEnvVarSet()
      ? SessionType::kCatalog
      : SessionType::kRegular;
}

bool Empty(const PgOperationBuffer& buffer) {
  return !buffer.Size();
}

// Update the buffer setting. 'max_batch_size' will be adjusted to multiple of 'multiple'
void Update(BufferingSettings* buffering_settings, int multiple = 1) {
  /* Use the gflag value if the session variable is unset for batch size. */
  uint64_t max_batch_size = ysql_session_max_batch_size <= 0
    ? FLAGS_ysql_session_max_batch_size
    : static_cast<uint64_t>(ysql_session_max_batch_size);
  buffering_settings->max_batch_size = ((max_batch_size + multiple - 1) / multiple) * multiple;
  buffering_settings->max_in_flight_operations = static_cast<uint64_t>(ysql_max_in_flight_ops);
  buffering_settings->multiple = multiple;
  auto msg = Format("Adjust max_batch_size from $0 to $1", max_batch_size,
      buffering_settings->max_batch_size);
  if (multiple > 1) {
    LOG(INFO) << msg;
  } else {
    VLOG(3) << msg;
  }
}

RowMarkType GetRowMarkType(const PgsqlOp& op) {
  return op.is_read()
      ? GetRowMarkTypeFromPB(down_cast<const PgsqlReadOp&>(op).read_request())
      : RowMarkType::ROW_MARK_ABSENT;
}

bool IsReadOnly(const PgsqlOp& op) {
  return op.is_read() && !IsValidRowMarkType(GetRowMarkType(op));
}

Result<ReadHybridTime> GetReadTime(const PgsqlOps& operations) {
  ReadHybridTime read_time;
  for (const auto& op : operations) {
    if (op->read_time()) {
      // TODO(#18127): Substitute LOG_IF(WARNING) with SCHECK.
      // cumulative_add_cardinality_correction and cumulative_add_comprehensive_promotion in
      // TestPgRegressThirdPartyExtensionsHll fail on this check.
      LOG_IF(WARNING, read_time && read_time != op->read_time())
          << "Operations in a batch have different read times "
          << read_time << " vs " << op->read_time();
      read_time = op->read_time();
    }
  }
  return read_time;
}

// Helper function to chose read time with in_txn_limit from pair of read time.
// All components in read times except in_txn_limit must be equal.
// One of the read time must have in_txn_limit equal to HybridTime::kMax
// (i.e. must be default initialized)
Result<const ReadHybridTime&> ActualReadTime(
    std::reference_wrapper<const ReadHybridTime> read_time1,
    std::reference_wrapper<const ReadHybridTime> read_time2) {
  if (read_time1 == read_time2) {
    return read_time1.get();
  }
  const auto* read_time_with_in_txn_limit_max = &(read_time1.get());
  const auto* read_time = &(read_time2.get());
  if (read_time_with_in_txn_limit_max->in_txn_limit != HybridTime::kMax) {
    std::swap(read_time_with_in_txn_limit_max, read_time);
  }
  SCHECK(
      read_time_with_in_txn_limit_max->in_txn_limit == HybridTime::kMax,
      InvalidArgument, "At least one read time with kMax in_txn_limit is expected");
  auto tmp_read_time = *read_time;
  tmp_read_time.in_txn_limit = read_time_with_in_txn_limit_max->in_txn_limit;
  SCHECK(
      tmp_read_time == *read_time_with_in_txn_limit_max,
      InvalidArgument, "Ambiguous read time $0 $1", read_time1, read_time2);
  return *read_time;
}

Status UpdateReadTime(tserver::PgPerformOptionsPB* options, const ReadHybridTime& read_time) {
  ReadHybridTime options_read_time;
  const auto* actual_read_time = &read_time;
  if (options->has_read_time()) {
    options_read_time = ReadHybridTime::FromPB(options->read_time());
    // In case of follower reads a read time is set in the options, with the in_txn_limit set to
    // kMax. But when fetching the next page, the ops_read_time might have a different
    // in_txn_limit (received by the tserver with the first page).
    // ActualReadTime will select appropriate read time (read time with in_txn_limit)
    // and make necessary checks.

    actual_read_time = &VERIFY_RESULT_REF(ActualReadTime(options_read_time, read_time));
  }
  actual_read_time->AddToPB(options);
  return Status::OK();
}

void ApplyForceCatalogModification(PgsqlReadOp& op) {

}

void ApplyForceCatalogModification(PgsqlOp& op) {
  if (op.is_write()) {
    down_cast<PgsqlWriteOp&>(op).write_request().set_force_catalog_modifications(true);
  }
}

template<class PB>
void AdvisoryLockRequestInitCommon(
    PB& req, uint64_t session_id, const YbcAdvisoryLockId& lock_id, YbcAdvisoryLockMode mode) {
  req.set_session_id(session_id);
  req.set_db_oid(lock_id.database_id);
  auto& req_lock = *req.add_locks();
  auto& req_lock_id = *req_lock.mutable_lock_id();
  req_lock_id.set_classid(lock_id.classid);
  req_lock_id.set_objid(lock_id.objid);
  req_lock_id.set_objsubid(lock_id.objsubid);
  req_lock.set_lock_mode(mode == YbcAdvisoryLockMode::YB_ADVISORY_LOCK_EXCLUSIVE
      ? tserver::AdvisoryLockMode::LOCK_EXCLUSIVE : tserver::AdvisoryLockMode::LOCK_SHARE);

}

bool IsTableAffectedByOperations(
  const PgObjectId& table_id, const PgsqlOp& op, std::span<const PgObjectId> buffered_rels) {

  PgObjectId index_id;
  if (op.is_read()) {
    const auto& req = down_cast<const PgsqlReadOp&>(op).read_request();
    if (req.has_index_request()) {
      index_id = PgObjectId{req.index_request().table_id()};
    }
  }
  return std::ranges::any_of(
      buffered_rels,
      [&table_id, &index_id] (const auto& rel) { return table_id == rel || index_id == rel; });
}

std::optional<ReadTimeAction> MakeReadTimeActionForFlush(const PgTxnManager& txn_manager) {
  if (txn_manager.IsDdlMode()) {
    return std::nullopt;
  }
  return txn_manager.GetIsolationLevel() == IsolationLevel::NON_TRANSACTIONAL
      ? ReadTimeAction::RESET : ReadTimeAction::ENSURE_IS_SET;
}

} // namespace

//--------------------------------------------------------------------------------------------------
// Class PgSession::RunHelper
//--------------------------------------------------------------------------------------------------

class PgSession::RunHelper {
 public:
  RunHelper(
      PgSession* pg_session, SessionType session_type, HybridTime in_txn_limit,
      ForceNonBufferable force_non_bufferable)
      : pg_session_(*pg_session),
        session_type_(session_type),
        in_txn_limit_(in_txn_limit),
        force_non_bufferable_(force_non_bufferable) {
    VLOG(2) << "RunHelper session_type: " << ToString(session_type_)
            << ", in_txn_limit: " << ToString(in_txn_limit_)
            << ", force_non_bufferable: " << force_non_bufferable_;
  }

  Status Apply(const PgTableDesc& table, PgsqlOpPtr op) {
    // Refer src/yb/yql/pggate/README for details about buffering.
    //
    // TODO(#16261): Consider the following scenario:
    // 1) A read op arrives here and adds itself to operations_.
    // 2) Two write ops follow which change the same row and are added to operations_.
    //    Should they not be flushed separately? (as per requirement 2b in the README)
    //
    // Moreover, consider a situation in which they are flushed separately, but the in_txn_limit
    // picked might be incorrect:
    // 1) A non-bufferable write op on key k1 enters Apply() and is flushed.
    // 2) A read which has a preset in_txn_limit enters and is added to operations_. The
    //    in_txn_limit happens to be smaller than the write time of the write op in (1).
    // 3) Another write to key k1 is added to operations_. But it will end up using the preset
    //    in_txn_limit. Isn't this incorrect?
    //
    // Both scenarios might not occur with any sort of SQL statement. But we should still try to
    // find statements that can lead to this and fix these cases. Even if we don't find anything
    // that leads to such scenarios, we should fix these anyway (unless we prove these scenarios
    // can never occur).

    VLOG(2) << "Apply " << (op->is_read() ? "read" : "write") << " op"
            << ", table name: " << table.table_name().table_name()
            << ", table relfilenode id: " << table.relfilenode_id();

    auto& buffer = pg_session_.buffer_;

    // Try buffering this operation if it is a write operation, buffering is enabled and no
    // operations have been already applied to current session (yb session does not exist).
    if (ops_info_.ops.Empty() && pg_session_.buffering_enabled_ &&
        !force_non_bufferable_ && op->is_write()) {
        if (PREDICT_FALSE(yb_debug_log_docdb_requests)) {
          LOG_WITH_PREFIX(INFO) << "Buffering operation on table "
            << table.table_name().table_name() << ": "
            << op->ToString();
        }
        return buffer.Add(table,
                          PgsqlWriteOpPtr(std::move(op), down_cast<PgsqlWriteOp*>(op.get())),
                          IsTransactional());
    }
    bool read_only = op->is_read();
    // Flush all buffered operations (if any) before performing non-bufferable operation
    if (!Empty(buffer)) {
      SCHECK(ops_info_.ops.Empty(),
            IllegalState,
            "Buffered operations must be flushed before applying first non-bufferable operation");
      // Buffered write operations can't be combined within a single RPC with non-bufferable read
      // operations in case the reads have a preset in_txn_limit. This is because as per requirement
      // 1 and 2b) in README.txt, the writes and reads will require a different in_txn_limit. In
      // case the in_txn_limit wasn't yet picked for the reads, they can be flushed together with
      // the writes since both would use the current hybrid time as the in_txn_limit.
      //
      // Note however that the buffer has to be flushed before the non-buffered op if:
      // 1) it is a non-buffered read that is on a table that is touched by the buffered writes
      // 2) it is a non-buffered write that touches the same key as a buffered write
      //
      // Also operations for a catalog session are read-only operations. And "buffer" only buffers
      // write operations. Since the writes and catalog reads belong to different session types (as
      // per PgClientSession), we can't send them to the local tserver proxy in 1 rpc, so we flush
      // the buffer before performing catalog reads.
      YbcFlushDebugContext debug_context;
      debug_context.reason = YB_CONFLICTING_READ;
      debug_context.oidarg = table.pg_table_id().object_oid;
      debug_context.strarg1 = table.table_name().table_name().c_str();

      if ((IsTransactional() && in_txn_limit_) || IsCatalog()) {
        RETURN_NOT_OK(buffer.Flush(debug_context));
      } else {
        ops_info_.ops = VERIFY_RESULT(buffer.Take(IsTransactional(), debug_context));
        ops_info_.num_ops_taken_from_buffer = ops_info_.ops.Size();
        read_only = read_only && ops_info_.ops.Empty();
      }
    }

    if (PREDICT_FALSE(yb_debug_log_docdb_requests)) {
      LOG_WITH_PREFIX(INFO) << "Applying operation on table "
                            << table.table_name().table_name()
                            << ": " << op->ToString();
    }

    const auto row_mark_type = GetRowMarkType(*op);

    RETURN_NOT_OK(Add(table, std::move(op)));

    if (!IsTransactional()) {
      return Status::OK();
    }

    read_only = read_only && !IsValidRowMarkType(row_mark_type);

    return pg_session_.pg_txn_manager_->CalculateIsolation(
        read_only, pg_session_.pg_txn_manager_->GetTxnPriorityRequirement(row_mark_type));
  }

  Result<PerformFuture> Flush(std::optional<CacheOptions>&& cache_options) {
    if (ops_info_.ops.Empty()) {
      // All operations were buffered, no need to flush.
      return PerformFuture();
    }

    if (PREDICT_FALSE(yb_debug_log_docdb_requests)) {
      LOG_WITH_PREFIX(INFO) << "Flushing collected operations, using session type: "
                            << ToString(session_type_) << " num ops: " << ops_info_.ops.Size();
    }

    return pg_session_.Perform(
        std::move(ops_info_.ops),
        {.use_catalog_session = IsCatalog(),
         .cache_options = std::move(cache_options),
         .in_txn_limit = in_txn_limit_
        });
  }

 private:
  inline static bool IsTransactional(SessionType type) {
    return type == SessionType::kTransactional;
  }

  inline bool IsTransactional() const {
    return IsTransactional(session_type_);
  }

  inline UseCatalogSession IsCatalog() const {
    return UseCatalogSession(session_type_ == SessionType::kCatalog);
  }

  inline std::string LogPrefix() const {
    return pg_session_.LogPrefix();
  }

  Status Add(const PgTableDesc& table, PgsqlOpPtr&& op) {
    if (IsTableAffectedByOperations(
        table.pg_table_id(), *op,
        {ops_info_.ops.relations().data(), ops_info_.num_ops_taken_from_buffer})) {
      auto [buffered_ops, ops] =
          Split(std::move(ops_info_.ops), ops_info_.num_ops_taken_from_buffer);
      DCHECK_EQ(ops_info_.num_ops_taken_from_buffer, buffered_ops.Size());
      ops_info_.ops = std::move(ops);
      ops_info_.num_ops_taken_from_buffer = 0;

      YbcFlushDebugContext debug_context;
      debug_context.reason = YB_CONFLICTING_READ;
      debug_context.oidarg = table.pg_table_id().object_oid;
      debug_context.strarg1 = table.table_name().table_name().c_str();

      RETURN_NOT_OK(VERIFY_RESULT(pg_session_.FlushOperations(
        std::move(buffered_ops), IsTransactional(), debug_context)).Get());
    }
    ops_info_.ops.Add(std::move(op), table);
    return Status::OK();
  }

  struct OperationsInfo {
    BufferableOperations ops;
    size_t num_ops_taken_from_buffer{0};
  };

  OperationsInfo ops_info_;
  PgSession& pg_session_;
  const SessionType session_type_;
  const HybridTime in_txn_limit_;
  const ForceNonBufferable force_non_bufferable_;
};

//--------------------------------------------------------------------------------------------------
// Class PgSession
//--------------------------------------------------------------------------------------------------

PgSession::PgSession(
    PgClient& pg_client,
    scoped_refptr<PgTxnManager> pg_txn_manager,
    const YbcPgCallbacks& pg_callbacks,
    YbcPgExecStatsState& stats_state,
    bool is_pg_binary_upgrade,
    std::reference_wrapper<const WaitEventWatcher> wait_event_watcher,
    BufferingSettings& buffering_settings)
    : pg_client_(pg_client),
      pg_txn_manager_(std::move(pg_txn_manager)),
      metrics_(stats_state),
      pg_callbacks_(pg_callbacks),
      buffering_settings_(buffering_settings),
      buffer_(
          [this](
              BufferableOperations&& ops, bool transactional,
              const YbcFlushDebugContext& debug_context) {
            return FlushOperations(std::move(ops), transactional, debug_context);
          },
          buffering_settings_),
      is_major_pg_version_upgrade_(is_pg_binary_upgrade),
      wait_event_watcher_(wait_event_watcher) {
  Update(&buffering_settings_);
}

PgSession::~PgSession() = default;

//--------------------------------------------------------------------------------------------------

Status PgSession::IsDatabaseColocated(const PgOid database_oid, bool *colocated,
                                      bool *legacy_colocated_database) {
  auto resp = VERIFY_RESULT(pg_client_.GetDatabaseInfo(database_oid));
  *colocated = resp.colocated();
  if (resp.has_legacy_colocated_database())
    *legacy_colocated_database = resp.legacy_colocated_database();
  else
    *legacy_colocated_database = true;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status PgSession::DropDatabase(const std::string& database_name, PgOid database_oid) {
  tserver::PgDropDatabaseRequestPB req;
  req.set_database_name(database_name);
  req.set_database_oid(database_oid);

  RETURN_NOT_OK(pg_client_.DropDatabase(&req, CoarseTimePoint()));
  return Status::OK();
}

// This function is only used to get the protobuf-based catalog version, not using
// the pg_yb_catalog_version table.
Status PgSession::GetCatalogMasterVersion(uint64_t *version) {
  *version = VERIFY_RESULT(pg_client_.GetCatalogMasterVersion());
  return Status::OK();
}

Result<int> PgSession::GetXClusterRole(uint32_t db_oid) {
  return pg_client_.GetXClusterRole(db_oid);
}

Status PgSession::CancelTransaction(const unsigned char* transaction_id) {
  return pg_client_.CancelTransaction(transaction_id);
}

Status PgSession::CreateSequencesDataTable() {
  return pg_client_.CreateSequencesDataTable();
}

Status PgSession::InsertSequenceTuple(int64_t db_oid,
                                      int64_t seq_oid,
                                      uint64_t ysql_catalog_version,
                                      bool is_db_catalog_version_mode,
                                      int64_t last_val,
                                      bool is_called) {
  return pg_client_.InsertSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, last_val, is_called);
}

Result<bool> PgSession::UpdateSequenceTuple(int64_t db_oid,
                                            int64_t seq_oid,
                                            uint64_t ysql_catalog_version,
                                            bool is_db_catalog_version_mode,
                                            int64_t last_val,
                                            bool is_called,
                                            std::optional<int64_t> expected_last_val,
                                            std::optional<bool> expected_is_called) {
  return pg_client_.UpdateSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, last_val, is_called,
      expected_last_val, expected_is_called);
}

Result<std::pair<int64_t, int64_t>> PgSession::FetchSequenceTuple(int64_t db_oid,
                                                                  int64_t seq_oid,
                                                                  uint64_t ysql_catalog_version,
                                                                  bool is_db_catalog_version_mode,
                                                                  uint32_t fetch_count,
                                                                  int64_t inc_by,
                                                                  int64_t min_value,
                                                                  int64_t max_value,
                                                                  bool cycle) {
  return pg_client_.FetchSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, fetch_count, inc_by,
      min_value, max_value, cycle);
}

Result<std::pair<int64_t, bool>> PgSession::ReadSequenceTuple(int64_t db_oid,
                                                              int64_t seq_oid,
                                                              uint64_t ysql_catalog_version,
                                                              bool is_db_catalog_version_mode) {
  std::optional<uint64_t> optional_ysql_catalog_version = std::nullopt;
  std::optional<uint64_t> optional_yb_read_time = std::nullopt;
  if (!yb_disable_catalog_version_check) {
    optional_ysql_catalog_version = ysql_catalog_version;
  }
  if (yb_read_time != 0) {
    optional_yb_read_time = yb_read_time;
  }
  return pg_client_.ReadSequenceTuple(
      db_oid, seq_oid, optional_ysql_catalog_version, is_db_catalog_version_mode,
      optional_yb_read_time);
}

//--------------------------------------------------------------------------------------------------

Status PgSession::DropTable(const PgObjectId& table_id, bool use_regular_transaction_block) {
  tserver::PgDropTableRequestPB req;
  table_id.ToPB(req.mutable_table_id());
  req.set_use_regular_transaction_block(use_regular_transaction_block);
  RETURN_NOT_OK(
      SetupIsolationAndPerformOptionsForDdl(req.mutable_options(), use_regular_transaction_block));
  return ResultToStatus(pg_client_.DropTable(&req, CoarseTimePoint()));
}

Status PgSession::DropIndex(
    const PgObjectId& index_id,
    bool use_regular_transaction_block,
    client::YBTableName* indexed_table_name) {
  tserver::PgDropTableRequestPB req;
  index_id.ToPB(req.mutable_table_id());
  req.set_index(true);
  req.set_use_regular_transaction_block(use_regular_transaction_block);
  RETURN_NOT_OK(
    SetupIsolationAndPerformOptionsForDdl(req.mutable_options(), use_regular_transaction_block));
  auto result = VERIFY_RESULT(pg_client_.DropTable(&req, CoarseTimePoint()));
  if (indexed_table_name) {
    *indexed_table_name = std::move(result);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Result<PgTableDescPtr> PgSession::DoLoadTable(
    const PgObjectId& table_id, bool fail_on_cache_hit, master::IncludeHidden include_hidden) {
  auto cached_table_it = table_cache_.find(table_id);
  const auto exists = cached_table_it != table_cache_.end();
  const auto cache_hit = exists && cached_table_it->second;
  SCHECK(!(fail_on_cache_hit && cache_hit), IllegalState,
        "Cache entry found while cache miss is expected.");
  if (cache_hit) {
    auto status = cached_table_it->second->EnsurePartitionListIsUpToDate(&pg_client_);
    if (status.ok()) {
      return cached_table_it->second;
    }

    // Failed to reload table partitions, let's try to reload the table.
    LOG(WARNING) << Format(
        "Partition list refresh failed for table \"$0\": $1. Invalidating table cache.",
        cached_table_it->second->table_name(), status);
    InvalidateTableCache(table_id, InvalidateOnPgClient::kFalse);
    return DoLoadTable(table_id, /* fail_on_cache_hit */ true, include_hidden);
  }

  VLOG(4) << "Table cache MISS: " << table_id << " reopen = " << exists;
  auto table = VERIFY_RESULT(
      pg_client_.OpenTable(
        table_id, exists, table_cache_min_ysql_catalog_version_, include_hidden));

  if (exists) {
    cached_table_it->second = table;
  } else {
    table_cache_.emplace(table_id, table);
  }
  return table;
}

Result<PgTableDescPtr> PgSession::LoadTable(const PgObjectId& table_id) {
  VLOG(3) << "Loading table descriptor for " << table_id;
  // When loading table description and yb_read_time is set, return the table properties even if the
  // table is hidden. For instance, this is required for succesful return of yb_table_properties()
  // when yb_read_time is set and the table was hidden at yb_read_time.
  master::IncludeHidden include_hidden = master::IncludeHidden(yb_read_time != 0);
  return DoLoadTable(table_id, /* fail_on_cache_hit */ false, include_hidden);
}

void PgSession::InvalidateTableCache(
    const PgObjectId& table_id, InvalidateOnPgClient invalidate_on_pg_client) {
  if (invalidate_on_pg_client) {
    // Keep special record about this table_id, so when we would open this table again,
    // reopen flag will be sent to pg client service.
    // This flag means that pg client service should remove table from his cache and fetch
    // new data from master.
    // It is optional optimization, but some tests fails w/o it, since they expect that
    // local table information is updated after alter table operation.
    table_cache_[table_id] = nullptr;
  } else {
    auto it = table_cache_.find(table_id);
    if (it != table_cache_.end() && it->second) {
      table_cache_.erase(it);
    }
  }
}

void PgSession::InvalidateAllTablesCache(uint64_t min_ysql_catalog_version) {
  if (table_cache_min_ysql_catalog_version_ >= min_ysql_catalog_version) {
    return;
  }

  table_cache_min_ysql_catalog_version_ = min_ysql_catalog_version;
  table_cache_.clear();
}

Result<client::TableSizeInfo> PgSession::GetTableDiskSize(const PgObjectId& table_oid) {
  return pg_client_.GetTableDiskSize(table_oid);
}

Status PgSession::StartOperationsBuffering() {
  SCHECK(!buffering_enabled_, IllegalState, "Buffering has been already started");
  if (PREDICT_FALSE(!Empty(buffer_))) {
    LOG(DFATAL) << "Buffering hasn't been started yet but "
                << buffer_.Size()
                << " buffered operations found";
  }
  Update(&buffering_settings_, 1 /* multiple */);
  buffering_enabled_ = true;
  return Status::OK();
}

Status PgSession::StopOperationsBuffering() {
  SCHECK(buffering_enabled_, IllegalState, "Buffering hasn't been started");
  buffering_enabled_ = false;
  return FlushBufferedOperations(YB_END_OPERATIONS_BUFFERING);
}

void PgSession::ResetOperationsBuffering() {
  DropBufferedOperations();
  buffering_enabled_ = false;
}

std::string PgSession::FlushReasonToString(const YbcFlushDebugContext& debug_context) const {
  switch (debug_context.reason) {
    // Transaction control
    case YbcFlushReason::YB_BEGIN_SUBTRANSACTION:
      return Format("due to begin of new subtransaction for $0 (current SubTransactionId: $1)",
                    debug_context.strarg1 ? debug_context.strarg1 : "unnamed txn",
                    debug_context.uintarg);
    case YbcFlushReason::YB_END_SUBTRANSACTION:
      return Format("due to end of subtransaction with SubTransactionId $0", debug_context.uintarg);
    case YbcFlushReason::YB_ACTIVATE_SUBTRANSACTION:
      return Format("due to activation of subtransaction with SubTransactionId $0",
                    debug_context.uintarg);
    case YbcFlushReason::YB_ROLLBACK_TO_SUBTRANSACTION:
      return Format("due to rollback to subtransaction with SubTransactionId $0",
                    debug_context.uintarg);
    case YbcFlushReason::YB_COMMIT_TRANSACTION:
      return Format("due to commit of plain transaction ($0)",
                    debug_context.oidarg == kInvalidOid ?
                    "contains no DDL ops" :
                    Format("contains DDL on database with OID $0", debug_context.oidarg));

    // Snapshot management
    case YbcFlushReason::YB_GET_TRANSACTION_SNAPSHOT:
      return Format("before getting a new transaction snapshot (current read point: $0) in "
                    "Read Committed isolation", debug_context.uintarg);
    case YbcFlushReason::YB_CHANGE_TRANSACTION_SNAPSHOT:
      return Format("before restoring transaction snapshot corresponding to read point $0",
                    debug_context.uintarg);

    // DDLs
    case YbcFlushReason::YB_ENTER_DDL_TRANSACTION_MODE:
      return "due to entering DDL mode";
    case YbcFlushReason::YB_EXIT_DDL_TRANSACTION_MODE:
      return "due to exiting DDL mode";

    // Functions, stored procedures and utilities
    case YbcFlushReason::YB_UNBATCHABLE_SQL_STMT_IN_SQL_FUNCTION:
      return Format("before executing non-DML statement $0 (see CmdType in nodes.h) "
                    "in SQL function '$1'", debug_context.uintarg, debug_context.strarg1);
    case YbcFlushReason::YB_UNBATCHABLE_PL_STMT:
      return Format("before executing PL statement of type '$0' in function '$1'",
                    debug_context.strarg1, debug_context.strarg2);
    case YbcFlushReason::YB_UNBATCHABLE_SQL_STMT_IN_PL_FUNCTION:
      return Format("before executing SQL statement with command tag '$0' in PL function '$1'",
                    debug_context.strarg1, debug_context.strarg2);
    case YbcFlushReason::YB_COPY_BATCH:
      return Format("after copying batch of tuples for table '$0' (total tuples processed: $1)",
                    debug_context.strarg1, debug_context.uintarg);

    // Miscellaneous reasons
    case YbcFlushReason::YB_SWITCH_TO_DB_CATALOG_VERSION_MODE:
      return Format("switch from global to per-database catalog version mode "
                    "(current DB OID: $0)", debug_context.oidarg);
    case YbcFlushReason::YB_CATALOG_TABLE_PREFETCH:
      return "before prefetching catalog tables";
    case YbcFlushReason::YB_END_OF_TOP_LEVEL_STMT:
      return "at the end of top-level statement";
    case YbcFlushReason::YB_END_OPERATIONS_BUFFERING:
      return "at the end of operations buffering";

    // Internal buffer control
    case YbcFlushReason::YB_BUFFER_FULL:
      return Format("due to buffer being full (ops size: $0 bytes)", debug_context.uintarg);
    case YbcFlushReason::YB_CONFLICTING_KEY_WRITE:
      return Format("before enqueueing a conflicting write operation on table '$0' with OID $1$2",
                    debug_context.strarg1, debug_context.oidarg,
                    debug_context.strarg2 ? Format(" (key: $0)", debug_context.strarg2) : "");
    case YbcFlushReason::YB_CONFLICTING_READ:
      return Format("before performing a non-bufferable read operation on table '$0' with OID $1",
                    debug_context.strarg1, debug_context.oidarg);
  }

  return "for unknown reason"; // keep compiler happy
}

Status PgSession::FlushBufferedOperations(
    const YbcFlushDebugContext& debug_context) {
  return buffer_.Flush(debug_context);
}

Status PgSession::FlushBufferedOperations(YbcFlushReason reason) {
  YbcFlushDebugContext debug_context;
  debug_context.reason = reason;
  return FlushBufferedOperations(debug_context);
}

void PgSession::DropBufferedOperations() {
  buffer_.Clear();
}

Status PgSession::AdjustOperationsBuffering(int multiple) {
  SCHECK(buffering_enabled_, IllegalState, "Buffering has not started yet");
  if (PREDICT_FALSE(!Empty(buffer_))) {
    LOG(DFATAL) << "Buffer should be empty, but "
                << buffer_.Size()
                << " buffered operations found";
  }
  Update(&buffering_settings_, multiple);
  return Status::OK();
}

PgIsolationLevel PgSession::GetIsolationLevel() {
  return pg_txn_manager_->GetPgIsolationLevel();
}

bool PgSession::IsHashBatchingEnabled() {
  return yb_enable_hash_batch_in &&
      GetIsolationLevel() != PgIsolationLevel::SERIALIZABLE;
}

std::string PgSession::GenerateNewYbrowid() {
  if (PREDICT_FALSE(FLAGS_TEST_generate_ybrowid_sequentially)) {
    unsigned char buf[sizeof(uint64_t)];
    BigEndian::Store64(buf, MonoTime::Now().ToUint64());
    return std::string(reinterpret_cast<char*>(buf), sizeof(buf));
  }

  // Generate a new random and unique v4 UUID.
  return GenerateObjectId(true /* binary_id */);
}

Result<bool> PgSession::IsInitDbDone() {
  return pg_client_.IsInitDbDone();
}

Result<FlushFuture> PgSession::FlushOperations(
    BufferableOperations&& ops, bool transactional, const YbcFlushDebugContext& debug_context) {
  if (PREDICT_FALSE(yb_debug_log_docdb_requests)) {
    LOG_WITH_PREFIX(INFO) << "Flushing buffered operations " << FlushReasonToString(debug_context)
                          << " using " << (transactional ? "transactional" : "non-transactional")
                          << " session (num ops: " << ops.Size() << ")";
  }

  if (transactional) {
    RETURN_NOT_OK(pg_txn_manager_->CalculateIsolation(
        false /* read_only */,
        pg_txn_manager_->GetTxnPriorityRequirement(RowMarkType::ROW_MARK_ABSENT)));
  }

  // When YSQL is flushing a pipeline of Perform rpcs asynchronously i.e., without waiting for
  // responses of those rpcs, it is required that PgClientService pick a read time before the very
  // first request that is forwarded to docdb (potentially on a remote t-server) so that the same
  // read time can be used for all Perform rpcs in the transaction.
  //
  // If there is no pipelining of Perform rpcs, the read time can be picked on docdb and the later
  // Perform rpcs can use the same read time. This has some benefits such as not having to wait
  // for the safe time on docdb to catch up with an already chosen read time, and allowing docdb to
  // internally retry the request in case of read restart/ conflict errors.
  //
  // ReadTimeAction helps to determine whether it can safely use the optimization of allowing
  // docdb (which serves the operation) to pick the read time.

  return FlushFuture{
      VERIFY_RESULT(Perform(
          std::move(ops), { .read_time_action = MakeReadTimeActionForFlush(*pg_txn_manager_) })),
      *this, metrics_};
}

Result<PerformFuture> PgSession::Perform(BufferableOperations&& ops, PerformOptions&& ops_options) {
  DCHECK(!ops.Empty());
  tserver::PgPerformOptionsPB options;
  if (ops_options.use_catalog_session) {
    if (catalog_read_time_) {
      catalog_read_time_.ToPB(options.mutable_read_time());
    }
    options.set_use_catalog_session(true);
  } else {
    RETURN_NOT_OK(pg_txn_manager_->SetupPerformOptions(&options, ops_options.read_time_action));
    if (pg_txn_manager_->IsTxnInProgress()) {
      options.mutable_in_txn_limit_ht()->set_value(ops_options.in_txn_limit.ToUint64());
    }
    auto ops_read_time = VERIFY_RESULT(GetReadTime(ops.operations()));
    if (ops_read_time) {
      RETURN_NOT_OK(UpdateReadTime(&options, ops_read_time));
    }
  }

  options.set_force_global_transaction(yb_force_global_transaction);
  options.set_force_tablespace_locality(yb_force_tablespace_locality);
  options.set_force_tablespace_locality_oid(yb_force_tablespace_locality_oid);
  options.set_is_all_region_local(std::all_of(
          ops.operations().begin(), ops.operations().end(),
          [](const auto& op) { return op->is_region_local(); }));

  // For DDLs, ysql_upgrades and PGCatalog accesses, we always use the default read-time
  // and effectively skip xcluster_database_consistency which enables reads as of xcluster safetime.
  options.set_use_xcluster_database_consistency(
      yb_xcluster_consistency_level == XCLUSTER_CONSISTENCY_DATABASE &&
      !(ops_options.use_catalog_session || pg_txn_manager_->IsDdlMode() ||
        yb_non_ddl_txn_for_sys_tables_allowed));

  if (yb_read_time != 0) {
    SCHECK(
        !pg_txn_manager_->IsDdlMode(), IllegalState,
        "DDL operation can not be performed while yb_read_time is set to nonzero.");
    // Disallow serializable reads that are not read-only as they need to acquire locks and thus,
    // not pure reads.
    SCHECK(
        pg_txn_manager_->GetIsolationLevel() != IsolationLevel::SERIALIZABLE_ISOLATION,
        IllegalState,
        "Transactions with serializable isolation can not be performed while yb_read_time is set "
        "to nonzero. Try setting the transaction as read only or try another isolation level.");
    // Only read-only DMLs are allowed when yb_read_time is set to non-zero.
    for (const auto& pg_op : ops.operations()) {
      SCHECK(
          IsReadOnly(*pg_op), IllegalState,
          "Write DML operation can not be performed while yb_read_time is set to nonzero.");
    }
    if (yb_is_read_time_ht) {
      ReadHybridTime::FromUint64(yb_read_time).ToPB(options.mutable_read_time());
    } else {
      ReadHybridTime::FromMicros(yb_read_time).ToPB(options.mutable_read_time());
    }
  }

  // If all operations belong to the same database then set the namespace.
  // System database template1 is ignored as we may read global system catalog like tablespaces
  // in the same batch.
  if (!ops.Empty()) {
    auto database_oid = kPgInvalidOid;
    for (const auto& relation : ops.relations()) {
      if (relation.database_oid == kTemplate1Oid) {
        continue;
      }

      if (PREDICT_FALSE(database_oid != kPgInvalidOid && database_oid != relation.database_oid)) {
        // We do not expect this to be true. Adding a log to catch violation just in case.
        YB_LOG_EVERY_N_SECS(WARNING, 60) << Format(
            "Operations from multiple databases ('$0', '$1') found in a single Perform step",
            database_oid, relation.database_oid);
        database_oid = kPgInvalidOid;
        break;
      }

      database_oid = relation.database_oid;
    }

    if (database_oid != kPgInvalidOid) {
      options.set_namespace_id(GetPgsqlNamespaceId(database_oid));
    }
  }
  options.set_trace_requested(pg_txn_manager_->ShouldEnableTracing());

  if (ops_options.cache_options) {
    auto& cache_options = *ops_options.cache_options;
    auto& caching_info = *options.mutable_caching_info();
    caching_info.set_key_group(cache_options.key_group);
    caching_info.set_key_value(std::move(cache_options.key_value));
    if (cache_options.lifetime_threshold_ms) {
      caching_info.mutable_lifetime_threshold_ms()->set_value(*cache_options.lifetime_threshold_ms);
    }
  }

  // Workaround for index backfill case:
  //
  // In case of index backfill, the read_time is set and is to be used for reading. However, if
  // read committed isolation is enabled, the read_time_manipulation is also set to RESET for
  // index backfill since it is a non-DDL statement.
  //
  // As a workaround, clear the read time manipulation to prefer read time over manipulation in
  // case both are set. Remove after proper fix in context of GH #18080.
  if (options.read_time_manipulation() != tserver::ReadTimeManipulation::NONE &&
      options.has_read_time()) {
    options.clear_read_time_manipulation();
  }

  DCHECK(!options.has_read_time() || options.isolation() != IsolationLevel::SERIALIZABLE_ISOLATION);

#ifndef NDEBUG
  if (!options.ddl_mode() && !options.use_catalog_session()) {
    LOG_IF(DFATAL, !pg_txn_manager_->CheckPlainTxnRequestedObjectLocks().ok());
  }
#endif

  PgsqlOps operations;
  PgObjectIds relations;
  std::move(ops).MoveTo(operations, relations);
  return PerformFuture(
      pg_client_.PerformAsync(&options, std::move(operations), metrics_),
      std::move(relations));
}

InsertOnConflictBuffer& PgSession::GetInsertOnConflictBuffer(void* plan) {
  auto iter = std::find_if(
      insert_on_conflict_buffers_.begin(), insert_on_conflict_buffers_.end(),
      [&plan](const InsertOnConflictPlanBuffer& buffer) {
        return buffer.first == plan;
      });

  if (iter == insert_on_conflict_buffers_.end()) {
    insert_on_conflict_buffers_.emplace_back(std::make_pair(plan, InsertOnConflictBuffer()));
    return insert_on_conflict_buffers_.back().second;
  }

  return iter->second;
}

InsertOnConflictBuffer& PgSession::GetInsertOnConflictBuffer() {
  CHECK_EQ(insert_on_conflict_buffers_.empty(), false);
  return insert_on_conflict_buffers_.front().second;
}

void PgSession::ClearAllInsertOnConflictBuffers() {
  for (auto& buffer : insert_on_conflict_buffers_) {
    // Since we will end up clearing all intents, might as well simply pass true for all the calls.
    buffer.second.Clear(true /* clear_intents */);
  }

  insert_on_conflict_buffers_.clear();
}

void PgSession::ClearInsertOnConflictBuffer(void* plan) {
  auto iter = std::find_if(
      insert_on_conflict_buffers_.begin(), insert_on_conflict_buffers_.end(),
      [&plan](const InsertOnConflictPlanBuffer& buffer) {
        return buffer.first == plan;
      });

  DCHECK(iter != insert_on_conflict_buffers_.end());
  // This should only be called in case the keys_ map was manually cleared beforehand.  Otherwise,
  // we could run into memory leaks of the slots in that map.
  DCHECK_EQ(iter->second.GetNumIndexKeys(), 0);
  // Only clear the global intents cache if this is the final buffer.
  iter->second.Clear(insert_on_conflict_buffers_.size() == 1 /* clear_intents */);
  insert_on_conflict_buffers_.erase(iter);
}

bool PgSession::IsInsertOnConflictBufferEmpty() const {
  return insert_on_conflict_buffers_.empty();
}

Result<int> PgSession::TabletServerCount(bool primary_only) {
  return pg_client_.TabletServerCount(primary_only);
}

Result<yb::tserver::PgGetLockStatusResponsePB> PgSession::GetLockStatusData(
    const std::string& table_id, const std::string& transaction_id) {
  return pg_client_.GetLockStatusData(table_id, transaction_id);
}

Result<client::TabletServersInfo> PgSession::ListTabletServers() {
  return pg_client_.ListLiveTabletServers(false);
}

Status PgSession::GetIndexBackfillProgress(std::vector<PgObjectId> index_ids,
                                           uint64_t* num_rows_read_from_table,
                                           double* num_rows_backfilled) {
  return pg_client_.GetIndexBackfillProgress(index_ids,
                                             num_rows_read_from_table,
                                             num_rows_backfilled);
}

void PgSession::ResetCatalogReadPoint() {
  catalog_read_time_ = ReadHybridTime();
}

void PgSession::TrySetCatalogReadPoint(const ReadHybridTime& read_ht) {
  if (read_ht) {
    catalog_read_time_ = read_ht;
  }
}

Status PgSession::SetupIsolationAndPerformOptionsForDdl(
  tserver::PgPerformOptionsPB* options, bool use_regular_transaction_block) {
  if (!use_regular_transaction_block) {
    return Status::OK();
  }
  RSTATUS_DCHECK(
      pg_txn_manager_->IsDdlModeWithRegularTransactionBlock(), IllegalState,
      "Expected to be in DDL mode with regular transaction block");

  RETURN_NOT_OK(pg_txn_manager_->CalculateIsolation(
    false /* read_only */,
    pg_txn_manager_->GetTxnPriorityRequirement(RowMarkType::ROW_MARK_ABSENT)));

  return pg_txn_manager_->SetupPerformOptions(options);
}

Status PgSession::SetActiveSubTransaction(SubTransactionId id) {
  YbcFlushDebugContext debug_context;
  debug_context.reason = YbcFlushReason::YB_ACTIVATE_SUBTRANSACTION;
  debug_context.uintarg = id;
  // It's required that we flush all buffered operations before changing the SubTransactionMetadata
  // used by the underlying batcher and RPC logic, as this will snapshot the current
  // SubTransactionMetadata for use in construction of RPCs for already-queued operations, thereby
  // ensuring that previous operations use previous SubTransactionMetadata. If we do not flush here,
  // already queued operations may incorrectly use this newly modified SubTransactionMetadata when
  // they are eventually sent to DocDB.
  VLOG(4) << "SetActiveSubTransactionId " << id;
  RETURN_NOT_OK(FlushBufferedOperations(debug_context));
  pg_txn_manager_->SetActiveSubTransactionId(id);

  return Status::OK();
}

Status PgSession::RollbackToSubTransaction(SubTransactionId id) {
  YbcFlushDebugContext debug_context;
  debug_context.reason = YbcFlushReason::YB_ROLLBACK_TO_SUBTRANSACTION;
  debug_context.uintarg = id;
  // See comment in SetActiveSubTransaction -- we must flush buffered operations before updating any
  // SubTransactionMetadata.
  // TODO(read committed): performance improvement -
  // don't wait for ops which have already been sent ahead by pg_session i.e., to the batcher, then
  // rpc layer and beyond. For such ops, rely on aborted sub txn list in status tablet to invalidate
  // writes which will be asynchronously written to txn participants.
  RETURN_NOT_OK(FlushBufferedOperations(debug_context));
  const auto status = pg_txn_manager_->RollbackToSubTransaction(id);
  VLOG_WITH_FUNC(4) << "id: " << id << ", error: " << status;
  return status;
}

void PgSession::SetTransactionHasWrites() {
  pg_txn_manager_->SetTransactionHasWrites();
}

Result<bool> PgSession::CurrentTransactionUsesFastPath() const {
  // Single-shard modifications outside of an explicit transaction block are considered "fast-path"
  // when:
  // 1. The isolation level is NON_TRANSACTIONAL.
  // 2. The statement has performed at least one write - this is required because standalone read
  //    statements and reads before the first write/explicit lock in a transaction block will have
  //    its isolation level set to NON_TRANSACTIONAL.
  // Further, we also check if the operations buffer is empty because in some scenarios, such as
  // stored procedures, writes can be buffered and not flushed at the statement boundary, but rather
  // at a later point in the execution of the procedure. In such cases, the isolation level for the
  // buffered writes is not computed at the time of calling this function, leading the transaction
  // manager to report the isolation level as NON_TRANSACTIONAL (the default).
  //
  // Note that the fast-path variant of the COPY command uses a combination of the fast-path with
  // operations bufferring. However, it is guaranteed to perform a flush at the end of the statement
  // and it cannot be invoked via EXPLAIN (the only caller of this function). So, we can safely
  // ignore this case here.
  return VERIFY_RESULT(pg_txn_manager_->TransactionHasNonTransactionalWrites()) && !buffer_.Size();
}

void PgSession::ResetHasCatalogWriteOperationsInDdlMode() {
  has_catalog_write_ops_in_ddl_mode_ = false;
}

bool PgSession::HasCatalogWriteOperationsInDdlMode() const {
  return has_catalog_write_ops_in_ddl_mode_ && pg_txn_manager_->IsDdlMode();
}

void PgSession::SetDdlHasSyscatalogChanges() {
  pg_txn_manager_->SetDdlHasSyscatalogChanges();
}

Status PgSession::ValidatePlacements(
    const std::string& live_placement_info,
    const std::string& read_replica_placement_info,
    bool check_satisfiable) {
  // If no placements were provided, fall back to cluster defaults.
  if (live_placement_info.empty() && read_replica_placement_info.empty()) {
    return Status::OK();
  }

  if (!read_replica_placement_info.empty() && live_placement_info.empty()) {
    return STATUS(InvalidArgument,
                  "read_replica_placement option requires replica_placement to be set");
  }

  ReplicationInfoPB replication_info =
      VERIFY_RESULT(TablespaceParser::FromString(
          live_placement_info, read_replica_placement_info,
          true /* fail_on_validation_error */));

  if (check_satisfiable && !live_placement_info.empty()) {
    tserver::PgValidatePlacementRequestPB req;
    *req.mutable_replication_info() = std::move(replication_info);
    return pg_client_.ValidatePlacement(&req);
  }

  return Status::OK();
}

template<class Generator>
Result<PerformFuture> PgSession::DoRunAsync(
    const Generator& generator, const RunOptions& options,
    std::optional<CacheOptions>&& cache_options) {
  const auto first_table_op = generator();
  RSTATUS_DCHECK(!first_table_op.IsEmpty(), IllegalState, "Operation list must not be empty");
  // Previously, yb_non_ddl_txn_for_sys_tables_allowed flag caused CREATE VIEW to fail with
  // read restart error because subsequent cache refresh used an outdated txn to read from the
  // system catalog.
  // As a quick fix, we prevent yb_non_ddl_txn_for_sys_tables_allowed from affecting reads.
  //
  // If we're in major PG version upgrade mode, then it's safe to use a non-DDL transaction for
  // access to the PG catalog because it will be the next-version catalog which is only being
  // accessed by the one process that's currently doing the writes.
  const auto non_ddl_txn_for_sys_tables_allowed =
      yb_non_ddl_txn_for_sys_tables_allowed || is_major_pg_version_upgrade_;
  const auto group_session_type = VERIFY_RESULT(GetRequiredSessionType(
      *pg_txn_manager_, *first_table_op.table, **first_table_op.operation,
      non_ddl_txn_for_sys_tables_allowed));
  auto table_op = generator();
  RunHelper runner(
      this, group_session_type, options.in_txn_limit, options.force_non_bufferable);
  const auto ddl_force_catalog_mod_opt = pg_txn_manager_->GetDdlForceCatalogModification();
  auto processor =
      [this,
       is_ddl = ddl_force_catalog_mod_opt.has_value(),
       force_catalog_modification = is_major_pg_version_upgrade_ ||
                                    ddl_force_catalog_mod_opt.value_or(false) ||
                                    YBIsMajorUpgradeInitDb(),
       group_session_type, &runner, non_ddl_txn_for_sys_tables_allowed](const auto& table_op) {
        DCHECK(!table_op.IsEmpty());
        const auto& table = *table_op.table;
        const auto& op = *table_op.operation;
        const auto session_type = VERIFY_RESULT(GetRequiredSessionType(
            *pg_txn_manager_, table, *op, non_ddl_txn_for_sys_tables_allowed));
        RSTATUS_DCHECK_EQ(
            session_type, group_session_type,
            IllegalState, "Operations on different sessions can't be mixed");
        const bool is_ysql_catalog_table =
            table.schema().table_properties().is_ysql_catalog_table();
        if (force_catalog_modification && is_ysql_catalog_table) {
          ApplyForceCatalogModification(*op);
        }
        // We can have a DDL event trigger that writes to a user table instead of ysql
        // catalog table. The DDL itself may be a no-op (e.g., GRANT a privilege to a
        // user that already has that privilege). We do not want to account this case
        // as writing to ysql catalog so we can avoid incrementing the catalog version.
        has_catalog_write_ops_in_ddl_mode_ =
            has_catalog_write_ops_in_ddl_mode_ ||
            (is_ddl && op->is_write() && is_ysql_catalog_table);
        return runner.Apply(table, op);
    };
  RETURN_NOT_OK(processor(first_table_op));
  for (; !table_op.IsEmpty(); table_op = generator()) {
    RETURN_NOT_OK(processor(table_op));
  }
  return runner.Flush(std::move(cache_options));
}

Result<PerformFuture> PgSession::RunAsync(
    std::span<const PgsqlOpPtr> ops, const PgTableDesc& table, const RunOptions& options) {
  const auto generator = [i = ops.begin(), end = ops.end(), t = &table]() mutable {
      using TO = TableOperation<PgsqlOpPtr>;
      return i != end ? TO{.operation = &*i++, .table = t} : TO();
  };
  return DoRunAsync(make_lw_function(generator), options);
}

Result<PerformFuture> PgSession::RunAsync(
    const OperationGenerator& generator, const RunOptions& options) {
  return DoRunAsync(generator, options);
}

Result<PerformFuture> PgSession::RunAsync(
    const ReadOperationGenerator& generator, std::optional<CacheOptions>&& cache_options) {
  if (cache_options) {
    RSTATUS_DCHECK(!cache_options->key_value.empty(), InvalidArgument, "Cache key can't be empty");
    // Ensure no buffered requests will be added to cached request.
    YbcFlushDebugContext debug_context;
    debug_context.reason = YbcFlushReason::YB_CATALOG_TABLE_PREFETCH;
    debug_context.strarg1 = cache_options->key_value.c_str();
    RETURN_NOT_OK(buffer_.Flush(debug_context));
  }
  return DoRunAsync(generator, {}, std::move(cache_options));
}

Result<bool> PgSession::CheckIfPitrActive() {
  return pg_client_.CheckIfPitrActive();
}

Result<bool> PgSession::IsObjectPartOfXRepl(const PgObjectId& table_id) {
  return pg_client_.IsObjectPartOfXRepl(table_id);
}

Result<TableKeyRanges> PgSession::GetTableKeyRanges(
    const PgObjectId& table_id, Slice lower_bound_key, Slice upper_bound_key,
    uint64_t max_num_ranges, uint64_t range_size_bytes, bool is_forward, uint32_t max_key_length) {
  // TODO(ysql_parallel_query): consider async population of range boundaries to avoid blocking
  // calling worker on waiting for range boundaries.
  return pg_client_.GetTableKeyRanges(
      table_id, lower_bound_key, upper_bound_key, max_num_ranges, range_size_bytes, is_forward,
      max_key_length);
}

Result<tserver::PgListReplicationSlotsResponsePB> PgSession::ListReplicationSlots() {
  return pg_client_.ListReplicationSlots();
}

Result<tserver::PgGetReplicationSlotResponsePB> PgSession::GetReplicationSlot(
    const ReplicationSlotName& slot_name) {
  return pg_client_.GetReplicationSlot(slot_name);
}

PgWaitEventWatcher PgSession::StartWaitEvent(ash::WaitStateCode wait_event) {
  DCHECK_NE(wait_event, ash::WaitStateCode::kWaitingOnTServer);
  return wait_event_watcher_(wait_event, ash::PggateRPC::kNoRPC);
}

Result<tserver::PgYCQLStatementStatsResponsePB> PgSession::YCQLStatementStats() {
  return pg_client_.YCQLStatementStats();
}

Result<tserver::PgActiveSessionHistoryResponsePB> PgSession::ActiveSessionHistory() {
  return pg_client_.ActiveSessionHistory();
}

const std::string PgSession::LogPrefix() const {
  return Format("Session id $0: ", pg_client_.SessionID());
}

Result<yb::tserver::PgTabletsMetadataResponsePB> PgSession::TabletsMetadata() {
  return pg_client_.TabletsMetadata();
}

Result<yb::tserver::PgServersMetricsResponsePB> PgSession::ServersMetrics() {
  return pg_client_.ServersMetrics();
}

Status PgSession::SetCronLastMinute(int64_t last_minute) {
  return pg_client_.SetCronLastMinute(last_minute);
}

Result<int64_t> PgSession::GetCronLastMinute() { return pg_client_.GetCronLastMinute(); }

Status PgSession::AcquireAdvisoryLock(
    const YbcAdvisoryLockId& lock_id, YbcAdvisoryLockMode mode, bool wait, bool session) {

  tserver::PgAcquireAdvisoryLockRequestPB req;
  AdvisoryLockRequestInitCommon(req, pg_client_.SessionID(), lock_id, mode);
  req.set_wait(wait);
  if (session) {
    req.set_session(session);
  }
  // Populate the options even in case session level lock requests, as it would set relevant
  // state on pg_client_session necessary for retries on statement rollback.
  auto& options = *req.mutable_options();
  // If isolation level is READ_COMMITTED, set priority of the transaction to kHighestPriority.
  RETURN_NOT_OK(pg_txn_manager_->CalculateIsolation(
    false /* read_only */,
    pg_txn_manager_->GetTxnPriorityRequirement(RowMarkType::ROW_MARK_ABSENT)));
  RETURN_NOT_OK(pg_txn_manager_->SetupPerformOptions(&options));
  // TODO(advisory-lock): Fully validate that the optimization of local txn will not be applied,
  // then it should be safe to skip set_force_global_transaction.
  options.set_force_global_transaction(true);
  return pg_client_.AcquireAdvisoryLock(&req, CoarseTimePoint());
}

Status PgSession::ReleaseAdvisoryLock(const YbcAdvisoryLockId& lock_id, YbcAdvisoryLockMode mode) {
  // ReleaseAdvisoryLock is only used for session level advisory locks, hence no need to populate
  // the req with txn meta.
  tserver::PgReleaseAdvisoryLockRequestPB req;
  AdvisoryLockRequestInitCommon(req, pg_client_.SessionID(), lock_id, mode);
  return pg_client_.ReleaseAdvisoryLock(&req, CoarseTimePoint());
}

Status PgSession::ReleaseAllAdvisoryLocks(uint32_t db_oid) {
  tserver::PgReleaseAdvisoryLockRequestPB req;
  req.set_session_id(pg_client_.SessionID());
  req.set_db_oid(db_oid);
  return pg_client_.ReleaseAdvisoryLock(&req, CoarseTimePoint());
}

}  // namespace yb::pggate
