#include "postgres.h"

#include "access/attnum.h"
#include "catalog/pg_type_d.h"
#include "nodes/makefuncs.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "nodes/primnodes.h"
#include "parser/parse_collate.h"
#include "parser/parse_expr.h"
#include "parser/parse_func.h"
#include "parser/parse_node.h"
#include "parser/parse_relation.h"
#include "parser/parse_target.h"
#include "parser/parsetree.h"

#include "nodes/ag_nodes.h"
#include "nodes/cypher_nodes.h"
#include "parser/cypher_clause.h"
#include "parser/cypher_expr.h"
#include "parser/cypher_parse_node.h"
#include "utils/ag_func.h"
#include "utils/agtype.h"

static Query *transform_cypher_return(cypher_parsestate *cpstate,
                                      cypher_clause *clause);
static List *transform_cypher_item_list(cypher_parsestate *cpstate,
                                        List *items);
static Query *transform_cypher_create(cypher_parsestate *cpstate,
                                      cypher_clause *clause);

static RangeTblEntry *transform_prev_cypher_clause(cypher_parsestate *cpstate,
                                                   cypher_clause *clause);
static Query *analyze_cypher_clause(cypher_clause *clause,
                                    cypher_parsestate *parent_cpstate);

Query *transform_cypher_clause(cypher_parsestate *cpstate,
                               cypher_clause *clause)
{
    Query *result;

    Node *self = clause->self;

    // examine the type of clause and call appropriate transform logic for it
    if (is_ag_node(self, cypher_return))
        result = transform_cypher_return(cpstate, clause);
    else if (is_ag_node(self, cypher_with))
        return NULL;
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

    query->targetList = transform_cypher_item_list(cpstate, self->items);

    query->rtable = pstate->p_rtable;
    query->jointree = makeFromExpr(pstate->p_joinlist, NULL);

    assign_query_collations(pstate, query);

    return query;
}

static List *transform_cypher_item_list(cypher_parsestate *cpstate,
                                        List *items)
{
    ParseState *pstate = (ParseState *)cpstate;
    List *targets = NIL;
    ListCell *li;

    foreach (li, items)
    {
        ResTarget *item = lfirst(li);
        Node *expr;
        char *colname;
        TargetEntry *te;

        expr = transform_cypher_expr(cpstate, item->val,
                                     EXPR_KIND_SELECT_TARGET);
        colname = (item->name ? item->name : FigureColname(item->val));

        te = makeTargetEntry((Expr *)expr, (AttrNumber)pstate->p_next_resno++,
                             colname, false);

        targets = lappend(targets, te);
    }

    return targets;
}

static Query *transform_cypher_create(cypher_parsestate *cpstate,
                                      cypher_clause *clause)
{
    ParseState *pstate = (ParseState *)cpstate;
    cypher_create *self = (cypher_create *)clause->self;
    Const *pattern_const;
    Const *null_const;
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
    pattern_const = makeConst(INTERNALOID, -1, InvalidOid, 1,
                              PointerGetDatum(self->pattern), false, true);

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

/*
 * This function is similar to transformFromClause() that is called with a
 * single RangeSubselect.
 */
static RangeTblEntry *transform_prev_cypher_clause(cypher_parsestate *cpstate,
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

    query = analyze_cypher_clause(clause, cpstate);

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
