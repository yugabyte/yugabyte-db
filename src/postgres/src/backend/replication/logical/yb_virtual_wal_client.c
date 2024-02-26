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

/*
 * Checkpoint per tablet.
 *
 * TODO(#20726): This will not be needed once we have the virtual WAL component
 * ready in the CDC service.
 */
static List *tablet_checkpoints = NIL;

/* Cached records received from the CDC service. */
static YBCPgChangeRecordBatch *cached_records = NULL;
static size_t cached_records_last_sent_row_idx = 0;

/* The LSN of the last record streamed via logical replication. */
static XLogRecPtr yb_last_lsn = InvalidXLogRecPtr;

static List *YBCGetTables(List *publication_names);
/*
 * TODO(#20726): These functions will not be needed once we have the virtual WAL
 * component ready in the CDC service.
 */
static void YBCGetTabletCheckpoints(List *tables);
static void YBCSetInitialTabletCheckpoints();
static XLogRecPtr YBCGenerateLSN();

void
YBCInitVirtualWal(List *yb_publication_names)
{
	MemoryContext	caller_context;
	List			*tables;

	elog(DEBUG1, "YBCInitVirtualWal");

	virtual_wal_context = AllocSetContextCreate(GetCurrentMemoryContext(),
												"YB virtual WAL context",
												ALLOCSET_DEFAULT_SIZES);
	caller_context = GetCurrentMemoryContext();

	/* Start a transaction to be able to read the catalog tables. */
	StartTransactionCommand();

	/* Persist the tablet checkpoints outside of the transaction context. */
	MemoryContextSwitchTo(virtual_wal_context);

	tables = YBCGetTables(yb_publication_names);

	/*
	 * TODO(#20726): Replace these two calls with a call to InitVirtualWal in
	 * the CDC service once it is ready.
	 */
	YBCGetTabletCheckpoints(tables);
	YBCSetInitialTabletCheckpoints();

	list_free(tables);
	AbortCurrentTransaction();
	MemoryContextSwitchTo(caller_context);

	yb_last_lsn = 0;
}

void
YBCDestroyVirtualWal()
{
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
YBCGetTabletCheckpoints(List *tables)
{
	ListCell *lc;
	foreach (lc, tables)
	{
		YBCPgTabletCheckpoint	*checkpoints;
		size_t 					numtablets;
		Oid						table_oid = lfirst_oid(lc);

		YBCGetTabletListToPollForStreamAndTable(
			MyReplicationSlot->data.yb_stream_id, table_oid, &checkpoints,
			&numtablets);

		for (size_t i = 0; i < numtablets; i++)
		{
			checkpoints[i].table_oid = table_oid;
			tablet_checkpoints = lappend(tablet_checkpoints, &checkpoints[i]);
		}
	}
}

static void
YBCSetInitialTabletCheckpoints()
{
	ListCell *lc;
	foreach (lc, tablet_checkpoints)
	{
		YBCPgTabletCheckpoint *tc = (YBCPgTabletCheckpoint *) lfirst(lc);
		YBCPgCDCSDKCheckpoint *new_checkpoint;

		new_checkpoint = palloc(sizeof(YBCPgCDCSDKCheckpoint));
		new_checkpoint->index = 0;
		new_checkpoint->term = 0;

		YBCSetCDCTabletCheckpoint(MyReplicationSlot->data.yb_stream_id,
								  tc->location->tablet_id, new_checkpoint, 0,
								  true);

		tc->checkpoint->index = 0;
		tc->checkpoint->term = 0;
	}
}

YBCPgVirtualWalRecord *
YBCReadRecord(XLogReaderState *state, XLogRecPtr RecPtr, char **errormsg)
{
	MemoryContext			caller_context;
	XLogRecPtr				record_lsn = InvalidXLogRecPtr;
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

		/*
		 * TODO(#20726): The below code assumes that the number of tablets in 1.
		 * Once the virtual WAL component is ready, this Walsender code will not
		 * be aware of tablets. It will just get a stream of records from the
		 * virtual wal and would not need to know/care about the tablet it came
		 * from.
		 */
		Assert(list_length(tablet_checkpoints) == 1);
		ListCell *lc;
		foreach (lc, tablet_checkpoints)
		{
			YBCPgTabletCheckpoint *tc = (YBCPgTabletCheckpoint *) lfirst(lc);

			/* We no longer need the earlier record batch. */
			if (cached_records)
				pfree(cached_records);

			YBCGetCDCChanges(MyReplicationSlot->data.yb_stream_id,
							 tc->location->tablet_id, tc->checkpoint,
							 &cached_records);

			if (tc->checkpoint)
				pfree(tc->checkpoint);

			tc->checkpoint = cached_records->checkpoint;
			cached_records->table_oid = tc->table_oid;
		}

		cached_records_last_sent_row_idx = 0;
	}
	Assert(cached_records);

	/*
	 * We did not get any records from CDC service, return NULL and retry in the
	 * next iteration.
	 */
	if (cached_records->row_count == 0)
	{
		MemoryContextSwitchTo(caller_context);
		return NULL;
	}

	/* Get an LSN from the generator. */
	record_lsn = YBCGenerateLSN();

	record = palloc(sizeof(YBCPgVirtualWalRecord));
	record->data = &cached_records->rows[cached_records_last_sent_row_idx++];
	record->table_oid = cached_records->table_oid;
	record->lsn = record_lsn;
	/*
	 * TODO(#20726): Remove this hardcoded value once the Virtual WAL component
	 * is ready in CDC service. It will return the xid in the response.
	 */
	record->xid = 1;

	state->ReadRecPtr = record_lsn;

	if (state->yb_virtual_wal_record)
		pfree(state->yb_virtual_wal_record);
	state->yb_virtual_wal_record = record;

	MemoryContextSwitchTo(caller_context);
	return record;
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

static XLogRecPtr
YBCGenerateLSN()
{
	return ++yb_last_lsn;
}
