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

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup.h"
#include "access/htup_details.h"
#include "access/skey.h"
#include "access/stratnum.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "fmgr.h"
#include "nodes/execnodes.h"
#include "nodes/makefuncs.h"
#include "storage/lockdefs.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/relcache.h"

#include "catalog/ag_graph.h"
#include "catalog/ag_label.h"
#include "commands/label_commands.h"
#include "executor/cypher_utils.h"
#include "utils/ag_cache.h"
#include "utils/graphid.h"

/*
 * INSERT INTO ag_catalog.ag_label
 * VALUES (label_name, label_graph, label_id, label_kind,
 *         label_relation, seq_name)
 */
void insert_label(const char *label_name, Oid graph_oid, int32 label_id,
                  char label_kind, Oid label_relation, const char *seq_name)
{
    NameData label_name_data;
    NameData seq_name_data;
    Datum values[Natts_ag_label];
    bool nulls[Natts_ag_label];
    Relation ag_label;
    HeapTuple tuple;

    /*
     * NOTE: Is it better to make use of label_id and label_kind domain types
     *       than to use assert to check label_id and label_kind are valid?
     */
    AssertArg(label_name);
    AssertArg(label_id_is_valid(label_id));
    AssertArg(label_kind == LABEL_KIND_VERTEX ||
              label_kind == LABEL_KIND_EDGE);
    AssertArg(OidIsValid(label_relation));
    AssertArg(seq_name);

    ag_label = table_open(ag_label_relation_id(), RowExclusiveLock);

    namestrcpy(&label_name_data, label_name);
    values[Anum_ag_label_name - 1] = NameGetDatum(&label_name_data);
    nulls[Anum_ag_label_name - 1] = false;

    values[Anum_ag_label_graph - 1] = ObjectIdGetDatum(graph_oid);
    nulls[Anum_ag_label_graph - 1] = false;

    values[Anum_ag_label_id - 1] = Int32GetDatum(label_id);
    nulls[Anum_ag_label_id - 1] = false;

    values[Anum_ag_label_kind - 1] = CharGetDatum(label_kind);
    nulls[Anum_ag_label_kind - 1] = false;

    values[Anum_ag_label_relation - 1] = ObjectIdGetDatum(label_relation);
    nulls[Anum_ag_label_relation - 1] = false;

    namestrcpy(&seq_name_data, seq_name);
    values[Anum_ag_label_seq_name - 1] = NameGetDatum(&seq_name_data);
    nulls[Anum_ag_label_seq_name - 1] = false;

    tuple = heap_form_tuple(RelationGetDescr(ag_label), values, nulls);

    /*
     * CatalogTupleInsert() is originally for PostgreSQL's catalog. However,
     * it is used at here for convenience.
     */
    CatalogTupleInsert(ag_label, tuple);

    table_close(ag_label, RowExclusiveLock);
}

/* DELETE FROM ag_catalog.ag_label WHERE relation = relation */
void delete_label(Oid relation)
{
    ScanKeyData scan_keys[1];
    Relation ag_label;
    SysScanDesc scan_desc;
    HeapTuple tuple;

    ScanKeyInit(&scan_keys[0], Anum_ag_label_relation, BTEqualStrategyNumber,
                F_OIDEQ, ObjectIdGetDatum(relation));

    ag_label = table_open(ag_label_relation_id(), RowExclusiveLock);
    scan_desc = systable_beginscan(ag_label, ag_label_relation_index_id(),
                                   true, NULL, 1, scan_keys);

    tuple = systable_getnext(scan_desc);
    if (!HeapTupleIsValid(tuple))
    {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_TABLE),
                 errmsg("label (relation=%u) does not exist", relation)));
    }

    CatalogTupleDelete(ag_label, &tuple->t_self);

    systable_endscan(scan_desc);
    table_close(ag_label, RowExclusiveLock);
}

int32 get_label_id(const char *label_name, Oid graph_oid)
{
    label_cache_data *cache_data;

    cache_data = search_label_name_graph_cache(label_name, graph_oid);
    if (cache_data)
        return cache_data->id;
    else
        return INVALID_LABEL_ID;
}

Oid get_label_relation(const char *label_name, Oid graph_oid)
{
    label_cache_data *cache_data;

    cache_data = search_label_name_graph_cache(label_name, graph_oid);
    if (cache_data)
        return cache_data->relation;
    else
        return InvalidOid;
}

char *get_label_relation_name(const char *label_name, Oid graph_oid)
{
    return get_rel_name(get_label_relation(label_name, graph_oid));
}

char get_label_kind(const char *label_name, Oid label_graph)
{
    label_cache_data *cache_data;

    cache_data = search_label_name_graph_cache(label_name, label_graph);
    if (cache_data)
    {
        return cache_data->kind;
    }
    else
    {
        return INVALID_LABEL_ID;
    }
}

char *get_label_seq_relation_name(const char *label_name)
{
    return psprintf("%s_id_seq", label_name);
}

PG_FUNCTION_INFO_V1(_label_name);

/*
 * Using the graph name and the vertex/edge's graphid, find
 * the correct label name from ag_catalog.label
 */
Datum _label_name(PG_FUNCTION_ARGS)
{
    char *label_name;
    label_cache_data *label_cache;
    Oid graph;
    uint32 label_id;

    if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
        ereport(ERROR, (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                        errmsg("graph_oid and label_id must not be null")));

    graph = PG_GETARG_OID(0);

    label_id = (int32)(((uint64)AG_GETARG_GRAPHID(1)) >> ENTRY_ID_BITS);

    label_cache = search_label_graph_oid_cache(graph, label_id);

    label_name = NameStr(label_cache->name);

    if (IS_AG_DEFAULT_LABEL(label_name))
        PG_RETURN_CSTRING("");

    PG_RETURN_CSTRING(label_name);
}

PG_FUNCTION_INFO_V1(_label_id);

Datum _label_id(PG_FUNCTION_ARGS)
{
    Name graph_name;
    Name label_name;
    Oid graph;
    int32 id;

    if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
    {
        ereport(ERROR, (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                        errmsg("graph_name and label_name must not be null")));
    }
    graph_name = PG_GETARG_NAME(0);
    label_name = PG_GETARG_NAME(1);

    graph = get_graph_oid(NameStr(*graph_name));
    id = get_label_id(NameStr(*label_name), graph);

    PG_RETURN_INT32(id);
}

PG_FUNCTION_INFO_V1(_extract_label_id);

Datum _extract_label_id(PG_FUNCTION_ARGS)
{
    graphid graph_oid;

    if (PG_ARGISNULL(0))
    {
        ereport(ERROR, (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                        errmsg("graph_oid must not be null")));
    }
    graph_oid = AG_GETARG_GRAPHID(0);

    PG_RETURN_INT32(get_graphid_label_id(graph_oid));
}

bool label_id_exists(Oid graph_oid, int32 label_id)
{
    label_cache_data *cache_data;

    cache_data = search_label_graph_oid_cache(graph_oid, label_id);
    if (cache_data)
        return true;
    else
        return false;
}

/*
 * Creates A RangeVar for the given label.
 */
RangeVar *get_label_range_var(char *graph_name, Oid graph_oid,
                              char *label_name)
{
    char *relname;
    label_cache_data *label_cache;

    label_cache = search_label_name_graph_cache(label_name, graph_oid);

    relname = get_rel_name(label_cache->relation);

    return makeRangeVar(graph_name, relname, 2);
}

/*
 * Retrieves a list of all the names of a graph.
 *
 * XXX: We may want to use the cache system for this function,
 * however the cache system currently requires us to know the
 * name of the label we want.
 */
List *get_all_edge_labels_per_graph(EState *estate, Oid graph_oid)
{
    List *labels = NIL;
    ScanKeyData scan_keys[2];
    Relation ag_label;
    TableScanDesc scan_desc;
    HeapTuple tuple;
    TupleTableSlot *slot;
    ResultRelInfo *resultRelInfo;

    /* setup scan keys to get all edges for the given graph oid */
    ScanKeyInit(&scan_keys[1], Anum_ag_label_graph, BTEqualStrategyNumber,
                F_OIDEQ, ObjectIdGetDatum(graph_oid));
    ScanKeyInit(&scan_keys[0], Anum_ag_label_kind, BTEqualStrategyNumber,
                F_CHAREQ, CharGetDatum(LABEL_TYPE_EDGE));

    /* setup the table to be scanned */
    ag_label = table_open(ag_label_relation_id(), RowExclusiveLock);
    scan_desc = table_beginscan(ag_label, estate->es_snapshot, 2, scan_keys);

    resultRelInfo = create_entity_result_rel_info(estate, "ag_catalog",
                                                  "ag_label");

    slot = ExecInitExtraTupleSlot(
        estate, RelationGetDescr(resultRelInfo->ri_RelationDesc),
        &TTSOpsHeapTuple);

    /* scan through the results and get all the label names. */
    while(true)
    {
        Name label;
        bool isNull;
        Datum datum;

        tuple = heap_getnext(scan_desc, ForwardScanDirection);

        /* no more labels to process */
        if (!HeapTupleIsValid(tuple))
            break;

        ExecStoreHeapTuple(tuple, slot, false);

        datum = slot_getattr(slot, Anum_ag_label_name, &isNull);
        label = DatumGetName(datum);

        labels = lappend(labels, label);
    }

    table_endscan(scan_desc);

    destroy_entity_result_rel_info(resultRelInfo);
    table_close(resultRelInfo->ri_RelationDesc, RowExclusiveLock);

    return labels;
}
