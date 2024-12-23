/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/operators/bson_expression_miscellaneous_operators.c
 *
 * Miscellaneous Operator expression implementations of BSON.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>
#include <fmgr.h>

#include "io/helio_bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "utils/helio_errors.h"
#include "metadata/metadata_cache.h"
#include "opclass/helio_bson_text_gin.h"
#include "vector/vector_utilities.h"
#include "utils/lsyscache.h"
#include "utils/fmgroids.h"
#include "catalog/pg_type.h"


/*
 * Parses a $rand expression.
 * $rand is expressed as : {"$rand": {}} or {"$rand" : []} and can't take any inputs as of now
 */
void
ParseDollarRand(const bson_value_t *argument, AggregationExpressionData *data,
				ParseAggregationExpressionContext *context)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT &&
		argument->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION10065),
						errmsg("invalid parameter: expected an object ($rand)")));
	}
	else if ((argument->value_type == BSON_TYPE_DOCUMENT &&
			  !IsBsonValueEmptyDocument(argument)) ||
			 (argument->value_type == BSON_TYPE_ARRAY &&
			  !IsBsonValueEmptyArray(argument)))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_DOLLARRANDNONEMPTYARGUMENT),
						errmsg("$rand does not currently accept arguments")));
	}

	/* The random value will be generated in HandlePreParsedDollarRand to avoid caching a constant value. */
	data->operator.arguments = NULL;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_Empty;
}


/*
 * Handles executing a pre-parsed $rand expression.
 * The below implementation uses Postgres' "drandom" method to generate the
 * random number in range of [0 - 1) (0 inclusive and 1 exclusive)
 */
void
HandlePreParsedDollarRand(pgbson *doc, void *arguments,
						  ExpressionResult *expressionResult)
{
	float8 randomFloat8 = DatumGetFloat8(OidFunctionCall0(PostgresDrandomFunctionId()));
	bson_value_t randomValue = {
		.value_type = BSON_TYPE_DOUBLE,
		.value.v_double = randomFloat8
	};

	ExpressionResultSetValue(expressionResult, &randomValue);
}


/*
 * Parses $meta expression.
 * $meta is expressed as : {"$meta": <text>} and can take only one argument.
 */
void
ParseDollarMeta(const bson_value_t *argument, AggregationExpressionData *data,
				ParseAggregationExpressionContext *context)
{
	if (argument->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
						errmsg("$meta expected value of type text, found %s",
							   BsonTypeName(argument->value_type))));
	}

	int numOfRequiredArgs = 1;
	AggregationExpressionData *parsedData = ParseFixedArgumentsForExpression(argument,
																			 numOfRequiredArgs,
																			 "$meta",
																			 &data->
																			 operator.
																			 argumentsKind,
																			 context);

	data->operator.arguments = parsedData;
}


/*
 * Handles executing a pre-parsed $meta expression.
 * This currently expects the son_text_meta_qual be injected
 * by the query planner as a restriction info qual.
 * That function will persist this as a function local state; This will
 * then use that to Evaluate the ts_rank function against the vector + query.
 * Ideally this info will be transited via the $let support variables and
 * is cached in that state.
 * TODO: Integrate to $let.
 */
void
HandlePreParsedDollarMeta(pgbson *doc, void *arguments,
						  ExpressionResult *expressionResult)
{
	AggregationExpressionData *argument = (AggregationExpressionData *) arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(argument, doc, &childResult, isNullOnEmpty);

	bson_value_t currentValue = childResult.value;

	StringView valueView = {
		.string = currentValue.value.v_utf8.str,
		.length = currentValue.value.v_utf8.len
	};

	if (StringViewEqualsCString(&valueView, "indexKey"))
	{
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						errmsg("Returning indexKey for $meta not supported")));
	}

	double rank = 0;
	if (StringViewEqualsCString(&valueView, "textScore"))
	{
		rank = EvaluateMetaTextScore(doc);
	}
	else if (StringViewEqualsCString(&valueView, "searchScore") ||
			 StringViewEqualsCString(&valueView, "vectorSearchScore"))
	{
		rank = EvaluateMetaSearchScore(doc);
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION17308),
						errmsg("Unsupported argument to $meta: %.*s",
							   valueView.length, valueView.string)));
	}

	/* Now we know it's just a $meta with a text score */
	bson_value_t result =
	{
		.value_type = BSON_TYPE_DOUBLE,
		.value.v_double = rank
	};
	ExpressionResultSetValue(expressionResult, &result);
}
