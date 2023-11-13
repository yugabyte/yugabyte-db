/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
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
#include "catalog/namespace.h"
#include "common/hashfn.h"
#include "commands/label_commands.h"
#include "utils/datum.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/snapmgr.h"

#include "utils/age_global_graph.h"
#include "catalog/ag_graph.h"
#include "catalog/ag_label.h"

/* defines */
#define VERTEX_HTAB_NAME "Vertex to edge lists " /* added a space at end for */
#define EDGE_HTAB_NAME "Edge to vertex mapping " /* the graph name to follow */
#define VERTEX_HTAB_INITIAL_SIZE 1000000
#define EDGE_HTAB_INITIAL_SIZE 1000000

/* internal data structures implementation */

/* vertex entry for the vertex_hashtable */
typedef struct vertex_entry
{
    graphid vertex_id;             /* vertex id, it is also the hash key */
    ListGraphId *edges_in;         /* List of entering edges graphids (int64) */
    ListGraphId *edges_out;        /* List of exiting edges graphids (int64) */
    ListGraphId *edges_self;       /* List of selfloop edges graphids (int64) */
    Oid vertex_label_table_oid;    /* the label table oid */
    Datum vertex_properties;       /* datum property value */
} vertex_entry;

/* edge entry for the edge_hashtable */
typedef struct edge_entry
{
    graphid edge_id;               /* edge id, it is also the hash key */
    Oid edge_label_table_oid;      /* the label table oid */
    Datum edge_properties;         /* datum property value */
    graphid start_vertex_id;       /* start vertex */
    graphid end_vertex_id;         /* end vertex */
} edge_entry;

/*
 * GRAPH global context per graph. They are chained together via next.
 * Be aware that the global pointer will point to the root BUT that
 * the root will change as new graphs are added to the top.
 */
typedef struct GRAPH_global_context
{
    char *graph_name;              /* graph name */
    Oid graph_oid;                 /* graph oid for searching */
    HTAB *vertex_hashtable;        /* hashtable to hold vertex edge lists */
    HTAB *edge_hashtable;          /* hashtable to hold edge to vertex map */
    TransactionId xmin;            /* transaction ids for this graph */
    TransactionId xmax;
    CommandId curcid;              /* currentCommandId graph was created with */
    int64 num_loaded_vertices;     /* number of loaded vertices in this graph */
    int64 num_loaded_edges;        /* number of loaded edges in this graph */
    ListGraphId *vertices;         /* vertices for vertex hashtable cleanup */
    struct GRAPH_global_context *next; /* next graph */
} GRAPH_global_context;

/* global variable to hold the per process GRAPH global context */
static GRAPH_global_context *global_graph_contexts = NULL;

/* declarations */
/* GRAPH global context functions */
static void free_specific_GRAPH_global_context(GRAPH_global_context *ggctx);
static bool delete_specific_GRAPH_global_contexts(char *graph_name);
static bool delete_GRAPH_global_contexts(void);
static void create_GRAPH_global_hashtables(GRAPH_global_context *ggctx);
static void load_GRAPH_global_hashtables(GRAPH_global_context *ggctx);
static void load_vertex_hashtable(GRAPH_global_context *ggctx);
static void load_edge_hashtable(GRAPH_global_context *ggctx);
static void freeze_GRAPH_global_hashtables(GRAPH_global_context *ggctx);
static List *get_ag_labels_names(Snapshot snapshot, Oid graph_oid,
                                 char label_type);
static bool insert_edge(GRAPH_global_context *ggctx, graphid edge_id,
                        Datum edge_properties, graphid start_vertex_id,
                        graphid end_vertex_id, Oid edge_label_table_oid);
static bool insert_vertex_edge(GRAPH_global_context *ggctx,
                               graphid start_vertex_id, graphid end_vertex_id,
                               graphid edge_id);
static bool insert_vertex_entry(GRAPH_global_context *ggctx, graphid vertex_id,
                                Oid vertex_label_table_oid,
                                Datum vertex_properties);
/* definitions */

/*
 * Helper function to determine validity of the passed GRAPH_global_context.
 * This is based off of the current active snapshot, to see if the graph could
 * have been modified. Ideally, we should find a way to more accurately know
 * whether the particular graph was modified.
 */
bool is_ggctx_invalid(GRAPH_global_context *ggctx)
{
    Snapshot snap = GetActiveSnapshot();

    /*
     * If the transaction ids (xmin or xmax) or currentCommandId (curcid) have
     * changed, then we have a graph that was updated. This means that the
     * global context for this graph is no longer valid.
     */
    return (ggctx->xmin != snap->xmin ||
            ggctx->xmax != snap->xmax ||
            ggctx->curcid != snap->curcid);

}
/*
 * Helper function to create the global vertex and edge hashtables. One
 * hashtable will hold the vertex, its edges (both incoming and exiting) as a
 * list, and its properties datum. The other hashtable will hold the edge, its
 * properties datum, and its source and target vertex.
 */
static void create_GRAPH_global_hashtables(GRAPH_global_context *ggctx)
{
    HASHCTL vertex_ctl;
    HASHCTL edge_ctl;
    char *graph_name = NULL;
    char *vhn = NULL;
    char *ehn = NULL;
    int glen;
    int vlen;
    int elen;

    /* get the graph name and length */
    graph_name = ggctx->graph_name;
    glen = strlen(graph_name);
    /* get the vertex htab name length */
    vlen = strlen(VERTEX_HTAB_NAME);
    /* get the edge htab name length */
    elen = strlen(EDGE_HTAB_NAME);
    /* allocate the space and build the names */
    vhn = palloc0(vlen + glen + 1);
    ehn = palloc0(elen + glen + 1);
    /* copy in the names */
    strcpy(vhn, VERTEX_HTAB_NAME);
    strcpy(ehn, EDGE_HTAB_NAME);
    /* add in the graph name */
    vhn = strncat(vhn, graph_name, glen);
    ehn = strncat(ehn, graph_name, glen);

    /* initialize the vertex hashtable */
    MemSet(&vertex_ctl, 0, sizeof(vertex_ctl));
    vertex_ctl.keysize = sizeof(int64);
    vertex_ctl.entrysize = sizeof(vertex_entry);
    vertex_ctl.hash = tag_hash;
    ggctx->vertex_hashtable = hash_create(vhn, VERTEX_HTAB_INITIAL_SIZE,
                                          &vertex_ctl,
                                          HASH_ELEM | HASH_FUNCTION);
    pfree(vhn);

    /* initialize the edge hashtable */
    MemSet(&edge_ctl, 0, sizeof(edge_ctl));
    edge_ctl.keysize = sizeof(int64);
    edge_ctl.entrysize = sizeof(edge_entry);
    edge_ctl.hash = tag_hash;
    ggctx->edge_hashtable = hash_create(ehn, EDGE_HTAB_INITIAL_SIZE, &edge_ctl,
                                        HASH_ELEM | HASH_FUNCTION);
    pfree(ehn);
}

/* helper function to get a List of all label names for the specified graph */
static List *get_ag_labels_names(Snapshot snapshot, Oid graph_oid,
                                 char label_type)
{
    List *labels = NIL;
    ScanKeyData scan_keys[2];
    Relation ag_label;
    TableScanDesc scan_desc;
    HeapTuple tuple;
    TupleDesc tupdesc;

    /* we need a valid snapshot */
    Assert(snapshot != NULL);

    /* setup scan keys to get all edges for the given graph oid */
    ScanKeyInit(&scan_keys[1], Anum_ag_label_graph, BTEqualStrategyNumber,
                F_OIDEQ, ObjectIdGetDatum(graph_oid));
    ScanKeyInit(&scan_keys[0], Anum_ag_label_kind, BTEqualStrategyNumber,
                F_CHAREQ, CharGetDatum(label_type));

    /* setup the table to be scanned, ag_label in this case */
    ag_label = table_open(ag_label_relation_id(), ShareLock);
    scan_desc = table_beginscan(ag_label, snapshot, 2, scan_keys);

    /* get the tupdesc - we don't need to release this one */
    tupdesc = RelationGetDescr(ag_label);
    /* bail if the number of columns differs - this table has 5 */
    Assert(tupdesc->natts == Natts_ag_label);

    /* get all of the label names */
    while((tuple = heap_getnext(scan_desc, ForwardScanDirection)) != NULL)
    {
        Name label;
        bool is_null = false;

        /* something is wrong if this tuple isn't valid */
        Assert(HeapTupleIsValid(tuple));
        /* get the label name */
        label = DatumGetName(heap_getattr(tuple, Anum_ag_label_name, tupdesc,
                                          &is_null));
        Assert(!is_null);
        /* add it to our list */
        labels = lappend(labels, label);
    }

    /* close up scan */
    table_endscan(scan_desc);
    table_close(ag_label, ShareLock);

    return labels;
}

/*
 * Helper function to insert one edge/edge->vertex, key/value pair, in the
 * current GRAPH global edge hashtable.
 */
static bool insert_edge(GRAPH_global_context *ggctx, graphid edge_id,
                        Datum edge_properties, graphid start_vertex_id,
                        graphid end_vertex_id, Oid edge_label_table_oid)
{
    edge_entry *value = NULL;
    bool found = false;

    /* search for the edge */
    value = (edge_entry *)hash_search(ggctx->edge_hashtable, (void *)&edge_id,
                                      HASH_ENTER, &found);
    /*
     * If we found the key, either we have a duplicate, or we made a mistake and
     * inserted it already. Either way, this isn't good so don't insert it and
     * return false. Likewise, if the value returned is NULL, don't do anything,
     * just return false. This way the caller can decide what to do.
     */
    if (found || value == NULL)
    {
        return false;
    }

    /* not sure if we really need to zero out the entry, as we set everything */
    MemSet(value, 0, sizeof(edge_entry));

    /*
     * Set the edge id - this is important as this is the hash key value used
     * for hash function collisions.
     */
    value->edge_id = edge_id;
    value->edge_properties = edge_properties;
    value->start_vertex_id = start_vertex_id;
    value->end_vertex_id = end_vertex_id;
    value->edge_label_table_oid = edge_label_table_oid;

    /* increment the number of loaded edges */
    ggctx->num_loaded_edges++;

    return true;
}

/*
 * Helper function to insert an entire vertex into the current GRAPH global
 * vertex hashtable. It will return false if there is a duplicate.
 */
static bool insert_vertex_entry(GRAPH_global_context *ggctx, graphid vertex_id,
                                Oid vertex_label_table_oid,
                                Datum vertex_properties)
{
    vertex_entry *ve = NULL;
    bool found = false;

    /* search for the vertex */
    ve = (vertex_entry *)hash_search(ggctx->vertex_hashtable,
                                     (void *)&vertex_id, HASH_ENTER, &found);

    /* we should never have duplicates, return false */
    if (found)
    {
        return false;
    }

    /* again, MemSet may not be needed here */
    MemSet(ve, 0, sizeof(vertex_entry));

    /*
     * Set the vertex id - this is important as this is the hash key value
     * used for hash function collisions.
     */
    ve->vertex_id = vertex_id;
    /* set the label table oid for this vertex */
    ve->vertex_label_table_oid = vertex_label_table_oid;
    /* set the datum vertex properties */
    ve->vertex_properties = vertex_properties;
    /* set the NIL edge list */
    ve->edges_in = NULL;
    ve->edges_out = NULL;
    ve->edges_self = NULL;

    /* we also need to store the vertex id for clean up of vertex lists */
    ggctx->vertices = append_graphid(ggctx->vertices, vertex_id);

    /* increment the number of loaded vertices */
    ggctx->num_loaded_vertices++;

    return true;
}

/*
 * Helper function to append one edge to an existing vertex in the current
 * global vertex hashtable.
 */
static bool insert_vertex_edge(GRAPH_global_context *ggctx,
                               graphid start_vertex_id, graphid end_vertex_id,
                               graphid edge_id)
{
    vertex_entry *value = NULL;
    bool found = false;
    bool is_selfloop = false;

    /* is it a self loop */
    is_selfloop = (start_vertex_id == end_vertex_id);

    /* search for the start vertex of the edge */
    value = (vertex_entry *)hash_search(ggctx->vertex_hashtable,
                                        (void *)&start_vertex_id, HASH_FIND,
                                        &found);
    /* vertices were preloaded so it must be there */
    Assert(found);
    if (!found)
    {
        return found;
    }

    /* if it is a self loop, add the edge to edges_self and we're done */
    if (is_selfloop)
    {
        value->edges_self = append_graphid(value->edges_self, edge_id);
        return found;
    }

    /* add the edge to the edges_out list of the start vertex */
    value->edges_out = append_graphid(value->edges_out, edge_id);

    /* search for the end vertex of the edge */
    value = (vertex_entry *)hash_search(ggctx->vertex_hashtable,
                                        (void *)&end_vertex_id, HASH_FIND,
                                        &found);
    /* vertices were preloaded so it must be there */
    Assert(found);
    if (!found)
    {
        return found;
    }

    /* add the edge to the edges_in list of the end vertex */
    value->edges_in = append_graphid(value->edges_in, edge_id);

    return found;
}

/* helper routine to load all vertices into the GRAPH global vertex hashtable */
static void load_vertex_hashtable(GRAPH_global_context *ggctx)
{
    Oid graph_oid;
    Oid graph_namespace_oid;
    Snapshot snapshot;
    List *vertex_label_names = NIL;
    ListCell *lc;

    /* get the specific graph OID and namespace (schema) OID */
    graph_oid = ggctx->graph_oid;
    graph_namespace_oid = get_namespace_oid(ggctx->graph_name, false);
    /* get the active snapshot */
    snapshot = GetActiveSnapshot();
    /* get the names of all of the vertex label tables */
    vertex_label_names = get_ag_labels_names(snapshot, graph_oid,
                                             LABEL_TYPE_VERTEX);
    /* go through all vertex label tables in list */
    foreach (lc, vertex_label_names)
    {
        Relation graph_vertex_label;
        TableScanDesc scan_desc;
        HeapTuple tuple;
        char *vertex_label_name;
        Oid vertex_label_table_oid;
        TupleDesc tupdesc;

        /* get the vertex label name */
        vertex_label_name = lfirst(lc);
        /* get the vertex label name's OID */
        vertex_label_table_oid = get_relname_relid(vertex_label_name,
                                                   graph_namespace_oid);
        /* open the relation (table) and begin the scan */
        graph_vertex_label = table_open(vertex_label_table_oid, ShareLock);
        scan_desc = table_beginscan(graph_vertex_label, snapshot, 0, NULL);
        /* get the tupdesc - we don't need to release this one */
        tupdesc = RelationGetDescr(graph_vertex_label);
        /* bail if the number of columns differs */
        if (tupdesc->natts != 2)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_UNDEFINED_TABLE),
                     errmsg("Invalid number of attributes for %s.%s",
                     ggctx->graph_name, vertex_label_name)));
        }
        /* get all tuples in table and insert them into graph hashtables */
        while((tuple = heap_getnext(scan_desc, ForwardScanDirection)) != NULL)
        {
            graphid vertex_id;
            Datum vertex_properties;
            bool inserted = false;

            /* something is wrong if this isn't true */
            Assert(HeapTupleIsValid(tuple));
            /* get the vertex id */
            vertex_id = DatumGetInt64(column_get_datum(tupdesc, tuple, 0, "id",
                                                       GRAPHIDOID, true));
            /* get the vertex properties datum */
            vertex_properties = column_get_datum(tupdesc, tuple, 1,
                                                 "properties", AGTYPEOID, true);
            /* we need to make a copy of the properties datum */
            vertex_properties = datumCopy(vertex_properties, false, -1);

            /* insert vertex into vertex hashtable */
            inserted = insert_vertex_entry(ggctx, vertex_id,
                                           vertex_label_table_oid,
                                           vertex_properties);

            /* this insert must not fail, it means there is a duplicate */
            if (!inserted)
            {
                 elog(ERROR, "insert_vertex_entry: failed due to duplicate");
            }
        }

        /* end the scan and close the relation */
        table_endscan(scan_desc);
        table_close(graph_vertex_label, ShareLock);
    }
}

/*
 * Helper function to load all of the GRAPH global hashtables (vertex & edge)
 * for the current global context.
 */
static void load_GRAPH_global_hashtables(GRAPH_global_context *ggctx)
{
    /* initialize statistics */
    ggctx->num_loaded_vertices = 0;
    ggctx->num_loaded_edges = 0;

    /* insert all of our vertices */
    load_vertex_hashtable(ggctx);

    /* insert all of our edges */
    load_edge_hashtable(ggctx);
}

/*
 * Helper routine to load all edges into the GRAPH global edge and vertex
 * hashtables.
 */
static void load_edge_hashtable(GRAPH_global_context *ggctx)
{
    Oid graph_oid;
    Oid graph_namespace_oid;
    Snapshot snapshot;
    List *edge_label_names = NIL;
    ListCell *lc;

    /* get the specific graph OID and namespace (schema) OID */
    graph_oid = ggctx->graph_oid;
    graph_namespace_oid = get_namespace_oid(ggctx->graph_name, false);
    /* get the active snapshot */
    snapshot = GetActiveSnapshot();
    /* get the names of all of the edge label tables */
    edge_label_names = get_ag_labels_names(snapshot, graph_oid,
                                           LABEL_TYPE_EDGE);
    /* go through all edge label tables in list */
    foreach (lc, edge_label_names)
    {
        Relation graph_edge_label;
        TableScanDesc scan_desc;
        HeapTuple tuple;
        char *edge_label_name;
        Oid edge_label_table_oid;
        TupleDesc tupdesc;

        /* get the edge label name */
        edge_label_name = lfirst(lc);
        /* get the edge label name's OID */
        edge_label_table_oid = get_relname_relid(edge_label_name,
                                                 graph_namespace_oid);
        /* open the relation (table) and begin the scan */
        graph_edge_label = table_open(edge_label_table_oid, ShareLock);
        scan_desc = table_beginscan(graph_edge_label, snapshot, 0, NULL);
        /* get the tupdesc - we don't need to release this one */
        tupdesc = RelationGetDescr(graph_edge_label);
        /* bail if the number of columns differs */
        if (tupdesc->natts != 4)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_UNDEFINED_TABLE),
                     errmsg("Invalid number of attributes for %s.%s",
                     ggctx->graph_name, edge_label_name)));
        }
        /* get all tuples in table and insert them into graph hashtables */
        while((tuple = heap_getnext(scan_desc, ForwardScanDirection)) != NULL)
        {
            graphid edge_id;
            graphid edge_vertex_start_id;
            graphid edge_vertex_end_id;
            Datum edge_properties;
            bool inserted = false;

            /* something is wrong if this isn't true */
            Assert(HeapTupleIsValid(tuple));
            /* get the edge id */
            edge_id = DatumGetInt64(column_get_datum(tupdesc, tuple, 0, "id",
                                                     GRAPHIDOID, true));
            /* get the edge start_id (start vertex id) */
            edge_vertex_start_id = DatumGetInt64(column_get_datum(tupdesc,
                                                                  tuple, 1,
                                                                  "start_id",
                                                                  GRAPHIDOID,
                                                                  true));
            /* get the edge end_id (end vertex id)*/
            edge_vertex_end_id = DatumGetInt64(column_get_datum(tupdesc, tuple,
                                                                2, "end_id",
                                                                GRAPHIDOID,
                                                                true));
            /* get the edge properties datum */
            edge_properties = column_get_datum(tupdesc, tuple, 3, "properties",
                                               AGTYPEOID, true);

            /* we need to make a copy of the properties datum */
            edge_properties = datumCopy(edge_properties, false, -1);

            /* insert edge into edge hashtable */
            inserted = insert_edge(ggctx, edge_id, edge_properties,
                                   edge_vertex_start_id, edge_vertex_end_id,
                                   edge_label_table_oid);

            /* this insert must not fail */
            if (!inserted)
            {
                 elog(ERROR, "insert_edge: failed to insert");
            }

            /* insert the edge into the start and end vertices edge lists */
            inserted = insert_vertex_edge(ggctx, edge_vertex_start_id,
                                          edge_vertex_end_id, edge_id);
            /* this insert must not fail */
            if (!inserted)
            {
                 elog(ERROR, "insert_vertex_edge: failed to insert");
            }
        }

        /* end the scan and close the relation */
        table_endscan(scan_desc);
        table_close(graph_edge_label, ShareLock);
    }
}

/*
 * Helper function to freeze the GRAPH global hashtables from additional
 * inserts. This may, or may not, be useful. Currently, these hashtables are
 * only seen by the creating process and only for reading.
 */
static void freeze_GRAPH_global_hashtables(GRAPH_global_context *ggctx)
{
    hash_freeze(ggctx->vertex_hashtable);
    hash_freeze(ggctx->edge_hashtable);
}

/*
 * Helper function to free the entire specified GRAPH global context. After
 * running this you should not use the pointer in ggctx.
 */
static void free_specific_GRAPH_global_context(GRAPH_global_context *ggctx)
{
    GraphIdNode *curr_vertex = NULL;

    /* don't do anything if NULL */
    if (ggctx == NULL)
    {
        return;
    }

    /* free the graph name */
    pfree(ggctx->graph_name);
    ggctx->graph_name = NULL;

    ggctx->graph_oid = InvalidOid;
    ggctx->next = NULL;

    /* free the vertex edge lists, starting with the head */
    curr_vertex = peek_stack_head(ggctx->vertices);
    while (curr_vertex != NULL)
    {
        GraphIdNode *next_vertex = NULL;
        vertex_entry *value = NULL;
        bool found = false;
        graphid vertex_id;

        /* get the next vertex in the list, if any */
        next_vertex = next_GraphIdNode(curr_vertex);

        /* get the current vertex id */
        vertex_id = get_graphid(curr_vertex);

        /* retrieve the vertex entry */
        value = (vertex_entry *)hash_search(ggctx->vertex_hashtable,
                                            (void *)&vertex_id, HASH_FIND,
                                            &found);
        /* this is bad if it isn't found */
        Assert(found);

        /* free the edge list associated with this vertex */
        free_ListGraphId(value->edges_in);
        free_ListGraphId(value->edges_out);
        free_ListGraphId(value->edges_self);

        value->edges_in = NULL;
        value->edges_out = NULL;
        value->edges_self = NULL;

        /* move to the next vertex */
        curr_vertex = next_vertex;
    }

    /* free the vertices list */
    free_ListGraphId(ggctx->vertices);
    ggctx->vertices = NULL;

    /* free the hashtables */
    hash_destroy(ggctx->vertex_hashtable);
    hash_destroy(ggctx->edge_hashtable);

    ggctx->vertex_hashtable = NULL;
    ggctx->edge_hashtable = NULL;

    /* free the context */
    pfree(ggctx);
    ggctx = NULL;
}

/*
 * Helper function to manage the GRAPH global contexts. It will create the
 * context for the graph specified, provided it isn't already built and valid.
 * During processing it will free (delete) all invalid GRAPH contexts. It
 * returns the GRAPH global context for the specified graph.
 */
GRAPH_global_context *manage_GRAPH_global_contexts(char *graph_name,
                                                   Oid graph_oid)
{
    GRAPH_global_context *new_ggctx = NULL;
    GRAPH_global_context *curr_ggctx = NULL;
    GRAPH_global_context *prev_ggctx = NULL;
    MemoryContext oldctx = NULL;

    /* we need a higher context, or one that isn't destroyed by SRF exit */
    oldctx = MemoryContextSwitchTo(TopMemoryContext);

    /*
     * We need to see if any GRAPH global contexts already exist and if any do
     * for this particular graph. There are 5 possibilities -
     *
     *     1) There are no global contexts.
     *     2) One does exist for this graph but, is invalid.
     *     3) One does exist for this graph and is valid.
     *     4) One or more other contexts do exist and all are valid.
     *     5) One or more other contexts do exist but, one or more are invalid.
     */

    /* free the invalidated GRAPH global contexts first */
    prev_ggctx = NULL;
    curr_ggctx = global_graph_contexts;
    while (curr_ggctx != NULL)
    {
        GRAPH_global_context *next_ggctx = curr_ggctx->next;

        /* if the transaction ids have changed, we have an invalid graph */
        if (is_ggctx_invalid(curr_ggctx))
        {
            /*
             * If prev_ggctx is NULL then we are freeing the top of the
             * contexts. So, we need to point the global variable to the
             * new (next) top context, if there is one.
             */
            if (prev_ggctx == NULL)
            {
                global_graph_contexts = next_ggctx;
            }
            else
            {
                prev_ggctx->next = curr_ggctx->next;
            }

            /* free the current graph context */
            free_specific_GRAPH_global_context(curr_ggctx);
        }
        else
        {
            prev_ggctx = curr_ggctx;
        }

        /* advance to the next context */
        curr_ggctx = next_ggctx;
    }

    /* find our graph's context. if it exists, we are done */
    curr_ggctx = global_graph_contexts;
    while (curr_ggctx != NULL)
    {
        if (curr_ggctx->graph_oid == graph_oid)
        {
            /* switch our context back */
            MemoryContextSwitchTo(oldctx);
            /* we are done */
            return curr_ggctx;
        }
        curr_ggctx = curr_ggctx->next;
    }

    /* otherwise, we need to create one and possibly attach it */
    new_ggctx = palloc0(sizeof(GRAPH_global_context));

    if (global_graph_contexts != NULL)
    {
        new_ggctx->next = global_graph_contexts;
    }
    else
    {
        new_ggctx->next = NULL;
    }

    /* set the global context variable */
    global_graph_contexts = new_ggctx;

    /* set the graph name and oid */
    new_ggctx->graph_name = pstrdup(graph_name);
    new_ggctx->graph_oid = graph_oid;

    /* set the transaction ids */
    new_ggctx->xmin = GetActiveSnapshot()->xmin;
    new_ggctx->xmax = GetActiveSnapshot()->xmax;
    new_ggctx->curcid = GetActiveSnapshot()->curcid;

    /* initialize our vertices list */
    new_ggctx->vertices = NULL;

    /* build the hashtables for this graph */
    create_GRAPH_global_hashtables(new_ggctx);
    load_GRAPH_global_hashtables(new_ggctx);
    freeze_GRAPH_global_hashtables(new_ggctx);

    /* switch back to the previous memory context */
    MemoryContextSwitchTo(oldctx);

    return new_ggctx;
}

/*
 * Helper function to delete all of the global graph contexts used by the
 * process. When done the global global_graph_contexts will be NULL.
 */
static bool delete_GRAPH_global_contexts(void)
{
    GRAPH_global_context *curr_ggctx = NULL;
    bool retval = false;

    /* get the first context, if any */
    curr_ggctx = global_graph_contexts;

    /* free all GRAPH global contexts */
    while (curr_ggctx != NULL)
    {
        GRAPH_global_context *next_ggctx = curr_ggctx->next;

        /* free the current graph context */
        free_specific_GRAPH_global_context(curr_ggctx);

        /* advance to the next context */
        curr_ggctx = next_ggctx;

        retval = true;
    }

    /* clear the global variable */
    global_graph_contexts = NULL;

    return retval;
}

/*
 * Helper function to delete a specific global graph context used by the
 * process.
 */
static bool delete_specific_GRAPH_global_contexts(char *graph_name)
{
    GRAPH_global_context *prev_ggctx = NULL;
    GRAPH_global_context *curr_ggctx = NULL;
    Oid graph_oid = InvalidOid;

    if (graph_name == NULL)
    {
        return false;
    }

    /* get the graph oid */
    graph_oid = get_graph_oid(graph_name);

    /* get the first context, if any */
    curr_ggctx = global_graph_contexts;

    /* find the specified GRAPH global context */
    while (curr_ggctx != NULL)
    {
        GRAPH_global_context *next_ggctx = curr_ggctx->next;

        if (curr_ggctx->graph_oid == graph_oid)
        {
            /*
             * If prev_ggctx is NULL then we are freeing the top of the
             * contexts. So, we need to point the global variable to the
             * new (next) top context, if there is one.
             */
            if (prev_ggctx == NULL)
            {
                global_graph_contexts = next_ggctx;
            }
            else
            {
                prev_ggctx->next = curr_ggctx->next;
            }

            /* free the current graph context */
            free_specific_GRAPH_global_context(curr_ggctx);

            /* we found and freed it, return true */
            return true;
        }

        /* save the current as previous and advance to the next one */
        prev_ggctx = curr_ggctx;
        curr_ggctx = next_ggctx;
    }

    /* we didn't find it, return false */
    return false;
}

/*
 * Helper function to retrieve a vertex_entry from the graph's vertex hash
 * table. If there isn't one, it returns a NULL. The latter is necessary for
 * checking if the vsid and veid entries exist.
 */
vertex_entry *get_vertex_entry(GRAPH_global_context *ggctx, graphid vertex_id)
{
    vertex_entry *ve = NULL;
    bool found = false;

    /* retrieve the current vertex entry */
    ve = (vertex_entry *)hash_search(ggctx->vertex_hashtable,
                                     (void *)&vertex_id, HASH_FIND, &found);
    return ve;
}

/* helper function to retrieve an edge_entry from the graph's edge hash table */
edge_entry *get_edge_entry(GRAPH_global_context *ggctx, graphid edge_id)
{
    edge_entry *ee = NULL;
    bool found = false;

    /* retrieve the current edge entry */
    ee = (edge_entry *)hash_search(ggctx->edge_hashtable, (void *)&edge_id,
                                   HASH_FIND, &found);
    /* it should be found, otherwise we have problems */
    Assert(found);

    return ee;
}

/*
 * Helper function to find the GRAPH_global_context used by the specified
 * graph_oid. If not found, it returns NULL.
 */
GRAPH_global_context *find_GRAPH_global_context(Oid graph_oid)
{
    GRAPH_global_context *ggctx = NULL;

    /* get the root */
    ggctx = global_graph_contexts;

    while(ggctx != NULL)
    {
        /* if we found it return it */
        if (ggctx->graph_oid == graph_oid)
        {
            return ggctx;
        }

        /* advance to the next context */
        ggctx = ggctx->next;
    }

    /* we did not find it so return NULL */
    return NULL;
}

/* graph vertices accessor */
ListGraphId *get_graph_vertices(GRAPH_global_context *ggctx)
{
    return ggctx->vertices;
}

/* vertex_entry accessor functions */
graphid get_vertex_entry_id(vertex_entry *ve)
{
    return ve->vertex_id;
}

ListGraphId *get_vertex_entry_edges_in(vertex_entry *ve)
{
    return ve->edges_in;
}

ListGraphId *get_vertex_entry_edges_out(vertex_entry *ve)
{
    return ve->edges_out;
}

ListGraphId *get_vertex_entry_edges_self(vertex_entry *ve)
{
    return ve->edges_self;
}


Oid get_vertex_entry_label_table_oid(vertex_entry *ve)
{
    return ve->vertex_label_table_oid;
}

Datum get_vertex_entry_properties(vertex_entry *ve)
{
    return ve->vertex_properties;
}

/* edge_entry accessor functions */
graphid get_edge_entry_id(edge_entry *ee)
{
    return ee->edge_id;
}

Oid get_edge_entry_label_table_oid(edge_entry *ee)
{
    return ee->edge_label_table_oid;
}

Datum get_edge_entry_properties(edge_entry *ee)
{
    return ee->edge_properties;
}

graphid get_edge_entry_start_vertex_id(edge_entry *ee)
{
    return ee->start_vertex_id;
}

graphid get_edge_entry_end_vertex_id(edge_entry *ee)
{
    return ee->end_vertex_id;
}

/* PostgreSQL SQL facing functions */

/* PG wrapper function for age_delete_global_graphs */
PG_FUNCTION_INFO_V1(age_delete_global_graphs);

Datum age_delete_global_graphs(PG_FUNCTION_ARGS)
{
    agtype_value *agtv_temp = NULL;
    bool success = false;

    /* get the graph name if supplied */
    if (!PG_ARGISNULL(0))
    {
        agtv_temp = get_agtype_value("delete_global_graphs",
                                     AG_GET_ARG_AGTYPE_P(0),
                                     AGTV_STRING, false);
    }

    if (agtv_temp == NULL || agtv_temp->type == AGTV_NULL)
    {
        success = delete_GRAPH_global_contexts();
    }
    else if (agtv_temp->type == AGTV_STRING)
    {
        char *graph_name = NULL;

        graph_name = agtv_temp->val.string.val;
        success = delete_specific_GRAPH_global_contexts(graph_name);
    }
    else
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("delete_global_graphs: invalid graph name type")));
    }

    PG_RETURN_BOOL(success);
}

/* PG wrapper function for age_vertex_degree */
PG_FUNCTION_INFO_V1(age_vertex_stats);

Datum age_vertex_stats(PG_FUNCTION_ARGS)
{
    GRAPH_global_context *ggctx = NULL;
    vertex_entry *ve = NULL;
    ListGraphId *edges = NULL;
    agtype_value *agtv_vertex = NULL;
    agtype_value *agtv_temp = NULL;
    agtype_value agtv_integer;
    agtype_in_state result;
    char *graph_name = NULL;
    Oid graph_oid = InvalidOid;
    graphid vid = 0;
    int64 self_loops = 0;
    int64 degree = 0;

    /* the graph name is required, but this generally isn't user supplied */
    if (PG_ARGISNULL(0))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("vertex_stats: graph name cannot be NULL")));
    }

    /* get the graph name */
    agtv_temp = get_agtype_value("vertex_stats", AG_GET_ARG_AGTYPE_P(0),
                                 AGTV_STRING, true);

    /* we need the vertex */
    if (PG_ARGISNULL(1))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("vertex_stats: vertex cannot be NULL")));
    }

    /* get the vertex */
    agtv_vertex = get_agtype_value("vertex_stats", AG_GET_ARG_AGTYPE_P(1),
                                   AGTV_VERTEX, true);

    graph_name = pnstrdup(agtv_temp->val.string.val,
                          agtv_temp->val.string.len);

    /* get the graph oid */
    graph_oid = get_graph_oid(graph_name);

    /*
     * Create or retrieve the GRAPH global context for this graph. This function
     * will also purge off invalidated contexts.
     */
    ggctx = manage_GRAPH_global_contexts(graph_name, graph_oid);

    /* free the graph name */
    pfree(graph_name);

    /* get the id */
    agtv_temp = GET_AGTYPE_VALUE_OBJECT_VALUE(agtv_vertex, "id");
    vid = agtv_temp->val.int_value;

    /* get the vertex entry */
    ve = get_vertex_entry(ggctx, vid);

    /* zero the state */
    memset(&result, 0, sizeof(agtype_in_state));

    /* start the object */
    result.res = push_agtype_value(&result.parse_state, WAGT_BEGIN_OBJECT,
                                   NULL);
    /* store the id */
    result.res = push_agtype_value(&result.parse_state, WAGT_KEY,
                                   string_to_agtype_value("id"));
    result.res = push_agtype_value(&result.parse_state, WAGT_VALUE, agtv_temp);

    /* store the label */
    agtv_temp = GET_AGTYPE_VALUE_OBJECT_VALUE(agtv_vertex, "label");
    result.res = push_agtype_value(&result.parse_state, WAGT_KEY,
                                   string_to_agtype_value("label"));
    result.res = push_agtype_value(&result.parse_state, WAGT_VALUE, agtv_temp);

    /* set up an integer for returning values */
    agtv_temp = &agtv_integer;
    agtv_temp->type = AGTV_INTEGER;
    agtv_temp->val.int_value = 0;

    /* get and store the self_loops */
    edges = get_vertex_entry_edges_self(ve);
    self_loops = (edges != NULL) ? get_list_size(edges) : 0;
    agtv_temp->val.int_value = self_loops;
    result.res = push_agtype_value(&result.parse_state, WAGT_KEY,
                                   string_to_agtype_value("self_loops"));
    result.res = push_agtype_value(&result.parse_state, WAGT_VALUE, agtv_temp);

    /* get and store the in_degree */
    edges = get_vertex_entry_edges_in(ve);
    degree = (edges != NULL) ? get_list_size(edges) : 0;
    agtv_temp->val.int_value = degree + self_loops;
    result.res = push_agtype_value(&result.parse_state, WAGT_KEY,
                                   string_to_agtype_value("in_degree"));
    result.res = push_agtype_value(&result.parse_state, WAGT_VALUE, agtv_temp);

    /* get and store the out_degree */
    edges = get_vertex_entry_edges_out(ve);
    degree = (edges != NULL) ? get_list_size(edges) : 0;
    agtv_temp->val.int_value = degree + self_loops;
    result.res = push_agtype_value(&result.parse_state, WAGT_KEY,
                                   string_to_agtype_value("out_degree"));
    result.res = push_agtype_value(&result.parse_state, WAGT_VALUE, agtv_temp);

    /* close the object */
    result.res = push_agtype_value(&result.parse_state, WAGT_END_OBJECT, NULL);

    result.res->type = AGTV_OBJECT;

    PG_RETURN_POINTER(agtype_value_to_agtype(result.res));
}
