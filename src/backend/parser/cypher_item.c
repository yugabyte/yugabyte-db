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

#include "parser/cypher_expr.h"
#include "parser/cypher_item.h"

static List *ExpandAllTables(ParseState *pstate, int location);
static List *expand_rel_attrs(ParseState *pstate, RangeTblEntry *rte,
                              int rtindex, int sublevels_up, int location);

// see transformTargetEntry()
TargetEntry *transform_cypher_item(cypher_parsestate *cpstate, Node *node,
                                   Node *expr, ParseExprKind expr_kind,
                                   char *colname, bool resjunk)
{
    ParseState *pstate = (ParseState *)cpstate;

    if (!expr)
        expr = transform_cypher_expr(cpstate, node, expr_kind);

    if (!colname && !resjunk)
        colname = FigureColname(node);

    return makeTargetEntry((Expr *)expr, (AttrNumber)pstate->p_next_resno++,
                           colname, resjunk);
}

// see transformTargetList()
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
        /* clear the exprHasAgg flag to check transform for an aggregate */
        cpstate->exprHasAgg = false;

        /* transform the item */
        te = transform_cypher_item(cpstate, item->val, NULL, expr_kind,
                                   item->name, false);

        target_list = lappend(target_list, te);

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
