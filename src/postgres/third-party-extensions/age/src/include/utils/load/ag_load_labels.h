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


#ifndef AG_LOAD_LABELS_H
#define AG_LOAD_LABELS_H

#include "access/heapam.h"
#include "utils/load/age_load.h"

#define AGE_VERTIX 1
#define AGE_EDGE 2


struct counts {
    long unsigned fields;
    long unsigned allvalues;
    long unsigned rows;
};

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
    Oid temp_table_relid;
    bool id_field_exists;
    bool load_as_agtype;
    int curr_seq_num;
    batch_insert_state *batch_state;
} csv_vertex_reader;


void vertex_field_cb(void *field, size_t field_len, void *data);
void vertex_row_cb(int delim __attribute__((unused)), void *data);

int create_labels_from_csv_file(char *file_path, char *graph_name, Oid graph_oid,
                                char *label_name, int label_id,
                                bool id_field_exists, bool load_as_agtype);

#endif /* AG_LOAD_LABELS_H */
