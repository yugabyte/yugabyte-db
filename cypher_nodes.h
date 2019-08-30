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

typedef struct cypher_set
{
    ExtensibleNode extensible;
    List *items;
    bool is_remove;
} cypher_set;

typedef struct cypher_set_item
{
    ExtensibleNode extensible;
    Node *prop;
    Node *expr;
    bool is_add;
} cypher_set_item;

typedef struct cypher_delete
{
    ExtensibleNode extensible;
    bool        detach;
    List       *exprs;
} cypher_delete;

void out_cypher_return(StringInfo str, const ExtensibleNode *node);
void out_cypher_sort_item(StringInfo str, const ExtensibleNode *node);
void out_cypher_with(StringInfo str, const ExtensibleNode *node);
void out_cypher_set(StringInfo str, const ExtensibleNode *node);
void out_cypher_set_item(StringInfo str, const ExtensibleNode *node);
void out_cypher_delete(StringInfo str, const ExtensibleNode *node);

#endif
