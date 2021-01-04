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

#include "postgres.h"

#include "access/sysattr.h"
#include "access/htup_details.h"
#include "access/xact.h"
#include "storage/bufmgr.h"
#include "executor/tuptable.h"
#include "nodes/execnodes.h"
#include "nodes/extensible.h"
#include "nodes/nodes.h"
#include "nodes/plannodes.h"
#include "parser/parsetree.h"
#include "parser/parse_relation.h"
#include "rewrite/rewriteHandler.h"
#include "utils/rel.h"

#include "catalog/ag_label.h"
#include "commands/label_commands.h"
#include "executor/cypher_executor.h"
#include "executor/cypher_utils.h"
#include "parser/cypher_parse_node.h"
#include "nodes/cypher_nodes.h"
#include "utils/agtype.h"
#include "utils/graphid.h"

static void begin_cypher_set(CustomScanState *node, EState *estate,
                                int eflags);
static TupleTableSlot *exec_cypher_set(CustomScanState *node);
static void end_cypher_set(CustomScanState *node);
static void rescan_cypher_set(CustomScanState *node);

static void process_update_list(CustomScanState *node);
agtype_value *alter_property_value(agtype_value *properties, char *var_name, agtype *new_v, bool remove_property);
static HeapTuple update_entity_tuple(ResultRelInfo *resultRelInfo,
                                TupleTableSlot *elemTupleSlot, EState *estate, HeapTuple old_tuple);

const CustomExecMethods cypher_set_exec_methods = {"Cypher Set",
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
                          ExecGetResultType(node->ss.ps.lefttree));

    if (!CYPHER_CLAUSE_IS_TERMINAL(css->flags))
    {
        TupleDesc tupdesc = node->ss.ss_ScanTupleSlot->tts_tupleDescriptor;

        ExecAssignProjectionInfo(&node->ss.ps, tupdesc);
    }

    estate->es_output_cid++;
}

static HeapTuple update_entity_tuple(ResultRelInfo *resultRelInfo,
                                TupleTableSlot *elemTupleSlot, EState *estate, HeapTuple old_tuple)
{
    HeapTuple tuple;
    LockTupleMode lockmode;
    HeapUpdateFailureData hufd;
    HTSU_Result lock_result;
    HTSU_Result update_result;
    Buffer buffer;

    ResultRelInfo *saved_resultRelInfo = saved_resultRelInfo;;
    estate->es_result_relation_info = resultRelInfo;


    lockmode = ExecUpdateLockMode(estate, resultRelInfo);

    //XXX: Check lock_result
    lock_result = heap_lock_tuple(resultRelInfo->ri_RelationDesc, old_tuple, estate->es_output_cid,
                                  lockmode, LockWaitBlock, false, &buffer, &hufd);

    if (lock_result != HeapTupleMayBeUpdated)
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                    errmsg("Entity could not be locked for updating")));


    ExecStoreVirtualTuple(elemTupleSlot);
    tuple = ExecMaterializeSlot(elemTupleSlot);
    tuple->t_self = old_tuple->t_self;

    // Check the constraints of the tuple
    tuple->t_tableOid = RelationGetRelid(resultRelInfo->ri_RelationDesc);
    if (resultRelInfo->ri_RelationDesc->rd_att->constr != NULL)
        ExecConstraints(resultRelInfo, elemTupleSlot, estate);

    // Insert the tuple normally
    update_result = heap_update(resultRelInfo->ri_RelationDesc, &(tuple->t_self), tuple,
                         estate->es_output_cid, estate->es_crosscheck_snapshot,
                         true, &hufd, &lockmode);

    if (update_result != HeapTupleMayBeUpdated)
    ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                    errmsg("Entity failed to be updated")));

    // Insert index entries for the tuple
    if (resultRelInfo->ri_NumIndices > 0)
        ExecInsertIndexTuples(elemTupleSlot, &(tuple->t_self), estate, false,
                              NULL, NIL);

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
    cypher_set_custom_scan_state *css =
        (cypher_set_custom_scan_state *)node;
    TupleTableSlot *slot;

    do
    {
        css->tuple_info = NIL;

        process_update_list(node);

        slot = ExecProcNode(node->ss.ps.lefttree);
    } while (!TupIsNull(slot));
}

static bool check_path(agtype_value *path, graphid updated_id)
{
    int i;

    for (i = 0; i < path->val.array.num_elems; i++)
    {
        agtype_value *elem = &path->val.array.elems[i];

        agtype_value *id = get_agtype_value_object_value(elem, "id");

        if (updated_id == id->val.int_value)
            return true;
    }

    return false;
}

static agtype_value *replace_entity_in_path(agtype_value *path, graphid updated_id, agtype *updated_entity)
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

    parsed_agtype_value = push_agtype_value(&parse_state, tok, tok < WAGT_BEGIN_ARRAY ? r : NULL);

    for (i = 0; i < path->val.array.num_elems; i++)
    {
        agtype_value *id, *elem;

        elem = &path->val.array.elems[i];

        if (elem->type != AGTV_VERTEX && elem->type != AGTV_EDGE)
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("unsupported agtype found in a path")));

        id = get_agtype_value_object_value(elem, "id");

        if (updated_id == id->val.int_value)
            parsed_agtype_value = push_agtype_value(
                &parse_state, WAGT_ELEM, get_ith_agtype_value_from_container(&updated_entity->root, 0));
        else
            parsed_agtype_value = push_agtype_value(
                &parse_state, WAGT_ELEM, elem);
    }

    parsed_agtype_value = push_agtype_value(&parse_state, WAGT_END_ARRAY, NULL);

    parsed_agtype_value->type = AGTV_PATH;
    return parsed_agtype_value;
}

static void update_all_paths(CustomScanState *node, graphid id, agtype *updated_entity)
{
    cypher_set_custom_scan_state *css =
        (cypher_set_custom_scan_state *)node;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    TupleTableSlot *scanTupleSlot = econtext->ecxt_scantuple;

    int i;
    for (i = 0; i < scanTupleSlot->tts_tupleDescriptor->natts; i++)
    {
        agtype *original_entity;
        agtype_value *original_entity_value;

        if (scanTupleSlot->tts_tupleDescriptor->attrs[i].atttypid != AGTYPEOID)
            continue;

        if (scanTupleSlot->tts_isnull[i])
            continue;

        original_entity = DATUM_GET_AGTYPE_P(scanTupleSlot->tts_values[i]);
        original_entity_value = get_ith_agtype_value_from_container(&original_entity->root, 0);

        if (original_entity_value->type == AGTV_PATH)
        {
            if (check_path(original_entity_value, id))
            {
                agtype_value *new_path = replace_entity_in_path(original_entity_value, id, updated_entity);

                scanTupleSlot->tts_values[i] = AGTYPE_P_GET_DATUM(agtype_value_to_agtype(new_path));
            }
        }
    }
}

static void process_update_list(CustomScanState *node)
{
    cypher_set_custom_scan_state *css =
        (cypher_set_custom_scan_state *)node;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    TupleTableSlot *scanTupleSlot = econtext->ecxt_scantuple;
    ListCell *lc;
    EState *estate = css->css.ss.ps.state;

    css->tuple_info = NIL;

    foreach (lc, css->set_list->set_items)
    {
        agtype_value *altered_properties, *original_entity_value, *original_properties, *id, *label;
        agtype *original_entity, *new_property_value;
        bool remove_property;
        char *label_name;
        cypher_update_item *update_item = (cypher_update_item *)lfirst(lc);
        Datum new_entity;
        ResultRelInfo *resultRelInfo;
        TupleTableSlot *elemTupleSlot;
        HeapTuple heap_tuple;
        HeapTuple tuple;
        char *clause_name = css->set_list->clause_name;

        update_item = (cypher_update_item *)lfirst(lc);

        /*
         * If the entity is null, we can skip this update. this will be
         * possible when the OPTIONAL MATCH clause is implemented.
         */
        if (scanTupleSlot->tts_isnull[update_item->entity_position - 1])
            continue;

        if (scanTupleSlot->tts_tupleDescriptor->attrs[update_item->entity_position -1].atttypid != AGTYPEOID)
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                    errmsg("age %s clause can only update agtype", clause_name)));

        original_entity = DATUM_GET_AGTYPE_P(scanTupleSlot->tts_values[update_item->entity_position - 1]);
        original_entity_value = get_ith_agtype_value_from_container(&original_entity->root, 0);

        if (original_entity_value->type != AGTV_VERTEX && original_entity_value->type != AGTV_EDGE)
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                    errmsg("age %s clause can only update vertex and edges", clause_name)));

        // get the id and label for later
        id = get_agtype_value_object_value(original_entity_value, "id");
        label = get_agtype_value_object_value(original_entity_value, "label");
        label_name = pnstrdup(label->val.string.val, label->val.string.len);

        // get the properties we need to update
        original_properties = get_agtype_value_object_value(original_entity_value, "properties");

        /*
         * Determine if the property should be removed.
         */
        if(update_item->remove_item)
            remove_property = true;
        else
            remove_property = scanTupleSlot->tts_isnull[update_item->prop_position - 1];

        if (remove_property)
            new_property_value = NULL;
        else
            new_property_value = DATUM_GET_AGTYPE_P(scanTupleSlot->tts_values[update_item->prop_position - 1]);

        altered_properties = alter_property_value(original_properties, update_item->prop_name,
                                                  new_property_value, remove_property);

        // create the resultRelInfo that is the on disc table we need to update
        resultRelInfo = create_entity_result_rel_info(node, css->set_list->graph_name, label_name);

        ExecOpenIndices(resultRelInfo, false);

        elemTupleSlot = ExecInitExtraTupleSlot(estate, RelationGetDescr(resultRelInfo->ri_RelationDesc));

        if (original_entity_value->type == AGTV_VERTEX)
        {
            elemTupleSlot = populate_vertex_tts(elemTupleSlot, id, altered_properties);

            new_entity = make_vertex(GRAPHID_GET_DATUM(id->val.int_value),
                                     CStringGetDatum(label_name),
                                     AGTYPE_P_GET_DATUM(agtype_value_to_agtype(altered_properties)));
        }
        else if (original_entity_value->type == AGTV_EDGE)
        {
            agtype_value *startid = get_agtype_value_object_value(original_entity_value, "start_id");
            agtype_value *endid = get_agtype_value_object_value(original_entity_value, "end_id");

            elemTupleSlot = populate_edge_tts(elemTupleSlot, id, startid, endid, altered_properties);

            new_entity = make_edge(GRAPHID_GET_DATUM(id->val.int_value),
                                   GRAPHID_GET_DATUM(startid->val.int_value),
                                   GRAPHID_GET_DATUM(endid->val.int_value),
                                   CStringGetDatum(label_name),
                                   AGTYPE_P_GET_DATUM(agtype_value_to_agtype(altered_properties)));
        }

        // update the on-disc table
        heap_tuple = get_heap_tuple(node, update_item->var_name);

        tuple = update_entity_tuple(resultRelInfo, elemTupleSlot, estate, heap_tuple);

        if (update_item->var_name != NULL)
        {
            clause_tuple_information *tuple_info;

            tuple_info = palloc(sizeof(clause_tuple_information));

            tuple_info->tuple = tuple;
            tuple_info->name = update_item->var_name;

            css->tuple_info = lappend(css->tuple_info, tuple_info);
        }


        ExecCloseIndices(resultRelInfo);

        heap_close(resultRelInfo->ri_RelationDesc, RowExclusiveLock);

        // update the in-memory tuple slot
        scanTupleSlot->tts_values[update_item->entity_position - 1] = new_entity;

        update_all_paths(node, id->val.int_value, DATUM_GET_AGTYPE_P(new_entity));
    }
}

static TupleTableSlot *exec_cypher_set(CustomScanState *node)
{
    cypher_set_custom_scan_state *css =
        (cypher_set_custom_scan_state *)node;
    ResultRelInfo *saved_resultRelInfo;
    EState *estate = css->css.ss.ps.state;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    TupleTableSlot *slot;

    saved_resultRelInfo = estate->es_result_relation_info;

    css->tuple_info = NIL;

    //Process the subtree first
    estate->es_output_cid--;
    slot = ExecProcNode(node->ss.ps.lefttree);
    estate->es_output_cid++;

    if (TupIsNull(slot))
        return NULL;

    econtext->ecxt_scantuple =
        node->ss.ps.lefttree->ps_ProjInfo->pi_exprContext->ecxt_scantuple;

    if (CYPHER_CLAUSE_IS_TERMINAL(css->flags))
    {
        estate->es_result_relation_info = saved_resultRelInfo;

        process_all_tuples(node);

        return NULL;
    }

    process_update_list(node);

    estate->es_result_relation_info = saved_resultRelInfo;

    econtext->ecxt_scantuple =
        ExecProject(node->ss.ps.lefttree->ps_ProjInfo);

    return ExecProject(node->ss.ps.ps_ProjInfo);
}

static void end_cypher_set(CustomScanState *node)
{
    ExecEndNode(node->ss.ps.lefttree);

    GetCurrentCommandId(true);
}

static void rescan_cypher_set(CustomScanState *node)
{
    cypher_set_custom_scan_state *css =
        (cypher_set_custom_scan_state *)node;
    char *clause_name = css->set_list->clause_name;

     ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                    errmsg("cypher %s clause cannot be rescaned", clause_name),
                    errhint("its unsafe to use joins in a query with a Cypher %s clause", clause_name)));
}

Node *create_cypher_set_plan_state(CustomScan *cscan)
{
    cypher_set_custom_scan_state *cypher_css =
        palloc0(sizeof(cypher_set_custom_scan_state));
    cypher_update_information *set_list;

    cypher_css->cs = cscan;

    set_list = linitial(cscan->custom_private);
    cypher_css->set_list = set_list;
    cypher_css->flags = set_list->flags;

    cypher_css->css.ss.ps.type = T_CustomScanState;
    cypher_css->css.methods = &cypher_set_exec_methods;

    return (Node *)cypher_css;
}
