/*--------------------------------------------------------------------------------------------------
 *
 * yb_virtual_wal_table_reader.c
 *        Reads the change stream of a replication slot from public.pg_yb_cdc_changes instead of the
 *        CDC service.
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
 *        src/postgres/src/backend/replication/logical/yb_virtual_wal_table_reader.c
 *
 * The table is written by the CDC table poller of the tservers (see CdcTableOutputClient) and is
 * assumed to hold the changes of every committed transaction. This file only reads it. Of its
 * columns the reader uses:
 *
 *   commit_time         bigint   commit hybrid time of the transaction
 *   record_time         bigint   hybrid time of the record's write
 *   write_id            bigint   position of the write within its transaction
 *   transaction_id      uuid     only to order rows, nil for a single shard transaction
 *   table_id            uuid     the DocDB table id of the changed table
 *   row_ybctid          bytea    only to order rows
 *   op                  char(1)  'I', 'U' or 'D'
 *   before, after       bytea    a serialized CdcTableImagePB, or NULL
 *
 * An image has an entry per column, by attribute number, with the type OID of the column (0 for a
 * type that has none, which is looked up in the relation) and the column's QLValuePB, absent for a
 * NULL. These are the values that the CDC service sends a walsender, so they are converted by
 * the decoder the way those are.
 *
 * Rows are read in (commit_time, record_time, write_id, transaction_id, table_id, row_ybctid)
 * order. Like the CDC service does for a HYBRID_TIME slot, all rows with one commit_time are
 * streamed as one transaction whatever their transaction_id, and the LSN of every record of it is
 * that commit_time.
 *
 * A row must not become visible in the table with a key lower than one that is already visible,
 * otherwise a transaction can be streamed before a row that belongs to it, or skipped. In
 * particular all rows of one commit_time must become visible together: a row that shows up after
 * its commit_time has been read is never streamed. The pollers do not guarantee that yet.
 *
 * TODO: The table has no schema changes in it, so records are decoded against the current catalog
 * rather than the one at their commit time. A row written before an ALTER TABLE and streamed after
 * it is decoded with the new schema. See the TODO in InitVirtualWal.
 *
 * The table is hash partitioned on bucket_id, and the pollers write it with raw DocDB operations
 * that do not maintain a secondary index, so the primary key is the only fast access path. A query
 * reads a window of commit times with every bucket listed, so that it seeks to the range in each
 * bucket, and sorts only that window. Nothing here deletes rows. A row that does not fit this
 * layout stops the walsender with an error on every retry.
 *
 *--------------------------------------------------------------------------------------------------
 */

#include "postgres.h"

#include <inttypes.h>

#include "access/xact.h"
#include "datatype/timestamp.h"
#include "executor/spi.h"
#include "lib/stringinfo.h"
#include "miscadmin.h"
#include "pg_yb_utils.h"
#include "replication/yb_virtual_wal_table_reader.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/snapmgr.h"
#include "utils/uuid.h"
#include "yb/yql/pggate/ybc_pggate.h"

/* Most rows read by one query. A commit group is never split across two queries. */
#define YB_TABLE_READER_BATCH_ROWS 100000

/*
 * The pollers hash the ybctid of a row into this many buckets, the hash column
 * of the table's key. Listing them all lets a query seek to a commit_time range
 * in every bucket through the primary key, instead of scanning the table.
 */
#define YB_TABLE_READER_NUM_BUCKETS 256

/*
 * A query reads the commit times in a window of this many microseconds of
 * physical time. The window grows while a query returns few rows and shrinks
 * while it returns many, to keep the sort of one query cheap.
 */
#define YB_TABLE_READER_MIN_WINDOW_MICROS 10000
#define YB_TABLE_READER_MAX_WINDOW_MICROS 1000000
#define YB_TABLE_READER_MAX_TARGET_ROWS 20000
#define YB_TABLE_READER_MIN_TARGET_ROWS 1000

#define YB_HYBRID_TIME_LOGICAL_BITS 12

/*
 * A DocDB table id is a UUID whose last four bytes are the PostgreSQL table
 * OID, in big endian.
 */
#define YB_TABLE_ID_OID_OFFSET (UUID_LEN - 4)

#define YB_TABLE_READER_SELECT \
	"SELECT commit_time, record_time, table_id, op::text, before, after " \
	"FROM public.pg_yb_cdc_changes "

#define YB_TABLE_READER_ORDER_BY \
	"ORDER BY commit_time, record_time, write_id, transaction_id, table_id, " \
	"row_ybctid"

/* Output columns of YB_TABLE_READER_SELECT. */
#define YB_COL_COMMIT_TIME 1
#define YB_COL_RECORD_TIME 2
#define YB_COL_TABLE_ID 3
#define YB_COL_OP 4
#define YB_COL_BEFORE 5
#define YB_COL_AFTER 6

static Oid *yb_reader_table_oids = NULL;
static int	yb_reader_num_tables = 0;

/* The next query starts at the first commit group with at least this commit_time. */
static uint64 yb_reader_next_commit_time = 0;

static TransactionId yb_reader_next_xid = FirstNormalTransactionId;

/* The current window of commit times, in microseconds of physical time. */
static uint64 yb_reader_window_micros = 100000;

/* Queries over a window of commit times, with $1 and $2 as its bounds. */
static char *yb_reader_window_query = NULL;
static char *yb_reader_group_query = NULL;
static char *yb_reader_probe_query = NULL;

static int
YbOidCmp(const void *a, const void *b)
{
	Oid			oa = *(const Oid *) a;
	Oid			ob = *(const Oid *) b;

	return (oa > ob) - (oa < ob);
}

void
YbTableReaderInit(const Oid *table_oids, int num_tables,
				  XLogRecPtr resume_commit_time)
{
	StringInfoData buckets;

	yb_reader_num_tables = num_tables;
	yb_reader_table_oids = palloc(sizeof(Oid) * Max(num_tables, 1));
	memcpy(yb_reader_table_oids, table_oids, sizeof(Oid) * num_tables);
	qsort(yb_reader_table_oids, num_tables, sizeof(Oid), YbOidCmp);

	yb_reader_next_commit_time = resume_commit_time;
	yb_reader_next_xid = FirstNormalTransactionId;
	yb_reader_window_micros = 100000;

	initStringInfo(&buckets);
	for (int i = 0; i < YB_TABLE_READER_NUM_BUCKETS; i++)
		appendStringInfo(&buckets, "%s%d", i > 0 ? "," : "", i);
	yb_reader_group_query =
		psprintf(YB_TABLE_READER_SELECT "WHERE bucket_id IN (%s) "
				 "AND commit_time >= $1 AND commit_time < $2 "
				 YB_TABLE_READER_ORDER_BY, buckets.data);
	yb_reader_window_query =
		psprintf("%s LIMIT %d", yb_reader_group_query,
				 YB_TABLE_READER_BATCH_ROWS);
	yb_reader_probe_query =
		psprintf("SELECT 1 FROM public.pg_yb_cdc_changes "
				 "WHERE bucket_id IN (%s) AND commit_time >= $1 LIMIT 1",
				 buckets.data);
	pfree(buckets.data);
}

void
YbTableReaderDestroy(void)
{
	/* Allocated in the virtual WAL context, which the caller deletes. */
	yb_reader_table_oids = NULL;
	yb_reader_num_tables = 0;
	yb_reader_window_query = yb_reader_group_query = yb_reader_probe_query = NULL;
}

/* Returns the PostgreSQL OID in a DocDB table id. */
static Oid
YbTableOidFromTableId(const pg_uuid_t *table_id)
{
	const unsigned char *b = table_id->data + YB_TABLE_ID_OID_OFFSET;

	return ((Oid) b[0] << 24) | ((Oid) b[1] << 16) | ((Oid) b[2] << 8) | b[3];
}

static bool
YbIsReaderTable(Oid table_oid)
{
	return bsearch(&table_oid, yb_reader_table_oids, yb_reader_num_tables,
				   sizeof(Oid), YbOidCmp) != NULL;
}

static uint64
YbHybridTimeToPgEpochMicros(uint64 hybrid_time)
{
	int64		unix_micros = hybrid_time >> YB_HYBRID_TIME_LOGICAL_BITS;

	return unix_micros -
		(int64) (POSTGRES_EPOCH_JDATE - UNIX_EPOCH_JDATE) * SECS_PER_DAY *
		USECS_PER_SEC;
}

static void
YbInitRow(YbcPgRowMessage *row, YbcPgRowMessageAction action,
		  uint64 commit_time_ht, uint64 record_time_ht, uint32 xid)
{
	row->action = action;
	row->lsn = commit_time_ht;
	row->xid = xid;
	row->commit_time_ht = commit_time_ht;
	row->commit_time = YbHybridTimeToPgEpochMicros(commit_time_ht);
	row->record_time_ht = record_time_ht;
	row->table_oid = InvalidOid;
}

/*
 * Adds the columns of a before or after image to cols. A column of the other
 * image that is already in cols is completed, otherwise the column is added
 * and the other image is marked omitted for it.
 */
static void
YbAddImageColumns(const YbcCdcImageColumn *image, int image_columns,
				  bool is_after, uint64 commit_time, YbcPgDatumMessage *cols,
				  int *col_count)
{
	for (int k = 0; k < image_columns; k++)
	{
		const YbcCdcImageColumn *entry = &image[k];
		YbcPgDatumMessage *col = NULL;

		if (entry->attr_num <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_DATA_CORRUPTED),
					 errmsg("invalid attribute number %d in pg_yb_cdc_changes",
							entry->attr_num),
					 errdetail("commit_time: %" PRIu64, commit_time)));

		for (int i = 0; i < *col_count; i++)
		{
			if (cols[i].column_attr_num == entry->attr_num)
			{
				col = &cols[i];
				break;
			}
		}
		if (col == NULL)
		{
			col = &cols[(*col_count)++];
			col->column_name = NULL;
			col->column_attr_num = entry->attr_num;
			col->type_entity = entry->type_entity;

			/*
			 * As in the CDC service path, an omitted side is also null: the
			 * decoder copies the datum of an omitted column unless the replica
			 * identity is CHANGE.
			 */
			col->before_op_is_omitted = true;
			col->before_op_is_null = true;
			col->after_op_is_omitted = true;
			col->after_op_is_null = true;
		}

		if (is_after)
		{
			col->after_op_ql_value = entry->ql_value;
			col->after_op_is_null = entry->is_null;
			col->after_op_is_omitted = false;
		}
		else
		{
			col->before_op_ql_value = entry->ql_value;
			col->before_op_is_null = entry->is_null;
			col->before_op_is_omitted = false;
		}
	}
}

/* Fills row from the current SPI tuple. */
static void
YbFillDmlRow(YbcPgRowMessage *row, HeapTuple tuple, TupleDesc tupdesc,
			 uint64 commit_time_ht, Oid table_oid, uint32 xid, void *values)
{
	bool		isnull;
	Datum		val;
	char	   *op;
	const YbcCdcImageColumn *before_cols = NULL;
	const YbcCdcImageColumn *after_cols = NULL;
	int			before_count = 0;
	int			after_count = 0;

	val = SPI_getbinval(tuple, tupdesc, YB_COL_RECORD_TIME, &isnull);
	YbInitRow(row, YB_PG_ROW_MESSAGE_ACTION_UNKNOWN, commit_time_ht,
			  isnull ? 0 : DatumGetInt64(val), xid);
	row->table_oid = table_oid;

	val = SPI_getbinval(tuple, tupdesc, YB_COL_OP, &isnull);
	if (isnull)
		ereport(ERROR,
				(errcode(ERRCODE_DATA_CORRUPTED),
				 errmsg("op is null in pg_yb_cdc_changes"),
				 errdetail("commit_time: %" PRIu64, commit_time_ht)));
	op = TextDatumGetCString(val);
	switch (op[0])
	{
		case 'I':
			row->action = YB_PG_ROW_MESSAGE_ACTION_INSERT;
			break;
		case 'U':
			row->action = YB_PG_ROW_MESSAGE_ACTION_UPDATE;
			break;
		case 'D':
			row->action = YB_PG_ROW_MESSAGE_ACTION_DELETE;
			break;
		default:
			ereport(ERROR,
					(errcode(ERRCODE_DATA_CORRUPTED),
					 errmsg("invalid op \"%s\" in pg_yb_cdc_changes", op),
					 errdetail("commit_time: %" PRIu64, commit_time_ht)));
	}
	pfree(op);

	val = SPI_getbinval(tuple, tupdesc, YB_COL_BEFORE, &isnull);
	if (!isnull)
	{
		bytea	   *image = DatumGetByteaPP(val);

		HandleYBStatus(YBCPgParseCdcTableImage(values, VARDATA_ANY(image),
											   VARSIZE_ANY_EXHDR(image),
											   &before_cols, &before_count));
	}
	val = SPI_getbinval(tuple, tupdesc, YB_COL_AFTER, &isnull);
	if (!isnull)
	{
		bytea	   *image = DatumGetByteaPP(val);

		HandleYBStatus(YBCPgParseCdcTableImage(values, VARDATA_ANY(image),
											   VARSIZE_ANY_EXHDR(image),
											   &after_cols, &after_count));
	}

	if (before_count + after_count > 0)
	{
		row->cols = palloc0(sizeof(YbcPgDatumMessage) *
							(before_count + after_count));
		YbAddImageColumns(after_cols, after_count, true /* is_after */ ,
						  commit_time_ht, row->cols, &row->col_count);
		YbAddImageColumns(before_cols, before_count, false /* is_after */ ,
						  commit_time_ht, row->cols, &row->col_count);
	}
}

/*
 * Reads one query's worth of commit groups into batch. Returns false if the
 * table has nothing at or after the next commit_time.
 *
 * A query reads the commit times in a window starting at the next commit_time.
 * A window with no rows is skipped, if the table has rows after it, with a
 * larger window next time.
 */
static bool
YbReadBatch(YbcPgChangeRecordBatch *batch)
{
	MemoryContext caller_context = CurrentMemoryContext;
	bool		is_in_txn = IsTransactionOrTransactionBlock();
	Datum		args[2];
	Oid			arg_types[2] = {INT8OID, INT8OID};
	int			ret;
	uint64		ntuples;
	uint64		nusable;
	int64		target_rows;
	uint64		group_commit_time = 0;
	bool		group_open = false;
	int			nrows = 0;
	uint32		xid = 0;

	if (!is_in_txn)
		StartTransactionCommand();

	/* A walsender has no snapshot of its own, and SPI requires one. */
	PushActiveSnapshot(GetTransactionSnapshot());

	if (SPI_connect() != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed");
	MemoryContextSwitchTo(caller_context);

	for (;;)
	{
		uint64		window_end = yb_reader_next_commit_time +
		(yb_reader_window_micros << YB_HYBRID_TIME_LOGICAL_BITS);


		args[0] = Int64GetDatum((int64) yb_reader_next_commit_time);
		args[1] = Int64GetDatum((int64) window_end);
		ret = SPI_execute_with_args(yb_reader_window_query, 2, arg_types, args,
									NULL, true /* read_only */ , 0);
		if (ret != SPI_OK_SELECT)
			elog(ERROR, "querying pg_yb_cdc_changes failed: %d", ret);

		/*
		 * SPI leaves its procedure context current, which SPI_finish deletes.
		 * The batch must outlive it.
		 */
		MemoryContextSwitchTo(caller_context);

		if (SPI_processed > 0)
			break;

		/* An empty window: skip it if something comes after it. */
		args[0] = Int64GetDatum((int64) window_end);
		ret = SPI_execute_with_args(yb_reader_probe_query, 1, arg_types, args,
									NULL, true /* read_only */ , 0);
		if (ret != SPI_OK_SELECT)
			elog(ERROR, "querying pg_yb_cdc_changes failed: %d", ret);
		MemoryContextSwitchTo(caller_context);
		if (SPI_processed == 0)
		{
			SPI_finish();
			PopActiveSnapshot();
			if (!is_in_txn)
				AbortCurrentTransaction();
			MemoryContextSwitchTo(caller_context);
			return false;
		}
		yb_reader_next_commit_time = window_end;
		yb_reader_window_micros = Min(yb_reader_window_micros * 2,
									  YB_TABLE_READER_MAX_WINDOW_MICROS);
	}

	ntuples = SPI_processed;
	nusable = ntuples;

	/*
	 * Keep the window near the size that returns the target number of rows:
	 * as many as sort in half of work_mem, so that the sort of the query stays
	 * in memory, between a thousand and twenty thousand. A window of 20000
	 * rows of 600 bytes spilled to disk, which was 4% of the CPU of a
	 * walsender, and more for the sorting itself.
	 */
	target_rows = YB_TABLE_READER_MAX_TARGET_ROWS;
	if (ntuples > 0 && SPI_tuptable->vals[0]->t_len > 0)
	{
		int64		by_memory = ((int64) work_mem * 1024 / 2) /
		(SPI_tuptable->vals[0]->t_len + MAXALIGN(SizeofMinimalTupleHeader));

		target_rows = Max(YB_TABLE_READER_MIN_TARGET_ROWS,
						  Min(by_memory, YB_TABLE_READER_MAX_TARGET_ROWS));
	}
	if (ntuples < target_rows / 2)
		yb_reader_window_micros = Min(yb_reader_window_micros * 2,
									  YB_TABLE_READER_MAX_WINDOW_MICROS);
	else if (ntuples > target_rows * 2)
		yb_reader_window_micros = Max(yb_reader_window_micros / 2,
									  YB_TABLE_READER_MIN_WINDOW_MICROS);

	/*
	 * A full batch can end in the middle of a commit group. Keep only the
	 * groups that are complete. If the batch is a single group, read all of it.
	 */
	if (ntuples == YB_TABLE_READER_BATCH_ROWS)
	{
		bool		isnull;
		uint64		first_ct = DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[0],
														   SPI_tuptable->tupdesc,
														   YB_COL_COMMIT_TIME,
														   &isnull));
		uint64		last_ct = DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[ntuples - 1],
														  SPI_tuptable->tupdesc,
														  YB_COL_COMMIT_TIME,
														  &isnull));

		yb_reader_window_micros = Max(yb_reader_window_micros / 2,
									  YB_TABLE_READER_MIN_WINDOW_MICROS);
		if (first_ct == last_ct)
		{
			SPI_freetuptable(SPI_tuptable);
			args[0] = Int64GetDatum((int64) first_ct);
			args[1] = Int64GetDatum((int64) first_ct + 1);
			ret = SPI_execute_with_args(yb_reader_group_query, 2, arg_types,
										args, NULL, true /* read_only */ , 0);
			if (ret != SPI_OK_SELECT)
				elog(ERROR, "querying pg_yb_cdc_changes failed: %d", ret);
			MemoryContextSwitchTo(caller_context);
			ntuples = SPI_processed;
			nusable = ntuples;
		}
		else
		{
			while (nusable > 0 &&
				   DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[nusable - 1],
											   SPI_tuptable->tupdesc,
											   YB_COL_COMMIT_TIME,
											   &isnull)) == last_ct)
				nusable--;
		}
	}

	/* Worst case, every row is its own commit group: BEGIN, change, COMMIT. */
	batch->rows = palloc0(sizeof(YbcPgRowMessage) * nusable * 3);

	for (uint64 i = 0; i < nusable; i++)
	{
		HeapTuple	tuple = SPI_tuptable->vals[i];
		TupleDesc	tupdesc = SPI_tuptable->tupdesc;
		bool		isnull;
		Datum		val;
		uint64		commit_time;
		Oid			table_oid;

		val = SPI_getbinval(tuple, tupdesc, YB_COL_COMMIT_TIME, &isnull);
		if (isnull)
			ereport(ERROR,
					(errcode(ERRCODE_DATA_CORRUPTED),
					 errmsg("commit_time is null in pg_yb_cdc_changes")));
		commit_time = DatumGetInt64(val);

		/* The next query starts after the groups returned here. */
		yb_reader_next_commit_time = commit_time + 1;

		if (group_open && commit_time != group_commit_time)
		{
			YbInitRow(&batch->rows[nrows++], YB_PG_ROW_MESSAGE_ACTION_COMMIT,
					  group_commit_time, 0, xid);
			group_open = false;
		}

		val = SPI_getbinval(tuple, tupdesc, YB_COL_TABLE_ID, &isnull);
		if (isnull)
			ereport(ERROR,
					(errcode(ERRCODE_DATA_CORRUPTED),
					 errmsg("table_id is null in pg_yb_cdc_changes"),
					 errdetail("commit_time: %" PRIu64, commit_time)));
		table_oid = YbTableOidFromTableId(DatumGetUUIDP(val));
		if (!YbIsReaderTable(table_oid))
			continue;

		if (!group_open)
		{
			group_open = true;
			group_commit_time = commit_time;
			xid = yb_reader_next_xid++;
			if (yb_reader_next_xid < FirstNormalTransactionId)
				yb_reader_next_xid = FirstNormalTransactionId;
			YbInitRow(&batch->rows[nrows++], YB_PG_ROW_MESSAGE_ACTION_BEGIN,
					  commit_time, 0, xid);
		}

		YbFillDmlRow(&batch->rows[nrows++], tuple, tupdesc, commit_time,
					 table_oid, xid, batch->response);
	}

	if (group_open)
		YbInitRow(&batch->rows[nrows++], YB_PG_ROW_MESSAGE_ACTION_COMMIT,
				  group_commit_time, 0, xid);
	batch->row_count = nrows;

	SPI_finish();
	PopActiveSnapshot();
	if (!is_in_txn)
		AbortCurrentTransaction();
	MemoryContextSwitchTo(caller_context);

	return true;
}

/*
 * A read of the CDC table that overlaps, within the uncertainty of the clocks,
 * with a write to it fails with a restart-read error, which YugabyteDB
 * retries for the statements a session runs but not for a query in a
 * transaction that this reader began. The pollers write the table while it is
 * read, so this happens: at 30K changes/s about once in 30 seconds, and it ended
 * the walsender. Begin the transaction again, which reads at a later time, and
 * read the window again.
 */
#define YB_TABLE_READER_MAX_RESTARTS 20

static bool
YbReadBatchWithRestarts(YbcPgChangeRecordBatch *batch)
{
	MemoryContext caller_context = CurrentMemoryContext;
	/* A transaction that is not the reader's cannot be aborted here. */
	bool		can_restart = !IsTransactionOrTransactionBlock();

	for (int restarts = 0;; restarts++)
	{
		volatile bool more = false;
		volatile bool restart = false;

		PG_TRY();
		{
			more = YbReadBatch(batch);
		}
		PG_CATCH();
		{
			ErrorData  *edata;

			MemoryContextSwitchTo(caller_context);
			edata = CopyErrorData();
			if (!can_restart || restarts >= YB_TABLE_READER_MAX_RESTARTS ||
				edata->sqlerrcode != ERRCODE_YB_RESTART_READ)
			{
				FreeErrorData(edata);
				PG_RE_THROW();
			}
			FreeErrorData(edata);
			AbortCurrentTransaction();
			MemoryContextSwitchTo(caller_context);
			FlushErrorState();
			batch->rows = NULL;
			batch->row_count = 0;
			restart = true;
		}
		PG_END_TRY();

		if (!restart)
			return more;
		ereport(LOG,
				(errmsg("a read of pg_yb_cdc_changes needs a restart, reading the window again"),
				 errdetail("Restart %d of at most %d.", restarts + 1,
						   YB_TABLE_READER_MAX_RESTARTS)));
	}
}

YbcPgChangeRecordBatch *
YbTableReaderGetChanges(void)
{
	YbcPgChangeRecordBatch *batch = palloc0(sizeof(YbcPgChangeRecordBatch));
	MemoryContextCallback *free_values = palloc(sizeof(MemoryContextCallback));

	/*
	 * The values of the records are not converted yet, so they must outlive
	 * the batch's records. The caller resets the batch's context once the
	 * records are consumed, which frees them.
	 */
	batch->response = YBCPgNewCdcTableValues();
	free_values->func = YBCPgFreeCdcTableValues;
	free_values->arg = batch->response;
	MemoryContextRegisterResetCallback(CurrentMemoryContext, free_values);

	/*
	 * A query whose rows all belong to other tables yields no records. Keep
	 * reading rather than report an empty batch, which makes the caller sleep.
	 * Each read allocates its rows in this context, which is reset only once
	 * a batch is consumed, so free those of an empty read.
	 */
	while (YbReadBatchWithRestarts(batch) && batch->row_count == 0)
	{
		if (batch->rows)
		{
			pfree(batch->rows);
			batch->rows = NULL;
		}
	}

	return batch;
}
