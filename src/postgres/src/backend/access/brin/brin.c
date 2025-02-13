/*
 * brin.c
 *		Implementation of BRIN indexes for Postgres
 *
 * See src/backend/access/brin/README for details.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/access/brin/brin.c
 *
 * TODO
 *		* ScalarArrayOpExpr (amsearcharray -> SK_SEARCHARRAY)
 */
#include "postgres.h"

#include "access/brin.h"
#include "access/brin_page.h"
#include "access/brin_pageops.h"
#include "access/brin_xlog.h"
#include "access/relation.h"
#include "access/reloptions.h"
#include "access/relscan.h"
#include "access/table.h"
#include "access/tableam.h"
#include "access/xloginsert.h"
#include "catalog/index.h"
#include "catalog/pg_am.h"
#include "commands/vacuum.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "postmaster/autovacuum.h"
#include "storage/bufmgr.h"
#include "storage/freespace.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/index_selfuncs.h"
#include "utils/guc.h"
#include "utils/memutils.h"
#include "utils/rel.h"


/*
 * We use a BrinBuildState during initial construction of a BRIN index.
 * The running state is kept in a BrinMemTuple.
 */
typedef struct BrinBuildState
{
	Relation	bs_irel;
	int			bs_numtuples;
	Buffer		bs_currentInsertBuf;
	BlockNumber bs_pagesPerRange;
	BlockNumber bs_currRangeStart;
	BrinRevmap *bs_rmAccess;
	BrinDesc   *bs_bdesc;
	BrinMemTuple *bs_dtuple;
} BrinBuildState;

/*
 * Struct used as "opaque" during index scans
 */
typedef struct BrinOpaque
{
	BlockNumber bo_pagesPerRange;
	BrinRevmap *bo_rmAccess;
	BrinDesc   *bo_bdesc;
} BrinOpaque;

#define BRIN_ALL_BLOCKRANGES	InvalidBlockNumber

static BrinBuildState *initialize_brin_buildstate(Relation idxRel,
												  BrinRevmap *revmap, BlockNumber pagesPerRange);
static void terminate_brin_buildstate(BrinBuildState *state);
static void brinsummarize(Relation index, Relation heapRel, BlockNumber pageRange,
						  bool include_partial, double *numSummarized, double *numExisting);
static void form_and_insert_tuple(BrinBuildState *state);
static void union_tuples(BrinDesc *bdesc, BrinMemTuple *a,
						 BrinTuple *b);
static void brin_vacuum_scan(Relation idxrel, BufferAccessStrategy strategy);
static bool add_values_to_range(Relation idxRel, BrinDesc *bdesc,
								BrinMemTuple *dtup, Datum *values, bool *nulls);
static bool check_null_keys(BrinValues *bval, ScanKey *nullkeys, int nnullkeys);

/*
 * BRIN handler function: return IndexAmRoutine with access method parameters
 * and callbacks.
 */
Datum
brinhandler(PG_FUNCTION_ARGS)
{
	IndexAmRoutine *amroutine = makeNode(IndexAmRoutine);

	amroutine->amstrategies = 0;
	amroutine->amsupport = BRIN_LAST_OPTIONAL_PROCNUM;
	amroutine->amoptsprocnum = BRIN_PROCNUM_OPTIONS;
	amroutine->amcanorder = false;
	amroutine->amcanorderbyop = false;
	amroutine->amcanbackward = false;
	amroutine->amcanunique = false;
	amroutine->amcanmulticol = true;
	amroutine->amoptionalkey = true;
	amroutine->amsearcharray = false;
	amroutine->amsearchnulls = true;
	amroutine->amstorage = true;
	amroutine->amclusterable = false;
	amroutine->ampredlocks = false;
	amroutine->amcanparallel = false;
	amroutine->amcaninclude = false;
	amroutine->amusemaintenanceworkmem = false;
	amroutine->amparallelvacuumoptions =
		VACUUM_OPTION_PARALLEL_CLEANUP;
	amroutine->amkeytype = InvalidOid;

	amroutine->ambuild = brinbuild;
	amroutine->ambuildempty = brinbuildempty;
	amroutine->aminsert = brininsert;
	amroutine->ambulkdelete = brinbulkdelete;
	amroutine->amvacuumcleanup = brinvacuumcleanup;
	amroutine->amcanreturn = NULL;
	amroutine->amcostestimate = brincostestimate;
	amroutine->amoptions = brinoptions;
	amroutine->amproperty = NULL;
	amroutine->ambuildphasename = NULL;
	amroutine->amvalidate = brinvalidate;
	amroutine->amadjustmembers = NULL;
	amroutine->ambeginscan = brinbeginscan;
	amroutine->amrescan = brinrescan;
	amroutine->amgettuple = NULL;
	amroutine->amgetbitmap = bringetbitmap;
	amroutine->amendscan = brinendscan;
	amroutine->ammarkpos = NULL;
	amroutine->amrestrpos = NULL;
	amroutine->amestimateparallelscan = NULL;
	amroutine->aminitparallelscan = NULL;
	amroutine->amparallelrescan = NULL;

	PG_RETURN_POINTER(amroutine);
}

/*
 * A tuple in the heap is being inserted.  To keep a brin index up to date,
 * we need to obtain the relevant index tuple and compare its stored values
 * with those of the new tuple.  If the tuple values are not consistent with
 * the summary tuple, we need to update the index tuple.
 *
 * If autosummarization is enabled, check if we need to summarize the previous
 * page range.
 *
 * If the range is not currently summarized (i.e. the revmap returns NULL for
 * it), there's nothing to do for this tuple.
 */
bool
brininsert(Relation idxRel, Datum *values, bool *nulls,
		   ItemPointer heaptid, Relation heapRel,
		   IndexUniqueCheck checkUnique,
		   bool indexUnchanged,
		   IndexInfo *indexInfo)
{
	BlockNumber pagesPerRange;
	BlockNumber origHeapBlk;
	BlockNumber heapBlk;
	BrinDesc   *bdesc = (BrinDesc *) indexInfo->ii_AmCache;
	BrinRevmap *revmap;
	Buffer		buf = InvalidBuffer;
	MemoryContext tupcxt = NULL;
	MemoryContext oldcxt = GetCurrentMemoryContext();
	bool		autosummarize = BrinGetAutoSummarize(idxRel);

	revmap = brinRevmapInitialize(idxRel, &pagesPerRange, NULL);

	/*
	 * origHeapBlk is the block number where the insertion occurred.  heapBlk
	 * is the first block in the corresponding page range.
	 */
	origHeapBlk = ItemPointerGetBlockNumber(heaptid);
	heapBlk = (origHeapBlk / pagesPerRange) * pagesPerRange;

	for (;;)
	{
		bool		need_insert = false;
		OffsetNumber off;
		BrinTuple  *brtup;
		BrinMemTuple *dtup;

		CHECK_FOR_INTERRUPTS();

		/*
		 * If auto-summarization is enabled and we just inserted the first
		 * tuple into the first block of a new non-first page range, request a
		 * summarization run of the previous range.
		 */
		if (autosummarize &&
			heapBlk > 0 &&
			heapBlk == origHeapBlk &&
			ItemPointerGetOffsetNumber(heaptid) == FirstOffsetNumber)
		{
			BlockNumber lastPageRange = heapBlk - 1;
			BrinTuple  *lastPageTuple;

			lastPageTuple =
				brinGetTupleForHeapBlock(revmap, lastPageRange, &buf, &off,
										 NULL, BUFFER_LOCK_SHARE, NULL);
			if (!lastPageTuple)
			{
				bool		recorded;

				recorded = AutoVacuumRequestWork(AVW_BRINSummarizeRange,
												 RelationGetRelid(idxRel),
												 lastPageRange);
				if (!recorded)
					ereport(LOG,
							(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
							 errmsg("request for BRIN range summarization for index \"%s\" page %u was not recorded",
									RelationGetRelationName(idxRel),
									lastPageRange)));
			}
			else
				LockBuffer(buf, BUFFER_LOCK_UNLOCK);
		}

		brtup = brinGetTupleForHeapBlock(revmap, heapBlk, &buf, &off,
										 NULL, BUFFER_LOCK_SHARE, NULL);

		/* if range is unsummarized, there's nothing to do */
		if (!brtup)
			break;

		/* First time through in this statement? */
		if (bdesc == NULL)
		{
			MemoryContextSwitchTo(indexInfo->ii_Context);
			bdesc = brin_build_desc(idxRel);
			indexInfo->ii_AmCache = (void *) bdesc;
			MemoryContextSwitchTo(oldcxt);
		}
		/* First time through in this brininsert call? */
		if (tupcxt == NULL)
		{
			tupcxt = AllocSetContextCreate(GetCurrentMemoryContext(),
										   "brininsert cxt",
										   ALLOCSET_DEFAULT_SIZES);
			MemoryContextSwitchTo(tupcxt);
		}

		dtup = brin_deform_tuple(bdesc, brtup, NULL);

		need_insert = add_values_to_range(idxRel, bdesc, dtup, values, nulls);

		if (!need_insert)
		{
			/*
			 * The tuple is consistent with the new values, so there's nothing
			 * to do.
			 */
			LockBuffer(buf, BUFFER_LOCK_UNLOCK);
		}
		else
		{
			Page		page = BufferGetPage(buf);
			ItemId		lp = PageGetItemId(page, off);
			Size		origsz;
			BrinTuple  *origtup;
			Size		newsz;
			BrinTuple  *newtup;
			bool		samepage;

			/*
			 * Make a copy of the old tuple, so that we can compare it after
			 * re-acquiring the lock.
			 */
			origsz = ItemIdGetLength(lp);
			origtup = brin_copy_tuple(brtup, origsz, NULL, NULL);

			/*
			 * Before releasing the lock, check if we can attempt a same-page
			 * update.  Another process could insert a tuple concurrently in
			 * the same page though, so downstream we must be prepared to cope
			 * if this turns out to not be possible after all.
			 */
			newtup = brin_form_tuple(bdesc, heapBlk, dtup, &newsz);
			samepage = brin_can_do_samepage_update(buf, origsz, newsz);
			LockBuffer(buf, BUFFER_LOCK_UNLOCK);

			/*
			 * Try to update the tuple.  If this doesn't work for whatever
			 * reason, we need to restart from the top; the revmap might be
			 * pointing at a different tuple for this block now, so we need to
			 * recompute to ensure both our new heap tuple and the other
			 * inserter's are covered by the combined tuple.  It might be that
			 * we don't need to update at all.
			 */
			if (!brin_doupdate(idxRel, pagesPerRange, revmap, heapBlk,
							   buf, off, origtup, origsz, newtup, newsz,
							   samepage))
			{
				/* no luck; start over */
				MemoryContextResetAndDeleteChildren(tupcxt);
				continue;
			}
		}

		/* success! */
		break;
	}

	brinRevmapTerminate(revmap);
	if (BufferIsValid(buf))
		ReleaseBuffer(buf);
	MemoryContextSwitchTo(oldcxt);
	if (tupcxt != NULL)
		MemoryContextDelete(tupcxt);

	return false;
}

/*
 * Initialize state for a BRIN index scan.
 *
 * We read the metapage here to determine the pages-per-range number that this
 * index was built with.  Note that since this cannot be changed while we're
 * holding lock on index, it's not necessary to recompute it during brinrescan.
 */
IndexScanDesc
brinbeginscan(Relation r, int nkeys, int norderbys)
{
	IndexScanDesc scan;
	BrinOpaque *opaque;

	scan = RelationGetIndexScan(r, nkeys, norderbys);

	opaque = (BrinOpaque *) palloc(sizeof(BrinOpaque));
	opaque->bo_rmAccess = brinRevmapInitialize(r, &opaque->bo_pagesPerRange,
											   scan->xs_snapshot);
	opaque->bo_bdesc = brin_build_desc(r);
	scan->opaque = opaque;

	return scan;
}

/*
 * Execute the index scan.
 *
 * This works by reading index TIDs from the revmap, and obtaining the index
 * tuples pointed to by them; the summary values in the index tuples are
 * compared to the scan keys.  We return into the TID bitmap all the pages in
 * ranges corresponding to index tuples that match the scan keys.
 *
 * If a TID from the revmap is read as InvalidTID, we know that range is
 * unsummarized.  Pages in those ranges need to be returned regardless of scan
 * keys.
 */
int64
bringetbitmap(IndexScanDesc scan, TIDBitmap *tbm)
{
	Relation	idxRel = scan->indexRelation;
	Buffer		buf = InvalidBuffer;
	BrinDesc   *bdesc;
	Oid			heapOid;
	Relation	heapRel;
	BrinOpaque *opaque;
	BlockNumber nblocks;
	BlockNumber heapBlk;
	int			totalpages = 0;
	FmgrInfo   *consistentFn;
	MemoryContext oldcxt;
	MemoryContext perRangeCxt;
	BrinMemTuple *dtup;
	BrinTuple  *btup = NULL;
	Size		btupsz = 0;
	ScanKey   **keys,
			  **nullkeys;
	int		   *nkeys,
			   *nnullkeys;
	int			keyno;
	char	   *ptr;
	Size		len;
	char	   *tmp PG_USED_FOR_ASSERTS_ONLY;

	opaque = (BrinOpaque *) scan->opaque;
	bdesc = opaque->bo_bdesc;
	pgstat_count_index_scan(idxRel);

	/*
	 * We need to know the size of the table so that we know how long to
	 * iterate on the revmap.
	 */
	heapOid = IndexGetRelation(RelationGetRelid(idxRel), false);
	heapRel = table_open(heapOid, AccessShareLock);
	nblocks = RelationGetNumberOfBlocks(heapRel);
	table_close(heapRel, AccessShareLock);

	/*
	 * Make room for the consistent support procedures of indexed columns.  We
	 * don't look them up here; we do that lazily the first time we see a scan
	 * key reference each of them.  We rely on zeroing fn_oid to InvalidOid.
	 */
	consistentFn = palloc0(sizeof(FmgrInfo) * bdesc->bd_tupdesc->natts);

	/*
	 * Make room for per-attribute lists of scan keys that we'll pass to the
	 * consistent support procedure. We don't know which attributes have scan
	 * keys, so we allocate space for all attributes. That may use more memory
	 * but it's probably cheaper than determining which attributes are used.
	 *
	 * We keep null and regular keys separate, so that we can pass just the
	 * regular keys to the consistent function easily.
	 *
	 * To reduce the allocation overhead, we allocate one big chunk and then
	 * carve it into smaller arrays ourselves. All the pieces have exactly the
	 * same lifetime, so that's OK.
	 *
	 * XXX The widest index can have 32 attributes, so the amount of wasted
	 * memory is negligible. We could invent a more compact approach (with
	 * just space for used attributes) but that would make the matching more
	 * complex so it's not a good trade-off.
	 */
	len =
		MAXALIGN(sizeof(ScanKey *) * bdesc->bd_tupdesc->natts) +	/* regular keys */
		MAXALIGN(sizeof(ScanKey) * scan->numberOfKeys) * bdesc->bd_tupdesc->natts +
		MAXALIGN(sizeof(int) * bdesc->bd_tupdesc->natts) +
		MAXALIGN(sizeof(ScanKey *) * bdesc->bd_tupdesc->natts) +	/* NULL keys */
		MAXALIGN(sizeof(ScanKey) * scan->numberOfKeys) * bdesc->bd_tupdesc->natts +
		MAXALIGN(sizeof(int) * bdesc->bd_tupdesc->natts);

	ptr = palloc(len);
	tmp = ptr;

	keys = (ScanKey **) ptr;
	ptr += MAXALIGN(sizeof(ScanKey *) * bdesc->bd_tupdesc->natts);

	nullkeys = (ScanKey **) ptr;
	ptr += MAXALIGN(sizeof(ScanKey *) * bdesc->bd_tupdesc->natts);

	nkeys = (int *) ptr;
	ptr += MAXALIGN(sizeof(int) * bdesc->bd_tupdesc->natts);

	nnullkeys = (int *) ptr;
	ptr += MAXALIGN(sizeof(int) * bdesc->bd_tupdesc->natts);

	for (int i = 0; i < bdesc->bd_tupdesc->natts; i++)
	{
		keys[i] = (ScanKey *) ptr;
		ptr += MAXALIGN(sizeof(ScanKey) * scan->numberOfKeys);

		nullkeys[i] = (ScanKey *) ptr;
		ptr += MAXALIGN(sizeof(ScanKey) * scan->numberOfKeys);
	}

	Assert(tmp + len == ptr);

	/* zero the number of keys */
	memset(nkeys, 0, sizeof(int) * bdesc->bd_tupdesc->natts);
	memset(nnullkeys, 0, sizeof(int) * bdesc->bd_tupdesc->natts);

	/* Preprocess the scan keys - split them into per-attribute arrays. */
	for (keyno = 0; keyno < scan->numberOfKeys; keyno++)
	{
		ScanKey		key = &scan->keyData[keyno];
		AttrNumber	keyattno = key->sk_attno;

		/*
		 * The collation of the scan key must match the collation used in the
		 * index column (but only if the search is not IS NULL/ IS NOT NULL).
		 * Otherwise we shouldn't be using this index ...
		 */
		Assert((key->sk_flags & SK_ISNULL) ||
			   (key->sk_collation ==
				TupleDescAttr(bdesc->bd_tupdesc,
							  keyattno - 1)->attcollation));

		/*
		 * First time we see this index attribute, so init as needed.
		 *
		 * This is a bit of an overkill - we don't know how many scan keys are
		 * there for this attribute, so we simply allocate the largest number
		 * possible (as if all keys were for this attribute). This may waste a
		 * bit of memory, but we only expect small number of scan keys in
		 * general, so this should be negligible, and repeated repalloc calls
		 * are not free either.
		 */
		if (consistentFn[keyattno - 1].fn_oid == InvalidOid)
		{
			FmgrInfo   *tmp;

			/* First time we see this attribute, so no key/null keys. */
			Assert(nkeys[keyattno - 1] == 0);
			Assert(nnullkeys[keyattno - 1] == 0);

			tmp = index_getprocinfo(idxRel, keyattno,
									BRIN_PROCNUM_CONSISTENT);
			fmgr_info_copy(&consistentFn[keyattno - 1], tmp,
						   GetCurrentMemoryContext());
		}

		/* Add key to the proper per-attribute array. */
		if (key->sk_flags & SK_ISNULL)
		{
			nullkeys[keyattno - 1][nnullkeys[keyattno - 1]] = key;
			nnullkeys[keyattno - 1]++;
		}
		else
		{
			keys[keyattno - 1][nkeys[keyattno - 1]] = key;
			nkeys[keyattno - 1]++;
		}
	}

	/* allocate an initial in-memory tuple, out of the per-range memcxt */
	dtup = brin_new_memtuple(bdesc);

	/*
	 * Setup and use a per-range memory context, which is reset every time we
	 * loop below.  This avoids having to free the tuples within the loop.
	 */
	perRangeCxt = AllocSetContextCreate(GetCurrentMemoryContext(),
										"bringetbitmap cxt",
										ALLOCSET_DEFAULT_SIZES);
	oldcxt = MemoryContextSwitchTo(perRangeCxt);

	/*
	 * Now scan the revmap.  We start by querying for heap page 0,
	 * incrementing by the number of pages per range; this gives us a full
	 * view of the table.
	 */
	for (heapBlk = 0; heapBlk < nblocks; heapBlk += opaque->bo_pagesPerRange)
	{
		bool		addrange;
		bool		gottuple = false;
		BrinTuple  *tup;
		OffsetNumber off;
		Size		size;

		CHECK_FOR_INTERRUPTS();

		MemoryContextResetAndDeleteChildren(perRangeCxt);

		tup = brinGetTupleForHeapBlock(opaque->bo_rmAccess, heapBlk, &buf,
									   &off, &size, BUFFER_LOCK_SHARE,
									   scan->xs_snapshot);
		if (tup)
		{
			gottuple = true;
			btup = brin_copy_tuple(tup, size, btup, &btupsz);
			LockBuffer(buf, BUFFER_LOCK_UNLOCK);
		}

		/*
		 * For page ranges with no indexed tuple, we must return the whole
		 * range; otherwise, compare it to the scan keys.
		 */
		if (!gottuple)
		{
			addrange = true;
		}
		else
		{
			dtup = brin_deform_tuple(bdesc, btup, dtup);
			if (dtup->bt_placeholder)
			{
				/*
				 * Placeholder tuples are always returned, regardless of the
				 * values stored in them.
				 */
				addrange = true;
			}
			else
			{
				int			attno;

				/*
				 * Compare scan keys with summary values stored for the range.
				 * If scan keys are matched, the page range must be added to
				 * the bitmap.  We initially assume the range needs to be
				 * added; in particular this serves the case where there are
				 * no keys.
				 */
				addrange = true;
				for (attno = 1; attno <= bdesc->bd_tupdesc->natts; attno++)
				{
					BrinValues *bval;
					Datum		add;
					Oid			collation;

					/*
					 * skip attributes without any scan keys (both regular and
					 * IS [NOT] NULL)
					 */
					if (nkeys[attno - 1] == 0 && nnullkeys[attno - 1] == 0)
						continue;

					bval = &dtup->bt_columns[attno - 1];

					/*
					 * First check if there are any IS [NOT] NULL scan keys,
					 * and if we're violating them. In that case we can
					 * terminate early, without invoking the support function.
					 *
					 * As there may be more keys, we can only determine
					 * mismatch within this loop.
					 */
					if (bdesc->bd_info[attno - 1]->oi_regular_nulls &&
						!check_null_keys(bval, nullkeys[attno - 1],
										 nnullkeys[attno - 1]))
					{
						/*
						 * If any of the IS [NOT] NULL keys failed, the page
						 * range as a whole can't pass. So terminate the loop.
						 */
						addrange = false;
						break;
					}

					/*
					 * So either there are no IS [NOT] NULL keys, or all
					 * passed. If there are no regular scan keys, we're done -
					 * the page range matches. If there are regular keys, but
					 * the page range is marked as 'all nulls' it can't
					 * possibly pass (we're assuming the operators are
					 * strict).
					 */

					/* No regular scan keys - page range as a whole passes. */
					if (!nkeys[attno - 1])
						continue;

					Assert((nkeys[attno - 1] > 0) &&
						   (nkeys[attno - 1] <= scan->numberOfKeys));

					/* If it is all nulls, it cannot possibly be consistent. */
					if (bval->bv_allnulls)
					{
						addrange = false;
						break;
					}

					/*
					 * Collation from the first key (has to be the same for
					 * all keys for the same attribute).
					 */
					collation = keys[attno - 1][0]->sk_collation;

					/*
					 * Check whether the scan key is consistent with the page
					 * range values; if so, have the pages in the range added
					 * to the output bitmap.
					 *
					 * The opclass may or may not support processing of
					 * multiple scan keys. We can determine that based on the
					 * number of arguments - functions with extra parameter
					 * (number of scan keys) do support this, otherwise we
					 * have to simply pass the scan keys one by one.
					 */
					if (consistentFn[attno - 1].fn_nargs >= 4)
					{
						/* Check all keys at once */
						add = FunctionCall4Coll(&consistentFn[attno - 1],
												collation,
												PointerGetDatum(bdesc),
												PointerGetDatum(bval),
												PointerGetDatum(keys[attno - 1]),
												Int32GetDatum(nkeys[attno - 1]));
						addrange = DatumGetBool(add);
					}
					else
					{
						/*
						 * Check keys one by one
						 *
						 * When there are multiple scan keys, failure to meet
						 * the criteria for a single one of them is enough to
						 * discard the range as a whole, so break out of the
						 * loop as soon as a false return value is obtained.
						 */
						int			keyno;

						for (keyno = 0; keyno < nkeys[attno - 1]; keyno++)
						{
							add = FunctionCall3Coll(&consistentFn[attno - 1],
													keys[attno - 1][keyno]->sk_collation,
													PointerGetDatum(bdesc),
													PointerGetDatum(bval),
													PointerGetDatum(keys[attno - 1][keyno]));
							addrange = DatumGetBool(add);
							if (!addrange)
								break;
						}
					}
				}
			}
		}

		/* add the pages in the range to the output bitmap, if needed */
		if (addrange)
		{
			BlockNumber pageno;

			for (pageno = heapBlk;
				 pageno <= Min(nblocks, heapBlk + opaque->bo_pagesPerRange) - 1;
				 pageno++)
			{
				MemoryContextSwitchTo(oldcxt);
				tbm_add_page(tbm, pageno);
				totalpages++;
				MemoryContextSwitchTo(perRangeCxt);
			}
		}
	}

	MemoryContextSwitchTo(oldcxt);
	MemoryContextDelete(perRangeCxt);

	if (buf != InvalidBuffer)
		ReleaseBuffer(buf);

	/*
	 * XXX We have an approximation of the number of *pages* that our scan
	 * returns, but we don't have a precise idea of the number of heap tuples
	 * involved.
	 */
	return totalpages * 10;
}

/*
 * Re-initialize state for a BRIN index scan
 */
void
brinrescan(IndexScanDesc scan, ScanKey scankey, int nscankeys,
		   ScanKey orderbys, int norderbys)
{
	/*
	 * Other index AMs preprocess the scan keys at this point, or sometime
	 * early during the scan; this lets them optimize by removing redundant
	 * keys, or doing early returns when they are impossible to satisfy; see
	 * _bt_preprocess_keys for an example.  Something like that could be added
	 * here someday, too.
	 */

	if (scankey && scan->numberOfKeys > 0)
		memmove(scan->keyData, scankey,
				scan->numberOfKeys * sizeof(ScanKeyData));
}

/*
 * Close down a BRIN index scan
 */
void
brinendscan(IndexScanDesc scan)
{
	BrinOpaque *opaque = (BrinOpaque *) scan->opaque;

	brinRevmapTerminate(opaque->bo_rmAccess);
	brin_free_desc(opaque->bo_bdesc);
	pfree(opaque);
}

/*
 * Per-heap-tuple callback for table_index_build_scan.
 *
 * Note we don't worry about the page range at the end of the table here; it is
 * present in the build state struct after we're called the last time, but not
 * inserted into the index.  Caller must ensure to do so, if appropriate.
 */
static void
brinbuildCallback(Relation index,
				  ItemPointer tid,
				  Datum *values,
				  bool *isnull,
				  bool tupleIsAlive,
				  void *brstate)
{
	BrinBuildState *state = (BrinBuildState *) brstate;
	BlockNumber thisblock;

	thisblock = ItemPointerGetBlockNumber(tid);

	/*
	 * If we're in a block that belongs to a future range, summarize what
	 * we've got and start afresh.  Note the scan might have skipped many
	 * pages, if they were devoid of live tuples; make sure to insert index
	 * tuples for those too.
	 */
	while (thisblock > state->bs_currRangeStart + state->bs_pagesPerRange - 1)
	{

		BRIN_elog((DEBUG2,
				   "brinbuildCallback: completed a range: %u--%u",
				   state->bs_currRangeStart,
				   state->bs_currRangeStart + state->bs_pagesPerRange));

		/* create the index tuple and insert it */
		form_and_insert_tuple(state);

		/* set state to correspond to the next range */
		state->bs_currRangeStart += state->bs_pagesPerRange;

		/* re-initialize state for it */
		brin_memtuple_initialize(state->bs_dtuple, state->bs_bdesc);
	}

	/* Accumulate the current tuple into the running state */
	(void) add_values_to_range(index, state->bs_bdesc, state->bs_dtuple,
							   values, isnull);
}

/*
 * brinbuild() -- build a new BRIN index.
 */
IndexBuildResult *
brinbuild(Relation heap, Relation index, IndexInfo *indexInfo)
{
	IndexBuildResult *result;
	double		reltuples;
	double		idxtuples;
	BrinRevmap *revmap;
	BrinBuildState *state;
	Buffer		meta;
	BlockNumber pagesPerRange;

	/*
	 * We expect to be called exactly once for any index relation.
	 */
	if (RelationGetNumberOfBlocks(index) != 0)
		elog(ERROR, "index \"%s\" already contains data",
			 RelationGetRelationName(index));

	/*
	 * Critical section not required, because on error the creation of the
	 * whole relation will be rolled back.
	 */

	meta = ReadBuffer(index, P_NEW);
	Assert(BufferGetBlockNumber(meta) == BRIN_METAPAGE_BLKNO);
	LockBuffer(meta, BUFFER_LOCK_EXCLUSIVE);

	brin_metapage_init(BufferGetPage(meta), BrinGetPagesPerRange(index),
					   BRIN_CURRENT_VERSION);
	MarkBufferDirty(meta);

	if (RelationNeedsWAL(index))
	{
		xl_brin_createidx xlrec;
		XLogRecPtr	recptr;
		Page		page;

		xlrec.version = BRIN_CURRENT_VERSION;
		xlrec.pagesPerRange = BrinGetPagesPerRange(index);

		XLogBeginInsert();
		XLogRegisterData((char *) &xlrec, SizeOfBrinCreateIdx);
		XLogRegisterBuffer(0, meta, REGBUF_WILL_INIT | REGBUF_STANDARD);

		recptr = XLogInsert(RM_BRIN_ID, XLOG_BRIN_CREATE_INDEX);

		page = BufferGetPage(meta);
		PageSetLSN(page, recptr);
	}

	UnlockReleaseBuffer(meta);

	/*
	 * Initialize our state, including the deformed tuple state.
	 */
	revmap = brinRevmapInitialize(index, &pagesPerRange, NULL);
	state = initialize_brin_buildstate(index, revmap, pagesPerRange);

	/*
	 * Now scan the relation.  No syncscan allowed here because we want the
	 * heap blocks in physical order.
	 */
	reltuples = table_index_build_scan(heap, index, indexInfo, false, true,
									   brinbuildCallback, (void *) state, NULL);

	/* process the final batch */
	form_and_insert_tuple(state);

	/* release resources */
	idxtuples = state->bs_numtuples;
	brinRevmapTerminate(state->bs_rmAccess);
	terminate_brin_buildstate(state);

	/*
	 * Return statistics
	 */
	result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));

	result->heap_tuples = reltuples;
	result->index_tuples = idxtuples;

	return result;
}

void
brinbuildempty(Relation index)
{
	Buffer		metabuf;

	/* An empty BRIN index has a metapage only. */
	metabuf =
		ReadBufferExtended(index, INIT_FORKNUM, P_NEW, RBM_NORMAL, NULL);
	LockBuffer(metabuf, BUFFER_LOCK_EXCLUSIVE);

	/* Initialize and xlog metabuffer. */
	START_CRIT_SECTION();
	brin_metapage_init(BufferGetPage(metabuf), BrinGetPagesPerRange(index),
					   BRIN_CURRENT_VERSION);
	MarkBufferDirty(metabuf);
	log_newpage_buffer(metabuf, true);
	END_CRIT_SECTION();

	UnlockReleaseBuffer(metabuf);
}

/*
 * brinbulkdelete
 *		Since there are no per-heap-tuple index tuples in BRIN indexes,
 *		there's not a lot we can do here.
 *
 * XXX we could mark item tuples as "dirty" (when a minimum or maximum heap
 * tuple is deleted), meaning the need to re-run summarization on the affected
 * range.  Would need to add an extra flag in brintuples for that.
 */
IndexBulkDeleteResult *
brinbulkdelete(IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
			   IndexBulkDeleteCallback callback, void *callback_state)
{
	/* allocate stats if first time through, else re-use existing struct */
	if (stats == NULL)
		stats = (IndexBulkDeleteResult *) palloc0(sizeof(IndexBulkDeleteResult));

	return stats;
}

/*
 * This routine is in charge of "vacuuming" a BRIN index: we just summarize
 * ranges that are currently unsummarized.
 */
IndexBulkDeleteResult *
brinvacuumcleanup(IndexVacuumInfo *info, IndexBulkDeleteResult *stats)
{
	Relation	heapRel;

	/* No-op in ANALYZE ONLY mode */
	if (info->analyze_only)
		return stats;

	if (!stats)
		stats = (IndexBulkDeleteResult *) palloc0(sizeof(IndexBulkDeleteResult));
	stats->num_pages = RelationGetNumberOfBlocks(info->index);
	/* rest of stats is initialized by zeroing */

	heapRel = table_open(IndexGetRelation(RelationGetRelid(info->index), false),
						 AccessShareLock);

	brin_vacuum_scan(info->index, info->strategy);

	brinsummarize(info->index, heapRel, BRIN_ALL_BLOCKRANGES, false,
				  &stats->num_index_tuples, &stats->num_index_tuples);

	table_close(heapRel, AccessShareLock);

	return stats;
}

/*
 * reloptions processor for BRIN indexes
 */
bytea *
brinoptions(Datum reloptions, bool validate)
{
	static const relopt_parse_elt tab[] = {
		{"pages_per_range", RELOPT_TYPE_INT, offsetof(BrinOptions, pagesPerRange)},
		{"autosummarize", RELOPT_TYPE_BOOL, offsetof(BrinOptions, autosummarize)}
	};

	return (bytea *) build_reloptions(reloptions, validate,
									  RELOPT_KIND_BRIN,
									  sizeof(BrinOptions),
									  tab, lengthof(tab));
}

/*
 * SQL-callable function to scan through an index and summarize all ranges
 * that are not currently summarized.
 */
Datum
brin_summarize_new_values(PG_FUNCTION_ARGS)
{
	Datum		relation = PG_GETARG_DATUM(0);

	return DirectFunctionCall2(brin_summarize_range,
							   relation,
							   Int64GetDatum((int64) BRIN_ALL_BLOCKRANGES));
}

/*
 * SQL-callable function to summarize the indicated page range, if not already
 * summarized.  If the second argument is BRIN_ALL_BLOCKRANGES, all
 * unsummarized ranges are summarized.
 */
Datum
brin_summarize_range(PG_FUNCTION_ARGS)
{
	Oid			indexoid = PG_GETARG_OID(0);
	int64		heapBlk64 = PG_GETARG_INT64(1);
	BlockNumber heapBlk;
	Oid			heapoid;
	Relation	indexRel;
	Relation	heapRel;
	Oid			save_userid;
	int			save_sec_context;
	int			save_nestlevel;
	double		numSummarized = 0;

	if (RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("recovery is in progress"),
				 errhint("BRIN control functions cannot be executed during recovery.")));

	if (heapBlk64 > BRIN_ALL_BLOCKRANGES || heapBlk64 < 0)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("block number out of range: %lld",
						(long long) heapBlk64)));
	heapBlk = (BlockNumber) heapBlk64;

	/*
	 * We must lock table before index to avoid deadlocks.  However, if the
	 * passed indexoid isn't an index then IndexGetRelation() will fail.
	 * Rather than emitting a not-very-helpful error message, postpone
	 * complaining, expecting that the is-it-an-index test below will fail.
	 */
	heapoid = IndexGetRelation(indexoid, true);
	if (OidIsValid(heapoid))
	{
		heapRel = table_open(heapoid, ShareUpdateExclusiveLock);

		/*
		 * Autovacuum calls us.  For its benefit, switch to the table owner's
		 * userid, so that any index functions are run as that user.  Also
		 * lock down security-restricted operations and arrange to make GUC
		 * variable changes local to this command.  This is harmless, albeit
		 * unnecessary, when called from SQL, because we fail shortly if the
		 * user does not own the index.
		 */
		GetUserIdAndSecContext(&save_userid, &save_sec_context);
		SetUserIdAndSecContext(heapRel->rd_rel->relowner,
							   save_sec_context | SECURITY_RESTRICTED_OPERATION);
		save_nestlevel = NewGUCNestLevel();
	}
	else
	{
		heapRel = NULL;
		/* Set these just to suppress "uninitialized variable" warnings */
		save_userid = InvalidOid;
		save_sec_context = -1;
		save_nestlevel = -1;
	}

	indexRel = index_open(indexoid, ShareUpdateExclusiveLock);

	/* Must be a BRIN index */
	if (indexRel->rd_rel->relkind != RELKIND_INDEX ||
		indexRel->rd_rel->relam != BRIN_AM_OID)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not a BRIN index",
						RelationGetRelationName(indexRel))));

	/* User must own the index (comparable to privileges needed for VACUUM) */
	if (heapRel != NULL && !pg_class_ownercheck(indexoid, save_userid))
		aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_INDEX,
					   RelationGetRelationName(indexRel));

	/*
	 * Since we did the IndexGetRelation call above without any lock, it's
	 * barely possible that a race against an index drop/recreation could have
	 * netted us the wrong table.  Recheck.
	 */
	if (heapRel == NULL || heapoid != IndexGetRelation(indexoid, false))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_TABLE),
				 errmsg("could not open parent table of index \"%s\"",
						RelationGetRelationName(indexRel))));

	/* OK, do it */
	brinsummarize(indexRel, heapRel, heapBlk, true, &numSummarized, NULL);

	/* Roll back any GUC changes executed by index functions */
	AtEOXact_GUC(false, save_nestlevel);

	/* Restore userid and security context */
	SetUserIdAndSecContext(save_userid, save_sec_context);

	relation_close(indexRel, ShareUpdateExclusiveLock);
	relation_close(heapRel, ShareUpdateExclusiveLock);

	PG_RETURN_INT32((int32) numSummarized);
}

/*
 * SQL-callable interface to mark a range as no longer summarized
 */
Datum
brin_desummarize_range(PG_FUNCTION_ARGS)
{
	Oid			indexoid = PG_GETARG_OID(0);
	int64		heapBlk64 = PG_GETARG_INT64(1);
	BlockNumber heapBlk;
	Oid			heapoid;
	Relation	heapRel;
	Relation	indexRel;
	bool		done;

	if (RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("recovery is in progress"),
				 errhint("BRIN control functions cannot be executed during recovery.")));

	if (heapBlk64 > MaxBlockNumber || heapBlk64 < 0)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("block number out of range: %lld",
						(long long) heapBlk64)));
	heapBlk = (BlockNumber) heapBlk64;

	/*
	 * We must lock table before index to avoid deadlocks.  However, if the
	 * passed indexoid isn't an index then IndexGetRelation() will fail.
	 * Rather than emitting a not-very-helpful error message, postpone
	 * complaining, expecting that the is-it-an-index test below will fail.
	 *
	 * Unlike brin_summarize_range(), autovacuum never calls this.  Hence, we
	 * don't switch userid.
	 */
	heapoid = IndexGetRelation(indexoid, true);
	if (OidIsValid(heapoid))
		heapRel = table_open(heapoid, ShareUpdateExclusiveLock);
	else
		heapRel = NULL;

	indexRel = index_open(indexoid, ShareUpdateExclusiveLock);

	/* Must be a BRIN index */
	if (indexRel->rd_rel->relkind != RELKIND_INDEX ||
		indexRel->rd_rel->relam != BRIN_AM_OID)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not a BRIN index",
						RelationGetRelationName(indexRel))));

	/* User must own the index (comparable to privileges needed for VACUUM) */
	if (!pg_class_ownercheck(indexoid, GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_INDEX,
					   RelationGetRelationName(indexRel));

	/*
	 * Since we did the IndexGetRelation call above without any lock, it's
	 * barely possible that a race against an index drop/recreation could have
	 * netted us the wrong table.  Recheck.
	 */
	if (heapRel == NULL || heapoid != IndexGetRelation(indexoid, false))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_TABLE),
				 errmsg("could not open parent table of index \"%s\"",
						RelationGetRelationName(indexRel))));

	/* the revmap does the hard work */
	do
	{
		done = brinRevmapDesummarizeRange(indexRel, heapBlk);
	}
	while (!done);

	relation_close(indexRel, ShareUpdateExclusiveLock);
	relation_close(heapRel, ShareUpdateExclusiveLock);

	PG_RETURN_VOID();
}

/*
 * Build a BrinDesc used to create or scan a BRIN index
 */
BrinDesc *
brin_build_desc(Relation rel)
{
	BrinOpcInfo **opcinfo;
	BrinDesc   *bdesc;
	TupleDesc	tupdesc;
	int			totalstored = 0;
	int			keyno;
	long		totalsize;
	MemoryContext cxt;
	MemoryContext oldcxt;

	cxt = AllocSetContextCreate(GetCurrentMemoryContext(),
								"brin desc cxt",
								ALLOCSET_SMALL_SIZES);
	oldcxt = MemoryContextSwitchTo(cxt);
	tupdesc = RelationGetDescr(rel);

	/*
	 * Obtain BrinOpcInfo for each indexed column.  While at it, accumulate
	 * the number of columns stored, since the number is opclass-defined.
	 */
	opcinfo = (BrinOpcInfo **) palloc(sizeof(BrinOpcInfo *) * tupdesc->natts);
	for (keyno = 0; keyno < tupdesc->natts; keyno++)
	{
		FmgrInfo   *opcInfoFn;
		Form_pg_attribute attr = TupleDescAttr(tupdesc, keyno);

		opcInfoFn = index_getprocinfo(rel, keyno + 1, BRIN_PROCNUM_OPCINFO);

		opcinfo[keyno] = (BrinOpcInfo *)
			DatumGetPointer(FunctionCall1(opcInfoFn, attr->atttypid));
		totalstored += opcinfo[keyno]->oi_nstored;
	}

	/* Allocate our result struct and fill it in */
	totalsize = offsetof(BrinDesc, bd_info) +
		sizeof(BrinOpcInfo *) * tupdesc->natts;

	bdesc = palloc(totalsize);
	bdesc->bd_context = cxt;
	bdesc->bd_index = rel;
	bdesc->bd_tupdesc = tupdesc;
	bdesc->bd_disktdesc = NULL; /* generated lazily */
	bdesc->bd_totalstored = totalstored;

	for (keyno = 0; keyno < tupdesc->natts; keyno++)
		bdesc->bd_info[keyno] = opcinfo[keyno];
	pfree(opcinfo);

	MemoryContextSwitchTo(oldcxt);

	return bdesc;
}

void
brin_free_desc(BrinDesc *bdesc)
{
	/* make sure the tupdesc is still valid */
	Assert(bdesc->bd_tupdesc->tdrefcount >= 1);
	/* no need for retail pfree */
	MemoryContextDelete(bdesc->bd_context);
}

/*
 * Fetch index's statistical data into *stats
 */
void
brinGetStats(Relation index, BrinStatsData *stats)
{
	Buffer		metabuffer;
	Page		metapage;
	BrinMetaPageData *metadata;

	metabuffer = ReadBuffer(index, BRIN_METAPAGE_BLKNO);
	LockBuffer(metabuffer, BUFFER_LOCK_SHARE);
	metapage = BufferGetPage(metabuffer);
	metadata = (BrinMetaPageData *) PageGetContents(metapage);

	stats->pagesPerRange = metadata->pagesPerRange;
	stats->revmapNumPages = metadata->lastRevmapPage - 1;

	UnlockReleaseBuffer(metabuffer);
}

/*
 * Initialize a BrinBuildState appropriate to create tuples on the given index.
 */
static BrinBuildState *
initialize_brin_buildstate(Relation idxRel, BrinRevmap *revmap,
						   BlockNumber pagesPerRange)
{
	BrinBuildState *state;

	state = palloc(sizeof(BrinBuildState));

	state->bs_irel = idxRel;
	state->bs_numtuples = 0;
	state->bs_currentInsertBuf = InvalidBuffer;
	state->bs_pagesPerRange = pagesPerRange;
	state->bs_currRangeStart = 0;
	state->bs_rmAccess = revmap;
	state->bs_bdesc = brin_build_desc(idxRel);
	state->bs_dtuple = brin_new_memtuple(state->bs_bdesc);

	return state;
}

/*
 * Release resources associated with a BrinBuildState.
 */
static void
terminate_brin_buildstate(BrinBuildState *state)
{
	/*
	 * Release the last index buffer used.  We might as well ensure that
	 * whatever free space remains in that page is available in FSM, too.
	 */
	if (!BufferIsInvalid(state->bs_currentInsertBuf))
	{
		Page		page;
		Size		freespace;
		BlockNumber blk;

		page = BufferGetPage(state->bs_currentInsertBuf);
		freespace = PageGetFreeSpace(page);
		blk = BufferGetBlockNumber(state->bs_currentInsertBuf);
		ReleaseBuffer(state->bs_currentInsertBuf);
		RecordPageWithFreeSpace(state->bs_irel, blk, freespace);
		FreeSpaceMapVacuumRange(state->bs_irel, blk, blk + 1);
	}

	brin_free_desc(state->bs_bdesc);
	pfree(state->bs_dtuple);
	pfree(state);
}

/*
 * On the given BRIN index, summarize the heap page range that corresponds
 * to the heap block number given.
 *
 * This routine can run in parallel with insertions into the heap.  To avoid
 * missing those values from the summary tuple, we first insert a placeholder
 * index tuple into the index, then execute the heap scan; transactions
 * concurrent with the scan update the placeholder tuple.  After the scan, we
 * union the placeholder tuple with the one computed by this routine.  The
 * update of the index value happens in a loop, so that if somebody updates
 * the placeholder tuple after we read it, we detect the case and try again.
 * This ensures that the concurrently inserted tuples are not lost.
 *
 * A further corner case is this routine being asked to summarize the partial
 * range at the end of the table.  heapNumBlocks is the (possibly outdated)
 * table size; if we notice that the requested range lies beyond that size,
 * we re-compute the table size after inserting the placeholder tuple, to
 * avoid missing pages that were appended recently.
 */
static void
summarize_range(IndexInfo *indexInfo, BrinBuildState *state, Relation heapRel,
				BlockNumber heapBlk, BlockNumber heapNumBlks)
{
	Buffer		phbuf;
	BrinTuple  *phtup;
	Size		phsz;
	OffsetNumber offset;
	BlockNumber scanNumBlks;

	/*
	 * Insert the placeholder tuple
	 */
	phbuf = InvalidBuffer;
	phtup = brin_form_placeholder_tuple(state->bs_bdesc, heapBlk, &phsz);
	offset = brin_doinsert(state->bs_irel, state->bs_pagesPerRange,
						   state->bs_rmAccess, &phbuf,
						   heapBlk, phtup, phsz);

	/*
	 * Compute range end.  We hold ShareUpdateExclusive lock on table, so it
	 * cannot shrink concurrently (but it can grow).
	 */
	Assert(heapBlk % state->bs_pagesPerRange == 0);
	if (heapBlk + state->bs_pagesPerRange > heapNumBlks)
	{
		/*
		 * If we're asked to scan what we believe to be the final range on the
		 * table (i.e. a range that might be partial) we need to recompute our
		 * idea of what the latest page is after inserting the placeholder
		 * tuple.  Anyone that grows the table later will update the
		 * placeholder tuple, so it doesn't matter that we won't scan these
		 * pages ourselves.  Careful: the table might have been extended
		 * beyond the current range, so clamp our result.
		 *
		 * Fortunately, this should occur infrequently.
		 */
		scanNumBlks = Min(RelationGetNumberOfBlocks(heapRel) - heapBlk,
						  state->bs_pagesPerRange);
	}
	else
	{
		/* Easy case: range is known to be complete */
		scanNumBlks = state->bs_pagesPerRange;
	}

	/*
	 * Execute the partial heap scan covering the heap blocks in the specified
	 * page range, summarizing the heap tuples in it.  This scan stops just
	 * short of brinbuildCallback creating the new index entry.
	 *
	 * Note that it is critical we use the "any visible" mode of
	 * table_index_build_range_scan here: otherwise, we would miss tuples
	 * inserted by transactions that are still in progress, among other corner
	 * cases.
	 */
	state->bs_currRangeStart = heapBlk;
	table_index_build_range_scan(heapRel, state->bs_irel, indexInfo, false, true, false,
								 heapBlk, scanNumBlks,
								 brinbuildCallback, (void *) state, NULL,
								 NULL /* bfinfo */ , NULL /* bfresult */ );

	/*
	 * Now we update the values obtained by the scan with the placeholder
	 * tuple.  We do this in a loop which only terminates if we're able to
	 * update the placeholder tuple successfully; if we are not, this means
	 * somebody else modified the placeholder tuple after we read it.
	 */
	for (;;)
	{
		BrinTuple  *newtup;
		Size		newsize;
		bool		didupdate;
		bool		samepage;

		CHECK_FOR_INTERRUPTS();

		/*
		 * Update the summary tuple and try to update.
		 */
		newtup = brin_form_tuple(state->bs_bdesc,
								 heapBlk, state->bs_dtuple, &newsize);
		samepage = brin_can_do_samepage_update(phbuf, phsz, newsize);
		didupdate =
			brin_doupdate(state->bs_irel, state->bs_pagesPerRange,
						  state->bs_rmAccess, heapBlk, phbuf, offset,
						  phtup, phsz, newtup, newsize, samepage);
		brin_free_tuple(phtup);
		brin_free_tuple(newtup);

		/* If the update succeeded, we're done. */
		if (didupdate)
			break;

		/*
		 * If the update didn't work, it might be because somebody updated the
		 * placeholder tuple concurrently.  Extract the new version, union it
		 * with the values we have from the scan, and start over.  (There are
		 * other reasons for the update to fail, but it's simple to treat them
		 * the same.)
		 */
		phtup = brinGetTupleForHeapBlock(state->bs_rmAccess, heapBlk, &phbuf,
										 &offset, &phsz, BUFFER_LOCK_SHARE,
										 NULL);
		/* the placeholder tuple must exist */
		if (phtup == NULL)
			elog(ERROR, "missing placeholder tuple");
		phtup = brin_copy_tuple(phtup, phsz, NULL, NULL);
		LockBuffer(phbuf, BUFFER_LOCK_UNLOCK);

		/* merge it into the tuple from the heap scan */
		union_tuples(state->bs_bdesc, state->bs_dtuple, phtup);
	}

	ReleaseBuffer(phbuf);
}

/*
 * Summarize page ranges that are not already summarized.  If pageRange is
 * BRIN_ALL_BLOCKRANGES then the whole table is scanned; otherwise, only the
 * page range containing the given heap page number is scanned.
 * If include_partial is true, then the partial range at the end of the table
 * is summarized, otherwise not.
 *
 * For each new index tuple inserted, *numSummarized (if not NULL) is
 * incremented; for each existing tuple, *numExisting (if not NULL) is
 * incremented.
 */
static void
brinsummarize(Relation index, Relation heapRel, BlockNumber pageRange,
			  bool include_partial, double *numSummarized, double *numExisting)
{
	BrinRevmap *revmap;
	BrinBuildState *state = NULL;
	IndexInfo  *indexInfo = NULL;
	BlockNumber heapNumBlocks;
	BlockNumber pagesPerRange;
	Buffer		buf;
	BlockNumber startBlk;

	revmap = brinRevmapInitialize(index, &pagesPerRange, NULL);

	/* determine range of pages to process */
	heapNumBlocks = RelationGetNumberOfBlocks(heapRel);
	if (pageRange == BRIN_ALL_BLOCKRANGES)
		startBlk = 0;
	else
	{
		startBlk = (pageRange / pagesPerRange) * pagesPerRange;
		heapNumBlocks = Min(heapNumBlocks, startBlk + pagesPerRange);
	}
	if (startBlk > heapNumBlocks)
	{
		/* Nothing to do if start point is beyond end of table */
		brinRevmapTerminate(revmap);
		return;
	}

	/*
	 * Scan the revmap to find unsummarized items.
	 */
	buf = InvalidBuffer;
	for (; startBlk < heapNumBlocks; startBlk += pagesPerRange)
	{
		BrinTuple  *tup;
		OffsetNumber off;

		/*
		 * Unless requested to summarize even a partial range, go away now if
		 * we think the next range is partial.  Caller would pass true when it
		 * is typically run once bulk data loading is done
		 * (brin_summarize_new_values), and false when it is typically the
		 * result of arbitrarily-scheduled maintenance command (vacuuming).
		 */
		if (!include_partial &&
			(startBlk + pagesPerRange > heapNumBlocks))
			break;

		CHECK_FOR_INTERRUPTS();

		tup = brinGetTupleForHeapBlock(revmap, startBlk, &buf, &off, NULL,
									   BUFFER_LOCK_SHARE, NULL);
		if (tup == NULL)
		{
			/* no revmap entry for this heap range. Summarize it. */
			if (state == NULL)
			{
				/* first time through */
				Assert(!indexInfo);
				state = initialize_brin_buildstate(index, revmap,
												   pagesPerRange);
				indexInfo = BuildIndexInfo(index);
			}
			summarize_range(indexInfo, state, heapRel, startBlk, heapNumBlocks);

			/* and re-initialize state for the next range */
			brin_memtuple_initialize(state->bs_dtuple, state->bs_bdesc);

			if (numSummarized)
				*numSummarized += 1.0;
		}
		else
		{
			if (numExisting)
				*numExisting += 1.0;
			LockBuffer(buf, BUFFER_LOCK_UNLOCK);
		}
	}

	if (BufferIsValid(buf))
		ReleaseBuffer(buf);

	/* free resources */
	brinRevmapTerminate(revmap);
	if (state)
	{
		terminate_brin_buildstate(state);
		pfree(indexInfo);
	}
}

/*
 * Given a deformed tuple in the build state, convert it into the on-disk
 * format and insert it into the index, making the revmap point to it.
 */
static void
form_and_insert_tuple(BrinBuildState *state)
{
	BrinTuple  *tup;
	Size		size;

	tup = brin_form_tuple(state->bs_bdesc, state->bs_currRangeStart,
						  state->bs_dtuple, &size);
	brin_doinsert(state->bs_irel, state->bs_pagesPerRange, state->bs_rmAccess,
				  &state->bs_currentInsertBuf, state->bs_currRangeStart,
				  tup, size);
	state->bs_numtuples++;

	pfree(tup);
}

/*
 * Given two deformed tuples, adjust the first one so that it's consistent
 * with the summary values in both.
 */
static void
union_tuples(BrinDesc *bdesc, BrinMemTuple *a, BrinTuple *b)
{
	int			keyno;
	BrinMemTuple *db;
	MemoryContext cxt;
	MemoryContext oldcxt;

	/* Use our own memory context to avoid retail pfree */
	cxt = AllocSetContextCreate(GetCurrentMemoryContext(),
								"brin union",
								ALLOCSET_DEFAULT_SIZES);
	oldcxt = MemoryContextSwitchTo(cxt);
	db = brin_deform_tuple(bdesc, b, NULL);
	MemoryContextSwitchTo(oldcxt);

	for (keyno = 0; keyno < bdesc->bd_tupdesc->natts; keyno++)
	{
		FmgrInfo   *unionFn;
		BrinValues *col_a = &a->bt_columns[keyno];
		BrinValues *col_b = &db->bt_columns[keyno];
		BrinOpcInfo *opcinfo = bdesc->bd_info[keyno];

		if (opcinfo->oi_regular_nulls)
		{
			/* Adjust "hasnulls". */
			if (!col_a->bv_hasnulls && col_b->bv_hasnulls)
				col_a->bv_hasnulls = true;

			/* If there are no values in B, there's nothing left to do. */
			if (col_b->bv_allnulls)
				continue;

			/*
			 * Adjust "allnulls".  If A doesn't have values, just copy the
			 * values from B into A, and we're done.  We cannot run the
			 * operators in this case, because values in A might contain
			 * garbage.  Note we already established that B contains values.
			 */
			if (col_a->bv_allnulls)
			{
				int			i;

				col_a->bv_allnulls = false;

				for (i = 0; i < opcinfo->oi_nstored; i++)
					col_a->bv_values[i] =
						datumCopy(col_b->bv_values[i],
								  opcinfo->oi_typcache[i]->typbyval,
								  opcinfo->oi_typcache[i]->typlen);

				continue;
			}
		}

		unionFn = index_getprocinfo(bdesc->bd_index, keyno + 1,
									BRIN_PROCNUM_UNION);
		FunctionCall3Coll(unionFn,
						  bdesc->bd_index->rd_indcollation[keyno],
						  PointerGetDatum(bdesc),
						  PointerGetDatum(col_a),
						  PointerGetDatum(col_b));
	}

	MemoryContextDelete(cxt);
}

/*
 * brin_vacuum_scan
 *		Do a complete scan of the index during VACUUM.
 *
 * This routine scans the complete index looking for uncatalogued index pages,
 * i.e. those that might have been lost due to a crash after index extension
 * and such.
 */
static void
brin_vacuum_scan(Relation idxrel, BufferAccessStrategy strategy)
{
	BlockNumber nblocks;
	BlockNumber blkno;

	/*
	 * Scan the index in physical order, and clean up any possible mess in
	 * each page.
	 */
	nblocks = RelationGetNumberOfBlocks(idxrel);
	for (blkno = 0; blkno < nblocks; blkno++)
	{
		Buffer		buf;

		CHECK_FOR_INTERRUPTS();

		buf = ReadBufferExtended(idxrel, MAIN_FORKNUM, blkno,
								 RBM_NORMAL, strategy);

		brin_page_cleanup(idxrel, buf);

		ReleaseBuffer(buf);
	}

	/*
	 * Update all upper pages in the index's FSM, as well.  This ensures not
	 * only that we propagate leaf-page FSM updates made by brin_page_cleanup,
	 * but also that any pre-existing damage or out-of-dateness is repaired.
	 */
	FreeSpaceMapVacuum(idxrel);
}

static bool
add_values_to_range(Relation idxRel, BrinDesc *bdesc, BrinMemTuple *dtup,
					Datum *values, bool *nulls)
{
	int			keyno;
	bool		modified = false;

	/*
	 * Compare the key values of the new tuple to the stored index values; our
	 * deformed tuple will get updated if the new tuple doesn't fit the
	 * original range (note this means we can't break out of the loop early).
	 * Make a note of whether this happens, so that we know to insert the
	 * modified tuple later.
	 */
	for (keyno = 0; keyno < bdesc->bd_tupdesc->natts; keyno++)
	{
		Datum		result;
		BrinValues *bval;
		FmgrInfo   *addValue;

		bval = &dtup->bt_columns[keyno];

		if (bdesc->bd_info[keyno]->oi_regular_nulls && nulls[keyno])
		{
			/*
			 * If the new value is null, we record that we saw it if it's the
			 * first one; otherwise, there's nothing to do.
			 */
			if (!bval->bv_hasnulls)
			{
				bval->bv_hasnulls = true;
				modified = true;
			}

			continue;
		}

		addValue = index_getprocinfo(idxRel, keyno + 1,
									 BRIN_PROCNUM_ADDVALUE);
		result = FunctionCall4Coll(addValue,
								   idxRel->rd_indcollation[keyno],
								   PointerGetDatum(bdesc),
								   PointerGetDatum(bval),
								   values[keyno],
								   nulls[keyno]);
		/* if that returned true, we need to insert the updated tuple */
		modified |= DatumGetBool(result);
	}

	return modified;
}

static bool
check_null_keys(BrinValues *bval, ScanKey *nullkeys, int nnullkeys)
{
	int			keyno;

	/*
	 * First check if there are any IS [NOT] NULL scan keys, and if we're
	 * violating them.
	 */
	for (keyno = 0; keyno < nnullkeys; keyno++)
	{
		ScanKey		key = nullkeys[keyno];

		Assert(key->sk_attno == bval->bv_attno);

		/* Handle only IS NULL/IS NOT NULL tests */
		if (!(key->sk_flags & SK_ISNULL))
			continue;

		if (key->sk_flags & SK_SEARCHNULL)
		{
			/* IS NULL scan key, but range has no NULLs */
			if (!bval->bv_allnulls && !bval->bv_hasnulls)
				return false;
		}
		else if (key->sk_flags & SK_SEARCHNOTNULL)
		{
			/*
			 * For IS NOT NULL, we can only skip ranges that are known to have
			 * only nulls.
			 */
			if (bval->bv_allnulls)
				return false;
		}
		else
		{
			/*
			 * Neither IS NULL nor IS NOT NULL was used; assume all indexable
			 * operators are strict and thus return false with NULL value in
			 * the scan key.
			 */
			return false;
		}
	}

	return true;
}
