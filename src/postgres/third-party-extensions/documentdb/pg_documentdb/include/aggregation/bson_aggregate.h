/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/aggregation/bson_aggregates.h
 *
 * BSON Aggregate headers
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_AGGREGATE_H
#define BSON_AGGREGATE_H

#include <postgres.h>
#if PG_VERSION_NUM >= 160000
#include <varatt.h>
#endif

/*
 * Varlena wrapper for MAXALIGN(8bytes) structs that are serialized as a varlena
 */
typedef struct pg_attribute_aligned (MAXIMUM_ALIGNOF) MaxAlignedVarlena
{
	int32 vl_len_; /* (DO NOT TOUCH) varlena header */
	int32 pad;     /* (UNUSED SPACE) 4 bytes to align following data at 8-byte boundary */
	char state[FLEXIBLE_ARRAY_MEMBER]; /* raw struct bytes to follow */
} MaxAlignedVarlena;


/*
 * Allocate a MaxAlignedVarlena of structSize bytes without
 * zeroing the memory.
 */
static inline MaxAlignedVarlena *
AllocateMaxAlignedVarlena(Size structSize)
{
	Size total = sizeof(MaxAlignedVarlena) + structSize;
	MaxAlignedVarlena *bytes = (MaxAlignedVarlena *) palloc(total);
	SET_VARSIZE(bytes, total);
	return bytes;
}


/*
 * Allocate a MaxAlignedVarlena of structSize bytes with
 * zeroing the memory.
 */
static inline MaxAlignedVarlena *
AllocateZeroedMaxAlignedVarlena(Size structSize)
{
	Size total = sizeof(MaxAlignedVarlena) + structSize;
	MaxAlignedVarlena *bytes = (MaxAlignedVarlena *) palloc0(total);
	SET_VARSIZE(bytes, total);
	return bytes;
}


static inline MaxAlignedVarlena *
GetMaxAlignedVarlena(bytea *bytes)
{
	return (MaxAlignedVarlena *) bytes;
}


#endif
