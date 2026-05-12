/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/planner/documents_planner.c
 *
 * Implementation of the documentdb_api planner hook.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <float.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <bson.h>

#include <catalog/pg_am.h>
#include <catalog/pg_class.h>
#include <catalog/pg_opfamily.h>
#include <storage/lmgr.h>
#include <optimizer/planner.h>
#include "optimizer/pathnode.h"
#include <optimizer/cost.h>
#include <nodes/nodes.h>
#include <nodes/makefuncs.h>
#include <nodes/nodeFuncs.h>
#include <nodes/parsenodes.h>
#include <nodes/print.h>
#include <parser/parse_target.h>
#include <storage/lockdefs.h>
#include <tcop/utility.h>
#include <utils/builtins.h>
#include <utils/lsyscache.h>
#include <utils/syscache.h>
#include <executor/spi.h>
#include <parser/parse_relation.h>

#include "geospatial/bson_geospatial_geonear.h"
#include "metadata/collection.h"
#include "metadata/metadata_cache.h"
#include "planner/documentdb_planner.h"
#include "query/query_operator.h"
#include "opclass/bson_index_support.h"
#include "opclass/bson_gin_index_mgmt.h"
#include "metadata/index.h"
#include "customscan/bson_custom_scan.h"
#include "customscan/bson_custom_query_scan.h"
#include "opclass/bson_text_gin.h"
#include "aggregation/bson_aggregation_pipeline.h"
#include "utils/query_utils.h"
#include "api_hooks.h"
#include "query/bson_compare.h"
#include "planner/documents_custom_planner.h"
#include "index_am/index_am_utils.h"


typedef enum DocumentDbQueryFlag
{
	HAS_QUERY_OPERATOR = 1 << 0,
	HAS_DOCUMENTDB_COLLECTION_RTE = 1 << 2,
	HAS_CURSOR_STATE_PARAM = 1 << 3,
	HAS_CURSOR_FUNC = 1 << 4,
	HAS_AGGREGATION_FUNCTION = 1 << 5,
	HAS_NESTED_AGGREGATION_FUNCTION = 1 << 6,
	HAS_QUERY_MATCH_FUNCTION = 1 << 7
} DocumentDbQueryFlag;


typedef enum IndexPriorityOrdering
{
	IndexPriorityOrdering_PrimaryKey = 0,
	IndexPriorityOrdering_Composite = 1,
	IndexPriorityOrdering_Composite_Wildcard = 2,
	IndexPriorityOrdering_Regular = 3,
	IndexPriorityOrdering_Wildcard = 4,
	IndexPriorityOrdering_Other = 5
} IndexPriorityOrdering;

typedef struct ReplaceDocumentDbCollectionContext
{
	/* whether or not the collection is non-existent function */
	bool isNonExistentCollection;

	/* the bound parameters for the given request context */
	ParamListInfo boundParams;

	/* The query associated with this context */
	Query *query;
} ReplaceDocumentDbCollectionContext;

/*
 * State that tracks the DocumentDbQueryFlags walker
 */
typedef struct DocumentDbQueryFlagsState
{
	/* Output: The set of flags encountered */
	int documentDbQueryFlags;

	/* The current depth (intermediate state during walking) */
	int queryDepth;
} DocumentDbQueryFlagsState;

static bool DocumentDbQueryFlagsWalker(Node *node, DocumentDbQueryFlagsState *queryFlags);
static int DocumentDbQueryFlags(Query *query);
static bool IsReadWriteCommand(Query *query);
static Query * ReplaceDocumentDbCollectionFunction(Query *query, ParamListInfo
												   boundParams,
												   bool *isNonExistentCollection);
static bool ReplaceDocumentDbCollectionFunctionWalker(Node *node,
													  ReplaceDocumentDbCollectionContext *
													  context);
static bool HasUnresolvedExternParamsWalker(Node *expression, ParamListInfo boundParams);
static bool IsRTEShardForDocumentDbCollection(RangeTblEntry *rte,
											  bool *isDocumentDbDataNamespace,
											  uint64 *collectionId);
static bool ProcessWorkerWriteQueryPath(PlannerInfo *root, RelOptInfo *rel, Index rti,
										RangeTblEntry *rte);
static Query * ExpandAggregationFunction(Query *node, ParamListInfo boundParams,
										 PlannedStmt **plan);
static Query * ExpandNestedAggregationFunction(Query *node, ParamListInfo boundParams);

static void ForceExcludeNonIndexPaths(PlannerInfo *root, RelOptInfo *rel,
									  Index rti, RangeTblEntry *rte);
static void ExtensionRelPathlistHookCoreNew(PlannerInfo *root, RelOptInfo *rel, Index rti,
											RangeTblEntry *rte, uint64 collectionId, bool
											isShardQuery);

extern bool ForceRUMIndexScanToBitmapHeapScan;
extern bool EnableCollation;
extern bool EnableLetAndCollationForQueryMatch;
extern bool EnableVariablesSupportForWriteCommands;
extern bool EnableIndexOrderbyPushdown;
extern bool ForceDisableSeqScan;
extern bool EnableExtendedExplainPlans;
extern bool EnableIndexPriorityOrdering;
extern bool EnableLogRelationIndexesOrder;
extern bool ForceBitmapScanForLookup;
extern bool EnableIndexOnlyScan;
extern bool EnableCursorsOnAggregationQueryRewrite;
extern bool EnableIdIndexCustomCostFunction;
extern bool EnableCompositeParallelIndexScan;
extern bool ForceParallelScanIfAvailable;

planner_hook_type ExtensionPreviousPlannerHook = NULL;
set_rel_pathlist_hook_type ExtensionPreviousSetRelPathlistHook = NULL;
explain_get_index_name_hook_type ExtensionPreviousIndexNameHook = NULL;
get_relation_info_hook_type ExtensionPreviousGetRelationInfoHook = NULL;


/*
 * Checks if for the given query we need to consider bitmap heap conversion.
 * Few places where we do not consider bitmap heap conversion:
 * - If the query is a $merge outer query.
 * - If the query is a $lookup query and has join RTEs.
 */
static inline bool
IsBitmapHeapConversionSupported(PlannerInfo *root, RelOptInfo *rel)
{
	if (!ForceRUMIndexScanToBitmapHeapScan)
	{
		return false;
	}

	if (EnableIndexOrderbyPushdown || EnableIndexOnlyScan)
	{
		return false;
	}

	/*
	 * Determine if the current relation is the outer query of a $merge stage.
	 * We do not push this relation to the bitmap index.
	 * For the outer relation, the relid will always be 1 since $merge is the last stage of the pipeline.
	 */
	if (root->parse->commandType == CMD_MERGE && rel->relid == 1)
	{
		return false;
	}

	/* Not supported for lookup, check if no JOIN RTEs */
	if (!ForceBitmapScanForLookup && root->hasJoinRTEs)
	{
		return false;
	}

	return true;
}


/*
 * DocumentDBApiPlanner transforms the query tree before passing it
 * to the planner.
 */
PlannedStmt *
DocumentDBApiPlanner(Query *parse, const char *queryString, int cursorOptions,
					 ParamListInfo boundParams)
{
	bool hasUnresolvedParams = false;
	int queryFlags = 0;
	bool isNonExistentCollection = false;
	PlannedStmt *plan = NULL;
	if (IsDocumentDBApiExtensionActive())
	{
		if (IsReadWriteCommand(parse))
		{
			ThrowIfWriteCommandNotAllowed();
		}

		if (parse->commandType != CMD_INSERT)
		{
			queryFlags = DocumentDbQueryFlags(parse);
		}

		if (queryFlags & HAS_AGGREGATION_FUNCTION)
		{
			parse = (Query *) ExpandAggregationFunction(parse, boundParams, &plan);
			if (plan != NULL)
			{
				return plan;
			}
		}

		if (queryFlags & HAS_NESTED_AGGREGATION_FUNCTION)
		{
			parse = (Query *) ExpandNestedAggregationFunction(parse, boundParams);
		}

		/* replace the @@ operators and inject shard_key_value filters */
		if (queryFlags & HAS_QUERY_OPERATOR ||
			queryFlags & HAS_DOCUMENTDB_COLLECTION_RTE ||
			queryFlags & HAS_QUERY_MATCH_FUNCTION)
		{
			parse = (Query *) ReplaceBsonQueryOperators(parse, boundParams);
		}

		/* the collection replace needs to happen *after* the query rewrite. */
		/* this is to handle cases where there's an invalid query against a collection */
		/* that doesn't exist. We need to error out from the invalid query first */
		if (queryFlags & HAS_DOCUMENTDB_COLLECTION_RTE)
		{
			parse = ReplaceDocumentDbCollectionFunction(parse, boundParams,
														&isNonExistentCollection);
		}

		/* replace parameters in cursor_state calls, we need the values during planning */
		if (queryFlags & HAS_CURSOR_STATE_PARAM)
		{
			parse = ReplaceCursorParamValues(parse, boundParams);
		}

		/* for extension queries with unbound parameters, dissuade the parameter */
		if (queryFlags != 0)
		{
			hasUnresolvedParams =
				HasUnresolvedExternParamsWalker((Node *) parse, boundParams);
		}
	}

	if (ExtensionPreviousPlannerHook != NULL)
	{
		plan = ExtensionPreviousPlannerHook(parse, queryString, cursorOptions,
											boundParams);
	}
	else
	{
		plan = standard_planner(parse, queryString, cursorOptions, boundParams);
	}

	if (hasUnresolvedParams)
	{
		/*
		 * When we are doing generic planning for a prepared statement,
		 * parameters are not yet assigned a specific value and our planner
		 * optimizations do not know what to do so we fall back to a very
		 * inefficient or erroring implementation. Signal this to the planner
		 * by using an ultra-high cost (lowered to avoid overflow when summing
		 * costs).
		 */
		plan->planTree->total_cost = FLT_MAX / 10000000;
	}
	else if ((queryFlags & HAS_CURSOR_FUNC) != 0 &&
			 !isNonExistentCollection)
	{
		/*
		 * Only validate the custom scan checks on collections that exist.
		 * Also CTE inlining doesn't happen on volatile functions so if the cursor
		 * projection is still volatile (Pre 1.5-0) then skip validation.
		 */
		ValidateCursorCustomScanPlan(plan->planTree);
	}

	return plan;
}


/*
 * Extracts Operator information from a given RestrictInfo
 * gets the AttrNumber, Operator, and Const if and only if
 * the RestrictInfo is an OpExpr of the form
 * "Var Op Const"
 */
inline static bool
TryExtractDataFromRestrictInfo(RestrictInfo *rinfo, AttrNumber *leftAttr, Oid *opNo,
							   Const **rightConst)
{
	*leftAttr = InvalidAttrNumber;
	*opNo = InvalidOid;
	*rightConst = NULL;
	if (!IsA(rinfo->clause, OpExpr))
	{
		return false;
	}

	OpExpr *opExpr = (OpExpr *) rinfo->clause;
	if (list_length(opExpr->args) != 2)
	{
		return false;
	}

	Expr *leftExpr = linitial(opExpr->args);
	Expr *rightExpr = lsecond(opExpr->args);
	if (!IsA(leftExpr, Var) || !IsA(rightExpr, Const))
	{
		return false;
	}

	*leftAttr = castNode(Var, leftExpr)->varattno;
	*opNo = opExpr->opno;
	*rightConst = castNode(Const, rightExpr);

	return !(*rightConst)->constisnull;
}


inline static bool
TryExtractObjectIdDataFormFuncExprRestrictInfo(RestrictInfo *rinfo, Oid funcOid,
											   bson_value_t *filterValue)
{
	if (!IsA(rinfo->clause, FuncExpr))
	{
		return false;
	}

	FuncExpr *funcExpr = (FuncExpr *) rinfo->clause;

	if (funcExpr->funcid != funcOid || list_length(funcExpr->args) != 2)
	{
		return false;
	}

	Expr *leftExpr = linitial(funcExpr->args);
	Expr *rightExpr = lsecond(funcExpr->args);
	if (!IsA(leftExpr, Var) || !IsA(rightExpr, Const))
	{
		return false;
	}

	if (castNode(Var, leftExpr)->varattno != DOCUMENT_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER)
	{
		return false;
	}

	Const *rightConst = castNode(Const, rightExpr);

	pgbsonelement queryElement;
	if (!rightConst->constisnull &&
		TryGetSinglePgbsonElementFromPgbson(DatumGetPgBsonPacked(
												rightConst->constvalue),
											&queryElement) &&
		queryElement.pathLength == 3 && strcmp(queryElement.path, "_id") == 0)
	{
		*filterValue = queryElement.bsonValue;
		return true;
	}

	return false;
}


bool
InMatchIsEquvalentTo(ScalarArrayOpExpr *opExpr, const bson_value_t *arrayValue)
{
	if (opExpr == NULL || arrayValue->value_type != BSON_TYPE_ARRAY)
	{
		return false;
	}

	List *inMatchArgs = opExpr->args;
	if (list_length(inMatchArgs) != 2)
	{
		return false;
	}

	if (!IsA(lsecond(inMatchArgs), Const))
	{
		return false;
	}

	Const *secondMatch = (Const *) lsecond(inMatchArgs);

	bson_iter_t arrayValueIter;
	BsonValueInitIterator(arrayValue, &arrayValueIter);

	ArrayType *inArrayValue = DatumGetArrayTypeP(secondMatch->constvalue);

	const int slice_ndim = 0;
	ArrayMetaState *mState = NULL;
	ArrayIterator inArrayIterator = array_create_iterator(inArrayValue, slice_ndim,
														  mState);
	Datum arrayDatum = { 0 };
	bool isNull = false;
	while (array_iterate(inArrayIterator, &arrayDatum, &isNull))
	{
		if (!bson_iter_next(&arrayValueIter) || isNull)
		{
			array_free_iterator(inArrayIterator);
			return false;
		}

		const bson_value_t *rightValue = bson_iter_value(&arrayValueIter);
		pgbsonelement leftElement = { 0 };
		pgbson *leftBson = DatumGetPgBsonPacked(arrayDatum);
		PgbsonToSinglePgbsonElement(leftBson, &leftElement);

		if (!BsonValueEquals(&leftElement.bsonValue, rightValue))
		{
			array_free_iterator(inArrayIterator);
			return false;
		}
	}

	array_free_iterator(inArrayIterator);
	if (bson_iter_next(&arrayValueIter))
	{
		return false;
	}

	return true;
}


static void
ExtensionRelPathlistHookCore(PlannerInfo *root, RelOptInfo *rel, Index rti,
							 RangeTblEntry *rte)
{
	bool isDocumentDbDataNamespace = false;
	uint64 collectionId = 0;
	bool isShardQuery = IsRTEShardForDocumentDbCollection(rte, &isDocumentDbDataNamespace,
														  &collectionId);

	if (!isDocumentDbDataNamespace)
	{
		/* Skip looking for queries not pertaining to documentdb data tables */
		return;
	}

	if (!isShardQuery)
	{
		return;
	}

	if (ProcessWorkerWriteQueryPath(root, rel, rti, rte))
	{
		return;
	}

	ExtensionRelPathlistHookCoreNew(root, rel, rti, rte, collectionId, isShardQuery);
}


static void
ExtensionRelPathlistHookCoreNew(PlannerInfo *root, RelOptInfo *rel, Index rti,
								RangeTblEntry *rte, uint64 collectionId, bool
								isShardQuery)
{
	ReplaceExtensionFunctionContext indexContext = {
		.queryDataForVectorSearch = { 0 },
		.hasVectorSearchQuery = false,
		.hasStreamingContinuationScan = false,
		.primaryKeyLookupPath = NULL,
		.inputData = {
			.collectionId = collectionId,
			.isShardQuery = isShardQuery,
			.rteIndex = rti
		},
		.forceIndexQueryOpData = {
			.type = ForceIndexOpType_None,
			.path = NULL,
			.opExtraState = NULL
		}
	};

	if (ForceDisableSeqScan)
	{
		ForceExcludeNonIndexPaths(root, rel, rti, rte);
	}

	/*
	 * Before determining anything further, detect any force pushdown scenarios by walking
	 * the restriction paths.
	 */
	WalkPathsForIndexOperations(rel->pathlist, &indexContext);
	WalkRestrictionPathsForIndexOperations(rel->baserestrictinfo, rel->joininfo,
										   &indexContext);

	/* Before we *replace* function operators in restriction paths, we should apply the force pushdown
	 * logic while we still have the FuncExprs available.
	 */
	if (indexContext.forceIndexQueryOpData.type != ForceIndexOpType_None)
	{
		ForceIndexForQueryOperators(root, rel, &indexContext);
	}

	rel->baserestrictinfo =
		ReplaceExtensionFunctionOperatorsInRestrictionPaths(rel->baserestrictinfo,
															&indexContext);

	/*
	 * Replace all function operators that haven't been transformed in indexed
	 * paths into OpExpr clauses.
	 */
	ReplaceExtensionFunctionOperatorsInPaths(root, rel, rel->pathlist, PARENTTYPE_NONE,
											 &indexContext);

	if (EnableIndexOrderbyPushdown)
	{
		ConsiderIndexOrderByPushdownForId(root, rel, rte, rti, &indexContext);
	}

	if (EnableIndexOnlyScan)
	{
		ConsiderIndexOnlyScan(root, rel, rte, rti, &indexContext);
	}

	/*
	 * Update any paths with custom scans as appropriate.
	 */
	bool updatedPaths = false;
	if (indexContext.hasStreamingContinuationScan)
	{
		updatedPaths = UpdatePathsWithExtensionStreamingCursorPlans(root, rel, rte,
																	&indexContext);
	}

	/* Not a streaming cursor scenario.
	 * Streaming cursors auto convert into Bitmap Paths.
	 * Handle force conversion of bitmap paths.
	 */
	if (!updatedPaths)
	{
		if (IsBitmapHeapConversionSupported(root, rel))
		{
			UpdatePathsToForceRumIndexScanToBitmapHeapScan(root, rel);
		}
		else if (indexContext.forceIndexQueryOpData.type == ForceIndexOpType_Text)
		{
			/* Text indexes require bitmap paths since we leverage bitmapquals
			 * to run the meta_qual.
			 * TODO support indexscan if available.
			 */
			UpdatePathsToForceRumIndexScanToBitmapHeapScan(root, rel);
		}
	}

	/* For vector, text search inject custom scan path to track lifetime of
	 * $meta/ivfprobes.
	 */
	if (indexContext.hasVectorSearchQuery)
	{
		AddExtensionQueryScanForVectorQuery(root, rel, rte,
											&indexContext.queryDataForVectorSearch);
	}
	else if (indexContext.forceIndexQueryOpData.type == ForceIndexOpType_Text)
	{
		QueryTextIndexData *textIndexData =
			(QueryTextIndexData *) indexContext.forceIndexQueryOpData.opExtraState;
		if (textIndexData != NULL && textIndexData->indexOptions != NULL &&
			textIndexData->query != (Datum) 0)
		{
			AddExtensionQueryScanForTextQuery(root, rel, rte, textIndexData);
		}
	}

	if (EnableExtendedExplainPlans)
	{
		/* Finally: Add the custom scan wrapper for explain plans */
		AddExplainCustomScanWrapper(root, rel, rte);
	}

	if (ForceParallelScanIfAvailable)
	{
		ListCell *cell;
		foreach(cell, rel->pathlist)
		{
			Path *path = lfirst(cell);
			path->total_cost += disable_cost;
		}
	}
}


/*
 * ExtensionRelPathlistHook transforms the query paths after the initial planning phase
 * before the final logical plan is formed.
 */
void
ExtensionRelPathlistHook(PlannerInfo *root, RelOptInfo *rel, Index rti,
						 RangeTblEntry *rte)
{
	if (IsDocumentDBApiExtensionActive())
	{
		ExtensionRelPathlistHookCore(root, rel, rti, rte);
	}

	if (ExtensionPreviousSetRelPathlistHook != NULL)
	{
		ExtensionPreviousSetRelPathlistHook(root, rel, rti, rte);
	}
}


/*
 * GetIndexOptInfoSortOrder determines the sort order for IndexOptInfo based on
 * the index type and properties. This is used to prioritize indexes in the
 * relation.
 *
 * 0 - Primary key indexes (Btree)
 * 1 - Composite indexes
 * 2 - Regular BSON indexes
 * 3 - Wildcard indexes
 * 4 - Other index access methods
 */
static int
GetIndexOptInfoSortOrder(const IndexOptInfo *info, int *pathCount)
{
	Oid amOid = info->relam;
	*pathCount = info->ncolumns;
	if (amOid == BTREE_AM_OID)
	{
		return IndexPriorityOrdering_PrimaryKey;
	}

	/* If the index is not a regular BSON index, we give it the lowest priority. */
	if (info->ncolumns <= 0 || !IsBsonRegularIndexAm(amOid))
	{
		return IndexPriorityOrdering_Other;
	}

	Oid firstOpClassOid = info->opfamily[0];

	/* If it is composite op class it's the next priority. Since composite indexes have a single column, we just get the first column for the opclass. */
	if (IsCompositeOpFamilyOid(amOid, firstOpClassOid))
	{
		/* Weight single path composite before wildcard */
		BsonGinCompositePathOptions *options =
			(BsonGinCompositePathOptions *) info->opclassoptions[0];
		*pathCount = GetCompositeOpClassPathCount(options);
		return options->wildcardPathIndex >= 0 ?
			   IndexPriorityOrdering_Composite_Wildcard : IndexPriorityOrdering_Composite;
	}

	/* Wildcard indexes should go after exact path indexes. */
	for (int i = 0; i < info->ncolumns && info->opclassoptions != NULL; i++)
	{
		BsonGinIndexOptionsBase *options =
			(BsonGinIndexOptionsBase *) info->opclassoptions[i];

		if (options == NULL)
		{
			continue;
		}

		if (options->type == IndexOptionsType_Wildcard)
		{
			return IndexPriorityOrdering_Wildcard;
		}

		if (options->type == IndexOptionsType_SinglePath)
		{
			BsonGinSinglePathOptions *singlePathOptions =
				(BsonGinSinglePathOptions *) options;

			if (singlePathOptions->isWildcard)
			{
				return IndexPriorityOrdering_Wildcard;
			}
		}
	}

	return IndexPriorityOrdering_Regular;
}


/*
 * CompareIndexOptionsFunc is a comparison function for sorting IndexOptInfo
 * based on their sort order. It is used to prioritize indexes in the relation.
 * The sort order is determined by the index type and its properties.
 */
static int
CompareIndexOptionsFunc(const ListCell *a, const ListCell *b)
{
	IndexOptInfo *infoA = (IndexOptInfo *) lfirst(a);
	IndexOptInfo *infoB = (IndexOptInfo *) lfirst(b);

	int32_t pathCountA, pathCountB;
	int sortOrderA = GetIndexOptInfoSortOrder(infoA, &pathCountA);
	int sortOrderB = GetIndexOptInfoSortOrder(infoB, &pathCountB);

	if (sortOrderA != sortOrderB)
	{
		return sortOrderA - sortOrderB;
	}

	/* Prefer smaller indexes that match (pathCount 2 is better than pathCount 3)*/
	return pathCountA - pathCountB;
}


/*
 * LogRelationIndexesOrder logs the order of indexes in the relation.
 * This is useful for debugging and understanding how indexes are prioritized.
 */
static void
LogRelationIndexesOrder(const RelOptInfo *rel)
{
	if (rel->indexlist != NIL)
	{
		ListCell *cell;
		foreach(cell, rel->indexlist)
		{
			IndexOptInfo *info = (IndexOptInfo *) lfirst(cell);
			char *indexName = "(unknown)";

			HeapTuple idxTup = SearchSysCache1(RELOID, ObjectIdGetDatum(info->indexoid));
			if (HeapTupleIsValid(idxTup))
			{
				Form_pg_class idxForm = (Form_pg_class) GETSTRUCT(idxTup);
				indexName = NameStr(idxForm->relname);
				ReleaseSysCache(idxTup);
			}

			char *amName = "(unknown)";
			HeapTuple amTup = SearchSysCache1(AMOID, ObjectIdGetDatum(info->relam));
			if (HeapTupleIsValid(amTup))
			{
				Form_pg_am amForm = (Form_pg_am) GETSTRUCT(amTup);
				amName = NameStr(amForm->amname);
				ReleaseSysCache(amTup);
			}

			char *opfamilyName = "(unknown)";

			int numPaths = info->ncolumns;
			if (info->ncolumns > 0)
			{
				Oid opfamilyOid = info->opfamily[0];
				HeapTuple ofTup = SearchSysCache1(OPFAMILYOID, ObjectIdGetDatum(
													  opfamilyOid));
				if (HeapTupleIsValid(ofTup))
				{
					Form_pg_opfamily ofForm = (Form_pg_opfamily) GETSTRUCT(ofTup);
					opfamilyName = NameStr(ofForm->opfname);
					ReleaseSysCache(ofTup);
				}

				if (IsCompositeOpFamilyOid(info->relam, opfamilyOid))
				{
					numPaths = GetCompositeOpClassPathCount(info->opclassoptions[0]);
				}
			}

			ereport(LOG, errmsg(
						"Name: %s, access method: %s, 1st opfamily: %s, numPaths %d",
						indexName, amName, opfamilyName, numPaths));
		}
	}
}


/*
 * ExtensionGetRelationInfoHookCore is the core implementation of the get_relation_info
 * hook for the DocumentDB API extension. It modifies the relation info based on the
 * extension's requirements.
 *
 * First it sorts the relation index list if enabled, based on the index priorities to be considered by the planner if their cost is the same or similar.
 * 1. Primary key indexes are given the highest priority.
 * 2. Composite indexes are given the next priority.
 * 3. Regular BSON indexes are given the next priority.
 * 4. Any other index access method is given the lowest priority.
 *
 */
static void
ExtensionGetRelationInfoHookCore(PlannerInfo *root, Oid relationObjectId,
								 bool inhparent, RelOptInfo *rel)
{
	Oid namespaceId = get_rel_namespace(relationObjectId);
	if (namespaceId != ApiDataNamespaceOid())
	{
		/* Not a documentdb data namespace, skip */
		return;
	}

	if (EnableIndexPriorityOrdering && rel->indexlist != NIL)
	{
		list_sort(rel->indexlist, CompareIndexOptionsFunc);
	}

	/* In this path btree will be first if any */
	if (list_length(rel->indexlist) > 0)
	{
		ListCell *cell;
		foreach(cell, rel->indexlist)
		{
			IndexOptInfo *firstIndex = lfirst(cell);
			if (EnableIdIndexCustomCostFunction && firstIndex->relam == BTREE_AM_OID)
			{
				firstIndex->amcostestimate = documentdb_btcostestimate;
			}
			else if (firstIndex->ncolumns == 1 &&
					 IsCompositeOpFamilyOidWithParallelSupport(firstIndex->relam,
															   firstIndex->opfamily[0]))
			{
				firstIndex->amcanparallel = EnableCompositeParallelIndexScan;
			}
		}
	}

	if (EnableLogRelationIndexesOrder)
	{
		LogRelationIndexesOrder(rel);
	}
}


/* Implementation for the get_relation_info hook. Calls our hook if the extension is active and then calls the previous info hook if any was defined before we registered ours. */
void
ExtensionGetRelationInfoHook(PlannerInfo *root,
							 Oid relationObjectId,
							 bool inhparent,
							 RelOptInfo *rel)
{
	if (IsDocumentDBApiExtensionActive())
	{
		ExtensionGetRelationInfoHookCore(root, relationObjectId, inhparent, rel);
	}

	if (ExtensionPreviousGetRelationInfoHook != NULL)
	{
		ExtensionPreviousGetRelationInfoHook(root, relationObjectId, inhparent, rel);
	}
}


/*
 * DocumentDbQueryFlags determines whether the given query tree contains
 * extension-specific constructs that are relevant to the planner.
 */
static int
DocumentDbQueryFlags(Query *query)
{
	DocumentDbQueryFlagsState queryFlags = { 0 };

	DocumentDbQueryFlagsWalker((Node *) query, &queryFlags);

	return queryFlags.documentDbQueryFlags;
}


inline static bool
IsAggregationFunction(Oid funcId)
{
	return funcId == ApiCatalogAggregationPipelineFunctionId() ||
		   funcId == ApiCatalogAggregationFindFunctionId() ||
		   funcId == ApiCatalogAggregationCountFunctionId() ||
		   funcId == ApiCatalogAggregationDistinctFunctionId() ||
		   funcId == ApiCatalogAggregationGetMoreFunctionId();
}


/*
 * DocumentDbQueryFlagsWalker determines whether the given expression tree contains
 * extension-specific constructs that are relevant to the planner.
 */
static bool
DocumentDbQueryFlagsWalker(Node *node, DocumentDbQueryFlagsState *queryFlags)
{
	CHECK_FOR_INTERRUPTS();

	if (node == NULL)
	{
		return false;
	}

	if (IsA(node, RangeTblEntry))
	{
		RangeTblEntry *rte = (RangeTblEntry *) node;

		if (IsDocumentDbCollectionBasedRTE(rte))
		{
			queryFlags->documentDbQueryFlags |= HAS_DOCUMENTDB_COLLECTION_RTE;
		}
		else if (rte->rtekind == RTE_FUNCTION &&
				 list_length(rte->functions) == 1)
		{
			RangeTblFunction *rangeTblFunc = linitial(rte->functions);
			if (!IsA(rangeTblFunc->funcexpr, FuncExpr))
			{
				return false;
			}

			FuncExpr *funcExpr = (FuncExpr *) rangeTblFunc->funcexpr;

			/* Defer the func check until we really have to */
			if (list_length(funcExpr->args) != 2 &&
				list_length(funcExpr->args) != 3)
			{
				return false;
			}

			if (funcExpr->funcresulttype != BsonTypeId() ||
				!funcExpr->funcretset)
			{
				return false;
			}

			if (IsAggregationFunction(funcExpr->funcid))
			{
				if (queryFlags->queryDepth > 1)
				{
					queryFlags->documentDbQueryFlags |= HAS_NESTED_AGGREGATION_FUNCTION;
				}
				else
				{
					queryFlags->documentDbQueryFlags |= HAS_AGGREGATION_FUNCTION;
				}

				return true;
			}
		}

		return false;
	}
	else if (IsA(node, OpExpr))
	{
		OpExpr *opExpr = (OpExpr *) node;

		if (opExpr->opno == BsonQueryOperatorId())
		{
			queryFlags->documentDbQueryFlags |= HAS_QUERY_OPERATOR;
		}

		return false;
	}
	else if (IsA(node, FuncExpr))
	{
		FuncExpr *funcExpr = (FuncExpr *) node;

		if (funcExpr->funcid == ApiCursorStateFunctionId())
		{
			queryFlags->documentDbQueryFlags |= HAS_CURSOR_FUNC;

			Node *queryNode = lsecond(funcExpr->args);
			if (IsA(queryNode, Param))
			{
				queryFlags->documentDbQueryFlags |= HAS_CURSOR_STATE_PARAM;
			}
		}

		bool useQueryMatchWithLetAndCollation = EnableCollation ||
												EnableLetAndCollationForQueryMatch ||
												EnableVariablesSupportForWriteCommands;
		if (useQueryMatchWithLetAndCollation &&
			funcExpr->funcid == BsonQueryMatchWithLetAndCollationFunctionId())
		{
			queryFlags->documentDbQueryFlags |= HAS_QUERY_MATCH_FUNCTION;
		}

		return false;
	}
	else if (IsA(node, Query))
	{
		queryFlags->queryDepth++;
		bool result = query_tree_walker((Query *) node, DocumentDbQueryFlagsWalker,
										queryFlags, QTW_EXAMINE_RTES_BEFORE);
		queryFlags->queryDepth--;
		return result;
	}

	return expression_tree_walker(node, DocumentDbQueryFlagsWalker, queryFlags);
}


/*
 * Helper method that identifies if a query statement is read-write or read only.
 */
static bool
IsReadWriteCommand(Query *query)
{
	CmdType commandType = query->commandType;

	/* This is based on Postgres' logic:
	 * https://github.com/postgres/postgres/blob/c8e1ba736b2b9e8c98d37a5b77c4ed31baf94147/src/backend/tcop/utility.c#L101
	 *
	 * We can't use that method directly since that takes a PlannedStmt and we need to check before calling Citus'
	 * planner as we want to avoid them throwing the error to have control on our error message and error code.
	 *
	 * CMD_UTILITY is not included here, as that is taken care by the process_utilit_hook.c which is called
	 * before the planner for utility commands.
	 *
	 * See: IsUtilityCommandReadWrite
	 */
	switch (commandType)
	{
		case CMD_SELECT:
		{
			if (query->rowMarks != NIL || query->hasModifyingCTE)
			{
				return true;
			}
			else
			{
				return false;
			}
		}

		case CMD_UPDATE:
		case CMD_INSERT:
		case CMD_DELETE:
#if PG_VERSION_NUM >= 150000
		case CMD_MERGE:
#endif
			{
				return true;
			}

		default:
		{
			return false;
		}
	}
}


/*
 * ReplaceDocumentDbCollectionFunction replaces all occurences of the
 * ApiSchema.collection() function call with the corresponding
 * table.
 */
static Query *
ReplaceDocumentDbCollectionFunction(Query *query, ParamListInfo boundParams,
									bool *isNonExistentCollection)
{
	/*
	 * We will change a function RTE into a relation RTE so we can use
	 * a regular walker that does not copy the whole query tree.
	 */
	ReplaceDocumentDbCollectionContext context =
	{
		.boundParams = boundParams,
		.isNonExistentCollection = false,
		.query = query
	};
	ReplaceDocumentDbCollectionFunctionWalker((Node *) query, &context);
	*isNonExistentCollection = context.isNonExistentCollection;
	return query;
}


/*
 * ReplaceDocumentDbCollectionFunctionWalker recurses into the input to replace
 * all occurences of the ApiSchema.collection() function call with the corresponding
 * table.
 */
static bool
ReplaceDocumentDbCollectionFunctionWalker(Node *node,
										  ReplaceDocumentDbCollectionContext *context)
{
	CHECK_FOR_INTERRUPTS();

	if (node == NULL)
	{
		return false;
	}

	if (IsA(node, RangeTblEntry))
	{
		RangeTblEntry *rte = (RangeTblEntry *) node;

		if (IsResolvableDocumentDbCollectionBasedRTE(rte, context->boundParams))
		{
			/* extract common arguments for collection-based RTE of the form ApiSchema.*collection*(db, coll, ..) */
			RangeTblFunction *rangeTableFunc = linitial(rte->functions);
			FuncExpr *funcExpr = (FuncExpr *) rangeTableFunc->funcexpr;
			Const *dbConst = GetConstParamValue((Node *) linitial(funcExpr->args),
												context->boundParams);
			Const *collectionConst = GetConstParamValue((Node *) lsecond(funcExpr->args),
														context->boundParams);
			Datum databaseNameDatum = dbConst->constvalue;
			Datum collectionNameDatum = collectionConst->constvalue;

			/* retrieve collection details and lock the underlying relation for reads */
			MongoCollection *collection =
				GetMongoCollectionByNameDatum(databaseNameDatum, collectionNameDatum,
											  AccessShareLock);

			if (collection == NULL)
			{
				/*
				 * non-existent collections should be treated as empty.
				 * Here we replace the ApiSchema.collection() function call
				 * empty_data_table() which returns an response mimicking SELECT
				 * from an empty documentdb data collection.
				 */
				funcExpr->funcid = BsonEmptyDataTableFunctionId();
				funcExpr->args = NIL;
				context->isNonExistentCollection = true;
			}
			else
			{
				/* 1. Perform common actions for Collection-based RTEs*/
				/* 1.a. change function RTE into a relation RTE */
				rte->rtekind = RTE_RELATION;
				rte->relid = collection->relationId;
				rte->relkind = RELKIND_RELATION;
				rte->functions = NIL;
				rte->inh = true;
#if PG_VERSION_NUM >= 160000

				RTEPermissionInfo *permInfo = addRTEPermissionInfo(
					&context->query->rteperminfos, rte);
				permInfo->requiredPerms = ACL_SELECT;
#else
				rte->requiredPerms = ACL_SELECT;
#endif
				rte->rellockmode = AccessShareLock;
			}
		}

		return false;
	}
	else if (IsA(node, Query))
	{
		Query *originalQuery = context->query;
		context->query = (Query *) node;
		bool result = query_tree_walker((Query *) node,
										ReplaceDocumentDbCollectionFunctionWalker,
										context, QTW_EXAMINE_RTES_BEFORE);
		context->query = originalQuery;
		return result;
	}

	return expression_tree_walker(node, ReplaceDocumentDbCollectionFunctionWalker,
								  context);
}


/*
 * Get the Const value of a parameter.
 */
Const *
GetConstParamValue(Node *param, ParamListInfo boundParams)
{
	if (!IsA(param, Const))
	{
		param = EvaluateBoundParameters(param, boundParams);
	}

	Assert(IsA(param, Const));
	return (Const *) param;
}


/*
 * IsResolvableDocumentDbCollectionBasedRTE returns whether the given node is a function RTE
 * of the form ApiSchema.*collection*('db', 'coll', ...).
 *
 * Otherwise, we return false, thereby allowing the RTE_FUNCTION to be called directly, and not
 * changing it to a RTE_RELATION.
 */
bool
IsResolvableDocumentDbCollectionBasedRTE(RangeTblEntry *rte, ParamListInfo boundParams)
{
	if (!IsDocumentDbCollectionBasedRTE(rte))
	{
		return false;
	}

	RangeTblFunction *rangeTblFunc = linitial(rte->functions);
	FuncExpr *funcExpr = (FuncExpr *) rangeTblFunc->funcexpr;

	/* Handle the common params (db and coll) for collection-based RTEs */
	Node *dbArg = (Node *) linitial(funcExpr->args);
	Node *collectionArg = (Node *) lsecond(funcExpr->args);

	if (!IsA(dbArg, Const))
	{
		dbArg = EvaluateBoundParameters(dbArg, boundParams);
	}

	if (!IsA(collectionArg, Const))
	{
		collectionArg = EvaluateBoundParameters(collectionArg, boundParams);
	}

	if (!IsA(dbArg, Const) || !IsA(collectionArg, Const))
	{
		/* in this case, we will call the function directly */
		return false;
	}

	/* Perform Function-specific actions */
	if (funcExpr->funcid == ApiCollectionFunctionId() ||
		funcExpr->funcid == DocumentDBApiCollectionFunctionId())
	{
		return true;
	}

	return false;
}


/*
 * IsDocumentDbCollectionBasedRTE returns whether the given node is a
 * collection() RTE.
 */
bool
IsDocumentDbCollectionBasedRTE(RangeTblEntry *rte)
{
	if (rte->rtekind != RTE_FUNCTION)
	{
		return false;
	}

	if (list_length(rte->functions) != 1)
	{
		return false;
	}

	RangeTblFunction *rangeTblFunc = linitial(rte->functions);
	if (!IsA(rangeTblFunc->funcexpr, FuncExpr))
	{
		return false;
	}

	FuncExpr *funcExpr = (FuncExpr *) rangeTblFunc->funcexpr;
	if (list_length(funcExpr->args) < 2)
	{
		return false;
	}

	if (funcExpr->funcid != ApiCollectionFunctionId() &&
		funcExpr->funcid != DocumentDBApiCollectionFunctionId())
	{
		return false;
	}

	return true;
}


/*
 * The default implementation of PG get IndexName for an OID.
 */
inline static const char *
IndexIdGetIndexNameDefault(Oid indexId)
{
	const char *pgIndexName = get_rel_name(indexId);
	if (pgIndexName == NULL)
	{
		elog(ERROR, "cache lookup failed for index %u", indexId);
	}

	return pgIndexName;
}


/*
 * Explain hook to get the index name from a index Object ID.
 * This checks if the index is an extension index, and if it is,
 * then looks up the index name from the index options for that
 * index.
 */
const char *
ExtensionExplainGetIndexName(Oid indexId)
{
	if (IsDocumentDBApiExtensionActive())
	{
		bool useLibPQ = true;
		const char *documentDbIndexName = ExtensionIndexOidGetIndexName(indexId,
																		useLibPQ);
		if (documentDbIndexName != NULL)
		{
			return documentDbIndexName;
		}
	}

	if (ExtensionPreviousIndexNameHook != NULL)
	{
		return ExtensionPreviousIndexNameHook(indexId);
	}

	return IndexIdGetIndexNameDefault(indexId);
}


/*
 * Given a postgres index name, returns the corresponding documentdb index name if available.
 */
const char *
GetDocumentDBIndexNameFromPostgresIndex(const char *pgIndexName, bool useLibPq)
{
	int prefixLength = strlen(DOCUMENT_DATA_TABLE_INDEX_NAME_FORMAT_PREFIX);
	if (strncmp(pgIndexName, DOCUMENT_DATA_TABLE_INDEX_NAME_FORMAT_PREFIX,
				prefixLength) == 0)
	{
		int64 indexIdValue = atoll(pgIndexName + prefixLength);
		StringInfo indexNameQuery = makeStringInfo();
		const char *indexName = NULL;
		if (useLibPq)
		{
			appendStringInfo(indexNameQuery,
							 "SELECT (index_spec).index_name FROM %s.collection_indexes WHERE index_id = %ld",
							 ApiCatalogSchemaName, indexIdValue);

			indexName = ExtensionExecuteQueryOnLocalhostViaLibPQ(indexNameQuery->data);
		}
		else
		{
			appendStringInfo(indexNameQuery,
							 "SELECT (index_spec).index_name FROM %s.collection_indexes WHERE index_id = $1",
							 ApiCatalogSchemaName);

			bool readOnly = true;
			bool isNull[1] = { true };
			Datum resultDatum[1] = { 0 };

			Datum args[1] = { Int64GetDatum(indexIdValue) };
			Oid argTypes[1] = { INT8OID };
			char argNulls[1] = { ' ' };

			RunMultiValueQueryWithNestedDistribution(indexNameQuery->data, 1, argTypes,
													 args, argNulls, readOnly,
													 SPI_OK_SELECT, resultDatum, isNull,
													 1);
			if (!isNull[0])
			{
				indexName = TextDatumGetCString(resultDatum[0]);
			}
		}

		return indexName;
	}
	else if (strncmp(pgIndexName, DOCUMENT_DATA_PRIMARY_KEY_FORMAT_PREFIX,
					 strlen(DOCUMENT_DATA_PRIMARY_KEY_FORMAT_PREFIX)) == 0)
	{
		/* this is the _id index */
		return ID_INDEX_NAME;
	}

	return NULL;
}


/*
 * Retrieves the "documentdb" index name for a given indexId.
 * This is retrieved by using the collection_indexes table
 * every time. Introduces an option to use libPQ or SPI.
 *
 * For LibPQ, Note that this should only be used for nested
 * distributed transaction cases that are not in the hot path
 * (e.g. EXPLAIN scenarios).
 */
const char *
ExtensionIndexOidGetIndexName(Oid indexId, bool useLibPq)
{
	const char *pgIndexName = IndexIdGetIndexNameDefault(indexId);
	if (pgIndexName == NULL)
	{
		return NULL;
	}

	/* if it's an extension secondary index */
	const char *indexName = GetDocumentDBIndexNameFromPostgresIndex(pgIndexName,
																	useLibPq);
	if (indexName == NULL)
	{
		indexName = IndexIdGetIndexNameDefault(indexId);
	}

	return indexName;
}


/*
 * HasUnresolvedExternParamsWalker returns true if the passed in expression
 * has external parameters that are not contained in boundParams, false
 * otherwise.
 *
 * (Copied from Citus)
 */
static bool
HasUnresolvedExternParamsWalker(Node *expression, ParamListInfo boundParams)
{
	if (expression == NULL)
	{
		return false;
	}

	if (IsA(expression, Param))
	{
		Param *param = (Param *) expression;
		int paramId = param->paramid;

		/* only care about user supplied parameters */
		if (param->paramkind != PARAM_EXTERN)
		{
			return false;
		}

		/* Verify if the parameter is valid */
		if (boundParams && paramId > 0 && paramId <= boundParams->numParams)
		{
			return false;
		}

		return true;
	}

	/* keep traversing */
	if (IsA(expression, Query))
	{
		return query_tree_walker((Query *) expression,
								 HasUnresolvedExternParamsWalker,
								 boundParams,
								 0);
	}
	else
	{
		return expression_tree_walker(expression,
									  HasUnresolvedExternParamsWalker,
									  boundParams);
	}
}


static bool
CheckRelNameValidity(const char *relName, uint64_t *collectionId)
{
	if (relName == NULL ||
		strncmp(relName, "documents_", 10) != 0)
	{
		return false;
	}

	/* We use strtoull since it returns the first character that didn't match
	 * We expect this to return the '_' character when it's a collection shard
	 * like ApiDataSchemaName.documents_1_111 and the parsed value will be 1.
	 * Alternatively, this will be \0 and the parsed value will be 1 if the
	 * table is documents_1 (parent table).
	 */
	char *numEndPointer = NULL;
	uint64 parsedCollectionId = strtoull(&relName[10], &numEndPointer, 10);
	if (IsShardTableForDocumentDbTable(relName, numEndPointer))
	{
		*collectionId = parsedCollectionId;
		return true;
	}

	return false;
}


/*
 * Returns true if the relation of RTE pointed to
 * is a DocumentDB table base collection. e.g.
 * for ApiDataSchemaName.documents_1 -> false (if sharding is enabled)
 * but
 * ApiDataSchemaName.documents_1_1034 -> true
 *
 * This is because we need to substitute the runtime expression
 * with the index expression in the planner to avoid re-evaluating
 * index clauses for index scans. But we also only want to do this in
 * the shard queries (We need to retain the runtime functions in the
 * coordinator since index selection should only really happen in the
 * shards).
 * For more details see bson_query_index_selection_sharded_tests.sql
 */
static bool
IsRTEShardForDocumentDbCollection(RangeTblEntry *rte, bool *isDocumentDbDataNamespace,
								  uint64 *collectionId)
{
	if (rte->rtekind != RTE_RELATION ||
		rte->relkind != RELKIND_RELATION)
	{
		return false;
	}

	Oid tableOid = rte->relid;
	Oid relNamespace = get_rel_namespace(tableOid);

	*isDocumentDbDataNamespace = relNamespace == ApiDataNamespaceOid();
	if (!*isDocumentDbDataNamespace)
	{
		return false;
	}

	HeapTuple tp = SearchSysCache1(RELOID, ObjectIdGetDatum(tableOid));
	if (HeapTupleIsValid(tp))
	{
		Form_pg_class reltup = (Form_pg_class) GETSTRUCT(tp);
		const char *relNameStr = NameStr(reltup->relname);
		bool isValid = CheckRelNameValidity(relNameStr, collectionId);
		ReleaseSysCache(tp);
		return isValid;
	}

	return false;
}


/*
 * For Insert/Update/Delete queries, we can't use create_distributed_function
 * directly since that needs a single colocation group. Consequently, we use
 * a special query - where we write the query as
 *
 * SELECT update_worker(collectionId, shardKeyValue, 0, ...) FROM ApiData.documents_1 WHERE shard_key_value = 1;
 *
 * In the query coordinator. When that query gets distributed to the shard, it will look like
 *
 * SELECT update_worker(collectionId, shardKeyValue, 0, ...) FROM ApiData.documents_1_shardid WHERE shard_key_value = 1;
 *
 * In the shard, we then rewrite that query (as below into)
 * SELECT update_worker(collectionId, shardKeyValue, <oid of shard table>, ...);
 *
 * The replacement of the shard table in the function allows the worker function to know that the planner replacement
 * happened (and error out otherwise).
 *
 * In future iterations the worker function can use the shard OID but for now it's only used to indicate that this planner
 * replacement happened.
 *
 * In the future, once distribution works across colo groups this can be removed.
 */
static bool
ProcessWorkerWriteQueryPath(PlannerInfo *root, RelOptInfo *rel, Index rti,
							RangeTblEntry *rte)
{
	if (list_length(root->processed_tlist) != 1)
	{
		return false;
	}

	TargetEntry *entry = linitial(root->processed_tlist);
	if (!IsA(entry->expr, FuncExpr))
	{
		return false;
	}

	/* Reduce the likelihood of doing the Func OID lookup since older
	 * schemas won't have it.
	 */
	FuncExpr *funcExpr = (FuncExpr *) entry->expr;
	if (list_length(funcExpr->args) != 6)
	{
		return false;
	}

	if (!(funcExpr->funcid == UpdateWorkerFunctionOid() ||
		  funcExpr->funcid == InsertWorkerFunctionOid() ||
		  funcExpr->funcid == DeleteWorkerFunctionOid() ||
		  funcExpr->funcid == CommandNodeWorkerFunctionOid()))
	{
		return false;
	}

	/* It's a shard query for a update worker projector
	 * Transform this query into a FuncRTE with a Var projector
	 */
	entry->expr = (Expr *) makeVar(rti, 1, DocumentDBCoreBsonTypeId(), -1,
								   InvalidOid, 0);
	rte->rtekind = RTE_FUNCTION;
	RangeTblFunction *func = makeNode(RangeTblFunction);
	Node *shardArg = list_nth(funcExpr->args, 2);
	if (IsA(shardArg, Const))
	{
		Const *shardConst = (Const *) shardArg;
		shardConst->constvalue = ObjectIdGetDatum(rte->relid);
	}

	func->funcexpr = (Node *) funcExpr;
	rte->functions = list_make1(func);

	Path *funcScanPath = create_functionscan_path(root, rel, NIL, 0);
	rel->pathlist = list_make1(funcScanPath);
	rel->partial_pathlist = NIL;
	rel->baserestrictinfo = NIL;
#if PG_VERSION_NUM >= 160000
	rte->perminfoindex = 0;
#endif
	return true;
}


/*
 * Walks queries and if it encounters a query that could meet the requirements of the aggregation
 * query, replaces it with the post-processed query.
 */
static Node *
MutateQueryAggregatorFunction(Node *node, ParamListInfo boundParams)
{
	if (node == NULL)
	{
		return node;
	}

	if (IsA(node, Query))
	{
		ListCell *cell;
		Query *query = (Query *) node;
		foreach(cell, query->rtable)
		{
			RangeTblEntry *entry = lfirst(cell);
			if (entry->rtekind == RTE_FUNCTION &&
				list_length(entry->functions) == 1)
			{
				RangeTblFunction *expr = (RangeTblFunction *) linitial(entry->functions);
				if (IsA(expr->funcexpr, FuncExpr) &&
					IsAggregationFunction(castNode(FuncExpr, expr->funcexpr)->funcid))
				{
					PlannedStmt *stmt = NULL;
					return (Node *) ExpandAggregationFunction(query, boundParams, &stmt);
				}
			}
		}

		return (Node *) query_tree_mutator((Query *) node, MutateQueryAggregatorFunction,
										   boundParams, QTW_DONT_COPY_QUERY |
										   QTW_EXAMINE_RTES_BEFORE);
	}

	return expression_tree_mutator(node, MutateQueryAggregatorFunction, boundParams);
}


static Query *
ExpandNestedAggregationFunction(Query *query, ParamListInfo boundParams)
{
	return query_tree_mutator(query, MutateQueryAggregatorFunction, boundParams,
							  QTW_DONT_COPY_QUERY | QTW_EXAMINE_RTES_BEFORE);
}


/*
 * Traverses the query looking for an aggregation pipeline function.
 * If it's found, then replaces the function with nothing, and updates the query
 * to track the contents of the aggregation pipeline.
 */
static Query *
ExpandAggregationFunction(Query *query, ParamListInfo boundParams, PlannedStmt **plan)
{
	/* Top level validations - these are right now during development */
	*plan = NULL;

	/* Today we only support a query of the form
	 * SELECT document FROM bson_aggregation_pipeline('db', 'pipeline bson'::bson);
	 * OR
	 * SELECT document FROM bson_aggregation_find('db', 'findSpec bson'::bson);
	 * This restriction exists during the development phase and can change as we move to prod.
	 */
	if (list_length(query->rtable) != 1)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline node should have 1 collection. Found %d. This is unexpected",
							list_length(query->rtable))));
	}

	if (query->jointree == NULL)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline node should have at least 1 collection and query. This is unexpected")));
	}

	if (list_length(query->jointree->fromlist) != 1)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline node should have exactly 1 collection to query from not %d. This is unexpected",
							list_length(query->jointree->fromlist))));
	}

	if (list_length(query->cteList) > 0)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline node should have CTEs. This is currently unsupported")));
	}

	if (list_length(query->targetList) != 1)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline query should not have more than 1 projector Found %d. This is currently unsupported",
							list_length(query->targetList))));
	}

	if (query->limitOffset != NULL || query->limitCount != NULL)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline query should not have skip/limit. This is currently unsupported")));
	}

	if (query->sortClause != NIL)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline query should not have sort. This is currently unsupported")));
	}

	TargetEntry *targetEntry = linitial(query->targetList);
	if (!IsA(targetEntry->expr, Var))
	{
		ereport(ERROR, (errmsg(
							"Projector must be a single (alias-ed) column. This is unexpected")));
	}

	if (query->jointree->quals != NULL)
	{
		ereport(ERROR, (errmsg(
							"Query must not have filters. This is unexpected")));
	}

	RangeTblEntry *rte = (RangeTblEntry *) linitial(query->rtable);

	if (rte->rtekind != RTE_FUNCTION || list_length(rte->functions) != 1)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline node should select from the aggregation function kind %d. This is unexpected",
							rte->rtekind)));
	}

	RangeTblFunction *rangeTblFunc = linitial(rte->functions);
	if (!IsA(rangeTblFunc->funcexpr, FuncExpr))
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline node should select is not a function. This is unexpected")));
	}

	FuncExpr *aggregationFunc = (FuncExpr *) rangeTblFunc->funcexpr;

	if (list_length(aggregationFunc->args) != 2 &&
		list_length(aggregationFunc->args) != 3)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline node should have 2 or 3 args. This is unexpected")));
	}

	Node *databaseArg = linitial(aggregationFunc->args);
	Node *secondArg = lsecond(aggregationFunc->args);
	Node *thirdArg = NULL;

	if (!IsA(secondArg, Const) || !IsA(databaseArg, Const))
	{
		secondArg = EvaluateBoundParameters(secondArg, boundParams);
		databaseArg = EvaluateBoundParameters(databaseArg, boundParams);
	}

	if (!IsA(secondArg, Const) || !IsA(databaseArg, Const))
	{
		/* Let the runtime deal with this (This will either go to the runtime function and fail, or noop due to prepared and come back here
		 * to be evaluated during the EXECUTE)
		 */
		return query;
	}

	if (list_length(aggregationFunc->args) == 3)
	{
		thirdArg = lthird(aggregationFunc->args);
		if (!IsA(thirdArg, Const))
		{
			thirdArg = EvaluateBoundParameters(thirdArg, boundParams);
		}

		if (!IsA(thirdArg, Const))
		{
			/* Let the runtime deal with this (This will either go to the runtime function and fail,
			 * or noop due to prepared and come back here
			 * to be evaluated during the EXECUTE)*/
			return query;
		}
	}

	Const *databaseConst = (Const *) databaseArg;
	Const *aggregationConst = (Const *) secondArg;
	if (databaseConst->constisnull || aggregationConst->constisnull)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline arguments should not be null. This is unexpected")));
	}

	pgbson *pipeline = DatumGetPgBson(aggregationConst->constvalue);

	QueryData queryData = GenerateFirstPageQueryData();
	bool enableCursorParam = false;
	bool setStatementTimeout = false;
	Query *finalQuery;
	if (aggregationFunc->funcid == ApiCatalogAggregationPipelineFunctionId())
	{
		finalQuery = GenerateAggregationQuery(DatumGetTextPP(databaseConst->constvalue),
											  pipeline,
											  &queryData,
											  enableCursorParam, setStatementTimeout);
	}
	else if (aggregationFunc->funcid == ApiCatalogAggregationFindFunctionId())
	{
		finalQuery = GenerateFindQuery(DatumGetTextPP(databaseConst->constvalue),
									   pipeline, &queryData,
									   enableCursorParam, setStatementTimeout);
	}
	else if (aggregationFunc->funcid == ApiCatalogAggregationCountFunctionId())
	{
		finalQuery = GenerateCountQuery(DatumGetTextPP(databaseConst->constvalue),
										pipeline,
										setStatementTimeout);
	}
	else if (aggregationFunc->funcid == ApiCatalogAggregationDistinctFunctionId())
	{
		finalQuery = GenerateDistinctQuery(DatumGetTextPP(databaseConst->constvalue),
										   pipeline,
										   setStatementTimeout);
	}
	else if (aggregationFunc->funcid == ApiCatalogAggregationGetMoreFunctionId())
	{
		Const *thirdConst = (Const *) thirdArg;
		if (thirdConst->constisnull)
		{
			ereport(ERROR, (errmsg(
								"Aggregation pipeline arguments should not be null. This is unexpected")));
		}

		finalQuery = GenerateGetMoreQuery(DatumGetTextPP(databaseConst->constvalue),
										  pipeline, DatumGetPgBson(
											  thirdConst->constvalue),
										  &queryData, enableCursorParam,
										  setStatementTimeout);
	}
	else
	{
		ereport(ERROR, (errmsg(
							"Unrecognized pipeline functionid provided. This is unexpected")));
	}

	if (queryData.cursorKind == QueryCursorType_PointRead)
	{
		/* Assert we're a shard query */
		bool isDocumentDbDataNamespace;
		uint64_t collectionId;
		bool isShardQuery = IsRTEShardForDocumentDbCollection(linitial(
																  finalQuery->rtable),
															  &isDocumentDbDataNamespace,
															  &collectionId);

		if (!isShardQuery)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Unexpected - found point read plan on a non-direct-shard collection")));
		}

		/* For point reads, allow for fast path planning */
		*plan = TryCreatePointReadPlan(finalQuery);
	}

	if (EnableCursorsOnAggregationQueryRewrite)
	{
		ereport(DEBUG1, (errmsg("Aggregation cursorKind is %d", queryData.cursorKind)));
	}

	return finalQuery;
}


static bool
IsPrimaryKeyScanOnJustShardKey(Path *path)
{
	if (path->pathtype != T_IndexScan)
	{
		return false;
	}

	IndexPath *indexPath = (IndexPath *) path;
	if (indexPath->indexinfo->relam == BTREE_AM_OID)
	{
		if (list_length(indexPath->indexclauses) == 1)
		{
			return true;
		}
	}

	return false;
}


static List *
TrimPathListForSeqTypeScans(List *pathList)
{
	ListCell *cell;
	foreach(cell, pathList)
	{
		Path *path = (Path *) lfirst(cell);

		if (path->pathtype != T_IndexScan &&
			path->pathtype != T_BitmapHeapScan)
		{
			elog(DEBUG1, "Excluding path non-index path %d for scan",
				 path->pathtype);
			pathList = foreach_delete_current(pathList, cell);
			continue;
		}

		/*
		 * Now validate it's not just a scan on the primary key
		 * with the shard key value.
		 */
		if (IsPrimaryKeyScanOnJustShardKey(path))
		{
			elog(DEBUG1, "Excluding primary key scan on just shard key %d for scan",
				 path->pathtype);
			pathList = foreach_delete_current(pathList, cell);
			continue;
		}
		if (path->pathtype == T_BitmapHeapScan)
		{
			BitmapHeapPath *bitmapHeapPath = (BitmapHeapPath *) path;
			if (bitmapHeapPath->bitmapqual != NULL &&
				IsPrimaryKeyScanOnJustShardKey(bitmapHeapPath->bitmapqual))
			{
				elog(DEBUG1, "Excluding bitmap heap scan on just shard key %d for scan",
					 path->pathtype);
				pathList = foreach_delete_current(pathList, cell);
				continue;
			}
		}
	}

	return pathList;
}


static void
ForceExcludeNonIndexPaths(PlannerInfo *root, RelOptInfo *rel,
						  Index rti, RangeTblEntry *rte)
{
	if (rel->pathlist == NIL)
	{
		return;
	}

	rel->pathlist = TrimPathListForSeqTypeScans(rel->pathlist);
	rel->partial_pathlist = TrimPathListForSeqTypeScans(rel->partial_pathlist);

	if (rel->pathlist == NIL)
	{
		/* Try a round of planning with no sequential paths and another round of trimming
		 * before failing.
		 */
		create_index_paths(root, rel);

		rel->pathlist = TrimPathListForSeqTypeScans(rel->pathlist);
		rel->partial_pathlist = TrimPathListForSeqTypeScans(rel->partial_pathlist);


		if (rel->pathlist == NIL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
							errmsg(
								"Could not find any valid index to push down for query")));
		}
	}
}
