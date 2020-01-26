#ifndef AG_CYPHER_ITEM_H
#define AG_CYPHER_ITEM_H

#include "postgres.h"

#include "nodes/nodes.h"
#include "nodes/pg_list.h"
#include "nodes/primnodes.h"
#include "parser/parse_node.h"

#include "parser/cypher_parse_node.h"

TargetEntry *transform_cypher_item(cypher_parsestate *cpstate, Node *node,
                                   Node *expr, ParseExprKind expr_kind,
                                   char *colname, bool resjunk);
List *transform_cypher_item_list(cypher_parsestate *cpstate, List *item_list,
                                 ParseExprKind expr_kind);

#endif
