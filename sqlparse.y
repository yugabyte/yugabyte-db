%{

#define YYPARSE_PARAM result  /* need this to pass a pointer (void *) to yyparse */
#define YYDEBUG 1

#include "postgres.h"

#undef yylex                 /* failure to redefine yylex will result in a call to  the */
#define yylex orafce_sql_yylex     /* wrong scanner when running inside the postgres backend  */

extern int yylex(void);      /* defined as fdate_yylex in fdatescan.l */

static char *scanbuf;
static int	scanbuflen;

void orafce_sql_yyerror(const char *message);
int orafce_sql_yyparse(void *result);

%}
%union
{
    char		*str;
}
    
/* BISON Declarations */
%token ICONST DCONST DPOINTS SCONST OTHERS
%start root

%type <str> ICONST iconst
%type <str> DCONST dconst
%type <str> SCONST sconst
%type <str> OTHERS others
%type <str> root
%type <str> anyelement
%type <str> anyelementlist

/* Grammar follows */
%%

/* ext_dow_lst and ext_number_lst I need for updates */

root: anyelementlist 	{ $$ = $1; }
	;

anyelementlist:
		    anyelement		{ $$ = $1; };
		    | anyelementlist anyelement {$$ = $1; };
		;

anyelement:
	iconst
	    {
		    *((void **) result) = $1;
		    elog(NOTICE, "ICONST \"%s\"", $1);
	    }
	| sconst
	    {
		*((void **) result) = $1;
		elog(NOTICE, "SCONST \"%s\"", $1);
	    }
	| dconst
	    {
		*((void **) result) = $1;
		elog(NOTICE, "DCONST \"%s\"", $1);
	    }
	| others
	    {
		*((void **) result) = $1;
		elog(NOTICE, "OTHERS \"%s\"", $1);
	    }    
	;

	
iconst:		ICONST		{ $$ = $1;}
dconst:		DCONST		{ $$ = $1;}
sconst:		SCONST		{ $$ = $1;}
others:		OTHERS		{ $$ = $1;}
%%

#include "sqlscan.c"
