/*-------------------------------------------------------------------------
 *
 * rum.h
 *	  Exported definitions for RUM index.
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 2015-2022, Postgres Professional
 * Portions Copyright (c) 2006-2022, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */

#ifndef __RUM_H__
#define __RUM_H__

#include "access/amapi.h"
#include "access/generic_xlog.h"
#include "access/itup.h"
#include "access/sdir.h"
#include "lib/rbtree.h"
#include "storage/bufmgr.h"
#include "utils/datum.h"
#include "utils/memutils.h"

#include "rumsort.h"

/* PG16 defined visibility for PGDLLEXPORT properly, for PG15, we need to set it */
#if PG_VERSION_NUM < 160000
#undef PGDLLEXPORT
#ifdef HAVE_VISIBILITY_ATTRIBUTE
#define PGDLLEXPORT __attribute__((visibility("default")))
#else
#define PGDLLEXPORT
#endif
#endif

/* RUM distance strategies */
#define RUM_DISTANCE 20
#define RUM_LEFT_DISTANCE 21
#define RUM_RIGHT_DISTANCE 22

typedef uint16 RumVacuumCycleId;

/*
 * Page opaque data in a inverted index page.
 *
 * Note: RUM does not include a page ID word as do the other index types.
 * This is OK because the opaque data is only 8 bytes and so can be reliably
 * distinguished by size.  Revisit this if the size ever increases.
 * Further note: as of 9.2, SP-GiST also uses 8-byte special space.  This is
 * still OK, as long as RUM isn't using all of the high-order bits in its
 * flags word, because that way the flags word cannot match the page ID used
 * by SP-GiST.
 */
typedef struct RumPageOpaqueData
{
	BlockNumber leftlink;       /* prev page if any */
	BlockNumber rightlink;      /* next page if any */
	union
	{
		OffsetNumber dataPageMaxoff;        /* number entries on RUM_DATA page: number of
		                                     * heap ItemPointers on RUM_DATA|RUM_LEAF page
		                                     * or number of PostingItems on RUM_DATA &
		                                     * ~RUM_LEAF page. */

		OffsetNumber entryPageUnused;
	};

	OffsetNumber dataPageFreespace;
	uint16 flags;               /* see bit definitions below */
	RumVacuumCycleId cycleId; /* The vacuum cycleId */
}   RumPageOpaqueData;

typedef RumPageOpaqueData *RumPageOpaque;

#define RUM_DATA (1 << 0)
#define RUM_LEAF (1 << 1)
#define RUM_DELETED (1 << 2)
#define RUM_META (1 << 3)

/* The page has only dead tuples (the equivalent)
 * of LP_DEAD for posting tree pages.
 */

/* DEPRECATED (REUSABLE): #define RUM_LIST (1 << 4) */
#define RUM_PAGE_IS_DEAD_ROWS (1 << 4)

/* DEPRECATED (REUSABLE): #define RUM_LIST_FULLROW (1 << 5) */
#define RUM_HALF_DEAD (1 << 6)
#define RUM_INCOMPLETE_SPLIT (1 << 7)   /* page was split, but parent not updated */

/* Page numbers of fixed-location pages */
#define RUM_METAPAGE_BLKNO (0)
#define RUM_ROOT_BLKNO (1)

/*
 * GinStatsData represents stats data for planner use
 * TODO: Keep in sync with PostgreSQL
 */
typedef struct RumStatsData
{
	BlockNumber nPendingPages;
	BlockNumber nTotalPages;
	BlockNumber nEntryPages;
	BlockNumber nDataPages;
	int64 nEntries;
	int32 ginVersion;
} RumStatsData;

typedef struct RumMetaPageData
{
	/*
	 * RUM version number
	 */
	uint32 rumVersion;

	/*
	 * Pointers to head and tail of pending list (unused).
	 * XXX unused - pending list is removed.
	 */
	BlockNumber head;
	BlockNumber tail;

	/*
	 * Free space in bytes in the pending list's tail page.
	 */
	uint32 tailFreeSize;

	/*
	 * We store both number of pages and number of heap tuples that are in the
	 * pending list.
	 */
	BlockNumber nPendingPages;
	int64 nPendingHeapTuples;

	/*
	 * Statistics for planner use (accurate as of last VACUUM)
	 */
	BlockNumber nTotalPages;
	BlockNumber nEntryPages;
	BlockNumber nDataPages;
	int64 nEntries;
}   RumMetaPageData;

#define RUM_CURRENT_VERSION (0xC0DE0002)

#define RumPageGetMeta(p) \
	((RumMetaPageData *) PageGetContents(p))

/*
 * Macros for accessing a RUM index page's opaque data
 */
#define RumPageGetOpaque(page) ((RumPageOpaque) PageGetSpecialPointer(page))

#define RumPageRightLink(page) (RumPageGetOpaque(page)->rightlink)
#define RumPageLeftLink(page) (RumPageGetOpaque(page)->leftlink)

#define RumPageIsLeaf(page) ((RumPageGetOpaque(page)->flags & RUM_LEAF) != 0)
#define RumPageSetLeaf(page) (RumPageGetOpaque(page)->flags |= RUM_LEAF)
#define RumPageSetNonLeaf(page) (RumPageGetOpaque(page)->flags &= ~RUM_LEAF)
#define RumPageIsData(page) ((RumPageGetOpaque(page)->flags & RUM_DATA) != 0)
#define RumPageSetData(page) (RumPageGetOpaque(page)->flags |= RUM_DATA)

#define RumPageIsDeleted(page) ((RumPageGetOpaque(page)->flags & RUM_DELETED) != 0)
#define RumPageIsNotDeleted(page) ((RumPageGetOpaque(page)->flags & RUM_DELETED) == 0)
#define RumPageSetDeleted(page) (RumPageGetOpaque(page)->flags |= RUM_DELETED)
#define RumPageSetNonDeleted(page) (RumPageGetOpaque(page)->flags &= ~RUM_DELETED)
#define RumPageForceSetDeleted(page) (RumPageGetOpaque(page)->flags = RUM_DELETED)

#define RumPageIsHalfDead(page) ((RumPageGetOpaque(page)->flags & RUM_HALF_DEAD) != 0)
#define RumPageSetHalfDead(page) (RumPageGetOpaque(page)->flags |= RUM_HALF_DEAD)
#define RumPageSetNonHalfDead(page) (RumPageGetOpaque(page)->flags &= ~RUM_HALF_DEAD)

#define RumPageIsIncompleteSplit(page) ((RumPageGetOpaque(page)->flags & \
										 RUM_INCOMPLETE_SPLIT) != 0)

#define RumPageGetCycleId(page) (RumPageGetOpaque(page)->cycleId)

/*
 * Set the XMIN based of the half-dead page based on maxoff and freespace (these are only
 * used in dataPage). When the XID horizon goes past this, we will mark the page as deleted.
 * This is similar to what's done in GIN.
 */
#define RumPageGetDeleteXid(page) (((PageHeader) (page))->pd_prune_xid)
#define RumPageSetDeleteXid(page, xid) (((PageHeader) (page))->pd_prune_xid = xid)

#define RumPageRightMost(page) (RumPageGetOpaque(page)->rightlink == InvalidBlockNumber)
#define RumPageLeftMost(page) (RumPageGetOpaque(page)->leftlink == InvalidBlockNumber)

/* Dealing with LP_DEAD on entry tree */
#define RumIndexEntryIsDead(itemId) (ItemIdIsDead(itemId))
#define RumIndexEntryMarkDead(itemId) (ItemIdMarkDead(itemId))
#define RumIndexEntryRevive(itemId) \
	( \
		(itemId)->lp_flags = LP_NORMAL \
	)


/* Dealing with LP_DEAD on Posting tree page */
#define RumDataPageEntryIsDead(page) ((RumPageGetOpaque(page)->flags & \
									   RUM_PAGE_IS_DEAD_ROWS) != 0)
#define RumDataPageEntryMarkDead(page) (RumPageGetOpaque(page)->flags |= \
											RUM_PAGE_IS_DEAD_ROWS)
#define RumDataPageEntryRevive(page) (RumPageGetOpaque(page)->flags &= \
										  ~RUM_PAGE_IS_DEAD_ROWS)

/* Upper bound for number of TIDs per page. beyond this if the Postinglist
 * compresses, we won't store them.
 * various code already assumes BLCKSZ * RumItem for the in memory cached
 * set of TIDs per page so reuse that here.
 */
#define MaxTIDsPerRumPage BLCKSZ

/*
 * We use our own ItemPointerGet(BlockNumber|GetOffsetNumber)
 * to avoid Asserts, since sometimes the ip_posid isn't "valid"
 */
#define RumItemPointerGetBlockNumber(pointer) \
	BlockIdGetBlockNumber(&(pointer)->ip_blkid)

#define RumItemPointerGetOffsetNumber(pointer) \
	((pointer)->ip_posid)

/*
 * Special-case item pointer values needed by the RUM search logic.
 *	MIN: sorts less than any valid item pointer
 *	MAX: sorts greater than any valid item pointer
 *	LOSSY PAGE: indicates a whole heap page, sorts after normal item
 *				pointers for that page
 * Note that these are all distinguishable from an "invalid" item pointer
 * (which is InvalidBlockNumber/0) as well as from all normal item
 * pointers (which have item numbers in the range 1..MaxHeapTuplesPerPage).
 */
#ifndef ItemPointerSetMin
#define ItemPointerSetMin(p) \
	ItemPointerSet((p), (BlockNumber) 0, (OffsetNumber) 0)
#define ItemPointerIsMin(p) \
	(RumItemPointerGetOffsetNumber(p) == (OffsetNumber) 0 && \
	 RumItemPointerGetBlockNumber(p) == (BlockNumber) 0)
#define ItemPointerSetMax(p) \
	ItemPointerSet((p), InvalidBlockNumber, (OffsetNumber) 0xfffe)
#define ItemPointerIsMax(p) \
	(RumItemPointerGetOffsetNumber(p) == (OffsetNumber) 0xfffe && \
	 RumItemPointerGetBlockNumber(p) == InvalidBlockNumber)
#define ItemPointerSetLossyPage(p, b) \
	ItemPointerSet((p), (b), (OffsetNumber) 0xffff)
#define ItemPointerIsLossyPage(p) \
	(RumItemPointerGetOffsetNumber(p) == (OffsetNumber) 0xffff && \
	 RumItemPointerGetBlockNumber(p) != InvalidBlockNumber)
#endif

typedef struct RumItem
{
	ItemPointerData iptr;
	bool addInfoIsNull;
	Datum addInfo;
}   RumItem;

#define RumItemSetMin(item) \
	do { \
		ItemPointerSetMin(&((item)->iptr)); \
		(item)->addInfoIsNull = true; \
		(item)->addInfo = (Datum) 0; \
	} while (0)


#define RumItemSetInvalid(item) \
	do { \
		ItemPointerSetInvalid(&((item)->iptr)); \
		(item)->addInfoIsNull = true; \
		(item)->addInfo = (Datum) 0; \
	} while (0)


/*
 * Posting item in a non-leaf posting-tree page
 */
typedef struct
{
	/* We use BlockIdData not BlockNumber to avoid padding space wastage */
	BlockIdData child_blkno;
	RumItem item;
} RumPostingItem;

#define PostingItemGetBlockNumber(pointer) \
	BlockIdGetBlockNumber(&(pointer)->child_blkno)

#define PostingItemSetBlockNumber(pointer, blockNumber) \
	BlockIdSet(&((pointer)->child_blkno), (blockNumber))

/*
 * Category codes to distinguish placeholder nulls from ordinary NULL keys.
 * Note that the datatype size and the first two code values are chosen to be
 * compatible with the usual usage of bool isNull flags.
 *
 * RUM_CAT_EMPTY_QUERY is never stored in the index; and notice that it is
 * chosen to sort before not after regular key values.
 */
typedef signed char RumNullCategory;

#define RUM_CAT_NORM_KEY 0              /* normal, non-null key value */
#define RUM_CAT_NULL_KEY 1              /* null key value */
#define RUM_CAT_EMPTY_ITEM 2            /* placeholder for zero-key item */
#define RUM_CAT_NULL_ITEM 3             /* placeholder for null item */
#define RUM_CAT_EMPTY_QUERY (-1)        /* placeholder for full-scan query */

/* (Custom documentdb): This is net new from base RUM for ordering */
#define RUM_CAT_ORDER_ITEM 4

/*
 * searchMode settings for extractQueryFn.
 */
#define GIN_SEARCH_MODE_DEFAULT 0
#define GIN_SEARCH_MODE_INCLUDE_EMPTY 1
#define GIN_SEARCH_MODE_ALL 2
#define GIN_SEARCH_MODE_EVERYTHING 3        /* for internal use only */

/*
 * Access macros for null category byte in entry tuples
 */
#define RumCategoryOffset(itup, rumstate) \
	(IndexInfoFindDataOffset((itup)->t_info) + \
	 ((rumstate)->oneCol ? 0 : sizeof(int16)))

#define RumGetNullCategory(itup) \
	(*((RumNullCategory *) ((char *) (itup) + IndexTupleSize(itup) - \
							sizeof(RumNullCategory))))
#define RumSetNullCategory(itup, c) \
	(*((RumNullCategory *) ((char *) (itup) + IndexTupleSize(itup) - \
							sizeof(RumNullCategory))) = (c))

/*
 * Access macros for leaf-page entry tuples (see discussion in README)
 */
#define RumGetNPosting(itup) RumItemPointerGetOffsetNumber(&(itup)->t_tid)
#define RumSetNPosting(itup, n) ItemPointerSetOffsetNumber(&(itup)->t_tid, n)
#define RUM_TREE_POSTING ((OffsetNumber) 0xffff)
#define RumIsPostingTree(itup) (RumGetNPosting(itup) == RUM_TREE_POSTING)
#define RumSetPostingTree(itup, blkno) \
	(RumSetNPosting((itup), RUM_TREE_POSTING), \
	 ItemPointerSetBlockNumber(&(itup)->t_tid, blkno))
#define RumGetPostingTree(itup) RumItemPointerGetBlockNumber(&(itup)->t_tid)

#define RumGetPostingOffset(itup) RumItemPointerGetBlockNumber(&(itup)->t_tid)
#define RumSetPostingOffset(itup, n) ItemPointerSetBlockNumber(&(itup)->t_tid, n)
#define RumGetPosting(itup) ((Pointer) ((char *) (itup) + RumGetPostingOffset(itup)))

/*
 * Maximum size of an item on entry tree page. Make sure that we fit at least
 * three items on each page. (On regular B-tree indexes, we must fit at least
 * three items: two data items and the "high key". In RUM entry tree, we don't
 * currently store the high key explicitly, we just use the rightmost item on
 * the page, so it would actually be enough to fit two items.)
 */
#define RumMaxItemSize \
	Min(INDEX_SIZE_MASK, \
		MAXALIGN_DOWN(((BLCKSZ - \
						MAXALIGN(SizeOfPageHeaderData + 3 * sizeof(ItemIdData)) - \
						MAXALIGN(sizeof(RumPageOpaqueData))) / 3)))


/*
 * Access macros for non-leaf entry tuples
 */
#define RumGetDownlink(itup) RumItemPointerGetBlockNumber(&(itup)->t_tid)
#define RumSetDownlink(itup, blkno) ItemPointerSet(&(itup)->t_tid, blkno, \
												   InvalidOffsetNumber)


/*
 * Data (posting tree) pages
 */

/*
 * FIXME -- Currently RumItem is placed as a pages right bound and RumPostingItem
 * is placed as a non-leaf pages item. Both RumItem and RumPostingItem stores
 * AddInfo as a raw Datum, which is bogus. It is fine for pass-by-value
 * attributes, but it isn't for pass-by-reference, which may have variable
 * length of data. This AddInfo is used only by order_by_attach indexes, so it
 * isn't allowed to create index using ordering over pass-by-reference AddInfo,
 * see initRumState(). This can be solved by having non-fixed length right bound
 * and non-fixed non-leaf posting tree item.
 */
#define RumDataPageGetRightBound(page) ((RumItem *) PageGetContents(page))
#define RumDataPageGetData(page) \
	(PageGetContents(page) + MAXALIGN(sizeof(RumItem)))
#define RumDataPageGetItem(page, i) \
	(RumDataPageGetData(page) + ((i) - 1) * sizeof(RumPostingItem))

#define RumDataPageGetFreeSpace(page) \
	(BLCKSZ - MAXALIGN(SizeOfPageHeaderData) \
	 - MAXALIGN(sizeof(RumItem)) /* right bound */ \
	 - RumPageGetOpaque(page)->dataPageMaxoff * sizeof(RumPostingItem) \
	 - MAXALIGN(sizeof(RumPageOpaqueData)))

#define RumDataPageMaxOff(page) (RumPageGetOpaque(page)->dataPageMaxoff)
#define RumDataPageReadFreeSpaceValue(page) (RumPageGetOpaque(page)->dataPageFreespace)

#define RumMaxLeafDataItems \
	((BLCKSZ - MAXALIGN(SizeOfPageHeaderData) - \
	  MAXALIGN(sizeof(RumItem)) /* right bound */ - \
	  MAXALIGN(sizeof(RumPageOpaqueData))) \
	 / sizeof(ItemPointerData))

typedef struct
{
	ItemPointerData iptr;
	OffsetNumber offsetNumer;
	uint16 pageOffset;
	Datum addInfo;       /* optional */
}   RumDataLeafItemIndex;

#define RumDataLeafIndexCount 32

#define RumDataPageSize \
	(BLCKSZ - MAXALIGN(SizeOfPageHeaderData) \
	 - MAXALIGN(sizeof(RumItem)) /* right bound */ \
	 - MAXALIGN(sizeof(RumPageOpaqueData)) \
	 - MAXALIGN(sizeof(RumDataLeafItemIndex) * RumDataLeafIndexCount))

#define RumDataPageFreeSpacePre(page, ptr) \
	(RumDataPageSize \
	 - ((ptr) - RumDataPageGetData(page)))

#define RumPageGetIndexes(page) \
	((RumDataLeafItemIndex *) (RumDataPageGetData(page) + RumDataPageSize))

/*
 * Storage type for RUM's reloptions
 */
typedef struct RumOptions
{
	int32 vl_len_;              /* varlena header (do not touch directly!) */
	bool useAlternativeOrder;
	int attachColumn;
	int addToColumn;
}   RumOptions;

#define ALT_ADD_INFO_NULL_FLAG (0x8000)

/* Macros for buffer lock/unlock operations */
#define RUM_UNLOCK BUFFER_LOCK_UNLOCK
#define RUM_SHARE BUFFER_LOCK_SHARE
#define RUM_EXCLUSIVE BUFFER_LOCK_EXCLUSIVE

#define MAX_STRATEGIES (8)
typedef struct RumConfig
{
	Oid addInfoTypeOid;

	struct
	{
		StrategyNumber strategy;
		ScanDirection direction;
	}       strategyInfo[MAX_STRATEGIES];
}   RumConfig;

/*
 * RumState: working data structure describing the index being worked on
 */
typedef struct RumState
{
	Relation index;
	bool isBuild;
	bool oneCol;                /* true if single-column index */
	bool useAlternativeOrder;
	AttrNumber attrnAttachColumn;
	AttrNumber attrnAddToColumn;

	/*
	 * origTupDesc is the nominal tuple descriptor of the index, ie, the i'th
	 * attribute shows the key type (not the input data type!) of the i'th
	 * index column.  In a single-column index this describes the actual leaf
	 * index tuples.  In a multi-column index, the actual leaf tuples contain
	 * a smallint column number followed by a key datum of the appropriate
	 * type for that column.  We set up tupdesc[i] to describe the actual
	 * rowtype of the index tuples for the i'th column, ie, (int2, keytype).
	 * Note that in any case, leaf tuples contain more data than is known to
	 * the TupleDesc; see access/gin/README for details.
	 */
	TupleDesc origTupdesc;
	TupleDesc tupdesc[INDEX_MAX_KEYS];
	RumConfig rumConfig[INDEX_MAX_KEYS];
	Form_pg_attribute addAttrs[INDEX_MAX_KEYS];

	/*
	 * Per-index-column opclass support functions
	 */
	FmgrInfo compareFn[INDEX_MAX_KEYS];
	FmgrInfo extractValueFn[INDEX_MAX_KEYS];
	FmgrInfo extractQueryFn[INDEX_MAX_KEYS];
	FmgrInfo consistentFn[INDEX_MAX_KEYS];
	FmgrInfo comparePartialFn[INDEX_MAX_KEYS];          /* optional method */
	FmgrInfo configFn[INDEX_MAX_KEYS];          /* optional method */
	FmgrInfo preConsistentFn[INDEX_MAX_KEYS];           /* optional method */
	FmgrInfo orderingFn[INDEX_MAX_KEYS];        /* optional method */
	FmgrInfo outerOrderingFn[INDEX_MAX_KEYS];           /* optional method */
	FmgrInfo joinAddInfoFn[INDEX_MAX_KEYS];         /* optional method */
	/* canPartialMatch[i] is true if comparePartialFn[i] is valid */
	bool canPartialMatch[INDEX_MAX_KEYS];

	/* canPreConsistent[i] is true if preConsistentFn[i] is valid */
	bool canPreConsistent[INDEX_MAX_KEYS];

	/* canOrdering[i] is true if orderingFn[i] is valid */
	bool canOrdering[INDEX_MAX_KEYS];
	bool canOuterOrdering[INDEX_MAX_KEYS];
	bool canJoinAddInfo[INDEX_MAX_KEYS];

	FmgrInfo canPreConsistentFn[INDEX_MAX_KEYS];
	bool hasCanPreConsistentFn[INDEX_MAX_KEYS];

	/* Collations to pass to the support functions */
	Oid supportCollation[INDEX_MAX_KEYS];
}   RumState;

/* Accessor for the i'th attribute of tupdesc. */
#if PG_VERSION_NUM > 100000
#define RumTupleDescAttr(tupdesc, i) (TupleDescAttr(tupdesc, i))
#else
#define RumTupleDescAttr(tupdesc, i) ((tupdesc)->attrs [(i)])
#endif

/* rumutil.c */
extern PGDLLIMPORT bytea * documentdb_rumoptions(Datum reloptions, bool validate);
extern bool rumproperty(Oid index_oid, int attno,
						IndexAMProperty prop, const char *propname,
						bool *res, bool *isnull);
extern void initRumState(RumState *state, Relation index);
extern Buffer RumNewBuffer(Relation index);
extern void RumInitBuffer(GenericXLogState *state, Buffer buffer, uint32 flags,
						  bool isBuild);
extern void RumInitPage(Page page, uint32 f, Size pageSize);
extern void RumInitMetabuffer(GenericXLogState *state, Buffer metaBuffer,
							  bool isBuild);
extern int rumCompareEntries(RumState *rumstate, OffsetNumber attnum,
							 Datum a, RumNullCategory categorya,
							 Datum b, RumNullCategory categoryb);
extern int rumCompareAttEntries(RumState *rumstate,
								OffsetNumber attnuma, Datum a, RumNullCategory categorya,
								OffsetNumber attnumb, Datum b, RumNullCategory categoryb);
extern Datum * rumExtractEntries(RumState *rumstate, OffsetNumber attnum,
								 Datum value, bool isNull,
								 int32 *nentries, RumNullCategory **categories,
								 Datum **addInfo, bool **addInfoIsNull);

extern OffsetNumber rumtuple_get_attrnum(RumState *rumstate, IndexTuple tuple);
extern Datum rumtuple_get_key(RumState *rumstate, IndexTuple tuple,
							  RumNullCategory *category);

extern void rumGetStats(Relation index, RumStatsData *stats);
extern void rumUpdateStats(Relation index, const RumStatsData *stats,
						   bool isBuild);

/* ruminsert.c */
extern IndexBuildResult * rumbuild(Relation heap, Relation index,
								   struct IndexInfo *indexInfo);
extern void rumbuildempty(Relation index);
extern bool ruminsert(Relation index, Datum *values, bool *isnull,
					  ItemPointer ht_ctid, Relation heapRel,
					  IndexUniqueCheck checkUnique
#if PG_VERSION_NUM >= 140000
					  , bool indexUnchanged
#endif
#if PG_VERSION_NUM >= 100000
					  , struct IndexInfo *indexInfo
#endif
					  );
extern void rumEntryInsert(RumState *rumstate,
						   OffsetNumber attnum, Datum key, RumNullCategory category,
						   RumItem *items, uint32 nitem, RumStatsData *buildStats);

/* rumbtree.c */

typedef struct RumBtreeStack
{
	BlockNumber blkno;
	Buffer buffer;
	OffsetNumber off;

	/* predictNumber contains predicted number of pages on current level */
	uint32 predictNumber;
	struct RumBtreeStack *parent;
}   RumBtreeStack;

typedef struct RumBtreeData *RumBtree;

typedef struct RumBtreeData
{
	/* search methods */
	BlockNumber (*findChildPage)(RumBtree, RumBtreeStack *);
	bool (*isMoveRight)(RumBtree, Page);
	bool (*findItem)(RumBtree, RumBtreeStack *);

	/* insert methods */
	OffsetNumber (*findChildPtr)(RumBtree, Page, BlockNumber, OffsetNumber);
	BlockNumber (*getLeftMostPage)(RumBtree, Page);
	bool (*isEnoughSpace)(RumBtree, Buffer, OffsetNumber);
	void (*placeToPage)(RumBtree, Page, OffsetNumber);
	Page (*splitPage)(RumBtree, Buffer, Buffer, Page, Page, OffsetNumber);
	void (*fillRoot)(RumBtree, Buffer, Buffer, Buffer, Page, Page, Page);
	void (*fillBtreeForIncompleteSplit)(RumBtree, RumBtreeStack *, Buffer);

	bool isData;
	bool searchMode;

	Relation index;
	RumState *rumstate;
	bool fullScan;
	ScanDirection scanDirection;

	BlockNumber rightblkno;

	AttrNumber entryAttnum;

	/* Entry options */
	Datum entryKey;
	RumNullCategory entryCategory;
	IndexTuple entry;
	bool isDelete;

	/* Data (posting tree) options */
	RumItem *items;

	uint32 nitem;
	uint32 curitem;

	RumPostingItem pitem;
}   RumBtreeData;

extern RumBtreeStack * rumPrepareFindLeafPage(RumBtree btree, BlockNumber blkno);
extern RumBtreeStack * rumFindLeafPage(RumBtree btree, RumBtreeStack *stack);
extern RumBtreeStack * rumReFindLeafPage(RumBtree btree, RumBtreeStack *stack);
extern Buffer rumStep(Buffer buffer, Relation index, int lockmode,
					  ScanDirection scanDirection);
extern void freeRumBtreeStack(RumBtreeStack *stack);
extern void rumInsertValue(Relation index, RumBtree btree, RumBtreeStack *stack,
						   RumStatsData *buildStats);
extern void rumFindParents(RumBtree btree, RumBtreeStack *stack, BlockNumber rootBlkno);

/* rumentrypage.c */
extern void rumPrepareEntryScan(RumBtree btree, OffsetNumber attnum,
								Datum key, RumNullCategory category,
								RumState *rumstate);
extern void rumEntryFillRoot(RumBtree btree, Buffer root, Buffer lbuf, Buffer rbuf,
							 Page page, Page lpage, Page rpage);
extern IndexTuple rumPageGetLinkItup(RumBtree btree, Buffer buf, Page page);
extern void rumReadTuple(RumState *rumstate, OffsetNumber attnum,
						 IndexTuple itup, RumItem *items, bool copyAddInfo);
extern void rumReadTuplePointers(RumState *rumstate, OffsetNumber attnum,
								 IndexTuple itup, ItemPointerData *ipd);
bool entryIsMoveRight(RumBtree btree, Page page);
bool entryLocateLeafEntryBounds(RumBtree btree, Page page,
								OffsetNumber low, OffsetNumber high,
								OffsetNumber *targetOffset);
IndexTuple rumEntryGetRightMostTuple(Page page);

/* rumdatapage.c */
extern void updateItemIndexes(Page page, OffsetNumber attnum, RumState *rumstate);
extern int rumCompareItemPointers(const ItemPointerData *a, const ItemPointerData *b);
extern int compareRumItem(RumState *state, const AttrNumber attno,
						  const RumItem *a, const RumItem *b);
extern void convertIndexToKey(RumDataLeafItemIndex *src, RumItem *dst);
extern Pointer rumPlaceToDataPageLeaf(Pointer ptr, OffsetNumber attnum,
									  RumItem *item, ItemPointer prev,
									  RumState *rumstate);
extern Size rumCheckPlaceToDataPageLeaf(OffsetNumber attnum,
										RumItem *item, ItemPointer prev,
										RumState *rumstate, Size size);
extern uint32 rumMergeRumItems(RumState *rumstate, AttrNumber attno,
							   RumItem *dst,
							   RumItem *a, uint32 na,
							   RumItem *b, uint32 nb);
extern void RumDataPageAddItem(Page page, void *data, OffsetNumber offset);
extern void RumPageDeletePostingItem(Page page, OffsetNumber offset);

typedef struct
{
	RumBtreeData btree;
	RumBtreeStack *stack;
}   RumPostingTreeScan;

extern RumPostingTreeScan * rumPrepareScanPostingTree(Relation index,
													  BlockNumber rootBlkno, bool
													  searchMode,
													  ScanDirection scanDirection,
													  OffsetNumber attnum,
													  RumState *rumstate);
extern void rumInsertItemPointers(RumState *rumstate,
								  OffsetNumber attnum,
								  RumPostingTreeScan *gdi,
								  RumItem *items, uint32 nitem,
								  RumStatsData *buildStats);
extern Buffer rumScanBeginPostingTree(RumPostingTreeScan *gdi, RumItem *item);
extern void rumDataFillRoot(RumBtree btree, Buffer root, Buffer lbuf, Buffer rbuf,
							Page page, Page lpage, Page rpage);
extern void rumPrepareDataScan(RumBtree btree, Relation index, OffsetNumber attnum,
							   RumState *rumstate);

/* rumscan.c */

typedef struct RumScanItem
{
	RumItem item;
	Datum keyValue;
	RumNullCategory keyCategory;
}   RumScanItem;

/*
 * RumScanKeyData describes a single RUM index qualifier expression.
 *
 * From each qual expression, we extract one or more specific index search
 * conditions, which are represented by RumScanEntryData.  It's quite
 * possible for identical search conditions to be requested by more than
 * one qual expression, in which case we merge such conditions to have just
 * one unique RumScanEntry --- this is particularly important for efficiency
 * when dealing with full-index-scan entries.  So there can be multiple
 * RumScanKeyData.scanEntry pointers to the same RumScanEntryData.
 *
 * In each RumScanKeyData, nentries is the true number of entries, while
 * nuserentries is the number that extractQueryFn returned (which is what
 * we report to consistentFn).  The "user" entries must come first.
 */
typedef struct RumScanKeyData *RumScanKey;

typedef struct RumScanEntryData *RumScanEntry;

typedef struct RumScanKeyData
{
	/* Real number of entries in scanEntry[] (always > 0) */
	uint32 nentries;

	/* Number of entries that extractQueryFn and consistentFn know about */
	uint32 nuserentries;

	/* array of RumScanEntry pointers, one per extracted search condition */
	RumScanEntry *scanEntry;

	/* array of check flags, reported to consistentFn */
	bool *entryRes;

	/* array of additional information, used in consistentFn and orderingFn */
	Datum *addInfo;
	bool *addInfoIsNull;

	/* additional information, used in outerOrderingFn */
	bool useAddToColumn;
	Datum outerAddInfo;
	bool outerAddInfoIsNull;

	/* Key information, used in orderingFn */
	Datum curKey;
	RumNullCategory curKeyCategory;
	bool useCurKey;

	/* other data needed for calling consistentFn */
	Datum query;

	/* NB: these three arrays have only nuserentries elements! */
	Datum *queryValues;
	RumNullCategory *queryCategories;
	Pointer *extra_data;
	StrategyNumber strategy;
	int32 searchMode;
	OffsetNumber attnum;
	OffsetNumber attnumOrig;

	/*
	 * Match status data.  curItem is the TID most recently tested (could be a
	 * lossy-page pointer).  curItemMatches is TRUE if it passes the
	 * consistentFn test; if so, recheckCurItem is the recheck flag.
	 * isFinished means that all the input entry streams are finished, so this
	 * key cannot succeed for any later TIDs.
	 */
	RumItem curItem;
	bool curItemMatches;
	bool recheckCurItem;
	bool isFinished;
	bool orderBy;
	bool willSort;        /* just a copy of RumScanOpaqueData.willSort */
	ScanDirection scanDirection;

	/* array of keys, used to scan using additional information as keys */
	RumScanKey *addInfoKeys;
	uint32 addInfoNKeys;
}   RumScanKeyData;

typedef struct RumScanEntryData
{
	/* query key and other information from extractQueryFn */
	Datum queryKey;
	RumNullCategory queryCategory;
	bool isPartialMatch;
	Pointer extra_data;
	StrategyNumber strategy;
	int32 searchMode;
	OffsetNumber attnum;
	OffsetNumber attnumOrig;

	/* Current page in posting tree */
	Buffer buffer;

	/* current ItemPointer to heap */
	RumItem curItem;

	/* Used for ordering using distance */
	Datum curKey;
	RumNullCategory curKeyCategory;
	bool useCurKey;

	/*
	 * For a partial-match or full-scan query, we accumulate all TIDs and
	 * and additional information here
	 */
	RumTuplesortstate *matchSortstate;
	RumScanItem collectRumItem;

	/* for full-scan query with order-by */
	RumBtreeStack *stack;
	bool scanWithAddInfo;

	/* used for Posting list and one page in Posting tree */
	RumItem *list;
	int16 nlist;
	int16 offset;
	XLogRecPtr cachedLsn;

	ScanDirection scanDirection;
	bool isFinished;
	bool reduceResult;
	uint32 predictNumberResult;

	/* used to scan posting tree */
	RumPostingTreeScan *gdi;

	/* used in fast scan in addition to preConsistentFn */
	bool preValue;

	/* Find by AddInfo */
	bool useMarkAddInfo;
	RumItem markAddInfo;

	/* Compare partial addition new for documentdb */
	bool isMatchMinimalTuple;

	/* Optional used in skipscans */
	Datum queryKeyOverride;
}   RumScanEntryData;

typedef struct
{
	ItemPointerData iptr;
	float8 distance;
	bool recheck;
}   RumOrderingItem;

typedef enum
{
	RumFastScan,
	RumRegularScan,
	RumFullScan,
	RumOrderedScan, /* documentdb: This is new */
}   RumScanType;

/* Struct that holds information for projecting an index tuple. */
typedef struct RumProjectIndexTupleData
{
	/* The tuple descriptor to form the index tuple. */
	TupleDesc indexTupleDesc;

	/* The datum returned by the extensibility point representing the index tuple to project */
	Datum indexTupleDatum;

	/* The final index tuple built from the descriptor and the datum. */
	IndexTuple iscan_tuple;
} RumProjectIndexTupleData;

typedef struct RumOrderByScanData
{
	RumBtreeStack *orderStack;
	Page orderByEntryPageCopy;
	bool isPageValid;
	RumScanEntry orderByEntry;
	IndexTuple boundEntryTuple;
} RumOrderByScanData;

typedef struct RumScanOpaqueData
{
	/* tempCtx is used to hold consistent and ordering functions data */
	MemoryContext tempCtx;

	/* keyCtx is used to hold key and entry data */
	MemoryContext keyCtx;
	RumState rumstate;

	RumScanKey *keys;               /* one per scan qualifier expr */
	uint32 nkeys;

	RumScanEntry *entries;          /* one per index search condition */
	RumScanEntry *sortedEntries;    /* Sorted entries. Used in fast scan */
	int entriesIncrIndex;           /* used in fast scan */
	uint32 totalentries;
	uint32 allocentries;            /* allocated length of entries[] and
	                                 * sortedEntries[] */

	RumTuplesortstate *sortstate;
	int norderbys;              /* Number of columns in ordering.
	                             * Will be assigned to sortstate->nKeys */

	RumItem item;               /* current item used in index scan */
	bool firstCall;

	bool isVoidRes;             /* true if query is unsatisfiable */
	bool willSort;              /* is there any columns in ordering */
	RumScanType scanType;
	bool isParallelEnabled;

	ScanDirection naturalOrder;
	bool secondPass;

	/* on a regular scan, how many loops of scans were done. */
	uint32_t scanLoops;

	/* In an ordered scan, the key pointing to the order by key */
	int32_t orderByKeyIndex;
	bool orderByHasRecheck;

	RumOrderByScanData *orderByScanData;
	ScanDirection orderScanDirection;
	bool recheckCurrentItem;
	bool recheckCurrentItemOrderBy;

	/* documentdb: whether or not to use a simple scanGetNextItem in rumgettuple */
	bool useSimpleScan;

	/* LP_DEAD stuff */
	ItemPointerData *killedItems;
	int numKilled;
	bool ignoreKilledTuples;
	uint32_t killedItemsSkipped;

	/* stateContext to hold state from rumstate (documentdb: This is new ) */
	MemoryContext rumStateCtx;

	/* Index only scan metadata. */
	RumProjectIndexTupleData *projectIndexTupleData;
}   RumScanOpaqueData;

typedef RumScanOpaqueData *RumScanOpaque;

extern IndexScanDesc rumbeginscan(Relation rel, int nkeys, int norderbys);
extern void rumendscan(IndexScanDesc scan);
extern void rumrescan(IndexScanDesc scan, ScanKey scankey, int nscankeys,
					  ScanKey orderbys, int norderbys);
extern Datum rummarkpos(PG_FUNCTION_ARGS);
extern Datum rumrestrpos(PG_FUNCTION_ARGS);
extern void rumNewScanKey(IndexScanDesc scan);
extern void freeScanKeys(RumScanOpaque so);

#if PG_VERSION_NUM >= 180000
extern Size rumestimateparallelscan(Relation rel, int nkeys, int norderbys);
#elif PG_VERSION_NUM >= 170000
extern Size rumestimateparallelscan(int nkeys, int norderbys);
#else
extern Size rumestimateparallelscan(void);
#endif
extern void ruminitparallelscan(void *target);
extern void rumparallelrescan(IndexScanDesc scan);
extern bool rum_parallel_scan_start(IndexScanDesc scan, bool *startScan);
extern bool rum_parallel_scan_start_notify(IndexScanDesc scan);

extern bool rum_parallel_seize(ParallelIndexScanDesc parallelScan,
							   BlockNumber *blockNumber);
extern void rum_parallel_release(ParallelIndexScanDesc parallelScan, BlockNumber
								 nextBlock);

/* rumget.c */
extern int64 rumgetbitmap(IndexScanDesc scan, TIDBitmap *tbm);
extern bool rumgettuple(IndexScanDesc scan, ScanDirection direction);
extern void RumKillEntryItems(RumScanOpaque so, RumOrderByScanData *scanData);

/* rumvacuum.c */
extern IndexBulkDeleteResult * rumbulkdelete(IndexVacuumInfo *info,
											 IndexBulkDeleteResult *stats,
											 IndexBulkDeleteCallback callback,
											 void *callback_state);
extern IndexBulkDeleteResult * rumvacuumcleanup(IndexVacuumInfo *info,
												IndexBulkDeleteResult *stats);


/* rumvacuumutil.c */
extern void InitializeRumVacuumState(void);
extern RumVacuumCycleId rum_start_vacuum_cycle_id(Relation rel);
extern void rum_end_vacuum_cycle_id(Relation rel);
extern RumVacuumCycleId rum_vacuum_get_cycleId(Relation rel);


/* rumvalidate.c */
extern bool rumvalidate(Oid opclassoid);

/* rumvacuum.c */
extern void rumVacuumPruneEmptyEntries(Relation indexRel);

/* rumbulk.c */
#if PG_VERSION_NUM <= 100006 || PG_VERSION_NUM == 110000
typedef RBNode RBTNode;
#endif

/* rumselfuncs.c */
extern PGDLLIMPORT void documentdb_rum_costestimate(struct PlannerInfo *root, struct
													IndexPath *path, double
													loop_count,
													Cost *indexStartupCost,
													Cost *indexTotalCost,
													Selectivity *indexSelectivity,
													double *indexCorrelation,
													double *indexPages);

typedef struct RumEntryAccumulator
{
	RBTNode rbnode;
	Datum key;
	RumNullCategory category;
	OffsetNumber attnum;
	bool shouldSort;
	RumItem *list;
	uint32 maxcount;            /* allocated size of list[] */
	uint32 count;               /* current count of list[] entries */
}   RumEntryAccumulator;

typedef struct
{
	RumState *rumstate;
	long allocatedMemory;
	RumEntryAccumulator *entryallocator;
	uint32 eas_used;
	RBTree *tree;
#if PG_VERSION_NUM >= 100000
	RBTreeIterator tree_walk;
#endif
	RumItem *sortSpace;
	uint32 sortSpaceN;
} BuildAccumulator;

extern void rumInitBA(BuildAccumulator *accum);
extern void rumInsertBAEntries(BuildAccumulator *accum,
							   ItemPointer heapptr, OffsetNumber attnum,
							   Datum *entries, Datum *addInfo, bool *addInfoIsNull,
							   RumNullCategory *categories, int32 nentries);
extern void rumBeginBAScan(BuildAccumulator *accum);
extern RumItem * rumGetBAEntry(BuildAccumulator *accum,
							   OffsetNumber *attnum, Datum *key,
							   RumNullCategory *category,
							   uint32 *n);

/*
 * amproc indexes for inverted indexes.
 */
#define GIN_COMPARE_PROC 1
#define GIN_EXTRACTVALUE_PROC 2
#define GIN_EXTRACTQUERY_PROC 3
#define GIN_CONSISTENT_PROC 4
#define GIN_COMPARE_PARTIAL_PROC 5

/* rum_ts_utils.c */
#define RUM_CONFIG_PROC 6
#define RUM_PRE_CONSISTENT_PROC 7
#define RUM_ORDERING_PROC 8
#define RUM_OUTER_ORDERING_PROC 9
#define RUM_ADDINFO_JOIN 10
#define RUM_INDEX_CONFIG_PROC 11
#define RUM_CAN_PRE_CONSISTENT_PROC 12

/* NProcs changes for documentdb from 10 to 12 */
#define RUMNProcs 12

#define RUM_DEFAULT_TRACK_INCOMPLETE_SPLIT true
#define RUM_DEFAULT_FIX_INCOMPLETE_SPLIT true

/* GUC parameters */
extern PGDLLIMPORT int RumFuzzySearchLimit;
extern PGDLLIMPORT int RumDataPageIntermediateSplitSize;
extern PGDLLIMPORT bool RumThrowErrorOnInvalidDataPage;
extern PGDLLIMPORT bool RumDisableFastScan;
extern PGDLLIMPORT bool RumEnableParallelIndexBuild;
extern PGDLLIMPORT int RumParallelIndexWorkersOverride;
extern PGDLLIMPORT bool RumSkipRetryOnDeletePage;
extern PGDLLIMPORT bool RumForceOrderedIndexScan;
extern PGDLLIMPORT bool RumPreferOrderedIndexScan;
extern PGDLLIMPORT bool RumEnableSkipIntermediateEntry;
extern PGDLLIMPORT bool RumVacuumEntryItems;
extern PGDLLIMPORT bool RumUseNewItemPtrDecoding;
extern PGDLLIMPORT bool RumPruneEmptyPages;
extern PGDLLIMPORT bool RumTrackIncompleteSplit;
extern PGDLLIMPORT bool RumFixIncompleteSplit;
extern PGDLLIMPORT bool RumInjectPageSplitIncomplete;
extern PGDLLIMPORT bool RumEnableParallelVacuumFlags;
extern PGDLLIMPORT bool RumEnableCustomCostEstimate;
extern PGDLLIMPORT bool RumEnableNewBulkDelete;
extern PGDLLIMPORT bool RumNewBulkDeleteInlineDataPages;
extern PGDLLIMPORT bool RumVacuumSkipPrunePostingTreePages;
extern PGDLLIMPORT bool RumEnableSupportDeadIndexItems;
extern PGDLLIMPORT bool RumSkipResetOnDeadEntryPage;

/*
 * Functions for reading ItemPointers with additional information. Used in
 * various .c files and have to be inline for being fast.
 */

#define SEVENTHBIT (0x40)
#define SIXMASK (0x3F)

#define InitBlockNumberIncr(x, iptr) uint64 x = iptr->ip_blkid.bi_lo + \
												(iptr->ip_blkid.bi_hi << 16)
#define InitBlockNumberIncrZero(x) uint64 x = 0

/*
 * Decode varbyte-encoded integer at *ptr. *ptr is incremented to next integer.
 */
static uint64
decode_varbyte_blocknumber(unsigned char **ptr)
{
	uint64 val;
	unsigned char *p = *ptr;
	uint64 c;

	/* 1st byte */
	c = *(p++);
	val = c & 0x7F;
	if (c & 0x80)
	{
		/* 2nd byte */
		c = *(p++);
		val |= (c & 0x7F) << 7;
		if (c & 0x80)
		{
			/* 3rd byte */
			c = *(p++);
			val |= (c & 0x7F) << 14;
			if (c & 0x80)
			{
				/* 4th byte */
				c = *(p++);
				val |= (c & 0x7F) << 21;
				if (c & 0x80)
				{
					/* 5th byte */
					c = *(p++);
					val |= (c & 0x7F) << 28;
					if (c & 0x80)
					{
						/* 6th byte */
						c = *(p++);
						val |= (c & 0x7F) << 35;
						if (c & 0x80)
						{
							/* 7th byte, should not have continuation bit */
							c = *(p++);
							val |= c << 42;
							Assert((c & 0x80) == 0);
						}
					}
				}
			}
		}
	}

	*ptr = p;

	return val;
}


/*
 * Read next item pointer from leaf data page. Replaces current item pointer
 * with the next one. Zero item pointer should be passed in order to read the
 * first item pointer. Also reads value of addInfoIsNull flag which is stored
 * with item pointer.
 */
static inline char *
rumDataPageLeafReadItemPointerWithBlockNumberIncr(char *ptr, RumItem *item,
												  uint64 *blockNumberIncrPtr)
{
	uint16 offset = 0;
	int i;
	uint8 v;

	*blockNumberIncrPtr += decode_varbyte_blocknumber((unsigned char **) &ptr);
	Assert(*blockNumberIncrPtr < ((uint64) 1 << 32));

	item->iptr.ip_blkid.bi_lo = *blockNumberIncrPtr & 0xFFFF;
	item->iptr.ip_blkid.bi_hi = (*blockNumberIncrPtr >> 16) & 0xFFFF;

	i = 0;

	while (true)
	{
		v = *ptr;
		ptr++;
		Assert(i < 14 || ((i == 14) && ((v & SIXMASK) < (1 << 2))));

		if (v & HIGHBIT)
		{
			offset |= (v & (~HIGHBIT)) << i;
		}
		else
		{
			offset |= (v & SIXMASK) << i;
			item->addInfoIsNull = (v & SEVENTHBIT) ? true : false;
			break;
		}
		i += 7;
	}

	if (RumThrowErrorOnInvalidDataPage && !OffsetNumberIsValid(offset))
	{
		/* Reuse retry on lost path */
		elog(ERROR, "invalid offset on rumpage");
	}

	Assert(OffsetNumberIsValid(offset));
	item->iptr.ip_posid = offset;

	return ptr;
}


static inline char *
rumDataPageLeafReadItemPointerNew(char *ptr, RumItem *rumItem)
{
	InitBlockNumberIncr(blockNumberIncr, (&rumItem->iptr));
	return rumDataPageLeafReadItemPointerWithBlockNumberIncr(ptr, rumItem,
															 &blockNumberIncr);
}


static inline Pointer
rumDataPageLeafReadWithBlockNumberIncr(Pointer ptr, OffsetNumber attnum, RumItem *item,
									   bool copyAddInfo, RumState *rumstate,
									   uint64 *blockNumberIncrPtr)
{
	Form_pg_attribute attr;

	if (rumstate->useAlternativeOrder)
	{
		memcpy(&item->iptr, ptr, sizeof(ItemPointerData));
		ptr += sizeof(ItemPointerData);

		if (item->iptr.ip_posid & ALT_ADD_INFO_NULL_FLAG)
		{
			item->iptr.ip_posid &= ~ALT_ADD_INFO_NULL_FLAG;
			item->addInfoIsNull = true;
		}
		else
		{
			item->addInfoIsNull = false;
		}
	}
	else
	{
		ptr = rumDataPageLeafReadItemPointerWithBlockNumberIncr(ptr, item,
																blockNumberIncrPtr);
	}

	Assert(item->iptr.ip_posid != InvalidOffsetNumber);

	if (!item->addInfoIsNull)
	{
		attr = rumstate->addAttrs[attnum - 1];

		Assert(attr);

		if (attr->attbyval)
		{
			/* do not use aligment for pass-by-value types */
			union
			{
				int16 i16;
				int32 i32;
			}
			u;

			switch (attr->attlen)
			{
				case sizeof(char):
				{
					item->addInfo = Int8GetDatum(*ptr);
					break;
				}

				case sizeof(int16):
				{
					memcpy(&u.i16, ptr, sizeof(int16));
					item->addInfo = Int16GetDatum(u.i16);
					break;
				}

				case sizeof(int32):
				{
					memcpy(&u.i32, ptr, sizeof(int32));
					item->addInfo = Int32GetDatum(u.i32);
					break;
				}

#if SIZEOF_DATUM == 8
				case sizeof(Datum):
				{
					memcpy(&item->addInfo, ptr, sizeof(Datum));
					break;
				}

#endif
				default:
					elog(ERROR, "unsupported byval length: %d",
						 (int) (attr->attlen));
			}
		}
		else
		{
			Datum addInfo;

			ptr = (Pointer) att_align_pointer(ptr, attr->attalign, attr->attlen,
											  ptr);
			addInfo = fetch_att(ptr, attr->attbyval, attr->attlen);
			item->addInfo = copyAddInfo ?
							datumCopy(addInfo, attr->attbyval, attr->attlen) : addInfo;
		}

		ptr = (Pointer) att_addlength_pointer(ptr, attr->attlen, ptr);
	}
	return ptr;
}


inline static void
rumPopulateDataPage(RumState *rumstate, RumScanEntry entry, OffsetNumber maxoff, Page
					pageInner)
{
	InitBlockNumberIncrZero(blockNumberIncr);
	Pointer ptr = RumDataPageGetData(pageInner);
	for (OffsetNumber i = FirstOffsetNumber; i <= maxoff; i = OffsetNumberNext(i))
	{
		ptr = rumDataPageLeafReadWithBlockNumberIncr(ptr, entry->attnum,
													 &entry->list[i - FirstOffsetNumber],
													 true,
													 rumstate, &blockNumberIncr);
	}

	if (maxoff < 1)
	{
		memset(&entry->list[0], 0, sizeof(RumItem));
	}
}


/*
 * Read next item pointer from leaf data page. Replaces current item pointer
 * with the next one. Zero item pointer should be passed in order to read the
 * first item pointer. Also reads value of addInfoIsNull flag which is stored
 * with item pointer.
 */
static inline char *
rumDataPageLeafReadItemPointer(char *ptr, ItemPointer iptr, bool *addInfoIsNull)
{
	uint32 blockNumberIncr = 0;
	uint16 offset = 0;
	int i;
	uint8 v;

	i = 0;
	do {
		v = *ptr;
		ptr++;
		blockNumberIncr |= (v & (~HIGHBIT)) << i;
		Assert(i < 28 || ((i == 28) && ((v & (~HIGHBIT)) < (1 << 4))));
		i += 7;
	} while (v & HIGHBIT);

	Assert((uint64) iptr->ip_blkid.bi_lo + ((uint64) iptr->ip_blkid.bi_hi << 16) +
		   (uint64) blockNumberIncr < ((uint64) 1 << 32));

	blockNumberIncr += iptr->ip_blkid.bi_lo + (iptr->ip_blkid.bi_hi << 16);

	iptr->ip_blkid.bi_lo = blockNumberIncr & 0xFFFF;
	iptr->ip_blkid.bi_hi = (blockNumberIncr >> 16) & 0xFFFF;

	i = 0;

	while (true)
	{
		v = *ptr;
		ptr++;
		Assert(i < 14 || ((i == 14) && ((v & SIXMASK) < (1 << 2))));

		if (v & HIGHBIT)
		{
			offset |= (v & (~HIGHBIT)) << i;
		}
		else
		{
			offset |= (v & SIXMASK) << i;
			if (addInfoIsNull)
			{
				*addInfoIsNull = (v & SEVENTHBIT) ? true : false;
			}
			break;
		}
		i += 7;
	}

	if (!OffsetNumberIsValid(offset) && RumThrowErrorOnInvalidDataPage)
	{
		/* Reuse retry on lost path */
		elog(ERROR, "invalid offset on rumpage");
	}

	Assert(OffsetNumberIsValid(offset));
	iptr->ip_posid = offset;

	return ptr;
}


/*
 * Reads next item pointer and additional information from leaf data page.
 * Replaces current item pointer with the next one. Zero item pointer should be
 * passed in order to read the first item pointer.
 *
 * It is necessary to pass copyAddInfo=true if additional information is used
 * when the data page is unlocked. If the additional information is used without
 * locking one can get unexpected behaviour.
 */
static inline Pointer
rumDataPageLeafRead(Pointer ptr, OffsetNumber attnum, RumItem *item,
					bool copyAddInfo, RumState *rumstate)
{
	Form_pg_attribute attr;

	if (rumstate->useAlternativeOrder)
	{
		memcpy(&item->iptr, ptr, sizeof(ItemPointerData));
		ptr += sizeof(ItemPointerData);

		if (item->iptr.ip_posid & ALT_ADD_INFO_NULL_FLAG)
		{
			item->iptr.ip_posid &= ~ALT_ADD_INFO_NULL_FLAG;
			item->addInfoIsNull = true;
		}
		else
		{
			item->addInfoIsNull = false;
		}
	}
	else if (RumUseNewItemPtrDecoding)
	{
		ptr = rumDataPageLeafReadItemPointerNew(ptr, item);
	}
	else
	{
		ptr = rumDataPageLeafReadItemPointer(ptr, &item->iptr,
											 &item->addInfoIsNull);
	}

	Assert(item->iptr.ip_posid != InvalidOffsetNumber);

	if (!item->addInfoIsNull)
	{
		attr = rumstate->addAttrs[attnum - 1];

		Assert(attr);

		if (attr->attbyval)
		{
			/* do not use aligment for pass-by-value types */
			union
			{
				int16 i16;
				int32 i32;
			}
			u;

			switch (attr->attlen)
			{
				case sizeof(char):
				{
					item->addInfo = Int8GetDatum(*ptr);
					break;
				}

				case sizeof(int16):
				{
					memcpy(&u.i16, ptr, sizeof(int16));
					item->addInfo = Int16GetDatum(u.i16);
					break;
				}

				case sizeof(int32):
				{
					memcpy(&u.i32, ptr, sizeof(int32));
					item->addInfo = Int32GetDatum(u.i32);
					break;
				}

#if SIZEOF_DATUM == 8
				case sizeof(Datum):
				{
					memcpy(&item->addInfo, ptr, sizeof(Datum));
					break;
				}

#endif
				default:
					elog(ERROR, "unsupported byval length: %d",
						 (int) (attr->attlen));
			}
		}
		else
		{
			Datum addInfo;

			ptr = (Pointer) att_align_pointer(ptr, attr->attalign, attr->attlen,
											  ptr);
			addInfo = fetch_att(ptr, attr->attbyval, attr->attlen);
			item->addInfo = copyAddInfo ?
							datumCopy(addInfo, attr->attbyval, attr->attlen) : addInfo;
		}

		ptr = (Pointer) att_addlength_pointer(ptr, attr->attlen, ptr);
	}
	return ptr;
}


/*
 * Reads next item pointer from leaf data page.
 * Replaces current item pointer with the next one. Zero item pointer should be
 * passed in order to read the first item pointer.
 */
static inline Pointer
rumDataPageLeafReadPointer(Pointer ptr, OffsetNumber attnum, RumItem *item,
						   RumState *rumstate)
{
	Form_pg_attribute attr;

	if (rumstate->useAlternativeOrder)
	{
		memcpy(&item->iptr, ptr, sizeof(ItemPointerData));
		ptr += sizeof(ItemPointerData);

		if (item->iptr.ip_posid & ALT_ADD_INFO_NULL_FLAG)
		{
			item->iptr.ip_posid &= ~ALT_ADD_INFO_NULL_FLAG;
			item->addInfoIsNull = true;
		}
		else
		{
			item->addInfoIsNull = false;
		}
	}
	else
	{
		ptr = rumDataPageLeafReadItemPointer(ptr, &item->iptr,
											 &item->addInfoIsNull);
	}

	Assert(item->iptr.ip_posid != InvalidOffsetNumber);

	if (!item->addInfoIsNull)
	{
		attr = rumstate->addAttrs[attnum - 1];

		Assert(attr);

		if (!attr->attbyval)
		{
			ptr = (Pointer) att_align_pointer(ptr, attr->attalign, attr->attlen,
											  ptr);
		}

		ptr = (Pointer) att_addlength_pointer(ptr, attr->attlen, ptr);
	}
	return ptr;
}


extern Datum FunctionCall10Coll(FmgrInfo *flinfo, Oid collation,
								Datum arg1, Datum arg2,
								Datum arg3, Datum arg4, Datum arg5,
								Datum arg6, Datum arg7, Datum arg8,
								Datum arg9, Datum arg10);

/* PostgreSQL version-agnostic creation of memory context */
#if PG_VERSION_NUM >= 120000
#define RumContextCreate(parent, name) \
	AllocSetContextCreate(parent, name, ALLOCSET_DEFAULT_SIZES)
#elif PG_VERSION_NUM >= 110000
	#define RumContextCreate(parent, name) \
	AllocSetContextCreateExtended(parent, name, \
								  ALLOCSET_DEFAULT_MINSIZE, \
								  ALLOCSET_DEFAULT_INITSIZE, \
								  ALLOCSET_DEFAULT_MAXSIZE)
#else
	#define RumContextCreate(parent, name) \
	AllocSetContextCreate(parent, name, \
						  ALLOCSET_DEFAULT_MINSIZE, \
						  ALLOCSET_DEFAULT_INITSIZE, \
						  ALLOCSET_DEFAULT_MAXSIZE)
#endif

/*
 * Constant definition for progress reporting.  Phase numbers must match
 * rumbuildphasename.
 */

/* PROGRESS_CREATEIDX_SUBPHASE_INITIALIZE is 1 (see progress.h) */
#define PROGRESS_RUM_PHASE_INDEXBUILD_TABLESCAN 2
#define PROGRESS_RUM_PHASE_PERFORMSORT_1 3
#define PROGRESS_RUM_PHASE_MERGE_1 4
#define PROGRESS_RUM_PHASE_PERFORMSORT_2 5
#define PROGRESS_RUM_PHASE_MERGE_2 6
#define PROGRESS_RUM_PHASE_WRITE_WAL 7


#define UNREDACTED_RUM_LOG_CODE MAKE_SQLSTATE('R', 'Z', 'Z', 'Z', 'Z')
typedef int (*rum_format_log_hook)(const char *fmt, ...) pg_attribute_printf (1, 2);
extern PGDLLIMPORT rum_format_log_hook rum_unredacted_log_emit_hook;

#define errmsg_unredacted(...) \
	(rum_unredacted_log_emit_hook ? \
	 (*rum_unredacted_log_emit_hook)(__VA_ARGS__) : \
	 errmsg_internal(__VA_ARGS__))


#define elog_rum_unredacted(...) \
	ereport(LOG, (errcode(UNREDACTED_RUM_LOG_CODE), errhidecontext(true), \
				  errhidestmt(true), errmsg_unredacted( \
					  __VA_ARGS__)))

#endif   /* __RUM_H__ */
