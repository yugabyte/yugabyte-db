/* Processed by ecpg (regression mode) */
/* These include files are added by the preprocessor */
#include <ecpglib.h>
#include <ecpgerrno.h>
#include <sqlca.h>
/* End of automatic include section */
#define ECPGdebug(X,Y) ECPGdebug((X)+100,(Y))

#line 1 "dt_test.pgc"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pgtypes_date.h>
#include <pgtypes_timestamp.h>
#include <pgtypes_interval.h>


#line 1 "regression.h"






#line 8 "dt_test.pgc"


int
main(void)
{
	/* exec sql begin declare section */
		 
		 
		  
		 
	
#line 14 "dt_test.pgc"
 date date1 ;
 
#line 15 "dt_test.pgc"
 timestamp ts1 ;
 
#line 16 "dt_test.pgc"
 interval * iv1 , iv2 ;
 
#line 17 "dt_test.pgc"
 char * text ;
/* exec sql end declare section */
#line 18 "dt_test.pgc"

	date date2;
	int mdy[3] = { 4, 19, 1998 };
	char *fmt, *out, *in;
	char *d1 = "Mon Jan 17 1966";
	char *t1 = "2000-7-12 17:34:29";
	int i;

	ECPGdebug(1, stderr);
	/* exec sql whenever sqlerror  do sqlprint ( ) ; */
#line 27 "dt_test.pgc"

	{ ECPGconnect(__LINE__, 0, "ecpg1_regression" , NULL, NULL , NULL, 0); 
#line 28 "dt_test.pgc"

if (sqlca.sqlcode < 0) sqlprint ( );}
#line 28 "dt_test.pgc"

	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "create table date_test ( d date , ts timestamp )", ECPGt_EOIT, ECPGt_EORT);
#line 29 "dt_test.pgc"

if (sqlca.sqlcode < 0) sqlprint ( );}
#line 29 "dt_test.pgc"

	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "set datestyle to iso", ECPGt_EOIT, ECPGt_EORT);
#line 30 "dt_test.pgc"

if (sqlca.sqlcode < 0) sqlprint ( );}
#line 30 "dt_test.pgc"

	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "set intervalstyle to postgres_verbose", ECPGt_EOIT, ECPGt_EORT);
#line 31 "dt_test.pgc"

if (sqlca.sqlcode < 0) sqlprint ( );}
#line 31 "dt_test.pgc"


	date1 = PGTYPESdate_from_asc(d1, NULL);
	ts1 = PGTYPEStimestamp_from_asc(t1, NULL);

	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "insert into date_test ( d , ts ) values ( $1  , $2  )", 
	ECPGt_date,&(date1),(long)1,(long)1,sizeof(date), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, 
	ECPGt_timestamp,&(ts1),(long)1,(long)1,sizeof(timestamp), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EOIT, ECPGt_EORT);
#line 36 "dt_test.pgc"

if (sqlca.sqlcode < 0) sqlprint ( );}
#line 36 "dt_test.pgc"


	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "select * from date_test where d = $1 ", 
	ECPGt_date,&(date1),(long)1,(long)1,sizeof(date), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EOIT, 
	ECPGt_date,&(date1),(long)1,(long)1,sizeof(date), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, 
	ECPGt_timestamp,&(ts1),(long)1,(long)1,sizeof(timestamp), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EORT);
#line 38 "dt_test.pgc"

if (sqlca.sqlcode < 0) sqlprint ( );}
#line 38 "dt_test.pgc"


	text = PGTYPESdate_to_asc(date1);
	printf ("Date: %s\n", text);
	PGTYPESchar_free(text);

	text = PGTYPEStimestamp_to_asc(ts1);
	printf ("timestamp: %s\n", text);
	PGTYPESchar_free(text);

	iv1 = PGTYPESinterval_from_asc("13556 days 12 hours 34 minutes 14 seconds ", NULL);
	PGTYPESinterval_copy(iv1, &iv2);
	text = PGTYPESinterval_to_asc(&iv2);
	printf ("interval: %s\n", text);
	PGTYPESinterval_free(iv1);
	PGTYPESchar_free(text);

	PGTYPESdate_mdyjul(mdy, &date2);
	printf("m: %d, d: %d, y: %d\n", mdy[0], mdy[1], mdy[2]);
	/* reset */
	mdy[0] = mdy[1] = mdy[2] = 0;

	printf("date seems to get encoded to julian %ld\n", date2);

	PGTYPESdate_julmdy(date2, mdy);
	printf("m: %d, d: %d, y: %d\n", mdy[0], mdy[1], mdy[2]);

	ts1 = PGTYPEStimestamp_from_asc("2003-12-04 17:34:29", NULL);
	text = PGTYPEStimestamp_to_asc(ts1);
	fmt = "(ddd), mmm. dd, yyyy, repeat: (ddd), mmm. dd, yyyy. end";
	out = (char*) malloc(strlen(fmt) + 1);
	date1 = PGTYPESdate_from_timestamp(ts1);
	PGTYPESdate_fmt_asc(date1, fmt, out);
	printf("date_day of %s is %d\n", text, PGTYPESdate_dayofweek(date1));
	printf("Above date in format \"%s\" is \"%s\"\n", fmt, out);
	PGTYPESchar_free(text);
	free(out);

	out = (char*) malloc(48);
	i = PGTYPEStimestamp_fmt_asc(&ts1, out, 47, "Which is day number %j in %Y.");
	printf("%s\n", out);
	free(out);


	/* rdate_defmt_asc() */

	date1 = 0;
	fmt = "yy/mm/dd";
	in = "In the year 1995, the month of December, it is the 25th day";
	/*    0123456789012345678901234567890123456789012345678901234567890
	 *    0         1         2         3         4         5         6
	 */
	PGTYPESdate_defmt_asc(&date1, fmt, in);
	text = PGTYPESdate_to_asc(date1);
	printf("date_defmt_asc1: %s\n", text);
	PGTYPESchar_free(text);

	date1 = 0;
	fmt = "mmmm. dd. yyyy";
	in = "12/25/95";
	PGTYPESdate_defmt_asc(&date1, fmt, in);
	text = PGTYPESdate_to_asc(date1);
	printf("date_defmt_asc2: %s\n", text);
	PGTYPESchar_free(text);

	date1 = 0;
	fmt = "yy/mm/dd";
	in = "95/12/25";
	PGTYPESdate_defmt_asc(&date1, fmt, in);
	text = PGTYPESdate_to_asc(date1);
	printf("date_defmt_asc3: %s\n", text);
	PGTYPESchar_free(text);

	date1 = 0;
	fmt = "yy/mm/dd";
	in = "1995, December 25th";
	PGTYPESdate_defmt_asc(&date1, fmt, in);
	text = PGTYPESdate_to_asc(date1);
	printf("date_defmt_asc4: %s\n", text);
	PGTYPESchar_free(text);

	date1 = 0;
	fmt = "dd-mm-yy";
	in = "This is 25th day of December, 1995";
	PGTYPESdate_defmt_asc(&date1, fmt, in);
	text = PGTYPESdate_to_asc(date1);
	printf("date_defmt_asc5: %s\n", text);
	PGTYPESchar_free(text);

	date1 = 0;
	fmt = "mmddyy";
	in = "Dec. 25th, 1995";
	PGTYPESdate_defmt_asc(&date1, fmt, in);
	text = PGTYPESdate_to_asc(date1);
	printf("date_defmt_asc6: %s\n", text);
	PGTYPESchar_free(text);

	date1 = 0;
	fmt = "mmm. dd. yyyy";
	in = "dec 25th 1995";
	PGTYPESdate_defmt_asc(&date1, fmt, in);
	text = PGTYPESdate_to_asc(date1);
	printf("date_defmt_asc7: %s\n", text);
	PGTYPESchar_free(text);

	date1 = 0;
	fmt = "mmm. dd. yyyy";
	in = "DEC-25-1995";
	PGTYPESdate_defmt_asc(&date1, fmt, in);
	text = PGTYPESdate_to_asc(date1);
	printf("date_defmt_asc8: %s\n", text);
	PGTYPESchar_free(text);

	date1 = 0;
	fmt = "mm yy   dd.";
	in = "12199525";
	PGTYPESdate_defmt_asc(&date1, fmt, in);
	text = PGTYPESdate_to_asc(date1);
	printf("date_defmt_asc9: %s\n", text);
	PGTYPESchar_free(text);

	date1 = 0;
	fmt = "yyyy fierj mm   dd.";
	in = "19951225";
	PGTYPESdate_defmt_asc(&date1, fmt, in);
	text = PGTYPESdate_to_asc(date1);
	printf("date_defmt_asc10: %s\n", text);
	PGTYPESchar_free(text);

	date1 = 0;
	fmt = "mm/dd/yy";
	in = "122595";
	PGTYPESdate_defmt_asc(&date1, fmt, in);
	text = PGTYPESdate_to_asc(date1);
	printf("date_defmt_asc12: %s\n", text);
	PGTYPESchar_free(text);

	PGTYPEStimestamp_current(&ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	/* can't output this in regression mode */
	/* printf("timestamp_current: Now: %s\n", text); */
	PGTYPESchar_free(text);

	ts1 = PGTYPEStimestamp_from_asc("96-02-29", NULL);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_to_asc1: %s\n", text);
	PGTYPESchar_free(text);

	ts1 = PGTYPEStimestamp_from_asc("1994-02-11 3:10:35", NULL);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_to_asc2: %s\n", text);
	PGTYPESchar_free(text);

	ts1 = PGTYPEStimestamp_from_asc("1994-02-11 26:10:35", NULL);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_to_asc3: %s\n", text);
	PGTYPESchar_free(text);

/*	abc-03:10:35-def-02/11/94-gh  */
/*      12345678901234567890123456789 */

	out = (char*) malloc(32);
	i = PGTYPEStimestamp_fmt_asc(&ts1, out, 31, "abc-%X-def-%x-ghi%%");
	printf("timestamp_fmt_asc: %d: %s\n", i, out);
	free(out);

	fmt = "This is a %m/%d/%y %H-%Ml%Stest";
	in =  "This is a 4/12/80 3-39l12test";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	fmt = "%a %b %d %H:%M:%S %z %Y";
	in =  "Tue Jul 22 17:28:44 +0200 2003";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	fmt = "%a %b %d %H:%M:%S %z %Y";
	in =  "Tue Feb 29 17:28:44 +0200 2000";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	fmt = "%a %b %d %H:%M:%S %z %Y";
	in =  "Tue Feb 29 17:28:44 +0200 1900";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error (should be error!): %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	fmt = "%a %b %d %H:%M:%S %z %Y";
	in =  "Tue Feb 29 17:28:44 +0200 1996";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	fmt = "%b %d %H:%M:%S %z %Y";
	in =  "      Jul 31 17:28:44 +0200 1996";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	fmt = "%b %d %H:%M:%S %z %Y";
	in =  "      Jul 32 17:28:44 +0200 1996";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error (should be error!): %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	fmt = "%a %b %d %H:%M:%S %z %Y";
	in =  "Tue Feb 29 17:28:44 +0200 1997";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error (should be error!): %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	fmt = "%";
	in =  "Tue Jul 22 17:28:44 +0200 2003";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error (should be error!): %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	fmt = "a %";
	in =  "Tue Jul 22 17:28:44 +0200 2003";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error (should be error!): %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	fmt = "%b, %d %H_%M`%S %z %Y";
	in =  "    Jul, 22 17_28 `44 +0200  2003  ";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	fmt = "%a %b %%%d %H:%M:%S %Z %Y";
	in =  "Tue Jul %22 17:28:44 CEST 2003";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	fmt = "%a %b %%%d %H:%M:%S %Z %Y";
	in =  "Tue Jul %22 17:28:44 CEST 2003";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	fmt = "abc%n %C %B %%%d %H:%M:%S %Z %Y";
	in =  "abc\n   19 October %22 17:28:44 CEST 2003";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	fmt = "abc%n %C %B %%%d %H:%M:%S %Z %y";
	in =  "abc\n   18 October %34 17:28:44 CEST 80";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error (should be error!): %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	fmt = "";
	in =  "abc\n   18 October %34 17:28:44 CEST 80";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error (should be error!): %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	fmt = NULL;
	in =  "1980-04-12 3:49:44      ";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, NULL) = %s, error: %d\n", in, text, i);
	PGTYPESchar_free(text);

	fmt = "%B %d, %Y. Time: %I:%M%p";
	in =  "July 14, 1988. Time: 9:15am";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	in = "September 6 at 01:30 pm in the year 1983";
	fmt = "%B %d at %I:%M %p in the year %Y";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	in = "  1976, July 14. Time: 9:15am";
	fmt = "%Y,   %B %d. Time: %I:%M %p";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	in = "  1976, July 14. Time: 9:15 am";
	fmt = "%Y,   %B %d. Time: %I:%M%p";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	in = "  1976, P.M. July 14. Time: 9:15";
	fmt = "%Y, %P  %B %d. Time: %I:%M";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	in = "1234567890";
	fmt = "%s";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	text = PGTYPEStimestamp_to_asc(ts1);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, text, i);
	PGTYPESchar_free(text);

	out = (char*) malloc(64);
	fmt = "%a %b %d %H:%M:%S %Y";
	in =  "Mon Dec 30 17:28:44 2019";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	i = PGTYPEStimestamp_fmt_asc(&ts1, out, 63, fmt);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, out, i);
	free(out);

	out = (char*) malloc(64);
	fmt = "%a %b %d %H:%M:%S %Y";
	in =  "Mon December 30 17:28:44 2019";
	i = PGTYPEStimestamp_defmt_asc(in, fmt, &ts1);
	i = PGTYPEStimestamp_fmt_asc(&ts1, out, 63, fmt);
	printf("timestamp_defmt_asc(%s, %s) = %s, error: %d\n", in, fmt, out, i);
	free(out);

	{ ECPGtrans(__LINE__, NULL, "rollback");
#line 381 "dt_test.pgc"

if (sqlca.sqlcode < 0) sqlprint ( );}
#line 381 "dt_test.pgc"

        { ECPGdisconnect(__LINE__, "CURRENT");
#line 382 "dt_test.pgc"

if (sqlca.sqlcode < 0) sqlprint ( );}
#line 382 "dt_test.pgc"


	return 0;
}
