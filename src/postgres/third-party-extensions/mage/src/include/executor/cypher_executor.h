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

#ifndef AG_CYPHER_EXECUTOR_H
#define AG_CYPHER_EXECUTOR_H

#include "nodes/extensible.h"

#define DELETE_SCAN_STATE_NAME "Cypher Delete"
#define SET_SCAN_STATE_NAME "Cypher Set"
#define CREATE_SCAN_STATE_NAME "Cypher Create"
#define MERGE_SCAN_STATE_NAME "Cypher Merge"

Node *create_cypher_create_plan_state(CustomScan *cscan);
extern const CustomExecMethods cypher_create_exec_methods;

Node *create_cypher_set_plan_state(CustomScan *cscan);
extern const CustomExecMethods cypher_set_exec_methods;

Node *create_cypher_delete_plan_state(CustomScan *cscan);
extern const CustomExecMethods cypher_delete_exec_methods;

Node *create_cypher_merge_plan_state(CustomScan *cscan);
extern const CustomExecMethods cypher_merge_exec_methods;

#endif
