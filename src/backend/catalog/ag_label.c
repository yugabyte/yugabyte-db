#include "postgres.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/xact.h"
#include "catalog/indexing.h"
#include "catalog/objectaddress.h"
#include "commands/tablecmds.h"
#include "nodes/makefuncs.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"

#include "catalog/ag_label.h"
#include "utils/agtype.h"

#define Anum_ag_graph_name 1
#define Anum_ag_label_name 2
#define Anum_ag_label_oid 3
#define Anum_ag_label_type 4

#define Natts_ag_label 4

static CreateStmt *makeCreateStmt(RangeVar *rv, List *tableElts);
Oid create_vertex_label(const Name graph_name, const Name label_name);
void add_label_to_catalog(const Name graph_name, const Name label_name,
                          const cypher_label_kind label_type,
                          const Oid label_oid);
static Oid ag_label_relation_id(void);
static Oid ag_labels_index_id(void);

static Oid ag_label_relation_id(void)
{
    Oid id;

    id = get_relname_relid("ag_label", ag_catalog_namespace_id());
    if (!OidIsValid(id))
    {
        ereport(ERROR, (errcode(ERRCODE_UNDEFINED_TABLE),
                        errmsg("table \"ag_label\" does not exist")));
    }

    return id;
}

static Oid ag_labels_index_id(void)
{
    Oid id;

    id = get_relname_relid("ag_label_index", ag_catalog_namespace_id());
    if (!OidIsValid(id))
    {
        ereport(ERROR, (errcode(ERRCODE_UNDEFINED_TABLE),
                        errmsg("index \"ag_label_index\" does not exist")));
    }

    return id;
}

void create_label(const Name graph_name, const Name label_name,
                  cypher_label_kind label_type)
{
    Oid label_oid;

    switch (label_type)
    {
    case CYPHER_LABEL_VERTEX:
        label_oid = create_vertex_label(graph_name, label_name);
        break;
    case CYPHER_LABEL_EDGE:
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("edges are not supported in CREATE clause")));
        break;
    default:
        ereport(ERROR, (errmsg("unrecognized label type")));
    }

    add_label_to_catalog(graph_name, label_name, label_type, label_oid);

    CommandCounterIncrement();
}

Oid create_vertex_label(const Name graph_name, const Name label_name)
{
    RangeVar *rv;
    List *tableElmts = NIL;
    ColumnDef *cd;
    CreateStmt *stmt;
    ObjectAddress reladdr;
    Oid graph_type_oid;

    rv = makeRangeVar(NameStr(*graph_name), NameStr(*label_name), -1);

    graph_type_oid =
        GetSysCacheOid2(TYPENAMENSP, CStringGetDatum("graphid"),
                        ObjectIdGetDatum(ag_catalog_namespace_id()));

    cd = makeColumnDef("vertex_id", graph_type_oid, 0, InvalidOid);
    tableElmts = lappend(tableElmts, cd);

    cd = makeColumnDef("props", AGTYPEOID, 0, InvalidOid);
    tableElmts = lappend(tableElmts, cd);

    stmt = makeCreateStmt(rv, tableElmts);
    reladdr = DefineRelation(stmt, RELKIND_RELATION, InvalidOid, NULL, NULL);

    return reladdr.objectId;
}

bool label_exists(const Name graph_name, const Name label_name,
                  cypher_label_kind label_type)
{
    ScanKeyData scan_keys[2];
    Relation ag_label;
    SysScanDesc scan_desc;
    HeapTuple tuple;

    ScanKeyInit(&scan_keys[0], Anum_ag_graph_name, BTEqualStrategyNumber,
                F_NAMEEQ, NameGetDatum(graph_name));

    ScanKeyInit(&scan_keys[1], Anum_ag_label_name, BTEqualStrategyNumber,
                F_NAMEEQ, NameGetDatum(label_name));

    ag_label = heap_open(ag_label_relation_id(), AccessShareLock);

    scan_desc = systable_beginscan(ag_label, ag_labels_index_id(), true, NULL,
                                   2, scan_keys);

    tuple = systable_getnext(scan_desc);

    heap_close(ag_label, AccessShareLock);

    systable_endscan(scan_desc);

    if (!HeapTupleIsValid(tuple))
    {
        return false;
    }

    // Will need to check label_type here when edges are implemented.

    return true;
}

// INSERT INTO ag_catalog.ag_label
// VALUES (graph_name, label_name, label_oid, label_type)
void add_label_to_catalog(const Name graph_name, const Name label_name,
                          const cypher_label_kind label_type,
                          const Oid label_oid)
{
    Datum values[Natts_ag_label];
    bool nulls[Natts_ag_label];
    Relation ag_label;
    HeapTuple tuple;

    values[Anum_ag_graph_name - 1] = NameGetDatum(graph_name);
    nulls[Anum_ag_graph_name - 1] = false;

    values[Anum_ag_label_name - 1] = NameGetDatum(label_name);
    nulls[Anum_ag_label_name - 1] = false;

    values[Anum_ag_label_oid - 1] = Int32GetDatum(label_oid);
    nulls[Anum_ag_label_oid - 1] = false;

    values[Anum_ag_label_type - 1] = Int8GetDatum(label_type);
    nulls[Anum_ag_label_type - 1] = false;

    ag_label = heap_open(ag_label_relation_id(), RowExclusiveLock);

    tuple = heap_form_tuple(RelationGetDescr(ag_label), values, nulls);

    /*
     * CatalogTupleInsert() is originally for PostgreSQL's catalog. However,
     * it is used at here for convenience. We don't care about the returned OID
     * because ag_label doesn't have OID column.
     */
    CatalogTupleInsert(ag_label, tuple);

    heap_close(ag_label, RowExclusiveLock);
}

static CreateStmt *makeCreateStmt(RangeVar *rv, List *tableElts)
{
    CreateStmt *cs = makeNode(CreateStmt);

    cs->relation = rv;
    cs->tableElts = tableElts;
    cs->inhRelations = NIL;
    cs->partbound = NULL;
    cs->ofTypename = NULL;
    cs->constraints = NIL;
    cs->options = NIL;
    cs->oncommit = ONCOMMIT_NOOP;
    cs->tablespacename = NULL;
    cs->if_not_exists = false;

    return cs;
}
