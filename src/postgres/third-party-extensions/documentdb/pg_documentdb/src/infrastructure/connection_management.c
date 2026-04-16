
/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/commands/connection_management.c
 *
 * Functions and callbacks related with connection management.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <libpq-fe.h>
#include <nodes/pg_list.h>

#include "commands/connection_management.h"


static PGconn *ActiveConnection = NULL;


static bool PGConnXactIsActive(PGconn *conn);
static bool PGConnTryCancel(PGconn *conn);


/*
 * ConnMgrTryCancelActiveConnection tries cancelling the active libpq
 * connection if it exists and is active, and resets ActiveConnection.
 *
 * Must not throw an hard error since the abort handler would interpret any
 * hard error thrown here as a FATAL one.
 */
void
ConnMgrTryCancelActiveConnection(void)
{
	if (!ActiveConnection)
	{
		/* there is no active libpq connection */
		return;
	}

	/*
	 * Try canceling the query if remote transaction is still active.
	 * Otherwise, it is already committed, cannot cancel it anymore.
	 */
	if (PGConnXactIsActive(ActiveConnection))
	{
		PGConnTryCancel(ActiveConnection);
	}

	PQfinish(ActiveConnection);

	ActiveConnection = NULL;
}


/*
 * PGConnXactIsActive returns true if remote transaction associated with
 * given connection is still active.
 */
static bool
PGConnXactIsActive(PGconn *conn)
{
	return PQstatus(conn) == CONNECTION_OK &&
		   PQtransactionStatus(conn) == PQTRANS_ACTIVE;
}


/*
 * PGConnTryCancel tries to cancel given connection and returns true on
 * success. On failure, emits a warning and returns false.
 */
static bool
PGConnTryCancel(PGconn *conn)
{
	PGcancel *cancelObject = PQgetCancel(conn);
	if (cancelObject == NULL)
	{
		ereport(WARNING, (errmsg("could not issue cancel request for libpq "
								 "connection due to %s",
								 (PQsocket(conn) == PGINVALID_SOCKET) ?
								 "invalid socket" : "OOM")));
		return false;
	}

	/* as noted in PQcancel, 256 seems sufficient for errorBuffer */
	char errorBuffer[256] = { 0 };
	bool canceled = PQcancel(cancelObject, errorBuffer, sizeof(errorBuffer));
	if (!canceled)
	{
		ereport(WARNING, (errmsg("could not issue cancel request for libpq "
								 "connection: %s", errorBuffer)));
	}

	PQfreeCancel(cancelObject);
	return canceled;
}


/*
 * ConnMgrForgetActiveConnection resets active connection.
 */
void
ConnMgrForgetActiveConnection(void)
{
	Assert(ActiveConnection);
	ActiveConnection = NULL;
}


/*
 * ConnMgrResetActiveConnection sets ActiveConnection to given
 * connection so that the abort handler can then try cancelling it if the
 * current transaction / subtransaction rollbacks or aborts.
 */
void
ConnMgrResetActiveConnection(PGconn *conn)
{
	Assert(!ActiveConnection);
	ActiveConnection = conn;
}
