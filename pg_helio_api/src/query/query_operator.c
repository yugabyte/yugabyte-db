/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/planner/query_operator.c
 *
 * Implementation of BSON query to operator conversion.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <catalog/namespace.h>
#include <catalog/pg_collation.h>
#include <catalog/pg_operator.h>
#include <executor/executor.h>
#include <optimizer/optimizer.h>
#include <nodes/makefuncs.h>
#include <nodes/nodes.h>
#include <nodes/nodeFuncs.h>
#include <utils/builtins.h>
#include <utils/typcache.h>
#include <utils/lsyscache.h>
#include <utils/syscache.h>
#include <utils/timestamp.h>
#include <utils/array.h>
#include <parser/parse_coerce.h>
#include <parser/parsetree.h>
#include <parser/parse_clause.h>
#include <catalog/pg_type.h>
#include <funcapi.h>
#include <lib/stringinfo.h>
#include <metadata/metadata_cache.h>
#include <math.h>
#include <nodes/supportnodes.h>

#include "io/helio_bson_core.h"
#include "query/helio_bson_compare.h"
#include "aggregation/bson_query.h"
#include "types/decimal128.h"
#include "utils/mongo_errors.h"
#include "commands/defrem.h"
#include "geospatial/bson_geospatial_common.h"
#include "geospatial/bson_geospatial_geonear.h"
#include "geospatial/bson_geospatial_shape_operators.h"
#include "metadata/collection.h"
#include "planner/helio_planner.h"
#include "query/query_operator.h"
#include "sharding/sharding.h"
#include "utils/rel.h"
#include "opclass/helio_bson_text_gin.h"
#include "utils/feature_counter.h"
#include "vector/vector_common.h"
#include "vector/vector_utilities.h"
#include "types/pcre_regex.h"
#include "query/bson_dollar_operators.h"
#include "commands/commands_common.h"
#include "utils/version_utils.h"
#include "collation/collation.h"


/*
 * ReplaceBsonQueryOperatorsContext is passed down while looking for
 * <document> @@ <query> expressions in ReplaceBsonQueryOperatorsMutator.
 *
 * We include the query to interpret Vars using currentQuery->rtable,
 * and the parameter list in case the query is defined as a parameter.
 */
typedef struct ReplaceBsonQueryOperatorsContext
{
	/* current query in which we are looking for an OpExpr */
	Query *currentQuery;

	/* parameter values of the current execution */
	ParamListInfo boundParams;

	/* List of sort clauses, if any query operator adds them
	 * e.g. $near, $nearSphere etc, will be NULL for most of
	 * the query operators.
	 *
	 * Please note that the `ressortgroupref` is needed to be updated
	 * based on the overall query structure later
	 */
	List *sortClauses;

	/* List of Target entries for these sort clauses
	 *
	 * Please note that the `resno` is needed to be updated
	 * based on the overall query structure later
	 */
	List *targetEntries;
} ReplaceBsonQueryOperatorsContext;

/* Context passed as an argument to CreateIdFilterForQuery */
typedef struct IdFilterWalkerContext
{
	/* The id filter qualifiers extracted from the query operator quals */
	List *idQuals;

	/* The index into the RTE where the collection is. */
	Index collectionVarno;

	/* Whether the _id filed is of a type where collation rules apply, e.g., UTF8 */
	bool isCollationAware;
} IdFilterWalkerContext;

extern bool EnableCollation;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static Const * MakeBsonConst(pgbson *pgbson);
static Node * ReplaceBsonQueryOperatorsMutator(Node *node,
											   ReplaceBsonQueryOperatorsContext *context);
static Expr * ExpandBsonQueryOperator(OpExpr *queryOpExpr, Node *queryNode,
									  Query *currentQuery, ParamListInfo boundParams,
									  List **targetEntries, List **sortClauses);
static Expr * CreateBoolExprFromLogicalExpression(bson_iter_t *queryDocIterator,
												  BsonQueryOperatorContext *c,
												  const char *traversedPath);
static List * CreateQualsFromLogicalExpressionArrayIterator(bson_iter_t *arrayIterator,
															BsonQueryOperatorContext *c,
															const char *traversedPath);
static Expr * CreateOpExprFromComparisonExpression(bson_iter_t *queryDocIterator,
												   BsonQueryOperatorContext *context,
												   const char *traversedPath,
												   const char *currentKey);
static Expr * CreateOpExprFromOperatorDocIterator(const char *path,
												  bson_iter_t *operatorDocIterator,
												  BsonQueryOperatorContext *context);
static Expr * CreateOpExprFromOperatorDocIteratorCore(bson_iter_t *operatorDocIterator,
													  BsonQueryOperatorContext *context,
													  const char *path,
													  bool *regexFound,
													  bson_value_t **options);
static Expr * CreateFuncExprForQueryOperator(BsonQueryOperatorContext *context, const
											 char *path,
											 const MongoQueryOperator *operator,
											 const bson_value_t *value);
static Const * CreateConstFromBsonValue(const char *path, const bson_value_t *value, const
										char *collationString);
static Expr * CreateExprForDollarAll(const char *path,
									 bson_iter_t *operatorDocIterator,
									 BsonQueryOperatorContext *context,
									 const MongoQueryOperator *operator);
static MongoCollection * GetCollectionReferencedByDocumentVar(Expr *documentExpr,
															  Query *currentQuery,
															  Index *collectionVarno,
															  ParamListInfo boundParams);
static MongoCollection * GetCollectionForRTE(RangeTblEntry *rte, ParamListInfo
											 boundParams);
static Expr * CreateShardKeyValueFilter(int collectionVarNo, Const *valueConst);
static Expr * CreateExprForDollarRegex(bson_iter_t *currIter, bson_value_t **options,
									   BsonQueryOperatorContext *context,
									   const MongoQueryOperator *operator,
									   const char *path);
static Expr * CreateFuncExprForRegexOperator(const bson_value_t *options,
											 const bson_value_t *regexBsonValue,
											 BsonQueryOperatorContext *context,
											 const MongoQueryOperator *operator,
											 const char *path);
static Expr * CreateExprForDollarMod(bson_iter_t *currIter,
									 BsonQueryOperatorContext *context,
									 const MongoQueryOperator *operator,
									 const char *path);
static Expr * CreateExprForBitwiseQueryOperators(bson_iter_t *operatorDocIterator,
												 BsonQueryOperatorContext *context,
												 const MongoQueryOperator *operator,
												 const char *path);
static bool SortAndWriteInt32BsonTypeArray(const bson_value_t *array,
										   pgbson_writer *writer,
										   const char *opName);
static List * CreateQualsFromQueryDocIteratorInternal(bson_iter_t *queryDocIterator,
													  BsonQueryOperatorContext *context,
													  const char *traversedPath);
static Expr * CreateQualForBsonValueExpressionCore(const bson_value_t *expression,
												   BsonQueryOperatorContext *context,
												   const char *traversedPath,
												   const char *basePath);
static Expr * TryProcessOrIntoDollarIn(BsonQueryOperatorContext *context,
									   List *orQuals);
static Expr * TryOptimizeDollarOrExpr(BsonQueryOperatorContext *context,
									  List *orQuals);
static Expr * ParseBsonValueForNearAndCreateOpExpr(bson_iter_t *operatorDocIterator,
												   BsonQueryOperatorContext *context,
												   const char *path, const
												   char *mongoOperatorName);
static void ValidateOptionsArgument(const bson_value_t *argBsonValue);
static void ValidateRegexArgument(const bson_value_t *argBsonValue);
static void EnsureValidTypeNameForDollarType(const char *typeName);
static void EnsureValidTypeCodeForDollarType(int64_t typeCode);

static Expr * WithIndexSupportExpression(Expr *docExpr, Expr *geoOperatorExpr,
										 const char *path, bool isSpherical);
static Expr * TryOptimizeNotInnerExpr(Expr *innerExpr, BsonQueryOperatorContext *context);

/* Return true if double value can be represented as fixed integer
 * e.g., 10.023 -> this number can not be represented as fixed integer so return false
 *       10.000 -> this number can be represented as fixed integer (10) so return true
 */
static inline bool
IsDoubleAFixedInteger(double value)
{
	return floor(value) == value;
}


static inline void
pg_attribute_noreturn()
ThrowInvalidRegexOptions(char c)
{
	ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION51108), errmsg(
						"invalid flag in regex options %c", c),
					errhint("invalid flag in regex options %c", c)));
}

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */
PG_FUNCTION_INFO_V1(bson_query_match);
PG_FUNCTION_INFO_V1(bson_true_match);
PG_FUNCTION_INFO_V1(query_match_support);


/*
 * bson_query_match is a lazy, inefficient implementation of the @@
 * operator, used only when we cannot replace it in the planner hook.
 */
Datum
bson_query_match(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *query = PG_GETARG_PGBSON(1);

	Const *documentConst = MakeBsonConst(document);
	Const *queryConst = MakeBsonConst(query);

	OpExpr *queryExpr = makeNode(OpExpr);
	queryExpr->opno = BsonQueryOperatorId();
	queryExpr->opfuncid = BsonQueryMatchFunctionId();
	queryExpr->inputcollid = InvalidOid;
	queryExpr->opresulttype = BsonTypeId();
	queryExpr->args = list_make2(documentConst, queryConst);
	queryExpr->location = -1;

	ereport(NOTICE, (errmsg("using bson_query_match implementation")));

	/* expand the @@ operator into regular BSON operators */
	ReplaceBsonQueryOperatorsContext context;
	memset(&context, 0, sizeof(context));

	Node *quals = ReplaceBsonQueryOperatorsMutator((Node *) queryExpr, &context);

	/* evaluate the constant expressions */
	Node *evaluatedExpr = eval_const_expressions(NULL, (Node *) quals);
	if (!IsA(evaluatedExpr, Const))
	{
		ereport(ERROR, (errmsg("failed to evaluated expression to constant")));
	}

	/* obtain the boolean result */
	Const *evaluatedConst = (Const *) evaluatedExpr;

	PG_RETURN_DATUM(evaluatedConst->constvalue);
}


/*
 * bson_true_match is a dummy placeholder function used to hold
 * a pointer to the parameterized value in the planner hook for the @@
 * operator. This is needed for citus distribution to have a pointer from
 * the query to the parameterized value to ensure it's distributed to the workers
 * properly. We make this a single function that takes 1 param and returns true
 * so that it's evaluated once per query context.
 */
Datum
bson_true_match(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(true);
}


/*
 * Is a support function for the query match function to expand it into the individual clauses.
 */
Datum
query_match_support(PG_FUNCTION_ARGS)
{
	Node *supportRequest = (Node *) PG_GETARG_POINTER(0);
	if (IsA(supportRequest, SupportRequestSimplify))
	{
		/* Try to convert operator/function call to index conditions */
		SupportRequestSimplify *req =
			(SupportRequestSimplify *) supportRequest;
		FuncExpr *funcExpr = req->fcall;

		if (funcExpr->funcid == BsonQueryMatchFunctionId() &&
			list_length(funcExpr->args) == 2)
		{
			Expr *firstArg = linitial(funcExpr->args);
			Expr *secondArg = lsecond(funcExpr->args);

			if (!IsA(secondArg, Const))
			{
				PG_RETURN_POINTER(NULL);
			}

			Const *constValue = (Const *) secondArg;
			if (constValue->constisnull)
			{
				PG_RETURN_POINTER(NULL);
			}

			pgbson *queryDocument = (pgbson *) DatumGetPointer(constValue->constvalue);

			/* open the Mongo query document */
			bson_iter_t queryDocIter;
			PgbsonInitIterator(queryDocument, &queryDocIter);

			BsonQueryOperatorContext context = { 0 };
			context.documentExpr = firstArg;
			context.inputType = MongoQueryOperatorInputType_Bson;
			context.simplifyOperators = true;
			context.coerceOperatorExprIfApplicable = true;
			context.requiredFilterPathNameHashSet = NULL;
			context.variableContext = NULL;

			/* convert the Mongo query to a list of Postgres quals */
			List *quals = CreateQualsFromQueryDocIterator(&queryDocIter, &context);
			UpdateQueryOperatorContextSortList(req->root->parse,
											   context.targetEntries,
											   context.sortClauses);

			if (quals != NIL)
			{
				PG_RETURN_POINTER(make_ands_explicit(quals));
			}
		}
	}

	PG_RETURN_POINTER(NULL);
}


/*
 * MakeBsonConst creates a Const expression for a given bson.
 */
static Const *
MakeBsonConst(pgbson *pgbson)
{
	Const *bsonConst = makeNode(Const);
	bsonConst->consttype = BsonTypeId();
	bsonConst->consttypmod = -1;
	bsonConst->constlen = -1;
	bsonConst->constvalue = PointerGetDatum(pgbson);
	bsonConst->constbyval = false;
	bsonConst->constisnull = false;
	bsonConst->location = -1;

	return bsonConst;
}


/*
 * MakeSimpleDocumentVar returns a Var node for the document column of a
 * Mongo data table assuming that returned node will be the only variable of
 * the query in which caller will use the Var, and that variable is not a
 * subquery variable.
 */
Var *
MakeSimpleDocumentVar(void)
{
	/* the only Var in the Query context (if any) */
	Index varno = 1;

	/* not lives in a subquery */
	Index varlevelsup = 0;

	return makeVar(varno, MONGO_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER,
				   BsonTypeId(), MONGO_DATA_TABLE_DOCUMENT_VAR_TYPMOD,
				   MONGO_DATA_TABLE_DOCUMENT_VAR_COLLATION, varlevelsup);
}


/*
 * ReplaceBsonQueryOperators replaces all occurrences of <bson> @@ <bson>
 * with an expanded expression that uses low-level BSON operators.
 */
Node *
ReplaceBsonQueryOperators(Query *node, ParamListInfo boundParams)
{
	ReplaceBsonQueryOperatorsContext context;
	memset(&context, 0, sizeof(context));
	context.currentQuery = node;
	context.boundParams = boundParams;

	return ReplaceBsonQueryOperatorsMutator((Node *) node, &context);
}


/*
 * Creates a parsed query AST for a given document containing
 * a query expression. The input VAR is placed as an internal typed
 * variable.
 * e.g. { "$or" [ { "$gte": 3 }, { "$lte": 6}]}
 *   -> VAR @>= '{ "" : 3 }' OR VAR @<= '{ "": 6 }'
 *
 * The collation is passed in the context and then is embedded in the
 * operator for final comparisons.
 */
Expr *
CreateQualForBsonValueExpression(const bson_value_t *expression, const
								 char *collationString)
{
	if (expression->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
							"expression should be a document")));
	}

	Var *var = makeVar(1, 1, INTERNALOID, -1, DEFAULT_COLLATION_OID, 0);
	BsonQueryOperatorContext context = { 0 };
	context.documentExpr = (Expr *) var;
	context.inputType = MongoQueryOperatorInputType_BsonValue;
	context.simplifyOperators = true;
	context.coerceOperatorExprIfApplicable = false;
	context.requiredFilterPathNameHashSet = NULL;
	context.variableContext = NULL;
	context.collationString = collationString;

	const char *traversedPath = NULL;
	const char *basePath = "";
	return CreateQualForBsonValueExpressionCore(expression, &context,
												traversedPath, basePath);
}


/*
 * Creates a parsed query AST for a given array containing
 * a list of query expressions. The input VAR is placed as an internal typed
 * variable.
 * e.g. [ 1, 2]
 *   -> VAR @= '{ "" : 1 }' AND VAR @= '{ "": 2 }'
 */
Expr *
CreateQualForBsonValueArrayExpression(const bson_value_t *expression)
{
	if (expression->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
							"expression should be an array")));
	}

	Var *var = makeVar(1, 1, INTERNALOID, -1, DEFAULT_COLLATION_OID, 0);
	BsonQueryOperatorContext context = { 0 };
	context.documentExpr = (Expr *) var;
	context.inputType = MongoQueryOperatorInputType_BsonValue;
	context.simplifyOperators = true;
	context.coerceOperatorExprIfApplicable = false;
	context.requiredFilterPathNameHashSet = NULL;
	context.variableContext = NULL;

	const char *traversedPath = NULL;
	const char *basePath = "";

	bson_iter_t queryDocIterator;
	bson_iter_init_from_data(&queryDocIterator,
							 expression->value.v_doc.data,
							 expression->value.v_doc.data_len);
	List *quals = NIL;
	while (bson_iter_next(&queryDocIterator))
	{
		const bson_value_t *value = bson_iter_value(&queryDocIterator);
		if (BSON_ITER_HOLDS_DOCUMENT(&queryDocIterator))
		{
			quals = lappend(quals,
							CreateQualForBsonValueExpressionCore(value,
																 &context,
																 traversedPath,
																 basePath));
		}
		else
		{
			const MongoQueryOperator *eqOperator =
				GetMongoQueryOperatorByQueryOperatorType(
					QUERY_OPERATOR_EQ, context.inputType);

			/* <path> : <value>, convert to = expression  */
			quals = lappend(quals,
							CreateFuncExprForQueryOperator(&context, "",
														   eqOperator, value));
		}
	}

	return make_ands_explicit(quals);
}


/*
 * Creates a BsonValue based Expression for a top level query (i.e. one
 * typically supplied by an @@ operator).
 */
List *
CreateQualsForBsonValueTopLevelQuery(const pgbson *query)
{
	Var *var = makeVar(1, 1, INTERNALOID, -1, DEFAULT_COLLATION_OID, 0);
	BsonQueryOperatorContext context = { 0 };
	context.documentExpr = (Expr *) var;
	context.inputType = MongoQueryOperatorInputType_BsonValue;
	context.simplifyOperators = true;
	context.coerceOperatorExprIfApplicable = false;
	context.requiredFilterPathNameHashSet = NULL;
	context.variableContext = NULL;

	bson_iter_t queryDocIterator;
	PgbsonInitIterator(query, &queryDocIterator);
	return CreateQualsFromQueryDocIterator(&queryDocIterator, &context);
}


/*
 * Creates a parsed query AST for a given document containing
 * a query expression. The input is a documentExpr provided
 * as an input as a bson type.
 * variable.
 * e.g. { "$or" [ { "$gte": 3 }, { "$lte": 6}]}
 * Given a base queryPath of "a.b"
 *   -> documentExpr @>= '{ "a.b" : 3 }' OR documentExpr @<= '{ "a.b": 6 }'
 */
Expr *
CreateQualForBsonExpression(const bson_value_t *expression, const
							char *queryPath)
{
	if (expression->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
							"expression should be a document")));
	}

	BsonQueryOperatorContext context = { 0 };
	Var *var = makeVar(1, 1, BsonTypeId(), -1, DEFAULT_COLLATION_OID, 0);
	context.documentExpr = (Expr *) var;
	context.inputType = MongoQueryOperatorInputType_Bson;
	context.simplifyOperators = true;
	context.coerceOperatorExprIfApplicable = false;
	context.requiredFilterPathNameHashSet = NULL;
	context.variableContext = NULL;

	const char *traversedPath = queryPath;
	const char *basePath = queryPath;
	return CreateQualForBsonValueExpressionCore(expression, &context, traversedPath,
												basePath);
}


/*
 * Creates a parsed query AST for a given document containing
 * a query expression off the specified base path.
 * variable.
 * e.g. { "$or" [ { "$gte": 3 }, { "$lte": 6}]}
 *   -> documentExpr @>= '{ "" : 3 }' OR documentExpr @<= '{ "": 6 }'
 */
static Expr *
CreateQualForBsonValueExpressionCore(const bson_value_t *expression,
									 BsonQueryOperatorContext *context,
									 const char *traversedPath,
									 const char *basePath)
{
	bson_iter_t queryDocIterator;
	bson_iter_init_from_data(&queryDocIterator,
							 expression->value.v_doc.data,
							 expression->value.v_doc.data_len);

	/* This is a variant of CreateQualsFromQueryDocIterator that
	 * allows for array based qualifiers (e.g. $gte: 2) without a path
	 * associated with it. We track top level operators and create a dummy
	 * value filter
	 * Additionally, it adds a final qual for array/object type filters if the input
	 * filter has a path or top level operators ($and/$or) since array filters
	 * require this.
	 */
	List *quals = NIL;
	bool addObjectArrayFilter = false;
	bool regexFound = false;
	bson_value_t *options = NULL;
	while (bson_iter_next(&queryDocIterator))
	{
		/* field or logical operator */
		const char *path = bson_iter_key(&queryDocIterator);
		Expr *qual;

		if (path[0] == '$')
		{
			/* in this fork there's 2 possibilities:
			 * There's a logical operator at the root
			 * e.g. { "$eq": 5 } or { "$and": [ ... ]}
			 * This is different from regular queries where the top level operator
			 * has to be a path.
			 * Consequently, we do a first pass check if it matches any operator,
			 * and pass that through.
			 * $and/$or/$nor is handled explicitly.
			 * $eq etc are handled by the default case.
			 */
			const MongoQueryOperator *operator;
			operator = GetMongoQueryOperatorByMongoOpName(path, context->inputType);

			switch (operator->operatorType)
			{
				/* Handle logical operators that are found the same as query expressions */
				case QUERY_OPERATOR_AND:
				case QUERY_OPERATOR_OR:
				case QUERY_OPERATOR_NOR:
				case QUERY_OPERATOR_EXPR:
				case QUERY_OPERATOR_TEXT:
				case QUERY_OPERATOR_ALWAYS_FALSE:
				case QUERY_OPERATOR_ALWAYS_TRUE:
				{
					regexFound = false;
					addObjectArrayFilter = true;
					qual = CreateBoolExprFromLogicalExpression(&queryDocIterator,
															   context,
															   traversedPath);
					break;
				}

				default:
				{
					/* Otherwise, it's a valid operator, create an OpExpr for the expression */
					qual = CreateOpExprFromOperatorDocIteratorCore(&queryDocIterator,
																   context, basePath,
																   &regexFound, &options);
					break;
				}
			}
		}
		else
		{
			/* all other paths are comparisons */
			addObjectArrayFilter = true;
			regexFound = false;
			qual = CreateOpExprFromComparisonExpression(&queryDocIterator, context,
														traversedPath,
														path);
		}

		if (qual != NULL)
		{
			quals = lappend(quals, qual);
		}
	}

	if (addObjectArrayFilter &&
		context->inputType == MongoQueryOperatorInputType_BsonValue)
	{
		/*
		 * For some expressions like { "b": 2 } or $and/$or in an expression context, we need to only consider
		 * values that are objects/arrays. We add that as an explicit filter here
		 */
		const MongoQueryOperator *typeOperator =
			GetMongoQueryOperatorByQueryOperatorType(QUERY_OPERATOR_TYPE,
													 context->inputType);
		bson_value_t dollarTypeValue;
		dollarTypeValue.value_type = BSON_TYPE_INT32;
		dollarTypeValue.value.v_int32 = BSON_TYPE_DOCUMENT;
		Expr *isObjectQual = CreateFuncExprForQueryOperator(
			context,
			basePath, typeOperator,
			&dollarTypeValue);

		dollarTypeValue.value.v_int32 = BSON_TYPE_ARRAY;
		Expr *isArrayQual = CreateFuncExprForQueryOperator(context,
														   basePath, typeOperator,
														   &dollarTypeValue);

		BoolExpr *logicalExpr = makeNode(BoolExpr);
		logicalExpr->boolop = OR_EXPR;
		logicalExpr->args = list_make2(isObjectQual, isArrayQual);
		logicalExpr->location = -1;

		quals = lappend(quals, logicalExpr);
	}

	return make_ands_explicit(quals);
}


/*
 * ReplaceBsonQueryOperatorsMutator is a mutator that replaces all occurrences
 * of <bson> @@ <bson> with an expanded expression that uses low-level BSON
 * operators.
 */
static Node *
ReplaceBsonQueryOperatorsMutator(Node *node, ReplaceBsonQueryOperatorsContext *context)
{
	if (node == NULL)
	{
		return NULL;
	}

	if (IsA(node, OpExpr))
	{
		OpExpr *opExpr = (OpExpr *) node;

		if (opExpr->opno == BsonQueryOperatorId())
		{
			/* operator always has 2 arguments */
			Assert(list_length(opExpr->args) == 2);

			Node *queryNode = lsecond(opExpr->args);
			queryNode = EvaluateBoundParameters(queryNode, context->boundParams);

			/*
			 * ExpandBsonQueryOperator adds shard_key_value filters based on the query
			 * for sharded collections. Below, we add shard_key_value = 0 filters for
			 * non-sharded collections in FROM.
			 */
			if (IsA(queryNode, Const))
			{
				Node *expandedExpr =
					(Node *) ExpandBsonQueryOperator(opExpr,
													 queryNode,
													 context->currentQuery,
													 context->boundParams,
													 &(context->targetEntries),
													 &(context->sortClauses));

				return expandedExpr;
			}
		}

		return node;
	}
	else if (IsA(node, Query))
	{
		Query *prevQuery = context->currentQuery;
		List *prevSortClauses = context->sortClauses;
		List *prevTargetEntries = context->targetEntries;
		Query *currentQuery = (Query *) node;
		context->sortClauses = NIL;
		context->targetEntries = NIL;

		/* descending into (sub)query */
		context->currentQuery = currentQuery;

		/* also descend into subqueries */
		Query *result = query_tree_mutator(currentQuery, ReplaceBsonQueryOperatorsMutator,
										   context, 0);

		UpdateQueryOperatorContextSortList(result, context->sortClauses,
										   context->targetEntries);

		ListCell *rteCell = NULL;
		int varno = 0;

		List *quals = make_ands_implicit((Expr *) result->jointree->quals);
		foreach(rteCell, result->rtable)
		{
			varno++;

			RangeTblEntry *rte = (RangeTblEntry *) lfirst(rteCell);
			if (!IsResolvableMongoCollectionBasedRTE(rte, context->boundParams))
			{
				continue;
			}

			MongoCollection *collection = GetCollectionForRTE(rte, context->boundParams);
			if (collection != NULL && collection->shardKey == NULL)
			{
				/* construct a shard_key_value = <collection_id> filter */
				Expr *zeroShardKeyFilter = CreateNonShardedShardKeyValueFilter(varno,
																			   collection);

				/* add the filter to WHERE */
				quals = lappend(quals, zeroShardKeyFilter);
			}
		}

		result->jointree->quals = (Node *) make_ands_explicit(quals);

		/* back to parent query */
		context->currentQuery = prevQuery;
		context->sortClauses = prevSortClauses;
		context->targetEntries = prevTargetEntries;

		return (Node *) result;
	}

	return expression_tree_mutator(node, ReplaceBsonQueryOperatorsMutator, context);
}


/*
 * ExpandBsonQueryOperator returns an expanded expression that is logically
 * equivalent to the Mongo query BSON on the right side of the expression.
 * hasSublink flag is set when query tree has SUBLINK. This is used later in ReplaceBsonQueryOperatorsMutator to set Query->hasSubLinks flag.
 * Example: There is $elemMatch operator in expression which results in creation of SUBLINK in query tree.
 */
static Expr *
ExpandBsonQueryOperator(OpExpr *queryOpExpr, Node *queryNode,
						Query *currentQuery, ParamListInfo boundParams,
						List **targetEntries, List **sortClauses)
{
	BsonQueryOperatorContext context = { 0 };
	context.documentExpr = linitial(queryOpExpr->args);
	context.inputType = MongoQueryOperatorInputType_Bson;
	context.simplifyOperators = true;
	context.coerceOperatorExprIfApplicable = true;
	context.requiredFilterPathNameHashSet = NULL;
	context.variableContext = NULL;

	Node *queryExpr = queryNode;

	/* we can only expand Const queries */
	Assert(IsA(queryExpr, Const));

	/* extract the Mongo query document from the query Const */
	Const *queryConst = (Const *) queryExpr;

	/* Early bailout from planning if the query is null */
	if (queryConst->constisnull)
	{
		/* Return the original query expression for @@ which is marked strict and will return
		 * NULL when one of the operand is NULL
		 */
		return (Expr *) queryOpExpr;
	}

	pgbson *queryDocument = (pgbson *) DatumGetPointer(queryConst->constvalue);

	/* open the Mongo query document */
	bson_iter_t queryDocIter;
	PgbsonInitIterator(queryDocument, &queryDocIter);

	/* convert the Mongo query to a list of Postgres quals */
	List *quals = CreateQualsFromQueryDocIterator(&queryDocIter, &context);

	if (context.targetEntries != NULL)
	{
		*targetEntries = context.targetEntries;
	}
	if (context.sortClauses != NULL)
	{
		*sortClauses = context.sortClauses;
	}

	/* extract the collection via the document Var */
	if (quals != NIL)
	{
		Index collectionVarno;

		/* if query is on a collection, get the collection metadata */
		MongoCollection *collection =
			GetCollectionReferencedByDocumentVar(context.documentExpr,
												 currentQuery,
												 &collectionVarno, boundParams);
		if (collection != NULL)
		{
			bool hasShardKeyFilters = false;
			if (collection->shardKey != NULL)
			{
				/* extract the shard_key_value filter for the given collection */
				bson_value_t queryDocValue = ConvertPgbsonToBsonValue(queryDocument);
				Expr *shardKeyFilters =
					CreateShardKeyFiltersForQuery(&queryDocValue, collection->shardKey,
												  collection->collectionId,
												  collectionVarno);

				/* include shard_key_value filter in quals */
				if (shardKeyFilters != NULL)
				{
					hasShardKeyFilters = true;
					quals = lappend(quals, shardKeyFilters);
				}
			}
			else
			{
				hasShardKeyFilters = true;
			}

			if (hasShardKeyFilters)
			{
				/* Mongo allows collation on _id field. We need to make sure we do that as well. We can't
				 * push the Id filter to primary key index if the type needs to be collation aware (e.g., _id contains UTF8 )*/
				bool isCollationAware;
				Expr *idFilter = CreateIdFilterForQuery(quals,
														collectionVarno,
														&isCollationAware);

				/* include _id filter in quals */
				if (idFilter != NULL &&
					!(isCollationAware && IsCollationApplicable(
						  context.collationString)))
				{
					quals = lappend(quals, idFilter);
				}
			}
		}
	}

	/* create a function and hide it. Make the function point to the parameterized value
	 * So that the distributed planner works against this in Citus
	 * Since we replace the parameterized @@ operator with actual operators, the parameterized value
	 * is left behind with nothing in the query AST pointing to it. When Citus does a distributed
	 * query and pushes it to the remote workers via LibPQ, it sets the Oid of the parameterized
	 * types to 0 on the Coordinator; The worker then re-resolves the Oid based on the type Oid
	 * found in the worker. However, this doesn't work if the parameter is not referenced by the query
	 * (if there's unused parameters). This is a bug in Citus, and while this is unresolved, we can't
	 * leave the parameter lying around. Consequently, we use a dummy operator to point to the parameter
	 * to make distributed queries work.
	 * TODO: fix this in Citus and remove this block.
	 * Tracking bug: https://github.com/citusdata/citus/issues/5787
	 */
	Node *secondArg = lsecond(queryOpExpr->args);
	if (IsA(secondArg, Param))
	{
		FuncExpr *trueFunction = makeFuncExpr(
			BsonTrueFunctionId(), BOOLOID, list_make1(secondArg), InvalidOid,
			InvalidOid, COERCE_EXPLICIT_CALL);
		quals = lappend(quals, trueFunction);
	}

	/* multiple quals are implicit ANDs */
	return make_ands_explicit(quals);
}


/* For a given value, returns true if it is a valid element/document for $in/$nin ops.
 * Valid document : It can have $regex in it or {path:value} pattern but cannot have any other operator.
 */
bool
IsValidBsonDocumentForDollarInOrNinOp(const bson_value_t *value)
{
	if (value->value_type == BSON_TYPE_DOCUMENT)
	{
		bson_iter_t iterator;
		if (bson_iter_init_from_data(
				&iterator,
				value->value.v_doc.data,
				value->value.v_doc.data_len) &&
			bson_iter_next(&iterator))
		{
			const char *key = bson_iter_key(&iterator);

			if (key[0] == '$')
			{
				return strcmp(key, "$regex") == 0;
			}
		}
	}

	return true;
}


/*
 * ValidateQueryDocument is a wrapper around CreateQualsFromQueryDocIterator
 * that can be used to validate given query document.
 */
void
ValidateQueryDocument(pgbson *queryDocument)
{
	bson_iter_t queryDocIter;
	PgbsonInitIterator(queryDocument, &queryDocIter);

	BsonQueryOperatorContext context = {
		.documentExpr = (Expr *) MakeSimpleDocumentVar(),
		.inputType = MongoQueryOperatorInputType_Bson,
		.simplifyOperators = false,
		.coerceOperatorExprIfApplicable = false,
		.requiredFilterPathNameHashSet = NULL,
		.variableContext = NULL,
	};

	CreateQualsFromQueryDocIterator(&queryDocIter, &context);
}


/*
 * QueryDocumentsAreEquivalent returns true if given query documents are
 * equivalent.
 */
bool
QueryDocumentsAreEquivalent(const pgbson *leftQueryDocument,
							const pgbson *rightQueryDocument)
{
	bson_iter_t leftDocumentIter;
	PgbsonInitIterator(leftQueryDocument, &leftDocumentIter);

	BsonQueryOperatorContext leftDocOpContext = {
		.documentExpr = (Expr *) MakeSimpleDocumentVar(),
		.inputType = MongoQueryOperatorInputType_Bson,
		.simplifyOperators = false,
		.coerceOperatorExprIfApplicable = false,
		.requiredFilterPathNameHashSet = NULL,
		.variableContext = NULL,
	};

	List *leftDocumentQuals = CreateQualsFromQueryDocIterator(&leftDocumentIter,
															  &leftDocOpContext);

	bson_iter_t rightDocumentIter;
	PgbsonInitIterator(rightQueryDocument, &rightDocumentIter);

	BsonQueryOperatorContext rightDocOpContext = {
		.documentExpr = (Expr *) MakeSimpleDocumentVar(),
		.inputType = MongoQueryOperatorInputType_Bson,
		.simplifyOperators = false,
		.coerceOperatorExprIfApplicable = false,
		.requiredFilterPathNameHashSet = NULL,
		.variableContext = NULL,
	};

	List *rightDocumentQuals = CreateQualsFromQueryDocIterator(&rightDocumentIter,
															   &rightDocOpContext);

	bool weak = false;
	return predicate_implied_by(leftDocumentQuals, rightDocumentQuals, weak) &&
		   predicate_implied_by(rightDocumentQuals, leftDocumentQuals, weak);
}


/*
 * CreateQualsFromQueryDocIterator constructs a list of quals from a
 * query document iterator, which can be recursively called for logical
 * expressions.
 */
List *
CreateQualsFromQueryDocIterator(bson_iter_t *queryDocIterator,
								BsonQueryOperatorContext *context)
{
	const char *traversedPath = NULL;
	return CreateQualsFromQueryDocIteratorInternal(queryDocIterator, context,
												   traversedPath);
}


/*
 * if $ref and $id exist in the queryDocIterator, it needs to be treated as childPath,
 * and the quals need to be created for the childPath.
 * else should be treated as a regular queryDocIterator.
 */
static Expr *
CreateQualsForDBRef(bson_iter_t *refIterator,
					BsonQueryOperatorContext *context,
					const char *path)
{
	bson_value_t idValue = { 0 };
	bson_value_t dbValue = { 0 };
	bson_value_t refValue = *bson_iter_value(refIterator);

	List *quals = NIL;
	bool hasUnknownKey = false;
	while (bson_iter_next(refIterator))
	{
		const char *key = bson_iter_key(refIterator);
		if (strcmp(key, "$id") == 0)
		{
			idValue = *bson_iter_value(refIterator);
		}
		else if (strcmp(key, "$db") == 0)
		{
			dbValue = *bson_iter_value(refIterator);
		}
		else
		{
			hasUnknownKey = true;
			break;
		}
	}

	/*todo support optional fields */
	if (hasUnknownKey)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
						errmsg(
							"unknown key for DBRef, only $ref, $id and $db are allowed")));
	}

	const MongoQueryOperator *eqOperator = GetMongoQueryOperatorByQueryOperatorType(
		QUERY_OPERATOR_EQ, context->inputType);

	/* <path> : <value>, convert to = expression  */
	StringInfo pathBuffer = makeStringInfo();
	appendStringInfo(pathBuffer, "%s.$ref", path);
	Expr *refExpr = CreateFuncExprForQueryOperator(context, pathBuffer->data,
												   eqOperator, &refValue);
	quals = lappend(quals, refExpr);

	resetStringInfo(pathBuffer);
	appendStringInfo(pathBuffer, "%s.$id", path);
	Expr *idExpr = CreateFuncExprForQueryOperator(context, pathBuffer->data,
												  eqOperator, &idValue);
	quals = lappend(quals, idExpr);

	resetStringInfo(pathBuffer);
	appendStringInfo(pathBuffer, "%s.$db", path);
	Expr *dbExpr;
	if (dbValue.value_type != BSON_TYPE_EOD)
	{
		dbExpr = CreateFuncExprForQueryOperator(context, pathBuffer->data,
												eqOperator, &dbValue);
	}
	else
	{
		/* to align with Atlas behavior */
		const MongoQueryOperator *existOperator =
			GetMongoQueryOperatorByQueryOperatorType(
				QUERY_OPERATOR_EXISTS, context->inputType);
		dbValue.value_type = BSON_TYPE_BOOL;
		dbValue.value.v_bool = false;
		dbExpr = CreateFuncExprForQueryOperator(context, pathBuffer->data,
												existOperator, &dbValue);
	}
	quals = lappend(quals, dbExpr);
	pfree(pathBuffer->data);
	return make_ands_explicit(quals);
}


/*
 * Core implementation of CreateQualsFromQueryDocIterator.
 */
static List *
CreateQualsFromQueryDocIteratorInternal(bson_iter_t *queryDocIterator,
										BsonQueryOperatorContext *context,
										const char *traversedPath)
{
	List *quals = NIL;
	check_stack_depth();
	while (bson_iter_next(queryDocIterator))
	{
		/* field or logical operator */
		const char *path = bson_iter_key(queryDocIterator);
		Expr *qual;

		if (path[0] == '$')
		{
			/* we expect all top-level operators to be logical expressions */
			qual = CreateBoolExprFromLogicalExpression(queryDocIterator, context,
													   traversedPath);
		}
		else
		{
			/* all other paths are comparisons */
			qual = CreateOpExprFromComparisonExpression(queryDocIterator, context,
														traversedPath,
														path);
		}

		quals = lappend(quals, qual);
	}

	return quals;
}


/*
 * CreateBoolExprFromLogicalExpression converts $and, $or, $nor and $not
 * expressions to Postgres expressions.
 */
static Expr *
CreateBoolExprFromLogicalExpression(bson_iter_t *queryDocIterator,
									BsonQueryOperatorContext *context,
									const char *traversedPath)
{
	const char *mongoOperatorName = bson_iter_key(queryDocIterator);
	const MongoQueryOperator *operator = GetMongoQueryOperatorByMongoOpName(
		mongoOperatorName, context->inputType);

	/*
	 * Increment the feature counter for the operator,
	 * make sure we don't attempt to read out of range feature as this
	 * is in shared memory space
	 */
	if (operator->featureType >= 0 && operator->featureType < MAX_FEATURE_INDEX)
	{
		ReportFeatureUsage(operator->featureType);
	}

	MongoQueryOperatorType operatorType = operator->operatorType;

	if (operatorType != QUERY_OPERATOR_AND &&
		operatorType != QUERY_OPERATOR_OR &&
		operatorType != QUERY_OPERATOR_NOR &&
		operatorType != QUERY_OPERATOR_EXPR &&
		operatorType != QUERY_OPERATOR_TEXT &&
		operatorType != QUERY_OPERATOR_ALWAYS_TRUE &&
		operatorType != QUERY_OPERATOR_ALWAYS_FALSE &&
		operatorType != QUERY_OPERATOR_SAMPLERATE)
	{
		/* invalid query operator such as $eq at top level of query document */
		/* We throw feature not supported since $where and such might be specified here */
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
						errmsg(
							"unknown top level operator: %s. If you have a field "
							"name that starts with a '$' symbol, consider using "
							"$getField or $setField.",
							mongoOperatorName),
						errdetail_log(
							"unknown top level operator: %s. If you have a field "
							"name that starts with a '$' symbol, consider using "
							"$getField or $setField.",
							mongoOperatorName)));
	}

	if (operatorType == QUERY_OPERATOR_ALWAYS_TRUE ||
		operatorType == QUERY_OPERATOR_ALWAYS_FALSE)
	{
		const bson_value_t *value = bson_iter_value(queryDocIterator);
		if (!BsonValueIsNumberOrBool(value) || BsonValueAsInt32(value) != 1)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
							errmsg("%s must be an integer value of 1",
								   operatorType == QUERY_OPERATOR_ALWAYS_TRUE ?
								   "$alwaysTrue" : "$alwaysFalse")));
		}

		bool isNull = false;
		return (Expr *) makeBoolConst(operatorType == QUERY_OPERATOR_ALWAYS_TRUE, isNull);
	}

	if (operatorType == QUERY_OPERATOR_SAMPLERATE)
	{
		/* This expression { $sampleRate: 0.33 } is equivalent to
		 *  using the $rand: { $lt: [ { $rand: {} }, 0.33 ] } */
		const bson_value_t *sampleRate = bson_iter_value(queryDocIterator);
		if (!BsonValueIsNumber(sampleRate))
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
							errmsg(
								"argument to $sampleRate must be a numeric type"),
							errhint("argument to $sampleRate is: %s",
									BsonValueToJsonForLogging(sampleRate))));
		}
		double sampleRateValue = BsonValueAsDouble(sampleRate);

		/* sampleRate value outside of [0, 1] is invalid.
		 * use de-morgan's law to handle nan, because the comparisons involving NaN are always false.
		 * ref: https://standards.ieee.org/ieee/754/6210, boolean sameQuantum(source, source) */
		if (!(sampleRateValue >= 0 && sampleRateValue <= 1))
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
							errmsg(
								"numeric argument to $sampleRate must be in [0, 1]"),
							errhint(
								"numeric argument to $sampleRate is: %f",
								sampleRateValue)));
		}

		/* PostgresDrandomFunctionId() returns a double precision float
		 * in a continuous uniform distribution between [0.0, 1.0).
		 * if sample_rate=0.0, zero documents will be selected
		 * if sample_rate=1.0, all documents will be selected */
		Expr *randomValueExpr = (Expr *) makeFuncExpr(PostgresDrandomFunctionId(),
													  FLOAT8OID,
													  NIL,
													  InvalidOid, InvalidOid,
													  COERCE_EXPLICIT_CALL);

		Expr *constExpr = (Expr *) makeConst(FLOAT8OID, -1, InvalidOid, sizeof(float8),
											 Float8GetDatum(sampleRateValue), false,
											 true);
		Expr *opExpr = make_opclause(Float8LessOperator, BOOLOID, false,
									 randomValueExpr, constExpr, InvalidOid, InvalidOid);
		return opExpr;
	}

	if (operatorType == QUERY_OPERATOR_EXPR)
	{
		if (context->inputType != MongoQueryOperatorInputType_Bson)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
							errmsg(
								"$expr can only be applied to the top-level document")));
		}

		if (context->variableContext != NULL)
		{
			/* $expr with let */
			Const *constValue = CreateConstFromBsonValue("", bson_iter_value(
															 queryDocIterator),
														 context->collationString);
			return (Expr *) makeFuncExpr(BsonExprWithLetFunctionId(), BOOLOID,
										 list_make3(context->documentExpr, constValue,
													context->variableContext),
										 InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
		}

		/* Special case for $expr */
		const char *path = "";
		return CreateFuncExprForQueryOperator(context,
											  path,
											  operator,
											  bson_iter_value(queryDocIterator));
	}

	if (operatorType == QUERY_OPERATOR_TEXT)
	{
		if (context->inputType != MongoQueryOperatorInputType_Bson)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
							errmsg(
								"$text can only be applied to the top-level document")));
		}

		/* Special case for $text */
		const char *path = "";
		BsonValidateTextQuery(bson_iter_value(queryDocIterator));
		return CreateFuncExprForQueryOperator(context,
											  path,
											  operator,
											  bson_iter_value(queryDocIterator));
	}

	bson_iter_t logicalExpressionsIterator;

	/* type safety checks */
	if (bson_iter_type(queryDocIterator) != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
							"%s must be an array",
							mongoOperatorName)));
	}

	/* open array of expressions (or document in case of $not) */
	bson_iter_recurse(queryDocIterator, &logicalExpressionsIterator);

	/* convert logical query operator to BoolExpr */
	BoolExpr *logicalExpr = makeNode(BoolExpr);

	switch (operatorType)
	{
		case QUERY_OPERATOR_AND:
		{
			List *innerQuals =
				CreateQualsFromLogicalExpressionArrayIterator(&logicalExpressionsIterator,
															  context, traversedPath);

			if (context->simplifyOperators &&
				list_length(innerQuals) == 1)
			{
				/* And of a single entry is just the entry */
				return linitial(innerQuals);
			}

			logicalExpr->boolop = AND_EXPR;
			logicalExpr->args = innerQuals;
			logicalExpr->location = -1;
			break;
		}

		case QUERY_OPERATOR_OR:
		{
			List *innerQuals =
				CreateQualsFromLogicalExpressionArrayIterator(&logicalExpressionsIterator,
															  context, traversedPath);

			/* Special case, if the $or is a simple array of equality on the same path
			 * then it's converted to a $in
			 */
			if (context->simplifyOperators)
			{
				Expr *processedQual = TryProcessOrIntoDollarIn(context, innerQuals);
				if (processedQual != NULL)
				{
					return processedQual;
				}

				processedQual = TryOptimizeDollarOrExpr(context, innerQuals);
				if (processedQual != NULL)
				{
					return processedQual;
				}
			}

			logicalExpr->boolop = OR_EXPR;
			logicalExpr->args = innerQuals;
			logicalExpr->location = -1;
			break;
		}

		case QUERY_OPERATOR_NOR:
		{
			List *innerQuals =
				CreateQualsFromLogicalExpressionArrayIterator(&logicalExpressionsIterator,
															  context, traversedPath);

			BoolExpr *orExpr = makeNode(BoolExpr);
			orExpr->boolop = OR_EXPR;
			orExpr->args = innerQuals;
			orExpr->location = -1;

			Const *falseConst = makeConst(BOOLOID, -1, InvalidOid, 1,
										  BoolGetDatum(false), false, true);

			/* convert NULL to false */
			CoalesceExpr *coalesceExpr = makeNode(CoalesceExpr);
			coalesceExpr->coalescetype = BOOLOID;
			coalesceExpr->coalescecollid = InvalidOid;
			coalesceExpr->args = list_make2(orExpr, falseConst);
			coalesceExpr->location = -1;

			/* negate (NULL and false become true) */
			logicalExpr->boolop = NOT_EXPR;
			logicalExpr->args = list_make1(coalesceExpr);
			logicalExpr->location = -1;
			break;
		}

		default:
		{
			ereport(ERROR, (errmsg("unrecognized logical operator: %d", operatorType)));
		}
	}

	/* Fail if geoNear op was found under $or or $nor along with other filters. */
	if ((operatorType == QUERY_OPERATOR_OR || operatorType == QUERY_OPERATOR_NOR) &&
		context->targetEntries != NULL &&
		TargetListContainsGeonearOp(context->targetEntries))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
							"geo $near must be top-level expr")));
	}

	return (Expr *) logicalExpr;
}


/*
 * CreateQualsFromLogicalExpressionArrayIterator converts all elements of the given array
 * to quals
 */
static List *
CreateQualsFromLogicalExpressionArrayIterator(bson_iter_t *expressionsArrayIterator,
											  BsonQueryOperatorContext *context,
											  const char *traversedPath)
{
	List *quals = NIL;

	while (bson_iter_next(expressionsArrayIterator))
	{
		if (bson_iter_type(expressionsArrayIterator) != BSON_TYPE_DOCUMENT)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
								"$or/$and/$nor entries need to be full objects")));
		}

		/* open expression document */
		bson_iter_t expressionDocIterator;
		bson_iter_recurse(expressionsArrayIterator, &expressionDocIterator);

		/* convert to list of quals */
		List *expressionQuals = CreateQualsFromQueryDocIteratorInternal(
			&expressionDocIterator,
			context,
			traversedPath);

		/* make ANDs of individual expressions explicit */
		Expr *andedExpressionQual = make_ands_explicit(expressionQuals);

		quals = lappend(quals, andedExpressionQual);
	}

	if (quals == NIL)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
							"$or/$and/$nor arrays must have at least one entry")));
	}

	return quals;
}


/* Check if the bson iterator's value type is UNDEFINED
 * iter: iterator which holds the value to be validated
 * isInMatchExpression: true, when the operator for which the
 *          value to be validated is either $in/$nin
 */
static inline void
ValidateIfIteratorValueUndefined(bson_iter_t *iter, bool isInMatchExpression)
{
	if (BSON_ITER_HOLDS_UNDEFINED(iter))
	{
		if (isInMatchExpression)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
								"InMatchExpression equality cannot be undefined")));
		}
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
							"cannot compare to undefined")));
	}
}


/*
 * CreateOpExprFromComparisonExpression creates an operator expression of the form
 * (<bson> <operator> <bson> [AND ...]) for a single path from queryDocIterator.
 */
static Expr *
CreateOpExprFromComparisonExpression(bson_iter_t *queryDocIterator,
									 BsonQueryOperatorContext *context,
									 const char *traversedPath,
									 const char *currentBsonKey)
{
	/* Check if the operator value to be validated is for $in/$nin */
	bool isInMatchExpression = false;

	/* Check if the value without operator is 'undefined', for e.g. {x: undefined} */
	ValidateIfIteratorValueUndefined(queryDocIterator, isInMatchExpression);

	const char *key = currentBsonKey;

	StringInfo pathBuffer = makeStringInfo();
	if (traversedPath != NULL)
	{
		appendStringInfo(pathBuffer, "%s.", traversedPath);
	}

	appendStringInfoString(pathBuffer, key);
	char *path = pathBuffer->data;
	const bson_value_t *value = bson_iter_value(queryDocIterator);

	if (bson_iter_type(queryDocIterator) == BSON_TYPE_REGEX)
	{
		/* <path> : {"$regex": "/.../"}, convert to @~ expression  */
		const MongoQueryOperator *regexOperator =
			GetMongoQueryOperatorByQueryOperatorType(
				QUERY_OPERATOR_REGEX, context->inputType);

		return CreateFuncExprForQueryOperator(context, path, regexOperator,
											  value);
	}
	else if (bson_iter_type(queryDocIterator) != BSON_TYPE_DOCUMENT)
	{
		const MongoQueryOperator *eqOperator = GetMongoQueryOperatorByQueryOperatorType(
			QUERY_OPERATOR_EQ, context->inputType);

		/* <path> : <value>, convert to = expression  */
		return CreateFuncExprForQueryOperator(context, path, eqOperator, value);
	}

	/*
	 * We have a <field> : <query operator> pair, where query operator is
	 * of the form { "$op" : <value> }, with the possibility of multiple
	 * operators.
	 */

	/* open query operator BSON document */
	bson_iter_t operatorDocIterator;
	bson_iter_recurse(queryDocIterator, &operatorDocIterator);

	/*
	 * If the document under path is empty or starts with a non-operator,
	 * treat it as an equality comparison.
	 */
	bson_iter_t checkIterator = operatorDocIterator;
	if (!bson_iter_next(&checkIterator) || bson_iter_key(&checkIterator)[0] != '$')
	{
		const MongoQueryOperator *eqOperator = GetMongoQueryOperatorByQueryOperatorType(
			QUERY_OPERATOR_EQ, context->inputType);
		return CreateFuncExprForQueryOperator(context, path, eqOperator, value);
	}

	/*
	 * Operator document of the form { "$op" : <value>, ... }.
	 * Convert operators into corresponding Postgres expressions.
	 */
	return CreateOpExprFromOperatorDocIterator(path, &operatorDocIterator, context);
}


/*
 * Simple wrapper inline function to minimize the switch statement duplication.
 */
inline static Expr *
CreateFuncExprForSimpleQueryOperator(bson_iter_t *operatorDocIterator,
									 BsonQueryOperatorContext *context,
									 const MongoQueryOperator *operator,
									 const char *path)
{
	/* get value we are comparing against */
	const bson_value_t *value = bson_iter_value(operatorDocIterator);

	/* construct the <document> <operator> { <path> : <value> } expression */
	return CreateFuncExprForQueryOperator(
		context,
		path, operator,
		value);
}


/*
 * CreateOpExprFromOperatorDocIterator creates an operator expression of the form
 * (<bson> <operator> <bson> [AND ...]) for a given path and expression of the
 * form { "$op" : <value>, ... }.
 */
static Expr *
CreateOpExprFromOperatorDocIterator(const char *path,
									bson_iter_t *operatorDocIterator,
									BsonQueryOperatorContext *context)
{
	bool regexFound = false;
	bson_value_t *options = NULL;
	List *quals = NIL;

	while (bson_iter_next(operatorDocIterator))
	{
		Expr *qual = CreateOpExprFromOperatorDocIteratorCore(operatorDocIterator, context,
															 path, &regexFound, &options);
		if (qual != NULL)
		{
			quals = lappend(quals, qual);
		}
	}

	/* "options" is initialized only when $options is seen before $regex.
	 * Later when $regex is found, options will be consumed and an expression
	 * is created. It will be then set to NULL. If this
	 * (setting of options to NULL) did not happen, that means there is
	 * $options present as an orphan without a $regex. Hence throw error */
	if (options != NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
							"$options needs a $regex")));
	}

	return make_ands_explicit(quals);
}


static Expr *
CreateOpExprFromOperatorDocIteratorCore(bson_iter_t *operatorDocIterator,
										BsonQueryOperatorContext *context,
										const char *path,
										bool *regexFound,
										bson_value_t **options)
{
	/* get query operator type */
	const char *mongoOperatorName = bson_iter_key(operatorDocIterator);
	const MongoQueryOperator *operator =
		GetMongoQueryOperatorByMongoOpName(mongoOperatorName, context->inputType);

	/*
	 * Increment the feature counter for the operator,
	 * make sure we don't attempt to read out of range feature as this
	 * is in shared memory space
	 */
	if (operator->featureType >= 0 && operator->featureType < MAX_FEATURE_INDEX)
	{
		ReportFeatureUsage(operator->featureType);
	}

	switch (operator->operatorType)
	{
		case QUERY_OPERATOR_IN:
		case QUERY_OPERATOR_NIN:
		{
			if (!BSON_ITER_HOLDS_ARRAY(operatorDocIterator))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
								errmsg("%s needs an array", mongoOperatorName)));
			}

			bson_iter_t arrayIterator;
			int32_t numValues = 0;
			bson_value_t currentValue = { 0 };
			if (bson_iter_recurse(operatorDocIterator, &arrayIterator))
			{
				while (bson_iter_next(&arrayIterator))
				{
					numValues++;
					currentValue = *bson_iter_value(&arrayIterator);
					if (!IsValidBsonDocumentForDollarInOrNinOp(
							&currentValue))
					{
						ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
											"cannot nest $ under %s",
											operator->mongoOperatorName)));
					}
					else
					{
						/* Check if the operator value to be validated is for $in/$nin */
						bool isInMatchExpression = true;
						ValidateIfIteratorValueUndefined(&arrayIterator,
														 isInMatchExpression);
					}
				}
			}

			if (numValues == 1 && context->simplifyOperators &&
				currentValue.value_type != BSON_TYPE_REGEX)
			{
				/* Special case, $in with a single value is converted to $eq except in the case of Regexes */
				MongoQueryOperatorType operatorType = operator->operatorType ==
													  QUERY_OPERATOR_IN ?
													  QUERY_OPERATOR_EQ :
													  QUERY_OPERATOR_NE;
				const MongoQueryOperator *actualOperator =
					GetMongoQueryOperatorByQueryOperatorType(
						operatorType, context->inputType);
				return CreateFuncExprForQueryOperator(context, path, actualOperator,
													  &currentValue);
			}
			else
			{
				return CreateFuncExprForSimpleQueryOperator(operatorDocIterator,
															context, operator,
															path);
			}
		}

		case QUERY_OPERATOR_ALL:
		{
			if (!BSON_ITER_HOLDS_ARRAY(operatorDocIterator))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
								errmsg("%s needs an array", mongoOperatorName)));
			}

			bson_iter_t arrayIterator;
			if (bson_iter_recurse(operatorDocIterator, &arrayIterator))
			{
				while (bson_iter_next(&arrayIterator))
				{
					/* Check if the operator value to be validated is for $in/$nin */
					bool isInMatchExpression = false;
					ValidateIfIteratorValueUndefined(&arrayIterator,
													 isInMatchExpression);
				}
			}

			return CreateExprForDollarAll(path, operatorDocIterator,
										  context, operator);
		}

		case QUERY_OPERATOR_SIZE:
		{
			const bson_value_t *value = bson_iter_value(operatorDocIterator);

			/**
			 * TODO: FIXME - Verify whether strict number check is required here
			 * */
			if (!BsonValueIsNumberOrBool(value))
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg(
									"Failed to parse $size. Expected a number in: $size: %s",
									BsonValueToJsonForLogging(value)),
								errhint(
									"Failed to parse $size. Expected a number in: $size, found %s",
									BsonTypeName(value->value_type))));
			}
			else
			{
				double doubleValue = BsonValueAsDouble(value);
				if (!IsDoubleAFixedInteger(doubleValue))
				{
					ereport(ERROR, (errcode(MongoBadValue),
									errmsg(
										"Failed to parse $size. Expected an integer in: $size: %s",
										BsonValueToJsonForLogging(value)),
									errhint(
										"Failed to parse $size. Expected an integer in: $size, found %s",
										BsonTypeName(value->value_type))));
				}
				else if (doubleValue < 0)
				{
					ereport(ERROR, (errcode(MongoBadValue), errmsg(
										"Failed to parse $size. Expected a non-negative number in: $size: %s",
										BsonValueToJsonForLogging(value))));
				}
			}

			return CreateFuncExprForSimpleQueryOperator(operatorDocIterator,
														context, operator,
														path);
		}

		case QUERY_OPERATOR_ELEMMATCH:
		{
			if (bson_iter_type(operatorDocIterator) != BSON_TYPE_DOCUMENT)
			{
				ereport(ERROR, (errcode(MongoBadValue), errmsg(
									"$elemMatch needs an Object")));
			}

			return CreateFuncExprForSimpleQueryOperator(operatorDocIterator,
														context, operator,
														path);
		}

		case QUERY_OPERATOR_TYPE:
		{
			const bson_value_t *typeIdValue = bson_iter_value(operatorDocIterator);
			if (BSON_ITER_HOLDS_UTF8(operatorDocIterator))
			{
				/* try to resolve the type */
				EnsureValidTypeNameForDollarType(typeIdValue->value.v_utf8.str);
			}
			else if (BSON_ITER_HOLDS_NUMBER(operatorDocIterator))
			{
				double doubleValue = BsonValueAsDouble(typeIdValue);
				if (!IsDoubleAFixedInteger(doubleValue))
				{
					ereport(ERROR, (errcode(MongoBadValue),
									errmsg("Invalid numerical type code %s",
										   BsonValueToJsonForLogging(typeIdValue))));
				}

				/* try to resolve the type */
				int64_t typeNumber = BsonValueAsInt64(typeIdValue);
				EnsureValidTypeCodeForDollarType(typeNumber);
			}
			else if (BSON_ITER_HOLDS_ARRAY(operatorDocIterator))
			{
				bson_iter_t arrayIterator;
				bson_iter_recurse(operatorDocIterator, &arrayIterator);
				bool typeArrayHasElements = false;
				while (bson_iter_next(&arrayIterator))
				{
					typeArrayHasElements = true;
					const bson_value_t *typeIdArrayValue = bson_iter_value(
						&arrayIterator);
					if (BSON_ITER_HOLDS_UTF8(&arrayIterator))
					{
						/* try to resolve the type */
						EnsureValidTypeNameForDollarType(
							typeIdArrayValue->value.v_utf8.str);
					}
					else if (BSON_ITER_HOLDS_NUMBER(&arrayIterator))
					{
						double doubleValue = BsonValueAsDouble(typeIdArrayValue);
						if (!IsDoubleAFixedInteger(doubleValue))
						{
							ereport(ERROR, (errcode(MongoBadValue),
											errmsg("Invalid numerical type code %s",
												   BsonValueToJsonForLogging(
													   typeIdArrayValue))));
						}

						/* try to resolve the type */
						int64_t typeNumber = BsonValueAsInt64(typeIdArrayValue);
						EnsureValidTypeCodeForDollarType(typeNumber);
					}
					else
					{
						ereport(ERROR, (errcode(MongoBadValue),
										errmsg(
											"type must be represented as a number or a string")));
					}
				}

				if (!typeArrayHasElements)
				{
					ereport(ERROR, (errcode(MongoFailedToParse),
									errmsg("%s must match at least one type", path)));
				}
			}
			else
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg(
									"type must be represented as a number or a string")));
			}

			return CreateFuncExprForSimpleQueryOperator(operatorDocIterator,
														context, operator,
														path);
		}

		case QUERY_OPERATOR_EXISTS:
		{
			bool existsArg = BsonValueAsBool(bson_iter_value(operatorDocIterator));

			if (existsArg)
			{
				/* In order to support partial filter expressions with $exist: true we need to transform it to a >= MinKey as Postgres partial indexes only support
				 * GT, LT, EQ, NE operator types.
				 */
				const MongoQueryOperator *actualOperator =
					GetMongoQueryOperatorByQueryOperatorType(
						QUERY_OPERATOR_GTE, context->inputType);

				bson_value_t minKeyValue = {
					.value_type = BSON_TYPE_MINKEY
				};

				return CreateFuncExprForQueryOperator(context, path, actualOperator,
													  &minKeyValue);
			}
			else
			{
				/* $exists: false is not supported on partial filter expressions */
				bool isInMatchExpression = false;
				ValidateIfIteratorValueUndefined(operatorDocIterator,
												 isInMatchExpression);
				return CreateFuncExprForSimpleQueryOperator(operatorDocIterator,
															context, operator,
															path);
			}
		}

		case QUERY_OPERATOR_EQ:
		{
			/* Check if the operator value to be validated is for $in/$nin */
			bool isInMatchExpression = false;
			ValidateIfIteratorValueUndefined(operatorDocIterator, isInMatchExpression);
			return CreateFuncExprForSimpleQueryOperator(operatorDocIterator,
														context, operator,
														path);
		}

		case QUERY_OPERATOR_GT:
		case QUERY_OPERATOR_GTE:
		case QUERY_OPERATOR_LT:
		case QUERY_OPERATOR_LTE:
		case QUERY_OPERATOR_NE:
		{
			/* Regex arguments not allowed with these operators */
			if (BSON_ITER_HOLDS_REGEX(operatorDocIterator))
			{
				if (operator->operatorType == QUERY_OPERATOR_NE)
				{
					ereport(ERROR, (errcode(MongoBadValue),
									errmsg("Can't have regex as arg to $ne.")));
				}

				ereport(ERROR, (errcode(MongoBadValue),
								errmsg(
									"Can't have RegEx as arg to predicate over field '%s'.",
									path)));
			}

			/* Check if the operator value to be validated is for $in/$nin */
			bool isInMatchExpression = false;
			ValidateIfIteratorValueUndefined(operatorDocIterator, isInMatchExpression);
			return CreateFuncExprForSimpleQueryOperator(operatorDocIterator,
														context, operator,
														path);
		}

		case QUERY_OPERATOR_MOD:
		{
			return CreateExprForDollarMod(operatorDocIterator,
										  context, operator, path);
		}

		case QUERY_OPERATOR_REGEX:
		{
			*regexFound = true;

			return CreateExprForDollarRegex(operatorDocIterator,
											options, context, operator, path);
		}

		case QUERY_OPERATOR_NOT:
		{
			Expr *innerExpr;

			if (bson_iter_type(operatorDocIterator) == BSON_TYPE_DOCUMENT)
			{
				/* open inner document */
				bson_iter_t notIterator;
				bson_iter_recurse(operatorDocIterator, &notIterator);

				/* Throw error if $not spec is empty */
				bson_iter_t checkIterator = notIterator;
				if (!bson_iter_next(&checkIterator))
				{
					ereport(ERROR, (errcode(MongoBadValue), errmsg(
										"$not cannot be empty")));
				}

				/*
				 * Convert query document within { <path> : {"$not" : { ... }}}
				 * to an expression on the original path.
				 */
				innerExpr =
					CreateOpExprFromOperatorDocIterator(path, &notIterator, context);
			}
			else if (bson_iter_type(operatorDocIterator) == BSON_TYPE_REGEX)
			{
				const bson_value_t *regexValue = bson_iter_value(operatorDocIterator);

				const MongoQueryOperator *regexOperator =
					GetMongoQueryOperatorByQueryOperatorType(QUERY_OPERATOR_REGEX,
															 context->inputType);

				/* convert <path> : {"$regex": "/.../"} to @~ expression  */
				innerExpr =
					CreateFuncExprForQueryOperator(context, path,
												   regexOperator, regexValue);
			}
			else
			{
				ereport(ERROR, (errcode(MongoBadValue), errmsg(
									"$not needs a regex or a document")));
			}

			Expr *optimizedExpr = TryOptimizeNotInnerExpr(innerExpr, context);
			if (optimizedExpr != NULL)
			{
				return optimizedExpr;
			}

			Const *falseConst = makeConst(BOOLOID, -1, InvalidOid, 1,
										  BoolGetDatum(false), false, true);

			/* convert NULL to false */
			CoalesceExpr *coalesceExpr = makeNode(CoalesceExpr);
			coalesceExpr->coalescetype = BOOLOID;
			coalesceExpr->coalescecollid = InvalidOid;
			coalesceExpr->args = list_make2(innerExpr, falseConst);
			coalesceExpr->location = -1;

			/* negate (NULL and false become true) */
			BoolExpr *notExpr = makeNode(BoolExpr);
			notExpr->boolop = NOT_EXPR;
			notExpr->args = list_make1(coalesceExpr);
			notExpr->location = -1;
			return (Expr *) notExpr;
		}

		case QUERY_OPERATOR_BITS_ANY_CLEAR:
		case QUERY_OPERATOR_BITS_ALL_CLEAR:
		case QUERY_OPERATOR_BITS_ALL_SET:
		case QUERY_OPERATOR_BITS_ANY_SET:
		{
			return CreateExprForBitwiseQueryOperators(operatorDocIterator,
													  context,
													  operator,
													  path);
		}

		case QUERY_OPERATOR_TEXT:
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg(
								"$text operator is not yet implemented")));
		}

		case QUERY_OPERATOR_WITHIN:
		case QUERY_OPERATOR_GEOWITHIN:
		{
			EnsureGeospatialFeatureEnabled();

			if (!BSON_ITER_HOLDS_DOCUMENT(operatorDocIterator))
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg("geometry must be an object")));
			}

			const bson_value_t *value = bson_iter_value(operatorDocIterator);
			bson_value_t shapesValue;
			const ShapeOperator *shapeOperator = GetShapeOperatorByValue(value,
																		 &shapesValue);

			ShapeOperatorInfo *opInfo = palloc0(sizeof(ShapeOperatorInfo));
			opInfo->queryStage = QueryStage_RUNTIME;
			opInfo->queryOperatorType = operator->operatorType;

			/* Only Validate the shapeOperator */
			shapeOperator->getShapeDatum(&shapesValue, opInfo);

			Expr *geoWithinFuncExpr = CreateFuncExprForSimpleQueryOperator(
				operatorDocIterator, context, operator, path);
			return WithIndexSupportExpression(context->documentExpr, geoWithinFuncExpr,
											  path, shapeOperator->isSpherical);
		}

		case QUERY_OPERATOR_GEOINTERSECTS:
		{
			EnsureGeospatialFeatureEnabled();

			if (!BSON_ITER_HOLDS_DOCUMENT(operatorDocIterator))
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg("geometry must be an object")));
			}

			const bson_value_t *value = bson_iter_value(operatorDocIterator);
			bson_value_t shapesValue;
			const ShapeOperator *shapeOperator = GetShapeOperatorByValue(value,
																		 &shapesValue);

			if (shapeOperator->op != GeospatialShapeOperator_GEOMETRY)
			{
				/* In mongo $centerSphere with $geoIntersects does not throw error but it should
				 * https://jira.mongodb.org/browse/SERVER-30390
				 */

				ereport(ERROR, (
							errcode(MongoBadValue),
							errmsg(
								"$geoIntersect not supported with provided geometry: %s",
								BsonValueToJsonForLogging(value)),
							errhint(
								"$geoIntersect not supported with provided geometry.")));
			}

			ShapeOperatorInfo *opInfo = palloc0(sizeof(ShapeOperatorInfo));
			opInfo->queryOperatorType = operator->operatorType;

			/* Validate the query at planning */
			shapeOperator->getShapeDatum(&shapesValue, opInfo);

			Expr *geoIntersectsFuncExpr = CreateFuncExprForSimpleQueryOperator(
				operatorDocIterator, context, operator, path);
			return WithIndexSupportExpression(context->documentExpr,
											  geoIntersectsFuncExpr,
											  path, shapeOperator->isSpherical);
		}

		case QUERY_OPERATOR_NEAR:
		case QUERY_OPERATOR_NEARSPHERE:
		case QUERY_OPERATOR_GEONEAR:
		{
			EnsureGeospatialFeatureEnabled();

			if (!IsClusterVersionAtleastThis(1, 17, 2))
			{
				ereport(ERROR, (errcode(MongoCommandNotSupported),
								errmsg(
									"$near and $nearSphere are not supported yet.")));
			}

			if (!BSON_ITER_HOLDS_DOCUMENT(operatorDocIterator) &&
				!BSON_ITER_HOLDS_ARRAY(operatorDocIterator))
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg("near must be first in: { $near: %s }",
									   BsonValueToJsonForLogging(
										   bson_iter_value(operatorDocIterator)))));
			}

			return ParseBsonValueForNearAndCreateOpExpr(operatorDocIterator, context,
														path, mongoOperatorName);
		}

		/* logical operators are not supposed to be recognized at this level */
		case QUERY_OPERATOR_AND:
		case QUERY_OPERATOR_NOR:
		case QUERY_OPERATOR_OR:

		default:
		{
			if (strcmp(mongoOperatorName, "$options") == 0)
			{
				if (*regexFound)
				{
					/* Just ignore this $options as this is already dealt when processing $regex */
					*regexFound = false;
					return NULL;
				}

				/* This happens when options for $regex is provided before
				 * $regex. Then store the Options string in "options" and
				 * use it when $regex is hit next */
				*options = palloc(sizeof(bson_value_t));
				bson_value_copy(bson_iter_value(operatorDocIterator), *options);

				return NULL;
			}

			if (strcmp(mongoOperatorName, "$ref") == 0)
			{
				bson_iter_t refIterator = *operatorDocIterator;
				if (bson_iter_next(&refIterator) && strcmp(bson_iter_key(&refIterator),
														   "$id") == 0)
				{
					return CreateQualsForDBRef(operatorDocIterator, context, path);
				}
			}

			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"unknown operator: %s",
								mongoOperatorName),
							errhint("unknown operator: %s",
									mongoOperatorName)));
		}
	}
}


/*
 * Validates that a given string type name is correct as
 * an input for $type.
 */
static void
EnsureValidTypeNameForDollarType(const char *typeName)
{
	/* Special case for number */
	if (strcmp(typeName, "number") == 0)
	{
		return;
	}

	BsonTypeFromName(typeName);
}


/*
 * Validates that a given type code is valid as an input for
 * $type and throws if it is not.
 */
static void
EnsureValidTypeCodeForDollarType(int64_t typeCode)
{
	bson_type_t ignoreType;
	if (!TryGetTypeFromInt64(typeCode, &ignoreType))
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"Invalid numerical type code: %lld",
							(long long int) typeCode)));
	}
}


/*
 * Creates Expression for
 * {"path" : { "$bitsOp": value} }.
 */
static Expr *
CreateExprForBitwiseQueryOperators(bson_iter_t *operatorDocIterator,
								   BsonQueryOperatorContext *context,
								   const MongoQueryOperator *operator,
								   const char *path)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	const bson_value_t *operatorDocValue = bson_iter_value(operatorDocIterator);
	bson_type_t operatorDocValueType = bson_iter_type(operatorDocIterator);
	bool isInputArrayAlreadySorted = false;
	Expr *qual;

	switch (operatorDocValueType)
	{
		case BSON_TYPE_INT64:
		{
			/*
			 * NOTE: Mongo does not throw the exception but they claim in their
			 * public doc that numeric bitmask should fit into 32 bit signed int
			 */
			int int32Val = BsonValueAsInt32(operatorDocValue);
			int64 int64Val = operatorDocValue->value.v_int64;

			if (int32Val != int64Val)
			{
				ereport(ERROR, (errcode(MongoFailedToParse),
								errmsg(
									"Cannot represent as a 32-bit integer: %s: %s.0",
									operator->mongoOperatorName,
									BsonValueToJsonForLogging(
										operatorDocValue)),
								errhint(
									"Cannot represent argument of type %s as a 32-bit integer in operator: %s",
									BsonTypeName(operatorDocValue->value_type),
									operator->mongoOperatorName)));
			}

			int dataLength = sizeof(int32Val);
			WriteSetBitPositionArray((uint8_t *) &int32Val, dataLength, &writer);
			break;
		}

		case BSON_TYPE_DECIMAL128:
		{
			if (!IsDecimal128InInt32Range(operatorDocValue) ||
				!IsDecimal128AFixedInteger(operatorDocValue))
			{
				ereport(ERROR, (errcode(MongoFailedToParse),
								errmsg(
									"Cannot represent as a 32-bit integer: %s: %s.0",
									operator->mongoOperatorName,
									BsonValueToJsonForLogging(
										operatorDocValue)),
								errhint(
									"Cannot represent argument of type %s as a 32-bit integer in operator: %s",
									BsonTypeName(operatorDocValue->value_type),
									operator->mongoOperatorName)));
			}

			int intVal = BsonValueAsInt32(operatorDocValue);
			if (intVal < 0)
			{
				ereport(ERROR, (errcode(MongoFailedToParse),
								errmsg(
									"Expected a positive number in: %s: %d.0",
									operator->mongoOperatorName,
									intVal),
								errhint(
									"Expected a positive number in: %s: %d.0",
									operator->mongoOperatorName,
									intVal)));
			}

			int dataLength = sizeof(intVal);
			WriteSetBitPositionArray((uint8_t *) &intVal, dataLength, &writer);
			break;
		}

		case BSON_TYPE_DOUBLE:
		{
			double doubleVal = BsonValueAsDouble(operatorDocValue);

			if (!IsDoubleAFixedInteger(doubleVal))
			{
				ereport(ERROR, (errcode(MongoFailedToParse),
								errmsg(
									"Expected an integer: %s: %s",
									operator->mongoOperatorName,
									BsonValueToJsonForLogging(
										operatorDocValue)),
								errhint(
									"Expected an integer: %s: %s",
									operator->mongoOperatorName,
									BsonTypeName(operatorDocValue->value_type))));
			}

			int intVal = BsonValueAsInt32(operatorDocValue);

			if (intVal < 0)
			{
				ereport(ERROR, (errcode(MongoFailedToParse), errmsg(
									"Expected a positive number in: %s: %d.0",
									operator->mongoOperatorName,
									intVal)));
			}

			int dataLength = sizeof(intVal);
			WriteSetBitPositionArray((uint8_t *) &intVal, dataLength, &writer);
			break;
		}

		case BSON_TYPE_INT32:
		{
			int intVal = operatorDocValue->value.v_int32;

			/* Negative integer is an incorrect input */
			if (intVal < 0)
			{
				ereport(ERROR, (errcode(MongoFailedToParse), errmsg(
									"Expected a positive number in: %s: %d.0",
									operator->mongoOperatorName,
									intVal)));
			}

			/* convert filter to set bit position array */
			int dataLength = sizeof(intVal);
			WriteSetBitPositionArray((uint8_t *) &intVal, dataLength, &writer);
			break;
		}

		case BSON_TYPE_BINARY:
		{
			/* reading base64 decoded string from operatorDocValue*/
			unsigned char *decodedData = operatorDocValue->value.v_binary.data;
			int decodeDataLength = operatorDocValue->value.v_binary.data_len;

			/* convert filter to set bit position array */
			WriteSetBitPositionArray(decodedData, decodeDataLength, &writer);
			break;
		}

		case BSON_TYPE_ARRAY:
		{
			/* Sort Set bit position array */
			isInputArrayAlreadySorted = SortAndWriteInt32BsonTypeArray(operatorDocValue,
																	   &writer,
																	   operator->
																	   mongoOperatorName);
			break;
		}

		default:
		{
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg(
								"%s takes an Array, a number, or a BinData but received: %s: \\%s\\",
								path,
								operator->mongoOperatorName,
								BsonValueToJsonForLogging(
									operatorDocValue)),
							errhint(
								"Path takes an Array, a number, or a BinData but received: %s: \\%s\\",
								operator->mongoOperatorName,
								BsonTypeName(
									operatorDocValue->value_type))));
		}
	}


	if (isInputArrayAlreadySorted)
	{
		qual = CreateFuncExprForSimpleQueryOperator(operatorDocIterator,
													context, operator,
													path);
	}
	else
	{
		bson_iter_t arrayIter;
		pgbson *setPositionArrayBson = PgbsonWriterGetPgbson(&writer);
		PgbsonInitIteratorAtPath(setPositionArrayBson, "", &arrayIter);
		qual = CreateFuncExprForSimpleQueryOperator(&arrayIter,
													context, operator,
													path);
	}
	return qual;
}


/*
 * CreateOpExprForQueryOperator returns an FuncExpr to perform comparison
 * defined by the query operator document.
 */
static Expr *
CreateFuncExprForQueryOperator(BsonQueryOperatorContext *context, const char *path,
							   const MongoQueryOperator *operator,
							   const bson_value_t *value)
{
	/* check if the operator requires an index for given path */
	if (context->requiredFilterPathNameHashSet != NULL)
	{
		bool found = false;
		StringView hashEntry = CreateStringViewFromString(path);
		hash_search(context->requiredFilterPathNameHashSet, &hashEntry, HASH_FIND,
					&found);

		if (!found)
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg(
								"The index for filter path '%s' was not found, please check whether the index is created.",
								path)));
		}
	}

	Expr *comparison = NULL;

	/* construct left and right side of the comparison */
	Const *constValue = CreateConstFromBsonValue(path, value, context->collationString);


	Oid functionOid = operator->postgresRuntimeFunctionOidLookup();
	if (!OidIsValid(functionOid))
	{
		ereport(ERROR, (errmsg("<bson> %s <bson> operator not defined",
							   operator->mongoOperatorName)));
	}

	constValue->consttype = operator->operandTypeOid();

	if (context->coerceOperatorExprIfApplicable &&
		operator->postgresRuntimeOperatorOidLookup != NULL)
	{
		/* First try to see if the Oid exists */
		Oid operatorOid = operator->postgresRuntimeOperatorOidLookup();
		if (OidIsValid(operatorOid))
		{
			OpExpr *opExpr = (OpExpr *) make_opclause(operatorOid, BOOLOID,
													  false,
													  context->documentExpr,
													  (Expr *) constValue,
													  InvalidOid, InvalidOid);
			opExpr->opfuncid = functionOid;
			return (Expr *) opExpr;
		}
	}

	List *args = list_make2(context->documentExpr, constValue);

	/* construct Func(document, <value>) expression */
	comparison = (Expr *) makeFuncExpr(functionOid, BOOLOID,
									   args, InvalidOid, InvalidOid,
									   COERCE_EXPLICIT_CALL);
	return comparison;
}


/*
 * CreateConstFromBsonValue returns a Const that mimics the output of bson_get_value.
 */
static Const *
CreateConstFromBsonValue(const char *path, const bson_value_t *value, const
						 char *collationString)
{
	/* convert value to BSON Datum */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendValue(&writer, path, strlen(path), value);

	if (IsCollationApplicable(collationString))
	{
		PgbsonWriterAppendUtf8(&writer, "collation", 9, collationString);
	}

	pgbson *bson = PgbsonWriterGetPgbson(&writer);

	Const *bsonValueDoc = makeNode(Const);
	bsonValueDoc->consttype = BsonTypeId();
	bsonValueDoc->consttypmod = -1;
	bsonValueDoc->constlen = -1;
	bsonValueDoc->constvalue = PointerGetDatum(bson);
	bsonValueDoc->constbyval = false;
	bsonValueDoc->constisnull = false;
	bsonValueDoc->location = -1;

	return bsonValueDoc;
}


/*
 * Creates Expression quals for $all : [...]. This also validates the array content for various scenarios and reports error accordingly.
 * All of the array elements could contain either simple elements or $elemMatch expressions (not both together).
 */
static Expr *
CreateExprForDollarAll(const char *path,
					   bson_iter_t *operatorDocIterator,
					   BsonQueryOperatorContext *context,
					   const MongoQueryOperator *operator)
{
	bson_iter_t arrayIterator;

	/* open array of elements (or documents in case of $all : [{$elemMatch : {}}...] ) and validate it. */
	bson_iter_recurse(operatorDocIterator, &arrayIterator);
	bool foundObject = false;
	bool foundElement = false;
	bool foundElemMatch = false;

	while (bson_iter_next(&arrayIterator))
	{
		if (bson_iter_type(&arrayIterator) != BSON_TYPE_DOCUMENT)
		{
			if (foundObject)
			{
				ereport(ERROR, (errcode(MongoBadValue), errmsg(
									"$all/$elemMatch has to be consistent")));
			}

			foundElement = true;
			continue;
		}

		bson_iter_t docIterator;
		bson_iter_recurse(&arrayIterator, &docIterator);

		/* if an empty document. Consider it as foundElement. */
		if (!bson_iter_next(&docIterator))
		{
			if (foundObject)
			{
				ereport(ERROR, (errcode(MongoBadValue), errmsg(
									"$all/$elemMatch has to be consistent")));
			}
			foundElement = true;
			continue;
		}

		const char *bsonKey = bson_iter_key(&docIterator);

		/* it is expression of form {path : value}, Consider it as foundElement. */
		if (bsonKey[0] != '$')
		{
			if (foundObject)
			{
				ereport(ERROR, (errcode(MongoBadValue), errmsg(
									"$all/$elemMatch has to be consistent")));
			}
			foundElement = true;
			continue;
		}

		const MongoQueryOperator *keyOp = GetMongoQueryOperatorByMongoOpName(bsonKey,
																			 context->
																			 inputType);
		MongoQueryOperatorType operatorType = keyOp->operatorType;

		if (operatorType == QUERY_OPERATOR_AND || operatorType == QUERY_OPERATOR_OR ||
			operatorType == QUERY_OPERATOR_NOR || operatorType == QUERY_OPERATOR_NOT)
		{
			if (foundObject)
			{
				ereport(ERROR, (errcode(MongoBadValue), errmsg(
									"$all/$elemMatch has to be consistent")));
			}
			foundElement = true;
			continue;
		}

		if (foundElement)
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"no $ expressions in $all")));
		}
		else if (foundElemMatch && keyOp->operatorType != QUERY_OPERATOR_ELEMMATCH)
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"$all/$elemMatch has to be consistent")));
		}
		else if (keyOp->operatorType != QUERY_OPERATOR_ELEMMATCH)
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"no $ expressions in $all")));
		}
		else if (keyOp->operatorType == QUERY_OPERATOR_ELEMMATCH)
		{
			foundElemMatch = true;
		}

		foundObject = true;
	}

	return CreateFuncExprForSimpleQueryOperator(operatorDocIterator,
												context, operator,
												path);
}


/*
 * CreateShardKeyFiltersForQuery returns a filter of the form:
 * shard_key_value = <hash of shard key fields>
 * if the list of quals restricts the shard key to a single value.
 *
 * We currently only extract a single shard key value based on top-level
 * filters and $and.
 */
Expr *
CreateShardKeyFiltersForQuery(const bson_value_t *queryDocument, pgbson *shardKeyBson,
							  uint64_t collectionId, Index collectionVarno)
{
	/* compute the hash of the shard key valeus */
	int64 shardKeyHash = 0;
	if (!ComputeShardKeyHashForQueryValue(shardKeyBson, collectionId, queryDocument,
										  &shardKeyHash))
	{
		/* not all shard key values are set */
		return NULL;
	}

	/* all shard key values are set, build the shard_key_value filter */

	/* create Const for the hash of shard key field values */
	Datum shardKeyFieldValuesHashDatum = Int64GetDatum(shardKeyHash);
	Const *shardKeyValueConst = makeConst(INT8OID, -1, InvalidOid, 8,
										  shardKeyFieldValuesHashDatum, false, true);

	/* construct document <operator> <value> expression */
	return CreateShardKeyValueFilter(collectionVarno, shardKeyValueConst);
}


/*
 * GetCollectionReferencedByDocumentVar finds the collection referenced by
 * documentExpr if it contains a single Var, or NULL if it not a single Var,
 * or the FROM clause entry is not a ApiSchema.collection call.
 */
static MongoCollection *
GetCollectionReferencedByDocumentVar(Expr *documentExpr,
									 Query *currentQuery,
									 Index *collectionVarno,
									 ParamListInfo boundParams)
{
	List *documentVars = pull_var_clause((Node *) documentExpr, 0);
	if (list_length(documentVars) != 1)
	{
		return NULL;
	}

	Var *documentVar = linitial(documentVars);

	/* find the FROM ApiSchema.collection(...) clause to which document refers */
	RangeTblEntry *rte = rt_fetch(documentVar->varno, currentQuery->rtable);
	if (!IsResolvableMongoCollectionBasedRTE(rte, boundParams))
	{
		return NULL;
	}

	if (collectionVarno != NULL)
	{
		*collectionVarno = documentVar->varno;
	}

	return GetCollectionForRTE(rte, boundParams);
}


/*
 * GetCollectionForRTE returns the MongoCollection metadata for a given
 * ApiSchema.collection(..) RTE.
 */
static MongoCollection *
GetCollectionForRTE(RangeTblEntry *rte, ParamListInfo boundParams)
{
	Assert(IsResolvableMongoCollectionBasedRTE(rte, boundParams));

	RangeTblFunction *rangeTableFunc = linitial(rte->functions);
	FuncExpr *funcExpr = (FuncExpr *) rangeTableFunc->funcexpr;
	Const *dbConst = GetConstParamValue((Node *) linitial(funcExpr->args),
										boundParams);
	Const *collectionConst = GetConstParamValue((Node *) lsecond(funcExpr->args),
												boundParams);
	Datum databaseNameDatum = dbConst->constvalue;
	Datum collectionNameDatum = collectionConst->constvalue;

	MongoCollection *collection =
		GetMongoCollectionByNameDatum(databaseNameDatum, collectionNameDatum,
									  AccessShareLock);

	return collection;
}


/*
 * CreateZeroShardKeyValueFilter creates a filter of the form shard_key_value = <collection_id>
 * for the given varno (read: rtable index).
 */
Expr *
CreateNonShardedShardKeyValueFilter(int collectionVarno, const
									MongoCollection *collection)
{
	/* reinterpret cast to int64_t. */
	int64_t shardKeyValue = *(int64_t *) &collection->collectionId;
	Const *nonShardedShardKeyConst = makeConst(INT8OID, -1, InvalidOid, 8,
											   Int64GetDatum(shardKeyValue), false, true);

	return CreateShardKeyValueFilter(collectionVarno, nonShardedShardKeyConst);
}


/*
 * CreateZeroShardKeyValueFilter creates a filter of the form shard_key_value = <value>
 * for the given varno (read: rtable index).
 */
static Expr *
CreateShardKeyValueFilter(int collectionVarno, Const *valueConst)
{
	/* shard_key_value is always the first column in our data tables */
	AttrNumber shardKeyAttNum = MONGO_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER;
	Var *shardKeyValueVar = makeVar(collectionVarno, shardKeyAttNum, INT8OID, -1,
									InvalidOid, 0);

	/* construct document <operator> <value> expression */
	Expr *shardKeyValueFilter = make_opclause(BigintEqualOperatorId(), BOOLOID, false,
											  (Expr *) shardKeyValueVar,
											  (Expr *) valueConst, InvalidOid,
											  InvalidOid);

	return shardKeyValueFilter;
}


/*
 * Creates a basic object_id <op> <value> op_expr for a given collection var.
 */
static Expr *
MakeSimpleIdExpr(const bson_value_t *filterValue, Index collectionVarno, Oid operatorId)
{
	pgbson *qualValue = BsonValueToDocumentPgbson(filterValue);
	Const *documentIdConst = makeConst(BsonTypeId(), -1, InvalidOid, -1,
									   PointerGetDatum(qualValue),
									   false, false);

	/* _id is always the second column in our data tables */
	AttrNumber documentIdAttnum = 2;
	Var *documentIdVar = makeVar(collectionVarno, documentIdAttnum, BsonTypeId(), -1,
								 InvalidOid, 0);

	/* construct object_id <operator> <value> expression */
	Expr *documentIdFilter = make_opclause(operatorId, BOOLOID, false,
										   (Expr *) documentIdVar,
										   (Expr *) documentIdConst, InvalidOid,
										   InvalidOid);
	return documentIdFilter;
}


/*
 * Given the binary arguments of a FuncExpr or OpExpr,
 * A specified Collection Var index in a RangeTable,
 * A mongo operator ($in, $eq, $gt etc), constructs an appropriate
 * Expr if one can be made for an _id filter. Creates a qual for the
 * object_id column in the table and adds it to the list idFilterQuals.
 * The modified list is returned to the caller.
 * e.g.
 * { "_id": { $in: [ 1, 2, 3 ]}} is converted to
 * object_id IN ( { "": 1}, { "": 2 }) etc.
 * Note that this only works for B-tree supported OpIds since the
 * (shard_key_value, object_id) index is a B-tree index.
 */
static void
CheckAndAddIdFilter(List *opArgs, IdFilterWalkerContext *context,
					const MongoIndexOperatorInfo *operator)
{
	Expr *firstArg = linitial(opArgs);
	Expr *secondArg = lsecond(opArgs);

	if (!IsA(firstArg, Var))
	{
		return;
	}

	/* Skip if the qual is not against the document column */
	Var *firstVar = (Var *) firstArg;
	if (firstVar->varattno != MONGO_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER ||
		((Index) firstVar->varno) != context->collectionVarno)
	{
		return;
	}

	if (!IsA(secondArg, Const))
	{
		return;
	}

	/* We know it's a filter on the document column */
	Const *secondConst = (Const *) secondArg;
	Assert(secondConst->consttype == BsonTypeId() ||
		   secondConst->consttype == BsonQueryTypeId());

	pgbson *qual = DatumGetPgBson(secondConst->constvalue);

	pgbsonelement qualElement;
	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(qual, &qualElement);

		/*  regex. _id can take Code, but Code is agnostic to collation
		 * _id can't be array accrording to mongo senmantics, but it can
		 * be an array in helio since we rewrite $or conditions on _id as $in. */
		if (qualElement.bsonValue.value_type == BSON_TYPE_UTF8 ||
			qualElement.bsonValue.value_type == BSON_TYPE_DOCUMENT ||
			qualElement.bsonValue.value_type == BSON_TYPE_ARRAY)
		{
			context->isCollationAware = true;
		}
	}
	else
	{
		PgbsonToSinglePgbsonElement(qual, &qualElement);
	}

	if (qualElement.pathLength == IdFieldStringView.length &&
		strncmp(qualElement.path, IdFieldStringView.string, IdFieldStringView.length) ==
		0)
	{
		switch (operator->indexStrategy)
		{
			case BSON_INDEX_STRATEGY_DOLLAR_EQUAL:
			{
				Expr *documentIdFilter = MakeSimpleIdExpr(&qualElement.bsonValue,
														  context->collectionVarno,
														  BsonEqualOperatorId());
				context->idQuals = lappend(context->idQuals, documentIdFilter);
				return;
			}

			case BSON_INDEX_STRATEGY_DOLLAR_GREATER:
			{
				Expr *documentIdFilter = MakeSimpleIdExpr(&qualElement.bsonValue,
														  context->collectionVarno,
														  BsonGreaterThanOperatorId());
				context->idQuals = lappend(context->idQuals, documentIdFilter);
				return;
			}

			case BSON_INDEX_STRATEGY_DOLLAR_LESS:
			{
				Expr *documentIdFilter = MakeSimpleIdExpr(&qualElement.bsonValue,
														  context->collectionVarno,
														  BsonLessThanOperatorId());
				context->idQuals = lappend(context->idQuals, documentIdFilter);
				return;
			}


			case BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL:
			{
				Expr *documentIdFilter = MakeSimpleIdExpr(&qualElement.bsonValue,
														  context->collectionVarno,
														  BsonGreaterThanEqualOperatorId());
				context->idQuals = lappend(context->idQuals, documentIdFilter);
				return;
			}

			case BSON_INDEX_STRATEGY_DOLLAR_LESS_EQUAL:
			{
				Expr *documentIdFilter = MakeSimpleIdExpr(&qualElement.bsonValue,
														  context->collectionVarno,
														  BsonLessThanEqualOperatorId());
				context->idQuals = lappend(context->idQuals, documentIdFilter);
				return;
			}

			case BSON_INDEX_STRATEGY_DOLLAR_IN:
			{
				if (qualElement.bsonValue.value_type != BSON_TYPE_ARRAY)
				{
					return;
				}

				List *inArgs = NIL;
				bson_iter_t inQualsIter;
				BsonValueInitIterator(&qualElement.bsonValue, &inQualsIter);


				/* Get the $in values */
				while (bson_iter_next(&inQualsIter))
				{
					inArgs = lappend(inArgs, MakeBsonConst(BsonValueToDocumentPgbson(
															   bson_iter_value(
																   &inQualsIter))));
				}

				if (inArgs != NIL)
				{
					/* Create an IN clause, in SQL this is
					 * a "ANY ( bson[] )" expression.
					 */
					ScalarArrayOpExpr *inOperator = makeNode(ScalarArrayOpExpr);
					inOperator->useOr = true;
					inOperator->opno = BsonEqualOperatorId();

					/* First arg is the object_id var */
					AttrNumber documentIdAttnum = 2;
					Var *documentIdVar = makeVar(context->collectionVarno,
												 documentIdAttnum,
												 BsonTypeId(), -1,
												 InvalidOid, 0);

					/* Second arg is an ArrayExpr containing the documents */
					ArrayExpr *arrayExpr = makeNode(ArrayExpr);
					arrayExpr->array_typeid = GetBsonArrayTypeOid();
					arrayExpr->element_typeid = BsonTypeId();
					arrayExpr->multidims = false;
					arrayExpr->elements = inArgs;
					inOperator->args = list_make2(documentIdVar, arrayExpr);

					context->idQuals = lappend(context->idQuals, inOperator);
				}

				return;
			}

			default:
			{
				return;
			}
		}
	}
}


/*
 * Visitor used by CreateIdFilterForQuery when traversing a given query's
 * qualifiers to extract object_id filters.
 */
static bool
VisitIdFilterExpression(Node *node, IdFilterWalkerContext *context)
{
	CHECK_FOR_INTERRUPTS();

	if (node == NULL)
	{
		return false;
	}

	if (IsA(node, BoolExpr))
	{
		BoolExpr *boolExpr = (BoolExpr *) node;
		if (boolExpr->boolop != AND_EXPR)
		{
			/* Stop traversing on anything but AND */
			return false;
		}

		return expression_tree_walker(node, VisitIdFilterExpression, context);
	}
	else if (IsA(node, OpExpr))
	{
		OpExpr *opExpr = (OpExpr *) node;
		if (opExpr->opfuncid != InvalidOid && list_length(opExpr->args) == 2)
		{
			const MongoIndexOperatorInfo *indexOp =
				GetMongoIndexOperatorInfoByPostgresFuncId(opExpr->opfuncid);
			if (indexOp->indexStrategy != BSON_INDEX_STRATEGY_INVALID)
			{
				CheckAndAddIdFilter(opExpr->args, context, indexOp);
			}
		}

		return false;
	}
	else if (IsA(node, FuncExpr))
	{
		FuncExpr *funcExpr = (FuncExpr *) node;
		const MongoIndexOperatorInfo *indexOp =
			GetMongoIndexOperatorInfoByPostgresFuncId(funcExpr->funcid);
		if (indexOp->indexStrategy != BSON_INDEX_STRATEGY_INVALID &&
			list_length(funcExpr->args) == 2)
		{
			CheckAndAddIdFilter(funcExpr->args, context, indexOp);
		}

		return false;
	}
	else if (IsA(node, List))
	{
		return expression_tree_walker(node, VisitIdFilterExpression, context);
	}
	else
	{
		/* Don't try to handle any other type of clause (better safe than)
		 * extracting ID filters where we don't know.
		 */
		return false;
	}
}


/*
 * CreateIdFilterForQuery creates an _id = <documentIdValue> filter to include
 * in a query such that we can utilize the primary key index.
 */
Expr *
CreateIdFilterForQuery(List *existingQuals,
					   Index collectionVarno, bool *isCollationAware)
{
	IdFilterWalkerContext walkerContext = { 0 };
	walkerContext.idQuals = NIL;
	walkerContext.collectionVarno = collectionVarno;
	expression_tree_walker((Node *) existingQuals, VisitIdFilterExpression,
						   &walkerContext);

	*isCollationAware = walkerContext.isCollationAware;
	if (walkerContext.idQuals == NIL)
	{
		return NULL;
	}

	return make_ands_explicit(walkerContext.idQuals);
}


/*
 * Validates the Orderby expression and returns whether or not
 * the order by is ascending.
 */
bool
ValidateOrderbyExpressionAndGetIsAscending(pgbson *orderby)
{
	pgbsonelement orderingElement;
	if (!TryGetSinglePgbsonElementFromPgbson(orderby,
											 &orderingElement))
	{
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg(
							"Multi-column order not supported yet")));
	}

	/* Validate field name (Copied from the GW )*/
	if (orderingElement.pathLength == 0 ||
		orderingElement.path[0] == '.' ||
		orderingElement.path[orderingElement.pathLength - 1] == '.' ||
		strstr(orderingElement.path, "..") != NULL)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("Bad sort specification")));
	}

	/* Try to see if it's an order by $meta */
	if (TryCheckMetaScoreOrderBy(&orderingElement.bsonValue))
	{
		/*
		 * It's a search associated with $text to be dealt with
		 * The runtime function will evaluate the $meta.
		 */
		return false;
	}

	if (!BsonValueIsNumber(&orderingElement.bsonValue))
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("Invalid sort direction %s",
							   BsonValueToJsonForLogging(
								   &orderingElement.bsonValue))));
	}

	int64_t sortOrder = BsonValueAsInt64(&orderingElement.bsonValue);
	if (sortOrder != 1 && sortOrder != -1)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("Invalid sort direction %s",
							   BsonValueToJsonForLogging(
								   &orderingElement.bsonValue))));
	}

	return sortOrder == 1;
}


/*
 * Create function expression for $regex after populating
 * the provided options for $regex.
 * This is specifically needed when we run the query from
 * from the mongo client where $regex and $options comes
 * as two different operators even though $options is not
 * a mongo operator. Here we fetch the value of $options
 * and put in the bson_value_t of $regex iterator */
static Expr *
CreateFuncExprForRegexOperator(const bson_value_t *options, const
							   bson_value_t *regexBsonValue,
							   BsonQueryOperatorContext *context,
							   const MongoQueryOperator *operator,
							   const char *path)
{
	ValidateRegexArgument(regexBsonValue);
	bson_value_t regexInputValue;

	if (options != NULL)
	{
		ValidateOptionsArgument(options);
		regexInputValue.value_type = BSON_TYPE_REGEX;
		regexInputValue.value.v_regex.regex = pstrdup(
			regexBsonValue->value.v_regex.regex);
		regexInputValue.value.v_regex.options = options->value.v_utf8.str;

		regexBsonValue = &regexInputValue;
	}

	/* Just need to call RegexCompile here to validate regex pattern.
	 * hence don't need options here */
	RegexCompileDuringPlanning(regexBsonValue->value.v_regex.regex, NULL);

	return CreateFuncExprForQueryOperator(
		context, path,
		operator, regexBsonValue);
}


/*
 * Creates expression for
 * {"path" : { "$regex": <pattern>, "$options" : ""} }.
 */
static Expr *
CreateExprForDollarRegex(bson_iter_t *currIter, bson_value_t **options,
						 BsonQueryOperatorContext *context,
						 const MongoQueryOperator *operator,
						 const char *path)
{
	const bson_value_t *regexBsonValue = bson_iter_value(currIter);
	bson_iter_t optionsIter = *currIter;

	/* This case occurs if $options is given ahead of $regex in the spec */
	if (*options != NULL)
	{
		if (regexBsonValue->value_type == BSON_TYPE_REGEX &&
			strlen(regexBsonValue->value.v_regex.options) != 0)
		{
			ereport(ERROR, (errcode(MongoLocation51074), errmsg(
								"options set in both $regex and $options")));
		}

		Expr *qual = CreateFuncExprForRegexOperator(*options, regexBsonValue, context,
													operator, path);
		pfree(*options);
		*options = NULL;
		return qual;
	}

	/*
	 * $regex is found. Now look for $options, either immediately following
	 * it or after another possible document iter entry. eg:
	 * t.find({ description: { "$regex": " line ",
	 *                         "$eq": "Single line description.",
	 *                         "$options": "i"
	 *                       }
	 *        });
	 */
	while (bson_iter_next(&optionsIter))
	{
		if (strcmp(bson_iter_key(&optionsIter), "$options") == 0)
		{
			if (regexBsonValue->value_type == BSON_TYPE_REGEX &&
				strlen(regexBsonValue->value.v_regex.options) != 0)
			{
				ereport(ERROR, (errcode(MongoLocation51075), errmsg(
									"options set in both $regex and $options")));
			}

			bson_value_t *optionsBsonVal = (bson_value_t *) bson_iter_value(&optionsIter);
			Expr *qual = CreateFuncExprForRegexOperator(optionsBsonVal, regexBsonValue,
														context, operator, path);
			return qual;
		}
	}

	return CreateFuncExprForRegexOperator(*options, regexBsonValue,
										  context, operator, path);
}


/*
 * Creates expression for
 * {"path" : { "$mod": [ divisor, remainder ]} }.
 */
static Expr *
CreateExprForDollarMod(bson_iter_t *operatorDocIterator,
					   BsonQueryOperatorContext *context,
					   const MongoQueryOperator *operator,
					   const char *path)
{
	if (!BSON_ITER_HOLDS_ARRAY(operatorDocIterator))
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"malformed mod, needs to be an array")));
	}

	const bson_value_t *sourceArray = bson_iter_value(operatorDocIterator);
	bson_iter_t arrayIter;
	BsonValueInitIterator(sourceArray, &arrayIter);

	uint8_t numElements = 0;
	while (bson_iter_next(&arrayIter))
	{
		numElements++;
		if (numElements == 3)
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"malformed mod, too many elements")));
		}
		if (!BSON_ITER_HOLDS_NUMBER(&arrayIter) &&
			!BSON_ITER_HOLDS_DECIMAL128(&arrayIter))
		{
			if (numElements == 1)
			{
				ereport(ERROR, (errcode(MongoBadValue), errmsg(
									"malformed mod, divisor not a number")));
			}
			else
			{
				ereport(ERROR, (errcode(MongoBadValue), errmsg(
									"malformed mod, remainder not a number")));
			}
		}
		const bson_value_t *arrayVal = bson_iter_value(&arrayIter);
		if (arrayVal->value_type == BSON_TYPE_DECIMAL128 ||
			arrayVal->value_type == BSON_TYPE_DOUBLE)
		{
			bson_value_t dec128Val;
			dec128Val.value_type = BSON_TYPE_DECIMAL128;
			dec128Val.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(arrayVal);

			if (!IsDecimal128Finite(&dec128Val))
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg(numElements == 1 ?
									   "malformed mod, divisor value is invalid :: caused by :: Unable to coerce NaN/Inf to integral type"
									   :
									   "malformed mod, remainder value is invalid :: caused by :: Unable to coerce NaN/Inf to integral type")));
			}
			if (!IsDecimal128InInt64Range(&dec128Val))
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg(numElements == 1 ?
									   "malformed mod, divisor value is invalid :: caused by :: Out of bounds coercing to integral value"
									   :
									   "malformed mod, remainder value is invalid :: caused by :: Out of bounds coercing to integral value")));
			}
		}

		if (numElements == 1)
		{
			int64_t divisor = BsonValueAsInt64(arrayVal);
			if (divisor == 0)
			{
				ereport(ERROR, (errcode(MongoBadValue), errmsg(
									"divisor cannot be 0")));
			}
		}
	}

	if (numElements < 2)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"malformed mod, not enough elements")));
	}

	return CreateFuncExprForSimpleQueryOperator(operatorDocIterator,
												context, operator,
												path);
}


/*
 * EvaluateBoundParameters evaluates an expression based on the bound parameters specified,
 * expands the resulting values and returns the evaluated operator expression.
 */
Node *
EvaluateBoundParameters(Node *expression, ParamListInfo boundParams)
{
	/*
	 * evaluate constant expressions (e.g. casts)
	 */
	PlannerInfo *planner = NULL;
	if (boundParams != NULL)
	{
		/* Set up largely-dummy planner state */
		Query *query = makeNode(Query);
		query->commandType = CMD_SELECT;

		PlannerGlobal *glob = makeNode(PlannerGlobal);
		glob->boundParams = boundParams;

		planner = makeNode(PlannerInfo);
		planner->parse = query;
		planner->glob = glob;
		planner->query_level = 1;
		planner->planner_cxt = CurrentMemoryContext;
		planner->wt_param_id = -1;
	}

	return eval_const_expressions(planner, expression);
}


/*
 * comparator of qsort algorithm for sort in ascending order
 */
static inline int
qSortAscendingComparator(const void *p1, const void *p2)
{
	return (*(int *) p1 - *(int *) p2);
}


/*
 * this function sorts BSON_TYPE_ARRAY in ascending order only if it not already sorted.
 * if input array is already sorted return true and if it is not sorted apply qsort to sort it and return false.
 * traverse input array and create c array, apply stdlib qsort function and write sorted array to input writer.
 * Note: input array elements should be type of positive int32 otherwise will throw an error.
 * @array : input bson array
 * @writer : writer to write new sorted array
 * @opName : Operator name like $bitsAllClear, $bitsAnyClear (used in error messages)
 */
static bool
SortAndWriteInt32BsonTypeArray(const bson_value_t *bsonArray, pgbson_writer *writer, const
							   char *opName)
{
	bson_iter_t arrayIter;
	BsonValueInitIterator(bsonArray, &arrayIter);
	int arrayLength = 0;
	int prevValue = -1;
	bool isSorted = true;
	bool checkFixedInteger = false;
	while (bson_iter_next(&arrayIter))
	{
		const bson_value_t *element = bson_iter_value(&arrayIter);

		if (!IsBsonValue32BitInteger(element, checkFixedInteger))
		{
			switch (element->value_type)
			{
				case BSON_TYPE_DOUBLE:
				{
					ereport(ERROR, (errcode(MongoFailedToParse),
									errmsg(
										"Expected an integer: %s: %s", opName,
										BsonValueToJsonForLogging(element)),
									errhint(
										"Expected an integer in operator: %s, found:%s",
										opName,
										BsonTypeName(element->value_type))));
					break;
				}

				case BSON_TYPE_DECIMAL128:
				case BSON_TYPE_INT64:
				{
					ereport(ERROR, (errcode(MongoFailedToParse),
									errmsg(
										"bit positions cannot be represented as a 32-bit signed integer: %s.0",
										BsonValueToJsonForLogging(element)),
									errhint(
										"bit positions of type %s cannot be represented as a 32-bit signed integer",
										BsonTypeName(element->value_type))));
					break;
				}

				default:
				{
					ereport(ERROR, (errcode(MongoFailedToParse),
									errmsg(
										"bit positions must be an integer but got: %d: \\%s\\",
										arrayLength, BsonValueToJsonForLogging(element)),
									errhint(
										"bit positions must be an integer but got: \\%s\\ arrayLength: %d",
										BsonTypeName(element->value_type), arrayLength)));
				}
			}
		}


		int elementValue = BsonValueAsInt32(element);
		if (prevValue > elementValue && isSorted)
		{
			isSorted = false;
		}

		if (elementValue < 0)
		{
			ereport(ERROR, (errcode(MongoFailedToParse), errmsg(
								"bit positions must be >= 0 but got: %d: \\%s\\",
								arrayLength, BsonValueToJsonForLogging(element))));
		}
		prevValue = elementValue;
		arrayLength++;
	}

	if (isSorted)
	{
		return true;
	}

	BsonValueInitIterator(bsonArray, &arrayIter);
	int *cArray = (int *) palloc(sizeof(int) * arrayLength);
	int currIndex = 0;

	while (bson_iter_next(&arrayIter))
	{
		const bson_value_t *leftIdKey = bson_iter_value(&arrayIter);
		cArray[currIndex++] = BsonValueAsInt32(leftIdKey);
	}

	qsort(cArray, arrayLength, sizeof(int), qSortAscendingComparator);

	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(writer, "", 0, &arrayWriter);

	for (int i = 0; i < arrayLength; i++)
	{
		bson_value_t elementValue;
		elementValue.value_type = BSON_TYPE_INT32;
		elementValue.value.v_int32 = cArray[i];
		PgbsonArrayWriterWriteValue(&arrayWriter, &elementValue);
	}

	pfree(cArray);
	PgbsonWriterEndArray(writer, &arrayWriter);

	return false;
}


/*
 * Check for null byte and argument type in a given options argument.
 */
static void
ValidateOptionsArgument(const bson_value_t *argBsonValue)
{
	if (argBsonValue->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"$options has to be a string")));
	}

	/* Only options which mongo supports are i, m, x, s, u*/
	char *valueString = argBsonValue->value.v_utf8.str;
	for (uint32_t index = 0; index < argBsonValue->value.v_utf8.len; index++)
	{
		if (valueString[index] == 'i' || valueString[index] == 'm' ||
			valueString[index] == 'x' || valueString[index] == 's' ||
			valueString[index] == 'u')
		{
			continue;
		}
		else
		{
			ThrowInvalidRegexOptions(valueString[index]);
		}
	}
}


/*
 * Check for null byte and argument type in a given regex argument.
 */
static void
ValidateRegexArgument(const bson_value_t *argBsonValue)
{
	if (argBsonValue->value_type != BSON_TYPE_UTF8 &&
		argBsonValue->value_type != BSON_TYPE_REGEX)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"$regex has to be a string")));
	}

	/* Validate options when input type is BSON_TYPE_REGEX. */
	/* This is different from options validation as we support less flags when bson type is regex as per mongo behaviour */
	if (argBsonValue->value_type == BSON_TYPE_REGEX)
	{
		char *optionStr = argBsonValue->value.v_regex.options;
		int lenOptionStr = strlen(optionStr);
		for (int index = 0; index < lenOptionStr; index++)
		{
			if (optionStr[index] == 'i' || optionStr[index] == 'm' ||
				optionStr[index] == 'u')
			{
				continue;
			}
			else
			{
				ThrowInvalidRegexOptions(optionStr[index]);
			}
		}
	}

	if (argBsonValue->value_type == BSON_TYPE_UTF8 &&
		strlen(argBsonValue->value.v_utf8.str) < argBsonValue->value.v_utf8.len)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"Regular expression cannot contain an embedded null byte")));
	}
}


static Expr *
TryProcessOrIntoDollarIn(BsonQueryOperatorContext *context,
						 List *orQuals)
{
	const MongoQueryOperator *op = GetMongoQueryOperatorByQueryOperatorType(
		QUERY_OPERATOR_EQ, context->inputType);

	/* First pass - validate all paths are the same and the $eq operator */
	StringView singlePath = { .length = 0, .string = "" };
	Expr *firstArg = NULL;
	ListCell *cell;
	pgbson_writer inValueWriter;
	PgbsonWriterInit(&inValueWriter);

	pgbson_element_writer elementWriter;
	PgbsonInitObjectElementWriter(&inValueWriter, &elementWriter, "", 0);

	pgbson_array_writer arrayWriter;
	PgbsonElementWriterStartArray(&elementWriter, &arrayWriter);
	foreach(cell, orQuals)
	{
		Expr *currentExpr = lfirst(cell);
		if (IsA(currentExpr, BoolExpr))
		{
			BoolExpr *boolExpr = (BoolExpr *) currentExpr;
			if (boolExpr->boolop == AND_EXPR &&
				list_length(boolExpr->args) == 1)
			{
				/* Flatten single $and */
				currentExpr = linitial(boolExpr->args);
			}
		}

		Oid funcId;
		List *args;
		if (IsA(currentExpr, FuncExpr))
		{
			FuncExpr *expr = (FuncExpr *) currentExpr;
			funcId = expr->funcid;
			args = expr->args;
		}
		else if (IsA(currentExpr, OpExpr))
		{
			OpExpr *opExpr = (OpExpr *) currentExpr;
			funcId = opExpr->opfuncid;
			args = opExpr->args;
		}
		else
		{
			return NULL;
		}

		/* Not a $eq - cannot convert to $in */
		if (funcId != op->postgresRuntimeFunctionOidLookup())
		{
			return NULL;
		}

		Expr *currentFirstArg = linitial(args);
		Expr *secondArg = lsecond(args);

		if (firstArg == NULL)
		{
			firstArg = currentFirstArg;
		}
		else if (!equal(firstArg, currentFirstArg))
		{
			return NULL;
		}

		/* not a const, cannot convert to $in */
		if (!IsA(secondArg, Const))
		{
			return NULL;
		}

		Const *argConst = (Const *) secondArg;
		if (argConst->constisnull)
		{
			return NULL;
		}

		pgbson *value = DatumGetPgBson(argConst->constvalue);
		pgbsonelement valueElement;
		PgbsonToSinglePgbsonElement(value, &valueElement); /* TODO: collation support */

		StringView currentPath = {
			.length = valueElement.pathLength, .string = valueElement.path
		};
		if (singlePath.length == 0)
		{
			singlePath = currentPath;
		}
		else if (!StringViewEquals(&singlePath, &currentPath))
		{
			/* $or with different paths - cannot convert to $in */
			return NULL;
		}

		PgbsonArrayWriterWriteValue(&arrayWriter, &valueElement.bsonValue);
	}

	/* Pass 2: At this point all quals are $eq with the same path */
	if (firstArg == NULL || singlePath.length == 0)
	{
		return NULL;
	}

	PgbsonElementWriterEndArray(&elementWriter, &arrayWriter);

	bson_value_t inValue = PgbsonElementWriterGetValue(&elementWriter);

	const MongoQueryOperator *inOperator =
		GetMongoQueryOperatorByQueryOperatorType(QUERY_OPERATOR_IN, context->inputType);
	return CreateFuncExprForQueryOperator(context, singlePath.string, inOperator,
										  &inValue);
}


/*
 * Optimize for common expression scenarios for $or
 * Currently handled optimizations:
 * { PATH: { $exists: false } } OR { PATH: { $eq: null } } -> { $eq: null }
 */
static Expr *
TryOptimizeDollarOrExpr(BsonQueryOperatorContext *context,
						List *orQuals)
{
	if (list_length(orQuals) == 1)
	{
		/* $or of a single qual is the qual */
		return (Expr *) linitial(orQuals);
	}

	/* For now, only optimize when $or has 2 entries */
	if (list_length(orQuals) != 2)
	{
		return NULL;
	}

	const MongoQueryOperator *eqop = GetMongoQueryOperatorByQueryOperatorType(
		QUERY_OPERATOR_EQ, context->inputType);
	const MongoQueryOperator *existsop = GetMongoQueryOperatorByQueryOperatorType(
		QUERY_OPERATOR_EXISTS, context->inputType);

	Expr *firstArg = NULL;
	Expr *equalsNullExpr = NULL;
	StringView singlePathEqualsNull = { 0 };
	StringView singlePathExistsFalse = { 0 };
	ListCell *cell;
	foreach(cell, orQuals)
	{
		Expr *currentExpr = lfirst(cell);

		Oid funcId;
		List *args;
		if (IsA(currentExpr, FuncExpr))
		{
			FuncExpr *expr = (FuncExpr *) currentExpr;
			funcId = expr->funcid;
			args = expr->args;
		}
		else if (IsA(currentExpr, OpExpr))
		{
			OpExpr *opExpr = (OpExpr *) currentExpr;
			funcId = opExpr->opfuncid;
			args = opExpr->args;
		}
		else
		{
			return NULL;
		}

		if (funcId != eqop->postgresRuntimeFunctionOidLookup() &&
			funcId != existsop->postgresRuntimeFunctionOidLookup())
		{
			continue;
		}

		Expr *currentFirstArg = linitial(args);
		Expr *secondArg = lsecond(args);

		if (firstArg == NULL)
		{
			firstArg = currentFirstArg;
		}
		else if (!equal(firstArg, currentFirstArg))
		{
			return NULL;
		}

		/* not a const, cannot optimize */
		if (!IsA(secondArg, Const))
		{
			return NULL;
		}

		Const *argConst = (Const *) secondArg;
		if (argConst->constisnull)
		{
			return NULL;
		}

		pgbson *value = DatumGetPgBson(argConst->constvalue);
		pgbsonelement valueElement;

		if (EnableCollation)
		{
			/* Since, we are optimizing Or expression not br rewriting a the expression, but by getting rid of
			 * redundant quals from the Quals list, we can ignore the collation*/
			PgbsonToSinglePgbsonElementWithCollation(value, &valueElement);
		}
		else
		{
			PgbsonToSinglePgbsonElement(value, &valueElement);
		}

		if (funcId == eqop->postgresRuntimeFunctionOidLookup())
		{
			/* Ignore all except { $eq: null } */
			if (valueElement.bsonValue.value_type != BSON_TYPE_NULL)
			{
				continue;
			}

			StringView currentPath = {
				.length = valueElement.pathLength, .string = valueElement.path
			};
			if (singlePathEqualsNull.length == 0)
			{
				singlePathEqualsNull = currentPath;
			}
			else if (!StringViewEquals(&singlePathEqualsNull, &currentPath))
			{
				/* $or with different paths - cannot currently optimize */
				return NULL;
			}

			equalsNullExpr = currentExpr;
		}
		else if (funcId == existsop->postgresRuntimeFunctionOidLookup())
		{
			if (BsonValueAsBool(&valueElement.bsonValue))
			{
				/* Exists true scenarios can be ignored */
				continue;
			}

			StringView currentPath = {
				.length = valueElement.pathLength, .string = valueElement.path
			};
			if (singlePathExistsFalse.length == 0)
			{
				singlePathExistsFalse = currentPath;
			}
			else if (!StringViewEquals(&singlePathExistsFalse, &currentPath))
			{
				/* $or with different paths - cannot currently optimize */
				return NULL;
			}
		}
	}

	if (singlePathExistsFalse.length == 0 || singlePathEqualsNull.length == 0)
	{
		/* Both aren't present can bail */
		return NULL;
	}

	if (!StringViewEquals(&singlePathExistsFalse, &singlePathEqualsNull))
	{
		/* Not the same path */
		return NULL;
	}

	/* Here we have $eq: null OR $exists: false
	 * return just $eq: null if it is not null.
	 * This is because $eq: null is a superset of $exists: false
	 * and can enable better index optimizations.
	 */
	return equalsNullExpr;
}


/*
 * WithIndexSupportExpression converts the document <geoOperator> query to
 * bson_validate_geometry(document, 'path') <geoOperator> query expression
 * so that this can be matched against Geospatial indexes.
 */
static Expr *
WithIndexSupportExpression(Expr *docExpr, Expr *geoOperatorExpr,
						   const char *path, bool isSpherical)
{
	FuncExpr *geoOperatorFuncExpr = (FuncExpr *) geoOperatorExpr;

	Const *pathConst = makeConst(TEXTOID, -1, InvalidOid, -1, CStringGetTextDatum(path),
								 false, false);

	Oid bsonValidateFunctionId = isSpherical ? BsonValidateGeographyFunctionId() :
								 BsonValidateGeometryFunctionId();
	Oid typeId = isSpherical ? GeographyTypeId() : GeometryTypeId();
	Expr *validateExpr = (Expr *) makeFuncExpr(bsonValidateFunctionId,
											   typeId,
											   list_make2(docExpr,
														  pathConst),
											   InvalidOid,
											   InvalidOid,
											   COERCE_EXPLICIT_CALL);
	List *argsList = list_make2(validateExpr, lsecond(geoOperatorFuncExpr->args));
	geoOperatorFuncExpr->args = argsList;
	return (Expr *) geoOperatorFuncExpr;
}


/*
 * Tries to get the negator for a given query operator if one is available.
 */
static const MongoQueryOperator *
GetNegationOperatorForQueryOperator(const MongoQueryOperator *queryOperator,
									Datum filterValue,
									BsonQueryOperatorContext *context)
{
	/* See if we can convert the Expr into an equivalent NOT version */
	switch (queryOperator->operatorType)
	{
		case QUERY_OPERATOR_EQ:
		{
			return GetMongoQueryOperatorByQueryOperatorType(QUERY_OPERATOR_NE,
															context->inputType);
		}

		case QUERY_OPERATOR_IN:
		{
			return GetMongoQueryOperatorByQueryOperatorType(QUERY_OPERATOR_NIN,
															context->inputType);
		}

		case QUERY_OPERATOR_NE:
		{
			return GetMongoQueryOperatorByQueryOperatorType(QUERY_OPERATOR_EQ,
															context->inputType);
		}

		case QUERY_OPERATOR_GT:
		{
			return GetMongoQueryOperatorByQueryOperatorType(QUERY_OPERATOR_NOT_GT,
															context->inputType);
		}

		case QUERY_OPERATOR_GTE:
		{
			pgbson *filterbson = DatumGetPgBsonPacked(filterValue);
			pgbsonelement greaterElement;
			PgbsonToSinglePgbsonElement(filterbson, &greaterElement);
			if (greaterElement.bsonValue.value_type == BSON_TYPE_MINKEY)
			{
				/* This is the { exists: true } query - don't optimize this */
				return NULL;
			}

			return GetMongoQueryOperatorByQueryOperatorType(QUERY_OPERATOR_NOT_GTE,
															context->inputType);
		}

		case QUERY_OPERATOR_LT:
		{
			return GetMongoQueryOperatorByQueryOperatorType(QUERY_OPERATOR_NOT_LT,
															context->inputType);
		}

		case QUERY_OPERATOR_LTE:
		{
			pgbson *filterbson = DatumGetPgBsonPacked(filterValue);
			pgbsonelement greaterElement;
			PgbsonToSinglePgbsonElement(filterbson, &greaterElement);
			if (greaterElement.bsonValue.value_type == BSON_TYPE_MAXKEY)
			{
				/* This is a cross-type comparison query - don't optimize this */
				return NULL;
			}

			return GetMongoQueryOperatorByQueryOperatorType(QUERY_OPERATOR_NOT_LTE,
															context->inputType);
		}

		default:
		{
			return NULL;
		}
	}
}


/*
 * Tries to optimize an expression that is in a $not operator to see if it
 * can be pushed into a child context.
 */
static Expr *
TryOptimizeNotInnerExpr(Expr *innerExpr, BsonQueryOperatorContext *context)
{
	if (context->inputType != MongoQueryOperatorInputType_Bson ||
		!context->simplifyOperators)
	{
		return NULL;
	}

	const MongoQueryOperator *queryOperator = NULL;
	List *args = NIL;
	if (IsA(innerExpr, FuncExpr))
	{
		FuncExpr *funcExpr = (FuncExpr *) innerExpr;
		queryOperator = GetMongoQueryOperatorByPostgresFuncId(funcExpr->funcid);
		args = funcExpr->args;
	}
	else if (IsA(innerExpr, OpExpr))
	{
		OpExpr *opExpr = (OpExpr *) innerExpr;
		queryOperator = GetMongoQueryOperatorByPostgresFuncId(opExpr->opfuncid);
		args = opExpr->args;
	}

	if (queryOperator == NULL ||
		queryOperator->operatorType == QUERY_OPERATOR_UNKNOWN ||
		list_length(args) != 2)
	{
		return NULL;
	}

	Node *second = lsecond(args);
	if (!IsA(second, Const))
	{
		return NULL;
	}

	Const *secondConst = (Const *) second;

	const MongoQueryOperator *negator = GetNegationOperatorForQueryOperator(queryOperator,
																			secondConst->
																			constvalue,
																			context);
	if (negator == NULL || negator->operatorType == QUERY_OPERATOR_UNKNOWN)
	{
		return NULL;
	}

	Oid negatorFunc = negator->postgresRuntimeFunctionOidLookup();

	if (negatorFunc == InvalidOid)
	{
		return NULL;
	}

	secondConst->consttype = negator->operandTypeOid();
	return (Expr *) makeFuncExpr(negatorFunc, BOOLOID,
								 args, InvalidOid, InvalidOid,
								 COERCE_EXPLICIT_CALL);
}


/* Create quals for $near, $nearSphere and $geoNear. */
static Expr *
ParseBsonValueForNearAndCreateOpExpr(bson_iter_t *operatorDocIterator,
									 BsonQueryOperatorContext *context, const char *path,
									 const char *mongoOperatorName)
{
	const pgbson *queryDoc = GetGeonearSpecFromNearQuery(operatorDocIterator, path,
														 mongoOperatorName);

	/* Check if this is not the 1st $near or $nearSphere occurrence in same query. */
	if (context->targetEntries)
	{
		bool isGeonear = TargetListContainsGeonearOp(context->targetEntries);

		if (isGeonear)
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("Too many geoNear expressions")));
		}
	}

	/*
	 * In mongo there are different unsupported errors,
	 * but we are throwing a generalized single error
	 * that is thrown by mongo in a majority of contexts.
	 */
	if (context->inputType == MongoQueryOperatorInputType_BsonValue)
	{
		ereport(ERROR,
				(errcode(MongoLocation5626500),
				 errmsg(
					 "$geoNear, $near, and $nearSphere are not allowed in this context, "
					 "as these operators require sorting geospatial data. If you do not need sort, "
					 "consider using $geoWithin instead.")));
	}

	GeonearRequest *request = ParseGeonearRequest(queryDoc);
	Expr *docExpr = context->documentExpr;

	TargetEntry *sortTargetEntry;
	SortGroupClause *sortGroupClause;
	List *quals = CreateExprForGeonearAndNearSphere(queryDoc, docExpr,
													request, &sortTargetEntry,
													&sortGroupClause);

	context->targetEntries = lappend(context->targetEntries, sortTargetEntry);
	context->sortClauses = lappend(context->sortClauses, sortGroupClause);

	return make_ands_explicit(quals);
}


/*
 * Updates the missing `resno` and `sortgroupref` fields for sortclause
 * based on the existing `query` structure
 */
void
UpdateQueryOperatorContextSortList(Query *query, List *sortClauses,
								   List *targetEntries)
{
	if (!sortClauses || !targetEntries)
	{
		return;
	}

	Assert(list_length(sortClauses) == list_length(targetEntries));

	ParseState *pstate = make_parsestate(NULL);
	pstate->p_next_resno = list_length(query->targetList) + 1;
	pstate->p_expr_kind = EXPR_KIND_ORDER_BY;

	ListCell *targetCell = NULL;
	ListCell *sortCell = NULL;
	forboth(targetCell, targetEntries, sortCell, sortClauses)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(targetCell);
		SortGroupClause *sortClause = (SortGroupClause *) lfirst(sortCell);

		/* update tle resno and ressortgroupref */
		tle->resno = pstate->p_next_resno++;
		sortClause->tleSortGroupRef = assignSortGroupRef(tle, query->targetList);

		query->sortClause = lappend(query->sortClause, sortClause);
		query->targetList = lappend(query->targetList, tle);
	}

	free_parsestate(pstate);
}
