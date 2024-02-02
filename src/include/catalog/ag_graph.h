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

#ifndef AG_AG_GRAPH_H
#define AG_AG_GRAPH_H

#include "catalog/ag_catalog.h"

#define Anum_ag_graph_oid 1
#define Anum_ag_graph_name 2
#define Anum_ag_graph_namespace 3

#define Natts_ag_graph 3

#define ag_graph_relation_id() ag_relation_id("ag_graph", "table")
#define ag_graph_name_index_id() ag_relation_id("ag_graph_name_index", "index")
#define ag_graph_namespace_index_id() \
    ag_relation_id("ag_graph_namespace_index", "index")

void insert_graph(const Name graph_name, const Oid nsp_id);
void delete_graph(const Name graph_name);
void update_graph_name(const Name graph_name, const Name new_name);

uint32 get_graph_oid(const char *graph_name);
char *get_graph_namespace_name(const char *graph_name);

List *get_graphnames(void);
void drop_graphs(List *graphnames);

#define graph_exists(graph_name) OidIsValid(get_graph_oid(graph_name))

#endif
