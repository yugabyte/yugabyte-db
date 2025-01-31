/* Processed by ecpg (regression mode) */
/* These include files are added by the preprocessor */
#include <ecpglib.h>
#include <ecpgerrno.h>
#include <sqlca.h>
/* End of automatic include section */
#define ECPGdebug(X,Y) ECPGdebug((X)+100,(Y))

#line 1 "outofscope.pgc"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>


#line 1 "regression.h"






#line 6 "outofscope.pgc"



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

#line 8 "outofscope.pgc"


/* exec sql begin declare section */

#line 1 "struct.h"
 

				
			
							/* dec_t */
			
			

   typedef struct mytype  MYTYPE ;

#line 9 "struct.h"


 

				
				
				
				
				

   typedef struct mynulltype  MYNULLTYPE ;

#line 19 "struct.h"


#line 11 "outofscope.pgc"

struct mytype { 
#line 3 "struct.h"
 int id ;
 
#line 4 "struct.h"
 char t [ 64 ] ;
 
#line 5 "struct.h"
 double d1 ;
 
#line 6 "struct.h"
 double d2 ;
 
#line 7 "struct.h"
 char c [ 30 ] ;
 } ; struct mynulltype { 
#line 13 "struct.h"
 int id ;
 
#line 14 "struct.h"
 int t ;
 
#line 15 "struct.h"
 int d1 ;
 
#line 16 "struct.h"
 int d2 ;
 
#line 17 "struct.h"
 int c ;
 } ;/* exec sql end declare section */
#line 12 "outofscope.pgc"


/* exec sql whenever sqlerror  stop ; */
#line 14 "outofscope.pgc"


/* Functions for test 1 */

static void
get_var1(MYTYPE **myvar0, MYNULLTYPE **mynullvar0)
{
	/* exec sql begin declare section */
			  
		  
	
#line 22 "outofscope.pgc"
 MYTYPE * myvar = malloc ( sizeof ( MYTYPE ) ) ;
 
#line 23 "outofscope.pgc"
 MYNULLTYPE * mynullvar = malloc ( sizeof ( MYNULLTYPE ) ) ;
/* exec sql end declare section */
#line 24 "outofscope.pgc"


	/* Test DECLARE ... SELECT ... INTO with pointers */

	ECPGset_var( 0, ( myvar ), __LINE__);\
 ECPGset_var( 1, ( mynullvar ), __LINE__);\
 /* declare mycur cursor for select * from a1 */
#line 28 "outofscope.pgc"

if (sqlca.sqlcode < 0) exit (1);
#line 28 "outofscope.pgc"

#line 28 "outofscope.pgc"


	if (sqlca.sqlcode != 0)
		exit(1);

	*myvar0 = myvar;
	*mynullvar0 = mynullvar;
}

static void
open_cur1(void)
{
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "declare mycur cursor for select * from a1", ECPGt_EOIT, 
	ECPGt_int,&((*( MYTYPE  *)(ECPGget_var( 0)) ).id),(long)1,(long)1,sizeof( struct mytype ), 
	ECPGt_int,&((*( MYNULLTYPE  *)(ECPGget_var( 1)) ).id),(long)1,(long)1,sizeof( struct mynulltype ), 
	ECPGt_char,&((*( MYTYPE  *)(ECPGget_var( 0)) ).t),(long)64,(long)1,sizeof( struct mytype ), 
	ECPGt_int,&((*( MYNULLTYPE  *)(ECPGget_var( 1)) ).t),(long)1,(long)1,sizeof( struct mynulltype ), 
	ECPGt_double,&((*( MYTYPE  *)(ECPGget_var( 0)) ).d1),(long)1,(long)1,sizeof( struct mytype ), 
	ECPGt_int,&((*( MYNULLTYPE  *)(ECPGget_var( 1)) ).d1),(long)1,(long)1,sizeof( struct mynulltype ), 
	ECPGt_double,&((*( MYTYPE  *)(ECPGget_var( 0)) ).d2),(long)1,(long)1,sizeof( struct mytype ), 
	ECPGt_int,&((*( MYNULLTYPE  *)(ECPGget_var( 1)) ).d2),(long)1,(long)1,sizeof( struct mynulltype ), 
	ECPGt_char,&((*( MYTYPE  *)(ECPGget_var( 0)) ).c),(long)30,(long)1,sizeof( struct mytype ), 
	ECPGt_int,&((*( MYNULLTYPE  *)(ECPGget_var( 1)) ).c),(long)1,(long)1,sizeof( struct mynulltype ), ECPGt_EORT);
#line 40 "outofscope.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 40 "outofscope.pgc"

}

static void
get_record1(void)
{
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "fetch mycur", ECPGt_EOIT, 
	ECPGt_int,&((*( MYTYPE  *)(ECPGget_var( 0)) ).id),(long)1,(long)1,sizeof( struct mytype ), 
	ECPGt_int,&((*( MYNULLTYPE  *)(ECPGget_var( 1)) ).id),(long)1,(long)1,sizeof( struct mynulltype ), 
	ECPGt_char,&((*( MYTYPE  *)(ECPGget_var( 0)) ).t),(long)64,(long)1,sizeof( struct mytype ), 
	ECPGt_int,&((*( MYNULLTYPE  *)(ECPGget_var( 1)) ).t),(long)1,(long)1,sizeof( struct mynulltype ), 
	ECPGt_double,&((*( MYTYPE  *)(ECPGget_var( 0)) ).d1),(long)1,(long)1,sizeof( struct mytype ), 
	ECPGt_int,&((*( MYNULLTYPE  *)(ECPGget_var( 1)) ).d1),(long)1,(long)1,sizeof( struct mynulltype ), 
	ECPGt_double,&((*( MYTYPE  *)(ECPGget_var( 0)) ).d2),(long)1,(long)1,sizeof( struct mytype ), 
	ECPGt_int,&((*( MYNULLTYPE  *)(ECPGget_var( 1)) ).d2),(long)1,(long)1,sizeof( struct mynulltype ), 
	ECPGt_char,&((*( MYTYPE  *)(ECPGget_var( 0)) ).c),(long)30,(long)1,sizeof( struct mytype ), 
	ECPGt_int,&((*( MYNULLTYPE  *)(ECPGget_var( 1)) ).c),(long)1,(long)1,sizeof( struct mynulltype ), ECPGt_EORT);
#line 46 "outofscope.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 46 "outofscope.pgc"

}

static void
close_cur1(void)
{
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "close mycur", ECPGt_EOIT, ECPGt_EORT);
#line 52 "outofscope.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 52 "outofscope.pgc"

}

int
main (void)
{
	MYTYPE		*myvar;
	MYNULLTYPE	*mynullvar;
	int loopcount;
	char msg[128];

	ECPGdebug(1, stderr);

	strcpy(msg, "connect");
	{ ECPGconnect(__LINE__, 0, "ecpg1_regression" , NULL, NULL , NULL, 0); 
#line 66 "outofscope.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 66 "outofscope.pgc"


	strcpy(msg, "set");
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "set datestyle to iso", ECPGt_EOIT, ECPGt_EORT);
#line 69 "outofscope.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 69 "outofscope.pgc"


	strcpy(msg, "create");
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "create table a1 ( id serial primary key , t text , d1 numeric , d2 float8 , c character ( 10 ) )", ECPGt_EOIT, ECPGt_EORT);
#line 72 "outofscope.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 72 "outofscope.pgc"


	strcpy(msg, "insert");
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "insert into a1 ( id , t , d1 , d2 , c ) values ( default , 'a' , 1.0 , 2 , 'a' )", ECPGt_EOIT, ECPGt_EORT);
#line 75 "outofscope.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 75 "outofscope.pgc"

	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "insert into a1 ( id , t , d1 , d2 , c ) values ( default , null , null , null , null )", ECPGt_EOIT, ECPGt_EORT);
#line 76 "outofscope.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 76 "outofscope.pgc"

	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "insert into a1 ( id , t , d1 , d2 , c ) values ( default , 'b' , 2.0 , 3 , 'b' )", ECPGt_EOIT, ECPGt_EORT);
#line 77 "outofscope.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 77 "outofscope.pgc"


	strcpy(msg, "commit");
	{ ECPGtrans(__LINE__, NULL, "commit");
#line 80 "outofscope.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 80 "outofscope.pgc"


	/* Test out-of-scope DECLARE/OPEN/FETCH/CLOSE */

	get_var1(&myvar, &mynullvar);
	open_cur1();

	for (loopcount = 0; loopcount < 100; loopcount++)
	{
		memset(myvar, 0, sizeof(MYTYPE));
		get_record1();
		if (sqlca.sqlcode == ECPG_NOT_FOUND)
			break;
		printf("id=%d%s t='%s'%s d1=%f%s d2=%f%s c = '%s'%s\n",
			myvar->id, mynullvar->id ? " (NULL)" : "",
			myvar->t, mynullvar->t ? " (NULL)" : "",
			myvar->d1, mynullvar->d1 ? " (NULL)" : "",
			myvar->d2, mynullvar->d2 ? " (NULL)" : "",
			myvar->c, mynullvar->c ? " (NULL)" : "");
	}

	close_cur1();

	free(myvar);
	free(mynullvar);

	strcpy(msg, "drop");
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "drop table a1", ECPGt_EOIT, ECPGt_EORT);
#line 107 "outofscope.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 107 "outofscope.pgc"


	strcpy(msg, "commit");
	{ ECPGtrans(__LINE__, NULL, "commit");
#line 110 "outofscope.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 110 "outofscope.pgc"


	strcpy(msg, "disconnect");
	{ ECPGdisconnect(__LINE__, "CURRENT");
#line 113 "outofscope.pgc"

if (sqlca.sqlcode < 0) exit (1);}
#line 113 "outofscope.pgc"


	return 0;
}
