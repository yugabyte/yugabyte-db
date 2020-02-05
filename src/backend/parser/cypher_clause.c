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

#include "catalog/pg_type_d.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "nodes/primnodes.h"
#include "optimizer/var.h"
#include "parser/parse_clause.h"
#include "parser/parse_coerce.h"
#include "parser/parse_collate.h"
#include "parser/parse_expr.h"
#include "parser/parse_func.h"
#include "parser/parse_node.h"
#include "parser/parse_relation.h"
#include "parser/parse_target.h"
#include "parser/parsetree.h"

#include "catalog/ag_label.h"
#include "commands/label_commands.h"
#include "nodes/ag_nodes.h"
#include "nodes/cypher_nodes.h"
#include "parser/cypher_clause.h"
#include "parser/cypher_expr.h"
#include "parser/cypher_item.h"
#include "parser/cypher_parse_node.h"
#include "utils/ag_func.h"
#include "utils/agtype.h"

// projection
static Query *transform_cypher_return(cypher_parsestate *cpstate,
                                      cypher_clause *clause);
static List *transform_cypher_order_by(cypher_parsestate *cpstate,
                                       List *sort_items, List **target_list,
                                       ParseExprKind expr_kind);
static TargetEntry *find_target_list_entry(cypher_parsestate *cpstate,
                                           Node *node, List **target_list,
                                           ParseExprKind expr_kind);
static Node *transform_cypher_limit(cypher_parsestate *cpstate, Node *node,
                                    ParseExprKind expr_kind,
                                    const char *construct_name);
static Query *transform_cypher_with(cypher_parsestate *cpstate,
                                    cypher_clause *clause);

// updating clause
static Query *transform_cypher_create(cypher_parsestate *cpstate,
                                      cypher_clause *clause);
static List *transform_cypher_create_pattern(cypher_parsestate *cpstate,
                                             List *pattern);
static cypher_path *transform_cypher_create_path(cypher_parsestate *cpstate,
                                                 cypher_path *cp);

static RangeTblEntry *transform_prev_cypher_clause(cypher_parsestate *cpstate,
                                                   cypher_clause *prev_clause);
static Query *analyze_cypher_clause(cypher_clause *clause,
                                    cypher_parsestate *parent_cpstate);

Query *transform_cypher_clause(cypher_parsestate *cpstate,
                               cypher_clause *clause)
{
    Node *self = clause->self;
    Query *result;

    // examine the type of clause and call the transform logic for it
    if (is_ag_node(self, cypher_return))
        result = transform_cypher_return(cpstate, clause);
    else if (is_ag_node(self, cypher_with))
        return transform_cypher_with(cpstate, clause);
    else if (is_ag_node(self, cypher_match))
        return NULL;
    else if (is_ag_node(self, cypher_create))
        result = transform_cypher_create(cpstate, clause);
    else if (is_ag_node(self, cypher_set))
        return NULL;
    else if (is_ag_node(self, cypher_delete))
        return NULL;
    else
        ereport(ERROR, (errmsg_internal("unexpected Node for cypher_clause")));

    result->querySource = QSRC_ORIGINAL;
    result->canSetTag = true;

    return result;
}

static Query *transform_cypher_return(cypher_parsestate *cpstate,
                                      cypher_clause *clause)
{
    ParseState *pstate = (ParseState *)cpstate;
    cypher_return *self = (cypher_return *)clause->self;
    Query *query;

    query = makeNode(Query);
    query->commandType = CMD_SELECT;

    if (clause->prev)
        transform_prev_cypher_clause(cpstate, clause->prev);

    query->targetList = transform_cypher_item_list(cpstate, self->items,
                                                   EXPR_KIND_SELECT_TARGET);

    markTargetListOrigins(pstate, query->targetList);

    // ORDER BY
    query->sortClause = transform_cypher_order_by(cpstate, self->order_by,
                                                  &query->targetList,
                                                  EXPR_KIND_ORDER_BY);

    // TODO: auto GROUP BY for aggregation

    // DISTINCT
    if (self->distinct)
    {
        query->distinctClause = transformDistinctClause(pstate,
                                                        &query->targetList,
                                                        query->sortClause,
                                                        false);
        query->hasDistinctOn = false;
    }
    else
    {
        query->distinctClause = NIL;
        query->hasDistinctOn = false;
    }

    // SKIP and LIMIT
    query->limitOffset = transform_cypher_limit(cpstate, self->skip,
                                                EXPR_KIND_OFFSET, "SKIP");
    query->limitCount = transform_cypher_limit(cpstate, self->limit,
                                               EXPR_KIND_LIMIT, "LIMIT");

    query->rtable = pstate->p_rtable;
    query->jointree = makeFromExpr(pstate->p_joinlist, NULL);

    assign_query_collations(pstate, query);

    return query;
}

// see transformSortClause()
static List *transform_cypher_order_by(cypher_parsestate *cpstate,
                                       List *sort_items, List **target_list,
                                       ParseExprKind expr_kind)
{
    ParseState *pstate = (ParseState *)cpstate;
    List *sort_list = NIL;
    ListCell *li;

    foreach (li, sort_items)
    {
        SortBy *sort_by = lfirst(li);
        TargetEntry *te;

        te = find_target_list_entry(cpstate, sort_by->node, target_list,
                                    expr_kind);

        sort_list = addTargetToSortList(pstate, te, sort_list, *target_list,
                                        sort_by);
    }

    return sort_list;
}

// see findTargetlistEntrySQL99()
static TargetEntry *find_target_list_entry(cypher_parsestate *cpstate,
                                           Node *node, List **target_list,
                                           ParseExprKind expr_kind)
{
    Node *expr;
    ListCell *lt;
    TargetEntry *te;

    expr = transform_cypher_expr(cpstate, node, expr_kind);

    foreach (lt, *target_list)
    {
        Node *te_expr;

        te = lfirst(lt);
        te_expr = strip_implicit_coercions((Node *)te->expr);

        if (equal(expr, te_expr))
            return te;
    }

    te = transform_cypher_item(cpstate, node, expr, expr_kind, NULL, true);

    *target_list = lappend(*target_list, te);

    return te;
}

// see transformLimitClause()
static Node *transform_cypher_limit(cypher_parsestate *cpstate, Node *node,
                                    ParseExprKind expr_kind,
                                    const char *construct_name)
{
    ParseState *pstate = (ParseState *)cpstate;
    Node *qual;

    if (!node)
        return NULL;

    qual = transform_cypher_expr(cpstate, node, expr_kind);

    qual = coerce_to_specific_type(pstate, qual, INT8OID, construct_name);

    // LIMIT can't refer to any variables of the current query.
    if (contain_vars_of_level(qual, 0))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_COLUMN_REFERENCE),
                 errmsg("argument of %s must not contain variables",
                        construct_name),
                 parser_errposition(pstate, locate_var_of_level(qual, 0))));
    }

    return qual;
}

static Query *transform_cypher_with(cypher_parsestate *cpstate,
                                    cypher_clause *clause)
{
    ParseState *pstate = (ParseState *)cpstate;
    cypher_with *self = (cypher_with *)clause->self;
    cypher_return *return_clause;
    cypher_clause *prev_clause;
    Query *query;
    RangeTblEntry *rte;
    int rtindex;
    Node *qual;

    return_clause = make_ag_node(cypher_return);
    return_clause->distinct = self->distinct;
    return_clause->items = self->items;
    return_clause->order_by = self->order_by;
    return_clause->skip = self->skip;
    return_clause->limit = self->limit;

    prev_clause = palloc(sizeof(*prev_clause));
    prev_clause->self = (Node *)return_clause;
    prev_clause->prev = clause->prev;

    query = makeNode(Query);
    query->commandType = CMD_SELECT;

    rte = transform_prev_cypher_clause(cpstate, prev_clause);
    rtindex = list_length(pstate->p_rtable);
    Assert(rtindex == 1); // rte is the only RangeTblEntry in pstate

    query->targetList = expandRelAttrs(pstate, rte, rtindex, 0, -1);

    markTargetListOrigins(pstate, query->targetList);

    // see transformWhereClause()
    if (self->where)
    {
        qual = transform_cypher_expr(cpstate, self->where, EXPR_KIND_WHERE);
        qual = coerce_to_boolean(pstate, qual, "WHERE");
    }
    else
    {
        qual = NULL;
    }

    query->rtable = pstate->p_rtable;
    query->jointree = makeFromExpr(pstate->p_joinlist, qual);

    assign_query_collations(pstate, query);

    return query;
}

static Query *transform_cypher_create(cypher_parsestate *cpstate,
                                      cypher_clause *clause)
{
    ParseState *pstate = (ParseState *)cpstate;
    cypher_create *self = (cypher_create *)clause->self;
    Const *pattern_const;
    Const *null_const;
    List *transformed_pattern;
    Expr *func_expr;
    Oid func_create_oid;
    Query *query;
    TargetEntry *tle;

    query = makeNode(Query);
    query->commandType = CMD_SELECT;
    query->targetList = NIL;

    func_create_oid = get_ag_func_oid("_cypher_create_clause", 1, INTERNALOID);

    null_const = makeNullConst(AGTYPEOID, -1, InvalidOid);
    tle = makeTargetEntry((Expr *)null_const, pstate->p_next_resno++,
                          "cypher_create_null_value", false);
    query->targetList = lappend(query->targetList, tle);

    /*
     * Create the Const Node to hold the pattern. skip the parse node,
     * because we would not be able to control how our pointer to the
     * internal type is copied.
     */
    transformed_pattern = transform_cypher_create_pattern(cpstate,
                                                          self->pattern);
    pattern_const = makeConst(INTERNALOID, -1, InvalidOid, 1,
                              PointerGetDatum(transformed_pattern), false,
                              true);

    /*
     * Create the FuncExpr Node.
     * NOTE: We can't use Postgres' transformExpr function, because it will
     * recursively transform the arguments, and our internal type would
     * force an error to be thrown.
     */
    func_expr = (Expr *)makeFuncExpr(func_create_oid, AGTYPEOID,
                                     list_make1(pattern_const), InvalidOid,
                                     InvalidOid, COERCE_EXPLICIT_CALL);

    // Create the target entry
    tle = makeTargetEntry(func_expr, pstate->p_next_resno++,
                          "cypher_create_clause", false);
    query->targetList = lappend(query->targetList, tle);

    query->rtable = pstate->p_rtable;
    query->jointree = makeFromExpr(pstate->p_joinlist, NULL);

    return query;
}

static List *transform_cypher_create_pattern(cypher_parsestate *cpstate,
                                             List *pattern)
{
    ListCell *lc;

    Assert(list_length(pattern) == 1);

    foreach (lc, pattern)
    {
        transform_cypher_create_path(cpstate, lfirst(lc));
    }

    return pattern;
}

static cypher_path *transform_cypher_create_path(cypher_parsestate *cpstate,
                                                 cypher_path *path)
{
    ListCell *lc;

    foreach (lc, path->path)
    {
        if (is_ag_node(lfirst(lc), cypher_node))
        {
            cypher_node *node = lfirst(lc);

            if (node->label && !label_exists(node->label, cpstate->graph_oid))
                create_vertex_label(cpstate->graph_name, node->label);
        }
        else if (is_ag_node(lfirst(lc), cypher_relationship))
        {
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("edges are not supported in CREATE clause")));
        }
        else
        {
            ereport(ERROR,
                    (errmsg_internal("unreconized node in create pattern")));
        }
    }

    return path;
}

/*
 * This function is similar to transformFromClause() that is called with a
 * single RangeSubselect.
 */
static RangeTblEntry *transform_prev_cypher_clause(cypher_parsestate *cpstate,
                                                   cypher_clause *prev_clause)
{
    ParseState *pstate = (ParseState *)cpstate;
    const bool lateral = false;
    Query *query;
    RangeTblEntry *rte;

    Assert(pstate->p_expr_kind == EXPR_KIND_NONE);
    pstate->p_expr_kind = EXPR_KIND_FROM_SUBSELECT;
    // p_lateral_active is false since query is the only FROM clause item here.
    pstate->p_lateral_active = lateral;

    query = analyze_cypher_clause(prev_clause, cpstate);

    pstate->p_lateral_active = false;
    pstate->p_expr_kind = EXPR_KIND_NONE;

    rte = addRangeTableEntryForSubquery(pstate, query, makeAlias("_", NIL),
                                        lateral, true);

    /*
     * NOTE: skip namespace conflicts check since rte will be the only
     *       RangeTblEntry in pstate
     */

    Assert(list_length(pstate->p_rtable) == 1);
    addRTEtoQuery(pstate, rte, true, true, true);

    return rte;
}

static Query *analyze_cypher_clause(cypher_clause *clause,
                                    cypher_parsestate *parent_cpstate)
{
    cypher_parsestate *cpstate;
    Query *query;

    cpstate = make_cypher_parsestate(parent_cpstate);

    query = transform_cypher_clause(cpstate, clause);

    free_cypher_parsestate(cpstate);

    return query;
}
