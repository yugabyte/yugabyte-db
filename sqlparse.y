%{

#define YYPARSE_PARAM result  /* need this to pass a pointer (void *) to yyparse */
#define YYDEBUG 1

#define YYLLOC_DEFAULT(Current, Rhs, N) \
do { \
if (N) \
(Current) = (Rhs)[1]; \
else \
(Current) = (Rhs)[0]; \
} while (0)                      

#include "postgres.h"
#include "plvlex.h"
#include "nodes/pg_list.h"


#define MOVE_TO_S(src,dest,col)	dest->col = src.col ? pstrdup(src.col) : NULL
#define MOVE_TO(src,dest,col)	dest->col = src.col

#define FILL_NODE(src,dest)	\
	MOVE_TO_S(src,dest,str), \
	MOVE_TO(src,dest,keycode), \
	MOVE_TO(src,dest,lloc), \
	MOVE_TO_S(src,dest,sep), \
	MOVE_TO(src,dest,modificator)

static orafce_lexnode *__node;

#define CREATE_NODE(src,type) 	\
  ( \
    __node = (orafce_lexnode*) palloc(sizeof(orafce_lexnode)), \
    __node->typenode = type, \
    __node->classname = #type, \
    FILL_NODE(src,__node), \
    __node)
    
    


extern int yylex(void);      /* defined as fdate_yylex in fdatescan.l */

static char *scanbuf;
static int	scanbuflen;

void orafce_sql_yyerror(const char *message);


#define YYLTYPE int

%}
%name-prefix="orafce_sql_yy" 
%locations

%union
{
    int 	ival;
    orafce_lexnode	*node;
    List		*list;
    struct
    {
	    char 	*str;
	    int		keycode;
	    int		lloc;
	    char	*sep;
	    char *modificator;
    }				val;


}
    
/* BISON Declarations */
%token <val>    IDENT NCONST SCONST OP PARAM COMMENT WHITESPACE KEYWORD OTHERS TYPECAST

%type <list> elements
%type <node> anyelement
%type <list> root

%start root


/* Grammar follows */
%%

root:
	    elements { *((void**)result) = $1; }
	;

elements:
		anyelement { $$ = list_make1($1);}
		| elements anyelement { $$ = lappend($1, $2);}
	;

anyelement:
		IDENT		{ $$ = (orafce_lexnode*) CREATE_NODE($1, IDENT);  }
		| NCONST	{ $$ = (orafce_lexnode*) CREATE_NODE($1, NCONST); }
		| SCONST	{ $$ = (orafce_lexnode*) CREATE_NODE($1, SCONST); }
		| OP		{ $$ = (orafce_lexnode*) CREATE_NODE($1, OP);    }
		| PARAM		{ $$ = (orafce_lexnode*) CREATE_NODE($1, PARAM); }
		| COMMENT	{ $$ = (orafce_lexnode*) CREATE_NODE($1, COMMENT);    }
		| WHITESPACE	{ $$ = (orafce_lexnode*) CREATE_NODE($1, WHITESPACE); }
		| KEYWORD	{ $$ = (orafce_lexnode*) CREATE_NODE($1, KEYWORD); }
		| OTHERS	{ $$ = (orafce_lexnode*) CREATE_NODE($1, OTHERS);  }
	;
%%



#include "sqlscan.c"
