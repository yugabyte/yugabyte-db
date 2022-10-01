#include "postgres.h"
#include "funcapi.h"
#include "fmgr.h"

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
	PG_RETURN_NULL();
}

/*
 * CREATE OR REPLACE FUNCTION oracle.remainder(int, int)
 * RETURNS smallint
 */
Datum
orafce_reminder_int(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}

/*
 * CREATE OR REPLACE FUNCTION oracle.remainder(bigint, bigint)
 * RETURNS bigint
 */
Datum
orafce_reminder_bigint(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}

/*
 * CREATE OR REPLACE FUNCTION oracle.remainder(numeric, numeric)
 * RETURNS numeric
 */
Datum
orafce_reminder_numeric(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}
