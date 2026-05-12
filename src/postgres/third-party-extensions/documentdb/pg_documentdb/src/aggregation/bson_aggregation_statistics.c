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
#include <windowapi.h>
#include <executor/nodeWindowAgg.h>
#include <executor/executor.h>
#include <catalog/pg_type.h>
#include <math.h>
#include <nodes/pg_list.h>

#include "aggregation/bson_aggregate.h"
#include "utils/documentdb_errors.h"
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
	int64_t count;

	/* number of decimal values in current window, used to determine if we need to return decimal128 value */
	int decimalCount;
} BsonCovarianceAndVarianceAggState;

typedef struct BsonExpMovingAvg
{
	bool init;
	bool isAlpha;
	bson_value_t weight;
	bson_value_t preValue;
} BsonExpMovingAvg;

enum InputValidFlags
{
	InputValidFlags_Unknown = 0,
	InputValidFlags_N = 1,
	InputValidFlags_Alpha = 2,
	InputValidFlags_Input = 4
};

typedef struct BsonIntegralAndDerivativeAggState
{
	/* The result value of current window */
	bson_value_t result;

	/* anchorX/anchorY is an anchor point for calculating integral or derivative.
	 * For $integral, anchorX is updated to the previous document of the window,
	 * For $derivative, anchorX is always updated to the first document of the window.
	 */
	bson_value_t anchorX;
	bson_value_t anchorY;
} BsonIntegralAndDerivativeAggState;

/*
 * ArithmeticOperation is used to determine which operation to perform.
 * The operations are:
 *   ArithmeticOperation_Add, i.e. AddNumberToBsonValue,
 *   ArithmeticOperation_Subtract, i.e. SubtractNumberFromBsonValue,
 *   ArithmeticOperation_Multiply, i.e. MultiplyWithFactorAndUpdate, or
 *   ArithmeticOperation_Divide, i.e. DivideBsonValueNumbers.
 */
typedef enum ArithmeticOperation
{
	ArithmeticOperation_Add = 0,
	ArithmeticOperation_Subtract = 1,
	ArithmeticOperation_Multiply = 2,
	ArithmeticOperation_Divide = 3
} ArithmeticOperation;

typedef enum ArithmeticOperationErrorSource
{
	OperationSource_CovariancePopFinal = 0,
	OperationSource_CovarianceSampFinal = 1,
	OperationSource_StdDevPopFinal = 2,
	OperationSource_StdDevSampFinal = 3,
	OperationSource_StdDevPopWinfuncFinal = 4,
	OperationSource_StdDevSampWinfuncFinal = 5,
	OperationSource_InvYCAlgr = 6,
	OperationSource_CombineYCAlgr = 7,
	OperationSource_SFuncYCAlgr = 8,
	OperationSource_ExpMovingAvg = 9,
} ArithmeticOperationErrorSource;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */


bool ParseInputWeightForExpMovingAvg(const bson_value_t *opValue,
									 bson_value_t *inputExpression,
									 bson_value_t *weightExpression,
									 bson_value_t *decimalWeightValue);

static MaxAlignedVarlena * AllocateBsonCovarianceOrVarianceAggState(void);
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
static void CalculateExpMovingAvg(bson_value_t *currentValue, bson_value_t *perValue,
								  bson_value_t *weightValue, bool isAlpha,
								  bson_value_t *resultValue);
static MaxAlignedVarlena * AllocateBsonIntegralAndDerivativeAggState(void);

static void HandleIntegralDerivative(bson_value_t *xBsonValue, bson_value_t *yBsonValue,
									 long timeUnitInMs,
									 BsonIntegralAndDerivativeAggState *currentState,
									 const bool isIntegralOperator);
static void RunTimeCheckForIntegralAndDerivative(bson_value_t *xBsonValue,
												 bson_value_t *yBsonValue,
												 long timeUnitInMs,
												 const bool isIntegralOperator);
static bool IntegralOfTwoPointsByTrapezoidalRule(bson_value_t *xValue,
												 bson_value_t *yValue,
												 BsonIntegralAndDerivativeAggState *
												 currentState,
												 bson_value_t *timeUnitInMs);
static bool DerivativeOfTwoPoints(bson_value_t *xValue, bson_value_t *yValue,
								  BsonIntegralAndDerivativeAggState *currentState,
								  bson_value_t *timeUnitInMs);
static void CalculateSqrtForStdDev(const bson_value_t *inputValue,
								   bson_value_t *outputResult);

static void ArithmeticOperationFunc(ArithmeticOperation op, bson_value_t *state, const
									bson_value_t *number, ArithmeticOperationErrorSource
									errSource);
static void HandleArithmeticOperationError(const char *opName, bson_value_t *state,
										   const
										   bson_value_t *number,
										   ArithmeticOperationErrorSource errSource);

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
PG_FUNCTION_INFO_V1(bson_exp_moving_avg);
PG_FUNCTION_INFO_V1(bson_derivative_transition);
PG_FUNCTION_INFO_V1(bson_integral_transition);
PG_FUNCTION_INFO_V1(bson_integral_derivative_final);
PG_FUNCTION_INFO_V1(bson_std_dev_pop_samp_winfunc_invtransition);
PG_FUNCTION_INFO_V1(bson_std_dev_pop_winfunc_final);
PG_FUNCTION_INFO_V1(bson_std_dev_samp_winfunc_final);

/*
 * Transition function for the BSONCOVARIANCEPOP and BSONCOVARIANCESAMP aggregate.
 * Use the Youngs-Cramer algorithm to incorporate the new value into the
 * transition values.
 * If calculating variance, we use X and Y as the same value.
 */
Datum
bson_covariance_pop_samp_transition(PG_FUNCTION_ARGS)
{
	MaxAlignedVarlena *bytes;
	BsonCovarianceAndVarianceAggState *currentState;

	/* If the intermediate state has never been initialized, create it */
	if (PG_ARGISNULL(0))
	{
		MemoryContext aggregateContext;
		if (AggCheckCallContext(fcinfo, &aggregateContext) != AGG_CONTEXT_WINDOW)
		{
			ereport(ERROR, errmsg(
						"window aggregate function is invoked outside a valid window aggregate context"));
		}

		/* Create the aggregate state in the aggregate context. */
		MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);

		bytes = AllocateBsonCovarianceOrVarianceAggState();

		currentState = (BsonCovarianceAndVarianceAggState *) bytes->state;
		currentState->sx.value_type = BSON_TYPE_DOUBLE;
		currentState->sx.value.v_double = 0.0;
		currentState->sy.value_type = BSON_TYPE_DOUBLE;
		currentState->sy.value.v_double = 0.0;
		currentState->sxy.value_type = BSON_TYPE_DOUBLE;
		currentState->sxy.value.v_double = 0.0;
		currentState->count = 0;
		currentState->decimalCount = 0;

		MemoryContextSwitchTo(oldContext);
	}
	else
	{
		bytes = GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));
		currentState = (BsonCovarianceAndVarianceAggState *) bytes->state;
	}

	pgbson *currentXValue = PG_GETARG_MAYBE_NULL_PGBSON(1);
	pgbson *currentYValue = PG_GETARG_MAYBE_NULL_PGBSON(2);

	if (currentXValue == NULL || IsPgbsonEmptyDocument(currentXValue) ||
		currentYValue == NULL || IsPgbsonEmptyDocument(currentYValue))
	{
		PG_RETURN_POINTER(bytes);
	}

	pgbsonelement currentXValueElement;
	PgbsonToSinglePgbsonElement(currentXValue, &currentXValueElement);
	pgbsonelement currentYValueElement;
	PgbsonToSinglePgbsonElement(currentYValue, &currentYValueElement);

	/* we should ignore numeric values */
	if (BsonValueIsNumber(&currentXValueElement.bsonValue) &&
		BsonValueIsNumber(&currentYValueElement.bsonValue))
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

	MaxAlignedVarlena *combinedStateBytes = AllocateBsonCovarianceOrVarianceAggState();
	BsonCovarianceAndVarianceAggState *currentState =
		(BsonCovarianceAndVarianceAggState *) combinedStateBytes->state;

	MemoryContextSwitchTo(oldContext);

	/* Handle either left or right being null. A new state needs to be allocated regardless */
	currentState->sx.value_type = BSON_TYPE_DOUBLE;
	currentState->sx.value.v_double = 0.0;
	currentState->sy.value_type = BSON_TYPE_DOUBLE;
	currentState->sy.value.v_double = 0.0;
	currentState->sxy.value_type = BSON_TYPE_DOUBLE;
	currentState->sxy.value.v_double = 0.0;
	currentState->count = 0;
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
		MaxAlignedVarlena *rightBytes =
			GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(1));
		memcpy(combinedStateBytes->state, rightBytes->state,
			   sizeof(BsonCovarianceAndVarianceAggState));
	}
	else if (PG_ARGISNULL(1))
	{
		if (PG_ARGISNULL(0))
		{
			PG_RETURN_NULL();
		}
		MaxAlignedVarlena *leftBytes =
			GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));
		memcpy(combinedStateBytes->state, leftBytes->state,
			   sizeof(BsonCovarianceAndVarianceAggState));
	}
	else
	{
		MaxAlignedVarlena *leftBytes =
			GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));
		MaxAlignedVarlena *rightBytes =
			GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(1));
		BsonCovarianceAndVarianceAggState *leftState =
			(BsonCovarianceAndVarianceAggState *) leftBytes->state;
		BsonCovarianceAndVarianceAggState *rightState =
			(BsonCovarianceAndVarianceAggState *) rightBytes->state;
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

	MaxAlignedVarlena *bytes;
	BsonCovarianceAndVarianceAggState *currentState;

	if (PG_ARGISNULL(0))
	{
		PG_RETURN_NULL();
	}
	else
	{
		bytes = GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));
		currentState = (BsonCovarianceAndVarianceAggState *) bytes->state;
	}

	pgbson *currentXValue = PG_GETARG_MAYBE_NULL_PGBSON(1);
	pgbson *currentYValue = PG_GETARG_MAYBE_NULL_PGBSON(2);

	if (currentXValue == NULL || IsPgbsonEmptyDocument(currentXValue) ||
		currentYValue == NULL || IsPgbsonEmptyDocument(currentYValue))
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
	if (IsBsonValueNaN(&currentState->sxy) ||
		IsBsonValueInfinity(&currentState->sxy) ||
		IsBsonValueNaN(&currentXValueElement.bsonValue) ||
		IsBsonValueInfinity(&currentXValueElement.bsonValue) ||
		IsBsonValueNaN(&currentYValueElement.bsonValue) ||
		IsBsonValueInfinity(&currentYValueElement.bsonValue) ||
		currentState->count == 0)
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
	MaxAlignedVarlena *covarianceIntermediateState =
		PG_ARGISNULL(0) ? NULL : GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));

	pgbsonelement finalValue;
	finalValue.path = "";
	finalValue.pathLength = 0;
	if (covarianceIntermediateState != NULL)
	{
		bson_value_t bsonResult = { 0 };
		BsonCovarianceAndVarianceAggState *covarianceState =
			(BsonCovarianceAndVarianceAggState *) covarianceIntermediateState->state;

		if (IsBsonValueNaN(&covarianceState->sxy) ||
			IsBsonValueInfinity(&covarianceState->sxy) != 0)
		{
			bsonResult = covarianceState->sxy;
		}
		else if (covarianceState->count == 0)
		{
			/* Returns null for empty sets or wrong input field count */
			finalValue.bsonValue.value_type = BSON_TYPE_NULL;
			PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
		}
		else if (covarianceState->count == 1)
		{
			/* Returns 0 for single numeric value */
			/* Return double even if the value is decimal128 */
			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = 0;
			PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
		}
		else
		{
			bsonResult = covarianceState->sxy;

			/* bsonCount = covarianceState->count */
			bson_value_t bsonCount = {
				.value_type = BSON_TYPE_INT64,
				.value.v_int64 = covarianceState->count
			};

			ArithmeticOperationFunc(ArithmeticOperation_Divide,
									&bsonResult,
									&bsonCount,
									OperationSource_CovariancePopFinal);
		}

		finalValue.bsonValue = bsonResult;

		/* if there is at least one decimal value in current window, we should return decimal128; otherwise, return double */
		if (covarianceState->decimalCount > 0)
		{
			finalValue.bsonValue.value_type = BSON_TYPE_DECIMAL128;
			finalValue.bsonValue.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
				&bsonResult);
		}
		else
		{
			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = BsonValueAsDouble(&bsonResult);
		}
	}
	else
	{
		/* Returns null for empty sets */
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
	MaxAlignedVarlena *covarianceIntermediateState =
		PG_ARGISNULL(0) ? NULL : GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));

	pgbsonelement finalValue;
	finalValue.path = "";
	finalValue.pathLength = 0;
	if (covarianceIntermediateState != NULL)
	{
		bson_value_t bsonResult = { 0 };
		BsonCovarianceAndVarianceAggState *covarianceState =
			(BsonCovarianceAndVarianceAggState *) covarianceIntermediateState->state;

		if (IsBsonValueNaN(&covarianceState->sxy) ||
			IsBsonValueInfinity(&covarianceState->sxy))
		{
			bsonResult = covarianceState->sxy;
		}
		else if (covarianceState->count == 0 ||
				 covarianceState->count == 1)
		{
			/* Returns null for empty sets, single numeric value or wrong input field count */
			finalValue.bsonValue.value_type = BSON_TYPE_NULL;
			PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
		}
		else
		{
			bson_value_t countMinus1 = {
				.value_type = BSON_TYPE_INT64,
				.value.v_int64 = covarianceState->count - 1
			};
			bsonResult = covarianceState->sxy;

			ArithmeticOperationFunc(ArithmeticOperation_Divide,
									&bsonResult, &countMinus1,
									OperationSource_CovarianceSampFinal);
		}

		/* if there is at least one decimal value in current window, we should return decimal128; otherwise, return double */
		finalValue.bsonValue = bsonResult;

		/* if there is at least one decimal value in current window, we should return decimal128; otherwise, return double */
		if (covarianceState->decimalCount > 0)
		{
			finalValue.bsonValue.value_type = BSON_TYPE_DECIMAL128;
			finalValue.bsonValue.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
				&bsonResult);
		}
		else
		{
			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = BsonValueAsDouble(&bsonResult);
		}
	}
	else
	{
		/* Returns null for empty sets */
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
	MaxAlignedVarlena *bytes;
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

		currentState = (BsonCovarianceAndVarianceAggState *) bytes->state;
		currentState->sx.value_type = BSON_TYPE_DOUBLE;
		currentState->sx.value.v_double = 0.0;
		currentState->sy.value_type = BSON_TYPE_DOUBLE;
		currentState->sy.value.v_double = 0.0;
		currentState->sxy.value_type = BSON_TYPE_DOUBLE;
		currentState->sxy.value.v_double = 0.0;
		currentState->count = 0;
		currentState->decimalCount = 0;

		MemoryContextSwitchTo(oldContext);
	}
	else
	{
		bytes = GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));
		currentState = (BsonCovarianceAndVarianceAggState *) bytes->state;
	}
	pgbson *currentValue = PG_GETARG_MAYBE_NULL_PGBSON(1);

	if (currentValue == NULL || IsPgbsonEmptyDocument(currentValue))
	{
		PG_RETURN_POINTER(bytes);
	}

	pgbsonelement currentValueElement;
	PgbsonToSinglePgbsonElement(currentValue, &currentValueElement);

	/* Skip non-numeric values */
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
		ereport(ERROR, errmsg(
					"Aggregate function invoked in non-aggregate context"));
	}

	/* Create the aggregate state in the aggregate context. */
	MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);

	MaxAlignedVarlena *combinedStateBytes = AllocateBsonCovarianceOrVarianceAggState();
	BsonCovarianceAndVarianceAggState *currentState =
		(BsonCovarianceAndVarianceAggState *) combinedStateBytes->state;

	MemoryContextSwitchTo(oldContext);

	/* Handle either left or right being null. A new state needs to be allocated regardless */
	currentState->sx.value_type = BSON_TYPE_DOUBLE;
	currentState->sx.value.v_double = 0.0;
	currentState->sy.value_type = BSON_TYPE_DOUBLE;
	currentState->sy.value.v_double = 0.0;
	currentState->sxy.value_type = BSON_TYPE_DOUBLE;
	currentState->sxy.value.v_double = 0.0;
	currentState->count = 0;
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
		MaxAlignedVarlena *rightStateBytes =
			GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(1));
		memcpy(combinedStateBytes->state, rightStateBytes->state,
			   sizeof(BsonCovarianceAndVarianceAggState));
	}
	else if (PG_ARGISNULL(1))
	{
		if (PG_ARGISNULL(0))
		{
			PG_RETURN_NULL();
		}
		MaxAlignedVarlena *leftStateBytes =
			GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));
		memcpy(combinedStateBytes->state, leftStateBytes->state,
			   sizeof(BsonCovarianceAndVarianceAggState));
	}
	else
	{
		MaxAlignedVarlena *leftStateBytes =
			GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));
		MaxAlignedVarlena *rightStateBytes =
			GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(1));
		BsonCovarianceAndVarianceAggState *leftState =
			(BsonCovarianceAndVarianceAggState *) leftStateBytes->state;
		BsonCovarianceAndVarianceAggState *rightState =
			(BsonCovarianceAndVarianceAggState *) rightStateBytes->state;

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
	MaxAlignedVarlena *stdDevIntermediateState =
		PG_ARGISNULL(0) ? NULL : GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));

	pgbsonelement finalValue;
	finalValue.path = "";
	finalValue.pathLength = 0;
	if (stdDevIntermediateState != NULL)
	{
		BsonCovarianceAndVarianceAggState *stdDevState =
			(BsonCovarianceAndVarianceAggState *) stdDevIntermediateState->state;

		if (IsBsonValueNaN(&stdDevState->sxy) ||
			IsBsonValueInfinity(&stdDevState->sxy))
		{
			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = NAN;
		}
		else if (stdDevState->count == 0)
		{
			/* Returns $null for empty sets */
			finalValue.bsonValue.value_type = BSON_TYPE_NULL;
		}
		else if (stdDevState->count == 1)
		{
			/* Returns 0 for single numeric value */
			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = 0.0;
		}
		else
		{
			bson_value_t result = stdDevState->sxy;
			bson_value_t bsonCount = {
				.value_type = BSON_TYPE_INT64,
				.value.v_int64 = stdDevState->count
			};

			ArithmeticOperationFunc(ArithmeticOperation_Divide, &result,
									&bsonCount,
									OperationSource_StdDevPopFinal);

			/* The value type of finalValue.bsonValue is set to BSON_TYPE_DOUBLE inside the function*/
			CalculateSqrtForStdDev(&result, &finalValue.bsonValue);
		}
	}
	else
	{
		/* Returns $null for empty sets */
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
	MaxAlignedVarlena *stdDevIntermediateState =
		PG_ARGISNULL(0) ? NULL : GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));

	pgbsonelement finalValue;
	finalValue.path = "";
	finalValue.pathLength = 0;
	if (stdDevIntermediateState != NULL)
	{
		BsonCovarianceAndVarianceAggState *stdDevState =
			(BsonCovarianceAndVarianceAggState *) stdDevIntermediateState->state;

		if (stdDevState->count == 0 ||
			stdDevState->count == 1)
		{
			/* Returns $null for empty sets or single numeric value */
			finalValue.bsonValue.value_type = BSON_TYPE_NULL;
		}
		else if (IsBsonValueInfinity(&stdDevState->sxy))
		{
			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = NAN;
		}
		else
		{
			bson_value_t countMinus1 = {
				.value_type = BSON_TYPE_INT64,
				.value.v_int64 = stdDevState->count - 1
			};
			bson_value_t result = stdDevState->sxy;

			ArithmeticOperationFunc(ArithmeticOperation_Divide, &result,
									&countMinus1,
									OperationSource_StdDevSampFinal);

			/* The value type of finalValue.bsonValue is set to BSON_TYPE_DOUBLE inside the function*/
			CalculateSqrtForStdDev(&result, &finalValue.bsonValue);
		}
	}
	else
	{
		/* Returns $null for empty sets */
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;
	}

	PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
}


/*
 * Function that calculate expMovingAvg value one by one.
 */
Datum
bson_exp_moving_avg(PG_FUNCTION_ARGS)
{
	WindowObject winobj = PG_WINDOW_OBJECT();
	BsonExpMovingAvg *stateData;

	stateData = (BsonExpMovingAvg *)
				WinGetPartitionLocalMemory(winobj, sizeof(BsonExpMovingAvg));

	bool isnull = false;

	pgbson *currentValue = DatumGetPgBson(WinGetFuncArgCurrent(winobj, 0, &isnull));

	pgbsonelement currentValueElement;
	PgbsonToSinglePgbsonElement(currentValue, &currentValueElement);
	bson_value_t bsonCurrentValue = currentValueElement.bsonValue;

	pgbsonelement finalValue;
	finalValue.path = "";
	finalValue.pathLength = 0;

	/* if currentValue is not a numeric type, return null. */
	if (BsonValueIsNumber(&bsonCurrentValue))
	{
		/* first call, init stateData. */
		if (!stateData->init)
		{
			/* get weight value, if isAlpha == true, the weightValue is Alpha, if isAlpha == false, the weightValue is N. */
			pgbson *weightValue = DatumGetPgBson(WinGetFuncArgCurrent(winobj, 1,
																	  &isnull));
			bool isAlpha = DatumGetBool(WinGetFuncArgCurrent(winobj, 2, &isnull));

			pgbsonelement weightValueElement;
			PgbsonToSinglePgbsonElement(weightValue, &weightValueElement);
			bson_value_t bsonWeightValue = weightValueElement.bsonValue;

			stateData->init = true;
			stateData->isAlpha = isAlpha;
			stateData->preValue = bsonCurrentValue;
			stateData->weight = bsonWeightValue;
			finalValue.bsonValue = bsonCurrentValue;
			PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
		}
		else
		{
			bson_value_t bsonPerValue = stateData->preValue;
			bson_value_t bsonResultValue;

			/*
			 * CalculateExpMovingAvg will compute the result of expMovingAvg.
			 *
			 * If the parameter is N,
			 * the calculation is: current result = current value * ( 2 / ( N + 1 ) ) + previous result * ( 1 - ( 2 / ( N + 1 ) ) )
			 * To improve calculation accuracy, we need to convert the calculation to: (currentValue * 2 + preValue * ( N - 1 ) ) / ( N + 1 )
			 *
			 * If the parameter is alpha,
			 * the calculation is: current result = current value * alpha + previous result * ( 1 - alpha )
			 */
			CalculateExpMovingAvg(&bsonCurrentValue, &bsonPerValue,
								  &stateData->weight,
								  stateData->isAlpha, &bsonResultValue);


			/* If currentValue is of type decimal128, then expMovingResult will also be of type decimal128, */
			/* and all subsequent expMoving results will also be of type decimal128. */
			if (bsonCurrentValue.value_type == BSON_TYPE_DECIMAL128 ||
				stateData->preValue.value_type == BSON_TYPE_DECIMAL128)
			{
				stateData->preValue = bsonResultValue;
			}
			else
			{
				/* If result can be represented as an integer, we need to keep only the integer part. : 5.0 -> 5 */
				/* If result overflows int32, IsBsonValue32BitInteger will return false. */
				bool checkFixedInteger = true;
				if (IsBsonValue32BitInteger(&bsonResultValue, checkFixedInteger))
				{
					stateData->preValue.value_type = BSON_TYPE_INT32;
					stateData->preValue.value.v_int32 = BsonValueAsInt32(
						&bsonResultValue);
				}
				else
				{
					/* If result can be represented as an integer, we need to keep only the integer part. : 5.0 -> 5 */
					if (IsBsonValue64BitInteger(&bsonResultValue, checkFixedInteger))
					{
						stateData->preValue.value_type = BSON_TYPE_INT64;
						stateData->preValue.value.v_int64 = BsonValueAsInt64(
							&bsonResultValue);
					}
					else
					{
						stateData->preValue.value_type = BSON_TYPE_DOUBLE;
						stateData->preValue.value.v_double = BsonValueAsDouble(
							&bsonResultValue);
					}
				}
			}

			finalValue.bsonValue = stateData->preValue;
			PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
		}
	}
	else
	{
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;
		PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
	}
}


/* transition function for the BSON_INTEGRAL aggregate
 * use the trapzoidal rule to calculate the integral
 */
Datum
bson_integral_transition(PG_FUNCTION_ARGS)
{
	MaxAlignedVarlena *bytes;
	bool isIntegral = true;
	BsonIntegralAndDerivativeAggState *currentState;

	pgbson *xValue = PG_GETARG_MAYBE_NULL_PGBSON(1);
	pgbson *yValue = PG_GETARG_MAYBE_NULL_PGBSON(2);
	long timeUnitInt64 = PG_GETARG_INT64(3);
	pgbsonelement xValueElement, yValueElement;
	PgbsonToSinglePgbsonElement(xValue, &xValueElement);
	PgbsonToSinglePgbsonElement(yValue, &yValueElement);
	if (IsPgbsonEmptyDocument(xValue) || IsPgbsonEmptyDocument(yValue))
	{
		PG_RETURN_NULL();
	}
	RunTimeCheckForIntegralAndDerivative(&xValueElement.bsonValue,
										 &yValueElement.bsonValue, timeUnitInt64,
										 isIntegral);
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

		bytes = AllocateBsonIntegralAndDerivativeAggState();
		currentState = (BsonIntegralAndDerivativeAggState *) bytes->state;
		currentState->result.value_type = BSON_TYPE_DOUBLE;
		currentState->result.value.v_double = 0.0;

		/* update the anchor point with current document in window */
		currentState->anchorX = xValueElement.bsonValue;
		currentState->anchorY = yValueElement.bsonValue;

		/* if xValue is a date, convert it to double */
		if (xValueElement.bsonValue.value_type == BSON_TYPE_DATE_TIME)
		{
			currentState->anchorX.value_type = BSON_TYPE_DOUBLE;
			currentState->anchorX.value.v_double = BsonValueAsDouble(
				&xValueElement.bsonValue);
		}
		MemoryContextSwitchTo(oldContext);
		PG_RETURN_POINTER(bytes);
	}
	else
	{
		bytes = GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));
		currentState = (BsonIntegralAndDerivativeAggState *) bytes->state;
	}
	HandleIntegralDerivative(&xValueElement.bsonValue, &yValueElement.bsonValue,
							 timeUnitInt64,
							 currentState, isIntegral);

	/* update the anchor point with current document in window */
	currentState->anchorX = xValueElement.bsonValue;
	currentState->anchorY = yValueElement.bsonValue;

	/* if xValue is a date, convert it to double */
	if (xValueElement.bsonValue.value_type == BSON_TYPE_DATE_TIME)
	{
		currentState->anchorX.value_type = BSON_TYPE_DOUBLE;
		currentState->anchorX.value.v_double = BsonValueAsDouble(
			&xValueElement.bsonValue);
	}
	PG_RETURN_POINTER(bytes);
}


/* transition function for the BSON_DERIVATIVE aggregate
 * use dy/dx to calculate the derivative
 */
Datum
bson_derivative_transition(PG_FUNCTION_ARGS)
{
	pgbson *xValue = PG_GETARG_MAYBE_NULL_PGBSON(1);
	pgbson *yValue = PG_GETARG_MAYBE_NULL_PGBSON(2);
	long timeUnitInt64 = PG_GETARG_INT64(3);
	pgbsonelement xValueElement, yValueElement;
	PgbsonToSinglePgbsonElement(xValue, &xValueElement);
	PgbsonToSinglePgbsonElement(yValue, &yValueElement);

	MaxAlignedVarlena *bytes;
	bool isIntegral = false;
	BsonIntegralAndDerivativeAggState *currentState;
	if (IsPgbsonEmptyDocument(xValue) || IsPgbsonEmptyDocument(yValue))
	{
		PG_RETURN_NULL();
	}
	RunTimeCheckForIntegralAndDerivative(&xValueElement.bsonValue,
										 &yValueElement.bsonValue, timeUnitInt64,
										 isIntegral);
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

		bytes = AllocateBsonIntegralAndDerivativeAggState();
		currentState = (BsonIntegralAndDerivativeAggState *) bytes->state;
		currentState->result.value_type = BSON_TYPE_NULL;

		/* anchor points are always the first document in the window for $derivative*/
		/* if xValue is a date, convert it to double */
		if (xValueElement.bsonValue.value_type == BSON_TYPE_DATE_TIME)
		{
			currentState->anchorX.value_type = BSON_TYPE_DOUBLE;
			currentState->anchorX.value.v_double = BsonValueAsDouble(
				&xValueElement.bsonValue);
		}
		else
		{
			currentState->anchorX = xValueElement.bsonValue;
		}
		if (yValueElement.bsonValue.value_type == BSON_TYPE_DATE_TIME)
		{
			currentState->anchorY.value_type = BSON_TYPE_DOUBLE;
			currentState->anchorY.value.v_double = BsonValueAsDouble(
				&yValueElement.bsonValue);
		}
		else
		{
			currentState->anchorY = yValueElement.bsonValue;
		}

		/* We have the first document in state and the second incoming document
		 * it's ready to calculate the derivative */
		MemoryContextSwitchTo(oldContext);
		PG_RETURN_POINTER(bytes);
	}
	else
	{
		bytes = GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));
		currentState = (BsonIntegralAndDerivativeAggState *) bytes->state;
	}
	if (IsPgbsonEmptyDocument(xValue) || IsPgbsonEmptyDocument(yValue))
	{
		PG_RETURN_POINTER(bytes);
	}
	HandleIntegralDerivative(&xValueElement.bsonValue, &yValueElement.bsonValue,
							 timeUnitInt64,
							 currentState, isIntegral);
	PG_RETURN_POINTER(bytes);
}


/* final function for the BSON_INTEGRAL and BSON_DERIVATIVE aggregate
 * This takes the final value created and outputs a bson "integral" or "derivative"
 * with the appropriate type.
 */
Datum
bson_integral_derivative_final(PG_FUNCTION_ARGS)
{
	MaxAlignedVarlena *currentState =
		PG_ARGISNULL(0) ? NULL : GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));
	pgbsonelement finalValue;
	finalValue.path = "";
	finalValue.pathLength = 0;

	if (currentState != NULL)
	{
		BsonIntegralAndDerivativeAggState *state =
			(BsonIntegralAndDerivativeAggState *) currentState->state;
		if (state->result.value_type != BSON_TYPE_NULL)
		{
			finalValue.bsonValue = state->result;
		}
		else if (state->result.value_type == BSON_TYPE_NULL)
		{
			finalValue.bsonValue.value_type = BSON_TYPE_NULL;
		}
	}
	else
	{
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;
	}
	PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
}


/*
 * Applies the "final calculation" (FINALFUNC) for BSONSTDDEVPOP window aggregate operator.
 * This takes the final value created and outputs a bson stddev pop
 * with the appropriate type.
 */
Datum
bson_std_dev_pop_winfunc_final(PG_FUNCTION_ARGS)
{
	MaxAlignedVarlena *stdDevIntermediateState =
		PG_ARGISNULL(0) ? NULL : GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));

	pgbsonelement finalValue;
	finalValue.path = "";
	finalValue.pathLength = 0;
	if (stdDevIntermediateState != NULL)
	{
		BsonCovarianceAndVarianceAggState *stdDevState =
			(BsonCovarianceAndVarianceAggState *) stdDevIntermediateState->state;

		if (IsBsonValueNaN(&stdDevState->sxy) ||
			IsBsonValueInfinity(&stdDevState->sxy) != 0)
		{
			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = NAN;
			PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
		}
		else if (stdDevState->count == 0)
		{
			/* we return null for empty sets */
			finalValue.bsonValue.value_type = BSON_TYPE_NULL;
			PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
		}
		else if (stdDevState->count == 1)
		{
			/* we returns 0 for single numeric value */
			/* return double even if the value is decimal128 */
			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = 0.0;
			PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
		}
		else
		{
			bson_value_t result = stdDevState->sxy;
			bson_value_t bsonCount = {
				.value_type = BSON_TYPE_INT64,
				.value.v_int64 = stdDevState->count
			};

			ArithmeticOperationFunc(ArithmeticOperation_Divide, &result,
									&bsonCount,
									OperationSource_StdDevPopWinfuncFinal);

			/* The value type of finalValue.bsonValue is set to BSON_TYPE_DOUBLE inside the function*/
			CalculateSqrtForStdDev(&result, &finalValue.bsonValue);
		}
	}
	else
	{
		/* we return null for empty sets */
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;
	}

	PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
}


/*
 * Applies the "final calculation" (FINALFUNC) for BSONSTDDEVSAMP window function.
 * This takes the final value created and outputs a bson stddev samp
 * with the appropriate type.
 */
Datum
bson_std_dev_samp_winfunc_final(PG_FUNCTION_ARGS)
{
	MaxAlignedVarlena *stdDevIntermediateState =
		PG_ARGISNULL(0) ? NULL : GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));

	pgbsonelement finalValue;
	finalValue.path = "";
	finalValue.pathLength = 0;
	if (stdDevIntermediateState != NULL)
	{
		BsonCovarianceAndVarianceAggState *stdDevState =
			(BsonCovarianceAndVarianceAggState *) stdDevIntermediateState->state;

		if (IsBsonValueNaN(&stdDevState->sxy) ||
			IsBsonValueInfinity(&stdDevState->sxy))
		{
			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = NAN;
			PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
		}
		else if (stdDevState->count == 0 ||
				 stdDevState->count == 1)
		{
			/* we returns null for empty sets or single numeric value */
			finalValue.bsonValue.value_type = BSON_TYPE_NULL;
			PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
		}
		else
		{
			bson_value_t countMinus1 = {
				.value_type = BSON_TYPE_INT64,
				.value.v_int64 = stdDevState->count - 1
			};
			bson_value_t result = stdDevState->sxy;

			ArithmeticOperationFunc(ArithmeticOperation_Divide, &result,
									&countMinus1,
									OperationSource_StdDevSampWinfuncFinal);

			/* The value type of finalValue.bsonValue is set to BSON_TYPE_DOUBLE inside the function*/
			CalculateSqrtForStdDev(&result, &finalValue.bsonValue);
		}
	}
	else
	{
		/* we return null for empty sets */
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;
	}

	PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
}


/*
 * Applies the "inverse transition function" (MINVFUNC) for BSONSTDDEVPOP and BSONSTDDEVSAMP.
 * takes one aggregate state structures (BsonCovarianceAndVarianceAggState)
 * and single data point. Remove the single data from BsonCovarianceAndVarianceAggState
 */
Datum
bson_std_dev_pop_samp_winfunc_invtransition(PG_FUNCTION_ARGS)
{
	MemoryContext aggregateContext;
	if (AggCheckCallContext(fcinfo, &aggregateContext) != AGG_CONTEXT_WINDOW)
	{
		ereport(ERROR, errmsg(
					"window aggregate function called in non-window-aggregate context"));
	}

	MaxAlignedVarlena *bytes;
	BsonCovarianceAndVarianceAggState *currentState;

	if (PG_ARGISNULL(0))
	{
		PG_RETURN_NULL();
	}
	else
	{
		bytes = GetMaxAlignedVarlena(PG_GETARG_BYTEA_P(0));
		currentState = (BsonCovarianceAndVarianceAggState *) bytes->state;
	}

	pgbson *currentValue = PG_GETARG_MAYBE_NULL_PGBSON(1);

	if (currentValue == NULL || IsPgbsonEmptyDocument(currentValue))
	{
		PG_RETURN_POINTER(bytes);
	}

	pgbsonelement currentValueElement;
	PgbsonToSinglePgbsonElement(currentValue, &currentValueElement);

	if (!BsonTypeIsNumber(currentValueElement.bsonValue.value_type))
	{
		PG_RETURN_POINTER(bytes);
	}

	/* restart aggregate if NaN or Infinity in current state or current values */
	/* or count is 0 */
	if (IsBsonValueNaN(&currentState->sxy) ||
		IsBsonValueInfinity(&currentState->sxy) ||
		IsBsonValueNaN(&currentValueElement.bsonValue) ||
		IsBsonValueInfinity(&currentValueElement.bsonValue) ||
		currentState->count == 0)
	{
		PG_RETURN_NULL();
	}

	CalculateInvFuncForCovarianceOrVarianceWithYCAlgr(&currentValueElement.bsonValue,
													  &currentValueElement.bsonValue,
													  currentState);

	PG_RETURN_POINTER(bytes);
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */

static MaxAlignedVarlena *
AllocateBsonCovarianceOrVarianceAggState()
{
	MaxAlignedVarlena *combinedStateBytes =
		AllocateMaxAlignedVarlena(sizeof(BsonCovarianceAndVarianceAggState));

	return combinedStateBytes;
}


static MaxAlignedVarlena *
AllocateBsonIntegralAndDerivativeAggState()
{
	MaxAlignedVarlena *combinedStateBytes =
		AllocateMaxAlignedVarlena(sizeof(BsonIntegralAndDerivativeAggState));
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

	bson_value_t bsonCurrentXValue = *newXValue;
	bson_value_t bsonCurrentYValue = *newYValue;

	/* bsonN = currentState->count */
	bson_value_t bsonN = {
		.value_type = BSON_TYPE_INT64,
		.value.v_int64 = currentState->count
	};

	/* currentState->count -= 1.0 */
	currentState->count--;
	bson_value_t bsonNMinusOne = {
		.value_type = BSON_TYPE_INT64,
		.value.v_int64 = currentState->count
	};

	/* currentState->sx -= bsonCurrentXValue */
	ArithmeticOperationFunc(ArithmeticOperation_Subtract,
							&currentState->sx,
							&bsonCurrentXValue,
							OperationSource_InvYCAlgr);

	/* currentState->sy -= bsonCurrentYValue */
	ArithmeticOperationFunc(ArithmeticOperation_Subtract,
							&currentState->sy,
							&bsonCurrentYValue,
							OperationSource_InvYCAlgr);

	bson_value_t bsonXTmp = bsonCurrentXValue;
	bson_value_t bsonYTmp = bsonCurrentYValue;

	/* bsonXTmp = currentState->sx - bsonCurrentXValue * currentState->count */
	/* X * N^ */
	ArithmeticOperationFunc(ArithmeticOperation_Multiply, &bsonXTmp,
							&bsonNMinusOne,
							OperationSource_InvYCAlgr);

	/* Sx^ - X * N^ */
	bson_value_t bsonTmp = currentState->sx;
	ArithmeticOperationFunc(ArithmeticOperation_Subtract, &bsonTmp,
							&bsonXTmp, OperationSource_InvYCAlgr);
	bsonXTmp = bsonTmp;

	/* bsonYTmp = currentState->sy - bsonCurrentYValue * bsonNMinusOne(currentState->count) */
	/* Y * N^ */
	ArithmeticOperationFunc(ArithmeticOperation_Multiply, &bsonYTmp,
							&bsonNMinusOne, OperationSource_InvYCAlgr);

	/* Sy^ - Y * N^ */
	bsonTmp = currentState->sy;
	ArithmeticOperationFunc(ArithmeticOperation_Subtract, &bsonTmp,
							&bsonYTmp,
							OperationSource_InvYCAlgr);
	bsonYTmp = bsonTmp;

	bson_value_t bsonXYTmp;

	/* bsonXYTmp = bsonXTmp * bsonYTmp */
	/* (Sx^ - X * N^) * (Sy^ - Y * N^) */
	bsonXYTmp = bsonXTmp;
	ArithmeticOperationFunc(ArithmeticOperation_Multiply, &bsonXYTmp,
							&bsonYTmp,
							OperationSource_InvYCAlgr);

	bson_value_t bsonTmpNxx = bsonN;

	/* bsonTmpNxx = bsonN * bsonNMinusOne(currentState->count) */
	ArithmeticOperationFunc(ArithmeticOperation_Multiply, &bsonTmpNxx,
							&bsonNMinusOne,
							OperationSource_InvYCAlgr);

	bsonTmp = bsonXYTmp;

	/* bsonTmp = bsonXYTmp / bsonTmpNxx */
	/* (Sx^ - X * N^) * (Sy^ - Y * N^) / (N * N^) = N^/N * (Sx^/N^ - X) * (Sy^/N^ - Y) */
	ArithmeticOperationFunc(ArithmeticOperation_Divide, &bsonTmp,
							&bsonTmpNxx,
							OperationSource_InvYCAlgr);

	/* currentState->sxy -= bsonTmp */
	ArithmeticOperationFunc(ArithmeticOperation_Subtract,
							&currentState->sxy, &bsonTmp,
							OperationSource_InvYCAlgr);
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
	if (leftState->count == 0)
	{
		memcpy(currentState, rightState, sizeof(BsonCovarianceAndVarianceAggState));
		return;
	}
	else if (rightState->count == 0)
	{
		memcpy(currentState, leftState, sizeof(BsonCovarianceAndVarianceAggState));
		return;
	}

	/* update the count of decimal values */
	currentState->decimalCount = leftState->decimalCount + rightState->decimalCount;

	/* currentState->count = leftState->count + rightState->count */
	currentState->count = leftState->count + rightState->count;

	/* handle infinities first */
	/* if infinity values from left and right node with different signs, return NaN */
	/* if with the same sign, return the infinity value */
	/* if no infinity values, continue to calculate the covariance/variance */
	int isInfinityLeft = IsBsonValueInfinity(&leftState->sxy);
	int isInfinityRight = IsBsonValueInfinity(&rightState->sxy);
	if (isInfinityLeft * isInfinityRight == -1)
	{
		if (currentState->sxy.value_type == BSON_TYPE_DECIMAL128)
		{
			SetDecimal128NaN(&currentState->sxy);
		}
		else
		{
			currentState->sxy.value.v_double = NAN;
		}
		return;
	}
	else if (isInfinityLeft + isInfinityRight != 0)
	{
		if (currentState->sxy.value_type == BSON_TYPE_DECIMAL128)
		{
			if (isInfinityLeft + isInfinityRight > 0)
			{
				SetDecimal128PositiveInfinity(&currentState->sxy);
			}
			else
			{
				SetDecimal128NegativeInfinity(&currentState->sxy);
			}
		}
		else
		{
			currentState->sxy.value_type = BSON_TYPE_DOUBLE;
			if (isInfinityLeft + isInfinityRight > 0)
			{
				currentState->sxy.value.v_double = (double) INFINITY;
			}
			else
			{
				currentState->sxy.value.v_double = (double) -INFINITY;
			}
		}
		return;
	}

	/* currentState->sx = leftState->sx + rightState->sx */
	ArithmeticOperationFunc(ArithmeticOperation_Add, &currentState->sx,
							&leftState->sx,
							OperationSource_CombineYCAlgr);
	ArithmeticOperationFunc(ArithmeticOperation_Add, &currentState->sx,
							&rightState->sx,
							OperationSource_CombineYCAlgr);

	/* currentState->sy = leftState->sy + rightState->sy */
	ArithmeticOperationFunc(ArithmeticOperation_Add, &currentState->sy,
							&leftState->sy,
							OperationSource_CombineYCAlgr);
	ArithmeticOperationFunc(ArithmeticOperation_Add, &currentState->sy,
							&rightState->sy,
							OperationSource_CombineYCAlgr);

	bson_value_t bsonTmpLeft = leftState->sx;
	bson_value_t bsonTmpRight = rightState->sx;

	bson_value_t bsonXTmp;

	/* bsonXTmp = leftState->sx / leftState->count - rightState->sx / rightState->count; */
	/* leftState->count and rightState->count won't be 0 as we have checked outside */

	bson_value_t leftN = {
		.value_type = BSON_TYPE_INT64,
		.value.v_int64 = leftState->count
	};
	bson_value_t rightN = {
		.value_type = BSON_TYPE_INT64,
		.value.v_int64 = rightState->count
	};

	/* Sx1/N1 */
	ArithmeticOperationFunc(ArithmeticOperation_Divide, &bsonTmpLeft,
							&leftN,
							OperationSource_CombineYCAlgr);

	/* Sx2/N2 */
	ArithmeticOperationFunc(ArithmeticOperation_Divide, &bsonTmpRight,
							&rightN,
							OperationSource_CombineYCAlgr);

	/* Sx1/N1 - Sx2/N2 */
	bsonXTmp = bsonTmpLeft;

	ArithmeticOperationFunc(ArithmeticOperation_Subtract, &bsonXTmp,
							&bsonTmpRight,
							OperationSource_CombineYCAlgr);

	bsonTmpLeft = leftState->sy;
	bsonTmpRight = rightState->sy;
	bson_value_t bsonYTmp;

	/* bsonYTmp = leftState->sy / leftState->count - rightState->sy / rightState->count; */
	/* Sy1/N1 */
	ArithmeticOperationFunc(ArithmeticOperation_Divide, &bsonTmpLeft,
							&leftN,
							OperationSource_CombineYCAlgr);

	/* Sy2/N2 */
	ArithmeticOperationFunc(ArithmeticOperation_Divide, &bsonTmpRight,
							&rightN,
							OperationSource_CombineYCAlgr);

	/* Sy1/N1 - Sy2/N2 */
	bsonYTmp = bsonTmpLeft;
	ArithmeticOperationFunc(ArithmeticOperation_Subtract, &bsonYTmp,
							&bsonTmpRight,
							OperationSource_CombineYCAlgr);
	bson_value_t bsonNxx = leftN;

	/* bsonNxx = leftState->count * rightState->count; */
	ArithmeticOperationFunc(ArithmeticOperation_Multiply, &bsonNxx,
							&rightN,
							OperationSource_CombineYCAlgr);

	bson_value_t bsonXYTmp = bsonXTmp;

	/* bsonXYTmp = bsonXTmp * bsonYTmp; */
	ArithmeticOperationFunc(ArithmeticOperation_Multiply, &bsonXYTmp,
							&bsonYTmp,
							OperationSource_CombineYCAlgr);

	bson_value_t bsonTmp = bsonNxx;

	/* bsonTmp = bsonNxx * bsonXYTmp; */
	/* N1 * N2 * (Sx1/N1 - Sx2/N2) * (Sy1/N1 - Sy2/N2) */
	ArithmeticOperationFunc(ArithmeticOperation_Multiply, &bsonTmp,
							&bsonXYTmp,
							OperationSource_CombineYCAlgr);

	bson_value_t bsonD = bsonTmp;

	/* bsonD = bsonTmp / N; */
	/* N1 * N2 * (Sx1/N1 - Sx2/N2) * (Sy1/N1 - Sy2/N2) / N; */
	/* didn't check if N is 0, as we should not meet that case */

	bson_value_t currentN = {
		.value_type = BSON_TYPE_INT64,
		.value.v_int64 = currentState->count
	};

	ArithmeticOperationFunc(ArithmeticOperation_Divide, &bsonD, &currentN,
							OperationSource_CombineYCAlgr);

	/* Sxy1 + Sxy2 */
	currentState->sxy = leftState->sxy;
	ArithmeticOperationFunc(ArithmeticOperation_Add, &currentState->sxy,
							&rightState->sxy,
							OperationSource_CombineYCAlgr);

	/* Sxy1 + Sxy2 + N1 * N2 * (Sx1/N1 - Sx2/N2) * (Sy1/N1 - Sy2/N2) / N */
	ArithmeticOperationFunc(ArithmeticOperation_Add, &currentState->sxy,
							&bsonD,
							OperationSource_CombineYCAlgr);
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
	bson_value_t intOne;
	intOne.value_type = BSON_TYPE_INT32;
	intOne.value.v_int32 = (int32_t) 1;

	bson_value_t bsonXTmp;
	bson_value_t bsonYTmp;
	bson_value_t bsonXYTmp;
	bson_value_t bsonNxx;

	bson_value_t bsonCurrentXValue = *newXValue;
	bson_value_t bsonCurrentYValue = *newYValue;

	/* bsonN = currentState->count */
	bson_value_t bsonN, bsonNTmp;
	bsonN.value_type = BSON_TYPE_INT64;
	bsonN.value.v_int64 = currentState->count;
	bsonNTmp.value_type = BSON_TYPE_INT64;
	bsonNTmp.value.v_int64 = currentState->count;

	/* update the count of decimal values accordingly */
	if (newXValue->value_type == BSON_TYPE_DECIMAL128 ||
		newYValue->value_type == BSON_TYPE_DECIMAL128)
	{
		currentState->decimalCount++;
	}

	ArithmeticOperationFunc(ArithmeticOperation_Add, &bsonN, &intOne,
							OperationSource_SFuncYCAlgr);

	/* NAN will be addressed in subsequent sections */
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
		currentState->count++;
		if (currentState->sxy.value_type == BSON_TYPE_DECIMAL128)
		{
			SetDecimal128NaN(&currentState->sxy);
		}
		else
		{
			currentState->sxy.value_type = BSON_TYPE_DOUBLE;
			currentState->sxy.value.v_double = NAN;
		}
		return;
	}
	else if (isInfinityX || isInfinityY || isInfinitySxy)

	/* infinities with the same sign, return infinity */
	{
		currentState->count++;
		if (currentState->sxy.value_type == BSON_TYPE_DECIMAL128)
		{
			if ((isInfinityX > 0) || (isInfinityY > 0) || (isInfinitySxy > 0))
			{
				SetDecimal128PositiveInfinity(&currentState->sxy);
			}
			else
			{
				SetDecimal128NegativeInfinity(&currentState->sxy);
			}
		}
		else
		{
			currentState->sxy.value_type = BSON_TYPE_DOUBLE;
			if ((isInfinityX > 0) || (isInfinityY > 0) || (isInfinitySxy > 0))
			{
				currentState->sxy.value.v_double = (double) INFINITY;
			}
			else
			{
				currentState->sxy.value.v_double = (double) -INFINITY;
			}
		}

		return;
	}


	/* currentState->sx += bsonCurrentXValue */
	ArithmeticOperationFunc(ArithmeticOperation_Add, &currentState->sx,
							&bsonCurrentXValue,
							OperationSource_SFuncYCAlgr);

	/* currentState->sy += bsonCurrentYValue */
	ArithmeticOperationFunc(ArithmeticOperation_Add, &currentState->sy,
							&bsonCurrentYValue,
							OperationSource_SFuncYCAlgr);

	if (currentState->count > 0)
	{
		/* bsonXTmp = bsonCurrentXValue * bsonN - currentState->sx; */
		bsonXTmp = bsonCurrentXValue;
		ArithmeticOperationFunc(ArithmeticOperation_Multiply,
								&bsonXTmp, &bsonN,
								OperationSource_SFuncYCAlgr);
		ArithmeticOperationFunc(ArithmeticOperation_Subtract,
								&bsonXTmp,
								&currentState->sx,
								OperationSource_SFuncYCAlgr);

		/* bsonYTmp = bsonCurrentYValue * bsonN - currentState->sy; */
		bsonYTmp = bsonCurrentYValue;
		ArithmeticOperationFunc(ArithmeticOperation_Multiply,
								&bsonYTmp, &bsonN,
								OperationSource_SFuncYCAlgr);
		ArithmeticOperationFunc(ArithmeticOperation_Subtract,
								&bsonYTmp,
								&currentState->sy,
								OperationSource_SFuncYCAlgr);

		/* currentState->sxy += bsonXTmp * bsonYTmp / (bsonN * currentState->count); */
		bsonXYTmp = bsonXTmp;
		ArithmeticOperationFunc(ArithmeticOperation_Multiply,
								&bsonXYTmp, &bsonYTmp,
								OperationSource_SFuncYCAlgr);

		bsonNxx = bsonN;
		ArithmeticOperationFunc(ArithmeticOperation_Multiply, &bsonNxx,
								&bsonNTmp,
								OperationSource_SFuncYCAlgr);
		ArithmeticOperationFunc(ArithmeticOperation_Divide, &bsonXYTmp,
								&bsonNxx,
								OperationSource_SFuncYCAlgr);

		/* currentState->sxy += bsonXYTmp */
		ArithmeticOperationFunc(ArithmeticOperation_Add,
								&currentState->sxy, &bsonXYTmp,
								OperationSource_SFuncYCAlgr);
	}
	else
	{
		/*
		 * At the first input, we normally can leave currentState->sxy as 0.
		 * However, if the first input is Inf or NaN, we'd better force currentState->sxy
		 * to Inf or NaN.
		 */
		if (IsBsonValueNaN(newXValue) ||
			IsBsonValueNaN(newYValue) ||
			isInfinityX * isInfinityY == -1)
		{
			if (currentState->sxy.value_type == BSON_TYPE_DECIMAL128)
			{
				currentState->sxy.value_type = BSON_TYPE_DECIMAL128;
				SetDecimal128NaN(&currentState->sxy);
			}
			else
			{
				currentState->sxy.value_type = BSON_TYPE_DOUBLE;
				currentState->sxy.value.v_double = NAN;
			}
		}
		else if (isInfinityX + isInfinityY != 0)
		{
			if (currentState->sxy.value_type == BSON_TYPE_DECIMAL128)
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
			else
			{
				currentState->sxy.value_type = BSON_TYPE_DOUBLE;
				if (isInfinityX + isInfinityY > 0)
				{
					currentState->sxy.value.v_double = (double) INFINITY;
				}
				else
				{
					currentState->sxy.value.v_double = (double) -INFINITY;
				}
			}
		}
	}

	currentState->count++;
}


bool
ParseInputWeightForExpMovingAvg(const bson_value_t *opValue,
								bson_value_t *inputExpression,
								bson_value_t *weightExpression,
								bson_value_t *decimalWeightValue)
{
	bson_iter_t docIter;
	BsonValueInitIterator(opValue, &docIter);


	/*
	 * The $expMovingAvg accumulator expects a document in the form of
	 * { "input": <value>, "alpha": <value> } or { "input": <value>, "N": <value> }
	 * input is required parameter, both N and alpha are optional, but must specify either N or alpha, cannot specify both.
	 * paramsValid is initially 0 and is used to check if params are valid:
	 * if N is available, paramsValid |= 1;
	 * if Alpha is available, paramsValid |= 2;
	 * if input is available, paramsValid |= 4;
	 *
	 * So the opValue is only valid when paramsValid is equal to 5 (101) or 6(101).
	 */
	int32 paramsValid = InputValidFlags_Unknown;

	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);

		if (strcmp(key, "input") == 0)
		{
			*inputExpression = *bson_iter_value(&docIter);
			paramsValid |= InputValidFlags_Input;
		}
		else if (strcmp(key, "alpha") == 0)
		{
			/*
			 * Alpha is a float number, must be between 0 and 1 (exclusive).
			 */
			*weightExpression = *bson_iter_value(&docIter);

			if (BsonValueAsDouble(weightExpression) <= 0 || BsonValueAsDouble(
					weightExpression) >= 1)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
								errmsg(
									"The value of 'alpha' must lie strictly between 0 and 1 (not inclusive), but the provided alpha is: %lf",
									BsonValueAsDouble(weightExpression))));
			}
			decimalWeightValue->value_type = BSON_TYPE_DECIMAL128;
			decimalWeightValue->value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
				weightExpression);
			paramsValid |= InputValidFlags_Alpha;
		}
		else if (strcmp(key, "N") == 0)
		{
			/*
			 * N must be an integer greater than one.
			 */
			*weightExpression = *bson_iter_value(&docIter);

			if (BsonTypeIsNumber(weightExpression->value_type))
			{
				bool checkFixedInteger = true;
				if (!IsBsonValue64BitInteger(weightExpression, checkFixedInteger) &&
					!IsBsonValueNegativeNumber(weightExpression))
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
									errmsg(
										"The 'N' field is required to be an integer value, but instead a floating-point number was provided as N: %lf; to specify a non-integer, please use the 'alpha' argument.",
										BsonValueAsDouble(weightExpression))));
				}
				else if (IsBsonValueNegativeNumber(weightExpression))
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
									errmsg(
										"'N' cannot be less than or equal to 0. Received %d",
										BsonValueAsInt32(weightExpression))));
				}
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
								errmsg(
									"Expected 'integer' type for 'N' field but found '%s' type.",
									BsonTypeName(weightExpression->value_type))));
			}


			decimalWeightValue->value_type = BSON_TYPE_DECIMAL128;
			decimalWeightValue->value.v_decimal128 = GetBsonValueAsDecimal128(
				weightExpression);
			paramsValid |= InputValidFlags_N;
		}
		else
		{
			/*incorrect parameter,like "alpah" */
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg(
								"Got unrecognized field in $expMovingAvg, The $expMovingAvg sub-object must contain exactly two specific fields: one labeled 'input', and the other either labeled 'N' or 'alpha'.")));
		}
	}

	if (paramsValid <= InputValidFlags_Input || paramsValid == (InputValidFlags_Input |
																InputValidFlags_N |
																InputValidFlags_Alpha))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"The $expMovingAvg sub-object must contain exactly two specific fields: one labeled 'input', and the other either labeled 'N' or 'alpha'.")));
	}

	return (paramsValid & InputValidFlags_Alpha) ? true : false;
}


/*
 * Function to calculate expMovingAvg.
 *
 * If the parameter is N,
 * the calculation is: current result = current value * ( 2 / ( N + 1 ) ) + previous result * ( 1 - ( 2 / ( N + 1 ) ) )
 * To improve calculation accuracy, we need to convert the calculation to: (currentValue * 2 + preValue * ( N - 1 ) ) / ( N + 1 )
 *
 * If the parameter is alpha,
 * the calculation is: current result = current value * alpha + previous result * ( 1 - alpha )
 */
static void
CalculateExpMovingAvg(bson_value_t *bsonCurrentValue, bson_value_t *bsonPreValue,
					  bson_value_t *bsonWeightValue, bool isAlpha,
					  bson_value_t *bsonResultValue)
{
	bson_value_t currentValue = *bsonCurrentValue;
	bson_value_t preValue = *bsonPreValue;
	bson_value_t weightValue = *bsonWeightValue;

	bson_value_t intOne = {
		.value_type = BSON_TYPE_INT32,
		.value.v_int32 = 1
	};

	if (isAlpha)
	{
		/*
		 *  current result = current value * alpha + previous result * ( 1 - alpha )
		 */

		/* 1 - alpha */
		ArithmeticOperationFunc(ArithmeticOperation_Subtract,
								&intOne,
								&weightValue,
								OperationSource_ExpMovingAvg);

		/* previous result * ( 1 - alpha ) */
		ArithmeticOperationFunc(ArithmeticOperation_Multiply,
								&preValue,
								&intOne,
								OperationSource_ExpMovingAvg);


		/* current value * alpha */
		ArithmeticOperationFunc(ArithmeticOperation_Multiply,
								&currentValue,
								&weightValue,
								OperationSource_ExpMovingAvg);

		/* preValue = previous result * ( 1 - alpha ) */
		/* currentValue = current value * alpha */
		/* currentValue = currentValue + preValue */
		ArithmeticOperationFunc(ArithmeticOperation_Add,
								&currentValue,
								&preValue,
								OperationSource_ExpMovingAvg);
	}
	else
	{
		/*
		 *  current result = current value * ( 2 / ( N + 1 ) ) + previous result * ( 1 - ( 2 / ( N + 1 ) ) )
		 *  To improve calculation accuracy, we need to convert the calculation:
		 *  (currentValue * 2 + preValue * ( N - 1 ) ) / ( N + 1 )
		 */
		bson_value_t intTwo = {
			.value_type = BSON_TYPE_INT32,
			.value.v_int32 = 2
		};

		/* currentValue = currentValue * 2 */
		ArithmeticOperationFunc(ArithmeticOperation_Multiply,
								&currentValue,
								&intTwo,
								OperationSource_ExpMovingAvg);


		/* weightValue = weightValue - 1 */
		ArithmeticOperationFunc(ArithmeticOperation_Subtract,
								&weightValue,
								&intOne,
								OperationSource_ExpMovingAvg);

		/* preValue = preValue * weightValue */
		/* wightValue was updated before (line: 2049) */
		ArithmeticOperationFunc(ArithmeticOperation_Multiply,
								&preValue,
								&weightValue,
								OperationSource_ExpMovingAvg);


		/* currentValue = currentValue + preValue */
		ArithmeticOperationFunc(ArithmeticOperation_Add,
								&currentValue,
								&preValue,
								OperationSource_ExpMovingAvg);

		/*bsonWeightValue = N; */
		/* N + 1 */
		/* add 2 here due to weightValue substract 1 before (line: 2052) */
		ArithmeticOperationFunc(ArithmeticOperation_Add,
								&weightValue,
								&intTwo,
								OperationSource_ExpMovingAvg);

		/* currentValue = currentValue / weightValue */
		ArithmeticOperationFunc(ArithmeticOperation_Divide,
								&currentValue,
								&weightValue,
								OperationSource_ExpMovingAvg);
	}

	*bsonResultValue = currentValue;
}


/* This function is used to handle the $integral and $derivative operators
 *  1. Calculate the integral or derivative value
 *  2. Update the result value in the currentState
 */
static void
HandleIntegralDerivative(bson_value_t *xBsonValue, bson_value_t *yBsonValue,
						 long timeUnitInt64,
						 BsonIntegralAndDerivativeAggState *currentState,
						 const bool isIntegralOperator)
{
	/* Intermidiate variables for calculation */
	bson_value_t timeUnitInMs, yValue, xValue;
	timeUnitInMs.value_type = BSON_TYPE_DOUBLE;
	timeUnitInMs.value.v_double = timeUnitInt64;
	yValue = *yBsonValue;
	xValue = *xBsonValue;

	/* Convert date time to double to avoid calucation error*/
	if (xBsonValue->value_type == BSON_TYPE_DATE_TIME)
	{
		xValue.value_type = BSON_TYPE_DOUBLE;
		xValue.value.v_double = BsonValueAsDouble(xBsonValue);
	}
	if (yBsonValue->value_type == BSON_TYPE_DATE_TIME)
	{
		yValue.value_type = BSON_TYPE_DOUBLE;
		yValue.value.v_double = BsonValueAsDouble(yBsonValue);
	}

	/* Calculation status */
	bool success = isIntegralOperator ?
				   IntegralOfTwoPointsByTrapezoidalRule(&xValue, &yValue,
														currentState,
														&timeUnitInMs) :
				   DerivativeOfTwoPoints(&xValue, &yValue, currentState,
										 &timeUnitInMs);

	/* If calculation failed, throw an error */
	if (!success)
	{
		char *opName = isIntegralOperator ? "$integral" : "$derivative";
		ereport(ERROR, errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
				errmsg(
					"Handling %s: yValue = %f, xValue = %f, currentState->anchorX = %f, currentState->anchorY = %f, currentState->result = %f",
					opName, BsonValueAsDouble(&yValue), BsonValueAsDouble(&xValue),
					BsonValueAsDouble(&currentState->anchorX), BsonValueAsDouble(
						&currentState->anchorY), BsonValueAsDouble(
						&currentState->result)),
				errdetail_log(
					"Handling %s: yValue = %f, xValue = %f, currentState->anchorX = %f, currentState->anchorY = %f, currentState->result = %f",
					opName, BsonValueAsDouble(&yValue), BsonValueAsDouble(&xValue),
					BsonValueAsDouble(&currentState->anchorX), BsonValueAsDouble(
						&currentState->anchorY), BsonValueAsDouble(
						&currentState->result)));
	}
}


/* This function is used to check the runtime syntax, input, and unit for $integral and $derivative operators */
static void
RunTimeCheckForIntegralAndDerivative(bson_value_t *xBsonValue, bson_value_t *yBsonValue,
									 long timeUnitInt64, bool isIntegralOperator)
{
	const char *opName = isIntegralOperator ? "$integral" : "$derivative";

	/* if xBsonValue is not a date and unit is specified, throw an error */
	if (IsBsonValueDateTimeFormat(xBsonValue->value_type) ||
		xBsonValue->value_type == BSON_TYPE_NULL)
	{
		if (!timeUnitInt64)
		{
			int errorCode = isIntegralOperator ? ERRCODE_DOCUMENTDB_LOCATION5423902 :
							ERRCODE_DOCUMENTDB_LOCATION5624901;
			const char *errorMsg = isIntegralOperator
								   ?
								   "%s (with no 'unit') expects the sortBy field to be numeric"
								   : "%s with sortBy set to Date needs a specified 'unit'";
			ereport(ERROR, errcode(errorCode),
					errmsg(errorMsg, opName),
					errdetail_log(errorMsg, opName));
		}
	}

	/* if xBsonValue is a number and unit is specified, throw an error */
	else if (BsonTypeIsNumber(xBsonValue->value_type))
	{
		if (timeUnitInt64)
		{
			int errorCode = isIntegralOperator ? ERRCODE_DOCUMENTDB_LOCATION5423901 :
							ERRCODE_DOCUMENTDB_LOCATION5624900;
			const char *errorMsg =
				"%s with 'unit' requires that the sortBy field needs to be a Date value";
			ereport(ERROR, errcode(errorCode),
					errmsg(errorMsg, opName),
					errdetail_log(errorMsg, opName));
		}
	}

	/* if unit is specifed but x is not a date, throw an error */
	if (timeUnitInt64 && xBsonValue->value_type != BSON_TYPE_DATE_TIME)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5429513), errmsg(
							"Expected 'Date' type for 'sortBy' field but found '%s' type",
							BsonTypeName(
								xBsonValue->value_type))));
	}

	/* if unit is not specified and x is a date, throw an error */
	else if (!timeUnitInt64 && xBsonValue->value_type == BSON_TYPE_DATE_TIME)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5429413), errmsg(
							"When specifying windows that cover a range of dates or times, it is necessary to include a corresponding unit.")));
	}

	/* y must be a number or valid date time*/
	if (!(BsonTypeIsNumber(yBsonValue->value_type) ||
		  yBsonValue->value_type == BSON_TYPE_DATE_TIME))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5423900),
						errmsg(
							"The input value of %s window function must be a vector"
							" of 2 value, the first value must be numeric or date type "
							"and the second must be numeric.", opName),
						errdetail_log("Input value is: %s",
									  BsonTypeName(yBsonValue->value_type))));
	}
}


/* This function is used to calculate the integral
 * of current state and current document in window by Trapezoidal Rule.
 * The result will be promoted to decimal if one of the input is decimal.
 */
bool
IntegralOfTwoPointsByTrapezoidalRule(bson_value_t *xValue,
									 bson_value_t *yValue,
									 BsonIntegralAndDerivativeAggState *currentState,
									 bson_value_t *timeUnitInMs)
{
	bool success = true;
	bson_value_t stateXValue = currentState->anchorX;
	const bson_value_t stateYValue = currentState->anchorY;

	bool overflowedFromInt64, convertInt64OverflowToDouble = false;

	/* get time delta */
	success &= SubtractNumberFromBsonValue(xValue, &stateXValue, &overflowedFromInt64);

	/* add anchorY axis */
	success &= AddNumberToBsonValue(yValue, &stateYValue, &overflowedFromInt64);

	/* get area */
	success &= MultiplyWithFactorAndUpdate(yValue, xValue, convertInt64OverflowToDouble);
	bson_value_t bsonValueTwo;
	bsonValueTwo.value_type = BSON_TYPE_DOUBLE;
	bsonValueTwo.value.v_double = 2.0;

	success &= DivideBsonValueNumbers(yValue, &bsonValueTwo);
	if (timeUnitInMs->value.v_double != 0.0)
	{
		success &= DivideBsonValueNumbers(yValue, timeUnitInMs);
	}

	success &= AddNumberToBsonValue(&currentState->result, yValue, &overflowedFromInt64);
	return success;
}


/* This function is used to calculate the derivative
 * of current state and current document in window by derivative rule.
 * The result will be promoted to decimal if one of the input is decimal.
 */
bool
DerivativeOfTwoPoints(bson_value_t *xValue, bson_value_t *yValue,
					  BsonIntegralAndDerivativeAggState *currentState,
					  bson_value_t *timeUnitInMs)
{
	bool success = true;
	bson_value_t stateXValue = currentState->anchorX;
	const bson_value_t stateYValue = currentState->anchorY;

	bool overflowedFromInt64 = false;

	/* get time delta */
	success &= SubtractNumberFromBsonValue(xValue, &stateXValue, &overflowedFromInt64);
	if (timeUnitInMs->value.v_double != 0.0)
	{
		success &= DivideBsonValueNumbers(xValue, timeUnitInMs);
	}

	/* get value delta */
	success &= SubtractNumberFromBsonValue(yValue, &stateYValue, &overflowedFromInt64);

	/* get derivative */
	if ((xValue->value_type == BSON_TYPE_DOUBLE && xValue->value.v_double == 0.0) ||
		(xValue->value_type == BSON_TYPE_DECIMAL128 && IsDecimal128Zero(xValue)))
	{
		currentState->result.value_type = BSON_TYPE_NULL;
		return success;
	}

	success &= DivideBsonValueNumbers(yValue, xValue);
	currentState->result = *yValue;
	return success;
}


/* This function is used to calculate the square root of input value.
 * The output result is double.
 */
static void
CalculateSqrtForStdDev(const bson_value_t *inputValue, bson_value_t *outputResult)
{
	outputResult->value_type = BSON_TYPE_DOUBLE;
	double resultForSqrt = 0;
	if (inputValue->value_type == BSON_TYPE_DECIMAL128)
	{
		if (IsDecimal128InDoubleRange(inputValue))
		{
			resultForSqrt = BsonValueAsDouble(inputValue);
			if (resultForSqrt < 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR)),
						errmsg("CalculateSqrtForStdDev: *inputValue = %f",
							   BsonValueAsDouble(inputValue)),
						errdetail_log(
							"CalculateSqrtForStdDev: *inputResult = %f",
							BsonValueAsDouble(inputValue)));
			}
			else
			{
				outputResult->value.v_double = sqrt(resultForSqrt);
			}
		}
		else
		{
			outputResult->value.v_double = NAN;
		}
	}
	else
	{
		resultForSqrt = BsonValueAsDouble(inputValue);
		outputResult->value.v_double = sqrt(resultForSqrt);
	}
}


/* This function is to execute the arithmetic functions.
 * such as AddNumberToBsonValue, SubtractNumberFromBsonValue, MultiplyWithFactorAndUpdate, DivideBsonValueNumbers
 * return void as the individual operation return error directly if failed.
 */
static void
ArithmeticOperationFunc(ArithmeticOperation op, bson_value_t *state, const
						bson_value_t *number, ArithmeticOperationErrorSource errSource)
{
	bool opResult = false;
	bool overflowedFromInt64 = false;
	char *opName = "AddNumberToBsonValue";

	switch (op)
	{
		case ArithmeticOperation_Add:
		{
			opResult = AddNumberToBsonValue(state, number, &overflowedFromInt64);
			break;
		}

		case ArithmeticOperation_Subtract:
		{
			opName = "SubtractNumberFromBsonValue";
			opResult = SubtractNumberFromBsonValue(state, number, &overflowedFromInt64);
			break;
		}

		case ArithmeticOperation_Multiply:
		{
			opName = "MultiplyWithFactorAndUpdate";
			bool convertInt64OverflowToDouble = false;
			opResult = MultiplyWithFactorAndUpdate(state, number,
												   convertInt64OverflowToDouble);
			break;
		}

		case ArithmeticOperation_Divide:
		{
			opName = "DivideBsonValueNumbers";
			opResult = DivideBsonValueNumbers(state, number);
			break;
		}

		default:
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR)),
					errmsg("Unknown arithmetic operation: %d", op));
	}

	if (!opResult)
	{
		HandleArithmeticOperationError(opName, state, number, errSource);
	}
}


/* This function is to handle error of arithmetic operations
 * from ArithmeticOperationFunc.
 */
static void
HandleArithmeticOperationError(const char *opName, bson_value_t *state, const
							   bson_value_t *number, ArithmeticOperationErrorSource
							   errSource)
{
	char *errMsg = "An internal error occurred during the calculation of %s.";
	char *errMsgSource = "variance/covariance";
	char *errMsgDetails =
		"Failed while calculating %s result: opName = %s, state = %s, number = %s.";
	char *calculateFunc = "";

	switch (errSource)
	{
		case OperationSource_CovariancePopFinal:
		{
			calculateFunc = "bson_covariance_pop_final";
			break;
		}

		case OperationSource_CovarianceSampFinal:
		{
			calculateFunc = "bson_covariance_samp_final";
			break;
		}

		case OperationSource_StdDevPopFinal:
		{
			calculateFunc = "bson_std_dev_pop_final";
			break;
		}

		case OperationSource_StdDevSampFinal:
		{
			calculateFunc = "bson_std_dev_samp_final";
			break;
		}

		case OperationSource_StdDevPopWinfuncFinal:
		{
			calculateFunc = "bson_std_dev_pop_winfunc_final";
			break;
		}

		case OperationSource_StdDevSampWinfuncFinal:
		{
			calculateFunc = "bson_std_dev_samp_winfunc_final";
			break;
		}

		case OperationSource_CombineYCAlgr:
		{
			calculateFunc =
				"CalculateCombineFuncForCovarianceOrVarianceWithYCAlgr";
			break;
		}

		case OperationSource_InvYCAlgr:
		{
			calculateFunc =
				"CalculateInvFuncForCovarianceOrVarianceWithYCAlgr";
			break;
		}

		case OperationSource_SFuncYCAlgr:
		{
			calculateFunc =
				"CalculateSFuncForCovarianceOrVarianceWithYCAlgr";
			break;
		}

		case OperationSource_ExpMovingAvg:
		{
			calculateFunc = "CalculateExpMovingAvg";
			errMsgSource = "expMovingAvg";
			break;
		}
	}

	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR)),
			errmsg(errMsg, errMsgSource),
			errdetail_log(
				errMsgDetails,
				calculateFunc,
				opName,
				BsonValueToJsonForLogging(state),
				BsonValueToJsonForLogging(number)));
}
