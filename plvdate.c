/*
  This code implements one part of functonality of 
  free available library PL/Vision. Please look www.quest.com
*/

#define PLVDATE_VERSION  "PostgreSQL PLVdate, version 1.0, March 2006"

#include "postgres.h"
#include "utils/date.h"
#include "utils/builtins.h"
#include "utils/nabstime.h"
#include <sys/time.h>

/*
 * External (defined in PgSQL datetime.c (timestamp utils))
 */

extern char *days[];

Datum plvdate_add_bizdays (PG_FUNCTION_ARGS);
Datum plvdate_nearest_bizday (PG_FUNCTION_ARGS);
Datum plvdate_next_bizday (PG_FUNCTION_ARGS);
Datum plvdate_bizdays_between (PG_FUNCTION_ARGS);
Datum plvdate_prev_bizday (PG_FUNCTION_ARGS);
Datum plvdate_isbizday (PG_FUNCTION_ARGS);

Datum plvdate_set_nonbizday_dow (PG_FUNCTION_ARGS);
Datum plvdate_unset_nonbizday_dow (PG_FUNCTION_ARGS);
Datum plvdate_set_nonbizday_day (PG_FUNCTION_ARGS);
Datum plvdate_unset_nonbizday_day (PG_FUNCTION_ARGS);
Datum plvdate_set_nonbizday_day_eyear (PG_FUNCTION_ARGS);
Datum plvdate_unset_nonbizday_day_eyear (PG_FUNCTION_ARGS);
Datum plvdate_use_easter (PG_FUNCTION_ARGS);

Datum plvdate_default_czech (PG_FUNCTION_ARGS);

Datum plvdate_version (PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(plvdate_add_bizdays);
PG_FUNCTION_INFO_V1(plvdate_nearest_bizday);
PG_FUNCTION_INFO_V1(plvdate_next_bizday);
PG_FUNCTION_INFO_V1(plvdate_bizdays_between);
PG_FUNCTION_INFO_V1(plvdate_prev_bizday);
PG_FUNCTION_INFO_V1(plvdate_isbizday);

PG_FUNCTION_INFO_V1(plvdate_set_nonbizday_dow);
PG_FUNCTION_INFO_V1(plvdate_unset_nonbizday_dow);
PG_FUNCTION_INFO_V1(plvdate_set_nonbizday_day);
PG_FUNCTION_INFO_V1(plvdate_unset_nonbizday_day);
PG_FUNCTION_INFO_V1(plvdate_use_easter);

PG_FUNCTION_INFO_V1(plvdate_default_czech);

PG_FUNCTION_INFO_V1(plvdate_version);

#define CHECK_SEQ_SEARCH(_l, _s) \
do { \
     if ((_l) < 0) {                                                 \
               ereport(ERROR,                                        \
                     (errcode(ERRCODE_INVALID_DATETIME_FORMAT),      \
                      errmsg("invalid value for %s", (_s))));        \
              }                                                      \
} while (0)

int ora_seq_search(char *name, char **array, int max);

#define SUNDAY     (1 << 0)
#define SATURDAY   (1 << 6)

static unsigned char nonbizdays = SUNDAY | SATURDAY; 
static bool use_easter = true;

#define MAX_HOLYDAYS   50
#define MAX_EXCEPTIONS 50

typedef struct {
	char day;
	char month;
} holyday_desc;

static holyday_desc holydays[MAX_HOLYDAYS];
static DateADT exceptions[MAX_EXCEPTIONS];

static int holydays_c = 0;
static int exceptions_c = 0;

static holyday_desc czech_holydays[] = {
	{1,1}, // Novy rok
	{1,0}, // Nedele velikonocni
	{2,0}, // Pondeli velikonocni
	{1,5}, // Svatek prace
	{8,5}, // Den osvobozeni
	{5,7}, // Den slovanskych verozvestu
	{6,7}, // Den upaleni mistra Jana Husa
	{28,9}, // Den ceske statnosti
	{28,10}, // Den vzniku samostatneho ceskoslovenskeho statu
	{17,11}, // Den boje za svobodu a demokracii
	{24,12}, // Stedry den
	{25,12}, // 1. svatek vanocni
	{26,12}  // 2. svatek vanocni
};

static void 
easter_sunday(int year, int* dd, int* mm)
{
	int b, d, e, q;
	
	if (year < 1900 || year > 2099)
		ereport(ERROR,
				(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
				 errmsg("Easter is defined for years between 1900 and 2099")));

	b = 255 - 11 * (year % 19);
	d = ((b - 21) % 30) + 21;
	if (d > 38) d -= 1;
	e = (year + year/4 + d + 1) % 7;
	q = d + 7 - e;
	if (q < 32)
	{
		*dd = q; *mm = 3;
	}
	else
	{
		*dd = q - 31; *mm = 4;
	}
}


/****************************************************************
 * PLVdate.add_bizdays
 *
 * Syntax:
 *   FUNCTION add_bizdays(IN dt DATE, IN days int) RETURNS DATE; 
 *
 * Purpouse:
 *   Get the date created by adding <n> business days to a date
 *
 ****************************************************************/

Datum 
plvdate_add_bizdays (PG_FUNCTION_ARGS)
{
	DateADT day = PG_GETARG_DATEADT(0);
	int days = PG_GETARG_INT32(1);

	int d = j2day(day+POSTGRES_EPOCH_JDATE);
	int offset = 0;

	do
	{
		d = ++d % 7;
		offset += 1;
		if (!((1 << d) & nonbizdays))
			days -= 1;
	} while (days > 0);

	PG_RETURN_DATEADT(day+offset);
}


/****************************************************************
 * PLVdate.nearest_bizday
 *
 * Syntax:
 *   FUNCTION nearest_bizday(IN dt DATE) RETURNS DATE;
 *
 * Purpouse:
 *   Get the nearest business date to a given date, user defined
 *
 ****************************************************************/

Datum 
plvdate_nearest_bizday (PG_FUNCTION_ARGS)
{
	elog(WARNING, "Not implemented yet");

	PG_RETURN_NULL();
}


/****************************************************************
 * PLVdate.next_bizday
 *
 * Syntax:
 *   FUNCTION next_bizday(IN dt DATE) RETURNS DATE;
 *
 * Purpouse:
 *   Get the next business date from a given date, user defined
 *
 ****************************************************************/

Datum 
plvdate_next_bizday (PG_FUNCTION_ARGS)
{
	DateADT day = PG_GETARG_DATEADT(0);
	int d = j2day(day+POSTGRES_EPOCH_JDATE);
	int offset = 0;

	do
	{
		d = ++d % 7;
		offset += 1;
	} while ((1 << d) & nonbizdays);

	PG_RETURN_DATEADT(day+offset);
}


/****************************************************************
 * PLVdate.bizdays_between
 *
 * Syntax:
 *   FUNCTION bizdays_between(IN dt1 DATE, IN dt2 DATE)
 *     RETURNS int; 
 *
 * Purpouse:
 *   Get the number of business days between two dates
 *
 ****************************************************************/

Datum 
plvdate_bizdays_between (PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}


/****************************************************************
 * PLVdate.prev_bizday
 *
 * Syntax:
 *   FUNCTION prev_bizday(IN dt DATE) RETURNS date;
 *
 * Purpouse:
 *   Get the previous business date from a given date, user 
 * defined
 *
 ****************************************************************/

Datum 
plvdate_prev_bizday (PG_FUNCTION_ARGS)
{
	elog(WARNING, "Not implemented yet");

	PG_RETURN_NULL();
}


/****************************************************************
 * PLVdate.isbizday
 *
 * Syntax:
 *   FUNCTION isbizday(IN dt DATE) RETURNS bool;
 *
 * Purpouse:
 *   Call this function to determine if a date is a business day
 *
 ****************************************************************/

Datum 
plvdate_isbizday (PG_FUNCTION_ARGS)
{
	DateADT day = PG_GETARG_DATEADT(0);

	PG_RETURN_BOOL(!((1 << j2day(day+POSTGRES_EPOCH_JDATE)) & nonbizdays));
}


/****************************************************************
 * PLVdate.set_nonbizday
 *
 * Syntax:
 *   FUNCTION set_nonbizday(IN VARCHAR) RETURNS void;
 *
 * Purpouse:
 *   Set day of week as non bussines day
 *
 ****************************************************************/

Datum 
plvdate_set_nonbizday_dow (PG_FUNCTION_ARGS)
{
	unsigned char check;

	text *day_txt = PG_GETARG_TEXT_P(0);
	
	int d = ora_seq_search(VARDATA(day_txt), days, VARATT_SIZEP(day_txt) - VARHDRSZ);
    CHECK_SEQ_SEARCH(d, "DAY/Day/day");

	check = nonbizdays | (1 << d);
	if (check == 0x7f)
		elog(ERROR, "Can't to set all non bussined days.");
    
	nonbizdays = nonbizdays | (1 << d);

	PG_RETURN_VOID();
}

/****************************************************************
 * PLVdate.unset_nonbizday
 *
 * Syntax:
 *   FUNCTION unset_nonbizday(IN VARCHAR) RETURNS void;
 *
 * Purpouse:
 *   Unset day of week as non bussines day
 *
 ****************************************************************/

Datum 
plvdate_unset_nonbizday_dow (PG_FUNCTION_ARGS)
{
	text *day_txt = PG_GETARG_TEXT_P(0);
	
	int d = ora_seq_search(VARDATA(day_txt), days, VARATT_SIZEP(day_txt) - VARHDRSZ);
    CHECK_SEQ_SEARCH(d, "DAY/Day/day");
    
	nonbizdays = (nonbizdays | (1 << d)) ^ (1 << d);

	PG_RETURN_VOID();
}

/****************************************************************
 * PLVdate.set_nonbizday
 *
 * Syntax:
 *   FUNCTION set_nonbizday(IN DATE, false) RETURNS void;
 *   FUNCTION set_nonbizday(IN DATE, IN BOOL) RETURNS void;
 *
 * Purpouse:
 *   Set day as non bussines day, second arg specify year's 
 * periodicity
 *
 ****************************************************************/

Datum 
plvdate_set_nonbizday_day (PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}

/****************************************************************
 * PLVdate.unset_nonbizday
 *
 * Syntax:
 *   FUNCTION unset_nonbizday(IN DATE, false) RETURNS void;
 *   FUNCTION unset_nonbizday(IN DATE, IN BOOL) RETURNS void;
 *
 * Purpouse:
 *   Unset day as non bussines day, second arg specify year's 
 * periodicity
 *
 ****************************************************************/
	 
Datum 
plvdate_unset_nonbizday_day (PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}


/****************************************************************
 * PLVdate.use_easter
 *
 * Syntax:
 *   FUNCTION use_easter(IN bool) RETURNS void
 *
 * Purpouse:
 *   Have to use easter as nonbizday?
 *
 ****************************************************************/

Datum 
plvdate_use_easter (PG_FUNCTION_ARGS)
{
	use_easter = PG_GETARG_BOOL(0);

	PG_RETURN_VOID();
}

/*
 * Default knowed configurations
 * 
 */

Datum
plvdate_default_czech (PG_FUNCTION_ARGS)
{
	nonbizdays = SUNDAY | SATURDAY; 
	use_easter = true;
	exceptions_c = 0;

	holydays_c = 13;
	memcpy(holydays, czech_holydays, holydays_c*sizeof(holyday_desc));

	PG_RETURN_VOID();
}

/*
 * helper maintaince functions
 */

Datum
plvdate_version (PG_FUNCTION_ARGS)
{
	elog(WARNING, "Not implemented yet");

	PG_RETURN_NULL();
}
