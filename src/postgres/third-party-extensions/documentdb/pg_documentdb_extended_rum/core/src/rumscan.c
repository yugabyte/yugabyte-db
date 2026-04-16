/*-------------------------------------------------------------------------
 *
 * rumscan.c
 *	  routines to manage scans of inverted index relations
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 2015-2022, Postgres Professional
 * Portions Copyright (c) 1996-2016, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/relscan.h"
#include "pgstat.h"
#include "commands/explain.h"
#if PG_VERSION_NUM >= 180000
#include <commands/explain_state.h>
#include <commands/explain_format.h>
#endif
#include <storage/lwlock.h>
#include "pg_documentdb_rum.h"

extern int RumParallelScanTrancheId;
extern const char *RumParallelScanTrancheName;

#if PG_VERSION_NUM >= 180000
#define ParallelScanGetOpaque(x) OffsetToPointer((void *) x, \
												 x->ps_offset_am)

#else
#define ParallelScanGetOpaque(x) OffsetToPointer((void *) x, \
												 x->ps_offset)

static bool TrancheRegistered = false;
#endif

typedef enum RumParallelScanState
{
	RumParallelScanState_NotInitialized = 0,
	RumParallelScanState_RunningStartScan = 1,
	RumParallelScanState_StartScanDone = 2,
	RumParallelScanState_Idle = 3,
	RumParallelScanState_ScanningTree = 4,
	RumParallelScanState_Done = 5,
} RumParallelScanState;

typedef struct RumParallelScanDescData
{
	BlockNumber rum_ps_current_page; /* latest or next page to be scanned */
	RumParallelScanState parallel_scan_state;
	bool isParallelScanEligible;
	LWLock rum_ps_lock;             /* protects shared parallel state */
	ConditionVariable rum_ps_cv;    /* used to synchronize parallel scan */
} RumParallelScanDescData;

extern PGDLLEXPORT void try_explain_documentdb_rum_index(IndexScanDesc scan,
														 struct ExplainState *es);
extern PGDLLEXPORT bool can_documentdb_rum_index_scan_ordered(IndexScanDesc scan);

IndexScanDesc
rumbeginscan(Relation rel, int nkeys, int norderbys)
{
	IndexScanDesc scan;
	RumScanOpaque so;
	MemoryContext prev = CurrentMemoryContext;

	scan = RelationGetIndexScan(rel, nkeys, norderbys);

	/* allocate private workspace */
	so = (RumScanOpaque) palloc(sizeof(RumScanOpaqueData));
	so->sortstate = NULL;
	so->keys = NULL;
	so->nkeys = 0;
	so->firstCall = true;
	so->totalentries = 0;
	so->sortedEntries = NULL;
	so->orderByScanData = NULL;
	so->scanLoops = 0;
	so->killedItems = NULL;
	so->numKilled = 0;
	so->killedItemsSkipped = 0;
	so->orderByKeyIndex = -1;
	so->orderScanDirection = ForwardScanDirection;
	so->tempCtx = RumContextCreate(CurrentMemoryContext,
								   "Rum scan temporary context");
	so->keyCtx = RumContextCreate(CurrentMemoryContext,
								  "Rum scan key context");
	so->rumStateCtx = RumContextCreate(CurrentMemoryContext,
									   "Rum state context");

	/* Allocate rumstate in the key context so it gets cleaned on endscan */
	MemoryContextSwitchTo(so->rumStateCtx);
	initRumState(&so->rumstate, scan->indexRelation);
	MemoryContextSwitchTo(prev);

#if PG_VERSION_NUM >= 120000

	/*
	 * Starting from PG 12 we need to invalidate result's item pointer. Earlier
	 * it was done by invalidating scan->xs_ctup by RelationGetIndexScan().
	 */
	ItemPointerSetInvalid(&scan->xs_heaptid);
#endif
	scan->opaque = so;

	return scan;
}


/*
 * Create a new RumScanEntry, unless an equivalent one already exists,
 * in which case just return it
 */
static RumScanEntry
rumFillScanEntry(RumScanOpaque so, OffsetNumber attnum,
				 StrategyNumber strategy, int32 searchMode,
				 Datum queryKey, RumNullCategory queryCategory,
				 bool isPartialMatch, Pointer extra_data)
{
	RumState *rumstate = &so->rumstate;
	RumScanEntry scanEntry;
	uint32 i;

	/*
	 * Look for an existing equivalent entry.
	 *
	 * Entries with non-null extra_data are never considered identical, since
	 * we can't know exactly what the opclass might be doing with that.
	 */
	if (extra_data == NULL || !isPartialMatch)
	{
		for (i = 0; i < so->totalentries; i++)
		{
			RumScanEntry prevEntry = so->entries[i];

			if (prevEntry->extra_data == NULL &&
				prevEntry->isPartialMatch == isPartialMatch &&
				prevEntry->strategy == strategy &&
				prevEntry->searchMode == searchMode &&
				prevEntry->attnum == attnum &&
				rumCompareEntries(rumstate, attnum,
								  prevEntry->queryKey,
								  prevEntry->queryCategory,
								  queryKey,
								  queryCategory) == 0)
			{
				/* Successful match */
				return prevEntry;
			}
		}
	}

	/* Nope, create a new entry */
	scanEntry = (RumScanEntry) palloc(sizeof(RumScanEntryData));
	scanEntry->queryKeyOverride = (Datum) 0;
	scanEntry->queryKey = queryKey;
	scanEntry->queryCategory = queryCategory;
	scanEntry->isPartialMatch = isPartialMatch;
	scanEntry->extra_data = extra_data;
	scanEntry->strategy = strategy;
	scanEntry->searchMode = searchMode;
	scanEntry->attnum = scanEntry->attnumOrig = attnum;

	scanEntry->buffer = InvalidBuffer;
	RumItemSetMin(&scanEntry->curItem);
	scanEntry->curKey = (Datum) 0;
	scanEntry->curKeyCategory = RUM_CAT_NULL_KEY;
	scanEntry->useCurKey = false;
	scanEntry->matchSortstate = NULL;
	scanEntry->scanWithAddInfo = false;
	scanEntry->list = NULL;
	scanEntry->gdi = NULL;
	scanEntry->stack = NULL;
	scanEntry->nlist = 0;
	scanEntry->cachedLsn = InvalidXLogRecPtr;
	scanEntry->offset = InvalidOffsetNumber;
	scanEntry->isFinished = false;
	scanEntry->reduceResult = false;
	scanEntry->useMarkAddInfo = false;
	scanEntry->scanDirection = ForwardScanDirection;
	scanEntry->predictNumberResult = 0;
	ItemPointerSetMin(&scanEntry->markAddInfo.iptr);

	return scanEntry;
}


/*
 * Initialize the next RumScanKey using the output from the extractQueryFn
 */
static void
rumFillScanKey(RumScanOpaque so, OffsetNumber attnum,
			   StrategyNumber strategy, int32 searchMode,
			   Datum query, uint32 nQueryValues,
			   Datum *queryValues, RumNullCategory *queryCategories,
			   bool *partial_matches, Pointer *extra_data,
			   bool orderBy)
{
	RumScanKey key = palloc0(sizeof(*key));
	RumState *rumstate = &so->rumstate;
	uint32 nUserQueryValues = nQueryValues;
	uint32 i;

	so->keys[so->nkeys++] = key;

	/* Non-default search modes add one "hidden" entry to each key */
	if (searchMode != GIN_SEARCH_MODE_DEFAULT)
	{
		nQueryValues++;
	}
	key->orderBy = orderBy;

	key->query = query;
	key->queryValues = queryValues;
	key->queryCategories = queryCategories;
	key->extra_data = extra_data;
	key->strategy = strategy;
	key->searchMode = searchMode;
	key->attnum = key->attnumOrig = attnum;
	key->useAddToColumn = false;
	key->useCurKey = false;
	key->scanDirection = ForwardScanDirection;

	RumItemSetMin(&key->curItem);
	key->curItemMatches = false;
	key->recheckCurItem = false;
	key->isFinished = false;

	key->addInfoKeys = NULL;
	key->addInfoNKeys = 0;

	if (key->orderBy)
	{
		if (key->attnum != rumstate->attrnAttachColumn)
		{
			key->useCurKey = rumstate->canOrdering[attnum - 1] &&

			                 /* ordering function by index key value has 3 arguments */
							 rumstate->orderingFn[attnum - 1].fn_nargs == 3;
		}

		/* Add key to order by additional information... */
		if (key->attnum == rumstate->attrnAttachColumn)
		{
			Form_pg_attribute attr = RumTupleDescAttr(rumstate->origTupdesc,
													  attnum - 1);

			if (nQueryValues != 1)
			{
				elog(ERROR, "extractQuery should return only one value for ordering");
			}
			if (attr->attbyval == false)
			{
				elog(ERROR, "doesn't support order by over pass-by-reference column");
			}

			if (key->attnum == rumstate->attrnAttachColumn)
			{
				if (rumstate->canOuterOrdering[attnum - 1] == false)
				{
					elog(ERROR, "doesn't support ordering as additional info");
				}

				key->useAddToColumn = true;
				key->outerAddInfoIsNull = true;
				key->attnum = rumstate->attrnAddToColumn;
			}
			else if (key->useCurKey)
			{
				RumScanKey scanKey = NULL;

				for (i = 0; i < so->nkeys; i++)
				{
					if (so->keys[i]->orderBy == false &&
						so->keys[i]->attnum == key->attnum)
					{
						scanKey = so->keys[i];
						break;
					}
				}

				if (scanKey == NULL)
				{
					elog(ERROR, "cannot order without attribute %d in WHERE clause",
						 key->attnum);
				}
				else if (scanKey->nentries > 1)
				{
					elog(ERROR, "scan key should contain only one value");
				}
				else if (scanKey->nentries == 0)    /* Should not happen */
				{
					elog(ERROR, "scan key should contain key value");
				}

				key->useCurKey = true;
				scanKey->scanEntry[0]->useCurKey = true;
			}

			key->nentries = 0;
			key->nuserentries = 0;

			key->scanEntry = NULL;
			key->entryRes = NULL;
			key->addInfo = NULL;
			key->addInfoIsNull = NULL;

			so->willSort = true;

			return;
		}
		else if (rumstate->canOrdering[attnum - 1] == false)
		{
			elog(ERROR, "doesn't support ordering, check operator class definition");
		}
		else
		{
			int numOrderingArgs = rumstate->orderingFn[attnum - 1].fn_nargs;
			if (numOrderingArgs == 3 || numOrderingArgs == 10)
			{
				/* These are default rum ordering things - let it be */
			}
			else if (numOrderingArgs == 4)
			{
				/* This is ordering by raw key - let it be */
				so->willSort = true;
			}
			else
			{
				elog(ERROR,
					 "doesn't support ordering - ordering function is incorrect, check operator class definition");
			}
		}
	}

	key->nentries = nQueryValues;
	key->nuserentries = nUserQueryValues;
	key->scanEntry = (RumScanEntry *) palloc(sizeof(RumScanEntry) * nQueryValues);
	key->entryRes = (bool *) palloc0(sizeof(bool) * nQueryValues);
	key->addInfo = (Datum *) palloc0(sizeof(Datum) * nQueryValues);
	key->addInfoIsNull = (bool *) palloc(sizeof(bool) * nQueryValues);
	for (i = 0; i < nQueryValues; i++)
	{
		key->addInfoIsNull[i] = true;
	}

	for (i = 0; i < nQueryValues; i++)
	{
		Datum queryKey;
		RumNullCategory queryCategory;
		bool isPartialMatch;
		Pointer this_extra;

		if (i < nUserQueryValues)
		{
			/* set up normal entry using extractQueryFn's outputs */
			queryKey = queryValues[i];
			queryCategory = queryCategories[i];

			/*
			 * check inconsistence related to impossibility to do partial match
			 * and existance of prefix expression in tsquery
			 */
			if (partial_matches && partial_matches[i] &&
				!rumstate->canPartialMatch[attnum - 1])
			{
				elog(ERROR, "Compare with prefix expressions isn't supported");
			}

			isPartialMatch = (partial_matches) ? partial_matches[i] : false;
			this_extra = (extra_data) ? extra_data[i] : NULL;
		}
		else
		{
			/* set up hidden entry */
			queryKey = (Datum) 0;
			switch (searchMode)
			{
				case GIN_SEARCH_MODE_INCLUDE_EMPTY:
				{
					queryCategory = RUM_CAT_EMPTY_ITEM;
					break;
				}

				case GIN_SEARCH_MODE_ALL:
				{
					queryCategory = RUM_CAT_EMPTY_QUERY;
					break;
				}

				case GIN_SEARCH_MODE_EVERYTHING:
				{
					queryCategory = RUM_CAT_EMPTY_QUERY;
					break;
				}

				default:
				{
					elog(ERROR, "unexpected searchMode: %d", searchMode);
					queryCategory = 0;  /* keep compiler quiet */
					break;
				}
			}
			isPartialMatch = false;
			this_extra = NULL;

			/*
			 * We set the strategy to a fixed value so that rumFillScanEntry
			 * can combine these entries for different scan keys.  This is
			 * safe because the strategy value in the entry struct is only
			 * used for partial-match cases.  It's OK to overwrite our local
			 * variable here because this is the last loop iteration.
			 */
			strategy = InvalidStrategy;
		}

		key->scanEntry[i] = rumFillScanEntry(so, attnum,
											 strategy, searchMode,
											 queryKey, queryCategory,
											 isPartialMatch, this_extra);
	}
}


static void
freeScanEntries(RumScanEntry *entries, uint32 nentries)
{
	uint32 i;

	for (i = 0; i < nentries; i++)
	{
		RumScanEntry entry = entries[i];

		if (entry->gdi)
		{
			freeRumBtreeStack(entry->gdi->stack);
			pfree(entry->gdi);
		}
		else
		{
			if (entry->buffer != InvalidBuffer)
			{
				ReleaseBuffer(entry->buffer);
			}
		}
		if (entry->stack)
		{
			freeRumBtreeStack(entry->stack);
		}
		if (entry->list)
		{
			pfree(entry->list);
		}
		if (entry->matchSortstate)
		{
			rum_tuplesort_end(entry->matchSortstate);
		}
		pfree(entry);
	}
}


void
freeScanKeys(RumScanOpaque so)
{
	if (RumEnableSupportDeadIndexItems &&
		so->orderByScanData && so->numKilled > 0 &&
		so->orderByScanData->isPageValid &&
		so->orderByScanData->orderByEntryPageCopy &&
		so->orderByScanData->orderStack)
	{
		/* last chance to kill entries - needs to be called
		 * before freeScanEntries releases buffer pins.
		 */
		LockBuffer(so->orderByScanData->orderStack->buffer, RUM_SHARE);
		RumKillEntryItems(so, so->orderByScanData);
		LockBuffer(so->orderByScanData->orderStack->buffer, RUM_UNLOCK);
	}

	freeScanEntries(so->entries, so->totalentries);

	if (so->orderByScanData)
	{
		if (so->orderByScanData->orderStack)
		{
			freeRumBtreeStack(so->orderByScanData->orderStack);
		}

		if (so->orderByScanData->orderByEntryPageCopy)
		{
			pfree(so->orderByScanData->orderByEntryPageCopy);
		}

		pfree(so->orderByScanData);
		so->orderByScanData = NULL;
	}

	if (so->killedItems != NULL)
	{
		pfree(so->killedItems);
		so->killedItems = NULL;
		so->numKilled = 0;
	}

	MemoryContextReset(so->keyCtx);
	so->keys = NULL;
	so->nkeys = 0;

	if (so->sortedEntries)
	{
		pfree(so->sortedEntries);
	}
	so->entries = NULL;
	so->sortedEntries = NULL;
	so->totalentries = 0;

	if (so->sortstate)
	{
		rum_tuplesort_end(so->sortstate);
		so->sortstate = NULL;
	}
}


static void
initScanKey(RumScanOpaque so, ScanKey skey, bool *hasPartialMatch, bool hasOrdering, bool
			hasParallel)
{
	Datum *queryValues;
	int32 nQueryValues = 0;
	bool *partial_matches = NULL;
	Pointer *extra_data = NULL;
	bool *nullFlags = NULL;
	int32 searchMode = GIN_SEARCH_MODE_DEFAULT;

	/* Only apply the search mode when it's safe */
	if ((hasOrdering || RumForceOrderedIndexScan || so->projectIndexTupleData ||
		 hasParallel) &&
		so->rumstate.canOrdering[skey->sk_attno - 1] &&
		so->rumstate.orderingFn[skey->sk_attno - 1].fn_nargs == 4)
	{
		/* Let extractQuery know we're doing an ordered scan */
		searchMode = GIN_SEARCH_MODE_ALL;
	}

	/*
	 * We assume that RUM-indexable operators are strict, so a null query
	 * argument means an unsatisfiable query.
	 */
	if (skey->sk_flags & SK_ISNULL)
	{
		/* Do not set isVoidRes for order keys */
		if ((skey->sk_flags & SK_ORDER_BY) == 0)
		{
			so->isVoidRes = true;
		}
		return;
	}

	/* OK to call the extractQueryFn */
	queryValues = (Datum *)
				  DatumGetPointer(FunctionCall7Coll(
									  &so->rumstate.extractQueryFn[skey->sk_attno - 1],
									  so->rumstate.supportCollation[skey->
																	sk_attno - 1],
									  skey->sk_argument,
									  PointerGetDatum(&nQueryValues),
									  UInt16GetDatum(skey->sk_strategy),
									  PointerGetDatum(&partial_matches),
									  PointerGetDatum(&extra_data),
									  PointerGetDatum(&nullFlags),
									  PointerGetDatum(&searchMode)));

	/*
	 * If bogus searchMode is returned, treat as RUM_SEARCH_MODE_ALL; note in
	 * particular we don't allow extractQueryFn to select
	 * RUM_SEARCH_MODE_EVERYTHING.
	 */
	if (searchMode < GIN_SEARCH_MODE_DEFAULT ||
		searchMode > GIN_SEARCH_MODE_ALL)
	{
		searchMode = GIN_SEARCH_MODE_ALL;
	}

	/*
	 * In default mode, no keys means an unsatisfiable query.
	 */
	if (queryValues == NULL || nQueryValues <= 0)
	{
		if (searchMode == GIN_SEARCH_MODE_DEFAULT)
		{
			/* Do not set isVoidRes for order keys */
			if ((skey->sk_flags & SK_ORDER_BY) == 0)
			{
				so->isVoidRes = true;
			}
			return;
		}
		nQueryValues = 0;       /* ensure sane value */
	}

	/*
	 * If the extractQueryFn didn't create a nullFlags array, create one,
	 * assuming that everything's non-null.  Otherwise, run through the array
	 * and make sure each value is exactly 0 or 1; this ensures binary
	 * compatibility with the RumNullCategory representation. While at it,
	 * detect whether any null keys are present.
	 */
	if (nullFlags == NULL)
	{
		nullFlags = (bool *) palloc0(nQueryValues * sizeof(bool));
	}
	else
	{
		int32 j;

		for (j = 0; j < nQueryValues; j++)
		{
			if (nullFlags[j])
			{
				nullFlags[j] = true;    /* not any other nonzero value */
			}
		}
	}

	/* now we can use the nullFlags as category codes */

	rumFillScanKey(so, skey->sk_attno,
				   skey->sk_strategy, searchMode,
				   skey->sk_argument, nQueryValues,
				   queryValues, (RumNullCategory *) nullFlags,
				   partial_matches, extra_data,
				   (skey->sk_flags & SK_ORDER_BY) ? true : false);

	if (partial_matches && hasPartialMatch)
	{
		int32 j;
		RumScanKey key = so->keys[so->nkeys - 1];

		for (j = 0; *hasPartialMatch == false && j < key->nentries; j++)
		{
			*hasPartialMatch |= key->scanEntry[j]->isPartialMatch;
		}
	}
}


static ScanDirection
lookupScanDirection(RumState *state, AttrNumber attno, StrategyNumber strategy)
{
	int i;
	RumConfig *rumConfig = state->rumConfig + attno - 1;

	for (i = 0; i < MAX_STRATEGIES; i++)
	{
		if (rumConfig->strategyInfo[i].strategy != InvalidStrategy)
		{
			break;
		}
		if (rumConfig->strategyInfo[i].strategy == strategy)
		{
			return rumConfig->strategyInfo[i].direction;
		}
	}

	return NoMovementScanDirection;
}


static void
fillMarkAddInfo(RumScanOpaque so, RumScanKey orderKey)
{
	int i;

	for (i = 0; i < so->nkeys; i++)
	{
		RumScanKey scanKey = so->keys[i];
		ScanDirection scanDirection;

		if (scanKey->orderBy)
		{
			continue;
		}

		if (scanKey->attnum == so->rumstate.attrnAddToColumn &&
			orderKey->attnum == so->rumstate.attrnAddToColumn &&
			(scanDirection = lookupScanDirection(&so->rumstate,
												 orderKey->attnumOrig,
												 orderKey->strategy)) !=
			NoMovementScanDirection)
		{
			int j;

			if (so->naturalOrder != NoMovementScanDirection &&
				so->naturalOrder != scanDirection)
			{
				elog(ERROR, "Could not scan in differ directions at the same time");
			}

			for (j = 0; j < scanKey->nentries; j++)
			{
				RumScanEntry scanEntry = scanKey->scanEntry[j];

				if (scanEntry->useMarkAddInfo)
				{
					elog(ERROR, "could not order by more than one operator");
				}
				scanEntry->useMarkAddInfo = true;
				scanEntry->markAddInfo.addInfoIsNull = false;
				scanEntry->markAddInfo.addInfo = orderKey->queryValues[0];
				scanEntry->scanDirection = scanDirection;
			}

			scanKey->scanDirection = scanDirection;
			so->naturalOrder = scanDirection;
		}
	}
}


static void
adjustScanDirection(RumScanOpaque so)
{
	int i;

	if (so->naturalOrder == NoMovementScanDirection)
	{
		return;
	}

	for (i = 0; i < so->nkeys; i++)
	{
		RumScanKey scanKey = so->keys[i];

		if (scanKey->orderBy)
		{
			continue;
		}

		if (scanKey->attnum == so->rumstate.attrnAddToColumn)
		{
			if (scanKey->scanDirection != so->naturalOrder)
			{
				int j;

				if (scanKey->scanDirection != NoMovementScanDirection)
				{
					elog(ERROR, "Could not scan in differ directions at the same time");
				}

				scanKey->scanDirection = so->naturalOrder;
				for (j = 0; j < scanKey->nentries; j++)
				{
					RumScanEntry scanEntry = scanKey->scanEntry[j];

					scanEntry->scanDirection = so->naturalOrder;
				}
			}
		}
	}
}


void
rumNewScanKey(IndexScanDesc scan)
{
	RumScanOpaque so = (RumScanOpaque) scan->opaque;
	int i;
	bool checkEmptyEntry = false;
	bool hasPartialMatch = false;
	bool hasOrderBy = scan->numberOfOrderBys > 0;
	bool hasParallel = scan->parallel_scan != NULL;
	MemoryContext oldCtx;
	enum
	{
		haofNone = 0x00,
		haofHasAddOnRestriction = 0x01,
		haofHasAddToRestriction = 0x02
	}
	hasAddOnFilter = haofNone;

	so->naturalOrder = NoMovementScanDirection;
	so->useSimpleScan = false;
	so->secondPass = false;
	so->orderByHasRecheck = false;
	so->entriesIncrIndex = -1;
	so->norderbys = scan->numberOfOrderBys;
	so->willSort = false;
	so->orderByScanData = NULL;
	so->projectIndexTupleData = NULL;

	/*
	 * Allocate all the scan key information in the key context. (If
	 * extractQuery leaks anything there, it won't be reset until the end of
	 * scan or rescan, but that's OK.)
	 */
	oldCtx = MemoryContextSwitchTo(so->keyCtx);

	/* if no scan keys provided, allocate extra EVERYTHING RumScanKey */
	so->keys = (RumScanKey *)
			   palloc((Max(scan->numberOfKeys, 1) + scan->numberOfOrderBys) *
					  sizeof(*so->keys));
	so->nkeys = 0;

	so->isVoidRes = false;

	for (i = 0; i < scan->numberOfKeys; i++)
	{
		initScanKey(so, &scan->keyData[i], &hasPartialMatch, hasOrderBy, hasParallel);
		if (so->isVoidRes)
		{
			break;
		}
	}

	/*
	 * If there are no regular scan keys, generate an EVERYTHING scankey to
	 * drive a full-index scan.
	 */
	if (so->nkeys == 0 && !so->isVoidRes)
	{
		rumFillScanKey(so, FirstOffsetNumber,
					   InvalidStrategy,
					   GIN_SEARCH_MODE_EVERYTHING,
					   (Datum) 0, 0,
					   NULL, NULL, NULL, NULL, false);
		checkEmptyEntry = true;
	}

	if (scan->numberOfOrderBys > 0)
	{
		/* Store the first order by key index here */
		/* We enforce that we have a prefix equality in this case in the layer above */
		so->orderByKeyIndex = so->nkeys;
		for (i = 0; i < scan->numberOfOrderBys; i++)
		{
			initScanKey(so, &scan->orderByData[i], NULL, hasOrderBy, hasParallel);
		}
	}

	/*
	 * Fill markAddInfo if possible
	 */
	for (i = 0; i < so->nkeys && so->rumstate.useAlternativeOrder; i++)
	{
		RumScanKey key = so->keys[i];

		if (so->rumstate.useAlternativeOrder &&
			key->orderBy && key->useAddToColumn &&
			key->attnum == so->rumstate.attrnAddToColumn)
		{
			fillMarkAddInfo(so, key);
		}

		if (key->orderBy == false)
		{
			if (key->attnumOrig == so->rumstate.attrnAddToColumn)
			{
				hasAddOnFilter |= haofHasAddToRestriction;
			}
			if (key->attnumOrig == so->rumstate.attrnAttachColumn)
			{
				hasAddOnFilter |= haofHasAddOnRestriction;
			}
		}

		key->willSort = so->willSort;
	}

	if ((hasAddOnFilter & haofHasAddToRestriction) &&
		(hasAddOnFilter & haofHasAddOnRestriction))
	{
		RumScanKey *keys = palloc(sizeof(*keys) * so->nkeys);
		int nkeys = 0,
			j;
		RumScanKey addToKey = NULL;

		for (i = 0; i < so->nkeys; i++)
		{
			RumScanKey key = so->keys[i];

			if (key->orderBy == false &&
				key->attnumOrig == so->rumstate.attrnAttachColumn)
			{
				for (j = 0; addToKey == NULL && j < so->nkeys; j++)
				{
					if (so->keys[j]->orderBy == false &&
						so->keys[j]->attnumOrig == so->rumstate.attrnAddToColumn)
					{
						addToKey = so->keys[j];

						addToKey->addInfoKeys =
							palloc(sizeof(*addToKey->addInfoKeys) * so->nkeys);
					}
				}

				if (addToKey == NULL)
				{
					keys[nkeys++] = key;
				}
				else
				{
					addToKey->addInfoKeys[addToKey->addInfoNKeys++] = key;
				}
			}
			else
			{
				keys[nkeys++] = key;
			}
		}

		pfree(so->keys);
		so->keys = keys;
		so->nkeys = nkeys;
	}

	adjustScanDirection(so);

	/* initialize expansible array of RumScanEntry pointers */
	so->totalentries = 0;
	so->allocentries = 32;
	so->entries = (RumScanEntry *)
				  palloc(so->allocentries * sizeof(RumScanEntry));
	so->sortedEntries = NULL;

	for (i = 0; i < so->nkeys; i++)
	{
		RumScanKey key = so->keys[i];

		/* Add it to so's array */
		while (so->totalentries + key->nentries >= so->allocentries)
		{
			so->allocentries *= 2;
			so->entries = (RumScanEntry *)
						  repalloc(so->entries, so->allocentries * sizeof(RumScanEntry));
		}

		if (key->scanEntry != NULL)
		{
			memcpy(so->entries + so->totalentries,
				   key->scanEntry, sizeof(*key->scanEntry) * key->nentries);
			so->totalentries += key->nentries;
		}
	}

	/*
	 * If there are order-by keys, mark empty entry for scan with add info.
	 * If so->nkeys > 1 then there are order-by keys.
	 */
	if (checkEmptyEntry && so->nkeys > 1)
	{
		Assert(so->totalentries > 0);
		so->entries[0]->scanWithAddInfo = true;
	}

	if (scan->numberOfOrderBys > 0)
	{
		scan->xs_orderbyvals = palloc0(sizeof(Datum) * scan->numberOfOrderBys);
		scan->xs_orderbynulls = palloc(sizeof(bool) * scan->numberOfOrderBys);
		memset(scan->xs_orderbynulls, true, sizeof(bool) *
			   scan->numberOfOrderBys);
	}

	if (scan->xs_want_itup)
	{
		char *attributeName = NULL;
		int attributeTypeModifier = -1;
		int numDimensions = 0;
		int natts = RelationGetNumberOfAttributes(scan->indexRelation);

		so->projectIndexTupleData = palloc0(sizeof(RumProjectIndexTupleData));
		so->projectIndexTupleData->iscan_tuple = NULL;
		so->projectIndexTupleData->indexTupleDatum = (Datum) 0;

		so->projectIndexTupleData->indexTupleDesc = CreateTemplateTupleDesc(natts);
		for (i = 0; i < natts; i++)
		{
			TupleDescInitEntry(so->projectIndexTupleData->indexTupleDesc, (AttrNumber) i +
							   1, attributeName,
							   scan->indexRelation->rd_opcintype[i],
							   attributeTypeModifier,
							   numDimensions);
		}

		scan->xs_itupdesc = so->projectIndexTupleData->indexTupleDesc;
	}

	MemoryContextSwitchTo(oldCtx);

	pgstat_count_index_scan(scan->indexRelation);
}


Size
#if PG_VERSION_NUM >= 180000
rumestimateparallelscan(Relation rel, int nkeys, int norderbys)
#elif PG_VERSION_NUM >= 170000
rumestimateparallelscan(int nkeys, int norderbys)
#else
rumestimateparallelscan(void)
#endif
{
	return sizeof(RumParallelScanDescData);
}


void
ruminitparallelscan(void *target)
{
	RumParallelScanDescData *rum_ps_target = (RumParallelScanDescData *) target;

#if PG_VERSION_NUM < 180000
	if (!TrancheRegistered)
	{
		LWLockRegisterTranche(RumParallelScanTrancheId, RumParallelScanTrancheName);
		TrancheRegistered = true;
	}
#endif

	LWLockInitialize(&rum_ps_target->rum_ps_lock, RumParallelScanTrancheId);
	rum_ps_target->rum_ps_current_page = InvalidBlockNumber;
	rum_ps_target->parallel_scan_state = RumParallelScanState_NotInitialized;
	rum_ps_target->isParallelScanEligible = false;
	ConditionVariableInit(&rum_ps_target->rum_ps_cv);
}


void
rumparallelrescan(IndexScanDesc scan)
{
	RumParallelScanDescData *psdata;

	ParallelIndexScanDesc parallel_scan = scan->parallel_scan;

	Assert(parallel_scan);

	psdata = (RumParallelScanDescData *) ParallelScanGetOpaque(parallel_scan);

	/*
	 * In theory, we don't need to acquire the spinlock here, because there
	 * shouldn't be any other workers running at this point, but we do so for
	 * consistency.
	 */
	LWLockAcquire(&psdata->rum_ps_lock, LW_EXCLUSIVE);
	psdata->rum_ps_current_page = InvalidBlockNumber;
	psdata->parallel_scan_state = RumParallelScanState_NotInitialized;
	psdata->isParallelScanEligible = false;
	LWLockRelease(&psdata->rum_ps_lock);
}


bool
rum_parallel_scan_start(IndexScanDesc scan, bool *startScan)
{
	RumParallelScanDescData *psdata;
	bool result = false;
	bool exitLoop = false;
	ParallelIndexScanDesc parallel_scan = scan->parallel_scan;

	Assert(parallel_scan);

	psdata = (RumParallelScanDescData *) ParallelScanGetOpaque(parallel_scan);

	while (!exitLoop)
	{
		CHECK_FOR_INTERRUPTS();
		LWLockAcquire(&psdata->rum_ps_lock, LW_EXCLUSIVE);
		switch (psdata->parallel_scan_state)
		{
			case RumParallelScanState_NotInitialized:
			{
				/* First thread to get here - start parallel scan */
				psdata->parallel_scan_state = RumParallelScanState_RunningStartScan;
				*startScan = true;
				result = true;
				exitLoop = true;
				break;
			}

			case RumParallelScanState_RunningStartScan:
			{
				/* Another thread is running startScan - let the other thread finish first
				 * before doing anything else.
				 */
				exitLoop = false;
				break;
			}

			/* Any higher states should be considered one thread is done with startScan */
			case RumParallelScanState_StartScanDone:
			default:
			{
				*startScan = false;
				exitLoop = true;
				result = psdata->isParallelScanEligible;
				break;
			}
		}
		LWLockRelease(&psdata->rum_ps_lock);

		if (!exitLoop)
		{
			/* Wait for notification */
			ConditionVariableSleep(&psdata->rum_ps_cv, PG_WAIT_EXTENSION);
		}
	}

	return result;
}


bool
rum_parallel_seize(ParallelIndexScanDesc parallelScan, BlockNumber *blockNumber)
{
	RumParallelScanDescData *psdata;
	bool result = false;
	bool exitLoop = false;

	Assert(parallelScan);

	psdata = (RumParallelScanDescData *) ParallelScanGetOpaque(parallelScan);

	while (!exitLoop)
	{
		CHECK_FOR_INTERRUPTS();
		LWLockAcquire(&psdata->rum_ps_lock, LW_EXCLUSIVE);
		switch (psdata->parallel_scan_state)
		{
			case RumParallelScanState_NotInitialized:
			case RumParallelScanState_RunningStartScan:
			{
				/* Unexpected */
				LWLockRelease(&psdata->rum_ps_lock);
				ereport(ERROR, (errmsg(
									"Parallel scan seize called before initialization. Unexpected")));
				break;
			}

			case RumParallelScanState_StartScanDone:
			case RumParallelScanState_Idle:
			{
				*blockNumber = psdata->rum_ps_current_page;
				result = true;
				exitLoop = true;
				psdata->parallel_scan_state = RumParallelScanState_ScanningTree;
				break;
			}

			case RumParallelScanState_ScanningTree:
			{
				result = false;
				exitLoop = false;
				break;
			}

			case RumParallelScanState_Done:
			{
				result = false;
				exitLoop = true;
				break;
			}

			default:
			{
				LWLockRelease(&psdata->rum_ps_lock);
				ereport(ERROR, (errmsg(
									"Parallel scan seize called with unexpected state %d",
									psdata->parallel_scan_state)));
				break;
			}
		}
		LWLockRelease(&psdata->rum_ps_lock);

		if (!exitLoop)
		{
			/* Wait for notification */
			ConditionVariableSleep(&psdata->rum_ps_cv, PG_WAIT_EXTENSION);
		}
	}

	return result;
}


void
rum_parallel_release(ParallelIndexScanDesc parallelScan, BlockNumber nextBlock)
{
	RumParallelScanDescData *psdata;

	Assert(parallelScan);

	psdata = (RumParallelScanDescData *) ParallelScanGetOpaque(parallelScan);


	LWLockAcquire(&psdata->rum_ps_lock, LW_EXCLUSIVE);
	if (psdata->parallel_scan_state != RumParallelScanState_ScanningTree)
	{
		ereport(ERROR, (errmsg(
							"rum_parallel_release called with unexpected current state %d",
							psdata->parallel_scan_state)));
	}

	if (nextBlock == InvalidBlockNumber)
	{
		psdata->parallel_scan_state = RumParallelScanState_Done;
		psdata->rum_ps_current_page = nextBlock;
	}
	else
	{
		psdata->parallel_scan_state = RumParallelScanState_Idle;
		psdata->rum_ps_current_page = nextBlock;
	}
	LWLockRelease(&psdata->rum_ps_lock);
	ConditionVariableBroadcast(&psdata->rum_ps_cv);
}


bool
rum_parallel_scan_start_notify(IndexScanDesc scan)
{
	RumParallelScanDescData *psdata;
	bool isParallelEnabled = false;
	RumScanOpaque so = (RumScanOpaque) scan->opaque;
	ParallelIndexScanDesc parallel_scan = scan->parallel_scan;

	Assert(parallel_scan);

	psdata = (RumParallelScanDescData *) ParallelScanGetOpaque(parallel_scan);


	LWLockAcquire(&psdata->rum_ps_lock, LW_EXCLUSIVE);
	psdata->parallel_scan_state = RumParallelScanState_StartScanDone;
	psdata->isParallelScanEligible = so->scanType == RumOrderedScan &&
									 ScanDirectionIsForward(so->orderScanDirection);
	psdata->rum_ps_current_page = InvalidBlockNumber;
	isParallelEnabled = psdata->isParallelScanEligible;
	LWLockRelease(&psdata->rum_ps_lock);
	ConditionVariableBroadcast(&psdata->rum_ps_cv);
	return isParallelEnabled;
}


void
rumrescan(IndexScanDesc scan, ScanKey scankey, int nscankeys,
		  ScanKey orderbys, int norderbys)
{
	/* remaining arguments are ignored */
	RumScanOpaque so = (RumScanOpaque) scan->opaque;

	so->firstCall = true;
	so->ignoreKilledTuples = scan->ignore_killed_tuples;

	freeScanKeys(so);

	if (scankey && scan->numberOfKeys > 0)
	{
		memmove(scan->keyData, scankey,
				scan->numberOfKeys * sizeof(ScanKeyData));
	}
	if (orderbys && scan->numberOfOrderBys > 0)
	{
		memmove(scan->orderByData, orderbys,
				scan->numberOfOrderBys * sizeof(ScanKeyData));
	}
}


void
rumendscan(IndexScanDesc scan)
{
	RumScanOpaque so = (RumScanOpaque) scan->opaque;

	freeScanKeys(so);

	MemoryContextDelete(so->tempCtx);
	MemoryContextDelete(so->keyCtx);
	MemoryContextDelete(so->rumStateCtx);

	pfree(so);
}


Datum
rummarkpos(PG_FUNCTION_ARGS)
{
	elog(ERROR, "RUM does not support mark/restore");
	PG_RETURN_VOID();
}


Datum
rumrestrpos(PG_FUNCTION_ARGS)
{
	elog(ERROR, "RUM does not support mark/restore");
	PG_RETURN_VOID();
}


PGDLLEXPORT bool
can_documentdb_rum_index_scan_ordered(IndexScanDesc scan)
{
	RumScanOpaque so = (RumScanOpaque) scan->opaque;
	bool isSupportedOrderedScan = scan->numberOfKeys > 0;
	int i = 0;
	for (i = 0; i < scan->numberOfKeys; i++)
	{
		AttrNumber keyAttr = scan->keyData[i].sk_attno;
		if (keyAttr != scan->keyData[i].sk_attno)
		{
			isSupportedOrderedScan = false;
			break;
		}

		if (!so->rumstate.canPartialMatch[keyAttr - 1] ||
			!so->rumstate.canOrdering[keyAttr - 1] ||
			so->rumstate.orderingFn[keyAttr - 1].fn_nargs != 4)
		{
			isSupportedOrderedScan = false;
			break;
		}
	}

	return isSupportedOrderedScan;
}


extern PGDLLEXPORT void
try_explain_documentdb_rum_index(IndexScanDesc scan, ExplainState *es)
{
	/* This function is called from explain.c */
	int i, j;
	List *entryList = NIL;
	const char *scanType = "unknown";
	RumScanOpaque so = (RumScanOpaque) scan->opaque;
	ExplainPropertyInteger("innerScanLoops", "loops", so->scanLoops, es);

	if (so->killedItemsSkipped > 0)
	{
		ExplainPropertyInteger("deadEntriesOrPagesSkipped", "items",
							   so->killedItemsSkipped, es);
	}

	if (scan->parallel_scan != NULL)
	{
		ExplainPropertyBool("parallelScanCapable", so->isParallelEnabled, es);
	}

	switch (so->scanType)
	{
		case RumFastScan:
		{
			scanType = "fast";
			break;
		}

		case RumFullScan:
		{
			scanType = "full";
			break;
		}

		case RumRegularScan:
		{
			scanType = "regular";
			break;
		}

		case RumOrderedScan:
		{
			scanType = "ordered";
			break;
		}

		default:
		{
			scanType = "unknown";
			break;
		}
	}

	ExplainPropertyText("scanType", scanType, es);
	for (i = 0; i < so->nkeys; i++)
	{
		StringInfo buf = makeStringInfo();
		if (so->keys[i]->orderBy)
		{
			continue;
		}

		appendStringInfo(buf, "key %d: [", i + 1);
		for (j = 0; j < so->keys[i]->nentries; j++)
		{
			RumScanEntry entry = so->keys[i]->scanEntry[j];
			if (j > 0)
			{
				appendStringInfo(buf, ", ");
			}

			appendStringInfo(buf, "(isInequality: %s, estimatedEntryCount: %u)",
							 entry->isPartialMatch ? "true" : "false",
							 entry->predictNumberResult);
		}

		appendStringInfoString(buf, "]");
		entryList = lappend(entryList,
							buf->data);
	}

	ExplainPropertyList("scanKeyDetails", entryList, es);
}
