/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/commands/kill_op.c
 *
 * Implementation of the kill_op command.
 *-------------------------------------------------------------------------
 */

#include <postgres.h>

#include <catalog/pg_authid.h>
#include <lib/stringinfo.h>
#include <miscadmin.h>
#include <utils/acl.h>
#include <utils/builtins.h>

#include "commands/commands_common.h"
#include "commands/parse_error.h"
#include "utils/documentdb_errors.h"
#include "utils/error_utils.h"
#include "utils/query_utils.h"
#include "utils/string_view.h"

#include "api_hooks.h"
#include "commands/diagnostic_commands_common.h"


/*
 * The operationId is of the format <shardId>:<opId>,
 * Parse and decouple,
 * shardId is the unique identifier for the PID running the backend PG operation
 * opId is the microsecond timestamp of when the operation backend started.
 * databaseName is the database against which the KillOp is running.
 */
typedef struct
{
	StringView shardIdView;
	StringView opIdView;
	char *databaseName;
} ParsedKillOpArgs;


PG_FUNCTION_INFO_V1(command_kill_op);

static const char * GetDefaultOperationCancellationQuery(int64 shardId,
														 StringView *opIdStringView,
														 int *nargs, Oid **argTypes,
														 Datum **argValues,
														 char **argNulls);
static void ValidateAndParseKillOpCommand(pgbson *commandSpec,
										  ParsedKillOpArgs *parsedArgs);

/*
 * Validated a number string to contain only digits.
 * Throws an error if invalid.
 */
static inline void
CheckValidIdentifier(StringView *identifier, const char *fieldId)
{
	if (identifier == NULL || identifier->length == 0)
	{
		return;
	}

	for (uint32_t i = 0; i < identifier->length; i++)
	{
		if (identifier->string[i] < '0' || identifier->string[i] > '9')
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("Invalid %s: %.*s", fieldId, identifier->length,
								   identifier->string),
							errdetail_log("Invalid %s: %.*s", fieldId, identifier->length,
										  identifier->string)));
		}
	}
}


/*
 * Implements the kill_op command.
 * The command takes a killOp wire protocol compatible command spec and makes an attempt to cancel
 * the operation uniquely identified by the `op` field in the command spec.
 * The op field is of the format <shardId>:<opId>.
 * where
 * shardId is the unique identifier for the PG backend process running the operation.
 * opId is the microsecond timestamp of when the operation backend started.
 *
 * See src/commands/current_op.c for more details
 */
Datum
command_kill_op(PG_FUNCTION_ARGS)
{
	pgbson *commandSpec = PG_GETARG_PGBSON(0);
	ParsedKillOpArgs parsedArgs;
	memset(&parsedArgs, 0, sizeof(ParsedKillOpArgs));

	ValidateAndParseKillOpCommand(commandSpec, &parsedArgs);

	/*
	 * We need a null terminated string for pg_strtoint64
	 */
	char *shardIdString = palloc(parsedArgs.shardIdView.length + 1); /* +1 for null terminator */
	memcpy(shardIdString, parsedArgs.shardIdView.string, parsedArgs.shardIdView.length);
	shardIdString[parsedArgs.shardIdView.length] = '\0';
	int64 shardId = pg_strtoint64(shardIdString);

	if (shardId <= SINGLE_NODE_ID)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("Invalid shardid: %s", shardIdString),
						errdetail_log(
							"Invalid shardid: %s, shardId provided doesn't match the"
							" expected format from currentOp", shardIdString)));
	}

	int nargs = 0;
	bool readOnly = false;
	Oid *argTypes = NULL;
	Datum *argValues = NULL;
	char *argNulls;

	const char *killOpQuery = GetOperationCancellationQuery(shardId, &parsedArgs.opIdView,
															&nargs, &argTypes, &argValues,
															&argNulls,
															GetDefaultOperationCancellationQuery);

	/* If hook doesn't provide any query it's no-op success */
	if (killOpQuery != NULL && nargs > 0 && argTypes != NULL &&
		argValues != NULL && argNulls != NULL)
	{
		bool isNull = false;
		ExtensionExecuteQueryWithArgsViaSPI(killOpQuery, nargs, argTypes, argValues,
											argNulls, readOnly, SPI_OK_SELECT, &isNull);
	}

	/* Build success response */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	StringInfo shardName = makeStringInfo();
	int processId = shardId % SINGLE_NODE_ID;
	int shardNameId = (int) ((shardId - processId) / SINGLE_NODE_ID);
	appendStringInfo(shardName, "shard%d", shardNameId);

	PgbsonWriterAppendUtf8(&writer, "shard", 5, shardName->data);
	PgbsonWriterAppendInt32OrDouble(&writer, "shardid", 7, processId);
	PgbsonWriterAppendDouble(&writer, "ok", 2, 1.0);

	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
}


/*=================*/
/* Private helpers */
/*=================*/

static void
ValidateAndParseKillOpCommand(pgbson *commandSpec, ParsedKillOpArgs *parsedArgs)
{
	bson_iter_t commandIter;
	PgbsonInitIterator(commandSpec, &commandIter);
	while (bson_iter_next(&commandIter))
	{
		StringView keyView = bson_iter_key_string_view(&commandIter);

		if (StringViewEqualsCString(&keyView, "op"))
		{
			EnsureTopLevelFieldType("op", &commandIter, BSON_TYPE_UTF8);
			uint32 length = 0;
			const char *operationId = bson_iter_utf8(&commandIter, &length);
			if (length == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("The op field in killOp cannot be empty")));
			}
			StringView operationIdView = CreateStringViewFromString(operationId);

			parsedArgs->opIdView = StringViewFindSuffix(&operationIdView, ':');
			if (parsedArgs->opIdView.length == 0 ||
				(parsedArgs->opIdView.length + 1 == operationIdView.length))
			{
				/*
				 * Validate incorrect formats for the operationId
				 * such as '', ':<id>', '<id>:'
				 * Valid format is shardid:opid
				 */
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28625),
								errmsg(
									"The op argument to killOp must be of the format shardid:opid but found %.*s",
									operationIdView.length, operationIdView.string),
								errdetail_log(
									"The op argument to killOp must be of the format shardid:opid but found %.*s",
									operationIdView.length, operationIdView.string)));
			}

			parsedArgs->shardIdView.string = operationIdView.string;
			parsedArgs->shardIdView.length = operationIdView.length -
											 parsedArgs->opIdView.length - 1; /* -1 for `:` */

			CheckValidIdentifier(&parsedArgs->shardIdView, "shardId");
			CheckValidIdentifier(&parsedArgs->opIdView, "opId");
		}
		else if (StringViewEqualsCString(&keyView, "$db"))
		{
			EnsureTopLevelFieldType("$db", &commandIter, BSON_TYPE_UTF8);
			parsedArgs->databaseName = (char *) bson_iter_utf8(&commandIter, NULL);
		}
		else if (StringViewEqualsCString(&keyView, "killOp"))
		{
			/* This is the command name, ignore */
			continue;
		}
		else if (!IsCommonSpecIgnoredField(keyView.string))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("%.*s is an unknown field",
								   keyView.length, keyView.string),
							errdetail_log("%.*s is an unknown field",
										  keyView.length, keyView.string)));
		}
	}

	if (parsedArgs->shardIdView.length == 0 || parsedArgs->opIdView.length == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION50759),
						errmsg("Did not provide \"op\" field")));
	}

	if (parsedArgs->databaseName == NULL || strcmp(parsedArgs->databaseName, "admin") !=
		0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("killOp may only be run against the admin database.")));
	}

	if (!has_privs_of_role(GetUserId(), ROLE_PG_SIGNAL_BACKEND))
	{
		/*
		 * Be strict about who can call killOp and attemp to kill backends, only allow
		 * superusers and explicit users having pg_signal_backend role.
		 *
		 * TODO: In future when user/roles are supported, this can be extended similar to PG
		 * to allow users owning their backends to be cancelled even if they miss the above
		 * roles
		 */
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNAUTHORIZED),
						errmsg("not authorized on admin to execute command"
							   " { killOp: 1, op: \"%.*s:%.*s\" }",
							   parsedArgs->shardIdView.length,
							   parsedArgs->shardIdView.string,
							   parsedArgs->opIdView.length,
							   parsedArgs->opIdView.string)));
	}
}


/*
 * Get the default operation cancellation query for single node scenario.
 */
static const char *
GetDefaultOperationCancellationQuery(int64 shardId, StringView *opIdView, int *nargs,
									 Oid **argTypes, Datum **argValues,
									 char **argNulls)
{
	Assert(shardId > SINGLE_NODE_ID &&
		   opIdView != NULL &&
		   opIdView->length > 0);

	/* We extract only the PID for single node cases */
	int pid = (int) (shardId - SINGLE_NODE_ID);

	StringInfo query = makeStringInfo();

	/*
	 * KillOp query attempts to cancel any operation that is still active but is a no-op
	 * when the operation is already finished the connection state is 'idle', in order to
	 * kill idle connection we have to force terminate the backend.
	 */
	appendStringInfo(query,
					 " SELECT "
					 "  CASE WHEN state = 'idle' THEN pg_terminate_backend($1)"
					 "       ELSE pg_cancel_backend($1)"
					 "  END "
					 " FROM pg_stat_activity WHERE pid = $1 "
					 " AND (EXTRACT(epoch FROM query_start) * 1000000)::numeric(20,0) = $2::numeric(20,0) LIMIT 1");

	*nargs = 2;
	*argTypes = palloc(sizeof(Oid) * (*nargs));
	*argValues = palloc(sizeof(Datum) * (*nargs));
	*argNulls = palloc0(sizeof(char) * (*nargs));
	(*argTypes)[0] = INT4OID;
	(*argValues)[0] = Int32GetDatum(pid);
	(*argTypes)[1] = TEXTOID;
	(*argValues)[1] = CStringGetTextDatum(opIdView->string);

	(*argNulls)[0] = ' ';
	(*argNulls)[1] = ' ';

	return query->data;
}
