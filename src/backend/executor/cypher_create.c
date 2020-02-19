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

#include "executor/tuptable.h"
#include "nodes/execnodes.h"
#include "nodes/extensible.h"
#include "nodes/nodes.h"
#include "nodes/plannodes.h"

#include "executor/cypher_executor.h"

static void begin_cypher_create(CustomScanState *node, EState *estate,
                                int eflags);
static TupleTableSlot *exec_cypher_create(CustomScanState *node);
static void end_cypher_create(CustomScanState *node);

const CustomExecMethods cypher_create_exec_methods = {
    "Cypher Create",
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
    NULL
};

static void begin_cypher_create(CustomScanState *node, EState *estate,
                                int eflags)
{
}

static TupleTableSlot *exec_cypher_create(CustomScanState *node)
{
    return NULL;
}

static void end_cypher_create(CustomScanState *node)
{
}

Node *create_cypher_create_plan_state(CustomScan *cscan)
{
    CustomScanState *css;

    css = makeNode(CustomScanState);

    css->methods = &cypher_create_exec_methods;

    return (Node *)css;
}
