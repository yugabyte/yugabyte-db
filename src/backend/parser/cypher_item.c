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

#include "nodes/makefuncs.h"
#include "parser/parse_relation.h"
#include "parser/parse_target.h"
#include "miscadmin.h"

#include "parser/cypher_expr.h"
#include "parser/cypher_item.h"
#include "parser/cypher_clause.h"

static List *ExpandAllTables(ParseState *pstate, int location);
static List *expand_rel_attrs(ParseState *pstate, RangeTblEntry *rte,
                              int rtindex, int sublevels_up, int location);
bool has_a_cypher_list_comprehension_node(Node *expr);

/* see transformTargetEntry() */
TargetEntry *transform_cypher_item(cypher_parsestate *cpstate, Node *node,
                                   Node *expr, ParseExprKind expr_kind,
                                   char *colname, bool resjunk)
{
    ParseState *pstate = (ParseState *)cpstate;
    bool old_p_lateral_active = pstate->p_lateral_active;

    /* we want to see lateral variables */
    pstate->p_lateral_active = true;

    if (!expr)
        expr = transform_cypher_expr(cpstate, node, expr_kind);

    /* set lateral back to what it was */
    pstate->p_lateral_active = old_p_lateral_active;

    if (!colname && !resjunk)
        colname = FigureColname(node);

    return makeTargetEntry((Expr *)expr, (AttrNumber)pstate->p_next_resno++,
                           colname, resjunk);
}

/*
 * Helper function to determine if the passed node has a list_comprehension
 * node embedded in it.
 */
bool has_a_cypher_list_comprehension_node(Node *expr)
{
    /* return false on NULL input */
    if (expr == NULL)
    {
        return false;
    }

    /* since this function recurses, it could be driven to stack overflow */
    check_stack_depth();

    switch (nodeTag(expr))
    {
    case T_A_Expr:
    {
        /*
         * We need to recurse into the left and right nodes
         * to check if there is an unwind node in there
         */
        A_Expr *a_expr = (A_Expr *)expr;

        return (has_a_cypher_list_comprehension_node(a_expr->lexpr) ||
                has_a_cypher_list_comprehension_node(a_expr->rexpr));
    }
    case T_BoolExpr:
    {
        BoolExpr *bexpr = (BoolExpr *)expr;
        ListCell *lc;

        /* is any of the boolean expression argument a list comprehension? */
        foreach(lc, bexpr->args)
        {
            Node *arg = lfirst(lc);

            if (has_a_cypher_list_comprehension_node(arg))
            {
                return true;
            }
        }
        break;
    }
    case T_A_Indirection:
    {
        /* set expr to the object of the indirection */
        expr = ((A_Indirection *)expr)->arg;

        /* check the object of the indirection */
        return has_a_cypher_list_comprehension_node(expr);
    }
    case T_ExtensibleNode:
    {
        if (is_ag_node(expr, cypher_unwind))
        {
            cypher_unwind *cu = (cypher_unwind *)expr;

            /* it is a list comprehension if it has a collect node */
            return cu->collect != NULL;
        }
        else if (is_ag_node(expr, cypher_map))
        {
            cypher_map *map;
            int i;

            map = (cypher_map *)expr;

            if (map->keyvals == NULL || map->keyvals->length == 0)
            {
                return false;
            }

            /* check each key and value for a list comprehension */
            for (i = 0; i < map->keyvals->length; i += 2)
            {
                Node *val;

                /* get the value */
                val = (Node *)map->keyvals->elements[i + 1].ptr_value;

                /* check the value */
                if (has_a_cypher_list_comprehension_node(val))
                {
                    return true;
                }
            }
        }
        else if (is_ag_node(expr, cypher_string_match))
        {
            cypher_string_match *csm_match = (cypher_string_match *)expr;

            /* is lhs or rhs of the string match a list comprehension? */
            return (has_a_cypher_list_comprehension_node(csm_match->lhs) ||
                    has_a_cypher_list_comprehension_node(csm_match->rhs));
        }
        else if (is_ag_node(expr, cypher_typecast))
        {
            cypher_typecast *ctypecast = (cypher_typecast *)expr;

            /* is expr being typecasted a list comprehension? */
            return has_a_cypher_list_comprehension_node(ctypecast->expr);
        }
        else if (is_ag_node(expr, cypher_comparison_aexpr))
        {
            cypher_comparison_aexpr *aexpr = (cypher_comparison_aexpr *)expr;

            /* is left or right argument a list comprehension? */
            return (has_a_cypher_list_comprehension_node(aexpr->lexpr) ||
                    has_a_cypher_list_comprehension_node(aexpr->rexpr));
        }
        else if (is_ag_node(expr, cypher_comparison_boolexpr))
        {
            cypher_comparison_boolexpr *bexpr = (cypher_comparison_boolexpr *)expr;
            ListCell *lc;

            /* is any of the boolean expression argument a list comprehension? */
            foreach(lc, bexpr->args)
            {
                Node *arg = lfirst(lc);

                if (has_a_cypher_list_comprehension_node(arg))
                {
                    return true;
                }
            }
        }
        break;
    }
    default:
        break;
    }
    /* otherwise, return false */
    return false;
}

/* see transformTargetList() */
List *transform_cypher_item_list(cypher_parsestate *cpstate, List *item_list,
                                 List **groupClause, ParseExprKind expr_kind)
{
    List *target_list = NIL;
    ListCell *li;
    List *group_clause = NIL;
    bool hasAgg = false;
    bool expand_star;

    expand_star = (expr_kind != EXPR_KIND_UPDATE_SOURCE);

    foreach (li, item_list)
    {
        ResTarget *item = lfirst(li);
        TargetEntry *te;
        bool has_list_comp = false;

        if (expand_star)
        {
            if (IsA(item->val, ColumnRef))
            {
                ColumnRef  *cref = (ColumnRef *) item->val;

                if (IsA(llast(cref->fields), A_Star))
                {
                    ParseState *pstate = &cpstate->pstate;

                    /* we only allow a bare '*' */
                    if (list_length(cref->fields) != 1)
                    {
                        ereport(ERROR, (errcode(ERRCODE_SYNTAX_ERROR),
                                        errmsg("Invalid number of fields for *"),
                                        parser_errposition(pstate,
                                                           cref->location)));
                    }

                    target_list = list_concat(target_list,
                                              ExpandAllTables(pstate,
                                                              cref->location));
                    continue;
                }
            }
        }

        /* Check if we have a list comprehension */
        has_list_comp = has_a_cypher_list_comprehension_node(item->val);

        /* Clear the exprHasAgg flag to check transform for an aggregate */
        cpstate->exprHasAgg = false;

        if (has_list_comp && item_list->length > 1)
        {
            /*
             * Create a subquery for the list comprehension and transform it
             * as a subquery. Then expand the target list of the subquery.
             * This is to avoid multiple unnest functions in the same query
             * level and collect not able to distinguish correctly.
             */
            ParseNamespaceItem *pnsi;
            cypher_return *cr;
            cypher_clause cc;

            cr = make_ag_node(cypher_return);
            cr->items = list_make1(item);

            cc.prev = NULL;
            cc.next = NULL;
            cc.self = (Node *)cr;

            pnsi = transform_cypher_clause_as_subquery(cpstate,
                                                       transform_cypher_clause,
                                                       &cc, NULL, true);

            target_list = list_concat(target_list,
                                      expandNSItemAttrs(&cpstate->pstate, pnsi,
                                                        0, true, -1));
        }
        else
        {
            /* transform the item */
            te = transform_cypher_item(cpstate, item->val, NULL, expr_kind,
                                       item->name, false);

            target_list = lappend(target_list, te);
        }

        /*
         * Did the transformed item contain an aggregate function? If it didn't,
         * add it to the potential group_clause. If it did, flag that we found
         * an aggregate in an expression
         */
        if (!cpstate->exprHasAgg)
        {
            group_clause = lappend(group_clause, item->val);
        }
        else
        {
            hasAgg = true;
        }

        /*
         * This is for a special case with list comprehension, which is embedded
         * in a cypher_unwind node. We need to group the results but not expose
         * the grouping expression.
         */
        if (has_list_comp)
        {
            ParseState *pstate = &cpstate->pstate;
            ParseNamespaceItem *nsitem = NULL;
            RangeTblEntry *rte = NULL;

            /*
             * There should be at least 2 entries in p_namespace. One for the
             * variable in the reading clause and one for the variable in the
             * list_comprehension expression. Otherwise, there is nothing to
             * group with.
             */
            if (list_length(pstate->p_namespace) > 1)
            {
                /*
                 * Get the first namespace item which should be the first
                 * variable from the reading clause.
                 */
                nsitem = lfirst(list_head(pstate->p_namespace));
                /* extract the rte */
                rte = nsitem->p_rte;

                /*
                 * If we have a non-null column name make a ColumnRef to it.
                 * Otherwise, there wasn't a variable specified in the reading
                 * clause. If that is the case don't. Because there isn't
                 * anything to group with.
                 */
                if (rte->eref->colnames != NULL && nsitem->p_cols_visible)
                {
                    ColumnRef *cref = NULL;
                    char *colname = NULL;

                    /* get the name of the column (varname) */
                    colname = strVal(lfirst(list_head(rte->eref->colnames)));

                    /* create the ColumnRef */
                    cref = makeNode(ColumnRef);
                    cref->fields = list_make1(makeString(colname));
                    cref->location = -1;

                    /* add the expression for grouping */
                    group_clause = lappend(group_clause, cref);
                }
            }
        }
    }

    /*
     * If we found an aggregate function, we need to return the group_clause,
     * even if NIL. parseCheckAggregates at the end of transform_cypher_return
     * will verify if it is valid.
     */
    if (hasAgg)
    {
        *groupClause = group_clause;
    }

    return target_list;
}

/*
 * From PG's ExpandAllTables()
 *     Transforms '*' (in the target list) into a list of targetlist entries.
 */
static List *ExpandAllTables(ParseState *pstate, int location)
{
    List *target = NIL;
    bool found_table = false;
    ListCell *l;

    foreach(l, pstate->p_namespace)
    {
        ParseNamespaceItem *nsitem = (ParseNamespaceItem *) lfirst(l);

        /* Ignore table-only items */
        if (!nsitem->p_cols_visible)
            continue;
        /* Should not have any lateral-only items when parsing targetlist */
        Assert(!nsitem->p_lateral_only);
        /* Remember we found a p_cols_visible item */
        found_table = true;

        target = list_concat(target, expand_rel_attrs(pstate,
                                                      nsitem->p_rte,
                                                      nsitem->p_rtindex,
                                                      0, location));
    }

    /* Check for "RETURN *;" */
    if (!found_table)
        ereport(ERROR, (errcode(ERRCODE_SYNTAX_ERROR),
                        errmsg("RETURN * without a pattern is not valid"),
                        parser_errposition(pstate, location)));

    return target;
}

/*
 * From PG's expandRelAttrs
 * Modified to exclude hidden variables and aliases in RETURN *
 */
static List *expand_rel_attrs(ParseState *pstate, RangeTblEntry *rte,
                              int rtindex, int sublevels_up, int location)
{
    List *names, *vars;
    ListCell *name, *var;
    List *te_list = NIL;
    int var_prefix_len = strlen(AGE_DEFAULT_VARNAME_PREFIX);
    int alias_prefix_len = strlen(AGE_DEFAULT_ALIAS_PREFIX);

    expandRTE(rte, rtindex, sublevels_up, location, false, &names, &vars);

    /*
     * Require read access to the table.  This is normally redundant with the
     * markVarForSelectPriv calls below, but not if the table has zero
     * columns.
     */
    rte->requiredPerms |= ACL_SELECT;

    /* iterate through the variables */
    forboth(name, names, var, vars)
    {
        char *label = strVal(lfirst(name));
        Var *varnode = (Var *)lfirst(var);
        TargetEntry *te;

        /* we want to skip our "hidden" variables */
        if (strncmp(AGE_DEFAULT_VARNAME_PREFIX, label, var_prefix_len) == 0)
            continue;

        /* we want to skip out "hidden" aliases */
        if (strncmp(AGE_DEFAULT_ALIAS_PREFIX, label, alias_prefix_len) == 0)
            continue;

        /* add this variable to the list */
        te = makeTargetEntry((Expr *)varnode,
                             (AttrNumber)pstate->p_next_resno++, label, false);
        te_list = lappend(te_list, te);

        /* Require read access to each column */
        markVarForSelectPriv(pstate, varnode);
    }

    Assert(name == NULL && var == NULL);    /* lists not the same length? */

    return te_list;
}
