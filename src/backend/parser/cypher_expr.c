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

#include "catalog/pg_type.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/value.h"
#include "optimizer/tlist.h"
#include "parser/parse_coerce.h"
#include "parser/parse_collate.h"
#include "parser/parse_expr.h"
#include "parser/parse_func.h"
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
static Node *transform_CaseExpr(cypher_parsestate *cpstate,
                                    CaseExpr *cexpr);
static Node *transform_CoalesceExpr(cypher_parsestate *cpstate,
                                    CoalesceExpr *cexpr);
static Node *transform_SubLink(cypher_parsestate *cpstate, SubLink *sublink);
static Node *transform_FuncCall(cypher_parsestate *cpstate, FuncCall *fn);
static Node *transform_WholeRowRef(ParseState *pstate, RangeTblEntry *rte,
                                   int location);
static ArrayExpr *make_agtype_array_expr(List *args);

/* transform a cypher expression */
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
        NullTest *tranformed_expr = makeNode(NullTest);

        tranformed_expr->arg = (Expr *)transform_cypher_expr_recurse(cpstate,
                                                         (Node *)n->arg);
        tranformed_expr->nulltesttype = n->nulltesttype;
        tranformed_expr->argisrow = type_is_rowtype(exprType((Node *)tranformed_expr->arg));
        tranformed_expr->location = n->location;

        return (Node *) tranformed_expr;
    }
    case T_CaseExpr:
        return transform_CaseExpr(cpstate, (CaseExpr *) expr);
    case T_CaseTestExpr:
        return expr;
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
        ereport(ERROR,
                (errmsg_internal("unrecognized ExtensibleNode: %s",
                                 ((ExtensibleNode *)expr)->extnodename)));
        return NULL;
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

/*
 * Private function borrowed from PG's transformWholeRowRef.
 * Construct a whole-row reference to represent the notation "relation.*".
 */
static Node *transform_WholeRowRef(ParseState *pstate, RangeTblEntry *rte,
                                   int location)
{
    Var *result;
    int vnum;
    int sublevels_up;

    /* Find the RTE's rangetable location */
    vnum = RTERangeTablePosn(pstate, rte, &sublevels_up);

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
     markVarForSelectPriv(pstate, result, rte);

     return (Node *)result;
}

/*
 * Function to transform a ColumnRef node from the grammar into a Var node
 * Code borrowed from PG's transformColumnRef.
 */
static Node *transform_ColumnRef(cypher_parsestate *cpstate, ColumnRef *cref)
{
    ParseState *pstate = (ParseState *)cpstate;
    RangeTblEntry *rte = NULL;
    Node *field1 = NULL;
    Node *field2 = NULL;
    char *colname = NULL;
    char *nspname = NULL;
    char *relname = NULL;
    Node *node = NULL;
    int levels_up;

    switch (list_length(cref->fields))
    {
        case 1:
            {
                transform_entity *te;
                field1 = (Node*)linitial(cref->fields);

                Assert(IsA(field1, String));
                colname = strVal(field1);

                /* Try to identify as an unqualified column */
                node = colNameToVar(pstate, colname, false, cref->location);
                if (node != NULL)
                {
                        break;
                }

                /*
                 * If expr_kind is WHERE, Try to find the columnRef as a
                 * transform_entity and extract the expr.
                 */
                if (pstate->p_expr_kind == EXPR_KIND_WHERE)
                {
                    te = find_variable(cpstate, colname) ;
                    if (te != NULL && te->expr != NULL)
                    {
                        node = (Node *)te->expr;
                        break;
                    }
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
                rte = refnameRangeTblEntry(pstate, NULL, colname,
                                           cref->location, &levels_up);
                if (rte)
                {
                    node = transform_WholeRowRef(pstate, rte, cref->location);
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
                            errmsg("unable to transform whole row for %s", colname),
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
                rte = refnameRangeTblEntry(pstate, nspname, relname,
                                           cref->location, &levels_up);
                if (rte == NULL)
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
                    node = transform_WholeRowRef(pstate, rte, cref->location);
                    break;
                }

                Assert(IsA(field2, String));

                /* try to identify as a column of the RTE */
                node = scanRTEForColumn(pstate, rte, colname, cref->location, 0,
                                        NULL);
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
    {
        func_oid = get_ag_func_oid("agtype_build_map", 0);
    }
    else if (!cm->keep_null)
    {
        func_oid = get_ag_func_oid("agtype_build_map_nonull", 1, ANYOID);
    }
    else
    {
        func_oid = get_ag_func_oid("agtype_build_map", 1, ANYOID);
    }

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
    {
        func_oid = get_ag_func_oid("agtype_build_list", 0);
    }
    else
    {
        func_oid = get_ag_func_oid("agtype_build_list", 1, ANYOID);
    }

    fexpr = makeFuncExpr(func_oid, AGTYPEOID, newelems, InvalidOid, InvalidOid,
                         COERCE_EXPLICIT_CALL);
    fexpr->location = cl->location;

    return (Node *)fexpr;
}

// makes a VARIADIC agtype array
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

    /* transform indirection argument expression */
    ind_arg_expr = transform_cypher_expr_recurse(cpstate, a_ind->arg);

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
                n->val.type = T_Null;
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
                n->val.type = T_Null;
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
    /* if none was found, error out */
    else
    {
        ereport(ERROR,
                (errmsg_internal("typecast \'%s\' not supported",
                                 ctypecast->typecast)));
    }

    /* make a function call node */
    fnode = makeFuncCall(fname, list_make1(ctypecast->expr),
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
        char *name = ((Value*)linitial(fn->funcname))->val.str;
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
static Node *transform_CaseExpr(cypher_parsestate *cpstate,CaseExpr
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

        n->val.type = T_Null;
        n->location = -1;
        defresult = (Node *) n;
    }
    newcexpr->defresult = (Expr *) transform_cypher_expr_recurse(cpstate, defresult);

    resultexprs = lcons(newcexpr->defresult, resultexprs);

    ptype = select_common_type(pstate, resultexprs, "CASE", NULL);
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
    else
        elog(ERROR, "unsupported SubLink type");

    return result;
}
