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

#ifndef AG_CYPHER_UTILS_H
#define AG_CYPHER_UTILS_H

#include "access/heapam.h"

#include "nodes/cypher_nodes.h"
#include "utils/agtype.h"

/* declaration of a useful postgres macro that isn't in a header file */
#define DatumGetItemPointer(X)	 ((ItemPointer) DatumGetPointer(X))
#define ItemPointerGetDatum(X)	 PointerGetDatum(X)

/*
 * When executing the children of the CREATE, SET, REMOVE, and
 * DELETE clauses, we need to alter the command id in the estate
 * and the snapshot. That way we can hide the modified tuples from
 * the sub clauses that should not know what their parent clauses are
 * doing.
 */
#define Increment_Estate_CommandId(estate) \
    estate->es_output_cid++; \
    estate->es_snapshot->curcid++;

#define Decrement_Estate_CommandId(estate) \
    estate->es_output_cid--; \
    estate->es_snapshot->curcid--;

#define DELETE_VERTEX_HTAB_NAME "delete_vertex_htab"
#define DELETE_VERTEX_HTAB_SIZE 1000000

typedef struct cypher_create_custom_scan_state
{
    CustomScanState css;
    CustomScan *cs;
    List *pattern;
    List *path_values;
    uint32 flags;
    TupleTableSlot *slot;
    Oid graph_oid;
} cypher_create_custom_scan_state;

typedef struct cypher_set_custom_scan_state
{
    CustomScanState css;
    CustomScan *cs;
    cypher_update_information *set_list;
    int flags;
} cypher_set_custom_scan_state;

typedef struct cypher_delete_custom_scan_state
{
    CustomScanState css;
    CustomScan *cs;
    cypher_delete_information *delete_data;
    int flags;
    List *edge_labels;

    /*
     * Deleted vertex IDs are stored in this hashtable.
     *
     * When a vertex item is deleted, it must be checked if there is any edges
     * connected to it. The connected edges are either deleted or an error is
     * thrown depending on the DETACH option. However, the check for connected
     * edges is not done immediately. Instead the deleted vertex IDs are stored
     * in the hashtable. Once all vertices are deleted, this hashtable is used
     * to process the connected edges with only one scan of the edge tables.
     *
     * Note on performance: Additional performance gain may be possible if
     * the standard DELETE .. USING .. command can be used instead of this
     * hashtable. Because Postgres may create a better plan to execute that
     * command depending on the statistics and available indexes on start_id
     * and end_id column.
     */
    HTAB *vertex_id_htab;
} cypher_delete_custom_scan_state;

typedef struct cypher_merge_custom_scan_state
{
    CustomScanState css;
    CustomScan *cs;
    cypher_merge_information *merge_information;
    int flags;
    cypher_create_path *path;
    List *path_values;
    Oid graph_oid;
    AttrNumber merge_function_attr;
    bool created_new_path;
    bool found_a_path;
    CommandId base_currentCommandId;
    struct created_path *created_paths_list;
} cypher_merge_custom_scan_state;

TupleTableSlot *populate_vertex_tts(TupleTableSlot *elemTupleSlot,
                                    agtype_value *id, agtype_value *properties);
TupleTableSlot *populate_edge_tts(
    TupleTableSlot *elemTupleSlot, agtype_value *id, agtype_value *startid,
    agtype_value *endid, agtype_value *properties);

ResultRelInfo *create_entity_result_rel_info(EState *estate, char *graph_name,
                                             char *label_name);
void destroy_entity_result_rel_info(ResultRelInfo *result_rel_info);

bool entity_exists(EState *estate, Oid graph_oid, graphid id);
HeapTuple insert_entity_tuple(ResultRelInfo *resultRelInfo,
                              TupleTableSlot *elemTupleSlot,
                              EState *estate);
HeapTuple insert_entity_tuple_cid(ResultRelInfo *resultRelInfo,
                                  TupleTableSlot *elemTupleSlot,
                                  EState *estate, CommandId cid);

#endif
