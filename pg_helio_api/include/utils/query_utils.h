/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/query_utils.h
 *
 * Utilities to execute a command/query.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <utils/array.h>
#include <executor/spi.h>

#ifndef QUERY_UTILS_H
#define QUERY_UTILS_H


/*
 * Execute a query within the current transaction and return the first datum
 * of the first tuple returned by given query (if any).
 *
 * Rollbacking or aborting the current transaction / subtransaction is
 * guaranteed to rollback the query that has been executed even if query
 * execution has been completed already.
 */
Datum ExtensionExecuteQueryViaSPI(const char *query, bool readOnly, int expectedSPIOK,
								  bool *isNull);
void ExtensionExecuteMultiValueQueryViaSPI(const char *query, bool readOnly,
										   int expectedSPIOK, Datum *datums,
										   bool *isNull, int numValues);
Datum ExtensionExecuteQueryWithArgsViaSPI(const char *query, int nargs, Oid *argTypes,
										  Datum *argValues, char *argNulls,
										  bool readOnly, int expectedSPIOK,
										  bool *isNull);
Datum ExtensionExecuteCappedQueryWithArgsViaSPI(const char *query, int nargs,
												Oid *argTypes,
												Datum *argValues, char *argNulls,
												bool readOnly, int expectedSPIOK,
												bool *isNull, int statementTimeout,
												int lockTimeout);
void ExtensionExecuteMultiValueQueryWithArgsViaSPI(const char *query, int nargs,
												   Oid *argTypes, Datum *argValues,
												   char *argNulls, bool readOnly,
												   int expectedSPIOK, Datum *datums,
												   bool *isNull, int numValues);
uint64 ExtensionExecuteCappedStatementWithArgsViaSPI(const char *query, int nargs,
													 Oid *argTypes,
													 Datum *argValues, char *argNulls,
													 bool readOnly, int expectedSPIOK,
													 int statementTimeout, int
													 lockTimeout);

/*
 * Execute a query outside the current transaction and return the first
 * attribute of the first tuple returned by given query (if any).
 *
 * After the execution of given query completes, it cannot be rollbacked even
 * if current transaction / subtransaction rollbacks or aborts.
 *
 * Otherwise, i.e.: if this function didn't return yet, then there is still
 * a chance to cancel the query when rollbacking or aborting the current the
 * transaction / subtransaction since the abort handlers in
 * TransactionCallback & SubTransactionCallback will then try
 * cancelling the query. However, it is still not guaranteed that given query
 * will be rollbacked. This is because;
 *   i)  The transaction that we started over the libpq connection might got
 *       committed already, i.e.: remote transaction is not active anymore.
 *   ii) Theoretically, connection establishment to localhost might still fail
 *       while trying to send the cancellation.
 *
 * [1] Why can using a libpq connection might cause an undetectable deadlock ?
 *     (Please keep in mind that "s" means "session" and "r" means "resource"
 *     in this scenario.)
 *
 *     Suppose that s1 acquired r1 and waits for r2, and s2 acquired r2 and
 *     waits for r1. In that case, postgres would throw an error telling that
 *     those two sessions entered into a deadlock. Or, if those resources belong
 *     to different Citus nodes (coordinator / MX worker), then Citus would
 *     report this as a "distributed deadlock" instead of postgres.
 *
 *     And now let's consider another scenario. Suppose that s1 acquired r1
 *     and the libpq connection (initiated via ExtensionExecuteQueryOnLocalhostViaLibPQ())
 *     that s1 is waiting for is waiting for r2, and s2 acquired r2 and it
 *     waits for r1. Here, s2 waits for s1 to release a resource, and s1 waits
 *     for the libpq to finish its IO; and that libpq connection is waiting for
 *     s2 to release a resource. Given the wait-triangle here, one could expect
 *     postgres or Citus to detect this as a deadlock condition where three
 *     processes involved; but this is not the case. This is because, postgres
 *     doesn't take the connection-IO waits into the account (even if they are
 *     local) when detecting deadlock conditions and such a scenario would cause
 *     those processes to wait for each other indefinitely. For this reason, callers
 *     should be careful about such cases when using ExtensionExecuteQueryViaLibPQ()
 *     instead of ExtensionExecuteQueryViaSPI() or such.
 */
char * ExtensionExecuteQueryOnLocalhostViaLibPQ(char *query);

/*
 * Same as ExtensionExecuteQueryOnLocalhostViaLibPQ, but connects as specific user.
 */
char * ExtensionExecuteQueryAsUserOnLocalhostViaLibPQ(char *query, const Oid userOid,
													  bool useSerialExecution);

/* Same as ExtensionExecuteQueryAsUserOnLocalhostViaLibPQ, but it allows to execute parameterized query */
char * ExtensionExecuteQueryWithArgsAsUserOnLocalhostViaLibPQ(char *query, const Oid
															  userOid, int nParams,
															  Oid *paramTypes, const
															  char **parameterValues);

/*
 * Helper method to create a string query using a variable number
 * of arguments - similar to doing
 * StringInfo s = makeStringInfo();
 * appendStringInfo(s, "queryFormat", args);
 * return s->data;
 */
pg_attribute_printf(1, 2) inline static const char *
FormatSqlQuery(const char *queryFormat, ...)
{
	StringInfoData cmdbuf;

	initStringInfo(&cmdbuf);
	for (;;)
	{
		va_list args;
		int needed;

		va_start(args, queryFormat);
		needed = appendStringInfoVA(&cmdbuf, queryFormat, args);
		va_end(args);
		if (needed == 0)
		{
			break;              /* success */
		}
		enlargeStringInfo(&cmdbuf, needed);
	}

	return cmdbuf.data;
}

#endif
