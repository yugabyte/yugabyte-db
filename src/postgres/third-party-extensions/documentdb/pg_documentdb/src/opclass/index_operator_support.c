/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/opclass/index_operator_support.c
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

#include "metadata/index.h"
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
#include "index_am/index_am_utils.h"
#include "query/bson_dollar_selectivity.h"
#include "planner/documentdb_planner.h"
#include "aggregation/bson_query_common.h"
#include "operators/bson_expression.h"

extern bool EnableExprLookupIndexPushdown;

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */
PG_FUNCTION_INFO_V1(dollar_expr_support);


static bool ExprCanBePushedToIndex(SupportRequestIndexCondition *supportRequest);
static List * PushExprToIndex(SupportRequestIndexCondition *supportRequest);


/*
 * handles function support for the $expr filter function
 */
Datum
dollar_expr_support(PG_FUNCTION_ARGS)
{
	Node *supportRequest = (Node *) PG_GETARG_POINTER(0);
	Pointer responsePointer = NULL;
	if (IsA(supportRequest, SupportRequestIndexCondition))
	{
		/* Try to convert operator/function call to index conditions */
		SupportRequestIndexCondition *req =
			(SupportRequestIndexCondition *) supportRequest;

		/*
		 * $expr is always lossy when pushed down to the index.
		 */
		req->lossy = true;

		if (ExprCanBePushedToIndex(req))
		{
			List *exprQuals = PushExprToIndex(req);
			if (exprQuals != NIL)
			{
				responsePointer = (Pointer) exprQuals;
			}
		}
	}

	PG_RETURN_POINTER(responsePointer);
}


static bool
ExprCanBePushedToIndex(SupportRequestIndexCondition *supportRequest)
{
	if (!EnableExprLookupIndexPushdown)
	{
		return false;
	}

	/* A $expr can be pushed to the index iff the index is non-multikey */
	GetMultikeyStatusFunc getMultiKeyStatusFunc = GetMultiKeyStatusByRelAm(
		supportRequest->index->relam);
	if (getMultiKeyStatusFunc == NULL)
	{
		return false;
	}

	if (!IsCompositeOpFamilyOid(
			supportRequest->index->relam,
			supportRequest->index->opfamily[supportRequest->indexcol]))
	{
		return false;
	}

	bool isMultiKeyIndex = false;
	Relation indexRel = index_open(supportRequest->index->indexoid, NoLock);
	isMultiKeyIndex = getMultiKeyStatusFunc(indexRel);
	index_close(indexRel, NoLock);

	return !isMultiKeyIndex;
}


/*
 * Given a binary comparison function, and input qualifiers,
 * Adds supported qualifier functions to the quals in inputQuals
 * if applicable. Otherwise returns inputQuals as is.
 * If the expression is "path" "op" "value" then uses the
 * primaryFunctionOid to get the index operator and adds that.
 * If it's "value" "op" "path" then uses the commutatorOid to generate
 * the OpExpr.
 */
static List *
PushBinaryExpressionQuals(bson_iter_t *outerIter, List *inputQuals,
						  bytea *indexOptions, Expr *documentExpr,
						  Expr *variableSpec,
						  Oid primaryFunctionOid, Oid commutatorOid)
{
	bson_iter_t exprIter;
	if (!BSON_ITER_HOLDS_ARRAY(outerIter) ||
		!bson_iter_recurse(outerIter, &exprIter))
	{
		return inputQuals;
	}

	bson_value_t leftArg = { 0 };
	bson_value_t rightArg = { 0 };
	int argCount = 0;
	while (bson_iter_next(&exprIter))
	{
		if (argCount == 0)
		{
			leftArg = *bson_iter_value(&exprIter);
		}
		else if (argCount == 1)
		{
			rightArg = *bson_iter_value(&exprIter);
		}
		else
		{
			return inputQuals;
		}

		argCount++;
	}

	if (argCount != 2)
	{
		return inputQuals;
	}

	AggregationExpressionData leftExprData = { 0 };
	AggregationExpressionData rightExprData = { 0 };
	ParseAggregationExpressionContext leftContext = { 0 };
	ParseAggregationExpressionContext rightContext = { 0 };
	ParseAggregationExpressionData(&leftExprData, &leftArg,
								   &leftContext);
	ParseAggregationExpressionData(&rightExprData, &rightArg,
								   &rightContext);

	pgbsonelement queryElement = { 0 };
	Oid operatorOid = InvalidOid;
	bool isExpression = false;
	if (leftExprData.kind == AggregationExpressionKind_Path)
	{
		/* Check if expression is $path Op value */
		if (rightExprData.kind == AggregationExpressionKind_Constant ||
			rightExprData.kind == AggregationExpressionKind_Variable)
		{
			queryElement.path = &leftExprData.value.value.v_utf8.str[1];
			queryElement.pathLength = leftExprData.value.value.v_utf8.len - 1;
			queryElement.bsonValue = rightExprData.value;
			operatorOid = primaryFunctionOid;
			isExpression = rightExprData.kind == AggregationExpressionKind_Variable;
		}
	}
	else if (rightExprData.kind == AggregationExpressionKind_Path)
	{
		/* Check if expression is value Op $path */
		if (leftExprData.kind == AggregationExpressionKind_Constant ||
			leftExprData.kind == AggregationExpressionKind_Variable)
		{
			queryElement.path = &rightExprData.value.value.v_utf8.str[1];
			queryElement.pathLength = rightExprData.value.value.v_utf8.len - 1;
			queryElement.bsonValue = leftExprData.value;
			operatorOid = commutatorOid;
			isExpression = leftExprData.kind == AggregationExpressionKind_Variable;
		}
	}

	if (operatorOid == InvalidOid)
	{
		return inputQuals;
	}

	const MongoIndexOperatorInfo *operator =
		GetMongoIndexOperatorInfoByPostgresFuncId(operatorOid);

	if (operator->indexStrategy == BSON_INDEX_STRATEGY_INVALID)
	{
		return inputQuals;
	}

	if (!ValidateIndexForQualifierElement(indexOptions, &queryElement,
										  operator->indexStrategy))
	{
		return inputQuals;
	}

	pgbson *queryBson = PgbsonElementToPgbson(&queryElement);
	Const *queryBsonConst =
		makeConst(BsonTypeId(), -1, InvalidOid, -1, PointerGetDatum(queryBson), false,
				  false);

	Expr *secondArg;
	if (isExpression)
	{
		if (variableSpec == NULL)
		{
			/* Can't push down if it's a variable without a let spec */
			return inputQuals;
		}

		/* The source doc is empty */
		Const *emptyBsonConst =
			makeConst(BsonTypeId(), -1, InvalidOid, -1, PointerGetDatum(
						  PgbsonInitEmpty()), false, false);

		Node *boolIsNullOnEmptyConst = makeBoolConst(true, false);

		/* Generate the expression
		 * document @<operator> bson_expression_get_with_let('{]', '{ filterCondition }', '{ variableSpec }'})
		 */
		secondArg = (Expr *) makeFuncExpr(
			BsonExpressionGetWithLetFunctionOid(), BsonTypeId(),
			list_make4(emptyBsonConst, queryBsonConst, boolIsNullOnEmptyConst,
					   variableSpec),
			InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
		if (IsA(variableSpec, Const))
		{
			secondArg = (Expr *) eval_const_expressions(NULL, (Node *) secondArg);
		}
	}
	else
	{
		secondArg = (Expr *) queryBsonConst;
	}

	List *args = list_make2(documentExpr, secondArg);
	Expr *finalExpression =
		(Expr *) GetOpExprClauseFromIndexOperator(operator, args, indexOptions);
	return lappend(inputQuals, finalExpression);
}


/*
 * Walks an $expr expression document and extracts all valid
 * index expressions that can be extracted from the $expr condition.
 */
static List *
WalkExprIterForSupportedQuals(bson_iter_t *exprIter,
							  bytea *indexOptions, Expr *documentExpr,
							  Expr *variableSpec, List *inputQuals)
{
	while (bson_iter_next(exprIter))
	{
		const char *operatorKey = bson_iter_key(exprIter);

		/* Traverse nested $ands to see if we can get expr conditions */
		if (strcmp(operatorKey, "$and") == 0)
		{
			bson_iter_t andIter;
			if (!BSON_ITER_HOLDS_ARRAY(exprIter) ||
				!bson_iter_recurse(exprIter, &andIter))
			{
				continue;
			}

			while (bson_iter_next(&andIter))
			{
				bson_iter_t qualIter;
				if (!BSON_ITER_HOLDS_DOCUMENT(&andIter) ||
					!bson_iter_recurse(&andIter, &qualIter))
				{
					continue;
				}

				inputQuals =
					WalkExprIterForSupportedQuals(&qualIter, indexOptions, documentExpr,
												  variableSpec, inputQuals);
			}
		}
		else if (strcmp(operatorKey, "$eq") == 0)
		{
			inputQuals = PushBinaryExpressionQuals(exprIter, inputQuals, indexOptions,
												   documentExpr, variableSpec,
												   BsonEqualMatchIndexFunctionId(),
												   BsonEqualMatchIndexFunctionId());
		}
		else if (strcmp(operatorKey, "$gt") == 0)
		{
			inputQuals = PushBinaryExpressionQuals(exprIter, inputQuals, indexOptions,
												   documentExpr, variableSpec,
												   BsonGreaterThanMatchIndexFunctionId(),
												   BsonLessThanEqualMatchIndexFunctionId());
		}
		else if (strcmp(operatorKey, "$gte") == 0)
		{
			inputQuals = PushBinaryExpressionQuals(exprIter, inputQuals, indexOptions,
												   documentExpr, variableSpec,
												   BsonGreaterThanEqualMatchIndexFunctionId(),
												   BsonLessThanMatchIndexFunctionId());
		}
		else if (strcmp(operatorKey, "$lt") == 0)
		{
			inputQuals = PushBinaryExpressionQuals(exprIter, inputQuals, indexOptions,
												   documentExpr, variableSpec,
												   BsonLessThanMatchIndexFunctionId(),
												   BsonGreaterThanEqualMatchIndexFunctionId());
		}
		else if (strcmp(operatorKey, "$lte") == 0)
		{
			inputQuals = PushBinaryExpressionQuals(exprIter, inputQuals, indexOptions,
												   documentExpr, variableSpec,
												   BsonLessThanEqualMatchIndexFunctionId(),
												   BsonGreaterThanMatchIndexFunctionId());
		}
	}

	return inputQuals;
}


/*
 * Walks an $expr function filter and returns a list of expressions
 * that can be pushed to an index based off of the expr condition.
 */
static List *
PushExprToIndex(SupportRequestIndexCondition *supportRequest)
{
	if (supportRequest->funcid != BsonExprFunctionId() &&
		supportRequest->funcid != BsonExprWithLetFunctionId())
	{
		return NULL;
	}

	if (!IsA(supportRequest->node, FuncExpr) ||
		!supportRequest->index->opclassoptions)
	{
		return NULL;
	}

	FuncExpr *exprFunc = (FuncExpr *) supportRequest->node;
	if (list_length(exprFunc->args) > 3)
	{
		return NULL;
	}

	Expr *documentExpr = linitial(exprFunc->args);

	/* Now get the expression arg */
	Expr *exprSpec = lsecond(exprFunc->args);
	Expr *variableSpec = list_length(exprFunc->args) == 2 ? NULL : lthird(exprFunc->args);

	if (!IsA(exprSpec, Const))
	{
		return NULL;
	}

	Const *exprConst = (Const *) exprSpec;
	if (exprConst->constisnull)
	{
		return NULL;
	}

	pgbson *exprBson = DatumGetPgBson(exprConst->constvalue);

	pgbsonelement exprElement = { 0 };
	bson_iter_t exprIter;
	PgbsonToSinglePgbsonElement(exprBson, &exprElement);
	BsonValueInitIterator(&exprElement.bsonValue, &exprIter);
	bytea *indexOptions = supportRequest->index->opclassoptions[supportRequest->indexcol];

	List *supportedQuals = NIL;
	return WalkExprIterForSupportedQuals(&exprIter, indexOptions, documentExpr,
										 variableSpec, supportedQuals);
}
