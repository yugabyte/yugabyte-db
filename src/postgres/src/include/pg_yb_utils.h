/* ----------
 * pg_yb_utils.h
 *
 * Utilities for YugaByte/PostgreSQL integration that have to be defined on the
 * PostgreSQL side.
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/include/pg_yb_utils.h
 * ----------
 */

#ifndef PG_YB_UTILS_H
#define PG_YB_UTILS_H

#include "c.h"
#include "postgres.h"

#include "access/reloptions.h"
#include "catalog/pg_database.h"
#include "common/pg_yb_common.h"
#include "executor/instrument.h"
#include "nodes/parsenodes.h"
#include "nodes/plannodes.h"
#include "tcop/utility.h"
#include "utils/guc.h"
#include "utils/relcache.h"
#include "utils/resowner.h"

#include "yb/yql/pggate/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

#include "yb_ysql_conn_mgr_helper.h"

/*
 * Version of the catalog entries in the relcache and catcache.
 * We (only) rely on a following invariant: If the catalog cache version here is
 * actually the latest (master) version, then the catalog data is indeed up to
 * date. In any other case, we will end up doing a cache refresh anyway.
 * (I.e. cache data is only valid if the version below matches with master's
 * version, otherwise all bets are off and we need to refresh.)
 *
 * So we should handle cases like:
 * 1. yb_catalog_cache_version being behind the actual data in the caches.
 * 2. Data in the caches spanning multiple version (because catalog was updated
 *    during a cache refresh).
 * As long as the invariant above is not violated we should (at most) end up
 * doing a redundant cache refresh.
 *
 * TODO: Improve cache versioning and refresh logic to be more fine-grained to
 * reduce frequency and/or duration of cache refreshes.
 */

#define YB_CATCACHE_VERSION_UNINITIALIZED (0)

/*
 * Check if (const char *)FLAG is non-empty.
 */
#define IS_NON_EMPTY_STR_FLAG(flag) (flag != NULL && flag[0] != '\0')

/*
 * Utility to get the current cache version that accounts for the fact that
 * during a DDL we automatically apply the pending syscatalog changes to
 * the local cache (of the current session).
 * Therefore, if we are within a DDL we return yb_catalog_cache_version + 1.
 * Currently, this is only used during procedure/function compilation so that
 * compilation during CREATE FUNCTION/PROCEDURE is cached correctly.
 * TODO Is there a simpler way to handle this?
 */
extern uint64_t YBGetActiveCatalogCacheVersion();

extern uint64_t YbGetCatalogCacheVersion();

extern void YbUpdateCatalogCacheVersion(uint64_t catalog_cache_version);

extern void YbResetCatalogCacheVersion();

extern uint64_t YbGetLastKnownCatalogCacheVersion();

extern YBCPgLastKnownCatalogVersionInfo
YbGetCatalogCacheVersionForTablePrefetching();

extern void YbUpdateLastKnownCatalogCacheVersion(uint64_t catalog_cache_version);

typedef enum GeolocationDistance {
	ZONE_LOCAL,
	REGION_LOCAL,
	CLOUD_LOCAL,
	INTER_CLOUD,
	UNKNOWN_DISTANCE
} GeolocationDistance;

extern GeolocationDistance get_tablespace_distance (Oid tablespaceoid);
/*
 * Checks whether YugaByte functionality is enabled within PostgreSQL.
 * This relies on pgapi being non-NULL, so probably should not be used
 * in postmaster (which does not need to talk to YB backend) or early
 * in backend process initialization. In those cases the
 * YBIsEnabledInPostgresEnvVar function might be more appropriate.
 */
extern bool IsYugaByteEnabled();

extern bool yb_enable_docdb_tracing;
extern bool yb_read_from_followers;
extern int32_t yb_follower_read_staleness_ms;

/* YB_TODO: Remove this. */
#define YbFirstBootstrapObjectId 10000

/*
 * Iterate over databases and execute a given code snippet.
 * Should terminate with YB_FOR_EACH_DB_END.
 */
#define YB_FOR_EACH_DB(pg_db_tuple) \
	{ \
		/* Shared operations shouldn't be used during initdb. */ \
		Assert(!IsBootstrapProcessingMode()); \
		Relation    pg_db      = table_open(DatabaseRelationId, AccessExclusiveLock); \
		HeapTuple   pg_db_tuple; \
		SysScanDesc pg_db_scan = systable_beginscan( \
			pg_db, \
			InvalidOid /* indexId */, \
			false /* indexOK */, \
			NULL /* snapshot */, \
			0 /* nkeys */, \
			NULL /* key */); \
		while (HeapTupleIsValid(pg_db_tuple = systable_getnext(pg_db_scan))) \
		{ \

#define YB_FOR_EACH_DB_END \
		} \
		systable_endscan(pg_db_scan); \
		table_close(pg_db, AccessExclusiveLock); \
	}

/*
 * Given a relation, checks whether the relation is supported in YugaByte mode.
 */
extern void CheckIsYBSupportedRelation(Relation relation);

extern void CheckIsYBSupportedRelationByKind(char relkind);

/*
 * Given a relation (table) id, returns whether this table is handled by
 * YugaByte: i.e. it is not a temporary or foreign table.
 */
extern bool IsYBRelationById(Oid relid);

extern bool IsYBRelation(Relation relation);

/*
 * Same as IsYBRelation but it additionally includes views on YugaByte
 * relations i.e. views on persistent (non-temporary) tables.
 */
extern bool IsYBBackedRelation(Relation relation);

/*
 * Returns whether a relation is TEMP table
 */
extern bool YbIsTempRelation(Relation relation);

/*
 * Returns whether a relation's attribute is a real column in the backing
 * YugaByte table. (It implies we can both read from and write to it).
 */
extern bool IsRealYBColumn(Relation rel, int attrNum);

/*
 * Returns whether a relation's attribute is a YB system column.
 */
extern bool IsYBSystemColumn(int attrNum);

extern void YBReportFeatureUnsupported(const char *err_msg);

extern AttrNumber YBGetFirstLowInvalidAttrNumber(bool is_yb_relation);

extern AttrNumber YBGetFirstLowInvalidAttributeNumber(Relation relation);

extern AttrNumber YBGetFirstLowInvalidAttributeNumberFromOid(Oid relid);

extern int YBAttnumToBmsIndex(Relation rel, AttrNumber attnum);

extern AttrNumber YBBmsIndexToAttnum(Relation rel, int idx);

extern int YBAttnumToBmsIndexWithMinAttr(AttrNumber minattr, AttrNumber attnum);

extern AttrNumber YBBmsIndexToAttnumWithMinAttr(AttrNumber minattr, int idx);

/*
 * Get primary key columns as bitmap set of a table for real YB columns.
 * Subtracts YBGetFirstLowInvalidAttributeNumber from column attribute numbers.
 */
extern Bitmapset *YBGetTablePrimaryKeyBms(Relation rel);

/*
 * Get primary key columns as bitmap set of a table for real and system YB columns.
 * Subtracts (YBSystemFirstLowInvalidAttributeNumber + 1) from column attribute numbers.
 */
extern Bitmapset *YBGetTableFullPrimaryKeyBms(Relation rel);

/*
 * Return whether a database with oid dbid is a colocated database.
 * legacy_colocated_database is one output parameter. Its value indicates
 * whether database with oid dbid is a legacy colocated database.
 */
extern bool YbIsDatabaseColocated(Oid dbid, bool *legacy_colocated_database);

/*
 * Check if a relation has row triggers that may reference the old row.
 * Specifically for an update/delete DML (where there actually is an old row).
 */
extern bool YBRelHasOldRowTriggers(Relation rel, CmdType operation);

/*
 * Check if a relation has secondary indices.
 */
extern bool YBRelHasSecondaryIndices(Relation relation);

/*
 * Whether to route BEGIN / COMMIT / ROLLBACK to YugaByte's distributed
 * transactions.
 */
extern bool YBTransactionsEnabled();

/*
 * Whether read committed isolation is supported for the cluster or not (via the TServer gflag
 * yb_enable_read_committed_isolation).
 */
extern bool YBIsReadCommittedSupported();

/*
 * Whether the current txn is of READ COMMITTED (or READ UNCOMMITTED) isolation level, and it uses
 * the new READ COMMITTED implementation instead of mapping to REPEATABLE READ level. The latter
 * condition is dictated by the value of gflag yb_enable_read_committed_isolation.
 */
extern bool IsYBReadCommitted();

/*
 * Whether wait-queues are enabled for the cluster or not (via the TServer gflag
 * enable_wait_queues).
 */
extern bool YBIsWaitQueueEnabled();

/*
 * Whether the per database catalog version mode is enabled.
 */
extern bool YBIsDBCatalogVersionMode();

/*
 * Whether we need to preload additional catalog tables.
 */
extern bool YbNeedAdditionalCatalogTables();

/*
 * Since DDL metadata in master DocDB and postgres system tables is not modified
 * in an atomic fashion, it is possible that we could have a table existing in
 * postgres metadata but not in DocDB. In the case of a delete it is really
 * problematic, since we can't delete the table nor can we create a new one with
 * the same name. So in this case we just ignore the DocDB 'NotFound' error and
 * delete our metadata.
 */
extern void HandleYBStatusIgnoreNotFound(YBCStatus status, bool *not_found);

/*
 * Handle YBStatus while logging a custom error for DocDB 'NotFound' error.
 */
extern void
HandleYBStatusWithCustomErrorForNotFound(YBCStatus status,
										 const char *message_for_not_found);

/*
 * Same as HandleYBStatus but delete the table description first if the
 * status is not ok.
 */
extern void HandleYBTableDescStatus(YBCStatus status, YBCPgTableDesc table);
/*
 * YB initialization that needs to happen when a PostgreSQL backend process
 * is started. Reports errors using ereport.
 */
extern void YBInitPostgresBackend(const char *program_name,
								  const char *db_name,
								  const char *user_name,
								  uint64_t *session_id);

/*
 * This should be called on all exit paths from the PostgreSQL backend process.
 * Only main PostgreSQL backend thread is expected to call this.
 */
extern void YBOnPostgresBackendShutdown();

/*
 * Signals PgTxnManager to recreate the transaction. This is used when we need
 * to restart a transaction that failed due to a transaction conflict error.
 */
extern void YBCRecreateTransaction();

/*
 * Signals PgTxnManager to restart current transaction - pick a new read point, etc.
 * This relies on transaction/session read time already being marked for restart by YB layer.
 */
extern void YBCRestartTransaction();

/*
 * Commits the current YugaByte-level transaction (if any).
 */
extern void YBCCommitTransaction();

/*
 * Aborts the current YugaByte-level transaction.
 */
extern void YBCAbortTransaction();

extern void YBCSetActiveSubTransaction(SubTransactionId id);

extern void YBCRollbackToSubTransaction(SubTransactionId id);

/*
 * Return true if we want to allow PostgreSQL's own locking. This is needed
 * while system tables are still managed by PostgreSQL.
 */
extern bool YBIsPgLockingEnabled();

/*
 * Get the type ID of a real or virtual attribute (column).
 * Returns InvalidOid if the attribute number is invalid.
 */
extern Oid GetTypeId(int attrNum, TupleDesc tupleDesc);

/*
 * Return a string representation of the given type id, or say it is unknown.
 * What is returned is always a static C string constant.
 */
extern const char* YBPgTypeOidToStr(Oid type_id);

/*
 * Return a string representation of the given PgDataType, or say it is unknown.
 * What is returned is always a static C string constant.
 */
extern const char* YBCPgDataTypeToStr(YBCPgDataType yb_type);

/*
 * Report an error saying the given type as not supported by YugaByte.
 */
extern void YBReportTypeNotSupported(Oid type_id);

/*
 * Log whether or not YugaByte is enabled.
 */
extern void YBReportIfYugaByteEnabled();

#define YB_REPORT_TYPE_NOT_SUPPORTED(type_id) do { \
		Oid computed_type_id = type_id; \
		ereport(ERROR, \
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED), \
					errmsg("Type not yet supported in YugaByte: %d (%s)", \
						computed_type_id, YBPgTypeOidToStr(computed_type_id)))); \
	} while (0)

/*
 * Determines if PostgreSQL should restart all child processes if one of them
 * crashes. This behavior usually shows up in the log like so:
 *
 * WARNING:  terminating connection because of crash of another server process
 * DETAIL:  The postmaster has commanded this server process to roll back the
 *          current transaction and exit, because another server process exited
 *          abnormally and possibly corrupted shared memory.
 *
 * However, we want to avoid this behavior in some cases, e.g. when our test
 * framework is trying to intentionally cause core dumps of stuck backend
 * processes and analyze them. Disabling this behavior is controlled by setting
 * the YB_PG_NO_RESTART_ALL_CHILDREN_ON_CRASH_FLAG_PATH variable to a file path,
 * which could be created or deleted at run time, and its existence is always
 * checked.
 */
bool YBShouldRestartAllChildrenIfOneCrashes();

/*
 * These functions help indicating if we are connected to template0 or template1.
 */
void YbSetConnectedToTemplateDb();
bool YbIsConnectedToTemplateDb();

/*
 * Whether every ereport of the ERROR level and higher should log a stack trace.
 */
bool YBShouldLogStackTraceOnError();

/*
 * Converts the PostgreSQL error level as listed in elog.h to a string. Always
 * returns a static const char string.
 */
const char* YBPgErrorLevelToString(int elevel);

/*
 * Get the database name for a relation id (accounts for system databases and
 * shared relations)
 */
const char* YBCGetDatabaseName(Oid relid);

/*
 * Get the schema name for a schema oid (accounts for system namespaces)
 */
const char* YBCGetSchemaName(Oid schemaoid);

/*
 * Get the real database id of a relation. For shared relations
 * (which are meant to be accessible from all databases), it will be template1.
 */
Oid YBCGetDatabaseOid(Relation rel);
Oid YBCGetDatabaseOidByRelid(Oid relid);
Oid YBCGetDatabaseOidFromShared(bool relisshared);

/*
 * Raise an unsupported feature error with the given message and
 * linking to the referenced issue (if any).
 */
void YBRaiseNotSupported(const char *msg, int issue_no);
void YBRaiseNotSupportedSignal(const char *msg, int issue_no, int signal_level);

/*
 * Return the value of (base ^ exponent) bounded by the upper limit.
 */
extern double PowerWithUpperLimit(double base, int exponent, double upper_limit);

/*
 * Return whether to use wholerow junk attribute for YB relations.
 */
extern bool YbUseWholeRowJunkAttribute(Relation relation,
									   Bitmapset *updatedCols,
									   CmdType operation,
									   List *returningList);

/*
 * Return whether to use scanned "old" tuple to reconstruct the new tuple during
 * UPDATE operations for YB relations. See function definition for details.
 */
extern bool YbUseScanTupleInUpdate(Relation relation, Bitmapset *updatedCols, List *returningList);

/*
 * Return whether the returning list for an UPDATE statement is a subset of the columns being
 * updated by the UPDATE query.
 */
bool YbReturningListSubsetOfUpdatedCols(Relation rel, Bitmapset *updatedCols, List *returningList);

//------------------------------------------------------------------------------
// YB GUC variables.

/**
 * YSQL guc variables that can be used to toggle yugabyte features.
 * See also the corresponding entries in guc.c.
 */

/* Enables tables/indexes to be created WITH (table_oid = x). */
extern bool yb_enable_create_with_table_oid;

/*
 * During CREATE INDEX, the delay between stages, from
 * - indislive=true to indisready=true
 * - indisready=true to launching backfill
 */
extern int yb_index_state_flags_update_delay;

/*
 * Enables expression pushdown.
 * If true, planner sends supported expressions to DocDB for evaluation
 */
extern bool yb_enable_expression_pushdown;

/*
 * Enables distinct pushdown.
 * If true, send supported DISTINCT operations to DocDB
 */
extern bool yb_enable_distinct_pushdown;

/*
 * Enables index aggregate pushdown (IndexScan only, not IndexOnlyScan).
 * If true, request aggregated results from DocDB when possible.
 */
extern bool yb_enable_index_aggregate_pushdown;

/*
 * YSQL guc variable that is used to enable the use of Postgres's selectivity
 * functions and YSQL table statistics.
 * e.g. 'SET yb_enable_optimizer_statistics = true'
 * See also the corresponding entries in guc.c.
 */
extern bool yb_enable_optimizer_statistics;

/*
 * If true then condition rechecking is bypassed at YSQL if the condition is
 * bound to DocDB.
 */
extern bool yb_bypass_cond_recheck;

/*
 * Enables nonbreaking DDL mode in which a DDL statement is not considered as
 * a "breaking catalog change" and therefore will not cause running transactions
 * to abort.
 */
extern bool yb_make_next_ddl_statement_nonbreaking;

/*
 * Allows capability to disable prefetching in a PLPGSQL FOR loop over a query.
 * This is introduced for some test(s) with lazy evaluation in READ COMMITTED
 * isolation that require the read rpcs to be issued over multiple invocations
 * of the lazily evaluable function. If prefetching is enabled, the first
 * invocation could possibly issue read rpcs to all tablets until the
 * specified number of rows is prefetched -- in which case no read rpcs would be
 * issued in later invocations.
 */
extern bool yb_plpgsql_disable_prefetch_in_for_query;

/*
 * Allow nextval() to fetch the value range and advance the sequence value in a
 * single operation.
 * If disabled, nextval() reads sequence value first, advances it and apply the
 * new value, which may fail due to concurrent modification and has to be
 * retried.
 */
extern bool yb_enable_sequence_pushdown;

/*
 * Disable waiting for backends to have up-to-date catalog version.
 */
extern bool yb_disable_wait_for_backends_catalog_version;

/*
 * Enables YB cost model for Sequential and Index scans
 */
extern bool yb_enable_base_scans_cost_model;

/*
 * Total timeout for waiting for backends to have up-to-date catalog version.
 */
extern int yb_wait_for_backends_catalog_version_timeout;

/*
 * If true, we will always prefer batched nested loop join plans over nested
 * loop join plans.
 */
extern bool yb_prefer_bnl;

/*
 * If true, all fields that vary from run to run are hidden from the
 * output of EXPLAIN.
 */
extern bool yb_explain_hide_non_deterministic_fields;

/*
 * Enables scalar array operation pushdown.
 * If true, planner sends supported expressions to DocDB for evaluation
 */
extern bool yb_enable_saop_pushdown;

/*
 * Enables the use of TOAST compression for the Postgres catcache.
*/
extern int yb_toast_catcache_threshold;

//------------------------------------------------------------------------------
// GUC variables needed by YB via their YB pointers.
extern int StatementTimeout;

//------------------------------------------------------------------------------
// YB Debug utils.

/**
 * YSQL guc variables that can be used to toggle yugabyte debug features.
 * e.g. 'SET yb_debug_report_error_stacktrace=true' and
 *      'RESET yb_debug_report_error_stacktrace'.
 * See also the corresponding entries in guc.c.
 */

/* Add stacktrace information to every YSQL error. */
extern bool yb_debug_report_error_stacktrace;

/* Log cache misses and cache refresh events. */
extern bool yb_debug_log_catcache_events;

/*
 * Log automatic statement (or transaction) restarts such as read-restarts and
 * schema-version restarts (e.g. catalog version mismatch errors).
 */
extern bool yb_debug_log_internal_restarts;

/*
 * Relaxes some internal sanity checks for system catalogs to allow creating them.
 */
extern bool yb_test_system_catalogs_creation;

/*
 * If set to true, next DDL operation (only creating a relation for now) will fail,
 * resetting this back to false.
 */
extern bool yb_test_fail_next_ddl;

/*
 * If set to true, next increment catalog version operation will fail and
 * reset this back to false.
 */
extern bool yb_test_fail_next_inc_catalog_version;

/*
 * This number times disable_cost is added to the cost for some unsupported
 * ybgin index scans.
 */
extern double yb_test_ybgin_disable_cost_factor;

/*
 * Block the given index creation phase.
 * - "indisready": index state change to indisready
 *   (not supported for non-concurrent)
 * - "backfill": index backfill phase
 * - "postbackfill": post-backfill operations like validation and event triggers
 */
extern char *yb_test_block_index_phase;

/*
 * Same as above, but fails the operation at the given stage instead of
 * blocking.
 */
extern char *yb_test_fail_index_state_change;

/*
 * If set to true, any DDLs that rewrite tables/indexes will fail after
 * the new table is created.
 */
extern bool yb_test_fail_table_rewrite_after_creation;

/* GUC variable yb_test_stay_in_global_catalog_version_mode. */
extern bool yb_test_stay_in_global_catalog_version_mode;

/*
 * If set to true, any DDLs that rewrite tables/indexes will not drop the
 * old relfilenode/DocDB table.
 */
extern bool yb_test_table_rewrite_keep_old_table;

/*
 * Denotes whether DDL operations touching DocDB system catalog will be rolled
 * back upon failure. These two GUC variables are used together. See comments
 * for the gflag --ysql_enable_ddl_atomicity_infra in common_flags.cc.
*/
extern bool yb_enable_ddl_atomicity_infra;
extern bool yb_ddl_rollback_enabled;
static inline bool
YbDdlRollbackEnabled () {
	return yb_enable_ddl_atomicity_infra && yb_ddl_rollback_enabled;
}

extern bool yb_use_hash_splitting_by_default;

/*
 * GUC to allow user to silence the error saying that advisory locks are not
 * supported.
 */
extern bool yb_silence_advisory_locks_not_supported_error;

/*
 * See also ybc_util.h which contains additional such variable declarations for
 * variables that are (also) used in the pggate layer.
 * Currently: yb_debug_log_docdb_requests.
 */

/*
 * Get a string representation of a datum (given its type).
 */
extern const char* YBDatumToString(Datum datum, Oid typid);

/*
 * Get a string representation of a tuple (row) given its tuple description (schema).
 */
extern const char* YbHeapTupleToString(HeapTuple tuple, TupleDesc tupleDesc);

/* Get a string representation of a bitmapset (for debug purposes only!) */
extern const char* YbBitmapsetToString(Bitmapset *bms);

/*
 * Checks if the master thinks initdb has already been done.
 */
bool YBIsInitDbAlreadyDone();

int YBGetDdlNestingLevel();
void YbSetIsGlobalDDL();

typedef enum YbSysCatalogModificationAspect
{
	YB_SYS_CAT_MOD_ASPECT_ALTERING_EXISTING_DATA = 1,
	YB_SYS_CAT_MOD_ASPECT_VERSION_INCREMENT = 2,
	YB_SYS_CAT_MOD_ASPECT_BREAKING_CHANGE = 4
} YbSysCatalogModificationAspect;

typedef enum YbDdlMode
{
	YB_DDL_MODE_NO_ALTERING = 0,

	YB_DDL_MODE_SILENT_ALTERING =
		YB_SYS_CAT_MOD_ASPECT_ALTERING_EXISTING_DATA,

	YB_DDL_MODE_VERSION_INCREMENT =
		YB_SYS_CAT_MOD_ASPECT_ALTERING_EXISTING_DATA |
		YB_SYS_CAT_MOD_ASPECT_VERSION_INCREMENT,

	YB_DDL_MODE_BREAKING_CHANGE =
		YB_SYS_CAT_MOD_ASPECT_ALTERING_EXISTING_DATA |
		YB_SYS_CAT_MOD_ASPECT_VERSION_INCREMENT |
		YB_SYS_CAT_MOD_ASPECT_BREAKING_CHANGE
} YbDdlMode;

void YBIncrementDdlNestingLevel(YbDdlMode mode);
void YBDecrementDdlNestingLevel();

typedef struct YbDdlModeOptional
{
	bool has_value;
	YbDdlMode value;
} YbDdlModeOptional;

YbDdlModeOptional YbGetDdlMode(
	PlannedStmt *pstmt, ProcessUtilityContext context);

extern void YBBeginOperationsBuffering();
extern void YBEndOperationsBuffering();
extern void YBResetOperationsBuffering();
extern void YBFlushBufferedOperations();

bool YBEnableTracing();
bool YBReadFromFollowersEnabled();
int32_t YBFollowerReadStalenessMs();

/*
 * Allocates YBCPgYBTupleIdDescriptor with nattrs arguments by using palloc.
 * Resulted object can be released with pfree.
 */
YBCPgYBTupleIdDescriptor* YBCCreateYBTupleIdDescriptor(Oid db_oid, Oid table_relfilenode_oid,
	int nattrs);
void YBCFillUniqueIndexNullAttribute(YBCPgYBTupleIdDescriptor* descr);

/*
 * Lazily loads yb_table_properties field in Relation.
 *
 * YbGetTableProperties expects the table to be present in the DocDB, while
 * YbTryGetTableProperties queries the DocDB first and returns NULL if not found.
 *
 * Both calls returns the same yb_table_properties field from Relation
 * for convenience (can be NULL for the second call).
 *
 * Note that these calls will rarely send out RPC because of
 * Relation/TableDesc cache.
 *
 * TODO(alex):
 *    An optimization we could use is to amend RelationBuildDesc or
 *    ScanPgRelation to do a custom RPC fetching YB properties as well.
 *    However, TableDesc cache makes this low-priority.
 */
YbTableProperties YbGetTableProperties(Relation rel);
YbTableProperties YbGetTablePropertiesById(Oid relid);
YbTableProperties YbTryGetTableProperties(Relation rel);

typedef enum YbTableDistribution
{
	YB_SYSTEM,
	YB_COLOCATED,
	YB_HASH_SHARDED,
	YB_RANGE_SHARDED
} YbTableDistribution;
YbTableDistribution YbGetTableDistribution(Oid relid);

/*
 * Check whether the given libc locale is supported in YugaByte mode.
 */
bool YBIsSupportedLibcLocale(const char *localebuf);

/* Spin wait while test guc var actual equals expected. */
extern void YbTestGucBlockWhileStrEqual(char **actual, const char *expected,
										const char *msg);

extern void YbTestGucFailIfStrEqual(char *actual, const char *expected);

extern int YbGetNumberOfFunctionOutputColumns(Oid func_oid);

char *YBDetailSorted(char *input);

/*
 * For given collation, type and value, setup collation info.
 */
void YBGetCollationInfo(
	Oid collation_id,
	const YBCPgTypeEntity *type_entity,
	Datum datum,
	bool is_null,
	YBCPgCollationInfo *collation_info);

/*
 * Setup collation info in attr.
 */
void YBSetupAttrCollationInfo(YBCPgAttrValueDescriptor *attr, const YBCPgColumnInfo *column_info);

/*
 * Check whether the collation is a valid non-C collation.
 */
bool YBIsCollationValidNonC(Oid collation_id);

/*
 * For the column 'attr_num' and its collation id, return the collation id that
 * will be used to do collation encoding. For example, if the column 'attr_num'
 * represents a non-key column, we do not need to store the collation key and
 * this function will return InvalidOid which will disable collation encoding
 * for the column string value.
 */
Oid YBEncodingCollation(YBCPgStatement handle, int attr_num, Oid attcollation);

/*
 * Check whether the user ID is of a user who has the yb_extension role.
 */
bool IsYbExtensionUser(Oid member);

/*
 * Check whether the user ID is of a user who has the yb_fdw role.
 */
bool IsYbFdwUser(Oid member);

/*
 * Array of IDs of non-immutable functions that do not perform any database
 * lookups or writes. When these functions are used in an INSERT/UPDATE/DELETE
 * statement, they will not cause the actual modify statement to become a
 * cross shard operation.
 */
extern const uint32 yb_funcs_safe_for_pushdown[];

/*
 * These functions are unsafe to run in a multi-threaded environment. There is
 * no specific attribute that identifies them as such, so we have to manually
 * identify them.
 */
extern const uint32 yb_funcs_unsafe_for_pushdown[];

/*
 * Number of functions in 'yb_funcs_safe_for_pushdown' above.
 */
extern const int yb_funcs_safe_for_pushdown_count;

/*
 * Number of functions in 'yb_funcs_unsafe_for_pushdown' above.
 */
extern const int yb_funcs_unsafe_for_pushdown_count;

/**
 * Use the YB_PG_PDEATHSIG environment variable to set the signal to be sent to
 * the current process in case the parent process dies. This is Linux-specific
 * and can only be done from the child process (the postmaster process). The
 * parent process here is yb-master or yb-tserver.
 */
void YBSetParentDeathSignal();

/**
 * Given a relation, return it's relfilenode OID. In YB, the relfilenode OID
 * maps to the relation's DocDB table ID. Note: if the table has not
 * previously been rewritten, this function returns the OID of the table.
 */
Oid YbGetRelfileNodeId(Relation relation);

/**
 * Given a relation ID, return the relation's relfilenode OID.
 */
Oid YbGetRelfileNodeIdFromRelId(Oid relationId);
/*
 * Check whether the user ID is of a user who has the yb_db_admin role.
 */
bool IsYbDbAdminUser(Oid member);

/*
 * Check whether the user ID is of a user who has the yb_db_admin role
 * (excluding superusers).
 */
bool IsYbDbAdminUserNosuper(Oid member);

/*
 * Check unsupported system columns and report error.
 */
void YbCheckUnsupportedSystemColumns(int attnum, const char *colname, RangeTblEntry *rte);

/*
 * Register system table for prefetching.
 */
void YbRegisterSysTableForPrefetching(int sys_table_id);
void YbTryRegisterCatalogVersionTableForPrefetching();

/*
 * Returns true if the relation is a non-system relation in the same region.
 */
bool YBCIsRegionLocal(Relation rel);

/*
 * Return NULL for all non-range-partitioned tables.
 * Return an empty string for one-tablet range-partitioned tables.
 * Return SPLIT AT VALUES clause string (i.e. SPLIT AT VALUES(...))
 * for all range-partitioned tables with more than one tablet.
 * Return an empty string when duplicate split points exist
 * after tablet splitting.
 * Return an emptry string when a NULL value is present in split points
 * after tablet splitting.
 */
extern Datum yb_get_range_split_clause(PG_FUNCTION_ARGS);

extern bool check_yb_xcluster_consistency_level(char **newval, void **extra,
												GucSource source);
extern void assign_yb_xcluster_consistency_level(const char *newval,
												 void		*extra);

/*
 * Updates the session stats snapshot with the collected stats and copies the
 * difference to the query execution context's instrumentation.
 */
void YbUpdateSessionStats(YbInstrumentation *yb_instr);

extern bool check_yb_read_time(char **newval, void **extra, GucSource source);
extern void assign_yb_read_time(const char *newval, void *extra);

/* GUC assign hook for max_replication_slots */
extern void yb_assign_max_replication_slots(int newval, void *extra);

/*
 * Refreshes the session stats snapshot with the collected stats. This function
 * is to be invoked before the query has started its execution.
 */
void YbRefreshSessionStatsBeforeExecution();

/*
 * Refreshes the session stats snapshot with the collected stats. This function
 * is to be invoked when during/after query execution.
 */
void YbRefreshSessionStatsDuringExecution();
/*
 * Updates the global flag indicating whether RPC requests to the underlying
 * storage layer need to be timed.
 */
void YbToggleSessionStatsTimer(bool timing_on);

/**
 * Update the global flag indicating what metric changes to capture and return
 * from the tserver to PG.
 */
void YbSetMetricsCaptureType(YBCPgMetricsCaptureType metrics_capture);

/*
 * If the tserver gflag --ysql_disable_server_file_access is set to
 * true, then prevent any server file writes/reads/execution.
 */
extern void YBCheckServerAccessIsAllowed();

void YbSetCatalogCacheVersion(YBCPgStatement handle, uint64_t version);

uint64_t YbGetSharedCatalogVersion();
uint32_t YbGetNumberOfDatabases();
bool YbCatalogVersionTableInPerdbMode();

/*
 * This function maps the user intended row-level lock policy i.e., "pg_wait_policy" of
 * type enum LockWaitPolicy to the "docdb_wait_policy" of type enum WaitPolicy as defined in
 * common.proto.
 *
 * The semantics of the WaitPolicy enum differ slightly from those of the traditional LockWaitPolicy
 * in Postgres, as explained in common.proto. This is for historical reasons. WaitPolicy in
 * common.proto was created as a copy of LockWaitPolicy to be passed to the Tserver to help in
 * appropriate conflict-resolution steps for the different row-level lock policies.
 *
 * In isolation level SERIALIZABLE, this function sets docdb_wait_policy to WAIT_BLOCK as
 * this is the only policy currently supported for SERIALIZABLE.
 *
 * However, if wait queues aren't enabled in the following cases:
 *  * Isolation level SERIALIZABLE
 *  * The user requested LockWaitBlock in another isolation level
 * this function sets docdb_wait_policy to WAIT_ERROR (which actually uses the "Fail on Conflict"
 * conflict management policy instead of "no wait" semantics, as explained in "enum WaitPolicy" in
 * common.proto).
 *
 * Logs a warning:
 * 1. In isolation level SERIALIZABLE for a pg_wait_policy of LockWaitSkip and LockWaitError
 *    because SKIP LOCKED and NOWAIT are not supported yet.
 * 2. In isolation level REPEATABLE READ for a pg_wait_policy of LockWaitError because NOWAIT
 *    is not supported.
 */
void YBSetRowLockPolicy(int *docdb_wait_policy, LockWaitPolicy pg_wait_policy);

const char *yb_fetch_current_transaction_priority(void);

void GetStatusMsgAndArgumentsByCode(
	const uint32_t pg_err_code, uint16_t txn_err_code, YBCStatus s,
	const char **msg_buf, size_t *msg_nargs, const char ***msg_args,
	const char **detail_buf, size_t *detail_nargs, const char ***detail_args);

bool YbIsBatchedExecution();
void YbSetIsBatchedExecution(bool value);

/* Check if the given column is a part of the relation's key. */
bool YbIsColumnPartOfKey(Relation rel, const char *column_name);

/* Get a relation's split options. */
OptSplit *YbGetSplitOptions(Relation rel);

#define HandleYBStatus(status) \
	HandleYBStatusAtErrorLevel(status, ERROR)

/*
 * Macro to convert DocDB Status to Postgres error.
 * It is generally based on the ereport macro, it makes a sequence of errxxx()
 * function calls, where errstart() comes the first and errfinish() the last.
 *
 * The error location info (file name, line number, function name) comes from
 * the status, so we need lower level access, that's why we can not use ereport
 * here. Also we don't need ereport's flexibility, as we support transfer of
 * limited subset of Postgres error fields.
 *
 * We require the compiler to support __builtin_constant_p.
 */
#ifdef HAVE__BUILTIN_CONSTANT_P
#define HandleYBStatusAtErrorLevel(status, elevel) \
	do \
	{ \
		AssertMacro(!IsMultiThreadedMode()); \
		YBCStatus _status = (status); \
		if (_status) \
		{ \
			const int 		adjusted_elevel = YBCStatusIsFatalError(_status) ? FATAL : elevel; \
			const uint32_t	pg_err_code = YBCStatusPgsqlError(_status); \
			const uint16_t	txn_err_code = YBCStatusTransactionError(_status); \
			const char	   *filename = YBCStatusFilename(_status); \
			int				lineno = YBCStatusLineNumber(_status); \
			const char	   *funcname = YBCStatusFuncname(_status); \
			const char	   *msg_buf = NULL; \
			const char	   *detail_buf = NULL; \
			size_t		    msg_nargs = 0; \
			size_t		    detail_nargs = 0; \
			const char	  **msg_args = NULL; \
			const char	  **detail_args = NULL; \
			GetStatusMsgAndArgumentsByCode(pg_err_code, txn_err_code, _status, \
										   &msg_buf, &msg_nargs, &msg_args, \
										   &detail_buf, &detail_nargs, \
										   &detail_args); \
			YBCFreeStatus(_status); \
			if (errstart(adjusted_elevel, TEXTDOMAIN)) \
			{ \
				Assert(msg_buf); \
				yb_errmsg_from_status(msg_buf, msg_nargs, msg_args); \
				if (detail_buf) \
					yb_errdetail_from_status(detail_buf, detail_nargs, detail_args); \
				yb_set_pallocd_error_file_and_func(filename, funcname); \
				errcode(pg_err_code); \
				yb_txn_errcode(txn_err_code); \
				errhidecontext(true); \
				errfinish(NULL, \
						  lineno > 0 ? lineno : __LINE__, \
						  NULL); \
				if (__builtin_constant_p(elevel) && (elevel) >= ERROR) \
					pg_unreachable(); \
			} \
			else \
			{ \
				if (filename) \
					pfree((void*) filename); \
				if (funcname) \
					pfree((void*) funcname); \
			} \
		} \
	} while (0)
#endif

/*
 * Increments a tally of sticky objects (TEMP TABLES/WITH HOLD CURSORS)
 * maintained for every transaction.
 */
extern void increment_sticky_object_count();

/*
 * Decrements a tally of sticky objects (TEMP TABLES/WITH HOLD CURSORS)
 * maintained for every transaction.
 */
extern void decrement_sticky_object_count();

/*
 * Check if there exists a database object that requires a sticky connection.
 */
extern bool YbIsStickyConnection(int *change);

/*
 * Creates a shallow copy of the pointer list.
 */
extern void** YbPtrListToArray(const List* str_list, size_t* length);

/*
 * Reads the contents of the given file assuming that the filename is an
 * absolute path.
 *
 * The file contents are returned as a single palloc'd chunk with an extra \0
 * byte added to the end.
 */
extern char* YbReadWholeFile(const char *filename, int* length, int elevel);

extern bool yb_use_tserver_key_auth;

extern bool yb_use_tserver_key_auth_check_hook(bool *newval,
		void **extra, GucSource source);

extern void YbATCopyPrimaryKeyToCreateStmt(Relation rel,
										   Relation pg_constraint,
										   CreateStmt *create_stmt);

extern void YbIndexSetNewRelfileNode(Relation indexRel, Oid relfileNodeId,
									 bool yb_copy_split_options);

/*
 * Returns the ordering type for a primary key. By default, the first element of
 * YB relations are sorted by HASH, unless Postgres sorting is set, or the table
 * is colocated.
 */
extern SortByDir YbSortOrdering(SortByDir ordering, bool is_colocated, bool is_tablegroup, bool is_first_key);

extern void YbGetRedactedQueryString(const char* query, int query_len,
									 const char** redacted_query, int* redacted_query_len);

extern void YbRelationSetNewRelfileNode(Relation rel, Oid relfileNodeId,
										bool yb_copy_split_options,
										bool is_truncate);

extern Relation YbGetRelationWithOverwrittenReplicaIdentity(Oid relid,
															char replident);

extern void YBCUpdateYbReadTimeAndInvalidateRelcache(uint64_t read_time);
#endif /* PG_YB_UTILS_H */
