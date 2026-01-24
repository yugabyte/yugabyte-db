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

typedef void (*YbcAshAcquireBufferLock)(bool);
typedef YbcAshSample* (*YbcAshGetNextCircularBufferSlot)();
typedef const YbcPgTypeEntity* (*YbcTypeEntityProvider)(int, YbcPgOid);
typedef void (*YbcGetTableKeyRangesCallback)(void*, const char*, size_t);

typedef void * YbcSliceVector;
typedef const void * YbcConstSliceVector;
typedef void * YbcSliceSet;
typedef const void * YbcConstSliceSet;

typedef void (*YbcRecordTempRelationDDL_hook_type)();
extern YbcRecordTempRelationDDL_hook_type YBCRecordTempRelationDDL_hook;

typedef struct {
  YbcStatus ybc_status;
  YbcPgExplicitRowLockErrorInfo error_info;
} YbcPgExplicitRowLockStatus;

// This must be called exactly once to initialize the YB/PostgreSQL gateway API before any other
// functions in this API are called.
YbcStatus YBCInitPgGate(
    YbcPgTypeEntities type_entities, const YbcPgCallbacks* pg_callbacks,
    const YbcPgInitPostgresInfo *init_postgres_info, YbcPgAshConfig* ash_config,
    YbcPgExecStatsState *session_stats, bool is_binary_upgrade);

void YBCDestroyPgGate();
void YBCInterruptPgGate();

//--------------------------------------------------------------------------------------------------
// Environment and Session.
void YBCDumpCurrentPgSessionState(YbcPgSessionState* session_data);

void YBCRestorePgSessionState(const YbcPgSessionState* session_data);

void YBCPgIncrementIndexRecheckCount();

uint64_t YBCPgGetSessionID();

// Initialize YBCPgMemCtx.
// - Postgres uses memory context to hold all of its allocated space. Once all associated operations
//   are done, the context is destroyed.
// - There YugaByte objects are bound to Postgres operations. All of these objects' allocated
//   memory will be held by YBCPgMemCtx, whose handle belongs to Postgres MemoryContext. Once all
//   Postgres operations are done, associated YugaByte memory context (YBCPgMemCtx) will be
//   destroyed together with Postgres memory context.
YbcPgMemctx YBCPgCreateMemctx();
YbcStatus YBCPgDestroyMemctx(YbcPgMemctx memctx);
YbcStatus YBCPgResetMemctx(YbcPgMemctx memctx);
void YBCPgDeleteStatement(YbcPgStatement handle);

// Invalidate the sessions table cache.
YbcStatus YBCPgInvalidateCache(uint64_t min_ysql_catalog_version);

// Update the table cache's min_ysql_catalog_version.
YbcStatus YBCPgUpdateTableCacheMinVersion(uint64_t min_ysql_catalog_version);

// Check if initdb has been already run.
YbcStatus YBCPgIsInitDbDone(bool* initdb_done);

// Get gflag TEST_ysql_disable_transparent_cache_refresh_retry
bool YBCGetDisableTransparentCacheRefreshRetry();

// Set global catalog_version to the local tserver's catalog version
// stored in shared memory.
YbcStatus YBCGetSharedCatalogVersion(uint64_t* catalog_version);

// Set per-db catalog_version to the local tserver's per-db catalog version
// stored in shared memory.
YbcStatus YBCGetSharedDBCatalogVersion(
    YbcPgOid db_oid, uint64_t* catalog_version);

// Return the number of rows in pg_yb_catalog_table. Only used when per-db
// catalog version mode is enabled.
YbcStatus YBCGetNumberOfDatabases(uint32_t* num_databases);

// Return true if the pg_yb_catalog_version table has been updated to
// have one row per database.
YbcStatus YBCCatalogVersionTableInPerdbMode(bool* perdb_mode);

YbcStatus YBCGetTserverCatalogMessageLists(
    YbcPgOid db_oid, uint64_t ysql_catalog_version, uint32_t num_catalog_versions,
    YbcCatalogMessageLists* message_lists);

YbcStatus YBCPgSetTserverCatalogMessageList(
    YbcPgOid db_oid, bool is_breaking_change, uint64_t new_catalog_version,
    const YbcCatalogMessageList *message_list);

YbcStatus YBCGetYbSystemTableOid(
    YbcPgOid namespace_oid, const char* table_name, YbcPgOid* table_oid);

// Return auth_key to the local tserver's postgres authentication key stored in shared memory.
uint64_t YBCGetSharedAuthKey();

// Return UUID of the local tserver
const unsigned char* YBCGetLocalTserverUuid();

// Get access to callbacks.
const YbcPgCallbacks* YBCGetPgCallbacks();

int64_t YBCGetPgggateCurrentAllocatedBytes();

int64_t YBCGetActualHeapSizeBytes();

// Call root MemTacker to consume the consumption bytes.
// Return true if MemTracker exists (inited by pggate); otherwise false.
bool YBCTryMemConsume(int64_t bytes);

// Call root MemTacker to release the release bytes.
// Return true if MemTracker exists (inited by pggate); otherwise false.
bool YBCTryMemRelease(int64_t bytes);

YbcStatus YBCGetHeapConsumption(YbcTcmallocStats *desc);

int64_t YBCGetTCMallocSamplingPeriod();
void YBCSetTCMallocSamplingPeriod(int64_t sample_period_bytes);
YbcStatus YBCGetHeapSnapshot(YbcHeapSnapshotSample** snapshot,
                             int64_t* num_samples,
                             bool peak_heap);

void YBCDumpTcMallocHeapProfile(bool peak_heap, size_t max_call_stacks);

// Validate the JWT based on the options including the identity matching based on the identity map.
YbcStatus YBCValidateJWT(const char *token, const YbcPgJwtAuthOptions *options);
YbcStatus YBCFetchFromUrl(const char *url, char **buf);

// Is this node acting as the pg_cron leader?
bool YBCIsCronLeader();
YbcStatus YBCSetCronLastMinute(int64_t last_minute);
YbcStatus YBCGetCronLastMinute(int64_t* last_minute);

int YBCGetXClusterRole(uint32_t db_oid);

//--------------------------------------------------------------------------------------------------
// YB Bitmap Scan Operations
//--------------------------------------------------------------------------------------------------

// returns a void pointer to a std::set
YbcSliceSet YBCBitmapCreateSet();

// Modifies sa to contain the union of `std::set`s sa and sb, and deletes sb. If the item already
// exists in sa, the duplicate item is deleted. Returns the number of new bytes added to sa.
size_t YBCBitmapUnionSet(YbcSliceSet sa, YbcConstSliceSet sb);

// Returns a std::set representing the union of `std::set`s sa and sb.
// One of sa or sb are deleted, and the other is reused.
// Any items not included in the final set are deleted
YbcSliceSet YBCBitmapIntersectSet(YbcSliceSet sa, YbcSliceSet sb);

// Insert the vector of ybctids into the set. Returns the number of new bytes allocated for the
// ybctids.
size_t YBCBitmapInsertYbctidsIntoSet(YbcSliceSet set, YbcConstSliceVector vec);

// Returns a new std::vector containing all the elements of the std::set set. It
// is the caller's responsibility to delete the set.
YbcConstSliceVector YBCBitmapCopySetToVector(YbcConstSliceSet set, size_t *size);

// Returns a vector representing a chunk of the given vector. ybctids are
// shallow copied - their underlying allocations are shared.
YbcConstSliceVector YBCBitmapGetVectorRange(YbcConstSliceVector vec,
                                            size_t start,
                                            size_t length);

void YBCBitmapShallowDeleteVector(YbcConstSliceVector vec);
void YBCBitmapShallowDeleteSet(YbcConstSliceSet set);

// Deletes the set allocation AND all of the ybctids contained within the set.
void YBCBitmapDeepDeleteSet(YbcConstSliceSet set);

size_t YBCBitmapGetSetSize(YbcConstSliceSet set);
size_t YBCBitmapGetVectorSize(YbcConstSliceVector vec);

//--------------------------------------------------------------------------------------------------
// DDL Statements
//--------------------------------------------------------------------------------------------------

// DATABASE ----------------------------------------------------------------------------------------
// Get whether the given database is colocated
// and whether the database is a legacy colocated database.
YbcStatus YBCPgIsDatabaseColocated(const YbcPgOid database_oid, bool *colocated,
                                   bool *legacy_colocated_database);

YbcStatus YBCInsertSequenceTuple(int64_t db_oid,
                                 int64_t seq_oid,
                                 uint64_t ysql_catalog_version,
                                 bool is_db_catalog_version_mode,
                                 int64_t last_val,
                                 bool is_called);

YbcStatus YBCUpdateSequenceTupleConditionally(int64_t db_oid,
                                              int64_t seq_oid,
                                              uint64_t ysql_catalog_version,
                                              bool is_db_catalog_version_mode,
                                              int64_t last_val,
                                              bool is_called,
                                              int64_t expected_last_val,
                                              bool expected_is_called,
                                              bool *skipped);

YbcStatus YBCUpdateSequenceTuple(int64_t db_oid,
                                 int64_t seq_oid,
                                 uint64_t ysql_catalog_version,
                                 bool is_db_catalog_version_mode,
                                 int64_t last_val,
                                 bool is_called,
                                 bool* skipped);

YbcStatus YBCFetchSequenceTuple(int64_t db_oid,
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

YbcStatus YBCReadSequenceTuple(int64_t db_oid,
                               int64_t seq_oid,
                               uint64_t ysql_catalog_version,
                               bool is_db_catalog_version_mode,
                               int64_t *last_val,
                               bool *is_called);

YbcStatus YBCPgNewDropSequence(const YbcPgOid database_oid,
                               const YbcPgOid sequence_oid,
                               YbcPgStatement *handle);

YbcStatus YBCPgExecDropSequence(YbcPgStatement handle);

YbcStatus YBCPgNewDropDBSequences(const YbcPgOid database_oid,
                                  YbcPgStatement *handle);

// Create database.
YbcStatus YBCPgNewCreateDatabase(const char *database_name,
                                 YbcPgOid database_oid,
                                 YbcPgOid source_database_oid,
                                 YbcPgOid next_oid,
                                 const bool colocated,
                                 YbcCloneInfo *yb_clone_info,
                                 YbcPgStatement *handle);
YbcStatus YBCPgExecCreateDatabase(YbcPgStatement handle);

// Drop database.
YbcStatus YBCPgNewDropDatabase(const char *database_name,
                               YbcPgOid database_oid,
                               YbcPgStatement *handle);
YbcStatus YBCPgExecDropDatabase(YbcPgStatement handle);

// Alter database.
YbcStatus YBCPgNewAlterDatabase(const char *database_name,
                               YbcPgOid database_oid,
                               YbcPgStatement *handle);
YbcStatus YBCPgAlterDatabaseRenameDatabase(YbcPgStatement handle, const char *newname);
YbcStatus YBCPgExecAlterDatabase(YbcPgStatement handle);

// Reserve oids.
YbcStatus YBCPgReserveOids(YbcPgOid database_oid,
                           YbcPgOid next_oid,
                           uint32_t count,
                           YbcPgOid *begin_oid,
                           YbcPgOid *end_oid);

// Retrieve the protobuf-based catalog version (now deprecated for new clusters).
YbcStatus YBCPgGetCatalogMasterVersion(uint64_t *version);

YbcStatus YBCPgInvalidateTableCacheByTableId(const char *table_id);

// TABLEGROUP --------------------------------------------------------------------------------------

// Create tablegroup.
YbcStatus YBCPgNewCreateTablegroup(const char *database_name,
                                   YbcPgOid database_oid,
                                   YbcPgOid tablegroup_oid,
                                   YbcPgOid tablespace_oid,
                                   YbcPgStatement *handle);
YbcStatus YBCPgExecCreateTablegroup(YbcPgStatement handle);

// Drop tablegroup.
YbcStatus YBCPgNewDropTablegroup(YbcPgOid database_oid,
                                 YbcPgOid tablegroup_oid,
                                 YbcPgStatement *handle);
YbcStatus YBCPgExecDropTablegroup(YbcPgStatement handle);

// TABLE -------------------------------------------------------------------------------------------
// Create and drop table "database_name.schema_name.table_name()".
// - When "schema_name" is NULL, the table "database_name.table_name" is created.
// - When "database_name" is NULL, the table "connected_database_name.table_name" is created.
YbcStatus YBCPgNewCreateTable(const char *database_name,
                              const char *schema_name,
                              const char *table_name,
                              YbcPgOid database_oid,
                              YbcPgOid table_relfilenode_oid,
                              bool is_shared_table,
                              bool is_sys_catalog_table,
                              bool if_not_exist,
                              YbcPgYbrowidMode ybrowid_mode,
                              bool is_colocated_via_database,
                              YbcPgOid tablegroup_oid,
                              YbcPgOid colocation_id,
                              YbcPgOid tablespace_oid,
                              bool is_matview,
                              YbcPgOid pg_table_oid,
                              YbcPgOid old_relfilenode_oid,
                              bool is_truncate,
                              YbcPgStatement *handle);

YbcStatus YBCPgCreateTableAddColumn(YbcPgStatement handle, const char *attr_name, int attr_num,
                                    const YbcPgTypeEntity *attr_type, bool is_hash, bool is_range,
                                    bool is_desc, bool is_nulls_first);

YbcStatus YBCPgCreateTableSetNumTablets(YbcPgStatement handle, int32_t num_tablets);

YbcStatus YBCPgAddSplitBoundary(YbcPgStatement handle, YbcPgExpr *exprs, int expr_count);

YbcStatus YBCPgExecCreateTable(YbcPgStatement handle);

YbcStatus YBCPgNewAlterTable(YbcPgOid database_oid,
                             YbcPgOid table_relfilenode_oid,
                             YbcPgStatement *handle);

YbcStatus YBCPgAlterTableAddColumn(YbcPgStatement handle, const char *name, int order,
                                   const YbcPgTypeEntity *attr_type,
                                   YbcPgExpr missing_value);

YbcStatus YBCPgAlterTableRenameColumn(YbcPgStatement handle, const char *oldname,
                                      const char *newname);

YbcStatus YBCPgAlterTableDropColumn(YbcPgStatement handle, const char *name);

YbcStatus YBCPgAlterTableSetReplicaIdentity(YbcPgStatement handle, const char identity_type);

YbcStatus YBCPgAlterTableRenameTable(YbcPgStatement handle, const char *newname);

YbcStatus YBCPgAlterTableIncrementSchemaVersion(YbcPgStatement handle);

YbcStatus YBCPgAlterTableSetTableId(
    YbcPgStatement handle, const YbcPgOid database_oid, const YbcPgOid table_relfilenode_oid);

YbcStatus YBCPgAlterTableSetSchema(YbcPgStatement handle, const char *schema_name);

YbcStatus YBCPgExecAlterTable(YbcPgStatement handle);

YbcStatus YBCPgAlterTableInvalidateTableCacheEntry(YbcPgStatement handle);

void YBCPgAlterTableInvalidateTableByOid(
    const YbcPgOid database_oid, const YbcPgOid table_relfilenode_oid);
void YBCPgRemoveTableCacheEntry(
    const YbcPgOid database_oid, const YbcPgOid table_relfilenode_oid);

YbcStatus YBCPgNewDropTable(YbcPgOid database_oid,
                            YbcPgOid table_relfilenode_oid,
                            bool if_exist,
                            YbcPgStatement *handle);

YbcStatus YBCPgNewTruncateTable(YbcPgOid database_oid,
                                YbcPgOid table_relfilenode_oid,
                                YbcPgStatement *handle);

YbcStatus YBCPgExecTruncateTable(YbcPgStatement handle);

YbcStatus YBCPgGetTableDesc(YbcPgOid database_oid,
                            YbcPgOid table_relfilenode_oid,
                            YbcPgTableDesc *handle);

YbcStatus YBCPgGetColumnInfo(YbcPgTableDesc table_desc,
                             int16_t attr_number,
                             YbcPgColumnInfo *column_info);

// Callers should probably use YbGetTableProperties instead.
YbcStatus YBCPgGetTableProperties(YbcPgTableDesc table_desc,
                                  YbcTableProperties properties);

YbcStatus YBCPgDmlModifiesRow(YbcPgStatement handle, bool *modifies_row);

YbcStatus YBCPgSetIsSysCatalogVersionChange(YbcPgStatement handle);

YbcStatus YBCPgSetCatalogCacheVersion(YbcPgStatement handle, uint64_t version);

YbcStatus YBCPgSetDBCatalogCacheVersion(YbcPgStatement handle,
                                        YbcPgOid db_oid,
                                        uint64_t version);
YbcStatus YBCPgSetTablespaceOid(YbcPgStatement handle, uint32_t tablespace_oid);

YbcStatus YBCPgTableExists(const YbcPgOid database_oid,
                           const YbcPgOid table_relfilenode_oid,
                           bool *exists);

YbcStatus YBCPgGetTableDiskSize(YbcPgOid table_relfilenode_oid,
                                YbcPgOid database_oid,
                                int64_t *size,
                                int32_t *num_missing_tablets);

YbcStatus YBCGetSplitPoints(YbcPgTableDesc table_desc,
                            const YbcPgTypeEntity **type_entities,
                            YbcPgTypeAttrs *type_attrs_arr,
                            YbcPgSplitDatum *split_points,
                            bool *has_null, bool *has_gin_null);

// INDEX -------------------------------------------------------------------------------------------
// Create and drop index "database_name.schema_name.index_name()".
// - When "schema_name" is NULL, the index "database_name.index_name" is created.
// - When "database_name" is NULL, the index "connected_database_name.index_name" is created.
YbcStatus YBCPgNewCreateIndex(const char *database_name,
                              const char *schema_name,
                              const char *index_name,
                              YbcPgOid database_oid,
                              YbcPgOid index_relfilenode_oid,
                              YbcPgOid table_relfilenode_oid,
                              bool is_shared_index,
                              bool is_sys_catalog_index,
                              bool is_unique_index,
                              const bool skip_index_backfill,
                              bool if_not_exist,
                              bool is_colocated_via_database,
                              YbcPgOid tablegroup_oid,
                              YbcPgOid colocation_id,
                              YbcPgOid tablespace_oid,
                              YbcPgOid pg_table_oid,
                              YbcPgOid old_relfilenode_oid,
                              YbcPgStatement *handle);

YbcStatus YBCPgCreateIndexAddColumn(YbcPgStatement handle, const char *attr_name, int attr_num,
                                    const YbcPgTypeEntity *attr_type, bool is_hash, bool is_range,
                                    bool is_desc, bool is_nulls_first);

YbcStatus YBCPgCreateIndexSetNumTablets(YbcPgStatement handle, int32_t num_tablets);

YbcStatus YBCPgCreateIndexSetVectorOptions(YbcPgStatement handle, YbcPgVectorIdxOptions *options);

YbcStatus YBCPgCreateIndexSetHnswOptions(
    YbcPgStatement handle, int m, int m0, int ef_construction);

YbcStatus YBCPgExecCreateIndex(YbcPgStatement handle);

YbcStatus YBCPgNewDropIndex(YbcPgOid database_oid,
                            YbcPgOid index_relfilenode_oid,
                            bool if_exist,
                            bool ddl_rollback_enabled,
                            YbcPgStatement *handle);

YbcStatus YBCPgExecPostponedDdlStmt(YbcPgStatement handle);

YbcStatus YBCPgExecDropTable(YbcPgStatement handle);

YbcStatus YBCPgExecDropIndex(YbcPgStatement handle);

YbcStatus YBCPgWaitForBackendsCatalogVersion(
    YbcPgOid dboid,
    uint64_t version,
    pid_t pid,
    int* num_lagging_backends);

YbcStatus YBCPgBackfillIndex(
    const YbcPgOid database_oid,
    const YbcPgOid index_relfilenode_oid);

YbcStatus YBCPgWaitVectorIndexReady(
    const YbcPgOid database_oid,
    const YbcPgOid index_relfilenode_oid);

//--------------------------------------------------------------------------------------------------
// DML statements (select, insert, update, delete, truncate)
//--------------------------------------------------------------------------------------------------

// This function is for specifying the selected or returned expressions.
// - SELECT target_expr1, target_expr2, ...
// - INSERT / UPDATE / DELETE ... RETURNING target_expr1, target_expr2, ...
YbcStatus YBCPgDmlAppendTarget(
    YbcPgStatement handle, YbcPgExpr target, bool is_for_secondary_index);

// Add a WHERE clause condition to the statement.
// Currently only SELECT statement supports WHERE clause conditions.
// Only serialized Postgres expressions are allowed.
// Multiple quals added to the same statement are implicitly AND'ed.
YbcStatus YbPgDmlAppendQual(
    YbcPgStatement handle, YbcPgExpr qual, uint32_t serialization_version,
    bool is_for_secondary_index);

// Add column reference needed to evaluate serialized Postgres expression.
// PgExpr's other than serialized Postgres expressions are inspected and if they contain any
// column references, they are added automatically io the DocDB request. We do not do that
// for serialized postgres expression because it may be expensive to deserialize and analyze
// potentially complex expressions. While expressions are deserialized anyway by DocDB, the
// concern about cost of analysis still stands.
// While optional in regular column reference expressions, column references needed to evaluate
// serialized Postgres expression must contain Postgres data type information. DocDB needs to know
// how to convert values from the DocDB formats to use them to evaluate Postgres expressions.
YbcStatus YbPgDmlAppendColumnRef(
    YbcPgStatement handle, YbcPgExpr colref, bool is_for_secondary_index);

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
YbcStatus YBCPgDmlBindColumn(YbcPgStatement handle, int attr_num, YbcPgExpr attr_value);
YbcStatus YBCPgDmlBindColumnCondBetween(YbcPgStatement handle, int attr_num,
                                        YbcPgExpr attr_value,
                                        bool start_inclusive,
                                        YbcPgExpr attr_value_end,
                                        bool end_inclusive);
YbcStatus YBCPgDmlBindColumnCondIn(YbcPgStatement handle,
                                   YbcPgExpr lhs,
                                   int n_attr_values,
                                   YbcPgExpr *attr_values);
YbcStatus YBCPgDmlBindColumnCondIsNotNull(YbcPgStatement handle, int attr_num);
YbcStatus YBCPgDmlBindRow(
    YbcPgStatement handle, uint64_t ybctid, YbcBindColumn* columns, int count);

YbcStatus YBCPgDmlGetColumnInfo(YbcPgStatement handle, int attr_num, YbcPgColumnInfo* info);

YbcStatus YBCPgDmlBindHashCodes(
    YbcPgStatement handle,
    YbcPgBoundType start_type, uint16_t start_value,
    YbcPgBoundType end_type, uint16_t end_value);

YbcStatus YBCPgDmlBindBounds(
    YbcPgStatement handle, uint64_t lower_bound_ybctid, bool lower_bound_inclusive,
    uint64_t upper_bound_ybctid, bool upper_bound_inclusive);

// For parallel scan only, limit fetch to specified range of ybctids
YbcStatus YBCPgDmlBindRange(YbcPgStatement handle,
                            const char *lower_bound, size_t lower_bound_len,
                            const char *upper_bound, size_t upper_bound_len);

YbcStatus YBCPgDmlAddRowUpperBound(YbcPgStatement handle, int n_col_values,
                                    YbcPgExpr *col_values, bool is_inclusive);

YbcStatus YBCPgDmlAddRowLowerBound(YbcPgStatement handle, int n_col_values,
                                    YbcPgExpr *col_values, bool is_inclusive);

YbcStatus YBCPgDmlSetMergeSortKeys(YbcPgStatement handle, int num_keys,
                                   const YbcSortKey *sort_keys);

// Binding Tables: Bind the whole table in a statement.  Do not use with BindColumn.
YbcStatus YBCPgDmlBindTable(YbcPgStatement handle);

// API for SET clause.
YbcStatus YBCPgDmlAssignColumn(YbcPgStatement handle,
                               int attr_num,
                               YbcPgExpr attr_value);

YbcStatus YBCPgDmlANNBindVector(YbcPgStatement handle, YbcPgExpr vector);

YbcStatus YBCPgDmlANNSetPrefetchSize(YbcPgStatement handle, int prefetch_size);

YbcStatus YBCPgDmlHnswSetReadOptions(YbcPgStatement handle, int ef_search);

// This function is to fetch the targets in YBCPgDmlAppendTarget() from the rows that were defined
// by YBCPgDmlBindColumn().
YbcStatus YBCPgDmlFetch(YbcPgStatement handle, int32_t natts, uint64_t *values, bool *isnulls,
                        YbcPgSysColumns *syscols, bool *has_data);

// Utility method that checks stmt type and calls either exec insert, update, or delete internally.
YbcStatus YBCPgDmlExecWriteOp(YbcPgStatement handle, int32_t *rows_affected_count);

// This function returns the tuple id (ybctid) of a Postgres tuple.
YbcStatus YBCPgBuildYBTupleId(const YbcPgYBTupleIdDescriptor* data, uint64_t *ybctid);

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
YbcStatus YBCPgStartOperationsBuffering();
YbcStatus YBCPgStopOperationsBuffering();
void YBCPgResetOperationsBuffering();
YbcStatus YBCPgFlushBufferedOperations(const YbcFlushDebugContext *debug_context);
YbcStatus YBCPgAdjustOperationsBuffering(int multiple);

YbcStatus YBCPgNewSample(const YbcPgOid database_oid,
                         const YbcPgOid table_relfilenode_oid,
                         YbcPgTableLocalityInfo locality_info,
                         int targrows,
                         double rstate_w,
                         uint64_t rand_state_s0,
                         uint64_t rand_state_s1,
                         YbcPgStatement *handle);

YbcStatus YBCPgSampleNextBlock(YbcPgStatement handle, bool *has_more);

YbcStatus YBCPgExecSample(YbcPgStatement handle, YbcPgExecParameters* exec_params);

YbcStatus YBCPgGetEstimatedRowCount(YbcPgStatement handle, int *sampledrows, double *liverows,
                                    double *deadrows);

// INSERT ------------------------------------------------------------------------------------------

// Allocate block of inserts to the same table.
YbcStatus YBCPgNewInsertBlock(
    YbcPgOid database_oid,
    YbcPgOid table_oid,
    YbcPgTableLocalityInfo locality_info,
    YbcPgTransactionSetting transaction_setting,
    YbcPgStatement *handle);

YbcStatus YBCPgNewInsert(YbcPgOid database_oid,
                         YbcPgOid table_relfilenode_oid,
                         YbcPgTableLocalityInfo locality_info,
                         YbcPgStatement *handle,
                         YbcPgTransactionSetting transaction_setting);

YbcStatus YBCPgExecInsert(YbcPgStatement handle);

YbcStatus YBCPgInsertStmtSetUpsertMode(YbcPgStatement handle);

YbcStatus YBCPgInsertStmtSetWriteTime(YbcPgStatement handle, const uint64_t write_time);

YbcStatus YBCPgInsertStmtSetIsBackfill(YbcPgStatement handle, const bool is_backfill);

// UPDATE ------------------------------------------------------------------------------------------
YbcStatus YBCPgNewUpdate(YbcPgOid database_oid,
                         YbcPgOid table_relfilenode_oid,
                         YbcPgTableLocalityInfo locality_info,
                         YbcPgStatement *handle,
                         YbcPgTransactionSetting transaction_setting);

YbcStatus YBCPgExecUpdate(YbcPgStatement handle);

// DELETE ------------------------------------------------------------------------------------------
YbcStatus YBCPgNewDelete(YbcPgOid database_oid,
                         YbcPgOid table_relfilenode_oid,
                         YbcPgTableLocalityInfo locality_info,
                         YbcPgStatement *handle,
                         YbcPgTransactionSetting transaction_setting);

YbcStatus YBCPgExecDelete(YbcPgStatement handle);

YbcStatus YBCPgDeleteStmtSetIsPersistNeeded(YbcPgStatement handle, const bool is_persist_needed);

// Colocated TRUNCATE ------------------------------------------------------------------------------
YbcStatus YBCPgNewTruncateColocated(YbcPgOid database_oid,
                                    YbcPgOid table_relfilenode_oid,
                                    YbcPgTableLocalityInfo locality_info,
                                    YbcPgStatement *handle,
                                    YbcPgTransactionSetting transaction_setting);

YbcStatus YBCPgExecTruncateColocated(YbcPgStatement handle);

// SELECT ------------------------------------------------------------------------------------------
YbcStatus YBCPgNewSelect(YbcPgOid database_oid,
                         YbcPgOid table_relfilenode_oid,
                         const YbcPgPrepareParameters *prepare_params,
                         YbcPgTableLocalityInfo locality_info,
                         YbcPgStatement *handle);

// Set forward/backward scan direction.
YbcStatus YBCPgSetForwardScan(YbcPgStatement handle, bool is_forward_scan);

// Set prefix length for distinct index scans.
YbcStatus YBCPgSetDistinctPrefixLength(YbcPgStatement handle, int distinct_prefix_length);

YbcStatus YBCPgExecSelect(YbcPgStatement handle, const YbcPgExecParameters *exec_params);

// Gets the ybctids from a request. Returns a vector of all the results.
// If the ybctids would exceed max_bytes, set `exceeded_work_mem` to true.
YbcStatus YBCPgRetrieveYbctids(YbcPgStatement handle, const YbcPgExecParameters *exec_params,
                               int natts, YbcSliceVector *ybctids, size_t *count,
                               bool *exceeded_work_mem);
YbcStatus YBCPgFetchRequestedYbctids(YbcPgStatement handle, const YbcPgExecParameters *exec_params,
                                     YbcConstSliceVector ybctids);
YbcStatus YBCPgBindYbctids(YbcPgStatement handle, int n, uintptr_t* datums);

bool YBCPgIsValidYbctid(uint64_t ybctid);

// Functions----------------------------------------------------------------------------------------
YbcStatus YBCAddFunctionParam(
    YbcPgFunction handle, const char *name, const YbcPgTypeEntity *type_entity, uint64_t datum,
    bool is_null);

YbcStatus YBCAddFunctionTarget(
    YbcPgFunction handle, const char *attr_name, const YbcPgTypeEntity *type_entity,
    const YbcPgTypeAttrs type_attrs);

YbcStatus YBCSRFGetNext(YbcPgFunction handle, uint64_t *values, bool *is_nulls, bool *has_data);

YbcStatus YBCFinalizeFunctionTargets(YbcPgFunction handle);

// Transaction control -----------------------------------------------------------------------------
YbcStatus YBCPgBeginTransaction(int64_t start_time);
YbcStatus YBCPgRecreateTransaction();
YbcStatus YBCPgRestartTransaction();
YbcStatus YBCPgResetTransactionReadPoint(bool is_catalog_snapshot);
YbcStatus YBCPgEnsureReadPoint();
YbcStatus YBCPgRestartReadPoint();
bool YBCIsRestartReadPointRequested();
YbcStatus YBCPgCommitPlainTransaction();
YbcStatus YBCPgCommitPlainTransactionContainingDDL(
    YbcPgOid ddl_db_oid, bool ddl_is_silent_altering);
YbcStatus YBCPgAbortPlainTransaction();
YbcStatus YBCPgSetTransactionIsolationLevel(int isolation);
YbcStatus YBCPgSetTransactionReadOnly(bool read_only);
YbcStatus YBCPgSetTransactionDeferrable(bool deferrable);
YbcStatus YBCPgSetInTxnBlock(bool in_txn_blk);
YbcStatus YBCPgSetReadOnlyStmt(bool read_only_stmt);
YbcStatus YBCPgSetEnableTracing(bool tracing);
YbcStatus YBCPgUpdateFollowerReadsConfig(bool enable_follower_reads, int32_t staleness_ms);
YbcStatus YBCPgSetDdlStateInPlainTransaction();
YbcStatus YBCPgEnterSeparateDdlTxnMode();
bool YBCPgHasWriteOperationsInDdlTxnMode();
YbcStatus YBCPgExitSeparateDdlTxnMode(YbcPgOid db_oid, bool is_silent_altering);
YbcStatus YBCPgClearSeparateDdlTxnMode();
YbcStatus YBCPgSetActiveSubTransaction(uint32_t id);
YbcStatus YBCPgRollbackToSubTransaction(uint32_t id);
double YBCGetTransactionPriority();
YbcTxnPriorityRequirement YBCGetTransactionPriorityType();
YbcStatus YBCPgGetSelfActiveTransaction(YbcPgUuid *txn_id, bool *is_null);
YbcStatus YBCPgActiveTransactions(YbcPgSessionTxnInfo *infos, size_t num_infos);
bool YBCPgIsDdlMode();
bool YBCPgIsDdlModeWithRegularTransactionBlock();
bool YBCCurrentTransactionUsesFastPath();

// System validation -------------------------------------------------------------------------------
// Validate whether placement information is theoretically valid. If check_satisfiable is true,
// also check whether the current set of tservers can satisfy the requested placement.
YbcStatus YBCPgValidatePlacements(
    const char *live_placement_info, const char *read_placement_info,
    bool check_satisfiable);

//--------------------------------------------------------------------------------------------------
// Expressions.

// Column references.
YbcStatus YBCPgNewColumnRef(
    YbcPgStatement stmt, int attr_num, const YbcPgTypeEntity *type_entity,
    bool collate_is_valid_non_c, const YbcPgTypeAttrs *type_attrs,
    YbcPgExpr *expr_handle);

// Constant expressions.
// Construct an actual constant value.
YbcStatus YBCPgNewConstant(
    YbcPgStatement stmt, const YbcPgTypeEntity *type_entity, bool collate_is_valid_non_c,
    const char *collation_sortkey, uint64_t datum, bool is_null, YbcPgExpr *expr_handle);
// Construct a virtual constant value.
YbcStatus YBCPgNewConstantVirtual(
    YbcPgStatement stmt, const YbcPgTypeEntity *type_entity,
    YbcPgDatumKind datum_kind, YbcPgExpr *expr_handle);
// Construct an operator expression on a constant.
YbcStatus YBCPgNewConstantOp(
    YbcPgStatement stmt, const YbcPgTypeEntity *type_entity, bool collate_is_valid_non_c,
    const char *collation_sortkey, uint64_t datum, bool is_null, YbcPgExpr *expr_handle,
    bool is_gt);

// The following update functions only work for constants.
// Overwriting the constant expression with new value.
YbcStatus YBCPgUpdateConstInt2(YbcPgExpr expr, int16_t value, bool is_null);
YbcStatus YBCPgUpdateConstInt4(YbcPgExpr expr, int32_t value, bool is_null);
YbcStatus YBCPgUpdateConstInt8(YbcPgExpr expr, int64_t value, bool is_null);
YbcStatus YBCPgUpdateConstFloat4(YbcPgExpr expr, float value, bool is_null);
YbcStatus YBCPgUpdateConstFloat8(YbcPgExpr expr, double value, bool is_null);
YbcStatus YBCPgUpdateConstText(YbcPgExpr expr, const char *value, bool is_null);
YbcStatus YBCPgUpdateConstBinary(YbcPgExpr expr, const char *value, int64_t bytes, bool is_null);

// Expressions with operators "=", "+", "between", "in", ...
YbcStatus YBCPgNewOperator(
    YbcPgStatement stmt, const char *opname, const YbcPgTypeEntity *type_entity,
    bool collate_is_valid_non_c, YbcPgExpr *op_handle);
YbcStatus YBCPgOperatorAppendArg(YbcPgExpr op_handle, YbcPgExpr arg);

YbcStatus YBCPgNewTupleExpr(
    YbcPgStatement stmt, const YbcPgTypeEntity *tuple_type_entity,
    const YbcPgTypeAttrs *type_attrs, int num_elems,
    YbcPgExpr *elems, YbcPgExpr *expr_handle);

YbcStatus YBCGetDocDBKeySize(uint64_t data, const YbcPgTypeEntity *typeentity,
                            bool is_null, size_t *type_size);

YbcStatus YBCAppendDatumToKey(uint64_t data,  const YbcPgTypeEntity
                            *typeentity, bool is_null, char *key_ptr,
                            size_t *bytes_written);

uint16_t YBCCompoundHash(const char *key, size_t length);

// Referential Integrity Check Caching.
void YBCPgDeleteFromForeignKeyReferenceCache(YbcPgOid table_relfilenode_oid, uint64_t ybctid);
void YBCPgAddIntoForeignKeyReferenceCache(YbcPgOid table_relfilenode_oid, uint64_t ybctid);
YbcStatus YBCPgForeignKeyReferenceCacheDelete(const YbcPgYBTupleIdDescriptor* descr);
YbcStatus YBCForeignKeyReferenceExists(
    const YbcPgYBTupleIdDescriptor* descr, YbcPgTableLocalityInfo locality_info, bool* res);
YbcStatus YBCAddForeignKeyReferenceIntent(
    const YbcPgYBTupleIdDescriptor* descr, YbcPgTableLocalityInfo locality_info,
    bool is_deferred_trigger);
void YBCNotifyDeferredTriggersProcessingStarted();

// Explicit Row-level Locking.
YbcPgExplicitRowLockStatus YBCAddExplicitRowLockIntent(
    YbcPgOid table_relfilenode_oid, uint64_t ybctid, YbcPgOid database_oid,
    const YbcPgExplicitRowLockParams *params, YbcPgTableLocalityInfo locality_info);
YbcPgExplicitRowLockStatus YBCFlushExplicitRowLockIntents();

// INSERT ... ON CONFLICT batching -----------------------------------------------------------------
YbcStatus YBCPgAddInsertOnConflictKey(const YbcPgYBTupleIdDescriptor* tupleid, void* state,
                                      YbcPgInsertOnConflictKeyInfo* info);
YbcStatus YBCPgInsertOnConflictKeyExists(const YbcPgYBTupleIdDescriptor* tupleid, void* state,
                                         YbcPgInsertOnConflictKeyState* res);
YbcStatus YBCPgDeleteInsertOnConflictKey(const YbcPgYBTupleIdDescriptor* tupleid, void* state,
                                         YbcPgInsertOnConflictKeyInfo* info);
YbcStatus YBCPgDeleteNextInsertOnConflictKey(void* state, YbcPgInsertOnConflictKeyInfo* info);
YbcStatus YBCPgAddInsertOnConflictKeyIntent(const YbcPgYBTupleIdDescriptor* tupleid);
void YBCPgClearAllInsertOnConflictCaches();
void YBCPgClearInsertOnConflictCache(void* state);
uint64_t YBCPgGetInsertOnConflictKeyCount(void* state);
//--------------------------------------------------------------------------------------------------

// This is called by initdb. Used to customize some behavior.
void YBCInitFlags();

bool YBCPgIsYugaByteEnabled();

// Sets the specified timeout in the rpc service.
void YBCSetTimeout(int timeout_ms);
void YBCClearTimeout();

void YBCSetLockTimeout(int lock_timeout_ms, void* extra);

void YBCCheckForInterrupts();

//--------------------------------------------------------------------------------------------------
// Thread-Local variables.

void* YBCPgGetThreadLocalCurrentMemoryContext();

void* YBCPgSetThreadLocalCurrentMemoryContext(void *memctx);

void YBCPgResetCurrentMemCtxThreadLocalVars();

void* YBCPgGetThreadLocalStrTokPtr();

void YBCPgSetThreadLocalStrTokPtr(char *new_pg_strtok_ptr);

int YBCPgGetThreadLocalYbExpressionVersion();

void YBCPgSetThreadLocalYbExpressionVersion(int yb_expr_version);

void* YBCPgSetThreadLocalJumpBuffer(void* new_buffer);

void* YBCPgGetThreadLocalJumpBuffer();

void* YBCPgSetThreadLocalErrStatus(void* new_status);

void* YBCPgGetThreadLocalErrStatus();

YbcPgThreadLocalRegexpCache* YBCPgGetThreadLocalRegexpCache();

YbcPgThreadLocalRegexpCache* YBCPgInitThreadLocalRegexpCache(
    size_t buffer_size, YbcPgThreadLocalRegexpCacheCleanup cleanup);

void YBCPgResetCatalogReadTime();
YbcReadHybridTime YBCGetPgCatalogReadTime();

YbcStatus YBCNewGetLockStatusDataSRF(YbcPgFunction *handle);

YbcStatus YBCGetTabletServerHosts(YbcServerDescriptor **tablet_servers, size_t* numservers);

YbcStatus YBCGetIndexBackfillProgress(YbcPgOid* index_oids, YbcPgOid* database_oids,
                                      uint64_t* num_rows_read_from_table,
                                      double* num_rows_backfilled, int num_indexes);

void YBCStartSysTablePrefetchingNoCache();

void YBCStartSysTablePrefetching(
    YbcPgOid database_oid,
    YbcPgLastKnownCatalogVersionInfo catalog_version,
    YbcPgSysTablePrefetcherCacheMode cache_mode);

void YBCStopSysTablePrefetching();

bool YBCIsSysTablePrefetchingStarted();

void YBCRegisterSysTableForPrefetching(
    YbcPgOid database_oid, YbcPgOid table_oid, YbcPgOid index_oid, int row_oid_filtering_attr,
    bool fetch_ybctid);

YbcStatus YBCPrefetchRegisteredSysTables();

YbcStatus YBCPgCheckIfPitrActive(bool* is_active);

YbcStatus YBCIsObjectPartOfXRepl(YbcPgOid database_oid, YbcPgOid table_relfilenode_oid,
                                 bool* is_object_part_of_xrepl);

YbcStatus YBCPgCancelTransaction(const unsigned char* transaction_id);

// Breaks table data into ranges of approximately range_size_bytes each, at most into
// `max_num_ranges`.
// Returns (through callback) list of these ranges end keys and fills current_tserver_ht if not
// nullptr.

// It is guaranteed that returned keys are at most max_key_length bytes.
// lower_bound_key is inclusive, upper_bound_key is exclusive.
// Iff we've reached the end of the table (or upper bound) then empty key is returned as the last
// key.
YbcStatus YBCGetTableKeyRanges(
    YbcPgOid database_oid, YbcPgOid table_relfilenode_oid, const char* lower_bound_key,
    size_t lower_bound_key_size, const char* upper_bound_key, size_t upper_bound_key_size,
    uint64_t max_num_ranges, uint64_t range_size_bytes, bool is_forward, uint32_t max_key_length,
    YbcGetTableKeyRangesCallback callback, void* callback_param);

//--------------------------------------------------------------------------------------------------
// Replication Slots.

YbcStatus YBCPgNewCreateReplicationSlot(const char *slot_name,
                                        const char *plugin_name,
                                        YbcPgOid database_oid,
                                        YbcPgReplicationSlotSnapshotAction snapshot_action,
                                        YbcLsnType lsn_type,
                                        YbcOrderingMode ordering_mode,
                                        YbcPgStatement *handle);
YbcStatus YBCPgExecCreateReplicationSlot(YbcPgStatement handle,
                                         uint64_t *consistent_snapshot_time);

YbcStatus YBCPgListReplicationSlots(
    YbcReplicationSlotDescriptor **replication_slots, size_t *numreplicationslots);

YbcStatus YBCPgGetReplicationSlot(
    const char *slot_name, YbcReplicationSlotDescriptor **replication_slot);

YbcStatus YBCPgNewDropReplicationSlot(const char *slot_name,
                                      YbcPgStatement *handle);
YbcStatus YBCPgExecDropReplicationSlot(YbcPgStatement handle);

YbcStatus YBCPgInitVirtualWalForCDC(
    const char *stream_id, const YbcPgOid database_oid, YbcPgOid *relations, YbcPgOid *relfilenodes,
    size_t num_relations, const YbcReplicationSlotHashRange *slot_hash_range, uint64_t active_pid,
    YbcPgOid *publications, size_t num_publications, bool yb_is_pub_all_tables);

YbcStatus YBCPgGetLagMetrics(const char *stream_id, int64_t *lag_metric);

YbcStatus YBCPgUpdatePublicationTableList(
    const char *stream_id, const YbcPgOid database_oid, YbcPgOid *relations, YbcPgOid *relfilenodes,
    size_t num_relations);

YbcStatus YBCPgDestroyVirtualWalForCDC();

YbcStatus YBCPgGetCDCConsistentChanges(const char *stream_id,
                                       YbcPgChangeRecordBatch **record_batch,
                                       YbcTypeEntityProvider type_entity_provider);

YbcStatus YBCPgUpdateAndPersistLSN(
    const char* stream_id, YbcPgXLogRecPtr restart_lsn_hint, YbcPgXLogRecPtr confirmed_flush,
    YbcPgXLogRecPtr* restart_lsn);

// Get a new OID from the OID allocator of database db_oid.
YbcStatus YBCGetNewObjectId(YbcPgOid db_oid, YbcPgOid* new_oid);

YbcStatus YBCYcqlStatementStats(YbcYCQLStatementStats** stats, size_t* num_stats);

// Active Session History
void YBCStoreTServerAshSamples(
    YbcAshAcquireBufferLock acquire_cb_lock_fn, YbcAshGetNextCircularBufferSlot get_cb_slot_fn,
    uint64_t sample_time);

YbcStatus YBCLocalTablets(YbcPgLocalTabletsDescriptor** tablets, size_t* count);

YbcStatus YBCTabletsMetadata(YbcPgGlobalTabletsDescriptor** tablets, size_t* count);

YbcStatus YBCServersMetrics(YbcPgServerMetricsInfo** serverMetricsInfo, size_t* count);

YbcStatus YBCDatabaseClones(YbcPgDatabaseCloneInfo** databaseClones, size_t* count);

YbcReadPointHandle YBCPgGetCurrentReadPoint();
YbcReadPointHandle YBCPgGetMaxReadPoint();
YbcStatus YBCPgRestoreReadPoint(YbcReadPointHandle read_point);
YbcStatus YBCPgRegisterSnapshotReadTime(
    uint64_t read_time, bool use_read_time, YbcReadPointHandle* handle);

// Records the current statement as a temporary relation DDL statement.
void YBCRecordTempRelationDDL();

// Allow the DDL to modify the pg catalog even if it has been blocked for YSQL major upgrades. This
// should only be used for DDLs that are safe to perform during a YSQL major upgrade.
void YBCDdlEnableForceCatalogModification();

uint64_t YBCGetCurrentHybridTimeLsn();

YbcStatus YBCAcquireAdvisoryLock(
    YbcAdvisoryLockId lock_id, YbcAdvisoryLockMode mode, bool wait, bool session);
YbcStatus YBCReleaseAdvisoryLock(YbcAdvisoryLockId lock_id, YbcAdvisoryLockMode mode);
YbcStatus YBCReleaseAllAdvisoryLocks(uint32_t db_oid);

YbcStatus YBCPgExportSnapshot(
    const YbcPgTxnSnapshot* snapshot, char** snapshot_id,
    const YbcReadPointHandle* explicit_read_point);
YbcStatus YBCPgImportSnapshot(const char* snapshot_id, YbcPgTxnSnapshot* snapshot);

bool YBCPgHasExportedSnapshots();
void YBCPgClearExportedTxnSnapshots();

YbcStatus YBCAcquireObjectLock(YbcObjectLockId lock_id, YbcObjectLockMode mode);

// Indicates if the YB universe is in the process of a YSQL major version upgrade (e.g., pg11 to
// pg15). This will return true before any process has been upgraded to the new version, and will
// return false after the upgrade has been finalized.
// This will return false for regular YB upgrades (both major and minor).
// DevNote: Finalize is a multi-step process involving YsqlMajorCatalog Finalize, AutoFlag Finalize,
// and YsqlUpgrade. This will return false after the AutoFlag Finalize step.
bool YBCPgYsqlMajorVersionUpgradeInProgress();

bool YBCIsBinaryUpgrade();
void YBCSetBinaryUpgrade(bool value);

YbcStatus YBCInitTransaction(const YbcPgInitTransactionData *data);
YbcStatus YBCCommitTransactionIntermediate(const YbcPgInitTransactionData *data);

YbcStatus YBCTriggerRelcacheInitConnection(const char* dbname);

YbcFlushDebugContext YBCMakeFlushDebugContextBeginSubTxn(uint32_t id, const char *name);
YbcFlushDebugContext YBCMakeFlushDebugContextEndSubTxn(uint32_t id);
YbcFlushDebugContext YBCMakeFlushDebugContextGetTxnSnapshot();
YbcFlushDebugContext YBCMakeFlushDebugContextUnbatchableStmtInSqlFunc(
    uint64_t cmd, const char *func_name);
YbcFlushDebugContext YBCMakeFlushDebugContextUnbatchablePlStmt(
    const char *stmt_name, const char *func_name);
YbcFlushDebugContext YBCMakeFlushDebugContextUnbatchableStmtInPlFunc(
    const char *cmd_name, const char *func_name);
YbcFlushDebugContext YBCMakeFlushDebugContextCopyBatch(
    uint64_t tuples_processed, const char *table_name);
YbcFlushDebugContext YBCMakeFlushDebugContextSwithToDbCatalogVersionMode(YbcPgOid db_oid);
YbcFlushDebugContext YBCMakeFlushDebugContextEndOfTopLevelStmt();

#ifdef __cplusplus
}  // extern "C"
#endif
