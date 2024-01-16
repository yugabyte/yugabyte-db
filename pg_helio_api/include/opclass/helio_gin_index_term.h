/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/opclass/helio_gin_index_term.h
 *
 * Common declarations of the serialization of index terms.
 *
 *-------------------------------------------------------------------------
 */

#ifndef HELIO_GIN_INDEX_TERM_H
#define HELIO_GIN_INDEX_TERM_H

#include "utils/string_view.h"
#include "io/helio_bson_core.h"

/* Struct used in manipulating bson index terms */
typedef struct BsonIndexTerm
{
	/* Whether or not the term is truncated */
	bool isIndexTermTruncated;

	/* The index term element */
	pgbsonelement element;
} BsonIndexTerm;

/* Struct for a serialized index term */
typedef struct BsonIndexTermSerialized
{
	/* Whether or not the term is truncated */
	bool isIndexTermTruncated;

	/* The serialized index term value */
	bytea *indexTermVal;
} BsonIndexTermSerialized;


bool IsSerializedIndexTermTruncated(bytea *indexTermSerialized);
void InitializeBsonIndexTerm(bytea *indexTermSerialized, BsonIndexTerm *indexTerm);

BsonIndexTermSerialized SerializeBsonIndexTerm(pgbsonelement *indexElement,
											   int32_t indexTermSizeLimit);

Datum GenerateRootTerm(void);
Datum GenerateRootTruncatedTerm(void);

int32_t CompareBsonIndexTerm(BsonIndexTerm *left, BsonIndexTerm *right,
							 bool *isComparisonValid);

#endif
