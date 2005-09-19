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
#define CASE_fmt_DDD	case 23: case 24: case 25:
#define CASE_fmt_HH	case 26: case 27: case 28:
#define CASE_fmt_MI	case 29:

//chybi implementace DDD /// pro typ date nema smysl

char *date_fmt[] = 
    {"Y", "Yy", "Yyy", "Yyyy", "Syyy", "syear",
     "I", "Iy", "Iyy", "Iyyy",
     "Q", "Ww", "Iw", "W",
     "Day", "Dy", "D",
     "Month", "Mon", "Mm", "Rm",
     "Cc", "Scc",
     "Ddd", "Dd", "J",
     "Hh", "Hh12", "Hh24",
     "Mi",
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
Datum ora_date_trunc (PG_FUNCTION_ARGS);
Datum ora_date_round (PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(next_day);
PG_FUNCTION_INFO_V1(last_day);
PG_FUNCTION_INFO_V1(months_between);
PG_FUNCTION_INFO_V1(add_months);
PG_FUNCTION_INFO_V1(ora_date_trunc);
PG_FUNCTION_INFO_V1(ora_date_round);

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
 * ISO year 
 * 
 */
 
#define DATE2J(y,m,d)	(date2j((y),(m),(d)) - POSTGRES_EPOCH_JDATE)
#define J2DAY(date)	(j2day(date + POSTGRES_EPOCH_JDATE))

static
DateADT iso_year (int y, int m, int d)
{
    DateADT result, result2, day;
    int off;
    
    result = DATE2J(y,1,1);
    day = DATE2J(y,m,d);
    off = 4 - J2DAY(result);
    result += off + ((off >= 0) ? - 3: + 4);

    if (result > day)
    {
	result = DATE2J(y-1,1,1);
	off = 4 - J2DAY(result);
	result += off + ((off >= 0) ? - 3: + 4);
    }
    
    if (((day - result) / 7 + 1) > 52)
    {
	result2 = DATE2J(y+1,1,1);
	off = 4 - J2DAY(result2);
	result2 += off + ((off >= 0) ? - 3: + 4);
	if (day >= result2)
		return result2;
    }
    
    return result;
}
    
//ora_timestamp_trunc
//ora_timestamp_round

/********************************************************************
 *
 * ora_date_trunc .. trunc
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

Datum ora_date_trunc (PG_FUNCTION_ARGS)
{
    DateADT day = PG_GETARG_DATEADT(0);
    text *fmt = PG_GETARG_TEXT_P(1);
    
    int y, m, d;
    DateADT result;
    
    int f = seq_search(VARDATA(fmt), date_fmt, VARATT_SIZEP(fmt) - VARHDRSZ);
    CHECK_SEQ_SEARCH(f, "round/trunc format string");
    
    j2date(day + POSTGRES_EPOCH_JDATE, &y, &m, &d);
        
    switch (f)
    {
	CASE_fmt_CC
	    result = DATE2J((y/100)*100+1,1,1);
	    break;	
	CASE_fmt_YYYY
	    result = DATE2J(y,1,1);
	    break;
	CASE_fmt_IYYY
	    result = iso_year(y,m,d);
	    break;
	CASE_fmt_MON
	    result = DATE2J(y,m,1);
	    break;
	CASE_fmt_WW
	    result = day - (day - DATE2J(y,1,1)) % 7;
	    break;
	CASE_fmt_IW
	    result = day - (day - iso_year(y,m,d)) % 7;
	    break;
	CASE_fmt_W
	    result = day - (day - DATE2J(y,m,1)) % 7;
	    break;
	CASE_fmt_DAY
	    result = day - J2DAY(day);
	    break;
	CASE_fmt_Q
	    result = DATE2J(y,((m-1)/3)*3+1,1);
	    break;	    
	default:
	    result = day;
    }

    PG_RETURN_DATEADT(result);
}

//date_round_frm
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


Datum ora_date_round (PG_FUNCTION_ARGS)
{
    DateADT day = PG_GETARG_DATEADT(0);
    text *fmt = PG_GETARG_TEXT_P(1);

    int y, m, d, z; 
    DateADT result;
    
    int f = seq_search(VARDATA(fmt), date_fmt, VARATT_SIZEP(fmt) - VARHDRSZ);
    CHECK_SEQ_SEARCH(f, "round/trunc format string");
    
    j2date(day + POSTGRES_EPOCH_JDATE, &y, &m, &d);

    switch (f)
    {
	CASE_fmt_CC
	    result = DATE2J((y/100)*100+(day < DATE2J((y/100)*100+50,7,1) ?1:101),1,1);
	    break;	
	CASE_fmt_YYYY
	    result = DATE2J(y+(day<DATE2J(y,7,1)?0:1),1,1);
	    break;
	CASE_fmt_IYYY
	    {
		if (day < DATE2J(y,7,1))
		{
		    result = iso_year(y, m, d);
		}
		else
		{
		    DateADT iy1 = iso_year(y+1, 1, 8);
		    result = iy1;
		    
		    if (((day - DATE2J(y,1,1)) / 7 + 1) >= 52) 
		    {
			bool overl = ((date2j(y+2,1,1)-date2j(y+1,1,1)) == 366);
			bool isSaturday = (J2DAY(day) == 6);

			DateADT iy2 = iso_year(y+2, 1, 8);
			DateADT day1 = DATE2J(y+1,1,1);
			/* exception saturdays */
			if (iy1 >= (day1) && day >= day1 - 2 && isSaturday)
			{
			    result = overl?iy2:iy1;
			} 
			/* iso year stars in last year and day >= iso year */
			else if (iy1 <= (day1) && day >= iy1 - 3) 
			{
			    DateADT cmp = iy1 - (iy1 < day1?0:1);
			    int d = J2DAY(day1);
			    /* some exceptions */
			    if ((day >= cmp - 2) && (!(d == 3 && overl)))
			    {			
				/* if year don't starts in thursday */
				if ((d < 4 && J2DAY(day) != 5 && !isSaturday)||(d == 2 && isSaturday && overl))
				{
				    result = iy2;
				}
			    }
			}			
		    }
		}    
	    }
	    break;
	CASE_fmt_MON
	    result = DATE2J(y,m+(day<DATE2J(y,m,16)?0:1),1);
	    break;
	CASE_fmt_WW
	    z = (day - DATE2J(y,1,1)) % 7;
	    result = day - z + (z < 4?0:7);
	    break;
	CASE_fmt_IW
	    {
		z = (day - iso_year(y,m,d)) % 7;
		result = day - z + (z < 4?0:7);
		if (((day - DATE2J(y,1,1)) / 7 + 1) >= 52)
		{
		    /* only for last iso week */
		    DateADT isoyear = iso_year(y+1, 1, 8);
		    if (isoyear > (DATE2J(y+1,1,1)-1))
			if (day > isoyear - 7)
			{
			    int d = J2DAY(day);
			    result -= (d == 0 || d > 4?7:0);
			}
		}
	    }
	    break;
	CASE_fmt_W
	    z = (day - DATE2J(y,m,1)) % 7;
	    result = day - z + (z < 4?0:7);
	    break;
	CASE_fmt_DAY
	    z = J2DAY(day);
	    result = day - z + (z < 4?0:7);
	    break;
	CASE_fmt_Q
	    result = DATE2J(y,((m-1)/3)*3+(day<(DATE2J(y,((m-1)/3)*3+2,16))?1:4),1);
	    break;	 
	default:
	    result = day;   
    }

    PG_RETURN_DATEADT(result);    
}


// Dalsi plan zkusmo si napsat kod pro timestamp a pak se rozhodnout co
// bude efektivnejsi, jestli prevod na timestamp a zpet nebo na date a zpet
// pripadne ponechani obou variant. Pripadne pouze minuty hodiny pro timestamp
// a zbytek zavolat date
