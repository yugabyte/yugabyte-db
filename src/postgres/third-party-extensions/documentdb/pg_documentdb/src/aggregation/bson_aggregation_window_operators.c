/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/aggregation/bson_aggregation_window_operators.c
 *
 * Support functions for all the window operators used by $setWindowFields
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <math.h>
#include <common/int128.h>
#include <catalog/pg_type.h>
#include <nodes/makefuncs.h>
#include <parser/parse_clause.h>
#include <parser/parse_node.h>
#include <optimizer/optimizer.h>
#include <utils/builtins.h>
#include <utils/fmgroids.h>
#include <utils/datetime.h>
#include <utils/array.h>

#include "aggregation/bson_aggregation_window_operators.h"
#include "aggregation/bson_aggregation_statistics.h"
#include "commands/parse_error.h"
#include "operators/bson_expression_operators.h"
#include "query/bson_compare.h"
#include "query/query_operator.h"
#include "types/decimal128.h"
#include "utils/date_utils.h"
#include "utils/feature_counter.h"
#include "utils/documentdb_errors.h"

/* --------------------------------------------------------- */
/* Data types */
/* --------------------------------------------------------- */

/*
 * Custom FRAMEOPTION flags relevant to mongo $setWindowFields stage.
 *
 * Note: Make sure these flag values adhere to the existing FRAMEOPTION flags defined in PG
 * nodes/parsenodes.h and no flag should overlap with existing flags.
 */
typedef enum FRAMEOPTION_MONGO
{
	FRAMEOPTION_MONGO_INVALID = 0x0,
	FRAMEOPTION_MONGO_RANGE_UNITS = 0x1000000,
	FRAMEOPTION_MONGO_UNKNOWN_FIELDS = 0x2000000
} FRAMEOPTION_MONGO;

#define FRAMEOPTION_MONGO_ONLY (FRAMEOPTION_MONGO_RANGE_UNITS | \
								FRAMEOPTION_MONGO_UNKNOWN_FIELDS)

/* Default Frame options for mongodb are documents window with unbounded preceding and unboudnded following */
#define FRAMEOPTION_MONGO_SETWINDOWFIELDS_DEFAULT (FRAMEOPTION_NONDEFAULT | \
												   FRAMEOPTION_ROWS | \
												   FRAMEOPTION_START_UNBOUNDED_PRECEDING | \
												   FRAMEOPTION_BETWEEN | \
												   FRAMEOPTION_END_UNBOUNDED_FOLLOWING)

/* $setWindowFields `sort` options, required for the validation against different combinations
 * with range/documents based windows or window aggregation operators.
 */
typedef struct SetWindowFieldSortOption
{
	/* The pgbson const of the sort spec */
	Const *sortSpecConst;

	/* Whether the sort is ascending or not */
	bool isAscending;
} SetWindowFieldSortOption;


typedef struct
{
	/* Index of the window clause for this window operator function, the number should be unique for windowclauses
	 * and map exactly to one window operator
	 */
	Index winRef;

	/* ParseState required to add the target entries, mostly for the resno */
	ParseState *pstate;

	/* The document Var expression */
	Expr *docExpr;

	/* Output field name of the operator results */
	const char *outputFieldName;

	/* Sort options list */
	List *sortOptions;

	/* Variable context for operators */
	Expr *variableContext;

	/* boolean to check if the window is present */
	bool isWindowPresent;

	/* whether internal window operator is enabled or not */
	bool enableInternalWindowOperator;
} WindowOperatorContext;


typedef WindowFunc *(*WindowOperatorFunc)(const bson_value_t *opValue,
										  WindowOperatorContext *context);

typedef struct
{
	/* The operator name e.g. $addToSet, $avg, $denseRank etc */
	const char *operatorName;

	/* The function that will create WindowClause for the window operator */
	WindowOperatorFunc windowOperatorFunc;
} WindowOperatorDefinition;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static void ParseAndSetFrameOption(const bson_value_t *value, WindowClause *windowClause,
								   DateUnit timeUnit, int *frameOptions);
static WindowFunc * HandleDollarIntegralWindowOperator(const bson_value_t *opValue,
													   WindowOperatorContext *context);
static WindowFunc * HandleDollarDerivativeWindowOperator(const bson_value_t *opValue,
														 WindowOperatorContext *context);
static void ParseIntegralDerivativeExpression(const bson_value_t *opValue,
											  WindowOperatorContext *context,
											  Expr **xExpr,
											  Expr **yExpr,
											  long *unitInMs,
											  bool isIntegralOperator);
static WindowFunc * GetIntegralDerivativeWindowFunc(const bson_value_t *opValue,
													WindowOperatorContext *context,
													bool isIntegral);
static Datum EnsureValidUnitOffsetAndGetInterval(const bson_value_t *value,
												 DateUnit dateUnit);

static TargetEntry * UpdateWindowOperatorAndFrameOptions(const
														 bson_value_t *windowOpValue,
														 WindowOperatorContext *context,
														 WindowClause *windowClause,
														 int *allFrameOptions);
static void UpdatePartitionAndSortClauses(Query *query, Expr *docExpr,
										  Expr *partitionByExpr,
										  List *sortByClauses, int allFrameOptions,
										  ParseState *pstate);
static bool IsPartitionByOnShardKey(const bson_value_t *partitionByValue,
									const MongoCollection *collection);
static void ThrowInvalidFrameOptions(void);
static void ThrowExtraInvalidFrameOptions(const char *str1, const char *str2);
static void ThrowInvalidWindowValue(const char *windowType, const bson_value_t *value);
static void UpdateWindowOptions(const pgbsonelement *element, WindowClause *windowClause,
								WindowOperatorContext *context, int *frameOptions);
static void UpdateWindowAggregationOperator(const pgbsonelement *element,
											WindowOperatorContext *context,
											TargetEntry **entry);
static WindowFunc * GetSimpleBsonExpressionGetWindowFunc(const bson_value_t *opValue,
														 WindowOperatorContext *context,
														 Oid aggregateFunctionOid);
static inline void ValidateInputForRankFunctions(const bson_value_t *opValue,
												 WindowOperatorContext *context,
												 char *opName);
static void ParseInputDocumentForDollarShift(const bson_value_t *opValue,
											 bson_value_t *output, bson_value_t *by,
											 bson_value_t *defaultValue);
static void ParseInputDocumentForDollarShift(const bson_value_t *opValue,
											 bson_value_t *output, bson_value_t *by,
											 bson_value_t *defaultValue);
static WindowFunc * HandleDollarTopBottomOperators(const bson_value_t *opValue,
												   WindowOperatorContext *context,
												   const char *opName,
												   Oid aggregateFunctionOid,
												   bool isNOperator);
static WindowFunc * HandleDollarFirstLastOperators(const bson_value_t *opValue,
												   WindowOperatorContext *context,
												   const char *opName,
												   Oid aggregateFunctionOid,
												   bool isNOperator);

/*===================================*/
/* Window Operator Handler functions */
/*===================================*/
static WindowFunc * HandleDollarAddToSetWindowOperator(const bson_value_t *opValue,
													   WindowOperatorContext *context);
static WindowFunc * HandleDollarAvgWindowOperator(const bson_value_t *opValue,
												  WindowOperatorContext *context);
static WindowFunc * HandleDollarCountWindowOperator(const bson_value_t *opValue,
													WindowOperatorContext *context);
static WindowFunc * HandleDollarCovariancePopWindowOperator(const bson_value_t *opValue,
															WindowOperatorContext *
															context);
static WindowFunc * HandleDollarCovarianceSampWindowOperator(const bson_value_t *opValue,
															 WindowOperatorContext *
															 context);
static WindowFunc * HandleDollarDenseRankWindowOperator(const bson_value_t *opValue,
														WindowOperatorContext *context);
static WindowFunc * HandleDollarDocumentNumberWindowOperator(const bson_value_t *opValue,
															 WindowOperatorContext *
															 context);
static WindowFunc * HandleDollarPushWindowOperator(const bson_value_t *opValue,
												   WindowOperatorContext *context);
static WindowFunc * HandleDollarRankWindowOperator(const bson_value_t *opValue,
												   WindowOperatorContext *context);
static WindowFunc * HandleDollarSumWindowOperator(const bson_value_t *opValue,
												  WindowOperatorContext *context);
static WindowFunc * HandleDollarExpMovingAvgWindowOperator(const bson_value_t *opValue,
														   WindowOperatorContext *context);
static WindowFunc * HandleDollarLinearFillWindowOperator(const bson_value_t *opValue,
														 WindowOperatorContext *context);
static WindowFunc * HandleDollarLocfFillWindowOperator(const bson_value_t *opValue,
													   WindowOperatorContext *context);
static WindowFunc * HandleDollarConstFillWindowOperator(const bson_value_t *opValue,
														WindowOperatorContext *context);
static WindowFunc * HandleDollarShiftWindowOperator(const bson_value_t *opValue,
													WindowOperatorContext *context);
static WindowFunc * HandleDollarTopNWindowOperator(const bson_value_t *opValue,
												   WindowOperatorContext *context);
static WindowFunc * HandleDollarBottomNWindowOperator(const bson_value_t *opValue,
													  WindowOperatorContext *context);
static WindowFunc * HandleDollarTopWindowOperator(const bson_value_t *opValue,
												  WindowOperatorContext *context);
static WindowFunc * HandleDollarBottomWindowOperator(const bson_value_t *opValue,
													 WindowOperatorContext *context);
static WindowFunc * HandleDollarStdDevPopWindowOperator(const bson_value_t *opValue,
														WindowOperatorContext *context);
static WindowFunc * HandleDollarStdDevSampWindowOperator(const bson_value_t *opValue,
														 WindowOperatorContext *context);
static WindowFunc * HandleDollarFirstWindowOperator(const bson_value_t *opValue,
													WindowOperatorContext *context);
static WindowFunc * HandleDollarLastWindowOperator(const bson_value_t *opValue,
												   WindowOperatorContext *context);
static WindowFunc * HandleDollarFirstNWindowOperator(const bson_value_t *opValue,
													 WindowOperatorContext *context);
static WindowFunc * HandleDollarLastNWindowOperator(const bson_value_t *opValue,
													WindowOperatorContext *context);
static WindowFunc * HandleDollarMaxNWindowOperator(const bson_value_t *opValue,
												   WindowOperatorContext *context);
static WindowFunc * HandleDollarMinNWindowOperator(const bson_value_t *opValue,
												   WindowOperatorContext *context);
static WindowFunc * HandleDollarMinWindowOperator(const bson_value_t *opValue,
												  WindowOperatorContext *context);
static WindowFunc * HandleDollarMaxWindowOperator(const bson_value_t *opValue,
												  WindowOperatorContext *context);


/*
 * Window operators definitions.
 *
 * Note: Please keep the list in alphabetical order for better readability.
 */
static const WindowOperatorDefinition WindowOperatorDefinitions[] =
{
	{
		.operatorName = "$_internal_constFill",
		.windowOperatorFunc = &HandleDollarConstFillWindowOperator
	},
	{
		.operatorName = "$addToSet",
		.windowOperatorFunc = &HandleDollarAddToSetWindowOperator
	},
	{
		.operatorName = "$avg",
		.windowOperatorFunc = &HandleDollarAvgWindowOperator
	},
	{
		.operatorName = "$bottom",
		.windowOperatorFunc = &HandleDollarBottomWindowOperator
	},
	{
		.operatorName = "$bottomN",
		.windowOperatorFunc = &HandleDollarBottomNWindowOperator
	},
	{
		.operatorName = "$count",
		.windowOperatorFunc = &HandleDollarCountWindowOperator
	},
	{
		.operatorName = "$covariancePop",
		.windowOperatorFunc = &HandleDollarCovariancePopWindowOperator
	},
	{
		.operatorName = "$covarianceSamp",
		.windowOperatorFunc = &HandleDollarCovarianceSampWindowOperator
	},
	{
		.operatorName = "$denseRank",
		.windowOperatorFunc = &HandleDollarDenseRankWindowOperator
	},
	{
		.operatorName = "$derivative",
		.windowOperatorFunc = HandleDollarDerivativeWindowOperator
	},
	{
		.operatorName = "$documentNumber",
		.windowOperatorFunc = &HandleDollarDocumentNumberWindowOperator
	},
	{
		.operatorName = "$expMovingAvg",
		.windowOperatorFunc = &HandleDollarExpMovingAvgWindowOperator
	},
	{
		.operatorName = "$first",
		.windowOperatorFunc = &HandleDollarFirstWindowOperator
	},
	{
		.operatorName = "$firstN",
		.windowOperatorFunc = &HandleDollarFirstNWindowOperator
	},
	{
		.operatorName = "$integral",
		.windowOperatorFunc = HandleDollarIntegralWindowOperator
	},
	{
		.operatorName = "$last",
		.windowOperatorFunc = &HandleDollarLastWindowOperator
	},
	{
		.operatorName = "$lastN",
		.windowOperatorFunc = &HandleDollarLastNWindowOperator
	},
	{
		.operatorName = "$linearFill",
		.windowOperatorFunc = &HandleDollarLinearFillWindowOperator
	},
	{
		.operatorName = "$locf",
		.windowOperatorFunc = &HandleDollarLocfFillWindowOperator
	},
	{
		.operatorName = "$max",
		.windowOperatorFunc = &HandleDollarMaxWindowOperator
	},
	{
		.operatorName = "$maxN",
		.windowOperatorFunc = &HandleDollarMaxNWindowOperator
	},
	{
		.operatorName = "$median",
		.windowOperatorFunc = NULL
	},
	{
		.operatorName = "$min",
		.windowOperatorFunc = &HandleDollarMinWindowOperator
	},
	{
		.operatorName = "$minN",
		.windowOperatorFunc = &HandleDollarMinNWindowOperator
	},
	{
		.operatorName = "$percentile",
		.windowOperatorFunc = NULL
	},
	{
		.operatorName = "$push",
		.windowOperatorFunc = &HandleDollarPushWindowOperator
	},
	{
		.operatorName = "$rank",
		.windowOperatorFunc = &HandleDollarRankWindowOperator
	},
	{
		.operatorName = "$shift",
		.windowOperatorFunc = &HandleDollarShiftWindowOperator
	},
	{
		.operatorName = "$stdDevPop",
		.windowOperatorFunc = &HandleDollarStdDevPopWindowOperator
	},
	{
		.operatorName = "$stdDevSamp",
		.windowOperatorFunc = &HandleDollarStdDevSampWindowOperator
	},
	{
		.operatorName = "$sum",
		.windowOperatorFunc = &HandleDollarSumWindowOperator
	},
	{
		.operatorName = "$top",
		.windowOperatorFunc = &HandleDollarTopWindowOperator
	},
	{
		.operatorName = "$topN",
		.windowOperatorFunc = &HandleDollarTopNWindowOperator
	}
};

static const int WindowOperatorsCount = sizeof(WindowOperatorDefinitions) /
										sizeof(WindowOperatorDefinition);


/*
 * Ensures the given window spec of $setWindowFields' output is valid combination
 */
static inline void
EnsureValidWindowSpec(int frameOptions)
{
	if ((frameOptions & FRAMEOPTION_ROWS) != FRAMEOPTION_ROWS &&
		(frameOptions & FRAMEOPTION_RANGE) != FRAMEOPTION_RANGE &&
		(frameOptions & FRAMEOPTION_MONGO_UNKNOWN_FIELDS) ==
		FRAMEOPTION_MONGO_UNKNOWN_FIELDS)
	{
		/* Only unknwon fields are provided */
		ereport(ERROR, (
					errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
					errmsg(
						"'window' field can only contain 'documents' as the only argument "
						"or 'range' with an optional 'unit' field")));
	}
	if ((frameOptions & FRAMEOPTION_ROWS) == FRAMEOPTION_ROWS &&
		(frameOptions & FRAMEOPTION_RANGE) == FRAMEOPTION_RANGE)
	{
		/* Both range and document are provided */
		ThrowInvalidFrameOptions();
	}

	if ((frameOptions & FRAMEOPTION_ROWS) == FRAMEOPTION_ROWS &&
		(frameOptions & FRAMEOPTION_MONGO_UNKNOWN_FIELDS) ==
		FRAMEOPTION_MONGO_UNKNOWN_FIELDS)
	{
		/* Document window with unknown fields */
		ThrowExtraInvalidFrameOptions("documents", "");
	}

	if ((frameOptions & FRAMEOPTION_RANGE) == FRAMEOPTION_RANGE &&
		(frameOptions & FRAMEOPTION_MONGO_UNKNOWN_FIELDS) ==
		FRAMEOPTION_MONGO_UNKNOWN_FIELDS)
	{
		/* Range window with unknown field */
		ThrowExtraInvalidFrameOptions("range", "besides 'unit'");
	}

	if ((frameOptions & FRAMEOPTION_MONGO_RANGE_UNITS) == FRAMEOPTION_MONGO_RANGE_UNITS &&
		(frameOptions & FRAMEOPTION_RANGE) != FRAMEOPTION_RANGE)
	{
		/* Units without range */
		ereport(ERROR, (
					errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
					errmsg(
						"Window bounds can only specify 'unit' with range-based bounds.")));
	}
}


/*
 * Returns whether the query target list contains resjunk entries
 */
inline static bool
HasResJunkEntries(Query *query)
{
	return query && query->targetList != NIL &&
		   count_nonjunk_tlist_entries(query->targetList) != list_length(
		query->targetList);
}


/*
 * Ensures the given window spec of $setWindowFields' is compliant with sortBy field
 * requirement
 */
static inline void
EnsureSortRequirements(int frameOptions, WindowOperatorContext *context)
{
	int sortByLength = list_length(context->sortOptions);
	if ((frameOptions & FRAMEOPTION_RANGE) == FRAMEOPTION_RANGE)
	{
		/* Must be supplied for a range window */
		if (sortByLength != 1)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5339902),
							errmsg("Range-based bounds require sortBy a single field")));
		}

		SetWindowFieldSortOption *sortField = (SetWindowFieldSortOption *) linitial(
			context->sortOptions);
		if (!sortField->isAscending)
		{
			/* TODO: Actual error code for this is 8947401 which is beyond our current error limit
			 * Fix later to extend the error code range to accomodate this, for now throwing
			 * generic failed to parse
			 */
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("Range-based bounds require an ascending sortBy")));
		}
	}
	else if (sortByLength == 0)
	{
		bool isUnbounded = ((frameOptions & FRAMEOPTION_START_UNBOUNDED_PRECEDING) &&
							(frameOptions & FRAMEOPTION_END_UNBOUNDED_FOLLOWING));

		if (!isUnbounded)
		{
			/* Must be supplied for a bounded document window */
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5339901),
							errmsg("Document-based bounds require a sortBy")));
		}
	}
}


Query *
HandleSetWindowFields(const bson_value_t *existingValue, Query *query,
					  AggregationPipelineBuildContext *context)
{
	bool enableInternalWindowOperator = false;
	Expr *partitionByExpr = NULL;

	return HandleSetWindowFieldsCore(existingValue, query, context, partitionByExpr,
									 enableInternalWindowOperator);
}


/*
 * $setWindowFields aggregation stage handler.
 * This function constructs the query AST for Window aggregation operators over a partition defined by the $setWindowFields spec.
 * MongoDB spec is:
 * {
 *     $setWindowFields:{
 *         partitionBy: <expression>,
 *         sortBy : <sortSpec>,
 *         output: {
 *             <field1>: {
 *                 <window aggregation operator>: <spec>,
 *                 window: {
 *                      documents: [<bounds>],
 *                      <range>: [<bounds>], // either `window` or `range`
 *                      <unit>: <time unit> // only for `range`
 *                 }
 *             },
 *             ...
 *             <other window aggregation operators>
 *         }
 *     }
 * }
 *
 * The Postgres query that is formed at the end of the function is:
 *
 * SELECT ApiInternalSchemaName.bson_dollar_merge_documents(document, ApiCatalogSchema.bson_repath_and_build(<field1>::text, total)) AS document
 * FROM (
 *     SELECT
 *         document,
 *         <windowAggOperator> OVER (PARTITION BY bson_expression_get_partition(document, <expression>, isNullOnEmpty) <ORDER BY bson_orderby(document, <sortSpec>)> <ROWS / RANGE frame options> ) as total,
 *         <other window aggregator operators>
 *     FROM (
 *          <prior stage result>
 *     )
 * );
 *
 * The window aggregation operation is only pushed to shards in case when `partitionBy` expression is same as the `shardKey` of collection or
 *
 * If partitionByExpr is specified and not null, it is used as the partitionBy expression in priority and the partitionBy field in the spec would be skipped.
 * Otherwise, the partitionBy expression is derived from the partitionBy field in the $setWindowFields spec.
 *
 * If enbaleInternalWindowOperator is set to true, the internal window operators can be used to perform the window operations;
 * In normal usage of $setWindowFields, this should be false.
 *
 * the query is a single shard query.
 */
Query *
HandleSetWindowFieldsCore(const bson_value_t *existingValue,
						  Query *query,
						  AggregationPipelineBuildContext *context,
						  Expr *partitionByExpr,
						  bool enableInternalWindowOperator)
{
	ReportFeatureUsage(FEATURE_STAGE_SETWINDOWFIELDS);

	if (existingValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"the $setWindowFields stage specification must be an object, "
							"found %s", BsonTypeName(existingValue->value_type)),
						errdetail_log(
							"the $setWindowFields stage specification must be an object, "
							"found %s", BsonTypeName(existingValue->value_type))));
	}

	RangeTblEntry *rte = linitial(query->rtable);
	bool isRTEDataTable = (rte->rtekind == RTE_RELATION || rte->rtekind == RTE_FUNCTION);
	if (!isRTEDataTable || query->limitOffset || query->limitCount ||
		HasResJunkEntries(query))
	{
		/* Migrate existing query to subquery so that window operators can be applied
		 * on a subquery
		 */
		query = MigrateQueryToSubQuery(query, context);
	}

	TargetEntry *firstEntry = linitial(query->targetList);
	Expr *docExpr = firstEntry->expr;

	bson_value_t outputSpec = { 0 };
	Expr *partitionExpr = partitionByExpr;
	List *sortOptions = NIL;

	bson_iter_t iter;
	BsonValueInitIterator(existingValue, &iter);
	while (bson_iter_next(&iter))
	{
		const char *key = bson_iter_key(&iter);
		const bson_value_t *value = bson_iter_value(&iter);
		if (partitionExpr == NULL && strcmp(key, "partitionBy") == 0)
		{
			if (value->value_type == BSON_TYPE_ARRAY)
			{
				ereport(ERROR, (
							errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
							errmsg(
								"An expression used to partition cannot evaluate to value of type array")));
			}

			/* If partitionBy is on the shard key expression and the base table is still the rte
			 * use the shard key column directly, otherwise get the expression on which to parition by
			 */
			if (isRTEDataTable &&
				IsPartitionByOnShardKey(value, context->mongoCollection))
			{
				partitionExpr = (Expr *) makeVar(((Var *) docExpr)->varno,
												 DOCUMENT_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER,
												 INT8OID, -1,
												 InvalidOid, 0);
			}
			else
			{
				/* Consider empty or missing values as null */
				bool isNullOnEmpty = true;
				List *args;
				Oid functionOid;

				if (context->variableSpec != NULL)
				{
					functionOid = BsonExpressionPartitionGetWithLetFunctionOid();
					args = list_make4(docExpr,
									  MakeBsonConst(BsonValueToDocumentPgbson(value)),
									  MakeBoolValueConst(isNullOnEmpty),
									  context->variableSpec);
				}
				else
				{
					functionOid = BsonExpressionPartitionGetFunctionOid();
					args = list_make3(docExpr,
									  MakeBsonConst(BsonValueToDocumentPgbson(value)),
									  MakeBoolValueConst(isNullOnEmpty));
				}

				partitionExpr = (Expr *) makeFuncExpr(
					functionOid, BsonTypeId(), args, InvalidOid,
					InvalidOid, COERCE_EXPLICIT_CALL);
			}
		}
		else if (strcmp(key, "sortBy") == 0)
		{
			EnsureTopLevelFieldValueType("$setWindowFields.sortBy", value,
										 BSON_TYPE_DOCUMENT);

			bson_iter_t sortByIter;
			BsonValueInitIterator(value, &sortByIter);
			while (bson_iter_next(&sortByIter))
			{
				pgbsonelement element;
				BsonIterToPgbsonElement(&sortByIter, &element);

				pgbson *sortDoc = PgbsonElementToPgbson(&element);
				Const *sortBson = MakeBsonConst(sortDoc);
				bool isAscending = ValidateOrderbyExpressionAndGetIsAscending(sortDoc);

				SetWindowFieldSortOption *sortOption = palloc(
					sizeof(SetWindowFieldSortOption));
				sortOption->sortSpecConst = sortBson;
				sortOption->isAscending = isAscending;

				sortOptions = lappend(sortOptions, sortOption);
			}
		}
		else if (strcmp(key, "output") == 0)
		{
			EnsureTopLevelFieldValueType("$setWindowFields.output", value,
										 BSON_TYPE_DOCUMENT);

			/*
			 * Since we need the partition and orderby clauses,
			 * we copy the output value to add window functions later.
			 */
			outputSpec = *value;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNKNOWNBSONFIELD),
							errmsg(
								"BSON field '$setWindowFields.%s' is an unknown field.",
								key),
							errdetail_log(
								"BSON field '$setWindowFields.%s' is an unknown field.",
								key)));
		}
	}

	/* Required fields check */
	if (outputSpec.value_type == BSON_TYPE_EOD)
	{
		ThrowTopLevelMissingFieldErrorWithCode("$setWindowFields.output",
											   ERRCODE_DOCUMENTDB_LOCATION40414);
	}

	/* Construct all the window clauses */
	bool hasWindowOperators = !IsBsonValueEmptyDocument(&outputSpec);
	bson_iter_t outputIter;
	BsonValueInitIterator(&outputSpec, &outputIter);

	Index winRef = 0;
	int childIndex = 1;
	int allFrameOptions = 0;

	ParseState *parseState = make_parsestate(NULL);
	parseState->p_expr_kind = EXPR_KIND_WINDOW_PARTITION;
	parseState->p_next_resno = list_length(query->targetList) + 1;

	List *repathArgs = NIL;
	while (bson_iter_next(&outputIter))
	{
		pgbsonelement output;
		BsonIterToPgbsonElement(&outputIter, &output);

		if (output.pathLength == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40352),
							errmsg("FieldPath cannot be constructed with empty string")));
		}

		if (output.path[0] == '$')
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16410),
							errmsg(
								"FieldPath field names may not start with '$'. Consider using $getField or $setField.")));
		}

		if (output.bsonValue.value_type != BSON_TYPE_DOCUMENT)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("The field '%s' must be an object", output.path),
							errdetail_log(
								"$setWindowField output field must be an object")));
		}

		winRef++;

		/* Fill common info for a window which is defined by "partitionBy" and "sortBy" */
		WindowClause *windowClause = makeNode(WindowClause);
		windowClause->winref = winRef;

		WindowOperatorContext windowOpContext = {
			.docExpr = docExpr,
			.outputFieldName = output.path,
			.pstate = parseState,
			.winRef = winRef,
			.sortOptions = sortOptions,
			.variableContext = context->variableSpec,
			.enableInternalWindowOperator = enableInternalWindowOperator,
		};

		TargetEntry *windowpOperatorTle =
			UpdateWindowOperatorAndFrameOptions(&(output.bsonValue), &windowOpContext,
												windowClause, &allFrameOptions);
		query->targetList = lappend(query->targetList, windowpOperatorTle);

		repathArgs = lappend(repathArgs, MakeTextConst(windowOpContext.outputFieldName,
													   strlen(
														   windowOpContext.outputFieldName)));
		repathArgs = lappend(repathArgs, makeVarFromTargetEntry(childIndex,
																windowpOperatorTle));
		query->windowClause = lappend(query->windowClause, windowClause);
	}

	if (hasWindowOperators)
	{
		query->hasWindowFuncs = true;

		/*
		 * Now we can add the partition by and order by clauses' resjunk entries in the target list,
		 * Note: All resjunk entries should be followed by non-resjunk entries added above for window functions.
		 */
		UpdatePartitionAndSortClauses(query, docExpr, partitionExpr, sortOptions,
									  allFrameOptions, parseState);

		/*
		 * Migrate to subquery and merge results of all window functions back to document
		 * bson_add_fields(document, bson_repath_and_build(<window functions results>))
		 */
		context->expandTargetList = true;
		query = MigrateQueryToSubQuery(query, context);
		TargetEntry *firstEntryAfterSubQuery = linitial(query->targetList);

		/* Use bson_repath_and_build to merge the output of all window operations */
		Expr *repathExpr = GenerateMultiExpressionRepathExpression(repathArgs);
		FuncExpr *mergeDocumentsExpr = makeFuncExpr(BsonDollaMergeDocumentsFunctionOid(),
													BsonTypeId(),
													list_make2(
														(Expr *) firstEntryAfterSubQuery->
														expr,
														repathExpr),
													InvalidOid, InvalidOid,
													COERCE_EXPLICIT_CALL);
		firstEntryAfterSubQuery->expr = (Expr *) mergeDocumentsExpr;

		/* Push everything to subquery after this */
		context->requiresSubQuery = true;
	}

	return query;
}


/* ================================
 * Private static helpers
 * ================================
 */


/*
 * Parses the given window operator value of $setWindowField, updates the given windowClause
 * with the window operator and frame options and returns the target entry for the window operator.
 *
 * @param windowOpValue The window operator bson value to parse
 * @param outputFieldName The output field name for the window operator target entry
 * @param windowClause The window clause to update with the window operator and frame options
 * @param allFrameOptions - An OR'ed value of all frame options in multiple window operators
 */
TargetEntry *
UpdateWindowOperatorAndFrameOptions(const bson_value_t *windowOpValue,
									WindowOperatorContext *context,
									WindowClause *windowClause,
									int *allFrameOptions)
{
	bson_iter_t valueIter;
	BsonValueInitIterator(windowOpValue, &valueIter);

	int frameOptions = 0;
	TargetEntry *entry = NULL;
	pgbsonelement windowValue = { 0 }, operatorValue = { 0 };
	while (bson_iter_next(&valueIter))
	{
		const char *key = bson_iter_key(&valueIter);

		if (strcmp(key, "window") == 0)
		{
			BsonIterToPgbsonElement(&valueIter, &windowValue);
		}
		else if (key[0] == '$')
		{
			BsonIterToPgbsonElement(&valueIter, &operatorValue);
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("Window function found an unknown argument: %s", key),
							errdetail_log("Window function found an unknown argument")));
		}
	}

	if (operatorValue.bsonValue.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("Expected a $-prefixed window function, window")));
	}

	/* Add the window operators and frame options */
	context->isWindowPresent = windowValue.bsonValue.value_type != BSON_TYPE_EOD;
	UpdateWindowAggregationOperator(&operatorValue, context, &entry);
	if (windowValue.bsonValue.value_type != BSON_TYPE_EOD)
	{
		/* The window value object should be a document.*/
		if (windowValue.bsonValue.value_type != BSON_TYPE_DOCUMENT)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("'window' field must be an object")));
		}

		UpdateWindowOptions(&windowValue, windowClause, context, &frameOptions);
	}

	if (frameOptions == 0)
	{
		/* If no frame options are set, then default to document unbounded, unbounded */
		frameOptions = FRAMEOPTION_MONGO_SETWINDOWFIELDS_DEFAULT;
	}
	*allFrameOptions |= frameOptions;

	/* In the window frameOptions strip documentdb specific options */
	windowClause->frameOptions = frameOptions & (~FRAMEOPTION_MONGO_ONLY);

	/* entry should not remain NULL at this point */
	Assert(entry != NULL);
	return entry;
}


/* Function to update all window clauses to use same partition and sort clauses.
 * Also creates the resjunk target entries for the partition and sort clauses
 * after all target entries for window operators have been added.
 */
static void
UpdatePartitionAndSortClauses(Query *query, Expr *docExpr,
							  Expr *partitionByExpr,
							  List *sortOptions, int allFrameOptions,
							  ParseState *pstate)
{
	bool resjunk = true;

	/* Get the SortGroupClause of all partitionby and sortby clauses */
	List *partitionClauseList = NIL;
	List *orderByClauseList = NIL;

	if (partitionByExpr != NULL)
	{
		/* Add partition clause entry */
		TargetEntry *partitionTle = makeTargetEntry((Expr *) partitionByExpr,
													pstate->p_next_resno++,
													"?partitionBy?", resjunk);
		assignSortGroupRef(partitionTle, query->targetList);
		query->targetList = lappend(query->targetList, partitionTle);

		SortGroupClause *partitionClause = makeNode(SortGroupClause);
		partitionClause->tleSortGroupRef = partitionTle->ressortgroupref;
		partitionClause->eqop = BsonEqualOperatorId();
		partitionClause->sortop = BsonLessThanOperatorId();
		partitionClause->nulls_first = false;
		partitionClause->hashable = true;

		partitionClauseList = list_make1(partitionClause);
	}

	if (sortOptions != NIL && list_length(sortOptions) > 0)
	{
		/*
		 * In MongoDB all the sort clauses are common for all the window definitions.
		 * So we decide here which ORDER BY function is needed to be called based on the all
		 * window definitions.
		 * If all windows are document based : BsonOrderBy
		 * If 1/more range windows : BsonOrderByPartition
		 */
		bool isRangeWindow = (allFrameOptions & FRAMEOPTION_RANGE) == FRAMEOPTION_RANGE;
		bool isTimeRangeWindow = (allFrameOptions & FRAMEOPTION_MONGO_RANGE_UNITS) ==
								 FRAMEOPTION_MONGO_RANGE_UNITS;

		ListCell *lc;
		foreach(lc, sortOptions)
		{
			SetWindowFieldSortOption *sortOption =
				(SetWindowFieldSortOption *) lfirst(lc);
			List *args = NIL;
			Oid sortFunctionOid = InvalidOid;
			if (isRangeWindow)
			{
				sortFunctionOid = BsonOrderByPartitionFunctionOid();
				args = list_make3(docExpr, sortOption->sortSpecConst,
								  MakeBoolValueConst(isTimeRangeWindow));
			}
			else
			{
				sortFunctionOid = BsonOrderByFunctionOid();
				args = list_make2(docExpr, sortOption->sortSpecConst);
			}
			Expr *expr = (Expr *) makeFuncExpr(sortFunctionOid, BsonTypeId(), args,
											   InvalidOid, InvalidOid,
											   COERCE_EXPLICIT_CALL);

			SortBy *sortBy = makeNode(SortBy);
			sortBy->location = -1;
			sortBy->sortby_dir = sortOption->isAscending ? SORTBY_ASC : SORTBY_DESC;
			sortBy->sortby_nulls = sortOption->isAscending ? SORTBY_NULLS_FIRST :
								   SORTBY_NULLS_LAST;
			sortBy->node = (Node *) expr;

			TargetEntry *sortEntry = makeTargetEntry((Expr *) sortBy->node,
													 pstate->p_next_resno++,
													 NULL, resjunk);
			query->targetList = lappend(query->targetList, sortEntry);
			orderByClauseList = addTargetToSortList(pstate, sortEntry,
													orderByClauseList,
													query->targetList,
													sortBy);
		}
	}

	/* Update partitionby and sort by for all window clauses */
	ListCell *lc;
	foreach(lc, query->windowClause)
	{
		WindowClause *wc = (WindowClause *) lfirst(lc);
		wc->partitionClause = partitionClauseList;
		wc->orderClause = orderByClauseList;
	}
}


/*
 * Create a PG Interval datum from the offset provided for time based range window
 */
static Datum
EnsureValidUnitOffsetAndGetInterval(const bson_value_t *value, DateUnit dateUnit)
{
	Assert(value != NULL && dateUnit != DateUnit_Invalid);

	if (!IsBsonValueFixedInteger(value))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("With 'unit', range-based bounds must be an integer")));
	}

	int64 offset = labs(BsonValueAsInt64(value));
	return GetIntervalFromDateUnitAndAmount(dateUnit, offset);
}


/*
 * Parse the window option for the window operators and sets the respective
 * frame options in the window clause
 */
static void
ParseAndSetFrameOption(const bson_value_t *value, WindowClause *windowClause,
					   DateUnit timeUnit, int *frameOptions)
{
	Assert(value->value_type == BSON_TYPE_ARRAY);
	bson_iter_t frameIter;
	BsonValueInitIterator(value, &frameIter);

	bool isDocumentFrame = (*frameOptions & FRAMEOPTION_ROWS) == FRAMEOPTION_ROWS;
	const char *frameName = isDocumentFrame ? "documents" : "range";

	uint32 index = 0;
	bson_value_t boundsInterval[2] = {
		{ 0 }, { 0 }
	};
	while (bson_iter_next(&frameIter))
	{
		if (index > 2)
		{
			ThrowInvalidWindowValue(frameName, value);
		}

		const bson_value_t *frameValue = bson_iter_value(&frameIter);

		if (frameValue->value_type != BSON_TYPE_UTF8 &&
			!BsonTypeIsNumber(frameValue->value_type))
		{
			if (isDocumentFrame)
			{
				ereport(ERROR, (
							errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("Numeric document-based bounds must be an integer")));
			}
			else
			{
				ereport(ERROR, (
							errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("Range-based bounds expression must be a number")));
			}
		}
		else if (frameValue->value_type == BSON_TYPE_UTF8)
		{
			if (strcmp(frameValue->value.v_utf8.str, "unbounded") == 0)
			{
				boundsInterval[index].value_type = index == 0 ? BSON_TYPE_MINKEY :
												   BSON_TYPE_MAXKEY;

				*frameOptions |= (index == 0) ?
								 FRAMEOPTION_START_UNBOUNDED_PRECEDING :
								 FRAMEOPTION_END_UNBOUNDED_FOLLOWING;
			}
			else if (strcmp(frameValue->value.v_utf8.str, "current") == 0)
			{
				boundsInterval[index].value_type = BSON_TYPE_INT32;
				boundsInterval[index].value.v_int32 = 0;

				*frameOptions |= (index == 0) ?
								 FRAMEOPTION_START_CURRENT_ROW :
								 FRAMEOPTION_END_CURRENT_ROW;
			}
			else
			{
				ereport(ERROR, (
							errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg(
								"Window bounds must be 'unbounded', 'current', or a number.")));
			}
		}
		else
		{
			boundsInterval[index] = *frameValue;
			bool isPreceding = IsBsonValueNegativeNumber(frameValue);
			Node *frameNode = NULL;
			if (isDocumentFrame)
			{
				/* Set the document based frame offsets */
				bool checkFixedInteger = true;
				if (!IsBsonValue64BitInteger(frameValue, checkFixedInteger))
				{
					ereport(ERROR, (
								errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
								errmsg(
									"Numeric document-based bounds must be an integer")));
				}

				/* Make the frame value positive, we set the `isPreceding` based on negative value
				 * but the value should be positive
				 */
				int64 frameValueInt = labs(BsonValueAsInt64(frameValue));
				frameNode = (Node *) makeConst(INT8OID, -1, InvalidOid, sizeof(int64_t),
											   Int64GetDatum(frameValueInt), false, true);
			}
			else
			{
				/* Set the range based frame offsets */
				bool isTimeBasedRangeWindow = (*frameOptions &
											   FRAMEOPTION_MONGO_RANGE_UNITS);
				Oid inRangeFunctionOid = isTimeBasedRangeWindow ?
										 BsonInRangeIntervalFunctionId() :
										 BsonInRangeNumericFunctionId();
				if (index == 0)
				{
					windowClause->startInRangeFunc = inRangeFunctionOid;
				}
				else
				{
					windowClause->endInRangeFunc = inRangeFunctionOid;
				}

				if (isTimeBasedRangeWindow)
				{
					Datum interval = EnsureValidUnitOffsetAndGetInterval(frameValue,
																		 timeUnit);
					frameNode = (Node *) makeConst(INTERVALOID, -1, InvalidOid,
												   sizeof(Interval),
												   interval, false, false);
				}
				else
				{
					frameNode = (Node *) MakeBsonConst(BsonValueToDocumentPgbson(
														   frameValue));
				}

				/**
				 * Range-based bounds require an ascending sortBy. Thus we set the inRangeAsc and inRangeNullsFirst always set to be true
				 */
				windowClause->inRangeAsc = true;
				windowClause->inRangeNullsFirst = true;
			}

			if (index == 0)
			{
				*frameOptions |= (isPreceding) ? FRAMEOPTION_START_OFFSET_PRECEDING :
								 FRAMEOPTION_START_OFFSET_FOLLOWING;
				windowClause->startOffset = frameNode;
			}
			else
			{
				*frameOptions |= (isPreceding) ? FRAMEOPTION_END_OFFSET_PRECEDING :
								 FRAMEOPTION_END_OFFSET_FOLLOWING;
				windowClause->endOffset = frameNode;
			}
		}

		index++;
	}

	if (index != 2)
	{
		ThrowInvalidWindowValue(frameName, value);
	}

	bool isComparisionValid = false;
	if (CompareBsonValueAndType(&boundsInterval[0], &boundsInterval[1],
								&isComparisionValid) > 0)
	{
		ereport(ERROR, (
					errcode(ERRCODE_DOCUMENTDB_LOCATION5339900),
					errmsg("Lower bound must not exceed upper bound: %s",
						   BsonValueToJsonForLogging(value)),
					errdetail_log("Lower bound must not exceed upper bound.")));
	}

	*frameOptions |= FRAMEOPTION_BETWEEN;
}


/*
 * Checks if partitionBy expression of $setWindowFields stage is on the shard key
 * of the collection
 */
static bool
IsPartitionByOnShardKey(const bson_value_t *partitionByValue,
						const MongoCollection *collection)
{
	if (collection == NULL || collection->shardKey == NULL)
	{
		return false;
	}

	if (partitionByValue->value_type != BSON_TYPE_UTF8)
	{
		return false;
	}
	StringView partitionByFieldView = CreateStringViewFromString(
		partitionByValue->value.v_utf8.str);

	pgbsonelement shardKeyElement;
	if (!TryGetSinglePgbsonElementFromPgbson(collection->shardKey, &shardKeyElement))
	{
		return false;
	}

	if (partitionByFieldView.length > 1 &&
		StringViewStartsWith(&partitionByFieldView, '$'))
	{
		StringView partitionByWithoutDollar = StringViewSubstring(&partitionByFieldView,
																  1);
		if (StringViewEqualsCString(&partitionByWithoutDollar, shardKeyElement.path))
		{
			return true;
		}
	}

	return false;
}


/* Helper method that throws the error for invalid frame options */
static void
pg_attribute_noreturn()
ThrowInvalidFrameOptions()
{
	ereport(ERROR, (
				errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
				errmsg(
					"Window bounds can specify either 'documents' or 'unit', not both.")));
}


/* Helper method that throws the error extra frame options */
static void
pg_attribute_noreturn()
ThrowExtraInvalidFrameOptions(const char * str1, const char * str2)
{
	ereport(ERROR, (
				errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
				errmsg("'window' field that specifies %s cannot have other fields %s",
					   str1, str2),
				errdetail_log(
					"'window' field that specifies %s cannot have other fields %s",
					str1, str2)));
}


static void
pg_attribute_noreturn()
ThrowInvalidWindowValue(const char * windowType, const bson_value_t * value)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
					errmsg("Window bounds must be a 2-element array: %s: %s",
						   windowType,
						   BsonValueToJsonForLogging(value)),
					errdetail_log("Window bounds must be a 2-element array: %s: %s",
								  windowType,
								  BsonTypeName(value->value_type))));
}


/*
 * Update the window options for the given window operator and update the window clause
 * MongoDB spec of window operator is as follows:
 * {... window: { documnets: [start, end] }}
 * {... window: { range: [start, end], <unit: "unit">}}
 *
 */
static void
UpdateWindowOptions(const pgbsonelement *element, WindowClause *windowClause,
					WindowOperatorContext *context, int *frameOptions)
{
	bson_iter_t windowIter;
	BsonValueInitIterator(&element->bsonValue, &windowIter);
	bson_value_t documentWindow, rangeWindow;
	DateUnit dateUnit = DateUnit_Invalid;
	while (bson_iter_next(&windowIter))
	{
		const char *windowKey = bson_iter_key(&windowIter);
		const bson_value_t *windowValue = bson_iter_value(&windowIter);

		if (strcmp(windowKey, "documents") == 0)
		{
			if (windowValue->value_type != BSON_TYPE_ARRAY)
			{
				ThrowInvalidWindowValue(windowKey, windowValue);
			}
			*frameOptions |= FRAMEOPTION_ROWS | FRAMEOPTION_NONDEFAULT;
			documentWindow = *windowValue;
		}
		else if (strcmp(windowKey, "range") == 0)
		{
			if (windowValue->value_type != BSON_TYPE_ARRAY)
			{
				ThrowInvalidWindowValue(windowKey, windowValue);
			}
			*frameOptions |= FRAMEOPTION_RANGE | FRAMEOPTION_NONDEFAULT;
			rangeWindow = *windowValue;
		}
		else if (strcmp(windowKey, "unit") == 0)
		{
			*frameOptions |= FRAMEOPTION_MONGO_RANGE_UNITS;
			if (windowValue->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
								errmsg("'unit' must be a string")));
			}
			dateUnit = GetDateUnitFromString(windowValue->value.v_utf8.str);

			if (dateUnit == DateUnit_Invalid)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
								errmsg("unknown time unit value: %s",
									   windowValue->value.v_utf8.str)));
			}
		}
		else
		{
			*frameOptions |= FRAMEOPTION_MONGO_UNKNOWN_FIELDS;
		}
	}

	/* guard against invalid combos */
	EnsureValidWindowSpec(*frameOptions);

	/* Now add all the frame options for either document or range based window */
	if (*frameOptions & FRAMEOPTION_ROWS)
	{
		ParseAndSetFrameOption(&documentWindow, windowClause, dateUnit,
							   frameOptions);
	}
	else if (*frameOptions & FRAMEOPTION_RANGE)
	{
		ParseAndSetFrameOption(&rangeWindow, windowClause, dateUnit,
							   frameOptions);
	}

	/* Check if window and sortBy fields meet the requirements */
	EnsureSortRequirements(*frameOptions, context);
}


/*
 * UpdateWindowAggregationOperator transforms the window aggregation operator given in the $setWindowFields spec
 * to a WindowFunc node and sets the target entry for the operator.
 */
static void
UpdateWindowAggregationOperator(const pgbsonelement *element,
								WindowOperatorContext *context, TargetEntry **entry)
{
	bool knownOperator = false;
	for (int i = 0; i < WindowOperatorsCount; i++)
	{
		const WindowOperatorDefinition *definition =
			&WindowOperatorDefinitions[i];
		if (strcmp(element->path, definition->operatorName) == 0)
		{
			if (definition->windowOperatorFunc == NULL)
			{
				ereport(ERROR, (
							errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg("Window operator %s is not supported yet",
								   element->path),
							errdetail_log("Window operator %s is not supported yet",
										  element->path)));
			}

			knownOperator = true;

			WindowFunc *windowFunc = definition->windowOperatorFunc(&element->bsonValue,
																	context);
			*entry = makeTargetEntry((Expr *) windowFunc,
									 context->pstate->p_next_resno++,
									 (char *) context->outputFieldName,
									 false);
			break;
		}
	}

	if (!knownOperator)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("Unrecognized window function, %s", element->path),
						errdetail_log("Unrecognized window function, %s",
									  element->path)));
	}
}


/*
 * A Simple helper function to get the window function for an window aggregate of this form
 * <window_aggregate>(bson_expression_get(<expression>)).
 *
 * The window aggregate to be used is determined by the aggregateFunctionOid.
 */
static WindowFunc *
GetSimpleBsonExpressionGetWindowFunc(const bson_value_t *opValue,
									 WindowOperatorContext *context,
									 Oid aggregateFunctionOid)
{
	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = aggregateFunctionOid;
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = true;
	Const *constValue = MakeBsonConst(BsonValueToDocumentPgbson(opValue));

	Const *trueConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true), false,
								 true);
	List *args;
	Oid functionOid;

	if (context->variableContext != NULL)
	{
		functionOid = BsonExpressionGetWithLetFunctionOid();
		args = list_make4(context->docExpr, constValue, trueConst,
						  context->variableContext);
	}
	else
	{
		functionOid = BsonExpressionGetFunctionOid();
		args = list_make3(context->docExpr, constValue, trueConst);
	}

	FuncExpr *accumFunc = makeFuncExpr(
		functionOid, BsonTypeId(), args, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);
	windowFunc->args = list_make1(accumFunc);
	return windowFunc;
}


/*
 * Handle for $integral window aggregation operator.
 * Returns the WindowFunc for bson aggregate function `bsonintegral`
 */
static WindowFunc *
HandleDollarIntegralWindowOperator(const bson_value_t *opValue,
								   WindowOperatorContext *context)
{
	return GetIntegralDerivativeWindowFunc(opValue, context, true);
}


/*
 * Handle for $derivative window aggregation operator.
 * Returns the WindowFunc for bson aggregate function `bsonderivative`
 */
static WindowFunc *
HandleDollarDerivativeWindowOperator(const bson_value_t *opValue,
									 WindowOperatorContext *context)
{
	return GetIntegralDerivativeWindowFunc(opValue, context, false);
}


/*
 * Get the window function for the integral or derivative window aggregation operator.
 */
WindowFunc *
GetIntegralDerivativeWindowFunc(const bson_value_t *opValue,
								WindowOperatorContext *context,
								bool isIntegral)
{
	if (!(context->isWindowPresent || isIntegral))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("$derivative requires explicit window bounds")));
	}
	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = isIntegral ? BsonIntegralAggregateFunctionOid() :
						   BsonDerivativeAggregateFunctionOid();
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = true;
	Expr *xExpr = NULL;
	Expr *yExpr = NULL;
	long unitInMs = 0;
	ParseIntegralDerivativeExpression(opValue, context, &xExpr, &yExpr, &unitInMs,
									  isIntegral);

	Const *trueConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true), false,
								 true);
	Const *unitConst = (Const *) makeConst(INT8OID, -1, InvalidOid, sizeof(int64_t),
										   unitInMs, false, true);
	List *xArgs, *yArgs;
	Oid functionOid;
	if (context->variableContext != NULL)
	{
		functionOid = BsonExpressionGetWithLetFunctionOid();
		xArgs = list_make4(context->docExpr, xExpr, trueConst,
						   context->variableContext);
		yArgs = list_make4(context->docExpr, yExpr, trueConst,
						   context->variableContext);
	}
	else
	{
		functionOid = BsonExpressionGetFunctionOid();
		xArgs = list_make3(context->docExpr, xExpr, trueConst);
		yArgs = list_make3(context->docExpr, yExpr, trueConst);
	}

	FuncExpr *xAccumFunc = makeFuncExpr(
		functionOid, BsonTypeId(), xArgs, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);
	FuncExpr *yAccumFunc = makeFuncExpr(
		functionOid, BsonTypeId(), yArgs, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	windowFunc->args = list_make3(xAccumFunc, yAccumFunc, unitConst);
	return windowFunc;
}


/*
 * Parse the integral or derivative expression and return the expression.
 */
inline void
ParseIntegralDerivativeExpression(const bson_value_t *opValue,
								  WindowOperatorContext *context,
								  Expr **xExpr,        /* Pointer to pointer */
								  Expr **yExpr,        /* Pointer to pointer */
								  long *unitInMs,
								  bool isIntegralOperator)
{
	const char *operatorName = isIntegralOperator ? "$integral" : "$derivative";
	SetWindowFieldSortOption *sortField;
	if (!list_length(context->sortOptions))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("%s requires a sortBy", operatorName)));
	}
	else if (list_length(context->sortOptions) > 1)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("%s requires a non-compound sortBy", operatorName)));
	}
	else
	{
		sortField = (SetWindowFieldSortOption *) list_nth(
			context->sortOptions, 0);
	}

	pgbson *sortSpecBson = DatumGetPgBson(sortField->sortSpecConst->constvalue);
	pgbson *opValueBson = BsonValueToDocumentPgbson(opValue);
	bson_iter_t iterOpValue, iterSortSpec;

	PgbsonInitIterator(opValueBson, &iterOpValue);
	PgbsonInitIterator(sortSpecBson, &iterSortSpec);
	while (bson_iter_next(&iterOpValue))
	{
		const bson_value_t *valueUserInput = bson_iter_value(&iterOpValue);
		bson_iter_t valueIter;
		BsonValueInitIterator(valueUserInput, &valueIter);
		while (bson_iter_next(&valueIter))
		{
			const char *key = bson_iter_key(&valueIter);
			const bson_value_t *value = bson_iter_value(&valueIter);
			if (strcmp(key, "input") == 0)
			{
				*yExpr = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(value)); /* Dereference to modify the pointer */
			}
			else if (strcmp(key, "unit") == 0)
			{
				DateUnit unit = GetDateUnitFromString(value->value.v_utf8.str);
				if (unit == DateUnit_Invalid)
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
									errmsg("unknown time unit value: %s",
										   value->value.v_utf8.str),
									errdetail_log("unknown time unit value: %s",
												  value->value.v_utf8.str)));
				}
				else if (unit < DateUnit_Week)
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5490710), errmsg(
										"unit must be 'week' or smaller"),
									errdetail_log("unit must be 'week' or smaller")));
				}
				Datum interval = GetIntervalFromDateUnitAndAmount(unit, 1);
				float8 secondsInInterval = DatumGetFloat8(DirectFunctionCall2(
															  interval_part,
															  CStringGetTextDatum(EPOCH),
															  interval));
				*unitInMs = (long) (secondsInInterval * 1000);
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
								errmsg("%s got unexpected argument: %s", operatorName,
									   key)));
			}
		}
	}
	if (!(*yExpr))
	{
		ereport(ERROR, (
					errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
					errmsg(
						"%s requires an 'input' expression", operatorName)));
	}
	if (bson_iter_next(&iterSortSpec))
	{
		const char *key = bson_iter_key(&iterSortSpec);
		StringInfo result = makeStringInfo();
		appendStringInfo(result, "$%s", key);
		bson_value_t resultBsonValue;
		resultBsonValue.value_type = BSON_TYPE_UTF8;
		resultBsonValue.value.v_utf8.len = result->len;
		resultBsonValue.value.v_utf8.str = result->data;
		*xExpr = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(&resultBsonValue)); /* Dereference to modify the pointer */
		pfree(result->data);
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("%s requires a non-compound sortBy", operatorName)));
	}
}


/*
 * Handle for $sum window aggregation operator.
 * Returns the WindowFunc for bson aggregate function `bsonsum`
 */
static WindowFunc *
HandleDollarSumWindowOperator(const bson_value_t *opValue,
							  WindowOperatorContext *context)
{
	return GetSimpleBsonExpressionGetWindowFunc(opValue, context,
												BsonSumAggregateFunctionOid());
}


/*
 * Handle for $avg window aggregation operator.
 * Returns the WindowFunc for bson aggregate function `bsonavg`
 */
static WindowFunc *
HandleDollarAvgWindowOperator(const bson_value_t *opValue,
							  WindowOperatorContext *context)
{
	return GetSimpleBsonExpressionGetWindowFunc(opValue, context,
												BsonAvgAggregateFunctionOid());
}


/*
 * Handle for $count window aggregation operator.
 * Returns the WindowFunc for bson aggregate function `bsonsum`
 */
static WindowFunc *
HandleDollarCountWindowOperator(const bson_value_t *opValue,
								WindowOperatorContext *context)
{
	if (!IsBsonValueEmptyDocument(opValue))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("$count only accepts an empty object as input")));
	}

	bson_value_t newOpValue =
	{
		.value_type = BSON_TYPE_INT32,
		.value = { .v_int32 = 1 }
	};


	return GetSimpleBsonExpressionGetWindowFunc(&newOpValue, context,
												BsonSumAggregateFunctionOid());
}


static WindowFunc *
HandleDollarPushWindowOperator(const bson_value_t *opValue,
							   WindowOperatorContext *context)
{
	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = BsonArrayAggregateAllArgsFunctionOid();
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = true;

	Const *constValue = MakeBsonConst(BsonValueToDocumentPgbson(
										  opValue));

	/* empty values should not be converted to {"": null} values */
	Const *nullOnEmptyConst = (Const *) MakeBoolValueConst(false);

	List *funcArgs;
	Oid functionOid;

	if (context->variableContext != NULL)
	{
		functionOid = BsonExpressionGetWithLetFunctionOid();
		funcArgs = list_make4(context->docExpr, constValue, nullOnEmptyConst,
							  context->variableContext);
	}
	else
	{
		functionOid = BsonExpressionGetFunctionOid();
		funcArgs = list_make3(context->docExpr, constValue, nullOnEmptyConst);
	}

	FuncExpr *accumFunc = makeFuncExpr(
		functionOid, BsonTypeId(), funcArgs, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	bool handleSingleValue = true;
	List *aggregateArgs = list_make3(
		(Expr *) accumFunc,
		MakeTextConst(context->outputFieldName, strlen(context->outputFieldName)),
		MakeBoolValueConst(handleSingleValue));
	windowFunc->args = aggregateArgs;
	return windowFunc;
}


static WindowFunc *
HandleDollarAddToSetWindowOperator(const bson_value_t *opValue,
								   WindowOperatorContext *context)
{
	return GetSimpleBsonExpressionGetWindowFunc(opValue, context,
												BsonAddToSetAggregateFunctionOid());
}


/*
 *  Parse array input for $covariancePop and $covarianceSamp window operators
 */
static List *
ParseCovarianceWindowOperator(const bson_value_t *opValue, WindowOperatorContext *context)
{
	Expr *constXValue = NULL;
	Expr *constYValue = NULL;
	int varCount = 0;

	if (opValue->value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t opValueIter;
		BsonValueInitIterator(opValue, &opValueIter);
		while (bson_iter_next(&opValueIter))
		{
			varCount++;
			if (varCount == 1)
			{
				constXValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(
														 bson_iter_value(&opValueIter)));
				continue;
			}
			else if (varCount == 2)
			{
				constYValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(
														 bson_iter_value(&opValueIter)));
				continue;
			}
			else
			{
				break;
			}
		}
	}

	if (varCount != 2)
	/* for cases where arguments is not an array or with incorrect length, return null */
	{
		bson_value_t nullDocument = (bson_value_t) {
			.value_type = BSON_TYPE_NULL
		};
		constXValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(&nullDocument));
		constYValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(&nullDocument));
	}

	/* empty values should be converted to {"": null} values so that they're ignored*/
	Const *nullOnEmptyConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true),
										false, true);
	List *xArgs;
	List *yArgs;
	Oid functionOid;

	if (context->variableContext != NULL)
	{
		functionOid = BsonExpressionGetWithLetFunctionOid();
		xArgs = list_make4(context->docExpr, constXValue, nullOnEmptyConst,
						   context->variableContext);
		yArgs = list_make4(context->docExpr, constYValue, nullOnEmptyConst,
						   context->variableContext);
	}
	else
	{
		functionOid = BsonExpressionGetFunctionOid();
		xArgs = list_make3(context->docExpr, constXValue, nullOnEmptyConst);
		yArgs = list_make3(context->docExpr, constYValue, nullOnEmptyConst);
	}

	FuncExpr *xAccumFunc = makeFuncExpr(
		functionOid, BsonTypeId(), xArgs, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);
	FuncExpr *yAccumFunc = makeFuncExpr(
		functionOid, BsonTypeId(), yArgs, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	return list_make2(xAccumFunc, yAccumFunc);
}


/*
 * Handle for $covariancePop window aggregation operator.
 * Returns the WindowFunc for bson aggregate function `bsoncovariancepop`
 */
static WindowFunc *
HandleDollarCovariancePopWindowOperator(const bson_value_t *opValue,
										WindowOperatorContext *context)
{
	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = BsonCovariancePopAggregateFunctionOid();
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = true;

	windowFunc->args = ParseCovarianceWindowOperator(opValue, context);
	return windowFunc;
}


/*
 * Handle for $covarianceSamp window aggregation operator.
 * Returns the WindowFunc for bson aggregate function `bsonsum`
 */
static WindowFunc *
HandleDollarCovarianceSampWindowOperator(const bson_value_t *opValue,
										 WindowOperatorContext *context)
{
	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = BsonCovarianceSampAggregateFunctionOid();
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = true;

	windowFunc->args = ParseCovarianceWindowOperator(opValue, context);
	return windowFunc;
}


/*
 * Handle for $rank window aggregation operator.
 * Returns the WindowFunc for bson aggregate function `bson_dense_rank`
 */
static WindowFunc *
HandleDollarRankWindowOperator(const bson_value_t *opValue,
							   WindowOperatorContext *context)
{
	char *opName = "$rank";
	ValidateInputForRankFunctions(opValue, context, opName);
	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = BsonRankFunctionOid();
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = false;

	return windowFunc;
}


/*
 * Handle for $denseRank window aggregation operator.
 * Returns the WindowFunc for bson aggregate function `bson_rank`
 */
static WindowFunc *
HandleDollarDenseRankWindowOperator(const bson_value_t *opValue,
									WindowOperatorContext *context)
{
	char *opName = "$denseRank";
	ValidateInputForRankFunctions(opValue, context, opName);
	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = BsonDenseRankFunctionOid();
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = false;

	return windowFunc;
}


/*
 * Handle for $documentNumber window aggregation operator.
 * Returns the WindowFunc for bson aggregate function `bson_document_number`
 */
static WindowFunc *
HandleDollarDocumentNumberWindowOperator(const bson_value_t *opValue,
										 WindowOperatorContext *context)
{
	char *opName = "$documentNumber";
	ValidateInputForRankFunctions(opValue, context, opName);
	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = BsonDocumentNumberFunctionOid();
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = false;

	return windowFunc;
}


static inline void
ValidateInputForRankFunctions(const bson_value_t *opValue,
							  WindowOperatorContext *context, char *opName)
{
	if (context->isWindowPresent)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5371601),
						errmsg("Rank style window functions take no other arguments")));
	}

	if (!IsBsonValueEmptyDocument(opValue))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5371603),
						errmsg("(None) must be specified with '{}' as the value")));
	}

	if (list_length(context->sortOptions) != 1)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5371602),
						errmsg(
							"%s must be specified with a top level sortBy expression with exactly one element",
							opName)));
	}
}


/*
 * Handle for $expMovingAvg window aggregation operator.
 * $expMovingAvg syntax:
 * {
 *  $expMovingAvg: {
 *     input: <input expression>,
 *     N: <integer>,
 *     alpha: <float>
 *  }
 * }
 *
 */
static WindowFunc *
HandleDollarExpMovingAvgWindowOperator(const bson_value_t *opValue,
									   WindowOperatorContext *context)
{
	if (list_length(context->sortOptions) == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"$expMovingAvg requires an explicit 'sortBy'")));
	}

	/* $expMovingAvg is not support window parameter*/
	if (context->isWindowPresent == true || opValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"$expMovingAvg must have exactly one argument that is an object")));
	}

	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = BsonExpMovingAvgAggregateFunctionOid();
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = false;

	bson_value_t inputExpression = { 0 };
	bson_value_t weightExpression = { 0 };
	bson_value_t decimalWeightValue;

	/* Used to determine N or Alpha. */

	bool isAlpha = ParseInputWeightForExpMovingAvg(opValue, &inputExpression,
												   &weightExpression,
												   &decimalWeightValue);

	Expr *inputConstValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(
													   &inputExpression));

	Const *trueConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true), false,
								 true);

	/* If isAlpha == true, decimalWeightValue stores the value of Alpha. */
	/* If isAlpha == false, decimalWeightValue stores the value of N. */
	Const *weightConstValue = MakeBsonConst(BsonValueToDocumentPgbson(
												&decimalWeightValue));

	Const *isAlphaConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(isAlpha),
									false,
									true);

	List *argsInput;
	Oid functionOid;

	if (context->variableContext != NULL)
	{
		functionOid = BsonExpressionGetWithLetFunctionOid();
		argsInput = list_make4(context->docExpr, inputConstValue, trueConst,
							   context->variableContext);
	}
	else
	{
		functionOid = BsonExpressionGetFunctionOid();
		argsInput = list_make3(context->docExpr, inputConstValue, trueConst);
	}

	FuncExpr *accumFunc = makeFuncExpr(
		functionOid, BsonTypeId(), argsInput, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	windowFunc->args = list_make3(accumFunc, weightConstValue, isAlphaConst);
	return windowFunc;
}


/*
 * Handle for $linearFill window aggregation operator.
 */
static WindowFunc *
HandleDollarLinearFillWindowOperator(const bson_value_t *opValue,
									 WindowOperatorContext *context)
{
	if (list_length(context->sortOptions) != 1)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION605001),
						errmsg(
							"$linearFill must be specified with a top level sortBy expression with exactly one element")));
	}
	if (context->isWindowPresent)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"'window' field is not allowed in $linearFill")));
	}

	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = BsonLinearFillFunctionOid();
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = false;

	Expr *constValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(opValue)); \
	Const *trueConst = (Const *) MakeBoolValueConst(true);
	List *args;
	Oid functionOid;

	if (context->variableContext != NULL)
	{
		functionOid = BsonExpressionGetWithLetFunctionOid();
		args = list_make4(context->docExpr, constValue, trueConst,
						  context->variableContext);
	}
	else
	{
		functionOid = BsonExpressionGetFunctionOid();
		args = list_make3(context->docExpr, constValue, trueConst);
	}

	FuncExpr *accumFunc = makeFuncExpr(
		functionOid, BsonTypeId(), args, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	SetWindowFieldSortOption *sortOption = (SetWindowFieldSortOption *) linitial(
		context->sortOptions);
	List *sortExprArgs = list_make2(context->docExpr, sortOption->sortSpecConst);

	FuncExpr *sortExpr = makeFuncExpr(
		BsonOrderByFunctionOid(), BsonTypeId(), sortExprArgs, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	windowFunc->args = list_make2(accumFunc, sortExpr);
	return windowFunc;
}


/*
 * Handle for $locf window aggregation operator.
 */
static WindowFunc *
HandleDollarLocfFillWindowOperator(const bson_value_t *opValue,
								   WindowOperatorContext *context)
{
	if (context->isWindowPresent)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"'window' field is not allowed in $locf")));
	}

	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = BsonLocfFillFunctionOid();
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = false;

	Expr *constValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(opValue));

	Const *trueConst = (Const *) MakeBoolValueConst(true);
	List *args;
	Oid functionOid;

	if (context->variableContext != NULL)
	{
		functionOid = BsonExpressionGetWithLetFunctionOid();
		args = list_make4(context->docExpr, constValue, trueConst,
						  context->variableContext);
	}
	else
	{
		functionOid = BsonExpressionGetFunctionOid();
		args = list_make3(context->docExpr, constValue, trueConst);
	}

	FuncExpr *accumFunc = makeFuncExpr(
		functionOid, BsonTypeId(), args, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);
	windowFunc->args = list_make1(accumFunc);
	return windowFunc;
}


/*
 * Handler for $shift window aggregation operator.
 * $shift syntax:
 * {
 *  $shift: {
 *     output: <output expression>,
 *     by: <integer>,
 *     default: <default expression>
 *  }
 * }
 */
static WindowFunc *
HandleDollarShiftWindowOperator(const bson_value_t *opValue,
								WindowOperatorContext *context)
{
	if (context->isWindowPresent)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("$shift does not accept a 'window' field")));
	}

	if (context->sortOptions == NIL || list_length(context->sortOptions) < 1)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("'$shift' requires a sortBy")));
	}

	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = BsonShiftFunctionOid();
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = false;

	/* Parse accumulatorValue to pull out input/N*/
	bson_value_t output = { 0 };
	bson_value_t by = { 0 };
	bson_value_t defaultValue = { 0 };
	ParseInputDocumentForDollarShift(opValue, &output,
									 &by, &defaultValue);

	Expr *outConst = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(&output));
	Const *trueConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true), false,
								 true);

	List *args;
	Oid functionOid;

	if (context->variableContext != NULL)
	{
		functionOid = BsonExpressionGetWithLetFunctionOid();
		args = list_make4(context->docExpr, outConst, trueConst,
						  context->variableContext);
	}
	else
	{
		functionOid = BsonExpressionGetFunctionOid();
		args = list_make3(context->docExpr, outConst, trueConst);
	}

	Const *byConst = makeConst(INT4OID, -1, InvalidOid, sizeof(int32_t),
							   Int32GetDatum(by.value.v_int32), false, true);
	Expr *defaultConst = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(&defaultValue));

	FuncExpr *accumFunc = makeFuncExpr(
		functionOid, BsonTypeId(), args, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	List *aggregateArgs = list_make3(
		(Expr *) accumFunc,
		byConst,
		defaultConst);
	windowFunc->args = aggregateArgs;
	return windowFunc;
}


/**
 * Parses the input document for $shift windpw aggregation operator and extracts the value for output, by and default.
 * @param opValue: input document for the $shift operator.
 * @param output:  this is a pointer which after parsing will hold output expression.
 * @param by: this is a pointer which after parsing will hold by i.e. shift offset value.
 * @param defaultValue: this is a pointer which after parsing will hold default value in case by value is out of partition bounds.
 */
static void
ParseInputDocumentForDollarShift(const bson_value_t *opValue, bson_value_t *output,
								 bson_value_t *by, bson_value_t *defaultValue)
{
	if (opValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("Argument to $shift must be an object")));
	}

	bson_iter_t docIter;
	BsonValueInitIterator(opValue, &docIter);
	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "output") == 0)
		{
			*output = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "by") == 0)
		{
			*by = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "default") == 0)
		{
			*defaultValue = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("Unknown argument in $shift")));
		}
	}

	if (output->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("$shift requires an 'output' expression.")));
	}

	if (by->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("$shift requires 'by' as an integer value.")));
	}

	if (defaultValue->value_type == BSON_TYPE_EOD)
	{
		defaultValue->value_type = BSON_TYPE_NULL;
	}

	bool checkFixedInteger = true;
	if (!IsBsonValue32BitInteger(by, checkFixedInteger))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("'$shift:by' field must be an integer, but found by: %s",
							   BsonValueToJsonForLogging(by)),
						errdetail_log(
							"'$shift:by' field must be an integer, but found by: %s",
							BsonValueToJsonForLogging(by))));
	}

	/* by value should be a 32 bit integer */
	by->value.v_int32 = BsonValueAsInt32WithRoundingMode(by,
														 ConversionRoundingMode_Floor);
	by->value_type = BSON_TYPE_INT32;

	AggregationExpressionData *expressionData =
		palloc0(sizeof(AggregationExpressionData));
	ParseAggregationExpressionData(expressionData, defaultValue, NULL);

	if (!IsAggregationExpressionConstant(expressionData))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"'$shift:default' expression must yield a constant value.")));
	}

	pfree(expressionData);
}


/*
 * Handler for $topN window aggregation operator.
 * input format:
 * {
 *  $topN:
 *     {
 *        output: <expression>,
 *        n: <expression>,
 *        sortBy: { <field1>: <sort order>, <field2>: <sort order> ... },
 *     }
 *	}
 */
static WindowFunc *
HandleDollarTopNWindowOperator(const bson_value_t *opValue,
							   WindowOperatorContext *context)
{
	bool isNOperator = true;
	return HandleDollarTopBottomOperators(opValue, context, "$topN",
										  BsonFirstNAggregateAllArgsFunctionOid(),
										  isNOperator);
}


/*
 * Handler for $bottomN window aggregation operator.
 * input format:
 * {
 *  $bottomN:
 *     {
 *        output: <expression>,
 *        n: <expression>,
 *        sortBy: { <field1>: <sort order>, <field2>: <sort order> ... },
 *     }
 *	}
 */
static WindowFunc *
HandleDollarBottomNWindowOperator(const bson_value_t *opValue,
								  WindowOperatorContext *context)
{
	bool isNOperator = true;
	return HandleDollarTopBottomOperators(opValue, context, "$bottomN",
										  BsonLastNAggregateAllArgsFunctionOid(),
										  isNOperator);
}


/*
 * Handler for $top window aggregation operator.
 * input format:
 * {
 *  $top:
 *     {
 *        output: <expression>,
 *        sortBy: { <field1>: <sort order>, <field2>: <sort order> ... },
 *     }
 *	}
 */
static WindowFunc *
HandleDollarTopWindowOperator(const bson_value_t *opValue,
							  WindowOperatorContext *context)
{
	bool isNOperator = false;
	return HandleDollarTopBottomOperators(opValue, context, "$top",
										  BsonFirstAggregateAllArgsFunctionOid(),
										  isNOperator);
}


/*
 * Handler for $bottom window aggregation operator.
 * input format:
 * {
 *  $bottom:
 *     {
 *        output: <expression>,
 *        sortBy: { <field1>: <sort order>, <field2>: <sort order> ... },
 *     }
 *	}
 */
static WindowFunc *
HandleDollarBottomWindowOperator(const bson_value_t *opValue,
								 WindowOperatorContext *context)
{
	bool isNOperator = false;
	return HandleDollarTopBottomOperators(opValue, context, "$bottom",
										  BsonLastAggregateAllArgsFunctionOid(),
										  isNOperator);
}


/*
 * Common handler for $top(N) and $bottom(N) operators.
 * Parses the spec and generates the WindowFunc for the respective bson aggregate function.
 *
 * TODO: Support n(elementsToFetch) as an expression
 */
static WindowFunc *
HandleDollarTopBottomOperators(const bson_value_t *opValue,
							   WindowOperatorContext *context,
							   const char *opName,
							   Oid aggregateFunctionOid,
							   bool isNOperator)
{
	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = aggregateFunctionOid;
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = true;

	/* Parse accumulatorValue to pull out input/N*/
	bson_value_t output = { 0 };
	bson_value_t elementsToFetch = { 0 };
	bson_value_t sortSpec = { 0 };
	ParseInputDocumentForTopAndBottom(opValue, &output,
									  &elementsToFetch, &sortSpec, opName);

	int nelems = BsonDocumentValueCountKeys(&sortSpec);
	Datum *sortDatumArray = palloc(sizeof(Datum) * nelems);

	bson_iter_t sortIter;
	BsonValueInitIterator(&sortSpec, &sortIter);
	int i = 0;
	while (bson_iter_next(&sortIter))
	{
		pgbsonelement sortElement = { 0 };
		sortElement.path = bson_iter_key(&sortIter);
		sortElement.pathLength = strlen(sortElement.path);
		sortElement.bsonValue = *bson_iter_value(&sortIter);
		sortDatumArray[i] = PointerGetDatum(PgbsonElementToPgbson(&sortElement));
		i++;
	}

	ArrayType *arrayValue = construct_array(sortDatumArray, nelems, BsonTypeId(), -1,
											false, TYPALIGN_INT);
	Const *sortArrayConst = makeConst(GetBsonArrayTypeOid(), -1, InvalidOid, -1,
									  PointerGetDatum(arrayValue), false, false);

	Const *constValue = MakeBsonConst(BsonValueToDocumentPgbson(&output));

	if (isNOperator)
	{
		ValidateElementForNGroupAccumulators(&elementsToFetch, opName);
		Const *nConst = makeConst(INT8OID, -1, InvalidOid, sizeof(int64_t),
								  Int64GetDatum(elementsToFetch.value.v_int64), false,
								  true);

		windowFunc->args = list_make4(context->docExpr, nConst, sortArrayConst,
									  constValue);
	}
	else
	{
		windowFunc->args = list_make3(context->docExpr, sortArrayConst, constValue);
	}

	return windowFunc;
}


/*
 *  Parse input for $stdDevPop and $stdDevSamp window operators
 */
static List *
ParseStdDevWindowOperator(const bson_value_t *opValue, WindowOperatorContext *context)
{
	Expr *constXValue = NULL;

	/* we treat the input array as null */
	if (opValue->value_type == BSON_TYPE_ARRAY)
	{
		bson_value_t nullDocument = (bson_value_t) {
			.value_type = BSON_TYPE_NULL
		};
		constXValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(&nullDocument));
	}
	else
	{
		constXValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(opValue));
	}

	/* empty values should be converted to {"": null} values so that they're ignored*/
	Const *nullOnEmptyConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true),
										false, true);
	List *xArgs;
	Oid functionOid;

	if (context->variableContext != NULL)
	{
		functionOid = BsonExpressionGetWithLetFunctionOid();
		xArgs = list_make4(context->docExpr, constXValue, nullOnEmptyConst,
						   context->variableContext);
	}
	else
	{
		functionOid = BsonExpressionGetFunctionOid();
		xArgs = list_make3(context->docExpr, constXValue, nullOnEmptyConst);
	}

	FuncExpr *xAccumFunc = makeFuncExpr(
		functionOid, BsonTypeId(), xArgs, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	return list_make1(xAccumFunc);
}


/*
 * Handle for $stdDevPop window aggregation operator.
 * Returns the WindowFunc for bson aggregate function `bsonstdDevPop`
 */
static WindowFunc *
HandleDollarStdDevPopWindowOperator(const bson_value_t *opValue,
									WindowOperatorContext *context)
{
	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = BsonStdDevPopAggregateFunctionOid();
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = true;

	windowFunc->args = ParseStdDevWindowOperator(opValue, context);
	return windowFunc;
}


/*
 * Handle for $stdDevSamp window aggregation operator.
 * Returns the WindowFunc for bson aggregate function `bsonstdDevSamp`
 */
static WindowFunc *
HandleDollarStdDevSampWindowOperator(const bson_value_t *opValue,
									 WindowOperatorContext *context)
{
	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = BsonStdDevSampAggregateFunctionOid();
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = true;

	windowFunc->args = ParseStdDevWindowOperator(opValue, context);
	return windowFunc;
}


/**
 * Function that check the syntax of MaxN/MinN.
 * @param opValue: input document for the $MaxN/$MinN accumulator.
 * @param opName: "maxN" or "minN".
 */
static void
ValidteForMaxNMinNNAccumulators(const bson_value_t *opValue, const char *opName)
{
	if (opValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5787900),
						errmsg("specification must be an object; found %s: %s", opName,
							   BsonValueToJsonForLogging(opValue)),
						errdetail_log(
							"specification must be an object; opname: %s type found: %s",
							opName,
							BsonTypeName(opValue->value_type))));
	}
	bson_iter_t docIter;
	BsonValueInitIterator(opValue, &docIter);

	bson_value_t input = { 0 };
	bson_value_t elementsToFetch = { 0 };
	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "input") == 0)
		{
			input = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "n") == 0)
		{
			elementsToFetch = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5787901),
							errmsg("%s found an unknown argument: %s", opName, key),
							errdetail_log("%s found an unknown argument", opName)));
		}
	}

	/**
	 * Validation check to see if input and elements to fetch are present otherwise throw error.
	 */
	if (input.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5787907),
						errmsg("%s requires an 'input' field", opName),
						errdetail_log("%s requires an 'input' field", opName)));
	}

	if (elementsToFetch.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5787906),
						errmsg("%s requires an 'n' field", opName),
						errdetail_log("%s requires an 'n' field", opName)));
	}
}


/*
 * Handle for $MaxN window aggregation operator.
 */
static WindowFunc *
HandleDollarMaxNWindowOperator(const bson_value_t *opValue,
							   WindowOperatorContext *context)
{
	/*check the syntax of maxN/minN */
	ValidteForMaxNMinNNAccumulators(opValue, "maxN");

	/*reuse the logic of maxN in the group stage.*/
	return GetSimpleBsonExpressionGetWindowFunc(opValue, context,
												BsonMaxNAggregateFunctionOid());
}


/*
 * Handle for $MinN window aggregation operator.
 */
static WindowFunc *
HandleDollarMinNWindowOperator(const bson_value_t *opValue,
							   WindowOperatorContext *context)
{
	/*check the syntax of maxN/minN */
	ValidteForMaxNMinNNAccumulators(opValue, "minN");

	/*reuse the logic of minN in the group stage.*/
	return GetSimpleBsonExpressionGetWindowFunc(opValue, context,
												BsonMinNAggregateFunctionOid());
}


/*
 * Handler for $first window aggregation operator.
 * input format: { $first: <expression> }
 */
static WindowFunc *
HandleDollarFirstWindowOperator(const bson_value_t *opValue,
								WindowOperatorContext *context)
{
	bool isNOperator = false;
	Oid functionOid;
	if (context->sortOptions != NIL && list_length(context->sortOptions) > 0)
	{
		functionOid = BsonFirstAggregateAllArgsFunctionOid();
	}
	else
	{
		functionOid = BsonFirstOnSortedAggregateAllArgsFunctionOid();
	}

	return HandleDollarFirstLastOperators(opValue, context, "$first", functionOid,
										  isNOperator);
}


/*
 * Handler for $last window aggregation operator.
 * input format: { $last: <expression> }
 */
static WindowFunc *
HandleDollarLastWindowOperator(const bson_value_t *opValue,
							   WindowOperatorContext *context)
{
	bool isNOperator = false;
	Oid functionOid;
	if (context->sortOptions != NIL && list_length(context->sortOptions) > 0)
	{
		functionOid = BsonLastAggregateAllArgsFunctionOid();
	}
	else
	{
		functionOid = BsonLastOnSortedAggregateAllArgsFunctionOid();
	}

	return HandleDollarFirstLastOperators(opValue, context, "$last", functionOid,
										  isNOperator);
}


/*
 * Handler for $firstN window aggregation operator.
 * input format:
 * {
 *  $firstN:
 *     {
 *        input: <expression>,
 *        n: <expression>
 *     }
 *	}
 */
static WindowFunc *
HandleDollarFirstNWindowOperator(const bson_value_t *opValue,
								 WindowOperatorContext *context)
{
	bool isNOperator = true;
	Oid functionOid;
	if (context->sortOptions != NIL && list_length(context->sortOptions) > 0)
	{
		functionOid = BsonFirstNAggregateAllArgsFunctionOid();
	}
	else
	{
		functionOid = BsonFirstNOnSortedAggregateAllArgsFunctionOid();
	}

	return HandleDollarFirstLastOperators(opValue, context, "$firstN", functionOid,
										  isNOperator);
}


/*
 * Handler for $lastN window aggregation operator.
 * input format:
 * {
 *  $lastN:
 *     {
 *        input: <expression>,
 *        n: <expression>
 *     }
 *	}
 */
static WindowFunc *
HandleDollarLastNWindowOperator(const bson_value_t *opValue,
								WindowOperatorContext *context)
{
	bool isNOperator = true;
	Oid functionOid;
	if (context->sortOptions != NIL && list_length(context->sortOptions) > 0)
	{
		functionOid = BsonLastNAggregateAllArgsFunctionOid();
	}
	else
	{
		functionOid = BsonLastNOnSortedAggregateAllArgsFunctionOid();
	}

	return HandleDollarFirstLastOperators(opValue, context, "$lastN", functionOid,
										  isNOperator);
}


/*
 * Common handler for $first(N) and $last(N) operators.
 * Parses the spec and generates the WindowFunc for the respective bson aggregate function.
 *
 * TODO: Support n(elementsToFetch) as an expression
 */
static WindowFunc *
HandleDollarFirstLastOperators(const bson_value_t *opValue,
							   WindowOperatorContext *context,
							   const char *opName,
							   Oid aggregateFunctionOid,
							   bool isNOperator)
{
	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->wintype = BsonTypeId();
	windowFunc->winfnoid = aggregateFunctionOid;
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = true;

	Const *constValue, *nConst;
	if (isNOperator)
	{
		bson_value_t input = { 0 };
		bson_value_t elementsToFetch = { 0 };
		ParseInputForNGroupAccumulators(opValue, &input, &elementsToFetch, opName);
		ValidateElementForNGroupAccumulators(&elementsToFetch, opName);
		nConst = makeConst(INT8OID, -1, InvalidOid, sizeof(int64_t),
						   Int64GetDatum(elementsToFetch.value.v_int64), false,
						   true);

		constValue = MakeBsonConst(BsonValueToDocumentPgbson(&input));
	}
	else
	{
		nConst = NULL;
		constValue = MakeBsonConst(BsonValueToDocumentPgbson(opValue));
	}

	if (context->sortOptions != NIL && list_length(context->sortOptions) > 0)
	{
		int nelems = list_length(context->sortOptions);
		Datum *sortDatumArray = palloc(sizeof(Datum) * nelems);

		ListCell *lc;
		int i = 0;
		foreach(lc, context->sortOptions)
		{
			SetWindowFieldSortOption *sortOption =
				(SetWindowFieldSortOption *) lfirst(lc);
			sortDatumArray[i] = sortOption->sortSpecConst->constvalue;
			i++;
		}

		ArrayType *arrayValue = construct_array(sortDatumArray, nelems, BsonTypeId(), -1,
												false, TYPALIGN_INT);
		Const *sortArrayConst = makeConst(GetBsonArrayTypeOid(), -1, InvalidOid, -1,
										  PointerGetDatum(arrayValue), false, false);

		if (isNOperator)
		{
			windowFunc->args = list_make4(context->docExpr, nConst, sortArrayConst,
										  constValue);
		}
		else
		{
			windowFunc->args = list_make3(context->docExpr, sortArrayConst, constValue);
		}
	}
	else
	{
		if (isNOperator)
		{
			windowFunc->args = list_make3(context->docExpr, nConst, constValue);
		}
		else
		{
			windowFunc->args = list_make2(context->docExpr, constValue);
		}
	}

	return windowFunc;
}


/*
 * Handle for const fill window aggregation operator.
 * Returns the WindowFunc for bson aggregate function `bsonsum`
 */
static WindowFunc *
HandleDollarConstFillWindowOperator(const bson_value_t *opValue,
									WindowOperatorContext *context)
{
	if (!context->enableInternalWindowOperator)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("Unrecognized window function, $_internal_constFill.")));
	}
	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = BsonConstFillFunctionOid();
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = context->winRef;
	windowFunc->winstar = false;
	windowFunc->winagg = false;

	bson_value_t fillValue = { 0 };
	bson_value_t path = { 0 };

	bson_iter_t opSpeciter;
	BsonValueInitIterator(opValue, &opSpeciter);
	while (bson_iter_next(&opSpeciter))
	{
		const char *key = bson_iter_key(&opSpeciter);
		const bson_value_t *value = bson_iter_value(&opSpeciter);
		if (strcmp(key, "value") == 0)
		{
			fillValue = *value;
		}
		else if (strcmp(key, "path") == 0)
		{
			path = *value;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("Unknown field %s in $constFill", key)));
		}
	}

	Expr *constValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(&path));
	Expr *filledValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(&fillValue));
	Const *trueConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true), false,
								 true);
	Const *falseConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(false), false,
								  true);

	List *args;
	List *argsFilled;
	Oid functionOid;

	if (context->variableContext != NULL)
	{
		functionOid = BsonExpressionGetWithLetFunctionOid();
		args = list_make4(context->docExpr, constValue, trueConst,
						  context->variableContext);
		argsFilled = list_make4(context->docExpr, filledValue, falseConst,
								context->variableContext);
	}
	else
	{
		functionOid = BsonExpressionGetFunctionOid();
		args = list_make3(context->docExpr, constValue, trueConst);
		argsFilled = list_make3(context->docExpr, filledValue, falseConst);
	}

	FuncExpr *accumFunc = makeFuncExpr(
		functionOid, BsonTypeId(), args, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	FuncExpr *filledExpr = makeFuncExpr(
		functionOid, BsonTypeId(), argsFilled, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	windowFunc->args = list_make2(accumFunc, filledExpr);
	return windowFunc;
}


/*
 * Handle the $min window operator. This uses the existing
 * `bsonmin` aggregate function.
 *
 * TODO: This is just a quick implementation. Later add inverse transition
 * to make it performant for moving windows
 */
static WindowFunc *
HandleDollarMinWindowOperator(const bson_value_t *opValue,
							  WindowOperatorContext *context)
{
	/*reuse the logic of min in the group stage.*/
	return GetSimpleBsonExpressionGetWindowFunc(opValue, context,
												BsonMinAggregateFunctionOid());
}


/*
 * Handle the $max window operator. This uses the existing
 * `bsonmax` aggregate function.
 *
 * TODO: This is just a quick implementation. Later add inverse transition
 * to make it performant for moving windows
 */
static WindowFunc *
HandleDollarMaxWindowOperator(const bson_value_t *opValue,
							  WindowOperatorContext *context)
{
	return GetSimpleBsonExpressionGetWindowFunc(opValue, context,
												BsonMaxAggregateFunctionOid());
}
