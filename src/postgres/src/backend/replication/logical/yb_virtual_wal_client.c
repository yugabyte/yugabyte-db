/*--------------------------------------------------------------------------------------------------
 *
 * yb_virtual_wal_client.c
 *        Commands for readings records from the YB Virtual WAL exposed by the CDC service.
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *        src/postgres/src/backend/replication/logical/yb_virtual_wal_client.c
 *
 *--------------------------------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/xact.h"
#include "commands/ybccmds.h"
#include "replication/slot.h"
#include "replication/yb_virtual_wal_client.h"
#include "utils/memutils.h"

static MemoryContext virtual_wal_context = NULL;

/* Cached records received from the CDC service. */
static YBCPgChangeRecordBatch *cached_records = NULL;
static size_t cached_records_last_sent_row_idx = 0;

typedef struct UnackedTransactionInfo {
	TransactionId xid;
	XLogRecPtr begin_lsn;
	XLogRecPtr commit_lsn;
} YBUnackedTransactionInfo;

/*
 * A List of YBUnackedTransactionInfo.
 *
 * Keeps tracked of the metadata of all transactions that are yet to be
 * confirmed as flushed by the client. Used for the calculation of restart_lsn
 * from the confirmed_flush_lsn sent by the client.
 *
 * Transactions are appended at the end of the list and removed from the start.
 *
 * Relies on the following guarantees:
 * 1. No interleaved transactions sent from the GetConsistentChanges RPC
 * 2. Only committed transactions are sent from the GetConsistentChanges RPC
 * 3. The LSN values of transaction BEGIN/COMMIT are monotonically increasing
 *    i.e. if commit_ht(t2) > commit_ht(t1), then:
 *        a) begin_lsn(t2) > begin_lsn(t1)
 *        b) commit_lsn(t2) > commit_lsn (t1).
 *    Also note that begin_lsn(t2) > commit_lsn(t1) by the guarantee of no
 *    interleaved transactions.
 *
 * The size of this list depends on how fast the client confirms the flush of
 * the streamed changes.
 *
 * TODO(#21399): Store this in a separate memory context for ease of tracking.
 */
static List *unacked_transactions = NIL;

static List *YBCGetTables(List *publication_names);
static void InitVirtualWal(List *publication_names);

static void TrackUnackedTransaction(YBCPgVirtualWalRecord *record);
static XLogRecPtr CalculateRestartLSN(XLogRecPtr confirmed_flush);
static void CleanupAckedTransactions(XLogRecPtr confirmed_flush);

void
YBCInitVirtualWal(List *yb_publication_names)
{
	MemoryContext	caller_context;

	elog(DEBUG1, "YBCInitVirtualWal");

	virtual_wal_context = AllocSetContextCreate(GetCurrentMemoryContext(),
												"YB virtual WAL context",
												ALLOCSET_DEFAULT_SIZES);
	caller_context = GetCurrentMemoryContext();

	/* Start a transaction to be able to read the catalog tables. */
	StartTransactionCommand();

	/*
	 * Allocate any data within the virtual wal context i.e. outside of the
	 * transaction context.
	 */
	MemoryContextSwitchTo(virtual_wal_context);

	InitVirtualWal(yb_publication_names);

	AbortCurrentTransaction();
	MemoryContextSwitchTo(caller_context);

	unacked_transactions = NIL;
}

void
YBCDestroyVirtualWal()
{
	YBCDestroyVirtualWalForCDC();

	if (virtual_wal_context)
		MemoryContextDelete(virtual_wal_context);
}

static List *
YBCGetTables(List *publication_names)
{
	List	*yb_publications;

	Assert(IsTransactionState());

	yb_publications =
		YBGetPublicationsByNames(publication_names, false /* missing_ok */);

	return yb_pg_get_publications_tables(yb_publications);
}

static void
InitVirtualWal(List *publication_names)
{
	List		*tables;
	Oid			*table_oids;

	tables = YBCGetTables(publication_names);

	table_oids = palloc(sizeof(Oid) * list_length(tables));
	ListCell *lc;
	size_t table_idx = 0;
	foreach (lc, tables)
		table_oids[table_idx++] = lfirst_oid(lc);

	YBCInitVirtualWalForCDC(MyReplicationSlot->data.yb_stream_id, table_oids,
							list_length(tables));

	pfree(table_oids);
	list_free(tables);
}

YBCPgVirtualWalRecord *
YBCReadRecord(XLogReaderState *state, char **errormsg)
{
	MemoryContext			caller_context;
	YBCPgVirtualWalRecord	*record = NULL;

	elog(DEBUG4, "YBCReadRecord");

	caller_context = MemoryContextSwitchTo(virtual_wal_context);

	/* reset error state */
	*errormsg = NULL;
	state->errormsg_buf[0] = '\0';

	YBResetDecoder(state);

	/* Fetch a batch of changes from CDC service if needed. */
	if (cached_records == NULL ||
		cached_records_last_sent_row_idx >= cached_records->row_count)
	{
		elog(DEBUG5, "YBCReadRecord: Fetching a fresh batch of changes.");

		/* We no longer need the earlier record batch. */
		if (cached_records)
			pfree(cached_records);

		YBCGetCDCConsistentChanges(MyReplicationSlot->data.yb_stream_id,
								   &cached_records);

		cached_records_last_sent_row_idx = 0;
	}
	Assert(cached_records);

	/*
	 * We did not get any records from CDC service, return NULL and retry in the
	 * next iteration.
	 */
	if (!cached_records || cached_records->row_count == 0)
	{
		/*
		 * TODO(#20726): Sleep for a configurable amount of time here to avoid
		 * spamming the CDC service.
		 */
		MemoryContextSwitchTo(caller_context);
		return NULL;
	}

	record = &cached_records->rows[cached_records_last_sent_row_idx++];
	state->ReadRecPtr = record->lsn;
	state->yb_virtual_wal_record = record;

	TrackUnackedTransaction(record);

	MemoryContextSwitchTo(caller_context);
	return record;
}

static void
TrackUnackedTransaction(YBCPgVirtualWalRecord *record)
{
	YBUnackedTransactionInfo *transaction = NULL;

	switch (record->action)
	{
		case YB_PG_ROW_MESSAGE_ACTION_BEGIN:
		{
			transaction = palloc(sizeof(YBUnackedTransactionInfo));
			transaction->xid = record->xid;
			transaction->begin_lsn = record->lsn;
			transaction->commit_lsn = InvalidXLogRecPtr;

			unacked_transactions = lappend(unacked_transactions, transaction);
			break;
		}

		case YB_PG_ROW_MESSAGE_ACTION_COMMIT:
		{
			YBUnackedTransactionInfo *txninfo = NULL;

			/*
			 * We should at least have one transaction which we appended while
			 * handling the corresponding BEGIN record.
			 */
			Assert(list_length(unacked_transactions) > 0);

			txninfo = (YBUnackedTransactionInfo *) lfirst(
				list_tail((List *) unacked_transactions));
			Assert(txninfo->xid == record->xid);
			Assert(txninfo->begin_lsn != InvalidXLogRecPtr);
			Assert(txninfo->commit_lsn == InvalidXLogRecPtr);

			txninfo->commit_lsn = record->lsn;
			break;
		}

		/* Not of interest here. */
		case YB_PG_ROW_MESSAGE_ACTION_UNKNOWN: switch_fallthrough();
		case YB_PG_ROW_MESSAGE_ACTION_INSERT: switch_fallthrough();
		case YB_PG_ROW_MESSAGE_ACTION_UPDATE: switch_fallthrough();
		case YB_PG_ROW_MESSAGE_ACTION_DELETE:
			return;
	}
}

XLogRecPtr
YBCGetFlushRecPtr(void)
{
	/*
	 * The FlushRecPtr is used by the walsender to save CPU cycles when there is
	 * no more WAL data to stream. It is compared against the LSN of the last
	 * record streamed to the client. If the walsender has sent all the data
	 * i.e. sentPtr >= flushRecPtr, walsender sleeps on a condition variable and
	 * is awakened by the PG WAL when there is more data to be streamed.
	 *
	 * This mechanism is not applicable to YSQL yet as DocDB does not provide a
	 * mechanism to check if there are more WAL entries to stream. So we always
	 * return UINT64_MAX from here so that the walsender always thinks that
	 * there is more data to be streamed and we continue to poll records from
	 * CDC service.
	 */
	return PG_UINT64_MAX;
}

XLogRecPtr
YBCCalculatePersistAndGetRestartLSN(XLogRecPtr confirmed_flush)
{
	XLogRecPtr		restart_lsn_hint = CalculateRestartLSN(confirmed_flush);
	YBCPgXLogRecPtr	restart_lsn = InvalidXLogRecPtr;

	/* There was nothing to ack, so we can return early. */
	if (restart_lsn_hint == InvalidXLogRecPtr)
	{
		elog(DEBUG4, "No unacked transaction were found, skipping the "
					 "persistence of confirmed_flush and restart_lsn_hint");
		return restart_lsn_hint;
	}

	YBCUpdateAndPersistLSN(MyReplicationSlot->data.yb_stream_id, restart_lsn_hint,
						   confirmed_flush, &restart_lsn);

	CleanupAckedTransactions(confirmed_flush);
	return restart_lsn;
}

static XLogRecPtr
CalculateRestartLSN(XLogRecPtr confirmed_flush)
{
	XLogRecPtr					restart_lsn = InvalidXLogRecPtr;
	ListCell					*lc;
	YBUnackedTransactionInfo	*txn;
	int							numunacked = list_length(unacked_transactions);

	if (numunacked == 0)
		return InvalidXLogRecPtr;

	foreach (lc, unacked_transactions)
	{
		txn = (YBUnackedTransactionInfo *) lfirst(lc);

		/*
		 * The previous transaction was fully flushed by the client but this
		 * transaction hasn't been flushed or flushed partially. So if we were
		 * to restart at this point, streaming should start from the begin of
		 * this transaction which is logically equivalent to the end of the last
		 * transaction.
		 *
		 * Example: 3 unacked transactions with begin and commit lsn as follows
		 * T1: [3, 5]
		 * T2: [7, 10]
		 * T3: [11, 14]
		 * Suppose confirmed_flush = 8, then:
		 * The restart lsn could be 6 or 7 in this case. We choose 6 here as
		 * the commit_lsn(T1) + 1.
		 */
		if (confirmed_flush < txn->commit_lsn)
			break;

		/*
		 * The client has fully flushed this transaction. If there were to
		 * be any restart at this point, the streaming should start from the
		 * next unacked transaction onwards (if it exists).
		 *
		 * There are two cases here:
		 * 1. There are more transactions in the unacked_transactions list after
		 * this one: the next iteration of this 'for' loop will handle this
		 * case.
		 * 2. This is the last unacked transaction: The assignment of
		 * restart_lsn below handles this case.
		 */
		restart_lsn = txn->commit_lsn + 1;
	}

	return restart_lsn;
}

/*
 * Delete all transactions which have been fully flushed by the client and will
 * not be streamed again.
 */
static void
CleanupAckedTransactions(XLogRecPtr confirmed_flush)
{
	ListCell					*cell;
	ListCell					*next;
	YBUnackedTransactionInfo	*txn;

	for (cell = list_head(unacked_transactions); cell; cell = next)
	{
		txn = (YBUnackedTransactionInfo *) lfirst(cell);
		next = lnext(unacked_transactions, cell);

		if (txn->commit_lsn <= confirmed_flush)
			unacked_transactions =
				list_delete_cell(unacked_transactions, cell);
		else
			break;
	}
}
