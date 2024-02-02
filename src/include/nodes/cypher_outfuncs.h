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

#ifndef AG_CYPHER_OUTFUNCS_H
#define AG_CYPHER_OUTFUNCS_H

/*
 * Serialization functions for AGE's ExtensibleNodes. We assign
 * each node to its serialized function in the DEFINE_NODE_METHODS
 * and DEFINE_NODE_METHODS_EXTENDED macros in ag_nodes.c.
 */

// clauses
void out_cypher_return(StringInfo str, const ExtensibleNode *node);
void out_cypher_with(StringInfo str, const ExtensibleNode *node);
void out_cypher_match(StringInfo str, const ExtensibleNode *node);
void out_cypher_create(StringInfo str, const ExtensibleNode *node);
void out_cypher_set(StringInfo str, const ExtensibleNode *node);
void out_cypher_set_item(StringInfo str, const ExtensibleNode *node);
void out_cypher_delete(StringInfo str, const ExtensibleNode *node);
void out_cypher_unwind(StringInfo str, const ExtensibleNode *node);
void out_cypher_merge(StringInfo str, const ExtensibleNode *node);

// pattern
void out_cypher_path(StringInfo str, const ExtensibleNode *node);
void out_cypher_node(StringInfo str, const ExtensibleNode *node);
void out_cypher_relationship(StringInfo str, const ExtensibleNode *node);

// expression
void out_cypher_bool_const(StringInfo str, const ExtensibleNode *node);
void out_cypher_param(StringInfo str, const ExtensibleNode *node);
void out_cypher_map(StringInfo str, const ExtensibleNode *node);
void out_cypher_list(StringInfo str, const ExtensibleNode *node);

// comparison expression
void out_cypher_comparison_aexpr(StringInfo str, const ExtensibleNode *node);
void out_cypher_comparison_boolexpr(StringInfo str, const ExtensibleNode *node);

// string match
void out_cypher_string_match(StringInfo str, const ExtensibleNode *node);

// typecast
void out_cypher_typecast(StringInfo str, const ExtensibleNode *node);

// integer constant
void out_cypher_integer_const(StringInfo str, const ExtensibleNode *node);

// sub pattern
void out_cypher_sub_pattern(StringInfo str, const ExtensibleNode *node);

// procedure call

void out_cypher_call(StringInfo str, const ExtensibleNode *node);

// create private data structures
void out_cypher_create_target_nodes(StringInfo str, const ExtensibleNode *node);
void out_cypher_create_path(StringInfo str, const ExtensibleNode *node);
void out_cypher_target_node(StringInfo str, const ExtensibleNode *node);

// set/remove private data structures
void out_cypher_update_information(StringInfo str, const ExtensibleNode *node);
void out_cypher_update_item(StringInfo str, const ExtensibleNode *node);

// delete private data structures
void out_cypher_delete_information(StringInfo str, const ExtensibleNode *node);
void out_cypher_delete_item(StringInfo str, const ExtensibleNode *node);

// merge private data structures
void out_cypher_merge_information(StringInfo str, const ExtensibleNode *node);

#endif
