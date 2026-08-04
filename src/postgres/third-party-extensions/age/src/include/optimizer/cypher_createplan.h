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

#ifndef AG_CYPHER_CREATEPLAN_H
#define AG_CYPHER_CREATEPLAN_H

Plan *plan_cypher_create_path(PlannerInfo *root, RelOptInfo *rel,
                              CustomPath *best_path, List *tlist,
                              List *clauses, List *custom_plans);

Plan *plan_cypher_set_path(PlannerInfo *root, RelOptInfo *rel,
                           CustomPath *best_path, List *tlist,
                           List *clauses, List *custom_plans);

Plan *plan_cypher_delete_path(PlannerInfo *root, RelOptInfo *rel,
                              CustomPath *best_path, List *tlist,
                              List *clauses, List *custom_plans);

Plan *plan_cypher_merge_path(PlannerInfo *root, RelOptInfo *rel,
                             CustomPath *best_path, List *tlist,
                             List *clauses, List *custom_plans);

#endif
