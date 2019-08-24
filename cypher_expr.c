#include "postgres.h"

#include "miscadmin.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/value.h"
#include "parser/parse_node.h"

#include "cypher_expr.h"

static Node *transform_cypher_expr_recurse(ParseState *pstate, Node *expr);

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
    {
        A_Const *c = (A_Const *)expr;
        Value *v = &c->val;

        return (Node *)make_const(pstate, v, c->location);
    }
    default:
        ereport(ERROR,(errmsg("unrecognized node type: %d", nodeTag(expr))));
        return NULL;
    }
}
