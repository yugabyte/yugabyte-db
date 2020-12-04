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
#include "access/sysattr.h"
#include "nodes/extensible.h"
#include "nodes/makefuncs.h"
#include "nodes/nodes.h"
#include "nodes/nodeFuncs.h"
#include "nodes/plannodes.h"
#include "parser/parsetree.h"
#include "parser/parse_relation.h"
#include "utils/rel.h"

#include "catalog/ag_label.h"
#include "commands/label_commands.h"
#include "executor/cypher_utils.h"
#include "utils/agtype.h"
#include "utils/graphid.h"

static bool find_scan_state_walker(PlanState *p, void *context);
static bool inspect_clause_tuple_info(List *tuple_info, char *var_name);

typedef struct find_scan_state_context {
    char *var_name;
    PlanState *variable_plan_state;
    EState *estate;
} find_scan_state_context;

/*
 * This function will find the scan state that a variable name
 * is referencing.
 */
PlanState *find_plan_state(CustomScanState *node, char *var_name)
{
    PlanState *ps;
    find_scan_state_context *context;

    context = palloc(sizeof(find_scan_state_context));
    context->var_name = var_name;
    context->estate = node->ss.ps.state;

    planstate_tree_walker((PlanState *)node, find_scan_state_walker, context);

    ps = context->variable_plan_state;

    if (ps == NULL)
        ereport(ERROR,
            (errmsg("cannot find plan state for variable '%s'", var_name)));

    return ps;
}


static bool find_scan_state_walker(PlanState *p, void *context)
{
    find_scan_state_context *cnxt = (find_scan_state_context *)context;
    EState *estate = cnxt->estate;
    char *var_name = cnxt->var_name;

    switch (p->type)
    {
        case T_CustomScanState:
        {
            CustomScanState *css = (CustomScanState *)p;
            const struct CustomExecMethods *methods = css->methods;

            if (!strcmp(methods->CustomName, "Cypher Create"))
            {
                cypher_create_custom_scan_state *custom_state = (cypher_create_custom_scan_state *)css;
                if (inspect_clause_tuple_info(custom_state->tuple_info, var_name))
                {
                    cnxt->variable_plan_state = (PlanState *)p;
                    return true;
                }
            }
            else if (!strcmp(methods->CustomName, "Cypher Set"))
            {
                cypher_set_custom_scan_state *custom_state = (cypher_set_custom_scan_state *)css;
                if (inspect_clause_tuple_info(custom_state->tuple_info, var_name))
                {
                    cnxt->variable_plan_state = (PlanState *)p;
                    return true;
                }
            }
            break;
        }
        case T_AppendState:
        {
            if (planstate_tree_walker(p, find_scan_state_walker, context))
            {
                AppendState *append = (AppendState *)p;
                cnxt->variable_plan_state = (PlanState *)append->appendplans[append->as_whichplan];
                return true;
            }
            else
            {
                return false;
            }
        }
        case T_SeqScanState:
        {
            SeqScanState *seq = (SeqScanState *)p;
            SeqScan *scan = (SeqScan *)seq->ss.ps.plan;
            RangeTblEntry *rte = rt_fetch(scan->scanrelid, estate->es_range_table);
            Alias *alias = rte->alias;

            if (!strcmp(alias->aliasname, var_name) && seq->ss.ss_ScanTupleSlot->tts_tuple != NULL)
            {
                cnxt->variable_plan_state = (PlanState *)seq;
                return true;
            }

            return false;
        }
        default:
            break;
    }

    return planstate_tree_walker(p, find_scan_state_walker, context);
}

HeapTuple get_heap_tuple(CustomScanState *node, char *var_name)
{
    PlanState *ps = find_plan_state(node, var_name);
    HeapTuple heap_tuple;

    switch (ps->type)
    {
        case T_CustomScanState:
        {
            CustomScanState *css = (CustomScanState *)ps;
            const struct CustomExecMethods *methods = css->methods;
            List *tuple_info;
            ListCell *lc;

            if (!strcmp(methods->CustomName, "Cypher Create"))
            {
                cypher_create_custom_scan_state *css = (cypher_create_custom_scan_state *)ps;
                tuple_info = css->tuple_info;
            }
            else if (!strcmp(methods->CustomName, "Cypher Set"))
            {
                cypher_set_custom_scan_state *css = (cypher_set_custom_scan_state *)ps;
                tuple_info = css->tuple_info;
            }
            else
            {
                ereport(ERROR, (errmsg("cannot extract tuple information from %s", methods->CustomName)));
            }

            foreach(lc, tuple_info)
            {
                clause_tuple_information *info = lfirst(lc);

                if (!strcmp(info->name, var_name))
                {
                    heap_tuple = info->tuple;
                    break;
                }
            }
            break;
        }
        case T_SeqScanState:
        {
            bool isNull;
            TupleTableSlot *ss_tts = ((SeqScanState *)ps)->ss.ss_ScanTupleSlot;

            if (!ss_tts->tts_tuple)
                ereport(ERROR, (errmsg("cypher update clause needs physical tuples")));

            heap_getsysattr(ss_tts->tts_tuple, SelfItemPointerAttributeNumber,
                            ss_tts->tts_tupleDescriptor, &isNull);

            if (isNull)
                ereport(ERROR, (errmsg("cypher cannot find entity to update")));

            heap_tuple = ss_tts->tts_tuple;
            break;
        }
        default:
            ereport(ERROR, (errmsg("cannot extract heap tuple from scan state")));
            break;
    }

    return heap_tuple;
}

/*static bool inspect_create_clause_tuple_info(cypher_create_custom_scan_state *css, char *var_name)
{
    ListCell *lc;

    foreach(lc, css->tuple_info)
    {
        create_clause_tuple_information *info = lfirst(lc);

        if (!strcmp(info->name, var_name))
            return true;
    }
    return false;
}*/

static bool inspect_clause_tuple_info(List *tuple_info, char *var_name)
{
    ListCell *lc;

    foreach(lc, tuple_info)
    {
        clause_tuple_information *info = lfirst(lc);

        if (!strcmp(info->name, var_name))
            return true;
    }
    return false;
}

ResultRelInfo *create_entity_result_rel_info(CustomScanState *node, char *graph_name, char *label_name)
{
    EState *estate = node->ss.ps.state;
    Oid relid;
    RangeVar *rv;
    Relation label_relation, rel;
    ResultRelInfo *resultRelInfo;

    ParseState *pstate = make_parsestate(NULL);

    if (strlen(label_name) == 0)
        rv = makeRangeVar(graph_name, AG_DEFAULT_LABEL_VERTEX, -1);
    else
        rv = makeRangeVar(graph_name, label_name, -1);
    label_relation = parserOpenTable(pstate, rv, RowExclusiveLock);

    relid = RelationGetRelid(label_relation);

    heap_close(label_relation, NoLock);

    free_parsestate(pstate);

    rel = heap_open(relid, RowExclusiveLock);
    resultRelInfo = palloc(sizeof(ResultRelInfo));

    InitResultRelInfo(resultRelInfo, rel,
                      list_length(estate->es_range_table), NULL,
                      estate->es_instrument);

    return resultRelInfo;
}

ItemPointer get_self_item_pointer(TupleTableSlot *tts)
{
    ItemPointer ip;
    Datum d;
    bool isNull;

    if (!tts->tts_tuple)
        ereport(ERROR, (errmsg("cypher update clause needs physical tuples")));

    d = heap_getsysattr(tts->tts_tuple, SelfItemPointerAttributeNumber,
                        tts->tts_tupleDescriptor, &isNull);

    if (isNull)
        ereport(ERROR, (errmsg("cypher cannot find entity to update")));

    ip = (ItemPointer)DatumGetPointer(d);

    return ip;
}

TupleTableSlot *populate_vertex_tts(
    TupleTableSlot *elemTupleSlot, agtype_value *id, agtype_value *properties)
{
    bool properties_isnull;

    if (id == NULL)
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("vertex id field cannot be NULL")));

    properties_isnull = properties == NULL;

    elemTupleSlot->tts_values[vertex_tuple_id] = GRAPHID_GET_DATUM(id->val.int_value);
    elemTupleSlot->tts_isnull[vertex_tuple_id] = false;

    elemTupleSlot->tts_values[vertex_tuple_properties] =
        AGTYPE_P_GET_DATUM(agtype_value_to_agtype(properties));
    elemTupleSlot->tts_isnull[vertex_tuple_properties] = properties_isnull;

    return elemTupleSlot;
}

TupleTableSlot *populate_edge_tts(
    TupleTableSlot *elemTupleSlot, agtype_value *id, agtype_value *startid,
    agtype_value *endid, agtype_value *properties)
{
    bool properties_isnull;

    if (id == NULL)
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("edge id field cannot be NULL")));
    if (startid == NULL)
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("edge start_id field cannot be NULL")));

    if (endid == NULL)
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("edge end_id field cannot be NULL")));


    properties_isnull = properties == NULL;

    elemTupleSlot->tts_values[edge_tuple_id] =
        GRAPHID_GET_DATUM(id->val.int_value);
    elemTupleSlot->tts_isnull[edge_tuple_id] = false;

    elemTupleSlot->tts_values[edge_tuple_start_id] =
        GRAPHID_GET_DATUM(startid->val.int_value);
    elemTupleSlot->tts_isnull[edge_tuple_start_id] = false;

    elemTupleSlot->tts_values[edge_tuple_end_id] =
        GRAPHID_GET_DATUM(endid->val.int_value);
    elemTupleSlot->tts_isnull[edge_tuple_end_id] = false;

    elemTupleSlot->tts_values[edge_tuple_properties] =
        AGTYPE_P_GET_DATUM(agtype_value_to_agtype(properties));
    elemTupleSlot->tts_isnull[edge_tuple_properties] = properties_isnull;

    return elemTupleSlot;
}
