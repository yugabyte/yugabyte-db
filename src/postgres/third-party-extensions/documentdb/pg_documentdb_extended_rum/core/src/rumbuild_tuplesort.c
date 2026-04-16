/*-------------------------------------------------------------------------
 *
 * rumbuild_tuplesort.c
 *	  Compatibility functions for rumbuild_tuplesort
 *
 *
 * Common declarations for RUM tuplesort exports for index builds.
 *
 * Note: In order to support parallel sort, portions of this file are taken from
 * gininsert.c in postgres
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "rumbuild_tuplesort.h"
#include "storage/itemptr.h"
#include "utils/typcache.h"
#include "catalog/pg_collation.h"

#define MaxHeapTuplesPerPageBits 11
#define MaxBytesPerInteger 7

#define RumNextPostingListSegment(cur) ((RumPostingList *) (((char *) (cur)) + \
															SizeOfRumPostingList((cur))))

static inline uint64
itemptr_to_uint64(const ItemPointerData *iptr)
{
	uint64 val;

	Assert(ItemPointerIsValid(iptr));

	val = ItemPointerGetBlockNumberNoCheck(iptr);
	val <<= MaxHeapTuplesPerPageBits;
	val |= ItemPointerGetOffsetNumberNoCheck(iptr);

	return val;
}


static inline void
uint64_to_itemptr(uint64 val, ItemPointer iptr)
{
	ItemPointerSetOffsetNumber(iptr, val & ((1 << MaxHeapTuplesPerPageBits) - 1));
	val = val >> MaxHeapTuplesPerPageBits;
	ItemPointerSetBlockNumber(iptr, val);

	Assert(ItemPointerIsValid(iptr));
}


static void
encode_varbyte(uint64 val, unsigned char **ptr)
{
	unsigned char *p = *ptr;

	while (val > 0x7F)
	{
		*(p++) = 0x80 | (val & 0x7F);
		val >>= 7;
	}
	*(p++) = (unsigned char) val;

	*ptr = p;
}


static uint64
decode_varbyte(unsigned char **ptr)
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


RumPostingList *
rumCompressPostingList(const RumItem *ipd, int nipd, int maxsize,
					   int *nwritten)
{
	uint64 prev;
	int totalpacked = 0;
	int maxbytes;
	RumPostingList *result;
	unsigned char *ptr;
	unsigned char *endptr;

	maxsize = SHORTALIGN_DOWN(maxsize);

	result = palloc(maxsize);

	maxbytes = maxsize - offsetof(RumPostingList, bytes);
	Assert(maxbytes > 0);

	/* Store the first special item */
	result->first = ipd[0].iptr;

	prev = itemptr_to_uint64(&result->first);

	ptr = result->bytes;
	endptr = result->bytes + maxbytes;
	for (totalpacked = 1; totalpacked < nipd; totalpacked++)
	{
		uint64 val = itemptr_to_uint64(&ipd[totalpacked].iptr);
		uint64 delta = val - prev;

		Assert(val > prev);

		if (endptr - ptr >= MaxBytesPerInteger)
		{
			encode_varbyte(delta, &ptr);
		}
		else
		{
			/*
			 * There are less than 7 bytes left. Have to check if the next
			 * item fits in that space before writing it out.
			 */
			unsigned char buf[MaxBytesPerInteger];
			unsigned char *p = buf;

			encode_varbyte(delta, &p);
			if (p - buf > (endptr - ptr))
			{
				break;          /* output is full */
			}
			memcpy(ptr, buf, p - buf);
			ptr += (p - buf);
		}
		prev = val;
	}
	result->nbytes = ptr - result->bytes;

	/*
	 * If we wrote an odd number of bytes, zero out the padding byte at the
	 * end.
	 */
	if (result->nbytes != SHORTALIGN(result->nbytes))
	{
		result->bytes[result->nbytes] = 0;
	}

	if (nwritten)
	{
		*nwritten = totalpacked;
	}

	Assert(SizeOfRumPostingList(result) <= maxsize);
	return result;
}


RumItem *
rumPostingListDecodeAllSegments(RumPostingList *segment, int len, int *ndecoded_out)
{
	RumItem *result;
	int nallocated;
	uint64 val;
	char *endseg = ((char *) segment) + len;
	int ndecoded;
	unsigned char *ptr;
	unsigned char *endptr;

	/*
	 * Guess an initial size of the array.
	 */
	nallocated = segment->nbytes * 2 + 1;
	result = palloc(nallocated * sizeof(RumItem));

	ndecoded = 0;
	while ((char *) segment < endseg)
	{
		/* enlarge output array if needed */
		if (ndecoded >= nallocated)
		{
			nallocated *= 2;
			result = repalloc(result, nallocated * sizeof(RumItem));
		}

		/* copy the first item */
		Assert(OffsetNumberIsValid(ItemPointerGetOffsetNumber(&segment->first)));
		Assert(ndecoded == 0 || rumCompareItemPointers(&segment->first, &result[ndecoded -
																				1].iptr) >
			   0);
		result[ndecoded].iptr = segment->first;
		result[ndecoded].addInfoIsNull = true; /* no addInfo in posting lists */
		result[ndecoded].addInfo = (Datum) 0;
		ndecoded++;

		val = itemptr_to_uint64(&segment->first);
		ptr = segment->bytes;
		endptr = segment->bytes + segment->nbytes;
		while (ptr < endptr)
		{
			/* enlarge output array if needed */
			if (ndecoded >= nallocated)
			{
				nallocated *= 2;
				result = repalloc(result, nallocated * sizeof(RumItem));
			}

			val += decode_varbyte(&ptr);

			uint64_to_itemptr(val, &result[ndecoded].iptr);
			result[ndecoded].addInfoIsNull = true;
			result[ndecoded].addInfo = (Datum) 0;
			ndecoded++;
		}
		segment = RumNextPostingListSegment(segment);
	}

	if (ndecoded_out)
	{
		*ndecoded_out = ndecoded;
	}
	return result;
}


RumItem *
rumMergeItemPointers(RumItem *a, uint32 na,
					 RumItem *b, uint32 nb, int *nmerged)
{
	RumItem *dst;

	dst = (RumItem *) palloc((na + nb) * sizeof(RumItem));

	/*
	 * If the argument arrays don't overlap, we can just append them to each
	 * other.
	 */
	if (na == 0 || nb == 0 || rumCompareItemPointers(&a[na - 1].iptr, &b[0].iptr) < 0)
	{
		memcpy(dst, a, na * sizeof(RumItem));
		memcpy(&dst[na], b, nb * sizeof(RumItem));
		*nmerged = na + nb;
	}
	else if (rumCompareItemPointers(&b[nb - 1].iptr, &a[0].iptr) < 0)
	{
		memcpy(dst, b, nb * sizeof(RumItem));
		memcpy(&dst[nb], a, na * sizeof(RumItem));
		*nmerged = na + nb;
	}
	else
	{
		RumItem *dptr = dst;
		RumItem *aptr = a;
		RumItem *bptr = b;

		while (aptr - a < na && bptr - b < nb)
		{
			int cmp = rumCompareItemPointers(&aptr->iptr, &bptr->iptr);

			if (cmp > 0)
			{
				*dptr++ = *bptr++;
			}
			else if (cmp == 0)
			{
				/* only keep one copy of the identical items */
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

		*nmerged = dptr - dst;
	}

	return dst;
}


#if PG_VERSION_NUM >= 160000
static void
removeabbrev_index_rum(Tuplesortstate *state, SortTuple *stups, int count)
{
	Assert(false);
	elog(ERROR, "removeabbrev_index_rum not implemented");
}


static int
_rum_compare_tuples(RumTuple *a, RumTuple *b, SortSupport ssup)
{
	int r;
	Datum keya,
		  keyb;

	if (a->attrnum < b->attrnum)
	{
		return -1;
	}

	if (a->attrnum > b->attrnum)
	{
		return 1;
	}

	if (a->category < b->category)
	{
		return -1;
	}

	if (a->category > b->category)
	{
		return 1;
	}

	if (a->category == RUM_CAT_NORM_KEY)
	{
		keya = _rum_parse_tuple_key(a);
		keyb = _rum_parse_tuple_key(b);

		r = ApplySortComparator(keya, false,
								keyb, false,
								&ssup[a->attrnum - 1]);

		/* if the key is the same, consider the first TID in the array */
		return (r != 0) ? r : ItemPointerCompare(RumTupleGetFirst(a),
												 RumTupleGetFirst(b));
	}

	return ItemPointerCompare(RumTupleGetFirst(a),
							  RumTupleGetFirst(b));
}


static int
comparetup_index_rum(const SortTuple *a, const SortTuple *b,
					 Tuplesortstate *state)
{
	TuplesortPublic *base = TuplesortstateGetPublic(state);

	Assert(!TuplesortstateGetPublic(state)->haveDatum1);

	return _rum_compare_tuples((RumTuple *) a->tuple,
							   (RumTuple *) b->tuple,
							   base->sortKeys);
}


static void
writetup_index_rum(Tuplesortstate *state, LogicalTape *tape, SortTuple *stup)
{
	TuplesortPublic *base = TuplesortstateGetPublic(state);
	RumTuple *tuple = (RumTuple *) stup->tuple;
	unsigned int tuplen = tuple->tuplen;

	tuplen = tuplen + sizeof(tuplen);
	LogicalTapeWrite(tape, &tuplen, sizeof(tuplen));
	LogicalTapeWrite(tape, tuple, tuple->tuplen);
	if (base->sortopt & TUPLESORT_RANDOMACCESS) /* need trailing length word? */
	{
		LogicalTapeWrite(tape, &tuplen, sizeof(tuplen));
	}
}


static void
readtup_index_rum(Tuplesortstate *state, SortTuple *stup,
				  LogicalTape *tape, unsigned int len)
{
	RumTuple *tuple;
	TuplesortPublic *base = TuplesortstateGetPublic(state);
	unsigned int tuplen = len - sizeof(unsigned int);

	/*
	 * Allocate space for the RUM sort tuple, which already has the proper
	 * length included in the header.
	 */
	tuple = (RumTuple *) tuplesort_readtup_alloc(state, tuplen);

	tuple->tuplen = tuplen;

	LogicalTapeReadExact(tape, tuple, tuplen);
	if (base->sortopt & TUPLESORT_RANDOMACCESS) /* need trailing length word? */
	{
		LogicalTapeReadExact(tape, &tuplen, sizeof(tuplen));
	}
	stup->tuple = (void *) tuple;

	/* no abbreviations (FIXME maybe use attrnum for this?) */
	stup->datum1 = (Datum) 0;
}


Tuplesortstate *
tuplesort_begin_indexbuild_rum(Relation heapRel,
							   Relation indexRel,
							   int workMem, SortCoordinate coordinate,
							   int sortopt)
{
	Tuplesortstate *state = tuplesort_begin_common(workMem, coordinate,
												   sortopt);
	TuplesortPublic *base = TuplesortstateGetPublic(state);
	MemoryContext oldcontext;
	int i;
	TupleDesc desc = RelationGetDescr(indexRel);

	oldcontext = MemoryContextSwitchTo(base->maincontext);

	/*
	 * Multi-column RUM indexes expand the row into a separate index entry for
	 * attribute, and that's what we write into the tuplesort. But we still
	 * need to initialize sortsupport for all the attributes.
	 */
	base->nKeys = IndexRelationGetNumberOfKeyAttributes(indexRel);

	/* Prepare SortSupport data for each column */
	base->sortKeys = (SortSupport) palloc0(base->nKeys *
										   sizeof(SortSupportData));

	for (i = 0; i < base->nKeys; i++)
	{
		SortSupport sortKey = base->sortKeys + i;
		Form_pg_attribute att = TupleDescAttr(desc, i);
		TypeCacheEntry *typentry;

		sortKey->ssup_cxt = CurrentMemoryContext;
		sortKey->ssup_collation = indexRel->rd_indcollation[i];
		sortKey->ssup_nulls_first = false;
		sortKey->ssup_attno = i + 1;
		sortKey->abbreviate = false;

		Assert(sortKey->ssup_attno != 0);

		if (!OidIsValid(sortKey->ssup_collation))
		{
			sortKey->ssup_collation = DEFAULT_COLLATION_OID;
		}

		/*
		 * Look for a ordering for the index key data type, and then the sort
		 * support function.
		 */
		typentry = lookup_type_cache(att->atttypid, TYPECACHE_LT_OPR);
		PrepareSortSupportFromOrderingOp(typentry->lt_opr, sortKey);
	}

	base->removeabbrev = removeabbrev_index_rum;
	base->comparetup = comparetup_index_rum;
	base->writetup = writetup_index_rum;
	base->readtup = readtup_index_rum;
	base->haveDatum1 = false;
	base->arg = NULL;

	MemoryContextSwitchTo(oldcontext);

	return state;
}


void
tuplesort_putrumtuple(Tuplesortstate *state, RumTuple *tuple, Size size)
{
#if PG_VERSION_NUM >= 170000
	Size tuplen;
#endif
	SortTuple stup;
	RumTuple *ctup;
	TuplesortPublic *base = TuplesortstateGetPublic(state);
	MemoryContext oldcontext = MemoryContextSwitchTo(base->tuplecontext);

	/* copy the RumTuple into the right memory context */
	ctup = palloc(size);
	memcpy(ctup, tuple, size);

	stup.tuple = ctup;
	stup.datum1 = (Datum) 0;
	stup.isnull1 = false;

#if PG_VERSION_NUM >= 170000

	/* GetMemoryChunkSpace is not supported for bump contexts */
	if (TupleSortUseBumpTupleCxt(base->sortopt))
	{
		tuplen = MAXALIGN(size);
	}
	else
	{
		tuplen = GetMemoryChunkSpace(ctup);
	}

	tuplesort_puttuple_common(state, &stup,
							  base->sortKeys &&
							  base->sortKeys->abbrev_converter &&
							  !stup.isnull1, tuplen);
#else
	tuplesort_puttuple_common(state, &stup,
							  base->sortKeys &&
							  base->sortKeys->abbrev_converter &&
							  !stup.isnull1);
#endif

	MemoryContextSwitchTo(oldcontext);
}


RumTuple *
tuplesort_getrumtuple(Tuplesortstate *state, Size *len, bool forward)
{
	TuplesortPublic *base = TuplesortstateGetPublic(state);
	MemoryContext oldcontext = MemoryContextSwitchTo(base->sortcontext);
	SortTuple stup;
	RumTuple *tup;

	if (!tuplesort_gettuple_common(state, forward, &stup))
	{
		stup.tuple = NULL;
	}

	MemoryContextSwitchTo(oldcontext);

	if (!stup.tuple)
	{
		return NULL;
	}

	tup = (RumTuple *) stup.tuple;

	*len = tup->tuplen;

	return tup;
}


#else

Tuplesortstate *
tuplesort_begin_indexbuild_rum(Relation heapRel,
							   Relation indexRel,
							   int workMem, SortCoordinate coordinate,
							   int sortopt)
{
	ereport(ERROR, errmsg("RUM parallel index build requires PostgreSQL 16 or later"));
}


RumTuple *
tuplesort_getrumtuple(Tuplesortstate *state, Size *len, bool forward)
{
	ereport(ERROR, errmsg("RUM parallel index build requires PostgreSQL 16 or later"));
}


void
tuplesort_putrumtuple(Tuplesortstate *state, RumTuple *tuple, Size size)
{
	ereport(ERROR, errmsg("RUM parallel index build requires PostgreSQL 16 or later"));
}


#endif
