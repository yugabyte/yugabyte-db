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

#include "nodes/ag_nodes.h"
#include "nodes/cypher_copyfuncs.h"
#include "nodes/cypher_outfuncs.h"
#include "nodes/cypher_readfuncs.h"
#include "nodes/cypher_nodes.h"

static bool equal_ag_node(const ExtensibleNode *a, const ExtensibleNode *b);

// This list must match ag_node_tag.
const char *node_names[] = {
    "ag_node_invalid",
    "cypher_return",
    "cypher_with",
    "cypher_match",
    "cypher_create",
    "cypher_set",
    "cypher_set_item",
    "cypher_delete",
    "cypher_unwind",
    "cypher_merge",
    "cypher_path",
    "cypher_node",
    "cypher_relationship",
    "cypher_bool_const",
    "cypher_param",
    "cypher_map",
    "cypher_list",
    "cypher_comparison_aexpr",
    "cypher_comparison_boolexpr",
    "cypher_string_match",
    "cypher_typecast",
    "cypher_integer_const",
    "cypher_sub_pattern",
    "cypher_call",
    "cypher_create_target_nodes",
    "cypher_create_path",
    "cypher_target_node",
    "cypher_update_information",
    "cypher_update_item",
    "cypher_delete_information",
    "cypher_delete_item",
    "cypher_merge_information"
};

/*
 * Each node defined with this will have
 * an out function defined, but copy, equal,
 * and read will throw errors.
 */
#define DEFINE_NODE_METHODS(type) \
    { \
        CppAsString(type), \
        sizeof(type), \
        copy_ag_node, \
        equal_ag_node, \
        CppConcat(out_, type), \
        read_ag_node \
    }

/*
 *  Each node defined with this will have a
 *  copy, read, and write function defined.
 *  Equal will still throw an error.
 */
#define DEFINE_NODE_METHODS_EXTENDED(type) \
    { \
        CppAsString(type), \
        sizeof(type), \
        CppConcat(copy_, type), \
        equal_ag_node, \
        CppConcat(out_, type), \
        CppConcat(read_, type) \
    }

// This list must match ag_node_tag.
const ExtensibleNodeMethods node_methods[] = {
    DEFINE_NODE_METHODS(cypher_return),
    DEFINE_NODE_METHODS(cypher_with),
    DEFINE_NODE_METHODS(cypher_match),
    DEFINE_NODE_METHODS(cypher_create),
    DEFINE_NODE_METHODS(cypher_set),
    DEFINE_NODE_METHODS(cypher_set_item),
    DEFINE_NODE_METHODS(cypher_delete),
    DEFINE_NODE_METHODS(cypher_unwind),
    DEFINE_NODE_METHODS(cypher_merge),
    DEFINE_NODE_METHODS(cypher_path),
    DEFINE_NODE_METHODS(cypher_node),
    DEFINE_NODE_METHODS(cypher_relationship),
    DEFINE_NODE_METHODS(cypher_bool_const),
    DEFINE_NODE_METHODS(cypher_param),
    DEFINE_NODE_METHODS(cypher_map),
    DEFINE_NODE_METHODS(cypher_list),
    DEFINE_NODE_METHODS(cypher_comparison_aexpr),
    DEFINE_NODE_METHODS(cypher_comparison_boolexpr),
    DEFINE_NODE_METHODS(cypher_string_match),
    DEFINE_NODE_METHODS(cypher_typecast),
    DEFINE_NODE_METHODS(cypher_integer_const),
    DEFINE_NODE_METHODS(cypher_sub_pattern),
    DEFINE_NODE_METHODS(cypher_call),
    DEFINE_NODE_METHODS_EXTENDED(cypher_create_target_nodes),
    DEFINE_NODE_METHODS_EXTENDED(cypher_create_path),
    DEFINE_NODE_METHODS_EXTENDED(cypher_target_node),
    DEFINE_NODE_METHODS_EXTENDED(cypher_update_information),
    DEFINE_NODE_METHODS_EXTENDED(cypher_update_item),
    DEFINE_NODE_METHODS_EXTENDED(cypher_delete_information),
    DEFINE_NODE_METHODS_EXTENDED(cypher_delete_item),
    DEFINE_NODE_METHODS_EXTENDED(cypher_merge_information)
};

static bool equal_ag_node(const ExtensibleNode *a, const ExtensibleNode *b)
{
    ereport(ERROR, (errmsg("unexpected equal() over ag_node's")));
}

void register_ag_nodes(void)
{
    static bool initialized = false;
    int i;

    if (initialized)
        return;

    for (i = 0; i < lengthof(node_methods); i++)
        RegisterExtensibleNodeMethods(&node_methods[i]);

    initialized = true;
}

ExtensibleNode *_new_ag_node(Size size, ag_node_tag tag)
{
    ExtensibleNode *n;

    n = (ExtensibleNode *)palloc0fast(size);
    n->type = T_ExtensibleNode;
    n->extnodename = node_names[tag];

    return n;
}
