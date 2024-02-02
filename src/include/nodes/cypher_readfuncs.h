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

#ifndef AG_CYPHER_READFUNCS_H
#define AG_CYPHER_READFUNCS_H

#include "nodes/extensible.h"

/*
 * Deserialization functions for AGE's ExtensibleNodes. We assign
 * each node to its deserialization function in the DEFINE_NODE_METHODS
 * and DEFINE_NODE_METHODS_EXTENDED macros in ag_nodes.c.

 *
 * All functions are dependent on the pg_strtok function. We do not
 * setup pg_strtok. That is for the caller to do. By default that
 * is the responsibility of Postgres' nodeRead function. We assume
 * that was setup correctly.
 */

void read_ag_node(ExtensibleNode *node);

// create data structures
void read_cypher_create_target_nodes(struct ExtensibleNode *node);
void read_cypher_create_path(struct ExtensibleNode *node);
void read_cypher_target_node(struct ExtensibleNode *node);

// set/remove data structures
void read_cypher_update_information(struct ExtensibleNode *node);
void read_cypher_update_item(struct ExtensibleNode *node);

// delete data structures
void read_cypher_delete_information(struct ExtensibleNode *node);
void read_cypher_delete_item(struct ExtensibleNode *node);

void read_cypher_merge_information(struct ExtensibleNode *node);

#endif
