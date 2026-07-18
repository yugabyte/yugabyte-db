/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/aggregation/bson_bucket_auto.c
 *
 * Implementation of the $bucketAuto stage against every document in the aggregation.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <nodes/execnodes.h>
#include <parser/parse_clause.h>
#include <parser/parse_node.h>
#include <windowapi.h>

#include "aggregation/bson_project.h"
#include "commands/parse_error.h"
#include "io/bson_core.h"
#include "metadata/metadata_cache.h"
#include "query/bson_compare.h"
#include "utils/fmgr_utils.h"
#include "utils/feature_counter.h"
#include "utils/documentdb_errors.h"

#include "aggregation/bson_bucket_auto.h"

/*
 * Structure for $bucketAuto specs.
 */
typedef struct
{
	/* groupBy field */
	bson_value_t groupBy;

	/* number of buckets wanted, the actual number of buckets in result may be different */
	int32 numBuckets;

	/* granularity, example: POWERSOF2, R5 */
	StringView granularity;
} BucketAutoArguments;

/*
 * bucketAuto processing context.
 */
typedef struct
{
	/*
	 * below fields are common context, only need to be initialized once at first call.
	 */

	/* total rows unprocessed */
	int64 totalRows;

	/* number of buckets, this may be different with the numBuckets specified by query */
	int32 nBuckets;

	/* (totalRows) / (nBuckets), how many rows should be in the bucket when distributed evenly, it may exceed this num for certain cases */
	int64 expectRowsLimit;

	/* (totalRows) % (nBuckets) */
	int64 remainder;

	/*
	 * below fields are bucket specified context
	 */
	int32 bucketId;

	/* row index in current bucket, starting from 1 */
	int64 rowIndex;

	/* actual rows should be in the bucket */
	int64 actualRowsLimit;

	/* lower bound of current bucket */
	pgbson *lower_bound;

	/* upper bound of current bucket */
	pgbson *upper_bound;

	/*
	 * memory context exists during query execution
	 */
	MemoryContext mcxt;
} BucketAutoState;

static const char *BUCKETAUTO_BUCKET_ID_FIELD = "bucket_id";
static const StringView BUCKETAUTO_GRANULARITY_SUPPORTED_TYPES[] = {
	{ "POWERSOF2", 9 },
	{ "1-2-5", 5 },
	{ "R5", 2 },
	{ "R10", 3 },
	{ "R20", 3 },
	{ "R40", 3 },
	{ "R80", 3 },
	{ "E6", 2 },
	{ "E12", 3 },
	{ "E24", 3 },
	{ "E48", 3 },
	{ "E96", 3 },
	{ "E192", 4 }
};

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static Query * BuildBucketAutoQuery(Query *query,
									AggregationPipelineBuildContext *
									context, const
									bson_value_t *groupBy, const
									bson_value_t *bucketAutoSpec);

static void BuildBucketAutoGroupSpec(const bson_value_t *output, bson_value_t *groupSpec);

static void SetLowerBound(const pgbson *currentValue, const BucketAutoArguments *args,
						  BucketAutoState *state);

static void SetUpperBound(WindowObject winobj, const BucketAutoArguments *args,
						  BucketAutoState *state);

static void InitializeBucketAutoArguments(BucketAutoArguments *args, const pgbson *spec);

static void ValidateGranularityType(const char *granularity);

static void ValidateValueIsNumberic(pgbson *value);

static double FindClosestPowersOf2(double n, bool findLarger);

static double FindClosest125(double n, bool findLarger);

static double FindClosestRenardOrEseries(double n, bool findLarger, const
										 char *seriesType);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(bson_dollar_bucket_auto);

/*
 * Assign a bucket id for each document with a window function. Similar to ntile(n) window function of Postgres.
 * result format: {"_id" : {"min" : <lower_bound>, "max" : <upper_bound>}}.
 */
Datum
bson_dollar_bucket_auto(PG_FUNCTION_ARGS)
{
	WindowObject winobj = PG_WINDOW_OBJECT();
	bool isNull = false;

	/* get currrent datum */
	pgbson *currentValue = NULL;

	Datum datumCurrent = WinGetFuncArgCurrent(winobj, 0, &isNull);
	if (!isNull)
	{
		currentValue = DatumGetPgBson(datumCurrent);
	}

	/* put arguments into cache */
	pgbson *spec = NULL;
	Datum datumSpec = WinGetFuncArgCurrent(winobj, 1, &isNull);
	if (!isNull)
	{
		spec = DatumGetPgBson(datumSpec);
	}

	BucketAutoArguments *args = NULL;
	int argposition = 1;
	SetCachedFunctionState(
		args,
		BucketAutoArguments,
		argposition,
		InitializeBucketAutoArguments,
		spec
		);

	if (args == NULL)
	{
		args = palloc0(sizeof(BucketAutoArguments));
		InitializeBucketAutoArguments(args, spec);
	}

	if (args->granularity.length > 0)
	{
		ValidateValueIsNumberic(currentValue);
	}

	/* initialize $bucketAuto state */
	BucketAutoState *state;
	state = (BucketAutoState *)
			WinGetPartitionLocalMemory(winobj, sizeof(BucketAutoState));

	if (state->bucketId == 0)
	{
		/* first call, initialize state */
		/* based on postgre's nodeWindowAgg source code, within window functions, fn_mcxt points to per-query level memory  */
		state->mcxt = fcinfo->flinfo->fn_mcxt;

		/* Todo: optimize by getting n from estimate count of collStats */
		state->totalRows = WinGetPartitionRowCount(winobj);
		state->nBuckets = args->numBuckets;
		state->expectRowsLimit = state->totalRows / state->nBuckets;
		if (state->expectRowsLimit == 0)
		{
			/* if the number of rows is less than the number of buckets, we need to set the number of buckets to the number of rows */
			state->expectRowsLimit = 1;
			state->nBuckets = state->totalRows;
		}
		state->remainder = state->totalRows % state->nBuckets;
	}

	/*
	 * compute bucket id for current row
	 */
	state->rowIndex++;

	if (state->rowIndex == 1)
	{
		/* first row in a bucket, prepare bucket specified context */
		state->bucketId += 1;

		/* initialize how many rows actually should be in the bucket */
		state->actualRowsLimit = state->expectRowsLimit;
		if (state->remainder > 0)
		{
			/* if the numRows is not divisible by the numBuckets, we need to distribute the remainder to the first few buckets */
			state->actualRowsLimit++;
			state->remainder--;
		}
		if (state->totalRows < state->actualRowsLimit)
		{
			state->actualRowsLimit = state->totalRows;
		}

		/* set lower and upper bound, lower bound setting should go first */
		SetLowerBound(currentValue, args, state);
		SetUpperBound(winobj, args, state);
	}

	if (state->rowIndex == state->actualRowsLimit)
	{
		/* last row in the bucket, prepare moving to next bucket */
		state->rowIndex = 0;
	}

	state->totalRows--;

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_writer innerWriter;
	PgbsonWriterStartDocument(&writer, "bucket_id", 9, &innerWriter);
	pgbsonelement lowerBoundElement;
	PgbsonToSinglePgbsonElement(state->lower_bound, &lowerBoundElement);
	PgbsonWriterAppendValue(&innerWriter, "min", 3, &lowerBoundElement.bsonValue);
	pgbsonelement upperBoundElement;
	PgbsonToSinglePgbsonElement(state->upper_bound, &upperBoundElement);
	PgbsonWriterAppendValue(&innerWriter, "max", 3, &upperBoundElement.bsonValue);
	PgbsonWriterEndDocument(&writer, &innerWriter);
	pgbson *result = PgbsonWriterGetPgbson(&writer);
	PG_RETURN_POINTER(result);
}


/*
 * Handles the $bucketAuto stage.
 * Validate the arguments and check required fields.
 * The conversion to postgresql query will be done in 2 parts:
 * 1. Function bson_dollar_bucket_auto() to calculate the bucket id for each row.
 * 2. Call HandleGroup() to group the data by the bucket id.
 */
Query *
HandleBucketAuto(const bson_value_t *existingValue, Query *query,
				 AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_BUCKET_AUTO);

	if (existingValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40240),
						errmsg(
							"$bucketAuto requires an object argument, but a value of type %s was provided instead.",
							BsonTypeName(
								existingValue->value_type))));
	}

	/* Push prior stuff to a subquery first since we're gonna aggregate our way */
	query = MigrateQueryToSubQuery(query, context);

	bson_value_t groupBy = { 0 };
	int numBuckets = 0;
	bson_value_t output = { 0 };
	char *granularity = NULL;

	bson_iter_t iter;
	BsonValueInitIterator(existingValue, &iter);
	while (bson_iter_next(&iter))
	{
		const char *key = bson_iter_key(&iter);
		const bson_value_t *value = bson_iter_value(&iter);
		if (strcmp(key, "groupBy") == 0)
		{
			groupBy = *bson_iter_value(&iter);
		}
		else if (strcmp(key, "buckets") == 0)
		{
			if (!BsonValueIsNumber(value))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40241),
								errmsg(
									"The 'buckets' field in $bucketAuto must contain a numeric value, but a different type was detected: %s",
									BsonTypeName(value->value_type))));
			}

			bool checkFixedInteger = true;
			if (!IsBsonValue32BitInteger(value, checkFixedInteger))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40242),
								errmsg(
									"The 'buckets' setting in $bucketAuto must fit within a 32-bit integer range, but was given: %s, type: %s",
									BsonValueToJsonForLogging(value), BsonTypeName(
										value->value_type))));
			}

			int num = BsonValueAsInt32(value);
			if (num <= 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40243),
								errmsg(
									"The 'buckets' field in the $bucketAuto operator must have a value greater than zero, but the provided value was: %d",
									num)));
			}
			numBuckets = num;
		}
		else if (strcmp(key, "output") == 0)
		{
			if (value->value_type != BSON_TYPE_DOCUMENT)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40244),
								errmsg(
									"The 'output' field in $bucketAuto must be an object, but a different type was provided: %s",
									BsonTypeName(value->value_type))));
			}
			output = *value;
		}
		else if (strcmp(key, "granularity") == 0)
		{
			if (value->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40261),
								errmsg(
									"Expected 'string' for 'granularity' field for $bucketAuto but found '%s' type",
									BsonTypeName(value->value_type))));
			}
			granularity = value->value.v_utf8.str;
			ValidateGranularityType(granularity);
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40245),
							errmsg("Unrecognized option to $bucketAuto: %s", key)));
		}
	}

	/* Required fields check */
	if (groupBy.value_type == BSON_TYPE_EOD || numBuckets == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40246),
						errmsg(
							"The $bucketAuto stage must include both 'groupBy' and 'buckets' parameters.")));
	}

	AggregationExpressionData parsedGroupBy = { 0 };
	ParseAggregationExpressionContext parseContext = { 0 };
	ParseAggregationExpressionData(&parsedGroupBy, &groupBy, &parseContext);
	if (parsedGroupBy.kind != AggregationExpressionKind_Path && parsedGroupBy.kind !=
		AggregationExpressionKind_Operator)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40239),
						errmsg(
							"The $bucketAuto 'groupBy' field must be specified using either a $-prefixed path or a valid expression object, but instead received: %s",
							BsonValueToJsonForLogging(&groupBy))));
	}

	/* step 1: ntile window function to assign bucket_id for each row. */
	query = BuildBucketAutoQuery(query, context,
								 &groupBy,
								 existingValue);

	/* step 2: Group by bucket id and add output fields. */
	bson_value_t groupSpec = { 0 };
	BuildBucketAutoGroupSpec(&output, &groupSpec);
	query = HandleGroup(&groupSpec, query, context);

	return query;
}


/* Build bson_dollar_bucket_auto window function with window clause to assign bucket id for each row, then migrate to subquery and merge the bucket id into document.
 * Result query:
 * SELECT bson_dollar_add_fields(document, bucket_id) AS document
 *  FROM (
 *     SELECT document, bson_dollar_bucket_auto(bson_expression_get(document, '{ "" : "<groupByField>" }'::bson, true),  '{ "groupBy" : "<groupByField>", "buckets": <buckets> }'::bson) OVER (ORDER BY bson_expression_get(document, '{ "" : "<groupByField>" }'::bson, true)) AS bucket_id
 *     FROM <collection>
 *  ) AS new_document;
 */
static Query *
BuildBucketAutoQuery(Query *query,
					 AggregationPipelineBuildContext *context, const
					 bson_value_t *groupBy, const
					 bson_value_t *bucketAutoSpec)
{
	/* get groupBy field function expression.
	 * About let variables support, arguments "buckets" and "granularity" are constants, "output" with let will be handled by HandleGroup, we only need to take care of variableSpec when evaluating groupBy field.
	 */
	pgbson *groupByDoc = BsonValueToDocumentPgbson(groupBy);
	TargetEntry *origEntry = linitial(query->targetList);

	List *args;
	Oid bsonExpressionGetFunction;
	if (context->variableSpec != NULL)
	{
		bsonExpressionGetFunction = BsonExpressionGetWithLetFunctionOid();
		args = list_make4(origEntry->expr, MakeBsonConst(groupByDoc),
						  MakeBoolValueConst(true), context->variableSpec);
	}
	else
	{
		bsonExpressionGetFunction = BsonExpressionGetFunctionOid();
		args = list_make3(origEntry->expr, MakeBsonConst(groupByDoc),
						  MakeBoolValueConst(true));
	}

	FuncExpr *getGroupbyFieldExpr = makeFuncExpr(bsonExpressionGetFunction,
												 BsonTypeId(), args,
												 InvalidOid, InvalidOid,
												 COERCE_EXPLICIT_CALL);

	Index winRef = 1;
	ParseState *parseState = make_parsestate(NULL);
	parseState->p_expr_kind = EXPR_KIND_WINDOW_ORDER;
	parseState->p_next_resno = list_length(query->targetList) + 1;

	/*
	 * bson_dollar_bucket_auto window function
	 */
	WindowFunc *windowFunc = makeNode(WindowFunc);
	windowFunc->winfnoid = BsonDollarBucketAutoFunctionOid();
	windowFunc->wintype = BsonTypeId();
	windowFunc->winref = winRef;
	windowFunc->winstar = false;

	/* set winagg to false to declare this is a window function instead of a plain aggregate.*/
	windowFunc->winagg = false;

	pgbson *specBson = PgbsonInitFromDocumentBsonValue(bucketAutoSpec);
	windowFunc->args = list_make2(getGroupbyFieldExpr, MakeBsonConst(specBson));

	char *bucketIdFieldName = pstrdup(BUCKETAUTO_BUCKET_ID_FIELD);
	bool resjunk = false;
	TargetEntry *bucketAutoEntry = makeTargetEntry((Expr *) windowFunc,
												   parseState->p_next_resno++,
												   bucketIdFieldName, resjunk);
	query->targetList = lappend(query->targetList, bucketAutoEntry);

	/* set this field to true to make WindowFunc in a WindowAgg plan node */
	query->hasWindowFuncs = true;

	/*
	 * window clause, order by groupBy field
	 */
	WindowClause *windowClause = makeNode(WindowClause);
	windowClause->winref = winRef;

	windowClause->frameOptions = (
		FRAMEOPTION_NONDEFAULT |  /* Explicitly specifies a custom window frame instead of using the default (RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) */
		FRAMEOPTION_ROWS |        /* The frame is defined by a specific number of rows before and/or after the current row */
		FRAMEOPTION_START_UNBOUNDED_PRECEDING |  /* Frame starts from the first row in the partition */
		FRAMEOPTION_BETWEEN |  /* Enables "BETWEEN start AND end" syntax for frame specification */
		FRAMEOPTION_END_UNBOUNDED_FOLLOWING  /* Frame extends to the last row in the partition */
		);

	List *orderByClauseList = NIL;
	SortBy *sortBy = makeNode(SortBy);
	sortBy->location = -1;
	sortBy->sortby_dir = SORTBY_ASC;
	sortBy->node = (Node *) getGroupbyFieldExpr;

	TargetEntry *sortEntry = makeTargetEntry((Expr *) sortBy->node,
											 parseState->p_next_resno++,
											 NULL, true);

	/* Add order by clause's resjunc entry into target list */
	query->targetList = lappend(query->targetList, sortEntry);

	orderByClauseList = addTargetToSortList(parseState, sortEntry,
											orderByClauseList,
											query->targetList,
											sortBy);
	windowClause->orderClause = orderByClauseList;
	query->windowClause = lappend(query->windowClause, windowClause);

	/*
	 * Migrate to subquery and merge the result bucket id into document
	 */
	context->expandTargetList = true;
	query = MigrateQueryToSubQuery(query, context);
	TargetEntry *docEntry = linitial(query->targetList);

	Index childIndex = 1;
	FuncExpr *newDocExpr = makeFuncExpr(BsonDollarAddFieldsFunctionOid(),
										BsonTypeId(),
										list_make2(
											(Expr *) docEntry->
											expr,
											makeVarFromTargetEntry(childIndex,
																   bucketAutoEntry)),
										InvalidOid, InvalidOid,
										COERCE_EXPLICIT_CALL);
	docEntry->expr = (Expr *) newDocExpr;

	/* Push everything to subquery after this */
	context->requiresSubQuery = true;

	return query;
}


/*
 * build group spec to call HandleGroup.
 * 1. Add '_id' field to group spec, which is the bucket id generated by window function.
 * 2. Add other fields specified in $bucketAuto's 'output'. When 'output' is not specified, we put a count field by default.
 */
static void
BuildBucketAutoGroupSpec(const bson_value_t *output, bson_value_t *groupSpec)
{
	pgbson_writer groupWriter;
	PgbsonWriterInit(&groupWriter);
	char bucketIdFieldPath[20] = "$";
	strcat(bucketIdFieldPath, BUCKETAUTO_BUCKET_ID_FIELD);
	PgbsonWriterAppendUtf8(&groupWriter, "_id", 3, bucketIdFieldPath);

	/* Add other fields specified in $bucketAuto's 'output'. When 'output' is not specified, we put a count field by default. */
	if (output->value_type != BSON_TYPE_EOD)
	{
		bson_iter_t outputIter;
		BsonValueInitIterator(output, &outputIter);
		while (bson_iter_next(&outputIter))
		{
			const char *key = bson_iter_key(&outputIter);
			const bson_value_t *value = bson_iter_value(&outputIter);
			PgbsonWriterAppendValue(&groupWriter, key, strlen(key), value);
		}
	}
	else
	{
		pgbson_writer countWriter;
		PgbsonWriterStartDocument(&groupWriter, "count", 5, &countWriter);
		PgbsonWriterAppendInt32(&countWriter, "$sum", 4, 1);
		PgbsonWriterEndDocument(&groupWriter, &countWriter);
	}

	pgbson *group = PgbsonWriterGetPgbson(&groupWriter);
	*groupSpec = ConvertPgbsonToBsonValue(group);
}


/*
 * Compute the lower bound of the bucket.
 * for non-first bucket, the lower bound is the upper bound of the previous bucket.
 * for first bucket,
 *  - With granularity: The lower bound of first bucket is the previous value smaller than min value in the granularity number series.
 *  - Without granularity: The lower bound is the min value of the bucket.
 */
static void
SetLowerBound(const pgbson *currentValue, const BucketAutoArguments *args,
			  BucketAutoState *state)
{
	if (state->lower_bound != NULL)
	{
		pfree(state->lower_bound);
	}

	if (state->bucketId > 1)
	{
		state->lower_bound = state->upper_bound;
		state->upper_bound = NULL;
		return;
	}

	if (args->granularity.length > 0)
	{
		pgbsonelement currentValueElement;
		PgbsonToSinglePgbsonElement(currentValue, &currentValueElement);
		double currentValueDouble = BsonValueAsDouble(&currentValueElement.bsonValue);
		double lowerBound = 0;
		bool findLarger = false;
		if (strcmp(args->granularity.string, "POWERSOF2") == 0)
		{
			lowerBound = FindClosestPowersOf2(currentValueDouble, findLarger);
		}
		else if (strcmp(args->granularity.string, "1-2-5") == 0)
		{
			lowerBound = FindClosest125(currentValueDouble, findLarger);
		}
		else
		{
			lowerBound = FindClosestRenardOrEseries(currentValueDouble, findLarger,
													args->granularity.string);
		}

		bson_value_t lowerBoundValue = {
			.value_type = BSON_TYPE_DOUBLE,
			.value.v_double = lowerBound
		};
		state->lower_bound = CopyPgbsonIntoMemoryContext(BsonValueToDocumentPgbson(
															 &lowerBoundValue),
														 state->mcxt);
	}
	else
	{
		state->lower_bound = CopyPgbsonIntoMemoryContext(currentValue, state->mcxt);
	}
}


/*
 * Compute the upper bound of the bucket.
 * With granularity:
 *  - The upper bound is the the next value larger than max value in the granularity number series, for all the buckets.
 * Without granularity:
 *  - The upper bound of the last bucket is the max value of it.
 *  - The upper bound of non-last bucket is the next value larger than the max value in currenct bucket, in other words, the min value in next bucket.
 */
static void
SetUpperBound(WindowObject winobj, const BucketAutoArguments *args,
			  BucketAutoState *state)
{
	if (state->upper_bound != NULL)
	{
		pfree(state->upper_bound);
	}
	pgbson *upperBound = NULL;

	bool isMaxValueNull = true;
	bool isMaxValueOut = false;
	Datum maxOfBucketDatum = WinGetFuncArgInPartition(winobj, 0,
													  state->actualRowsLimit - 1,
													  WINDOW_SEEK_CURRENT, true,
													  &isMaxValueNull,
													  &isMaxValueOut);

	if (isMaxValueNull || isMaxValueOut)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("unexpected - failed to get max value of bucket.")));
	}
	pgbson *maxOfBucket = DatumGetPgBson(maxOfBucketDatum);
	pgbsonelement maxOfCurrBucketElement;
	PgbsonToSinglePgbsonElement(maxOfBucket, &maxOfCurrBucketElement);

	/* Apply granularity. */
	if (args->granularity.length > 0)
	{
		ValidateValueIsNumberic(maxOfBucket);

		double maxOfCurrBucketDouble = BsonValueAsDouble(
			&maxOfCurrBucketElement.bsonValue);
		double upperBoundDouble = 0;
		bool findLarger = true;
		if (strcmp(args->granularity.string, "POWERSOF2") == 0)
		{
			upperBoundDouble = FindClosestPowersOf2(maxOfCurrBucketDouble, findLarger);
		}
		else if (strcmp(args->granularity.string, "1-2-5") == 0)
		{
			upperBoundDouble = FindClosest125(maxOfCurrBucketDouble, findLarger);
		}
		else
		{
			upperBoundDouble = FindClosestRenardOrEseries(maxOfCurrBucketDouble,
														  findLarger,
														  args->granularity.string);
		}

		bson_value_t upperBoundValue = {
			.value_type = BSON_TYPE_DOUBLE,
			.value.v_double = upperBoundDouble
		};
		upperBound = BsonValueToDocumentPgbson(&upperBoundValue);
	}
	else
	{
		upperBound = maxOfBucket;
	}

	/* iterate next bucket elements, find the first value that is larger than the max value of current bucket.*/
	int start_offset = state->actualRowsLimit;
	bool isNextNull = false;
	bool isNextOut = false;
	while (true)
	{
		Datum nextDatum = WinGetFuncArgInPartition(winobj, 0, start_offset,
												   WINDOW_SEEK_CURRENT, false,
												   &isNextNull,
												   &isNextOut);
		if (isNextNull || isNextOut)
		{
			break;
		}
		pgbson *next = DatumGetPgBson(nextDatum);
		pgbsonelement nextElement;
		PgbsonToSinglePgbsonElement(next, &nextElement);
		bool isComparionValid;
		if (args->granularity.length > 0)
		{
			pgbsonelement upperBoundElement;
			PgbsonToSinglePgbsonElement(upperBound, &upperBoundElement);
			int compareWithBound = CompareBsonValueAndType(&upperBoundElement.bsonValue,
														   &nextElement.bsonValue,
														   &isComparionValid);
			if (compareWithBound > 0)
			{
				/* expand bucket, when next element value is less than the upper bound set with granularity */
				state->actualRowsLimit++;
				if (state->remainder > 0)
				{
					state->remainder--;
				}
				start_offset++;
			}
			else
			{
				break;
			}
		}
		else
		{
			int compareWithMax = CompareBsonValueAndType(
				&maxOfCurrBucketElement.bsonValue, &nextElement.bsonValue,
				&isComparionValid);
			if (compareWithMax > 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
								errmsg(
									"Unexpected internal error: max value of current bucket is larger than value in next bucket.")));
			}
			else if (compareWithMax == 0)
			{
				/* expand bucket, when next element value equals max value of current bucket */
				state->actualRowsLimit++;
				if (state->remainder > 0)
				{
					state->remainder--;
				}
				start_offset++;
			}
			else
			{
				upperBound = next;
				break;
			}
		}
	}
	state->upper_bound = CopyPgbsonIntoMemoryContext(upperBound, state->mcxt);
}


static void
InitializeBucketAutoArguments(BucketAutoArguments *args, const pgbson *spec)
{
	bson_iter_t iter;
	PgbsonInitIterator(spec, &iter);
	while (bson_iter_next(&iter))
	{
		const char *key = bson_iter_key(&iter);
		const bson_value_t *value = bson_iter_value(&iter);
		if (strcmp(key, "groupBy") == 0)
		{
			args->groupBy = *value;
		}
		else if (strcmp(key, "buckets") == 0)
		{
			args->numBuckets = BsonValueAsInt32(value);
		}
		else if (strcmp(key, "granularity") == 0)
		{
			args->granularity = (StringView) {
				.string = value->value.v_utf8.str,
				.length = value->value.v_utf8.len
			};
		}
	}
}


static void
ValidateGranularityType(const char *granularity)
{
	int arraySize = sizeof(BUCKETAUTO_GRANULARITY_SUPPORTED_TYPES) /
					sizeof(BUCKETAUTO_GRANULARITY_SUPPORTED_TYPES[0]);
	for (int i = 0; i < arraySize; i++)
	{
		if (StringViewEqualsCString(&BUCKETAUTO_GRANULARITY_SUPPORTED_TYPES[i],
									granularity))
		{
			return;
		}
	}
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40257),
					errmsg("Rounding granularity not recognized: %s", granularity)));
}


static void
ValidateValueIsNumberic(pgbson *value)
{
	pgbsonelement currentValueElement;
	PgbsonToSinglePgbsonElement(value, &currentValueElement);
	bson_value_t current = currentValueElement.bsonValue;
	if (!BsonValueIsNumber(&current))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40258),
						errmsg(
							"$bucketAuto only allows specifying a 'granularity' with numeric boundaries, but encountered a value of type: %s",
							BsonTypeName(current.value_type))));
	}
	double currentValueDouble = BsonValueAsDouble(&current);
	if (currentValueDouble < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40260),
						errmsg(
							" $bucketAuto only allows specifying a 'granularity' with numeric boundaries, but a negative value was provided: %f",
							currentValueDouble)));
	}
}


static double
FindClosestPowersOf2(double n, bool findLarger)
{
	if (n == 0)
	{
		return 0;
	}

	double base = 1.0;
	if (n < 1)
	{
		while (base > n)
		{
			base /= 2.0;
		}
		if (findLarger)
		{
			return base * 2.0; /* Smallest power of 2 that is strictly larger than n */
		}
		else
		{
			return base == n ? base / 2.0 : base; /* Closest power of 2 strictly less than n */
		}
	}
	else
	{
		while (base < n)
		{
			base *= 2.0;
		}
		if (findLarger)
		{
			return base == n ? base * 2.0 : base;
		}
		else
		{
			return base / 2.0;
		}
	}
}


static double
FindClosest125(double n, bool findLarger)
{
	if (n == 0)
	{
		return 0;
	}

	double base = 1.0;

	while (base < n)
	{
		base *= 10.0;
	}

	/* Now base is at least as large as n */
	if (findLarger)
	{
		/* We need to find the smallest power of 10 times 1, 2, or 5 that's larger than n */
		base /= 10;
		if (base > n)
		{
			return base;
		}
		else if (base * 2 > n)
		{
			return base * 2;
		}
		else if (base * 5 > n)
		{
			return base * 5;
		}
		else
		{
			return base * 10;
		}
	}
	else
	{
		/* We need to find the largest power of 10 times 1, 2, or 5 that's smaller than n */
		if (base / 2 < n)
		{
			return base / 2;
		}
		else if (base / 5 < n)
		{
			return base / 5;
		}
		else
		{
			return base / 10;
		}
	}
}


static double
FindClosestRenardOrEseries(double n, bool findLarger, const char *seriesType)
{
	if (n == 0)
	{
		return 0;
	}

	/* Define Renard series in the most desirable rounding scale */
	static const double r5[] = { 1.0, 1.6, 2.5, 4.0, 6.3 };
	static const double r10[] = { 1.0, 1.25, 1.6, 2.0, 2.5, 3.15, 4.0, 5.0, 6.3, 8.0 };
	static const double r20[] = {
		1.0, 1.12, 1.25, 1.4, 1.6, 1.8, 2.0, 2.24, 2.5, 2.8, 3.15, 3.55, 4.0, 4.5, 5.0,
		5.6, 6.3, 7.1, 8.0, 9.0
	};
	static const double r40[] = {
		1.0, 1.06, 1.12, 1.18, 1.25, 1.32, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 2.0, 2.12, 2.24,
		2.36, 2.5, 2.65, 2.8, 3.0, 3.15, 3.35, 3.55, 3.75, 4.0, 4.25, 4.5, 4.75, 5.0, 5.3,
		5.6, 6.0, 6.3, 6.7, 7.1, 7.5, 8.0, 8.5, 9.0, 9.5
	};
	static const double r80[] = {
		1.00, 1.03, 1.06, 1.09, 1.12, 1.15, 1.18, 1.22, 1.26, 1.29, 1.32, 1.36, 1.40,
		1.44, 1.48, 1.52, 1.56, 1.60, 1.65, 1.69, 1.74, 1.78, 1.83, 1.88, 1.93, 1.98,
		2.03, 2.09, 2.14, 2.20, 2.26, 2.32, 2.38, 2.44, 2.50, 2.56, 2.63, 2.70, 2.77,
		2.84, 2.91, 2.99, 3.06, 3.14, 3.22, 3.30, 3.38, 3.46, 3.55, 3.63, 3.72, 3.81,
		3.90, 4.00, 4.09, 4.19, 4.29, 4.39, 4.49, 4.60, 4.71, 4.82, 4.93, 5.04, 5.16,
		5.28, 5.40, 5.52, 5.65, 5.78,
		5.91, 6.03, 6.17, 6.30, 6.44, 6.58, 6.72, 6.86, 7.01, 7.16,
		7.32, 7.47, 7.62, 7.78, 7.94, 8.00, 8.17, 8.34, 8.51, 8.68,
		8.86, 9.04, 9.22, 9.41, 9.60, 9.79
	};
	static const double e6[] = { 1.0, 1.5, 2.2, 3.3, 4.7, 6.8 };
	static const double e12[] = {
		1.0, 1.2, 1.5, 1.8, 2.2, 2.7, 3.3, 3.9, 4.7, 5.6, 6.8, 8.2
	};
	static const double e24[] = {
		1.0, 1.1, 1.2, 1.3, 1.5, 1.6, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0,
		3.3, 3.6, 3.9, 4.3, 4.7, 5.1, 5.6, 6.2, 6.8, 7.5, 8.2, 9.1
	};
	static const double e48[] = {
		1.00, 1.05, 1.10, 1.15, 1.21, 1.27, 1.33, 1.40, 1.47, 1.54, 1.62, 1.69,
		1.78, 1.87, 1.96, 2.05, 2.15, 2.26, 2.37, 2.49, 2.61, 2.74, 2.87, 3.01,
		3.16, 3.32, 3.48, 3.65, 3.83, 4.02, 4.22, 4.42, 4.64, 4.87, 5.11, 5.36,
		5.62, 5.90, 6.19, 6.49, 6.81, 7.15, 7.50, 7.87, 8.25, 8.66, 9.09, 9.53
	};
	static const double e96[] = {
		1.00, 1.02, 1.05, 1.07, 1.10, 1.13, 1.15, 1.18, 1.21, 1.24, 1.27, 1.30,
		1.33, 1.37, 1.40, 1.43, 1.47, 1.50, 1.54, 1.58, 1.62, 1.65, 1.69, 1.74,
		1.78, 1.82, 1.87, 1.91, 1.96, 2.00, 2.05, 2.10, 2.15, 2.21, 2.26, 2.32,
		2.37, 2.43, 2.49, 2.55, 2.61, 2.67, 2.74, 2.80, 2.87, 2.94, 3.01, 3.09,
		3.16, 3.24, 3.32, 3.40, 3.48, 3.57, 3.65, 3.74, 3.83, 3.92, 4.02, 4.12,
		4.22, 4.32, 4.42, 4.53, 4.64, 4.75, 4.87, 4.99, 5.11, 5.23, 5.36, 5.49,
		5.62, 5.76, 5.90, 6.04, 6.19, 6.34, 6.49, 6.65, 6.81, 6.98, 7.15, 7.32,
		7.50, 7.68, 7.87, 8.06, 8.25, 8.45, 8.66, 8.87, 9.09, 9.31, 9.53, 9.76
	};
	static const double e192[] = {
		1.00, 1.01, 1.02, 1.04, 1.05, 1.06, 1.07, 1.09, 1.10, 1.11, 1.13, 1.14, 1.15,
		1.17, 1.18, 1.20, 1.21, 1.23, 1.24, 1.26, 1.27, 1.29, 1.30, 1.32, 1.33, 1.35,
		1.37, 1.38, 1.40, 1.42, 1.43, 1.45, 1.47, 1.49, 1.50, 1.52, 1.54, 1.56, 1.58,
		1.60, 1.62, 1.64, 1.65, 1.67, 1.69, 1.72, 1.74, 1.76, 1.78, 1.80, 1.82, 1.84,
		1.87, 1.89, 1.91, 1.93, 1.96, 1.98, 2.00, 2.03, 2.05, 2.08, 2.10, 2.13, 2.15,
		2.18, 2.21, 2.23, 2.26, 2.29, 2.32, 2.34, 2.37, 2.40, 2.43, 2.46, 2.49, 2.52,
		2.55, 2.58, 2.61, 2.64, 2.67, 2.71, 2.74, 2.77, 2.80, 2.84, 2.87, 2.91, 2.94,
		2.98, 3.01, 3.05, 3.09, 3.12, 3.16, 3.20, 3.24, 3.28, 3.32, 3.36, 3.40, 3.44,
		3.48, 3.52, 3.57, 3.61, 3.65, 3.70, 3.74, 3.79, 3.83, 3.88, 3.92, 3.97, 4.02,
		4.07, 4.12, 4.17, 4.22, 4.27, 4.32, 4.37, 4.42, 4.48, 4.53, 4.59, 4.64, 4.70,
		4.75, 4.81, 4.87, 4.93, 4.99, 5.05, 5.11, 5.17, 5.23, 5.30, 5.36, 5.42, 5.49,
		5.56, 5.62, 5.69, 5.76, 5.83, 5.90, 5.97, 6.04, 6.12, 6.19, 6.26, 6.34, 6.42,
		6.49, 6.57, 6.65, 6.73, 6.81, 6.90, 6.98, 7.06, 7.15, 7.23, 7.32, 7.41, 7.50,
		7.59, 7.68, 7.77, 7.87, 7.96, 8.06, 8.16, 8.25, 8.35, 8.45, 8.56, 8.66, 8.76,
		8.87, 8.98, 9.09, 9.20, 9.31, 9.42, 9.53, 9.65, 9.76, 9.88
	};

	/* Select the series based on seriesType */
	const double *series;
	int seriesSize;
	if (strcmp(seriesType, "R5") == 0)
	{
		series = r5;
		seriesSize = sizeof(r5) / sizeof(r5[0]);
	}
	else if (strcmp(seriesType, "R10") == 0)
	{
		series = r10;
		seriesSize = sizeof(r10) / sizeof(r10[0]);
	}
	else if (strcmp(seriesType, "R20") == 0)
	{
		series = r20;
		seriesSize = sizeof(r20) / sizeof(r20[0]);
	}
	else if (strcmp(seriesType, "R40") == 0)
	{
		series = r40;
		seriesSize = sizeof(r40) / sizeof(r40[0]);
	}
	else if (strcmp(seriesType, "R80") == 0)
	{
		series = r80;
		seriesSize = sizeof(r80) / sizeof(r80[0]);
	}
	else if (strcmp(seriesType, "E6") == 0)
	{
		series = e6;
		seriesSize = sizeof(e6) / sizeof(e6[0]);
	}
	else if (strcmp(seriesType, "E12") == 0)
	{
		series = e12;
		seriesSize = sizeof(e12) / sizeof(e12[0]);
	}
	else if (strcmp(seriesType, "E24") == 0)
	{
		series = e24;
		seriesSize = sizeof(e24) / sizeof(e24[0]);
	}
	else if (strcmp(seriesType, "E48") == 0)
	{
		series = e48;
		seriesSize = sizeof(e48) / sizeof(e48[0]);
	}
	else if (strcmp(seriesType, "E96") == 0)
	{
		series = e96;
		seriesSize = sizeof(e96) / sizeof(e96[0]);
	}
	else if (strcmp(seriesType, "E192") == 0)
	{
		series = e192;
		seriesSize = sizeof(e192) / sizeof(e192[0]);
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("Unexpected: Unknown number series type: %s",
							   seriesType)));
	}

	double base = 1.0;

	/* Scale the base up or down to find the appropriate range where n fits */
	const double MIN_BASE = 5e-324; /* Minimum positive double value */
	const double MAX_BASE = 1.79769e308; /* Largest positive double */

	/* let base <= n */
	if (n <= 1.0)
	{
		while (base > n)
		{
			base /= 10.0;
			if (base < MIN_BASE)
			{
				/* hard code this to pass the jstest */
				return findLarger ? 1.02e-321 : 0;
			}
		}
	}
	else
	{
		while (base * 10.0 <= n)
		{
			if (base >= MAX_BASE / 10.0)
			{
				break;
			}
			base *= 10.0;
		}
	}

	/* Find the closest Renard value based on whether we want larger or smaller */
	if (findLarger)
	{
		for (int i = 0; i < seriesSize; i++)
		{
			double candidate = base * series[i];
			if (candidate > n)
			{
				return candidate;
			}
		}

		/* If no factors in this base are large enough, return the next base. Example: when granularity is R5, n=7, we should return 10. */
		return base * 10.0;
	}
	else
	{
		if (base == n)
		{
			base /= 10.0;
		}
		for (int i = seriesSize - 1; i >= 0; i--)
		{
			double candidate = base * series[i];
			if (candidate < n)
			{
				return candidate;
			}
		}

		/* the last candidate is base*1.0, which is always less than n. */
	}

	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
					errmsg("Unexpected: Failed to find a value in the series for %f",
						   n)));
}
