#include "postgres.h"
#include "utils/builtins.h"

#include "orafunc.h"

Datum dbms_random_initialize(PG_FUNCTION_ARGS);
Datum dbms_random_normal(PG_FUNCTION_ARGS);
Datum dbms_random_random(PG_FUNCTION_ARGS);
Datum dbms_random_seed_int(PG_FUNCTION_ARGS);
Datum dbms_random_seed_varchar(PG_FUNCTION_ARGS);
Datum dbms_random_string(PG_FUNCTION_ARGS);
Datum dbms_random_terminate(PG_FUNCTION_ARGS);
Datum dbms_random_value(PG_FUNCTION_ARGS);
Datum dbms_random_value_range(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(dbms_random_initialize);
PG_FUNCTION_INFO_V1(dbms_random_normal);
PG_FUNCTION_INFO_V1(dbms_random_random);
PG_FUNCTION_INFO_V1(dbms_random_seed_int);
PG_FUNCTION_INFO_V1(dbms_random_seed_varchar);
PG_FUNCTION_INFO_V1(dbms_random_string);
PG_FUNCTION_INFO_V1(dbms_random_terminate);
PG_FUNCTION_INFO_V1(dbms_random_value);
PG_FUNCTION_INFO_V1(dbms_random_value_range);

/* 
 * dbms_random.initialize (seed IN BINARY_INTEGER)
 *
 *     Initialize package with a seed value
 */
Datum 
dbms_random_initialize(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}

/*
 * dbms_random.normal() RETURN NUMBER;
 *
 *     Returns random numbers in a standard normal distribution
 */
Datum
dbms_random_normal(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}

/*
 * dbms_random.random() RETURN BINARY_INTEGER;
 *
 *     Generate Random Numeric Values
 */
Datum
dbms_random_random(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}

/* 
 * dbms_random.seed(val IN BINARY_INTEGER);
 * dbms_random.seed(val IN VARCHAR2);
 *
 *     Reset the seed value
 */
Datum
dbms_random_seed_int(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}

Datum
dbms_random_seed_varchar(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}

/*
 * dbms_random.string(opt IN CHAR, len IN NUMBER) RETURN VARCHAR2;
 * 
 *     Create Random Strings
 * opt seed values:
 * 'a','A'  alpha characters only (mixed case)
 * 'l','L'  lower case alpha characters only
 * 'p','P'  any printable characters
 * 'u','U'  upper case alpha characters only
 * 'x','X'  any alpha-numeric characters (upper)
 */
Datum
dbms_random_string(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}

/*
 * dbms_random.terminate;
 *
 *     Terminate use of the Package
 */
Datum
dbms_random_terminate(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}

/*
 * dbms_random.value() RETURN NUMBER;
 *
 *     Gets a random number, greater than or equal to 0 and less than 1.
 */
Datum
dbms_random_value(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}

/*
 * dbms_random.value(low  NUMBER, high NUMBER) RETURN NUMBER
 *
 *     Alternatively, you can get a random Oracle number x, 
 *     where x is greater than or equal to low and less than high 
 */
Datum
dbms_random_value_range(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}
