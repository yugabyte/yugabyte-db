/*
 * pg_trunc2del
 *
 * This program is open source, licensed under the PostgreSQL license.
 * For license terms, see the LICENSE file.
 *
 */
#include "postgres.h"
#include "fmgr.h"

#include <access/heapam.h>
#include "nodes/nodes.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/plancat.h"
#include <storage/bufmgr.h>
#include "tcop/utility.h"
#include <utils/rel.h>


/* for testing
#include "optimizer/planner.h"
*/

PG_MODULE_MAGIC;


/*--- Functions --- */

void	_PG_init(void);
void	_PG_fini(void);

void hypo_utility_hook(Node *parsetree,
					   const char *queryString,
					   ProcessUtilityContext context,
					   ParamListInfo params,
					   DestReceiver *dest,
					   char *completionTag);

static ProcessUtility_hook_type prev_utility_hook = NULL;


static void hypo_gri_hook(PlannerInfo *root,
						  Oid relationObjectId,
						  bool inhparent,
						  RelOptInfo *rel);

static get_relation_info_hook_type	prev_gri_hook = NULL;

static void addHypotheticalIndexes(PlannerInfo *root,
						Oid relationObjectId,
						bool inhparent,
						RelOptInfo *rel);

static bool hypo_query_walker(Node *node);

static bool	fix_empty_table = false;
bool isExplain = false;

/* for testing
static PlannedStmt *hypo_plannerhook(Query *query, int cursorOptions, ParamListInfo boundParams);
static planner_hook_type prev_planner_hook = NULL;
*/

void
_PG_init(void)
{

	/* Install hooks */
	prev_utility_hook = ProcessUtility_hook;
	ProcessUtility_hook = hypo_utility_hook;

	prev_gri_hook = get_relation_info_hook;
	get_relation_info_hook = hypo_gri_hook;

	elog(NOTICE,"pg_hypo installed !");
	/* for testing
	prev_planner_hook = planner_hook;
	planner_hook = hypo_plannerhook;
	*/
}

void
_PG_fini(void)
{
	/* uninstall hooks */
	ProcessUtility_hook = prev_utility_hook;
	get_relation_info_hook = prev_gri_hook;

	/* for testing
	planner_hook = prev_planner_hook;
	*/
}

void
hypo_utility_hook(Node *parsetree,
				  const char *queryString,
				  ProcessUtilityContext context,
				  ParamListInfo params,
				  DestReceiver *dest,
				  char *completionTag)
{
	isExplain = query_or_expression_tree_walker(parsetree, hypo_query_walker, NULL, 0);

	if (isExplain)
	{
		elog(NOTICE, "Query is an explain (no analyze) :)");
		elog(NOTICE, "%s", queryString);
	}
	else
	{
		elog(NOTICE, "I don't mind this query");
		elog(NOTICE, "%s", queryString);
	}
	elog(NOTICE, "\n");

	if (prev_utility_hook)
		prev_utility_hook(parsetree, queryString,
								context, params,
								dest, completionTag);
	else
		standard_ProcessUtility(parsetree, queryString,
								context, params,
								dest, completionTag);

}

static bool
hypo_query_walker(Node *parsetree)
{
	elog(NOTICE, "Pour info, T_ExplainStmt, = %d", T_ExplainStmt);
	if (parsetree == NULL)
	{
		elog(NOTICE, "node IS NULL");
		return false;
	}
	elog(NOTICE, "node : %d", parsetree->type);
	switch (nodeTag(parsetree))
	{
		case T_ExplainStmt:
			{
				ListCell *lc;
				foreach(lc, ((ExplainStmt *) parsetree)->options)
				{
					DefElem    *opt = (DefElem *) lfirst(lc);
		
					if (strcmp(opt->defname, "analyze") == 0)
						return false;
				}
			}
			return true;
			break;
		default:
			return false;
	}
	return false;
}

static void
addHypotheticalIndexes(PlannerInfo *root, Oid relationObjectId, bool inhparent, RelOptInfo *rel) {
	ListCell   *l;

	foreach(l, rel->indexlist)
	{
		IndexOptInfo	*info = (IndexOptInfo*)lfirst(l);
		rel->indexlist = list_delete_ptr(rel->indexlist, info);
	}
}

static void hypo_gri_hook(PlannerInfo *root,
						  Oid relationObjectId,
						  bool inhparent,
						  RelOptInfo *rel)
{
	if (isExplain)
	{
		Relation 	relation;
		elog(NOTICE,"gri ET C'EST UN EXPLAIN!!");

		relation = heap_open(relationObjectId, NoLock);
		if (relation->rd_rel->relkind == RELKIND_RELATION)
		{
			if (fix_empty_table && RelationGetNumberOfBlocks(relation) == 0)
			{
				/*
				 * estimate_rel_size() could be too pessimistic for particular
				 * workload
				 */
				rel->pages = 0.0;
				rel->tuples = 0.0;
			}

			addHypotheticalIndexes(root, relationObjectId, inhparent, rel);
		}
		heap_close(relation, NoLock);
	}
	else
		elog(NOTICE,"gri, no explain");

	if (prev_gri_hook)
		prev_gri_hook(root, relationObjectId, inhparent, rel);
}

/* testing
static PlannedStmt *
hypo_plannerhook(Query *parse,
			 int cursorOptions,
			 ParamListInfo boundParams)
{
	PlannedStmt *plannedstmt = NULL;
	if (prev_planner_hook)
		plannedstmt = prev_planner_hook(parse, cursorOptions, boundParams);
	else
		plannedstmt = standard_planner(parse, cursorOptions, boundParams);

	elog(NOTICE,"hypo: %d", (Node *) parse->type);
	return plannedstmt;
}
*/
