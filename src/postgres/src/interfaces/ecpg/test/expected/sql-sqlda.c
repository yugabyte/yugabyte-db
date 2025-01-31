/* Processed by ecpg (regression mode) */
/* These include files are added by the preprocessor */
#include <ecpglib.h>
#include <ecpgerrno.h>
#include <sqlca.h>
/* End of automatic include section */
#define ECPGdebug(X,Y) ECPGdebug((X)+100,(Y))

#line 1 "sqlda.pgc"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "ecpg_config.h"


#line 1 "regression.h"






#line 6 "sqlda.pgc"


#line 1 "sqlda.h"
#ifndef ECPG_SQLDA_H
#define ECPG_SQLDA_H

#ifdef _ECPG_INFORMIX_H

#include "sqlda-compat.h"
typedef struct sqlvar_compat sqlvar_t;
typedef struct sqlda_compat sqlda_t;

#else

#include "sqlda-native.h"
typedef struct sqlvar_struct sqlvar_t;
typedef struct sqlda_struct sqlda_t;

#endif

#endif							/* ECPG_SQLDA_H */

#line 7 "sqlda.pgc"


#line 1 "pgtypes_numeric.h"
#ifndef PGTYPES_NUMERIC
#define PGTYPES_NUMERIC

#include <pgtypes.h>

#define NUMERIC_POS						0x0000
#define NUMERIC_NEG						0x4000
#define NUMERIC_NAN						0xC000
#define NUMERIC_NULL						0xF000
#define NUMERIC_MAX_PRECISION			1000
#define NUMERIC_MAX_DISPLAY_SCALE		NUMERIC_MAX_PRECISION
#define NUMERIC_MIN_DISPLAY_SCALE		0
#define NUMERIC_MIN_SIG_DIGITS			16

#define DECSIZE 30

typedef unsigned char NumericDigit;
typedef struct
{
	int			ndigits;		/* number of digits in digits[] - can be 0! */
	int			weight;			/* weight of first digit */
	int			rscale;			/* result scale */
	int			dscale;			/* display scale */
	int			sign;			/* NUMERIC_POS, NUMERIC_NEG, or NUMERIC_NAN */
	NumericDigit *buf;			/* start of alloc'd space for digits[] */
	NumericDigit *digits;		/* decimal digits */
} numeric;

typedef struct
{
	int			ndigits;		/* number of digits in digits[] - can be 0! */
	int			weight;			/* weight of first digit */
	int			rscale;			/* result scale */
	int			dscale;			/* display scale */
	int			sign;			/* NUMERIC_POS, NUMERIC_NEG, or NUMERIC_NAN */
	NumericDigit digits[DECSIZE];	/* decimal digits */
} decimal;

#ifdef __cplusplus
extern "C"
{
#endif

numeric    *PGTYPESnumeric_new(void);
decimal    *PGTYPESdecimal_new(void);
void		PGTYPESnumeric_free(numeric *);
void		PGTYPESdecimal_free(decimal *);
numeric    *PGTYPESnumeric_from_asc(char *, char **);
char	   *PGTYPESnumeric_to_asc(numeric *, int);
int			PGTYPESnumeric_add(numeric *, numeric *, numeric *);
int			PGTYPESnumeric_sub(numeric *, numeric *, numeric *);
int			PGTYPESnumeric_mul(numeric *, numeric *, numeric *);
int			PGTYPESnumeric_div(numeric *, numeric *, numeric *);
int			PGTYPESnumeric_cmp(numeric *, numeric *);
int			PGTYPESnumeric_from_int(signed int, numeric *);
int			PGTYPESnumeric_from_long(signed long int, numeric *);
int			PGTYPESnumeric_copy(numeric *, numeric *);
int			PGTYPESnumeric_from_double(double, numeric *);
int			PGTYPESnumeric_to_double(numeric *, double *);
int			PGTYPESnumeric_to_int(numeric *, int *);
int			PGTYPESnumeric_to_long(numeric *, long *);
int			PGTYPESnumeric_to_decimal(numeric *, decimal *);
int			PGTYPESnumeric_from_decimal(decimal *, numeric *);

#ifdef __cplusplus
}
#endif

#endif							/* PGTYPES_NUMERIC */

#line 8 "sqlda.pgc"


/* exec sql whenever sqlerror  stop ; */
#line 10 "sqlda.pgc"


/* These shouldn't be under DECLARE SECTION */
sqlda_t	*inp_sqlda, *outp_sqlda, *outp_sqlda1;

static void
dump_sqlda(sqlda_t *sqlda)
{
	int	i;

	if (sqlda == NULL)
	{
		printf("dump_sqlda called with NULL sqlda\n");
		return;
	}

	for (i = 0; i < sqlda->sqld; i++)
	{
		if (sqlda->sqlvar[i].sqlind && *(sqlda->sqlvar[i].sqlind) == -1)
			printf("name sqlda descriptor: '%s' value NULL'\n", sqlda->sqlvar[i].sqlname.data);
		else
		switch (sqlda->sqlvar[i].sqltype)
		{
		case ECPGt_char:
			printf("name sqlda descriptor: '%s' value '%s'\n", sqlda->sqlvar[i].sqlname.data, sqlda->sqlvar[i].sqldata);
			break;
		case ECPGt_int:
			printf("name sqlda descriptor: '%s' value %d\n", sqlda->sqlvar[i].sqlname.data, *(int *)sqlda->sqlvar[i].sqldata);
			break;
		case ECPGt_long:
			printf("name sqlda descriptor: '%s' value %ld\n", sqlda->sqlvar[i].sqlname.data, *(long int *)sqlda->sqlvar[i].sqldata);
			break;
		case ECPGt_long_long:
			printf(
#ifdef _WIN32
				"name sqlda descriptor: '%s' value %I64d\n",
#else
				"name sqlda descriptor: '%s' value %lld\n",
#endif
				sqlda->sqlvar[i].sqlname.data, *(long long int *)sqlda->sqlvar[i].sqldata);
			break;
		case ECPGt_double:
			printf("name sqlda descriptor: '%s' value %f\n", sqlda->sqlvar[i].sqlname.data, *(double *)sqlda->sqlvar[i].sqldata);
			break;
		case ECPGt_numeric:
			{
				char    *val;

				val = PGTYPESnumeric_to_asc((numeric*)sqlda->sqlvar[i].sqldata, -1);
				printf("name sqlda descriptor: '%s' value NUMERIC '%s'\n", sqlda->sqlvar[i].sqlname.data, val);
				PGTYPESchar_free(val);
				break;
			}
		}
	}
}

int
main (void)
{
/* exec sql begin declare section */
		  
		  
		
		

#line 71 "sqlda.pgc"
 char * stmt1 = "SELECT * FROM t1" ;
 
#line 72 "sqlda.pgc"
 char * stmt2 = "SELECT * FROM t1 WHERE id = ?" ;
 
#line 73 "sqlda.pgc"
 int rec ;
 
#line 74 "sqlda.pgc"
 int id ;
/* exec sql end declare section */
#line 75 "sqlda.pgc"


	char msg[128];

	ECPGdebug(1, stderr);

	strcpy(msg, "connect");
	{ ECPGconnect(__LINE__, 0, "ecpg1_regression" , NULL, NULL , "regress1", 0); 
#line 82 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 82 "sqlda.pgc"


	strcpy(msg, "set");
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "set datestyle to iso", ECPGt_EOIT, ECPGt_EORT);
#line 85 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 85 "sqlda.pgc"


	strcpy(msg, "create");
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "create table t1 ( id integer , t text , d1 numeric , d2 float8 , c char ( 10 ) , big bigint )", ECPGt_EOIT, ECPGt_EORT);
#line 95 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 95 "sqlda.pgc"


	strcpy(msg, "insert");
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "insert into t1 values ( 1 , 'a' , 1.0 , 1 , 'a' , 1111111111111111111 ) , ( 2 , null , null , null , null , null ) , ( 3 , 'c' , 0.0 , 3 , 'c' , 3333333333333333333 ) , ( 4 , 'd' , 'NaN' , 4 , 'd' , 4444444444444444444 ) , ( 5 , 'e' , 0.001234 , 5 , 'e' , 5555555555555555555 )", ECPGt_EOIT, ECPGt_EORT);
#line 103 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 103 "sqlda.pgc"


	strcpy(msg, "commit");
	{ ECPGtrans(__LINE__, NULL, "commit");
#line 106 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 106 "sqlda.pgc"


	/* SQLDA test for getting all records from a table */

	outp_sqlda = NULL;

	strcpy(msg, "prepare");
	{ ECPGprepare(__LINE__, NULL, 0, "st_id1", stmt1);
#line 113 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 113 "sqlda.pgc"


	strcpy(msg, "declare");
	/* declare mycur1 cursor for $1 */
#line 116 "sqlda.pgc"


	strcpy(msg, "open");
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "declare mycur1 cursor for $1", 
	ECPGt_char_variable,(ECPGprepared_statement(NULL, "st_id1", __LINE__)),(long)1,(long)1,(1)*sizeof(char), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EOIT, ECPGt_EORT);
#line 119 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 119 "sqlda.pgc"


	/* exec sql whenever not found  break ; */
#line 121 "sqlda.pgc"


	rec = 0;
	while (1)
	{
		strcpy(msg, "fetch");
		{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "fetch 1 from mycur1", ECPGt_EOIT, 
	ECPGt_sqlda, &outp_sqlda, 0L, 0L, 0L, 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EORT);
#line 127 "sqlda.pgc"

if (sqlca.sqlcode == ECPG_NOT_FOUND) break;
#line 127 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 127 "sqlda.pgc"


		printf("FETCH RECORD %d\n", ++rec);
		dump_sqlda(outp_sqlda);
	}

	/* exec sql whenever not found  continue ; */
#line 133 "sqlda.pgc"


	strcpy(msg, "close");
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "close mycur1", ECPGt_EOIT, ECPGt_EORT);
#line 136 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 136 "sqlda.pgc"


	strcpy(msg, "deallocate");
	{ ECPGdeallocate(__LINE__, 0, NULL, "st_id1");
#line 139 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 139 "sqlda.pgc"


	free(outp_sqlda);

	/* SQLDA test for getting ALL records into the sqlda list */

	outp_sqlda = NULL;

	strcpy(msg, "prepare");
	{ ECPGprepare(__LINE__, NULL, 0, "st_id2", stmt1);
#line 148 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 148 "sqlda.pgc"


	strcpy(msg, "declare");
	/* declare mycur2 cursor for $1 */
#line 151 "sqlda.pgc"


	strcpy(msg, "open");
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "declare mycur2 cursor for $1", 
	ECPGt_char_variable,(ECPGprepared_statement(NULL, "st_id2", __LINE__)),(long)1,(long)1,(1)*sizeof(char), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EOIT, ECPGt_EORT);
#line 154 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 154 "sqlda.pgc"


	strcpy(msg, "fetch");
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "fetch all from mycur2", ECPGt_EOIT, 
	ECPGt_sqlda, &outp_sqlda, 0L, 0L, 0L, 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EORT);
#line 157 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 157 "sqlda.pgc"


	outp_sqlda1 = outp_sqlda;
	rec = 0;
	while (outp_sqlda1)
	{
		sqlda_t	*ptr;
		printf("FETCH RECORD %d\n", ++rec);
		dump_sqlda(outp_sqlda1);

		ptr = outp_sqlda1;
		outp_sqlda1 = outp_sqlda1->desc_next;
		free(ptr);
	}

	strcpy(msg, "close");
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "close mycur2", ECPGt_EOIT, ECPGt_EORT);
#line 173 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 173 "sqlda.pgc"


	strcpy(msg, "deallocate");
	{ ECPGdeallocate(__LINE__, 0, NULL, "st_id2");
#line 176 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 176 "sqlda.pgc"


	/* SQLDA test for getting one record using an input descriptor */

	/*
	 * Input sqlda has to be built manually
	 * sqlda_t contains 1 sqlvar_t structure already.
	 */
	inp_sqlda = (sqlda_t *)malloc(sizeof(sqlda_t));
	memset(inp_sqlda, 0, sizeof(sqlda_t));
	inp_sqlda->sqln = 1;

	inp_sqlda->sqlvar[0].sqltype = ECPGt_int;
	inp_sqlda->sqlvar[0].sqldata = (char *)&id;

	printf("EXECUTE RECORD 4\n");

	id = 4;

	outp_sqlda = NULL;

	strcpy(msg, "prepare");
	{ ECPGprepare(__LINE__, NULL, 0, "st_id3", stmt2);
#line 198 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 198 "sqlda.pgc"


	strcpy(msg, "execute");
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_execute, "st_id3", 
	ECPGt_sqlda, &inp_sqlda, 0L, 0L, 0L, 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EOIT, 
	ECPGt_sqlda, &outp_sqlda, 0L, 0L, 0L, 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EORT);
#line 201 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 201 "sqlda.pgc"


	dump_sqlda(outp_sqlda);

	strcpy(msg, "deallocate");
	{ ECPGdeallocate(__LINE__, 0, NULL, "st_id3");
#line 206 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 206 "sqlda.pgc"


	free(inp_sqlda);
	free(outp_sqlda);

	/* SQLDA test for getting one record using an input descriptor
	 * on a named connection
	 */

	{ ECPGconnect(__LINE__, 0, "ecpg1_regression" , NULL, NULL , "con2", 0); 
#line 215 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 215 "sqlda.pgc"


	/*
	 * Input sqlda has to be built manually
	 * sqlda_t contains 1 sqlvar_t structure already.
	 */
	inp_sqlda = (sqlda_t *)malloc(sizeof(sqlda_t));
	memset(inp_sqlda, 0, sizeof(sqlda_t));
	inp_sqlda->sqln = 1;

	inp_sqlda->sqlvar[0].sqltype = ECPGt_int;
	inp_sqlda->sqlvar[0].sqldata = (char *)&id;

	printf("EXECUTE RECORD 4\n");

	id = 4;

	outp_sqlda = NULL;

	strcpy(msg, "prepare");
	{ ECPGprepare(__LINE__, "con2", 0, "st_id4", stmt2);
#line 235 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 235 "sqlda.pgc"


	strcpy(msg, "execute");
	{ ECPGdo(__LINE__, 0, 1, "con2", 0, ECPGst_execute, "st_id4", 
	ECPGt_sqlda, &inp_sqlda, 0L, 0L, 0L, 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EOIT, 
	ECPGt_sqlda, &outp_sqlda, 0L, 0L, 0L, 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EORT);
#line 238 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 238 "sqlda.pgc"


	dump_sqlda(outp_sqlda);

	strcpy(msg, "commit");
	{ ECPGtrans(__LINE__, "con2", "commit");
#line 243 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 243 "sqlda.pgc"


	strcpy(msg, "deallocate");
	{ ECPGdeallocate(__LINE__, 0, NULL, "st_id4");
#line 246 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 246 "sqlda.pgc"


	free(inp_sqlda);
	free(outp_sqlda);

	strcpy(msg, "disconnect");
	{ ECPGdisconnect(__LINE__, "con2");
#line 252 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 252 "sqlda.pgc"


	/* End test */

	strcpy(msg, "drop");
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "drop table t1", ECPGt_EOIT, ECPGt_EORT);
#line 257 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 257 "sqlda.pgc"


	strcpy(msg, "commit");
	{ ECPGtrans(__LINE__, NULL, "commit");
#line 260 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 260 "sqlda.pgc"


	strcpy(msg, "disconnect");
	{ ECPGdisconnect(__LINE__, "CURRENT");
#line 263 "sqlda.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 263 "sqlda.pgc"


	return 0;
}
