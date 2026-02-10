/*-------------------------------------------------------------------------
 *
 * pg_yb_utils.c
 *	  Utilities for YugaByte/PostgreSQL integration that have to be defined on
 *	  the PostgreSQL side.
 *
 * Copyright (c) YugabyteDB, Inc.
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

#include "postgres.h"

#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "access/heaptoast.h"
#include "access/htup.h"
#include "access/htup_details.h"
#include "access/relation.h"
#include "access/sysattr.h"
#include "access/table.h"
#include "access/tupdesc.h"
#include "access/xact.h"
#include "catalog/catalog.h"
#include "catalog/index.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
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
#include "catalog/pg_enum.h"
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
#include "catalog/pg_statistic_ext_d.h"
#include "catalog/pg_statistic_ext_data_d.h"
#include "catalog/pg_tablespace.h"
#include "catalog/pg_trigger.h"
#include "catalog/pg_type.h"
#include "catalog/pg_yb_catalog_version.h"
#include "catalog/pg_yb_invalidation_messages.h"
#include "catalog/pg_yb_logical_client_version.h"
#include "catalog/pg_yb_profile.h"
#include "catalog/pg_yb_role_profile.h"
#include "catalog/yb_catalog_version.h"
#include "catalog/yb_logical_client_version.h"
#include "catalog/yb_type.h"
#include "commands/dbcommands.h"
#include "commands/defrem.h"
#include "commands/variable.h"
#include "commands/yb_cmds.h"
#include "common/ip.h"
#include "common/pg_yb_common.h"
#include "executor/ybExpr.h"
#include "fmgr.h"
#include "funcapi.h"
#include "lib/stringinfo.h"
#include "libpq/hba.h"
#include "libpq/libpq-be.h"
#include "libpq/libpq.h"
#include "libpq/pqformat.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/readfuncs.h"
#include "optimizer/cost.h"
#include "optimizer/plancat.h"
#include "parser/parse_utilcmd.h"
#include "pg_yb_utils.h"
#include "pgstat.h"
#include "postmaster/interrupt.h"
#include "replication/origin.h"
#ifndef HAVE_GETRUSAGE
#include "rusagestub.h"
#endif
#include "storage/procarray.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/fmgroids.h"
#include "utils/inval.h"
#include "utils/jsonb.h"
#include "utils/lsyscache.h"
#include "utils/pg_locale.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "utils/snapshot.h"
#include "utils/spccache.h"
#include "utils/syscache.h"
#include "utils/uuid.h"
#include "yb/yql/pggate/util/ybc_util.h"
#include "yb/yql/pggate/ybc_gflags.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb_ash.h"
#include "yb_query_diagnostics.h"

static uint64_t yb_catalog_cache_version = YB_CATCACHE_VERSION_UNINITIALIZED;
static uint64_t yb_last_known_catalog_cache_version = YB_CATCACHE_VERSION_UNINITIALIZED;
static uint64_t yb_new_catalog_version = YB_CATCACHE_VERSION_UNINITIALIZED;

static uint64_t yb_logical_client_cache_version = YB_CATCACHE_VERSION_UNINITIALIZED;
static bool yb_need_invalidate_all_table_cache = false;

static Oid	yb_system_db_oid_cache = InvalidOid;

static bool YbHasDdlMadeChanges();
static int YbGetNumCreateFunctionStmts();
static int YbGetNumRollbackToSavepointStmts();
static bool YBIsCurrentStmtCreateFunction();

uint64_t
YBGetActiveCatalogCacheVersion()
{
	if (yb_catalog_version_type == CATALOG_VERSION_CATALOG_TABLE)
	{
		/*
		 * Note that YBIsCurrentStmtCreateFunction is for both CREATE (OR REPLACE)
		 * FUNCTION and CREATE (OR REPLACE) PROCEDURE.
		 */
		if (YBGetDdlNestingLevel() > 0 && YBIsCurrentStmtCreateFunction())
			return yb_catalog_cache_version + 1;
		if (YBIsDdlTransactionBlockEnabled())
		{
			uint64_t active_catalog_version = yb_catalog_cache_version +
											  YbGetNumRollbackToSavepointStmts();
			if (YBIsCurrentStmtCreateFunction())
				active_catalog_version += YbGetNumCreateFunctionStmts();
			return active_catalog_version;
		}
	}
	return yb_catalog_cache_version;
}

uint64_t
YbGetCatalogCacheVersion()
{
	return yb_catalog_cache_version;
}

uint64_t
YbGetNewCatalogVersion()
{
	return yb_new_catalog_version;
}

void
YbSetNeedInvalidateAllTableCache()
{
	yb_need_invalidate_all_table_cache = true;
}

void
YbResetNeedInvalidateAllTableCache()
{
	yb_need_invalidate_all_table_cache = false;
}

bool
YbGetNeedInvalidateAllTableCache()
{
	return yb_need_invalidate_all_table_cache;
}

bool
YbCanTryInvalidateTableCacheEntry()
{
	return IsYugaByteEnabled() &&
		yb_enable_invalidate_table_cache_entry &&
		!yb_need_invalidate_all_table_cache;
}

YbcPgLastKnownCatalogVersionInfo
YbGetCatalogCacheVersionForTablePrefetching()
{
	/*
	 * TODO: In future YBGetLastKnownCatalogCacheVersion must be used instead of
	 * YbGetMasterCatalogVersion to reduce numer of RPCs to a master.
	 * But this requires some additional changes. This optimization will
	 * be done separately.
	 */
	uint64_t	version = YB_CATCACHE_VERSION_UNINITIALIZED;
	YbcReadHybridTime read_time = {};
	bool		is_db_catalog_version_mode = YBIsDBCatalogVersionMode();

	if (*YBCGetGFlags()->ysql_enable_read_request_caching)
	{
		YbInvalidateCatalogSnapshot();
		version = YbGetMasterCatalogVersion();
		read_time = YBCGetPgCatalogReadTime();
	}
	return (YbcPgLastKnownCatalogVersionInfo)
	{
		.version = version,
			.version_read_time = read_time,
			.is_db_catalog_version_mode = is_db_catalog_version_mode,
	};
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
SendLogicalClientCacheVersionToFrontend()
{
	StringInfoData buf;

	/* Initialize buffer to store the outgoing message */
	initStringInfo(&buf);

	/* Use 'r' for a YB_PARAMETER_STATUS message */
	pq_beginmessage(&buf, 'r');
	pq_sendstring(&buf, "yb_logical_client_version");	/* Key */
	char		yb_logical_client_cache_version_str[16];

	snprintf(yb_logical_client_cache_version_str, 16, "%" PRIu64,
			 yb_logical_client_cache_version);
	pq_sendstring(&buf, yb_logical_client_cache_version_str);	/* Value */
	/* No flags are needed for this variable */
	pq_sendbyte(&buf, 0);		/* flags */

	pq_endmessage(&buf);

	/* Ensure the message is sent to the frontend */
	pq_flush();
}

void
YbResetNewCatalogVersion()
{
	yb_new_catalog_version = YB_CATCACHE_VERSION_UNINITIALIZED;
}

void
YbSetNewCatalogVersion(uint64_t new_version)
{
	Assert(yb_new_catalog_version == YB_CATCACHE_VERSION_UNINITIALIZED);
	yb_new_catalog_version = new_version;
	if (*YBCGetGFlags()->log_ysql_catalog_versions)
		ereport(LOG,
				(errmsg("set new catalog version: %" PRIu64,
						yb_new_catalog_version)));
}

void
YbSetLogicalClientCacheVersion(uint64_t logical_client_cache_version)
{
	if (yb_logical_client_cache_version == YB_CATCACHE_VERSION_UNINITIALIZED)
		yb_logical_client_cache_version = logical_client_cache_version;
}

void
YbResetLogicalClientCacheVersion()
{
	yb_logical_client_cache_version = YB_CATCACHE_VERSION_UNINITIALIZED;
}

void
YbUpdateLastKnownCatalogCacheVersion(uint64_t catalog_cache_version)
{
	if (yb_last_known_catalog_cache_version < catalog_cache_version)
		yb_last_known_catalog_cache_version = catalog_cache_version;
}

void
YbResetCatalogCacheVersion()
{
	yb_catalog_cache_version = YB_CATCACHE_VERSION_UNINITIALIZED;
	yb_pgstat_set_catalog_version(yb_catalog_cache_version);
}

/* These values are lazily initialized based on corresponding environment variables. */
int			ybc_pg_double_write = -1;
int			ybc_disable_pg_locking = -1;

/* Forward declarations */
static void YBCInstallTxnDdlHook();
static bool YBCanEnableDBCatalogVersionMode();

bool		yb_enable_docdb_tracing = false;
bool		yb_read_from_followers = false;
bool		yb_follower_reads_behavior_before_fixing_20482 = false;
int32_t		yb_follower_read_staleness_ms = 0;
bool		yb_default_collation_resolved = false;

bool
IsYugaByteEnabled()
{
	/* We do not support Init/Bootstrap processing modes yet. */
	return YBCPgIsYugaByteEnabled();
}

void
CheckIsYBSupportedRelationByKind(char relkind)
{
	if (!(relkind == RELKIND_RELATION || relkind == RELKIND_INDEX ||
		  relkind == RELKIND_VIEW || relkind == RELKIND_SEQUENCE ||
		  relkind == RELKIND_COMPOSITE_TYPE || relkind == RELKIND_PARTITIONED_TABLE ||
		  relkind == RELKIND_PARTITIONED_INDEX || relkind == RELKIND_FOREIGN_TABLE ||
		  relkind == RELKIND_MATVIEW || relkind == RELKIND_TOASTVALUE))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("this feature is not supported in Yugabyte")));
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

	const char	relkind = relation->rd_rel->relkind;

	CheckIsYBSupportedRelationByKind(relkind);

	/*
	 * Currently only support regular tables and indexes. Temp tables and
	 * views are supported, but they are not YB relations.
	 */
	return ((relkind == RELKIND_RELATION ||
			 relkind == RELKIND_INDEX ||
			 relkind == RELKIND_PARTITIONED_TABLE ||
			 relkind == RELKIND_PARTITIONED_INDEX ||
			 relkind == RELKIND_MATVIEW) &&
			relation->rd_rel->relpersistence != RELPERSISTENCE_TEMP);
}

bool
IsYBRelationById(Oid relid)
{
	Relation	relation = RelationIdGetRelation(relid);
	bool		is_supported = IsYBRelation(relation);

	RelationClose(relation);
	return is_supported;
}

bool
IsYBBackedRelation(Relation relation)
{
	return (IsYBRelation(relation) ||
			(relation->rd_rel->relkind == RELKIND_VIEW &&
			 relation->rd_rel->relpersistence != RELPERSISTENCE_TEMP));
}

bool
YbIsTempRelation(Relation relation)
{
	return relation->rd_rel->relpersistence == RELPERSISTENCE_TEMP;
}

bool
YbIsRangeVarTempRelation(const RangeVar *relation)
{
	Oid			relid = RangeVarGetRelidExtended(relation, NoLock,
												 RVR_MISSING_OK, /* callback */ NULL, /* callback_arg */ NULL);

	return OidIsValid(relid) && get_rel_persistence(relid) == RELPERSISTENCE_TEMP;
}

bool
IsRealYBColumn(Relation rel, int attrNum)
{
	return (attrNum > 0 &&
			!TupleDescAttr(rel->rd_att, attrNum - 1)->attisdropped);
}

bool
IsYBSystemColumn(int attrNum)
{
	return (attrNum == YBRowIdAttributeNumber ||
			attrNum == YBIdxBaseTupleIdAttributeNumber ||
			attrNum == YBUniqueIdxKeySuffixAttributeNumber);
}

AttrNumber
YBGetFirstLowInvalidAttributeNumber(Relation relation)
{
	/*
	 * Foreign tables contain a superset of columns that a foreign server is
	 * allowed to populate. These are usually user defined columns. With
	 * postgres_fdw (a foreign data wrapper that points to a server that speaks
	 * the postgres wire protocol), a foreign server may be yet another
	 * YugabyteDB instance. In this case, the foreign table (param: relation)
	 * is simply a pointer to a YugabyteDB table on the foreign cluster, which
	 * of course has a ybctid system column. The ybctid column is used
	 * extensively by postgres_fdw implicitly to perform updates and deletes.
	 * It is not very convenient to check what FDW a foreign table belongs to.
	 * Furthermore, all foreign tables (irrespective of FDW) are currently
	 * created with a ybctid column. Therefore, assume that that all foreign
	 * tables have a ybctid column and return the YB-specific offset.
	 */
	return (IsYBRelation(relation) ||
			relation->rd_rel->relkind == RELKIND_FOREIGN_TABLE ?
			YBFirstLowInvalidAttributeNumber :
			FirstLowInvalidHeapAttributeNumber);
}

AttrNumber
YBGetFirstLowInvalidAttributeNumberFromOid(Oid relid)
{
	Relation	relation = RelationIdGetRelation(relid);
	AttrNumber	attr_num = YBGetFirstLowInvalidAttributeNumber(relation);

	RelationClose(relation);
	return attr_num;
}

int
YBAttnumToBmsIndex(Relation rel, AttrNumber attnum)
{
	return YBAttnumToBmsIndexWithMinAttr(YBGetFirstLowInvalidAttributeNumber(rel),
										 attnum);
}

AttrNumber
YBBmsIndexToAttnum(Relation rel, int idx)
{
	return YBBmsIndexToAttnumWithMinAttr(YBGetFirstLowInvalidAttributeNumber(rel),
										 idx);
}

int
YBAttnumToBmsIndexWithMinAttr(AttrNumber minattr, AttrNumber attnum)
{
	return attnum - minattr + 1;
}

AttrNumber
YBBmsIndexToAttnumWithMinAttr(AttrNumber minattr, int idx)
{
	return idx + minattr - 1;
}

/*
 * Get primary key columns as bitmap of a table,
 * subtracting minattr from attributes.
 */
static Bitmapset *
GetTablePrimaryKeyBms(Relation rel,
					  AttrNumber minattr,
					  bool includeYBSystemColumns)
{
	Oid			dboid = YBCGetDatabaseOid(rel);
	int			natts = RelationGetNumberOfAttributes(rel);
	Bitmapset  *pkey = NULL;
	YbcPgTableDesc ybc_tabledesc = NULL;
	MemoryContext oldctx;

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

		YbcPgColumnInfo column_info = {0};

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

Bitmapset *
YBGetTablePrimaryKeyBms(Relation rel)
{
	if (!rel->primary_key_bms)
	{
		rel->primary_key_bms =
			GetTablePrimaryKeyBms(rel,
								  YBGetFirstLowInvalidAttributeNumber(rel) /* minattr */ ,
								  false /* includeYBSystemColumns */ );
	}
	return rel->primary_key_bms;
}

Bitmapset *
YBGetTableFullPrimaryKeyBms(Relation rel)
{
	if (!rel->full_primary_key_bms)
	{
		rel->full_primary_key_bms =
			GetTablePrimaryKeyBms(rel,
								  YBSystemFirstLowInvalidAttributeNumber + 1 /* minattr */ ,
								  true /* includeYBSystemColumns */ );
	}
	return rel->full_primary_key_bms;
}

extern bool
YBRelHasOldRowTriggers(Relation rel, CmdType operation)
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
		return (trigdesc->trig_update_after_row ||
				trigdesc->trig_update_before_row);
	}
	/*
	 * This is an update operation. We look for both update and delete triggers
	 * as update on partitioned tables can result in deletes as well.
	 */
	return (trigdesc->trig_update_after_row ||
			trigdesc->trig_update_before_row ||
			trigdesc->trig_delete_after_row ||
			trigdesc->trig_delete_before_row);
}

bool
YbIsDatabaseColocated(Oid dbid, bool *legacy_colocated_database)
{
	bool		colocated;

	HandleYBStatus(YBCPgIsDatabaseColocated(dbid, &colocated,
											legacy_colocated_database));
	return colocated;
}

bool
YBRelHasSecondaryIndices(Relation relation)
{
	if (!relation->rd_rel->relhasindex)
		return false;

	bool		has_indices = false;
	List	   *indexlist = RelationGetIndexList(relation);
	ListCell   *lc;

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
	static int	cached_value = -1;

	if (cached_value == -1)
	{
		cached_value = YBCIsEnvVarTrueWithDefault("YB_PG_TRANSACTIONS_ENABLED", true);
	}
	return IsYugaByteEnabled() && cached_value;
}

bool
YBIsReadCommittedSupported()
{
	static int	cached_value = -1;

	if (cached_value == -1)
	{

#ifdef NDEBUG
		cached_value = YBCIsEnvVarTrueWithDefault("FLAGS_yb_enable_read_committed_isolation", true);
#else
		cached_value = YBCIsEnvVarTrueWithDefault("FLAGS_yb_enable_read_committed_isolation", false);
#endif
	}
	return cached_value;
}

bool
IsYBReadCommitted()
{
	return (IsYugaByteEnabled() && YBIsReadCommittedSupported() &&
			(XactIsoLevel == XACT_READ_COMMITTED ||
			 XactIsoLevel == XACT_READ_UNCOMMITTED));
}

bool
YBIsWaitQueueEnabled()
{
	static int	cached_value = -1;

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
			if (MyDatabaseId != Template1DbOid)
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
		YBFlushBufferedOperations(YBCMakeFlushDebugContextSwithToDbCatalogVersionMode(MyDatabaseId));
		return true;
	}

	/* We cannot enable per-db catalog version mode yet. */
	return false;
}

bool
YBIsDBLogicalClientVersionMode()
{
	static bool cached_is_db_logical_client_version_mode = false;

	if (cached_is_db_logical_client_version_mode)
		return true;

	if (!IsYugaByteEnabled() ||
		YbGetLogicalClientVersionType() != LOGICAL_CLIENT_VERSION_CATALOG_TABLE ||
		!*YBCGetGFlags()->TEST_ysql_enable_db_logical_client_version_mode)
		return false;

	/*
	 * During second phase of initdb, logical client version mode is supported.
	 */
	if (YBCIsInitDbModeEnvVarSet())
	{
		cached_is_db_logical_client_version_mode = true;
		return true;
	}

	return true;
}

YbObjectLockMode
YBGetObjectLockMode()
{
	if (!YBTransactionsEnabled())
		return PG_OBJECT_LOCK_MODE;

	static int	cached_value = -1;
	if (cached_value == -1)
	{
		cached_value = *YBCGetGFlags()->enable_object_locking_for_table_locks;
	}
	return cached_value ? YB_OBJECT_LOCK_ENABLED : YB_OBJECT_LOCK_DISABLED;
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
bool
YbNeedAdditionalCatalogTables()
{
	return (*YBCGetGFlags()->ysql_catalog_preload_additional_tables ||
			IS_NON_EMPTY_STR_FLAG(YBCGetGFlags()->ysql_catalog_preload_additional_table_list));
}

static const char *
FetchUniqueConstraintName(Oid relation_id)
{
	const char *name = NULL;
	Relation	rel = RelationIdGetRelation(relation_id);

	if (!rel->rd_index && rel->rd_pkindex != InvalidOid)
	{
		Relation	pkey = RelationIdGetRelation(rel->rd_pkindex);

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
GetStatusMsgAndArgumentsByCode(const uint32_t pg_err_code, YbcStatus s,
							   const char **msg_buf, size_t *msg_nargs,
							   const char ***msg_args, const char **detail_buf,
							   size_t *detail_nargs, const char ***detail_args,
							   const char **detail_log_buf,
							   size_t *detail_log_nargs,
							   const char ***detail_log_args)
{
	const char *status_msg = YBCMessageAsCString(s);
	size_t		status_nargs;
	const char **status_args = YBCStatusArguments(s, &status_nargs);


	/* Initialize message and detail buffers with default values */
	*msg_buf = status_msg;
	*msg_nargs = status_nargs;
	*msg_args = status_args;
	*detail_buf = NULL;
	*detail_nargs = 0;
	*detail_args = NULL;
	*detail_log_buf = NULL;
	*detail_log_nargs = 0;
	*detail_log_args = NULL;
	elog(DEBUG2, "status_msg=%s pg_err_code=%d", status_msg, pg_err_code);

	switch (pg_err_code)
	{
		case ERRCODE_UNIQUE_VIOLATION:
			*msg_buf = "duplicate key value violates unique constraint \"%s\"";
			*msg_nargs = 1;
			*msg_args = (const char **) palloc(sizeof(const char *));
			(*msg_args)[0] = FetchUniqueConstraintName(YBCStatusRelationOid(s));
			break;
		case ERRCODE_YB_TXN_ABORTED:
			*msg_buf = "current transaction is expired or aborted";
			*msg_nargs = 0;
			*msg_args = NULL;

			*detail_buf = status_msg;
			*detail_nargs = status_nargs;
			*detail_args = status_args;
			break;
		case ERRCODE_YB_TXN_CONFLICT:
			*msg_buf = "could not serialize access due to concurrent update";
			*msg_nargs = 0;
			*msg_args = NULL;

			*detail_buf = status_msg;
			*detail_nargs = status_nargs;
			*detail_args = status_args;
			break;
		case ERRCODE_YB_RESTART_READ:
			*msg_buf = "Restart read required";
			*msg_nargs = 0;
			*msg_args = NULL;

			/*
			 * Read restart errors occur when writes fall within the uncertianty
			 * interval [read_time, global_limit).
			 *
			 * Moreover, read_time can be less than the current time since it
			 * is picked as the docdb tablet's safe time as an optimization in
			 * some cases.
			 *
			 * As a consequence, read_time may be lower than the commit time of the
			 * previous transaction from the same session.
			 *
			 * In this case, a read restart error may be issued to move the read
			 * time past the commit time.
			 *
			 * To capture such cases, print the start time of the statement. This
			 * allows comparison between the start time and the original read time.
			 */
			*detail_log_buf = psprintf("%s, stmt_start_time: %s, txn_start_time: %s, iso:%d",
									   status_msg,
									   timestamptz_to_str(GetCurrentStatementStartTimestamp()),
									   timestamptz_to_str(GetCurrentTransactionStartTimestamp()),
									   XactIsoLevel);
			*detail_log_nargs = status_nargs;
			*detail_log_args = status_args;
			break;
		case ERRCODE_YB_DEADLOCK:
			*msg_buf = "deadlock detected";
			*msg_nargs = 0;
			*msg_args = NULL;

			*detail_buf = status_msg;
			*detail_nargs = status_nargs;
			*detail_args = status_args;
			break;
		default:
			break;
	}
}

void
HandleYBStatusIgnoreNotFound(YbcStatus status, bool *not_found)
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
HandleYBStatusWithCustomErrorForNotFound(YbcStatus status,
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
HandleYBTableDescStatus(YbcStatus status, YbcPgTableDesc table)
{
	if (!status)
		return;

	HandleYBStatus(status);
}

static const char *
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
	YbcPgExecStatsState current_state;
	YbcPgExecStats latest_snapshot;
} YbSessionStats;

static YbSessionStats yb_session_stats = {0};

static uint64_t yb_retry_counts[YB_TXN_CONFLICT_KIND_COUNT] = {0};

void
YbResetRetryCounts()
{
	memset(yb_retry_counts, 0, sizeof(yb_retry_counts));
}

void
YbIncrementRetryCount(YbTxnError kind)
{
	Assert(kind >= 0 && kind < YB_TXN_CONFLICT_KIND_COUNT);
	yb_retry_counts[kind]++;

	if (yb_test_reset_retry_counts > 0 && YbGetTotalRetryCount() >= yb_test_reset_retry_counts)
		YbTestGucBlockWhileIntNotEqual(&yb_test_reset_retry_counts, -1, "yb_test_reset_retry_counts");
}

uint64_t
YbGetRetryCount(YbTxnError kind)
{
	Assert(kind >= 0 && kind < YB_TXN_CONFLICT_KIND_COUNT);
	return yb_retry_counts[kind];
}

uint64_t
YbGetTotalRetryCount()
{
	return yb_retry_counts[YB_TXN_CONFLICT] + yb_retry_counts[YB_TXN_RESTART_READ] +
		   yb_retry_counts[YB_TXN_DEADLOCK] + yb_retry_counts[YB_TXN_ABORTED];
}

YbTxnError
YbSqlErrorCodeToTransactionError(int sqlerrcode)
{
	switch (sqlerrcode)
	{
		case ERRCODE_YB_TXN_CONFLICT:
			return YB_TXN_CONFLICT;
		case ERRCODE_YB_RESTART_READ:
			return YB_TXN_RESTART_READ;
		case ERRCODE_YB_DEADLOCK:
			return YB_TXN_DEADLOCK;
		case ERRCODE_YB_TXN_ABORTED:
			return YB_TXN_ABORTED;
		case ERRCODE_YB_TXN_SKIP_LOCKING:
			return YB_TXN_SKIP_LOCKING;
		case ERRCODE_YB_TXN_LOCK_NOT_FOUND:
			return YB_TXN_LOCK_NOT_FOUND;
		default:
			ereport(ERROR,
					(errmsg("unknown sql error code: %d", sqlerrcode)));
	}
}

static YbcPgAshConfig ash_config;

static void
IpAddressToBytes(YbcPgAshConfig *ash_config)
{
	if (!YbAshIsClientAddrSet())
		return;

	uint8_t		addr_family = ash_config->metadata->addr_family;

	switch (addr_family)
	{
		case AF_UNIX:
			yb_switch_fallthrough();
		case AF_UNSPEC:
			break;
		case AF_INET:
			yb_switch_fallthrough();
		case AF_INET6:
			if (inet_ntop(addr_family, ash_config->metadata->client_addr,
						  ash_config->host, INET6_ADDRSTRLEN) == NULL)
				ereport(LOG,
						(errmsg("failed converting IP address from binary to string")));
			break;
		default:
			ereport(LOG,
					(errmsg("unknown address family found: %u", addr_family)));
	}
}

static uint16_t
YbGetSessionReplicationOriginId(void)
{
	return replorigin_session_origin;
}

void
YBInitPostgresBackend(const char *program_name, const YbcPgInitPostgresInfo *init_info)
{
	HandleYBStatus(YBCInit(program_name, palloc, cstring_to_text_with_len,
						   YbSwitchPgGateMemoryContext, YbCreatePgGateMemoryContext,
						   YbDeletePgGateMemoryContext));

	/*
	 * Enable "YB mode" for PostgreSQL so that we will initiate a connection
	 * to the YugaByte cluster right away from every backend process. We only

	 * do this if this env variable is set, so we can still run the regular
	 * PostgreSQL "make check".
	 */
	if (YBIsEnabledInPostgresEnvVar())
	{
		const YbcPgCallbacks callbacks = {
			.GetCurrentYbMemctx = &GetCurrentYbMemctx,
			.GetDebugQueryString = &GetDebugQueryString,
			.WriteExecOutParam = &YbWriteExecOutParam,
			.UnixEpochToPostgresEpoch = &YbUnixEpochToPostgresEpoch,
			.ConstructArrayDatum = &YbConstructArrayDatum,
			.CheckUserMap = &check_usermap,
			.PgstatReportWaitStart = &yb_pgstat_report_wait_start,
			.GetCatalogSnapshotReadPoint = &YbGetCatalogSnapshotReadPoint,
			.GetSessionReplicationOriginId = &YbGetSessionReplicationOriginId,
			.CheckForInterrupts = &YBCheckForInterrupts,
		};

		ash_config.metadata = &MyProc->yb_ash_metadata;

		IpAddressToBytes(&ash_config);
		const YbcPgInitPostgresInfo default_init_info = {
			.parallel_leader_session_id = NULL,
			.shared_data = &MyProc->yb_shared_data
		};

		HandleYBStatusAtErrorLevel(YBCInitPgGate(YbGetTypeTable(),
												 &callbacks,
												 init_info ? init_info : &default_init_info,
												 &ash_config,
												 &yb_session_stats.current_state,
												 IsBinaryUpgrade), FATAL);
		YBCInstallTxnDdlHook();

		/*
		 * The auth-backend doesn't execute user queries. So we don't need ASH
		 * and query diagnostics.
		 */
		if (!YbIsAuthBackend())
		{
			if (yb_enable_ash)
				YbAshInit();

			if (yb_enable_query_diagnostics)
				YbQueryDiagnosticsInstallHook();
		}

		/*
		 * Upon completion of the first heartbeat to the local tserver, retrieve
		 * and store the session ID in shared memory, so that entities
		 * associated with a session ID (like txn IDs) can be transitively
		 * mapped to PG backends.
		 */
		yb_pgstat_add_session_info(YBCPgGetSessionID());
	}
}

void
YBOnPostgresBackendShutdown()
{
	YBCDestroyPgGate();
}

void
YbWaitForSharedCatalogVersionToCatchup(uint64_t version)
{
	if (!YbIsInvalidationMessageEnabled())
		return;

	/*
	 * When incremental catalog cache is enabled, we want to wait
	 * for the yb_new_catalog_version to propagate to shared
	 * memory of this node to allow proper ordering of the following
	 * scenario:
	 * SELECT * FROM foo;
	 * \! ysqlsh -f ddl_script.sql
	 * SELECT * FROM foo;
	 * where ddl_script.sql may contain DDL statement(s) that caused
	 * breaking catalog version to increment. Assume there are no other
	 * conconcurrent DDLs. Due to heartbeat delay, we may see this
	 * session's local catalog version as 1, shared memory catalog version
	 * as 3, but latest breaking catalog version as 5 after ddl_script.sql
	 * completes. With incremental cache refresh we only ask for inval
	 * messages of version 2 and 3 and then we will start executing the
	 * second SELECT. It is possible by the time the read RPC reaches the
	 * target tablet server (which could be this node itself), a new
	 * heartbeat has already updated breaking version to 5, causing the
	 * SELECT to fail because it has only version 3. But from user's
	 * pespective, ddl_script.sql has synchronously completed and the
	 * second SELECT had better to see its effect as if ddl_script.sql were
	 * executed inline from this session without an ERROR.
	 * By waiting for yb_new_catalog_version showing up in shared memory,
	 * we avoid the above ERROR because now we ask for inval messages
	 * of version 2, 3, 4, 5 and the read RPC of the second SELECT will
	 * not see the ERROR as described above.
	 */
	uint64_t	shared_catalog_version = YbGetSharedCatalogVersion();

	/* Wait up to 60 seconds, with a 0.1-second interval. */
	int			count = 0;

	while (shared_catalog_version < version && count++ < 600)
	{
		/*
		 * This can happen if database MyDatabaseId is dropped by another session.
		 */
		if (shared_catalog_version == YB_CATCACHE_VERSION_UNINITIALIZED)
			return;
		/* Avoid flooding the log file, but always print for the first time. */
		if (count % 20 == 1)
			ereport(LOG,
					(errmsg("waiting for shared catalog version to reach %" PRIu64,
							version),
					 errhidestmt(true),
					 errhidecontext(true)));
		/* wait 0.1 sec */
		pg_usleep(100000L);
		shared_catalog_version = YbGetSharedCatalogVersion();
	}
	if (shared_catalog_version >= version)
		ereport(LOG,
				(errmsg("shared catalog version has reached %" PRIu64,
						shared_catalog_version),
				 errhidestmt(true),
				 errhidecontext(true)));
	else
		ereport(WARNING,
				(errmsg("shared catalog version %" PRIu64 " has not reached %" PRIu64,
						shared_catalog_version, version),
				 errhidestmt(true),
				 errhidecontext(true)));
}

/*---------------------------------------------------------------------------*/
/* Transactional DDL support - Common Definitions                            */
/*---------------------------------------------------------------------------*/

static ProcessUtility_hook_type prev_ProcessUtility = NULL;
typedef struct
{
	uint64_t	applied;
	uint64_t	pending;
} YbCatalogModificationAspects;

typedef struct YbCatalogMessageList
{
	SharedInvalidationMessage *msgs;
	size_t		nmsgs;
	struct YbCatalogMessageList *next;
} YbCatalogMessageList;

/*
 * Some SQL statements require to switch to other userid (e.g. table owner's
 * userid) and lock down security-restricted operations during execution.
 * PG saves and restores userid and SecurityRestrictionContext properly during
 * normal execution. They aren't restored when an exception happens so that the
 * normal execution is aborted. This struct is used to record userid and
 * SecurityRestrictionContextwe so that we can properly restore the saved values
 * during query retries when the normal execution is aborted.
 */
typedef struct YbUserIdAndSecContext
{
	bool		is_set;
	Oid			userid;
	int			sec_context;
} YbUserIdAndSecContext;

typedef struct YbDatabaseAndRelfileNodeId
{
	Oid			database_oid;
	Oid			relfilenode_id;
} YbDatabaseAndRelfileNodeOid;

typedef struct
{
	int			nesting_level;
	MemoryContext mem_context;
	YbCatalogModificationAspects catalog_modification_aspects;
	bool		is_global_ddl;
	/* set to true if the current statement being executed is a DDL */
	bool		is_top_level_ddl_active;
	NodeTag		current_stmt_node_tag;
	CommandTag	current_stmt_ddl_command_tag;
	CommandTag	last_stmt_ddl_command_tag;
	Oid			database_oid;
	int			num_committed_pg_txns;

	/*
	 * Number of create function or procedure statements in the current ddl
	 * transaction block. Only used when ddl transaction block is enabled.
	 */
	int			num_create_function_stmts;
	/*
	 * Number of rollback to savepoint statements in the current ddl
	 * transaction block. Only used when ddl transaction block is enabled.
	 */
	int			num_rollback_to_savepoint_stmts;
	/*
	 * This indicates whether the current DDL transaction is running as part of
	 * the regular transaction block.
	 *
	 * Set to false if yb_ddl_transaction_block_enabled is false.
	 *
	 * This is also false for online schema changes as they are a class of DDLs
	 * that split a single DDL into several steps. Each of these steps is a
	 * separate transaction that commits independently of the top level
	 * transaction. We need these steps to commit so that these intermediate
	 * changes are visible to all other backends. Note that an online schema
	 * change can never happen in a transaction block.
	 */
	bool		use_regular_txn_block;

	/*
	 * List of YbDatabaseAndRelfileNodeId representing tables that have been
	 * altered in the current transaction block. This is used to invalidate the
	 * cache of the altered tables upon rollback of the transaction.
	 *
	 * When yb_ddl_transaction_block_enabled is false, this list just
	 * contains the tables that have been altered as part of the current ALTER
	 * TABLE statement. Otherwise, it includes all the tables that have been
	 * altered in the transaction block i.e. could be from multiple alter table
	 * statements.
	 *
	 * Allocated inside the TopTransactionContext since it needs to be
	 * maintained for the complete transaction block. Cleared whenever
	 * ddl_transaction_state is reset.
	 */
	List	   *altered_table_ids;

	YbCatalogMessageList *committed_pg_txn_messages;
	bool		force_send_inval_messages;
	YbUserIdAndSecContext userid_and_sec_context;
} YbDdlTransactionState;

static YbDdlTransactionState ddl_transaction_state = {0};

static void YBResetEnableSpecialDDLMode();
static void YBResetDdlState();

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

	/*
	 * use_regular_txn_block is only true if
	 * yb_ddl_transaction_block_enabled is true. So no need to check the
	 * flag separately.
	 */
	if (ddl_transaction_state.use_regular_txn_block)
	{
		/*
		 * The transaction contains DDL statements and uses regular transaction
		 * block.
		 */
		YBCommitTransactionContainingDDL();
		return;
	}

	HandleYBStatus(YBCPgCommitPlainTransaction());
}

void
YBCAbortTransaction()
{
	if (!IsYugaByteEnabled() || !YBTransactionsEnabled())
		return;

	if (ddl_transaction_state.use_regular_txn_block)
		YBResetDdlState();

	/*
	 * If a DDL operation during a DDL txn fails, the txn will be aborted before
	 * we get here. However if there are failures afterwards (i.e. during
	 * COMMIT or catalog version increment), then we might get here as part of
	 * top level error recovery in PostgresMain() with the DDL txn state still
	 * set in pggate. Clean it up in that case.
	 */
	YbcStatus	status = YBCPgClearSeparateDdlTxnMode();

	/*
	 * Aborting a transaction is likely to fail only when there are issues
	 * communicating with the tserver. Close the backend connection in such
	 * scenarios to avoid a recursive loop of aborting again and again as part
	 * of error handling in PostgresMain() because of the error faced during
	 * abort.
	 *
	 * Note - If you are changing the behavior to not terminate the backend,
	 * please consider its impact on sub-transaction abort failures
	 * (YBCRollbackToSubTransaction) as well.
	 */
	if (unlikely(status))
		elog(FATAL, "Failed to abort DDL transaction: %s", YBCMessageAsCString(status));

	status = YBCPgAbortPlainTransaction();
	if (unlikely(status))
		elog(FATAL, "Failed to abort DML transaction: %s", YBCMessageAsCString(status));
}

void
YBCSetActiveSubTransaction(SubTransactionId id)
{
	HandleYBStatus(YBCPgSetActiveSubTransaction(id));
}

void
YBCRollbackToSubTransaction(SubTransactionId id)
{
	/*
	 * This function is invoked:
	 * - explicitly by user flows ("ROLLBACK TO <savepoint>" commands)
	 * - implicitly as a result of abort/commit flows
	 * - implicitly to implement exception handling in procedures and statement
	 *   retries for YugabyteDB's read committed isolation level
	 * An error in rolling back to a subtransaction is likely due to issues in
	 * communicating with the tserver. Closing the backend connection
	 * here prevents Postgres from attempting transaction error recovery, which
	 * invokes the AbortCurrentTransaction flow (via the top level error handler
	 * in PostgresMain()), and likely ending up in a PANIC'ed state due to
	 * repeated failures caused by AbortSubTransaction not being reentrant.
	 * Closing the backend here is acceptable because alternate ways of
	 * handling this failure end up trying to abort the transaction which
	 * would anyway terminate the backend on failure. Revisit this approach in
	 * case the behavior of YBCAbortTransaction changes.
	 */
	YbcStatus	status = YBCPgRollbackToSubTransaction(id);

	if (unlikely(status))
		elog(FATAL, "Failed to rollback to subtransaction %" PRId32 ": %s",
			 id, YBCMessageAsCString(status));

	YbInvalidateTableCacheForAlteredTables();
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

const char *
YBPgTypeOidToStr(Oid type_id)
{
	switch (type_id)
	{
		case BOOLOID:
			return "BOOL";
		case BYTEAOID:
			return "BYTEA";
		case CHAROID:
			return "CHAR";
		case NAMEOID:
			return "NAME";
		case INT8OID:
			return "INT8";
		case INT2OID:
			return "INT2";
		case INT2VECTOROID:
			return "INT2VECTOR";
		case INT4OID:
			return "INT4";
		case REGPROCOID:
			return "REGPROC";
		case TEXTOID:
			return "TEXT";
		case OIDOID:
			return "OID";
		case TIDOID:
			return "TID";
		case XIDOID:
			return "XID";
		case CIDOID:
			return "CID";
		case OIDVECTOROID:
			return "OIDVECTOR";
		case JSONOID:
			return "JSON";
		case XMLOID:
			return "XML";
		case PG_NODE_TREEOID:
			return "PG_NODE_TREE";
		case PG_NDISTINCTOID:
			return "PG_NDISTINCT";
		case PG_DEPENDENCIESOID:
			return "PG_DEPENDENCIES";
		case PG_MCV_LISTOID:
			return "PG_MCV_LIST";
		case PG_DDL_COMMANDOID:
			return "PG_DDL_COMMAND";
		case XID8OID:
			return "XID8OID";
		case POINTOID:
			return "POINT";
		case LSEGOID:
			return "LSEG";
		case PATHOID:
			return "PATH";
		case BOXOID:
			return "BOX";
		case POLYGONOID:
			return "POLYGON";
		case LINEOID:
			return "LINE";
		case FLOAT4OID:
			return "FLOAT4";
		case FLOAT8OID:
			return "FLOAT8";
		case UNKNOWNOID:
			return "UNKNOWN";
		case CIRCLEOID:
			return "CIRCLE";
		case MONEYOID:
			return "MONEY";
		case MACADDROID:
			return "MACADDR";
		case INETOID:
			return "INET";
		case CIDROID:
			return "CIDR";
		case MACADDR8OID:
			return "MACADDR8";
		case ACLITEMOID:
			return "ACLITEM";
		case BPCHAROID:
			return "BPCHAR";
		case VARCHAROID:
			return "VARCHAR";
		case DATEOID:
			return "DATE";
		case TIMEOID:
			return "TIME";
		case TIMESTAMPOID:
			return "TIMESTAMP";
		case TIMESTAMPTZOID:
			return "TIMESTAMPTZ";
		case INTERVALOID:
			return "INTERVAL";
		case TIMETZOID:
			return "TIMETZ";
		case BITOID:
			return "BIT";
		case VARBITOID:
			return "VARBIT";
		case NUMERICOID:
			return "NUMERIC";
		case REFCURSOROID:
			return "REFCURSOR";
		case REGPROCEDUREOID:
			return "REGPROCEDURE";
		case REGOPEROID:
			return "REGOPER";
		case REGOPERATOROID:
			return "REGOPERATOR";
		case REGCLASSOID:
			return "REGCLASS";
		case REGCOLLATIONOID:
			return "REGCOLLATION";
		case REGTYPEOID:
			return "REGTYPE";
		case REGROLEOID:
			return "REGROLE";
		case REGNAMESPACEOID:
			return "REGNAMESPACE";
		case UUIDOID:
			return "UUID";
		case PG_LSNOID:
			return "LSN";
		case TSVECTOROID:
			return "TSVECTOR";
		case GTSVECTOROID:
			return "GTSVECTOR";
		case TSQUERYOID:
			return "TSQUERY";
		case REGCONFIGOID:
			return "REGCONFIG";
		case REGDICTIONARYOID:
			return "REGDICTIONARY";
		case JSONBOID:
			return "JSONB";
		case JSONPATHOID:
			return "JSONPATH";
		case TXID_SNAPSHOTOID:
			return "TXID_SNAPSHOT";
		case PG_SNAPSHOTOID:
			return "PG_SNAPSHOT";
		case INT4RANGEOID:
			return "INT4RANGE";
		case NUMRANGEOID:
			return "NUMRANGE";
		case TSRANGEOID:
			return "TSRANGE";
		case TSTZRANGEOID:
			return "TSTZRANGE";
		case DATERANGEOID:
			return "DATERANGE";
		case INT8RANGEOID:
			return "INT8RANGE";
		case INT4MULTIRANGEOID:
			return "INT4MULTIRANGE";
		case NUMMULTIRANGEOID:
			return "NUMMULTIRANGE";
		case TSMULTIRANGEOID:
			return "TSMULTIRANGE";
		case TSTZMULTIRANGEOID:
			return "TSTZMULTIRANGE";
		case DATEMULTIRANGEOID:
			return "DATEMULTIRANGE";
		case INT8MULTIRANGEOID:
			return "INT8MULTIRANGE";
		case RECORDOID:
			return "RECORD";
		case RECORDARRAYOID:
			return "RECORDARRAY";
		case CSTRINGOID:
			return "CSTRING";
		case ANYOID:
			return "ANY";
		case ANYARRAYOID:
			return "ANYARRAY";
		case VOIDOID:
			return "VOID";
		case TRIGGEROID:
			return "TRIGGER";
		case EVENT_TRIGGEROID:
			return "EVENT_TRIGGER";
		case LANGUAGE_HANDLEROID:
			return "LANGUAGE_HANDLER";
		case INTERNALOID:
			return "INTERNAL";
		case ANYELEMENTOID:
			return "ANYELEMENT";
		case ANYNONARRAYOID:
			return "ANYNONARRAY";
		case ANYENUMOID:
			return "ANYENUM";
		case FDW_HANDLEROID:
			return "FDW_HANDLER";
		case INDEX_AM_HANDLEROID:
			return "INDEX_AM_HANDLER";
		case TSM_HANDLEROID:
			return "TSM_HANDLER";
		case TABLE_AM_HANDLEROID:
			return "TABLE_AM_HANDLER";
		case ANYRANGEOID:
			return "ANYRANGE";
		case ANYCOMPATIBLEOID:
			return "ANYCOMPATIBLE";
		case ANYCOMPATIBLEARRAYOID:
			return "ANYCOMPATIBLEARRAY";
		case ANYCOMPATIBLENONARRAYOID:
			return "ANYCOMPATIBLENONARRAY";
		case ANYCOMPATIBLERANGEOID:
			return "ANYCOMPATIBLERANGE";
		case ANYMULTIRANGEOID:
			return "ANYMULTIRANGE";
		case ANYCOMPATIBLEMULTIRANGEOID:
			return "ANYCOMPATIBLEMULTIRANGE";
		case PG_BRIN_BLOOM_SUMMARYOID:
			return "PG_BRIN_BLOOM_SUMMARY";
		case PG_BRIN_MINMAX_MULTI_SUMMARYOID:
			return "PG_BRIN_MINMAX_MULTI_SUMMARY";
		case BOOLARRAYOID:
			return "BOOLARRAY";
		case BYTEAARRAYOID:
			return "BYTEAARRAY";
		case CHARARRAYOID:
			return "CHARARRAY";
		case NAMEARRAYOID:
			return "NAMEARRAY";
		case INT8ARRAYOID:
			return "INT8ARRAY";
		case INT2ARRAYOID:
			return "INT2ARRAY";
		case INT2VECTORARRAYOID:
			return "INT2VECTORARRAY";
		case INT4ARRAYOID:
			return "INT4ARRAY";
		case REGPROCARRAYOID:
			return "REGPROCARRAY";
		case TEXTARRAYOID:
			return "TEXTARRAY";
		case OIDARRAYOID:
			return "OIDARRAY";
		case TIDARRAYOID:
			return "TIDARRAY";
		case XIDARRAYOID:
			return "XIDARRAY";
		case CIDARRAYOID:
			return "CIDARRAY";
		case OIDVECTORARRAYOID:
			return "OIDVECTORARRAY";
		case PG_TYPEARRAYOID:
			return "PG_TYPEARRAY";
		case PG_ATTRIBUTEARRAYOID:
			return "PG_ATTRIBUTEARRAY";
		case PG_PROCARRAYOID:
			return "PG_PROCARRAY";
		case PG_CLASSARRAYOID:
			return "PG_CLASSARRAY";
		case JSONARRAYOID:
			return "JSONARRAY";
		case XMLARRAYOID:
			return "XMLARRAY";
		case XID8ARRAYOID:
			return "XID8ARRAY";
		case POINTARRAYOID:
			return "POINTARRAY";
		case LSEGARRAYOID:
			return "LSEGARRAY";
		case PATHARRAYOID:
			return "PATHARRAY";
		case BOXARRAYOID:
			return "BOXARRAY";
		case POLYGONARRAYOID:
			return "POLYGONARRAY";
		case LINEARRAYOID:
			return "LINEARRAY";
		case FLOAT4ARRAYOID:
			return "FLOAT4ARRAY";
		case FLOAT8ARRAYOID:
			return "FLOAT8ARRAY";
		case CIRCLEARRAYOID:
			return "CIRCLEARRAY";
		case MONEYARRAYOID:
			return "MONEYARRAY";
		case MACADDRARRAYOID:
			return "MACADDRARRAY";
		case INETARRAYOID:
			return "INETARRAY";
		case CIDRARRAYOID:
			return "CIDRARRAY";
		case MACADDR8ARRAYOID:
			return "MACADDR8ARRAY";
		case ACLITEMARRAYOID:
			return "ACLITEMARRAY";
		case BPCHARARRAYOID:
			return "BPCHARARRAY";
		case VARCHARARRAYOID:
			return "VARCHARARRAY";
		case DATEARRAYOID:
			return "DATEARRAY";
		case TIMEARRAYOID:
			return "TIMEARRAY";
		case TIMESTAMPARRAYOID:
			return "TIMESTAMPARRAY";
		case TIMESTAMPTZARRAYOID:
			return "TIMESTAMPTZARRAY";
		case INTERVALARRAYOID:
			return "INTERVALARRAY";
		case TIMETZARRAYOID:
			return "TIMETZARRAY";
		case BITARRAYOID:
			return "BITARRAY";
		case VARBITARRAYOID:
			return "VARBITARRAY";
		case NUMERICARRAYOID:
			return "NUMERICARRAY";
		case REFCURSORARRAYOID:
			return "REFCURSORARRAY";
		case REGPROCEDUREARRAYOID:
			return "REGPROCEDUREARRAY";
		case REGOPERARRAYOID:
			return "REGOPERARRAY";
		case REGOPERATORARRAYOID:
			return "REGOPERATORARRAY";
		case REGCLASSARRAYOID:
			return "REGCLASSARRAY";
		case REGCOLLATIONARRAYOID:
			return "REGCOLLATIONARRAY";
		case REGTYPEARRAYOID:
			return "REGTYPEARRAY";
		case REGROLEARRAYOID:
			return "REGROLEARRAYOID";
		case REGNAMESPACEARRAYOID:
			return "REGNAMESPACEARRAYOID";
		case UUIDARRAYOID:
			return "UUIDARRAY";
		case PG_LSNARRAYOID:
			return "PG_LSNARRAY";
		case TSVECTORARRAYOID:
			return "TSVECTORARRAY";
		case GTSVECTORARRAYOID:
			return "GTSVECTORARRAY";
		case TSQUERYARRAYOID:
			return "TSQUERYARRAY";
		case REGCONFIGARRAYOID:
			return "REGCONFIGARRAY";
		case REGDICTIONARYARRAYOID:
			return "REGDICTIONARYARRAY";
		case JSONBARRAYOID:
			return "JSONBARRAY";
		case JSONPATHARRAYOID:
			return "JSONPATHARRAY";
		case TXID_SNAPSHOTARRAYOID:
			return "TXID_SNAPSHOTARRAY";
		case PG_SNAPSHOTARRAYOID:
			return "PG_SNAPSHOTARRAY";
		case INT4RANGEARRAYOID:
			return "INT4RANGEARRAY";
		case NUMRANGEARRAYOID:
			return "NUMRANGEARRAY";
		case TSRANGEARRAYOID:
			return "TSRANGEARRAY";
		case TSTZRANGEARRAYOID:
			return "TSTZRANGEARRAY";
		case DATERANGEARRAYOID:
			return "DATERANGEARRAY";
		case INT8RANGEARRAYOID:
			return "INT8RANGEARRAY";
		case INT4MULTIRANGEARRAYOID:
			return "INT4MULTIRANGEARRAY";
		case NUMMULTIRANGEARRAYOID:
			return "NUMMULTIRANGEARRAY";
		case TSMULTIRANGEARRAYOID:
			return "TSMULTIRANGEARRAY";
		case TSTZMULTIRANGEARRAYOID:
			return "TSTZMULTIRANGEARRAY";
		case DATEMULTIRANGEARRAYOID:
			return "DATEMULTIRANGEARRAY";
		case INT8MULTIRANGEARRAYOID:
			return "INT8MULTIRANGEARRAY";
		case CSTRINGARRAYOID:
			return "CSTRINGARRAY";
		case BSONOID:
			return "BSON";
		default:
			return "user_defined_type";
	}
}

const char *
YBCPgDataTypeToStr(YbcPgDataType yb_type)
{
	switch (yb_type)
	{
		case YB_YQL_DATA_TYPE_NOT_SUPPORTED:
			return "NOT_SUPPORTED";
		case YB_YQL_DATA_TYPE_UNKNOWN_DATA:
			return "UNKNOWN_DATA";
		case YB_YQL_DATA_TYPE_NULL_VALUE_TYPE:
			return "NULL_VALUE_TYPE";
		case YB_YQL_DATA_TYPE_INT8:
			return "INT8";
		case YB_YQL_DATA_TYPE_INT16:
			return "INT16";
		case YB_YQL_DATA_TYPE_INT32:
			return "INT32";
		case YB_YQL_DATA_TYPE_INT64:
			return "INT64";
		case YB_YQL_DATA_TYPE_STRING:
			return "STRING";
		case YB_YQL_DATA_TYPE_BOOL:
			return "BOOL";
		case YB_YQL_DATA_TYPE_FLOAT:
			return "FLOAT";
		case YB_YQL_DATA_TYPE_DOUBLE:
			return "DOUBLE";
		case YB_YQL_DATA_TYPE_BINARY:
			return "BINARY";
		case YB_YQL_DATA_TYPE_TIMESTAMP:
			return "TIMESTAMP";
		case YB_YQL_DATA_TYPE_DECIMAL:
			return "DECIMAL";
		case YB_YQL_DATA_TYPE_VARINT:
			return "VARINT";
		case YB_YQL_DATA_TYPE_INET:
			return "INET";
		case YB_YQL_DATA_TYPE_LIST:
			return "LIST";
		case YB_YQL_DATA_TYPE_MAP:
			return "MAP";
		case YB_YQL_DATA_TYPE_SET:
			return "SET";
		case YB_YQL_DATA_TYPE_UUID:
			return "UUID";
		case YB_YQL_DATA_TYPE_TIMEUUID:
			return "TIMEUUID";
		case YB_YQL_DATA_TYPE_TUPLE:
			return "TUPLE";
		case YB_YQL_DATA_TYPE_TYPEARGS:
			return "TYPEARGS";
		case YB_YQL_DATA_TYPE_USER_DEFINED_TYPE:
			return "USER_DEFINED_TYPE";
		case YB_YQL_DATA_TYPE_FROZEN:
			return "FROZEN";
		case YB_YQL_DATA_TYPE_DATE:
			return "DATE";
		case YB_YQL_DATA_TYPE_TIME:
			return "TIME";
		case YB_YQL_DATA_TYPE_JSONB:
			return "JSONB";
		case YB_YQL_DATA_TYPE_UINT8:
			return "UINT8";
		case YB_YQL_DATA_TYPE_UINT16:
			return "UINT16";
		case YB_YQL_DATA_TYPE_UINT32:
			return "UINT32";
		case YB_YQL_DATA_TYPE_UINT64:
			return "UINT64";
		case YB_YQL_DATA_TYPE_BSON:
			return "BSON";
		default:
			return "unknown";
	}
}

void
YBReportIfYugaByteEnabled()
{
	if (YBIsEnabledInPostgresEnvVar())
	{
		ereport(LOG,
				(errmsg("YugaByte is ENABLED in PostgreSQL. Transactions are %s.",
						(YBCIsEnvVarTrue("YB_PG_TRANSACTIONS_ENABLED") ?
						 "enabled" :
						 "disabled"))));
	}
	else
	{
		ereport(LOG,
				(errmsg("YugaByte is NOT ENABLED -- "
						"this is a vanilla PostgreSQL server!")));
	}
}

bool
YBShouldRestartAllChildrenIfOneCrashes()
{
	if (!YBIsEnabledInPostgresEnvVar())
	{
		ereport(LOG, (errmsg("YBShouldRestartAllChildrenIfOneCrashes returning 0, YBIsEnabledInPostgresEnvVar is false")));
		return true;
	}
	/*
	 * We will use PostgreSQL's default behavior (restarting all children if one of them crashes)
	 * if the flag env variable is not specified or the file pointed by it does not exist.
	 */
	return YBCIsEnvVarTrueWithDefault("FLAGS_yb_pg_terminate_child_backend", true);
}

const char *
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
	if (MyDatabaseId == Template1DbOid || IsSharedRelation(relid))
		return "template1";
	else
		return get_database_name(MyDatabaseId);
}

const char *
YBCGetSchemaName(Oid schemaoid)
{
	/*
	 * Hardcode the names for system namespaces since the cache might not
	 * be initialized during initdb (bootstrap mode).
	 * TODO Eventually YB should switch to using oid's everywhere so
	 * that dbname and schemaname should not be needed at all.
	 */
	if (IsCatalogNamespace(schemaoid))
		return "pg_catalog";
	else if (IsToastNamespace(schemaoid))
		return "pg_toast";
	else
		return get_namespace_name(schemaoid);
}

Oid
YBCGetDatabaseOid(Relation rel)
{
	return YBCGetDatabaseOidFromShared(rel->rd_rel->relisshared,
									   rel->belongs_to_yb_system_db);
}

Oid
YBCGetDatabaseOidByRelid(Oid relid)
{
	Relation	relation = RelationIdGetRelation(relid);
	bool		relisshared = relation->rd_rel->relisshared;

	RelationClose(relation);
	return YBCGetDatabaseOidFromShared(relisshared,
									   relation->belongs_to_yb_system_db);
}

Oid
YbSystemDbOid()
{
	if (yb_system_db_oid_cache == InvalidOid)
		yb_system_db_oid_cache = get_database_oid(YbSystemDbName, true);
	return yb_system_db_oid_cache;
}

Oid
YBCGetDatabaseOidFromShared(bool relisshared, bool belongs_to_yb_system_db)
{
	Assert(!relisshared || !belongs_to_yb_system_db);
	return relisshared ? Template1DbOid :
		(belongs_to_yb_system_db ? YbSystemDbOid() : MyDatabaseId);
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

	double		res = 1.0;

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

bool
YbWholeRowAttrRequired(Relation relation, CmdType operation)
{
	Assert(IsYBRelation(relation));

	/*
	 * For UPDATE, wholerow attribute is required to get the values of unchanged
	 * columns.
	 */
	if (operation == CMD_UPDATE)
		return true;

	/*
	 * For DELETE, wholerow is required for tables with:
	 * 1. secondary indexes to removing index entries
	 * 2. row triggers to pass the old row for trigger execution.
	 */
	return (operation == CMD_DELETE &&
			(YBRelHasSecondaryIndices(relation) ||
			 YBRelHasOldRowTriggers(relation, operation)));
}

/*------------------------------------------------------------------------------
 * YB GUC variables.
 *------------------------------------------------------------------------------
 */

bool		yb_enable_create_with_table_oid = false;
int			yb_index_state_flags_update_delay = 1000;
bool		yb_enable_expression_pushdown = true;
bool		yb_enable_distinct_pushdown = true;
bool		yb_enable_index_aggregate_pushdown = true;
bool		yb_enable_optimizer_statistics = false;
bool		yb_bypass_cond_recheck = true;
bool		yb_make_next_ddl_statement_nonbreaking = false;
bool		yb_make_next_ddl_statement_nonincrementing = false;
bool		yb_plpgsql_disable_prefetch_in_for_query = false;
bool		yb_enable_sequence_pushdown = true;
bool		yb_disable_wait_for_backends_catalog_version = false;
bool		yb_enable_base_scans_cost_model = false;
bool		yb_enable_update_reltuples_after_create_index = false;
int			yb_wait_for_backends_catalog_version_timeout = 5 * 60 * 1000;	/* 5 min */
bool		yb_prefer_bnl = false;
bool		yb_explain_hide_non_deterministic_fields = false;
bool		yb_enable_saop_pushdown = true;
int			yb_toast_catcache_threshold = 2048; /* 2 KB */
int			yb_parallel_range_size = 1024 * 1024;
int			yb_insert_on_conflict_read_batch_size = 1024;
bool		yb_enable_fkey_catcache = true;
bool		yb_enable_nop_alter_role_optimization = true;
bool		yb_enable_inplace_index_update = true;
bool		yb_ignore_freeze_with_copy = true;
bool		yb_enable_docdb_vector_type = false;
bool		yb_enable_invalidation_messages = true;
bool		yb_enable_invalidate_table_cache_entry = true;
int			yb_invalidation_message_expiration_secs = 10;
int			yb_max_num_invalidation_messages = 4096;
bool		yb_enable_parallel_scan_colocated = true;
bool		yb_enable_parallel_scan_hash_sharded = false;
bool		yb_enable_parallel_scan_range_sharded = false;
bool		yb_enable_parallel_scan_system = false;
bool        yb_test_make_all_ddl_statements_incrementing = false;
bool		yb_always_increment_catalog_version_on_ddl = true;
bool		yb_enable_negative_catcache_entries = true;

/* DEPRECATED */
bool		yb_enable_advisory_locks = true;


YBUpdateOptimizationOptions yb_update_optimization_options = {
	.has_infra = true,
	.is_enabled = true,
	.num_cols_to_compare = 50,
	.max_cols_size_to_compare = 10 * 1024
};

bool		yb_speculatively_execute_pl_statements = false;
bool		yb_whitelist_extra_stmts_for_pl_speculative_execution = false;

/*------------------------------------------------------------------------------
 * YB Debug utils.
 *------------------------------------------------------------------------------
 */

bool		yb_debug_log_docdb_error_backtrace = false;

bool		yb_debug_original_backtrace_format = false;

bool		yb_debug_log_internal_restarts = false;

bool		yb_test_system_catalogs_creation = false;

bool		yb_test_fail_next_ddl = false;

bool		yb_force_catalog_update_on_next_ddl = false;

bool		yb_test_fail_all_drops = false;

bool		yb_test_fail_next_inc_catalog_version = false;

double		yb_test_ybgin_disable_cost_factor = 2.0;

char	   *yb_test_block_index_phase = "";

char	   *yb_test_fail_index_state_change = "";

char	   *yb_default_replica_identity = "CHANGE";

bool		yb_test_fail_table_rewrite_after_creation = false;
bool		yb_test_preload_catalog_tables = false;

bool		yb_test_stay_in_global_catalog_version_mode = false;

bool		yb_test_table_rewrite_keep_old_table = false;
bool		yb_test_collation = false;
bool		yb_test_inval_message_portability = false;
int			yb_test_delay_after_applying_inval_message_ms = 0;
int			yb_test_delay_set_local_tserver_inval_message_ms = 0;
double		yb_test_delay_next_ddl = 0;
int			yb_test_reset_retry_counts = -1;

/*
 * These two GUC variables are used together to control whether DDL atomicity
 * is enabled. See comments for the gflag --ysql_yb_enable_ddl_atomicity_infra
 * in common_flags.cc.
 */
bool		yb_enable_ddl_atomicity_infra = true;
bool		yb_ddl_rollback_enabled = false;

bool		yb_silence_advisory_locks_not_supported_error = false;

bool		yb_use_hash_splitting_by_default = true;

bool		yb_xcluster_automatic_mode_target_ddl = false;

bool		yb_enable_extended_sql_codes = false;

bool		yb_user_ddls_preempt_auto_analyze = true;

bool		yb_enable_pg_stat_statements_rpc_stats = true;

bool		yb_enable_pg_stat_statements_docdb_metrics = false;

bool		yb_enable_global_views = false;

const char *
YBDatumToString(Datum datum, Oid typid)
{
	Oid			typoutput = InvalidOid;
	bool		typisvarlena = false;

	getTypeOutputInfo(typid, &typoutput, &typisvarlena);
	return OidOutputFunctionCall(typoutput, datum);
}

const char *
YbHeapTupleToStringWithIsOmitted(HeapTuple tuple, TupleDesc tupleDesc,
								 bool *is_omitted)
{
	/*
	 * sanity checks
	 */
	Assert(tuple != NULL);

	const char *result;
	TupleTableSlot *slot = MakeTupleTableSlot(tupleDesc, &TTSOpsHeapTuple);

	ExecStoreHeapTuple(tuple, slot, false);
	result = YbSlotToStringWithIsOmitted(slot, is_omitted);
	ExecDropSingleTupleTableSlot(slot);
	return result;
}

const char *
YbSlotToString(TupleTableSlot *slot)
{
	return YbSlotToStringWithIsOmitted(slot, NULL);
}

const char *
YbSlotToStringWithIsOmitted(TupleTableSlot *slot, bool *is_omitted)
{
	/*
	 * sanity checks
	 */
	Assert(slot != NULL);
	Assert(slot->tts_tupleDescriptor != NULL);

	Datum		attr = (Datum) 0;
	int			natts = slot->tts_tupleDescriptor->natts;
	bool		isnull = false;
	StringInfoData buf;

	initStringInfo(&buf);

	appendStringInfoChar(&buf, '(');
	if (!TTS_EMPTY(slot))
	{
		for (int attnum = 1; attnum <= natts; ++attnum)
		{
			attr = slot_getattr(slot, attnum, &isnull);
			if (is_omitted && is_omitted[attnum - 1])
			{
				appendStringInfoString(&buf, "omitted");
			}
			else if (isnull)
			{
				appendStringInfoString(&buf, "null");
			}
			else
			{
				Oid			typid = TupleDescAttr(slot->tts_tupleDescriptor, attnum - 1)->atttypid;

				appendStringInfoString(&buf, YBDatumToString(attr, typid));
			}

			if (attnum != natts)
			{
				appendStringInfoString(&buf, ", ");
			}
		}
	}
	appendStringInfoChar(&buf, ')');
	return buf.data;
}

bool
YBIsInitDbAlreadyDone()
{
	bool		done = false;

	HandleYBStatus(YBCPgIsInitDbDone(&done));
	return done;
}

/*---------------------------------------------------------------------------*/
/* Transactional DDL support - Functioning                                   */
/*---------------------------------------------------------------------------*/

static void
MergeCatalogModificationAspects(YbCatalogModificationAspects *aspects,
								bool apply)
{
	if (apply)
		aspects->applied |= aspects->pending;
	aspects->pending = 0;
}

static void
YBResetEnableSpecialDDLMode()
{
	/*
	 * Reset yb_make_next_ddl_statement_nonbreaking to avoid its further side
	 * effect that may not be intended.
	 *
	 * Also, reset Connection Manager cache if the value was cached to begin
	 * with.
	 */
	if (YbIsClientYsqlConnMgr() && yb_make_next_ddl_statement_nonbreaking)
		YbSendParameterStatusForConnectionManager("yb_make_next_ddl_statement_nonbreaking", "false");
	yb_make_next_ddl_statement_nonbreaking = false;

	/*
	 * Reset yb_make_next_ddl_statement_nonincrementing to avoid its further side
	 * effect that may not be intended.
	 *
	 * Also, reset Connection Manager cache if the value was cached to begin
	 * with.
	 */
	if (YbIsClientYsqlConnMgr() && yb_make_next_ddl_statement_nonincrementing)
		YbSendParameterStatusForConnectionManager("yb_make_next_ddl_statement_nonincrementing", "false");
	yb_make_next_ddl_statement_nonincrementing = false;
}

/*
 * Release all space allocated in the yb_memctx of a context and all of
 * its descendants, but don't delete the yb_memctx themselves.
 */
static YbcStatus
YbMemCtxReset(MemoryContext context)
{
	AssertArg(MemoryContextIsValid(context));
	for (MemoryContext child = context->firstchild;
		 child != NULL;
		 child = child->nextchild)
	{
		YbcStatus	status = YbMemCtxReset(child);

		if (status)
			return status;
	}
	return context->yb_memctx ? YBCPgResetMemctx(context->yb_memctx) : NULL;
}

static void
YBClearDdlTransactionState()
{
	if (ddl_transaction_state.altered_table_ids != NIL)
	{
		list_free(ddl_transaction_state.altered_table_ids);
		ddl_transaction_state.altered_table_ids = NIL;
	}

	ddl_transaction_state = (YbDdlTransactionState)
	{
	};
}

static void
YBResetDdlState()
{
	YbcStatus	status = NULL;

	if (ddl_transaction_state.mem_context)
	{
		if (CurrentMemoryContext == ddl_transaction_state.mem_context)
			MemoryContextSwitchTo(ddl_transaction_state.mem_context->parent);

		/*
		 * Reset the yb_memctx of the ddl memory context including its
		 * descendants. This is to ensure that all the operations in this ddl
		 * transaction are completed before we abort the ddl transaction. For
		 * example, when a ddl transaction aborts there may be a PgDocOp in
		 * this ddl transaction which still has a pending Perform operation to
		 * pre-fetch the next batch of rows and the Perform's RPC call has not
		 * completed yet. Releasing the ddl memory context will trigger the
		 * call to ~PgDocOp where we'll wait for the pending operation to
		 * complete. Because all the objects allocated during this ddl
		 * transaction are released, we assume they are no longer needed after
		 * the ddl transaction aborts.
		 */
		status = YbMemCtxReset(ddl_transaction_state.mem_context);
	}

	bool		use_regular_txn_block = ddl_transaction_state.use_regular_txn_block;

	if (ddl_transaction_state.userid_and_sec_context.is_set)
	{
		SetUserIdAndSecContext(ddl_transaction_state.userid_and_sec_context.userid,
							   ddl_transaction_state.userid_and_sec_context.sec_context);
	}

	YBClearDdlTransactionState();
	YBResetEnableSpecialDDLMode();
	/*
	 * If the DDL uses the regular transaction block, then we are not in a
	 * separate DDL transaction mode. The ddl_state stored in PGGate will be
	 * cleared up as part of the abort of the regular transaction.
	 */
	if (!use_regular_txn_block)
		HandleYBStatus(YBCPgClearSeparateDdlTxnMode());
	HandleYBStatus(status);
}

bool
YBIsDdlTransactionBlockEnabled()
{
	bool		enabled = yb_ddl_transaction_block_enabled;

	if (!IsYBReadCommitted())
		return enabled;

	/*
	 * For READ COMMITTED isolation, also check if DDL transaction support has
	 * been explicitly disabled.
	 */
	return enabled && !yb_disable_ddl_transaction_block_for_read_committed;
}

int
YBGetDdlNestingLevel()
{
	return ddl_transaction_state.nesting_level;
}

NodeTag
YBGetCurrentStmtDdlNodeTag()
{
	return ddl_transaction_state.current_stmt_node_tag;
}

CommandTag
YBGetCurrentStmtDdlCommandTag()
{
	return ddl_transaction_state.current_stmt_ddl_command_tag;
}

bool
YBIsCurrentStmtDdl()
{
	return ddl_transaction_state.is_top_level_ddl_active;
}

static bool
YBIsCurrentStmtCreateFunction()
{
	return ddl_transaction_state.current_stmt_node_tag == T_CreateFunctionStmt;
}

bool
YBGetDdlUseRegularTransactionBlock()
{
	return ddl_transaction_state.use_regular_txn_block;
}

void
YBSetDdlOriginalNodeAndCommandTag(NodeTag nodeTag,
								  CommandTag commandTag)
{
	ddl_transaction_state.current_stmt_node_tag = nodeTag;
	ddl_transaction_state.current_stmt_ddl_command_tag = commandTag;
}

void
YbSetIsGlobalDDL()
{
	ddl_transaction_state.is_global_ddl = true;
}

static bool
CheckIsAnalyzeDDL()
{
	if (ddl_transaction_state.current_stmt_node_tag == T_VacuumStmt)
	{
		CommandTag	cmdtag = ddl_transaction_state.current_stmt_ddl_command_tag;

		Assert(cmdtag);
		return cmdtag == CMDTAG_ANALYZE;
	}
	return false;
}

void
YbTrackAlteredTableId(Oid relid)
{
	MemoryContext oldcontext;

	Assert(TopTransactionContext != NULL);
	oldcontext = MemoryContextSwitchTo(TopTransactionContext);

	YbDatabaseAndRelfileNodeOid *entry;

	entry = (YbDatabaseAndRelfileNodeOid *) palloc0(sizeof(YbDatabaseAndRelfileNodeOid));
	entry->database_oid = YBCGetDatabaseOidByRelid(relid);
	entry->relfilenode_id = YbGetRelfileNodeIdFromRelId(relid);

	ddl_transaction_state.altered_table_ids =
		lappend(ddl_transaction_state.altered_table_ids, entry);
	MemoryContextSwitchTo(oldcontext);
}

void
YBIncrementDdlNestingLevel(YbDdlMode mode)
{
	if (YBIsDdlTransactionBlockEnabled() && IsTransactionBlock())
	{
		elog(ERROR,
				"YBIncrementDdlNestingLevel: autonomous DDL not exepcted inside a transaction block");
	}

	if (ddl_transaction_state.nesting_level == 0)
	{
		/*
		 * Restart couting the number of committed PG transactions during
		 * this YB DDL transaction.
		 */
		ddl_transaction_state.num_committed_pg_txns = 0;
		ddl_transaction_state.mem_context =
			AllocSetContextCreate(CurrentMemoryContext,
								  "aux ddl memory context",
								  ALLOCSET_DEFAULT_SIZES);

		MemoryContextSwitchTo(ddl_transaction_state.mem_context);
		ddl_transaction_state.use_regular_txn_block = false;
		HandleYBStatus(YBCPgEnterSeparateDdlTxnMode());

		if (yb_force_catalog_update_on_next_ddl)
		{
			YBCDdlEnableForceCatalogModification();
			yb_force_catalog_update_on_next_ddl = false;
			if (YbIsClientYsqlConnMgr())
				YbSendParameterStatusForConnectionManager("yb_force_catalog_update_on_next_ddl",
														  "false");
		}
		YbMaybeLockMasterCatalogVersion();
	}

	++ddl_transaction_state.nesting_level;
	ddl_transaction_state.catalog_modification_aspects.pending |= mode;
}

void
YBAddDdlTxnState(YbDdlMode mode)
{
	Assert(yb_ddl_transaction_block_enabled);

	/*
	 * If we have already executed a DDL in the current transaction block, then
	 * just add the new mode to the existing transaction state.
	 */
	if (ddl_transaction_state.use_regular_txn_block)
	{
		elog(DEBUG3, "YBAddDdlTxnState: adding mode = %d", mode);

		/*
		 * We can arrive here in two cases:
		 * 1. When there has been a DDL statement before in the transaction
		 *    block. Example: BEGIN; DDL1; DDL2; COMMIT; In this case, when
		 *    executing DDL2, we will arrive at this function with
		 *    ddl_transaction_state.use_regular_txn_block already true.
		 *
		 * 2. When a DDL statement executes another statement internally.
		 *    Example: CREATE TABLE test (a int primary key, b int);
		 *    In this case, the statement also executes a CREATE INDEX
		 *    internally. So we will arrive at this point with
		 *    ddl_transaction_state.use_regular_txn_block as true.
		 */
		ddl_transaction_state.catalog_modification_aspects.pending |= mode;

		/*
		 * Note that T_CreateFunctionStmt is for both CREATE (OR REPLACE) FUNCTION
		 * and CREATE (OR REPLACE) PROCEDURE.
		 */
		ddl_transaction_state.num_create_function_stmts +=
			ddl_transaction_state.current_stmt_node_tag == T_CreateFunctionStmt ? 1 : 0;
		return;
	}

	/*
	 * This is the first DDL statement in the transaction block. We need to set
	 * the DDL state in the PGGate and also initialize ddl_transaction_state.
	 *
	 * Restart counting the number of committed PG transactions during
	 * this YB DDL transaction.
	 */
	ddl_transaction_state.num_committed_pg_txns = 0;
	ddl_transaction_state.num_create_function_stmts =
		ddl_transaction_state.current_stmt_node_tag == T_CreateFunctionStmt ? 1 : 0;
	ddl_transaction_state.mem_context =
		AllocSetContextCreate(CurrentMemoryContext,
							  "aux ddl memory context",
							  ALLOCSET_DEFAULT_SIZES);
	HandleYBStatus(YBCPgSetDdlStateInPlainTransaction());
	ddl_transaction_state.use_regular_txn_block = true;
	ddl_transaction_state.catalog_modification_aspects.pending |= mode;
	YbMaybeLockMasterCatalogVersion();
	elog(DEBUG3, "YBAddDdlTxnState: new DDL in txn block, mode = %d", mode);
}

void
YBMergeDdlTxnState()
{
	Assert(yb_ddl_transaction_block_enabled);

	const bool	has_change = YbHasDdlMadeChanges();
	MergeCatalogModificationAspects(&ddl_transaction_state.catalog_modification_aspects,
									has_change);
}

void
YBAddModificationAspects(YbDdlMode mode)
{
	ddl_transaction_state.catalog_modification_aspects.pending |= mode;
}

static YbDdlMode
YbCatalogModificationAspectsToDdlMode(uint64_t catalog_modification_aspects)
{
	YbDdlMode	mode = catalog_modification_aspects;

	switch (mode)
	{
		case YB_DDL_MODE_NO_ALTERING:
			yb_switch_fallthrough();
		case YB_DDL_MODE_SILENT_ALTERING:
			yb_switch_fallthrough();
		case YB_DDL_MODE_VERSION_INCREMENT:
			yb_switch_fallthrough();
		case YB_DDL_MODE_BREAKING_CHANGE:
			return mode;
	}
	Assert(false);
	return YB_DDL_MODE_BREAKING_CHANGE;
}

YbDdlMode
YBGetCurrentDdlMode()
{
	uint64_t	combined_aspects =
		ddl_transaction_state.catalog_modification_aspects.applied |
		ddl_transaction_state.catalog_modification_aspects.pending;

	return YbCatalogModificationAspectsToDdlMode(combined_aspects);
}

bool
YbIsInvalidationMessageEnabled()
{
	/*
	 * If one or more PG transactions have already been committed, then
	 * we may have missed some invalidation messages associated with them.
	 * For now we only support invalidation messages for per-database
	 * catalog version mode for simplicity because that mode is the default
	 * and there is no reported case where per-database catalog version
	 * mode is disabled to convert the cluster to global catalog version
	 * mode. If there is a demand arise in the future to also support
	 * invalidation messages in global catalog version mode, we can come
	 * back and reconsider that.
	 */
	return yb_enable_invalidation_messages &&
		ddl_transaction_state.num_committed_pg_txns == 0 &&
		YBIsDBCatalogVersionMode();
}

bool
YbTrackPgTxnInvalMessagesForAnalyze()
{
	/*
	 * In some cases, PG can commit when the outer DDL statement isn't complete yet.
	 * PG commits implies the invalidation messages are disposed of and at the end
	 * of the DDL statement when YB tries to fetch the invalidation messages that
	 * are regarded as associated with this DDL statement, we will not get the full
	 * list of messages because those that are already disposed off due to embedded
	 * PG commits.
	 */

	/* We only need to count PG commits within DDL statement. */
	if (ddl_transaction_state.nesting_level == 0)
		return false;

	/* For any other case except for ANALYZE, we need to count PG commits. */
	if (!CheckIsAnalyzeDDL())
		return true;

	/* We only need to count PG commits when using inval messages. */
	if (!YbIsInvalidationMessageEnabled())
		return false;

	/*
	 * If there is no write, then there are no inval messages so this commit is
	 * equivalent to a no-op.
	 */
	if (!YBCPgHasWriteOperationsInDdlTxnMode())
		return false;

	int			numCatCacheMsgs = 0;
	int			numRelCacheMsgs = 0;

	numCatCacheMsgs = YbGetSubGroupInvalMessages(NULL, YB_CATCACHE_MSGS);
	numRelCacheMsgs = YbGetSubGroupInvalMessages(NULL, YB_RELCACHE_MSGS);
	int			nmsgs = numCatCacheMsgs + numRelCacheMsgs;

	/*
	 * If this PG commit does not involve any invalidation messages, we do not
	 * need to count this commit.
	 */
	if (nmsgs == 0)
		return false;

	SharedInvalidationMessage *catCacheInvalMessages = NULL;
	SharedInvalidationMessage *relCacheInvalMessages = NULL;
	SharedInvalidationMessage *currentInvalMessages = NULL;

	numCatCacheMsgs = YbGetSubGroupInvalMessages(&catCacheInvalMessages,
												 YB_CATCACHE_MSGS);
	numRelCacheMsgs = YbGetSubGroupInvalMessages(&relCacheInvalMessages,
												 YB_RELCACHE_MSGS);
	Assert(ddl_transaction_state.mem_context);
	currentInvalMessages = (SharedInvalidationMessage *)
		MemoryContextAlloc(ddl_transaction_state.mem_context,
						   nmsgs * sizeof(SharedInvalidationMessage));
	if (numCatCacheMsgs > 0)
		memcpy(currentInvalMessages,
			   catCacheInvalMessages,
			   numCatCacheMsgs * sizeof(SharedInvalidationMessage));
	if (numRelCacheMsgs > 0)
		memcpy(currentInvalMessages + numCatCacheMsgs,
			   relCacheInvalMessages,
			   numRelCacheMsgs * sizeof(SharedInvalidationMessage));
	if (log_min_messages <= DEBUG1 || yb_debug_log_catcache_events)
		YbLogInvalidationMessages(currentInvalMessages, nmsgs);
	YbCatalogMessageList *current = (YbCatalogMessageList *)
		MemoryContextAlloc(ddl_transaction_state.mem_context,
						   sizeof(YbCatalogMessageList));

	current->msgs = currentInvalMessages;
	current->nmsgs = nmsgs;
	/*
	 * Here we track committed pg txn messages in reverse order. Later
	 * we reverse it again when copying.
	 */
	current->next = ddl_transaction_state.committed_pg_txn_messages;
	ddl_transaction_state.committed_pg_txn_messages = current;
	elog(DEBUG1, "tracking catalog version in nested PG commit");
	return false;
}

void
YbIncrementPgTxnsCommitted()
{
	++ddl_transaction_state.num_committed_pg_txns;
}

static int
YbGetNumCreateFunctionStmts()
{
	return ddl_transaction_state.num_create_function_stmts;
}

static int
YbGetNumRollbackToSavepointStmts()
{
	return ddl_transaction_state.num_rollback_to_savepoint_stmts;
}

/*
 * If local version is x and this DDL incremented catalog version to x + 1,
 * then we can do this optimization because the invalidation messages
 * of x + 1 have been applied by this DDL and incrementing local version to
 * x + 1 will not miss any messages that should be applied. However, if this
 * DDL incremented catalog version to x + 2, it means there is one concurrent
 * DDL that has incremented catalog version to x + 1. In this case if we do
 * this optimization we will miss the messages of x + 1 and then reapply the
 * messages of x + 2. Missing messages of x + 1 will affect correctness. We
 * will leave local version as x which will allow us to apply messages of
 * x + 1, and then reapply messages of x + 2. We assume reapplying messages
 * of x + 2 is fine because it only causes some redundant on-demand loading
 * of cache entries that are removed again by reapplying messages of x + 2.
 */
void
YbCheckNewLocalCatalogVersionOptimization()
{
	Assert(OidIsValid(MyDatabaseId));

	const uint64_t new_version = YbGetNewCatalogVersion();

	if (new_version == YB_CATCACHE_VERSION_UNINITIALIZED)
		/*
		 * If we do not get a new_version as expected, fall back to the old way
		 * where we bump up the local catalog version.
		 * There are two known cases where this can happen:
		 * (1) if we upgrade from an old release that pg_yb_invalidation_messages
		 * does not exist, we could not do incremental catalog cache refresh,
		 * in this case we fall back to old behavior.
		 * (2) if pg_yb_catalog_version is out of sync with pg_database due to
		 * corruption, MyDatabaseId is missing from pg_yb_catalog_version, we will
		 * not be able to return current_version + 1 for MyDatabaseId. In this
		 * case yb_increment_db_catalog_version_with_inval_messages or
		 * yb_increment_all_db_catalog_versions_with_inval_messages returns a PG
		 * null and we detect that and return 0 for new_version. MyDatabaseId
		 * missing from pg_yb_catalog_version is a more critical problem, the system
		 * cannot function properly and needs to be manually fixed. We don't
		 * consider how to properly deal with that case here so also fall back to
		 * old behavior.
		 */
		YbUpdateCatalogCacheVersion(YbGetCatalogCacheVersion() + 1);
	else if (YbGetCatalogCacheVersion() + 1 == new_version)
		YbUpdateCatalogCacheVersion(new_version);
	else
	{
		elog(LOG,
			 "skipped optimization, "
			 "local catalog version of db %u "
			 "kept at %" PRIu64 ", new catalog version %" PRIu64,
			 MyDatabaseId, YbGetCatalogCacheVersion(), new_version);
		/*
		 * If we remain at x when the new_version of this DDL is x + 2, we need
		 * to wait for x + 1's invalidation messages, since we already know the
		 * latest version is >= x + 2, let's wait for shared memory to catch up
		 * to x + 2.
		 */
		YbWaitForSharedCatalogVersionToCatchup(new_version);
	}
}

static void
YbCheckNewSharedCatalogVersionOptimization(bool is_breaking_change,
										   SharedInvalidationMessage *msgs,
										   int nmsgs)
{
	Assert(OidIsValid(MyDatabaseId));

	const uint64_t new_version = YbGetNewCatalogVersion();

	if (new_version == YB_CATCACHE_VERSION_UNINITIALIZED)
		return;

	/*
	 * Do not perform the optimization if there is a gap between the local
	 * catalog version and the new version. Otherwise, PG backends on this
	 * node that have the same local catalog version will not be able to
	 * perform incremental catalog cache refresh because of this gap: it
	 * takes a heartbeat delay for the gap to be filled. Normally the next
	 * tserver to master heartbeat response will bring back the missing
	 * catalog versions and their invalidation messages.
	 */
	if (YbGetCatalogCacheVersion() + 1 != new_version)
		return;

	YbcCatalogMessageList message_list;

	if (msgs)
	{
		message_list.message_list = (char *) msgs;
		message_list.num_bytes = sizeof(SharedInvalidationMessage) * nmsgs;
	}
	else
	{
		/* This is a PG null and will force full catalog cache refresh. */
		message_list.message_list = NULL;
		message_list.num_bytes = 0;
	}

	elog(DEBUG2,
		 "YbCheckNewSharedCatalogVersionOptimization: "
		 "updating tserver shared catalog version to %" PRIu64, new_version);
	if (yb_test_delay_set_local_tserver_inval_message_ms > 0)
		pg_usleep(yb_test_delay_set_local_tserver_inval_message_ms * 1000L);
	YbcStatus	status = YBCPgSetTserverCatalogMessageList(MyDatabaseId,
														   is_breaking_change,
														   new_version,
														   &message_list);

	if (YBCStatusIsTryAgain(status))
	{
		const char *reason = YBCStatusMessageBegin(status);

		elog(LOG, "failed to set local tserver catalog message list, "
			 "waiting for heartbeats instead: %s", reason);
		YBCFreeStatus(status);
		YbWaitForSharedCatalogVersionToCatchup(new_version);
	}
	else
		HandleYBStatus(status);
}

static int
YbTotalCommittedPgTxnMessages()
{
	if (!CheckIsAnalyzeDDL())
	{
		/* For now we only track committed pg txn for ANALYZE */
		Assert(ddl_transaction_state.committed_pg_txn_messages == NULL);
		return 0;
	}
	int			total = 0;

	for (YbCatalogMessageList *current = ddl_transaction_state.committed_pg_txn_messages;
		 current != NULL; current = current->next)
		total += current->nmsgs;
	return total;
}

static void
YbCopyCommittedPgTxnMessages(SharedInvalidationMessage *currentInvalMessages)
{
	YbCatalogMessageList *current;
	int			num_pg_txn_commits = 0;

	for (current = ddl_transaction_state.committed_pg_txn_messages;
		 current != NULL; current = current->next)
		++num_pg_txn_commits;
	YbCatalogMessageList *temp = (YbCatalogMessageList *)
		MemoryContextAlloc(ddl_transaction_state.mem_context,
						   sizeof(YbCatalogMessageList) * num_pg_txn_commits);

	/*
	 * Copy the list in reverse order to get back the original order of committed
	 * pg txns.
	 */
	int			count = num_pg_txn_commits;

	for (current = ddl_transaction_state.committed_pg_txn_messages;
		 current != NULL; current = current->next)
		temp[--count] = *current;
	Assert(count == 0);
	int			total = 0;

	/* Copy the messages of committed pg txns into currentInvalMessages. */
	for (count = 0; count < num_pg_txn_commits; ++count)
	{
		current = &temp[count];
		memcpy(currentInvalMessages + total, current->msgs,
			   current->nmsgs * sizeof(SharedInvalidationMessage));
		total += current->nmsgs;
	}
}

void
YBCommitTransactionContainingDDL()
{
	const bool	has_change = YbHasDdlMadeChanges();

	MergeCatalogModificationAspects(&ddl_transaction_state.catalog_modification_aspects,
									has_change);

	Assert(ddl_transaction_state.nesting_level == 0);
	if (yb_test_fail_next_ddl)
	{
		yb_test_fail_next_ddl = false;
		if (YbIsClientYsqlConnMgr())
			YbSendParameterStatusForConnectionManager("yb_test_fail_next_ddl", "false");
		elog(ERROR, "Failed DDL operation as requested");
	}

	if (yb_test_delay_next_ddl > 0)
	{
		elog(LOG, "sleeping for %d us before next ddl",
			 (int) (yb_test_delay_next_ddl * 1000));
		pg_usleep((int) (yb_test_delay_next_ddl * 1000));
	}

	/*
	 * We cannot reset the ddl memory context as we do in the abort case
	 * (see YBResetDdlState) because there are cases where objects
	 * allocated during the ddl transaction are still needed after this
	 * ddl transaction commits successfully.
	 */

	if (CurrentMemoryContext == ddl_transaction_state.mem_context)
		MemoryContextSwitchTo(ddl_transaction_state.mem_context->parent);

	YBResetEnableSpecialDDLMode();
	bool		increment_done = false;
	bool		is_silent_altering = false;
	int			nmsgs = 0;
	int			numCatCacheMsgs = 0;
	int			numRelCacheMsgs = 0;
	bool		enable_inval_msgs = YbIsInvalidationMessageEnabled();
	bool		is_breaking_change = false;
	SharedInvalidationMessage *currentInvalMessages = NULL;

	if (has_change)
	{
		const YbDdlMode mode = YbCatalogModificationAspectsToDdlMode(ddl_transaction_state.catalog_modification_aspects.applied);

		/* accumulated invalidation messages in the transaction block */
		SharedInvalidationMessage *catCacheInvalMessages = NULL;
		SharedInvalidationMessage *relCacheInvalMessages = NULL;

		/* messages from the current DDL */
		SharedInvalidationMessage *currentCatCacheInvalMessages = NULL;
		SharedInvalidationMessage *currentRelCacheInvalMessages = NULL;

		if (enable_inval_msgs)
		{
			/*
			 * TODO (myang) pg_yb_catalog_version itself has a catalog cache, do
			 * we need to invalidate all of its entries via a call such as
			 * CacheInvalidateCatalog(YBCatalogVersionRelationId)?
			 */
			numCatCacheMsgs = YbGetSubGroupInvalMessages(&catCacheInvalMessages,
														 YB_CATCACHE_MSGS);
			numRelCacheMsgs = YbGetSubGroupInvalMessages(&relCacheInvalMessages,
														 YB_RELCACHE_MSGS);

			currentCatCacheInvalMessages = catCacheInvalMessages;
			if (numCatCacheMsgs > 0)
				Assert(catCacheInvalMessages);
			currentRelCacheInvalMessages = relCacheInvalMessages;
			if (numRelCacheMsgs > 0)
				Assert(relCacheInvalMessages);

			int			numExistingCatCacheMsgs = YbGetNumInvalMessagesInTxn(YB_CATCACHE_MSGS);
			int			numExistingRelCacheMsgs = YbGetNumInvalMessagesInTxn(YB_RELCACHE_MSGS);

			Assert(numCatCacheMsgs >= numExistingCatCacheMsgs);
			Assert(numRelCacheMsgs >= numExistingRelCacheMsgs);

			int			total = YbTotalCommittedPgTxnMessages();

			/*
			 * We can not have committed pg txns in ANALYZE within a
			 * transaction block.
			 */
			Assert(total == 0 || (numExistingCatCacheMsgs == 0 && numExistingRelCacheMsgs == 0));
			/*
			 * Adjust currentCatCacheInvalMessages pointers to the catcache messages
			 * generated by the current DDL. E.g., if numExistingCatCacheMsgs == 20,
			 * it means that we have accumulated 20 catcache messages before the
			 * current DDL. If numCatCacheMsgs == 25, it means the current DDL has
			 * generated 5 messages not 25. We want to skip the first 20 messages to
			 * avoid re-applying them. After that we also need to adjust numCatCacheMsgs
			 * to 5 and numExistingCatCacheMsgs to 25 for the next possible DDL in the
			 * current PG transaction (represented by transInvalInfo).
			 */
			if (currentCatCacheInvalMessages)
				currentCatCacheInvalMessages += numExistingCatCacheMsgs;
			numCatCacheMsgs -= numExistingCatCacheMsgs;
			YbAddNumInvalMessagesInTxn(YB_CATCACHE_MSGS, numCatCacheMsgs);

			/* Same adjustment for relcache messages. */
			if (currentRelCacheInvalMessages)
				currentRelCacheInvalMessages += numExistingRelCacheMsgs;
			numRelCacheMsgs -= numExistingRelCacheMsgs;
			YbAddNumInvalMessagesInTxn(YB_RELCACHE_MSGS, numRelCacheMsgs);

			nmsgs = numCatCacheMsgs + numRelCacheMsgs + total;
			if (nmsgs > 0)
			{
				int			max_allowed = yb_max_num_invalidation_messages;

				if (nmsgs > max_allowed)
				{
					elog(LOG, "too many invalidation messages: %d, max allowed %d",
						 nmsgs, max_allowed);
					/*
					 * If we have too many invalidation messages, write PG null into
					 * messages so that we fall back to do catalog cache refresh.
					 */
					currentInvalMessages = NULL;
				}
				else
				{
					currentInvalMessages = (SharedInvalidationMessage *)
						MemoryContextAlloc(CurTransactionContext,
										   nmsgs * sizeof(SharedInvalidationMessage));
					if (total > 0)
						YbCopyCommittedPgTxnMessages(currentInvalMessages);

					if (numCatCacheMsgs > 0)
						memcpy(currentInvalMessages + total,
							   currentCatCacheInvalMessages,
							   numCatCacheMsgs * sizeof(SharedInvalidationMessage));
					if (numRelCacheMsgs > 0)
						memcpy(currentInvalMessages + total + numCatCacheMsgs,
							   currentRelCacheInvalMessages,
							   numRelCacheMsgs * sizeof(SharedInvalidationMessage));
				}
			}
			else
				Assert(nmsgs == 0);
			YBC_LOG_INFO("DEBUG: pg null=%d, nmsgs=%d", !currentInvalMessages, nmsgs);
		}
		else if (ddl_transaction_state.num_committed_pg_txns > 0)
			YBC_LOG_INFO("DEBUG: num_committed_pg_txns: %d",
						 ddl_transaction_state.num_committed_pg_txns);

		/* Clear yb_sender_pid for unit test to have a stable result. */
		if (yb_test_inval_message_portability && currentInvalMessages)
			for (int i = 0; i < nmsgs; ++i)
			{
				SharedInvalidationMessage *msg = &currentInvalMessages[i];

				msg->yb_header.yb_sender_pid = 0;
			}
		if (currentInvalMessages && log_min_messages <= DEBUG1)
			YbLogInvalidationMessages(currentInvalMessages, nmsgs);

		CommandTag ddl_cmdtag = ddl_transaction_state.current_stmt_ddl_command_tag;
		if (ddl_cmdtag == CMDTAG_UNKNOWN)
			 ddl_cmdtag = ddl_transaction_state.last_stmt_ddl_command_tag;
		const char *command_tag_name = GetCommandTagName(ddl_cmdtag);

		is_breaking_change = mode & YB_SYS_CAT_MOD_ASPECT_BREAKING_CHANGE;
		bool		increment_for_conn_mgr_needed = false;

		if (YbIsYsqlConnMgrEnabled())
		{
			/* We should not come here on auth backend. */
			Assert(!yb_is_auth_backend);
			/*
			 * Auth backends only read shared relations. If tserver cache is used by
			 * auth backends, we need to make sure stale tserver cache entries are
			 * detected by incrementing the catalog version.
			 */
			if (ddl_transaction_state.is_global_ddl &&
				YbCheckTserverResponseCacheForAuthGflags())
				increment_for_conn_mgr_needed = true;
		}

		/*
		 * We can skip incrementing catalog version if nmsgs is 0.
		 */
		increment_done =
			((mode & YB_SYS_CAT_MOD_ASPECT_VERSION_INCREMENT) ||
			 increment_for_conn_mgr_needed) &&
			(!enable_inval_msgs || nmsgs > 0) &&
			YbIncrementMasterCatalogVersionTableEntry(is_breaking_change,
													  ddl_transaction_state.is_global_ddl,
													  command_tag_name,
													  currentInvalMessages, nmsgs);

		is_silent_altering = (mode == YB_DDL_MODE_SILENT_ALTERING);
	}

	Oid			database_oid = YbGetDatabaseOidToIncrementCatalogVersion();
	bool		use_regular_txn_block = ddl_transaction_state.use_regular_txn_block;

	YBClearDdlTransactionState();

	if (use_regular_txn_block)
	{
		HandleYBStatus(YBCPgCommitPlainTransactionContainingDDL(MyDatabaseId, is_silent_altering));
		/*
		 * Next reads from catalog tables have to see changes made by the plain
		 * transaction that contains DDL.
		 */
		if (YBCIsLegacyModeForCatalogOps())
			YBCPgResetCatalogReadTime();
	}
	else
	{
		HandleYBStatus(YBCPgExitSeparateDdlTxnMode(MyDatabaseId,
												   is_silent_altering));
		/*
		 * Next reads from catalog tables have to see changes made by the DDL transaction.
		 */
		YBCPgResetCatalogReadTime();
	}

	/*
	 * Optimization to avoid redundant cache refresh on the current session
	 * since we should have already updated the cache locally while
	 * applying the DDL changes.
	 * (Doing this after YBCPgExitSeparateDdlTxnMode so it only executes
	 * if DDL txn commit succeeds.)
	 */
	if (increment_done)
	{
		if (enable_inval_msgs && database_oid == MyDatabaseId)
		{
			/*
			 * We only do shared catalog version optimization for MyDatabaseId.
			 * That is even if the DDL has global impact, we only set the new
			 * catalog version of MyDatabaseId in shared memory because we do
			 * not know the new catalog version of any other databases for a
			 * global impact DDL.
			 */
			YbCheckNewSharedCatalogVersionOptimization(is_breaking_change,
													   currentInvalMessages,
													   nmsgs);
			YbCheckNewLocalCatalogVersionOptimization();
		}
		else if (database_oid == MyDatabaseId || !YBIsDBCatalogVersionMode())
			YbUpdateCatalogCacheVersion(YbGetCatalogCacheVersion() + 1);
		else
			elog(LOG,
				 "skipped optimization, "
				 "database_oid: %u, "
				 "local catalog version of db %u "
				 "kept at %" PRIu64, database_oid, MyDatabaseId, YbGetCatalogCacheVersion());

		if (YbIsClientYsqlConnMgr())
		{
			/* Wait for tserver hearbeat */
			int32_t		sleep = 1000 * 2 * YBGetHeartbeatIntervalMs();

			elog(LOG_SERVER_ONLY,
				 "connection manager: adding sleep of %d microseconds "
				 "after DDL commit",
				 sleep);
			pg_usleep(sleep);
		}
	}

	List	   *handles = YBGetDdlHandles();
	ListCell   *lc = NULL;

	foreach(lc, handles)
	{
		YbcPgStatement handle = (YbcPgStatement) lfirst(lc);

		/*
		 * At this point we have already applied the DDL in the YSQL layer and
		 * executing the postponed DocDB statement is not strictly required.
		 * Ignore 'NotFound' because DocDB might already notice applied DDL.
		 * See comment for YBGetDdlHandles in xact.h for more details.
		 */
		YbcStatus	status = YBCPgExecPostponedDdlStmt(handle);

		if (YBCStatusIsNotFound(status))
		{
			YBCFreeStatus(status);
		}
		else
		{
			HandleYBStatusAtErrorLevel(status, WARNING);
		}
	}
	YBClearDdlHandles();
	if (increment_done)
		YBC_LOG_INFO("%s: got %d invalidation messages, local catalog version %" PRIu64,
			 __func__, nmsgs, yb_catalog_cache_version);
}

void
YBDecrementDdlNestingLevel()
{
	Assert(!ddl_transaction_state.use_regular_txn_block);

	--ddl_transaction_state.nesting_level;
	/*
	 * Merge catalog modification aspects if the nesting level > 0. For
	 * nesting_level = 0, it is done inside the YBCommitTransactionContainingDDL
	 * function.
	 */
	if (ddl_transaction_state.nesting_level > 0)
	{
		const bool	has_change = YbHasDdlMadeChanges();

		MergeCatalogModificationAspects(&ddl_transaction_state.catalog_modification_aspects,
										has_change);
	}
	/*
	 * The transaction contains DDL statements and uses a separate DDL
	 * transaction.
	 */
	else
		YBCommitTransactionContainingDDL();
}

static Node *
GetActualStmtNode(PlannedStmt *pstmt)
{
	if (nodeTag(pstmt->utilityStmt) == T_ExplainStmt)
	{
		ExplainStmt *stmt = castNode(ExplainStmt, pstmt->utilityStmt);
		Node	   *actual_stmt = castNode(Query, stmt->query)->utilityStmt;

		if (actual_stmt)
		{
			/*
			 * EXPLAIN statement may have multiple ANALYZE options.
			 * The value of the last one will take effect.
			 */
			bool		analyze = false;
			ListCell   *lc;

			foreach(lc, stmt->options)
			{
				DefElem    *opt = (DefElem *) lfirst(lc);

				if (strcmp(opt->defname, "analyze") == 0)
					analyze = defGetBoolean(opt);
			}
			if (analyze)
				return actual_stmt;
		}
	}
	return pstmt->utilityStmt;
}

static bool
YbShouldIncrementLogicalClientVersion(PlannedStmt *pstmt)
{
	Node	   *parsetree = GetActualStmtNode(pstmt);
	NodeTag		node_tag = nodeTag(parsetree);

	switch (node_tag)
	{
		case T_AlterDatabaseSetStmt:
		case T_AlterRoleSetStmt:
			return true;
		case T_AlterRoleStmt:
			{
				AlterRoleStmt *stmt = castNode(AlterRoleStmt, parsetree);

				if (list_length(stmt->options) == 1)
				{
					DefElem    *def = (DefElem *) linitial(stmt->options);

					/*
					 * In case of ALTER ROLE <role> superuser, increment the
					 * logical client as for the new backends, the role will have
					 * superuser priviledges.
					 */
					if (strcmp(def->defname, "superuser") == 0)
						return true;
				}
				break;
			}
		default:
			return false;
	}
	return false;
}

static bool
YbIsTopLevelOrAtomicStatement(ProcessUtilityContext context)
{
	return context == PROCESS_UTILITY_TOPLEVEL ||
		(context == PROCESS_UTILITY_QUERY &&
		 ddl_transaction_state.current_stmt_node_tag != T_RefreshMatViewStmt);
}

YbDdlModeOptional
YbGetDdlMode(PlannedStmt *pstmt, ProcessUtilityContext context,
			 bool *requires_autonomous_transaction)
{
	bool		is_ddl = true;
	bool		should_increment_version_by_default =
		yb_test_make_all_ddl_statements_incrementing ||
		yb_always_increment_catalog_version_on_ddl;
	bool		is_version_increment = should_increment_version_by_default;
	bool		is_breaking_change = true;
	bool		is_altering_existing_data = false;
	bool		should_run_in_autonomous_transaction = false;
	bool		is_top_level = (context == PROCESS_UTILITY_TOPLEVEL);

	Assert(requires_autonomous_transaction);
	*requires_autonomous_transaction = false;

	Node	   *parsetree = GetActualStmtNode(pstmt);
	NodeTag		node_tag = nodeTag(parsetree);

	/*
	 * During a major PG version upgrade, the logical state of the catalog is
	 * kept constant, and we're merely creating a new-major-version catalog
	 * that's semantically equivalent to the old-major version catalog. During
	 * this process we need to keep the catalog version constant, to avoid
	 * needing to refresh the catalog. This is safe because the only DDLs
	 * allowed are being performed by the new-major-version pg_restore process.
	 */
	if (IsBinaryUpgrade)
	{
		return (YbDdlModeOptional)
		{
			.has_value = true,
				.value = YbCatalogModificationAspectsToDdlMode(YB_DDL_MODE_NO_ALTERING),
		};
	}

	/*
	 * Note: REFRESH MATVIEW (CONCURRENTLY) executes subcommands using SPI.
	 * So, if the context is PROCESS_UTILITY_QUERY (command triggered using
	 * SPI), and the current original node tag is T_RefreshMatViewStmt, do
	 * not update the original node tag.
	 */
	if (YbIsTopLevelOrAtomicStatement(context))
	{
		/*
		 * The node tag from the top-level or atomic process utility must
		 * be persisted so that DDL commands with multiple nested
		 * subcommands can determine whether catalog version should
		 * be incremented.
		 */
		ddl_transaction_state.current_stmt_node_tag = node_tag;
		if (ddl_transaction_state.current_stmt_ddl_command_tag != CMDTAG_UNKNOWN)
			ddl_transaction_state.last_stmt_ddl_command_tag =
				ddl_transaction_state.current_stmt_ddl_command_tag;
		ddl_transaction_state.current_stmt_ddl_command_tag =
			CreateCommandTag(parsetree);
		if (CheckIsAnalyzeDDL() && !ddl_transaction_state.userid_and_sec_context.is_set)
		{
			Oid			save_userid;
			int			save_sec_context;

			GetUserIdAndSecContext(&save_userid, &save_sec_context);
			ddl_transaction_state.userid_and_sec_context.is_set = true;
			ddl_transaction_state.userid_and_sec_context.userid = save_userid;
			ddl_transaction_state.userid_and_sec_context.sec_context = save_sec_context;
		}
	}
	else
	{
		Assert(context == PROCESS_UTILITY_SUBCOMMAND ||
			   context == PROCESS_UTILITY_QUERY_NONATOMIC ||
			   (context == PROCESS_UTILITY_QUERY &&
				ddl_transaction_state.current_stmt_node_tag ==
				T_RefreshMatViewStmt));

		is_version_increment = false;
		is_breaking_change = false;
	}

	switch (node_tag)
	{
			/*
			 * The lists of tags here have been generated using e.g.:
			 * cat $( find src/postgres -name "nodes.h" ) | grep "T_Create" | sort | uniq |
			 *   sed 's/,//g' | while read s; do echo -e "\t\tcase $s:"; done
			 * All T_Create... tags from nodes.h:
			 */

		case T_YbCreateTableGroupStmt:
		case T_CreateTableSpaceStmt:
		case T_CreatedbStmt:
		case T_DefineStmt:		/* CREATE OPERATOR/AGGREGATE/COLLATION/etc */
		case T_CommentStmt:		/* COMMENT (create new comment) */
		case T_YbCreateProfileStmt:
			/*
			 * Simple add objects are not breaking changes, and they do not even require
			 * a version increment because we do not do any negative caching for them.
			 */
			is_version_increment = should_increment_version_by_default;
			is_breaking_change = false;
			break;

		case T_RuleStmt:		/* CREATE RULE */
			/*
			 * CREATE RULE sets relhasrules to true in the table's pg_class entry.
			 * We need to increment the catalog version to ensure this change is
			 * picked up by other backends.
			 */
			is_version_increment = true;
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
				is_version_increment = should_increment_version_by_default;
			is_breaking_change = false;
			break;

		case T_ViewStmt:		/* CREATE VIEW */
			is_breaking_change = false;

			/*
			 * For system catalog additions we need to force cache refresh
			 * because of negative caching of pg_class and pg_type
			 * (see SearchCatCacheMiss).
			 * Concurrent transaction needs not to be aborted though.
			 */
			if (IsYsqlUpgrade &&
				YbIsCatalogNamespaceByName(castNode(ViewStmt, parsetree)->view->schemaname))
				break;

			/* Create or replace view needs to increment catalog version. */
			if (!castNode(ViewStmt, parsetree)->replace)
				is_version_increment = should_increment_version_by_default;
			break;

		case T_CompositeTypeStmt:	/* Create (composite) type */
		case T_CreateAmStmt:
		case T_CreateCastStmt:
		case T_CreateConversionStmt:
		case T_CreateDomainStmt:	/* Create (domain) type */
		case T_CreateEnumStmt:	/* Create (enum) type */
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
		case T_CreateRangeStmt: /* Create (range) type */
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
				int			nopts = list_length(stmt->options);

				if (nopts == 0)
					is_version_increment = should_increment_version_by_default;
				else
				{
					bool		reference_other_role = false;
					ListCell   *lc;

					foreach(lc, stmt->options)
					{
						DefElem    *def = (DefElem *) lfirst(lc);

						if (strcmp(def->defname, "rolemembers") == 0 ||
							strcmp(def->defname, "adminmembers") == 0 ||
							strcmp(def->defname, "addroleto") == 0)
						{
							reference_other_role = true;
							break;
						}
					}
					if (!reference_other_role)
						is_version_increment = should_increment_version_by_default;
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
				 * Increment the catalog version for create inherited tables
				 * so that the corresponding cache can be invalidated
				 */
				if (stmt->inhRelations)
					break;

				/*
				 * For system catalog additions we need to force cache refresh
				 * because of negative caching of pg_class and pg_type
				 * (see SearchCatCacheMiss).
				 * Concurrent transaction needs not to be aborted though.
				 */
				if (IsYsqlUpgrade &&
					YbIsCatalogNamespaceByName(stmt->relation->schemaname))
				{
					/*
					 * Adding a shared relation is considered as having global
					 * impact. However when upgrading an old release, the function
					 * pg_catalog.yb_increment_all_db_catalog_versions may not
					 * exist yet, in this case YbSetIsGlobalDDL is not applicable.
					 */
					if (stmt->tablespacename &&
						strcmp(stmt->tablespacename, "pg_global") == 0 &&
						YBIsDBCatalogVersionMode())
					{
						Oid			func_oid = YbGetSQLIncrementCatalogVersionsFunctionOid();

						if (OidIsValid(func_oid))
							YbSetIsGlobalDDL();
					}
					break;
				}

				if (stmt->relation->relpersistence == RELPERSISTENCE_TEMP)
				{
					is_version_increment = false;
					is_altering_existing_data = true;
					YBMarkTxnUsesTempRelAndSetTxnId();
				}
				else
				{
					is_version_increment = should_increment_version_by_default;
				}
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
			is_version_increment = should_increment_version_by_default;
			is_breaking_change = false;
			break;

		case T_CreateSeqStmt:
			is_breaking_change = false;

			/*
			 * Need to increment if owner is set to ensure its dependency
			 * cache is updated.
			 */
			if (!OidIsValid(castNode(CreateSeqStmt, parsetree)->ownerId))
				is_version_increment = should_increment_version_by_default;
			break;

		case T_CreateFunctionStmt:
			is_breaking_change = false;
			break;

		case T_DiscardStmt:		/* DISCARD ALL/SEQUENCES/TEMP */
			/*
			 * This command alters existing data. But this update affects only
			 * objects of current connection. No version increment is required.
			 */
			is_breaking_change = false;
			is_version_increment = false;
			is_altering_existing_data = true;
			break;

			/* All T_Drop... tags from nodes.h: */
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
				if (ddl_transaction_state.current_stmt_node_tag == T_RefreshMatViewStmt)
					is_version_increment = false;
				else
				{
					/*
					 * If all dropped objects are temporary, we do not need to
					 * bump the catalog version.
					 */
					DropStmt   *stmt = castNode(DropStmt, parsetree);

				if (stmt->removeType == OBJECT_INDEX ||
					stmt->removeType == OBJECT_TABLE ||
					stmt->removeType == OBJECT_VIEW)
				{
					ListCell   *cell;

					is_version_increment = false;
					bool		marked_temp = false;

						foreach(cell, stmt->objects)
						{
							RangeVar   *rel = makeRangeVarFromNameList((List *) lfirst(cell));

							if (!YbIsRangeVarTempRelation(rel))
								is_version_increment = true;
							else if (!marked_temp)
							{
								marked_temp = true;
								YBMarkTxnUsesTempRelAndSetTxnId();
							}
							if (is_version_increment && marked_temp)
								break;
						}

						if (!is_version_increment)
							is_altering_existing_data = true;
					}
				}
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
				is_version_increment = should_increment_version_by_default;
			break;

			/* All T_Alter... tags from nodes.h: */
		case T_AlterCollationStmt:
		case T_AlterDatabaseRefreshCollStmt:
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
		case T_AlterStatsStmt:
		case T_AlterSubscriptionStmt:
		case T_AlterSystemStmt:
		case T_AlterTSConfigurationStmt:
		case T_AlterTSDictionaryStmt:
		case T_AlterTableCmd:
		case T_AlterTableMoveAllStmt:
		case T_AlterTableSpaceOptionsStmt:
		case T_AlterUserMappingStmt:
		case T_AlternativeSubPlan:
		case T_ReassignOwnedStmt:
		case T_AlterTypeStmt:
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
					DefElem    *def = (DefElem *) linitial(stmt->options);

					if (strcmp(def->defname, "password") == 0)
					{
						is_breaking_change = false;
						is_version_increment = should_increment_version_by_default;
					}
				}
				break;
			}

		case T_AlterTableStmt:
			{
				/*
				 * We rely on table schema version mismatch to abort
				 * transactions that touch the table.
				 */
				is_breaking_change = false;

				AlterTableStmt *stmt = castNode(AlterTableStmt, parsetree);

				if (YbIsRangeVarTempRelation(stmt->relation))
				{
					is_version_increment = false;
					is_altering_existing_data = true;
					YBMarkTxnUsesTempRelAndSetTxnId();
					break;
				}

				/*
				 * Must increment catalog version when creating table with foreign
				 * key reference and refresh PG cache on ongoing transactions.
				 */
				if ((context == PROCESS_UTILITY_SUBCOMMAND ||
					 context == PROCESS_UTILITY_QUERY_NONATOMIC) &&
					ddl_transaction_state.current_stmt_node_tag == T_CreateStmt &&
					node_tag == T_AlterTableStmt)
				{
					ListCell   *lcmd;

					foreach(lcmd, stmt->cmds)
					{
						AlterTableCmd *cmd = (AlterTableCmd *) lfirst(lcmd);

						if (cmd->def != NULL &&
							IsA(cmd->def, Constraint) &&
							((Constraint *) cmd->def)->contype == CONSTR_FOREIGN)
						{
							is_version_increment = true;
							break;
						}
					}
				}
				break;
			}

			/* ALTER .. RENAME TO syntax gets parsed into a T_RenameStmt node. */
		case T_RenameStmt:
			{
				const RenameStmt *const stmt = castNode(RenameStmt, parsetree);

				/*
				 * We don't have to increment the catalog version for renames
				 * on temp objects as they are only visible to the current
				 * session.
				 */
				if (stmt->relation && YbIsRangeVarTempRelation(stmt->relation))
				{
					is_breaking_change = false;
					is_version_increment = false;
					is_altering_existing_data = true;
					YBMarkTxnUsesTempRelAndSetTxnId();
				}
				break;
			}

			/* T_Grant... */
		case T_GrantStmt:
			/* Grant (add permission) is not a breaking change, but revoke is. */
			is_breaking_change = !castNode(GrantStmt, parsetree)->is_grant;
			break;

		case T_GrantRoleStmt:
			/* Grant (add permission) is not a breaking change, but revoke is. */
			is_breaking_change = !castNode(GrantRoleStmt, parsetree)->is_grant;
			break;

			/* T_Index... */
		case T_IndexStmt:
			{
				IndexStmt  *stmt = castNode(IndexStmt, parsetree);

				/*
				 * For nonconcurrent index backfill we do not guarantee global consistency anyway.
				 * For (new) concurrent backfill the backfill process should wait for ongoing
				 * transactions so we don't have to force a transaction abort on PG side.
				 */
				if (YbIsRangeVarTempRelation(stmt->relation))
				{
					is_version_increment = false;
					is_altering_existing_data = true;
					YBMarkTxnUsesTempRelAndSetTxnId();
				}
				is_breaking_change = false;
				should_run_in_autonomous_transaction = !IsInTransactionBlock(is_top_level) &&
						YBCIsLegacyModeForCatalogOps();
				break;
			}

		case T_VacuumStmt:
			{
				/*
				 * Four cases:
				 * 1. VACUUM: No-op in YugabyteDB, no catalog version increment.
				 * 2. ANALYZE: Increment catalog version.
				 * 3. VACUUM ANALYZE: Same as ANALYZE, increment catalog version.
				 * 4. ANALYZE via SPI (as part of concurrent MV refresh): No catalog
				 *    version increment.
				 */
				VacuumStmt *vacuum_stmt = castNode(VacuumStmt, parsetree);
				bool		is_analyze = false;
				bool		is_from_spi =
					(ddl_transaction_state.current_stmt_node_tag != T_VacuumStmt);

				is_breaking_change = false;
				is_version_increment = false;

				/* Case 2: ANALYZE */
				if (!vacuum_stmt->is_vacuumcmd)
					is_analyze = true;
				else
				{
					ListCell   *lc;
					foreach(lc, vacuum_stmt->options)
					{
						DefElem    *def_elem = lfirst_node(DefElem, lc);
						if (strcmp(def_elem->defname, "analyze") == 0)
						{
							/* Case 3: VACUUM ANALYZE */
							is_analyze = true;
							break;
						}
					}
				}

				is_ddl = is_analyze;

				/*
				 * Cases 2 & 3: Increment catalog version for ANALYZE to force
				 * catalog cache refresh for updated table statistics.
				 * Case 4: Skip increment if ANALYZE is being executed as part of
				 * concurrent MV refresh.
				 */
				if (is_analyze && !is_from_spi)
					is_version_increment = true;

				break;
			}

		case T_RefreshMatViewStmt:
			{
				RefreshMatViewStmt *stmt = castNode(RefreshMatViewStmt, parsetree);

				is_breaking_change = false;
				if (stmt->concurrent || YbRefreshMatviewInPlace())
				{
					/*
					 * REFRESH MATERIALIZED VIEW CONCURRENTLY does not need
					 * a catalog version increment as it does not alter any
					 * metadata. The command only performs data changes.
					 *
					 * In-place refresh forces the refresh to happen in a
					 * similar way.
					 */
					is_version_increment = false;
					/*
					 * REFRESH MATERIALIZED VIEW CONCURRENTLY uses temp tables
					 * which generates a PostgreSQL XID. Mark the transaction
					 * as such, so that it can be handled at commit time.
					 */
					YbSetTxnUsesTempRel();
				}
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
			is_version_increment = should_increment_version_by_default;
			is_breaking_change = false;
			break;

		case T_SecLabelStmt:
			/*
			 * This is related to defining or updating a security label on a
			 * database object, so this is a breaking change.
			 */
			break;

		case T_TransactionStmt:
			{
				TransactionStmt *stmt = castNode(TransactionStmt, parsetree);

				if (YBIsDdlTransactionBlockEnabled() && stmt->kind == TRANS_STMT_ROLLBACK_TO)
					++ddl_transaction_state.num_rollback_to_savepoint_stmts;
				/*
				 * We make a special case for YSQL upgrade, where we often use
				 * DML statements writing to catalog tables directly under the GUC
				 * yb_non_ddl_txn_for_sys_tables_allowed=1. These DML statements
				 * generate invalidation messages that if ignored can cause stale
				 * cache problem. We mark a COMMIT statement as ddl so that we
				 * can capture these DML-generated invalidation messages and send
				 * them to all PG backends on all the nodes.
				 */
				if (IsYsqlUpgrade &&
					stmt->kind == TRANS_STMT_COMMIT &&
				/*
				 * A COMMIT statement itself does not ensure a successful
				 * commit. If the current transaction is already aborted,
				 * it is equivalent to a ROLLBACK statement.
				 */
					IsTransactionState() &&
					YbIsInvalidationMessageEnabled())
				{
					/*
					 * We assume YSQL upgrade only makes simple use of COMMIT
					 * so that we can handle invalidation messages correctly.
					 */
					if (is_top_level)
						is_breaking_change = false;
					else
						elog(ERROR, "improper nesting level of COMMIT in YSQL upgrade");
				}
				else
					is_ddl = false;
				break;
			}

		default:
			/* Not a DDL operation. */
			is_ddl = false;
			break;
	}

	if (YbIsTopLevelOrAtomicStatement(context))
		ddl_transaction_state.is_top_level_ddl_active = is_ddl;

	if (!is_ddl)
	{
		/*
		 * Only clear up the DDL state if the previous statement used a separate
		 * DDL transaction.
		 */
		if (ddl_transaction_state.nesting_level == 0 &&
			!ddl_transaction_state.use_regular_txn_block)
			YBClearDdlTransactionState();
		else
			ddl_transaction_state.current_stmt_ddl_command_tag = CMDTAG_UNKNOWN;
		return (YbDdlModeOptional)
		{
		};
	}

	/*
	 * If yb_make_next_ddl_statement_nonbreaking is true, then no DDL statement
	 * will cause a breaking catalog change.
	 */
	if (yb_make_next_ddl_statement_nonbreaking)
		is_breaking_change = false;

	/*
	 * If yb_make_next_ddl_statement_nonincrementing is true, then no DDL statement
	 * will cause a catalog version to increment. Note that we also disable breaking
	 * catalog change as well because it does not make sense to only increment
	 * breaking breaking catalog version.
	 * If incremental catalog cache refresh is enabled, we ignore
	 * yb_make_next_ddl_statement_nonincrementing. Consider this example where
	 * each ddl statement increments the catalog version.
	 * BEGIN;
	 *   SET yb_make_next_ddl_statement_nonincrementing = true;
	 *   ddl_statement_1;
	 *   SET yb_make_next_ddl_statement_nonincrementing = true;
	 *   ddl_statement_2;
	 *   ddl_statement_3;
	 * END;
	 * In full refresh mode, ddl_statement_3 would have caused a full catalog cache
	 * refresh which will allow a PG backend to see the effect of ddl_statement_1
	 * and ddl_statement_2. In incremental refresh mode, ddl_statement_3 only causes
	 * an incremental catalog cache refresh and the effect of ddl_statement_1 and
	 * ddl_statement_2 are lost.
	 */
	if (yb_make_next_ddl_statement_nonincrementing &&
		!YbIsInvalidationMessageEnabled())
	{
		is_version_increment = false;
		is_breaking_change = false;
	}


	is_altering_existing_data |= is_version_increment;

	uint64_t	aspects = 0;

	if (is_altering_existing_data)
		aspects |= YB_SYS_CAT_MOD_ASPECT_ALTERING_EXISTING_DATA;

	if (is_version_increment)
		aspects |= YB_SYS_CAT_MOD_ASPECT_VERSION_INCREMENT;

	if (is_breaking_change)
		aspects |= YB_SYS_CAT_MOD_ASPECT_BREAKING_CHANGE;

	*requires_autonomous_transaction = YBIsDdlTransactionBlockEnabled() &&
		should_run_in_autonomous_transaction;

	if (!is_version_increment)
		ddl_transaction_state.current_stmt_ddl_command_tag = CMDTAG_UNKNOWN;

	return (YbDdlModeOptional)
	{
		.has_value = true,
			.value = YbCatalogModificationAspectsToDdlMode(aspects),
	};
}

static void
CheckAlterDatabaseDdl(PlannedStmt *pstmt)
{
	Node	   *const parsetree = GetActualStmtNode(pstmt);
	char	   *dbname = NULL;

	switch (nodeTag(parsetree))
	{
		case T_AlterDatabaseSetStmt:
			dbname = castNode(AlterDatabaseSetStmt, parsetree)->dbname;
			break;
		case T_AlterDatabaseStmt:
			dbname = castNode(AlterDatabaseStmt, parsetree)->dbname;
			break;
		case T_AlterDatabaseRefreshCollStmt:
			dbname = castNode(AlterDatabaseRefreshCollStmt, parsetree)->dbname;
			break;
		case T_RenameStmt:
			{
				const RenameStmt *const stmt = castNode(RenameStmt, parsetree);

				/*
				 * ALTER DATABASE RENAME needs to have global impact. In global
				 * catalog version mode is_global_ddl does not apply so it is
				 * not turned on.
				 */
				if (stmt->renameType == OBJECT_DATABASE)
					Assert(ddl_transaction_state.is_global_ddl ||
						   !YBIsDBCatalogVersionMode());
				break;
			}
		case T_AlterOwnerStmt:
			{
				const AlterOwnerStmt *const stmt = castNode(AlterOwnerStmt, parsetree);

				/*
				 * ALTER DATABASE OWNER needs to have global impact, however we
				 * may have a no-op ALTER DATABASE OWNER when the new owner is the
				 * same as the old owner and there is no write made to pg_database
				 * to turn on is_global_ddl. Also in global catalog version mode
				 * is_global_ddl does not apply so it is not turned on either.
				 */
				if (stmt->objectType == OBJECT_DATABASE)
					Assert(ddl_transaction_state.is_global_ddl ||
						   !YBCPgHasWriteOperationsInDdlTxnMode() ||
						   !YBIsDBCatalogVersionMode());
				break;
			}
		default:
			break;
	}
	if (dbname)
	{
		/*
		 * Some ALTER DATABASE statements do not need to be a global impact DDL,
		 * they only need to increment the catalog version of the database that
		 * is altered, which may not be the same as MyDatabaseId.
		 */
		ddl_transaction_state.database_oid = get_database_oid(dbname, false);
		ddl_transaction_state.is_global_ddl = false;
	}
	else
		ddl_transaction_state.database_oid = InvalidOid;
}

static void
YBTxnDdlProcessUtility(PlannedStmt *pstmt,
					   const char *queryString,
					   bool readOnlyTree,
					   ProcessUtilityContext context,
					   ParamListInfo params,
					   QueryEnvironment *queryEnv,
					   DestReceiver *dest,
					   QueryCompletion *qc)
{

	bool		should_run_in_autonomous_transaction = false;
	const YbDdlModeOptional ddl_mode =
		YbGetDdlMode(pstmt, context, &should_run_in_autonomous_transaction);

	const bool	is_ddl = ddl_mode.has_value;

	/*
	 * Start a separate DDL transaction if
	 * - yb_ddl_transaction_block_enabled is false or
	 * - If we were asked to by YbGetDdlMode. Currently, only done for
	 * CREATE INDEX outside of explicit transaction block.
	 */
	const bool	use_separate_ddl_transaction =
		is_ddl && (should_run_in_autonomous_transaction ||
				   !YBIsDdlTransactionBlockEnabled());

	elog(DEBUG3, "is_ddl %d", is_ddl);
	PG_TRY();
	{
		if (is_ddl)
		{
#ifdef YB_TODO					/* utils/syscache.h has
								 * YbInitPinnedCacheIfNeeded removed. */
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
				YbInitPinnedCacheIfNeeded(true /* shared_only */ );
#endif

			if (use_separate_ddl_transaction)
				YBIncrementDdlNestingLevel(ddl_mode.value);
			else
			{
				/*
				 * Disallow DDL if savepoint for DDL support is disabled and
				 * there is an active savepoint except the implicit ones created
				 * for READ COMMITTED isolation.
				 */
				if (!(yb_enable_ddl_savepoint_infra &&
					  *YBCGetGFlags()->ysql_yb_enable_ddl_savepoint_support) &&
					YBTransactionContainsNonReadCommittedSavepoint())
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("interleaving SAVEPOINT & DDL in transaction"
									" disallowed without DDL savepoint support"),
							 errhint("Consider enabling ysql_yb_enable_ddl_savepoint_support.")));

				YBAddDdlTxnState(ddl_mode.value);
			}

			if (YbShouldIncrementLogicalClientVersion(pstmt) &&
				YbIsClientYsqlConnMgr() &&
				YbIncrementMasterLogicalClientVersionTableEntry())
				elog(LOG, "Logical client version incremented");
		}

		if (prev_ProcessUtility)
			prev_ProcessUtility(pstmt, queryString, readOnlyTree,
								context, params, queryEnv,
								dest, qc);
		else
			standard_ProcessUtility(pstmt, queryString, readOnlyTree,
									context, params, queryEnv,
									dest, qc);

		/*
		 * YB: Account for stats collected during the execution of utility command
		 * Only refresh stats at the last level of nesting
		 */
		if (YBGetDdlNestingLevel() == 1)
			YbRefreshSessionStatsDuringExecution();

		if (is_ddl)
		{
			CheckAlterDatabaseDdl(pstmt);

			if (use_separate_ddl_transaction)
				YBDecrementDdlNestingLevel();
			else
				YBMergeDdlTxnState();

			/*
			 * Reset the is_top_level_ddl_active for this statement as it is
			 * done executing. This field is set in YbGetDdlMode when we detect
			 * a DDL statement. We need to the unset it after the DDL statement
			 * is done and cannot wait for it to be unset upon receiving the
			 * next statement. This is because not all statements (for eg. DMLs)
			 * call YBTxnDdlProcessUtility and hence YbGetDdlMode.
			 */
			if (YbIsTopLevelOrAtomicStatement(context))
				ddl_transaction_state.is_top_level_ddl_active = false;
		}
	}
	PG_CATCH();
	{
		if (use_separate_ddl_transaction)
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

static void
YBCInstallTxnDdlHook()
{
	if (!YBCIsInitDbModeEnvVarSet())
	{
		prev_ProcessUtility = ProcessUtility_hook;
		ProcessUtility_hook = YBTxnDdlProcessUtility;
	}
};

/*
 * Used in YB to re-invalidate table cache entries either at the end of:
 * a) Transaction if DDL + DML transaction support is enabled and the
 *    transaction included an ALTER TABLE operation.
 * b) ALTER TABLE operation:
 *    1. Phase 3 scan/rewrite tables.
 *    2. Any failures during the ALTER TABLE operation, if DDL + DML transaction
 *       support is disabled.
 * c) ROLLBACK TO SAVEPOINT operation if DDL savepoint support is enabled and
 *    transaction block included an ALTER TABLE operation.
 */
void
YbInvalidateTableCacheForAlteredTables()
{
	if ((YbDdlRollbackEnabled() ||
		 YBIsDdlTransactionBlockEnabled()) &&
		ddl_transaction_state.altered_table_ids)
	{
		/*
		 * As part of DDL transaction verification, we may have incremented
		 * the schema version for the affected tables. So, re-invalidate
		 * the table cache entries of the affected tables.
		 */
		ListCell   *lc = NULL;

		foreach(lc, ddl_transaction_state.altered_table_ids)
		{
			YbDatabaseAndRelfileNodeOid *object_id =
				(YbDatabaseAndRelfileNodeOid *) lfirst(lc);

			/*
			 * This is safe to do even for tables which don't exist or have
			 * already been invalidated because this is just a deletion/marking
			 * in an in-memory map.
			 */
			YBCPgAlterTableInvalidateTableByOid(object_id->database_oid,
												object_id->relfilenode_id);
		}
	}
}

static unsigned int buffering_nesting_level = 0;

void
YBBeginOperationsBuffering()
{
	if (++buffering_nesting_level == 1)
	{
		HandleYBStatus(YBCPgStartOperationsBuffering());
	}
}

void
YBEndOperationsBuffering()
{
	/*
	 * buffering_nesting_level could be 0 because YBResetOperationsBuffering was
	 * called on starting new query and postgres calls standard_ExecutorFinish
	 * on non finished executor from previous failed query.
	 */
	if (buffering_nesting_level && !--buffering_nesting_level)
	{
		HandleYBStatus(YBCPgStopOperationsBuffering());
	}
}

void
YBResetOperationsBuffering()
{
	buffering_nesting_level = 0;
	YBCPgResetOperationsBuffering();
}

void
YBFlushBufferedOperations(YbcFlushDebugContext debug_context)
{
	HandleYBStatus(YBCPgFlushBufferedOperations(&debug_context));
}

bool
YBEnableTracing()
{
	return yb_enable_docdb_tracing;
}

void
YBAdjustOperationsBuffering(int multiple)
{
	HandleYBStatus(YBCPgAdjustOperationsBuffering(multiple));
}

bool
YBReadFromFollowersEnabled()
{
	return yb_read_from_followers;
}

bool
YBFollowerReadsBehaviorBefore20482()
{
	return yb_follower_reads_behavior_before_fixing_20482;
}

int32_t
YBFollowerReadStalenessMs()
{
	return yb_follower_read_staleness_ms;
}

YbcPgYBTupleIdDescriptor *
YBCCreateYBTupleIdDescriptor(Oid db_oid, Oid table_relfilenode_oid, int nattrs)
{
	void	   *mem = palloc(sizeof(YbcPgYBTupleIdDescriptor) + nattrs * sizeof(YbcPgAttrValueDescriptor));
	YbcPgYBTupleIdDescriptor *result = mem;

	result->nattrs = nattrs;
	result->attrs = mem + sizeof(YbcPgYBTupleIdDescriptor);
	result->database_oid = db_oid;
	result->table_relfilenode_oid = table_relfilenode_oid;
	return result;
}

void
YBCFillUniqueIndexNullAttribute(YbcPgYBTupleIdDescriptor *descr)
{
	YbcPgAttrValueDescriptor *last_attr = descr->attrs + descr->nattrs - 1;

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
YbTestGucBlockWhileIntNotEqual(int *actual, int expected,
							const char *msg)
{
	static const int kSpinWaitMs = 100;

	while (*actual != expected)
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
	int			ncols = 0;		/* Equals to the number of OUT arguments. */

	HeapTuple	proctup = SearchSysCache1(PROCOID, ObjectIdGetDatum(func_oid));

	if (!HeapTupleIsValid(proctup))
		elog(ERROR, "cache lookup failed for function %u", func_oid);

	bool		is_null = false;
	Datum		proargmodes = SysCacheGetAttr(PROCOID, proctup,
											  Anum_pg_proc_proargmodes,
											  &is_null);

	Assert(!is_null);
	ArrayType  *proargmodes_arr = DatumGetArrayTypeP(proargmodes);

	ncols = 0;
	for (int i = 0; i < ARR_DIMS(proargmodes_arr)[0]; ++i)
		if (ARR_DATA_PTR(proargmodes_arr)[i] == PROARGMODE_OUT)
			++ncols;

	ReleaseSysCache(proctup);

	return ncols;
}

int
YbGetNumberOfFunctionInputParameters(Oid func_oid)
{
	int			nargs = 0;		/* Equals to the number of IN parameters. */

	HeapTuple	proctup = SearchSysCache1(PROCOID, ObjectIdGetDatum(func_oid));

	if (!HeapTupleIsValid(proctup))
		elog(ERROR, "cache lookup failed for function %u", func_oid);

	bool		is_null = false;
	Datum		pronargs = SysCacheGetAttr(PROCOID, proctup,
											  Anum_pg_proc_pronargs,
											  &is_null);

	Assert(!is_null);
	nargs = DatumGetInt16(pronargs);

	ReleaseSysCache(proctup);

	return nargs;
}

/*
 * For backward compatibility, this function dynamically adapts to the number
 * of output columns defined in pg_proc.
 */
Datum
yb_servers(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;

	static int	ncols = 0;

#define YB_SERVERS_COLS_V1 8
#define YB_SERVERS_COLS_V2 9
#define YB_SERVERS_COLS_V3 10

	if (ncols < YB_SERVERS_COLS_V3)
		ncols = YbGetNumberOfFunctionOutputColumns(F_YB_SERVERS);

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;
		TupleDesc	tupdesc;

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
		tupdesc = CreateTemplateTupleDesc(ncols);

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

		if (ncols >= YB_SERVERS_COLS_V2)
			TupleDescInitEntry(tupdesc, (AttrNumber) 9,
							   "uuid", TEXTOID, -1, 0);

		if (ncols >= YB_SERVERS_COLS_V3)
			TupleDescInitEntry(tupdesc, (AttrNumber) 10,
							   "universe_uuid", TEXTOID, -1, 0);
		funcctx->tuple_desc = BlessTupleDesc(tupdesc);

		YbcServerDescriptor *servers = NULL;
		size_t		numservers = 0;

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

		int			cntr = funcctx->call_cntr;
		YbcServerDescriptor *server = (YbcServerDescriptor *) funcctx->user_fctx + cntr;
		bool		is_primary = server->is_primary;
		const char *node_type = is_primary ? "primary" : "read_replica";

		/* TODO: Remove hard coding of port and num_connections */
		values[0] = CStringGetTextDatum(server->host);
		values[1] = Int64GetDatum(server->pg_port);
		values[2] = Int64GetDatum(0);
		values[3] = CStringGetTextDatum(node_type);
		values[4] = CStringGetTextDatum(server->cloud);
		values[5] = CStringGetTextDatum(server->region);
		values[6] = CStringGetTextDatum(server->zone);
		values[7] = CStringGetTextDatum(server->public_ip);

		if (ncols >= YB_SERVERS_COLS_V2)
			values[8] = CStringGetTextDatum(server->uuid);

		if (ncols >= YB_SERVERS_COLS_V3)
			values[9] = CStringGetTextDatum(server->universe_uuid);
		memset(nulls, 0, sizeof(nulls));
		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);
		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
	}
	else
		SRF_RETURN_DONE(funcctx);

#undef YB_SERVERS_COLS_V1
#undef YB_SERVERS_COLS_V2
#undef YB_SERVERS_COLS_V3

}

bool
YbIsUtf8Locale(const char *localebuf)
{
	return strcasecmp(localebuf, "en_US.utf8") == 0 ||
		strcasecmp(localebuf, "en_US.UTF-8") == 0;
}

bool
YbIsCLocale(const char *localebuf)
{
	return strcasecmp(localebuf, "C") == 0 ||
		strcasecmp(localebuf, "POSIX") == 0;
}

bool
YBIsSupportedLibcLocale(const char *localebuf)
{
	/*
	 * For libc mode, Yugabyte only supports the basic locales.
	 */
	return YbIsCLocale(localebuf) || YbIsUtf8Locale(localebuf);
}

void
YbCheckUnsupportedLibcLocale(const char *localebuf)
{
	if (IsYugaByteEnabled() && !YBIsSupportedLibcLocale(localebuf))
	{
		char	   *locale = pstrdup(localebuf);

		if (yb_test_collation)
		{
			/*
			 * For testing to be stable across linux and mac, normalize
			 * the locale name.
			 */
			char	   *utf8 = strstr(locale, "UTF-8");

			if (utf8)
				strcpy(utf8, "utf8");
		}
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("unsupprted locale name: \"%s\"", locale)));
	}
}

static YbcStatus
YbGetTablePropertiesCommon(Relation rel)
{
	if (rel->yb_table_properties)
	{
		/* Already loaded, nothing to do */
		return NULL;
	}

	Oid			dbid = YBCGetDatabaseOid(rel);
	Oid			relfileNodeId = YbGetRelfileNodeId(rel);

	YbcPgTableDesc desc = NULL;
	YbcStatus	status = YBCPgGetTableDesc(dbid, relfileNodeId, &desc);

	if (status)
		return status;

	/* Relcache entry data must live in CacheMemoryContext */
	rel->yb_table_properties =
		MemoryContextAllocZero(CacheMemoryContext, sizeof(YbcTablePropertiesData));

	return YBCPgGetTableProperties(desc, rel->yb_table_properties);
}

YbcTableProperties
YbGetTableProperties(Relation rel)
{
	HandleYBStatus(YbGetTablePropertiesCommon(rel));
	return rel->yb_table_properties;
}

YbcTableProperties
YbGetTablePropertiesById(Oid relid)
{
	Relation	relation = RelationIdGetRelation(relid);

	HandleYBStatus(YbGetTablePropertiesCommon(relation));
	RelationClose(relation);
	return relation->yb_table_properties;
}

YbcTableProperties
YbTryGetTableProperties(Relation rel)
{
	bool		not_found = false;

	HandleYBStatusIgnoreNotFound(YbGetTablePropertiesCommon(rel), &not_found);
	return not_found ? NULL : rel->yb_table_properties;
}

YbTableDistribution
YbGetTableDistribution(Oid relid)
{
	YbTableDistribution result;
	Relation	relation = RelationIdGetRelation(relid);

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
	char	   *arg_buf;

	size_t		size = 0;

	for (int i = 0; i < PG_NARGS(); i++)
	{
		Oid			argtype = get_fn_expr_argtype(fcinfo->flinfo, i);

		if (unlikely(argtype == UNKNOWNOID))
		{
			ereport(ERROR,
					(errcode(ERRCODE_INDETERMINATE_DATATYPE),
					 errmsg("undefined datatype given to yb_hash_code")));
			PG_RETURN_NULL();
		}

		size_t		typesize;
		const YbcPgTypeEntity *typeentity = YbDataTypeFromOidMod(InvalidAttrNumber,
																 argtype);
		YbcStatus	status = YBCGetDocDBKeySize(PG_GETARG_DATUM(i), typeentity,
												PG_ARGISNULL(i), &typesize);

		if (unlikely(status))
		{
			YBCFreeStatus(status);
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("unsupported datatype given to yb_hash_code"),
					 errdetail("Only types supported by HASH key columns are allowed."),
					 errhint("Use explicit casts to ensure input types are as desired.")));
			PG_RETURN_NULL();
		}
		size += typesize;
	}

	arg_buf = alloca(size);

	/* TODO(Tanuj): Look into caching the above buffer */

	char	   *arg_buf_pos = arg_buf;

	size_t		total_bytes = 0;

	for (int i = 0; i < PG_NARGS(); i++)
	{
		Oid			argtype = get_fn_expr_argtype(fcinfo->flinfo, i);
		const YbcPgTypeEntity *typeentity = YbDataTypeFromOidMod(InvalidAttrNumber,
																 argtype);
		size_t		written;
		YbcStatus	status = YBCAppendDatumToKey(PG_GETARG_DATUM(i),
												 typeentity,
												 PG_ARGISNULL(i), arg_buf_pos,
												 &written);

		if (unlikely(status))
		{
			YBCFreeStatus(status);
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("unsupported datatype given to yb_hash_code"),
					 errdetail("Only types supported by HASH key columns are allowed."),
					 errhint("Use explicit casts to ensure input types are as desired.")));
			PG_RETURN_NULL();
		}
		arg_buf_pos += written;

		total_bytes += written;
	}

	/* hash the contents of the buffer and return */
	uint16_t	hashed_val = YBCCompoundHash(arg_buf, total_bytes);

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

	int			expected_ncols = 5;

	static int	ncols = 0;

	if (ncols < expected_ncols)
		ncols = YbGetNumberOfFunctionOutputColumns(8033);	/* yb_table_properties
															 * function oid
															 * hardcoded in
															 * pg_proc.dat */

	Datum		values[ncols];
	bool		nulls[ncols];

	Relation	rel = relation_open(relid, AccessShareLock);
	Oid			dbid = YBCGetDatabaseOid(rel);
	Oid			relfileNodeId = YbGetRelfileNodeId(rel);

	YbcPgTableDesc yb_tabledesc = NULL;
	YbcTablePropertiesData yb_table_properties;
	bool		not_found = false;

	HandleYBStatusIgnoreNotFound(YBCPgGetTableDesc(dbid, relfileNodeId,
												   &yb_tabledesc),
								 &not_found);
	if (!not_found)
		HandleYBStatusIgnoreNotFound(YBCPgGetTableProperties(yb_tabledesc,
															 &yb_table_properties),
									 &not_found);

	tupdesc = CreateTemplateTupleDesc(ncols);
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
		YbcTableProperties yb_props = &yb_table_properties;

		values[0] = Int64GetDatum(yb_props->num_tablets);
		values[1] = Int64GetDatum(yb_props->num_hash_key_columns);
		values[2] = BoolGetDatum(yb_props->is_colocated);
		if (ncols >= expected_ncols)
		{
			values[3] = (OidIsValid(yb_props->tablegroup_oid) ?
						 ObjectIdGetDatum(yb_props->tablegroup_oid) :
						 (Datum) 0);
			values[4] = (OidIsValid(yb_props->colocation_id) ?
						 ObjectIdGetDatum(yb_props->colocation_id) :
						 (Datum) 0);
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

Datum
yb_database_clones(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	int			i;
#define YB_DATABASE_CLONES_COLS 7

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

	YbcPgDatabaseCloneInfo *database_clones_info = NULL;
	size_t		num_clones = 0;

	HandleYBStatus(YBCDatabaseClones(&database_clones_info, &num_clones));

	for (i = 0; i < num_clones; ++i)
	{
		YbcPgDatabaseCloneInfo *clone_info = (YbcPgDatabaseCloneInfo *) database_clones_info + i;
		Datum		values[YB_DATABASE_CLONES_COLS];
		bool		nulls[YB_DATABASE_CLONES_COLS];

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));


		values[1] = CStringGetTextDatum(clone_info->db_name);
		values[2] = ObjectIdGetDatum(clone_info->parent_db_id);
		values[3] = CStringGetTextDatum(clone_info->parent_db_name);
		values[4] = CStringGetTextDatum(clone_info->state);
		values[5] = TimestampTzGetDatum(clone_info->as_of_time);

		/* Optional values that are not set all the time */
		if (OidIsValid(clone_info->db_id))
		{
			values[0] = ObjectIdGetDatum(clone_info->db_id);
		}
		else
			nulls[0] = true;

		if (strlen(clone_info->failure_reason))
		{
			values[6] = CStringGetTextDatum(clone_info->failure_reason);
		}
		else
			nulls[6] = true;

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

#undef YB_DATABASE_CLONES_COLS

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	MemoryContextSwitchTo(oldcontext);

	return (Datum) 0;
}

/* This function caches the local tserver's uuid locally */
const unsigned char *
YbGetLocalTServerUuid()
{
	static const unsigned char *local_tserver_uuid = NULL;

	if (!local_tserver_uuid && IsYugaByteEnabled())
		local_tserver_uuid = YBCGetLocalTserverUuid();

	return local_tserver_uuid;
}

Datum
yb_get_local_tserver_uuid(PG_FUNCTION_ARGS)
{
	pg_uuid_t *uuid = (pg_uuid_t *) palloc(UUID_LEN);
	memcpy(uuid->data, YbGetLocalTServerUuid(), UUID_LEN);
	return UUIDPGetDatum(uuid);
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
	int			len = strlen(str);
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
				int			charlen = pg_encoding_mblen(encoding, s);

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
getSplitPointsInfo(Oid relid, YbcPgTableDesc yb_tabledesc,
				   YbcTableProperties yb_table_properties,
				   Oid *pkeys_atttypid,
				   YbcPgSplitDatum *split_datums,
				   bool *has_null, bool *has_gin_null)
{
	Assert(yb_table_properties->num_tablets > 1);

	size_t		num_range_key_columns = yb_table_properties->num_range_key_columns;
	const YbcPgTypeEntity *type_entities[num_range_key_columns];
	YbcPgTypeAttrs type_attrs_arr[num_range_key_columns];

	/*
	 * Get key columns' YbcPgTypeEntity and YbcPgTypeAttrs.
	 * For range-partitioned tables, use primary key to get key columns' type
	 * info. For range-partitioned indexes, get key columns' type info from
	 * indexes themselves.
	 */
	Relation	rel = relation_open(relid, AccessShareLock);
	bool		is_table = rel->rd_rel->relkind == RELKIND_RELATION ||
		rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE;
	Relation	index_rel = (is_table ?
							 relation_open(RelationGetPrimaryKeyIndex(rel),
										   AccessShareLock) :
							 rel);
	Form_pg_index rd_index = index_rel->rd_index;
	TupleDesc	tupledesc = rel->rd_att;

	for (int i = 0; i < rd_index->indnkeyatts; ++i)
	{
		Form_pg_attribute attr = TupleDescAttr(tupledesc,
											   (is_table ?
												rd_index->indkey.values[i] - 1 :
												i));

		type_entities[i] = YbDataTypeFromOidMod(InvalidAttrNumber,
												attr->atttypid);
		YbcPgTypeAttrs type_attrs;

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
rangeSplitClause(Oid relid, YbcPgTableDesc yb_tabledesc,
				 YbcTableProperties yb_table_properties, StringInfo str)
{
	Assert(!str->len);
	Assert(yb_table_properties->num_tablets > 1);
	size_t		num_range_key_columns = yb_table_properties->num_range_key_columns;
	size_t		num_splits = yb_table_properties->num_tablets - 1;
	Oid			pkeys_atttypid[num_range_key_columns];
	YbcPgSplitDatum split_datums[num_splits * num_range_key_columns];
	StringInfo	prev_split_point = makeStringInfo();
	StringInfo	cur_split_point = makeStringInfo();
	bool		has_null = false;
	bool		has_gin_null = false;

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
			int			split_datum_idx = split_idx * num_range_key_columns + col_idx;

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
									false /* use_double_quotes */ );
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
 * as a list of list of Exprs.
 */
static void
getRangeSplitPointsList(Oid relid, YbcPgTableDesc yb_tabledesc,
						YbcTableProperties yb_table_properties,
						List **split_points)
{
	Assert(yb_table_properties->num_tablets > 1);
	size_t		num_range_key_columns = yb_table_properties->num_range_key_columns;
	size_t		num_splits = yb_table_properties->num_tablets - 1;
	Oid			pkeys_atttypid[num_range_key_columns];
	YbcPgSplitDatum split_datums[num_splits * num_range_key_columns];
	bool		has_null;
	bool		has_gin_null;

	/* Get Split point values as YbcPgSplitDatum. */
	getSplitPointsInfo(relid, yb_tabledesc, yb_table_properties,
					   pkeys_atttypid, split_datums, &has_null, &has_gin_null);

	/* Construct split points list. */
	for (int split_idx = 0; split_idx < num_splits; ++split_idx)
	{
		List	   *split_point = NIL;

		for (int col_idx = 0; col_idx < num_range_key_columns; ++col_idx)
		{
			int			split_datum_idx = split_idx * num_range_key_columns + col_idx;

			switch (split_datums[split_datum_idx].datum_kind)
			{
					ColumnRef  *c;
					StringInfo	str;

				case YB_YQL_DATUM_LIMIT_MIN:
					c = makeNode(ColumnRef);
					c->fields = list_make1(makeString("minvalue"));
					split_point = lappend(split_point, c);
					break;
				case YB_YQL_DATUM_LIMIT_MAX:
					c = makeNode(ColumnRef);
					c->fields = list_make1(makeString("maxvalue"));
					split_point = lappend(split_point, c);
					break;
				default:
					str = makeStringInfo();
					appendDatumToString(str,
										split_datums[split_datum_idx].datum,
										pkeys_atttypid[col_idx],
										pg_get_client_encoding(),
										true /* use_double_quotes */ );
					Node	   *value = nodeRead(str->data, str->len);
					A_Const    *n = makeNode(A_Const);

					switch (value->type)
					{
						case T_Integer:
							n->val.ival = *((Integer *) value);
							break;
						case T_Float:
							n->val.fval = *((Float *) value);
							break;
						case T_Boolean:
							n->val.boolval = *((Boolean *) value);
							break;
						case T_String:
							n->val.sval = *((String *) value);
							break;
						case T_BitString:
							n->val.bsval = *((BitString *) value);
							break;
						default:
							ereport(ERROR,
									(errmsg("unexpected node type %d",
											value->type)));
					}
					split_point = lappend(split_point, n);
			}
		}
		*split_points = lappend(*split_points, split_point);
	}
}

Datum
yb_get_range_split_clause(PG_FUNCTION_ARGS)
{
	Oid			relid = PG_GETARG_OID(0);
	bool		exists_in_yb = false;
	YbcPgTableDesc yb_tabledesc = NULL;
	YbcTablePropertiesData yb_table_properties;
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

const char *
yb_fetch_current_transaction_priority(void)
{
	YbcTxnPriorityRequirement txn_priority_type;
	double		txn_priority;
	static char buf[50];

	txn_priority_type = YBCGetTransactionPriorityType();
	txn_priority = YBCGetTransactionPriority();

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
	pg_uuid_t  *txn_id = NULL;
	bool		is_null = false;

	if (!yb_enable_pg_locks)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						errmsg("get_current_transaction is unavailable"),
						errdetail("yb_enable_pg_locks is false or a system "
								  "upgrade is in progress")));
	}

	txn_id = (pg_uuid_t *) palloc(sizeof(pg_uuid_t));
	HandleYBStatus(YBCPgGetSelfActiveTransaction((YbcPgUuid *) txn_id,
												 &is_null));

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

	pg_uuid_t  *id = PG_GETARG_UUID_P(0);
	YbcStatus	status = YBCPgCancelTransaction(id->data);

	if (status)
	{
		ereport(NOTICE,
				(errmsg("failed to cancel transaction"),
				 errdetail("%s", YBCMessageAsCString(status))));
		YBCFreeStatus(status);
		PG_RETURN_BOOL(false);
	}
	YBCFreeStatus(status);
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
	Oid			tableOid = PG_GETARG_OID(0);

	/* Fetch required info about the relation */
	Relation	relation = relation_open(tableOid, NoLock);
	Oid			tablespaceId = relation->rd_rel->reltablespace;
	bool		isTempTable = (relation->rd_rel->relpersistence ==
							   RELPERSISTENCE_TEMP);

	RelationClose(relation);

	/* Temp tables are local. */
	if (isTempTable)
	{
		PG_RETURN_BOOL(true);
	}
	YbGeolocationDistance distance = get_tablespace_distance(tablespaceId);

	PG_RETURN_BOOL(distance == REGION_LOCAL || distance == ZONE_LOCAL);
}

Datum
yb_server_region(PG_FUNCTION_ARGS)
{
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
	static int	ncols = 0;

#define YB_TABLET_INFO_COLS_V1 8
#define YB_TABLET_INFO_COLS_V2 9

	if (ncols < YB_TABLET_INFO_COLS_V2)
		ncols = YbGetNumberOfFunctionOutputColumns(F_YB_LOCAL_TABLETS);

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

	YbcPgLocalTabletsDescriptor *tablets = NULL;
	size_t		num_tablets = 0;

	HandleYBStatus(YBCLocalTablets(&tablets, &num_tablets));

	for (i = 0; i < num_tablets; ++i)
	{
		YbcPgLocalTabletsDescriptor *tablet = (YbcPgLocalTabletsDescriptor *) tablets + i;
		YbcPgTabletsDescriptor *tablet_descriptor = &tablet->tablet_descriptor;
		Datum		values[ncols];
		bool		nulls[ncols];
		bytea	   *partition_key_start;
		bytea	   *partition_key_end;

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		values[0] = CStringGetTextDatum(tablet_descriptor->tablet_id);
		values[1] = CStringGetTextDatum(tablet_descriptor->table_id);
		values[2] = CStringGetTextDatum(tablet_descriptor->table_type);
		values[3] = CStringGetTextDatum(tablet_descriptor->namespace_name);
		values[4] = CStringGetTextDatum(tablet->pgschema_name);
		values[5] = CStringGetTextDatum(tablet_descriptor->table_name);

		if (tablet_descriptor->partition_key_start_len)
		{
			partition_key_start = bytesToBytea(tablet_descriptor->partition_key_start,
											   tablet_descriptor->partition_key_start_len);
			values[6] = PointerGetDatum(partition_key_start);
		}
		else
			nulls[6] = true;

		if (tablet_descriptor->partition_key_end_len)
		{
			partition_key_end = bytesToBytea(tablet_descriptor->partition_key_end,
											 tablet_descriptor->partition_key_end_len);
			values[7] = PointerGetDatum(partition_key_end);
		}
		else
			nulls[7] = true;

		if (ncols >= YB_TABLET_INFO_COLS_V2)
			values[8] = CStringGetTextDatum(tablet->tablet_data_state);

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

#undef YB_TABLET_INFO_COLS_V1
#undef YB_TABLET_INFO_COLS_V2

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	MemoryContextSwitchTo(oldcontext);

	return (Datum) 0;
}

static Datum
GetMetricsAsJsonbDatum(YbcMetricsInfo *metrics, size_t metricsCount)
{
	JsonbParseState *state = NULL;
	JsonbValue	result;
	JsonbValue	key;
	JsonbValue	value;

	pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);
	for (int j = 0; j < metricsCount; j++)
	{
		key.type = jbvString;
		key.val.string.val = (char *) metrics[j].name;
		key.val.string.len = strlen(metrics[j].name);
		pushJsonbValue(&state, WJB_KEY, &key);

		value.type = jbvString;
		value.val.string.val = (char *) metrics[j].value;
		value.val.string.len = strlen(metrics[j].value);
		pushJsonbValue(&state, WJB_VALUE, &value);
	}
	result = *pushJsonbValue(&state, WJB_END_OBJECT, NULL);
	Jsonb	   *jsonb = JsonbValueToJsonb(&result);

	return JsonbPGetDatum(jsonb);
}

Datum
yb_servers_metrics(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	int			i;
#define YB_SERVERS_METRICS_COLS 4

	/* only superuser and yb_db_admin can query this function */
	if (!superuser() && !IsYbDbAdminUser(GetUserId()))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 (errmsg("only superusers and yb_db_admin can query yb_servers_metrics"))));

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

	YbcPgServerMetricsInfo *servers_metrics_info = NULL;
	size_t		num_servers = 0;

	HandleYBStatus(YBCServersMetrics(&servers_metrics_info, &num_servers));

	for (i = 0; i < num_servers; ++i)
	{
		YbcPgServerMetricsInfo *metricsInfo = (YbcPgServerMetricsInfo *) servers_metrics_info + i;
		Datum		values[YB_SERVERS_METRICS_COLS];
		bool		nulls[YB_SERVERS_METRICS_COLS];

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		values[0] = CStringGetTextDatum(metricsInfo->uuid);
		values[1] = GetMetricsAsJsonbDatum(metricsInfo->metrics, metricsInfo->metrics_count);
		values[2] = CStringGetTextDatum(metricsInfo->status);
		values[3] = CStringGetTextDatum(metricsInfo->error);

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

#undef YB_SERVERS_METRICS_COLS

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	MemoryContextSwitchTo(oldcontext);

	return (Datum) 0;
}

/*---------------------------------------------------------------------------*/
/* Deterministic DETAIL order                                                */
/*---------------------------------------------------------------------------*/

static int
yb_detail_sort_comparator(const void *a, const void *b)
{
	return strcmp(*(const char **) a, *(const char **) b);
}

typedef struct
{
	char	  **lines;
	int			length;
} YbDetailSorter;

void
detail_sorter_from_list(YbDetailSorter *v, List *litems, int capacity)
{
	v->lines = (char **) palloc(sizeof(char *) * capacity);
	v->length = 0;
	ListCell   *lc;

	foreach(lc, litems)
	{
		if (v->length < capacity)
		{
			v->lines[v->length++] = (char *) lfirst(lc);
		}
	}
}

char	  **
detail_sorter_lines_sorted(YbDetailSorter *v)
{
	qsort(v->lines, v->length,
		  sizeof(const char *), yb_detail_sort_comparator);
	return v->lines;
}

void
detail_sorter_free(YbDetailSorter *v)
{
	pfree(v->lines);
}

char *
YBDetailSorted(char *input)
{
	if (input == NULL)
		return input;

	/*
	 * this delimiter is hard coded in backend/catalog/pg_shdepend.c, inside of
	 * the storeObjectDescription function:
	 */
	char		delimiter[2] = "\n";

	/* init stringinfo used for concatenation of the output: */
	StringInfoData s;

	initStringInfo(&s);

	/* this list stores the non-empty tokens, extra counter to know how many: */
	List	   *line_store = NIL;
	int			line_count = 0;

	char	   *token;

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

	YbDetailSorter sorter;

	detail_sorter_from_list(&sorter, line_store, line_count);

	if (line_count == 0)
	{
		/* put the original input in: */
		appendStringInfoString(&s, input);
	}
	else
	{
		char	  **sortedLines = detail_sorter_lines_sorted(&sorter);

		for (int i = 0; i < line_count; i++)
		{
			if (sortedLines[i] != NULL)
			{
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
static const char *
YBComputeNonCSortKey(Oid collation_id, const char *value, int64_t bytes)
{
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
	Size		bsize = -1;
	bool		is_icu_provider = false;
	const int	buflen1 = bytes;
	char	   *buf1 = palloc(buflen1 + 1);
	char	   *buf2 = palloc(kTextBufLen);
	int			buflen2 = kTextBufLen;

	memcpy(buf1, value, bytes);
	buf1[buflen1] = '\0';

#ifdef USE_ICU
	int32_t		ulen = -1;
	UChar	   *uchar = NULL;
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
		Assert((ptrdiff_t) bsize >= 0);
		/*
		 * Both strxfrm and strxfrm_l return the length of the transformed
		 * string not including the terminating \0 byte.
		 */
		Assert(buf2[bsize] == '\0');
	}
	return buf2;
}

void
YBGetCollationInfo(Oid collation_id,
				   const YbcPgTypeEntity *type_entity,
				   Datum datum,
				   bool is_null,
				   YbcPgCollationInfo *collation_info)
{
	if (!type_entity)
	{
		Assert(collation_id == InvalidOid);
		collation_info->collate_is_valid_non_c = false;
		collation_info->sortkey = NULL;
		return;
	}

	if (type_entity->yb_type != YB_YQL_DATA_TYPE_STRING)
	{
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
	switch (type_entity->type_oid)
	{
		case NAMEOID:
		case TEXTOID:
		case BPCHAROID:
		case VARCHAROID:
			if (collation_id == InvalidOid)
			{
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
	if (!is_null && collation_info->collate_is_valid_non_c)
	{
		char	   *value;
		int64_t		bytes = type_entity->datum_fixed_size;

		type_entity->datum_to_yb(datum, &value, &bytes);
		/*
		 * Collation sort keys are compared using strcmp so they are null
		 * terminated and cannot have embedded \0 byte.
		 */
		collation_info->sortkey = YBComputeNonCSortKey(collation_id, value, bytes);
	}
	else
	{
		collation_info->sortkey = NULL;
	}
}

static bool
YBNeedCollationEncoding(const YbcPgColumnInfo *column_info)
{
	/* We only need collation encoding for range keys. */
	return (column_info->is_primary && !column_info->is_hash);
}

void
YBSetupAttrCollationInfo(YbcPgAttrValueDescriptor *attr, const YbcPgColumnInfo *column_info)
{
	if (attr->collation_id != InvalidOid && !YBNeedCollationEncoding(column_info))
	{
		attr->collation_id = InvalidOid;
	}
	YBGetCollationInfo(attr->collation_id, attr->type_entity, attr->datum,
					   attr->is_null, &attr->collation_info);
}

bool
YBIsCollationValidNonC(Oid collation_id)
{
	/*
	 * Before Postgres has properly setup the default collation as the database
	 * connection during connection time, we can only be doing catalog table
	 * accesses and PG15 has made collation aware columns to have explicit C
	 * collation.
	 */
	Assert(yb_default_collation_resolved ||
		   !OidIsValid(collation_id) ||
		   collation_id == C_COLLATION_OID);

	bool		is_valid_non_c = (YBIsCollationEnabled() &&
								  OidIsValid(collation_id) &&
								  !lc_collate_is_c(collation_id));

	return is_valid_non_c;
}

bool
YBRequiresCacheToCheckLocale(Oid collation)
{
	/*
	 * lc_collate_is_c and lc_ctype_is_c have some basic checks for C locale.
	 * If those checks fail to give an answer, then these functions check the
	 * catalog cache. In DocDB, we cannot use the catalog cache - so we should
	 * not push down collations where DocDB would need to access the cache to
	 * get information about the locale.
	 */
	return OidIsValid(collation) && collation != DEFAULT_COLLATION_OID
		&& collation != C_COLLATION_OID && collation != POSIX_COLLATION_OID;
}

bool
YBIsDbLocaleDefault()
{
	/*
	 * YB's initdb sets the default locale to UTF-8 for LC_CTYPE and C for
	 * LC_COLLATE. If a database changes its locale to a non-UTF-8 locale, then
	 * DocDB may have different semantics and return different results.
	 * (See CheckMyDatabase in postinit.c and setlocales in initdb.c)
	 */
	char	   *locale;

	if ((locale = setlocale(LC_CTYPE, NULL)) && !YbIsUtf8Locale(locale))
		return false;
	if ((locale = setlocale(LC_COLLATE, NULL)) && !YbIsCLocale(locale))
		return false;

	return true;
}

Oid
YBEncodingCollation(YbcPgStatement handle, int attr_num, Oid attcollation)
{
	if (attcollation == InvalidOid)
		return InvalidOid;
	YbcPgColumnInfo column_info = {0};

	HandleYBStatus(YBCPgDmlGetColumnInfo(handle, attr_num, &column_info));
	return YBNeedCollationEncoding(&column_info) ? attcollation : InvalidOid;
}

bool
IsYbExtensionUser(Oid member)
{
	return IsYugaByteEnabled() && has_privs_of_role(member, DEFAULT_ROLE_YB_EXTENSION);
}

bool
IsYbFdwUser(Oid member)
{
	return IsYugaByteEnabled() && has_privs_of_role(member, DEFAULT_ROLE_YB_FDW);
}

void
YBSetParentDeathSignal()
{
#ifdef HAVE_SYS_PRCTL_H
	char	   *pdeathsig_str = getenv("YB_PG_PDEATHSIG");

	if (pdeathsig_str)
	{
		char	   *end_ptr = NULL;
		long int	pdeathsig = strtol(pdeathsig_str, &end_ptr, 10);

		if (end_ptr == pdeathsig_str + strlen(pdeathsig_str))
		{
			if (pdeathsig >= 1 && pdeathsig <= 31)
			{
				/*
				 * TODO: prctl(PR_SET_PDEATHSIG) is Linux-specific, look into
				 * portable ways to prevent orphans when parent is killed.
				 */
				prctl(PR_SET_PDEATHSIG, pdeathsig);
			}
			else
			{
				fprintf(stderr,
						"Error: YB_PG_PDEATHSIG is an invalid signal value: %ld",
						pdeathsig);
			}

		}
		else
		{
			fprintf(stderr,
					"Error: failed to parse the value of YB_PG_PDEATHSIG: %s",
					pdeathsig_str);
		}
	}
#endif
}

Oid
YbGetRelfileNodeId(Relation relation)
{
	if (relation->rd_rel->relfilenode != InvalidOid)
	{
		return relation->rd_rel->relfilenode;
	}
	return RelationGetRelid(relation);
}

Oid
YbGetRelfileNodeIdFromRelId(Oid relationId)
{
	Relation	rel = RelationIdGetRelation(relationId);
	Oid			relfileNodeId = YbGetRelfileNodeId(rel);

	RelationClose(rel);
	return relfileNodeId;
}

bool
IsYbDbAdminUser(Oid member)
{
	return IsYugaByteEnabled() && has_privs_of_role(member, DEFAULT_ROLE_YB_DB_ADMIN);
}

bool
IsYbDbAdminUserNosuper(Oid member)
{
	return IsYugaByteEnabled() && is_member_of_role_nosuper(member, DEFAULT_ROLE_YB_DB_ADMIN);
}

void
YbCheckUnsupportedSystemColumns(int attnum, const char *colname, RangeTblEntry *rte)
{
	if (rte->relkind == RELKIND_FOREIGN_TABLE)
		return;
	switch (attnum)
	{
		case SelfItemPointerAttributeNumber:
		case MinTransactionIdAttributeNumber:
		case MinCommandIdAttributeNumber:
		case MaxTransactionIdAttributeNumber:
		case MaxCommandIdAttributeNumber:
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("system column \"%s\" is not supported yet", colname)));
		default:
			break;
	}
}

void
YbRegisterSysTableForPrefetching(int sys_table_id)
{
	/*
	 * sys_only_filter_attr stores attr which will be used to filter table rows
	 * related to system cache entries. In case particular table must always
	 * load all the rows or system cache filtering is disabled the
	 * sys_only_filter_attr must be set to InvalidAttrNumber.
	 */
	int			sys_only_filter_attr = InvalidAttrNumber;
	int			db_id = MyDatabaseId;
	int			sys_table_index_id = InvalidOid;
	bool		fetch_ybctid = true;

	switch (sys_table_id)
	{
			/* TemplateDb tables */
		case AuthMemRelationId: /* pg_auth_members */
			db_id = Template1DbOid;
			sys_table_index_id = AuthMemMemRoleIndexId;
			sys_only_filter_attr = InvalidAttrNumber;
			break;
		case AuthIdRelationId:	/* pg_authid */
			db_id = Template1DbOid;
			sys_table_index_id = AuthIdRolnameIndexId;
			sys_only_filter_attr = InvalidAttrNumber;
			break;
		case DatabaseRelationId:	/* pg_database */
			db_id = Template1DbOid;
			sys_table_index_id = DatabaseNameIndexId;
			sys_only_filter_attr = InvalidAttrNumber;
			break;

		case YBCatalogVersionRelationId:	/* pg_yb_catalog_version */
			fetch_ybctid = false;
			yb_switch_fallthrough();

		case DbRoleSettingRelationId:	/* pg_db_role_setting */
			yb_switch_fallthrough();
		case TableSpaceRelationId:	/* pg_tablespace */
			yb_switch_fallthrough();
		case YbProfileRelationId:	/* pg_yb_profile */
			yb_switch_fallthrough();
		case YbRoleProfileRelationId:	/* pg_yb_role_profile */
			db_id = Template1DbOid;
			sys_only_filter_attr = InvalidAttrNumber;
			break;

			/* MyDb tables */
		case AccessMethodProcedureRelationId:	/* pg_amproc */
			sys_table_index_id = AccessMethodProcedureIndexId;
			sys_only_filter_attr = Anum_pg_amproc_oid;
			break;
		case AccessMethodRelationId:	/* pg_am */
			sys_table_index_id = AmNameIndexId;
			sys_only_filter_attr = Anum_pg_am_oid;
			break;
		case AttrDefaultRelationId: /* pg_attrdef */
			sys_table_index_id = AttrDefaultIndexId;
			sys_only_filter_attr = Anum_pg_attrdef_oid;
			break;
		case AttributeRelationId:	/* pg_attribute */
			sys_table_index_id = AttributeRelidNameIndexId;
			sys_only_filter_attr = Anum_pg_attribute_attrelid;
			break;
		case CastRelationId:	/* pg_cast */
			sys_table_index_id = CastSourceTargetIndexId;
			sys_only_filter_attr = Anum_pg_cast_oid;
			break;
		case ConstraintRelationId:	/* pg_constraint */
			sys_table_index_id = ConstraintRelidTypidNameIndexId;
			sys_only_filter_attr = Anum_pg_constraint_oid;
			break;
		case EnumRelationId:	/* pg_enum */
			sys_table_index_id = EnumTypIdLabelIndexId;
			sys_only_filter_attr = Anum_pg_enum_oid;
			break;
		case IndexRelationId:	/* pg_index */
			sys_table_index_id = IndexIndrelidIndexId;
			sys_only_filter_attr = Anum_pg_index_indexrelid;
			break;
		case InheritsRelationId:	/* pg_inherits */
			sys_table_index_id = InheritsParentIndexId;
			sys_only_filter_attr = Anum_pg_inherits_inhrelid;
			break;
		case NamespaceRelationId:	/* pg_namespace */
			sys_table_index_id = NamespaceNameIndexId;
			sys_only_filter_attr = Anum_pg_namespace_oid;
			break;
		case OperatorClassRelationId:	/* pg_opclass */
			sys_table_index_id = OpclassAmNameNspIndexId;
			sys_only_filter_attr = Anum_pg_opclass_oid;
			break;
		case OperatorRelationId:	/* pg_operator */
			sys_table_index_id = OperatorNameNspIndexId;
			sys_only_filter_attr = Anum_pg_operator_oid;
			break;
		case PolicyRelationId:	/* pg_policy */
			sys_table_index_id = PolicyPolrelidPolnameIndexId;
			sys_only_filter_attr = Anum_pg_policy_oid;
			break;
		case ProcedureRelationId:	/* pg_proc */
			sys_table_index_id = ProcedureNameArgsNspIndexId;
			sys_only_filter_attr = Anum_pg_proc_oid;
			break;
		case RelationRelationId:	/* pg_class */
			sys_table_index_id = ClassNameNspIndexId;
			sys_only_filter_attr = Anum_pg_class_oid;
			break;
		case CollationRelationId:	/* pg_collation */
			sys_table_index_id = CollationNameEncNspIndexId;
			break;
		case RangeRelationId:	/* pg_range */
			sys_only_filter_attr = Anum_pg_range_rngtypid;
			break;
		case RewriteRelationId: /* pg_rewrite */
			sys_table_index_id = RewriteRelRulenameIndexId;
			sys_only_filter_attr = Anum_pg_rewrite_oid;
			break;
		case StatisticRelationId:	/* pg_statistic */
			sys_only_filter_attr = Anum_pg_statistic_starelid;
			break;
		case StatisticExtRelationId:	/* pg_statistic_ext */
			sys_table_index_id = StatisticExtNameIndexId;
			sys_only_filter_attr = Anum_pg_statistic_ext_oid;
			break;
		case StatisticExtDataRelationId:	/* pg_statistic_ext_data */
			sys_only_filter_attr = Anum_pg_statistic_ext_data_stxoid;
			break;
		case TriggerRelationId: /* pg_trigger */
			sys_table_index_id = TriggerRelidNameIndexId;
			sys_only_filter_attr = Anum_pg_trigger_oid;
			break;
		case TypeRelationId:	/* pg_type */
			sys_table_index_id = TypeNameNspIndexId;
			sys_only_filter_attr = Anum_pg_type_oid;
			break;
		case AccessMethodOperatorRelationId:	/* pg_amop */
			sys_table_index_id = AccessMethodOperatorIndexId;
			sys_only_filter_attr = Anum_pg_amop_oid;
			break;
		case PartitionedRelationId: /* pg_partitioned_table */
			sys_only_filter_attr = Anum_pg_partitioned_table_partrelid;
			break;

		default:
			{
				ereport(FATAL,
						(errcode(ERRCODE_INTERNAL_ERROR),
						 errmsg("sys table '%d' is not yet intended for preloading",
								sys_table_id)));

			}
	}

	if (!YbUseMinimalCatalogCachesPreload())
		sys_only_filter_attr = InvalidAttrNumber;

	YBCRegisterSysTableForPrefetching(db_id, sys_table_id, sys_table_index_id,
									  sys_only_filter_attr, fetch_ybctid);
}

void
YbTryRegisterCatalogVersionTableForPrefetching()
{
	if (YbGetCatalogVersionType() == CATALOG_VERSION_CATALOG_TABLE)
		YbRegisterSysTableForPrefetching(YBCatalogVersionRelationId);
}

static bool
YBCIsRegionLocal(Relation rel)
{
	double		cost = 0.0;

	return (IsNormalProcessingMode() &&
			!IsSystemRelation(rel) &&
			get_yb_tablespace_cost(rel->rd_rel->reltablespace, &cost) &&
			cost <= yb_interzone_cost);
}

bool
check_yb_xcluster_consistency_level(char **newval, void **extra, GucSource source)
{
	int			newConsistency = XCLUSTER_CONSISTENCY_TABLET;

	if (strcmp(*newval, "tablet") == 0)
		newConsistency = XCLUSTER_CONSISTENCY_TABLET;
	else if (strcmp(*newval, "database") == 0)
		newConsistency = XCLUSTER_CONSISTENCY_DATABASE;
	else
		return false;

	*extra = malloc(sizeof(int));
	if (!*extra)
		return false;
	*((int *) *extra) = newConsistency;

	return true;
}

void
assign_yb_xcluster_consistency_level(const char *newval, void *extra)
{
	yb_xcluster_consistency_level = *((int *) extra);
}

bool
parse_yb_read_time(const char *value, unsigned long long *result, bool *is_ht_unit)
{
	unsigned long long val;
	char	   *endptr;

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
	bool		is_ht_unit;

	if (!parse_yb_read_time(*newval, &value_ull, &is_ht_unit))
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
		char		read_time_string[23];

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
	unsigned long long now_micro_sec = ((unsigned long long) now_tv.tv_sec * USECS_PER_SEC) + now_tv.tv_usec;

	if (read_time_ull > now_micro_sec)
	{
		GUC_check_errdetail("Provided timestamp is in the future.");
		return false;
	}
	return true;
}

void
assign_yb_read_time(const char *newval, void *extra)
{
	unsigned long long value_ull;
	bool		is_ht_unit;

	elog(DEBUG1, "Setting yb_read_time to %s", newval);
	parse_yb_read_time(newval, &value_ull, &is_ht_unit);
	/*
	 * Don't refresh the sys caches in case the read time value didn't change.
	 */
	bool		needs_syscaches_refresh = (yb_read_time != value_ull);

	yb_read_time = value_ull;
	yb_is_read_time_ht = is_ht_unit;

	/* Clear and reload system catalog caches, including all callbacks. */
	if (needs_syscaches_refresh)
		YbResetCatalogCacheVersion();

	/* Skip logging the warning for internal calls. */
	if (yb_read_time && !yb_disable_catalog_version_check && !am_walsender)
	{
		ereport(NOTICE,
				(errmsg("yb_read_time should only be set for read-only queries. "
						"Write-DML or DDL queries are not allowed when yb_read_time is "
						"set.")));
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

void
YBCheckServerAccessIsAllowed()
{
	if (*YBCGetGFlags()->ysql_disable_server_file_access)
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("server file access disabled"),
				 errdetail("tserver flag ysql_disable_server_file_access is "
						   "set to true.")));
}

static void
aggregateRpcMetrics(YbcPgExecStorageMetrics *instr_metrics,
					const YbcPgExecStorageMetrics *exec_stats_metrics)
{
	if (exec_stats_metrics->version == 0)
		return;

	instr_metrics->version += exec_stats_metrics->version;

	for (int i = 0; i < YB_STORAGE_GAUGE_COUNT; ++i)
		instr_metrics->gauges[i] += exec_stats_metrics->gauges[i];

	for (int i = 0; i < YB_STORAGE_COUNTER_COUNT; ++i)
		instr_metrics->counters[i] += exec_stats_metrics->counters[i];
	for (int i = 0; i < YB_STORAGE_EVENT_COUNT; ++i)
	{
		const YbcPgExecEventMetric *val = &exec_stats_metrics->events[i];
		YbcPgExecEventMetric *agg = &instr_metrics->events[i];

		agg->sum += val->sum;
		agg->count += val->count;
	}
}

static void
aggregateStats(YbInstrumentation *instr, const YbcPgExecStats *exec_stats)
{
	/* User Table stats */
	instr->tbl_reads.count += exec_stats->tables.reads;
	instr->tbl_reads.wait_time += exec_stats->tables.read_wait;
	instr->tbl_read_ops += exec_stats->tables.read_ops;
	instr->tbl_writes += exec_stats->tables.writes;
	instr->tbl_reads.rows_scanned += exec_stats->tables.rows_scanned;
	instr->tbl_reads.rows_received += exec_stats->tables.rows_received;

	/* Secondary Index stats */
	instr->index_reads.count += exec_stats->indices.reads;
	instr->index_reads.wait_time += exec_stats->indices.read_wait;
	instr->index_read_ops += exec_stats->indices.read_ops;
	instr->index_writes += exec_stats->indices.writes;
	instr->index_reads.rows_scanned += exec_stats->indices.rows_scanned;
	instr->index_reads.rows_received += exec_stats->indices.rows_received;

	/* System Catalog stats */
	instr->catalog_reads.count += exec_stats->catalog.reads;
	instr->catalog_reads.wait_time += exec_stats->catalog.read_wait;
	instr->catalog_read_ops += exec_stats->catalog.read_ops;
	instr->catalog_writes += exec_stats->catalog.writes;
	instr->catalog_reads.rows_scanned += exec_stats->catalog.rows_scanned;
	instr->catalog_reads.rows_received += exec_stats->catalog.rows_received;

	/* Flush stats */
	instr->write_flushes.count += exec_stats->num_flushes;
	instr->write_flushes.wait_time += exec_stats->flush_wait;

	aggregateRpcMetrics(&instr->read_metrics, &exec_stats->read_metrics);
	aggregateRpcMetrics(&instr->write_metrics, &exec_stats->write_metrics);

	instr->rows_removed_by_recheck += exec_stats->rows_removed_by_recheck;
	instr->commit_wait += exec_stats->commit_wait;
}

static YbcPgExecReadWriteStats
getDiffReadWriteStats(const YbcPgExecReadWriteStats *current,
					  const YbcPgExecReadWriteStats *old)
{
	return (YbcPgExecReadWriteStats)
	{
		current->reads - old->reads,
			current->read_ops - old->read_ops,
			current->writes - old->writes,
			current->read_wait - old->read_wait,
			current->rows_scanned - old->rows_scanned,
			current->rows_received - old->rows_received
	};
}

static void
calculateStorageMetricsDiff(YbcPgExecStorageMetrics *result,
							const YbcPgExecStorageMetrics *current,
							const YbcPgExecStorageMetrics *old)
{
	if (old->version == current->version)
		return;

	result->version = current->version;

	for (int i = 0; i < YB_STORAGE_GAUGE_COUNT; ++i)
		result->gauges[i] =
			current->gauges[i] - old->gauges[i];

	for (int i = 0; i < YB_STORAGE_COUNTER_COUNT; ++i)
		result->counters[i] =
			current->counters[i] - old->counters[i];

	for (int i = 0; i < YB_STORAGE_EVENT_COUNT; ++i)
	{
		YbcPgExecEventMetric *result_metric = &result->events[i];
		const YbcPgExecEventMetric *current_metric = &current->events[i];
		const YbcPgExecEventMetric *old_metric = &old->events[i];

		result_metric->sum = current_metric->sum - old_metric->sum;
		result_metric->count = current_metric->count - old_metric->count;
	}
}

static void
calculateExecStatsDiff(const YbSessionStats *stats, YbcPgExecStats *result)
{
	const YbcPgExecStats *current = &stats->current_state.stats;
	const YbcPgExecStats *old = &stats->latest_snapshot;

	result->tables = getDiffReadWriteStats(&current->tables, &old->tables);
	result->indices = getDiffReadWriteStats(&current->indices, &old->indices);
	result->catalog = getDiffReadWriteStats(&current->catalog, &old->catalog);

	result->num_flushes = current->num_flushes - old->num_flushes;
	result->flush_wait = current->flush_wait - old->flush_wait;

	calculateStorageMetricsDiff(&result->read_metrics, &current->read_metrics, &old->read_metrics);
	calculateStorageMetricsDiff(&result->write_metrics, &current->write_metrics, &old->write_metrics);

	result->rows_removed_by_recheck = current->rows_removed_by_recheck - old->rows_removed_by_recheck;
	result->commit_wait = current->commit_wait - old->commit_wait;
}

static void
refreshExecStats(YbSessionStats *stats, bool include_catalog_stats)
{
	const YbcPgExecStats *current = &stats->current_state.stats;
	YbcPgExecStats *old = &stats->latest_snapshot;

	old->tables = current->tables;
	old->indices = current->indices;

	old->num_flushes = current->num_flushes;
	old->flush_wait = current->flush_wait;

	if (include_catalog_stats)
		old->catalog = current->catalog;

	if (yb_session_stats.current_state.metrics_capture)
	{
		memcpy(&old->read_metrics, &current->read_metrics, sizeof(old->read_metrics));
		memcpy(&old->write_metrics, &current->write_metrics, sizeof(old->write_metrics));
	}

	old->rows_removed_by_recheck = current->rows_removed_by_recheck;
	old->commit_wait = current->commit_wait;
}

void
YbUpdateSessionStats(YbInstrumentation *yb_instr)
{
	YbcPgExecStats exec_stats = {0};

	/* Find the diff between the current stats and the last stats snapshot */
	calculateExecStatsDiff(&yb_session_stats, &exec_stats);

	/*
	 * Refresh the snapshot to reflect the current state of query execution.
	 * This function is always invoked during the query execution phase.
	 */
	YbRefreshSessionStatsDuringExecution();

	/*
	 * Update the supplied instrumentation handle with the delta calculated
	 * above.
	 */
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

bool
YbIsSessionStatsTimerEnabled()
{
	return yb_session_stats.current_state.is_timing_required;
}

void
YbToggleCommitStatsCollection(bool enable)
{
	yb_session_stats.current_state.is_commit_stats_required = enable;
}

bool
YbIsCommitStatsCollectionEnabled()
{
	return yb_session_stats.current_state.is_commit_stats_required;
}

void
YbRecordCommitLatency(uint64_t latency_us)
{
	yb_session_stats.current_state.stats.commit_wait += latency_us;
}

void
YbSetMetricsCaptureType(YbcPgMetricsCaptureType metrics_capture)
{
	yb_session_stats.current_state.metrics_capture = metrics_capture;
}

void
YbSetMetricsCaptureTypeIfUnset(YbcPgMetricsCaptureType metrics_capture)
{
	if (yb_session_stats.current_state.metrics_capture == YB_YQL_METRICS_CAPTURE_NONE)
		yb_session_stats.current_state.metrics_capture = metrics_capture;
}

void
YbSetCatalogCacheVersion(YbcPgStatement handle, uint64_t version)
{
	/*
	 * Skip setting catalog version which skips catalog version check at
	 * tserver. Used in time-traveling queries as they might read old data
	 * with old catalog version.
	 */
	if (yb_disable_catalog_version_check || yb_is_calling_internal_sql_for_ddl)
		return;
	HandleYBStatus(YBIsDBCatalogVersionMode()
				   ? YBCPgSetDBCatalogCacheVersion(handle, MyDatabaseId, version)
				   : YBCPgSetCatalogCacheVersion(handle, version));
}

static Oid
YbGetNonSystemTablespaceOid(Relation rel)
{
	if (rel->rd_rel->reltablespace < FirstNormalObjectId)
	{
		Assert(OidIsValid(rel->rd_id));
		Assert(rel->rd_rel->reltablespace == InvalidOid ||
			   rel->rd_rel->reltablespace == GLOBALTABLESPACE_OID);
		return InvalidOid;
	}
	return rel->rd_rel->reltablespace;
}

YbcPgTableLocalityInfo
YbBuildTableLocalityInfo(Relation rel)
{
	return (YbcPgTableLocalityInfo)
	{
		.is_region_local = YBCIsRegionLocal(rel), .tablespace_oid = YbGetNonSystemTablespaceOid(rel)
	};
}

YbcPgTableLocalityInfo
YbBuildSystemTableLocalityInfo(Oid sys_rel_oid)
{
	Assert(IsCatalogRelationOid(sys_rel_oid));
	return (YbcPgTableLocalityInfo){};
}

uint64_t
YbGetSharedCatalogVersion()
{
	uint64_t	version = 0;

	HandleYBStatus(YBIsDBCatalogVersionMode()
				   ? YBCGetSharedDBCatalogVersion(MyDatabaseId, &version)
				   : YBCGetSharedCatalogVersion(&version));
	return version;
}

LockWaitPolicy
YBGetDocDBWaitPolicy(LockWaitPolicy pg_wait_policy)
{
	LockWaitPolicy result = pg_wait_policy;

	if (!YBCPgIsDdlMode() && IsolationIsSerializable())
	{
		/*
		 * TODO(concurrency-control): We don't honour SKIP LOCKED/ NO WAIT yet in serializable
		 * isolation level.
		 *
		 * The !YBCPgIsDdlMode() check is to avoid the warning for DDLs because they try to acquire a
		 * row lock on the catalog version with LockWaitError for Fail-on-Conflict semantics.
		 */
		if (pg_wait_policy == LockWaitSkip || pg_wait_policy == LockWaitError)
			elog(WARNING,
				 "%s clause is not supported yet for SERIALIZABLE isolation "
				 "(GH issue #11761)",
				 pg_wait_policy == LockWaitSkip ? "SKIP LOCKED" : "NO WAIT");

		result = LockWaitBlock;
	}

	if (result == LockWaitBlock && !YBIsWaitQueueEnabled())
	{
		/*
		 * If wait-queues are not enabled, we default to the "Fail-on-Conflict" policy which is
		 * mapped to LockWaitError right now (see WaitPolicy proto for meaning of
		 * "Fail-on-Conflict" and the reason why LockWaitError is not mapped to no-wait
		 * semantics but to Fail-on-Conflict semantics).
		 */
		elog(DEBUG1, "Falling back to LockWaitError since wait-queues are not enabled");
		result = LockWaitError;
	}
	elog(DEBUG2, "docdb_wait_policy=%d pg_wait_policy=%d", result, pg_wait_policy);
	return result;
}

uint32_t
YbGetNumberOfDatabases()
{
	uint32_t	num_databases = 0;

	HandleYBStatus(YBCGetNumberOfDatabases(&num_databases));
	/*
	 * It is possible that at the beginning master has not passed back the
	 * contents of pg_yb_catalog_versions back to tserver yet so that tserver's
	 * ysql_db_catalog_version_map_ is still empty. In this case we get 0
	 * databases back.
	 */
	return num_databases;
}

bool
YbCatalogVersionTableInPerdbMode()
{
	bool		perdb_mode = false;

	HandleYBStatus(YBCCatalogVersionTableInPerdbMode(&perdb_mode));
	return perdb_mode;
}

static bool yb_is_batched_execution = false;

bool
YbIsBatchedExecution()
{
	return yb_is_batched_execution;
}

void
YbSetIsBatchedExecution(bool value)
{
	yb_is_batched_execution = value;
}

YbOptSplit *
YbGetSplitOptions(Relation rel)
{
	if (rel->yb_table_properties->is_colocated)
		return NULL;

	YbOptSplit *split_options = makeNode(YbOptSplit);

	/*
	 * The split type is NUM_TABLETS when the relation has hash key columns
	 * OR if the relation's range key is currently being dropped. Otherwise,
	 * the split type is SPLIT_POINTS.
	 */
	split_options->split_type =
		rel->yb_table_properties->num_hash_key_columns > 0 ||
		((rel->rd_rel->relkind == RELKIND_RELATION ||
		  rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE) &&
		 RelationGetPrimaryKeyIndex(rel) == InvalidOid) ? NUM_TABLETS :
		SPLIT_POINTS;
	split_options->num_tablets = rel->yb_table_properties->num_tablets;

	/*
	 * Copy split points for range keys with more than one tablet.
	 */
	if (split_options->split_type == SPLIT_POINTS
		&& rel->yb_table_properties->num_tablets > 1)
	{
		YbcPgTableDesc yb_desc = NULL;

		HandleYBStatus(YBCPgGetTableDesc(MyDatabaseId,
										 YbGetRelfileNodeId(rel), &yb_desc));
		getRangeSplitPointsList(RelationGetRelid(rel), yb_desc,
								rel->yb_table_properties,
								&split_options->split_points);
	}
	return split_options;
}

bool
YbIsColumnPartOfKey(Relation rel, const char *column_name)
{
	if (column_name)
	{
		Bitmapset  *pkey = YBGetTablePrimaryKeyBms(rel);
		HeapTuple	attTup = SearchSysCacheCopyAttName(RelationGetRelid(rel),
													   column_name);

		if (HeapTupleIsValid(attTup))
		{
			Form_pg_attribute attform = (Form_pg_attribute) GETSTRUCT(attTup);
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
int			ysql_conn_mgr_sticky_object_count = 0;

/*
 * `yb_ysql_conn_mgr_sticky_guc` is used to denote stickiness of a connection
 * due to the setting of GUC variables that cannot be directly supported
 * by Connection Manager.
 */
bool		yb_ysql_conn_mgr_sticky_guc = false;

/*
 * `yb_ysql_conn_mgr_superuser_existed` denotes whether the session user was
 * ever a superuser.
 */
bool		yb_ysql_conn_mgr_superuser_existed = false;

/*
 * `yb_ysql_conn_mgr_sticky_locks` denotes whether any session-scoped locks
 * are/were held by the current session.
 */
bool		yb_ysql_conn_mgr_sticky_locks = false;

bool
YbIsSuperuserConnSticky()
{
	return *(YBCGetGFlags()->ysql_conn_mgr_superuser_sticky);
}

/*
 * ```YbIsConnectionMadeStickyUsingGUC()``` returns whether or not a
 * connection is made sticky using specific GUC variables. This also includes
 * making the connection sticky if the "session user" was ever a superuser,
 * provided that the flag ysql_conn_mgr_superuser_sticky is enabled.
 */
static bool
YbIsConnectionMadeStickyUsingGUC()
{
	/*
	 * If the user on this backend was ever a superuser, let the connection
	 * remain sticky to avoid potential errors during deploy phase for
	 * superuser-only GUC variables. Deliberately leave superuser() as the last
	 * condition to avoid unnecessary calls to superuser(), while also allowing
	 * the history stored in yb_ysql_conn_mgr_superuser_existed to be used on
	 * priority.
	 */
	yb_ysql_conn_mgr_superuser_existed = yb_ysql_conn_mgr_superuser_existed ||
		session_auth_is_superuser;
	return yb_ysql_conn_mgr_sticky_guc = yb_ysql_conn_mgr_sticky_guc ||
		(YbIsSuperuserConnSticky() && yb_ysql_conn_mgr_superuser_existed);
}

/*
 * ```YbIsStickyConnection(int *change)``` updates the number of objects that requires a sticky
 * connection and returns whether or not the client connection
 * requires stickiness. i.e. if there is any `WITH HOLD CURSOR` or `TEMP TABLE`
 * at the end of the transaction.
 *
 * Also check if any GUC variable is set that requires a sticky connection.
 */
bool
YbIsStickyConnection(int *change)
{
	ysql_conn_mgr_sticky_object_count += *change;
	*change = 0;				/* Since it is updated it will be set to 0 */
	elog(DEBUG5, "Number of sticky objects: %d", ysql_conn_mgr_sticky_object_count);
	return (ysql_conn_mgr_sticky_object_count > 0) ||
		YbIsConnectionMadeStickyUsingGUC() ||
		yb_ysql_conn_mgr_sticky_locks;
}

void	  **
YbPtrListToArray(const List *str_list, size_t *length)
{
	void	  **buf;
	ListCell   *lc;

	/* Assumes that the pointer sizes are equal for every type */
	buf = (void **) palloc(sizeof(void *) * list_length(str_list));
	*length = 0;
	foreach(lc, str_list)
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
YbReadWholeFile(const char *filename, int *length, int elevel)
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
bool		yb_use_tserver_key_auth;

bool
yb_use_tserver_key_auth_check_hook(bool *newval, void **extra, GucSource source)
{
	/* Allow setting yb_use_tserver_key_auth to false */
	/*
	 * Parallel workers are created and maintained by postmaster. So physical connections
	 * can never be of parallel worker type, therefore it makes no sense to restore
	 * or even do check/assign hooks for ysql connection manager specific guc variables
	 * on parallel worker process.
	 */
	if (!(*newval) || yb_is_parallel_worker == true)
		return true;

	/*
	 * yb_use_tserver_key_auth can only be set for client connections made on
	 * unix socket.
	 */
	if (MyProcPort->raddr.addr.ss_family != AF_UNIX)
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
							  true /* indexOK */ , NULL /* snapshot */ ,
							  1 /* nkeys */ , &key);

	bool		pk_copied = false;

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
					AttrMap    *att_map = build_attrmap_by_name(RelationGetDescr(rel),
																RelationGetDescr(rel),
																false /* yb_ignore_type_mismatch */ );

					Relation	idx_rel =
						index_open(con_form->conindid, AccessShareLock);
					IndexStmt  *index_stmt = generateClonedIndexStmt(NULL, idx_rel,
																	 att_map, NULL);

					Constraint *pk_constr = makeNode(Constraint);

					pk_constr->contype = CONSTR_PRIMARY;
					pk_constr->conname = index_stmt->idxname;
					pk_constr->options = index_stmt->options;
					pk_constr->indexspace = index_stmt->tableSpace;

					ListCell   *cell;

					foreach(cell, index_stmt->indexParams)
					{
						IndexElem  *ielem = lfirst(cell);

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
						 bool yb_copy_split_options,
						 YbOptSplit *preserved_index_split_options)
{
	bool		isNull;
	HeapTuple	indexTuple;
	HeapTuple	tuple;
	Datum		reloptions = (Datum) 0;
	Relation	indexedRel;
	IndexInfo  *indexInfo;
	oidvector  *indclass = NULL;
	Datum		indclassDatum;
	YbOptSplit *splitOpt = NULL;

	tuple = SearchSysCache1(RELOID,
							ObjectIdGetDatum(RelationGetRelid(indexRel)));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for index %u",
			 RelationGetRelid(indexRel));

	reloptions = SysCacheGetAttr(RELOID, tuple, Anum_pg_class_reloptions,
								 &isNull);
	ReleaseSysCache(tuple);
	reloptions = ybExcludeNonPersistentReloptions(reloptions);
	indexedRel = table_open(IndexGetRelation(RelationGetRelid(indexRel), false),
							ShareLock);
	indexInfo = BuildIndexInfo(indexRel);

	YbTryGetTableProperties(indexRel);
	YbGetTableProperties(indexedRel);

	indexTuple = SearchSysCache1(INDEXRELID,
								 ObjectIdGetDatum(RelationGetRelid(indexRel)));
	if (!HeapTupleIsValid(indexTuple))
		elog(ERROR, "cache lookup failed for index %u",
			 RelationGetRelid(indexRel));

	indclassDatum = SysCacheGetAttr(INDEXRELID, indexTuple,
									Anum_pg_index_indclass, &isNull);
	if (!isNull)
		indclass = (oidvector *) DatumGetPointer(indclassDatum);
	ReleaseSysCache(indexTuple);

	if (yb_copy_split_options)
	{
		splitOpt = indexRel->yb_table_properties
					? YbGetSplitOptions(indexRel)
					: preserved_index_split_options;
	}

	YBCCreateIndex(RelationGetRelationName(indexRel),
				   indexInfo,
				   RelationGetDescr(indexRel),
				   indexRel->rd_indoption,
				   reloptions,
				   RelationGetRelid(indexRel),
				   indexedRel,
				   splitOpt,
				   true /* skip_index_backfill */ ,
				   indexedRel->yb_table_properties->is_colocated,
				   indexedRel->yb_table_properties->tablegroup_oid,
				   InvalidOid /* colocation ID */ ,
				   indexRel->rd_rel->reltablespace,
				   newRelfileNodeId,
				   !indexRel->yb_table_properties ?
					   InvalidOid : YbGetRelfileNodeId(indexRel) /* oldRelfileNodeId */ ,
				   indclass ? indclass->values : NULL);

	table_close(indexedRel, ShareLock);

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
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("USING is not allowed in an index")));
			break;

		case SORTBY_HASH:
			if (is_tablegroup && !MyDatabaseColocated)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg("cannot create a hash partitioned index in a TABLEGROUP")));
			else if (is_colocated)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
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

/*
 * Given a query string, this functions redacts any password tokens in the string.
 * The input query string is preserved.
 * redacted_query_len is an optional parameter.
 */
const char *
YbGetRedactedQueryString(const char *query, int *redacted_query_len)
{
	const char *redacted_query = YbRedactPasswordIfExists(query, CMDTAG_UNKNOWN);

	if (redacted_query_len)
		*redacted_query_len = strlen(redacted_query);

	return redacted_query;
}

bool
YbIsUpdateOptimizationEnabled()
{
	return (yb_update_optimization_options.has_infra &&
			yb_update_optimization_options.is_enabled &&
			yb_update_optimization_options.num_cols_to_compare > 0 &&
			yb_update_optimization_options.max_cols_size_to_compare > 0);
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
	CreateStmt *dummyStmt = makeNode(CreateStmt);

	dummyStmt->relation =
		makeRangeVar(NULL, RelationGetRelationName(rel), -1);
	Relation	pg_constraint = table_open(ConstraintRelationId,
										   RowExclusiveLock);

	YbATCopyPrimaryKeyToCreateStmt(rel, pg_constraint, dummyStmt);
	table_close(pg_constraint, RowExclusiveLock);
	if (yb_copy_split_options)
	{
		YbGetTableProperties(rel);
		dummyStmt->split_options = YbGetSplitOptions(rel);
	}
	bool		is_null;
	HeapTuple	tuple = SearchSysCache1(RELOID,
										ObjectIdGetDatum(RelationGetRelid(rel)));
	Datum		datum = SysCacheGetAttr(RELOID,
										tuple, Anum_pg_class_reloptions, &is_null);

	if (!is_null)
		dummyStmt->options = untransformRelOptions(datum);
	ReleaseSysCache(tuple);
	YBCCreateTable(dummyStmt, RelationGetRelationName(rel),
				   rel->rd_rel->relkind, RelationGetDescr(rel),
				   RelationGetRelid(rel),
				   RelationGetNamespace(rel),
				   YbGetTableProperties(rel)->tablegroup_oid,
				   InvalidOid, rel->rd_rel->reltablespace,
				   newRelfileNodeId,
				   rel->rd_rel->relfilenode,
				   is_truncate);

	if (yb_test_fail_table_rewrite_after_creation)
		elog(ERROR, "Injecting error.");
}

Relation
YbGetRelationWithOverwrittenReplicaIdentity(Oid relid, char replident)
{
	Relation	relation;

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
	/* Shouldn't go backwards on yb_read_time */
	Assert(yb_read_time <= read_time_ht);
	char		read_time[50];

	sprintf(read_time, "%llu ht", (unsigned long long) read_time_ht);
	assign_yb_read_time(read_time, NULL);
	YbRelationCacheInvalidate();
}

void
YBCResetYbReadTimeAndInvalidateRelcache()
{
	assign_yb_read_time("0", NULL);
	YbRelationCacheInvalidate();
}

uint64_t
YbCalculateTimeDifferenceInMicros(TimestampTz yb_start_time)
{
	long		secs;
	int			microsecs;

	TimestampDifference(yb_start_time, GetCurrentTimestamp(), &secs,
						&microsecs);
	return secs * USECS_PER_SEC + microsecs;
}

static bool
YbIsSeparateDDLOrInitDBMode()
{
	return (YBCPgIsDdlMode() && !YBCPgIsDdlModeWithRegularTransactionBlock()) || YBCIsInitDbModeEnvVarSet();
}

bool
YbIsReadCommittedTxn()
{
	return IsYBReadCommitted() && !YbIsSeparateDDLOrInitDBMode();
}

bool
YbSkipPgSnapshotManagement()
{
	if (!YBCIsLegacyModeForCatalogOps())
		return false;

	/*
	 * In legacy pre-object locking mode, YSQL was skipping Pg's snapshot management for:
	 *
	 * (1) Separate DDL transactions (i.e., DDLs that are not part of a regular transaction block):
	 *     This is because changing snapshots would affect the ongoing active DML transaction.
	 * (2) Initdb mode. Actually, even if we didn't skip for initdb mode, it would be okay because the
	 *     snapshot's read time serial number wouldn't be used anyway. This is because initdb uses
	 *     the kRegular session type in PgSession.
	 * (3) Serializable isolation level: since YSQL doesn't use SSI (and instead uses
	 *     2 phase locking), it reads the latest data on DocDB always for this isolation level. So,
	 *     we just skipped the snapshot management. However, allowing snapshot management wouldn't
	 *     result in any issues because for serializable isolation, we anyway don't set a read time
	 *     for a read time serial number in PgClientSession.
	 *
	 * For the new object locking mode, we can ignore (1) because separate DDL transactions can't
	 * run in parallel with active DML transactions. This is because transactional DDL is enabled if
	 * object locking is enabled. We can ignore (2) because that check isn't required anyway. For
	 * (3), we need to enable snapshot management for catalog snapshots in serializable isolation.
	 * So, the handling of still using the latest read time instead of using snapshots for DMLs is
	 * done elsewhere.
	 */
	return YbIsSeparateDDLOrInitDBMode() || IsolationIsSerializable();
}

static YbOptionalReadPointHandle
YbMakeReadPointHandle(YbcReadPointHandle read_point)
{
	return (YbOptionalReadPointHandle)
	{
		.has_value = true,.value = read_point
	};
}

YbOptionalReadPointHandle
YbBuildCurrentReadPointHandle()
{
	return !YbSkipPgSnapshotManagement()
		? YbMakeReadPointHandle(YBCPgGetCurrentReadPoint())
		: (YbOptionalReadPointHandle)
	{
	};
}

void
YbUseSnapshotReadTime(uint64_t read_time)
{
	HandleYBStatus(YBCPgRegisterSnapshotReadTime(read_time,
												 true /* use_read_time */ ,
												 NULL /* handle */ ));
}

YbOptionalReadPointHandle
YbRegisterSnapshotReadTime(uint64_t read_time)
{
	YbcReadPointHandle handle = 0;

	HandleYBStatus(YBCPgRegisterSnapshotReadTime(read_time,
												 false /* use_read_time */ ,
												 &handle));
	return YbMakeReadPointHandle(handle);
}

YbOptionalReadPointHandle
YbResetTransactionReadPoint(bool is_catalog_snapshot)
{
	if (YbSkipPgSnapshotManagement())
		return (YbOptionalReadPointHandle)
	{
	};

	/*
	 * If this is a query layer retry for a kReadRestart error, avoid resetting the read point.
	 *
	 * TODO(#29272): Ensure that this logic of not resetting the read point works fine even when there
	 * are multiple snapshots being used by Pg.
	 */
	if (!YBCIsRestartReadPointRequested())
	{
		/*
		 * Flush all earlier operations so that they complete on the previous snapshot.
		 */
		YBFlushBufferedOperations(YBCMakeFlushDebugContextGetTxnSnapshot());
		HandleYBStatus(YBCPgResetTransactionReadPoint(is_catalog_snapshot));
	}

	if (!YBCIsLegacyModeForCatalogOps() && is_catalog_snapshot)
		return YbMakeReadPointHandle(YBCPgGetMaxReadPoint());
	else
		return YbMakeReadPointHandle(YBCPgGetCurrentReadPoint());
}

/*
 * TODO(#22370): the method will be used to make Const Based Optimizer to be
 * aware of fast backward scan capability.
 */
bool
YbUseFastBackwardScan()
{
	return *(YBCGetGFlags()->ysql_use_fast_backward_scan);
}

bool
YbIsYsqlConnMgrWarmupModeEnabled()
{
	return strcmp(YBCGetGFlags()->TEST_ysql_conn_mgr_dowarmup_all_pools_mode, "none") != 0;
}

bool
YbIsYsqlConnMgrEnabled()
{
	static int	cached_value = -1;

	if (cached_value == -1)
		cached_value = YBCIsEnvVarTrueWithDefault("FLAGS_enable_ysql_conn_mgr", false);
	return cached_value;
}

bool
YbIsAuthBackend()
{
	return yb_is_auth_backend;
}

/* Used in YB to check if an attribute is a key column. */
bool
YbIsAttrPrimaryKeyColumn(Relation rel, AttrNumber attnum)
{
	Bitmapset  *pkey = YBGetTablePrimaryKeyBms(rel);

	return bms_is_member(attnum -
						 YBGetFirstLowInvalidAttributeNumber(rel), pkey);
}

/* Retrieve the sort ordering of the first key element of an index. */
SortByDir
YbGetIndexKeySortOrdering(Relation indexRel)
{
	if (IndexRelationGetNumberOfKeyAttributes(indexRel) == 0)
		return SORTBY_DEFAULT;
	/*
	 * If there are key columns, check the indoption of the first
	 * key attribute.
	 */
	if (indexRel->rd_indoption[0] & INDOPTION_HASH)
		return SORTBY_HASH;
	if (indexRel->rd_indoption[0] & INDOPTION_DESC)
		return SORTBY_DESC;
	return SORTBY_ASC;
}

/*
 * Determine if the unsafe truncate (i.e., without table rewrite) should
 * be used for a given relation and its indexes.
 * Also provide the reason why unsafe truncate is used. The reason is used to
 * provide appropriate error messages to the users in case unsafe truncate
 * cannot be used.
 */
YbTruncateType
YbUseUnsafeTruncate(Relation rel)
{
	if (!IsYBRelation(rel))
		return YB_SAFE_TRUNCATE;

	if (IsSystemRelation(rel))
		return YB_UNSAFE_TRUNCATE_SYSTEM_RELATION;

	if (!yb_enable_alter_table_rewrite)
		return YB_UNSAFE_TRUNCATE_TABLE_REWRITE_DISABLED;

	return YB_SAFE_TRUNCATE;
}


/*
 * Given a table attribute number, get a corresponding index attribute number.
 * Throw an error if it is not found.
 */
AttrNumber
YbGetIndexAttnum(Relation index, AttrNumber table_attno)
{
	for (int i = 0; i < IndexRelationGetNumberOfAttributes(index); ++i)
	{
		if (table_attno == index->rd_index->indkey.values[i])
			return i + 1;
	}
	elog(ERROR, "column is not in index");
}

Oid
YbGetDatabaseOidToIncrementCatalogVersion()
{
	if (!YBIsDBCatalogVersionMode())
		return Template1DbOid;
	if (OidIsValid(ddl_transaction_state.database_oid))
		return ddl_transaction_state.database_oid;
	return MyDatabaseId;
}

bool
YbApplyInvalidationMessages(YbcCatalogMessageLists *message_lists)
{
	/*
	 * First pass: run through all the lists to ensure that every message
	 * can be applied. Note that each list of messages are generated by a
	 * DDL transaction and therefore must be applied all together. If any
	 * one of the messages cannot be applied, there is no point to apply
	 * any of the others because a catalog cache refresh is needed anyway.
	 */
	for (YbcCatalogMessageList *msglist = message_lists->message_lists;
		 msglist < message_lists->message_lists + message_lists->num_lists;
		 ++msglist)
	{
		/* Check the current message list at invalMessages. */
		const SharedInvalidationMessage *invalMessages =
			(const SharedInvalidationMessage *) msglist->message_list;

		elog(yb_debug_log_catcache_events ? LOG : DEBUG1, "invalMessages=%p, msglist->num_bytes=%zu",
			 invalMessages, msglist->num_bytes);
		if (!invalMessages)
		{
			elog(LOG, "pg null message");
			/*
			 * This is a PG null value for the messages column in the
			 * pg_yb_invalidation_message table. We will need catalog
			 * cache refresh in this case because we failed to generate
			 * or get the invalidation messages. For example, in PITR
			 * restore, we only increment the catalog version without
			 * generating a list of invalidation messages.
			 */
			return false;
		}

		/*
		 * If msglist->num_bytes is 0, this is a PG empty string '' which
		 * is a special case where we only update pg_yb_catalog_version
		 * table to just bump up the catalog version without executing any
		 * DDL to change any of the PG catalog state. Because in this case
		 * there is no catalog change at all and we consider this case as
		 * successfully applied.
		 */
		if (msglist->num_bytes == 0)
		{
			elog(LOG, "empty string message");
			continue;
		}

		/*
		 * Sanity check if the list of messages were generated by a PG backend
		 * from a different release where the sizeof SharedInvalidationMessage
		 * has changed, we need catalog cache refresh.
		 */
		if (msglist->num_bytes % sizeof(SharedInvalidationMessage) != 0)
		{
			elog(WARNING, "size of SharedInvalidationMessage mismatch");
			return false;
		}

		size_t		nmsgs = msglist->num_bytes / sizeof(SharedInvalidationMessage);

		if (log_min_messages <= DEBUG1 || yb_debug_log_catcache_events)
			YbLogInvalidationMessages(invalMessages, nmsgs);
		for (size_t i = 0; i < nmsgs; ++i)
			/*
			 * If the message cannot be applied, we need catalog cache refresh.
			 */
			if (!YbCanApplyMessage(invalMessages + i))
				return false;
	}

	/* Second pass: run through all the lists and apply every message. */
	pid_t		mypid = getpid();

	for (YbcCatalogMessageList *msglist = message_lists->message_lists;
		 msglist < message_lists->message_lists + message_lists->num_lists;
		 ++msglist)
	{
		SharedInvalidationMessage *invalMessages =
			(SharedInvalidationMessage *) msglist->message_list;
		size_t		nmsgs = msglist->num_bytes / sizeof(SharedInvalidationMessage);

		for (SharedInvalidationMessage *msg = invalMessages;
			 msg < invalMessages + nmsgs; ++msg)
		{
			if (msg->id >= SysCacheSize)
			{
				/*
				 * This represents a message to invalidate a new catcache from
				 * a newer release that does not exist in this backend.
				 */
				elog(WARNING, "skip non-existent catcache %d", msg->id);
				continue;
			}

			/*
			 * Set yb_sender_pid to mypid because LocalExecuteInvalidationMessage
			 * can only apply a message when its yb_sender_pid indicates that it
			 * is sent by this process.
			 */
			msg->yb_header.yb_sender_pid = mypid;
			LocalExecuteInvalidationMessage(msg);
		}
	}
	return true;
}

bool
YbInvalidationMessagesTableExists()
{
	static bool cached_invalidation_messages_table_exists = false;

	if (cached_invalidation_messages_table_exists)
		return true;
	HandleYBStatus(YBCPgTableExists(Template1DbOid,
									YbInvalidationMessagesRelationId,
									&cached_invalidation_messages_table_exists));
	return cached_invalidation_messages_table_exists;
}

bool		yb_is_calling_internal_sql_for_ddl = false;
bool		yb_is_internal_connection = false;
char *
YbGetPotentiallyHiddenOidText(Oid oid)
{
	if (*YBCGetGFlags()->TEST_hide_details_for_pg_regress)
		return "<oid_hidden_for_pg_regress>";
	else
	{
		char	   *oid_text = palloc(11 * sizeof(char));

		sprintf(oid_text, "%u", oid);
		/*
		 * It is expected the caller uses this string in an error message, so
		 * the palloc'd memory will get freed via memory context free.
		 */
		return oid_text;
	}
}

bool
YbRefreshMatviewInPlace()
{
	return yb_refresh_matview_in_place ||
		YBCPgYsqlMajorVersionUpgradeInProgress();
}

static bool
YbHasDdlMadeChanges()
{
	return YBCPgHasWriteOperationsInDdlTxnMode() ||
		ddl_transaction_state.current_stmt_node_tag == T_TransactionStmt ||
		ddl_transaction_state.force_send_inval_messages;
}

void
YbForceSendInvalMessages()
{
	ddl_transaction_state.force_send_inval_messages = true;
}

/*
 * Scale the ru_maxrss value according to the platform.
 * On Linux, the maxrss is in kilobytes.
 * On OSX, the maxrss is in bytes and scale it to kilobytes.
 * https://www.manpagez.com/man/2/getrusage/osx-10.12.3.php
 */
static long
scale_rss_to_kb(long maxrss)
{
#ifdef __APPLE__
	maxrss = maxrss / 1024;
#endif
	return maxrss;
}

long
YbGetPeakRssKb()
{
	struct rusage r;

	getrusage(RUSAGE_SELF, &r);
	return scale_rss_to_kb(r.ru_maxrss);
}

bool
YbIsAnyDependentGeneratedColPK(Relation rel, AttrNumber attnum)
{
	AttrNumber	offset = YBGetFirstLowInvalidAttributeNumber(rel);
	Bitmapset  *target_cols = bms_make_singleton(attnum - offset);
	Bitmapset  *dependent_generated_cols =
		get_dependent_generated_columns(NULL /* root */ , 0 /* rti */ ,
										target_cols,
										NULL /* yb_generated_cols_source */ ,
										rel);
	int			bms_index;

	while ((bms_index = bms_first_member(dependent_generated_cols)) >= 0)
	{
		AttrNumber	dependent_attnum = bms_index + offset;

		if (YbIsAttrPrimaryKeyColumn(rel, dependent_attnum))
			return true;
	}
	bms_free(dependent_generated_cols);
	bms_free(target_cols);

	return false;
}

bool
YbCheckTserverResponseCacheForAuthGflags()
{
	/*
	 * Do not use tserver cache if we do not have incremental catalog cache
	 * refresh because the cost of global-impact DDLs (which is needed to
	 * use tserver cache for auth processing) is too high.
	 */
	return
		*YBCGetGFlags()->ysql_enable_read_request_caching &&
		*YBCGetGFlags()->ysql_enable_read_request_cache_for_connection_auth &&
		yb_enable_invalidation_messages;
}

bool
YbUseTserverResponseCacheForAuth(uint64_t shared_catalog_version)
{
	if (!YbIsAuthBackend())
		return false;
	/* We should only see auth backend if connection manager is enabled. */
	Assert(YbIsYsqlConnMgrEnabled());

	if (!YbCheckTserverResponseCacheForAuthGflags())
		return false;

	/*
	 * For now we do not allow using tserver response cache for auth processing
	 * if login profile is enabled. This is because the login process itself
	 * writes to pg_yb_role_profile table but this is not done under a DDL
	 * statement context. As a result the catalog version isn't incremented
	 * but the tserver response cache becomes stale. Newer login processing
	 * will continue to use the stale cache which isn't right.
	 */
	if (*YBCGetGFlags()->ysql_enable_profile && YbLoginProfileCatalogsExist)
		return false;

	/*
	 * Tserver response cache requires a valid catalog version. Use the shared
	 * memory catalog version as an approximation of the latest master catalog
	 * version.
	 */
	if (shared_catalog_version == YB_CATCACHE_VERSION_UNINITIALIZED)
		return false;
	return true;
}

bool
YbCatalogPreloadRequired()
{
	return YbNeedAdditionalCatalogTables() || !*YBCGetGFlags()->ysql_use_relcache_file;
}

bool
YbUseMinimalCatalogCachesPreload()
{
	if (*YBCGetGFlags()->ysql_minimal_catalog_caches_preload)
		return true;
	if (YbNeedAdditionalCatalogTables())
		return false;
	if (yb_is_internal_connection)
		return true;
	return false;
}

/* Comparison function for sorting strings in a List */
static int
string_list_compare(const ListCell *a, const ListCell *b)
{
	return strcmp((char *) lfirst(a), (char *) lfirst(b));
}

/*
 * Returns the metadata for all tablets in the cluster.
 * The returned data structure is a row type with the following columns:
 * - tablet_id: text
 * - object_uuid: text
 * - namespace: text
 * - object_name: text
 * - type: text
 * - start_hash_code: int32
 * - end_hash_code: int32
 * - leader: text
 * - replicas: text[]
 *
 * The start_hash_code and end_hash_code are the hash codes of the start and end
 * keys of the tablet for hash sharded tables. Leader is provided as a separate
 * column for simpler querying and self-explanatory access.
 */
Datum
yb_get_tablet_metadata(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	static int	ncols = 9;

	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that "
						"cannot accept a set")));

	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not "
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
				(errmsg("return type must be a row type")));

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	YbcPgGlobalTabletsDescriptor *tablets = NULL;
	size_t		num_tablets = 0;

	HandleYBStatus(YBCTabletsMetadata(&tablets, &num_tablets));

	for (int i = 0; i < num_tablets; ++i)
	{
		YbcPgGlobalTabletsDescriptor *tablet = (YbcPgGlobalTabletsDescriptor *) tablets + i;
		YbcPgTabletsDescriptor *tablet_descriptor = &tablet->tablet_descriptor;
		Datum		values[ncols];
		bool		nulls[ncols];

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		values[0] = CStringGetTextDatum(tablet_descriptor->tablet_id);
		values[1] = CStringGetTextDatum(tablet_descriptor->table_id);
		values[2] = CStringGetTextDatum(tablet_descriptor->namespace_name);
		values[3] = CStringGetTextDatum(tablet_descriptor->table_name);
		values[4] = CStringGetTextDatum(tablet_descriptor->table_type);

		if (tablet->is_hash_partitioned)
		{
			values[5] =
				UInt16GetDatum(YBCDecodeMultiColumnHashLeftBound(tablet_descriptor->partition_key_start,
																 tablet_descriptor->partition_key_start_len));	/* start_hash is
																												 * inclusive */
			values[6] =
				UInt16GetDatum(YBCDecodeMultiColumnHashRightBound(tablet_descriptor->partition_key_end,
																  tablet_descriptor->partition_key_end_len) + 1);	/* end_hash is exclusive */
		}
		else
		{
			nulls[5] = true;
			nulls[6] = true;
		}

		/* Convert replicas array to PostgreSQL text array */
		if (tablet->replicas_count > 0)
		{
			Assert(tablet->replicas != NULL);

			/* The last replica is the leader. */
			values[7] = CStringGetTextDatum(tablet->replicas[tablet->replicas_count - 1]);

			/* Convert char ** to List * */
			List	   *replicas_list = NIL;

			for (size_t idx = 0; idx < tablet->replicas_count; idx++)
				replicas_list = lappend(replicas_list, (char *) tablet->replicas[idx]);

			/*
			 * Sort the list lexicographically for consistency, so that all rows
			 * with same replicas have same entries.
			 */
			list_sort(replicas_list, string_list_compare);
			values[8] = PointerGetDatum(strlist_to_textarray(replicas_list));
		}
		else
		{
			nulls[7] = true;
			nulls[8] = true;
		}

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	MemoryContextSwitchTo(oldcontext);
	return (Datum) 0;
}

YbcPgStatement
YbNewSample(Relation rel,
			int targrows,
			double rstate_w,
			uint64_t rand_state_s0,
			uint64_t rand_state_s1)
{
	YbcPgStatement result = NULL;
	HandleYBStatus(YBCPgNewSample(YBCGetDatabaseOid(rel), YbGetRelfileNodeId(rel),
								  YbBuildTableLocalityInfo(rel), targrows, rstate_w, rand_state_s0,
								  rand_state_s1, &result));
	return result;
}

YbcPgStatement
YbNewSelect(Relation rel, const YbcPgPrepareParameters *prepare_params)
{
	YbcPgStatement result = NULL;
	HandleYBStatus(YBCPgNewSelect(YBCGetDatabaseOid(rel), YbGetRelfileNodeId(rel), prepare_params,
								  YbBuildTableLocalityInfo(rel), &result));
	return result;
}

YbcPgStatement
YbNewUpdateForDb(Oid db_oid, Relation rel, YbcPgTransactionSetting transaction_setting)
{
	YbcPgStatement result = NULL;
	HandleYBStatus(YBCPgNewUpdate(db_oid, YbGetRelfileNodeId(rel),
								  YbBuildTableLocalityInfo(rel), &result, transaction_setting));
	return result;
}

YbcPgStatement
YbNewUpdate(Relation rel, YbcPgTransactionSetting transaction_setting)
{
	return YbNewUpdateForDb(YBCGetDatabaseOid(rel), rel, transaction_setting);
}

YbcPgStatement
YbNewDelete(Relation rel, YbcPgTransactionSetting transaction_setting)
{
	YbcPgStatement result = NULL;
	HandleYBStatus(YBCPgNewDelete(YBCGetDatabaseOid(rel), YbGetRelfileNodeId(rel),
								  YbBuildTableLocalityInfo(rel), &result, transaction_setting));
	return result;
}

YbcPgStatement
YbNewInsertForDb(Oid db_oid, Relation rel, YbcPgTransactionSetting transaction_setting)
{
	YbcPgStatement result = NULL;
	HandleYBStatus(YBCPgNewInsert(db_oid, YbGetRelfileNodeId(rel),
								  YbBuildTableLocalityInfo(rel), &result, transaction_setting));
	return result;
}

YbcPgStatement
YbNewInsert(Relation rel, YbcPgTransactionSetting transaction_setting)
{
	return YbNewInsertForDb(YBCGetDatabaseOid(rel), rel, transaction_setting);
}

extern YbcPgStatement
YbNewInsertBlock(Relation rel, YbcPgTransactionSetting transaction_setting)
{
	YbcPgStatement result = NULL;
	HandleYBStatus(YBCPgNewInsertBlock(YBCGetDatabaseOid(rel), YbGetRelfileNodeId(rel),
									   YbBuildTableLocalityInfo(rel), transaction_setting,
									   &result));
	return result;
}

static YbcStatus
YbNewTruncateColocatedImpl(Relation rel, YbcPgTransactionSetting transaction_setting,
						   YbcPgStatement *result)
{
	return YBCPgNewTruncateColocated(YBCGetDatabaseOid(rel), YbGetRelfileNodeId(rel),
									 YbBuildTableLocalityInfo(rel), result, transaction_setting);
}

YbcPgStatement
YbNewTruncateColocated(Relation rel, YbcPgTransactionSetting transaction_setting)
{
	YbcPgStatement result = NULL;
	HandleYBStatus(YbNewTruncateColocatedImpl(rel, transaction_setting, &result));
	return result;
}

YbcPgStatement
YbNewTruncateColocatedIgnoreNotFound(Relation rel, YbcPgTransactionSetting transaction_setting)
{
	bool not_found = false;
	YbcPgStatement result = NULL;
	HandleYBStatusIgnoreNotFound(YbNewTruncateColocatedImpl(rel, transaction_setting, &result),
								 &not_found);
	return not_found ? NULL : result;
}
