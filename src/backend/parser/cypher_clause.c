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
#include "rewrite/rewriteHandler.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"

#include "catalog/ag_graph.h"
#include "catalog/ag_label.h"
#include "commands/label_commands.h"
#include "nodes/ag_nodes.h"
#include "nodes/cypher_nodes.h"
#include "parser/cypher_clause.h"
#include "parser/cypher_expr.h"
#include "parser/cypher_item.h"
#include "parser/cypher_parse_node.h"
#include "utils/ag_cache.h"
#include "utils/ag_func.h"
#include "utils/agtype.h"
#include "utils/graphid.h"

typedef Query *(*transform_method) (cypher_parsestate *cpstate,
                                    cypher_clause *clause);

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
static Query *transform_cypher_clause_with_where(cypher_parsestate *cpstate,
                                                 transform_method transform,
                                                 cypher_clause *clause,
                                                 Node *where);

// reading clause
static Query *transform_cypher_match(cypher_parsestate *cpstate,
                                     cypher_clause *clause);
static Query *transform_cypher_match_pattern(cypher_parsestate *cpstate,
                                             cypher_clause *clause);
static cypher_node *get_node_from_pattern(ParseState *pstate, List *pattern);
static void transform_cypher_node(cypher_parsestate *cpstate,
                                  cypher_node *node, List **target_list);
static Node *make_vertex_expr(cypher_parsestate *cpstate, RangeTblEntry *rte,
                              char *label);

// updating clause
static Query *transform_cypher_create(cypher_parsestate *cpstate,
                                      cypher_clause *clause);
static List *transform_cypher_create_pattern(cypher_parsestate *cpstate,
                                             List *pattern);
static List *transform_cypher_create_path(cypher_parsestate *cpstate,
                                          cypher_path *cp);
static cypher_target_node *
transform_create_cypher_node(cypher_parsestate *cpstate, cypher_node *node);

// transform
#define transform_prev_cypher_clause(cpstate, prev_clause) \
    transform_cypher_clause_as_subquery(cpstate, transform_cypher_clause, \
                                        prev_clause)
static RangeTblEntry *transform_cypher_clause_as_subquery(
    cypher_parsestate *cpstate, transform_method transform,
    cypher_clause *clause);
static Query *analyze_cypher_clause(transform_method transform,
                                    cypher_clause *clause,
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
        return transform_cypher_match(cpstate, clause);
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
    cypher_with *self = (cypher_with *)clause->self;
    cypher_return *return_clause;
    cypher_clause *wrapper;

    // TODO: check that all items have an alias for each

    // WITH clause is basically RETURN clause with optional WHERE subclause
    return_clause = make_ag_node(cypher_return);
    return_clause->distinct = self->distinct;
    return_clause->items = self->items;
    return_clause->order_by = self->order_by;
    return_clause->skip = self->skip;
    return_clause->limit = self->limit;

    wrapper = palloc(sizeof(*wrapper));
    wrapper->self = (Node *)return_clause;
    wrapper->prev = clause->prev;

    return transform_cypher_clause_with_where(cpstate, transform_cypher_return,
                                              wrapper, self->where);
}

static Query *transform_cypher_clause_with_where(cypher_parsestate *cpstate,
                                                 transform_method transform,
                                                 cypher_clause *clause,
                                                 Node *where)
{
    ParseState *pstate = (ParseState *)cpstate;
    Query *query;

    if (where)
    {
        RangeTblEntry *rte;
        int rtindex;
        Node *qual;

        query = makeNode(Query);
        query->commandType = CMD_SELECT;

        rte = transform_cypher_clause_as_subquery(cpstate, transform, clause);
        rtindex = list_length(pstate->p_rtable);
        Assert(rtindex == 1); // rte is the only RangeTblEntry in pstate

        query->targetList = expandRelAttrs(pstate, rte, rtindex, 0, -1);

        markTargetListOrigins(pstate, query->targetList);

        // see transformWhereClause()
        qual = transform_cypher_expr(cpstate, where, EXPR_KIND_WHERE);
        qual = coerce_to_boolean(pstate, qual, "WHERE");

        query->rtable = pstate->p_rtable;
        query->jointree = makeFromExpr(pstate->p_joinlist, qual);

        assign_query_collations(pstate, query);
    }
    else
    {
        query = transform(cpstate, clause);
    }

    return query;
}

static Query *transform_cypher_match(cypher_parsestate *cpstate,
                                     cypher_clause *clause)
{
    cypher_match *self = (cypher_match *)clause->self;

    return transform_cypher_clause_with_where(cpstate,
                                              transform_cypher_match_pattern,
                                              clause, self->where);
}

static Query *transform_cypher_match_pattern(cypher_parsestate *cpstate,
                                             cypher_clause *clause)
{
    ParseState *pstate = (ParseState *)cpstate;
    cypher_match *self = (cypher_match *)clause->self;
    Query *query;

    query = makeNode(Query);
    query->commandType = CMD_SELECT;

    if (clause->prev)
    {
        RangeTblEntry *rte;
        int rtindex;

        rte = transform_prev_cypher_clause(cpstate, clause->prev);
        rtindex = list_length(pstate->p_rtable);
        Assert(rtindex == 1); // rte is the first RangeTblEntry in pstate

        /*
         * add all the target entries in rte to the current target list to pass
         * all the variables that are introduced in the previous clause to the
         * next clause
         */
        query->targetList = expandRelAttrs(pstate, rte, rtindex, 0, -1);
    }

    // TODO: transform self->pattern into a connected component

    /*
     * TODO: transform the connected component into RangeTblEntry's and
     *       TargetEntry's
     */
    // NOTE: for now, only patterns that have a single node are supported
    transform_cypher_node(cpstate,
                          get_node_from_pattern(pstate, self->pattern),
                          &query->targetList);

    markTargetListOrigins(pstate, query->targetList);

    query->rtable = pstate->p_rtable;
    query->jointree = makeFromExpr(pstate->p_joinlist, NULL);

    assign_query_collations(pstate, query);

    return query;
}

/*
 * NOTE: a temporary logic that checks whether given pattern has only 1 node
 *       and returns the node
 */
static cypher_node *get_node_from_pattern(ParseState *pstate, List *pattern)
{
    cypher_path *path;
    cypher_node *node;

    // a pattern has at least 1 path that has at least 1 node
    path = linitial(pattern);
    node = linitial(path->path);

    // only 1 path
    if (list_length(pattern) > 1 || list_length(path->path) > 1)
    {
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("MATCH clause can have only 1 node"),
                 parser_errposition(pstate, path->location)));
    }

    return node;
}

static void transform_cypher_node(cypher_parsestate *cpstate,
                                  cypher_node *node, List **target_list)
{
    ParseState *pstate = (ParseState *)cpstate;
    char *schema_name;
    char *rel_name;
    RangeVar *label_range_var;
    Alias *alias;
    RangeTblEntry *rte;
    int resno;
    TargetEntry *te;

    /*
     * NOTE: for now, nodes without a name are not supported because
     *       patterns can have only 1 node
     */
    if (!node->name)
    {
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("nodes without a name are not supported"),
                 parser_errposition(pstate, node->location)));
    }

    // NOTE: for now, nodes without a label are not supported
    if (!node->label)
    {
        node->label = "_ag_label_vertex";
    }

    // NOTE: for now, nodes with a property condition are not supported
    if (node->props)
    {
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("nodes with a property condition are not supported"),
                 parser_errposition(pstate, node->location)));
    }

    schema_name = get_graph_namespace_name(cpstate->graph_name);
    rel_name = get_label_relation_name(node->label, cpstate->graph_oid);
    label_range_var = makeRangeVar(schema_name, rel_name, -1);
    alias = makeAlias(node->name, NIL);

    rte = addRangeTableEntry(pstate, label_range_var, alias,
                             label_range_var->inh, true);
    /*
     * relation is visible (r.a in expression works) but attributes in the
     * relation are not visible (a in expression doesn't work)
     */
    addRTEtoQuery(pstate, rte, true, true, false);

    resno = pstate->p_next_resno++;
    te = makeTargetEntry((Expr *)make_vertex_expr(cpstate, rte, node->label),
                         resno, node->name, false);
    *target_list = lappend(*target_list, te);
}

static Node *make_vertex_expr(cypher_parsestate *cpstate, RangeTblEntry *rte,
                              char *label)
{
    ParseState *pstate = (ParseState *)cpstate;
    Oid label_name_func_oid;
    Oid func_oid;
    Node *id;
    Const *graph_oid_const;
    Node *props;
    List *args, *label_name_args;
    FuncExpr *func_expr;
    FuncExpr *label_name_func_expr;

    func_oid = get_ag_func_oid("_agtype_build_vertex", 3, GRAPHIDOID,
                               CSTRINGOID, AGTYPEOID);

    id = scanRTEForColumn(pstate, rte, "id", -1, 0, NULL);

    label_name_func_oid = get_ag_func_oid("_label_name", 2, OIDOID,
                                          GRAPHIDOID);

    graph_oid_const = makeConst(OIDOID, -1, InvalidOid, sizeof(Oid),
                                ObjectIdGetDatum(cpstate->graph_oid), false,
                                true);

    label_name_args = list_make2(graph_oid_const, id);

    label_name_func_expr = makeFuncExpr(label_name_func_oid, CSTRINGOID,
                                        label_name_args, InvalidOid,
                                        InvalidOid, COERCE_EXPLICIT_CALL);
    label_name_func_expr->location = -1;

    props = scanRTEForColumn(pstate, rte, "properties", -1, 0, NULL);

    args = list_make3(id, label_name_func_expr, props);

    func_expr = makeFuncExpr(func_oid, AGTYPEOID, args, InvalidOid, InvalidOid,
                             COERCE_EXPLICIT_CALL);
    func_expr->location = -1;

    return (Node *)func_expr;
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
    List *transformed_pattern = NIL;

    Assert(list_length(pattern) == 1);

    foreach (lc, pattern)
    {
        List *transformed_path;

        transformed_path = transform_cypher_create_path(cpstate, lfirst(lc));

        transformed_pattern = lappend(transformed_pattern, transformed_path);
    }

    return transformed_pattern;
}

static List *transform_cypher_create_path(cypher_parsestate *cpstate,
                                          cypher_path *path)
{
    ListCell *lc;
    List *transformed_path = NIL;

    foreach (lc, path->path)
    {
        if (is_ag_node(lfirst(lc), cypher_node))
        {
            cypher_node *node = lfirst(lc);

            cypher_target_node *rel = transform_create_cypher_node(cpstate,
                                                                   node);

            transformed_path = lappend(transformed_path, rel);
        }
        else if (is_ag_node(lfirst(lc), cypher_relationship))
        {
            cypher_relationship *edge = lfirst(lc);

            if (!edge->label)
                ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                                errmsg("cannot create a edge without a label"),
                                parser_errposition(&cpstate->pstate,
                                                   edge->location)));

            // create the label entry if it does not exist
            if (!label_exists(edge->label, cpstate->graph_oid))
            {
                List *parent;
                RangeVar *rv;

                rv = get_label_range_var(cpstate->graph_name, cpstate->graph_oid,
                                        AG_DEFAULT_LABEL_EDGE);

                parent = list_make1(rv);

                create_label(cpstate->graph_name, edge->label, LABEL_TYPE_EDGE,
                             parent);
            }

            //exit, only create the first vertex for now
            return transformed_path;
        }
        else
        {
            ereport(ERROR,
                    (errmsg_internal("unreconized node in create pattern")));
        }
    }

    return transformed_path;
}

static cypher_target_node *
transform_create_cypher_node(cypher_parsestate *cpstate, cypher_node *node)
{
    cypher_target_node *rel = palloc(sizeof(cypher_target_node));
    List *targetList = NIL;
    Expr *id, *properties;
    Relation label_relation;
    RangeVar *rv;
    RangeTblEntry *rte;
    TargetEntry *te;

    if (!node->label)
        node->label = AG_DEFAULT_LABEL_VERTEX;

    // create the label entry if it does not exist
    if (!label_exists(node->label, cpstate->graph_oid))
    {
        List *parent;
        RangeVar *rv;

        rv = get_label_range_var(cpstate->graph_name, cpstate->graph_oid,
                                 AG_DEFAULT_LABEL_VERTEX);

        parent = list_make1(rv);

        create_label(cpstate->graph_name, node->label, LABEL_TYPE_VERTEX, parent);
    }

    // lock the relation of the label
    rv = makeRangeVar(cpstate->graph_name, node->label, -1);
    label_relation = parserOpenTable(&cpstate->pstate, rv, RowExclusiveLock);

    // Store the relid
    rel->relid = RelationGetRelid(label_relation);

    rte = addRangeTableEntryForRelation((ParseState *)cpstate, label_relation,
                                        NULL, false, false);
    rte->requiredPerms = ACL_INSERT;

    // Store the relid
    rel->relid = RelationGetRelid(label_relation);

    // Build Id expression, always use the default logic
    id = (Expr *)build_column_default(label_relation, Anum_ag_label_table_id);

    te = makeTargetEntry(id, InvalidAttrNumber, "id", false);
    targetList = lappend(targetList, te);

    // Build properties expression, if no map is given, use the default logic
    if (node->props)
        properties = (Expr *)transform_cypher_expr(cpstate, node->props,
                                                   EXPR_KIND_INSERT_TARGET);
    else
        properties = (Expr *)build_column_default(
            label_relation, Anum_ag_label_table_properties);

    te = makeTargetEntry(properties, InvalidAttrNumber, "properties", false);

    targetList = lappend(targetList, te);
    rel->targetList = targetList;

    // Keep the lock
    heap_close(label_relation, NoLock);

    rel->expr_states = NIL;

    return rel;
}

/*
 * This function is similar to transformFromClause() that is called with a
 * single RangeSubselect.
 */
static RangeTblEntry *transform_cypher_clause_as_subquery(
    cypher_parsestate *cpstate, transform_method transform,
    cypher_clause *clause)
{
    ParseState *pstate = (ParseState *)cpstate;
    const bool lateral = false;
    Query *query;
    RangeTblEntry *rte;

    Assert(pstate->p_expr_kind == EXPR_KIND_NONE);
    pstate->p_expr_kind = EXPR_KIND_FROM_SUBSELECT;
    // p_lateral_active is false since query is the only FROM clause item here.
    pstate->p_lateral_active = lateral;

    query = analyze_cypher_clause(transform, clause, cpstate);

    pstate->p_lateral_active = false;
    pstate->p_expr_kind = EXPR_KIND_NONE;

    rte = addRangeTableEntryForSubquery(pstate, query, makeAlias("_", NIL),
                                        lateral, true);

    /*
     * NOTE: skip namespace conflicts check since rte will be the only
     *       RangeTblEntry in pstate
     */

    Assert(list_length(pstate->p_rtable) == 1);
    // all variables(attributes) from the previous clause(subquery) are visible
    addRTEtoQuery(pstate, rte, true, false, true);

    return rte;
}

static Query *analyze_cypher_clause(transform_method transform,
                                    cypher_clause *clause,
                                    cypher_parsestate *parent_cpstate)
{
    cypher_parsestate *cpstate;
    Query *query;

    cpstate = make_cypher_parsestate(parent_cpstate);

    query = transform(cpstate, clause);

    free_cypher_parsestate(cpstate);

    return query;
}
