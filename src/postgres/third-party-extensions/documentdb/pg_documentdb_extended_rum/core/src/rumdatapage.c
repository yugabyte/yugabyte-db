/*-------------------------------------------------------------------------
 *
 * rumdatapage.c
 *	  page utilities routines for the postgres inverted index access method.
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 2015-2022, Postgres Professional
 * Portions Copyright (c) 1996-2016, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "pg_documentdb_rum.h"

/* Test GUC to test data page splits */
extern int RumDataPageIntermediateSplitSize;

static BlockNumber dataGetLeftMostPage(RumBtree btree, Page page);
static BlockNumber dataGetRightMostPage(RumBtree btree, Page page);

/* Does datatype allow packing into the 1-byte-header varlena format? */
#define TYPE_IS_PACKABLE(typlen, typstorage) \
	((typlen) == -1 && (typstorage) != 'p')

/*
 * Increment data_length by the space needed by the datum, including any
 * preceding alignment padding.
 */
static Size
rumComputeDatumSize(Size data_length, Datum val, bool typbyval, char typalign,
					int16 typlen, char typstorage)
{
	if (TYPE_IS_PACKABLE(typlen, typstorage) &&
		VARATT_CAN_MAKE_SHORT(DatumGetPointer(val)))
	{
		/*
		 * we're anticipating converting to a short varlena header, so adjust
		 * length and don't count any alignment
		 */
		data_length += VARATT_CONVERTED_SHORT_SIZE(DatumGetPointer(val));
	}
	else if (typbyval)
	{
		/*
		 * do not align type pass-by-value because anyway we will copy Datum
		 */
		data_length = att_addlength_datum(data_length, typlen, val);
	}
	else
	{
		data_length = att_align_datum(data_length, typalign, typlen, val);
		data_length = att_addlength_datum(data_length, typlen, val);
	}

	return data_length;
}


/*
 * Write the given datum beginning at ptr (after advancing to correct
 * alignment, if needed). Setting padding bytes to zero if needed. Return the
 * pointer incremented by space used.
 */
static Pointer
rumDatumWrite(Pointer ptr, Datum datum, bool typbyval, char typalign,
			  int16 typlen, char typstorage)
{
	Size data_length;
	Pointer prev_ptr = ptr;

	if (typbyval)
	{
		/* pass-by-value */
		union
		{
			int16 i16;
			int32 i32;
		}
		u;

		/* align-safe version of store_att_byval(ptr, datum, typlen); */
		switch (typlen)
		{
			case sizeof(char):
			{
				*ptr = DatumGetChar(datum);
				break;
			}

			case sizeof(int16):
			{
				u.i16 = DatumGetInt16(datum);
				memcpy(ptr, &u.i16, sizeof(int16));
				break;
			}

			case sizeof(int32):
			{
				u.i32 = DatumGetInt32(datum);
				memcpy(ptr, &u.i32, sizeof(int32));
				break;
			}

#if SIZEOF_DATUM == 8
			case sizeof(Datum):
			{
				memcpy(ptr, &datum, sizeof(Datum));
				break;
			}

#endif
			default:
				elog(ERROR, "unsupported byval length: %d", (int) (typlen));
		}

		data_length = (Size) typlen;
	}
	else if (typlen == -1)
	{
		/* varlena */
		Pointer val = DatumGetPointer(datum);

		if (VARATT_IS_EXTERNAL(val))
		{
			/*
			 * Throw error, because we must never put a toast pointer inside a
			 * range object.  Caller should have detoasted it.
			 */
			elog(ERROR, "cannot store a toast pointer inside a range");
			data_length = 0;    /* keep compiler quiet */
		}
		else if (VARATT_IS_SHORT(val))
		{
			/* no alignment for short varlenas */
			data_length = VARSIZE_SHORT(val);
			memmove(ptr, val, data_length);
		}
		else if (TYPE_IS_PACKABLE(typlen, typstorage) &&
				 VARATT_CAN_MAKE_SHORT(val))
		{
			/* convert to short varlena -- no alignment */
			data_length = VARATT_CONVERTED_SHORT_SIZE(val);
			SET_VARSIZE_SHORT(ptr, data_length);
			memmove(ptr + 1, VARDATA(val), data_length - 1);
		}
		else
		{
			/* full 4-byte header varlena */
			ptr = (char *) att_align_nominal(ptr, typalign);
			data_length = VARSIZE(val);
			memmove(ptr, val, data_length);
		}
	}
	else if (typlen == -2)
	{
		/* cstring ... never needs alignment */
		Assert(typalign == 'c');
		data_length = strlen(DatumGetCString(datum)) + 1;
		memmove(ptr, DatumGetPointer(datum), data_length);
	}
	else
	{
		/* fixed-length pass-by-reference */
		ptr = (char *) att_align_nominal(ptr, typalign);
		Assert(typlen > 0);
		data_length = (Size) typlen;
		memmove(ptr, DatumGetPointer(datum), data_length);
	}

	if (ptr != prev_ptr)
	{
		memset(prev_ptr, 0, ptr - prev_ptr);
	}
	ptr += data_length;

	return ptr;
}


/*
 * Write item pointer into leaf data page using varbyte encoding. Since
 * BlockNumber is stored in incremental manner we also need a previous item
 * pointer. Also store addInfoIsNull flag using one bit of OffsetNumber.
 */
static char *
rumDataPageLeafWriteItemPointer(RumState *rumstate, char *ptr, ItemPointer iptr,
								ItemPointer prev,
								bool addInfoIsNull)
{
	uint32 blockNumberIncr = 0;
	uint16 offset = iptr->ip_posid;

	if (rumstate->useAlternativeOrder)
	{
		ItemPointerData x = *iptr;

		if (addInfoIsNull)
		{
			x.ip_posid |= ALT_ADD_INFO_NULL_FLAG;
		}

		memcpy(ptr, &x, sizeof(x));
		ptr += sizeof(x);
	}
	else
	{
		Assert(rumCompareItemPointers(iptr, prev) > 0);
		Assert(OffsetNumberIsValid(iptr->ip_posid));

		blockNumberIncr = iptr->ip_blkid.bi_lo + (iptr->ip_blkid.bi_hi << 16) -
						  (prev->ip_blkid.bi_lo + (prev->ip_blkid.bi_hi << 16));


		while (true)
		{
			*ptr = (blockNumberIncr & (~HIGHBIT)) |
				   ((blockNumberIncr >= HIGHBIT) ? HIGHBIT : 0);
			ptr++;
			if (blockNumberIncr < HIGHBIT)
			{
				break;
			}
			blockNumberIncr >>= 7;
		}

		while (true)
		{
			if (offset >= SEVENTHBIT)
			{
				*ptr = (offset & (~HIGHBIT)) | HIGHBIT;
				ptr++;
				offset >>= 7;
			}
			else
			{
				*ptr = offset | (addInfoIsNull ? SEVENTHBIT : 0);
				ptr++;
				break;
			}
		}
	}

	return ptr;
}


/**
 * Place item pointer with additional information into leaf data page.
 */
Pointer
rumPlaceToDataPageLeaf(Pointer ptr, OffsetNumber attnum,
					   RumItem *item, ItemPointer prev, RumState *rumstate)
{
	Form_pg_attribute attr;

	ptr = rumDataPageLeafWriteItemPointer(rumstate, ptr, &item->iptr, prev,
										  item->addInfoIsNull);

	if (!item->addInfoIsNull)
	{
		attr = rumstate->addAttrs[attnum - 1];
		Assert(attr);

		ptr = rumDatumWrite(ptr, item->addInfo, attr->attbyval, attr->attalign,
							attr->attlen, attr->attstorage);
	}
	return ptr;
}


/*
 * Calculate size of incremental varbyte encoding of item pointer.
 */
static int
rumDataPageLeafGetItemPointerSize(ItemPointer iptr, ItemPointer prev)
{
	uint32 blockNumberIncr = 0;
	uint16 offset = iptr->ip_posid;
	int size = 0;

	blockNumberIncr = iptr->ip_blkid.bi_lo + (iptr->ip_blkid.bi_hi << 16) -
					  (prev->ip_blkid.bi_lo + (prev->ip_blkid.bi_hi << 16));


	while (true)
	{
		size++;
		if (blockNumberIncr < HIGHBIT)
		{
			break;
		}
		blockNumberIncr >>= 7;
	}

	while (true)
	{
		size++;
		if (offset < SEVENTHBIT)
		{
			break;
		}
		offset >>= 7;
	}

	return size;
}


/*
 * Returns size of item pointers with additional information if leaf data page
 * after inserting another one.
 */
Size
rumCheckPlaceToDataPageLeaf(OffsetNumber attnum,
							RumItem *item, ItemPointer prev, RumState *rumstate, Size
							size)
{
	Form_pg_attribute attr;

	if (rumstate->useAlternativeOrder)
	{
		size += sizeof(ItemPointerData);
	}
	else
	{
		size += rumDataPageLeafGetItemPointerSize(&item->iptr, prev);
	}

	if (!item->addInfoIsNull)
	{
		attr = rumstate->addAttrs[attnum - 1];
		Assert(attr);

		size = rumComputeDatumSize(size, item->addInfo, attr->attbyval,
								   attr->attalign, attr->attlen, attr->attstorage);
	}

	return size;
}


int
rumCompareItemPointers(const ItemPointerData *a, const ItemPointerData *b)
{
	BlockNumber ba = RumItemPointerGetBlockNumber(a);
	BlockNumber bb = RumItemPointerGetBlockNumber(b);

	if (ba == bb)
	{
		OffsetNumber oa = RumItemPointerGetOffsetNumber(a);
		OffsetNumber ob = RumItemPointerGetOffsetNumber(b);

		if (oa == ob)
		{
			return 0;
		}
		return (oa > ob) ? 1 : -1;
	}

	return (ba > bb) ? 1 : -1;
}


int
compareRumItem(RumState *state, const AttrNumber attno,
			   const RumItem *a, const RumItem *b)
{
	if (state->useAlternativeOrder && attno == state->attrnAddToColumn)
	{
		/* assume NULL is less than any real value */
		if (a->addInfoIsNull == false && b->addInfoIsNull == false)
		{
			int res;
			AttrNumber attnum = state->attrnAttachColumn;

			res = DatumGetInt32(FunctionCall2Coll(
									&state->compareFn[attnum - 1],
									state->supportCollation[attnum - 1],
									a->addInfo, b->addInfo));
			if (res != 0)
			{
				return res;
			}

			/* fallback to ItemPointerCompare */
		}
		else if (a->addInfoIsNull == true)
		{
			if (b->addInfoIsNull == false)
			{
				return -1;
			}

			/* fallback to ItemPointerCompare */
		}
		else
		{
			Assert(b->addInfoIsNull == true);
			return 1;
		}
	}

	return rumCompareItemPointers(&a->iptr, &b->iptr);
}


/*
 * Merge two ordered arrays of itempointers, eliminating any duplicates.
 * Returns the number of items in the result.
 * Caller is responsible that there is enough space at *dst.
 */
uint32
rumMergeRumItems(RumState *rumstate, AttrNumber attno, RumItem *dst,
				 RumItem *a, uint32 na, RumItem *b, uint32 nb)
{
	RumItem *dptr = dst;
	RumItem *aptr = a,
			*bptr = b;

	while (aptr - a < na && bptr - b < nb)
	{
		int cmp;

		cmp = compareRumItem(rumstate, attno, aptr, bptr);

		if (cmp > 0)
		{
			*dptr++ = *bptr++;
		}
		else if (cmp == 0)
		{
			/* we want only one copy of the identical items */
			*dptr++ = *bptr++;
			aptr++;
		}
		else
		{
			*dptr++ = *aptr++;
		}
	}

	while (aptr - a < na)
	{
		*dptr++ = *aptr++;
	}

	while (bptr - b < nb)
	{
		*dptr++ = *bptr++;
	}

	return dptr - dst;
}


/*
 * Checks, should we move to right link...
 * Compares inserting item pointer with right bound of current page
 */
static bool
dataIsMoveRight(RumBtree btree, Page page)
{
	int res;

	if (RumPageRightMost(page))
	{
		return false;
	}

	res = compareRumItem(btree->rumstate,
						 btree->entryAttnum,
						 &btree->items[btree->curitem],
						 RumDataPageGetRightBound(page));

	return (res > 0) ? true : false;
}


/*
 * Find correct RumPostingItem in non-leaf page. It supposed that page
 * correctly chosen and searching value SHOULD be on page
 */
static BlockNumber
dataLocateItem(RumBtree btree, RumBtreeStack *stack)
{
	OffsetNumber low,
				 high,
				 maxoff;
	RumPostingItem *pitem = NULL;
	int result;
	Page page = BufferGetPage(stack->buffer);

	Assert(!RumPageIsLeaf(page));
	Assert(RumPageIsData(page));

	if (btree->fullScan)
	{
		stack->off = FirstOffsetNumber;
		stack->predictNumber *= RumDataPageMaxOff(page);
		if (ScanDirectionIsForward(btree->scanDirection))
		{
			return dataGetLeftMostPage(btree, page);
		}
		else
		{
			return dataGetRightMostPage(btree, page);
		}
	}

	low = FirstOffsetNumber;
	maxoff = high = RumDataPageMaxOff(page);
	Assert(high >= low);

	high++;

	while (high > low)
	{
		OffsetNumber mid = low + ((high - low) / 2);

		pitem = (RumPostingItem *) RumDataPageGetItem(page, mid);

		if (mid == maxoff)
		{
			/*
			 * Right infinity, page already correctly chosen with a help of
			 * dataIsMoveRight
			 */
			result = -1;
		}
		else
		{
			result = compareRumItem(btree->rumstate,
									btree->entryAttnum,
									&btree->items[btree->curitem],
									&pitem->item);
		}

		if (result == 0)
		{
			stack->off = mid;
			stack->predictNumber *= RumDataPageMaxOff(page) - mid;
			Assert(PostingItemGetBlockNumber(pitem) != 0);
			return PostingItemGetBlockNumber(pitem);
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

	stack->predictNumber *= RumDataPageMaxOff(page) - high;
	stack->off = high;
	pitem = (RumPostingItem *) RumDataPageGetItem(page, high);
	Assert(PostingItemGetBlockNumber(pitem) != 0);
	return PostingItemGetBlockNumber(pitem);
}


void
convertIndexToKey(RumDataLeafItemIndex *src, RumItem *dst)
{
	dst->iptr = src->iptr;
	if (dst->iptr.ip_posid & ALT_ADD_INFO_NULL_FLAG)
	{
		dst->iptr.ip_posid &= ~ALT_ADD_INFO_NULL_FLAG;
		dst->addInfoIsNull = true;
	}
	else
	{
		dst->addInfoIsNull = false;
		dst->addInfo = src->addInfo;
	}
}


/*
 * Find item pointer in leaf data page. Returns true if given item pointer is
 * found and false if it's not. Sets offset and iptrOut to last item pointer
 * which is less than given one. Sets ptrOut ahead that item pointer.
 */
static bool
findInLeafPage(RumBtree btree, Page page, OffsetNumber *offset,
			   ItemPointerData *iptrOut, Pointer *ptrOut)
{
	Pointer ptr = RumDataPageGetData(page);
	OffsetNumber i,
				 maxoff,
				 first = FirstOffsetNumber;
	RumItem item;
	int cmp;

	Assert(RumPageIsData(page));
	Assert(RumPageIsLeaf(page));

	RumItemSetMin(&item);
	maxoff = RumDataPageMaxOff(page);

	/*
	 * At first, search index at the end of page. As the result we narrow
	 * [first, maxoff] range.
	 */
	for (i = 0; i < RumDataLeafIndexCount; i++)
	{
		RumDataLeafItemIndex *index = RumPageGetIndexes(page) + i;

		if (index->offsetNumer == InvalidOffsetNumber)
		{
			break;
		}

		if (btree->rumstate->useAlternativeOrder)
		{
			RumItem k;

			convertIndexToKey(index, &k);
			cmp = compareRumItem(btree->rumstate,
								 btree->entryAttnum, &k,
								 &btree->items[btree->curitem]);
		}
		else
		{
			cmp = rumCompareItemPointers(&index->iptr,
										 &btree->items[btree->curitem].iptr);
		}

		if (cmp < 0)
		{
			ptr = RumDataPageGetData(page) + index->pageOffset;
			first = index->offsetNumer;
			item.iptr = index->iptr;
		}
		else
		{
			maxoff = index->offsetNumer - 1;
			break;
		}
	}

	/* Search page in [first, maxoff] range found by page index */
	for (i = first; i <= maxoff; i++)
	{
		*ptrOut = ptr;
		*iptrOut = item.iptr;

		ptr = rumDataPageLeafRead(ptr, btree->entryAttnum, &item,
								  false, btree->rumstate);

		cmp = compareRumItem(btree->rumstate, btree->entryAttnum,
							 &btree->items[btree->curitem], &item);

		if (cmp == 0)
		{
			*offset = i;
			return true;
		}
		if (cmp < 0)
		{
			*offset = i;
			return false;
		}
	}

	*ptrOut = ptr;
	*iptrOut = item.iptr;
	*offset = RumDataPageMaxOff(page) + 1;
	return false;
}


/*
 * Searches correct position for value on leaf page.
 * Page should be correctly chosen.
 * Returns true if value found on page.
 */
static bool
dataLocateLeafItem(RumBtree btree, RumBtreeStack *stack)
{
	Page page = BufferGetPage(stack->buffer);
	ItemPointerData iptr;
	Pointer ptr;

	Assert(RumPageIsLeaf(page));
	Assert(RumPageIsData(page));

	if (btree->fullScan)
	{
		stack->off = FirstOffsetNumber;
		return true;
	}

	return findInLeafPage(btree, page, &stack->off, &iptr, &ptr);
}


/*
 * Finds links to blkno on non-leaf page, returns
 * offset of RumPostingItem
 */
static OffsetNumber
dataFindChildPtr(RumBtree btree, Page page, BlockNumber blkno, OffsetNumber storedOff)
{
	OffsetNumber i,
				 maxoff = RumDataPageMaxOff(page);
	RumPostingItem *pitem;

	Assert(!RumPageIsLeaf(page));
	Assert(RumPageIsData(page));

	/* if page isn't changed, we return storedOff */
	if (storedOff >= FirstOffsetNumber && storedOff <= maxoff)
	{
		pitem = (RumPostingItem *) RumDataPageGetItem(page, storedOff);
		if (PostingItemGetBlockNumber(pitem) == blkno)
		{
			return storedOff;
		}

		/*
		 * we hope, that needed pointer goes to right. It's true if there
		 * wasn't a deletion
		 */
		for (i = storedOff + 1; i <= maxoff; i++)
		{
			pitem = (RumPostingItem *) RumDataPageGetItem(page, i);
			if (PostingItemGetBlockNumber(pitem) == blkno)
			{
				return i;
			}
		}

		maxoff = storedOff - 1;
	}

	/* last chance */
	for (i = FirstOffsetNumber; i <= maxoff; i++)
	{
		pitem = (RumPostingItem *) RumDataPageGetItem(page, i);
		if (PostingItemGetBlockNumber(pitem) == blkno)
		{
			return i;
		}
	}

	return InvalidOffsetNumber;
}


/*
 * returns blkno of leftmost child
 */
static BlockNumber
dataGetLeftMostPage(RumBtree btree, Page page)
{
	RumPostingItem *pitem;

	Assert(!RumPageIsLeaf(page));
	Assert(RumPageIsData(page));
	Assert(RumDataPageMaxOff(page) >= FirstOffsetNumber);

	pitem = (RumPostingItem *) RumDataPageGetItem(page, FirstOffsetNumber);
	return PostingItemGetBlockNumber(pitem);
}


static BlockNumber
dataGetRightMostPage(RumBtree btree, Page page)
{
	RumPostingItem *pitem;

	Assert(!RumPageIsLeaf(page));
	Assert(RumPageIsData(page));
	Assert(RumDataPageMaxOff(page) >= FirstOffsetNumber);

	pitem = (RumPostingItem *)
			RumDataPageGetItem(page, RumDataPageMaxOff(page));
	return PostingItemGetBlockNumber(pitem);
}


/*
 * add ItemPointer or RumPostingItem to page. data should point to
 * correct value! depending on leaf or non-leaf page
 */
void
RumDataPageAddItem(Page page, void *data, OffsetNumber offset)
{
	OffsetNumber maxoff = RumDataPageMaxOff(page);
	char *ptr;

	Assert(!RumPageIsLeaf(page));

	if (offset == InvalidOffsetNumber)
	{
		ptr = RumDataPageGetItem(page, maxoff + 1);
	}
	else
	{
		ptr = RumDataPageGetItem(page, offset);
		if (offset <= maxoff)
		{
			memmove(ptr + sizeof(RumPostingItem),
					ptr,
					((uint16) (maxoff - offset + 1)) * sizeof(RumPostingItem));
		}
	}
	memcpy(ptr, data, sizeof(RumPostingItem));
	RumDataPageMaxOff(page)++;

	/* Adjust pd_lower */
	((PageHeader) page)->pd_lower =
		RumDataPageGetItem(page, RumDataPageMaxOff(page) + 1) - page;
	Assert(((PageHeader) page)->pd_lower <= ((PageHeader) page)->pd_upper);
}


/*
 * Deletes posting item from non-leaf page
 */
void
RumPageDeletePostingItem(Page page, OffsetNumber offset)
{
	OffsetNumber maxoff = RumDataPageMaxOff(page);

	Assert(!RumPageIsLeaf(page));
	Assert(offset >= FirstOffsetNumber && offset <= maxoff);

	if (offset != maxoff)
	{
		char *dstptr = RumDataPageGetItem(page, offset),
			 *sourceptr = RumDataPageGetItem(page, offset + 1);

		memmove(dstptr, sourceptr, sizeof(RumPostingItem) * (uint16) (maxoff - offset));
	}

	RumDataPageMaxOff(page)--;

	/* Adjust pd_lower */
	((PageHeader) page)->pd_lower =
		RumDataPageGetItem(page, RumDataPageMaxOff(page) + 1) - page;
	Assert(((PageHeader) page)->pd_lower <= ((PageHeader) page)->pd_upper);
}


/*
 * checks space to install at least one new value,
 * item pointer never deletes!
 */
static bool
dataIsEnoughSpace(RumBtree btree, Buffer buf, OffsetNumber off)
{
	Page page = BufferGetPage(buf);

	Assert(RumPageIsData(page));
	Assert(!btree->isDelete);

	if (RumPageIsLeaf(page))
	{
		ItemPointerData iptr = { { 0, 0 }, 0 };
		Size size = 0;

		/*
		 * Calculate additional size using worst case assumption: varbyte
		 * encoding from zero item pointer. Also use worst case assumption
		 * about alignment.
		 */
		size = rumCheckPlaceToDataPageLeaf(btree->entryAttnum,
										   btree->items + btree->curitem, &iptr,
										   btree->rumstate, 0);
		size += MAXIMUM_ALIGNOF;

		if (RumDataPageReadFreeSpaceValue(page) >= size)
		{
			return true;
		}
	}
	else if (RumDataPageIntermediateSplitSize > 1 &&
			 RumDataPageMaxOff(page) > RumDataPageIntermediateSplitSize)
	{
		/* Test only guc to force split intermediate pages */
		return false;
	}
	else if (sizeof(RumPostingItem) <= RumDataPageGetFreeSpace(page))
	{
		return true;
	}

	return false;
}


/*
 * In case of previous split update old child blkno to
 * new right page
 * item pointer never deletes!
 */
static BlockNumber
dataPrepareData(RumBtree btree, Page page, OffsetNumber off)
{
	BlockNumber ret = InvalidBlockNumber;

	Assert(RumPageIsData(page));

	if (!RumPageIsLeaf(page) && btree->rightblkno != InvalidBlockNumber)
	{
		RumPostingItem *pitem = (RumPostingItem *) RumDataPageGetItem(page, off);

		PostingItemSetBlockNumber(pitem, btree->rightblkno);
		ret = btree->rightblkno;
	}

	btree->rightblkno = InvalidBlockNumber;

	return ret;
}


/*
 * Places keys to page and fills WAL record. In case leaf page and
 * build mode puts all ItemPointers to page.
 */
static void
dataPlaceToPage(RumBtree btree, Page page, OffsetNumber off)
{
	Assert(RumPageIsData(page));

	dataPrepareData(btree, page, off);

	if (!RumSkipResetOnDeadEntryPage)
	{
		RumDataPageEntryRevive(page);
	}

	if (RumPageIsLeaf(page))
	{
		Pointer ptr = RumDataPageGetData(page),
				copyPtr = NULL;
		ItemPointerData iptr = { { 0, 0 }, 0 };
		RumItem copyItem;
		bool copyItemEmpty = true;

		/*
		 * Must have pageCopy MAXALIGNed to use PG macros to access data in
		 * it. Should not rely on compiler alignment preferences to avoid
		 * pageCopy overflow related to PG in-memory page items alignment
		 * inside rumDataPageLeafRead() or elsewhere.
		 */
		char pageCopyStorage[BLCKSZ + MAXIMUM_ALIGNOF];
		char *pageCopy = (char *) MAXALIGN(pageCopyStorage);
		int maxoff = RumDataPageMaxOff(page);
		int freespace,
			insertCount = 0;
		bool stopAppend = false;

		RumItemSetMin(&copyItem);

		/*
		 * We're going to prevent var-byte re-encoding of whole page. Find
		 * position in page using page indexes.
		 */
		findInLeafPage(btree, page, &off, &iptr, &ptr);

		if (off <= maxoff)
		{
			/*
			 * Read next item-pointer with additional information: we'll have
			 * to re-encode it. Copy previous part of page.
			 */
			memcpy(pageCopy + (ptr - page), ptr, BLCKSZ - (ptr - page));
			copyPtr = pageCopy + (ptr - page);
			copyItem.iptr = iptr;
		}
		else
		{
			/*
			 * Force insertion of new items until insertion items are less than
			 * right bound.
			 */
		}

		freespace = RumDataPageReadFreeSpaceValue(page);

		/*
		 * We are execute a merge join over old page and new items, but we
		 * should stop inserting of new items if free space of page is ended
		 * to put all old items back
		 */
		while (42)
		{
			int cmp;

			/* get next item to copy if we pushed it on previous loop */
			if (copyItemEmpty == true && off <= maxoff)
			{
				copyPtr = rumDataPageLeafRead(copyPtr, btree->entryAttnum,
											  &copyItem, false,
											  btree->rumstate);
				copyItemEmpty = false;
			}

			if (off <= maxoff && btree->curitem < btree->nitem)
			{
				if (stopAppend)
				{
					cmp = -1; /* force copy */
				}
				else
				{
					cmp = compareRumItem(btree->rumstate, btree->entryAttnum,
										 &copyItem,
										 btree->items + btree->curitem);
				}
			}
			else if (btree->curitem < btree->nitem)
			{
				/* we copied all old items but we have to add more new items */
				if (stopAppend)
				{
					/* there is no free space on page */
					break;
				}
				else if (RumPageRightMost(page))
				{
					/* force insertion of new item */
					cmp = 1;
				}
				else if ((cmp = compareRumItem(btree->rumstate, btree->entryAttnum,
											   RumDataPageGetRightBound(page),
											   btree->items + btree->curitem)) >= 0)
				{
					/*
					 * Force insertion if current item is greater than last item
					 * of the page but less than right bound.
					 */
					if (off > maxoff)
					{
						/* force insertion of new item */
						cmp = 1;
					}
				}
				else
				{
					/* new items should be inserted on next page */
					break;
				}
			}
			else if (off <= maxoff)
			{
				/* force copy, we believe that all old items could be placed */
				cmp = -1;
			}
			else
			{
				break;
			}

			if (cmp <= 0)
			{
				ptr = rumPlaceToDataPageLeaf(ptr, btree->entryAttnum,
											 &copyItem,
											 &iptr, btree->rumstate);

				iptr = copyItem.iptr;
				off++;
				copyItemEmpty = true;

				if (cmp == 0)
				{
					btree->curitem++;
				}
			}
			else /* if (cmp > 0) */
			{
				int newItemSize,
					aligmentSize = ptr - (char *) MAXALIGN_DOWN(ptr);
#ifdef USE_ASSERT_CHECKING
				char *oldptr = ptr;
#endif

				newItemSize = rumCheckPlaceToDataPageLeaf(btree->entryAttnum,
														  btree->items + btree->curitem,
														  &iptr,
														  btree->rumstate, aligmentSize);

				newItemSize -= aligmentSize;

				if (newItemSize <= freespace)
				{
					ptr = rumPlaceToDataPageLeaf(ptr, btree->entryAttnum,
												 btree->items + btree->curitem,
												 &iptr, btree->rumstate);

					Assert(ptr - oldptr == newItemSize);
					iptr = btree->items[btree->curitem].iptr;
					freespace -= newItemSize;
					btree->curitem++;
					insertCount++;
				}
				else
				{
					stopAppend = true;
				}
			}

			Assert(RumDataPageFreeSpacePre(page, ptr) >= 0);
		}

		RumDataPageMaxOff(page) += insertCount;

		/* Update indexes in the end of page */
		updateItemIndexes(page, btree->entryAttnum, btree->rumstate);
	}
	else
	{
		RumDataPageAddItem(page, &(btree->pitem), off);
	}
}


/* Macro for leaf data page split: switch to right page if needed. */
#define CHECK_SWITCH_TO_RPAGE \
	do { \
		if (ptr - RumDataPageGetData(page) > totalsize / 2 && page == newlPage) \
		{ \
			maxLeftItem = curItem; \
			ItemPointerSetMin(&prevIptr); \
			RumDataPageMaxOff(newlPage) = j; \
			page = rPage; \
			ptr = RumDataPageGetData(rPage); \
			j = FirstOffsetNumber; \
		} \
		else \
		{ \
			j++; \
		} \
	} while (0)


/*
 * Place tuple and split page, original buffer(lbuf) leaves untouched,
 * returns shadow page of lbuf filled new data.
 * Item pointers with additional information are distributed between pages by
 * equal size on its, not an equal number!
 */
static Page
dataSplitPageLeaf(RumBtree btree, Buffer lbuf, Buffer rbuf,
				  Page lPage, Page rPage, OffsetNumber off)
{
	OffsetNumber i,
				 j,
				 maxoff;
	Size totalsize = 0,
		 prevTotalsize;
	Pointer ptr,
			copyPtr;
	Page page;
	Page newlPage = PageGetTempPageCopy(lPage);
	Size pageSize = PageGetPageSize(newlPage);
	Size maxItemSize = 0;
	ItemPointerData prevIptr;
	RumItem maxLeftItem,
			curItem;
	RumItem item;
	int maxItemIndex = btree->curitem;

	/*
	 * Must have lpageCopy MAXALIGNed to use PG macros to access data in
	 * it. Should not rely on compiler alignment preferences to avoid
	 * lpageCopy overflow related to PG in-memory page items alignment
	 * inside rumDataPageLeafRead() etc.
	 */
	static char lpageCopyStorage[BLCKSZ + MAXIMUM_ALIGNOF];
	char *lpageCopy = (char *) MAXALIGN(lpageCopyStorage);

	memset(&item, 0, sizeof(item));
	dataPrepareData(btree, newlPage, off);
	maxoff = RumDataPageMaxOff(newlPage);

	/* Copy original data of the page */
	memcpy(lpageCopy, newlPage, BLCKSZ);

	/* Reinitialize pages */
	RumInitPage(rPage, RumPageGetOpaque(newlPage)->flags, pageSize);
	RumInitPage(newlPage, RumPageGetOpaque(rPage)->flags, pageSize);

	RumDataPageMaxOff(newlPage) = 0;
	RumDataPageMaxOff(rPage) = 0;

	if (!RumSkipResetOnDeadEntryPage)
	{
		/* Paranoia: Set the revive flags on the child splits */
		RumDataPageEntryRevive(newlPage);
		RumDataPageEntryRevive(rPage);
	}

	/* Calculate the whole size we're going to place */
	copyPtr = RumDataPageGetData(lpageCopy);
	RumItemSetMin(&item);
	for (i = FirstOffsetNumber; i <= maxoff; i++)
	{
		if (i == off)
		{
			prevIptr = item.iptr;
			item = btree->items[maxItemIndex];

			prevTotalsize = totalsize;
			totalsize = rumCheckPlaceToDataPageLeaf(btree->entryAttnum,
													&item, &prevIptr, btree->rumstate,
													totalsize);

			maxItemIndex++;
			maxItemSize = Max(maxItemSize, totalsize - prevTotalsize);
		}

		prevIptr = item.iptr;
		copyPtr = rumDataPageLeafRead(copyPtr, btree->entryAttnum, &item,
									  false, btree->rumstate);

		prevTotalsize = totalsize;
		totalsize = rumCheckPlaceToDataPageLeaf(btree->entryAttnum,
												&item, &prevIptr, btree->rumstate,
												totalsize);

		maxItemSize = Max(maxItemSize, totalsize - prevTotalsize);
	}

	if (off == maxoff + 1)
	{
		prevIptr = item.iptr;
		item = btree->items[maxItemIndex];
		if (RumPageRightMost(newlPage))
		{
			Size newTotalsize;

			/*
			 * Found how many new item pointer we're going to add using worst
			 * case assumptions about odd placement and alignment.
			 */
			while (maxItemIndex < btree->nitem &&
				   (newTotalsize = rumCheckPlaceToDataPageLeaf(btree->entryAttnum,
															   &item, &prevIptr,
															   btree->rumstate,
															   totalsize)) < 2 *
				   RumDataPageSize - 2 * maxItemSize - 2 *
				   MAXIMUM_ALIGNOF)
			{
				maxItemIndex++;
				maxItemSize = Max(maxItemSize, newTotalsize - totalsize);
				totalsize = newTotalsize;

				prevIptr = item.iptr;
				if (maxItemIndex < btree->nitem)
				{
					item = btree->items[maxItemIndex];
				}
			}
		}
		else
		{
			totalsize = rumCheckPlaceToDataPageLeaf(btree->entryAttnum,
													&item, &prevIptr, btree->rumstate,
													totalsize);
			maxItemIndex++;
		}
	}

	/*
	 * Place item pointers with additional information to the pages using
	 * previous calculations.
	 */
	ptr = RumDataPageGetData(newlPage);
	page = newlPage;
	j = FirstOffsetNumber;

	ItemPointerSetMin(&item.iptr);
	prevIptr = item.iptr;
	copyPtr = RumDataPageGetData(lpageCopy);
	for (i = FirstOffsetNumber; i <= maxoff; i++)
	{
		if (i == off)
		{
			while (btree->curitem < maxItemIndex)
			{
				curItem = btree->items[btree->curitem];
				ptr = rumPlaceToDataPageLeaf(ptr, btree->entryAttnum,
											 &btree->items[btree->curitem],
											 &prevIptr, btree->rumstate);
				Assert(RumDataPageFreeSpacePre(page, ptr) >= 0);

				prevIptr = btree->items[btree->curitem].iptr;
				btree->curitem++;

				CHECK_SWITCH_TO_RPAGE;
			}
		}

		copyPtr = rumDataPageLeafRead(copyPtr, btree->entryAttnum, &item,
									  false, btree->rumstate);

		curItem = item;
		ptr = rumPlaceToDataPageLeaf(ptr, btree->entryAttnum, &item,
									 &prevIptr, btree->rumstate);
		Assert(RumDataPageFreeSpacePre(page, ptr) >= 0);

		prevIptr = item.iptr;

		CHECK_SWITCH_TO_RPAGE;
	}

	if (off == maxoff + 1)
	{
		while (btree->curitem < maxItemIndex)
		{
			curItem = btree->items[btree->curitem];
			ptr = rumPlaceToDataPageLeaf(ptr, btree->entryAttnum,
										 &btree->items[btree->curitem], &prevIptr,
										 btree->rumstate);
			Assert(RumDataPageFreeSpacePre(page, ptr) >= 0);

			prevIptr = btree->items[btree->curitem].iptr;
			btree->curitem++;

			CHECK_SWITCH_TO_RPAGE;
		}
	}

	RumDataPageMaxOff(rPage) = j - 1;

	PostingItemSetBlockNumber(&(btree->pitem), BufferGetBlockNumber(lbuf));
	btree->pitem.item = maxLeftItem;
	btree->rightblkno = BufferGetBlockNumber(rbuf);

	*RumDataPageGetRightBound(rPage) = *RumDataPageGetRightBound(lpageCopy);
	*RumDataPageGetRightBound(newlPage) = maxLeftItem;

	/* Fill indexes at the end of pages */
	updateItemIndexes(newlPage, btree->entryAttnum, btree->rumstate);
	updateItemIndexes(rPage, btree->entryAttnum, btree->rumstate);

	return newlPage;
}


/*
 * split page and fills WAL record. original buffer(lbuf) leaves untouched,
 * returns shadow page of lbuf filled new data. In leaf page and build mode puts all
 * ItemPointers to pages. Also, in build mode splits data by way to full fulled
 * left page
 */
static Page
dataSplitPageInternal(RumBtree btree, Buffer lbuf, Buffer rbuf,
					  Page lPage, Page rPage, OffsetNumber off)
{
	char *ptr;
	OffsetNumber separator;
	RumItem *bound;
	Page newlPage = PageGetTempPageCopy(BufferGetPage(lbuf));
	RumItem oldbound = *RumDataPageGetRightBound(newlPage);
	unsigned int sizeofitem = sizeof(RumPostingItem);
	OffsetNumber maxoff = RumDataPageMaxOff(newlPage);
	Size pageSize = PageGetPageSize(newlPage);
	Size freeSpace;

	/*
	 * Must have vector MAXALIGNed to use PG macros to access data in
	 * it. Should not rely on compiler alignment preferences to avoid
	 * vector overflow related to PG in-memory page items alignment
	 * inside rumDataPageLeafRead() etc.
	 */
	static char vectorStorage[2 * BLCKSZ + MAXIMUM_ALIGNOF];
	char *vector = (char *) MAXALIGN(vectorStorage);

	RumInitPage(rPage, RumPageGetOpaque(newlPage)->flags, pageSize);
	freeSpace = RumDataPageGetFreeSpace(rPage);
	dataPrepareData(btree, newlPage, off);

	memcpy(vector, RumDataPageGetItem(newlPage, FirstOffsetNumber),
		   maxoff * sizeofitem);

	Assert(!RumPageIsLeaf(newlPage));
	ptr = vector + (off - 1) * sizeofitem;
	if (maxoff + 1 - off != 0)
	{
		memmove(ptr + sizeofitem, ptr, (uint16) (maxoff - off + 1) * sizeofitem);
	}
	memcpy(ptr, &(btree->pitem), sizeofitem);

	maxoff++;

	/*
	 * we suppose that during index creation table scaned from begin to end,
	 * so ItemPointers are monotonically increased..
	 */
	if (btree->rumstate && btree->rumstate->isBuild &&
		RumPageRightMost(newlPage))
	{
		separator = freeSpace / sizeofitem;
	}
	else
	{
		separator = maxoff / 2;
	}

	RumInitPage(rPage, RumPageGetOpaque(newlPage)->flags, pageSize);
	RumInitPage(newlPage, RumPageGetOpaque(rPage)->flags, pageSize);

	if (!RumSkipResetOnDeadEntryPage)
	{
		/* Paranoia: Set the revive flags on the child splits */
		RumDataPageEntryRevive(newlPage);
		RumDataPageEntryRevive(rPage);
	}

	if (RumEnableNewBulkDelete)
	{
		RumPageGetCycleId(newlPage) = rum_vacuum_get_cycleId(btree->index);
		RumPageGetCycleId(rPage) = RumPageGetCycleId(newlPage);
	}

	ptr = RumDataPageGetItem(newlPage, FirstOffsetNumber);
	memcpy(ptr, vector, separator * sizeofitem);
	RumDataPageMaxOff(newlPage) = separator;

	/* Adjust pd_lower */
	((PageHeader) newlPage)->pd_lower = (ptr + separator * sizeofitem) -
										newlPage;

	ptr = RumDataPageGetItem(rPage, FirstOffsetNumber);
	memcpy(ptr, vector + separator * sizeofitem,
		   (uint16) (maxoff - separator) * sizeofitem);
	RumDataPageMaxOff(rPage) = maxoff - separator;

	/* Adjust pd_lower */
	((PageHeader) rPage)->pd_lower = (ptr +
									  (maxoff - separator) * sizeofitem) -
									 rPage;

	PostingItemSetBlockNumber(&(btree->pitem), BufferGetBlockNumber(lbuf));
	if (RumPageIsLeaf(newlPage))
	{
		btree->pitem.item.iptr = ((RumPostingItem *)
								  RumDataPageGetItem(newlPage, RumDataPageMaxOff(
														 newlPage)))->item.iptr;
	}
	else
	{
		btree->pitem.item = ((RumPostingItem *)
							 RumDataPageGetItem(newlPage, RumDataPageMaxOff(
													newlPage)))->item;
	}
	btree->rightblkno = BufferGetBlockNumber(rbuf);

	/* set up right bound for left page */
	bound = RumDataPageGetRightBound(newlPage);
	*bound = btree->pitem.item;

	/* set up right bound for right page */
	bound = RumDataPageGetRightBound(rPage);
	*bound = oldbound;

	return newlPage;
}


/*
 * Split page of posting tree. Calls relevant function of internal of leaf page
 * because they are handled very different.
 */
static Page
dataSplitPage(RumBtree btree, Buffer lbuf, Buffer rbuf,
			  Page lpage, Page rpage, OffsetNumber off)
{
	if (RumPageIsLeaf(BufferGetPage(lbuf)))
	{
		return dataSplitPageLeaf(btree, lbuf, rbuf, lpage, rpage, off);
	}
	else
	{
		return dataSplitPageInternal(btree, lbuf, rbuf, lpage, rpage, off);
	}
}


/*
 * Updates indexes in the end of leaf page which are used for faster search.
 * Also updates freespace opaque field of page. Returns last item pointer of
 * page.
 */
void
updateItemIndexes(Page page, OffsetNumber attnum, RumState *rumstate)
{
	Pointer ptr;
	RumItem item;
	int j = 0,
		maxoff,
		i;
	InitBlockNumberIncrZero(blocknoIncr);

	/* Iterating through the page data */

	maxoff = RumDataPageMaxOff(page);
	ptr = RumDataPageGetData(page);
	RumItemSetMin(&item);

	for (i = FirstOffsetNumber; i <= maxoff; i++)
	{
		/* Place next page index entry if it's time to */
		if (i * (RumDataLeafIndexCount + 1) > (j + 1) * maxoff)
		{
			RumDataLeafItemIndex *e = RumPageGetIndexes(page) + j;

			e->iptr = item.iptr;
			e->offsetNumer = i;
			e->pageOffset = ptr - RumDataPageGetData(page);
			if (rumstate->useAlternativeOrder)
			{
				e->addInfo = item.addInfo;
				if (item.addInfoIsNull)
				{
					e->iptr.ip_posid |= ALT_ADD_INFO_NULL_FLAG;
				}
			}
			j++;
		}

		if (RumUseNewItemPtrDecoding)
		{
			ptr = rumDataPageLeafReadWithBlockNumberIncr(ptr, attnum, &item, false,
														 rumstate, &blocknoIncr);
		}
		else
		{
			ptr = rumDataPageLeafRead(ptr, attnum, &item, false, rumstate);
		}
	}

	/* Fill rest of page indexes with InvalidOffsetNumber if any */
	for (; j < RumDataLeafIndexCount; j++)
	{
		RumPageGetIndexes(page)[j].offsetNumer = InvalidOffsetNumber;
	}

	/* Update freespace of page */
	RumDataPageReadFreeSpaceValue(page) = RumDataPageFreeSpacePre(page, ptr);

	/* Adjust pd_lower and pd_upper */
	((PageHeader) page)->pd_lower = ptr - page;
	((PageHeader) page)->pd_upper = ((char *) RumPageGetIndexes(page)) - page;

	Assert(ptr <= (char *) RumPageGetIndexes(page));
	Assert(((PageHeader) page)->pd_upper >= ((PageHeader) page)->pd_lower);
	Assert(((PageHeader) page)->pd_upper - ((PageHeader) page)->pd_lower ==
		   RumDataPageReadFreeSpaceValue(page));
}


/*
 * Fills new root by right bound values from child.
 * Also called from rumxlog, should not use btree
 */
void
rumDataFillRoot(RumBtree btree, Buffer root, Buffer lbuf, Buffer rbuf,
				Page page, Page lpage, Page rpage)
{
	RumPostingItem li,
				   ri;

	memset(&li, 0, sizeof(RumPostingItem));
	li.item = *RumDataPageGetRightBound(lpage);
	PostingItemSetBlockNumber(&li, BufferGetBlockNumber(lbuf));
	RumDataPageAddItem(page, &li, InvalidOffsetNumber);

	memset(&ri, 0, sizeof(RumPostingItem));
	ri.item = *RumDataPageGetRightBound(rpage);
	PostingItemSetBlockNumber(&ri, BufferGetBlockNumber(rbuf));
	RumDataPageAddItem(page, &ri, InvalidOffsetNumber);
}


static void
rumDataFillBtreeForIncompleteSplit(RumBtree btree, RumBtreeStack *stack, Buffer buffer)
{
	Page page = BufferGetPage(buffer);

	/* Fill rightblkno to ensure we're tracking the split page */
	btree->rightblkno = RumPageGetOpaque(page)->rightlink;

	/* Set the posting item */
	if (RumPageIsLeaf(page))
	{
		btree->pitem.item = *RumDataPageGetRightBound(page);
	}
	else
	{
		btree->pitem.item = ((RumPostingItem *)
							 RumDataPageGetItem(page, RumDataPageMaxOff(
													page)))->item;
	}

	PostingItemSetBlockNumber(&btree->pitem, BufferGetBlockNumber(buffer));
}


void
rumPrepareDataScan(RumBtree btree, Relation index, OffsetNumber attnum,
				   RumState *rumstate)
{
	memset(btree, 0, sizeof(RumBtreeData));

	btree->index = index;
	btree->rumstate = rumstate;

	btree->findChildPage = dataLocateItem;
	btree->isMoveRight = dataIsMoveRight;
	btree->findItem = dataLocateLeafItem;
	btree->findChildPtr = dataFindChildPtr;
	btree->getLeftMostPage = dataGetLeftMostPage;
	btree->isEnoughSpace = dataIsEnoughSpace;
	btree->placeToPage = dataPlaceToPage;
	btree->splitPage = dataSplitPage;
	btree->fillRoot = rumDataFillRoot;
	btree->fillBtreeForIncompleteSplit = rumDataFillBtreeForIncompleteSplit;

	btree->isData = true;
	btree->searchMode = false;
	btree->isDelete = false;
	btree->fullScan = false;
	btree->scanDirection = ForwardScanDirection;

	btree->entryAttnum = attnum;
}


RumPostingTreeScan *
rumPrepareScanPostingTree(Relation index, BlockNumber rootBlkno,
						  bool searchMode, ScanDirection scanDirection,
						  OffsetNumber attnum, RumState *rumstate)
{
	RumPostingTreeScan *gdi = (RumPostingTreeScan *) palloc0(sizeof(RumPostingTreeScan));

	rumPrepareDataScan(&gdi->btree, index, attnum, rumstate);

	gdi->btree.searchMode = searchMode;
	gdi->btree.fullScan = searchMode;
	gdi->btree.scanDirection = scanDirection;

	gdi->stack = rumPrepareFindLeafPage(&gdi->btree, rootBlkno);

	return gdi;
}


/*
 * Inserts array of item pointers, may execute several tree scan (very rare)
 */
void
rumInsertItemPointers(RumState *rumstate,
					  OffsetNumber attnum,
					  RumPostingTreeScan *gdi,
					  RumItem *items, uint32 nitem,
					  RumStatsData *buildStats)
{
	BlockNumber rootBlkno;

	Assert(gdi->stack);
	rootBlkno = gdi->stack->blkno;
	gdi->btree.items = items;
	gdi->btree.nitem = nitem;
	gdi->btree.curitem = 0;

	while (gdi->btree.curitem < gdi->btree.nitem)
	{
		if (!gdi->stack)
		{
			gdi->stack = rumPrepareFindLeafPage(&gdi->btree, rootBlkno);
		}

		gdi->stack = rumFindLeafPage(&gdi->btree, gdi->stack);

		if (gdi->btree.findItem(&(gdi->btree), gdi->stack))
		{
			/*
			 * gdi->btree.items[gdi->btree.curitem] already exists in index
			 */
			gdi->btree.curitem++;

			/* Before we unlock, ensure that the page is revived if needed */
			if (!RumSkipResetOnDeadEntryPage &&
				RumDataPageEntryIsDead(BufferGetPage(gdi->stack->buffer)))
			{
				GenericXLogState *state = GenericXLogStart(gdi->btree.index);
				Page page = GenericXLogRegisterBuffer(state, gdi->stack->buffer, 0);
				RumDataPageEntryRevive(page);
				GenericXLogFinish(state);
			}

			LockBuffer(gdi->stack->buffer, RUM_UNLOCK);
			freeRumBtreeStack(gdi->stack);
		}
		else
		{
			rumInsertValue(rumstate->index, &(gdi->btree), gdi->stack, buildStats);
		}

		gdi->stack = NULL;
	}
}


Buffer
rumScanBeginPostingTree(RumPostingTreeScan *gdi, RumItem *item)
{
	if (item)
	{
		gdi->btree.fullScan = false;
		gdi->btree.items = item;
		gdi->btree.curitem = 0;
		gdi->btree.nitem = 1;
	}

	gdi->stack = rumFindLeafPage(&gdi->btree, gdi->stack);
	return gdi->stack->buffer;
}
