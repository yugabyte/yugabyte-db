/*-------------------------------------------------------------------------
 *
 * rumget.c
 *	  fetch tuples from a RUM scan.
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 2015-2022, Postgres Professional
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "rumsort.h"

#include "access/relscan.h"
#include "storage/predicate.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#if PG_VERSION_NUM >= 120000
#include "utils/float.h"
#endif
#if PG_VERSION_NUM >= 150000
#include "common/pg_prng.h"
#endif
#include "pg_documentdb_rum.h"

typedef enum RumIndexTransformOperation
{
	RumIndexTransform_IndexGenerateSkipBound = 1
} RumIndexTransformOperation;


/* Scan bounds used in comparePartial initialization */
typedef struct RumItemScanEntryBounds
{
	RumItem minItem;
	RumItem maxItem;
} RumItemScanEntryBounds;

/* GUC parameter */
extern int RumFuzzySearchLimit;
extern bool RumDisableFastScan;
extern bool RumForceOrderedIndexScan;
extern bool RumPreferOrderedIndexScan;
extern bool RumEnableSkipIntermediateEntry;

static bool scanPage(RumState *rumstate, RumScanEntry entry, RumItem *item,
					 bool equalOk);
static void insertScanItem(RumScanOpaque so, bool recheck);
static int scan_entry_cmp(const void *p1, const void *p2, void *arg);
static void entryGetItem(RumState *rumstate, RumScanEntry entry, bool *nextEntryList,
						 Snapshot snapshot, RumItem *advancePast,
						 RumScanOpaque so);
static void entryFindItem(RumState *rumstate, RumScanEntry entry, RumItem *item, Snapshot
						  snapshot);

/*
 * Extract key value for ordering.
 *
 * XXX FIXME only pass-by-value!!! Value should be copied to
 * long-lived memory context and, somehow, freeed. Seems, the
 * last is real problem.
 */
#define SCAN_ENTRY_GET_KEY(entry, rumstate, itup) \
	do { \
		if ((entry)->useCurKey) { \
			(entry)->curKey = rumtuple_get_key(rumstate, itup, &(entry)->curKeyCategory); \
		} \
	} while (0)

/*
 * Assign key value for ordering.
 *
 * XXX FIXME only pass-by-value!!! Value should be copied to
 * long-lived memory context and, somehow, freeed. Seems, the
 * last is real problem.
 */
#define SCAN_ITEM_PUT_KEY(entry, item, key, category) \
	do { \
		if ((entry)->useCurKey) \
		{ \
			(item).keyValue = key; \
			(item).keyCategory = category; \
		} \
	} while (0)


inline static bool
IsEntryDeadForKilledTuple(bool ignoreKilledTuples, ItemId itemId)
{
	return RumEnableSupportDeadIndexItems && ignoreKilledTuples && \
		   RumIndexEntryIsDead(itemId);
}


inline static bool
IsDataPageDeadForKilledTuple(bool ignoreKilledTuples, Page dataPage)
{
	return RumEnableSupportDeadIndexItems && ignoreKilledTuples && \
		   RumDataPageEntryIsDead(dataPage);
}


static bool
callAddInfoConsistentFn(RumState *rumstate, RumScanKey key)
{
	uint32 i;
	bool res = true;

	/* it should be true for search key, but it could be false for order key */
	Assert(key->attnum == key->attnumOrig);

	if (key->attnum != rumstate->attrnAddToColumn)
	{
		return true;
	}

	/*
	 * remember some addinfo value for later ordering by addinfo from
	 * another column
	 */

	key->outerAddInfoIsNull = true;

	if (key->addInfoKeys == NULL && key->willSort == false)
	{
		return true;
	}

	for (i = 0; i < key->nentries; i++)
	{
		if (key->entryRes[i] && key->addInfoIsNull[i] == false)
		{
			key->outerAddInfoIsNull = false;

			/*
			 * XXX FIXME only pass-by-value!!! Value should be copied to
			 * long-lived memory context and, somehow, freeed. Seems, the
			 * last is real problem.
			 * But actually it's a problem only for ordering, as restricting
			 * clause it used only inside this function.
			 */
			key->outerAddInfo = key->addInfo[i];
			break;
		}
	}

	if (key->addInfoKeys)
	{
		if (key->outerAddInfoIsNull)
		{
			res = false; /* assume strict operator */
		}
		for (i = 0; res && i < key->addInfoNKeys; i++)
		{
			RumScanKey subkey = key->addInfoKeys[i];
			int j;

			for (j = 0; res && j < subkey->nentries; j++)
			{
				RumScanEntry scanSubEntry = subkey->scanEntry[j];
				int cmp =
					DatumGetInt32(FunctionCall4Coll(
									  &rumstate->comparePartialFn[
										  scanSubEntry->attnumOrig - 1],
									  rumstate->supportCollation[
										  scanSubEntry->attnumOrig - 1],
									  scanSubEntry->queryKey,
									  key->outerAddInfo,
									  UInt16GetDatum(scanSubEntry->strategy),
									  PointerGetDatum(scanSubEntry->extra_data)
									  ));

				if (cmp != 0)
				{
					res = false;
				}
			}
		}
	}

	return res;
}


/*
 * Convenience function for invoking a key's consistentFn
 */
static bool
callConsistentFn(RumState *rumstate, RumScanKey key)
{
	bool res;

	/* it should be true for search key, but it could be false for order key */
	Assert(key->attnum == key->attnumOrig);

	/*
	 * If we're dealing with a dummy EVERYTHING key, we don't want to call the
	 * consistentFn; just claim it matches.
	 */
	if (key->searchMode == GIN_SEARCH_MODE_EVERYTHING)
	{
		key->recheckCurItem = false;
		res = true;
	}
	else
	{
		/*
		 * Initialize recheckCurItem in case the consistentFn doesn't know it
		 * should set it.  The safe assumption in that case is to force
		 * recheck.
		 */
		key->recheckCurItem = true;

		res = DatumGetBool(FunctionCall10Coll(&rumstate->consistentFn[key->attnum - 1],
											  rumstate->supportCollation[key->attnum - 1],
											  PointerGetDatum(key->entryRes),
											  UInt16GetDatum(key->strategy),
											  key->query,
											  UInt32GetDatum(key->nuserentries),
											  PointerGetDatum(key->extra_data),
											  PointerGetDatum(&key->recheckCurItem),
											  PointerGetDatum(key->queryValues),
											  PointerGetDatum(key->queryCategories),
											  PointerGetDatum(key->addInfo),
											  PointerGetDatum(key->addInfoIsNull)
											  ));
	}

	return res && callAddInfoConsistentFn(rumstate, key);
}


/*
 * Goes to the next page if current offset is outside of bounds
 */
static bool
moveRightIfItNeeded(RumBtreeData *btree, RumBtreeStack *stack)
{
	Page page = BufferGetPage(stack->buffer);

	if (stack->off > PageGetMaxOffsetNumber(page))
	{
		/*
		 * We scanned the whole page, so we should take right page
		 */
		if (RumPageRightMost(page))
		{
			return false;       /* no more pages */
		}
		stack->buffer = rumStep(stack->buffer, btree->index, RUM_SHARE,
								ForwardScanDirection);
		stack->blkno = BufferGetBlockNumber(stack->buffer);
		stack->off = FirstOffsetNumber;
	}

	return true;
}


/*
 * Identify the "current" item among the input entry streams for this scan key,
 * and test whether it passes the scan key qual condition.
 *
 * The current item is the smallest curItem among the inputs.  key->curItem
 * is set to that value.  key->curItemMatches is set to indicate whether that
 * TID passes the consistentFn test.  If so, key->recheckCurItem is set true
 * iff recheck is needed for this item pointer
 *
 * If all entry streams are exhausted, sets key->isFinished to true.
 *
 * Item pointers must be returned in ascending order.
 */
static int
compareRumItemScanDirection(RumState *rumstate, AttrNumber attno,
							ScanDirection scanDirection,
							RumItem *a, RumItem *b)
{
	int res = compareRumItem(rumstate, attno, a, b);

	return (ScanDirectionIsForward(scanDirection)) ? res : -res;
}


static int
compareCurRumItemScanDirection(RumState *rumstate, RumScanEntry entry,
							   RumItem *minItem)
{
	return compareRumItemScanDirection(rumstate,
									   entry->attnumOrig,
									   entry->scanDirection,
									   &entry->curItem, minItem);
}


inline static bool
IsEntryInBounds(RumState *rumstate, RumScanEntry scanEntry,
				RumItem *item, RumItemScanEntryBounds *scanEntryBounds,
				bool checkMaximum)
{
	Assert(ItemPointerIsValid(&scanEntryBounds->minItem.iptr));
	if (compareRumItem(rumstate, scanEntry->attnumOrig,
					   item, &scanEntryBounds->minItem) < 0)
	{
		return false;
	}

	if (checkMaximum &&
		ItemPointerIsValid(&scanEntryBounds->maxItem.iptr) &&
		compareRumItem(rumstate, scanEntry->attnumOrig,
					   item, &scanEntryBounds->maxItem) > 0)
	{
		return false;
	}

	return true;
}


/*
 * Scan all pages of a posting tree and save all its heap ItemPointers
 * in scanEntry->matchSortstate
 */
static void
scanPostingTree(Relation index, RumScanEntry scanEntry,
				BlockNumber rootPostingTree, OffsetNumber attnum,
				RumState *rumstate, Datum idatum, RumNullCategory icategory,
				Snapshot snapshot, bool ignoreKilledTuples,
				RumItemScanEntryBounds *scanEntryBounds)
{
	RumPostingTreeScan *gdi;
	Buffer buffer;
	Page page;

	Assert(ScanDirectionIsForward(scanEntry->scanDirection));

	/* Descend to the leftmost leaf page */
	gdi = rumPrepareScanPostingTree(index, rootPostingTree, true,
									ForwardScanDirection, attnum, rumstate);

	buffer = rumScanBeginPostingTree(gdi, NULL);

	IncrBufferRefCount(buffer); /* prevent unpin in freeRumBtreeStack */

	PredicateLockPage(index, BufferGetBlockNumber(buffer), snapshot);

	freeRumBtreeStack(gdi->stack);
	pfree(gdi);

	/*
	 * Loop iterates through all leaf pages of posting tree
	 */
	for (;;)
	{
		OffsetNumber maxoff,
					 i;
		bool shouldScanPage = true;

		page = BufferGetPage(buffer);
		maxoff = RumDataPageMaxOff(page);

		if (scanEntryBounds != NULL &&
			RumPageIsNotDeleted(page) &&
			maxoff >= FirstOffsetNumber && !RumPageRightMost(page))
		{
			/* For page level checks, we only check the minimum. i.e.
			 * is the Right-bound (max item in the page) less than the
			 * min possible item pointer. We don't use max here as that is
			 * left to the individual tuples.
			 */
			bool checkMaximum = false;
			shouldScanPage = IsEntryInBounds(rumstate, scanEntry,
											 RumDataPageGetRightBound(page),
											 scanEntryBounds, checkMaximum);
		}

		if (IsDataPageDeadForKilledTuple(ignoreKilledTuples, page))
		{
			shouldScanPage = false;
		}

		if (shouldScanPage && RumPageIsNotDeleted(page) && maxoff >= FirstOffsetNumber)
		{
			bool checkMaximum = true;
			RumScanItem item;
			Pointer ptr;

			MemSet(&item, 0, sizeof(item));
			ItemPointerSetMin(&item.item.iptr);

			ptr = RumDataPageGetData(page);
			for (i = FirstOffsetNumber; i <= maxoff; i++)
			{
				ptr = rumDataPageLeafRead(ptr, attnum, &item.item, false,
										  rumstate);

				if (scanEntryBounds != NULL &&
					!IsEntryInBounds(rumstate, scanEntry, &item.item, scanEntryBounds,
									 checkMaximum))
				{
					continue;
				}

				if (scanEntry->isMatchMinimalTuple)
				{
					rum_tuplesort_putrumitem_minimal(scanEntry->matchSortstate,
													 &item.item.iptr);
				}
				else
				{
					SCAN_ITEM_PUT_KEY(scanEntry, item, idatum, icategory);
					rum_tuplesort_putrumitem(scanEntry->matchSortstate, &item);
				}

				scanEntry->predictNumberResult++;
			}
		}

		if (RumPageRightMost(page))
		{
			break;              /* no more pages */
		}
		buffer = rumStep(buffer, index, RUM_SHARE, ForwardScanDirection);

		PredicateLockPage(index, BufferGetBlockNumber(buffer), snapshot);
	}

	UnlockReleaseBuffer(buffer);
}


/*
 * Collects TIDs into scanEntry->matchSortstate for all heap tuples that
 * match the search entry.  This supports three different match modes:
 *
 * 1. Partial-match support: scan from current point until the
 *	  comparePartialFn says we're done.
 * 2. SEARCH_MODE_ALL: scan from current point (which should be first
 *	  key for the current attnum) until we hit null items or end of attnum
 * 3. SEARCH_MODE_EVERYTHING: scan from current point (which should be first
 *	  key for the current attnum) until we hit end of attnum
 *
 * Returns true if done, false if it's necessary to restart scan from scratch
 */
static bool
collectMatchBitmap(RumBtreeData *btree, RumBtreeStack *stack,
				   bool ignoreKilledTuples, RumScanEntry scanEntry, Snapshot snapshot,
				   RumItemScanEntryBounds *scanEntryBounds)
{
	OffsetNumber attnum;
	Form_pg_attribute attr;
	FmgrInfo *cmp = NULL;
	RumState *rumstate = btree->rumstate;

	if (rumstate->useAlternativeOrder &&
		scanEntry->attnumOrig == rumstate->attrnAddToColumn)
	{
		cmp = &rumstate->compareFn[rumstate->attrnAttachColumn - 1];
	}

	/* Initialize  */
	if (!rumstate->useAlternativeOrder &&
		!scanEntry->useCurKey && !scanEntry->scanWithAddInfo)
	{
		scanEntry->matchSortstate =
			rum_tuplesort_begin_rumitem_minimal(work_mem, cmp);
		scanEntry->isMatchMinimalTuple = true;
	}
	else
	{
		scanEntry->matchSortstate = rum_tuplesort_begin_rumitem(work_mem, cmp);
		scanEntry->isMatchMinimalTuple = false;
	}

	/* Null query cannot partial-match anything */
	if (scanEntry->isPartialMatch &&
		scanEntry->queryCategory != RUM_CAT_NORM_KEY)
	{
		return true;
	}

	/* Locate tupdesc entry for key column (for attbyval/attlen data) */
	attnum = scanEntry->attnumOrig;
	attr = RumTupleDescAttr(rumstate->origTupdesc, attnum - 1);

	for (;;)
	{
		Page page;
		IndexTuple itup;
		Datum idatum;
		ItemId itemId;
		RumNullCategory icategory;

		/*
		 * stack->off points to the interested entry, buffer is already locked
		 */
		if (moveRightIfItNeeded(btree, stack) == false)
		{
			return true;
		}

		page = BufferGetPage(stack->buffer);
		itemId = PageGetItemId(page, stack->off);
		itup = (IndexTuple) PageGetItem(page, itemId);

		/*
		 * If tuple stores another attribute then stop scan
		 */
		if (rumtuple_get_attrnum(rumstate, itup) != attnum)
		{
			return true;
		}

		/* If the request is to ignore killed tuples, check that */
		if (IsEntryDeadForKilledTuple(ignoreKilledTuples, itemId))
		{
			stack->off++;
			continue;
		}

		/* Safe to fetch attribute value */
		idatum = rumtuple_get_key(rumstate, itup, &icategory);

		/*
		 * Check for appropriate scan stop conditions
		 */
		if (scanEntry->isPartialMatch)
		{
			int32 cmp;

			/*
			 * In partial match, stop scan at any null (including
			 * placeholders); partial matches never match nulls
			 */
			if (icategory != RUM_CAT_NORM_KEY)
			{
				return true;
			}

			/*----------
			 * Check of partial match.
			 * case cmp == 0 => match
			 * case cmp > 0 => not match and finish scan
			 * case cmp < 0 => not match and continue scan
			 *----------
			 */
			cmp = DatumGetInt32(FunctionCall4Coll(&rumstate->comparePartialFn[attnum - 1],
												  rumstate->supportCollation[attnum - 1],
												  scanEntry->queryKey,
												  idatum,
												  UInt16GetDatum(scanEntry->strategy),
												  PointerGetDatum(
													  scanEntry->extra_data)));

			if (cmp > 0)
			{
				return true;
			}
			else if (cmp < 0)
			{
				stack->off++;
				continue;
			}
		}
		else if (scanEntry->searchMode == GIN_SEARCH_MODE_ALL)
		{
			/*
			 * In ALL mode, we are not interested in null items, so we can
			 * stop if we get to a null-item placeholder (which will be the
			 * last entry for a given attnum).  We do want to include NULL_KEY
			 * and EMPTY_ITEM entries, though.
			 */
			if (icategory == RUM_CAT_NULL_ITEM)
			{
				return true;
			}
		}

		/*
		 * OK, we want to return the TIDs listed in this entry.
		 */
		if (RumIsPostingTree(itup))
		{
			BlockNumber rootPostingTree = RumGetPostingTree(itup);

			/*
			 * We should unlock current page (but not unpin) during tree scan
			 * to prevent deadlock with vacuum processes.
			 *
			 * We save current entry value (idatum) to be able to re-find our
			 * tuple after re-locking
			 */
			if (icategory == RUM_CAT_NORM_KEY)
			{
				idatum = datumCopy(idatum, attr->attbyval, attr->attlen);
			}

			LockBuffer(stack->buffer, RUM_UNLOCK);

			/* Collect all the TIDs in this entry's posting tree */
			scanPostingTree(btree->index, scanEntry, rootPostingTree, attnum,
							rumstate, idatum, icategory, snapshot, ignoreKilledTuples,
							scanEntryBounds);

			/*
			 * We lock again the entry page and while it was unlocked insert
			 * might have occurred, so we need to re-find our position.
			 */
			LockBuffer(stack->buffer, RUM_SHARE);
			page = BufferGetPage(stack->buffer);
			if (!RumPageIsLeaf(page))
			{
				/*
				 * Root page becomes non-leaf while we unlock it. We will
				 * start again, this situation doesn't occur often - root can
				 * became a non-leaf only once per life of index.
				 */
				return false;
			}

			/* Search forward to re-find idatum */
			for (;;)
			{
				Datum newDatum;
				RumNullCategory newCategory;

				if (moveRightIfItNeeded(btree, stack) == false)
				{
					elog(ERROR, "lost saved point in index");   /* must not happen !!! */
				}
				page = BufferGetPage(stack->buffer);
				itemId = PageGetItemId(page, stack->off);
				itup = (IndexTuple) PageGetItem(page, itemId);

				if (rumtuple_get_attrnum(rumstate, itup) != attnum)
				{
					elog(ERROR, "lost saved point in index");   /* must not happen !!! */
				}

				newDatum = rumtuple_get_key(rumstate, itup,
											&newCategory);

				if (rumCompareEntries(rumstate, attnum,
									  newDatum, newCategory,
									  idatum, icategory) == 0)
				{
					break;      /* Found! */
				}
				stack->off++;
			}

			if (icategory == RUM_CAT_NORM_KEY && !attr->attbyval)
			{
				pfree(DatumGetPointer(idatum));
			}
		}
		else
		{
			int i;
			char *ptr = RumGetPosting(itup);
			RumScanItem item;

			MemSet(&item, 0, sizeof(item));
			ItemPointerSetMin(&item.item.iptr);
			for (i = 0; i < RumGetNPosting(itup); i++)
			{
				bool checkMaximum = true;
				ptr = rumDataPageLeafRead(ptr, scanEntry->attnum, &item.item,
										  true, rumstate);
				if (scanEntryBounds != NULL &&
					!IsEntryInBounds(rumstate, scanEntry, &item.item, scanEntryBounds,
									 checkMaximum))
				{
					continue;
				}

				if (scanEntry->isMatchMinimalTuple)
				{
					rum_tuplesort_putrumitem_minimal(scanEntry->matchSortstate,
													 &item.item.iptr);
				}
				else
				{
					SCAN_ITEM_PUT_KEY(scanEntry, item, idatum, icategory);
					rum_tuplesort_putrumitem(scanEntry->matchSortstate, &item);
				}
			}

			scanEntry->predictNumberResult += RumGetNPosting(itup);
		}

		/*
		 * Done with this entry, go to the next
		 */
		stack->off++;
	}
}


/*
 * set right position in entry->list accordingly to markAddInfo.
 * returns true if there is not such position.
 */
static bool
setListPositionScanEntry(RumState *rumstate, RumScanEntry entry)
{
	OffsetNumber StopLow = entry->offset,
				 StopHigh = entry->nlist;

	if (entry->useMarkAddInfo == false)
	{
		entry->offset = (ScanDirectionIsForward(entry->scanDirection)) ?
						0 : entry->nlist - 1;
		return false;
	}

	while (StopLow < StopHigh)
	{
		int res;

		entry->offset = StopLow + ((StopHigh - StopLow) >> 1);
		res = compareRumItem(rumstate, entry->attnumOrig, &entry->markAddInfo,
							 entry->list + entry->offset);

		if (res < 0)
		{
			StopHigh = entry->offset;
		}
		else if (res > 0)
		{
			StopLow = entry->offset + 1;
		}
		else
		{
			return false;
		}
	}

	if (ScanDirectionIsForward(entry->scanDirection))
	{
		entry->offset = StopHigh;

		return (StopHigh >= entry->nlist);
	}
	else
	{
		if (StopHigh == 0)
		{
			return true;
		}

		entry->offset = StopHigh - 1;

		return false;
	}
}


/*
 * Start* functions setup beginning state of searches: finds correct buffer and pins it.
 * scanEntryBounds is an optional argument that contains min/max bounds if found for the entry
 * used in partialMatch scenarios
 */
static void
startScanEntry(RumState *rumstate, bool ignoreKilledTuples, RumScanEntry entry,
			   Snapshot snapshot, RumItemScanEntryBounds *scanEntryBounds)
{
	RumBtreeData btreeEntry;
	RumBtreeStack *stackEntry;
	Page page;
	bool needUnlock;

restartScanEntry:
	entry->buffer = InvalidBuffer;
	RumItemSetMin(&entry->curItem);
	entry->offset = InvalidOffsetNumber;
	entry->list = NULL;
	entry->gdi = NULL;
	entry->stack = NULL;
	entry->nlist = 0;
	entry->cachedLsn = InvalidXLogRecPtr;
	entry->matchSortstate = NULL;
	entry->reduceResult = false;
	entry->predictNumberResult = 0;

	/*
	 * we should find entry, and begin scan of posting tree or just store
	 * posting list in memory
	 */
	rumPrepareEntryScan(&btreeEntry, entry->attnum,
						entry->queryKey, entry->queryCategory,
						rumstate);
	btreeEntry.searchMode = true;
	stackEntry = rumFindLeafPage(&btreeEntry, NULL);
	page = BufferGetPage(stackEntry->buffer);
	needUnlock = true;

	entry->isFinished = true;

	PredicateLockPage(rumstate->index, BufferGetBlockNumber(stackEntry->buffer),
					  snapshot);

	if (entry->isPartialMatch ||
		(entry->queryCategory == RUM_CAT_EMPTY_QUERY &&
		 !entry->scanWithAddInfo))
	{
		/*
		 * btreeEntry.findItem locates the first item >= given search key.
		 * (For RUM_CAT_EMPTY_QUERY, it will find the leftmost index item
		 * because of the way the RUM_CAT_EMPTY_QUERY category code is
		 * assigned.)  We scan forward from there and collect all TIDs needed
		 * for the entry type.
		 */
		btreeEntry.findItem(&btreeEntry, stackEntry);
		if (collectMatchBitmap(&btreeEntry, stackEntry, ignoreKilledTuples, entry,
							   snapshot,
							   scanEntryBounds) == false)
		{
			/*
			 * RUM tree was seriously restructured, so we will cleanup all
			 * found data and rescan. See comments near 'return false' in
			 * collectMatchBitmap()
			 */
			if (entry->matchSortstate)
			{
				rum_tuplesort_end(entry->matchSortstate);
				entry->matchSortstate = NULL;
			}
			LockBuffer(stackEntry->buffer, RUM_UNLOCK);
			freeRumBtreeStack(stackEntry);
			goto restartScanEntry;
		}

		if (entry->matchSortstate)
		{
			rum_tuplesort_performsort(entry->matchSortstate);
			ItemPointerSetMin(&entry->collectRumItem.item.iptr);
			entry->isFinished = false;
		}
	}
	else if (entry->curKeyCategory == RUM_CAT_ORDER_ITEM)
	{
		ereport(ERROR, (errmsg("Unsupported call startScanEntry on order item key")));
	}
	else if (btreeEntry.findItem(&btreeEntry, stackEntry) ||
			 (entry->queryCategory == RUM_CAT_EMPTY_QUERY &&
			  entry->scanWithAddInfo))
	{
		IndexTuple itup;
		ItemId itemid = PageGetItemId(page, stackEntry->off);

		/*
		 * We don't want to crash if line pointer is not used.
		 */
		if (entry->queryCategory == RUM_CAT_EMPTY_QUERY &&
			!ItemIdHasStorage(itemid))
		{
			goto endScanEntry;
		}

		if (IsEntryDeadForKilledTuple(ignoreKilledTuples, itemid))
		{
			goto endScanEntry;
		}

		itup = (IndexTuple) PageGetItem(page, itemid);

		if (RumIsPostingTree(itup))
		{
			BlockNumber rootPostingTree = RumGetPostingTree(itup);
			RumPostingTreeScan *gdi;
			Page pageInner;
			OffsetNumber maxoff,
						 i;
			Pointer ptr;
			RumItem item;

			ItemPointerSetMin(&item.iptr);

			/*
			 * We should unlock entry page before touching posting tree to
			 * prevent deadlocks with vacuum processes. Because entry is never
			 * deleted from page and posting tree is never reduced to the
			 * posting list, we can unlock page after getting BlockNumber of
			 * root of posting tree.
			 */
			LockBuffer(stackEntry->buffer, RUM_UNLOCK);
			needUnlock = false;
			gdi = rumPrepareScanPostingTree(rumstate->index, rootPostingTree, true,
											entry->scanDirection, entry->attnum,
											rumstate);

			entry->buffer = rumScanBeginPostingTree(gdi, entry->useMarkAddInfo ?
													&entry->markAddInfo : NULL);

			entry->gdi = gdi;

			PredicateLockPage(rumstate->index, BufferGetBlockNumber(entry->buffer),
							  snapshot);

			/*
			 * We keep buffer pinned because we need to prevent deletion of
			 * page during scan. See RUM's vacuum implementation. RefCount is
			 * increased to keep buffer pinned after freeRumBtreeStack() call.
			 */
			pageInner = BufferGetPage(entry->buffer);
			entry->predictNumberResult = gdi->stack->predictNumber * RumDataPageMaxOff(
				pageInner);

			/*
			 * Keep page content in memory to prevent durable page locking
			 */
			entry->list = (RumItem *) palloc(BLCKSZ * sizeof(RumItem));
			maxoff = RumDataPageMaxOff(pageInner);
			entry->nlist = maxoff;
			entry->cachedLsn = PageGetLSN(pageInner);

			if (IsDataPageDeadForKilledTuple(ignoreKilledTuples, pageInner) &&
				maxoff >= FirstOffsetNumber)
			{
				/* In this path, the first page is dead, but rather than
				 * moving left right away, we add the first tuple here
				 * and let entryGetItem do the heavy lifting.
				 */
				ptr = RumDataPageGetData(pageInner);

				/* Ensure the first entry is 0 initialized */
				memset(&entry->list[0], 0, sizeof(RumItem));

				ptr = rumDataPageLeafRead(ptr, entry->attnum, &item, true,
										  rumstate);
				entry->list[0] = item;
				entry->nlist = 1;
			}
			else if (RumUseNewItemPtrDecoding)
			{
				rumPopulateDataPage(rumstate, entry, entry->nlist, pageInner);
			}
			else
			{
				ptr = RumDataPageGetData(pageInner);

				/* Ensure the first entry is 0 initialized */
				memset(&entry->list[0], 0, sizeof(RumItem));
				for (i = FirstOffsetNumber; i <= maxoff; i = OffsetNumberNext(i))
				{
					ptr = rumDataPageLeafRead(ptr, entry->attnum, &item, true,
											  rumstate);
					entry->list[i - FirstOffsetNumber] = item;
				}
			}

			LockBuffer(entry->buffer, RUM_UNLOCK);
			entry->isFinished = setListPositionScanEntry(rumstate, entry);
			if (!entry->isFinished)
			{
				entry->curItem = entry->list[entry->offset];
			}
		}
		else if (RumGetNPosting(itup) > 0)
		{
			entry->nlist = RumGetNPosting(itup);
			entry->cachedLsn = InvalidXLogRecPtr;
			entry->predictNumberResult = (uint32) entry->nlist;
			entry->list = (RumItem *) palloc(sizeof(RumItem) * entry->nlist);

			rumReadTuple(rumstate, entry->attnum, itup, entry->list, true);
			entry->isFinished = setListPositionScanEntry(rumstate, entry);
			if (!entry->isFinished)
			{
				entry->curItem = entry->list[entry->offset];
			}
		}

		if (entry->queryCategory == RUM_CAT_EMPTY_QUERY &&
			entry->scanWithAddInfo)
		{
			entry->stack = stackEntry;
		}

		SCAN_ENTRY_GET_KEY(entry, rumstate, itup);
	}

endScanEntry:
	if (needUnlock)
	{
		LockBuffer(stackEntry->buffer, RUM_UNLOCK);
	}
	if (entry->stack == NULL)
	{
		freeRumBtreeStack(stackEntry);
	}
}


inline static void
startScanKey(RumState *rumstate, RumScanKey key)
{
	RumItemSetMin(&key->curItem);
	key->curItemMatches = false;
	key->recheckCurItem = false;
	key->isFinished = false;
}


/*
 * Compare entries position. At first consider isFinished flag, then compare
 * item pointers.
 */
static int
cmpEntries(RumState *rumstate, RumScanEntry e1, RumScanEntry e2)
{
	int res;

	if (e1->isFinished == true)
	{
		if (e2->isFinished == true)
		{
			return 0;
		}
		else
		{
			return 1;
		}
	}
	if (e2->isFinished)
	{
		return -1;
	}

	/*
	 * compareRumItem assumes the attNums are equal for alternative orders
	 * If alternative orders are requested, ensure we check for equality of
	 * the attNums
	 */
	if (rumstate->useAlternativeOrder && e1->attnumOrig != e2->attnumOrig)
	{
		return (e1->attnumOrig < e2->attnumOrig) ? 1 : -1;
	}

	res = compareRumItem(rumstate, e1->attnumOrig, &e1->curItem,
						 &e2->curItem);

	return (ScanDirectionIsForward(e1->scanDirection)) ? res : -res;
}


static int
scan_entry_cmp(const void *p1, const void *p2, void *arg)
{
	RumScanEntry e1 = *((RumScanEntry *) p1);
	RumScanEntry e2 = *((RumScanEntry *) p2);

	return -cmpEntries(arg, e1, e2);
}


/*
 * Given a query and set of keys, tries to get the min/max item that could theoretically
 * match that key in the index.
 */
static void
DetectIndexBounds(RumScanOpaque so, RumState *rumstate,
				  RumItem *minItem, RumItem *maxItem)
{
	int i;
	bool canPreConsistent;
	ItemPointerSetInvalid(&minItem->iptr);
	ItemPointerSetInvalid(&maxItem->iptr);
	for (i = 0; i < so->nkeys; i++)
	{
		RumScanEntry currentEntry;
		RumScanKey currKey = so->keys[i];
		bool hasValidMax = false;
		if (!so->rumstate.hasCanPreConsistentFn[currKey->attnum - 1])
		{
			continue;
		}

		/* Assume that only keys that support "fast scans" and pre-consistent checks
		 * can participate in faster lookups.
		 */
		canPreConsistent = DatumGetBool(FunctionCall6Coll(
											&rumstate->canPreConsistentFn[currKey->attnum
																		  -
																		  1],
											rumstate->supportCollation[currKey->attnum -
																	   1],
											UInt16GetDatum(currKey->strategy),
											currKey->query,
											UInt32GetDatum(currKey->nuserentries),
											PointerGetDatum(currKey->extra_data),
											PointerGetDatum(currKey->queryValues),
											PointerGetDatum(currKey->queryCategories)));

		if (!canPreConsistent || currKey->nentries != 1)
		{
			continue;
		}

		currentEntry = currKey->scanEntry[0];

		/* Validate there's nothing that prevents us from accessing start/end */
		if (currentEntry->isPartialMatch ||
			currentEntry->isFinished ||
			!ItemPointerIsValid(&currentEntry->curItem.iptr))
		{
			continue;
		}

		/* We have a valid scan key and entry: capture the minimum item. This is the minimal item
		 * for this scanKey - now capture the "max" of this across all keys
		 */
		if (!ItemPointerIsValid(&minItem->iptr) ||
			compareRumItem(rumstate, currentEntry->attnum, &currentEntry->curItem,
						   minItem) > 0)
		{
			*minItem = currentEntry->curItem;
		}

		hasValidMax = currentEntry->nlist > 0;
		if (hasValidMax && BufferIsValid(currentEntry->buffer))
		{
			/* In certain cases, we can have a Posting Tree with 1 page. If we are already
			 * the right most page then we can consider the max from this page.
			 */
			Page page = BufferGetPage(currentEntry->buffer);
			hasValidMax = RumPageRightMost(page);
		}

		/* See if we can capture the "max" - this can happen for low selectivity keys (keys that don't have
		 * a posting tree). For a posting tree while we could capture this, we don't wanna do a page walk
		 * so we skip that here for now. Across keys, we pick the "min" of the maxes.
		 */
		if (hasValidMax &&
			(!ItemPointerIsValid(&maxItem->iptr) ||
			 compareRumItem(rumstate, currentEntry->attnum,
							&currentEntry->list[currentEntry->nlist - 1], maxItem) < 0))
		{
			*maxItem = currentEntry->list[currentEntry->nlist - 1];
		}
	}
}


static void
startScanEntryExtended(IndexScanDesc scan, RumState *rumstate, RumScanOpaque so)
{
	int i, minPartialMatchIndex = -1;
	RumItemScanEntryBounds scanEntryBounds;
	RumItemScanEntryBounds *entryBoundsPtr = NULL;

	/* First start the scan entries for everything that's not range */
	for (i = 0; i < so->totalentries; i++)
	{
		if (!so->entries[i]->isPartialMatch)
		{
			startScanEntry(rumstate, so->ignoreKilledTuples, so->entries[i],
						   scan->xs_snapshot,
						   NULL);
		}
		else if (minPartialMatchIndex < 0)
		{
			minPartialMatchIndex = i;
		}
	}

	if (minPartialMatchIndex < 0)
	{
		/* if there's no partialMatch we're done */
		return;
	}

	/* Now walk the keys and see if there's any information we can get about the "min" row
	 * or the "max" row that matches.
	 */
	ItemPointerSetInvalid(&scanEntryBounds.minItem.iptr);
	ItemPointerSetInvalid(&scanEntryBounds.maxItem.iptr);
	DetectIndexBounds(so, rumstate, &scanEntryBounds.minItem, &scanEntryBounds.maxItem);

	/* If we detected at least a min, then let's set it on the partial scan */
	if (ItemPointerIsValid(&scanEntryBounds.minItem.iptr))
	{
		entryBoundsPtr = &scanEntryBounds;
	}
	else
	{
		entryBoundsPtr = NULL;
	}

	/* Now initialize partialMatch entries based on the information from the entries already initialized */
	for (i = minPartialMatchIndex; i < so->totalentries; i++)
	{
		if (so->entries[i]->isPartialMatch)
		{
			/*
			 * When initializing it, if we're doing an index intersection with a non-partial match
			 * and the overall state allows for a tidbitmap instead of a tuplestore.
			 */
			startScanEntry(rumstate, so->ignoreKilledTuples, so->entries[i],
						   scan->xs_snapshot,
						   entryBoundsPtr);
		}
	}
}


inline static int
CompareRumKeyScanDirection(RumScanOpaque so, AttrNumber attnum,
						   Datum leftDatum, RumNullCategory leftCategory,
						   Datum rightDatum, RumNullCategory rightCategory)
{
	int cmp = rumCompareEntries(&so->rumstate,
								attnum,
								leftDatum,
								leftCategory,
								rightDatum,
								rightCategory);
	return ScanDirectionIsBackward(so->orderScanDirection) ? -cmp : cmp;
}


static bool
ValidateIndexEntry(RumScanOpaque so, Datum idatum,
				   bool *markedEntryFinished, bool *scanFinished, bool *canSkipCheck)
{
	int idx, jdx;
	int cmp;

	so->scanLoops++;
	so->recheckCurrentItem = false;
	so->recheckCurrentItemOrderBy = false;

	/* check if we need to skip based on page splits */
	if (so->orderByScanData->boundEntryTuple != NULL)
	{
		RumNullCategory icategory;
		Datum skipKey = rumtuple_get_key(&so->rumstate,
										 so->orderByScanData->boundEntryTuple,
										 &icategory);

		/* CompareRun */
		cmp = CompareRumKeyScanDirection(so,
										 so->orderByScanData->orderByEntry->attnum,
										 skipKey, icategory,
										 idatum, RUM_CAT_NORM_KEY);

		if (cmp >= 0)
		{
			return false;
		}

		pfree(so->orderByScanData->boundEntryTuple);
		so->orderByScanData->boundEntryTuple = NULL;
	}

	/* Validate filters */
	for (idx = 0; idx < so->nkeys; idx++)
	{
		bool allEntriesExhausted;
		bool hasAnyMatch = false;
		RumScanKey curKey = so->keys[idx];
		if (curKey->orderBy)
		{
			continue;
		}

		allEntriesExhausted = true;
		for (jdx = 0; jdx < curKey->nentries; jdx++)
		{
			if (curKey->scanEntry[jdx]->isFinished)
			{
				curKey->entryRes[jdx] = false;
			}
			else
			{
				cmp = DatumGetInt32(FunctionCall4Coll(
										&so->rumstate.comparePartialFn[curKey->attnum
																	   -
																	   1],
										so->rumstate.supportCollation[
											curKey->attnum - 1],
										curKey->scanEntry[jdx]->queryKey,
										idatum,
										UInt16GetDatum(
											curKey->scanEntry[jdx]->strategy),
										PointerGetDatum(
											curKey->scanEntry[jdx]->extra_data)));
				if (cmp == 0)
				{
					hasAnyMatch = true;
					allEntriesExhausted = false;
					curKey->entryRes[jdx] = true;
				}
				else if (cmp < 0)
				{
					if (cmp < -1 &&
						curKey->scanEntry[jdx] == so->orderByScanData->orderByEntry)
					{
						*canSkipCheck = true;
					}

					allEntriesExhausted = false;
					curKey->entryRes[jdx] = false;
				}
				else
				{
					/* Mark that the key is finished */
					*markedEntryFinished = true;
					curKey->scanEntry[jdx]->isFinished = true;
					curKey->entryRes[jdx] = false;
				}
			}
		}

		if (allEntriesExhausted)
		{
			/* No entry for this key matched, or said continue, we can stop searching */
			*scanFinished = true;
			return false;
		}

		/* Now call consistent on the key */
		if (!hasAnyMatch)
		{
			return hasAnyMatch;
		}

		if (!callConsistentFn(&so->rumstate, curKey))
		{
			return false;
		}

		/* Set recheck based on if any keys want recheck on this */
		so->recheckCurrentItem = so->recheckCurrentItem ||
								 curKey->recheckCurItem;
	}

	/* Validate recheckOrderBy */
	cmp = DatumGetInt32(FunctionCall4Coll(
							&so->rumstate.comparePartialFn[
								so->orderByScanData->orderByEntry->attnum - 1],
							so->rumstate.supportCollation[
								so->orderByScanData->orderByEntry->attnum - 1],
							so->orderByScanData->orderByEntry->queryKey,
							idatum,
							UInt16GetDatum(0),
							PointerGetDatum(
								so->orderByScanData->orderByEntry->extra_data)));
	if (cmp < 0)
	{
		so->recheckCurrentItemOrderBy = true;
	}

	return true;
}


/*
 * This is a copy of index_form_tuple in Postgres,
 * except we don't try to compress the tuples at all
 * since this is not destined for storage but the runtime.
 * Additionally, we reuse the prior indextuple memory to avoid
 * re-allocating if possible.
 */
static IndexTuple
IndexBuildTupleDynamic(TupleDesc tupleDescriptor,
					   Datum *values,
					   bool *isnull,
					   IndexTuple priorTuple,
					   MemoryContext context)
{
	char *tp;                   /* tuple pointer */
	IndexTuple tuple;           /* return tuple */
	Size size,
		 data_size,
		 hoff;
	int i;
	unsigned short infomask = 0;
	bool hasnull = false;
	uint16 tupmask = 0;
	int numberOfAttributes = tupleDescriptor->natts;

	if (numberOfAttributes > INDEX_MAX_KEYS)
	{
		ereport(ERROR,
				(errcode(ERRCODE_TOO_MANY_COLUMNS),
				 errmsg("number of index columns (%d) exceeds limit (%d)",
						numberOfAttributes, INDEX_MAX_KEYS)));
	}

	for (i = 0; i < numberOfAttributes; i++)
	{
		if (isnull[i])
		{
			hasnull = true;
			break;
		}
	}

	if (hasnull)
	{
		infomask |= INDEX_NULL_MASK;
	}

	hoff = IndexInfoFindDataOffset(infomask);
	data_size = heap_compute_data_size(tupleDescriptor,
									   values, isnull);
	size = hoff + data_size;
	size = MAXALIGN(size);      /* be conservative */

	if (priorTuple != NULL)
	{
		Size priorSize = IndexTupleSize(priorTuple);
		if (priorSize < size)
		{
			priorTuple = repalloc(priorTuple, size);
		}

		tp = (char *) priorTuple;
		memset(tp, 0, sizeof(IndexTupleData));
	}
	else
	{
		tp = (char *) MemoryContextAllocZero(context, size);
	}

	tuple = (IndexTuple) tp;
	heap_fill_tuple(tupleDescriptor,
					values,
					isnull,
					(char *) tp + hoff,
					data_size,
					&tupmask,
					(hasnull ? (bits8 *) tp + sizeof(IndexTupleData) : NULL));

	/*
	 * We do this because heap_fill_tuple wants to initialize a "tupmask"
	 * which is used for HeapTuples, but we want an indextuple infomask. The
	 * only relevant info is the "has variable attributes" field. We have
	 * already set the hasnull bit above.
	 */
	if (tupmask & HEAP_HASVARWIDTH)
	{
		infomask |= INDEX_VAR_MASK;
	}

	/*
	 * Here we make sure that the size will fit in the field reserved for it
	 * in t_info.
	 */
	if ((size & INDEX_SIZE_MASK) != size)
	{
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("index row requires %zu bytes, maximum size is %zu",
						size, (Size) INDEX_SIZE_MASK)));
	}

	infomask |= size;

	/*
	 * initialize metadata
	 */
	tuple->t_info = infomask;
	return tuple;
}


static void
PrepareOrderedMatchedEntry(RumScanOpaque so, RumScanEntry entry,
						   Snapshot snapshot, IndexTuple itup)
{
	/* Before unlocking any pages, we want to ensure that orderby properties are preserved
	 * This needs to be done if the current key has recheck, or if we've historically
	 * had any entry that needed recheck since the runtime can re-evaluate any key after
	 * a recheck was set.
	 */
	if (so->recheckCurrentItemOrderBy || so->orderByHasRecheck)
	{
		int i;
		MemoryContext oldContext;
		RumNullCategory icategory;
		Datum idatum = rumtuple_get_key(&so->rumstate, itup, &icategory);
		so->orderByHasRecheck = true;

		oldContext = MemoryContextSwitchTo(so->keyCtx);

		/* We need to walk all the order by keys and project them */
		for (i = so->orderByKeyIndex; i < so->nkeys; i++)
		{
			if (!so->keys[i]->orderBy)
			{
				continue;
			}

			so->keys[i]->curKey = FunctionCall4(
				&so->rumstate.orderingFn[so->keys[i]->attnum - 1],
				idatum,
				so->keys[i]->query,
				UInt16GetDatum(so->keys[i]->strategy),
				so->keys[i]->curKey);
		}
		MemoryContextSwitchTo(oldContext);
	}

	if (so->projectIndexTupleData)
	{
		/* This is the case where we want to project a document that maches the index paths */
		MemoryContext oldContext;
		RumNullCategory icategory;
		Datum values[INDEX_MAX_KEYS] = { 0 };
		bool isnull[INDEX_MAX_KEYS] = { true };

		Datum idatum = rumtuple_get_key(&so->rumstate, itup, &icategory);

		memset(isnull, true, sizeof(bool) *
			   so->projectIndexTupleData->indexTupleDesc->natts);
		oldContext = MemoryContextSwitchTo(so->keyCtx);

		so->projectIndexTupleData->indexTupleDatum = FunctionCall4(
			&so->rumstate.orderingFn[0],
			idatum,
			(Datum) 0,
			UInt16GetDatum(UINT16_MAX),
			so->projectIndexTupleData->indexTupleDatum);

		/* Now form the index datum (freeing the prior one) */
		values[0] = so->projectIndexTupleData->indexTupleDatum;
		isnull[0] = false;

		so->projectIndexTupleData->iscan_tuple = IndexBuildTupleDynamic(
			so->projectIndexTupleData->indexTupleDesc, values, isnull,
			so->projectIndexTupleData->iscan_tuple, so->keyCtx);
		MemoryContextSwitchTo(oldContext);
	}

	if (RumIsPostingTree(itup))
	{
		BlockNumber rootPostingTree = RumGetPostingTree(itup);
		RumPostingTreeScan *gdi;
		Page pageInner;
		OffsetNumber maxoff,
					 i;
		Pointer ptr;
		RumItem item;

		ItemPointerSetMin(&item.iptr);

		/*
		 * The entry page should be unlocked before touching posting tree to
		 * prevent deadlocks with vacuum processes. Because entry is never
		 * deleted from page and posting tree is never reduced to the
		 * posting list, we can unlock page after getting BlockNumber of
		 * root of posting tree.
		 */
		gdi = rumPrepareScanPostingTree(so->rumstate.index, rootPostingTree, true,
										entry->scanDirection, entry->attnum,
										&so->rumstate);

		entry->buffer = rumScanBeginPostingTree(gdi, entry->useMarkAddInfo ?
												&entry->markAddInfo : NULL);

		entry->gdi = gdi;

		PredicateLockPage(so->rumstate.index, BufferGetBlockNumber(entry->buffer),
						  snapshot);

		/*
		 * We keep buffer pinned because we need to prevent deletion of
		 * page during scan. See RUM's vacuum implementation. RefCount is
		 * increased to keep buffer pinned after freeRumBtreeStack() call.
		 */
		pageInner = BufferGetPage(entry->buffer);
		entry->predictNumberResult += gdi->stack->predictNumber * RumDataPageMaxOff(
			pageInner);

		/*
		 * Keep page content in memory to prevent durable page locking
		 */
		entry->list = (RumItem *) palloc(BLCKSZ * sizeof(RumItem));
		maxoff = RumDataPageMaxOff(pageInner);
		entry->nlist = maxoff;
		entry->cachedLsn = PageGetLSN(pageInner);

		if (RumUseNewItemPtrDecoding)
		{
			rumPopulateDataPage(&so->rumstate, entry, maxoff, pageInner);
		}
		else
		{
			ptr = RumDataPageGetData(pageInner);

			/* Ensure the first entry is 0 initialized */
			memset(&entry->list[0], 0, sizeof(RumItem));
			for (i = FirstOffsetNumber; i <= maxoff; i = OffsetNumberNext(i))
			{
				ptr = rumDataPageLeafRead(ptr, entry->attnum, &item, true,
										  &so->rumstate);
				entry->list[i - FirstOffsetNumber] = item;
			}
		}

		LockBuffer(entry->buffer, RUM_UNLOCK);
		entry->isFinished = setListPositionScanEntry(&so->rumstate, entry);
		if (!entry->isFinished)
		{
			entry->curItem = entry->list[entry->offset];
		}
	}
	else if (RumGetNPosting(itup) > 0)
	{
		entry->nlist = RumGetNPosting(itup);
		entry->cachedLsn = InvalidXLogRecPtr;
		entry->predictNumberResult += (uint32) entry->nlist;
		entry->list = (RumItem *) palloc(sizeof(RumItem) * entry->nlist);

		rumReadTuple(&so->rumstate, entry->attnum, itup, entry->list, true);
		entry->isFinished = setListPositionScanEntry(&so->rumstate, entry);
		if (!entry->isFinished)
		{
			entry->curItem = entry->list[entry->offset];
		}
	}
	else
	{
		/* No postings, so mark entry as finished */
		entry->nlist = 0;
		entry->cachedLsn = InvalidXLogRecPtr;
		entry->isFinished = true;
	}
}


static void
startScanEntryOrderedCore(RumScanOpaque so, RumScanEntry minScanEntry, Snapshot snapshot)
{
	RumBtreeData btreeEntry;
	RumBtreeStack *stackEntry;
	Page page;
	bool needUnlock, foundInLeaf;
	ItemId itemid;
	RumScanEntry entry = minScanEntry;
	RumState *rumstate = &so->rumstate;
	Datum entryToUse;

	entry->buffer = InvalidBuffer;
	RumItemSetMin(&entry->curItem);
	entry->offset = InvalidOffsetNumber;
	entry->list = NULL;
	entry->gdi = NULL;
	entry->stack = NULL;
	entry->nlist = 0;
	entry->cachedLsn = InvalidXLogRecPtr;
	entry->matchSortstate = NULL;
	entry->reduceResult = false;
	entry->predictNumberResult = 0;

	if (so->orderByScanData->orderStack)
	{
		freeRumBtreeStack(so->orderByScanData->orderStack);
	}
	so->orderByScanData->orderStack = NULL;

	if (so->orderByScanData->isPageValid)
	{
		so->orderByScanData->isPageValid = false;
	}

	/* Current entry being considered for ordered scan */
	so->orderByScanData->orderByEntry = entry;

	/*
	 * we should find entry, and begin scan of posting tree or just store
	 * posting list in memory
	 */
	entryToUse = entry->queryKeyOverride != (Datum) 0 ? entry->queryKeyOverride :
				 entry->queryKey;
	rumPrepareEntryScan(&btreeEntry, entry->attnum,
						entryToUse, entry->queryCategory,
						rumstate);
	btreeEntry.searchMode = true;
	stackEntry = rumFindLeafPage(&btreeEntry, NULL);
	page = BufferGetPage(stackEntry->buffer);
	needUnlock = true;

	entry->isFinished = true;

	PredicateLockPage(rumstate->index, BufferGetBlockNumber(stackEntry->buffer),
					  snapshot);

	/* Not found for the exact item */
	foundInLeaf = btreeEntry.findItem(&btreeEntry, stackEntry);

	if (!foundInLeaf &&
		ScanDirectionIsBackward(so->orderScanDirection) &&
		stackEntry->off > PageGetMaxOffsetNumber(page))
	{
		/* The start went off the maximum and stackEntry->off points to the max */
		stackEntry->off = PageGetMaxOffsetNumber(page);
	}

	/* Otherwise found something valid */
	itemid = PageGetItemId(page, stackEntry->off);

	if (!ItemIdHasStorage(itemid))
	{
		goto endOrderedScanEntry;
	}

	/* Let MoveScanForward deal with the reving and setting of stuff */
	so->orderByScanData->orderStack = stackEntry;
	entry->isFinished = true;

endOrderedScanEntry:
	if (needUnlock)
	{
		LockBuffer(stackEntry->buffer, RUM_UNLOCK);
	}
	if (entry->stack == NULL && so->orderByScanData->orderStack == NULL)
	{
		freeRumBtreeStack(stackEntry);
	}
}


static RumScanEntry
getMinScanEntry(RumScanOpaque so)
{
	int i, j;
	int cmp;
	RumScanEntry globalMinEntry = NULL;

	for (i = 0; i < so->nkeys; i++)
	{
		/* Get the minimum entry per key */
		RumScanKey key = so->keys[i];
		RumScanEntry minEntry = NULL;
		if (key->orderBy)
		{
			continue;
		}

		for (j = 0; j < key->nentries; j++)
		{
			if (key->scanEntry[j]->isFinished)
			{
				/* Ignore finished entries (with no results) */
				continue;
			}
			if (minEntry == NULL)
			{
				minEntry = key->scanEntry[j];
				continue;
			}

			cmp = rumCompareEntries(&so->rumstate,
									minEntry->attnum,
									minEntry->queryKey, minEntry->queryCategory,
									key->scanEntry[j]->queryKey,
									key->scanEntry[j]->queryCategory);
			if (ScanDirectionIsBackward(so->orderScanDirection))
			{
				cmp = -cmp;
			}

			if (cmp > 0)
			{
				/* minEntry is bigger than scanEntry - shift minEntry */
				minEntry = key->scanEntry[j];
			}
		}

		if (minEntry == NULL)
		{
			/* No entries for this key, skip */
			continue;
		}

		/* Across scan keys, pick the maximum */
		if (globalMinEntry == NULL)
		{
			globalMinEntry = minEntry;
		}
		else
		{
			cmp = rumCompareEntries(&so->rumstate,
									globalMinEntry->attnum,
									globalMinEntry->queryKey,
									globalMinEntry->queryCategory,
									minEntry->queryKey,
									minEntry->queryCategory);
			if (ScanDirectionIsBackward(so->orderScanDirection))
			{
				cmp = -cmp;
			}

			if (cmp < 0)
			{
				/* globalMinEntry is smaller than scanEntry - shift minEntry */
				globalMinEntry = minEntry;
			}
		}
	}

	return globalMinEntry;
}


static void
startOrderedScanEntries(IndexScanDesc scan, RumState *rumstate, RumScanOpaque so)
{
	/* Now adjust the bounds based on the minimum value of the other scan keys */
	RumScanEntry minEntry = getMinScanEntry(so);
	if (minEntry == NULL)
	{
		so->isVoidRes = true;
		return;
	}

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
	}

	so->orderByScanData = palloc0(sizeof(RumOrderByScanData));
	so->orderByScanData->orderByEntryPageCopy = palloc(BLCKSZ);
	startScanEntryOrderedCore(so, minEntry, scan->xs_snapshot);
}


static void
startScan(IndexScanDesc scan)
{
	RumScanOpaque so = (RumScanOpaque) scan->opaque;
	RumState *rumstate = &so->rumstate;
	uint32 i;
	RumScanType scanType = RumFastScan;
	MemoryContext oldCtx = MemoryContextSwitchTo(so->keyCtx);
	bool isSupportedOrderedScan = false;

	/* Validate that there's only 1 attnum in all the keys,
	 * multiatt ordered scan is not supported
	 * Ordered scan also requires comparePartial and
	 * an ordering function on all keys.
	 */
	isSupportedOrderedScan = so->nkeys > 0;
	for (i = 0; i < so->nkeys; i++)
	{
		RumScanKey key = so->keys[i];
		if (key->attnum != so->keys[0]->attnum)
		{
			isSupportedOrderedScan = false;
			break;
		}

		if (!rumstate->canPartialMatch[key->attnum - 1] ||
			!rumstate->canOrdering[key->attnum - 1] ||
			rumstate->orderingFn[key->attnum - 1].fn_nargs != 4)
		{
			isSupportedOrderedScan = false;
			break;
		}
	}

	if (RumForceOrderedIndexScan && isSupportedOrderedScan)
	{
		scanType = RumOrderedScan;
		startOrderedScanEntries(scan, rumstate, so);
	}
	else if (so->norderbys > 0 &&
			 so->willSort && !rumstate->useAlternativeOrder)
	{
		scanType = RumOrderedScan;
		startOrderedScanEntries(scan, rumstate, so);
	}
	else if (scan->parallel_scan != NULL && isSupportedOrderedScan)
	{
		scanType = RumOrderedScan;
		startOrderedScanEntries(scan, rumstate, so);
	}
	else if (scan->xs_want_itup)
	{
		if (!isSupportedOrderedScan)
		{
			ereport(ERROR, (errmsg(
								"Unexpected index only scan when ordered scan is not supported.")));
		}

		/* If we want to return index tuples, we can use ordered scan */
		scanType = RumOrderedScan;
		startOrderedScanEntries(scan, rumstate, so);
	}
	else if (isSupportedOrderedScan && RumPreferOrderedIndexScan &&
			 so->totalentries == 1 && so->entries[0]->isPartialMatch)
	{
		/* We can simply use an ordered scan if there's only 1 entry
		 * This would happen for any scenario that is not needing a
		 * consistent check intersection.
		 */
		scanType = RumOrderedScan;
		startOrderedScanEntries(scan, rumstate, so);
	}
	else if (so->norderbys == 0 && !so->willSort && !rumstate->useAlternativeOrder)
	{
		startScanEntryExtended(scan, rumstate, so);
	}
	else
	{
		for (i = 0; i < so->totalentries; i++)
		{
			startScanEntry(rumstate, so->ignoreKilledTuples, so->entries[i],
						   scan->xs_snapshot,
						   NULL);
		}
	}
	MemoryContextSwitchTo(oldCtx);

	for (i = 0; i < so->nkeys; i++)
	{
		RumScanKey key = so->keys[i];
		startScanKey(rumstate, key);

		/*
		 * Check if we can use a fast scan.
		 * Use fast scan iff all keys have preConsistent method. But we can stop
		 * checking if at least one key have not preConsistent method and use
		 * regular scan.
		 */

		/* Check first key is it used to full-index scan */
		if (i == 0 && scanType < RumFullScan && key->nentries > 0 &&
			key->scanEntry[i]->scanWithAddInfo)
		{
			scanType = RumFullScan;
		}

		/* Else check keys for preConsistent method */
		else if (scanType == RumFastScan && !so->rumstate.canPreConsistent[key->attnum -
																		   1])
		{
			scanType = RumRegularScan;
		}
		else if (scanType == RumFastScan &&
				 so->rumstate.hasCanPreConsistentFn[key->attnum - 1])
		{
			bool canPreConsistent = DatumGetBool(FunctionCall6Coll(
													 &rumstate->canPreConsistentFn[key->
																				   attnum
																				   -
																				   1],
													 rumstate->
													 supportCollation[key->attnum - 1],
													 UInt16GetDatum(
														 key->strategy),
													 key->query,
													 UInt32GetDatum(
														 key->nuserentries),
													 PointerGetDatum(
														 key->extra_data),
													 PointerGetDatum(
														 key->queryValues),
													 PointerGetDatum(
														 key->queryCategories)
													 ));
			if (!canPreConsistent)
			{
				scanType = RumRegularScan;
			}
		}
	}

	if (scanType == RumFastScan)
	{
		if (RumDisableFastScan)
		{
			/*
			 * If fast scan is disabled, we should use regular scan.
			 */
			scanType = RumRegularScan;
		}

		for (i = 0; i < so->totalentries && scanType == RumFastScan; i++)
		{
			RumScanEntry entry = so->entries[i];
			if (entry->isPartialMatch)
			{
				scanType = RumRegularScan;
			}
		}
	}

	ItemPointerSetInvalid(&so->item.iptr);

	if (scanType == RumFastScan)
	{
		/*
		 * We are going to use fast scan. Do some preliminaries. Start scan of
		 * each entry and sort entries by descending item pointers.
		 */
		so->sortedEntries = (RumScanEntry *) palloc(sizeof(RumScanEntry) *
													so->totalentries);
		memcpy(so->sortedEntries, so->entries, sizeof(RumScanEntry) *
			   so->totalentries);
		for (i = 0; i < so->totalentries; i++)
		{
			if (!so->sortedEntries[i]->isFinished)
			{
				entryGetItem(&so->rumstate, so->sortedEntries[i], NULL,
							 scan->xs_snapshot, NULL, so);
			}
		}
		qsort_arg(so->sortedEntries, so->totalentries, sizeof(RumScanEntry),
				  scan_entry_cmp, rumstate);
	}

	so->scanType = scanType;
}


static int
KillItemPointerSortComparer(const void *left, const void *right)
{
	ItemPointer leftPtr = (ItemPointer) left;
	ItemPointer rightPtr = (ItemPointer) right;

	return ItemPointerCompare(leftPtr, rightPtr);
}


static bool
PostingListHasAliveTuples(RumScanOpaque so, Pointer postingPtr,
						  int numPostings, OffsetNumber attnum,
						  int numKilled)
{
	RumItem item;
	Pointer ptr = postingPtr;
	int j = 0, k = 0;
	InitBlockNumberIncrZero(blockNumberIncr);
	bool hasAliveTuples = false;

	/* If the total number of killed tuples is less than postings,
	 * there *must* be some alive tuples by definition.
	 */
	if (numKilled < numPostings)
	{
		return true;
	}

	RumItemSetInvalid(&item);
	while (!hasAliveTuples && k < numKilled &&
		   j < numPostings)
	{
		int cmp;
		if (!ItemPointerIsValid(&item.iptr))
		{
			ptr = rumDataPageLeafReadWithBlockNumberIncr(
				ptr, attnum, &item, false, &so->rumstate, &blockNumberIncr);
		}

		cmp = ItemPointerCompare(&item.iptr, &so->killedItems[k]);
		if (cmp < 0)
		{
			/* item pointer is smaller than killed items - at least one
			 * item pointer is alive break.
			 */
			hasAliveTuples = true;
			break;
		}
		else if (cmp > 0)
		{
			/* Item pointer is bigger than killed items a later killed item may match */
			k++;
		}
		else
		{
			/* Advance to the next tuple */
			RumItemSetInvalid(&item);
			k++;
			j++;
		}
	}

	/* Some tuples are still alive on a TID bigger than killed tuples
	 * OR some tuples are smaller - the tuple is considered alive.
	 */
	return hasAliveTuples || j < numPostings;
}


static void
RumKillDataPageItems(RumScanOpaque so, XLogRecPtr cachedPageLsn, Buffer buffer,
					 OffsetNumber attnum)
{
	Pointer ptr;
	int numPostings;
	XLogRecPtr latestLsn;
	Page page = BufferGetPage(buffer);
	int numKilled = so->numKilled;
	so->numKilled = 0;

	if (so->killedItems == NULL ||
		numKilled == 0 ||
		!RumEnableSupportDeadIndexItems ||
		cachedPageLsn == InvalidXLogRecPtr)
	{
		return;
	}

	/* We have share lock on current buffer */
	if (RumDataPageMaxOff(page) < FirstOffsetNumber)
	{
		return;
	}

	/* We have share lock on current buffer. Ensure contents unchanged */
	latestLsn = BufferGetLSNAtomic(buffer);
	Assert(!XLogRecPtrIsInvalid(cachedPageLsn));
	Assert(cachedPageLsn <= latestLsn);
	if (cachedPageLsn != latestLsn)
	{
		/* Modified while not pinned means hinting is not safe. */
		return;
	}

	page = BufferGetPage(buffer);

	numPostings = RumDataPageMaxOff(page);
	if (numKilled < numPostings)
	{
		return;
	}

	ptr = RumDataPageGetData(page);

	/* Sort killed items to make things easier */
	qsort(so->killedItems, numKilled, sizeof(ItemPointerData),
		  KillItemPointerSortComparer);

	if (PostingListHasAliveTuples(so, ptr, numPostings, attnum, numKilled))
	{
		return;
	}

	/* Note we don't generate WAL records and let checkpoint handle this.*/
	RumDataPageEntryMarkDead(page);
	MarkBufferDirtyHint(buffer, true);
}


void
RumKillEntryItems(RumScanOpaque so, RumOrderByScanData *scanData)
{
	XLogRecPtr latestLsn, cachedLsn;
	Buffer buffer = scanData->orderStack->buffer;
	OffsetNumber i;
	Page page;
	bool killedsomething = false;
	int numKilled = so->numKilled;
	so->numKilled = 0;

	if (so->killedItems == NULL ||
		numKilled == 0 ||
		!RumEnableSupportDeadIndexItems)
	{
		return;
	}

	/* We have share lock on current buffer. Ensure contents unchanged */
	latestLsn = BufferGetLSNAtomic(buffer);
	cachedLsn = PageGetLSN(scanData->orderByEntryPageCopy);
	Assert(!XLogRecPtrIsInvalid(cachedLsn));
	Assert(cachedLsn <= latestLsn);
	if (cachedLsn != latestLsn)
	{
		/* Modified while not pinned means hinting is not safe. */
		return;
	}

	/* Sort killed items to make things easier */
	qsort(so->killedItems, numKilled, sizeof(ItemPointerData),
		  KillItemPointerSortComparer);

	page = BufferGetPage(buffer);
	for (i = FirstOffsetNumber; i <= PageGetMaxOffsetNumber(page); i++)
	{
		ItemId curItem = PageGetItemId(page, i);
		IndexTuple itup;
		Pointer ptr;
		int32_t numPostings;
		if (!ItemIdHasStorage(curItem))
		{
			continue;
		}

		itup = (IndexTuple) PageGetItem(page, curItem);
		if (RumIsPostingTree(itup))
		{
			/* Posting tree entries do not get set as dead */
			continue;
		}

		numPostings = RumGetNPosting(itup);
		if (numPostings < 1 || numKilled < numPostings)
		{
			continue;
		}

		ptr = RumGetPosting(itup);
		if (PostingListHasAliveTuples(
				so, ptr, numPostings, scanData->orderByEntry->attnum, numKilled))
		{
			continue;
		}

		/* If we got here, j == numPostings and hasAliveTuples never got set,
		 * mark the hint bits on the tuple. Note we don't generate WAL records
		 * and let checkpoint handle this.
		 */
		if (!RumIndexEntryIsDead(curItem))
		{
			/* found the item/all posting list items */
			RumIndexEntryMarkDead(curItem);
			killedsomething = true;
		}
	}

	if (killedsomething)
	{
		MarkBufferDirtyHint(buffer, true);
	}
}


/*
 * Gets next ItemPointer from PostingTree. Note, that we copy
 * page into RumScanEntry->list array and unlock page, but keep it pinned
 * to prevent interference with vacuum
 */
static void
entryGetNextItem(RumState *rumstate, RumScanEntry entry, Snapshot snapshot,
				 RumItem *advancePast, RumScanOpaque so)
{
	Page page;

	for (;;)
	{
		RumItem *comparePast;
		bool equalOk;
		bool shouldScanPage = true;

		if (entry->offset >= 0 && entry->offset < entry->nlist)
		{
			entry->curItem = entry->list[entry->offset];
			entry->offset += entry->scanDirection;
			return;
		}

		LockBuffer(entry->buffer, RUM_SHARE);
		page = BufferGetPage(entry->buffer);

		/* If the page got split by the time we get here, then refind the leftmost page */
		while (!RumPageIsLeaf(page))
		{
			RumBtreeData btree;
			BlockNumber newBlock;
			Buffer newBuffer;
			rumPrepareDataScan(&btree, rumstate->index, entry->attnum, rumstate);
			newBlock = btree.getLeftMostPage(&btree, page);
			newBuffer = ReadBuffer(btree.index, newBlock);
			LockBuffer(newBuffer, RUM_SHARE);
			UnlockReleaseBuffer(entry->buffer);
			entry->buffer = newBuffer;
			page = BufferGetPage(entry->buffer);
		}

		PredicateLockPage(rumstate->index, BufferGetBlockNumber(entry->buffer), snapshot);

		comparePast = &entry->curItem;
		equalOk = false;

		/* When scanning the current page, pick advancePast if it's higher than entry
		 * we're looking for. Typically this may be generally true, but in the case where
		 * you have something like a $in [ 1, 2, 3 ], the advancePast tracks the minEntry
		 * while one of thee internal entries could be further ahead.
		 */
		if (advancePast != NULL &&
			ItemPointerIsValid(&advancePast->iptr) &&
			compareRumItemScanDirection(rumstate, entry->attnumOrig, entry->scanDirection,
										comparePast, advancePast) < 0)
		{
			comparePast = advancePast;
			equalOk = true;
		}

		if (IsDataPageDeadForKilledTuple(so->ignoreKilledTuples, page))
		{
			so->killedItemsSkipped++;
			shouldScanPage = false;
		}

		if (shouldScanPage && scanPage(rumstate, entry, comparePast, equalOk))
		{
			LockBuffer(entry->buffer, RUM_UNLOCK);
			return;
		}

		/* before we advance further, handle deletes */
		if (RumEnableSupportDeadIndexItems && so->numKilled > 0)
		{
			RumKillDataPageItems(so, entry->cachedLsn, entry->buffer, entry->attnum);
		}

		for (;;)
		{
			OffsetNumber maxoff,
						 i;
			Pointer ptr;
			RumItem item;
			bool searchBorder;

			searchBorder = (ScanDirectionIsForward(entry->scanDirection) &&
							ItemPointerIsValid(&entry->curItem.iptr));

			/*
			 * It's needed to go by right link. During that we should refind
			 * first ItemPointer greater that stored
			 */
			if ((ScanDirectionIsForward(entry->scanDirection) && RumPageRightMost(
					 page)) ||
				(ScanDirectionIsBackward(entry->scanDirection) && RumPageLeftMost(page)))
			{
				UnlockReleaseBuffer(entry->buffer);
				ItemPointerSetInvalid(&entry->curItem.iptr);

				entry->buffer = InvalidBuffer;
				entry->isFinished = true;
				entry->gdi->stack->buffer = InvalidBuffer;
				return;
			}

			entry->buffer = rumStep(entry->buffer, rumstate->index,
									RUM_SHARE, entry->scanDirection);
			entry->gdi->stack->buffer = entry->buffer;
			entry->gdi->stack->blkno = BufferGetBlockNumber(entry->buffer);
			page = BufferGetPage(entry->buffer);

			PredicateLockPage(rumstate->index, BufferGetBlockNumber(entry->buffer),
							  snapshot);

			entry->offset = -1;
			maxoff = RumDataPageMaxOff(page);
			entry->cachedLsn = PageGetLSN(page);
			entry->nlist = maxoff;
			ItemPointerSetMin(&item.iptr);
			ptr = RumDataPageGetData(page);

			/*
			 * Quick check to see if this page will meet our needs: If the right most bound
			 * of this page is less than our comparePast, then skip this and move on to the next
			 * page.
			 */
			if (ScanDirectionIsForward(entry->scanDirection) &&
				!RumPageRightMost(page) &&
				advancePast != NULL &&
				ItemPointerIsValid(&advancePast->iptr))
			{
				int cmp;
				comparePast = &entry->curItem;
				if (compareRumItemScanDirection(rumstate, entry->attnumOrig,
												entry->scanDirection,
												comparePast, advancePast) < 0)
				{
					comparePast = advancePast;
				}

				cmp = compareRumItem(rumstate, entry->attnumOrig,
									 RumDataPageGetRightBound(page), comparePast);
				if (cmp < 0 || (cmp <= 0 && !equalOk))
				{
					/* go on next page */
					LockBuffer(entry->buffer, RUM_UNLOCK);
					break;
				}
			}

			if (IsDataPageDeadForKilledTuple(so->ignoreKilledTuples, page))
			{
				/* go on next page */
				LockBuffer(entry->buffer, RUM_UNLOCK);
				break;
			}

			for (i = FirstOffsetNumber; i <= maxoff; i = OffsetNumberNext(i))
			{
				ptr = rumDataPageLeafRead(ptr, entry->attnum, &item, true,
										  rumstate);
				entry->list[i - FirstOffsetNumber] = item;

				if (searchBorder)
				{
					/* don't search position for backward scan,
					 * because of split algorithm */
					int cmp = compareRumItem(rumstate,
											 entry->attnumOrig,
											 &entry->curItem,
											 &item);

					if (cmp > 0)
					{
						entry->offset = i - FirstOffsetNumber;
						searchBorder = false;
					}
				}
			}

			LockBuffer(entry->buffer, RUM_UNLOCK);

			if (entry->offset < 0)
			{
				if (ScanDirectionIsForward(entry->scanDirection) &&
					ItemPointerIsValid(&entry->curItem.iptr))
				{
					/* go on next page */
					break;
				}
				if (maxoff == 0)
				{
					/* This page had 0 items, don't scan it and go to the next page */
					break;
				}

				entry->offset = (ScanDirectionIsForward(entry->scanDirection)) ?
								0 : entry->nlist - 1;
			}

			entry->curItem = entry->list[entry->offset];
			entry->offset += entry->scanDirection;
			return;
		}
	}
}


inline static void
ResetEntryItem(RumScanEntry entry)
{
	entry->buffer = InvalidBuffer;
	RumItemSetMin(&entry->curItem);
	entry->offset = InvalidOffsetNumber;
	if (entry->gdi)
	{
		freeRumBtreeStack(entry->gdi->stack);
		pfree(entry->gdi);
	}
	entry->gdi = NULL;
	if (entry->list)
	{
		pfree(entry->list);
		entry->list = NULL;
		entry->nlist = 0;
		entry->cachedLsn = InvalidXLogRecPtr;
	}
	entry->matchSortstate = NULL;
	entry->reduceResult = false;
	entry->isFinished = false;
}


static bool
entryGetNextItemList(RumState *rumstate, RumScanEntry entry, Snapshot snapshot)
{
	Page page;
	IndexTuple itup;
	RumBtreeData btree;
	bool needUnlock;

	Assert(!entry->isFinished);
	Assert(entry->stack);
	Assert(ScanDirectionIsForward(entry->scanDirection));

	ResetEntryItem(entry);
	entry->predictNumberResult = 0;

	rumPrepareEntryScan(&btree, entry->attnum,
						entry->queryKey, entry->queryCategory,
						rumstate);

	LockBuffer(entry->stack->buffer, RUM_SHARE);

	/*
	 * stack->off points to the interested entry, buffer is already locked
	 */
	if (!moveRightIfItNeeded(&btree, entry->stack))
	{
		ItemPointerSetInvalid(&entry->curItem.iptr);
		entry->isFinished = true;
		LockBuffer(entry->stack->buffer, RUM_UNLOCK);
		return false;
	}

	page = BufferGetPage(entry->stack->buffer);
	itup = (IndexTuple) PageGetItem(page, PageGetItemId(page,
														entry->stack->off));
	needUnlock = true;

	/*
	 * If tuple stores another attribute then stop scan
	 */
	if (rumtuple_get_attrnum(btree.rumstate, itup) != entry->attnum)
	{
		ItemPointerSetInvalid(&entry->curItem.iptr);
		entry->isFinished = true;
		LockBuffer(entry->stack->buffer, RUM_UNLOCK);
		return false;
	}

	/*
	 * OK, we want to return the TIDs listed in this entry.
	 */
	if (RumIsPostingTree(itup))
	{
		BlockNumber rootPostingTree = RumGetPostingTree(itup);
		RumPostingTreeScan *gdi;
		Page pageInner;
		OffsetNumber maxoff,
					 i;
		Pointer ptr;
		RumItem item;

		ItemPointerSetMin(&item.iptr);

		/*
		 * We should unlock entry page before touching posting tree to
		 * prevent deadlocks with vacuum processes. Because entry is never
		 * deleted from page and posting tree is never reduced to the
		 * posting list, we can unlock page after getting BlockNumber of
		 * root of posting tree.
		 */
		LockBuffer(entry->stack->buffer, RUM_UNLOCK);
		needUnlock = false;
		gdi = rumPrepareScanPostingTree(rumstate->index,
										rootPostingTree, true, entry->scanDirection,
										entry->attnumOrig, rumstate);

		entry->buffer = rumScanBeginPostingTree(gdi, NULL);
		entry->gdi = gdi;

		PredicateLockPage(rumstate->index, BufferGetBlockNumber(entry->buffer), snapshot);

		/*
		 * We keep buffer pinned because we need to prevent deletion of
		 * page during scan. See RUM's vacuum implementation. RefCount is
		 * increased to keep buffer pinned after freeRumBtreeStack() call.
		 */
		pageInner = BufferGetPage(entry->buffer);
		entry->predictNumberResult = gdi->stack->predictNumber *
									 RumDataPageMaxOff(pageInner);

		/*
		 * Keep page content in memory to prevent durable page locking
		 */
		entry->list = (RumItem *) palloc(BLCKSZ * sizeof(RumItem));
		maxoff = RumDataPageMaxOff(pageInner);
		entry->nlist = maxoff;
		entry->cachedLsn = PageGetLSN(pageInner);

		if (RumUseNewItemPtrDecoding)
		{
			rumPopulateDataPage(rumstate, entry, maxoff, pageInner);
		}
		else
		{
			ptr = RumDataPageGetData(pageInner);

			/* Ensure the first entry is 0 initialized */
			memset(&entry->list[0], 0, sizeof(RumItem));
			for (i = FirstOffsetNumber; i <= maxoff; i = OffsetNumberNext(i))
			{
				ptr = rumDataPageLeafRead(ptr, entry->attnum, &item, true,
										  rumstate);
				entry->list[i - FirstOffsetNumber] = item;
			}
		}

		LockBuffer(entry->buffer, RUM_UNLOCK);
		entry->isFinished = false;
	}
	else if (RumGetNPosting(itup) > 0)
	{
		entry->nlist = RumGetNPosting(itup);
		entry->cachedLsn = InvalidXLogRecPtr;
		entry->predictNumberResult = (uint32) entry->nlist;
		entry->list = (RumItem *) palloc(sizeof(RumItem) * entry->nlist);

		rumReadTuple(rumstate, entry->attnum, itup, entry->list, true);
		entry->isFinished = setListPositionScanEntry(rumstate, entry);
	}

	Assert(entry->nlist > 0 && entry->list);

	entry->curItem = entry->list[entry->offset];
	entry->offset += entry->scanDirection;

	SCAN_ENTRY_GET_KEY(entry, rumstate, itup);

	/*
	 * Done with this entry, go to the next for the future.
	 */
	entry->stack->off++;

	if (needUnlock)
	{
		LockBuffer(entry->stack->buffer, RUM_UNLOCK);
	}

	return true;
}


#if PG_VERSION_NUM < 150000
#define rum_rand() (((double) random()) / ((double) MAX_RANDOM_VALUE))
#else
#define rum_rand() pg_prng_double(&pg_global_prng_state)
#endif

#define dropItem(e) (rum_rand() > ((double) RumFuzzySearchLimit) / \
					 ((double) ((e)->predictNumberResult)))

/*
 * Sets entry->curItem to next heap item pointer for one entry of one scan key,
 * or sets entry->isFinished to true if there are no more.
 *
 * Item pointers must be returned in ascending order.
 *
 * if advancePast is not null, uses that to move the find forward.
 */
static void
entryGetItem(RumState *rumstate, RumScanEntry entry,
			 bool *nextEntryList, Snapshot snapshot,
			 RumItem *advancePast, RumScanOpaque so)
{
	Assert(!entry->isFinished);

	if (nextEntryList)
	{
		*nextEntryList = false;
	}

	if (entry->matchSortstate)
	{
		Assert(ScanDirectionIsForward(entry->scanDirection));

		do {
			RumScanItem collected;
			RumScanItem *current_collected;
			RumScanItem current_collected_wrapper_item;

			/* We are finished, but should return last result */
			if (ItemPointerIsMax(&entry->collectRumItem.item.iptr))
			{
				entry->isFinished = true;
				rum_tuplesort_end(entry->matchSortstate);
				entry->matchSortstate = NULL;
				break;
			}

			/* collectRumItem could store the begining of current result */
			if (!ItemPointerIsMin(&entry->collectRumItem.item.iptr))
			{
				collected = entry->collectRumItem;
			}
			else
			{
				MemSet(&collected, 0, sizeof(collected));
			}

			ItemPointerSetMin(&entry->curItem.iptr);

			for (;;)
			{
				bool should_free;

				if (entry->isMatchMinimalTuple)
				{
					bool forward = true;
					ItemPointerData *current_collected_minimal_item;
					current_collected_minimal_item = rum_tuplesort_getrumitem_minimal(
						entry->matchSortstate,
						forward,
						&should_free);
					if (current_collected_minimal_item == NULL)
					{
						current_collected = NULL;
					}
					else
					{
						current_collected_wrapper_item.item.iptr =
							*current_collected_minimal_item;
						current_collected_wrapper_item.item.addInfoIsNull = true;
						current_collected_wrapper_item.keyValue = 0;
						current_collected_wrapper_item.keyCategory = RUM_CAT_NULL_KEY;
						current_collected = &current_collected_wrapper_item;
						if (should_free)
						{
							pfree(current_collected_minimal_item);
							should_free = false;
						}
					}
				}
				else
				{
					current_collected = rum_tuplesort_getrumitem(
						entry->matchSortstate,
						ScanDirectionIsForward(entry->scanDirection) ? true : false,
						&should_free);
				}

				if (current_collected == NULL)
				{
					entry->curItem = collected.item;
					if (entry->useCurKey)
					{
						entry->curKey = collected.keyValue;
						entry->curKeyCategory = collected.keyCategory;
					}
					break;
				}

				if (ItemPointerIsMin(&collected.item.iptr) ||
					rumCompareItemPointers(&collected.item.iptr,
										   &current_collected->item.iptr) == 0)
				{
					Datum joinedAddInfo = (Datum) 0;
					bool joinedAddInfoIsNull;

					if (ItemPointerIsMin(&collected.item.iptr))
					{
						joinedAddInfoIsNull = true; /* will change later */
						collected.item.addInfoIsNull = true;
					}
					else
					{
						joinedAddInfoIsNull = collected.item.addInfoIsNull ||
											  current_collected->item.addInfoIsNull;
					}

					if (joinedAddInfoIsNull)
					{
						joinedAddInfoIsNull =
							(collected.item.addInfoIsNull &&
							 current_collected->item.addInfoIsNull);

						if (collected.item.addInfoIsNull == false)
						{
							joinedAddInfo = collected.item.addInfo;
						}
						else if (current_collected->item.addInfoIsNull == false)
						{
							joinedAddInfo = current_collected->item.addInfo;
						}
					}
					else if (rumstate->canJoinAddInfo[entry->attnumOrig - 1])
					{
						joinedAddInfo =
							FunctionCall2(
								&rumstate->joinAddInfoFn[entry->attnumOrig - 1],
								collected.item.addInfo,
								current_collected->item.addInfo);
					}
					else
					{
						joinedAddInfo = current_collected->item.addInfo;
					}

					collected.item.iptr = current_collected->item.iptr;
					collected.item.addInfoIsNull = joinedAddInfoIsNull;
					collected.item.addInfo = joinedAddInfo;
					if (entry->useCurKey)
					{
						collected.keyValue = current_collected->keyValue;
						collected.keyCategory = current_collected->keyCategory;
					}

					if (should_free)
					{
						pfree(current_collected);
					}
				}
				else
				{
					entry->curItem = collected.item;
					entry->collectRumItem = *current_collected;
					if (entry->useCurKey)
					{
						entry->curKey = collected.keyValue;
						entry->curKeyCategory = collected.keyCategory;
					}
					if (should_free)
					{
						pfree(current_collected);
					}

					break;
				}
			}

			if (current_collected == NULL)
			{
				/* mark next call as last */
				ItemPointerSetMax(&entry->collectRumItem.item.iptr);

				/* even current call is last */
				if (ItemPointerIsMin(&entry->curItem.iptr))
				{
					entry->isFinished = true;
					rum_tuplesort_end(entry->matchSortstate);
					entry->matchSortstate = NULL;
					break;
				}
			}
		} while (entry->reduceResult == true && dropItem(entry));
	}
	else if (!BufferIsValid(entry->buffer))
	{
		if (entry->offset >= 0 && entry->offset < entry->nlist)
		{
			entry->curItem = entry->list[entry->offset];
			entry->offset += entry->scanDirection;
		}
		else if (entry->stack)
		{
			entry->offset++;
			if (entryGetNextItemList(rumstate, entry, snapshot) && nextEntryList)
			{
				*nextEntryList = true;
			}
		}
		else
		{
			ItemPointerSetInvalid(&entry->curItem.iptr);
			entry->isFinished = true;
		}
	}

	/* Get next item from posting tree */
	else
	{
		do {
			entryGetNextItem(rumstate, entry, snapshot, advancePast, so);
		} while (entry->isFinished == false &&
				 entry->reduceResult == true &&
				 dropItem(entry));
		if (entry->stack && entry->isFinished)
		{
			entry->isFinished = false;
			if (entryGetNextItemList(rumstate, entry, snapshot) && nextEntryList)
			{
				*nextEntryList = true;
			}
		}
	}
}


static void
keyGetItem(RumState *rumstate, MemoryContext tempCtx, RumScanKey key)
{
	RumItem minItem;
	uint32 i;
	RumScanEntry entry;
	bool res;
	MemoryContext oldCtx;
	bool allFinished = true;
	bool minItemInited = false;

	Assert(!key->isFinished);

	/*
	 * Find the minimum of the active entry curItems.
	 */

	for (i = 0; i < key->nentries; i++)
	{
		entry = key->scanEntry[i];
		if (entry->isFinished == false)
		{
			allFinished = false;

			if (minItemInited == false ||
				compareCurRumItemScanDirection(rumstate, entry, &minItem) < 0)
			{
				minItem = entry->curItem;
				minItemInited = true;
			}
		}
	}

	if (allFinished)
	{
		/* all entries are finished */
		key->isFinished = true;
		return;
	}

	/*
	 * We might have already tested this item; if so, no need to repeat work.
	 */
	if (rumCompareItemPointers(&key->curItem.iptr, &minItem.iptr) == 0)
	{
		return;
	}

	/*
	 * OK, advance key->curItem and perform consistentFn test.
	 */
	key->curItem = minItem;

	/* prepare for calling consistentFn in temp context */
	oldCtx = MemoryContextSwitchTo(tempCtx);

	/*
	 * Prepare entryRes array to be passed to consistentFn.
	 */
	for (i = 0; i < key->nentries; i++)
	{
		entry = key->scanEntry[i];
		if (entry->isFinished == false &&
			rumCompareItemPointers(&entry->curItem.iptr, &key->curItem.iptr) == 0)
		{
			key->entryRes[i] = true;
			key->addInfo[i] = entry->curItem.addInfo;
			key->addInfoIsNull[i] = entry->curItem.addInfoIsNull;
		}
		else
		{
			key->entryRes[i] = false;
			key->addInfo[i] = (Datum) 0;
			key->addInfoIsNull[i] = true;
		}
	}

	res = callConsistentFn(rumstate, key);

	key->curItemMatches = res;

	/* clean up after consistentFn calls */
	MemoryContextSwitchTo(oldCtx);
	MemoryContextReset(tempCtx);
}


/*
 * Checks that item is greater than
 * the advancePast specified if the advancePast is valid.
 * true if advancePast is not valid.
 */
inline static bool
IsScanEntryNotPast(RumState *rumstate,
				   RumScanEntry item,
				   RumItem *advancePast)
{
	return !ItemPointerIsValid(&advancePast->iptr) ||
		   compareCurRumItemScanDirection(rumstate, item, advancePast) <= 0;
}


/*
 * Checks that item is at least equal to or greater than
 * the advancePast specified if the advancePast is valid.
 * false if advancePast is not valid.
 */
inline static bool
IsScanEntryLessThan(RumState *rumstate,
					RumScanEntry item,
					RumItem *advancePast)
{
	return ItemPointerIsValid(&advancePast->iptr) &&
		   compareCurRumItemScanDirection(rumstate, item, advancePast) < 0;
}


/*
 * Get next heap item pointer (after advancePast) from scan.
 * Returns true if anything found.
 * On success, *item and *recheck are set.
 *
 * Note: this is very nearly the same logic as in keyGetItem(), except
 * that we know the keys are to be combined with AND logic, whereas in
 * keyGetItem() the combination logic is known only to the consistentFn.
 */
static bool
scanGetItemRegular(IndexScanDesc scan, RumItem *advancePast,
				   RumItem *item, bool *recheck)
{
	RumScanOpaque so = (RumScanOpaque) scan->opaque;
	RumState *rumstate = &so->rumstate;
	RumItem myAdvancePast = *advancePast;
	RumItem myIntermediatePast, myIntermediatePastTemp;
	uint32 i;
	bool allFinished;
	bool match, itemSet;

	/* Start by assuming we want to begin the scan at advancePast */
	RumItemSetInvalid(&myIntermediatePast);
	for (;;)
	{
		/*
		 * Advance any entries that are <= myAdvancePast according to
		 * scan direction. On first call myAdvancePast is invalid,
		 * so anyway we are needed to call entryGetItem()
		 */
		allFinished = true;

		for (i = 0; i < so->totalentries; i++)
		{
			RumScanEntry entry = so->entries[i];

			/* For a regular scan, we iterate on the scanEntry to find the next
			 * candidate. The next candidate entry is decided based on a few things:
			 * - the prior item that was a match (or if the first time, scan it anyway)
			 * - The prior item known to be the lower bound from a previous miss. This
			 *   happens for conjunctions and we track the Max(MinScanEntry) to move
			 *   the other entries forward.
			 */
			while (entry->isFinished == false &&
				   (IsScanEntryNotPast(rumstate, entry, &myAdvancePast) ||
					IsScanEntryLessThan(rumstate, entry, &myIntermediatePast)))
			{
				if (!entry->isPartialMatch &&
					IsScanEntryLessThan(rumstate, entry, &myIntermediatePast))
				{
					entryFindItem(rumstate, entry, &myIntermediatePast,
								  scan->xs_snapshot);
				}
				else
				{
					entryGetItem(rumstate, entry, NULL, scan->xs_snapshot,
								 &myIntermediatePast, so);
				}

				if (!ItemPointerIsValid(&myAdvancePast.iptr))
				{
					break;
				}
			}

			if (entry->isFinished == false)
			{
				allFinished = false;
			}
		}

		if (allFinished)
		{
			/* all entries exhausted, so we're done */
			return false;
		}

		/*
		 * Perform the consistentFn test for each scan key.  If any key
		 * reports isFinished, meaning its subset of the entries is exhausted,
		 * we can stop.  Otherwise, set *item to the minimum of the key
		 * curItems.
		 */

		itemSet = false;
		RumItemSetInvalid(&myIntermediatePastTemp);
		for (i = 0; i < so->nkeys; i++)
		{
			RumScanKey key = so->keys[i];
			int cmp;

			if (key->orderBy)
			{
				continue;
			}

			keyGetItem(&so->rumstate, so->tempCtx, key);

			if (key->isFinished)
			{
				return false;   /* finished one of keys */
			}
			if (itemSet == false)
			{
				*item = key->curItem;
				itemSet = true;
			}
			cmp = compareRumItem(rumstate, key->attnumOrig,
								 &key->curItem, item);
			if ((ScanDirectionIsForward(key->scanDirection) && cmp < 0) ||
				(ScanDirectionIsBackward(key->scanDirection) && cmp > 0))
			{
				*item = key->curItem;
			}

			/* key->curItem maps to the "lowest" TID recognized by this scan key
			 * Now track the "highest" of this curItem across all keys.
			 * We will use this in subsequent entryGetItem to skip entries that we
			 * know will never match. This is for instance in the case of
			 * A && B
			 * if A matches row >= (0, 200)
			 * and B matches row >= (0, 600)
			 * we know that we can safely scan from (0, 600) for future scans.
			 */
			if (!ItemPointerIsValid(&myIntermediatePastTemp.iptr) ||
				compareRumItem(rumstate, key->attnumOrig, &key->curItem,
							   &myIntermediatePastTemp) > 0)
			{
				myIntermediatePastTemp = key->curItem;
			}
		}

		/*----------
		 * Now *item contains first ItemPointer after previous result.
		 *----------
		 */
		match = true;
		for (i = 0; match && i < so->nkeys; i++)
		{
			RumScanKey key = so->keys[i];

			if (key->orderBy)
			{
				continue;
			}

			if (key->curItemMatches)
			{
				if (rumCompareItemPointers(&item->iptr, &key->curItem.iptr) == 0)
				{
					continue;
				}
			}
			match = false;
			break;
		}

		if (match)
		{
			break;
		}

		/*
		 * No hit.  Update myAdvancePast to this TID, so that on the next pass
		 * we'll move to the next possible entry.
		 */
		myAdvancePast = *item;

		/* In the case where we had a miss, we also track the
		 * highest intermediate TID we've seen - we use this to
		 * move the scan forward in subsequent scans.
		 */
		myIntermediatePast = myIntermediatePastTemp;

		so->scanLoops++;
	}

	/*
	 * We must return recheck = true if any of the keys are marked recheck.
	 */
	*recheck = false;
	for (i = 0; i < so->nkeys; i++)
	{
		RumScanKey key = so->keys[i];

		if (key->orderBy)
		{
			int j;

			/* Catch up order key with *item */
			for (j = 0; j < key->nentries; j++)
			{
				RumScanEntry entry = key->scanEntry[j];

				while (entry->isFinished == false &&
					   compareRumItem(rumstate, key->attnumOrig,
									  &entry->curItem, item) < 0)
				{
					entryGetItem(rumstate, entry, NULL, scan->xs_snapshot, NULL,
								 so);
				}
			}
		}
		else if (key->recheckCurItem)
		{
			*recheck = true;
			break;
		}
	}

	so->scanLoops++;
	return true;
}


/*
 * Finds part of page containing requested item using small index at the end
 * of page.
 */
static bool
scanPage(RumState *rumstate, RumScanEntry entry, RumItem *item, bool equalOk)
{
	int j;
	RumItem iter_item;
	Pointer ptr;
	OffsetNumber first = FirstOffsetNumber,
				 i,
				 maxoff;
	int16 bound = -1;
	bool found_eq = false;
	int cmp;
	Page page = BufferGetPage(entry->buffer);

	RumItemSetMin(&iter_item);

	if (ScanDirectionIsForward(entry->scanDirection) && !RumPageRightMost(page))
	{
		cmp = compareRumItem(rumstate, entry->attnumOrig,
							 RumDataPageGetRightBound(page), item);
		if (cmp < 0 || (cmp <= 0 && !equalOk))
		{
			return false;
		}
	}

	ptr = RumDataPageGetData(page);
	maxoff = RumDataPageMaxOff(page);

	for (j = 0; j < RumDataLeafIndexCount; j++)
	{
		RumDataLeafItemIndex *index = &RumPageGetIndexes(page)[j];

		if (index->offsetNumer == InvalidOffsetNumber)
		{
			break;
		}

		if (rumstate->useAlternativeOrder)
		{
			RumItem k;

			convertIndexToKey(index, &k);
			cmp = compareRumItem(rumstate, entry->attnumOrig, &k, item);
		}
		else
		{
			cmp = rumCompareItemPointers(&index->iptr, &item->iptr);
		}

		if (cmp < 0 || (cmp <= 0 && !equalOk))
		{
			ptr = RumDataPageGetData(page) + index->pageOffset;
			first = index->offsetNumer;
			iter_item.iptr = index->iptr;
		}
		else
		{
			if (ScanDirectionIsBackward(entry->scanDirection))
			{
				if (j + 1 < RumDataLeafIndexCount)
				{
					maxoff = RumPageGetIndexes(page)[j + 1].offsetNumer;
				}
			}
			else
			{
				maxoff = index->offsetNumer - 1;
			}
			break;
		}
	}

	if (ScanDirectionIsBackward(entry->scanDirection) && first >= maxoff)
	{
		first = FirstOffsetNumber;
		ItemPointerSetMin(&iter_item.iptr);
		ptr = RumDataPageGetData(page);
	}

	entry->nlist = maxoff - first + 1;
	entry->cachedLsn = PageGetLSN(page);
	bound = -1;
	for (i = first; i <= maxoff; i++)
	{
		ptr = rumDataPageLeafRead(ptr, entry->attnum, &iter_item, true,
								  rumstate);
		entry->list[i - first] = iter_item;

		if (bound != -1)
		{
			continue;
		}

		cmp = compareRumItem(rumstate, entry->attnumOrig,
							 item, &iter_item);

		if (cmp <= 0)
		{
			bound = i - first;
			if (cmp == 0)
			{
				found_eq = true;
			}
		}
	}

	if (bound == -1)
	{
		if (ScanDirectionIsBackward(entry->scanDirection))
		{
			entry->offset = maxoff - first;
			goto end;
		}
		return false;
	}

	if (found_eq)
	{
		entry->offset = bound;
		if (!equalOk)
		{
			entry->offset += entry->scanDirection;
		}
	}
	else if (ScanDirectionIsBackward(entry->scanDirection))
	{
		entry->offset = bound - 1;
	}
	else
	{
		entry->offset = bound;
	}

	if (entry->offset < 0 || entry->offset >= entry->nlist)
	{
		return false;
	}

end:
	entry->curItem = entry->list[entry->offset];
	entry->offset += entry->scanDirection;
	return true;
}


/*
 * Find item of scan entry wich is greater or equal to the given item.
 */
static void
entryFindItem(RumState *rumstate, RumScanEntry entry, RumItem *item, Snapshot snapshot)
{
	Page page;
	if (entry->nlist == 0)
	{
		entry->isFinished = true;
		return;
	}

	/* Try to find in loaded part of page */
	if ((ScanDirectionIsForward(entry->scanDirection) &&
		 compareRumItem(rumstate, entry->attnumOrig,
						&entry->list[entry->nlist - 1], item) >= 0) ||
		(ScanDirectionIsBackward(entry->scanDirection) &&
		 compareRumItem(rumstate, entry->attnumOrig,
						&entry->list[0], item) <= 0))
	{
		if (compareRumItemScanDirection(rumstate, entry->attnumOrig,
										entry->scanDirection,
										&entry->curItem, item) >= 0)
		{
			return;
		}
		while (entry->offset >= 0 && entry->offset < entry->nlist)
		{
			if (compareRumItemScanDirection(rumstate, entry->attnumOrig,
											entry->scanDirection,
											&entry->list[entry->offset],
											item) >= 0)
			{
				entry->curItem = entry->list[entry->offset];
				entry->offset += entry->scanDirection;
				return;
			}
			entry->offset += entry->scanDirection;
		}
	}

	if (!BufferIsValid(entry->buffer))
	{
		entry->isFinished = true;
		return;
	}

	/* Check rest of page */
	LockBuffer(entry->buffer, RUM_SHARE);

	/* If the page got split by the time we get here, then refind the leftmost page */
	page = BufferGetPage(entry->buffer);
	while (!RumPageIsLeaf(page))
	{
		RumBtreeData btree;
		BlockNumber newBlock;
		Buffer newBuffer;
		rumPrepareDataScan(&btree, rumstate->index, entry->attnum, rumstate);
		newBlock = btree.getLeftMostPage(&btree, page);
		newBuffer = ReadBuffer(btree.index, newBlock);
		LockBuffer(newBuffer, RUM_SHARE);
		UnlockReleaseBuffer(entry->buffer);
		entry->buffer = newBuffer;
		page = BufferGetPage(entry->buffer);
	}

	PredicateLockPage(rumstate->index, BufferGetBlockNumber(entry->buffer), snapshot);

	if (scanPage(rumstate, entry, item, true))
	{
		LockBuffer(entry->buffer, RUM_UNLOCK);
		return;
	}

	/* Try to traverse to another leaf page */
	entry->gdi->btree.items = item;
	entry->gdi->btree.curitem = 0;
	entry->gdi->btree.fullScan = false;

	entry->gdi->stack->buffer = entry->buffer;
	entry->gdi->stack = rumReFindLeafPage(&entry->gdi->btree, entry->gdi->stack);
	entry->buffer = entry->gdi->stack->buffer;

	PredicateLockPage(rumstate->index, BufferGetBlockNumber(entry->buffer), snapshot);

	if (scanPage(rumstate, entry, item, true))
	{
		LockBuffer(entry->buffer, RUM_UNLOCK);
		return;
	}

	/* At last try to traverse by direction */
	for (;;)
	{
		entry->buffer = rumStep(entry->buffer, rumstate->index,
								RUM_SHARE, entry->scanDirection);
		entry->gdi->stack->buffer = entry->buffer;

		if (entry->buffer == InvalidBuffer)
		{
			ItemPointerSetInvalid(&entry->curItem.iptr);
			entry->isFinished = true;
			return;
		}

		PredicateLockPage(rumstate->index, BufferGetBlockNumber(entry->buffer), snapshot);

		entry->gdi->stack->blkno = BufferGetBlockNumber(entry->buffer);

		if (scanPage(rumstate, entry, item, true))
		{
			LockBuffer(entry->buffer, RUM_UNLOCK);
			return;
		}
	}
}


/*
 * Do preConsistent check for all the key where applicable.
 */
static bool
preConsistentCheck(RumScanOpaque so)
{
	RumState *rumstate = &so->rumstate;
	uint32 i,
		   j;
	bool recheck;

	for (j = 0; j < so->nkeys; j++)
	{
		RumScanKey key = so->keys[j];
		bool hasFalse = false;

		if (key->orderBy)
		{
			continue;
		}

		if (key->searchMode == GIN_SEARCH_MODE_EVERYTHING)
		{
			continue;
		}

		if (!so->rumstate.canPreConsistent[key->attnum - 1])
		{
			continue;
		}

		for (i = 0; i < key->nentries; i++)
		{
			RumScanEntry entry = key->scanEntry[i];

			key->entryRes[i] = entry->preValue;
			if (!entry->preValue)
			{
				hasFalse = true;
			}
		}

		if (!hasFalse)
		{
			continue;
		}

		if (!DatumGetBool(FunctionCall8Coll(&rumstate->preConsistentFn[key->attnum - 1],
											rumstate->supportCollation[key->attnum - 1],
											PointerGetDatum(key->entryRes),
											UInt16GetDatum(key->strategy),
											key->query,
											UInt32GetDatum(key->nuserentries),
											PointerGetDatum(key->extra_data),
											PointerGetDatum(&recheck),
											PointerGetDatum(key->queryValues),
											PointerGetDatum(key->queryCategories)

											)))
		{
			return false;
		}
	}
	return true;
}


/*
 * Shift value of some entry which index in so->sortedEntries is equal or greater
 * to i.
 */
static void
entryShift(int i, RumScanOpaque so, bool find, Snapshot snapshot)
{
	int minIndex = -1,
		j;
	uint32 minPredictNumberResult = 0;
	RumState *rumstate = &so->rumstate;

	/*
	 * It's more efficient to move entry with smallest posting list/tree. So
	 * find one.
	 */
	for (j = i; j < so->totalentries; j++)
	{
		if (minIndex < 0 ||
			so->sortedEntries[j]->predictNumberResult < minPredictNumberResult)
		{
			minIndex = j;
			minPredictNumberResult = so->sortedEntries[j]->predictNumberResult;
		}
	}

	/* Do shift of required type */
	if (find)
	{
		entryFindItem(rumstate, so->sortedEntries[minIndex],
					  &so->sortedEntries[i - 1]->curItem, snapshot);
	}
	else if (!so->sortedEntries[minIndex]->isFinished)
	{
		entryGetItem(rumstate, so->sortedEntries[minIndex], NULL, snapshot, NULL,
					 so);
	}

	/* Restore order of so->sortedEntries */
	while (minIndex > 0 &&
		   cmpEntries(rumstate, so->sortedEntries[minIndex],
					  so->sortedEntries[minIndex - 1]) > 0)
	{
		RumScanEntry tmp;

		tmp = so->sortedEntries[minIndex];
		so->sortedEntries[minIndex] = so->sortedEntries[minIndex - 1];
		so->sortedEntries[minIndex - 1] = tmp;
		minIndex--;
	}
}


/*
 * Get next item pointer using fast scan.
 */
static bool
scanGetItemFast(IndexScanDesc scan, RumItem *advancePast,
				RumItem *item, bool *recheck)
{
	RumScanOpaque so = (RumScanOpaque) scan->opaque;
	int i,
		j,
		k;
	bool preConsistentResult,
		 consistentResult;

	if (so->entriesIncrIndex >= 0)
	{
		for (k = so->entriesIncrIndex; k < so->totalentries; k++)
		{
			entryShift(k, so, false, scan->xs_snapshot);
		}
	}

	for (;;)
	{
		/*
		 * Our entries is ordered by descending of item pointers. The first
		 * goal is to find border where preConsistent becomes false.
		 */
		preConsistentResult = true;
		so->scanLoops++;
		j = 0;
		k = 0;
		for (i = 0; i < so->totalentries; i++)
		{
			so->sortedEntries[i]->preValue = true;
		}
		for (i = 1; i < so->totalentries; i++)
		{
			if (cmpEntries(&so->rumstate, so->sortedEntries[i], so->sortedEntries[i -
																				  1]) < 0)
			{
				k = i;
				for (; j < i; j++)
				{
					so->sortedEntries[j]->preValue = false;
				}

				if ((preConsistentResult = preConsistentCheck(so)) == false)
				{
					break;
				}
			}
		}

		/*
		 * If we found false in preConsistent then we can safely move entries
		 * which was true in preConsistent argument.
		 */
		if (so->sortedEntries[i - 1]->isFinished == true)
		{
			return false;
		}

		if (preConsistentResult == false)
		{
			entryShift(i, so, true, scan->xs_snapshot);
			continue;
		}

		/* Call consistent method */
		consistentResult = true;
		for (i = 0; i < so->nkeys; i++)
		{
			RumScanKey key = so->keys[i];

			if (key->orderBy)
			{
				continue;
			}

			for (j = 0; j < key->nentries; j++)
			{
				RumScanEntry entry = key->scanEntry[j];

				if (entry->isFinished == false &&
					rumCompareItemPointers(&entry->curItem.iptr,
										   &so->sortedEntries[so->totalentries -
															  1]->curItem.iptr) == 0)
				{
					key->entryRes[j] = true;
					key->addInfo[j] = entry->curItem.addInfo;
					key->addInfoIsNull[j] = entry->curItem.addInfoIsNull;
				}
				else
				{
					key->entryRes[j] = false;
					key->addInfo[j] = (Datum) 0;
					key->addInfoIsNull[j] = true;
				}
			}

			if (!callConsistentFn(&so->rumstate, key))
			{
				consistentResult = false;
				for (j = k; j < so->totalentries; j++)
				{
					entryShift(j, so, false, scan->xs_snapshot);
				}
				continue;
			}
		}

		if (consistentResult == false)
		{
			continue;
		}

		/* Calculate recheck from each key */
		*recheck = false;
		for (i = 0; i < so->nkeys; i++)
		{
			RumScanKey key = so->keys[i];

			if (key->orderBy)
			{
				continue;
			}

			if (key->recheckCurItem)
			{
				*recheck = true;
				break;
			}
		}

		*item = so->sortedEntries[so->totalentries - 1]->curItem;
		so->entriesIncrIndex = k;

		return true;
	}
	return false;
}


/*
 * Get next item pointer using full-index scan.
 *
 * First key is used to full scan, other keys are only used for ranking.
 */
static bool
scanGetItemFull(IndexScanDesc scan, RumItem *advancePast,
				RumItem *item, bool *recheck)
{
	RumScanOpaque so = (RumScanOpaque) scan->opaque;
	RumScanKey key;
	RumScanEntry entry;
	bool nextEntryList;
	uint32 i;

	Assert(so->nkeys > 0 && so->totalentries > 0);
	Assert(so->entries[0]->scanWithAddInfo);

	/* Full-index scan key */
	key = so->keys[0];
	Assert(key->searchMode == GIN_SEARCH_MODE_EVERYTHING);

	/*
	 * This is first entry of the first key, which is used for full-index
	 * scan.
	 */
	entry = so->entries[0];

	if (entry->isFinished)
	{
		return false;
	}

	entryGetItem(&so->rumstate, entry, &nextEntryList, scan->xs_snapshot, NULL,
				 so);

	if (entry->isFinished)
	{
		return false;
	}

	/* Fill outerAddInfo */
	key->entryRes[0] = true;
	key->addInfo[0] = entry->curItem.addInfo;
	key->addInfoIsNull[0] = entry->curItem.addInfoIsNull;
	callAddInfoConsistentFn(&so->rumstate, key);

	/* Move related order by entries */
	if (nextEntryList)
	{
		for (i = 1; i < so->totalentries; i++)
		{
			RumScanEntry orderEntry = so->entries[i];
			if (orderEntry->nlist > 0)
			{
				orderEntry->isFinished = false;
				orderEntry->offset = InvalidOffsetNumber;
				RumItemSetMin(&orderEntry->curItem);
			}
		}
	}

	for (i = 1; i < so->totalentries; i++)
	{
		RumScanEntry orderEntry = so->entries[i];

		while (orderEntry->isFinished == false &&
			   (!ItemPointerIsValid(&orderEntry->curItem.iptr) ||
				compareCurRumItemScanDirection(&so->rumstate, orderEntry,
											   &entry->curItem) < 0))
		{
			entryGetItem(&so->rumstate, orderEntry, NULL, scan->xs_snapshot, NULL,
						 so);
		}
	}

	*item = entry->curItem;
	*recheck = false;
	return true;
}


inline static void
CopyPageContents(Page sourcePage, Page targetPage)
{
	Size pageSize = PageGetPageSize(sourcePage);
	memcpy(targetPage, sourcePage, pageSize);
}


static bool
MoveBuffersForOrderedScan(RumScanOpaque so, RumBtree btree)
{
	RumOrderByScanData *scanData = so->orderByScanData;
	Page page;
	BlockNumber nextBlockNo = InvalidBlockNumber;
	IndexTuple boundTuple = NULL;
	OffsetNumber boundTupleOffset = InvalidOffsetNumber;
	if (!scanData->isPageValid)
	{
		/* First time after startOrderedScan is called - need to init from current buffer page */
		LockBuffer(scanData->orderStack->buffer, RUM_SHARE);
		page = BufferGetPage(scanData->orderStack->buffer);
		CopyPageContents(page, scanData->orderByEntryPageCopy);
		scanData->isPageValid = true;
		LockBuffer(scanData->orderStack->buffer, RUM_UNLOCK);
		return true;
	}

	/* We have a page already, check if it's reusable */
	if (ScanDirectionIsForward(so->orderScanDirection))
	{
		if (scanData->orderStack->off <= PageGetMaxOffsetNumber(
				scanData->orderByEntryPageCopy))
		{
			/* Current page is still valid */
			return true;
		}

		LockBuffer(scanData->orderStack->buffer, RUM_SHARE);
		page = BufferGetPage(scanData->orderStack->buffer);
		if (RumPageRightMost(page))
		{
			goto cleanupOnMissing;
		}

		/* Store the target block as per the cached result */
		nextBlockNo = RumPageGetOpaque(scanData->orderByEntryPageCopy)->rightlink;
		boundTupleOffset = PageGetMaxOffsetNumber(scanData->orderByEntryPageCopy);
	}
	else
	{
		if (scanData->orderStack->off >= FirstOffsetNumber)
		{
			/* Current page is still valid */
			return true;
		}

		LockBuffer(scanData->orderStack->buffer, RUM_SHARE);
		page = BufferGetPage(scanData->orderStack->buffer);
		if (RumPageLeftMost(page))
		{
			goto cleanupOnMissing;
		}

		/* Store the target block as per the cached result */
		nextBlockNo = RumPageGetOpaque(scanData->orderByEntryPageCopy)->leftlink;
		boundTupleOffset = FirstOffsetNumber;
	}

	/* About to move pages, handle kill item stuff */
	if (RumEnableSupportDeadIndexItems && so->numKilled > 0)
	{
		RumKillEntryItems(so, scanData);
	}

	/* Now do the step to the direction requested */
	scanData->orderStack->buffer = rumStep(
		scanData->orderStack->buffer, btree->index, RUM_SHARE,
		so->orderScanDirection);
	scanData->orderStack->blkno = BufferGetBlockNumber(scanData->orderStack->buffer);

	if (scanData->orderStack->blkno != nextBlockNo)
	{
		/* Page pointer was split since we last looked at it
		 * Store the indexTuple from the prior page at the bounds - we will use
		 * this to skip entries until we hit the right one again.
		 */
		if (boundTuple == NULL)
		{
			/* track the last known tuple we scanned first - this is helpful in resuming
			 * from this point (and tuples before this in scanOrder will be skipped).
			 */
			boundTuple = (IndexTuple) PageGetItem(
				scanData->orderByEntryPageCopy,
				PageGetItemId(scanData->orderByEntryPageCopy,
							  boundTupleOffset));
			boundTuple = CopyIndexTuple(boundTuple);
			scanData->boundEntryTuple = boundTuple;
		}
	}

	/* Found a valid buffer to move to, now copy the buffer into the temp storage */
	page = BufferGetPage(scanData->orderStack->buffer);
	CopyPageContents(page, scanData->orderByEntryPageCopy);
	scanData->isPageValid = true;
	scanData->orderStack->off =
		ScanDirectionIsBackward(so->orderScanDirection) ?
		PageGetMaxOffsetNumber(scanData->orderByEntryPageCopy)
		: FirstOffsetNumber;
	LockBuffer(scanData->orderStack->buffer, RUM_UNLOCK);
	return true;

cleanupOnMissing:
	ItemPointerSetInvalid(&scanData->orderByEntry->curItem.iptr);
	scanData->orderByEntry->isFinished = true;
	LockBuffer(scanData->orderStack->buffer, RUM_UNLOCK);
	return false;
}


static bool
MoveBuffersForOrderedScanParallel(RumScanOpaque so, RumBtree btree, ParallelIndexScanDesc
								  parallelScan)
{
	RumOrderByScanData *scanData = so->orderByScanData;
	Page page;
	if (ScanDirectionIsForward(so->orderScanDirection))
	{
		if (scanData->isPageValid &&
			scanData->orderStack->off <= PageGetMaxOffsetNumber(
				scanData->orderByEntryPageCopy))
		{
			/* Current page is still valid */
			return true;
		}

		/* About to move pages, kill entries if needed */
		if (scanData->isPageValid && RumEnableSupportDeadIndexItems &&
			so->numKilled > 0)
		{
			RumKillEntryItems(so, scanData);
		}

		while (true)
		{
			BlockNumber startingBlock;
			bool hasMore = rum_parallel_seize(parallelScan, &startingBlock);
			if (!hasMore)
			{
				return false;
			}

			if (startingBlock == InvalidBlockNumber)
			{
				/* We won the race and have registered the starting block
				 * start by copying this and moving forward on this buffer while
				 * notifying the parallel state that we've currently processed this block.
				 */
				LockBuffer(scanData->orderStack->buffer, RUM_SHARE);
				page = BufferGetPage(scanData->orderStack->buffer);
				CopyPageContents(page, scanData->orderByEntryPageCopy);
				scanData->isPageValid = true;

				/* In the forward scan we store the right link as read right now in the parallel data */
				rum_parallel_release(parallelScan, RumPageRightLink(page));
				LockBuffer(scanData->orderStack->buffer, RUM_UNLOCK);
				return true;
			}

			/*
			 * Some other thread had updated the parallel state already, this page is the right sibling of
			 * the page that was last scanned. We now hold the lock on traversal of pages. The current page
			 * is considered valid if it's not dead.
			 */
			ReleaseBuffer(scanData->orderStack->buffer);
			scanData->orderStack->blkno = startingBlock;
			scanData->orderStack->buffer = ReadBuffer(btree->index, startingBlock);
			LockBuffer(scanData->orderStack->buffer, RUM_SHARE);
			page = BufferGetPage(scanData->orderStack->buffer);

			/* Let other threads move on with the next buffer */
			/* In the forward scan we store the right link as read right now in the parallel data */
			rum_parallel_release(parallelScan, RumPageRightLink(page));
			if (RumPageIsDeleted(page) || RumPageIsHalfDead(page))
			{
				/* TODO: Should we release here? */
				continue;
			}
			else
			{
				/* The page is valid - use it */
				CopyPageContents(page, scanData->orderByEntryPageCopy);
				scanData->isPageValid = true;

				LockBuffer(scanData->orderStack->buffer, RUM_UNLOCK);
				return true;
			}
		}
	}
	else
	{
		/* Backward scan logic */
		ereport(ERROR, (errmsg("TODO : IMPLEMENT ME")));
	}
}


static bool
MoveScanForward(RumScanOpaque so, Snapshot snapshot, ParallelIndexScanDesc parallelScan)
{
	Page page;
	IndexTuple itup;
	RumBtreeData btree;
	Datum idatum;
	RumNullCategory icategory;
	bool isIndexMatch;
	bool markedEntryFinished = false;
	bool scanFinished = false, canSkipCheck = false;
	RumScanEntry entry = so->orderByScanData->orderByEntry;

	Assert(entry->isFinished);
	Assert(so->orderByScanData->orderStack);
	Assert(ScanDirectionIsForward(entry->scanDirection));

	ResetEntryItem(entry);

	rumPrepareEntryScan(&btree, entry->attnum,
						entry->queryKey, entry->queryCategory,
						&so->rumstate);

	for (;;)
	{
		/*
		 * stack->off points to the interested entry, buffer is already locked
		 */
		bool moveResult = parallelScan != NULL ?
						  MoveBuffersForOrderedScanParallel(so, &btree, parallelScan) :
						  MoveBuffersForOrderedScan(so, &btree);
		ItemId itemId;
		if (!moveResult)
		{
			return false;
		}

		page = so->orderByScanData->orderByEntryPageCopy;

		itemId = PageGetItemId(page, so->orderByScanData->orderStack->off);
		itup = (IndexTuple) PageGetItem(page, itemId);

		/*
		 * If tuple stores another attribute then stop scan (or walk back)
		 * for reverse scan.
		 */
		if (rumtuple_get_attrnum(btree.rumstate, itup) != entry->attnum)
		{
			if (ScanDirectionIsBackward(so->orderScanDirection))
			{
				so->orderByScanData->orderStack->off += so->orderScanDirection;
				continue;
			}

			ItemPointerSetInvalid(&entry->curItem.iptr);
			entry->isFinished = true;
			return false;
		}

		/* If the request is to ignore killed tuples, check that */
		if (IsEntryDeadForKilledTuple(so->ignoreKilledTuples, itemId))
		{
			so->orderByScanData->orderStack->off += so->orderScanDirection;
			so->killedItemsSkipped++;
			continue;
		}

		/* Check if the current tuple matches */
		idatum = rumtuple_get_key(&so->rumstate, itup, &icategory);

		markedEntryFinished = false;
		scanFinished = false;
		canSkipCheck = false;
		isIndexMatch = ValidateIndexEntry(so, idatum, &markedEntryFinished,
										  &scanFinished, &canSkipCheck);

		if (scanFinished)
		{
			return false;
		}

		if (!isIndexMatch)
		{
			if (markedEntryFinished)
			{
				/* Some scanEntry just got marked as finished - find the new minimum scanEntry */
				RumScanEntry newMinEntry = getMinScanEntry(so);
				if (newMinEntry != NULL)
				{
					int cmp = CompareRumKeyScanDirection(so,
														 newMinEntry->attnum,
														 newMinEntry->queryKey,
														 newMinEntry->queryCategory,
														 idatum,
														 newMinEntry->queryCategory);
					if (cmp > 0)
					{
						/* start the orderedScan again with the new entry now */
						startScanEntryOrderedCore(so, newMinEntry, snapshot);
						newMinEntry->isFinished = false;
						entry = so->orderByScanData->orderByEntry;
						if (!so->orderByScanData->orderStack)
						{
							/* There is no entry left - close scan */
							return false;
						}
						else
						{
							/* move right and continue */
							continue;
						}
					}

					/* fall through to increment below as current value is better than the minKey available */
				}
			}
			else if (RumEnableSkipIntermediateEntry && canSkipCheck &&
					 ScanDirectionIsForward(so->orderScanDirection) &&
					 so->totalentries == 1 &&
					 so->rumstate.canOuterOrdering[so->orderByScanData->orderByEntry->
												   attnum - 1] &&
					 so->rumstate.outerOrderingFn[so->orderByScanData->orderByEntry->
												  attnum - 1].fn_nargs == 4)
			{
				/* In this path, the orderbyEntry marked itself to push the scan forward due to the tail entry being done
				 * but the prefix of it being unbounded.
				 * TODO: Lift the restriction for so->totalentries == 1 by tracking multiple orderby entries here.
				 * That's because the new value for the sortEntry *could* skip over ranges that are valid for other entries.
				 */
				bool resetScan = false;
				MemoryContext oldCtx = MemoryContextSwitchTo(so->tempCtx);
				bool attbyval = TupleDescAttr(so->rumstate.origTupdesc, entry->attnum -
											  1)->attbyval;
				int attlen = TupleDescAttr(so->rumstate.origTupdesc, entry->attnum -
										   1)->attlen;
				Datum entryToUse = entry->queryKeyOverride != (Datum) 0 ?
								   entry->queryKeyOverride : entry->queryKey;
				Datum recheckDatum = FunctionCall4Coll(
					&so->rumstate.outerOrderingFn[so->orderByScanData->orderByEntry->
												  attnum - 1],
					so->rumstate.supportCollation[so->orderByScanData->orderByEntry->
												  attnum - 1],
					idatum,
					entryToUse,
					UInt16GetDatum(RumIndexTransform_IndexGenerateSkipBound),
					PointerGetDatum(so->orderByScanData->orderByEntry->extra_data));
				MemoryContextSwitchTo(oldCtx);

				if (recheckDatum != (Datum) 0)
				{
					if (!attbyval && entry->queryKeyOverride != (Datum) 0)
					{
						pfree(DatumGetPointer(entry->queryKeyOverride));
					}

					entry->queryKeyOverride = datumTransfer(recheckDatum, attbyval,
															attlen);
					resetScan = true;
				}

				MemoryContextReset(so->tempCtx);

				if (resetScan && parallelScan == NULL)
				{
					/* Check if it's worth moving to the next page */
					btree.entryKey = entry->queryKeyOverride;
					if (entryIsMoveRight(&btree, page))
					{
						startScanEntryOrderedCore(so, entry, snapshot);
						entry->isFinished = false;
						if (!so->orderByScanData->orderStack)
						{
							/* There is no entry left - close scan */
							return false;
						}
						else
						{
							/* move right and continue */
							continue;
						}
					}
					else
					{
						OffsetNumber targetOffset = so->orderByScanData->orderStack->off;
						entryLocateLeafEntryBounds(&btree, page,
												   so->orderByScanData->orderStack->off,
												   PageGetMaxOffsetNumber(page),
												   &targetOffset);
						if (targetOffset > so->orderByScanData->orderStack->off)
						{
							so->orderByScanData->orderStack->off = targetOffset;
							continue;
						}
					}
				}
			}

			so->orderByScanData->orderStack->off += so->orderScanDirection;
			continue;
		}

		PrepareOrderedMatchedEntry(so, entry, snapshot, itup);
		if (entry->nlist == 0)
		{
			while (!entry->isFinished && entry->nlist == 0)
			{
				/* Rev the entry until we have an nlist that is > 0 or the item is finished */
				entryGetItem(&so->rumstate, entry, NULL, snapshot, NULL, so);
			}

			if (entry->isFinished)
			{
				ResetEntryItem(entry);

				/* Dead tuple due to vacuum, move forward */
				so->orderByScanData->orderStack->off += so->orderScanDirection;
				continue;
			}
		}
		else
		{
			Assert(entry->nlist > 0 && entry->list);

			entry->curItem = entry->list[entry->offset];
			entry->offset += entry->scanDirection;
		}

		/*
		 * Done with this entry, go to the next for the future.
		 */
		so->orderByScanData->orderStack->off += so->orderScanDirection;
		return true;
	}
}


static bool
scanGetItemOrdered(IndexScanDesc scan, RumItem *advancePast,
				   RumItem *item, bool *recheck, bool *recheckOrderby)
{
	RumScanOpaque so = (RumScanOpaque) scan->opaque;
	if (!so->orderByScanData->orderByEntry->isFinished)
	{
		entryGetItem(&so->rumstate, so->orderByScanData->orderByEntry, NULL,
					 scan->xs_snapshot, NULL, so);
	}

	if (so->orderByScanData->orderByEntry->isFinished)
	{
		/* Check if we can move forward to the next entry */
		if (so->orderByScanData->orderStack == NULL)
		{
			return false;
		}

		if (!MoveScanForward(so, scan->xs_snapshot, scan->parallel_scan))
		{
			return false;
		}
	}

	*item = so->orderByScanData->orderByEntry->curItem;

	/* If we're rechecking the order by, also recheck the filters for good measure */
	*recheck = so->recheckCurrentItem || so->recheckCurrentItemOrderBy;
	*recheckOrderby = so->recheckCurrentItemOrderBy;

	if (so->orderByHasRecheck)
	{
		for (int i = so->orderByKeyIndex; i < so->nkeys; i++)
		{
			RumScanKey orderByKey = so->keys[i];
			if (orderByKey->orderBy)
			{
				scan->xs_orderbyvals[i - so->orderByKeyIndex] = orderByKey->curKey;
				scan->xs_orderbynulls[i - so->orderByKeyIndex] = false;
			}
		}
	}
	return true;
}


/*
 * Get next item whether using regular or fast scan.
 */
static bool
scanGetItem(IndexScanDesc scan, RumItem *advancePast,
			RumItem *item, bool *recheck, bool *recheckOrderby)
{
	RumScanOpaque so = (RumScanOpaque) scan->opaque;

	if (so->scanType == RumFastScan)
	{
		return scanGetItemFast(scan, advancePast, item, recheck);
	}
	else if (so->scanType == RumFullScan)
	{
		return scanGetItemFull(scan, advancePast, item, recheck);
	}
	else if (so->scanType == RumOrderedScan)
	{
		return scanGetItemOrdered(scan, advancePast, item, recheck, recheckOrderby);
	}
	else
	{
		return scanGetItemRegular(scan, advancePast, item, recheck);
	}
}


#define RumIsNewKey(s) (((RumScanOpaque) scan->opaque)->keys == NULL)
#define RumIsVoidRes(s) (((RumScanOpaque) scan->opaque)->isVoidRes)

int64
rumgetbitmap(IndexScanDesc scan, TIDBitmap *tbm)
{
	RumScanOpaque so = (RumScanOpaque) scan->opaque;
	int64 ntids = 0;
	bool recheck, recheckOrderbyIgnore;
	RumItem item;

	/*
	 * Set up the scan keys, and check for unsatisfiable query.
	 */
	if (RumIsNewKey(scan))
	{
		rumNewScanKey(scan);
	}

	if (RumIsVoidRes(scan))
	{
		return 0;
	}

	ntids = 0;

	so->entriesIncrIndex = -1;

	/*
	 * Now scan the main index.
	 */
	startScan(scan);

	ItemPointerSetInvalid(&item.iptr);

	for (;;)
	{
		CHECK_FOR_INTERRUPTS();

		if (!scanGetItem(scan, &item, &item, &recheck, &recheckOrderbyIgnore))
		{
			break;
		}

		tbm_add_tuples(tbm, &item.iptr, 1, recheck);
		ntids++;
	}

	return ntids;
}


static float8
keyGetOrdering(RumState *rumstate, MemoryContext tempCtx, RumScanKey key,
			   ItemPointer iptr)
{
	RumScanEntry entry;
	uint32 i;

	if (key->useAddToColumn)
	{
		Assert(key->nentries == 0);
		Assert(key->nuserentries == 0);

		if (key->outerAddInfoIsNull)
		{
			return get_float8_infinity();
		}

		if (rumstate->outerOrderingFn[rumstate->attrnAttachColumn - 1].fn_nargs != 3)
		{
			ereport(ERROR, (errmsg(
								"Cannot order by addToColumn and have order by raw keys")));
		}

		return DatumGetFloat8(FunctionCall3(
								  &rumstate->outerOrderingFn[rumstate->attrnAttachColumn -
															 1],
								  key->outerAddInfo,
								  key->queryValues[0],
								  UInt16GetDatum(key->strategy)
								  ));
	}
	else if (key->useCurKey)
	{
		if (rumstate->orderingFn[key->attnum - 1].fn_nargs != 3)
		{
			ereport(ERROR, (errmsg("Cannot order by curKey and have order by raw keys")));
		}

		Assert(key->nentries == 0);
		Assert(key->nuserentries == 0);

		if (key->curKeyCategory != RUM_CAT_NORM_KEY)
		{
			return get_float8_infinity();
		}

		return DatumGetFloat8(FunctionCall3(
								  &rumstate->orderingFn[key->attnum - 1],
								  key->curKey,
								  key->query,
								  UInt16GetDatum(key->strategy)
								  ));
	}

	for (i = 0; i < key->nentries; i++)
	{
		entry = key->scanEntry[i];
		if (entry->isFinished == false &&
			rumCompareItemPointers(&entry->curItem.iptr, iptr) == 0)
		{
			key->addInfo[i] = entry->curItem.addInfo;
			key->addInfoIsNull[i] = entry->curItem.addInfoIsNull;
			key->entryRes[i] = true;
		}
		else
		{
			key->addInfo[i] = (Datum) 0;
			key->addInfoIsNull[i] = true;
			key->entryRes[i] = false;
		}
	}

	if (rumstate->orderingFn[key->attnum - 1].fn_nargs != 10)
	{
		ereport(ERROR, (errmsg("Cannot order by curKey and have order by raw keys")));
	}

	return DatumGetFloat8(FunctionCall10Coll(&rumstate->orderingFn[key->attnum - 1],
											 rumstate->supportCollation[key->attnum - 1],
											 PointerGetDatum(key->entryRes),
											 UInt16GetDatum(key->strategy),
											 key->query,
											 UInt32GetDatum(key->nuserentries),
											 PointerGetDatum(key->extra_data),
											 PointerGetDatum(&key->recheckCurItem),
											 PointerGetDatum(key->queryValues),
											 PointerGetDatum(key->queryCategories),
											 PointerGetDatum(key->addInfo),
											 PointerGetDatum(key->addInfoIsNull)
											 ));
}


static void
insertScanItem(RumScanOpaque so, bool recheck)
{
	RumSortItem *item;
	uint32 i,
		   j;

	item = (RumSortItem *)
		   MemoryContextAllocZero(rum_tuplesort_get_memorycontext(so->sortstate),
								  RumSortItemSize(so->norderbys));
	item->iptr = so->item.iptr;
	item->recheck = recheck;

	if (AttributeNumberIsValid(so->rumstate.attrnAddToColumn) || so->willSort)
	{
		int nOrderByAnother = 0,
			nOrderByKey = 0,
			countByAnother = 0,
			countByKey = 0;

		for (i = 0; i < so->nkeys; i++)
		{
			if (so->keys[i]->useAddToColumn)
			{
				so->keys[i]->outerAddInfoIsNull = true;
				nOrderByAnother++;
			}
			else if (so->keys[i]->useCurKey)
			{
				nOrderByKey++;
			}
		}

		for (i = 0; (countByAnother < nOrderByAnother || countByKey < nOrderByKey) &&
			 i < so->nkeys; i++)
		{
			if (countByAnother < nOrderByAnother &&
				so->keys[i]->attnum == so->rumstate.attrnAddToColumn &&
				so->keys[i]->outerAddInfoIsNull == false)
			{
				Assert(!so->keys[i]->orderBy);
				Assert(!so->keys[i]->useAddToColumn);

				for (j = i; j < so->nkeys; j++)
				{
					if (so->keys[j]->useAddToColumn &&
						so->keys[j]->outerAddInfoIsNull == true)
					{
						so->keys[j]->outerAddInfoIsNull = false;
						so->keys[j]->outerAddInfo = so->keys[i]->outerAddInfo;
						countByAnother++;
					}
				}
			}
			else if (countByKey < nOrderByKey && so->keys[i]->nentries > 0 &&
					 so->keys[i]->scanEntry[0]->useCurKey)
			{
				Assert(!so->keys[i]->orderBy);

				for (j = i + 1; j < so->nkeys; j++)
				{
					if (so->keys[j]->useCurKey)
					{
						so->keys[j]->curKey = so->keys[i]->scanEntry[0]->curKey;
						so->keys[j]->curKeyCategory =
							so->keys[i]->scanEntry[0]->curKeyCategory;
						countByKey++;
					}
				}
			}
		}
	}

	j = 0;
	for (i = 0; i < so->nkeys; i++)
	{
		if (!so->keys[i]->orderBy)
		{
			continue;
		}

		item->data[j] = keyGetOrdering(&so->rumstate, so->tempCtx, so->keys[i],
									   &so->item.iptr);

		j++;
	}
	rum_tuplesort_putrum(so->sortstate, item);
}


static void
reverseScan(IndexScanDesc scan)
{
	RumScanOpaque so = (RumScanOpaque) scan->opaque;
	int i, j;

	freeScanKeys(so);
	rumNewScanKey(scan);

	for (i = 0; i < so->nkeys; i++)
	{
		RumScanKey key = so->keys[i];

		key->scanDirection = -key->scanDirection;

		for (j = 0; j < key->nentries; j++)
		{
			RumScanEntry entry = key->scanEntry[j];

			entry->scanDirection = -entry->scanDirection;
		}
	}

	startScan(scan);
}


bool
rumgettuple(IndexScanDesc scan, ScanDirection direction)
{
	bool recheck = false;
	bool recheckOrderby = false;
	RumScanOpaque so = (RumScanOpaque) scan->opaque;
	RumSortItem *item;
	bool should_free;

#if PG_VERSION_NUM >= 120000
#define GET_SCAN_TID(scan) ((scan)->xs_heaptid)
#define SET_SCAN_TID(scan, tid) ((scan)->xs_heaptid = (tid))
#else
#define GET_SCAN_TID(scan) ((scan)->xs_ctup.t_self)
#define SET_SCAN_TID(scan, tid) ((scan)->xs_ctup.t_self = (tid))
#endif

	if (so->firstCall)
	{
		/*
		 * Set up the scan keys, and check for unsatisfiable query.
		 */
		if (RumIsNewKey(scan))
		{
			rumNewScanKey(scan);
		}

		if (!ScanDirectionIsNoMovement(direction))
		{
			so->orderScanDirection = direction;
		}

		so->firstCall = false;
		ItemPointerSetInvalid(&GET_SCAN_TID(scan));

		if (RumIsVoidRes(scan))
		{
			return false;
		}

		/* If parallel is enabled, we let one thread start and
		 * determine the scanType - if it's a supported scan, then
		 * other workers can participate - otherwise, the other
		 * workers bail.
		 */
		if (scan->parallel_scan != NULL)
		{
			bool runStartScan = false;
			bool isScanParallelValid = rum_parallel_scan_start(scan, &runStartScan);
			if (runStartScan)
			{
				startScan(scan);
				so->isParallelEnabled = rum_parallel_scan_start_notify(scan);
			}
			else if (!isScanParallelValid)
			{
				return false;
			}
			else
			{
				/* Run startScan as well on the workers - the rest is done below
				 * with parallel cooperation.
				 */
				so->isParallelEnabled = true;
				startScan(scan);
			}
		}
		else
		{
			startScan(scan);
		}

		if (so->scanType == RumOrderedScan)
		{
			so->useSimpleScan = true;
		}
		else if (so->norderbys == 0 &&
				 so->scanType != RumFullScan && !so->rumstate.useAlternativeOrder)
		{
			/* We don't search here. */
			so->useSimpleScan = true;
		}
		else if (so->naturalOrder == NoMovementScanDirection)
		{
			so->sortstate = rum_tuplesort_begin_rum(work_mem, so->norderbys,
													false, so->scanType == RumFullScan);


			while (scanGetItem(scan, &so->item, &so->item, &recheck, &recheckOrderby))
			{
				insertScanItem(so, recheck);
			}
			rum_tuplesort_performsort(so->sortstate);
		}
	}

	if (so->useSimpleScan)
	{
		if (scan->kill_prior_tuple && RumEnableSupportDeadIndexItems)
		{
			/* Remember it for later. (We'll deal with all such
			 * tuples at once right before leaving the index page.)
			 */
			if (so->killedItems == NULL)
			{
				so->killedItems = (ItemPointerData *) palloc(MaxTIDsPerRumPage *
															 sizeof(ItemPointerData));
			}

			if (so->numKilled < MaxTIDsPerRumPage)
			{
				so->killedItems[so->numKilled++] = so->item.iptr;
			}
		}

		if (scanGetItem(scan, &so->item, &so->item, &recheck, &recheckOrderby))
		{
			SET_SCAN_TID(scan, so->item.iptr);
			scan->xs_recheck = recheck;
			scan->xs_recheckorderby = recheckOrderby;

			if (scan->xs_want_itup && so->projectIndexTupleData)
			{
				scan->xs_itup = so->projectIndexTupleData->iscan_tuple;
			}

			return true;
		}

		return false;
	}

	if (so->naturalOrder != NoMovementScanDirection)
	{
		if (scanGetItem(scan, &so->item, &so->item, &recheck, &recheckOrderby))
		{
			SET_SCAN_TID(scan, so->item.iptr);
			scan->xs_recheck = recheck;
			scan->xs_recheckorderby = recheckOrderby;

			return true;
		}
		else if (so->secondPass == false)
		{
			reverseScan(scan);
			so->secondPass = true;
			return rumgettuple(scan, direction);
		}

		return false;
	}

	item = rum_tuplesort_getrum(so->sortstate, true, &should_free);
	while (item)
	{
		uint32 i,
			   j = 0;

		if (rumCompareItemPointers(&GET_SCAN_TID(scan), &item->iptr) == 0)
		{
			if (should_free)
			{
				pfree(item);
			}
			item = rum_tuplesort_getrum(so->sortstate, true, &should_free);
			continue;
		}

		SET_SCAN_TID(scan, item->iptr);
		scan->xs_recheck = item->recheck;
		scan->xs_recheckorderby = false;

		for (i = 0; i < so->nkeys; i++)
		{
			if (!so->keys[i]->orderBy)
			{
				continue;
			}
			scan->xs_orderbyvals[j] = Float8GetDatum(item->data[j]);
			scan->xs_orderbynulls[j] = false;

			j++;
		}

		if (should_free)
		{
			pfree(item);
		}
		return true;
	}

	return false;
}
