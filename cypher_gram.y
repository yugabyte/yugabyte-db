%{
#include "postgres.h"

#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "parser/parser.h"

#include "cypher_gram.h"
#include "cypher_nodes.h"
#include "nodes.h"
#include "scan.h"

// override the default action for locations
#define YYLLOC_DEFAULT(current, rhs, n) \
    do \
    { \
        if ((n) > 0) \
            current = (rhs)[1]; \
        else \
            current = -1; \
    } while (0)

#define YYMALLOC palloc
#define YYFREE pfree
%}

%locations
%name-prefix="cypher_yy"
%pure-parser

%lex-param {ag_scanner_t scanner}
%parse-param {ag_scanner_t scanner}
%parse-param {cypher_yy_extra *extra}

%union {
    /* basic types */
    int integer;
    char *string;
    bool boolean;
    Node *node;

    /* specific types */
    List *list;
}

%token <integer> INTEGER
%token <string> DECIMAL_P STRING

%token <string> IDENTIFIER
%token <string> PARAMETER

/* operators that have more than 1 character */
%token NOT_EQ LT_EQ GT_EQ DOT_DOT PLUS_EQ EQ_TILDE

/* keywords in alphabetical order */
%token AS ASC ASCENDING
       BY
       DESC DESCENDING DISTINCT
       FALSE_P
       LIMIT
       NULL_P
       ORDER
       RETURN
       SKIP
       TRUE_P
       WHERE
       WITH

/* query */
%type <list> single_query
%type <list> with_chain_opt with_chain

/* RETURN and WITH clause */
%type <node> return return_item sort_item skip_opt limit_opt with
%type <boolean> distinct_opt
%type <list> return_items order_by_opt sort_items
%type <integer> order_opt

/* common */
%type <node> where_opt

/* expression */
%type <node> expr atom literal var

/* precedence: lowest to highest */
%nonassoc '=' NOT_EQ '<' LT_EQ '>' GT_EQ
%left '+' '-'
%left '*' '/' '%'
%left '^'
%right UNARY_OP

%{
static Node *make_int_const(int i, int location);
static Node *make_float_const(char *s, int location);
static Node *make_string_const(char *s, int location);
static Node *make_bool_const(bool b, int location);
static Node *make_null_const(int location);
static Node *make_type_cast(Node *arg, TypeName *typename, int location);
%}

%%

stmt:
    single_query semicolon_opt
        {
            extra->result = $1;
        }
    ;

single_query:
    with_chain_opt return
        {
            if ($1)
            {
                $$ = lappend($1, $2);
            }
            else
            {
                $$ = list_make1($2);
            }
        }
    ;

with_chain_opt:
    /* empty */
        {
            $$ = NIL;
        }
    | with_chain
        {
            $$ = $1;
        }
    ;

with_chain:
    with
        {
            $$ = list_make1($1);
        }
    | with_chain with
        {
            $$ = lappend($1, $2);
        }
    ;

semicolon_opt:
    /* empty */
    | ';'
    ;

return:
    RETURN distinct_opt return_items order_by_opt skip_opt limit_opt
        {
            cypher_return *n;

            n = make_ag_node(cypher_return);
            n->distinct = $2;
            n->items = $3;
            n->order_by = $4;
            n->skip = $5;
            n->limit = $6;

            $$ = (Node *)n;
        }
    ;

distinct_opt:
    /* empty */
        {
            $$ = false;
        }
    | DISTINCT
        {
            $$ = true;
        }
    ;

return_items:
    return_item
        {
            $$ = list_make1($1);
        }
    | return_items ',' return_item
        {
            $$ = lappend($1, $3);
        }
    ;

return_item:
    expr AS var
        {
            cypher_return_item *n;

            n = make_ag_node(cypher_return_item);
            n->expr = $1;
            n->name = (ColumnRef *)$3;

            $$ = (Node *)n;
        }
    | expr
        {
            cypher_return_item *n;

            n = make_ag_node(cypher_return_item);
            n->expr = $1;
            n->name = NULL;

            $$ = (Node *)n;
        }
    ;

order_by_opt:
    /* empty */
        {
            $$ = NIL;
        }
    | ORDER BY sort_items
        {
            $$ = $3;
        }
    ;

sort_items:
    sort_item
        {
            $$ = list_make1($1);
        }
    | sort_items ',' sort_item
        {
            $$ = lappend($1, $3);
        }
    ;

sort_item:
    expr order_opt
        {
            cypher_sort_item *n;

            n = make_ag_node(cypher_sort_item);
            n->expr = $1;
            n->order = $2;

            $$ = (Node *)n;
        }
    ;

order_opt:
    /* empty */
        {
            $$ = CYPHER_ORDER_ASC;
        }
    | ASC
        {
            $$ = CYPHER_ORDER_ASC;
        }
    | ASCENDING
        {
            $$ = CYPHER_ORDER_ASC;
        }
    | DESC
        {
            $$ = CYPHER_ORDER_DESC;
        }
    | DESCENDING
        {
            $$ = CYPHER_ORDER_DESC;
        }
    ;

skip_opt:
    /* empty */
        {
            $$ = NULL;
        }
    | SKIP expr
        {
            $$ = $2;
        }
    ;

limit_opt:
    /* empty */
        {
            $$ = NULL;
        }
    | LIMIT expr
        {
            $$ = $2;
        }
    ;

with:
    WITH distinct_opt return_items order_by_opt skip_opt limit_opt where_opt
        {
            cypher_with *n;

            n = make_ag_node(cypher_with);
            n->distinct = $2;
            n->items = $3;
            n->order_by = $4;
            n->skip = $5;
            n->limit = $6;
            n->where = $7;

            $$ = (Node *)n;
        }
    ;

where_opt:
    /* empty */
        {
            $$ = NULL;
        }
    | WHERE expr
        {
            $$ = $2;
        }
    ;

expr:
    atom
        {
            $$ = $1;
        }
    ;

atom:
    literal
        {
            $$ = $1;
        }
    | var
        {
            $$ = $1;
        }
    ;

literal:
    INTEGER
        {
            $$ = make_int_const($1, @1);
        }
    | DECIMAL_P
        {
            $$ = make_float_const($1, @1);
        }
    | STRING
        {
            $$ = make_string_const($1, @1);
        }
    | TRUE_P
        {
            $$ = make_bool_const(true, @1);
        }
    | FALSE_P
        {
            $$ = make_bool_const(false, @1);
        }
    | NULL_P
        {
            $$ = make_null_const(@1);
        }
    ;

var:
    IDENTIFIER
        {
            ColumnRef *n;

            n = makeNode(ColumnRef);
            n->fields = list_make1(makeString($1));
            n->location = @1;

            $$ = (Node *)n;
        }
    ;

%%

static Node *make_int_const(int i, int location)
{
    A_Const *n;

    n = makeNode(A_Const);
    n->val.type = T_Integer;
    n->val.val.ival = i;
    n->location = location;

    return (Node *)n;
}

static Node *make_float_const(char *s, int location)
{
    A_Const *n;

    n = makeNode(A_Const);
    n->val.type = T_Float;
    n->val.val.str = s;
    n->location = location;

    return (Node *)n;
}

static Node *make_string_const(char *s, int location)
{
    A_Const *n;

    n = makeNode(A_Const);
    n->val.type = T_String;
    n->val.val.str = s;
    n->location = location;

    return (Node *)n;
}

static Node *make_bool_const(bool b, int location)
{
    A_Const *n;

    n = makeNode(A_Const);
    n->val.type = T_String;
    n->val.val.str = (b ? "t" : "f");
    n->location = location;

    return make_type_cast((Node *)n, SystemTypeName("bool"), -1);
}

static Node *make_null_const(int location)
{
    A_Const *n;

    n = makeNode(A_Const);
    n->val.type = T_Null;
    n->location = location;

    return (Node *)n;
}

static Node *make_type_cast(Node *arg, TypeName *typename, int location)
{
    TypeCast *n;

    n = makeNode(TypeCast);
    n->arg = arg;
    n->typeName = typename;
    n->location = location;

    return (Node *)n;
}
