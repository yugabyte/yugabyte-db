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
    "cypher_match",
    "cypher_create",
    "cypher_set",
    "cypher_set_item",
    "cypher_delete",
    "cypher_path",
    "cypher_node",
    "cypher_relationship",
    "cypher_bool_const",
    "cypher_param",
    "cypher_map",
    "cypher_list",
    "cypher_string_match"
};

#define DEFINE_NODE_METHODS(type) \
    { \
        CppAsString(type), \
        sizeof(type), \
        copy_ag_node, \
        equal_ag_node, \
        CppConcat(out_, type), \
        read_ag_node \
    }

const ExtensibleNodeMethods node_methods[] = {
    DEFINE_NODE_METHODS(cypher_return),
    DEFINE_NODE_METHODS(cypher_with),
    DEFINE_NODE_METHODS(cypher_match),
    DEFINE_NODE_METHODS(cypher_create),
    DEFINE_NODE_METHODS(cypher_set),
    DEFINE_NODE_METHODS(cypher_set_item),
    DEFINE_NODE_METHODS(cypher_delete),
    DEFINE_NODE_METHODS(cypher_path),
    DEFINE_NODE_METHODS(cypher_node),
    DEFINE_NODE_METHODS(cypher_relationship),
    DEFINE_NODE_METHODS(cypher_bool_const),
    DEFINE_NODE_METHODS(cypher_param),
    DEFINE_NODE_METHODS(cypher_map),
    DEFINE_NODE_METHODS(cypher_list),
    DEFINE_NODE_METHODS(cypher_string_match)
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
