/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/opclass/index_support.c
 *
 * Support methods for index selection and push down.
 * See also: https://www.postgresql.org/docs/current/gin-extensibility.html
 * See also: https://www.postgresql.org/docs/current/xfunc-optimization.html
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>
#include <fmgr.h>
#include <nodes/nodes.h>
#include <utils/builtins.h>
#include <catalog/pg_type.h>
#include <nodes/pathnodes.h>
#include <nodes/supportnodes.h>
#include <nodes/makefuncs.h>
#include <catalog/pg_am.h>
#include <optimizer/paths.h>
#include <optimizer/pathnode.h>
#include "nodes/pg_list.h"
#include <pg_config_manual.h>

#include "query/query_operator.h"
#include "geospatial/bson_geospatial_geonear.h"
#include "planner/mongo_query_operator.h"
#include "opclass/bson_index_support.h"
#include "opclass/bson_gin_index_mgmt.h"
#include "opclass/bson_text_gin.h"
#include "metadata/metadata_cache.h"
#include "utils/documentdb_errors.h"
#include "vector/vector_utilities.h"
#include "vector/vector_spec.h"
#include "utils/version_utils.h"
#include "query/bson_compare.h"

typedef struct
{
	pgbsonelement minElement;
	bool isMinInclusive;
	IndexClause *minClause;
	pgbsonelement maxElement;
	bool isMaxInclusive;
	IndexClause *maxClause;

	bool isInvalidCandidateForRange;
} DollarRangeElement;

typedef List *(*UpdateIndexList)(List *indexes,
								 ReplaceExtensionFunctionContext *context);
typedef bool (*MatchIndexPath)(IndexPath *path, void *state);
typedef bool (*ModifyTreeToUseAlternatePath)(PlannerInfo *root, RelOptInfo *rel,
											 ReplaceExtensionFunctionContext *context,
											 MatchIndexPath matchIndexPath);
typedef void (*NoIndexFoundHandler)(void);


/*
 * Force index pushdown operator support functions
 */
typedef struct
{
	/*
	 * Mongo query operator type
	 */
	ForceIndexOpType operator;

	/*
	 * Update the index list to filter out non-applicable
	 * indexes and then try creating index paths agains to
	 * push down to the now available index.
	 */
	UpdateIndexList updateIndexes;

	/*
	 * After a new set of paths are generated this function would
	 * be called to match if the path is what the operator expects it
	 * to be, usually the path is checked to be an index path and the operator
	 * specific quals are pushed to the index
	 */
	MatchIndexPath matchIndexPath;

	/*
	 * If updating index list doesn't help in creating any interesting index
	 * paths, then just ask the operator to do any necessary updates to the
	 * query tree and try any alternate path, this can be any path based on
	 * the query operator and should return true to notify that a valid
	 * path exist.
	 */
	ModifyTreeToUseAlternatePath alternatePath;

	/*
	 * Handler when no applicable index was found
	 */
	NoIndexFoundHandler noIndexHandler;
} ForceIndexSupportFuncs;


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static Expr * HandleSupportRequestCondition(SupportRequestIndexCondition *req);
static Path * ReplaceFunctionOperatorsInPlanPath(PlannerInfo *root, RelOptInfo *rel,
												 Path *path, PlanParentType parentType,
												 ReplaceExtensionFunctionContext *context);
static Expr * ProcessRestrictionInfoAndRewriteFuncExpr(Expr *clause,
													   ReplaceExtensionFunctionContext *
													   context, bool trimClauses);
static OpExpr * GetOpExprClauseFromIndexOperator(const MongoIndexOperatorInfo *operator,
												 List *args, bytea *indexOptions);

static void ExtractAndSetSearchParamterFromWrapFunction(IndexPath *indexPath,
														ReplaceExtensionFunctionContext *
														context);
static Path * OptimizeBitmapQualsForBitmapAnd(BitmapAndPath *path,
											  ReplaceExtensionFunctionContext *context);
static IndexPath * OptimizeIndexPathForFilters(IndexPath *indexPath,
											   ReplaceExtensionFunctionContext *context);
static Expr * OpExprForAggregationStageSupportFunction(Node *supportRequest);
static Path * FindIndexPathForQueryOperator(RelOptInfo *rel, List *pathList,
											ReplaceExtensionFunctionContext *context,
											MatchIndexPath matchIndexPath,
											void *matchContext);
static bool IsMatchingPathForQueryOperator(RelOptInfo *rel, Path *path,
										   ReplaceExtensionFunctionContext *context,
										   MatchIndexPath matchIndexPath,
										   void *matchContext);

/*-------------------------------*/
/* Force index support functions */
/*-------------------------------*/
static List * UpdateIndexListForGeonear(List *existingIndex,
										ReplaceExtensionFunctionContext *context);
static bool MatchIndexPathForGeonear(IndexPath *path, void *matchContext);
static bool TryUseAlternateIndexGeonear(PlannerInfo *root, RelOptInfo *rel,
										ReplaceExtensionFunctionContext *context,
										MatchIndexPath matchIndexPath);
static List * UpdateIndexListForText(List *existingIndex,
									 ReplaceExtensionFunctionContext *context);
static List * UpdateIndexListForVector(List *existingIndex,
									   ReplaceExtensionFunctionContext *context);
static bool MatchIndexPathForText(IndexPath *path, void *matchContext);
static bool MatchIndexPathForVector(IndexPath *path, void *matchContext);
static bool PushTextQueryToRuntime(PlannerInfo *root, RelOptInfo *rel,
								   ReplaceExtensionFunctionContext *context,
								   MatchIndexPath matchIndexPath);
static void ThrowNoTextIndexFound(void);
static void ThrowNoVectorIndexFound(void);

static bool MatchIndexPathEquals(IndexPath *path, void *matchContext);


static const ForceIndexSupportFuncs ForceIndexOperatorSupport[] =
{
	{
		.operator = ForceIndexOpType_GeoNear,
		.updateIndexes = &UpdateIndexListForGeonear,
		.matchIndexPath = &MatchIndexPathForGeonear,
		.alternatePath = &TryUseAlternateIndexGeonear,
		.noIndexHandler = &ThrowGeoNearUnableToFindIndex
	},
	{
		.operator = ForceIndexOpType_Text,
		.updateIndexes = &UpdateIndexListForText,
		.matchIndexPath = &MatchIndexPathForText,
		.noIndexHandler = &ThrowNoTextIndexFound,
		.alternatePath = &PushTextQueryToRuntime,
	},
	{
		.operator = ForceIndexOpType_VectorSearch,
		.updateIndexes = &UpdateIndexListForVector,
		.matchIndexPath = &MatchIndexPathForVector,
		.noIndexHandler = &ThrowNoVectorIndexFound,
	}
};

static const int ForceIndexOperatorsCount = sizeof(ForceIndexOperatorSupport) /
											sizeof(ForceIndexSupportFuncs);

extern bool EnableVectorForceIndexPushdown;
extern bool EnableIndexOperatorBounds;

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */
PG_FUNCTION_INFO_V1(dollar_support);
PG_FUNCTION_INFO_V1(bson_dollar_lookup_filter_support);
PG_FUNCTION_INFO_V1(bson_dollar_merge_filter_support);

/*
 * Handles the Support functions for the dollar logical operators.
 * Currently, this only supports the 'SupportRequestIndexCondition'
 * This basically takes a FuncExpr input that has a bson_dollar_<op>
 * and *iff* the index pointed to by the index matches the function,
 * returns the equivalent OpExpr for that function.
 * This means that this hook allows us to match each Qual directly against
 * an index (and each index column) independently, and push down each qual
 * directly against an index column custom matching against the index.
 * For more details see: https://www.postgresql.org/docs/current/xfunc-optimization.html
 * See also: https://github.com/postgres/postgres/blob/677a1dc0ca0f33220ba1ea8067181a72b4aff536/src/backend/optimizer/path/indxpath.c#L2329
 */
Datum
dollar_support(PG_FUNCTION_ARGS)
{
	Node *supportRequest = (Node *) PG_GETARG_POINTER(0);
	List *responseNodes = NIL;
	if (IsA(supportRequest, SupportRequestIndexCondition))
	{
		/* Try to convert operator/function call to index conditions */
		SupportRequestIndexCondition *req =
			(SupportRequestIndexCondition *) supportRequest;
		Expr *finalNode = HandleSupportRequestCondition(req);
		if (finalNode != NULL)
		{
			/* if we matched the condition to the index, then this function is not lossy -
			 * The operator is a perfect match for the function.
			 */
			req->lossy = false;
			responseNodes = lappend(responseNodes, finalNode);
		}
	}

	PG_RETURN_POINTER(responseNodes);
}


/*
 * Support function for index pushdown for $lookup join
 * filters. This is needed and can't use the regular index filters
 * since those use a Const value and require Const values to push down
 * to extract the index paths. So we use a 3rd argument which provides
 * the index path and use that to push down to the appropriate index.
 */
Datum
bson_dollar_lookup_filter_support(PG_FUNCTION_ARGS)
{
	Node *supportRequest = (Node *) PG_GETARG_POINTER(0);
	Expr *finalOpExpr = OpExprForAggregationStageSupportFunction(supportRequest);

	if (finalOpExpr)
	{
		PG_RETURN_POINTER(list_make1(finalOpExpr));
	}

	PG_RETURN_POINTER(NULL);
}


/*
 * Support function for index pushdown for $merge join
 * filters. This is needed and can't use the regular index filters
 * since those use a Const value and require Const values to push down
 * to extract the index paths. So we use a 3rd argument which provides
 * the index path and use that to push down to the appropriate index.
 */
Datum
bson_dollar_merge_filter_support(PG_FUNCTION_ARGS)
{
	Node *supportRequest = (Node *) PG_GETARG_POINTER(0);
	Expr *finalOpExpr = OpExprForAggregationStageSupportFunction(supportRequest);

	if (finalOpExpr)
	{
		PG_RETURN_POINTER(list_make1(finalOpExpr));
	}

	PG_RETURN_POINTER(NULL);
}


/**
 * This function creates an operator expression for support functions used in aggregation stages. These support functions enable the
 * pushdown of operations to the index. Regular support functions cannot be used because they require constants, while some aggregation
 * stages, such as $lookup and $merge, use variable expressions. To handle these cases, we need specialized support functions.
 *
 * Return opExpression for
 * $merge stage we create opExpr for $eq `@=` operator
 * $lookup stage we create opExpr for $in `@*=` operator
 */
static Expr *
OpExprForAggregationStageSupportFunction(Node *supportRequest)
{
	if (!IsA(supportRequest, SupportRequestIndexCondition))
	{
		return NULL;
	}

	SupportRequestIndexCondition *req = (SupportRequestIndexCondition *) supportRequest;

	if (!IsA(req->node, FuncExpr))
	{
		return NULL;
	}

	Oid operatorOid = -1;
	if (req->funcid == BsonDollarLookupJoinFilterFunctionOid())
	{
		operatorOid = BsonInMatchFunctionId();
	}
	else if (req->funcid == BsonDollarMergeJoinFunctionOid())
	{
		operatorOid = BsonEqualMatchIndexFunctionId();
	}
	else
	{
		return NULL;
	}

	FuncExpr *funcExpr = (FuncExpr *) req->node;
	if (list_length(funcExpr->args) != 3)
	{
		return NULL;
	}

	Node *thirdNode = lthird(funcExpr->args);
	if (!IsA(thirdNode, Const))
	{
		return NULL;
	}

	/* This is the lookup/merge join function. We can't use regular support functions
	 * since they need Consts and Lookup is an expression. So we use a 3rd arg for
	 * the index path.
	 */
	Const *thirdConst = (Const *) thirdNode;
	text *path = DatumGetTextPP(thirdConst->constvalue);

	StringView pathView = CreateStringViewFromText(path);
	const MongoIndexOperatorInfo *operator = GetMongoIndexOperatorInfoByPostgresFuncId(
		operatorOid);

	bytea *options = req->index->opclassoptions[req->indexcol];
	if (options == NULL)
	{
		return NULL;
	}

	if (!ValidateIndexForQualifierPathForDollarIn(options, &pathView))
	{
		return NULL;
	}

	OpExpr *finalExpression = GetOpExprClauseFromIndexOperator(operator,
															   funcExpr->args,
															   options);
	return (Expr *) finalExpression;
}


/*
 * Checks if an Expr is the expression
 * WHERE shard_key_value = 'collectionId'
 * and is an unsharded equality operator.
 */
inline static bool
IsOpExprShardKeyForUnshardedCollections(Expr *expr, uint64 collectionId)
{
	if (!IsA(expr, OpExpr))
	{
		return false;
	}

	OpExpr *opExpr = (OpExpr *) expr;
	Expr *firstArg = linitial(opExpr->args);
	Expr *secondArg = lsecond(opExpr->args);

	if (opExpr->opno != BigintEqualOperatorId())
	{
		return false;
	}

	if (!IsA(firstArg, Var) || !IsA(secondArg, Const))
	{
		return false;
	}

	Var *firstArgVar = (Var *) firstArg;
	Const *secondArgConst = (Const *) secondArg;
	return firstArgVar->varattno == DOCUMENT_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER &&
		   DatumGetInt64(secondArgConst->constvalue) == (int64) collectionId;
}


/*
 * Given a set of restriction paths (Qualifiers) built from the query plan,
 * Replaces any unresolved bson_dollar_<op> functions with the equivalent
 * OpExpr calls across the primary path relations that are built from the logical
 * plan.
 * Note that This is done before the best path and scan plan is decided.
 * We do this here because we introduce functions like
 * "bson_dollar_eq" in the parse phase.
 * In the early plan phase, the support function maps the eq function to the index
 * as an operator if possible. However, in the case of BitMapHeap scan paths, the FuncExpr
 * rels are considered ON TOP of the OpExpr rels and Postgres today does not do an EquivalenceClass
 * between OpExpr and FuncExpr of the same type. Consequently, what ends up happening is that there's
 * an index scan with a Recheck on the function value and matched documents are revalidated.
 * To prevent this, we rewrite any unresolved functions as OpExpr values. This meets Postgres's equivalence
 * checks and therefore gets removed from the 'qpquals' (runtime post-evaluation quals) for a bitmap scan.
 * Note that this is not something we see in IndexScans since IndexScans directly use the index paths we pass
 * in via the support functions. Only BitMap scans are impacted here for the qpqualifiers.
 * This also has the benefit of having unified views on Explain wtih opexpr being the mode to view operators.
 */
List *
ReplaceExtensionFunctionOperatorsInRestrictionPaths(List *restrictInfo,
													ReplaceExtensionFunctionContext *
													context)
{
	if (list_length(restrictInfo) < 1)
	{
		return restrictInfo;
	}

	ListCell *cell;
	foreach(cell, restrictInfo)
	{
		RestrictInfo *rinfo = lfirst_node(RestrictInfo, cell);
		if (context->inputData.isShardQuery &&
			context->inputData.collectionId > 0 &&
			IsOpExprShardKeyForUnshardedCollections(rinfo->clause,
													context->inputData.collectionId))
		{
			/* Simplify expression:
			 * On unsharded collections, we need the shard_key_value
			 * filter to route to the appropriate shard. However
			 * inside the shard, we know that the filter is always true
			 * so in this case, replace the shard_key_value filter with
			 * "TRUE" by removing it from the baserestrictinfo.
			 * We don't remove it from all paths and generation since we
			 * may need it for BTREE lookups with object_id filters.
			 */
			if (list_length(restrictInfo) == 1)
			{
				return NIL;
			}

			restrictInfo = foreach_delete_current(restrictInfo, cell);
			continue;
		}

		/* These paths don't have an index associated with it */
		bool trimClauses = true;
		Expr *expr = ProcessRestrictionInfoAndRewriteFuncExpr(rinfo->clause,
															  context, trimClauses);
		if (expr == NULL)
		{
			restrictInfo = foreach_delete_current(restrictInfo, cell);
			continue;
		}

		rinfo->clause = expr;
	}

	return restrictInfo;
}


/*
 * Given a List of Index Paths, walks the paths and substitutes any unresolved
 * and unreplaced bson_dollar_<op> functions with the equivalent OpExpr calls
 * across the various Index Path types (BitMap, IndexScan, SeqScan). This way
 * when the EXPLAIN output is read out, we see the @= operators instead of the
 * functions. This is primarily aesthetic for EXPLAIN output - but good to be
 * consistent.
 */
void
ReplaceExtensionFunctionOperatorsInPaths(PlannerInfo *root, RelOptInfo *rel,
										 List *pathsList, PlanParentType parentType,
										 ReplaceExtensionFunctionContext *context)
{
	if (list_length(pathsList) < 1)
	{
		return;
	}

	ListCell *cell;
	foreach(cell, pathsList)
	{
		Path *path = (Path *) lfirst(cell);
		lfirst(cell) = ReplaceFunctionOperatorsInPlanPath(root, rel, path, parentType,
														  context);
	}
}


/*
 * Returns true if the index is the primary key index for
 * the collections.
 */
bool
IsBtreePrimaryKeyIndex(IndexOptInfo *indexInfo)
{
	return indexInfo->relam == BTREE_AM_OID &&
		   indexInfo->nkeycolumns == 2 &&
		   indexInfo->unique &&
		   indexInfo->indexkeys[0] ==
		   DOCUMENT_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER &&
		   indexInfo->indexkeys[1] == DOCUMENT_DATA_TABLE_OBJECT_ID_VAR_ATTR_NUMBER;
}


/*
 * ForceIndexForQueryOperators ensures that the index path is available for a
 * query operator which requires a mandatory index, e.g ($geoNear, $text etc).
 *
 * Today we assume that only one such operator is used in a query, because we only try to
 * prioritize one index path, if the operator is not pushed to the index.
 *
 * Note: This function doesn't do any validation to make sure only one such operator is provided
 * in the query, so this should be done during the query construction.
 */
void
ForceIndexForQueryOperators(PlannerInfo *root, RelOptInfo *rel,
							ReplaceExtensionFunctionContext *context)
{
	if (context->forceIndexQueryOpData.type == ForceIndexOpType_None ||
		(context->forceIndexQueryOpData.type == ForceIndexOpType_GeoNear &&
		 !TryFindGeoNearOpExpr(root, context)))
	{
		/* If no special operator requirement or geonear with no geonear operator (other geo operators)
		 * then return */
		return;
	}

	/*
	 * First check if the query for special operator is pushed to index and there are multiple index paths, then
	 * discard other paths so that only the index path for the special operator is used.
	 */
	if (context->forceIndexQueryOpData.path != NULL)
	{
		if (list_length(rel->pathlist) == 1)
		{
			/* If there is only one index path, then return */
			return;
		}

		Path *matchingPath = FindIndexPathForQueryOperator(rel, rel->pathlist, context,
														   MatchIndexPathEquals,
														   context->forceIndexQueryOpData.
														   path);
		rel->partial_pathlist = NIL;
		rel->pathlist = list_make1(matchingPath);
		return;
	}

	ForceIndexSupportFuncs *forceIndexFuncs = NULL;
	for (int i = 0; i < ForceIndexOperatorsCount; i++)
	{
		if (ForceIndexOperatorSupport[i].operator == context->forceIndexQueryOpData.type)
		{
			forceIndexFuncs = (ForceIndexSupportFuncs *) &ForceIndexOperatorSupport[i];
		}
	}

	if (forceIndexFuncs == NULL)
	{
		/* No index support functions !!, can't do anything */
		return;
	}

	List *oldIndexList = rel->indexlist;
	List *oldPathList = rel->pathlist;
	List *oldPartialPathList = rel->partial_pathlist;

	/* Only consider the indexes that we want to push to based on the operator */
	List *newIndexList = forceIndexFuncs->updateIndexes(oldIndexList, context);

	/* Generate interesting index paths again with filtered indexes */
	rel->indexlist = newIndexList;
	rel->pathlist = NIL;
	rel->partial_pathlist = NIL;

	create_index_paths(root, rel);

	/* Check if index path was created for the operator based on matching criteria */
	Path *matchingPath = FindIndexPathForQueryOperator(rel, rel->pathlist,
													   context,
													   forceIndexFuncs->matchIndexPath,
													   context->forceIndexQueryOpData.
													   opExtraState);
	if (matchingPath == NULL)
	{
		/* We didn't find any index path for the query operators by just updating the
		 * indexlist, if the operator supports alternate index pushdown delegate to the
		 * operator otherwise its just a failure to find the index.
		 */
		bool alternatePathCreated = false;
		if (forceIndexFuncs->alternatePath != NULL)
		{
			alternatePathCreated =
				forceIndexFuncs->alternatePath(root, rel, context,
											   forceIndexFuncs->matchIndexPath);
		}

		if (!alternatePathCreated)
		{
			forceIndexFuncs->noIndexHandler();
		}
	}

	rel->indexlist = oldIndexList;
	if (rel->pathlist == NIL)
	{
		/* Just use the old pathlist if no new paths are added and there is no error
		 * because we want to continue with the query
		 */
		rel->pathlist = oldPathList;
		rel->partial_pathlist = oldPartialPathList;
	}

	/* Replace the func exprs to opExpr for consistency if new quals are added above */
	rel->baserestrictinfo =
		ReplaceExtensionFunctionOperatorsInRestrictionPaths(rel->baserestrictinfo,
															context);
}


/* --------------------------------------------------------- */
/* Private functions */
/* --------------------------------------------------------- */

/*
 * Inspects an input SupportRequestIndexCondition and associated FuncExpr
 * and validates whether it is satisfied by the index specified in the request.
 * If it is, then returns a new OpExpr for the condition.
 * Else, returns NULL;
 */
static Expr *
HandleSupportRequestCondition(SupportRequestIndexCondition *req)
{
	/* Input validation */

	List *args;
	const MongoIndexOperatorInfo *operator = GetMongoIndexQueryOperatorFromNode(req->node,
																				&args);

	if (list_length(args) != 2)
	{
		return NULL;
	}

	if (operator->indexStrategy == BSON_INDEX_STRATEGY_INVALID)
	{
		return NULL;
	}

	/*
	 *  TODO : Push down to index if operand is not a constant
	 */
	Node *operand = lsecond(args);
	if (!IsA(operand, Const))
	{
		return NULL;
	}

	/* Try to get the index options we serialized for the index.
	 * If one doesn't exist, we can't handle push downs of this clause */
	bytea *options = req->index->opclassoptions[req->indexcol];
	if (options == NULL)
	{
		return NULL;
	}

	Oid operatorFamily = req->index->opfamily[req->indexcol];

	Datum queryValue = ((Const *) operand)->constvalue;

	/* Lookup the func in the set of operators */
	if (operator->indexStrategy == BSON_INDEX_STRATEGY_DOLLAR_TEXT)
	{
		/* For text, we only match the operator family with the op family
		 * For the bson text.
		 */
		if (operatorFamily != BsonRumTextPathOperatorFamily())
		{
			return NULL;
		}

		Expr *finalExpression =
			(Expr *) GetOpExprClauseFromIndexOperator(operator, args, options);
		return finalExpression;
	}
	if (operator->indexStrategy != BSON_INDEX_STRATEGY_INVALID)
	{
		/* Check if the index is valid for the function */
		if (!ValidateIndexForQualifierValue(options, queryValue,
											operator->indexStrategy))
		{
			return NULL;
		}

		Expr *finalExpression =
			(Expr *) GetOpExprClauseFromIndexOperator(operator, args, options);
		return finalExpression;
	}

	return NULL;
}


/*
 * Extract search parameters from indexPath->indexinfo->indrestrictinfo, which contains a list of restriction clauses represents clause of WHERE or JOIN
 * set to context->queryDataForVectorSearch
 *
 * For vector search, it is of the following form.
 * ApiCatalogSchemaName.bson_search_param(document, '{ "nProbes": 4 }'::ApiCatalogSchemaName.bson)
 */
static void
ExtractAndSetSearchParamterFromWrapFunction(IndexPath *indexPath,
											ReplaceExtensionFunctionContext *context)
{
	List *quals = indexPath->indexinfo->indrestrictinfo;
	if (quals != NULL)
	{
		ListCell *cell;
		foreach(cell, quals)
		{
			RestrictInfo *rinfo = lfirst_node(RestrictInfo, cell);
			Expr *qual = rinfo->clause;
			if (IsA(qual, FuncExpr))
			{
				FuncExpr *expr = (FuncExpr *) qual;
				if (expr->funcid == ApiBsonSearchParamFunctionId())
				{
					Const *bsonConst = (Const *) lsecond(expr->args);
					context->queryDataForVectorSearch.SearchParamBson =
						bsonConst->constvalue;
					break;
				}
			}
		}
	}
}


static List *
OptimizeIndexExpressionsForRange(List *indexClauses)
{
	ListCell *indexPathCell;
	DollarRangeElement rangeElements[INDEX_MAX_KEYS];
	memset(&rangeElements, 0, sizeof(DollarRangeElement) * INDEX_MAX_KEYS);

	foreach(indexPathCell, indexClauses)
	{
		IndexClause *iclause = (IndexClause *) lfirst(indexPathCell);
		RestrictInfo *rinfo = iclause->rinfo;

		if (!IsA(rinfo->clause, OpExpr))
		{
			continue;
		}

		OpExpr *opExpr = (OpExpr *) rinfo->clause;
		const MongoIndexOperatorInfo *operator =
			GetMongoIndexOperatorByPostgresOperatorId(opExpr->opno);
		bool isComparisonInvalidIgnore = false;

		DollarRangeElement *element = &rangeElements[iclause->indexcol];

		if (element->isInvalidCandidateForRange)
		{
			continue;
		}

		switch (operator->indexStrategy)
		{
			case BSON_INDEX_STRATEGY_DOLLAR_GREATER:
			case BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL:
			{
				Const *argsConst = lsecond(opExpr->args);
				pgbson *secondArg = DatumGetPgBson(argsConst->constvalue);
				pgbsonelement argElement;
				PgbsonToSinglePgbsonElement(secondArg, &argElement);

				if (argElement.bsonValue.value_type == BSON_TYPE_NULL &&
					operator->indexStrategy == BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL)
				{
					/* $gte: null - skip range optimization (go through normal path)
					 * that skips ComparePartial and uses runtime recheck
					 */
					break;
				}

				if (argElement.bsonValue.value_type == BSON_TYPE_MINKEY &&
					operator->indexStrategy == BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL)
				{
					/* This is similar to $exists: true, skip optimization and rely on
					 * more efficient $exists: true check that doesn't need comparePartial.
					 * This is still okay since $lte/$lt starts with At least MinKey() so
					 * it doesn't change the bounds to be any better.
					 */
					break;
				}

				if (element->minElement.pathLength == 0)
				{
					element->minElement = argElement;
					element->isMinInclusive = operator->indexStrategy ==
											  BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL;
					element->minClause = iclause;
				}
				else if (element->minElement.pathLength != argElement.pathLength ||
						 strncmp(element->minElement.path, argElement.path,
								 argElement.pathLength) != 0)
				{
					element->isInvalidCandidateForRange = true;
				}
				else if (CompareBsonValueAndType(
							 &element->minElement.bsonValue, &argElement.bsonValue,
							 &isComparisonInvalidIgnore) < 0)
				{
					element->minElement = argElement;
					element->isMinInclusive = operator->indexStrategy ==
											  BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL;
					element->minClause = iclause;
				}

				break;
			}

			case BSON_INDEX_STRATEGY_DOLLAR_LESS:
			case BSON_INDEX_STRATEGY_DOLLAR_LESS_EQUAL:
			{
				Const *argsConst = lsecond(opExpr->args);
				pgbson *secondArg = DatumGetPgBson(argsConst->constvalue);
				pgbsonelement argElement;
				PgbsonToSinglePgbsonElement(secondArg, &argElement);

				if (argElement.bsonValue.value_type == BSON_TYPE_NULL &&
					operator->indexStrategy == BSON_INDEX_STRATEGY_DOLLAR_LESS_EQUAL)
				{
					/* $lte: null - skip range optimization (go through normal path)
					 * that skips ComparePartial and uses runtime recheck
					 */
					break;
				}

				if (element->maxElement.pathLength == 0)
				{
					element->maxElement = argElement;
					element->isMaxInclusive = operator->indexStrategy ==
											  BSON_INDEX_STRATEGY_DOLLAR_LESS_EQUAL;
					element->maxClause = iclause;
				}
				else if (element->maxElement.pathLength != argElement.pathLength ||
						 strncmp(element->maxElement.path, argElement.path,
								 argElement.pathLength) != 0)
				{
					element->isInvalidCandidateForRange = true;
				}
				else if (CompareBsonValueAndType(
							 &element->maxElement.bsonValue, &argElement.bsonValue,
							 &isComparisonInvalidIgnore) > 0)
				{
					element->maxElement = argElement;
					element->isMaxInclusive = operator->indexStrategy ==
											  BSON_INDEX_STRATEGY_DOLLAR_LESS_EQUAL;
					element->maxClause = iclause;
				}

				break;
			}

			default:
			{
				break;
			}
		}
	}

	for (int i = 0; i < INDEX_MAX_KEYS; i++)
	{
		if (rangeElements[i].isInvalidCandidateForRange)
		{
			continue;
		}

		if (rangeElements[i].minElement.bsonValue.value_type == BSON_TYPE_EOD ||
			rangeElements[i].maxElement.bsonValue.value_type == BSON_TYPE_EOD)
		{
			continue;
		}

		if (rangeElements[i].minElement.pathLength !=
			rangeElements[i].maxElement.pathLength ||
			strncmp(rangeElements[i].minElement.path, rangeElements[i].maxElement.path,
					rangeElements[i].minElement.pathLength) != 0)
		{
			continue;
		}

		OpExpr *expr = (OpExpr *) rangeElements[i].minClause->rinfo->clause;

		pgbson_writer clauseWriter;
		pgbson_writer childWriter;
		PgbsonWriterInit(&clauseWriter);
		PgbsonWriterStartDocument(&clauseWriter, rangeElements[i].minElement.path,
								  rangeElements[i].minElement.pathLength,
								  &childWriter);

		PgbsonWriterAppendValue(&childWriter, "min", 3,
								&rangeElements[i].minElement.bsonValue);
		PgbsonWriterAppendValue(&childWriter, "max", 3,
								&rangeElements[i].maxElement.bsonValue);
		PgbsonWriterAppendBool(&childWriter, "minInclusive", 12,
							   rangeElements[i].isMinInclusive);
		PgbsonWriterAppendBool(&childWriter, "maxInclusive", 12,
							   rangeElements[i].isMaxInclusive);
		PgbsonWriterEndDocument(&clauseWriter, &childWriter);


		Const *bsonConst = makeConst(BsonTypeId(), -1, InvalidOid, -1, PointerGetDatum(
										 PgbsonWriterGetPgbson(&clauseWriter)), false,
									 false);

		OpExpr *opExpr = (OpExpr *) make_opclause(BsonRangeMatchOperatorOid(), BOOLOID,
												  false,
												  linitial(expr->args),
												  (Expr *) bsonConst, InvalidOid,
												  InvalidOid);
		opExpr->opfuncid = BsonRangeMatchFunctionId();
		rangeElements[i].minClause->rinfo->clause = (Expr *) opExpr;
		rangeElements[i].minClause->indexquals = list_make1(
			rangeElements[i].minClause->rinfo);
		rangeElements[i].maxClause->rinfo->clause = (Expr *) opExpr;
		indexClauses = list_delete_ptr(indexClauses, rangeElements[i].maxClause);
	}

	return indexClauses;
}


/*
 * This function walks all the necessary qualifiers in a query Plan "Path"
 * Note that this currently replaces all the bson_dollar_<op> function calls
 * in the bitmapquals (which are used to display Recheck Conditions in EXPLAIN).
 * This way the Recheck conditions are consistent with the operator clauses pushed
 * to the index. This ensures that recheck conditions are also treated as equivalent
 * to the main index clauses. For more details see create_bitmap_scan_plan()
 */
static Path *
ReplaceFunctionOperatorsInPlanPath(PlannerInfo *root, RelOptInfo *rel, Path *path,
								   PlanParentType parentType,
								   ReplaceExtensionFunctionContext *context)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	if (IsA(path, BitmapOrPath))
	{
		BitmapOrPath *orPath = (BitmapOrPath *) path;
		ReplaceExtensionFunctionOperatorsInPaths(root, rel, orPath->bitmapquals,
												 PARENTTYPE_INVALID, context);
	}
	else if (IsA(path, BitmapAndPath))
	{
		BitmapAndPath *andPath = (BitmapAndPath *) path;
		ReplaceExtensionFunctionOperatorsInPaths(root, rel, andPath->bitmapquals,
												 PARENTTYPE_INVALID,
												 context);
		path = OptimizeBitmapQualsForBitmapAnd(andPath, context);
	}
	else if (IsA(path, BitmapHeapPath))
	{
		BitmapHeapPath *heapPath = (BitmapHeapPath *) path;
		heapPath->bitmapqual = ReplaceFunctionOperatorsInPlanPath(root, rel,
																  heapPath->bitmapqual,
																  PARENTTYPE_BITMAPHEAP,
																  context);
	}
	else if (IsA(path, IndexPath))
	{
		IndexPath *indexPath = (IndexPath *) path;

		/* Ignore primary key lookup paths parented in a bitmap scan:
		 * This can happen because a RUM index lookup can produce a 0 cost query as well
		 * and Postgres picks both and does a BitmapAnd - instead rely on a top level index path.
		 */
		if (IsBtreePrimaryKeyIndex(indexPath->indexinfo) &&
			list_length(indexPath->indexclauses) > 1 &&
			parentType != PARENTTYPE_INVALID)
		{
			context->primaryKeyLookupPath = indexPath;
		}

		const VectorIndexDefinition *vectorDefinition = NULL;
		if (indexPath->indexorderbys != NIL)
		{
			/* Only check for vector when there's an order by */
			vectorDefinition = GetVectorIndexDefinitionByIndexAmOid(
				indexPath->indexinfo->relam);
		}

		if (vectorDefinition != NULL)
		{
			context->hasVectorSearchQuery = true;
			context->queryDataForVectorSearch.VectorAccessMethodOid =
				indexPath->indexinfo->relam;

			/*
			 * For vector search, we also need to extract the search parameter from the wrap function.
			 * ApiCatalogSchemaName.bson_search_param(document, '{ "nProbes": 4 }'::ApiCatalogSchemaName.bson)
			 */
			ExtractAndSetSearchParamterFromWrapFunction(indexPath, context);

			if (EnableVectorForceIndexPushdown)
			{
				context->forceIndexQueryOpData.type = ForceIndexOpType_VectorSearch;
				context->forceIndexQueryOpData.path = indexPath;
			}
		}
		else
		{
			/* RUM/GIST indexes */
			ListCell *indexPathCell;
			foreach(indexPathCell, indexPath->indexclauses)
			{
				IndexClause *iclause = (IndexClause *) lfirst(indexPathCell);
				RestrictInfo *rinfo = iclause->rinfo;
				bytea *options = NULL;
				if (indexPath->indexinfo->opclassoptions != NULL)
				{
					options = indexPath->indexinfo->opclassoptions[iclause->indexcol];
				}

				/* Specific to text indexes: If the OpFamily is for Text, update the context
				 * with the index options for text. This is used later to process restriction info
				 * so that we can push down the TSQuery with the appropriate default language settings.
				 */
				if (indexPath->indexinfo->opfamily[iclause->indexcol] ==
					BsonRumTextPathOperatorFamily())
				{
					/* If there's no options, set it. Otherwise, fail with "too many paths" */
					QueryTextIndexData *textIndexData =
						context->forceIndexQueryOpData.opExtraState;
					if (textIndexData != NULL)
					{
						ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
										errmsg("Too many text expressions")));
					}
					context->forceIndexQueryOpData.type = ForceIndexOpType_Text;
					context->forceIndexQueryOpData.path = indexPath;
					textIndexData = palloc0(sizeof(QueryTextIndexData));
					textIndexData->indexOptions = options;
					context->forceIndexQueryOpData.opExtraState = (void *) textIndexData;

					ReplaceExtensionFunctionContext childContext = {
						{ 0 }, false, false, context->inputData,
						{ .path = NULL, .type = ForceIndexOpType_None, .opExtraState =
							  NULL }
					};
					childContext.forceIndexQueryOpData = context->forceIndexQueryOpData;
					bool trimClauses = false;
					rinfo->clause = ProcessRestrictionInfoAndRewriteFuncExpr(
						rinfo->clause,
						&childContext, trimClauses);
				}
				else
				{
					ReplaceExtensionFunctionContext childContext = {
						{ 0 }, false, false, context->inputData,
						{ .path = NULL, .type = ForceIndexOpType_None, .opExtraState =
							  NULL }
					};
					bool trimClauses = false;
					rinfo->clause = ProcessRestrictionInfoAndRewriteFuncExpr(
						rinfo->clause,
						&childContext, trimClauses);
				}
			}

			if (indexPath->indexinfo->relam == GIST_AM_OID &&
				list_length(indexPath->indexorderbys) == 1)
			{
				/* Specific to geonear: Check if the geonear query is pushed to index */
				Expr *orderByExpr = linitial(indexPath->indexorderbys);
				if (IsA(orderByExpr, OpExpr) && ((OpExpr *) orderByExpr)->opno ==
					BsonGeonearDistanceOperatorId())
				{
					context->forceIndexQueryOpData.type = ForceIndexOpType_GeoNear;
					context->forceIndexQueryOpData.path = indexPath;
				}
			}

			if (indexPath->indexinfo->relam == RumIndexAmId())
			{
				indexPath->indexclauses = OptimizeIndexExpressionsForRange(
					indexPath->indexclauses);
			}
		}

		indexPath = OptimizeIndexPathForFilters(indexPath, context);
	}

	return path;
}


/* Given an expression object, rewrites the function as an equivalent
 * OpExpr. If it's a Bool Expr (AND, NOT, OR) evaluates the inner FuncExpr
 * and replaces them with the OpExpr equivalents.
 */
Expr *
ProcessRestrictionInfoAndRewriteFuncExpr(Expr *clause,
										 ReplaceExtensionFunctionContext *context,
										 bool trimClauses)
{
	CHECK_FOR_INTERRUPTS();
	check_stack_depth();

	/* These are unresolved functions from the index planning */
	if (IsA(clause, FuncExpr) || IsA(clause, OpExpr))
	{
		List *args;
		const MongoIndexOperatorInfo *operator = GetMongoIndexQueryOperatorFromNode(
			(Node *) clause, &args);
		if (operator->indexStrategy == BSON_INDEX_STRATEGY_DOLLAR_TEXT)
		{
			/*
			 * For text indexes, we inject a noop filter that does nothing, but tracks
			 * the serialization details of the index. This is then later used in $meta
			 * queries to get the rank
			 */
			context->forceIndexQueryOpData.type = ForceIndexOpType_Text;
			QueryTextIndexData *textIndexData =
				(QueryTextIndexData *) context->forceIndexQueryOpData.opExtraState;

			if (textIndexData != NULL && textIndexData->indexOptions != NULL)
			{
				/* TODO: Make TextIndex force use the index path if available
				 * Today this isn't guaranteed if there's another path picked
				 * e.g. ORDER BY object_id.
				 */
				context->inputData.isRuntimeTextScan = true;
				OpExpr *expr = GetOpExprClauseFromIndexOperator(operator, args,
																textIndexData->
																indexOptions);
				Expr *finalExpr = (Expr *) GetFuncExprForTextWithIndexOptions(
					expr->args, textIndexData->indexOptions,
					context->inputData.isRuntimeTextScan,
					textIndexData);
				if (finalExpr != NULL)
				{
					return finalExpr;
				}
			}
		}
		else if (operator->indexStrategy != BSON_INDEX_STRATEGY_INVALID)
		{
			return (Expr *) GetOpExprClauseFromIndexOperator(operator, args,
															 NULL);
		}
	}
	else if (IsA(clause, NullTest))
	{
		NullTest *nullTest = (NullTest *) clause;
		if (context->forceIndexQueryOpData.type == ForceIndexOpType_None &&
			nullTest->nulltesttype == IS_NOT_NULL &&
			IsA(nullTest->arg, FuncExpr))
		{
			Oid functionOid = ((FuncExpr *) nullTest->arg)->funcid;
			if (functionOid == BsonValidateGeographyFunctionId() ||
				functionOid == BsonValidateGeometryFunctionId())
			{
				/*
				 * The query contains a geospatial operator, now assume that it is a potential
				 * geonear query as well, because today for few instances we can't uniquely identify
				 * if the query is a geonear query.
				 *
				 * e.g. Sharded collections cases where ORDER BY is not pushed to the shards so we only
				 * get the PFE of geospatial operators.
				 */
				context->forceIndexQueryOpData.type = ForceIndexOpType_GeoNear;
			}
		}
	}
	else if (IsA(clause, ScalarArrayOpExpr))
	{
		if (EnableIndexOperatorBounds &&
			IsClusterVersionAtleast(DocDB_V0, 102, 0) &&
			context->inputData.isShardQuery && trimClauses)
		{
			ScalarArrayOpExpr *arrayOpExpr = (ScalarArrayOpExpr *) clause;
			if (arrayOpExpr->opno == BsonIndexBoundsEqualOperatorId())
			{
				/* These are only used for index selectivity - trim it here */
				return NULL;
			}
		}
	}
	else if (IsA(clause, BoolExpr))
	{
		BoolExpr *boolExpr = (BoolExpr *) clause;
		List *processedBoolArgs = NIL;
		ListCell *boolArgsCell;

		/* Evaluate args of the Boolean expression for FuncExprs */
		foreach(boolArgsCell, boolExpr->args)
		{
			Expr *innerExpr = (Expr *) lfirst(boolArgsCell);
			Expr *processedExpr = ProcessRestrictionInfoAndRewriteFuncExpr(
				innerExpr, context, trimClauses);
			if (processedExpr != NULL)
			{
				processedBoolArgs = lappend(processedBoolArgs,
											processedExpr);
			}
		}

		if (list_length(processedBoolArgs) == 0)
		{
			return NULL;
		}
		else if (list_length(processedBoolArgs) == 1 &&
				 boolExpr->boolop != NOT_EXPR)
		{
			/* If there's only one argument for $and/$or, return it */
			return (Expr *) linitial(processedBoolArgs);
		}

		boolExpr->args = processedBoolArgs;
	}

	return clause;
}


/*
 * Given a Mongo Index operator and a FuncExpr/OpExpr args that were constructed in the
 * query planner, along with the index options for an index, constructs an opExpr that is
 * appropriate for that index.
 * For regular operators this means converting to an operator that is used by that index
 * For TEXT this uses the language and weights that are in the index options to generate an
 * appropriate TSQuery.
 */
static OpExpr *
GetOpExprClauseFromIndexOperator(const MongoIndexOperatorInfo *operator, List *args,
								 bytea *indexOptions)
{
	/* the index is valid for this qualifier - convert to opexpr */
	Oid operatorId = GetMongoQueryOperatorOid(operator);
	if (!OidIsValid(operatorId))
	{
		ereport(ERROR, (errmsg("<bson> %s <bson> operator not defined",
							   operator->postgresOperatorName)));
	}

	if (operator->indexStrategy == BSON_INDEX_STRATEGY_DOLLAR_TEXT)
	{
		/* for $text, we convert the input query into a 'tsvector' @@ 'tsquery' */
		Node *firstArg = (Node *) linitial(args);
		Node *bsonOperand = (Node *) lsecond(args);

		if (!IsA(bsonOperand, Const))
		{
			ereport(ERROR, (errmsg("Expecting a constant value for the text query")));
		}

		Const *operand = (Const *) bsonOperand;

		Assert(operand->consttype == BsonTypeId());
		pgbson *bsonValue = DatumGetPgBson(operand->constvalue);
		pgbsonelement element;
		PgbsonToSinglePgbsonElement(bsonValue, &element);

		Datum result = BsonTextGenerateTSQuery(&element.bsonValue, indexOptions);
		operand = makeConst(TSQUERYOID, -1, InvalidOid, -1, result,
							false, false);
		return (OpExpr *) make_opclause(operatorId, BOOLOID, false,
										(Expr *) firstArg,
										(Expr *) operand, InvalidOid, InvalidOid);
	}
	else
	{
		/* construct document <operator> <value> expression */
		Node *firstArg = (Node *) linitial(args);
		Node *operand = (Node *) lsecond(args);

		Expr *operandExpr;
		if (IsA(operand, Const))
		{
			Const *constOp = (Const *) operand;
			constOp = copyObject(constOp);
			constOp->consttype = BsonTypeId();
			operandExpr = (Expr *) constOp;
		}
		else if (IsA(operand, Var))
		{
			Var *varOp = (Var *) operand;
			varOp = copyObject(varOp);
			varOp->vartype = BsonTypeId();
			operandExpr = (Expr *) varOp;
		}
		else if (IsA(operand, Param))
		{
			Param *paramOp = (Param *) operand;
			paramOp = copyObject(paramOp);
			paramOp->paramtype = BsonTypeId();
			operandExpr = (Expr *) paramOp;
		}
		else
		{
			operandExpr = (Expr *) operand;
		}

		return (OpExpr *) make_opclause(operatorId, BOOLOID, false,
										(Expr *) firstArg,
										operandExpr, InvalidOid, InvalidOid);
	}
}


/*
 * In the scenario where we have a BitmapAnd of [ A AND B ]
 * if any of the nested IndexPaths are for shard_key_value = 'collid'
 * if this is true, then it's for an unsharded collection so we should remove
 * this qual.
 */
static Path *
OptimizeBitmapQualsForBitmapAnd(BitmapAndPath *andPath,
								ReplaceExtensionFunctionContext *context)
{
	if (!context->inputData.isShardQuery ||
		context->inputData.collectionId == 0)
	{
		return (Path *) andPath;
	}

	ListCell *cell;
	foreach(cell, andPath->bitmapquals)
	{
		Path *path = (Path *) lfirst(cell);
		if (IsA(path, IndexPath))
		{
			IndexPath *indexPath = (IndexPath *) path;

			if (indexPath->indexinfo->relam != BTREE_AM_OID ||
				list_length(indexPath->indexclauses) != 1)
			{
				/* Skip any non Btree and cases where there are more index
				 * clauses.
				 */
				continue;
			}

			IndexClause *clause = linitial(indexPath->indexclauses);
			if (clause->indexcol == 0 &&
				IsOpExprShardKeyForUnshardedCollections(clause->rinfo->clause,
														context->inputData.collectionId))
			{
				/* The index path is a single restrict info on the shard_key_value = 'collectionid'
				 * This index path can be removed.
				 */
				andPath->bitmapquals = foreach_delete_current(andPath->bitmapquals, cell);
			}
		}
	}

	if (list_length(andPath->bitmapquals) == 1)
	{
		return (Path *) linitial(andPath->bitmapquals);
	}

	return (Path *) andPath;
}


static IndexPath *
OptimizeIndexPathForFilters(IndexPath *indexPath,
							ReplaceExtensionFunctionContext *context)
{
	/* For cases of partial filter expressions the base restrict info is "copied" into the index exprs
	 * so in this case we need to do the restrictinfo changes here too.
	 * see check_index_predicates on indxpath.c.
	 */
	if (indexPath->indexinfo->indpred == NIL)
	{
		return indexPath;
	}

	indexPath->indexinfo->indrestrictinfo =
		ReplaceExtensionFunctionOperatorsInRestrictionPaths(
			indexPath->indexinfo->indrestrictinfo, context);
	return indexPath;
}


/*
 * There maybe index paths created if any other applicable index is found
 * cheaper than the geospatial indexes. For geonear force index pushdown
 * we only consider all the geospatial indexes
 */
static List *
UpdateIndexListForGeonear(List *existingIndex,
						  ReplaceExtensionFunctionContext *context)
{
	List *newIndexesListForGeonear = NIL;
	ListCell *indexCell;
	foreach(indexCell, existingIndex)
	{
		IndexOptInfo *index = lfirst_node(IndexOptInfo, indexCell);
		if (index->relam == GIST_AM_OID && index->ncolumns > 0 &&
			(index->opfamily[0] == BsonGistGeographyOperatorFamily() ||
			 index->opfamily[0] == BsonGistGeometryOperatorFamily()))
		{
			newIndexesListForGeonear = lappend(newIndexesListForGeonear, index);
		}
	}
	return newIndexesListForGeonear;
}


/*
 * Pushed the text index query to runtime with index options if
 * no index path can be created
 */
static bool
PushTextQueryToRuntime(PlannerInfo *root, RelOptInfo *rel,
					   ReplaceExtensionFunctionContext *context,
					   MatchIndexPath matchIndexPath)
{
	QueryTextIndexData *textIndexData =
		(QueryTextIndexData *) context->forceIndexQueryOpData.opExtraState;
	if (textIndexData != NULL && textIndexData->indexOptions != NULL)
	{
		context->inputData.isRuntimeTextScan = true;
		return true;
	}
	return false;
}


/*
 * This method checks if the geonear query is eligible for using an alternate
 * index based on the type of query and then creates the index path for with
 * updated index quals again
 */
static bool
TryUseAlternateIndexGeonear(PlannerInfo *root, RelOptInfo *rel,
							ReplaceExtensionFunctionContext *context,
							MatchIndexPath matchIndexPath)
{
	OpExpr *geoNearOpExpr = (OpExpr *) context->forceIndexQueryOpData.opExtraState;
	if (geoNearOpExpr == NULL)
	{
		return false;
	}

	GeonearRequest *request;
	List *_2dIndexList = NIL;
	List *_2dsphereIndexList = NIL;
	GetAllGeoIndexesFromRelIndexList(rel->indexlist, &_2dIndexList,
									 &_2dsphereIndexList);

	if (CanGeonearQueryUseAlternateIndex(geoNearOpExpr, &request))
	{
		char *keyToUse = request->key;
		bool useSphericalIndex = true;
		bool isEmptyKey = strlen(request->key) == 0;
		if (isEmptyKey)
		{
			keyToUse =
				CheckGeonearEmptyKeyCanUseIndex(request, _2dIndexList,
												_2dsphereIndexList,
												&useSphericalIndex);
		}
		UpdateGeoNearQueryTreeToUseAlternateIndex(root, rel, geoNearOpExpr, keyToUse,
												  useSphericalIndex, isEmptyKey);
	}
	else
	{
		/* No index pushdown possible for geonear just error out */
		ThrowGeoNearUnableToFindIndex();
	}

	/* Because we have updated the quals to make use of index which could not be considered
	 * earlier as the indpred don't match and the sort_pathkeys are different, so we need
	 * to make sure that the sort_pathkey are constructed and index predicates are validated with the new quals.
	 */
	root->sort_pathkeys = make_pathkeys_for_sortclauses(root,
														root->parse->sortClause,
														root->parse->targetList);

	/*
	 * Make the query_pathkeys same as sort_pathkeys because we are only intereseted in making
	 * the index path for the geonear sort clause.
	 *
	 * create_index_paths will use the query_pathkeys to match the index with order by clause
	 * and generate the index path
	 */
	root->query_pathkeys = root->sort_pathkeys;

	/* `check_index_predicates` will set the indpred for indexes based on new quals and also
	 * sets indrestrictinfo which is all the quals less the ones that are implicitly implied by the index predicate.
	 * So for creating this we need to used the original restrictinfo list,
	 * we can safely use that because we updated the quals in place.
	 */
	check_index_predicates(root, rel);

	/* Try to create the index paths again with only the quals needed
	 * so that all the other indexes are ignored.
	 */
	rel->pathlist = NIL;
	rel->partial_pathlist = NIL;

	create_index_paths(root, rel);

	Path *matchedPath =
		FindIndexPathForQueryOperator(rel, rel->pathlist,
									  context, matchIndexPath,
									  context->forceIndexQueryOpData.opExtraState);
	if (matchedPath != NULL)
	{
		/* Discard any other path */
		rel->pathlist = list_make1(matchedPath);
		ReplaceExtensionFunctionOperatorsInPaths(root, rel, rel->pathlist,
												 PARENTTYPE_NONE, context);
		return true;
	}
	return false;
}


/*
 * We need to use all the available indexes for text queries as
 * these can be used in OR clauses. And BitmapOrPath requires
 * the indexes in all the OR arms to be present otherwise it can't
 * create a BitmapOrPath.
 * e.g. {$or [{$text: ..., a: 2}, {other: 1}]}. This needs to have
 * an index on `other` so that this text query can be pushed to the index.
 *
 * more info at generate_bitmap_or_paths
 */
static List *
UpdateIndexListForText(List *existingIndex, ReplaceExtensionFunctionContext *context)
{
	ListCell *indexCell;
	bool isValidTextIndexFound = false;
	foreach(indexCell, existingIndex)
	{
		IndexOptInfo *index = lfirst_node(IndexOptInfo, indexCell);
		if (index->relam == RumIndexAmId() && index->nkeycolumns > 0)
		{
			for (int i = 0; i < index->nkeycolumns; i++)
			{
				if (index->opfamily[i] == BsonRumTextPathOperatorFamily())
				{
					isValidTextIndexFound = true;
					QueryTextIndexData *textIndexData =
						(QueryTextIndexData *) context->forceIndexQueryOpData.opExtraState;
					if (textIndexData == NULL)
					{
						textIndexData = palloc0(sizeof(QueryTextIndexData));
						context->forceIndexQueryOpData.opExtraState =
							(void *) textIndexData;
					}
					textIndexData->indexOptions = index->opclassoptions[i];

					break;
				}
			}
		}
	}

	if (!isValidTextIndexFound)
	{
		ThrowNoTextIndexFound();
	}

	return existingIndex;
}


/*
 * This today checks BitmapHeapPath, BitmapOrPath, BitmapAndPath and IndexPath
 * and returns true if it has an index path which matches the
 * query operator based on `matchIndexPath` function.
 */
static bool
IsMatchingPathForQueryOperator(RelOptInfo *rel, Path *path,
							   ReplaceExtensionFunctionContext *context,
							   MatchIndexPath matchIndexPath,
							   void *matchContext)
{
	CHECK_FOR_INTERRUPTS();
	check_stack_depth();

	if (IsA(path, BitmapHeapPath))
	{
		BitmapHeapPath *bitmapHeapPath = (BitmapHeapPath *) path;
		return IsMatchingPathForQueryOperator(rel, bitmapHeapPath->bitmapqual,
											  context, matchIndexPath, matchContext);
	}
	else if (IsA(path, BitmapOrPath))
	{
		BitmapOrPath *bitmapOrPath = (BitmapOrPath *) path;
		if (FindIndexPathForQueryOperator(rel, bitmapOrPath->bitmapquals, context,
										  matchIndexPath, matchContext) != NULL)
		{
			return true;
		}
		return false;
	}
	else if (IsA(path, BitmapAndPath))
	{
		BitmapAndPath *bitmapAndPath = (BitmapAndPath *) path;
		if (FindIndexPathForQueryOperator(rel, bitmapAndPath->bitmapquals, context,
										  matchIndexPath, matchContext) != NULL)
		{
			return true;
		}
		return false;
	}
	else if (IsA(path, IndexPath))
	{
		IndexPath *indexPath = (IndexPath *) path;
		if (matchIndexPath(indexPath, matchContext))
		{
			return true;
		}
		return false;
	}
	return false;
}


/*
 * Checks the newly constructed pathlist to see if the query operator that needs index are
 * pushed to the right index and returns the topLevel path which includes the indexpath for
 * the operator
 *
 * Returns a NULL path in case no index path was found
 */
static Path *
FindIndexPathForQueryOperator(RelOptInfo *rel, List *pathList,
							  ReplaceExtensionFunctionContext *context,
							  MatchIndexPath matchIndexPath,
							  void *matchContext)
{
	CHECK_FOR_INTERRUPTS();
	check_stack_depth();

	if (list_length(pathList) == 0)
	{
		return NULL;
	}
	ListCell *cell;
	foreach(cell, pathList)
	{
		Path *path = (Path *) lfirst(cell);
		if (IsMatchingPathForQueryOperator(rel, path, context, matchIndexPath,
										   matchContext))
		{
			return path;
		}
	}
	return NULL;
}


/*
 * Matches the index path for $geoNear query and checks if the index path
 * has a predicate which equals to geonear operator left side arguments which
 * is basically the predicate qual to match to the index
 */
static bool
MatchIndexPathForGeonear(IndexPath *indexPath, void *matchContext)
{
	if (indexPath->indexinfo->relam == GIST_AM_OID &&
		indexPath->indexinfo->nkeycolumns > 0 &&
		(indexPath->indexinfo->opfamily[0] == BsonGistGeographyOperatorFamily() ||
		 indexPath->indexinfo->opfamily[0] == BsonGistGeometryOperatorFamily()))
	{
		OpExpr *geoNearOpExpr = (OpExpr *) matchContext;
		if (geoNearOpExpr == NULL)
		{
			return false;
		}

		if (equal(linitial(geoNearOpExpr->args),
				  linitial(indexPath->indexinfo->indexprs)))
		{
			return true;
		}
	}
	return false;
}


/*
 * This function just performs a pointer equality for two index
 * paths provided
 */
static bool
MatchIndexPathEquals(IndexPath *path, void *matchContext)
{
	Node *matchedIndexPath = (Node *) matchContext;

	if (!IsA(matchedIndexPath, IndexPath))
	{
		return false;
	}

	return path == (IndexPath *) matchedIndexPath;
}


/*
 * Matches the indexPath for $text query. It just checks if the index used
 * is a text index, as there can only be at max one text index for a collection.
 */
static bool
MatchIndexPathForText(IndexPath *indexPath, void *matchContext)
{
	if (indexPath->indexinfo->relam == RumIndexAmId() &&
		indexPath->indexinfo->ncolumns > 0)
	{
		for (int ind = 0; ind < indexPath->indexinfo->ncolumns; ind++)
		{
			if (indexPath->indexinfo->opfamily[ind] == BsonRumTextPathOperatorFamily())
			{
				return true;
			}
		}
	}
	return false;
}


pg_attribute_noreturn()
static void
ThrowNoTextIndexFound()
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INDEXNOTFOUND),
					errmsg("text index required for $text query")));
}


static void
ThrowNoVectorIndexFound(void)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INDEXNOTFOUND),
					errmsg("vector index required for $search query during pushdown")));
}


static bool
MatchIndexPathForVector(IndexPath *indexPath, void *matchContext)
{
	const VectorIndexDefinition *def = GetVectorIndexDefinitionByIndexAmOid(
		indexPath->indexinfo->relam);
	return def != NULL;
}


static List *
UpdateIndexListForVector(List *existingIndex,
						 ReplaceExtensionFunctionContext *context)
{
	/* Trim all indexes except vector indexes for the purposes of planning */
	List *newIndexesListForVector = NIL;
	ListCell *indexCell;
	foreach(indexCell, existingIndex)
	{
		IndexOptInfo *index = lfirst_node(IndexOptInfo, indexCell);
		const VectorIndexDefinition *def = GetVectorIndexDefinitionByIndexAmOid(
			index->relam);
		if (def != NULL)
		{
			newIndexesListForVector = lappend(newIndexesListForVector, index);
		}
	}
	return newIndexesListForVector;
}
