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

#include "catalog/pg_constraint.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/tlist.h"
#include "optimizer/optimizer.h"
#include "parser/cypher_parse_agg.h"
#include "parser/parsetree.h"
#include "rewrite/rewriteManip.h"
#include "utils/agtype.h"

typedef struct
{
    ParseState *pstate;
    int min_varlevel;
    int min_agglevel;
    int sublevels_up;
} check_agg_arguments_context;

typedef struct
{
    ParseState *pstate;
    Query *qry;
    PlannerInfo *root;
    List *groupClauses;
    List *groupClauseCommonVars;
    bool have_non_var_grouping;
    List **func_grouped_rels;
    int sublevels_up;
    bool in_agg_direct_args;
} check_ungrouped_columns_context;

static void check_ungrouped_columns(Node *node, ParseState *pstate, Query *qry,
                                    List *groupClauses, List *groupClauseVars,
                                    bool have_non_var_grouping,
                                    List **func_grouped_rels);
static bool check_ungrouped_columns_walker(Node *node,
                                           check_ungrouped_columns_context *context);
static void finalize_grouping_exprs(Node *node, ParseState *pstate, Query *qry,
                                    List *groupClauses, PlannerInfo *root,
                                    bool have_non_var_grouping);
static bool finalize_grouping_exprs_walker(Node *node,
                                           check_ungrouped_columns_context *context);
static List *expand_groupingset_node(GroupingSet *gs);
static List * expand_grouping_sets(List *groupingSets, int limit);

/*
 * From PG's parseCheckAggregates
 *
 * Check for aggregates where they shouldn't be and improper grouping.
 * This function should be called after the target list and qualifications
 * are finalized.
 *
 * Misplaced aggregates are now mostly detected in transformAggregateCall,
 * but it seems more robust to check for aggregates in recursive queries
 * only after everything is finalized.  In any case it's hard to detect
 * improper grouping on-the-fly, so we have to make another pass over the
 * query for that.
 */
void parse_check_aggregates(ParseState *pstate, Query *qry)
{
    List *gset_common = NIL;
    List *groupClauses = NIL;
    List *groupClauseCommonVars = NIL;
    bool have_non_var_grouping;
    List *func_grouped_rels = NIL;
    ListCell *l;
    bool hasJoinRTEs;
    bool hasSelfRefRTEs;
    PlannerInfo *root = NULL;
    Node *clause;

    /* This should only be called if we found aggregates or grouping */
    Assert(pstate->p_hasAggs || qry->groupClause || qry->havingQual || qry->groupingSets);

    /*
     * If we have grouping sets, expand them and find the intersection of all
     * sets.
     */
    if (qry->groupingSets)
    {
        /*
         * The limit of 4096 is arbitrary and exists simply to avoid resource
         * issues from pathological constructs.
         */
        List *gsets = expand_grouping_sets(qry->groupingSets, 4096);

        if (!gsets)
            ereport(ERROR,
                    (errcode(ERRCODE_STATEMENT_TOO_COMPLEX),
                     errmsg("too many grouping sets present (maximum 4096)"),
                     parser_errposition(pstate,
                                        qry->groupClause ?
                                        exprLocation((Node *) qry->groupClause) :
                                        exprLocation((Node *) qry->groupingSets))));

        /*
         * The intersection will often be empty, so help things along by
         * seeding the intersect with the smallest set.
         */
        gset_common = linitial(gsets);

        if (gset_common)
        {
            for_each_cell(l, gsets, lnext(gsets, list_head(gsets)))
            {
                gset_common = list_intersection_int(gset_common, lfirst(l));
                if (!gset_common)
                    break;
            }
        }

        /*
         * If there was only one grouping set in the expansion, AND if the
         * groupClause is non-empty (meaning that the grouping set is not
         * empty either), then we can ditch the grouping set and pretend we
         * just had a normal GROUP BY.
         */
        if (list_length(gsets) == 1 && qry->groupClause)
            qry->groupingSets = NIL;
    }

    /*
     * Scan the range table to see if there are JOIN or self-reference CTE
     * entries.  We'll need this info below.
     */
    hasJoinRTEs = hasSelfRefRTEs = false;
    foreach(l, pstate->p_rtable)
    {
        RangeTblEntry *rte = (RangeTblEntry *) lfirst(l);

        if (rte->rtekind == RTE_JOIN)
            hasJoinRTEs = true;
        else if (rte->rtekind == RTE_CTE && rte->self_reference)
            hasSelfRefRTEs = true;
    }

    /*
     * Build a list of the acceptable GROUP BY expressions for use by
     * check_ungrouped_columns().
     *
     * We get the TLE, not just the expr, because GROUPING wants to know the
     * sortgroupref.
     */
    foreach(l, qry->groupClause)
    {
        SortGroupClause *grpcl = (SortGroupClause *) lfirst(l);
        TargetEntry *expr;

        expr = get_sortgroupclause_tle(grpcl, qry->targetList);
        if (expr == NULL)
            continue; /* probably cannot happen */

        groupClauses = lcons(expr, groupClauses);
    }

    /*
     * If there are join alias vars involved, we have to flatten them to the
     * underlying vars, so that aliased and unaliased vars will be correctly
     * taken as equal.  We can skip the expense of doing this if no rangetable
     * entries are RTE_JOIN kind. We use the planner's flatten_join_alias_vars
     * routine to do the flattening; it wants a PlannerInfo root node, which
     * fortunately can be mostly dummy.
     */
    if (hasJoinRTEs)
    {
        root = makeNode(PlannerInfo);
        root->parse = qry;
        root->planner_cxt = CurrentMemoryContext;
        root->hasJoinRTEs = true;

        groupClauses = (List *) flatten_join_alias_vars((Query*)root,
                                                        (Node *) groupClauses);
    }

    /*
     * Detect whether any of the grouping expressions aren't simple Vars; if
     * they're all Vars then we don't have to work so hard in the recursive
     * scans.  (Note we have to flatten aliases before this.)
     *
     * Track Vars that are included in all grouping sets separately in
     * groupClauseCommonVars, since these are the only ones we can use to
     * check for functional dependencies.
     */
    have_non_var_grouping = false;
    foreach(l, groupClauses)
    {
        TargetEntry *tle = lfirst(l);

        if (!IsA(tle->expr, Var))
        {
            have_non_var_grouping = true;
        }
        else if (!qry->groupingSets ||
                 list_member_int(gset_common, tle->ressortgroupref))
        {
            groupClauseCommonVars = lappend(groupClauseCommonVars, tle->expr);
        }
    }

    /*
     * Check the targetlist and HAVING clause for ungrouped variables.
     *
     * Note: because we check resjunk tlist elements as well as regular ones,
     * this will also find ungrouped variables that came from ORDER BY and
     * WINDOW clauses.  For that matter, it's also going to examine the
     * grouping expressions themselves --- but they'll all pass the test ...
     *
     * We also finalize GROUPING expressions, but for that we need to traverse
     * the original (unflattened) clause in order to modify nodes.
     */
    clause = (Node *) qry->targetList;
    finalize_grouping_exprs(clause, pstate, qry, groupClauses, root,
                            have_non_var_grouping);
    if (hasJoinRTEs)
        clause = flatten_join_alias_vars((Query*)root, clause);
    check_ungrouped_columns(clause, pstate, qry, groupClauses,
                            groupClauseCommonVars, have_non_var_grouping,
                            &func_grouped_rels);

    clause = (Node *) qry->havingQual;
    finalize_grouping_exprs(clause, pstate, qry, groupClauses, root,
                            have_non_var_grouping);
    if (hasJoinRTEs)
        clause = flatten_join_alias_vars((Query*)root, clause);
    check_ungrouped_columns(clause, pstate, qry, groupClauses,
                            groupClauseCommonVars, have_non_var_grouping,
                            &func_grouped_rels);

    /*
     * Per spec, aggregates can't appear in a recursive term.
     */
    if (pstate->p_hasAggs && hasSelfRefRTEs)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_RECURSION),
                 errmsg("aggregate functions are not allowed in a recursive query's recursive term"),
                 parser_errposition(pstate, locate_agg_of_level((Node *) qry, 0))));
}

/*
 * check_ungrouped_columns -
 *
 * Scan the given expression tree for ungrouped variables (variables
 * that are not listed in the groupClauses list and are not within
 * the arguments of aggregate functions).  Emit a suitable error message
 * if any are found.
 *
 * NOTE: we assume that the given clause has been transformed suitably for
 * parser output.  This means we can use expression_tree_walker.
 *
 * NOTE: we recognize grouping expressions in the main query, but only
 * grouping Vars in subqueries.  For example, this will be rejected,
 * although it could be allowed:
 *    SELECT (SELECT x FROM bar where y = (foo.a + foo.b))
 *    FROM foo
 *    GROUP BY a + b;
 * The difficulty is the need to account for different sublevels_up.
 * This appears to require a whole custom version of equal(), which is
 * way more pain than the feature seems worth.
 */
static void check_ungrouped_columns(Node *node, ParseState *pstate, Query *qry,
                                    List *groupClauses,
                                    List *groupClauseCommonVars,
                                    bool have_non_var_grouping,
                                    List **func_grouped_rels)
{
    check_ungrouped_columns_context context;

    context.pstate = pstate;
    context.qry = qry;
    context.root = NULL;
    context.groupClauses = groupClauses;
    context.groupClauseCommonVars = groupClauseCommonVars;
    context.have_non_var_grouping = have_non_var_grouping;
    context.func_grouped_rels = func_grouped_rels;
    context.sublevels_up = 0;
    context.in_agg_direct_args = false;
    check_ungrouped_columns_walker(node, &context);
}

static bool check_ungrouped_columns_walker(Node *node, check_ungrouped_columns_context *context)
{
    ListCell *gl;

    if (node == NULL)
        return false;

    if (IsA(node, Const) || IsA(node, Param))
        return false; /* constants are always acceptable */

    if (IsA(node, Aggref))
    {
        Aggref *agg = (Aggref *) node;

        if ((int) agg->agglevelsup == context->sublevels_up)
        {
            /*
             * If we find an aggregate call of the original level, do not
             * recurse into its normal arguments, ORDER BY arguments, or
             * filter; ungrouped vars there are not an error.  But we should
             * check direct arguments as though they weren't in an aggregate.
             * We set a special flag in the context to help produce a useful
             * error message for ungrouped vars in direct arguments.
             */
            bool result;

            Assert(!context->in_agg_direct_args);
            context->in_agg_direct_args = true;
            result = check_ungrouped_columns_walker((Node *) agg->aggdirectargs,
                                                    context);
            context->in_agg_direct_args = false;
            return result;
        }

        /*
         * We can skip recursing into aggregates of higher levels altogether,
         * since they could not possibly contain Vars of concern to us (see
         * transformAggregateCall).  We do need to look at aggregates of lower
         * levels, however.
         */
        if ((int) agg->agglevelsup > context->sublevels_up)
            return false;
    }

    if (IsA(node, GroupingFunc))
    {
        GroupingFunc *grp = (GroupingFunc *) node;

        /* handled GroupingFunc separately, no need to recheck at this level */

        if ((int) grp->agglevelsup >= context->sublevels_up)
            return false;
    }

    /*
     * If we have any GROUP BY items that are not simple Vars, check to see if
     * subexpression as a whole matches any GROUP BY item. We need to do this
     * at every recursion level so that we recognize GROUPed-BY expressions
     * before reaching variables within them. But this only works at the outer
     * query level, as noted above.
     */
    if (context->have_non_var_grouping && context->sublevels_up == 0)
    {
        foreach(gl, context->groupClauses)
        {
            TargetEntry *tle = lfirst(gl);

            if (equal(node, tle->expr))
                return false; /* acceptable, do not descend more */
        }
    }

    /*
     * If we have an ungrouped Var of the original query level, we have a
     * failure.  Vars below the original query level are not a problem, and
     * neither are Vars from above it.  (If such Vars are ungrouped as far as
     * their own query level is concerned, that's someone else's problem...)
     */
    if (IsA(node, Var))
    {
        Var *var = (Var *) node;
        RangeTblEntry *rte;
        char *attname;

        if (var->varlevelsup != context->sublevels_up)
            return false; /* it's not local to my query, ignore */

        /*
         * Check for a match, if we didn't do it above.
         */
        if (!context->have_non_var_grouping || context->sublevels_up != 0)
        {
            foreach(gl, context->groupClauses)
            {
                Var *gvar = (Var *) ((TargetEntry *) lfirst(gl))->expr;

                if (IsA(gvar, Var) &&
                    gvar->varno == var->varno &&
                    gvar->varattno == var->varattno &&
                    gvar->varlevelsup == 0)
                    return false; /* acceptable, we're okay */
            }
        }

        /*
         * Check whether the Var is known functionally dependent on the GROUP
         * BY columns.  If so, we can allow the Var to be used, because the
         * grouping is really a no-op for this table.  However, this deduction
         * depends on one or more constraints of the table, so we have to add
         * those constraints to the query's constraintDeps list, because it's
         * not semantically valid anymore if the constraint(s) get dropped.
         * (Therefore, this check must be the last-ditch effort before raising
         * error: we don't want to add dependencies unnecessarily.)
         *
         * Because this is a pretty expensive check, and will have the same
         * outcome for all columns of a table, we remember which RTEs we've
         * already proven functional dependency for in the func_grouped_rels
         * list.  This test also prevents us from adding duplicate entries to
         * the constraintDeps list.
         */
        if (list_member_int(*context->func_grouped_rels, var->varno))
            return false; /* previously proven acceptable */

        Assert(var->varno > 0 &&
               (int) var->varno <= list_length(context->pstate->p_rtable));
        rte = rt_fetch(var->varno, context->pstate->p_rtable);
        if (rte->rtekind == RTE_RELATION)
        {
            if (check_functional_grouping(rte->relid, var->varno, 0,
                                          context->groupClauseCommonVars,
                                          &context->qry->constraintDeps))
            {
                *context->func_grouped_rels = lappend_int(*context->func_grouped_rels,
                                                          var->varno);
                return false; /* acceptable */
            }
        }

        /* Found an ungrouped local variable; generate error message */
        attname = get_rte_attribute_name(rte, var->varattno);
        if (context->sublevels_up == 0)
            ereport(ERROR, (errcode(ERRCODE_GROUPING_ERROR),
                            errmsg("\"%s\" must be either part of an explicitly listed key or used inside an aggregate function",
                                   attname), context->in_agg_direct_args ?
                                       errdetail("Direct arguments of an ordered-set aggregate must use only grouped columns.") :
                                       0, parser_errposition(context->pstate, var->location)));
        else
            ereport(ERROR, (errcode(ERRCODE_GROUPING_ERROR),
                            errmsg("subquery uses ungrouped column \"%s.%s\" from outer query",
                                   rte->eref->aliasname, attname),
                                   parser_errposition(context->pstate, var->location)));
    }

    if (IsA(node, Query))
    {
        /* Recurse into subselects */
        bool result;

        context->sublevels_up++;
        result = query_tree_walker((Query *) node,
                                   check_ungrouped_columns_walker,
                                   (void *) context, 0);
        context->sublevels_up--;
        return result;
    }

    return expression_tree_walker(node, check_ungrouped_columns_walker,
                                  (void *) context);
}

/*
 * finalize_grouping_exprs -
 *	  Scan the given expression tree for GROUPING() and related calls,
 *	  and validate and process their arguments.
 *
 * This is split out from check_ungrouped_columns above because it needs
 * to modify the nodes (which it does in-place, not via a mutator) while
 * check_ungrouped_columns may see only a copy of the original thanks to
 * flattening of join alias vars. So here, we flatten each individual
 * GROUPING argument as we see it before comparing it.
 */
static void finalize_grouping_exprs(Node *node, ParseState *pstate, Query *qry,
                                    List *groupClauses, PlannerInfo *root,
                                    bool have_non_var_grouping)
{
    check_ungrouped_columns_context context;

    context.pstate = pstate;
    context.qry = qry;
    context.root = root;
    context.groupClauses = groupClauses;
    context.groupClauseCommonVars = NIL;
    context.have_non_var_grouping = have_non_var_grouping;
    context.func_grouped_rels = NULL;
    context.sublevels_up = 0;
    context.in_agg_direct_args = false;
    finalize_grouping_exprs_walker(node, &context);
}

static bool finalize_grouping_exprs_walker(Node *node,
                                           check_ungrouped_columns_context *context)
{
    ListCell *gl;

    if (node == NULL)
        return false;
    if (IsA(node, Const) || IsA(node, Param))
        return false; /* constants are always acceptable */

    if (IsA(node, Aggref))
    {
        Aggref *agg = (Aggref *) node;

        if ((int) agg->agglevelsup == context->sublevels_up)
        {
            /*
             * If we find an aggregate call of the original level, do not
             * recurse into its normal arguments, ORDER BY arguments, or
             * filter; GROUPING exprs of this level are not allowed there. But
             * check direct arguments as though they weren't in an aggregate.
             */
            bool result;

            Assert(!context->in_agg_direct_args);
            context->in_agg_direct_args = true;
            result = finalize_grouping_exprs_walker((Node *) agg->aggdirectargs,
                                                context);
            context->in_agg_direct_args = false;
            return result;
        }

        /*
         * We can skip recursing into aggregates of higher levels altogether,
         * since they could not possibly contain exprs of concern to us (see
         * transformAggregateCall).  We do need to look at aggregates of lower
         * levels, however.
         */
        if ((int) agg->agglevelsup > context->sublevels_up)
            return false;
    }

    if (IsA(node, GroupingFunc))
    {
        GroupingFunc *grp = (GroupingFunc *) node;

        /*
         * We only need to check GroupingFunc nodes at the exact level to
         * which they belong, since they cannot mix levels in arguments.
         */

        if ((int) grp->agglevelsup == context->sublevels_up)
        {
            ListCell *lc;
            List *ref_list = NIL;

            foreach(lc, grp->args)
            {
                Node *expr = lfirst(lc);
                Index ref = 0;

                if (context->root)
                    expr = flatten_join_alias_vars((Query*)context->root, expr);

                /*
                 * Each expression must match a grouping entry at the current
                 * query level. Unlike the general expression case, we don't
                 * allow functional dependencies or outer references.
                 */

                if (IsA(expr, Var))
                {
                    Var *var = (Var *) expr;

                    if (var->varlevelsup == context->sublevels_up)
                    {
                        foreach(gl, context->groupClauses)
                        {
                            TargetEntry *tle = lfirst(gl);
                            Var *gvar = (Var *) tle->expr;

                            if (IsA(gvar, Var) &&
                                gvar->varno == var->varno &&
                                gvar->varattno == var->varattno &&
                                gvar->varlevelsup == 0)
                            {
                                ref = tle->ressortgroupref;
                                break;
                            }
                        }
                    }
                }
                else if (context->have_non_var_grouping &&
                         context->sublevels_up == 0)
                {
                    foreach(gl, context->groupClauses)
                    {
                        TargetEntry *tle = lfirst(gl);

                        if (equal(expr, tle->expr))
                        {
                           ref = tle->ressortgroupref;
                           break;
                        }
                    }
                }

                if (ref == 0)
                    ereport(ERROR,
                            (errcode(ERRCODE_GROUPING_ERROR),
                             errmsg("arguments to GROUPING must be grouping expressions of the associated query level"),
                             parser_errposition(context->pstate,
                             exprLocation(expr))));

                ref_list = lappend_int(ref_list, ref);
            }

            grp->refs = ref_list;
        }

        if ((int) grp->agglevelsup > context->sublevels_up)
            return false;
    }

    if (IsA(node, Query))
    {
        /* Recurse into subselects */
        bool result;

        context->sublevels_up++;
        result = query_tree_walker((Query *) node,
                                   finalize_grouping_exprs_walker,
                                   (void *) context, 0);
        context->sublevels_up--;
        return result;
    }
    return expression_tree_walker(node, finalize_grouping_exprs_walker,
                                  (void *) context);
}


/*
 * Given a GroupingSet node, expand it and return a list of lists.
 *
 * For EMPTY nodes, return a list of one empty list.
 *
 * For SIMPLE nodes, return a list of one list, which is the node content.
 *
 * For CUBE and ROLLUP nodes, return a list of the expansions.
 *
 * For SET nodes, recursively expand contained CUBE and ROLLUP.
 */
static List * expand_groupingset_node(GroupingSet *gs)
{
    List *result = NIL;

    switch (gs->kind)
    {
        case GROUPING_SET_EMPTY:
            result = list_make1(NIL);
            break;

        case GROUPING_SET_SIMPLE:
            result = list_make1(gs->content);
            break;

        case GROUPING_SET_ROLLUP:
        {
            List *rollup_val = gs->content;
            ListCell *lc;
            int curgroup_size = list_length(gs->content);

            while (curgroup_size > 0)
            {
                List *current_result = NIL;
                int i = curgroup_size;

                foreach(lc, rollup_val)
                {
                    GroupingSet *gs_current = (GroupingSet *) lfirst(lc);

                    Assert(gs_current->kind == GROUPING_SET_SIMPLE);

                    current_result = list_concat(current_result,
                                                 list_copy(gs_current->content));

                    /* If we are done with making the current group, break */
                    if (--i == 0)
                        break;
                }

                result = lappend(result, current_result);
                --curgroup_size;
            }

            result = lappend(result, NIL);
        }
            break;

        case GROUPING_SET_CUBE:
        {
            List *cube_list = gs->content;
            int number_bits = list_length(cube_list);
            uint32 num_sets;
            uint32 i;

            /* parser should cap this much lower */
            Assert(number_bits < 31);

            num_sets = (1U << number_bits);

            for (i = 0; i < num_sets; i++)
            {
                List *current_result = NIL;
                ListCell *lc;
                uint32 mask = 1U;

                foreach(lc, cube_list)
                {
                    GroupingSet *gs_current = (GroupingSet *) lfirst(lc);

                    Assert(gs_current->kind == GROUPING_SET_SIMPLE);

                    if (mask & i)
                    {
                        current_result = list_concat(current_result,
                                                     list_copy(gs_current->content));
                    }

                    mask <<= 1;
                }

                result = lappend(result, current_result);
            }
        }
            break;

        case GROUPING_SET_SETS:
        {
            ListCell *lc;

            foreach(lc, gs->content)
            {
                List *current_result = expand_groupingset_node(lfirst(lc));

                result = list_concat(result, current_result);
            }
        }
            break;
    }

    return result;
}

static int cmp_list_len_asc(const void *a, const void *b)
{
    int la = list_length(*(List *const *) a);
    int lb = list_length(*(List *const *) b);

    return (la > lb) ? 1 : (la == lb) ? 0 : -1;
}

/*
 * Expand a groupingSets clause to a flat list of grouping sets.
 * The returned list is sorted by length, shortest sets first.
 *
 * This is mainly for the planner, but we use it here too to do
 * some consistency checks.
 */
static List * expand_grouping_sets(List *groupingSets, int limit)
{
    List *expanded_groups = NIL;
    List *result = NIL;
    double numsets = 1;
    ListCell *lc;

    if (groupingSets == NIL)
        return NIL;

    foreach(lc, groupingSets)
    {
        List *current_result = NIL;
        GroupingSet *gs = lfirst(lc);

        current_result = expand_groupingset_node(gs);

        Assert(current_result != NIL);

        numsets *= list_length(current_result);

        if (limit >= 0 && numsets > limit)
            return NIL;

        expanded_groups = lappend(expanded_groups, current_result);
    }

    /*
     * Do cartesian product between sublists of expanded_groups. While at it,
     * remove any duplicate elements from individual grouping sets (we must
     * NOT change the number of sets though)
     */

    foreach(lc, (List *) linitial(expanded_groups))
    {
        result = lappend(result, list_union_int(NIL, (List *) lfirst(lc)));
    }

    for_each_cell(lc, expanded_groups,
                  lnext(expanded_groups, list_head(expanded_groups)))
    {
        List *p = lfirst(lc);
        List *new_result = NIL;
        ListCell *lc2;

        foreach(lc2, result)
        {
            List *q = lfirst(lc2);
            ListCell *lc3;

            foreach(lc3, p)
            {
                new_result = lappend(new_result,
                                     list_union_int(q,(List *) lfirst(lc3)));
            }
        }
        result = new_result;
    }

    if (list_length(result) > 1)
    {
        int			result_len = list_length(result);
        List	  **buf = palloc(sizeof(List *) * result_len);
        List	  **ptr = buf;

        foreach(lc, result)
        {
            *ptr++ = lfirst(lc);
        }

        qsort(buf, result_len, sizeof(List *), cmp_list_len_asc);

        result = NIL;
        ptr = buf;

        while (result_len-- > 0)
            result = lappend(result, *ptr++);

        pfree_if_not_null(buf);
    }

    return result;
}
