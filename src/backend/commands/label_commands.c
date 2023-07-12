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

#include "access/heapam.h"
#include "access/xact.h"
#include "catalog/dependency.h"
#include "catalog/namespace.h"
#include "catalog/objectaddress.h"
#include "catalog/pg_class_d.h"
#include "commands/defrem.h"
#include "commands/sequence.h"
#include "commands/tablecmds.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "nodes/plannodes.h"
#include "nodes/primnodes.h"
#include "nodes/value.h"
#include "parser/parse_node.h"
#include "parser/parser.h"
#include "storage/lockdefs.h"
#include "tcop/dest.h"
#include "tcop/utility.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"

#include "catalog/ag_graph.h"
#include "catalog/ag_label.h"
#include "commands/label_commands.h"
#include "utils/ag_cache.h"
#include "utils/agtype.h"
#include "utils/graphid.h"
#include "utils/name_validation.h"

/*
 * Relation name doesn't have to be label name but the same name is used so
 * that users can find the backed relation for a label only by its name.
 */
#define gen_label_relation_name(label_name) (label_name)

static void create_table_for_label(char *graph_name, char *label_name,
                                   char *schema_name, char *rel_name,
                                   char *seq_name, char label_type,
                                   List *parents);

// common
static List *create_edge_table_elements(char *graph_name, char *label_name,
                                        char *schema_name, char *rel_name,
                                        char *seq_name);
static List *create_vertex_table_elements(char *graph_name, char *label_name,
                                          char *schema_name, char *rel_name,
                                          char *seq_name);
static void create_sequence_for_label(RangeVar *seq_range_var);
static Constraint *build_pk_constraint(void);
static Constraint *build_id_default(char *graph_name, char *label_name,
                                    char *schema_name, char *seq_name);
static FuncCall *build_id_default_func_expr(char *graph_name, char *label_name,
                                            char *schema_name, char *seq_name);
static Constraint *build_not_null_constraint(void);
static Constraint *build_properties_default(void);
static void alter_sequence_owned_by_for_label(RangeVar *seq_range_var,
                                              char *rel_name);
static int32 get_new_label_id(Oid graph_oid, Oid nsp_id);
static void change_label_id_default(char *graph_name, char *label_name,
                                    char *schema_name, char *seq_name,
                                    Oid relid);

// drop
static void remove_relation(List *qname);
static void range_var_callback_for_remove_relation(const RangeVar *rel,
                                                   Oid rel_oid,
                                                   Oid odl_rel_oid,
                                                   void *arg);



PG_FUNCTION_INFO_V1(create_vlabel);

/*
 * This is a callback function
 * This function will be called when the user will call SELECT create_vlabel.
 * The function takes two parameters
 * 1. Graph name
 * 2. Label Name
 * Function will create a vertex label
 * Function returns an error if graph or label names or not provided
*/

Datum create_vlabel(PG_FUNCTION_ARGS)
{
    char *graph;
    Name graph_name;
    char *graph_name_str;
    Oid graph_oid;
    List *parent;

    RangeVar *rv;

    char *label;
    Name label_name;
    char *label_name_str;

    // checking if user has not provided the graph name
    if (PG_ARGISNULL(0))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("graph name must not be NULL")));
    }

    // checking if user has not provided the label name
    if (PG_ARGISNULL(1))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("label name must not be NULL")));
    }

    graph_name = PG_GETARG_NAME(0);
    label_name = PG_GETARG_NAME(1);

    graph_name_str = NameStr(*graph_name);
    label_name_str = NameStr(*label_name);

    // Check if graph does not exist
    if (!graph_exists(graph_name_str))
    {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_SCHEMA),
                        errmsg("graph \"%s\" does not exist.", graph_name_str)));
    }

    graph_oid = get_graph_oid(graph_name_str);

    // Check if label with the input name already exists
    if (label_exists(label_name_str, graph_oid))
    {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_SCHEMA),
                        errmsg("label \"%s\" already exists", label_name_str)));
    }

    //Create the default label tables
    graph = graph_name->data;
    label = label_name->data;

    rv = get_label_range_var(graph, graph_oid, AG_DEFAULT_LABEL_VERTEX);

    parent = list_make1(rv);

    create_label(graph, label, LABEL_TYPE_VERTEX, parent);

    ereport(NOTICE,
            (errmsg("VLabel \"%s\" has been created", NameStr(*label_name))));

    PG_RETURN_VOID();
}

PG_FUNCTION_INFO_V1(create_elabel);

/*
 * This is a callback function
 * This function will be called when the user will call SELECT create_elabel.
 * The function takes two parameters
 * 1. Graph name
 * 2. Label Name
 * Function will create an edge label
 * Function returns an error if graph or label names or not provided
*/

Datum create_elabel(PG_FUNCTION_ARGS)
{
    char *graph;
    Name graph_name;
    char *graph_name_str;
    Oid graph_oid;
    List *parent;

    RangeVar *rv;

    char *label;
    Name label_name;
    char *label_name_str;

    // checking if user has not provided the graph name
    if (PG_ARGISNULL(0))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("graph name must not be NULL")));
    }

    // checking if user has not provided the label name
    if (PG_ARGISNULL(1))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("label name must not be NULL")));
    }

    graph_name = PG_GETARG_NAME(0);
    label_name = PG_GETARG_NAME(1);

    graph_name_str = NameStr(*graph_name);
    label_name_str = NameStr(*label_name);

    // Check if graph does not exist
    if (!graph_exists(graph_name_str))
    {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_SCHEMA),
                 errmsg("graph \"%s\" does not exist.", graph_name_str)));
    }

    graph_oid = get_graph_oid(graph_name_str);

    // Check if label with the input name already exists
    if (label_exists(label_name_str, graph_oid))
    {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_SCHEMA),
                        errmsg("label \"%s\" already exists", label_name_str)));
    }

    //Create the default label tables
    graph = graph_name->data;
    label = label_name->data;

    rv = get_label_range_var(graph, graph_oid, AG_DEFAULT_LABEL_EDGE);

    parent = list_make1(rv);
    create_label(graph, label, LABEL_TYPE_EDGE, parent);

    ereport(NOTICE,
            (errmsg("ELabel \"%s\" has been created", NameStr(*label_name))));

    PG_RETURN_VOID();
}

/*
 * For the new label, create an entry in ag_catalog.ag_label, create a
 * new table and sequence. Returns the oid from the new tuple in
 * ag_catalog.ag_label.
 */
void create_label(char *graph_name, char *label_name, char label_type,
                  List *parents)
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

    if (!is_valid_label(label_name, label_type))
    {
        ereport(ERROR, (errcode(ERRCODE_UNDEFINED_SCHEMA),
                        errmsg("label name is invalid")));
    }

    if (!is_valid_label(label_name, label_type))
    {
        ereport(ERROR, (errcode(ERRCODE_UNDEFINED_SCHEMA),
                        errmsg("label name is invalid")));
    }

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
    rel_name = gen_label_relation_name(label_name);
    seq_name = ChooseRelationName(rel_name, "id", "seq", nsp_id, false);
    seq_range_var = makeRangeVar(schema_name, seq_name, -1);
    create_sequence_for_label(seq_range_var);

    // create a table for the new label
    create_table_for_label(graph_name, label_name, schema_name, rel_name,
                           seq_name, label_type, parents);

    // record the new label in ag_label
    relation_id = get_relname_relid(rel_name, nsp_id);

    // If a label has parents, switch the parents id default, with its own.
    if (list_length(parents) != 0)
        change_label_id_default(graph_name, label_name, schema_name, seq_name,
                                relation_id);

    // associate the sequence with the "id" column
    alter_sequence_owned_by_for_label(seq_range_var, rel_name);

    // get a new "id" for the new label
    label_id = get_new_label_id(graph_oid, nsp_id);

    insert_label(label_name, graph_oid, label_id, label_type,
                 relation_id, seq_name);

    CommandCounterIncrement();
}

// CREATE TABLE `schema_name`.`rel_name` (
//   "id" graphid PRIMARY KEY DEFAULT "ag_catalog"."_graphid"(...),
//   "start_id" graphid NOT NULL note: only for edge labels
//   "end_id" graphid NOT NULL  note: only for edge labels
//   "properties" agtype NOT NULL DEFAULT "ag_catalog"."agtype_build_map"()
// )
static void create_table_for_label(char *graph_name, char *label_name,
                                   char *schema_name, char *rel_name,
                                   char *seq_name, char label_type,
                                   List *parents)
{
    CreateStmt *create_stmt;
    PlannedStmt *wrapper;

    create_stmt = makeNode(CreateStmt);

    // relpersistence is set to RELPERSISTENCE_PERMANENT by makeRangeVar()
    create_stmt->relation = makeRangeVar(schema_name, rel_name, -1);

    /*
     * When a new table has parents, do not create a column definition list.
     * Use the parents' column definition list instead, via Postgres'
     * inheritance system.
     */
    if (list_length(parents) != 0)
        create_stmt->tableElts = NIL;
    else if (label_type == LABEL_TYPE_EDGE)
        create_stmt->tableElts = create_edge_table_elements(
            graph_name, label_name, schema_name, rel_name, seq_name);
    else if (label_type == LABEL_TYPE_VERTEX)
        create_stmt->tableElts = create_vertex_table_elements(
            graph_name, label_name, schema_name, rel_name, seq_name);
    else
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                        errmsg("undefined label type \'%c\'", label_type)));

    create_stmt->inhRelations = parents;
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

// CREATE TABLE `schema_name`.`rel_name` (
//   "id" graphid PRIMARY KEY DEFAULT "ag_catalog"."_graphid"(...),
//   "start_id" graphid NOT NULL
//   "end_id" graphid NOT NULL
//   "properties" agtype NOT NULL DEFAULT "ag_catalog"."agtype_build_map"()
// )
static List *create_edge_table_elements(char *graph_name, char *label_name,
                                        char *schema_name, char *rel_name,
                                        char *seq_name)
{
    ColumnDef *id;
    ColumnDef *start_id;
    ColumnDef *end_id;
    ColumnDef *props;

    // "id" graphid PRIMARY KEY DEFAULT "ag_catalog"."_graphid"(...)
    id = makeColumnDef(AG_EDGE_COLNAME_ID, GRAPHIDOID, -1, InvalidOid);
    id->constraints = list_make2(build_pk_constraint(),
                                 build_id_default(graph_name, label_name,
                                                  schema_name, seq_name));

    // "start_id" graphid NOT NULL
    start_id = makeColumnDef(AG_EDGE_COLNAME_START_ID, GRAPHIDOID, -1,
                             InvalidOid);
    start_id->constraints = list_make1(build_not_null_constraint());

    // "end_id" graphid NOT NULL
    end_id = makeColumnDef(AG_EDGE_COLNAME_END_ID, GRAPHIDOID, -1, InvalidOid);
    end_id->constraints = list_make1(build_not_null_constraint());

    // "properties" agtype NOT NULL DEFAULT "ag_catalog"."agtype_build_map"()
    props = makeColumnDef(AG_EDGE_COLNAME_PROPERTIES, AGTYPEOID, -1,
                          InvalidOid);
    props->constraints = list_make2(build_not_null_constraint(),
                                    build_properties_default());

    return list_make4(id, start_id, end_id, props);
}

// CREATE TABLE `schema_name`.`rel_name` (
//   "id" graphid PRIMARY KEY DEFAULT "ag_catalog"."_graphid"(...),
//   "properties" agtype NOT NULL DEFAULT "ag_catalog"."agtype_build_map"()
// )
static List *create_vertex_table_elements(char *graph_name, char *label_name,
                                          char *schema_name, char *rel_name,
                                          char *seq_name)
{
    ColumnDef *id;
    ColumnDef *props;

    // "id" graphid PRIMARY KEY DEFAULT "ag_catalog"."_graphid"(...)
    id = makeColumnDef(AG_VERTEX_COLNAME_ID, GRAPHIDOID, -1, InvalidOid);
    id->constraints = list_make2(build_pk_constraint(),
                                 build_id_default(graph_name, label_name,
                                                  schema_name, seq_name));

    // "properties" agtype NOT NULL DEFAULT "ag_catalog"."agtype_build_map"()
    props = makeColumnDef(AG_VERTEX_COLNAME_PROPERTIES, AGTYPEOID, -1,
                          InvalidOid);
    props->constraints = list_make2(build_not_null_constraint(),
                                    build_properties_default());

    return list_make2(id, props);
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

/*
 * Builds the primary key constraint for when a table is created.
 */
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

/*
 * Construct a FuncCall node that will create the default logic for the label's
 * id.
 */
static FuncCall *build_id_default_func_expr(char *graph_name, char *label_name,
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

    // Build a node that gets the label id
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

    //Build a node that will get the next val from the label's sequence
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

    /*
     * Build a node that constructs the graphid from the label id function
     * and the next val function for the given sequence.
     */
    graphid_func_name = list_make2(makeString("ag_catalog"),
                                   makeString("_graphid"));
    graphid_func_args = list_make2(label_id_func, nextval_func);
    graphid_func = makeFuncCall(graphid_func_name, graphid_func_args, -1);

    return graphid_func;
}

/*
 * Construct a default constraint on the id column for a newly created table
 */
static Constraint *build_id_default(char *graph_name, char *label_name,
                                    char *schema_name, char *seq_name)
{
    FuncCall *graphid_func;
    Constraint *id_default;

    graphid_func = build_id_default_func_expr(graph_name, label_name,
                                              schema_name, seq_name);

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

/*
 * Alter the default constraint on the label's id to the use the given
 * sequence.
 */
static void change_label_id_default(char *graph_name, char *label_name,
                                    char *schema_name, char *seq_name,
                                    Oid relid)
{
    ParseState *pstate;
    AlterTableStmt *tbl_stmt;
    AlterTableCmd *tbl_cmd;
    RangeVar *rv;
    FuncCall *func_call;
    AlterTableUtilityContext atuc;

    func_call = build_id_default_func_expr(graph_name, label_name, schema_name,
                                           seq_name);

    rv = makeRangeVar(schema_name, label_name, -1);

    pstate = make_parsestate(NULL);
    pstate->p_sourcetext = "(generated ALTER TABLE command)";

    tbl_stmt = makeNode(AlterTableStmt);
    tbl_stmt->relation = rv;
    tbl_stmt->missing_ok = false;

    tbl_cmd = makeNode(AlterTableCmd);
    tbl_cmd->subtype = AT_ColumnDefault;
    tbl_cmd->name = "id";
    tbl_cmd->def = (Node *)func_call;

    tbl_stmt->cmds = list_make1(tbl_cmd);

    atuc.relid = relid;
    atuc.queryEnv = pstate->p_queryEnv;
    atuc.queryString = pstate->p_sourcetext;

    AlterTable(tbl_stmt, AccessExclusiveLock, &atuc);

    CommandCounterIncrement();
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
        int32 label_id;

        // the data type of the sequence is integer (int4)
        label_id = (int32) nextval_internal(seq_id, true);
        Assert(label_id_is_valid(label_id));
        if (!label_id_exists(graph_oid, label_id))
        {
            return (int32) label_id;
        }
    }

    ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                    errmsg("no more new labels are available"),
                    errhint("The maximum number of labels in a graph is %d",
                            LABEL_ID_MAX)));
    return 0;
}

PG_FUNCTION_INFO_V1(drop_label);

Datum drop_label(PG_FUNCTION_ARGS)
{
    Name graph_name;
    Name label_name;
    bool force;
    char *graph_name_str;
    graph_cache_data *cache_data;
    Oid graph_oid;
    Oid nsp_id;
    char *label_name_str;
    Oid label_relation;
    char *schema_name;
    char *rel_name;
    List *qname;

    if (PG_ARGISNULL(0))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("graph name must not be NULL")));
    }
    if (PG_ARGISNULL(1))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("label name must not be NULL")));
    }
    graph_name = PG_GETARG_NAME(0);
    label_name = PG_GETARG_NAME(1);
    force = PG_GETARG_BOOL(2);

    graph_name_str = NameStr(*graph_name);
    cache_data = search_graph_name_cache(graph_name_str);
    if (!cache_data)
    {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_SCHEMA),
                 errmsg("graph \"%s\" does not exist", graph_name_str)));
    }
    graph_oid = cache_data->oid;
    nsp_id = cache_data->namespace;

    label_name_str = NameStr(*label_name);
    label_relation = get_label_relation(label_name_str, graph_oid);
    if (!OidIsValid(label_relation))
    {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_TABLE),
                 errmsg("label \"%s\" does not exist", label_name_str)));
    }

    if (force)
    {
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("force option is not supported yet")));
    }

    schema_name = get_namespace_name(nsp_id);
    rel_name = get_rel_name(label_relation);
    qname = list_make2(makeString(schema_name), makeString(rel_name));

    remove_relation(qname);
    // CommandCounterIncrement() is called in performDeletion()

    // delete_label() will be called in object_access()

    ereport(NOTICE, (errmsg("label \"%s\".\"%s\" has been dropped",
                            graph_name_str, label_name_str)));

    PG_RETURN_VOID();
}

// See RemoveRelations() for more details.
static void remove_relation(List *qname)
{
    RangeVar *rel;
    Oid rel_oid;
    ObjectAddress address;

    AssertArg(list_length(qname) == 2);

    // concurrent is false so lockmode is AccessExclusiveLock

    // relkind is RELKIND_RELATION

    AcceptInvalidationMessages();

    rel = makeRangeVarFromNameList(qname);
    rel_oid = RangeVarGetRelidExtended(rel, AccessExclusiveLock,
                                       RVR_MISSING_OK,
                                       range_var_callback_for_remove_relation,
                                       NULL);

    if (!OidIsValid(rel_oid))
    {
        /*
         * before calling this function, this condition is already checked in
         * drop_graph()
         */
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                        errmsg("ag_label catalog is corrupted"),
                        errhint("Table \"%s\".\"%s\" does not exist",
                                rel->schemaname, rel->relname)));
    }

    // concurrent is false

    ObjectAddressSet(address, RelationRelationId, rel_oid);

    /*
     * set PERFORM_DELETION_INTERNAL flag so that object_access_hook can ignore
     * this deletion
     */
    performDeletion(&address, DROP_RESTRICT, PERFORM_DELETION_INTERNAL);
}

// See RangeVarCallbackForDropRelation() for more details.
static void range_var_callback_for_remove_relation(const RangeVar *rel,
                                                   Oid rel_oid,
                                                   Oid odl_rel_oid,
                                                   void *arg)
{
    /*
     * arg is NULL because relkind is always RELKIND_RELATION, heapOid is
     * always InvalidOid, partParentOid is always InvalidOid, and concurrent is
     * always false. See RemoveRelations() for more details.
     */

    // heapOid is always InvalidOid

    // partParentOid is always InvalidOid

    if (!OidIsValid(rel_oid))
        return;

    // classform->relkind is always RELKIND_RELATION

    // relkind == expected_relkind

    if (!pg_class_ownercheck(rel_oid, GetUserId()) &&
        !pg_namespace_ownercheck(get_rel_namespace(rel_oid), GetUserId()))
    {
        aclcheck_error(ACLCHECK_NOT_OWNER,
                       get_relkind_objtype(get_rel_relkind(rel_oid)),
                       rel->relname);
    }

    // the target relation is not system class

    // relkind is always RELKIND_RELATION

    // is_partition is false
}
