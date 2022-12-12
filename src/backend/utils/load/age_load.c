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

#include "postgres.h"

#include "access/heapam.h"
#include "access/xact.h"
#include "catalog/dependency.h"
#include "catalog/namespace.h"
#include "catalog/objectaddress.h"
#include "catalog/pg_class_d.h"
#include "commands/defrem.h"
#include "commands/sequence.h"
#include "commands/tablecmds.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "nodes/plannodes.h"
#include "nodes/primnodes.h"
#include "nodes/value.h"
#include "parser/parse_node.h"
#include "parser/parser.h"
#include "storage/lockdefs.h"
#include "tcop/dest.h"
#include "tcop/utility.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"

#include "catalog/ag_graph.h"
#include "catalog/ag_label.h"
#include "commands/label_commands.h"
#include "utils/ag_cache.h"
#include "utils/agtype.h"
#include "utils/graphid.h"

#include "utils/load/age_load.h"
#include "utils/load/ag_load_labels.h"
#include "utils/load/ag_load_edges.h"

agtype* create_empty_agtype(void)
{
    agtype_in_state result;

    memset(&result, 0, sizeof(agtype_in_state));

    result.res = push_agtype_value(&result.parse_state,
                                   WAGT_BEGIN_OBJECT, NULL);
    result.res = push_agtype_value(&result.parse_state,
                                   WAGT_END_OBJECT, NULL);

    return agtype_value_to_agtype(result.res);
}

agtype* create_agtype_from_list(char **header, char **fields,
                                size_t fields_len, int64 vertex_id)
{
    agtype_in_state result;
    int i;

    memset(&result, 0, sizeof(agtype_in_state));

    result.res = push_agtype_value(&result.parse_state,
                                   WAGT_BEGIN_OBJECT, NULL);

    result.res = push_agtype_value(&result.parse_state,
                                   WAGT_KEY,
                                   string_to_agtype_value("__id__"));
    result.res = push_agtype_value(&result.parse_state,
                                   WAGT_VALUE,
                                   integer_to_agtype_value(vertex_id));

    for (i = 0; i<fields_len; i++)
    {
        result.res = push_agtype_value(&result.parse_state,
                                       WAGT_KEY,
                                       string_to_agtype_value(header[i]));
        result.res = push_agtype_value(&result.parse_state,
                                       WAGT_VALUE,
                                       string_to_agtype_value(fields[i]));
    }

    result.res = push_agtype_value(&result.parse_state,
                                   WAGT_END_OBJECT, NULL);

    return agtype_value_to_agtype(result.res);
}

agtype* create_agtype_from_list_i(char **header, char **fields,
                                  size_t fields_len, size_t start_index)
{

    agtype_in_state result;
    size_t i;

    if (start_index + 1 == fields_len)
    {
        return create_empty_agtype();
    }
    memset(&result, 0, sizeof(agtype_in_state));

    result.res = push_agtype_value(&result.parse_state,
                                   WAGT_BEGIN_OBJECT, NULL);

    for (i = start_index; i<fields_len; i++)
    {
        result.res = push_agtype_value(&result.parse_state,
                                       WAGT_KEY,
                                       string_to_agtype_value(header[i]));
        result.res = push_agtype_value(&result.parse_state,
                                       WAGT_VALUE,
                                       string_to_agtype_value(fields[i]));
    }

    result.res = push_agtype_value(&result.parse_state,
                                   WAGT_END_OBJECT, NULL);

    return agtype_value_to_agtype(result.res);
}

void insert_edge_simple(Oid graph_id, char* label_name, graphid edge_id,
                        graphid start_id, graphid end_id,
                        agtype* edge_properties)
{

    Datum values[6];
    bool nulls[4] = {false, false, false, false};
    Relation label_relation;
    HeapTuple tuple;


    values[0] = GRAPHID_GET_DATUM(edge_id);
    values[1] = GRAPHID_GET_DATUM(start_id);
    values[2] = GRAPHID_GET_DATUM(end_id);
    values[3] = AGTYPE_P_GET_DATUM((edge_properties));

    label_relation = heap_open(get_label_relation(label_name,
                                                  graph_id),
                               RowExclusiveLock);

    tuple = heap_form_tuple(RelationGetDescr(label_relation),
                            values, nulls);
    heap_insert(label_relation, tuple,
                GetCurrentCommandId(true), 0, NULL);
    heap_close(label_relation, RowExclusiveLock);
    CommandCounterIncrement();
}

void insert_vertex_simple(Oid graph_id, char* label_name,
                          graphid vertex_id,
                          agtype* vertex_properties)
{

    Datum values[2];
    bool nulls[2] = {false, false};
    Relation label_relation;
    HeapTuple tuple;

    values[0] = GRAPHID_GET_DATUM(vertex_id);
    values[1] = AGTYPE_P_GET_DATUM((vertex_properties));

    label_relation = heap_open(get_label_relation(label_name,
                                                  graph_id),
                               RowExclusiveLock);
    tuple = heap_form_tuple(RelationGetDescr(label_relation),
                            values, nulls);
    heap_insert(label_relation, tuple,
                GetCurrentCommandId(true), 0, NULL);
    heap_close(label_relation, RowExclusiveLock);
    CommandCounterIncrement();
}


PG_FUNCTION_INFO_V1(load_labels_from_file);
Datum load_labels_from_file(PG_FUNCTION_ARGS)
{

    Name graph_name;
    Name label_name;
    text* file_path;
    char* graph_name_str;
    char* label_name_str;
    char* file_path_str;
    Oid graph_id;
    int32 label_id;
    bool id_field_exists;

    if (PG_ARGISNULL(0))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("graph name must not be NULL")));
    }

    if (PG_ARGISNULL(1))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("label name must not be NULL")));
    }

    if (PG_ARGISNULL(2))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("file path must not be NULL")));
    }

    graph_name = PG_GETARG_NAME(0);
    label_name = PG_GETARG_NAME(1);
    file_path = PG_GETARG_TEXT_P(2);
    id_field_exists = PG_GETARG_BOOL(3);


    graph_name_str = NameStr(*graph_name);
    label_name_str = NameStr(*label_name);
    file_path_str = text_to_cstring(file_path);

    graph_id = get_graph_oid(graph_name_str);
    label_id = get_label_id(label_name_str, graph_id);

    create_labels_from_csv_file(file_path_str, graph_name_str,
                                graph_id, label_name_str,
                                label_id, id_field_exists);
    PG_RETURN_VOID();

}

PG_FUNCTION_INFO_V1(load_edges_from_file);
Datum load_edges_from_file(PG_FUNCTION_ARGS)
{

    Name graph_name;
    Name label_name;
    text* file_path;
    char* graph_name_str;
    char* label_name_str;
    char* file_path_str;
    Oid graph_id;
    int32 label_id;

    if (PG_ARGISNULL(0))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("graph name must not be NULL")));
    }

    if (PG_ARGISNULL(1))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("label name must not be NULL")));
    }

    if (PG_ARGISNULL(2))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("file path must not be NULL")));
    }

    graph_name = PG_GETARG_NAME(0);
    label_name = PG_GETARG_NAME(1);
    file_path = PG_GETARG_TEXT_P(2);

    graph_name_str = NameStr(*graph_name);
    label_name_str = NameStr(*label_name);
    file_path_str = text_to_cstring(file_path);

    graph_id = get_graph_oid(graph_name_str);
    label_id = get_label_id(label_name_str, graph_id);

    create_edges_from_csv_file(file_path_str, graph_name_str,
                               graph_id, label_name_str, label_id);
    PG_RETURN_VOID();

}