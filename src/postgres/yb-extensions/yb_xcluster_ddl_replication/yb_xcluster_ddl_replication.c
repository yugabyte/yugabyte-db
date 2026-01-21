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

#include "catalog/pg_type_d.h"
#include "commands/event_trigger.h"
#include "commands/extension.h"
#include "executor/spi.h"
#include "extension_util.h"
#include "json_util.h"
#include "source_ddl_end_handler.h"
#include "utils/builtins.h"
#include "utils/fmgrprotos.h"
#include "utils/queryjumble.h"

PG_MODULE_MAGIC;


/*
 * Extension variables.
 */

static bool enable_manual_ddl_replication = false;
char		*ddl_queue_primary_key_ddl_end_time = NULL;
char 		*ddl_queue_primary_key_queue_id = NULL;

static const struct config_enum_entry replication_role_overrides[] = {
	{"", XCLUSTER_ROLE_UNSPECIFIED, /* hidden */ false},
	{"NONE", XCLUSTER_ROLE_UNSPECIFIED, /* hidden */ false},
	{"UNSPECIFIED", XCLUSTER_ROLE_UNSPECIFIED, /* hidden */ true},
	{"NOT_AUTOMATIC_MODE", XCLUSTER_ROLE_NOT_AUTOMATIC_MODE, /* hidden */ true},
	{"UNAVAILABLE", XCLUSTER_ROLE_UNAVAILABLE, /* hidden */ true},
	{"SOURCE", XCLUSTER_ROLE_AUTOMATIC_SOURCE, /* hidden */ false},
	{"AUTOMATIC_SOURCE", XCLUSTER_ROLE_AUTOMATIC_SOURCE, /* hidden */ true},
	{"TARGET", XCLUSTER_ROLE_AUTOMATIC_TARGET, /* hidden */ false},
	{"AUTOMATIC_TARGET", XCLUSTER_ROLE_AUTOMATIC_TARGET, /* hidden */ true},
{NULL, 0, false}};

/*
 * Call FetchReplicationRole() at the start of every DDL to fill this variable
 * in before using it.
 */
static int	replication_role = XCLUSTER_ROLE_UNAVAILABLE;
static int	replication_role_override = XCLUSTER_ROLE_UNSPECIFIED;

/*
 * Information about the current DDL.
 * The query string, location and length represent the substring of the query
 * that is currently being processed. This enables to capture only the DDL part
 * of a multi-statement query, or a FUNCTION with multiple statements.
 */
const char *query_string = NULL;
int query_location = 0;
int query_len = 0;
Node *parsetree = NULL;

/* Information about an extension that is being created. */
bool is_in_extension_ddl = false;
bool is_self_extension_ddl = false;
char *current_extension_name = NULL;

/*
 * Util functions.
 */
static void RecordTempRelationDDL();
static void XClusterProcessUtility(PlannedStmt *pstmt,
								   const char *queryString,
								   bool readOnlyTree,
								   ProcessUtilityContext context,
								   ParamListInfo params,
								   QueryEnvironment *queryEnv,
								   DestReceiver *dest,
								   QueryCompletion *qc);

/*
 * Per DDL Variables.
 */

/*
 * This is updated as the DDL triggers run, ending up with the decision of
 * whether or not to replicate the DDL that is currently running.
 *
 * Once this becomes true, it remains true for the rest of the DDL.
 */
static bool yb_should_replicate_ddl = false;

static YbcRecordTempRelationDDL_hook_type prev_YBCRecordTempRelationDDL = NULL;
static ProcessUtility_hook_type prev_ProcessUtility = NULL;

/*
 * The GUC variables `enable_manual_ddl_replication` and
 * `TEST_replication_role_override` cannot be supported using connection
 * manager due to how GUC variables are supported through connection manager.
 * Any modifications to these variables should cause the connection to become
 * sticky so as to not allow internal changes to the variables to occur while
 * not servicing an active client connection.
 *
 * These assign hooks have no purpose if connection manager is not being used.
 */

static void
assign_enable_manual_ddl_replication(bool newval, void *extra)
{
	if (!YbIsClientYsqlConnMgr())
		return;
	elog(LOG, "Making connection sticky for setting enable_manual_ddl_replication");
	yb_ysql_conn_mgr_sticky_guc = true;
}

static void
assign_TEST_replication_role_override(int newval, void *extra)
{
	if (!YbIsClientYsqlConnMgr())
		return;
	elog(LOG, "Making connection sticky for setting TEST_replication_role_override");
	yb_ysql_conn_mgr_sticky_guc = true;
}

/*
 * _PG_init gets called when the extension is loaded.
 */
void
_PG_init(void)
{
	if (IsBinaryUpgrade)
		return;

	DefineCustomBoolVariable("yb_xcluster_ddl_replication.enable_manual_ddl_replication",
							 gettext_noop("Temporarily disable automatic xCluster DDL replication - DDLs will have "
										  "to be manually executed on the target."),
							 gettext_noop("DDL strings will still be captured and replicated, but will be marked "
										  "with a 'manual_replication' flag."),
							 &enable_manual_ddl_replication,
							 false,
							 PGC_USERSET,
							 0,
							 NULL, assign_enable_manual_ddl_replication, NULL);

	DefineCustomStringVariable("yb_xcluster_ddl_replication.ddl_queue_primary_key_ddl_end_time",
							   gettext_noop("Internal use only: Used by HandleTargetDDLEnd function."),
							   NULL,
							   &ddl_queue_primary_key_ddl_end_time,
							   "",
							   PGC_SUSET,
							   0,
							   NULL, NULL, NULL);

	DefineCustomStringVariable("yb_xcluster_ddl_replication.ddl_queue_primary_key_query_id",
							   gettext_noop("Internal use only: Used by HandleTargetDDLEnd function."),
							   NULL,
							   &ddl_queue_primary_key_queue_id,
							   "",
							   PGC_SUSET,
							   0,
							   NULL, NULL, NULL);

	DefineCustomEnumVariable("yb_xcluster_ddl_replication.TEST_replication_role_override",
							 gettext_noop("Test override for replication role."),
							 NULL,
							 &replication_role_override,
							 XCLUSTER_ROLE_UNSPECIFIED,
							 replication_role_overrides,
							 PGC_SUSET,
							 0,
							 NULL, assign_TEST_replication_role_override, NULL);

	prev_YBCRecordTempRelationDDL = YBCRecordTempRelationDDL_hook;
	YBCRecordTempRelationDDL_hook = RecordTempRelationDDL;

	prev_ProcessUtility = ProcessUtility_hook;
	ProcessUtility_hook = XClusterProcessUtility;

}

void
FetchReplicationRole()
{
	if (replication_role_override != XCLUSTER_ROLE_UNSPECIFIED)
		replication_role = replication_role_override;
	else
		replication_role = YBCGetXClusterRole(MyDatabaseId);

	if (replication_role == XCLUSTER_ROLE_UNAVAILABLE)
	{
		ereport(ERROR,
				(errcode(ERRCODE_YB_ERROR),
				 errmsg("unable to fetch replication role")));
	}
}

bool
IsDisabled(CommandTag command_tag)
{
	/*
	 * Disabled if we are not in xCluster automatic mode.
	 * Disabled for our own extension.
	 * Disabled if we are in an extension DDL and not handling the top level
	 * query.
	 */
	return (replication_role != XCLUSTER_ROLE_AUTOMATIC_SOURCE &&
			replication_role != XCLUSTER_ROLE_AUTOMATIC_TARGET) ||
		   is_self_extension_ddl ||
		   (is_in_extension_ddl && !IsExtensionDdl(command_tag));
}

bool
IsReplicationSource()
{
	return (replication_role == XCLUSTER_ROLE_AUTOMATIC_SOURCE);
}

bool
IsReplicationTarget()
{
	return (replication_role == XCLUSTER_ROLE_AUTOMATIC_TARGET);
}

PG_FUNCTION_INFO_V1(get_replication_role);
Datum
get_replication_role(PG_FUNCTION_ARGS)
{
	FetchReplicationRole();
	char	   *role_name;

	switch (replication_role)
	{
		case XCLUSTER_ROLE_UNSPECIFIED:
			role_name = "unspecified";
			break;
		case XCLUSTER_ROLE_UNAVAILABLE:
			role_name = "unavailable";
			break;
		case XCLUSTER_ROLE_NOT_AUTOMATIC_MODE:
			role_name = "not_automatic_mode";
			break;
		case XCLUSTER_ROLE_AUTOMATIC_SOURCE:
			role_name = "source";
			break;
		case XCLUSTER_ROLE_AUTOMATIC_TARGET:
			role_name = "target";
			break;
		default:
			role_name = "unknown";
			break;
	}
	PG_RETURN_TEXT_P(cstring_to_text(role_name));
}

#define INSERT_INTO_TABLE_NUM_ARGS 3

void
InsertIntoTable(const char *table_name, int64 ddl_end_time, int64 query_id,
				Jsonb *yb_data)
{
	Oid			arg_types[INSERT_INTO_TABLE_NUM_ARGS];
	Datum		arg_vals[INSERT_INTO_TABLE_NUM_ARGS];
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

	int			exec_res = SPI_execute_with_args(query_buf.data, INSERT_INTO_TABLE_NUM_ARGS, arg_types,
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
	(void) AddNStringJsonEntry(state, "query", query_string, query_len);
	JsonbValue *jsonb_val = pushJsonbValue(&state, WJB_END_OBJECT, NULL);
	Jsonb	   *jsonb = JsonbValueToJsonb(jsonb_val);

	InsertIntoTable(REPLICATED_DDLS_TABLE_NAME, ddl_end_time, query_id, jsonb);

	CLOSE_MEM_CONTEXT_AND_SPI;
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
	(void) AddNStringJsonEntry(state, "query", query_string, query_len);
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

	if (enable_manual_ddl_replication)
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
		int64		query_id = random();

		InsertIntoTable(DDL_QUEUE_TABLE_NAME, epoch_time, query_id, jsonb);

		/*
		 * Also insert into the replicated_ddls table to handle switchovers.
		 *
		 * During switchover, we have a middle state with A target <-> B
		 * target.  In this state, A is polling from B, and so ddl_queue on A
		 * could try to process its ddl_queue entries. But since we write to
		 * replicated_ddls on A, the ddl_queue handler will see that all DDLs
		 * in the queue have been processed.
		 */
		InsertIntoReplicatedDDLs(epoch_time, query_id);
	}

	CLOSE_MEM_CONTEXT_AND_SPI;
}

void
HandleTargetDDLEnd(EventTriggerData *trig_data)
{
	/* Manual DDLs are not captured at all on the target. */
	if (enable_manual_ddl_replication)
		return;

	/*
	 * DDLs on target are blocked in pg_client_session before they can modify
	 * the catalog, so if a user executed DDL got this far then it means this is a
	 * pass through DDL command.
	 */
	if (!yb_xcluster_automatic_mode_target_ddl)
		return;

	/*
	 * We expect ddl_queue_primary_key_* variables to have been set earlier in
	 * the transaction by the ddl_queue handler.
	 */
	int64		pkey_ddl_end_time = GetInt64FromVariable(ddl_queue_primary_key_ddl_end_time,
														 "ddl_queue_primary_key_ddl_end_time");
	int64		pkey_query_id = GetInt64FromVariable(ddl_queue_primary_key_queue_id,
													 "ddl_queue_primary_key_query_id");

	InsertIntoReplicatedDDLs(pkey_ddl_end_time, pkey_query_id);
}

void
HandleSourceSQLDrop(EventTriggerData *trig_data)
{
	if (enable_manual_ddl_replication)
		return;

	/* Create memory context for handling query execution. */
	MemoryContext context_new,
				context_old;
	Oid			save_userid;
	int			save_sec_context;

	INIT_MEM_CONTEXT_AND_SPI_CONNECT("yb_xcluster_ddl_replication.HandleSourceSQLDrop context");

	yb_should_replicate_ddl |= ProcessSourceEventTriggerDroppedObjects(trig_data->tag);

	CLOSE_MEM_CONTEXT_AND_SPI;
}

void
HandleSourceTableRewrite(EventTriggerData *trig_data)
{
	if (enable_manual_ddl_replication)
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
	if (enable_manual_ddl_replication)
	{
		/*
		 * Always replicate manual DDLs regardless of what they are.
		 * Will show up on the target with a manual_replication field set.
		 */
		yb_should_replicate_ddl = true;
		return;
	}

	ClearRewrittenTableOidList();
}

void
HandleTargetDDLStart(EventTriggerData *trig_data)
{
	yb_xcluster_target_ddl_bypass = false;

	/* Bypass DDLs executed in manual mode, or from the target poller. */
	if (enable_manual_ddl_replication || yb_xcluster_automatic_mode_target_ddl)
	{
		yb_xcluster_target_ddl_bypass = true;
		return;
	}

	/*
	 * Temp relations are bypassed in RecordTempRelationDDL.
	 * DDLs that are not caught by the trigger (ex CREATE DATABASE) are bypassed
	 * in XClusterProcessUtility.
	 */
}

static char *
GetExtensionName(CommandTag tag)
{
	/*
	 * In the case where we are creating our own extension, we only start
	 * capturing DDLs mid way and miss the top level command tag. So rely instead
	 * on the creating_extension global variable.
	 */
	if (creating_extension)
		tag = CMDTAG_CREATE_EXTENSION;

	switch (tag)
	{
		case CMDTAG_CREATE_EXTENSION:
				return ((CreateExtensionStmt *) parsetree)->extname;
		case CMDTAG_ALTER_EXTENSION:
				return ((AlterExtensionStmt *) parsetree)->extname;
		case CMDTAG_DROP_EXTENSION:
			{
				DropStmt   *stmt = (DropStmt *) parsetree;

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

void
EvaluateExtensionDDL(CommandTag command_tag)
{
	if (!is_in_extension_ddl)
		return;

	if (current_extension_name == NULL)
	{
		current_extension_name = GetExtensionName(command_tag);

		if (current_extension_name != NULL &&
			strcmp(current_extension_name, EXTENSION_NAME) == 0)
			is_self_extension_ddl = true;
	}

	if (is_self_extension_ddl)
		return;

	/* Disallow extensions that contain complex DDLs like CREATE TABLE. */
	if (!IsPassThroughDdlCommandSupported(command_tag))
	{
		elog(ERROR,
			 "Database is replicating DDLs for xCluster. Extension %s is not "
			 "supported because it contains unsupported DDLs within the "
			 "extension script. Create the extension before adding the "
			 "database to xCluster.\n"
			 "Unsupported DDL within extension : %s\n",
			 current_extension_name, GetCommandTagName(command_tag));
	}
}

PG_FUNCTION_INFO_V1(handle_ddl_start);
Datum
handle_ddl_start(PG_FUNCTION_ARGS)
{
	if (!CALLED_AS_EVENT_TRIGGER(fcinfo))	/* internal error */
		elog(ERROR, "not fired by event trigger manager");

	EventTriggerData *trig_data = (EventTriggerData *) fcinfo->context;

	FetchReplicationRole();
	EvaluateExtensionDDL(trig_data->tag);

	if (IsDisabled(trig_data->tag))
		PG_RETURN_NULL();

	/*
	 * Given a possibly multi-statement source string, confine our attention to
	 * the relevant part of the string.
	 */
	query_string = CleanQuerytext(query_string, &query_location, &query_len);

	if (IsReplicationSource())
	{
		HandleSourceDDLStart(trig_data);
	}
	if (IsReplicationTarget())
	{
		HandleTargetDDLStart(trig_data);
	}

	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(handle_ddl_end);
Datum
handle_ddl_end(PG_FUNCTION_ARGS)
{
	if (!CALLED_AS_EVENT_TRIGGER(fcinfo))	/* internal error */
		elog(ERROR, "not fired by event trigger manager");

	EventTriggerData *trig_data = (EventTriggerData *) fcinfo->context;

	if (IsDisabled(trig_data->tag))
		PG_RETURN_NULL();

	Assert(query_string != NULL);
	Assert(query_len > 0);

	/*
	 * Capture the DDL as long as its not a step within another Extension DDL
	 * batch.
	 */
	if (IsReplicationSource())
	{
		HandleSourceDDLEnd(trig_data);
	}
	if (IsReplicationTarget())
	{
		HandleTargetDDLEnd(trig_data);
	}

	query_string = NULL;
	query_len = 0;

	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(handle_sql_drop);
Datum
handle_sql_drop(PG_FUNCTION_ARGS)
{
	if (!CALLED_AS_EVENT_TRIGGER(fcinfo))	/* internal error */
		elog(ERROR, "not fired by event trigger manager");

	EventTriggerData *trig_data = (EventTriggerData *) fcinfo->context;

	if (IsDisabled(trig_data->tag))
		PG_RETURN_NULL();

	if (IsReplicationSource())
	{
		HandleSourceSQLDrop(trig_data);
	}

	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(handle_table_rewrite);
Datum
handle_table_rewrite(PG_FUNCTION_ARGS)
{
	if (!CALLED_AS_EVENT_TRIGGER(fcinfo))	/* internal error */
		elog(ERROR, "not fired by event trigger manager");

	EventTriggerData *trig_data = (EventTriggerData *) fcinfo->context;

	if (IsDisabled(trig_data->tag))
		PG_RETURN_NULL();

	if (IsReplicationSource())
	{
		HandleSourceTableRewrite(trig_data);
	}

	PG_RETURN_NULL();
}

static void
RecordTempRelationDDL()
{
	/*
	 * If we are manually running a DDL on a temp relation on the target, then do not block it.
	 */
	if (IsReplicationTarget())
		yb_xcluster_target_ddl_bypass = true;

	if (prev_YBCRecordTempRelationDDL)
		prev_YBCRecordTempRelationDDL();
}

void
HandleQueryStart(PlannedStmt *pstmt, const char *queryString, bool is_top_level_query)
{
	/*
	 * For DDLs that are handled by the handle_ddl_start event trigger,
	 * HandleTargetDDLStart will set yb_xcluster_target_ddl_bypass to false and
	 * then allow them on a case by case basis. For any DDL that is not handled
	 * by the trigger, we will set yb_xcluster_target_ddl_bypass to true and
	 * allow it to pass through.
	 */
	yb_xcluster_target_ddl_bypass = true;

	/*
	 * For extension DDLs, only capture the top level query.
	 */
	if (!is_in_extension_ddl)
	{
		query_location = pstmt->stmt_location;
		query_len = pstmt->stmt_len;
		query_string = queryString;
		parsetree = pstmt->utilityStmt;
	}

	if (is_top_level_query && IsExtensionDdl(CreateCommandTag(parsetree)))
	{
		is_in_extension_ddl = true;
	}
}

void
HandleQueryEnd(bool is_top_level_query)
{
	if (is_top_level_query)
	{
		is_in_extension_ddl = false;
		is_self_extension_ddl = false;
		current_extension_name = NULL;
	}

	/*
	 * For extension DDLs, retain the query string and length until the top
	 * level query ends.
	 */
	if (!is_in_extension_ddl)
	{
		query_string = NULL;
		query_len = 0;
	}
}

static void
XClusterProcessUtility(PlannedStmt *pstmt,
					   const char *queryString,
					   bool readOnlyTree,
					   ProcessUtilityContext context,
					   ParamListInfo params,
					   QueryEnvironment *queryEnv,
					   DestReceiver *dest,
					   QueryCompletion *qc)
{
	bool is_top_level_query = (context == PROCESS_UTILITY_TOPLEVEL);
	bool isCompleteQuery = (context != PROCESS_UTILITY_SUBCOMMAND);

	if (isCompleteQuery)
		HandleQueryStart(pstmt, queryString, is_top_level_query);

	PG_TRY();
	{
		if (prev_ProcessUtility)
			prev_ProcessUtility(pstmt, queryString, readOnlyTree, context,
								params, queryEnv, dest, qc);
		else
			standard_ProcessUtility(pstmt, queryString, readOnlyTree, context,
									params, queryEnv, dest, qc);
	}
	PG_FINALLY();
	{
		if (isCompleteQuery)
			HandleQueryEnd(is_top_level_query);
	}
	PG_END_TRY();
}
