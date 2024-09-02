/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/types/decimal128.c
 *
 * Decimal128 support for helioapi extension
 * Referrences:
 * http://www.netlib.org/misc/intel/README.txt
 *
 * Testing the library methods:
 *    - <folder with intelmathlib>/TESTS/README.txt.
 *    - ./readtest.in list multiple test cases where operations can signal flag exception.
 *
 *-------------------------------------------------------------------------
 */
#include "types/decimal128.h"

/* Including lib headers here to limit the exposure and separating the concerns across */
#include <bid_conf.h>
#include <bid_functions.h>
#include <bid_internal.h>
#include <math.h>

#include "utils/mongo_errors.h"


#if BID_BIG_ENDIAN
#define BID_HIGH_BITS 0
#define BID_LOW_BITS 1
#else
#define BID_HIGH_BITS 1
#define BID_LOW_BITS 0
#endif

#define BID128_EXP_BIAS 6176ull

/*
 * Used to shift the exponent bits in their respective place based on the dec128 format requirement.
 * For more info => https://en.wikipedia.org/wiki/Decimal128_floating-point_format#:~:text=single%20byte%20value.-,Binary%20integer%20significand%20field,-%5Bedit%5D
 */
#define BID128_EXP_BITS_OFFSET 49

/* rounding modes can be separately defined while doing any mathematical operations on decimal128 */
/* For more info: https://en.wikipedia.org/wiki/Floating-point_arithmetic#Rounding_modes */
typedef enum Decimal128RoundingMode
{
	/* Rounds the number to nearest value, in case of ties between digits rounds to nearest even */
	Decimal128RoundingMode_NearestEven = 0,

	/* Rounds towards smaller digit, -ve results thus tends to round away from zero */
	Decimal128RoundingMode_Downward = 1,

	/* Rounds towards larger digit, -ve results thus tends to round towards zero */
	Decimal128RoundingMode_Upward = 2,

	/* Rounds towards zero or in other words truncates the results */
	Decimal128RoundingMode_TowardZero = 3,

	/* Rounds the number to nearest value, in case of ties between digits rounds away from zero */
	Decimal128RoundingMode_NearestAway = 4,

	/* Default rounding modes which satisfies most of the decimal128 needs */
	Decimal128RoundingMode_Default = Decimal128RoundingMode_NearestEven
} Decimal128RoundingMode;

/* Type to indicate the type of math operation to perform on a decimal 128. */
typedef enum Decimal128MathOperation
{
	Decimal128MathOperation_Add = 0,
	Decimal128MathOperation_Subtract = 1,
	Decimal128MathOperation_Multiply = 2,
	Decimal128MathOperation_Divide = 3,
	Decimal128MathOperation_Mod = 4,
	Decimal128MathOperation_Ceil = 5,
	Decimal128MathOperation_Floor = 6,
	Decimal128MathOperation_Exp = 7,
	Decimal128MathOperation_Sqrt = 8,
	Decimal128MathOperation_Abs = 9,
	Decimal128MathOperation_Log10 = 10,
	Decimal128MathOperation_NaturalLogarithm = 11,
	Decimal128MathOperation_Log = 12,
	Decimal128MathOperation_Pow = 13,
	Decimal128MathOperation_Round = 14,
	Decimal128MathOperation_Trunc = 15,
	Decimal128MathOperation_Sin = 16,
	Decimal128MathOperation_Cos = 17,
	Decimal128MathOperation_Tan = 18,
	Decimal128MathOperation_Sinh = 19,
	Decimal128MathOperation_Cosh = 20,
	Decimal128MathOperation_Tanh = 21,
	Decimal128MathOperation_Asin = 22,
	Decimal128MathOperation_Acos = 23,
	Decimal128MathOperation_Atan = 24,
	Decimal128MathOperation_Atan2 = 25,
	Decimal128MathOperation_Asinh = 26,
	Decimal128MathOperation_Acosh = 27,
	Decimal128MathOperation_Atanh = 28,
} Decimal128MathOperation;

typedef unsigned int _IDEC_flags;

#define HIGH_BITS(x) (x->value.v_decimal128.high)
#define LOW_BITS(x) (x->value.v_decimal128.low)

#define ALL_EXCEPTION_FLAG_CLEAR 0


/* ============================ */
/* Exception Flag Helper Macros */
/* ============================ */

#define IsOperationInvalid(x) (((_IDEC_flags) x & BID_INVALID_EXCEPTION) == \
							   BID_INVALID_EXCEPTION)
#define IsOperationDivideByZero(x) (((_IDEC_flags) x & BID_ZERO_DIVIDE_EXCEPTION) == \
									BID_ZERO_DIVIDE_EXCEPTION)
#define IsOperationOverflow(x) (((_IDEC_flags) x & BID_OVERFLOW_EXCEPTION) == \
								BID_OVERFLOW_EXCEPTION)
#define IsOperationUnderflow(x) (((_IDEC_flags) x & BID_UNDERFLOW_EXCEPTION) == \
								 BID_UNDERFLOW_EXCEPTION)
#define IsOperationInExact(x) (((_IDEC_flags) x & BID_INEXACT_EXCEPTION) == \
							   BID_INEXACT_EXCEPTION)
#define IsOperationSuccess(x) ((_IDEC_flags) x == 0)

/* ======================================= */
/* Internal Utilities to work with library */
/* ======================================= */

static inline BID_UINT128 GetBIDUINT128FromBsonValue(const bson_value_t *decimal);
static inline bson_decimal128_t GetBsonDecimal128FromBIDUINT128(const BID_UINT128 *bid);
static inline void CheckDecimal128Type(const bson_value_t *value);
static inline void ThrowConversionFailureError(const BID_UINT128 value);
static bool BIDUINT128SignalingEqual(BID_UINT128 x, BID_UINT128 y,
									 _IDEC_flags *exceptionFlag);
static void LogWith1Operand(const char *logMessage, const BID_UINT128 *op1, const
							_IDEC_flags *flag);
static void LogWith2Operands(const char *logMessage, const BID_UINT128 *op1, const
							 BID_UINT128 *op2, const _IDEC_flags *flag);
static Decimal128Result GetDecimal128ResultFromFlag(_IDEC_flags flag);
static int64_t Decimal128ToInt64Floor(const bson_value_t *value, bool *isOverFlow);
static Decimal128Result RoundOrTruncateDecimal128Number(const bson_value_t *number,
														int64_t precision,
														bson_value_t *result,
														Decimal128MathOperation operation);
static Decimal128Result Decimal128MathematicalOperation2Operands(const bson_value_t *x,
																 const bson_value_t *y,
																 bson_value_t *result,
																 Decimal128MathOperation
																 operation);
static Decimal128Result Decimal128MathematicalOperation1Operand(const bson_value_t *value,
																bson_value_t *result,
																Decimal128MathOperation
																operation);
static bson_decimal128_t GetBsonValueAsDecimal128Core(const bson_value_t *value,
													  bool shouldQuantized);


/*
 * Get the decimal 128 value as int32.
 *
 * This method throws `MongoConversionFailure` if :
 *    - NaN is attempted in conversion
 *    - converted result overflows the int32 range
 */
int32_t
GetBsonDecimal128AsInt32(const bson_value_t *value, ConversionRoundingMode roundingMode)
{
	BID_UINT128 bidValue = GetBIDUINT128FromBsonValue(value);

	_IDEC_flags exceptionFlags = ALL_EXCEPTION_FLAG_CLEAR;

	int32_t returnValue;
	if (roundingMode == ConversionRoundingMode_NearestEven)
	{
		returnValue = bid128_to_int32_xrnint(bidValue, &exceptionFlags);
	}
	else
	{
		returnValue = bid128_to_int32_xint(bidValue, &exceptionFlags);
	}

	/* Above conversion can signal invalid conversion if
	 *      1- bidValue is (+/-) NaN
	 *      2- bidValue is (+/-) Infinity
	 *      3- Converted value cannot fit in the range of int32
	 *
	 *  Conversion can also signal inexact exception which is ignored as similar to Native Mongo
	 */
	if (!IsOperationSuccess(exceptionFlags) && IsOperationInvalid(exceptionFlags))
	{
		if (IsOperationInvalid(exceptionFlags))
		{
			ThrowConversionFailureError(bidValue);
		}
		else
		{
			/* Ignore and log other exceptions */
			LogWith1Operand("Decimal128 conversion to int32 signalled exception",
							&bidValue,
							&exceptionFlags);
		}
	}
	return returnValue;
}


/*
 * Get the decimal 128 value as int64.
 *
 * This method throws `MongoConversionFailure` error if NaN is attempted in conversion or converted result overflows
 * the int64 range
 */
int64_t
GetBsonDecimal128AsInt64(const bson_value_t *value, ConversionRoundingMode roundingMode)
{
	BID_UINT128 bidValue = GetBIDUINT128FromBsonValue(value);

	_IDEC_flags exceptionFlags = ALL_EXCEPTION_FLAG_CLEAR;

	int64_t returnValue;
	if (roundingMode == ConversionRoundingMode_NearestEven)
	{
		returnValue = bid128_to_int64_xrnint(bidValue, &exceptionFlags);
	}
	else
	{
		returnValue = bid128_to_int64_xint(bidValue, &exceptionFlags);
	}

	/* Above conversion can signal invalid conversion if
	 *      1- bidValue is (+/-) NaN
	 *      2- bidValue is (+/-) Infinity
	 *      3- Converted value cannot fit in the range of int64
	 *
	 *  Conversion can also signal inexact exception which is ignored as similar to Native Mongo
	 */
	if (!IsOperationSuccess(exceptionFlags))
	{
		if (IsOperationInvalid(exceptionFlags))
		{
			ThrowConversionFailureError(bidValue);
		}
		else
		{
			/* Ignore and log other exceptions */
			LogWith1Operand("Decimal128 conversion to int64 signalled exception",
							&bidValue,
							&exceptionFlags);
		}
	}
	return returnValue;
}


/*
 * Get the decimal 128 value as double.
 *
 * This method throws `MongoConversionFailure` error if converted result overflows
 * the double range
 */
double
GetBsonDecimal128AsDouble(const bson_value_t *value)
{
	BID_UINT128 bidValue = GetBIDUINT128FromBsonValue(value);

	_IDEC_flags exceptionFlags = ALL_EXCEPTION_FLAG_CLEAR;

	double returnValue = bid128_to_binary64(bidValue, Decimal128RoundingMode_Default,
											&exceptionFlags);

	/* Above conversion can signal invalid, underflow, overflow and inexact exception */
	if (!IsOperationSuccess(exceptionFlags))
	{
		if (IsOperationOverflow(exceptionFlags) || IsOperationUnderflow(exceptionFlags))
		{
			ereport(ERROR, (errcode(MongoConversionFailure),
							errmsg("Conversion would overflow target type")));
		}
		else
		{
			/* Ignore and log other exceptions */
			LogWith1Operand("Decimal128 conversion to double signalled exception",
							&bidValue, &exceptionFlags);
		}
	}

	return returnValue;
}


/*
 * Get the decimal 128 value as double.
 *
 * This method doesn't throw any error if the operation overflows or underflows
 * and returns infinity/-infinity
 */
double
GetBsonDecimal128AsDoubleQuiet(const bson_value_t *value)
{
	BID_UINT128 bidValue = GetBIDUINT128FromBsonValue(value);

	_IDEC_flags exceptionFlags = ALL_EXCEPTION_FLAG_CLEAR;

	double returnValue = bid128_to_binary64(bidValue, Decimal128RoundingMode_Default,
											&exceptionFlags);

	if (!IsOperationSuccess(exceptionFlags))
	{
		/* Ignore and log other exceptions */
		LogWith1Operand("Decimal128 conversion to double signalled exception",
						&bidValue, &exceptionFlags);
	}

	return returnValue;
}


/*
 * Get the decimal 128 value as long double.
 *
 * This method throws `MongoConversionFailure` error if converted result overflows
 * the int80 range
 */
long double
GetBsonDecimal128AsLongDouble(const bson_value_t *value)
{
	BID_UINT128 bidValue = GetBIDUINT128FromBsonValue(value);

	_IDEC_flags exceptionFlags = ALL_EXCEPTION_FLAG_CLEAR;

	long double returnValue = bid128_to_binary80(bidValue, Decimal128RoundingMode_Default,
												 &exceptionFlags);

	/* Above conversion can signal invalid, underflow, overflow and inexact exception */
	if (!IsOperationSuccess(exceptionFlags))
	{
		if (IsOperationOverflow(exceptionFlags) || IsOperationUnderflow(exceptionFlags))
		{
			ereport(ERROR, (errcode(MongoConversionFailure),
							errmsg("Conversion would overflow target type")));
		}
		else
		{
			/* Ignore and log other exceptions */
			LogWith1Operand("Decimal128 conversion to double signalled exception",
							&bidValue, &exceptionFlags);
		}
	}

	return returnValue;
}


/*
 * Get the decimal 128 value as bool.
 */
bool
GetBsonDecimal128AsBool(const bson_value_t *value)
{
	BID_UINT128 bidValue = GetBIDUINT128FromBsonValue(value);
	return !bid128_isZero(bidValue);
}


/*
 * Get the decimal 128 value as string.
 */
char *
GetBsonDecimal128AsString(const bson_value_t *value)
{
	CheckDecimal128Type(value);
	char decimal128StringRepr[BSON_DECIMAL128_STRING];
	bson_decimal128_to_string(&(value->value.v_decimal128), decimal128StringRepr);
	short len = strlen(decimal128StringRepr) + 1; /* +1 for '\0' */
	char *finalString = palloc(len);
	memcpy(finalString, decimal128StringRepr, len);
	return finalString;
}


/**
 * Get the bson_decimal_128 representation value of other number types with Quantization
 */
bson_decimal128_t
GetBsonValueAsDecimal128Quantized(const bson_value_t *value)
{
	bool shouldQuantizeDouble = true;
	return GetBsonValueAsDecimal128Core(value, shouldQuantizeDouble);
}


/*
 * Gets BSON value as decimal128 value.
 * Quantization is turned off for this variant
 */
bson_decimal128_t
GetBsonValueAsDecimal128(const bson_value_t *value)
{
	bool shouldQuantizeDouble = false;
	return GetBsonValueAsDecimal128Core(value, shouldQuantizeDouble);
}


/*
 * Checks if decimal128 value is NaN
 */
bool
IsDecimal128NaN(const bson_value_t *value)
{
	BID_UINT128 bid = GetBIDUINT128FromBsonValue(value);
	return bid128_isNaN(bid);
}


/*
 * Checks if decimal128 value is infinite
 */
bool
IsDecimal128Infinity(const bson_value_t *value)
{
	BID_UINT128 bid = GetBIDUINT128FromBsonValue(value);
	return bid128_isInf(bid);
}


/*
 * Checks if decimal128 value is zero, subnormal or normal (not infinite or NaN)
 */
bool
IsDecimal128Finite(const bson_value_t *value)
{
	BID_UINT128 bid = GetBIDUINT128FromBsonValue(value);
	return bid128_isFinite(bid);
}


/*
 * Checks if decimal128 value is zero
 */
bool
IsDecimal128Zero(const bson_value_t *value)
{
	BID_UINT128 bid = GetBIDUINT128FromBsonValue(value);
	return bid128_isZero(bid);
}


/*
 * Sets decimal128 NaN in the provided value
 */
void
SetDecimal128NaN(bson_value_t *value)
{
	value->value.v_decimal128.high = NAN_MASK64;
	value->value.v_decimal128.low = 0ul;
}


/*
 * Sets decimal128 negative infinity in the provided value
 */
void
SetDecimal128NegativeInfinity(bson_value_t *value)
{
	value->value.v_decimal128.high = SINFINITY_MASK64;
	value->value.v_decimal128.low = 0ul;
}


/*
 * Sets decimal128 positive infinity in the provided value
 */
void
SetDecimal128PositiveInfinity(bson_value_t *value)
{
	value->value.v_decimal128.high = INFINITY_MASK64;
	value->value.v_decimal128.low = 0ul;
}


/*
 * Sets decimal128 zero in the provided value
 */
void
SetDecimal128Zero(bson_value_t *value)
{
	/*
	 * Let's see what is happening in the below line.
	 * 6176 => 1100000100000
	 * 6176 << 49 => (sign)0 (exponent)01100000100000 (significand)0000000000000000000000000000000000000000000000000
	 *
	 * Now the exponent bits are set to => 01100000100000 which is again 6176
	 *
	 * Finally subtracting the bias/ offset will give => 6176 - 6176 = 0 (exponent)
	 */
	value->value.v_decimal128.high = (BID128_EXP_BIAS << BID128_EXP_BITS_OFFSET);
	value->value.v_decimal128.low = 0ul;
}


/*
 * Checks if a given decimal128 number is in range of Int 64 numbers.
 *
 * Decimal128 is truncated to int64 by removing the decimal part for conversion
 */
bool
IsDecimal128InInt64Range(const bson_value_t *value)
{
	if (!IsDecimal128Finite(value))
	{
		/* Return false if the number is not finite */
		return false;
	}

	BID_UINT128 bid = GetBIDUINT128FromBsonValue(value);
	_IDEC_flags flag = ALL_EXCEPTION_FLAG_CLEAR;

	/* Try conversion, the below method can only signal invalid in case of overflow because,
	 * we have already checked if number is finite (not infinity and NaN)
	 */
	bid128_to_int64_xint(bid, &flag);
	if (IsOperationInvalid(flag))
	{
		/* Overflowed or underflowed */
		return false;
	}
	return true;
}


/*
 * Checks if a given decimal128 number is in range of Int 32 numbers.
 *
 * Decimal128 is truncated to int32 by removing the decimal part for conversion
 */
bool
IsDecimal128InInt32Range(const bson_value_t *value)
{
	if (!IsDecimal128Finite(value))
	{
		/* Return false if the number is not finite */
		return false;
	}

	BID_UINT128 bid = GetBIDUINT128FromBsonValue(value);
	_IDEC_flags flag = ALL_EXCEPTION_FLAG_CLEAR;

	/* Try conversion, the below method can only signal invalid in case of overflow because,
	 * we have already checked if number is finite (not infinity and NaN)
	 */
	bid128_to_int32_xint(bid, &flag);
	if (IsOperationInvalid(flag))
	{
		/* Overflowed or underflowed */
		return false;
	}
	return true;
}


/*
 * Checks if a given decimal128 number is in range of double numbers.
 */
bool
IsDecimal128InDoubleRange(const bson_value_t *value)
{
	BID_UINT128 bidValue = GetBIDUINT128FromBsonValue(value);
	_IDEC_flags exceptionFlags = ALL_EXCEPTION_FLAG_CLEAR;

	bid128_to_binary64(bidValue, Decimal128RoundingMode_Default, &exceptionFlags);
	if (IsOperationInvalid(exceptionFlags) ||
		IsOperationOverflow(exceptionFlags) ||
		IsOperationUnderflow(exceptionFlags))
	{
		return false;
	}

	return true;
}


/* Return true if decimal128 value can be represented as fixed integer
 * e.g., 10.000023 -> this number can not be represented as fixed integer so return false
 *       10.000000 -> this number can be represented as fixed integer (10) so return true
 */
bool
IsDecimal128AFixedInteger(const bson_value_t *value)
{
	if (value->value_type != BSON_TYPE_DECIMAL128)
	{
		return false;
	}
	bool isOverFlow = false;
	int64_t intval = Decimal128ToInt64Floor(value, &isOverFlow);
	if (isOverFlow)
	{
		return false;
	}

	bson_value_t intBsonval;
	intBsonval.value.v_decimal128 = GetDecimal128FromInt64(intval);
	intBsonval.value_type = BSON_TYPE_DECIMAL128;
	bool isComparisonValid;

	if (CompareBsonDecimal128(value, &intBsonval, &isComparisonValid) == 0)
	{
		return true;
	}

	return false;
}


/* This method compares 2 BSON_TYPE_DECIMAL (left & right) values and return
 * 0 => if equal
 * 1 => if left is greater than left
 * -1 => if left is smaller than right
 */
int
CompareBsonDecimal128(const bson_value_t *left, const bson_value_t *right,
					  bool *isComparisonValid)
{
	_IDEC_flags my_fpsf = ALL_EXCEPTION_FLAG_CLEAR;

	BID_UINT128 leftBid = GetBIDUINT128FromBsonValue(left);
	BID_UINT128 rightBid = GetBIDUINT128FromBsonValue(right);

	bool isEqual = BIDUINT128SignalingEqual(leftBid, rightBid, &my_fpsf);

	/* Checking the flag for invalid comparision, i.e if only one of the operand is NaN then comparision is invalid */
	if (IsOperationInvalid(my_fpsf))
	{
		*isComparisonValid = false;
		return IsDecimal128NaN(left) ? -1 : 1;
	}

	if (isEqual)
	{
		/* Both decimal values are equal */
		*isComparisonValid = true;
		return 0;
	}

	int result = 0;
	my_fpsf = ALL_EXCEPTION_FLAG_CLEAR;

	/* bid128_quiet_greater can only throw invalid exception for SNaNs which is already checked earlier, so we can ignore flag */
	result = bid128_quiet_greater(leftBid, rightBid, &my_fpsf);
	*isComparisonValid = true;
	return result > 0 ? 1 : -1;
}


/*
 * Rounds the decimal value to the closest larger value and stores the outcome in result.
 *
 * EXCEPTION_HANDLING: All the exceptions are ignored and logged because similar test cases don't produce any errors in Mongo Protocol
 */
Decimal128Result
CeilDecimal128Number(const bson_value_t *value, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(value, result,
												   Decimal128MathOperation_Ceil);
}


/*
 * Rounds the decimal value to the closest smaller value and stores the outcome in the result.
 *
 * EXCEPTION_HANDLING: All the exceptions are ignored and logged because similar test cases don't produce any errors in Mongo Protocol
 */
Decimal128Result
FloorDecimal128Number(const bson_value_t *value, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(value, result,
												   Decimal128MathOperation_Floor);
}


/*
 * Calculates the euler exponent and stores the outcome in result.
 *
 * Note: If the result of the exp operation is beyond the range of decimal128, then result is converted to Infinity / -Infinity
 * Range of the decimal128 number is ±0.000000000000000000000000000000000×10e−6143 to ±9.999999999999999999999999999999999×10e6144.
 *
 * EXCEPTION_HANDLING: All the exceptions are ignored and logged because similar test cases don't produce any errors in Mongo Protocol
 */
Decimal128Result
EulerExpDecimal128(const bson_value_t *power, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(power, result,
												   Decimal128MathOperation_Exp);
}


/*
 * Calculates the square root of value and stores the outcome in result.
 *
 * EXCEPTION_HANDLING: All the exceptions are ignored and logged because similar test cases don't produce any errors in Mongo Protocol
 */
Decimal128Result
SqrtDecimal128Number(const bson_value_t *value, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(value, result,
												   Decimal128MathOperation_Sqrt);
}


/*
 * Calculates the absolute value and stores the outcome in result.
 *
 * Note: If the result of the abs operation is beyond the range of decimal128, then result is converted to Infinity / -Infinity
 * Range of the decimal128 number is ±0.000000000000000000000000000000000×10e−6143 to ±9.999999999999999999999999999999999×10e6144.
 *
 * EXCEPTION_HANDLING: All the exceptions are ignored and logged because similar test cases don't produce any errors in Mongo Protocol
 */
Decimal128Result
AbsDecimal128Number(const bson_value_t *value, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(value, result,
												   Decimal128MathOperation_Abs);
}


/*
 * Calculates the logarithm base 10 of value and stores the outcome in result.
 *
 * EXCEPTION_HANDLING: All the exceptions are ignored and logged because similar test cases don't produce any errors in Mongo Protocol
 */
Decimal128Result
Log10Decimal128Number(const bson_value_t *value, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(value, result,
												   Decimal128MathOperation_Log10);
}


/*
 * Calculates the natural logarithm of value and stores the outcome in result.
 *
 * EXCEPTION_HANDLING: All the exceptions are ignored and logged because similar test cases don't produce any errors in Mongo Protocol
 */
Decimal128Result
NaturalLogarithmDecimal128Number(const bson_value_t *value, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(value, result,
												   Decimal128MathOperation_NaturalLogarithm);
}


/*
 * Calculates the log of value to the specified base and stores the outcome in result.
 *
 * Note: If the result of the log operation is beyond the range of decimal128, then result is converted to Infinity / -Infinity
 * Range of the decimal128 number is ±0.000000000000000000000000000000000×10e−6143 to ±9.999999999999999999999999999999999×10e6144.
 *
 * EXCEPTION_HANDLING: All the exceptions are ignored and logged because similar test cases don't produce any errors in Mongo Protocol
 */
Decimal128Result
LogDecimal128Number(const bson_value_t *value, const bson_value_t *base,
					bson_value_t *result)
{
	return Decimal128MathematicalOperation2Operands(value, base, result,
													Decimal128MathOperation_Log);
}


/*
 * Calculates the power of a base to specific exponent and stores the outcome in result.
 *
 * Note: If the result of the pow operation is beyond the range of decimal128, then result is converted to Infinity / -Infinity
 * Range of the decimal128 number is ±0.000000000000000000000000000000000×10e−6143 to ±9.999999999999999999999999999999999×10e6144.
 *
 * EXCEPTION_HANDLING: All the exceptions are ignored and logged because similar test cases don't produce any errors in Mongo Protocol
 */
Decimal128Result
PowDecimal128Number(const bson_value_t *base, const bson_value_t *exponent,
					bson_value_t *result)
{
	return Decimal128MathematicalOperation2Operands(base, exponent, result,
													Decimal128MathOperation_Pow);
}


/*
 * Multiplies 2 BSON_TYPE_DECIMAL128 (x & y) values and stores the outcome in result.
 *
 * Note: If the result of the multiplication operation is beyond the range of decimal128, then result is converted to Infinity / -Infinity
 * Range of the decimal128 number is ±0.000000000000000000000000000000000×10e−6143 to ±9.999999999999999999999999999999999×10e6144.
 *
 * EXCEPTION_HANDLING: All the exceptions are ignored and logged because similar test cases don't produce any errors in Mongo Protocol
 */
Decimal128Result
MultiplyDecimal128Numbers(const bson_value_t *x, const bson_value_t *y,
						  bson_value_t *result)
{
	return Decimal128MathematicalOperation2Operands(x, y, result,
													Decimal128MathOperation_Multiply);
}


/*
 * Adds 2 BSON_TYPE_DECIMAL128 (x & y) values and stores the outcome in result.
 *
 * Note: If the result of the addition operation is beyond the range of decimal128, then result is converted to Infinity / -Infinity
 * Range of the decimal128 number is ±0.000000000000000000000000000000000×10e−6143 to ±9.999999999999999999999999999999999×10e6144.
 *
 * EXCEPTION_HANDLING: All the exceptions are ignored and logged because similar test cases don't produce any errors in Mongo Protocol
 */
Decimal128Result
AddDecimal128Numbers(const bson_value_t *x, const bson_value_t *y, bson_value_t *result)
{
	return Decimal128MathematicalOperation2Operands(x, y, result,
													Decimal128MathOperation_Add);
}


/*
 * Calculates the difference of 2 BSON_TYPE_DECIMAL128 (x & y) values and stores the outcome in result.
 *
 * Note: If the result of the difference operation is beyond the range of decimal128, then result is converted to Infinity / -Infinity
 * Range of the decimal128 number is ±0.000000000000000000000000000000000×10e−6143 to ±9.999999999999999999999999999999999×10e6144.
 *
 * EXCEPTION_HANDLING: All the exceptions are ignored and logged because similar test cases don't produce any errors in Mongo Protocol
 */
Decimal128Result
SubtractDecimal128Numbers(const bson_value_t *x, const bson_value_t *y,
						  bson_value_t *result)
{
	return Decimal128MathematicalOperation2Operands(x, y, result,
													Decimal128MathOperation_Subtract);
}


/*
 * Calculates the division of 2 BSON_TYPE_DECIMAL128 (x & y) values and stores the outcome in result.
 *
 * Note: If the result of the divide operation is beyond the range of decimal128, then result is converted to Infinity / -Infinity
 * Range of the decimal128 number is ±0.000000000000000000000000000000000×10e−6143 to ±9.999999999999999999999999999999999×10e6144.
 *
 * EXCEPTION_HANDLING: All the exceptions are ignored and logged because similar test cases don't produce any errors in Mongo Protocol
 */
Decimal128Result
DivideDecimal128Numbers(const bson_value_t *x, const bson_value_t *y,
						bson_value_t *result)
{
	return Decimal128MathematicalOperation2Operands(x, y, result,
													Decimal128MathOperation_Divide);
}


/*
 * Calculates the modulo of 2 BSON_TYPE_DECIMAL128 (x & y) values and stores the outcome in result.
 *
 * Note: If the result of the mod operation is beyond the range of decimal128, then result is converted to Infinity / -Infinity
 * Range of the decimal128 number is ±0.000000000000000000000000000000000×10e−6143 to ±9.999999999999999999999999999999999×10e6144.
 *
 * EXCEPTION_HANDLING: All the exceptions are ignored and logged because similar test cases don't produce any errors in Mongo Protocol
 */
Decimal128Result
ModDecimal128Numbers(const bson_value_t *x, const bson_value_t *y,
					 bson_value_t *result)
{
	return Decimal128MathematicalOperation2Operands(x, y, result,
													Decimal128MathOperation_Mod);
}


/* Rounds a decimal128 number to the specified precision and sets it to the result value.*/
Decimal128Result
RoundDecimal128Number(const bson_value_t *number, int64_t precision, bson_value_t *result)
{
	return RoundOrTruncateDecimal128Number(number, precision, result,
										   Decimal128MathOperation_Round);
}


/* Truncates a decimal128 number to the specified precision and sets it to the result value.*/
Decimal128Result
TruncDecimal128Number(const bson_value_t *number, int64_t precision, bson_value_t *result)
{
	return RoundOrTruncateDecimal128Number(number, precision, result,
										   Decimal128MathOperation_Trunc);
}


/* Finds the cosine of a decimal128 number and sets it to the result value.*/
Decimal128Result
AcosDecimal128Number(const bson_value_t *number, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(number, result,
												   Decimal128MathOperation_Acos);
}


/* Finds the hyperbolic cosine of a decimal128 number and sets it to the result value.*/
Decimal128Result
AcoshDecimal128Number(const bson_value_t *number, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(number, result,
												   Decimal128MathOperation_Acosh);
}


/* Finds the cosine of a decimal128 number and sets it to the result value.*/
Decimal128Result
AsinDecimal128Number(const bson_value_t *number, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(number, result,
												   Decimal128MathOperation_Asin);
}


/* Finds the hyperbolic cosine of a decimal128 number and sets it to the result value.*/
Decimal128Result
AsinhDecimal128Number(const bson_value_t *number, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(number, result,
												   Decimal128MathOperation_Asinh);
}


/* Finds the cosine of a decimal128 number and sets it to the result value.*/
Decimal128Result
AtanDecimal128Number(const bson_value_t *number, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(number, result,
												   Decimal128MathOperation_Atan);
}


/* Finds the hyperbolic cosine of a decimal128 number and sets it to the result value.*/
Decimal128Result
Atan2Decimal128Numbers(const bson_value_t *x, const bson_value_t *y, bson_value_t *result)
{
	return Decimal128MathematicalOperation2Operands(x, y, result,
													Decimal128MathOperation_Atan2);
}


/* Finds the cosine of a decimal128 number and sets it to the result value.*/
Decimal128Result
AtanhDecimal128Number(const bson_value_t *number, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(number, result,
												   Decimal128MathOperation_Atanh);
}


/* Finds the cosine of a decimal128 number and sets it to the result value.*/
Decimal128Result
CosDecimal128Number(const bson_value_t *number, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(number, result,
												   Decimal128MathOperation_Cos);
}


/* Finds the hyperbolic cosine of a decimal128 number and sets it to the result value.*/
Decimal128Result
CoshDecimal128Number(const bson_value_t *number, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(number, result,
												   Decimal128MathOperation_Cosh);
}


/* Finds the sine of a decimal128 number and sets it to the result value.*/
Decimal128Result
SinDecimal128Number(const bson_value_t *number, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(number, result,
												   Decimal128MathOperation_Sin);
}


/* Finds the hyperbolic sine of a decimal128 number and sets it to the result value*/
Decimal128Result
SinhDecimal128Number(const bson_value_t *number, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(number, result,
												   Decimal128MathOperation_Sinh);
}


/* Finds the tangent of a decimal128 number and sets it to the result value.*/
Decimal128Result
TanDecimal128Number(const bson_value_t *number, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(number, result,
												   Decimal128MathOperation_Tan);
}


/* Finds the hyperbolic tangent of a decimal128 number and sets it to the result value.*/
Decimal128Result
TanhDecimal128Number(const bson_value_t *number, bson_value_t *result)
{
	return Decimal128MathematicalOperation1Operand(number, result,
												   Decimal128MathOperation_Tanh);
}


/* Helper function that rounds or truncates a number to the specified precision and
 * sets it to the result value. If the precision will cause a number greater than 35 digits,
 * we will round it to 35 as that's the maximum digits that can be represented in a decimal128
 * otherwise we would get a NaN result. */
static Decimal128Result
RoundOrTruncateDecimal128Number(const bson_value_t *number, int64_t precision,
								bson_value_t *result, Decimal128MathOperation operation)
{
	BID_UINT128 resultBid;
	BID_UINT128 numberBid = GetBIDUINT128FromBsonValue(number);
	_IDEC_flags exceptionFlag = ALL_EXCEPTION_FLAG_CLEAR;

	Decimal128RoundingMode roundMode = operation == Decimal128MathOperation_Round ?
									   Decimal128RoundingMode_NearestEven :
									   Decimal128RoundingMode_TowardZero;

	if (bid128_isInf(numberBid) || bid128_isNaN(numberBid))
	{
		resultBid = numberBid;
	}
	else if (precision == 0)
	{
		/* This is the default round/trunc */
		resultBid = bid128_round_integral_exact(numberBid, roundMode, &exceptionFlag);
	}
	else
	{
		int32_t exp = 0;
		if (!bid128_isZero(numberBid))
		{
			/* We calculate the exponent which represents the number of digits before
			 * the decimal point, to calculate if the result with the requested precision
			 * fits in 35 decimal digits. */
			_IDEC_flags exceptionFlagIgnore = ALL_EXCEPTION_FLAG_CLEAR;
			BID_UINT128 log10Bid = bid128_log10(bid128_abs(numberBid),
												Decimal128RoundingMode_NearestEven,
												&exceptionFlagIgnore);
			exp = bid128_to_int32_xceil(log10Bid, &exceptionFlagIgnore);

			/* If we have a value != 0.0, the max number of digits including the decimal point we
			 * can return to be represented in a decimal128 is 35. */
			if (exp + precision > 34)
			{
				precision = 34 - exp;
			}
		}

		BID_UINT128 quantY;
		quantY.w[BID_LOW_BITS] = 0x0000000000000001ull;
		quantY.w[BID_HIGH_BITS] = (BID128_EXP_BIAS + (-precision)) << 49;

		exceptionFlag = ALL_EXCEPTION_FLAG_CLEAR;
		resultBid = bid128_quantize(numberBid, quantY,
									roundMode,
									&exceptionFlag);
	}

	result->value_type = BSON_TYPE_DECIMAL128;
	result->value.v_decimal128 = GetBsonDecimal128FromBIDUINT128(&resultBid);

	/* log exception, if any */
	if (!IsOperationSuccess(exceptionFlag))
	{
		/* Log and ignore all exceptions */
		LogWith1Operand("Decimal128 round signalled exception", &numberBid,
						&exceptionFlag);
	}

	return GetDecimal128ResultFromFlag(exceptionFlag);
}


/*
 * Performs the specified mathematical operation on x and y and sets it to result.
 */
static Decimal128Result
Decimal128MathematicalOperation2Operands(const bson_value_t *x, const bson_value_t *y,
										 bson_value_t *result, Decimal128MathOperation
										 operation)
{
	BID_UINT128 zBid;
	BID_UINT128 xBid = GetBIDUINT128FromBsonValue(x);
	BID_UINT128 yBid = GetBIDUINT128FromBsonValue(y);
	_IDEC_flags exceptionFlag = ALL_EXCEPTION_FLAG_CLEAR;

	switch (operation)
	{
		case Decimal128MathOperation_Add:
		{
			zBid = bid128_add(xBid, yBid, Decimal128RoundingMode_NearestEven,
							  &exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Subtract:
		{
			zBid = bid128_sub(xBid, yBid, Decimal128RoundingMode_NearestEven,
							  &exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Multiply:
		{
			zBid = bid128_mul(xBid, yBid, Decimal128RoundingMode_NearestEven,
							  &exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Divide:
		{
			zBid = bid128_div(xBid, yBid, Decimal128RoundingMode_NearestEven,
							  &exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Mod:
		{
			zBid = bid128_fmod(xBid, yBid,
							   &exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Log:
		{
			/* Should do base conversion to calculate the log of a specific base:
			 * LogB(number) = Log10(number) / Log10(base)
			 */
			_IDEC_flags exceptionFlagIgnore = ALL_EXCEPTION_FLAG_CLEAR;
			BID_UINT128 log10NumberBid = bid128_log10(xBid,
													  Decimal128RoundingMode_NearestEven,
													  &exceptionFlagIgnore);

			exceptionFlagIgnore = ALL_EXCEPTION_FLAG_CLEAR;
			BID_UINT128 log10BaseBid = bid128_log10(yBid,
													Decimal128RoundingMode_NearestEven,
													&exceptionFlagIgnore);

			zBid = bid128_div(log10NumberBid, log10BaseBid,
							  Decimal128RoundingMode_NearestEven,
							  &exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Pow:
		{
			BID_UINT128 powerBid = bid128_pow(xBid, yBid, Decimal128RoundingMode_Downward,
											  &exceptionFlag);

			BID_UINT128 zeroAllBits;
			zeroAllBits.w[BID_HIGH_BITS] = 0;
			zeroAllBits.w[BID_LOW_BITS] = 0;

			/* We add 0 to the power result to include all decimal digits in the result,
			 * this is to match mongo. i.e: 10^-2=0.01000000000000000000000000000000000
			 * or 2^2=4.000000000000000000000000000000000. */
			zBid = bid128_add(powerBid, zeroAllBits, Decimal128RoundingMode_NearestEven,
							  &exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Atan2:
		{
			zBid = bid128_atan2(xBid, yBid, Decimal128RoundingMode_NearestEven,
								&exceptionFlag);
			break;
		}

		default:
		{
			ereport(ERROR, (errmsg("Unknown math operator with 2 operands: %d",
								   operation)));
		}
	}

	result->value_type = BSON_TYPE_DECIMAL128;
	result->value.v_decimal128 = GetBsonDecimal128FromBIDUINT128(&zBid);

	/* log exception, if any */
	if (!IsOperationSuccess(exceptionFlag))
	{
		/* Log and ignore all exceptions */
		LogWith2Operands("Decimal128 addition signalled exception", &xBid, &yBid,
						 &exceptionFlag);
	}

	return GetDecimal128ResultFromFlag(exceptionFlag);
}


/*
 * Performs the specified mathematical operation on value and sets it to result.
 */
static Decimal128Result
Decimal128MathematicalOperation1Operand(const bson_value_t *value, bson_value_t *result,
										Decimal128MathOperation operation)
{
	BID_UINT128 resultBid;
	BID_UINT128 valueBid = GetBIDUINT128FromBsonValue(value);
	_IDEC_flags exceptionFlag = ALL_EXCEPTION_FLAG_CLEAR;

	switch (operation)
	{
		case Decimal128MathOperation_Ceil:
		{
			resultBid = bid128_round_integral_positive(valueBid, &exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Floor:
		{
			resultBid = bid128_round_integral_negative(valueBid, &exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Exp:
		{
			resultBid = bid128_exp(valueBid, Decimal128RoundingMode_NearestEven,
								   &exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Sqrt:
		{
			resultBid = bid128_sqrt(valueBid, Decimal128RoundingMode_NearestEven,
									&exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Abs:
		{
			resultBid = bid128_abs(valueBid);
			break;
		}

		case Decimal128MathOperation_Log10:
		{
			resultBid = bid128_log10(valueBid, Decimal128RoundingMode_NearestEven,
									 &exceptionFlag);
			break;
		}

		case Decimal128MathOperation_NaturalLogarithm:
		{
			resultBid = bid128_log(valueBid, Decimal128RoundingMode_NearestEven,
								   &exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Sin:
		{
			resultBid = bid128_sin(valueBid, Decimal128RoundingMode_NearestEven,
								   &exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Cos:
		{
			resultBid = bid128_cos(valueBid, Decimal128RoundingMode_NearestEven,
								   &exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Tan:
		{
			resultBid = bid128_tan(valueBid, Decimal128RoundingMode_NearestEven,
								   &exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Sinh:
		{
			resultBid = bid128_sinh(valueBid, Decimal128RoundingMode_NearestEven,
									&exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Cosh:
		{
			resultBid = bid128_cosh(valueBid, Decimal128RoundingMode_NearestEven,
									&exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Tanh:
		{
			resultBid = bid128_tanh(valueBid, Decimal128RoundingMode_NearestEven,
									&exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Asin:
		{
			resultBid = bid128_asin(valueBid, Decimal128RoundingMode_NearestEven,
									&exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Acos:
		{
			resultBid = bid128_acos(valueBid, Decimal128RoundingMode_NearestEven,
									&exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Atan:
		{
			resultBid = bid128_atan(valueBid, Decimal128RoundingMode_NearestEven,
									&exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Asinh:
		{
			resultBid = bid128_asinh(valueBid, Decimal128RoundingMode_NearestEven,
									 &exceptionFlag);
			break;
		}

		case Decimal128MathOperation_Acosh:
		{
			resultBid = bid128_acosh(valueBid, Decimal128RoundingMode_NearestEven,
									 &exceptionFlag);
			break;
		}


		case Decimal128MathOperation_Atanh:
		{
			resultBid = bid128_atanh(valueBid, Decimal128RoundingMode_NearestEven,
									 &exceptionFlag);
			break;
		}

		default:
		{
			ereport(ERROR, (errmsg("Unknown math operator with 1 operand: %d",
								   operation)));
		}
	}

	result->value_type = BSON_TYPE_DECIMAL128;
	result->value.v_decimal128 = GetBsonDecimal128FromBIDUINT128(&resultBid);

	/* log exception, if any */
	if (!IsOperationSuccess(exceptionFlag))
	{
		/* Log and ignore all exceptions */
		LogWith1Operand("Decimal128 math operation signaled exception", &valueBid,
						&exceptionFlag);
	}

	return GetDecimal128ResultFromFlag(exceptionFlag);
}


/**
 * This method returns the Intel Math Library representation from bson decimal128 representation
 */
static inline BID_UINT128
GetBIDUINT128FromBsonValue(const bson_value_t *decimal)
{
	CheckDecimal128Type(decimal);

	BID_UINT128 bid;
	bid.w[BID_HIGH_BITS] = HIGH_BITS(decimal);
	bid.w[BID_LOW_BITS] = LOW_BITS(decimal);
	return bid;
}


/**
 * This method returns the bson decimal 128 representation from Intel Math Library representation
 *
 */
static inline bson_decimal128_t
GetBsonDecimal128FromBIDUINT128(const BID_UINT128 *bid)
{
	bson_decimal128_t decimal;
	decimal.high = bid->w[BID_HIGH_BITS];
	decimal.low = bid->w[BID_LOW_BITS];
	return decimal;
}


/**
 * Checks if value is decimal128 otherwise throws generic error
 */
static inline void
CheckDecimal128Type(const bson_value_t *value)
{
	if (value->value_type != BSON_TYPE_DECIMAL128)
	{
		ereport(ERROR, (errmsg("Expected Decimal128 value for operation but got %s",
							   BsonTypeName(value->value_type))));
	}
}


/*
 * Logs the message in server logs in the format "{logMessage} | Operands: 1e1 [and 1e2] | Exception: 8"
 * These logs are maintained where Native mongo for the same exception works fine
 */
static void
LogWith2Operands(const char *logMessage, const BID_UINT128 *op1, const BID_UINT128 *op2,
				 const _IDEC_flags *flag)
{
	char bidString[BSON_DECIMAL128_STRING];
	_IDEC_flags ignore = 0;
	bid128_to_string(bidString, *op1, &ignore);

	StringInfo logErrorMsg = makeStringInfo();
	appendStringInfo(logErrorMsg, "%s | Operands: %s", logMessage, bidString);

	if (op2 != NULL)
	{
		char bidString2[BSON_DECIMAL128_STRING];
		ignore = 0;
		bid128_to_string(bidString2, *op2, &ignore);
		appendStringInfo(logErrorMsg, " and %s", bidString2);
	}

	appendStringInfo(logErrorMsg, " | Exception: %d", *flag);

	/* Add to debug logs */
	ereport(DEBUG1, (errmsg("%s", logErrorMsg->data)));
}


/*
 * Logs with 1 operand, useful in case of unary operations e.g: conversions
 */
static void
LogWith1Operand(const char *logMessage, const BID_UINT128 *op1, const _IDEC_flags *flag)
{
	return LogWith2Operands(logMessage, op1, NULL, flag);
}


/*
 * Get the Decimal128Result from exception flag
 */
static
Decimal128Result
GetDecimal128ResultFromFlag(_IDEC_flags flag)
{
	/* Sometimes many operations can signal 2 exception in conjunction with inexact execption
	 * Usually inexact exception is seen to be ignored in the test cases for native mongo.
	 * So we also ignore inexact in case of multiple exception
	 */
	if ((flag & BID_INEXACT_EXCEPTION) && ((flag ^ BID_INEXACT_EXCEPTION) != 0))
	{
		/* If other exceptions along with inexact is present, unset inexact bit to map properly to the Decimal128Result */
		flag = (flag) & (~BID_INEXACT_EXCEPTION);
	}
	switch (flag)
	{
		case ALL_EXCEPTION_FLAG_CLEAR:
		{
			return Decimal128Result_Success;
		}

		case BID_INVALID_EXCEPTION:
		{
			return Decimal128Result_Invalid;
		}

		case BID_INEXACT_EXCEPTION:
		{
			return Decimal128Result_Inexact;
		}

		case BID_OVERFLOW_EXCEPTION:
		{
			return Decimal128Result_Overflow;
		}

		case BID_UNDERFLOW_EXCEPTION:
		{
			return Decimal128Result_Underflow;
		}

		case BID_ZERO_DIVIDE_EXCEPTION:
		{
			return Decimal128Result_DivideByZero;
		}

		case BID_DENORMAL_EXCEPTION:
		{
			return Decimal128Result_Denormal;
		}

		default:
		{
			return Decimal128Result_Unknown;
		}
	}
}


/*
 * Throws conversion failure errors in case when decimal128 values can't be converted to target type
 */
static void
ThrowConversionFailureError(const BID_UINT128 value)
{
	/* Invalid flag can be set for any of the below case:
	 *      1- Numbers is NaN / SNaN
	 *		2- Number is (+/-) Infinity
	 *		3- Converted value cannot fit in the range of int64
	 *
	 */
	if (bid128_isInf(value))
	{
		ereport(ERROR, (errcode(MongoConversionFailure),
						errmsg("Attempt to convert Infinity to integer")));
	}
	else if (bid128_isNaN(value))
	{
		ereport(ERROR, (errcode(MongoConversionFailure),
						errmsg("Attempt to convert NaN value to integer")));
	}
	else
	{
		/* Native mongo shows overflow in both overflow / underflow cases,
		 * also intel math lib doesn't indicate overflow / underflow in exception flag
		 */
		ereport(ERROR, (errcode(MongoConversionFailure),
						errmsg("Conversion would overflow target type")));
	}
}


/**
 *  BIDUINT128SignalingEqual will signal BID_INVALID_EXCEPTION operation in case when NaN numbers are included in comparision.
 *  Returns:
 *  true => when both decimal numbers are equal or both values compared are NaNs
 *  false => when both are not equal or one of the number compared is NaN
 *
 *  Also in case when one number is NaN is comparision "exceptionFlag" is set BID_INVALID_EXCEPTION opeartion
 *
 * Note: This functionality is not directly exposed from library, and the exception is only raised when SNaNs are compared & we wanted the same behaviour for QNaNs (Quiet NaNs)
 * to report invalid comparision of decimal128 numbers
 */
static bool
BIDUINT128SignalingEqual(BID_UINT128 x, BID_UINT128 y, _IDEC_flags *exceptionFlag)
{
	bool isXNaN = bid128_isNaN(x);
	bool isYNaN = bid128_isNaN(y);
	if (isXNaN && isYNaN)
	{
		/* Both NaN's return equals */
		return 1;
	}
	if (isXNaN || isYNaN)
	{
		/* signal invalid exception */
		*exceptionFlag |= BID_INVALID_EXCEPTION;
		return 0;
	}
	return bid128_quiet_equal(x, y, exceptionFlag);
}


/* Convert 128-bit decimal floating-point value to 64-bit signed integer in rounding-down mode */
static int64_t
Decimal128ToInt64Floor(const bson_value_t *value, bool *isOverFlow)
{
	BID_UINT128 bid = GetBIDUINT128FromBsonValue(value);
	_IDEC_flags flag = ALL_EXCEPTION_FLAG_CLEAR;
	int64 int64Value = __bid128_to_int64_floor(bid, &flag);

	if (IsOperationInvalid(flag))
	{
		*isOverFlow = true;
		return 0;
	}

	return int64Value;
}


/* Get the bson_decimal_128 representation value from 64 bit integer */
bson_decimal128_t
GetDecimal128FromInt64(int64_t value)
{
	BID_UINT128 bidValue = __bid128_from_int64(value);
	return GetBsonDecimal128FromBIDUINT128(&bidValue);
}


/**
 * This method accepts additional parameter to decide if quantization would be done or not,
 * this only affects double to decimal128 conversion. This attempts to perform exact conversion from double to dec128
 * which is crucial for other mathematical computations
 *
 * Quantize(X, Y) => Returns a quantized decimal128 which has same numerical value as X and exponent as Y
 */
static bson_decimal128_t
GetBsonValueAsDecimal128Core(const bson_value_t *value, bool shouldQuantizeDouble)
{
	if (!BsonValueIsNumberOrBool(value) && value->value_type != BSON_TYPE_DATE_TIME)
	{
		ereport(ERROR, (errmsg(
							"Expected numeric, boolean or date value for conversion")));
	}
	BID_UINT128 bidValue = { .w = { 0UL, 0UL } };
	switch (value->value_type)
	{
		case BSON_TYPE_INT32:
		{
			bidValue = bid128_from_int32(value->value.v_int32);
			break;
		}

		case BSON_TYPE_INT64:
		{
			bidValue = bid128_from_int64(value->value.v_int64);
			break;
		}

		case BSON_TYPE_BOOL:
		{
			/*
			 * 6176 => 1100000100000
			 * 6176 << 49 => (sign)0 (exponent)01100000100000 (significand)0000000000000000000000000000000000000000000000000
			 *
			 * Now the exponent bits are set to => 01100000100000 which is again 6176
			 *
			 * Finally subtracting the bias/ offset will give => 6176 - 6176 = 0 (exponent)
			 */
			bidValue.w[BID_HIGH_BITS] = (BID128_EXP_BIAS << BID128_EXP_BITS_OFFSET);
			bidValue.w[BID_LOW_BITS] = value->value.v_bool;
			break;
		}

		case BSON_TYPE_DOUBLE:
		{
			_IDEC_flags exceptionFlag = ALL_EXCEPTION_FLAG_CLEAR;

			/* Using the binary64_to_bid128 conversion method from lib fills the additional 19 significands with non zero values.
			 * e.g:
			 * double => 0.999999999999999
			 * decimal128 => 0.9999999999999990007992778373591136 (after conversion)
			 *
			 * So now the approach is taken to quantize the above inexact value to strip the extra digits after 15 significand digits
			 *
			 * Quantize(X, Y) => Returns a quantized decimal128 which has same numerical value as X and exponent as Y
			 * Refer IntelMathLib->README.txt for more detail
			 * Approach:
			 *  1) Perform an inexact conversion to decimal128.
			 *  2) Get the exponent of original double value in base10 lets call this expbase10.
			 *  3) Generate a "Y" for quantize method which is of form 1e<exp> where exp = expbase10 -15 (significands in double)
			 *  4) Quantize
			 *
			 * Case: 9.9e301
			 * binary64_to_bid128 converts to NumberDecimal(9.899999999999999270702923405185988E+301)
			 * Quantize(9.899999999999999270702923405185988E+301, 1e286) => 9.90000000000000
			 *
			 * NOTE: This approach covers almost all the cases, but still we have noticed that in corner cases it might put an extra 0 at the end
			 * So if in any case quantized result has more than 15 digits of precision then we quantize again with 1 less exponent to remove the extra 0
			 */

			/* Step 1 convert to bid128 (this would end up padding non-zero values at the end) */
			BID_UINT128 bid128Inexact = binary64_to_bid128(value->value.v_double,
														   Decimal128RoundingMode_Default,
														   &exceptionFlag);

			if (bid128_isInf(bid128Inexact) || bid128_isNaN(bid128Inexact) ||
				!shouldQuantizeDouble)
			{
				/* No need to quantize if the value is Infinity or NaN */
				bidValue = bid128Inexact;
			}
			else
			{
				/* Step 2 get the exponent of actual double value in base10 */
				double doubleVal = value->value.v_double;
				int exp = (doubleVal == 0.0) ? 0 : (int) ceil(log10(fabs(doubleVal)));

				/*
				 * Step 3 Get a "y" BID_UINT128 value with exponent that we need after quantization. This is generally exp - 15 (significand double supports)
				 */
				BID_UINT128 quantY;
				quantY.w[BID_LOW_BITS] = 0x0000000000000001ull;

				/*
				 * To set the exponent directly left shift 49 places, because first 14 bits after the sign represent exponent for our case
				 * https://en.wikipedia.org/wiki/Decimal128_floating-point_format#:~:text=single%20byte%20value.-,Binary%20integer%20significand%20field,-%5Bedit%5D
				 */
				quantY.w[BID_HIGH_BITS] =
					(BID128_EXP_BIAS + (exp - 15)) << BID128_EXP_BITS_OFFSET;

				/* Step 3 Quantize */
				exceptionFlag = ALL_EXCEPTION_FLAG_CLEAR;
				bidValue = bid128_quantize(bid128Inexact, quantY,
										   Decimal128RoundingMode_Default,
										   &exceptionFlag);

				/* If quantized result has more than 15 precision digit again quantize with one less digit */
				if (bidValue.w[BID_LOW_BITS] > (1E15 - 1))
				{
					exceptionFlag = ALL_EXCEPTION_FLAG_CLEAR;
					quantY.w[BID_HIGH_BITS] = (BID128_EXP_BIAS + (exp - 14)) << 49;
					bidValue = bid128_quantize(bid128Inexact, quantY,
											   Decimal128RoundingMode_Default,
											   &exceptionFlag);
				}
			}

			/* Above conversion can signal inexact and invalid exception and both are ignored when compared to native mongo */
			if (!IsOperationSuccess(exceptionFlag))
			{
				LogWith1Operand("Decimal128 conversion from double signalled exception",
								&bidValue, &exceptionFlag);
			}
			break;
		}

		case BSON_TYPE_DATE_TIME:
		{
			bidValue = bid128_from_int64(value->value.v_datetime);
			break;
		}

		case BSON_TYPE_DECIMAL128:
		{
			return value->value.v_decimal128;
		}

		case BSON_TYPE_UTF8:
		{
			/* This is handled by libbson api */
			bson_decimal128_t dec128;
			bson_decimal128_from_string(value->value.v_utf8.str, &dec128);
			return dec128;
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
							errmsg("Unexpected type")));
		}
	}
	return GetBsonDecimal128FromBIDUINT128(&bidValue);
}
