/*
 * Note - I don't find any documentation about pseudo random
 * number generator used in Oracle. So the results of these
 * functions should be different then native Oracle functions!
 * This library is based on ANSI C implementation.
 */

#include "postgres.h"
#include "access/hash.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"

#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <errno.h>

#include "orafce.h"
#include "builtins.h"

#if PG_VERSION_NUM >= 160000

#include "varatt.h"

#endif

PG_FUNCTION_INFO_V1(dbms_random_initialize);
PG_FUNCTION_INFO_V1(dbms_random_normal);
PG_FUNCTION_INFO_V1(dbms_random_random);
PG_FUNCTION_INFO_V1(dbms_random_seed_int);
PG_FUNCTION_INFO_V1(dbms_random_seed_varchar);
PG_FUNCTION_INFO_V1(dbms_random_string);
PG_FUNCTION_INFO_V1(dbms_random_terminate);
PG_FUNCTION_INFO_V1(dbms_random_value);
PG_FUNCTION_INFO_V1(dbms_random_value_range);

/* Coefficients in rational approximations. */
static const double a[] =
{
	-3.969683028665376e+01,
	 2.209460984245205e+02,
	-2.759285104469687e+02,
	 1.383577518672690e+02,
	-3.066479806614716e+01,
	 2.506628277459239e+00
};

static const double b[] =
{
	-5.447609879822406e+01,
	 1.615858368580409e+02,
	-1.556989798598866e+02,
	 6.680131188771972e+01,
	-1.328068155288572e+01
};

static const double c[] =
{
	-7.784894002430293e-03,
	-3.223964580411365e-01,
	-2.400758277161838e+00,
	-2.549732539343734e+00,
	 4.374664141464968e+00,
	 2.938163982698783e+00
};

static const double d[] =
{
	7.784695709041462e-03,
	3.224671290700398e-01,
	2.445134137142996e+00,
	3.754408661907416e+00
};

#define LOW 0.02425
#define HIGH 0.97575

static double ltqnorm(double p);


/*
 * dbms_random.initialize (seed IN BINARY_INTEGER)
 *
 *     Initialize package with a seed value
 */
Datum
dbms_random_initialize(PG_FUNCTION_ARGS)
{
	int seed = PG_GETARG_INT32(0);

	srand(seed);

	PG_RETURN_VOID();
}

/*
 * dbms_random.normal() RETURN NUMBER;
 *
 *     Returns random numbers in a standard normal distribution
 */
Datum
dbms_random_normal(PG_FUNCTION_ARGS)
{
	float8 result;

	/* need random value from (0..1) */
	result = ltqnorm(((double) rand() + 1) / ((double) RAND_MAX + 2));

	PG_RETURN_FLOAT8(result);
}

/*
 * dbms_random.random() RETURN BINARY_INTEGER;
 *
 *     Generate Random Numeric Values
 */
Datum
dbms_random_random(PG_FUNCTION_ARGS)
{
	int result;
	/*
	 * Oracle generator generates numebers from -2^31 and +2^31,
	 * ANSI C only from 0 .. RAND_MAX,
	 */
	result = 2 * (rand() - RAND_MAX / 2);

	PG_RETURN_INT32(result);
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
	int seed = PG_GETARG_INT32(0);

	srand(seed);

	PG_RETURN_VOID();
}

/*
 * Atention!
 *
 * Hash function should be changed between mayor pg versions,
 * don't use text based seed for regres tests!
 */
Datum
dbms_random_seed_varchar(PG_FUNCTION_ARGS)
{
	text *key = PG_GETARG_TEXT_P(0);
	Datum seed;

	seed = hash_any((unsigned char *) VARDATA_ANY(key), VARSIZE_ANY_EXHDR(key));

	srand((int) seed);

	PG_RETURN_VOID();
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
static text *
random_string(const char *charset, size_t chrset_size, int len)
{
	StringInfo	str;
	int	i;

	str = makeStringInfo();
	for (i = 0; i < len; i++)
	{
		double		r = (double) rand();
		int pos = (int) floor((r / ((double) RAND_MAX + 1)) * chrset_size);

		appendStringInfoChar(str, charset[pos]);
	}

	return cstring_to_text(str->data);
}

Datum
dbms_random_string(PG_FUNCTION_ARGS)
{
	char *option;
	int	len;
	const char *charset;
	size_t chrset_size;

	const char *alpha_mixed = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
	const char *lower_only = "abcdefghijklmnopqrstuvwxyz";
	const char *upper_only = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	const char *upper_alphanum = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	const char *printable = "`1234567890-=qwertyuiop[]asdfghjkl;'zxcvbnm,./!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVVBNM<>? \\~";

	if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
		ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("an argument is NULL")));

	option = text_to_cstring(PG_GETARG_TEXT_P(0));
	if (strlen(option) != 1)
		ereport(ERROR,
				(errcode(ERRCODE_STRING_DATA_RIGHT_TRUNCATION),
				 errmsg("this first parameter value is more than 1 characters long")));

	len = PG_GETARG_INT32(1);

	switch (option[0])
	{
		case 'a':
		case 'A':
			charset = alpha_mixed;
			chrset_size = strlen(alpha_mixed);
			break;
		case 'l':
		case 'L':
			charset = lower_only;
			chrset_size = strlen(lower_only);
			break;
		case 'u':
		case 'U':
			charset = upper_only;
			chrset_size = strlen(upper_only);
			break;
		case 'x':
		case 'X':
			charset = upper_alphanum;
			chrset_size = strlen(upper_alphanum);
			break;
		case 'p':
		case 'P':
			charset = printable;
			chrset_size = strlen(printable);
			break;

		/* Otherwise the returning string is in uppercase alpha characters. */
		default:
			charset = upper_only;
			chrset_size = strlen(upper_only);
			break;
	}

	PG_RETURN_TEXT_P(random_string(charset, chrset_size, len));
}

/*
 * dbms_random.terminate;
 *
 *     Terminate use of the Package
 */
Datum
dbms_random_terminate(PG_FUNCTION_ARGS)
{
	/* do nothing */
	PG_RETURN_VOID();
}

/*
 * dbms_random.value() RETURN NUMBER;
 *
 *     Gets a random number, greater than or equal to 0 and less than 1.
 */
Datum
dbms_random_value(PG_FUNCTION_ARGS)
{
	float8 result;

	/* result [0.0 - 1.0) */
	result = (double) rand() / ((double) RAND_MAX + 1);

	PG_RETURN_FLOAT8(result);
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
	float8 low = PG_GETARG_FLOAT8(0);
	float8 high = PG_GETARG_FLOAT8(1);
	float8 result;

	if (low > high)
		PG_RETURN_NULL();

	result = ((double) rand() / ((double) RAND_MAX + 1)) * ( high -  low) + low;

	PG_RETURN_FLOAT8(result);
}


/*
 * Lower tail quantile for standard normal distribution function.
 *
 * This function returns an approximation of the inverse cumulative
 * standard normal distribution function.  I.e., given P, it returns
 * an approximation to the X satisfying P = Pr{Z <= X} where Z is a
 * random variable from the standard normal distribution.
 *
 * The algorithm uses a minimax approximation by rational functions
 * and the result has a relative error whose absolute value is less
 * than 1.15e-9.
 *
 * Author:      Peter J. Acklam
 * Time-stamp:  2002-06-09 18:45:44 +0200
 * E-mail:      jacklam@math.uio.no
 * WWW URL:     http://www.math.uio.no/~jacklam
 *
 * C implementation adapted from Peter's Perl version
 */
static double
ltqnorm(double p)
{
	double q, r;

	errno = 0;

	if (p < 0 || p > 1)
	{
		errno = EDOM;
		return 0.0;
	}
	else if (p == 0)
	{
		errno = ERANGE;
		return -HUGE_VAL /* minus "infinity" */;
	}
	else if (p == 1)
	{
		errno = ERANGE;
		return HUGE_VAL /* "infinity" */;
	}
	else if (p < LOW)
	{
		/* Rational approximation for lower region */
		q = sqrt(-2*log(p));
		return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
			((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1);
	}
	else if (p > HIGH)
	{
		/* Rational approximation for upper region */
		q  = sqrt(-2*log(1-p));
		return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
			((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1);
	}
	else
	{
		/* Rational approximation for central region */
		q = p - 0.5;
		r = q*q;
		return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
			(((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1);
	}
}
