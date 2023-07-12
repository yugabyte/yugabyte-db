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
#include "access/htup_details.h"
#include "access/xact.h"
#include "executor/tuptable.h"
#include "nodes/execnodes.h"
#include "nodes/extensible.h"
#include "nodes/nodes.h"
#include "nodes/plannodes.h"
#include "rewrite/rewriteHandler.h"
#include "storage/bufmgr.h"
#include "utils/rel.h"

#include "executor/cypher_executor.h"
#include "executor/cypher_utils.h"
#include "nodes/cypher_nodes.h"
#include "utils/agtype.h"
#include "utils/graphid.h"

static void begin_cypher_set(CustomScanState *node, EState *estate,
                                int eflags);
static TupleTableSlot *exec_cypher_set(CustomScanState *node);
static void end_cypher_set(CustomScanState *node);
static void rescan_cypher_set(CustomScanState *node);

static void process_update_list(CustomScanState *node);
static HeapTuple update_entity_tuple(ResultRelInfo *resultRelInfo,
                                     TupleTableSlot *elemTupleSlot,
                                     EState *estate, HeapTuple old_tuple);

const CustomExecMethods cypher_set_exec_methods = {SET_SCAN_STATE_NAME,
                                                      begin_cypher_set,
                                                      exec_cypher_set,
                                                      end_cypher_set,
                                                      rescan_cypher_set,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL};

static void begin_cypher_set(CustomScanState *node, EState *estate,
                             int eflags)
{
    cypher_set_custom_scan_state *css =
        (cypher_set_custom_scan_state *)node;
    Plan *subplan;

    Assert(list_length(css->cs->custom_plans) == 1);

    subplan = linitial(css->cs->custom_plans);
    node->ss.ps.lefttree = ExecInitNode(subplan, estate, eflags);

    ExecAssignExprContext(estate, &node->ss.ps);

    ExecInitScanTupleSlot(estate, &node->ss,
                          ExecGetResultType(node->ss.ps.lefttree),
                          &TTSOpsHeapTuple);

    if (!CYPHER_CLAUSE_IS_TERMINAL(css->flags))
    {
        TupleDesc tupdesc = node->ss.ss_ScanTupleSlot->tts_tupleDescriptor;

        ExecAssignProjectionInfo(&node->ss.ps, tupdesc);
    }

    /*
     * Postgres does not assign the es_output_cid in queries that do
     * not write to disk, ie: SELECT commands. We need the command id
     * for our clauses, and we may need to initialize it. We cannot use
     * GetCurrentCommandId because there may be other cypher clauses
     * that have modified the command id.
     */
    if (estate->es_output_cid == 0)
    {
        estate->es_output_cid = estate->es_snapshot->curcid;
    }

    Increment_Estate_CommandId(estate);
}

static HeapTuple update_entity_tuple(ResultRelInfo *resultRelInfo,
                                     TupleTableSlot *elemTupleSlot,
                                     EState *estate, HeapTuple old_tuple)
{
    HeapTuple tuple = NULL;
    LockTupleMode lockmode;
    TM_FailureData hufd;
    TM_Result lock_result;
    Buffer buffer;
    bool update_indexes;
    TM_Result   result;
    CommandId cid = GetCurrentCommandId(true);
    ResultRelInfo *saved_resultRelInfo = estate->es_result_relation_info;

    estate->es_result_relation_info = resultRelInfo;

    lockmode = ExecUpdateLockMode(estate, resultRelInfo);

    lock_result = heap_lock_tuple(resultRelInfo->ri_RelationDesc, old_tuple,
                                  GetCurrentCommandId(false), lockmode,
                                  LockWaitBlock, false, &buffer, &hufd);

    if (lock_result == TM_Ok)
    {
        ExecOpenIndices(resultRelInfo, false);
        ExecStoreVirtualTuple(elemTupleSlot);
        tuple = ExecFetchSlotHeapTuple(elemTupleSlot, true, NULL);
        tuple->t_self = old_tuple->t_self;

        // Check the constraints of the tuple
        tuple->t_tableOid = RelationGetRelid(resultRelInfo->ri_RelationDesc);
        if (resultRelInfo->ri_RelationDesc->rd_att->constr != NULL)
        {
            ExecConstraints(resultRelInfo, elemTupleSlot, estate);
        }

        result = table_tuple_update(resultRelInfo->ri_RelationDesc,
                                    &tuple->t_self, elemTupleSlot,
                                    cid, estate->es_snapshot,
                                    estate->es_crosscheck_snapshot,
                                    true /* wait for commit */ ,
                                    &hufd, &lockmode, &update_indexes);

        if (result == TM_SelfModified)
        {
            if (hufd.cmax != cid)
            {
                ereport(ERROR,
                        (errcode(ERRCODE_TRIGGERED_DATA_CHANGE_VIOLATION),
                         errmsg("tuple to be updated was already modified")));
            }

            ExecCloseIndices(resultRelInfo);
            estate->es_result_relation_info = saved_resultRelInfo;

            return tuple;
        }

        if (result != TM_Ok)
        {
            ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                    errmsg("Entity failed to be updated: %i", result)));
        }

        // Insert index entries for the tuple
        if (resultRelInfo->ri_NumIndices > 0 && update_indexes)
        {
          ExecInsertIndexTuples(elemTupleSlot, estate, false, NULL, NIL);
        }

        ExecCloseIndices(resultRelInfo);
    }
    else if (lock_result == TM_SelfModified)
    {
        if (hufd.cmax != cid)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_TRIGGERED_DATA_CHANGE_VIOLATION),
                     errmsg("tuple to be updated was already modified")));
        }
    }
    else
    {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                errmsg("Entity failed to be updated: %i", lock_result)));
    }

    ReleaseBuffer(buffer);

    estate->es_result_relation_info = saved_resultRelInfo;

    return tuple;
}

/*
 * When the CREATE clause is the last cypher clause, consume all input from the
 * previous clause(s) in the first call of exec_cypher_create.
 */
static void process_all_tuples(CustomScanState *node)
{
    cypher_set_custom_scan_state *css = (cypher_set_custom_scan_state *)node;
    TupleTableSlot *slot;
    EState *estate = css->css.ss.ps.state;

    do
    {
        process_update_list(node);
        Decrement_Estate_CommandId(estate)
        slot = ExecProcNode(node->ss.ps.lefttree);
        Increment_Estate_CommandId(estate)
    } while (!TupIsNull(slot));
}

/*
 * Checks the path to see if the entities contained within
 * have the same graphid and the updated_id field. Returns
 * true if yes, false otherwise.
 */
static bool check_path(agtype_value *path, graphid updated_id)
{
    int i;

    for (i = 0; i < path->val.array.num_elems; i++)
    {
        agtype_value *elem = &path->val.array.elems[i];

        agtype_value *id = GET_AGTYPE_VALUE_OBJECT_VALUE(elem, "id");

        if (updated_id == id->val.int_value)
        {
            return true;
        }
    }

    return false;
}

/*
 * Construct a new agtype path with the entity with updated_id
 * replacing all of its instances in path with updated_entity
 */
static agtype_value *replace_entity_in_path(agtype_value *path,
                                            graphid updated_id,
                                            agtype *updated_entity)
{
    agtype_iterator *it;
    agtype_iterator_token tok = WAGT_DONE;
    agtype_parse_state *parse_state = NULL;
    agtype_value *r;
    agtype_value *parsed_agtype_value = NULL;
    agtype *prop_agtype;
    int i;

    r = palloc(sizeof(agtype_value));

    prop_agtype = agtype_value_to_agtype(path);
    it = agtype_iterator_init(&prop_agtype->root);
    tok = agtype_iterator_next(&it, r, true);

    parsed_agtype_value = push_agtype_value(&parse_state, tok,
                                            tok < WAGT_BEGIN_ARRAY ? r : NULL);

    // Iterate through the path, replace entities as necessary.
    for (i = 0; i < path->val.array.num_elems; i++)
    {
        agtype_value *id, *elem;

        elem = &path->val.array.elems[i];

        // something unexpected happened, throw an error.
        if (elem->type != AGTV_VERTEX && elem->type != AGTV_EDGE)
        {
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                            errmsg("unsupported agtype found in a path")));
        }

        // extract the id field
        id = GET_AGTYPE_VALUE_OBJECT_VALUE(elem, "id");

        /*
         * Either replace or keep the entity in the new path, depending on the id
         * check.
         */
        if (updated_id == id->val.int_value)
        {
            parsed_agtype_value = push_agtype_value(&parse_state, WAGT_ELEM,
                get_ith_agtype_value_from_container(&updated_entity->root, 0));
        }
        else
        {
            parsed_agtype_value = push_agtype_value(&parse_state, WAGT_ELEM,
                                                    elem);
        }
    }

    parsed_agtype_value = push_agtype_value(&parse_state, WAGT_END_ARRAY, NULL);
    parsed_agtype_value->type = AGTV_PATH;

    return parsed_agtype_value;
}

/*
 * When a vertex or edge is updated, we need to update the vertex
 * or edge if it is contained within a path. Scan through scanTupleSlot
 * to find all paths and check if they need to be updated.
 */
static void update_all_paths(CustomScanState *node, graphid id,
                             agtype *updated_entity)
{
    cypher_set_custom_scan_state *css = (cypher_set_custom_scan_state *)node;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    TupleTableSlot *scanTupleSlot = econtext->ecxt_scantuple;
    int i;

    for (i = 0; i < scanTupleSlot->tts_tupleDescriptor->natts; i++)
    {
        agtype *original_entity;
        agtype_value *original_entity_value;

        // skip nulls
        if (scanTupleSlot->tts_tupleDescriptor->attrs[i].atttypid != AGTYPEOID)
        {
            continue;
        }

        // skip non agtype values
        if (scanTupleSlot->tts_isnull[i])
        {
            continue;
        }

        original_entity = DATUM_GET_AGTYPE_P(scanTupleSlot->tts_values[i]);

        // if the value is not a scalar type, its not a path
        if (!AGTYPE_CONTAINER_IS_SCALAR(&original_entity->root))
        {
            continue;
        }

        original_entity_value = get_ith_agtype_value_from_container(&original_entity->root, 0);

        // we found a path
        if (original_entity_value->type == AGTV_PATH)
        {
            // check if the path contains the entity.
            if (check_path(original_entity_value, id))
            {
                // the path does contain the entity replace with the new entity.
                agtype_value *new_path = replace_entity_in_path(original_entity_value, id, updated_entity);

                scanTupleSlot->tts_values[i] = AGTYPE_P_GET_DATUM(agtype_value_to_agtype(new_path));
            }
        }
    }
}

static void process_update_list(CustomScanState *node)
{
    cypher_set_custom_scan_state *css = (cypher_set_custom_scan_state *)node;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    TupleTableSlot *scanTupleSlot = econtext->ecxt_scantuple;
    ListCell *lc;
    EState *estate = css->css.ss.ps.state;
    int *luindex = NULL;
    int lidx = 0;

    /* allocate an array to hold the last update index of each 'entity' */
    luindex = palloc0(sizeof(int) * scanTupleSlot->tts_nvalid);
    /*
     * Iterate through the SET items list and store the loop index of each
     * 'entity' update. As there is only one entry for each entity, this will
     * have the effect of overwriting the previous loop index stored - if this
     * 'entity' is used more than once. This will create an array of the last
     * loop index for the update of that particular 'entity'. This will allow us
     * to correctly update an 'entity' after all other previous updates to that
     * 'entity' have been done.
     */
    foreach (lc, css->set_list->set_items)
    {
        cypher_update_item *update_item = NULL;

        update_item = (cypher_update_item *)lfirst(lc);
        luindex[update_item->entity_position - 1] = lidx;

        /* increment the loop index */
        lidx++;
    }

    /* reset loop index */
    lidx = 0;

    /* iterate through SET set items */
    foreach (lc, css->set_list->set_items)
    {
        agtype_value *altered_properties;
        agtype_value *original_entity_value;
        agtype_value *original_properties;
        agtype_value *id;
        agtype_value *label;
        agtype *original_entity;
        agtype *new_property_value;
        TupleTableSlot *slot;
        ResultRelInfo *resultRelInfo;
        ScanKeyData scan_keys[1];
        TableScanDesc scan_desc;
        bool remove_property;
        char *label_name;
        cypher_update_item *update_item;
        Datum new_entity;
        HeapTuple heap_tuple;
        char *clause_name = css->set_list->clause_name;
        int cid;

        update_item = (cypher_update_item *)lfirst(lc);

        /*
         * If the entity is null, we can skip this update. this will be
         * possible when the OPTIONAL MATCH clause is implemented.
         */
        if (scanTupleSlot->tts_isnull[update_item->entity_position - 1])
        {
            continue;
        }

        if (scanTupleSlot->tts_tupleDescriptor->attrs[update_item->entity_position -1].atttypid != AGTYPEOID)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("age %s clause can only update agtype",
                            clause_name)));
        }

        original_entity = DATUM_GET_AGTYPE_P(scanTupleSlot->tts_values[update_item->entity_position - 1]);
        original_entity_value = get_ith_agtype_value_from_container(&original_entity->root, 0);

        if (original_entity_value->type != AGTV_VERTEX &&
            original_entity_value->type != AGTV_EDGE)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("age %s clause can only update vertex and edges",
                            clause_name)));
        }

        /* get the id and label for later */
        id = GET_AGTYPE_VALUE_OBJECT_VALUE(original_entity_value, "id");
        label = GET_AGTYPE_VALUE_OBJECT_VALUE(original_entity_value, "label");

        label_name = pnstrdup(label->val.string.val, label->val.string.len);
        /* get the properties we need to update */
        original_properties = GET_AGTYPE_VALUE_OBJECT_VALUE(original_entity_value,
                                                            "properties");

        /*
         * Determine if the property should be removed. This will be because
         * this is a REMOVE clause or the variable references a variable that is
         * NULL. It will be possible for a variable to be NULL when OPTIONAL
         * MATCH is implemented.
         */
        if (update_item->remove_item)
        {
            remove_property = true;
        }
        else
        {
            remove_property = scanTupleSlot->tts_isnull[update_item->prop_position - 1];
        }

        /*
         * If we need to remove the property, set the value to NULL. Otherwise
         * fetch the evaluated expression from the tuple slot.
         */
        if (remove_property)
        {
            new_property_value = NULL;
        }
        else
        {
            new_property_value = DATUM_GET_AGTYPE_P(scanTupleSlot->tts_values[update_item->prop_position - 1]);
        }

        // Alter the properties Agtype value.
        if (strcmp(update_item->prop_name, ""))
        {
            altered_properties = alter_property_value(original_properties,
                                                      update_item->prop_name,
                                                      new_property_value,
                                                      remove_property);
        }
        else
        {
            altered_properties = alter_properties(
                update_item->is_add ? original_properties : NULL,
                new_property_value);

            /*
             * For SET clause with plus-equal operator, nulls are not removed
             * from the map during transformation because they are required in
             * the executor to alter (merge) properties correctly. Only after
             * that step, they can be removed.
             */
            if (update_item->is_add)
            {
                remove_null_from_agtype_object(altered_properties);
            }
        }

        resultRelInfo = create_entity_result_rel_info(
            estate, css->set_list->graph_name, label_name);

        slot = ExecInitExtraTupleSlot(
            estate, RelationGetDescr(resultRelInfo->ri_RelationDesc),
            &TTSOpsHeapTuple);

        /*
         *  Now that we have the updated properties, create a either a vertex or
         *  edge Datum for the in-memory update, and setup the tupleTableSlot
         *  for the on-disc update.
         */
        if (original_entity_value->type == AGTV_VERTEX)
        {
            new_entity = make_vertex(GRAPHID_GET_DATUM(id->val.int_value),
                                     CStringGetDatum(label_name),
                                     AGTYPE_P_GET_DATUM(agtype_value_to_agtype(altered_properties)));

            slot = populate_vertex_tts(slot, id, altered_properties);
        }
        else if (original_entity_value->type == AGTV_EDGE)
        {
            agtype_value *startid = GET_AGTYPE_VALUE_OBJECT_VALUE(original_entity_value, "start_id");
            agtype_value *endid = GET_AGTYPE_VALUE_OBJECT_VALUE(original_entity_value, "end_id");

            new_entity = make_edge(GRAPHID_GET_DATUM(id->val.int_value),
                                   GRAPHID_GET_DATUM(startid->val.int_value),
                                   GRAPHID_GET_DATUM(endid->val.int_value),
                                   CStringGetDatum(label_name),
                                   AGTYPE_P_GET_DATUM(agtype_value_to_agtype(altered_properties)));

            slot = populate_edge_tts(slot, id, startid, endid,
                                     altered_properties);
        }
        else
        {
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                            errmsg("age %s clause can only update vertex and edges",
                                   clause_name)));
        }

        /* place the datum in its tuple table slot position. */
        scanTupleSlot->tts_values[update_item->entity_position - 1] = new_entity;

        /*
         * If the tuple table slot has paths, we need to inspect them to see if
         * the updated entity is contained within them and replace the entity
         * if it is.
         */
        update_all_paths(node,
                         id->val.int_value, DATUM_GET_AGTYPE_P(new_entity));

        /*
         * If the last update index for the entity is equal to the current loop
         * index, then update this tuple.
         */
        cid = estate->es_snapshot->curcid;
        estate->es_snapshot->curcid = GetCurrentCommandId(false);

        if (luindex[update_item->entity_position - 1] == lidx)
        {
            /*
             * Setup the scan key to require the id field on-disc to match the
             * entity's graphid.
             */
            ScanKeyInit(&scan_keys[0], 1, BTEqualStrategyNumber, F_GRAPHIDEQ,
                        GRAPHID_GET_DATUM(id->val.int_value));
            /*
             * Setup the scan description, with the correct snapshot and scan
             * keys.
             */
            scan_desc = table_beginscan(resultRelInfo->ri_RelationDesc,
                                        estate->es_snapshot, 1, scan_keys);
            /* Retrieve the tuple. */
            heap_tuple = heap_getnext(scan_desc, ForwardScanDirection);

            /*
             * If the heap tuple still exists (It wasn't deleted between the
             * match and this SET/REMOVE) update the heap_tuple.
             */
            if (HeapTupleIsValid(heap_tuple))
            {
                heap_tuple = update_entity_tuple(resultRelInfo, slot, estate,
                                                 heap_tuple);
            }
            /* close the ScanDescription */
            table_endscan(scan_desc);
        }

        estate->es_snapshot->curcid = cid;
        /* close relation */
        ExecCloseIndices(resultRelInfo);
        table_close(resultRelInfo->ri_RelationDesc, RowExclusiveLock);

        /* increment loop index */
        lidx++;
    }
    /* free our lookup array */
    pfree(luindex);
}

static TupleTableSlot *exec_cypher_set(CustomScanState *node)
{
    cypher_set_custom_scan_state *css = (cypher_set_custom_scan_state *)node;
    ResultRelInfo *saved_resultRelInfo;
    EState *estate = css->css.ss.ps.state;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    TupleTableSlot *slot;

    saved_resultRelInfo = estate->es_result_relation_info;

    //Process the subtree first
    Decrement_Estate_CommandId(estate);
    slot = ExecProcNode(node->ss.ps.lefttree);
    Increment_Estate_CommandId(estate);

    if (TupIsNull(slot))
    {
        return NULL;
    }

    econtext->ecxt_scantuple =
        node->ss.ps.lefttree->ps_ProjInfo->pi_exprContext->ecxt_scantuple;

    if (CYPHER_CLAUSE_IS_TERMINAL(css->flags))
    {
        estate->es_result_relation_info = saved_resultRelInfo;

        process_all_tuples(node);

        /* increment the command counter to reflect the updates */
        CommandCounterIncrement();

        return NULL;
    }

    process_update_list(node);

    /* increment the command counter to reflect the updates */
    CommandCounterIncrement();

    estate->es_result_relation_info = saved_resultRelInfo;

    econtext->ecxt_scantuple = ExecProject(node->ss.ps.lefttree->ps_ProjInfo);

    return ExecProject(node->ss.ps.ps_ProjInfo);
}

static void end_cypher_set(CustomScanState *node)
{
    ExecEndNode(node->ss.ps.lefttree);
}

static void rescan_cypher_set(CustomScanState *node)
{
    cypher_set_custom_scan_state *css = (cypher_set_custom_scan_state *)node;
    char *clause_name = css->set_list->clause_name;

     ereport(ERROR,
             (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                      errmsg("cypher %s clause cannot be rescanned",
                             clause_name),
                      errhint("its unsafe to use joins in a query with a Cypher %s clause", clause_name)));
}

Node *create_cypher_set_plan_state(CustomScan *cscan)
{
    cypher_set_custom_scan_state *cypher_css = palloc0(sizeof(cypher_set_custom_scan_state));
    cypher_update_information *set_list;
    char *serialized_data;
    Const *c;

    cypher_css->cs = cscan;

    // get the serialized data structure from the Const and deserialize it.
    c = linitial(cscan->custom_private);
    serialized_data = (char *)c->constvalue;
    set_list = stringToNode(serialized_data);

    Assert(is_ag_node(set_list, cypher_update_information));

    cypher_css->set_list = set_list;
    cypher_css->flags = set_list->flags;

    cypher_css->css.ss.ps.type = T_CustomScanState;
    cypher_css->css.methods = &cypher_set_exec_methods;

    return (Node *)cypher_css;
}
