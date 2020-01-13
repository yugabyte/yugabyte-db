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
#include "utils/builtins.h"
#include "utils/lsyscache.h"

#include "agtype.h"
#include "cypher_clause.h"
#include "cypher_expr.h"
#include "cypher_nodes.h"
#include "nodes.h"

static void insert_dummy_clause(ParseState *pstate);
static Query *transform_cypher_create(ParseState *pstate,
                                      cypher_create *clause);
static Query *transform_cypher_return(ParseState *pstate,
                                      cypher_return *clause);
static List *transform_cypher_item_list(ParseState *pstate, List *items);

Query *transform_cypher_stmt(ParseState *pstate, List *stmt)
{
    /*
     *  XXX: current implementation is only for a single RETURN or CREATE
     *  clause
     */
    if (list_length(stmt) > 1)
        ereport(ERROR, (errmsg("unexpected query")));

    if (is_ag_node(linitial(stmt), cypher_return))
    {
        cypher_return *clause;

        clause = (cypher_return *)linitial(stmt);

        return transform_cypher_return(pstate, clause);
    }
    else if (is_ag_node(linitial(stmt), cypher_create))
    {
        cypher_create *clause;

        clause = (cypher_create *)linitial(stmt);

        return transform_cypher_create(pstate, clause);
    }
    else
    {
        ereport(ERROR, (errmsg("unexpected query")));
    }
}

/*
 * Postgres' optimizer phase pulls up simple subqueries. This optimization
 * can result in the set_rel_pathlist_hook not being called to turn the
 * modifying function calls into our CustomPath. To avoid this, we put a
 * dummy function call in the From clause, so the planner does not pull
 * up this sub-select and we can intercept and modify in the
 * set_rel_pathlist_hook.
 */
static void insert_dummy_clause(ParseState *pstate)
{
    FuncCall *fc;
    FuncExpr *fe;
    RangeFunction *rf;
    RangeTblEntry *rte;

    /*
     * We need to build the parse node tree structure, so we can use
     * Postgres' transform logic.
     */
    // Setup Function Call Node
    fc = makeFuncCall(list_make1(makeString("cypher_dummy_clause")), NIL, 0);

    /*
     * Use Postgres' tranform logic to create the FuncExpr that will
     * be inserted in the RangeTable
     */
    // Setup Range Table Node
    rf = makeNode(RangeFunction);
    rf->lateral = false;
    rf->ordinality = NULL;
    rf->is_rowsfrom = false;
    rf->functions = list_make1(list_make2(fc, NIL));
    rf->alias = NULL;
    rf->coldeflist = NIL;

    // Build a FuncExpr node from the FuncCall parse node
    fe = (FuncExpr *)transformExpr(pstate, (Node *)fc,
                                   EXPR_KIND_FROM_FUNCTION);

    // Add the FuncExpr node to the range table
    rte = addRangeTableEntryForFunction(
        pstate, lappend(NIL, FigureColname((Node *)fc)), lappend(NIL, fe),
        list_make1(NIL), rf, false, true);

    // Add the range table ref to pstate's joinlist
    addRTEtoQuery(pstate, rte, true, false, false);
}

static Query *transform_cypher_create(ParseState *pstate,
                                      cypher_create *clause)
{
    Const *pattern_const;
    Const *null_const;
    Expr *func_expr;
    Oid func_create_oid;
    Oid internal_type = INTERNALOID;
    Query *query;
    TargetEntry *tle;

    query = makeNode(Query);
    query->commandType = CMD_SELECT;
    query->targetList = NIL;

    func_create_oid = GetSysCacheOid3(
        PROCNAMEARGSNSP, PointerGetDatum("cypher_create_clause"),
        PointerGetDatum(buildoidvector(&internal_type, 1)),
        ObjectIdGetDatum(ag_catalog_namespace_id()));


    null_const = makeNullConst(AGTYPEOID, -1, InvalidOid);
    tle = makeTargetEntry((Expr *)null_const, pstate->p_next_resno++,
                          "cypher_create_null_value", false);
    query->targetList = lappend(query->targetList, tle);

    /*
     * Create the Const Node to hold the pattern. skip the parse node,
     * because we would not be able to control how our pointer to the
     * internal type is copied.
     */
    pattern_const = makeConst(internal_type, -1, InvalidOid, 1,
                              PointerGetDatum(clause->pattern), false, true);

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

    // In the simple CREATE case, add a dummy clause to the FROM clause
    insert_dummy_clause(pstate);

    query->rtable = pstate->p_rtable;
    query->jointree = makeFromExpr(pstate->p_joinlist, NULL);

    return query;
}

static Query *transform_cypher_return(ParseState *pstate,
                                      cypher_return *clause)
{
    Query *query;

    query = makeNode(Query);
    query->commandType = CMD_SELECT;

    query->targetList = transform_cypher_item_list(pstate, clause->items);

    query->jointree = makeFromExpr(NIL, NULL);

    assign_query_collations(pstate, query);

    return query;
}

static List *transform_cypher_item_list(ParseState *pstate, List *items)
{
    List *targets = NIL;
    ListCell *li;

    foreach (li, items)
    {
        ResTarget *item = lfirst(li);
        Node *expr;
        char *colname;
        TargetEntry *te;

        expr = transform_cypher_expr(pstate, item->val,
                                     EXPR_KIND_SELECT_TARGET);
        colname = (item->name ? item->name : FigureColname(item->val));

        te = makeTargetEntry((Expr *)expr, (AttrNumber)pstate->p_next_resno++,
                             colname, false);

        targets = lappend(targets, te);
    }

    return targets;
}
