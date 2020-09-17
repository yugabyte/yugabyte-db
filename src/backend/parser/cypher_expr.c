/*
 * Copyright 2020 Bitnine Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "postgres.h"

#include "catalog/pg_type.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/value.h"
#include "optimizer/tlist.h"
#include "parser/parse_coerce.h"
#include "parser/parse_expr.h"
#include "parser/cypher_clause.h"
#include "parser/parse_node.h"
#include "parser/parse_oper.h"
#include "parser/parse_relation.h"
#include "utils/builtins.h"
#include "utils/int8.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

#include "commands/label_commands.h"
#include "nodes/cypher_nodes.h"
#include "parser/cypher_expr.h"
#include "parser/cypher_parse_node.h"
#include "utils/ag_func.h"
#include "utils/agtype.h"

/* supported function definitions */
#define FUNC_ENDNODE    {"endNode",    "endnode",    AGTYPEOID, AGTYPEOID, 0, AGTYPEOID, 1, 2, true, false}
#define FUNC_HEAD       {"head",       "head",       AGTYPEOID, 0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_ID         {"id",         "id",         AGTYPEOID, 0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_STARTID    {"start_id",   "start_id",   AGTYPEOID, 0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_ENDID      {"end_id",     "end_id",     AGTYPEOID, 0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_LAST       {"last",       "last",       AGTYPEOID, 0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_LENGTH     {"length",     "length",     AGTYPEOID, 0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_PROPERTIES {"properties", "properties", AGTYPEOID, 0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_SIZE       {"size",       "size",       ANYOID,    0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_STARTNODE  {"startNode",  "startnode",  AGTYPEOID, AGTYPEOID, 0, AGTYPEOID, 1, 2, true, false}
#define FUNC_TOBOOLEAN  {"toBoolean",  "toboolean",  ANYOID,    0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_TOFLOAT    {"toFloat",    "tofloat",    ANYOID,    0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_TOINTEGER  {"toInteger",  "tointeger",  ANYOID,    0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_TYPE       {"type",       "type",       AGTYPEOID, 0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_EXISTS     {"exists",     "exists_property", AGTYPEOID, 0, 0, BOOLOID, 1, 1, false, false}
#define FUNC_TOSTRING   {"toString",   "tostring",   ANYOID,    0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_REVERSE    {"reverse",    "reverse",    ANYOID,    0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_TOUPPER    {"toUpper",    "touppercase", ANYOID,   0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_TOLOWER    {"toLower",    "tolowercase", ANYOID,   0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_LTRIM      {"lTrim",      "l_trim",     ANYOID,    0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_RTRIM      {"rTrim",      "r_trim",     ANYOID,    0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_BTRIM      {"trim",       "b_trim",     ANYOID,    0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_RSUBSTR    {"right",      "r_substr",   ANYOID,    ANYOID, 0, AGTYPEOID, 2, 1, false, false}
#define FUNC_LSUBSTR    {"left",       "l_substr",   ANYOID,    ANYOID, 0, AGTYPEOID, 2, 1, false, false}
#define FUNC_BSUBSTR    {"substring",  "b_substr",   ANYOID,    ANYOID, ANYOID, AGTYPEOID, -1, 1, false, false}
#define FUNC_SPLIT      {"split",      "split",      ANYOID,    ANYOID, 0, AGTYPEOID, 2, 1, false, false}
#define FUNC_REPLACE    {"replace",    "replace",    ANYOID,    ANYOID, 0, AGTYPEOID, 3, 1, false, false}
#define FUNC_RSIN       {"sin",        "r_sin",      ANYOID,    0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_RCOS       {"cos",        "r_cos",      ANYOID,    0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_RTAN       {"tan",        "r_tan",      ANYOID,    0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_RCOT       {"cot",        "r_cot",      ANYOID,    0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_RASIN      {"asin",       "r_asin",     ANYOID,    0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_RACOS      {"acos",       "r_acos",     ANYOID,    0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_RATAN      {"atan",       "r_atan",     ANYOID,    0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_RATAN2     {"atan2",      "r_atan2",    ANYOID,    0, 0, AGTYPEOID, 2, 1, false, false}
#define FUNC_PI         {"pi",         "pi",         0,         0, 0, FLOAT8OID, 0, 0, false, true}
#define FUNC_DEGREES    {"degrees",    "degrees_from_radians", ANYOID, 0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_RADIANS    {"radians",    "radians_from_degrees", ANYOID, 0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_ROUND      {"round",      "ag_round",   ANYOID, 0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_CEIL       {"ceil",       "ag_ceil",    ANYOID, 0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_FLOOR      {"floor",      "ag_floor",   ANYOID, 0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_ABS        {"abs",        "ag_abs",     ANYOID, 0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_SIGN       {"sign",       "ag_sign",    ANYOID, 0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_RAND       {"rand",       "random",     0,      0, 0, FLOAT8OID, 0, 0, false, true}
#define FUNC_LOG        {"log",        "ag_log",     ANYOID, 0, 0, AGTYPEOID, 1, 1, false, false}
#define FUNC_LOG10      {"log10",      "ag_log10",   ANYOID, 0, 0, AGTYPEOID, 1, 1, false, false}

/* supported functions */
#define SUPPORTED_FUNCTIONS {FUNC_TYPE, FUNC_ENDNODE, FUNC_HEAD, FUNC_ID, \
                             FUNC_STARTID, FUNC_ENDID, FUNC_LAST, FUNC_LENGTH, \
                             FUNC_PROPERTIES, FUNC_SIZE, FUNC_STARTNODE, \
                             FUNC_TOINTEGER, FUNC_TOBOOLEAN, FUNC_TOFLOAT, \
                             FUNC_EXISTS, FUNC_TOSTRING, FUNC_REVERSE, \
                             FUNC_TOUPPER, FUNC_TOLOWER, FUNC_LTRIM, \
                             FUNC_RTRIM, FUNC_BTRIM, FUNC_RSUBSTR, \
                             FUNC_LSUBSTR, FUNC_BSUBSTR, FUNC_SPLIT, \
                             FUNC_REPLACE, FUNC_RSIN, FUNC_RCOS, FUNC_RTAN, \
                             FUNC_RCOT, FUNC_RASIN, FUNC_RACOS, FUNC_RATAN, \
                             FUNC_RATAN2, FUNC_PI, FUNC_DEGREES, FUNC_RADIANS, \
                             FUNC_ROUND, FUNC_CEIL, FUNC_FLOOR, FUNC_ABS, \
                             FUNC_SIGN, FUNC_RAND, FUNC_LOG, FUNC_LOG10}

/* structure for supported function signatures */
typedef struct function_signature
{
    /* the name from the parser */
    char *parsed_name;
    /* the actual function name in the code */
    char *actual_name;
    /* input types, currently only up to 3 are supported */
    Oid input1_oid;
    Oid input2_oid;
    Oid input3_oid;
    /* output type */
    Oid result_oid;
    /* number of expressions (arguments) passed by the parser */
    int nexprs;
    /*
     * The number of actual arguments to the function. This can differ from the
     * number of expressions passed if the function requires additional
     * information to be passed, such as the graph name.
     */
    int nargs;
    /* needs graph name passed */
    bool needs_graph_name;
    /* is the function listed in pg_catalog */
    bool in_pg_catalog;
} function_signature;

static Node *transform_cypher_expr_recurse(cypher_parsestate *cpstate,
                                           Node *expr);
static Node *transform_A_Const(cypher_parsestate *cpstate, A_Const *ac);
static Node *transform_ColumnRef(cypher_parsestate *cpstate, ColumnRef *cref);
static Node *transform_A_Indirection(cypher_parsestate *cpstate,
                                     A_Indirection *a_ind);
static Node *transform_AEXPR_OP(cypher_parsestate *cpstate, A_Expr *a);
static Node *transform_BoolExpr(cypher_parsestate *cpstate, BoolExpr *expr);
static Node *transform_cypher_bool_const(cypher_parsestate *cpstate,
                                         cypher_bool_const *bc);
static Node *transform_cypher_integer_const(cypher_parsestate *cpstate,
                                            cypher_integer_const *ic);
static Node *transform_AEXPR_IN(cypher_parsestate *cpstate, A_Expr *a);
static Node *transform_cypher_param(cypher_parsestate *cpstate,
                                    cypher_param *cp);
static Node *transform_cypher_map(cypher_parsestate *cpstate, cypher_map *cm);
static Node *transform_cypher_list(cypher_parsestate *cpstate,
                                   cypher_list *cl);
static Node *transform_cypher_string_match(cypher_parsestate *cpstate,
                                           cypher_string_match *csm_node);
static Node *transform_cypher_typecast(cypher_parsestate *cpstate,
                                       cypher_typecast *ctypecast);
static Node *transform_cypher_function(cypher_parsestate *cpstate,
                                       cypher_function *cfunction);
static Node *transform_CoalesceExpr(cypher_parsestate *cpstate,
                                    CoalesceExpr *cexpr);
static Node *transform_SubLink(cypher_parsestate *cpstate, SubLink *sublink);

Node *transform_cypher_expr(cypher_parsestate *cpstate, Node *expr,
                            ParseExprKind expr_kind)
{
    ParseState *pstate = (ParseState *)cpstate;
    ParseExprKind old_expr_kind;
    Node *result;

    // save and restore identity of expression type we're parsing
    Assert(expr_kind != EXPR_KIND_NONE);
    old_expr_kind = pstate->p_expr_kind;
    pstate->p_expr_kind = expr_kind;

    result = transform_cypher_expr_recurse(cpstate, expr);

    pstate->p_expr_kind = old_expr_kind;

    return result;
}

static Node *transform_cypher_expr_recurse(cypher_parsestate *cpstate,
                                           Node *expr)
{
    if (!expr)
        return NULL;

    // guard against stack overflow due to overly complex expressions
    check_stack_depth();

    switch (nodeTag(expr))
    {
    case T_A_Const:
        return transform_A_Const(cpstate, (A_Const *)expr);
    case T_ColumnRef:
        return transform_ColumnRef(cpstate, (ColumnRef *)expr);
    case T_A_Indirection:
        return transform_A_Indirection(cpstate, (A_Indirection *)expr);
    case T_A_Expr:
    {
        A_Expr *a = (A_Expr *)expr;

        switch (a->kind)
        {
        case AEXPR_OP:
            return transform_AEXPR_OP(cpstate, a);
        case AEXPR_IN:
            return transform_AEXPR_IN(cpstate, a);
        default:
            ereport(ERROR, (errmsg_internal("unrecognized A_Expr kind: %d",
                                            a->kind)));
        }
    }
    case T_BoolExpr:
        return transform_BoolExpr(cpstate, (BoolExpr *)expr);
    case T_NullTest:
    {
        NullTest *n = (NullTest *)expr;

        n->arg = (Expr *)transform_cypher_expr_recurse(cpstate,
                                                       (Node *)n->arg);
        n->argisrow = type_is_rowtype(exprType((Node *)n->arg));

        return expr;
    }
    case T_CoalesceExpr:
        return transform_CoalesceExpr(cpstate, (CoalesceExpr *) expr);
    case T_ExtensibleNode:
        if (is_ag_node(expr, cypher_bool_const))
            return transform_cypher_bool_const(cpstate,
                                               (cypher_bool_const *)expr);
        if (is_ag_node(expr, cypher_integer_const))
            return transform_cypher_integer_const(cpstate,
                                                  (cypher_integer_const *)expr);
        if (is_ag_node(expr, cypher_param))
            return transform_cypher_param(cpstate, (cypher_param *)expr);
        if (is_ag_node(expr, cypher_map))
            return transform_cypher_map(cpstate, (cypher_map *)expr);
        if (is_ag_node(expr, cypher_list))
            return transform_cypher_list(cpstate, (cypher_list *)expr);
        if (is_ag_node(expr, cypher_string_match))
            return transform_cypher_string_match(cpstate,
                                                 (cypher_string_match *)expr);
        if (is_ag_node(expr, cypher_typecast))
            return transform_cypher_typecast(cpstate,
                                             (cypher_typecast *)expr);
        if (is_ag_node(expr, cypher_function))
            return transform_cypher_function(cpstate,
                                             (cypher_function *)expr);

        ereport(ERROR,
                (errmsg_internal("unrecognized ExtensibleNode: %s",
                                 ((ExtensibleNode *)expr)->extnodename)));
        return NULL;
    case T_SubLink:
        return transform_SubLink(cpstate, (SubLink *)expr);
        break;
    default:
        ereport(ERROR, (errmsg_internal("unrecognized node type: %d",
                                        nodeTag(expr))));
    }
    return NULL;
}

static Node *transform_A_Const(cypher_parsestate *cpstate, A_Const *ac)
{
    ParseState *pstate = (ParseState *)cpstate;
    ParseCallbackState pcbstate;
    Value *v = &ac->val;
    Datum d = (Datum)0;
    bool is_null = false;
    Const *c;

    setup_parser_errposition_callback(&pcbstate, pstate, ac->location);
    switch (nodeTag(v))
    {
    case T_Integer:
        d = integer_to_agtype((int64)intVal(v));
        break;
    case T_Float:
        {
            char *n = strVal(v);
            int64 i;

            if (scanint8(n, true, &i))
            {
                d = integer_to_agtype(i);
            }
            else
            {
                float8 f = float8in_internal(n, NULL, "double precision", n);

                d = float_to_agtype(f);
            }
        }
        break;
    case T_String:
        d = string_to_agtype(strVal(v));
        break;
    case T_Null:
        is_null = true;
        break;
    default:
        ereport(ERROR,
                (errmsg_internal("unrecognized node type: %d", nodeTag(v))));
        return NULL;
    }
    cancel_parser_errposition_callback(&pcbstate);

    // typtypmod, typcollation, typlen, and typbyval of agtype are hard-coded.
    c = makeConst(AGTYPEOID, -1, InvalidOid, -1, d, is_null, false);
    c->location = ac->location;
    return (Node *)c;
}

static Node *transform_ColumnRef(cypher_parsestate *cpstate, ColumnRef *cref)
{
    ParseState *pstate = (ParseState *)cpstate;
    Node *field1;
    const char *colname;
    Node *var;

    field1 = linitial(cref->fields);
    Assert(IsA(field1, String));
    colname = strVal(field1);

    var = colNameToVar(pstate, colname, false, cref->location);
    if (!var)
    {
        RangeTblEntry *rte;

        /*
         * If we find an rte with the column ref name, this expr might be
         * referencing a property in a vertex or edge. In that case switch
         * the columnRef to a ColumnRef of the rte.
         */
        if ((rte = find_rte(cpstate, (char *)colname)))
        {
            return scanRTEForColumn(pstate, rte, AG_VERTEX_COLNAME_PROPERTIES,
                                    -1, 0, NULL);
        }

        ereport(ERROR, (errcode(ERRCODE_UNDEFINED_COLUMN),
                        errmsg("variable `%s` does not exist", colname),
                        parser_errposition(pstate, cref->location)));
    }

    return var;
}

static Node *transform_AEXPR_OP(cypher_parsestate *cpstate, A_Expr *a)
{
    ParseState *pstate = (ParseState *)cpstate;
    Node *last_srf = pstate->p_last_srf;
    Node *lexpr = transform_cypher_expr_recurse(cpstate, a->lexpr);
    Node *rexpr = transform_cypher_expr_recurse(cpstate, a->rexpr);

    return (Node *)make_op(pstate, a->name, lexpr, rexpr, last_srf,
                           a->location);
}

static Node *transform_AEXPR_IN(cypher_parsestate *cpstate, A_Expr *a)
{
    Oid func_in_oid;
    FuncExpr *result;
    List *args = NIL;

    args = lappend(args, transform_cypher_expr_recurse(cpstate, a->rexpr));
    args = lappend(args, transform_cypher_expr_recurse(cpstate, a->lexpr));

    /* get the agtype_access_slice function */
    func_in_oid = get_ag_func_oid("agtype_in_operator", 2, AGTYPEOID,
                                  AGTYPEOID);

    result = makeFuncExpr(func_in_oid, AGTYPEOID, args, InvalidOid, InvalidOid,
                          COERCE_EXPLICIT_CALL);

    result->location = exprLocation(a->lexpr);

    return (Node *)result;
}

static Node *transform_BoolExpr(cypher_parsestate *cpstate, BoolExpr *expr)
{
    ParseState *pstate = (ParseState *)cpstate;
    List *args = NIL;
    const char *opname;
    ListCell *la;

    switch (expr->boolop)
    {
    case AND_EXPR:
        opname = "AND";
        break;
    case OR_EXPR:
        opname = "OR";
        break;
    case NOT_EXPR:
        opname = "NOT";
        break;
    default:
        ereport(ERROR, (errmsg_internal("unrecognized boolop: %d",
                                        (int)expr->boolop)));
        return NULL;
    }

    foreach (la, expr->args)
    {
        Node *arg = lfirst(la);

        arg = transform_cypher_expr_recurse(cpstate, arg);
        arg = coerce_to_boolean(pstate, arg, opname);

        args = lappend(args, arg);
    }

    return (Node *)makeBoolExpr(expr->boolop, args, expr->location);
}

static Node *transform_cypher_bool_const(cypher_parsestate *cpstate,
                                         cypher_bool_const *bc)
{
    ParseState *pstate = (ParseState *)cpstate;
    ParseCallbackState pcbstate;
    Datum agt;
    Const *c;

    setup_parser_errposition_callback(&pcbstate, pstate, bc->location);
    agt = boolean_to_agtype(bc->boolean);
    cancel_parser_errposition_callback(&pcbstate);

    // typtypmod, typcollation, typlen, and typbyval of agtype are hard-coded.
    c = makeConst(AGTYPEOID, -1, InvalidOid, -1, agt, false, false);
    c->location = bc->location;

    return (Node *)c;
}

static Node *transform_cypher_integer_const(cypher_parsestate *cpstate,
                                            cypher_integer_const *ic)
{
    ParseState *pstate = (ParseState *)cpstate;
    ParseCallbackState pcbstate;
    Datum agt;
    Const *c;

    setup_parser_errposition_callback(&pcbstate, pstate, ic->location);
    agt = integer_to_agtype(ic->integer);
    cancel_parser_errposition_callback(&pcbstate);

    // typtypmod, typcollation, typlen, and typbyval of agtype are hard-coded.
    c = makeConst(AGTYPEOID, -1, InvalidOid, -1, agt, false, false);
    c->location = ic->location;

    return (Node *)c;
}

static Node *transform_cypher_param(cypher_parsestate *cpstate,
                                    cypher_param *cp)
{
    ParseState *pstate = (ParseState *)cpstate;
    Const *const_str;
    FuncExpr *func_expr;
    Oid func_access_oid;
    List *args = NIL;

    if (!cpstate->params)
    {
        ereport(
            ERROR,
            (errcode(ERRCODE_UNDEFINED_PARAMETER),
             errmsg(
                 "parameters argument is missing from cypher() function call"),
             parser_errposition(pstate, cp->location)));
    }

    /* get the agtype_access_operator function */
    func_access_oid = get_ag_func_oid("agtype_access_operator", 1,
                                      AGTYPEARRAYOID);

    args = lappend(args, copyObject(cpstate->params));

    const_str = makeConst(AGTYPEOID, -1, InvalidOid, -1,
                          string_to_agtype(cp->name), false, false);

    args = lappend(args, const_str);

    func_expr = makeFuncExpr(func_access_oid, AGTYPEOID, args, InvalidOid,
                             InvalidOid, COERCE_EXPLICIT_CALL);
    func_expr->location = cp->location;

    return (Node *)func_expr;
}

static Node *transform_cypher_map(cypher_parsestate *cpstate, cypher_map *cm)
{
    ParseState *pstate = (ParseState *)cpstate;
    List *newkeyvals = NIL;
    ListCell *le;
    FuncExpr *fexpr;
    Oid func_oid;

    Assert(list_length(cm->keyvals) % 2 == 0);

    le = list_head(cm->keyvals);
    while (le != NULL)
    {
        Node *key;
        Node *val;
        Node *newval;
        ParseCallbackState pcbstate;
        Const *newkey;

        key = lfirst(le);
        le = lnext(le);
        val = lfirst(le);
        le = lnext(le);

        newval = transform_cypher_expr_recurse(cpstate, val);

        setup_parser_errposition_callback(&pcbstate, pstate, cm->location);
        // typtypmod, typcollation, typlen, and typbyval of agtype are
        // hard-coded.
        newkey = makeConst(TEXTOID, -1, InvalidOid, -1,
                           CStringGetTextDatum(strVal(key)), false, false);
        cancel_parser_errposition_callback(&pcbstate);

        newkeyvals = lappend(lappend(newkeyvals, newkey), newval);
    }

    if (list_length(newkeyvals) == 0)
        func_oid = get_ag_func_oid("agtype_build_map", 0);
    else
        func_oid = get_ag_func_oid("agtype_build_map", 1, ANYOID);

    fexpr = makeFuncExpr(func_oid, AGTYPEOID, newkeyvals, InvalidOid,
                         InvalidOid, COERCE_EXPLICIT_CALL);
    fexpr->location = cm->location;

    return (Node *)fexpr;
}

static Node *transform_cypher_list(cypher_parsestate *cpstate, cypher_list *cl)
{
    List *newelems = NIL;
    ListCell *le;
    FuncExpr *fexpr;
    Oid func_oid;

    foreach (le, cl->elems)
    {
        Node *newv;

        newv = transform_cypher_expr_recurse(cpstate, lfirst(le));

        newelems = lappend(newelems, newv);
    }

    if (list_length(newelems) == 0)
        func_oid = get_ag_func_oid("agtype_build_list", 0);
    else
        func_oid = get_ag_func_oid("agtype_build_list", 1, ANYOID);

    fexpr = makeFuncExpr(func_oid, AGTYPEOID, newelems, InvalidOid, InvalidOid,
                         COERCE_EXPLICIT_CALL);
    fexpr->location = cl->location;

    return (Node *)fexpr;
}

static Node *transform_A_Indirection(cypher_parsestate *cpstate,
                                     A_Indirection *a_ind)
{
    int location;
    ListCell *lc;
    Node *ind_arg_expr;
    FuncExpr *func_expr = NULL;
    Oid func_access_oid;
    Oid func_slice_oid;
    List *args = NIL;
    bool is_access = false;

    /* validate that we have an indirection with at least 1 entry */
    Assert(a_ind != NULL && list_length(a_ind->indirection));
    /* get the agtype_access_operator function */
    func_access_oid = get_ag_func_oid("agtype_access_operator", 1,
                                      AGTYPEARRAYOID);
    /* get the agtype_access_slice function */
    func_slice_oid = get_ag_func_oid("agtype_access_slice", 3, AGTYPEOID,
                                     AGTYPEOID, AGTYPEOID);

    ind_arg_expr = transform_cypher_expr_recurse(cpstate, a_ind->arg);
    location = exprLocation(ind_arg_expr);

    args = lappend(args, ind_arg_expr);
    foreach (lc, a_ind->indirection)
    {
        Node *node = lfirst(lc);

        /* is this a slice? */
        if (IsA(node, A_Indices) && ((A_Indices *)node)->is_slice)
        {
            A_Indices *indices = (A_Indices *)node;

            /* were we working on an access? if so, wrap and close it */
            if (is_access)
            {
                func_expr = makeFuncExpr(func_access_oid, AGTYPEOID, args,
                                         InvalidOid, InvalidOid,
                                         COERCE_EXPLICIT_CALL);
                func_expr->location = location;
                args = lappend(NIL, func_expr);
                /* we are no longer working on an access */
                is_access = false;
            }
            /* add slice bounds to args */
            if (!indices->lidx)
            {
                A_Const *n = makeNode(A_Const);
                n->val.type = T_Null;
                n->location = -1;
                node = transform_cypher_expr_recurse(cpstate, (Node *)n);
            }
            else
                node = transform_cypher_expr_recurse(cpstate, indices->lidx);
            args = lappend(args, node);
            if (!indices->uidx)
            {
                A_Const *n = makeNode(A_Const);
                n->val.type = T_Null;
                n->location = -1;
                node = transform_cypher_expr_recurse(cpstate, (Node *)n);
            }
            else
                node = transform_cypher_expr_recurse(cpstate, indices->uidx);
            args = lappend(args, node);
            /* wrap and close it */
            func_expr = makeFuncExpr(func_slice_oid, AGTYPEOID, args,
                                     InvalidOid, InvalidOid,
                                     COERCE_EXPLICIT_CALL);
            func_expr->location = location;
            args = lappend(NIL, func_expr);
        }
        /* is this a string or index?*/
        else if (IsA(node, String) || IsA(node, A_Indices))
        {
            /* we are working on an access */
            is_access = true;
            /* is this an index? */
            if (IsA(node, A_Indices))
            {
                A_Indices *indices = (A_Indices *)node;

                node = transform_cypher_expr_recurse(cpstate, indices->uidx);
                args = lappend(args, node);
            }
            /* it must be a string */
            else
            {
                Const *const_str = makeConst(AGTYPEOID, -1, InvalidOid, -1,
                                             string_to_agtype(strVal(node)),
                                             false, false);
                args = lappend(args, const_str);
            }
        }
        /* not an indirection we understand */
        else
        {
            ereport(ERROR,
                    (errmsg("invalid indirection node %d", nodeTag(node))));
        }
    }

    /* if we were doing an access, we need wrap the args with access func. */
    if (is_access)
        func_expr = makeFuncExpr(func_access_oid, AGTYPEOID, args, InvalidOid,
                                 InvalidOid, COERCE_EXPLICIT_CALL);
    Assert(func_expr != NULL);
    func_expr->location = location;

    return (Node *)func_expr;
}

static Node *transform_cypher_string_match(cypher_parsestate *cpstate,
                                           cypher_string_match *csm_node)
{
    Node *expr;
    FuncExpr *func_expr;
    Oid func_access_oid;
    List *args = NIL;
    const char *func_name;

    switch (csm_node->operation)
    {
    case CSMO_STARTS_WITH:
        func_name = "agtype_string_match_starts_with";
        break;
    case CSMO_ENDS_WITH:
        func_name = "agtype_string_match_ends_with";
        break;
    case CSMO_CONTAINS:
        func_name = "agtype_string_match_contains";
        break;

    default:
        ereport(ERROR,
                (errmsg_internal("unknown Cypher string match operation")));
    }

    func_access_oid = get_ag_func_oid(func_name, 2, AGTYPEOID, AGTYPEOID);

    expr = transform_cypher_expr_recurse(cpstate, csm_node->lhs);
    args = lappend(args, expr);
    expr = transform_cypher_expr_recurse(cpstate, csm_node->rhs);
    args = lappend(args, expr);

    func_expr = makeFuncExpr(func_access_oid, AGTYPEOID, args, InvalidOid,
                             InvalidOid, COERCE_EXPLICIT_CALL);
    func_expr->location = csm_node->location;

    return (Node *)func_expr;
}

/*
 * Function to create a typecasting node
 */
static Node *transform_cypher_typecast(cypher_parsestate *cpstate,
                                       cypher_typecast *ctypecast)
{
    Node *expr;
    FuncExpr *func_expr;
    List *func_args = NIL;
    Oid func_agtype_typecast_operator_oid = InvalidOid;

    /* verify input parameter */
    Assert (cpstate != NULL);
    Assert (ctypecast != NULL);

    /* get the oid of the requested typecast function */
    if (pg_strcasecmp(ctypecast->typecast, "edge") == 0)
    {
        func_agtype_typecast_operator_oid =
            get_ag_func_oid("agtype_typecast_edge", 1, AGTYPEOID);
    }
    else if (pg_strcasecmp(ctypecast->typecast, "path") == 0)
    {
        func_agtype_typecast_operator_oid =
            get_ag_func_oid("agtype_typecast_path", 1, AGTYPEOID);
    }
    else if (pg_strcasecmp(ctypecast->typecast, "vertex") == 0)
    {
        func_agtype_typecast_operator_oid =
            get_ag_func_oid("agtype_typecast_vertex", 1, AGTYPEOID);
    }
    else if (pg_strcasecmp(ctypecast->typecast, "numeric") == 0)
    {
        func_agtype_typecast_operator_oid =
            get_ag_func_oid("agtype_typecast_numeric", 1, AGTYPEOID);
    }
    else if (pg_strcasecmp(ctypecast->typecast, "float") == 0)
    {
        func_agtype_typecast_operator_oid =
            get_ag_func_oid("agtype_typecast_float", 1, AGTYPEOID);
    }
    /* if none was found, error out */
    else
    {
        ereport(ERROR,
                (errmsg_internal("typecast \'%s\' not supported",
                                 ctypecast->typecast)));
    }

    /* transform the expression to be typecast */
    expr = transform_cypher_expr_recurse(cpstate, ctypecast->expr);

    /* append the expression and build the function node */
    func_args = lappend(func_args, expr);
    func_expr = makeFuncExpr(func_agtype_typecast_operator_oid, AGTYPEOID,
                             func_args, InvalidOid, InvalidOid,
                             COERCE_EXPLICIT_CALL);
    func_expr->location = ctypecast->location;

    return (Node *)func_expr;
}

/*
 * Function to create a function execute node
 */
static Node *transform_cypher_function(cypher_parsestate *cpstate,
                                       cypher_function *cfunction)
{
    FuncExpr *func_expr = NULL;
    char *funcname = NULL;
    List *exprs = NIL;
    List *texprs = NIL;
    ListCell *lc = NULL;
    int nexprs;
    Oid func_operator_oid = InvalidOid;
    char *graph_name = cpstate->graph_name;
    int i;
    /* load supported functions */
    function_signature supported_functions[] = SUPPORTED_FUNCTIONS;
    function_signature *fs = NULL;
    int nfunctions = sizeof(supported_functions)/sizeof(function_signature);

    /* verify input parameter */
    Assert (cpstate != NULL);
    Assert (cfunction != NULL);
    Assert (nfunctions >= 0);

    /* get the function name and expressions */
    funcname = cfunction->funcname;
    exprs = cfunction->exprs;
    nexprs = list_length(exprs);

    /* iterate through SUPPORTED_FUNCTIONS */
    for (i = 0; i < nfunctions; i++)
    {
        fs = &supported_functions[i];
        if (strcmp(funcname, fs->parsed_name) == 0)
        {
            /* is the function listed in pg_catalog */
            if (fs->in_pg_catalog)
                func_operator_oid = get_pg_func_oid(fs->actual_name, fs->nargs,
                                                    fs->input1_oid,
                                                    fs->input2_oid,
                                                    fs->input3_oid);
            else
                func_operator_oid = get_ag_func_oid(fs->actual_name, fs->nargs,
                                                    fs->input1_oid,
                                                    fs->input2_oid,
                                                    fs->input3_oid);
            break;
        }
    }

    /* we should have something at this point */
    Assert(fs != NULL);
    /* if none was found, error out */
    if (func_operator_oid == InvalidOid)
        ereport(ERROR, (errmsg_internal("function \'%s\' not supported",
                                        cfunction->funcname)));
    /*
     * verify the number of passed arguments -
     * if -1 its variable but at least 1
     * otherwise they must match.
     */
    if (((fs->nexprs != -1) || (nexprs == 0)) &&
        (fs->nexprs != nexprs))
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("invalid number of input parameters for %s()",
                               funcname)));
    /* does this function need the graph name passed in as the first arg? */
    if (fs->needs_graph_name)
    {
        Datum d = string_to_agtype(graph_name);
        Const *c = makeConst(AGTYPEOID, -1, InvalidOid, -1, d, false, false);

        texprs = lappend(texprs, c);
    }
    /* transform expression arguments */
    foreach(lc, exprs)
    {
        Node *expr = (Node *)lfirst(lc);

        expr = transform_cypher_expr_recurse(cpstate, expr);
        texprs = lappend(texprs, expr);
    }
    /* make function node */
    func_expr = makeFuncExpr(func_operator_oid, fs->result_oid, texprs,
                             InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
    func_expr->location = cfunction->location;

    return (Node *)func_expr;
}

/*
 * Code borrowed from PG's transformCoalesceExpr and updated for AGE
 */
static Node *transform_CoalesceExpr(cypher_parsestate *cpstate, CoalesceExpr
                                    *cexpr)
{
    ParseState *pstate = &cpstate->pstate;
    CoalesceExpr *newcexpr = makeNode(CoalesceExpr);
    Node *last_srf = pstate->p_last_srf;
    List *newargs = NIL;
    List *newcoercedargs = NIL;
    ListCell *args;

    foreach(args, cexpr->args)
    {
        Node *e = (Node *)lfirst(args);
        Node *newe;

        newe = transform_cypher_expr_recurse(cpstate, e);
        newargs = lappend(newargs, newe);
    }

    newcexpr->coalescetype = select_common_type(pstate, newargs, "COALESCE",
                                                NULL);
    /* coalescecollid will be set by parse_collate.c */

    /* Convert arguments if necessary */
    foreach(args, newargs)
    {
        Node *e = (Node *)lfirst(args);
        Node *newe;

        newe = coerce_to_common_type(pstate, e, newcexpr->coalescetype,
                                     "COALESCE");
        newcoercedargs = lappend(newcoercedargs, newe);
    }

    /* if any subexpression contained a SRF, complain */
    if (pstate->p_last_srf != last_srf)
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 /* translator: %s is name of a SQL construct, eg GROUP BY */
                 errmsg("set-returning functions are not allowed in %s",
                        "COALESCE"),
                 parser_errposition(pstate, exprLocation(pstate->p_last_srf))));

    newcexpr->args = newcoercedargs;
    newcexpr->location = cexpr->location;
    return (Node *) newcexpr;
}

/* from PG's transformSubLink but reduced and hooked into our parser */
static Node *transform_SubLink(cypher_parsestate *cpstate, SubLink *sublink)
{
    Node *result = (Node*)sublink;
    Query *qtree;
    ParseState *pstate = (ParseState*)cpstate;

    /*
     * Check to see if the sublink is in an invalid place within the query. We
     * allow sublinks everywhere in SELECT/INSERT/UPDATE/DELETE, but generally
     * not in utility statements.
     */
    switch (pstate->p_expr_kind)
    {
        case EXPR_KIND_NONE:
            Assert(false);          /* can't happen */
            break;
        case EXPR_KIND_OTHER:
            /* Accept sublink here; caller must throw error if wanted */
            break;
        case EXPR_KIND_FROM_SUBSELECT:
        case EXPR_KIND_WHERE:
            /* okay */
            break;
        default:
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                            errmsg_internal("unsupported SubLink"),
                            parser_errposition(pstate, sublink->location)));
    }

    pstate->p_hasSubLinks = true;

    /*
     * OK, let's transform the sub-SELECT.
     */
    qtree = cypher_parse_sub_analyze(sublink->subselect, cpstate, NULL, false,
                                     true);

    /*
     * Check that we got a SELECT.  Anything else should be impossible given
     * restrictions of the grammar, but check anyway.
     */
    if (!IsA(qtree, Query) || qtree->commandType != CMD_SELECT)
        elog(ERROR, "unexpected non-SELECT command in SubLink");

    sublink->subselect = (Node *)qtree;

    if (sublink->subLinkType == EXISTS_SUBLINK)
    {
        /*
         * EXISTS needs no test expression or combining operator. These fields
         * should be null already, but make sure.
         */
        sublink->testexpr = NULL;
        sublink->operName = NIL;
    }
    else
        elog(ERROR, "unsupported SubLink type");

    return result;
}
