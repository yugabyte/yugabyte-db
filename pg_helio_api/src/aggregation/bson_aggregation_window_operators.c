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

#include "aggregation/bson_aggregation_window_operators.h"
#include "aggregation/bson_aggregation_statistics.h"
#include "commands/parse_error.h"
#include "query/helio_bson_compare.h"
#include "query/query_operator.h"
#include "utils/date_utils.h"
#include "utils/feature_counter.h"
#include "utils/mongo_errors.h"
#include "types/decimal128.h"

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


/* GUC to enable SetWindowFields stage */
extern bool EnableSetWindowFields;

/*
 * Window operators definitions.
 *
 * Note: Please keep the list in alphabetical order for better readability.
 */
static const WindowOperatorDefinition WindowOperatorDefinitions[] =
{
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
		.windowOperatorFunc = NULL
	},
	{
		.operatorName = "$bottomN",
		.windowOperatorFunc = NULL
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
		.windowOperatorFunc = NULL
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
		.windowOperatorFunc = NULL
	},
	{
		.operatorName = "$firstN",
		.windowOperatorFunc = NULL
	},
	{
		.operatorName = "$integral",
		.windowOperatorFunc = NULL
	},
	{
		.operatorName = "$last",
		.windowOperatorFunc = NULL
	},
	{
		.operatorName = "$lastN",
		.windowOperatorFunc = NULL
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
		.windowOperatorFunc = NULL
	},
	{
		.operatorName = "$maxN",
		.windowOperatorFunc = NULL
	},
	{
		.operatorName = "$median",
		.windowOperatorFunc = NULL
	},
	{
		.operatorName = "$min",
		.windowOperatorFunc = NULL
	},
	{
		.operatorName = "$minN",
		.windowOperatorFunc = NULL
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
		.windowOperatorFunc = NULL
	},
	{
		.operatorName = "$stdDevPop",
		.windowOperatorFunc = NULL
	},
	{
		.operatorName = "$stdDevSamp",
		.windowOperatorFunc = NULL
	},
	{
		.operatorName = "$sum",
		.windowOperatorFunc = &HandleDollarSumWindowOperator
	},
	{
		.operatorName = "$top",
		.windowOperatorFunc = NULL
	},
	{
		.operatorName = "$topN",
		.windowOperatorFunc = NULL
	}
};

static const int WindowOperatorsCount = sizeof(WindowOperatorDefinitions) /
										sizeof(WindowOperatorDefinition);


/*
 * Ensures the given window spec of $setWindowFields' output is valid combination
 */
static inline void
EnsureValidWindowSpec(int frameOptions, WindowOperatorContext *context)
{
	if ((frameOptions & FRAMEOPTION_ROWS) != FRAMEOPTION_ROWS &&
		(frameOptions & FRAMEOPTION_RANGE) != FRAMEOPTION_RANGE &&
		(frameOptions & FRAMEOPTION_MONGO_UNKNOWN_FIELDS) ==
		FRAMEOPTION_MONGO_UNKNOWN_FIELDS)
	{
		/* Only unknwon fields are provided */
		ereport(ERROR, (
					errcode(MongoFailedToParse),
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
					errcode(MongoFailedToParse),
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
			ereport(ERROR, (errcode(MongoLocation5339902),
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
			ereport(ERROR, (errcode(MongoFailedToParse),
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
			ereport(ERROR, (errcode(MongoLocation5339901),
							errmsg("Document-based bounds require a sortBy")));
		}
	}
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
 * SELECT helio_api_internal.bson_dollar_merge_documents(document, mongo_catalog.bson_repath_and_build(<field1>::text, total)) AS document
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
 * the query is a single shard query.
 */
Query *
HandleSetWindowFields(const bson_value_t *existingValue, Query *query,
					  AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_SETWINDOWFIELDS);

	if (!EnableSetWindowFields || !IsClusterVersionAtleastThis(1, 19, 0))
	{
		ereport(ERROR, (errcode(MongoCommandNotSupported),
						errmsg(
							"$setWindowFields is not supported yet.")));
	}

	if (existingValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg(
							"the $setWindowFields stage specification must be an object, "
							"found %s", BsonTypeName(existingValue->value_type)),
						errhint(
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
	Expr *partitionExpr = NULL;
	List *sortOptions = NIL;

	bson_iter_t iter;
	BsonValueInitIterator(existingValue, &iter);
	while (bson_iter_next(&iter))
	{
		const char *key = bson_iter_key(&iter);
		const bson_value_t *value = bson_iter_value(&iter);
		if (strcmp(key, "partitionBy") == 0)
		{
			if (value->value_type == BSON_TYPE_ARRAY)
			{
				ereport(ERROR, (
							errcode(MongoTypeMismatch),
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
												 MONGO_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER,
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
			ereport(ERROR, (errcode(MongoUnknownBsonField),
							errmsg(
								"BSON field '$setWindowFields.%s' is an unknown field.",
								key),
							errhint(
								"BSON field '$setWindowFields.%s' is an unknown field.",
								key)));
		}
	}

	/* Required fields check */
	if (outputSpec.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation40414),
						errmsg(
							"BSON field '$setWindowFields.output' is missing but a required field")));
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
			ereport(ERROR, (errcode(MongoLocation40352),
							errmsg("FieldPath cannot be constructed with empty string")));
		}

		if (output.path[0] == '$')
		{
			ereport(ERROR, (errcode(MongoLocation16410),
							errmsg(
								"FieldPath field names may not start with '$'. Consider using $getField or $setField.")));
		}

		if (output.bsonValue.value_type != BSON_TYPE_DOCUMENT)
		{
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg("The field '%s' must be an object", output.path),
							errhint("$setWindowField output field must be an object")));
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
		FuncExpr *repathExpr = makeFuncExpr(BsonRepathAndBuildFunctionOid(),
											BsonTypeId(),
											repathArgs, InvalidOid, InvalidOid,
											COERCE_EXPLICIT_CALL);
		FuncExpr *mergeDocumentsExpr = makeFuncExpr(GetMergeDocumentsFunctionOid(),
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
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg("Window function found an unknown argument: %s", key),
							errhint("Window function found an unknown argument")));
		}
	}

	if (operatorValue.bsonValue.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
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
			ereport(ERROR, (errcode(MongoFailedToParse),
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

	/* In the window frameOptions strip vCore specific options */
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
		ereport(ERROR, (errcode(MongoFailedToParse),
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
							errcode(MongoFailedToParse),
							errmsg("Numeric document-based bounds must be an integer")));
			}
			else
			{
				ereport(ERROR, (
							errcode(MongoFailedToParse),
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
							errcode(MongoFailedToParse),
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
								errcode(MongoFailedToParse),
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
					errcode(MongoLocation5339900),
					errmsg("Lower bound must not exceed upper bound: %s",
						   BsonValueToJsonForLogging(value)),
					errhint("Lower bound must not exceed upper bound.")));
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
				errcode(MongoFailedToParse),
				errmsg(
					"Window bounds can specify either 'documents' or 'unit', not both.")));
}


/* Helper method that throws the error extra frame options */
static void
pg_attribute_noreturn()
ThrowExtraInvalidFrameOptions(const char * str1, const char * str2)
{
	ereport(ERROR, (
				errcode(MongoFailedToParse),
				errmsg("'window' field that specifies %s cannot have other fields %s",
					   str1, str2),
				errhint("'window' field that specifies %s cannot have other fields %s",
						str1, str2)));
}


static void
pg_attribute_noreturn()
ThrowInvalidWindowValue(const char * windowType, const bson_value_t * value)
{
	ereport(ERROR, (errcode(MongoFailedToParse),
					errmsg("Window bounds must be a 2-element array: %s: %s",
						   windowType,
						   BsonValueToJsonForLogging(value)),
					errhint("Window bounds must be a 2-element array: %s: %s",
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
				ereport(ERROR, (errcode(MongoFailedToParse),
								errmsg("'unit' must be a string")));
			}
			dateUnit = GetDateUnitFromString(windowValue->value.v_utf8.str);

			if (dateUnit == DateUnit_Invalid)
			{
				ereport(ERROR, (errcode(MongoFailedToParse),
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
	EnsureValidWindowSpec(*frameOptions, context);

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
							errcode(MongoCommandNotSupported),
							errmsg("Window operator %s is not supported yet",
								   element->path),
							errhint("Window operator %s is not supported yet",
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
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg("Unrecognized window function, %s", element->path),
						errhint("Unrecognized window function, %s", element->path)));
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
	Expr *constValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(opValue));

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
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("$count only accepts an empty object as input")));
	}

	bson_value_t newOpValue =
	{
		value_type: BSON_TYPE_INT32,
		value: { v_int32: 1 }
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

	Expr *constValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(
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
	if (!IsClusterVersionAtleastThis(1, 21, 0))
	{
		ereport(ERROR, (errcode(MongoCommandNotSupported),
						errmsg("Window operator $covariancePop is not supported yet")));
	}

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
	if (!IsClusterVersionAtleastThis(1, 21, 0))
	{
		ereport(ERROR, (errcode(MongoCommandNotSupported),
						errmsg("Window operator $covarianceSamp is not supported yet")));
	}
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
	if (!IsClusterVersionAtleastThis(1, 21, 0))
	{
		ereport(ERROR, (errcode(MongoCommandNotSupported),
						errmsg("$rank is not supported yet")));
	}
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
	if (!IsClusterVersionAtleastThis(1, 21, 0))
	{
		ereport(ERROR, (errcode(MongoCommandNotSupported),
						errmsg("$denseRank is not supported yet")));
	}
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
	if (!IsClusterVersionAtleastThis(1, 22, 0))
	{
		ereport(ERROR, (errcode(MongoCommandNotSupported),
						errmsg("$documentNumber is not supported yet")));
	}
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
		ereport(ERROR, (errcode(MongoLocation5371601),
						errmsg("Rank style window functions take no other arguments")));
	}

	if (!IsBsonValueEmptyDocument(opValue))
	{
		ereport(ERROR, (errcode(MongoLocation5371603),
						errmsg("(None) must be specified with '{}' as the value")));
	}

	if (list_length(context->sortOptions) != 1)
	{
		ereport(ERROR, (errcode(MongoLocation5371602),
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
	if (!(IsClusterVersionAtleastThis(1, 22, 0)))
	{
		ereport(ERROR, (errcode(MongoCommandNotSupported),
						errmsg(
							"$expMovingAvg is only supported on vCore 1.21.0 and above")));
	}

	if (list_length(context->sortOptions) == 0)
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg(
							"$expMovingAvg requires an explicit 'sortBy'")));
	}

	/* $expMovingAvg is not support window parameter*/
	if (context->isWindowPresent == true || opValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
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
	if (!(IsClusterVersionAtleastThis(1, 22, 0)))
	{
		ereport(ERROR, (errcode(MongoLocation605001),
						errmsg(
							" $linearFill is only supported on vCore 1.21.0 and above")));
	}
	if (list_length(context->sortOptions) != 1)
	{
		ereport(ERROR, (errcode(MongoLocation605001),
						errmsg(
							" $linearFill must be specified with a top level sortBy expression with exactly one element")));
	}
	if (context->isWindowPresent)
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg(
							" 'window' field is not allowed in $linearFill")));
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
	if (!(IsClusterVersionAtleastThis(1, 22, 0)))
	{
		ereport(ERROR, (errcode(MongoLocation605001),
						errmsg(
							" $locf is only supported on vCore 1.21.0 and above")));
	}
	if (context->isWindowPresent)
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg(
							" 'window' field is not allowed in $locf")));
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
