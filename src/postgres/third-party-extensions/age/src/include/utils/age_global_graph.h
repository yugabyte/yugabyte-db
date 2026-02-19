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

#ifndef AG_AGE_GLOBAL_GRAPH_H
#define AG_AGE_GLOBAL_GRAPH_H

#include "utils/age_graphid_ds.h"

/*
 * We declare the graph nodes and edges here, and in this way, so that it may be
 * used elsewhere. However, we keep the contents private by defining it in
 * age_global_graph.c
 */

/* vertex entry for the vertex_hashtable */
typedef struct vertex_entry vertex_entry;

/* edge entry for the edge_hashtable */
typedef struct edge_entry edge_entry;

typedef struct GRAPH_global_context GRAPH_global_context;

/* GRAPH global context functions */
GRAPH_global_context *manage_GRAPH_global_contexts(char *graph_name,
                                                   Oid graph_oid);
GRAPH_global_context *find_GRAPH_global_context(Oid graph_oid);
bool is_ggctx_invalid(GRAPH_global_context *ggctx);
/* GRAPH retrieval functions */
ListGraphId *get_graph_vertices(GRAPH_global_context *ggctx);
vertex_entry *get_vertex_entry(GRAPH_global_context *ggctx,
                               graphid vertex_id);
edge_entry *get_edge_entry(GRAPH_global_context *ggctx, graphid edge_id);
/* vertex entry accessor functions*/
graphid get_vertex_entry_id(vertex_entry *ve);
ListGraphId *get_vertex_entry_edges_in(vertex_entry *ve);
ListGraphId *get_vertex_entry_edges_out(vertex_entry *ve);
ListGraphId *get_vertex_entry_edges_self(vertex_entry *ve);
Oid get_vertex_entry_label_table_oid(vertex_entry *ve);
Datum get_vertex_entry_properties(vertex_entry *ve);
/* edge entry accessor functions */
graphid get_edge_entry_id(edge_entry *ee);
Oid get_edge_entry_label_table_oid(edge_entry *ee);
Datum get_edge_entry_properties(edge_entry *ee);
graphid get_edge_entry_start_vertex_id(edge_entry *ee);
graphid get_edge_entry_end_vertex_id(edge_entry *ee);
#endif
