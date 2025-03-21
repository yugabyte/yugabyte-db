/*-----------------------------------------------------------------------------
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
 * License for the specific language governing permissions and limitations under
 * the License.
 *
 *-----------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/xact.h"
#include "catalog/pg_type_d.h"
#include "commands/event_trigger.h"
#include "executor/spi.h"
#include "extension_util.h"
#include "json_util.h"
#include "nodes/pg_list.h"
#include "pg_yb_utils.h"
#include "source_ddl_end_handler.h"
#include "utils/fmgrprotos.h"

PG_MODULE_MAGIC;

/* Extension variables. */
typedef enum YbClusterReplicationRole
{
	REPLICATION_ROLE_DISABLED,
	REPLICATION_ROLE_SOURCE,
	REPLICATION_ROLE_TARGET,
	REPLICATION_ROLE_BIDIRECTIONAL,
} YbClusterReplicationRole;

static const struct config_enum_entry replication_roles[] = {
	{"DISABLED", REPLICATION_ROLE_DISABLED, false},
	{"SOURCE", REPLICATION_ROLE_SOURCE, false},
	{"TARGET", REPLICATION_ROLE_TARGET, false},
	{"BIDIRECTIONAL", REPLICATION_ROLE_BIDIRECTIONAL, /* hidden */ true},
	{NULL, 0, false},
};

static int	ReplicationRole = REPLICATION_ROLE_DISABLED;
static bool EnableManualDDLReplication = false;
char	   *DDLQueuePrimaryKeyDDLEndTime = NULL;
char	   *DDLQueuePrimaryKeyQueryId = NULL;

/* Util functions. */
static bool IsInIgnoreList(EventTriggerData *trig_data);

/* Per DDL Variables. */

/*
 * This is updated as the DDL triggers run, ending up with the decision of
 * whether or not to replicate the DDL that is currently running.
 *
 * Once this becomes true, it remains true for the rest of the DDL.
 */
static bool yb_should_replicate_ddl = false;

/*
 * _PG_init gets called when the extension is loaded.
 */
void
_PG_init(void)
{
	if (IsBinaryUpgrade)
		return;

	DefineCustomEnumVariable("yb_xcluster_ddl_replication.replication_role",
							 gettext_noop("xCluster Replication role per database. "
										  "NOTE: Manually changing this can lead to replication errors."),
							 NULL,
							 &ReplicationRole,
							 REPLICATION_ROLE_DISABLED,
							 replication_roles,
							 PGC_SUSET,
							 0,
							 NULL, NULL, NULL);

	DefineCustomBoolVariable("yb_xcluster_ddl_replication.enable_manual_ddl_replication",
							 gettext_noop("Temporarily disable automatic xCluster DDL replication - DDLs will have "
										  "to be manually executed on the target."),
							 gettext_noop("DDL strings will still be captured and replicated, but will be marked "
										  "with a 'manual_replication' flag."),
							 &EnableManualDDLReplication,
							 false,
							 PGC_USERSET,
							 0,
							 NULL, NULL, NULL);

	DefineCustomStringVariable("yb_xcluster_ddl_replication.ddl_queue_primary_key_ddl_end_time",
							   gettext_noop("Internal use only: Used by HandleTargetDDLEnd function."),
							   NULL,
							   &DDLQueuePrimaryKeyDDLEndTime,
							   "",
							   PGC_SUSET,
							   0,
							   NULL, NULL, NULL);

	DefineCustomStringVariable("yb_xcluster_ddl_replication.ddl_queue_primary_key_query_id",
							   gettext_noop("Internal use only: Used by HandleTargetDDLEnd function."),
							   NULL,
							   &DDLQueuePrimaryKeyQueryId,
							   "",
							   PGC_SUSET,
							   0,
							   NULL, NULL, NULL);
}

bool
IsReplicationSource()
{
	return (ReplicationRole == REPLICATION_ROLE_SOURCE ||
			ReplicationRole == REPLICATION_ROLE_BIDIRECTIONAL);
}

bool
IsReplicationTarget()
{
	return (ReplicationRole == REPLICATION_ROLE_TARGET ||
			ReplicationRole == REPLICATION_ROLE_BIDIRECTIONAL);
}

void
InsertIntoTable(const char *table_name, int64 ddl_end_time, int64 query_id,
				Jsonb *yb_data)
{
	const int	kNumArgs = 3;
	Oid			arg_types[kNumArgs];
	Datum		arg_vals[kNumArgs];
	StringInfoData query_buf;

	initStringInfo(&query_buf);
	appendStringInfo(&query_buf,
					 "INSERT INTO %s.%s (ddl_end_time, query_id, yb_data) values "
					 "($1,$2,$3)",
					 EXTENSION_NAME, table_name);

	arg_types[0] = INT8OID;
	arg_vals[0] = Int64GetDatum(ddl_end_time);

	arg_types[1] = INT8OID;
	arg_vals[1] = Int64GetDatum(query_id);

	arg_types[2] = JSONBOID;
	arg_vals[2] = PointerGetDatum(yb_data);

	int			exec_res = SPI_execute_with_args(query_buf.data, kNumArgs, arg_types,
												 arg_vals, /* Nulls */ NULL, /* readonly */ false,
												  /* tuple-count limit */ 1);

	if (exec_res != SPI_OK_INSERT)
		elog(ERROR, "SPI_exec failed (error %d): %s", exec_res, query_buf.data);
}

void
InsertIntoReplicatedDDLs(int64 ddl_end_time, int64 query_id)
{
	/* Create memory context for handling json creation + query execution. */
	MemoryContext context_new,
				context_old;
	Oid			save_userid;
	int			save_sec_context;

	INIT_MEM_CONTEXT_AND_SPI_CONNECT("yb_xcluster_ddl_replication.InsertIntoReplicatedDDLs context");

	JsonbParseState *state = NULL;

	(void) pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);
	(void) AddStringJsonEntry(state, "query", debug_query_string);
	JsonbValue *jsonb_val = pushJsonbValue(&state, WJB_END_OBJECT, NULL);
	Jsonb	   *jsonb = JsonbValueToJsonb(jsonb_val);

	InsertIntoTable(REPLICATED_DDLS_TABLE_NAME, ddl_end_time, query_id, jsonb);

	CLOSE_MEM_CONTEXT_AND_SPI;
}

bool
IsExtensionDdl(CommandTag command_tag)
{
	if (command_tag == CMDTAG_CREATE_EXTENSION ||
		command_tag == CMDTAG_DROP_EXTENSION ||
		command_tag == CMDTAG_ALTER_EXTENSION)
	{
		return true;
	}

	return false;
}

/*
 * Extensions DDLs result in multiple DDL statements being executed during
 * create/alter/drop of extensions. This function checks whether the current
 * DDL is being executed as part of an Extension DDL such as CREATE/ALTER/DROP
 * extension.
 */
bool
IsCurrentDdlPartOfExtensionDdlBatch(CommandTag command_tag)
{
	/* Extension DDL cannot be executed within another extension DDL. */
	if (IsExtensionDdl(command_tag))
	{
		return false;
	}

	List	   *parse_tree = pg_parse_query(debug_query_string);
	ListCell   *lc;

	foreach(lc, parse_tree)
	{
		RawStmt    *stmt = (RawStmt *) lfirst(lc);
		CommandTag	stmt_command_tag = CreateCommandTag(stmt->stmt);

		if (IsExtensionDdl(stmt_command_tag))
		{
			return true;
		}
	}

	return false;
}

/*
 * Reports an error if the query string has multiple commands in it, or a
 * command tag that doesn't match up with the one captured from the event
 * trigger.
 *
 * Some clients can send multiple commands together as one single query string.
 * This can cause issues for this extension:
 * - If the query has a mix of DDL and DML commands, then we'd end up
 *   replicating those rows twice.
 * - Even if the query has multiple DDLs in it, it is simpler to handle these
 *   individually. Eg. we may need to add additional modifications for each
 *   individual DDL (eg setting oids).
 */
void
DisallowMultiStatementQueries(CommandTag command_tag)
{
	List	   *parse_tree = pg_parse_query(debug_query_string);
	ListCell   *lc;
	int			count = 0;

	foreach(lc, parse_tree)
	{
		++count;
		RawStmt    *stmt = (RawStmt *) lfirst(lc);
		CommandTag	stmt_command_tag = CreateCommandTag(stmt->stmt);

		/*
		 * Only Extension DDLs are allowed to be part of multi-statement as
		 * they typically executes multiple DDLs under the covers.
		 */
		if (!IsExtensionDdl(stmt_command_tag))
		{
			if (count > 1 || command_tag != stmt_command_tag)
				elog(ERROR,
					 "Database is replicating DDLs for xCluster. In this mode only "
					 "a single DDL command is allowed in the query string.\n"
					 "Please run the commands one at a time.\n"
					 "Full query string: %s. \n"
					 "Statement 1: %s\n"
					 "Statement 2: %s",
					 debug_query_string,
					 GetCommandTagName(command_tag),
					 GetCommandTagName(stmt_command_tag));
		}
		else if (!IsPassThroughDdlCommandSupported(command_tag))
		{
			elog(ERROR,
				 "Database is replicating DDLs for xCluster. Extension cannot be supported "
				 "because it contains DDLs that are not yet supported by replication. \n"
				 "Full query string: %s. \n"
				 "Unsupported DDL within extension : %s\n",
				 debug_query_string,
				 GetCommandTagName(command_tag));
		}
	}
}

void
HandleSourceDDLEnd(EventTriggerData *trig_data)
{
	/* Create memory context for handling json creation + query execution. */
	MemoryContext context_new,
				context_old;
	Oid			save_userid;
	int			save_sec_context;

	INIT_MEM_CONTEXT_AND_SPI_CONNECT("yb_xcluster_ddl_replication.HandleSourceDDLEnd context");

	/* Begin constructing json, fill common fields first. */
	JsonbParseState *state = NULL;

	(void) pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);
	(void) AddNumericJsonEntry(state, "version", 1);
	(void) AddStringJsonEntry(state, "query", debug_query_string);
	(void) AddStringJsonEntry(state, "command_tag",
							  GetCommandTagName(trig_data->tag));

	const char *current_user = GetUserNameFromId(save_userid, false);

	if (current_user)
		(void) AddStringJsonEntry(state, "user", current_user);

	LOCAL_FCINFO(fcinfo, 0);
	InitFunctionCallInfoData(*fcinfo, NULL, 0, InvalidOid, NULL, NULL);
	const char *cur_schema = DatumGetCString(current_schema(fcinfo));

	if (cur_schema)
		(void) AddStringJsonEntry(state, "schema", cur_schema);

	if (EnableManualDDLReplication)
	{
		(void) AddBoolJsonEntry(state, "manual_replication", true);
	}
	else
	{
		yb_should_replicate_ddl |= ProcessSourceEventTriggerDDLCommands(state);
	}

	if (yb_should_replicate_ddl)
	{
		/* Construct the jsonb and insert completed row into ddl_queue table. */
		JsonbValue *jsonb_val = pushJsonbValue(&state, WJB_END_OBJECT, NULL);
		Jsonb	   *jsonb = JsonbValueToJsonb(jsonb_val);

		/*
		 * Compute the current time in micros since epoch.
		 * This is the ddl_end time, which is after all the docdb schema changes for
		 * the DDL have occurred (note that the pg catalog changes are not yet
		 * visible as the txn hasn't committed yet).
		 * Current time also give us a distinct and ordered time for DDLs within a
		 * transaction, as opposed to using now(), which is the same within a txn.
		 *
		 * TODO (#25999): We will also use this time as the safe time to run the DDL
		 * on the target, by waiting for other pollers to catch up to this safe time
		 * before running the DDL. Since this time is after the docdb schema is
		 * applied, we are fine at this point to catch up the pg catalog changes.
		 */
		TimestampTz epoch_time = (GetCurrentTimestamp() - SetEpochTimestamp());
		int64 query_id = random();

		InsertIntoTable(DDL_QUEUE_TABLE_NAME, epoch_time, query_id, jsonb);

		if (ReplicationRole == REPLICATION_ROLE_SOURCE)
		{
			/*
			 * Also insert into the replicated_ddls table to handle switchovers.
			 *
			 * During switchover, we have a middle state with A target <-> B target.
			 * In this state, A is polling from B, and so ddl_queue on A could try to
			 * process its ddl_queue entries. But since we write to replicated_ddls on
			 * A, the ddl_queue handler will see that all DDLs in the queue have been
			 * processed.
			 */
			InsertIntoReplicatedDDLs(epoch_time, query_id);
		}
	}

	CLOSE_MEM_CONTEXT_AND_SPI;
}

void
HandleTargetDDLEnd(EventTriggerData *trig_data)
{
	/* Manual DDLs are not captured at all on the target. */
	if (EnableManualDDLReplication)
		return;
	/*
	 * We expect ddl_queue_primary_key_* variables to have been set earlier in
	 * the transaction by the ddl_queue handler.
	 */
	int64		pkey_ddl_end_time = GetInt64FromVariable(DDLQueuePrimaryKeyDDLEndTime,
															 "ddl_queue_primary_key_ddl_end_time");
	int64		pkey_query_id = GetInt64FromVariable(DDLQueuePrimaryKeyQueryId,
													 "ddl_queue_primary_key_query_id");

	InsertIntoReplicatedDDLs(pkey_ddl_end_time, pkey_query_id);
}

void
HandleSourceSQLDrop(EventTriggerData *trig_data)
{
	if (EnableManualDDLReplication)
		return;

	/* Create memory context for handling query execution. */
	MemoryContext context_new,
				context_old;
	Oid			save_userid;
	int			save_sec_context;

	INIT_MEM_CONTEXT_AND_SPI_CONNECT("yb_xcluster_ddl_replication.HandleSourceSQLDrop context");

	yb_should_replicate_ddl |= ProcessSourceEventTriggerDroppedObjects();

	CLOSE_MEM_CONTEXT_AND_SPI;
}

void
HandleSourceTableRewrite(EventTriggerData *trig_data)
{
	if (EnableManualDDLReplication)
		return;

	/* Create memory context for handling query execution. */
	MemoryContext context_new,
				context_old;
	Oid			save_userid;
	int			save_sec_context;

	INIT_MEM_CONTEXT_AND_SPI_CONNECT("yb_xcluster_ddl_replication.HandleSourceTableRewrite context");

	ProcessSourceEventTriggerTableRewrite();

	CLOSE_MEM_CONTEXT_AND_SPI;
}

void
HandleSourceDDLStart(EventTriggerData *trig_data)
{
	/* By default we don't replicate. */
	yb_should_replicate_ddl = false;
	if (EnableManualDDLReplication)
	{
		/*
		 * Always replicate manual DDLs regardless of what they are.
		 * Will show up on the target with a manual_replication field set.
		 */
		yb_should_replicate_ddl = true;
		return;
	}

	/*
	 * Do some initial checks here before the source query runs.
	 */
	DisallowMultiStatementQueries(trig_data->tag);
	ClearRewrittenTableOidList();
}

PG_FUNCTION_INFO_V1(handle_ddl_start);
Datum
handle_ddl_start(PG_FUNCTION_ARGS)
{
	if (!CALLED_AS_EVENT_TRIGGER(fcinfo))	/* internal error */
		elog(ERROR, "not fired by event trigger manager");

	if (ReplicationRole == REPLICATION_ROLE_DISABLED)
		PG_RETURN_NULL();

	EventTriggerData *trig_data = (EventTriggerData *) fcinfo->context;

	if (IsInIgnoreList(trig_data))
		PG_RETURN_NULL();

	if (IsReplicationSource())
	{
		HandleSourceDDLStart(trig_data);
	}

	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(handle_ddl_end);
Datum
handle_ddl_end(PG_FUNCTION_ARGS)
{
	if (!CALLED_AS_EVENT_TRIGGER(fcinfo))	/* internal error */
		elog(ERROR, "not fired by event trigger manager");

	if (ReplicationRole == REPLICATION_ROLE_DISABLED)
		PG_RETURN_NULL();

	EventTriggerData *trig_data = (EventTriggerData *) fcinfo->context;

	if (IsInIgnoreList(trig_data))
		PG_RETURN_NULL();

	/*
	 * Capture the DDL as long as its not a step within another Extension DDL
	 * batch.
	 */
	if (!IsCurrentDdlPartOfExtensionDdlBatch(trig_data->tag))
	{
		if (IsReplicationSource())
		{
			HandleSourceDDLEnd(trig_data);
		}
		if (IsReplicationTarget())
		{
			HandleTargetDDLEnd(trig_data);
		}
	}

	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(handle_sql_drop);
Datum
handle_sql_drop(PG_FUNCTION_ARGS)
{
	if (!CALLED_AS_EVENT_TRIGGER(fcinfo))	/* internal error */
		elog(ERROR, "not fired by event trigger manager");

	if (ReplicationRole == REPLICATION_ROLE_DISABLED)
		PG_RETURN_NULL();

	EventTriggerData *trig_data = (EventTriggerData *) fcinfo->context;

	if (IsInIgnoreList(trig_data))
		PG_RETURN_NULL();

	if (IsReplicationSource() && !IsCurrentDdlPartOfExtensionDdlBatch(trig_data->tag))
	{
		HandleSourceSQLDrop(trig_data);
	}

	/* HandleTargetDDLEnd will be handled in handle_ddl_end. */

	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(handle_table_rewrite);
Datum
handle_table_rewrite(PG_FUNCTION_ARGS)
{
	if (!CALLED_AS_EVENT_TRIGGER(fcinfo))	/* internal error */
		elog(ERROR, "not fired by event trigger manager");

	if (ReplicationRole == REPLICATION_ROLE_DISABLED)
		PG_RETURN_NULL();

	EventTriggerData *trig_data = (EventTriggerData *) fcinfo->context;

	if (IsInIgnoreList(trig_data))
		PG_RETURN_NULL();

	if (IsReplicationSource())
	{
		HandleSourceTableRewrite(trig_data);
	}

	PG_RETURN_NULL();
}

static char *
GetExtensionName(CommandTag tag, List *parse_tree)
{
	switch (tag)
	{
		case CMDTAG_CREATE_EXTENSION:
			{
				CreateExtensionStmt *stmt;

				stmt = (CreateExtensionStmt *)
					linitial_node(RawStmt, parse_tree)->stmt;

				return stmt->extname;
			}
		case CMDTAG_ALTER_EXTENSION:
			{
				AlterExtensionStmt *stmt;

				stmt = (AlterExtensionStmt *)
					linitial_node(RawStmt, parse_tree)->stmt;

				return stmt->extname;
			}
		case CMDTAG_DROP_EXTENSION:
			{
				DropStmt   *stmt;

				stmt = (DropStmt *)
					linitial_node(RawStmt, parse_tree)->stmt;

				/* Ensure there is at least one object in the list. */
				if (stmt->objects == NULL || list_length(stmt->objects) != 1)
				{
					elog(WARNING, "Unexpected number of objects in DROP EXTENSION statement");
					return NULL;
				}

				Node	   *object = linitial(stmt->objects);

				if (!IsA(object, String))
				{
					elog(WARNING, "Unexpected object type in DROP EXTENSION statement");
					return NULL;
				}

				return strVal(castNode(String, object));
			}
		default:
			return NULL;
	}
}

static bool
IsInIgnoreList(EventTriggerData *trig_data)
{
	if (!IsExtensionDdl(trig_data->tag))
	{
		return false;
	}

	List	   *parse_tree = pg_parse_query(debug_query_string);
	char	   *extname = GetExtensionName(trig_data->tag, parse_tree);

	if (extname != NULL && strcmp(extname, EXTENSION_NAME) == 0)
	{
		return true;
	}

	return false;
}
