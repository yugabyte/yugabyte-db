/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_compare.c
 *
 * Implementation of the BSON type comparisons.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>
#include <utils/array.h>
#include <utils/builtins.h>
#include <math.h>

#include "io/bson_core.h"
#include "query/bson_compare.h"
#include "types/decimal128.h"
#include "utils/mongo_errors.h"
#include "utils/hashset_utils.h"

/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

static const int64 MillisecondsInSecond = 1000;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static int CompareBsonIter(bson_iter_t *left, bson_iter_t *right, bool compareFields);
static long double BsonNumberAsLongDouble(const bson_value_t *left);
static int CompareNumbers(const bson_value_t *left, const bson_value_t *right,
						  bool *isComparisonValid);
static int GetSortOrderType(bson_type_t type);
static int CompareBsonValue(const bson_value_t *left, const bson_value_t *right,
							bool *isComparisonValid);
static double BsonValueAsDoubleCore(const bson_value_t *value, bool quiet);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(extension_bson_compare);
PG_FUNCTION_INFO_V1(extension_bson_equal);
PG_FUNCTION_INFO_V1(extension_bson_not_equal);
PG_FUNCTION_INFO_V1(extension_bson_gt);
PG_FUNCTION_INFO_V1(extension_bson_gte);
PG_FUNCTION_INFO_V1(extension_bson_lt);
PG_FUNCTION_INFO_V1(extension_bson_lte);
PG_FUNCTION_INFO_V1(bson_unique_index_equal);

Datum
extension_bson_compare(PG_FUNCTION_ARGS)
{
	pgbson *leftBson = PG_GETARG_PGBSON(0);
	pgbson *rightBson = PG_GETARG_PGBSON(1);

	int compareResult = ComparePgbson(leftBson, rightBson);
	PG_RETURN_INT32(compareResult);
}


Datum
extension_bson_equal(PG_FUNCTION_ARGS)
{
	pgbson *leftBson = PG_GETARG_PGBSON(0);
	pgbson *rightBson = PG_GETARG_PGBSON(1);

	int compareResult = ComparePgbson(leftBson, rightBson);

	PG_RETURN_BOOL(compareResult == 0);
}


Datum
extension_bson_not_equal(PG_FUNCTION_ARGS)
{
	pgbson *leftBson = PG_GETARG_PGBSON(0);
	pgbson *rightBson = PG_GETARG_PGBSON(1);

	int compareResult = ComparePgbson(leftBson, rightBson);

	PG_RETURN_BOOL(compareResult != 0);
}


Datum
extension_bson_lt(PG_FUNCTION_ARGS)
{
	pgbson *leftBson = PG_GETARG_PGBSON(0);
	pgbson *rightBson = PG_GETARG_PGBSON(1);

	int compareResult = ComparePgbson(leftBson, rightBson);

	PG_RETURN_BOOL(compareResult < 0);
}


Datum
extension_bson_lte(PG_FUNCTION_ARGS)
{
	pgbson *leftBson = PG_GETARG_PGBSON(0);
	pgbson *rightBson = PG_GETARG_PGBSON(1);

	int compareResult = ComparePgbson(leftBson, rightBson);

	PG_RETURN_BOOL(compareResult <= 0);
}


Datum
extension_bson_gt(PG_FUNCTION_ARGS)
{
	pgbson *leftBson = PG_GETARG_PGBSON(0);
	pgbson *rightBson = PG_GETARG_PGBSON(1);

	int compareResult = ComparePgbson(leftBson, rightBson);

	PG_RETURN_BOOL(compareResult > 0);
}


Datum
extension_bson_gte(PG_FUNCTION_ARGS)
{
	pgbson *leftBson = PG_GETARG_PGBSON(0);
	pgbson *rightBson = PG_GETARG_PGBSON(1);

	int compareResult = ComparePgbson(leftBson, rightBson);

	PG_RETURN_BOOL(compareResult >= 0);
}


/*
 * bson_unique_index_equal is a dummy function that is used by the runtime to represent a unique index comparison.
 * The operator is unused as it is always pushed into the RUM index for index evaluation.
 * Note we can't use bson_equal (which does a field by field semantic equality), nor dollar_equal since it expects
 * document @= filter (which is not the behavior seen for unique indexes). We need a custom commutative operator
 * that allows for index pushdown for unique.
 */
Datum
bson_unique_index_equal(PG_FUNCTION_ARGS)
{
	ereport(ERROR, errmsg(
				"Unique equal should only be an operator pushed to the index."));
}


/*
 * ComparePgbson compares 2 BSON objects.
 */
int
ComparePgbson(const pgbson *leftBson, const pgbson *rightBson)
{
	bson_iter_t leftIter;
	bson_iter_t rightIter;

	if (PgbsonEquals(leftBson, rightBson))
	{
		return 0;
	}

	PgbsonInitIterator(leftBson, &leftIter);
	PgbsonInitIterator(rightBson, &rightIter);
	return CompareBsonIter(&leftIter, &rightIter, true);
}


/*
 * Compares 2 pgbson objects which may be null.
 */
int
CompareNullablePgbson(pgbson *leftBson, pgbson *rightBson)
{
	/* If the bsons are both null or the same pointer, they are equal */
	if (leftBson == rightBson)
	{
		return 0;
	}

	if (leftBson == NULL)
	{
		return -1;
	}
	else if (rightBson == NULL)
	{
		return 1;
	}

	return ComparePgbson(leftBson, rightBson);
}


int
CompareBsonIter(bson_iter_t *leftIter, bson_iter_t *rightIter, bool compareFields)
{
	check_stack_depth();
	while (true)
	{
		bool leftNext = bson_iter_next(leftIter);
		bool rightNext = bson_iter_next(rightIter);
		int32_t cmp;
		pgbsonelement leftElement;
		pgbsonelement rightElement;

		if (!leftNext && !rightNext)
		{
			/* both reached the end, they must be equal. */
			return 0;
		}
		else if (!leftNext || !rightNext)
		{
			/* one of them ended, not equal. */
			/* if left has more, then left > right -> 1 */
			/* if left ended, then right > left -> -1. */
			return leftNext ? 1 : -1;
		}

		BsonIterToPgbsonElement(leftIter, &leftElement);
		BsonIterToPgbsonElement(rightIter, &rightElement);
		if (!compareFields)
		{
			leftElement.pathLength = 0;
			rightElement.pathLength = 0;
		}

		/* they both have values compare typeCode. */
		cmp = CompareBsonSortOrderType(&leftElement.bsonValue, &rightElement.bsonValue);
		if (cmp != 0)
		{
			return cmp;
		}

		/* next compare field name. */
		cmp = CompareStrings(leftElement.path, leftElement.pathLength, rightElement.path,
							 rightElement.pathLength);
		if (cmp != 0)
		{
			return cmp;
		}

		bool ignoreIsComparisonValid;
		cmp = CompareBsonValue(&leftElement.bsonValue, &rightElement.bsonValue,
							   &ignoreIsComparisonValid);
		if (cmp != 0)
		{
			return cmp;
		}
	}

	return 0;
}


/*
 * checks if two bson values are equal.
 * types are compared using mongo semantics, so comparing 1 to 1.0 will return true.
 */
bool
BsonValueEquals(const bson_value_t *left, const bson_value_t *right)
{
	bool isComparisonValidIgnore;
	return CompareBsonValueAndType(left, right, &isComparisonValidIgnore) == 0;
}


/*
 * Compares two bson values using strict comparison semantics. Instead of
 * considering type sort order (which is equal for all numeric types for example)
 * it compares the actual type values and then if the same, it compares the actual values.
 */
bool
BsonValueEqualsStrict(const bson_value_t *left, const bson_value_t *right)
{
	if (left == NULL || right == NULL)
	{
		return left == right;
	}

	if (left->value_type != right->value_type)
	{
		return false;
	}

	bool isComparisonValid;
	int cmp = CompareBsonValue(left, right, &isComparisonValid);

	return isComparisonValid && cmp == 0;
}


/*
 * Compares two bson values by mongo semantics.
 * Returns 0 if the two values are equal
 * Returns 1 if left > right
 * Returns -1 if left < right.
 * Sets isComparisonValid if the comparison between the two values
 * is not expected to be valid (e.g. comparison of non NaN values against NaN)
 */
int
CompareBsonValueAndType(const bson_value_t *left, const bson_value_t *right,
						bool *isComparisonValid)
{
	int cmp;
	*isComparisonValid = true;
	if ((cmp = CompareBsonSortOrderType(left, right)) != 0)
	{
		return cmp;
	}

	return CompareBsonValue(left, right, isComparisonValid);
}


double
BsonValueAsDouble(const bson_value_t *value)
{
	bool quiet = false;
	return BsonValueAsDoubleCore(value, quiet);
}


/*
 * Similar to BsonValueAsDouble but doesn't throw error if BSON_DECIMAL_128
 * is out of range of double
 */
double
BsonValueAsDoubleQuiet(const bson_value_t *value)
{
	bool quiet = true;
	return BsonValueAsDoubleCore(value, quiet);
}


/*
 * Converts Numeric bson value to 64 bit integer
 * This method can throw, see GetBsonDecimal128AsInt64 method summary for details.
 */
int64_t
BsonValueAsInt64(const bson_value_t *value)
{
	bool throwIfFailed = false;
	return BsonValueAsInt64WithRoundingMode(value, ConversionRoundingMode_Floor,
											throwIfFailed);
}


/*
 * Converts Numeric bson value to 64 bit integer with the specified rounding mode
 * If throwIfFailed is true, we will do validation and throw errors if:
 *    - The type is not convertible to int64.
 *    - The value is outside of the int64 range.
 * GetBsonDecimal128AsInt64 can throw, see method summary for details.
 */
int64_t
BsonValueAsInt64WithRoundingMode(const bson_value_t *value,
								 ConversionRoundingMode roundingMode,
								 bool throwIfFailed)
{
	if (throwIfFailed)
	{
		if (!BsonValueIsNumber(value))
		{
			ereport(ERROR, (errcode(MongoLocation16004), errmsg(
								"can't convert from BSON type %s to long",
								BsonTypeName(value->value_type))));
		}

		bool checkFixedInteger = false;
		if (!IsBsonValue64BitInteger(value, checkFixedInteger))
		{
			ereport(ERROR, (errcode(MongoLocation31109), errmsg(
								"Can't coerce out of range value %s to long",
								BsonValueToJsonForLogging(value))));
		}
	}

	switch (value->value_type)
	{
		case BSON_TYPE_BOOL:
		{
			return (int64_t) value->value.v_bool;
		}

		case BSON_TYPE_DOUBLE:
		{
			if (roundingMode == ConversionRoundingMode_NearestEven)
			{
				return (int64_t) round(value->value.v_double);
			}
			else
			{
				return (int64_t) value->value.v_double;
			}
		}

		case BSON_TYPE_INT64:
		{
			return value->value.v_int64;
		}

		case BSON_TYPE_DATE_TIME:
		{
			return value->value.v_datetime;
		}

		case BSON_TYPE_INT32:
		{
			return (int64_t) value->value.v_int32;
		}

		case BSON_TYPE_DECIMAL128:
		{
			return GetBsonDecimal128AsInt64(value, roundingMode);
		}

		default:
		{
			return 0;
		}
	}
}


/*
 * Converts Numeric bson value to 32 bit integer
 * This method throws `MongoConversionFailure` if bson_value_type is v_decimal128 and :
 *    - NaN is attempted in conversion
 *    - converted result overflows the int32 range
 */
int32_t
BsonValueAsInt32(const bson_value_t *value)
{
	return BsonValueAsInt32WithRoundingMode(value, ConversionRoundingMode_Floor);
}


/*
 * Converts Numeric bson value to 32 bit integer with the specified rounding mode
 * This method throws `MongoConversionFailure` if bson_value_type is v_decimal128 and :
 *    - NaN is attempted in conversion
 *    - converted result overflows the int32 range
 */
int32_t
BsonValueAsInt32WithRoundingMode(const bson_value_t *value,
								 ConversionRoundingMode roundingMode)
{
	switch (value->value_type)
	{
		case BSON_TYPE_BOOL:
		{
			return (int32_t) value->value.v_bool;
		}

		case BSON_TYPE_DOUBLE:
		{
			if (roundingMode == ConversionRoundingMode_NearestEven)
			{
				return (int32_t) round(value->value.v_double);
			}
			else
			{
				return (int32_t) value->value.v_double;
			}
		}

		case BSON_TYPE_INT64:
		{
			return (int32_t) value->value.v_int64;
		}

		case BSON_TYPE_INT32:
		{
			return value->value.v_int32;
		}

		case BSON_TYPE_DECIMAL128:
		{
			return GetBsonDecimal128AsInt32(value, roundingMode);
		}

		default:
		{
			return 0;
		}
	}
}


/*
 * Converts a bson value to bool
 * It returns true for any Numeric value not 0 and false for any 0.
 * For non Numeric, it returns false for EOD, Undefined or NULL and true for any other value.
 */
bool
BsonValueAsBool(const bson_value_t *value)
{
	switch (value->value_type)
	{
		case BSON_TYPE_BOOL:
		{
			return value->value.v_bool;
		}

		case BSON_TYPE_DOUBLE:
		{
			return value->value.v_double != 0.0;
		}

		case BSON_TYPE_INT32:
		{
			return value->value.v_int32 != 0;
		}

		case BSON_TYPE_INT64:
		{
			return value->value.v_int64 != 0;
		}

		case BSON_TYPE_DECIMAL128:
		{
			return !IsDecimal128Zero(value);
		}

		case BSON_TYPE_NULL:
		case BSON_TYPE_EOD:
		case BSON_TYPE_UNDEFINED:
		{
			return false;
		}

		default:
		{
			/* Any other value evaluates to true. */
			return true;
		}
	}
}


/* Given a bson value it tries to get its date time representation in milliseconds. */
int64_t
BsonValueAsDateTime(const bson_value_t *value)
{
	switch (value->value_type)
	{
		case BSON_TYPE_DATE_TIME:
		{
			return value->value.v_datetime;
		}

		case BSON_TYPE_TIMESTAMP:
		{
			return value->value.v_timestamp.timestamp * MillisecondsInSecond;
		}

		case BSON_TYPE_OID:
		{
			return bson_oid_get_time_t(&value->value.v_oid) * MillisecondsInSecond;
		}

		default:
		{
			ereport(ERROR, (errcode(MongoLocation16006), errmsg(
								"can't convert from BSON type %s to Date",
								BsonTypeName(value->value_type))));
		}
	}
}


/* Indicates whether the value can be represented as an int32 value without overflow or truncating decimal digits. */
bool
IsBsonValue32BitInteger(const bson_value_t *value, bool checkFixedInteger)
{
	switch (value->value_type)
	{
		case BSON_TYPE_INT32:
		{
			return true;
		}

		case BSON_TYPE_DOUBLE:
		{
			double doubleVal = value->value.v_double;
			return doubleVal <= INT32_MAX &&
				   doubleVal >= INT32_MIN &&
				   (!checkFixedInteger || (floor(doubleVal) == doubleVal));
		}

		case BSON_TYPE_INT64:
		{
			int32_t intVal = (int32_t) value->value.v_int64;
			return intVal == value->value.v_int64;
		}

		case BSON_TYPE_DECIMAL128:
		{
			return IsDecimal128InInt32Range(value) &&
				   (!checkFixedInteger || IsDecimal128AFixedInteger(value));
		}

		default:
		{
			return false;
		}
	}
}


/* Indicates whether the value can be represented as an int64 value without overflow or truncating decimal digits. */
bool
IsBsonValue64BitInteger(const bson_value_t *value, bool checkFixedInteger)
{
	switch (value->value_type)
	{
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		{
			return true;
		}

		case BSON_TYPE_DOUBLE:
		{
			bson_value_t dec128Val;
			dec128Val.value_type = BSON_TYPE_DECIMAL128;
			dec128Val.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(value);
			return IsDecimal128InInt64Range(&dec128Val) &&
				   (!checkFixedInteger || IsDecimal128AFixedInteger(&dec128Val));
		}

		case BSON_TYPE_DECIMAL128:
		{
			return IsDecimal128InInt64Range(value) &&
				   (!checkFixedInteger || IsDecimal128AFixedInteger(value));
		}

		default:
		{
			return false;
		}
	}
}


bool
IsBsonValueFixedInteger(const bson_value_t *value)
{
	switch (value->value_type)
	{
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		{
			return true;
		}

		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		{
			bson_value_t decimalValue = {
				.value_type = BSON_TYPE_DECIMAL128,
				.value.v_decimal128 = GetBsonValueAsDecimal128(value),
			};

			return IsDecimal128AFixedInteger(&decimalValue);
		}

		default:
			return false;
	}
}


bool
BsonTypeIsNumber(bson_type_t type)
{
	switch (type)
	{
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_INT64:
		case BSON_TYPE_INT32:
		case BSON_TYPE_DECIMAL128:
		{
			return true;
		}

		default:
			return false;
	}
}


/* returns true if bson value is (double)NaN or (Decimal128)NaN */
bool
IsBsonValueNaN(const bson_value_t *value)
{
	if (value->value_type == BSON_TYPE_DECIMAL128 ||
		value->value_type == BSON_TYPE_DOUBLE)
	{
		if (isnan(BsonValueAsDouble(value)))
		{
			return true;
		}
	}

	return false;
}


/*
 * Returns 0 if value is not Infinity
 * Returns 1 if value is Infinity
 * Returns -1 if value is -Infinity
 */
int
IsBsonValueInfinity(const bson_value_t *value)
{
	if (value->value_type == BSON_TYPE_DECIMAL128 ||
		value->value_type == BSON_TYPE_DOUBLE)
	{
		double doubleValue = BsonValueAsDouble(value);

		if (doubleValue == (double) INFINITY)
		{
			return 1;
		}

		if (doubleValue == (double) -INFINITY)
		{
			return -1;
		}
	}

	return 0;
}


bool
BsonTypeIsNumberOrBool(bson_type_t type)
{
	switch (type)
	{
		case BSON_TYPE_BOOL:
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_INT64:
		case BSON_TYPE_INT32:
		case BSON_TYPE_DECIMAL128:
		{
			return true;
		}

		default:
			return false;
	}
}


int
CompareBsonSortOrderType(const bson_value_t *left, const bson_value_t *right)
{
	return CompareSortOrderType(left->value_type, right->value_type);
}


int
CompareSortOrderType(bson_type_t left, bson_type_t right)
{
	int32_t leftType = GetSortOrderType(left);
	int32_t rightType = GetSortOrderType(right);

	return leftType - rightType;
}


/* --------------------------------------------------------- */
/* Helpers */
/* --------------------------------------------------------- */


/*
 *  Compares two bson values.
 *  Please DO NOT  expose this method beyond this file.
 */
static int
CompareBsonValue(const bson_value_t *left, const bson_value_t *right,
				 bool *isComparisonValid)
{
	*isComparisonValid = true;
	if (CompareBsonSortOrderType(left, right) != 0)
	{
		ereport(ERROR, errmsg("left & right sort data types must match"));
	}

	/* same type, same path. now compare value. */
	switch (left->value_type)
	{
		case BSON_TYPE_EOD:
		case BSON_TYPE_MINKEY:
		case BSON_TYPE_UNDEFINED:
		case BSON_TYPE_NULL:
		case BSON_TYPE_MAXKEY:
		{
			return 0;
		}

		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_DECIMAL128:
		case BSON_TYPE_BOOL:
		{
			return CompareNumbers(left, right, isComparisonValid);
		}

		case BSON_TYPE_UTF8:
		{
			return CompareStrings(
				left->value.v_utf8.str,
				left->value.v_utf8.len,
				right->value.v_utf8.str,
				right->value.v_utf8.len);
		}

		case BSON_TYPE_SYMBOL:
		{
			return CompareStrings(
				left->value.v_symbol.symbol,
				left->value.v_symbol.len,
				right->value.v_symbol.symbol,
				right->value.v_symbol.len);
		}

		case BSON_TYPE_DOCUMENT:
		case BSON_TYPE_ARRAY:
		{
			bson_iter_t leftInnerIt, rightInnerIt;
			if (!bson_iter_init_from_data(
					&leftInnerIt,
					left->value.v_doc.data,
					left->value.v_doc.data_len))
			{
				ereport(ERROR, errmsg(
							"Could not initialize nested iterator for document"));
			}
			if (!bson_iter_init_from_data(
					&rightInnerIt,
					right->value.v_doc.data,
					right->value.v_doc.data_len))
			{
				ereport(ERROR, errmsg(
							"Could not initialize nested iterator for document"));
			}

			bool compareFields = true;
			return CompareBsonIter(&leftInnerIt, &rightInnerIt, compareFields);
		}

		case BSON_TYPE_BINARY:
		{
			uint32_t leftLen = left->value.v_binary.data_len;
			uint32_t rightLen = right->value.v_binary.data_len;
			if (leftLen != rightLen)
			{
				return leftLen - rightLen;
			}

			bson_subtype_t leftSubType = left->value.v_binary.subtype;
			bson_subtype_t rightSubType = right->value.v_binary.subtype;
			if (leftSubType != rightSubType)
			{
				return (int) leftSubType - (int) rightSubType;
			}

			const uint8_t *leftInner = left->value.v_binary.data;
			const uint8_t *rightInner = right->value.v_binary.data;
			return memcmp(leftInner, rightInner, leftLen);
		}

		case BSON_TYPE_OID:
		{
			const bson_oid_t *leftOid = &left->value.v_oid;
			const bson_oid_t *rightOid = &right->value.v_oid;
			return bson_oid_compare(leftOid, rightOid);
		}

		case BSON_TYPE_DATE_TIME:
		{
			int64_t leftVal = left->value.v_datetime;
			int64_t rightVal = right->value.v_datetime;
			return leftVal > rightVal ? 1 : (leftVal == rightVal ? 0 : -1);
		}

		case BSON_TYPE_TIMESTAMP:
		{
			/* compare the time value first */
			int64_t leftVal = left->value.v_timestamp.timestamp;
			int64_t rightVal = right->value.v_timestamp.timestamp;
			if (leftVal != rightVal)
			{
				return leftVal > rightVal ? 1 : -1;
			}

			/* then compare the increment value after. */
			leftVal = left->value.v_timestamp.increment;
			rightVal = right->value.v_timestamp.increment;

			return leftVal > rightVal ? 1 : (leftVal == rightVal ? 0 : -1);
		}

		case BSON_TYPE_REGEX:
		{
			if (left->value.v_regex.regex == NULL || right->value.v_regex.regex == NULL)
			{
				return (left->value.v_regex.regex != NULL) ? 1 : -1;
			}

			int cmp = strcmp(
				left->value.v_regex.regex,
				right->value.v_regex.regex);
			if (cmp != 0)
			{
				return cmp;
			}

			if (left->value.v_regex.options == NULL || right->value.v_regex.options ==
				NULL)
			{
				return (left->value.v_regex.options != NULL) ? 1 : -1;
			}

			return strcmp(left->value.v_regex.options, right->value.v_regex.options);
		}

		case BSON_TYPE_CODE:
		{
			return CompareStrings(
				left->value.v_code.code,
				left->value.v_code.code_len,
				right->value.v_code.code,
				right->value.v_code.code_len);
		}

		case BSON_TYPE_CODEWSCOPE:
		{
			int cmp = CompareStrings(
				left->value.v_codewscope.code,
				left->value.v_codewscope.code_len,
				right->value.v_codewscope.code,
				right->value.v_codewscope.code_len);
			if (cmp != 0)
			{
				return cmp;
			}

			bson_iter_t leftInnerIt, rightInnerIt;
			if (!bson_iter_init_from_data(
					&leftInnerIt,
					left->value.v_codewscope.scope_data,
					left->value.v_codewscope.scope_len))
			{
				ereport(ERROR, errmsg(
							"Could not initialize nested iterator for scope"));
			}
			if (!bson_iter_init_from_data(
					&rightInnerIt,
					right->value.v_codewscope.scope_data,
					right->value.v_codewscope.scope_len))
			{
				ereport(ERROR, errmsg(
							"Could not initialize nested iterator for scope"));
			}

			bool compareFields = true;
			return CompareBsonIter(&leftInnerIt, &rightInnerIt, compareFields);
		}

		case BSON_TYPE_DBPOINTER:
		{
			int cmp = CompareStrings(
				left->value.v_dbpointer.collection,
				left->value.v_dbpointer.collection_len,
				right->value.v_dbpointer.collection,
				right->value.v_dbpointer.collection_len);
			if (cmp != 0)
			{
				return cmp;
			}

			const bson_oid_t *leftOid = &left->value.v_dbpointer.oid;
			const bson_oid_t *rightOid = &right->value.v_dbpointer.oid;
			return bson_oid_compare(leftOid, rightOid);
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg("invalid bson type %s - not supported yet",
								   BsonTypeName(left->value_type)),
							errhint("invalid bson type %s - not supported yet",
									BsonTypeName(left->value_type))));
		}
	}
}


static int
GetSortOrderType(bson_type_t type)
{
	switch (type)
	{
		case BSON_TYPE_EOD:
		case BSON_TYPE_MINKEY:
		{
			return 0x0;
		}

		case BSON_TYPE_UNDEFINED:
		case BSON_TYPE_NULL:
		{
			return 0x1;
		}

		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_DECIMAL128:
		{
			return 0x2;
		}

		case BSON_TYPE_UTF8:
		case BSON_TYPE_SYMBOL:
		{
			return 0x3;
		}

		case BSON_TYPE_DOCUMENT:
		{
			return 0x4;
		}

		case BSON_TYPE_ARRAY:
		{
			return 0x5;
		}

		case BSON_TYPE_BINARY:
		{
			return 0x6;
		}

		case BSON_TYPE_OID:
		{
			return 0x7;
		}

		case BSON_TYPE_BOOL:
		{
			return 0x8;
		}

		case BSON_TYPE_DATE_TIME:
		{
			return 0x9;
		}

		case BSON_TYPE_TIMESTAMP:
		{
			return 0xA;
		}

		case BSON_TYPE_REGEX:
		{
			return 0xB;
		}

		case BSON_TYPE_DBPOINTER:
		{
			return 0xC;
		}

		case BSON_TYPE_CODE:
		{
			return 0xD;
		}

		case BSON_TYPE_CODEWSCOPE:
		{
			return 0xE;
		}

		case BSON_TYPE_MAXKEY:
		{
			return 0xF;
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg("invalid bson type - not supported yet")));
		}
	}
}


static int
CompareNumbers(const bson_value_t *leftValue, const bson_value_t *rightValue,
			   bool *isComparisonValid)
{
	*isComparisonValid = true;
	if (leftValue->value_type == BSON_TYPE_DECIMAL128 ||
		rightValue->value_type == BSON_TYPE_DECIMAL128)
	{
		/* promote type to decimal128 for both values */
		bson_value_t leftDecimal, rightDecimal;
		leftDecimal.value_type = BSON_TYPE_DECIMAL128;
		rightDecimal.value_type = BSON_TYPE_DECIMAL128;

		leftDecimal.value.v_decimal128 = GetBsonValueAsDecimal128(leftValue);
		rightDecimal.value.v_decimal128 = GetBsonValueAsDecimal128(rightValue);

		return CompareBsonDecimal128(&leftDecimal, &rightDecimal, isComparisonValid);
	}
	else if (leftValue->value_type == BSON_TYPE_DOUBLE ||
			 rightValue->value_type == BSON_TYPE_DOUBLE)
	{
		long double leftVal = BsonNumberAsLongDouble(leftValue);
		long double rightVal = BsonNumberAsLongDouble(rightValue);

		/* special case handling for NaN */
		if (isnan(leftVal) || isnan(rightVal))
		{
			if (isnan(leftVal) && isnan(rightVal))
			{
				return 0;
			}
			else if (isnan(leftVal))
			{
				*isComparisonValid = false;
				return -1;
			}
			else
			{
				*isComparisonValid = false;
				return 1;
			}
		}

		return leftVal > rightVal ? 1 : (leftVal == rightVal ? 0 : -1);
	}
	else
	{
		int64_t leftVal = BsonValueAsInt64(leftValue);
		int64_t rightVal = BsonValueAsInt64(rightValue);
		return leftVal > rightVal ? 1 : (leftVal == rightVal ? 0 : -1);
	}
}


/*
 * Converts a bsonValue numeric type to long double type
 * with 80 bit precision.
 */
static long double
BsonNumberAsLongDouble(const bson_value_t *value)
{
	switch (value->value_type)
	{
		case BSON_TYPE_BOOL:
		{
			return (long double) value->value.v_bool;
		}

		case BSON_TYPE_DOUBLE:
		{
			return (long double) value->value.v_double;
		}

		case BSON_TYPE_INT32:
		{
			return (long double) value->value.v_int32;
		}

		case BSON_TYPE_INT64:
		{
			return (long double) value->value.v_int64;
		}

		case BSON_TYPE_DECIMAL128:
		{
			return GetBsonDecimal128AsLongDouble(value);
		}

		default:
		{
			return 0;
		}
	}
}


int
CompareStrings(const char *left, uint32_t leftLength, const char *right, uint32_t
			   rightLength)
{
	uint32_t minLength = leftLength < rightLength ? leftLength : rightLength;
	if (minLength == 0)
	{
		return leftLength - rightLength;
	}

	int32_t cmp = memcmp(left, right, minLength);
	if (cmp != 0)
	{
		return cmp;
	}

	return leftLength - rightLength;
}


/*
 * Core implementation of converting bson value to double
 * In quiet mode no error is thrown if conversion results in overflow or underflow
 */
static double
BsonValueAsDoubleCore(const bson_value_t *value, bool quiet)
{
	switch (value->value_type)
	{
		case BSON_TYPE_BOOL:
		{
			return (double) value->value.v_bool;
		}

		case BSON_TYPE_DOUBLE:
		{
			return value->value.v_double;
		}

		case BSON_TYPE_INT32:
		{
			return (double) value->value.v_int32;
		}

		case BSON_TYPE_INT64:
		{
			return (double) value->value.v_int64;
		}

		case BSON_TYPE_DECIMAL128:
		{
			if (quiet)
			{
				return GetBsonDecimal128AsDoubleQuiet(value);
			}
			else
			{
				return GetBsonDecimal128AsDouble(value);
			}
		}

		case BSON_TYPE_DATE_TIME:
		{
			return (double) value->value.v_datetime;
		}

		default:
		{
			return 0;
		}
	}
}
