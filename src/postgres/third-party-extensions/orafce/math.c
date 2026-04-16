#include "postgres.h"

#include <math.h>

#include "funcapi.h"
#include "fmgr.h"
#include "utils/numeric.h"
#include "utils/builtins.h"


#include "orafce.h"
#include "builtins.h"

PG_FUNCTION_INFO_V1(orafce_reminder_smallint);
PG_FUNCTION_INFO_V1(orafce_reminder_int);
PG_FUNCTION_INFO_V1(orafce_reminder_bigint);
PG_FUNCTION_INFO_V1(orafce_reminder_numeric);

/*
 * CREATE OR REPLACE FUNCTION oracle.remainder(smallint, smallint)
 * RETURNS smallint
 */
Datum
orafce_reminder_smallint(PG_FUNCTION_ARGS)
{
	int16		arg1 = PG_GETARG_INT16(0);
	int16		arg2 = PG_GETARG_INT16(1);

	if (arg2 == 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_DIVISION_BY_ZERO),
				 errmsg("division by zero")));
	}

	if (arg2 == -1)
		PG_RETURN_INT16(0);

	PG_RETURN_INT16(arg1 - ((int16) round(((double) arg1) / ((double) arg2)) * arg2));
}

/*
 * CREATE OR REPLACE FUNCTION oracle.remainder(int, int)
 * RETURNS int
 */
Datum
orafce_reminder_int(PG_FUNCTION_ARGS)
{
	int32		arg1 = PG_GETARG_INT32(0);
	int32		arg2 = PG_GETARG_INT32(1);

	if (arg2 == 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_DIVISION_BY_ZERO),
				 errmsg("division by zero")));
	}

	if (arg2 == -1)
		PG_RETURN_INT32(0);

	PG_RETURN_INT32(arg1 - ((int32) round(((double) arg1) / ((double) arg2)) * arg2));
}

/*
 * CREATE OR REPLACE FUNCTION oracle.remainder(bigint, bigint)
 * RETURNS bigint
 */
Datum
orafce_reminder_bigint(PG_FUNCTION_ARGS)
{
	int64		arg1 = PG_GETARG_INT64(0);
	int64		arg2 = PG_GETARG_INT64(1);

	if (arg2 == 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_DIVISION_BY_ZERO),
				 errmsg("division by zero")));
	}

	if (arg2 == -1)
		PG_RETURN_INT32(0);

	PG_RETURN_INT64(arg1 - ((int64) round(((long double) arg1) / ((long double) arg2)) * arg2));
}

/*
 * This will handle NaN and Infinity cases
 */
static Numeric
duplicate_numeric(Numeric num)
{
	Numeric		res;

	res = (Numeric) palloc(VARSIZE(num));
	memcpy(res, num, VARSIZE(num));
	return res;
}

static Numeric
get_numeric_in(const char *str)
{
	return DatumGetNumeric(
			  DirectFunctionCall3(numeric_in,
								  CStringGetDatum(str),
								  ObjectIdGetDatum(0),
								  Int32GetDatum(-1)));
}

static bool
orafce_numeric_is_inf(Numeric num)
{

#if PG_VERSION_NUM >= 140000

	return numeric_is_inf(num);

#else

	/* older releases doesn't support +-Infinitity in numeric type */

	return false;

#endif

}

/*
 * CREATE OR REPLACE FUNCTION oracle.remainder(numeric, numeric)
 * RETURNS numeric
 */
Datum
orafce_reminder_numeric(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);
	Numeric		result;
	float8		val2;

	if (numeric_is_nan(num1))
		duplicate_numeric(num1);
	if (numeric_is_nan(num2))
		duplicate_numeric(num2);

	val2 = DatumGetFloat8(DirectFunctionCall1(numeric_float8, NumericGetDatum(num2)));

	if (val2 == 0)
		ereport(ERROR,
				(errcode(ERRCODE_DIVISION_BY_ZERO),
				 errmsg("division by zero")));

	if (orafce_numeric_is_inf(num1))
		PG_RETURN_NUMERIC(get_numeric_in("NaN"));

	if (orafce_numeric_is_inf(num2))
		duplicate_numeric(num1);

#if PG_VERSION_NUM >= 150000

	result = numeric_sub_opt_error(
				num1,
				numeric_mul_opt_error(
					DatumGetNumeric(
						DirectFunctionCall2(
							numeric_round,
							NumericGetDatum(
								numeric_div_opt_error(num1, num2,NULL)),
							Int32GetDatum(0))),
					num2,
					NULL),
				NULL);

#else

	result = DatumGetNumeric(
			DirectFunctionCall2(numeric_sub,
				NumericGetDatum(num1),
				DirectFunctionCall2(numeric_mul,
					DirectFunctionCall2(numeric_round,
						DirectFunctionCall2(numeric_div,
											NumericGetDatum(num1),
											NumericGetDatum(num2)),
						Int32GetDatum(0)),
					NumericGetDatum(num2))));

#endif

	PG_RETURN_NUMERIC(result);
}
