/*-------------------------------------------------------------------------
 *
 * pg_yb_utils.c
 *	  Utilities for YugaByte/PostgreSQL integration that have to be defined on
 *	  the PostgreSQL side.
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
 * IDENTIFICATION
 *	  src/backend/utils/misc/pg_yb_utils.c
 *
 *-------------------------------------------------------------------------
 */

#include "pg_yb_utils.h"

#include <assert.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "access/tuptoaster.h"
#include "c.h"
#include "postgres.h"
#include "miscadmin.h"
#include "access/htup.h"
#include "access/htup_details.h"
#include "access/sysattr.h"
#include "access/tupdesc.h"
#include "access/xact.h"
#include "executor/ybcExpr.h"
#include "catalog/catalog.h"
#include "catalog/index.h"
#include "catalog/indexing.h"
#include "catalog/pg_am.h"
#include "catalog/pg_amop.h"
#include "catalog/pg_amproc.h"
#include "catalog/pg_attrdef.h"
#include "catalog/pg_auth_members.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_cast.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_database.h"
#include "catalog/pg_db_role_setting.h"
#include "catalog/pg_inherits.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_partitioned_table.h"
#include "catalog/pg_policy.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_range_d.h"
#include "catalog/pg_rewrite.h"
#include "catalog/pg_statistic_d.h"
#include "catalog/pg_tablespace.h"
#include "catalog/pg_trigger.h"
#include "catalog/pg_type.h"
#include "catalog/pg_yb_catalog_version.h"
#include "catalog/pg_yb_profile.h"
#include "catalog/pg_yb_role_profile.h"
#include "catalog/yb_catalog_version.h"
#include "catalog/yb_type.h"
#include "commands/dbcommands.h"
#include "commands/defrem.h"
#include "commands/variable.h"
#include "commands/ybccmds.h"
#include "common/ip.h"
#include "common/pg_yb_common.h"
#include "lib/stringinfo.h"
#include "libpq/hba.h"
#include "libpq/libpq.h"
#include "libpq/libpq-be.h"
#include "nodes/makefuncs.h"
#include "optimizer/cost.h"
#include "parser/parse_utilcmd.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/pg_locale.h"
#include "utils/rel.h"
#include "utils/spccache.h"
#include "utils/syscache.h"
#include "utils/uuid.h"
#include "fmgr.h"
#include "funcapi.h"
#include "mb/pg_wchar.h"

#include "yb/yql/pggate/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "pgstat.h"
#include "nodes/readfuncs.h"
#include "yb_ash.h"

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

static uint64_t yb_catalog_cache_version = YB_CATCACHE_VERSION_UNINITIALIZED;
static uint64_t yb_last_known_catalog_cache_version =
	YB_CATCACHE_VERSION_UNINITIALIZED;

uint64_t YBGetActiveCatalogCacheVersion() {
	if (yb_catalog_version_type == CATALOG_VERSION_CATALOG_TABLE &&
		YBGetDdlNestingLevel() > 0)
		return yb_catalog_cache_version + 1;

	return yb_catalog_cache_version;
}

uint64_t
YbGetCatalogCacheVersion()
{
	return yb_catalog_cache_version;
}

uint64_t
YbGetLastKnownCatalogCacheVersion()
{
	const uint64_t shared_catalog_version = YbGetSharedCatalogVersion();
	return shared_catalog_version > yb_last_known_catalog_cache_version ?
		shared_catalog_version : yb_last_known_catalog_cache_version;
}

YBCPgLastKnownCatalogVersionInfo
YbGetCatalogCacheVersionForTablePrefetching()
{
	// TODO: In future YBGetLastKnownCatalogCacheVersion must be used instead of
	//       YbGetMasterCatalogVersion to reduce numer of RPCs to a master.
	//       But this requires some additional changes. This optimization will
	//       be done separately.
	uint64_t version = YB_CATCACHE_VERSION_UNINITIALIZED;
	bool is_db_catalog_version_mode = YBIsDBCatalogVersionMode();
	if (*YBCGetGFlags()->ysql_enable_read_request_caching)
	{
		YBCPgResetCatalogReadTime();
		version = YbGetMasterCatalogVersion();
	}
	return (YBCPgLastKnownCatalogVersionInfo){
		.version = version,
		.is_db_catalog_version_mode = is_db_catalog_version_mode};
}

void
YbUpdateCatalogCacheVersion(uint64_t catalog_cache_version)
{
	yb_catalog_cache_version = catalog_cache_version;
	yb_pgstat_set_catalog_version(yb_catalog_cache_version);
	YbUpdateLastKnownCatalogCacheVersion(yb_catalog_cache_version);
	if (*YBCGetGFlags()->log_ysql_catalog_versions)
		ereport(LOG,
				(errmsg("set local catalog version: %" PRIu64,
						yb_catalog_cache_version)));
}

void
YbUpdateLastKnownCatalogCacheVersion(uint64_t catalog_cache_version)
{
	if (yb_last_known_catalog_cache_version < catalog_cache_version)
		yb_last_known_catalog_cache_version	= catalog_cache_version;
}

void
YbResetCatalogCacheVersion()
{
	yb_catalog_cache_version = YB_CATCACHE_VERSION_UNINITIALIZED;
	yb_pgstat_set_catalog_version(yb_catalog_cache_version);
}

/** These values are lazily initialized based on corresponding environment variables. */
int ybc_pg_double_write = -1;
int ybc_disable_pg_locking = -1;

/* Forward declarations */
static void YBCInstallTxnDdlHook();
static bool YBCanEnableDBCatalogVersionMode();

bool yb_enable_docdb_tracing = false;
bool yb_read_from_followers = false;
int32_t yb_follower_read_staleness_ms = 0;

bool
IsYugaByteEnabled()
{
	/* We do not support Init/Bootstrap processing modes yet. */
	return YBCPgIsYugaByteEnabled();
}

void
CheckIsYBSupportedRelation(Relation relation)
{
	const char relkind = relation->rd_rel->relkind;
	CheckIsYBSupportedRelationByKind(relkind);
}

void
CheckIsYBSupportedRelationByKind(char relkind)
{
	if (!(relkind == RELKIND_RELATION || relkind == RELKIND_INDEX ||
		  relkind == RELKIND_VIEW || relkind == RELKIND_SEQUENCE ||
		  relkind == RELKIND_COMPOSITE_TYPE || relkind == RELKIND_PARTITIONED_TABLE ||
		  relkind == RELKIND_PARTITIONED_INDEX || relkind == RELKIND_FOREIGN_TABLE ||
		  relkind == RELKIND_MATVIEW))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								errmsg("This feature is not supported in YugaByte.")));
}

bool
IsYBRelation(Relation relation)
{
	/*
	 * NULL relation is possible if regular ForeignScan is confused for
	 * Yugabyte sequential scan, which is backed by ForeignScan, too.
	 * Rather than performing probably not trivial and unreliable checks by
	 * the caller to distinguish them, we allow NULL argument here.
	 */
	if (!IsYugaByteEnabled() || !relation)
		return false;

	const char relkind = relation->rd_rel->relkind;

	CheckIsYBSupportedRelationByKind(relkind);

	/* Currently only support regular tables and indexes.
	 * Temp tables and views are supported, but they are not YB relations. */
	return (relkind == RELKIND_RELATION || relkind == RELKIND_INDEX || relkind == RELKIND_PARTITIONED_TABLE ||
			relkind == RELKIND_PARTITIONED_INDEX || relkind == RELKIND_MATVIEW) &&
			relation->rd_rel->relpersistence != RELPERSISTENCE_TEMP;
}

bool
IsYBRelationById(Oid relid)
{
	Relation relation     = RelationIdGetRelation(relid);
	bool     is_supported = IsYBRelation(relation);
	RelationClose(relation);
	return is_supported;
}

bool
IsYBBackedRelation(Relation relation)
{
	return IsYBRelation(relation) ||
		(relation->rd_rel->relkind == RELKIND_VIEW &&
		relation->rd_rel->relpersistence != RELPERSISTENCE_TEMP);
}

bool
YbIsTempRelation(Relation relation)
{
	return relation->rd_rel->relpersistence == RELPERSISTENCE_TEMP;
}

bool IsRealYBColumn(Relation rel, int attrNum)
{
	return (attrNum > 0 && !TupleDescAttr(rel->rd_att, attrNum - 1)->attisdropped) ||
		   (rel->rd_rel->relhasoids && attrNum == ObjectIdAttributeNumber);
}

bool IsYBSystemColumn(int attrNum)
{
	return (attrNum == YBRowIdAttributeNumber ||
			attrNum == YBIdxBaseTupleIdAttributeNumber ||
			attrNum == YBUniqueIdxKeySuffixAttributeNumber);
}

AttrNumber YBGetFirstLowInvalidAttributeNumber(Relation relation)
{
	return IsYBRelation(relation)
		   ? YBFirstLowInvalidAttributeNumber
		   : FirstLowInvalidHeapAttributeNumber;
}

AttrNumber YBGetFirstLowInvalidAttributeNumberFromOid(Oid relid)
{
	Relation   relation = RelationIdGetRelation(relid);
	AttrNumber attr_num = YBGetFirstLowInvalidAttributeNumber(relation);
	RelationClose(relation);
	return attr_num;
}

int YBAttnumToBmsIndex(Relation rel, AttrNumber attnum)
{
	return YBAttnumToBmsIndexWithMinAttr(
		YBGetFirstLowInvalidAttributeNumber(rel), attnum);
}

AttrNumber YBBmsIndexToAttnum(Relation rel, int idx)
{
	return YBBmsIndexToAttnumWithMinAttr(
		YBGetFirstLowInvalidAttributeNumber(rel), idx);
}

int YBAttnumToBmsIndexWithMinAttr(AttrNumber minattr, AttrNumber attnum)
{
	return attnum - minattr + 1;
}

AttrNumber YBBmsIndexToAttnumWithMinAttr(AttrNumber minattr, int idx)
{
	return idx + minattr - 1;
}

/*
 * Get primary key columns as bitmap of a table,
 * subtracting minattr from attributes.
 */
static Bitmapset *GetTablePrimaryKeyBms(Relation rel,
										AttrNumber minattr,
										bool includeYBSystemColumns)
{
	Oid            dboid         = YBCGetDatabaseOid(rel);
	int            natts         = RelationGetNumberOfAttributes(rel);
	Bitmapset      *pkey         = NULL;
	YBCPgTableDesc ybc_tabledesc = NULL;
	MemoryContext  oldctx;

	/* Get the primary key columns 'pkey' from YugaByte. */
	HandleYBStatus(YBCPgGetTableDesc(dboid, YbGetRelfileNodeId(rel),
		&ybc_tabledesc));
	oldctx = MemoryContextSwitchTo(CacheMemoryContext);
	for (AttrNumber attnum = minattr; attnum <= natts; attnum++)
	{
		if ((!includeYBSystemColumns && !IsRealYBColumn(rel, attnum)) ||
			(!IsRealYBColumn(rel, attnum) && !IsYBSystemColumn(attnum)))
		{
			continue;
		}

		YBCPgColumnInfo column_info = {0};
		HandleYBTableDescStatus(YBCPgGetColumnInfo(ybc_tabledesc,
												   attnum,
												   &column_info),
								ybc_tabledesc);

		if (column_info.is_hash || column_info.is_primary)
		{
			pkey = bms_add_member(pkey, attnum - minattr);
		}
	}

	MemoryContextSwitchTo(oldctx);
	return pkey;
}

Bitmapset *YBGetTablePrimaryKeyBms(Relation rel)
{
	if (!rel->primary_key_bms) {
		rel->primary_key_bms = GetTablePrimaryKeyBms(
			rel,
			YBGetFirstLowInvalidAttributeNumber(rel) /* minattr */,
			false /* includeYBSystemColumns */);
	}
	return rel->primary_key_bms;
}

Bitmapset *YBGetTableFullPrimaryKeyBms(Relation rel)
{
	if (!rel->full_primary_key_bms) {
		rel->full_primary_key_bms = GetTablePrimaryKeyBms(
			rel,
			YBSystemFirstLowInvalidAttributeNumber + 1 /* minattr */,
			true /* includeYBSystemColumns */);
	}
	return rel->full_primary_key_bms;
}

extern bool YBRelHasOldRowTriggers(Relation rel, CmdType operation)
{
	TriggerDesc *trigdesc = rel->trigdesc;
	if (!trigdesc)
	{
		return false;
	}
	if (operation == CMD_DELETE)
	{
		return trigdesc->trig_delete_after_row ||
			   trigdesc->trig_delete_before_row;
	}
	if (operation != CMD_UPDATE)
	{
			return false;
	}
	if (rel->rd_rel->relkind != RELKIND_PARTITIONED_TABLE &&
		!rel->rd_rel->relispartition)
	{
		return trigdesc->trig_update_after_row ||
			   trigdesc->trig_update_before_row;
	}
	/*
	 * This is an update operation. We look for both update and delete triggers
	 * as update on partitioned tables can result in deletes as well.
	 */
	return trigdesc->trig_update_after_row ||
		 trigdesc->trig_update_before_row ||
		 trigdesc->trig_delete_after_row ||
		 trigdesc->trig_delete_before_row;
}

bool
YbIsDatabaseColocated(Oid dbid, bool *legacy_colocated_database)
{
	bool colocated;
	HandleYBStatus(YBCPgIsDatabaseColocated(dbid, &colocated,
											legacy_colocated_database));
	return colocated;
}

bool
YBRelHasSecondaryIndices(Relation relation)
{
	if (!relation->rd_rel->relhasindex)
		return false;

	bool	 has_indices = false;
	List	 *indexlist = RelationGetIndexList(relation);
	ListCell *lc;

	foreach(lc, indexlist)
	{
		if (lfirst_oid(lc) == relation->rd_pkindex)
			continue;
		has_indices = true;
		break;
	}

	list_free(indexlist);

	return has_indices;
}

bool
YBTransactionsEnabled()
{
	static int cached_value = -1;
	if (cached_value == -1)
	{
		cached_value = YBCIsEnvVarTrueWithDefault("YB_PG_TRANSACTIONS_ENABLED", true);
	}
	return IsYugaByteEnabled() && cached_value;
}

bool
YBIsReadCommittedSupported()
{
	static int cached_value = -1;
	if (cached_value == -1)
	{

#ifdef NDEBUG
		cached_value = YBCIsEnvVarTrueWithDefault("FLAGS_yb_enable_read_committed_isolation", false);
#else
		cached_value = YBCIsEnvVarTrueWithDefault("FLAGS_yb_enable_read_committed_isolation", true);
#endif
	}
	return cached_value;
}

bool
IsYBReadCommitted()
{
	return IsYugaByteEnabled() && YBIsReadCommittedSupported() &&
				 (XactIsoLevel == XACT_READ_COMMITTED || XactIsoLevel == XACT_READ_UNCOMMITTED);
}

bool
YBIsWaitQueueEnabled()
{
	static int cached_value = -1;
	if (cached_value == -1)
	{
		cached_value = YBCIsEnvVarTrueWithDefault("FLAGS_enable_wait_queues", true);
	}
	return IsYugaByteEnabled() && cached_value;
}

/*
 * Return true if we are in per-database catalog version mode. In order to
 * use per-database catalog version mode, two conditions must be met:
 *   * --FLAGS_ysql_enable_db_catalog_version_mode=true
 *   * the table pg_yb_catalog_version has one row per database.
 * This function takes care of the YSQL upgrade from global catalog version
 * mode to per-database catalog version mode when the default value of
 * --FLAGS_ysql_enable_db_catalog_version_mode is changed to true. In this
 * upgrade procedure --FLAGS_ysql_enable_db_catalog_version_mode is set to
 * true before the table pg_yb_catalog_version is updated to have one row per
 * database.
 * This function does not consider going from per-database catalog version
 * mode back to global catalog version mode.
 */
bool
YBIsDBCatalogVersionMode()
{
	static bool cached_is_db_catalog_version_mode = false;

	if (cached_is_db_catalog_version_mode)
		return true;

	/*
	 * During bootstrap phase in initdb, CATALOG_VERSION_PROTOBUF_ENTRY is used
	 * for catalog version type.
	 */
	if (!IsYugaByteEnabled() ||
		YbGetCatalogVersionType() != CATALOG_VERSION_CATALOG_TABLE ||
		!*YBCGetGFlags()->ysql_enable_db_catalog_version_mode)
		return false;

	/*
	 * During second phase of initdb, per-db catalog version mode is supported.
	 */
	if (YBCIsInitDbModeEnvVarSet())
	{
		cached_is_db_catalog_version_mode = true;
		return true;
	}

	/*
	 * At this point, we know that FLAGS_ysql_enable_db_catalog_version_mode is
	 * turned on. However in case of YSQL upgrade we may not be ready to enable
	 * per-db catalog version mode yet. Note that we only provide support where
	 * we go from global catalog version mode to per-db catalog version mode,
	 * not for the opposite direction.
	 */
	if (YBCanEnableDBCatalogVersionMode())
	{
		cached_is_db_catalog_version_mode = true;
		/*
		 * If MyDatabaseId is not resolved, the caller is going to set up the
		 * catalog version in per-database catalog version mode. There is
		 * no need to set it up here.
		 */
		if (OidIsValid(MyDatabaseId))
		{
			/*
			 * MyDatabaseId is already resolved so the caller may have already
			 * set up the catalog version in global catalog version mode. The
			 * upgrade of table pg_yb_catalog_version to per-database catalog
			 * version mode does not change the catalog version of database
			 * template1 but will set the initial per-database catalog version
			 * value to 1 for all other databases. Set catalog version to 1
			 * except for database template1 to avoid unnecessary catalog cache
			 * refresh.
			 * Note that we assume there are no DDL statements running during
			 * YSQL upgrade and in particular we do not support concurrent DDL
			 * statements when switching from global catalog version mode to
			 * per-database catalog version mode. As of 2023-08-07, this is not
			 * enforced and therefore if a concurrent DDL statement is executed:
			 * (1) if this DDL statement also increments a table schema, we still
			 * have the table schema version mismatch check as a safety net to
			 * reject stale read/write RPCs;
			 * (2) if this DDL statement only increments the catalog version,
			 * then stale read/write RPCs are possible which can lead to wrong
			 * results;
			 */
			elog(LOG, "change to per-db mode");
			if (MyDatabaseId != TemplateDbOid)
			{
				yb_last_known_catalog_cache_version = 1;
				YbUpdateCatalogCacheVersion(1);
			}
		}

		/*
		 * YB does write operation buffering to reduce the number of RPCs.
		 * That is, PG backend can buffer several write operations and send
		 * them out in a single RPC. Here we dynamically switch from global
		 * catalog version mode to per-database catalog version mode, so
		 * flush the buffered write operations. Otherwise, we can end up
		 * having the first write operations in global catalog version mode,
		 * and the rest write operations in per-database catalog version.
		 * Mixing global and per-database catalog versions in a single RPC
		 * triggers a tserver SCHECK failure.
		 */
		YBFlushBufferedOperations();
		return true;
	}

	/* We cannot enable per-db catalog version mode yet. */
	return false;
}

static bool
YBCanEnableDBCatalogVersionMode()
{
	/*
	 * Even when FLAGS_ysql_enable_db_catalog_version_mode is turned on we
	 * cannot simply enable per-database catalog mode if the table
	 * pg_yb_catalog_version does not have one row for each database.
	 * Consider YSQL upgrade, it happens after cluster software upgrade and
	 * can take time. During YSQL upgrade we need to wait until the
	 * pg_yb_catalog_version table is updated to have one row per database.
	 * In addition, we do not want to switch to per-database catalog version
	 * mode at any moment to prevent the following case:
	 *
	 * (1) At time t1, pg_yb_catalog_version is prefetched and there is only
	 * one row in the table because the table has not been upgraded yet.
	 * (2) At time t2 > t1, pg_yb_catalog_version is transactionally upgraded
	 * to have one row per database.
	 * (3) At time t3 > t2, assume that we already switched to per-database
	 * catalog version mode, then we will try to find the row of MyDatabaseId
	 * from the pg_yb_catalog_version data prefetched in step (1). That row
	 * would not exist because at time t1 pg_yb_catalog_version only had one
	 * row for template1. This is going to cause a user visible exception.
	 *
	 * Therefore after the pg_yb_catalog_version is upgraded, we may continue
	 * to remain on global catalog version mode until we are not doing
	 * prefetching.
	 */
	if (YBCIsSysTablePrefetchingStarted())
		return false;

	if (yb_test_stay_in_global_catalog_version_mode)
		return false;

	/*
	 * We assume that the table pg_yb_catalog_version has either exactly
	 * one row in global catalog version mode, or one row per database in
	 * per-database catalog version mode. It is unexpected if it has more
	 * than one rows but not exactly one row per database. During YSQL
	 * upgrade, the pg_yb_catalog_version is transactionally updated
	 * to have one row per database.
	 */
	return YbCatalogVersionTableInPerdbMode();
}

/*
 * Used to determine whether we should preload certain catalog tables.
 */
bool YbNeedAdditionalCatalogTables()
{
	return *YBCGetGFlags()->ysql_catalog_preload_additional_tables ||
			IS_NON_EMPTY_STR_FLAG(YBCGetGFlags()->ysql_catalog_preload_additional_table_list);
}

void
YBReportFeatureUnsupported(const char *msg)
{
	ereport(ERROR,
			(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			 errmsg("%s", msg)));
}

static const char*
FetchUniqueConstraintName(Oid relation_id)
{
	const char* name = NULL;
	Relation rel = RelationIdGetRelation(relation_id);

	if (!rel->rd_index && rel->rd_pkindex != InvalidOid)
	{
		Relation pkey = RelationIdGetRelation(rel->rd_pkindex);

		name = pstrdup(RelationGetRelationName(pkey));

		RelationClose(pkey);
	}
	else
		name = pstrdup(RelationGetRelationName(rel));

	RelationClose(rel);
	return name;
}

/*
 * GetStatusMsgAndArgumentsByCode - get error message arguments out of the
 * status codes
 *
 * We already have cases when DocDB returns status with SQL code and
 * relation Oid, but without error message, assuming the message is generated
 * on Postgres side, with relation name retrieved by Oid. We have to keep
 * the functionality for backward compatibility.
 *
 * Same approach can be used for similar cases, when status is originated from
 * DocDB: by known SQL code the function may set or amend the error message and
 * message arguments.
 */
void
GetStatusMsgAndArgumentsByCode(const uint32_t pg_err_code,
							   uint16_t txn_err_code, YBCStatus s,
							   const char **msg_buf, size_t *msg_nargs,
							   const char ***msg_args, const char **detail_buf,
							   size_t *detail_nargs, const char ***detail_args)
{
	const char	*status_msg = YBCMessageAsCString(s);
	size_t		 status_nargs;
	const char **status_args = YBCStatusArguments(s, &status_nargs);


	// Initialize message and detail buffers with default values
	*msg_buf = status_msg;
	*msg_nargs = status_nargs;
	*msg_args = status_args;
	*detail_buf = NULL;
	*detail_nargs = 0;
	*detail_args = NULL;

	switch(pg_err_code)
	{
		case ERRCODE_T_R_SERIALIZATION_FAILURE:
			if(YBCIsTxnConflictError(txn_err_code))
			{
				*msg_buf = "could not serialize access due to concurrent update";
				*msg_nargs = 0;
				*msg_args = NULL;

				*detail_buf = status_msg;
				*detail_nargs = status_nargs;
				*detail_args = status_args;
			}
			else if(YBCIsTxnAbortedError(txn_err_code))
			{
				*msg_buf = "current transaction is expired or aborted";
				*msg_nargs = 0;
				*msg_args = NULL;

				*detail_buf = status_msg;
				*detail_nargs = status_nargs;
				*detail_args = status_args;
			}
			break;
		case ERRCODE_UNIQUE_VIOLATION:
			*msg_buf = "duplicate key value violates unique constraint \"%s\"";
			*msg_nargs = 1;
			*msg_args = (const char **) palloc(sizeof(const char *));
			(*msg_args)[0] = FetchUniqueConstraintName(YBCStatusRelationOid(s));
			break;
		case ERRCODE_T_R_DEADLOCK_DETECTED:
			if (YBCIsTxnDeadlockError(txn_err_code)) {
				*msg_buf = "deadlock detected";
				*msg_nargs = 0;
				*msg_args = NULL;

				*detail_buf = status_msg;
				*detail_nargs = status_nargs;
				*detail_args = status_args;
			}
			break;
		default:
			break;
	}
}

void
HandleYBStatusIgnoreNotFound(YBCStatus status, bool *not_found)
{
	if (!status)
		return;

	if (YBCStatusIsNotFound(status))
	{
		*not_found = true;
		YBCFreeStatus(status);
		return;
	}
	*not_found = false;
	HandleYBStatus(status);
}

void
HandleYBStatusWithCustomErrorForNotFound(YBCStatus status,
										 const char *message_for_not_found)
{
	bool		not_found = false;

	HandleYBStatusIgnoreNotFound(status, &not_found);

	if (not_found)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("%s", message_for_not_found)));
}

void
HandleYBTableDescStatus(YBCStatus status, YBCPgTableDesc table)
{
	if (!status)
		return;

	HandleYBStatus(status);
}

static const char*
GetDebugQueryString()
{
	return debug_query_string;
}

/*
 * Ensure we've defined the correct postgres Oid values. This function only
 * contains compile-time assertions. It would have been made 'static' but it is
 * not called anywhere and making it 'static' caused compiler warning which
 * broke the build.
 */
void
YBCheckDefinedOids()
{
	static_assert(kInvalidOid == InvalidOid, "Oid mismatch");
	static_assert(kByteArrayOid == BYTEAOID, "Oid mismatch");
}

/*
 * Holds the RPC/Storage execution stats for the session. A handle to this
 * struct is passed down to pggate which record updates to the stats as they
 * happen. This model helps avoid making copies of the stats and passing it
 * back/forth.
 */
typedef struct YbSessionStats
{
	YBCPgExecStatsState current_state;
	YBCPgExecStats		latest_snapshot;
} YbSessionStats;

static YbSessionStats yb_session_stats = {0};

void
YBInitPostgresBackend(
	const char *program_name,
	const char *db_name,
	const char *user_name,
	uint64_t *session_id)
{
	HandleYBStatus(YBCInit(program_name, palloc, cstring_to_text_with_len));

	/*
	 * Enable "YB mode" for PostgreSQL so that we will initiate a connection
	 * to the YugaByte cluster right away from every backend process. We only

	 * do this if this env variable is set, so we can still run the regular
	 * PostgreSQL "make check".
	 */
	if (YBIsEnabledInPostgresEnvVar())
	{
		const YBCPgTypeEntity *type_table;
		int count;
		YbGetTypeTable(&type_table, &count);
		YBCPgCallbacks callbacks;
		callbacks.GetCurrentYbMemctx = &GetCurrentYbMemctx;
		callbacks.GetDebugQueryString = &GetDebugQueryString;
		callbacks.WriteExecOutParam = &YbWriteExecOutParam;
		callbacks.UnixEpochToPostgresEpoch = &YbUnixEpochToPostgresEpoch;
		callbacks.ConstructArrayDatum = &YbConstructArrayDatum;
		callbacks.CheckUserMap = &check_usermap;
		callbacks.PgstatReportWaitStart = &yb_pgstat_report_wait_start;
		YBCPgAshConfig ash_config;
		ash_config.metadata = &MyProc->yb_ash_metadata;
		ash_config.is_metadata_set = &MyProc->yb_is_ash_metadata_set;
		ash_config.yb_enable_ash = &yb_enable_ash;
		YBCInitPgGate(type_table, count, callbacks, session_id, &ash_config);
		YBCInstallTxnDdlHook();
		if (yb_ash_enable_infra)
			YbAshInstallHooks();

		/*
		 * For each process, we create one YBC session for PostgreSQL to use
		 * when accessing YugaByte storage.
		 *
		 * TODO: do we really need to DB name / username here?
		 */
		HandleYBStatus(YBCPgInitSession(db_name ? db_name : user_name,
										&yb_session_stats.current_state));
		YBCSetTimeout(StatementTimeout, NULL);

		/*
		 * Upon completion of the first heartbeat to the local tserver, retrieve
		 * and store the session ID in shared memory, so that entities
		 * associated with a session ID (like txn IDs) can be transitively
		 * mapped to PG backends.
		 */
		yb_pgstat_add_session_info(YBCPgGetSessionID());
		if (yb_ash_enable_infra)
			YbAshSetSessionId(YBCPgGetSessionID());
	}
}

void
YBOnPostgresBackendShutdown()
{
	YBCDestroyPgGate();
}

void
YBCRecreateTransaction()
{
	if (!IsYugaByteEnabled())
		return;
	HandleYBStatus(YBCPgRecreateTransaction());
}

void
YBCRestartTransaction()
{
	if (!IsYugaByteEnabled())
		return;
	HandleYBStatus(YBCPgRestartTransaction());
}

void
YBCCommitTransaction()
{
	if (!IsYugaByteEnabled())
		return;

	HandleYBStatus(YBCPgCommitTransaction());
}

void
YBCAbortTransaction()
{
	if (!IsYugaByteEnabled())
		return;

	if (YBTransactionsEnabled())
		HandleYBStatus(YBCPgAbortTransaction());
}

void
YBCSetActiveSubTransaction(SubTransactionId id)
{
	HandleYBStatus(YBCPgSetActiveSubTransaction(id));
}

void
YBCRollbackToSubTransaction(SubTransactionId id)
{
	HandleYBStatus(YBCPgRollbackToSubTransaction(id));
}

bool
YBIsPgLockingEnabled()
{
	return !YBTransactionsEnabled();
}

static bool yb_connected_to_template_db = false;

void
YbSetConnectedToTemplateDb()
{
	yb_connected_to_template_db = true;
}

bool
YbIsConnectedToTemplateDb()
{
	return yb_connected_to_template_db;
}

Oid
GetTypeId(int attrNum, TupleDesc tupleDesc)
{
	switch (attrNum)
	{
		case SelfItemPointerAttributeNumber:
			return TIDOID;
		case ObjectIdAttributeNumber:
			return OIDOID;
		case MinTransactionIdAttributeNumber:
			return XIDOID;
		case MinCommandIdAttributeNumber:
			return CIDOID;
		case MaxTransactionIdAttributeNumber:
			return XIDOID;
		case MaxCommandIdAttributeNumber:
			return CIDOID;
		case TableOidAttributeNumber:
			return OIDOID;
		default:
			if (attrNum > 0 && attrNum <= tupleDesc->natts)
				return TupleDescAttr(tupleDesc, attrNum - 1)->atttypid;
			else
				return InvalidOid;
	}
}

const char*
YBPgTypeOidToStr(Oid type_id) {
	switch (type_id) {
		case BOOLOID: return "BOOL";
		case BYTEAOID: return "BYTEA";
		case CHAROID: return "CHAR";
		case NAMEOID: return "NAME";
		case INT8OID: return "INT8";
		case INT2OID: return "INT2";
		case INT2VECTOROID: return "INT2VECTOR";
		case INT4OID: return "INT4";
		case REGPROCOID: return "REGPROC";
		case TEXTOID: return "TEXT";
		case OIDOID: return "OID";
		case TIDOID: return "TID";
		case XIDOID: return "XID";
		case CIDOID: return "CID";
		case OIDVECTOROID: return "OIDVECTOR";
		case JSONOID: return "JSON";
		case XMLOID: return "XML";
		case PGNODETREEOID: return "PGNODETREE";
		case PGNDISTINCTOID: return "PGNDISTINCT";
		case PGDEPENDENCIESOID: return "PGDEPENDENCIES";
		case PGDDLCOMMANDOID: return "PGDDLCOMMAND";
		case POINTOID: return "POINT";
		case LSEGOID: return "LSEG";
		case PATHOID: return "PATH";
		case BOXOID: return "BOX";
		case POLYGONOID: return "POLYGON";
		case LINEOID: return "LINE";
		case FLOAT4OID: return "FLOAT4";
		case FLOAT8OID: return "FLOAT8";
		case ABSTIMEOID: return "ABSTIME";
		case RELTIMEOID: return "RELTIME";
		case TINTERVALOID: return "TINTERVAL";
		case UNKNOWNOID: return "UNKNOWN";
		case CIRCLEOID: return "CIRCLE";
		case CASHOID: return "CASH";
		case MACADDROID: return "MACADDR";
		case INETOID: return "INET";
		case CIDROID: return "CIDR";
		case MACADDR8OID: return "MACADDR8";
		case INT2ARRAYOID: return "INT2ARRAY";
		case INT4ARRAYOID: return "INT4ARRAY";
		case TEXTARRAYOID: return "TEXTARRAY";
		case OIDARRAYOID: return "OIDARRAY";
		case FLOAT4ARRAYOID: return "FLOAT4ARRAY";
		case ACLITEMOID: return "ACLITEM";
		case CSTRINGARRAYOID: return "CSTRINGARRAY";
		case BPCHAROID: return "BPCHAR";
		case VARCHAROID: return "VARCHAR";
		case DATEOID: return "DATE";
		case TIMEOID: return "TIME";
		case TIMESTAMPOID: return "TIMESTAMP";
		case TIMESTAMPTZOID: return "TIMESTAMPTZ";
		case INTERVALOID: return "INTERVAL";
		case TIMETZOID: return "TIMETZ";
		case BITOID: return "BIT";
		case VARBITOID: return "VARBIT";
		case NUMERICOID: return "NUMERIC";
		case REFCURSOROID: return "REFCURSOR";
		case REGPROCEDUREOID: return "REGPROCEDURE";
		case REGOPEROID: return "REGOPER";
		case REGOPERATOROID: return "REGOPERATOR";
		case REGCLASSOID: return "REGCLASS";
		case REGTYPEOID: return "REGTYPE";
		case REGROLEOID: return "REGROLE";
		case REGNAMESPACEOID: return "REGNAMESPACE";
		case REGTYPEARRAYOID: return "REGTYPEARRAY";
		case UUIDOID: return "UUID";
		case LSNOID: return "LSN";
		case TSVECTOROID: return "TSVECTOR";
		case GTSVECTOROID: return "GTSVECTOR";
		case TSQUERYOID: return "TSQUERY";
		case REGCONFIGOID: return "REGCONFIG";
		case REGDICTIONARYOID: return "REGDICTIONARY";
		case JSONBOID: return "JSONB";
		case INT4RANGEOID: return "INT4RANGE";
		case RECORDOID: return "RECORD";
		case RECORDARRAYOID: return "RECORDARRAY";
		case CSTRINGOID: return "CSTRING";
		case ANYOID: return "ANY";
		case ANYARRAYOID: return "ANYARRAY";
		case VOIDOID: return "VOID";
		case TRIGGEROID: return "TRIGGER";
		case EVTTRIGGEROID: return "EVTTRIGGER";
		case LANGUAGE_HANDLEROID: return "LANGUAGE_HANDLER";
		case INTERNALOID: return "INTERNAL";
		case OPAQUEOID: return "OPAQUE";
		case ANYELEMENTOID: return "ANYELEMENT";
		case ANYNONARRAYOID: return "ANYNONARRAY";
		case ANYENUMOID: return "ANYENUM";
		case FDW_HANDLEROID: return "FDW_HANDLER";
		case INDEX_AM_HANDLEROID: return "INDEX_AM_HANDLER";
		case TSM_HANDLEROID: return "TSM_HANDLER";
		case ANYRANGEOID: return "ANYRANGE";
		default: return "user_defined_type";
	}
}

const char*
YBCPgDataTypeToStr(YBCPgDataType yb_type) {
	switch (yb_type) {
		case YB_YQL_DATA_TYPE_NOT_SUPPORTED: return "NOT_SUPPORTED";
		case YB_YQL_DATA_TYPE_UNKNOWN_DATA: return "UNKNOWN_DATA";
		case YB_YQL_DATA_TYPE_NULL_VALUE_TYPE: return "NULL_VALUE_TYPE";
		case YB_YQL_DATA_TYPE_INT8: return "INT8";
		case YB_YQL_DATA_TYPE_INT16: return "INT16";
		case YB_YQL_DATA_TYPE_INT32: return "INT32";
		case YB_YQL_DATA_TYPE_INT64: return "INT64";
		case YB_YQL_DATA_TYPE_STRING: return "STRING";
		case YB_YQL_DATA_TYPE_BOOL: return "BOOL";
		case YB_YQL_DATA_TYPE_FLOAT: return "FLOAT";
		case YB_YQL_DATA_TYPE_DOUBLE: return "DOUBLE";
		case YB_YQL_DATA_TYPE_BINARY: return "BINARY";
		case YB_YQL_DATA_TYPE_TIMESTAMP: return "TIMESTAMP";
		case YB_YQL_DATA_TYPE_DECIMAL: return "DECIMAL";
		case YB_YQL_DATA_TYPE_VARINT: return "VARINT";
		case YB_YQL_DATA_TYPE_INET: return "INET";
		case YB_YQL_DATA_TYPE_LIST: return "LIST";
		case YB_YQL_DATA_TYPE_MAP: return "MAP";
		case YB_YQL_DATA_TYPE_SET: return "SET";
		case YB_YQL_DATA_TYPE_UUID: return "UUID";
		case YB_YQL_DATA_TYPE_TIMEUUID: return "TIMEUUID";
		case YB_YQL_DATA_TYPE_TUPLE: return "TUPLE";
		case YB_YQL_DATA_TYPE_TYPEARGS: return "TYPEARGS";
		case YB_YQL_DATA_TYPE_USER_DEFINED_TYPE: return "USER_DEFINED_TYPE";
		case YB_YQL_DATA_TYPE_FROZEN: return "FROZEN";
		case YB_YQL_DATA_TYPE_DATE: return "DATE";
		case YB_YQL_DATA_TYPE_TIME: return "TIME";
		case YB_YQL_DATA_TYPE_JSONB: return "JSONB";
		case YB_YQL_DATA_TYPE_UINT8: return "UINT8";
		case YB_YQL_DATA_TYPE_UINT16: return "UINT16";
		case YB_YQL_DATA_TYPE_UINT32: return "UINT32";
		case YB_YQL_DATA_TYPE_UINT64: return "UINT64";
		default: return "unknown";
	}
}

void
YBReportIfYugaByteEnabled()
{
	if (YBIsEnabledInPostgresEnvVar()) {
		ereport(LOG, (errmsg(
			"YugaByte is ENABLED in PostgreSQL. Transactions are %s.",
			YBCIsEnvVarTrue("YB_PG_TRANSACTIONS_ENABLED") ?
			"enabled" : "disabled")));
	} else {
		ereport(LOG, (errmsg("YugaByte is NOT ENABLED -- "
							"this is a vanilla PostgreSQL server!")));
	}
}

bool
YBShouldRestartAllChildrenIfOneCrashes() {
	if (!YBIsEnabledInPostgresEnvVar()) {
		ereport(LOG, (errmsg("YBShouldRestartAllChildrenIfOneCrashes returning 0, YBIsEnabledInPostgresEnvVar is false")));
		return true;
	}
	// We will use PostgreSQL's default behavior (restarting all children if one of them crashes)
	// if the flag env variable is not specified or the file pointed by it does not exist.
	return YBCIsEnvVarTrueWithDefault("FLAGS_yb_pg_terminate_child_backend", true);
}

bool
YBShouldLogStackTraceOnError()
{
	static int cached_value = -1;
	if (cached_value != -1)
	{
		return cached_value;
	}

	cached_value = YBCIsEnvVarTrue("YB_PG_STACK_TRACE_ON_ERROR");
	return cached_value;
}

const char*
YBPgErrorLevelToString(int elevel) {
	switch (elevel)
	{
		case DEBUG5: return "DEBUG5";
		case DEBUG4: return "DEBUG4";
		case DEBUG3: return "DEBUG3";
		case DEBUG2: return "DEBUG2";
		case DEBUG1: return "DEBUG1";
		case LOG: return "LOG";
		case LOG_SERVER_ONLY: return "LOG_SERVER_ONLY";
		case INFO: return "INFO";
		case WARNING: return "WARNING";
		case ERROR: return "ERROR";
		case FATAL: return "FATAL";
		case PANIC: return "PANIC";
		default: return "UNKNOWN";
	}
}

const char*
YBCGetDatabaseName(Oid relid)
{
	/*
	 * Hardcode the names for system db since the cache might not
	 * be initialized during initdb (bootstrap mode).
	 * For shared rels (e.g. pg_database) we may not have a database id yet,
	 * so assuming template1 in that case since that's where shared tables are
	 * stored in YB.
	 * TODO Eventually YB should switch to using oid's everywhere so
	 * that dbname and schemaname should not be needed at all.
	 */
	if (MyDatabaseId == TemplateDbOid || IsSharedRelation(relid))
		return "template1";
	else
		return get_database_name(MyDatabaseId);
}

const char*
YBCGetSchemaName(Oid schemaoid)
{
	/*
	 * Hardcode the names for system namespaces since the cache might not
	 * be initialized during initdb (bootstrap mode).
	 * TODO Eventually YB should switch to using oid's everywhere so
	 * that dbname and schemaname should not be needed at all.
	 */
	if (IsSystemNamespace(schemaoid))
		return "pg_catalog";
	else if (IsToastNamespace(schemaoid))
		return "pg_toast";
	else
		return get_namespace_name(schemaoid);
}

Oid
YBCGetDatabaseOid(Relation rel)
{
	return YBCGetDatabaseOidFromShared(rel->rd_rel->relisshared);
}

Oid
YBCGetDatabaseOidByRelid(Oid relid)
{
	Relation relation    = RelationIdGetRelation(relid);
	bool     relisshared = relation->rd_rel->relisshared;
	RelationClose(relation);
	return YBCGetDatabaseOidFromShared(relisshared);
}

Oid
YBCGetDatabaseOidFromShared(bool relisshared)
{
	return relisshared ? TemplateDbOid : MyDatabaseId;
}

void
YBRaiseNotSupported(const char *msg, int issue_no)
{
	YBRaiseNotSupportedSignal(msg, issue_no, YBUnsupportedFeatureSignalLevel());
}

void
YBRaiseNotSupportedSignal(const char *msg, int issue_no, int signal_level)
{
	if (issue_no > 0)
	{
		ereport(signal_level,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("%s", msg),
				 errhint("See https://github.com/yugabyte/yugabyte-db/issues/%d. "
						 "React with thumbs up to raise its priority", issue_no)));
	}
	else
	{
		ereport(signal_level,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("%s", msg),
				 errhint("Please report the issue on "
						 "https://github.com/YugaByte/yugabyte-db/issues")));
	}
}

double
PowerWithUpperLimit(double base, int exp, double upper_limit)
{
	assert(base >= 1);
	assert(exp >= 0);

	double res = 1.0;
	while (exp)
	{
		if (exp & 1)
			res *= base;
		if (res >= upper_limit)
			return upper_limit;

		exp = exp >> 1;
		base *= base;
	}
	return res;
}

//------------------------------------------------------------------------------
// YB GUC variables.

bool yb_enable_create_with_table_oid = false;
int yb_index_state_flags_update_delay = 1000;
bool yb_enable_expression_pushdown = true;
bool yb_enable_distinct_pushdown = true;
bool yb_enable_index_aggregate_pushdown = true;
bool yb_enable_optimizer_statistics = false;
bool yb_bypass_cond_recheck = true;
bool yb_make_next_ddl_statement_nonbreaking = false;
bool yb_plpgsql_disable_prefetch_in_for_query = false;
bool yb_enable_sequence_pushdown = true;
bool yb_disable_wait_for_backends_catalog_version = false;
bool yb_enable_base_scans_cost_model = false;
int yb_wait_for_backends_catalog_version_timeout = 5 * 60 * 1000;	/* 5 min */
bool yb_prefer_bnl = false;
bool yb_explain_hide_non_deterministic_fields = false;
bool yb_enable_saop_pushdown = true;
int yb_toast_catcache_threshold = -1;

//------------------------------------------------------------------------------
// YB Debug utils.

bool yb_debug_report_error_stacktrace = false;

bool yb_debug_log_catcache_events = false;

bool yb_debug_log_internal_restarts = false;

bool yb_test_system_catalogs_creation = false;

bool yb_test_fail_next_ddl = false;

bool yb_test_fail_next_inc_catalog_version = false;

double yb_test_ybgin_disable_cost_factor = 2.0;

char *yb_test_block_index_phase = "";

char *yb_test_fail_index_state_change = "";

bool yb_test_fail_table_rewrite_after_creation = false;

bool yb_test_stay_in_global_catalog_version_mode = false;

bool yb_test_table_rewrite_keep_old_table = false;

/*
 * These two GUC variables are used together to control whether DDL atomicity
 * is enabled. See comments for the gflag --ysql_yb_enable_ddl_atomicity_infra
 * in common_flags.cc.
 */
bool yb_enable_ddl_atomicity_infra = true;
bool yb_ddl_rollback_enabled = false;

bool yb_silence_advisory_locks_not_supported_error = false;

bool yb_use_hash_splitting_by_default = true;

const char*
YBDatumToString(Datum datum, Oid typid)
{
	Oid			typoutput = InvalidOid;
	bool		typisvarlena = false;

	getTypeOutputInfo(typid, &typoutput, &typisvarlena);
	return OidOutputFunctionCall(typoutput, datum);
}

const char*
YbHeapTupleToString(HeapTuple tuple, TupleDesc tupleDesc)
{
	Datum attr = (Datum) 0;
	int natts = tupleDesc->natts;
	bool isnull = false;
	StringInfoData buf;
	initStringInfo(&buf);

	appendStringInfoChar(&buf, '(');
	for (int attnum = 1; attnum <= natts; ++attnum) {
		attr = heap_getattr(tuple, attnum, tupleDesc, &isnull);
		if (isnull)
		{
			appendStringInfoString(&buf, "null");
		}
		else
		{
			Oid typid = TupleDescAttr(tupleDesc, attnum - 1)->atttypid;
			appendStringInfoString(&buf, YBDatumToString(attr, typid));
		}
		if (attnum != natts) {
			appendStringInfoString(&buf, ", ");
		}
	}
	appendStringInfoChar(&buf, ')');
	return buf.data;
}

const char*
YbBitmapsetToString(Bitmapset *bms)
{
	StringInfo str = makeStringInfo();
	outBitmapset(str, bms);
	return str->data;
}

bool
YBIsInitDbAlreadyDone()
{
	bool done = false;
	HandleYBStatus(YBCPgIsInitDbDone(&done));
	return done;
}

/*---------------------------------------------------------------------------*/
/* Transactional DDL support                                                 */
/*---------------------------------------------------------------------------*/

static ProcessUtility_hook_type prev_ProcessUtility = NULL;
typedef struct CatalogModificationAspects
{
	uint64_t applied;
	uint64_t pending;

} CatalogModificationAspects;

typedef struct DdlTransactionState
{
	int nesting_level;
	MemoryContext mem_context;
	CatalogModificationAspects catalog_modification_aspects;
	bool is_global_ddl;
	NodeTag original_node_tag;
	const char *original_ddl_command_tag;
} DdlTransactionState;

static DdlTransactionState ddl_transaction_state = {0};

static void
MergeCatalogModificationAspects(
	CatalogModificationAspects *aspects, bool apply) {
	if (apply)
		aspects->applied |= aspects->pending;
	aspects->pending = 0;
}

static void
YBResetEnableNonBreakingDDLMode()
{
	/*
	 * Reset yb_make_next_ddl_statement_nonbreaking to avoid its further side
	 * effect that may not be intended.
	 */
	yb_make_next_ddl_statement_nonbreaking = false;
}

/*
 * Release all space allocated in the yb_memctx of a context and all of
 * its descendants, but don't delete the yb_memctx themselves.
 */
static YBCStatus
YbMemCtxReset(MemoryContext context)
{
	AssertArg(MemoryContextIsValid(context));
	for (MemoryContext child = context->firstchild;
		 child != NULL;
		 child = child->nextchild)
	{
		YBCStatus status = YbMemCtxReset(child);
		if (status)
			return status;
	}
	return context->yb_memctx ? YBCPgResetMemctx(context->yb_memctx) : NULL;
}

static void
YBResetDdlState()
{
	YBCStatus status = NULL;
	if (ddl_transaction_state.mem_context)
	{
		if (GetCurrentMemoryContext() == ddl_transaction_state.mem_context)
			MemoryContextSwitchTo(ddl_transaction_state.mem_context->parent);
		/* Reset the yb_memctx of the ddl memory context including its descendants.
		 * This is to ensure that all the operations in this ddl transaction are
		 * completed before we abort the ddl transaction. For example, when a ddl
		 * transaction aborts there may be a PgDocOp in this ddl transaction which
		 * still has a pending Perform operation to pre-fetch the next batch of
		 * rows and the Perform's RPC call has not completed yet. Releasing the ddl
		 * memory context will trigger the call to ~PgDocOp where we'll wait for
		 * the pending operation to complete. Because all the objects allocated
		 * during this ddl transaction are released, we assume they are no longer
		 * needed after the ddl transaction aborts.
		 */
		status = YbMemCtxReset(ddl_transaction_state.mem_context);
	}
	ddl_transaction_state = (struct DdlTransactionState){0};
	YBResetEnableNonBreakingDDLMode();
	HandleYBStatus(YBCPgClearSeparateDdlTxnMode());
	HandleYBStatus(status);
}

int
YBGetDdlNestingLevel()
{
	return ddl_transaction_state.nesting_level;
}

void YbSetIsGlobalDDL()
{
	ddl_transaction_state.is_global_ddl = true;
}

void
YBIncrementDdlNestingLevel(YbDdlMode mode)
{
	if (ddl_transaction_state.nesting_level == 0)
	{
		ddl_transaction_state.mem_context = AllocSetContextCreate(
			GetCurrentMemoryContext(), "aux ddl memory context",
			ALLOCSET_DEFAULT_SIZES);

		MemoryContextSwitchTo(ddl_transaction_state.mem_context);
		HandleYBStatus(YBCPgEnterSeparateDdlTxnMode());
	}
	++ddl_transaction_state.nesting_level;
	ddl_transaction_state.catalog_modification_aspects.pending |= mode;
}

static YbDdlMode
YbCatalogModificationAspectsToDdlMode(uint64_t catalog_modification_aspects)
{
	YbDdlMode mode = catalog_modification_aspects;
	switch(mode)
	{
		case YB_DDL_MODE_NO_ALTERING: switch_fallthrough();
		case YB_DDL_MODE_SILENT_ALTERING: switch_fallthrough();
		case YB_DDL_MODE_VERSION_INCREMENT: switch_fallthrough();
		case YB_DDL_MODE_BREAKING_CHANGE: return mode;
	}
	Assert(false);
	return YB_DDL_MODE_BREAKING_CHANGE;
}

void
YBDecrementDdlNestingLevel()
{
	const bool has_write = YBCPgHasWriteOperationsInDdlTxnMode();
	MergeCatalogModificationAspects(
		&ddl_transaction_state.catalog_modification_aspects, has_write);

	--ddl_transaction_state.nesting_level;
	if (yb_test_fail_next_ddl)
	{
		yb_test_fail_next_ddl = false;
		elog(ERROR, "Failed DDL operation as requested");
	}
	if (ddl_transaction_state.nesting_level == 0)
	{
		/*
		 * We cannot reset the ddl memory context as we do in the abort case
		 * (see YBResetDdlState) because there are cases where objects
		 * allocated during the ddl transaction are still needed after this
		 * ddl transaction commits successfully.
		 */

		if (GetCurrentMemoryContext() == ddl_transaction_state.mem_context)
			MemoryContextSwitchTo(ddl_transaction_state.mem_context->parent);

		YBResetEnableNonBreakingDDLMode();
		bool increment_done = false;
		bool is_silent_altering = false;
		if (has_write)
		{
			const YbDdlMode mode = YbCatalogModificationAspectsToDdlMode(
				ddl_transaction_state.catalog_modification_aspects.applied);

			increment_done =
				(mode & YB_SYS_CAT_MOD_ASPECT_VERSION_INCREMENT) &&
				YbIncrementMasterCatalogVersionTableEntry(
					mode & YB_SYS_CAT_MOD_ASPECT_BREAKING_CHANGE,
					ddl_transaction_state.is_global_ddl,
					ddl_transaction_state.original_ddl_command_tag);

			is_silent_altering = (mode == YB_DDL_MODE_SILENT_ALTERING);
		}

		ddl_transaction_state = (DdlTransactionState) {};

		HandleYBStatus(YBCPgExitSeparateDdlTxnMode(
			MyDatabaseId, is_silent_altering));

		/*
		 * Optimization to avoid redundant cache refresh on the current session
		 * since we should have already updated the cache locally while
		 * applying the DDL changes.
		 * (Doing this after YBCPgExitSeparateDdlTxnMode so it only executes
		 * if DDL txn commit succeeds.)
		 */
		if (increment_done)
			YbUpdateCatalogCacheVersion(YbGetCatalogCacheVersion() + 1);

		List *handles = YBGetDdlHandles();
		ListCell *lc = NULL;
		foreach(lc, handles)
		{
			YBCPgStatement handle = (YBCPgStatement) lfirst(lc);
			/*
			 * At this point we have already applied the DDL in the YSQL layer and
			 * executing the postponed DocDB statement is not strictly required.
			 * Ignore 'NotFound' because DocDB might already notice applied DDL.
			 * See comment for YBGetDdlHandles in xact.h for more details.
			 */
			YBCStatus status = YBCPgExecPostponedDdlStmt(handle);
			if (YBCStatusIsNotFound(status)) {
				YBCFreeStatus(status);
			} else {
				HandleYBStatusAtErrorLevel(status, WARNING);
			}
		}
		YBClearDdlHandles();
	}
}

static Node*
GetActualStmtNode(PlannedStmt *pstmt)
{
	if (nodeTag(pstmt->utilityStmt) == T_ExplainStmt)
	{
		ExplainStmt *stmt = castNode(ExplainStmt, pstmt->utilityStmt);
		Node *actual_stmt = castNode(Query, stmt->query)->utilityStmt;
		if (actual_stmt)
		{
			/*
			 * EXPLAIN statement may have multiple ANALYZE options.
			 * The value of the last one will take effect.
			 */
			bool analyze = false;
			ListCell *lc;
			foreach(lc, stmt->options)
			{
				DefElem *opt = (DefElem *) lfirst(lc);
				if (strcmp(opt->defname, "analyze") == 0)
					analyze = defGetBoolean(opt);
			}
			if (analyze)
				return actual_stmt;
		}
	}
	return pstmt->utilityStmt;
}

YbDdlModeOptional YbGetDdlMode(
	PlannedStmt *pstmt, ProcessUtilityContext context)
{
	bool is_ddl = true;
	bool is_version_increment = true;
	bool is_breaking_change = true;
	bool is_altering_existing_data = false;

	Node *parsetree = GetActualStmtNode(pstmt);
	NodeTag node_tag = nodeTag(parsetree);

	/*
	 * Note: REFRESH MATVIEW (CONCURRENTLY) executes subcommands using SPI.
	 * So, if the context is PROCESS_UTILITY_QUERY (command triggered using
	 * SPI), and the current original node tag is T_RefreshMatViewStmt, do
	 * not update the original node tag.
	 */
	if (context == PROCESS_UTILITY_TOPLEVEL ||
		(context == PROCESS_UTILITY_QUERY &&
		 ddl_transaction_state.original_node_tag != T_RefreshMatViewStmt))
	{
		/*
		 * The node tag from the top-level or atomic process utility must
		 * be persisted so that DDL commands with multiple nested
		 * subcommands can determine whether catalog version should
		 * be incremented.
		 */
		ddl_transaction_state.original_node_tag = node_tag;
		ddl_transaction_state.original_ddl_command_tag =
			CreateCommandTag(parsetree);
	}
	else
	{
		Assert(context == PROCESS_UTILITY_SUBCOMMAND ||
			   context == PROCESS_UTILITY_QUERY_NONATOMIC ||
			   (context == PROCESS_UTILITY_QUERY &&
				ddl_transaction_state.original_node_tag ==
				T_RefreshMatViewStmt));

		is_version_increment = false;
		is_breaking_change = false;
	}

	switch (node_tag) {
		// The lists of tags here have been generated using e.g.:
		// cat $( find src/postgres -name "nodes.h" ) | grep "T_Create" | sort | uniq |
		//   sed 's/,//g' | while read s; do echo -e "\t\tcase $s:"; done
		// All T_Create... tags from nodes.h:

		case T_CreateTableGroupStmt:
		case T_CreateTableSpaceStmt:
		case T_CreatedbStmt:
		case T_DefineStmt: // CREATE OPERATOR/AGGREGATE/COLLATION/etc
		case T_CommentStmt: // COMMENT (create new comment)
		case T_RuleStmt: // CREATE RULE
		case T_YbCreateProfileStmt:
			/*
			 * Simple add objects are not breaking changes, and they do not even require
			 * a version increment because we do not do any negative caching for them.
			 */
			is_version_increment = false;
			is_breaking_change = false;
			break;

		case T_TruncateStmt:
			/*
			 * TRUNCATE (on YB relations) using the old approach does not
			 * make any system catalog changes, so it doesn't require a
			 * version increment.
			 * TRUNCATE using the new rewrite approach changes the
			 * relfilenode field in pg_class, so it requires a version
			 * increment.
			 */
			if (!yb_enable_alter_table_rewrite)
				is_version_increment = false;
			is_breaking_change = false;
			break;

		case T_ViewStmt: // CREATE VIEW
			is_breaking_change = false;

			/*
			 * For system catalog additions we need to force cache refresh
			 * because of negative caching of pg_class and pg_type
			 * (see SearchCatCacheMiss).
			 * Concurrent transaction needs not to be aborted though.
			 */
			if (IsYsqlUpgrade &&
				YbIsSystemNamespaceByName(castNode(ViewStmt, parsetree)->view->schemaname))
				break;

			is_version_increment = false;
			break;

		case T_CompositeTypeStmt: // Create (composite) type
		case T_CreateAmStmt:
		case T_CreateCastStmt:
		case T_CreateConversionStmt:
		case T_CreateDomainStmt: // Create (domain) type
		case T_CreateEnumStmt: // Create (enum) type
		case T_CreateEventTrigStmt:
		case T_CreateExtensionStmt:
		case T_CreateFdwStmt:
		case T_CreateForeignServerStmt:
		case T_CreateForeignTableStmt:
		case T_CreateOpClassItem:
		case T_CreateOpClassStmt:
		case T_CreateOpFamilyStmt:
		case T_CreatePLangStmt:
		case T_CreatePolicyStmt:
		case T_CreatePublicationStmt:
		case T_CreateRangeStmt: // Create (range) type
		case T_CreateReplicationSlotCmd:
		case T_CreateSchemaStmt:
		case T_CreateStatsStmt:
		case T_CreateSubscriptionStmt:
		case T_CreateTransformStmt:
		case T_CreateTrigStmt:
		case T_CreateUserMappingStmt:
			/*
			 * Add objects that may reference/alter other objects so we need to increment the
			 * catalog version to ensure the other objects' metadata is refreshed.
			 * This is either for:
			 * 		- objects that may refresh/alter other objects, to maintain
			 *		  such other objects' consistency and keep their metadata
			 *		  fresh
			 *		- objects where we have negative caching enabled in
			 *		  order to correctly invalidate negative cache entries
			 */
			is_breaking_change = false;
			break;

		case T_CreateRoleStmt:
		{
			is_breaking_change = false;
			/*
			 * If a create role statement does not reference another existing
			 * role there is no need to increment catalog version.
			 */
			CreateRoleStmt *stmt = castNode(CreateRoleStmt, parsetree);
			int nopts = list_length(stmt->options);
			if (nopts == 0)
				is_version_increment = false;
			else
			{
				bool reference_other_role = false;
				ListCell   *lc;
				foreach(lc, stmt->options)
				{
					DefElem *def = (DefElem *) lfirst(lc);
					if (strcmp(def->defname, "rolemembers") == 0 ||
						strcmp(def->defname, "adminmembers") == 0 ||
						strcmp(def->defname, "addroleto") == 0)
					{
						reference_other_role = true;
						break;
					}
				}
				if (!reference_other_role)
					is_version_increment = false;
			}
			break;
		}

		case T_CreateStmt:
		{
			CreateStmt *stmt = castNode(CreateStmt, parsetree);
			is_breaking_change = false;
			/*
			 * If a partition table is being created, this means pg_inherits
			 * table that is being cached should be invalidated. If the cache
			 * is not invalidated here, it is possible that one connection
			 * could create a new partition and insert data into it without
			 * the other connections knowing about this. However, due to
			 * snapshot isolation guarantees, transactions that are already
			 * underway need not abort.
			 */
			if (stmt->partbound)
				break;

			/*
			 * For system catalog additions we need to force cache refresh
			 * because of negative caching of pg_class and pg_type
			 * (see SearchCatCacheMiss).
			 * Concurrent transaction needs not to be aborted though.
			 */
			if (IsYsqlUpgrade &&
				YbIsSystemNamespaceByName(stmt->relation->schemaname))
				break;

			is_version_increment = false;
			break;
		}

		/*
		 * Create Table As Select need not include the same checks as Create Table as complex tables
		 * (eg: partitions) cannot be created using this statement.
		*/
		case T_CreateTableAsStmt:
			/*
			 * Simple add objects are not breaking changes, and they do not even require
			 * a version increment because we do not do any negative caching for them.
			 */
			is_version_increment = false;
			is_breaking_change = false;
			break;

		case T_CreateSeqStmt:
			is_breaking_change = false;
			/* Need to increment if owner is set to ensure its dependency cache is updated. */
			if (!OidIsValid(castNode(CreateSeqStmt, parsetree)->ownerId))
				is_version_increment = false;
			break;

		case T_CreateFunctionStmt:
			is_breaking_change = false;
			if (!castNode(CreateFunctionStmt, parsetree)->replace)
				is_version_increment = false;
			break;

		case T_DiscardStmt: // DISCARD ALL/SEQUENCES/TEMP
			/*
			 * This command alters existing data. But this update affects only
			 * objects of current connection. No version increment is required.
			 */
			is_breaking_change = false;
			is_version_increment = false;
			is_altering_existing_data = true;
			break;

		// All T_Drop... tags from nodes.h:
		case T_DropOwnedStmt:
		case T_DropReplicationSlotCmd:
		case T_DropRoleStmt:
		case T_DropSubscriptionStmt:
		case T_DropTableSpaceStmt:
		case T_DropUserMappingStmt:
			break;

		case T_DropStmt:
		{
			/*
			 * If this is a DROP statement that is being executed as part of
			 * REFRESH MATVIEW (CONCURRENTLY), we are only dropping temporary
			 * tables, and do not need to increment catalog version.
			 */
			if (ddl_transaction_state.original_node_tag ==
				T_RefreshMatViewStmt)
				is_version_increment = false;
			is_breaking_change = false;
			break;
		}
		case T_YbDropProfileStmt:
			is_breaking_change = false;
			break;

		case T_DropdbStmt:
			/*
			 * We already invalidate all connections to that DB by dropping it
			 * so nothing to do on the cache side.
			 */
			is_breaking_change = false;
			/*
			 * In per-database catalog version mode, we do not need to rely on
			 * catalog cache refresh to check that the database exists. We
			 * detect that the database is dropped as we can no longer find
			 * the row for MyDatabaseId when the table pg_yb_catalog_version
			 * is prefetched from the master. We do need to rely on catalog
			 * cache refresh to check that the database exists in global
			 * catalog version mode.
			 */
			if (YBIsDBCatalogVersionMode())
				is_version_increment = false;
			break;

		// All T_Alter... tags from nodes.h:
		case T_AlterCollationStmt:
		case T_AlterDatabaseSetStmt:
		case T_AlterDatabaseStmt:
		case T_AlterDefaultPrivilegesStmt:
		case T_AlterDomainStmt:
		case T_AlterEnumStmt:
		case T_AlterEventTrigStmt:
		case T_AlterExtensionContentsStmt:
		case T_AlterExtensionStmt:
		case T_AlterFdwStmt:
		case T_AlterForeignServerStmt:
		case T_AlterFunctionStmt:
		case T_AlterObjectDependsStmt:
		case T_AlterObjectSchemaStmt:
		case T_AlterOpFamilyStmt:
		case T_AlterOperatorStmt:
		case T_AlterOwnerStmt:
		case T_AlterPolicyStmt:
		case T_AlterPublicationStmt:
		case T_AlterRoleSetStmt:
		case T_AlterSeqStmt:
		case T_AlterSubscriptionStmt:
		case T_AlterSystemStmt:
		case T_AlterTSConfigurationStmt:
		case T_AlterTSDictionaryStmt:
		case T_AlterTableCmd:
		case T_AlterTableMoveAllStmt:
		case T_AlterTableSpaceOptionsStmt:
		case T_AlterUserMappingStmt:
		case T_AlternativeSubPlan:
		case T_AlternativeSubPlanState:
		case T_ReassignOwnedStmt:
		/* ALTER .. RENAME TO syntax gets parsed into a T_RenameStmt node. */
		case T_RenameStmt:
			break;

		case T_AlterRoleStmt:
		{
			/*
			 * If this is a simple alter role change password statement,
			 * there is no need to increment catalog version. Password
			 * is only used for authentication at connection setup time.
			 * A new password does not affect existing connections that
			 * were authenticated using the old password.
			 */
			AlterRoleStmt *stmt = castNode(AlterRoleStmt, parsetree);
			if (list_length(stmt->options) == 1)
			{
				DefElem *def = (DefElem *) linitial(stmt->options);
				if (strcmp(def->defname, "password") == 0)
				{
					is_breaking_change = false;
					is_version_increment = false;
				}
			}
			break;
		}

		case T_AlterTableStmt:
			is_breaking_change = false;
			/*
			 * Must increment catalog version when creating table with foreign
			 * key reference and refresh PG cache on ongoing transactions.
			 */
			if ((context == PROCESS_UTILITY_SUBCOMMAND ||
				 context == PROCESS_UTILITY_QUERY_NONATOMIC) &&
				ddl_transaction_state.original_node_tag == T_CreateStmt &&
				node_tag == T_AlterTableStmt)
			{
				AlterTableStmt *stmt = castNode(AlterTableStmt, parsetree);
				ListCell   *lcmd;
				foreach(lcmd, stmt->cmds)
				{
					AlterTableCmd *cmd = (AlterTableCmd *) lfirst(lcmd);
					if (IsA(cmd->def, Constraint) &&
						((Constraint *) cmd->def)->contype == CONSTR_FOREIGN)
					{
						is_version_increment = true;
						break;
					}
				}
			}
			break;

		// T_Grant...
		case T_GrantStmt:
			/* Grant (add permission) is not a breaking change, but revoke is. */
			is_breaking_change = !castNode(GrantStmt, parsetree)->is_grant;
			break;

		case T_GrantRoleStmt:
			/* Grant (add permission) is not a breaking change, but revoke is. */
			is_breaking_change = !castNode(GrantRoleStmt, parsetree)->is_grant;
			break;

		// T_Index...
		case T_IndexStmt:
			/*
			 * For nonconcurrent index backfill we do not guarantee global consistency anyway.
			 * For (new) concurrent backfill the backfill process should wait for ongoing
			 * transactions so we don't have to force a transaction abort on PG side.
			 */
			is_breaking_change = false;
			break;

		case T_VacuumStmt:
			/* Vacuum with analyze updates relation and attribute statistics */
			is_version_increment = false;
			is_breaking_change = false;
			is_ddl = castNode(VacuumStmt, parsetree)->options & VACOPT_ANALYZE;
			break;

		case T_RefreshMatViewStmt:
		{
			RefreshMatViewStmt *stmt = castNode(RefreshMatViewStmt, parsetree);
			is_breaking_change = false;
			if (stmt->concurrent)
				/*
				 * REFRESH MATERIALIZED VIEW CONCURRENTLY does not need
				 * a catalog version increment as it does not alter any
				 * metadata. The command only performs data changes.
				 */
				is_version_increment = false;
			else
				/*
				 * REFRESH MATERIALIZED VIEW NONCONCURRENTLY needs a catalog
				 * version increment as it alters the metadata of the
				 * materialized view (pg_class.relfilenode). It does not need
				 * to be a breaking change as materialized views are read-only,
				 * so there is no risk of lost writes. Concurrent SELECTs may
				 * read stale data from the old matview, or fail if the old
				 * matview is dropped.
				 */
				is_version_increment = true;
			break;
		}
		case T_ReindexStmt:
			/*
			 * Does not need catalog version increment since only data changes,
			 * not metadata--unless the data itself is metadata (system index).
			 * It could be nice to force a cache refresh when fixing a system
			 * index corruption, but just because a system index is REINDEXed
			 * doesn't mean it had a corruption. If there's a system index
			 * corruption, manual intervention is already needed, so might as
			 * well let the user deal with refreshing clients.
			 */
			is_version_increment = false;
			is_breaking_change = false;
			break;

		default:
			/* Not a DDL operation. */
			is_ddl = false;
			break;
	}

	if (!is_ddl)
		return (YbDdlModeOptional){};

	/*
	 * If yb_make_next_ddl_statement_nonbreaking is true, then no DDL statement
	 * will cause a breaking catalog change.
	 */
	if (yb_make_next_ddl_statement_nonbreaking)
		is_breaking_change = false;

	is_altering_existing_data |= is_version_increment;

	uint64_t aspects = 0;
	if (is_altering_existing_data)
		aspects |= YB_SYS_CAT_MOD_ASPECT_ALTERING_EXISTING_DATA;

	if (is_version_increment)
		aspects |= YB_SYS_CAT_MOD_ASPECT_VERSION_INCREMENT;

	if (is_breaking_change)
		aspects |= YB_SYS_CAT_MOD_ASPECT_BREAKING_CHANGE;

	return (YbDdlModeOptional){
		.has_value = true,
		.value = YbCatalogModificationAspectsToDdlMode(aspects)
	};
}

static void
YBTxnDdlProcessUtility(
	PlannedStmt *pstmt,
	const char *queryString,
	ProcessUtilityContext context,
	ParamListInfo params,
	QueryEnvironment *queryEnv,
	DestReceiver *dest,
	char *completionTag)
{

	const YbDdlModeOptional ddl_mode = YbGetDdlMode(pstmt, context);

	const bool is_ddl = ddl_mode.has_value;

	PG_TRY();
	{
		if (is_ddl)
		{
			if (YBIsDBCatalogVersionMode())
				/*
				 * In order to support concurrent non-global-impact DDLs
				 * across different databases, call YbInitPinnedCacheIfNeeded
				 * now which triggers a scan of pg_shdepend. This ensure that
				 * the scan is done without using a read time of the DDL
				 * transaction so that yb-master can retry read restarts
				 * automatically. Otherwise, a read restart error is
				 * returned to the PG backend the DDL statement will fail
				 * because DDLs cannot be restarted.
				 *
				 * YB NOTE: this implies a performance hit for DDL statements
				 * that do not need to call YbInitPinnedCacheIfNeeded.
				 */
				YbInitPinnedCacheIfNeeded(true /* shared_only */);

			YBIncrementDdlNestingLevel(ddl_mode.value);
		}

		if (prev_ProcessUtility)
			prev_ProcessUtility(pstmt, queryString,
								context, params, queryEnv,
								dest, completionTag);
		else
			standard_ProcessUtility(pstmt, queryString,
									context, params, queryEnv,
									dest, completionTag);

		if (is_ddl)
			YBDecrementDdlNestingLevel();
	}
	PG_CATCH();
	{
		if (is_ddl)
		{
			/*
			 * It is possible that nesting_level has wrong value due to error.
			 * Ddl transaction state should be reset.
			 */
			YBResetDdlState();
		}
		PG_RE_THROW();
	}
	PG_END_TRY();
}

static void YBCInstallTxnDdlHook() {
	if (!YBCIsInitDbModeEnvVarSet()) {
		prev_ProcessUtility = ProcessUtility_hook;
		ProcessUtility_hook = YBTxnDdlProcessUtility;
	}
};

static unsigned int buffering_nesting_level = 0;

void YBBeginOperationsBuffering() {
	if (++buffering_nesting_level == 1) {
		HandleYBStatus(YBCPgStartOperationsBuffering());
	}
}

void YBEndOperationsBuffering() {
	// buffering_nesting_level could be 0 because YBResetOperationsBuffering was called
	// on starting new query and postgres calls standard_ExecutorFinish on non finished executor
	// from previous failed query.
	if (buffering_nesting_level && !--buffering_nesting_level) {
		HandleYBStatus(YBCPgStopOperationsBuffering());
	}
}

void YBResetOperationsBuffering() {
	buffering_nesting_level = 0;
	YBCPgResetOperationsBuffering();
}

void YBFlushBufferedOperations() {
	HandleYBStatus(YBCPgFlushBufferedOperations());
}

bool YBEnableTracing() {
  return yb_enable_docdb_tracing;
}

bool YBReadFromFollowersEnabled() {
	return yb_read_from_followers;
}

int32_t YBFollowerReadStalenessMs() {
	return yb_follower_read_staleness_ms;
}

YBCPgYBTupleIdDescriptor* YBCCreateYBTupleIdDescriptor(Oid db_oid, Oid table_relfilenode_oid,
	int nattrs) {
	void* mem = palloc(sizeof(YBCPgYBTupleIdDescriptor) + nattrs * sizeof(YBCPgAttrValueDescriptor));
	YBCPgYBTupleIdDescriptor* result = mem;
	result->nattrs = nattrs;
	result->attrs = mem + sizeof(YBCPgYBTupleIdDescriptor);
	result->database_oid = db_oid;
	result->table_relfilenode_oid = table_relfilenode_oid;
	return result;
}

void YBCFillUniqueIndexNullAttribute(YBCPgYBTupleIdDescriptor* descr) {
	YBCPgAttrValueDescriptor* last_attr = descr->attrs + descr->nattrs - 1;
	last_attr->attr_num = YBUniqueIdxKeySuffixAttributeNumber;
	last_attr->type_entity = YbDataTypeFromOidMod(YBUniqueIdxKeySuffixAttributeNumber, BYTEAOID);
	last_attr->collation_id = InvalidOid;
	last_attr->is_null = true;
}

void
YbTestGucBlockWhileStrEqual(char **actual, const char *expected,
							const char *msg)
{
	static const int kSpinWaitMs = 100;
	while (strcmp(*actual, expected) == 0)
	{
		ereport(LOG,
				(errmsg("blocking %s for %dms", msg, kSpinWaitMs),
				 errhidestmt(true),
				 errhidecontext(true)));
		pg_usleep(kSpinWaitMs * 1000);

		/* Reload config in hopes that guc var actual changed. */
		if (ConfigReloadPending)
		{
			ConfigReloadPending = false;
			ProcessConfigFile(PGC_SIGHUP);
		}
	}
}

void
YbTestGucFailIfStrEqual(char *actual, const char *expected)
{
	if (strcmp(actual, expected) == 0)
	{
		ereport(ERROR,
				(errmsg("TEST injected failure at stage %s", expected),
				 errhidestmt(true),
				 errhidecontext(true)));

	}
}

int
YbGetNumberOfFunctionOutputColumns(Oid func_oid)
{
	int ncols = 0; /* Equals to the number of OUT arguments. */

	HeapTuple proctup = SearchSysCache1(PROCOID, ObjectIdGetDatum(func_oid));
	if (!HeapTupleIsValid(proctup))
		elog(ERROR, "cache lookup failed for function %u", func_oid);

	bool is_null = false;
	Datum proargmodes = SysCacheGetAttr(PROCOID, proctup,
										Anum_pg_proc_proargmodes,
										&is_null);
	Assert(!is_null);
	ArrayType* proargmodes_arr = DatumGetArrayTypeP(proargmodes);

	ncols = 0;
	for (int i = 0; i < ARR_DIMS(proargmodes_arr)[0]; ++i)
		if (ARR_DATA_PTR(proargmodes_arr)[i] == PROARGMODE_OUT)
			++ncols;

	ReleaseSysCache(proctup);

	return ncols;
}

/*
 * For backward compatibility, this function dynamically adapts to the number
 * of output columns defined in pg_proc.
 */
Datum
yb_servers(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;

	int expected_ncols = 9;

	static int ncols = 0;

	if (ncols < expected_ncols)
		ncols = YbGetNumberOfFunctionOutputColumns(8019 /* yb_servers function
												   oid hardcoded in pg_proc.dat */);

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;
		TupleDesc tupdesc;

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
		tupdesc = CreateTemplateTupleDesc(ncols, false);

		TupleDescInitEntry(tupdesc, (AttrNumber) 1,
						   "host", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2,
						   "port", INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3,
						   "num_connections", INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 4,
						   "node_type", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 5,
						   "cloud", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 6,
						   "region", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 7,
						   "zone", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 8,
						   "public_ip", TEXTOID, -1, 0);
		if (ncols >= expected_ncols)
		{
			TupleDescInitEntry(tupdesc, (AttrNumber) 9,
							   "uuid", TEXTOID, -1, 0);
		}
		funcctx->tuple_desc = BlessTupleDesc(tupdesc);

		YBCServerDescriptor *servers = NULL;
		size_t numservers = 0;
		HandleYBStatus(YBCGetTabletServerHosts(&servers, &numservers));
		funcctx->max_calls = numservers;
		funcctx->user_fctx = servers;
		MemoryContextSwitchTo(oldcontext);
	}
	funcctx = SRF_PERCALL_SETUP();
	if (funcctx->call_cntr < funcctx->max_calls)
	{
		Datum		values[ncols];
		bool		nulls[ncols];
		HeapTuple	tuple;

		int cntr = funcctx->call_cntr;
		YBCServerDescriptor *server = (YBCServerDescriptor *)funcctx->user_fctx + cntr;
		bool is_primary = server->is_primary;
		const char *node_type = is_primary ? "primary" : "read_replica";

		// TODO: Remove hard coding of port and num_connections
		values[0] = CStringGetTextDatum(server->host);
		values[1] = Int64GetDatum(server->pg_port);
		values[2] = Int64GetDatum(0);
		values[3] = CStringGetTextDatum(node_type);
		values[4] = CStringGetTextDatum(server->cloud);
		values[5] = CStringGetTextDatum(server->region);
		values[6] = CStringGetTextDatum(server->zone);
		values[7] = CStringGetTextDatum(server->public_ip);
		if (ncols >= expected_ncols)
		{
			values[8] = CStringGetTextDatum(server->uuid);
		}
		memset(nulls, 0, sizeof(nulls));
		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);
		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
	}
	else
		SRF_RETURN_DONE(funcctx);
}

bool YBIsSupportedLibcLocale(const char *localebuf) {
	/*
	 * For libc mode, Yugabyte only supports the basic locales.
	 */
	if (strcmp(localebuf, "C") == 0 || strcmp(localebuf, "POSIX") == 0)
		return true;
	return strcasecmp(localebuf, "en_US.utf8") == 0 ||
		   strcasecmp(localebuf, "en_US.UTF-8") == 0;
}

static YBCStatus
YbGetTablePropertiesCommon(Relation rel)
{
	if (rel->yb_table_properties)
	{
		/* Already loaded, nothing to do */
		return NULL;
	}

	Oid dbid          = YBCGetDatabaseOid(rel);
	Oid relfileNodeId = YbGetRelfileNodeId(rel);

	YBCPgTableDesc desc = NULL;
	YBCStatus status = YBCPgGetTableDesc(dbid, relfileNodeId, &desc);
	if (status)
		return status;

	/* Relcache entry data must live in CacheMemoryContext */
	rel->yb_table_properties =
		MemoryContextAllocZero(CacheMemoryContext, sizeof(YbTablePropertiesData));

	return YBCPgGetTableProperties(desc, rel->yb_table_properties);
}

YbTableProperties
YbGetTableProperties(Relation rel)
{
	HandleYBStatus(YbGetTablePropertiesCommon(rel));
	return rel->yb_table_properties;
}

YbTableProperties
YbGetTablePropertiesById(Oid relid)
{
	Relation relation     = RelationIdGetRelation(relid);
	HandleYBStatus(YbGetTablePropertiesCommon(relation));
	RelationClose(relation);
	return relation->yb_table_properties;
}

YbTableProperties
YbTryGetTableProperties(Relation rel)
{
	bool not_found = false;
	HandleYBStatusIgnoreNotFound(YbGetTablePropertiesCommon(rel), &not_found);
	return not_found ? NULL : rel->yb_table_properties;
}

YbTableDistribution
YbGetTableDistribution(Oid relid)
{
	YbTableDistribution result;
	Relation relation = RelationIdGetRelation(relid);
	if (IsSystemRelation(relation))
		result = YB_SYSTEM;
	else
	{
		HandleYBStatus(YbGetTablePropertiesCommon(relation));
		if (relation->yb_table_properties->is_colocated)
			result = YB_COLOCATED;
		else if (relation->yb_table_properties->num_hash_key_columns > 0)
			result = YB_HASH_SHARDED;
		else
			result = YB_RANGE_SHARDED;
	}
	RelationClose(relation);
	return result;
}

Datum
yb_hash_code(PG_FUNCTION_ARGS)
{
	/* Create buffer for hashing */
	char *arg_buf;

	size_t size = 0;
	for (int i = 0; i < PG_NARGS(); i++)
	{
		Oid	argtype = get_fn_expr_argtype(fcinfo->flinfo, i);

		if (unlikely(argtype == UNKNOWNOID))
		{
			ereport(ERROR,
				(errcode(ERRCODE_INDETERMINATE_DATATYPE),
				errmsg("undefined datatype given to yb_hash_code")));
			PG_RETURN_NULL();
		}

		size_t typesize;
		const YBCPgTypeEntity *typeentity =
				 YbDataTypeFromOidMod(InvalidAttrNumber, argtype);
		YBCStatus status = YBCGetDocDBKeySize(PG_GETARG_DATUM(i), typeentity,
							PG_ARGISNULL(i), &typesize);
		if (unlikely(status))
		{
			YBCFreeStatus(status);
			ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				errmsg("Unsupported datatype given to yb_hash_code"),
				errdetail("Only types supported by HASH key columns are allowed"),
				errhint("Use explicit casts to ensure input types are as desired")));
			PG_RETURN_NULL();
		}
		size += typesize;
	}

	arg_buf = alloca(size);

	/* TODO(Tanuj): Look into caching the above buffer */

	char *arg_buf_pos = arg_buf;

	size_t total_bytes = 0;
	for (int i = 0; i < PG_NARGS(); i++)
	{
		Oid	argtype = get_fn_expr_argtype(fcinfo->flinfo, i);
		const YBCPgTypeEntity *typeentity =
				 YbDataTypeFromOidMod(InvalidAttrNumber, argtype);
		size_t written;
		YBCStatus status = YBCAppendDatumToKey(PG_GETARG_DATUM(i), typeentity,
							PG_ARGISNULL(i), arg_buf_pos, &written);
		if (unlikely(status))
		{
			YBCFreeStatus(status);
			ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				errmsg("Unsupported datatype given to yb_hash_code"),
				errdetail("Only types supported by HASH key columns are allowed"),
				errhint("Use explicit casts to ensure input types are as desired")));
			PG_RETURN_NULL();
		}
		arg_buf_pos += written;

		total_bytes += written;
	}

	/* hash the contents of the buffer and return */
	uint16_t hashed_val = YBCCompoundHash(arg_buf, total_bytes);
	PG_RETURN_UINT16(hashed_val);
}

/*
 * For backward compatibility, this function dynamically adapts to the number
 * of output columns defined in pg_proc.
 */
Datum
yb_table_properties(PG_FUNCTION_ARGS)
{
	Oid			relid = PG_GETARG_OID(0);
	TupleDesc	tupdesc;

	int expected_ncols = 5;

	static int ncols = 0;

	if (ncols < expected_ncols)
		ncols = YbGetNumberOfFunctionOutputColumns(8033 /* yb_table_properties function
												   oid hardcoded in pg_proc.dat */);

	Datum		values[ncols];
	bool		nulls[ncols];

	Relation	rel = relation_open(relid, AccessShareLock);
	Oid dbid		= YBCGetDatabaseOid(rel);
	Oid relfileNodeId = YbGetRelfileNodeId(rel);

	YBCPgTableDesc yb_tabledesc = NULL;
	YbTablePropertiesData yb_table_properties;
	bool not_found = false;
	HandleYBStatusIgnoreNotFound(
		YBCPgGetTableDesc(dbid, relfileNodeId, &yb_tabledesc), &not_found);
	if (!not_found)
		HandleYBStatusIgnoreNotFound(
			YBCPgGetTableProperties(yb_tabledesc, &yb_table_properties),
			&not_found);

	tupdesc = CreateTemplateTupleDesc(ncols, false);
	TupleDescInitEntry(tupdesc, (AttrNumber) 1,
					   "num_tablets", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 2,
					   "num_hash_key_columns", INT8OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 3,
					   "is_colocated", BOOLOID, -1, 0);
	if (ncols >= expected_ncols)
	{
		TupleDescInitEntry(tupdesc, (AttrNumber) 4,
						   "tablegroup_oid", OIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 5,
						   "colocation_id", OIDOID, -1, 0);
	}
	BlessTupleDesc(tupdesc);

	if (!not_found)
	{
		YbTableProperties yb_props = &yb_table_properties;
		values[0] = Int64GetDatum(yb_props->num_tablets);
		values[1] = Int64GetDatum(yb_props->num_hash_key_columns);
		values[2] = BoolGetDatum(yb_props->is_colocated);
		if (ncols >= expected_ncols)
		{
			values[3] =
				OidIsValid(yb_props->tablegroup_oid)
					? ObjectIdGetDatum(yb_props->tablegroup_oid)
					: (Datum) 0;
			values[4] =
				OidIsValid(yb_props->colocation_id)
					? ObjectIdGetDatum(yb_props->colocation_id)
					: (Datum) 0;
		}

		memset(nulls, 0, sizeof(nulls));
		if (ncols >= expected_ncols)
		{
			nulls[3] = !OidIsValid(yb_props->tablegroup_oid);
			nulls[4] = !OidIsValid(yb_props->colocation_id);
		}
	}
	else
	{
		/* Table does not exist in YB, set nulls for all columns. */
		memset(nulls, 1, sizeof(nulls));
	}

	relation_close(rel, AccessShareLock);

	return HeapTupleGetDatum(heap_form_tuple(tupdesc, values, nulls));
}

/*
 * This function is adapted from code of PQescapeLiteral() in fe-exec.c.
 * If use_quote_strategy_token is false, the string value will be converted
 * to an SQL string literal and appended to the given StringInfo.
 * If use_quote_strategy_token is true, the string value will be enclosed in
 * double quotes, backslashes will be escaped and the value will be appended
 * to the given StringInfo.
 */
static void
appendStringToString(StringInfo buf, const char *str, int encoding,
					 bool use_quote_strategy_token)
{
	const char *s;
	int			num_quotes = 0;
	int			num_backslashes = 0;
	int 		len = strlen(str);
	int			input_len;

	/* Scan the string for characters that must be escaped. */
	for (s = str; (s - str) < strlen(str) && *s != '\0'; ++s)
	{
		if ((*s == '\'' && !use_quote_strategy_token))
			++num_quotes;
		else if (*s == '\\')
			++num_backslashes;
		else if (IS_HIGHBIT_SET(*s))
		{
			int			charlen;

			/* Slow path for possible multibyte characters */
			charlen = pg_encoding_mblen(encoding, s);

			/* Multibyte character overruns allowable length. */
			if ((s - str) + charlen > len || memchr(s, 0, charlen) != NULL)
			{
				ereport(ERROR,
						(errcode(ERRCODE_DATA_EXCEPTION),
						 errmsg("incomplete multibyte character")));
			}

			/* Adjust s, bearing in mind that for loop will increment it. */
			s += charlen - 1;
		}
	}

	input_len = s - str;

	/*
	 * If we are escaping a literal that contains backslashes, we use the
	 * escape string syntax so that the result is correct under either value
	 * of standard_conforming_strings.
	 * Note: if we are using double quotes, the string should not have escape
	 * string syntax.
	 */
	if (num_backslashes > 0 && !use_quote_strategy_token)
	{
		appendStringInfoChar(buf, 'E');
	}

	/* Opening quote. */
	use_quote_strategy_token ? appendStringInfoChar(buf, '\"') :
		appendStringInfoChar(buf, '\'');

	/*
	 * Use fast path if possible.
	 *
	 * We've already verified that the input string is well-formed in the
	 * current encoding. If it contains no quotes and, in the case of
	 * literal-escaping, no backslashes, then we can just copy it directly to
	 * the output buffer, adding the necessary quotes.
	 *
	 * If not, we must rescan the input and process each character
	 * individually.
	 */
	if (num_quotes == 0 && num_backslashes == 0)
	{
		appendStringInfoString(buf, str);
	}
	else
	{
		for (s = str; s - str < input_len; ++s)
		{
			/*
			 * Note: if we are using double quotes, we do not need to escape
			 * single quotes.
			 */
			if ((*s == '\'' && !use_quote_strategy_token) || *s == '\\')
			{
				appendStringInfoChar(buf, *s);
				appendStringInfoChar(buf, *s);
			}
			else if (!IS_HIGHBIT_SET(*s))
				appendStringInfoChar(buf, *s);
			else
			{
				int	charlen = pg_encoding_mblen(encoding, s);

				while (1)
				{
					appendStringInfoChar(buf, *s);
					if (--charlen == 0)
						break;
					++s;		/* for loop will provide the final increment */
				}
			}
		}
	}

	/* Closing quote. */
	use_quote_strategy_token ? appendStringInfoChar(buf, '\"') :
		appendStringInfoChar(buf, '\'');
}

/*
 * This function is adapted from code in pg_dump.c.
 * It converts an internal raw datum value to a output string based on
 * column type, and append the string to the StringInfo input parameter.
 * Datum of all types can be generated in a quoted string format
 * (e.g., '100' for integer 100), and rely on PG's type cast to function
 * correctly. Here, we specifically handle some cases to ignore quotes to
 * make the generated string look better.
 */
static void
appendDatumToString(StringInfo str, uint64_t datum, Oid typid, int encoding,
					bool use_double_quotes)
{
	const char *datum_str = YBDatumToString(datum, typid);
	switch (typid)
	{
		case INT2OID:
		case INT4OID:
		case INT8OID:
		case OIDOID:
		case FLOAT4OID:
		case FLOAT8OID:
		case NUMERICOID:
			/*
			 * These types are converted to string without quotes unless
			 * they contain values: Infinity and NaN.
			 */
			if (strspn(datum_str, "0123456789 +-eE.") == strlen(datum_str))
				appendStringInfoString(str, datum_str);
			else
				use_double_quotes ?
					appendStringInfo(str, "\"%s\"", datum_str) :
					appendStringInfo(str, "'%s'", datum_str);
			break;
		/*
		 * Currently, cannot create tables/indexes with a key containing
		 * type 'BIT' or 'VARBIT'.
		 */
		case BITOID:
		case VARBITOID:
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("type: %s not yet supported",
							YBPgTypeOidToStr(typid))));
			break;
		default:
			/* All other types are appended as string literals. */
			appendStringToString(str, datum_str, encoding,
								 use_double_quotes);
			break;
	}
}

/*
 * This function gets range relations' split point values as PG datums.
 * It also stores key columns' data types in input parameters: pkeys_atttypid.
 */
static void
getSplitPointsInfo(Oid relid, YBCPgTableDesc yb_tabledesc,
				   YbTableProperties yb_table_properties,
				   Oid *pkeys_atttypid,
				   YBCPgSplitDatum *split_datums,
				   bool *has_null, bool *has_gin_null)
{
	Assert(yb_table_properties->num_tablets > 1);

	size_t num_range_key_columns = yb_table_properties->num_range_key_columns;
	const YBCPgTypeEntity *type_entities[num_range_key_columns];
	YBCPgTypeAttrs type_attrs_arr[num_range_key_columns];
	/*
	 * Get key columns' YBCPgTypeEntity and YBCPgTypeAttrs.
	 * For range-partitioned tables, use primary key to get key columns' type
	 * info. For range-partitioned indexes, get key columns' type info from
	 * indexes themselves.
	 */
	Relation rel = relation_open(relid, AccessShareLock);
	bool is_table = rel->rd_rel->relkind == RELKIND_RELATION;
	Relation index_rel = is_table
							? relation_open(RelationGetPrimaryKeyIndex(rel),
											AccessShareLock)
							: rel;
	Form_pg_index rd_index = index_rel->rd_index;
	TupleDesc tupledesc = rel->rd_att;

	for (int i = 0; i < rd_index->indnkeyatts; ++i)
	{
		Form_pg_attribute attr =
			TupleDescAttr(tupledesc, is_table ? rd_index->indkey.values[i] - 1
											  : i);
		type_entities[i] = YbDataTypeFromOidMod(InvalidAttrNumber,
												attr->atttypid);
		YBCPgTypeAttrs type_attrs;
		type_attrs.typmod = attr->atttypmod;
		type_attrs_arr[i] = type_attrs;
		pkeys_atttypid[i] = attr->atttypid;
	}
	if (is_table)
		relation_close(index_rel, AccessShareLock);
	relation_close(rel, AccessShareLock);

	/* Get Split point values as Postgres datums */
	HandleYBStatus(YBCGetSplitPoints(yb_tabledesc, type_entities,
									 type_attrs_arr, split_datums, has_null,
									 has_gin_null));
}

/*
 * This function constructs SPLIT AT VALUES clause for range-partitioned tables
 * with more than one tablet.
 */
static void
rangeSplitClause(Oid relid, YBCPgTableDesc yb_tabledesc,
				 YbTableProperties yb_table_properties, StringInfo str)
{
	Assert(!str->len);
	Assert(yb_table_properties->num_tablets > 1);
	size_t num_range_key_columns = yb_table_properties->num_range_key_columns;
	size_t num_splits = yb_table_properties->num_tablets - 1;
	Oid pkeys_atttypid[num_range_key_columns];
	YBCPgSplitDatum split_datums[num_splits * num_range_key_columns];
	StringInfo prev_split_point = makeStringInfo();
	StringInfo cur_split_point = makeStringInfo();
	bool has_null = false;
	bool has_gin_null = false;

	/* Get Split point values as Postgres datum */
	getSplitPointsInfo(relid, yb_tabledesc, yb_table_properties, pkeys_atttypid,
					   split_datums, &has_null, &has_gin_null);

	/*
	 * Check for existence of NULL in split points.
	 * We don't support specify NULL in SPLIT AT VALUES clause for both
	 * CREATE TABLE and CREATE INDEX.
	 * However, split points of indexes generated by tablet splitting can have
	 * NULLs in its split points.
	 */
	if (has_null)
	{
		ereport(WARNING,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("NULL value present in split points"),
				 errdetail("Specifying NULL value in SPLIT AT VALUES clause is "
						   "not supported.")));
		return;
	}

	/*
	 * Check for existence of GinNull in split points.
	 * We don't support (1) decoding GinNull into Postgres datum and
	 * (2) specify GinNull in SPLIT AT VALUES clause for both
	 * CREATE TABLE and CREATE INDEX.
	 * However, split points of GIN indexes generated by tablet splitting can have
	 * GinNull in its split points.
	 */
	if (has_gin_null)
	{
		ereport(WARNING,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("GinNull value present in split points"),
				 errdetail("Specifying GinNull value in SPLIT AT VALUES clause is "
						   "not supported.")));
		return;
	}

	/* Process Datum and use StringInfo to accumulate c-string data */
	appendStringInfoString(str, "SPLIT AT VALUES (");
	for (int split_idx = 0; split_idx < num_splits; ++split_idx)
	{
		if (split_idx)
		{
			appendStringInfoString(str, ", ");
		}
		appendStringInfoChar(cur_split_point, '(');
		for (int col_idx = 0; col_idx < num_range_key_columns; ++col_idx)
		{
			if (col_idx)
			{
				appendStringInfoString(cur_split_point, ", ");
			}
			int split_datum_idx = split_idx * num_range_key_columns + col_idx;
			if (split_datums[split_datum_idx].datum_kind ==
				YB_YQL_DATUM_LIMIT_MIN)
			{
				/* Min boundary */
				appendStringInfoString(cur_split_point, "MINVALUE");
			}
			else if (split_datums[split_datum_idx].datum_kind ==
					 YB_YQL_DATUM_LIMIT_MAX)
			{
				/* Max boundary */
				appendStringInfoString(cur_split_point, "MAXVALUE");
			}
			else
			{
				/* Actual datum value */
				appendDatumToString(cur_split_point,
									split_datums[split_datum_idx].datum,
									pkeys_atttypid[col_idx],
									pg_get_client_encoding(),
									false /* use_double_quotes */);
			}
		}
		appendStringInfoChar(cur_split_point, ')');

		/*
		 * Check for duplicate split points.
		 * Given current syntax of SPLIT AT VALUES doesn't allow specifying
		 * hidden column values for indexes, and tablet splitting can
		 * happen on hidden columns of indexes,
		 * duplicate split points (excluding the hidden column)
		 * can happen for indexes.
		 */
		if (strcmp(cur_split_point->data, prev_split_point->data) == 0)
		{
			ereport(WARNING,
					(errcode(ERRCODE_WARNING),
					 errmsg("duplicate split points in SPLIT AT VALUES clause "
							"of relation with oid %u", relid)));
			/* Empty string if duplicate split points exist. */
			resetStringInfo(str);
			return;
		}
		appendStringInfoString(str, cur_split_point->data);
		resetStringInfo(prev_split_point);
		appendStringInfoString(prev_split_point, cur_split_point->data);
		resetStringInfo(cur_split_point);
	}
	appendStringInfoChar(str, ')');
}

/*
 * This function is used to retrieve a range partitioned table's split points
 * as a list of PartitionRangeDatums.
 */
static void
getRangeSplitPointsList(Oid relid, YBCPgTableDesc yb_tabledesc,
						YbTableProperties yb_table_properties,
						List **split_points)
{
	Assert(yb_table_properties->num_tablets > 1);
	size_t num_range_key_columns = yb_table_properties->num_range_key_columns;
	size_t num_splits = yb_table_properties->num_tablets - 1;
	Oid pkeys_atttypid[num_range_key_columns];
	YBCPgSplitDatum split_datums[num_splits * num_range_key_columns];
	bool has_null;
	bool has_gin_null;

	/* Get Split point values as YBCPgSplitDatum. */
	getSplitPointsInfo(relid, yb_tabledesc, yb_table_properties,
					   pkeys_atttypid, split_datums, &has_null, &has_gin_null);

	/* Construct PartitionRangeDatums split points list. */
	for (int split_idx = 0; split_idx < num_splits; ++split_idx)
	{
		List *split_point = NIL;
		for (int col_idx = 0; col_idx < num_range_key_columns; ++col_idx)
		{
			PartitionRangeDatum *datum = makeNode(PartitionRangeDatum);
			int split_datum_idx = split_idx * num_range_key_columns + col_idx;
			switch (split_datums[split_datum_idx].datum_kind)
			{
				case YB_YQL_DATUM_LIMIT_MIN:
					datum->kind = PARTITION_RANGE_DATUM_MINVALUE;
					break;
				case YB_YQL_DATUM_LIMIT_MAX:
					datum->kind = PARTITION_RANGE_DATUM_MAXVALUE;
					break;
				default:
					datum->kind = PARTITION_RANGE_DATUM_VALUE;
					StringInfo str = makeStringInfo();
					appendDatumToString(str,
										split_datums[split_datum_idx].datum,
										pkeys_atttypid[col_idx],
										pg_get_client_encoding(),
										true /* use_double_quotes */);
					A_Const *value = makeNode(A_Const);
					value->val = *(Value *) nodeRead(str->data, str->len);
					datum->value = (Node *) value;
			}
			split_point = lappend(split_point, datum);
		}
		*split_points = lappend(*split_points, split_point);
	}
}

Datum
yb_get_range_split_clause(PG_FUNCTION_ARGS)
{
	Oid			relid = PG_GETARG_OID(0);
	bool		exists_in_yb = false;
	YBCPgTableDesc yb_tabledesc = NULL;
	YbTablePropertiesData yb_table_properties;
	StringInfoData str;
	char	   *range_split_clause = NULL;
	Relation	relation = RelationIdGetRelation(relid);
	Oid			relfileNodeId;

	if (relation)
	{
		relfileNodeId = YbGetRelfileNodeId(relation);
		RelationClose(relation);
		HandleYBStatus(YBCPgTableExists(MyDatabaseId, relfileNodeId,
			&exists_in_yb));
	}

	if (!exists_in_yb)
	{
		elog(NOTICE, "relation with oid %u is not backed by YB", relid);
		PG_RETURN_NULL();
	}

	HandleYBStatus(YBCPgGetTableDesc(MyDatabaseId, relfileNodeId,
		&yb_tabledesc));
	HandleYBStatus(YBCPgGetTableProperties(yb_tabledesc, &yb_table_properties));

	if (yb_table_properties.num_hash_key_columns > 0)
	{
		elog(NOTICE, "relation with oid %u is not range-partitioned", relid);
		PG_RETURN_NULL();
	}

	/*
	 * Get SPLIT AT VALUES clause for range relations with more than one tablet.
	 * Skip one-tablet range-partition relations such that this function
	 * return an empty string for them.
	 *
	 * For YB backup, if an error is thrown from a PG backend, ysql_dump will
	 * exit, generate an empty YSQLDUMP file, and block YB backup workflow.
	 * Currently, we don't have the functionality to adjust options used for 
	 * ysql_dump on YBA and YBM, so we don't have a way to to turn on/off
	 * a backup-related feature used in ysql_dump.
	 * There are known cases which caused decoding of split points to fail in
	 * the past and are handled specifically now.
	 * (1) null values appear in split points after tablet splitting
	 * (2) GinNull values appear in split points after tablet splitting
	 * (3) duplicate split points appear after tablet splitting on an
	 *     index's hidden column
	 * Thus, for the sake of YB backup, protect the split point decoding with
	 * TRY CATCH block in case decoding fails due to other unknown cases.
	 * Return an empty string if decoding fails.
	 */
	initStringInfo(&str);
	PG_TRY();
	{
		if (yb_table_properties.num_tablets > 1)
			rangeSplitClause(relid, yb_tabledesc, &yb_table_properties, &str);
	}
	PG_CATCH();
	{
		ereport(WARNING,
				(errcode(ERRCODE_WARNING),
				 errmsg("cannot decode split point in SPLIT AT VALUES clause "
						 "of relation with oid %u", relid),
				 errdetail("Returning an empty string instead.")));
		/* Empty string if split point decoding fails. */
		resetStringInfo(&str);
	}
	PG_END_TRY();
	range_split_clause = str.data;

	PG_RETURN_CSTRING(range_split_clause);
}

const char*
yb_fetch_current_transaction_priority(void)
{
	TxnPriorityRequirement txn_priority_type;
	double				   txn_priority;
	static char			   buf[50];

	txn_priority_type = YBCGetTransactionPriorityType();
	txn_priority	  = YBCGetTransactionPriority();

	if (txn_priority_type == kHighestPriority)
		snprintf(buf, sizeof(buf), "Highest priority transaction");
	else if (txn_priority_type == kHigherPriorityRange)
		snprintf(buf, sizeof(buf),
				 "%.9lf (High priority transaction)", txn_priority);
	else
		snprintf(buf, sizeof(buf),
				 "%.9lf (Normal priority transaction)", txn_priority);

	return buf;
}

Datum
yb_get_current_transaction_priority(PG_FUNCTION_ARGS)
{
	PG_RETURN_CSTRING(yb_fetch_current_transaction_priority());
}

Datum
yb_get_effective_transaction_isolation_level(PG_FUNCTION_ARGS)
{
	PG_RETURN_CSTRING(yb_fetch_effective_transaction_isolation_level());
}

Datum
yb_get_current_transaction(PG_FUNCTION_ARGS)
{
	pg_uuid_t *txn_id = NULL;
	bool is_null = false;

	if (!yb_enable_pg_locks)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						errmsg("get_current_transaction is unavailable"),
						errdetail("yb_enable_pg_locks is false or a system "
								  "upgrade is in progress")));
	}

	txn_id = (pg_uuid_t *) palloc(sizeof(pg_uuid_t));
	HandleYBStatus(
		YBCPgGetSelfActiveTransaction((YBCPgUuid *) txn_id, &is_null));

	if (is_null)
		PG_RETURN_NULL();

	PG_RETURN_UUID_P(txn_id);
}

Datum
yb_cancel_transaction(PG_FUNCTION_ARGS)
{
	if (!IsYbDbAdminUser(GetUserId()))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied to cancel transaction")));

	pg_uuid_t *id = PG_GETARG_UUID_P(0);
	YBCStatus status = YBCPgCancelTransaction(id->data);
	if (status)
	{
		ereport(NOTICE,
				(errmsg("failed to cancel transaction"),
				 errdetail("%s", YBCMessageAsCString(status))));
		PG_RETURN_BOOL(false);
	}
	PG_RETURN_BOOL(true);
}

/*
 * This PG function takes one optional bool input argument (legacy).
 * If the input argument is not specified or its value is false, this function
 * returns whether the current database is a colocated database.
 * If the value of the input argument is true, this function returns whether the
 * current database is a legacy colocated database.
 */
Datum
yb_is_database_colocated(PG_FUNCTION_ARGS)
{
	if (!PG_GETARG_BOOL(0))
		PG_RETURN_BOOL(MyDatabaseColocated);
	PG_RETURN_BOOL(MyDatabaseColocated && MyColocatedDatabaseLegacy);
}

/*
 * This function serves mostly as a helper for YSQL migration to introduce
 * pg_yb_catalog_version table without breaking version continuity.
 */
Datum
yb_catalog_version(PG_FUNCTION_ARGS)
{
	PG_RETURN_UINT64(YbGetMasterCatalogVersion());
}

Datum
yb_is_local_table(PG_FUNCTION_ARGS)
{
	Oid tableOid = PG_GETARG_OID(0);

	/* Fetch required info about the relation */
	Relation relation = relation_open(tableOid, NoLock);
	Oid tablespaceId = relation->rd_rel->reltablespace;
	bool isTempTable =
		(relation->rd_rel->relpersistence == RELPERSISTENCE_TEMP);
	RelationClose(relation);

	/* Temp tables are local. */
	if (isTempTable)
	{
			PG_RETURN_BOOL(true);
	}
	GeolocationDistance distance = get_tablespace_distance(tablespaceId);
	PG_RETURN_BOOL(distance == REGION_LOCAL || distance == ZONE_LOCAL);
}

Datum
yb_server_region(PG_FUNCTION_ARGS) {
	const char *current_region = YBGetCurrentRegion();

	if (current_region == NULL)
		PG_RETURN_NULL();

	return CStringGetTextDatum(current_region);
}

Datum
yb_server_cloud(PG_FUNCTION_ARGS)
{
	const char *current_cloud = YBGetCurrentCloud();

	if (current_cloud == NULL)
		PG_RETURN_NULL();

	return CStringGetTextDatum(current_cloud);
}

Datum
yb_server_zone(PG_FUNCTION_ARGS)
{
	const char *current_zone = YBGetCurrentZone();

	if (current_zone == NULL)
		PG_RETURN_NULL();

	return CStringGetTextDatum(current_zone);
}

static bytea *
bytesToBytea(const char *in, int len)
{
	bytea	   *out;

	out = (bytea *) palloc(len + VARHDRSZ);
	SET_VARSIZE(out, len + VARHDRSZ);
	memcpy(VARDATA(out), in, len);

	return out;
}

Datum
yb_local_tablets(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	int			i;
#define YB_TABLET_INFO_COLS 8

	/* only superuser and yb_db_admin can query this function */
	if (!superuser() && !IsYbDbAdminUser(GetUserId()))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 (errmsg("only superusers and yb_db_admin can query yb_local_tablets"))));

	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));

	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not " \
						"allowed in this context")));

	/*
	 * Switch context to construct returned data structures and store
	 * returned values from tserver.
	 */
	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	/* Build a tuple descriptor */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		ereport(ERROR,
				(errmsg_internal("return type must be a row type")));

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	YBCPgTabletsDescriptor	*tablets = NULL;
	size_t		num_tablets = 0;
	HandleYBStatus(YBCLocalTablets(&tablets, &num_tablets));

	for (i = 0; i < num_tablets; ++i)
	{
		YBCPgTabletsDescriptor *tablet = (YBCPgTabletsDescriptor *)tablets + i;
		Datum		values[YB_TABLET_INFO_COLS];
		bool		nulls[YB_TABLET_INFO_COLS];
		bytea	   *partition_key_start;
		bytea	   *partition_key_end;

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		values[0] = CStringGetTextDatum(tablet->tablet_id);
		values[1] = CStringGetTextDatum(tablet->table_id);
		values[2] = CStringGetTextDatum(tablet->table_type);
		values[3] = CStringGetTextDatum(tablet->namespace_name);
		values[4] = CStringGetTextDatum(tablet->pgschema_name);
		values[5] = CStringGetTextDatum(tablet->table_name);

		if (tablet->partition_key_start_len)
		{
			partition_key_start = bytesToBytea(tablet->partition_key_start,
											   tablet->partition_key_start_len);
			values[6] = PointerGetDatum(partition_key_start);
		}
		else
			nulls[6] = true;

		if (tablet->partition_key_end_len)
		{
			partition_key_end = bytesToBytea(tablet->partition_key_end,
											 tablet->partition_key_end_len);
			values[7] = PointerGetDatum(partition_key_end);
		}
		else
			nulls[7] = true;

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

#undef YB_TABLET_INFO_COLS

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	MemoryContextSwitchTo(oldcontext);

	return (Datum) 0;
}

/*---------------------------------------------------------------------------*/
/* Deterministic DETAIL order                                                */
/*---------------------------------------------------------------------------*/

static int yb_detail_sort_comparator(const void *a, const void *b)
{
	return strcmp(*(const char **) a, *(const char **) b);
}

typedef struct {
	char **lines;
	int length;
} DetailSorter;

void detail_sorter_from_list(DetailSorter *v, List *litems, int capacity)
{
	v->lines = (char **)palloc(sizeof(char *) * capacity);
	v->length = 0;
	ListCell *lc;
	foreach (lc, litems)
	{
		if (v->length < capacity)
		{
			v->lines[v->length++] = (char *)lfirst(lc);
		}
	}
}

char **detail_sorter_lines_sorted(DetailSorter *v)
{
	qsort(v->lines, v->length,
		sizeof (const char *), yb_detail_sort_comparator);
	return v->lines;
}

void detail_sorter_free(DetailSorter *v)
{
	pfree(v->lines);
}

char *YBDetailSorted(char *input)
{
	if (input == NULL)
		return input;

	// this delimiter is hard coded in backend/catalog/pg_shdepend.c,
	// inside of the storeObjectDescription function:
	char delimiter[2] = "\n";

	// init stringinfo used for concatenation of the output:
	StringInfoData s;
	initStringInfo(&s);

	// this list stores the non-empty tokens, extra counter to know how many:
	List *line_store = NIL;
	int line_count = 0;

	char *token;
	token = strtok(input, delimiter);
	while (token != NULL)
	{
		if (strcmp(token, "") != 0)
		{
			line_store = lappend(line_store, token);
			line_count++;
		}
		token = strtok(NULL, delimiter);
	}

	DetailSorter sorter;
	detail_sorter_from_list(&sorter, line_store, line_count);

	if (line_count == 0)
	{
		// put the original input in:
		appendStringInfoString(&s, input);
	}
	else
	{
		char **sortedLines = detail_sorter_lines_sorted(&sorter);
		for (int i=0; i<line_count; i++)
		{
			if (sortedLines[i] != NULL) {
				if (i > 0)
					appendStringInfoString(&s, delimiter);
				appendStringInfoString(&s, sortedLines[i]);
			}
		}
	}

	detail_sorter_free(&sorter);
	list_free(line_store);

	return s.data;
}

/*
 * This function is adapted from code in varlena.c.
 */
static const char*
YBComputeNonCSortKey(Oid collation_id, const char* value, int64_t bytes) {
	/*
	 * We expect collation_id is a valid non-C collation.
	 */
	pg_locale_t locale = 0;
	if (collation_id != DEFAULT_COLLATION_OID)
	{
		locale = pg_newlocale_from_collation(collation_id);
		Assert(locale);
	}
	static const int kTextBufLen = 1024;
	Size bsize = -1;
	bool is_icu_provider = false;
	const int buflen1 = bytes;
	char* buf1 = palloc(buflen1 + 1);
	char* buf2 = palloc(kTextBufLen);
	int buflen2 = kTextBufLen;
	memcpy(buf1, value, bytes);
	buf1[buflen1] = '\0';

#ifdef USE_ICU
	int32_t		ulen = -1;
	UChar		*uchar = NULL;
#endif

#ifdef USE_ICU
	/* When using ICU, convert string to UChar. */
	if (locale && locale->provider == COLLPROVIDER_ICU)
	{
		is_icu_provider = true;
		ulen = icu_to_uchar(&uchar, buf1, buflen1);
	}
#endif

	/*
	 * Loop: Call strxfrm() or ucol_getSortKey(), possibly enlarge buffer,
	 * and try again. Both of these functions have the result buffer
	 * content undefined if the result did not fit, so we need to retry
	 * until everything fits.
	 */
	for (;;)
	{
#ifdef USE_ICU
		if (locale && locale->provider == COLLPROVIDER_ICU)
		{
			bsize = ucol_getSortKey(locale->info.icu.ucol,
									uchar, ulen,
									(uint8_t *) buf2, buflen2);
		}
		else
#endif
#ifdef HAVE_LOCALE_T
		if (locale && locale->provider == COLLPROVIDER_LIBC)
			bsize = strxfrm_l(buf2, buf1, buflen2, locale->info.lt);
		else
#endif
			bsize = strxfrm(buf2, buf1, buflen2);

		if (bsize < buflen2)
			break;

		/*
		 * Grow buffer and retry.
		 */
		pfree(buf2);
		buflen2 = Max(bsize + 1, Min(buflen2 * 2, MaxAllocSize));
		buf2 = palloc(buflen2);
	}

#ifdef USE_ICU
	if (uchar)
		pfree(uchar);
#endif

	pfree(buf1);
	if (is_icu_provider)
	{
		Assert(bsize > 0);
		/*
		 * Each sort key ends with one \0 byte and does not contain any
		 * other \0 byte. The terminating \0 byte is included in bsize.
		 */
		Assert(buf2[bsize - 1] == '\0');
	}
	else
	{
		Assert(bsize >= 0);
		/*
		 * Both strxfrm and strxfrm_l return the length of the transformed
		 * string not including the terminating \0 byte.
		 */
		Assert(buf2[bsize] == '\0');
	}
	return buf2;
}

void YBGetCollationInfo(
	Oid collation_id,
	const YBCPgTypeEntity *type_entity,
	Datum datum,
	bool is_null,
	YBCPgCollationInfo *collation_info) {

	if (!type_entity) {
		Assert(collation_id == InvalidOid);
		collation_info->collate_is_valid_non_c = false;
		collation_info->sortkey = NULL;
		return;
	}

	if (type_entity->yb_type != YB_YQL_DATA_TYPE_STRING) {
		/*
		 * A character array type is processed as YB_YQL_DATA_TYPE_BINARY but it
		 * can have a collation. For example:
		 *   CREATE TABLE t (id text[] COLLATE "en_US.utf8");
		 *
		 * GIN indexes have null categories, so ybgin indexes pass the category
		 * number down using GIN_NULL type. Even if the column is collatable,
		 * nulls should be unaffected by collation.
		 *
		 * pg_trgm GIN indexes have key type int32 but also valid collation for
		 * regex purposes on the indexed type text. Add an exception here for
		 * int32. Since this relaxes the assert for other situations involving
		 * int32, a proper fix should be done in the future.
		 */
		Assert(collation_id == InvalidOid ||
			   type_entity->yb_type == YB_YQL_DATA_TYPE_BINARY ||
			   type_entity->yb_type == YB_YQL_DATA_TYPE_GIN_NULL ||
			   type_entity->yb_type == YB_YQL_DATA_TYPE_INT32);
		collation_info->collate_is_valid_non_c = false;
		collation_info->sortkey = NULL;
		return;
	}
	switch (type_entity->type_oid) {
		case NAMEOID:
			/*
			 * In bootstrap code, postgres 11.2 hard coded to InvalidOid but
			 * postgres 13.2 hard coded to C_COLLATION_OID. Adjust the assertion
			 * when we upgrade to postgres 13.2.
			 */
			Assert(collation_id == InvalidOid);
			collation_id = C_COLLATION_OID;
			break;
		case TEXTOID:
		case BPCHAROID:
		case VARCHAROID:
			if (collation_id == InvalidOid) {
				/*
				 * In postgres, an index can include columns. Included columns
				 * have no collation. Included character column value will be
				 * stored as C collation. It can only be stored and retrieved
				 * as a value in DocDB. Any comparison must be done by the
				 * postgres layer.
				 */
				collation_id = C_COLLATION_OID;
			}
			break;
		case CSTRINGOID:
			Assert(collation_id == C_COLLATION_OID);
			break;
		default:
			/* Not supported text type. */
			Assert(false);
	}
	collation_info->collate_is_valid_non_c = YBIsCollationValidNonC(collation_id);
	if (!is_null && collation_info->collate_is_valid_non_c) {
		char *value;
		int64_t bytes = type_entity->datum_fixed_size;
		type_entity->datum_to_yb(datum, &value, &bytes);
		/*
		 * Collation sort keys are compared using strcmp so they are null
		 * terminated and cannot have embedded \0 byte.
		 */
		collation_info->sortkey = YBComputeNonCSortKey(collation_id, value, bytes);
	} else {
		collation_info->sortkey = NULL;
	}
}

static bool YBNeedCollationEncoding(const YBCPgColumnInfo *column_info) {
	/* We only need collation encoding for range keys. */
	return (column_info->is_primary && !column_info->is_hash);
}

void YBSetupAttrCollationInfo(YBCPgAttrValueDescriptor *attr, const YBCPgColumnInfo *column_info) {
	if (attr->collation_id != InvalidOid && !YBNeedCollationEncoding(column_info)) {
		attr->collation_id = InvalidOid;
	}
	YBGetCollationInfo(attr->collation_id, attr->type_entity, attr->datum,
					   attr->is_null, &attr->collation_info);
}

bool YBIsCollationValidNonC(Oid collation_id) {
	/*
	 * For now we only allow database to have C collation. Therefore for
	 * DEFAULT_COLLATION_OID it cannot be a valid non-C collation. This
	 * special case for DEFAULT_COLLATION_OID is made here because YB
	 * PgExpr code is called before Postgres has properly setup the default
	 * collation to that of the database connected. So lc_collate_is_c can
	 * return false for DEFAULT_COLLATION_OID which isn't correct.
	 * We stop support non-C collation if collation support is disabled.
	 */
	bool is_valid_non_c = YBIsCollationEnabled() &&
						  OidIsValid(collation_id) &&
						  collation_id != DEFAULT_COLLATION_OID &&
						  !lc_collate_is_c(collation_id);
	/*
	 * For testing only, we use en_US.UTF-8 for default collation and
	 * this is a valid non-C collation.
	 */
	Assert(!kTestOnlyUseOSDefaultCollation || YBIsCollationEnabled());
	if (kTestOnlyUseOSDefaultCollation && collation_id == DEFAULT_COLLATION_OID)
		is_valid_non_c = true;
	return is_valid_non_c;
}

Oid YBEncodingCollation(YBCPgStatement handle, int attr_num, Oid attcollation) {
	if (attcollation == InvalidOid)
		return InvalidOid;
	YBCPgColumnInfo column_info = {0};
	HandleYBStatus(YBCPgDmlGetColumnInfo(handle, attr_num, &column_info));
	return YBNeedCollationEncoding(&column_info) ? attcollation : InvalidOid;
}

bool IsYbExtensionUser(Oid member) {
	return IsYugaByteEnabled() && has_privs_of_role(member, DEFAULT_ROLE_YB_EXTENSION);
}

bool IsYbFdwUser(Oid member) {
	return IsYugaByteEnabled() && has_privs_of_role(member, DEFAULT_ROLE_YB_FDW);
}

void YBSetParentDeathSignal()
{
#ifdef HAVE_SYS_PRCTL_H
	char* pdeathsig_str = getenv("YB_PG_PDEATHSIG");
	if (pdeathsig_str)
	{
		char* end_ptr = NULL;
		long int pdeathsig = strtol(pdeathsig_str, &end_ptr, 10);
		if (end_ptr == pdeathsig_str + strlen(pdeathsig_str)) {
			if (pdeathsig >= 1 && pdeathsig <= 31) {
				// TODO: prctl(PR_SET_PDEATHSIG) is Linux-specific, look into portable ways to
				// prevent orphans when parent is killed.
				prctl(PR_SET_PDEATHSIG, pdeathsig);
			}
			else
			{
				fprintf(
					stderr,
					"Error: YB_PG_PDEATHSIG is an invalid signal value: %ld",
					pdeathsig);
			}

		}
		else
		{
			fprintf(
				stderr,
				"Error: failed to parse the value of YB_PG_PDEATHSIG: %s",
				pdeathsig_str);
		}
	}
#endif
}

Oid YbGetRelfileNodeId(Relation relation) {
	if (relation->rd_rel->relfilenode != InvalidOid) {
		return relation->rd_rel->relfilenode;
	}
	return RelationGetRelid(relation);
}

Oid YbGetRelfileNodeIdFromRelId(Oid relationId) {
	Relation rel = RelationIdGetRelation(relationId);
	Oid relfileNodeId = YbGetRelfileNodeId(rel);
	RelationClose(rel);
	return relfileNodeId;
}

bool IsYbDbAdminUser(Oid member) {
	return IsYugaByteEnabled() && has_privs_of_role(member, DEFAULT_ROLE_YB_DB_ADMIN);
}

bool IsYbDbAdminUserNosuper(Oid member) {
	return IsYugaByteEnabled() && is_member_of_role_nosuper(member, DEFAULT_ROLE_YB_DB_ADMIN);
}

void YbCheckUnsupportedSystemColumns(Var *var, const char *colname, RangeTblEntry *rte) {
	if (rte->relkind == RELKIND_FOREIGN_TABLE)
		return;
	switch (var->varattno)
	{
		case SelfItemPointerAttributeNumber:
		case MinTransactionIdAttributeNumber:
		case MinCommandIdAttributeNumber:
		case MaxTransactionIdAttributeNumber:
		case MaxCommandIdAttributeNumber:
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					errmsg("System column \"%s\" is not supported yet", colname)));
		default:
			break;
	}
}

void YbRegisterSysTableForPrefetching(int sys_table_id) {
	// sys_only_filter_attr stores attr which will be used to filter table rows
	// related to system cache entries.
	// In case particular table must always load all the rows or
	// system cache filtering is disabled the sys_only_filter_attr
	// must be set to InvalidAttrNumber.
	int sys_only_filter_attr = ObjectIdAttributeNumber;
	int db_id = MyDatabaseId;
	int sys_table_index_id = InvalidOid;

	switch(sys_table_id)
	{
		// TemplateDb tables
		case AuthMemRelationId:                           // pg_auth_members
			db_id = TemplateDbOid;
			sys_table_index_id = AuthMemMemRoleIndexId;
			sys_only_filter_attr = InvalidAttrNumber;
			break;
		case AuthIdRelationId:                            // pg_authid
			db_id = TemplateDbOid;
			sys_table_index_id = AuthIdRolnameIndexId;
			sys_only_filter_attr = InvalidAttrNumber;
			break;
		case DatabaseRelationId:                          // pg_database
			db_id = TemplateDbOid;
			sys_table_index_id = DatabaseNameIndexId;
			sys_only_filter_attr = InvalidAttrNumber;
			break;

		case DbRoleSettingRelationId:    switch_fallthrough(); // pg_db_role_setting
		case TableSpaceRelationId:       switch_fallthrough(); // pg_tablespace
		case YBCatalogVersionRelationId: switch_fallthrough(); // pg_yb_catalog_version
		case YbProfileRelationId:        switch_fallthrough(); // pg_yb_profile
		case YbRoleProfileRelationId:                          // pg_yb_role_profile
			db_id = TemplateDbOid;
			sys_only_filter_attr = InvalidAttrNumber;
			break;

		// MyDb tables
		case AccessMethodProcedureRelationId:             // pg_amproc
			sys_table_index_id = AccessMethodProcedureIndexId;
			break;
		case AccessMethodRelationId:                      // pg_am
			sys_table_index_id = AmNameIndexId;
			break;
		case AttrDefaultRelationId:                       // pg_attrdef
			sys_table_index_id = AttrDefaultIndexId;
			break;
		case AttributeRelationId:                         // pg_attribute
			sys_table_index_id = AttributeRelidNameIndexId;
			sys_only_filter_attr = Anum_pg_attribute_attrelid;
			break;
		case CastRelationId:                              // pg_cast
			sys_table_index_id = CastSourceTargetIndexId;
			break;
		case ConstraintRelationId:                        // pg_constraint
			sys_table_index_id = ConstraintRelidTypidNameIndexId;
			break;
		case IndexRelationId:                             // pg_index
			sys_table_index_id = IndexIndrelidIndexId;
			sys_only_filter_attr = Anum_pg_index_indexrelid;
			break;
		case InheritsRelationId:                          // pg_inherits
			sys_table_index_id = InheritsParentIndexId;
			sys_only_filter_attr = Anum_pg_inherits_inhrelid;
			break;
		case NamespaceRelationId:                         // pg_namespace
			sys_table_index_id = NamespaceNameIndexId;
			break;
		case OperatorClassRelationId:                     // pg_opclass
			sys_table_index_id = OpclassAmNameNspIndexId;
			break;
		case OperatorRelationId:                          // pg_operator
			sys_table_index_id = OperatorNameNspIndexId;
			break;
		case PolicyRelationId:                            // pg_policy
			sys_table_index_id = PolicyPolrelidPolnameIndexId;
			break;
		case ProcedureRelationId:                         // pg_proc
			sys_table_index_id = ProcedureNameArgsNspIndexId;
			break;
		case RelationRelationId:                          // pg_class
			sys_table_index_id = ClassNameNspIndexId;
			break;
		case RangeRelationId:                             // pg_range
			sys_only_filter_attr = Anum_pg_range_rngtypid;
			break;
		case RewriteRelationId:                           // pg_rewrite
			sys_table_index_id = RewriteRelRulenameIndexId;
			break;
		case StatisticRelationId:                         // pg_statistic
			sys_only_filter_attr = Anum_pg_statistic_starelid;
			break;
		case TriggerRelationId:                           // pg_trigger
			sys_table_index_id = TriggerRelidNameIndexId;
			break;
		case TypeRelationId:                              // pg_type
			sys_table_index_id = TypeNameNspIndexId;
			break;
		case AccessMethodOperatorRelationId:              // pg_amop
			sys_table_index_id = AccessMethodOperatorIndexId;
			break;
		case PartitionedRelationId:                       // pg_partitioned_table
			sys_only_filter_attr = Anum_pg_partitioned_table_partrelid;
			break;

		default:
		{
			ereport(FATAL,
					(errcode(ERRCODE_INTERNAL_ERROR),
					 errmsg("Sys table '%d' is not yet intended for preloading", sys_table_id)));

		}
	}

	if (!*YBCGetGFlags()->ysql_minimal_catalog_caches_preload)
		sys_only_filter_attr = InvalidAttrNumber;

	YBCRegisterSysTableForPrefetching(
		db_id, sys_table_id, sys_table_index_id, sys_only_filter_attr);
}

void YbTryRegisterCatalogVersionTableForPrefetching()
{
	if (YbGetCatalogVersionType() == CATALOG_VERSION_CATALOG_TABLE)
		YbRegisterSysTableForPrefetching(YBCatalogVersionRelationId);
}

bool YBCIsRegionLocal(Relation rel) {
	double cost = 0.0;
	return IsNormalProcessingMode() &&
			!IsSystemRelation(rel) &&
			get_yb_tablespace_cost(rel->rd_rel->reltablespace, &cost) &&
			cost <= yb_interzone_cost;
}

bool check_yb_xcluster_consistency_level(char** newval, void** extra, GucSource source) {
	int newConsistency = XCLUSTER_CONSISTENCY_TABLET;
	if (strcmp(*newval, "tablet") == 0)
		newConsistency = XCLUSTER_CONSISTENCY_TABLET;
	else if (strcmp(*newval, "database") == 0)
		newConsistency = XCLUSTER_CONSISTENCY_DATABASE;
	else
		return false;

	*extra = malloc(sizeof(int));
	if (!*extra)
		return false;
	*((int*)*extra) = newConsistency;

	return true;
}

void assign_yb_xcluster_consistency_level(const char* newval, void* extra) {
	yb_xcluster_consistency_level = *((int*)extra);
}

bool
parse_yb_read_time(const char *value, unsigned long long *result, bool* is_ht_unit)
{
	unsigned long long	val;
	char	           *endptr;

	if (is_ht_unit)
	{
		*is_ht_unit = false;
	}

	/* To suppress compiler warnings, always set output params */
	if (result)
		*result = 0;

	errno = 0;
	val = strtoull(value, &endptr, 0);

	if (endptr == value || errno == ERANGE)
		return false;

	/* allow whitespace between integer and unit */
	while (isspace((unsigned char) *endptr))
		endptr++;

	/* Handle possible unit */
	if (*endptr != '\0')
	{
		char		unit[2 + 1];
		int			unitlen;
		bool		converted = false;

		unitlen = 0;
		while (*endptr != '\0' && !isspace((unsigned char) *endptr) &&
			   unitlen < 2)
			unit[unitlen++] = *(endptr++);
		unit[unitlen] = '\0';
		/* allow whitespace after unit */
		while (isspace((unsigned char) *endptr))
			endptr++;

		if (*endptr == '\0')
			converted = (strcmp(unit, "ht") == 0);
		if (!converted)
			return false;
		else if (is_ht_unit)
			*is_ht_unit = true;
	}

	if (result)
		*result = val;
	return true;
}

bool
check_yb_read_time(char **newval, void **extra, GucSource source)
{
	/* Read time should be convertable to unsigned long long */
	unsigned long long read_time_ull;
	unsigned long long value_ull;
	bool is_ht_unit;
	if(!parse_yb_read_time(*newval, &value_ull, &is_ht_unit))
	{
		return false;
	}

	if (is_ht_unit)
	{
		/*
		 * Right shift by 12 bits to get physical time in micros from HybridTime
		 * See src/yb/common/hybrid_time.h (GetPhysicalValueMicros)
		 */
		read_time_ull = value_ull >> 12;
	}
	else
	{
		read_time_ull = value_ull;
		char read_time_string[23];
		sprintf(read_time_string, "%llu", read_time_ull);
		if (strcmp(*newval, read_time_string))
		{
			GUC_check_errdetail("Accepted value is Unix timestamp in microseconds."
								" i.e. 1694673026673528");
			return false;
		}
	}

	/* Read time should not be set to a timestamp in the future */
	struct timeval now_tv;
	gettimeofday(&now_tv, NULL);
	unsigned long long now_micro_sec = ((unsigned long long)now_tv.tv_sec * USECS_PER_SEC) + now_tv.tv_usec;
	if(read_time_ull > now_micro_sec)
	{
		GUC_check_errdetail("Provided timestamp is in the future.");
		return false;
	}
	return true;
}

void
assign_yb_read_time(const char* newval, void *extra)
{
	unsigned long long value_ull;
	bool is_ht_unit;
	parse_yb_read_time(newval, &value_ull, &is_ht_unit);
	yb_read_time = value_ull;
	yb_is_read_time_ht = is_ht_unit;
	if (!am_walsender)
	{
		ereport(NOTICE,
				(errmsg("yb_read_time should be set with caution."),
				errdetail("No DDL operations should be performed while it is set and "
						"it should not be set to a timestamp before a DDL "
						"operation has been performed. It doesn't have well defined semantics"
						" for normal transactions and is only to be used after consultation")));
	}
}

void
yb_assign_max_replication_slots(int newval, void *extra)
{
	ereport(NOTICE,
		(errmsg("max_replication_slots should be controlled using the Gflag"
				" \"max_replication_slots\" on the YB-Master process"),
		 errdetail("In Yugabyte clusters, the replication slots are managed by"
		 		   " the YB-Master globally. Hence limits on the number of"
				   " replication slots should be controlled using Gflags and"
				   " not session-level GUC variables.")));
}

void YBCheckServerAccessIsAllowed() {
	if (*YBCGetGFlags()->ysql_disable_server_file_access)
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("server file access disabled"),
				 errdetail("tserver flag ysql_disable_server_file_access is "
						   "set to true")));
}

static void
aggregateStats(YbInstrumentation *instr, const YBCPgExecStats *exec_stats)
{
	/* User Table stats */
	instr->tbl_reads.count += exec_stats->tables.reads;
	instr->tbl_reads.wait_time += exec_stats->tables.read_wait;
	instr->tbl_writes += exec_stats->tables.writes;
	instr->tbl_reads.rows_scanned += exec_stats->tables.rows_scanned;

	/* Secondary Index stats */
	instr->index_reads.count += exec_stats->indices.reads;
	instr->index_reads.wait_time += exec_stats->indices.read_wait;
	instr->index_writes += exec_stats->indices.writes;
	instr->index_reads.rows_scanned += exec_stats->indices.rows_scanned;

	/* System Catalog stats */
	instr->catalog_reads.count += exec_stats->catalog.reads;
	instr->catalog_reads.wait_time += exec_stats->catalog.read_wait;
	instr->catalog_writes += exec_stats->catalog.writes;
	instr->catalog_reads.rows_scanned += exec_stats->catalog.rows_scanned;

	/* Flush stats */
	instr->write_flushes.count += exec_stats->num_flushes;
	instr->write_flushes.wait_time += exec_stats->flush_wait;

	if (exec_stats->storage_metrics_version != instr->storage_metrics_version) {
		instr->storage_metrics_version = exec_stats->storage_metrics_version;
		for (int i = 0; i < YB_STORAGE_GAUGE_COUNT; ++i) {
			instr->storage_gauge_metrics[i] += exec_stats->storage_gauge_metrics[i];
		}
		for (int i = 0; i < YB_STORAGE_COUNTER_COUNT; ++i) {
			instr->storage_counter_metrics[i] += exec_stats->storage_counter_metrics[i];
		}
		for (int i = 0; i < YB_STORAGE_EVENT_COUNT; ++i) {
			YbPgEventMetric* agg = &instr->storage_event_metrics[i];
			const YBCPgExecEventMetric* val = &exec_stats->storage_event_metrics[i];
			agg->sum += val->sum;
			agg->count += val->count;
		}
	}
}

static YBCPgExecReadWriteStats
getDiffReadWriteStats(const YBCPgExecReadWriteStats *current,
					  const YBCPgExecReadWriteStats *old)
{
	return (YBCPgExecReadWriteStats)
	{
		current->reads - old->reads,
		current->writes - old->writes,
		current->read_wait - old->read_wait,
		current->rows_scanned - old->rows_scanned
	};
}

static void
calculateExecStatsDiff(const YbSessionStats *stats, YBCPgExecStats *result)
{
	const YBCPgExecStats *current = &stats->current_state.stats;
	const YBCPgExecStats *old = &stats->latest_snapshot;

	result->tables = getDiffReadWriteStats(&current->tables, &old->tables);
	result->indices = getDiffReadWriteStats(&current->indices, &old->indices);
	result->catalog = getDiffReadWriteStats(&current->catalog, &old->catalog);

	result->num_flushes = current->num_flushes - old->num_flushes;
	result->flush_wait = current->flush_wait - old->flush_wait;

	result->storage_metrics_version = current->storage_metrics_version;
	if (old->storage_metrics_version != current->storage_metrics_version) {
		for (int i = 0; i < YB_STORAGE_GAUGE_COUNT; ++i) {
			result->storage_gauge_metrics[i] =
					current->storage_gauge_metrics[i] - old->storage_gauge_metrics[i];
		}
		for (int i = 0; i < YB_STORAGE_COUNTER_COUNT; ++i) {
			result->storage_counter_metrics[i] =
					current->storage_counter_metrics[i] - old->storage_counter_metrics[i];
		}
		for (int i = 0; i < YB_STORAGE_EVENT_COUNT; ++i) {
			YBCPgExecEventMetric* result_metric = &result->storage_event_metrics[i];
			const YBCPgExecEventMetric* current_metric = &current->storage_event_metrics[i];
			const YBCPgExecEventMetric* old_metric = &old->storage_event_metrics[i];
			result_metric->sum = current_metric->sum - old_metric->sum;
			result_metric->count = current_metric->count - old_metric->count;
		}
	}
}

static void
refreshExecStats(YbSessionStats *stats, bool include_catalog_stats)
{
	const YBCPgExecStats *current = &stats->current_state.stats;
	YBCPgExecStats		 *old = &stats->latest_snapshot;

	old->tables = current->tables;
	old->indices = current->indices;

	old->num_flushes = current->num_flushes;
	old->flush_wait = current->flush_wait;

	if (include_catalog_stats)
		old->catalog = current->catalog;

	if (yb_session_stats.current_state.metrics_capture) {
		old->storage_metrics_version = current->storage_metrics_version;
		for (int i = 0; i < YB_STORAGE_GAUGE_COUNT; ++i) {
			old->storage_gauge_metrics[i] = current->storage_gauge_metrics[i];
		}
		for (int i = 0; i < YB_STORAGE_COUNTER_COUNT; ++i) {
			old->storage_counter_metrics[i] = current->storage_counter_metrics[i];
		}
		for (int i = 0; i < YB_STORAGE_EVENT_COUNT; ++i) {
			YBCPgExecEventMetric* old_metric = &old->storage_event_metrics[i];
			const YBCPgExecEventMetric* current_metric =
					&current->storage_event_metrics[i];
			old_metric->sum = current_metric->sum;
			old_metric->count = current_metric->count;
		}
	}
}

void
YbUpdateSessionStats(YbInstrumentation *yb_instr)
{
	YBCPgExecStats exec_stats = {0};

	/* Find the diff between the current stats and the last stats snapshot */
	calculateExecStatsDiff(&yb_session_stats, &exec_stats);

	/* Refresh the snapshot to reflect the current state of query execution.
	 * This function is always invoked during the query execution phase. */
	YbRefreshSessionStatsDuringExecution();

	/* Update the supplied instrumentation handle with the delta calculated
	 * above. */
	aggregateStats(yb_instr, &exec_stats);
}

void
YbRefreshSessionStatsBeforeExecution()
{
	/*
	 * Catalog related stats must not be reset here because most
	 * catalog lookups for a given query happen between
	 * (AFTER_EXECUTOR_END(N-1) to BEFORE_EXECUTOR_START(N)] where 'N'
	 * is the currently executing query in the session that we are
	 * interested in collecting stats for. The catalog read related can be
	 * refreshed during/after query execution.
	 */
	refreshExecStats(&yb_session_stats, false);
}

void
YbRefreshSessionStatsDuringExecution()
{
	/*
	 * Updates the stats snapshot with all stats. Stats that are
	 * incremented async to the Postgres execution framework (for
	 * example: reads caused by triggers and flushes), need special
	 * handling. This is because Postgres invokes the end of execution
	 * context (EndPlan() and EndExecutor()) before we have a chance to
	 * account for the flushes and trigger reads. We get around this by
	 * maintaining a query level instrumentation object in
	 * executor/execdesc.h which is updated with the async stats right
	 * before the execution context is garbage collected.
	 */
	refreshExecStats(&yb_session_stats, true);
}

void
YbToggleSessionStatsTimer(bool timing_on)
{
	yb_session_stats.current_state.is_timing_required = timing_on;
}

void
YbSetMetricsCaptureType(YBCPgMetricsCaptureType metrics_capture)
{
	yb_session_stats.current_state.metrics_capture = metrics_capture;
}

void YbSetCatalogCacheVersion(YBCPgStatement handle, uint64_t version)
{
	HandleYBStatus(YBIsDBCatalogVersionMode()
		? YBCPgSetDBCatalogCacheVersion(handle, MyDatabaseId, version)
		: YBCPgSetCatalogCacheVersion(handle, version));
}

uint64_t YbGetSharedCatalogVersion()
{
	uint64_t version = 0;
	HandleYBStatus(YBIsDBCatalogVersionMode()
		? YBCGetSharedDBCatalogVersion(MyDatabaseId, &version)
		: YBCGetSharedCatalogVersion(&version));
	return version;
}

void YBSetRowLockPolicy(int *docdb_wait_policy, LockWaitPolicy pg_wait_policy)
{
	if (XactIsoLevel == XACT_REPEATABLE_READ && pg_wait_policy == LockWaitError)
	{
		/* The user requested NOWAIT, which isn't allowed in RR. */
		elog(WARNING, "Setting wait policy to NOWAIT which is not allowed in "
					  "REPEATABLE READ isolation (GH issue #12166)");
	}

	if (IsolationIsSerializable())
	{
		/*
		 * TODO(concurrency-control): We don't honour SKIP LOCKED/ NO WAIT yet in serializable
		 * isolation level.
		 */
		if (pg_wait_policy == LockWaitSkip || pg_wait_policy == LockWaitError)
			elog(WARNING, "%s clause is not supported yet for SERIALIZABLE isolation "
						  "(GH issue #11761)",
						  pg_wait_policy == LockWaitSkip ? "SKIP LOCKED" : "NO WAIT");

		*docdb_wait_policy = LockWaitBlock;
	}
	else
	{
		*docdb_wait_policy = pg_wait_policy;
	}

	if (*docdb_wait_policy == LockWaitBlock && !YBIsWaitQueueEnabled())
	{
		/*
		 * If wait-queues are not enabled, we default to the "Fail-on-Conflict" policy which is
		 * mapped to LockWaitError right now (see WaitPolicy proto for meaning of
		 * "Fail-on-Conflict" and the reason why LockWaitError is not mapped to no-wait
		 * semantics but to Fail-on-Conflict semantics).
		 */
		elog(DEBUG1, "Falling back to LockWaitError since wait-queues are not enabled");
		*docdb_wait_policy = LockWaitError;
	}
	elog(DEBUG2, "docdb_wait_policy=%d pg_wait_policy=%d", *docdb_wait_policy, pg_wait_policy);
}

uint32_t YbGetNumberOfDatabases()
{
	uint32_t num_databases = 0;
	HandleYBStatus(YBCGetNumberOfDatabases(&num_databases));
	/*
	 * It is possible that at the beginning master has not passed back the
	 * contents of pg_yb_catalog_versions back to tserver yet so that tserver's
	 * ysql_db_catalog_version_map_ is still empty. In this case we get 0
	 * databases back.
	 */
	return num_databases;
}

bool YbCatalogVersionTableInPerdbMode()
{
	bool perdb_mode = false;
	HandleYBStatus(YBCCatalogVersionTableInPerdbMode(&perdb_mode));
	return perdb_mode;
}

static bool yb_is_batched_execution = false;

bool YbIsBatchedExecution()
{
	return yb_is_batched_execution;
}

void YbSetIsBatchedExecution(bool value)
{
	yb_is_batched_execution = value;
}

OptSplit *
YbGetSplitOptions(Relation rel)
{
	if (rel->yb_table_properties->is_colocated)
		return NULL;

	OptSplit *split_options = makeNode(OptSplit);
	/*
	 * The split type is NUM_TABLETS when the relation has hash key columns
	 * OR if the relation's range key is currently being dropped. Otherwise,
	 * the split type is SPLIT_POINTS.
	 */
	split_options->split_type =
		rel->yb_table_properties->num_hash_key_columns > 0 ||
		(rel->rd_rel->relkind == RELKIND_RELATION &&
		 RelationGetPrimaryKeyIndex(rel) == InvalidOid) ? NUM_TABLETS :
		SPLIT_POINTS;
	split_options->num_tablets = rel->yb_table_properties->num_tablets;

	/*
	 * Copy split points for range keys with more than one tablet.
	 */
	if (split_options->split_type == SPLIT_POINTS
		&& rel->yb_table_properties->num_tablets > 1)
	{
		YBCPgTableDesc yb_desc = NULL;
		HandleYBStatus(YBCPgGetTableDesc(MyDatabaseId,
						YbGetRelfileNodeId(rel), &yb_desc));
		getRangeSplitPointsList(RelationGetRelid(rel), yb_desc,
								rel->yb_table_properties,
								&split_options->split_points);
	}
	return split_options;
}

bool YbIsColumnPartOfKey(Relation rel, const char *column_name)
{
	if (column_name)
	{
		Bitmapset  *pkey   = YBGetTablePrimaryKeyBms(rel);
		HeapTuple  attTup =
			SearchSysCacheCopyAttName(RelationGetRelid(rel), column_name);
		if (HeapTupleIsValid(attTup))
		{
			Form_pg_attribute attform =
				(Form_pg_attribute) GETSTRUCT(attTup);
			AttrNumber	attnum = attform->attnum;
			if (bms_is_member(attnum -
							  YBGetFirstLowInvalidAttributeNumber(rel), pkey))
				return true;
		}
	}
	return false;
}

/*
 * ```ysql_conn_mgr_sticky_object_count``` is the count of the database objects
 * that requires the sticky connection
 * These objects are
 * 1. WITH HOLD CURSORS
 * 2. TEMP TABLE
 */
int ysql_conn_mgr_sticky_object_count = 0;

/*
 * ```YbIsStickyConnection(int *change)``` updates the number of objects that requires a sticky
 * connection and returns whether or not the client connection
 * requires stickiness. i.e. if there is any `WITH HOLD CURSOR` or `TEMP TABLE`
 * at the end of the transaction.
 */
bool YbIsStickyConnection(int *change)
{
	ysql_conn_mgr_sticky_object_count += *change;
	*change = 0; /* Since it is updated it will be set to 0 */
	elog(DEBUG5, "Number of sticky objects: %d", ysql_conn_mgr_sticky_object_count);
	return (ysql_conn_mgr_sticky_object_count > 0);
}

void**
YbPtrListToArray(const List* str_list, size_t* length) {
	void		**buf;
	ListCell	*lc;

	/* Assumes that the pointer sizes are equal for every type */
	buf = (void **) palloc(sizeof(void *) * list_length(str_list));
	*length = 0;
	foreach (lc, str_list)
	{
		buf[(*length)++] = (void *) lfirst(lc);
	}

	return buf;
}

/*
 * This function is almost equivalent to the `read_whole_file` function of
 * src/postgres/src/backend/commands/extension.c. It differs only in its error
 * handling. The original read_whole_file function logs errors elevel ERROR
 * while this function accepts the elevel as the argument for better control
 * over error handling.
 */
char *
YbReadWholeFile(const char *filename, int* length, int elevel)
{
	char	   *buf;
	FILE	   *file;
	size_t		bytes_to_read;
	struct stat fst;

	if (stat(filename, &fst) < 0)
	{
		ereport(elevel,
				(errcode_for_file_access(),
				 errmsg("could not stat file \"%s\": %m", filename)));
		return NULL;
	}

	if (fst.st_size > (MaxAllocSize - 1))
	{
		ereport(elevel,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("file \"%s\" is too large", filename)));
		return NULL;
	}
	bytes_to_read = (size_t) fst.st_size;

	if ((file = AllocateFile(filename, PG_BINARY_R)) == NULL)
	{
		ereport(elevel,
				(errcode_for_file_access(),
				 errmsg("could not open file \"%s\" for reading: %m",
						filename)));
		return NULL;
	}

	buf = (char *) palloc(bytes_to_read + 1);

	*length = fread(buf, 1, bytes_to_read, file);

	if (ferror(file))
	{
		ereport(elevel,
				(errcode_for_file_access(),
				 errmsg("could not read file \"%s\": %m", filename)));
		return NULL;
	}

	FreeFile(file);

	buf[*length] = '\0';
	return buf;
}

/*
 * Needed to support the guc variable yb_use_tserver_key_auth, which is
 * processed before authentication i.e. before setting this variable.
 */
bool yb_use_tserver_key_auth;

bool
yb_use_tserver_key_auth_check_hook(bool *newval, void **extra, GucSource source)
{
	/* Allow setting yb_use_tserver_key_auth to false */
	if (!(*newval))
		return true;

	/*
	 * yb_use_tserver_key_auth can only be set for client connections made on
	 * unix socket.
	 */
	if (!IS_AF_UNIX(MyProcPort->raddr.addr.ss_family))
		ereport(FATAL,
				(errcode(ERRCODE_PROTOCOL_VIOLATION),
				 errmsg("yb_use_tserver_key_auth can only be set if the "
						"connection is made over unix domain socket")));

	/*
	 * If yb_use_tserver_key_auth is set, authentication method used
	 * is yb_tserver_key. The auth method is decided even before setting the
	 * yb_use_tserver_key GUC variable in hba_getauthmethod (present in hba.c).
	 */
	Assert(MyProcPort->yb_is_tserver_auth_method);

	return true;
}

/*
 * Copies the primary key of a relation to a create stmt intended to clone that
 * relation.
 */
void
YbATCopyPrimaryKeyToCreateStmt(Relation rel, Relation pg_constraint,
							   CreateStmt *create_stmt)
{
	ScanKeyData key;
	SysScanDesc scan;
	HeapTuple	tuple;

	ScanKeyInit(&key, Anum_pg_constraint_conrelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(RelationGetRelid(rel)));
	scan = systable_beginscan(pg_constraint, ConstraintRelidTypidNameIndexId,
							  true /* indexOK */, NULL /* snapshot */,
							  1 /* nkeys */, &key);

	bool pk_copied = false;
	while (!pk_copied && HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		Form_pg_constraint con_form = (Form_pg_constraint) GETSTRUCT(tuple);
		switch (con_form->contype)
		{
			case CONSTRAINT_PRIMARY:
			{
				/*
				 * We don't actually need to map attributes here since there
				 * isn't a new relation yet, but we still need a map to
				 * generate an index stmt.
				 */
				AttrNumber *att_map = convert_tuples_by_name_map(
					RelationGetDescr(rel), RelationGetDescr(rel),
					gettext_noop("could not convert row type"),
					false /* yb_ignore_type_mismatch */);

				Relation idx_rel =
					index_open(con_form->conindid, AccessShareLock);
				IndexStmt *index_stmt = generateClonedIndexStmt(
					NULL, RelationGetRelid(rel), idx_rel, att_map,
					RelationGetDescr(rel)->natts, NULL);

				Constraint *pk_constr = makeNode(Constraint);
				pk_constr->contype = CONSTR_PRIMARY;
				pk_constr->conname = index_stmt->idxname;
				pk_constr->options = index_stmt->options;
				pk_constr->indexspace = index_stmt->tableSpace;

				ListCell *cell;
				foreach(cell, index_stmt->indexParams)
				{
					IndexElem *ielem = lfirst(cell);
					pk_constr->keys =
						lappend(pk_constr->keys, makeString(ielem->name));
					pk_constr->yb_index_params =
						lappend(pk_constr->yb_index_params, ielem);
				}
				create_stmt->constraints =
					lappend(create_stmt->constraints, pk_constr);

				index_close(idx_rel, AccessShareLock);
				pk_copied = true;
				break;
			}
			case CONSTRAINT_CHECK:
			case CONSTRAINT_FOREIGN:
			case CONSTRAINT_UNIQUE:
			case CONSTRAINT_TRIGGER:
			case CONSTRAINT_EXCLUSION:
				break;
			default:
				elog(ERROR, "invalid constraint type \"%c\"",
					 con_form->contype);
				break;
		}
	}
	systable_endscan(scan);
}

/*
 * In YB, a "relfilenode" corresponds to a DocDB table.
 * This function creates a new DocDB table for the given index,
 * with UUID corresponding to the given relfileNodeId. It is used when a
 * user index is re-indexed.
 */
void
YbIndexSetNewRelfileNode(Relation indexRel, Oid newRelfileNodeId,
						 bool yb_copy_split_options)
{
	bool		isNull;
	HeapTuple	tuple;
	Datum		reloptions = (Datum) 0;
	Relation	indexedRel;
	IndexInfo	*indexInfo;

	tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(
		RelationGetRelid(indexRel)));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for index %u",
				RelationGetRelid(indexRel));

	reloptions = SysCacheGetAttr(RELOID, tuple,
		Anum_pg_class_reloptions, &isNull);
	ReleaseSysCache(tuple);
	reloptions = ybExcludeNonPersistentReloptions(reloptions);
	indexedRel = heap_open(
		IndexGetRelation(RelationGetRelid(indexRel), false), ShareLock);
	indexInfo = BuildIndexInfo(indexRel);

	YbGetTableProperties(indexRel);
	YBCCreateIndex(RelationGetRelationName(indexRel),
				   indexInfo,
				   RelationGetDescr(indexRel),
				   indexRel->rd_indoption,
				   reloptions,
				   newRelfileNodeId,
				   indexedRel,
				   yb_copy_split_options ? YbGetSplitOptions(indexRel) : NULL,
				   true /* skip_index_backfill */,
				   indexRel->yb_table_properties->is_colocated,
				   indexRel->yb_table_properties->tablegroup_oid,
				   InvalidOid /* colocation ID */,
				   indexRel->rd_rel->reltablespace,
				   RelationGetRelid(indexRel),
				   YbGetRelfileNodeId(indexRel));

	heap_close(indexedRel, ShareLock);

	if (yb_test_fail_table_rewrite_after_creation)
		elog(ERROR, "Injecting error.");
}

SortByDir
YbSortOrdering(SortByDir ordering, bool is_colocated, bool is_tablegroup,
			   bool is_first_key)
{
	switch (ordering)
	{
		case SORTBY_DEFAULT:
			/*
				 * In Yugabyte, use HASH as the default for the first column of
				 * non-colocated tables
			 */
			if (IsYugaByteEnabled() && yb_use_hash_splitting_by_default &&
				is_first_key && !is_colocated && !is_tablegroup)
				return SORTBY_HASH;

			return SORTBY_ASC;

		case SORTBY_ASC:
		case SORTBY_DESC:
			break;

		case SORTBY_USING:
			ereport(ERROR, (errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				errmsg("USING is not allowed in an index")));
			break;

		case SORTBY_HASH:
			if (is_tablegroup && !MyDatabaseColocated)
				ereport(ERROR, (errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
								errmsg("cannot create a hash partitioned index in a TABLEGROUP")));
			else if (is_colocated)
				ereport(ERROR, (errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
								errmsg("cannot colocate hash partitioned index")));
			break;

		default:
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("unsupported column sort order: %d", ordering)));
			break;
	}

	return ordering;
}

void
YbGetRedactedQueryString(const char* query, int query_len,
						 const char** redacted_query, int* redacted_query_len)
{
	*redacted_query = pnstrdup(query, query_len);
	*redacted_query = RedactPasswordIfExists(*redacted_query);
	*redacted_query_len = strlen(*redacted_query);
}

/*
 * In YB, a "relfilenode" corresponds to a DocDB table.
 * This function creates a new DocDB table for the given table,
 * with UUID corresponding to the given relfileNodeId.
 */
void
YbRelationSetNewRelfileNode(Relation rel, Oid newRelfileNodeId,
							bool yb_copy_split_options, bool is_truncate)
{
	CreateStmt *dummyStmt	 = makeNode(CreateStmt);
	dummyStmt->relation		 =
		makeRangeVar(NULL, RelationGetRelationName(rel), -1);
	Relation pg_constraint = heap_open(ConstraintRelationId,
										RowExclusiveLock);
	YbATCopyPrimaryKeyToCreateStmt(rel, pg_constraint, dummyStmt);
	heap_close(pg_constraint, RowExclusiveLock);
	if (yb_copy_split_options)
	{
		YbGetTableProperties(rel);
		dummyStmt->split_options = YbGetSplitOptions(rel);
	}
	bool is_null;
	HeapTuple tuple = SearchSysCache1(RELOID,
		ObjectIdGetDatum(RelationGetRelid(rel)));
	Datum datum = SysCacheGetAttr(RELOID,
		tuple, Anum_pg_class_reloptions, &is_null);
	if (!is_null)
		dummyStmt->options = untransformRelOptions(datum);
	ReleaseSysCache(tuple);
	YBCCreateTable(dummyStmt, RelationGetRelationName(rel),
					rel->rd_rel->relkind, RelationGetDescr(rel),
					newRelfileNodeId,
					RelationGetNamespace(rel),
					YbGetTableProperties(rel)->tablegroup_oid,
					InvalidOid, rel->rd_rel->reltablespace,
					RelationGetRelid(rel),
					rel->rd_rel->relfilenode,
					is_truncate);

	if (yb_test_fail_table_rewrite_after_creation)
		elog(ERROR, "Injecting error.");
}

Relation
YbGetRelationWithOverwrittenReplicaIdentity(Oid relid, char replident)
{
	Relation relation;
	
	relation = RelationIdGetRelation(relid);
	if (!RelationIsValid(relation))
		elog(ERROR, "could not open relation with OID %u", relid);

	/* Overwrite the replica identity of the relation. */
	relation->rd_rel->relreplident = replident;
	return relation;
}

void
YBCUpdateYbReadTimeAndInvalidateRelcache(uint64_t read_time_ht)
{
	char read_time[50];

	sprintf(read_time, "%llu ht", (unsigned long long) read_time_ht);
	elog(DEBUG1, "Setting yb_read_time to %s ", read_time);
	assign_yb_read_time(read_time, NULL);
	YbRelationCacheInvalidate();
}

uint64_t
YbCalculateTimeDifferenceInMicros(TimestampTz yb_start_time)
{
	long secs;
	int microsecs;

	TimestampDifference(yb_start_time, GetCurrentTimestamp(), &secs,
						&microsecs);
	return secs * USECS_PER_SEC + microsecs;
}
