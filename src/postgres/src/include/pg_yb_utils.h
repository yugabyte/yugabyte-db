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
#include "nodes/parsenodes.h"
#include "nodes/plannodes.h"
#include "utils/guc.h"
#include "utils/relcache.h"
#include "utils/resowner.h"

#include "yb/common/ybc_util.h"
#include "yb/yql/pggate/ybc_pggate.h"

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
extern uint64_t yb_catalog_cache_version;

/* Stores the catalog version info that is fetched from the local tserver. */
extern YbTserverCatalogInfo yb_tserver_catalog_info;

/*
 * Stores the shared memory array db_catalog_versions_ index of the slot
 * allocated for the MyDatabaseId, or -1 if no slot has been allocated for
 * MyDatabaseId.
 */
extern int yb_my_database_id_shm_index;

#define YB_CATCACHE_VERSION_UNINITIALIZED (0)

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

extern void YBResetCatalogVersion();

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

extern bool yb_read_from_followers;
extern int32_t yb_follower_read_staleness_ms;

/*
 * Iterate over databases and execute a given code snippet.
 * Should terminate with YB_FOR_EACH_DB_END.
 */
#define YB_FOR_EACH_DB(pg_db_tuple) \
	{ \
		/* Shared operations shouldn't be used during initdb. */ \
		Assert(!IsBootstrapProcessingMode()); \
		Relation    pg_db      = heap_open(DatabaseRelationId, AccessExclusiveLock); \
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
		heap_close(pg_db, AccessExclusiveLock); \
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

extern bool YBNeedRetryAfterCacheRefresh(ErrorData *edata);

extern void YBReportFeatureUnsupported(const char *err_msg);

extern AttrNumber YBGetFirstLowInvalidAttributeNumber(Relation relation);

extern AttrNumber YBGetFirstLowInvalidAttributeNumberFromOid(Oid relid);

extern int YBAttnumToBmsIndex(Relation rel, AttrNumber attnum);

extern AttrNumber YBBmsIndexToAttnum(Relation rel, int idx);

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

extern bool YbIsDatabaseColocated(Oid dbid);

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
 * Whether the current txn is of READ COMMITTED (or READ UNCOMMITTED) isolation level and it it uses
 * the new READ COMMITTED implementation instead of mapping to REPEATABLE READ level. The latter
 * condition is dictated by the value of gflag yb_enable_read_committed_isolation.
 */
extern bool IsYBReadCommitted();

/*
 * Whether to allow users to use SAVEPOINT commands at the query layer.
 */
extern bool YBSavepointsEnabled();

/*
 * Whether the per database catalog version mode is enabled.
 */
extern bool YBIsDBCatalogVersionMode();

/*
 * Given a status returned by YB C++ code, reports that status as a PG/YSQL
 * ERROR using ereport if it is not OK.
 */
extern void	HandleYBStatus(YBCStatus status);

/*
 * Generic version of HandleYBStatus that reports the YBCStatus at the
 * specified PG/YSQL error level (e.g. ERROR or WARNING or NOTICE).
 */
void HandleYBStatusAtErrorLevel(YBCStatus status, int error_level);

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
								  const char *user_name);

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
 * YSQL guc variable that is used to enable the use of Postgres's selectivity
 * functions and YSQL table statistics.
 * e.g. 'SET yb_enable_optimizer_statistics = true'
 * See also the corresponding entries in guc.c.
 */
extern bool yb_enable_optimizer_statistics;

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

//------------------------------------------------------------------------------
// GUC variables needed by YB via their YB pointers.
extern int StatementTimeout;
extern int *YBCStatementTimeoutPtr;

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
extern const char* YBHeapTupleToString(HeapTuple tuple, TupleDesc tupleDesc);

/*
 * Checks if the master thinks initdb has already been done.
 */
bool YBIsInitDbAlreadyDone();

int YBGetDdlNestingLevel();
void YBIncrementDdlNestingLevel();
void YBDecrementDdlNestingLevel(bool is_catalog_version_increment,
                                bool is_breaking_catalog_change);
bool IsTransactionalDdlStatement(PlannedStmt *pstmt,
                                 bool *is_catalog_version_increment,
                                 bool *is_breaking_catalog_change);
extern void YBBeginOperationsBuffering();
extern void YBEndOperationsBuffering();
extern void YBResetOperationsBuffering();
extern void YBFlushBufferedOperations();

bool YBReadFromFollowersEnabled();
int32_t YBFollowerReadStalenessMs();

/*
 * Allocates YBCPgYBTupleIdDescriptor with nattrs arguments by using palloc.
 * Resulted object can be released with pfree.
 */
YBCPgYBTupleIdDescriptor* YBCCreateYBTupleIdDescriptor(Oid db_oid, Oid table_oid, int nattrs);
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

/*
 * Check whether the given libc locale is supported in YugaByte mode.
 */
bool YBIsSupportedLibcLocale(const char *localebuf);

void YBTestFailDdlIfRequested();

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
 * Number of functions in 'yb_funcs_safe_for_modify_fast_path' above.
 */
extern const int yb_funcs_safe_for_pushdown_count;

/**
 * Use the YB_PG_PDEATHSIG environment variable to set the signal to be sent to
 * the current process in case the parent process dies. This is Linux-specific
 * and can only be done from the child process (the postmaster process). The
 * parent process here is yb-master or yb-tserver.
 */
void YBSetParentDeathSignal();

/**
 * Return the relid to be used for the relation's storage in docDB.
 * Ex: If we have swapped relation A with relation B, relation A's
 * filenode has been set to relation B's OID.
 */
Oid YbGetStorageRelid(Relation relation);

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
void YbCheckUnsupportedSystemColumns(Var *var, const char *colname, RangeTblEntry *rte);

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
 */
extern Datum yb_get_range_split_clause(PG_FUNCTION_ARGS);

extern bool check_yb_xcluster_consistency_level(char **newval, void **extra,
												GucSource source);
extern void assign_yb_xcluster_consistency_level(const char *newval,
												 void		*extra);

#endif /* PG_YB_UTILS_H */
