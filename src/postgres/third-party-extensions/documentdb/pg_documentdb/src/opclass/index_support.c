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
#include <nodes/nodeFuncs.h>
#include <catalog/pg_am.h>
#include <optimizer/paths.h>
#include <optimizer/pathnode.h>
#include "nodes/pg_list.h"
#include <pg_config_manual.h>
#include <utils/lsyscache.h>
#include <optimizer/restrictinfo.h>
#include <optimizer/cost.h>
#include <access/genam.h>
#include <utils/index_selfuncs.h>

#include "metadata/index.h"
#include "query/query_operator.h"
#include "opclass/bson_gin_index_types_core.h"
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
#include "index_am/index_am_utils.h"
#include "query/bson_dollar_selectivity.h"
#include "planner/documentdb_planner.h"
#include "aggregation/bson_query_common.h"

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

typedef struct
{
	Expr *documentExpr;
	const char *documentDBIndexName;
	bool isSparse;
} IndexHintMatchContext;

typedef struct
{
	bson_value_t value;
	RestrictInfo *restrictInfo;
} RuntimePrimaryKeyRestrictionData;

typedef struct
{
	RestrictInfo *shardKeyQualExpr;
	struct
	{
		bson_value_t equalityBsonValue;
		RestrictInfo *restrictInfo;
		bool isPrimaryKeyEquality;
	} objectId;


	/* Found paths */
	IndexPath *primaryKeyLookupPath;

	/* Runtime expression checks for $eq
	 * List of RuntimePrimaryKeyRestrictionData
	 */
	List *runtimeEqualityRestrictionData;

	/* Runtime expression checks for $in
	 * List of RuntimePrimaryKeyRestrictionData
	 */
	List *runtimeDollarInRestrictionData;
} PrimaryKeyLookupContext;

typedef List *(*UpdateIndexList)(List *indexes,
								 ReplaceExtensionFunctionContext *context);
typedef bool (*MatchIndexPath)(IndexPath *path, void *state);
typedef bool (*ModifyTreeToUseAlternatePath)(PlannerInfo *root, RelOptInfo *rel,
											 ReplaceExtensionFunctionContext *context,
											 MatchIndexPath matchIndexPath);
typedef void (*NoIndexFoundHandler)(void);
typedef bool (*EnableForceIndexPushdown)(PlannerInfo *root,
										 ReplaceExtensionFunctionContext *context);

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
	 * Control switch to enable/disbale the force index pushdown
	 */
	EnableForceIndexPushdown enableForceIndexPushdown;

	/*
	 * Handler when no applicable index was found
	 */
	NoIndexFoundHandler noIndexHandler;
} ForceIndexSupportFuncs;


typedef struct IndexElemmatchState
{
	pgbson_array_writer *stateWriter;

	const char *indexPath;
	uint32_t indexPathLength;
	bool isTopLevel;
} IndexElemmatchState;


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
static Expr * ProcessFullScanForOrderBy(SupportRequestIndexCondition *req, List *args);
static OpExpr * CreateFullScanOpExpr(Expr *documentExpr, const char *sourcePath, uint32_t
									 sourcePathLength, int32_t orderByScanDirection);
static OpExpr * CreateExistsTrueOpExpr(Expr *documentExpr, const char *sourcePath,
									   uint32_t sourcePathLength);
static List * GetSortDetails(PlannerInfo *root, Index rti,
							 bool *hasOrderBy, bool *hasGroupby, bool *isOrderById);
static bool IsValidIndexPathForIdOrderBy(IndexPath *indexPath, List *sortDetails);
static bool IsValidForIndexOnlyScans(PlannerInfo *root);

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
static bool EnableGeoNearForceIndexPushdown(PlannerInfo *root,
											ReplaceExtensionFunctionContext *context);
static bool DefaultTrueForceIndexPushdown(PlannerInfo *root,
										  ReplaceExtensionFunctionContext *context);
static bool DefaultFalseForceIndexPushdown(PlannerInfo *root,
										   ReplaceExtensionFunctionContext *context);
static Expr * ProcessElemMatchOperator(bytea *options, Datum queryValue, const
									   MongoIndexOperatorInfo *operator, List *args);

static List * UpdateIndexListForIndexHint(List *existingIndex,
										  ReplaceExtensionFunctionContext *context);
static bool MatchIndexPathForIndexHint(IndexPath *path, void *matchContext);
static bool TryUseAlternateIndexForIndexHint(PlannerInfo *root, RelOptInfo *rel,
											 ReplaceExtensionFunctionContext *context,
											 MatchIndexPath matchIndexPath);
static bool EnableIndexHintForceIndexPushdown(PlannerInfo *root,
											  ReplaceExtensionFunctionContext *context);
static void ThrowIndexHintUnableToFindIndex(void);

static List * UpdateIndexListForPrimaryKeyLookup(List *existingIndex,
												 ReplaceExtensionFunctionContext *context);
static bool MatchIndexPathForPrimaryKeyLookup(IndexPath *path, void *matchContext);
static bool TryUseAlternateIndexForPrimaryKeyLookup(PlannerInfo *root, RelOptInfo *rel,
													ReplaceExtensionFunctionContext *
													context,
													MatchIndexPath matchIndexPath);
static void PrimaryKeyLookupUnableToFindIndex(void);
static bool IsBsonRangeArgsForFullScan(List *args);

static const ForceIndexSupportFuncs ForceIndexOperatorSupport[] =
{
	[ForceIndexOpType_None] = {
		.operator = ForceIndexOpType_None,
		.updateIndexes = NULL,
		.matchIndexPath = &MatchIndexPathEquals,
		.alternatePath = NULL,
		.noIndexHandler = NULL,
		.enableForceIndexPushdown = &DefaultFalseForceIndexPushdown
	},
	[ForceIndexOpType_Text] = {
		.operator = ForceIndexOpType_Text,
		.updateIndexes = &UpdateIndexListForText,
		.matchIndexPath = &MatchIndexPathForText,
		.noIndexHandler = &ThrowNoTextIndexFound,
		.alternatePath = &PushTextQueryToRuntime,
		.enableForceIndexPushdown = &DefaultTrueForceIndexPushdown
	},
	[ForceIndexOpType_GeoNear] = {
		.operator = ForceIndexOpType_GeoNear,
		.updateIndexes = &UpdateIndexListForGeonear,
		.matchIndexPath = &MatchIndexPathForGeonear,
		.alternatePath = &TryUseAlternateIndexGeonear,
		.noIndexHandler = &ThrowGeoNearUnableToFindIndex,
		.enableForceIndexPushdown = &EnableGeoNearForceIndexPushdown
	},
	[ForceIndexOpType_VectorSearch] = {
		.operator = ForceIndexOpType_VectorSearch,
		.updateIndexes = &UpdateIndexListForVector,
		.matchIndexPath = &MatchIndexPathForVector,
		.noIndexHandler = &ThrowNoVectorIndexFound,
		.enableForceIndexPushdown = &DefaultTrueForceIndexPushdown
	},
	[ForceIndexOpType_IndexHint] = {
		.operator = ForceIndexOpType_IndexHint,
		.updateIndexes = &UpdateIndexListForIndexHint,
		.matchIndexPath = &MatchIndexPathForIndexHint,
		.alternatePath = &TryUseAlternateIndexForIndexHint,
		.noIndexHandler = &ThrowIndexHintUnableToFindIndex,
		.enableForceIndexPushdown = &EnableIndexHintForceIndexPushdown
	},
	[ForceIndexOpType_PrimaryKeyLookup] = {
		.operator = ForceIndexOpType_PrimaryKeyLookup,
		.updateIndexes = &UpdateIndexListForPrimaryKeyLookup,
		.matchIndexPath = &MatchIndexPathForPrimaryKeyLookup,
		.alternatePath = &TryUseAlternateIndexForPrimaryKeyLookup,
		.noIndexHandler = &PrimaryKeyLookupUnableToFindIndex,
		.enableForceIndexPushdown = &DefaultTrueForceIndexPushdown
	}
};

extern bool EnableVectorForceIndexPushdown;
extern bool EnableGeonearForceIndexPushdown;
extern bool UseNewElemMatchIndexPushdown;
extern bool UseNewElemMatchIndexOperatorOnPushdown;
extern bool DisableDollarSupportFuncSelectivity;
extern bool EnableNewOperatorSelectivityMode;
extern bool EnableCompositeIndexPlanner;
extern bool LowSelectivityForLookup;
extern bool EnableIndexOrderbyPushdown;
extern bool EnableIndexOrderByReverse;
extern bool SetSelectivityForFullScan;
extern bool EnableExprLookupIndexPushdown;
extern bool EnableUnifyPfeOnIndexInfo;
extern bool EnableIdIndexPushdown;
extern bool ForceIndexOnlyScanIfAvailable;
extern bool EnableIdIndexCustomCostFunction;
extern bool EnableIndexOnlyScan;
extern bool EnableOrderByIdOnCostFunction;
extern bool EnablePrimaryKeyCursorScan;

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
	Pointer responsePointer = NULL;
	if (IsA(supportRequest, SupportRequestIndexCondition))
	{
		/* Try to convert operator/function call to index conditions */
		SupportRequestIndexCondition *req =
			(SupportRequestIndexCondition *) supportRequest;

		/* if we matched the condition to the index, then this function is not lossy -
		 * The operator is a perfect match for the function.
		 */
		req->lossy = false;

		Expr *finalNode = HandleSupportRequestCondition(req);
		if (finalNode != NULL)
		{
			if (IsA(finalNode, BoolExpr))
			{
				BoolExpr *boolExpr = (BoolExpr *) finalNode;
				responsePointer = (Pointer) boolExpr->args;
			}
			else
			{
				responsePointer = (Pointer) list_make1(finalNode);
			}
		}
	}
	else if (IsA(supportRequest, SupportRequestSelectivity))
	{
		SupportRequestSelectivity *req = (SupportRequestSelectivity *) supportRequest;
		if (!DisableDollarSupportFuncSelectivity &&
			(EnableNewOperatorSelectivityMode || EnableCompositeIndexPlanner))
		{
			const MongoIndexOperatorInfo *indexOperator =
				GetMongoIndexOperatorInfoByPostgresFuncId(req->funcid);
			if (indexOperator != NULL && indexOperator->indexStrategy !=
				BSON_INDEX_STRATEGY_INVALID)
			{
				/* See plancat.c function_selectivity */
				const double defaultFuncExprSelectivity = 0.3333333;
				Oid selectivityOpExpr = GetMongoQueryOperatorOid(indexOperator);
				double selectivity = GetDollarOperatorSelectivity(
					req->root, selectivityOpExpr, req->args, req->inputcollid,
					req->varRelid, defaultFuncExprSelectivity);
				req->selectivity = selectivity;
				responsePointer = (Pointer) req;
			}
		}
		else if (SetSelectivityForFullScan &&
				 req->funcid == BsonRangeMatchFunctionId())
		{
			/* For fullScan for orderby, we want to ensure we mark the
			 * selectivity as 1.0 to ensure that we say that it will select
			 * all rows for planner estimation.
			 */
			if (IsBsonRangeArgsForFullScan(req->args))
			{
				req->selectivity = 1.0;
				responsePointer = (Pointer) req;
			}
		}
	}
	else if (IsA(supportRequest, SupportRequestCost))
	{
		/* Since a fullscan qpqual is ripped out by the planner,
		 * we simply say here that its cost is super low.
		 */
		SupportRequestCost *req = (SupportRequestCost *) supportRequest;
		if (SetSelectivityForFullScan &&
			req->funcid == BsonRangeMatchFunctionId() && req->node != NULL &&
			IsA(req->node, FuncExpr))
		{
			FuncExpr *func = (FuncExpr *) req->node;
			if (IsBsonRangeArgsForFullScan(func->args))
			{
				req->per_tuple = 1e-9;
				req->startup = 0;
				responsePointer = (Pointer) req;
			}
		}
	}

	PG_RETURN_POINTER(responsePointer);
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

	if (LowSelectivityForLookup &&
		IsA(supportRequest, SupportRequestSelectivity))
	{
		SupportRequestSelectivity *req = (SupportRequestSelectivity *) supportRequest;

		/*
		 * Consider low selectivity of lookup filter for better index estimates.
		 */
		req->selectivity = LowSelectivity;
		PG_RETURN_POINTER(req);
	}

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


static bool
IsBsonRangeArgsForFullScan(List *args)
{
	if (list_length(args) == 2)
	{
		return false;
	}

	Expr *queryVal = lsecond(args);
	if (!IsA(queryVal, Const))
	{
		/* If the query value is not a constant, we can't push down */
		return false;
	}

	Const *queryConst = (Const *) queryVal;
	pgbson *queryBson = DatumGetPgBson(queryConst->constvalue);

	pgbsonelement queryElement;
	PgbsonToSinglePgbsonElement(queryBson, &queryElement);

	DollarRangeParams rangeParams = { 0 };
	InitializeQueryDollarRange(&queryElement.bsonValue, &rangeParams);
	if (rangeParams.isFullScan)
	{
		return true;
	}

	return false;
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
	BsonIndexStrategy strategy = BSON_INDEX_STRATEGY_INVALID;
	if (req->funcid == BsonDollarLookupJoinFilterFunctionOid())
	{
		operatorOid = BsonInMatchFunctionId();
		strategy = BSON_INDEX_STRATEGY_DOLLAR_IN;
	}
	else if (req->funcid == BsonDollarMergeJoinFunctionOid())
	{
		operatorOid = BsonEqualMatchIndexFunctionId();
		strategy = BSON_INDEX_STRATEGY_DOLLAR_EQUAL;
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

	if (!ValidateIndexForQualifierPathForEquality(options, &pathView, strategy))
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
bool
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


inline static void
ThrowIfIncompatibleOpForIndexHint(ForceIndexOpType hintOpType, ForceIndexOpType opType)
{
	if (hintOpType != ForceIndexOpType_IndexHint)
	{
		return;
	}

	if (opType == ForceIndexOpType_Text)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("$text queries cannot specify hint")));
	}
	else if (opType == ForceIndexOpType_VectorSearch)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("Vector search queries cannot specify hint")));
	}
	else if (opType == ForceIndexOpType_GeoNear)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("GeoNear queries cannot specify hint")));
	}
}


static void
CheckNullTestForGeoSpatialForcePushdown(ReplaceExtensionFunctionContext *context,
										NullTest *nullTest)
{
	if (context->forceIndexQueryOpData.type != ForceIndexOpType_GeoNear &&
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
			ThrowIfIncompatibleOpForIndexHint(
				context->forceIndexQueryOpData.type, ForceIndexOpType_GeoNear);
			context->forceIndexQueryOpData.type = ForceIndexOpType_GeoNear;
		}
	}
}


/*
 * Walks an specific restriction expr and collections the necessary information from it
 * and stores the relevant information in the ReplaceExtensionFunctionContext. This may be
 * information about streaming cursors, geospatial indexes, and other index-related metadata.
 * Note that currentRestrictInfo can be NULL if there's an OR/AND and this is recursing.
 */
static void
CheckRestrictionPathNodeForIndexOperation(Expr *currentExpr,
										  ReplaceExtensionFunctionContext *context,
										  PrimaryKeyLookupContext *primaryKeyContext,
										  RestrictInfo *currentRestrictInfo)
{
	CHECK_FOR_INTERRUPTS();
	check_stack_depth();
	if (IsA(currentExpr, FuncExpr))
	{
		FuncExpr *funcExpr = (FuncExpr *) currentExpr;
		if (IsClusterVersionAtleast(DocDB_V0, 106, 0) &&
			funcExpr->funcid == BsonIndexHintFunctionOid())
		{
			Node *secondNode = lsecond(funcExpr->args);
			if (!IsA(secondNode, Const))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("Index hint must be a constant value")));
			}

			Node *keyDocumentNode = lthird(funcExpr->args);
			if (!IsA(keyDocumentNode, Const))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("Index key document must be a constant value")));
			}

			Node *sparseNode = lfourth(funcExpr->args);
			if (!IsA(sparseNode, Const))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("Index sparse must be a constant value")));
			}

			ThrowIfIncompatibleOpForIndexHint(
				ForceIndexOpType_IndexHint, context->forceIndexQueryOpData.type);
			Const *secondConst = (Const *) secondNode;
			IndexHintMatchContext *hintContext = palloc0(
				sizeof(IndexHintMatchContext));
			hintContext->documentExpr = linitial(funcExpr->args);
			hintContext->documentDBIndexName = TextDatumGetCString(
				secondConst->constvalue);
			hintContext->isSparse = DatumGetBool(((Const *) sparseNode)->constvalue);

			context->forceIndexQueryOpData.type = ForceIndexOpType_IndexHint;
			context->forceIndexQueryOpData.path = NULL;
			context->forceIndexQueryOpData.opExtraState = hintContext;
		}
		else if (funcExpr->funcid == ApiBsonSearchParamFunctionId())
		{
			/* Just validate indexHint is incompatible with vector search but don't set
			 * the forceIndexQueryOpData.type to vector search yet to keep compatibility.
			 */
			context->hasVectorSearchQuery = true;
			ThrowIfIncompatibleOpForIndexHint(
				context->forceIndexQueryOpData.type, ForceIndexOpType_VectorSearch);
		}
		else if (funcExpr->funcid == ApiCursorStateFunctionId())
		{
			context->hasStreamingContinuationScan = true;
		}
		else
		{
			const MongoQueryOperator *operator =
				GetMongoQueryOperatorByQueryOperatorType(QUERY_OPERATOR_TEXT,
														 MongoQueryOperatorInputType_Bson);
			if (operator->postgresRuntimeFunctionOidLookup() == funcExpr->funcid)
			{
				ThrowIfIncompatibleOpForIndexHint(
					context->forceIndexQueryOpData.type, ForceIndexOpType_Text);
				context->forceIndexQueryOpData.type = ForceIndexOpType_Text;
			}
			else if (primaryKeyContext != NULL && funcExpr->funcid ==
					 BsonInMatchFunctionId())
			{
				Expr *firstArg = linitial(funcExpr->args);
				Expr *secondArg = lsecond(funcExpr->args);
				if (IsA(firstArg, Var) && IsA(secondArg, Const))
				{
					Var *var = (Var *) firstArg;
					Const *rightConst = (Const *) secondArg;
					if (var->varattno == DOCUMENT_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER &&
						var->varno == (int) context->inputData.rteIndex)
					{
						pgbsonelement queryElement;
						if (TryGetSinglePgbsonElementFromPgbson(
								DatumGetPgBsonPacked(rightConst->constvalue),
								&queryElement) &&
							queryElement.pathLength == 3 && strcmp(queryElement.path,
																   "_id") == 0)
						{
							RuntimePrimaryKeyRestrictionData *runtimeDollarIn =
								palloc0(sizeof(RuntimePrimaryKeyRestrictionData));
							runtimeDollarIn->value = queryElement.bsonValue;
							runtimeDollarIn->restrictInfo = currentRestrictInfo;

							primaryKeyContext->runtimeDollarInRestrictionData =
								lappend(primaryKeyContext->runtimeDollarInRestrictionData,
										runtimeDollarIn);
						}
					}
				}
			}
		}
	}
	else if (primaryKeyContext != NULL && currentRestrictInfo != NULL && IsA(currentExpr,
																			 OpExpr))
	{
		OpExpr *opExpr = (OpExpr *) currentExpr;
		if (opExpr->opno == BigintEqualOperatorId())
		{
			Expr *firstArg = linitial(opExpr->args);
			if (IsA(firstArg, Var))
			{
				Var *var = (Var *) firstArg;
				if (var->varattno ==
					DOCUMENT_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER &&
					var->varno == (int) context->inputData.rteIndex)
				{
					primaryKeyContext->shardKeyQualExpr = currentRestrictInfo;
					context->plannerOrderByData.shardKeyEqualityExpr =
						currentRestrictInfo;
					context->plannerOrderByData.isShardKeyEqualityOnUnsharded =
						IsOpExprShardKeyForUnshardedCollections(currentExpr,
																context->inputData.
																collectionId);
				}
			}
		}
		else if (opExpr->opno == BsonEqualOperatorId())
		{
			Expr *firstArg = linitial(opExpr->args);
			Expr *secondArg = lsecond(opExpr->args);
			if (IsA(firstArg, Var) && IsA(secondArg, Const))
			{
				Var *var = (Var *) firstArg;
				Const *rightConst = (Const *) secondArg;
				if (var->varattno == DOCUMENT_DATA_TABLE_OBJECT_ID_VAR_ATTR_NUMBER &&
					var->varno == (int) context->inputData.rteIndex)
				{
					pgbsonelement queryElement;
					primaryKeyContext->objectId.restrictInfo = currentRestrictInfo;
					primaryKeyContext->objectId.isPrimaryKeyEquality = true;
					if (TryGetSinglePgbsonElementFromPgbson(
							DatumGetPgBsonPacked(rightConst->constvalue), &queryElement))
					{
						primaryKeyContext->objectId.equalityBsonValue =
							queryElement.bsonValue;
					}
				}
			}
		}
		else if (opExpr->opno == BsonEqualMatchRuntimeOperatorId())
		{
			Expr *firstArg = linitial(opExpr->args);
			Expr *secondArg = lsecond(opExpr->args);
			if (IsA(firstArg, Var) && IsA(secondArg, Const))
			{
				Var *var = (Var *) firstArg;
				Const *rightConst = (Const *) secondArg;
				if (var->varattno == DOCUMENT_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER &&
					var->varno == (int) context->inputData.rteIndex)
				{
					pgbsonelement queryElement;
					if (TryGetSinglePgbsonElementFromPgbson(
							DatumGetPgBsonPacked(rightConst->constvalue),
							&queryElement) &&
						queryElement.pathLength == 3 && strcmp(queryElement.path,
															   "_id") == 0)
					{
						RuntimePrimaryKeyRestrictionData *equalityRestrictionData =
							palloc0(sizeof(RuntimePrimaryKeyRestrictionData));
						equalityRestrictionData->value = queryElement.bsonValue;
						equalityRestrictionData->restrictInfo = currentRestrictInfo;
						primaryKeyContext->runtimeEqualityRestrictionData =
							lappend(primaryKeyContext->runtimeEqualityRestrictionData,
									equalityRestrictionData);
					}
				}
			}
		}
	}
	else if (primaryKeyContext != NULL && primaryKeyContext->objectId.restrictInfo ==
			 NULL &&
			 IsA(currentExpr, ScalarArrayOpExpr))
	{
		ScalarArrayOpExpr *scalarArrayOpExpr = (ScalarArrayOpExpr *) currentExpr;
		if (scalarArrayOpExpr->opno == BsonEqualOperatorId() &&
			scalarArrayOpExpr->useOr)
		{
			Expr *firstArg = linitial(scalarArrayOpExpr->args);
			if (IsA(firstArg, Var))
			{
				Var *var = (Var *) firstArg;
				if (var->varattno == DOCUMENT_DATA_TABLE_OBJECT_ID_VAR_ATTR_NUMBER &&
					var->varno == (int) context->inputData.rteIndex)
				{
					primaryKeyContext->objectId.restrictInfo = currentRestrictInfo;
				}
			}
		}
	}
	else if (IsA(currentExpr, NullTest))
	{
		NullTest *nullTest = (NullTest *) currentExpr;
		CheckNullTestForGeoSpatialForcePushdown(context, nullTest);
	}
	else if (IsA(currentExpr, BoolExpr))
	{
		BoolExpr *boolExpr = (BoolExpr *) currentExpr;
		ListCell *boolArgs;
		PrimaryKeyLookupContext *childContext = NULL;
		foreach(boolArgs, boolExpr->args)
		{
			CheckRestrictionPathNodeForIndexOperation(lfirst(boolArgs), context,
													  childContext, NULL);
		}
	}
}


static bool
HasTextPathOpFamily(IndexOptInfo *indexInfo)
{
	Oid textOpClass = GetTextPathOpFamilyOid(indexInfo->relam);
	if (textOpClass == InvalidOid)
	{
		return false;
	}

	for (int i = 0; i < indexInfo->ncolumns; i++)
	{
		if (indexInfo->opfamily[i] == textOpClass)
		{
			return true;
		}
	}

	return false;
}


static void
CheckPathForIndexOperations(Path *path, ReplaceExtensionFunctionContext *context)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	if (IsA(path, BitmapOrPath))
	{
		BitmapOrPath *orPath = (BitmapOrPath *) path;
		WalkPathsForIndexOperations(orPath->bitmapquals, context);
	}
	else if (IsA(path, BitmapAndPath))
	{
		BitmapAndPath *andPath = (BitmapAndPath *) path;
		WalkPathsForIndexOperations(andPath->bitmapquals, context);
	}
	else if (IsA(path, BitmapHeapPath))
	{
		BitmapHeapPath *heapPath = (BitmapHeapPath *) path;
		CheckPathForIndexOperations(heapPath->bitmapqual, context);
	}
	else if (IsA(path, IndexPath))
	{
		IndexPath *indexPath = (IndexPath *) path;

		/* Ignore primary key lookup paths parented in a bitmap scan:
		 * This can happen because a RUM index lookup can produce a 0 cost query as well
		 * and Postgres picks both and does a BitmapAnd - instead rely on a top level index path.
		 */
		if (IsBtreePrimaryKeyIndex(indexPath->indexinfo) &&
			list_length(indexPath->indexclauses) > 1)
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
		else if (indexPath->indexinfo->relam == GIST_AM_OID &&
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
		else if (HasTextPathOpFamily(indexPath->indexinfo))
		{
			/* RUM/GIST indexes */
			ListCell *indexPathCell;
			foreach(indexPathCell, indexPath->indexclauses)
			{
				IndexClause *iclause = (IndexClause *) lfirst(indexPathCell);
				bytea *options = NULL;
				if (indexPath->indexinfo->opclassoptions != NULL)
				{
					options = indexPath->indexinfo->opclassoptions[iclause->indexcol];
				}

				/* Specific to text indexes: If the OpFamily is for Text, update the context
				 * with the index options for text. This is used later to process restriction info
				 * so that we can push down the TSQuery with the appropriate default language settings.
				 */
				if (IsTextPathOpFamilyOid(
						indexPath->indexinfo->relam,
						indexPath->indexinfo->opfamily[iclause->indexcol]))
				{
					/* If there's no options, set it. Otherwise, fail with "too many paths" */
					if (context->forceIndexQueryOpData.opExtraState != NULL)
					{
						ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
										errmsg("Excessive number of text expressions")));
					}
					context->forceIndexQueryOpData.type = ForceIndexOpType_Text;
					context->forceIndexQueryOpData.path = indexPath;
					QueryTextIndexData *textIndexData = palloc0(
						sizeof(QueryTextIndexData));
					textIndexData->indexOptions = options;
					context->forceIndexQueryOpData.opExtraState = (void *) textIndexData;
				}
			}
		}
	}
}


void
WalkPathsForIndexOperations(List *pathsList,
							ReplaceExtensionFunctionContext *context)
{
	ListCell *cell;
	foreach(cell, pathsList)
	{
		Path *path = (Path *) lfirst(cell);
		CheckPathForIndexOperations(path, context);
	}
}


void
WalkRestrictionPathsForIndexOperations(List *restrictInfo,
									   List *joinInfo,
									   ReplaceExtensionFunctionContext *
									   context)
{
	PrimaryKeyLookupContext primaryKeyContext = { 0 };

	ListCell *cell;
	foreach(cell, restrictInfo)
	{
		RestrictInfo *rinfo = lfirst_node(RestrictInfo, cell);
		CheckRestrictionPathNodeForIndexOperation(
			rinfo->clause, context, &primaryKeyContext, rinfo);
	}

	foreach(cell, joinInfo)
	{
		RestrictInfo *rinfo = lfirst_node(RestrictInfo, cell);
		CheckRestrictionPathNodeForIndexOperation(
			rinfo->clause, context, &primaryKeyContext, rinfo);
	}

	/* Set primary key force pushdown if requested. */
	if (context->forceIndexQueryOpData.type == ForceIndexOpType_None &&
		primaryKeyContext.shardKeyQualExpr != NULL &&
		primaryKeyContext.objectId.restrictInfo != NULL)
	{
		PrimaryKeyLookupContext *pkContext = palloc(sizeof(PrimaryKeyLookupContext));
		primaryKeyContext.primaryKeyLookupPath = context->primaryKeyLookupPath;

		*pkContext = primaryKeyContext;
		context->forceIndexQueryOpData.type = ForceIndexOpType_PrimaryKeyLookup;
		context->forceIndexQueryOpData.path = NULL;
		context->forceIndexQueryOpData.opExtraState = pkContext;
	}
	else
	{
		list_free_deep(primaryKeyContext.runtimeDollarInRestrictionData);
		list_free_deep(primaryKeyContext.runtimeEqualityRestrictionData);
	}
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
			if (EnablePrimaryKeyCursorScan && context->hasStreamingContinuationScan)
			{
				/* Don't trim the shard key qual here - wait until the continuation is formed */
				continue;
			}

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
			if (list_length(restrictInfo) == 1)
			{
				return NIL;
			}

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
Path *
ForceIndexForQueryOperators(PlannerInfo *root, RelOptInfo *rel,
							ReplaceExtensionFunctionContext *context)
{
	if (context->forceIndexQueryOpData.type == ForceIndexOpType_None ||
		context->forceIndexQueryOpData.type >= ForceIndexOpType_Max)
	{
		/* If no special operator requirement */
		return NULL;
	}

	const ForceIndexSupportFuncs *forceIndexFuncs =
		&ForceIndexOperatorSupport[context->forceIndexQueryOpData.type];
	if (!forceIndexFuncs->enableForceIndexPushdown(root, context))
	{
		/* No index support functions !!, or force index pushdown not required then can't do anything */
		return NULL;
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
			return NULL;
		}

		Path *matchingPath = FindIndexPathForQueryOperator(rel, rel->pathlist, context,
														   MatchIndexPathEquals,
														   context->forceIndexQueryOpData.
														   path);
		rel->partial_pathlist = NIL;
		rel->pathlist = list_make1(matchingPath);
		return matchingPath;
	}

	List *oldIndexList = rel->indexlist;
	List *oldPathList = rel->pathlist;
	List *oldPartialPathList = rel->partial_pathlist;

	Path *matchingPath = NULL;

	/* Only consider the indexes that we want to push to based on the operator */
	List *newIndexList = forceIndexFuncs->updateIndexes(oldIndexList, context);
	if (list_length(newIndexList) > 0)
	{
		/* Generate interesting index paths again with filtered indexes */
		rel->indexlist = newIndexList;
		rel->pathlist = NIL;
		rel->partial_pathlist = NIL;

		create_index_paths(root, rel);

		/* Check if index path was created for the operator based on matching criteria */
		matchingPath = FindIndexPathForQueryOperator(rel, rel->pathlist,
													 context,
													 forceIndexFuncs->matchIndexPath,
													 context->forceIndexQueryOpData.
													 opExtraState);
	}

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
		else if (list_length(rel->pathlist) > 0)
		{
			/* If alternate path is created, then we can use the first path as the matching path */
			matchingPath = linitial(rel->pathlist);
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

	return matchingPath;
}


static bool
ProjectionReferencesDocumentVar(Expr *node, void *state)
{
	CHECK_FOR_INTERRUPTS();

	if (node == NULL)
	{
		return false;
	}

	if (IsA(node, Var))
	{
		/* If we have any vars, just return true */
		bool *isFound = (bool *) state;
		*isFound = true;
		return false;
	}
	else if (IsA(node, Query))
	{
		/* A projection with a subquery - don't apply indexonlyscan optimization */
		bool *isFound = (bool *) state;
		*isFound = true;
		return false;
	}

	return expression_tree_walker((Node *) node, ProjectionReferencesDocumentVar, state);
}


static inline bool
IndexStrategySupportsIndexOnlyScan(BsonIndexStrategy indexStrategy)
{
	return !IsNegationStrategy(indexStrategy) &&
		   indexStrategy != BSON_INDEX_STRATEGY_DOLLAR_ELEMMATCH &&
		   indexStrategy != BSON_INDEX_STRATEGY_DOLLAR_TYPE &&
		   indexStrategy != BSON_INDEX_STRATEGY_DOLLAR_SIZE &&
		   indexStrategy != BSON_INDEX_STRATEGY_INVALID;
}


static inline bool
IsShardKeyFilterBoolExpr(BoolExpr *boolExpr, RestrictInfo *shardKeyExpr)
{
	bool isShardKeyFilter = false;
	ListCell *boolArgs;
	foreach(boolArgs, boolExpr->args)
	{
		RestrictInfo *boolRinfo = lfirst_node(RestrictInfo, boolArgs);
		if (boolRinfo == shardKeyExpr)
		{
			isShardKeyFilter = true;
			break;
		}
	}

	return isShardKeyFilter;
}


static bool
IndexClausesValidForIndexOnlyScan(IndexPath *indexPath,
								  RelOptInfo *rel,
								  ReplaceExtensionFunctionContext *replaceContext)
{
	bytea *indexOptions = indexPath->indexinfo->opclassoptions != NULL ?
						  indexPath->indexinfo->opclassoptions[0] : NULL;
	if (indexOptions == NULL)
	{
		return false;
	}

	ListCell *clauseCell;
	foreach(clauseCell, indexPath->indexclauses)
	{
		IndexClause *clause = (IndexClause *) lfirst(clauseCell);
		if (clause->lossy)
		{
			return false;
		}

		if (clause->indexcol != 0 ||
			list_length(clause->indexquals) != 1)
		{
			/* Only support indexonlyscan if the index clause is on the first column */
			return false;
		}

		RestrictInfo *rinfo = clause->rinfo;
		if (!IsA(rinfo->clause, OpExpr))
		{
			return false;
		}

		OpExpr *opExpr = (OpExpr *) rinfo->clause;
		const MongoIndexOperatorInfo *indexOperator =
			GetMongoIndexOperatorByPostgresOperatorId(opExpr->opno);

		if (!IndexStrategySupportsIndexOnlyScan(indexOperator->indexStrategy))
		{
			return false;
		}

		/* TODO (IndexOnlyScan): can we support null equality? */
		Expr *secondArg = lsecond(opExpr->args);
		if (!IsA(secondArg, Const))
		{
			return false;
		}
	}

	ListCell *rinfoCell;
	foreach(rinfoCell, indexPath->indexinfo->indrestrictinfo)
	{
		RestrictInfo *baseRestrictInfo = (RestrictInfo *) lfirst(rinfoCell);
		Expr *clause = baseRestrictInfo->clause;

		if (!IsA(clause, OpExpr))
		{
			if (IsA(clause, BoolExpr) &&
				replaceContext->plannerOrderByData.isShardKeyEqualityOnUnsharded &&
				IsShardKeyFilterBoolExpr((BoolExpr *) clause,
										 replaceContext->plannerOrderByData.
										 shardKeyEqualityExpr))
			{
				/* If it is a shard key filter, we can safely do an index only scan. */
				continue;
			}

			return false;
		}

		OpExpr *opExpr = (OpExpr *) clause;

		Expr *secondArg = lsecond(opExpr->args);
		if (!IsA(secondArg, Const))
		{
			return false;
		}

		const MongoIndexOperatorInfo *indexOperator =
			GetMongoIndexOperatorByPostgresOperatorId(opExpr->opno);
		BsonIndexStrategy indexStrategy = indexOperator->indexStrategy;

		if (indexStrategy == BSON_INDEX_STRATEGY_INVALID)
		{
			/* if it is a shard key filter, we can safely do an index only scan. */
			if (replaceContext->plannerOrderByData.isShardKeyEqualityOnUnsharded &&
				baseRestrictInfo ==
				replaceContext->plannerOrderByData.shardKeyEqualityExpr)
			{
				continue;
			}

			return false;
		}

		Datum queryValue = ((Const *) secondArg)->constvalue;
		if (!ValidateIndexForQualifierValue(indexOptions, queryValue,
											indexStrategy))
		{
			return false;
		}
	}

	/* All indexclauses are covered by the index and are not lossy operators. */
	return true;
}


static bool
PlanHasAggregates(PlannerInfo *root)
{
	return list_length(root->agginfos) != 0 ||
		   (root->parent_root != NULL && PlanHasAggregates(root->parent_root));
}


static bool
IsValidForIndexOnlyScans(PlannerInfo *root)
{
	if (!PlanHasAggregates(root) ||
		root->hasJoinRTEs)
	{
		/* Don't handle simple queries for now - only things with aggregates
		 * Note: Things like GroupBy with no aggregates will not work here, but
		 * that's okay. We also only consider base tables for index only scans.
		 * TODO: This can also be extended to handle covered indexes later.
		 */
		return false;
	}

	bool projectionHasVarOrQuery = false;
	expression_tree_walker((Node *) root->processed_tlist,
						   ProjectionReferencesDocumentVar,
						   &projectionHasVarOrQuery);
	if (projectionHasVarOrQuery)
	{
		/* If the projection has a Var or a Query, we can't do index only scan
		 * because we can't cover the projection.
		 */
		return false;
	}

	return true;
}


/*
 * Check whether we can handle index scans as index only scans.
 * This is possible if:
 * 1) The query is against a base table
 * 2) There are no joins
 * 3) Projection is covered (Today this requires projection to be a constant but
 *    this can be extended in the future)
 * 4) Filters are covered by the index.
 * 5) The index filters are are not lossy operators.
 * 6) The index is a composite index.
 */
void
ConsiderIndexOnlyScan(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte,
					  Index rti, ReplaceExtensionFunctionContext *context)
{
	if (rte->rtekind != RTE_RELATION)
	{
		return;
	}

	if (!IsValidForIndexOnlyScans(root))
	{
		return;
	}

	if (rel->pathlist == NIL)
	{
		/* No paths to consider */
		return;
	}

	List *addedPaths = NIL;
	ListCell *cell;
	foreach(cell, rel->pathlist)
	{
		Path *path = (Path *) lfirst(cell);
		if (IsA(path, BitmapHeapPath))
		{
			BitmapHeapPath *bitmapPath = (BitmapHeapPath *) path;

			if (IsA(bitmapPath->bitmapqual, IndexPath))
			{
				path = (Path *) bitmapPath->bitmapqual;
			}
		}

		if (!IsA(path, IndexPath))
		{
			continue;
		}

		bool isBtreeIndex = false;
		IndexPath *indexPath = (IndexPath *) path;

		if (indexPath->path.pathtype == T_IndexOnlyScan)
		{
			/* Already an index only scan */
			continue;
		}

		if (IsBtreePrimaryKeyIndex(indexPath->indexinfo) &&
			EnableIdIndexPushdown)
		{
			if (EnableIdIndexCustomCostFunction && !ForceIndexOnlyScanIfAvailable)
			{
				continue;
			}

			isBtreeIndex = true;
			bool hasOtherQuals = false;
			IndexPath *modified = TrimIndexRestrictInfoForBtreePath(root, indexPath,
																	&hasOtherQuals);
			if (hasOtherQuals)
			{
				/* Not modified or has non _id quals - skip */
				continue;
			}

			if (modified == indexPath)
			{
				indexPath = palloc(sizeof(IndexPath));
				memcpy(indexPath, modified, sizeof(IndexPath));
			}
			else
			{
				indexPath = modified;
			}
		}
		else
		{
			if (indexPath->indexinfo->nkeycolumns < 1 ||
				!IsOrderBySupportedOnOpClass(indexPath->indexinfo->relam,
											 indexPath->indexinfo->opfamily[0]))
			{
				continue;
			}

			if (!CompositeIndexSupportsIndexOnlyScan(indexPath))
			{
				continue;
			}

			if (!IndexClausesValidForIndexOnlyScan(indexPath, rel, context))
			{
				continue;
			}
		}

		/* we need to copy the index path and set it as index only scan.
		 * Also we need to set canreturn to true so that postgres allows the index only scan path. */
		IndexPath *indexPathCopy;
		if (!isBtreeIndex)
		{
			indexPathCopy = makeNode(IndexPath);
			memcpy(indexPathCopy, indexPath, sizeof(IndexPath));

			indexPathCopy->indexinfo = palloc(sizeof(IndexOptInfo));
			memcpy(indexPathCopy->indexinfo, indexPath->indexinfo,
				   sizeof(IndexOptInfo));

			indexPathCopy->indexinfo->canreturn = palloc0(sizeof(bool) *
														  indexPathCopy->indexinfo->
														  ncolumns);
			indexPathCopy->indexinfo->canreturn[0] = true;
		}
		else
		{
			/* This is pre-copied by TrimIndexRestrictInfoForBtreePath */
			indexPathCopy = indexPath;
		}

		indexPathCopy->path.pathtype = T_IndexOnlyScan;

		bool partialPath = false;
		double loopCount = 1.0;
		cost_index(indexPathCopy, root, loopCount, partialPath);

		addedPaths = lappend(addedPaths, indexPathCopy);
	}

	if (ForceIndexOnlyScanIfAvailable &&
		list_length(addedPaths) > 0)
	{
		/* reset pathlist to only have these */
		rel->pathlist = addedPaths;
		rel->partial_pathlist = NIL;
	}
	else
	{
		ListCell *pathsToAddCell;
		foreach(pathsToAddCell, addedPaths)
		{
			/* now add the new paths */
			Path *newPath = lfirst(pathsToAddCell);
			add_path(rel, newPath);
		}

		list_free(addedPaths);
	}
}


inline static IndexOptInfo *
GetPrimaryKeyIndexOptInfo(RelOptInfo *rel)
{
	ListCell *index;
	foreach(index, rel->indexlist)
	{
		IndexOptInfo *indexInfo = lfirst(index);
		if (IsBtreePrimaryKeyIndex(indexInfo))
		{
			return indexInfo;
		}
	}

	return NULL;
}


static void
ConsiderBtreeOrderByPushdown(PlannerInfo *root, IndexPath *indexPath)
{
	bool hasOrderBy = false;
	bool hasGroupby = false;
	bool isOrderById = false;
	List *sortDetails = GetSortDetails(root, indexPath->path.parent->relid, &hasOrderBy,
									   &hasGroupby, &isOrderById);

	if (sortDetails == NIL || !isOrderById)
	{
		list_free_deep(sortDetails);
		return;
	}

	if (!IsValidIndexPathForIdOrderBy(indexPath, sortDetails))
	{
		list_free_deep(sortDetails);
		return;
	}

	/*
	 * We have a single sort and a primary key - consider if
	 * it is an _id pushdown.
	 */
	SortIndexInputDetails *sortDetailsInput = linitial(sortDetails);

	/* The first clause is a shard key equality - can push order by */
	indexPath->path.pathkeys = list_make1(sortDetailsInput->sortPathKey);

	/* If the sort is descending, we need to scan the index backwards */
	if (SortPathKeyStrategy(sortDetailsInput->sortPathKey) == BTGreaterStrategyNumber)
	{
		indexPath->indexscandir = BackwardScanDirection;
	}

	list_free_deep(sortDetails);
}


void
documentdb_btcostestimate(PlannerInfo *root, IndexPath *path, double loop_count,
						  Cost *indexStartupCost, Cost *indexTotalCost,
						  Selectivity *indexSelectivity, double *indexCorrelation,
						  double *indexPages)
{
	if (EnableOrderByIdOnCostFunction && EnableIdIndexCustomCostFunction &&
		list_length(root->query_pathkeys) == 1)
	{
		ConsiderBtreeOrderByPushdown(root, path);
	}

	if (EnableIdIndexCustomCostFunction && EnableIndexOnlyScan &&
		IsValidForIndexOnlyScans(root))
	{
		bool hasOtherQuals = false;
		IndexPath *modified = TrimIndexRestrictInfoForBtreePath(root, path,
																&hasOtherQuals);
		if (!hasOtherQuals)
		{
			*path = *modified;
			path->path.pathtype = T_IndexOnlyScan;
		}

		if (modified != path)
		{
			/* Free if copy */
			pfree(modified);
		}
	}

	btcostestimate(root, path, loop_count, indexStartupCost, indexTotalCost,
				   indexSelectivity, indexCorrelation, indexPages);
}


inline static IndexClause *
BuildPointReadIndexClause(RestrictInfo *restrictInfo, int indexCol)
{
	IndexClause *iclause = makeNode(IndexClause);
	iclause->rinfo = restrictInfo;
	iclause->indexquals = list_make1(restrictInfo);
	iclause->lossy = false;
	iclause->indexcol = indexCol;
	iclause->indexcols = NIL;
	return iclause;
}


static List *
GetSortDetails(PlannerInfo *root, Index rti,
			   bool *hasOrderBy, bool *hasGroupby, bool *isOrderById)
{
	List *sortDetails = NIL;
	ListCell *sortCell;
	foreach(sortCell, root->query_pathkeys)
	{
		PathKey *pathkey = (PathKey *) lfirst(sortCell);
		if (pathkey->pk_eclass == NULL ||
			list_length(pathkey->pk_eclass->ec_members) != 1)
		{
			return NIL;
		}

		EquivalenceMember *member = linitial(pathkey->pk_eclass->ec_members);

		if (!IsA(member->em_expr, FuncExpr))
		{
			return NIL;
		}

		FuncExpr *func = (FuncExpr *) member->em_expr;
		if (func->funcid == BsonOrderByFunctionOid())
		{
			if (*hasGroupby)
			{
				return NIL;
			}

			*hasOrderBy = true;
		}
		else if (func->funcid == BsonExpressionGetFunctionOid() ||
				 func->funcid == BsonExpressionGetWithLetFunctionOid())
		{
			if (*hasOrderBy)
			{
				return NIL;
			}

			*hasGroupby = true;
		}
		else
		{
			return NIL;
		}

		/* This is an order by function */
		Expr *firstArg = linitial(func->args);
		Expr *secondArg = lsecond(func->args);

		if (!IsA(firstArg, Var) || !IsA(secondArg, Const))
		{
			return NIL;
		}

		Var *firstVar = (Var *) firstArg;
		Const *secondConst = (Const *) secondArg;

		if (firstVar->varno != (int) rti ||
			firstVar->varattno != DOCUMENT_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER ||
			firstVar->vartype != BsonTypeId() ||
			secondConst->consttype != BsonTypeId() || secondConst->constisnull)
		{
			return NIL;
		}

		pgbsonelement sortElement;
		PgbsonToSinglePgbsonElement(
			DatumGetPgBson(secondConst->constvalue), &sortElement);
		if (*hasGroupby)
		{
			/* In the case of group by the expression would be { "": expr }
			 * Here we can push down to the index iff the expression is a path.
			 */
			if (sortElement.bsonValue.value_type != BSON_TYPE_UTF8)
			{
				return NIL;
			}

			if (sortElement.bsonValue.value.v_utf8.len > 1 &&
				sortElement.bsonValue.value.v_utf8.str[0] == '$')
			{
				/* This is a valid path: Track the path in the sortElement to decide pushdown */
				sortElement.path = sortElement.bsonValue.value.v_utf8.str + 1;
				sortElement.pathLength = sortElement.bsonValue.value.v_utf8.len - 1;
				sortElement.bsonValue.value_type = BSON_TYPE_INT32;
				sortElement.bsonValue.value.v_int32 = SortPathKeyStrategy(pathkey) ==
													  BTGreaterStrategyNumber ? -1 : 1;
				pgbson *sortSpec = PgbsonElementToPgbson(&sortElement);

				/* Also rewrite the secondConst so that the Expr on the sort operator is correct */
				secondConst = makeConst(BsonTypeId(), -1, InvalidOid, -1, PointerGetDatum(
											sortSpec), false, false);
			}
			else
			{
				return NIL;
			}
		}

		SortIndexInputDetails *sortDetailsInput =
			palloc0(sizeof(SortIndexInputDetails));
		sortDetailsInput->sortPath = sortElement.path;
		sortDetailsInput->sortPathKey = pathkey;
		sortDetailsInput->sortVar = (Expr *) firstVar;
		sortDetailsInput->sortDatum = (Expr *) secondConst;
		sortDetails = lappend(sortDetails, sortDetailsInput);

		*isOrderById = *isOrderById ||
					   (sortElement.pathLength == 3 && strcmp(sortElement.path, "_id") ==
						0);
	}

	return sortDetails;
}


static bool
IsValidIndexPathForIdOrderBy(IndexPath *indexPath, List *sortDetails)
{
	if (indexPath->indexinfo->relam != BTREE_AM_OID ||
		!IsBtreePrimaryKeyIndex(indexPath->indexinfo))
	{
		return false;
	}

	if (list_length(sortDetails) != 1)
	{
		return false;
	}

	/* We have a single sort and a primary key - consider if
	 * it is an _id pushdown.
	 */
	SortIndexInputDetails *sortDetailsInput = linitial(sortDetails);
	if (strcmp(sortDetailsInput->sortPath, "_id") != 0)
	{
		return false;
	}

	/*
	 * We can push down the _id sort to the primary key index
	 * if and only if there's a shard_key equality.
	 */
	if (list_length(indexPath->indexclauses) < 1)
	{
		return false;
	}

	IndexClause *indexClause = linitial(indexPath->indexclauses);
	if (!IsA(indexClause->rinfo->clause, OpExpr))
	{
		return false;
	}

	OpExpr *opExpr = (OpExpr *) indexClause->rinfo->clause;
	Expr *firstArg = linitial(opExpr->args);
	Expr *secondArg = lsecond(opExpr->args);

	if (opExpr->opno != BigintEqualOperatorId() ||
		!IsA(firstArg, Var) || !IsA(secondArg, Const))
	{
		return false;
	}

	Var *firstVar = (Var *) firstArg;
	return firstVar->varattno == DOCUMENT_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER;
}


void
ConsiderIndexOrderByPushdownForId(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte,
								  Index rti, ReplaceExtensionFunctionContext *context)
{
	/* In this path, we only consider order by pushdown for the PK index - so we only support
	 * having a single order by path key
	 */
	if (EnableOrderByIdOnCostFunction || list_length(root->query_pathkeys) != 1)
	{
		return;
	}

	if (rte->rtekind != RTE_RELATION)
	{
		return;
	}

	bool hasOrderBy = false;
	bool hasGroupby = false;
	bool isOrderById = false;
	List *sortDetails = GetSortDetails(root, rti, &hasOrderBy, &hasGroupby, &isOrderById);

	if (sortDetails == NIL || !isOrderById)
	{
		list_free_deep(sortDetails);
		return;
	}

	List *pathsToAdd = NIL;
	ListCell *cell;
	bool hasIndexPaths = false;
	foreach(cell, rel->pathlist)
	{
		Path *path = lfirst(cell);

		if (IsA(path, BitmapHeapPath))
		{
			BitmapHeapPath *bitmapPath = (BitmapHeapPath *) path;

			if (IsA(bitmapPath->bitmapqual, IndexPath))
			{
				path = (Path *) bitmapPath->bitmapqual;
			}
		}

		if (!IsA(path, IndexPath))
		{
			continue;
		}

		IndexPath *indexPath = (IndexPath *) path;
		hasIndexPaths = true;
		if (!IsValidIndexPathForIdOrderBy(indexPath, sortDetails))
		{
			continue;
		}

		/* The first clause is a shard key equality - can push order by */
		IndexPath *newPath = makeNode(IndexPath);
		memcpy(newPath, indexPath, sizeof(IndexPath));
		SortIndexInputDetails *sortDetailsInput = linitial(sortDetails);
		newPath->path.pathkeys = list_make1(sortDetailsInput->sortPathKey);

		/* If the sort is descending, we need to scan the index backwards */
		if (SortPathKeyStrategy(sortDetailsInput->sortPathKey) == BTGreaterStrategyNumber)
		{
			newPath->indexscandir = BackwardScanDirection;
		}

		/* Don't modify the list we're enumerating */
		pathsToAdd = lappend(pathsToAdd, newPath);
	}

	/* Special case: if there were no index paths and
	 * this is a single sort on the _id path, then we can
	 * add a new index path for the _id sort iff it's filtered on shard key.
	 * While we have a FullScan Expr for regular indexes, we don't for _id
	 * so instead we do that logic here.
	 */
	if (isOrderById && list_length(sortDetails) == 1 &&
		!hasIndexPaths && context->plannerOrderByData.shardKeyEqualityExpr != NULL)
	{
		SortIndexInputDetails *sortDetailsInput = linitial(sortDetails);
		IndexOptInfo *primaryKeyIndex = GetPrimaryKeyIndexOptInfo(rel);

		if (primaryKeyIndex != NULL)
		{
			ScanDirection scanDir =
				SortPathKeyStrategy(sortDetailsInput->sortPathKey) ==
				BTGreaterStrategyNumber ?
				BackwardScanDirection : ForwardScanDirection;

			IndexClause *shard_key_clause =
				BuildPointReadIndexClause(
					context->plannerOrderByData.shardKeyEqualityExpr, 0);
			List *indexClauses = list_make1(shard_key_clause);
			IndexPath *primaryKeyPath = create_index_path(
				root, primaryKeyIndex, indexClauses,
				NIL /* yb_bitmap_idx_pushdowns */,
				NIL, NIL, NIL, scanDir, false, NULL,
				1, false,
				NULL);	/* yb_merge_scan_append_saop_cols */
			primaryKeyPath->path.pathkeys = list_make1(sortDetailsInput->sortPathKey);
			pathsToAdd = lappend(pathsToAdd, primaryKeyPath);
		}
	}

	list_free_deep(sortDetails);

	foreach(cell, pathsToAdd)
	{
		/* now add the new paths */
		Path *newPath = lfirst(cell);
		add_path(rel, newPath);
	}
}


static void
ProcessOrderByStatements(PlannerInfo *root,
						 IndexPath *path, int32_t minOrderByColumn,
						 int32_t maxOrderByColumn, bool isMultiKeyIndex,
						 const char *queryOrderPaths[INDEX_MAX_KEYS],
						 bool equalityPrefixes[INDEX_MAX_KEYS],
						 bool nonEqualityPrefixes[INDEX_MAX_KEYS],
						 int32_t pathSortOrders[INDEX_MAX_KEYS])
{
	int i = 0, sortDetailsIndex = 0;

	bool hasOrderBy = false;
	bool hasGroupby = false;
	bool isOrderById = false;
	List *sortDetails = GetSortDetails(root, path->path.parent->relid, &hasOrderBy,
									   &hasGroupby, &isOrderById);

	if (list_length(sortDetails) == 0)
	{
		return;
	}

	if (isMultiKeyIndex && hasGroupby)
	{
		/* We can't push down orderby on a multikey index if there is a group by */
		list_free_deep(sortDetails);
		return;
	}

	List *indexOrderBys = NIL;
	List *indexPathKeys = NIL;
	List *indexOrderbyCols = NIL;
	int32_t determinedSortOrder = 0;
	for (; i < minOrderByColumn; i++)
	{
		if (!equalityPrefixes[i])
		{
			/* No orderby on the column */
			list_free_deep(sortDetails);
			return;
		}
	}

	for (i = minOrderByColumn; i <= maxOrderByColumn; i++)
	{
		if (isMultiKeyIndex)
		{
			/* For a multi-key index, all order by related paths must have no filter specifications */
			if (nonEqualityPrefixes[i] || equalityPrefixes[i])
			{
				break;
			}
		}

		/* From this point, onwards, each path must either have an order or a valid filter
		 * for the path.
		 */
		if (pathSortOrders[i] != 0)
		{
			/* This path has an order by */
			if (determinedSortOrder == 0)
			{
				determinedSortOrder = pathSortOrders[i];
			}
			else if (pathSortOrders[i] != determinedSortOrder)
			{
				/* Can no longer push any further orderby to this index */
				break;
			}

			if (determinedSortOrder < 0 && !EnableIndexOrderByReverse)
			{
				continue;
			}

			if (determinedSortOrder < 0 &&
				!(IsClusterVersionAtleast(DocDB_V0, 107, 0) ||
				  IsClusterVersionAtLeastPatch(DocDB_V0, 106, 1)))
			{
				break;
			}

			SortIndexInputDetails *sortDetailsInput =
				(SortIndexInputDetails *) list_nth(sortDetails, sortDetailsIndex);

			if (strcmp(sortDetailsInput->sortPath, queryOrderPaths[i]) != 0)
			{
				/* The order by path does not match the index path */
				break;
			}

			sortDetailsIndex++;

			/* Path sort order matches the currently determined index sort order */
			/* Now we've reached the first orderby */
			Oid indexOperator = pathSortOrders[i] < 0 ?
								BsonOrderByReverseIndexOperatorId() :
								BsonOrderByIndexOperatorId();
			Expr *orderElement = make_opclause(
				indexOperator, BsonTypeId(), false,
				(Expr *) sortDetailsInput->sortVar,
				(Expr *) sortDetailsInput->sortDatum,
				InvalidOid, InvalidOid);
			indexOrderBys = lappend(indexOrderBys, orderElement);
			indexPathKeys = lappend(indexPathKeys, sortDetailsInput->sortPathKey);
			indexOrderbyCols = lappend_int(indexOrderbyCols, 0);
		}
		else if (!equalityPrefixes[i])
		{
			/* No order by on this column but we're less than the maxOrderBy.
			 * If we don't have an equality prefix, this is no longer valid
			 * for orderby
			 */
			break;
		}
	}

	path->indexorderbys = indexOrderBys;
	path->indexorderbycols = indexOrderbyCols;
	path->path.pathkeys = indexPathKeys;

	list_free_deep(sortDetails);
}


static bool
PopulateQueryPathAndValueFromOpExpr(OpExpr *opExpr, const char **queryPathString,
									bson_value_t *queryValue)
{
	Expr *queryVal = lsecond(opExpr->args);
	queryValue->value_type = BSON_TYPE_EOD;
	if (IsA(queryVal, Const))
	{
		Const *queryConst = (Const *) queryVal;
		pgbson *queryBson = DatumGetPgBson(queryConst->constvalue);

		pgbsonelement queryElement;
		PgbsonToSinglePgbsonElement(queryBson, &queryElement);
		*queryPathString = queryElement.path;
		*queryValue = queryElement.bsonValue;
		return true;
	}
	else if (IsA(queryVal, FuncExpr) && EnableExprLookupIndexPushdown)
	{
		FuncExpr *funcExpr = (FuncExpr *) queryVal;
		if (funcExpr->funcid ==
			DocumentDBApiInternalBsonLookupExtractFilterExpressionFunctionOid() &&
			list_length(funcExpr->args) >= 2)
		{
			Expr *secondArg = lsecond(funcExpr->args);
			if (IsA(secondArg, Const) && !castNode(Const, secondArg)->constisnull)
			{
				Const *secondConst = (Const *) secondArg;

				pgbsonelement queryElement;
				PgbsonToSinglePgbsonElementWithCollation(DatumGetPgBson(
															 secondConst->
															 constvalue),
														 &queryElement);
				*queryPathString = queryElement.path;
				return true;
			}
		}
		else if (funcExpr->funcid == BsonDollarMergeExtractFilterFunctionOid() &&
				 list_length(funcExpr->args) >= 2)
		{
			Expr *secondArg = lsecond(funcExpr->args);
			if (IsA(secondArg, Const) && !castNode(Const, secondArg)->constisnull)
			{
				Const *secondConst = (Const *) secondArg;
				*queryPathString = TextDatumGetCString(secondConst->constvalue);
				return true;
			}
		}
		else if (funcExpr->funcid == BsonExpressionGetWithLetFunctionOid() &&
				 list_length(funcExpr->args) >= 4)
		{
			Expr *secondArg = lsecond(funcExpr->args);
			if (IsA(secondArg, Const) && !castNode(Const, secondArg)->constisnull)
			{
				Const *thirdConst = (Const *) secondArg;

				pgbsonelement queryElement;
				PgbsonToSinglePgbsonElementWithCollation(DatumGetPgBson(
															 thirdConst->
															 constvalue),
														 &queryElement);
				*queryPathString = queryElement.path;
				return true;
			}
		}
	}

	*queryPathString = NULL;
	return false;
}


static int32_t
UpdateEqualityPrefixesAndGetSortOrder(const char *queryPath, bytea *opClassOptions,
									  OpExpr *expr, bool isPartialFilterExpr,
									  bson_value_t *optionalQueryValue,
									  bool equalityPrefixes[INDEX_MAX_KEYS],
									  bool nonEqualityPrefixes[INDEX_MAX_KEYS],
									  int32_t *outputColumnNumber,
									  int8_t *indexSortDirection)
{
	int columnNumber = GetCompositeOpClassColumnNumber(queryPath,
													   opClassOptions,
													   indexSortDirection);

	*outputColumnNumber = columnNumber;

	/* Collect orderby clauses here */
	if (columnNumber < 0)
	{
		return 0;
	}

	int32_t orderScanDirection = 0;
	const MongoIndexOperatorInfo *info =
		GetMongoIndexOperatorByPostgresOperatorId(expr->opno);
	switch (info->indexStrategy)
	{
		case BSON_INDEX_STRATEGY_DOLLAR_EQUAL:
		{
			equalityPrefixes[columnNumber] = true;
			break;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL:
		{
			/* this is not a full scan (only exists: true is allowed) */
			if (optionalQueryValue->value_type != BSON_TYPE_MINKEY)
			{
				nonEqualityPrefixes[columnNumber] = true;
			}

			break;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_LESS_EQUAL:
		{
			/* this is not a full scan (only <= MaxKey is allowed) */
			if (optionalQueryValue->value_type != BSON_TYPE_MAXKEY)
			{
				nonEqualityPrefixes[columnNumber] = true;
			}

			break;
		}

		case BSON_INDEX_STRATEGY_INVALID:
		{
			if (expr->opno == BsonRangeMatchOperatorOid() &&
				optionalQueryValue->value_type != BSON_TYPE_EOD)
			{
				DollarRangeParams rangeParams = { 0 };
				InitializeQueryDollarRange(optionalQueryValue, &rangeParams);
				if (!rangeParams.isFullScan)
				{
					nonEqualityPrefixes[columnNumber] = true;
				}

				orderScanDirection = rangeParams.orderScanDirection;
			}
			else if (isPartialFilterExpr && expr->opno ==
					 BsonEqualMatchRuntimeOperatorId())
			{
				equalityPrefixes[columnNumber] = true;
			}
			else
			{
				nonEqualityPrefixes[columnNumber] = true;
			}

			break;
		}

		default:
		{
			/* Track the filters as being a non-equality (range predicate) */
			nonEqualityPrefixes[columnNumber] = true;
			break;
		}
	}

	return orderScanDirection;
}


/*
 * Some of the quals for order by can come from the PFE:
 * Consider a case where you have an index (a, b, c) with a pfe b = 1
 * where you have a = 1, b = 1, order by c. b = 1 gets stripped since it
 * matches the PFE exactly, so this code only sees a = 1, fullscan(c).
 * This should be considered valid for order by since the PFE covers the
 * missing column. This is tracked by walking the PFE here.
 *
 * Similarly, consider the index (a) with pfe a = 1.
 * While this may seem like a corner case, this is still valid, and in this
 * case we can push down to the index as the first column is found from the PFE.
 * Or an index with (a) with PFE a > 1 where teh query predicate is a > 1. Similarly
 * we need to walk the PFEs to ensure we capture whether the first path is specified.
 */
static bool
ProcessCompositePartialFilter(List *indexPredicate, bytea *opClassOptions,
							  bool equalityPrefixes[INDEX_MAX_KEYS],
							  bool nonEqualityPrefixes[INDEX_MAX_KEYS])
{
	ListCell *cell;
	bool hasFirstPathSpecified = false;
	foreach(cell, indexPredicate)
	{
		Node *predQual = (Node *) lfirst(cell);

		/* walk the index predicates and check if they match the index */
		if (!IsA(predQual, OpExpr))
		{
			continue;
		}

		OpExpr *expr = (OpExpr *) predQual;

		const char *queryPath = NULL;
		bson_value_t optionalQueryValue = { 0 };
		if (!PopulateQueryPathAndValueFromOpExpr(expr, &queryPath, &optionalQueryValue))
		{
			continue;
		}

		if (queryPath == NULL)
		{
			continue;
		}

		int columnNumber = -1;
		int8_t indexSortDirection = -1;
		bool isPartialFilterExpr = true;
		UpdateEqualityPrefixesAndGetSortOrder(
			queryPath, opClassOptions, expr, isPartialFilterExpr,
			&optionalQueryValue, equalityPrefixes, nonEqualityPrefixes, &columnNumber,
			&indexSortDirection);

		if (columnNumber == 0)
		{
			hasFirstPathSpecified = true;
		}
	}

	return hasFirstPathSpecified;
}


bool
TraverseIndexPathForCompositeIndex(struct IndexPath *indexPath, struct PlannerInfo *root)
{
	ListCell *cell;
	bool firstFilterColumnFound = false;
	bool indexCanOrder = false;
	bool isMultiKeyIndex = false;
	GetMultikeyStatusFunc getMultiKeyStatusFunc = GetMultiKeyStatusByRelAm(
		indexPath->indexinfo->relam);

	if (getMultiKeyStatusFunc != NULL &&
		indexPath->indexinfo->amcanorderbyop &&
		EnableIndexOrderbyPushdown &&
		list_length(root->query_pathkeys) > 0)
	{
		Relation indexRel = index_open(indexPath->indexinfo->indexoid, NoLock);
		isMultiKeyIndex = getMultiKeyStatusFunc(indexRel);
		index_close(indexRel, NoLock);
	}

	bool indexSupportsOrderByDesc = GetIndexSupportsBackwardsScan(
		indexPath->indexinfo->relam, &indexCanOrder);

	int32_t pathSortOrders[INDEX_MAX_KEYS] = { 0 };
	bool equalityPrefixes[INDEX_MAX_KEYS] = { false };
	bool nonEqualityPrefixes[INDEX_MAX_KEYS] = { false };
	const char *queryOrderPaths[INDEX_MAX_KEYS] = { 0 };
	int32_t minOrderByColumn = INT_MAX;
	int32_t maxOrderByColumn = -1;
	List *orderbyIndexClauses = NIL;
	foreach(cell, indexPath->indexclauses)
	{
		IndexClause *clause = (IndexClause *) lfirst(cell);
		ListCell *iclauseCell;
		foreach(iclauseCell, clause->indexquals)
		{
			RestrictInfo *qual = (RestrictInfo *) lfirst(iclauseCell);
			if (!IsA(qual->clause, OpExpr))
			{
				continue;
			}

			OpExpr *expr = (OpExpr *) qual->clause;

			const char *queryPath = NULL;
			bson_value_t optionalQueryValue = { 0 };
			if (!PopulateQueryPathAndValueFromOpExpr(expr, &queryPath,
													 &optionalQueryValue))
			{
				continue;
			}

			if (queryPath == NULL)
			{
				continue;
			}

			int columnNumber = -1;
			int8_t indexSortDirection = 0;
			bool isPartialFilterExpr = false;
			int8_t orderScanDirection = UpdateEqualityPrefixesAndGetSortOrder(
				queryPath, indexPath->indexinfo->opclassoptions[0],
				expr, isPartialFilterExpr, &optionalQueryValue, equalityPrefixes,
				nonEqualityPrefixes, &columnNumber, &indexSortDirection);

			if (columnNumber < 0)
			{
				continue;
			}

			if (orderScanDirection == 0)
			{
				/* Found a filter path */
				if (columnNumber == 0)
				{
					firstFilterColumnFound = true;
				}

				continue;
			}

			bool currentPathKeyIsReverseSort = orderScanDirection != indexSortDirection;
			if (currentPathKeyIsReverseSort && !indexSupportsOrderByDesc)
			{
				continue;
			}

			pathSortOrders[columnNumber] = currentPathKeyIsReverseSort ? -1 : 1;
			queryOrderPaths[columnNumber] = queryPath;
			minOrderByColumn = Min(minOrderByColumn, columnNumber);
			maxOrderByColumn = Max(maxOrderByColumn, columnNumber);
			orderbyIndexClauses = lappend(orderbyIndexClauses, clause);
		}
	}

	if (EnableUnifyPfeOnIndexInfo &&
		indexPath->indexinfo->indpred != NIL)
	{
		if (ProcessCompositePartialFilter(
				indexPath->indexinfo->indpred,
				indexPath->indexinfo->opclassoptions[0],
				equalityPrefixes, nonEqualityPrefixes))
		{
			firstFilterColumnFound = true;
		}
	}

	/* One final pass to add the appropriate order by clauses to the index path */
	if (indexCanOrder && maxOrderByColumn >= 0)
	{
		ProcessOrderByStatements(root, indexPath, minOrderByColumn,
								 maxOrderByColumn, isMultiKeyIndex,
								 queryOrderPaths, equalityPrefixes,
								 nonEqualityPrefixes, pathSortOrders);

		/* Trim the order by clauses from the index if there's filters */
		if (firstFilterColumnFound)
		{
			foreach(cell, orderbyIndexClauses)
			{
				IndexClause *clause = lfirst(cell);
				if (list_length(indexPath->indexclauses) <= 1)
				{
					/* Don't delete the last clause */
					break;
				}

				indexPath->indexclauses = list_delete_ptr(indexPath->indexclauses,
														  clause);
			}
		}

		list_free(orderbyIndexClauses);
	}

	/* Valid if we pushed some order by or a filter path was found on at least the first column */
	return firstFilterColumnFound || indexPath->indexorderbys != NIL;
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
		if (req->funcid == BsonFullScanFunctionOid())
		{
			/* Process this separate for orderby */
			return ProcessFullScanForOrderBy(req, args);
		}

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
		if (!IsTextPathOpFamilyOid(req->index->relam, operatorFamily))
		{
			return NULL;
		}

		Expr *finalExpression =
			(Expr *) GetOpExprClauseFromIndexOperator(operator, args, options);
		return finalExpression;
	}

	if (operator->indexStrategy == BSON_INDEX_STRATEGY_DOLLAR_ELEMMATCH &&
		(IsCompositeOpFamilyOid(req->index->relam, operatorFamily) ||
		 UseNewElemMatchIndexPushdown))
	{
		Expr *elemMatchExpr = ProcessElemMatchOperator(options, queryValue, operator,
													   args);
		if (elemMatchExpr != NULL)
		{
			req->lossy = true;
			return elemMatchExpr;
		}

		return NULL;
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


IndexPath *
TrimIndexRestrictInfoForBtreePath(PlannerInfo *root, IndexPath *indexPath,
								  bool *hasNonIdClauses)
{
	List *clauseRestrictInfos = NIL;
	List *objectIdClauses = NIL;
	ListCell *cell;
	bool hasOtherClauses = false;
	foreach(cell, indexPath->indexclauses)
	{
		IndexClause *clause = lfirst(cell);
		clauseRestrictInfos = lappend(clauseRestrictInfos, clause->rinfo);
		if (clause->indexcol == 1)
		{
			objectIdClauses = lappend(objectIdClauses, clause->rinfo->clause);
		}
	}

	/* Now walk the btree index restrict info for a match */
	List *restrictInfosToRemove = NIL;
	List *additionalIndexClauses = NIL;
	foreach(cell, indexPath->indexinfo->indrestrictinfo)
	{
		RestrictInfo *rinfo = lfirst(cell);
		if (list_member(clauseRestrictInfos, rinfo))
		{
			continue;
		}

		if (!IsA(rinfo->clause, OpExpr))
		{
			hasOtherClauses = true;
			continue;
		}

		OpExpr *clauseExpr = (OpExpr *) rinfo->clause;
		if (list_length(clauseExpr->args) != 2)
		{
			hasOtherClauses = true;
			continue;
		}

		if (!IsA(linitial(clauseExpr->args), Var) ||
			(castNode(Var, linitial(clauseExpr->args))->varattno !=
			 DOCUMENT_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER) ||
			!IsA(lsecond(clauseExpr->args), Const))
		{
			hasOtherClauses = true;
			continue;
		}

		Var *firstVar = linitial(clauseExpr->args);
		Const *secondConst = lsecond(clauseExpr->args);
		pgbson *qual = DatumGetPgBson(secondConst->constvalue);

		pgbsonelement qualElement;
		const char *collation = PgbsonToSinglePgbsonElementWithCollation(qual,
																		 &qualElement);
		if (collation != NULL)
		{
			hasOtherClauses = true;
			continue;
		}

		if (qualElement.pathLength != 3 || strcmp(qualElement.path, "_id") != 0)
		{
			hasOtherClauses = true;
			continue;
		}

		const MongoIndexOperatorInfo *indexOp = GetMongoIndexOperatorByPostgresOperatorId(
			clauseExpr->opno);

		Expr *primaryKeyExpr = NULL;
		Expr *secondaryKeyExpr = NULL;
		switch (indexOp->indexStrategy)
		{
			case BSON_INDEX_STRATEGY_DOLLAR_GREATER:
			{
				primaryKeyExpr = MakeSimpleIdExpr(&qualElement.bsonValue, firstVar->varno,
												  BsonGreaterThanOperatorId());
				secondaryKeyExpr = MakeUpperBoundIdExpr(&qualElement.bsonValue,
														firstVar->varno);
				break;
			}

			case BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL:
			{
				primaryKeyExpr = MakeSimpleIdExpr(&qualElement.bsonValue, firstVar->varno,
												  BsonGreaterThanEqualOperatorId());
				secondaryKeyExpr = MakeUpperBoundIdExpr(&qualElement.bsonValue,
														firstVar->varno);
				break;
			}

			case BSON_INDEX_STRATEGY_DOLLAR_LESS:
			{
				primaryKeyExpr = MakeSimpleIdExpr(&qualElement.bsonValue, firstVar->varno,
												  BsonLessThanOperatorId());
				secondaryKeyExpr = MakeLowerBoundIdExpr(&qualElement.bsonValue,
														firstVar->varno);
				break;
			}

			case BSON_INDEX_STRATEGY_DOLLAR_LESS_EQUAL:
			{
				primaryKeyExpr = MakeSimpleIdExpr(&qualElement.bsonValue, firstVar->varno,
												  BsonLessThanEqualOperatorId());
				secondaryKeyExpr = MakeLowerBoundIdExpr(&qualElement.bsonValue,
														firstVar->varno);
				break;
			}

			case BSON_INDEX_STRATEGY_DOLLAR_EQUAL:
			{
				primaryKeyExpr = MakeSimpleIdExpr(&qualElement.bsonValue, firstVar->varno,
												  BsonEqualOperatorId());
				break;
			}

			default:
			{
				hasOtherClauses = true;
				continue;
			}
		}

		additionalIndexClauses = lappend(additionalIndexClauses, primaryKeyExpr);
		if (secondaryKeyExpr != NULL)
		{
			additionalIndexClauses = lappend(additionalIndexClauses, secondaryKeyExpr);
		}

		restrictInfosToRemove = lappend(restrictInfosToRemove, rinfo);
	}

	list_free(clauseRestrictInfos);
	if (list_length(additionalIndexClauses) == 0)
	{
		*hasNonIdClauses = hasOtherClauses;
		return indexPath;
	}

	IndexPath *indexPathCopy = palloc(sizeof(IndexPath));
	memcpy(indexPathCopy, indexPath, sizeof(IndexPath));

	IndexOptInfo *indexInfoCopy = palloc(sizeof(IndexOptInfo));
	memcpy(indexInfoCopy, indexPath->indexinfo, sizeof(IndexOptInfo));
	indexInfoCopy->indrestrictinfo = list_difference_ptr(indexInfoCopy->indrestrictinfo,
														 restrictInfosToRemove);
	indexPathCopy->indexinfo = indexInfoCopy;

	List *origList = indexPathCopy->indexclauses;
	foreach(cell, additionalIndexClauses)
	{
		Expr *clause = lfirst(cell);
		if (list_member(objectIdClauses, clause))
		{
			continue;
		}

		RestrictInfo *additionalRestrictInfo =
			make_simple_restrictinfo(root, clause);
		IndexClause *singleIndexClause = makeNode(IndexClause);
		singleIndexClause->rinfo = additionalRestrictInfo;
		singleIndexClause->indexquals = list_make1(additionalRestrictInfo);
		singleIndexClause->lossy = false;
		singleIndexClause->indexcol = 1;
		singleIndexClause->indexcols = NIL;

		if (origList == indexPathCopy->indexclauses)
		{
			origList = list_copy(indexPathCopy->indexclauses);
		}

		origList = lappend(origList, singleIndexClause);
	}

	indexPathCopy->indexclauses = origList;
	*hasNonIdClauses = hasOtherClauses;
	return indexPathCopy;
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
		bool isPrimaryKeyIndexPath = false;
		if (IsBtreePrimaryKeyIndex(indexPath->indexinfo) &&
			list_length(indexPath->indexclauses) > 1 &&
			parentType != PARENTTYPE_INVALID)
		{
			context->primaryKeyLookupPath = indexPath;
			isPrimaryKeyIndexPath = true;
		}

		const VectorIndexDefinition *vectorDefinition = NULL;
		if (indexPath->indexorderbys != NIL)
		{
			/* Only check for vector when there's an order by */
			vectorDefinition = GetVectorIndexDefinitionByIndexAmOid(
				indexPath->indexinfo->relam);
		}

		if (indexPath->indexinfo->indrestrictinfo != NIL && rel->baserestrictinfo == NIL)
		{
			indexPath->indexinfo->indrestrictinfo = NIL;
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
		else if (indexPath->indexinfo->relam == GIST_AM_OID &&
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
		else
		{
			/* RUM/GIST indexes */
			ListCell *indexPathCell;
			foreach(indexPathCell, indexPath->indexclauses)
			{
				IndexClause *iclause = (IndexClause *) lfirst(indexPathCell);
				RestrictInfo *rinfo = iclause->rinfo;
				ReplaceExtensionFunctionContext childContext = { 0 };
				childContext.inputData = context->inputData;
				childContext.forceIndexQueryOpData = context->forceIndexQueryOpData;
				bool trimClauses = false;
				rinfo->clause = ProcessRestrictionInfoAndRewriteFuncExpr(
					rinfo->clause,
					&childContext, trimClauses);
			}

			if (BsonIndexAmRequiresRangeOptimization(indexPath->indexinfo->relam,
													 indexPath->indexinfo->opfamily[0]))
			{
				indexPath->indexclauses = OptimizeIndexExpressionsForRange(
					indexPath->indexclauses);
			}
		}

		indexPath = OptimizeIndexPathForFilters(indexPath, context);

		/* For btree indexscans ensure that we trim alternate quals */
		if (isPrimaryKeyIndexPath &&
			EnableIdIndexPushdown &&
			indexPath->path.pathtype != T_IndexOnlyScan)
		{
			bool hasOtherQualsIgnore = false;
			path = (Path *) TrimIndexRestrictInfoForBtreePath(root, indexPath,
															  &hasOtherQualsIgnore);
		}
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
			if (context->forceIndexQueryOpData.type == ForceIndexOpType_None)
			{
				context->forceIndexQueryOpData.type = ForceIndexOpType_Text;
			}

			if (context->forceIndexQueryOpData.type != ForceIndexOpType_Text)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"Text index pushdown is not supported for this query")));
			}

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
		else if (trimClauses && IsA(clause, FuncExpr))
		{
			FuncExpr *funcExpr = (FuncExpr *) clause;
			if (funcExpr->funcid == BsonFullScanFunctionOid())
			{
				/* Trim these */
				return NULL;
			}
			else if (funcExpr->funcid == BsonIndexHintFunctionOid())
			{
				/* Trim these */
				return NULL;
			}
		}
	}
	else if (IsA(clause, NullTest))
	{
		NullTest *nullTest = (NullTest *) clause;
		CheckNullTestForGeoSpatialForcePushdown(context, nullTest);
	}
	else if (IsA(clause, ScalarArrayOpExpr))
	{
		if (context->inputData.isShardQuery && trimClauses)
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
OpExpr *
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

	if (EnableUnifyPfeOnIndexInfo)
	{
		/*
		 * If there's a consideration of a bitmap path,
		 * then the PFE can get added as a bitmap qual.
		 * In order to ensure we don't get extra runtime filters,
		 * ensure the structure of the filters on the indexOptInfo
		 * is the same as the one in the index quals.
		 * Do this on a copy of the indexoptinfo to not modify the
		 * one on the base index (in case there's other index paths etc
		 * depending on it).
		 */
		IndexOptInfo *copiedInfo = palloc(sizeof(IndexOptInfo));
		memcpy(copiedInfo, indexPath->indexinfo, sizeof(IndexOptInfo));
		List *processedPred = NIL;
		ListCell *singleCell;
		foreach(singleCell, copiedInfo->indpred)
		{
			Expr *predExpr = (Expr *) lfirst(singleCell);
			if (!IsA(predExpr, OpExpr))
			{
				predExpr = copyObject(predExpr);
			}

			bool trimClauses = true;
			Expr *expr = ProcessRestrictionInfoAndRewriteFuncExpr(predExpr,
																  context, trimClauses);
			if (expr != NULL)
			{
				processedPred = lappend(processedPred, expr);
			}
		}

		copiedInfo->indpred = processedPred;
		indexPath->indexinfo = copiedInfo;
	}

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
		if (IsBsonRegularIndexAm(index->relam) &&
			index->nkeycolumns > 0)
		{
			for (int i = 0; i < index->nkeycolumns; i++)
			{
				if (IsTextPathOpFamilyOid(index->relam, index->opfamily[i]))
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
 * Enables/disables the force index pushdown for geonear query based on the configuruation
 * setting `enableIndexForGeonear` or checks if the geonear order by clauses are really present
 * in the query.
 */
static bool
EnableGeoNearForceIndexPushdown(PlannerInfo *root,
								ReplaceExtensionFunctionContext *context)
{
	if (EnableGeonearForceIndexPushdown)
	{
		/* Geonear with no geonear operator (other geo operators) should not force geo index */
		return TryFindGeoNearOpExpr(root, context);
	}

	return false;
}


static bool
DefaultTrueForceIndexPushdown(PlannerInfo *root, ReplaceExtensionFunctionContext *context)
{
	return true;
}


static bool
DefaultFalseForceIndexPushdown(PlannerInfo *root,
							   ReplaceExtensionFunctionContext *context)
{
	return false;
}


/*
 * Matches the indexPath for $text query. It just checks if the index used
 * is a text index, as there can only be at max one text index for a collection.
 */
static bool
MatchIndexPathForText(IndexPath *indexPath, void *matchContext)
{
	if (IsBsonRegularIndexAm(indexPath->indexinfo->relam) &&
		indexPath->indexinfo->ncolumns > 0)
	{
		for (int ind = 0; ind < indexPath->indexinfo->ncolumns; ind++)
		{
			if (IsTextPathOpFamilyOid(indexPath->indexinfo->relam,
									  indexPath->indexinfo->opfamily[ind]))
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
					errmsg("A text index is necessary to perform a $text query.")));
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


static List *
UpdateIndexListForIndexHint(List *existingIndex,
							ReplaceExtensionFunctionContext *context)
{
	/* Trim all indexes except those that match the hint */
	const IndexHintMatchContext *hintContext = (const
												IndexHintMatchContext *) context->
											   forceIndexQueryOpData.opExtraState;
	List *newIndexesListForHint = NIL;
	ListCell *indexCell;
	foreach(indexCell, existingIndex)
	{
		bool useLibPq = false;
		IndexOptInfo *index = lfirst_node(IndexOptInfo, indexCell);
		const char *docdbIndexName = ExtensionIndexOidGetIndexName(index->indexoid,
																   useLibPq);
		if (docdbIndexName == NULL)
		{
			continue;
		}

		if (strcmp(docdbIndexName, hintContext->documentDBIndexName) == 0)
		{
			newIndexesListForHint = lappend(newIndexesListForHint, index);
		}
	}

	return newIndexesListForHint;
}


static bool
MatchIndexPathForIndexHint(IndexPath *path, void *matchContext)
{
	const IndexHintMatchContext *context = (const IndexHintMatchContext *) matchContext;
	bool useLibPq = false;
	const char *docdbIndexName = ExtensionIndexOidGetIndexName(path->indexinfo->indexoid,
															   useLibPq);

	if (docdbIndexName == NULL)
	{
		return false;
	}

	/*
	 * Given that we force this index down we update the cost for it to be 0.
	 * In theory this is not needed since this is the only path available.
	 * However, this raised an issue where for RUM, we set the cost to INFINITY.
	 * In explain this is logged as cost: Infinity (without quotes) which breaks
	 * some Json parsers. To not have that happen for selected paths, we explicitly
	 * also set the costs to 0.
	 */
	bool isMatch = (strcmp(docdbIndexName, context->documentDBIndexName) == 0);
	if (isMatch)
	{
		path->indextotalcost = 0;
		path->path.total_cost = 0;
		path->path.startup_cost = 0;
	}

	return isMatch;
}


static bool
TryUseAlternateIndexForIndexHint(PlannerInfo *root, RelOptInfo *rel,
								 ReplaceExtensionFunctionContext *context,
								 MatchIndexPath matchIndexPath)
{
	IndexHintMatchContext *hintContext =
		(IndexHintMatchContext *) context->forceIndexQueryOpData.opExtraState;

	IndexOptInfo *matchedInfo = NULL;
	if (list_length(rel->indexlist) < 1)
	{
		return false;
	}

	matchedInfo = linitial(rel->indexlist);

	/* Non composite op classes do not support fullscan operators */
	const char *firstIndexPath = NULL;

	if (matchedInfo->unique && matchedInfo->nkeycolumns == 2 &&
		matchedInfo->relam == BTREE_AM_OID)
	{
		/* This will be the primary key Btree create an empty scan on it */
		IndexPath *newPath = create_index_path(root, matchedInfo,
												NIL /* yb_bitmap_idx_pushdowns */,
											    NIL, NIL, NIL, NIL,
											   ForwardScanDirection, false, NULL, 1,
											   false,
											   NULL);	/* yb_merge_scan_append_saop_cols */
		add_path(rel, (Path *) newPath);
		return true;
	}

	int indexCol = 0;
	bool isHashedIndex = false;
	bool isWildCardIndex = false;
	if (IsBsonRegularIndexAm(matchedInfo->relam))
	{
		bytea *opClassOptions = matchedInfo->opclassoptions[0];
		if (IsUniqueCheckOpFamilyOid(matchedInfo->relam, matchedInfo->opfamily[0]))
		{
			/* For unique indexes, the first column is the shard key constraint */
			opClassOptions = matchedInfo->opclassoptions[1];
			indexCol = 1;
		}

		isHashedIndex = IsHashedPathOpFamilyOid(
			matchedInfo->relam, matchedInfo->opfamily[indexCol]);

		if (opClassOptions != NULL)
		{
			firstIndexPath = GetFirstPathFromIndexOptionsIfApplicable(
				opClassOptions, &isWildCardIndex);
		}
	}

	if (firstIndexPath == NULL || isWildCardIndex)
	{
		/* For hashed indexes, we don't support pushing down a full scan
		 * TODO: Support that. But in the interim for this unsupported index thunk to
		 * SeqScan.
		 * TODO: Should we do this for all unsupported cases (e.g. geospatial)
		 */
		if (isHashedIndex)
		{
			Path *seqscan = create_seqscan_path(root, rel, NULL, 0);
			add_path(rel, seqscan);
			return true;
		}

		return false;
	}

	/* For Sparse indexes with hint, we create an { exists: true } clause */
	OpExpr *scanClause;
	if (hintContext->isSparse)
	{
		scanClause = CreateExistsTrueOpExpr(
			hintContext->documentExpr,
			firstIndexPath, strlen(firstIndexPath));
	}
	else
	{
		int32_t orderByScanDirectionNone = 0;
		scanClause = CreateFullScanOpExpr(
			hintContext->documentExpr,
			firstIndexPath, strlen(firstIndexPath), orderByScanDirectionNone);
	}

	RestrictInfo *fullScanRestrictInfo =
		make_simple_restrictinfo(root, (Expr *) scanClause);
	IndexClause *singleIndexClause = makeNode(IndexClause);
	singleIndexClause->rinfo = fullScanRestrictInfo;
	singleIndexClause->indexquals = list_make1(fullScanRestrictInfo);
	singleIndexClause->lossy = false;
	singleIndexClause->indexcol = indexCol;
	singleIndexClause->indexcols = NIL;

	List *indexClauses = list_make1(singleIndexClause);
	IndexPath *newPath = create_index_path(root, matchedInfo, indexClauses,
										   NIL /* yb_bitmap_idx_pushdowns */,
										   NIL, NIL, NIL,
										   ForwardScanDirection, false, NULL, 1,
										   false,
										   NULL);	/* yb_merge_scan_append_saop_cols */

	/* See comment as well in MatchIndexPathForIndexHint */
	newPath->indextotalcost = 0;
	newPath->path.total_cost = 0;
	newPath->path.startup_cost = 0;
	add_path(rel, (Path *) newPath);
	return true;
}


static void
ThrowIndexHintUnableToFindIndex(void)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNABLETOFINDINDEX),
					errmsg(
						"index specified by index hint is not found or invalid for the filters")));
}


static bool
EnableIndexHintForceIndexPushdown(PlannerInfo *root,
								  ReplaceExtensionFunctionContext *context)
{
	return IsClusterVersionAtleast(DocDB_V0, 106, 0);
}


static List *
UpdateIndexListForPrimaryKeyLookup(List *existingIndex,
								   ReplaceExtensionFunctionContext *context)
{
	/* This is done in the alternate path scenario */
	return NIL;
}


static bool
MatchIndexPathForPrimaryKeyLookup(IndexPath *path, void *matchContext)
{
	/* TODO: Can we do better here */
	return false;
}


static bool
TryUseAlternateIndexForPrimaryKeyLookup(PlannerInfo *root, RelOptInfo *rel,
										ReplaceExtensionFunctionContext *indexContext,
										MatchIndexPath matchIndexPath)
{
	PrimaryKeyLookupContext *context =
		(PrimaryKeyLookupContext *) indexContext->forceIndexQueryOpData.opExtraState;

	IndexOptInfo *primaryKeyInfo = GetPrimaryKeyIndexOptInfo(rel);
	if (primaryKeyInfo == NULL)
	{
		return false;
	}

	IndexClause *objectIdClause =
		BuildPointReadIndexClause(context->objectId.restrictInfo, 1);

	IndexPath *path = NULL;
	if (context->primaryKeyLookupPath != NULL &&
		IsA(context->primaryKeyLookupPath, IndexPath))
	{
		path = (IndexPath *) context->primaryKeyLookupPath;

		bool indexPathHasEquality = false;
		ListCell *clauseCell;
		foreach(clauseCell, path->indexclauses)
		{
			IndexClause *clause = (IndexClause *) lfirst(clauseCell);
			if (clause->rinfo == context->objectId.restrictInfo)
			{
				indexPathHasEquality = true;
				break;
			}
		}

		if (!indexPathHasEquality)
		{
			path->indexclauses = lappend(path->indexclauses, objectIdClause);
		}
	}
	else
	{
		IndexClause *shardKeyClause =
			BuildPointReadIndexClause(context->shardKeyQualExpr, 0);
		List *clauses = list_make2(shardKeyClause, objectIdClause);
		List *orderbys = NIL;
		List *orderbyCols = NIL;
		List *pathKeys = NIL;
		bool indexOnly = false;
		Relids outerRelids = bms_copy(rel->lateral_relids);

		outerRelids = bms_add_members(outerRelids,
									  context->objectId.restrictInfo->clause_relids);
		if (context->shardKeyQualExpr->clause_relids)
		{
			outerRelids = bms_add_members(outerRelids,
										  context->shardKeyQualExpr->clause_relids);
		}

		outerRelids = bms_del_member(outerRelids, rel->relid);

#if PG_VERSION_NUM < 160000

		/* Enforce convention that outerRelids is exactly NULL if empty */
		if (bms_is_empty(outerRelids))
		{
			outerRelids = NULL;
		}
#endif

		double loopCount = 1;
		bool partialPath = false;
		path = create_index_path(root, primaryKeyInfo, clauses,
								 NIL /* yb_bitmap_idx_pushdowns */,
								 orderbys,
								 orderbyCols, pathKeys,
								 ForwardScanDirection, indexOnly,
								 outerRelids,
								 loopCount, partialPath,
								 NULL);	/* yb_merge_scan_append_saop_cols */
	}

	path->indextotalcost = 0;
	path->path.startup_cost = 0;
	path->path.total_cost = 0;

	/* Set cardinality for primary key lookup */
	if (context->objectId.isPrimaryKeyEquality)
	{
		path->path.rows = 1;
	}

	add_path(rel, (Path *) path);

	/* Trim the runtime expr if available */
	ListCell *runtimeCell;
	if (context->objectId.equalityBsonValue.value_type != BSON_TYPE_EOD)
	{
		foreach(runtimeCell, context->runtimeEqualityRestrictionData)
		{
			RuntimePrimaryKeyRestrictionData *equalityRestrictionData =
				(RuntimePrimaryKeyRestrictionData *) lfirst(runtimeCell);
			if (equalityRestrictionData->restrictInfo != NULL &&
				context->objectId.equalityBsonValue.value_type != BSON_TYPE_EOD &&
				BsonValueEquals(&context->objectId.equalityBsonValue,
								&equalityRestrictionData->value))
			{
				rel->baserestrictinfo = list_delete_ptr(rel->baserestrictinfo,
														equalityRestrictionData->
														restrictInfo);
			}
		}
	}
	else if (IsA(context->objectId.restrictInfo->clause, ScalarArrayOpExpr))
	{
		foreach(runtimeCell, context->runtimeDollarInRestrictionData)
		{
			RuntimePrimaryKeyRestrictionData *equalityRestrictionData =
				(RuntimePrimaryKeyRestrictionData *) lfirst(runtimeCell);
			if (equalityRestrictionData->restrictInfo != NULL &&
				IsA(context->objectId.restrictInfo->clause, ScalarArrayOpExpr) &&
				InMatchIsEquvalentTo(
					(ScalarArrayOpExpr *) context->objectId.restrictInfo->clause,
					&equalityRestrictionData->value))
			{
				rel->baserestrictinfo = list_delete_ptr(rel->baserestrictinfo,
														equalityRestrictionData->
														restrictInfo);
			}
		}
	}

	list_free_deep(context->runtimeDollarInRestrictionData);
	list_free_deep(context->runtimeEqualityRestrictionData);
	return true;
}


static void
PrimaryKeyLookupUnableToFindIndex(void)
{
	/* Do nothing and fall back to current behavior/logic */
}


static bool
IsSupportedElemMatchExpr(Node *elemMatchExpr, bytea *options,
						 const MongoIndexOperatorInfo **targetOperator,
						 List **innerOpArgs, pgbsonelement *innerQueryElement)
{
	List *innerArgs;
	const MongoIndexOperatorInfo *innerOperator = GetMongoIndexQueryOperatorFromNode(
		elemMatchExpr,
		&innerArgs);
	if (innerOperator == NULL ||
		innerOperator->indexStrategy == BSON_INDEX_STRATEGY_INVALID)
	{
		/* This is not a valid operator for elemMatch */
		return false;
	}

	if (innerOperator->indexStrategy == BSON_INDEX_STRATEGY_DOLLAR_ELEMMATCH ||
		IsNegationStrategy(innerOperator->indexStrategy))
	{
		/* We don't support negation strategies for nested elemMatch
		 * TODO(Composite): Can we do this safely?
		 */
		return false;
	}

	Node *operand = lsecond(innerArgs);
	Datum innerQueryValue = ((Const *) operand)->constvalue;

	/* Check if the index is valid for the function */
	if (!ValidateIndexForQualifierValue(options, innerQueryValue,
										innerOperator->indexStrategy))
	{
		return false;
	}

	/* Since $eq can fail to traverse array of array paths, elemMatch pushdown cannot handle
	 * this since we need to skip the recheck.
	 * TODO: If we can get the recheck skipped here, we can support this here too.
	 */
	pgbsonelement queryElement;
	PgbsonToSinglePgbsonElement(DatumGetPgBson(innerQueryValue), &queryElement);
	StringView queryPath = {
		.string = queryElement.path,
		.length = queryElement.pathLength
	};
	if (PathHasArrayIndexElements(&queryPath))
	{
		/* We don't support array index elements in elemMatch */
		return false;
	}

	if (innerOperator->indexStrategy == BSON_INDEX_STRATEGY_DOLLAR_TEXT)
	{
		return false;
	}

	*targetOperator = innerOperator;
	*innerOpArgs = innerArgs;
	*innerQueryElement = queryElement;
	return true;
}


static void
WalkExprAndAddSupportedElemMatchExprsNew(List *clauses, bytea *options,
										 IndexElemmatchState *elemMatchState, const
										 char *topLevelPath)
{
	CHECK_FOR_INTERRUPTS();
	check_stack_depth();

	ListCell *elemMatchCell;
	foreach(elemMatchCell, clauses)
	{
		Node *elemMatchExpr = (Node *) lfirst(elemMatchCell);

		if (IsA(elemMatchCell, BoolExpr))
		{
			BoolExpr *boolExpr = (BoolExpr *) elemMatchExpr;
			if (boolExpr->boolop != AND_EXPR)
			{
				/* We only support $elemMatch with AND expressions */
				continue;
			}

			WalkExprAndAddSupportedElemMatchExprsNew(
				boolExpr->args, options, elemMatchState, topLevelPath);
			continue;
		}


		List *innerArgs = NIL;
		const MongoIndexOperatorInfo *innerOperator;
		pgbsonelement queryElement;
		if (!IsSupportedElemMatchExpr(elemMatchExpr, options, &innerOperator, &innerArgs,
									  &queryElement))
		{
			continue;
		}

		if (elemMatchState->indexPath == NULL)
		{
			elemMatchState->indexPath = queryElement.path;
			elemMatchState->indexPathLength = queryElement.pathLength;
			elemMatchState->isTopLevel = strcmp(topLevelPath, queryElement.path) == 0;
		}
		else if (elemMatchState->indexPathLength != queryElement.pathLength ||
				 strncmp(elemMatchState->indexPath, queryElement.path,
						 queryElement.pathLength) != 0)
		{
			continue;
		}

		pgbson_writer qualWriter;
		PgbsonArrayWriterStartDocument(elemMatchState->stateWriter, &qualWriter);
		PgbsonWriterAppendInt32(&qualWriter, "op", 2, innerOperator->indexStrategy);
		PgbsonWriterAppendValue(&qualWriter, "value", 5, &queryElement.bsonValue);
		PgbsonWriterAppendBool(&qualWriter, "isTopLevel", 10, elemMatchState->isTopLevel);
		PgbsonArrayWriterEndDocument(elemMatchState->stateWriter, &qualWriter);
	}
}


static List *
WalkExprAndAddSupportedElemMatchExprs(List *clauses, bytea *options)
{
	CHECK_FOR_INTERRUPTS();
	check_stack_depth();

	List *matchedArgs = NIL;
	ListCell *elemMatchCell;
	foreach(elemMatchCell, clauses)
	{
		Node *elemMatchExpr = (Node *) lfirst(elemMatchCell);

		if (IsA(elemMatchCell, BoolExpr))
		{
			BoolExpr *boolExpr = (BoolExpr *) elemMatchExpr;
			if (boolExpr->boolop != AND_EXPR)
			{
				/* We only support $elemMatch with AND expressions */
				continue;
			}

			List *nestedExprs = WalkExprAndAddSupportedElemMatchExprs(
				boolExpr->args, options);
			matchedArgs = list_concat(matchedArgs, nestedExprs);
			continue;
		}

		List *innerArgs = NIL;
		const MongoIndexOperatorInfo *innerOperator;
		pgbsonelement queryElement;
		if (!IsSupportedElemMatchExpr(elemMatchExpr, options, &innerOperator, &innerArgs,
									  &queryElement))
		{
			continue;
		}

		Expr *finalExpression =
			(Expr *) GetOpExprClauseFromIndexOperator(innerOperator, innerArgs,
													  options);
		matchedArgs = lappend(matchedArgs, finalExpression);
	}

	return matchedArgs;
}


static Expr *
GetElemMatchIndexPushdownOperator(Expr *documentExpr, pgbsonelement *queryElement)
{
	/* In this path, we write the elemMatch as a simple $elemMatch opExpr
	 * with a opExpr format:
	 * "path": { "elemMatchIndexOp": [ { "op": INDEX_STRATEGY, "value": BSON } ] }
	 */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_writer elemMatchWriter;
	PgbsonWriterStartDocument(&writer, queryElement->path, queryElement->pathLength,
							  &elemMatchWriter);
	PgbsonWriterAppendValue(&elemMatchWriter, "elemMatchIndexOp", 16,
							&queryElement->bsonValue);
	PgbsonWriterEndDocument(&writer, &elemMatchWriter);

	Const *bsonConst = makeConst(BsonTypeId(), -1, InvalidOid, -1, PointerGetDatum(
									 PgbsonWriterGetPgbson(&writer)), false,
								 false);
	return (Expr *) make_opclause(BsonRangeMatchOperatorOid(), BOOLOID, false,
								  documentExpr, (Expr *) bsonConst, InvalidOid,
								  InvalidOid);
}


static Expr *
ProcessElemMatchOperator(bytea *options, Datum queryValue, const
						 MongoIndexOperatorInfo *operator, List *args)
{
	pgbson *queryBson = DatumGetPgBson(queryValue);
	pgbsonelement argElement = { 0 };
	PgbsonToSinglePgbsonElement(queryBson, &argElement);


	BsonQueryOperatorContext context = { 0 };
	BsonQueryOperatorContextCommonBuilder(&context);
	context.documentExpr = linitial(args);

	/* Convert the pgbson query into a query AST that processes bson */
	Expr *expr = CreateQualForBsonExpression(&argElement.bsonValue,
											 argElement.path, &context);

	/* Get the underlying list of expressions that are AND-ed */
	List *clauses = make_ands_implicit(expr);

	if (UseNewElemMatchIndexOperatorOnPushdown)
	{
		IndexElemmatchState elemMatchState = { 0 };

		pgbson_writer writer;
		PgbsonWriterInit(&writer);
		pgbson_array_writer arrayWriter;
		PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);
		elemMatchState.stateWriter = &arrayWriter;

		WalkExprAndAddSupportedElemMatchExprsNew(clauses, options, &elemMatchState,
												 argElement.path);

		if (elemMatchState.indexPath == NULL)
		{
			PgbsonWriterFree(&writer);
			return NULL;
		}

		pgbsonelement queryElement;
		queryElement.path = elemMatchState.indexPath;
		queryElement.pathLength = elemMatchState.indexPathLength;
		queryElement.bsonValue = PgbsonArrayWriterGetValue(&arrayWriter);
		Expr *result = GetElemMatchIndexPushdownOperator(context.documentExpr,
														 &queryElement);
		PgbsonWriterFree(&writer);
		return result;
	}
	else
	{
		List *matchedArgs = WalkExprAndAddSupportedElemMatchExprs(clauses, options);
		if (matchedArgs == NIL)
		{
			return NULL;
		}
		else if (list_length(matchedArgs) == 1)
		{
			/* If there's only one argument for $elemMatch, return it */
			return (Expr *) linitial(matchedArgs);
		}
		else
		{
			return make_ands_explicit(matchedArgs);
		}
	}
}


static OpExpr *
CreateExistsTrueOpExpr(Expr *documentExpr, const char *sourcePath,
					   uint32_t sourcePathLength)
{
	/* If the index is valid for the function, convert it to an OpExpr for a
	 * $exists true.
	 */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	bson_value_t minKey = { 0 };
	minKey.value_type = BSON_TYPE_MINKEY;
	PgbsonWriterAppendValue(&writer, sourcePath, sourcePathLength, &minKey);
	Const *bsonConst = makeConst(BsonTypeId(), -1, InvalidOid, -1, PointerGetDatum(
									 PgbsonWriterGetPgbson(&writer)), false,
								 false);

	const MongoIndexOperatorInfo *info = GetMongoIndexOperatorInfoByPostgresFuncId(
		BsonGreaterThanEqualMatchIndexFunctionId());
	OpExpr *opExpr = (OpExpr *) make_opclause(GetMongoQueryOperatorOid(info), BOOLOID,
											  false,
											  documentExpr,
											  (Expr *) bsonConst, InvalidOid,
											  InvalidOid);
	opExpr->opfuncid = BsonGreaterThanEqualMatchIndexFunctionId();
	return opExpr;
}


static OpExpr *
CreateFullScanOpExpr(Expr *documentExpr, const char *sourcePath, uint32_t
					 sourcePathLength, int32_t orderByDirection)
{
	/* If the index is valid for the function, convert it to an OpExpr for a
	 * $range full scan.
	 */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_writer rangeWriter;
	PgbsonWriterStartDocument(&writer, sourcePath, sourcePathLength,
							  &rangeWriter);
	if (orderByDirection == 0)
	{
		PgbsonWriterAppendBool(&rangeWriter, "fullScan", 8, true);
	}
	else
	{
		PgbsonWriterAppendInt32(&rangeWriter, "orderByScan", 11, orderByDirection);
	}

	PgbsonWriterEndDocument(&writer, &rangeWriter);

	Const *bsonConst = makeConst(BsonTypeId(), -1, InvalidOid, -1, PointerGetDatum(
									 PgbsonWriterGetPgbson(&writer)), false,
								 false);
	OpExpr *opExpr = (OpExpr *) make_opclause(BsonRangeMatchOperatorOid(), BOOLOID,
											  false,
											  documentExpr,
											  (Expr *) bsonConst, InvalidOid,
											  InvalidOid);
	opExpr->opfuncid = BsonRangeMatchFunctionId();
	return opExpr;
}


/*
 * When querying a table with no filters and an orderby, there is a full scan
 * filter applied that allows for index pushdowns. If this is the first key
 * of a composite index, allow the pushdown to support cases like
 * SELECT document from table order by a asc
 */
static Expr *
ProcessFullScanForOrderBy(SupportRequestIndexCondition *req, List *args)
{
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

	if (!IsCompositeOpFamilyOid(req->index->relam, operatorFamily))
	{
		return NULL;
	}

	if (!ValidateIndexForQualifierValue(options, queryValue,
										BSON_INDEX_STRATEGY_DOLLAR_ORDERBY))
	{
		return NULL;
	}

	pgbsonelement sortElement;
	PgbsonToSinglePgbsonElement(DatumGetPgBson(queryValue), &sortElement);

	int8_t sortDirection;
	GetCompositeOpClassColumnNumber(sortElement.path, options,
									&sortDirection);

	int32_t querySortDirection = BsonValueAsInt32(&sortElement.bsonValue);
	bool indexCanOrder = false;
	bool indexSupportsReverseSort = GetIndexSupportsBackwardsScan(req->index->relam,
																  &indexCanOrder);
	if (querySortDirection != sortDirection && !indexSupportsReverseSort)
	{
		return NULL;
	}

	if (!indexCanOrder)
	{
		return NULL;
	}

	/* If the index is valid for the function, convert it to an OpExpr for a
	 * $range full scan.
	 */
	pgbsonelement sourceElement;
	PgbsonToSinglePgbsonElement(DatumGetPgBson(queryValue), &sourceElement);

	return (Expr *) CreateFullScanOpExpr(
		linitial(args), sourceElement.path, sourceElement.pathLength, querySortDirection);
}
