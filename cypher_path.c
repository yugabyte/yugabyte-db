#include "postgres.h"

#include "catalog/namespace.h"
#include "catalog/pg_type_d.h"
#include "optimizer/paths.h"
#include "optimizer/tlist.h"
#include "utils/builtins.h"
#include "utils/syscache.h"

#include "agtype.h"
#include "ag_catalog.h"
#include "cypher_path.h"
#include "cypher_scan.h"
#include "nodes.h"

typedef struct cypher_create_path
{
    CustomPath cp;
    List *pattern;
} cypher_create_path;

const CustomPathMethods cypher_create_path_methods = {
    "Cypher Create Path Custom Methods", plan_cypher_create_path, NULL};

static set_rel_pathlist_hook_type prev_set_rel_pathlist_hook;

static void cypher_set_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
                                    Index rti, RangeTblEntry *rte);
static bool is_cypher_create_clause(PlannerInfo *root, RelOptInfo *rel,
                                    Index rti, RangeTblEntry *rte);
static void handle_cypher_create_clause(PlannerInfo *root, RelOptInfo *rel,
                                        Index rti, RangeTblEntry *rte);

void set_rel_pathlist_init(void)
{
    prev_set_rel_pathlist_hook = set_rel_pathlist_hook;
    set_rel_pathlist_hook = cypher_set_rel_pathlist;
}

void set_rel_pathlist_fini(void)
{
    set_rel_pathlist_hook = prev_set_rel_pathlist_hook;
}

static void cypher_set_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
                                    Index rti, RangeTblEntry *rte)
{
    if (prev_set_rel_pathlist_hook)
        prev_set_rel_pathlist_hook(root, rel, rti, rte);

    if (is_cypher_create_clause(root, rel, rti, rte))
        handle_cypher_create_clause(root, rel, rti, rte);
}

PG_FUNCTION_INFO_V1(cypher_create_clause);
Datum cypher_create_clause(PG_FUNCTION_ARGS)
{
    ereport(ERROR, (errmsg("invalid use of cypher CREATE clause")));
}

PG_FUNCTION_INFO_V1(cypher_dummy_clause);
Datum cypher_dummy_clause(PG_FUNCTION_ARGS)
{
    ereport(ERROR, (errmsg("invalid use of cypher DUMMY clause")));
}

struct Plan *plan_cypher_create_path(PlannerInfo *root, RelOptInfo *rel,
                                     struct CustomPath *best_path, List *tlist,
                                     List *clauses, List *custom_plans)
{
    cypher_create_path *ccp = (cypher_create_path *)best_path;
    List *custom_private = NIL;
    CustomScan *cs = makeNode(CustomScan);

    cs->scan.plan.type = ccp->cp.path.pathtype;
    cs->scan.plan.startup_cost = ccp->cp.path.startup_cost;
    cs->scan.plan.total_cost = ccp->cp.path.total_cost;
    cs->scan.plan.plan_rows = 0;
    cs->scan.plan.plan_width = 0;

    cs->scan.plan.parallel_aware = ccp->cp.path.parallel_aware;
    cs->scan.plan.parallel_safe = ccp->cp.path.parallel_safe;

    // Set later in set_plan_refs
    cs->scan.plan.plan_node_id = 0;
    cs->scan.plan.targetlist = tlist;
    cs->scan.plan.qual = NIL;
    cs->scan.plan.lefttree = NULL;
    cs->scan.plan.righttree = NULL;
    cs->scan.plan.initPlan = NIL;
    cs->scan.plan.extParam = NULL;
    cs->scan.plan.allParam = NULL;
    cs->scan.scanrelid = 0;
    cs->flags = ccp->cp.flags;

    custom_private = lappend(custom_private, ccp->pattern);
    cs->custom_private = custom_private;

    // Drop the child path for basic CREATE clause
    cs->custom_plans = NIL;
    cs->custom_exprs = NULL;
    cs->custom_scan_tlist = tlist;
    cs->custom_relids = NULL;

    cs->methods = &cypher_create_scan_methods;

    return (Plan *)cs;
}

/*
 * Check to see if the rte is a cypher clause. An rte is only
 * a cypher clause if it is a Subquery, with one entry in it's
 * target list, that is a FuncExpr
 */
static bool is_cypher_create_clause(PlannerInfo *root, RelOptInfo *rel,
                                    Index rti, RangeTblEntry *rte)
{
    FuncExpr *func_expr;
    Oid func_create_oid;
    Oid internal_type = INTERNALOID;
    TargetEntry *target_entry;

    // If its not a subquery, its not a cypher_clause
    if (rte->rtekind != RTE_SUBQUERY)
        return false;

    target_entry = llast(rte->subquery->targetList);

    // If the one entry is not a FuncExpr, its not a cypher clause
    if (!IsA(target_entry->expr, FuncExpr))
        return false;

    // Get the function oid for the cypher_clause_type and see if it matches
    // the FuncExpr funcid
    func_create_oid = GetSysCacheOid3(
        PROCNAMEARGSNSP, PointerGetDatum("cypher_create_clause"),
        PointerGetDatum(buildoidvector(&internal_type, 1)),
        ObjectIdGetDatum(ag_catalog_namespace_id()));
    func_expr = (FuncExpr *)target_entry->expr;

    if (func_create_oid != func_expr->funcid)
        return false;

    return true;
}

/*
 * Process the relation and replace all possible paths with our CustomPath.
 */
static void handle_cypher_create_clause(PlannerInfo *root, RelOptInfo *rel,
                                        Index rti, RangeTblEntry *rte)
{
    Const *c;
    cypher_create_path *ccp;
    FuncExpr *func_expr;
    TargetEntry *target_entry;

    ccp = palloc(sizeof(cypher_create_path));
    ccp->cp.path.type = T_CustomPath;
    ccp->cp.path.pathtype = T_CustomScan;
    ccp->cp.path.parent = rel;
    ccp->cp.path.pathtarget = rel->reltarget;

    ccp->cp.path.param_info = NULL;
    // Do not allow parallel methods
    ccp->cp.path.parallel_aware = false;
    ccp->cp.path.parallel_safe = false;
    ccp->cp.path.parallel_workers = 0;
    // Basic CREATE will not return rows
    ccp->cp.path.rows = 0;
    // Basic create will not fetch any pages
    ccp->cp.path.startup_cost = 0;
    ccp->cp.path.total_cost = 0;
    // No output ordering for Basic CREATE
    ccp->cp.path.pathkeys = NULL;
    // Disable all custom flags for now
    ccp->cp.flags = 0;
    // Basic CREATE clauses do not have children
    ccp->cp.custom_paths = NIL;
    ccp->cp.custom_private = NIL;

    ccp->cp.methods = &cypher_create_path_methods;

    // Add the list of patterns from in the Cypher Clause arg list to the Path
    target_entry = (TargetEntry *)llast(rte->subquery->targetList);
    func_expr = (FuncExpr *)target_entry->expr;
    c = linitial(func_expr->args);

    ccp->pattern = (List *)DatumGetPointer(c->constvalue);

    // Clear out existing Paths and add our Custom Path
    pfree(rel->pathlist);
    rel->pathlist = list_make1(ccp);
}
