#include "postgres.h"

#include "access/attnum.h"
#include "nodes/makefuncs.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "nodes/primnodes.h"
#include "parser/parse_node.h"
#include "parser/parse_target.h"

#include "parser/cypher_expr.h"
#include "parser/cypher_item.h"
#include "parser/cypher_parse_node.h"

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
                                 ParseExprKind expr_kind)
{
    List *target_list = NIL;
    ListCell *li;

    foreach (li, item_list)
    {
        ResTarget *item = lfirst(li);
        TargetEntry *te;

        te = transform_cypher_item(cpstate, item->val, NULL, expr_kind,
                                   item->name, false);

        target_list = lappend(target_list, te);
    }

    return target_list;
}
