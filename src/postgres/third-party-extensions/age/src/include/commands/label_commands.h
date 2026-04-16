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

#ifndef AG_LABEL_COMMANDS_H
#define AG_LABEL_COMMANDS_H

#define LABEL_TYPE_VERTEX 'v'
#define LABEL_TYPE_EDGE 'e'

#define AG_DEFAULT_LABEL_EDGE "_ag_label_edge"
#define AG_DEFAULT_LABEL_VERTEX "_ag_label_vertex"

#define AG_VERTEX_COLNAME_ID "id"
#define AG_VERTEX_COLNAME_PROPERTIES "properties"

#define AG_ACCESS_FUNCTION_ID "age_id"

#define AG_VERTEX_ACCESS_FUNCTION_ID "age_id"
#define AG_VERTEX_ACCESS_FUNCTION_PROPERTIES "age_properties"

#define AG_EDGE_COLNAME_ID "id"
#define AG_EDGE_COLNAME_START_ID "start_id"
#define AG_EDGE_COLNAME_END_ID "end_id"
#define AG_EDGE_COLNAME_PROPERTIES "properties"

#define AG_EDGE_ACCESS_FUNCTION_ID "age_id"
#define AG_EDGE_ACCESS_FUNCTION_START_ID "age_start_id"
#define AG_EDGE_ACCESS_FUNCTION_END_ID "age_end_id"
#define AG_EDGE_ACCESS_FUNCTION_PROPERTIES "age_properties"

#define IS_DEFAULT_LABEL_EDGE(str) \
    (str != NULL && strcmp(AG_DEFAULT_LABEL_EDGE, str) == 0)
#define IS_DEFAULT_LABEL_VERTEX(str) \
    (str != NULL && strcmp(AG_DEFAULT_LABEL_VERTEX, str) == 0)

#define IS_AG_DEFAULT_LABEL(x) \
    (IS_DEFAULT_LABEL_EDGE(x) || IS_DEFAULT_LABEL_VERTEX(x))

void create_label(char *graph_name, char *label_name, char label_type,
                  List *parents);

Datum create_vlabel(PG_FUNCTION_ARGS);

Datum create_elabel(PG_FUNCTION_ARGS);

#endif
