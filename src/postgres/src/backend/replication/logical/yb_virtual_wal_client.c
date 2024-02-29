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

static List *YBCGetTables(List *publication_names);
static void InitVirtualWal(List *publication_names);

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
YBCReadRecord(XLogReaderState *state, XLogRecPtr RecPtr, char **errormsg)
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
