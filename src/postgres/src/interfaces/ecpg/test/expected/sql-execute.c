/* Processed by ecpg (regression mode) */
/* These include files are added by the preprocessor */
#include <ecpglib.h>
#include <ecpgerrno.h>
#include <sqlca.h>
/* End of automatic include section */
#define ECPGdebug(X,Y) ECPGdebug((X)+100,(Y))

#line 1 "execute.pgc"
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


#line 1 "regression.h"






#line 6 "execute.pgc"


/* exec sql whenever sqlerror  sqlprint ; */
#line 8 "execute.pgc"


int
main(void)
{
/* exec sql begin declare section */
	 
	 
	 
	 
	 

#line 14 "execute.pgc"
 int amount [ 8 ] ;
 
#line 15 "execute.pgc"
 int increment = 100 ;
 
#line 16 "execute.pgc"
 char name [ 8 ] [ 8 ] ;
 
#line 17 "execute.pgc"
 char letter [ 8 ] [ 1 ] ;
 
#line 18 "execute.pgc"
 char command [ 128 ] ;
/* exec sql end declare section */
#line 19 "execute.pgc"

	int i,j;

	ECPGdebug(1, stderr);

	{ ECPGconnect(__LINE__, 0, "ecpg1_regression" , NULL, NULL , "main", 0); 
#line 24 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 24 "execute.pgc"

	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "create table test ( name char ( 8 ) , amount int , letter char ( 1 ) )", ECPGt_EOIT, ECPGt_EORT);
#line 25 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 25 "execute.pgc"

	{ ECPGtrans(__LINE__, NULL, "commit");
#line 26 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 26 "execute.pgc"


	/* test handling of embedded quotes in EXECUTE IMMEDIATE "literal" */
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_exec_immediate, "insert into test (name, \042amount\042, letter) values ('db: ''r1''', 1, 'f')", ECPGt_EOIT, ECPGt_EORT);
#line 29 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 29 "execute.pgc"


	sprintf(command, "insert into test (name, amount, letter) values ('db: ''r1''', 2, 't')");
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_exec_immediate, command, ECPGt_EOIT, ECPGt_EORT);
#line 32 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 32 "execute.pgc"


	sprintf(command, "insert into test (name, amount, letter) select name, amount+10, letter from test");
	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_exec_immediate, command, ECPGt_EOIT, ECPGt_EORT);
#line 35 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 35 "execute.pgc"


	printf("Inserted %ld tuples via execute immediate\n", sqlca.sqlerrd[2]);

	sprintf(command, "insert into test (name, amount, letter) select name, amount+$1, letter from test");
	{ ECPGprepare(__LINE__, NULL, 0, "i", command);
#line 40 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 40 "execute.pgc"

	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_execute, "i", 
	ECPGt_int,&(increment),(long)1,(long)1,sizeof(int), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EOIT, ECPGt_EORT);
#line 41 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 41 "execute.pgc"


	printf("Inserted %ld tuples via prepared execute\n", sqlca.sqlerrd[2]);

	{ ECPGtrans(__LINE__, NULL, "commit");
#line 45 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 45 "execute.pgc"


	sprintf (command, "select * from test");

	{ ECPGprepare(__LINE__, NULL, 0, "f", command);
#line 49 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 49 "execute.pgc"

	/* declare CUR cursor for $1 */
#line 50 "execute.pgc"


	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "declare CUR cursor for $1", 
	ECPGt_char_variable,(ECPGprepared_statement(NULL, "f", __LINE__)),(long)1,(long)1,(1)*sizeof(char), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EOIT, ECPGt_EORT);
#line 52 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 52 "execute.pgc"

	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "fetch 8 in CUR", ECPGt_EOIT, 
	ECPGt_char,(name),(long)8,(long)8,(8)*sizeof(char), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, 
	ECPGt_int,(amount),(long)1,(long)8,sizeof(int), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, 
	ECPGt_char,(letter),(long)1,(long)8,(1)*sizeof(char), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EORT);
#line 53 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 53 "execute.pgc"


	for (i=0, j=sqlca.sqlerrd[2]; i<j; i++)
	{
		/* exec sql begin declare section */
		    
		   
		
#line 58 "execute.pgc"
 char n [ 8 ] , l = letter [ i ] [ 0 ] ;
 
#line 59 "execute.pgc"
 int a = amount [ i ] ;
/* exec sql end declare section */
#line 60 "execute.pgc"


		strncpy(n, name[i], 8);
		printf("name[%d]=%8.8s\tamount[%d]=%d\tletter[%d]=%c\n", i, n, i, a, i, l);
	}

	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "close CUR", ECPGt_EOIT, ECPGt_EORT);
#line 66 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 66 "execute.pgc"

	{ ECPGdeallocate(__LINE__, 0, NULL, "f");
#line 67 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 67 "execute.pgc"


	sprintf (command, "select * from test where amount = $1");

	{ ECPGprepare(__LINE__, NULL, 0, "f", command);
#line 71 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 71 "execute.pgc"

	/* declare CUR2 cursor for $1 */
#line 72 "execute.pgc"


	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "declare CUR2 cursor for $1", 
	ECPGt_char_variable,(ECPGprepared_statement(NULL, "f", __LINE__)),(long)1,(long)1,(1)*sizeof(char), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, 
	ECPGt_const,"1",(long)1,(long)1,strlen("1"), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EOIT, ECPGt_EORT);
#line 74 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 74 "execute.pgc"

	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "fetch in CUR2", ECPGt_EOIT, 
	ECPGt_char,(name),(long)8,(long)8,(8)*sizeof(char), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, 
	ECPGt_int,(amount),(long)1,(long)8,sizeof(int), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, 
	ECPGt_char,(letter),(long)1,(long)8,(1)*sizeof(char), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EORT);
#line 75 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 75 "execute.pgc"


	for (i=0, j=sqlca.sqlerrd[2]; i<j; i++)
	{
		/* exec sql begin declare section */
		    
		   
		
#line 80 "execute.pgc"
 char n [ 8 ] , l = letter [ i ] [ 0 ] ;
 
#line 81 "execute.pgc"
 int a = amount [ i ] ;
/* exec sql end declare section */
#line 82 "execute.pgc"


		strncpy(n, name[i], 8);
		printf("name[%d]=%8.8s\tamount[%d]=%d\tletter[%d]=%c\n", i, n, i, a, i, l);
	}

	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "close CUR2", ECPGt_EOIT, ECPGt_EORT);
#line 88 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 88 "execute.pgc"

	{ ECPGdeallocate(__LINE__, 0, NULL, "f");
#line 89 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 89 "execute.pgc"


	sprintf (command, "select * from test where amount = $1");

	{ ECPGprepare(__LINE__, NULL, 0, "f", command);
#line 93 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 93 "execute.pgc"

	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_execute, "f", 
	ECPGt_const,"2",(long)1,(long)1,strlen("2"), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EOIT, 
	ECPGt_char,(name),(long)8,(long)8,(8)*sizeof(char), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, 
	ECPGt_int,(amount),(long)1,(long)8,sizeof(int), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, 
	ECPGt_char,(letter),(long)1,(long)8,(1)*sizeof(char), 
	ECPGt_NO_INDICATOR, NULL , 0L, 0L, 0L, ECPGt_EORT);
#line 94 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 94 "execute.pgc"


	for (i=0, j=sqlca.sqlerrd[2]; i<j; i++)
	{
		/* exec sql begin declare section */
		    
		   
		
#line 99 "execute.pgc"
 char n [ 8 ] , l = letter [ i ] [ 0 ] ;
 
#line 100 "execute.pgc"
 int a = amount [ i ] ;
/* exec sql end declare section */
#line 101 "execute.pgc"


		strncpy(n, name[i], 8);
		printf("name[%d]=%8.8s\tamount[%d]=%d\tletter[%d]=%c\n", i, n, i, a, i, l);
	}

	{ ECPGdeallocate(__LINE__, 0, NULL, "f");
#line 107 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 107 "execute.pgc"

	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "drop table test", ECPGt_EOIT, ECPGt_EORT);
#line 108 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 108 "execute.pgc"

	{ ECPGtrans(__LINE__, NULL, "commit");
#line 109 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 109 "execute.pgc"

	{ ECPGdisconnect(__LINE__, "CURRENT");
#line 110 "execute.pgc"

if (sqlca.sqlcode < 0) sqlprint();}
#line 110 "execute.pgc"


	return 0;
}
