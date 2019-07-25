#include "postgres.h"

#include "lib/stringinfo.h"
#include "nodes/extensible.h"
#include "nodes/nodes.h"

#include "cypher_nodes.h"

#define DEFINE_AG_NODE(type) \
    type *_node = (type *)node

#define write_node_field(field_name) \
    do \
    { \
        appendStringInfoString(str, " :" #field_name " "); \
        outNode(str, _node->field_name); \
    } while (0)

#define write_bool_field(field_name) \
    do \
    { \
        appendStringInfo(str, " :" #field_name " %s", \
                         _node->field_name ? "true" : "false"); \
    } while (0)

// write an enumerated-type field as an integer code
#define write_enum_field(field_name, enum_type) \
    do \
    { \
        appendStringInfo(str, " :" #field_name " %d", \
                         (int)_node->field_name); \
    } while (0)

void out_cypher_return(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_return);

    write_bool_field(distinct);
    write_node_field(items);
    write_node_field(order_by);
    write_node_field(skip);
    write_node_field(limit);
}

void out_cypher_return_item(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_return_item);

    write_node_field(expr);
    write_node_field(name);
}

void out_cypher_sort_item(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_sort_item);

    write_node_field(expr);
    write_enum_field(order, cypher_order);
}

void out_cypher_with(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_with);

    write_bool_field(distinct);
    write_node_field(items);
    write_node_field(order_by);
    write_node_field(skip);
    write_node_field(limit);
    write_node_field(where);
}
