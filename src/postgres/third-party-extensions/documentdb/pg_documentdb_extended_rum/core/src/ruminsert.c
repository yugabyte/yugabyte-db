/*-------------------------------------------------------------------------
 *
 * ruminsert.c
 *	  insert routines for the postgres inverted index access method.
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 2015-2022, Postgres Professional
 * Portions Copyright (c) 1996-2016, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * Note: In order to support parallel sort, portions of this file are taken from
 * gininsert.c in postgres
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/generic_xlog.h"
#if PG_VERSION_NUM >= 120000
#include "access/tableam.h"
#endif
#include "storage/predicate.h"
#include "catalog/index.h"
#include "miscadmin.h"
#include "utils/backend_progress.h"
#include "utils/datum.h"
#include "commands/progress.h"
#include "access/parallel.h"
#include "access/tableam.h"
#include "tcop/tcopprot.h"
#include "utils/backend_status.h"
#include "access/table.h"
#include "catalog/pg_collation.h"
#include "utils/wait_event.h"

#include "pg_documentdb_rum.h"
#include "rumbuild_tuplesort.h"

extern bool RumEnableParallelIndexBuild;
extern int RumParallelIndexWorkersOverride;

extern PGDLLEXPORT void documentdb_rum_parallel_build_main(dsm_segment *seg,
														   shm_toc *toc);

/* Magic numbers for parallel state sharing */
#define PARALLEL_KEY_RUM_SHARED UINT64CONST(0xB000000000000001)
#define PARALLEL_KEY_TUPLESORT UINT64CONST(0xB000000000000002)
#define PARALLEL_KEY_QUERY_TEXT UINT64CONST(0xB000000000000003)
#define PARALLEL_KEY_WAL_USAGE UINT64CONST(0xB000000000000004)
#define PARALLEL_KEY_BUFFER_USAGE UINT64CONST(0xB000000000000005)

typedef struct RumBuildShared
{
	/*
	 * These fields are not modified during the build.  They primarily exist
	 * for the benefit of worker processes that need to create state
	 * corresponding to that used by the leader.
	 */
	Oid heaprelid;
	Oid indexrelid;
	bool isconcurrent;
	int scantuplesortstates;

	/*
	 * workersdonecv is used to monitor the progress of workers.  All parallel
	 * participants must indicate that they are done before leader can use
	 * results built by the workers (and before leader can write the data into
	 * the index).
	 */
	ConditionVariable workersdonecv;

	/*
	 * mutex protects all following fields
	 *
	 * These fields contain status information of interest to RUM index builds
	 * that must work just the same when an index is built in parallel.
	 */
	slock_t mutex;

	/*
	 * Mutable state that is maintained by workers, and reported back to
	 * leader at end of the scans.
	 *
	 * nparticipantsdone is number of worker processes finished.
	 *
	 * reltuples is the total number of input heap tuples.
	 *
	 * indtuples is the total number of tuples that made it into the index.
	 */
	int nparticipantsdone;
	double reltuples;
	double indtuples;

	/*
	 * ParallelTableScanDescData data follows. Can't directly embed here, as
	 * implementations of the parallel table scan desc interface might need
	 * stronger alignment.
	 */
} RumBuildShared;


#define ParallelTableScanFromRumBuildShared(shared) \
	(ParallelTableScanDesc) ((char *) (shared) + BUFFERALIGN(sizeof(RumBuildShared)))


typedef struct RumLeader
{
	/* parallel context itself */
	ParallelContext *pcxt;

	/*
	 * nparticipanttuplesorts is the exact number of worker processes
	 * successfully launched, plus one leader process if it participates as a
	 * worker (only DISABLE_LEADER_PARTICIPATION builds avoid leader
	 * participating as a worker).
	 */
	int nparticipanttuplesorts;

	/*
	 * Leader process convenience pointers to shared state (leader avoids TOC
	 * lookups).
	 *
	 * RumBuildShared is the shared state for entire build.  sharedsort is the
	 * shared, tuplesort-managed state passed to each process tuplesort.
	 * snapshot is the snapshot used by the scan iff an MVCC snapshot is
	 * required.
	 */
	RumBuildShared *rumshared;
	Sharedsort *sharedsort;
	Snapshot snapshot;
	WalUsage *walusage;
	BufferUsage *bufferusage;
} RumLeader;


typedef struct
{
	RumState rumstate;
	double indtuples;
	RumStatsData buildStats;
	MemoryContext tmpCtx;
	MemoryContext funcCtx;
	BuildAccumulator accum;

	/* Parallel build information */
	int work_mem;
	ItemPointerData tid;

	/*
	 * bs_leader is only present when a parallel index build is performed, and
	 * only in the leader process.
	 */
	RumLeader *bs_leader;
	int bs_worker_id;

	/* used to pass information from workers to leader */
	double bs_numtuples;
	double bs_reltuples;

	/*
	 * The sortstate is used by workers (including the leader). It has to be
	 * part of the build state, because that's the only thing passed to the
	 * build callback etc.
	 */
	Tuplesortstate *bs_sortstate;

	/*
	 * The sortstate used only within a single worker for the first merge pass
	 * happenning there. In principle it doesn't need to be part of the build
	 * state and we could pass it around directly, but it's more convenient
	 * this way. And it's part of the build state, after all.
	 */
	Tuplesortstate *bs_worker_sort;
}   RumBuildState;

typedef struct RumBuffer
{
	OffsetNumber attnum;
	RumNullCategory category;
	Datum key;                  /* 0 if no key (and keylen == 0) */
	Size keylen;                /* number of bytes (not typlen) */

	/* type info */
	int16 typlen;
	bool typbyval;

	/* Number of TIDs to collect before attempt to write some out. */
	int maxitems;

	/* array of TID values */
	int nitems;
	int nfrozen;
	SortSupport ssup;           /* for sorting/comparing keys */
	RumItem *items;
} RumBuffer;

static void _rum_end_parallel(RumLeader *rumleader, RumBuildState *state);

#if PG_VERSION_NUM >= 120000
#define IndexBuildHeapScan(A, B, C, D, E, F) \
	table_index_build_scan(A, B, C, D, true, E, F, NULL)
#elif PG_VERSION_NUM >= 110000
#define IndexBuildHeapScan(A, B, C, D, E, F) \
	IndexBuildHeapScan(A, B, C, D, E, F, NULL)
#endif

static IndexBuildResult * rumbuild_serial(Relation heap, Relation index, struct
										  IndexInfo *indexInfo,
										  RumBuildState *buildstate);

static IndexBuildResult * rumbuild_parallel(Relation heap, Relation index, struct
											IndexInfo *indexInfo,
											RumBuildState *buildstate,
											bool canBuildParallel);

/*
 * Creates new posting tree with one page, containing the given TIDs.
 * Returns the page number (which will be the root of this posting tree).
 *
 * items[] must be in sorted order with no duplicates.
 */
static BlockNumber
createPostingTree(RumState *rumstate, OffsetNumber attnum, Relation index,
				  RumItem *items, uint32 nitems)
{
	BlockNumber blkno;
	Buffer buffer = RumNewBuffer(index);
	Page page;
	int i;
	Pointer ptr;
	ItemPointerData prev_iptr = { { 0, 0 }, 0 };
	GenericXLogState *state = NULL;

	if (rumstate->isBuild)
	{
		page = BufferGetPage(buffer);
		START_CRIT_SECTION();
	}
	else
	{
		state = GenericXLogStart(index);
		page = GenericXLogRegisterBuffer(state, buffer, GENERIC_XLOG_FULL_IMAGE);
	}
	RumInitPage(page, RUM_DATA | RUM_LEAF, BufferGetPageSize(buffer));

	blkno = BufferGetBlockNumber(buffer);

	RumDataPageMaxOff(page) = nitems;
	ptr = RumDataPageGetData(page);
	for (i = 0; i < nitems; i++)
	{
		if (i > 0)
		{
			prev_iptr = items[i - 1].iptr;
		}
		ptr = rumPlaceToDataPageLeaf(ptr, attnum, &items[i],
									 &prev_iptr, rumstate);
	}
	Assert(RumDataPageFreeSpacePre(page, ptr) >= 0);
	updateItemIndexes(page, attnum, rumstate);

	if (rumstate->isBuild)
	{
		MarkBufferDirty(buffer);
	}
	else
	{
		GenericXLogFinish(state);
	}

	UnlockReleaseBuffer(buffer);

	if (rumstate->isBuild)
	{
		END_CRIT_SECTION();
	}

	return blkno;
}


/*
 * Form a tuple for entry tree.
 *
 * If the tuple would be too big to be stored, function throws a suitable
 * error if errorTooBig is true, or returns NULL if errorTooBig is false.
 *
 * See src/backend/access/gin/README for a description of the index tuple
 * format that is being built here.  We build on the assumption that we
 * are making a leaf-level key entry containing a posting list of nipd items.
 * If the caller is actually trying to make a posting-tree entry, non-leaf
 * entry, or pending-list entry, it should pass nipd = 0 and then overwrite
 * the t_tid fields as necessary.  In any case, items can be NULL to skip
 * copying any itempointers into the posting list; the caller is responsible
 * for filling the posting list afterwards, if items = NULL and nipd > 0.
 */
static IndexTuple
RumFormTuple(RumState *rumstate,
			 OffsetNumber attnum, Datum key, RumNullCategory category,
			 RumItem *items, uint32 nipd, bool errorTooBig)
{
	Datum datums[3];
	bool isnull[3];
	IndexTuple itup;
	uint32 newsize;
	int i;
	ItemPointerData nullItemPointer = { { 0, 0 }, 0 };

	/* Build the basic tuple: optional column number, plus key datum */
	if (rumstate->oneCol)
	{
		datums[0] = key;
		isnull[0] = (category != RUM_CAT_NORM_KEY);
		isnull[1] = true;
	}
	else
	{
		datums[0] = UInt16GetDatum(attnum);
		isnull[0] = false;
		datums[1] = key;
		isnull[1] = (category != RUM_CAT_NORM_KEY);
		isnull[2] = true;
	}

	itup = index_form_tuple(rumstate->tupdesc[attnum - 1], datums, isnull);

	/*
	 * Determine and store offset to the posting list, making sure there is
	 * room for the category byte if needed.
	 *
	 * Note: because index_form_tuple MAXALIGNs the tuple size, there may well
	 * be some wasted pad space.  Is it worth recomputing the data length to
	 * prevent that?  That would also allow us to Assert that the real data
	 * doesn't overlap the RumNullCategory byte, which this code currently
	 * takes on faith.
	 */
	newsize = IndexTupleSize(itup);

	RumSetPostingOffset(itup, newsize);

	RumSetNPosting(itup, nipd);

	/*
	 * Add space needed for posting list, if any.  Then check that the tuple
	 * won't be too big to store.
	 */

	if (nipd > 0)
	{
		newsize = rumCheckPlaceToDataPageLeaf(attnum, &items[0],
											  &nullItemPointer,
											  rumstate, newsize);
		for (i = 1; i < nipd; i++)
		{
			newsize = rumCheckPlaceToDataPageLeaf(attnum, &items[i],
												  &items[i - 1].iptr,
												  rumstate, newsize);
		}
	}


	if (category != RUM_CAT_NORM_KEY)
	{
		Assert(IndexTupleHasNulls(itup));
		newsize = newsize + sizeof(RumNullCategory);
	}
	newsize = MAXALIGN(newsize);

	if (newsize > RumMaxItemSize)
	{
		if (errorTooBig)
		{
			ereport(ERROR,
					(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
					 errmsg("index row size %lu exceeds maximum %lu for index \"%s\"",
							(unsigned long) newsize,
							(unsigned long) RumMaxItemSize,
							RelationGetRelationName(rumstate->index))));
		}
		pfree(itup);
		return NULL;
	}

	/*
	 * Resize tuple if needed
	 */
	if (newsize != IndexTupleSize(itup))
	{
		itup = repalloc(itup, newsize);

		memset((char *) itup + IndexTupleSize(itup),
			   0, newsize - IndexTupleSize(itup));

		/* set new size in tuple header */
		itup->t_info &= ~INDEX_SIZE_MASK;
		itup->t_info |= newsize;
	}

	/*
	 * Copy in the posting list, if provided
	 */
	if (nipd > 0)
	{
		char *ptr = RumGetPosting(itup);

		ptr = rumPlaceToDataPageLeaf(ptr, attnum, &items[0],
									 &nullItemPointer, rumstate);
		for (i = 1; i < nipd; i++)
		{
			ptr = rumPlaceToDataPageLeaf(ptr, attnum, &items[i],
										 &items[i - 1].iptr, rumstate);
		}

		Assert(MAXALIGN((ptr - ((char *) itup)) +
						((category == RUM_CAT_NORM_KEY) ? 0 : sizeof(RumNullCategory))) ==
			   newsize);
	}

	/*
	 * Insert category byte, if needed
	 */
	if (category != RUM_CAT_NORM_KEY)
	{
		Assert(IndexTupleHasNulls(itup));
		RumSetNullCategory(itup, category);
	}

	return itup;
}


/*
 * Adds array of item pointers to tuple's posting list, or
 * creates posting tree and tuple pointing to tree in case
 * of not enough space.  Max size of tuple is defined in
 * RumFormTuple().  Returns a new, modified index tuple.
 * items[] must be in sorted order with no duplicates.
 */
static IndexTuple
addItemPointersToLeafTuple(RumState *rumstate,
						   IndexTuple old, RumItem *items, uint32 nitem,
						   RumStatsData *buildStats)
{
	OffsetNumber attnum;
	Datum key;
	RumNullCategory category;
	IndexTuple res;
	RumItem *newItems,
			*oldItems;
	int oldNPosting,
		newNPosting;

	Assert(!RumIsPostingTree(old));

	attnum = rumtuple_get_attrnum(rumstate, old);
	key = rumtuple_get_key(rumstate, old, &category);

	oldNPosting = RumGetNPosting(old);
	oldItems = (RumItem *) palloc(sizeof(RumItem) * oldNPosting);

	newNPosting = oldNPosting + nitem;
	newItems = (RumItem *) palloc(sizeof(RumItem) * newNPosting);

	rumReadTuple(rumstate, attnum, old, oldItems, false);

	newNPosting = rumMergeRumItems(rumstate, attnum, newItems,
								   items, nitem, oldItems, oldNPosting);


	/* try to build tuple with room for all the items */
	res = RumFormTuple(rumstate, attnum, key, category,
					   newItems, newNPosting, false);

	if (!res)
	{
		/* posting list would be too big, convert to posting tree */
		BlockNumber postingRoot;
		RumPostingTreeScan *gdi;

		/*
		 * Initialize posting tree with the old tuple's posting list.  It's
		 * surely small enough to fit on one posting-tree page, and should
		 * already be in order with no duplicates.
		 */
		postingRoot = createPostingTree(rumstate,
										attnum,
										rumstate->index,
										oldItems,
										oldNPosting);

		/* During index build, count the newly-added data page */
		if (buildStats)
		{
			buildStats->nDataPages++;
		}

		/* Now insert the TIDs-to-be-added into the posting tree */
		gdi = rumPrepareScanPostingTree(rumstate->index, postingRoot, false,
										ForwardScanDirection, attnum, rumstate);
		rumInsertItemPointers(rumstate, attnum, gdi, items, nitem, buildStats);

		pfree(gdi);

		/* And build a new posting-tree-only result tuple */
		res = RumFormTuple(rumstate, attnum, key, category, NULL, 0, true);
		RumSetPostingTree(res, postingRoot);
	}

	return res;
}


/*
 * Build a fresh leaf tuple, either posting-list or posting-tree format
 * depending on whether the given items list will fit.
 * items[] must be in sorted order with no duplicates.
 *
 * This is basically the same logic as in addItemPointersToLeafTuple,
 * but working from slightly different input.
 */
static IndexTuple
buildFreshLeafTuple(RumState *rumstate,
					OffsetNumber attnum, Datum key, RumNullCategory category,
					RumItem *items, uint32 nitem, RumStatsData *buildStats)
{
	IndexTuple res;

	/* try to build tuple with room for all the items */
	res = RumFormTuple(rumstate, attnum, key, category, items, nitem, false);

	if (!res)
	{
		/* posting list would be too big, build posting tree */
		BlockNumber postingRoot;
		ItemPointerData prevIptr = { { 0, 0 }, 0 };
		Size size = 0;
		int itemsCount = 0;

		do {
			size = rumCheckPlaceToDataPageLeaf(attnum, &items[itemsCount],
											   &prevIptr, rumstate, size);
			prevIptr = items[itemsCount].iptr;
			itemsCount++;
		} while (itemsCount < nitem && size < RumDataPageSize);

		if (size >= RumDataPageSize)
		{
			itemsCount--;
		}

		/*
		 * Build posting-tree-only result tuple.  We do this first so as to
		 * fail quickly if the key is too big.
		 */
		res = RumFormTuple(rumstate, attnum, key, category, NULL, 0, true);

		/*
		 * Initialize posting tree with as many TIDs as will fit on the first
		 * page.
		 */
		postingRoot = createPostingTree(rumstate,
										attnum,
										rumstate->index,
										items,
										itemsCount);

		/* During index build, count the newly-added data page */
		if (buildStats)
		{
			buildStats->nDataPages++;
		}

		/* Add any remaining TIDs to the posting tree */
		if (nitem > itemsCount)
		{
			RumPostingTreeScan *gdi;

			gdi = rumPrepareScanPostingTree(rumstate->index, postingRoot, false,
											ForwardScanDirection,
											attnum, rumstate);

			rumInsertItemPointers(rumstate,
								  attnum,
								  gdi,
								  items + itemsCount,
								  nitem - itemsCount,
								  buildStats);

			pfree(gdi);
		}

		/* And save the root link in the result tuple */
		RumSetPostingTree(res, postingRoot);
	}

	return res;
}


/*
 * Insert one or more heap TIDs associated with the given key value.
 * This will either add a single key entry, or enlarge a pre-existing entry.
 *
 * During an index build, buildStats is non-null and the counters
 * it contains should be incremented as needed.
 */
void
rumEntryInsert(RumState *rumstate,
			   OffsetNumber attnum, Datum key, RumNullCategory category,
			   RumItem *items, uint32 nitem,
			   RumStatsData *buildStats)
{
	RumBtreeData btree;
	RumBtreeStack *stack;
	IndexTuple itup;
	Page page;

	/* During index build, count the to-be-inserted entry */
	if (buildStats)
	{
		buildStats->nEntries++;
	}

	rumPrepareEntryScan(&btree, attnum, key, category, rumstate);

	stack = rumFindLeafPage(&btree, NULL);
	page = BufferGetPage(stack->buffer);

	CheckForSerializableConflictIn(btree.index, NULL, stack->buffer);

	if (btree.findItem(&btree, stack))
	{
		/* found pre-existing entry */
		itup = (IndexTuple) PageGetItem(page, PageGetItemId(page, stack->off));

		if (RumIsPostingTree(itup))
		{
			/* add entries to existing posting tree
			 * Note: Posting tree entries are never marked dead.
			 */
			BlockNumber rootPostingTree = RumGetPostingTree(itup);
			RumPostingTreeScan *gdi;

			/* release all stack */
			LockBuffer(stack->buffer, RUM_UNLOCK);
			freeRumBtreeStack(stack);

			/* insert into posting tree */
			gdi = rumPrepareScanPostingTree(rumstate->index, rootPostingTree,
											false, ForwardScanDirection,
											attnum, rumstate);
			rumInsertItemPointers(rumstate, attnum, gdi, items,
								  nitem, buildStats);
			pfree(gdi);

			return;
		}

		/* modify an existing leaf entry */
		itup = addItemPointersToLeafTuple(rumstate, itup,
										  items, nitem, buildStats);

		btree.isDelete = true;
	}
	else
	{
		/* no match, so construct a new leaf entry */
		itup = buildFreshLeafTuple(rumstate, attnum, key, category,
								   items, nitem, buildStats);
	}

	/* Insert the new or modified leaf tuple */
	btree.entry = itup;
	rumInsertValue(rumstate->index, &btree, stack, buildStats);
	pfree(itup);
}


/*
 * Extract index entries for a single indexable item, and add them to the
 * BuildAccumulator's state.
 *
 * This function is used only during initial index creation.
 */
static void
rumHeapTupleBulkInsert(RumBuildState *buildstate, OffsetNumber attnum,
					   Datum value, bool isNull,
					   ItemPointer heapptr,
					   Datum outerAddInfo,
					   bool outerAddInfoIsNull)
{
	Datum *entries;
	RumNullCategory *categories;
	int32 nentries;
	MemoryContext oldCtx;
	Datum *addInfo;
	bool *addInfoIsNull;
	int i;
	Form_pg_attribute attr = buildstate->rumstate.addAttrs[attnum - 1];

	oldCtx = MemoryContextSwitchTo(buildstate->funcCtx);
	entries = rumExtractEntries(buildstate->accum.rumstate, attnum,
								value, isNull,
								&nentries, &categories,
								&addInfo, &addInfoIsNull);

	if (attnum == buildstate->rumstate.attrnAddToColumn)
	{
		addInfo = palloc(sizeof(*addInfo) * nentries);
		addInfoIsNull = palloc(sizeof(*addInfoIsNull) * nentries);

		for (i = 0; i < nentries; i++)
		{
			addInfo[i] = outerAddInfo;
			addInfoIsNull[i] = outerAddInfoIsNull;
		}
	}

	MemoryContextSwitchTo(oldCtx);
	for (i = 0; i < nentries; i++)
	{
		if (!addInfoIsNull[i])
		{
			/* Check existance of additional information attribute in index */
			if (!attr)
			{
				Form_pg_attribute current_attr = RumTupleDescAttr(
					buildstate->rumstate.origTupdesc, attnum - 1);

				elog(ERROR,
					 "additional information attribute \"%s\" is not found in index",
					 NameStr(current_attr->attname));
			}

			addInfo[i] = datumCopy(addInfo[i], attr->attbyval, attr->attlen);
		}
	}

	rumInsertBAEntries(&buildstate->accum, heapptr, attnum,
					   entries, addInfo, addInfoIsNull, categories, nentries);

	buildstate->indtuples += nentries;

	MemoryContextReset(buildstate->funcCtx);
}


static void
rumBuildCallback(Relation index,
#if PG_VERSION_NUM < 130000
				 HeapTuple htup,
#else
				 ItemPointer tid,
#endif
				 Datum *values,
				 bool *isnull, bool tupleIsAlive, void *state)
{
	RumBuildState *buildstate = (RumBuildState *) state;
	MemoryContext oldCtx;
	int i;
	Datum outerAddInfo = (Datum) 0;
	bool outerAddInfoIsNull = true;
#if PG_VERSION_NUM < 130000
	ItemPointer tid = &htup->t_self;
#endif

	if (AttributeNumberIsValid(buildstate->rumstate.attrnAttachColumn))
	{
		outerAddInfo = values[buildstate->rumstate.attrnAttachColumn - 1];
		outerAddInfoIsNull = isnull[buildstate->rumstate.attrnAttachColumn - 1];
	}

	oldCtx = MemoryContextSwitchTo(buildstate->tmpCtx);

	for (i = 0; i < buildstate->rumstate.origTupdesc->natts; i++)
	{
		rumHeapTupleBulkInsert(buildstate, (OffsetNumber) (i + 1),
							   values[i], isnull[i],
							   tid,
							   outerAddInfo, outerAddInfoIsNull);
	}

	/* If we've maxed out our available memory, dump everything to the index */
	if (buildstate->accum.allocatedMemory >= maintenance_work_mem * 1024L)
	{
		RumItem *items;
		Datum key;
		RumNullCategory category;
		uint32 nlist;
		OffsetNumber attnum;

		rumBeginBAScan(&buildstate->accum);
		while ((items = rumGetBAEntry(&buildstate->accum,
									  &attnum, &key, &category, &nlist)) != NULL)
		{
			/* there could be many entries, so be willing to abort here */
			CHECK_FOR_INTERRUPTS();
			rumEntryInsert(&buildstate->rumstate, attnum, key, category,
						   items, nlist, &buildstate->buildStats);
		}

		MemoryContextReset(buildstate->tmpCtx);
		rumInitBA(&buildstate->accum);
	}

	MemoryContextSwitchTo(oldCtx);
}


IndexBuildResult *
rumbuild(Relation heap, Relation index, struct IndexInfo *indexInfo)
{
	bool isParallelIndexCapable = true;
	int i = 0;
	RumBuildState buildstate;
	Buffer RootBuffer,
		   MetaBuffer;

	if (RelationGetNumberOfBlocks(index) != 0)
	{
		elog(ERROR, "index \"%s\" already contains data",
			 RelationGetRelationName(index));
	}

#if PG_VERSION_NUM >= 160000
#else
	isParallelIndexCapable = false;
#endif

	initRumState(&buildstate.rumstate, index);
	buildstate.rumstate.isBuild = true;
	buildstate.indtuples = 0;
	memset(&buildstate.buildStats, 0, sizeof(RumStatsData));

	buildstate.bs_numtuples = 0;
	buildstate.bs_reltuples = 0;
	buildstate.bs_leader = NULL;
	memset(&buildstate.tid, 0, sizeof(ItemPointerData));

	/* initialize the meta page */
	MetaBuffer = RumNewBuffer(index);

	/* initialize the root page */
	RootBuffer = RumNewBuffer(index);

	START_CRIT_SECTION();
	RumInitMetabuffer(NULL, MetaBuffer, buildstate.rumstate.isBuild);
	MarkBufferDirty(MetaBuffer);
	RumInitBuffer(NULL, RootBuffer, RUM_LEAF, buildstate.rumstate.isBuild);
	MarkBufferDirty(RootBuffer);

	UnlockReleaseBuffer(MetaBuffer);
	UnlockReleaseBuffer(RootBuffer);
	END_CRIT_SECTION();

	/* count the root as first entry page */
	buildstate.buildStats.nEntryPages++;

	/*
	 * create a temporary memory context that is reset once for each tuple
	 * inserted into the index
	 */
	buildstate.tmpCtx = RumContextCreate(CurrentMemoryContext,
										 "Rum build temporary context");

	buildstate.funcCtx = RumContextCreate(CurrentMemoryContext,
										  "Rum build temporary context for user-defined function");

	buildstate.accum.rumstate = &buildstate.rumstate;
	rumInitBA(&buildstate.accum);

	/* Scenarios that have addinfo need to skip parallel build */
	for (i = 0; i < INDEX_MAX_KEYS && isParallelIndexCapable; i++)
	{
		if (buildstate.rumstate.addAttrs[i] != NULL)
		{
			isParallelIndexCapable = false;
			break;
		}

		if (buildstate.rumstate.canJoinAddInfo[i])
		{
			isParallelIndexCapable = false;
			break;
		}
	}

	if (buildstate.rumstate.attrnAddToColumn != InvalidAttrNumber)
	{
		isParallelIndexCapable = false;
	}

	/* We only support parallel build when it's sorted via itempointers only */
	if (RumEnableParallelIndexBuild)
	{
		return rumbuild_parallel(heap, index, indexInfo, &buildstate,
								 isParallelIndexCapable);
	}
	else
	{
		return rumbuild_serial(heap, index, indexInfo, &buildstate);
	}
}


static IndexBuildResult *
rumbuild_serial(Relation heap, Relation index, struct IndexInfo *indexInfo,
				RumBuildState *buildstate)
{
	MemoryContext oldCtx;
	IndexBuildResult *result;
	RumItem *items;
	Datum key;
	RumNullCategory category;
	uint32 nlist;
	OffsetNumber attnum;
	BlockNumber blkno;
	double reltuples;

	/*
	 * Do the heap scan.  We disallow sync scan here because dataPlaceToPage
	 * prefers to receive tuples in TID order.
	 */
	reltuples = IndexBuildHeapScan(heap, index, indexInfo, false,
								   rumBuildCallback, (void *) buildstate);

	/* dump remaining entries to the index */
	oldCtx = MemoryContextSwitchTo(buildstate->tmpCtx);
	rumBeginBAScan(&buildstate->accum);
	while ((items = rumGetBAEntry(&buildstate->accum,
								  &attnum, &key, &category, &nlist)) != NULL)
	{
		/* there could be many entries, so be willing to abort here */
		CHECK_FOR_INTERRUPTS();
		rumEntryInsert(&buildstate->rumstate, attnum, key, category,
					   items, nlist, &buildstate->buildStats);
	}
	MemoryContextSwitchTo(oldCtx);

	MemoryContextDelete(buildstate->funcCtx);
	MemoryContextDelete(buildstate->tmpCtx);

	/*
	 * Update metapage stats
	 */
	buildstate->buildStats.nTotalPages = RelationGetNumberOfBlocks(index);
	rumUpdateStats(index, &buildstate->buildStats, buildstate->rumstate.isBuild);

	/*
	 * Write index to xlog
	 */
	for (blkno = 0; blkno < buildstate->buildStats.nTotalPages; blkno++)
	{
		Buffer buffer;
		GenericXLogState *state;

		CHECK_FOR_INTERRUPTS();

		buffer = ReadBuffer(index, blkno);
		LockBuffer(buffer, RUM_EXCLUSIVE);

		state = GenericXLogStart(index);
		GenericXLogRegisterBuffer(state, buffer, GENERIC_XLOG_FULL_IMAGE);
		GenericXLogFinish(state);

		UnlockReleaseBuffer(buffer);
	}

	/*
	 * Return statistics
	 */
	result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));

	result->heap_tuples = reltuples;
	result->index_tuples = buildstate->indtuples;

	return result;
}


typedef struct
{
	dlist_node node;            /* linked list pointers */
	RumPostingList *seg;
} RumSegmentInfo;

static RumTuple *
_rum_build_tuple(OffsetNumber attrnum, unsigned char category,
				 Datum key, int16 typlen, bool typbyval,
				 RumItem *items, uint32 nitems,
				 Size *len)
{
	RumTuple *tuple;
	char *ptr;

	Size tuplen;
	int keylen;

	dlist_mutable_iter iter;
	dlist_head segments;
	int ncompressed;
	Size compresslen;

	/*
	 * Calculate how long is the key value. Only keys with RUM_CAT_NORM_KEY
	 * have actual non-empty key. We include varlena headers and \0 bytes for
	 * strings, to make it easier to access the data in-line.
	 *
	 * For byval types we simply copy the whole Datum. We could store just the
	 * necessary bytes, but this is simpler to work with and not worth the
	 * extra complexity. Moreover we still need to do the MAXALIGN to allow
	 * direct access to items pointers.
	 *
	 * XXX Note that for byval types we store the whole datum, no matter what
	 * the typlen value is.
	 */
	if (category != RUM_CAT_NORM_KEY)
	{
		keylen = 0;
	}
	else if (typbyval)
	{
		keylen = sizeof(Datum);
	}
	else if (typlen > 0)
	{
		keylen = typlen;
	}
	else if (typlen == -1)
	{
		keylen = VARSIZE_ANY(key);
	}
	else if (typlen == -2)
	{
		keylen = strlen(DatumGetPointer(key)) + 1;
	}
	else
	{
		elog(ERROR, "unexpected typlen value (%d)", typlen);
	}

	/* Compress the item reference pointers */
	ncompressed = 0;
	compresslen = 0;
	dlist_init(&segments);

	/* generate compressed segments of TID list chunks */
	while (ncompressed < nitems)
	{
		int cnt;
		RumSegmentInfo *seginfo = palloc(sizeof(RumSegmentInfo));

		seginfo->seg = rumCompressPostingList(&items[ncompressed],
											  (nitems - ncompressed),
											  UINT16_MAX,
											  &cnt);

		ncompressed += cnt;
		compresslen += SizeOfRumPostingList(seginfo->seg);

		dlist_push_tail(&segments, &seginfo->node);
	}

	/*
	 * Determine RUM tuple length with all the data included. Be careful about
	 * alignment, to allow direct access to compressed segments (those require
	 * only SHORTALIGN).
	 */
	tuplen = SHORTALIGN(offsetof(RumTuple, data) + keylen) + compresslen;

	*len = tuplen;

	/*
	 * Allocate space for the whole RUM tuple.
	 *
	 * The palloc0 is needed - writetup_index_rum will write the whole tuple
	 * to disk, so we need to make sure the padding bytes are defined
	 * (otherwise valgrind would report this).
	 */
	tuple = palloc0(tuplen);

	tuple->tuplen = tuplen;
	tuple->attrnum = attrnum;
	tuple->category = category;
	tuple->keylen = keylen;
	tuple->nitems = nitems;

	/* key type info */
	tuple->typlen = typlen;
	tuple->typbyval = typbyval;

	/*
	 * Copy the key and items into the tuple. First the key value, which we
	 * can simply copy right at the beginning of the data array.
	 */
	if (category == RUM_CAT_NORM_KEY)
	{
		if (typbyval)
		{
			memcpy(tuple->data, &key, sizeof(Datum));
		}
		else if (typlen > 0)    /* byref, fixed length */
		{
			memcpy(tuple->data, DatumGetPointer(key), typlen);
		}
		else if (typlen == -1)
		{
			memcpy(tuple->data, DatumGetPointer(key), keylen);
		}
		else if (typlen == -2)
		{
			memcpy(tuple->data, DatumGetPointer(key), keylen);
		}
	}

	/* finally, copy the TIDs into the array */
	ptr = (char *) tuple + SHORTALIGN(offsetof(RumTuple, data) + keylen);

	/* copy in the compressed data, and free the segments */
	dlist_foreach_modify(iter, &segments)
	{
		RumSegmentInfo *seginfo = dlist_container(RumSegmentInfo, node, iter.cur);

		memcpy(ptr, seginfo->seg, SizeOfRumPostingList(seginfo->seg));

		ptr += SizeOfRumPostingList(seginfo->seg);

		dlist_delete(&seginfo->node);

		pfree(seginfo->seg);
		pfree(seginfo);
	}

	return tuple;
}


Datum
_rum_parse_tuple_key(RumTuple *a)
{
	Datum key;

	if (a->category != RUM_CAT_NORM_KEY)
	{
		return (Datum) 0;
	}

	if (a->typbyval)
	{
		memcpy(&key, a->data, a->keylen);
		return key;
	}

	return PointerGetDatum(a->data);
}


static RumItem *
_rum_parse_tuple_items(RumTuple *a)
{
	int len;
	char *ptr;
	int ndecoded;
	RumItem *items;

	len = a->tuplen - SHORTALIGN(offsetof(RumTuple, data) + a->keylen);
	ptr = (char *) a + SHORTALIGN(offsetof(RumTuple, data) + a->keylen);

	items = rumPostingListDecodeAllSegments((RumPostingList *) ptr, len, &ndecoded);

	Assert(ndecoded == a->nitems);

	return items;
}


/*
 * Check that TID array contains valid values, and that it's sorted (if we
 * expect it to be).
 */
static void
AssertCheckRumItems(RumBuffer *buffer)
{
#ifdef USE_ASSERT_CHECKING

	/* we should not have a buffer with no TIDs to sort */
	Assert(buffer->items != NULL);
	Assert(buffer->nitems > 0);

	for (int i = 0; i < buffer->nitems; i++)
	{
		Assert(ItemPointerIsValid(&buffer->items[i].iptr));
		Assert(buffer->items[i].addInfoIsNull);

		/* don't check ordering for the first TID item */
		if (i == 0)
		{
			continue;
		}

		Assert(ItemPointerCompare(&buffer->items[i - 1].iptr, &buffer->items[i].iptr) <
			   0);
	}
#endif
}


static void
AssertCheckRumBuffer(RumBuffer *buffer)
{
#ifdef USE_ASSERT_CHECKING

	/* if we have any items, the array must exist */
	Assert(!((buffer->nitems > 0) && (buffer->items == NULL)));

	/*
	 * The buffer may be empty, in which case we must not call the check of
	 * item pointers, because that assumes non-emptiness.
	 */
	if (buffer->nitems == 0)
	{
		return;
	}

	/* Make sure the item pointers are valid and sorted. */
	AssertCheckRumItems(buffer);
#endif
}


static RumBuffer *
RumBufferInit(RumState *state)
{
	RumBuffer *buffer = palloc0(sizeof(RumBuffer));
	int i,
		nKeys;

	/*
	 * How many items can we fit into the memory limit? We don't want to end
	 * with too many TIDs. and 64kB seems more than enough. But maybe this
	 * should be tied to maintenance_work_mem or something like that?
	 */
	buffer->maxitems = (64 * 1024L) / sizeof(RumItem);

	nKeys = IndexRelationGetNumberOfKeyAttributes(state->index);

	buffer->ssup = palloc0(sizeof(SortSupportData) * nKeys);

	/*
	 * Lookup ordering operator for the index key data type, and initialize
	 * the sort support function.
	 */
	for (i = 0; i < nKeys; i++)
	{
		Oid cmpFunc;
		SortSupport sortKey = &buffer->ssup[i];

		sortKey->ssup_cxt = CurrentMemoryContext;
		sortKey->ssup_collation = state->index->rd_indcollation[i];

		if (!OidIsValid(sortKey->ssup_collation))
		{
			sortKey->ssup_collation = DEFAULT_COLLATION_OID;
		}

		sortKey->ssup_nulls_first = false;
		sortKey->ssup_attno = i + 1;
		sortKey->abbreviate = false;

		Assert(sortKey->ssup_attno != 0);

		cmpFunc = state->compareFn[i].fn_oid;
		PrepareSortSupportComparisonShim(cmpFunc, sortKey);
	}

	return buffer;
}


static bool
RumBufferIsEmpty(RumBuffer *buffer)
{
	return (buffer->nitems == 0);
}


static bool
RumBufferKeyEquals(RumBuffer *buffer, RumTuple *tup)
{
	int r;
	Datum tupkey;

	AssertCheckRumBuffer(buffer);

	if (tup->attrnum != buffer->attnum)
	{
		return false;
	}

	/* same attribute should have the same type info */
	Assert(tup->typbyval == buffer->typbyval);
	Assert(tup->typlen == buffer->typlen);

	if (tup->category != buffer->category)
	{
		return false;
	}

	/*
	 * For NULL/empty keys, this means equality, for normal keys we need to
	 * compare the actual key value.
	 */
	if (buffer->category != RUM_CAT_NORM_KEY)
	{
		return true;
	}

	/*
	 * For the tuple, get either the first sizeof(Datum) bytes for byval
	 * types, or a pointer to the beginning of the data array.
	 */
	tupkey = (buffer->typbyval) ? *(Datum *) tup->data : PointerGetDatum(tup->data);

	r = ApplySortComparator(buffer->key, false,
							tupkey, false,
							&buffer->ssup[buffer->attnum - 1]);

	return (r == 0);
}


static bool
RumBufferShouldTrim(RumBuffer *buffer, RumTuple *tup)
{
	/* not enough TIDs to trim (1024 is somewhat arbitrary number) */
	if (buffer->nfrozen < 1024)
	{
		return false;
	}

	/* no need to trim if we have not hit the memory limit yet */
	if ((buffer->nitems + tup->nitems) < buffer->maxitems)
	{
		return false;
	}

	/*
	 * OK, we have enough frozen TIDs to flush, and we have hit the memory
	 * limit, so it's time to write it out.
	 */
	return true;
}


/*
 * Stores the tuple that was retrieved from the worker into the in memory
 * state in the coordinator/leader. Ensures that it merges any prior state
 * that was found.
 */
static void
RumBufferStoreTuple(RumBuffer *buffer, RumTuple *tup)
{
	RumItem *items;
	Datum key;

	AssertCheckRumBuffer(buffer);

	key = _rum_parse_tuple_key(tup);
	items = _rum_parse_tuple_items(tup);

	/* if the buffer is empty, set the fields (and copy the key) */
	if (RumBufferIsEmpty(buffer))
	{
		buffer->category = tup->category;
		buffer->keylen = tup->keylen;
		buffer->attnum = tup->attrnum;

		buffer->typlen = tup->typlen;
		buffer->typbyval = tup->typbyval;

		if (tup->category == RUM_CAT_NORM_KEY)
		{
			buffer->key = datumCopy(key, buffer->typbyval, buffer->typlen);
		}
		else
		{
			buffer->key = (Datum) 0;
		}
	}

	/*
	 * Try freeze TIDs at the beginning of the list, i.e. exclude them from
	 * the mergesort. We can do that with TIDs before the first TID in the new
	 * tuple we're about to add into the buffer.
	 *
	 * We do this incrementally when adding data into the in-memory buffer,
	 * and not later (e.g. when hitting a memory limit), because it allows us
	 * to skip the frozen data during the mergesort, making it cheaper.
	 */

	/*
	 * Check if the last TID in the current list is frozen. This is the case
	 * when merging non-overlapping lists, e.g. in each parallel worker.
	 */
	if ((buffer->nitems > 0) &&
		(rumCompareItemPointers(&buffer->items[buffer->nitems - 1].iptr,
								RumTupleGetFirst(tup)) == 0))
	{
		buffer->nfrozen = buffer->nitems;
	}

	/*
	 * Now find the last TID we know to be frozen, i.e. the last TID right
	 * before the new RUM tuple.
	 *
	 * Start with the first not-yet-frozen tuple, and walk until we find the
	 * first TID that's higher. If we already know the whole list is frozen
	 * (i.e. nfrozen == nitems), this does nothing.
	 *
	 * XXX This might do a binary search for sufficiently long lists, but it
	 * does not seem worth the complexity. Overlapping lists should be rare
	 * common, TID comparisons are cheap, and we should quickly freeze most of
	 * the list.
	 */
	for (int i = buffer->nfrozen; i < buffer->nitems; i++)
	{
		/* Is the TID after the first TID of the new tuple? Can't freeze. */
		if (rumCompareItemPointers(&buffer->items[i].iptr,
								   RumTupleGetFirst(tup)) > 0)
		{
			break;
		}

		buffer->nfrozen++;
	}

	/* add the new TIDs into the buffer, combine using merge-sort */
	{
		int nnew;
		RumItem *new;

		/*
		 * Resize the array - we do this first, because we'll dereference the
		 * first unfrozen TID, which would fail if the array is NULL. We'll
		 * still pass 0 as number of elements in that array though.
		 */
		if (buffer->items == NULL)
		{
			buffer->items = palloc((buffer->nitems + tup->nitems) * sizeof(RumItem));
		}
		else
		{
			buffer->items = repalloc(buffer->items,
									 (buffer->nitems + tup->nitems) * sizeof(RumItem));
		}

		new = rumMergeItemPointers(&buffer->items[buffer->nfrozen], /* first unfronzen */
								   (buffer->nitems - buffer->nfrozen),  /* num of unfrozen */
								   items, tup->nitems, &nnew);

		Assert(nnew == (tup->nitems + (buffer->nitems - buffer->nfrozen)));

		memcpy(&buffer->items[buffer->nfrozen], new,
			   nnew * sizeof(RumItem));

		pfree(new);

		buffer->nitems += tup->nitems;

		AssertCheckRumItems(buffer);
	}

	/* free the decompressed TID list */
	pfree(items);
}


static void
RumBufferReset(RumBuffer *buffer)
{
	Assert(!RumBufferIsEmpty(buffer));

	/* release byref values, do nothing for by-val ones */
	if ((buffer->category == RUM_CAT_NORM_KEY) && !buffer->typbyval)
	{
		pfree(DatumGetPointer(buffer->key));
	}

	/*
	 * Not required, but makes it more likely to trigger NULL derefefence if
	 * using the value incorrectly, etc.
	 */
	buffer->key = (Datum) 0;

	buffer->attnum = 0;
	buffer->category = 0;
	buffer->keylen = 0;
	buffer->nitems = 0;
	buffer->nfrozen = 0;

	buffer->typlen = 0;
	buffer->typbyval = 0;
}


static void
RumBufferTrim(RumBuffer *buffer)
{
	Assert((buffer->nfrozen > 0) && (buffer->nfrozen <= buffer->nitems));

	memmove(&buffer->items[0], &buffer->items[buffer->nfrozen],
			sizeof(RumItem) * (buffer->nitems - buffer->nfrozen));

	buffer->nitems -= buffer->nfrozen;
	buffer->nfrozen = 0;
}


static void
RumBufferFree(RumBuffer *buffer)
{
	if (buffer->items)
	{
		pfree(buffer->items);
	}

	/* release byref values, do nothing for by-val ones */
	if (!RumBufferIsEmpty(buffer) &&
		(buffer->category == RUM_CAT_NORM_KEY) && !buffer->typbyval)
	{
		pfree(DatumGetPointer(buffer->key));
	}

	pfree(buffer);
}


static bool
RumBufferCanAddKey(RumBuffer *buffer, RumTuple *tup)
{
	/* empty buffer can accept data for any key */
	if (RumBufferIsEmpty(buffer))
	{
		return true;
	}

	/* otherwise just data for the same key */
	return RumBufferKeyEquals(buffer, tup);
}


/*
 * Flush the current build state to the intermediate state tuplestore.
 */
static void
rumFlushBuildState(RumBuildState *buildstate, Relation index)
{
	RumItem *list;
	Datum key;
	RumNullCategory category;
	uint32 nlist;
	OffsetNumber attnum;
	TupleDesc tdesc = RelationGetDescr(index);

	rumBeginBAScan(&buildstate->accum);
	while ((list = rumGetBAEntry(&buildstate->accum,
								 &attnum, &key, &category, &nlist)) != NULL)
	{
		/* information about the key */
		Form_pg_attribute attr = TupleDescAttr(tdesc, (attnum - 1));

		/* RUM tuple and tuple length */
		RumTuple *tup;
		Size tuplen;

		/* there could be many entries, so be willing to abort here */
		CHECK_FOR_INTERRUPTS();

		tup = _rum_build_tuple(attnum, category,
							   key, attr->attlen, attr->attbyval,
							   list, nlist, &tuplen);

		tuplesort_putrumtuple(buildstate->bs_worker_sort, tup, tuplen);

		pfree(tup);
	}

	MemoryContextReset(buildstate->tmpCtx);
	rumInitBA(&buildstate->accum);
}


static void
rumBuildCallbackParallel(Relation index, ItemPointer tid, Datum *values,
						 bool *isnull, bool tupleIsAlive, void *state)
{
	RumBuildState *buildstate = (RumBuildState *) state;
	MemoryContext oldCtx;
	int i;

	Assert(buildstate->rumstate.useAlternativeOrder == false &&
		   buildstate->rumstate.attrnAddToColumn == InvalidAttrNumber);
	oldCtx = MemoryContextSwitchTo(buildstate->tmpCtx);

	/*
	 * if scan wrapped around - flush accumulated entries and start anew
	 *
	 * With parallel scans, we don't have a guarantee the scan does not start
	 * half-way through the relation (serial builds disable sync scans and
	 * always start from block 0, parallel scans require allow_sync=true).
	 *
	 * Building the posting lists assumes the TIDs are monotonic and never go
	 * back, and the wrap around would break that. We handle that by detecting
	 * the wraparound, and flushing all entries. This means we'll later see
	 * two separate entries with non-overlapping TID lists (which can be
	 * combined by merge sort).
	 *
	 * To detect a wraparound, we remember the last TID seen by each worker
	 * (for any key). If the next TID seen by the worker is lower, the scan
	 * must have wrapped around.
	 */
	if (ItemPointerCompare(tid, &buildstate->tid) < 0)
	{
		rumFlushBuildState(buildstate, index);
	}

	/* remember the TID we're about to process */
	buildstate->tid = *tid;

	for (i = 0; i < buildstate->rumstate.origTupdesc->natts; i++)
	{
		rumHeapTupleBulkInsert(buildstate, (OffsetNumber) (i + 1),
							   values[i], isnull[i], tid, (Datum) 0, true);
	}

	/*
	 * If we've maxed out our available memory, dump everything to the
	 * tuplesort. We use half the per-worker fraction of maintenance_work_mem,
	 * the other half is used for the tuplesort.
	 */
	if (buildstate->accum.allocatedMemory >= buildstate->work_mem * (Size) 1024)
	{
		rumFlushBuildState(buildstate, index);
	}

	MemoryContextSwitchTo(oldCtx);
}


static void
_rum_process_worker_data(RumBuildState *state, Tuplesortstate *worker_sort,
						 bool progress)
{
	RumTuple *tup;
	Size tuplen;

	RumBuffer *buffer;

	/*
	 * Initialize buffer to combine entries for the same key.
	 *
	 * The workers are limited to the same amount of memory as during the sort
	 * in rumBuildCallbackParallel. But this probably should be the 32MB used
	 * during planning, just like there.
	 */
	buffer = RumBufferInit(&state->rumstate);

	/* sort the raw per-worker data */
	if (progress)
	{
		pgstat_progress_update_param(PROGRESS_CREATEIDX_SUBPHASE,
									 PROGRESS_RUM_PHASE_PERFORMSORT_1);
	}

	tuplesort_performsort(state->bs_worker_sort);

	/* reset the number of RUM tuples produced by this worker */
	state->bs_numtuples = 0;

	if (progress)
	{
		pgstat_progress_update_param(PROGRESS_CREATEIDX_SUBPHASE,
									 PROGRESS_RUM_PHASE_MERGE_1);
	}

	/*
	 * Read the RUM tuples from the shared tuplesort, sorted by the key, and
	 * merge them into larger chunks for the leader to combine.
	 */
	while ((tup = tuplesort_getrumtuple(worker_sort, &tuplen, true)) != NULL)
	{
		CHECK_FOR_INTERRUPTS();

		/*
		 * If the buffer can accept the new RUM tuple, just store it there and
		 * we're done. If it's a different key (or maybe too much data) flush
		 * the current contents into the index first.
		 */
		if (!RumBufferCanAddKey(buffer, tup))
		{
			RumTuple *ntup;
			Size ntuplen;

			/*
			 * Buffer is not empty and it's storing a different key - flush
			 * the data into the insert, and start a new entry for current
			 * RumTuple.
			 */
			AssertCheckRumItems(buffer);

			ntup = _rum_build_tuple(buffer->attnum, buffer->category,
									buffer->key, buffer->typlen, buffer->typbyval,
									buffer->items, buffer->nitems, &ntuplen);

			tuplesort_putrumtuple(state->bs_sortstate, ntup, ntuplen);
			state->bs_numtuples++;

			pfree(ntup);

			/* discard the existing data */
			RumBufferReset(buffer);
		}

		/*
		 * We're about to add a RUM tuple to the buffer - check the memory
		 * limit first, and maybe write out some of the data into the index
		 * first, if needed (and possible). We only flush the part of the TID
		 * list that we know won't change, and only if there's enough data for
		 * compression to work well.
		 */
		if (RumBufferShouldTrim(buffer, tup))
		{
			RumTuple *ntup;
			Size ntuplen;

			Assert(buffer->nfrozen > 0);

			/*
			 * Buffer is not empty and it's storing a different key - flush
			 * the data into the insert, and start a new entry for current
			 * RumTuple.
			 */
			AssertCheckRumItems(buffer);

			ntup = _rum_build_tuple(buffer->attnum, buffer->category,
									buffer->key, buffer->typlen, buffer->typbyval,
									buffer->items, buffer->nfrozen, &ntuplen);

			tuplesort_putrumtuple(state->bs_sortstate, ntup, ntuplen);

			pfree(ntup);

			/* truncate the data we've just discarded */
			RumBufferTrim(buffer);
		}

		/*
		 * Remember data for the current tuple (either remember the new key,
		 * or append if to the existing data).
		 */
		RumBufferStoreTuple(buffer, tup);
	}

	/* flush data remaining in the buffer (for the last key) */
	if (!RumBufferIsEmpty(buffer))
	{
		RumTuple *ntup;
		Size ntuplen;

		AssertCheckRumItems(buffer);

		ntup = _rum_build_tuple(buffer->attnum, buffer->category,
								buffer->key, buffer->typlen, buffer->typbyval,
								buffer->items, buffer->nitems, &ntuplen);

		tuplesort_putrumtuple(state->bs_sortstate, ntup, ntuplen);
		state->bs_numtuples++;

		pfree(ntup);

		/* discard the existing data */
		RumBufferReset(buffer);
	}

	/* relase all the memory */
	RumBufferFree(buffer);

	tuplesort_end(worker_sort);
}


static double
_rum_parallel_heapscan(RumBuildState *state)
{
	RumBuildShared *rumshared = state->bs_leader->rumshared;
	int nparticipanttuplesorts;

	nparticipanttuplesorts = state->bs_leader->nparticipanttuplesorts;
	for (;;)
	{
		SpinLockAcquire(&rumshared->mutex);
		if (rumshared->nparticipantsdone == nparticipanttuplesorts)
		{
			/* copy the data into leader state */
			state->bs_reltuples = rumshared->reltuples;
			state->bs_numtuples = rumshared->indtuples;

			SpinLockRelease(&rumshared->mutex);
			break;
		}
		SpinLockRelease(&rumshared->mutex);

		ConditionVariableSleep(&rumshared->workersdonecv,
							   WAIT_EVENT_PARALLEL_CREATE_INDEX_SCAN);
	}

	ConditionVariableCancelSleep();

	return state->bs_reltuples;
}


static void
_rum_parallel_scan_and_build(RumBuildState *state,
							 RumBuildShared *rumshared, Sharedsort *sharedsort,
							 Relation heap, Relation index,
							 int sortmem, bool progress)
{
	SortCoordinate coordinate;
	TableScanDesc scan;
	double reltuples;
	IndexInfo *indexInfo;

	/* Initialize local tuplesort coordination state */
	coordinate = palloc0(sizeof(SortCoordinateData));
	coordinate->isWorker = true;
	coordinate->nParticipants = -1;
	coordinate->sharedsort = sharedsort;

	/* remember how much space is allowed for the accumulated entries */
	state->work_mem = (sortmem / 2);

	/* Begin "partial" tuplesort */
	state->bs_sortstate = tuplesort_begin_indexbuild_rum(heap, index,
														 state->work_mem,
														 coordinate,
														 TUPLESORT_NONE);

	/* Local per-worker sort of raw-data */
	state->bs_worker_sort = tuplesort_begin_indexbuild_rum(heap, index,
														   state->work_mem,
														   NULL,
														   TUPLESORT_NONE);

	/* Join parallel scan */
	indexInfo = BuildIndexInfo(index);
	indexInfo->ii_Concurrent = rumshared->isconcurrent;

	scan = table_beginscan_parallel(heap,
									ParallelTableScanFromRumBuildShared(rumshared));

	reltuples = table_index_build_scan(heap, index, indexInfo, true, progress,
									   rumBuildCallbackParallel, state, scan);

	/* write remaining accumulated entries */
	rumFlushBuildState(state, index);

	/*
	 * Do the first phase of in-worker processing - sort the data produced by
	 * the callback, and combine them into much larger chunks and place that
	 * into the shared tuplestore for leader to process.
	 */
	_rum_process_worker_data(state, state->bs_worker_sort, progress);

	/* sort the RUM tuples built by this worker */
	tuplesort_performsort(state->bs_sortstate);

	state->bs_reltuples += reltuples;

	/*
	 * Completed. Recording ambuild performance statistics.
	 */
	SpinLockAcquire(&rumshared->mutex);
	rumshared->nparticipantsdone++;
	rumshared->reltuples += state->bs_reltuples;
	rumshared->indtuples += state->bs_numtuples;
	SpinLockRelease(&rumshared->mutex);

	/* Notify leader */
	ConditionVariableSignal(&rumshared->workersdonecv);

	tuplesort_end(state->bs_sortstate);
}


static Size
_rum_parallel_estimate_shared(Relation heap, Snapshot snapshot)
{
	/* c.f. shm_toc_allocate as to why BUFFERALIGN is used */
	return add_size(BUFFERALIGN(sizeof(RumBuildShared)),
					table_parallelscan_estimate(heap, snapshot));
}


static void
_rum_leader_participate_as_worker(RumBuildState *buildstate, Relation heap, Relation
								  index)
{
	RumLeader *rumleader = buildstate->bs_leader;
	int sortmem;

	/*
	 * Might as well use reliable figure when doling out maintenance_work_mem
	 * (when requested number of workers were not launched, this will be
	 * somewhat higher than it is for other workers).
	 */
	sortmem = maintenance_work_mem / rumleader->nparticipanttuplesorts;

	/* Perform work common to all participants */
	_rum_parallel_scan_and_build(buildstate, rumleader->rumshared,
								 rumleader->sharedsort, heap, index,
								 sortmem, true);
}


static void
_rum_begin_parallel(RumBuildState *buildstate, Relation heap, Relation index,
					bool isconcurrent, int request)
{
	ParallelContext *pcxt;
	int scantuplesortstates;
	Snapshot snapshot;
	Size estrumshared;
	Size estsort;
	RumBuildShared *rumshared;
	Sharedsort *sharedsort;
	RumLeader *rumleader = (RumLeader *) palloc0(sizeof(RumLeader));
	WalUsage *walusage;
	BufferUsage *bufferusage;
	bool leaderparticipates = true;
	int querylen;

#ifdef DISABLE_LEADER_PARTICIPATION
	leaderparticipates = false;
#endif

	/*
	 * Enter parallel mode, and create context for parallel build of rum index
	 */
	EnterParallelMode();
	Assert(request > 0);
	pcxt = CreateParallelContext("pg_documentdb_extended_rum_core",
								 "documentdb_rum_parallel_build_main",
								 request);

	scantuplesortstates = leaderparticipates ? request + 1 : request;

	/*
	 * Prepare for scan of the base relation.  In a normal index build, we use
	 * SnapshotAny because we must retrieve all tuples and do our own time
	 * qual checks (because we have to index RECENTLY_DEAD tuples).  In a
	 * concurrent build, we take a regular MVCC snapshot and index whatever's
	 * live according to that.
	 */
	if (!isconcurrent)
	{
		snapshot = SnapshotAny;
	}
	else
	{
		snapshot = RegisterSnapshot(GetTransactionSnapshot());
	}

	/*
	 * Estimate size for our own PARALLEL_KEY_RUM_SHARED workspace.
	 */
	estrumshared = _rum_parallel_estimate_shared(heap, snapshot);
	shm_toc_estimate_chunk(&pcxt->estimator, estrumshared);
	estsort = tuplesort_estimate_shared(scantuplesortstates);
	shm_toc_estimate_chunk(&pcxt->estimator, estsort);

	shm_toc_estimate_keys(&pcxt->estimator, 2);

	/*
	 * Estimate space for WalUsage and BufferUsage -- PARALLEL_KEY_WAL_USAGE
	 * and PARALLEL_KEY_BUFFER_USAGE.
	 *
	 * If there are no extensions loaded that care, we could skip this.  We
	 * have no way of knowing whether anyone's looking at pgWalUsage or
	 * pgBufferUsage, so do it unconditionally.
	 */
	shm_toc_estimate_chunk(&pcxt->estimator,
						   mul_size(sizeof(WalUsage), pcxt->nworkers));
	shm_toc_estimate_keys(&pcxt->estimator, 1);
	shm_toc_estimate_chunk(&pcxt->estimator,
						   mul_size(sizeof(BufferUsage), pcxt->nworkers));
	shm_toc_estimate_keys(&pcxt->estimator, 1);

	/* Finally, estimate PARALLEL_KEY_QUERY_TEXT space */
	if (debug_query_string)
	{
		querylen = strlen(debug_query_string);
		shm_toc_estimate_chunk(&pcxt->estimator, querylen + 1);
		shm_toc_estimate_keys(&pcxt->estimator, 1);
	}
	else
	{
		querylen = 0;           /* keep compiler quiet */
	}

	/* Everyone's had a chance to ask for space, so now create the DSM */
	InitializeParallelDSM(pcxt);

	/* If no DSM segment was available, back out (do serial build) */
	if (pcxt->seg == NULL)
	{
		if (IsMVCCSnapshot(snapshot))
		{
			UnregisterSnapshot(snapshot);
		}
		DestroyParallelContext(pcxt);
		ExitParallelMode();
		return;
	}

	/* Store shared build state, for which we reserved space */
	rumshared = (RumBuildShared *) shm_toc_allocate(pcxt->toc, estrumshared);

	/* Initialize immutable state */
	rumshared->heaprelid = RelationGetRelid(heap);
	rumshared->indexrelid = RelationGetRelid(index);
	rumshared->isconcurrent = isconcurrent;
	rumshared->scantuplesortstates = scantuplesortstates;

	ConditionVariableInit(&rumshared->workersdonecv);
	SpinLockInit(&rumshared->mutex);

	/* Initialize mutable state */
	rumshared->nparticipantsdone = 0;
	rumshared->reltuples = 0.0;
	rumshared->indtuples = 0.0;

	table_parallelscan_initialize(heap,
								  ParallelTableScanFromRumBuildShared(rumshared),
								  snapshot);

	/*
	 * Store shared tuplesort-private state, for which we reserved space.
	 * Then, initialize opaque state using tuplesort routine.
	 */
	sharedsort = (Sharedsort *) shm_toc_allocate(pcxt->toc, estsort);
	tuplesort_initialize_shared(sharedsort, scantuplesortstates,
								pcxt->seg);

	shm_toc_insert(pcxt->toc, PARALLEL_KEY_RUM_SHARED, rumshared);
	shm_toc_insert(pcxt->toc, PARALLEL_KEY_TUPLESORT, sharedsort);

	/* Store query string for workers */
	if (debug_query_string)
	{
		char *sharedquery;

		sharedquery = (char *) shm_toc_allocate(pcxt->toc, querylen + 1);
		memcpy(sharedquery, debug_query_string, querylen + 1);
		shm_toc_insert(pcxt->toc, PARALLEL_KEY_QUERY_TEXT, sharedquery);
	}

	/*
	 * Allocate space for each worker's WalUsage and BufferUsage; no need to
	 * initialize.
	 */
	walusage = shm_toc_allocate(pcxt->toc,
								mul_size(sizeof(WalUsage), pcxt->nworkers));
	shm_toc_insert(pcxt->toc, PARALLEL_KEY_WAL_USAGE, walusage);
	bufferusage = shm_toc_allocate(pcxt->toc,
								   mul_size(sizeof(BufferUsage), pcxt->nworkers));
	shm_toc_insert(pcxt->toc, PARALLEL_KEY_BUFFER_USAGE, bufferusage);

	/* Launch workers, saving status for leader/caller */
	LaunchParallelWorkers(pcxt);
	rumleader->pcxt = pcxt;
	rumleader->nparticipanttuplesorts = pcxt->nworkers_launched;
	if (leaderparticipates)
	{
		rumleader->nparticipanttuplesorts++;
	}
	rumleader->rumshared = rumshared;
	rumleader->sharedsort = sharedsort;
	rumleader->snapshot = snapshot;
	rumleader->walusage = walusage;
	rumleader->bufferusage = bufferusage;

	/* If no workers were successfully launched, back out (do serial build) */
	if (pcxt->nworkers_launched == 0)
	{
		_rum_end_parallel(rumleader, NULL);
		return;
	}

	/* Save leader state now that it's clear build will be parallel */
	buildstate->bs_leader = rumleader;

	/* Join heap scan ourselves */
	if (leaderparticipates)
	{
		_rum_leader_participate_as_worker(buildstate, heap, index);
	}

	/*
	 * Caller needs to wait for all launched workers when we return.  Make
	 * sure that the failure-to-start case will not hang forever.
	 */
	WaitForParallelWorkersToAttach(pcxt);
}


static double
_rum_parallel_merge(RumBuildState *state)
{
	RumTuple *tup;
	Size tuplen;
	double reltuples = 0;
	RumBuffer *buffer;
	MemoryContext oldCtx;

	/* RUM tuples from workers, merged by leader */
	double numtuples = 0;

	/* wait for workers to scan table and produce partial results */
	reltuples = _rum_parallel_heapscan(state);

	/* If at least one tuple got parallel then log it */
	if (reltuples >= 1.0)
	{
		elog(LOG, "Rum performing parallel merge on %f tuples.", reltuples);
	}

	/* Execute the sort */
	pgstat_progress_update_param(PROGRESS_CREATEIDX_SUBPHASE,
								 PROGRESS_RUM_PHASE_PERFORMSORT_2);

	/* do the actual sort in the leader */
	tuplesort_performsort(state->bs_sortstate);

	/*
	 * Initialize buffer to combine entries for the same key.
	 *
	 * The leader is allowed to use the whole maintenance_work_mem buffer to
	 * combine data. The parallel workers already completed.
	 */
	buffer = RumBufferInit(&state->rumstate);

	/*
	 * Set the progress target for the next phase.  Reset the block number
	 * values set by table_index_build_scan
	 */
	{
		const int progress_index[] = {
			PROGRESS_CREATEIDX_SUBPHASE,
			PROGRESS_CREATEIDX_TUPLES_TOTAL,
			PROGRESS_SCAN_BLOCKS_TOTAL,
			PROGRESS_SCAN_BLOCKS_DONE
		};
		const int64 progress_vals[] = {
			PROGRESS_RUM_PHASE_MERGE_2,
			state->bs_numtuples,
			0, 0
		};

		pgstat_progress_update_multi_param(4, progress_index, progress_vals);
	}

	/*
	 * Read the RUM tuples from the shared tuplesort, sorted by category and
	 * key. That probably gives us order matching how data is organized in the
	 * index.
	 *
	 * We don't insert the RUM tuples right away, but instead accumulate as
	 * many TIDs for the same key as possible, and then insert that at once.
	 * This way we don't need to decompress/recompress the posting lists, etc.
	 */
	while ((tup = tuplesort_getrumtuple(state->bs_sortstate, &tuplen, true)) != NULL)
	{
		CHECK_FOR_INTERRUPTS();

		/*
		 * If the buffer can accept the new RUM tuple, just store it there and
		 * we're done. If it's a different key (or maybe too much data) flush
		 * the current contents into the index first.
		 */
		if (!RumBufferCanAddKey(buffer, tup))
		{
			/*
			 * Buffer is not empty and it's storing a different key - flush
			 * the data into the insert, and start a new entry for current
			 * RumTuple.
			 */
			AssertCheckRumItems(buffer);

			oldCtx = MemoryContextSwitchTo(state->tmpCtx);
			rumEntryInsert(&state->rumstate,
						   buffer->attnum, buffer->key, buffer->category,
						   buffer->items, buffer->nitems, &state->buildStats);
			MemoryContextSwitchTo(oldCtx);
			MemoryContextReset(state->tmpCtx);

			/* discard the existing data */
			RumBufferReset(buffer);
		}

		/*
		 * We're about to add a RUM tuple to the buffer - check the memory
		 * limit first, and maybe write out some of the data into the index
		 * first, if needed (and possible). We only flush the part of the TID
		 * list that we know won't change, and only if there's enough data for
		 * compression to work well.
		 */
		if (RumBufferShouldTrim(buffer, tup))
		{
			Assert(buffer->nfrozen > 0);

			/*
			 * Buffer is not empty and it's storing a different key - flush
			 * the data into the insert, and start a new entry for current
			 * RumTuple.
			 */
			AssertCheckRumItems(buffer);

			oldCtx = MemoryContextSwitchTo(state->tmpCtx);
			rumEntryInsert(&state->rumstate,
						   buffer->attnum, buffer->key, buffer->category,
						   buffer->items, buffer->nfrozen, &state->buildStats);
			MemoryContextSwitchTo(oldCtx);
			MemoryContextReset(state->tmpCtx);

			/* truncate the data we've just discarded */
			RumBufferTrim(buffer);
		}

		/*
		 * Remember data for the current tuple (either remember the new key,
		 * or append if to the existing data).
		 */
		RumBufferStoreTuple(buffer, tup);

		/* Report progress */
		pgstat_progress_update_param(PROGRESS_CREATEIDX_TUPLES_DONE,
									 ++numtuples);
	}

	/* flush data remaining in the buffer (for the last key) */
	if (!RumBufferIsEmpty(buffer))
	{
		AssertCheckRumItems(buffer);

		rumEntryInsert(&state->rumstate,
					   buffer->attnum, buffer->key, buffer->category,
					   buffer->items, buffer->nitems, &state->buildStats);

		/* discard the existing data */
		RumBufferReset(buffer);

		/* Report progress */
		pgstat_progress_update_param(PROGRESS_CREATEIDX_TUPLES_DONE,
									 ++numtuples);
	}

	/* relase all the memory */
	RumBufferFree(buffer);
	tuplesort_end(state->bs_sortstate);

	return reltuples;
}


static void
_rum_end_parallel(RumLeader *rumleader, RumBuildState *state)
{
	int i;

	/* Terminate active worker processes */
	WaitForParallelWorkersToFinish(rumleader->pcxt);

	/*
	 * Next, accumulate WAL usage.  (This must wait for the workers to finish,
	 * or we might get incomplete data.)
	 */
	for (i = 0; i < rumleader->pcxt->nworkers_launched; i++)
	{
		InstrAccumParallelQuery(&rumleader->bufferusage[i], &rumleader->walusage[i]);
	}

	/* Free last reference to MVCC snapshot, if one was used */
	if (IsMVCCSnapshot(rumleader->snapshot))
	{
		UnregisterSnapshot(rumleader->snapshot);
	}
	DestroyParallelContext(rumleader->pcxt);
	ExitParallelMode();
}


static IndexBuildResult *
rumbuild_parallel(Relation heap, Relation index, struct IndexInfo *indexInfo,
				  RumBuildState *buildstate, bool canBuildParallel)
{
	MemoryContext oldCtx;
	IndexBuildResult *result;
	double reltuples;
	RumItem *items;
	Datum key;
	RumNullCategory category;
	uint32 nlist;
	OffsetNumber attnum;

	/* Report table scan phase started */
	pgstat_progress_update_param(PROGRESS_CREATEIDX_SUBPHASE,
								 PROGRESS_RUM_PHASE_INDEXBUILD_TABLESCAN);

	/*
	 * Attempt to launch parallel worker scan when required
	 *
	 * XXX plan_create_index_workers makes the number of workers dependent on
	 * maintenance_work_mem, requiring 32MB for each worker. For RUM that's
	 * reasonable too, because we sort the data just like btree. It does
	 * ignore the memory used to accumulate data in memory (set by work_mem),
	 * but there is no way to communicate that to plan_create_index_workers.
	 */
#if PG_VERSION_NUM >= 160000
	if (RumParallelIndexWorkersOverride > 0 && canBuildParallel)
	{
		int parallel_workers = RumParallelIndexWorkersOverride;
		parallel_workers = Min(parallel_workers,
							   max_parallel_maintenance_workers);
		while (parallel_workers > 0 &&
			   maintenance_work_mem / (parallel_workers + 1) < 32 * 1024)
		{
			parallel_workers--;
		}

		indexInfo->ii_ParallelWorkers = parallel_workers;
		elog(DEBUG1, "Overriding parallel workers to %d",
			 RumParallelIndexWorkersOverride);
	}
#endif

	if (indexInfo->ii_ParallelWorkers > 0 &&
		RumParallelIndexWorkersOverride > 0 &&
		canBuildParallel)
	{
		ereport(DEBUG1, (errmsg("parallel index build requested with %d workers",
								indexInfo->ii_ParallelWorkers)));
		_rum_begin_parallel(buildstate, heap, index, indexInfo->ii_Concurrent,
							indexInfo->ii_ParallelWorkers);
	}

	/*
	 * If parallel build requested and at least one worker process was
	 * successfully launched, set up coordination state, wait for workers to
	 * complete. Then read all tuples from the shared tuplesort and insert
	 * them into the index.
	 *
	 * In serial mode, simply scan the table and build the index one index
	 * tuple at a time.
	 */
	if (buildstate->bs_leader)
	{
		/* TODO: Enable parallel index build here */
		SortCoordinate coordinate;

		coordinate = (SortCoordinate) palloc0(sizeof(SortCoordinateData));
		coordinate->isWorker = false;
		coordinate->nParticipants =
			buildstate->bs_leader->nparticipanttuplesorts;
		coordinate->sharedsort = buildstate->bs_leader->sharedsort;
		buildstate->bs_sortstate =
			tuplesort_begin_indexbuild_rum(heap, index,
										   maintenance_work_mem, coordinate,
										   TUPLESORT_NONE);

		/* scan the relation in parallel and merge per-worker results */
		reltuples = _rum_parallel_merge(buildstate);
		_rum_end_parallel(buildstate->bs_leader, buildstate);
	}
	else                        /* no parallel index build */
	{
		reltuples = IndexBuildHeapScan(heap, index, indexInfo, false,
									   rumBuildCallback, (void *) buildstate);

		/* dump remaining entries to the index */
		oldCtx = MemoryContextSwitchTo(buildstate->tmpCtx);
		rumBeginBAScan(&buildstate->accum);
		while ((items = rumGetBAEntry(&buildstate->accum,
									  &attnum, &key, &category, &nlist)) != NULL)
		{
			/* there could be many entries, so be willing to abort here */
			CHECK_FOR_INTERRUPTS();
			rumEntryInsert(&buildstate->rumstate, attnum, key, category,
						   items, nlist, &buildstate->buildStats);
		}
		MemoryContextSwitchTo(oldCtx);
	}

	MemoryContextDelete(buildstate->funcCtx);
	MemoryContextDelete(buildstate->tmpCtx);

	/*
	 * Update metapage stats
	 */
	buildstate->buildStats.nTotalPages = RelationGetNumberOfBlocks(index);
	rumUpdateStats(index, &buildstate->buildStats, true);

	pgstat_progress_update_param(PROGRESS_CREATEIDX_SUBPHASE,
								 PROGRESS_RUM_PHASE_WRITE_WAL);

	/*
	 * We didn't write WAL records as we built the index, so if WAL-logging is
	 * required, write all pages to the WAL now.
	 */
	if (RelationNeedsWAL(index))
	{
		log_newpage_range(index, MAIN_FORKNUM,
						  0, RelationGetNumberOfBlocks(index),
						  true);
	}

	/*
	 * Return statistics
	 */
	result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));

	if (buildstate->bs_leader)
	{
		ereport(DEBUG1, (errmsg(
							 "parallel index build completed with %f heaptuples and %f indextuples",
							 reltuples, buildstate->indtuples)));
	}

	result->heap_tuples = reltuples;
	result->index_tuples = buildstate->indtuples;
	return result;
}


/*
 *	rumbuildempty() -- build an empty rum index in the initialization fork
 */
void
rumbuildempty(Relation index)
{
	Buffer RootBuffer,
		   MetaBuffer;
	GenericXLogState *state;

	state = GenericXLogStart(index);

	/* An empty RUM index has two pages. */
	MetaBuffer =
		ReadBufferExtended(index, INIT_FORKNUM, P_NEW, RBM_NORMAL, NULL);
	LockBuffer(MetaBuffer, BUFFER_LOCK_EXCLUSIVE);
	RootBuffer =
		ReadBufferExtended(index, INIT_FORKNUM, P_NEW, RBM_NORMAL, NULL);
	LockBuffer(RootBuffer, BUFFER_LOCK_EXCLUSIVE);

	/* Initialize and xlog metabuffer and root buffer. */
	RumInitMetabuffer(state, MetaBuffer, false);
	RumInitBuffer(state, RootBuffer, RUM_LEAF, false);

	GenericXLogFinish(state);

	/* Unlock and release the buffers. */
	UnlockReleaseBuffer(MetaBuffer);
	UnlockReleaseBuffer(RootBuffer);
}


/*
 * Insert index entries for a single indexable item during "normal"
 * (non-fast-update) insertion
 */
static void
rumHeapTupleInsert(RumState *rumstate, OffsetNumber attnum,
				   Datum value, bool isNull,
				   ItemPointer item,
				   Datum outerAddInfo,
				   bool outerAddInfoIsNull)
{
	Datum *entries;
	RumNullCategory *categories;
	int32 i,
		  nentries;
	Datum *addInfo;
	bool *addInfoIsNull;

	entries = rumExtractEntries(rumstate, attnum, value, isNull,
								&nentries, &categories, &addInfo, &addInfoIsNull);

	if (attnum == rumstate->attrnAddToColumn)
	{
		addInfo = palloc(sizeof(*addInfo) * nentries);
		addInfoIsNull = palloc(sizeof(*addInfoIsNull) * nentries);

		for (i = 0; i < nentries; i++)
		{
			addInfo[i] = outerAddInfo;
			addInfoIsNull[i] = outerAddInfoIsNull;
		}
	}

	for (i = 0; i < nentries; i++)
	{
		RumItem insert_item;

		/* Check existance of additional information attribute in index */
		if (!addInfoIsNull[i] && !rumstate->addAttrs[attnum - 1])
		{
			Form_pg_attribute attr = RumTupleDescAttr(rumstate->origTupdesc,
													  attnum - 1);

			elog(ERROR, "additional information attribute \"%s\" is not found in index",
				 NameStr(attr->attname));
		}

		memset(&insert_item, 0, sizeof(insert_item));
		insert_item.iptr = *item;
		insert_item.addInfo = addInfo[i];
		insert_item.addInfoIsNull = addInfoIsNull[i];

		rumEntryInsert(rumstate, attnum, entries[i], categories[i],
					   &insert_item, 1, NULL);
	}
}


bool
ruminsert(Relation index, Datum *values, bool *isnull,
		  ItemPointer ht_ctid, Relation heapRel,
		  IndexUniqueCheck checkUnique
#if PG_VERSION_NUM >= 140000
		  , bool indexUnchanged
#endif
#if PG_VERSION_NUM >= 100000
		  , struct IndexInfo *indexInfo
#endif
		  )
{
	RumState rumstate;
	MemoryContext oldCtx;
	MemoryContext insertCtx;
	int i;
	Datum outerAddInfo = (Datum) 0;
	bool outerAddInfoIsNull = true;

	insertCtx = RumContextCreate(CurrentMemoryContext,
								 "Rum insert temporary context");

	oldCtx = MemoryContextSwitchTo(insertCtx);

	initRumState(&rumstate, index);

	if (AttributeNumberIsValid(rumstate.attrnAttachColumn))
	{
		outerAddInfo = values[rumstate.attrnAttachColumn - 1];
		outerAddInfoIsNull = isnull[rumstate.attrnAttachColumn - 1];
	}

	for (i = 0; i < rumstate.origTupdesc->natts; i++)
	{
		rumHeapTupleInsert(&rumstate, (OffsetNumber) (i + 1),
						   values[i], isnull[i], ht_ctid,
						   outerAddInfo, outerAddInfoIsNull);
	}

	MemoryContextSwitchTo(oldCtx);
	MemoryContextDelete(insertCtx);

	return false;
}


PGDLLEXPORT void
documentdb_rum_parallel_build_main(dsm_segment *seg, shm_toc *toc)
{
	bool progress = false;
	char *sharedquery;
	RumBuildShared *rumshared;
	Sharedsort *sharedsort;
	RumBuildState buildstate;
	Relation heapRel;
	Relation indexRel;
	LOCKMODE heapLockmode;
	LOCKMODE indexLockmode;
	WalUsage *walusage;
	BufferUsage *bufferusage;
	int sortmem;

	/*
	 * The only possible status flag that can be set to the parallel worker is
	 * PROC_IN_SAFE_IC.
	 */
	Assert((MyProc->statusFlags == 0) ||
		   (MyProc->statusFlags == PROC_IN_SAFE_IC));

	/* Set debug_query_string for individual workers first */
	sharedquery = shm_toc_lookup(toc, PARALLEL_KEY_QUERY_TEXT, true);
	debug_query_string = sharedquery;

	/* Report the query string from leader */
	pgstat_report_activity(STATE_RUNNING, debug_query_string);

	/* Look up rum shared state */
	rumshared = shm_toc_lookup(toc, PARALLEL_KEY_RUM_SHARED, false);

	/* Open relations using lock modes known to be obtained by index.c */
	if (!rumshared->isconcurrent)
	{
		heapLockmode = ShareLock;
		indexLockmode = AccessExclusiveLock;
	}
	else
	{
		heapLockmode = ShareUpdateExclusiveLock;
		indexLockmode = RowExclusiveLock;
	}

	/* Open relations within worker */
	heapRel = table_open(rumshared->heaprelid, heapLockmode);
	indexRel = index_open(rumshared->indexrelid, indexLockmode);

	/* initialize the RUM build state */
	initRumState(&buildstate.rumstate, indexRel);
	buildstate.indtuples = 0;
	memset(&buildstate.buildStats, 0, sizeof(RumStatsData));

	memset(&buildstate.tid, 0, sizeof(ItemPointerData));

	/*
	 * create a temporary memory context that is used to hold data not yet
	 * dumped out to the index
	 */
	buildstate.tmpCtx = AllocSetContextCreate(CurrentMemoryContext,
											  "Rum build temporary context",
											  ALLOCSET_DEFAULT_SIZES);

	/*
	 * create a temporary memory context that is used for calling
	 * rumExtractEntries(), and can be reset after each tuple
	 */
	buildstate.funcCtx = AllocSetContextCreate(CurrentMemoryContext,
											   "Rum build temporary context for user-defined function",
											   ALLOCSET_DEFAULT_SIZES);

	buildstate.accum.rumstate = &buildstate.rumstate;
	rumInitBA(&buildstate.accum);


	/* Look up shared state private to tuplesort.c */
	sharedsort = shm_toc_lookup(toc, PARALLEL_KEY_TUPLESORT, false);
	tuplesort_attach_shared(sharedsort, seg);

	/* Prepare to track buffer usage during parallel execution */
	InstrStartParallelQuery();

	/*
	 * Might as well use reliable figure when doling out maintenance_work_mem
	 * (when requested number of workers were not launched, this will be
	 * somewhat higher than it is for other workers).
	 */
	sortmem = maintenance_work_mem / rumshared->scantuplesortstates;

	/* Don't update the total number of blocks on progress on the worker */
	_rum_parallel_scan_and_build(&buildstate, rumshared, sharedsort,
								 heapRel, indexRel, sortmem, progress);

	/* Report WAL/buffer usage during parallel execution */
	bufferusage = shm_toc_lookup(toc, PARALLEL_KEY_BUFFER_USAGE, false);
	walusage = shm_toc_lookup(toc, PARALLEL_KEY_WAL_USAGE, false);
	InstrEndParallelQuery(&bufferusage[ParallelWorkerNumber],
						  &walusage[ParallelWorkerNumber]);

	index_close(indexRel, indexLockmode);
	table_close(heapRel, heapLockmode);
}
