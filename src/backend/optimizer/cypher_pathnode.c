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
#include "nodes/nodes.h"
#include "nodes/pg_list.h"
#include "nodes/relation.h"

#include "optimizer/cypher_createplan.h"
#include "optimizer/cypher_pathnode.h"

const CustomPathMethods cypher_create_path_methods = {
    "Cypher Create", plan_cypher_create_path, NULL};
const CustomPathMethods cypher_set_path_methods = {
    "Cypher Set", plan_cypher_set_path, NULL};


CustomPath *create_cypher_create_path(PlannerInfo *root, RelOptInfo *rel,
                                      List *custom_private)
{
    CustomPath *cp;

    cp = makeNode(CustomPath);

    cp->path.pathtype = T_CustomScan;

    cp->path.parent = rel;
    cp->path.pathtarget = rel->reltarget;

    cp->path.param_info = NULL;

    // Do not allow parallel methods
    cp->path.parallel_aware = false;
    cp->path.parallel_safe = false;
    cp->path.parallel_workers = 0;

    cp->path.rows = 0; // Basic CREATE will not return rows
    cp->path.startup_cost = 0; // Basic CREATE will not fetch any pages
    cp->path.total_cost = 0;

    // No output ordering for basic CREATE
    cp->path.pathkeys = NULL;

    // Disable all custom flags for now
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

    // Do not allow parallel methods
    cp->path.parallel_aware = false;
    cp->path.parallel_safe = false;
    cp->path.parallel_workers = 0;

    cp->path.rows = 0; // Basic SET will not return rows
    cp->path.startup_cost = 0; // Basic SET will not fetch any pages
    cp->path.total_cost = 0;

    // No output ordering for basic SET
    cp->path.pathkeys = NULL;

    // Disable all custom flags for now
    cp->flags = 0;

    cp->custom_paths = rel->pathlist;
    cp->custom_private = custom_private;
    cp->methods = &cypher_set_path_methods;

    return cp;
}
