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

#include "access/xact.h"
#include "nodes/makefuncs.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "nodes/plannodes.h"
#include "tcop/dest.h"
#include "tcop/utility.h"
#include "utils/lsyscache.h"

#include "catalog/ag_graph.h"
#include "catalog/ag_label.h"
#include "commands/label_commands.h"
#include "utils/agtype.h"
#include "utils/graphid.h"

static void create_table_for_vertex_label(char *graph_name, char *label_name);

Oid create_vertex_label(char *graph_name, char *label_name)
{
    Oid nsp_id;
    Oid relation_id;
    Oid label_oid;

    // create a table for the new lable
    create_table_for_vertex_label(graph_name, label_name);

    // TODO: generate "id" for label (use sequence)

    // record the new lable in ag_label
    nsp_id = get_graph_namespace(graph_name);
    relation_id = get_relname_relid(label_name, nsp_id);
    label_oid = insert_label(label_name, get_graph_oid(graph_name), 0,
                             LABEL_KIND_VERTEX, relation_id);
    CommandCounterIncrement();

    return label_oid;
}

// CREATE TABLE "graph_name"."label_name" (
//   "id" graphid PRIMARY KEY,
//   "properties" agtype NOT NULL DEFAULT "ag_catalog"."agtype_build_map"()
// )
static void create_table_for_vertex_label(char *graph_name, char *label_name)
{
    CreateStmt *create_stmt;
    ColumnDef *id;
    Constraint *pk;
    ColumnDef *props;
    Constraint *not_null;
    List *func_name;
    Constraint *props_default;
    PlannedStmt *wrapper;

    create_stmt = makeNode(CreateStmt);

    // relpersistence is set to RELPERSISTENCE_PERMANENT by makeRangeVar()
    create_stmt->relation = makeRangeVar(graph_name, label_name, -1);

    // "id" graphid PRIMARY KEY
    id = makeColumnDef("id", GRAPHIDOID, -1, InvalidOid);
    pk = makeNode(Constraint);
    pk->contype = CONSTR_PRIMARY;
    pk->location = -1;
    pk->keys = NULL;
    pk->options = NIL;
    pk->indexname = NULL;
    pk->indexspace = NULL;
    id->constraints = list_make1(pk);

    // "properties" agtype NOT NULL DEFAULT "ag_catalog"."agtype_build_map"()
    props = makeColumnDef("properties", AGTYPEOID, -1, InvalidOid);
    not_null = makeNode(Constraint);
    not_null->contype = CONSTR_NOTNULL;
    not_null->location = -1;
    props_default = makeNode(Constraint);
    props_default->contype = CONSTR_DEFAULT;
    props_default->location = -1;
    func_name = list_make2(makeString("ag_catalog"),
                           makeString("agtype_build_map"));
    props_default->raw_expr = (Node *)makeFuncCall(func_name, NIL, -1);
    props_default->cooked_expr = NULL;
    props->constraints = list_make2(not_null, props_default);

    create_stmt->tableElts = list_make2(id, props);

    create_stmt->inhRelations = NIL;
    create_stmt->partbound = NULL;
    create_stmt->ofTypename = NULL;
    create_stmt->constraints = NIL;
    create_stmt->options = NIL;
    create_stmt->oncommit = ONCOMMIT_NOOP;
    create_stmt->tablespacename = NULL;
    create_stmt->if_not_exists = false;

    wrapper = makeNode(PlannedStmt);
    wrapper->commandType = CMD_UTILITY;
    wrapper->canSetTag = false;
    wrapper->utilityStmt = (Node *)create_stmt;
    wrapper->stmt_location = -1;
    wrapper->stmt_len = 0;

    ProcessUtility(wrapper, "(generated CREATE TABLE command)",
                   PROCESS_UTILITY_SUBCOMMAND, NULL, NULL, None_Receiver,
                   NULL);
    // CommandCounterIncrement() is called in ProcessUtility()
}
