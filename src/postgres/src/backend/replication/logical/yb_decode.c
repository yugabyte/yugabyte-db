/*--------------------------------------------------------------------------------------------------
 *
 * yb_decode.c
 *		This module decodes YB Virtual WAL records read using yb_virtual_wal_client.h's APIs for the
 *		purpose of logical decoding by passing information to the
 *		reorderbuffer module (containing the actual changes).
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
 *        src/postgres/src/backend/replication/logical/yb_decode.c
 *
 *--------------------------------------------------------------------------------------------------
 */

#include "postgres.h"

#include <inttypes.h>

#include "access/xact.h"
#include "pg_yb_utils.h"
#include "replication/walsender_private.h"
#include "replication/yb_decode.h"
#include "utils/rel.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

static void
YBDecodeInsert(LogicalDecodingContext *ctx, XLogReaderState *record);
static void
YBDecodeUpdate(LogicalDecodingContext *ctx, XLogReaderState *record);
static void
YBDecodeDelete(LogicalDecodingContext *ctx, XLogReaderState *record);
static void
YBDecodeCommit(LogicalDecodingContext *ctx, XLogReaderState *record);

static HeapTuple
YBGetHeapTuplesForRecord(const YBCPgVirtualWalRecord *yb_record,
						 enum ReorderBufferChangeType change_type);
static int
YBFindAttributeIndexInDescriptor(TupleDesc tupdesc, const char *column_name);
static void
YBHandleRelcacheRefresh(LogicalDecodingContext *ctx, XLogReaderState *record);

static void
YBLogTupleDescIfRequested(const YBCPgVirtualWalRecord *yb_record,
						  TupleDesc tupdesc);

/*
 * Take every record received from the YB VirtualWAL and perform the actions
 * required to decode it using the output plugin already setup in the logical
 * decoding context.
 *
 * We make certain assumptions about the records received from the VirtualWAL:
 * 1. No interleaved transactions
 * 2. No irrelevant records other than DDL
 *
 * Even though there are no interleaved transactions, we still use the
 * ReorderBuffer component as it has the logic to spill large transactions to
 * disk.
 */
void
YBLogicalDecodingProcessRecord(LogicalDecodingContext *ctx,
							   XLogReaderState *record)
{
	TimestampTz start_time = GetCurrentTimestamp();

	elog(DEBUG4,
		 "YBLogicalDecodingProcessRecord: Decoding record with action = %d. "
		 "yb_read_time is set to %d",
		 record->yb_virtual_wal_record->action, yb_is_read_time_ht);

	/* Check if we need a relcache refresh. */
	YBHandleRelcacheRefresh(ctx, record);

	/* Now delegate to specific handlers depending on the action type. */
	switch (record->yb_virtual_wal_record->action)
	{
		/* Nothing to handle here. */
		case YB_PG_ROW_MESSAGE_ACTION_DDL:
			elog(DEBUG4,
				 "Received DDL record for table: %d, xid: %d, commit_time_ht: "
				 "%" PRIu64,
				 record->yb_virtual_wal_record->table_oid,
				 record->yb_virtual_wal_record->xid,
				 record->yb_virtual_wal_record->commit_time_ht);
			break;

		case YB_PG_ROW_MESSAGE_ACTION_BEGIN:
			/*
			 * Start a transaction so that we can get the relation by oid in
			 * case of change operations. This transaction must be aborted
			 * after processing the corresponding commit record.
			 */
			StartTransactionCommand();
			break;

		case YB_PG_ROW_MESSAGE_ACTION_INSERT:
		{
			YBDecodeInsert(ctx, record);
			break;
		}

		case YB_PG_ROW_MESSAGE_ACTION_UPDATE:
		{
			YBDecodeUpdate(ctx, record);
			break;
		}

		case YB_PG_ROW_MESSAGE_ACTION_DELETE:
		{
			YBDecodeDelete(ctx, record);
			break;
		}

		case YB_PG_ROW_MESSAGE_ACTION_COMMIT:
		{
			YBDecodeCommit(ctx, record);

			/*
			 * Abort the transaction that we started upon receiving the BEGIN
			 * message.
			 */
			AbortCurrentTransaction();
			Assert(!IsTransactionState());
			break;
		}

		/* Should never happen. */
		case YB_PG_ROW_MESSAGE_ACTION_UNKNOWN:
			pg_unreachable();
	}

	YbWalSndTotalTimeInYBDecodeMicros +=
		YbCalculateTimeDifferenceInMicros(start_time);
}

/*
 * YB version of the DecodeInsert function from decode.c
 */
static void
YBDecodeInsert(LogicalDecodingContext *ctx, XLogReaderState *record)
{
	const YBCPgVirtualWalRecord	*yb_record = record->yb_virtual_wal_record;
	ReorderBufferChange			*change = ReorderBufferGetChange(ctx->reorder);
	HeapTuple					tuple;
	ReorderBufferTupleBuf		*tuple_buf;

	Assert(ctx->reader->ReadRecPtr == yb_record->lsn);

	change->action = REORDER_BUFFER_CHANGE_INSERT;
	/*
	 * We do not send the replication origin information. So any dummy value is
	 * sufficient here.
	 */
	change->origin_id = 1;

	ReorderBufferProcessXid(ctx->reorder, yb_record->xid,
							ctx->reader->ReadRecPtr);

	/*
	 * In PG, we know the size of the tuple from the header, so they can
	 * directly allocated the tuple_buf in the reorderbuffer and copy the tuple
	 * contents into it. In YSQL, we don't know the size of the tuple. The only
	 * way to do it is to first create the heap tuple itself. So we do a two
	 * step process.
	 *
	 * We first create a heap tuple from the YB virtual WAL record just to know
	 * its size. We then allocate the necessary space in the reorderbuffer and
	 * copy the tuple contents into it. Finally, we free the first tuple
	 * created.
	 */
	tuple = YBGetHeapTuplesForRecord(yb_record, REORDER_BUFFER_CHANGE_INSERT);
	tuple_buf =
		ReorderBufferGetTupleBuf(ctx->reorder, tuple->t_len + HEAPTUPLESIZE);
	yb_heap_copytuple_with_tuple(tuple, &tuple_buf->tuple);
	pfree(tuple);

	change->data.tp.newtuple = tuple_buf;
	change->data.tp.oldtuple = NULL;
	change->data.tp.yb_table_oid = yb_record->table_oid;

	change->data.tp.clear_toast_afterwards = true;
	/* YB_TODO: Do the right thing for the last parameter, toast_insert. */
	ReorderBufferQueueChange(ctx->reorder, yb_record->xid,
							 ctx->reader->ReadRecPtr, change, false);
}

/*
 * YB version of the DecodeUpdate function from decode.c
 */
static void
YBDecodeUpdate(LogicalDecodingContext *ctx, XLogReaderState *record)
{
	const YBCPgVirtualWalRecord	*yb_record = record->yb_virtual_wal_record;
	ReorderBufferChange		*change = ReorderBufferGetChange(ctx->reorder);
	Relation				relation;
	TupleDesc				tupdesc;
	int						nattrs;
	HeapTuple				after_op_tuple;
	HeapTuple				before_op_tuple;
	ReorderBufferTupleBuf	*after_op_tuple_buf;
	ReorderBufferTupleBuf	*before_op_tuple_buf;
	bool					*before_op_is_omitted = NULL;
	bool					*after_op_is_omitted = NULL;
	bool 					should_handle_omitted_case;

	change->action = REORDER_BUFFER_CHANGE_UPDATE;
	change->lsn = yb_record->lsn;
	change->origin_id = yb_record->lsn;

	relation =
		YbGetRelationWithOverwrittenReplicaIdentity(yb_record->table_oid,
													YBCGetReplicaIdentityForRelation(yb_record->table_oid));
	tupdesc = RelationGetDescr(relation);
	nattrs = tupdesc->natts;
	YBLogTupleDescIfRequested(yb_record, tupdesc);

	/*
	 * Allocate is_omitted arrays before so that we can directly write to it
	 * instead of creating a temporary array and doing a memcpy.
	 * We assume that columns are omitted by default.
	 *
	 * The special handling of omission vs NULL is only required
	 * for YB specific replica identity (record type) values. Presently, it is
	 * only CHANGE.
	 */
	should_handle_omitted_case =
		YBCGetReplicaIdentityForRelation(yb_record->table_oid) ==
		YB_REPLICA_IDENTITY_CHANGE;
	if (should_handle_omitted_case)
	{
		before_op_is_omitted = YBAllocateIsOmittedArray(ctx->reorder, nattrs);
		after_op_is_omitted = YBAllocateIsOmittedArray(ctx->reorder, nattrs);
		memset(after_op_is_omitted, 1, sizeof(bool) * nattrs);
		memset(before_op_is_omitted, 1, sizeof(bool) * nattrs);
	}

	Datum after_op_datums[nattrs];
	bool after_op_is_nulls[nattrs];
	Datum before_op_datums[nattrs];
	bool before_op_is_nulls[nattrs];
	memset(after_op_is_nulls, 1, sizeof(after_op_is_nulls));
	memset(before_op_is_nulls, 1, sizeof(before_op_is_nulls));
	for (int col_idx = 0; col_idx < yb_record->col_count; col_idx++)
	{
		YBCPgDatumMessage *col = &yb_record->cols[col_idx];

		/*
		 * Column name is null when both new and old values are omitted. If this
		 * were to happen here, this would indicate that an empty column value
		 * was sent from the CDC service which should have been caught in
		 * ybc_pggate.
		 */
		Assert(col->column_name);

		int attr_idx =
			YBFindAttributeIndexInDescriptor(tupdesc, col->column_name);

		if (should_handle_omitted_case)
		{
			after_op_is_omitted[attr_idx] = col->after_op_is_omitted;
			before_op_is_omitted[attr_idx] = col->before_op_is_omitted;
		}

		if (!should_handle_omitted_case || !col->after_op_is_omitted)
		{
			after_op_datums[attr_idx] = col->after_op_datum;
			after_op_is_nulls[attr_idx] = col->after_op_is_null;
		}
		if (!should_handle_omitted_case || !col->before_op_is_omitted)
		{
			before_op_datums[attr_idx] = col->before_op_datum;
			before_op_is_nulls[attr_idx] = col->before_op_is_null;
		}
	}

	/* See the comment in YBDecodeInsert on why we create tuples twice. */
	after_op_tuple =
		heap_form_tuple(tupdesc, after_op_datums, after_op_is_nulls);
	after_op_tuple_buf =
		ReorderBufferGetTupleBuf(ctx->reorder,
								 after_op_tuple->t_len + HEAPTUPLESIZE);
	yb_heap_copytuple_with_tuple(after_op_tuple, &after_op_tuple_buf->tuple);
	pfree(after_op_tuple);
	after_op_tuple_buf->yb_is_omitted = after_op_is_omitted;
	after_op_tuple_buf->yb_is_omitted_size =
		(should_handle_omitted_case) ? nattrs : 0;

	/*
	 * In YB, the primary key updates are sent as DELETE + INSERT. So the only
	 * updates that will be streamed are for non-primary key updates. For
	 * non-primary key updates non-null old-tuple will be sent only for replica
	 * identity FULL to match the PG behaviour.
	 */
	if (relation->rd_rel->relreplident == REPLICA_IDENTITY_FULL)
	{
		before_op_tuple =
			heap_form_tuple(tupdesc, before_op_datums, before_op_is_nulls);
		before_op_tuple_buf =
			ReorderBufferGetTupleBuf(ctx->reorder,
									 before_op_tuple->t_len + HEAPTUPLESIZE);
		yb_heap_copytuple_with_tuple(before_op_tuple,
									 &before_op_tuple_buf->tuple);
		pfree(before_op_tuple);
		before_op_tuple_buf->yb_is_omitted = before_op_is_omitted;
		before_op_tuple_buf->yb_is_omitted_size =
			(should_handle_omitted_case) ? nattrs : 0;
	}
	else
	{
		before_op_tuple_buf = NULL;
		if (should_handle_omitted_case && before_op_is_omitted)
			pfree(before_op_is_omitted);
	}

	if (log_min_messages <= DEBUG2)
	{
		const char *new_tuple_string;

		new_tuple_string =
			YbHeapTupleToStringWithIsOmitted(&after_op_tuple_buf->tuple,
											 tupdesc,
											 after_op_is_omitted);

		if (before_op_tuple_buf)
		{
			const char *old_tuple_string;

			old_tuple_string =
				YbHeapTupleToStringWithIsOmitted(&before_op_tuple_buf->tuple,
												 tupdesc,
												 before_op_is_omitted);
			elog(DEBUG2,
				 "yb_decode: before_op heap tuple: %s, after_op heap tuple: %s",
				 old_tuple_string, new_tuple_string);
			pfree((char *) old_tuple_string);
		}
		else
			elog(DEBUG2,
				 "yb_decode: before_op heap tuple: NULL, after_op heap tuple: %s",
				 new_tuple_string);

		pfree((char *) new_tuple_string);
	}

	change->data.tp.newtuple = after_op_tuple_buf;
	change->data.tp.oldtuple = before_op_tuple_buf;
	change->data.tp.yb_table_oid = yb_record->table_oid;

	change->data.tp.clear_toast_afterwards = true;
	/* YB_TODO: Do the right thing for the last parameter, toast_insert. */
	ReorderBufferQueueChange(ctx->reorder, yb_record->xid,
							 ctx->reader->ReadRecPtr, change, false);

	RelationClose(relation);
}

/*
 * YB version of the DecodeDelete function from decode.c
 */
static void
YBDecodeDelete(LogicalDecodingContext *ctx, XLogReaderState *record)
{
	const YBCPgVirtualWalRecord	*yb_record = record->yb_virtual_wal_record;
	ReorderBufferChange			*change = ReorderBufferGetChange(ctx->reorder);
	HeapTuple					tuple;
	ReorderBufferTupleBuf		*tuple_buf;

	Assert(ctx->reader->ReadRecPtr == yb_record->lsn);

	change->action = REORDER_BUFFER_CHANGE_DELETE;
	/*
	 * We do not send the replication origin information. So any dummy value is
	 * sufficient here.
	 */
	change->origin_id = 1;

	ReorderBufferProcessXid(ctx->reorder, yb_record->xid,
							ctx->reader->ReadRecPtr);

	/* See the comment in YBDecodeInsert on why we create tuples twice. */
	tuple = YBGetHeapTuplesForRecord(yb_record, REORDER_BUFFER_CHANGE_DELETE);
	tuple_buf =
		ReorderBufferGetTupleBuf(ctx->reorder, tuple->t_len + HEAPTUPLESIZE);
	yb_heap_copytuple_with_tuple(tuple, &tuple_buf->tuple);
	pfree(tuple);

	change->data.tp.newtuple = NULL;
	change->data.tp.oldtuple = tuple_buf;
	change->data.tp.yb_table_oid = yb_record->table_oid;

	change->data.tp.clear_toast_afterwards = true;
	/* YB_TODO: Do the right thing for the last parameter, toast_insert. */
	ReorderBufferQueueChange(ctx->reorder, yb_record->xid,
							 ctx->reader->ReadRecPtr, change, false);
}

/*
 * YB version of the DecodeCommit function from decode.c
 */
static void
YBDecodeCommit(LogicalDecodingContext *ctx, XLogReaderState *record)
{
	const YBCPgVirtualWalRecord	*yb_record = record->yb_virtual_wal_record;
	XLogRecPtr					commit_lsn = yb_record->lsn;
	XLogRecPtr					end_lsn = yb_record->lsn + 1;
	XLogRecPtr					origin_lsn = yb_record->lsn;
	/*
	 * We do not send the replication origin information. So any dummy value is
	 * sufficient here.
	 */
	RepOriginId					origin_id = 1;

	/*
	 * Skip the records which the client hasn't asked for. Simpler version of a
	 * similar check done in DecodeCommit in decode.c
	 */
	if (commit_lsn < ctx->yb_start_decoding_at)
	{
		/*
		 * ReorderBufferForget handles the cleanup of subtransactions as well.
		 * So this is sufficient to clean up the transaction along with its
		 * subtransactions.
		 */
		elog(DEBUG1,
			 "YBDecodeCommit: Ignoring txn %d with commit_lsn = %lu as "
			 "yb_start_decoding_at = %lu.",
			 yb_record->xid, commit_lsn, ctx->yb_start_decoding_at);
		ReorderBufferForget(ctx->reorder, yb_record->xid, commit_lsn);
		return;
	}

	elog(DEBUG1,
		 "Going to stream transaction: %d with commit_lsn: %lu and "
		 "end_lsn: %lu",
		 yb_record->xid, commit_lsn, end_lsn);

	ReorderBufferCommit(ctx->reorder, yb_record->xid, commit_lsn, end_lsn,
						yb_record->commit_time, origin_id, origin_lsn);

	elog(DEBUG1,
		 "Successfully streamed transaction: %d with commit_lsn: %lu and "
		 "end_lsn: %lu",
		 yb_record->xid, commit_lsn, end_lsn);
}

static HeapTuple
YBGetHeapTuplesForRecord(const YBCPgVirtualWalRecord *yb_record,
						 enum ReorderBufferChangeType change_type)
{
	Relation					relation;
	TupleDesc					tupdesc;
	int							nattrs;
	HeapTuple					tuple;

	/*
	 * Note that we don't strictly need to overwrite the replica identity in
	 * yb_decode.c as this field is not read here. For correctness, we only need
	 * to override it in the reorderbuffer. We do it here just for completeness
	 * so that we don't end up with conflicting pieces of information about the
	 * replica identity in two different files that can lead to confusion.
	 */
	relation =
		YbGetRelationWithOverwrittenReplicaIdentity(yb_record->table_oid,
													YBCGetReplicaIdentityForRelation(yb_record->table_oid));

	tupdesc = RelationGetDescr(relation);
	nattrs = tupdesc->natts;
	YBLogTupleDescIfRequested(yb_record, tupdesc);

	Datum datums[nattrs];
	bool is_nulls[nattrs];
	/* Set value to null by default so that we treat dropped columns as null. */
	memset(is_nulls, true, sizeof(is_nulls));
	for (int col_idx = 0; col_idx < yb_record->col_count; col_idx++)
	{
		const YBCPgDatumMessage *col = &yb_record->cols[col_idx];
		int attr_idx =
			YBFindAttributeIndexInDescriptor(tupdesc, col->column_name);

		datums[attr_idx] = (change_type == REORDER_BUFFER_CHANGE_INSERT) ?
							   col->after_op_datum :
							   col->before_op_datum;
		is_nulls[attr_idx] = (change_type == REORDER_BUFFER_CHANGE_INSERT) ?
								 col->after_op_is_null :
								 col->before_op_is_null;
	}

	tuple = heap_form_tuple(tupdesc, datums, is_nulls);
	if (log_min_messages <= DEBUG2)
	{
		const char *tuple_string =
			YbHeapTupleToStringWithIsOmitted(tuple, tupdesc, NULL);

		elog(DEBUG2,
			 "yb_decode: The heap tuple: %s for operation: %s", tuple_string,
			 (change_type == REORDER_BUFFER_CHANGE_INSERT) ? "INSERT" :
															 "DELETE");

		pfree((char *) tuple_string);
	}

	RelationClose(relation);
	return tuple;
}

/*
 * TODO(#20726): Optimize this lookup via a cache. We do not need to iterate
 * through all attributes everytime this function is called. This should be done
 * after we have landed support for schema evolution as this logic is highly
 * likely to change before that.
 */
static int
YBFindAttributeIndexInDescriptor(TupleDesc tupdesc, const char *column_name)
{
	int attr_idx = 0;
	for (attr_idx = 0; attr_idx < tupdesc->natts; attr_idx++)
	{
		if (tupdesc->attrs[attr_idx].attisdropped)
			continue;

		if (!strcmp(tupdesc->attrs[attr_idx].attname.data, column_name))
			return attr_idx;
	}

	ereport(ERROR,
			(errcode(ERRCODE_INTERNAL_ERROR),
			 errmsg("could not find column with name %s in tuple"
					" descriptor", column_name)));
	return -1;			/* keep compiler quiet */
}

static void
YBHandleRelcacheRefresh(LogicalDecodingContext *ctx, XLogReaderState *record)
{
	Oid		table_oid = record->yb_virtual_wal_record->table_oid;

	switch (record->yb_virtual_wal_record->action)
	{
		case YB_PG_ROW_MESSAGE_ACTION_DDL:
		{
			bool		found;

			/*
			 * Mark for relcache invalidation to be done on first DML by just
			 * inserting an entry for the table_oid.
			 */
			hash_search(ctx->yb_needs_relcache_invalidation, &table_oid,
						HASH_ENTER, &found);
			break;
		}

		case YB_PG_ROW_MESSAGE_ACTION_INSERT: switch_fallthrough();
		case YB_PG_ROW_MESSAGE_ACTION_UPDATE: switch_fallthrough();
		case YB_PG_ROW_MESSAGE_ACTION_DELETE:
		{
			bool needs_invalidation = false;

			hash_search(ctx->yb_needs_relcache_invalidation, &table_oid,
						HASH_FIND, &needs_invalidation);

			if (needs_invalidation)
			{
				uint64_t read_time_ht;

				/* Use the commit_time_ht of the DML. */
				read_time_ht = record->yb_virtual_wal_record->commit_time_ht;

				elog(DEBUG2,
					 "Setting yb_read_time to record's commit_time_ht: %" PRIu64,
					 read_time_ht);
				YBCUpdateYbReadTimeAndInvalidateRelcache(read_time_ht);

				/*
				 * Let the plugin know that the schema for this table has
				 * changed, so it must send the new relation object to the
				 * client.
				 */
				YBReorderBufferSchemaChange(ctx->reorder, table_oid);

				bool found;
				hash_search(ctx->yb_needs_relcache_invalidation, &table_oid,
							HASH_REMOVE, &found);
				Assert(found);
			}
			break;
		}

		/* Nothing to handle for these types. */
		case YB_PG_ROW_MESSAGE_ACTION_UNKNOWN: switch_fallthrough();
		case YB_PG_ROW_MESSAGE_ACTION_BEGIN:   switch_fallthrough();
		case YB_PG_ROW_MESSAGE_ACTION_COMMIT:
			return;
	}
}

static void
YBLogTupleDescIfRequested(const YBCPgVirtualWalRecord *yb_record,
						  TupleDesc tupdesc)
{
	/* Log tuple descriptor for DEBUG2 onwards. */
	if (log_min_messages <= DEBUG2)
	{
		elog(DEBUG2, "Printing tuple descriptor for relation %d\n",
			 yb_record->table_oid);
		for (int attr_idx = 0; attr_idx < tupdesc->natts; attr_idx++)
		{
			elog(DEBUG2, "Col %d: name = %s, dropped = %d, type = %d\n",
						 attr_idx, tupdesc->attrs[attr_idx].attname.data,
						 tupdesc->attrs[attr_idx].attisdropped,
						 tupdesc->attrs[attr_idx].atttypid);
		}
	}
}
