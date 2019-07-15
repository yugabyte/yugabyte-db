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

#include "ag_graph.h"

static Oid create_schema_for_graph(const Name graph_name);
static void drop_schema_for_graph(const Name graph_name, const bool cascade);

PG_FUNCTION_INFO_V1(create_graph);

Datum create_graph(PG_FUNCTION_ARGS)
{
    Name graph_name;
    Oid nsp_id;

    if (PG_ARGISNULL(0)) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("graph name must not be NULL")));
    }
    graph_name = PG_GETARG_NAME(0);

    nsp_id = create_schema_for_graph(graph_name);
    insert_graph(graph_name, nsp_id);

    // Make sure later steps can see the object created here.
    CommandCounterIncrement();

    ereport(NOTICE,
            (errmsg("graph \"%s\" has been created", NameStr(*graph_name))));

    PG_RETURN_VOID();
}

static Oid create_schema_for_graph(const Name graph_name)
{
    CreateSchemaStmt *stmt;
    Oid nsp_id;

    // This is the same with running `CREATE SCHEMA graph_name`.
    // schemaname doesn't have to be graph_name but the same name is used so
    // that users can find the backed schema for a graph only by its name.
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

    if (PG_ARGISNULL(0)) {
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

    nsp_id = get_graph_namespace(graph_name);

    if (!pg_namespace_ownercheck(nsp_id, GetUserId())) {
        aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_SCHEMA,
                       get_namespace_name(nsp_id));
    }

    object.classId = NamespaceRelationId;
    object.objectId = nsp_id;
    object.objectSubId = 0;

    behavior = cascade ? DROP_CASCADE : DROP_RESTRICT;

    performDeletion(&object, behavior, 0);
}
