/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/aggregation/bson_aggregation_statistics.c
 *
 * Implementation of the aggregate functions typically used in
 * statistical analysis (such as covariance, standard deviation, etc.)
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>
#include <catalog/pg_type.h>
#include <math.h>

#include "utils/mongo_errors.h"
#include "types/decimal128.h"


/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

/* state used for pop and sample variance/covariance both */
/* for variance calculation x = y */
typedef struct BsonCovarianceAndVarianceAggState
{
	bson_value_t sx;
	bson_value_t sy;
	bson_value_t sxy;
	bson_value_t count;

	/* number of decimal values in current window, used to determine if we need to return decimal128 value */
	int decimalCount;
} BsonCovarianceAndVarianceAggState;


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

#define GENERATE_ERROR_MSG(ErrorMessage, Element1, Element2) \
	ErrorMessage \
	# Element1 " = %s, " # Element2 " = %s", \
	BsonValueToJsonForLogging(&(Element1)), BsonValueToJsonForLogging(&(Element2))

#define HANDLE_DECIMAL_OP_ERROR(OperatorFuncName, Element1, Element2, Result, \
								ErrorMessage) \
	do { \
		Decimal128Result decimalOpResult = (OperatorFuncName) (&(Element1), &(Element2), \
															   &(Result)); \
		if (decimalOpResult != Decimal128Result_Success && decimalOpResult != \
			Decimal128Result_Inexact) { \
			ereport(ERROR, (errcode(MongoInternalError)), \
					errmsg(GENERATE_ERROR_MSG(ErrorMessage, Element1, Element2))); \
		} \
	} while (0)

static bytea * AllocateBsonCovarianceOrVarianceAggState(void);
static void CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr(const
																  BsonCovarianceAndVarianceAggState
																  *leftState, const
																  BsonCovarianceAndVarianceAggState
																  *rightState,
																  BsonCovarianceAndVarianceAggState
																  *currentState);
static void CalculateInvFuncForCovarianceOrVarianceWithYCAlgr(const
															  bson_value_t *newXValue,
															  const bson_value_t *
															  newYValue,
															  BsonCovarianceAndVarianceAggState
															  *currentState);
static void CalculateSFuncForCovarianceOrVarianceWithYCAlgr(const bson_value_t *newXValue,
															const bson_value_t *newYValue,
															BsonCovarianceAndVarianceAggState
															*currentState);


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(bson_covariance_pop_samp_transition);
PG_FUNCTION_INFO_V1(bson_covariance_pop_samp_combine);
PG_FUNCTION_INFO_V1(bson_covariance_pop_samp_invtransition);
PG_FUNCTION_INFO_V1(bson_covariance_pop_final);
PG_FUNCTION_INFO_V1(bson_covariance_samp_final);
PG_FUNCTION_INFO_V1(bson_std_dev_pop_samp_transition);
PG_FUNCTION_INFO_V1(bson_std_dev_pop_samp_combine);
PG_FUNCTION_INFO_V1(bson_std_dev_pop_final);
PG_FUNCTION_INFO_V1(bson_std_dev_samp_final);


/*
 * Transition function for the BSONCOVARIANCEPOP and BSONCOVARIANCESAMP aggregate.
 * Use the Youngs-Cramer algorithm to incorporate the new value into the
 * transition values.
 * If calculating variance, we use X and Y as the same value.
 */
Datum
bson_covariance_pop_samp_transition(PG_FUNCTION_ARGS)
{
	bytea *bytes;
	BsonCovarianceAndVarianceAggState *currentState;

	/* If the intermediate state has never been initialized, create it */
	if (PG_ARGISNULL(0))
	{
		MemoryContext aggregateContext;
		if (AggCheckCallContext(fcinfo, &aggregateContext) != AGG_CONTEXT_WINDOW)
		{
			ereport(ERROR, errmsg(
						"window aggregate function called in non-window-aggregate context"));
		}

		/* Create the aggregate state in the aggregate context. */
		MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);

		bytes = AllocateBsonCovarianceOrVarianceAggState();

		currentState = (BsonCovarianceAndVarianceAggState *) VARDATA(bytes);
		currentState->sx.value_type = BSON_TYPE_DECIMAL128;
		SetDecimal128Zero(&currentState->sx);
		currentState->sy.value_type = BSON_TYPE_DECIMAL128;
		SetDecimal128Zero(&currentState->sy);
		currentState->sxy.value_type = BSON_TYPE_DECIMAL128;
		SetDecimal128Zero(&currentState->sxy);
		currentState->count.value_type = BSON_TYPE_DECIMAL128;
		SetDecimal128Zero(&currentState->count);
		currentState->decimalCount = 0;

		MemoryContextSwitchTo(oldContext);
	}
	else
	{
		bytes = PG_GETARG_BYTEA_P(0);
		currentState = (BsonCovarianceAndVarianceAggState *) VARDATA_ANY(bytes);
	}

	pgbson *currentXValue = PG_GETARG_MAYBE_NULL_PGBSON(1);
	pgbson *currentYValue = PG_GETARG_MAYBE_NULL_PGBSON(2);

	if (currentXValue == NULL || IsPgbsonEmptyDocument(currentXValue) || currentYValue ==
		NULL || IsPgbsonEmptyDocument(currentYValue))
	{
		PG_RETURN_POINTER(bytes);
	}

	pgbsonelement currentXValueElement;
	PgbsonToSinglePgbsonElement(currentXValue, &currentXValueElement);
	pgbsonelement currentYValueElement;
	PgbsonToSinglePgbsonElement(currentYValue, &currentYValueElement);

	/* we should ignore numeric values */
	if (BsonValueIsNumber(&currentXValueElement.bsonValue) && BsonValueIsNumber(
			&currentYValueElement.bsonValue))
	{
		CalculateSFuncForCovarianceOrVarianceWithYCAlgr(&currentXValueElement.bsonValue,
														&currentYValueElement.bsonValue,
														currentState);
	}

	PG_RETURN_POINTER(bytes);
}


/*
 * Applies the "combine function" (COMBINEFUNC) for BSONCOVARIANCEPOP and BSONCOVARIANCESAMP.
 * takes two of the aggregate state structures (BsonCovarianceAndVarianceAggState)
 * and combines them to form a new BsonCovarianceAndVarianceAggState that has the combined
 * sum and count.
 */
Datum
bson_covariance_pop_samp_combine(PG_FUNCTION_ARGS)
{
	MemoryContext aggregateContext;
	if (AggCheckCallContext(fcinfo, &aggregateContext) != AGG_CONTEXT_WINDOW)
	{
		ereport(ERROR, errmsg(
					"window aggregate function called in non-window-aggregate context"));
	}

	/* Create the aggregate state in the aggregate context. */
	MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);

	bytea *combinedStateBytes = AllocateBsonCovarianceOrVarianceAggState();
	BsonCovarianceAndVarianceAggState *currentState =
		(BsonCovarianceAndVarianceAggState *) VARDATA_ANY(combinedStateBytes);

	MemoryContextSwitchTo(oldContext);

	/* Handle either left or right being null. A new state needs to be allocated regardless */
	currentState->sx.value_type = BSON_TYPE_DECIMAL128;
	SetDecimal128Zero(&currentState->sx);
	currentState->sy.value_type = BSON_TYPE_DECIMAL128;
	SetDecimal128Zero(&currentState->sy);
	currentState->sxy.value_type = BSON_TYPE_DECIMAL128;
	SetDecimal128Zero(&currentState->sxy);
	currentState->count.value_type = BSON_TYPE_DECIMAL128;
	SetDecimal128Zero(&currentState->count);
	currentState->decimalCount = 0;

	/* handle left or right being null */
	/* It's worth handling the special cases N1 = 0 and N2 = 0 separately */
	/* since those cases are trivial, and we then don't need to worry about */
	/* division-by-zero errors in the general case. */
	if (PG_ARGISNULL(0))
	{
		if (PG_ARGISNULL(1))
		{
			PG_RETURN_NULL();
		}
		memcpy(VARDATA(combinedStateBytes), VARDATA_ANY(PG_GETARG_BYTEA_P(1)),
			   sizeof(BsonCovarianceAndVarianceAggState));
	}
	else if (PG_ARGISNULL(1))
	{
		if (PG_ARGISNULL(0))
		{
			PG_RETURN_NULL();
		}
		memcpy(VARDATA(combinedStateBytes), VARDATA_ANY(PG_GETARG_BYTEA_P(0)),
			   sizeof(BsonCovarianceAndVarianceAggState));
	}
	else
	{
		BsonCovarianceAndVarianceAggState *leftState =
			(BsonCovarianceAndVarianceAggState *) VARDATA_ANY(PG_GETARG_BYTEA_P(0));
		BsonCovarianceAndVarianceAggState *rightState =
			(BsonCovarianceAndVarianceAggState *) VARDATA_ANY(PG_GETARG_BYTEA_P(1));

		CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr(leftState, rightState,
															  currentState);
	}

	PG_RETURN_POINTER(combinedStateBytes);
}


/*
 * Applies the "inverse transition function" (MINVFUNC) for BSONCOVARIANCEPOP and BSONCOVARIANCESAMP.
 * takes one aggregate state structures (BsonCovarianceAndVarianceAggState)
 * and single data point. Remove the single data from BsonCovarianceAndVarianceAggState
 */
Datum
bson_covariance_pop_samp_invtransition(PG_FUNCTION_ARGS)
{
	MemoryContext aggregateContext;
	if (AggCheckCallContext(fcinfo, &aggregateContext) != AGG_CONTEXT_WINDOW)
	{
		ereport(ERROR, errmsg(
					"window aggregate function called in non-window-aggregate context"));
	}

	bytea *bytes;
	BsonCovarianceAndVarianceAggState *currentState;

	if (PG_ARGISNULL(0))
	{
		PG_RETURN_NULL();
	}
	else
	{
		bytes = PG_GETARG_BYTEA_P(0);
		currentState = (BsonCovarianceAndVarianceAggState *) VARDATA_ANY(bytes);
	}

	pgbson *currentXValue = PG_GETARG_MAYBE_NULL_PGBSON(1);
	pgbson *currentYValue = PG_GETARG_MAYBE_NULL_PGBSON(2);

	if (currentXValue == NULL || IsPgbsonEmptyDocument(currentXValue) || currentYValue ==
		NULL || IsPgbsonEmptyDocument(currentYValue))
	{
		PG_RETURN_POINTER(bytes);
	}

	pgbsonelement currentXValueElement;
	PgbsonToSinglePgbsonElement(currentXValue, &currentXValueElement);
	pgbsonelement currentYValueElement;
	PgbsonToSinglePgbsonElement(currentYValue, &currentYValueElement);

	if (!BsonTypeIsNumber(currentXValueElement.bsonValue.value_type) ||
		!BsonTypeIsNumber(currentYValueElement.bsonValue.value_type))
	{
		PG_RETURN_POINTER(bytes);
	}

	/* restart aggregate if NaN or Infinity in current state or current values */
	/* or count is 0 */
	if (IsBsonValueNaN(&currentState->sxy) || IsBsonValueInfinity(&currentState->sxy) ||
		IsBsonValueNaN(&currentXValueElement.bsonValue) || IsBsonValueInfinity(
			&currentXValueElement.bsonValue) ||
		IsBsonValueNaN(&currentYValueElement.bsonValue) || IsBsonValueInfinity(
			&currentYValueElement.bsonValue) ||
		BsonValueAsInt64(&currentState->count) == 0)
	{
		PG_RETURN_NULL();
	}

	CalculateInvFuncForCovarianceOrVarianceWithYCAlgr(&currentXValueElement.bsonValue,
													  &currentYValueElement.bsonValue,
													  currentState);

	PG_RETURN_POINTER(bytes);
}


/*
 * Applies the "final calculation" (FINALFUNC) for BSONCOVARIANCEPOP.
 * This takes the final value created and outputs a bson covariance pop
 * with the appropriate type.
 */
Datum
bson_covariance_pop_final(PG_FUNCTION_ARGS)
{
	bytea *covarianceIntermediateState = PG_ARGISNULL(0) ? NULL : PG_GETARG_BYTEA_P(0);

	pgbsonelement finalValue;
	finalValue.path = "";
	finalValue.pathLength = 0;
	if (covarianceIntermediateState != NULL)
	{
		bson_value_t decimalResult = { 0 };
		decimalResult.value_type = BSON_TYPE_DECIMAL128;
		BsonCovarianceAndVarianceAggState *covarianceState =
			(BsonCovarianceAndVarianceAggState *) VARDATA_ANY(
				covarianceIntermediateState);

		if (IsBsonValueNaN(&covarianceState->sxy) || IsBsonValueInfinity(
				&covarianceState->sxy) != 0)
		{
			decimalResult.value.v_decimal128 = covarianceState->sxy.value.v_decimal128;
		}
		else if (BsonValueAsInt64(&covarianceState->count) == 0)
		{
			/* Mongo returns null for empty sets or wrong input field count */
			finalValue.bsonValue.value_type = BSON_TYPE_NULL;
			PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
		}
		else if (BsonValueAsInt64(&covarianceState->count) == 1)
		{
			/* Mongo returns 0 for single numeric value */
			/* return double even if the value is decimal128 */
			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = 0;
			PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
		}
		else
		{
			HANDLE_DECIMAL_OP_ERROR(DivideDecimal128Numbers, covarianceState->sxy,
									covarianceState->count, decimalResult,
									"Failed while calculating bson_covariance_pop_final decimalResult for these values: ");
		}

		/* if there is at least one decimal value in current window, we should return decimal128; otherwise, return double */
		if (covarianceState->decimalCount > 0)
		{
			finalValue.bsonValue.value_type = BSON_TYPE_DECIMAL128;
			finalValue.bsonValue.value.v_decimal128 = decimalResult.value.v_decimal128;
		}
		else
		{
			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = BsonValueAsDouble(&decimalResult);
		}
	}
	else
	{
		/* Mongo returns null for empty sets */
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;
	}

	PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
}


/*
 * Applies the "final calculation" (FINALFUNC) for BSONCOVARIANCESAMP.
 * This takes the final value created and outputs a bson covariance samp
 * with the appropriate type.
 */
Datum
bson_covariance_samp_final(PG_FUNCTION_ARGS)
{
	bytea *covarianceIntermediateState = PG_ARGISNULL(0) ? NULL : PG_GETARG_BYTEA_P(0);

	pgbsonelement finalValue;
	finalValue.path = "";
	finalValue.pathLength = 0;
	if (covarianceIntermediateState != NULL)
	{
		bson_value_t decimalResult = { 0 };
		decimalResult.value_type = BSON_TYPE_DECIMAL128;
		BsonCovarianceAndVarianceAggState *covarianceState =
			(BsonCovarianceAndVarianceAggState *) VARDATA_ANY(
				covarianceIntermediateState);

		if (IsBsonValueNaN(&covarianceState->sxy) || IsBsonValueInfinity(
				&covarianceState->sxy))
		{
			decimalResult.value.v_decimal128 = covarianceState->sxy.value.v_decimal128;
		}
		else if (BsonValueAsInt64(&covarianceState->count) == 0 || BsonValueAsInt64(
					 &covarianceState->count) == 1)
		{
			/* Mongo returns null for empty sets, single numeric value or wrong input field count */
			finalValue.bsonValue.value_type = BSON_TYPE_NULL;
			PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
		}
		else
		{
			bson_value_t decimalOne;
			decimalOne.value_type = BSON_TYPE_DECIMAL128;
			decimalOne.value.v_decimal128 = GetDecimal128FromInt64(1);

			bson_value_t countMinus1;
			countMinus1.value_type = BSON_TYPE_DECIMAL128;
			HANDLE_DECIMAL_OP_ERROR(SubtractDecimal128Numbers, covarianceState->count,
									decimalOne, countMinus1,
									"Failed while calculating bson_covariance_samp_final countMinus1 for these values: ");

			HANDLE_DECIMAL_OP_ERROR(DivideDecimal128Numbers, covarianceState->sxy,
									countMinus1, decimalResult,
									"Failed while calculating bson_covariance_samp_final decimalResult for these values: ");
		}

		/* if there is at least one decimal value in current window, we should return decimal128; otherwise, return double */
		if (covarianceState->decimalCount > 0)
		{
			finalValue.bsonValue.value_type = BSON_TYPE_DECIMAL128;
			finalValue.bsonValue.value.v_decimal128 = decimalResult.value.v_decimal128;
		}
		else
		{
			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = BsonValueAsDouble(&decimalResult);
		}
	}
	else
	{
		/* Mongo returns null for empty sets */
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;
	}

	PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
}


/*
 * Transition function for the BSON_STD_DEV_POP and BSON_STD_DEV_SAMP aggregate.
 * Implementation refer to https://github.com/postgres/postgres/blob/master/src/backend/utils/adt/float.c#L2950
 */
Datum
bson_std_dev_pop_samp_transition(PG_FUNCTION_ARGS)
{
	bytea *bytes;
	BsonCovarianceAndVarianceAggState *currentState;

	/* If the intermediate state has never been initialized, create it */
	if (PG_ARGISNULL(0))
	{
		MemoryContext aggregateContext;
		if (!AggCheckCallContext(fcinfo, &aggregateContext))
		{
			ereport(ERROR, errmsg(
						"aggregate function std dev pop sample transition called in non-aggregate context"));
		}

		/* Create the aggregate state in the aggregate context. */
		MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);

		bytes = AllocateBsonCovarianceOrVarianceAggState();

		currentState = (BsonCovarianceAndVarianceAggState *) VARDATA(bytes);
		currentState->sx.value_type = BSON_TYPE_DECIMAL128;
		SetDecimal128Zero(&currentState->sx);
		currentState->sy.value_type = BSON_TYPE_DECIMAL128;
		SetDecimal128Zero(&currentState->sy);
		currentState->sxy.value_type = BSON_TYPE_DECIMAL128;
		SetDecimal128Zero(&currentState->sxy);
		currentState->count.value_type = BSON_TYPE_DECIMAL128;
		SetDecimal128Zero(&currentState->count);
		currentState->decimalCount = 0;

		MemoryContextSwitchTo(oldContext);
	}
	else
	{
		bytes = PG_GETARG_BYTEA_P(0);
		currentState = (BsonCovarianceAndVarianceAggState *) VARDATA_ANY(bytes);
	}
	pgbson *currentValue = PG_GETARG_MAYBE_NULL_PGBSON(1);

	if (currentValue == NULL || IsPgbsonEmptyDocument(currentValue))
	{
		PG_RETURN_POINTER(bytes);
	}

	pgbsonelement currentValueElement;
	PgbsonToSinglePgbsonElement(currentValue, &currentValueElement);

	/* mongo ignores non-numeric values */
	if (BsonValueIsNumber(&currentValueElement.bsonValue))
	{
		CalculateSFuncForCovarianceOrVarianceWithYCAlgr(&currentValueElement.bsonValue,
														&currentValueElement.bsonValue,
														currentState);
	}

	PG_RETURN_POINTER(bytes);
}


/*
 * Applies the "combine function" (COMBINEFUNC) for std_dev_pop and std_dev_samp.
 * takes two of the aggregate state structures (bson_std_dev_agg_state)
 * and combines them to form a new bson_std_dev_agg_state that has the combined
 * sum and count.
 */
Datum
bson_std_dev_pop_samp_combine(PG_FUNCTION_ARGS)
{
	MemoryContext aggregateContext;
	if (!AggCheckCallContext(fcinfo, &aggregateContext))
	{
		ereport(ERROR, errmsg("aggregate function called in non-aggregate context"));
	}

	/* Create the aggregate state in the aggregate context. */
	MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);

	bytea *combinedStateBytes = AllocateBsonCovarianceOrVarianceAggState();
	BsonCovarianceAndVarianceAggState *currentState =
		(BsonCovarianceAndVarianceAggState *) VARDATA_ANY(
			combinedStateBytes);

	MemoryContextSwitchTo(oldContext);

	/* Handle either left or right being null. A new state needs to be allocated regardless */
	currentState->sx.value_type = BSON_TYPE_DECIMAL128;
	SetDecimal128Zero(&currentState->sx);
	currentState->sy.value_type = BSON_TYPE_DECIMAL128;
	SetDecimal128Zero(&currentState->sy);
	currentState->sxy.value_type = BSON_TYPE_DECIMAL128;
	SetDecimal128Zero(&currentState->sxy);
	currentState->count.value_type = BSON_TYPE_DECIMAL128;
	SetDecimal128Zero(&currentState->count);
	currentState->decimalCount = 0;

	/*--------------------
	 * The transition values combine using a generalization of the
	 * Youngs-Cramer algorithm as follows:
	 *
	 *	N = N1 + N2
	 *	Sx = Sx1 + Sx2
	 *	Sxx = Sxx1 + Sxx2 + N1 * N2 * (Sx1/N1 - Sx2/N2)^2 / N;
	 *
	 * It's worth handling the special cases N1 = 0 and N2 = 0 separately
	 * since those cases are trivial, and we then don't need to worry about
	 * division-by-zero errors in the general case.
	 *--------------------
	 */

	if (PG_ARGISNULL(0))
	{
		if (PG_ARGISNULL(1))
		{
			PG_RETURN_NULL();
		}
		memcpy(VARDATA(combinedStateBytes), VARDATA_ANY(PG_GETARG_BYTEA_P(1)),
			   sizeof(BsonCovarianceAndVarianceAggState));
	}
	else if (PG_ARGISNULL(1))
	{
		if (PG_ARGISNULL(0))
		{
			PG_RETURN_NULL();
		}
		memcpy(VARDATA(combinedStateBytes), VARDATA_ANY(PG_GETARG_BYTEA_P(0)),
			   sizeof(BsonCovarianceAndVarianceAggState));
	}
	else
	{
		BsonCovarianceAndVarianceAggState *leftState =
			(BsonCovarianceAndVarianceAggState *) VARDATA_ANY(
				PG_GETARG_BYTEA_P(0));
		BsonCovarianceAndVarianceAggState *rightState =
			(BsonCovarianceAndVarianceAggState *) VARDATA_ANY(
				PG_GETARG_BYTEA_P(1));

		CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr(leftState, rightState,
															  currentState);
	}

	PG_RETURN_POINTER(combinedStateBytes);
}


/*
 * Applies the "final calculation" (FINALFUNC) for std_dev_pop.
 * This takes the final value created and outputs a bson "std_dev_pop"
 * with the appropriate type.
 */
Datum
bson_std_dev_pop_final(PG_FUNCTION_ARGS)
{
	bytea *stdDevIntermediateState = PG_ARGISNULL(0) ? NULL : PG_GETARG_BYTEA_P(0);

	pgbsonelement finalValue;
	finalValue.path = "";
	finalValue.pathLength = 0;
	if (stdDevIntermediateState != NULL)
	{
		BsonCovarianceAndVarianceAggState *stdDevState =
			(BsonCovarianceAndVarianceAggState *) VARDATA_ANY(
				stdDevIntermediateState);

		if (IsBsonValueNaN(&stdDevState->sxy) || IsBsonValueInfinity(&stdDevState->sxy))
		{
			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = NAN;
		}
		else if (BsonValueAsInt64(&stdDevState->count) == 0)
		{
			/* Mongo returns $null for empty sets */
			finalValue.bsonValue.value_type = BSON_TYPE_NULL;
		}
		else if (BsonValueAsInt64(&stdDevState->count) == 1)
		{
			/* Mongo returns 0 for single numeric value */
			finalValue.bsonValue.value_type = BSON_TYPE_INT32;
			finalValue.bsonValue.value.v_int32 = 0;
		}
		else
		{
			bson_value_t result;
			result.value_type = BSON_TYPE_DECIMAL128;
			HANDLE_DECIMAL_OP_ERROR(DivideDecimal128Numbers, stdDevState->sxy,
									stdDevState->count, result,
									"Failed while calculating bson_std_dev_pop_final result for these values: ");

			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = sqrt(BsonValueAsDouble(&result));
		}
	}
	else
	{
		/* Mongo returns $null for empty sets */
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;
	}

	PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
}


/*
 * Applies the "final calculation" (FINALFUNC) for std_dev_samp.
 * This takes the final value created and outputs a bson "std_dev_samp"
 * with the appropriate type.
 */
Datum
bson_std_dev_samp_final(PG_FUNCTION_ARGS)
{
	bytea *stdDevIntermediateState = PG_ARGISNULL(0) ? NULL : PG_GETARG_BYTEA_P(0);

	pgbsonelement finalValue;
	finalValue.path = "";
	finalValue.pathLength = 0;
	if (stdDevIntermediateState != NULL)
	{
		BsonCovarianceAndVarianceAggState *stdDevState =
			(BsonCovarianceAndVarianceAggState *) VARDATA_ANY(
				stdDevIntermediateState);

		if (BsonValueAsInt64(&stdDevState->count) == 0 || BsonValueAsInt64(
				&stdDevState->count) == 1)
		{
			/* Mongo returns $null for empty sets or single numeric value */
			finalValue.bsonValue.value_type = BSON_TYPE_NULL;
		}
		else if (IsBsonValueInfinity(&stdDevState->sxy))
		{
			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = NAN;
		}
		else
		{
			bson_value_t decimalOne;
			decimalOne.value_type = BSON_TYPE_DECIMAL128;
			decimalOne.value.v_decimal128 = GetDecimal128FromInt64(1);

			bson_value_t countMinus1;
			countMinus1.value_type = BSON_TYPE_DECIMAL128;
			HANDLE_DECIMAL_OP_ERROR(SubtractDecimal128Numbers, stdDevState->count,
									decimalOne, countMinus1,
									"Failed while calculating bson_std_dev_samp_final countMinus1 for these values: ");

			bson_value_t result;
			result.value_type = BSON_TYPE_DECIMAL128;
			HANDLE_DECIMAL_OP_ERROR(DivideDecimal128Numbers, stdDevState->sxy,
									countMinus1, result,
									"Failed while calculating bson_std_dev_samp_final result for these values: ");

			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = sqrt(BsonValueAsDouble(&result));
		}
	}
	else
	{
		/* Mongo returns $null for empty sets */
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;
	}

	PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */

bytea *
AllocateBsonCovarianceOrVarianceAggState()
{
	int bson_size = sizeof(BsonCovarianceAndVarianceAggState) + VARHDRSZ;
	bytea *combinedStateBytes = (bytea *) palloc0(bson_size);
	SET_VARSIZE(combinedStateBytes, bson_size);

	return combinedStateBytes;
}


/*
 * Function to calculate variance/covariance state for inverse function.
 * If calculating variance, we use X and Y as the same value.
 *
 * According to a generalization of the
 * Youngs-Cramer algorithm formular, we update the state data like this
 * ```
 *  N^ = N - 1
 *  Sx^ = Sx - X
 *  Sy^ = Sy - Y
 *  Sxy^ = Sxy - N^/N * (Sx^/N^ - X) * (Sy^/N^ - Y)
 *  ```
 */
static void
CalculateInvFuncForCovarianceOrVarianceWithYCAlgr(const bson_value_t *newXValue,
												  const bson_value_t *newYValue,
												  BsonCovarianceAndVarianceAggState *
												  currentState)
{
	/* update the count of decimal values accordingly */
	if (newXValue->value_type == BSON_TYPE_DECIMAL128 ||
		newYValue->value_type == BSON_TYPE_DECIMAL128)
	{
		currentState->decimalCount--;
	}

	bson_value_t decimalCurrentXValue;
	decimalCurrentXValue.value_type = BSON_TYPE_DECIMAL128;
	decimalCurrentXValue.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
		newXValue);

	bson_value_t decimalCurrentYValue;
	decimalCurrentYValue.value_type = BSON_TYPE_DECIMAL128;
	decimalCurrentYValue.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
		newYValue);

	bson_value_t decimalOne;
	decimalOne.value_type = BSON_TYPE_DECIMAL128;
	decimalOne.value.v_decimal128 = GetDecimal128FromInt64(1);

	/* decimalN = currentState->count */
	bson_value_t decimalN;
	decimalN.value_type = BSON_TYPE_DECIMAL128;
	decimalN.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
		&currentState->count);

	/* currentState->count -= 1.0 */
	HANDLE_DECIMAL_OP_ERROR(SubtractDecimal128Numbers, currentState->count, decimalOne,
							currentState->count,
							"Failed while calculating CalculateInvFuncForCovarianceOrVarianceWithYCAlgr count for these values: ");

	/* currentState->sx -= decimalCurrentXValue */
	HANDLE_DECIMAL_OP_ERROR(SubtractDecimal128Numbers, currentState->sx,
							decimalCurrentXValue, currentState->sx,
							"Failed while calculating CalculateInvFuncForCovarianceOrVarianceWithYCAlgr sx for these values: ");

	/* currentState->sy -= decimalCurrentYValue */
	HANDLE_DECIMAL_OP_ERROR(SubtractDecimal128Numbers, currentState->sy,
							decimalCurrentYValue, currentState->sy,
							"Failed while calculating CalculateInvFuncForCovarianceOrVarianceWithYCAlgr sy for these values: ");

	bson_value_t decimalXTmp;
	decimalXTmp.value_type = BSON_TYPE_DECIMAL128;

	bson_value_t decimalYTmp;
	decimalYTmp.value_type = BSON_TYPE_DECIMAL128;

	/* decimalXTmp = currentState->sx - decimalCurrentXValue * currentState->count */
	/* X * N^ */
	HANDLE_DECIMAL_OP_ERROR(MultiplyDecimal128Numbers, decimalCurrentXValue,
							currentState->count, decimalXTmp,
							"Failed while calculating CalculateInvFuncForCovarianceOrVarianceWithYCAlgr decimalXTmp_1 for these values: ");

	/* Sx^ - X * N^ */
	HANDLE_DECIMAL_OP_ERROR(SubtractDecimal128Numbers, currentState->sx, decimalXTmp,
							decimalXTmp,
							"Failed while calculating CalculateInvFuncForCovarianceOrVarianceWithYCAlgr decimalXTmp_2 for these values: ");

	/* decimalYTmp = currentState->sy - decimalCurrentYValue * currentState->count */
	/* Y * N^ */
	HANDLE_DECIMAL_OP_ERROR(MultiplyDecimal128Numbers, decimalCurrentYValue,
							currentState->count, decimalYTmp,
							"Failed while calculating CalculateInvFuncForCovarianceOrVarianceWithYCAlgr decimalYTmp_1 = for these values: ");

	/* Sy^ - Y * N^ */
	HANDLE_DECIMAL_OP_ERROR(SubtractDecimal128Numbers, currentState->sy, decimalYTmp,
							decimalYTmp,
							"Failed while calculating CalculateInvFuncForCovarianceOrVarianceWithYCAlgr decimalYTmp_2 for these values: ");

	bson_value_t decimalXYTmp;
	decimalXYTmp.value_type = BSON_TYPE_DECIMAL128;

	/* decimalXYTmp = decimalXTmp * decimalYTmp */
	/* (Sx^ - X * N^) * (Sy^ - Y * N^) */
	HANDLE_DECIMAL_OP_ERROR(MultiplyDecimal128Numbers, decimalXTmp, decimalYTmp,
							decimalXYTmp,
							"Failed while calculating CalculateInvFuncForCovarianceOrVarianceWithYCAlgr decimalXYTmp for these values: ");

	bson_value_t decimalTmpNxx;
	decimalTmpNxx.value_type = BSON_TYPE_DECIMAL128;

	/* decimalTmpNxx = decimalN * currentState->count */
	HANDLE_DECIMAL_OP_ERROR(MultiplyDecimal128Numbers, decimalN, currentState->count,
							decimalTmpNxx,
							"Failed while calculating CalculateInvFuncForCovarianceOrVarianceWithYCAlgr decimalTmpNxx for these values: ");

	bson_value_t decimalTmp;
	decimalTmp.value_type = BSON_TYPE_DECIMAL128;

	/* decimalTmp = decimalXYTmp / decimalTmpNxx */
	/* (Sx^ - X * N^) * (Sy^ - Y * N^) / (N * N^) = N^/N * (Sx^/N^ - X) * (Sy^/N^ - Y) */
	HANDLE_DECIMAL_OP_ERROR(DivideDecimal128Numbers, decimalXYTmp, decimalTmpNxx,
							decimalTmp,
							"Failed while calculating CalculateInvFuncForCovarianceOrVarianceWithYCAlgr decimalTmp for these values: ");

	/* currentState->sxy -= decimalTmp */
	HANDLE_DECIMAL_OP_ERROR(SubtractDecimal128Numbers, currentState->sxy, decimalTmp,
							currentState->sxy,
							"Failed while calculating CalculateInvFuncForCovarianceOrVarianceWithYCAlgr sxy for these values: ");
}


/*
 * Function to calculate variance/covariance state for combine function.
 * If calculating variance, we use X and Y as the same value.
 *
 * The transition values combine using a generalization of the
 * Youngs-Cramer algorithm as follows:
 *
 *	N = N1 + N2
 *	Sx = Sx1 + Sx2
 *	Sy = Sy1 + Sy2
 *	Sxy = Sxy1 + Sxy2 + N1 * N2 * (Sx1/N1 - Sx2/N2) * (Sy1/N1 - Sy2/N2) / N;
 */
static void
CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr(const
													  BsonCovarianceAndVarianceAggState *
													  leftState, const
													  BsonCovarianceAndVarianceAggState *
													  rightState,
													  BsonCovarianceAndVarianceAggState *
													  currentState)
{
	/* If either of left or right node's count is 0, we can just copy the other node's state and return */
	if (IsDecimal128Zero(&leftState->count))
	{
		memcpy(currentState, rightState, sizeof(BsonCovarianceAndVarianceAggState));
		return;
	}
	else if (IsDecimal128Zero(&rightState->count))
	{
		memcpy(currentState, leftState, sizeof(BsonCovarianceAndVarianceAggState));
		return;
	}

	/* update the count of decimal values */
	currentState->decimalCount = leftState->decimalCount + rightState->decimalCount;

	/* currentState->count = leftState->count + rightState->count */
	HANDLE_DECIMAL_OP_ERROR(AddDecimal128Numbers, leftState->count, rightState->count,
							currentState->count,
							"Failed while calculating CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr count for these values: ");

	/* handle infinities first */
	/* if infinity values from left and right node with different signs, return NaN */
	/* if with the same sign, return the infinity value */
	/* if no infinity values, continue to calculate the covariance/variance */
	int isInfinityLeft = IsBsonValueInfinity(&leftState->sxy);
	int isInfinityRight = IsBsonValueInfinity(&rightState->sxy);
	if (isInfinityLeft * isInfinityRight == -1)
	{
		currentState->sxy.value_type = BSON_TYPE_DECIMAL128;
		SetDecimal128NaN(&currentState->sxy);
		return;
	}
	else if (isInfinityLeft + isInfinityRight != 0)
	{
		currentState->sxy.value_type = BSON_TYPE_DECIMAL128;
		if (isInfinityLeft + isInfinityRight > 0)
		{
			SetDecimal128PositiveInfinity(&currentState->sxy);
		}
		else
		{
			SetDecimal128NegativeInfinity(&currentState->sxy);
		}
		return;
	}

	/* currentState->sx = leftState->sx + rightState->sx */
	HANDLE_DECIMAL_OP_ERROR(AddDecimal128Numbers, leftState->sx, rightState->sx,
							currentState->sx,
							"Failed while calculating CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr sx for these values: ");

	/* currentState->sy = leftState->sy + rightState->sy */
	HANDLE_DECIMAL_OP_ERROR(AddDecimal128Numbers, leftState->sy, rightState->sy,
							currentState->sy,
							"Failed while calculating CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr sy for these values: ");

	bson_value_t decimalTmpLeft;
	decimalTmpLeft.value_type = BSON_TYPE_DECIMAL128;
	SetDecimal128Zero(&decimalTmpLeft);

	bson_value_t decimalTmpRight;
	decimalTmpRight.value_type = BSON_TYPE_DECIMAL128;
	SetDecimal128Zero(&decimalTmpRight);

	bson_value_t decimalXTmp;
	decimalXTmp.value_type = BSON_TYPE_DECIMAL128;

	/* decimalXTmp = leftState->sx / leftState->count - rightState->sx / rightState->count; */
	/* leftState->count and rightState->count won't be 0 as we have checked outside */

	/* Sx1/N1 */
	HANDLE_DECIMAL_OP_ERROR(DivideDecimal128Numbers, leftState->sx, leftState->count,
							decimalTmpLeft,
							"Failed while calculating CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr decimalTmpLeft_1 for these values: ");

	/* Sx2/N2 */
	HANDLE_DECIMAL_OP_ERROR(DivideDecimal128Numbers, rightState->sx, rightState->count,
							decimalTmpRight,
							"Failed while calculating CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr decimalTmpRight_1 for these values: ");

	/* Sx1/N1 - Sx2/N2 */
	HANDLE_DECIMAL_OP_ERROR(SubtractDecimal128Numbers, decimalTmpLeft, decimalTmpRight,
							decimalXTmp,
							"Failed while calculating CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr decimalXTmp for these values: ");

	bson_value_t decimalYTmp;
	decimalYTmp.value_type = BSON_TYPE_DECIMAL128;

	/* decimalYTmp = leftState->sy / leftState->count - rightState->sy / rightState->count; */
	/* Sy1/N1 */
	HANDLE_DECIMAL_OP_ERROR(DivideDecimal128Numbers, leftState->sy, leftState->count,
							decimalTmpLeft,
							"Failed while calculating CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr decimalTmpLeft_2 for these values: ");

	/* Sy2/N2 */
	HANDLE_DECIMAL_OP_ERROR(DivideDecimal128Numbers, rightState->sy, rightState->count,
							decimalTmpRight,
							"Failed while calculating CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr decimalTmpRight_2 for these values: ");

	/* Sy1/N1 - Sy2/N2 */
	HANDLE_DECIMAL_OP_ERROR(SubtractDecimal128Numbers, decimalTmpLeft, decimalTmpRight,
							decimalYTmp,
							"Failed while calculating CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr decimalYTmp for these values: ");

	bson_value_t decimalNxx;
	decimalNxx.value_type = BSON_TYPE_DECIMAL128;

	/* decimalNxx = leftState->count * rightState->count; */
	HANDLE_DECIMAL_OP_ERROR(MultiplyDecimal128Numbers, leftState->count,
							rightState->count, decimalNxx,
							"Failed while calculating CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr decimalNxx for these values: ");

	bson_value_t decimalXYTmp;
	decimalXYTmp.value_type = BSON_TYPE_DECIMAL128;

	/* decimalXYTmp = decimalXTmp * decimalYTmp; */
	HANDLE_DECIMAL_OP_ERROR(MultiplyDecimal128Numbers, decimalXTmp, decimalYTmp,
							decimalXYTmp,
							"Failed while calculating CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr decimalXYTmp for these values: ");

	bson_value_t decimalTmp;
	decimalTmp.value_type = BSON_TYPE_DECIMAL128;

	/* decimalTmp = decimalNxx * decimalXYTmp; */
	/* N1 * N2 * (Sx1/N1 - Sx2/N2) * (Sy1/N1 - Sy2/N2) */
	HANDLE_DECIMAL_OP_ERROR(MultiplyDecimal128Numbers, decimalNxx, decimalXYTmp,
							decimalTmp,
							"Failed while calculating CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr decimalTmp for these values: ");

	bson_value_t decimalD;
	decimalD.value_type = BSON_TYPE_DECIMAL128;
	SetDecimal128Zero(&decimalD);

	/* decimalD = decimalTmp / N; */
	/* N1 * N2 * (Sx1/N1 - Sx2/N2) * (Sy1/N1 - Sy2/N2) / N; */
	/* didn't check if N is 0, as we should not meet that case */
	HANDLE_DECIMAL_OP_ERROR(DivideDecimal128Numbers, decimalTmp, currentState->count,
							decimalD,
							"Failed while calculating CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr decimalD for these values: ");

	/* Sxy1 + Sxy2 */
	HANDLE_DECIMAL_OP_ERROR(AddDecimal128Numbers, leftState->sxy, rightState->sxy,
							currentState->sxy,
							"Failed while calculating CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr sxy_1 for these values: ");

	/* Sxy1 + Sxy2 + N1 * N2 * (Sx1/N1 - Sx2/N2) * (Sy1/N1 - Sy2/N2) / N */
	HANDLE_DECIMAL_OP_ERROR(AddDecimal128Numbers, currentState->sxy, decimalD,
							currentState->sxy,
							"Failed while calculating CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr sxy_2 for these values: ");
}


/*
 * Function to calculate variance/covariance state for transition function.
 * Use the Youngs-Cramer algorithm to incorporate the new value into the
 * transition values.
 * If calculating variance, we use X and Y as the same value.
 *
 * N = N + 1
 * Sx = Sx + X
 * Sy = Sy + Y
 * Sxy = Sxy + (N - 1) / N * (X - Sx / N) * (Y - Sy / N)
 *
 */
static void
CalculateSFuncForCovarianceOrVarianceWithYCAlgr(const bson_value_t *newXValue,
												const bson_value_t *newYValue,
												BsonCovarianceAndVarianceAggState *
												currentState)
{
	bson_value_t decimalOne;
	decimalOne.value_type = BSON_TYPE_DECIMAL128;
	decimalOne.value.v_decimal128 = GetDecimal128FromInt64(1);

	bson_value_t decimalXTmp;
	decimalXTmp.value_type = BSON_TYPE_DECIMAL128;

	bson_value_t decimalYTmp;
	decimalYTmp.value_type = BSON_TYPE_DECIMAL128;

	bson_value_t decimalXYTmp;
	decimalXYTmp.value_type = BSON_TYPE_DECIMAL128;

	bson_value_t decimalNxx;
	decimalNxx.value_type = BSON_TYPE_DECIMAL128;

	bson_value_t decimalCurrentXValue;
	decimalCurrentXValue.value_type = BSON_TYPE_DECIMAL128;
	decimalCurrentXValue.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
		newXValue);

	bson_value_t decimalCurrentYValue;
	decimalCurrentYValue.value_type = BSON_TYPE_DECIMAL128;
	decimalCurrentYValue.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
		newYValue);

	/* decimalN = currentState->count */
	bson_value_t decimalN;
	decimalN.value_type = BSON_TYPE_DECIMAL128;
	decimalN.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
		&currentState->count);

	/* update the count of decimal values accordingly */
	if (newXValue->value_type == BSON_TYPE_DECIMAL128 ||
		newYValue->value_type == BSON_TYPE_DECIMAL128)
	{
		currentState->decimalCount++;
	}

	/* decimalN += 1.0 */
	HANDLE_DECIMAL_OP_ERROR(AddDecimal128Numbers, decimalN, decimalOne, decimalN,
							"Failed while calculating CalculateSFuncForCovarianceOrVarianceWithYCAlgr decimalN for these values: ");

	/* NAN will be handled in later parts */
	/* focus on infinities first */
	/* We will check all the infinity values (if any) from Sxy(didn't update yet), X, Y */
	/* If all the infinity values have the same sign, we will return the infinity value */
	/* If any of the infinity values have different signs, we will return the NaN value */
	/* If no infinity values, we will continue the calculation */
	/* The return value of IsBsonValueInfinity */
	/* 0: finite number, 1: positive infinity, -1: negative infinity */
	int isInfinityX = IsBsonValueInfinity(newXValue);
	int isInfinityY = IsBsonValueInfinity(newYValue);
	int isInfinitySxy = IsBsonValueInfinity(&currentState->sxy);

	/* the product of two values is -1 means they are infinity values with different signs */
	if (isInfinityX * isInfinityY == -1 || isInfinityX * isInfinitySxy == -1 ||
		isInfinityY * isInfinitySxy == -1)
	/* infinities with different signs, return nan */
	{
		currentState->count = decimalN;
		currentState->sxy.value_type = BSON_TYPE_DECIMAL128;
		SetDecimal128NaN(&currentState->sxy);
		return;
	}
	else if (isInfinityX || isInfinityY || isInfinitySxy)
	/* infinities with the same sign, return infinity */
	{
		currentState->count = decimalN;
		currentState->sxy.value_type = BSON_TYPE_DECIMAL128;
		if ((isInfinityX > 0) || (isInfinityY > 0) || (isInfinitySxy > 0))
		{
			SetDecimal128PositiveInfinity(&currentState->sxy);
		}
		else
		{
			SetDecimal128NegativeInfinity(&currentState->sxy);
		}

		return;
	}


	/* currentState->sx += decimalCurrentXValue */
	HANDLE_DECIMAL_OP_ERROR(AddDecimal128Numbers, currentState->sx, decimalCurrentXValue,
							currentState->sx,
							"Failed while calculating CalculateSFuncForCovarianceOrVarianceWithYCAlgr sx for these values: ");

	/* currentState->sy += decimalCurrentYValue */
	HANDLE_DECIMAL_OP_ERROR(AddDecimal128Numbers, currentState->sy, decimalCurrentYValue,
							currentState->sy,
							"Failed while calculating CalculateSFuncForCovarianceOrVarianceWithYCAlgr sy for these values: ");

	if (BsonValueAsDouble(&currentState->count) > 0.0)
	{
		/* decimalXTmp = decimalCurrentXValue * decimalN - currentState->sx; */
		HANDLE_DECIMAL_OP_ERROR(MultiplyDecimal128Numbers, decimalCurrentXValue, decimalN,
								decimalXTmp,
								"Failed while calculating CalculateSFuncForCovarianceOrVarianceWithYCAlgr decimalXTmp_1 for these values: ");

		HANDLE_DECIMAL_OP_ERROR(SubtractDecimal128Numbers, decimalXTmp, currentState->sx,
								decimalXTmp,
								"Failed while calculating CalculateSFuncForCovarianceOrVarianceWithYCAlgr decimalXTmp_2 for these values: ");

		/* decimalYTmp = decimalCurrentYValue * decimalN - currentState->sy; */
		HANDLE_DECIMAL_OP_ERROR(MultiplyDecimal128Numbers, decimalCurrentYValue, decimalN,
								decimalYTmp,
								"Failed while calculating CalculateSFuncForCovarianceOrVarianceWithYCAlgr decimalYTmp_1 for these values: ");

		HANDLE_DECIMAL_OP_ERROR(SubtractDecimal128Numbers, decimalYTmp, currentState->sy,
								decimalYTmp,
								"Failed while calculating CalculateSFuncForCovarianceOrVarianceWithYCAlgr decimalYTmp_2 for these values: ");


		/* currentState->sxy += decimalXTmp * decimalYTmp / (decimalN * currentState->count); */
		HANDLE_DECIMAL_OP_ERROR(MultiplyDecimal128Numbers, decimalXTmp, decimalYTmp,
								decimalXYTmp,
								"Failed while calculating CalculateSFuncForCovarianceOrVarianceWithYCAlgr decimalXYTmp_1 for these values: ");

		HANDLE_DECIMAL_OP_ERROR(MultiplyDecimal128Numbers, decimalN, currentState->count,
								decimalNxx,
								"Failed while calculating CalculateSFuncForCovarianceOrVarianceWithYCAlgr decimalNxx for these values: ");

		decimalXTmp = decimalXYTmp; /* use decimalXTmp as a temporary variable to take origin value of decimalXYTmp */
		HANDLE_DECIMAL_OP_ERROR(DivideDecimal128Numbers, decimalXTmp, decimalNxx,
								decimalXYTmp,
								"Failed while calculating CalculateSFuncForCovarianceOrVarianceWithYCAlgr decimalXYTmp_2 for these values: ");

		/* currentState->sxy += decimalXYTmp */
		HANDLE_DECIMAL_OP_ERROR(AddDecimal128Numbers, currentState->sxy, decimalXYTmp,
								currentState->sxy,
								"Failed while calculating CalculateSFuncForCovarianceOrVarianceWithYCAlgr sxy for these values: ");
	}
	else
	{
		/*
		 * At the first input, we normally can leave currentState->sxy as 0.
		 * However, if the first input is Inf or NaN, we'd better force currentState->sxy
		 * to Inf or NaN.
		 */
		if (IsBsonValueNaN(newXValue) || IsBsonValueNaN(newYValue) || isInfinityX *
			isInfinityY == -1)
		{
			currentState->sxy.value_type = BSON_TYPE_DECIMAL128;
			SetDecimal128NaN(&currentState->sxy);
		}
		else if (isInfinityX + isInfinityY != 0)
		{
			currentState->sxy.value_type = BSON_TYPE_DECIMAL128;
			if (isInfinityX + isInfinityY > 0)
			{
				SetDecimal128PositiveInfinity(&currentState->sxy);
			}
			else
			{
				SetDecimal128NegativeInfinity(&currentState->sxy);
			}
		}
	}

	currentState->count = decimalN;
}
