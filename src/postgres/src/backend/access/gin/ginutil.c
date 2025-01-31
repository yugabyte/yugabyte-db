/*-------------------------------------------------------------------------
 *
 * ginutil.c
 *	  Utility routines for the Postgres inverted index access method.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *			src/backend/access/gin/ginutil.c
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/gin_private.h"
#include "access/ginxlog.h"
#include "access/reloptions.h"
#include "access/xloginsert.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_type.h"
#include "commands/vacuum.h"
#include "miscadmin.h"
#include "storage/indexfsm.h"
#include "storage/lmgr.h"
#include "storage/predicate.h"
#include "utils/builtins.h"
#include "utils/index_selfuncs.h"
#include "utils/typcache.h"

/* YB includes. */
#include "pg_yb_utils.h"
#include "utils/lsyscache.h"


/*
 * GIN handler function: return IndexAmRoutine with access method parameters
 * and callbacks.
 */
Datum
ginhandler(PG_FUNCTION_ARGS)
{
	IndexAmRoutine *amroutine = makeNode(IndexAmRoutine);

	amroutine->amstrategies = 0;
	amroutine->amsupport = GINNProcs;
	amroutine->amoptsprocnum = GIN_OPTIONS_PROC;
	amroutine->amcanorder = false;
	amroutine->amcanorderbyop = false;
	amroutine->amcanbackward = false;
	amroutine->amcanunique = false;
	amroutine->amcanmulticol = true;
	amroutine->amoptionalkey = true;
	amroutine->amsearcharray = false;
	amroutine->amsearchnulls = false;
	amroutine->amstorage = true;
	amroutine->amclusterable = false;
	amroutine->ampredlocks = true;
	amroutine->amcanparallel = false;
	amroutine->amcaninclude = false;
	amroutine->amusemaintenanceworkmem = true;
	amroutine->amparallelvacuumoptions =
		VACUUM_OPTION_PARALLEL_BULKDEL | VACUUM_OPTION_PARALLEL_CLEANUP;
	amroutine->amkeytype = InvalidOid;

	amroutine->ambuild = ginbuild;
	amroutine->ambuildempty = ginbuildempty;
	amroutine->aminsert = gininsert;
	amroutine->ambulkdelete = ginbulkdelete;
	amroutine->amvacuumcleanup = ginvacuumcleanup;
	amroutine->amcanreturn = NULL;
	amroutine->amcostestimate = gincostestimate;
	amroutine->amoptions = ginoptions;
	amroutine->amproperty = NULL;
	amroutine->ambuildphasename = NULL;
	amroutine->amvalidate = ginvalidate;
	amroutine->amadjustmembers = ginadjustmembers;
	amroutine->ambeginscan = ginbeginscan;
	amroutine->amrescan = ginrescan;
	amroutine->amgettuple = NULL;
	amroutine->amgetbitmap = gingetbitmap;
	amroutine->amendscan = ginendscan;
	amroutine->ammarkpos = NULL;
	amroutine->amrestrpos = NULL;
	amroutine->amestimateparallelscan = NULL;
	amroutine->aminitparallelscan = NULL;
	amroutine->amparallelrescan = NULL;

	PG_RETURN_POINTER(amroutine);
}

/*
 * initGinState: fill in an empty GinState struct to describe the index
 *
 * Note: assorted subsidiary data is allocated in the GetCurrentMemoryContext().
 */
void
initGinState(GinState *state, Relation index)
{
	TupleDesc	origTupdesc = RelationGetDescr(index);
	int			i;

	MemSet(state, 0, sizeof(GinState));

	state->index = index;
	state->oneCol = (origTupdesc->natts == 1);
	state->origTupdesc = origTupdesc;

	for (i = 0; i < origTupdesc->natts; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(origTupdesc, i);

		if (state->oneCol)
			state->tupdesc[i] = state->origTupdesc;
		else
		{
			state->tupdesc[i] = CreateTemplateTupleDesc(2);

			TupleDescInitEntry(state->tupdesc[i], (AttrNumber) 1, NULL,
							   INT2OID, -1, 0);
			TupleDescInitEntry(state->tupdesc[i], (AttrNumber) 2, NULL,
							   attr->atttypid,
							   attr->atttypmod,
							   attr->attndims);
			TupleDescInitEntryCollation(state->tupdesc[i], (AttrNumber) 2,
										attr->attcollation);
		}

		/*
		 * If the compare proc isn't specified in the opclass definition, look
		 * up the index key type's default btree comparator.
		 */
		if (index_getprocid(index, i + 1, GIN_COMPARE_PROC) != InvalidOid)
		{
			fmgr_info_copy(&(state->compareFn[i]),
						   index_getprocinfo(index, i + 1, GIN_COMPARE_PROC),
						   GetCurrentMemoryContext());
		}
		else
		{
			TypeCacheEntry *typentry;

			typentry = lookup_type_cache(attr->atttypid,
										 TYPECACHE_CMP_PROC_FINFO);
			if (!OidIsValid(typentry->cmp_proc_finfo.fn_oid))
				ereport(ERROR,
						(errcode(ERRCODE_UNDEFINED_FUNCTION),
						 errmsg("could not identify a comparison function for type %s",
								format_type_be(attr->atttypid))));
			fmgr_info_copy(&(state->compareFn[i]),
						   &(typentry->cmp_proc_finfo),
						   GetCurrentMemoryContext());
		}

		/* Opclass must always provide extract procs */
		fmgr_info_copy(&(state->extractValueFn[i]),
					   index_getprocinfo(index, i + 1, GIN_EXTRACTVALUE_PROC),
					   GetCurrentMemoryContext());
		fmgr_info_copy(&(state->extractQueryFn[i]),
					   index_getprocinfo(index, i + 1, GIN_EXTRACTQUERY_PROC),
					   GetCurrentMemoryContext());

		/*
		 * Check opclass capability to do tri-state or binary logic consistent
		 * check.
		 */
		if (index_getprocid(index, i + 1, GIN_TRICONSISTENT_PROC) != InvalidOid)
		{
			fmgr_info_copy(&(state->triConsistentFn[i]),
						   index_getprocinfo(index, i + 1, GIN_TRICONSISTENT_PROC),
						   GetCurrentMemoryContext());
		}

		if (index_getprocid(index, i + 1, GIN_CONSISTENT_PROC) != InvalidOid)
		{
			fmgr_info_copy(&(state->consistentFn[i]),
						   index_getprocinfo(index, i + 1, GIN_CONSISTENT_PROC),
						   GetCurrentMemoryContext());
		}

		if (state->consistentFn[i].fn_oid == InvalidOid &&
			state->triConsistentFn[i].fn_oid == InvalidOid)
		{
			elog(ERROR, "missing GIN support function (%d or %d) for attribute %d of index \"%s\"",
				 GIN_CONSISTENT_PROC, GIN_TRICONSISTENT_PROC,
				 i + 1, RelationGetRelationName(index));
		}

		/*
		 * Check opclass capability to do partial match.
		 */
		if (index_getprocid(index, i + 1, GIN_COMPARE_PARTIAL_PROC) != InvalidOid)
		{
			fmgr_info_copy(&(state->comparePartialFn[i]),
						   index_getprocinfo(index, i + 1, GIN_COMPARE_PARTIAL_PROC),
						   GetCurrentMemoryContext());
			state->canPartialMatch[i] = true;
		}
		else
		{
			state->canPartialMatch[i] = false;
		}

		/*
		 * If the index column has a specified collation, we should honor that
		 * while doing comparisons.  However, we may have a collatable storage
		 * type for a noncollatable indexed data type (for instance, hstore
		 * uses text index entries).  If there's no index collation then
		 * specify default collation in case the support functions need
		 * collation.  This is harmless if the support functions don't care
		 * about collation, so we just do it unconditionally.  (We could
		 * alternatively call get_typcollation, but that seems like expensive
		 * overkill --- there aren't going to be any cases where a GIN storage
		 * type has a nondefault collation.)
		 *
		 * For Yugabyte, _do_ use get_typcollation (through type_is_collatable)
		 * because we do care that collation is invalid rather than default for
		 * later assertion purposes.  Assuming the index is not created with
		 * explicit collation, expect the following:
		 * - int array: indexed type is _int and index type is int.  Since both
		 *   aren't collatable, set to InvalidOid (else if).
		 * - text array: indexed type is _text and index type is text.  Since
		 *   _text is collatable, set to collation of the indexed column (if).
		 * - yb_hstore: indexed type is hstore and index type is text.  Since
		 *   only text is collatable, set to default collation (else).
		 * - yb_pg_trgm: indexed type is text and index type is int.  Since
		 *   text is collatable, set to collation of the indexed column (if).
		 *   This valid collation is needed later for regex, for example.
		 *   TODO(#9595): fix interaction between this and the "if != STRING"
		 *   Assert in YBGetCollationInfo.
		 */
		if (OidIsValid(index->rd_indcollation[i]))
			state->supportCollation[i] = index->rd_indcollation[i];
		else if (IsYBRelation(index) && !type_is_collatable(attr->atttypid))
			state->supportCollation[i] = InvalidOid;
		else
			state->supportCollation[i] = DEFAULT_COLLATION_OID;
	}
}

/*
 * Extract attribute (column) number of stored entry from GIN tuple
 */
OffsetNumber
gintuple_get_attrnum(GinState *ginstate, IndexTuple tuple)
{
	OffsetNumber colN;

	if (ginstate->oneCol)
	{
		/* column number is not stored explicitly */
		colN = FirstOffsetNumber;
	}
	else
	{
		Datum		res;
		bool		isnull;

		/*
		 * First attribute is always int16, so we can safely use any tuple
		 * descriptor to obtain first attribute of tuple
		 */
		res = index_getattr(tuple, FirstOffsetNumber, ginstate->tupdesc[0],
							&isnull);
		Assert(!isnull);

		colN = DatumGetUInt16(res);
		Assert(colN >= FirstOffsetNumber && colN <= ginstate->origTupdesc->natts);
	}

	return colN;
}

/*
 * Extract stored datum (and possible null category) from GIN tuple
 */
Datum
gintuple_get_key(GinState *ginstate, IndexTuple tuple,
				 GinNullCategory *category)
{
	Datum		res;
	bool		isnull;

	if (ginstate->oneCol)
	{
		/*
		 * Single column index doesn't store attribute numbers in tuples
		 */
		res = index_getattr(tuple, FirstOffsetNumber, ginstate->origTupdesc,
							&isnull);
	}
	else
	{
		/*
		 * Since the datum type depends on which index column it's from, we
		 * must be careful to use the right tuple descriptor here.
		 */
		OffsetNumber colN = gintuple_get_attrnum(ginstate, tuple);

		res = index_getattr(tuple, OffsetNumberNext(FirstOffsetNumber),
							ginstate->tupdesc[colN - 1],
							&isnull);
	}

	if (isnull)
		*category = GinGetNullCategory(tuple, ginstate);
	else
		*category = GIN_CAT_NORM_KEY;

	return res;
}

/*
 * Allocate a new page (either by recycling, or by extending the index file)
 * The returned buffer is already pinned and exclusive-locked
 * Caller is responsible for initializing the page by calling GinInitBuffer
 */
Buffer
GinNewBuffer(Relation index)
{
	Buffer		buffer;
	bool		needLock;

	/* First, try to get a page from FSM */
	for (;;)
	{
		BlockNumber blkno = GetFreeIndexPage(index);

		if (blkno == InvalidBlockNumber)
			break;

		buffer = ReadBuffer(index, blkno);

		/*
		 * We have to guard against the possibility that someone else already
		 * recycled this page; the buffer may be locked if so.
		 */
		if (ConditionalLockBuffer(buffer))
		{
			if (GinPageIsRecyclable(BufferGetPage(buffer)))
				return buffer;	/* OK to use */

			LockBuffer(buffer, GIN_UNLOCK);
		}

		/* Can't use it, so release buffer and try again */
		ReleaseBuffer(buffer);
	}

	/* Must extend the file */
	needLock = !RELATION_IS_LOCAL(index);
	if (needLock)
		LockRelationForExtension(index, ExclusiveLock);

	buffer = ReadBuffer(index, P_NEW);
	LockBuffer(buffer, GIN_EXCLUSIVE);

	if (needLock)
		UnlockRelationForExtension(index, ExclusiveLock);

	return buffer;
}

void
GinInitPage(Page page, uint32 f, Size pageSize)
{
	GinPageOpaque opaque;

	PageInit(page, pageSize, sizeof(GinPageOpaqueData));

	opaque = GinPageGetOpaque(page);
	opaque->flags = f;
	opaque->rightlink = InvalidBlockNumber;
}

void
GinInitBuffer(Buffer b, uint32 f)
{
	GinInitPage(BufferGetPage(b), f, BufferGetPageSize(b));
}

void
GinInitMetabuffer(Buffer b)
{
	GinMetaPageData *metadata;
	Page		page = BufferGetPage(b);

	GinInitPage(page, GIN_META, BufferGetPageSize(b));

	metadata = GinPageGetMeta(page);

	metadata->head = metadata->tail = InvalidBlockNumber;
	metadata->tailFreeSize = 0;
	metadata->nPendingPages = 0;
	metadata->nPendingHeapTuples = 0;
	metadata->nTotalPages = 0;
	metadata->nEntryPages = 0;
	metadata->nDataPages = 0;
	metadata->nEntries = 0;
	metadata->ginVersion = GIN_CURRENT_VERSION;

	/*
	 * Set pd_lower just past the end of the metadata.  This is essential,
	 * because without doing so, metadata will be lost if xlog.c compresses
	 * the page.
	 */
	((PageHeader) page)->pd_lower =
		((char *) metadata + sizeof(GinMetaPageData)) - (char *) page;
}

/*
 * Compare two keys of the same index column
 */
int
ginCompareEntries(GinState *ginstate, OffsetNumber attnum,
				  Datum a, GinNullCategory categorya,
				  Datum b, GinNullCategory categoryb)
{
	/* if not of same null category, sort by that first */
	if (categorya != categoryb)
		return (categorya < categoryb) ? -1 : 1;

	/* all null items in same category are equal */
	if (categorya != GIN_CAT_NORM_KEY)
		return 0;

	/* both not null, so safe to call the compareFn */
	return DatumGetInt32(FunctionCall2Coll(&ginstate->compareFn[attnum - 1],
										   ginstate->supportCollation[attnum - 1],
										   a, b));
}

/*
 * Compare two keys of possibly different index columns
 */
int
ginCompareAttEntries(GinState *ginstate,
					 OffsetNumber attnuma, Datum a, GinNullCategory categorya,
					 OffsetNumber attnumb, Datum b, GinNullCategory categoryb)
{
	/* attribute number is the first sort key */
	if (attnuma != attnumb)
		return (attnuma < attnumb) ? -1 : 1;

	return ginCompareEntries(ginstate, attnuma, a, categorya, b, categoryb);
}


/*
 * Support for sorting key datums in ginExtractEntries
 *
 * Note: we only have to worry about null and not-null keys here;
 * ginExtractEntries never generates more than one placeholder null,
 * so it doesn't have to sort those.
 */
typedef struct
{
	Datum		datum;
	bool		isnull;
} keyEntryData;

typedef struct
{
	FmgrInfo   *cmpDatumFunc;
	Oid			collation;
	bool		haveDups;
} cmpEntriesArg;

static int
cmpEntries(const void *a, const void *b, void *arg)
{
	const keyEntryData *aa = (const keyEntryData *) a;
	const keyEntryData *bb = (const keyEntryData *) b;
	cmpEntriesArg *data = (cmpEntriesArg *) arg;
	int			res;

	if (aa->isnull)
	{
		if (bb->isnull)
			res = 0;			/* NULL "=" NULL */
		else
			res = 1;			/* NULL ">" not-NULL */
	}
	else if (bb->isnull)
		res = -1;				/* not-NULL "<" NULL */
	else
		res = DatumGetInt32(FunctionCall2Coll(data->cmpDatumFunc,
											  data->collation,
											  aa->datum, bb->datum));

	/*
	 * Detect if we have any duplicates.  If there are equal keys, qsort must
	 * compare them at some point, else it wouldn't know whether one should go
	 * before or after the other.
	 */
	if (res == 0)
		data->haveDups = true;

	return res;
}


/*
 * Extract the index key values from an indexable item
 *
 * The resulting key values are sorted, and any duplicates are removed.
 * This avoids generating redundant index entries.
 */
Datum *
ginExtractEntries(GinState *ginstate, OffsetNumber attnum,
				  Datum value, bool isNull,
				  int32 *nentries, GinNullCategory **categories)
{
	Datum	   *entries;
	bool	   *nullFlags;
	int32		i;

	/*
	 * We don't call the extractValueFn on a null item.  Instead generate a
	 * placeholder.
	 */
	if (isNull)
	{
		*nentries = 1;
		entries = (Datum *) palloc(sizeof(Datum));
		entries[0] = (Datum) 0;
		*categories = (GinNullCategory *) palloc(sizeof(GinNullCategory));
		(*categories)[0] = GIN_CAT_NULL_ITEM;
		return entries;
	}

	/* OK, call the opclass's extractValueFn */
	nullFlags = NULL;			/* in case extractValue doesn't set it */
	entries = (Datum *)
		DatumGetPointer(FunctionCall3Coll(&ginstate->extractValueFn[attnum - 1],
										  ginstate->supportCollation[attnum - 1],
										  value,
										  PointerGetDatum(nentries),
										  PointerGetDatum(&nullFlags)));

	/*
	 * Generate a placeholder if the item contained no keys.
	 */
	if (entries == NULL || *nentries <= 0)
	{
		*nentries = 1;
		entries = (Datum *) palloc(sizeof(Datum));
		entries[0] = (Datum) 0;
		*categories = (GinNullCategory *) palloc(sizeof(GinNullCategory));
		(*categories)[0] = GIN_CAT_EMPTY_ITEM;
		return entries;
	}

	/*
	 * If the extractValueFn didn't create a nullFlags array, create one,
	 * assuming that everything's non-null.
	 */
	if (nullFlags == NULL)
		nullFlags = (bool *) palloc0(*nentries * sizeof(bool));

	/*
	 * If there's more than one key, sort and unique-ify.
	 *
	 * XXX Using qsort here is notationally painful, and the overhead is
	 * pretty bad too.  For small numbers of keys it'd likely be better to use
	 * a simple insertion sort.
	 */
	if (*nentries > 1)
	{
		keyEntryData *keydata;
		cmpEntriesArg arg;

		keydata = (keyEntryData *) palloc(*nentries * sizeof(keyEntryData));
		for (i = 0; i < *nentries; i++)
		{
			keydata[i].datum = entries[i];
			keydata[i].isnull = nullFlags[i];
		}

		arg.cmpDatumFunc = &ginstate->compareFn[attnum - 1];
		arg.collation = ginstate->supportCollation[attnum - 1];
		arg.haveDups = false;
		qsort_arg(keydata, *nentries, sizeof(keyEntryData),
				  cmpEntries, (void *) &arg);

		if (arg.haveDups)
		{
			/* there are duplicates, must get rid of 'em */
			int32		j;

			entries[0] = keydata[0].datum;
			nullFlags[0] = keydata[0].isnull;
			j = 1;
			for (i = 1; i < *nentries; i++)
			{
				if (cmpEntries(&keydata[i - 1], &keydata[i], &arg) != 0)
				{
					entries[j] = keydata[i].datum;
					nullFlags[j] = keydata[i].isnull;
					j++;
				}
			}
			*nentries = j;
		}
		else
		{
			/* easy, no duplicates */
			for (i = 0; i < *nentries; i++)
			{
				entries[i] = keydata[i].datum;
				nullFlags[i] = keydata[i].isnull;
			}
		}

		pfree(keydata);
	}

	/*
	 * Create GinNullCategory representation from nullFlags.
	 */
	*categories = (GinNullCategory *) palloc0(*nentries * sizeof(GinNullCategory));
	for (i = 0; i < *nentries; i++)
		(*categories)[i] = (nullFlags[i] ? GIN_CAT_NULL_KEY : GIN_CAT_NORM_KEY);

	return entries;
}

bytea *
ginoptions(Datum reloptions, bool validate)
{
	static const relopt_parse_elt tab[] = {
		{"fastupdate", RELOPT_TYPE_BOOL, offsetof(GinOptions, useFastUpdate)},
		{"gin_pending_list_limit", RELOPT_TYPE_INT, offsetof(GinOptions,
															 pendingListCleanupSize)}
	};

	return (bytea *) build_reloptions(reloptions, validate,
									  RELOPT_KIND_GIN,
									  sizeof(GinOptions),
									  tab, lengthof(tab));
}

/*
 * Fetch index's statistical data into *stats
 *
 * Note: in the result, nPendingPages can be trusted to be up-to-date,
 * as can ginVersion; but the other fields are as of the last VACUUM.
 */
void
ginGetStats(Relation index, GinStatsData *stats)
{
	Buffer		metabuffer;
	Page		metapage;
	GinMetaPageData *metadata;

	if (IsYBBackedRelation(index))
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("cannot get stats on Yugabyte-backed gin index")));

	metabuffer = ReadBuffer(index, GIN_METAPAGE_BLKNO);
	LockBuffer(metabuffer, GIN_SHARE);
	metapage = BufferGetPage(metabuffer);
	metadata = GinPageGetMeta(metapage);

	stats->nPendingPages = metadata->nPendingPages;
	stats->nTotalPages = metadata->nTotalPages;
	stats->nEntryPages = metadata->nEntryPages;
	stats->nDataPages = metadata->nDataPages;
	stats->nEntries = metadata->nEntries;
	stats->ginVersion = metadata->ginVersion;

	UnlockReleaseBuffer(metabuffer);
}

/*
 * Write the given statistics to the index's metapage
 *
 * Note: nPendingPages and ginVersion are *not* copied over
 */
void
ginUpdateStats(Relation index, const GinStatsData *stats, bool is_build)
{
	Buffer		metabuffer;
	Page		metapage;
	GinMetaPageData *metadata;

	metabuffer = ReadBuffer(index, GIN_METAPAGE_BLKNO);
	LockBuffer(metabuffer, GIN_EXCLUSIVE);
	metapage = BufferGetPage(metabuffer);
	metadata = GinPageGetMeta(metapage);

	START_CRIT_SECTION();

	metadata->nTotalPages = stats->nTotalPages;
	metadata->nEntryPages = stats->nEntryPages;
	metadata->nDataPages = stats->nDataPages;
	metadata->nEntries = stats->nEntries;

	/*
	 * Set pd_lower just past the end of the metadata.  This is essential,
	 * because without doing so, metadata will be lost if xlog.c compresses
	 * the page.  (We must do this here because pre-v11 versions of PG did not
	 * set the metapage's pd_lower correctly, so a pg_upgraded index might
	 * contain the wrong value.)
	 */
	((PageHeader) metapage)->pd_lower =
		((char *) metadata + sizeof(GinMetaPageData)) - (char *) metapage;

	MarkBufferDirty(metabuffer);

	if (RelationNeedsWAL(index) && !is_build)
	{
		XLogRecPtr	recptr;
		ginxlogUpdateMeta data;

		data.node = index->rd_node;
		data.ntuples = 0;
		data.newRightlink = data.prevTail = InvalidBlockNumber;
		memcpy(&data.metadata, metadata, sizeof(GinMetaPageData));

		XLogBeginInsert();
		XLogRegisterData((char *) &data, sizeof(ginxlogUpdateMeta));
		XLogRegisterBuffer(0, metabuffer, REGBUF_WILL_INIT | REGBUF_STANDARD);

		recptr = XLogInsert(RM_GIN_ID, XLOG_GIN_UPDATE_META_PAGE);
		PageSetLSN(metapage, recptr);
	}

	UnlockReleaseBuffer(metabuffer);

	END_CRIT_SECTION();
}
