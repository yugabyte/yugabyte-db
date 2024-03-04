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

#include "access/xact.h"
#include "replication/yb_decode.h"
#include "utils/rel.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

static void
YBDecodeInsert(LogicalDecodingContext *ctx, XLogReaderState *record);
static void
YBDecodeCommit(LogicalDecodingContext *ctx, XLogReaderState *record);

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
	elog(DEBUG4,
		 "YBLogicalDecodingProcessRecord: Decoding record with action = %d.",
		 record->yb_virtual_wal_record->action);
	switch (record->yb_virtual_wal_record->action)
	{
		case YB_PG_ROW_MESSAGE_ACTION_BEGIN:
			/*
			 * Start a transaction so that we can get the relation by oid in
			 * case of change operations. This transaction must be aborted
			 * after processing the corresponding commit record.
			 */
			StartTransactionCommand();
			return;

		case YB_PG_ROW_MESSAGE_ACTION_INSERT:
		{
			YBDecodeInsert(ctx, record);
			break;
		}

		/* TODO(#20726): Support Update and Delete operations. */
		case YB_PG_ROW_MESSAGE_ACTION_UPDATE: switch_fallthrough();
		case YB_PG_ROW_MESSAGE_ACTION_DELETE:
			break;

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
}

/*
 * YB version of the DecodeInsert function from decode.c
 */
static void
YBDecodeInsert(LogicalDecodingContext *ctx, XLogReaderState *record)
{
	const YBCPgVirtualWalRecord	*yb_record = record->yb_virtual_wal_record;
	ReorderBufferChange			*change = ReorderBufferGetChange(ctx->reorder);
	Relation					relation;
	TupleDesc					tupdesc;
	int							nattrs;
	HeapTuple					tuple;
	ReorderBufferTupleBuf		*tuple_buf;

	change->action = REORDER_BUFFER_CHANGE_INSERT;
	change->lsn = yb_record->lsn;
	/*
	 * We do not send the replication origin information. So any dummy value is
	 * sufficient here.
	 */
	change->origin_id = 1;

	ReorderBufferProcessXid(ctx->reorder, yb_record->xid,
							ctx->reader->ReadRecPtr);

	/*
	 * TODO(#20726): This is the schema of the relation at the streaming time.
	 * We need this to be the schema of the table at record commit time.
	 */
	relation = RelationIdGetRelation(yb_record->table_oid);
	if (!RelationIsValid(relation))
		elog(ERROR, "could not open relation with OID %u",
			 yb_record->table_oid);

	tupdesc = RelationGetDescr(relation);
	nattrs = tupdesc->natts;

	Datum datums[nattrs];
	bool is_nulls[nattrs];
	/* Set value to null by default so that we treat dropped columns as null. */
	memset(is_nulls, true, sizeof(is_nulls));
	for (int col_idx = 0; col_idx < yb_record->col_count; col_idx++)
	{
		const YBCPgDatumMessage *col = &yb_record->cols[col_idx];
		int attr_idx = 0;
		for (attr_idx = 0; attr_idx < nattrs; attr_idx++)
		{
			/* Skip columns that have been dropped. */
			if (tupdesc->attrs[attr_idx].attisdropped)
				continue;

			if (!strcmp(tupdesc->attrs[attr_idx].attname.data, col->column_name))
				break;
		}
		if (attr_idx == nattrs)
		{
			ereport(ERROR,
					(errcode(ERRCODE_INTERNAL_ERROR),
					 errmsg("Could not find column with name %s"
							" in tuple descriptor for table %d",
							col->column_name, yb_record->table_oid)));
			continue;
		}

		datums[attr_idx] = col->datum;
		is_nulls[attr_idx] = col->is_null;
	}
	tuple = heap_form_tuple(tupdesc, datums, is_nulls);

	tuple_buf =
		ReorderBufferGetTupleBuf(ctx->reorder, tuple->t_len + HEAPTUPLESIZE);
	tuple_buf->tuple = *tuple;
	change->data.tp.newtuple = tuple_buf;
	change->data.tp.oldtuple = NULL;
	change->data.tp.yb_table_oid = yb_record->table_oid;

	change->data.tp.clear_toast_afterwards = true;
	ReorderBufferQueueChange(ctx->reorder, yb_record->xid,
							 ctx->reader->ReadRecPtr, change);

	RelationClose(relation);
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

	ReorderBufferCommit(ctx->reorder, yb_record->xid, commit_lsn, end_lsn,
						yb_record->commit_time, origin_id, origin_lsn);
}
