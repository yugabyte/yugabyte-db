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
#include <optimizer/pathnode.h>
#include "nodes/pg_list.h"
#include <pg_config_manual.h>

#include "query/query_operator.h"
#include "planner/mongo_query_operator.h"
#include "opclass/helio_index_support.h"
#include "opclass/helio_gin_index_mgmt.h"
#include "opclass/helio_bson_text_gin.h"
#include "metadata/metadata_cache.h"
#include "utils/helio_errors.h"
#include "vector/vector_utilities.h"
#include "vector/vector_spec.h"
#include "utils/version_utils.h"
#include "query/helio_bson_compare.h"

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


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static Expr * HandleSupportRequestCondition(SupportRequestIndexCondition *req);
static Path * ReplaceFunctionOperatorsInPlanPath(PlannerInfo *root, RelOptInfo *rel,
												 Path *path, PlanParentType parentType,
												 ReplaceExtensionFunctionContext *context);
static Expr * ProcessRestrictionInfoAndRewriteFuncExpr(Expr *clause,
													   ReplaceExtensionFunctionContext *
													   context);
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
	else if (IsClusterVersionAtleastThis(1, 19, 0) && req->funcid ==
			 BsonDollarMergeJoinFunctionOid())
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
	return firstArgVar->varattno == MONGO_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER &&
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
		rinfo->clause = ProcessRestrictionInfoAndRewriteFuncExpr(rinfo->clause,
																 context);
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
		   indexInfo->indexkeys[0] == MONGO_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER &&
		   indexInfo->indexkeys[1] == MONGO_DATA_TABLE_OBJECT_ID_VAR_ATTR_NUMBER;
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


/* Queries of the form */
/* ``` */
/* SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "val": { "$in": [1, 3]} } }'); */
/* ``` */
/* Will be **effectively** rewritten to */
/* ``` */
/* SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "$or": [{ "accid":1, "vid": 1, "val": 1}, { "accid":1, "vid": 1, "val": 3}] } }'); */
/* ``` */
/* However, we don't intercept the mongo query and rewrite it. As the `$in` can be used in different query path. Instead, we do the rewrite after the query plan generation, and specifically, when `$in` is shows up a condition in the Index Path. While `$in` today is pushed down to the index, it's not performant.  However, if we rewrite the `$in` to be a `OR` of a set of `$eq` clause, that becomes peformant, as `RUM fast scan` is enabled for `$eq` but not yet for `$in`. */

/* **_Explain before rewrite:_** */
/* ``` */
/*                ->  Bitmap Index Scan on val_accid_vid  (cost=0.00..0.00 rows=1 width=0) (actual time=23.937..23.937 rows=7 loops=1) */
/*                       Index Cond: Index Cond: ((document OPERATOR(mongo_catalog.@*=) '[{ "val" : { "$numberInt" : "1" } }, { "val" : { "$numberInt" : "3" } }]'::mongo_catalog.bson) AND (document OPERATOR(mongo_catalog.@=) '{ "accid" : { "$numberInt" : "1" } }'::mongo_catalog.bson) AND (document OPERATOR(mongo_catalog.@=) '{ "vid" : { "$numberInt" : "1" } }'::mongo_catalog.bson))* / */

/* ``` */
/* **_Explain after rewrite:_** */
/* ``` */

/* Custom Scan (Citus Adaptive) */
/*    Task Count: 1 */
/*    Tasks Shown: All */
/*    ->  Task */
/*          Node: host=localhost port=58080 dbname=regression */
/*          ->  Bitmap Heap Scan on documents_33000_330023 collection */
/*                Recheck Cond: (((document OPERATOR(mongo_catalog.@=) '{ "val" : { "$numberInt" : "1" } }'::mongo_catalog.bson) AND (document OPERATOR(mongo_catalog.@=) '{ "accid" : { "$numberInt" : "1" } }'::mongo_catalog.bson) AND (document OPERATOR(mongo_catalog.@=) '{ "vid" : { "$numberInt" : "1" } }'::mongo_catalog.bson)) OR ((document OPERATOR(mongo_catalog.@=) '{ "val" : { "$numberInt" : "3" } }'::mongo_catalog.bson) AND (document OPERATOR(mongo_catalog.@=) '{ "accid" : { "$numberInt" : "1" } }'::mongo_catalog.bson) AND (document OPERATOR(mongo_catalog.@=) '{ "vid" : { "$numberInt" : "1" } }'::mongo_catalog.bson))) */
/*                Filter: ((document OPERATOR(mongo_catalog.@*=) '{ "val" : [ { "$numberInt" : "1" }, { "$numberInt" : "3" } ] }'::mongo_catalog.bson) AND (shard_key_value = '4322365043291501017'::bigint)) */
/*                ->  BitmapOr */
/*                      ->  Bitmap Index Scan on val_accid_vid */
/*                            Index Cond: ((document OPERATOR(mongo_catalog.@=) '{ "val" : { "$numberInt" : "1" } }'::mongo_catalog.bson) AND (document OPERATOR(mongo_catalog.@=) '{ "accid" : { "$numberInt" : "1" } }'::mongo_catalog.bson) AND (document OPERATOR(mongo_catalog.@=) '{ "vid" : { "$numberInt" : "1" } }'::mongo_catalog.bson)) */
/*                      ->  Bitmap Index Scan on val_accid_vid */
/*                            Index Cond: ((document OPERATOR(mongo_catalog.@=) '{ "val" : { "$numberInt" : "3" } }'::mongo_catalog.bson) AND (document OPERATOR(mongo_catalog.@=) '{ "accid" : { "$numberInt" : "1" } }'::mongo_catalog.bson) AND (document OPERATOR(mongo_catalog.@=) '{ "vid" : { "$numberInt" : "1" } }'::mongo_catalog.bson)) */
/* (13 rows) */
/* ``` */
static Path *
OptimizeIndexExpressionsForDollarIn(PlannerInfo *root, RelOptInfo *rel, Path *path,
									PlanParentType parentType)
{
	IndexPath *indexPath = (IndexPath *) path;

	/* 1. Only do the rewrite for RUM Index
	 * 2. Not supporting nested BitmapOr*/
	if (indexPath->indexinfo->relam != RumIndexAmId() || parentType ==
		PARENTTYPE_INVALID || list_length(indexPath->indexclauses) == 1)
	{
		return path;
	}

	List *indexClauses = indexPath->indexclauses;
	ListCell *indexPathCell;
	IndexClause *inIndexClause = NULL;
	int inCount = 0;
	foreach(indexPathCell, indexClauses)
	{
		IndexClause *iclause = (IndexClause *) lfirst(indexPathCell);
		RestrictInfo *rinfo = iclause->rinfo;

		if (!IsA(rinfo->clause, OpExpr))
		{
			continue;
		}

		OpExpr *opExpr = (OpExpr *) rinfo->clause;
		if (opExpr->opno == BsonInOperatorId())
		{
			inIndexClause = iclause;
			inCount++;
		}
	}

	if (inIndexClause == NULL || inCount > 1)
	{
		return path;
	}

	/* Example $in Query Index Condition:
	 *  mongo_catalog.@*=(document, '{ "val" : [ { "$numberInt" : "1" }, { "$numberInt" : "3" } ] }'::mongo_catalog.bson) */
	OpExpr *inOpExpr = (OpExpr *) inIndexClause->rinfo->clause;
	Node *inList = lsecond(inOpExpr->args);

	if (!IsA(inList, Const))
	{
		return path;
	}

	Const *inListConst = (Const *) inList;
	pgbson *inQueryConditionBson = (pgbson *) DatumGetPgBson(inListConst->constvalue);
	pgbsonelement inQueryBson;
	PgbsonToSinglePgbsonElement(inQueryConditionBson, &inQueryBson);

	Assert(inQueryBson.bsonValue.value_type == BSON_TYPE_ARRAY);
	bson_iter_t arrayIter;
	BsonValueInitIterator(&inQueryBson.bsonValue, &arrayIter);
	List *orPaths = NIL;
	IndexPath *newIndexPath = NULL;

	while (bson_iter_next(&arrayIter))
	{
		const bson_value_t *inItemValue = bson_iter_value(&arrayIter);

		/* If the $in contains a Regex we skip the rewrite. There is an existing bug with the $regex code path
		 * that makes the rewrite not possible. Also, as a temporary optimization $regex is not our primary concern. */
		if (inItemValue->value_type == BSON_TYPE_REGEX)
		{
			return path;
		}

		pgbson_writer clauseWriter;
		PgbsonWriterInit(&clauseWriter);
		PgbsonWriterAppendValue(&clauseWriter, inQueryBson.path,
								inQueryBson.pathLength, inItemValue);

		Const *bsonConst = makeConst(BsonTypeId(), -1, InvalidOid, -1, PointerGetDatum(
										 PgbsonWriterGetPgbson(&clauseWriter)), false,
									 false);

		OpExpr *elementOpExpr = (OpExpr *) make_opclause(BsonEqualMatchOperatorId(),
														 BOOLOID, false,
														 linitial(inOpExpr->args),
														 (Expr *) bsonConst, InvalidOid,
														 InvalidOid);
		elementOpExpr->opfuncid = BsonEqualMatchIndexFunctionId();


		newIndexPath = makeNode(IndexPath);
		newIndexPath->path = indexPath->path;

		List *copyIndexClauses = NULL;
		foreach(indexPathCell, indexClauses)
		{
			IndexClause *iclause = (IndexClause *) lfirst(indexPathCell);
			RestrictInfo *rinfo = iclause->rinfo;
			OpExpr *opExpr = (OpExpr *) rinfo->clause;

			if (opExpr->opno == BsonInOperatorId())
			{
				IndexClause *newClause = makeNode(IndexClause);
				newClause->rinfo = copyObject(rinfo);
				newClause->rinfo->clause = (Expr *) elementOpExpr;
				newClause->indexquals = list_make1(newClause->rinfo);
				newClause->lossy = iclause->lossy;
				newClause->indexcol = iclause->indexcol;
				newClause->indexcols = iclause->indexcols;

				iclause = newClause;
			}


			copyIndexClauses = (copyIndexClauses == NIL) ? list_make1(iclause) : lappend(
				copyIndexClauses, iclause);
		}

		newIndexPath->indexclauses = copyIndexClauses;
		newIndexPath->indexorderbys = indexPath->indexorderbys;
		newIndexPath->indexorderbycols = indexPath->indexorderbycols;
		newIndexPath->indexinfo = indexPath->indexinfo;
		newIndexPath->indexscandir = indexPath->indexscandir;
		newIndexPath->indextotalcost = indexPath->indextotalcost;
		newIndexPath->indexselectivity = indexPath->indexselectivity;

		/* Copy cost and size fields */

		orPaths = lappend(orPaths, newIndexPath);
	}

	if (list_length(orPaths) == 0)
	{
		return path;
	}

	Path *reWrittenPath = NULL;
	if (list_length(orPaths) == 1)
	{
		reWrittenPath = (Path *) newIndexPath;
	}
	else
	{
		reWrittenPath = (Path *) create_bitmap_or_path(root, rel, orPaths);
	}

	if (parentType == PARENTTYPE_BITMAPHEAP)
	{
		return reWrittenPath;
	}
	else if (parentType == PARENTTYPE_NONE)
	{
		return (Path *) create_bitmap_heap_path(root, rel, reWrittenPath,
												rel->lateral_relids, 1.0,
												0);
	}
	else
	{
		return path;
	}
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
		const VectorIndexDefinition *vectorDefinition =
			GetVectorIndexDefinitionByIndexAmOid(
				indexPath->indexinfo->relam);
		if (vectorDefinition != NULL)
		{
			context->hasVectorSearchQuery = true;
		}

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
		else if (vectorDefinition != NULL)
		{
			/*
			 *  indexPath->indexorderbys contains a list of order by expressions. For vector search, it is of the following form.
			 *  Order By: (vector(ApiCatalogSchemaName.bson_extract_vector(collection.document, 'vectorPath'::text), 3, true) <#> '[3,4.9,1]'::vector)
			 *
			 *  OpExpr (FuncExpr (FuncExpr(document, CosntVectorPath), CosntDimension, ConstTrue), OpId, ConstVector)
			 *
			 *  Here we extarct:
			 *      1. The path name 'vectorPath' on which the index is defined.
			 *      2. Vector serch operator (<#> stands for COSINE)
			 *      3. The query vector [3, 4, 9, 1]
			 *
			 *  And store that for future usage by the $meta which would use this information to compute score for the resulting vectors.
			 */
			OpExpr *sortExpr = (OpExpr *) linitial(indexPath->indexorderbys);


			FuncExpr *vectorCastFunc = (FuncExpr *) linitial(sortExpr->args);
			FuncExpr *bsonExtractFunc = (FuncExpr *) linitial(vectorCastFunc->args);
			Const *vectorPathConst = (Const *) lsecond(bsonExtractFunc->args);

			Const *vectorConst = (Const *) lsecond(sortExpr->args);

			context->queryDataForVectorSearch.VectorPathName =
				vectorPathConst->constvalue;
			context->queryDataForVectorSearch.QueryVector = vectorConst->constvalue;
			context->queryDataForVectorSearch.SimilaritySearchOpOid = sortExpr->opno;
			context->queryDataForVectorSearch.VectorAccessMethodOid =
				indexPath->indexinfo->relam;

			/*
			 * For vector search, we also need to extract the search parameter from the wrap function.
			 * ApiCatalogSchemaName.bson_search_param(document, '{ "nProbes": 4 }'::ApiCatalogSchemaName.bson)
			 */
			ExtractAndSetSearchParamterFromWrapFunction(indexPath, context);
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
					context->hasTextIndexQuery = true;
					if (context->indexOptionsForText.indexOptions == NULL)
					{
						context->indexOptionsForText.indexOptions = options;
					}
					else
					{
						ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
										errmsg("Too many text expressions")));
					}

					ReplaceExtensionFunctionContext childContext = {
						{ 0 }, { 0 }, false, false, false, context->inputData
					};
					childContext.indexOptionsForText.indexOptions = options;
					rinfo->clause = ProcessRestrictionInfoAndRewriteFuncExpr(
						rinfo->clause,
						&childContext);
					context->indexOptionsForText = childContext.indexOptionsForText;
				}
				else
				{
					ReplaceExtensionFunctionContext childContext = {
						{ 0 }, { 0 }, false, false, false, context->inputData
					};
					rinfo->clause = ProcessRestrictionInfoAndRewriteFuncExpr(
						rinfo->clause,
						&childContext);
				}
			}

			indexPath->indexclauses = OptimizeIndexExpressionsForRange(
				indexPath->indexclauses);
			if (EnableInQueryOptimization)
			{
				path = OptimizeIndexExpressionsForDollarIn(root, rel, path, parentType);
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
										 ReplaceExtensionFunctionContext *context)
{
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
			context->hasTextIndexQuery = true;
			if (context->indexOptionsForText.indexOptions != NULL)
			{
				/* TODO: Make TextIndex force use the index path if available
				 * Today this isn't guaranteed if there's another path picked
				 * e.g. ORDER BY object_id.
				 */
				context->inputData.isRuntimeTextScan = true;
				OpExpr *expr = GetOpExprClauseFromIndexOperator(operator, args,
																context->
																indexOptionsForText.
																indexOptions);
				Expr *finalExpr = (Expr *) GetFuncExprForTextWithIndexOptions(
					expr->args, context->indexOptionsForText.indexOptions,
					context->inputData.isRuntimeTextScan,
					&context->indexOptionsForText);
				if (finalExpr != NULL)
				{
					return finalExpr;
				}
			}
		}
		else if (operator->indexStrategy != BSON_INDEX_STRATEGY_INVALID)
		{
			return (Expr *) GetOpExprClauseFromIndexOperator(operator, args,
															 context->indexOptionsForText.
															 indexOptions);
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
			processedBoolArgs = lappend(processedBoolArgs,
										ProcessRestrictionInfoAndRewriteFuncExpr(
											innerExpr, context));
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
