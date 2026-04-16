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

#include "nodes/extensible.h"

#include "optimizer/cypher_createplan.h"
#include "optimizer/cypher_pathnode.h"
#include "parser/cypher_analyze.h"
#include "executor/cypher_utils.h"
#include "optimizer/subselect.h"
#include "nodes/makefuncs.h"

static Const *convert_sublink_to_subplan(PlannerInfo *root,
                                         List *custom_private);
static bool expr_has_sublink(Node *node, void *context);

const CustomPathMethods cypher_create_path_methods = {
    CREATE_PATH_NAME, plan_cypher_create_path, NULL};
const CustomPathMethods cypher_set_path_methods = {
    SET_PATH_NAME, plan_cypher_set_path, NULL};
const CustomPathMethods cypher_delete_path_methods = {
    DELETE_PATH_NAME, plan_cypher_delete_path, NULL};
const CustomPathMethods cypher_merge_path_methods = {
    MERGE_PATH_NAME, plan_cypher_merge_path, NULL};

CustomPath *create_cypher_create_path(PlannerInfo *root, RelOptInfo *rel,
                                      List *custom_private)
{
    CustomPath *cp;

    cp = makeNode(CustomPath);

    cp->path.pathtype = T_CustomScan;

    cp->path.parent = rel;
    cp->path.pathtarget = rel->reltarget;

    cp->path.param_info = NULL;

    /* Do not allow parallel methods */
    cp->path.parallel_aware = false;
    cp->path.parallel_safe = false;
    cp->path.parallel_workers = 0;

    cp->path.rows = 0; /* Basic CREATE will not return rows */
    cp->path.startup_cost = 0; /* Basic CREATE will not fetch any pages */
    cp->path.total_cost = 0;

    /* No output ordering for basic CREATE */
    cp->path.pathkeys = NULL;

    /* Disable all custom flags for now */
    cp->flags = 0;

    cp->custom_paths = rel->pathlist;
    cp->custom_private = custom_private;
    cp->methods = &cypher_create_path_methods;

    return cp;
}

CustomPath *create_cypher_set_path(PlannerInfo *root, RelOptInfo *rel,
                                   List *custom_private)
{
    CustomPath *cp;

    cp = makeNode(CustomPath);

    cp->path.pathtype = T_CustomScan;

    cp->path.parent = rel;
    cp->path.pathtarget = rel->reltarget;

    cp->path.param_info = NULL;

    /* Do not allow parallel methods */
    cp->path.parallel_aware = false;
    cp->path.parallel_safe = false;
    cp->path.parallel_workers = 0;

    cp->path.rows = 0; /* Basic SET will not return rows */
    cp->path.startup_cost = 0; /* Basic SET will not fetch any pages */
    cp->path.total_cost = 0;

    /* No output ordering for basic SET */
    cp->path.pathkeys = NULL;

    /* Disable all custom flags for now */
    cp->flags = 0;

    cp->custom_paths = rel->pathlist;
    cp->custom_private = custom_private;
    cp->methods = &cypher_set_path_methods;

    return cp;
}

/*
 * Creates a Delete Path. Makes the original path a child of the new
 * path. We leave it to the caller to replace the pathlist of the rel.
 */
CustomPath *create_cypher_delete_path(PlannerInfo *root, RelOptInfo *rel,
                                   List *custom_private)
{
    CustomPath *cp;

    cp = makeNode(CustomPath);

    cp->path.pathtype = T_CustomScan;

    cp->path.parent = rel;
    cp->path.pathtarget = rel->reltarget;

    cp->path.param_info = NULL;

    /* Do not allow parallel methods */
    cp->path.parallel_aware = false;
    cp->path.parallel_safe = false;
    cp->path.parallel_workers = 0;

    cp->path.rows = 0;
    cp->path.startup_cost = 0;
    cp->path.total_cost = 0;

    /* No output ordering for basic SET */
    cp->path.pathkeys = NULL;

    /* Disable all custom flags for now */
    cp->flags = 0;

    /* Make the original paths the children of the new path */
    cp->custom_paths = rel->pathlist;
    /* Store the metadata Delete will need in the execution phase. */
    cp->custom_private = custom_private;
    /* Tells Postgres how to turn this path to the correct CustomScan */
    cp->methods = &cypher_delete_path_methods;

    return cp;
}

/*
 * Creates a merge path. Makes the original path a child of the new
 * path. We leave it to the caller to replace the pathlist of the rel.
 */
CustomPath *create_cypher_merge_path(PlannerInfo *root, RelOptInfo *rel,
                                   List *custom_private)
{
    CustomPath *cp;

    cp = makeNode(CustomPath);

    cp->path.pathtype = T_CustomScan;

    cp->path.parent = rel;
    cp->path.pathtarget = rel->reltarget;

    cp->path.param_info = NULL;

    /* Do not allow parallel methods */
    cp->path.parallel_aware = false;
    cp->path.parallel_safe = false;
    cp->path.parallel_workers = 0;

    cp->path.rows = 0;
    cp->path.startup_cost = 0;
    cp->path.total_cost = 0;

    /* No output ordering for basic SET */
    cp->path.pathkeys = NULL;

    /* Disable all custom flags for now */
    cp->flags = 0;

    /* Make the original paths the children of the new path */
    cp->custom_paths = rel->pathlist;

    /*
     * Store the metadata Merge will need in the execution phase.
     * We may have a sublink here in case the user used a list
     * comprehension in merge.
     */
    if (rel->subroot->parse->hasSubLinks)
    {
        cp->custom_private = list_make1(convert_sublink_to_subplan(root, custom_private));
    }
    else
    {
        cp->custom_private = custom_private;
    }

    /* Tells Postgres how to turn this path to the correct CustomScan */
    cp->methods = &cypher_merge_path_methods;

    return cp;
}

/*
 * Deserializes the merge information and checks if any property
 * expression (prop_expr) contains a SubLink.
 * If found, converts the SubLink to a SubPlan, updates the
 * structure accordingly, and serializes it back.
 */
static Const *convert_sublink_to_subplan(PlannerInfo *root, List *custom_private)
{
    cypher_merge_information *merge_information;
    char *serialized_data = NULL;
    Const *c = NULL;
    ListCell *lc = NULL;
    StringInfo str = makeStringInfo();

    c = linitial(custom_private);
    serialized_data = (char *)c->constvalue;
    merge_information = stringToNode(serialized_data);

    Assert(is_ag_node(merge_information, cypher_merge_information));

    /* Only part where we can expect a sublink is in prop_expr. */
    foreach (lc, merge_information->path->target_nodes)
    {
        cypher_target_node *node = (cypher_target_node *)lfirst(lc);
        Node *prop_expr = (Node *) node->prop_expr;

        if (expr_has_sublink(prop_expr, NULL))
        {
            node->prop_expr = (Expr *) SS_process_sublinks(root, prop_expr, false);
        }
    }

    /* Serialize the information again and return it. */
    outNode(str, (Node *)merge_information);

    return makeConst(INTERNALOID, -1, InvalidOid, str->len,
                     PointerGetDatum(str->data), false, false);
}

/*
 * Helper function to check if the node has a sublink.
 */
static bool expr_has_sublink(Node *node, void *context)
{
    if (node == NULL)
    {
        return false;
    }

    if (IsA(node, SubLink))
    {
        return true;
    }

    return cypher_expr_tree_walker(node, expr_has_sublink, context);
}
