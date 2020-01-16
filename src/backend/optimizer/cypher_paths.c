#include "postgres.h"

#include "catalog/pg_type_d.h"
#include "nodes/parsenodes.h"
#include "nodes/primnodes.h"
#include "nodes/relation.h"
#include "optimizer/pathnode.h"
#include "optimizer/paths.h"
#include "utils/builtins.h"
#include "utils/syscache.h"

#include "catalog/ag_catalog.h"
#include "optimizer/cypher_pathnode.h"
#include "optimizer/cypher_paths.h"

typedef enum cypher_clause_kind
{
    CYPHER_CLAUSE_NONE,
    CYPHER_CLAUSE_CREATE
} cypher_clause_kind;

// Cypher clause function OID cache
static struct
{
    Oid create;
} cypher_clause_func_oid;

static set_rel_pathlist_hook_type prev_set_rel_pathlist_hook;

static void set_rel_pathlist(PlannerInfo *root, RelOptInfo *rel, Index rti,
                             RangeTblEntry *rte);
static cypher_clause_kind get_cypher_clause_kind(RangeTblEntry *rte);
static void handle_cypher_create_clause(PlannerInfo *root, RelOptInfo *rel,
                                        Index rti, RangeTblEntry *rte);

void set_rel_pathlist_init(void)
{
    const Oid _oids[1] = {INTERNALOID};
    oidvector *args;
    Oid ag_catalog_oid;

    /*
     * initialize cypher_clause_func_oid to look up the OID of the function for
     * each updating clause only once.
     */

    args = buildoidvector(_oids, 1);
    ag_catalog_oid = ag_catalog_namespace_id();

    cypher_clause_func_oid.create = GetSysCacheOid3(
        PROCNAMEARGSNSP, PointerGetDatum("_cypher_create_clause"),
        PointerGetDatum(args), ObjectIdGetDatum(ag_catalog_oid));

    // install the hook
    prev_set_rel_pathlist_hook = set_rel_pathlist_hook;
    set_rel_pathlist_hook = set_rel_pathlist;
}

void set_rel_pathlist_fini(void)
{
    set_rel_pathlist_hook = prev_set_rel_pathlist_hook;
}

PG_FUNCTION_INFO_V1(_cypher_create_clause);

Datum _cypher_create_clause(PG_FUNCTION_ARGS)
{
    ereport(ERROR, (errmsg_internal("unhandled _cypher_create_clause(internal) function call")));

    PG_RETURN_NULL();
}

static void set_rel_pathlist(PlannerInfo *root, RelOptInfo *rel, Index rti,
                             RangeTblEntry *rte)
{
    if (prev_set_rel_pathlist_hook)
        prev_set_rel_pathlist_hook(root, rel, rti, rte);

    switch (get_cypher_clause_kind(rte))
    {
    case CYPHER_CLAUSE_CREATE:
        handle_cypher_create_clause(root, rel, rti, rte);
        break;
    case CYPHER_CLAUSE_NONE:
        break;
    default:
        ereport(ERROR, (errmsg_internal("invalid cypher_clause_kind")));
    }
}

/*
 * Check to see if the rte is a Cypher clause. An rte is only a Cypher clause
 * if it is a subquery, with the last entry in its target list, that is a
 * FuncExpr.
 */
static cypher_clause_kind get_cypher_clause_kind(RangeTblEntry *rte)
{
    TargetEntry *te;
    FuncExpr *fe;

    // If it's not a subquery, it's not a Cypher clause.
    if (rte->rtekind != RTE_SUBQUERY)
        return CYPHER_CLAUSE_NONE;

    // A Cypher clause function is always the last entry.
    te = llast(rte->subquery->targetList);

    // If the last entry is not a FuncExpr, it's not a Cypher clause.
    if (!IsA(te->expr, FuncExpr))
        return CYPHER_CLAUSE_NONE;

    fe = (FuncExpr *)te->expr;

    if (fe->funcid == cypher_clause_func_oid.create)
        return CYPHER_CLAUSE_CREATE;
    else
        return CYPHER_CLAUSE_NONE;
}

// replace all possible paths with our CustomPath
static void handle_cypher_create_clause(PlannerInfo *root, RelOptInfo *rel,
                                        Index rti, RangeTblEntry *rte)
{
    TargetEntry *te;
    FuncExpr *fe;
    Const *c;
    List *custom_private;
    CustomPath *cp;

    // Add the pattern to the CustomPath
    te = (TargetEntry *)llast(rte->subquery->targetList);
    fe = (FuncExpr *)te->expr;
    c = linitial(fe->args);
    custom_private = list_make1(DatumGetPointer(c->constvalue));

    // Discard any pre-existing paths
    rel->pathlist = NIL;
    rel->partial_pathlist = NIL;

    cp = create_cypher_create_path(root, rel, custom_private);
    add_path(rel, (Path *)cp);
}
