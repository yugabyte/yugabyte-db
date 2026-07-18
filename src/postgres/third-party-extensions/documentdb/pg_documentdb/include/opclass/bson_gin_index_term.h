/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/opclass/bson_gin_index_term.h
 *
 * Common declarations of the serialization of index terms.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_GIN_INDEX_TERM_H
#define BSON_GIN_INDEX_TERM_H

#include "utils/string_view.h"
#include "io/bson_core.h"
#include "opclass/bson_gin_index_mgmt.h"

/* Struct used in manipulating bson index terms */
typedef struct BsonIndexTerm
{
	/* The metadata for the term */
	uint8_t termMetadata;

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

/* Struct for a serialized index term */
typedef struct BsonCompressableIndexTermSerialized
{
	/* Whether or not the term is truncated */
	bool isIndexTermTruncated;

	/* The serialized index term value */
	Datum indexTermDatum;
} BsonCompressableIndexTermSerialized;

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

	/* Version of the index */
	IndexOptionsVersion indexVersion;

	/* Whether or not the term is for a descending index */
	bool isDescending;

	/* Whether the index supports value only terms */
	bool allowValueOnly;
} IndexTermCreateMetadata;

bool IsIndexTermMetadata(const BsonIndexTerm *indexTerm);


bool IsIndexTermTruncated(const BsonIndexTerm *indexTerm);


/* Special case of an undefined value in an array that
 * has a defined value.
 */
bool IsIndexTermMaybeUndefined(const BsonIndexTerm *indexTerm);


/* Whether or not an undefined term is due to the
 * value being undefined (as opposed to the listeral
 * undefined).
 */
bool IsIndexTermValueUndefined(const BsonIndexTerm *indexTerm);


/*
 * Whether or not the index term is compared in a descending manner.
 */
bool IsIndexTermValueDescending(const BsonIndexTerm *indexTerm);

bool IsSerializedIndexTermComposite(bytea *indexTermSerialized);
bool IsSerializedIndexTermTruncated(bytea *indexTermSerialized);
bool IsSerializedIndexTermMetadata(bytea *indexTermSerialized);

void InitializeBsonIndexTerm(bytea *indexTermSerialized, BsonIndexTerm *indexTerm);

int32_t InitializeCompositeIndexTerm(bytea *indexTermSerialized, BsonIndexTerm
									 indexTerm[INDEX_MAX_KEYS]);

int32_t InitializeSerializedCompositeIndexTerm(bytea *indexTermSerialized,
											   bytea *termValues[INDEX_MAX_KEYS]);

BsonIndexTermSerialized SerializeBsonIndexTerm(pgbsonelement *indexElement,
											   const IndexTermCreateMetadata *
											   indexMetadata);
BsonCompressableIndexTermSerialized SerializeBsonIndexTermWithCompression(
	pgbsonelement *indexElement,
	const
	IndexTermCreateMetadata
	*indexMetadata);

BsonIndexTermSerialized SerializeCompositeBsonIndexTerm(bytea **individualTerms, int32_t
														numTerms);
BsonCompressableIndexTermSerialized SerializeCompositeBsonIndexTermWithCompression(
	bytea **individualTerms, int32_t numTerms);

typedef enum RootMetadataKind
{
	RootMetadataKind_CorrelatedRootArray = 1
} RootMetadataKind;

Datum GenerateRootTerm(const IndexTermCreateMetadata *);
Datum GenerateRootExistsTerm(const IndexTermCreateMetadata *);
Datum GenerateRootNonExistsTerm(const IndexTermCreateMetadata *);
Datum GenerateRootTruncatedTerm(const IndexTermCreateMetadata *);
Datum GenerateRootMultiKeyTerm(const IndexTermCreateMetadata *);
Datum GenerateCorrelatedRootArrayTerm(const IndexTermCreateMetadata *);
Datum GenerateValueUndefinedTerm(const IndexTermCreateMetadata *termData);
Datum GenerateValueMaybeUndefinedTerm(const IndexTermCreateMetadata *termData);
int32_t CompareBsonIndexTerm(const BsonIndexTerm *left, const BsonIndexTerm *right,
							 bool *isComparisonValid);

/* Check if the term is a root truncation term */
inline static bool
IsRootTruncationTerm(const BsonIndexTerm *term)
{
	return IsIndexTermTruncated(term) &&
		   term->element.pathLength == 0 &&
		   term->element.bsonValue.value_type == BSON_TYPE_MAXKEY;
}


#endif
