/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/utils/query_utils.c
 *
 * Utilities to execute a command/query.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <libpq-fe.h>
#include <miscadmin.h>
#include <catalog/pg_type.h>
#include <commands/dbcommands.h>
#include <common/username.h>
#include <executor/spi.h>
#include <postmaster/postmaster.h>
#include <storage/latch.h>
#include <utils/array.h>
#include <utils/datum.h>
#include <utils/syscache.h>
#include <utils/wait_event.h>

#include "commands/connection_management.h"
#include "utils/query_utils.h"
#include "api_hooks.h"

extern char *LocalhostConnectionString;

/* Optional flags for running LibPQ on serial execution mode. */
char *SerialExecutionFlags = NULL;

static Datum SPIReturnDatum(bool *isNull, int position);
static char * ExtensionExecuteQueryViaLibPQ(char *query, char *connStr);
static char * ExtensionExecuteQueryWithArgsViaLibPQ(char *query, char *connStr, int
													nParams, Oid *paramTypes, const
													char **parameterValues);
static void PGConnFinishConnectionEstablishment(PGconn *conn);
static void PGConnFinishIO(PGconn *conn);
static char * PGConnReturnFirstField(PGconn *conn);
static void PGConnReportError(PGconn *conn, PGresult *result, int elevel);
static char * GetLocalhostConnStr(const Oid userOid, bool useSerialExecution);

/*
 * ExtensionExecuteQueryViaSPI executes given query via SPI and returns first
 * attribute of the first tuple returned.
 *
 * Throws an error if command fails, or connection establishment to SPI fails
 * for some reason.
 *
 * Note that returning NULL doesn't mean failure. It either means returned
 * attribute is NULL or query returned nothing at all. In that case, isNull
 * is set to true.
 */
Datum
ExtensionExecuteQueryViaSPI(const char *query, bool readOnly, int expectedSPIOK,
							bool *isNull)
{
	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, (errmsg("could not connect to SPI manager")));
	}

	ereport(DEBUG1, (errmsg("executing \"%s\" via SPI", query)));

	int tupleCountLimit = 1;
	int spiErrorCode = SPI_execute(query, readOnly, tupleCountLimit);
	if (spiErrorCode != expectedSPIOK)
	{
		ereport(ERROR, (errmsg("could not run SPI query %d", spiErrorCode)));
	}

	Datum retDatum = SPIReturnDatum(isNull, 1);

	if (SPI_finish() != SPI_OK_FINISH)
	{
		ereport(ERROR, (errmsg("could not finish SPI connection")));
	}

	return retDatum;
}


/*
 * ExtensionExecuteMultiValueQueryViaSPI executes given query via SPI just
 * like ExtensionExecuteQueryViaSPI. However, it returns up to N attribute
 * (column) values for the first tuple returned by the Query.
 *
 * Note that returning NULL doesn't mean failure. It either means returned
 * attribute is NULL or query returned nothing at all. In that case, isNull
 * is set to true.
 */
void
ExtensionExecuteMultiValueQueryViaSPI(const char *query, bool readOnly, int expectedSPIOK,
									  Datum *datums, bool *isNull, int numValues)
{
	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, (errmsg("could not connect to SPI manager")));
	}

	ereport(DEBUG1, (errmsg("executing \"%s\" via SPI", query)));

	int tupleCountLimit = 1;
	if (SPI_execute(query, readOnly, tupleCountLimit) != expectedSPIOK)
	{
		ereport(ERROR, (errmsg("could not run SPI query")));
	}

	for (int i = 0; i < numValues; i++)
	{
		datums[i] = SPIReturnDatum(&isNull[i], i + 1);
	}

	if (SPI_finish() != SPI_OK_FINISH)
	{
		ereport(ERROR, (errmsg("could not finish SPI connection")));
	}
}


/*
 * ExtensionExecuteQueryWithArgsViaSPI acts very same as ExtensionExecuteQueryViaSPI
 * except that it allows passing params seperately without embedding them to
 * query string by using placeholders such as $1, $2, ...
 */
Datum
ExtensionExecuteQueryWithArgsViaSPI(const char *query, int nargs, Oid *argTypes,
									Datum *argValues, char *argNulls, bool readOnly,
									int expectedSPIOK, bool *isNull)
{
	int statementTimeoutDisabled = 0;
	int lockTimeoutDisabled = 0;
	return ExtensionExecuteCappedQueryWithArgsViaSPI(
		query, nargs, argTypes, argValues, argNulls, readOnly, expectedSPIOK, isNull,
		statementTimeoutDisabled, lockTimeoutDisabled);
}


/*
 * ExtensionExecuteCappedQueryWithArgsViaSPI acts very same as ExtensionExecuteQueryViaSPI
 * except that it allows passing params seperately without embedding them to
 * query string by using placeholders such as $1, $2, ... and allows setting
 * statementTimeout and lockTimeout
 */
Datum
ExtensionExecuteCappedQueryWithArgsViaSPI(const char *query, int nargs, Oid *argTypes,
										  Datum *argValues, char *argNulls, bool readOnly,
										  int expectedSPIOK, bool *isNull,
										  int statementTimeout, int lockTimeout)
{
	int spiOptions = 0;
	if (lockTimeout > 0 || statementTimeout > 0)
	{
		spiOptions = SPI_OPT_NONATOMIC;
	}

	if (SPI_connect_ext(spiOptions) != SPI_OK_CONNECT)
	{
		ereport(ERROR, (errmsg("could not connect to SPI manager")));
	}

	ereport(DEBUG1, (errmsg("executing \"%s\" via SPI", query)));

	if (lockTimeout > 0)
	{
		int ret = SPI_exec(FormatSqlQuery("SET LOCAL lock_timeout TO %d", lockTimeout),
						   0);
		if (ret != SPI_OK_UTILITY)
		{
			elog(ERROR, "SPI_exec to set local lock_timeout failed: error code %d", ret);
		}
	}

	if (statementTimeout > 0)
	{
		int ret = SPI_exec(FormatSqlQuery("SET LOCAL statement_timeout TO %d",
										  statementTimeout), 0);
		if (ret != SPI_OK_UTILITY)
		{
			elog(ERROR, "SPI_exec to set local statement_timeout failed: error code %d",
				 ret);
		}
	}

	int tupleCountLimit = 1;
	if (SPI_execute_with_args(query, nargs, argTypes, argValues, argNulls,
							  readOnly, tupleCountLimit) != expectedSPIOK)
	{
		ereport(ERROR, (errmsg("could not run SPI query")));
	}

	Datum retDatum = SPIReturnDatum(isNull, 1);

	if (SPI_finish() != SPI_OK_FINISH)
	{
		ereport(ERROR, (errmsg("could not finish SPI connection")));
	}

	return retDatum;
}


/*
 * ExtensionExecuteCappedStatementWithArgsViaSPI acts very same as ExtensionExecuteQueryViaSPI
 * except that it allows passing params seperately without embedding them to
 * query string by using placeholders such as $1, $2, ...
 * It also doesn't return an output and instead returns the number of tuples affected by the
 * statement. Used in cases like DELETE, INSERT without returning where we only care about
 * the number of rows impacted.
 */
uint64
ExtensionExecuteCappedStatementWithArgsViaSPI(const char *query, int nargs, Oid *argTypes,
											  Datum *argValues, char *argNulls,
											  bool readOnly, int expectedSPIOK,
											  int statementTimeout, int lockTimeout)
{
	int spiOptions = 0;
	if (lockTimeout > 0 || statementTimeout > 0)
	{
		spiOptions = SPI_OPT_NONATOMIC;
	}

	if (SPI_connect_ext(spiOptions) != SPI_OK_CONNECT)
	{
		ereport(ERROR, (errmsg("could not connect to SPI manager")));
	}

	ereport(DEBUG1, (errmsg("executing \"%s\" via SPI", query)));

	if (lockTimeout > 0)
	{
		int ret = SPI_exec(FormatSqlQuery("SET LOCAL lock_timeout TO %d", lockTimeout),
						   0);
		if (ret != SPI_OK_UTILITY)
		{
			elog(ERROR, "SPI_exec to set local lock_timeout failed: error code %d", ret);
		}
	}

	if (statementTimeout > 0)
	{
		int ret = SPI_exec(FormatSqlQuery("SET LOCAL statement_timeout TO %d",
										  statementTimeout), 0);
		if (ret != SPI_OK_UTILITY)
		{
			elog(ERROR, "SPI_exec to set local statement_timeout failed: error code %d",
				 ret);
		}
	}

	int tupleCountLimit = 0;
	if (SPI_execute_with_args(query, nargs, argTypes, argValues, argNulls,
							  readOnly, tupleCountLimit) != expectedSPIOK)
	{
		ereport(ERROR, (errmsg("could not run SPI query")));
	}

	uint64 tuplesImpacted = SPI_processed;

	if (SPI_finish() != SPI_OK_FINISH)
	{
		ereport(ERROR, (errmsg("could not finish SPI connection")));
	}

	return tuplesImpacted;
}


/*
 * ExtensionExecuteMultiValueQueryWithArgsViaSPI acts very same as ExtensionExecuteMultiValueQueryViaSPI
 * except that it allows passing params seperately without embedding them to
 * query string by using placeholders such as $1, $2, ...
 */
void
ExtensionExecuteMultiValueQueryWithArgsViaSPI(const char *query, int nargs, Oid *argTypes,
											  Datum *argValues, char *argNulls,
											  bool readOnly, int expectedSPIOK,
											  Datum *datums, bool *isNull, int numValues)
{
	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, (errmsg("could not connect to SPI manager")));
	}

	ereport(DEBUG1, (errmsg("executing \"%s\" via SPI", query)));

	int tupleCountLimit = 1;
	if (SPI_execute_with_args(query, nargs, argTypes, argValues, argNulls,
							  readOnly, tupleCountLimit) != expectedSPIOK)
	{
		ereport(ERROR, (errmsg("could not run SPI query")));
	}

	for (int i = 0; i < numValues; i++)
	{
		datums[i] = SPIReturnDatum(&isNull[i], i + 1);
	}

	if (SPI_finish() != SPI_OK_FINISH)
	{
		ereport(ERROR, (errmsg("could not finish SPI connection")));
	}
}


/*
 * SPIReturnDatum copies first datum of the first tuple returned to SPI
 * by the last query that has been executed into current memory context and
 * returns that datum.
 *
 * Returns NULL if query returned nothing at all or if the returned datum
 * itself is NULL.
 *
 * Note: position is 1-based index.
 */
static Datum
SPIReturnDatum(bool *isNull, int position)
{
	*isNull = true;

	Datum retDatum = 0;
	if (SPI_processed >= 1 && SPI_tuptable && SPI_tuptable->tupdesc->natts >= position)
	{
		int tupleNumber = 0;
		AttrNumber attrNumber = position;
		Datum resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
										  SPI_tuptable->tupdesc, attrNumber, isNull);
		if (!*isNull)
		{
			/* copy datum into original memory context */
			Form_pg_attribute attr = TupleDescAttr(SPI_tuptable->tupdesc, attrNumber - 1);
			retDatum = SPI_datumTransfer(resultDatum, attr->attbyval, attr->attlen);
		}
	}

	return retDatum;
}


/*
 * ExtensionExecuteQueryViaLibPQ is a wrapper around ExtensionExecuteQueryViaLibPQ
 * that executes given query by using a libpq connection to localhost, and
 * returns first attribute of the first tuple returned.
 */
char *
ExtensionExecuteQueryOnLocalhostViaLibPQ(char *query)
{
	bool useSerialExecution = false;
	return ExtensionExecuteQueryViaLibPQ(query, GetLocalhostConnStr(InvalidOid,
																	useSerialExecution));
}


/*
 * Same as ExtensionExecuteQueryOnLocalhostViaLibPQ, but connects as specific user.
 */
char *
ExtensionExecuteQueryAsUserOnLocalhostViaLibPQ(char *query, const Oid userOid, bool
											   useSerialExecution)
{
	return ExtensionExecuteQueryViaLibPQ(query, GetLocalhostConnStr(userOid,
																	useSerialExecution));
}


/* Same as ExtensionExecuteQueryAsUserOnLocalhostViaLibPQ, but it allows to execute parameterized query */
char *
ExtensionExecuteQueryWithArgsAsUserOnLocalhostViaLibPQ(char *query, const Oid userOid, int
													   nParams, Oid *paramTypes, const
													   char **parameterValues)
{
	bool useSerialExecution = false;
	return ExtensionExecuteQueryWithArgsViaLibPQ(query, GetLocalhostConnStr(userOid,
																			useSerialExecution),
												 nParams, paramTypes, parameterValues);
}


/*
 * ExtensionExecuteQueryViaLibPQ executes given query by using a non-blocking
 * libpq connection to given host, and returns first attribute of the first
 * tuple returned.
 *
 * Throws an error if command fails or raises NOTICE/WARNING, or connection
 * establishment fails for some reason.
 *
 * Note that returning NULL doesn't mean failure. It either means returned
 * attribute is NULL or query returned nothing at all.
 *
 * Also note that unless there is a specific reason, it's more suitable to
 * use ExtensionExecuteQueryViaSPI instead of this function. See query_utils.h.
 */
static char *
ExtensionExecuteQueryViaLibPQ(char *query, char *connStr)
{
	PGconn *conn = PQconnectStart(connStr);
	if (conn == NULL)
	{
		/*
		 * Similar to PQgetResult, we dont't expect PQconnectStart to return
		 * NULL unless OOM happened.
		 */
		ereport(ERROR, (errmsg("could not establish connection, possibly "
							   "due to OOM")));
	}

	/* register so that abort handler can cancel in case of abort */
	ConnMgrResetActiveConnection(conn);

	/* we don't expect PQsetnonblocking to fail here but be on the safe side */
	int argNonBlocking = 1; /* means nonblocking=true */
	if (PQsetnonblocking(conn, argNonBlocking) != 0)
	{
		PGConnReportError(conn, NULL, ERROR);
	}

	/* XXX: maybe set a notice receiver */

	PGConnFinishConnectionEstablishment(conn);

	if (PQstatus(conn) != CONNECTION_OK)
	{
		PGConnReportError(conn, NULL, ERROR);
	}

	ereport(DEBUG1, (errmsg("executing \"%s\" via connection to \"%s\"",
							query, connStr)));

	if (!PQsendQuery(conn, query))
	{
		PGConnReportError(conn, NULL, ERROR);
	}

	if (PQisBusy(conn))
	{
		/* first need to finish any pending IO to get the result */
		PGConnFinishIO(conn);
	}

	char *retValue = PGConnReturnFirstField(conn);

	PQfinish(conn);

	/*
	 * Done with connection, so need to signal connection manager to forget
	 * about it since we cannot cancel this connection (via abort handler)
	 * anymore.
	 */
	ConnMgrForgetActiveConnection();

	return retValue;
}


/*
 * Like ExtensionExecuteQueryViaLibPQ, but gives capability to pass parameterized query
 */
static char *
ExtensionExecuteQueryWithArgsViaLibPQ(char *query, char *connStr, int nParams,
									  Oid *paramTypes, const char **parameterValues)
{
	PGconn *conn = PQconnectStart(connStr);
	if (conn == NULL)
	{
		/*
		 * Similar to PQgetResult, we dont't expect PQconnectStart to return
		 * NULL unless OOM happened.
		 */
		ereport(ERROR, (errmsg("could not establish connection, possibly "
							   "due to OOM")));
	}

	/* register so that abort handler can cancel in case of abort */
	ConnMgrResetActiveConnection(conn);

	/* we don't expect PQsetnonblocking to fail here but be on the safe side */
	int argNonBlocking = 1; /* means nonblocking=true */
	if (PQsetnonblocking(conn, argNonBlocking) != 0)
	{
		PGConnReportError(conn, NULL, ERROR);
	}

	/* XXX: maybe set a notice receiver */

	PGConnFinishConnectionEstablishment(conn);

	if (PQstatus(conn) != CONNECTION_OK)
	{
		PGConnReportError(conn, NULL, ERROR);
	}

	ereport(DEBUG1, (errmsg("executing \"%s\" via connection to \"%s\"",
							query, connStr)));

	int resultFormat = 0; /* means result in text format */
	if (!PQsendQueryParams(conn, query, nParams, paramTypes, parameterValues, NULL, NULL,
						   resultFormat))
	{
		PGConnReportError(conn, NULL, ERROR);
	}

	if (PQisBusy(conn))
	{
		/* first need to finish any pending IO to get the result */
		PGConnFinishIO(conn);
	}

	char *retValue = PGConnReturnFirstField(conn);

	PQfinish(conn);

	/*
	 * Done with connection, so need to signal connection manager to forget
	 * about it since we cannot cancel this connection (via abort handler)
	 * anymore.
	 */
	ConnMgrForgetActiveConnection();

	return retValue;
}


/*
 * PGConnFinishConnectionEstablishment finishes connections establishment
 * asynchronously for given connection if not done so yet.
 */
static void
PGConnFinishConnectionEstablishment(PGconn *conn)
{
	/*
	 * Poll connection until we have OK or FAILED status.
	 * Per spec for PQconnectPoll, first wait till socket is write-ready.
	 */
	PostgresPollingStatusType status = PGRES_POLLING_WRITING;
	do {
		int waitIOMask = (status == PGRES_POLLING_READING) ?
						 WL_SOCKET_READABLE : WL_SOCKET_WRITEABLE;

		/*
		 * As stated in pg commit 6f3bd98ebfc008cbd676da777bb0b2376c4c4bfa,
		 * reporting wait type to be PG_WAIT_EXTENSION seems reasonable.
		 *
		 * XXX: maybe wait for timeout too
		 */
		long timeout = 0;
		int waitResult = WaitLatchOrSocket(MyLatch,
										   WL_POSTMASTER_DEATH | WL_LATCH_SET |
										   waitIOMask, PQsocket(conn), timeout,
										   PG_WAIT_EXTENSION);

		if (waitResult & WL_POSTMASTER_DEATH)
		{
			ereport(ERROR, (errmsg("postmaster was shut down while establishing "
								   "libpq connection, exiting")));
		}

		if (waitResult & WL_LATCH_SET)
		{
			/* got interrupt */
			ResetLatch(MyLatch);
			CHECK_FOR_INTERRUPTS();
		}

		if (waitResult & waitIOMask)
		{
			/* socket is ready, advance the libpq state machine */
			status = PQconnectPoll(conn);
		}
	} while (status != PGRES_POLLING_OK && status != PGRES_POLLING_FAILED);
}


/*
 * PGConnFinishIO performs pending IO for given connection asynchronously.
 *
 * Callers can safely assume that given connection will not be busy anymore
 * after this function returns.
 */
static void
PGConnFinishIO(PGconn *conn)
{
	while (true)
	{
		int sendStatus = PQflush(conn);

		int waitFlags = WL_POSTMASTER_DEATH | WL_LATCH_SET;
		if (sendStatus == -1)
		{
			PGConnReportError(conn, NULL, ERROR);
		}
		else if (sendStatus == 1)
		{
			waitFlags |= WL_SOCKET_WRITEABLE;
		}

		if (PQconsumeInput(conn) == 0)
		{
			PGConnReportError(conn, NULL, ERROR);
		}

		if (PQisBusy(conn))
		{
			waitFlags |= WL_SOCKET_READABLE;
		}

		int waitAnyIOMask = WL_SOCKET_READABLE | WL_SOCKET_WRITEABLE;
		if ((waitFlags & waitAnyIOMask) == 0)
		{
			/* no IO is necessary anymore, we're done */
			break;
		}

		/*
		 * As stated in pg commit 6f3bd98ebfc008cbd676da777bb0b2376c4c4bfa,
		 * reporting wait type to be PG_WAIT_EXTENSION seems reasonable.
		 *
		 * XXX: maybe wait for timeout too
		 */
		long timeout = 0;
		int waitResult = WaitLatchOrSocket(MyLatch, waitFlags, PQsocket(conn),
										   timeout, PG_WAIT_EXTENSION);

		if (waitResult & WL_POSTMASTER_DEATH)
		{
			ereport(ERROR, (errmsg("postmaster was shut down while establishing "
								   "libpq connection, exiting")));
		}

		if (waitResult & WL_LATCH_SET)
		{
			/* got interrupt */
			ResetLatch(MyLatch);
			CHECK_FOR_INTERRUPTS();
		}
	}

	if (PQstatus(conn) == CONNECTION_BAD)
	{
		PGConnReportError(conn, NULL, ERROR);
	}

	/*
	 * Finished any pending IO, so should not be busy now; but be on the
	 * safe side.
	 */
	if (PQisBusy(conn))
	{
		ereport(ERROR, (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("could not finish IO over libpq connection")));
	}
}


/*
 * PGConnReturnFirstField copies value of the first attribute of the first
 * tuple that given connection returned into current memory context and
 * returns that value.
 *
 * Returns NULL if query returned nothing at all or if the returned attribute
 * itself is NULL.
 */
static char *
PGConnReturnFirstField(PGconn *conn)
{
	PGresult *execResult = PQgetResult(conn);
	if (execResult == NULL)
	{
		/*
		 * Postgres docs states that:
		 *   A non-null pointer will generally be returned except in
		 *   out-of-memory conditions or serious errors such as inability to
		 *   send the command to the server.
		 *
		 * Since PQresultStatus returns PGRES_FATAL_ERROR even when execResult
		 * is NULL too, let's first check this to be more clear.
		 */
		ereport(ERROR, (errmsg("could not fetch result from libpq connection, "
							   "possibly due to OOM")));
	}

	ExecStatusType resultStatus = PQresultStatus(execResult);
	if (resultStatus == PGRES_FATAL_ERROR ||
		resultStatus == PGRES_NONFATAL_ERROR)
	{
		PGConnReportError(conn, execResult, ERROR);
	}

	char *retValue = NULL;
	if (resultStatus == PGRES_COMMAND_OK)
	{
		/* will return NULL */
	}
	else if (resultStatus == PGRES_TUPLES_OK)
	{
		int tupleNumber = 0;
		int fieldNumber = 0;
		retValue = PQgetvalue(execResult, tupleNumber, fieldNumber);
		retValue = retValue ? pstrdup(retValue) : NULL;
	}
	else
	{
		ereport(ERROR, (errmsg("got not-implemented libpq result type")));
	}

	PQclear(execResult);
	return retValue;
}


/*
 * PGConnReportError reports failure associated with given PGconn or the
 * PGresult if it's passed to be non-NULL.
 *
 * That means, callers that only want to report the error associated with
 * connection --or that don't have such a result object at hand-- should pass
 * result to be NULL.
 *
 * Otherwise, this function would first attempt to extract the error from
 * given result, then from the connection itself if the result itself doesn't
 * contain any errors.
 *
 * If elevel is indicates an "error" that client can recover (means ERROR
 * atm), then frees given result (if passed to be non-NULL).
 *
 * Otherwise, i.e.: if throwing a harder error such as FATAL or only reporting
 * the error (e.g.: DEBUG/LOG message), then it doesn't perform such a cleanup.
 *
 * Note that we never shutdown the connection here (i.e.: PQfinish) since the
 * abort handler is responsible for doing that if a (recoverable) failure happens.
 */
void
PGConnReportError(PGconn *conn, PGresult *result, int elevel)
{
	PG_TRY();
	{
		char *sqlStateString = PQresultErrorField(result, PG_DIAG_SQLSTATE);
		char *messagePrimary = PQresultErrorField(result, PG_DIAG_MESSAGE_PRIMARY);
		char *messageDetail = PQresultErrorField(result, PG_DIAG_MESSAGE_DETAIL);
		char *messageHint = PQresultErrorField(result, PG_DIAG_MESSAGE_HINT);
		char *messageContext = PQresultErrorField(result, PG_DIAG_CONTEXT);

		/*
		 * If the result did not contain a message or is passed to be NULL,
		 * then use ERRCODE_CONNECTION_FAILURE.
		 */
		int sqlState = ERRCODE_CONNECTION_FAILURE;
		if (sqlStateString != NULL)
		{
			sqlState = MAKE_SQLSTATE(sqlStateString[0],
									 sqlStateString[1],
									 sqlStateString[2],
									 sqlStateString[3],
									 sqlStateString[4]);
		}

		/*
		 * If the result did not contain a message or is passed to be NULL,
		 * then the connection may provide a suitable top level one. At worst,
		 * this is an empty string.
		 */
		if (messagePrimary == NULL)
		{
			messagePrimary = pchomp(PQerrorMessage(conn));
		}

		ereport(elevel, (errcode(sqlState),
						 errmsg("%s", messagePrimary),
						 messageDetail ? errdetail("%s", messageDetail) : 0,
						 messageHint ? errhint("%s", messageHint) : 0,
						 messageContext ? errcontext("%s", messageContext) : 0,
						 errcontext("while executing command over libpq connection")));
	}
	PG_CATCH();
	{
		/*
		 * We get here only when elevel is passed to be ERROR due to ereport
		 * call above. In that case, we should free the result when re-throwing
		 * the error because the caller can't.
		 *
		 * If we are throwing a harder error (such as FATAL), then we wouldn't
		 * end up here anyway, and in that case, it already doesn't make sense
		 * to do such a cleanup.
		 */
		PQclear(result);
		PG_RE_THROW();
	}
	PG_END_TRY();
}


/*
 * GetLocalhostConnStr returns connection string to be used when connecting
 * to current database of localhost.
 * If userOid is InvalidOid, falls back to authenticated userId.
 */
static char *
GetLocalhostConnStr(const Oid userOid, bool useSerialExecution)
{
	const char *user_name;
	const bool no_err = false;
	if (userOid == InvalidOid)
	{
		user_name = GetUserNameFromId(GetAuthenticatedUserId(), no_err);
	}
	else
	{
		user_name = GetUserNameFromId(userOid, no_err);
	}

	const char *applicationName = GetDistributedApplicationName();

	if (applicationName == NULL)
	{
		applicationName = "HelioDBInternal";
	}

	StringInfo localhostConnStr = makeStringInfo();
	appendStringInfo(localhostConnStr,
					 "%s port=%d user=%s dbname=%s application_name='%s'",
					 LocalhostConnectionString, PostPortNumber,
					 user_name,
					 get_database_name(MyDatabaseId),
					 applicationName);

	if (useSerialExecution && SerialExecutionFlags != NULL)
	{
		appendStringInfoString(localhostConnStr, SerialExecutionFlags);
	}

	return localhostConnStr->data;
}
