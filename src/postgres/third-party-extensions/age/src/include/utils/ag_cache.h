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

#ifndef AG_AG_CACHE_H
#define AG_AG_CACHE_H

/* graph_cache_data contains the same fields that ag_graph catalog table has */
typedef struct graph_cache_data
{
    Oid oid;
    NameData name;
    Oid namespace;
} graph_cache_data;

/* label_cache_data contains the same fields that ag_label catalog table has */
typedef struct label_cache_data
{
    NameData name;
    Oid graph;
    int32 id;
    char kind;
    Oid relation;
    NameData seq_name;
} label_cache_data;

/* callers of these functions must not modify the returned struct */
graph_cache_data *search_graph_name_cache(const char *name);
graph_cache_data *search_graph_namespace_cache(Oid namespace);
label_cache_data *search_label_oid_cache(Oid oid);
label_cache_data *search_label_name_graph_cache(const char *name, Oid graph);
label_cache_data *search_label_graph_oid_cache(Oid graph, int32 id);
label_cache_data *search_label_relation_cache(Oid relation);
label_cache_data *search_label_seq_name_graph_cache(const char *name, Oid graph);

#endif
