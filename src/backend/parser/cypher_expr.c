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
#include "parser/parse_coerce.h"
#include "parser/parse_node.h"
#include "parser/parse_oper.h"
#include "parser/parse_relation.h"
#include "utils/builtins.h"
#include "utils/int8.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

#include "nodes/cypher_nodes.h"
#include "parser/cypher_expr.h"
#include "parser/cypher_parse_node.h"
#include "utils/ag_func.h"
#include "utils/agtype.h"

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
static Node *transform_AEXPR_IN(cypher_parsestate *cpstate, A_Expr *a);
static Node *transform_cypher_param(cypher_parsestate *cpstate,
                                    cypher_param *cp);
static Node *transform_cypher_map(cypher_parsestate *cpstate, cypher_map *cm);
static Node *transform_cypher_list(cypher_parsestate *cpstate,
                                   cypher_list *cl);
static Node *transform_cypher_string_match(cypher_parsestate *cpstate,
                                           cypher_string_match *csm_node);

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
    case T_ExtensibleNode:
        if (is_ag_node(expr, cypher_bool_const))
        {
            return transform_cypher_bool_const(cpstate,
                                               (cypher_bool_const *)expr);
        }
        else if (is_ag_node(expr, cypher_param))
        {
            return transform_cypher_param(cpstate, (cypher_param *)expr);
        }
        else if (is_ag_node(expr, cypher_map))
        {
            return transform_cypher_map(cpstate, (cypher_map *)expr);
        }
        else if (is_ag_node(expr, cypher_list))
        {
            return transform_cypher_list(cpstate, (cypher_list *)expr);
        }
        else if (is_ag_node(expr, cypher_string_match))
        {
            return transform_cypher_string_match(cpstate,
                                                 (cypher_string_match *)expr);
        }
        else
        {
            ereport(ERROR,
                    (errmsg_internal("unrecognized ExtensibleNode: %s",
                                     ((ExtensibleNode *)expr)->extnodename)));
            return NULL;
        }
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
    FuncExpr *func_expr;
    Oid func_access_oid;
    Oid func_slice_oid;
    List *args = NIL;
    bool is_access = false;

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
