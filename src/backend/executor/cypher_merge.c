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

#include "catalog/ag_label.h"
#include "executor/cypher_executor.h"
#include "executor/cypher_utils.h"
#include "utils/datum.h"

/*
 * The following structure is used to hold a single vertex or edge component
 * of a path. The smallest path is just a single vertex.
 *
 * Note: This structure is only useful for paths when stored in a dynamic
 *       array.
 */
typedef struct path_entry
{
    bool actual;                /* actual tuple passed in for a vertex */
    cypher_rel_dir direction;   /* the direction for the edge */
    graphid id;                 /* id of the vertex or edge */
    bool id_isNull;             /* id isNull */
    graphid start_id;           /* edge start id */
    graphid end_id;             /* edge end id */
    Oid label;                  /* label oid */
    Datum prop;                 /* properties datum */
    bool prop_isNull;           /* properties isNull */
    uint32 dih;                 /* datum_image_hash of properties datum */
} path_entry;

/*
 * The following structure is used to hold a path_entry in a linked list.
 *
 * Note: The path_entry is stored as a pointer to a pointer. In this case
 *       the **path_entry is a dynamic array of path_entry elements.
 */
typedef struct created_path
{
    struct created_path *next;  /* next link in linked list of path_entrys */
    struct path_entry **entry;  /* path_entry array for this link */
} created_path;

static void begin_cypher_merge(CustomScanState *node, EState *estate,
                               int eflags);
static TupleTableSlot *exec_cypher_merge(CustomScanState *node);
static void end_cypher_merge(CustomScanState *node);
static void rescan_cypher_merge(CustomScanState *node);
static Datum merge_vertex(cypher_merge_custom_scan_state *css,
                          cypher_target_node *node, ListCell *next, List* list,
                          path_entry **path_array, int path_index,
                          bool should_insert);
static void merge_edge(cypher_merge_custom_scan_state *css,
                       cypher_target_node *node, Datum prev_vertex_id,
                       ListCell *next, List *list,
                       path_entry **path_array, int path_index,
                       bool should_insert);
static void process_simple_merge(CustomScanState *node);
static bool check_path(cypher_merge_custom_scan_state *css,
                       TupleTableSlot *slot);
static void process_path(cypher_merge_custom_scan_state *css,
                         path_entry **path_array, bool should_insert);
static void mark_tts_isnull(TupleTableSlot *slot);

const CustomExecMethods cypher_merge_exec_methods = {MERGE_SCAN_STATE_NAME,
                                                     begin_cypher_merge,
                                                     exec_cypher_merge,
                                                     end_cypher_merge,
                                                     rescan_cypher_merge,
                                                     NULL, NULL, NULL, NULL,
                                                     NULL, NULL, NULL, NULL};

static path_entry **prebuild_path(CustomScanState *node);
static bool compare_2_paths(path_entry **lhs, path_entry **rhs,
                            int path_length);
static path_entry **find_duplicate_path(CustomScanState *node,
                                        path_entry **path_array);
static void free_path_entry_array(path_entry **path_array, int length);

/*
 * Initializes the MERGE Execution Node at the beginning of the execution
 * phase.
 */
static void begin_cypher_merge(CustomScanState *node, EState *estate,
                               int eflags)
{
    cypher_merge_custom_scan_state *css =
        (cypher_merge_custom_scan_state *)node;
    ListCell *lc = NULL;
    Plan *subplan = NULL;
    css->created_paths_list = NULL;

    Assert(list_length(css->cs->custom_plans) == 1);

    /* initialize the subplan */
    subplan = linitial(css->cs->custom_plans);
    node->ss.ps.lefttree = ExecInitNode(subplan, estate, eflags);

    /* TODO is this necessary? Removing it seems to not have an impact */
    ExecAssignExprContext(estate, &node->ss.ps);

    ExecInitScanTupleSlot(estate, &node->ss,
                          ExecGetResultType(node->ss.ps.lefttree),
                          &TTSOpsVirtual);

    /*
     * When MERGE is not the last clause in a cypher query. Setup projection
     * information to pass to the parent execution node.
     */
    if (!CYPHER_CLAUSE_IS_TERMINAL(css->flags))
    {
        TupleDesc tupdesc = node->ss.ss_ScanTupleSlot->tts_tupleDescriptor;

        ExecAssignProjectionInfo(&node->ss.ps, tupdesc);
    }

    /*
     * For each vertex and edge in the path, setup the information
     * needed if we need to create them.
     */
    foreach(lc, css->path->target_nodes)
    {
        cypher_target_node *cypher_node =
            (cypher_target_node *)lfirst(lc);
        Relation rel = NULL;

        /*
         * This entity is references an entity that is already declared. Either
         * by a previous clause or an entity earlier in the MERGE path. In both
         * cases, this target_entry will not create data, only reference data
         * that already exists.
         */
        if (!CYPHER_TARGET_NODE_INSERT_ENTITY(cypher_node->flags))
        {
            continue;
        }

        /* Open relation and acquire a row exclusive lock. */
        rel = table_open(cypher_node->relid, RowExclusiveLock);

        /* Initialize resultRelInfo for the vertex */
        cypher_node->resultRelInfo = makeNode(ResultRelInfo);
        InitResultRelInfo(cypher_node->resultRelInfo, rel,
                          list_length(estate->es_range_table), NULL,
                          estate->es_instrument);

        /* Open all indexes for the relation */
        ExecOpenIndices(cypher_node->resultRelInfo, false);

        /* Setup the relation's tuple slot */
        cypher_node->elemTupleSlot = ExecInitExtraTupleSlot(
            estate,
            RelationGetDescr(cypher_node->resultRelInfo->ri_RelationDesc),
            &TTSOpsHeapTuple);

        if (cypher_node->id_expr != NULL)
        {
            cypher_node->id_expr_state =
                ExecInitExpr(cypher_node->id_expr, (PlanState *)node);
        }

        if (cypher_node->prop_expr != NULL)
        {
            cypher_node->prop_expr_state = ExecInitExpr(cypher_node->prop_expr,
                                                        (PlanState *)node);
        }
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

    /* store the currentCommandId for this instance */
    css->base_currentCommandId = GetCurrentCommandId(false);

    Increment_Estate_CommandId(estate);
}

/*
 * Checks the subtree to see if the lateral join representing the MERGE path
 * found results. Returns true if the path does not exist and must be created,
 * false otherwise.
 */
static bool check_path(cypher_merge_custom_scan_state *css,
                       TupleTableSlot *slot)
{
    cypher_create_path *path = css->path;
    ListCell *lc = NULL;

    foreach(lc, path->target_nodes)
    {
        cypher_target_node *node = lfirst(lc);

        /*
         * If target_node as a valid attribute number and is a node not
         * declared in a previous clause, check the tuple position in the
         * slot. If the slot is null, the path was not found. The rules
         * state that if one part of the path does not exists, the whole
         * path must be created.
         */
        if (node->tuple_position != InvalidAttrNumber ||
            ((node->flags & CYPHER_TARGET_NODE_MERGE_EXISTS) == 0))
        {
            /*
             * Attribute number is 1 indexed and tts_values is 0 indexed,
             * offset by 1.
             */
            if (slot->tts_isnull[node->tuple_position - 1])
            {

                return true;
            }
        }
    }
    return false;
}

static void process_path(cypher_merge_custom_scan_state *css,
                         path_entry **path_array, bool should_insert)
{
    cypher_create_path *path = css->path;
    List *list = path->target_nodes;
    ListCell *lc = list_head(list);

    /*
     * Create the first vertex. The create_vertex function will
     * create the rest of the path, if necessary.
     */
    merge_vertex(css, lfirst(lc), lnext(list, lc), list,
                 path_array, 0, should_insert);

    /*
     * If this path is a variable, take the list that was accumulated
     * in the vertex/edge creation, create a path datum, and add to the
     * scantuple slot.
     */
    if (path->path_attr_num != InvalidAttrNumber)
    {
        ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
        TupleTableSlot *scantuple = econtext->ecxt_scantuple;
        Datum result;
        int tuple_position = path->path_attr_num - 1;
        bool debug_flag = false;

        /*
         * We need to make sure that the tuple_position is within the
         * boundaries of the tuple's number of attributes. Otherwise, it
         * will corrupt memory. The cases where it doesn't fit within are
         * usually due to a variable that is specified but there isn't a RETURN
         * clause. In these cases we just don't bother to store the
         * value.
         */
         if (!debug_flag &&
             (tuple_position < scantuple->tts_tupleDescriptor->natts ||
              scantuple->tts_tupleDescriptor->natts != 1))
        {
            result = make_path(css->path_values);

            /* store the result */
            scantuple->tts_values[tuple_position] = result;
            scantuple->tts_isnull[tuple_position] = false;
        }
    }
}

/*
 * Function that handles the case where MERGE is the only clause in the query.
 */
static void process_simple_merge(CustomScanState *node)
{
    cypher_merge_custom_scan_state *css =
        (cypher_merge_custom_scan_state *)node;
    EState *estate = css->css.ss.ps.state;
    TupleTableSlot *slot = NULL;

    /*Process the subtree first */
    Decrement_Estate_CommandId(estate)
    slot = ExecProcNode(node->ss.ps.lefttree);
    Increment_Estate_CommandId(estate)

    if (TupIsNull(slot))
    {
        ExprContext *econtext = node->ss.ps.ps_ExprContext;
        SubqueryScanState *sss = (SubqueryScanState *)node->ss.ps.lefttree;

        /* our child execution node should be a subquery */
        Assert(IsA(sss, SubqueryScanState));

        /* setup the scantuple that the process_path needs */
        econtext->ecxt_scantuple = sss->ss.ss_ScanTupleSlot;

        process_path(css, NULL, true);
    }
}

/*
 * Iterate through the TupleTableSlot's tts_values and marks the isnull field
 * with true.
 */
static void mark_tts_isnull(TupleTableSlot *slot)
{
    int numberOfAttributes = slot->tts_tupleDescriptor->natts;
    int i;

    for (i = 0; i < numberOfAttributes; i++)
    {
        Datum val;

        val = slot->tts_values[i];

        if (val == (Datum)NULL)
        {
            slot->tts_isnull[i] = true;
        }
    }
}

/* helper function to free a path_entry array given its length */
static void free_path_entry_array(path_entry **path_array, int length)
{
    int index;

    for (index = 0; index < length; index++)
    {
        pfree(path_array[index]);
    }
}

/*
 * Helper function to prebuild a path. The user needs to free the returned
 * path_entry when done.
 *
 * Note: The prebuilt path and its components are not filled out completely by
 *       this function. merge_vertex and merge_edge will/should fill out the
 *       rest. This is because the ID fields autoincrement the next available ID
 *       when evaluated AND the generated prebuilt path might not be used.
 */
static path_entry **prebuild_path(CustomScanState *node)
{
    cypher_merge_custom_scan_state *css =
        (cypher_merge_custom_scan_state *)node;
    List *nodes = css->path->target_nodes;
    int path_length = list_length(nodes);
    ListCell *lc = NULL;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    int counter = 0;

    path_entry **path_array = NULL;
    path_array = palloc0(sizeof(path_entry *) * path_length);

    /* iterate through the path, partially prebuilding it */
    foreach (lc, nodes)
    {
        /* get the node/edge and allocate the memory needed */
        cypher_target_node *node = lfirst(lc);
        path_entry *entry = palloc0(sizeof(path_entry));

        /* if this isn't an actual passed in tuple */
        if (CYPHER_TARGET_NODE_INSERT_ENTITY(node->flags))
        {
            bool isNull = false;

            entry->actual = false;
            entry->id = 0;
            entry->id_isNull = true;
            entry->direction = node->dir;
            entry->label = node->relid;
            entry->prop = ExecEvalExprSwitchContext(node->prop_expr_state,
                                                          econtext, &isNull);
            entry->prop_isNull = isNull;
            entry->dih = datum_image_hash(entry->prop, false, -1);
        }
        /* otherwise, it is */
        else
        {
            EState *estate = css->css.ss.ps.state;
            TupleTableSlot *scanTupleSlot = econtext->ecxt_scantuple;

            agtype *agt = NULL;
            Datum d;
            agtype_value *agtv_vertex = NULL;
            agtype_value *agtv_id = NULL;

            /* check that the variable isn't NULL */
            if (scanTupleSlot->tts_isnull[node->tuple_position - 1])
            {
                ereport(ERROR,
                        (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                         errmsg("Existing variable %s cannot be NULL in MERGE clause",
                                node->variable_name)));
            }

            /* get the vertex agtype in the scanTupleSlot */
            d = scanTupleSlot->tts_values[node->tuple_position - 1];
            agt = DATUM_GET_AGTYPE_P(d);

            /* Convert to an agtype value */
            agtv_vertex = get_ith_agtype_value_from_container(&agt->root, 0);

            if (agtv_vertex->type != AGTV_VERTEX)
            {
                ereport(ERROR,
                        (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                         errmsg("agtype must resolve to a vertex")));
            }

            /* extract the id agtype field */
            agtv_id = GET_AGTYPE_VALUE_OBJECT_VALUE(agtv_vertex, "id");

            /* set the necessary entry fields - actual & id */
            entry->actual = true;
            entry->id = (graphid) agtv_id->val.int_value;
            entry->id_isNull = false;
            entry->prop = 0;
            entry->prop_isNull = true;
            entry->dih = 0;

            if (!SAFE_TO_SKIP_EXISTENCE_CHECK(node->flags))
            {
                if (!entity_exists(estate, css->graph_oid, entry->id))
                {
                    ereport(ERROR,
                            (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                             errmsg("vertex assigned to variable %s was deleted",
                                    node->variable_name)));
                }
            }
        }

        /* save the pointer and move to the next */
        path_array[counter++] = entry;
    }

    return path_array;
}

/*
 * Helper function to compare 2 paths. By definition, paths don't know
 * specifics, so this comparison is somewhat generic.
 */
static bool compare_2_paths(path_entry **lhs, path_entry **rhs, int path_length)
{
    int i;

    /* iterate through the entire path, returning false for any mismatch */
    for (i = 0; i < path_length; i++)
    {
        /* if these are actual vertices (passed in from a variable) */
        if (lhs[i]->actual == rhs[i]->actual &&
            lhs[i]->actual == true)
        {
            /* just check the IDs */
            if (lhs[i]->id != rhs[i]->id)
            {
                return false;
            }
            else
            {
                continue;
            }
        }

        /* are the labels the same */
        if (lhs[i]->label != rhs[i]->label)
        {
            return false;
        }

        /* are the directions the same */
        if (lhs[i]->direction != rhs[i]->direction)
        {
            return false;
        }

        /* are the properties datum hashes the same */
        if (lhs[i]->dih != rhs[i]->dih)
        {
            return false;
        }

        /* are the properties datum images the same */
        if (!datum_image_eq(lhs[i]->prop, rhs[i]->prop, false, -1))
        {
            return false;
        }
    }

    /* no mismatches so it must match */
    return true;
}

/* helper function to find a duplicate path in the created_paths_list */
static path_entry **find_duplicate_path(CustomScanState *node,
                                         path_entry **path_array)
{
    cypher_merge_custom_scan_state *css =
        (cypher_merge_custom_scan_state *)node;
    int path_length = list_length(css->path->target_nodes);

    /* if the list is NULL just return NULL */
    if (css->created_paths_list == NULL)
    {
        return NULL;
    }
    /* otherwise, check to see if the path already exists */
    else
    {
        /* set to the top of the list */
        created_path *curr_path = css->created_paths_list;

        /* iterate through our list of created paths */
        while (curr_path != NULL)
        {
            /* if we have found the entry, return it */
            if (compare_2_paths(path_array, curr_path->entry, path_length))
            {
                return curr_path->entry;
            }

            /* otherwise, get the next path */
            curr_path = curr_path->next;
        }
    }

    /* if we didn't find it, return NULL */
    return NULL;
}

/*
 * Function that is called mid-execution. This function will call
 * its subtree in the execution tree, and depending on the results
 * create the new path, and depending on the context of the MERGE
 * within the query pass data to the parent execution node.
 *
 * Returns a TupleTableSlot with the next tuple to it parent or
 * Returns NULL when MERGE has no more tuples to emit.
 */
static TupleTableSlot *exec_cypher_merge(CustomScanState *node)
{
    cypher_merge_custom_scan_state *css =
        (cypher_merge_custom_scan_state *)node;
    EState *estate = css->css.ss.ps.state;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    TupleTableSlot *slot = NULL;
    bool terminal = CYPHER_CLAUSE_IS_TERMINAL(css->flags);

    /*
     * There are three cases that dictate the flow of the execution logic.
     *
     * 1. MERGE is not the first clause in the cypher query.
     * 2. MERGE is the first clause and there are no following clauses.
     * 3. MERGE is the first clause and there are following clauses.
     * CYPHER_CLAUSE_FLAG_PREVIOUS_CLAUSE
     */
    if (CYPHER_CLAUSE_HAS_PREVIOUS_CLAUSE(css->flags))
    {
        /*
         * Case 1: MERGE is not the first clause in the cypher query.
         *
         * For this case, we need to process all tuples given to us by the
         * previous clause. When we receive a tuple from the previous clause:
         * check to see if the left lateral join found the pattern already. If
         * it did, we don't need to create the pattern. If the lateral join did
         * not find the whole path, create the whole path.
         *
         * If this is a terminal clause, process all tuples. If not, pass the
         * tuple to up the execution tree.
         */
        do
        {
            /*Process the subtree first */
            Decrement_Estate_CommandId(estate)
            slot = ExecProcNode(node->ss.ps.lefttree);
            Increment_Estate_CommandId(estate)

            /*
             * We are done processing the subtree, mark as terminal
             * so the function returns NULL.
             */
            if (TupIsNull(slot))
            {
                terminal = true;
                break;
            }

            /* setup the scantuple that the process_path needs */
            econtext->ecxt_scantuple =
                node->ss.ps.lefttree->ps_ProjInfo->pi_exprContext->ecxt_scantuple;

            /*
             * Check the subtree to see if the lateral join representing the
             * MERGE path found results. If not, we need to create the path
             */
            if (check_path(css, econtext->ecxt_scantuple))
            {
                path_entry **prebuilt_path_array = NULL;
                path_entry **found_path_array = NULL;
                int path_length = list_length(css->path->target_nodes);

                /*
                 * Prebuild our path and verify that it wasn't already created.
                 *
                 * Note: This is currently only needed when there is a previous
                 *       clause. This is due to the fact that MERGE can't see
                 *       what it has just created. This isn't due to transaction
                 *       or command ids, it's due to the join's scan not being
                 *       able to add in the newly inserted tuples and rescan
                 *       with these tuples.
                 *
                 * Note: The prebuilt path is purposely generic as it needs to
                 *       only match a path. The more specific items will be
                 *       added by merge_vertex and merge_edge if it is inserted.
                 *
                 * Note: The IDs are purposely not created here because we may
                 *       need to throw them away if a path was previously
                 *       created. Remember, the IDs are automatically
                 *       incremented when fetched.
                 */
                prebuilt_path_array = prebuild_path(node);

                found_path_array = find_duplicate_path(node,
                                                       prebuilt_path_array);

                /* if found we don't need to insert anything, just reuse it */
                if (found_path_array)
                {
                    /* we don't need our prebuilt path anymore */
                    free_path_entry_array(prebuilt_path_array, path_length);

                    /* as this path exists, we don't need to insert it */
                    process_path(css, found_path_array, false);
                }
                /* otherwise, we need to insert the new, prebuilt, path */
                else
                {
                    created_path *new_path = palloc0(sizeof(created_path));

                    /* build the next linked list entry for our created_paths */
                    new_path = palloc0(sizeof(created_path));
                    new_path->next = css->created_paths_list;
                    new_path->entry = prebuilt_path_array;

                    /* we need to push our prebuilt path onto the list */
                    css->created_paths_list = new_path;

                    /*
                     * We need to pass in the prebuilt path so that it can get
                     * filled in with more specific information
                     */
                    process_path(css, prebuilt_path_array, true);
                }
            }

        } while (terminal);

        /* if this was a terminal MERGE just return NULL */
        if (terminal)
        {
            return NULL;
        }

        econtext->ecxt_scantuple = ExecProject(node->ss.ps.lefttree->ps_ProjInfo);

        return ExecProject(node->ss.ps.ps_ProjInfo);

    }
    else if (terminal)
    {
        /*
         * Case 2: MERGE is the first clause and there are no following clauses
         *
         * For case 2, check to see if we found the pattern, if not create it.
         * Return NULL in either cases, because no rows are expected.
         */
        process_simple_merge(node);

        /*
         * Case 2 always returns NULL the first time exec_cypher_merge is
         * called.
         */
        return NULL;
    }
    else
    {
        /*
         * Case 3: MERGE is the first clause and there are following clauses.
         *
         * Case three has two subcases:
         *
         * 1. The already path exists.
         * 2. The path does not exist.
         */

        /*
         * Part of Case 2.
         *
         * If created_new_path is marked as true. The path did not exist and
         * MERGE created it. We have already passed that information up the
         * execution tree, and now we tell MERGE's parents in the execution
         * tree there is no more tuples to pass.
         */
        if (css->created_new_path)
        {
            /*
             * If the created_new_path is set to true. Then MERGE should not
             * have found a path, because this should only be set to true if
             * merge found sub-case 1
             */
            Assert(css->found_a_path == false);

            return NULL;
        }

        /*
         * Process the subtree. The subtree will only consist of the MERGE
         * path.
         */
        Decrement_Estate_CommandId(estate)
        slot = ExecProcNode(node->ss.ps.lefttree);
        Increment_Estate_CommandId(estate)

        if (!TupIsNull(slot))
        {
            /*
             * Part of Sub-Case 1.
             *
             * If we found a path, mark the found_a_path flag to true and
             * pass the tuple to the next execution tree. When the path
             * exists, we don't need to create/modify anything.
             */
            css->found_a_path = true;

            econtext->ecxt_scantuple = ExecProject(node->ss.ps.lefttree->ps_ProjInfo);
            return ExecProject(node->ss.ps.ps_ProjInfo);
        }
        else if (TupIsNull(slot) && css->found_a_path)
        {
            /*
             * Part of Sub-Case 2.
             *
             * MERGE found the path(s) that already exists and we are done passing
             * all the found path(s) up the execution tree.
             */
            return NULL;
        }
        else
        {
            /*
             * Part of Sub-Case 1.
             *
             * MERGE could not find the path in memory and the sub-node in the
             * execution tree returned NULL. We need to create the path and
             * pass the tuple to the next execution node. The subtrees will
             * begin its cleanup process when there are no more tuples found.
             * So we will need to create a TupleTableSlot and populate with the
             * information from the newly created path that the query needs.
             */
            ExprContext *econtext = node->ss.ps.ps_ExprContext;
            SubqueryScanState *sss = (SubqueryScanState *)node->ss.ps.lefttree;

            /*
             * Our child execution node is always a subquery. If not there
             * is an issue.
             */
            Assert(IsA(sss, SubqueryScanState));

            /*
             * found_a_path should only be set to true if MERGE is following
             * sub-case 2.
             */
            Assert(css->found_a_path == false);

            /*
             * This block of sub-case 1 should only be executed once. To
             * create the single path if the path does not exist. If we find
             * ourselves here again, the internal state of the MERGE execution
             * node was incorrectly altered.
             */
            Assert(css->created_new_path == false);

            /*
             *  Postgres cleared the child tuple table slot, we need to remake
             *  it.
             */
            ExecInitScanTupleSlot(estate, &sss->ss,
                                  ExecGetResultType(sss->subplan),
                                  &TTSOpsVirtual);

            /* setup the scantuple that the process_path needs */
            econtext->ecxt_scantuple = sss->ss.ss_ScanTupleSlot;

            /* create the path */
            process_path(css, NULL, true);

            /* mark the create_new_path flag to true. */
            css->created_new_path = true;

            /*
             *  find the tts_values that process_path did not populate and
             *  mark as null.
             */
            mark_tts_isnull(econtext->ecxt_scantuple);

            /* store the heap tuble */
            ExecStoreVirtualTuple(econtext->ecxt_scantuple);

            /*
             * make the subquery's projection scan slot be the tuple table we
             * created and run the projection logic.
             */
            sss->ss.ps.ps_ProjInfo->pi_exprContext->ecxt_scantuple =
                                                        econtext->ecxt_scantuple;

            /* assign this to be our scantuple */
            econtext->ecxt_scantuple = ExecProject(node->ss.ps.lefttree->ps_ProjInfo);

            /*
             *  run the merge's projection logic and pass to its parent
             *  execution node
             */
            return ExecProject(node->ss.ps.ps_ProjInfo);
        }
    }
}


/*
 * Function called at the end of the execution phase to cleanup
 * MERGE.
 */
static void end_cypher_merge(CustomScanState *node)
{
    cypher_merge_custom_scan_state *css =
        (cypher_merge_custom_scan_state *)node;
    cypher_create_path *path = css->path;
    ListCell *lc = NULL;
    int path_length = list_length(path->target_nodes);

    /* increment the command counter */
    CommandCounterIncrement();

    ExecEndNode(node->ss.ps.lefttree);

    foreach (lc, path->target_nodes)
    {
        cypher_target_node *cypher_node = (cypher_target_node *)lfirst(lc);

        if (!CYPHER_TARGET_NODE_INSERT_ENTITY(cypher_node->flags))
        {
            continue;
        }

        /* close all indices for the node */
        ExecCloseIndices(cypher_node->resultRelInfo);

        /* close the relation itself */
        table_close(cypher_node->resultRelInfo->ri_RelationDesc,
                    RowExclusiveLock);
    }

    /* free up our created paths lists */
    while (css->created_paths_list != NULL)
    {
        created_path *next = css->created_paths_list->next;
        path_entry **entry = css->created_paths_list->entry;

        /* free up the path array elements */
        free_path_entry_array(entry, path_length);

        /* free up the array container */
        pfree(entry);

        /* free up the created_path container */
        pfree(css->created_paths_list);

        css->created_paths_list = next;
    }

}

/*
 * Rescan is mostly used by join execution nodes, and several others.
 * Since we are creating data here its not safe to rescan the node. Throw
 * an error and try to help the uer understand what went wrong.
 */
static void rescan_cypher_merge(CustomScanState *node)
{
    ereport(ERROR,
            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("cypher merge clause cannot be rescanned"),
                     errhint("its unsafe to use joins in a query with a Cypher MERGE clause")));
}

/*
 * Extracts the metadata information that MERGE needs from the
 * merge_custom_scan node and creates the cypher_merge_custom_scan_state
 * for the execution phase.
 */
Node *create_cypher_merge_plan_state(CustomScan *cscan)
{
    cypher_merge_custom_scan_state *cypher_css =
        palloc0(sizeof(cypher_merge_custom_scan_state));
    cypher_merge_information *merge_information;
    char *serialized_data = NULL;
    Const *c = NULL;

    cypher_css->cs = cscan;

    /* get the serialized data structure from the Const and deserialize it. */
    c = linitial(cscan->custom_private);
    serialized_data = (char *)c->constvalue;
    merge_information = stringToNode(serialized_data);

    Assert(is_ag_node(merge_information, cypher_merge_information));

    cypher_css->merge_information = merge_information;
    cypher_css->flags = merge_information->flags;
    cypher_css->merge_function_attr = merge_information->merge_function_attr;
    cypher_css->path = merge_information->path;
    cypher_css->created_new_path = false;
    cypher_css->found_a_path = false;
    cypher_css->graph_oid = merge_information->graph_oid;

    cypher_css->css.ss.ps.type = T_CustomScanState;
    cypher_css->css.methods = &cypher_merge_exec_methods;

    return (Node *)cypher_css;
}

/*
 * Creates the vertex entity, returns the vertex's id in case the caller is
 * the create_edge function.
 */
static Datum merge_vertex(cypher_merge_custom_scan_state *css,
                          cypher_target_node *node, ListCell *next, List *list,
                          path_entry **path_array, int path_index,
                          bool should_insert)
{
    bool isNull;
    Datum id;
    EState *estate = css->css.ss.ps.state;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    ResultRelInfo *resultRelInfo = node->resultRelInfo;
    TupleTableSlot *elemTupleSlot = node->elemTupleSlot;
    TupleTableSlot *scanTupleSlot = econtext->ecxt_scantuple;

    Assert(node->type == LABEL_KIND_VERTEX);

    /*
     * Vertices in a path might already exists. If they do get the id
     * to pass to the edges before and after it. Otherwise, insert the
     * new vertex into it's table and then pass the id along.
     */
    if (CYPHER_TARGET_NODE_INSERT_ENTITY(node->flags))
    {
        ResultRelInfo **old_estate_es_result_relations = NULL;
        Datum prop;

        /*
         * Set estate's result relation to the vertex's result
         * relation.
         *
         * Note: This obliterates what was their previously
         */

        /* save the old result relation info */
        old_estate_es_result_relations = estate->es_result_relations;

        estate->es_result_relations = &resultRelInfo;

        ExecClearTuple(elemTupleSlot);

        /* if we not are going to insert, we need our structure pointers */
        if (should_insert == false &&
            (path_array == NULL || path_array[path_index] == NULL))
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                             errmsg("invalid input parameter combination")));
        }

        /*
         * If we shouldn't insert the vertex, we need to retrieve it from the
         * storage structure.
         */
        if (should_insert == false &&
            path_array != NULL &&
            path_array[path_index] != NULL)
        {
            id = path_array[path_index]->id;
            isNull = path_array[path_index]->id_isNull;
        }
        /*
         * Otherwise, we need to retrieve the vertex normally and store its
         * unique values if the storage structure exists.
         */
        else if (should_insert == true)
        {
            /* get the next graphid for this vertex */
            id = ExecEvalExpr(node->id_expr_state, econtext, &isNull);

            if (path_array != NULL && path_array[path_index] != NULL)
            {
                /* store it */
                path_array[path_index]->id = id;
                path_array[path_index]->id_isNull = isNull;
            }
        }
        else
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                             errmsg("invalid input parameter combination")));
        }

        /* put the id values into the tuple slot */
        elemTupleSlot->tts_values[vertex_tuple_id] = id;
        elemTupleSlot->tts_isnull[vertex_tuple_id] = isNull;

        /*
         * Retrieve the properties and isNull values if the storage structure
         * exists.
         */
        if (path_array != NULL && path_array[path_index] != NULL)
        {
            prop = path_array[path_index]->prop;
            isNull = path_array[path_index]->prop_isNull;
        }
        /* otherwise, get them normally */
        else
        {
            /* get the properties for this vertex */
            prop = ExecEvalExpr(node->prop_expr_state, econtext, &isNull);
        }

        /* put the prop values into the tuple slot */
        elemTupleSlot->tts_values[vertex_tuple_properties] = prop;
        elemTupleSlot->tts_isnull[vertex_tuple_properties] = isNull;

        /*
         * Insert the new vertex.
         *
         * Depending on the currentCommandId, we need to do this one of two
         * different ways -
         *
         * 1) If they are equal, the currentCommandId hasn't been used for an
         *    update, or it hasn't been incremented after being used. In either
         *    case, we need to use the current one and then increment it so that
         *    the following commands will have visibility of this update. Note,
         *    it isn't our job to update the currentCommandId first and then do
         *    this check.
         *
         * 2) If they are not equal, the currentCommandId has been used and/or
         *    updated. In this case, we can't use it. Otherwise our update won't
         *    be visible to anything that follows, until the currentCommandId is
         *    updated again. Remember, visibility is, greater than but not equal
         *    to, the currentCommandID used for the update. So, in this case we
         *    need to use the original currentCommandId when begin_cypher_merge
         *    was initiated as everything under this instance of merge needs to
         *    be based off of that initial currentCommandId. This allows the
         *    following command to see the updates generated by this instance of
         *    merge.
         */
        if (should_insert &&
            css->base_currentCommandId == GetCurrentCommandId(false))
        {
            insert_entity_tuple(resultRelInfo, elemTupleSlot, estate);

            /*
             * Increment the currentCommandId since we processed an update. We
             * don't want to do this outside of this block because we don't want
             * to inadvertently or unnecessarily update the commandCounterId of
             * another command.
             */
            CommandCounterIncrement();
        }
        else if (should_insert)
        {
            insert_entity_tuple_cid(resultRelInfo, elemTupleSlot, estate,
                                    css->base_currentCommandId);
        }

        /* restore the old result relation info */
        estate->es_result_relations = old_estate_es_result_relations;

        /*
         * When the vertex is used by clauses higher in the execution tree
         * we need to create a vertex datum. When the vertex is a variable,
         * add to the scantuple slot. When the vertex is part of a path
         * variable, add to the list.
         */
        if (CYPHER_TARGET_NODE_OUTPUT(node->flags))
        {
            Datum result;

            /* make the vertex agtype */
            result = make_vertex(id, CStringGetDatum(node->label_name), prop);

            /* append to the path list */
            if (CYPHER_TARGET_NODE_IN_PATH(node->flags))
            {
                css->path_values = lappend(css->path_values,
                                           DatumGetPointer(result));
            }

            /*
             * Put the vertex in the correct spot in the scantuple, so parent
             * execution nodes can reference the newly created variable.
             */
            if (CYPHER_TARGET_NODE_IS_VARIABLE(node->flags))
            {
                bool debug_flag = false;
                int tuple_position = node->tuple_position - 1;

                /*
                 * We need to make sure that the tuple_position is within the
                 * boundaries of the tuple's number of attributes. Otherwise, it
                 * will corrupt memory. The cases where it doesn't fall within
                 * are usually due to a variable that is specified but there
                 * isn't a RETURN clause. In these cases we just don't bother to
                 * store the value.
                 */
                if (!debug_flag &&
                    (tuple_position < scanTupleSlot->tts_tupleDescriptor->natts ||
                     scanTupleSlot->tts_tupleDescriptor->natts != 1))
                {
                    /* store the result */
                    scanTupleSlot->tts_values[tuple_position] = result;
                    scanTupleSlot->tts_isnull[tuple_position] = false;
                }
            }
        }
    }
    /*
     * If we have the storage structure pointers, we have already retrieved the
     * ID from the datum in the scan tuple, so just retrieve it from the
     * structure.
     */
    else if (path_array != NULL && path_array[path_index] != NULL)
    {
        /* retrieve the id of the vertex */
        id = path_array[path_index]->id;

        /*
         * Add the Datum to the list of entities for creating the path variable
         */
        if (CYPHER_TARGET_NODE_IN_PATH(node->flags))
        {
            Datum vertex = scanTupleSlot->tts_values[node->tuple_position - 1];
            css->path_values = lappend(css->path_values,
                                       DatumGetPointer(vertex));
        }
    }
    else
    {
        agtype *a = NULL;
        Datum d;
        agtype_value *v = NULL;
        agtype_value *id_value = NULL;

        /* check that the variable isn't NULL */
        if (scanTupleSlot->tts_isnull[node->tuple_position - 1])
        {
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("Existing variable %s cannot be NULL in MERGE clause",
                            node->variable_name)));
        }

        /* get the vertex agtype in the scanTupleSlot */
        d = scanTupleSlot->tts_values[node->tuple_position - 1];
        a = DATUM_GET_AGTYPE_P(d);

        /* Convert to an agtype value */
        v = get_ith_agtype_value_from_container(&a->root, 0);

        if (v->type != AGTV_VERTEX)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("agtype must resolve to a vertex")));
        }

        /* extract the id agtype field */
        id_value = GET_AGTYPE_VALUE_OBJECT_VALUE(v, "id");

        /* extract the graphid and cast to a Datum */
        id = GRAPHID_GET_DATUM(id_value->val.int_value);

        /*
         * Its possible the variable has already been deleted. There are two
         * ways this can happen. One is the query explicitly deleted the
         * variable, the is_deleted flag will catch that. However, it is
         * possible the user deleted the vertex using another variable name. We
         * need to scan the table to find the vertex's current status relative
         * to this CREATE clause. If the variable was initially created in this
         * clause, we can skip this check, because the transaction system
         * guarantees that nothing can happen to that tuple, as far as we are
         * concerned with at this time.
         */
        if (!SAFE_TO_SKIP_EXISTENCE_CHECK(node->flags))
        {
            if (!entity_exists(estate, css->graph_oid, DATUM_GET_GRAPHID(id)))
            {
                ereport(ERROR,
                    (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                     errmsg("vertex assigned to variable %s was deleted",
                            node->variable_name)));
            }
        }

        /*
         * Add the Datum to the list of entities for creating the path variable
         */
        if (CYPHER_TARGET_NODE_IN_PATH(node->flags))
        {
            Datum vertex = scanTupleSlot->tts_values[node->tuple_position - 1];
            css->path_values = lappend(css->path_values,
                                       DatumGetPointer(vertex));
        }
    }

    /* If the path continues, create the next edge, passing the vertex's id. */
    if (next != NULL)
    {
        merge_edge(css, lfirst(next), id, lnext(list, next), list,
                   path_array, path_index+1, should_insert);
    }

    return id;
}

/*
 * Create the edge entity.
 */
static void merge_edge(cypher_merge_custom_scan_state *css,
                       cypher_target_node *node, Datum prev_vertex_id,
                       ListCell *next, List *list,
                       path_entry **path_array, int path_index,
                       bool should_insert)
{
    bool isNull;
    EState *estate = css->css.ss.ps.state;
    ExprContext *econtext = css->css.ss.ps.ps_ExprContext;
    ResultRelInfo *resultRelInfo = node->resultRelInfo;
    ResultRelInfo **old_estate_es_result_relations = NULL;
    TupleTableSlot *elemTupleSlot = node->elemTupleSlot;
    Datum id;
    Datum start_id, end_id, next_vertex_id;
    List *prev_path = css->path_values;
    Datum prop;

    Assert(node->type == LABEL_KIND_EDGE);
    Assert(lfirst(next) != NULL);

    /*
     * Create the next vertex before creating the edge. We need the
     * next vertex's id.
     */
    css->path_values = NIL;
    next_vertex_id = merge_vertex(css, lfirst(next), lnext(list, next), list,
                                  path_array, path_index+1, should_insert);

    /*
     * Set the start and end vertex ids
     */
    if (node->dir == CYPHER_REL_DIR_RIGHT || node->dir == CYPHER_REL_DIR_NONE)
    {
        /* create pattern (prev_vertex)-[edge]->(next_vertex) */
        start_id = prev_vertex_id;
        end_id = next_vertex_id;
    }
    else if (node->dir == CYPHER_REL_DIR_LEFT)
    {
        /* create pattern (prev_vertex)<-[edge]-(next_vertex) */
        start_id = next_vertex_id;
        end_id = prev_vertex_id;
    }
    else
    {
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("edge direction must be specified in a MERGE clause")));
    }

    /*
     * Set estate's result relation to the vertex's result
     * relation.
     *
     * Note: This obliterates what was their previously
     */

    /* save the old result relation info */
    old_estate_es_result_relations = estate->es_result_relations;

    estate->es_result_relations = &resultRelInfo;

    ExecClearTuple(elemTupleSlot);

    /* if we not are going to insert, we need our structure pointers */
    if (should_insert == false &&
        (path_array == NULL || path_array[path_index] == NULL))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("invalid input parameter combination")));
    }

    /*
     * If we shouldn't insert the edge, we need to retrieve the entire edge from
     * the storage structure.
     */
    if (should_insert == false &&
        path_array != NULL &&
        path_array[path_index] != NULL)
    {
        id = path_array[path_index]->id;
        isNull = path_array[path_index]->id_isNull;
        start_id = path_array[path_index]->start_id;
        end_id = path_array[path_index]->end_id;
    }
    /*
     * Otherwise, we need to get the edge's ID and store its unique values if
     * the storage structure exists
     */
    else if (should_insert == true)
    {
        /* get the next graphid for this edge */
        id = ExecEvalExpr(node->id_expr_state, econtext, &isNull);

        if (path_array != NULL && path_array[path_index] != NULL)
        {
            /* store it */
            path_array[path_index]->id = id;
            path_array[path_index]->id_isNull = isNull;
            path_array[path_index]->start_id = start_id;
            path_array[path_index]->end_id = end_id;
        }
    }
    else
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("invalid input parameter combination")));
    }

    /* put the id values into the tuple slot */
    elemTupleSlot->tts_values[edge_tuple_id] = id;
    elemTupleSlot->tts_isnull[edge_tuple_id] = isNull;

    /* Graph id for the starting vertex */
    elemTupleSlot->tts_values[edge_tuple_start_id] = start_id;
    elemTupleSlot->tts_isnull[edge_tuple_start_id] = false;

    /* Graph id for the ending vertex */
    elemTupleSlot->tts_values[edge_tuple_end_id] = end_id;
    elemTupleSlot->tts_isnull[edge_tuple_end_id] = false;

    /*
     * Retrieve the properties and isNull values if the storage structure
     * exists.
     */
    if (path_array != NULL && path_array[path_index] != NULL)
    {
        prop = path_array[path_index]->prop;
        isNull = path_array[path_index]->prop_isNull;
    }
    /* otherwise, get them normally */
    else
    {
        /* get the properties for this edge */
        prop = ExecEvalExpr(node->prop_expr_state, econtext, &isNull);
    }

    /* store the properties in the tuple slot */
    elemTupleSlot->tts_values[edge_tuple_properties] = prop;
    elemTupleSlot->tts_isnull[edge_tuple_properties] = isNull;

    /*
     * Insert the new edge.
     *
     * Depending on the currentCommandId, we need to do this one of two
     * different ways -
     *
     * 1) If they are equal, the currentCommandId hasn't been used for an
     *    update, or it hasn't been incremented after being used. In either
     *    case, we need to use the current one and then increment it so that
     *    the following commands will have visibility of this update. Note,
     *    it isn't our job to update the currentCommandId first and then do
     *    this check.
     *
     * 2) If they are not equal, the currentCommandId has been used and/or
     *    updated. In this case, we can't use it. Otherwise our update won't
     *    be visible to anything that follows, until the currentCommandId is
     *    updated again. Remember, visibility is, greater than but not equal
     *    to, the currentCommandID used for the update. So, in this case we
     *    need to use the original currentCommandId when begin_cypher_merge
     *    was initiated as everything under this instance of merge needs to
     *    be based off of that initial currentCommandId. This allows the
     *    following command to see the updates generated by this instance of
     *    merge.
     */
    if (should_insert &&
        css->base_currentCommandId == GetCurrentCommandId(false))
    {
        insert_entity_tuple(resultRelInfo, elemTupleSlot, estate);

        /*
         * Increment the currentCommandId since we processed an update. We
         * don't want to do this outside of this block because we don't want
         * to inadvertently or unnecessarily update the commandCounterId of
         * another command.
         */
        CommandCounterIncrement();
    }
    else if (should_insert)
    {
        insert_entity_tuple_cid(resultRelInfo, elemTupleSlot, estate,
                                    css->base_currentCommandId);
    }

    /* restore the old result relation info */
    estate->es_result_relations = old_estate_es_result_relations;

    /*
     * When the edge is used by clauses higher in the execution tree
     * we need to create an edge datum. When the edge is a variable,
     * add to the scantuple slot. When the edge is part of a path
     * variable, add to the list.
     */
    if (CYPHER_TARGET_NODE_OUTPUT(node->flags))
    {
        Datum result;

        result = make_edge(id, start_id, end_id,
                           CStringGetDatum(node->label_name), prop);

        /* add the Datum to the list of entities for creating the path variable */
        if (CYPHER_TARGET_NODE_IN_PATH(node->flags))
        {
            prev_path = lappend(prev_path, DatumGetPointer(result));
            css->path_values = list_concat(prev_path, css->path_values);
        }

        /* Add the entity to the TupleTableSlot if necessary */
        if (CYPHER_TARGET_NODE_IS_VARIABLE(node->flags))
        {
            TupleTableSlot *scantuple = econtext->ecxt_scantuple;
            bool debug_flag = false;
            int tuple_position = node->tuple_position - 1;

            /*
             * We need to make sure that the tuple_position is within the
             * boundaries of the tuple's number of attributes. Otherwise, it
             * will corrupt memory. The cases where it doesn't fall within are
             * usually due to a variable that is specified but there isn't a
             * RETURN clause. In these cases we just don't bother to store the
             * value.
             */
             if (!debug_flag &&
                 (tuple_position < scantuple->tts_tupleDescriptor->natts ||
                  scantuple->tts_tupleDescriptor->natts != 1))
            {
                /* store the result */
                scantuple->tts_values[tuple_position] = result;
                scantuple->tts_isnull[tuple_position] = false;
            }
        }
    }
}
