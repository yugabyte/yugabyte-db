/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_expression_bucket_operator.c
 *
 * Internal bucket Operator expression implementations of BSON.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>

#include "io/helio_bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "operators/bson_expression_bucket_operator.h"
#include "utils/mongo_errors.h"
#include "query/helio_bson_compare.h"

/* --------------------------------------------------------- */
/* Type definitions */
/* --------------------------------------------------------- */

/* Represents the arguments of operator $_bucketInternal */
typedef struct BucketInternalArguments
{
	AggregationExpressionData groupBy;
	List *boundaries;
	AggregationExpressionData defaultBucket;
} BucketInternalArguments;


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static void ParseBoundariesForBucketInternal(BucketInternalArguments *arguments,
											 bson_value_t *boundaries,
											 bson_value_t *defaultBucket);

static StringView BucketInternalStringView = {
	.string = "$_bucketInternal",
	.length = 16
};

/* Rewrite $bucket query into a $group query.
 * 1. validates arguments, check required fields existed and data type for each argument.
 * 2. build expression for $_bucketInternal operator, with 'groupBy','boundaries','default' arguments, then set it as '_id' field in $group
 * 3. build group fields based on $bucket's 'output' argument.
 * Example, before rewrite:
 * { $bucket: { groupBy: "$price", boundaries: [ 0, 100, 200 ], default: "other", output: { count: { $sum: 1 } } } }
 * After rewrite:
 * { $group: { _id: { $_bucketInternal: { groupBy: "$price", boundaries: [ 0, 100, 200 ], default: "other" } }, count: { $sum: 1 } } }
 */
void
RewriteBucketGroupSpec(const bson_value_t *bucketSpec, bson_value_t *groupSpec)
{
	if (bucketSpec->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation40201),
						errmsg(
							"Argument to $bucket stage must be an object, but found type: %s",
							BsonTypeName(
								bucketSpec->value_type)),
						errhint(
							"Argument to $bucket stage must be an object, but found type: %s",
							BsonTypeName(
								bucketSpec->value_type))));
	}

	bson_iter_t specIter;
	BsonValueInitIterator(bucketSpec, &specIter);
	bson_value_t groupBy = { 0 };
	bson_value_t boundaries = { 0 };
	bson_value_t defaultBucket = { 0 };
	bson_value_t output = { 0 };

	while (bson_iter_next(&specIter))
	{
		const char *key = bson_iter_key(&specIter);
		const bson_value_t *value = bson_iter_value(&specIter);

		if (strcmp(key, "groupBy") == 0)
		{
			groupBy = *value;
		}
		else if (strcmp(key, "boundaries") == 0)
		{
			boundaries = *value;
		}
		else if (strcmp(key, "default") == 0)
		{
			defaultBucket = *value;
		}
		else if (strcmp(key, "output") == 0)
		{
			output = *value;
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation40197),
							errmsg("Unrecognized option to $bucket: %s", key),
							errhint("Unrecognized option to $bucket: %s", key)));
		}
	}

	/* Check required field of groupBy and boundaries */
	if (groupBy.value_type == BSON_TYPE_EOD || boundaries.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation40198),
						errmsg(
							"$bucket requires 'groupBy' and 'boundaries' to be specified."),
						errhint(
							"The $bucket stage requires 'groupBy' and 'boundaries' to be specified.")));
	}

	/* Validate 'groupBy' value type */
	if (groupBy.value_type != BSON_TYPE_UTF8 && groupBy.value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation40202),
						errmsg(
							"The $bucket 'groupBy' field must be defined as a $-prefixed path or an expression. But found: %s",
							BsonTypeName(
								groupBy.value_type)),
						errhint(
							"The $bucket 'groupBy' field must be an operator expression.But found: %s",
							BsonTypeName(
								groupBy.value_type))));
	}

	/* Validate 'boundaries' type */
	if (boundaries.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation40200),
						errmsg(
							"The $bucket 'boundaries' field must be an array of values. But found: %s",
							BsonTypeName(
								boundaries.value_type)),
						errhint(
							"The $bucket 'boundaries' field must be an array of values. But found: %s",
							BsonTypeName(
								boundaries.value_type))));
	}

	/* Validate 'default' value type is constant */
	if (defaultBucket.value_type != BSON_TYPE_EOD)
	{
		AggregationExpressionData parsedDefaultValue = { 0 };
		ParseAggregationExpressionContext parseContext = { 0 };
		ParseAggregationExpressionData(&parsedDefaultValue, &defaultBucket,
									   &parseContext);
		if (parsedDefaultValue.kind != AggregationExpressionKind_Constant)
		{
			ereport(ERROR, (errcode(MongoLocation40195),
							errmsg(
								"The $bucket 'default' field must be a constant. Input value: %s",
								BsonValueToJsonForLogging(&defaultBucket)),
							errhint(
								"The $bucket 'default' field must be a constant.")));
		}
	}

	/* validate 'output' value type is document */
	if (output.value_type != BSON_TYPE_EOD && output.value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation40196),
						errmsg(
							"The $bucket 'output' field must be a document. But found: %s",
							BsonTypeName(
								output.value_type)),
						errhint(
							"The $bucket 'output' field must be a document. But found: %s",
							BsonTypeName(
								output.value_type))));
	}

	/* Convert to $group
	 * Build _id field
	 */
	pgbson_writer bucketInternalWriter;
	PgbsonWriterInit(&bucketInternalWriter);
	pgbson_writer bucketInternalSpecWriter;
	PgbsonWriterStartDocument(&bucketInternalWriter, BucketInternalStringView.string,
							  BucketInternalStringView.length,
							  &bucketInternalSpecWriter);

	PgbsonWriterAppendValue(&bucketInternalSpecWriter, "groupBy", 7, &groupBy);
	PgbsonWriterAppendValue(&bucketInternalSpecWriter, "boundaries", 10,
							&boundaries);
	if (defaultBucket.value_type != BSON_TYPE_EOD)
	{
		PgbsonWriterAppendValue(&bucketInternalSpecWriter, "default", 7,
								&defaultBucket);
	}
	PgbsonWriterEndDocument(&bucketInternalWriter, &bucketInternalSpecWriter);

	pgbson *bucketInternal = PgbsonWriterGetPgbson(&bucketInternalWriter);
	bson_value_t bucketInternalValue = ConvertPgbsonToBsonValue(bucketInternal);

	pgbson_writer groupWriter;
	PgbsonWriterInit(&groupWriter);
	PgbsonWriterAppendValue(&groupWriter, "_id", 3, &bucketInternalValue);

	/* build other fields of $group, based on $bucket's output.
	 * When output is not specified, we put a count field by default.
	 */
	if (output.value_type != BSON_TYPE_EOD)
	{
		bson_iter_t outputIter;
		BsonValueInitIterator(&output, &outputIter);
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
 * This function gets the final result for $_bucketInternal operator which returns the lower bound of the bucket a document belongs to.
 */
void
HandlePreParsedDollarBucketInternal(pgbson *doc, void *arguments,
									ExpressionResult *expressionResult)
{
	BucketInternalArguments *parsedData = arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(&parsedData->groupBy, doc,
									  &childResult,
									  isNullOnEmpty);

	bson_value_t evaluatedGroupByField = childResult.value;

	bson_value_t *result = NULL;

	/* Each adjacent pair of values acts as the inclusive lower boundary and the exclusive upper boundary for the bucket. */
	/* binary search to find the lower bound for evaluatedGroupByField */
	int left = 0;
	int right = list_length(parsedData->boundaries) - 1;
	bson_value_t lowerBound = { 0 };
	bson_value_t upperBound = { 0 };
	bool isComparisonValid = true;

	while (left < right)
	{
		int mid = left + (right - left) / 2;
		lowerBound = *(bson_value_t *) list_nth(parsedData->boundaries, mid);
		upperBound = *(bson_value_t *) list_nth(parsedData->boundaries, mid + 1);

		int compareValueLower = CompareBsonValueAndType(&evaluatedGroupByField,
														&lowerBound,
														&isComparisonValid);
		int compareValueUpper = CompareBsonValueAndType(&evaluatedGroupByField,
														&upperBound,
														&isComparisonValid);

		if (compareValueLower >= 0 && compareValueUpper < 0)
		{
			result = &lowerBound;
			break;
		}

		if (compareValueLower < 0)
		{
			right = mid;
		}
		else
		{
			left = mid + 1;
		}
	}

	/* Does not fall into any bucket */
	/* check if result is still the initial value */
	if (result == NULL)
	{
		if (parsedData->defaultBucket.kind == AggregationExpressionKind_Invalid)
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"$bucket could not find a bucket for an input value %s, and no default was specified.",
								BsonValueToJsonForLogging(&evaluatedGroupByField)),
							errhint(
								"The input value must fall into one of the bucket boundaries, or specify a default value.")));
		}
		result = &parsedData->defaultBucket.value;
	}

	ExpressionResultSetValue(expressionResult, result);
}


/*
 * This function handles the parsing for the operator $_bucketInternal.
 * Input structure for $_bucketInternal is something like
 * "$_bucketInternal" : { "groupBy" : "$year", "boundaries" : [ 2020, 2021, 2022 ], "default" : "other" }.
 */
void
ParseDollarBucketInternal(const bson_value_t *argument, AggregationExpressionData *data,
						  ParseAggregationExpressionContext *context)
{
	bson_iter_t docIter;
	BsonValueInitIterator(argument, &docIter);

	bson_value_t groupBy = { 0 };
	bson_value_t boundaries = { 0 };
	bson_value_t defaultBucket = { 0 };
	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "groupBy") == 0)
		{
			groupBy = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "boundaries") == 0)
		{
			boundaries = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "default") == 0)
		{
			defaultBucket = *bson_iter_value(&docIter);
		}
	}

	BucketInternalArguments *arguments = palloc0(sizeof(BucketInternalArguments));

	/* During $bucket stage handling, we already validated required fields and data type for each argument. */
	ParseAggregationExpressionContext parseContext = { 0 };
	ParseAggregationExpressionData(&arguments->groupBy, &groupBy, &parseContext);

	ParseBoundariesForBucketInternal(arguments, &boundaries, &defaultBucket);

	data->operator.arguments = arguments;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
}


/* boundaries constraints: */
/* 1. element type = constant. */
/* 2. element count > 1. */
/* 3. elements having same type. */
/* 4. in ascending order. */
/* The $bucket default field must be less than the lowest boundary or greater than or equal to the highest boundary, if having the same value type. */
static void
ParseBoundariesForBucketInternal(BucketInternalArguments *arguments,
								 bson_value_t *boundaries, bson_value_t *defaultBucket)
{
	bson_iter_t boundariesIter;
	BsonValueInitIterator(boundaries, &boundariesIter);
	bson_type_t boundaryType = BSON_TYPE_EOD;
	while (bson_iter_next(&boundariesIter))
	{
		const bson_value_t *value = bson_iter_value(&boundariesIter);
		AggregationExpressionData parsedValue = { 0 };
		ParseAggregationExpressionContext parseContext = { 0 };
		ParseAggregationExpressionData(&parsedValue, value, &parseContext);
		if (parsedValue.kind != AggregationExpressionKind_Constant)
		{
			ereport(ERROR, (errcode(MongoLocation40191),
							errmsg(
								"The $bucket 'boundaries' field must be an array of constant values."),
							errhint(
								"The $bucket 'boundaries' field must be an array of constant values.")));
		}

		if (boundaryType != BSON_TYPE_EOD && CompareSortOrderType(boundaryType,
																  value->value_type) != 0)
		{
			ereport(ERROR, (errcode(MongoLocation40193),
							errmsg(
								"The $bucket 'boundaries' field must be an array of values of the same type. Found different types: %s and %s",
								BsonTypeName(
									boundaryType), BsonTypeName(value->value_type)),
							errhint(
								"The $bucket 'boundaries' field must be an array of values of the same type.")));
		}

		if (list_length(arguments->boundaries) > 0)
		{
			bool isComparisonValid = false;
			if (CompareBsonValueAndType(value, llast(arguments->boundaries),
										&isComparisonValid) <= 0)
			{
				ereport(ERROR, (errcode(MongoLocation40194),
								errmsg(
									"The $bucket 'boundaries' field must be an array of values in ascending order."),
								errhint(
									"The $bucket 'boundaries' field must be an array of values in ascending order.")));
			}
		}
		boundaryType = value->value_type;
		bson_value_t *boundaryValue = palloc(sizeof(bson_value_t));
		bson_value_copy(value, boundaryValue);
		arguments->boundaries = lappend(arguments->boundaries, boundaryValue);
	}

	if (list_length(arguments->boundaries) < 2)
	{
		ereport(ERROR, (errcode(MongoLocation40192),
						errmsg(
							"The $bucket 'boundaries' field must have at least 2 values, but found: %d",
							list_length(arguments->boundaries)),
						errhint(
							"The $bucket 'boundaries' field must have at least 2 values, but found: %d",
							list_length(arguments->boundaries))));
	}

	if (defaultBucket->value_type != BSON_TYPE_EOD)
	{
		if (list_length(arguments->boundaries) > 1 && defaultBucket->value_type ==
			boundaryType)
		{
			bool isComparisonValid = false;
			if (CompareBsonValueAndType(defaultBucket, linitial(arguments->boundaries),
										&isComparisonValid) >= 0 &&
				CompareBsonValueAndType(defaultBucket, llast(arguments->boundaries),
										&isComparisonValid) < 0)
			{
				ereport(ERROR, (errcode(MongoLocation40199),
								errmsg(
									"The $bucket 'default' field must be less than the lowest boundary or greater than or equal to the highest boundary.")));
			}
		}
		arguments->defaultBucket.kind = AggregationExpressionKind_Constant;
		arguments->defaultBucket.value = *defaultBucket;
	}
}
