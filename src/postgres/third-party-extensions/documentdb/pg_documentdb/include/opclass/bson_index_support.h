/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/opclass/bson_index_support.h
 *
 * Common declarations for Index support functions.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_INDEX_SUPPORT_H
#define BSON_INDEX_SUPPORT_H

#include <opclass/bson_text_gin.h>
#include <vector/vector_utilities.h>
#include <optimizer/planner.h>
#include "planner/mongo_query_operator.h"

struct IndexOptInfo;

/*
 * Input immutable data for the ReplaceExtensionFunctionContext
 */
typedef struct ReplaceFunctionContextInput
{
	/* Whether or not to do a runtime check for $text */
	bool isRuntimeTextScan;

	/* Whether or not this is the query on the actual Shard table */
	bool isShardQuery;

	/* CollectionId of the base collection if it's known */
	uint64 collectionId;
} ReplaceFunctionContextInput;


/* The operation type for forcing index pushdown */
typedef enum ForceIndexOpType
{
	/* No index pushdown required */
	ForceIndexOpType_None,

	/* Index pushdown required due to $text */
	ForceIndexOpType_Text,

	/* Index pushdown required due to $geoNear */
	ForceIndexOpType_GeoNear,

	/* Index pushdown required for a vectorSearch */
	ForceIndexOpType_VectorSearch
} ForceIndexOpType;

/*
 * Data used to enforce index to special query operators like $geoNear, $text etc
 */
typedef struct ForceIndexQueryOperatorData
{
	/* Type of the mongo query operator used */
	ForceIndexOpType type;

	/*
	 * If pushed to index by default by Postgres, then the it points to the index path otherwise NULL
	 * In case this is NULL, we try to push to the available index
	 */
	IndexPath *path;

	/*
	 * Any operator specific metadata or state.
	 * e.g. For $geoNear, it is the operatorExpression which is used for deciding the index pushdown
	 */
	void *opExtraState;
} ForceIndexQueryOperatorData;

/*
 * Context object passed between ReplaceExtensionFunctionOperatorsInPaths
 * and ReplaceExtensionFunctionOperatorsInRestrictionPaths. This takes context
 * about what index paths were replaced and uses that in the replacement of
 * restriction paths.
 */
typedef struct ReplaceExtensionFunctionContext
{
	SearchQueryEvalData queryDataForVectorSearch;

	/* Whether or not the index paths/restriction paths have vector search query */
	bool hasVectorSearchQuery;

	/* Whether or not the index paths already has a primary key lookup */
	IndexPath *primaryKeyLookupPath;

	/* The input data context for the call */
	ReplaceFunctionContextInput inputData;

	/* The index data for operators can be put inside this, which are mutually exclusive and should require index */
	ForceIndexQueryOperatorData forceIndexQueryOpData;
} ReplaceExtensionFunctionContext;

/* Type of the parent node in the query plan of a query for $in optimization. This is not
 * intended for general use */
typedef enum PlanParentType
{
	/* Don't perform $in rewrite when parent is invalid */
	PARENTTYPE_INVALID = 0,

	/* Perform rewrite, but the rewritten BitmapORPath needs to be wrapped in a BitMapHeapPath*/
	PARENTTYPE_NONE,

	/* Peform rewrite into a BitmapORPath*/
	PARENTTYPE_BITMAPHEAP
}PlanParentType;

List * ReplaceExtensionFunctionOperatorsInRestrictionPaths(List *restrictInfo,
														   ReplaceExtensionFunctionContext
														   *context);
void ReplaceExtensionFunctionOperatorsInPaths(PlannerInfo *root, RelOptInfo *rel,
											  List *pathsList, PlanParentType parentType,
											  ReplaceExtensionFunctionContext *context);
void ForceIndexForQueryOperators(PlannerInfo *root, RelOptInfo *rel,
								 ReplaceExtensionFunctionContext *context);


bool IsBtreePrimaryKeyIndex(struct IndexOptInfo *indexInfo);
#endif
