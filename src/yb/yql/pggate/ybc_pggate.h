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

// C wrappers around "pggate" for PostgreSQL to call.

#pragma once

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include "yb/yql/pggate/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*YBCAshAcquireBufferLock)(bool);
typedef YBCAshSample* (*YBCAshGetNextCircularBufferSlot)();
typedef const YBCPgTypeEntity* (*YBCTypeEntityProvider)(int, YBCPgOid);
typedef void (*YBCGetTableKeyRangesCallback)(void*, const char*, size_t);

typedef void * SliceVector;
typedef const void * ConstSliceVector;
typedef void * SliceSet;
typedef const void * ConstSliceSet;

typedef struct PgExplicitRowLockStatus {
  YBCStatus ybc_status;
  YBCPgExplicitRowLockErrorInfo error_info;
} YBCPgExplicitRowLockStatus;

// This must be called exactly once to initialize the YB/PostgreSQL gateway API before any other
// functions in this API are called.
void YBCInitPgGate(const YBCPgTypeEntity *YBCDataTypeTable, int count,
                   YBCPgCallbacks pg_callbacks, uint64_t *session_id,
                   const YBCPgAshConfig* ash_config);
void YBCDestroyPgGate();
void YBCInterruptPgGate();

//--------------------------------------------------------------------------------------------------
// Environment and Session.
void YBCDumpCurrentPgSessionState(YBCPgSessionState* session_data);

void YBCRestorePgSessionState(const YBCPgSessionState* session_data);

YBCStatus YBCPgInitSession(YBCPgExecStatsState* session_stats, bool is_binary_upgrade);

uint64_t YBCPgGetSessionID();

// Initialize YBCPgMemCtx.
// - Postgres uses memory context to hold all of its allocated space. Once all associated operations
//   are done, the context is destroyed.
// - There YugaByte objects are bound to Postgres operations. All of these objects' allocated
//   memory will be held by YBCPgMemCtx, whose handle belongs to Postgres MemoryContext. Once all
//   Postgres operations are done, associated YugaByte memory context (YBCPgMemCtx) will be
//   destroyed together with Postgres memory context.
YBCPgMemctx YBCPgCreateMemctx();
YBCStatus YBCPgDestroyMemctx(YBCPgMemctx memctx);
YBCStatus YBCPgResetMemctx(YBCPgMemctx memctx);
void YBCPgDeleteStatement(YBCPgStatement handle);

// Invalidate the sessions table cache.
YBCStatus YBCPgInvalidateCache();

// Check if initdb has been already run.
YBCStatus YBCPgIsInitDbDone(bool* initdb_done);

// Get gflag TEST_ysql_disable_transparent_cache_refresh_retry
bool YBCGetDisableTransparentCacheRefreshRetry();

// Set global catalog_version to the local tserver's catalog version
// stored in shared memory.
YBCStatus YBCGetSharedCatalogVersion(uint64_t* catalog_version);

// Set per-db catalog_version to the local tserver's per-db catalog version
// stored in shared memory.
YBCStatus YBCGetSharedDBCatalogVersion(
    YBCPgOid db_oid, uint64_t* catalog_version);

// Return the number of rows in pg_yb_catalog_table. Only used when per-db
// catalog version mode is enabled.
YBCStatus YBCGetNumberOfDatabases(uint32_t* num_databases);

// Return true if the pg_yb_catalog_version table has been updated to
// have one row per database.
YBCStatus YBCCatalogVersionTableInPerdbMode(bool* perdb_mode);

// Return auth_key to the local tserver's postgres authentication key stored in shared memory.
uint64_t YBCGetSharedAuthKey();

// Return UUID of the local tserver
const unsigned char* YBCGetLocalTserverUuid();

// Get access to callbacks.
const YBCPgCallbacks* YBCGetPgCallbacks();

int64_t YBCGetPgggateCurrentAllocatedBytes();

int64_t YBCGetActualHeapSizeBytes();

// Call root MemTacker to consume the consumption bytes.
// Return true if MemTracker exists (inited by pggate); otherwise false.
bool YBCTryMemConsume(int64_t bytes);

// Call root MemTacker to release the release bytes.
// Return true if MemTracker exists (inited by pggate); otherwise false.
bool YBCTryMemRelease(int64_t bytes);

YBCStatus YBCGetHeapConsumption(YbTcmallocStats *desc);

// Validate the JWT based on the options including the identity matching based on the identity map.
YBCStatus YBCValidateJWT(const char *token, const YBCPgJwtAuthOptions *options);
YBCStatus YBCFetchFromUrl(const char *url, char **buf);

// Is this node acting as the pg_cron leader?
bool YBCIsCronLeader();
YBCStatus YBCSetCronLastMinute(int64_t last_minute);
YBCStatus YBCGetCronLastMinute(int64_t* last_minute);

//--------------------------------------------------------------------------------------------------
// YB Bitmap Scan Operations
//--------------------------------------------------------------------------------------------------

// returns a void pointer to a std::set
SliceSet YBCBitmapCreateSet();

// Modifies sa to contain the union of `std::set`s sa and sb, and deletes sb. If the item already
// exists in sa, the duplicate item is deleted. Returns the number of new bytes added to sa.
size_t YBCBitmapUnionSet(SliceSet sa, ConstSliceSet sb);

// Returns a std::set representing the union of `std::set`s sa and sb.
// One of sa or sb are deleted, and the other is reused.
// Any items not included in the final set are deleted
SliceSet YBCBitmapIntersectSet(SliceSet sa, SliceSet sb);

// Insert the vector of ybctids into the set. Returns the number of new bytes allocated for the
// ybctids.
size_t YBCBitmapInsertYbctidsIntoSet(SliceSet set, ConstSliceVector vec);

// Returns a new std::vector containing all the elements of the std::set set. It
// is the caller's responsibility to delete the set.
ConstSliceVector YBCBitmapCopySetToVector(ConstSliceSet set, size_t *size);

// Returns a vector representing a chunk of the given vector. ybctids are
// shallow copied - their underlying allocations are shared.
ConstSliceVector YBCBitmapGetVectorRange(ConstSliceVector vec, size_t start, size_t length);

void YBCBitmapShallowDeleteVector(ConstSliceVector vec);
void YBCBitmapShallowDeleteSet(ConstSliceSet set);

// Deletes the set allocation AND all of the ybctids contained within the set.
void YBCBitmapDeepDeleteSet(ConstSliceSet set);

size_t YBCBitmapGetSetSize(ConstSliceSet set);
size_t YBCBitmapGetVectorSize(ConstSliceVector vec);

//--------------------------------------------------------------------------------------------------
// DDL Statements
//--------------------------------------------------------------------------------------------------

// DATABASE ----------------------------------------------------------------------------------------
// Get whether the given database is colocated
// and whether the database is a legacy colocated database.
YBCStatus YBCPgIsDatabaseColocated(const YBCPgOid database_oid, bool *colocated,
                                   bool *legacy_colocated_database);

YBCStatus YBCInsertSequenceTuple(int64_t db_oid,
                                 int64_t seq_oid,
                                 uint64_t ysql_catalog_version,
                                 bool is_db_catalog_version_mode,
                                 int64_t last_val,
                                 bool is_called);

YBCStatus YBCUpdateSequenceTupleConditionally(int64_t db_oid,
                                              int64_t seq_oid,
                                              uint64_t ysql_catalog_version,
                                              bool is_db_catalog_version_mode,
                                              int64_t last_val,
                                              bool is_called,
                                              int64_t expected_last_val,
                                              bool expected_is_called,
                                              bool *skipped);

YBCStatus YBCUpdateSequenceTuple(int64_t db_oid,
                                 int64_t seq_oid,
                                 uint64_t ysql_catalog_version,
                                 bool is_db_catalog_version_mode,
                                 int64_t last_val,
                                 bool is_called,
                                 bool* skipped);

YBCStatus YBCFetchSequenceTuple(int64_t db_oid,
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

YBCStatus YBCReadSequenceTuple(int64_t db_oid,
                               int64_t seq_oid,
                               uint64_t ysql_catalog_version,
                               bool is_db_catalog_version_mode,
                               int64_t *last_val,
                               bool *is_called);

YBCStatus YBCPgNewDropSequence(const YBCPgOid database_oid,
                               const YBCPgOid sequence_oid,
                               YBCPgStatement *handle);

YBCStatus YBCPgExecDropSequence(YBCPgStatement handle);

YBCStatus YBCPgNewDropDBSequences(const YBCPgOid database_oid,
                                  YBCPgStatement *handle);

// Create database.
YBCStatus YBCPgNewCreateDatabase(const char *database_name,
                                 YBCPgOid database_oid,
                                 YBCPgOid source_database_oid,
                                 YBCPgOid next_oid,
                                 const bool colocated,
                                 YbCloneInfo *yb_clone_info,
                                 YBCPgStatement *handle);
YBCStatus YBCPgExecCreateDatabase(YBCPgStatement handle);

// Drop database.
YBCStatus YBCPgNewDropDatabase(const char *database_name,
                               YBCPgOid database_oid,
                               YBCPgStatement *handle);
YBCStatus YBCPgExecDropDatabase(YBCPgStatement handle);

// Alter database.
YBCStatus YBCPgNewAlterDatabase(const char *database_name,
                               YBCPgOid database_oid,
                               YBCPgStatement *handle);
YBCStatus YBCPgAlterDatabaseRenameDatabase(YBCPgStatement handle, const char *newname);
YBCStatus YBCPgExecAlterDatabase(YBCPgStatement handle);

// Reserve oids.
YBCStatus YBCPgReserveOids(YBCPgOid database_oid,
                           YBCPgOid next_oid,
                           uint32_t count,
                           YBCPgOid *begin_oid,
                           YBCPgOid *end_oid);

// Retrieve the protobuf-based catalog version (now deprecated for new clusters).
YBCStatus YBCPgGetCatalogMasterVersion(uint64_t *version);

YBCStatus YBCPgInvalidateTableCacheByTableId(const char *table_id);

// TABLEGROUP --------------------------------------------------------------------------------------

// Create tablegroup.
YBCStatus YBCPgNewCreateTablegroup(const char *database_name,
                                   YBCPgOid database_oid,
                                   YBCPgOid tablegroup_oid,
                                   YBCPgOid tablespace_oid,
                                   YBCPgStatement *handle);
YBCStatus YBCPgExecCreateTablegroup(YBCPgStatement handle);

// Drop tablegroup.
YBCStatus YBCPgNewDropTablegroup(YBCPgOid database_oid,
                                 YBCPgOid tablegroup_oid,
                                 YBCPgStatement *handle);
YBCStatus YBCPgExecDropTablegroup(YBCPgStatement handle);

// TABLE -------------------------------------------------------------------------------------------
// Create and drop table "database_name.schema_name.table_name()".
// - When "schema_name" is NULL, the table "database_name.table_name" is created.
// - When "database_name" is NULL, the table "connected_database_name.table_name" is created.
YBCStatus YBCPgNewCreateTable(const char *database_name,
                              const char *schema_name,
                              const char *table_name,
                              YBCPgOid database_oid,
                              YBCPgOid table_relfilenode_oid,
                              bool is_shared_table,
                              bool is_sys_catalog_table,
                              bool if_not_exist,
                              YBCPgYbrowidMode ybrowid_mode,
                              bool is_colocated_via_database,
                              YBCPgOid tablegroup_oid,
                              YBCPgOid colocation_id,
                              YBCPgOid tablespace_oid,
                              bool is_matview,
                              YBCPgOid pg_table_oid,
                              YBCPgOid old_relfilenode_oid,
                              bool is_truncate,
                              YBCPgStatement *handle);

YBCStatus YBCPgCreateTableAddColumn(YBCPgStatement handle, const char *attr_name, int attr_num,
                                    const YBCPgTypeEntity *attr_type, bool is_hash, bool is_range,
                                    bool is_desc, bool is_nulls_first);

YBCStatus YBCPgCreateTableSetNumTablets(YBCPgStatement handle, int32_t num_tablets);

YBCStatus YBCPgAddSplitBoundary(YBCPgStatement handle, YBCPgExpr *exprs, int expr_count);

YBCStatus YBCPgExecCreateTable(YBCPgStatement handle);

YBCStatus YBCPgNewAlterTable(YBCPgOid database_oid,
                             YBCPgOid table_relfilenode_oid,
                             YBCPgStatement *handle);

YBCStatus YBCPgAlterTableAddColumn(YBCPgStatement handle, const char *name, int order,
                                   const YBCPgTypeEntity *attr_type,
                                   YBCPgExpr missing_value);

YBCStatus YBCPgAlterTableRenameColumn(YBCPgStatement handle, const char *oldname,
                                      const char *newname);

YBCStatus YBCPgAlterTableDropColumn(YBCPgStatement handle, const char *name);

YBCStatus YBCPgAlterTableSetReplicaIdentity(YBCPgStatement handle, const char identity_type);

YBCStatus YBCPgAlterTableRenameTable(YBCPgStatement handle, const char *db_name,
                                     const char *newname);

YBCStatus YBCPgAlterTableIncrementSchemaVersion(YBCPgStatement handle);

YBCStatus YBCPgAlterTableSetTableId(
    YBCPgStatement handle, const YBCPgOid database_oid, const YBCPgOid table_relfilenode_oid);

YBCStatus YBCPgAlterTableSetSchema(YBCPgStatement handle, const char *schema_name);

YBCStatus YBCPgExecAlterTable(YBCPgStatement handle);

YBCStatus YBCPgAlterTableInvalidateTableCacheEntry(YBCPgStatement handle);

void YBCPgAlterTableInvalidateTableByOid(
    const YBCPgOid database_oid, const YBCPgOid table_relfilenode_oid);

YBCStatus YBCPgNewDropTable(YBCPgOid database_oid,
                            YBCPgOid table_relfilenode_oid,
                            bool if_exist,
                            YBCPgStatement *handle);

YBCStatus YBCPgNewTruncateTable(YBCPgOid database_oid,
                                YBCPgOid table_relfilenode_oid,
                                YBCPgStatement *handle);

YBCStatus YBCPgExecTruncateTable(YBCPgStatement handle);

YBCStatus YBCPgGetTableDesc(YBCPgOid database_oid,
                            YBCPgOid table_relfilenode_oid,
                            YBCPgTableDesc *handle);

YBCStatus YBCPgGetColumnInfo(YBCPgTableDesc table_desc,
                             int16_t attr_number,
                             YBCPgColumnInfo *column_info);

// Callers should probably use YbGetTableProperties instead.
YBCStatus YBCPgGetTableProperties(YBCPgTableDesc table_desc,
                                  YbTableProperties properties);

YBCStatus YBCPgDmlModifiesRow(YBCPgStatement handle, bool *modifies_row);

YBCStatus YBCPgSetIsSysCatalogVersionChange(YBCPgStatement handle);

YBCStatus YBCPgSetCatalogCacheVersion(YBCPgStatement handle, uint64_t version);

YBCStatus YBCPgSetDBCatalogCacheVersion(YBCPgStatement handle,
                                        YBCPgOid db_oid,
                                        uint64_t version);

YBCStatus YBCPgTableExists(const YBCPgOid database_oid,
                           const YBCPgOid table_relfilenode_oid,
                           bool *exists);

YBCStatus YBCPgGetTableDiskSize(YBCPgOid table_relfilenode_oid,
                                YBCPgOid database_oid,
                                int64_t *size,
                                int32_t *num_missing_tablets);

YBCStatus YBCGetSplitPoints(YBCPgTableDesc table_desc,
                            const YBCPgTypeEntity **type_entities,
                            YBCPgTypeAttrs *type_attrs_arr,
                            YBCPgSplitDatum *split_points,
                            bool *has_null, bool *has_gin_null);

// INDEX -------------------------------------------------------------------------------------------
// Create and drop index "database_name.schema_name.index_name()".
// - When "schema_name" is NULL, the index "database_name.index_name" is created.
// - When "database_name" is NULL, the index "connected_database_name.index_name" is created.
YBCStatus YBCPgNewCreateIndex(const char *database_name,
                              const char *schema_name,
                              const char *index_name,
                              YBCPgOid database_oid,
                              YBCPgOid index_relfilenode_oid,
                              YBCPgOid table_relfilenode_oid,
                              bool is_shared_index,
                              bool is_sys_catalog_index,
                              bool is_unique_index,
                              const bool skip_index_backfill,
                              bool if_not_exist,
                              bool is_colocated_via_database,
                              YBCPgOid tablegroup_oid,
                              YBCPgOid colocation_id,
                              YBCPgOid tablespace_oid,
                              YBCPgOid pg_table_oid,
                              YBCPgOid old_relfilenode_oid,
                              YBCPgStatement *handle);

YBCStatus YBCPgCreateIndexAddColumn(YBCPgStatement handle, const char *attr_name, int attr_num,
                                    const YBCPgTypeEntity *attr_type, bool is_hash, bool is_range,
                                    bool is_desc, bool is_nulls_first);

YBCStatus YBCPgCreateIndexSetNumTablets(YBCPgStatement handle, int32_t num_tablets);

YBCStatus YBCPgCreateIndexSetVectorOptions(YBCPgStatement handle, YbPgVectorIdxOptions *options);

YBCStatus YBCPgExecCreateIndex(YBCPgStatement handle);

YBCStatus YBCPgNewDropIndex(YBCPgOid database_oid,
                            YBCPgOid index_relfilenode_oid,
                            bool if_exist,
                            bool ddl_rollback_enabled,
                            YBCPgStatement *handle);

YBCStatus YBCPgExecPostponedDdlStmt(YBCPgStatement handle);

YBCStatus YBCPgExecDropTable(YBCPgStatement handle);

YBCStatus YBCPgExecDropIndex(YBCPgStatement handle);

YBCStatus YBCPgWaitForBackendsCatalogVersion(
    YBCPgOid dboid,
    uint64_t version,
    pid_t pid,
    int* num_lagging_backends);

YBCStatus YBCPgBackfillIndex(
    const YBCPgOid database_oid,
    const YBCPgOid index_relfilenode_oid);

//--------------------------------------------------------------------------------------------------
// DML statements (select, insert, update, delete, truncate)
//--------------------------------------------------------------------------------------------------

// This function is for specifying the selected or returned expressions.
// - SELECT target_expr1, target_expr2, ...
// - INSERT / UPDATE / DELETE ... RETURNING target_expr1, target_expr2, ...
YBCStatus YBCPgDmlAppendTarget(YBCPgStatement handle, YBCPgExpr target);

// Add a WHERE clause condition to the statement.
// Currently only SELECT statement supports WHERE clause conditions.
// Only serialized Postgres expressions are allowed.
// Multiple quals added to the same statement are implicitly AND'ed.
YBCStatus YbPgDmlAppendQual(
    YBCPgStatement handle, YBCPgExpr qual, bool is_for_secondary_index);

// Add column reference needed to evaluate serialized Postgres expression.
// PgExpr's other than serialized Postgres expressions are inspected and if they contain any
// column references, they are added automatically io the DocDB request. We do not do that
// for serialized postgres expression because it may be expensive to deserialize and analyze
// potentially complex expressions. While expressions are deserialized anyway by DocDB, the
// concern about cost of analysis still stands.
// While optional in regular column reference expressions, column references needed to evaluate
// serialized Postgres expression must contain Postgres data type information. DocDB needs to know
// how to convert values from the DocDB formats to use them to evaluate Postgres expressions.
YBCStatus YbPgDmlAppendColumnRef(
    YBCPgStatement handle, YBCPgExpr colref, bool is_for_secondary_index);

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
//
// NOTE ON KEY BINDING
// - For Sequential Scan, the target columns of the bind are those in the main table.
// - For Primary Scan, the target columns of the bind are those in the main table.
// - For Index Scan, the target columns of the bind are those in the index table.
//   The index-scan will use the bind to find base-ybctid which is then use to read data from
//   the main-table, and therefore the bind-arguments are not associated with columns in main table.
YBCStatus YBCPgDmlBindColumn(YBCPgStatement handle, int attr_num, YBCPgExpr attr_value);
YBCStatus YBCPgDmlBindColumnCondBetween(YBCPgStatement handle, int attr_num,
                                        YBCPgExpr attr_value,
                                        bool start_inclusive,
                                        YBCPgExpr attr_value_end,
                                        bool end_inclusive);
YBCStatus YBCPgDmlBindColumnCondIn(YBCPgStatement handle,
                                   YBCPgExpr lhs,
                                   int n_attr_values,
                                   YBCPgExpr *attr_values);
YBCStatus YBCPgDmlBindColumnCondIsNotNull(YBCPgStatement handle, int attr_num);
YBCStatus YBCPgDmlBindRow(
    YBCPgStatement handle, uint64_t ybctid, YBCBindColumn* columns, int count);

YBCStatus YBCPgDmlGetColumnInfo(YBCPgStatement handle, int attr_num, YBCPgColumnInfo* info);

YBCStatus YBCPgDmlBindHashCodes(
    YBCPgStatement handle,
    YBCPgBoundType start_type, uint16_t start_value,
    YBCPgBoundType end_type, uint16_t end_value);

// For parallel scan only, limit fetch to specified range of ybctids
YBCStatus YBCPgDmlBindRange(YBCPgStatement handle,
                            const char *lower_bound, size_t lower_bound_len,
                            const char *upper_bound, size_t upper_bound_len);

YBCStatus YBCPgDmlAddRowUpperBound(YBCPgStatement handle, int n_col_values,
                                    YBCPgExpr *col_values, bool is_inclusive);

YBCStatus YBCPgDmlAddRowLowerBound(YBCPgStatement handle, int n_col_values,
                                    YBCPgExpr *col_values, bool is_inclusive);

// Binding Tables: Bind the whole table in a statement.  Do not use with BindColumn.
YBCStatus YBCPgDmlBindTable(YBCPgStatement handle);

// API for SET clause.
YBCStatus YBCPgDmlAssignColumn(YBCPgStatement handle,
                               int attr_num,
                               YBCPgExpr attr_value);

YBCStatus YBCPgDmlANNBindVector(YBCPgStatement handle, YBCPgExpr vector);

YBCStatus YBCPgDmlANNSetPrefetchSize(YBCPgStatement handle, int prefetch_size);

// This function is to fetch the targets in YBCPgDmlAppendTarget() from the rows that were defined
// by YBCPgDmlBindColumn().
YBCStatus YBCPgDmlFetch(YBCPgStatement handle, int32_t natts, uint64_t *values, bool *isnulls,
                        YBCPgSysColumns *syscols, bool *has_data);

// Utility method that checks stmt type and calls either exec insert, update, or delete internally.
YBCStatus YBCPgDmlExecWriteOp(YBCPgStatement handle, int32_t *rows_affected_count);

// This function returns the tuple id (ybctid) of a Postgres tuple.
YBCStatus YBCPgBuildYBTupleId(const YBCPgYBTupleIdDescriptor* data, uint64_t *ybctid);

// DB Operations: WHERE, ORDER_BY, GROUP_BY, etc.
// + The following operations are run by DocDB.
//   - Not yet
//
// + The following operations are run by Postgres layer. An API might be added to move these
//   operations to DocDB.
//   - API for "where_expr"
//   - API for "order_by_expr"
//   - API for "group_by_expr"


// Buffer write operations.
YBCStatus YBCPgStartOperationsBuffering();
YBCStatus YBCPgStopOperationsBuffering();
void YBCPgResetOperationsBuffering();
YBCStatus YBCPgFlushBufferedOperations();

YBCStatus YBCPgNewSample(const YBCPgOid database_oid,
                         const YBCPgOid table_relfilenode_oid,
                         bool is_region_local,
                         int targrows,
                         double rstate_w,
                         uint64_t rand_state_s0,
                         uint64_t rand_state_s1,
                         YBCPgStatement *handle);

YBCStatus YBCPgSampleNextBlock(YBCPgStatement handle, bool *has_more);

YBCStatus YBCPgExecSample(YBCPgStatement handle);

YBCStatus YBCPgGetEstimatedRowCount(YBCPgStatement handle, double *liverows, double *deadrows);

// INSERT ------------------------------------------------------------------------------------------

// Allocate block of inserts to the same table.
YBCStatus YBCPgNewInsertBlock(
    YBCPgOid database_oid,
    YBCPgOid table_oid,
    bool is_region_local,
    YBCPgTransactionSetting transaction_setting,
    YBCPgStatement *handle);

YBCStatus YBCPgNewInsert(YBCPgOid database_oid,
                         YBCPgOid table_relfilenode_oid,
                         bool is_region_local,
                         YBCPgStatement *handle,
                         YBCPgTransactionSetting transaction_setting);

YBCStatus YBCPgExecInsert(YBCPgStatement handle);

YBCStatus YBCPgInsertStmtSetUpsertMode(YBCPgStatement handle);

YBCStatus YBCPgInsertStmtSetWriteTime(YBCPgStatement handle, const uint64_t write_time);

YBCStatus YBCPgInsertStmtSetIsBackfill(YBCPgStatement handle, const bool is_backfill);

// UPDATE ------------------------------------------------------------------------------------------
YBCStatus YBCPgNewUpdate(YBCPgOid database_oid,
                         YBCPgOid table_relfilenode_oid,
                         bool is_region_local,
                         YBCPgStatement *handle,
                         YBCPgTransactionSetting transaction_setting);

YBCStatus YBCPgExecUpdate(YBCPgStatement handle);

// DELETE ------------------------------------------------------------------------------------------
YBCStatus YBCPgNewDelete(YBCPgOid database_oid,
                         YBCPgOid table_relfilenode_oid,
                         bool is_region_local,
                         YBCPgStatement *handle,
                         YBCPgTransactionSetting transaction_setting);

YBCStatus YBCPgExecDelete(YBCPgStatement handle);

YBCStatus YBCPgDeleteStmtSetIsPersistNeeded(YBCPgStatement handle, const bool is_persist_needed);

// Colocated TRUNCATE ------------------------------------------------------------------------------
YBCStatus YBCPgNewTruncateColocated(YBCPgOid database_oid,
                                    YBCPgOid table_relfilenode_oid,
                                    bool is_region_local,
                                    YBCPgStatement *handle,
                                    YBCPgTransactionSetting transaction_setting);

YBCStatus YBCPgExecTruncateColocated(YBCPgStatement handle);

// SELECT ------------------------------------------------------------------------------------------
YBCStatus YBCPgNewSelect(YBCPgOid database_oid,
                         YBCPgOid table_relfilenode_oid,
                         const YBCPgPrepareParameters *prepare_params,
                         bool is_region_local,
                         YBCPgStatement *handle);

// Set forward/backward scan direction.
YBCStatus YBCPgSetForwardScan(YBCPgStatement handle, bool is_forward_scan);

// Set prefix length for distinct index scans.
YBCStatus YBCPgSetDistinctPrefixLength(YBCPgStatement handle, int distinct_prefix_length);

YBCStatus YBCPgSetHashBounds(YBCPgStatement handle, uint16_t low_bound, uint16_t high_bound);

YBCStatus YBCPgExecSelect(YBCPgStatement handle, const YBCPgExecParameters *exec_params);

// Gets the ybctids from a request. Returns a vector of all the results.
// If the ybctids would exceed max_bytes, set `exceeded_work_mem` to true.
YBCStatus YBCPgRetrieveYbctids(YBCPgStatement handle, const YBCPgExecParameters *exec_params,
                               int natts, SliceVector *ybctids, size_t *count,
                               bool *exceeded_work_mem);
YBCStatus YBCPgFetchRequestedYbctids(YBCPgStatement handle, const YBCPgExecParameters *exec_params,
                                     ConstSliceVector ybctids);

// Functions----------------------------------------------------------------------------------------
YBCStatus YBCAddFunctionParam(
    YBCPgFunction handle, const char *name, const YBCPgTypeEntity *type_entity, uint64_t datum,
    bool is_null);

YBCStatus YBCAddFunctionTarget(
    YBCPgFunction handle, const char *attr_name, const YBCPgTypeEntity *type_entity,
    const YBCPgTypeAttrs type_attrs);

YBCStatus YBCSRFGetNext(YBCPgFunction handle, uint64_t *values, bool *is_nulls, bool *has_data);

YBCStatus YBCFinalizeFunctionTargets(YBCPgFunction handle);

// Transaction control -----------------------------------------------------------------------------
YBCStatus YBCPgBeginTransaction(int64_t start_time);
YBCStatus YBCPgRecreateTransaction();
YBCStatus YBCPgRestartTransaction();
YBCStatus YBCPgResetTransactionReadPoint();
YBCStatus YBCPgEnsureReadPoint();
YBCStatus YBCPgRestartReadPoint();
bool YBCIsRestartReadPointRequested();
YBCStatus YBCPgCommitPlainTransaction();
YBCStatus YBCPgAbortPlainTransaction();
YBCStatus YBCPgSetTransactionIsolationLevel(int isolation);
YBCStatus YBCPgSetTransactionReadOnly(bool read_only);
YBCStatus YBCPgSetTransactionDeferrable(bool deferrable);
YBCStatus YBCPgSetInTxnBlock(bool in_txn_blk);
YBCStatus YBCPgSetReadOnlyStmt(bool read_only_stmt);
YBCStatus YBCPgSetEnableTracing(bool tracing);
YBCStatus YBCPgUpdateFollowerReadsConfig(bool enable_follower_reads, int32_t staleness_ms);
YBCStatus YBCPgEnterSeparateDdlTxnMode();
bool YBCPgHasWriteOperationsInDdlTxnMode();
YBCStatus YBCPgExitSeparateDdlTxnMode(YBCPgOid db_oid, bool is_silent_altering);
YBCStatus YBCPgClearSeparateDdlTxnMode();
YBCStatus YBCPgSetActiveSubTransaction(uint32_t id);
YBCStatus YBCPgRollbackToSubTransaction(uint32_t id);
double YBCGetTransactionPriority();
TxnPriorityRequirement YBCGetTransactionPriorityType();
YBCStatus YBCPgGetSelfActiveTransaction(YBCPgUuid *txn_id, bool *is_null);
YBCStatus YBCPgActiveTransactions(YBCPgSessionTxnInfo *infos, size_t num_infos);
bool YBCPgIsDdlMode();

// System validation -------------------------------------------------------------------------------
// Validate placement information
YBCStatus YBCPgValidatePlacement(const char *placement_info);

//--------------------------------------------------------------------------------------------------
// Expressions.

// Column references.
YBCStatus YBCPgNewColumnRef(
    YBCPgStatement stmt, int attr_num, const YBCPgTypeEntity *type_entity,
    bool collate_is_valid_non_c, const YBCPgTypeAttrs *type_attrs,
    YBCPgExpr *expr_handle);

// Constant expressions.
// Construct an actual constant value.
YBCStatus YBCPgNewConstant(
    YBCPgStatement stmt, const YBCPgTypeEntity *type_entity, bool collate_is_valid_non_c,
    const char *collation_sortkey, uint64_t datum, bool is_null, YBCPgExpr *expr_handle);
// Construct a virtual constant value.
YBCStatus YBCPgNewConstantVirtual(
    YBCPgStatement stmt, const YBCPgTypeEntity *type_entity,
    YBCPgDatumKind datum_kind, YBCPgExpr *expr_handle);
// Construct an operator expression on a constant.
YBCStatus YBCPgNewConstantOp(
    YBCPgStatement stmt, const YBCPgTypeEntity *type_entity, bool collate_is_valid_non_c,
    const char *collation_sortkey, uint64_t datum, bool is_null, YBCPgExpr *expr_handle,
    bool is_gt);

// The following update functions only work for constants.
// Overwriting the constant expression with new value.
YBCStatus YBCPgUpdateConstInt2(YBCPgExpr expr, int16_t value, bool is_null);
YBCStatus YBCPgUpdateConstInt4(YBCPgExpr expr, int32_t value, bool is_null);
YBCStatus YBCPgUpdateConstInt8(YBCPgExpr expr, int64_t value, bool is_null);
YBCStatus YBCPgUpdateConstFloat4(YBCPgExpr expr, float value, bool is_null);
YBCStatus YBCPgUpdateConstFloat8(YBCPgExpr expr, double value, bool is_null);
YBCStatus YBCPgUpdateConstText(YBCPgExpr expr, const char *value, bool is_null);
YBCStatus YBCPgUpdateConstBinary(YBCPgExpr expr, const char *value, int64_t bytes, bool is_null);

// Expressions with operators "=", "+", "between", "in", ...
YBCStatus YBCPgNewOperator(
    YBCPgStatement stmt, const char *opname, const YBCPgTypeEntity *type_entity,
    bool collate_is_valid_non_c, YBCPgExpr *op_handle);
YBCStatus YBCPgOperatorAppendArg(YBCPgExpr op_handle, YBCPgExpr arg);

YBCStatus YBCPgNewTupleExpr(
    YBCPgStatement stmt, const YBCPgTypeEntity *tuple_type_entity,
    const YBCPgTypeAttrs *type_attrs, int num_elems,
    YBCPgExpr *elems, YBCPgExpr *expr_handle);

YBCStatus YBCGetDocDBKeySize(uint64_t data, const YBCPgTypeEntity *typeentity,
                            bool is_null, size_t *type_size);

YBCStatus YBCAppendDatumToKey(uint64_t data,  const YBCPgTypeEntity
                            *typeentity, bool is_null, char *key_ptr,
                            size_t *bytes_written);

uint16_t YBCCompoundHash(const char *key, size_t length);

// Referential Integrity Check Caching.
void YBCPgDeleteFromForeignKeyReferenceCache(YBCPgOid table_relfilenode_oid, uint64_t ybctid);
void YBCPgAddIntoForeignKeyReferenceCache(YBCPgOid table_relfilenode_oid, uint64_t ybctid);
YBCStatus YBCPgForeignKeyReferenceCacheDelete(const YBCPgYBTupleIdDescriptor* descr);
YBCStatus YBCForeignKeyReferenceExists(const YBCPgYBTupleIdDescriptor* descr, bool* res);
YBCStatus YBCAddForeignKeyReferenceIntent(const YBCPgYBTupleIdDescriptor* descr,
                                          bool relation_is_region_local);

// Explicit Row-level Locking.
YBCPgExplicitRowLockStatus YBCAddExplicitRowLockIntent(
    YBCPgOid table_relfilenode_oid, uint64_t ybctid, YBCPgOid database_oid,
    const YBCPgExplicitRowLockParams *params, bool is_region_local);
YBCPgExplicitRowLockStatus YBCFlushExplicitRowLockIntents();

// INSERT ... ON CONFLICT batching -----------------------------------------------------------------
YBCStatus YBCPgAddInsertOnConflictKey(const YBCPgYBTupleIdDescriptor* tupleid, void* state,
                                      YBCPgInsertOnConflictKeyInfo* info);
YBCStatus YBCPgInsertOnConflictKeyExists(const YBCPgYBTupleIdDescriptor* tupleid, void* state,
                                         YBCPgInsertOnConflictKeyState* res);
YBCStatus YBCPgDeleteInsertOnConflictKey(const YBCPgYBTupleIdDescriptor* tupleid, void* state,
                                         YBCPgInsertOnConflictKeyInfo* info);
YBCStatus YBCPgDeleteNextInsertOnConflictKey(void* state, YBCPgInsertOnConflictKeyInfo* info);
YBCStatus YBCPgAddInsertOnConflictKeyIntent(const YBCPgYBTupleIdDescriptor* tupleid);
void YBCPgClearAllInsertOnConflictCaches();
void YBCPgClearInsertOnConflictCache(void* state);
uint64_t YBCPgGetInsertOnConflictKeyCount(void* state);
//--------------------------------------------------------------------------------------------------

bool YBCIsInitDbModeEnvVarSet();

// This is called by initdb. Used to customize some behavior.
void YBCInitFlags();

const YBCPgGFlagsAccessor* YBCGetGFlags();

bool YBCPgIsYugaByteEnabled();

// Sets the specified timeout in the rpc service.
void YBCSetTimeout(int timeout_ms, void* extra);

//--------------------------------------------------------------------------------------------------
// Thread-Local variables.

void* YBCPgGetThreadLocalCurrentMemoryContext();

void* YBCPgSetThreadLocalCurrentMemoryContext(void *memctx);

void YBCPgResetCurrentMemCtxThreadLocalVars();

void* YBCPgGetThreadLocalStrTokPtr();

void YBCPgSetThreadLocalStrTokPtr(char *new_pg_strtok_ptr);

void* YBCPgSetThreadLocalJumpBuffer(void* new_buffer);

void* YBCPgGetThreadLocalJumpBuffer();

void* YBCPgSetThreadLocalErrStatus(void* new_status);

void* YBCPgGetThreadLocalErrStatus();

YBCPgThreadLocalRegexpCache* YBCPgGetThreadLocalRegexpCache();

YBCPgThreadLocalRegexpCache* YBCPgInitThreadLocalRegexpCache(
    size_t buffer_size, YBCPgThreadLocalRegexpCacheCleanup cleanup);

void YBCPgResetCatalogReadTime();

YBCStatus YBCNewGetLockStatusDataSRF(YBCPgFunction *handle);

YBCStatus YBCGetTabletServerHosts(YBCServerDescriptor **tablet_servers, size_t* numservers);

YBCStatus YBCGetIndexBackfillProgress(YBCPgOid* index_relfilenode_oids, YBCPgOid* database_oids,
                                      uint64_t** backfill_statuses,
                                      int num_indexes);

void YBCStartSysTablePrefetchingNoCache();

void YBCStartSysTablePrefetching(
    YBCPgOid database_oid,
    YBCPgLastKnownCatalogVersionInfo catalog_version,
    YBCPgSysTablePrefetcherCacheMode cache_mode);

void YBCStopSysTablePrefetching();

bool YBCIsSysTablePrefetchingStarted();

void YBCRegisterSysTableForPrefetching(
    YBCPgOid database_oid, YBCPgOid table_oid, YBCPgOid index_oid, int row_oid_filtering_attr,
    bool fetch_ybctid);

YBCStatus YBCPrefetchRegisteredSysTables();

YBCStatus YBCPgCheckIfPitrActive(bool* is_active);

YBCStatus YBCIsObjectPartOfXRepl(YBCPgOid database_oid, YBCPgOid table_relfilenode_oid,
                                 bool* is_object_part_of_xrepl);

YBCStatus YBCPgCancelTransaction(const unsigned char* transaction_id);

// Breaks table data into ranges of approximately range_size_bytes each, at most into
// `max_num_ranges`.
// Returns (through callback) list of these ranges end keys and fills current_tserver_ht if not
// nullptr.

// It is guaranteed that returned keys are at most max_key_length bytes.
// lower_bound_key is inclusive, upper_bound_key is exclusive.
// Iff we've reached the end of the table (or upper bound) then empty key is returned as the last
// key.
YBCStatus YBCGetTableKeyRanges(
    YBCPgOid database_oid, YBCPgOid table_relfilenode_oid, const char* lower_bound_key,
    size_t lower_bound_key_size, const char* upper_bound_key, size_t upper_bound_key_size,
    uint64_t max_num_ranges, uint64_t range_size_bytes, bool is_forward, uint32_t max_key_length,
    YBCGetTableKeyRangesCallback callback, void* callback_param);

//--------------------------------------------------------------------------------------------------
// Replication Slots.

YBCStatus YBCPgNewCreateReplicationSlot(const char *slot_name,
                                        const char *plugin_name,
                                        YBCPgOid database_oid,
                                        YBCPgReplicationSlotSnapshotAction snapshot_action,
                                        YBCLsnType lsn_type,
                                        YBCPgStatement *handle);
YBCStatus YBCPgExecCreateReplicationSlot(YBCPgStatement handle,
                                         uint64_t *consistent_snapshot_time);

YBCStatus YBCPgListReplicationSlots(
    YBCReplicationSlotDescriptor **replication_slots, size_t *numreplicationslots);

YBCStatus YBCPgGetReplicationSlot(
    const char *slot_name, YBCReplicationSlotDescriptor **replication_slot);

YBCStatus YBCPgNewDropReplicationSlot(const char *slot_name,
                                      YBCPgStatement *handle);
YBCStatus YBCPgExecDropReplicationSlot(YBCPgStatement handle);

YBCStatus YBCPgInitVirtualWalForCDC(
    const char *stream_id, const YBCPgOid database_oid, YBCPgOid *relations, YBCPgOid *relfilenodes,
    size_t num_relations);

YBCStatus YBCPgUpdatePublicationTableList(
    const char *stream_id, const YBCPgOid database_oid, YBCPgOid *relations, YBCPgOid *relfilenodes,
    size_t num_relations);

YBCStatus YBCPgDestroyVirtualWalForCDC();

YBCStatus YBCPgGetCDCConsistentChanges(const char *stream_id,
                                       YBCPgChangeRecordBatch **record_batch,
                                       YBCTypeEntityProvider type_entity_provider);

YBCStatus YBCPgUpdateAndPersistLSN(
    const char* stream_id, YBCPgXLogRecPtr restart_lsn_hint, YBCPgXLogRecPtr confirmed_flush,
    YBCPgXLogRecPtr* restart_lsn);

// Get a new OID from the OID allocator of database db_oid.
YBCStatus YBCGetNewObjectId(YBCPgOid db_oid, YBCPgOid* new_oid);

YBCStatus YBCYcqlStatementStats(YCQLStatementStats** stats, size_t* num_stats);

// Active Session History
void YBCStoreTServerAshSamples(
    YBCAshAcquireBufferLock acquire_cb_lock_fn, YBCAshGetNextCircularBufferSlot get_cb_slot_fn,
    uint64_t sample_time);

YBCStatus YBCLocalTablets(YBCPgTabletsDescriptor** tablets, size_t* count);

YBCStatus YBCServersMetrics(YBCPgServerMetricsInfo** serverMetricsInfo, size_t* count);

YBCStatus YBCDatabaseClones(YBCPgDatabaseCloneInfo** databaseClones, size_t* count);

uint64_t YBCPgGetCurrentReadTimePoint();
YBCStatus YBCRestoreReadTimePoint(uint64_t read_time_point_handle);

void YBCForceAllowCatalogModifications(bool allowed);

#ifdef __cplusplus
}  // extern "C"
#endif

#ifdef __cplusplus
#include <optional>

namespace yb {
namespace pggate {

struct PgApiContext;

void YBCInitPgGateEx(
    const YBCPgTypeEntity *data_type_table, int count, YBCPgCallbacks pg_callbacks,
    PgApiContext *context, std::optional<uint64_t> session_id,
    const YBCPgAshConfig* ash_config);

} // namespace pggate
} // namespace yb
#endif
