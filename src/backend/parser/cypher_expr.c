/*
 * For PostgreSQL Database Management System:
 * (formerly known as Postgres, then as Postgres95)
 *
 * Portions Copyright (c) 1996-2010, The PostgreSQL Global Development Group
 *
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 * Permission to use, copy, modify, and distribute this software and its documentation for any purpose,
 * without fee, and without a written agreement is hereby granted, provided that the above copyright notice
 * and this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR DIRECT,
 * INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS,
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY
 * OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA
 * HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

#include "postgres.h"

#include "miscadmin.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/optimizer.h"
#include "parser/parse_coerce.h"
#include "parser/parse_collate.h"
#include "parser/parse_func.h"
#include "parser/cypher_clause.h"
#include "parser/parse_oper.h"
#include "parser/parse_relation.h"
#include "utils/builtins.h"
#include "utils/float.h"
#include "utils/lsyscache.h"

#include "parser/cypher_expr.h"
#include "parser/cypher_transform_entity.h"
#include "utils/ag_func.h"
#include "utils/agtype.h"

/* names of typecast functions */
#define FUNC_AGTYPE_TYPECAST_EDGE "agtype_typecast_edge"
#define FUNC_AGTYPE_TYPECAST_PATH "agtype_typecast_path"
#define FUNC_AGTYPE_TYPECAST_VERTEX "agtype_typecast_vertex"
#define FUNC_AGTYPE_TYPECAST_NUMERIC "agtype_typecast_numeric"
#define FUNC_AGTYPE_TYPECAST_FLOAT "agtype_typecast_float"
#define FUNC_AGTYPE_TYPECAST_INT "agtype_typecast_int"
#define FUNC_AGTYPE_TYPECAST_PG_FLOAT8 "agtype_to_float8"
#define FUNC_AGTYPE_TYPECAST_PG_BIGINT "agtype_to_int8"
#define FUNC_AGTYPE_TYPECAST_BOOL "agtype_typecast_bool"

static Node *transform_cypher_expr_recurse(cypher_parsestate *cpstate,
                                           Node *expr);
static Node *transform_A_Const(cypher_parsestate *cpstate, A_Const *ac);
static Node *transform_ColumnRef(cypher_parsestate *cpstate, ColumnRef *cref);
static Node *transform_A_Indirection(cypher_parsestate *cpstate,
                                     A_Indirection *a_ind);
static Node *transform_AEXPR_OP(cypher_parsestate *cpstate, A_Expr *a);
static Node *transform_cypher_comparison_aexpr_OP(cypher_parsestate *cpstate,
                                                  cypher_comparison_aexpr *a);
static Node *transform_BoolExpr(cypher_parsestate *cpstate, BoolExpr *expr);
static Node *transform_cypher_comparison_boolexpr(cypher_parsestate *cpstate,
                                                  cypher_comparison_boolexpr *b);
static Node *transform_cypher_bool_const(cypher_parsestate *cpstate,
                                         cypher_bool_const *bc);
static Node *transform_cypher_integer_const(cypher_parsestate *cpstate,
                                            cypher_integer_const *ic);
static Node *transform_AEXPR_IN(cypher_parsestate *cpstate, A_Expr *a);
static Node *transform_cypher_param(cypher_parsestate *cpstate,
                                    cypher_param *cp);
static Node *transform_cypher_map(cypher_parsestate *cpstate, cypher_map *cm);
static Node *transform_cypher_map_projection(cypher_parsestate *cpstate,
                                             cypher_map_projection *cmp);
static Node *transform_cypher_list(cypher_parsestate *cpstate,
                                   cypher_list *cl);
static Node *transform_cypher_string_match(cypher_parsestate *cpstate,
                                           cypher_string_match *csm_node);
static Node *transform_cypher_typecast(cypher_parsestate *cpstate,
                                       cypher_typecast *ctypecast);
static Node *transform_CaseExpr(cypher_parsestate *cpstate,
                                    CaseExpr *cexpr);
static Node *transform_CoalesceExpr(cypher_parsestate *cpstate,
                                    CoalesceExpr *cexpr);
static Node *transform_SubLink(cypher_parsestate *cpstate, SubLink *sublink);
static Node *transform_FuncCall(cypher_parsestate *cpstate, FuncCall *fn);
static Node *transform_WholeRowRef(ParseState *pstate, ParseNamespaceItem *pnsi,
                                   int location, int sublevels_up);
static ArrayExpr *make_agtype_array_expr(List *args);
static Node *transform_column_ref_for_indirection(cypher_parsestate *cpstate,
                                                  ColumnRef *cr);
static Node *transform_cypher_list_comprehension(cypher_parsestate *cpstate,
                                                 cypher_unwind *expr);

/* transform a cypher expression */
Node *transform_cypher_expr(cypher_parsestate *cpstate, Node *expr,
                            ParseExprKind expr_kind)
{
    ParseState *pstate = (ParseState *)cpstate;
    ParseExprKind old_expr_kind;
    Node *result;

    /* save and restore identity of expression type we're parsing */
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

    /* guard against stack overflow due to overly complex expressions */
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
        break;
    }
    case T_BoolExpr:
        return transform_BoolExpr(cpstate, (BoolExpr *)expr);
    case T_NullTest:
    {
        NullTest *n = (NullTest *)expr;
        NullTest *transformed_expr = makeNode(NullTest);

        transformed_expr->arg = (Expr *)transform_cypher_expr_recurse(cpstate,
                                                         (Node *)n->arg);
        transformed_expr->nulltesttype = n->nulltesttype;
        transformed_expr->argisrow = type_is_rowtype(exprType((Node *)transformed_expr->arg));
        transformed_expr->location = n->location;

        return (Node *) transformed_expr;
    }
    case T_CaseExpr:
        return transform_CaseExpr(cpstate, (CaseExpr *) expr);
    case T_CaseTestExpr:
        return expr;
    case T_CoalesceExpr:
        return transform_CoalesceExpr(cpstate, (CoalesceExpr *) expr);
    case T_ExtensibleNode:
    {
        if (is_ag_node(expr, cypher_bool_const))
        {
            return transform_cypher_bool_const(cpstate,
                                               (cypher_bool_const *)expr);
        }
        if (is_ag_node(expr, cypher_integer_const))
        {
            return transform_cypher_integer_const(cpstate,
                                                  (cypher_integer_const *)expr);
        }
        if (is_ag_node(expr, cypher_param))
        {
            return transform_cypher_param(cpstate, (cypher_param *)expr);
        }
        if (is_ag_node(expr, cypher_map))
        {
            return transform_cypher_map(cpstate, (cypher_map *)expr);
        }
        if (is_ag_node(expr, cypher_map_projection))
        {
            return transform_cypher_map_projection(
                cpstate, (cypher_map_projection *)expr);
        }
        if (is_ag_node(expr, cypher_list))
        {
            return transform_cypher_list(cpstate, (cypher_list *)expr);
        }
        if (is_ag_node(expr, cypher_string_match))
        {
            return transform_cypher_string_match(cpstate,
                                                 (cypher_string_match *)expr);
        }
        if (is_ag_node(expr, cypher_typecast))
        {
            return transform_cypher_typecast(cpstate,
                                             (cypher_typecast *)expr);
        }
        if (is_ag_node(expr, cypher_comparison_aexpr))
        {
            return transform_cypher_comparison_aexpr_OP(cpstate,
                                             (cypher_comparison_aexpr *)expr);
        }
        if (is_ag_node(expr, cypher_comparison_boolexpr))
        {
            return transform_cypher_comparison_boolexpr(cpstate,
                                             (cypher_comparison_boolexpr *)expr);
        }
        if (is_ag_node(expr, cypher_unwind))
        {
            return transform_cypher_list_comprehension(cpstate,
                                                       (cypher_unwind *) expr);
        }

        ereport(ERROR,
                (errmsg_internal("unrecognized ExtensibleNode: %s",
                                 ((ExtensibleNode *)expr)->extnodename)));

        return NULL;
    }
    case T_FuncCall:
        return transform_FuncCall(cpstate, (FuncCall *)expr);
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

    Datum d = (Datum)0;
    bool is_null = false;
    Const *c;

    setup_parser_errposition_callback(&pcbstate, pstate, ac->location);
    switch (nodeTag(&ac->val))
    {
    case T_Integer:
        d = integer_to_agtype((int64)intVal(&ac->val));
        break;
    case T_Float:
        {
	    char *n = ac->val.sval.sval;
            char *endptr;
            int64 i;
            errno = 0;

            i = strtoi64(ac->val.fval.fval, &endptr, 10);

            if (errno == 0 && *endptr == '\0')
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
        d = string_to_agtype(strVal(&ac->val));
        break;
    case T_Boolean:
        d = boolean_to_agtype(boolVal(&ac->val));
        break;
    default:
        if (ac->isnull)
        {
	    is_null = true;
	}
        else
        {
	    ereport(ERROR, (errmsg_internal("unrecognized node type: %d",
                                            nodeTag(&ac->val))));
	    return NULL;
	}
    }
    cancel_parser_errposition_callback(&pcbstate);

    /* typtypmod, typcollation, typlen, and typbyval of agtype are hard-coded. */
    c = makeConst(AGTYPEOID, -1, InvalidOid, -1, d, is_null, false);
    c->location = ac->location;
    return (Node *)c;
}

/*
 * Private function borrowed from PG's transformWholeRowRef.
 * Construct a whole-row reference to represent the notation "relation.*".
 */
static Node *transform_WholeRowRef(ParseState *pstate, ParseNamespaceItem *pnsi,
                                   int location, int sublevels_up)
{
    Var *result;
    int vnum;
    RangeTblEntry *rte;

    Assert(pnsi->p_rte != NULL);
    rte = pnsi->p_rte;

    /* Find the RTE's rangetable location */
    vnum = pnsi->p_rtindex;

    /*
     * Build the appropriate referencing node.  Note that if the RTE is a
     * function returning scalar, we create just a plain reference to the
     * function value, not a composite containing a single column.  This is
     * pretty inconsistent at first sight, but it's what we've done
     * historically.  One argument for it is that "rel" and "rel.*" mean the
     * same thing for composite relations, so why not for scalar functions...
     */
     result = makeWholeRowVar(rte, vnum, sublevels_up, true);

     /* location is not filled in by makeWholeRowVar */
     result->location = location;

     /* mark relation as requiring whole-row SELECT access */
     markVarForSelectPriv(pstate, result);

     return (Node *)result;
}

/*
 * Function to transform a ColumnRef node from the grammar into a Var node
 * Code borrowed from PG's transformColumnRef.
 */
static Node *transform_ColumnRef(cypher_parsestate *cpstate, ColumnRef *cref)
{
    ParseState *pstate = (ParseState *)cpstate;
    Node *field1 = NULL;
    Node *field2 = NULL;
    char *colname = NULL;
    char *nspname = NULL;
    char *relname = NULL;
    Node *node = NULL;
    ParseNamespaceItem *pnsi = NULL;
    int levels_up;

    switch (list_length(cref->fields))
    {
        case 1:
            {
                transform_entity *te;
                field1 = (Node*)linitial(cref->fields);

                Assert(IsA(field1, String));
                colname = strVal(field1);

                if (cpstate->p_list_comp &&
                    (pstate->p_expr_kind == EXPR_KIND_WHERE ||
                     pstate->p_expr_kind == EXPR_KIND_SELECT_TARGET) &&
                     list_length(pstate->p_namespace) > 0)
                {
                    /*
                     * Just scan through the last pnsi(that is for list comp)
                     * to find the column.
                     */
                    node = scanNSItemForColumn(pstate,
                                               llast(pstate->p_namespace),
                                               0, colname, cref->location);
                }
                else
                {
                    /* Try to identify as an unqualified column */
                    node = colNameToVar(pstate, colname, false,
                                        cref->location);
                }

                if (node != NULL)
                {
                        break;
                }

                /*
                 * Try to find the columnRef as a transform_entity
                 * and extract the expr.
                 */
                te = find_variable(cpstate, colname) ;
                if (te != NULL && te->expr != NULL &&
                    te->declared_in_current_clause)
                {
                    node = (Node *)te->expr;
                    break;
                }
                /*
                 * Not known as a column of any range-table entry.
                 * Try to find the name as a relation.  Note that only
                 * relations already entered into the rangetable will be
                 * recognized.
                 *
                 * This is a hack for backwards compatibility with
                 * PostQUEL-inspired syntax.  The preferred form now is
                 * "rel.*".
                 */
                pnsi = refnameNamespaceItem(pstate, NULL, colname,
                                           cref->location, &levels_up);
                if (pnsi)
                {
                    node = transform_WholeRowRef(pstate, pnsi, cref->location,
                                                 levels_up);
                }
                else
                {
                    ereport(ERROR,
                                (errcode(ERRCODE_UNDEFINED_COLUMN),
                                 errmsg("could not find rte for %s", colname),
                                 parser_errposition(pstate, cref->location)));
                }

                if (node == NULL)
                {
                    ereport(ERROR, (errcode(ERRCODE_DATA_EXCEPTION),
                            errmsg("unable to transform whole row for %s",
                                   colname),
                             parser_errposition(pstate, cref->location)));
                }

                break;
            }
        case 2:
            {
                Oid inputTypeId = InvalidOid;
                Oid targetTypeId = InvalidOid;

                field1 = (Node*)linitial(cref->fields);
                field2 = (Node*)lsecond(cref->fields);

                Assert(IsA(field1, String));
                relname = strVal(field1);

                if (IsA(field2, String))
                {
                    colname = strVal(field2);
                }

                /* locate the referenced RTE */
                pnsi = refnameNamespaceItem(pstate, nspname, relname,
                                           cref->location, &levels_up);

                if (pnsi == NULL)
                {
                    ereport(ERROR,
                            (errcode(ERRCODE_UNDEFINED_COLUMN),
                             errmsg("could not find rte for %s.%s", relname,
                                    colname),
                             parser_errposition(pstate, cref->location)));
                    break;
                }

                /*
                 * TODO: Left in for potential future use.
                 * Is it a whole-row reference?
                 */
                if (IsA(field2, A_Star))
                {
                    node = transform_WholeRowRef(pstate, pnsi, cref->location,
                                                 levels_up);
                    break;
                }

                Assert(IsA(field2, String));

                /* try to identify as a column of the RTE */
                node = scanNSItemForColumn(pstate, pnsi, levels_up, colname,
                                           cref->location);

                if (node == NULL)
                {
                    ereport(ERROR,
                            (errcode(ERRCODE_UNDEFINED_COLUMN),
                             errmsg("could not find column %s in rel %s of rte",
                                    colname, relname),
                             parser_errposition(pstate, cref->location)));
                }

                /* coerce it to AGTYPE if possible */
                inputTypeId = exprType(node);
                targetTypeId = AGTYPEOID;

                if (can_coerce_type(1, &inputTypeId, &targetTypeId,
                                    COERCION_EXPLICIT))
                {
                    node = coerce_type(pstate, node, inputTypeId, targetTypeId,
                                       -1, COERCION_EXPLICIT,
                                       COERCE_EXPLICIT_CAST, -1);
                }
                break;
            }
        default:
            {
                ereport(ERROR,
                        (errcode(ERRCODE_SYNTAX_ERROR),
                         errmsg("improper qualified name (too many dotted names): %s",
                                NameListToString(cref->fields)),
                         parser_errposition(pstate, cref->location)));
                break;
            }
    }

    if (node == NULL)
    {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_COLUMN),
                 errmsg("variable `%s` does not exist", colname),
                 parser_errposition(pstate, cref->location)));
    }

    return node;
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

/*
 * function for transforming cypher comparision A_Expr. Since this node is a
 * wrapper to let us know when a comparison occurs in a chained comparison,
 * we convert it to a regular A_expr and transform it.
 */
static Node *transform_cypher_comparison_aexpr_OP(cypher_parsestate *cpstate,
                                                  cypher_comparison_aexpr *a)
{
    A_Expr *n = makeNode(A_Expr);
    n->kind = a->kind;
    n->name = a->name;
    n->lexpr = a->lexpr;
    n->rexpr = a->rexpr;
    n->location = a->location;

    return (Node *)transform_AEXPR_OP(cpstate, n);
}


static Node *transform_AEXPR_IN(cypher_parsestate *cpstate, A_Expr *a)
{
    ParseState *pstate = (ParseState *)cpstate;
    cypher_list *rexpr;
    Node *result = NULL;
    Node *lexpr;
    List *rexprs;
    List *rvars;
    List *rnonvars;
    bool useOr;
    ListCell *l;

    if (!is_ag_node(a->rexpr, cypher_list))
    {
        /*
         * We need to build a function call here if the rexpr is already
         * tranformed. It can be already tranformed cypher_list as columnref.
         */ 
        Oid func_in_oid;
        FuncExpr *func_in_expr;
        List *args = NIL;

        args = lappend(args, transform_cypher_expr_recurse(cpstate, a->rexpr));
        args = lappend(args, transform_cypher_expr_recurse(cpstate, a->lexpr));

        /* get the agtype_in_operator function */
        func_in_oid = get_ag_func_oid("agtype_in_operator", 2, AGTYPEOID,
                                    AGTYPEOID);

        func_in_expr = makeFuncExpr(func_in_oid, AGTYPEOID, args, InvalidOid,
                                    InvalidOid, COERCE_EXPLICIT_CALL);

        func_in_expr->location = exprLocation(a->lexpr);

        return (Node *)func_in_expr;
    }

    Assert(is_ag_node(a->rexpr, cypher_list));

    /* If the operator is <>, combine with AND not OR. */
    if (strcmp(strVal(linitial(a->name)), "<>") == 0)
    {
        useOr = false;
    }
    else
    {
        useOr = true;
    }

    lexpr = transform_cypher_expr_recurse(cpstate, a->lexpr);

    rexprs = rvars = rnonvars = NIL;

    rexpr = (cypher_list *)a->rexpr;

    foreach(l, (List *) rexpr->elems)
    {
        Node *rexpr = transform_cypher_expr_recurse(cpstate, lfirst(l));

        rexprs = lappend(rexprs, rexpr);
                if (contain_vars_of_level(rexpr, 0))
                {
                        rvars = lappend(rvars, rexpr);
                }
                else
                {
                        rnonvars = lappend(rnonvars, rexpr);
                }
    }


    /*
     * ScalarArrayOpExpr is only going to be useful if there's more than one
     * non-Var righthand item.
     */
    if (list_length(rnonvars) > 1)
    {
        List *allexprs;
        Oid scalar_type;
        List *aexprs;
        ArrayExpr *newa;

        allexprs = list_concat(list_make1(lexpr), rnonvars);

        scalar_type = AGTYPEOID;

        /* verify they are a common type */
        if (!verify_common_type(scalar_type, allexprs))
        {
            ereport(ERROR,
                    errmsg_internal("not a common type: %d", scalar_type));
        }

        /*
         * coerce all the right-hand non-Var inputs to the common type
         * and build an ArrayExpr for them.
         */
        aexprs = NIL;
        foreach(l, rnonvars)
        {
            Node *rexpr = (Node *) lfirst(l);

            rexpr = coerce_to_common_type(pstate, rexpr, AGTYPEOID, "IN");
            aexprs = lappend(aexprs, rexpr);
        }
        newa = makeNode(ArrayExpr);
        newa->array_typeid = get_array_type(AGTYPEOID);
        /* array_collid will be set by parse_collate.c */
        newa->element_typeid = AGTYPEOID;
        newa->elements = aexprs;
        newa->multidims = false;
        result = (Node *) make_scalar_array_op(pstate, a->name, useOr,
                                               lexpr, (Node *) newa, a->location);

        /* Consider only the Vars (if any) in the loop below */
        rexprs = rvars;
    }

    /* Must do it the hard way, with a boolean expression tree. */
    foreach(l, rexprs)
    {
        Node *rexpr = (Node *) lfirst(l);
        Node *cmp;

        /* Ordinary scalar operator */
        cmp = (Node *) make_op(pstate, a->name, copyObject(lexpr), rexpr,
                               pstate->p_last_srf, a->location);

        cmp = coerce_to_boolean(pstate, cmp, "IN");
        if (result == NULL)
        {
            result = cmp;
        }
        else
        {
            result = (Node *) makeBoolExpr(useOr ? OR_EXPR : AND_EXPR,
                                           list_make2(result, cmp),
                                           a->location);
        }
    }

    return result;
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

/*
 * function for transforming cypher_comparison_boolexpr. Since this node is a
 * wrapper to let us know when a comparison occurs in a chained comparison,
 * we convert it to a PG BoolExpr and transform it.
 */
static Node *transform_cypher_comparison_boolexpr(cypher_parsestate *cpstate,
                                                  cypher_comparison_boolexpr *b)
{
    BoolExpr *n = makeNode(BoolExpr);

    n->boolop = b->boolop;
    n->args = b->args;
    n->location = b->location;

    return transform_BoolExpr(cpstate, n);
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

    /* typtypmod, typcollation, typlen, and typbyval of agtype are hard-coded. */
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

    /* typtypmod, typcollation, typlen, and typbyval of agtype are hard-coded. */
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

static Node *transform_cypher_map_projection(cypher_parsestate *cpstate,
                                             cypher_map_projection *cmp)
{
    ParseState *pstate;
    ListCell *lc;
    List *keyvals;
    Oid foid_agtype_build_map;
    FuncExpr *fexpr_new_map;
    bool has_all_prop_selector;
    Node *transformed_map_var;
    Oid foid_age_properties;
    FuncExpr *fexpr_orig_map;

    pstate = (ParseState *)cpstate;
    keyvals = NIL;
    has_all_prop_selector = false;
    fexpr_new_map = NULL;

    /*
     * Builds the original map: `age_properties(cmp->map_var)`. Whether map_var
     * is compatible (map, vertex or edge) is checked during the execution of
     * age_properties().
     */
    transformed_map_var = transform_cypher_expr_recurse(cpstate,
                                                        (Node *)cmp->map_var);
    foid_age_properties = get_ag_func_oid("age_properties", 1, AGTYPEOID);
    fexpr_orig_map = makeFuncExpr(foid_age_properties, AGTYPEOID,
                                  list_make1(transformed_map_var), InvalidOid,
                                  InvalidOid, COERCE_EXPLICIT_CALL);
    fexpr_orig_map->location = cmp->location;

    /*
     * Builds a new map. Each map projection element is transformed into a key
     * value pair (except for the ALL_PROPERTIES_SELECTOR type).
     */
    foreach (lc, cmp->map_elements)
    {
        cypher_map_projection_element *elem;
        Const *key;
        Node *val;

        elem = lfirst(lc);
        key = NULL;
        val = NULL;

        if (elem->type == ALL_PROPERTIES_SELECTOR)
        {
            has_all_prop_selector = true;
            continue;
        }

        /* Makes key and val based on elem->type */
        switch (elem->type)
        {
            case PROPERTY_SELECTOR:
            {
                Oid foid_access_op;
                FuncExpr *fexpr_access_op;
                ArrayExpr *args_access_op;
                Const *key_agtype;

                /* Makes key from elem->key */
                key = makeConst(TEXTOID, -1, InvalidOid, -1,
                                CStringGetTextDatum(elem->key), false, false);

                /* Makes val from `age_properties(cmp->map_var).key` */
                key_agtype = makeConst(AGTYPEOID, -1, InvalidOid, -1,
                                       string_to_agtype(elem->key), false,
                                       false);
                foid_access_op = get_ag_func_oid("agtype_access_operator", 1,
                                                 AGTYPEARRAYOID);
                args_access_op = make_agtype_array_expr(
                    list_make2(fexpr_orig_map, key_agtype));
                fexpr_access_op = makeFuncExpr(foid_access_op, AGTYPEOID,
                                               list_make1(args_access_op),
                                               InvalidOid, InvalidOid,
                                               COERCE_EXPLICIT_CALL);
                fexpr_access_op->funcvariadic = true;
                fexpr_access_op->location = -1;
                val = (Node *)fexpr_access_op;

                break;
            }
            case LITERAL_ENTRY:
            {
                key = makeConst(TEXTOID, -1, InvalidOid, -1,
                                CStringGetTextDatum(elem->key), false, false);
                val = transform_cypher_expr_recurse(cpstate, elem->value);
                break;
            }
            case VARIABLE_SELECTOR:
            {
                char *key_str;
                List *fields;

                Assert(IsA(elem->value, ColumnRef));

                /* Makes key from the ColumnRef's field */
                fields = ((ColumnRef *)elem->value)->fields;
                key_str = strVal(lfirst(list_head(fields)));
                key = makeConst(TEXTOID, -1, InvalidOid, -1,
                                CStringGetTextDatum(key_str), false, false);

                val = transform_cypher_expr_recurse(cpstate, elem->value);
                break;
            }
            case ALL_PROPERTIES_SELECTOR:
            {
                /*
                 * Key value pairs of the original map are added later outside
                 * the loop. Control never reaches this block.
                 */
                break;
            }
            default:
            {
                elog(ERROR, "unknown map projection element type");
            }
        }

        Assert(key);
        Assert(val);
        keyvals = lappend(lappend(keyvals, key), val);
    }

    if (keyvals)
    {
        foid_agtype_build_map = get_ag_func_oid("agtype_build_map_nonull", 1,
                                                ANYOID);
        fexpr_new_map = makeFuncExpr(foid_agtype_build_map, AGTYPEOID, keyvals,
                                     InvalidOid, InvalidOid,
                                     COERCE_EXPLICIT_CALL);
        fexpr_new_map->location = cmp->location;
    }

    /*
     * In case .* is present, returns age_properties(cmp->map_var) + the new
     * map. Else, returns the new map.
     */
    if (has_all_prop_selector)
    {
        if (!keyvals)
        {
            return (Node *)fexpr_orig_map;
        }
        else
        {
            return (Node *)make_op(pstate, list_make1(makeString("+")),
                                   (Node *)fexpr_orig_map,
                                   (Node *)fexpr_new_map,
                                   pstate->p_last_srf, -1);
        }
    }
    else
    {
        Assert(!has_all_prop_selector && fexpr_new_map);
        return (Node *)fexpr_new_map;
    }
}

/*
 * Helper function to transform a cypher map into an agtype map. The function
 * will use agtype_add to concatenate the argument list when the number of
 * parameters (keys and values) exceeds 100, a PG limitation.
 */
static Node *transform_cypher_map(cypher_parsestate *cpstate, cypher_map *cm)
{
    ParseState *pstate = (ParseState *)cpstate;
    List *newkeyvals_args = NIL;
    ListCell *le = NULL;
    FuncExpr *fexpr = NULL;
    FuncExpr *aa_lhs_arg = NULL;
    Oid abm_func_oid = InvalidOid;
    Oid aa_func_oid = InvalidOid;
    int nkeyvals = 0;
    int i = 0;

    /* get the number of keys and values */
    nkeyvals = list_length(cm->keyvals);

    /* error out if it isn't even */
    if (nkeyvals % 2 != 0)
    {
         ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("number of keys does not match number of values")));
    }

    if (nkeyvals == 0)
    {
        abm_func_oid = get_ag_func_oid("agtype_build_map", 0);
    }
    else if (!cm->keep_null)
    {
        abm_func_oid = get_ag_func_oid("agtype_build_map_nonull", 1, ANYOID);
    }
    else
    {
        abm_func_oid = get_ag_func_oid("agtype_build_map", 1, ANYOID);
    }

    /* get the concat function oid, if necessary */
    if (nkeyvals > 100)
    {
        aa_func_oid = get_ag_func_oid("agtype_add", 2, AGTYPEOID, AGTYPEOID);
    }

    /* get the key/val list */
    le = list_head(cm->keyvals);
    /* while we have key/val to process */
    while (le != NULL)
    {
        Node *key = NULL;
        Node *val = NULL;
        Node *newval = NULL;
        ParseCallbackState pcbstate;
        Const *newkey = NULL;

        /* get the key */
        key = lfirst(le);
        le = lnext(cm->keyvals, le);
        /* get the value */
        val = lfirst(le);
        le = lnext(cm->keyvals, le);

        /* transform the value */
        newval = transform_cypher_expr_recurse(cpstate, val);

        /*
         * If we have more than 50 key/value pairs, 100 elements, we will need
         * to add in the list concatenation function.
         */
        if (i >= 50)
        {
            /* build the object for the first 50 pairs for concat */
            fexpr = makeFuncExpr(abm_func_oid, AGTYPEOID, newkeyvals_args,
                                 InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
            fexpr->location = cm->location;

            /* initial case, set up for concatenating 2 lists */
            if (aa_lhs_arg == NULL)
            {
                aa_lhs_arg = fexpr;
            }
            /*
             * For every other case, concatenate the list on to the previous
             * concatenate operation.
             */
            else
            {
                List *aa_args = list_make2(aa_lhs_arg, fexpr);

                fexpr = makeFuncExpr(aa_func_oid, AGTYPEOID, aa_args,
                                     InvalidOid, InvalidOid,
                                     COERCE_EXPLICIT_CALL);
                fexpr->location = cm->location;

                /* set the lhs to the concatenation operation */
                aa_lhs_arg = fexpr;
            }

            /* reset for the next 50 pairs */
            newkeyvals_args = NIL;
            i = 0;
            fexpr = NULL;
        }

        /* build and append the transformed key/val pair */
        setup_parser_errposition_callback(&pcbstate, pstate, cm->location);
        /* typtypmod, typcollation, typlen, and typbyval of agtype are */
        /* hard-coded. */
        newkey = makeConst(TEXTOID, -1, InvalidOid, -1,
                           CStringGetTextDatum(strVal(key)), false, false);
        cancel_parser_errposition_callback(&pcbstate);

        newkeyvals_args = lappend(lappend(newkeyvals_args, newkey), newval);

        i++;
    }

    /* now build the final map function */
    fexpr = makeFuncExpr(abm_func_oid, AGTYPEOID, newkeyvals_args, InvalidOid,
                         InvalidOid, COERCE_EXPLICIT_CALL);
    fexpr->location = cm->location;

    /*
     * If there was a previous concatenation, build a final concatenation
     * function node.
     */
    if (aa_lhs_arg != NULL)
    {
        List *aa_args = list_make2(aa_lhs_arg, fexpr);

        fexpr = makeFuncExpr(aa_func_oid, AGTYPEOID, aa_args, InvalidOid,
                             InvalidOid, COERCE_EXPLICIT_CALL);
    }

    return (Node *)fexpr;
}

/*
 * Helper function to transform a cypher list into an agtype list. The function
 * will use agtype_add to concatenate argument lists when the number of list
 * elements, parameters, exceeds 100, a PG limitation.
 */
static Node *transform_cypher_list(cypher_parsestate *cpstate, cypher_list *cl)
{
    List *abl_args = NIL;
    ListCell *le = NULL;
    FuncExpr *aa_lhs_arg = NULL;
    FuncExpr *fexpr = NULL;
    Oid abl_func_oid = InvalidOid;
    Oid aa_func_oid = InvalidOid;
    int nelems = 0;
    int i = 0;

    /* determine which build function we need */
    nelems = list_length(cl->elems);
    if (nelems == 0)
    {
        abl_func_oid = get_ag_func_oid("agtype_build_list", 0);
    }
    else
    {
        abl_func_oid = get_ag_func_oid("agtype_build_list", 1, ANYOID);
    }

    /* get the concat function oid, if necessary */
    if (nelems > 100)
    {
        aa_func_oid = get_ag_func_oid("agtype_add", 2, AGTYPEOID, AGTYPEOID);
    }

    /* iterate through the list of elements */
    foreach (le, cl->elems)
    {
        Node *texpr = NULL;

        /* transform the argument */
        texpr = transform_cypher_expr_recurse(cpstate, lfirst(le));

        /*
         * If we have more than 100 elements we will need to add in the list
         * concatenation function.
         */
        if (i >= 100)
        {
            /* build the list function node argument for concatenate */
            fexpr = makeFuncExpr(abl_func_oid, AGTYPEOID, abl_args, InvalidOid,
                                 InvalidOid, COERCE_EXPLICIT_CALL);
            fexpr->location = cl->location;

            /* initial case, set up for concatenating 2 lists */
            if (aa_lhs_arg == NULL)
            {
                aa_lhs_arg = fexpr;
            }
            /*
             * For every other case, concatenate the list on to the previous
             * concatenate operation.
             */
            else
            {
                List *aa_args = list_make2(aa_lhs_arg, fexpr);

                fexpr = makeFuncExpr(aa_func_oid, AGTYPEOID, aa_args,
                                     InvalidOid, InvalidOid,
                                     COERCE_EXPLICIT_CALL);
                fexpr->location = cl->location;

                /* set the lhs to the concatenation operation */
                aa_lhs_arg = fexpr;
            }

            /* reset */
            abl_args = NIL;
            i = 0;
            fexpr = NULL;
        }

        /* now add the latest transformed expression to the list */
        abl_args = lappend(abl_args, texpr);
        i++;
    }

    /* now build the final list function */
    fexpr = makeFuncExpr(abl_func_oid, AGTYPEOID, abl_args, InvalidOid,
                         InvalidOid, COERCE_EXPLICIT_CALL);
    fexpr->location = cl->location;

    /*
     * If there was a previous concatenation or list function, build a final
     * concatenation function node
     */
    if (aa_lhs_arg != NULL)
    {
        List *aa_args = list_make2(aa_lhs_arg, fexpr);

        fexpr = makeFuncExpr(aa_func_oid, AGTYPEOID, aa_args, InvalidOid,
                             InvalidOid, COERCE_EXPLICIT_CALL);
    }

    return (Node *)fexpr;
}

/* makes a VARIADIC agtype array */
static ArrayExpr *make_agtype_array_expr(List *args)
{
    ArrayExpr  *newa = makeNode(ArrayExpr);

    newa->elements = args;

    /* assume all the variadic arguments were coerced to the same type */
    newa->element_typeid = AGTYPEOID;
    newa->array_typeid = AGTYPEARRAYOID;

    if (!OidIsValid(newa->array_typeid))
    {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_OBJECT),
                 errmsg("could not find array type for data type %s",
                        format_type_be(newa->element_typeid))));
    }

    /* array_collid will be set by parse_collate.c */
    newa->multidims = false;

    return newa;
}

/*
 * Transform a ColumnRef for indirection. Try to find the rte that the ColumnRef
 * references and pass the properties of that rte as what the ColumnRef is
 * referencing. Otherwise, reference the Var.
 */
static Node *transform_column_ref_for_indirection(cypher_parsestate *cpstate,
                                                  ColumnRef *cr)
{
    ParseState *pstate = (ParseState *)cpstate;
    ParseNamespaceItem *pnsi = NULL;
    Node *field1 = linitial(cr->fields);
    char *relname = NULL;
    Node *node = NULL;
    int levels_up = 0;

    Assert(IsA(field1, String));
    relname = strVal(field1);

    /* locate the referenced RTE (used to be find_rte(cpstate, relname)) */
    pnsi = refnameNamespaceItem(pstate, NULL, relname, cr->location,
                                &levels_up);

    /*
     * If we didn't find anything, try looking for a previous variable
     * reference. Otherwise, return NULL (colNameToVar will return NULL
     * if nothing is found).
     */
    if (!pnsi)
    {
        Node *prev_var = colNameToVar(pstate, relname, false, cr->location);

        return prev_var;
    }

    /* find the properties column of the NSI and return a var for it */
    node = scanNSItemForColumn(pstate, pnsi, 0, "properties", cr->location);

    /*
     * Error out if we couldn't find it.
     *
     * TODO: Should we error out or return NULL for further processing?
     *       For now, just error out.
     */
    if (!node)
    {
        ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT),
                        errmsg("could not find properties for %s", relname)));
    }

    return node;
}

static Node *transform_A_Indirection(cypher_parsestate *cpstate,
                                     A_Indirection *a_ind)
{
    int location;
    ListCell *lc = NULL;
    Node *ind_arg_expr = NULL;
    FuncExpr *func_expr = NULL;
    Oid func_access_oid = InvalidOid;
    Oid func_slice_oid = InvalidOid;
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

    /*
     * If the indirection argument is a ColumnRef, we want to pull out the
     * properties, as a var node, if possible.
     */
    if (IsA(a_ind->arg, ColumnRef))
    {
        ColumnRef *cr = (ColumnRef *)a_ind->arg;

        ind_arg_expr = transform_column_ref_for_indirection(cpstate, cr);
    }

    /*
     * If we didn't get the properties from a ColumnRef, just transform the
     * indirection argument.
     */
    if (ind_arg_expr == NULL)
    {
        ind_arg_expr = transform_cypher_expr_recurse(cpstate, a_ind->arg);
    }

    /* get the location of the expression */
    location = exprLocation(ind_arg_expr);

    /* add the expression as the first entry */
    args = lappend(args, ind_arg_expr);

    /* iterate through the indirections */
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
                ArrayExpr *newa = make_agtype_array_expr(args);

                func_expr = makeFuncExpr(func_access_oid, AGTYPEOID,
                                         list_make1(newa),
                                         InvalidOid, InvalidOid,
                                         COERCE_EXPLICIT_CALL);

                func_expr->funcvariadic = true;
                func_expr->location = location;

                /*
                 * The result of this function is the input to the next access
                 * or slice operator. So we need to start out with a new arg
                 * list with this function expression.
                 */
                args = lappend(NIL, func_expr);

                /* we are no longer working on an access */
                is_access = false;

            }

            /* add slice bounds to args */
            if (!indices->lidx)
            {
                A_Const *n = makeNode(A_Const);
                n->isnull = true;
                n->location = -1;
                node = transform_cypher_expr_recurse(cpstate, (Node *)n);
            }
            else
            {
                node = transform_cypher_expr_recurse(cpstate, indices->lidx);
            }

            args = lappend(args, node);

            if (!indices->uidx)
            {
                A_Const *n = makeNode(A_Const);
                n->isnull = true;
                n->location = -1;
                node = transform_cypher_expr_recurse(cpstate, (Node *)n);
            }
            else
            {
                node = transform_cypher_expr_recurse(cpstate, indices->uidx);
            }
            args = lappend(args, node);

            /* wrap and close it */
            func_expr = makeFuncExpr(func_slice_oid, AGTYPEOID, args,
                                     InvalidOid, InvalidOid,
                                     COERCE_EXPLICIT_CALL);
            func_expr->location = location;

            /*
             * The result of this function is the input to the next access
             * or slice operator. So we need to start out with a new arg
             * list with this function expression.
             */
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
    {
        ArrayExpr *newa = make_agtype_array_expr(args);

        func_expr = makeFuncExpr(func_access_oid, AGTYPEOID, list_make1(newa),
                                 InvalidOid, InvalidOid,
                                 COERCE_EXPLICIT_CALL);
        func_expr->funcvariadic = true;
    }

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
    List *fname;
    FuncCall *fnode;

    /* verify input parameter */
    Assert (cpstate != NULL);
    Assert (ctypecast != NULL);

    /* create the qualified function name, schema first */
    fname = list_make1(makeString("ag_catalog"));

    /* append the name of the requested typecast function */
    if (pg_strcasecmp(ctypecast->typecast, "edge") == 0)
    {
        fname = lappend(fname, makeString(FUNC_AGTYPE_TYPECAST_EDGE));
    }
    else if (pg_strcasecmp(ctypecast->typecast, "path") == 0)
    {
        fname = lappend(fname, makeString(FUNC_AGTYPE_TYPECAST_PATH));
    }
    else if (pg_strcasecmp(ctypecast->typecast, "vertex") == 0)
    {
        fname = lappend(fname, makeString(FUNC_AGTYPE_TYPECAST_VERTEX));
    }
    else if (pg_strcasecmp(ctypecast->typecast, "numeric") == 0)
    {
        fname = lappend(fname, makeString(FUNC_AGTYPE_TYPECAST_NUMERIC));
    }
    else if (pg_strcasecmp(ctypecast->typecast, "float") == 0)
    {
        fname = lappend(fname, makeString(FUNC_AGTYPE_TYPECAST_FLOAT));
    }
    else if (pg_strcasecmp(ctypecast->typecast, "int") == 0 ||
             pg_strcasecmp(ctypecast->typecast, "integer") == 0)
    {
        fname = lappend(fname, makeString(FUNC_AGTYPE_TYPECAST_INT));
    }
    else if (pg_strcasecmp(ctypecast->typecast, "pg_float8") == 0)
    {
        fname = lappend(fname, makeString(FUNC_AGTYPE_TYPECAST_PG_FLOAT8));
    }
    else if (pg_strcasecmp(ctypecast->typecast, "pg_bigint") == 0)
    {
        fname = lappend(fname, makeString(FUNC_AGTYPE_TYPECAST_PG_BIGINT));
    }
    else if ((pg_strcasecmp(ctypecast->typecast, "bool") == 0 || 
             pg_strcasecmp(ctypecast->typecast, "boolean") == 0))
    {
        fname = lappend(fname, makeString(FUNC_AGTYPE_TYPECAST_BOOL));
    }
    /* if none was found, error out */
    else
    {
        ereport(ERROR,
                (errmsg_internal("typecast \'%s\' not supported",
                                 ctypecast->typecast)));
    }

    /* make a function call node */
    fnode = makeFuncCall(fname, list_make1(ctypecast->expr), COERCE_SQL_SYNTAX,
                         ctypecast->location);

    /* return the transformed function */
    return transform_FuncCall(cpstate, fnode);
}

/*
 * Code borrowed from PG's transformFuncCall and updated for AGE
 */
static Node *transform_FuncCall(cypher_parsestate *cpstate, FuncCall *fn)
{
    ParseState *pstate = &cpstate->pstate;
    Node *last_srf = pstate->p_last_srf;
    List *targs = NIL;
    List *fname = NIL;
    ListCell *arg;
    Node *retval = NULL;

    /* Transform the list of arguments ... */
    foreach(arg, fn->args)
    {
        Node *farg = NULL;

        farg = (Node *)lfirst(arg);
        targs = lappend(targs, transform_cypher_expr_recurse(cpstate, farg));
    }

    /* within group should not happen */
    Assert(!fn->agg_within_group);

    /*
     * If the function name is not qualified, then it is one of ours. We need to
     * construct its name, and qualify it, so that PG can find it.
     */
    if (list_length(fn->funcname) == 1)
    {
        /* get the name, size, and the ag name allocated */
        char *name = ((String*)linitial(fn->funcname))->sval;
        int pnlen = strlen(name);
        char *ag_name = palloc(pnlen + 5);
        int i;

        /* copy in the prefix - all AGE functions are prefixed with age_ */
        strncpy(ag_name, "age_", 4);

        /*
         * All AGE function names are in lower case. So, copy in the name
         * in lower case.
         */
        for (i = 0; i < pnlen; i++)
            ag_name[i + 4] = tolower(name[i]);

        /* terminate it with 0 */
        ag_name[i + 4] = 0;

        /* qualify the name with our schema name */
        fname = list_make2(makeString("ag_catalog"), makeString(ag_name));

        /*
         * Currently 3 functions need the graph name passed in as the first
         * argument - in addition to the other arguments: startNode, endNode,
         * and vle. So, check for those 3 functions here and that the arg list
         * is not empty. Then prepend the graph name if necessary.
         */
        if ((list_length(targs) != 0) &&
            (strcmp("startNode", name) == 0 ||
              strcmp("endNode", name) == 0 ||
              strcmp("vle", name) == 0 ||
              strcmp("vertex_stats", name) == 0))
        {
            char *graph_name = cpstate->graph_name;
            Datum d = string_to_agtype(graph_name);
            Const *c = makeConst(AGTYPEOID, -1, InvalidOid, -1, d, false,
                                 false);

            targs = lcons(c, targs);
        }

    }
    /* If it is not one of our functions, pass the name list through */
    else
    {
        fname = fn->funcname;
    }

    /* ... and hand off to ParseFuncOrColumn */
    retval = ParseFuncOrColumn(pstate, fname, targs, last_srf, fn, false,
                               fn->location);

    /* flag that an aggregate was found during a transform */
    if (retval != NULL && retval->type == T_Aggref)
    {
        cpstate->exprHasAgg = true;
    }

    return retval;
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
    {
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 /* translator: %s is name of a SQL construct, eg GROUP BY */
                 errmsg("set-returning functions are not allowed in %s",
                        "COALESCE"),
                 parser_errposition(pstate, exprLocation(pstate->p_last_srf))));
    }

    newcexpr->args = newcoercedargs;
    newcexpr->location = cexpr->location;
    return (Node *) newcexpr;
}

/*
 * Code borrowed from PG's transformCaseExpr and updated for AGE
 */
static Node *transform_CaseExpr(cypher_parsestate *cpstate, CaseExpr
                                *cexpr)
{
    ParseState *pstate = &cpstate->pstate;
    CaseExpr   *newcexpr = makeNode(CaseExpr);
    Node       *last_srf = pstate->p_last_srf;
    Node       *arg;
    CaseTestExpr *placeholder;
    List       *newargs;
    List       *resultexprs;
    ListCell   *l;
    Node       *defresult;
    Oid         ptype;

    /* transform the test expression, if any */
    arg = transform_cypher_expr_recurse(cpstate, (Node *) cexpr->arg);

    /* generate placeholder for test expression */
    if (arg)
    {
        if (exprType(arg) == UNKNOWNOID)
            arg = coerce_to_common_type(pstate, arg, TEXTOID, "CASE");

        assign_expr_collations(pstate, arg);

        placeholder = makeNode(CaseTestExpr);
        placeholder->typeId = exprType(arg);
        placeholder->typeMod = exprTypmod(arg);
        placeholder->collation = exprCollation(arg);
    }
    else
    {
        placeholder = NULL;
    }

    newcexpr->arg = (Expr *) arg;

    /* transform the list of arguments */
    newargs = NIL;
    resultexprs = NIL;
    foreach(l, cexpr->args)
    {
        CaseWhen   *w = lfirst_node(CaseWhen, l);
        CaseWhen   *neww = makeNode(CaseWhen);
        Node       *warg;

        warg = (Node *) w->expr;
        if (placeholder)
        {
            if(is_ag_node(warg, cypher_comparison_aexpr) ||
               is_ag_node(warg, cypher_comparison_boolexpr) )
            {
                List *funcname = list_make1(makeString("ag_catalog"));
                funcname = lappend(funcname, makeString("bool_to_agtype"));

                warg = (Node *) makeFuncCall(funcname, list_make1(warg),
                                             COERCE_EXPLICIT_CAST,
                                             cexpr->location);
            }

            /* shorthand form was specified, so expand... */
            warg = (Node *) makeSimpleA_Expr(AEXPR_OP, "=",
                                             (Node *) placeholder,
                                             warg,
                                             w->location);
        }
        neww->expr = (Expr *) transform_cypher_expr_recurse(cpstate, warg);

        neww->expr = (Expr *) coerce_to_boolean(pstate,
                                                (Node *) neww->expr,
                                                "CASE/WHEN");

        warg = (Node *) w->result;

        if(is_ag_node(warg, cypher_comparison_aexpr) ||
           is_ag_node(warg, cypher_comparison_boolexpr) )
        {
            List *funcname = list_make1(makeString("ag_catalog"));
            funcname = lappend(funcname, makeString("bool_to_agtype"));

            warg = (Node *) makeFuncCall(funcname, list_make1(warg),
                                         COERCE_EXPLICIT_CAST,
                                         cexpr->location);
        }

        neww->result = (Expr *) transform_cypher_expr_recurse(cpstate, warg);
        neww->location = w->location;

        newargs = lappend(newargs, neww);
        resultexprs = lappend(resultexprs, neww->result);
    }

    newcexpr->args = newargs;

    /* transform the default clause */
    defresult = (Node *) cexpr->defresult;
    if (defresult == NULL)
    {
        A_Const    *n = makeNode(A_Const);

        n->isnull = true;
        n->location = -1;
        defresult = (Node *) n;
    }
    newcexpr->defresult = (Expr *) transform_cypher_expr_recurse(cpstate, defresult);

    resultexprs = lcons(newcexpr->defresult, resultexprs);

    /*
     * we pass a NULL context to select_common_type because the common types can
     * only be AGTYPEOID or BOOLOID. If it returns invalidoid, we know there is a
     * boolean involved.
     */
    ptype = select_common_type(pstate, resultexprs, NULL, NULL);

    /* InvalidOid shows that there is a boolean in the result expr. */
    if (ptype == InvalidOid)
    {
        /* we manually set the type to boolean here to handle the bool casting. */
        ptype = BOOLOID;
    }

    Assert(OidIsValid(ptype));
    newcexpr->casetype = ptype;
    /* casecollid will be set by parse_collate.c */

    /* Convert default result clause, if necessary */
    newcexpr->defresult = (Expr *)
        coerce_to_common_type(pstate,
                              (Node *) newcexpr->defresult,
                              ptype,
                              "CASE/ELSE");

    /* Convert when-clause results, if necessary */
    foreach(l, newcexpr->args)
    {
        CaseWhen   *w = (CaseWhen *) lfirst(l);

        w->result = (Expr *)
            coerce_to_common_type(pstate,
                                  (Node *) w->result,
                                  ptype,
                                  "CASE/WHEN");
    }

    /* if any subexpression contained a SRF, complain */
    if (pstate->p_last_srf != last_srf)
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
        /* translator: %s is name of a SQL construct, eg GROUP BY */
                 errmsg("set-returning functions are not allowed in %s",
                        "CASE"),
                 errhint("You might be able to move the set-returning function into a LATERAL FROM item."),
                 parser_errposition(pstate,
                                    exprLocation(pstate->p_last_srf))));

    newcexpr->location = cexpr->location;

    return (Node *) newcexpr;
}

/* from PG's transformSubLink but reduced and hooked into our parser */
static Node *transform_SubLink(cypher_parsestate *cpstate, SubLink *sublink)
{
    Node *result = (Node*)sublink;
    Query *qtree;
    ParseState *pstate = (ParseState*)cpstate;
    const char *err = NULL;
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
        case EXPR_KIND_JOIN_ON:
        case EXPR_KIND_JOIN_USING:
        case EXPR_KIND_FROM_FUNCTION:
        case EXPR_KIND_SELECT_TARGET:
        case EXPR_KIND_FROM_SUBSELECT:
        case EXPR_KIND_WHERE:
            /* okay */
            break;
        default:
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                            errmsg_internal("unsupported SubLink"),
                            parser_errposition(pstate, sublink->location)));
    }
    if (err)
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg_internal("%s", err),
                 parser_errposition(pstate, sublink->location)));

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
    else if (sublink->subLinkType == EXPR_SUBLINK ||
            sublink->subLinkType == ARRAY_SUBLINK)
    {
        /*
         * Make sure the subselect delivers a single column (ignoring resjunk
         * targets).
         */
        if (count_nonjunk_tlist_entries(qtree->targetList) != 1)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_SYNTAX_ERROR),
                     errmsg("subquery must return only one column"),
                     parser_errposition(pstate, sublink->location)));
        }

        /*
         * EXPR and ARRAY need no test expression or combining operator. These
         * fields should be null already, but make sure.
         */
        sublink->testexpr = NULL;
        sublink->operName = NIL;
    }
    else if (sublink->subLinkType == MULTIEXPR_SUBLINK)
    {
        /* Same as EXPR case, except no restriction on number of columns */
        sublink->testexpr = NULL;
        sublink->operName = NIL;
    }
    else
        elog(ERROR, "unsupported SubLink type");

    return result;
}

static Node *transform_cypher_list_comprehension(cypher_parsestate *cpstate,
                                                 cypher_unwind *unwind)
{
    cypher_clause cc;
    Node* expr;
    ParseNamespaceItem *pnsi;
    ParseState *pstate = (ParseState *)cpstate;

    cpstate->p_list_comp = true;
    pstate->p_lateral_active = true;

    cc.prev = NULL;
    cc.next = NULL;
    cc.self = (Node *)unwind;

    pnsi = transform_cypher_clause_as_subquery(cpstate,
                                               transform_cypher_clause,
                                               &cc, NULL, true);

    expr = transform_cypher_expr(cpstate, unwind->collect,
                                 EXPR_KIND_SELECT_TARGET);

    pnsi->p_cols_visible = false;
    pstate->p_lateral_active = false;

    return expr;
}
