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

#ifndef AG_CYPHER_COPYFUNCS_H
#define AG_CYPHER_COPYFUNCS_H

/*
 * Functions that let AGE's ExtensibleNodes be compatible with
 * Postgres' copyObject. We assign each node to its copy function
 * in the DEFINE_NODE_METHODS and DEFINE_NODE_METHODS_EXTENDED
 * macros in ag_nodes.c
 */

void copy_ag_node(ExtensibleNode *newnode, const ExtensibleNode *oldnode);

// create data structures
void copy_cypher_create_target_nodes(ExtensibleNode *newnode,
                                     const ExtensibleNode *from);
void copy_cypher_create_path(ExtensibleNode *newnode,
                             const ExtensibleNode *from);
void copy_cypher_target_node(ExtensibleNode *newnode,
                             const ExtensibleNode *from);

// set/remove data structures
void copy_cypher_update_information(ExtensibleNode *newnode,
                                    const ExtensibleNode *from);
void copy_cypher_update_item(ExtensibleNode *newnode,
                             const ExtensibleNode *from);

// delete data structures
void copy_cypher_delete_information(ExtensibleNode *newnode,
                                    const ExtensibleNode *from);
void copy_cypher_delete_item(ExtensibleNode *newnode,
                             const ExtensibleNode *from);

// merge data structure
void copy_cypher_merge_information(ExtensibleNode *newnode,
                                   const ExtensibleNode *from);
#endif
