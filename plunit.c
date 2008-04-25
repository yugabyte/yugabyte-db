/* 
 * This API is subset plunit lib with http://www.apollo-pro.com/help/pl_unit_assertions.htm
 *
 */

#include "postgres.h"
#include "funcapi.h"

Datum plunit_assert_true(PG_FUNCTION_ARGS);
Datum plunit_assert_true_message(PG_FUNCTION_ARGS);
Datum plunit_assert_false(PG_FUNCTION_ARGS);
Datum plunit_assert_false_message(PG_FUNCTION_ARGS);
Datum plunit_assert_null(PG_FUNCTION_ARGS);
Datum plunit_assert_null_message(PG_FUNCTION_ARGS);
Datum plunit_assert_not_null(PG_FUNCTION_ARGS);
Datum plunit_assert_not_null_message(PG_FUNCTION_ARGS);
Datum plunit_assert_equals(PG_FUNCTION_ARGS);
Datum plunit_assert_equals_message(PG_FUNCTION_ARGS);
Datum plunit_assert_equals_range(PG_FUNCTION_ARGS);
Datum plunit_assert_equals_range_message(PG_FUNCTION_ARGS);
Datum plunit_assert_not_equals(PG_FUNCTION_ARGS);
Datum plunit_assert_not_equals_message(PG_FUNCTION_ARGS);
Datum plunit_assert_not_equals_range(PG_FUNCTION_ARGS);
Datum plunit_assert_not_equals_range_message(PG_FUNCTION_ARGS);
Datum plunit_fail(PG_FUNCTION_ARGS);
Datum plunit_fail_message(PG_FUNCTION_ARGS);

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
 *   PROCEDURE assert_equals(expected double precision, expected double precision,
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
plunit_assert_equals(PG_FUNCTION_ARGS)
{
	return plunit_assert_equals_message(fcinfo);
}

Datum 
plunit_assert_equals_message(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}

Datum 
plunit_assert_equals_range(PG_FUNCTION_ARGS)
{
	return plunit_assert_equals_range_message(fcinfo);
}

Datum 
plunit_assert_equals_range_message(PG_FUNCTION_ARGS)
{
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
	PG_RETURN_VOID();
}

