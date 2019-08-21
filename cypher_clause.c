#include "postgres.h"

#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "parser/analyze.h"
#include "parser/parse_node.h"

#include "cypher_clause.h"
#include "cypher_nodes.h"
#include "nodes.h"

// XXX: current implementation is only for passing regression tests.
Query *transform_cypher_stmt(ParseState *pstate, List *stmt)
{
    List *values;
    cypher_return *clause;
    ListCell *li;
    SelectStmt *sel;
    Query *query;

    if (list_length(stmt) > 1 || !is_ag_node(linitial(stmt), cypher_return))
        ereport(ERROR, (errmsg("unexpected query")));

    values = NIL;
    clause = (cypher_return *)linitial(stmt);
    foreach (li, clause->items)
    {
        ResTarget *item = lfirst(li);

        values = lappend(values, item->val);
    }

    sel = makeNode(SelectStmt);
    sel->valuesLists = list_make1(values);

    query = transformStmt(pstate, (Node *)sel);

    return query;
}
