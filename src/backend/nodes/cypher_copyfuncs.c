/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "postgres.h"

#include "nodes/extensible.h"

#include "nodes/cypher_copyfuncs.h"
#include "nodes/cypher_nodes.h"

/*
 * Copied From Postgres
 *
 * Macros to simplify copying of different kinds of fields.  Use these
 * wherever possible to reduce the chance for silly typos.  Note that these
 * hard-wire the convention that the local variables in a Copy routine are
 * named 'extended_newnode' and 'extended_from'.
 */

// Declare the local fields needed to copy extensible nodes
#define COPY_LOCALS(nodeTypeName) \
    nodeTypeName *extended_newnode = (nodeTypeName *)newnode; \
    nodeTypeName *extended_from = (nodeTypeName *)from; \
    Assert(is_ag_node(newnode, nodeTypeName)); \
    Assert(is_ag_node(from, nodeTypeName));


// Copy a simple scalar field (int, float, bool, enum, etc)
#define COPY_SCALAR_FIELD(fldname) \
        (extended_newnode->fldname = extended_from->fldname)

// Copy a field that is a pointer to some kind of Node or Node tree
#define COPY_NODE_FIELD(fldname) \
        (extended_newnode->fldname = copyObject(extended_from->fldname))

// Copy a field that is a pointer to a Bitmapset
#define COPY_BITMAPSET_FIELD(fldname) \
        (extended_newnode->fldname = bms_copy(extended_from->fldname))

// Copy a field that is a pointer to a C string, or perhaps NULL
#define COPY_STRING_FIELD(fldname) \
        (extended_newnode->fldname = extended_from->fldname ? \
            pstrdup(extended_from->fldname) : (char *) NULL)

/*
 * Default copy function for cypher nodes. For most nodes, we don't expect
 * the node to ever be copied. So throw an error.
 */
void copy_ag_node(ExtensibleNode *newnode,
                         const ExtensibleNode *oldnode)
{
    ereport(ERROR, (errmsg("unexpected copyObject() over ag_node")));
}

// copy function for cypher_create_target_nodes
void copy_cypher_create_target_nodes(ExtensibleNode *newnode, const ExtensibleNode *from)
{
    COPY_LOCALS(cypher_create_target_nodes);

    COPY_SCALAR_FIELD(flags);
    COPY_SCALAR_FIELD(graph_oid);

    COPY_NODE_FIELD(paths);
}

// copy function for cypher_create_path
void copy_cypher_create_path(ExtensibleNode *newnode, const ExtensibleNode *from)
{
    COPY_LOCALS(cypher_create_path);

    COPY_SCALAR_FIELD(path_attr_num);
    COPY_STRING_FIELD(var_name);
    COPY_NODE_FIELD(target_nodes);
}

// copy function for cypher_target_node
void copy_cypher_target_node(ExtensibleNode *newnode, const ExtensibleNode *from)
{
    COPY_LOCALS(cypher_target_node);

    COPY_SCALAR_FIELD(type);
    COPY_SCALAR_FIELD(flags);
    COPY_SCALAR_FIELD(dir);
    COPY_SCALAR_FIELD(prop_attr_num);
    COPY_SCALAR_FIELD(relid);
    COPY_SCALAR_FIELD(tuple_position);

    COPY_STRING_FIELD(label_name);
    COPY_STRING_FIELD(variable_name);

    COPY_NODE_FIELD(id_expr);
    COPY_NODE_FIELD(id_expr_state);
    COPY_NODE_FIELD(prop_expr);
    COPY_NODE_FIELD(prop_expr_state);
    COPY_NODE_FIELD(resultRelInfo);
    COPY_NODE_FIELD(elemTupleSlot);
}

// copy function for cypher_update_information
void copy_cypher_update_information(ExtensibleNode *newnode, const ExtensibleNode *from)
{
    COPY_LOCALS(cypher_update_information);

    COPY_NODE_FIELD(set_items);
    COPY_SCALAR_FIELD(flags);
    COPY_SCALAR_FIELD(tuple_position);
    COPY_STRING_FIELD(graph_name);
    COPY_STRING_FIELD(clause_name);
}

// copy function for cypher_update_item
void copy_cypher_update_item(ExtensibleNode *newnode, const ExtensibleNode *from)
{
    COPY_LOCALS(cypher_update_item);

    COPY_SCALAR_FIELD(prop_position);
    COPY_SCALAR_FIELD(entity_position);
    COPY_STRING_FIELD(var_name);
    COPY_STRING_FIELD(prop_name);
    COPY_NODE_FIELD(qualified_name);
    COPY_SCALAR_FIELD(remove_item);
    COPY_SCALAR_FIELD(is_add);
}

// copy function for cypher_delete_information
void copy_cypher_delete_information(ExtensibleNode *newnode, const ExtensibleNode *from)
{
    COPY_LOCALS(cypher_delete_information);

    COPY_NODE_FIELD(delete_items);
    COPY_SCALAR_FIELD(flags);
    COPY_STRING_FIELD(graph_name);
    COPY_SCALAR_FIELD(graph_oid);
    COPY_SCALAR_FIELD(detach);
}

// copy function for cypher_delete_item
void copy_cypher_delete_item(ExtensibleNode *newnode, const ExtensibleNode *from)
{
    COPY_LOCALS(cypher_delete_item);

    COPY_NODE_FIELD(entity_position);
    COPY_STRING_FIELD(var_name);
}

// copy function for cypher_merge_information
void copy_cypher_merge_information(ExtensibleNode *newnode, const ExtensibleNode *from)
{
    COPY_LOCALS(cypher_merge_information);

    COPY_SCALAR_FIELD(flags);
    COPY_SCALAR_FIELD(graph_oid);
    COPY_SCALAR_FIELD(merge_function_attr);
    COPY_NODE_FIELD(path);
}
