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

#include "access/heapam.h"
#include "utils/load/age_load.h"

#ifndef AG_LOAD_EDGES_H
#define AG_LOAD_EDGES_H

typedef struct {
    size_t row;
    char **header;
    size_t *header_len;
    size_t header_num;
    char **fields;
    size_t *fields_len;
    size_t alloc;
    size_t cur_field;
    int error;
    size_t header_row_length;
    size_t curr_row_length;
    char *graph_name;
    Oid graph_oid;
    char *label_name;
    int label_id;
    Oid label_seq_relid;
    char *start_vertex;
    char *end_vertex;
    bool load_as_agtype;
    batch_insert_state *batch_state;
} csv_edge_reader;


void edge_field_cb(void *field, size_t field_len, void *data);
void edge_row_cb(int delim __attribute__((unused)), void *data);

int create_edges_from_csv_file(char *file_path, char *graph_name, Oid graph_oid,
                                char *label_name, int label_id,
                                bool load_as_agtype);

#endif /*AG_LOAD_EDGES_H */

