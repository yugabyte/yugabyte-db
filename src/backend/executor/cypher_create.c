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

#include "access/htup_details.h"
#include "access/xact.h"
#include "executor/tuptable.h"
#include "nodes/execnodes.h"
#include "nodes/extensible.h"
#include "nodes/nodes.h"
#include "nodes/plannodes.h"
#include "parser/parse_relation.h"
#include "rewrite/rewriteHandler.h"
#include "utils/rel.h"

#include "catalog/ag_label.h"
#include "executor/cypher_executor.h"
#include "executor/cypher_utils.h"
#include "nodes/cypher_nodes.h"
#include "utils/agtype.h"
#include "utils/graphid.h"

static void begin_cypher_create(CustomScanState *node, EState *estate,
                                int eflags);
static TupleTableSlot *exec_cypher_create(CustomScanState *node);
static void end_cypher_create(CustomScanState *node);
static void rescan_cypher_create(CustomScanState *node);

static void create_edge(cypher_create_custom_scan_state *css,
                        cypher_target_node *node, Datum prev_vertex_id,
                        ListCell *next);

static Datum create_vertex(cypher_create_custom_scan_state *css,
                           cypher_target_node *node, ListCell *next);
static HeapTuple insert_entity_tuple(ResultRelInfo *resultRelInfo,
                                TupleTableSlot *elemTupleSlot, EState *estate);
static void process_pattern(cypher_create_custom_scan_state *css);
static void process_all_tuples(CustomScanState *node, EState *estate);

const CustomExecMethods cypher_create_exec_methods = {"Cypher Create",
                                                      begin_cypher_create,
                                                      exec_cypher_create,
                                                      end_cypher_create,
                                                      rescan_cypher_create,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL};

static void begin_cypher_create(CustomScanState *node, EState *estate,
                                int eflags)
{
    cypher_create_custom_scan_state *css =
        (cypher_create_custom_scan_state *)node;
    ListCell *lc;
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

    foreach (lc, css->pattern)
    {
        cypher_create_path *path = lfirst(lc);
        ListCell *lc2;
        foreach (lc2, path->target_nodes)
        {
            cypher_target_node *cypher_node =
                (cypher_target_node *)lfirst(lc2);
            ListCell *lc_expr;
            Relation rel;

            if (!CYPHER_TARGET_NODE_INSERT_ENTITY(cypher_node->flags))
                continue;

            // Open relation and aquire a row exclusive lock.
            rel = heap_open(cypher_node->relid, RowExclusiveLock);

            // Initialize resultRelInfo for the vertex
            cypher_node->resultRelInfo = palloc(sizeof(ResultRelInfo));
            InitResultRelInfo(cypher_node->resultRelInfo, rel,
                              list_length(estate->es_range_table), NULL,
                              estate->es_instrument);

            // Open all indexes for the relation
            ExecOpenIndices(cypher_node->resultRelInfo, false);

            // Setup the relation's tuple slot
            cypher_node->elemTupleSlot = ExecInitExtraTupleSlot(
                estate,
                RelationGetDescr(cypher_node->resultRelInfo->ri_RelationDesc));

            // setup expr states for the relation's target list
            foreach (lc_expr, cypher_node->targetList)
            {
                TargetEntry *te = lfirst(lc_expr);

                cypher_node->expr_states =
                    lappend(cypher_node->expr_states,
                            ExecInitExpr(te->expr, (PlanState *)node));
            }
        }
    }

    estate->es_output_cid++;
}

/*
 * CREATE the vertices and edges for a CREATE clause pattern.
 */
static void process_pattern(cypher_create_custom_scan_state *css)
{
    ListCell *lc2;

    css->tuple_info = NIL;

    foreach (lc2, css->pattern)
    {
        cypher_create_path *path = lfirst(lc2);

        ListCell *lc = list_head(path->target_nodes);

        /*
         * Create the first vertex. The create_vertex function will
         * create the rest of the path, if necessary.
         */
        create_vertex(css, lfirst(lc), lnext(lc));

        /*
         * If this path is a variable, take the list that was accumulated
         * in the vertex/edge creation, create a path datum, and add to the
         * scantuple slot.
         */
        if (path->tuple_position > 0)
        {
            TupleTableSlot *scantuple;
            PlanState *ps;
            Datum result;

            ps = css->css.ss.ps.lefttree;
            scantuple = ps->ps_ExprContext->ecxt_scantuple;

            result = make_path(css->path_values);

            scantuple->tts_values[path->tuple_position - 1] = result;
            scantuple->tts_isnull[path->tuple_position - 1] = false;
        }

        css->path_values = NIL;
    }
}

/*
 * When the CREATE clause is the last cypher clause, consume all input from the
 * previous clause(s) in the first call of exec_cypher_create.
 */
static void process_all_tuples(CustomScanState *node, EState *estate)
{
    cypher_create_custom_scan_state *css =
        (cypher_create_custom_scan_state *)node;
    TupleTableSlot *slot;

    do
    {
        estate->es_output_cid++;
        process_pattern(css);
        estate->es_output_cid--;

        slot = ExecProcNode(node->ss.ps.lefttree);
    } while (!TupIsNull(slot));
}

static TupleTableSlot *exec_cypher_create(CustomScanState *node)
{
    cypher_create_custom_scan_state *css =
        (cypher_create_custom_scan_state *)node;
    ResultRelInfo *saved_resultRelInfo;
    EState *estate = css->css.ss.ps.state;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    TupleTableSlot *slot;
    MemoryContext old_mcxt;

    saved_resultRelInfo = estate->es_result_relation_info;

    //Process the subtree first
    estate->es_output_cid--;
    slot = ExecProcNode(node->ss.ps.lefttree);
    css->slot = slot;
    estate->es_output_cid++;

    econtext->ecxt_scantuple =
        node->ss.ps.lefttree->ps_ProjInfo->pi_exprContext->ecxt_scantuple;

    if (TupIsNull(slot))
        return NULL;

    old_mcxt = MemoryContextSwitchTo(econtext->ecxt_scantuple->tts_mcxt);

    if (CYPHER_CLAUSE_IS_TERMINAL(css->flags))
    {
        process_all_tuples(node, estate);

        MemoryContextSwitchTo(old_mcxt);

        estate->es_result_relation_info = saved_resultRelInfo;

        return NULL;
    }
    else
    {
        process_pattern(css);

        MemoryContextSwitchTo(old_mcxt);

        estate->es_result_relation_info = saved_resultRelInfo;

        /*
         * Now that process_pattern has filled in the missing values,
         * rerun the projection information.
         */
        econtext->ecxt_scantuple =
            ExecProject(node->ss.ps.lefttree->ps_ProjInfo);

        return ExecProject(node->ss.ps.ps_ProjInfo);
    }
}

static void end_cypher_create(CustomScanState *node)
{
    cypher_create_custom_scan_state *css =
        (cypher_create_custom_scan_state *)node;
    ListCell *lc;

    ExecEndNode(node->ss.ps.lefttree);

    foreach (lc, css->pattern)
    {
        cypher_create_path *path = lfirst(lc);
        ListCell *lc2;
        foreach (lc2, path->target_nodes)
        {
            cypher_target_node *cypher_node =
                (cypher_target_node *)lfirst(lc2);

            if (!CYPHER_TARGET_NODE_INSERT_ENTITY(cypher_node->flags))
                continue;

            // close all indices for the node
            ExecCloseIndices(cypher_node->resultRelInfo);

            // close the relation itself
            heap_close(cypher_node->resultRelInfo->ri_RelationDesc,
                       RowExclusiveLock);
        }
    }

    GetCurrentCommandId(true);
}

static void rescan_cypher_create(CustomScanState *node)
{
    ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                    errmsg("cypher create clause cannot be rescaned"),
                    errhint("its unsafe to use joins in a query with a Cypher CREATE clause")));
}

Node *create_cypher_create_plan_state(CustomScan *cscan)
{
    cypher_create_custom_scan_state *cypher_css =
        palloc0(sizeof(cypher_create_custom_scan_state));
    cypher_create_target_nodes *target_nodes;

    cypher_css->cs = cscan;

    target_nodes = linitial(cscan->custom_private);
    cypher_css->path_values = NIL;
    cypher_css->pattern = target_nodes->paths;
    cypher_css->tuple_info = NIL;
    cypher_css->flags = target_nodes->flags;

    cypher_css->css.ss.ps.type = T_CustomScanState;
    cypher_css->css.methods = &cypher_create_exec_methods;

    return (Node *)cypher_css;
}

/*
 * Create the edge entity.
 */
static void create_edge(cypher_create_custom_scan_state *css,
                        cypher_target_node *node, Datum prev_vertex_id,
                        ListCell *next)
{
    bool isNull;
    EState *estate = css->css.ss.ps.state;
    ExprState *es;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    ResultRelInfo *resultRelInfo = node->resultRelInfo;
    TupleTableSlot *elemTupleSlot = node->elemTupleSlot;
    TupleTableSlot *scanTupleSlot = econtext->ecxt_scantuple;
    Datum id;
    Datum start_id, end_id, next_vertex_id;
    List *prev_path = css->path_values;
    HeapTuple tuple;

    Assert(node->type == LABEL_KIND_EDGE);
    Assert(lfirst(next) != NULL);

    /*
     * Create the next vertex before creating the edge. We need the
     * next vertex's id.
     */
    css->path_values = NIL;
    next_vertex_id = create_vertex(css, lfirst(next), lnext(next));

    /*
     * Set the start and end vertex ids
     */
    if (node->dir == CYPHER_REL_DIR_RIGHT)
    {
        // create pattern (prev_vertex)-[edge]->(next_vertex)
        start_id = prev_vertex_id;
        end_id = next_vertex_id;
    }
    else
    {
        // create pattern (prev_vertex)<-[edge]-(next_vertex)
        start_id = next_vertex_id;
        end_id = prev_vertex_id;
    }

    /*
     * Set estate's result relation to the vertex's result
     * relation.
     *
     * Note: This obliterates what was their previously
     */
    estate->es_result_relation_info = resultRelInfo;

    ExecClearTuple(elemTupleSlot);

    // Graph Id for the edge
    es = linitial(node->expr_states);
    id = ExecEvalExpr(es, econtext, &isNull);
    elemTupleSlot->tts_values[edge_tuple_id] = id;
    elemTupleSlot->tts_isnull[edge_tuple_id] = isNull;

    // Graph id for the starting vertex
    elemTupleSlot->tts_values[edge_tuple_start_id] = start_id;
    elemTupleSlot->tts_isnull[edge_tuple_start_id] = false;

    // Graph id for the ending vertex
    elemTupleSlot->tts_values[edge_tuple_end_id] = end_id;
    elemTupleSlot->tts_isnull[edge_tuple_end_id] = false;

    // Edge's properties map
    elemTupleSlot->tts_values[edge_tuple_properties] =
        scanTupleSlot->tts_values[node->prop_var_no];
    elemTupleSlot->tts_isnull[edge_tuple_properties] =
        scanTupleSlot->tts_isnull[node->prop_var_no];

    // Insert the new edge
    tuple = insert_entity_tuple(resultRelInfo, elemTupleSlot, estate);

    if (node->variable_name != NULL)
    {
        clause_tuple_information *tuple_info;

        tuple_info = palloc(sizeof(clause_tuple_information));

        tuple_info->tuple = tuple;
        tuple_info->name = node->variable_name;

        css->tuple_info = lappend(css->tuple_info, tuple_info);
    }

    /*
     * When the edge is used by clauses higher in the execution tree
     * we need to create an edge datum. When the edge is a variable,
     * add to the scantuple slot. When the edge is part of a path
     * variable, add to the list.
     */
    if (CYPHER_TARGET_NODE_OUTPUT(node->flags))
    {
        PlanState *ps = css->css.ss.ps.lefttree;
        TupleTableSlot *scantuple = ps->ps_ExprContext->ecxt_scantuple;
        Datum result;

        result = make_edge(
            id, start_id, end_id, CStringGetDatum(node->label_name),
            PointerGetDatum(scanTupleSlot->tts_values[node->prop_var_no]));

        if (CYPHER_TARGET_NODE_IN_PATH(node->flags))
        {
            prev_path = lappend(prev_path, DatumGetPointer(result));
            css->path_values = list_concat(prev_path, css->path_values);
        }
        if (CYPHER_TARGET_NODE_IS_VARIABLE(node->flags))
        {
            scantuple->tts_values[node->tuple_position - 1] = result;
            scantuple->tts_isnull[node->tuple_position - 1] = false;
        }
    }
}

/*
 * Creates the vertex entity, returns the vertex's id in case the caller is
 * the create_edge function.
 */
static Datum create_vertex(cypher_create_custom_scan_state *css,
                           cypher_target_node *node, ListCell *next)
{
    bool isNull;
    Datum id;
    EState *estate = css->css.ss.ps.state;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    ResultRelInfo *resultRelInfo = node->resultRelInfo;
    TupleTableSlot *elemTupleSlot = node->elemTupleSlot;
    TupleTableSlot *scanTupleSlot = econtext->ecxt_scantuple;

    Assert(node->type == LABEL_KIND_VERTEX);

    if (CYPHER_TARGET_NODE_INSERT_ENTITY(node->flags))
    {
        PlanState *ps = css->css.ss.ps.lefttree;
        TupleTableSlot *scantuple = ps->ps_ExprContext->ecxt_scantuple;
        ExprState *id_es = linitial(node->expr_states);
        HeapTuple tuple;

        /*
         * Set estate's result relation to the vertex's result
         * relation.
         *
         * Note: This obliterates what was their previously
         */
        estate->es_result_relation_info = resultRelInfo;

        ExecClearTuple(elemTupleSlot);

        id = ExecEvalExpr(id_es, econtext, &isNull);
        elemTupleSlot->tts_values[vertex_tuple_id] = id;
        elemTupleSlot->tts_isnull[vertex_tuple_id] = isNull;

        scantuple->tts_values[node->id_var_no - 1] = id;
        scantuple->tts_isnull[node->id_var_no - 1] = isNull;

        elemTupleSlot->tts_values[vertex_tuple_properties] =
            scanTupleSlot->tts_values[node->prop_var_no];
        elemTupleSlot->tts_isnull[vertex_tuple_properties] =
            scanTupleSlot->tts_isnull[node->prop_var_no];

        // Insert the new vertex
        tuple = insert_entity_tuple(resultRelInfo, elemTupleSlot, estate);

        if (node->variable_name != NULL)
        {
            clause_tuple_information *tuple_info;

            tuple_info = palloc(sizeof(clause_tuple_information));

            tuple_info->tuple = tuple;
            tuple_info->name = node->variable_name;

            css->tuple_info = lappend(css->tuple_info, tuple_info);
        }

        /*
         * When the vertex is used by clauses higher in the execution tree
         * we need to create a vertex datum. When the vertex is a variable,
         * add to the scantuple slot. When the vertex is part of a path
         * variable, add to the list.
         */
        if (CYPHER_TARGET_NODE_OUTPUT(node->flags))
        {
            TupleTableSlot *scantuple;
            PlanState *ps;
            Datum result;

            ps = css->css.ss.ps.lefttree;
            scantuple = ps->ps_ExprContext->ecxt_scantuple;

            result = make_vertex(
                id, CStringGetDatum(node->label_name),
                PointerGetDatum(scanTupleSlot->tts_values[node->prop_var_no]));

            if (CYPHER_TARGET_NODE_IN_PATH(node->flags))
                css->path_values = lappend(css->path_values, DatumGetPointer(result));

            if (CYPHER_TARGET_NODE_IS_VARIABLE(node->flags))
            {
                scantuple->tts_values[node->tuple_position - 1] = result;
                scantuple->tts_isnull[node->tuple_position - 1] = false;
            }
        }
    }
    else
    {
        if (CYPHER_TARGET_NODE_ID_IS_AGTYPE(node->flags))
        {
            agtype *a;
            agtype_value *v;

            a = (agtype *)scanTupleSlot->tts_values[node->id_var_no];

            v = get_ith_agtype_value_from_container(&a->root, 0);

            if (v->type != AGTV_INTEGER)
                ereport(ERROR,
                        (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                         errmsg("id agtype must resolve to a graphid")));

            id = GRAPHID_GET_DATUM(v->val.int_value);
        }
        else if (CYPHER_TARGET_NODE_ID_IS_GRAPHID(node->flags))
        {
            id = scanTupleSlot->tts_values[node->id_var_no - 1];
        }
        else
        {
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                            errmsg("id agtype must resolve to a graphid")));
        }

        if (CYPHER_TARGET_NODE_IN_PATH(node->flags))
        {
            Datum vertex = scanTupleSlot->tts_values[node->tuple_position - 1];
            css->path_values = lappend(css->path_values, DatumGetPointer(vertex));
        }
    }

    // If the path continues, create the next edge, passing the vertex's id.
    if (next != NULL)
    {
        create_edge(css, lfirst(next), id, lnext(next));
    }

    return id;
}

/*
 * Insert the edge/vertex tuple into the table and indices. If the table's
 * constraints have not been violated.
 */
static HeapTuple insert_entity_tuple(ResultRelInfo *resultRelInfo,
                                      TupleTableSlot *elemTupleSlot, EState *estate)
{
    HeapTuple tuple;

    ExecStoreVirtualTuple(elemTupleSlot);
    tuple = ExecMaterializeSlot(elemTupleSlot);

    // Check the constraints of the tuple
    tuple->t_tableOid = RelationGetRelid(resultRelInfo->ri_RelationDesc);
    if (resultRelInfo->ri_RelationDesc->rd_att->constr != NULL)
        ExecConstraints(resultRelInfo, elemTupleSlot, estate);

    // Insert the tuple normally
    heap_insert(resultRelInfo->ri_RelationDesc, tuple, estate->es_output_cid,
                0, NULL);

    // Insert index entries for the tuple
    if (resultRelInfo->ri_NumIndices > 0)
        ExecInsertIndexTuples(elemTupleSlot, &(tuple->t_self), estate, false,
                              NULL, NIL);

    return tuple;
}
