/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/opclass/bson_gin_private.h
 *
 * Private declarations of the bson gin methods shared across files
 * that implement the GIN index extensibility
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_GIN_PRIVATE_H
#define BSON_GIN_PRIVATE_H

#include "opclass/bson_gin_common.h"
#include "planner/mongo_query_operator.h"
#include "operators/bson_expr_eval.h"
#include "opclass/bson_gin_index_term.h"
#include "opclass/bson_gin_index_mgmt.h"

/*
 * Arguments to Extract query used by mongo operators wrapped in a struct.
 */
typedef struct BsonExtractQueryArgs
{
	pgbsonelement filterElement;

	const char *collationString;

	int32 *nentries;

	bool **partialmatch;

	Pointer **extra_data;

	bytea *options;

	IndexTermCreateMetadata termMetadata;
} BsonExtractQueryArgs;

/*
 * Holds state pertinent to index term evaluation for
 * Expression based $elemMatch.
 */
typedef struct BsonElemMatchIndexExprState
{
	/* The query expression to be evaluated */
	ExprEvalState *expression;

	/* Whether or not the query expression is empty */
	bool isEmptyExpression;
} BsonElemMatchIndexExprState;


Datum * GinBsonExtractQueryCore(BsonIndexStrategy strategy,
								BsonExtractQueryArgs *args);
int32_t GinBsonComparePartialCore(BsonIndexStrategy strategy, BsonIndexTerm *queryValue,
								  BsonIndexTerm *compareValue, Pointer extraData);
bool GinBsonConsistentCore(BsonIndexStrategy strategy, bool *check,
						   Pointer *extra_data, int32_t numKeys, bool *recheck,
						   Datum *queryKeys, bytea *options, bool isPreconsistent);
int32_t GinBsonComparePartialElemMatchExpression(BsonIndexTerm *queryValue,
												 BsonIndexTerm *compareValue,
												 BsonElemMatchIndexExprState *exprState);

/* $elemMatch methods */

Datum * GinBsonExtractQueryElemMatch(BsonExtractQueryArgs *args);
int32_t GinBsonComparePartialElemMatch(BsonIndexTerm *queryValue,
									   BsonIndexTerm *compareValue,
									   Pointer extraData);
bool GinBsonElemMatchConsistent(bool *checkArray, Pointer *extraData, int32_t numKeys);

/* Shared with exclusion ops */

/*
 * Defines the behavior of index term traversal for a given path when walking a document
 */
typedef enum IndexTraverseOption
{
	/* The path is invalid and no terms should be generated in that tree. */
	IndexTraverse_Invalid = 0,

	/* The path may have valid descendants that could generate terms on the index */
	/* do not consider the path, but recurse down nested objects and arrays for terms */
	IndexTraverse_Recurse = 0x1,

	/* the path is a match and should be added to the index. */
	IndexTraverse_Match = 0x2,

	/* The path is a match and child paths are also matches */
	IndexTraverse_MatchAndRecurse = 0x3,
} IndexTraverseOption;


/* Index path options */
IndexTraverseOption GetSinglePathIndexTraverseOption(void *contextOptions,
													 const char *currentPath,
													 uint32_t currentPathLength,
													 bson_type_t bsonType,
													 int32_t *pathIndex);
IndexTraverseOption GetWildcardProjectionPathIndexTraverseOption(void *contextOptions,
																 const char *currentPath,
																 uint32_t
																 currentPathLength,
																 bson_type_t
																 bsonType,
																 int32_t *pathIndex);
IndexTraverseOption GetSinglePathIndexTraverseOptionCore(const char *indexPath,
														 uint32_t indexPathLength,
														 const char *currentPath,
														 uint32_t currentPathLength,
														 bool isWildcard);

/*
 * Defines a function that provides instruction on how to handle a path 'currentPath' given an index option
 * specification via contextOptions.
 */
typedef IndexTraverseOption (*GetIndexTraverseOptionFunc)(void *contextOptions, const
														  char *currentPath, uint32_t
														  currentPathLength,
														  bson_type_t valueType,
														  int32_t *pathIndex);


typedef struct GinEntrySet
{
	/* The entry array of index terms.*/
	Datum *entries;

	/* The size in Datum entries available in entries. */
	int32_t entryCapacity;

	/* the current position into the entries that should be written into. */
	int32_t index;
} GinEntrySet;

typedef struct GinEntryPathData
{
	/* holds the actual index term datums. */
	GinEntrySet terms;

	/* metadata including truncation limit for index terms. */
	IndexTermCreateMetadata termMetadata;

	/*
	 * When an array path has subtrees that have terms, and other other subtrees
	 * that do not have terms, this is marked as true.
	 */
	bool hasArrayPartialTermExistence;

	/* Whether or not to generate path based undefined terms (used in composite indexes) */
	bool generatePathBasedUndefinedTerms;

	/* Whether or not to skip generating the top level document term */
	bool skipGenerateTopLevelDocumentTerm;

	/*
	 * Whether or not to use the reduced wildcard term generation support.
	 */
	bool useReducedWildcardTerms;

	/* OUTPUT: The core terms count - without any metadata post-term generation */
	int32_t coreTermsCount;

	/* OUTPUT: Whether a root truncation term has already been created for this document */
	bool hasTruncatedTerms;

	/* OUTPUT: Whether array values were encountered during term generation */
	bool hasArrayValues;

	/*
	 * OUTPUT: Whether or not the path has array ancestors in the pre paths:
	 * for a path a.b.c
	 * if a, or b are arrays then this returns true.
	 * For wildcard indexes, returns true if any path had an array ancestor
	 */
	bool hasArrayAncestors;
} GinEntryPathData;


/*
 * Context object used to keep track of state while generating terms for the index.
 */
typedef struct
{
	/* Any index configuration options used to determine whether to generate terms or not */
	void *options;

	/* A function that provides instruction on whether a path should be indexed or not. */
	GetIndexTraverseOptionFunc traverseOptionsFunc;

	/* Whether or not to generate the not found term for a path */
	bool generateNotFoundTerm;

	/* Whether or not to skip generating the path undefined term on null */
	bool skipGeneratedPathUndefinedTermOnLiteralNull;

	/* Whether or not to skip generating the top level array term */
	bool skipGenerateTopLevelArrayTerm;

	bool enableCompositeReducedCorrelatedTerms;

	/* Path specific data (per path info) */
	void *pathDataState;

	/* Get the index term path data struct for the given path index */
	GinEntryPathData *(*getPathDataFunc)(void *pathDataState, int index);

	/* returns whether or not the path can be matched recursively (as opposed to exactly) */
	bool (*isRecursivePathMatch)(void *pathDataState, int index);

	/* the number of recursive path terms generated currently */
	int (*currentRecursivePathIndex)(void *pathDataState);

	/*
	 * Update correlated term path index terms. When this is not null, it means we should
	 * consider nested document terms as correlated for term generation.
	 */
	void (*updateCorrelatedTermPaths)(void *pathDataState, int32_t *previousTermIndex,
									  bool *termMatchStatus);

	int maxPaths;
} GenerateTermsContext;


/* Exports for the core index processing layer. */
void GenerateSinglePathTermsCore(pgbson *bson, GenerateTermsContext *context,
								 GinEntryPathData *pathData,
								 BsonGinSinglePathOptions *singlePathOptions);

void GenerateWildcardPathTermsCore(pgbson *bson, GenerateTermsContext *context,
								   GinEntryPathData *pathData,
								   BsonGinWildcardProjectionPathOptions *wildcardOptions);

void GenerateTermsForPath(pgbson *bson, GenerateTermsContext *context);
void GenerateTerms(pgbson *bson, GenerateTermsContext *context,
				   GinEntryPathData *pathData, bool addRootTerm);

Datum *GinBsonExtractQueryUniqueIndexTerms(PG_FUNCTION_ARGS);
Datum *GinBsonExtractQueryOrderBy(PG_FUNCTION_ARGS);
int32_t GinBsonComparePartialOrderBy(BsonIndexTerm *queryValue,
									 BsonIndexTerm *compareValue);

IndexTermCreateMetadata GetIndexTermMetadata(void *indexOptions);

IndexTraverseOption GetCompositePathIndexTraverseOption(BsonIndexStrategy strategy,
														void *contextOptions, const
														char *currentPath, uint32_t
														currentPathLength,
														const bson_value_t *bsonValue,
														int32_t *compositeIndexCol);
#endif
