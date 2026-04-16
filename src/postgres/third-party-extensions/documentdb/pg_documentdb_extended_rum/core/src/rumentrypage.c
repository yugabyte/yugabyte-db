/*-------------------------------------------------------------------------
 *
 * rumentrypage.c
 *	  page utilities routines for the postgres inverted index access method.
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 2015-2022, Postgres Professional
 * Portions Copyright (c) 1996-2013, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "pg_documentdb_rum.h"

/*
 * Read item pointers with additional information from leaf data page.
 * Information is stored in the same manner as in leaf data pages.
 */
void
rumReadTuple(RumState *rumstate, OffsetNumber attnum,
			 IndexTuple itup, RumItem *items, bool copyAddInfo)
{
	Pointer ptr = RumGetPosting(itup);
	RumItem item;
	int nipd = RumGetNPosting(itup),
		i;
	RumItemSetMin(&item);

	if (RumUseNewItemPtrDecoding)
	{
		InitBlockNumberIncr(blockNumberIncr, (&item.iptr));
		for (i = 0; i < nipd; i++)
		{
			ptr = rumDataPageLeafReadWithBlockNumberIncr(ptr, attnum, &item, copyAddInfo,
														 rumstate, &blockNumberIncr);
			items[i] = item;
		}
	}
	else
	{
		for (i = 0; i < nipd; i++)
		{
			ptr = rumDataPageLeafRead(ptr, attnum, &item, copyAddInfo, rumstate);
			items[i] = item;
		}
	}
}


/*
 * Read only item pointers from leaf data page.
 * Information is stored in the same manner as in leaf data pages.
 */
void
rumReadTuplePointers(RumState *rumstate, OffsetNumber attnum,
					 IndexTuple itup, ItemPointerData *ipd)
{
	Pointer ptr = RumGetPosting(itup);
	int nipd = RumGetNPosting(itup),
		i;
	RumItem item;

	ItemPointerSetMin(&item.iptr);
	for (i = 0; i < nipd; i++)
	{
		ptr = rumDataPageLeafReadPointer(ptr, attnum, &item, rumstate);
		ipd[i] = item.iptr;
	}
}


/*
 * Form a non-leaf entry tuple by copying the key data from the given tuple,
 * which can be either a leaf or non-leaf entry tuple.
 *
 * Any posting list in the source tuple is not copied.  The specified child
 * block number is inserted into t_tid.
 */
static IndexTuple
RumFormInteriorTuple(RumBtree btree, IndexTuple itup, Page page,
					 BlockNumber childblk)
{
	IndexTuple nitup;
	RumNullCategory category;

	if (RumPageIsLeaf(page) && !RumIsPostingTree(itup))
	{
		/* Tuple contains a posting list, just copy stuff before that */
		uint32 origsize = RumGetPostingOffset(itup);

		origsize = MAXALIGN(origsize);
		nitup = (IndexTuple) palloc(origsize);
		memcpy(nitup, itup, origsize);

		/* ... be sure to fix the size header field ... */
		nitup->t_info &= ~INDEX_SIZE_MASK;
		nitup->t_info |= origsize;
	}
	else
	{
		/* Copy the tuple as-is */
		nitup = (IndexTuple) palloc(IndexTupleSize(itup));
		memcpy(nitup, itup, IndexTupleSize(itup));
	}

	/* Now insert the correct downlink */
	RumSetDownlink(nitup, childblk);

	rumtuple_get_key(btree->rumstate, itup, &category);
	if (category != RUM_CAT_NORM_KEY)
	{
		Assert(IndexTupleHasNulls(itup));
		nitup->t_info |= INDEX_NULL_MASK;
		RumSetNullCategory(nitup, category);
	}

	return nitup;
}


/*
 * Entry tree is a "static", ie tuple never deletes from it,
 * so we don't use right bound, we use rightmost key instead.
 */
IndexTuple
rumEntryGetRightMostTuple(Page page)
{
	OffsetNumber maxoff = PageGetMaxOffsetNumber(page);

	Assert(maxoff != InvalidOffsetNumber);

	return (IndexTuple) PageGetItem(page, PageGetItemId(page, maxoff));
}


bool
entryIsMoveRight(RumBtree btree, Page page)
{
	IndexTuple itup;
	OffsetNumber attnum;
	Datum key;
	RumNullCategory category;

	if (RumPageRightMost(page))
	{
		return false;
	}

	if (RumPageIsHalfDead(page))
	{
		/* If on a half dead page, always move right */
		return true;
	}

	itup = rumEntryGetRightMostTuple(page);
	attnum = rumtuple_get_attrnum(btree->rumstate, itup);
	key = rumtuple_get_key(btree->rumstate, itup, &category);

	if (rumCompareAttEntries(btree->rumstate,
							 btree->entryAttnum, btree->entryKey, btree->entryCategory,
							 attnum, key, category) > 0)
	{
		return true;
	}

	return false;
}


/*
 * Find correct tuple in non-leaf page. It supposed that
 * page correctly chosen and searching value SHOULD be on page
 */
static BlockNumber
entryLocateEntry(RumBtree btree, RumBtreeStack *stack)
{
	OffsetNumber low,
				 high,
				 maxoff;
	IndexTuple itup = NULL;
	int result;
	Page page = BufferGetPage(stack->buffer);

	Assert(!RumPageIsLeaf(page));
	Assert(!RumPageIsData(page));

	if (btree->fullScan)
	{
		stack->off = FirstOffsetNumber;
		stack->predictNumber *= PageGetMaxOffsetNumber(page);
		return btree->getLeftMostPage(btree, page);
	}

	low = FirstOffsetNumber;
	maxoff = high = PageGetMaxOffsetNumber(page);
	Assert(high >= low);

	high++;

	while (high > low)
	{
		OffsetNumber mid = low + ((high - low) / 2);

		if (mid == maxoff && RumPageRightMost(page))
		{
			/* Right infinity */
			result = -1;
		}
		else
		{
			OffsetNumber attnum;
			Datum key;
			RumNullCategory category;

			itup = (IndexTuple) PageGetItem(page, PageGetItemId(page, mid));
			attnum = rumtuple_get_attrnum(btree->rumstate, itup);
			key = rumtuple_get_key(btree->rumstate, itup, &category);
			result = rumCompareAttEntries(btree->rumstate,
										  btree->entryAttnum,
										  btree->entryKey,
										  btree->entryCategory,
										  attnum, key, category);
		}

		if (result == 0)
		{
			stack->off = mid;
			Assert(RumGetDownlink(itup) != RUM_ROOT_BLKNO);
			return RumGetDownlink(itup);
		}
		else if (result > 0)
		{
			low = mid + 1;
		}
		else
		{
			high = mid;
		}
	}

	Assert(high >= FirstOffsetNumber && high <= maxoff);

	stack->off = high;
	itup = (IndexTuple) PageGetItem(page, PageGetItemId(page, high));
	Assert(RumGetDownlink(itup) != RUM_ROOT_BLKNO);
	return RumGetDownlink(itup);
}


/*
 * Searches correct position for value on leaf page.
 * Page should be correctly chosen.
 * Returns true if value found on page.
 */
static bool
entryLocateLeafEntry(RumBtree btree, RumBtreeStack *stack)
{
	Page page = BufferGetPage(stack->buffer);
	OffsetNumber low,
				 high;

	Assert(RumPageIsLeaf(page));
	Assert(!RumPageIsData(page));

	if (btree->fullScan)
	{
		stack->off = FirstOffsetNumber;
		return true;
	}

	low = FirstOffsetNumber;
	high = PageGetMaxOffsetNumber(page);

	return entryLocateLeafEntryBounds(btree, page, low, high, &stack->off);
}


bool
entryLocateLeafEntryBounds(RumBtree btree, Page page,
						   OffsetNumber low, OffsetNumber high,
						   OffsetNumber *targetOffset)
{
	Assert(!RumPageIsData(page));

	if (high < low)
	{
		*targetOffset = low;
		return false;
	}

	high++;

	while (high > low)
	{
		OffsetNumber mid = low + ((high - low) / 2);
		IndexTuple itup;
		OffsetNumber attnum;
		Datum key;
		RumNullCategory category;
		int result;

		itup = (IndexTuple) PageGetItem(page, PageGetItemId(page, mid));
		attnum = rumtuple_get_attrnum(btree->rumstate, itup);
		key = rumtuple_get_key(btree->rumstate, itup, &category);
		result = rumCompareAttEntries(btree->rumstate,
									  btree->entryAttnum,
									  btree->entryKey,
									  btree->entryCategory,
									  attnum, key, category);
		if (result == 0)
		{
			*targetOffset = mid;
			return true;
		}
		else if (result > 0)
		{
			low = mid + 1;
		}
		else
		{
			high = mid;
		}
	}

	*targetOffset = high;
	return false;
}


static OffsetNumber
entryFindChildPtr(RumBtree btree, Page page, BlockNumber blkno, OffsetNumber storedOff)
{
	OffsetNumber i,
				 maxoff = PageGetMaxOffsetNumber(page);
	IndexTuple itup;

	Assert(!RumPageIsLeaf(page));
	Assert(!RumPageIsData(page));

	/* if page isn't changed, we returns storedOff */
	if (storedOff >= FirstOffsetNumber && storedOff <= maxoff)
	{
		itup = (IndexTuple) PageGetItem(page, PageGetItemId(page, storedOff));
		if (RumGetDownlink(itup) == blkno)
		{
			return storedOff;
		}

		/*
		 * we hope, that needed pointer goes to right. It's true if there
		 * wasn't a deletion
		 */
		for (i = storedOff + 1; i <= maxoff; i++)
		{
			itup = (IndexTuple) PageGetItem(page, PageGetItemId(page, i));
			if (RumGetDownlink(itup) == blkno)
			{
				return i;
			}
		}
		maxoff = storedOff - 1;
	}

	/* last chance */
	for (i = FirstOffsetNumber; i <= maxoff; i++)
	{
		itup = (IndexTuple) PageGetItem(page, PageGetItemId(page, i));
		if (RumGetDownlink(itup) == blkno)
		{
			return i;
		}
	}

	return InvalidOffsetNumber;
}


static BlockNumber
entryGetLeftMostPage(RumBtree btree, Page page)
{
	IndexTuple itup;

	Assert(!RumPageIsLeaf(page));
	Assert(!RumPageIsData(page));
	Assert(PageGetMaxOffsetNumber(page) >= FirstOffsetNumber);

	itup = (IndexTuple) PageGetItem(page, PageGetItemId(page, FirstOffsetNumber));
	return RumGetDownlink(itup);
}


static bool
entryIsEnoughSpace(RumBtree btree, Buffer buf, OffsetNumber off)
{
	Size itupsz = 0;
	Page page = BufferGetPage(buf);

	Assert(btree->entry);
	Assert(!RumPageIsData(page));

	if (btree->isDelete)
	{
		IndexTuple itup = (IndexTuple) PageGetItem(page, PageGetItemId(page, off));

		itupsz = MAXALIGN(IndexTupleSize(itup)) + sizeof(ItemIdData);
	}

	if (PageGetFreeSpace(page) + itupsz >= MAXALIGN(IndexTupleSize(btree->entry)) +
		sizeof(ItemIdData))
	{
		return true;
	}

	return false;
}


/*
 * Delete tuple on leaf page if tuples existed and we
 * should update it, update old child blkno to new right page
 * if child split occurred
 */
static BlockNumber
entryPreparePage(RumBtree btree, Page page, OffsetNumber off)
{
	BlockNumber ret = InvalidBlockNumber;

	Assert(btree->entry);
	Assert(!RumPageIsData(page));

	if (btree->isDelete)
	{
		Assert(RumPageIsLeaf(page));
		PageIndexTupleDelete(page, off);
	}

	if (!RumPageIsLeaf(page) && btree->rightblkno != InvalidBlockNumber)
	{
		IndexTuple itup = (IndexTuple) PageGetItem(page, PageGetItemId(page, off));

		RumSetDownlink(itup, btree->rightblkno);
		ret = btree->rightblkno;
	}

	btree->rightblkno = InvalidBlockNumber;

	return ret;
}


/*
 * Place tuple on page and fills WAL record
 */
static void
entryPlaceToPage(RumBtree btree, Page page, OffsetNumber off)
{
	OffsetNumber placed;

	entryPreparePage(btree, page, off);

	placed = PageAddItem(page, (Item) btree->entry, IndexTupleSize(btree->entry), off,
						 false, false);
	if (placed != off)
	{
		elog(ERROR, "failed to add item to index page in \"%s\"",
			 RelationGetRelationName(btree->index));
	}

	Assert(ItemIdIsNormal(PageGetItemId(page, off)));
	btree->entry = NULL;
}


/*
 * Place tuple and split page, original buffer(lbuf) leaves untouched,
 * returns shadow page of lbuf filled new data.
 * Tuples are distributed between pages by equal size on its, not
 * an equal number!
 */
static Page
entrySplitPage(RumBtree btree, Buffer lbuf, Buffer rbuf,
			   Page lPage, Page rPage, OffsetNumber off)
{
	OffsetNumber i,
				 maxoff,
				 separator = InvalidOffsetNumber;
	Size totalsize = 0;
	Size lsize = 0,
		 size;
	char *ptr;
	IndexTuple itup,
			   leftrightmost = NULL;
	Page page;
	Page newlPage = PageGetTempPageCopy(lPage);
	Size pageSize = PageGetPageSize(newlPage);

	/*
	 * Must have tupstore MAXALIGNed to use PG macros to access data in
	 * it. Should not rely on compiler alignment preferences to avoid
	 * tupstore overflow related to PG in-memory page items alignment
	 * inside rumDataPageLeafRead() or elsewhere.
	 */
	static char tupstoreStorage[2 * BLCKSZ + MAXIMUM_ALIGNOF];
	char *tupstore = (char *) MAXALIGN(tupstoreStorage);

	entryPreparePage(btree, newlPage, off);

	maxoff = PageGetMaxOffsetNumber(newlPage);
	ptr = tupstore;

	for (i = FirstOffsetNumber; i <= maxoff; i++)
	{
		if (i == off)
		{
			size = MAXALIGN(IndexTupleSize(btree->entry));
			memcpy(ptr, btree->entry, size);
			ptr += size;
			totalsize += size + sizeof(ItemIdData);
		}

		itup = (IndexTuple) PageGetItem(newlPage, PageGetItemId(newlPage, i));
		size = MAXALIGN(IndexTupleSize(itup));
		memcpy(ptr, itup, size);
		ptr += size;
		totalsize += size + sizeof(ItemIdData);
	}

	if (off == maxoff + 1)
	{
		size = MAXALIGN(IndexTupleSize(btree->entry));
		memcpy(ptr, btree->entry, size);
		totalsize += size + sizeof(ItemIdData);
	}

	RumInitPage(rPage, RumPageGetOpaque(newlPage)->flags, pageSize);
	RumInitPage(newlPage, RumPageGetOpaque(rPage)->flags, pageSize);

	if (RumEnableNewBulkDelete)
	{
		RumPageGetCycleId(newlPage) = rum_vacuum_get_cycleId(btree->index);
		RumPageGetCycleId(rPage) = RumPageGetCycleId(newlPage);
	}

	ptr = tupstore;
	maxoff++;
	lsize = 0;

	page = newlPage;
	for (i = FirstOffsetNumber; i <= maxoff; i++)
	{
		PG_USED_FOR_ASSERTS_ONLY OffsetNumber writtenoffset = InvalidOffsetNumber;
		itup = (IndexTuple) ptr;

		if (lsize > totalsize / 2)
		{
			if (separator == InvalidOffsetNumber)
			{
				separator = i - 1;
			}
			page = rPage;
		}
		else
		{
			leftrightmost = itup;
			lsize += MAXALIGN(IndexTupleSize(itup)) + sizeof(ItemIdData);
		}

		if ((writtenoffset = PageAddItem(page, (Item) itup, IndexTupleSize(itup),
										 InvalidOffsetNumber,
										 false, false)) == InvalidOffsetNumber)
		{
			elog(ERROR, "failed to add item to index page in \"%s\"",
				 RelationGetRelationName(btree->index));
		}

		Assert(ItemIdIsNormal(PageGetItemId(page, writtenoffset)));
		ptr += MAXALIGN(IndexTupleSize(itup));
	}

	btree->entry = RumFormInteriorTuple(btree, leftrightmost, newlPage,
										BufferGetBlockNumber(lbuf));

	btree->rightblkno = BufferGetBlockNumber(rbuf);

	return newlPage;
}


/*
 * return newly allocated rightmost tuple
 */
IndexTuple
rumPageGetLinkItup(RumBtree btree, Buffer buf, Page page)
{
	IndexTuple itup,
			   nitup;

	itup = rumEntryGetRightMostTuple(page);
	nitup = RumFormInteriorTuple(btree, itup, page, BufferGetBlockNumber(buf));

	return nitup;
}


/*
 * Fills new root by rightest values from child.
 * Also called from rumxlog, should not use btree
 */
void
rumEntryFillRoot(RumBtree btree, Buffer root, Buffer lbuf, Buffer rbuf,
				 Page page, Page lpage, Page rpage)
{
	IndexTuple itup;

	itup = rumPageGetLinkItup(btree, lbuf, lpage);
	if (PageAddItem(page, (Item) itup, IndexTupleSize(itup), InvalidOffsetNumber, false,
					false) == InvalidOffsetNumber)
	{
		elog(ERROR, "failed to add item to index root page");
	}
	pfree(itup);

	itup = rumPageGetLinkItup(btree, rbuf, rpage);
	if (PageAddItem(page, (Item) itup, IndexTupleSize(itup), InvalidOffsetNumber, false,
					false) == InvalidOffsetNumber)
	{
		elog(ERROR, "failed to add item to index root page");
	}
	pfree(itup);
}


static void
rumEntryFillBtreeForIncompleteSplit(RumBtree btree, RumBtreeStack *stack, Buffer buffer)
{
	IndexTuple itup;
	Page page = BufferGetPage(buffer);

	/* Fill rightblkno to ensure we're tracking the split page */
	btree->rightblkno = RumPageGetOpaque(page)->rightlink;

	itup = rumEntryGetRightMostTuple(page);
	btree->entry = RumFormInteriorTuple(btree, itup, page, BufferGetBlockNumber(buffer));
}


/*
 * Set up RumBtree for entry page access
 *
 * Note: during WAL recovery, there may be no valid data in rumstate
 * other than a faked-up Relation pointer; the key datum is bogus too.
 */
void
rumPrepareEntryScan(RumBtree btree, OffsetNumber attnum,
					Datum key, RumNullCategory category,
					RumState *rumstate)
{
	memset(btree, 0, sizeof(RumBtreeData));

	btree->index = rumstate->index;
	btree->rumstate = rumstate;

	btree->findChildPage = entryLocateEntry;
	btree->isMoveRight = entryIsMoveRight;
	btree->findItem = entryLocateLeafEntry;
	btree->findChildPtr = entryFindChildPtr;
	btree->getLeftMostPage = entryGetLeftMostPage;
	btree->isEnoughSpace = entryIsEnoughSpace;
	btree->placeToPage = entryPlaceToPage;
	btree->splitPage = entrySplitPage;
	btree->fillRoot = rumEntryFillRoot;
	btree->fillBtreeForIncompleteSplit = rumEntryFillBtreeForIncompleteSplit;

	btree->isData = false;
	btree->searchMode = false;
	btree->fullScan = false;

	btree->entryAttnum = attnum;
	btree->entryKey = key;
	btree->entryCategory = category;
	btree->isDelete = false;
}
