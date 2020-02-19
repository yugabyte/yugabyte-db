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
#include "catalog/dependency.h"
#include "catalog/objectaddress.h"
#include "catalog/pg_namespace.h"
#include "commands/schemacmds.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "utils/acl.h"
#include "utils/lsyscache.h"

#include "catalog/ag_graph.h"

static Oid create_schema_for_graph(const Name graph_name);
static void drop_schema_for_graph(const Name graph_name, const bool cascade);
static void rename_graph(const Name graph_name, const Name new_name);

PG_FUNCTION_INFO_V1(create_graph);

Datum create_graph(PG_FUNCTION_ARGS)
{
    Name graph_name;
    Oid nsp_id;

    if (PG_ARGISNULL(0))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("graph name must not be NULL")));
    }
    graph_name = PG_GETARG_NAME(0);

    nsp_id = create_schema_for_graph(graph_name);
    insert_graph(graph_name, nsp_id);

    // Make effects of this command visible.
    CommandCounterIncrement();

    ereport(NOTICE,
            (errmsg("graph \"%s\" has been created", NameStr(*graph_name))));

    PG_RETURN_VOID();
}

static Oid create_schema_for_graph(const Name graph_name)
{
    CreateSchemaStmt *stmt;
    Oid nsp_id;

    /*
     * This is the same with running `CREATE SCHEMA graph_name`.
     * schemaname doesn't have to be graph_name but the same name is used so
     * that users can find the backed schema for a graph only by its name.
     */
    stmt = makeNode(CreateSchemaStmt);
    stmt->schemaname = NameStr(*graph_name);
    stmt->authrole = NULL;
    stmt->schemaElts = NIL;
    stmt->if_not_exists = false;
    nsp_id = CreateSchemaCommand(stmt, "(generated CREATE SCHEMA command)", -1,
                                 -1);

    return nsp_id;
}

PG_FUNCTION_INFO_V1(drop_graph);

Datum drop_graph(PG_FUNCTION_ARGS)
{
    Name graph_name;
    bool cascade;

    if (PG_ARGISNULL(0))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("graph name must not be NULL")));
    }
    graph_name = PG_GETARG_NAME(0);
    cascade = PG_GETARG_BOOL(1);

    drop_schema_for_graph(graph_name, cascade);
    delete_graph(graph_name);

    // Make effects of this command visible.
    CommandCounterIncrement();

    ereport(NOTICE,
            (errmsg("graph \"%s\" has been dropped", NameStr(*graph_name))));

    PG_RETURN_VOID();
}

static void drop_schema_for_graph(const Name graph_name, const bool cascade)
{
    Oid nsp_id;
    ObjectAddress object;
    DropBehavior behavior;

    nsp_id = get_graph_namespace(NameStr(*graph_name));
    Assert(OidIsValid(nsp_id));

    if (!pg_namespace_ownercheck(nsp_id, GetUserId()))
    {
        aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_SCHEMA,
                       get_namespace_name(nsp_id));
    }

    object.classId = NamespaceRelationId;
    object.objectId = nsp_id;
    object.objectSubId = 0;

    behavior = cascade ? DROP_CASCADE : DROP_RESTRICT;

    performDeletion(&object, behavior, 0);
}

PG_FUNCTION_INFO_V1(alter_graph);

/*
 * Function alter_graph, invoked by the sql function -
 * alter_graph(graph_name name, operation cstring, new_value name)
 * NOTE: Currently only RENAME is supported.
 *       graph_name and new_value are case sensitive.
 *       operation is case insensitive.
 */
Datum alter_graph(PG_FUNCTION_ARGS)
{
    Name graph_name;
    Name new_value;
    char *operation;

    if (PG_ARGISNULL(0))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("graph_name must not be NULL")));
    }
    if (PG_ARGISNULL(1))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("operation must not be NULL")));
    }
    if (PG_ARGISNULL(2))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("new_value must not be NULL")));
    }

    graph_name = PG_GETARG_NAME(0);
    operation = PG_GETARG_CSTRING(1);
    new_value = PG_GETARG_NAME(2);

    if (strcasecmp("RENAME", operation) == 0)
    {
        rename_graph(graph_name, new_value);
    }
    else
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("invalid operation \"%s\"", operation),
                        errhint("valid operations: RENAME")));
    }

    // Make sure latter steps can see the results of this operation.
    CommandCounterIncrement();

    PG_RETURN_VOID();
}

/*
 * Function to rename a graph by renaming the schema (which is also the
 * namespace) and updating the name in ag_graph
 */
static void rename_graph(const Name graph_name, const Name new_name)
{
    char *oldname = NameStr(*graph_name);
    char *newname = NameStr(*new_name);

    RenameSchema(oldname, newname);
    update_graph_name(graph_name, new_name);

    ereport(NOTICE,
            (errmsg("graph \"%s\" renamed to \"%s\"", oldname, newname)));
}
