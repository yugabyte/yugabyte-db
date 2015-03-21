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

#include "access/htup_details.h"
#include "catalog/catalog.h"
#include "catalog/heap.h"
#include "catalog/namespace.h"
#include "catalog/pg_class.h"
#include "catalog/pg_opclass.h"
#include "commands/defrem.h"
#include "commands/explain.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/plancat.h"
#include "parser/parser.h"
#include "parser/parse_coerce.h"
#include "storage/bufmgr.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/syscache.h"

PG_MODULE_MAGIC;

#define HYPO_MAX_COLS	10 /* # of column an hypothetical index can have */
#define HYPO_NB_COLS		4 /* # of column hypopg() returns */
#define HYPO_MAX_INDEXNAME	1024 /* max length of an hypothetical index */

bool isExplain = false;
static bool	fix_empty_table = false;

/*
 * Hypothetical index storage, pretty much an IndexOptInfo
 */
typedef struct hypoEntry
{
	Oid				oid; /* hypothetical index unique identifier */
	Oid				relid;		/* related relation Oid */
	Oid				reltablespace; /* tablespace of the index, if set */
	char			indexname[HYPO_MAX_INDEXNAME];	/* hypothetical index name */

	/* index descriptor informations */
	int				ncolumns; /* number of columns, only 1 for now */
	int				indexkeys[HYPO_MAX_COLS]; /* attnums */
	Oid				indexcollations[HYPO_MAX_COLS]; /* OIDs of collations of index columns */
	Oid				opfamily[HYPO_MAX_COLS]; /* OIDs of operator families for columns */
	Oid				opcintype[HYPO_MAX_COLS]; /* OIDs of opclass declared input data types */
	Oid				sortopfamily[HYPO_MAX_COLS]; /* OIDs of btree opfamilies, if orderable */
	bool			reverse_sort[HYPO_MAX_COLS]; /* is sort order descending? */
	bool			nulls_first[HYPO_MAX_COLS]; /* do NULLs come first in the sort order? */
	Oid				relam;  /* OID of the access method (in pg_am) */

	RegProcedure	amcostestimate; /* OID of the access method's cost fcn */

	bool			predOK;			/* true if predicate matches query */
	bool			unique;			/* true if a unique index */
	bool			immediate;		/* is uniqueness enforced immediately? */
	bool			canreturn;		/* can index return IndexTuples? */
	bool			amcanorderbyop; /* does AM support order by operator result? */
	bool			amoptionalkey;	/* can query omit key for the first column? */
	bool			amsearcharray;	/* can AM handle ScalarArrayOpExpr quals? */
	bool			amsearchnulls;	/* can AM search for NULL/NOT NULL entries? */
	bool			amhasgettuple;	/* does AM have amgettuple interface? */
	bool			amhasgetbitmap; /* does AM have amgetbitmap interface? */

} hypoEntry;

List *entries = NIL;


/*--- Functions --- */

void	_PG_init(void);
void	_PG_fini(void);

Datum	hypopg_reset(PG_FUNCTION_ARGS);
Datum	hypopg_add_index_internal(PG_FUNCTION_ARGS);
Datum	hypopg(PG_FUNCTION_ARGS);
Datum	hypopg_create_index(PG_FUNCTION_ARGS);
Datum	hypopg_drop_index(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(hypopg_reset);
PG_FUNCTION_INFO_V1(hypopg_add_index_internal);
PG_FUNCTION_INFO_V1(hypopg);
PG_FUNCTION_INFO_V1(hypopg_create_index);
PG_FUNCTION_INFO_V1(hypopg_drop_index);

static hypoEntry * newHypoEntry(Oid relid, Oid relam);
static Oid hypoGetNewOid(Oid relid);
static void addHypoEntry(hypoEntry *entry);

static void entry_reset(void);
static bool entry_store(Oid relid,
			char *indexname,
			Oid relam,
			int ncolumns,
			int indexkeys,
			int indexcollations,
			Oid opfamily,
			Oid opcintype);
static bool entry_store_parsetree(IndexStmt *node);
static bool entry_remove(Oid indexid);

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

static void injectHypotheticalIndex(PlannerInfo *root,
						Oid relationObjectId,
						bool inhparent,
						RelOptInfo *rel,
						Relation relation,
						hypoEntry *entry);
static bool hypo_query_walker(Node *node);

static List *
build_index_tlist(PlannerInfo *root, IndexOptInfo *index,
				  Relation heapRelation);
static Oid
GetIndexOpClass(List *opclass, Oid attrType,
						char *accessMethodName, Oid accessMethodId);

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

/* palloc a new hypoEntry, and give it a new OID, and some other global stuff */
static hypoEntry *
newHypoEntry(Oid relid, Oid relam)
{
	hypoEntry *entry;
	MemoryContext oldcontext;

	oldcontext = MemoryContextSwitchTo(TopMemoryContext);

	entry = palloc0(sizeof(hypoEntry));

	MemoryContextSwitchTo(oldcontext);

	entry->oid = hypoGetNewOid(relid);
	entry->relid = relid;
	entry->relam = relam;

	switch (entry->relam)
	{
		case BTREE_AM_OID:
			entry->amcostestimate = (RegProcedure) 1268; // btcostestimate
			entry->immediate = true;
			entry->canreturn = true;
			entry->amcanorderbyop = false;
			entry->amoptionalkey = true;
			entry->amsearcharray = true;
			entry->amsearchnulls = true;
			entry->amhasgettuple = true;
			entry->amhasgetbitmap = true;
			break;
		default:
			elog(WARNING, "pg_hypo: access method %d is not supported",
					entry->relam);
			break;
	}

	return entry;
}

/* Wrapper around GetNewRelFileNode */
static Oid
hypoGetNewOid(Oid relid)
{
	Relation	pg_class;
	Relation	relation;
	Oid			newoid;
	Oid			reltablespace;
	char		relpersistence;

	relation = heap_open(relid, AccessShareLock);

	reltablespace = relation->rd_rel->reltablespace;
	relpersistence = relation->rd_rel->relpersistence;

	heap_close(relation, AccessShareLock);

	pg_class = heap_open(RelationRelationId, RowExclusiveLock);

	newoid = GetNewRelFileNode(reltablespace, pg_class, relpersistence);

	heap_close(pg_class, RowExclusiveLock);

	return newoid;
}

static void
addHypoEntry(hypoEntry *entry)
{
	MemoryContext oldcontext;

	oldcontext = MemoryContextSwitchTo(TopMemoryContext);

	entries = lappend(entries, entry);

	MemoryContextSwitchTo(oldcontext);
}

static void
entry_reset(void)
{
	list_free_deep(entries);
	entries = NIL;
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
	hypoEntry *entry;

	/* Make sure user didn't try to add too many columns */
	if (ncolumns > HYPO_MAX_COLS)
		return false;

	entry = newHypoEntry(relid, relam);

	strcpy(entry->indexname, indexname);
	entry->unique = false;
	entry->ncolumns = ncolumns;
	entry->indexkeys[0] = indexkeys;
	entry->indexcollations[0] = indexcollations;
	entry->opfamily[0] = opfamily;
	entry->opcintype[0] = opcintype;
	entry->reverse_sort[0] = false;
	entry->nulls_first[0] = false;

	addHypoEntry(entry);

	return true;
}

static bool
entry_store_parsetree(IndexStmt *node)
{
	hypoEntry			*entry;
	HeapTuple			tuple;
	Form_pg_attribute	attform;
	Oid					relid;
	char				*indexRelationName;
	Oid					accessMethodId;
	int			ncolumns;
	ListCell	*lc;
	int			j = 0;


	ncolumns = list_length(node->indexParams);
	if (ncolumns > HYPO_MAX_COLS)
		return false;

	indexRelationName = palloc0(sizeof(char) * HYPO_MAX_INDEXNAME);
	indexRelationName = strcat(indexRelationName,"idx_hypo_");
	indexRelationName = strcat(indexRelationName, node->accessMethod);
	indexRelationName = strcat(indexRelationName, "_");

	if (node->relation->schemaname != NULL && (strcmp(node->relation->schemaname, "public") != 0))
	{
		indexRelationName = strcat(indexRelationName, node->relation->schemaname);
		indexRelationName = strcat(indexRelationName, "_");
	}

	indexRelationName = strcat(indexRelationName, node->relation->relname);

	relid =
		RangeVarGetRelid(node->relation, AccessShareLock, false);

	tuple = SearchSysCache1(AMNAME, PointerGetDatum(node->accessMethod));
	if (!HeapTupleIsValid(tuple))
	{
		if (!HeapTupleIsValid(tuple))
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("hypopg: access method \"%s\" does not exist",
						 node->accessMethod)));
	}
	accessMethodId = HeapTupleGetOid(tuple);

	ReleaseSysCache(tuple);

	/* now create the hypothetical index entry */
	entry = newHypoEntry(relid, accessMethodId);

	entry->unique = node->unique;

	entry->ncolumns = ncolumns = list_length(node->indexParams);

	/* iterate through columns */
	foreach(lc, node->indexParams)
	{
		IndexElem *indexelem = (IndexElem *) lfirst(lc);
		Oid		atttype;
		Oid		opclass;

		indexRelationName = strcat(indexRelationName, "_");
		indexRelationName = strcat(indexRelationName, indexelem->name);
		/* get the attribute catalog info */
		tuple = SearchSysCacheAttName(relid, indexelem->name);
		if (!HeapTupleIsValid(tuple))
		{
			elog(ERROR, "hypopg: column \"%s\" does not exist",
					indexelem->name);
		}
		attform = (Form_pg_attribute) GETSTRUCT(tuple);

		/* setup the attnum */
		entry->indexkeys[j] = attform->attnum;

		/* get the atttype */
		atttype = attform->atttypid;
		/* get the opclass */
		opclass = GetIndexOpClass(indexelem->opclass,
				atttype,
				node->accessMethod,
				accessMethodId);
		/* setup the opfamily */
		entry->opfamily[j] = get_opclass_family(opclass);

		ReleaseSysCache(tuple);

		entry->opcintype[j] = atttype; //TODO
		entry->indexcollations[j] = 0; /* TODO */
		entry->reverse_sort[j] = (indexelem->ordering == SORTBY_DESC ? true : false);
		entry->nulls_first[j] = (indexelem->nulls_ordering == SORTBY_NULLS_FIRST ? true : false);

		j++;
	}
	strcpy(entry->indexname, indexRelationName);

	addHypoEntry(entry);

	return true;
}

/* Remove an hypothetical index from the list */
static bool
entry_remove(Oid indexid)
{
	ListCell *lc;

	foreach(lc, entries)
	{
		hypoEntry *entry = (hypoEntry *) lfirst(lc);

		if (entry->oid == indexid)
		{
			entries = list_delete_ptr(entries, entry);
			return true;
		}
	}
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
injectHypotheticalIndex(PlannerInfo *root,
					 Oid relationObjectId,
					 bool inhparent,
					 RelOptInfo *rel,
					 Relation relation,
					 hypoEntry *entry)
{
	IndexOptInfo *index;
	int ncolumns, i;
	int ind_avg_width = 0;


	/* create a node */
	index = makeNode(IndexOptInfo);

	index->relam = entry->relam;

	if (index->relam != BTREE_AM_OID)
	{
		elog(WARNING, "hypopg: Only btree indexes are supported for now!");
		return;
	}

	// General stuff
	index->indexoid = entry->oid;
	index->reltablespace = rel->reltablespace; // same tablespace as relation, TODO
	index->rel = rel;
	index->ncolumns = ncolumns = entry->ncolumns;

	index->indexkeys = (int *) palloc(sizeof(int) * ncolumns);
	index->indexcollations = (Oid *) palloc(sizeof(int) * ncolumns);
	index->opfamily = (Oid *) palloc(sizeof(int) * ncolumns);
	index->opcintype = (Oid *) palloc(sizeof(int) * ncolumns);

	for (i = 0; i < ncolumns; i++)
	{
		index->indexkeys[i] = entry->indexkeys[i];
		ind_avg_width += get_attavgwidth(relation->rd_id, index->indexkeys[i]);
		switch (index->relam)
		{
			case BTREE_AM_OID:
				index->indexcollations[i] = entry->indexcollations[i];
				index->opfamily[i] = entry->opfamily[i];
				index->opcintype[i] = entry->opcintype[i];
				break;
		}
	}

	index->unique = entry->unique;

	index->amcostestimate = entry->amcostestimate;
	index->immediate = entry->immediate;
	index->canreturn = entry->canreturn;
	index->amcanorderbyop = entry->canreturn;
	index->amoptionalkey = entry->amoptionalkey;
	index->amsearcharray = entry->amsearcharray;
	index->amsearchnulls = entry->amsearchnulls;
	index->amhasgettuple = entry->amhasgettuple;
	index->amhasgetbitmap = entry->amhasgetbitmap;

	if (index->relam == BTREE_AM_OID)
	{
		index->tree_height = 1; // TODO
		index->sortopfamily = index->opfamily;
	}

	index->reverse_sort = (bool *) palloc(sizeof(bool) * ncolumns);
	index->nulls_first = (bool *) palloc(sizeof(bool) * ncolumns);

	for (i = 0; i < ncolumns; i++)
	{
		index->reverse_sort[i] = entry->reverse_sort[i];
		index->nulls_first[i] = entry->nulls_first[i];
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

/* This function will execute the "injectHypotheticalIndex" for every hypothetical
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
			ListCell *lc;

			foreach(lc, entries)
			{
				hypoEntry *entry = (hypoEntry *) lfirst(lc);

				if (entry->relid == relationObjectId) {
					// hypothetical index found, add it to the relation's indextlist
					injectHypotheticalIndex(root, relationObjectId, inhparent, rel, relation, entry);
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

	if (isExplain)
	{
		/* we're in an explain-only command. Return the name of the
		   * hypothetical index name if it's one of ours, otherwise return NULL
		 */
		ListCell *lc;
		foreach(lc, entries)
		{
			hypoEntry *entry = (hypoEntry *) lfirst(lc);
			if (entry->oid == indexId)
			{
				ret = entry->indexname;
			}
		}
	}
	return ret;
}

/*
 * Reset statistics.
 */
Datum
hypopg_reset(PG_FUNCTION_ARGS)
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
hypopg_add_index_internal(PG_FUNCTION_ARGS)
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
hypopg(PG_FUNCTION_ARGS)
{
	ReturnSetInfo	*rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	MemoryContext	per_query_ctx;
	MemoryContext	oldcontext;
	TupleDesc		tupdesc;
	Tuplestorestate	*tupstore;
	ListCell		*lc;


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

	foreach(lc, entries)
	{
		hypoEntry *entry = (hypoEntry *) lfirst(lc);
		Datum		values[HYPO_NB_COLS];
		bool		nulls[HYPO_NB_COLS];
		int			j = 0;

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		values[j++] = ObjectIdGetDatum(entry->oid);
		values[j++] = CStringGetTextDatum(strdup(entry->indexname));
		values[j++] = ObjectIdGetDatum(entry->relid);
		values[j++] = ObjectIdGetDatum(entry->relam);

		Assert(j == PG_STAT_PLAN_COLS);

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	return (Datum) 0;
}

/*
 * SQL wrapper to create an hypothetical index with his parsetree
 */
Datum
hypopg_create_index(PG_FUNCTION_ARGS)
{
	char		*sql = TextDatumGetCString(PG_GETARG_TEXT_PP(0));
	List		*parsetree_list;
	ListCell	*parsetree_item;
	char		*res;
	int			i = 1;

	parsetree_list = pg_parse_query(sql);

	foreach(parsetree_item, parsetree_list)
	{
		Node	   *parsetree = (Node *) lfirst(parsetree_item);
		if (nodeTag(parsetree) != T_IndexStmt)
		{
			elog(WARNING,
					"hypopg: SQL order #%d is not a CREATE INDEX statement",
					i);
		}
		else
		{
			entry_store_parsetree((IndexStmt *) parsetree);
		}
		i++;
	}
	res = nodeToString(parsetree_list);
	PG_RETURN_TEXT_P(cstring_to_text(res));
}

/*
 * SQL wrapper to drop an hypothetical index.
 */
Datum
hypopg_drop_index(PG_FUNCTION_ARGS)
{
	Oid indexid = PG_GETARG_OID(0);

	PG_RETURN_BOOL(entry_remove(indexid));
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

/*
 * Stolen from src/backend/commands/indexcmds.c, not exported :/
 * Resolve possibly-defaulted operator class specification
 */
static Oid
GetIndexOpClass(List *opclass, Oid attrType,
						char *accessMethodName, Oid accessMethodId)
{
	char	   *schemaname;
	char	   *opcname;
	HeapTuple	tuple;
	Oid			opClassId,
				opInputType;

	/*
	 * Release 7.0 removed network_ops, timespan_ops, and datetime_ops, so we
	 * ignore those opclass names so the default *_ops is used.  This can be
	 * removed in some later release.  bjm 2000/02/07
	 *
	 * Release 7.1 removes lztext_ops, so suppress that too for a while.  tgl
	 * 2000/07/30
	 *
	 * Release 7.2 renames timestamp_ops to timestamptz_ops, so suppress that
	 * too for awhile.  I'm starting to think we need a better approach. tgl
	 * 2000/10/01
	 *
	 * Release 8.0 removes bigbox_ops (which was dead code for a long while
	 * anyway).  tgl 2003/11/11
	 */
	if (list_length(opclass) == 1)
	{
		char	   *claname = strVal(linitial(opclass));

		if (strcmp(claname, "network_ops") == 0 ||
				strcmp(claname, "timespan_ops") == 0 ||
				strcmp(claname, "datetime_ops") == 0 ||
				strcmp(claname, "lztext_ops") == 0 ||
				strcmp(claname, "timestamp_ops") == 0 ||
				strcmp(claname, "bigbox_ops") == 0)
			opclass = NIL;
	}

	if (opclass == NIL)
	{
		/* no operator class specified, so find the default */
		opClassId = GetDefaultOpClass(attrType, accessMethodId);
		if (!OidIsValid(opClassId))
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("data type %s has no default operator class for access method \"%s\"",
						 format_type_be(attrType), accessMethodName),
					 errhint("You must specify an operator class for the index or define a default operator class for the data type.")));
		return opClassId;
	}

	/*
	 * Specific opclass name given, so look up the opclass.
	 */

	/* deconstruct the name list */
	DeconstructQualifiedName(opclass, &schemaname, &opcname);

	if (schemaname)
	{
		/* Look in specific schema only */
		Oid			namespaceId;

		namespaceId = LookupExplicitNamespace(schemaname, false);
		tuple = SearchSysCache3(CLAAMNAMENSP,
				ObjectIdGetDatum(accessMethodId),
				PointerGetDatum(opcname),
				ObjectIdGetDatum(namespaceId));
	}
	else
	{
		/* Unqualified opclass name, so search the search path */
		opClassId = OpclassnameGetOpcid(accessMethodId, opcname);
		if (!OidIsValid(opClassId))
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("operator class \"%s\" does not exist for access method \"%s\"",
						 opcname, accessMethodName)));
		tuple = SearchSysCache1(CLAOID, ObjectIdGetDatum(opClassId));
	}

	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("operator class \"%s\" does not exist for access method \"%s\"",
					 NameListToString(opclass), accessMethodName)));

	/*
	 * Verify that the index operator class accepts this datatype.  Note we
	 * will accept binary compatibility.
	 */
	opClassId = HeapTupleGetOid(tuple);
	opInputType = ((Form_pg_opclass) GETSTRUCT(tuple))->opcintype;

	if (!IsBinaryCoercible(attrType, opInputType))
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("operator class \"%s\" does not accept data type %s",
					 NameListToString(opclass), format_type_be(attrType))));

	ReleaseSysCache(tuple);

	return opClassId;
}
