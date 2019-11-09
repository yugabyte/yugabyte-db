#include "postgres.h"

#include "access/attnum.h"
#include "nodes/makefuncs.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "nodes/primnodes.h"
#include "parser/parse_collate.h"
#include "parser/parse_node.h"
#include "parser/parse_target.h"

#include "cypher_clause.h"
#include "cypher_expr.h"
#include "cypher_nodes.h"
#include "nodes.h"

static Query *transform_cypher_return(ParseState *pstate,
                                      cypher_return *clause);
static List *transform_cypher_item_list(ParseState *pstate, List *items);

Query *transform_cypher_stmt(ParseState *pstate, List *stmt)
{
    cypher_return *clause;

    // XXX: current implementation is only for RETURN clause
    if (list_length(stmt) > 1 || !is_ag_node(linitial(stmt), cypher_return))
        ereport(ERROR, (errmsg("unexpected query")));

    clause = (cypher_return *)linitial(stmt);

    return transform_cypher_return(pstate, clause);
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
