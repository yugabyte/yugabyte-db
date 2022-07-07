/*
 * %define api.prefix {orafce_sql_yy} is not compileable on old bison 2.4
 * so I am using obsolete but still working option.
 */

%name-prefix "orafce_sql_yy"

%{

#define YYDEBUG 1

#define YYLLOC_DEFAULT(Current, Rhs, N) \
do { \
if (N) \
(Current) = (Rhs)[1]; \
else \
(Current) = (Rhs)[0]; \
} while (0)

#include "postgres.h"
#include "orafce.h"
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
    __node->typenode = X_##type, \
    __node->classname = #type, \
    FILL_NODE(src,__node), \
    __node)


extern int yylex(void);      /* defined as fdate_yylex in fdatescan.l */

static char *scanbuf;
static int	scanbuflen;

void orafce_sql_yyerror(List **result, const char *message);

#define YYMALLOC	malloc	/* XXX: should use palloc? */
#define YYFREE		free	/* XXX: should use pfree? */

%}

%locations
%parse-param {List **result}

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
%token <val>    X_IDENT X_NCONST X_SCONST X_OP X_PARAM X_COMMENT X_WHITESPACE X_KEYWORD X_OTHERS X_TYPECAST

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
		X_IDENT		{ $$ = (orafce_lexnode*) CREATE_NODE($1, IDENT);  }
		| X_NCONST	{ $$ = (orafce_lexnode*) CREATE_NODE($1, NCONST); }
		| X_SCONST	{ $$ = (orafce_lexnode*) CREATE_NODE($1, SCONST); }
		| X_OP		{ $$ = (orafce_lexnode*) CREATE_NODE($1, OP);    }
		| X_PARAM		{ $$ = (orafce_lexnode*) CREATE_NODE($1, PARAM); }
		| X_COMMENT	{ $$ = (orafce_lexnode*) CREATE_NODE($1, COMMENT);    }
		| X_WHITESPACE	{ $$ = (orafce_lexnode*) CREATE_NODE($1, WHITESPACE); }
		| X_KEYWORD	{ $$ = (orafce_lexnode*) CREATE_NODE($1, KEYWORD); }
		| X_OTHERS	{ $$ = (orafce_lexnode*) CREATE_NODE($1, OTHERS);  }
	;
%%

#undef YYLTYPE

#include "sqlscan.c"
