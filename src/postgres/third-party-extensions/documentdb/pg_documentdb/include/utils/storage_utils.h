/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/storage_utils.h
 *
 * Utilities that provide physical storage related metrics and information.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <nodes/parsenodes.h>

#ifndef DOCDB_STORAGE_UTILS_H
#define DOCDB_STORAGE_UTILS_H

#define BYTES_PER_MB (double) (1024 * 1024)

typedef struct CollectionBloatStats
{
	/* Whether the PG stats are available or not for the collection */
	bool nullStats;

	/* Estimated bloats storage consumed by dead tuples for the colleciton in bytes based on PG relation's stats */
	uint64 estimatedBloatStorage;

	/* Estimated total collection size based on PG relation's stats  */
	uint64 estimatedTableStorage;
} CollectionBloatStats;

CollectionBloatStats GetCollectionBloatEstimate(uint64 collectionId);

#endif
