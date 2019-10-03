#include "postgres.h"

#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/value.h"
#include "parser/parse_node.h"
#include "parser/parse_oper.h"
#include "utils/builtins.h"
#include "utils/int8.h"

#include "agtype.h"
#include "cypher_expr.h"
#include "cypher_nodes.h"

static Node *transform_cypher_expr_recurse(ParseState *pstate, Node *expr);
static Node *transform_A_Const(ParseState *pstate, A_Const *ac);
static Datum integer_to_agtype(ParseState *pstate, int64 i, int location);
static Datum float_to_agtype(ParseState *pstate, char *f, int location);
static Datum string_to_agtype(ParseState *pstate, char *s, int location);
static Node *transform_cypher_bool_const(ParseState *pstate,
                                         cypher_bool_const *bc);
static Node *transform_AEXPR_OP(ParseState *pstate, A_Expr *a);

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
    case T_ExtensibleNode:
        if (is_ag_node(expr, cypher_bool_const))
        {
            return transform_cypher_bool_const(pstate,
                                               (cypher_bool_const *)expr);
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
    Value *v = &ac->val;
    int location = ac->location;
    Datum d = (Datum)0;
    bool is_null = false;
    Const *c;

    switch (nodeTag(v))
    {
    case T_Integer:
        d = integer_to_agtype(pstate, (int64)intVal(v), location);
        break;
    case T_Float:
    {
        char *n = strVal(v);
        int64 i;

        if (scanint8(n, true, &i))
            d = integer_to_agtype(pstate, i, location);
        else
            d = float_to_agtype(pstate, n, location);
    }
    break;
    case T_String:
        d = string_to_agtype(pstate, strVal(v), location);
        break;
    case T_Null:
        is_null = true;
        break;
    default:
        ereport(ERROR, (errmsg("unrecognized node type: %d", nodeTag(v))));
        return NULL;
    }

    // typtypmod, typcollation, typlen, and typbyval of agtype are hard-coded.
    c = makeConst(AGTYPEOID, -1, InvalidOid, -1, d, is_null, false);
    c->location = ac->location;
    return (Node *)c;
}

static Datum integer_to_agtype(ParseState *pstate, int64 i, int location)
{
    ParseCallbackState pcbstate;
    agtype_value agtv;
    agtype *agt;

    setup_parser_errposition_callback(&pcbstate, pstate, location);

    agtv.type = AGTV_INTEGER;
    agtv.val.int_value = i;
    agt = agtype_value_to_agtype(&agtv);

    cancel_parser_errposition_callback(&pcbstate);

    return AGTYPE_P_GET_DATUM(agt);
}

static Datum float_to_agtype(ParseState *pstate, char *f, int location)
{
    ParseCallbackState pcbstate;
    agtype_value agtv;
    agtype *agt;

    setup_parser_errposition_callback(&pcbstate, pstate, location);

    agtv.type = AGTV_FLOAT;
    agtv.val.float_value = float8in_internal(f, NULL, "double precision", f);
    agt = agtype_value_to_agtype(&agtv);

    cancel_parser_errposition_callback(&pcbstate);

    return AGTYPE_P_GET_DATUM(agt);
}

/*
 * This function assumes that the given string s is a valid agtype string for
 * internal storage. The intended use of this function is creating agtype of
 * a parsed literal string that is from the parser. The literal string comes
 * from the scanner that handles all the escape sequences.
 */
static Datum string_to_agtype(ParseState *pstate, char *s, int location)
{
    ParseCallbackState pcbstate;
    agtype_value agtv;
    agtype *agt;

    setup_parser_errposition_callback(&pcbstate, pstate, location);

    agtv.type = AGTV_STRING;
    agtv.val.string.val = s;
    agtv.val.string.len = strlen(s); // XXX: check_string_length()
    agt = agtype_value_to_agtype(&agtv);

    cancel_parser_errposition_callback(&pcbstate);

    return AGTYPE_P_GET_DATUM(agt);
}

static Node *transform_cypher_bool_const(ParseState *pstate,
                                         cypher_bool_const *bc)
{
    ParseCallbackState pcbstate;
    agtype_value agtv;
    agtype *agt;
    Const *c;

    setup_parser_errposition_callback(&pcbstate, pstate, bc->location);

    agtv.type = AGTV_BOOL;
    agtv.val.boolean = bc->boolean;
    agt = agtype_value_to_agtype(&agtv);

    cancel_parser_errposition_callback(&pcbstate);

    // typtypmod, typcollation, typlen, and typbyval of agtype are hard-coded.
    c = makeConst(AGTYPEOID, -1, InvalidOid, -1, AGTYPE_P_GET_DATUM(agt),
                  false, false);
    c->location = bc->location;

    return (Node *)c;
}

static Node *transform_AEXPR_OP(ParseState *pstate, A_Expr *a)
{
    Node *last_srf = pstate->p_last_srf;
    Node *lexpr = transform_cypher_expr_recurse(pstate, a->lexpr);
    Node *rexpr = transform_cypher_expr_recurse(pstate, a->rexpr);

    if (list_length(a->name) == 1)
    {
        const char *opname = strVal(linitial(a->name));

        if (strcmp(opname, "+") == 0 || strcmp(opname, "-") == 0 ||
            strcmp(opname, "*") == 0 || strcmp(opname, "/") == 0 ||
            strcmp(opname, "%") == 0 || strcmp(opname, "^") == 0)
        {
            return (Node *)make_op(pstate, a->name, lexpr, rexpr, last_srf,
                                   a->location);
        }
        else if (strcmp(opname, "=") == 0 || strcmp(opname, "<>") == 0 ||
                 strcmp(opname, "<") == 0 || strcmp(opname, ">") == 0 ||
                 strcmp(opname, "<=") == 0 || strcmp(opname, ">=") == 0)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("comparison operator not yet implemented: %s",
                            opname)));
        }
        else
            ereport(ERROR, (errmsg("unknown operator: %s", opname)));

        return NULL;
    }

    ereport(ERROR, (errmsg("invalid list length: %d", list_length(a->name))));

    return NULL;
}
