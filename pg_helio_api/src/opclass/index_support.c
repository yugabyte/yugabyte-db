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
#include <catalog/pg_type.h>
#include <nodes/pathnodes.h>
#include <nodes/supportnodes.h>
#include <nodes/makefuncs.h>
#include <catalog/pg_am.h>
#include <optimizer/pathnode.h>

#include "query/query_operator.h"
#include "planner/mongo_query_operator.h"
#include "opclass/helio_index_support.h"
#include "opclass/helio_gin_index_mgmt.h"
#include "opclass/helio_bson_text_gin.h"
#include "metadata/metadata_cache.h"
#include "utils/mongo_errors.h"
#include "vector/vector_utilities.h"
#include "utils/version_utils.h"
#include "query/helio_bson_compare.h"

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static Expr * HandleSupportRequestCondition(SupportRequestIndexCondition *req);
static Path * ReplaceFunctionOperatorsInPlanPath(Path *path,
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

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */
PG_FUNCTION_INFO_V1(dollar_support);

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
ReplaceExtensionFunctionOperatorsInPaths(List *pathsList,
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
		path = ReplaceFunctionOperatorsInPlanPath(path, context);
	}
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
	 *  https://msdata.visualstudio.com/CosmosDB/_workitems/edit/1769068
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
					context->queryDataForVectorSearch.SearchParamBson = PointerGetDatum(
						bsonConst->constvalue);
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
	bool isValidCandidateForRange = true;
	pgbsonelement minElementCandidate = { 0 };
	bool isMinInclusive = false;
	pgbsonelement maxElementCandidate = { 0 };
	bool isMaxInclusive = false;
	foreach(indexPathCell, indexClauses)
	{
		IndexClause *iclause = (IndexClause *) lfirst(indexPathCell);
		RestrictInfo *rinfo = iclause->rinfo;

		if (!isValidCandidateForRange)
		{
			break;
		}

		if (!IsA(rinfo->clause, OpExpr))
		{
			continue;
		}

		OpExpr *opExpr = (OpExpr *) rinfo->clause;
		const MongoIndexOperatorInfo *operator =
			GetMongoIndexOperatorByPostgresOperatorId(opExpr->opno);
		bool isComparisonInvalidIgnore = false;
		switch (operator->indexStrategy)
		{
			case BSON_INDEX_STRATEGY_DOLLAR_GREATER:
			case BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL:
			{
				Const *argsConst = lsecond(opExpr->args);
				pgbson *secondArg = DatumGetPgBson(argsConst->constvalue);
				pgbsonelement argElement;
				PgbsonToSinglePgbsonElement(secondArg, &argElement);

				if (minElementCandidate.pathLength == 0)
				{
					minElementCandidate = argElement;
					isMinInclusive = operator->indexStrategy ==
									 BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL;
				}
				else if (minElementCandidate.pathLength != argElement.pathLength ||
						 strncmp(minElementCandidate.path, argElement.path,
								 argElement.pathLength) != 0)
				{
					isValidCandidateForRange = false;
					break;
				}
				else if (CompareBsonValueAndType(
							 &minElementCandidate.bsonValue, &argElement.bsonValue,
							 &isComparisonInvalidIgnore) < 0)
				{
					minElementCandidate = argElement;
					isMinInclusive = operator->indexStrategy ==
									 BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL;
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

				if (maxElementCandidate.pathLength == 0)
				{
					maxElementCandidate = argElement;
					isMaxInclusive = operator->indexStrategy ==
									 BSON_INDEX_STRATEGY_DOLLAR_LESS_EQUAL;
				}
				else if (maxElementCandidate.pathLength != argElement.pathLength ||
						 strncmp(maxElementCandidate.path, argElement.path,
								 argElement.pathLength) != 0)
				{
					isValidCandidateForRange = false;
					break;
				}
				else if (CompareBsonValueAndType(
							 &maxElementCandidate.bsonValue, &argElement.bsonValue,
							 &isComparisonInvalidIgnore) > 0)
				{
					maxElementCandidate = argElement;
					isMaxInclusive = operator->indexStrategy ==
									 BSON_INDEX_STRATEGY_DOLLAR_LESS_EQUAL;
				}

				break;
			}

			default:
			{
				isValidCandidateForRange = false;
				break;
			}
		}
	}

	if (!isValidCandidateForRange)
	{
		return indexClauses;
	}

	if (minElementCandidate.bsonValue.value_type == BSON_TYPE_EOD ||
		maxElementCandidate.bsonValue.value_type == BSON_TYPE_EOD)
	{
		return indexClauses;
	}

	if (minElementCandidate.pathLength != maxElementCandidate.pathLength ||
		strncmp(minElementCandidate.path, maxElementCandidate.path,
				minElementCandidate.pathLength) != 0)
	{
		return indexClauses;
	}

	IndexClause *iclause = (IndexClause *) linitial(indexClauses);
	OpExpr *expr = (OpExpr *) iclause->rinfo->clause;

	pgbson_writer clauseWriter;
	pgbson_writer childWriter;
	PgbsonWriterInit(&clauseWriter);
	PgbsonWriterStartDocument(&clauseWriter, minElementCandidate.path,
							  minElementCandidate.pathLength,
							  &childWriter);

	PgbsonWriterAppendValue(&childWriter, "min", 3, &minElementCandidate.bsonValue);
	PgbsonWriterAppendValue(&childWriter, "max", 3, &maxElementCandidate.bsonValue);
	PgbsonWriterAppendBool(&childWriter, "minInclusive", 12, isMinInclusive);
	PgbsonWriterAppendBool(&childWriter, "maxInclusive", 12, isMaxInclusive);
	PgbsonWriterEndDocument(&clauseWriter, &childWriter);


	Const *bsonConst = makeConst(BsonTypeId(), -1, InvalidOid, -1, PointerGetDatum(
									 PgbsonWriterGetPgbson(&clauseWriter)), false, false);

	OpExpr *opExpr = (OpExpr *) make_opclause(BsonRangeMatchOperatorOid(), BOOLOID, false,
											  linitial(expr->args),
											  (Expr *) bsonConst, InvalidOid, InvalidOid);
	opExpr->opfuncid = BsonRangeMatchFunctionId();
	iclause->rinfo->clause = (Expr *) opExpr;
	iclause->indexquals = list_make1(iclause->rinfo);

	return list_make1(iclause);
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
ReplaceFunctionOperatorsInPlanPath(Path *path, ReplaceExtensionFunctionContext *context)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	if (IsA(path, BitmapOrPath))
	{
		BitmapOrPath *orPath = (BitmapOrPath *) path;
		ReplaceExtensionFunctionOperatorsInPaths(orPath->bitmapquals, context);
	}
	else if (IsA(path, BitmapAndPath))
	{
		BitmapAndPath *andPath = (BitmapAndPath *) path;
		ReplaceExtensionFunctionOperatorsInPaths(andPath->bitmapquals, context);
		path = OptimizeBitmapQualsForBitmapAnd(andPath, context);
	}
	else if (IsA(path, BitmapHeapPath))
	{
		BitmapHeapPath *heapPath = (BitmapHeapPath *) path;
		heapPath->bitmapqual = ReplaceFunctionOperatorsInPlanPath(heapPath->bitmapqual,
																  context);
	}
	else if (IsA(path, IndexPath))
	{
		IndexPath *indexPath = (IndexPath *) path;
		bool hasVectorSearch = indexPath->indexinfo->relam ==
							   PgVectorIvfFlatIndexAmId() ||
							   indexPath->indexinfo->relam == PgVectorHNSWIndexAmId();
		context->hasVectorSearchQuery = context->hasVectorSearchQuery ||
										hasVectorSearch;

		if (hasVectorSearch)
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

			context->queryDataForVectorSearch.VectorPathName = PointerGetDatum(
				vectorPathConst->constvalue);
			context->queryDataForVectorSearch.QueryVector = PointerGetDatum(
				vectorConst->constvalue);
			context->queryDataForVectorSearch.SimilaritySearchOpOid = sortExpr->opno;
			context->queryDataForVectorSearch.VectorAccessMethodOid =
				indexPath->indexinfo->relam;

			/*
			 * For vector search, we also need to extract the search parameter from the wrap function.
			 * ApiCatalogSchemaName.bson_search_param(document, '{ "nProbes": 4 }'::ApiCatalogSchemaName.bson)
			 */
			ExtractAndSetSearchParamterFromWrapFunction(indexPath, context);
		}

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
					ereport(ERROR, (errcode(MongoBadValue),
									errmsg("Too many text expressions")));
				}

				ReplaceExtensionFunctionContext childContext = {
					{ 0 }, { 0 }, false, false, context->inputData
				};
				childContext.indexOptionsForText.indexOptions = options;
				rinfo->clause = ProcessRestrictionInfoAndRewriteFuncExpr(rinfo->clause,
																		 &childContext);
				context->indexOptionsForText = childContext.indexOptionsForText;
			}
			else
			{
				ReplaceExtensionFunctionContext childContext = {
					{ 0 }, { 0 }, false, false, context->inputData
				};
				rinfo->clause = ProcessRestrictionInfoAndRewriteFuncExpr(rinfo->clause,
																		 &childContext);
			}
		}

		if (context->inputData.isShardQuery)
		{
			indexPath->indexclauses = OptimizeIndexExpressionsForRange(
				indexPath->indexclauses);
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
