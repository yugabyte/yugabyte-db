/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/aggregation/bson_aggregation_window_functions.c
 *
 *  Implementation of Window function based accumulator for $setWindowFields.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>
#include <catalog/pg_type.h>

#include "io/helio_bson_core.h"
#include "types/decimal128.h"
#include "utils/mongo_errors.h"
#include "windowapi.h"

typedef struct BsonLocfFillState
{
	bool init;
	bool hasPre;
	pgbson *preValue;
} BsonLocfFillState;

typedef struct BsonLinearFillState
{
	bool init;

	bool hasPre;
	bson_value_t preValue;              /* previous non-null value for filled field */
	bson_value_t preSortKeyValue;       /* the value of sortKey of previous non-null value */

	bool hasNext;
	bson_value_t nextValue;             /* next non-null value for filled field */
	bson_value_t nextSortKeyValue;      /* the value of sortKey of next non-null value */
	int64 nextPos;                      /* the position of next non-null value */

	bool sortKeyIsNumeric;              /* if sort key is numeric */
	bool sortKeyIsDate;                 /* if sort key is date */
	bool isOut;                         /* if all rows in current partition has been scanned */
} BsonLinearFillState;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static void CheckDecimal128Result(Decimal128Result result);
static void CheckSortKeyBsonValue(bool isnull, pgbsonelement *sortKeyElement);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(bson_rank);
PG_FUNCTION_INFO_V1(bson_dense_rank);
PG_FUNCTION_INFO_V1(bson_linear_fill);
PG_FUNCTION_INFO_V1(bson_locf_fill);

Datum
bson_rank(PG_FUNCTION_ARGS)
{
	Datum rank = window_rank(fcinfo);
	bson_value_t finalValue = { .value_type = BSON_TYPE_INT32 };
	finalValue.value.v_int32 = DatumGetInt64(rank);
	PG_RETURN_POINTER(BsonValueToDocumentPgbson(&finalValue));
}


Datum
bson_dense_rank(PG_FUNCTION_ARGS)
{
	Datum rank = window_dense_rank(fcinfo);
	bson_value_t finalValue = { .value_type = BSON_TYPE_INT32 };
	finalValue.value.v_int32 = DatumGetInt64(rank);
	PG_RETURN_POINTER(BsonValueToDocumentPgbson(&finalValue));
}


Datum
bson_linear_fill(PG_FUNCTION_ARGS)
{
	WindowObject winobj = PG_WINDOW_OBJECT();
	BsonLinearFillState *stateData;

	stateData = (BsonLinearFillState *)
				WinGetPartitionLocalMemory(winobj, sizeof(BsonLinearFillState));

	if (!stateData->init)
	{
		/* first call */
		stateData->init = true;
		stateData->hasPre = false;
		stateData->hasNext = false;
		stateData->sortKeyIsDate = false;
		stateData->sortKeyIsNumeric = false;
		stateData->isOut = false;
	}

	Datum nextDatum;
	Datum nextSortKeyDatum;
	pgbsonelement nextValueElement;
	pgbsonelement nextSortKeyElement;
	bool isnull = true;
	bool isout = false;
	bool isValueNull = true;
	bool isSortKeyNull = true;

	int64 current_pos = WinGetCurrentPosition(winobj);

	/**
	 * We need to verify the sortBy value when using linearFill. The restrictions are:
	 * 1) Value of the sortBy field must be numeric or a date
	 * 2) No repeated values in the sortBy field in a single partition
	 */
	pgbson *sortByValue = DatumGetPgBson(WinGetFuncArgCurrent(winobj, 1, &isnull));
	pgbsonelement sortByValueElement;
	PgbsonToSinglePgbsonElement(sortByValue, &sortByValueElement);
	CheckSortKeyBsonValue(isnull, &sortByValueElement);

	if (sortByValueElement.bsonValue.value_type == BSON_TYPE_DATE_TIME)
	{
		stateData->sortKeyIsDate = true;
	}
	else
	{
		stateData->sortKeyIsNumeric = true;
	}
	if (stateData->sortKeyIsDate && stateData->sortKeyIsNumeric)
	{
		ereport(ERROR, (errcode(MongoTypeMismatch),
						errmsg(
							"The sortBy field must be either numeric or a date, but not both"),
						errhint(
							"The sortBy field must be either numeric or a date, but not both")));
	}

	/* Check if there are repeated values in the sortBy field in a single partition */
	if (current_pos != 0 && WinRowsArePeers(winobj, current_pos - 1, current_pos))
	{
		ereport(ERROR, (errcode(MongoLocation6050106),
						errmsg("There can be no repeated values in the sort field"),
						errhint("There can be no repeated values in the sort field")));
	}

	/* Move forward winobj's mark position to release unnecessary tuples in TupleStore */
	WinSetMarkPosition(winobj, current_pos - 1);

	pgbson *currentValue = DatumGetPgBson(WinGetFuncArgCurrent(winobj, 0, &isnull));
	pgbsonelement currentValueElement;
	PgbsonToSinglePgbsonElement(currentValue, &currentValueElement);
	if (!isnull && currentValueElement.bsonValue.value_type != BSON_TYPE_NULL)
	{
		/* As Mongo does, the filled value should be numeric or nullish. */
		if (!BsonValueIsNumber(&currentValueElement.bsonValue))
		{
			ereport(ERROR, (errcode(MongoTypeMismatch),
							errmsg(" Value to be filled must be numeric or nullish"),
							errhint("Value to be filled must be numeric or nullish")));
		}
		stateData->hasPre = true;

		/* In linearFill, only numeric type is allowed, so we can directly use `=` to copy the value to state. */
		stateData->preValue = currentValueElement.bsonValue;
		stateData->preSortKeyValue = sortByValueElement.bsonValue;
		PG_RETURN_POINTER(currentValue);
	}

	pgbsonelement finalValue;
	finalValue.path = "";
	finalValue.pathLength = 0;

	/* return null if no pre non-null value or no next non-null value in current partition*/
	if (!stateData->hasPre || stateData->isOut)
	{
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;
		PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
	}

	/* In this case, cur is null and hasPre is true. get next non-null value */
	if (!stateData->hasNext || (current_pos >= stateData->nextPos))
	{
		int64 start_offset = 0;
		isout = false;
		isnull = true;
		isValueNull = true;
		isSortKeyNull = true;

		while (!isout && (isnull || isValueNull))
		{
			start_offset++;
			nextDatum = WinGetFuncArgInPartition(winobj, 0, start_offset,
												 WINDOW_SEEK_CURRENT, false, &isnull,
												 &isout);
			if (isnull || isout)
			{
				continue;
			}
			PgbsonToSinglePgbsonElement(DatumGetPgBson(nextDatum), &nextValueElement);
			isValueNull = nextValueElement.bsonValue.value_type == BSON_TYPE_NULL;

			/* As we didn't change the offset, we can reuse the `isout` and its value should be same as previous call */
			nextSortKeyDatum = WinGetFuncArgInPartition(winobj, 1, start_offset,
														WINDOW_SEEK_CURRENT, false,
														&isSortKeyNull, &isout);
			PgbsonToSinglePgbsonElement(DatumGetPgBson(nextSortKeyDatum),
										&nextSortKeyElement);
			CheckSortKeyBsonValue(isSortKeyNull, &nextSortKeyElement);
		}
		if (!isnull && !isValueNull)
		{
			stateData->hasNext = true;
			stateData->nextValue = nextValueElement.bsonValue;
			stateData->nextSortKeyValue = nextSortKeyElement.bsonValue;
			stateData->nextPos = current_pos + start_offset;
		}
		else
		{
			/* if this branch hit, current partiton reaches the end and has no more non-null value */
			stateData->hasNext = false;
			stateData->isOut = true;
		}
	}
	if (!stateData->hasNext)
	{
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;
		PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
	}

	/**
	 * As Mongo does, the filled value should be numeric or nullish.
	 */
	if (!BsonValueIsNumber(&stateData->nextValue))
	{
		ereport(ERROR, (errcode(MongoTypeMismatch),
						errmsg(" Value to be filled must be numeric or nullish"),
						errhint("Value to be filled must be numeric or nullish")));
	}

	/**
	 * As Mongo does, if both values are not Deciaml128, double will be returned; otherwise, Decimal128 will be returned.
	 */
	if (stateData->preValue.value_type != BSON_TYPE_DECIMAL128 &&
		stateData->nextValue.value_type != BSON_TYPE_DECIMAL128)
	{
		double preValue = BsonValueAsDouble(&stateData->preValue);
		double nextValue = BsonValueAsDouble(&stateData->nextValue);
		double currentSortKeyValue = BsonValueAsDouble(&sortByValueElement.bsonValue);
		double preSortKeyValue = BsonValueAsDouble(&stateData->preSortKeyValue);
		double nextSortKeyValue = BsonValueAsDouble(&stateData->nextSortKeyValue);

		double filledValue = preValue + (nextValue - preValue) * (currentSortKeyValue -
																  preSortKeyValue) /
							 (nextSortKeyValue - preSortKeyValue);

		finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
		finalValue.bsonValue.value.v_double = filledValue;
		PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
	}
	else
	{
		bson_value_t preValue;
		preValue.value_type = BSON_TYPE_DECIMAL128;
		preValue.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
			&stateData->preValue);

		bson_value_t nextValue;
		nextValue.value_type = BSON_TYPE_DECIMAL128;
		nextValue.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
			&stateData->nextValue);

		bson_value_t filledValue;

		bson_value_t preSortKeyValue;
		preSortKeyValue.value_type = BSON_TYPE_DECIMAL128;
		preSortKeyValue.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
			&stateData->preSortKeyValue);

		bson_value_t nextSortKeyValue;
		nextSortKeyValue.value_type = BSON_TYPE_DECIMAL128;
		nextSortKeyValue.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
			&stateData->nextSortKeyValue);

		bson_value_t currentSortKeyValue;
		currentSortKeyValue.value_type = BSON_TYPE_DECIMAL128;
		currentSortKeyValue.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
			&sortByValueElement.bsonValue);

		CheckDecimal128Result(SubtractDecimal128Numbers(&nextSortKeyValue,
														&preSortKeyValue,
														&nextSortKeyValue));
		CheckDecimal128Result(SubtractDecimal128Numbers(&currentSortKeyValue,
														&preSortKeyValue,
														&currentSortKeyValue));

		CheckDecimal128Result(SubtractDecimal128Numbers(&nextValue, &preValue,
														&filledValue));
		CheckDecimal128Result(MultiplyDecimal128Numbers(&filledValue,
														&currentSortKeyValue,
														&filledValue));
		CheckDecimal128Result(DivideDecimal128Numbers(&filledValue, &nextSortKeyValue,
													  &filledValue));
		CheckDecimal128Result(AddDecimal128Numbers(&preValue, &filledValue,
												   &filledValue));

		finalValue.bsonValue.value_type = BSON_TYPE_DECIMAL128;
		finalValue.bsonValue.value.v_decimal128 = filledValue.value.v_decimal128;
		PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
	}
}


Datum
bson_locf_fill(PG_FUNCTION_ARGS)
{
	WindowObject winobj = PG_WINDOW_OBJECT();
	BsonLocfFillState *stateData;

	stateData = (BsonLocfFillState *)
				WinGetPartitionLocalMemory(winobj, sizeof(BsonLocfFillState));

	if (!stateData->init)
	{
		/* first call */
		stateData->init = true;
		stateData->hasPre = false;
		stateData->preValue = NULL;
	}

	bool isnull = true;

	/* bool isout = true; */

	int64 current_pos = WinGetCurrentPosition(winobj);
	pgbson *currentValue = DatumGetPgBson(WinGetFuncArgCurrent(winobj, 0, &isnull));
	pgbsonelement currentValueElement;
	PgbsonToSinglePgbsonElement(currentValue, &currentValueElement);

	/* switch to partition level memory context */
	if (!isnull && currentValueElement.bsonValue.value_type != BSON_TYPE_NULL)
	{
		/* release memory of pre value because we will replaced it with current value by new memory allocation */
		if (stateData->preValue != NULL)
		{
			pfree(stateData->preValue);
		}
		stateData->hasPre = true;
		stateData->preValue = CopyPgbsonIntoMemoryContext(currentValue,
														  fcinfo->flinfo->fn_mcxt);

		/**
		 * Once we get current value, the values before current value won't be used in locf fill.
		 * Thus we can move the winobj's mark position forward to `current_pos - 1` to reduce the memory usage of TupleStore in NodeWindowAgg.
		 */
		WinSetMarkPosition(winobj, current_pos - 1);
		PG_RETURN_POINTER(currentValue);
	}

	/* return null if cur is null and no pre non-null value*/
	if (!stateData->hasPre)
	{
		pgbsonelement finalValue;
		finalValue.path = "";
		finalValue.pathLength = 0;
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;
		PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
	}
	else
	{
		PG_RETURN_POINTER(stateData->preValue);
	}
}


static void
CheckSortKeyBsonValue(bool isnull, pgbsonelement *sortKeyElement)
{
	if (isnull || sortKeyElement->bsonValue.value_type == BSON_TYPE_NULL)
	{
		ereport(ERROR, (errcode(MongoTypeMismatch),
						errmsg("sortBy value must be numeric or a date, but found null"),
						errhint(
							"sortBy value must be numeric or a date, but found null")));
	}
	if (!BsonValueIsNumber(&sortKeyElement->bsonValue) &&
		sortKeyElement->bsonValue.value_type != BSON_TYPE_DATE_TIME)
	{
		ereport(ERROR, (errcode(MongoTypeMismatch),
						errmsg("sortBy value must be numeric or a date, but found %s",
							   BsonTypeName(sortKeyElement->bsonValue.value_type)),
						errhint("sortBy value must be numeric or a date, but found %s",
								BsonTypeName(sortKeyElement->bsonValue.value_type))));
	}
}


/**
 * Check the result of Decimal128 operation. If the result is Decimal128Result_Unknown, throw an error.
 * $linearFill will return Infinity, -Infinity or NaN if the result is overflow, underflow or NaN.
 */
static void
CheckDecimal128Result(Decimal128Result result)
{
	if (result == Decimal128Result_Unknown)
	{
		ereport(ERROR, (errcode(MongoInternalError),
						errmsg(
							" Unknown error when processing Decimal128 numbers in $linearFill"),
						errhint(
							" Unknown error when processing Decimal128 numbers in $linearFill")));
	}
}
