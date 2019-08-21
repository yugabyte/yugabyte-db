#include "postgres.h"

#include "nodes/extensible.h"

#include "cypher_nodes.h"
#include "nodes.h"

static void copy_ag_node(ExtensibleNode *newnode,
                         const ExtensibleNode *oldnode);
static bool equal_ag_node(const ExtensibleNode *a, const ExtensibleNode *b);
static void read_ag_node(ExtensibleNode *node);

// This list must match ag_node_tag.
const char *node_names[] = {
    "cypher_return",
    "cypher_with",
    "cypher_set_clause",
    "cypher_set_prop"
};

#define DEFINE_NODE_METHODS(type) \
    { \
        #type, \
        sizeof(type), \
        copy_ag_node, \
        equal_ag_node, \
        out_##type, \
        read_ag_node \
    }

const ExtensibleNodeMethods node_methods[] = {
    DEFINE_NODE_METHODS(cypher_return),
    DEFINE_NODE_METHODS(cypher_with),
    DEFINE_NODE_METHODS(cypher_set_clause),
    DEFINE_NODE_METHODS(cypher_set_prop)
};

static void copy_ag_node(ExtensibleNode *newnode,
                         const ExtensibleNode *oldnode)
{
    ereport(ERROR, (errmsg("unexpected copyObject() over ag_node")));
}

static bool equal_ag_node(const ExtensibleNode *a, const ExtensibleNode *b)
{
    ereport(ERROR, (errmsg("unexpected equal() over ag_node's")));
}

static void read_ag_node(ExtensibleNode *node)
{
    ereport(ERROR, (errmsg("unexpected parseNodeString() for ag_node")));
}

void register_ag_nodes(void)
{
    int i;

    for (i = 0; i < lengthof(node_methods); i++)
        RegisterExtensibleNodeMethods(&node_methods[i]);
}

ExtensibleNode *new_ag_node(Size size, ag_node_tag tag)
{
    ExtensibleNode *n;

    n = (ExtensibleNode *)palloc0fast(size);
    n->type = T_ExtensibleNode;
    n->extnodename = node_names[tag];

    return n;
}
