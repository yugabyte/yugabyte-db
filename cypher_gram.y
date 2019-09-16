%{
#include "postgres.h"

#include "nodes/makefuncs.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "nodes/primnodes.h"
#include "nodes/value.h"
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
    /* types in cypher_yylex() */
    int integer;
    char *string;
    const char *keyword;

    /* extra types */
    bool boolean;
    Node *node;
    List *list;
}

%token <integer> INTEGER
%token <string> DECIMAL STRING

%token <string> IDENTIFIER
%token <string> PARAMETER

/* operators that have more than 1 character */
%token NOT_EQ LT_EQ GT_EQ DOT_DOT PLUS_EQ EQ_TILDE

/* keywords in alphabetical order */
%token <keyword> AND AS ASC ASCENDING
                 BY
                 CONTAINS
                 DELETE DESC DESCENDING DETACH DISTINCT
                 ENDS
                 FALSE_P
                 IN IS
                 LIMIT
                 NOT NULL_P
                 OR ORDER
                 REMOVE RETURN
                 SET SKIP STARTS
                 TRUE_P
                 WHERE
                 WITH

/* query */
%type <list> single_query multi_part_query
%type <list> updating_clause
%type <list> updating_clause_chain_opt updating_clause_chain
%type <list> with_chain_opt with_chain

/* RETURN and WITH clause */
%type <node> return return_item sort_item skip_opt limit_opt with
%type <boolean> distinct_opt
%type <list> return_items order_by_opt sort_items
%type <integer> order_opt

/* SET and REMOVE clause */
%type <node> set set_item remove remove_item
%type <list> set_item_list remove_item_list

/* DELETE clause */
%type <node> delete
%type <boolean> detach_opt

/* common */
%type <node> where_opt

/* expression */
%type <node> expr expr_opt atom literal map list var
%type <list> expr_comma_list expr_comma_list_opt map_keyvals

/* identifier */
%type <string> name
%type <keyword> reserved_keyword

/* precedence: lowest to highest */
%left OR
%left AND
%right NOT
%nonassoc '=' NOT_EQ '<' LT_EQ '>' GT_EQ
%left '+' '-'
%left '*' '/' '%'
%left '^'
%nonassoc IN IS
%right UNARY_MINUS
%nonassoc STARTS ENDS CONTAINS
%left '[' ']' '(' ')'
%left '.'

%{
// logical operators
static Node *make_or_expr(Node *lexpr, Node *rexpr, int location);
static Node *make_and_expr(Node *lexpr, Node *rexpr, int location);
static Node *make_not_expr(Node *expr, int location);

// arithmetic operators
static Node *do_negate(Node *n, int location);
static void do_negate_float(Value *v);

// indirection
static Node *append_indirection(Node *expr, Node *selector);

// literals
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
    multi_part_query return
        {
            if ($1)
                $$ = lappend($1, $2);
            else
                $$ = list_make1($2);
        }
    ;

multi_part_query:
    updating_clause_chain_opt with_chain_opt
        {
            if ($1 && $2)
                $$ = lappend($1, $2);
            else if ($1)
                $$ = list_make1($1);
            else if ($2)
                $$ = list_make1($2);
            else
                $$ = NIL;
        }
    ;

updating_clause_chain_opt:
    /* empty */
        {
            $$ = NIL;
        }
    | updating_clause_chain
    ;

updating_clause_chain:
    updating_clause
        {
            $$ = list_make1($1);
        }
    | updating_clause_chain updating_clause
        {
            $$ = lappend($1, $2);
        }
    ;

updating_clause:
    delete
        {
            $$ = list_make1($1);
        }
    | set
        {
            $$ = list_make1($1);
        }
    | remove
        {
            $$ = list_make1($1);
        }
    ;

with_chain_opt:
    /* empty */
        {
            $$ = NIL;
        }
    | with_chain
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
    expr AS name
        {
            ResTarget *n;

            n = makeNode(ResTarget);
            n->name = $3;
            n->indirection = NIL;
            n->val = $1;
            n->location = @1;

            $$ = (Node *)n;
        }
    | expr
        {
            ResTarget *n;

            n = makeNode(ResTarget);
            n->name = NULL;
            n->indirection = NIL;
            n->val = $1;
            n->location = @1;

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
            SortBy *n;

            n = makeNode(SortBy);
            n->node = $1;
            n->sortby_dir = $2;
            n->sortby_nulls = SORTBY_NULLS_DEFAULT;
            n->useOp = NIL;
            n->location = -1; // no operator

            $$ = (Node *)n;
        }
    ;

order_opt:
    /* empty */
        {
            $$ = SORTBY_DEFAULT; // is the same with SORTBY_ASC
        }
    | ASC
        {
            $$ = SORTBY_ASC;
        }
    | ASCENDING
        {
            $$ = SORTBY_ASC;
        }
    | DESC
        {
            $$ = SORTBY_DESC;
        }
    | DESCENDING
        {
            $$ = SORTBY_DESC;
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

set:
    SET set_item_list
        {
            cypher_set *n;

            n = make_ag_node(cypher_set);
            n->items = $2;
            n->is_remove = false;

            $$ = (Node *)n;
        }
    ;

set_item_list:
    set_item
        {
            $$ = list_make1($1);
        }
    | set_item_list ',' set_item
        {
            $$ = lappend($1, $3);
        }
    ;

set_item:
    expr '=' expr
        {
            cypher_set_item *n;

            n = make_ag_node(cypher_set_item);
            n->prop = $1;
            n->expr = $3;
            n->is_add = false;

            $$ = (Node *)n;
        }
   | expr PLUS_EQ expr
        {
            cypher_set_item *n;

            n = make_ag_node(cypher_set_item);
            n->prop = $1;
            n->expr = $3;
            n->is_add = true;

            $$ = (Node *)n;
        }
    ;

remove:
    REMOVE remove_item_list
        {
            cypher_set *n;

            n = make_ag_node(cypher_set);
            n->items = $2;
            n->is_remove = true;

            $$ = (Node *)n;
        }
    ;

remove_item_list:
    remove_item
        {
            $$ = list_make1($1);
        }
    | remove_item_list ',' remove_item
        {
            $$ = lappend($1, $3);
        }
    ;

remove_item:
    expr
        {
            cypher_set_item *n;

            n = make_ag_node(cypher_set_item);
            n->prop = $1;
            n->expr = make_null_const(-1);
            n->is_add = false;

            $$ = (Node *) n;
        }
    ;

delete:
    detach_opt DELETE expr_comma_list
        {
            cypher_delete *n;

            n = make_ag_node(cypher_delete);
            n->detach = $1;
            n->exprs = $3;
            $$ = (Node *) n;
        }
    ;

detach_opt:
    DETACH
        {
            $$ = true;
        }
    | /* EMPTY */
        {
            $$ = false;
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
    expr OR expr
        {
            $$ = make_or_expr($1, $3, @2);
        }
    | expr AND expr
        {
            $$ = make_and_expr($1, $3, @2);
        }
    | NOT expr
        {
            $$ = make_not_expr($2, @1);
        }
    | expr '=' expr
        {
            $$ = (Node *)makeSimpleA_Expr(AEXPR_OP, "=", $1, $3, @2);
        }
    | expr NOT_EQ expr
        {
            $$ = (Node *)makeSimpleA_Expr(AEXPR_OP, "<>", $1, $3, @2);
        }
    | expr '<' expr
        {
            $$ = (Node *)makeSimpleA_Expr(AEXPR_OP, "<", $1, $3, @2);
        }
    | expr LT_EQ expr
        {
            $$ = (Node *)makeSimpleA_Expr(AEXPR_OP, "<=", $1, $3, @2);
        }
    | expr '>' expr
        {
            $$ = (Node *)makeSimpleA_Expr(AEXPR_OP, ">", $1, $3, @2);
        }
    | expr GT_EQ expr
        {
            $$ = (Node *)makeSimpleA_Expr(AEXPR_OP, ">=", $1, $3, @2);
        }
    | expr '+' expr
        {
            $$ = (Node *)makeSimpleA_Expr(AEXPR_OP, "+", $1, $3, @2);
        }
    | expr '-' expr
        {
            $$ = (Node *)makeSimpleA_Expr(AEXPR_OP, "-", $1, $3, @2);
        }
    | expr '*' expr
        {
            $$ = (Node *)makeSimpleA_Expr(AEXPR_OP, "*", $1, $3, @2);
        }
    | expr '/' expr
        {
            $$ = (Node *)makeSimpleA_Expr(AEXPR_OP, "/", $1, $3, @2);
        }
    | expr '%' expr
        {
            $$ = (Node *)makeSimpleA_Expr(AEXPR_OP, "%", $1, $3, @2);
        }
    | expr '^' expr
        {
            $$ = (Node *)makeSimpleA_Expr(AEXPR_OP, "^", $1, $3, @2);
        }
    | expr IN expr
        {
            $$ = (Node *)makeSimpleA_Expr(AEXPR_IN, "=", $1, $3, @2);
        }
    | expr IS NULL_P %prec IS
        {
            NullTest *n;

            n = makeNode(NullTest);
            n->arg = (Expr *)$1;
            n->nulltesttype = IS_NULL;
            n->location = @2;

            $$ = (Node *)n;
        }
    | expr IS NOT NULL_P %prec IS
        {
            NullTest *n;

            n = makeNode(NullTest);
            n->arg = (Expr *)$1;
            n->nulltesttype = IS_NOT_NULL;
            n->location = @2;

            $$ = (Node *)n;
        }
    | '-' expr %prec UNARY_MINUS
        {
            $$ = do_negate($2, @1);
        }
    | expr STARTS WITH expr %prec STARTS
        {
            $$ = NULL;
        }
    | expr ENDS WITH expr %prec ENDS
        {
            $$ = NULL;
        }
    | expr CONTAINS expr
        {
            $$ = NULL;
        }
    | expr '[' expr ']'
        {
            A_Indices *i;

            i = makeNode(A_Indices);
            i->is_slice = false;
            i->lidx = NULL;
            i->uidx = $3;

            $$ = append_indirection($1, (Node *)i);
        }
    | expr '[' expr_opt DOT_DOT expr_opt ']'
        {
            A_Indices *i;

            i = makeNode(A_Indices);
            i->is_slice = true;
            i->lidx = $3;
            i->uidx = $5;

            $$ = append_indirection($1, (Node *)i);
        }
    | expr '.' name
        {
            $$ = append_indirection($1, (Node *)makeString($3));
        }
    | atom
    ;

expr_opt:
    /* empty */
        {
            $$ = NULL;
        }
    | expr
    ;

expr_comma_list:
    expr
        {
            $$ = list_make1($1);
        }
    | expr_comma_list ',' expr
        {
            $$ = lappend($1, $3);
        }
    ;

expr_comma_list_opt:
    /* empty */
        {
            $$ = NIL;
        }
    | expr_comma_list
    ;


atom:
    literal
    | var
    | '(' expr ')'
        {
            $$ = $2;
        }
    ;

literal:
    INTEGER
        {
            $$ = make_int_const($1, @1);
        }
    | DECIMAL
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
    | map
    | list
    ;

map:
    '{' map_keyvals '}'
        {
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                            errmsg("map literal parsed"),
                            errdetail("%s", nodeToString($2)),
                            errposition(@1 + 1)));
            $$ = NULL;
        }
    ;

map_keyvals:
    /* empty */
        {
            $$ = NIL;
        }
    | name ':' expr
        {
            $$ = list_make2(makeString($1), $3);
        }
    | map_keyvals ',' name ':' expr
        {
            $$ = lappend(lappend($1, makeString($3)), $5);
        }
    ;

list:
    '[' expr_comma_list_opt ']'
        {
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                            errmsg("list literal parsed"),
                            errdetail("%s", nodeToString($2)),
                            errposition(@1 + 1)));
            $$ = NULL;
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

name:
    IDENTIFIER
    | reserved_keyword
        {
            $$ = pstrdup($1);
        }
    ;

reserved_keyword:
    AND
    | AS
    | ASC
    | ASCENDING
    | BY
    | CONTAINS
    | DELETE
    | DESC
    | DESCENDING
    | DETACH
    | DISTINCT
    | ENDS
    | FALSE_P
    | IN
    | IS
    | LIMIT
    | NOT
    | NULL_P
    | OR
    | ORDER
    | REMOVE
    | RETURN
    | SET
    | SKIP
    | STARTS
    | TRUE_P
    | WHERE
    | WITH
    ;

%%

static Node *make_or_expr(Node *lexpr, Node *rexpr, int location)
{
    // flatten "a OR b OR c ..." to a single BoolExpr on sight
    if (IsA(lexpr, BoolExpr))
    {
        BoolExpr *bexpr = (BoolExpr *)lexpr;

        if (bexpr->boolop == OR_EXPR)
        {
            bexpr->args = lappend(bexpr->args, rexpr);

            return (Node *)bexpr;
        }
    }

    return (Node *)makeBoolExpr(OR_EXPR, list_make2(lexpr, rexpr), location);
}

static Node *make_and_expr(Node *lexpr, Node *rexpr, int location)
{
    // flatten "a AND b AND c ..." to a single BoolExpr on sight
    if (IsA(lexpr, BoolExpr))
    {
        BoolExpr *bexpr = (BoolExpr *)lexpr;

        if (bexpr->boolop == AND_EXPR)
        {
            bexpr->args = lappend(bexpr->args, rexpr);

            return (Node *)bexpr;
        }
    }

    return (Node *)makeBoolExpr(AND_EXPR, list_make2(lexpr, rexpr), location);
}

static Node *make_not_expr(Node *expr, int location)
{
    return (Node *)makeBoolExpr(NOT_EXPR, list_make1(expr), location);
}

static Node *do_negate(Node *n, int location)
{
    if (IsA(n, A_Const))
    {
        A_Const *c = (A_Const *)n;

        // report the constant's location as that of the '-' sign
        c->location = location;

        if (c->val.type == T_Integer)
        {
            c->val.val.ival = -c->val.val.ival;
            return n;
        }
        else if (c->val.type == T_Float)
        {
            do_negate_float(&c->val);
            return n;
        }
    }

    return (Node *)makeSimpleA_Expr(AEXPR_OP, "-", NULL, n, location);
}

static void do_negate_float(Value *v)
{
    Assert(IsA(v, Float));

    if (v->val.str[0] == '-')
        v->val.str = v->val.str + 1; // just strip the '-'
    else
        v->val.str = psprintf("-%s", v->val.str);
}

static Node *append_indirection(Node *expr, Node *selector)
{
    A_Indirection *indir;

    if (IsA(expr, A_Indirection))
    {
        indir = (A_Indirection *)expr;
        indir->indirection = lappend(indir->indirection, selector);

        return expr;
    }
    else
    {
        indir = makeNode(A_Indirection);
        indir->arg = expr;
        indir->indirection = list_make1(selector);

        return (Node *)indir;
    }
}

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
