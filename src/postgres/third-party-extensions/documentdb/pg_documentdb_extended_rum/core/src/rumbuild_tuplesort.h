/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/rumbuild_tuplesort.h
 *
 * Common declarations for RUM tuplesort exports for index builds.
 *
 * Note: In order to support parallel sort, portions of this file are taken from
 * gininsert.c in postgres
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *-------------------------------------------------------------------------
 */

#ifndef RUMBUILD_TUPLESORT_H
#define RUMBUILD_TUPLESORT_H

#include "utils/tuplesort.h"
#include "pg_documentdb_rum.h"


/*
 * Data for one key in a RUM index.
 */
typedef struct RumTuple
{
	int tuplen;                 /* length of the whole tuple */
	OffsetNumber attrnum;       /* attnum of index key */
	uint16 keylen;              /* bytes in data for key value */
	int16 typlen;               /* typlen for key */
	bool typbyval;              /* typbyval for key */
	signed char category;       /* category: normal or NULL? */
	int nitems;                 /* number of RumItems in the data */
	char data[FLEXIBLE_ARRAY_MEMBER];
} RumTuple;

extern Tuplesortstate * tuplesort_begin_indexbuild_rum(Relation heapRel,
													   Relation indexRel,
													   int workMem, SortCoordinate
													   coordinate,
													   int sortopt);

void tuplesort_putrumtuple(Tuplesortstate *state, RumTuple *tuple, Size size);
RumTuple * tuplesort_getrumtuple(Tuplesortstate *state, Size *len, bool forward);

typedef struct
{
	ItemPointerData first;      /* first item in this posting list (unpacked) */
	uint16 nbytes;              /* number of bytes that follow */
	unsigned char bytes[FLEXIBLE_ARRAY_MEMBER]; /* varbyte encoded items */
} RumPostingList;

#define SizeOfRumPostingList(plist) (offsetof(RumPostingList, bytes) + SHORTALIGN( \
										 (plist)->nbytes))


RumPostingList * rumCompressPostingList(const RumItem *ipd, int nipd, int maxsize,
										int *nwritten);
RumItem * rumPostingListDecodeAllSegments(RumPostingList *segment, int len,
										  int *ndecoded_out);

RumItem * rumMergeItemPointers(RumItem *a, uint32 na,
							   RumItem *b, uint32 nb, int *nmerged);

static inline ItemPointer
RumTupleGetFirst(RumTuple *tup)
{
	RumPostingList *list;

	list = (RumPostingList *) SHORTALIGN(tup->data + tup->keylen);

	return &list->first;
}


Datum _rum_parse_tuple_key(RumTuple *a);
#endif
