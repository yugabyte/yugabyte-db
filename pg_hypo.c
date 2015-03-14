/*
 * pg_trunc2del
 *
 * This program is open source, licensed under the PostgreSQL license.
 * For license terms, see the LICENSE file.
 *
 */
#include "postgres.h"
#include "fmgr.h"

#include "catalog/heap.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_opfamily.h"
#include "catalog/pg_type.h"
#include "commands/explain.h"
#include "nodes/makefuncs.h"
#include "nodes/nodes.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/plancat.h"
#include <storage/bufmgr.h>
#include "tcop/utility.h"
#include <utils/rel.h>


PG_MODULE_MAGIC;


/*--- Functions --- */

void	_PG_init(void);
void	_PG_fini(void);

static void hypo_utility_hook(Node *parsetree,
					   const char *queryString,
					   ProcessUtilityContext context,
					   ParamListInfo params,
					   DestReceiver *dest,
					   char *completionTag);
static ProcessUtility_hook_type prev_utility_hook = NULL;


static void hypo_get_relation_info_hook(PlannerInfo *root,
						  Oid relationObjectId,
						  bool inhparent,
						  RelOptInfo *rel);
static get_relation_info_hook_type	prev_get_relation_info_hook = NULL;

static const char *hypo_explain_get_index_name_hook(Oid indexId);
static explain_get_index_name_hook_type prev_explain_get_index_name_hook = NULL;

static void addHypotheticalIndexes(PlannerInfo *root,
						Oid relationObjectId,
						bool inhparent,
						RelOptInfo *rel,
						Relation relation);
static bool hypo_query_walker(Node *node);

static List *
build_index_tlist(PlannerInfo *root, IndexOptInfo *index,
				  Relation heapRelation);

static bool	fix_empty_table = false;
#define HYPOTHETICAL_INDEX_OID	0;
bool isExplain = false;

void
_PG_init(void)
{

	/* Install hooks */
	prev_utility_hook = ProcessUtility_hook;
	ProcessUtility_hook = hypo_utility_hook;

	prev_get_relation_info_hook = get_relation_info_hook;
	get_relation_info_hook = hypo_get_relation_info_hook;

	prev_explain_get_index_name_hook = explain_get_index_name_hook;
	explain_get_index_name_hook = hypo_explain_get_index_name_hook;

	elog(NOTICE,"pg_hypo installed !");
}

void
_PG_fini(void)
{
	/* uninstall hooks */
	ProcessUtility_hook = prev_utility_hook;
	get_relation_info_hook = prev_get_relation_info_hook;
	explain_get_index_name_hook = prev_explain_get_index_name_hook;

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
addHypotheticalIndexes(PlannerInfo *root, Oid relationObjectId, bool inhparent, RelOptInfo *rel, Relation relation) {
	ListCell   *l;
	IndexOptInfo *index;
	int ncolumns, i;

	foreach(l, rel->indexlist)
	{
		IndexOptInfo	*info = (IndexOptInfo*)lfirst(l);
		rel->indexlist = list_delete_ptr(rel->indexlist, info);
	}

	index = makeNode(IndexOptInfo);
	index->indexoid = HYPOTHETICAL_INDEX_OID;
	index->reltablespace = rel->reltablespace;
	index->rel = rel;
	index->ncolumns = ncolumns = 1;
	index->indexkeys = (int *) palloc(sizeof(int) * ncolumns);
	index->indexcollations = (Oid *) palloc(sizeof(int) * ncolumns);
	index->opfamily = (Oid *) palloc(sizeof(int) * ncolumns);
	index->opcintype = (Oid *) palloc(sizeof(int) * ncolumns);
	for (i = 0; i < ncolumns; i++)
	{
		index->indexkeys[i] = 1; // ?
		index->indexcollations[i] = 0; //?? C_COLLATION_OID;
		index->opfamily[i] = INTEGER_BTREE_FAM_OID;
		index->opcintype[i] = INT4OID; // ??INT4_BTREE_OPS_OID; // btree integer opclass
	}
	index->relam = BTREE_AM_OID;
	index->amcostestimate = (RegProcedure) 1268; // btcostestimate
	index->canreturn = true;
	index->amcanorderbyop = false;
	index->amoptionalkey = true;
	index->amsearcharray = true;
	index->amsearchnulls = true;
	index->amhasgettuple = true;
	index->amhasgetbitmap = true;
	index->unique = false;
	index->immediate = true;

	if (index->relam == BTREE_AM_OID)
	{
		index->sortopfamily = index->opfamily;
		index->reverse_sort = (bool *) palloc(sizeof(bool) * ncolumns);
		index->nulls_first = (bool *) palloc(sizeof(bool) * ncolumns);

		index->tree_height = 1;
		for (i = 0; i < ncolumns; i++)
		{
			index->reverse_sort[i] = false;
			index->nulls_first[i] = false;
		}

	}

	index->indexprs = NIL;
	index->indpred = NIL;
	index->indextlist = build_index_tlist(root, index, relation);

	if (index->indpred == NIL)
	{
		index->pages = rel->tuples / 1024;
		index->tuples = rel->tuples;
	}

	index->hypothetical = true;

	rel->indexlist = lcons(index, rel->indexlist);
	elog(WARNING, "Hypo index %d added.", index->indexoid);
}

static void hypo_get_relation_info_hook(PlannerInfo *root,
						  Oid relationObjectId,
						  bool inhparent,
						  RelOptInfo *rel)
{
	if (isExplain)
	{
		Relation 	relation;

		relation = heap_open(relationObjectId, NoLock);

		elog(NOTICE,"gri ET C'EST UN EXPLAIN!!");

		if (fix_empty_table && RelationGetNumberOfBlocks(relation) == 0)
		{
			/*
			 * estimate_rel_size() could be too pessimistic for particular
			 * workload
			 */
			rel->pages = 0.0;
			rel->tuples = 0.0;
		}


		if (relation->rd_rel->relkind == RELKIND_RELATION)
		{
			addHypotheticalIndexes(root, relationObjectId, inhparent, rel, relation);
		}

		heap_close(relation, NoLock);
	}
	else
		elog(NOTICE,"gri, no explain");

	if (prev_get_relation_info_hook)
		prev_get_relation_info_hook(root, relationObjectId, inhparent, rel);
}

static const char *
hypo_explain_get_index_name_hook(Oid indexId)
{
	if (isExplain && indexId == 0)
	{
		/* we're in an explain-only command and dealing with one of our
		 * hypothetical indexes.
		 * return a meaningful name for this index
		 */
		return "hypothetical_index_1";
	}
	return NULL; // otherwise return NULL, explain_get_index_name will handle it
}

// stolen from backend/optimisze/util/plancat.c
static List *
build_index_tlist(PlannerInfo *root, IndexOptInfo *index,
				  Relation heapRelation)
{
	List	   *tlist = NIL;
	Index		varno = index->rel->relid;
	ListCell   *indexpr_item;
	int			i;

	indexpr_item = list_head(index->indexprs);
	for (i = 0; i < index->ncolumns; i++)
	{
		int			indexkey = index->indexkeys[i];
		Expr	   *indexvar;

		if (indexkey != 0)
		{
			/* simple column */
			Form_pg_attribute att_tup;

			if (indexkey < 0)
				att_tup = SystemAttributeDefinition(indexkey,
										   heapRelation->rd_rel->relhasoids);
			else
				att_tup = heapRelation->rd_att->attrs[indexkey - 1];

			indexvar = (Expr *) makeVar(varno,
										indexkey,
										att_tup->atttypid,
										att_tup->atttypmod,
										att_tup->attcollation,
										0);
		}
		else
		{
			/* expression column */
			if (indexpr_item == NULL)
				elog(ERROR, "wrong number of index expressions");
			indexvar = (Expr *) lfirst(indexpr_item);
			indexpr_item = lnext(indexpr_item);
		}

		tlist = lappend(tlist,
						makeTargetEntry(indexvar,
										i + 1,
										NULL,
										false));
	}
	if (indexpr_item != NULL)
		elog(ERROR, "wrong number of index expressions");

	return tlist;
}

