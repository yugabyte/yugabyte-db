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

#ifndef AG_AG_NODES_H
#define AG_AG_NODES_H

#include "nodes/extensible.h"

/* This list must match node_names and node_methods. */
typedef enum ag_node_tag
{
    ag_node_invalid_t = 0,

    /* projection */
    cypher_return_t,
    cypher_with_t,
    /* reading clause */
    cypher_match_t,
    /* updating clause */
    cypher_create_t,
    cypher_set_t,
    cypher_set_item_t,
    cypher_delete_t,
    cypher_unwind_t,
    cypher_merge_t,
    /* pattern */
    cypher_path_t,
    cypher_node_t,
    cypher_relationship_t,
    /* expression */
    cypher_bool_const_t,
    cypher_param_t,
    cypher_map_t,
    cypher_map_projection_t,
    cypher_map_projection_element_t,
    cypher_list_t,
    /* comparison expression */
    cypher_comparison_aexpr_t,
    cypher_comparison_boolexpr_t,
    /* string match */
    cypher_string_match_t,
    /* typecast */
    cypher_typecast_t,
    /* integer constant */
    cypher_integer_const_t,
    /* sub patterns/queries */
    cypher_sub_pattern_t,
    cypher_sub_query_t,
    /* procedure calls */
    cypher_call_t,
    /* create data structures */
    cypher_create_target_nodes_t,
    cypher_create_path_t,
    cypher_target_node_t,
    /* set/remove data structures */
    cypher_update_information_t,
    cypher_update_item_t,
    /* delete data structures */
    cypher_delete_information_t,
    cypher_delete_item_t,
    cypher_merge_information_t
} ag_node_tag;

void register_ag_nodes(void);

ExtensibleNode *_new_ag_node(Size size, ag_node_tag tag);

#define new_ag_node(size, tag) \
    ( \
        AssertMacro((size) >= sizeof(ExtensibleNode)), \
        AssertMacro(tag != ag_node_invalid_t), \
        _new_ag_node(size, tag) \
    )

#define make_ag_node(type) \
    ((type *)new_ag_node(sizeof(type), CppConcat(type, _t)))

static inline bool _is_ag_node(Node *node, const char *extnodename)
{
    ExtensibleNode *extnode;

    if (!IsA(node, ExtensibleNode))
        return false;

    extnode = (ExtensibleNode *)node;
    if (strcmp(extnode->extnodename, extnodename) == 0)
        return true;

    return false;
}

#define is_ag_node(node, type) _is_ag_node((Node *)(node), CppAsString(type))
#define get_ag_node_tag(node) ((ag_node_tag)(((ExtensibleNode *)(node))->extnodename))

#endif
