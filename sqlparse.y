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
    int			ival;
    double		dval;
    char		*str;
}
    
/* BISON Declarations */
%token ICONST DCONST DPOINTS SCONST
%start root

%type <ival> ICONST iconst
%type <dval> DCONST dconst
%type <str> SCONST sconst
%type <node> root

/* Grammar follows */
%%

/* ext_dow_lst and ext_number_lst I need for updates */

root:	ICONST
	    {
		    *((void **) result) = $1;
		    elog(NOTICE, "ICONST \"%d\"", $1);
	    }
	| SCONST
	    {
		*((void **) result) = $1;
		elog(NOTICE, "SCONST \"%s\"", $1);
	    }

	;

	
iconst:		ICONST		{ $$ = $1;}
dconst:		DCONST		{ $$ = $1;}
sconst:		SCONST		{ $$ = $1;}
%%

#include "sqlscan.c"
