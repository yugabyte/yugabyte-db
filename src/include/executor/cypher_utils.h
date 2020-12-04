/*
 * Copyright 2020 Bitnine Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AG_CYPHER_UTILS_H
#define AG_CYPHER_UTILS_H

#include "nodes/extensible.h"
#include "nodes/nodes.h"
#include "nodes/plannodes.h"

#include "nodes/cypher_nodes.h"
#include "utils/agtype.h"

// declaration of a useful postgres macro that isn't in a header file
#define DatumGetItemPointer(X)	 ((ItemPointer) DatumGetPointer(X))
#define ItemPointerGetDatum(X)	 PointerGetDatum(X)

/*
 * This holds information in clauses that create or alter tuples on
 * disc, this is so future clause can manipulate those tuples if
 * necessary.
 */
typedef struct clause_tuple_information {
    char *name;
    HeapTuple tuple;
} clause_tuple_information;

typedef struct cypher_create_custom_scan_state
{
    CustomScanState css;
    CustomScan *cs;
    List *pattern;
    List *path_values;
    List *tuple_info;
    uint32 flags;
    TupleTableSlot *slot;
} cypher_create_custom_scan_state;

typedef struct cypher_set_custom_scan_state
{
    CustomScanState css;
    CustomScan *cs;
    cypher_update_information *set_list;
    List *tuple_info;
    int flags;
} cypher_set_custom_scan_state;

PlanState *find_plan_state(CustomScanState *node, char *var_name);

TupleTableSlot *populate_vertex_tts(TupleTableSlot *elemTupleSlot, agtype_value *id, agtype_value *properties);
TupleTableSlot *populate_edge_tts(
    TupleTableSlot *elemTupleSlot, agtype_value *id, agtype_value *startid,
    agtype_value *endid, agtype_value *properties);

ResultRelInfo *create_entity_result_rel_info(CustomScanState *node, char *graph_name, char *label_name);
ItemPointer get_self_item_pointer(TupleTableSlot *tts);
HeapTuple get_heap_tuple(CustomScanState *node, char *var_name);

#endif
