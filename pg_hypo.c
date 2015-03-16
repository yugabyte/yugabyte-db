/*
 * pg_trunc2del
 *
 * This program is open source, licensed under the PostgreSQL license.
 * For license terms, see the LICENSE file.
 *
 */
#include <unistd.h>

#include "postgres.h"
#include "fmgr.h"

#include "catalog/heap.h"
#include "commands/explain.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/plancat.h"
#include "storage/bufmgr.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"

PG_MODULE_MAGIC;

#define HYPO_MAX_COLS	1 /* # of column an hypothetical index can have */
#define HYPO_NB_COLS		4 /* # of column pg_hypo() returns */
#define HYPO_NB_INDEXES		50 /* # of hypothetical index a single session can hold */

bool isExplain = false;
static bool	fix_empty_table = false;

/*
 * Hypothetical index storage
 */
typedef struct hypoEntry
{
	Oid			relid;		/* related relation Oid */
	char		indexname[NAMEDATALEN];	/* hypothetical index name */
	Oid			relam;
	int			ncolumns; /* number of columns, only 1 for now */
	int			indexkeys; /* attnum */
	Oid			indexcollations;
	Oid			opfamily;
	Oid			opcintype;
} hypoEntry;

hypoEntry	entries[HYPO_NB_INDEXES];


/*--- Functions --- */

void	_PG_init(void);
void	_PG_fini(void);

Datum	pg_hypo_reset(PG_FUNCTION_ARGS);
Datum	pg_hypo_add_index_internal(PG_FUNCTION_ARGS);
Datum	pg_hypo(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pg_hypo_reset);
PG_FUNCTION_INFO_V1(pg_hypo_add_index_internal);
PG_FUNCTION_INFO_V1(pg_hypo);

static void entry_reset(void);
static bool entry_store(Oid relid,
			char *indexname,
			Oid relam,
			int ncolumns,
			int indexkeys,
			int indexcollations,
			Oid opfamily,
			Oid opcintype);

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

static void addHypotheticalIndex(PlannerInfo *root,
						Oid relationObjectId,
						bool inhparent,
						RelOptInfo *rel,
						Relation relation,
						int position);
static bool hypo_query_walker(Node *node);

static List *
build_index_tlist(PlannerInfo *root, IndexOptInfo *index,
				  Relation heapRelation);

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
}

void
_PG_fini(void)
{
	/* uninstall hooks */
	ProcessUtility_hook = prev_utility_hook;
	get_relation_info_hook = prev_get_relation_info_hook;
	explain_get_index_name_hook = prev_explain_get_index_name_hook;

}

static void
entry_reset(void)
{
	int i;
	/* Mark all entries relid with InvalidOid */
	for (i = 0; i < HYPO_NB_INDEXES ; i++)
	{
		entries[i].relid = InvalidOid;
	}

	return;
}

static bool
entry_store(Oid relid,
			char *indexname,
			Oid relam,
			int ncolumns,
			int indexkeys,
			int indexcollations,
			Oid opfamily,
			Oid opcintype)
{
	int i = 0;

	/* Make sure user didn't try to add too many columns */
	if (ncolumns > HYPO_MAX_COLS)
		return false;

	while (i < HYPO_NB_INDEXES)
	{
		if ( /* don't store twice the same index */
			(entries[i].relid == relid) &&
			(entries[i].indexkeys == indexkeys) &&
			(entries[i].relam == relam)
		)
		{
			/* if index already exists, then raise a warning */
			elog(WARNING, "pg_hypo: Index already existing \"%s\"", indexname);
			return false;
		}

		if (entries[i].relid == InvalidOid)
		{
			entries[i].relid = relid;
			strncpy(entries[i].indexname, indexname, NAMEDATALEN);
			entries[i].relam = relam;
			entries[i].ncolumns = ncolumns;
			entries[i].indexkeys = indexkeys;
			entries[i].indexcollations = indexcollations;
			entries[i].opfamily = opfamily;
			entries[i].opcintype = opcintype;

			return true;
		}
		i++;
	}

	/* if there's no more room, then raise a warning */
	elog(WARNING, "pg_hypo: no more free entry for storing index \"%s\"", indexname);
	return false;
}

/* This function setup the "isExplain" flag for next hooks.
 * If this flag is setup, we can add hypothetical indexes.
 */
void
hypo_utility_hook(Node *parsetree,
				  const char *queryString,
				  ProcessUtilityContext context,
				  ParamListInfo params,
				  DestReceiver *dest,
				  char *completionTag)
{
	isExplain = query_or_expression_tree_walker(parsetree, hypo_query_walker, NULL, 0);

	if (prev_utility_hook)
		prev_utility_hook(parsetree, queryString,
								context, params,
								dest, completionTag);
	else
		standard_ProcessUtility(parsetree, queryString,
								context, params,
								dest, completionTag);

}

/* Detect if the current utility command is compatible with hypothetical indexes
 * i.e. an EXPLAIN, no ANALYZE
 */
static bool
hypo_query_walker(Node *parsetree)
{
	if (parsetree == NULL)
		return false;

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

/* Real work here.
 * Build hypothetical indexes for the specified relation.
 */
static void
addHypotheticalIndex(PlannerInfo *root,
					 Oid relationObjectId,
					 bool inhparent,
					 RelOptInfo *rel,
					 Relation relation,
					 int position)
{
	IndexOptInfo *index;
	int ncolumns, i;
	int ind_avg_width = 0;


	/* create a node */
	index = makeNode(IndexOptInfo);

	index->relam = entries[position].relam;

	if (index->relam != BTREE_AM_OID)
	{
		elog(WARNING, "pg_hypo: Only btree indexes are supported for now!");
		return;
	}

	// General stuff
	index->indexoid = position+1;
	index->reltablespace = rel->reltablespace; // same tablespace as relation
	index->rel = rel;
	index->ncolumns = ncolumns = entries[position].ncolumns;

	index->indexkeys = (int *) palloc(sizeof(int) * ncolumns);
	index->indexcollations = (Oid *) palloc(sizeof(int) * ncolumns);
	index->opfamily = (Oid *) palloc(sizeof(int) * ncolumns);
	index->opcintype = (Oid *) palloc(sizeof(int) * ncolumns);

	for (i = 0; i < ncolumns; i++)
	{
		index->indexkeys[i] = entries[position].indexkeys;
		ind_avg_width += get_attavgwidth(relation->rd_id, index->indexkeys[i]);
		switch (index->relam)
		{
			case BTREE_AM_OID:
				// hardcode int4 cols, WIP
				index->indexcollations[i] = 0; //?? C_COLLATION_OID;
				index->opfamily[i] = entries[position].opfamily; //INTEGER_BTREE_FAM_OID;
				index->opcintype[i] = entries[position].opcintype; //INT4OID; // ??INT4_BTREE_OPS_OID; // btree integer opclass
				break;
		}
	}

	index->unique = false; /* no hypothetical unique index */

	if (index->relam == BTREE_AM_OID)
	{
		index->canreturn = true;
		index->amcanorderbyop = false;
		index->amoptionalkey = true;
		index->amsearcharray = true;
		index->amsearchnulls = true;
		index->amhasgettuple = true;
		index->amhasgetbitmap = true;
		index->immediate = true;
		index->amcostestimate = (RegProcedure) 1268; // btcostestimate
		index->sortopfamily = index->opfamily;
		index->tree_height = 1; // WIP

		index->reverse_sort = (bool *) palloc(sizeof(bool) * ncolumns);
		index->nulls_first = (bool *) palloc(sizeof(bool) * ncolumns);

		for (i = 0; i < ncolumns; i++)
		{
			index->reverse_sort[i] = false; // not handled for now, WIP
			index->nulls_first[i] = false; // not handled for now, WIP
		}
	}

	index->indexprs = NIL; // not handled for now, WIP
	index->indpred = NIL; // no partial index handled for now, WIP

	/* Build targetlist using the completed indexprs data, stolen from PostgreSQL */
	index->indextlist = build_index_tlist(root, index, relation);

	if (index->indpred == NIL)
	{
		/* very quick and pessimistic estimation :
		 * number of tuples * avg width,
		 * with ~ 50% bloat (including 10% fillfactor), plus 1 block
		 */
		index->pages = (rel->tuples * ind_avg_width * 2 / BLCKSZ) + 1;
		/* partial index not supported yet, so assume all tuples are in the index */
		index->tuples = rel->tuples;
	}

	/* obviously, setup this tag.
	 * However, it's only checked in selfuncs.c/get_actual_variable_range, so
	 * we still need to add hypothetical indexes *ONLY* in an
	 * explain-no-analyze command.
	 */
	index->hypothetical = true;

	/* add our hypothetical index in the relation's indexlist */
	rel->indexlist = lcons(index, rel->indexlist);
}

/* This function will execute the "addHypotheticalIndex" for every hypothetical
 * index found for each relation if the isExplain flag is setup.
 */
static void hypo_get_relation_info_hook(PlannerInfo *root,
						  Oid relationObjectId,
						  bool inhparent,
						  RelOptInfo *rel)
{
	if (isExplain)
	{
		Relation 	relation;

		relation = heap_open(relationObjectId, NoLock);

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
			int i;

			for (i = 0; i < HYPO_NB_INDEXES; i++)
			{
				if (entries[i].relid == InvalidOid)
					break;
				if (entries[i].relid == relationObjectId) {
					// hypothetical index found, add it to the relation's indextlist
					addHypotheticalIndex(root, relationObjectId, inhparent, rel, relation, i);
				}
			}
		}

		heap_close(relation, NoLock);
	}

	if (prev_get_relation_info_hook)
		prev_get_relation_info_hook(root, relationObjectId, inhparent, rel);
}

/* Return the hypothetical index name is indexId is ours, NULL otherwise, as
 * this is what explain_get_index_name expects to continue his job.
 */
static const char *
hypo_explain_get_index_name_hook(Oid indexId)
{
	char *ret = NULL;

	/* our index key is position-in-array +1, return NULL if if can't be ours */
	if (indexId > HYPO_NB_INDEXES)
		return ret;

	if (isExplain)
	{
		/* we're in an explain-only command. Return the name of the
		   * hypothetical index name if it's one of ours, otherwise return NULL
		 */
		if (entries[indexId-1].relid != InvalidOid)
		{
			ret = entries[indexId-1].indexname;
		}
	}
	return ret;
}

/*
 * Reset statistics.
 */
Datum
pg_hypo_reset(PG_FUNCTION_ARGS)
{
	entry_reset();
	PG_RETURN_VOID();
}

/*
 * Add an hypothetical index in the array, with all needed informations
 * it supposed to be called from the provided sql function, because I'm too
 * lazy to retrieve all the needed info in C !=
 */
Datum
pg_hypo_add_index_internal(PG_FUNCTION_ARGS)
{
	Oid		relid = PG_GETARG_OID(0);
	char	*indexname = TextDatumGetCString(PG_GETARG_TEXT_PP(1));
	Oid		relam = PG_GETARG_OID(2);
	int		ncolumns = PG_GETARG_INT32(3);
	int		indexkeys = PG_GETARG_INT32(4);
	Oid		indexcollations = PG_GETARG_OID(5);
	Oid		opfamily = PG_GETARG_OID(6);
	Oid		opcintype = PG_GETARG_OID(7);

	return entry_store(relid, indexname, relam, ncolumns, indexkeys, indexcollations, opfamily, opcintype);
}

/*
 * List created hypothetical indexes
 */
Datum
pg_hypo(PG_FUNCTION_ARGS)
{
	ReturnSetInfo	*rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	MemoryContext	per_query_ctx;
	MemoryContext	oldcontext;
	TupleDesc		tupdesc;
	Tuplestorestate	*tupstore;
	int				i = 0;


	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not " \
							"allowed in this context")));

	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	while (i < HYPO_NB_INDEXES)
	{
		Datum		values[HYPO_NB_COLS];
		bool		nulls[HYPO_NB_COLS];
		int			j = 0;

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		if (entries[i].relid == InvalidOid)
			break;

		values[j++] = CStringGetTextDatum(strdup(entries[i].indexname));
		values[j++] = ObjectIdGetDatum(entries[i].relid);
		values[j++] = Int32GetDatum(entries[i].indexkeys);
		values[j++] = ObjectIdGetDatum(entries[i].relam);

		Assert(j == PG_STAT_PLAN_COLS);

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
		i++;
	}

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	return (Datum) 0;
}

// stolen from backend/optimisze/util/plancat.c, no export of this function :(
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
