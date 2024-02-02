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

#ifndef AG_AG_LABEL_H
#define AG_AG_LABEL_H

#include "nodes/execnodes.h"

#define Anum_ag_label_vertex_table_id 1
#define Anum_ag_label_vertex_table_properties 2

#define Anum_ag_label_edge_table_id 1
#define Anum_ag_label_edge_table_start_id 2
#define Anum_ag_label_edge_table_end_id 3
#define Anum_ag_label_edge_table_properties 4

#define vertex_tuple_id Anum_ag_label_vertex_table_id - 1
#define vertex_tuple_properties Anum_ag_label_vertex_table_properties - 1

#define edge_tuple_id Anum_ag_label_edge_table_id - 1
#define edge_tuple_start_id Anum_ag_label_edge_table_start_id - 1
#define edge_tuple_end_id Anum_ag_label_edge_table_end_id - 1
#define edge_tuple_properties Anum_ag_label_edge_table_properties - 1



#define Anum_ag_label_name 1
#define Anum_ag_label_graph 2
#define Anum_ag_label_id 3
#define Anum_ag_label_kind 4
#define Anum_ag_label_relation 5
#define Anum_ag_label_seq_name 6


#define Natts_ag_label 6

#define ag_label_relation_id() ag_relation_id("ag_label", "table")
#define ag_label_name_graph_index_id() \
    ag_relation_id("ag_label_name_graph_index", "index")
#define ag_label_graph_oid_index_id() \
    ag_relation_id("ag_label_graph_oid_index", "index")
#define ag_label_relation_index_id() \
    ag_relation_id("ag_label_relation_index", "index")
#define ag_label_seq_name_graph_index_id() \
    ag_relation_id("ag_label_seq_name_graph_index", "index")

#define LABEL_ID_SEQ_NAME "_label_id_seq"

#define LABEL_KIND_VERTEX 'v'
#define LABEL_KIND_EDGE 'e'

void insert_label(const char *label_name, Oid graph_oid, int32 label_id,
                  char label_kind, Oid label_relation, const char *seq_name);
void delete_label(Oid relation);

int32 get_label_id(const char *label_name, Oid graph_oid);
Oid get_label_relation(const char *label_name, Oid graph_oid);
char *get_label_relation_name(const char *label_name, Oid graph_oid);
Oid get_label_oid(const char *label_name, Oid label_graph);
char get_label_kind(const char *label_name, Oid label_graph);

bool label_id_exists(Oid graph_oid, int32 label_id);
RangeVar *get_label_range_var(char *graph_name, Oid graph_oid,
                              char *label_name);

List *get_all_edge_labels_per_graph(EState *estate, Oid graph_oid);

#define label_exists(label_name, label_graph) \
    OidIsValid(get_label_id(label_name, label_graph))

#endif
