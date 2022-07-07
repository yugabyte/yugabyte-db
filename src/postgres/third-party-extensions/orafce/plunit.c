/*
 * This API is subset plunit lib with http://www.apollo-pro.com/help/pl_unit_assertions.htm
 *
 */

#include "postgres.h"

#include <math.h>
#include "funcapi.h"

#include "catalog/pg_collation.h"
#include "parser/parse_oper.h"
#include "utils/builtins.h"
#include "orafce.h"
#include "builtins.h"

PG_FUNCTION_INFO_V1(plunit_assert_true);
PG_FUNCTION_INFO_V1(plunit_assert_true_message);
PG_FUNCTION_INFO_V1(plunit_assert_false);
PG_FUNCTION_INFO_V1(plunit_assert_false_message);
PG_FUNCTION_INFO_V1(plunit_assert_null);
PG_FUNCTION_INFO_V1(plunit_assert_null_message);
PG_FUNCTION_INFO_V1(plunit_assert_not_null);
PG_FUNCTION_INFO_V1(plunit_assert_not_null_message);
PG_FUNCTION_INFO_V1(plunit_assert_equals);
PG_FUNCTION_INFO_V1(plunit_assert_equals_message);
PG_FUNCTION_INFO_V1(plunit_assert_equals_range);
PG_FUNCTION_INFO_V1(plunit_assert_equals_range_message);
PG_FUNCTION_INFO_V1(plunit_assert_not_equals);
PG_FUNCTION_INFO_V1(plunit_assert_not_equals_message);
PG_FUNCTION_INFO_V1(plunit_assert_not_equals_range);
PG_FUNCTION_INFO_V1(plunit_assert_not_equals_range_message);
PG_FUNCTION_INFO_V1(plunit_fail);
PG_FUNCTION_INFO_V1(plunit_fail_message);

static bool assert_equals_base(FunctionCallInfo fcinfo);
static bool assert_equals_range_base(FunctionCallInfo fcinfo);
static char *assert_get_message(FunctionCallInfo fcinfo, int nargs, char *default_message);


/****************************************************************
 * plunit.assert_true
 * plunit.assert_true_message
 *
 * Syntax:
 *   PROCEDURE assert_true(condition boolean, message varchar default '');
 *
 * Purpouse:
 *   Asserts that the condition is true.  The optional message will be
 *   displayed if the assertion fails.  If not supplied, a default message
 *   is displayed.
 *
 ****************************************************************/
Datum
plunit_assert_true(PG_FUNCTION_ARGS)
{
	return plunit_assert_true_message(fcinfo);
}

Datum
plunit_assert_true_message(PG_FUNCTION_ARGS)
{
	char	*message = assert_get_message(fcinfo, 2, "plunit.assert_true exception");
	bool condition = PG_GETARG_BOOL(0);

	if (PG_ARGISNULL(0) || !condition)
		ereport(ERROR,
				(errcode(ERRCODE_CHECK_VIOLATION),
				 errmsg("%s", message),
				 errdetail("Plunit.assertation fails (assert_true).")));

	PG_RETURN_VOID();
}

/****************************************************************
 * plunit.assert_false
 * plunit.assert_false_message
 *
 * Syntax:
 *   PROCEDURE assert_false(condition boolean, message varchar default '');
 *
 * Purpouse:
 *   Asserts that the condition is false.  The optional message will be
 *   displayed if the assertion fails.  If not supplied, a default message
 *   is displayed.
 *
 ****************************************************************/
Datum
plunit_assert_false(PG_FUNCTION_ARGS)
{
	return plunit_assert_false_message(fcinfo);
}

Datum
plunit_assert_false_message(PG_FUNCTION_ARGS)
{
	char	*message = assert_get_message(fcinfo, 2, "plunit.assert_false exception");
	bool condition = PG_GETARG_BOOL(0);

	if (PG_ARGISNULL(0) || condition)
		ereport(ERROR,
				(errcode(ERRCODE_CHECK_VIOLATION),
				 errmsg("%s", message),
				 errdetail("Plunit.assertation fails (assert_false).")));

	PG_RETURN_VOID();
}

/****************************************************************
 * plunit.assert_null
 * plunit.assert_null_message
 *
 * Syntax:
 *   PROCEDURE assert_null(actual anyelement, message varchar default '');
 *
 * Purpouse:
 *   Asserts that the actual is null.  The optional message will be
 *   displayed if the assertion fails.  If not supplied, a default message
 *   is displayed.
 *
 ****************************************************************/
Datum
plunit_assert_null(PG_FUNCTION_ARGS)
{
	return plunit_assert_null_message(fcinfo);
}

Datum
plunit_assert_null_message(PG_FUNCTION_ARGS)
{
	char	*message = assert_get_message(fcinfo, 2, "plunit.assert_null exception");

	if (!PG_ARGISNULL(0))
		ereport(ERROR,
				(errcode(ERRCODE_CHECK_VIOLATION),
				 errmsg("%s", message),
				 errdetail("Plunit.assertation fails (assert_null).")));

	PG_RETURN_VOID();
}

/****************************************************************
 * plunit.assert_not_null
 * plunit.assert_not_null_message
 *
 * Syntax:
 *   PROCEDURE assert_not_null(actual anyelement, message varchar default '');
 *
 * Purpouse:
 *   Asserts that the actual isn't null.  The optional message will be
 *   displayed if the assertion fails.  If not supplied, a default message
 *   is displayed.
 *
 ****************************************************************/
Datum
plunit_assert_not_null(PG_FUNCTION_ARGS)
{
	return plunit_assert_not_null_message(fcinfo);
}

Datum
plunit_assert_not_null_message(PG_FUNCTION_ARGS)
{
	char	*message = assert_get_message(fcinfo, 2, "plunit.assert_not_null exception");

	if (PG_ARGISNULL(0))
		ereport(ERROR,
				(errcode(ERRCODE_CHECK_VIOLATION),
				 errmsg("%s", message),
				 errdetail("Plunit.assertation fails (assert_not_null).")));

	PG_RETURN_VOID();
}


/****************************************************************
 * plunit.assert_equals
 * plunit.assert_equals_message
 * plunit.assert_equals_range
 * plunit.assert_equals_range_message
 *
 * Syntax:
 *   PROCEDURE assert_equals(expected anyelement,actual anyelement,
 *                           message varchar default '');
 *   PROCEDURE assert_equals(expected double precision, actual double precision,
 *                           range double precision, message varchar default '');
 *
 * Purpouse:
 *    Asserts that expected and actual are equal.  The optional message will be
 *    displayed if the assertion fails.  If not supplied, a default
 *    message is displayed.
 *    Asserts that expected and actual are within the specified range.
 *    The optional message will be displayed if the assertion fails.
 *    If not supplied, a default message is displayed.
 *
 ****************************************************************/
static char *
assert_get_message(FunctionCallInfo fcinfo, int nargs, char *message)
{
	char *result;

	if (PG_NARGS() == nargs)
	{
		text	*msg;

		if (PG_ARGISNULL(nargs - 1))
			ereport(ERROR,
					(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
					 errmsg("message is NULL"),
					 errdetail("Message may not be NULL.")));

		msg = PG_GETARG_TEXT_P(nargs - 1);
		result = text_to_cstring(msg);
	}
	else
		result = message;

	return result;
}


static bool
assert_equals_base(FunctionCallInfo fcinfo)
{
	Datum 		value1 = PG_GETARG_DATUM(0);
	Datum		value2 = PG_GETARG_DATUM(1);
	Oid		*ptr;

	ptr = (Oid *) fcinfo->flinfo->fn_extra;
	if (ptr == NULL)
	{
		Oid	  valtype = get_fn_expr_argtype(fcinfo->flinfo, 0);
		Oid eqopfcid;

		if (!OidIsValid(valtype))
			elog(ERROR, "could not determine data type of input");

		eqopfcid = equality_oper_funcid(valtype);

		if (!OidIsValid(eqopfcid))
			ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("unknown equal operand for datatype")));

		/* First time calling for current query: allocate storage */
		fcinfo->flinfo->fn_extra = MemoryContextAlloc(fcinfo->flinfo->fn_mcxt,
										    sizeof(Oid));
		ptr = (Oid *) fcinfo->flinfo->fn_extra;
		*ptr = eqopfcid;
	}

	return DatumGetBool(OidFunctionCall2Coll(*ptr, DEFAULT_COLLATION_OID, value1, value2));
}

Datum
plunit_assert_equals(PG_FUNCTION_ARGS)
{
	return plunit_assert_equals_message(fcinfo);
}

Datum
plunit_assert_equals_message(PG_FUNCTION_ARGS)
{
	char *message = assert_get_message(fcinfo, 3, "plunit.assert_equal exception");

	/* skip all tests for NULL value */
	if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
		ereport(ERROR,
				(errcode(ERRCODE_CHECK_VIOLATION),
				 errmsg("%s", message),
				 errdetail("Plunit.assertation fails (assert_equals).")));

	if (!assert_equals_base(fcinfo))
		ereport(ERROR,
				(errcode(ERRCODE_CHECK_VIOLATION),
				 errmsg("%s", message),
				 errdetail("Plunit.assertation fails (assert_equals).")));

	PG_RETURN_VOID();
}

Datum
plunit_assert_equals_range(PG_FUNCTION_ARGS)
{
	return plunit_assert_equals_range_message(fcinfo);
}

static bool
assert_equals_range_base(FunctionCallInfo fcinfo)
{
	float8	expected_value;
	float8	actual_value;
	float8	range_value;

        range_value = PG_GETARG_FLOAT8(2);
	if (range_value < 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("cannot set range to negative number")));

	expected_value = PG_GETARG_FLOAT8(0);
	actual_value = PG_GETARG_FLOAT8(1);

	return fabs(expected_value - actual_value) < range_value;
}

Datum
plunit_assert_equals_range_message(PG_FUNCTION_ARGS)
{
	char *message = assert_get_message(fcinfo, 4, "plunit.assert_equal exception");

	/* skip all tests for NULL value */
	if (PG_ARGISNULL(0) || PG_ARGISNULL(1) || PG_ARGISNULL(2))
		ereport(ERROR,
				(errcode(ERRCODE_CHECK_VIOLATION),
				 errmsg("%s", message),
				 errdetail("Plunit.assertation fails (assert_equals).")));

	if (!assert_equals_range_base(fcinfo))
		ereport(ERROR,
				(errcode(ERRCODE_CHECK_VIOLATION),
				 errmsg("%s", message),
				 errdetail("Plunit.assertation fails (assert_equals).")));

	PG_RETURN_VOID();
}


/****************************************************************
 * plunit.assert_not_equals
 * plunit.assert_not_equals_message
 * plunit.assert_not_equals_range
 * plunit.assert_not_equals_range_message
 *
 * Syntax:
 *   PROCEDURE assert_not_equals(expected anyelement,actual anyelement,
 *                           message varchar default '');
 *   PROCEDURE assert_not_equals(expected double precision, expected double precision,
 *                           range double precision, message varchar default '');
 *
 * Purpouse:
 *    Asserts that expected and actual are equal.  The optional message will be
 *    displayed if the assertion fails.  If not supplied, a default
 *    message is displayed.
 *    Asserts that expected and actual are within the specified range.
 *    The optional message will be displayed if the assertion fails.
 *    If not supplied, a default message is displayed.
 *
 ****************************************************************/
Datum
plunit_assert_not_equals(PG_FUNCTION_ARGS)
{
	return plunit_assert_not_equals_message(fcinfo);
}

Datum
plunit_assert_not_equals_message(PG_FUNCTION_ARGS)
{
	char *message = assert_get_message(fcinfo, 3, "plunit.assert_not_equal exception");

	/* skip all tests for NULL value */
	if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
		ereport(ERROR,
				(errcode(ERRCODE_CHECK_VIOLATION),
				 errmsg("%s", message),
				 errdetail("Plunit.assertation fails (assert_not_equals).")));

	if (assert_equals_base(fcinfo))
		ereport(ERROR,
				(errcode(ERRCODE_CHECK_VIOLATION),
				 errmsg("%s", message),
				 errdetail("Plunit.assertation fails (assert_not_equals).")));

	PG_RETURN_VOID();
}

Datum
plunit_assert_not_equals_range(PG_FUNCTION_ARGS)
{
	return plunit_assert_not_equals_range_message(fcinfo);
}

Datum
plunit_assert_not_equals_range_message(PG_FUNCTION_ARGS)
{
	char *message = assert_get_message(fcinfo, 4, "plunit.assert_not_equal exception");

	/* skip all tests for NULL value */
	if (PG_ARGISNULL(0) || PG_ARGISNULL(1) || PG_ARGISNULL(2))
		ereport(ERROR,
				(errcode(ERRCODE_CHECK_VIOLATION),
				 errmsg("%s", message),
				 errdetail("Plunit.assertation fails (assert_not_equals).")));

	if (assert_equals_range_base(fcinfo))
		ereport(ERROR,
				(errcode(ERRCODE_CHECK_VIOLATION),
				 errmsg("%s", message),
				 errdetail("Plunit.assertation fails (assert_not_equals).")));

	PG_RETURN_VOID();
}

/****************************************************************
 * plunit.fail
 * plunit.fail_message
 *
 * Syntax:
 *   PROCEDURE fail(message varchar default '');
 *
 * Purpouse:
 *    Fail can be used to cause a test procedure to fail
 *    immediately using the supplied message.
 *
 ****************************************************************/

Datum
plunit_fail(PG_FUNCTION_ARGS)
{
	return plunit_fail_message(fcinfo);
}

Datum
plunit_fail_message(PG_FUNCTION_ARGS)
{
	char *message = assert_get_message(fcinfo, 1, "plunit.assert_fail exception");

	ereport(ERROR,
				(errcode(ERRCODE_CHECK_VIOLATION),
				 errmsg("%s", message),
				 errdetail("Plunit.assertation (assert_fail).")));

	PG_RETURN_VOID();
}

