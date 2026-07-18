/*-------------------------------------------------------------------------
 *
 * rum_repair.c
 *	  utilities routines for the repairing a rum index.
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 2015-2022, Postgres Professional
 * Portions Copyright (c) 1996-2016, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * These functions are supposed to be used standalone for repairing rum indexes
 *
 *-------------------------------------------------------------------------
 */


#include "postgres.h"

#include "access/htup_details.h"
#include "miscadmin.h"
#include "storage/indexfsm.h"
#include "storage/lmgr.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/index_selfuncs.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "utils/typcache.h"
#include "utils/jsonb.h"
#include "utils/numeric.h"
#include "funcapi.h"

#include "pg_documentdb_rum.h"


PG_FUNCTION_INFO_V1(documentdb_rum_prune_empty_entries_on_index);
PG_FUNCTION_INFO_V1(documentdb_rum_repair_incomplete_split_on_index);
PG_FUNCTION_INFO_V1(documentdb_rum_repair_revive_all_pages_and_tuples);


static void rumRepairLostPathOnIndex(Relation index, bool trackDataPages, bool
									 dryrunMode);
static void MarkIncompleteSplitOnPage(RumState *rumState,
									  Buffer targetBuffer,
									  BlockNumber targetRightBlockNo);
static void CheckTreeAtLevel(RumState *rumState, BlockNumber blockNumber, int level,
							 bool trackDataPages, bool dryrunMode);
static void RumReviveAllPagesAndTuplesOnIndex(Relation rel, bool dryrunMode);


/*
 * Given an index specified by the indexRelId, crawls all the leaf
 * entries of the index and prunes any empty entries on the index.
 * If the prune_empty_pages is also set, then it'll mark the pages
 * as pruned for subsequent deletion/reuse.
 * This is to be used as a one-off when regular vacuum is insufficient
 * or there's an existing index that needs this change.
 * Note that it does not do the bulk deletion of pruning dead rows.
 * That is still delegated to vacuum.
 */
PGDLLEXPORT Datum
documentdb_rum_prune_empty_entries_on_index(PG_FUNCTION_ARGS)
{
	Relation indrel;
	Oid indexRelId = PG_GETARG_OID(0);

	indrel = index_open(indexRelId, RowExclusiveLock);
	if (indrel->rd_index->indisready)
	{
		RumStatsData statsData;

		/* This call at least validates the meta page state */
		rumGetStats(indrel, &statsData);
		rumVacuumPruneEmptyEntries(indrel);
	}

	index_close(indrel, RowExclusiveLock);
	PG_RETURN_VOID();
}


/* Given an index, walks the leaf pages and checks if it needs to
 * Revive any pages based on the LP_DEAD hints being set and revives
 * all the pages and flushes to wal.
 */
PGDLLEXPORT Datum
documentdb_rum_repair_revive_all_pages_and_tuples(PG_FUNCTION_ARGS)
{
	Relation indrel;
	Oid indexRelId = PG_GETARG_OID(0);
	bool dryrunMode = PG_GETARG_BOOL(1);

	indrel = index_open(indexRelId, RowExclusiveLock);
	if (indrel->rd_index->indisready)
	{
		RumStatsData statsData;

		/* This call at least validates the meta page state */
		rumGetStats(indrel, &statsData);

		elog(INFO, "Reviving all pages in index with dryRunMode %d",
			 dryrunMode);
		RumReviveAllPagesAndTuplesOnIndex(indrel, dryrunMode);
	}

	index_close(indrel, RowExclusiveLock);
	PG_RETURN_VOID();
}


/*
 * Given an index specified by the indexRelId argument,
 * walks the index at each level, and tracks incomplete splits.
 * If a split is encountered and dryRunMode is set to false, then
 * marks the left page of the split as INCOMPLETE_SPLIT. This ensures
 * that subsequent inserts will repair the tree and leave it in a
 * consistent state.
 */
PGDLLEXPORT Datum
documentdb_rum_repair_incomplete_split_on_index(PG_FUNCTION_ARGS)
{
	Relation indrel;
	Oid indexRelId = PG_GETARG_OID(0);
	bool trackDataPages = PG_GETARG_BOOL(1);
	bool dryrunMode = PG_GETARG_BOOL(2);

	indrel = index_open(indexRelId, RowExclusiveLock);
	if (indrel->rd_index->indisready)
	{
		RumStatsData statsData;

		/* This call at least validates the meta page state */
		rumGetStats(indrel, &statsData);

		elog(INFO, "Repairing index with dryRunMode %d, and trackDataPages %d",
			 dryrunMode, trackDataPages);
		rumRepairLostPathOnIndex(indrel, trackDataPages, dryrunMode);
	}

	index_close(indrel, RowExclusiveLock);
	PG_RETURN_VOID();
}


static void
CheckLeafEntryPageForSplits(RumState *rumState, Buffer buffer, bool dryrunMode)
{
	Page page = BufferGetPage(buffer);
	OffsetNumber off;

	/* Now walk all the entries at this level */
	for (off = FirstOffsetNumber; off <= PageGetMaxOffsetNumber(page); off++)
	{
		IndexTuple pageTuple = (IndexTuple) PageGetItem(page, PageGetItemId(page, off));
		if (RumIsPostingTree(pageTuple))
		{
			int level = 0;
			BlockNumber postingTreeRoot = RumGetDownlink(pageTuple);
			bool trackDataPages = true;
			CheckTreeAtLevel(rumState, postingTreeRoot, level, trackDataPages,
							 dryrunMode);
		}
	}
}


inline static OffsetNumber
GetMaxOffsetNumberForPage(Page page)
{
	return RumPageIsData(page) ? RumDataPageMaxOff(page) : PageGetMaxOffsetNumber(
		page);
}


inline static BlockNumber
GetChildBlockNumberForPage(Page page, OffsetNumber off)
{
	if (RumPageIsData(page))
	{
		RumPostingItem *pitem = (RumPostingItem *) RumDataPageGetItem(page, off);
		return PostingItemGetBlockNumber(pitem);
	}
	else
	{
		IndexTuple pageTuple = (IndexTuple) PageGetItem(page, PageGetItemId(page, off));
		return RumGetDownlink(pageTuple);
	}
}


static void
CheckTreeAtLevel(RumState *rumState, BlockNumber blockNumber, int level,
				 bool trackDataPages, bool dryrunMode)
{
	Buffer buffer, childBuffer = InvalidBuffer;
	Page page;
	BlockNumber childRightBlock = InvalidBlockNumber;
	BlockNumber leftMostChild = InvalidBlockNumber;
	bool isNextLevelIntermediate = false;
	bool childBufferHasIncompleteSplit = false;
	OffsetNumber off;
	elog(INFO, "Starting check at level %d", level);

	CHECK_FOR_INTERRUPTS();
	buffer = ReadBufferExtended(rumState->index, MAIN_FORKNUM, blockNumber,
								RBM_NORMAL, NULL);
	LockBuffer(buffer, RUM_SHARE);

	page = BufferGetPage(buffer);
	if (RumPageIsLeaf(page))
	{
		/* We reached a leaf level - we're done */
		if (trackDataPages && !RumPageIsData(page))
		{
			CheckLeafEntryPageForSplits(rumState, buffer, dryrunMode);
		}

		UnlockReleaseBuffer(buffer);
		return;
	}

	/* Now walk all the entries at this level */
	for (off = FirstOffsetNumber; off <= GetMaxOffsetNumberForPage(page); off++)
	{
		BlockNumber childBlock = GetChildBlockNumberForPage(page, off);
		bool isRightMost = off == GetMaxOffsetNumberForPage(page);
		BlockNumber nextSibling;
		bool shouldFixPage = false;
		if (!isRightMost)
		{
			nextSibling = GetChildBlockNumberForPage(page, off + 1);
		}
		else if (RumPageGetOpaque(page)->rightlink != InvalidBlockNumber)
		{
			/* This page goes off to the next intermediate page - we need to ensure that the link
			 * is correct across pages.
			 */
			Page rightPage;
			OffsetNumber rightMaxOffset;
			Buffer rightBuffer = ReadBufferExtended(rumState->index, MAIN_FORKNUM,
													RumPageGetOpaque(page)->rightlink,
													RBM_NORMAL, NULL);
			LockBuffer(rightBuffer, RUM_SHARE);
			rightPage = BufferGetPage(rightBuffer);
			rightMaxOffset = GetMaxOffsetNumberForPage(rightPage);
			if (rightMaxOffset > InvalidOffsetNumber)
			{
				nextSibling = GetChildBlockNumberForPage(rightPage, FirstOffsetNumber);
			}
			else
			{
				/* TODO: Is this the right thing to do (technically this shouldn't be possible) */
				nextSibling = InvalidBlockNumber;
			}

			UnlockReleaseBuffer(rightBuffer);
		}
		else
		{
			nextSibling = InvalidBlockNumber;
		}

		if (leftMostChild == InvalidBlockNumber)
		{
			leftMostChild = childBlock;
		}

		/* Check that the right link of this page is the next entry of the tree */
		childBuffer = ReadBufferExtended(rumState->index, MAIN_FORKNUM, childBlock,
										 RBM_NORMAL, NULL);
		LockBuffer(childBuffer, RUM_SHARE);
		childRightBlock = RumPageGetOpaque(BufferGetPage(childBuffer))->rightlink;
		childBufferHasIncompleteSplit = RumPageIsIncompleteSplit(BufferGetPage(
																	 childBuffer));
		isNextLevelIntermediate = isNextLevelIntermediate || !RumPageIsLeaf(BufferGetPage(
																				childBuffer));

		/* Don't yet release pin since we may need to relock it */
		LockBuffer(childBuffer, RUM_UNLOCK);

		if (childRightBlock != nextSibling)
		{
			if (childBufferHasIncompleteSplit)
			{
				elog(INFO, "Rum tree is in an incomplete split state. "
						   "parentPage %u has child %u with rightLink %u, but parent right link is %u",
					 blockNumber, childBlock, childRightBlock, nextSibling);
				shouldFixPage = false;
			}
			else
			{
				elog(INFO, "Rum tree is in an inconsistent state. "
						   "parentPage %u has child %u with rightLink %u, but parent right link is %u",
					 blockNumber, childBlock, childRightBlock, nextSibling);

				shouldFixPage = RumTrackIncompleteSplit && !dryrunMode;
			}
		}

		if (shouldFixPage)
		{
			/* Mark incomplete will release the buffer */
			LockBuffer(childBuffer, RUM_EXCLUSIVE);
			MarkIncompleteSplitOnPage(rumState, childBuffer, nextSibling);
		}
		else
		{
			ReleaseBuffer(childBuffer);
		}
	}

	UnlockReleaseBuffer(buffer);

	if (leftMostChild != InvalidBlockNumber && (isNextLevelIntermediate ||
												trackDataPages))
	{
		CheckTreeAtLevel(rumState, leftMostChild, level + 1, trackDataPages, dryrunMode);
	}
}


static void
MarkIncompleteSplitOnPage(RumState *rumState, Buffer targetBuffer,
						  BlockNumber targetRightBlockNo)
{
	GenericXLogState *state;
	Page page;
	int32_t numBuffersSet = 0;

	while (targetBuffer != InvalidBuffer)
	{
		BlockNumber nextBlockNo;
		state = GenericXLogStart(rumState->index);
		page = GenericXLogRegisterBuffer(state, targetBuffer, 0);
		nextBlockNo = RumPageGetOpaque(page)->rightlink;

		if (nextBlockNo != InvalidBlockNumber)
		{
			numBuffersSet++;
			RumPageGetOpaque(page)->flags |= RUM_INCOMPLETE_SPLIT;
		}

		GenericXLogFinish(state);

		/* Now that the XLog file is written do work to move on */
		if (nextBlockNo != targetRightBlockNo)
		{
			/* If we're the right most entry, subsequent pages may are
			 * also not tracked in the parent. Walk them and ensure that
			 * they get set as incomplete split.
			 */
			Buffer nextBuffer =
				ReadBufferExtended(rumState->index, MAIN_FORKNUM, nextBlockNo, RBM_NORMAL,
								   NULL);
			LockBuffer(nextBuffer, RUM_EXCLUSIVE);
			UnlockReleaseBuffer(targetBuffer);
			targetBuffer = nextBuffer;
		}
		else
		{
			UnlockReleaseBuffer(targetBuffer);
			targetBuffer = InvalidBuffer;
		}
	}

	elog(INFO, "Set %d buffers as incomplete split", numBuffersSet);
}


static void
rumRepairLostPathOnIndex(Relation index, bool trackDataPages, bool dryrunMode)
{
	RumState rumState;
	int level = 0;
	initRumState(&rumState, index);
	CheckTreeAtLevel(&rumState, RUM_ROOT_BLKNO, level, trackDataPages, dryrunMode);
}


static void
RumRevivePage(Relation index, BlockNumber blockNumber, bool dryrunMode)
{
	Buffer buffer;
	Page page;
	GenericXLogState *state;

	OffsetNumber i, maxoff;
	bool hasChanges = false;
	Page tmppage;

	/* Check for interrupts before getting any buffer locks */
	CHECK_FOR_INTERRUPTS();
	buffer = ReadBufferExtended(index, MAIN_FORKNUM, blockNumber,
								RBM_NORMAL, NULL);
	LockBuffer(buffer, RUM_SHARE);

	page = BufferGetPage(buffer);

	if (PageIsNew(page))
	{
		UnlockReleaseBuffer(buffer);
		return;
	}

	if (!RumPageIsLeaf(page))
	{
		UnlockReleaseBuffer(buffer);
		return;
	}

	if (RumPageIsData(page))
	{
		if (RumDataPageEntryIsDead(page))
		{
			elog(dryrunMode ? NOTICE : DEBUG1,
				 "modifying block %u with updates for revive", blockNumber);
			if (!dryrunMode)
			{
				LockBuffer(buffer, RUM_UNLOCK);
				LockBuffer(buffer, RUM_EXCLUSIVE);
				state = GenericXLogStart(index);
				page = GenericXLogRegisterBuffer(state, buffer, 0);
				RumDataPageEntryRevive(page);
				GenericXLogFinish(state);
			}
		}

		UnlockReleaseBuffer(buffer);
		return;
	}

	/* Leaf entry page - need to walk tuples */
	/* First pass - walk with share lock to ensure we need to do something */
	maxoff = PageGetMaxOffsetNumber(page);
	for (i = FirstOffsetNumber; i <= maxoff; i++)
	{
		ItemId itemId = PageGetItemId(page, i);
		if (RumIndexEntryIsDead(itemId))
		{
			hasChanges = true;
			break;
		}
	}

	if (!hasChanges)
	{
		UnlockReleaseBuffer(buffer);
		return;
	}

	/* Relock it with exclusive lock and walk it */
	LockBuffer(buffer, RUM_UNLOCK);
	LockBuffer(buffer, RUM_EXCLUSIVE);
	page = BufferGetPage(buffer);
	tmppage = page;

	maxoff = PageGetMaxOffsetNumber(page);
	for (i = FirstOffsetNumber; i <= maxoff; i++)
	{
		ItemId itemId = PageGetItemId(tmppage, i);
		if (RumIndexEntryIsDead(itemId))
		{
			if (dryrunMode)
			{
				elog(NOTICE, "Would revive entry at offset %d on block %u", i,
					 blockNumber);
			}
			else
			{
				if (tmppage == page)
				{
					tmppage = PageGetTempPageCopy(page);
					itemId = PageGetItemId(tmppage, i);
				}

				RumIndexEntryRevive(itemId);
			}
		}
	}

	if (tmppage != page)
	{
		elog(dryrunMode ? NOTICE : DEBUG1, "modifying block %u with updates for revive",
			 blockNumber);
		if (!dryrunMode)
		{
			state = GenericXLogStart(index);
			page = GenericXLogRegisterBuffer(state, buffer, 0);
			PageRestoreTempPage(tmppage, page);
			GenericXLogFinish(state);
		}
	}

	UnlockReleaseBuffer(buffer);
}


static void
RumReviveAllPagesAndTuplesOnIndex(Relation rel, bool dryrunMode)
{
	BlockNumber scanblkno = RUM_ROOT_BLKNO;
	for (;;)
	{
		BlockNumber num_pages = 0;

		/* Get the current relation length */
		LockRelationForExtension(rel, ExclusiveLock);
		num_pages = RelationGetNumberOfBlocks(rel);
		UnlockRelationForExtension(rel, ExclusiveLock);

		/* Quit if we've scanned the whole relation */
		if (scanblkno >= num_pages)
		{
			break;
		}

		/* Iterate over pages, then loop back to recheck length */
		for (; scanblkno < num_pages; scanblkno++)
		{
			RumRevivePage(rel, scanblkno, dryrunMode);
		}
	}
}
