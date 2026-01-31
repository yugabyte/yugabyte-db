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

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "yb/client/tablet_server.h"

#include "yb/common/pg_types.h"
#include "yb/common/transaction.h"

#include "yb/dockv/key_bytes.h"
#include "yb/dockv/doc_key.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/ref_counted.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/server/hybrid_clock.h"

#include "yb/tserver/tserver_util_fwd.h"

#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_fwd.h"
#include "yb/util/uuid.h"

#include "yb/yql/pggate/pg_client.h"
#include "yb/yql/pggate/pg_explicit_row_lock_buffer.h"
#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_fk_reference_cache.h"
#include "yb/yql/pggate/pg_function.h"
#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/pg_session_fwd.h"
#include "yb/yql/pggate/pg_setup_perform_options_accessor_tag.h"
#include "yb/yql/pggate/pg_statement.h"
#include "yb/yql/pggate/pg_sys_table_prefetcher.h"
#include "yb/yql/pggate/pg_tools.h"
#include "yb/yql/pggate/pg_type.h"
#include "yb/yql/pggate/pg_txn_manager.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"
#include "yb/yql/pggate/ybc_pggate.h"

namespace yb::pggate {

class PgDmlRead;
class PgFlushDebugContext;

struct PgMemctxComparator {
  using is_transparent = void;

  bool operator()(PgMemctx* l, PgMemctx* r) const {
    return l == r;
  }

  template<class T1, class T2>
  bool operator()(const T1& l, const T2& r) const {
    return (*this)(GetPgMemctxPtr(l), GetPgMemctxPtr(r));
  }

 private:
  static PgMemctx* GetPgMemctxPtr(PgMemctx* value) {
    return value;
  }

  static PgMemctx* GetPgMemctxPtr(const std::unique_ptr<PgMemctx>& value) {
    return value.get();
  }
};

struct PgMemctxHasher {
  using is_transparent = void;

  size_t operator()(const std::unique_ptr<PgMemctx>& value) const;
  size_t operator()(PgMemctx* value) const;
};

struct PgDdlCommitInfo {
  uint32_t db_oid;
  bool is_silent_altering;
};

//--------------------------------------------------------------------------------------------------
// Implements support for CAPI.
class PgApiImpl {
 public:
  struct MessengerHolder {
    std::unique_ptr<rpc::SecureContext> security_context;
    std::unique_ptr<rpc::Messenger> messenger;

    MessengerHolder(
        std::unique_ptr<rpc::SecureContext>&& security_context_,
        std::unique_ptr<rpc::Messenger>&& messenger_);
    MessengerHolder(MessengerHolder&& rhs);
    ~MessengerHolder();
  };

  ~PgApiImpl();

  const YbcPgCallbacks* pg_callbacks() const { return &pg_callbacks_; }

  // Interrupt aborts all pending RPCs immediately to unblock main thread.
  void Interrupt();

  void ResetCatalogReadTime();
  [[nodiscard]] ReadHybridTime GetCatalogReadTime() const;

  uint64_t GetSessionID() const { return pg_client_.SessionID(); }

  PgMemctx *CreateMemctx();
  Status DestroyMemctx(PgMemctx *memctx);
  Status ResetMemctx(PgMemctx *memctx);

  // Cache statements in YB Memctx. When Memctx is destroyed, the statement is destructed.
  Status AddToCurrentPgMemctx(std::unique_ptr<PgStatement> stmt,
                              PgStatement **handle);

  // Cache function calls in YB Memctx. When Memctx is destroyed, the function is destructed.
  Status AddToCurrentPgMemctx(std::unique_ptr<PgFunction> func, PgFunction **handle);

  // Cache table descriptor in YB Memctx. When Memctx is destroyed, the descriptor is destructed.
  Status AddToCurrentPgMemctx(size_t table_desc_id,
                              const PgTableDescPtr &table_desc);
  // Read table descriptor that was cached in YB Memctx.
  Status GetTabledescFromCurrentPgMemctx(size_t table_desc_id, PgTableDesc **handle);

  // Invalidate the sessions table cache.
  Status InvalidateCache(uint64_t min_ysql_catalog_version);

  // Update the table cache's min_ysql_catalog_version.
  Status UpdateTableCacheMinVersion(uint64_t min_ysql_catalog_version);

  // Get the gflag TEST_ysql_disable_transparent_cache_refresh_retry.
  bool GetDisableTransparentCacheRefreshRetry();

  Result<bool> IsInitDbDone();

  Result<uint64_t> GetSharedCatalogVersion(std::optional<PgOid> db_oid = std::nullopt);
  Result<uint32_t> GetNumberOfDatabases();
  Result<bool> CatalogVersionTableInPerdbMode();
  Result<tserver::PgGetTserverCatalogMessageListsResponsePB> GetTserverCatalogMessageLists(
      uint32_t db_oid, uint64_t ysql_catalog_version, uint32_t num_catalog_versions);
  Result<tserver::PgSetTserverCatalogMessageListResponsePB> SetTserverCatalogMessageList(
      uint32_t db_oid, bool is_breaking_change,
      uint64_t new_catalog_version, const YbcCatalogMessageList *message_list);

  Status GetYbSystemTableInfo(
      PgOid namespace_oid, std::string_view table_name, PgOid* oid, PgOid* relfilenode);
  uint64_t GetSharedAuthKey() const;
  const unsigned char *GetLocalTserverUuid() const;
  pid_t GetLocalTServerPid() const;
  Result<int> GetXClusterRole(uint32_t db_oid);

  Status NewTupleExpr(
    YbcPgStatement stmt, const YbcPgTypeEntity *tuple_type_entity,
    const YbcPgTypeAttrs *type_attrs, int num_elems,
    const YbcPgExpr *elems, YbcPgExpr *expr_handle);

  // Setup the table to store sequences data.
  Status CreateSequencesDataTable();

  Status InsertSequenceTuple(int64_t db_oid,
                             int64_t seq_oid,
                             uint64_t ysql_catalog_version,
                             bool is_db_catalog_version_mode,
                             int64_t last_val,
                             bool is_called);

  Status UpdateSequenceTupleConditionally(int64_t db_oid,
                                          int64_t seq_oid,
                                          uint64_t ysql_catalog_version,
                                          bool is_db_catalog_version_mode,
                                          int64_t last_val,
                                          bool is_called,
                                          int64_t expected_last_val,
                                          bool expected_is_called,
                                          bool *skipped);

  Status UpdateSequenceTuple(int64_t db_oid,
                             int64_t seq_oid,
                             uint64_t ysql_catalog_version,
                             bool is_db_catalog_version_mode,
                             int64_t last_val,
                             bool is_called,
                             bool* skipped);

  Status FetchSequenceTuple(int64_t db_oid,
                            int64_t seq_oid,
                            uint64_t ysql_catalog_version,
                            bool is_db_catalog_version_mode,
                            uint32_t fetch_count,
                            int64_t inc_by,
                            int64_t min_value,
                            int64_t max_value,
                            bool cycle,
                            int64_t *first_value,
                            int64_t *last_value);

  Status ReadSequenceTuple(int64_t db_oid,
                           int64_t seq_oid,
                           uint64_t ysql_catalog_version,
                           bool is_db_catalog_version_mode,
                           int64_t *last_val,
                           bool *is_called);

  void DeleteStatement(PgStatement *handle);

  const PgTypeInfo& pg_types() const { return pg_types_; }

  //------------------------------------------------------------------------------------------------
  // Determine whether the given database is colocated.
  Status IsDatabaseColocated(const PgOid database_oid, bool *colocated,
                             bool *legacy_colocated_database);

  // Create database.
  Status NewCreateDatabase(const char *database_name,
                           PgOid database_oid,
                           PgOid source_database_oid,
                           PgOid next_oid,
                           const bool colocated,
                           YbcCloneInfo *yb_clone_info,
                           PgStatement **handle);
  Status ExecCreateDatabase(PgStatement *handle);

  // Drop database.
  Status NewDropDatabase(const char *database_name,
                         PgOid database_oid,
                         PgStatement **handle);
  Status ExecDropDatabase(PgStatement *handle);

  // Alter database.
  Status NewAlterDatabase(const char *database_name,
                                 PgOid database_oid,
                                 PgStatement **handle);
  Status AlterDatabaseRenameDatabase(PgStatement *handle, const char *newname);
  Status ExecAlterDatabase(PgStatement *handle);

  // Reserve oids.
  Status ReserveOids(PgOid database_oid,
                     PgOid next_oid,
                     uint32_t count,
                     PgOid *begin_oid,
                     PgOid *end_oid);

  // Allocate a new object id from the oid allocator of database db_oid.
  Status GetNewObjectId(PgOid db_oid, PgOid *new_oid);

  Status GetCatalogMasterVersion(uint64_t *version);

  Status CancelTransaction(const unsigned char* transaction_id);

  // Load table.
  Result<PgTableDescPtr> LoadTable(const PgObjectId& table_id);

  // Invalidate the cache entry corresponding to table_id from the PgSession table cache.
  void InvalidateTableCache(const PgObjectId& table_id);
  void RemoveTableCacheEntry(const PgObjectId& table_id);

  //------------------------------------------------------------------------------------------------
  // Create and drop tablegroup.

  Status NewCreateTablegroup(const char *database_name,
                             const PgOid database_oid,
                             const PgOid tablegroup_oid,
                             const PgOid tablespace_oid,
                             PgStatement **handle);

  Status ExecCreateTablegroup(PgStatement *handle);

  Status NewDropTablegroup(const PgOid database_oid,
                           const PgOid tablegroup_oid,
                           PgStatement **handle);

  Status ExecDropTablegroup(PgStatement *handle);

  //------------------------------------------------------------------------------------------------
  // Create, alter and drop table.
  Status NewCreateTable(const char *database_name,
                        const char *schema_name,
                        const char *table_name,
                        const PgObjectId& table_id,
                        bool is_shared_table,
                        bool is_sys_catalog_table,
                        bool if_not_exist,
                        YbcPgYbrowidMode ybrowid_mode,
                        bool is_colocated_via_database,
                        const PgObjectId& tablegroup_oid,
                        const ColocationId colocation_id,
                        const PgObjectId& tablespace_oid,
                        bool is_matview,
                        const PgObjectId& pg_table_oid,
                        const PgObjectId& old_relfilenode_oid,
                        bool is_truncate,
                        PgStatement **handle);

  Status CreateTableAddColumn(PgStatement *handle, const char *attr_name, int attr_num,
                              const YbcPgTypeEntity *attr_type, bool is_hash,
                              bool is_range, bool is_desc, bool is_nulls_first);

  Status CreateTableSetNumTablets(PgStatement *handle, int32_t num_tablets);

  Status AddSplitBoundary(PgStatement *handle, PgExpr **exprs, int expr_count);

  Status ExecCreateTable(PgStatement *handle);

  Status NewAlterTable(const PgObjectId& table_id,
                       PgStatement **handle);

  Status AlterTableAddColumn(PgStatement *handle, const char *name,
                             int order, const YbcPgTypeEntity *attr_type,
                             YbcPgExpr missing_value);

  Status AlterTableRenameColumn(PgStatement *handle, const char *oldname,
                                const char *newname);

  Status AlterTableDropColumn(PgStatement *handle, const char *name);

  Status AlterTableSetReplicaIdentity(PgStatement *handle, const char identity_type);

  Status AlterTableRenameTable(PgStatement *handle, const char *newname);

  Status AlterTableIncrementSchemaVersion(PgStatement *handle);

  Status AlterTableSetTableId(PgStatement* handle, const PgObjectId& table_id);

  Status AlterTableSetSchema(PgStatement *handle, const char *schema_name);

  Status ExecAlterTable(PgStatement *handle);

  Status AlterTableInvalidateTableCacheEntry(PgStatement *handle);

  Status NewDropTable(const PgObjectId& table_id,
                      bool if_exist,
                      PgStatement **handle);

  Status NewTruncateTable(const PgObjectId& table_id,
                          PgStatement **handle);

  Status ExecTruncateTable(PgStatement *handle);

  Status GetTableDesc(const PgObjectId& table_id,
                      PgTableDesc **handle);

  Result<tserver::PgListClonesResponsePB> GetDatabaseClones();

  Result<YbcPgColumnInfo> GetColumnInfo(YbcPgTableDesc table_desc,
                                        int16_t attr_number);

  Result<bool> DmlModifiesRow(PgStatement* handle);

  Status SetIsSysCatalogVersionChange(PgStatement *handle);

  Status SetCatalogCacheVersion(
      PgStatement *handle, uint64_t version, std::optional<PgOid> db_oid = std::nullopt);

  Status SetTablespaceOid(PgStatement *handle, uint32_t tablespace_oid);

  Result<client::TableSizeInfo> GetTableDiskSize(const PgObjectId& table_oid);

  //------------------------------------------------------------------------------------------------
  // Create and drop index.
  Status NewCreateIndex(const char *database_name,
                        const char *schema_name,
                        const char *index_name,
                        const PgObjectId& index_id,
                        const PgObjectId& table_id,
                        bool is_shared_index,
                        bool is_sys_catalog_index,
                        bool is_unique_index,
                        const bool skip_index_backfill,
                        bool if_not_exist,
                        bool is_colocated_via_database,
                        const PgObjectId& tablegroup_oid,
                        const YbcPgOid& colocation_id,
                        const PgObjectId& tablespace_oid,
                        const PgObjectId& pg_table_oid,
                        const PgObjectId& old_relfilenode_oid,
                        PgStatement **handle);

  Status CreateIndexAddColumn(PgStatement *handle, const char *attr_name, int attr_num,
                              const YbcPgTypeEntity *attr_type, bool is_hash,
                              bool is_range, bool is_desc, bool is_nulls_first);

  Status CreateIndexSetNumTablets(PgStatement *handle, int32_t num_tablets);

  Status CreateIndexSetVectorOptions(PgStatement *handle, YbcPgVectorIdxOptions *options);

  Status CreateIndexSetHnswOptions(PgStatement *handle, int m, int m0, int ef_construction);

  Status CreateIndexAddSplitRow(PgStatement *handle, int num_cols,
                                YbcPgTypeEntity **types, uint64_t *data);

  Status ExecCreateIndex(PgStatement *handle);

  Status NewDropIndex(const PgObjectId& index_id,
                      bool if_exist,
                      bool ddl_rollback_enabled,
                      PgStatement **handle);

  Status ExecPostponedDdlStmt(PgStatement *handle);

  Status ExecDropTable(PgStatement *handle);

  Status ExecDropIndex(PgStatement *handle);

  Result<int> WaitForBackendsCatalogVersion(PgOid dboid, uint64_t version, pid_t pid);

  Status BackfillIndex(const PgObjectId& table_id);
  Status WaitVectorIndexReady(const PgObjectId& table_id);

  Status NewDropSequence(const YbcPgOid database_oid,
                         const YbcPgOid sequence_oid,
                         PgStatement **handle);

  Status ExecDropSequence(PgStatement *handle);

  Status NewDropDBSequences(const YbcPgOid database_oid,
                            PgStatement **handle);

  //------------------------------------------------------------------------------------------------
  // All DML statements
  Status DmlAppendTarget(PgStatement *handle, PgExpr *expr, bool is_for_secondary_index);

  Status DmlAppendQual(
      PgStatement *handle, PgExpr *expr, uint32_t serialization_version,
      bool is_for_secondary_index);

  Status DmlAppendColumnRef(PgStatement *handle, PgColumnRef *colref, bool is_for_secondary_index);

  // Binding Columns: Bind column with a value (expression) in a statement.
  // + This API is used to identify the rows you want to operate on. If binding columns are not
  //   there, that means you want to operate on all rows (full scan). You can view this as a
  //   a definitions of an initial rowset or an optimization over full-scan.
  //
  // + There are some restrictions on when BindColumn() can be used.
  //   Case 1: INSERT INTO tab(x) VALUES(x_expr)
  //   - BindColumn() can be used for BOTH primary-key and regular columns.
  //   - This bind-column function is used to bind "x" with "x_expr", and "x_expr" that can contain
  //     bind-variables (placeholders) and constants whose values can be updated for each execution
  //     of the same allocated statement.
  //
  //   Case 2: SELECT / UPDATE / DELETE <WHERE key = "key_expr">
  //   - BindColumn() can only be used for primary-key columns.
  //   - This bind-column function is used to bind the primary column "key" with "key_expr" that can
  //     contain bind-variables (placeholders) and constants whose values can be updated for each
  //     execution of the same allocated statement.
  Status DmlBindColumn(YbcPgStatement handle, int attr_num, YbcPgExpr attr_value);
  Status DmlBindColumnCondBetween(YbcPgStatement handle,
                                  int attr_num,
                                  PgExpr *attr_value,
                                  bool start_inclusive,
                                  PgExpr *attr_value_end,
                                  bool end_inclusive);
  Status DmlBindColumnCondIn(YbcPgStatement handle,
                             YbcPgExpr lhs,
                             int n_attr_values,
                             YbcPgExpr *attr_values);
  Status DmlBindColumnCondIsNotNull(PgStatement *handle, int attr_num);
  Status DmlBindRow(YbcPgStatement handle, uint64_t ybctid, YbcBindColumn* columns, int count);

  Status DmlBindHashCode(
      PgStatement* handle, const std::optional<Bound>& start, const std::optional<Bound>& end);

  Status DmlBindRange(YbcPgStatement handle,
                      Slice lower_bound,
                      bool lower_bound_inclusive,
                      Slice upper_bound,
                      bool upper_bound_inclusive);

  Status DmlBindBounds(PgStatement* handle,
                       const Slice lower_bound,
                       bool lower_bound_inclusive,
                       const Slice upper_bound,
                       bool upper_bound_inclusive);

  Status DmlAddRowUpperBound(YbcPgStatement handle,
                             int n_col_values,
                             YbcPgExpr *col_values,
                             bool is_inclusive);

  Status DmlAddRowLowerBound(YbcPgStatement handle,
                             int n_col_values,
                             YbcPgExpr *col_values,
                             bool is_inclusive);

  Status DmlSetMergeSortKeys(YbcPgStatement handle, int num_keys, const YbcSortKey *sort_keys);

  // Binding Tables: Bind the whole table in a statement.  Do not use with BindColumn.
  Status DmlBindTable(YbcPgStatement handle);

  // Utility method to get the info for column 'attr_num'.
  Result<YbcPgColumnInfo> DmlGetColumnInfo(YbcPgStatement handle, int attr_num);

  // API for SET clause.
  Status DmlAssignColumn(YbcPgStatement handle, int attr_num, YbcPgExpr attr_value);

  // This function is to fetch the targets in YBCPgDmlAppendTarget() from the rows that were defined
  // by YBCPgDmlBindColumn().
  Status DmlFetch(PgStatement *handle, int32_t natts, uint64_t *values, bool *isnulls,
                  YbcPgSysColumns *syscols, bool *has_data);

  // Utility method that checks stmt type and calls exec insert, update, or delete internally.
  Status DmlExecWriteOp(PgStatement *handle, int32_t *rows_affected_count);

  // This function adds a primary column to be used in the construction of the tuple id (ybctid).
  Status DmlAddYBTupleIdColumn(PgStatement *handle, int attr_num, uint64_t datum,
                               bool is_null, const YbcPgTypeEntity *type_entity);

  Result<dockv::KeyBytes> BuildTupleId(const YbcPgYBTupleIdDescriptor& descr);

  // DB Operations: SET, WHERE, ORDER_BY, GROUP_BY, etc.
  // + The following operations are run by DocDB.
  //   - API for "set_clause" (not yet implemented).
  //
  // + The following operations are run by Postgres layer. An API might be added to move these
  //   operations to DocDB.
  //   - API for "where_expr"
  //   - API for "order_by_expr"
  //   - API for "group_by_expr"

  // Buffer write operations.
  Status StartOperationsBuffering();
  Status StopOperationsBuffering();
  void ResetOperationsBuffering();
  Status FlushBufferedOperations(const PgFlushDebugContext& dbg_ctx);
  Status AdjustOperationsBuffering(int multiple = 1);

  //------------------------------------------------------------------------------------------------
  // Insert.
  Result<PgStatement*> NewInsertBlock(
      const PgObjectId& table_id,
      const YbcPgTableLocalityInfo& locality_info,
      YbcPgTransactionSetting transaction_setting);

  Status NewInsert(const PgObjectId& table_id,
                   const YbcPgTableLocalityInfo& locality_info,
                   PgStatement **handle,
                   YbcPgTransactionSetting transaction_setting =
                       YbcPgTransactionSetting::YB_TRANSACTIONAL);

  Status ExecInsert(PgStatement *handle);

  Status InsertStmtSetUpsertMode(PgStatement *handle);

  Status InsertStmtSetWriteTime(PgStatement *handle, const HybridTime write_time);

  Status InsertStmtSetIsBackfill(PgStatement *handle, const bool is_backfill);

  //------------------------------------------------------------------------------------------------
  // Update.
  Status NewUpdate(const PgObjectId& table_id,
                   const YbcPgTableLocalityInfo& locality_info,
                   PgStatement **handle,
                   YbcPgTransactionSetting transaction_setting =
                       YbcPgTransactionSetting::YB_TRANSACTIONAL);

  Status ExecUpdate(PgStatement *handle);

  //------------------------------------------------------------------------------------------------
  // Delete.
  Status NewDelete(const PgObjectId& table_id,
                   const YbcPgTableLocalityInfo& locality_info,
                   PgStatement **handle,
                   YbcPgTransactionSetting transaction_setting =
                       YbcPgTransactionSetting::YB_TRANSACTIONAL);

  Status ExecDelete(PgStatement *handle);

  Status DeleteStmtSetIsPersistNeeded(PgStatement *handle, const bool is_persist_needed);

  //------------------------------------------------------------------------------------------------
  // Colocated Truncate.
  Status NewTruncateColocated(const PgObjectId& table_id,
                              const YbcPgTableLocalityInfo& locality_info,
                              PgStatement **handle,
                              YbcPgTransactionSetting transaction_setting =
                                  YbcPgTransactionSetting::YB_TRANSACTIONAL);

  Status ExecTruncateColocated(PgStatement *handle);

  //------------------------------------------------------------------------------------------------
  // Select.
  Status NewSelect(
      const PgObjectId& table_id, const PgObjectId& index_id,
      const YbcPgPrepareParameters* prepare_params, const YbcPgTableLocalityInfo& locality_info,
      PgStatement** handle);

  Status SetForwardScan(PgStatement *handle, bool is_forward_scan);

  Status SetDistinctPrefixLength(PgStatement *handle, int distinct_prefix_length);

  Status ExecSelect(PgStatement *handle, const YbcPgExecParameters *exec_params);
  Result<bool> RetrieveYbctids(
      PgStatement *handle, const YbcPgExecParameters *exec_params, int natts,
      YbcSliceVector *ybctids, size_t *count);
  Status FetchRequestedYbctids(PgStatement *handle, const YbcPgExecParameters *exec_params,
                               YbcConstSliceVector ybctids);

  Status BindYbctids(PgStatement* handle, int n, uintptr_t* ybctids);

  bool IsValidYbctid(uint64_t ybctid);

  Status DmlANNBindVector(PgStatement *handle, PgExpr *vector);

  Status DmlANNSetPrefetchSize(PgStatement *handle, int prefetch_size);

  Status DmlHnswSetReadOptions(PgStatement *handle, int ef_search);

  void IncrementIndexRecheckCount();

  //------------------------------------------------------------------------------------------------
  // Functions.

  Status NewSRF(PgFunction **handle, PgFunctionDataProcessor processor);

  Status AddFunctionParam(
      PgFunction *handle, const std::string& name, const YbcPgTypeEntity *type_entity,
      uint64_t datum, bool is_null);

  Status AddFunctionTarget(
      PgFunction *handle, const std::string& name, const YbcPgTypeEntity *type_entity,
      const YbcPgTypeAttrs type_attrs);

  Status FinalizeFunctionTargets(PgFunction *handle);

  Status SRFGetNext(PgFunction *handle, uint64_t *values, bool *is_nulls, bool *has_data);

  Status NewGetLockStatusDataSRF(PgFunction **handle);

  //------------------------------------------------------------------------------------------------
  // Analyze.
  Status NewSample(
      const PgObjectId& table_id, const YbcPgTableLocalityInfo& locality_info, int targrows,
      const SampleRandomState& rand_state, PgStatement **handle);

  Result<bool> SampleNextBlock(PgStatement* handle);

  Status ExecSample(PgStatement *handle, YbcPgExecParameters* exec_params);

  Result<EstimatedRowCount> GetEstimatedRowCount(PgStatement* handle);

  //------------------------------------------------------------------------------------------------
  // Transaction control.
  Status BeginTransaction(int64_t start_time);
  Status RecreateTransaction();
  Status RestartTransaction();
  Status ResetTransactionReadPoint(bool is_catalog_snapshot);
  Status EnsureReadPoint();
  Status RestartReadPoint();
  bool IsRestartReadPointRequested();
  Status CommitPlainTransaction(
        const std::optional<PgDdlCommitInfo>& ddl_commit_info = std::nullopt);
  Status AbortPlainTransaction();
  Status SetTransactionIsolationLevel(int isolation);
  Status SetTransactionReadOnly(bool read_only);
  Status SetTransactionDeferrable(bool deferrable);
  Status SetInTxnBlock(bool in_txn_blk);
  Status SetReadOnlyStmt(bool read_only_stmt);
  Status SetEnableTracing(bool tracing);
  Status UpdateFollowerReadsConfig(bool enable_follower_reads, int32_t staleness_ms);
  Status SetDdlStateInPlainTransaction();
  Status EnterSeparateDdlTxnMode();
  bool HasWriteOperationsInDdlTxnMode() const;
  Status ExitSeparateDdlTxnMode(PgOid db_oid, bool is_silent_modification);
  Status ClearSeparateDdlTxnMode();
  Status SetActiveSubTransaction(SubTransactionId id);
  Status RollbackToSubTransaction(SubTransactionId id);
  double GetTransactionPriority() const;
  YbcTxnPriorityRequirement GetTransactionPriorityType() const;
  Result<Uuid> GetActiveTransaction() const;
  Status GetActiveTransactions(YbcPgSessionTxnInfo* infos, size_t num_infos);
  bool IsDdlMode() const;
  bool IsDdlModeWithRegularTransactionBlock() const;
  Result<bool> CurrentTransactionUsesFastPath() const;

  //------------------------------------------------------------------------------------------------
  // Expressions.
  // Column reference.
  Status NewColumnRef(
      PgStatement *handle, int attr_num, const YbcPgTypeEntity *type_entity,
      bool collate_is_valid_non_c, const YbcPgTypeAttrs *type_attrs, PgExpr **expr_handle);

  // Constant expressions.
  Status NewConstant(
      YbcPgStatement stmt, const YbcPgTypeEntity *type_entity, bool collate_is_valid_non_c,
      const char *collation_sortkey, uint64_t datum, bool is_null, YbcPgExpr *expr_handle);
  Status NewConstantVirtual(
      YbcPgStatement stmt, const YbcPgTypeEntity *type_entity,
      YbcPgDatumKind datum_kind, YbcPgExpr *expr_handle);
  Status NewConstantOp(
      YbcPgStatement stmt, const YbcPgTypeEntity *type_entity, bool collate_is_valid_non_c,
      const char *collation_sortkey, uint64_t datum, bool is_null, YbcPgExpr *expr_handle,
      bool is_gt);

  // TODO(neil) UpdateConstant should be merged into one.
  // Update constant.
  template<typename value_type>
  Status UpdateConstant(PgExpr *expr, value_type value, bool is_null) {
    if (expr->opcode() != PgExpr::Opcode::PG_EXPR_CONSTANT) {
      // Invalid handle.
      return STATUS(InvalidArgument, "Invalid expression handle for constant");
    }
    down_cast<PgConstant*>(expr)->UpdateConstant(value, is_null);
    return Status::OK();
  }
  Status UpdateConstant(PgExpr *expr, const char *value, bool is_null);
  Status UpdateConstant(PgExpr *expr, const void *value, int64_t bytes, bool is_null);

  // Operators.
  Status NewOperator(
      PgStatement *stmt, const char *opname, const YbcPgTypeEntity *type_entity,
      bool collate_is_valid_non_c, PgExpr **op_handle);
  Status OperatorAppendArg(PgExpr *op_handle, PgExpr *arg);

  // Foreign key reference caching.
  void DeleteForeignKeyReference(PgOid table_id, const Slice& ybctid);
  void AddForeignKeyReference(PgOid table_id, const Slice& ybctid);
  Result<bool> ForeignKeyReferenceExists(
      const PgObjectId& table_id, const Slice& ybctid, YbcPgTableLocalityInfo locality_info);
  Status AddForeignKeyReferenceIntent(
    const PgObjectId& table_id, const Slice& ybctid,
    const PgFKReferenceCache::IntentOptions& options);
  void NotifyDeferredTriggersProcessingStarted();

  Status AddExplicitRowLockIntent(
      const PgObjectId& table_id, const Slice& ybctid,
      const YbcPgExplicitRowLockParams& params, const YbcPgTableLocalityInfo& locality_info,
      YbcPgExplicitRowLockErrorInfo& error_info);
  Status FlushExplicitRowLockIntents(YbcPgExplicitRowLockErrorInfo& error_info);

  // INSERT ... ON CONFLICT batching ---------------------------------------------------------------
  Status AddInsertOnConflictKey(
      PgOid table_id, const Slice& ybctid, void* state, const YbcPgInsertOnConflictKeyInfo& info);
  YbcPgInsertOnConflictKeyState InsertOnConflictKeyExists(
      PgOid table_id, const Slice& ybctid, void* state);
  Result<YbcPgInsertOnConflictKeyInfo> DeleteInsertOnConflictKey(
      PgOid table_id, const Slice& ybctid, void* state);
  Result<YbcPgInsertOnConflictKeyInfo> DeleteNextInsertOnConflictKey(void* state);
  uint64_t GetInsertOnConflictKeyCount(void* state);
  void AddInsertOnConflictKeyIntent(PgOid table_id, const Slice& ybctid);
  void ClearAllInsertOnConflictCaches();
  void ClearInsertOnConflictCache(void* state);
  //------------------------------------------------------------------------------------------------

  // Sets the specified timeout in the rpc service.
  void SetTimeout(int timeout_ms);
  void ClearTimeout();

  void SetLockTimeout(int lock_timeout_ms);

  Result<yb::tserver::PgGetLockStatusResponsePB> GetLockStatusData(
      const std::string &table_id, const std::string &transaction_id);
  Result<client::TabletServersInfo> ListTabletServers();

  Status GetIndexBackfillProgress(std::vector<PgObjectId> oids,
                                  uint64_t* num_rows_read_from_table,
                                  double* num_rows_backfilled);

  void StartSysTablePrefetching(const PrefetcherOptions& options);
  void StopSysTablePrefetching();
  void PauseSysTablePrefetching();
  void ResumeSysTablePrefetching();
  bool IsSysTablePrefetchingStarted() const;
  void RegisterSysTableForPrefetching(
      const PgObjectId& table_id, const PgObjectId& index_id, int row_oid_filtering_attr,
      bool fetch_ybctid);
  Status PrefetchRegisteredSysTables();

  //------------------------------------------------------------------------------------------------
  // System Validation.
  Status ValidatePlacements(
      const char *live_placement_info, const char *read_placement_info,
      bool check_satisfiable);

  Result<bool> CheckIfPitrActive();

  Result<bool> IsObjectPartOfXRepl(const PgObjectId& table_id);

  Result<TableKeyRanges> GetTableKeyRanges(
      const PgObjectId& table_id, Slice lower_bound_key, Slice upper_bound_key,
      uint64_t max_num_ranges, uint64_t range_size_bytes, bool is_forward, uint32_t max_key_length);

  MemTracker& GetMemTracker() { return *mem_tracker_; }

  MemTracker& GetRootMemTracker() { return *MemTracker::GetRootTracker(); }

  void DumpSessionState(YbcPgSessionState* session_data);

  void RestoreSessionState(const YbcPgSessionState& session_data);

  void RollbackSubTransactionScopedSessionState();
  void RollbackTransactionScopedSessionState();

  //------------------------------------------------------------------------------------------------
  // Replication Slots Functions.

  // Create Replication Slot.
  Status NewCreateReplicationSlot(const char *slot_name,
                                  const char *plugin_name,
                                  const PgOid database_oid,
                                  YbcPgReplicationSlotSnapshotAction snapshot_action,
                                  YbcLsnType lsn_type,
                                  YbcOrderingMode ordering_mode,
                                  PgStatement **handle);
  Result<tserver::PgCreateReplicationSlotResponsePB> ExecCreateReplicationSlot(
      PgStatement *handle);

  Result<tserver::PgListReplicationSlotsResponsePB> ListReplicationSlots();

  Result<tserver::PgGetReplicationSlotResponsePB> GetReplicationSlot(
      const ReplicationSlotName& slot_name);

  Result<cdc::InitVirtualWALForCDCResponsePB> InitVirtualWALForCDC(
      const std::string& stream_id, const std::vector<PgObjectId>& table_ids,
      const YbcReplicationSlotHashRange* slot_hash_range, uint64_t active_pid,
      const std::vector<PgOid>& publication_oids, bool pub_all_tables);

  Result<cdc::UpdatePublicationTableListResponsePB> UpdatePublicationTableList(
      const std::string& stream_id, const std::vector<PgObjectId>& table_ids);

  Result<cdc::DestroyVirtualWALForCDCResponsePB> DestroyVirtualWALForCDC();

  Result<cdc::GetConsistentChangesResponsePB> GetConsistentChangesForCDC(
      const std::string& stream_id);

  Result<cdc::GetLagMetricsResponsePB> GetLagMetrics(
      const std::string& stream_id, int64_t *lag_metric);

  Result<cdc::UpdateAndPersistLSNResponsePB> UpdateAndPersistLSN(
      const std::string& stream_id, YbcPgXLogRecPtr restart_lsn, YbcPgXLogRecPtr confirmed_flush);

  // Drop Replication Slot.
  Status NewDropReplicationSlot(const char *slot_name,
                                PgStatement **handle);
  Status ExecDropReplicationSlot(PgStatement *handle);

  Result<std::string> ExportSnapshot(
      const YbcPgTxnSnapshot& snapshot, std::optional<YbcReadPointHandle> explicit_read_time);
  Result<YbcPgTxnSnapshot> ImportSnapshot(std::string_view snapshot_id);

  bool HasExportedSnapshots() const;
  void ClearExportedTxnSnapshots();

  Result<tserver::PgYCQLStatementStatsResponsePB> YCQLStatementStats();
  Result<tserver::PgActiveSessionHistoryResponsePB> ActiveSessionHistory();

  Result<tserver::PgTabletsMetadataResponsePB> TabletsMetadata(bool local_only);

  Result<tserver::PgServersMetricsResponsePB> ServersMetrics();

  bool IsCronLeader() const;
  Status SetCronLastMinute(int64_t last_minute);
  Result<int64_t> GetCronLastMinute();

  [[nodiscard]] YbcReadPointHandle GetCurrentReadPoint() const;
  [[nodiscard]] YbcReadPointHandle GetMaxReadPoint() const;
  Status RestoreReadPoint(YbcReadPointHandle read_point);
  Result<YbcReadPointHandle> RegisterSnapshotReadTime(uint64_t read_time, bool use_read_time);

  void DdlEnableForceCatalogModification();

  Status TriggerRelcacheInitConnection(const std::string& dbname);

  //----------------------------------------------------------------------------------------------
  // Advisory Locks.
  //----------------------------------------------------------------------------------------------

  Status AcquireAdvisoryLock(
      const YbcAdvisoryLockId& lock_id, YbcAdvisoryLockMode mode, bool wait, bool session);
  Status ReleaseAdvisoryLock(const YbcAdvisoryLockId& lock_id, YbcAdvisoryLockMode mode);
  Status ReleaseAllAdvisoryLocks(uint32_t db_oid);

  //----------------------------------------------------------------------------------------------
  // Table Locks.
  //----------------------------------------------------------------------------------------------
  Status AcquireObjectLock(const YbcObjectLockId& lock_id, YbcObjectLockMode mode);

  auto TemporaryDisableReadTimeHistoryCutoff() {
    return pg_txn_manager_->TemporaryDisableReadTimeHistoryCutoff();
  }

  struct PgSharedData;
  struct SignedPgSharedData;

  static Result<std::unique_ptr<PgApiImpl>> Make(
      YbcPgTypeEntities type_entities, const YbcPgCallbacks& pg_callbacks,
      const YbcPgInitPostgresInfo& init_postgres_info, YbcPgAshConfig& ash_config,
      YbcPgExecStatsState& session_stats, bool is_binary_upgrade);

 private:
  PgApiImpl(
      YbcPgTypeEntities type_entities, const YbcPgCallbacks& pg_callbacks,
      const YbcPgInitPostgresInfo& init_postgres_info, YbcPgAshConfig& ash_config,
      YbcPgExecStatsState& session_stats, bool is_binary_upgrade);
  Status Init(std::optional<uint64_t> session_id);

  SetupPerformOptionsAccessorTag ClearSessionState();

  class Interrupter;

  class TupleIdBuilder {
   public:
    Result<dockv::KeyBytes> Build(PgSession* session, const YbcPgYBTupleIdDescriptor& descr);

   private:
    void Prepare();

    ThreadSafeArena arena_;
    dockv::DocKey doc_key_;
    size_t counter_ = 0;
  };

  class PgSharedDataHolder {
   public:
    PgSharedDataHolder(YbcPgSharedDataPlaceholder& raw_data, bool is_owner);
    ~PgSharedDataHolder();

    [[nodiscard]] PgSharedData* operator->();

   private:
    SignedPgSharedData* signed_data_;
    const bool is_owner_;
  };

  PgTypeInfo pg_types_;

  // Metrics.
  std::unique_ptr<MetricRegistry> metric_registry_;
  scoped_refptr<MetricEntity> metric_entity_;

  // Memory tracker.
  std::shared_ptr<MemTracker> mem_tracker_;

  MessengerHolder messenger_holder_;

  std::unique_ptr<rpc::ProxyCache> proxy_cache_;

  YbcPgCallbacks pg_callbacks_;

  const WaitEventWatcher wait_event_watcher_;

  PgSharedDataHolder pg_shared_data_;

  // TODO Rename to client_ when YBClient is removed.
  PgClient pg_client_;
  std::unique_ptr<Interrupter> interrupter_;

  scoped_refptr<server::HybridClock> clock_;

  // Local tablet-server shared memory data.
  tserver::TServerSharedData* tserver_shared_object_;

  const bool enable_table_locking_;
  scoped_refptr<PgTxnManager> pg_txn_manager_;
  std::optional<PgSysTablePrefetcher> pg_sys_table_prefetcher_;
  std::unordered_set<std::unique_ptr<PgMemctx>, PgMemctxHasher, PgMemctxComparator> mem_contexts_;
  std::optional<std::pair<PgOid, int32_t>> catalog_version_db_index_;
  // Used as a snapshot of the tserver catalog version map prior to MyDatabaseId is resolved.
  std::unique_ptr<tserver::PgGetTserverCatalogVersionInfoResponsePB> catalog_version_info_;
  TupleIdBuilder tuple_id_builder_;
  BufferingSettings buffering_settings_;
  PgSessionPtr pg_session_;
  PgFKReferenceCache fk_reference_cache_;
  ExplicitRowLockBuffer explicit_row_lock_buffer_;

  ash::WaitStateInfoPtr wait_state_;
};

}  // namespace yb::pggate
