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

#include <inttypes.h>

#include "access/xact.h"
#include "commands/ybccmds.h"
#include "pg_yb_utils.h"
#include "replication/slot.h"
#include "replication/walsender_private.h"
#include "replication/yb_virtual_wal_client.h"
#include "utils/memutils.h"

static MemoryContext virtual_wal_context = NULL;
static MemoryContext cached_records_context = NULL;
static MemoryContext unacked_txn_list_context = NULL;

/* Cached records received from the CDC service. */
static YBCPgChangeRecordBatch *cached_records = NULL;
static size_t cached_records_last_sent_row_idx = 0;
static bool last_getconsistentchanges_response_empty = false;
static TimestampTz last_getconsistentchanges_response_receipt_time;
static XLogRecPtr last_txn_begin_lsn = InvalidXLogRecPtr;

/*
 * Marker to refresh publication's tables list. If set to true call
 * UpdatePublicationTableList before calling next GetConsistentChanges.
 */
static bool needs_publication_table_list_refresh = false;

/* The time at which the list of tables in the publication needs to be provided to the VWAL. */
static uint64_t publication_refresh_time = 0;

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
 */
static List *unacked_transactions = NIL;

static List *YBCGetTables(List *publication_names);
static void InitVirtualWal(List *publication_names);

static void PreProcessBeforeFetchingNextBatch();

static void TrackUnackedTransaction(YBCPgVirtualWalRecord *record);
static XLogRecPtr CalculateRestartLSN(XLogRecPtr confirmed_flush);
static void CleanupAckedTransactions(XLogRecPtr confirmed_flush);

static Oid *YBCGetTableOids(List *tables);
static void YBCRefreshReplicaIdentities();

void
YBCInitVirtualWal(List *yb_publication_names)
{
	MemoryContext	caller_context;

	elog(DEBUG1, "YBCInitVirtualWal");

	virtual_wal_context = AllocSetContextCreate(GetCurrentMemoryContext(),
												"YB virtual WAL context",
												ALLOCSET_DEFAULT_SIZES);
	/*
	 * A separate memory context for the cached record batch that we receive
	 * from the CDC service as a child of the virtual wal context. Makes it
	 * easier to free the batch before requesting another batch.
	 */
	cached_records_context = AllocSetContextCreate(virtual_wal_context,
													 "YB cached record batch "
													 "context",
													 ALLOCSET_DEFAULT_SIZES);
	/*
	 * A separate memory context for the unacked txn list as a child of the
	 * virtual wal context.
	 */
	unacked_txn_list_context = AllocSetContextCreate(virtual_wal_context,
													 "YB unacked txn list "
													 "context",
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
	last_getconsistentchanges_response_empty = false;
	last_getconsistentchanges_response_receipt_time = 0;
	last_txn_begin_lsn = InvalidXLogRecPtr;

	needs_publication_table_list_refresh = false;
}

void
YBCDestroyVirtualWal()
{
	YBCDestroyVirtualWalForCDC();

	if (unacked_txn_list_context)
		MemoryContextDelete(unacked_txn_list_context);

	if (cached_records_context)
		MemoryContextDelete(cached_records_context);

	if (virtual_wal_context)
		MemoryContextDelete(virtual_wal_context);

	needs_publication_table_list_refresh = false;
}

static List *
YBCGetTables(List *publication_names)
{
	List	*yb_publications;
	List	*tables;

	Assert(IsTransactionState());

	if (publication_names != NIL)
	{
		yb_publications =
			YBGetPublicationsByNames(publication_names, false /* missing_ok */);

		tables = yb_pg_get_publications_tables(yb_publications);
		list_free(yb_publications);
	}
	else
	{
		/*
		 * When the plugin does not provide a publication list, we assume that
		 * it targets all the tables present in the database and it uses
		 * publish_via_partition_root = false (default).
		 */
		tables = GetAllTablesPublicationRelations(false /* pubviaroot */);
	}


	return tables;
}

static void
InitVirtualWal(List *publication_names)
{
	List		*tables;
	Oid			*table_oids;

	YBCUpdateYbReadTimeAndInvalidateRelcache(
		MyReplicationSlot->data.yb_last_pub_refresh_time);

	tables = YBCGetTables(publication_names);
	table_oids = YBCGetTableOids(tables);	

	YBCInitVirtualWalForCDC(MyReplicationSlot->data.yb_stream_id, table_oids,
							list_length(tables));

	pfree(table_oids);
	list_free(tables);
}

YBCPgVirtualWalRecord *
YBCReadRecord(XLogReaderState *state, List *publication_names, char **errormsg)
{
	MemoryContext			caller_context;
	YBCPgVirtualWalRecord	*record = NULL;
	List					*tables;
	Oid						*table_oids;

	elog(DEBUG4, "YBCReadRecord");

	caller_context = MemoryContextSwitchTo(cached_records_context);

	/* reset error state */
	*errormsg = NULL;
	state->errormsg_buf[0] = '\0';

	YBResetDecoder(state);

	/* Fetch a batch of changes from CDC service if needed. */
	if (cached_records == NULL ||
		cached_records_last_sent_row_idx >= cached_records->row_count)
	{
		PreProcessBeforeFetchingNextBatch();

		if (needs_publication_table_list_refresh)
		{
			StartTransactionCommand();

			Assert(yb_read_time < publication_refresh_time);

			YBCUpdateYbReadTimeAndInvalidateRelcache(publication_refresh_time);

			/* Get tables in publication and call UpdatePublicationTableList. */
			tables = YBCGetTables(publication_names);
			table_oids = YBCGetTableOids(tables);
			YBCUpdatePublicationTableList(MyReplicationSlot->data.yb_stream_id,
										  table_oids, list_length(tables));

			pfree(table_oids);
			list_free(tables);
			AbortCurrentTransaction();

			// Refresh the replica identities.
			YBCRefreshReplicaIdentities();

			needs_publication_table_list_refresh = false;
		}

		YBCGetCDCConsistentChanges(MyReplicationSlot->data.yb_stream_id,
								   &cached_records);

		cached_records_last_sent_row_idx = 0;
		YbWalSndTotalTimeInYBDecodeMicros = 0;
		YbWalSndTotalTimeInReorderBufferMicros = 0;
		YbWalSndTotalTimeInSendingMicros = 0;
		last_getconsistentchanges_response_receipt_time = GetCurrentTimestamp();
	}
	Assert(cached_records);

	/*
	 * The GetConsistentChanges response has indicated that it is time to
	 * refresh the publication's table list. Before calling the next
	 * GetConsistentChanges get the table list as of time
	 * publication_refresh_time and notify the updated list to Virtual Wal.
	 */
	if (cached_records && cached_records->needs_publication_table_list_refresh)
	{
		needs_publication_table_list_refresh =
			cached_records->needs_publication_table_list_refresh;
		publication_refresh_time = cached_records->publication_refresh_time;
	}

	/*
	 * We did not get any records from CDC service, return NULL and retry in the
	 * next iteration.
	 */
	if (!cached_records || cached_records->row_count == 0)
	{
		last_getconsistentchanges_response_empty = true;
		MemoryContextSwitchTo(caller_context);
		return NULL;
	}

	last_getconsistentchanges_response_empty = false;
	record = &cached_records->rows[cached_records_last_sent_row_idx++];
	state->ReadRecPtr = record->lsn;
	state->yb_virtual_wal_record = record;

	TrackUnackedTransaction(record);

	MemoryContextSwitchTo(caller_context);
	return record;
}

static void
PreProcessBeforeFetchingNextBatch()
{
	long secs;
	int microsecs;

	/* Log the summary of time spent in processing the previous batch. */
	if (log_min_messages <= DEBUG1 &&
		last_getconsistentchanges_response_receipt_time != 0)
	{
		TimestampDifference(last_getconsistentchanges_response_receipt_time,
							GetCurrentTimestamp(), &secs, &microsecs);

		/*
		 * Note that this processing time does not include the time taken for
		 * the conversion from QLValuePB (proto) to PG datum values. This is
		 * done in ybc_pggate and is logged separately.
		 *
		 * The time being logged here is the total time it took for processing
		 * and sending a whole batch AFTER converting all the values to the PG
		 * format.
		 */
		elog(DEBUG1,
			 "Walsender processing time for the last batch is (%ld s, %d us)",
			 secs, microsecs);
		elog(DEBUG1,
			 "More Information: "
			 "batch_size: %d, "
			 "yb_decode: %" PRIu64 " us, "
			 "reorder buffer: %" PRIu64 " us, "
			 "socket: %" PRIu64 " us.",
			 (cached_records) ? cached_records->row_count : 0,
			 YbWalSndTotalTimeInYBDecodeMicros,
			 YbWalSndTotalTimeInReorderBufferMicros,
			 YbWalSndTotalTimeInSendingMicros);
	}

	/* We no longer need the earlier record batch. */
	if (cached_records)
		MemoryContextReset(cached_records_context);

	if (last_getconsistentchanges_response_empty)
	{
		elog(DEBUG4, "YBCReadRecord: Sleeping for %d ms due to empty response.",
			 yb_walsender_poll_sleep_duration_empty_ms);
		pg_usleep(1000L * yb_walsender_poll_sleep_duration_empty_ms);
	}
	else
	{
		elog(DEBUG4,
			 "YBCReadRecord: Sleeping for %d ms as the last "
			 "response was non-empty.",
			 yb_walsender_poll_sleep_duration_nonempty_ms);
		pg_usleep(1000L * yb_walsender_poll_sleep_duration_nonempty_ms);
	}

	elog(DEBUG5, "YBCReadRecord: Fetching a fresh batch of changes.");
}

static void
TrackUnackedTransaction(YBCPgVirtualWalRecord *record)
{
	MemoryContext			 caller_context;

	caller_context = GetCurrentMemoryContext();
	MemoryContextSwitchTo(unacked_txn_list_context);

	switch (record->action)
	{
		case YB_PG_ROW_MESSAGE_ACTION_BEGIN:
		{
			last_txn_begin_lsn = record->lsn;
			break;
		}

		case YB_PG_ROW_MESSAGE_ACTION_COMMIT:
		{
			YBUnackedTransactionInfo *transaction =
				palloc(sizeof(YBUnackedTransactionInfo));
			transaction->xid = record->xid;
			Assert(last_txn_begin_lsn != InvalidXLogRecPtr);
			transaction->begin_lsn = last_txn_begin_lsn;
			transaction->commit_lsn = record->lsn;

			unacked_transactions = lappend(unacked_transactions, transaction);
			break;
		}

		/* Not of interest here. */
		case YB_PG_ROW_MESSAGE_ACTION_UNKNOWN: switch_fallthrough();
		case YB_PG_ROW_MESSAGE_ACTION_DDL: switch_fallthrough();
		case YB_PG_ROW_MESSAGE_ACTION_INSERT: switch_fallthrough();
		case YB_PG_ROW_MESSAGE_ACTION_UPDATE: switch_fallthrough();
		case YB_PG_ROW_MESSAGE_ACTION_DELETE:
			break;
	}

	MemoryContextSwitchTo(caller_context);
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

	elog(DEBUG1, "Updating confirmed_flush to %lu and restart_lsn_hint to %lu",
		 confirmed_flush, restart_lsn_hint);

	YBCUpdateAndPersistLSN(MyReplicationSlot->data.yb_stream_id,
						   restart_lsn_hint, confirmed_flush, &restart_lsn);

	elog(DEBUG1, "The restart_lsn calculated by the virtual wal is %" PRIu64,
		 restart_lsn);

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

	elog(DEBUG1,
		 "The number of unacked transactions in the virtual wal client is %d",
		 numunacked);

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
	YBUnackedTransactionInfo	*txn;

	foreach(cell, unacked_transactions)
	{
		txn = (YBUnackedTransactionInfo *) lfirst(cell);

		if (txn->commit_lsn <= confirmed_flush)
			unacked_transactions =
				foreach_delete_current(unacked_transactions, cell);
		else
			break;
	}
}

/*
 * Get the table Oids for the list of tables provided as arguments. It is the
 * responsibility of the caller to free the array of Oid values returned from
 * this function. 
 */
static Oid *
YBCGetTableOids(List *tables)
{
	Oid			*table_oids;

	table_oids = palloc(sizeof(Oid) * list_length(tables));
	ListCell *lc;
	size_t table_idx = 0;
	foreach (lc, tables)
		table_oids[table_idx++] = lfirst_oid(lc);
	
	return table_oids;
}

static void 
YBCRefreshReplicaIdentities()
{
	YBCReplicationSlotDescriptor 	*yb_replication_slot;
	int							 	replica_identity_idx = 0;

	YBCGetReplicationSlot(MyReplicationSlot->data.name.data, &yb_replication_slot);

	for (replica_identity_idx = 0;
	 replica_identity_idx <
	 yb_replication_slot->replica_identities_count;
	 replica_identity_idx++)
	{
		YBCPgReplicaIdentityDescriptor *desc =
			&yb_replication_slot->replica_identities[replica_identity_idx];

		YBCPgReplicaIdentityDescriptor *value =
			hash_search(MyReplicationSlot->data.yb_replica_identities,
						&desc->table_oid, HASH_ENTER, NULL);
		value->table_oid = desc->table_oid;
		value->identity_type = desc->identity_type;
	}
}
