#include "postgres.h"
#include "utils/date.h"
#include "utils/builtins.h"
#include "utils/nabstime.h"

/*
 * External (defined in PgSQL datetime.c (timestamp utils))
 */

extern char *days[];

#define CASE_fmt_YYYY	case 0: case 1: case 2: case 3: case 4: case 5:
#define CASE_fmt_IYYY	case 6: case 7: case 8: case 9:
#define	CASE_fmt_Q	case 10:
#define	CASE_fmt_WW	case 11:
#define CASE_fmt_IW	case 12:
#define	CASE_fmt_W	case 13:
#define CASE_fmt_DAY	case 14: case 15: case 16:
#define CASE_fmt_MON	case 17: case 18: case 19: case 20:
#define CASE_fmt_CC	case 21: case 22:


char *date_fmt[] = 
    {"Y", "Yy", "Yyy", "Yyyy", "Syyy", "syear",
     "I", "Iy", "Iyy", "Iyyy",
     "Q", "Ww", "Iw", "W",
     "Day", "Dy", "D",
     "Month", "Mon", "Mm", "Rm",
     "Cc", "Scc",
     NULL};
     
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
Datum oround (PG_FUNCTION_ARGS);
Datum otrunc (PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(next_day);
PG_FUNCTION_INFO_V1(last_day);
PG_FUNCTION_INFO_V1(months_between);
PG_FUNCTION_INFO_V1(add_months);
PG_FUNCTION_INFO_V1(oround);
PG_FUNCTION_INFO_V1(otrunc);

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
	    
	    if (i == max && *p == '\0')
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
 * Returns last day of the month based on a date value
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
 * Returns the number of months between date1 and date2. If
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
 * Returns a date plus n months.
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

/*
 * ISO year - start first week contains thursday
 * everytime monday!
 */
 

static
DateADT iso_year (DateADT day, int y, int m, int d)
{
    DateADT result;
    int off;
    
    result = date2j(y,1,1) - POSTGRES_EPOCH_JDATE;
    off = 4 - j2day(result + POSTGRES_EPOCH_JDATE);
    result = (off <= 0) ? result + 4 + off:result - 3 + off;

    if (result > day)
    {
	result = date2j(y-1,1,1) - POSTGRES_EPOCH_JDATE;
	off = 4 - j2day(result + POSTGRES_EPOCH_JDATE);
	result = (off <= 0) ? result + 4 + off:result - 3 + off;
    }
    
    return result;
}
    
/********************************************************************
 *
 * otrunc .. trunc
 *
 * Syntax:
 *
 * date trunc(date date1, text format)
 *
 * Purpose:
 *
 * Returns d with the time portion of the day truncated to the unit 
 * specified by the format fmt.
 *
 ********************************************************************/

Datum otrunc (PG_FUNCTION_ARGS)
{
    DateADT day = PG_GETARG_DATEADT(0);
    text *fmt = PG_GETARG_TEXT_P(1);
    
    int y, m, d;
    DateADT result, tmp;
    
    int f = seq_search(VARDATA(fmt), date_fmt, VARATT_SIZEP(fmt) - VARHDRSZ);
    CHECK_SEQ_SEARCH(f, "round/trunc format string");
    
    j2date(day + POSTGRES_EPOCH_JDATE, &y, &m, &d);
        
    switch (f)
    {
	CASE_fmt_CC
	    result = date2j((y/100)*100,1,1) - POSTGRES_EPOCH_JDATE;
	    break;	
	CASE_fmt_YYYY
	    result = date2j(y,1,1) - POSTGRES_EPOCH_JDATE;
	    break;
	CASE_fmt_IYYY
	    result = iso_year(day,y,m,d);
	    break;
	CASE_fmt_MON
	    result = date2j(y,m,1) - POSTGRES_EPOCH_JDATE;
	    break;
	CASE_fmt_WW
	    tmp = date2j(y,1,1) - POSTGRES_EPOCH_JDATE;
	    result = day - (day - tmp) % 7;
	    break;
	CASE_fmt_IW
	    tmp = iso_year(day, y, m, d);
	    result = day - (day - tmp) % 7;
	    break;
	CASE_fmt_W
	    tmp = date2j(y,m,1) - POSTGRES_EPOCH_JDATE;
	    result = day - (day - tmp) % 7;
	    break;
	CASE_fmt_DAY
	    result = day - j2day(day+POSTGRES_EPOCH_JDATE);
	    break;
	CASE_fmt_Q
	    result = date2j(y,((m-1)/3)*3+1,1) - POSTGRES_EPOCH_JDATE;
	    break;	    
    }

    PG_RETURN_DATEADT(result);
}

/********************************************************************
 *
 * oround .. round
 *
 * Syntax:
 *
 * date round(date date1, text format)
 *
 * Purpose:
 *
 * Returns d with the time portion of the day roundeded to the unit 
 * specified by the format fmt.
 *
 ********************************************************************/


Datum oround (PG_FUNCTION_ARGS)
{
    DateADT day = PG_GETARG_DATEADT(0);
    text *fmt = PG_GETARG_TEXT_P(1);

    int y, m, d, z; 
    DateADT result, tmp;
    
    int f = seq_search(VARDATA(fmt), date_fmt, VARATT_SIZEP(fmt) - VARHDRSZ);
    CHECK_SEQ_SEARCH(f, "round/trunc format string");
    
    j2date(day + POSTGRES_EPOCH_JDATE, &y, &m, &d);

    switch (f)
    {
	CASE_fmt_CC
	    tmp = date2j((y/100)*100+50,7,1) - POSTGRES_EPOCH_JDATE;
	    result = date2j((y/100)*100+(day<tmp?0:100),1,1) - POSTGRES_EPOCH_JDATE;
	    break;	
	CASE_fmt_YYYY
	    tmp = date2j(y,7,1) - POSTGRES_EPOCH_JDATE;
	    result = date2j(y+(day<tmp?0:1),1,1) - POSTGRES_EPOCH_JDATE;
	    break;
	CASE_fmt_IYYY
	    {
		DateADT isoyear = iso_year(day, y, m, d);
		j2date(isoyear + POSTGRES_EPOCH_JDATE, &y, &m, &d);
		tmp = date2j(y, 7, 1) + POSTGRES_EPOCH_JDATE;
		if (day < tmp)
		    result = isoyear;
		else
		{
		    tmp = date2j(y+1, 1, 8) + POSTGRES_EPOCH_JDATE;
		    result = iso_year(tmp, y+1, 1, 8);
		}
		break;	
	    }
	CASE_fmt_MON
	    tmp = date2j(y,m,16) - POSTGRES_EPOCH_JDATE;
	    result = date2j(y,m+(day<tmp?0:1),1) - POSTGRES_EPOCH_JDATE;
	    break;
	CASE_fmt_WW
	    tmp = date2j(y,1,1) - POSTGRES_EPOCH_JDATE;
	    z = (day - tmp) % 7;
	    result = day - z + (z < 3?0:7);
	    break;
	CASE_fmt_IW
	    tmp = iso_year(day, y, m, d);
	    z = (day - tmp) % 7;
	    result = day - z + (z < 3?0:7);
	    break;
	CASE_fmt_W
	    tmp = date2j(y,m,1) - POSTGRES_EPOCH_JDATE;
	    z = (day - tmp) % 7;
	    result = day - z + (z < 3?0:7);
	    break;
	CASE_fmt_DAY
	    result = day - j2day(day+POSTGRES_EPOCH_JDATE);
	    break;
	CASE_fmt_Q
	    tmp = date2j(y,((m-1)/3)*3+2,16) - POSTGRES_EPOCH_JDATE;
	    result = date2j(y,((m-1)/3)*3+(day<tmp?1:4),1) - POSTGRES_EPOCH_JDATE;
	    break;	    
    }
    
    PG_RETURN_DATEADT(result);    
}