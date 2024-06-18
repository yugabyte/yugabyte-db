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
#include "utils/json.h"
#include "utils/jsonfuncs.h"
#include "common/jsonapi.h"

#include "utils/load/ag_load_edges.h"
#include "utils/load/ag_load_labels.h"
#include "utils/load/age_load.h"

static agtype_value *csv_value_to_agtype_value(char *csv_val);
static bool json_validate(text *json);

agtype *create_empty_agtype(void)
{
    agtype* out;
    agtype_in_state result;

    memset(&result, 0, sizeof(agtype_in_state));

    result.res = push_agtype_value(&result.parse_state, WAGT_BEGIN_OBJECT,
                                   NULL);
    result.res = push_agtype_value(&result.parse_state, WAGT_END_OBJECT, NULL);

    out = agtype_value_to_agtype(result.res);
    pfree_agtype_in_state(&result);

    return out;
}

/*
 * Validate JSON text.
 *
 * Note: this function is borrowed from PG16. It is simplified
 * by removing two parameters as they are not used in age.
 */
static bool json_validate(text *json)
{
    JsonLexContext *lex = makeJsonLexContext(json, false);
    JsonParseErrorType result;

    result = pg_parse_json(lex, &nullSemAction);

    return result == JSON_SUCCESS;
}

/*
 * Converts the given csv value to an agtype_value.
 *
 * If csv_val is not a valid json, it is wrapped by double-quotes to make it a
 * string value. Because agtype is jsonb-like, the token should be a valid
 * json in order to be parsed into an agtype_value of appropriate type.
 * Finally, agtype_value_from_cstring() is called for parsing.
 */
static agtype_value *csv_value_to_agtype_value(char *csv_val)
{
    char *new_csv_val;
    agtype_value *res;

    if (!json_validate(cstring_to_text(csv_val)))
    {
        /* wrap the string with double-quote */
        int oldlen;
        int newlen;

        oldlen = strlen(csv_val);
        newlen = oldlen + 2; /* +2 for double-quotes */
        new_csv_val = (char *)palloc(sizeof(char) * (newlen + 1));

        new_csv_val[0] = '"';
        strncpy(&new_csv_val[1], csv_val, oldlen);
        new_csv_val[oldlen + 1] = '"';
        new_csv_val[oldlen + 2] = '\0';
    }
    else
    {
        new_csv_val = csv_val;
    }

    res = agtype_value_from_cstring(new_csv_val, strlen(new_csv_val));

    /* extract from top-level row scalar array */
    if (res->type == AGTV_ARRAY && res->val.array.raw_scalar)
    {
        res = &res->val.array.elems[0];
    }

    return res;
}

agtype *create_agtype_from_list(char **header, char **fields, size_t fields_len,
                                int64 vertex_id, bool load_as_agtype)
{
    agtype* out;
    agtype_value* key_agtype;
    agtype_value* value_agtype;
    agtype_in_state result;
    int i;

    memset(&result, 0, sizeof(agtype_in_state));

    result.res = push_agtype_value(&result.parse_state, WAGT_BEGIN_OBJECT,
                                   NULL);

    key_agtype = string_to_agtype_value("__id__");
    result.res = push_agtype_value(&result.parse_state,
                                   WAGT_KEY,
                                   key_agtype);

    value_agtype = integer_to_agtype_value(vertex_id);
    result.res = push_agtype_value(&result.parse_state,
                                   WAGT_VALUE,
                                   value_agtype);

    pfree_agtype_value(key_agtype);
    pfree_agtype_value(value_agtype);

    for (i = 0; i<fields_len; i++)
    {
        key_agtype = string_to_agtype_value(header[i]);
        result.res = push_agtype_value(&result.parse_state,
                                       WAGT_KEY,
                                       key_agtype);

        if (load_as_agtype)
        {
            value_agtype = csv_value_to_agtype_value(fields[i]);
        }
        else
        {
            value_agtype = string_to_agtype_value(fields[i]);
        }

        result.res = push_agtype_value(&result.parse_state,
                                       WAGT_VALUE,
                                       value_agtype);

        pfree_agtype_value(key_agtype);
        pfree_agtype_value(value_agtype);
    }

    result.res = push_agtype_value(&result.parse_state,
                                   WAGT_END_OBJECT, NULL);

    out = agtype_value_to_agtype(result.res);
    pfree_agtype_in_state(&result);

    return out;
}

agtype* create_agtype_from_list_i(char **header, char **fields,
                                  size_t fields_len, size_t start_index,
                                  bool load_as_agtype)
{
    agtype* out;
    agtype_value* key_agtype;
    agtype_value* value_agtype;
    agtype_in_state result;
    size_t i;

    if (start_index + 1 == fields_len)
    {
        return create_empty_agtype();
    }

    memset(&result, 0, sizeof(agtype_in_state));

    result.res = push_agtype_value(&result.parse_state, WAGT_BEGIN_OBJECT,
                                   NULL);

    for (i = start_index; i < fields_len; i++)
    {
        key_agtype = string_to_agtype_value(header[i]);
        result.res = push_agtype_value(&result.parse_state,
                                       WAGT_KEY,
                                       key_agtype);

        if (load_as_agtype)
        {
            value_agtype = csv_value_to_agtype_value(fields[i]);
        }
        else
        {
            value_agtype = string_to_agtype_value(fields[i]);
        }

        result.res = push_agtype_value(&result.parse_state,
                                       WAGT_VALUE,
                                       value_agtype);

        pfree_agtype_value(key_agtype);
        pfree_agtype_value(value_agtype);
    }

    result.res = push_agtype_value(&result.parse_state,
                                   WAGT_END_OBJECT, NULL);

    out = agtype_value_to_agtype(result.res);
    pfree_agtype_in_state(&result);

    return out;
}

void insert_edge_simple(Oid graph_oid, char *label_name, graphid edge_id,
                        graphid start_id, graphid end_id,
                        agtype *edge_properties)
{

    Datum values[6];
    bool nulls[4] = {false, false, false, false};
    Relation label_relation;
    HeapTuple tuple;

    /* Check if label provided exists as vertex label, then throw error */
    if (get_label_kind(label_name, graph_oid) == LABEL_KIND_VERTEX)
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("label %s already exists as vertex label", label_name)));
    }

    values[0] = GRAPHID_GET_DATUM(edge_id);
    values[1] = GRAPHID_GET_DATUM(start_id);
    values[2] = GRAPHID_GET_DATUM(end_id);
    values[3] = AGTYPE_P_GET_DATUM((edge_properties));

    label_relation = table_open(get_label_relation(label_name, graph_oid),
                                RowExclusiveLock);

    tuple = heap_form_tuple(RelationGetDescr(label_relation),
                            values, nulls);
    heap_insert(label_relation, tuple,
                GetCurrentCommandId(true), 0, NULL);
    table_close(label_relation, RowExclusiveLock);
    CommandCounterIncrement();
}

void insert_vertex_simple(Oid graph_oid, char *label_name, graphid vertex_id,
                          agtype *vertex_properties)
{

    Datum values[2];
    bool nulls[2] = {false, false};
    Relation label_relation;
    HeapTuple tuple;

    /* Check if label provided exists as edge label, then throw error */
    if (get_label_kind(label_name, graph_oid) == LABEL_KIND_EDGE)
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("label %s already exists as edge label", label_name)));
    }

    values[0] = GRAPHID_GET_DATUM(vertex_id);
    values[1] = AGTYPE_P_GET_DATUM((vertex_properties));

    label_relation = table_open(get_label_relation(label_name, graph_oid),
                                RowExclusiveLock);
    tuple = heap_form_tuple(RelationGetDescr(label_relation),
                            values, nulls);
    heap_insert(label_relation, tuple,
                GetCurrentCommandId(true), 0, NULL);
    table_close(label_relation, RowExclusiveLock);
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
    Oid graph_oid;
    int32 label_id;
    bool id_field_exists;
    bool load_as_agtype;

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
    load_as_agtype = PG_GETARG_BOOL(4);


    graph_name_str = NameStr(*graph_name);
    label_name_str = NameStr(*label_name);
    file_path_str = text_to_cstring(file_path);

    graph_oid = get_graph_oid(graph_name_str);
    label_id = get_label_id(label_name_str, graph_oid);

    create_labels_from_csv_file(file_path_str, graph_name_str, graph_oid,
                                label_name_str, label_id, id_field_exists,
                                load_as_agtype);
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
    Oid graph_oid;
    int32 label_id;
    bool load_as_agtype;

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
    load_as_agtype = PG_GETARG_BOOL(3);

    graph_name_str = NameStr(*graph_name);
    label_name_str = NameStr(*label_name);
    file_path_str = text_to_cstring(file_path);

    graph_oid = get_graph_oid(graph_name_str);
    label_id = get_label_id(label_name_str, graph_oid);

    create_edges_from_csv_file(file_path_str, graph_name_str, graph_oid,
                               label_name_str, label_id, load_as_agtype);
    PG_RETURN_VOID();

}
