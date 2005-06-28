#include "postgres.h"
#include "utils/date.h"
#include "utils/builtins.h"
#include "utils/nabstime.h"

/*
 * External (defined in PgSQL datetime.c (timestamp utils))
 */

extern char *days[];

#define CHECK_SEQ_SEARCH(_l, _s) \
do { \
     if ((_l) < 0) {                                                 \
               ereport(ERROR,                                        \
                     (errcode(ERRCODE_INVALID_DATETIME_FORMAT),      \
                      errmsg("invalid value for %s", (_s))));        \
              }                                                      \
} while (0)

/*
 * Search const value in char array
 *
 */

Datum next_day (PG_FUNCTION_ARGS);
Datum last_day (PG_FUNCTION_ARGS);
Datum months_between (PG_FUNCTION_ARGS);
Datum add_months (PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(next_day);
PG_FUNCTION_INFO_V1(last_day);
PG_FUNCTION_INFO_V1(months_between);
PG_FUNCTION_INFO_V1(add_months);

static int
seq_search(char *name, char **array, int max)
{
    char *p, *n, **a;
    int last, i;
    
    if (!*name)
	return -1;
    
    *name = pg_toupper((unsigned char) *name);
    for (last = 0, a = array; *a != NULL; a++)
    {
	
	if (*name != **a)
	    continue;

	for (i = 1, p = *a + 1, n = name + 1;; n++, p++, i++)
	{
	    if (i == max)
		return a - array;
	    if (*p == '\0')
	       break;
	    if (i > last)
	    {
		*n = pg_tolower((unsigned char) *n);
		last = i;
	    }
	    
	    if (*n != *p)
		     break;
	}
    }
    
    return -1;
}

/********************************************************************
 *
 * next_day
 *
 * Syntax:
 *
 * date next_day(date value, text weekday)
 *
 * Purpose:
 *
 * Returns the first weekday that is greater than a date value.
 *
 ********************************************************************/


Datum next_day (PG_FUNCTION_ARGS)
{
	 
    DateADT day = PG_GETARG_DATEADT(0);
    text *day_txt = PG_GETARG_TEXT_P(1);
    int off;

    int d = seq_search(VARDATA(day_txt), days, VARATT_SIZEP(day_txt) - VARHDRSZ);
    CHECK_SEQ_SEARCH(d, "DAY/Day/day");
    
    off = d - j2day(day+POSTGRES_EPOCH_JDATE);
	
    PG_RETURN_DATEADT((off <= 0) ? day+off+7 : day + off);
}

/********************************************************************
 *
 * last_day
 *
 * Syntax:
 *
 * date last_day(date value)
 *
 * Purpose:
 *
 *Returns last day of the month based on a date value
 *
 ********************************************************************/


Datum last_day (PG_FUNCTION_ARGS)
{
    DateADT day = PG_GETARG_DATEADT(0);
    DateADT result;
    int y, m, d;
    j2date(day + POSTGRES_EPOCH_JDATE, &y, &m, &d);
    result = date2j(y, m+1, 1) - POSTGRES_EPOCH_JDATE;
    
    PG_RETURN_DATEADT(result - 1);
}

/********************************************************************
 *
 * months_between
 *
 * Syntax:
 *
 * float8 months_between(date date1, date date2)
 *
 * Purpose:
 *
 *Returns the number of months between date1 and date2. If
 *      a fractional month is calculated, the months_between  function
 *      calculates the fraction based on a 31-day month.
 *
 ********************************************************************/


Datum months_between (PG_FUNCTION_ARGS)
{
    DateADT date1 = PG_GETARG_DATEADT(0);
    DateADT date2 = PG_GETARG_DATEADT(1);
    
    int y1, m1, d1;
    int y2, m2, d2;
    
    float8 result;
    
    j2date(date1 + POSTGRES_EPOCH_JDATE, &y1, &m1, &d1);
    j2date(date2 + POSTGRES_EPOCH_JDATE, &y2, &m2, &d2);
    
    result = (y1 - y2) * 12 + (m1 - m2) + (d1 - d2) / 31.0;
    
    PG_RETURN_FLOAT8(result);  
}

/********************************************************************
 *
 * add_months
 *
 * Syntax:
 *
 * date add_months(date day, int val)
 *
 * Purpose:
 *
 *Returns a date plus n months.
 *
 ********************************************************************/


Datum add_months (PG_FUNCTION_ARGS)
{
    DateADT day = PG_GETARG_DATEADT(0);
    int n = PG_GETARG_INT32(1);
    int y, m, d;
    DateADT last_day, result;
    
    j2date(day + POSTGRES_EPOCH_JDATE, &y, &m, &d);
    result = date2j(y, m+n, d) - POSTGRES_EPOCH_JDATE;
    
    if (d > 28)
    {
	m += 2;
	if (m  > 12)
	{
	    ++y; m -= 12;
	}
	last_day = date2j(y, m, 1) - POSTGRES_EPOCH_JDATE - 1;
	if (last_day < result)
	    result = last_day;
    }
    
    PG_RETURN_DATEADT (result);
}
