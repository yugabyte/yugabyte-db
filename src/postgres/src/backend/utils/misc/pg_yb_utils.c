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
#include <sys/types.h>
#include <unistd.h>

#include "c.h"
#include "postgres.h"
#include "miscadmin.h"
#include "access/htup.h"
#include "access/htup_details.h"
#include "access/sysattr.h"
#include "access/tupdesc.h"
#include "access/xact.h"
#include "executor/ybcExpr.h"
#include "catalog/indexing.h"
#include "catalog/pg_am.h"
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
#include "catalog/pg_rewrite.h"
#include "catalog/pg_trigger.h"
#include "catalog/pg_type.h"
#include "catalog/pg_yb_catalog_version.h"
#include "catalog/catalog.h"
#include "catalog/yb_catalog_version.h"
#include "catalog/yb_type.h"
#include "commands/dbcommands.h"
#include "commands/defrem.h"
#include "common/pg_yb_common.h"
#include "lib/stringinfo.h"
#include "optimizer/cost.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/lsyscache.h"
#include "utils/pg_locale.h"
#include "utils/rel.h"
#include "utils/spccache.h"
#include "utils/syscache.h"
#include "fmgr.h"
#include "funcapi.h"
#include "mb/pg_wchar.h"

#include "yb/common/ybc_util.h"
#include "yb/yql/pggate/ybc_pggate.h"

#ifdef __linux__
#include <sys/prctl.h>
#endif

uint64_t yb_catalog_cache_version = YB_CATCACHE_VERSION_UNINITIALIZED;

YbTserverCatalogInfo yb_tserver_catalog_info = NULL;

/*
 * Shared memory array db_catalog_versions_ index of the slot allocated for the
 * MyDatabaseId.
 */
int yb_my_database_id_shm_index = -1;

uint64_t YBGetActiveCatalogCacheVersion() {
	if (yb_catalog_version_type == CATALOG_VERSION_CATALOG_TABLE &&
	    YBGetDdlNestingLevel() > 0)
		return yb_catalog_cache_version + 1;

	return yb_catalog_cache_version;
}

void YBResetCatalogVersion() {
  yb_catalog_cache_version = YB_CATCACHE_VERSION_UNINITIALIZED;
}

/** These values are lazily initialized based on corresponding environment variables. */
int ybc_pg_double_write = -1;
int ybc_disable_pg_locking = -1;

/* Forward declarations */
static void YBCInstallTxnDdlHook();

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

bool
YBNeedRetryAfterCacheRefresh(ErrorData *edata)
{
	// TODO Inspect error code to distinguish retryable errors.
	return true;
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
	return attnum - YBGetFirstLowInvalidAttributeNumber(rel);
}

AttrNumber YBBmsIndexToAttnum(Relation rel, int idx)
{
	return idx + YBGetFirstLowInvalidAttributeNumber(rel);
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

	/* Get the primary key columns 'pkey' from YugaByte. */
	HandleYBStatus(YBCPgGetTableDesc(dboid, YbGetStorageRelid(rel), &ybc_tabledesc));
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

	return pkey;
}

Bitmapset *YBGetTablePrimaryKeyBms(Relation rel)
{
	return GetTablePrimaryKeyBms(rel,
	                             YBGetFirstLowInvalidAttributeNumber(rel) /* minattr */,
	                             false /* includeYBSystemColumns */);
}

Bitmapset *YBGetTableFullPrimaryKeyBms(Relation rel)
{
	return GetTablePrimaryKeyBms(rel,
	                             YBSystemFirstLowInvalidAttributeNumber + 1 /* minattr */,
	                             true /* includeYBSystemColumns */);
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
YbIsDatabaseColocated(Oid dbid)
{
	bool colocated;
	HandleYBStatus(YBCPgIsDatabaseColocated(dbid, &colocated));
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
IsYBReadCommitted()
{
	static int cached_value = -1;
	if (cached_value == -1)
	{
		cached_value = YBCIsEnvVarTrueWithDefault("FLAGS_yb_enable_read_committed_isolation", false);
	}
	return IsYugaByteEnabled() && cached_value &&
				 (XactIsoLevel == XACT_READ_COMMITTED || XactIsoLevel == XACT_READ_UNCOMMITTED);
}

bool
YBSavepointsEnabled()
{
	static int cached_value = -1;
	if (cached_value == -1)
	{
		cached_value = YBCIsEnvVarTrueWithDefault("FLAGS_enable_pg_savepoints", true);
	}
	return IsYugaByteEnabled() && YBTransactionsEnabled() && cached_value;
}

bool
YBIsDBCatalogVersionMode()
{
	static int cached_value = -1;
	if (cached_value == -1)
	{
		cached_value = YBCIsEnvVarTrueWithDefault(
			"FLAGS_TEST_enable_db_catalog_version_mode", false);
	}
	/*
	 * During initdb (bootstrap mode), CATALOG_VERSION_PROTOBUF_ENTRY is used
	 * for catalog version type.
	 */
	return IsYugaByteEnabled() &&
		   YbGetCatalogVersionType() == CATALOG_VERSION_CATALOG_TABLE &&
		   cached_value;
}

void
YBReportFeatureUnsupported(const char *msg)
{
	ereport(ERROR,
			(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			 errmsg("%s", msg)));
}


static bool
YBShouldReportErrorStatus()
{
	static int cached_value = -1;
	if (cached_value == -1)
	{
		cached_value = YBCIsEnvVarTrue("YB_PG_REPORT_ERROR_STATUS");
	}

	return cached_value;
}

void
HandleYBStatus(YBCStatus status)
{
   HandleYBStatusAtErrorLevel(status, ERROR);
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

void HandleYBStatusAtErrorLevel(YBCStatus status, int error_level)
{
	if (!status)
		return;

	/* Build message in the current memory context. */
	const char* msg_buf = BuildYBStatusMessage(
			status, &FetchUniqueConstraintName);

	if (YBShouldReportErrorStatus())
		YBC_LOG_ERROR("HandleYBStatus: %s", msg_buf);

	const uint32_t pg_err_code = YBCStatusPgsqlError(status);
	const uint16_t txn_err_code = YBCStatusTransactionError(status);
	YBCFreeStatus(status);
	ereport(error_level,
			(errmsg("%s", msg_buf),
			 errcode(pg_err_code),
			 yb_txn_errcode(txn_err_code),
			 errhidecontext(true)));
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

void
YBInitPostgresBackend(
	const char *program_name,
	const char *db_name,
	const char *user_name)
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
		YBCInitPgGate(type_table, count, callbacks);
		YBCInstallTxnDdlHook();

		/*
		 * For each process, we create one YBC session for PostgreSQL to use
		 * when accessing YugaByte storage.
		 *
		 * TODO: do we really need to DB name / username here?
		 */
		HandleYBStatus(YBCPgInitSession(db_name ? db_name : user_name));
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
	if (YBSavepointsEnabled())
		HandleYBStatus(YBCPgSetActiveSubTransaction(id));
}

void
YBCRollbackToSubTransaction(SubTransactionId id)
{
	if (YBSavepointsEnabled())
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
bool yb_enable_expression_pushdown = false;
bool yb_enable_optimizer_statistics = false;
bool yb_make_next_ddl_statement_nonbreaking = false;
bool yb_plpgsql_disable_prefetch_in_for_query = false;

//------------------------------------------------------------------------------
// YB Debug utils.

bool yb_debug_report_error_stacktrace = false;

bool yb_debug_log_catcache_events = false;

bool yb_debug_log_internal_restarts = false;

bool yb_test_system_catalogs_creation = false;

bool yb_test_fail_next_ddl = false;

const char*
YBDatumToString(Datum datum, Oid typid)
{
	Oid			typoutput = InvalidOid;
	bool		typisvarlena = false;

	getTypeOutputInfo(typid, &typoutput, &typisvarlena);
	return OidOutputFunctionCall(typoutput, datum);
}

const char*
YBHeapTupleToString(HeapTuple tuple, TupleDesc tupleDesc)
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
static int ddl_nesting_level = 0;

static void
YBResetEnableNonBreakingDDLMode()
{
	/*
	 * Reset yb_make_next_ddl_statement_nonbreaking to avoid its further side
	 * effect that may not be intended.
	 */
	yb_make_next_ddl_statement_nonbreaking = false;
}

static void
YBResetDdlState()
{
	ddl_nesting_level = 0;
	YBResetEnableNonBreakingDDLMode();
	YBCPgClearSeparateDdlTxnMode();
}

int
YBGetDdlNestingLevel()
{
	return ddl_nesting_level;
}

void
YBIncrementDdlNestingLevel()
{
	if (ddl_nesting_level == 0)
	{
		HandleYBStatus(YBCPgEnterSeparateDdlTxnMode());
	}
	ddl_nesting_level++;
}

void
YBDecrementDdlNestingLevel(bool is_catalog_version_increment,
						   bool is_breaking_catalog_change)
{
	ddl_nesting_level--;
	if (ddl_nesting_level == 0)
	{
		YBResetEnableNonBreakingDDLMode();
		const bool increment_done =
			is_catalog_version_increment &&
			YBCPgHasWriteOperationsInDdlTxnMode() &&
			YbIncrementMasterCatalogVersionTableEntry(is_breaking_catalog_change);

		HandleYBStatus(YBCPgExitSeparateDdlTxnMode());

		/*
		 * Optimization to avoid redundant cache refresh on the current session
		 * since we should have already updated the cache locally while
		 * applying the DDL changes.
		 * (Doing this after YBCPgExitSeparateDdlTxnMode so it only executes
		 * if DDL txn commit succeeds.)
		 */
		if (increment_done)
		{
			yb_catalog_cache_version += 1;
			if (*YBCGetGFlags()->log_ysql_catalog_versions)
				ereport(LOG,
						(errmsg("%s: set local catalog version: %" PRIu64,
								__func__, yb_catalog_cache_version)));
		}

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

bool IsTransactionalDdlStatement(PlannedStmt *pstmt,
                                 bool *is_catalog_version_increment,
                                 bool *is_breaking_catalog_change)
{
	/* Assume the worst. */
	*is_catalog_version_increment = true;
	*is_breaking_catalog_change = true;

	bool is_ddl = true;
	Node *parsetree = GetActualStmtNode(pstmt);

	NodeTag node_tag = nodeTag(parsetree);
	switch (node_tag) {
		// The lists of tags here have been generated using e.g.:
		// cat $( find src/postgres -name "nodes.h" ) | grep "T_Create" | sort | uniq |
		//   sed 's/,//g' | while read s; do echo -e "\t\tcase $s:"; done
		// All T_Create... tags from nodes.h:

		case T_CreateDomainStmt:
		case T_CreateEnumStmt:
		case T_CreateTableGroupStmt:
		case T_CreateTableSpaceStmt:
		case T_CreatedbStmt:
		case T_DefineStmt: // CREATE OPERATOR/AGGREGATE/COLLATION/etc
		case T_CommentStmt: // COMMENT (create new comment)
		case T_DiscardStmt: // DISCARD ALL/SEQUENCES/TEMP affects only objects of current connection
		case T_RuleStmt: // CREATE RULE
		case T_TruncateStmt: // TRUNCATE changes system catalog in case of non-YB (i.e. TEMP) tables
		{
			/*
			 * Simple add objects are not breaking changes, and they do not even require
			 * a version increment because we do not do any negative caching for them.
			 */
			*is_catalog_version_increment = false;
			*is_breaking_catalog_change = false;
			break;
		}
		case T_ViewStmt: // CREATE VIEW
		{
			/*
			 * For system catalog additions we need to force cache refresh
			 * because of negative caching of pg_class and pg_type
			 * (see SearchCatCacheMiss).
			 * Concurrent transaction needs not to be aborted though.
			 */
			if (IsYsqlUpgrade &&
				YbIsSystemNamespaceByName(castNode(ViewStmt, parsetree)->view->schemaname))
			{
				*is_breaking_catalog_change = false;
				break;
			}

			*is_catalog_version_increment = false;
			*is_breaking_catalog_change = false;
			break;
		}

		case T_CompositeTypeStmt: // CREATE TYPE
		case T_CreateAmStmt:
		case T_CreateCastStmt:
		case T_CreateConversionStmt:
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
		case T_CreateRangeStmt:
		case T_CreateReplicationSlotCmd:
		case T_CreateRoleStmt:
		case T_CreateSchemaStmt:
		case T_CreateStatsStmt:
		case T_CreateSubscriptionStmt:
		case T_CreateTableAsStmt:
		case T_CreateTransformStmt:
		case T_CreateTrigStmt:
		case T_CreateUserMappingStmt:
		{
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
			*is_breaking_catalog_change = false;
			break;
		}
		case T_CreateStmt:
		{
			CreateStmt *stmt = castNode(CreateStmt, parsetree);
			ListCell *lc = NULL;
			/*
			 * If a partition table is being created, this means pg_inherits
			 * table that is being cached should be invalidated. If the cache
			 * is not invalidated here, it is possible that one connection
			 * could create a new partition and insert data into it without
			 * the other connections knowing about this. However, due to
			 * snapshot isolation guarantees, transactions that are already
			 * underway need not abort.
			 */
			if (stmt->partbound != NULL) {
				*is_breaking_catalog_change = false;
				break;
			}

			/*
			 * For system catalog additions we need to force cache refresh
			 * because of negative caching of pg_class and pg_type
			 * (see SearchCatCacheMiss).
			 * Concurrent transaction needs not to be aborted though.
			 */
			if (IsYsqlUpgrade &&
				YbIsSystemNamespaceByName(stmt->relation->schemaname))
			{
				*is_breaking_catalog_change = false;
				break;
			}

			foreach (lc, stmt->constraints)
			{
				Constraint *con = lfirst(lc);
				if (con->contype == CONSTR_FOREIGN)
				{
					/*
					 * Increment catalog version as it effectively alters the referenced table.
					 * TODO Technically this could also be a breaking change in case we have
					 * ongoing transactions affecting the referenced table.
					 * But we do not support consistent FK checks (w.r.t concurrent
					 * writes) yet anyway and the (upcoming) online, async
					 * implementation should wait for ongoing transactions so we do not
					 * have to force a transaction abort on PG side.
					 */
					*is_breaking_catalog_change = false;
					break;
				}
			}

			/*
			 * If no FK constraints, this is a simple add object so nothing to
			 * do (due to no negative caching).
			 */
			*is_catalog_version_increment = false;
			*is_breaking_catalog_change = false;
			break;
		}
		case T_CreateSeqStmt:
		{
			CreateSeqStmt *stmt = castNode(CreateSeqStmt, parsetree);
			/* Need to increment if owner is set to ensure its dependency cache is updated. */
			*is_breaking_catalog_change = false;
			if (stmt->ownerId == InvalidOid)
			{
				*is_catalog_version_increment = false;
			}
			break;
		}
		case T_CreateFunctionStmt:
		{
			CreateFunctionStmt *stmt = castNode(CreateFunctionStmt, parsetree);
			*is_breaking_catalog_change = false;
			if (!stmt->replace)
			{
				*is_catalog_version_increment = false;
			}
			break;
		}

		// All T_Drop... tags from nodes.h:
		case T_DropOwnedStmt:
		case T_DropReplicationSlotCmd:
		case T_DropRoleStmt:
		case T_DropSubscriptionStmt:
		case T_DropTableSpaceStmt:
		case T_DropUserMappingStmt:
			break;

		case T_DropStmt:
			*is_breaking_catalog_change = false;
			break;

		case T_DropdbStmt:
		    /*
			 * We already invalidate all connections to that DB by dropping it
			 * so nothing to do on the cache side.
			 */
			*is_breaking_catalog_change = false;
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
		case T_AlterRoleStmt:
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

		case T_AlterTableStmt:
		{
			*is_breaking_catalog_change = false;
			break;
		}

		// T_Grant...
		case T_GrantStmt:
		{
			/* Grant (add permission) is not a breaking change, but revoke is. */
			GrantStmt *stmt = castNode(GrantStmt, parsetree);
			*is_breaking_catalog_change = !stmt->is_grant;
			break;
		}
		case T_GrantRoleStmt:
		{
			/* Grant (add permission) is not a breaking change, but revoke is. */
			GrantRoleStmt *stmt = castNode(GrantRoleStmt, parsetree);
			*is_breaking_catalog_change = !stmt->is_grant;
			break;
		}

		// T_Index...
		case T_IndexStmt:
			/*
			 * For nonconcurrent index backfill we do not guarantee global consistency anyway.
			 * For (new) concurrent backfill the backfill process should wait for ongoing
			 * transactions so we don't have to force a transaction abort on PG side.
			 */
			*is_breaking_catalog_change = false;
			break;

		case T_VacuumStmt:
			/* Vacuum with analyze updates relation and attribute statistics */
			*is_catalog_version_increment = false;
			*is_breaking_catalog_change = false;
			is_ddl = castNode(VacuumStmt, parsetree)->options & VACOPT_ANALYZE;
			break;

		case T_RefreshMatViewStmt:
			break;

		case T_ReindexStmt:
			/*
			 * Does not need catalog version increment since only data changes,
			 * not metadata--unless the data itself is metadata (system index).
			 * It could be nice to force a cache refresh when fixing a system
			 * index corruption, but just because a system index is REINDEXed
			 * doesn't mean it had a corruption.  If there's a system index
			 * corruption, manual intervention is already needed, so might as
			 * well let the user deal with refreshing clients.
			 */
			*is_catalog_version_increment = false;
			*is_breaking_catalog_change = false;
			break;

		default:
			/* Not a DDL operation. */
			*is_catalog_version_increment = false;
			*is_breaking_catalog_change = false;
			is_ddl = false;
			break;
	}

	/*
	 * If yb_make_next_ddl_statement_nonbreaking is true, then no DDL statement
	 * will cause a breaking catalog change.
	 */
	if (yb_make_next_ddl_statement_nonbreaking)
		*is_breaking_catalog_change = false;

	/*
	 * For DDL, it does not make sense to get breaking catalog change without
	 * catalog version increment.
	 */
	Assert(!(is_ddl &&
			 *is_breaking_catalog_change &&
			 !*is_catalog_version_increment));

	return is_ddl;
}

static void YBTxnDdlProcessUtility(
		PlannedStmt *pstmt,
		const char *queryString,
		ProcessUtilityContext context,
		ParamListInfo params,
		QueryEnvironment *queryEnv,
		DestReceiver *dest,
		char *completionTag) {

	/* Assuming this is a breaking change by default. */
	bool is_catalog_version_increment = true;
	bool is_breaking_catalog_change = true;
	bool is_txn_ddl = IsTransactionalDdlStatement(pstmt,
	                                              &is_catalog_version_increment,
	                                              &is_breaking_catalog_change);

	if (is_txn_ddl) {
		YBIncrementDdlNestingLevel();
	}
	PG_TRY();
	{
		if (prev_ProcessUtility)
			prev_ProcessUtility(pstmt, queryString,
								context, params, queryEnv,
								dest, completionTag);
		else
			standard_ProcessUtility(pstmt, queryString,
									context, params, queryEnv,
									dest, completionTag);
	}
	PG_CATCH();
	{
		if (is_txn_ddl) {
			/*
			 * It is possible that ddl_nesting_level has wrong value due to error.
			 * Ddl transaction state should be reset.
			 */
			YBResetDdlState();
		}
		PG_RE_THROW();
	}
	PG_END_TRY();
	if (is_txn_ddl) {
		YBDecrementDdlNestingLevel(is_catalog_version_increment, is_breaking_catalog_change);
	}
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

bool YBReadFromFollowersEnabled() {
  return yb_read_from_followers;
}

int32_t YBFollowerReadStalenessMs() {
  return yb_follower_read_staleness_ms;
}

YBCPgYBTupleIdDescriptor* YBCCreateYBTupleIdDescriptor(Oid db_oid, Oid table_oid, int nattrs) {
	void* mem = palloc(sizeof(YBCPgYBTupleIdDescriptor) + nattrs * sizeof(YBCPgAttrValueDescriptor));
	YBCPgYBTupleIdDescriptor* result = mem;
	result->nattrs = nattrs;
	result->attrs = mem + sizeof(YBCPgYBTupleIdDescriptor);
	result->database_oid = db_oid;
	result->table_oid = table_oid;
	return result;
}

void YBCFillUniqueIndexNullAttribute(YBCPgYBTupleIdDescriptor* descr) {
	YBCPgAttrValueDescriptor* last_attr = descr->attrs + descr->nattrs - 1;
	last_attr->attr_num = YBUniqueIdxKeySuffixAttributeNumber;
	last_attr->type_entity = YbDataTypeFromOidMod(YBUniqueIdxKeySuffixAttributeNumber, BYTEAOID);
	last_attr->collation_id = InvalidOid;
	last_attr->is_null = true;
}

void YBTestFailDdlIfRequested() {
	if (!yb_test_fail_next_ddl)
		return;

	yb_test_fail_next_ddl = false;
	elog(ERROR, "DDL failed as requested");
}

static int YbGetNumberOfFunctionOutputColumns(Oid func_oid)
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
		return YBCStatusOKValue;
	}

	Oid dbid          = YBCGetDatabaseOid(rel);
	Oid storage_relid = YbGetStorageRelid(rel);

	YBCPgTableDesc desc = NULL;
	YBCStatus status = YBCPgGetTableDesc(dbid, storage_relid, &desc);
	if (!YBCStatusIsOK(status))
	{
		return status;
	}

	/* Relcache entry data must live in CacheMemoryContext */
	rel->yb_table_properties =
		MemoryContextAllocZero(CacheMemoryContext, sizeof(YbTablePropertiesData));

	HandleYBStatus(YBCPgGetTableProperties(desc, rel->yb_table_properties));

	return YBCStatusOKValue;
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
		if (unlikely(!YBCStatusIsOK(status)))
		{
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
		if (unlikely(!YBCStatusIsOK(status)))
		{
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

	YbTableProperties yb_props = YbTryGetTableProperties(rel);

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

	if (yb_props)
	{
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
 * Convert a string value to an SQL string literal and append it to
 * the given StringInfo.
 */
static void
appendStringLiteral(StringInfo buf, const char *str, int encoding)
{
	const char *s;
	int			num_quotes = 0;
	int			num_backslashes = 0;
	int 		len = strlen(str);
	int			input_len;

	/* Scan the string for characters that must be escaped. */
	for (s = str; (s - str) < strlen(str) && *s != '\0'; ++s)
	{
		if (*s == '\'')
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
	 */
	if (num_backslashes > 0)
	{
		appendStringInfoChar(buf, 'E');
	}

	/* Opening quote. */
	appendStringInfoChar(buf, '\'');

	/*
	 * Use fast path if possible.
	 *
	 * We've already verified that the input string is well-formed in the
	 * current encoding.  If it contains no quotes and, in the case of
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
			if (*s == '\'' || *s == '\\')
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
appendDatumToString(StringInfo str, uint64_t datum, Oid typid, int encoding)
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
			appendStringLiteral(str, datum_str, encoding);
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
				   YBCPgSplitDatum *split_datums)
{
	Assert(yb_table_properties->num_tablets > 1);

	/* Get key columns' YBCPgTypeEntity and YBCPgTypeAttrs */
	size_t num_range_key_columns = yb_table_properties->num_range_key_columns;
	const YBCPgTypeEntity *type_entities[num_range_key_columns];
	YBCPgTypeAttrs type_attrs_arr[num_range_key_columns];
	Relation rel = relation_open(relid, AccessShareLock);
	TupleDesc tupledesc = rel->rd_att;
	Bitmapset *pkey = YBGetTablePrimaryKeyBms(rel);
	AttrNumber attr_offset = YBGetFirstLowInvalidAttributeNumber(rel);

	int key_idx = 0;
	for (int i = 0; i < tupledesc->natts; ++i)
	{
		Form_pg_attribute attr = TupleDescAttr(tupledesc, i);
		/* Key column */
		if (bms_is_member(attr->attnum - attr_offset, pkey))
		{
			type_entities[key_idx] = YbDataTypeFromOidMod(InvalidAttrNumber,
														  attr->atttypid);
			YBCPgTypeAttrs type_attrs;
			type_attrs.typmod = attr->atttypmod;
			type_attrs_arr[key_idx] = type_attrs;
			pkeys_atttypid[key_idx] = attr->atttypid;
			++key_idx;
		}
	}

	/* Get Split point values as Postgres datums */
	HandleYBStatus(YBCGetSplitPoints(yb_tabledesc, type_entities,
									 type_attrs_arr, split_datums));
	relation_close(rel, AccessShareLock);
}

/*
 * This function constructs SPLIT AT VALUES clause for range-partitioned tables
 * with more than one tablet.
 */
static void
rangeSplitClause(Oid relid, YBCPgTableDesc yb_tabledesc,
				 YbTableProperties yb_table_properties, StringInfo str)
{
	Assert(yb_table_properties->num_tablets > 1);
	size_t num_range_key_columns = yb_table_properties->num_range_key_columns;
	size_t num_splits = yb_table_properties->num_tablets - 1;
	Oid pkeys_atttypid[num_range_key_columns];
	YBCPgSplitDatum split_datums[num_splits * num_range_key_columns];
	StringInfo prev_split_point = makeStringInfo();
	StringInfo cur_split_point = makeStringInfo();

	/* Get Split point values as Postgres datum */
	getSplitPointsInfo(relid, yb_tabledesc, yb_table_properties, pkeys_atttypid,
					   split_datums);

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
									pg_get_client_encoding());
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

Datum
yb_get_range_split_clause(PG_FUNCTION_ARGS)
{
	Oid			relid = PG_GETARG_OID(0);
	bool		exists_in_yb = false;
	YBCPgTableDesc yb_tabledesc = NULL;
	YbTablePropertiesData yb_table_properties;
	StringInfoData str;
	char	   *range_split_clause = NULL;

	HandleYBStatus(YBCPgTableExists(MyDatabaseId, relid, &exists_in_yb));
	if (!exists_in_yb)
	{
		elog(NOTICE, "relation with oid %u is not backed by YB", relid);
		PG_RETURN_NULL();
	}

	HandleYBStatus(YBCPgGetTableDesc(MyDatabaseId, relid, &yb_tabledesc));
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
	 */
	initStringInfo(&str);
	if (yb_table_properties.num_tablets > 1)
		rangeSplitClause(relid, yb_tabledesc, &yb_table_properties, &str);
	range_split_clause = str.data;

	PG_RETURN_CSTRING(range_split_clause);
}

Datum
yb_is_database_colocated(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(MyDatabaseColocated);
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
	 * and try again.  Both of these functions have the result buffer
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
		 * number down using GIN_NULL type.  Even if the column is collatable,
		 * nulls should be unaffected by collation.
		 *
		 * pg_trgm GIN indexes have key type int32 but also valid collation for
		 * regex purposes on the indexed type text.  Add an exception here for
		 * int32.  Since this relaxes the assert for other situations involving
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
#ifdef __linux__
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

Oid YbGetStorageRelid(Relation relation) {
	if (relation->rd_rel->relkind == RELKIND_MATVIEW &&
		relation->rd_rel->relfilenode != InvalidOid) {
		return relation->rd_rel->relfilenode;
	}
	return RelationGetRelid(relation);
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
	int db_id = MyDatabaseId;
	int sys_table_index_id = InvalidOid;

	switch(sys_table_id)
	{
		// TemplateDb tables
		case AuthMemRelationId:                           // pg_auth_members
			sys_table_index_id = AuthMemMemRoleIndexId;
			db_id = TemplateDbOid;
			break;
		case AuthIdRelationId:                            // pg_authid
			db_id = TemplateDbOid;
			sys_table_index_id = AuthIdRolnameIndexId;
			break;
		case DatabaseRelationId:                          // pg_database
			db_id = TemplateDbOid;
			sys_table_index_id = DatabaseNameIndexId;
			break;

		case DbRoleSettingRelationId:                     // pg_db_role_setting
			db_id = TemplateDbOid;
			break;

		// MyDb tables
		case AccessMethodProcedureRelationId:             // pg_amproc
			sys_table_index_id = AccessMethodProcedureIndexId;
			break;
		case AccessMethodRelationId:                      // pg_am AmNameIndexId
			sys_table_index_id = AmNameIndexId;
			break;
		case AttrDefaultRelationId:                       // pg_attrdef
			sys_table_index_id = AttrDefaultIndexId;
			break;
		case AttributeRelationId:                         // pg_attribute
			sys_table_index_id = AttributeRelidNameIndexId;
			break;
		case ConstraintRelationId:                        // pg_constraint
			sys_table_index_id = ConstraintRelidTypidNameIndexId;
			break;
		case IndexRelationId:                             // pg_index
			sys_table_index_id = IndexIndrelidIndexId;
			break;
		case InheritsRelationId:                          // pg_inherits
			sys_table_index_id = InheritsParentIndexId;
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
		case RelationRelationId:                          // pg_class
			sys_table_index_id = ClassNameNspIndexId;
			break;
		case RewriteRelationId:                           // pg_rewrite
			sys_table_index_id = RewriteRelRulenameIndexId;
			break;
		case TriggerRelationId:                           // pg_trigger
			sys_table_index_id = TriggerRelidNameIndexId;
			break;
		case TypeRelationId:                              // pg_type
			sys_table_index_id = TypeNameNspIndexId;
			break;

		case CastRelationId:        switch_fallthrough(); // pg_cast
		case PartitionedRelationId: switch_fallthrough(); // pg_partitioned_table
		case ProcedureRelationId:   break;                // pg_proc

		case YBCatalogVersionRelationId:                  // pg_yb_catalog_version
			db_id = YbMasterCatalogVersionTableDBOid();
			break;

		default:
		{
			ereport(FATAL,
			        (errcode(ERRCODE_INTERNAL_ERROR),
			         errmsg("Sys table '%d' is not yet inteded for preloading", sys_table_id)));

		}
	}
	YBCRegisterSysTableForPrefetching(db_id, sys_table_id, sys_table_index_id);
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
  if (strcmp(*newval, "tablet") == 0) {
    newConsistency = XCLUSTER_CONSISTENCY_TABLET;
  } else if (strcmp(*newval, "database") == 0) {
    newConsistency = XCLUSTER_CONSISTENCY_DATABASE;
  } else {
    return false;
  }

  *extra = malloc(sizeof(int));
  if (!*extra) return false;
  *((int*)*extra) = newConsistency;

  return true;
}

void assign_yb_xcluster_consistency_level(const char* newval, void* extra) {
  yb_xcluster_consistency_level = *((int*)extra);
}
