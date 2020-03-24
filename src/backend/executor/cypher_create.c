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
#include "executor/tuptable.h"
#include "nodes/execnodes.h"
#include "nodes/extensible.h"
#include "nodes/nodes.h"
#include "nodes/plannodes.h"
#include "parser/parse_relation.h"
#include "rewrite/rewriteHandler.h"
#include "utils/rel.h"

#include "executor/cypher_executor.h"
#include "nodes/cypher_nodes.h"

typedef struct cypher_create_custom_scan_state
{
    CustomScanState css;
    List *pattern;
} cypher_create_custom_scan_state;

static void begin_cypher_create(CustomScanState *node, EState *estate,
                                int eflags);
static TupleTableSlot *exec_cypher_create(CustomScanState *node);
static void end_cypher_create(CustomScanState *node);

void create_vertex(cypher_create_custom_scan_state *css,
                   cypher_target_node *node);

const CustomExecMethods cypher_create_exec_methods = {"Cypher Create",
                                                      begin_cypher_create,
                                                      exec_cypher_create,
                                                      end_cypher_create,
                                                      NULL,
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

    ExecAssignExprContext(estate, &node->ss.ps);

    foreach (lc, css->pattern)
    {
        ListCell *lc2;
        List *path = lfirst(lc);
        foreach (lc2, path)
        {
            cypher_target_node *cypher_node =
                (cypher_target_node *)lfirst(lc2);
            ListCell *lc_expr;
            Relation rel;

            /*
             * Open relation
             */
            rel = heap_open(cypher_node->relid, RowExclusiveLock);

            /*
             * initialize resultRelInfo for the vertex
             */
            cypher_node->resultRelInfo = palloc(sizeof(ResultRelInfo));
            InitResultRelInfo(cypher_node->resultRelInfo, rel,
                              list_length(estate->es_range_table), NULL,
                              estate->es_instrument);

            /*
             * Open all indexes for the relation
             */
            ExecOpenIndices(cypher_node->resultRelInfo, false);

            /*
             * Setup the relation's tuple slot
             */
            cypher_node->elemTupleSlot = ExecInitExtraTupleSlot(
                estate,
                RelationGetDescr(cypher_node->resultRelInfo->ri_RelationDesc));

            /*
             * setup expr states for the relation's target list
             */
            foreach (lc_expr, cypher_node->targetList)
            {
                TargetEntry *te = lfirst(lc_expr);

                cypher_node->expr_states =
                    lappend(cypher_node->expr_states,
                            ExecInitExpr(te->expr, (PlanState *)node));
            }
        }
    }
}

static TupleTableSlot *exec_cypher_create(CustomScanState *node)
{
    cypher_create_custom_scan_state *css =
        (cypher_create_custom_scan_state *)node;
    ListCell *lc2;
    ResultRelInfo *saved_resultRelInfo;
    TupleTableSlot *slot = node->ss.ps.ps_ResultTupleSlot;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    EState *estate = css->css.ss.ps.state;

    ResetExprContext(econtext);

    /*
     * Save estate's active result relation
     */
    saved_resultRelInfo = estate->es_result_relation_info;

    foreach (lc2, css->pattern)
    {
        List *path = lfirst(lc2);

        ListCell *lc = list_head(path);

        create_vertex(css, lfirst(lc));
    }

    /*
     * Restore estate's previous result relation
     */
    estate->es_result_relation_info = saved_resultRelInfo;

    return slot;
}

static void end_cypher_create(CustomScanState *node)
{
    cypher_create_custom_scan_state *css =
        (cypher_create_custom_scan_state *)node;
    ListCell *lc;

    foreach (lc, css->pattern)
    {
        List *path = lfirst(lc);
        ListCell *lc2;
        foreach (lc2, path)
        {
            cypher_target_node *cypher_node =
                (cypher_target_node *)lfirst(lc2);

            /*
             * close all indices for the node
             */
            ExecCloseIndices(cypher_node->resultRelInfo);

            /*
             * close the relation itself
             */
            heap_close(cypher_node->resultRelInfo->ri_RelationDesc,
                       RowExclusiveLock);
        }
    }
}

Node *create_cypher_create_plan_state(CustomScan *cscan)
{
    cypher_create_custom_scan_state *cypher_css =
        palloc0(sizeof(cypher_create_custom_scan_state));

    cypher_css->pattern = linitial(cscan->custom_private);

    cypher_css->css.ss.ps.type = T_CustomScanState;
    cypher_css->css.methods = &cypher_create_exec_methods;

    return (Node *)cypher_css;
}

void create_vertex(cypher_create_custom_scan_state *css,
                   cypher_target_node *node)
{
    bool isNull;
    EState *estate = css->css.ss.ps.state;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    HeapTuple tuple;
    int i = 0;
    ListCell *lc;
    ResultRelInfo *resultRelInfo = node->resultRelInfo;
    TupleTableSlot *elemTupleSlot = node->elemTupleSlot;

    /*
     * Set estate's result relation to the vertex's result
     * relation.
     *
     * Note: This obliterates what was their previously
     */
    estate->es_result_relation_info = resultRelInfo;

    ExecClearTuple(elemTupleSlot);

    /*
     * Add the values to the elemTupleSlot
     */
    foreach (lc, node->expr_states)
    {
        ExprState *es = lfirst(lc);

        elemTupleSlot->tts_values[i] = ExecEvalExpr(es, econtext, &isNull);
        elemTupleSlot->tts_isnull[i] = isNull;

        i++;
    }

    ExecStoreVirtualTuple(elemTupleSlot);
    tuple = ExecMaterializeSlot(elemTupleSlot);

    /*
     * check the constraints of the tuple
     */
    tuple->t_tableOid = RelationGetRelid(resultRelInfo->ri_RelationDesc);
    if (resultRelInfo->ri_RelationDesc->rd_att->constr != NULL)
        ExecConstraints(resultRelInfo, elemTupleSlot, estate);

    /*
     * insert the tuple normally
     */
    heap_insert(resultRelInfo->ri_RelationDesc, tuple, estate->es_output_cid,
                0, NULL);

    /*
     * insert index entries for the tuple
     */
    if (resultRelInfo->ri_NumIndices > 0)
        ExecInsertIndexTuples(elemTupleSlot, &(tuple->t_self), estate, false,
                              NULL, NIL);
}
