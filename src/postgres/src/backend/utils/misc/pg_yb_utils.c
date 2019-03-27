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

#include <sys/types.h>
#include <unistd.h>

#include "postgres.h"
#include "miscadmin.h"
#include "access/sysattr.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "catalog/pg_database.h"
#include "utils/builtins.h"
#include "catalog/pg_type.h"
#include "catalog/catalog.h"
#include "commands/dbcommands.h"

#include "pg_yb_utils.h"
#include "catalog/ybctype.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "common/pg_yb_common.h"

#include "utils/resowner_private.h"

YBCPgSession ybc_pg_session = NULL;

uint64 ybc_catalog_cache_version = 0;

/** These values are lazily initialized based on corresponding environment variables. */
int ybc_pg_double_write = -1;
int ybc_disable_pg_locking = -1;

YBCStatus ybc_commit_status = NULL;

bool
IsYugaByteEnabled()
{
	/* We do not support Init/Bootstrap processing modes yet. */
	return ybc_pg_session != NULL;
}

bool
IsYBRelation(Relation relation)
{
	const char relkind = relation->rd_rel->relkind;

	/* Currently only support regular tables and indexes */
	return (relkind == RELKIND_RELATION || relkind == RELKIND_INDEX);
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
IsYBRelationByKind(char relKind)
{
  return (relKind == RELKIND_RELATION || relKind == RELKIND_INDEX);
}

bool
YBNeedRetryAfterCacheRefresh(ErrorData *edata)
{
	// TODO Inspect error code to distinguish retryable errors.
	return true;
}

AttrNumber YBGetFirstLowInvalidAttributeNumber(Relation relation)
{
	return IsYugaByteEnabled() && IsYBRelation(relation)
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
	if (!status)
		return;
	if (YBShouldReportErrorStatus()) {
		YBC_LOG_ERROR("HandleYBStatus: %s", status->msg);
	}
	/* Copy the message to the current memory context and free the YBCStatus. */
	size_t status_len = strlen(status->msg);
	char* msg_buf = palloc(status_len + 1);
	strncpy(msg_buf, status->msg, status_len + 1);
	YBCFreeStatus(status);
	/* TODO: consider creating PostgreSQL error codes for YB statuses. */
	ereport(ERROR,
			(errcode(ERRCODE_INTERNAL_ERROR),
			 errmsg("%s", msg_buf)));
}

void
HandleYBStmtStatus(YBCStatus status, YBCPgStatement ybc_stmt)
{
	if (!status)
		return;

	if (ybc_stmt)
	{
		HandleYBStatus(YBCPgDeleteStatement(ybc_stmt));
	}
	HandleYBStatus(status);
}

void
HandleYBStmtStatusWithOwner(YBCStatus status,
                            YBCPgStatement ybc_stmt,
                            ResourceOwner owner)
{
	if (!status)
		return;

	if (ybc_stmt)
	{
		HandleYBStatus(YBCPgDeleteStatement(ybc_stmt));
		if (owner != NULL)
		{
			ResourceOwnerForgetYugaByteStmt(owner, ybc_stmt);
		}
	}
	HandleYBStatus(status);
}

void
HandleYBTableDescStatus(YBCStatus status, YBCPgTableDesc table)
{
	if (!status)
		return;

	if (table)
	{
		HandleYBStatus(YBCPgDeleteTableDesc(table));
	}
	HandleYBStatus(status);
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
		YBCGetTypeTable(&type_table, &count);
		YBCInitPgGate(type_table, count);

		if (ybc_pg_session != NULL) {
			YBC_LOG_FATAL("Double initialization of ybc_pg_session");
		}
		/*
		 * For each process, we create one YBC session for PostgreSQL to use
		 * when accessing YugaByte storage.
		 *
		 * TODO: do we really need to DB name / username here?
		 */
		if (db_name != NULL)
		{
			HandleYBStatus(YBCPgCreateSession(
				/* pg_env */ NULL, db_name, &ybc_pg_session));
		}
		else if (user_name != NULL)
		{
			HandleYBStatus(YBCPgCreateSession(
				/* pg_env */ NULL, user_name, &ybc_pg_session));
		}
	}
}

void
YBOnPostgresBackendShutdown()
{
	static bool shutdown_done = false;

	if (shutdown_done)
	{
		return;
	}
	if (ybc_pg_session)
	{
		YBCPgDestroySession(ybc_pg_session);
		ybc_pg_session = NULL;
	}
	YBCDestroyPgGate();
	shutdown_done = true;
}

static void
YBCResetCommitStatus()
{
	if (ybc_commit_status)
	{
		YBCFreeStatus(ybc_commit_status);
		ybc_commit_status = NULL;
	}
}

bool
YBCCommitTransaction()
{
	if (!IsYugaByteEnabled())
		return true;

	YBCStatus status =
		YBCPgTxnManager_CommitTransaction_Status(YBCGetPgTxnManager());
	if (status != NULL) {
		YBCResetCommitStatus();
		ybc_commit_status = status;
		return false;
	}

	return true;
}

void
YBCHandleCommitError()
{
	YBCStatus status = ybc_commit_status;
	if (status != NULL) {
		char* msg = palloc(strlen(status->msg) + 1);
		strcpy(msg, status->msg);
		YBCResetCommitStatus();
		ereport(ERROR,
				(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
				 errmsg("Error during commit: %s", msg)));
	}
}

bool
YBIsPgLockingEnabled()
{
	return !YBTransactionsEnabled();
}

static bool yb_preparing_templates = false;
void
YBSetPreparingTemplates() {
	yb_preparing_templates = true;
}

bool
YBIsPreparingTemplates() {
	return yb_preparing_templates;
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
	const char* flag_file_path =
		getenv("YB_PG_NO_RESTART_ALL_CHILDREN_ON_CRASH_FLAG_PATH");
	// We will use PostgreSQL's default behavior (restarting all children if one of them crashes)
	// if the flag env variable is not specified or the file pointed by it does not exist.
	return !flag_file_path || access(flag_file_path, F_OK) == -1;
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
	return rel->rd_rel->relisshared ? TemplateDbOid : MyDatabaseId;
}
