#include "postgres.h"

#include "catalog/pg_type.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/value.h"
#include "parser/parse_coerce.h"
#include "parser/parse_expr.h"
#include "parser/parse_func.h"
#include "parser/parse_node.h"
#include "parser/parse_oper.h"
#include "utils/builtins.h"
#include "utils/int8.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

#include "agtype.h"
#include "cypher_expr.h"
#include "cypher_nodes.h"

static Node *transform_cypher_expr_recurse(ParseState *pstate, Node *expr);
static Node *transform_A_Const(ParseState *pstate, A_Const *ac);
static Node *transform_AEXPR_OP(ParseState *pstate, A_Expr *a);
static Node *transform_BoolExpr(ParseState *pstate, BoolExpr *expr);
static Node *transform_cypher_bool_const(ParseState *pstate,
                                         cypher_bool_const *bc);
static Node *transform_cypher_param(ParseState *pstate, cypher_param *cp);
static Node *transform_cypher_map(ParseState *pstate, cypher_map *cm);
static Node *transform_cypher_list(ParseState *pstate, cypher_list *cl);
static Node *transform_cypher_indirection(ParseState *pstate,
                                          A_Indirection *ind);

Node *transform_cypher_expr(ParseState *pstate, Node *expr,
                            ParseExprKind expr_kind)
{
    Node *result;
    ParseExprKind old_expr_kind;

    // save and restore identity of expression type we're parsing
    Assert(expr_kind != EXPR_KIND_NONE);
    old_expr_kind = pstate->p_expr_kind;
    pstate->p_expr_kind = expr_kind;

    result = transform_cypher_expr_recurse(pstate, expr);

    pstate->p_expr_kind = old_expr_kind;

    return result;
}

static Node *transform_cypher_expr_recurse(ParseState *pstate, Node *expr)
{
    if (!expr)
        return NULL;

    // guard against stack overflow due to overly complex expressions
    check_stack_depth();

    switch (nodeTag(expr))
    {
    case T_A_Const:
        return transform_A_Const(pstate, (A_Const *)expr);
    case T_A_Expr:
    {
        A_Expr *a = (A_Expr *)expr;

        switch (a->kind)
        {
        case AEXPR_OP:
            return transform_AEXPR_OP(pstate, a);
        default:
            ereport(ERROR, (errmsg("unrecognized A_Expr kind: %d", a->kind)));
        }
    }
    case T_BoolExpr:
        return transform_BoolExpr(pstate, (BoolExpr *)expr);
    case T_NullTest:
    {
        NullTest *n = (NullTest *)expr;

        n->arg = (Expr *)transform_cypher_expr_recurse(pstate, (Node *)n->arg);
        n->argisrow = type_is_rowtype(exprType((Node *)n->arg));

        return expr;
    }
    case T_A_Indirection:
        return transform_cypher_indirection(pstate, (A_Indirection *)expr);
    case T_ExtensibleNode:
        if (is_ag_node(expr, cypher_bool_const))
        {
            return transform_cypher_bool_const(pstate,
                                               (cypher_bool_const *)expr);
        }
        else if (is_ag_node(expr, cypher_param))
        {
            return transform_cypher_param(pstate, (cypher_param *)expr);
        }
        else if (is_ag_node(expr, cypher_map))
        {
            return transform_cypher_map(pstate, (cypher_map *)expr);
        }
        else if (is_ag_node(expr, cypher_list))
        {
            return transform_cypher_list(pstate, (cypher_list *)expr);
        }
        else
        {
            ereport(ERROR, (errmsg("unrecognized ExtensibleNode: %s",
                                   ((ExtensibleNode *)expr)->extnodename)));
            return NULL;
        }
    default:
        ereport(ERROR, (errmsg("unrecognized node type: %d", nodeTag(expr))));
    }
    return NULL;
}

static Node *transform_A_Const(ParseState *pstate, A_Const *ac)
{
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
        ereport(ERROR, (errmsg("unrecognized node type: %d", nodeTag(v))));
        return NULL;
    }
    cancel_parser_errposition_callback(&pcbstate);

    // typtypmod, typcollation, typlen, and typbyval of agtype are hard-coded.
    c = makeConst(AGTYPEOID, -1, InvalidOid, -1, d, is_null, false);
    c->location = ac->location;
    return (Node *)c;
}

static Node *transform_AEXPR_OP(ParseState *pstate, A_Expr *a)
{
    Node *last_srf = pstate->p_last_srf;
    Node *lexpr = transform_cypher_expr_recurse(pstate, a->lexpr);
    Node *rexpr = transform_cypher_expr_recurse(pstate, a->rexpr);

    return (Node *)make_op(pstate, a->name, lexpr, rexpr, last_srf,
                           a->location);
}

static Node *transform_BoolExpr(ParseState *pstate, BoolExpr *expr)
{
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
        ereport(ERROR, (errmsg("unrecognized boolop: %d", (int)expr->boolop)));
        return NULL;
    }

    foreach (la, expr->args)
    {
        Node *arg = lfirst(la);

        arg = transform_cypher_expr_recurse(pstate, arg);
        arg = coerce_to_boolean(pstate, arg, opname);

        args = lappend(args, arg);
    }

    return (Node *)makeBoolExpr(expr->boolop, args, expr->location);
}

static Node *transform_cypher_bool_const(ParseState *pstate,
                                         cypher_bool_const *bc)
{
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

static Node *transform_cypher_param(ParseState *pstate, cypher_param *cp)
{
    Const *const_str;
    FuncExpr *func_expr;
    Oid func_access_oid;
    Oid func_arg_type;
    oidvector *parameter_types;
    List *args = NIL;

    if (!pstate->p_ref_hook_state)
    {
        ereport(
            ERROR,
            (errcode(ERRCODE_UNDEFINED_PARAMETER),
             errmsg(
                "parameters argument is missing from cypher function call"),
             parser_errposition(pstate, cp->location)));
    }

    /* we need the Oid for _agtype */
    func_arg_type =
        GetSysCacheOid2(TYPENAMENSP, CStringGetDatum("_agtype"),
                        ObjectIdGetDatum(ag_catalog_namespace_id()));
    parameter_types = buildoidvector(&func_arg_type, 1);

    /* get the agtype_access_operator function */
    func_access_oid = GetSysCacheOid3(
        PROCNAMEARGSNSP, PointerGetDatum("agtype_access_operator"),
        PointerGetDatum(parameter_types),
        ObjectIdGetDatum(ag_catalog_namespace_id()));

    args = lappend(args, copyObject(pstate->p_ref_hook_state));

    const_str = makeConst(AGTYPEOID, -1, InvalidOid, -1,
                          string_to_agtype(cp->name), false, false);

    args = lappend(args, const_str);

    func_expr = makeFuncExpr(func_access_oid, AGTYPEOID, args, InvalidOid,
                             InvalidOid, COERCE_EXPLICIT_CALL);
    func_expr->location = cp->location;

    return (Node *)func_expr;
}

static Node *transform_cypher_map(ParseState *pstate, cypher_map *cm)
{
    List *newkeyvals = NIL;
    ListCell *le;
    FuncExpr *fexpr;
    Oid func_oid;
    Oid agg_arg_types[1];
    oidvector *parameter_types;

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

        newval = transform_cypher_expr_recurse(pstate, val);

        setup_parser_errposition_callback(&pcbstate, pstate, cm->location);
        // typtypmod, typcollation, typlen, and typbyval of agtype are
        // hard-coded.
        newkey = makeConst(TEXTOID, -1, InvalidOid, -1,
                           CStringGetTextDatum(strVal(key)), false, false);
        cancel_parser_errposition_callback(&pcbstate);

        newkeyvals = lappend(lappend(newkeyvals, newkey), newval);
    }

    if (list_length(newkeyvals) == 0)
    {
        agg_arg_types[0] = InvalidOid;
        parameter_types = buildoidvector(agg_arg_types, 0);
    }
    else
    {
        agg_arg_types[0] = ANYOID;
        parameter_types = buildoidvector(agg_arg_types, 1);
    }

    func_oid = GetSysCacheOid3(PROCNAMEARGSNSP,
                               PointerGetDatum("agtype_build_map"),
                               PointerGetDatum(parameter_types),
                               ObjectIdGetDatum(ag_catalog_namespace_id()));

    fexpr = makeFuncExpr(func_oid, AGTYPEOID, newkeyvals, InvalidOid,
                         InvalidOid, COERCE_EXPLICIT_CALL);
    fexpr->location = cm->location;

    return (Node *)fexpr;
}

static Node *transform_cypher_list(ParseState *pstate, cypher_list *cl)
{
    List *newelems = NIL;
    ListCell *le;
    FuncExpr *fexpr;
    Oid func_oid;
    Oid agg_arg_types[1];
    oidvector *parameter_types;

    foreach (le, cl->elems)
    {
        Node *newv;

        newv = transform_cypher_expr_recurse(pstate, lfirst(le));

        newelems = lappend(newelems, newv);
    }

    if (list_length(newelems) == 0)
    {
        agg_arg_types[0] = InvalidOid;
        parameter_types = buildoidvector(agg_arg_types, 0);
    }
    else
    {
        agg_arg_types[0] = ANYOID;
        parameter_types = buildoidvector(agg_arg_types, 1);
    }

    func_oid = GetSysCacheOid3(PROCNAMEARGSNSP,
                               PointerGetDatum("agtype_build_list"),
                               PointerGetDatum(parameter_types),
                               ObjectIdGetDatum(ag_catalog_namespace_id()));

    fexpr = makeFuncExpr(func_oid, AGTYPEOID, newelems, InvalidOid, InvalidOid,
                         COERCE_EXPLICIT_CALL);
    fexpr->location = cl->location;

    return (Node *)fexpr;
}

static Node *transform_cypher_indirection(ParseState *pstate,
                                          A_Indirection *a_ind)
{
    int location;
    ListCell *lc;
    Node *ind_arg_expr;
    FuncExpr *func_expr;
    Oid func_access_oid;
    List *args = NIL;

    /* we need the array type agtype[] */
    Oid func_arg_types[] = {
        (GetSysCacheOid2(TYPENAMENSP, CStringGetDatum("_agtype"),
                         ObjectIdGetDatum(ag_catalog_namespace_id())))};
    oidvector *parameter_types = buildoidvector(func_arg_types, 1);

    /* get the agtype_access_operator function */
    func_access_oid = GetSysCacheOid3(
        PROCNAMEARGSNSP, PointerGetDatum("agtype_access_operator"),
        PointerGetDatum(parameter_types),
        ObjectIdGetDatum(ag_catalog_namespace_id()));

    ind_arg_expr = transform_cypher_expr_recurse(pstate, a_ind->arg);
    location = exprLocation(ind_arg_expr);

    args = lappend(args, ind_arg_expr);
    foreach (lc, a_ind->indirection)
    {
        Node *node = lfirst(lc);

        if (IsA(node, A_Indices))
        {
            A_Indices *indices = (A_Indices *)node;

            if (indices->is_slice)
            {
                ereport(ERROR, (errmsg("slices are not supported yet")));
            }
            else
            {
                node = transform_cypher_expr_recurse(pstate, indices->uidx);
                args = lappend(args, node);
            }
        }
        else if (IsA(node, String))
        {
            Const *const_str = makeConst(AGTYPEOID, -1, InvalidOid, -1,
                                         string_to_agtype(strVal(node)), false,
                                         false);
            args = lappend(args, const_str);
        }
        else
        {
            ereport(ERROR,
                    (errmsg("invalid indirection node %d", nodeTag(node))));
        }
    }

    func_expr = makeFuncExpr(func_access_oid, AGTYPEOID, args, InvalidOid,
                             InvalidOid, COERCE_EXPLICIT_CALL);
    func_expr->location = location;
    return (Node *)func_expr;
}
