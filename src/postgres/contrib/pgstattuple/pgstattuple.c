/*
 * contrib/pgstattuple/pgstattuple.c
 *
 * Copyright (c) 2001,2002	Tatsuo Ishii
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose, without fee, and without a
 * written agreement is hereby granted, provided that the above
 * copyright notice and this paragraph and the following two
 * paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE TO ANY PARTY FOR DIRECT,
 * INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS
 * DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE AUTHOR SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS
 * IS" BASIS, AND THE AUTHOR HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE,
 * SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

#include "postgres.h"

#include "access/gist_private.h"
#include "access/hash.h"
#include "access/heapam.h"
#include "access/nbtree.h"
#include "access/relscan.h"
#include "access/tableam.h"
#include "catalog/namespace.h"
#include "catalog/pg_am_d.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "storage/bufmgr.h"
#include "storage/lmgr.h"
#include "utils/builtins.h"
#include "utils/varlena.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(pgstattuple);
PG_FUNCTION_INFO_V1(pgstattuple_v1_5);
PG_FUNCTION_INFO_V1(pgstattuplebyid);
PG_FUNCTION_INFO_V1(pgstattuplebyid_v1_5);

/*
 * struct pgstattuple_type
 *
 * tuple_percent, dead_tuple_percent and free_percent are computable,
 * so not defined here.
 */
typedef struct pgstattuple_type
{
	uint64		table_len;
	uint64		tuple_count;
	uint64		tuple_len;
	uint64		dead_tuple_count;
	uint64		dead_tuple_len;
	uint64		free_space;		/* free/reusable space in bytes */
} pgstattuple_type;

typedef void (*pgstat_page) (pgstattuple_type *, Relation, BlockNumber,
							 BufferAccessStrategy);

static Datum build_pgstattuple_type(pgstattuple_type *stat,
									FunctionCallInfo fcinfo);
static Datum pgstat_relation(Relation rel, FunctionCallInfo fcinfo);
static Datum pgstat_heap(Relation rel, FunctionCallInfo fcinfo);
static void pgstat_btree_page(pgstattuple_type *stat,
							  Relation rel, BlockNumber blkno,
							  BufferAccessStrategy bstrategy);
static void pgstat_hash_page(pgstattuple_type *stat,
							 Relation rel, BlockNumber blkno,
							 BufferAccessStrategy bstrategy);
static void pgstat_gist_page(pgstattuple_type *stat,
							 Relation rel, BlockNumber blkno,
							 BufferAccessStrategy bstrategy);
static Datum pgstat_index(Relation rel, BlockNumber start,
						  pgstat_page pagefn, FunctionCallInfo fcinfo);
static void pgstat_index_page(pgstattuple_type *stat, Page page,
							  OffsetNumber minoff, OffsetNumber maxoff);

/*
 * build_pgstattuple_type -- build a pgstattuple_type tuple
 */
static Datum
build_pgstattuple_type(pgstattuple_type *stat, FunctionCallInfo fcinfo)
{
#define NCOLUMNS	9
#define NCHARS		314

	HeapTuple	tuple;
	char	   *values[NCOLUMNS];
	char		values_buf[NCOLUMNS][NCHARS];
	int			i;
	double		tuple_percent;
	double		dead_tuple_percent;
	double		free_percent;	/* free/reusable space in % */
	TupleDesc	tupdesc;
	AttInMetadata *attinmeta;

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	/*
	 * Generate attribute metadata needed later to produce tuples from raw C
	 * strings
	 */
	attinmeta = TupleDescGetAttInMetadata(tupdesc);

	if (stat->table_len == 0)
	{
		tuple_percent = 0.0;
		dead_tuple_percent = 0.0;
		free_percent = 0.0;
	}
	else
	{
		tuple_percent = 100.0 * stat->tuple_len / stat->table_len;
		dead_tuple_percent = 100.0 * stat->dead_tuple_len / stat->table_len;
		free_percent = 100.0 * stat->free_space / stat->table_len;
	}

	/*
	 * Prepare a values array for constructing the tuple. This should be an
	 * array of C strings which will be processed later by the appropriate
	 * "in" functions.
	 */
	for (i = 0; i < NCOLUMNS; i++)
		values[i] = values_buf[i];
	i = 0;
	snprintf(values[i++], NCHARS, INT64_FORMAT, stat->table_len);
	snprintf(values[i++], NCHARS, INT64_FORMAT, stat->tuple_count);
	snprintf(values[i++], NCHARS, INT64_FORMAT, stat->tuple_len);
	snprintf(values[i++], NCHARS, "%.2f", tuple_percent);
	snprintf(values[i++], NCHARS, INT64_FORMAT, stat->dead_tuple_count);
	snprintf(values[i++], NCHARS, INT64_FORMAT, stat->dead_tuple_len);
	snprintf(values[i++], NCHARS, "%.2f", dead_tuple_percent);
	snprintf(values[i++], NCHARS, INT64_FORMAT, stat->free_space);
	snprintf(values[i++], NCHARS, "%.2f", free_percent);

	/* build a tuple */
	tuple = BuildTupleFromCStrings(attinmeta, values);

	/* make the tuple into a datum */
	return HeapTupleGetDatum(tuple);
}

/* ----------
 * pgstattuple:
 * returns live/dead tuples info
 *
 * C FUNCTION definition
 * pgstattuple(text) returns pgstattuple_type
 *
 * The superuser() check here must be kept as the library might be upgraded
 * without the extension being upgraded, meaning that in pre-1.5 installations
 * these functions could be called by any user.
 * ----------
 */

Datum
pgstattuple(PG_FUNCTION_ARGS)
{
	text	   *relname = PG_GETARG_TEXT_PP(0);
	RangeVar   *relrv;
	Relation	rel;

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser to use pgstattuple functions")));

	/* open relation */
	relrv = makeRangeVarFromNameList(textToQualifiedNameList(relname));
	rel = relation_openrv(relrv, AccessShareLock);

	PG_RETURN_DATUM(pgstat_relation(rel, fcinfo));
}

/*
 * As of pgstattuple version 1.5, we no longer need to check if the user
 * is a superuser because we REVOKE EXECUTE on the function from PUBLIC.
 * Users can then grant access to it based on their policies.
 *
 * Otherwise identical to pgstattuple (above).
 */
Datum
pgstattuple_v1_5(PG_FUNCTION_ARGS)
{
	text	   *relname = PG_GETARG_TEXT_PP(0);
	RangeVar   *relrv;
	Relation	rel;

	/* open relation */
	relrv = makeRangeVarFromNameList(textToQualifiedNameList(relname));
	rel = relation_openrv(relrv, AccessShareLock);

	PG_RETURN_DATUM(pgstat_relation(rel, fcinfo));
}

/* Must keep superuser() check, see above. */
Datum
pgstattuplebyid(PG_FUNCTION_ARGS)
{
	Oid			relid = PG_GETARG_OID(0);
	Relation	rel;

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser to use pgstattuple functions")));

	/* open relation */
	rel = relation_open(relid, AccessShareLock);

	PG_RETURN_DATUM(pgstat_relation(rel, fcinfo));
}

/* Remove superuser() check for 1.5 version, see above */
Datum
pgstattuplebyid_v1_5(PG_FUNCTION_ARGS)
{
	Oid			relid = PG_GETARG_OID(0);
	Relation	rel;

	/* open relation */
	rel = relation_open(relid, AccessShareLock);

	PG_RETURN_DATUM(pgstat_relation(rel, fcinfo));
}

/*
 * pgstat_relation
 */
static Datum
pgstat_relation(Relation rel, FunctionCallInfo fcinfo)
{
	const char *err;

	/*
	 * Reject attempts to read non-local temporary relations; we would be
	 * likely to get wrong data since we have no visibility into the owning
	 * session's local buffers.
	 */
	if (RELATION_IS_OTHER_TEMP(rel))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot access temporary tables of other sessions")));

	if (RELKIND_HAS_TABLE_AM(rel->rd_rel->relkind) ||
		rel->rd_rel->relkind == RELKIND_SEQUENCE)
	{
		return pgstat_heap(rel, fcinfo);
	}
	else if (rel->rd_rel->relkind == RELKIND_INDEX)
	{
		switch (rel->rd_rel->relam)
		{
			case BTREE_AM_OID:
				return pgstat_index(rel, BTREE_METAPAGE + 1,
									pgstat_btree_page, fcinfo);
			case HASH_AM_OID:
				return pgstat_index(rel, HASH_METAPAGE + 1,
									pgstat_hash_page, fcinfo);
			case GIST_AM_OID:
				return pgstat_index(rel, GIST_ROOT_BLKNO + 1,
									pgstat_gist_page, fcinfo);
			case GIN_AM_OID:
				err = "gin index";
				break;
			case SPGIST_AM_OID:
				err = "spgist index";
				break;
			case BRIN_AM_OID:
				err = "brin index";
				break;
			default:
				err = "unknown index";
				break;
		}
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("index \"%s\" (%s) is not supported",
						RelationGetRelationName(rel), err)));
	}
	else
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot get tuple-level statistics for relation \"%s\"",
						RelationGetRelationName(rel)),
				 errdetail_relkind_not_supported(rel->rd_rel->relkind)));
	}

	return 0;					/* should not happen */
}

/*
 * pgstat_heap -- returns live/dead tuples info in a heap
 */
static Datum
pgstat_heap(Relation rel, FunctionCallInfo fcinfo)
{
	TableScanDesc scan;
	HeapScanDesc hscan;
	HeapTuple	tuple;
	BlockNumber nblocks;
	BlockNumber block = 0;		/* next block to count free space in */
	BlockNumber tupblock;
	Buffer		buffer;
	pgstattuple_type stat = {0};
	SnapshotData SnapshotDirty;

	if (rel->rd_rel->relam != HEAP_TABLE_AM_OID)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("only heap AM is supported")));

	/* Disable syncscan because we assume we scan from block zero upwards */
	scan = table_beginscan_strat(rel, SnapshotAny, 0, NULL, true, false);
	hscan = (HeapScanDesc) scan;

	InitDirtySnapshot(SnapshotDirty);

	nblocks = hscan->rs_nblocks;	/* # blocks to be scanned */

	/* scan the relation */
	while ((tuple = heap_getnext(scan, ForwardScanDirection)) != NULL)
	{
		CHECK_FOR_INTERRUPTS();

		/* must hold a buffer lock to call HeapTupleSatisfiesVisibility */
		LockBuffer(hscan->rs_cbuf, BUFFER_LOCK_SHARE);

		if (HeapTupleSatisfiesVisibility(tuple, &SnapshotDirty, hscan->rs_cbuf))
		{
			stat.tuple_len += tuple->t_len;
			stat.tuple_count++;
		}
		else
		{
			stat.dead_tuple_len += tuple->t_len;
			stat.dead_tuple_count++;
		}

		LockBuffer(hscan->rs_cbuf, BUFFER_LOCK_UNLOCK);

		/*
		 * To avoid physically reading the table twice, try to do the
		 * free-space scan in parallel with the heap scan.  However,
		 * heap_getnext may find no tuples on a given page, so we cannot
		 * simply examine the pages returned by the heap scan.
		 */
		tupblock = ItemPointerGetBlockNumber(&tuple->t_self);

		while (block <= tupblock)
		{
			CHECK_FOR_INTERRUPTS();

			buffer = ReadBufferExtended(rel, MAIN_FORKNUM, block,
										RBM_NORMAL, hscan->rs_strategy);
			LockBuffer(buffer, BUFFER_LOCK_SHARE);
			stat.free_space += PageGetHeapFreeSpace((Page) BufferGetPage(buffer));
			UnlockReleaseBuffer(buffer);
			block++;
		}
	}

	while (block < nblocks)
	{
		CHECK_FOR_INTERRUPTS();

		buffer = ReadBufferExtended(rel, MAIN_FORKNUM, block,
									RBM_NORMAL, hscan->rs_strategy);
		LockBuffer(buffer, BUFFER_LOCK_SHARE);
		stat.free_space += PageGetHeapFreeSpace((Page) BufferGetPage(buffer));
		UnlockReleaseBuffer(buffer);
		block++;
	}

	table_endscan(scan);
	relation_close(rel, AccessShareLock);

	stat.table_len = (uint64) nblocks * BLCKSZ;

	return build_pgstattuple_type(&stat, fcinfo);
}

/*
 * pgstat_btree_page -- check tuples in a btree page
 */
static void
pgstat_btree_page(pgstattuple_type *stat, Relation rel, BlockNumber blkno,
				  BufferAccessStrategy bstrategy)
{
	Buffer		buf;
	Page		page;

	buf = ReadBufferExtended(rel, MAIN_FORKNUM, blkno, RBM_NORMAL, bstrategy);
	LockBuffer(buf, BT_READ);
	page = BufferGetPage(buf);

	/* Page is valid, see what to do with it */
	if (PageIsNew(page))
	{
		/* fully empty page */
		stat->free_space += BLCKSZ;
	}
	else
	{
		BTPageOpaque opaque;

		opaque = BTPageGetOpaque(page);
		if (P_IGNORE(opaque))
		{
			/* deleted or half-dead page */
			stat->free_space += BLCKSZ;
		}
		else if (P_ISLEAF(opaque))
		{
			pgstat_index_page(stat, page, P_FIRSTDATAKEY(opaque),
							  PageGetMaxOffsetNumber(page));
		}
		else
		{
			/* internal page */
		}
	}

	_bt_relbuf(rel, buf);
}

/*
 * pgstat_hash_page -- check tuples in a hash page
 */
static void
pgstat_hash_page(pgstattuple_type *stat, Relation rel, BlockNumber blkno,
				 BufferAccessStrategy bstrategy)
{
	Buffer		buf;
	Page		page;

	buf = _hash_getbuf_with_strategy(rel, blkno, HASH_READ, 0, bstrategy);
	page = BufferGetPage(buf);

	if (PageGetSpecialSize(page) == MAXALIGN(sizeof(HashPageOpaqueData)))
	{
		HashPageOpaque opaque;

		opaque = HashPageGetOpaque(page);
		switch (opaque->hasho_flag & LH_PAGE_TYPE)
		{
			case LH_UNUSED_PAGE:
				stat->free_space += BLCKSZ;
				break;
			case LH_BUCKET_PAGE:
			case LH_OVERFLOW_PAGE:
				pgstat_index_page(stat, page, FirstOffsetNumber,
								  PageGetMaxOffsetNumber(page));
				break;
			case LH_BITMAP_PAGE:
			case LH_META_PAGE:
			default:
				break;
		}
	}
	else
	{
		/* maybe corrupted */
	}

	_hash_relbuf(rel, buf);
}

/*
 * pgstat_gist_page -- check tuples in a gist page
 */
static void
pgstat_gist_page(pgstattuple_type *stat, Relation rel, BlockNumber blkno,
				 BufferAccessStrategy bstrategy)
{
	Buffer		buf;
	Page		page;

	buf = ReadBufferExtended(rel, MAIN_FORKNUM, blkno, RBM_NORMAL, bstrategy);
	LockBuffer(buf, GIST_SHARE);
	gistcheckpage(rel, buf);
	page = BufferGetPage(buf);

	if (GistPageIsLeaf(page))
	{
		pgstat_index_page(stat, page, FirstOffsetNumber,
						  PageGetMaxOffsetNumber(page));
	}
	else
	{
		/* root or node */
	}

	UnlockReleaseBuffer(buf);
}

/*
 * pgstat_index -- returns live/dead tuples info in a generic index
 */
static Datum
pgstat_index(Relation rel, BlockNumber start, pgstat_page pagefn,
			 FunctionCallInfo fcinfo)
{
	BlockNumber nblocks;
	BlockNumber blkno;
	BufferAccessStrategy bstrategy;
	pgstattuple_type stat = {0};

	/* prepare access strategy for this index */
	bstrategy = GetAccessStrategy(BAS_BULKREAD);

	blkno = start;
	for (;;)
	{
		/* Get the current relation length */
		LockRelationForExtension(rel, ExclusiveLock);
		nblocks = RelationGetNumberOfBlocks(rel);
		UnlockRelationForExtension(rel, ExclusiveLock);

		/* Quit if we've scanned the whole relation */
		if (blkno >= nblocks)
		{
			stat.table_len = (uint64) nblocks * BLCKSZ;

			break;
		}

		for (; blkno < nblocks; blkno++)
		{
			CHECK_FOR_INTERRUPTS();

			pagefn(&stat, rel, blkno, bstrategy);
		}
	}

	relation_close(rel, AccessShareLock);

	return build_pgstattuple_type(&stat, fcinfo);
}

/*
 * pgstat_index_page -- for generic index page
 */
static void
pgstat_index_page(pgstattuple_type *stat, Page page,
				  OffsetNumber minoff, OffsetNumber maxoff)
{
	OffsetNumber i;

	stat->free_space += PageGetFreeSpace(page);

	for (i = minoff; i <= maxoff; i = OffsetNumberNext(i))
	{
		ItemId		itemid = PageGetItemId(page, i);

		if (ItemIdIsDead(itemid))
		{
			stat->dead_tuple_count++;
			stat->dead_tuple_len += ItemIdGetLength(itemid);
		}
		else
		{
			stat->tuple_count++;
			stat->tuple_len += ItemIdGetLength(itemid);
		}
	}
}
