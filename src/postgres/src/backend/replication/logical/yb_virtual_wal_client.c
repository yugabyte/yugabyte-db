/*--------------------------------------------------------------------------------------------------
 *
 * yb_virtual_wal_client.c
 *        Commands for readings records from the YB Virtual WAL exposed by the CDC service.
 *
 * Copyright (c) YugabyteDB, Inc.
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
#include "catalog/yb_type.h"
#include "commands/yb_cmds.h"
#include "pg_yb_utils.h"
#include "replication/slot.h"
#include "replication/walsender_private.h"
#include "replication/yb_virtual_wal_client.h"
#include "utils/memutils.h"
#include "utils/varlena.h"
#include "utils/wait_event.h"
#include "yb/yql/pggate/ybc_gflags.h"

static MemoryContext virtual_wal_context = NULL;
static MemoryContext cached_records_context = NULL;
static MemoryContext unacked_txn_list_context = NULL;

/* Cached records received from the CDC service. */
static YbcPgChangeRecordBatch *cached_records = NULL;
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

typedef struct YbUnackedTransactionInfo
{
	TransactionId xid;
	XLogRecPtr	begin_lsn;
	XLogRecPtr	commit_lsn;
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

/*
 * These represent the hash range constraints of a repliction slot and only
 * populated when hash range constraints are explicitly passed. These fields are
 * only required till Virtual WAL is initialised.
 */
static YbcReplicationSlotHashRange *slot_hash_range = NULL;

static List *YBCGetTables(List *publication_names, bool *yb_is_pub_all_tables);
static List *YBCGetTablesWithRetryIfNeeded(List *publication_names,
										   bool *yb_is_pub_all_tables,
										   bool *skip_setting_yb_read_time);
static void YBCRemoveTablesWithoutPrimaryKeyIfNotAllowedBySlot(List **tables);

static void InitVirtualWal(List *publication_names,
						   const YbcReplicationSlotHashRange *slot_hash_range);

static void PreProcessBeforeFetchingNextBatch();

static void TrackUnackedTransaction(YbVirtualWalRecord *record);
static XLogRecPtr CalculateRestartLSN(XLogRecPtr confirmed_flush);
static void CleanupAckedTransactions(XLogRecPtr confirmed_flush);

static Oid *YBCGetTableOids(List *tables);
static void YBCRefreshReplicaIdentities(Oid *table_oids, int num_tables);
static void ValidateReplicaIdentities(Oid *table_oids, int num_tables);

void
YBCInitVirtualWal(List *yb_publication_names)
{
	MemoryContext caller_context;

	elog(DEBUG1, "YBCInitVirtualWal");

	virtual_wal_context = AllocSetContextCreate(CurrentMemoryContext,
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
	caller_context = CurrentMemoryContext;

	/* Start a transaction to be able to read the catalog tables. */
	StartTransactionCommand();

	/*
	 * Allocate any data within the virtual wal context i.e. outside of the
	 * transaction context.
	 */
	MemoryContextSwitchTo(virtual_wal_context);

	InitVirtualWal(yb_publication_names, slot_hash_range);

	AbortCurrentTransaction();
	MemoryContextSwitchTo(caller_context);

	unacked_transactions = NIL;
	last_getconsistentchanges_response_empty = false;
	last_getconsistentchanges_response_receipt_time = 0;
	last_txn_begin_lsn = InvalidXLogRecPtr;

	needs_publication_table_list_refresh = false;
	if (yb_enable_consistent_replication_from_hash_range &&
		slot_hash_range != NULL)
		pfree(slot_hash_range);
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
YBCGetTables(List *publication_names, bool *yb_is_pub_all_tables)
{
	List	   *yb_publications;
	List	   *tables;

	Assert(IsTransactionState());

	if (publication_names != NIL)
	{
		yb_publications =
			YBGetPublicationsByNames(publication_names, false /* missing_ok */ );

		tables = yb_pg_get_publications_tables(yb_publications, yb_is_pub_all_tables);
		list_free(yb_publications);
	}
	else
	{
		/*
		 * When the plugin does not provide a publication list, we assume that
		 * it targets all the tables present in the database and it uses
		 * publish_via_partition_root = false (default).
		 */
		tables = GetAllTablesPublicationRelations(false /* pubviaroot */ );
		*yb_is_pub_all_tables = true;
	}


	return tables;
}

static List *
YBCGetTablesWithRetryIfNeeded(List *publication_names, bool *yb_is_pub_all_tables,
							  bool *skip_setting_yb_read_time)
{
	List	   *tables;
	MemoryContext caller_context = CurrentMemoryContext;

	PG_TRY();
	{
		tables = YBCGetTables(publication_names, yb_is_pub_all_tables);
	}
	PG_CATCH();
	{
		MemoryContext error_context = MemoryContextSwitchTo(caller_context);
		ErrorData  *edata = CopyErrorData();

		if (!yb_ignore_read_time_in_walsender || edata->sqlerrcode != ERRCODE_UNDEFINED_OBJECT)
		{
			MemoryContextSwitchTo(error_context);
			PG_RE_THROW();
		}

		/* Clean up the error state before retrying. */
		FlushErrorState();
		FreeErrorData(edata);

		elog(DEBUG1, "Encountered an error while trying to fetch tables by "
			 "setting yb_read_time. Will retry by resetting it.");
		YBCResetYbReadTimeAndInvalidateRelcache();
		if (skip_setting_yb_read_time)
			*skip_setting_yb_read_time = true;

		tables = YBCGetTables(publication_names, yb_is_pub_all_tables);
	}
	PG_END_TRY();

	/*
	 * Remove tables without primary key if replication slot does not allow them.
	 */
	YBCRemoveTablesWithoutPrimaryKeyIfNotAllowedBySlot(&tables);

	return tables;
}

static void
YBCRemoveTablesWithoutPrimaryKeyIfNotAllowedBySlot(List **tables)
{
	if (MyReplicationSlot->data.yb_allow_tables_without_primary_key)
		return;

	ListCell   *lc;

	foreach (lc, *tables)
	{
		Oid table_oid = lfirst_oid(lc);
		Relation rel = RelationIdGetRelation(table_oid);
		bool has_pk = false;

		Assert(RelationIsValid(rel));
		has_pk = YBGetTablePrimaryKeyBms(rel) != NULL;
		RelationClose(rel);
		if (!has_pk)
			*tables = foreach_delete_current(*tables, lc);
	}
}

static void
InitVirtualWal(List *publication_names,
			   const YbcReplicationSlotHashRange *slot_hash_range)
{
	List	   *tables;
	Oid		   *table_oids;
	Oid		   *yb_publication_oids;
	bool		yb_is_pub_all_tables = false;
	bool		skip_setting_yb_read_time = false;

	/*
	 * YB_TODO(#27686): Using the value of ysql_yb_enable_implicit_dynamic_tables_logical_replication
	 * for decision making here will yield improper behaviour when streams with pub refresh
	 * mechanism are upgraded to a version where the flag is true by default.
	 */
	if (*YBCGetGFlags()->ysql_yb_enable_implicit_dynamic_tables_logical_replication)
	{
		elog(DEBUG2,
			 "Setting yb_read_time to initial_record_commit_time for %" PRIu64,
			 MyReplicationSlot->data.yb_initial_record_commit_time_ht);
		YBCUpdateYbReadTimeAndInvalidateRelcache(MyReplicationSlot->data.yb_initial_record_commit_time_ht);
	}
	else
	{
		elog(DEBUG2,
			 "Setting yb_read_time to last_pub_refresh_time for "
			 "InitVirtualWal: %" PRIu64,
			 MyReplicationSlot->data.yb_last_pub_refresh_time);
		YBCUpdateYbReadTimeAndInvalidateRelcache(MyReplicationSlot->data.yb_last_pub_refresh_time);
	}

	/*
	 * We may encounter a failure prompting a retry in 2 cases:
	 *   1. When publication was created after the slot creation.
	 *   2. After PG 15 upgrade if last_pub_refresh_time lies in the period
	 * 		where YB was running PG 11.
	 * In the first case we do not expect yb_ignore_read_time_in_walsender
	 * to be true and we should error out without retrying.
	 * In the second case, the only way to proceed is to query
	 * the tables in the publication as of now. So, yb_ignore_read_time_in_walsender
	 * should be set and we will fetch the tables in publication as of now.
	 */
	tables = YBCGetTablesWithRetryIfNeeded(publication_names, &yb_is_pub_all_tables,
										   &skip_setting_yb_read_time);

	yb_publication_oids = YBGetPublicationOidsByNames(publication_names);
	if (yb_enable_consistent_replication_from_hash_range &&
		slot_hash_range != NULL)
	{
		if (list_length(tables) != 1)
			ereport(ERROR,
					(errmsg("publication should only contain 1 table "
							"for using hash range constraints on slot."),
					 errhint("Consider using an existing publication "
							 "created before the slot that contains 1 "
							 "table. Else, alter/create new publication "
							 "and use a new slot.")));
	}
	table_oids = YBCGetTableOids(tables);

	/*
	 * Throw an error if the plugin being used is pgoutput and there exist a
	 * table in publication with YB specific replica identity (CHANGE).
	 */
	ValidateReplicaIdentities(table_oids, list_length(tables));

	YBCInitVirtualWalForCDC(MyReplicationSlot->data.yb_stream_id, table_oids,
							list_length(tables), slot_hash_range, MyProcPid,
							yb_publication_oids, list_length(publication_names),
							yb_is_pub_all_tables);

	if (!*YBCGetGFlags()->ysql_yb_enable_implicit_dynamic_tables_logical_replication &&
		!skip_setting_yb_read_time)
	{
		elog(DEBUG2,
			 "Setting yb_read_time to initial_record_commit_time for %" PRIu64,
			 MyReplicationSlot->data.yb_initial_record_commit_time_ht);
		YBCUpdateYbReadTimeAndInvalidateRelcache(MyReplicationSlot->data.yb_initial_record_commit_time_ht);
	}

	pfree(table_oids);
	list_free(tables);
	pfree(yb_publication_oids);
}

static const YbcPgTypeEntity *
GetDynamicTypeEntity(int attr_num, Oid relid)
{
	bool		is_in_txn = IsTransactionOrTransactionBlock();

	if (!is_in_txn)
		StartTransactionCommand();

	Relation	rel = RelationIdGetRelation(relid);

	if (!RelationIsValid(rel))
		elog(ERROR, "Could not open relation with OID %u", relid);
	Oid			type_oid = GetTypeId(attr_num, RelationGetDescr(rel));

	RelationClose(rel);
	const YbcPgTypeEntity *type_entity = YbDataTypeFromOidMod(attr_num, type_oid);

	if (!is_in_txn)
		AbortCurrentTransaction();

	return type_entity;
}

YbVirtualWalRecord *
YBXLogReadRecord(XLogReaderState *state, List *publication_names,
				 char **errormsg)
{
	/* reset error state */
	*errormsg = NULL;
	state->errormsg_buf[0] = '\0';

	YBResetDecoder(state);

	YbVirtualWalRecord *record = YBCReadRecord(publication_names);

	if (record)
	{
		state->ReadRecPtr = record->lsn;
		state->yb_virtual_wal_record = record;
		TrackUnackedTransaction(record);
	}

	return record;
}

YbVirtualWalRecord *
YBCReadRecord(List *publication_names)
{
	MemoryContext caller_context;
	YbVirtualWalRecord *record = NULL;
	List	   *tables;
	Oid		   *table_oids;
	bool		yb_is_pub_all_tables = false;

	elog(DEBUG4, "YBCReadRecord");

	caller_context = MemoryContextSwitchTo(cached_records_context);

	/* Fetch a batch of changes from CDC service if needed. */
	if (cached_records == NULL ||
		cached_records_last_sent_row_idx >= cached_records->row_count)
	{
		PreProcessBeforeFetchingNextBatch();

		if (needs_publication_table_list_refresh)
		{
			StartTransactionCommand();

			Assert(yb_read_time <= publication_refresh_time);

			elog(DEBUG2,
				 "Setting yb_read_time to new pub_refresh_time: %" PRIu64,
				 publication_refresh_time);
			YBCUpdateYbReadTimeAndInvalidateRelcache(publication_refresh_time);

			/*
			 * We will need a retry post PG 15 upgrade, when the publication_refresh_time
			 * lies in the period where YB was running PG 11.
			 * If yb_ignore_read_time_in_walsender is set then try to fetch the tables
			 * in publication as of now upon retry.
			 */
			tables = YBCGetTablesWithRetryIfNeeded(publication_names,
												   &yb_is_pub_all_tables, NULL);

			table_oids = YBCGetTableOids(tables);
			YBCUpdatePublicationTableList(MyReplicationSlot->data.yb_stream_id,
										  table_oids, list_length(tables));

			/* Refresh the replica identities. */
			YBCRefreshReplicaIdentities(table_oids, list_length(tables));

			pfree(table_oids);
			list_free(tables);
			AbortCurrentTransaction();

			needs_publication_table_list_refresh = false;
		}

		YBCGetCDCConsistentChanges(MyReplicationSlot->data.yb_stream_id,
								   &cached_records, &GetDynamicTypeEntity);

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

	MemoryContextSwitchTo(caller_context);
	return record;
}

static void
PreProcessBeforeFetchingNextBatch()
{
	long		secs;
	int			microsecs;

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

	/* Don't track idle sleep time */
	pgstat_report_wait_start(WAIT_EVENT_YB_IDLE_SLEEP);

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

	pgstat_report_wait_end();

	elog(DEBUG5, "YBCReadRecord: Fetching a fresh batch of changes.");
}

static void
TrackUnackedTransaction(YbVirtualWalRecord *record)
{
	MemoryContext caller_context;

	caller_context = CurrentMemoryContext;
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
		case YB_PG_ROW_MESSAGE_ACTION_UNKNOWN:
			yb_switch_fallthrough();
		case YB_PG_ROW_MESSAGE_ACTION_DDL:
			yb_switch_fallthrough();
		case YB_PG_ROW_MESSAGE_ACTION_INSERT:
			yb_switch_fallthrough();
		case YB_PG_ROW_MESSAGE_ACTION_UPDATE:
			yb_switch_fallthrough();
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
	XLogRecPtr	restart_lsn_hint = CalculateRestartLSN(confirmed_flush);
	YbcPgXLogRecPtr restart_lsn = InvalidXLogRecPtr;

	/* There was nothing to ack, so we can return early. */
	if (restart_lsn_hint == InvalidXLogRecPtr)
	{
		elog(DEBUG4,
			 "No unacked transaction were found, skipping the "
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
	XLogRecPtr	restart_lsn = InvalidXLogRecPtr;
	ListCell   *lc;
	YBUnackedTransactionInfo *txn;
	int			numunacked = list_length(unacked_transactions);

	if (numunacked == 0)
		return InvalidXLogRecPtr;

	elog(DEBUG1,
		 "The number of unacked transactions in the virtual wal client is %d",
		 numunacked);

	foreach(lc, unacked_transactions)
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
	ListCell   *cell;
	YBUnackedTransactionInfo *txn;

	foreach(cell, unacked_transactions)
	{
		txn = (YBUnackedTransactionInfo *) lfirst(cell);

		if (txn->commit_lsn <= confirmed_flush)
		{
			unacked_transactions =
				foreach_delete_current(unacked_transactions, cell);
			pfree(txn);
		}
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
	Oid		   *table_oids;

	table_oids = palloc(sizeof(Oid) * list_length(tables));
	ListCell   *lc;
	size_t		table_idx = 0;

	foreach(lc, tables)
		table_oids[table_idx++] = lfirst_oid(lc);

	return table_oids;
}

static void
YBCRefreshReplicaIdentities(Oid *table_oids, int num_tables)
{
	YbcReplicationSlotDescriptor *yb_replication_slot;
	int			replica_identity_idx = 0;

	YBCGetReplicationSlot(MyReplicationSlot->data.name.data,
						  &yb_replication_slot, /* if_exists */ false);

	/* Populate the replica identities for new tables in MyReplicationSlot. */
	for (replica_identity_idx = 0;
		 replica_identity_idx <
		 yb_replication_slot->replica_identities_count;
		 replica_identity_idx++)
	{
		YbcPgReplicaIdentityDescriptor *desc;
		YbcPgReplicaIdentityDescriptor *value;

		desc =
			&yb_replication_slot->replica_identities[replica_identity_idx];

		value =
			hash_search(MyReplicationSlot->data.yb_replica_identities,
						&desc->table_oid, HASH_ENTER, NULL);
		value->table_oid = desc->table_oid;
		value->identity_type = desc->identity_type;
	}

	/*
	 * Throw an error if the plugin being used is pgoutput and a table has been
	 * added to the publication with YB specific replica identity (CHANGE).
	 */
	ValidateReplicaIdentities(table_oids, num_tables);
}

void
ValidateAndExtractHashRange(const char *hash_range_str, uint32_t *hash_range)
{
	/* Check for non-numeric characters */
	if (strspn(hash_range_str, "0123456789") != strlen(hash_range_str))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid value for hash_range")));

	unsigned long parsed_range;
	char	   *endptr;

	errno = 0;
	parsed_range = strtoul(hash_range_str, &endptr, 10);
	if (errno != 0 || *endptr != '\0')
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid value for hash_range")));

	if (parsed_range > (PG_UINT16_MAX + 1))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("hash_range out of bound")));

	*hash_range = (uint32_t) parsed_range;
}

/*
 * For slots with hash range constraints, extract the start & end hash range to
 * poll on a subset of tablets for a table. The 'hash_range' option (if present)
 * will be removed from the options list as the same list is used by output plugins
 * for option validations.
 */
void
YBCGetTableHashRange(List **options)
{
	bool		hash_range_option_given = false;
	ListCell   *lc;
	List	   *option_values = NIL;
	DefElem    *hash_range_option = NULL;

	foreach(lc, *options)
	{
		DefElem    *defel = (DefElem *) lfirst(lc);

		Assert(defel->arg == NULL || IsA(defel->arg, String));

		if (strcmp(defel->defname, "hash_range") == 0)
		{
			if (!yb_enable_consistent_replication_from_hash_range)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("hash_range option is unavailable"),
						 errdetail("hash_range option can be used after "
								   "yb_enable_consistent_replication_"
								   "from_hash_range is set to true on "
								   "all nodes.")));

			if (hash_range_option_given)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("conflicting or redundant options")));
			hash_range_option_given = true;

			elog(DEBUG2, "Value for hash_range option: %s", strVal(defel->arg));
			if (!SplitIdentifierString(strVal(defel->arg), ',', &option_values))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_NAME),
						 errmsg("invalid syntax for hash ranges")));

			if (list_length(option_values) != 2)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("hash_range option must only contain start range & end range")));

			uint32_t	extracted_start_range;

			ValidateAndExtractHashRange((char *) linitial(option_values),
										&extracted_start_range);
			uint32_t	extracted_end_range;

			ValidateAndExtractHashRange((char *) llast(option_values),
										&extracted_end_range);

			if (extracted_start_range == extracted_end_range)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("start hash range & end hash range must be different")));

			slot_hash_range = (YbcReplicationSlotHashRange *)
				palloc(sizeof(YbcReplicationSlotHashRange));
			slot_hash_range->start_range = extracted_start_range;
			slot_hash_range->end_range = extracted_end_range;

			elog(INFO, "start_range: %d, end_range: %d",
				 slot_hash_range->start_range, slot_hash_range->end_range);
			hash_range_option = defel;
		}
	}

	if (hash_range_option != NULL)
		*options = list_delete_ptr(*options, hash_range_option);

	if (option_values != NIL)
		list_free(option_values);
}

/*
 * This function validates that none of the tables passed to it have replica
 * identity CHANGE when we are using pgoutput plugin.
 */
static void
ValidateReplicaIdentities(Oid *table_oids, int num_tables)
{
	if (strcmp(MyReplicationSlot->data.plugin.data, PG_OUTPUT_PLUGIN) != 0)
		return;

	for (int i = 0; i < num_tables; i++)
	{
		YbcPgReplicaIdentityDescriptor *value = hash_search(MyReplicationSlot->data.yb_replica_identities,
															&table_oids[i],
															HASH_FIND,
															NULL);

		Assert(value);
		if (value->identity_type == YBC_YB_REPLICA_IDENTITY_CHANGE)
			ereport(ERROR,
					(errmsg("replica identity CHANGE (for table: %u) is not supported for output "
							"plugin pgoutput", table_oids[i]),
					 errhint("Consider using output plugin yboutput instead. ")));
	}
}
