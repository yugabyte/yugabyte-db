#ifndef AG_CYPHER_NODE_H
#define AG_CYPHER_NODE_H

#include "postgres.h"

#include "nodes/extensible.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"

#include "nodes.h"

typedef struct cypher_return
{
    ExtensibleNode extensible;
    bool distinct;
    List *items;
    List *order_by;
    Node *skip;
    Node *limit;
} cypher_return;

typedef struct cypher_return_item
{
    ExtensibleNode extensible;
    Node *expr;
    ColumnRef *name;
} cypher_return_item;

typedef enum cypher_order
{
    CYPHER_ORDER_ASC,
    CYPHER_ORDER_DESC
} cypher_order;

typedef struct cypher_sort_item
{
    ExtensibleNode extensible;
    Node *expr;
    cypher_order order;
} cypher_sort_item;

typedef struct cypher_with
{
    ExtensibleNode extensible;
    bool distinct;
    List *items;
    List *order_by;
    Node *skip;
    Node *limit;
    Node *where;
} cypher_with;

typedef struct cypher_set_clause
{
    ExtensibleNode extensible;
    bool is_remove;
    List *items;
} cypher_set_clause;

typedef struct cypher_set_prop
{
    ExtensibleNode extensible;
    Node       *prop;
    Node       *expr;
    bool        add;
} cypher_set_prop;

void out_cypher_return(StringInfo str, const ExtensibleNode *node);
void out_cypher_return_item(StringInfo str, const ExtensibleNode *node);
void out_cypher_sort_item(StringInfo str, const ExtensibleNode *node);
void out_cypher_with(StringInfo str, const ExtensibleNode *node);
void out_cypher_set_clause(StringInfo str, const ExtensibleNode *node);
void out_cypher_set_prop(StringInfo str, const ExtensibleNode *node);

#endif
