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

#include "storage/bufmgr.h"
#include "common/hashfn.h"

#include "catalog/ag_label.h"
#include "executor/cypher_executor.h"
#include "executor/cypher_utils.h"

static void begin_cypher_delete(CustomScanState *node, EState *estate,
                                int eflags);
static TupleTableSlot *exec_cypher_delete(CustomScanState *node);
static void end_cypher_delete(CustomScanState *node);
static void rescan_cypher_delete(CustomScanState *node);

static void process_delete_list(CustomScanState *node);

static void check_for_connected_edges(CustomScanState *node);
static agtype_value *extract_entity(CustomScanState *node,
                                    TupleTableSlot *scanTupleSlot,
                                    int entity_position);
static void delete_entity(EState *estate, ResultRelInfo *resultRelInfo,
                          HeapTuple tuple);

const CustomExecMethods cypher_delete_exec_methods = {DELETE_SCAN_STATE_NAME,
                                                      begin_cypher_delete,
                                                      exec_cypher_delete,
                                                      end_cypher_delete,
                                                      rescan_cypher_delete,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL};

/*
 * Initialization at the beginning of execution. Setup the child node,
 * setup its scan tuple slot and projection info, expression context,
 * collect metadata about visible edges, and alter the commandid for
 * the transaction.
 */
static void begin_cypher_delete(CustomScanState *node, EState *estate,
                                int eflags)
{
    cypher_delete_custom_scan_state *css =
        (cypher_delete_custom_scan_state *)node;
    Plan *subplan;
    HASHCTL hashctl;

    Assert(list_length(css->cs->custom_plans) == 1);

    /* setup child */
    subplan = linitial(css->cs->custom_plans);
    node->ss.ps.lefttree = ExecInitNode(subplan, estate, eflags);

    /* setup expr context */
    ExecAssignExprContext(estate, &node->ss.ps);

    /* setup scan tuple slot and projection info */
    ExecInitScanTupleSlot(estate, &node->ss,
                          ExecGetResultType(node->ss.ps.lefttree),
                          &TTSOpsHeapTuple);

    if (!CYPHER_CLAUSE_IS_TERMINAL(css->flags))
    {
        TupleDesc tupdesc = node->ss.ss_ScanTupleSlot->tts_tupleDescriptor;

        ExecAssignProjectionInfo(&node->ss.ps, tupdesc);
    }

    /*
     * Get all the labels that are visible to this delete clause at this point
     * in the transaction. To be used later when the delete clause finds
     * vertices.
     */
    css->edge_labels = get_all_edge_labels_per_graph(estate, css->delete_data->graph_oid);

    /* init vertex_id_htab */
    MemSet(&hashctl, 0, sizeof(hashctl));
    hashctl.keysize = sizeof(graphid);
    hashctl.entrysize =
        sizeof(graphid); /* entries are not used, but entrysize must >= keysize */
    hashctl.hash = tag_hash;
    css->vertex_id_htab = hash_create(DELETE_VERTEX_HTAB_NAME,
                                      DELETE_VERTEX_HTAB_SIZE, &hashctl,
                                      HASH_ELEM | HASH_FUNCTION);

    /*
     * Postgres does not assign the es_output_cid in queries that do
     * not write to disk, ie: SELECT commands. We need the command id
     * for our clauses, and we may need to initialize it. We cannot use
     * GetCurrentCommandId because there may be other cypher clauses
     * that have modified the command id.
     */
    if (estate->es_output_cid == 0)
        estate->es_output_cid = estate->es_snapshot->curcid;

    Increment_Estate_CommandId(estate);
}

/*
 * Called once per tuple. If this is a terminal DELETE clause
 * process everyone of its child tuple, otherwise process the
 * next tuple.
 */
static TupleTableSlot *exec_cypher_delete(CustomScanState *node)
{
    cypher_delete_custom_scan_state *css =
        (cypher_delete_custom_scan_state *)node;
    EState *estate = css->css.ss.ps.state;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    TupleTableSlot *slot;

    if (CYPHER_CLAUSE_IS_TERMINAL(css->flags))
    {
        /*
         * If the DELETE clause was the final cypher clause written
         * then we aren't returning anything from this result node.
         * So the exec_cypher_delete function will only be called once.
         * Therefore we will process all tuples from the subtree at once.
         */
        while(true)
        {
            /* Process the subtree first */
            Decrement_Estate_CommandId(estate)
            slot = ExecProcNode(node->ss.ps.lefttree);
            Increment_Estate_CommandId(estate)

            if (TupIsNull(slot))
                break;

            /* setup the scantuple that the process_delete_list needs */
            econtext->ecxt_scantuple =
                node->ss.ps.lefttree->ps_ProjInfo->pi_exprContext->ecxt_scantuple;

            process_delete_list(node);
        }

        return NULL;
    }
    else
    {
        /* Process the subtree first */
        Decrement_Estate_CommandId(estate)
        slot = ExecProcNode(node->ss.ps.lefttree);
        Increment_Estate_CommandId(estate)

        if (TupIsNull(slot))
            return NULL;

        /* setup the scantuple that the process_delete_list needs */
        econtext->ecxt_scantuple =
            node->ss.ps.lefttree->ps_ProjInfo->pi_exprContext->ecxt_scantuple;

        process_delete_list(node);

        econtext->ecxt_scantuple =
            ExecProject(node->ss.ps.lefttree->ps_ProjInfo);

        return ExecProject(node->ss.ps.ps_ProjInfo);
    }
}

/*
 * Called at the end of execution. Tell its child to
 * end its execution.
 */
static void end_cypher_delete(CustomScanState *node)
{
    check_for_connected_edges(node);

    hash_destroy(((cypher_delete_custom_scan_state *)node)->vertex_id_htab);

    ExecEndNode(node->ss.ps.lefttree);
}

/*
 * Used for rewinding the scan state and reprocessing the results.
 *
 * XXX: This is not currently supported. We need to find out
 * when this function will be called and determine a process
 * for allowing the Delete clause to run multiple times without
 * redundant edits to the database.
 */
static void rescan_cypher_delete(CustomScanState *node)
{
     ereport(ERROR,
             (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                      errmsg("cypher DELETE clause cannot be rescanned"),
                      errhint("its unsafe to use joins in a query with a Cypher DELETE clause")));
}

/*
 * Create the CustomScanState from the CustomScan and pass
 * necessary metadata.
 */
Node *create_cypher_delete_plan_state(CustomScan *cscan)
{
    cypher_delete_custom_scan_state *cypher_css =
        palloc0(sizeof(cypher_delete_custom_scan_state));
    cypher_delete_information *delete_data;
    char *serialized_data;
    Const *c;

    cypher_css->cs = cscan;

    /* get the serialized data structure from the Const and deserialize it. */
    c = linitial(cscan->custom_private);
    serialized_data = (char *)c->constvalue;
    delete_data = stringToNode(serialized_data);

    Assert(is_ag_node(delete_data, cypher_delete_information));

    cypher_css->delete_data = delete_data;
    cypher_css->flags = delete_data->flags;

    cypher_css->css.ss.ps.type = T_CustomScanState;
    cypher_css->css.methods = &cypher_delete_exec_methods;

    return (Node *)cypher_css;
}

/*
 * Extract the vertex or edge to be deleted, perform some type checking to
 * validate datum is an agtype vertex or edge.
 */
static agtype_value *extract_entity(CustomScanState *node,
                                    TupleTableSlot *scanTupleSlot,
                                    int entity_position)
{
    agtype_value *original_entity_value;
    agtype *original_entity;
    TupleDesc tupleDescriptor;

    tupleDescriptor = scanTupleSlot->tts_tupleDescriptor;

    /* type checking, make sure the entity is an agtype vertex or edge */
    if (tupleDescriptor->attrs[entity_position -1].atttypid != AGTYPEOID)
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                errmsg("DELETE clause can only delete agtype")));

    original_entity = DATUM_GET_AGTYPE_P(scanTupleSlot->tts_values[entity_position - 1]);
    original_entity_value = get_ith_agtype_value_from_container(&original_entity->root, 0);

    if (original_entity_value->type != AGTV_VERTEX && original_entity_value->type != AGTV_EDGE)
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                errmsg("DELETE clause can only delete vertices and edges")));

    return original_entity_value;
}

/*
 * Try and delete the entity that is describe by the HeapTuple in the table
 * described by the resultRelInfo.
 */
static void delete_entity(EState *estate, ResultRelInfo *resultRelInfo,
                          HeapTuple tuple)
{
    ResultRelInfo **saved_resultRels;
    LockTupleMode lockmode;
    TM_FailureData hufd;
    TM_Result lock_result;
    TM_Result delete_result;
    Buffer buffer;

    /* Find the physical tuple, this variable is coming from */
    saved_resultRels = estate->es_result_relations;
    estate->es_result_relations = &resultRelInfo;

    lockmode = ExecUpdateLockMode(estate, resultRelInfo);

    lock_result = heap_lock_tuple(resultRelInfo->ri_RelationDesc, tuple,
                                  GetCurrentCommandId(false), lockmode,
                                  LockWaitBlock, false, &buffer, &hufd);

    /*
     * It is possible the entity may have already been deleted. If the tuple
     * can be deleted, the lock result will be HeapTupleMayBeUpdated. If the
     * tuple was already deleted by this DELETE clause, the result would be
     * TM_SelfModified, if the result was deleted by a previous delete
     * clause, the result will TM_Invisible. Throw an error if any
     * other result was returned.
     */
    if (lock_result == TM_Ok)
    {
        delete_result = heap_delete(resultRelInfo->ri_RelationDesc,
                                    &tuple->t_self, GetCurrentCommandId(true),
                                    estate->es_crosscheck_snapshot, true, &hufd,
                                    false);

        /*
         * Unlike locking, the heap_delete either succeeded
         * HeapTupleMayBeUpdate, or it failed and returned any other result.
         */
        switch (delete_result)
        {
        case TM_Ok:
            break;
        case TM_SelfModified:
            ereport(
                ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg(
                     "deleting the same entity more than once cannot happen")));
            /* ereport never gets here */
            break;
        case TM_Updated:
            ereport(
                ERROR,
                (errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
                 errmsg("could not serialize access due to concurrent update")));
            /* ereport never gets here */
            break;
        default:
            elog(ERROR, "Entity failed to be update");
            /* elog never gets here */
            break;
        }
        /* increment the command counter */
        CommandCounterIncrement();

        /* Update command id in estate */
        estate->es_snapshot->curcid = GetCurrentCommandId(false);
        estate->es_output_cid = GetCurrentCommandId(false);
    }
    else if (lock_result != TM_Invisible && lock_result != TM_SelfModified)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Entity could not be locked for updating")));

    }

    ReleaseBuffer(buffer);

    estate->es_result_relations = saved_resultRels;
}

/*
 * After the delete's subtress has been processed, we then go through the list
 * of variables to be deleted.
 */
static void process_delete_list(CustomScanState *node)
{
    cypher_delete_custom_scan_state *css =
        (cypher_delete_custom_scan_state *)node;
    ListCell *lc;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    TupleTableSlot *scanTupleSlot = econtext->ecxt_scantuple;
    EState *estate = node->ss.ps.state;

    foreach(lc, css->delete_data->delete_items)
    {
        cypher_delete_item *item;
        agtype_value *original_entity_value, *id, *label;
        ScanKeyData scan_keys[1];
        TableScanDesc scan_desc;
        ResultRelInfo *resultRelInfo;
        HeapTuple heap_tuple;
        char *label_name;
        Integer *pos;
        int entity_position;

        item = lfirst(lc);

        pos = item->entity_position;
        entity_position = pos->ival;

        /* skip if the entity is null */
        if (scanTupleSlot->tts_isnull[entity_position - 1])
            continue;

        original_entity_value = extract_entity(node, scanTupleSlot,
                                               entity_position);

        id = GET_AGTYPE_VALUE_OBJECT_VALUE(original_entity_value, "id");
        label = GET_AGTYPE_VALUE_OBJECT_VALUE(original_entity_value, "label");
        label_name = pnstrdup(label->val.string.val, label->val.string.len);

        resultRelInfo = create_entity_result_rel_info(estate, css->delete_data->graph_name, label_name);

        /*
         * Setup the scan key to require the id field on-disc to match the
         * entity's graphid.
         */
        if (original_entity_value->type == AGTV_VERTEX)
        {
            ScanKeyInit(&scan_keys[0], Anum_ag_label_vertex_table_id,
                        BTEqualStrategyNumber, F_GRAPHIDEQ,
                        GRAPHID_GET_DATUM(id->val.int_value));
        }
        else if (original_entity_value->type == AGTV_EDGE)
        {
            ScanKeyInit(&scan_keys[0], Anum_ag_label_edge_table_id,
                        BTEqualStrategyNumber, F_GRAPHIDEQ,
                        GRAPHID_GET_DATUM(id->val.int_value));
        }
        else
        {
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                    errmsg("DELETE clause can only delete vertices and edges")));
        }

        /*
         * Setup the scan description, with the correct snapshot and scan keys.
         */
        estate->es_snapshot->curcid = GetCurrentCommandId(false);
        estate->es_output_cid = GetCurrentCommandId(false);
        scan_desc = table_beginscan(resultRelInfo->ri_RelationDesc,
                                    estate->es_snapshot, 1, scan_keys);

        /* Retrieve the tuple. */
        heap_tuple = heap_getnext(scan_desc, ForwardScanDirection);

        /*
         * If the heap tuple still exists (It wasn't deleted after this variable
         * was created) we can delete it. Otherwise, its safe to skip this
         * delete.
         */
        if (!HeapTupleIsValid(heap_tuple))
        {
            table_endscan(scan_desc);
            destroy_entity_result_rel_info(resultRelInfo);

            continue;
        }

        /*
         * For vertices, we insert the vertex ID in the hashtable
         * vertex_id_htab. This hashtable is used later to process
         * connected edges.
         */
        if (original_entity_value->type == AGTV_VERTEX)
        {
            bool found;
            hash_search(css->vertex_id_htab, (void *)&(id->val.int_value),
                        HASH_ENTER, &found);
        }

        /* At this point, we are ready to delete the node/vertex. */
        delete_entity(estate, resultRelInfo, heap_tuple);

        /* Close the scan and the relation. */
        table_endscan(scan_desc);
        destroy_entity_result_rel_info(resultRelInfo);
    }
}

/*
 * Scans the edge tables and checks if the deleted vertices are connected to
 * any edge(s). For DETACH DELETE, the connected edges are deleted. Otherwise,
 * an error is thrown.
 */
static void check_for_connected_edges(CustomScanState *node)
{
    ListCell *lc;
    cypher_delete_custom_scan_state *css =
        (cypher_delete_custom_scan_state *)node;
    EState *estate = css->css.ss.ps.state;
    char *graph_name = css->delete_data->graph_name;

    /* scans each label from css->edge_labels */
    foreach (lc, css->edge_labels)
    {
        char *label_name = lfirst(lc);
        ResultRelInfo *resultRelInfo;
        TableScanDesc scan_desc;
        HeapTuple tuple;
        TupleTableSlot *slot;

        resultRelInfo = create_entity_result_rel_info(estate, graph_name,
                                                      label_name);
        estate->es_snapshot->curcid = GetCurrentCommandId(false);
        estate->es_output_cid = GetCurrentCommandId(false);
        scan_desc = table_beginscan(resultRelInfo->ri_RelationDesc,
                                    estate->es_snapshot, 0, NULL);
        slot = ExecInitExtraTupleSlot(
            estate, RelationGetDescr(resultRelInfo->ri_RelationDesc),
            &TTSOpsHeapTuple);

        /* for each row */
        while (true)
        {
            graphid startid;
            graphid endid;
            bool isNull;
            bool found_startid = false;
            bool found_endid = false;

            tuple = heap_getnext(scan_desc, ForwardScanDirection);

            /* no more tuples to process, break and scan the next label. */
            if (!HeapTupleIsValid(tuple))
            {
                break;
            }

            ExecStoreHeapTuple(tuple, slot, false);

            startid = GRAPHID_GET_DATUM(slot_getattr(
                slot, Anum_ag_label_edge_table_start_id, &isNull));
            endid = GRAPHID_GET_DATUM(
                slot_getattr(slot, Anum_ag_label_edge_table_end_id, &isNull));

            hash_search(css->vertex_id_htab, (void *)&startid, HASH_FIND,
                        &found_startid);

            if (!found_startid)
            {
                hash_search(css->vertex_id_htab, (void *)&endid, HASH_FIND,
                            &found_endid);
            }

            if (found_startid || found_endid)
            {
                if (css->delete_data->detach)
                {
                    delete_entity(estate, resultRelInfo, tuple);
                }
                else
                {
                    ereport(
                        ERROR,
                        (errcode(ERRCODE_INTERNAL_ERROR),
                         errmsg(
                             "Cannot delete a vertex that has edge(s). "
                             "Delete the edge(s) first, or try DETACH DELETE.")));
                }
            }
        }

        table_endscan(scan_desc);
        destroy_entity_result_rel_info(resultRelInfo);
    }
}
