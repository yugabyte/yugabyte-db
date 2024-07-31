/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/decimal128.h
 *
 * The Decimal 128 support.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_DECIMAL_128_H
#define BSON_DECIMAL_128_H

#include <postgres.h>

#include "io/helio_bson_core.h"

/* Enum representing the decimal128 operation result state, these results try to map exception flag to enum and most of the case native mongo ignores the exception
 * But still decimal 128 operation should return Decimal128Result in order to do result specific task if needed in future
 */
typedef enum Decimal128Result
{
	/* Represents that the result is valid and operation is successful */
	Decimal128Result_Success = 1,

	/* Result represents operation is invalid, e.g: NaN is compared to other valid numbers */
	Decimal128Result_Invalid = 2,

	/* Result of the operation overflowed, i.e. resulted in Infinity */
	Decimal128Result_Overflow = 3,

	/* Result of the operation underflowes, i.e. resulted in -Infinity */
	Decimal128Result_Underflow = 4,

	/* Result represents operation attempted divide by zero */
	Decimal128Result_DivideByZero = 5,

	/* Result is not exact, meaning more than 34 significands were present after operation and after rounding result is not exactly same */
	Decimal128Result_Inexact = 6,

	/* Result is denormalized / subnormal number. Normally floating point numbers are represented in normalized form i.e. point after the first non-zero number */
	/* e.g. 1.2e-3 but when exponent is out of range and the number is underflowed then it can't be converted to normalized form because of leading zeroes */
	/* e.g. 1.2e-310 would be a denormal number for 64 bit floating point numbers */
	/* Further Referrence: https://en.wikipedia.org/wiki/Subnormal_number */
	Decimal128Result_Denormal = 7,

	/* Result has multiple exception flags set from which the result outcome is not clear */
	Decimal128Result_Unknown = 8
} Decimal128Result;

/* ========================*/
/* Type conversion methods */
/* ========================*/

int32_t GetBsonDecimal128AsInt32(const bson_value_t *value,
								 ConversionRoundingMode roundingMode);
int64_t GetBsonDecimal128AsInt64(const bson_value_t *value,
								 ConversionRoundingMode roundingMode);
double GetBsonDecimal128AsDouble(const bson_value_t *value);
double GetBsonDecimal128AsDoubleQuiet(const bson_value_t *value);
long double GetBsonDecimal128AsLongDouble(const bson_value_t *value);
bool GetBsonDecimal128AsBool(const bson_value_t *value);
char * GetBsonDecimal128AsString(const bson_value_t *value);
bson_decimal128_t GetBsonValueAsDecimal128(const bson_value_t *value);
bson_decimal128_t GetBsonValueAsDecimal128Quantized(const bson_value_t *value);

/* ========= */
/* Utilities */
/* ========= */

bool IsDecimal128InInt64Range(const bson_value_t *value);
bool IsDecimal128InInt32Range(const bson_value_t *value);
bool IsDecimal128InDoubleRange(const bson_value_t *value);
bool IsDecimal128NaN(const bson_value_t *value);
bool IsDecimal128Infinity(const bson_value_t *value);
bool IsDecimal128Finite(const bson_value_t *value);
bool IsDecimal128AFixedInteger(const bson_value_t *value);
bool IsDecimal128Zero(const bson_value_t *value);
void SetDecimal128Zero(bson_value_t *value);

/* ================================== */
/* Mathematical & logical operations  */
/* ================================== */

int CompareBsonDecimal128(const bson_value_t *left, const bson_value_t *right,
						  bool *isComparisonValid);
Decimal128Result MultiplyDecimal128Numbers(const bson_value_t *x, const bson_value_t *y,
										   bson_value_t *result);
Decimal128Result AddDecimal128Numbers(const bson_value_t *x, const bson_value_t *y,
									  bson_value_t *result);
Decimal128Result SubtractDecimal128Numbers(const bson_value_t *x, const bson_value_t *y,
										   bson_value_t *result);
Decimal128Result DivideDecimal128Numbers(const bson_value_t *x, const bson_value_t *y,
										 bson_value_t *result);
Decimal128Result ModDecimal128Numbers(const bson_value_t *x, const bson_value_t *y,
									  bson_value_t *result);
Decimal128Result CeilDecimal128Number(const bson_value_t *value, bson_value_t *result);
Decimal128Result FloorDecimal128Number(const bson_value_t *value, bson_value_t *result);
Decimal128Result EulerExpDecimal128(const bson_value_t *power, bson_value_t *result);
Decimal128Result SqrtDecimal128Number(const bson_value_t *value, bson_value_t *result);
Decimal128Result AbsDecimal128Number(const bson_value_t *value, bson_value_t *result);
Decimal128Result Log10Decimal128Number(const bson_value_t *value, bson_value_t *result);
Decimal128Result NaturalLogarithmDecimal128Number(const bson_value_t *value,
												  bson_value_t *result);
Decimal128Result LogDecimal128Number(const bson_value_t *value, const bson_value_t *base,
									 bson_value_t *result);
Decimal128Result PowDecimal128Number(const bson_value_t *base,
									 const bson_value_t *exponent, bson_value_t *result);
Decimal128Result RoundDecimal128Number(const bson_value_t *number, int64_t precision,
									   bson_value_t *result);
Decimal128Result TruncDecimal128Number(const bson_value_t *number, int64_t precision,
									   bson_value_t *result);
Decimal128Result SinDecimal128Number(const bson_value_t *value, bson_value_t *result);
Decimal128Result CosDecimal128Number(const bson_value_t *value, bson_value_t *result);
Decimal128Result TanDecimal128Number(const bson_value_t *value, bson_value_t *result);
Decimal128Result SinhDecimal128Number(const bson_value_t *value, bson_value_t *result);
Decimal128Result CoshDecimal128Number(const bson_value_t *value, bson_value_t *result);
Decimal128Result TanhDecimal128Number(const bson_value_t *value, bson_value_t *result);
Decimal128Result AsinDecimal128Number(const bson_value_t *value, bson_value_t *result);
Decimal128Result AcosDecimal128Number(const bson_value_t *value, bson_value_t *result);
Decimal128Result AtanDecimal128Number(const bson_value_t *value, bson_value_t *result);
Decimal128Result Atan2Decimal128Numbers(const bson_value_t *x, const bson_value_t *y,
										bson_value_t *result);
Decimal128Result AsinhDecimal128Number(const bson_value_t *value, bson_value_t *result);
Decimal128Result AcoshDecimal128Number(const bson_value_t *value, bson_value_t *result);
Decimal128Result AtanhDecimal128Number(const bson_value_t *value, bson_value_t *result);
#endif
