/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/planner/planner.c
 *
 * Implementation of the helioapi planner hook.
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
#include <storage/lmgr.h>
#include <optimizer/planner.h>
#include "optimizer/pathnode.h"
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

#include "metadata/collection.h"
#include "metadata/metadata_cache.h"
#include "planner/helio_planner.h"
#include "query/query_operator.h"
#include "opclass/helio_index_support.h"
#include "opclass/helio_gin_index_mgmt.h"
#include "metadata/index.h"
#include "customscan/helio_custom_scan.h"
#include "customscan/helio_custom_query_scan.h"
#include "opclass/helio_bson_text_gin.h"
#include "aggregation/bson_aggregation_pipeline.h"
#include "utils/query_utils.h"
#include "api_hooks.h"
#include "query/helio_bson_compare.h"


typedef enum MongoQueryFlag
{
	HAS_QUERY_OPERATOR = 1 << 0,
	HAS_MONGO_COLLECTION_RTE = 1 << 2,
	HAS_CURSOR_STATE_PARAM = 1 << 3,
	HAS_CURSOR_FUNC = 1 << 4,
	HAS_AGGREGATION_FUNCTION = 1 << 5,
} MongoQueryFlag;

typedef struct ReplaceMongoCollectionContext
{
	/* whether or not the collection is non-existent function */
	bool isNonExistentCollection;

	/* the bound parameters associated with the request */
	ParamListInfo boundParams;

	/* The query associated with this context */
	Query *query;
} ReplaceMongoCollectionContext;


static bool MongoQueryFlagsWalker(Node *node, int *queryFlags);
static int MongoQueryFlags(Query *query);
static bool IsReadWriteCommand(Query *query);
static Query * ReplaceMongoCollectionFunction(Query *query, ParamListInfo boundParams,
											  bool *isNonExistentCollection);
static bool ReplaceMongoCollectionFunctionWalker(Node *node,
												 ReplaceMongoCollectionContext *context);
static bool HasUnresolvedExternParamsWalker(Node *expression, ParamListInfo boundParams);
static bytea * TryFindTextIndexForRelation(RelOptInfo *rel, RangeTblEntry *rte);
static bool IsRTEShardForMongoCollection(RangeTblEntry *rte, bool *isMongoDataNamespace,
										 uint64 *collectionId);
static bool ProcessWorkerWriteQueryPath(PlannerInfo *root, RelOptInfo *rel, Index rti,
										RangeTblEntry *rte);
static inline bool IsAMergeOuterQuery(PlannerInfo *root, RelOptInfo *rel);
extern bool ForceRUMIndexScanToBitmapHeapScan;

planner_hook_type ExtensionPreviousPlannerHook = NULL;
set_rel_pathlist_hook_type ExtensionPreviousSetRelPathlistHook = NULL;
explain_get_index_name_hook_type ExtensionPreviousIndexNameHook = NULL;


/*
 * HelioApiPlanner  transforms the query tree before passing it
 * to the planner.
 */
PlannedStmt *
HelioApiPlanner(Query *parse, const char *queryString, int cursorOptions,
				ParamListInfo boundParams)
{
	bool hasUnresolvedParams = false;
	int queryFlags = 0;
	bool isNonExistentCollection = false;
	if (IsHelioApiExtensionActive())
	{
		if (IsReadWriteCommand(parse))
		{
			ThrowIfWriteCommandNotAllowed();
		}

		queryFlags = MongoQueryFlags(parse);
		if (queryFlags & HAS_AGGREGATION_FUNCTION)
		{
			parse = (Query *) ExpandAggregationFunction(parse, boundParams);
		}

		/* replace the @@ operators and inject shard_key_value filters */
		if (queryFlags & HAS_QUERY_OPERATOR ||
			queryFlags & HAS_MONGO_COLLECTION_RTE)
		{
			parse = (Query *) ReplaceBsonQueryOperators(parse, boundParams);
		}

		/* the collection replace needs to happen *after* the query rewrite. */
		/* this is to handle cases where there's an invalid query against a collection */
		/* that doesn't exist. We need to error out from the invalid query first */
		/* (see count11.js) */
		if (queryFlags & HAS_MONGO_COLLECTION_RTE)
		{
			parse = ReplaceMongoCollectionFunction(parse, boundParams,
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

	PlannedStmt *plan = NULL;

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


/*
 * Given a primary key lookup query, trims the restrictInfo based on the lookup
 */
static void
TrimPrimaryKeyQuals(List *restrictInfo, IndexPath *primaryKeyPath)
{
	RestrictInfo *objectIdFilter = NULL;
	AttrNumber objectAttr = InvalidAttrNumber;
	Const *rightConst = NULL;
	Oid opNo = InvalidOid;
	pgbsonelement queryElement = { 0 };
	bson_value_t objectIdColumnFilter = { 0 };

	ListCell *cell;
	foreach(cell, primaryKeyPath->indexclauses)
	{
		IndexClause *iclause = lfirst_node(IndexClause, cell);
		if (iclause->indexcol == 1 && list_length(iclause->indexquals) > 0)
		{
			objectIdFilter = linitial(iclause->indexquals);

			if (TryExtractDataFromRestrictInfo(objectIdFilter, &objectAttr, &opNo,
											   &rightConst) &&
				opNo == BsonEqualOperatorId() &&
				TryGetSinglePgbsonElementFromPgbson(DatumGetPgBsonPacked(
														rightConst->constvalue),
													&queryElement))
			{
				objectIdColumnFilter = queryElement.bsonValue;
			}
		}
	}

	/* Deterministically pick the primary key for $in/$eq similar to the path below */
	if (opNo == BsonEqualOperatorId() || IsA(objectIdFilter->clause, ScalarArrayOpExpr))
	{
		primaryKeyPath->path.startup_cost = 0;
		primaryKeyPath->path.total_cost = 0;
	}

	if (objectIdColumnFilter.value_type == BSON_TYPE_EOD)
	{
		return;
	}

	foreach(cell, restrictInfo)
	{
		RestrictInfo *rinfo = lfirst_node(RestrictInfo, cell);
		if (TryExtractDataFromRestrictInfo(rinfo, &objectAttr, &opNo, &rightConst) &&
			objectAttr == MONGO_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER &&
			opNo == BsonEqualMatchRuntimeOperatorId() &&
			TryGetSinglePgbsonElementFromPgbson(DatumGetPgBsonPacked(
													rightConst->constvalue),
												&queryElement) &&
			queryElement.pathLength == 3 && strcmp(queryElement.path, "_id") == 0 &&
			BsonValueEquals(&objectIdColumnFilter, &queryElement.bsonValue))
		{
			cell->ptr_value = objectIdFilter;
			return;
		}
	}
}


/*
 * Given that our RUM index cost is 0, currently, the planner may prefer putting in
 * the RUM index over other indexes that may be available. This is bad for primary key
 * lookup scenarios. Consequently, in cases where we have the primary key available
 * force-add a btree lookup.
 * TODO: Remove once we have proper statistics and costs for RUM index.
 */
static void
AddPointLookupQuery(List *restrictInfo, PlannerInfo *root, RelOptInfo *rel)
{
	RestrictInfo *shardKeyFilter = NULL;
	RestrictInfo *objectIdFilter = NULL;
	int32_t docObjectIdFilterEqualsIndex = -1;
	bson_value_t docObjectIdFilter = { 0 };
	bson_value_t objectIdColumnFilter = { 0 };
	AttrNumber objectAttr = InvalidAttrNumber;
	Const *rightConst = NULL;
	Oid opNo = InvalidOid;
	pgbsonelement queryElement = { 0 };

	ListCell *cell;
	foreach(cell, restrictInfo)
	{
		RestrictInfo *rinfo = lfirst_node(RestrictInfo, cell);
		if (IsA(rinfo->clause, OpExpr))
		{
			if (!TryExtractDataFromRestrictInfo(rinfo, &objectAttr, &opNo, &rightConst))
			{
				continue;
			}

			if (objectAttr == MONGO_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER &&
				opNo == BigintEqualOperatorId())
			{
				shardKeyFilter = rinfo;
				continue;
			}

			if (objectAttr == MONGO_DATA_TABLE_OBJECT_ID_VAR_ATTR_NUMBER &&
				opNo == BsonEqualOperatorId())
			{
				objectIdFilter = rinfo;
				if (TryGetSinglePgbsonElementFromPgbson(DatumGetPgBsonPacked(
															rightConst->constvalue),
														&queryElement))
				{
					objectIdColumnFilter = queryElement.bsonValue;
				}

				continue;
			}

			if (objectAttr == MONGO_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER &&
				opNo == BsonEqualMatchRuntimeOperatorId() &&
				TryGetSinglePgbsonElementFromPgbson(DatumGetPgBsonPacked(
														rightConst->constvalue),
													&queryElement) &&
				queryElement.pathLength == 3 && strcmp(queryElement.path, "_id") == 0)
			{
				docObjectIdFilterEqualsIndex = list_cell_number(restrictInfo, cell);
				docObjectIdFilter = queryElement.bsonValue;
			}
		}
		else if (IsA(rinfo->clause, ScalarArrayOpExpr))
		{
			/* Object_id IN fields */
			ScalarArrayOpExpr *arrayExpr = (ScalarArrayOpExpr *) rinfo->clause;
			if (!arrayExpr->useOr)
			{
				continue;
			}

			if (list_length(arrayExpr->args) != 2)
			{
				continue;
			}

			Expr *leftExpr = linitial(arrayExpr->args);
			if (!IsA(leftExpr, Var))
			{
				continue;
			}

			Var *leftVar = (Var *) leftExpr;
			if (leftVar->varattno == MONGO_DATA_TABLE_OBJECT_ID_VAR_ATTR_NUMBER &&
				arrayExpr->opno == BsonEqualOperatorId())
			{
				objectIdFilter = rinfo;
			}
		}
	}

	if (shardKeyFilter != NULL && objectIdFilter != NULL && rel->indexlist != NIL)
	{
		ListCell *index;
		foreach(index, rel->indexlist)
		{
			IndexOptInfo *indexInfo = lfirst(index);
			if (IsBtreePrimaryKeyIndex(indexInfo))
			{
				IndexClause *shardKeyClause = makeNode(IndexClause);
				shardKeyClause->rinfo = shardKeyFilter;
				shardKeyClause->indexquals = list_make1(shardKeyFilter);
				shardKeyClause->lossy = false;
				shardKeyClause->indexcol = 0;
				shardKeyClause->indexcols = NIL;

				IndexClause *objectIdClause = makeNode(IndexClause);
				objectIdClause->rinfo = objectIdFilter;
				objectIdClause->indexquals = list_make1(objectIdFilter);
				objectIdClause->lossy = false;
				objectIdClause->indexcol = 1;
				objectIdClause->indexcols = NIL;

				List *clauses = list_make2(shardKeyClause, objectIdClause);
				List *orderbys = NIL;
				List *orderbyCols = NIL;
				List *pathKeys = NIL;
				bool indexOnly = false;
				Relids outerRelids = NULL;
				double loopCount = 1;
				bool partialPath = false;
				IndexPath *path = create_index_path(root, indexInfo, clauses, orderbys,
													orderbyCols, pathKeys,
													ForwardScanDirection, indexOnly,
													outerRelids,
													loopCount, partialPath);
				path->indextotalcost = 0;
				path->path.startup_cost = 0;
				path->path.total_cost = 0;
				add_path(rel, (Path *) path);

				if (objectIdColumnFilter.value_type != BSON_TYPE_EOD &&
					docObjectIdFilter.value_type != BSON_TYPE_EOD &&
					BsonValueEquals(&objectIdColumnFilter, &docObjectIdFilter))
				{
					/* We can swap out the docId with the objectId */
					list_nth_cell(restrictInfo, docObjectIdFilterEqualsIndex)->ptr_value =
						objectIdFilter;
				}

				return;
			}
		}
	}
}


static void
ExtensionRelPathlistHookCore(PlannerInfo *root, RelOptInfo *rel, Index rti,
							 RangeTblEntry *rte)
{
	bool isMongoDataNamespace = false;
	uint64 collectionId = 0;
	bool isShardQuery = IsRTEShardForMongoCollection(rte, &isMongoDataNamespace,
													 &collectionId);

	if (!isMongoDataNamespace)
	{
		/* Skip looking for queries not pertaining to mongo data tables */
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

	ReplaceExtensionFunctionContext indexContext = {
		.queryDataForVectorSearch = { 0 },
		.hasVectorSearchQuery = false,
		.hasTextIndexQuery = false,
		.indexOptionsForText = { 0 },
		.primaryKeyLookupPath = NULL,
		.inputData = {
			.collectionId = collectionId,
			.isRuntimeTextScan = false,
			.isShardQuery = isShardQuery
		}
	};

	/*
	 * Replace all function operators that haven't been transformed in indexed
	 * paths into OpExpr clauses.
	 */
	ReplaceExtensionFunctionOperatorsInPaths(root, rel, rel->pathlist, PARENTTYPE_NONE,
											 &indexContext);

	/* If the query is operating on the shard of a distributed table
	 * (or a normal non mongo-table), then we swap this out with the operators
	 */
	if (indexContext.primaryKeyLookupPath != NULL)
	{
		TrimPrimaryKeyQuals(rel->baserestrictinfo, indexContext.primaryKeyLookupPath);
	}
	else
	{
		AddPointLookupQuery(rel->baserestrictinfo, root, rel);
	}

	rel->baserestrictinfo =
		ReplaceExtensionFunctionOperatorsInRestrictionPaths(rel->baserestrictinfo,
															&indexContext);

	if (indexContext.hasTextIndexQuery &&
		indexContext.indexOptionsForText.indexOptions == NULL)
	{
		/* In case we got a scenario where the query planner thought it was better to push to a different
		 * index or a seqscan, instead of just failing, try to find the index options and update the
		 * restriction paths: This differs from Mongo that tries to force the text index anyway.
		 */
		indexContext.indexOptionsForText.indexOptions = TryFindTextIndexForRelation(
			rel, rte);
		if (indexContext.indexOptionsForText.indexOptions != NULL)
		{
			indexContext.inputData.isRuntimeTextScan = true;
			rel->baserestrictinfo =
				ReplaceExtensionFunctionOperatorsInRestrictionPaths(rel->baserestrictinfo,
																	&indexContext);
		}
	}

	/* Now before modifying any paths, walk to check for raw path optimizations */
	UpdatePathsWithOptimizedExtensionCustomPlans(root, rel, rte);

	/*
	 * Update any paths with custom scans as appropriate.
	 */
	bool updatedPaths = UpdatePathsWithExtensionCustomPlans(root, rel, rte);
	if (!updatedPaths)
	{
		/* Not a streaming cursor scenario.
		 * Streaming cursors auto convert into Bitmap Paths.
		 * Handle force conversion of bitmap paths.
		 */
		if (ForceRUMIndexScanToBitmapHeapScan && !IsAMergeOuterQuery(root, rel))
		{
			UpdatePathsToForceRumIndexScanToBitmapHeapScan(root, rel);
		}
	}

	/* For vector, text search inject custom scan path to track lifetime of
	 * $meta/ivfprobes.
	 */
	if (indexContext.hasTextIndexQuery &&
		indexContext.indexOptionsForText.indexOptions != NULL &&
		indexContext.indexOptionsForText.query != (Datum) 0)
	{
		AddExtensionQueryScanForTextQuery(root, rel, rte,
										  &indexContext.indexOptionsForText);
	}
	else if (indexContext.hasVectorSearchQuery)
	{
		AddExtensionQueryScanForVectorQuery(root, rel, rte,
											&indexContext.queryDataForVectorSearch);
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
	if (IsHelioApiExtensionActive())
	{
		ExtensionRelPathlistHookCore(root, rel, rti, rte);
	}

	if (ExtensionPreviousSetRelPathlistHook != NULL)
	{
		ExtensionPreviousSetRelPathlistHook(root, rel, rti, rte);
	}
}


/*
 * MongoQueryFlags determines whether the given query tree contains
 * extension-specific constructs that are relevant to the planner.
 */
static int
MongoQueryFlags(Query *query)
{
	int queryFlags = 0;

	MongoQueryFlagsWalker((Node *) query, &queryFlags);

	return queryFlags;
}


/*
 * MongoQueryFlagsWalker determines whether the given expression tree contains
 * extension-specific constructs that are relevant to the planner.
 */
static bool
MongoQueryFlagsWalker(Node *node, int *queryFlags)
{
	CHECK_FOR_INTERRUPTS();

	if (node == NULL)
	{
		return false;
	}

	if (IsA(node, RangeTblEntry))
	{
		RangeTblEntry *rte = (RangeTblEntry *) node;

		if (IsMongoCollectionBasedRTE(rte))
		{
			*queryFlags |= HAS_MONGO_COLLECTION_RTE;
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
			if (list_length(funcExpr->args) != 2)
			{
				return false;
			}

			if (funcExpr->funcresulttype != BsonTypeId() ||
				!funcExpr->funcretset)
			{
				return false;
			}

			if (funcExpr->funcid == ApiCatalogAggregationPipelineFunctionId() ||
				funcExpr->funcid == ApiCatalogAggregationFindFunctionId() ||
				funcExpr->funcid == ApiCatalogAggregationCountFunctionId() ||
				funcExpr->funcid == ApiCatalogAggregationDistinctFunctionId())
			{
				*queryFlags |= HAS_AGGREGATION_FUNCTION;
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
			*queryFlags |= HAS_QUERY_OPERATOR;
		}

		return false;
	}
	else if (IsA(node, FuncExpr))
	{
		FuncExpr *funcExpr = (FuncExpr *) node;

		if (funcExpr->funcid == ApiCursorStateFunctionId())
		{
			*queryFlags |= HAS_CURSOR_FUNC;

			Node *queryNode = lsecond(funcExpr->args);
			if (IsA(queryNode, Param))
			{
				*queryFlags |= HAS_CURSOR_STATE_PARAM;
			}
		}

		return false;
	}
	else if (IsA(node, Query))
	{
		return query_tree_walker((Query *) node, MongoQueryFlagsWalker,
								 queryFlags, QTW_EXAMINE_RTES_BEFORE);
	}

	return expression_tree_walker(node, MongoQueryFlagsWalker, queryFlags);
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
 * ReplaceMongoCollectionFunction replaces all occurences of the
 * ApiSchema.collection() function call with the corresponding
 * table.
 */
static Query *
ReplaceMongoCollectionFunction(Query *query, ParamListInfo boundParams,
							   bool *isNonExistentCollection)
{
	/*
	 * We will change a function RTE into a relation RTE so we can use
	 * a regular walker that does not copy the whole query tree.
	 */
	ReplaceMongoCollectionContext context =
	{
		.boundParams = boundParams,
		.isNonExistentCollection = false,
		.query = query
	};
	ReplaceMongoCollectionFunctionWalker((Node *) query, &context);
	*isNonExistentCollection = context.isNonExistentCollection;
	return query;
}


/*
 * ReplaceMongoCollectionFunctionWalker recurses into the input to replace
 * all occurences of the ApiSchema.collection() function call with the corresponding
 * table.
 */
static bool
ReplaceMongoCollectionFunctionWalker(Node *node, ReplaceMongoCollectionContext *context)
{
	CHECK_FOR_INTERRUPTS();

	if (node == NULL)
	{
		return false;
	}

	if (IsA(node, RangeTblEntry))
	{
		RangeTblEntry *rte = (RangeTblEntry *) node;

		if (IsResolvableMongoCollectionBasedRTE(rte, context->boundParams))
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
				 * MongoDB treats non-existent collections as empty.
				 * Here we replace the ApiSchema.collection() function call
				 * empty_data_table() which returns an response mimicking SELECT
				 * from an empty mongo data collection.
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
										ReplaceMongoCollectionFunctionWalker,
										context, QTW_EXAMINE_RTES_BEFORE);
		context->query = originalQuery;
		return result;
	}

	return expression_tree_walker(node, ReplaceMongoCollectionFunctionWalker,
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
 * IsResolvableMongoCollectionBasedRTE returns whether the given node is a function RTE
 * of the form ApiSchema.*collection*('db', 'coll', ...).
 *
 * Otherwise, we return false, thereby allowing the RTE_FUNCTION to be called directly, and not
 * changing it to a RTE_RELATION.
 */
bool
IsResolvableMongoCollectionBasedRTE(RangeTblEntry *rte, ParamListInfo boundParams)
{
	if (!IsMongoCollectionBasedRTE(rte))
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
		funcExpr->funcid == HelioApiCollectionFunctionId())
	{
		return true;
	}

	return false;
}


/*
 * IsMongoCollectionBasedRTE returns whether the given node is a
 * collection() RTE.
 */
bool
IsMongoCollectionBasedRTE(RangeTblEntry *rte)
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
		funcExpr->funcid != HelioApiCollectionFunctionId())
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
	if (IsHelioApiExtensionActive())
	{
		bool useLibPQ = true;
		const char *mongoIndexName = ExtensionIndexOidGetIndexName(indexId, useLibPQ);
		if (mongoIndexName != NULL)
		{
			return mongoIndexName;
		}
	}

	if (ExtensionPreviousIndexNameHook != NULL)
	{
		return ExtensionPreviousIndexNameHook(indexId);
	}

	return IndexIdGetIndexNameDefault(indexId);
}


/*
 * Given a postgres index name, returns the corresponding mongo index name if available.
 */
const char *
GetHelioIndexNameFromPostgresIndex(const char *pgIndexName, bool useLibPq)
{
	int prefixLength = strlen(MONGO_DATA_TABLE_INDEX_NAME_FORMAT_PREFIX);
	if (strncmp(pgIndexName, MONGO_DATA_TABLE_INDEX_NAME_FORMAT_PREFIX,
				prefixLength) == 0)
	{
		int64 indexIdValue = atoll(pgIndexName + prefixLength);
		StringInfo indexNameQuery = makeStringInfo();
		appendStringInfo(indexNameQuery,
						 "SELECT (index_spec).index_name FROM %s.collection_indexes WHERE index_id = %ld",
						 ApiCatalogSchemaName, indexIdValue);

		const char *indexName = NULL;
		if (useLibPq)
		{
			indexName = ExtensionExecuteQueryOnLocalhostViaLibPQ(indexNameQuery->data);
		}
		else
		{
			bool readOnly = true;
			bool isNull;
			Datum resultDatum = ExtensionExecuteQueryViaSPI(indexNameQuery->data,
															readOnly,
															SPI_OK_SELECT, &isNull);
			if (!isNull)
			{
				indexName = TextDatumGetCString(resultDatum);
			}
		}

		return indexName;
	}
	else if (strncmp(pgIndexName, MONGO_DATA_PRIMARY_KEY_FORMAT_PREFIX,
					 strlen(MONGO_DATA_PRIMARY_KEY_FORMAT_PREFIX)) == 0)
	{
		/* this is the _id index on mongo */
		return ID_INDEX_NAME;
	}

	return NULL;
}


/*
 * Retrieves the "mongo" index name for a given indexId.
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
	const char *indexName = GetHelioIndexNameFromPostgresIndex(pgIndexName, useLibPq);
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

		/* check whether parameter is available */
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


static bytea *
TryFindTextIndexForRelation(RelOptInfo *rel, RangeTblEntry *rte)
{
	List *indexList = rel->indexlist;
	if (rel->indexlist == NIL && rte->relid != InvalidOid)
	{
		Relation relation = RelationIdGetRelation(rte->relid);
		if (relation != NULL)
		{
			indexList = RelationGetIndexList(relation);
			RelationClose(relation);
		}
	}

	ListCell *index;
	foreach(index, indexList)
	{
		IndexOptInfo *indexInfo = lfirst(index);
		if (indexInfo->opclassoptions == NULL)
		{
			continue;
		}

		if (indexInfo->relam == RumIndexAmId())
		{
			for (int i = 0; i < indexInfo->nkeycolumns; i++)
			{
				if (indexInfo->opclassoptions[i] != NULL &&
					indexInfo->opfamily[i] == BsonRumTextPathOperatorFamily())
				{
					return (bytea *) indexInfo->opclassoptions[i];
				}
			}
		}
	}

	return NULL;
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
	if (IsShardTableForMongoTable(relName, numEndPointer))
	{
		*collectionId = parsedCollectionId;
		return true;
	}

	return false;
}


/*
 * Returns true if the relation of RTE pointed to
 * is a Mongo table base collection. e.g.
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
IsRTEShardForMongoCollection(RangeTblEntry *rte, bool *isMongoDataNamespace,
							 uint64 *collectionId)
{
	if (rte->rtekind != RTE_RELATION ||
		rte->relkind != RELKIND_RELATION)
	{
		return false;
	}

	Oid tableOid = rte->relid;
	Oid relNamespace = get_rel_namespace(tableOid);

	*isMongoDataNamespace = relNamespace == ApiDataNamespaceOid();
	if (!*isMongoDataNamespace)
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
		  funcExpr->funcid == DeleteWorkerFunctionOid()))
	{
		return false;
	}

	/* It's a shard query for a update worker projector
	 * Transform this query into a FuncRTE with a Var projector
	 */
	entry->expr = (Expr *) makeVar(rti, 1, HelioCoreBsonTypeId(), -1,
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
 * Determine if the current relation is the outer query of a $merge stage.
 * We do not push this relation to the bitmap index.
 * For the outer relation, the relid will always be 1 since $merge is the last stage of the pipeline.
 */
static inline bool
IsAMergeOuterQuery(PlannerInfo *root, RelOptInfo *rel)
{
	return root->parse->commandType == CMD_MERGE && rel->relid == 1;
}
