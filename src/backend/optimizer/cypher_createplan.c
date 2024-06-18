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

#include "executor/cypher_executor.h"
#include "optimizer/cypher_createplan.h"

const CustomScanMethods cypher_create_plan_methods = {
    "Cypher Create", create_cypher_create_plan_state};
const CustomScanMethods cypher_set_plan_methods = {
    "Cypher Set", create_cypher_set_plan_state};
const CustomScanMethods cypher_delete_plan_methods = {
    "Cypher Delete", create_cypher_delete_plan_state};
const CustomScanMethods cypher_merge_plan_methods = {
    "Cypher Merge", create_cypher_merge_plan_state};

Plan *plan_cypher_create_path(PlannerInfo *root, RelOptInfo *rel,
                              CustomPath *best_path, List *tlist,
                              List *clauses, List *custom_plans)
{
    CustomScan *cs;
    Plan *subplan = linitial(custom_plans);

    cs = makeNode(CustomScan);

    cs->scan.plan.startup_cost = best_path->path.startup_cost;
    cs->scan.plan.total_cost = best_path->path.total_cost;

    cs->scan.plan.plan_rows = best_path->path.rows;
    cs->scan.plan.plan_width = 0;

    cs->scan.plan.parallel_aware = best_path->path.parallel_aware;
    cs->scan.plan.parallel_safe = best_path->path.parallel_safe;

    /* Set later in set_plan_refs */
    cs->scan.plan.plan_node_id = 0;
    cs->scan.plan.targetlist = tlist;
    cs->scan.plan.qual = NIL;
    cs->scan.plan.lefttree = NULL;
    cs->scan.plan.righttree = NULL;
    cs->scan.plan.initPlan = NIL;

    cs->scan.plan.extParam = NULL;
    cs->scan.plan.allParam = NULL;

    cs->scan.scanrelid = 0;

    cs->flags = best_path->flags;

    cs->custom_plans = custom_plans;
    cs->custom_exprs = NIL;
    cs->custom_private = best_path->custom_private;
    cs->custom_scan_tlist = subplan->targetlist;
    cs->custom_relids = NULL;
    cs->methods = &cypher_create_plan_methods;

    return (Plan *)cs;
}

Plan *plan_cypher_set_path(PlannerInfo *root, RelOptInfo *rel,
                           CustomPath *best_path, List *tlist,
                           List *clauses, List *custom_plans)
{
    CustomScan *cs;
    Plan *subplan = linitial(custom_plans);

    cs = makeNode(CustomScan);

    cs->scan.plan.startup_cost = best_path->path.startup_cost;
    cs->scan.plan.total_cost = best_path->path.total_cost;

    cs->scan.plan.plan_rows = best_path->path.rows;
    cs->scan.plan.plan_width = 0;

    cs->scan.plan.parallel_aware = best_path->path.parallel_aware;
    cs->scan.plan.parallel_safe = best_path->path.parallel_safe;

    cs->scan.plan.plan_node_id = 0; /* Set later in set_plan_refs */
    cs->scan.plan.targetlist = tlist;
    cs->scan.plan.qual = NIL;
    cs->scan.plan.lefttree = NULL;
    cs->scan.plan.righttree = NULL;
    cs->scan.plan.initPlan = NIL;

    cs->scan.plan.extParam = NULL;
    cs->scan.plan.allParam = NULL;

    cs->scan.scanrelid = 0;

    cs->flags = best_path->flags;

    cs->custom_plans = custom_plans;
    cs->custom_exprs = NIL;
    cs->custom_private = best_path->custom_private;
    cs->custom_scan_tlist = subplan->targetlist;

    cs->custom_relids = NULL;
    cs->methods = &cypher_set_plan_methods;

    return (Plan *)cs;
}

/*
 * Coverts the Scan node representing the DELETE clause
 * to the delete Plan node
 */
Plan *plan_cypher_delete_path(PlannerInfo *root, RelOptInfo *rel,
                           CustomPath *best_path, List *tlist,
                           List *clauses, List *custom_plans)
{
    CustomScan *cs;
    Plan *subplan = linitial(custom_plans);

    cs = makeNode(CustomScan);

    cs->scan.plan.startup_cost = best_path->path.startup_cost;
    cs->scan.plan.total_cost = best_path->path.total_cost;

    cs->scan.plan.plan_rows = best_path->path.rows;
    cs->scan.plan.plan_width = 0;

    cs->scan.plan.parallel_aware = best_path->path.parallel_aware;
    cs->scan.plan.parallel_safe = best_path->path.parallel_safe;

    cs->scan.plan.plan_node_id = 0; /* Set later in set_plan_refs */
    /*
     * the scan list of the delete node, used for its ScanTupleSlot used
     * by its parent in the execution phase.
     */
    cs->scan.plan.targetlist = tlist;
    cs->scan.plan.qual = NIL;
    cs->scan.plan.lefttree = NULL;
    cs->scan.plan.righttree = NULL;
    cs->scan.plan.initPlan = NIL;

    cs->scan.plan.extParam = NULL;
    cs->scan.plan.allParam = NULL;

    /*
     * We do not want Postgres to assume we are scanning a table, postgres'
     * optimizer will make assumptions about our targetlist that are false
     */
    cs->scan.scanrelid = 0;

    cs->flags = best_path->flags;

    /* child plan nodes are here, Postgres processed them for us. */
    cs->custom_plans = custom_plans;
    cs->custom_exprs = NIL;
    /* transfer delete metadata needed by the DELETE clause. */
    cs->custom_private = best_path->custom_private;
    /*
     * the scan list of the delete node's children, used for ScanTupleSlot
     * in execution.
     */
    cs->custom_scan_tlist = subplan->targetlist;

    cs->custom_relids = NULL;
    cs->methods = &cypher_delete_plan_methods;

    return (Plan *)cs;
}

/*
 * Coverts the Scan node representing the MERGE clause
 * to the merge Plan node
 */
Plan *plan_cypher_merge_path(PlannerInfo *root, RelOptInfo *rel,
                           CustomPath *best_path, List *tlist,
                           List *clauses, List *custom_plans)
{
    CustomScan *cs;
    Plan *subplan = linitial(custom_plans);

    cs = makeNode(CustomScan);

    cs->scan.plan.startup_cost = best_path->path.startup_cost;
    cs->scan.plan.total_cost = best_path->path.total_cost;

    cs->scan.plan.plan_rows = best_path->path.rows;
    cs->scan.plan.plan_width = 0;

    cs->scan.plan.parallel_aware = best_path->path.parallel_aware;
    cs->scan.plan.parallel_safe = best_path->path.parallel_safe;

    cs->scan.plan.plan_node_id = 0; /* Set later in set_plan_refs */
    /*
     * the scan list of the merge node, used for its ScanTupleSlot used
     * by its parent in the execution phase.
     */
    cs->scan.plan.targetlist = tlist;
    cs->scan.plan.qual = NIL;
    cs->scan.plan.lefttree = NULL;
    cs->scan.plan.righttree = NULL;
    cs->scan.plan.initPlan = NIL;

    cs->scan.plan.extParam = NULL;
    cs->scan.plan.allParam = NULL;

    /*
     * We do not want Postgres to assume we are scanning a table, postgres'
     * optimizer will make assumptions about our targetlist that are false
     */
    cs->scan.scanrelid = 0;

    cs->flags = best_path->flags;

    /* child plan nodes are here, Postgres processed them for us. */
    cs->custom_plans = custom_plans;
    cs->custom_exprs = NIL;
    /* transfer delete metadata needed by the MERGE clause. */
    cs->custom_private = best_path->custom_private;
    /*
     * the scan list of the merge node's children, used for ScanTupleSlot
     * in execution.
     */
    cs->custom_scan_tlist = subplan->targetlist;

    cs->custom_relids = NULL;
    cs->methods = &cypher_merge_plan_methods;

    return (Plan *)cs;
}
