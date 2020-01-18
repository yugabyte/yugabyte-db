#ifndef AG_CYPHER_EXPR_H
#define AG_CYPHER_EXPR_H

#include "postgres.h"

#include "nodes/nodes.h"
#include "parser/parse_node.h"

#include "parser/cypher_parse_node.h"

Node *transform_cypher_expr(cypher_parsestate *cpstate, Node *expr,
                            ParseExprKind expr_kind);

#endif
