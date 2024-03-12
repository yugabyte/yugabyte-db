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

	/* Whether or not it's a metadata term */
	bool isIndexTermMetadata;

	/* The index term element */
	pgbsonelement element;
} BsonIndexTerm;

/* Struct for a serialized index term */
typedef struct BsonIndexTermSerialized
{
	/* Whether or not the term is truncated */
	bool isIndexTermTruncated;

	/* Whether or not it's a root metadata term (exists/not exists) */
	bool isRootMetadataTerm;

	/* The serialized index term value */
	bytea *indexTermVal;
} BsonIndexTermSerialized;

/*
 * Index term metadata used in creating index terms.
 */
typedef struct IndexTermCreateMetadata
{
	/* Index term size limit for the term. */
	int32_t indexTermSizeLimit;

	/* The path prefix to truncate from the index term path. */
	StringView pathPrefix;

	/* If the path prefix is a wildcard path */
	bool isWildcardPathPrefix;
} IndexTermCreateMetadata;


bool IsSerializedIndexTermTruncated(bytea *indexTermSerialized);
void InitializeBsonIndexTerm(bytea *indexTermSerialized, BsonIndexTerm *indexTerm);

BsonIndexTermSerialized SerializeBsonIndexTerm(pgbsonelement *indexElement,
											   const IndexTermCreateMetadata *
											   indexMetadata);

Datum GenerateRootTerm(void);
Datum GenerateRootExistsTerm(void);
Datum GenerateRootNonExistsTerm(void);
Datum GenerateRootTruncatedTerm(void);
Datum GenerateRootMultiKeyTerm(void);

int32_t CompareBsonIndexTerm(BsonIndexTerm *left, BsonIndexTerm *right,
							 bool *isComparisonValid);

#endif
