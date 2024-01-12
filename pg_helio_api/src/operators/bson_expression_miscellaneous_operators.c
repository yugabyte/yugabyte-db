/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_expression_miscellaneous_operators.c
 *
 * Miscellaneous Operator expression implementations of BSON.
 * See also: https://www.mongodb.com/docs/manual/reference/operator/aggregation/#miscellaneous-operators
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>
#include <fmgr.h>

#include "io/bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "utils/mongo_errors.h"
#include "metadata/metadata_cache.h"
#include "opclass/pgmongo_bson_text_gin.h"
#include "vector/vector_utilities.h"
#include "utils/lsyscache.h"
#include "utils/fmgroids.h"
#include "catalog/pg_type.h"

/*
 * Evaluates the output of a $rand expression.
 * $rand is expressed as : {"$rand": {}} or {"$rand" : []} and can't take any inputs as of now
 * $rand generates a floating point number between 0 and 1
 * The below implementation uses Postgres' "drandom" method to generate the
 * random number in range of [0 - 1) (0 inclusive and 1 exclusive)
 */
void
HandleDollarRand(pgbson *doc, const bson_value_t *operatorValue,
				 ExpressionResult *expressionResult)
{
	if (operatorValue->value_type != BSON_TYPE_DOCUMENT &&
		operatorValue->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoDollarRandInvalidArg),
						errmsg("invalid parameter: expected an object ($rand)")));
	}
	else if ((operatorValue->value_type == BSON_TYPE_DOCUMENT &&
			  !IsBsonValueEmptyDocument(operatorValue)) ||
			 (operatorValue->value_type == BSON_TYPE_ARRAY &&
			  !IsBsonValueEmptyArray(operatorValue)))
	{
		ereport(ERROR, (errcode(MongoDollarRandNonEmptyArgument),
						errmsg("$rand does not currently accept arguments")));
	}

	float8 randomFloat8 = DatumGetFloat8(OidFunctionCall0(PostgresDrandomFunctionId()));
	bson_value_t randomValue = {
		.value_type = BSON_TYPE_DOUBLE,
		.value.v_double = randomFloat8
	};
	ExpressionResultSetValue(expressionResult, &randomValue);
}


/*
 * Applies the "$meta" expression operator. This currently expects the
 * bson_text_meta_qual be injected by the query planner as a restriction info
 * qual. That function will persist this as a function local state; This will
 * then use that to Evaluate the ts_rank function against the vector + query.
 * Ideally this info will be transited via the $let support variables and
 * is cached in that state.
 * TODO: Integrate to $let.
 */
void
HandleDollarMeta(pgbson *doc, const bson_value_t *operatorValue,
				 ExpressionResult *expressionResult)
{
	if (operatorValue->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("$meta expected value of type text, found %s",
							   BsonTypeName(operatorValue->value_type))));
	}

	StringView valueView = {
		.string = operatorValue->value.v_utf8.str,
		.length = operatorValue->value.v_utf8.len
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
	else if (StringViewEqualsCString(&valueView, "searchScore"))
	{
		/*https://www.mongodb.com/docs/manual/reference/operator/aggregation/meta/ */
		rank = EvaluateMetaSearchScore(doc);
	}
	else
	{
		ereport(ERROR, (errcode(MongoLocation17308),
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
