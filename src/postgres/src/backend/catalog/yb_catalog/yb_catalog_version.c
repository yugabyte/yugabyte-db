/*-------------------------------------------------------------------------
 *
 * yb_catalog_version.c
 *	  utility functions related to the ysql catalog version table.
 *
 * Portions Copyright (c) YugabyteDB, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <inttypes.h>

#include "access/htup_details.h"
#include "access/sysattr.h"
#include "access/yb_scan.h"
#include "catalog/catalog.h"
#include "catalog/namespace.h"
#include "catalog/pg_authid_d.h"
#include "catalog/pg_database.h"
#include "catalog/pg_namespace_d.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "catalog/pg_yb_catalog_version.h"
#include "catalog/pg_yb_invalidation_messages.h"
#include "catalog/schemapg.h"
#include "catalog/yb_catalog_version.h"
#include "executor/spi.h"
#include "executor/ybExpr.h"
#include "executor/ybModifyTable.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "optimizer/cost.h"
#include "pg_yb_utils.h"
#include "storage/lmgr.h"
#include "utils/catcache.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "yb/yql/pggate/ybc_gflags.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

YbCatalogVersionType yb_catalog_version_type = CATALOG_VERSION_UNSET;

static FormData_pg_attribute Desc_pg_yb_catalog_version[Natts_pg_yb_catalog_version] = {
	Schema_pg_yb_catalog_version
};

static bool YbGetMasterCatalogVersionFromTable(Oid db_oid, uint64_t *version,
											   bool acquire_lock);
static Datum YbGetMasterCatalogVersionTableEntryYbctid(Relation catalog_version_rel,
													   Oid db_oid);

/* Retrieve Catalog Version */

static uint64_t
YbGetMasterCatalogVersionImpl(bool acquire_lock)
{
	uint64_t	version = YB_CATCACHE_VERSION_UNINITIALIZED;

	switch (YbGetCatalogVersionType())
	{
		case CATALOG_VERSION_CATALOG_TABLE:
			if (YbGetMasterCatalogVersionFromTable(YbMasterCatalogVersionTableDBOid(), &version,
												   acquire_lock))
				return version;
			/*
			 * In spite of the fact the pg_yb_catalog_version table exists it has no actual
			 * version (this could happen during YSQL upgrade),
			 * fallback to an old protobuf mechanism until the next cache refresh.
			 */
			yb_catalog_version_type = CATALOG_VERSION_PROTOBUF_ENTRY;
			yb_switch_fallthrough();
		case CATALOG_VERSION_PROTOBUF_ENTRY:
			/* deprecated (kept for compatibility with old clusters). */
			HandleYBStatus(YBCPgGetCatalogMasterVersion(&version));
			return version;

		case CATALOG_VERSION_UNSET: /* should not happen. */
			break;
	}
	ereport(FATAL,
			(errcode(ERRCODE_INTERNAL_ERROR),
			 errmsg("catalog version type was not set, cannot load system catalog.")));
	return version;
}

uint64_t
YbGetMasterCatalogVersion()
{
	return YbGetMasterCatalogVersionImpl(false /* acquire_lock */ );
}

void
YbMaybeLockMasterCatalogVersion()
{
	/*
	 * When object locks are off (i.e., the old way), we ensure that concurrent
	 * DDLs don't stamp on each other by incrementing the catalog version. DDLs
	 * with overlapping [read time, commit time] windows would conflict with each
	 * other and only one of them would be able to make progress. This catalog
	 * version increment happens at the end of a DDL transaction.
	 *
	 * When auto analyze is turned on, we don't want ANALYZE DDL triggered by auto analyze to
	 * abort user DDLs. To achieve this, regular DDLs take a FOR KEY SHARE lock on the catalog version
	 * row with a high priority (see pg_session.cc) while ANALYZE triggered by auto analyze takes
	 * a low priority FOR UPDATE lock on all rows of the catalog version table.
	 * (1) Global DDLs (i.e., those that modify shared catalog tables) increment all catalog
	 *		 versions at the end of the DDL but only lock the catalog version of the current database
	 *       when they start. To enable them to abort ANALYZE on different DBs that can conflict with
	 *       them, ANALYZE locks all rows of catalog version table. It is challenging to identify a
	 *       global DDL at the start of a DDL so the other way around does not work.
	 * (2) We enable this feature only if the invalidation messages are used and per-database catalog
	 *		 version mode is enabled.
	 *
	 */
	if (yb_user_ddls_preempt_auto_analyze &&
		YBCIsLegacyModeForCatalogOps() &&
		YbIsInvalidationMessageEnabled() && YBIsDBCatalogVersionMode())
	{
		elog(DEBUG3, "Locking catalog version for db oid %d", MyDatabaseId);
		YbGetMasterCatalogVersionImpl(true /* acquire_lock */ );
	}
}

/* Modify Catalog Version */

static Datum
GetInvalidationMessages(const SharedInvalidationMessage *invalMessages, int nmsgs,
						bool *is_null)
{
	if (!invalMessages)
	{
		Assert(nmsgs >= 0);
		*is_null = true;
		return (Datum) 0;
	}

	size_t		str_len = sizeof(SharedInvalidationMessage) * nmsgs;
	bytea	   *bstr = palloc(VARHDRSZ + str_len);

	memcpy(VARDATA(bstr), invalMessages, str_len);
	SET_VARSIZE(bstr, VARHDRSZ + str_len);
	*is_null = false;
	return PointerGetDatum(bstr);
}

static void
YbCallSQLIncrementCatalogVersions(Oid functionId, bool is_breaking_change,
								  const char *command_tag)
{
	FmgrInfo	flinfo;

	LOCAL_FCINFO(fcinfo, 1);
	fmgr_info(functionId, &flinfo);
	InitFunctionCallInfoData(*fcinfo, &flinfo, 1, InvalidOid, NULL, NULL);
	fcinfo->args[0].value = BoolGetDatum(is_breaking_change);
	fcinfo->args[0].isnull = false;

	/* Save old values and set new values to enable the call. */
	bool		saved = yb_non_ddl_txn_for_sys_tables_allowed;

	yb_non_ddl_txn_for_sys_tables_allowed = true;
	Oid			save_userid;
	int			save_sec_context;

	GetUserIdAndSecContext(&save_userid, &save_sec_context);
	SetUserIdAndSecContext(BOOTSTRAP_SUPERUSERID,
						   SECURITY_RESTRICTED_OPERATION);
	/* Calling a user defined function requires a snapshot. */
	bool		snapshot_set = ActiveSnapshotSet();

	if (!snapshot_set)
		PushActiveSnapshot(GetTransactionSnapshot());

	if (!(*YBCGetGFlags()->TEST_hide_details_for_pg_regress))
	{
		bool		log_ysql_catalog_versions =
			*YBCGetGFlags()->log_ysql_catalog_versions;

		ereport(LOG,
				(errmsg("%s: incrementing all master db catalog versions (%sbreaking)",
						__func__, is_breaking_change ? "" : "non"),
				 errdetail("Node tag: %s.", command_tag ? command_tag : "n/a"),
				 errhidestmt(!log_ysql_catalog_versions),
				 errhidecontext(!log_ysql_catalog_versions)));
	}

	PG_TRY();
	{
		yb_is_calling_internal_sql_for_ddl = true;
		FunctionCallInvoke(fcinfo);
		/* Restore old values. */
		yb_non_ddl_txn_for_sys_tables_allowed = saved;
		yb_is_calling_internal_sql_for_ddl = false;
		SetUserIdAndSecContext(save_userid, save_sec_context);
		if (!snapshot_set)
			PopActiveSnapshot();
	}
	PG_CATCH();
	{
		/* Restore old values. */
		yb_non_ddl_txn_for_sys_tables_allowed = saved;
		yb_is_calling_internal_sql_for_ddl = false;
		SetUserIdAndSecContext(save_userid, save_sec_context);
		if (!snapshot_set)
			PopActiveSnapshot();
		PG_RE_THROW();
	}
	PG_END_TRY();
}

/*
 * When transactional DDL is enabled, multiple DDLs could contribute to the
 * catalog version increment. Hence, we need to be more explicit that we
 * are only logging the last catalog version incrementing DDL and there may
 * be more such DDLs that are not logged.
 */
static bool
LastDdlInTransactionBlock()
{
	return YBIsDdlTransactionBlockEnabled() && IsTransactionBlock();
}

static void
MaybeLogNewSQLIncrementCatalogVersion(bool success,
									  Oid db_oid,
									  bool is_breaking_change,
									  bool is_global_ddl,
									  const char *command_tag,
									  uint64_t new_version)
{
	if (!(*YBCGetGFlags()->TEST_hide_details_for_pg_regress))
	{
		bool		log_ysql_catalog_versions =
			*YBCGetGFlags()->log_ysql_catalog_versions;
		char		tmpbuf1[20] = "";
		char		tmpbuf2[60] = " failed";

		/* Log MyDatabaseId to if it differs from db_oid. */
		if (db_oid != MyDatabaseId)
			snprintf(tmpbuf1, sizeof(tmpbuf1), " from %u", MyDatabaseId);
		if (success)
			snprintf(tmpbuf2, sizeof(tmpbuf2),
					 ", new version for database %u is %" PRIu64,
					 db_oid, new_version);

		char	   *action = is_global_ddl ? "all master db catalog versions"
			: "master db catalog version";

		ereport(LOG,
				(errmsg("%s: incrementing %s "
						"(%sbreaking) with inval messages%s%s",
						__func__, action, is_breaking_change ? "" : "non",
						tmpbuf1, tmpbuf2),
				 errdetail("Local version: %" PRIu64 ", %snode tag: %s.",
						   YbGetCatalogCacheVersion(),
						   LastDdlInTransactionBlock() ? "last ddl " : "",
						   command_tag ? command_tag : "n/a"),
				 errhidestmt(!log_ysql_catalog_versions),
				 errhidecontext(!log_ysql_catalog_versions)));
	}
}

static uint64_t
YbCallNewSQLIncrementCatalogVersionHelper(Oid functionId,
										  Oid db_oid,
										  bool is_breaking_change,
										  const char *command_tag,
										  Datum messages,
										  bool is_null,
										  int expiration_secs,
										  bool is_global_ddl)
{
	FmgrInfo	flinfo;

	LOCAL_FCINFO(fcinfo, 4);
	fmgr_info(functionId, &flinfo);
	InitFunctionCallInfoData(*fcinfo, &flinfo, 4, InvalidOid, NULL, NULL);
	fcinfo->args[0].value = ObjectIdGetDatum(db_oid);
	fcinfo->args[0].isnull = false;
	fcinfo->args[1].value = BoolGetDatum(is_breaking_change);
	fcinfo->args[1].isnull = false;
	fcinfo->args[2].value = messages;
	fcinfo->args[2].isnull = is_null;
	fcinfo->args[3].value = expiration_secs;
	fcinfo->args[3].isnull = false;

	/* Save old values and set new values to enable the call. */
	bool		saved = yb_non_ddl_txn_for_sys_tables_allowed;

	yb_non_ddl_txn_for_sys_tables_allowed = true;
	bool		saved_enable_seqscan = enable_seqscan;

	/*
	 * Avoid sequential scan for non-global-impact DDL, otherwise we can get
	 * conflicts between concurrent cross-database DDLs.
	 */
	if (!is_global_ddl)
		enable_seqscan = false;
	Oid			save_userid;
	int			save_sec_context;

	GetUserIdAndSecContext(&save_userid, &save_sec_context);
	SetUserIdAndSecContext(BOOTSTRAP_SUPERUSERID,
						   SECURITY_RESTRICTED_OPERATION);
	/* Calling a user defined function requires a snapshot. */
	bool		snapshot_set = ActiveSnapshotSet();

	if (!snapshot_set)
		PushActiveSnapshot(GetTransactionSnapshot());
	volatile uint64_t new_version;

	PG_TRY();
	{
		yb_is_calling_internal_sql_for_ddl = true;
		Datum		retval = FunctionCallInvoke(fcinfo);

		/* Restore old values. */
		yb_non_ddl_txn_for_sys_tables_allowed = saved;
		yb_is_calling_internal_sql_for_ddl = false;
		enable_seqscan = saved_enable_seqscan;
		SetUserIdAndSecContext(save_userid, save_sec_context);
		if (!snapshot_set)
			PopActiveSnapshot();
		if (fcinfo->isnull)
		{
			elog(WARNING, "function %u returned NULL", functionId);
			new_version = YB_CATCACHE_VERSION_UNINITIALIZED;
		}
		else
		{
			new_version = DatumGetUInt64(retval);
			Assert(new_version != YB_CATCACHE_VERSION_UNINITIALIZED);
		}
		MaybeLogNewSQLIncrementCatalogVersion(true /* success */ ,
											  db_oid,
											  is_breaking_change,
											  is_global_ddl,
											  command_tag,
											  new_version);
	}
	PG_CATCH();
	{
		/* Restore old values. */
		yb_non_ddl_txn_for_sys_tables_allowed = saved;
		yb_is_calling_internal_sql_for_ddl = false;
		enable_seqscan = saved_enable_seqscan;
		SetUserIdAndSecContext(save_userid, save_sec_context);
		if (!snapshot_set)
			PopActiveSnapshot();
		MaybeLogNewSQLIncrementCatalogVersion(false /* success */ ,
											  db_oid,
											  is_breaking_change,
											  is_global_ddl,
											  command_tag,
											  0 /* new_version */ );
		PG_RE_THROW();
	}
	PG_END_TRY();
	return new_version;
}

static uint64_t
YbCallNewSQLIncrementCatalogVersion(Oid functionId, Oid db_oid, bool is_breaking_change,
									const char *command_tag,
									Datum messages, bool is_null, int expiration_secs)
{
	return YbCallNewSQLIncrementCatalogVersionHelper(functionId,
													 db_oid,
													 is_breaking_change,
													 command_tag,
													 messages,
													 is_null,
													 expiration_secs,
													 false /* is_global_ddl */ );
}

static uint64_t
YbCallNewSQLIncrementAllCatalogVersions(Oid functionId,
										Oid db_oid,
										bool is_breaking_change,
										const char *command_tag,
										Datum messages,
										bool is_null,
										int expiration_secs)
{
	return YbCallNewSQLIncrementCatalogVersionHelper(functionId,
													 db_oid,
													 is_breaking_change,
													 command_tag,
													 messages,
													 is_null,
													 expiration_secs,
													 true /* is_global_ddl */ );
}

static Oid
YbGetSQLIncrementCatalogVersionFunctionOidHelper(char *fname)
{
	List	   *names = list_make2(makeString("pg_catalog"), makeString(fname));
	FuncCandidateList clist = FuncnameGetCandidates(names,
													-1 /* nargs */ ,
													NIL /* argnames */ ,
													false /* expand_variadic */ ,
													false /* expand_defaults */ ,
													false /* include_out_arguments */ ,
													false /* missing_ok */ );

	/* We expect exactly one candidate. */
	if (clist && clist->next == NULL)
		return clist->oid;
	Assert(!clist);
	/* When upgrading an old release, the function may not exist. */
	return InvalidOid;
}

Oid
YbGetSQLIncrementCatalogVersionsFunctionOid()
{
	return YbGetSQLIncrementCatalogVersionFunctionOidHelper("yb_increment_all_db_catalog_versions");
}

static Oid
YbGetNewIncrementCatalogVersionFunctionOid()
{
	return YbGetSQLIncrementCatalogVersionFunctionOidHelper("yb_increment_db_catalog_version_with_inval_messages");
}

static Oid
YbGetNewIncrementAllCatalogVersionsFunctionOid()
{
	return YbGetSQLIncrementCatalogVersionFunctionOidHelper("yb_increment_all_db_catalog_versions_with_inval_messages");
}

static void
YbIncrementMasterDBCatalogVersionTableEntryImpl(Oid db_oid,
												bool is_breaking_change,
												bool is_global_ddl,
												const char *command_tag,
												const SharedInvalidationMessage *invalMessages,
												int nmsgs)
{
	Assert(YbGetCatalogVersionType() == CATALOG_VERSION_CATALOG_TABLE);

	if (!YBCIsLegacyModeForCatalogOps())
		LockRelationOid(YBCatalogVersionRelationId, ExclusiveLock);

	if (YbIsInvalidationMessageEnabled())
	{
		Oid			func_oid = is_global_ddl ? YbGetNewIncrementAllCatalogVersionsFunctionOid()
			: YbGetNewIncrementCatalogVersionFunctionOid();

		if (OidIsValid(func_oid) && YbInvalidationMessagesTableExists())
		{
			bool		is_null = false;
			Datum		messages = GetInvalidationMessages(invalMessages, nmsgs, &is_null);
			int			expiration_secs = yb_invalidation_message_expiration_secs;

			if (is_global_ddl)
			{
				/*
				 * Call yb_increment_all_db_catalog_versions_with_inval_messages(
				 *     is_breaking_change, messages, expiration_secs).
				 * Pass MyDatabaseId to get the new version of MyDatabaseId.
				 */
				uint64_t	new_version =
					YbCallNewSQLIncrementAllCatalogVersions(func_oid, MyDatabaseId,
															is_breaking_change,
															command_tag, messages,
															is_null, expiration_secs);

				YbSetNewCatalogVersion(new_version);
				return;
			}

			/*
			 * Call yb_increment_db_catalog_version_with_inval_messages(
			 *     db_oid, is_breaking_change, is_global_ddl, messages).
			 */
			uint64_t	new_version =
				YbCallNewSQLIncrementCatalogVersion(func_oid, db_oid,
													is_breaking_change,
													command_tag, messages,
													is_null, expiration_secs);

			/*
			 * The new version of database_oid is only meaningful when
			 * db_oid == MyDatabaseId.
			 */
			if (db_oid == MyDatabaseId)
				YbSetNewCatalogVersion(new_version);

			return;
		}
	}

	if (is_global_ddl)
	{
		Assert(YBIsDBCatalogVersionMode());
		Oid			func_oid = YbGetSQLIncrementCatalogVersionsFunctionOid();

		Assert(OidIsValid(func_oid));
		/* Call yb_increment_all_db_catalog_versions(is_breaking_change). */
		YbCallSQLIncrementCatalogVersions(func_oid, is_breaking_change,
										  command_tag);
		return;
	}

	/*
	 * There are two more scenarios we also call the function
	 * yb_increment_all_db_catalog_versions(is_breaking_change), both
	 * are related to cluster upgrade to a new release that has the
	 * per-database catalog version mode on by default:
	 * (1) during the tryout phase (the phase where the upgrade can be
	 * rolled back)
	 * (2) during the finalization phase (the phase where the upgrade can
	 * not be rolled back)
	 * In both cases, the gflag --ysql_enable_db_catalog_version_mode is
	 * true but YBIsDBCatalogVersionMode() is false. PG does not
	 * distinguish them. In (1), the table pg_yb_catalog_version has only
	 * one row. In (2), it is possible that pg_yb_catalog_version has
	 * already been updated to have multiple rows, but this PG backend
	 * hasn't seen multple rows yet and still operates in global catalog
	 * version mode. Calling yb_increment_all_db_catalog_versions ensures
	 * that this PG's version bump has an effect to all the PG backends
	 * including those already upgraded and are operating in per-database
	 * catalog version mode.
	 */
	if (*YBCGetGFlags()->ysql_enable_db_catalog_version_mode &&
		!YBIsDBCatalogVersionMode())
	{
		Oid			func_oid = YbGetSQLIncrementCatalogVersionsFunctionOid();

		if (OidIsValid(func_oid))
		{
			/* Call yb_increment_all_db_catalog_versions(is_breaking_change). */
			YbCallSQLIncrementCatalogVersions(func_oid, is_breaking_change,
											  command_tag);
			return;
		}
		/*
		 * If the function yb_increment_all_db_catalog_versions does not exist
		 * yet, there cannot be any PG backend in the cluster running in
		 * per-database catalog version mode. This is because the function
		 * is introduced before the table pg_yb_catalog_version is upgraded
		 * to have one row per database. In this case we continue to increment
		 * catalog version in the old way below.
		 */
	}

	YbcPgTypeAttrs type_attrs = {0};

	Relation	rel = RelationIdGetRelation(YBCatalogVersionRelationId);

	YbcPgStatement update_stmt = YbNewUpdate(rel, YB_TRANSACTIONAL);

	Datum		ybctid = YbGetMasterCatalogVersionTableEntryYbctid(rel, db_oid);

	/* Bind ybctid to identify the current row. */
	YbcPgExpr	ybctid_expr = YBCNewConstant(update_stmt, BYTEAOID, InvalidOid,
											 ybctid, false /* is_null */ );

	HandleYBStatus(YBCPgDmlBindColumn(update_stmt, YBTupleIdAttributeNumber,
									  ybctid_expr));

	/* Set expression c = c + 1 for current version attribute. */
	AttrNumber	attnum = Anum_pg_yb_catalog_version_current_version;
	Var		   *arg1 = makeVar(1,
							   attnum,
							   INT8OID,
							   0,
							   InvalidOid,
							   0);

	Const	   *arg2 = makeConst(INT8OID,
								 0,
								 InvalidOid,
								 sizeof(int64),
								 (Datum) 1,
								 false,
								 true);

	List	   *args = list_make2(arg1, arg2);

	FuncExpr   *expr = makeFuncExpr(F_INT8PL,
									INT8OID,
									args,
									InvalidOid,
									InvalidOid,
									COERCE_EXPLICIT_CALL);

	/* INT8 OID. */
	YbcPgExpr	ybc_expr = YBCNewEvalExprCall(update_stmt, (Expr *) expr);

	HandleYBStatus(YBCPgDmlAssignColumn(update_stmt, attnum, ybc_expr));
	YbcPgExpr	yb_expr = YBCNewColumnRef(update_stmt, attnum, INT8OID, InvalidOid, &type_attrs);

	YbAppendPrimaryColumnRef(update_stmt, yb_expr);

	/*
	 * If breaking change set the latest breaking version to the same
	 * expression.
	 */
	if (is_breaking_change)
	{
		ybc_expr = YBCNewEvalExprCall(update_stmt, (Expr *) expr);
		HandleYBStatus(YBCPgDmlAssignColumn(update_stmt, attnum + 1, ybc_expr));
	}

	int			rows_affected_count = 0;

	if (!(*YBCGetGFlags()->TEST_hide_details_for_pg_regress))
	{
		bool		log_ysql_catalog_versions = *YBCGetGFlags()->log_ysql_catalog_versions;
		char		tmpbuf[30] = "";

		if (YBIsDBCatalogVersionMode())
			snprintf(tmpbuf, sizeof(tmpbuf), " for database %u", db_oid);
		ereport(LOG,
				(errmsg("%s: incrementing master catalog version (%sbreaking)%s",
						__func__, is_breaking_change ? "" : "non", tmpbuf),
				 errdetail("Local version: %" PRIu64 ", %snode tag: %s.",
						   YbGetCatalogCacheVersion(),
						   LastDdlInTransactionBlock() ? "last ddl " : "",
						   command_tag ? command_tag : "n/a"),
				 errhidestmt(!log_ysql_catalog_versions),
				 errhidecontext(!log_ysql_catalog_versions)));
	}

	HandleYBStatus(YBCPgDmlExecWriteOp(update_stmt, &rows_affected_count));
	/*
	 * Under normal situation rows_affected_count should be exactly 1. However
	 * when a connection is established in per-database catalog version mode,
	 * if the table pg_yb_catalog_version is updated to only have one row
	 * for database template1 which is part of the process of converting to
	 * global catalog version mode, this connection remains in per-database
	 * catalog version mode. In this case rows_affected_count will be 0 unless
	 * MyDatabaseId is template1 because in pg_yb_catalog_version the row for
	 * MyDatabaseId no longer exists.
	 */
	if (rows_affected_count == 0)
		Assert(YBIsDBCatalogVersionMode());
	else
		Assert(rows_affected_count == 1);

	/* Cleanup. */
	update_stmt = NULL;
	RelationClose(rel);
}

bool
YbIncrementMasterCatalogVersionTableEntry(bool is_breaking_change,
										  bool is_global_ddl,
										  const char *command_tag,
										  const SharedInvalidationMessage *invalMessages,
										  int nmsgs)
{
	elog(DEBUG2, "YbIncrementMasterCatalogVersionTableEntry");
	YbResetNewCatalogVersion();
	if (YbGetCatalogVersionType() != CATALOG_VERSION_CATALOG_TABLE)
		return false;

	Oid			database_oid = YbGetDatabaseOidToIncrementCatalogVersion();

	Assert(OidIsValid(database_oid));

	YbIncrementMasterDBCatalogVersionTableEntryImpl(database_oid,
													is_breaking_change,
													is_global_ddl,
													command_tag,
													invalMessages,
													nmsgs);
	if (yb_test_fail_next_inc_catalog_version)
	{
		yb_test_fail_next_inc_catalog_version = false;
		if (YbIsClientYsqlConnMgr())
			YbSendParameterStatusForConnectionManager("yb_test_fail_next_inc_catalog_version",
													  "false");
		elog(ERROR, "Failed increment catalog version as requested");
	}

	return true;
}

bool
YbMarkStatementIfCatalogVersionIncrement(YbcPgStatement ybc_stmt,
										 Relation rel)
{
	if (YbGetCatalogVersionType() != CATALOG_VERSION_PROTOBUF_ENTRY)
	{
		/*
		 * Nothing to do -- only need to maintain this for the (old)
		 * protobuf-based way of storing the version.
		 */
		return false;
	}

	bool		is_syscatalog_change = YbIsSystemCatalogChange(rel);
	bool		modifies_row = false;

	HandleYBStatus(YBCPgDmlModifiesRow(ybc_stmt, &modifies_row));

	/*
	 * If this write may invalidate catalog cache tuples (i.e. UPDATE or DELETE),
	 * or this write may insert into a cached list, we must increment the
	 * cache version so other sessions can invalidate their caches.
	 * NOTE: If this relation caches lists, an INSERT could effectively be
	 * UPDATINGing the list object.
	 */
	bool		is_syscatalog_version_change = (is_syscatalog_change &&
												(modifies_row ||
												 RelationHasCachedLists(rel)));

	/* Let the master know if this should increment the catalog version. */
	if (is_syscatalog_version_change)
	{
		HandleYBStatus(YBCPgSetIsSysCatalogVersionChange(ybc_stmt));
	}

	return is_syscatalog_version_change;
}

void
YbCreateMasterDBCatalogVersionTableEntry(Oid db_oid)
{
	Assert(YbGetCatalogVersionType() == CATALOG_VERSION_CATALOG_TABLE);
	Assert(db_oid != MyDatabaseId);

	/*
	 * The table pg_yb_catalog_version is a shared relation in template1 and
	 * db_oid is the primary key. There is no separate docdb index table for
	 * primary key and therefore only one insert statement is needed to insert
	 * the row for db_oid.
	 */
	Relation	rel = RelationIdGetRelation(YBCatalogVersionRelationId);

	YbcPgStatement insert_stmt = YbNewInsert(rel, YB_SINGLE_SHARD_TRANSACTION);

	Datum		ybctid = YbGetMasterCatalogVersionTableEntryYbctid(rel, db_oid);

	YbcPgExpr	ybctid_expr = YBCNewConstant(insert_stmt, BYTEAOID, InvalidOid,
											 ybctid, false /* is_null */ );

	HandleYBStatus(YBCPgDmlBindColumn(insert_stmt, YBTupleIdAttributeNumber,
									  ybctid_expr));



	AttrNumber	attnum = Anum_pg_yb_catalog_version_current_version;
	Datum		initial_version = 1;
	YbcPgExpr	initial_version_expr = YBCNewConstant(insert_stmt, INT8OID,
													  InvalidOid,
													  initial_version,
													  false /* is_null */ );

	HandleYBStatus(YBCPgDmlBindColumn(insert_stmt, attnum,
									  initial_version_expr));
	HandleYBStatus(YBCPgDmlBindColumn(insert_stmt, attnum + 1,
									  initial_version_expr));

	int			rows_affected_count = 0;

	if (*YBCGetGFlags()->log_ysql_catalog_versions)
		ereport(LOG,
				(errmsg("%s: creating master catalog version for database %u",
						__func__, db_oid)));
	HandleYBStatus(YBCPgDmlExecWriteOp(insert_stmt, &rows_affected_count));
	/* Insert a new row does not affect any existing rows. */
	Assert(rows_affected_count == 0);

	/* Cleanup. */
	RelationClose(rel);
}

void
YbDeleteMasterDBInvalidationMessagesTableEntries(Oid db_oid)
{
	/*
	 * Connect to SPI manager
	 */
	if (SPI_connect() != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed");
	char		query[100];

	sprintf(query, "DELETE FROM pg_catalog.pg_yb_invalidation_messages WHERE db_oid = %u", db_oid);
	SPIPlanPtr	plan = SPI_prepare(query, 0, NULL);

	if (plan == NULL)
		elog(ERROR, "SPI_prepare failed for \"%s\"", query);

	bool		saved_yb_is_calling_internal_sql_for_ddl = yb_is_calling_internal_sql_for_ddl;
	Oid			save_userid;
	int			save_sec_context;

	GetUserIdAndSecContext(&save_userid, &save_sec_context);
	SetUserIdAndSecContext(BOOTSTRAP_SUPERUSERID,
						   SECURITY_RESTRICTED_OPERATION);
	yb_is_calling_internal_sql_for_ddl = true;
	PG_TRY();
	{
		int			spirc = SPI_execute_plan(plan, NULL, NULL, false, 0);

		yb_is_calling_internal_sql_for_ddl = saved_yb_is_calling_internal_sql_for_ddl;
		SetUserIdAndSecContext(save_userid, save_sec_context);
		if (spirc != SPI_OK_DELETE)
			elog(ERROR, "SPI_execute_plan failed for \"%s\"", query);
		ereport((*YBCGetGFlags()->log_ysql_catalog_versions ? LOG : DEBUG1),
				(errmsg("%s: deleted %lu invalidation messages for database %u",
						__func__, SPI_processed, db_oid)));
	}
	PG_CATCH();
	{
		yb_is_calling_internal_sql_for_ddl = saved_yb_is_calling_internal_sql_for_ddl;
		SetUserIdAndSecContext(save_userid, save_sec_context);
		PG_RE_THROW();
	}
	PG_END_TRY();

	/*
	 * Disconnect from SPI manager
	 */
	if (SPI_finish() != SPI_OK_FINISH)
		elog(ERROR, "SPI_finish failed");
}

void
YbDeleteMasterDBCatalogVersionTableEntry(Oid db_oid)
{
	Assert(YbGetCatalogVersionType() == CATALOG_VERSION_CATALOG_TABLE);
	Assert(db_oid != MyDatabaseId);

	/*
	 * The table pg_yb_catalog_version is a shared relation in template1 and
	 * db_oid is the primary key. There is no separate docdb index table for
	 * primary key and therefore only one delete statement is needed to delete
	 * the row for db_oid.
	 */

	Relation	rel = RelationIdGetRelation(YBCatalogVersionRelationId);

	YbcPgStatement delete_stmt = YbNewDelete(rel, YB_SINGLE_SHARD_TRANSACTION);

	Datum		ybctid = YbGetMasterCatalogVersionTableEntryYbctid(rel, db_oid);

	YbcPgExpr	ybctid_expr = YBCNewConstant(delete_stmt, BYTEAOID, InvalidOid,
											 ybctid, false /* is_null */ );

	HandleYBStatus(YBCPgDmlBindColumn(delete_stmt, YBTupleIdAttributeNumber,
									  ybctid_expr));

	int			rows_affected_count = 0;

	if (*YBCGetGFlags()->log_ysql_catalog_versions)
		ereport(LOG,
				(errmsg("%s: deleting master catalog version for database %u",
						__func__, db_oid)));
	HandleYBStatus(YBCPgDmlExecWriteOp(delete_stmt, &rows_affected_count));
	Assert(rows_affected_count == 1);

	RelationClose(rel);

	/*
	 * When invalidation messages are enabled, we also need to delete the
	 * invalidation messages stored in pg_yb_invalidation_messages table
	 * for db_oid.
	 */
	if (YbIsInvalidationMessageEnabled() && YbInvalidationMessagesTableExists())
		YbDeleteMasterDBInvalidationMessagesTableEntries(db_oid);
}

YbCatalogVersionType
YbGetCatalogVersionType()
{
	if (IsBootstrapProcessingMode())
	{
		/*
		 * We don't have the catalog version table at the start of initdb,
		 * and there's no point in switching later on.
		 */
		yb_catalog_version_type = CATALOG_VERSION_PROTOBUF_ENTRY;
	}
	else if (yb_catalog_version_type == CATALOG_VERSION_UNSET)
	{
		bool		catalog_version_table_exists = false;

		HandleYBStatus(YBCPgTableExists(Template1DbOid,
										YBCatalogVersionRelationId,
										&catalog_version_table_exists));
		yb_catalog_version_type = (catalog_version_table_exists ?
								   CATALOG_VERSION_CATALOG_TABLE :
								   CATALOG_VERSION_PROTOBUF_ENTRY);
	}
	return yb_catalog_version_type;
}


/*
 * Check if operation changes a system table, ignore changes during
 * initialization (bootstrap mode).
 */
bool
YbIsSystemCatalogChange(Relation rel)
{
	return IsCatalogRelation(rel) && !IsBootstrapProcessingMode();
}

bool
YbGetMasterCatalogVersionFromTable(Oid db_oid, uint64_t *version,
								   bool acquire_lock)
{
	*version = 0;				/* unset; */

	int			natts = Natts_pg_yb_catalog_version;

	/*
	 * pg_yb_catalog_version is a shared catalog table, so as per DocDB store,
	 * it belongs to the template1 database.
	 */
	int			oid_attnum = Anum_pg_yb_catalog_version_db_oid;
	int			current_version_attnum = Anum_pg_yb_catalog_version_current_version;
	Form_pg_attribute oid_attrdesc = &Desc_pg_yb_catalog_version[oid_attnum - 1];

	YbcPgStatement ybc_stmt;

	HandleYBStatus(YBCPgNewSelect(Template1DbOid,
								  YBCatalogVersionRelationId,
								  NULL /* prepare_params */ ,
								  YbBuildSystemTableLocalityInfo(YBCatalogVersionRelationId),
								  &ybc_stmt));

	if (!(acquire_lock && yb_use_internal_auto_analyze_service_conn))
	{
		Datum		oid_datum = Int32GetDatum(db_oid);
		YbcPgExpr	pkey_expr = YBCNewConstant(ybc_stmt, oid_attrdesc->atttypid,
											   oid_attrdesc->attcollation,
											   oid_datum, false /* is_null */ );

		HandleYBStatus(YBCPgDmlBindColumn(ybc_stmt, 1, pkey_expr));
	}
	for (AttrNumber attnum = 1; attnum <= natts; ++attnum)
		YbDmlAppendTargetRegularAttr(&Desc_pg_yb_catalog_version[attnum - 1],
									 ybc_stmt);

	YbcPgExecParameters exec_params = {0};

	if (acquire_lock)
	{
		/*
		 * We want to stick to Fail-on-Conflict concurrency control to ensure that higher priority
		 * user DDLs always take precedence over lower priority auto-ANALYZEs. In other words, user DDLs
		 * should abort running auto-ANALYZEs, and auto-ANALYZEs should face a conflict error if a user
		 * DDL is already running.
		 */
		exec_params.pg_wait_policy = LockWaitError;
		exec_params.docdb_wait_policy = YBGetDocDBWaitPolicy(exec_params.pg_wait_policy);
		exec_params.rowmark = yb_use_internal_auto_analyze_service_conn ?
			ROW_MARK_EXCLUSIVE :
			ROW_MARK_KEYSHARE;
	}

	HandleYBStatus(YBCPgExecSelect(ybc_stmt, acquire_lock ? &exec_params : NULL));

	bool		has_data = false;

	Datum	   *values = palloc0(natts * sizeof(Datum));
	bool	   *nulls = palloc(natts * sizeof(bool));
	YbcPgSysColumns syscols;
	bool		result = false;

	if (!YBIsDBCatalogVersionMode())
	{
		/* Fetch one row. */
		HandleYBStatus(YBCPgDmlFetch(ybc_stmt,
									 natts,
									 (uint64_t *) values,
									 nulls,
									 &syscols,
									 &has_data));

		if (has_data)
		{
			*version = DatumGetUInt64(values[current_version_attnum - 1]);
			result = true;
		}
	}
	else
	{
		/*
		 * When prefetching is enabled we always load all the rows even though
		 * we bind to the row matching given db_oid. This is a work around to
		 * pick the row that matches db_oid. This work around should be removed
		 * when prefetching is enhanced to support filtering.
		 */
		while (true)
		{
			/* Fetch one row. */
			HandleYBStatus(YBCPgDmlFetch(ybc_stmt,
										 natts,
										 (uint64_t *) values,
										 nulls,
										 &syscols,
										 &has_data));

			if (!has_data)
				ereport(ERROR,
						(errcode(ERRCODE_DATABASE_DROPPED),
						 errmsg("catalog version for database %u was not found.", db_oid),
						 errhint("Database may have been dropped and recreated. "
								 "Ensure your client connection pool or metadata cache "
								 "is refreshed to pick up the new database OID.")));

			uint32_t	oid = DatumGetUInt32(values[oid_attnum - 1]);

			if (oid == db_oid)
			{
				*version = DatumGetUInt64(values[current_version_attnum - 1]);
				result = true;
				break;
			}
		}
	}

	pfree(values);
	pfree(nulls);
	return result;
}

Datum
YbGetMasterCatalogVersionTableEntryYbctid(Relation catalog_version_rel,
										  Oid db_oid)
{
	/*
	 * Construct virtual slot (db_oid, null, null) for computing ybctid using
	 * YBCComputeYBTupleIdFromSlot. Note that db_oid is the primary key so we
	 * can use null for other columns for simplicity.
	 */
	TupleTableSlot *slot = MakeSingleTupleTableSlot(RelationGetDescr(catalog_version_rel),
													&TTSOpsVirtual);

	slot->tts_values[0] = db_oid;
	slot->tts_isnull[0] = false;
	slot->tts_values[1] = 0;
	slot->tts_isnull[1] = true;
	slot->tts_values[2] = 0;
	slot->tts_isnull[2] = true;
	slot->tts_nvalid = 3;

	Datum		ybctid = YBCComputeYBTupleIdFromSlot(catalog_version_rel, slot);

	ExecDropSingleTupleTableSlot(slot);
	return ybctid;
}

Oid
YbMasterCatalogVersionTableDBOid()
{
	/*
	 * MyDatabaseId is 0 during connection setup time before
	 * MyDatabaseId is resolved. In per-db mode, we use Template1DbOid
	 * during this period to find the catalog version in order to load
	 * initial catalog cache (needed for resolving MyDatabaseId, auth
	 * check etc.). Once MyDatabaseId is resolved from then on we'll
	 * use its catalog version.
	 */

	return YBIsDBCatalogVersionMode() && OidIsValid(MyDatabaseId)
		? MyDatabaseId : Template1DbOid;
}
