/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/opclass/bson_gin_index_mgmt.h
 *
 * Common declarations of the bson index management methods.
 *
 *-------------------------------------------------------------------------
 */

 #ifndef BSON_GIN_COMPOSITE_PRIVATE_H
 #define BSON_GIN_COMPOSITE_PRIVATE_H

 #include "io/bson_core.h"
 #include "opclass/bson_gin_index_mgmt.h"

typedef struct CompositeSingleBound
{
	bson_value_t bound;
	bool isBoundInclusive;

	/* The processed bound (post truncation if any) */
	bytea *serializedTerm;
	BsonIndexTerm indexTermValue;
} CompositeSingleBound;

typedef struct IndexRecheckArgs
{
	Pointer queryDatum;

	BsonIndexStrategy queryStrategy;
} IndexRecheckArgs;

typedef struct CompositeIndexBounds
{
	CompositeSingleBound lowerBound;
	CompositeSingleBound upperBound;

	bool isEqualityBound;

	bool requiresRuntimeRecheck;

	/* A list of IndexRecheckArgs that need recheck */
	List *indexRecheckFunctions;
} CompositeIndexBounds;

typedef struct PathScanKeyMap
{
	/* integer list of term indexes - one for each scanKey */
	List *scanIndices;
} PathScanKeyMap;

typedef struct PathScanTermMap
{
	/* Integer list of key indexes - one for each index path */
	List *scanKeyIndexList;
	int32_t numTermsPerPath;
} PathScanTermMap;

typedef struct CompositeQueryMetaInfo
{
	int32_t numIndexPaths;
	bool hasTruncation;
	int32_t truncationTermIndex;
	bool requiresRuntimeRecheck;
	int32_t numScanKeys;
	bool hasMultipleScanKeysPerPath;
	bool isBackwardScan;
	PathScanKeyMap *scanKeyMap;
	int32_t wildcardPathIndex;
} CompositeQueryMetaInfo;

typedef struct CompositeQueryRunData
{
	CompositeQueryMetaInfo *metaInfo;
	const char *wildcardPath;
	CompositeIndexBounds indexBounds[FLEXIBLE_ARRAY_MEMBER];
} CompositeQueryRunData;

typedef struct CompositeIndexBoundsSet
{
	/* The index path attribute (0 based) */
	int32_t indexAttribute;
	int32_t numBounds;

	/* The index path (if it is a wildcard match) */
	const char *wildcardPath;
	CompositeIndexBounds bounds[FLEXIBLE_ARRAY_MEMBER];
} CompositeIndexBoundsSet;

typedef struct VariableIndexBounds
{
	/* List of CompositeIndexBoundsSet */
	List *variableBoundsList;
} VariableIndexBounds;

static inline CompositeIndexBoundsSet *
CreateCompositeIndexBoundsSet(int32_t numTerms, int32_t indexAttribute,
							  const char *wildcardPath)
{
	CompositeIndexBoundsSet *set = palloc0(sizeof(CompositeIndexBoundsSet) +
										   (sizeof(CompositeIndexBounds) * numTerms));
	set->numBounds = numTerms;
	set->wildcardPath = wildcardPath;
	set->indexAttribute = indexAttribute;
	return set;
}


bool IsValidRecheckForIndexValue(const BsonIndexTerm *compareTerm,
								 IndexRecheckArgs *recheckArgs);

bytea * BuildLowerBoundTermFromIndexBounds(CompositeQueryRunData *runData,
										   IndexTermCreateMetadata *metadata,
										   bool *hasInequalityMatch, const
										   char **indexPaths,
										   uint32_t *indexPathLengths,
										   int8_t *sortOrders);

bool UpdateBoundsForTruncation(CompositeQueryRunData *runData,
							   IndexTermCreateMetadata *metadata,
							   const char **indexPaths, uint32_t *indexPathLengths,
							   int8_t *sortOrders);

void ParseOperatorStrategy(const char **indexPaths, uint32_t *indexPathLengths,
						   int32_t numPaths, int32_t wildcardIndex,
						   pgbsonelement *queryElement,
						   BsonIndexStrategy queryStrategy,
						   VariableIndexBounds *indexBounds);

void UpdateRunDataForVariableBounds(CompositeQueryRunData *runData,
									PathScanTermMap *termMap,
									VariableIndexBounds *variableBounds,
									int32_t permutation);

List * MergeSingleVariableBounds(List *variableBounds, const char **wildcardPath,
								 CompositeIndexBounds *mergedBounds);
List * MergeWildCardSingleVariableBounds(List *variableBounds);

void TrimSecondaryVariableBounds(VariableIndexBounds *variableBounds,
								 CompositeQueryRunData *runData);
void PickVariableBoundsForOrderedScan(VariableIndexBounds *variableBounds,
									  CompositeQueryRunData *runData);


/*
 * Simplified version of IsCompositePathWildcardMatch that doesn't do the array index
 * path validation. To be used for pre-validated paths.
 * CODESYNC: IsCompositePathWildcardMatch
 */
inline static bool
IsCompositePathWildcardMatchNoArrayCheck(const char *currentPath, const char *indexPath,
										 uint32_t indexPathLength)
{
	if (strncmp(currentPath, indexPath, indexPathLength) != 0)
	{
		/* not a match */
		return false;
	}

	if (indexPathLength == 0)
	{
		return true;
	}

	if (currentPath[indexPathLength] == '\0' ||
		currentPath[indexPathLength] == '.')
	{
		/* Exact match. */
		return true;
	}

	return false;
}


/* CODESYNC with IsCompositePathWildcardMatchNoArrayCheck */
inline static bool
IsCompositePathWildcardMatch(const char *currentPath, const char *indexPath,
							 uint32_t indexPathLength)
{
	if (strncmp(currentPath, indexPath, indexPathLength) != 0)
	{
		/* not a match */
		return false;
	}

	if (indexPathLength == 0)
	{
		/* Root wildcard */
		StringView parentPath = CreateStringViewFromString(currentPath);
		StringView subPath = StringViewFindPrefix(&parentPath, '.');
		return !SubPathHasArrayIndexElements(&parentPath, subPath);
	}

	if (currentPath[indexPathLength] == '\0')
	{
		/* Exact match. */
		return true;
	}
	else if (currentPath[indexPathLength] == '.')
	{
		/* or dotted suffix */
		StringView parentPath = CreateStringViewFromString(currentPath);
		StringView remainingPath = StringViewSubstring(&parentPath, indexPathLength + 1);
		return !SubPathHasArrayIndexElements(&parentPath, remainingPath);
	}

	return false;
}


#endif
