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

#include "catalog/ag_catalog.h"
#include "nodes/ag_nodes.h"
#include "optimizer/cypher_paths.h"
#include "parser/cypher_analyze.h"
#include "utils/ag_guc.h"

PG_MODULE_MAGIC;

void _PG_init(void);

void _PG_init(void)
{
    register_ag_nodes();
    set_rel_pathlist_init();
    object_access_hook_init();
    process_utility_hook_init();
    post_parse_analyze_init();
    define_config_params();
}

void _PG_fini(void);

void _PG_fini(void)
{
    post_parse_analyze_fini();
    process_utility_hook_fini();
    object_access_hook_fini();
    set_rel_pathlist_fini();
}
