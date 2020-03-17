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
#include "commands/defrem.h"
#include "commands/sequence.h"
#include "nodes/makefuncs.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "nodes/plannodes.h"
#include "nodes/value.h"
#include "parser/parse_node.h"
#include "parser/parser.h"
#include "tcop/dest.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"

#include "catalog/ag_graph.h"
#include "catalog/ag_label.h"
#include "commands/label_commands.h"
#include "utils/ag_cache.h"
#include "utils/agtype.h"
#include "utils/graphid.h"

static void create_table_for_vertex_label(char *graph_name, char *label_name,
                                          char *schema_name, char *seq_name);

// common
static void create_sequence_for_label(RangeVar *seq_range_var);
static Constraint *build_pk_constraint(void);
static Constraint *build_id_default(char *graph_name, char *label_name,
                                    char *schema_name, char *seq_name);
static Constraint *build_not_null_constraint(void);
static Constraint *build_properties_default(void);
static void alter_sequence_owned_by_for_label(RangeVar *seq_range_var,
                                              char *rel_name);
static int32 get_new_label_id(Oid graph_oid, Oid nsp_id);

Oid create_vertex_label(char *graph_name, char *label_name)
{
    graph_cache_data *cache_data;
    Oid graph_oid;
    Oid nsp_id;
    char *schema_name;
    char *rel_name;
    char *seq_name;
    RangeVar *seq_range_var;
    int32 label_id;
    Oid relation_id;
    Oid label_oid;

    cache_data = search_graph_name_cache(graph_name);
    if (!cache_data)
    {
        ereport(ERROR, (errcode(ERRCODE_UNDEFINED_SCHEMA),
                        errmsg("graph \"%s\" does not exist", graph_name)));
    }
    graph_oid = cache_data->oid;
    nsp_id = cache_data->namespace;

    // create a sequence for the new label to generate unique IDs for vertices
    schema_name = get_namespace_name(nsp_id);
    rel_name = get_label_relation_name(label_name);
    seq_name = ChooseRelationName(rel_name, "id", "seq", nsp_id, false);
    seq_range_var = makeRangeVar(schema_name, seq_name, -1);
    create_sequence_for_label(seq_range_var);

    // create a table for the new label
    create_table_for_vertex_label(graph_name, label_name, schema_name,
                                  seq_name);

    // associate the sequence with the "id" column
    alter_sequence_owned_by_for_label(seq_range_var, rel_name);

    // get a new "id" for the new label
    label_id = get_new_label_id(graph_oid, nsp_id);

    // record the new label in ag_label
    relation_id = get_relname_relid(rel_name, nsp_id);
    label_oid = insert_label(label_name, graph_oid, label_id,
                             LABEL_KIND_VERTEX, relation_id);
    CommandCounterIncrement();

    return label_oid;
}

// CREATE TABLE `schema_name`.`rel_name` (
//   "id" graphid PRIMARY KEY DEFAULT "ag_catalog"."_graphid"(...),
//   "properties" agtype NOT NULL DEFAULT "ag_catalog"."agtype_build_map"()
// )
static void create_table_for_vertex_label(char *graph_name, char *label_name,
                                          char *schema_name, char *seq_name)
{
    CreateStmt *create_stmt;
    char *rel_name;
    ColumnDef *id;
    ColumnDef *props;
    PlannedStmt *wrapper;

    create_stmt = makeNode(CreateStmt);

    // relpersistence is set to RELPERSISTENCE_PERMANENT by makeRangeVar()
    rel_name = get_label_relation_name(label_name);
    create_stmt->relation = makeRangeVar(schema_name, rel_name, -1);

    // "id" graphid PRIMARY KEY DEFAULT "ag_catalog"."_graphid"(...)
    id = makeColumnDef("id", GRAPHIDOID, -1, InvalidOid);
    id->constraints = list_make2(build_pk_constraint(),
                                 build_id_default(graph_name, label_name,
                                                  schema_name, seq_name));

    // "properties" agtype NOT NULL DEFAULT "ag_catalog"."agtype_build_map"()
    props = makeColumnDef("properties", AGTYPEOID, -1, InvalidOid);
    props->constraints = list_make2(build_not_null_constraint(),
                                    build_properties_default());

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

// CREATE SEQUENCE `seq_range_var` MAXVALUE `LOCAL_ID_MAX`
static void create_sequence_for_label(RangeVar *seq_range_var)
{
    ParseState *pstate;
    CreateSeqStmt *seq_stmt;
    char buf[32]; // greater than MAXINT8LEN+1
    DefElem *maxvalue;

    pstate = make_parsestate(NULL);
    pstate->p_sourcetext = "(generated CREATE SEQUENCE command)";

    seq_stmt = makeNode(CreateSeqStmt);
    seq_stmt->sequence = seq_range_var;
    pg_lltoa(ENTRY_ID_MAX, buf);
    maxvalue = makeDefElem("maxvalue", (Node *)makeFloat(pstrdup(buf)), -1);
    seq_stmt->options = list_make1(maxvalue);
    seq_stmt->ownerId = InvalidOid;
    seq_stmt->for_identity = false;
    seq_stmt->if_not_exists = false;

    DefineSequence(pstate, seq_stmt);
    CommandCounterIncrement();
}

// PRIMARY KEY
static Constraint *build_pk_constraint(void)
{
    Constraint *pk;

    pk = makeNode(Constraint);
    pk->contype = CONSTR_PRIMARY;
    pk->location = -1;
    pk->keys = NULL;
    pk->options = NIL;
    pk->indexname = NULL;
    pk->indexspace = NULL;

    return pk;
}

// "ag_catalog"."_graphid"(
//   "ag_catalog"."_label_id"(`graph_name`, `label_name`),
//   "nextval"('`schema_name`.`seq_name`'::"regclass")
// )
static Constraint *build_id_default(char *graph_name, char *label_name,
                                    char *schema_name, char *seq_name)
{
    List *label_id_func_name;
    A_Const *graph_name_const;
    A_Const *label_name_const;
    List *label_id_func_args;
    FuncCall *label_id_func;
    List *nextval_func_name;
    char *qualified_seq_name;
    A_Const *qualified_seq_name_const;
    TypeCast *regclass_cast;
    List *nextval_func_args;
    FuncCall *nextval_func;
    List *graphid_func_name;
    List *graphid_func_args;
    FuncCall *graphid_func;
    Constraint *id_default;

    // "ag_catalog"."_label_id"(`graph_name`, `label_name`)
    label_id_func_name = list_make2(makeString("ag_catalog"),
                                    makeString("_label_id"));
    graph_name_const = makeNode(A_Const);
    graph_name_const->val.type = T_String;
    graph_name_const->val.val.str = graph_name;
    graph_name_const->location = -1;
    label_name_const = makeNode(A_Const);
    label_name_const->val.type = T_String;
    label_name_const->val.val.str = label_name;
    label_name_const->location = -1;
    label_id_func_args = list_make2(graph_name_const, label_name_const);
    label_id_func = makeFuncCall(label_id_func_name, label_id_func_args, -1);

    // "nextval"('`schema_name`.`seq_name`'::"regclass")
    nextval_func_name = SystemFuncName("nextval");
    qualified_seq_name = quote_qualified_identifier(schema_name, seq_name);
    qualified_seq_name_const = makeNode(A_Const);
    qualified_seq_name_const->val.type = T_String;
    qualified_seq_name_const->val.val.str = qualified_seq_name;
    qualified_seq_name_const->location = -1;
    regclass_cast = makeNode(TypeCast);
    regclass_cast->typeName = SystemTypeName("regclass");
    regclass_cast->arg = (Node *)qualified_seq_name_const;
    regclass_cast->location = -1;
    nextval_func_args = list_make1(regclass_cast);
    nextval_func = makeFuncCall(nextval_func_name, nextval_func_args, -1);

    // "ag_catalog"."_graphid"(...)
    graphid_func_name = list_make2(makeString("ag_catalog"),
                                   makeString("_graphid"));
    graphid_func_args = list_make2(label_id_func, nextval_func);
    graphid_func = makeFuncCall(graphid_func_name, graphid_func_args, -1);

    id_default = makeNode(Constraint);
    id_default->contype = CONSTR_DEFAULT;
    id_default->location = -1;
    id_default->raw_expr = (Node *)graphid_func;
    id_default->cooked_expr = NULL;

    return id_default;
}

// NOT NULL
static Constraint *build_not_null_constraint(void)
{
    Constraint *not_null;

    not_null = makeNode(Constraint);
    not_null->contype = CONSTR_NOTNULL;
    not_null->location = -1;

    return not_null;
}

// DEFAULT "ag_catalog"."agtype_build_map"()
static Constraint *build_properties_default(void)
{
    List *func_name;
    FuncCall *func;
    Constraint *props_default;

    // "ag_catalog"."agtype_build_map"()
    func_name = list_make2(makeString("ag_catalog"),
                           makeString("agtype_build_map"));
    func = makeFuncCall(func_name, NIL, -1);

    props_default = makeNode(Constraint);
    props_default->contype = CONSTR_DEFAULT;
    props_default->location = -1;
    props_default->raw_expr = (Node *)func;
    props_default->cooked_expr = NULL;

    return props_default;
}

// CREATE SEQUENCE `seq_range_var` OWNED BY `schema_name`.`rel_name`."id"
static void alter_sequence_owned_by_for_label(RangeVar *seq_range_var,
                                              char *rel_name)
{
    ParseState *pstate;
    AlterSeqStmt *seq_stmt;
    char *schema_name;
    List *id;
    DefElem *owned_by;

    pstate = make_parsestate(NULL);
    pstate->p_sourcetext = "(generated ALTER SEQUENCE command)";

    seq_stmt = makeNode(AlterSeqStmt);
    seq_stmt->sequence = seq_range_var;
    schema_name = seq_range_var->schemaname;
    id = list_make3(makeString(schema_name), makeString(rel_name),
                    makeString("id"));
    owned_by = makeDefElem("owned_by", (Node *)id, -1);
    seq_stmt->options = list_make1(owned_by);
    seq_stmt->for_identity = false;
    seq_stmt->missing_ok = false;

    AlterSequence(pstate, seq_stmt);
    CommandCounterIncrement();
}

static int32 get_new_label_id(Oid graph_oid, Oid nsp_id)
{
    Oid seq_id;
    int cnt;

    // get the OID of the sequence
    seq_id = get_relname_relid(LABEL_ID_SEQ_NAME, nsp_id);
    if (!OidIsValid(seq_id))
    {
        ereport(ERROR, (errcode(ERRCODE_UNDEFINED_TABLE),
                        errmsg("sequence \"%s\" does not exists",
                               LABEL_ID_SEQ_NAME)));
    }

    for (cnt = LABEL_ID_MIN; cnt <= LABEL_ID_MAX; cnt++)
    {
        int64 label_id;

        // the data type of the sequence is integer (int4)
        label_id = nextval_internal(seq_id, true);
        Assert(label_id_is_valid(label_id));
        if (!label_id_exists(graph_oid, label_id))
            return (int32)label_id;
    }

    ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                    errmsg("no more new labels are available"),
                    errhint("The maximum number of labels in a graph is %d",
                            LABEL_ID_MAX)));
    return 0;
}
