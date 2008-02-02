#include "postgres.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/numeric.h"
#include "utils/pg_locale.h"

#include "orafunc.h"


Datum orafce_to_char_int4(PG_FUNCTION_ARGS);
Datum orafce_to_char_int8(PG_FUNCTION_ARGS);
Datum orafce_to_char_float4(PG_FUNCTION_ARGS);
Datum orafce_to_char_float8(PG_FUNCTION_ARGS);
Datum orafce_to_char_numeric(PG_FUNCTION_ARGS);
Datum orafce_to_number(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(orafce_to_char_int4);
PG_FUNCTION_INFO_V1(orafce_to_char_int8);
PG_FUNCTION_INFO_V1(orafce_to_char_float4);
PG_FUNCTION_INFO_V1(orafce_to_char_float8);
PG_FUNCTION_INFO_V1(orafce_to_char_numeric);
PG_FUNCTION_INFO_V1(orafce_to_number);


Datum
orafce_to_char_int4(PG_FUNCTION_ARGS)
{
	int32		arg0 = PG_GETARG_INT32(0);
	StringInfo	buf = makeStringInfo();

	appendStringInfo(buf, "%d", arg0);

	PG_RETURN_TEXT_P(CStringGetTextP(buf->data));
}


Datum
orafce_to_char_int8(PG_FUNCTION_ARGS)
{
	int64		arg0 = PG_GETARG_INT64(0);
	StringInfo	buf = makeStringInfo();

	appendStringInfo(buf, INT64_FORMAT, arg0);

	PG_RETURN_TEXT_P(CStringGetTextP(buf->data));
}


Datum
orafce_to_char_float4(PG_FUNCTION_ARGS)
{
	float4		arg0 = PG_GETARG_FLOAT4(0);
	StringInfo	buf = makeStringInfo();
	struct lconv *lconv = PGLC_localeconv();
	char	   *p;

	appendStringInfo(buf, "%f", arg0);

	for (p = buf->data; *p; p++)
		if (*p == '.')
			*p = lconv->decimal_point[0];

	PG_RETURN_TEXT_P(CStringGetTextP(buf->data));
}


Datum
orafce_to_char_float8(PG_FUNCTION_ARGS)
{
	float8		arg0 = PG_GETARG_FLOAT8(0);
	StringInfo	buf = makeStringInfo();
	struct lconv *lconv = PGLC_localeconv();
	char	   *p;

	appendStringInfo(buf, "%f", arg0);

	for (p = buf->data; *p; p++)
		if (*p == '.')
			*p = lconv->decimal_point[0];

	PG_RETURN_TEXT_P(CStringGetTextP(buf->data));
}


Datum
orafce_to_char_numeric(PG_FUNCTION_ARGS)
{
	Numeric		arg0 = PG_GETARG_NUMERIC(0);
	StringInfo	buf = makeStringInfo();
	struct lconv *lconv = PGLC_localeconv();
	char	   *p;

	appendStringInfoString(buf, DatumGetCString(DirectFunctionCall1(numeric_out, NumericGetDatum(arg0))));

	for (p = buf->data; *p; p++)
		if (*p == '.')
			*p = lconv->decimal_point[0];

	PG_RETURN_TEXT_P(CStringGetTextP(buf->data));
}


Datum
orafce_to_number(PG_FUNCTION_ARGS)
{
	text	   *arg0 = PG_GETARG_TEXT_P(0);
	char	   *buf;
	struct lconv *lconv = PGLC_localeconv();
	Numeric		res;
	char	   *p;

	buf = DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(arg0)));

	for (p = buf; *p; p++)
		if (*p == lconv->decimal_point[0] && lconv->decimal_point[0])
			*p = '.';
		else if (*p == lconv->thousands_sep[0] && lconv->thousands_sep[0])
			*p = ',';

	res = DatumGetNumeric(DirectFunctionCall3(numeric_in, CStringGetDatum(buf), 0, -1));

	PG_RETURN_NUMERIC(res);
}
