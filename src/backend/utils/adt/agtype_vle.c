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

#include "catalog/namespace.h"
#include "catalog/pg_type.h"
#include "funcapi.h"
#include "utils/datum.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"

#include "commands/label_commands.h"
#include "utils/agtype.h"
#include "utils/agtype_vle.h"
#include "catalog/ag_graph.h"
#include "catalog/ag_label.h"
#include "utils/graphid.h"
#include "nodes/cypher_nodes.h"

#define VERTEX_HTAB_NAME "Vertex to edge lists " /* added a space at end for */
#define EDGE_HTAB_NAME "Edge to vertex mapping " /* the graph name to follow */
#define EDGE_STATE_HTAB_NAME "Edge state "

/*
 * A simple linked list node for graphid lists (int64). PG's implementation
 * has too much overhead for this type of list as it only directly supports
 * regular ints, not int64s, of which a graphid currently is.
 */
typedef struct GraphIdNode
{
    graphid id;
    struct GraphIdNode *next;
} GraphIdNode;

/* a container for a linked list of GraphIdNodes */
typedef struct ListGraphId
{
    GraphIdNode *head;
    GraphIdNode *tail;
    int64 size;
} ListGraphId;

/* vertex entry for the vertex_hastable */
typedef struct vertex_entry
{
    graphid vertex_id;             /* vertex id, it is also the hash key */
    ListGraphId *edges;            /* A list of all (entering & exiting) edges'
                                    * graphids (int64s). */
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

/* edge state entry for the edge_state_hashtable */
typedef struct edge_state_entry
{
    graphid edge_id;               /* edge id, it is also the hash key */
    bool used_in_path;             /* like visited but more descriptive */
    bool has_been_matched;         /* have we checked for a  match */
    bool matched;                  /* is it a match */
} edge_state_entry;

/*
 * VLE global context per graph. They are chained together via next.
 * Be aware that the global pointer will point to the root BUT that
 * the root will change as new graphs are added to the top.
 */
typedef struct VLE_global_context
{
    char *graph_name;              /* graph name */
    Oid graph_oid;                 /* graph oid for searching */
    HTAB *vertex_hashtable;        /* hashtable to hold vertex edge lists */
    HTAB *edge_hashtable;          /* hashtable to hold edge to vertex map */
    TransactionId xmin;            /* transaction ids for this graph */
    TransactionId xmax;
    int64 num_loaded_vertices;     /* number of loaded vertices in this graph */
    int64 num_loaded_edges;        /* number of loaded edges in this graph */
    ListGraphId *vertices;         /* vertices for vertex hashtable cleanup */
    struct VLE_global_context *next; /* next graph */
} VLE_global_context;

/* VLE local context per each age_vle function in a query line */
typedef struct VLE_local_context
{
    char *graph_name;              /* name of the graph */
    Oid graph_oid;                 /* graph oid for searching */
    VLE_global_context *vlegctx;   /* global context for this local context */
    graphid vsid;                  /* starting vertex id */
    graphid veid;                  /* ending vertex id */
    char *edge_label_name;         /* edge label name for match */
    agtype_value *edge_conditions; /* edge property conditions for match */
    int64 lidx;                    /* lower (start) bound index */
    int64 uidx;                    /* upper (end) bound index */
    bool uidx_infinite;            /* flag if the upper bound is omitted */
    cypher_rel_dir edge_direction; /* the direction of the edge */
    HTAB *vertex_hashtable;        /* link to this context vertex hashtable */
    HTAB *edge_hashtable;          /* link to this context edge hashtable */
    HTAB *edge_state_hashtable;    /* local state hashtable for our edges */
    ListGraphId *dfs_vertex_stack; /* dfs stack for vertices */
    ListGraphId *dfs_edge_stack;   /* dfs stack for edges */
    ListGraphId *dfs_path_stack;   /* dfs stack containing the path */
} VLE_local_context;

/* global variable to hold the per process VLE global context */
static VLE_global_context *global_vle_contexts = NULL;

/* declarations */
/* agtype functions */
static bool is_agtype_null(agtype *agt_arg);
static agtype_value *get_agtype_value(char *funcname, agtype *agt_arg,
                                      enum agtype_value_type type);
static agtype_value *get_agtype_key(agtype_value *agtv, char *search_key,
                                    int search_key_len);
static agtype_iterator *get_next_object_pair(agtype_iterator *it,
                                             agtype_container *agtc,
                                             agtype_value *key,
                                             agtype_value *value);
static bool is_an_edge_match(VLE_local_context *vlelctx, edge_entry *ee);
/* VLE global context functions */
static VLE_global_context *manage_VLE_global_contexts(char *graph_name,
                                                      Oid graph_oid);
static void free_specific_global_context(VLE_global_context *vlegctx);
static void create_VLE_global_hashtables(VLE_global_context *vlegctx);
static void load_VLE_global_hashtables(VLE_global_context *vlegctx);
static void freeze_VLE_global_hashtables(VLE_global_context *vlegctx);
static List *get_edge_labels(Oid graph_oid, Snapshot snapshot);
static bool insert_edge(VLE_global_context *vlegctx, graphid edge_id,
                        Datum edge_properties, graphid start_vertex_id,
                        graphid end_vertex_id, Oid edge_label_table_oid);
static bool insert_vertex(VLE_global_context *vlegctx, graphid vertex_id,
                          graphid edge_id);
/* VLE local context functions */
static VLE_local_context *build_local_vle_context(FunctionCallInfo fcinfo);
static void create_VLE_local_state_hashtable(VLE_local_context *vlelctx);
static void free_VLE_local_context(VLE_local_context *vlelctx);
/* graphid list functions */
static ListGraphId *append_graphid(ListGraphId *container, graphid id);
static void free_ListGraphId(ListGraphId *container);
/* VLE graph traversal functions */
static vertex_entry *get_vertex_entry(VLE_local_context *vlelctx,
                                      graphid vertex_id);
static edge_entry *get_edge_entry(VLE_local_context *vlelctx, graphid edge_id);
static edge_state_entry *get_edge_state(VLE_local_context *vlelctx,
                                        graphid edge_id);
static ListGraphId *new_stack(void);
static void free_stack(ListGraphId *stack);
static void push_stack(ListGraphId *stack, graphid id);
static graphid pop_stack(ListGraphId *stack);
static graphid peek_stack(ListGraphId *stack);
static bool is_stack_empty(ListGraphId *stack);
static void load_initial_dfs_stacks(VLE_local_context *vlelctx);
static bool dfs_find_a_path(VLE_local_context *vlelctx);
static bool do_vsid_and_veid_exist(VLE_local_context *vlelctx);
static void add_valid_vertex_edges(VLE_local_context *vlelctx,
                                   graphid vertex_id);
static graphid get_next_vertex(VLE_local_context *vlelctx, edge_entry *ee);
/* VLE edge building functions */
static graphid *build_graphid_array_from_path_stack(ListGraphId *stack);
static agtype_value **build_edge_agtype_value_array(VLE_local_context *vlelctx,
                                                    graphid *gida, int size);

/* definitions */

/*
 * Helper function to add a graphid to the end of a ListGraphId container.
 * If the container is NULL, it creates the container with the entry.
 */
static ListGraphId *append_graphid(ListGraphId *container, graphid id)
{
    GraphIdNode *new_node = NULL;

    /* create the new link */
    new_node = palloc0(sizeof(GraphIdNode));
    new_node->id = id;
    new_node->next = NULL;

    /*
     * If the container is NULL then this is a new list. So, create the
     * container and add in the new link.
     */
    if (container == NULL)
    {
        container = palloc0(sizeof(ListGraphId));
        container->head = new_node;
        container->tail = new_node;
        container->size = 1;
    }
    /* otherwise, this is an existing list, append id */
    else
    {
        container->tail->next = new_node;
        container->tail = new_node;
        container->size++;
    }

    return container;
}

/* free (delete) a ListGraphId list */
static void free_ListGraphId(ListGraphId *container)
{
    GraphIdNode *curr_node = NULL;
    GraphIdNode *next_node = NULL;

    /* if the container is NULL we don't need to delete anything */
    if (container == NULL)
    {
        return;
    }

    /* otherwise, start from the head, free, and delete the links */
    curr_node = container->head;
    while (curr_node != NULL)
    {
        next_node = curr_node->next;
        /* we can do this because this is just a list of ints */
        pfree(curr_node);
        curr_node = next_node;
    }

    /* free the container */
    pfree(container);
}

/* fast helper function to test for AGTV_NULL in an agtype */
static bool is_agtype_null(agtype *agt_arg)
{
    agtype_container *agtc = &agt_arg->root;

    if (AGTYPE_CONTAINER_IS_SCALAR(agtc) &&
            AGTE_IS_NULL(agtc->children[0]))
    {
        return true;
    }
    return false;
}

/*
 * Extract an agtype_value from an agtype and verify that it is of the
 * correct type.
 *
 * Function will throw an error, stating the calling function name, for
 * invalid values - including AGTV_NULL
 *
 * Note: This only works for scalars wrapped in an array container, not
 * in objects.
 */
static agtype_value *get_agtype_value(char *funcname, agtype *agt_arg,
                                      enum agtype_value_type type)
{
    agtype_value *agtv_value = NULL;

    /* we need these */
    Assert(funcname != NULL);
    Assert(agt_arg != NULL);

    /* is it AGTV_NULL? */
    if (is_agtype_null(agt_arg))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("%s: agtype argument must not be AGTV_NULL",
                        funcname)));
    }

    /* get the agtype value */
    agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

    /* is it the correct type? */
    if (agtv_value->type != type)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("%s: agtype argument of wrong type",
                        funcname)));
    }
    return agtv_value;
}

/*
 * Helper function to create the global VLE vertex and edge hashtables. One
 * hashtable will hold the vertex and its edges (both incoming and exiting), as
 * a list. The other hashtable will hold the edge, its properties datum, and its
 * target vertex.
 */
static void create_VLE_global_hashtables(VLE_global_context *vlegctx)
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
    graph_name = vlegctx->graph_name;
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
    vlegctx->vertex_hashtable = hash_create(vhn, 1000, &vertex_ctl,
                                            HASH_ELEM | HASH_FUNCTION);

    /* initialize the edge hashtable */
    MemSet(&edge_ctl, 0, sizeof(edge_ctl));
    edge_ctl.keysize = sizeof(int64);
    edge_ctl.entrysize = sizeof(edge_entry);
    edge_ctl.hash = tag_hash;
    vlegctx->edge_hashtable = hash_create(ehn, 1000, &edge_ctl,
                                          HASH_ELEM | HASH_FUNCTION);
}

/* helper function to create the local VLE edge state hashtable. */
static void create_VLE_local_state_hashtable(VLE_local_context *vlelctx)
{
    HASHCTL edge_state_ctl;
    char *graph_name = NULL;
    char *eshn = NULL;
    int glen;
    int elen;

    /* get the graph name and length */
    graph_name = vlelctx->graph_name;
    glen = strlen(graph_name);
    /* get the edge state htab name length */
    elen = strlen(EDGE_HTAB_NAME);
    /* allocate the space and build the name */
    eshn = palloc0(elen + glen + 1);
    /* copy in the name */
    strcpy(eshn, EDGE_STATE_HTAB_NAME);
    /* add in the graph name */
    eshn = strncat(eshn, graph_name, glen);

    /* initialize the edge state hashtable */
    MemSet(&edge_state_ctl, 0, sizeof(edge_state_ctl));
    edge_state_ctl.keysize = sizeof(int64);
    edge_state_ctl.entrysize = sizeof(edge_state_entry);
    edge_state_ctl.hash = tag_hash;
    vlelctx->edge_state_hashtable = hash_create(eshn, 1000, &edge_state_ctl,
                                                HASH_ELEM | HASH_FUNCTION);
}

/*
 * Helper function to step through an object's key/value pairs.
 * The function will return NULL if there is nothing to do. Otherwise,
 * it will return the iterator and the passed key and value agtype_values
 * will be populated. The function should initially be called with a NULL for
 * the iterator, to initialize it to the beginning of the object.
 *
 * Note: This function is only for OBJECT containers.
 */
static agtype_iterator *get_next_object_pair(agtype_iterator *it,
                                             agtype_container *agtc,
                                             agtype_value *key,
                                             agtype_value *value)
{
    agtype_iterator_token itok;
    agtype_value tmp;

    /* verify input params */
    Assert(agtc != NULL);
    Assert(key != NULL);
    Assert(value != NULL);

    /* check to see if the container is empty */
    if (AGTYPE_CONTAINER_SIZE(agtc) == 0)
    {
        return NULL;
    }

    /* if the passed iterator is NULL, this is the first time, create it */
    if (it == NULL)
    {
        /* initial the iterator */
        it = agtype_iterator_init(agtc);
        /* get the first token */
        itok = agtype_iterator_next(&it, &tmp, false);
        /* it should be WAGT_BEGIN_OBJECT */
        Assert(itok == WAGT_BEGIN_OBJECT);
    }

    /* the next token should be a key or the end of the object */
    itok = agtype_iterator_next(&it, &tmp, false);
    Assert(itok == WAGT_KEY || WAGT_END_OBJECT);
    /* if this is the end of the object return NULL */
    if (itok == WAGT_END_OBJECT)
    {
        return NULL;
    }

    /* this should be the key, copy it */
    if (itok == WAGT_KEY)
    {
        memcpy(key, &tmp, sizeof(agtype_value));
    }

    /*
     * The next token should be a value but, it could be a begin tokens for
     * arrays or objects. For those we just return NULL to ignore them.
     */
    itok = agtype_iterator_next(&it, &tmp, true);
    Assert(itok == WAGT_VALUE);
    if (itok == WAGT_VALUE)
    {
        memcpy(value, &tmp, sizeof(agtype_value));
    }

    /* return the iterator */
    return it;
}

/*
 * Helper function to compare the edge conditions (properties we are looking
 * for in a matching edge) against an edge entry.
 *
 * Note: Currently the edge properties - to match - that are stored in the local
 *       context are of type agtype_value (in memory) while those from the edge
 *       are of agtype (on disk). This may change.
 */
static bool is_an_edge_match(VLE_local_context *vlelctx, edge_entry *ee)
{
    agtype_value *agtv_edge_conditions = NULL;
    agtype *agt_edge_properties = NULL;
    agtype_iterator *iterator = NULL;
    agtype_container *agtc_edge_properties = NULL;
    char *edge_label_name = NULL;
    int num_conditions = 0;
    int num_edge_properties = 0;
    int num_matches = 0;
    int property_index = 0;

    /* get the edge label name from the oid */
    edge_label_name = get_rel_name(ee->edge_label_table_oid);
    /* get our edge's properties */
    agt_edge_properties = DATUM_GET_AGTYPE_P(ee->edge_properties);
    /* get the container */
    agtc_edge_properties = &agt_edge_properties->root;
    /* for easier reading */
    agtv_edge_conditions = vlelctx->edge_conditions;

    /* get the number of conditions from the prototype edge */
    num_conditions = agtv_edge_conditions->val.object.num_pairs;
    /* get the number of properties in the edge to be matched */
    num_edge_properties = AGTYPE_CONTAINER_SIZE(agtc_edge_properties);

    /*
     * Check to see if the edge_properties object has AT LEAST as many pairs
     * to compare as the edge_conditions object has pairs. If not, it can't
     * possibly match.
     */
    if (num_conditions > num_edge_properties)
    {
        return false;
    }

    /*
     * Check for a label constraint. If the label name is NULL, there isn't one.
     */
    if (vlelctx->edge_label_name != NULL &&
        strcmp(vlelctx->edge_label_name, edge_label_name) != 0)
    {
        return false;
    }

    /*
     * We only care about verifying that we have all of the property conditions.
     * We don't care about extra unmatched properties. If there aren't any edge
     * conditions, then the edge passes by default.
     */
    if (num_conditions == 0)
    {
        return true;
    }

    /*
     * Iterate through the edge's properties, matching them against the
     * condtions required.
     */
    do
    {
        agtype_value edge_property_key = {0};
        agtype_value edge_property_value = {0};

        property_index++;

        /* get the next key/value pair from the edge_properties if it exists */
        iterator = get_next_object_pair(iterator, agtc_edge_properties,
                                        &edge_property_key,
                                        &edge_property_value);

        /* if there is a pair, see if the key is in the edge conditions */
        if (iterator != NULL)
        {
            agtype_value *condition_value = NULL;

            /* get the condition_value for the specified edge_property_key */
            condition_value = get_agtype_key(agtv_edge_conditions,
                                             edge_property_key.val.string.val,
                                             edge_property_key.val.string.len);

            /* if one exists, we have a key match */
            if (condition_value != NULL)
            {
                bool match = false;

                /* are they both scalars */
                if (IS_A_AGTYPE_SCALAR(condition_value) &&
                    IS_A_AGTYPE_SCALAR(&edge_property_value))
                {
                    match = compare_agtype_scalar_values(condition_value,
                                                         &edge_property_value)
                            == 0;
                }
                /* or are they both containers */
                else if (!IS_A_AGTYPE_SCALAR(condition_value) &&
                         !IS_A_AGTYPE_SCALAR(&edge_property_value))
                {
                    agtype *agt_condition = NULL;
                    agtype_container *agtc_condition = NULL;
                    agtype_container *agtc_property = NULL;

                    /* serialize this condition */
                    agt_condition = agtype_value_to_agtype(condition_value);

                    /* put them into containers for comparison */
                    agtc_condition = &agt_condition->root;
                    agtc_property = edge_property_value.val.binary.data;

                    /* check for an exact match */
                    if (compare_agtype_containers_orderability(agtc_condition,
                                                               agtc_property)
                        == 0)
                    {
                        match = true;
                    }
                }

                /* count matches */
                if (match)
                {
                    num_matches++;
                }
            }

            /* check to see if a match is no longer possible */
            if ((num_edge_properties - property_index) <
                (num_conditions - num_matches))
            {
                pfree(iterator);
                return false;
            }
        }
    }
    while (iterator != NULL);

    return true;
}

/*
 * Helper function to iterate through all object pairs, looking for a specific
 * key. It will return the key or NULL if not found.
 * TODO: what if the value is supposed to be NULL?
 */
static agtype_value *get_agtype_key(agtype_value *agtv, char *search_key,
                                    int search_key_len)
{
    int i;

    if (agtv == NULL || search_key == NULL || search_key_len <= 0)
        return NULL;

    /* iterate through all pairs */
    for (i = 0; i < agtv->val.object.num_pairs; i++)
    {
        agtype_value *agtv_key = &agtv->val.object.pairs[i].key;
        agtype_value *agtv_value = &agtv->val.object.pairs[i].value;

        char *current_key = agtv_key->val.string.val;
        int current_key_len = agtv_key->val.string.len;

        Assert(agtv_key->type == AGTV_STRING);

        /* check for an id of type integer */
        if (current_key_len == search_key_len &&
            pg_strcasecmp(current_key, search_key) == 0)
            return agtv_value;
    }

    return NULL;
}

/* helper function to get a List of all edge labels for the specified graph */
static List *get_edge_labels(Oid graph_oid, Snapshot snapshot)
{
    List *labels = NIL;
    ScanKeyData scan_keys[2];
    Relation ag_label;
    HeapScanDesc scan_desc;
    HeapTuple tuple;
    TupleDesc tupdesc;

    /* we need a valid snapshot */
    Assert(snapshot != NULL);

    /* setup scan keys to get all edges for the given graph oid */
    ScanKeyInit(&scan_keys[1], Anum_ag_label_graph, BTEqualStrategyNumber,
                F_OIDEQ, ObjectIdGetDatum(graph_oid));
    ScanKeyInit(&scan_keys[0], Anum_ag_label_kind, BTEqualStrategyNumber,
                F_CHAREQ, CharGetDatum(LABEL_TYPE_EDGE));

    /* setup the table to be scanned, ag_label in this case */
    ag_label = heap_open(ag_label_relation_id(), ShareLock);
    scan_desc = heap_beginscan(ag_label, snapshot, 2, scan_keys);

    /* get the tupdesc - we don't need to release this one */
    tupdesc = RelationGetDescr(ag_label);
    /* bail if the number of columns differs - this table has 5 */
    Assert(tupdesc->natts == 5);

    /* get all of the edge label names */
    while((tuple = heap_getnext(scan_desc, ForwardScanDirection)) != NULL)
    {
        Name label;

        /* something is wrong if this tuple isn't valid */
        Assert(HeapTupleIsValid(tuple));
        /* get the label name */
        label = DatumGetName(column_get_datum(tupdesc, tuple, 0, "name",
                                              NAMEOID, true));
        /* add it to our list */
        labels = lappend(labels, label);
    }

    /* close up scan */
    heap_endscan(scan_desc);
    heap_close(ag_label, ShareLock);

    return labels;
}

/*
 * Helper function to insert one edge/edge->vertex, key/value pair, in the
 * current global edge hashtable.
 */
static bool insert_edge(VLE_global_context *vlegctx, graphid edge_id,
                        Datum edge_properties, graphid start_vertex_id,
                        graphid end_vertex_id, Oid edge_label_table_oid)
{
    edge_entry *value = NULL;
    bool found = false;

    /* search for the edge */
    value = (edge_entry *)hash_search(vlegctx->edge_hashtable, (void *)&edge_id,
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
    vlegctx->num_loaded_edges++;

    return true;
}

/*
 * Helper function to insert one vertex/edge or to add one edge to an existing
 * vertex, key/value pair, in the current global vertex hashtable.
 */
static bool insert_vertex(VLE_global_context *vlegctx, graphid vertex_id,
                          graphid edge_id)
{
    vertex_entry *value = NULL;
    bool found = false;

    /* search for the vertex */
    value = (vertex_entry *)hash_search(vlegctx->vertex_hashtable,
                                      (void *)&vertex_id,
                                      HASH_ENTER, &found);

    /* if found, we need to append the edge id to the edges list. */
    if (found)
    {
        value->edges = append_graphid(value->edges, edge_id);
    }
    /* otherwise, we need to create a new value entry for this vertex */
    else
    {
        /* again, MemSet may not be needed here */
        MemSet(value, 0, sizeof(vertex_entry));

        /*
         * Set the vertex id - this is important as this is the hash key value
         * used for hash function collisions.
         */
        value->vertex_id = vertex_id;
        /* add in the edge id */
        value->edges = append_graphid(NULL, edge_id);
        /* we also need to store the vertex id for clean up of vertex lists */
        vlegctx->vertices = append_graphid(vlegctx->vertices, vertex_id);

        /* increment the number of loaded vertices */
        vlegctx->num_loaded_vertices++;
    }

    return true;
}

/*
 * Helper function to free up the memory used by the VLE_local_context.
 *
 * Currently, the only structures that needs to be freed are the edge state
 * hashtable and the dfs stacks (vertex, edge, and path). The hashtable is easy
 * because hash_create packages everything into its own memory context. So, you
 * only need to do a destroy.
 */
static void free_VLE_local_context(VLE_local_context *vlelctx)
{
    /* if the VLE context is NULL, do nothing */
    if (vlelctx == NULL)
    {
        return;
    }

    /* we need to free our state hashtable */
    hash_destroy(vlelctx->edge_state_hashtable);
    vlelctx->edge_state_hashtable = NULL;

    /* we need to free our stacks */
    free_stack(vlelctx->dfs_vertex_stack);
    free_stack(vlelctx->dfs_edge_stack);
    free_stack(vlelctx->dfs_path_stack);
    pfree(vlelctx->dfs_vertex_stack);
    pfree(vlelctx->dfs_edge_stack);
    pfree(vlelctx->dfs_path_stack);
    vlelctx->dfs_vertex_stack = NULL;
    vlelctx->dfs_edge_stack = NULL;
    vlelctx->dfs_path_stack = NULL;

    /* and finally the context itself */
    pfree(vlelctx);
}

/*
 * Helper function to load all of the VLE hashtables (vertex & edge) for the
 * current global context (graph).
 */
static void load_VLE_global_hashtables(VLE_global_context *vlegctx)
{
    Oid graph_oid;
    Oid graph_namespace_oid;
    Snapshot snapshot;
    List *edge_label_names = NIL;
    ListCell *lc;

    /* get the specific graph OID and namespace (schema) OID */
    graph_oid = vlegctx->graph_oid;
    graph_namespace_oid = get_namespace_oid(vlegctx->graph_name, false);
    /* get the active snapshot */
    snapshot = GetActiveSnapshot();
    /* get the names of all of the edge label tables */
    edge_label_names = get_edge_labels(graph_oid, snapshot);
    /* initialize statistics */
    vlegctx->num_loaded_vertices = 0;
    vlegctx->num_loaded_edges = 0;
    /* go through all edge label tables in list */
    foreach (lc, edge_label_names)
    {
        Relation graph_edge_label;
        HeapScanDesc scan_desc;
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
        graph_edge_label = heap_open(edge_label_table_oid, ShareLock);
        scan_desc = heap_beginscan(graph_edge_label, snapshot, 0, NULL);
        /* get the tupdesc - we don't need to release this one */
        tupdesc = RelationGetDescr(graph_edge_label);
        /* bail if the number of columns differs */
        if (tupdesc->natts != 4)
            ereport(ERROR,
                    (errcode(ERRCODE_UNDEFINED_TABLE),
                     errmsg("Invalid number of attributes for %s.%s",
                     vlegctx->graph_name, edge_label_name)));
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

            /* insert edge into edge hashtable */
            inserted = insert_edge(vlegctx, edge_id, edge_properties,
                                   edge_vertex_start_id, edge_vertex_end_id,
                                   edge_label_table_oid);
            /* this insert must not fail */
            if (!inserted)
            {
                 elog(ERROR, "insert_edge: failed to insert");
            }
            /*
             * Insert the vertex (start vertex in the edge) and the edge into
             * vertex hashtable.
             */
            inserted = insert_vertex(vlegctx, edge_vertex_start_id, edge_id);
            /* this insert must not fail */
            if (!inserted)
            {
                 elog(ERROR, "insert_vertex: failed to insert");
            }
            /*
             * Insert the vertex (end vertex in the edge) and the edge into
             * vertex hashtable. UNLESS this is a self loop (start == end
             * vertex) because that would be adding it twice.
             */
            if (edge_vertex_start_id != edge_vertex_end_id)
            {
                inserted = insert_vertex(vlegctx, edge_vertex_end_id, edge_id);
                /* this insert much not fail */
                if (!inserted)
                {
                     elog(ERROR, "insert_vertex: failed to insert");
                }
            }
        }

        /* end the scan and close the relation */
        heap_endscan(scan_desc);
        heap_close(graph_edge_label, ShareLock);
    }
}

/*
 * Helper function to freeze the VLE hashtables from additional inserts. This
 * may, or may not, be useful. Currently, these hashtables are only seen by the
 * creating process and only for reading.
 */
static void freeze_VLE_global_hashtables(VLE_global_context *vlegctx)
{
    hash_freeze(vlegctx->vertex_hashtable);
    hash_freeze(vlegctx->edge_hashtable);
}

/*
 * Helper function to free the entire specified context. After running this
 * you should not use the pointer in vlegctx.
 */
static void free_specific_global_context(VLE_global_context *vlegctx)
{
    GraphIdNode *curr_vertex = NULL;

    /* don't do anything if NULL */
    if (vlegctx == NULL)
    {
        return;
    }

    /* free the graph name */
    pfree(vlegctx->graph_name);

    /* free the vertex edge lists, starting with the head */
    curr_vertex = vlegctx->vertices->head;
    while (curr_vertex != NULL)
    {
        GraphIdNode *next_vertex = NULL;
        vertex_entry *value = NULL;
        bool found = false;
        graphid vertex_id;

        /* get the next vertex in the list, if any */
        next_vertex = curr_vertex->next;

        /* get the current vertex id */
        vertex_id = curr_vertex->id;

        /* retrieve the vertex entry */
        value = (vertex_entry *)hash_search(vlegctx->vertex_hashtable,
                                            (void *)&vertex_id, HASH_FIND,
                                            &found);
        /* this is bad if it isn't found */
        Assert(found);

        /* free the edge list associated with this vertex */
        free_ListGraphId(value->edges);

        /* move to the next vertex */
        curr_vertex = next_vertex;
    }

    /* free the vertices list */
    free_ListGraphId(vlegctx->vertices);

    /* free the hashtables */
    hash_destroy(vlegctx->vertex_hashtable);
    hash_destroy(vlegctx->edge_hashtable);

    /* free the context */
    pfree(vlegctx);
}

/*
 * Helper function to manage the VLE global contexts. It will create the context
 * for the graph specified, provided it isn't already build and valid. During
 * processing it will free (delete) all invalid contexts. It returns the global
 * context for the specified graph.
 */
static VLE_global_context *manage_VLE_global_contexts(char *graph_name,
                                                      Oid graph_oid)
{
    VLE_global_context *new_vlegctx = NULL;
    VLE_global_context *curr_vlegctx = NULL;
    VLE_global_context *prev_vlegctx = NULL;
    MemoryContext oldctx = NULL;

    /* we need a higher context, or one that isn't destroyed by SRF exit */
    oldctx = MemoryContextSwitchTo(TopMemoryContext);

    /*
     * We need to see if any global contexts already exist and if any do for
     * this particular graph. There are 5 possibilities -
     *
     *     1) There are no global contexts.
     *     2) One does exist for this graph but, is invalid.
     *     3) One does exist for this graph and is valid.
     *     4) One or more other contexts do exist and all are valid.
     *     5) One or more other contexts do exist but, one or more are invalid.
     */

    /* free the invalidated global contexts first */
    prev_vlegctx = NULL;
    curr_vlegctx = global_vle_contexts;
    while (curr_vlegctx != NULL)
    {
        VLE_global_context *next_vlegctx = curr_vlegctx->next;

        /* if the transaction ids have changed, we have an invalid graph */
        if (curr_vlegctx->xmin != GetActiveSnapshot()->xmin ||
            curr_vlegctx->xmax != GetActiveSnapshot()->xmax)
        {
            /*
             * If prev_vlegctx is NULL then we are freeing the top of the
             * contexts. So, we need to point the global variable to the
             * new (next) top context, if there is one.
             */
            if (prev_vlegctx == NULL)
            {
                global_vle_contexts = next_vlegctx;
            }
            else
            {
                prev_vlegctx->next = curr_vlegctx->next;
            }

            /* free the current graph context */
            free_specific_global_context(curr_vlegctx);
        }
        else
        {
            prev_vlegctx = curr_vlegctx;
        }

        /* advance to the next context */
        curr_vlegctx = next_vlegctx;
    }

    /* find our graph's context. if it exists, we are done */
    curr_vlegctx = global_vle_contexts;
    while (curr_vlegctx != NULL)
    {
        if (curr_vlegctx->graph_oid == graph_oid)
        {
            /* switch our context back */
            MemoryContextSwitchTo(oldctx);
            /* we are done */
            return curr_vlegctx;
        }
        curr_vlegctx = curr_vlegctx->next;
    }

    /* otherwise, we need to create one and possibly attach it */
    new_vlegctx = palloc0(sizeof(VLE_global_context));

    if (global_vle_contexts != NULL)
    {
        new_vlegctx->next = global_vle_contexts;
    }
    else
    {
        new_vlegctx->next = NULL;
    }

    /* set the global context variable */
    global_vle_contexts = new_vlegctx;

    /* set the graph name and oid */
    new_vlegctx->graph_name = pstrdup(graph_name);
    new_vlegctx->graph_oid = graph_oid;

    /* set the transaction ids */
    new_vlegctx->xmin = GetActiveSnapshot()->xmin;
    new_vlegctx->xmax = GetActiveSnapshot()->xmax;

    /* initialize our vertices list */
    new_vlegctx->vertices = NULL;

    /* build the hashtables for this graph */
    create_VLE_global_hashtables(new_vlegctx);
    load_VLE_global_hashtables(new_vlegctx);
    freeze_VLE_global_hashtables(new_vlegctx);

    /* switch back to the previous memory context */
    MemoryContextSwitchTo(oldctx);

    return new_vlegctx;
}

/* helper function to check if our start and end vertices exist */
static bool do_vsid_and_veid_exist(VLE_local_context *vlelctx)
{
    return ((get_vertex_entry(vlelctx, vlelctx->vsid) != NULL) &&
            (get_vertex_entry(vlelctx, vlelctx->veid) != NULL));
}

/* load the initial edges into the dfs_edge_stack */
static void load_initial_dfs_stacks(VLE_local_context *vlelctx)
{
    /*
     * If either the vsid or veid don't exist - don't load anything because
     * there won't be anything to find.
     */
    if (!do_vsid_and_veid_exist(vlelctx))
    {
        return;
    }

    /* add in the edges for the next vertex */
    add_valid_vertex_edges(vlelctx, vlelctx->vsid);
}

/*
 * Helper function to build the local VLE context. This is also the point
 * where, if necessary, the global contexts are created and freed.
 */
static VLE_local_context *build_local_vle_context(FunctionCallInfo fcinfo)
{
    VLE_global_context *vlegctx = NULL;
    VLE_local_context *vlelctx = NULL;
    agtype_value *agtv_temp = NULL;
    char *graph_name = NULL;
    Oid graph_oid;

    /* get the graph name */
    agtv_temp = get_agtype_value("age_vle", AG_GET_ARG_AGTYPE_P(0),
                                 AGTV_STRING);
    graph_name = pnstrdup(agtv_temp->val.string.val,
                          agtv_temp->val.string.len);
    /* get the graph oid */
    graph_oid = get_graph_oid(graph_name);

    /*
     * Create or retrieve the VLE global context for this graph. This function
     * will also purge off invalidated contexts.
    */
    vlegctx = manage_VLE_global_contexts(graph_name, graph_oid);

    /* allocate and initialize local VLE context */
    vlelctx = palloc0(sizeof(VLE_local_context));

    /* set the graph name and id */
    vlelctx->graph_name = graph_name;
    vlelctx->graph_oid = graph_oid;

    /* set the global context referenced by this local context */
    vlelctx->vlegctx = vlegctx;

    /* set the hashtables used by this context */
    vlelctx->vertex_hashtable = vlegctx->vertex_hashtable;
    vlelctx->edge_hashtable = vlegctx->edge_hashtable;

    /* get the start vertex id */
    agtv_temp = get_agtype_value("age_vle", AG_GET_ARG_AGTYPE_P(1),
                                 AGTV_VERTEX);
    agtv_temp = get_agtype_value_object_value(agtv_temp, "id");
    vlelctx->vsid = agtv_temp->val.int_value;

    /* get the end vertex id */
    agtv_temp = get_agtype_value("age_vle", AG_GET_ARG_AGTYPE_P(2),
                                 AGTV_VERTEX);
    agtv_temp = get_agtype_value_object_value(agtv_temp, "id");
    vlelctx->veid = agtv_temp->val.int_value;

    /* get the VLE edge prototype */
    agtv_temp = get_agtype_value("age_vle", AG_GET_ARG_AGTYPE_P(3), AGTV_EDGE);

    /* get the edge prototype's property conditions */
    vlelctx->edge_conditions = get_agtype_value_object_value(agtv_temp,
                                                             "properties");
    /* get the edge prototype's label name */
    agtv_temp = get_agtype_value_object_value(agtv_temp, "label");
    if (agtv_temp->type == AGTV_STRING &&
        agtv_temp->val.string.len != 0)
    {
        vlelctx->edge_label_name = pnstrdup(agtv_temp->val.string.val,
                                            agtv_temp->val.string.len);
    }
    else
    {
        vlelctx->edge_label_name = NULL;
    }

    /* get the left range index */
    agtv_temp = get_agtype_value("age_vle", AG_GET_ARG_AGTYPE_P(4),
                                 AGTV_INTEGER);
    vlelctx->lidx = agtv_temp->val.int_value;

    /* get the right range index. NULL means infinite */
    if (is_agtype_null(AG_GET_ARG_AGTYPE_P(5)))
    {
        vlelctx->uidx_infinite = true;
        vlelctx->uidx = 0;
    }
    else
    {
        agtv_temp = get_agtype_value("age_vle", AG_GET_ARG_AGTYPE_P(5),
                                     AGTV_INTEGER);
        vlelctx->uidx = agtv_temp->val.int_value;
        vlelctx->uidx_infinite = false;
    }
    /* get edge direction */
    agtv_temp = get_agtype_value("age_vle", AG_GET_ARG_AGTYPE_P(6),
                                 AGTV_INTEGER);
    vlelctx->edge_direction = agtv_temp->val.int_value;

    /* create the local state hashtable */
    create_VLE_local_state_hashtable(vlelctx);

    /* initialize the dfs stacks */
    vlelctx->dfs_vertex_stack = new_stack();
    vlelctx->dfs_edge_stack = new_stack();
    vlelctx->dfs_path_stack = new_stack();

    /* load in the starting edge(s) */
    load_initial_dfs_stacks(vlelctx);

    return vlelctx;
}

/*
 * Helper function to get the specified edge's state. If it does not find it, it
 * creates and initializes it.
 */
static edge_state_entry *get_edge_state(VLE_local_context *vlelctx,
                                        graphid edge_id)
{
    edge_state_entry *ese = NULL;
    bool found = false;

    /* retrieve the edge_state_entry from the edge state hashtable */
    ese = (edge_state_entry *)hash_search(vlelctx->edge_state_hashtable,
                                          (void *)&edge_id, HASH_ENTER, &found);

    /* if it isn't found, it needs to be created and initialized */
    if (!found)
    {
        MemSet(ese, 0, sizeof(edge_state_entry));

        /* the edge id is also the hash key for resolving collisions */
        ese->edge_id = edge_id;
        ese->used_in_path = false;
        ese->has_been_matched = false;
        ese->matched = false;
    }
    return ese;
}

/*
 * Helper function to retrieve a vertex_entry from the graph's vertex hash
 * table. If there isn't one, it returns a NULL. The latter is necessary for
 * checking if the vsid and veid entries exist.
 */
static vertex_entry *get_vertex_entry(VLE_local_context *vlelctx,
                                      graphid vertex_id)
{
    vertex_entry *ve = NULL;
    bool found = false;

    /* retrieve the current vertex entry */
    ve = (vertex_entry *)hash_search(vlelctx->vertex_hashtable,
                                     (void *)&vertex_id, HASH_FIND, &found);
    return ve;
}

/* helper function to retrieve an edge_entry from the graph's edge hash table */
static edge_entry *get_edge_entry(VLE_local_context *vlelctx, graphid edge_id)
{
    edge_entry *ee = NULL;
    bool found = false;

    /* retrieve the current edge entry */
    ee = (edge_entry *)hash_search(vlelctx->edge_hashtable, (void *)&edge_id,
                                   HASH_FIND, &found);
    /* it should be found, otherwise we have problems */
    Assert(found);

    return ee;
}

/* helper function to create a new, empty, stack */
static ListGraphId *new_stack(void)
{
    ListGraphId *stack = NULL;

    /* allocate the container for the stack */
    stack = palloc0(sizeof(ListGraphId));

    /* set it to its initial values */
    stack->head = NULL;
    stack->tail = NULL;
    stack->size = 0;

    /* return the new stack */
    return stack;
}

/* helper function to free a stack's contents but, not the container */
static void free_stack(ListGraphId *stack)
{
    Assert(stack != NULL);

    /* while there are entries */
    while (stack->head != NULL)
    {
        /* get the next element in the stack */
        GraphIdNode *next = stack->head->next;

        /* free the head element */
        pfree(stack->head);
        /* move the head to the next */
        stack->head = next;
    }

    /* reset the tail and size */
    stack->tail = NULL;
    stack->size = 0;
}

/*
 * Helper function for a generic push id (int64) to a stack. If the stack is
 * NULL, it will error out.
 */
static void push_stack(ListGraphId *stack, graphid id)
{
    GraphIdNode *new_node = NULL;

    Assert(stack != NULL);

    /* create the new element */
    new_node = palloc0(sizeof(GraphIdNode));
    new_node->id = id;
    new_node->next = NULL;

    /* insert (push) the new element on the top */
    new_node->next = stack->head;
    stack->head = new_node;
    stack->size++;
}

/*
 * Helper function for a generic pop id (int64) from a stack. If the stack is
 * empty, it will error out. You should verify that the stack isn't empty prior
 * to calling.
 */
static graphid pop_stack(ListGraphId *stack)
{
    GraphIdNode *node = NULL;
    graphid id;

    Assert(stack != NULL);
    Assert(stack->size != 0);

    /* remove the element from the top of the stack */
    node = stack->head;
    id = node->id;
    stack->head = stack->head->next;
    stack->size--;
    /* free the element */
    pfree(node);

    /* return the id */
    return id;
}

/*
 * Helper function for a generic peek from a stack. If the stack is empty, it
 * will error out. You should verify that the stack isn't empty prior to
 * calling.
 */
static graphid peek_stack(ListGraphId *stack)
{
    Assert(stack != NULL);
    Assert(stack->size != 0);

    /* return the id on the top of the stack */
    return stack->head->id;
}

/* helper function to see if a stack is empty */
static bool is_stack_empty(ListGraphId *stack)
{
    Assert(stack != NULL);

    return (stack->size == 0);
}

/*
 * Helper function to get the id of the next vertex to move to. This is to
 * simplify finding the next vertex due to the VLE edge's direction.
 */
static graphid get_next_vertex(VLE_local_context *vlelctx, edge_entry *ee)
{
    graphid terminal_vertex_id;

    /* get the result based on the specified VLE edge direction */
    switch (vlelctx->edge_direction)
    {
        case CYPHER_REL_DIR_RIGHT:
            terminal_vertex_id = ee->end_vertex_id;
            break;

        case CYPHER_REL_DIR_LEFT:
            terminal_vertex_id = ee->start_vertex_id;
            break;

        case CYPHER_REL_DIR_NONE:
        {
            ListGraphId *vertex_stack = NULL;
            graphid parent_vertex_id;

            vertex_stack = vlelctx->dfs_vertex_stack;
            /*
             * Get the parent vertex of this edge. When we are looking at edges
             * as un-directional, where we go to next depends on where we came
             * from. This is because we can go against an edge.
             */
            parent_vertex_id = peek_stack(vertex_stack);
            /* find the terminal vertex */
            if (ee->start_vertex_id == parent_vertex_id)
            {
                terminal_vertex_id = ee->end_vertex_id;
            }
            else if (ee->end_vertex_id == parent_vertex_id)
            {
                terminal_vertex_id = ee->start_vertex_id;
            }
            else
            {
                elog(ERROR, "get_next_vertex: no parent match");
            }

            break;
        }

        default:
            elog(ERROR, "get_next_vertex: unknown edge direction");
    }

    return terminal_vertex_id;
}

/*
 * Helper function to find one path.
 *
 * Note: On the very first entry into this function, the starting vertex's edges
 * should have already been loaded into the edge stack (this should have been
 * done by the SRF initialization phase).
 *
 * This function will always return on either a valid path found (true) or none
 * found (false). If one is found, the position (vertex & edge) will still be in
 * the stack. Each successive invocation within the SRF will then look for the
 * next available path until there aren't any left.
 */
static bool dfs_find_a_path(VLE_local_context *vlelctx)
{
    ListGraphId *vertex_stack = NULL;
    ListGraphId *edge_stack = NULL;
    ListGraphId *path_stack = NULL;
    graphid end_vertex_id;

    Assert(vlelctx != NULL);

    /* for ease of reading */
    vertex_stack = vlelctx->dfs_vertex_stack;
    edge_stack = vlelctx->dfs_edge_stack;
    path_stack = vlelctx->dfs_path_stack;
    end_vertex_id = vlelctx->veid;

    /* while we have edges to process */
    while (!is_stack_empty(edge_stack))
    {
        graphid edge_id;
        graphid next_vertex_id;
        edge_state_entry *ese = NULL;
        edge_entry *ee = NULL;
        bool found = false;

        /* get an edge, but leave it on the stack for now */
        edge_id = peek_stack(edge_stack);
        /* get the edge's state */
        ese = get_edge_state(vlelctx, edge_id);
        /*
         * If the edge is already in use, it means that the edge is in the path.
         * So, we need to see if it is the last path entry (we are backing up -
         * we need to remove the edge from the path stack and reset its state
         * and from the edge stack as we are done with it) or an interior edge
         * in the path (loop - we need to remove the edge from the edge stack
         * and start with the next edge).
         */
        if (ese->used_in_path)
        {
            graphid path_edge_id;

            /* get the edge id on the top of the path stack (last edge) */
            path_edge_id = peek_stack(path_stack);
            /*
             * If the ids are the same, we're backing up. So, remove it from the
             * path stack and reset used_in_path.
             */
            if (edge_id == path_edge_id)
            {
                pop_stack(path_stack);
                ese->used_in_path = false;
            }
            /* now remove it from the edge stack */
            pop_stack(edge_stack);
            /*
             * Remove its source vertex, if we are looking at edges as
             * un-directional. We only maintain the vertex stack when the
             * edge_direction is CYPHER_REL_DIR_NONE. This is to save space
             * and time.
             */
            if (vlelctx->edge_direction == CYPHER_REL_DIR_NONE)
            {
                pop_stack(vertex_stack);
            }
            /* move to the next edge */
            continue;
        }
        /*
         * Will the edge cause the path length to exceed the upper bounds when
         * added. If it does, we can skip it.
         */
        if (!vlelctx->uidx_infinite && path_stack->size >= vlelctx->uidx)
        {
            /* remove edge from edge_stack */
            pop_stack(edge_stack);

            /* make sure to remove it from the vertex stack if necessary */
            if (vlelctx->edge_direction == CYPHER_REL_DIR_NONE)
            {
                pop_stack(vertex_stack);
            }
            /* get next edge */
            continue;
        }
        /*
         * Otherwise, mark it and push it on the path stack. There is no need
         * to push it on the edge stack as it is already there.
         */
        ese->used_in_path = true;
        push_stack(path_stack, edge_id);

        /* now get the edge entry so we can get the next vertex to move to */
        ee = get_edge_entry(vlelctx, edge_id);
        next_vertex_id = get_next_vertex(vlelctx, ee);

        /*
         * Is this the end of a path that meets our requirements? Is its length
         * within the bounds specified?
         */
        if (next_vertex_id == end_vertex_id &&
            path_stack->size >= vlelctx->lidx &&
            (vlelctx->uidx_infinite || path_stack->size <= vlelctx->uidx))
        {
            /* we found one */
            found = true;
        }
        /*
         * If we have found the end vertex but, we are not within our upper
         * bounds, we need to back up. We still need to continue traversing
         * the graph if we aren't within our lower bounds, though.
         */
        if (next_vertex_id == end_vertex_id &&
            !vlelctx->uidx_infinite &&
            path_stack->size > vlelctx->uidx)
        {
            continue;
        }
        /*
         * If the path is now at its max length, we can't go down this path any
         * further. So, we need to back up. By continuing now, we leave it in a
         * state that will force a back up.
         */
        if (!found &&
            !vlelctx->uidx_infinite &&
            path_stack->size >= vlelctx->uidx)
        {
            continue;
        }

        /* add in the edges for the next vertex */
        add_valid_vertex_edges(vlelctx, next_vertex_id);

        if (found)
        {
            return found;
        }
    }

    return false;
}

/*
 * Helper function to add in valid vertex edges as part of the dfs path
 * algorithm. What constitutes a valid edge is the following -
 *
 *     1) Edge matches the correct direction specified.
 *     2) Edge is not currently in the path.
 *     3) Edge matches minimum edge properties specified.
 *
 * Note: The vertex must exist.
 * Note: Edge lists contain both entering and exiting edges. The direction
 *       determines which is followed.
 */
static void add_valid_vertex_edges(VLE_local_context *vlelctx,
                                   graphid vertex_id)
{
    ListGraphId *vertex_stack = NULL;
    ListGraphId *edge_stack = NULL;
    vertex_entry *ve = NULL;
    GraphIdNode *edge = NULL;

    /* get the vertex entry */
    ve = get_vertex_entry(vlelctx, vertex_id);
    /* there better be a valid vertex */
    Assert(ve != NULL);
    /* point to stacks */
    vertex_stack = vlelctx->dfs_vertex_stack;
    edge_stack = vlelctx->dfs_edge_stack;
    /* get a pointer to the first edge */
    if (ve->edges != NULL)
    {
        edge = ve->edges->head;
    }
    else
    {
        edge = NULL;
    }
    /* add in the vertex's edge(s) */
    while (edge != NULL)
    {
        edge_entry *ee = NULL;
        edge_state_entry *ese = NULL;
        graphid edge_id;

        /* get the edge_id */
        edge_id = edge->id;
        /* get the edge entry */
        ee = get_edge_entry(vlelctx, edge_id);
        /* it better exist */
        Assert(ee != NULL);

        /* is the edge going correct direction */
        if ((vlelctx->edge_direction == CYPHER_REL_DIR_NONE) ||
            (vertex_id == ee->start_vertex_id &&
             vlelctx->edge_direction == CYPHER_REL_DIR_RIGHT) ||
            (vertex_id == ee->end_vertex_id &&
             vlelctx->edge_direction == CYPHER_REL_DIR_LEFT))
        {
            /* get its state */
            ese = get_edge_state(vlelctx, edge_id);
            /*
             * Don't add any edges that we have already seen because they will
             * cause a loop to form.
             */
            if (!ese->used_in_path)
            {
                /* validate the edge if it hasn't been already */
                if (!ese->has_been_matched && is_an_edge_match(vlelctx, ee))
                {
                    ese->has_been_matched = true;
                    ese->matched = true;
                }
                else if (!ese->has_been_matched)
                {
                    ese->has_been_matched = true;
                    ese->matched = false;
                }
                /* if it is a match, add it */
                if (ese->has_been_matched && ese->matched)
                {
                    /*
                     * We need to maintain our source vertex for each edge added
                     * if the edge_direction is CYPHER_REL_DIR_NONE. This is due
                     * to the edges having a fixed direction and the dfs
                     * algorithm working strictly through edges. With an
                     * un-directional VLE edge, you don't know the vertex that
                     * you just came from. So, we need to store it.
                     */
                    if (vlelctx->edge_direction == CYPHER_REL_DIR_NONE)
                    {
                        push_stack(vertex_stack, ve->vertex_id);
                    }
                    push_stack(edge_stack, edge_id);
                }
            }
        }
        edge = edge->next;
    }
}

/*
 * Helper function to build a palloc'd graphid array from the path_stack.
 *
 * Note: Remember to pfree it when done. Even though it should be destroyed on
 *       exiting the SRF context.
 */
static graphid *build_graphid_array_from_path_stack(ListGraphId *stack)
{
    graphid *graphid_array = NULL;
    GraphIdNode *edge = NULL;
    int index = 0;
    int ssize = 0;

    if (stack == NULL)
    {
        return NULL;
    }

    /* allocate the graphid array */
    ssize = stack->size;
    graphid_array = palloc0(sizeof(graphid) * ssize);

    /* get the head of the stack */
    edge = stack->head;

    /*
     * We need to fill in the array from the back to the front. This is due
     * to the order of the path stack - last in first out.
     */
    index = ssize - 1;

    /* copy while we have an edge to copy */
    while (edge != NULL)
    {
        graphid_array[index] = edge->id;
        edge = edge->next;

        index--;
    }

    return graphid_array;
}

/*
 * Helper function to create and build an array of agtype_value edges from an
 * array of graphids.
 *
 * Note: You should free the array when done. Although, it should be freed
 *       when the context is destroyed from the return of the SRF call.
 */
static agtype_value **build_edge_agtype_value_array(VLE_local_context *vlelctx,
                                                    graphid *gida, int size)
{
    agtype_value **agtv_edge_array = NULL;
    agtype_value *agtv_edge = NULL;
    int index = 0;

    /* allocate our agtv_edge_array */
    agtv_edge_array = palloc0(sizeof(agtype_value *) * size);

    /* build array of datums of agtype edge */
    for (index = 0; index < size; index++)
    {
        char *label_name = NULL;
        edge_entry *ee = NULL;

        /* get the edge entry from the hashtable */
        ee = get_edge_entry(vlelctx, gida[index]);
        /* get the label name from the oid */
        label_name = get_rel_name(ee->edge_label_table_oid);
        /* reconstruct the edge */
        agtv_edge = agtype_value_build_edge(ee->edge_id, label_name,
                                            ee->end_vertex_id,
                                            ee->start_vertex_id,
                                            ee->edge_properties);
        /* add it to the array*/
        agtv_edge_array[index] = agtv_edge;
    }

    return agtv_edge_array;
}

/*
 * VLE function that takes the following input and returns a row called edges of
 * type agtype -
 *
 *     0 - agtype (graph name as string)
 *     1 - agtype (start vertex as a vertex)
 *     2 - agtype (end vertex as a vertex)
 *     3 - agtype (edge prototype to match as an edge)
 *                 Note: Only the label and properties are used. The
 *                       rest is ignored.
 *     4 - agtype lidx (lower range index)
 *                 Note: 0 is not supported but is equivalent to 1.
 *     5 - agtype uidx (upper range index)
 *                 Note: An AGTV_NULL is appropriate here for an
 *                       infinite upper bound.
 *     6 - agtype edge direction (enum) as an integer -
 *
 *               CYPHER_REL_DIR_NONE = 0,
 *               CYPHER_REL_DIR_LEFT = -1,
 *               CYPHER_REL_DIR_RIGHT = 1
 *
 *               NOTE: The numbers provided in the enum are strictly for
 *                     debugging purposes and will be removed.
 *
 * This is a set returning function. This means that the first call sets
 * up the initial structures and then outputs the first row. After that each
 * subsequent call generates one row of output data. PG will continue to call
 * the function until the function tells PG that it doesn't have any more rows
 * to output. At that point, the function needs to clean up all of its data
 * structures that are not meant to last between SRFs.
 */
PG_FUNCTION_INFO_V1(age_vle);

Datum age_vle(PG_FUNCTION_ARGS)
{
    FuncCallContext *funcctx;
    VLE_local_context *vlelctx = NULL;
    bool found_a_path = false;
    MemoryContext oldctx;

    /* stuff done only on the first call of the function */
    if (SRF_IS_FIRSTCALL())
    {
        /* all these need to be non NULL arguments */
        if (PG_ARGISNULL(0) || PG_ARGISNULL(1) || PG_ARGISNULL(2) ||
            PG_ARGISNULL(3) || PG_ARGISNULL(4) || PG_ARGISNULL(6))
        {
             ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("All arguments to age_vle must be non NULL")));
        }

        /* create a function context for cross-call persistence */
        funcctx = SRF_FIRSTCALL_INIT();

        /* switch to memory context appropriate for multiple function calls */
        oldctx = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

        /* build the local vle context */
        vlelctx = build_local_vle_context(fcinfo);

        /*
         * Point the function call context's user pointer to the local VLE
         * context just created
         */
        funcctx->user_fctx = vlelctx;

        MemoryContextSwitchTo(oldctx);
    }

    /* stuff done on every call of the function */
    funcctx = SRF_PERCALL_SETUP();

    /* restore our VLE local context */
    vlelctx = (VLE_local_context *)funcctx->user_fctx;

    /*
     * All work done in dfs_find_a_path needs to be done in a context that
     * survives multiple SRF calls. So switch to the appropriate context.
     */
    oldctx = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

    /* find one path */
    found_a_path = dfs_find_a_path(vlelctx);

    /* switch back to a more volatile context */
    MemoryContextSwitchTo(oldctx);

    /*
     * If we find a path, we need to convert the path_stack into a list that
     * the outside world can use.
     */
    if (found_a_path)
    {
        Datum result;
        graphid *gida = NULL;
        agtype_value **ava;
        int gidasize = 0;

        /* the path_stack should have something in it if we have a path */
        Assert(vlelctx->dfs_path_stack > 0);

        /*
         * Build the graphid array from the path_stack. This will also correct
         * for the path_stack being last in, first out.
         */
        gida = build_graphid_array_from_path_stack(vlelctx->dfs_path_stack);
        gidasize = vlelctx->dfs_path_stack->size;

        /* build an array of edges of agtype_value from our array of graphids */
        ava = build_edge_agtype_value_array(vlelctx, gida, gidasize);

        /*
         * Build an agtype_value array of agtype_value edges and return it as a
         * datum
         */
        result = build_agtype_value_array_of_agtype_value_edges(ava, gidasize);

        /* free both the graphid and agtype edge arrays */
        pfree(gida);
        pfree(ava);

        /* return the result and signal that the function is not yet done */
        SRF_RETURN_NEXT(funcctx, result);
    }
    /* otherwise, we are done and we need to cleanup and signal done */
    else
    {
        /* free the local context */
        free_VLE_local_context(vlelctx);

        /* signal that we are done */
        SRF_RETURN_DONE(funcctx);
    }
}
