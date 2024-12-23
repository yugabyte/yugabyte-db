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
#include "opclass/helio_gin_index_mgmt.h"

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

	/* The key size limit for wildcard indexes with truncation enabled. */
	uint32_t wildcardIndexTruncatedPathLimit;

	/* The path prefix to truncate from the index term path. */
	StringView pathPrefix;

	/* If the term belongs to a wildcard index. */
	bool isWildcard;

	/* If the term belongs to a wildcard projection index. */
	bool isWildcardProjection;

	/* The index version for this index */
	IndexOptionsVersion indexVersion;
} IndexTermCreateMetadata;


bool IsSerializedIndexTermTruncated(bytea *indexTermSerialized);
void InitializeBsonIndexTerm(bytea *indexTermSerialized, BsonIndexTerm *indexTerm);

BsonIndexTermSerialized SerializeBsonIndexTerm(pgbsonelement *indexElement,
											   const IndexTermCreateMetadata *
											   indexMetadata);

Datum GenerateRootTerm(const IndexTermCreateMetadata *);
Datum GenerateRootExistsTerm(const IndexTermCreateMetadata *);
Datum GenerateRootNonExistsTerm(const IndexTermCreateMetadata *);
Datum GenerateRootTruncatedTerm(const IndexTermCreateMetadata *);
Datum GenerateRootMultiKeyTerm(const IndexTermCreateMetadata *);

int32_t CompareBsonIndexTerm(BsonIndexTerm *left, BsonIndexTerm *right,
							 bool *isComparisonValid);

#endif
