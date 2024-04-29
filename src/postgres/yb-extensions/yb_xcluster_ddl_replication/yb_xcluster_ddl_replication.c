// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License.  You may obtain a copy
// of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
// License for the specific language governing permissions and limitations under
// the License.

#include "postgres.h"

#include "access/xact.h"
#include "catalog/pg_type_d.h"
#include "commands/event_trigger.h"
#include "executor/spi.h"
#include "utils/fmgrprotos.h"

#include "pg_yb_utils.h"

#include "extension_util.h"
#include "json_util.h"

PG_MODULE_MAGIC;

/* Extension variables. */
static int ReplicationRole = REPLICATION_ROLE_DISABLED;
static bool EnableManualDDLReplication = false;
char *DDLQueuePrimaryKeyStartTime = NULL;
char *DDLQueuePrimaryKeyQueryId = NULL;

/* Util functions. */
static bool IsInIgnoreList(EventTriggerData *trig_data);

/*
 * _PG_init gets called when the extension is loaded.
 */
void
_PG_init(void)
{
	if (IsBinaryUpgrade)
		return;

	DefineCustomEnumVariable(
		"yb_xcluster_ddl_replication.replication_role",
		gettext_noop(
			"xCluster Replication role per database. "
			"NOTE: Manually changing this can lead to replication errors."),
		NULL,
		&ReplicationRole,
		REPLICATION_ROLE_DISABLED,
		replication_roles,
		PGC_SUSET,
		0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"yb_xcluster_ddl_replication.enable_manual_ddl_replication",
		gettext_noop(
			"Temporarily disable automatic xCluster DDL replication - DDLs will have "
			"to be manually executed on the target."),
		gettext_noop(
			"DDL strings will still be captured and replicated, but will be marked "
			"with a 'manual_replication' flag."),
		&EnableManualDDLReplication,
		false,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomStringVariable(
		"yb_xcluster_ddl_replication.ddl_queue_primary_key_start_time",
		gettext_noop("Internal use only: Used by HandleTargetDDLEnd function."),
		NULL,
		&DDLQueuePrimaryKeyStartTime,
		"",
		PGC_SUSET,
		0,
		NULL, NULL, NULL);

	DefineCustomStringVariable(
		"yb_xcluster_ddl_replication.ddl_queue_primary_key_query_id",
		gettext_noop("Internal use only: Used by HandleTargetDDLEnd function."),
		NULL,
		&DDLQueuePrimaryKeyQueryId,
		"",
		PGC_SUSET,
		0,
		NULL, NULL, NULL);
}

void
InsertIntoTable(const char *table_name, int64 start_time, int64 query_id,
				Jsonb *yb_data)
{
	const int kNumArgs = 3;
	Oid arg_types[kNumArgs];
	Datum arg_vals[kNumArgs];
	StringInfoData query_buf;

	initStringInfo(&query_buf);
	appendStringInfo(&query_buf,
					"INSERT INTO %s.%s (start_time, query_id, yb_data) values "
					"($1,$2,$3)",
					EXTENSION_NAME, table_name);

	arg_types[0] = INT8OID;
	arg_vals[0] = Int64GetDatum(start_time);

	arg_types[1] = INT8OID;
	arg_vals[1] = Int64GetDatum(query_id);

	arg_types[2] = JSONBOID;
	arg_vals[2] = PointerGetDatum(yb_data);

	int exec_res = SPI_execute_with_args(query_buf.data, kNumArgs, arg_types,
										arg_vals, /* Nulls */ NULL, /* readonly */ false,
										/* tuple-count limit */ 1);
	if (exec_res != SPI_OK_INSERT)
		elog(ERROR, "SPI_exec failed (error %d): %s", exec_res, query_buf.data);
}

void
InsertIntoDDLQueue(Jsonb *yb_data)
{
	// Compute the transaction start time in micros since epoch.
	TimestampTz epoch_time =
		GetCurrentTransactionStartTimestamp() - SetEpochTimestamp();
	// Use random int for the query_id.
	InsertIntoTable(DDL_QUEUE_TABLE_NAME, epoch_time, random(), yb_data);
}

/* Returns whether or not to continue with processing the DDL. */
bool
HandleCreateTable()
{
	// TODO(jhe): Is there an alternate method to get this info?
	// TODO(jhe): Can we use ddl_deparse on command to handle each separately?
	StringInfoData query_buf;
	initStringInfo(&query_buf);
	appendStringInfo(
		&query_buf, "SELECT objid FROM pg_catalog.pg_event_trigger_ddl_commands()");
	int exec_res = SPI_execute(query_buf.data, true, 0);
	if (exec_res != SPI_OK_SELECT)
		elog(ERROR, "SPI_exec failed (error %d): %s", exec_res, query_buf.data);

	TupleDesc spiTupDesc = SPI_tuptable->tupdesc;
	bool found_yb_relation = false;
	for (int row = 0; row < SPI_processed; row++)
	{
		HeapTuple spiTuple = SPI_tuptable->vals[row];
		bool is_null;
		Oid objid =
			DatumGetObjectId(SPI_getbinval(spiTuple, spiTupDesc, 1, &is_null));

		Relation rel = RelationIdGetRelation(objid);
		// Ignore temporary tables and primary indexes (same as main table).
		if (!IsYBBackedRelation(rel) ||
			(rel->rd_rel->relkind == RELKIND_INDEX && rel->rd_index->indisprimary))
		{
			RelationClose(rel);
			continue;
		}

		// Also need to check colocated until that is supported.
		YbTableProperties table_props = YbGetTableProperties(rel);
		bool is_colocated = table_props->is_colocated;
		RelationClose(rel);
		if (is_colocated)
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg("Colocated objects are not yet supported by "
									"yb_xcluster_ddl_replication")));

		found_yb_relation = true;
	}

	// If all the objects are temporary, then stop processing early as we don't
	// need to replicate this ddl query at all.
	return found_yb_relation;
}

void
HandleSourceDDLEnd(EventTriggerData *trig_data)
{
	// Create memory context for handling json creation + query execution.
	MemoryContext context_new, context_old;
	Oid save_userid;
	int save_sec_context;
	INIT_MEM_CONTEXT_AND_SPI_CONNECT(
		"yb_xcluster_ddl_replication.HandleSourceDDLEnd context");

	// Begin constructing json, fill common fields first.
	JsonbParseState *state = NULL;
	(void) pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);
	(void) AddNumericJsonEntry(state, "version", 1);
	(void) AddStringJsonEntry(state, "query", debug_query_string);
	(void) AddStringJsonEntry(state, "command_tag", trig_data->tag);

	const char *current_user = GetUserNameFromId(GetUserId(), false);
	if (current_user)
		(void) AddStringJsonEntry(state, "user", current_user);

	FunctionCallInfoData fcinfo;
	InitFunctionCallInfoData(fcinfo, NULL, 0, InvalidOid, NULL, NULL);
	const char *cur_schema = DatumGetCString(current_schema(&fcinfo));
	if (cur_schema)
		(void) AddStringJsonEntry(state, "schema", cur_schema);

	/*
	 * TODO(jhe): Need a better way of handling all these DDL types. Perhaps can
	 * mimic CreateCommandTag and return a custom enum instead, thus allowing
	 * for switch cases here.
	 */
	if (EnableManualDDLReplication)
	{
		(void) AddBoolJsonEntry(state, "manual_replication", true);
	}
	else if (strcmp(trig_data->tag, "CREATE TABLE") == 0)
	{
		if (!HandleCreateTable())
			goto exit;
	}
	else
	{
		elog(ERROR,
			"Unsupported DDL: %s\nTo manually replicate, run DDL with "
			"SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = true",
			trig_data->tag);
	}

	// Construct the jsonb and insert completed row into ddl_queue table.
	JsonbValue *jsonb_val = pushJsonbValue(&state, WJB_END_OBJECT, NULL);
	Jsonb *jsonb = JsonbValueToJsonb(jsonb_val);

	InsertIntoDDLQueue(jsonb);

exit:
	CLOSE_MEM_CONTEXT_AND_SPI;
}

void
HandleTargetDDLEnd(EventTriggerData *trig_data)
{
	// Manual DDLs are not captured at all on the target.
	if (EnableManualDDLReplication)
		return;
	/*
	 * We expect ddl_queue_primary_key_* variables to have been set earlier in
	 * the transaction by the ddl_queue handler.
	 */
	int64 pkey_start_time = GetInt64FromVariable(DDLQueuePrimaryKeyStartTime,
												"ddl_queue_primary_key_start_time");
	int64 pkey_query_id = GetInt64FromVariable(DDLQueuePrimaryKeyQueryId,
												"ddl_queue_primary_key_query_id");

	// Create memory context for handling json creation + query execution.
	MemoryContext context_new, context_old;
	Oid save_userid;
	int save_sec_context;
	INIT_MEM_CONTEXT_AND_SPI_CONNECT(
		"yb_xcluster_ddl_replication.HandleTargetDDLEnd context");

	JsonbParseState *state = NULL;
	(void) pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);
	(void) AddStringJsonEntry(state, "query", debug_query_string);
	JsonbValue *jsonb_val = pushJsonbValue(&state, WJB_END_OBJECT, NULL);
	Jsonb *jsonb = JsonbValueToJsonb(jsonb_val);

	InsertIntoTable(REPLICATED_DDLS_TABLE_NAME, pkey_start_time, pkey_query_id,
					jsonb);

	CLOSE_MEM_CONTEXT_AND_SPI;
}

PG_FUNCTION_INFO_V1(handle_ddl_end);
Datum
handle_ddl_end(PG_FUNCTION_ARGS)
{
	if (ReplicationRole == REPLICATION_ROLE_DISABLED)
		PG_RETURN_NULL();

	if (!CALLED_AS_EVENT_TRIGGER(fcinfo)) /* internal error */
		elog(ERROR, "not fired by event trigger manager");

	EventTriggerData *trig_data = (EventTriggerData *) fcinfo->context;

	if (IsInIgnoreList(trig_data))
		PG_RETURN_NULL();

	if (ReplicationRole == REPLICATION_ROLE_SOURCE ||
		ReplicationRole == REPLICATION_ROLE_BIDIRECTIONAL)
	{
		HandleSourceDDLEnd(trig_data);
	}
	if (ReplicationRole == REPLICATION_ROLE_TARGET ||
		ReplicationRole == REPLICATION_ROLE_BIDIRECTIONAL)
	{
		HandleTargetDDLEnd(trig_data);
	}

	PG_RETURN_NULL();
}

static bool
IsInIgnoreList(EventTriggerData *trig_data)
{
	if (strcmp(trig_data->tag, "CREATE EXTENSION") == 0 ||
		strcmp(trig_data->tag, "DROP EXTENSION") == 0 ||
		strcmp(trig_data->tag, "ALTER EXTENSION") == 0)
	{
		return true;
	}
	return false;
}
