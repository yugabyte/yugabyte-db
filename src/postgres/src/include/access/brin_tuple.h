/*
 * brin_tuple.h
 *		Declarations for dealing with BRIN-specific tuples.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/include/access/brin_tuple.h
 */
#ifndef BRIN_TUPLE_H
#define BRIN_TUPLE_H

#include "access/brin_internal.h"
#include "access/tupdesc.h"

/*
 * The BRIN opclasses may register serialization callback, in case the on-disk
 * and in-memory representations differ (e.g. for performance reasons).
 */
typedef void (*brin_serialize_callback_type) (BrinDesc *bdesc, Datum src, Datum *dst);

/*
 * A BRIN index stores one index tuple per page range.  Each index tuple
 * has one BrinValues struct for each indexed column; in turn, each BrinValues
 * has (besides the null flags) an array of Datum whose size is determined by
 * the opclass.
 */
typedef struct BrinValues
{
	AttrNumber	bv_attno;		/* index attribute number */
	bool		bv_hasnulls;	/* are there any nulls in the page range? */
	bool		bv_allnulls;	/* are all values nulls in the page range? */
	Datum	   *bv_values;		/* current accumulated values */
	Datum		bv_mem_value;	/* expanded accumulated values */
	MemoryContext bv_context;
	brin_serialize_callback_type bv_serialize;
} BrinValues;

/*
 * This struct is used to represent an in-memory index tuple.  The values can
 * only be meaningfully decoded with an appropriate BrinDesc.
 */
typedef struct BrinMemTuple
{
	bool		bt_placeholder; /* this is a placeholder tuple */
	BlockNumber bt_blkno;		/* heap blkno that the tuple is for */
	MemoryContext bt_context;	/* memcxt holding the bt_columns values */
	/* output arrays for brin_deform_tuple: */
	Datum	   *bt_values;		/* values array */
	bool	   *bt_allnulls;	/* allnulls array */
	bool	   *bt_hasnulls;	/* hasnulls array */
	/* not an output array, but must be last */
	BrinValues	bt_columns[FLEXIBLE_ARRAY_MEMBER];
} BrinMemTuple;

/*
 * An on-disk BRIN tuple.  This is possibly followed by a nulls bitmask, with
 * room for 2 null bits (two bits for each indexed column); an opclass-defined
 * number of Datum values for each column follow.
 */
typedef struct BrinTuple
{
	/* heap block number that the tuple is for */
	BlockNumber bt_blkno;

	/* ---------------
	 * bt_info is laid out in the following fashion:
	 *
	 * 7th (high) bit: has nulls
	 * 6th bit: is placeholder tuple
	 * 5th bit: unused
	 * 4-0 bit: offset of data
	 * ---------------
	 */
	uint8		bt_info;
} BrinTuple;

#define SizeOfBrinTuple (offsetof(BrinTuple, bt_info) + sizeof(uint8))

/*
 * bt_info manipulation macros
 */
#define BRIN_OFFSET_MASK		0x1F
/* bit 0x20 is not used at present */
#define BRIN_PLACEHOLDER_MASK	0x40
#define BRIN_NULLS_MASK			0x80

#define BrinTupleDataOffset(tup)	((Size) (((BrinTuple *) (tup))->bt_info & BRIN_OFFSET_MASK))
#define BrinTupleHasNulls(tup)	(((((BrinTuple *) (tup))->bt_info & BRIN_NULLS_MASK)) != 0)
#define BrinTupleIsPlaceholder(tup) (((((BrinTuple *) (tup))->bt_info & BRIN_PLACEHOLDER_MASK)) != 0)


extern BrinTuple *brin_form_tuple(BrinDesc *brdesc, BlockNumber blkno,
								  BrinMemTuple *tuple, Size *size);
extern BrinTuple *brin_form_placeholder_tuple(BrinDesc *brdesc,
											  BlockNumber blkno, Size *size);
extern void brin_free_tuple(BrinTuple *tuple);
extern BrinTuple *brin_copy_tuple(BrinTuple *tuple, Size len,
								  BrinTuple *dest, Size *destsz);
extern bool brin_tuples_equal(const BrinTuple *a, Size alen,
							  const BrinTuple *b, Size blen);

extern BrinMemTuple *brin_new_memtuple(BrinDesc *brdesc);
extern BrinMemTuple *brin_memtuple_initialize(BrinMemTuple *dtuple,
											  BrinDesc *brdesc);
extern BrinMemTuple *brin_deform_tuple(BrinDesc *brdesc,
									   BrinTuple *tuple, BrinMemTuple *dMemtuple);

#endif							/* BRIN_TUPLE_H */
