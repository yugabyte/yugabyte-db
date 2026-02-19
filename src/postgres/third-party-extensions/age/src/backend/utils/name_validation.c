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

#include "catalog/pg_collation_d.h"
#include "utils/builtins.h"

#include "utils/name_validation.h"

static int regex_match(const char *string, const char *pattern);

/*
 * Returns whether the graph name is valid.
 *
 * @param graph_name name of the graph
 * @return int
 */
int is_valid_graph_name(const char *graph_name)
{
    int len = strlen(graph_name);

    if (len < MIN_GRAPH_NAME_LEN || len > MAX_GRAPH_NAME_LEN)
    {
        return 0;
    }

    return regex_match(graph_name, REGEX_GRAPH_NAME);
}

/*
 * Returns whether the label name is valid.
 *
 * Note: label_type parameter is not used in this implementation.
 * It should be used if validation algorithm for edge and vertex
 * differs in future.
 *
 * @param label_name name of the label
 * @param label_type label type defined in label_commands.h
 * @return int
 */
int is_valid_label_name(char *label_name, char label_type)
{
    int len = strlen(label_name);

    if (len < MIN_LABEL_NAME_LEN || len > MAX_LABEL_NAME_LEN)
    {
        ereport(WARNING,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("label name length not in range (%d <= length <= %d) length = %d",
                        MIN_LABEL_NAME_LEN, MAX_LABEL_NAME_LEN, len)));

        return 0;
    }

    return regex_match(label_name, REGEX_LABEL_NAME);
}

/*
 * Returns whether there is a regex match.
 *
 * @param string source string
 * @param pattern regex pattern
 * @return int match returns non-zero
 */
static int regex_match(const char *string, const char *pattern)
{
    text *t_string = NULL;
    text *t_pattern = NULL;
    Datum result;

    t_string = cstring_to_text_with_len(string, strlen(string));
    t_pattern = cstring_to_text_with_len(pattern, strlen(pattern));

    result = (DirectFunctionCall2Coll(textregexeq, C_COLLATION_OID,
                                      PointerGetDatum(t_string),
                                      PointerGetDatum(t_pattern)));

    return DatumGetBool(result);
}
