/*
 * HypoPG
 *
 * This program is open source, licensed under the PostgreSQL license.
 * For license terms, see the LICENSE file.
 *
 */
#include <unistd.h>

#include "postgres.h"
#include "fmgr.h"

#if PG_VERSION_NUM >= 90300
#include "access/htup_details.h"
#endif
#include "access/nbtree.h"
#include "access/sysattr.h"
#include "access/xlog.h"
#include "catalog/catalog.h"
#include "catalog/heap.h"
#include "catalog/namespace.h"
#include "catalog/pg_class.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_type.h"
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

#define HYPO_NB_COLS		12	/* # of column hypopg() returns */
#define HYPO_CREATE_COLS	2	/* # of column hypopg_create_index() returns */

bool		isExplain = false;

/*
 * Hypothetical index storage, pretty much an IndexOptInfo
 * Some dynamic informations such as pages and lines are not storedn but
 * computed when the hypothetical index is added.
 */
typedef struct hypoEntry
{
	Oid			oid;			/* hypothetical index unique identifier */
	Oid			relid;			/* related relation Oid */
	Oid			reltablespace;	/* tablespace of the index, if set */
	char	   *indexname;		/* hypothetical index name */

	BlockNumber pages;			/* number of estimated disk pages for the
								 * index */
	double		tuples;			/* number of estimated tuples in the index */
#if PG_VERSION_NUM >= 90300
	int			tree_height;	/* estimated index tree height, -1 if unknown */
#endif

	/* index descriptor informations */
	int			ncolumns;		/* number of columns, only 1 for now */
	short int  *indexkeys;		/* attnums */
	Oid		   *indexcollations;	/* OIDs of collations of index columns */
	Oid		   *opfamily;		/* OIDs of operator families for columns */
	Oid		   *opclass;		/* OIDs of opclass data types */
	Oid		   *opcintype;		/* OIDs of opclass declared input data types */
	Oid		   *sortopfamily;	/* OIDs of btree opfamilies, if orderable */
	bool	   *reverse_sort;	/* is sort order descending? */
	bool	   *nulls_first;	/* do NULLs come first in the sort order? */
	Oid			relam;			/* OID of the access method (in pg_am) */

	RegProcedure amcostestimate;	/* OID of the access method's cost fcn */

	bool		predOK;			/* true if predicate matches query */
	bool		unique;			/* true if a unique index */
	bool		immediate;		/* is uniqueness enforced immediately? */
#if PG_VERSION_NUM >= 90500
	bool	   *canreturn;		/* which index cols can be returned in an
								 * index-only scan? */
#else
	bool		canreturn;		/* can index return IndexTuples? */
#endif
	bool		amcanorderbyop; /* does AM support order by operator result? */
	bool		amoptionalkey;	/* can query omit key for the first column? */
	bool		amsearcharray;	/* can AM handle ScalarArrayOpExpr quals? */
	bool		amsearchnulls;	/* can AM search for NULL/NOT NULL entries? */
	bool		amhasgettuple;	/* does AM have amgettuple interface? */
	bool		amhasgetbitmap; /* does AM have amgetbitmap interface? */
} hypoEntry;

List	   *entries = NIL;


/*--- Functions --- */

void		_PG_init(void);
void		_PG_fini(void);

Datum		hypopg_reset(PG_FUNCTION_ARGS);
Datum		hypopg_add_index_internal(PG_FUNCTION_ARGS);
Datum		hypopg(PG_FUNCTION_ARGS);
Datum		hypopg_create_index(PG_FUNCTION_ARGS);
Datum		hypopg_drop_index(PG_FUNCTION_ARGS);
Datum		hypopg_relation_size(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(hypopg_reset);
PG_FUNCTION_INFO_V1(hypopg_add_index_internal);
PG_FUNCTION_INFO_V1(hypopg);
PG_FUNCTION_INFO_V1(hypopg_create_index);
PG_FUNCTION_INFO_V1(hypopg_drop_index);
PG_FUNCTION_INFO_V1(hypopg_relation_size);

static hypoEntry *newHypoEntry(Oid relid, char *accessMethod, int ncolumns);
static Oid	hypoGetNewOid(Oid relid);
static void addHypoEntry(hypoEntry *entry);

static void entry_reset(void);
static bool entry_store(Oid relid,
			char *indexname,
			char *accessMethod,
			int ncolumns,
			short int indexkeys,
			int indexcollations,
			Oid opfamily,
			Oid opcintype);
static const hypoEntry *entry_store_parsetree(IndexStmt *node);
static bool entry_remove(Oid indexid);

static void
hypo_utility_hook(Node *parsetree,
				  const char *queryString,
#if PG_VERSION_NUM >= 90300
				  ProcessUtilityContext context,
#endif
				  ParamListInfo params,
#if PG_VERSION_NUM < 90300
				  bool isTopLevel,
#endif
				  DestReceiver *dest,
				  char *completionTag);
static ProcessUtility_hook_type prev_utility_hook = NULL;


static void hypo_get_relation_info_hook(PlannerInfo *root,
							Oid relationObjectId,
							bool inhparent,
							RelOptInfo *rel);
static get_relation_info_hook_type prev_get_relation_info_hook = NULL;

static const char *hypo_explain_get_index_name_hook(Oid indexId);
static explain_get_index_name_hook_type prev_explain_get_index_name_hook = NULL;

static void injectHypotheticalIndex(PlannerInfo *root,
						Oid relationObjectId,
						bool inhparent,
						RelOptInfo *rel,
						Relation relation,
						hypoEntry *entry);
static bool hypo_query_walker(Node *node);

static void hypo_set_indexname(hypoEntry *entry, char *indexname);
static void hypo_estimate_index_simple(hypoEntry *entry,
						   BlockNumber *pages, double *tuples);
static void hypo_estimate_index(hypoEntry *entry, RelOptInfo *rel);

static List *build_index_tlist(PlannerInfo *root, IndexOptInfo *index,
				  Relation heapRelation);
static Oid GetIndexOpClass(List *opclass, Oid attrType,
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
newHypoEntry(Oid relid, char *accessMethod, int ncolumns)
{
	hypoEntry  *entry;
	MemoryContext oldcontext;
	HeapTuple	tuple;
#if PG_VERSION_NUM >= 90500
	int			i;
#endif

	oldcontext = MemoryContextSwitchTo(TopMemoryContext);

	entry = palloc0(sizeof(hypoEntry));
	entry->indexname = palloc0(NAMEDATALEN);
	/* palloc all arrays */
	entry->indexkeys = palloc0(sizeof(short int) * ncolumns);
	entry->indexcollations = palloc0(sizeof(Oid) * ncolumns);
	entry->opfamily = palloc0(sizeof(Oid) * ncolumns);
	entry->opclass = palloc0(sizeof(Oid) * ncolumns);
	entry->opcintype = palloc0(sizeof(Oid) * ncolumns);
	entry->sortopfamily = palloc0(sizeof(Oid) * ncolumns);
	entry->reverse_sort = palloc0(sizeof(bool) * ncolumns);
	entry->nulls_first = palloc0(sizeof(bool) * ncolumns);
#if PG_VERSION_NUM >= 90500
	entry->canreturn = palloc0(sizeof(bool) * ncolumns);
#endif

	MemoryContextSwitchTo(oldcontext);

	entry->oid = hypoGetNewOid(relid);
	entry->relid = relid;
	entry->immediate = true;

	tuple = SearchSysCache1(AMNAME, PointerGetDatum(accessMethod));

	if (!HeapTupleIsValid(tuple))
	{
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("hypopg: access method \"%s\" does not exist",
						accessMethod)));
	}

	entry->relam = HeapTupleGetOid(tuple);
	entry->amcostestimate = ((Form_pg_am) GETSTRUCT(tuple))->amcostestimate;
	entry->amcanorderbyop = ((Form_pg_am) GETSTRUCT(tuple))->amcanorderbyop;
	entry->amoptionalkey = ((Form_pg_am) GETSTRUCT(tuple))->amoptionalkey;
	entry->amsearcharray = ((Form_pg_am) GETSTRUCT(tuple))->amsearcharray;
	entry->amsearchnulls = ((Form_pg_am) GETSTRUCT(tuple))->amsearchnulls;
	entry->amhasgettuple = OidIsValid(((Form_pg_am) GETSTRUCT(tuple))->amgettuple);
	entry->amhasgetbitmap = OidIsValid(((Form_pg_am) GETSTRUCT(tuple))->amgetbitmap);

	ReleaseSysCache(tuple);

	/*
	 * canreturn should been checked with the amcanreturn proc, but this can't
	 * be done without a real Relation, so just let force it
	 */
	switch (entry->relam)
	{
		case BTREE_AM_OID:
			/* btree always support Index-Only scan */
#if PG_VERSION_NUM >= 90500
			for (i = 0; i < ncolumns; i++)
				entry->canreturn[i] = true;
#else
			entry->canreturn = true;
#endif
			break;
		default:

			/*
			 * do not store hypothetical indexes with access method not
			 * supported
			 */
			elog(ERROR, "hypopg: access method %d is not supported",
				 entry->relam);
			break;
	}

	return entry;
}

/* Wrapper around GetNewRelFileNode
 * Return a new OID for an hypothetical index.
 */
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

/* Add an hypoEntry to hypoEntries */

static void
addHypoEntry(hypoEntry *entry)
{
	MemoryContext oldcontext;

	oldcontext = MemoryContextSwitchTo(TopMemoryContext);

	entries = lappend(entries, entry);

	MemoryContextSwitchTo(oldcontext);
}

/* Remove cleanly all hypothetical indexes by calling entry_remove on each
 * entry.
 * hypo_remove function pfree all allocated memory
 */
static void
entry_reset(void)
{
	ListCell   *lc;

	foreach(lc, entries)
	{
		hypoEntry  *entry = (hypoEntry *) lc;

		entry_remove(entry->oid);
	}

	list_free(entries);
	entries = NIL;
	return;
}

/* Simplified function to add an hypotehtical index, with inly 1 column index
 */
static bool
entry_store(Oid relid,
			char *indexname,
			char *accessMethod,
			int ncolumns,
			short int indexkeys,
			int indexcollations,
			Oid opfamily,
			Oid opcintype)
{
	hypoEntry  *entry;

	entry = newHypoEntry(relid, accessMethod, 1);

	hypo_set_indexname(entry, indexname);
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

/* Create an hypothetical index from its CREATE INDEX parsetree
 */
static const hypoEntry *
entry_store_parsetree(IndexStmt *node)
{
	hypoEntry  *entry;
	HeapTuple	tuple;
	Form_pg_attribute attform;
	Oid			relid;
	StringInfoData indexRelationName;
	int			ncolumns;
	ListCell   *lc;
	int			j;
	bool		ok = true;


	/* check first if there's an expression.
	 * the column list will probably be checked twice, but it avoids the need
	 * to worry about freeing memory later.
	 */
	foreach(lc, node->indexParams)
	{
		IndexElem  *indexelem = (IndexElem *) lfirst(lc);

		if (indexelem->expr != NULL)
		{
			ok = false;
			break;
		}
	}

	if (!ok)
	{
		elog(WARNING, "hypopg: hypothetical indexes on expression are not supported yet");
		return false;
	}

	ncolumns = list_length(node->indexParams);

	initStringInfo(&indexRelationName);
	appendStringInfo(&indexRelationName, "%s", node->accessMethod);
	appendStringInfo(&indexRelationName, "_");

	if (node->relation->schemaname != NULL && (strcmp(node->relation->schemaname, "public") != 0))
	{
		appendStringInfo(&indexRelationName, "%s", node->relation->schemaname);
		appendStringInfo(&indexRelationName, "_");
	}

	appendStringInfo(&indexRelationName, "%s", node->relation->relname);

	relid =
		RangeVarGetRelid(node->relation, AccessShareLock, false);

	/* now create the hypothetical index entry */
	ncolumns = list_length(node->indexParams);

	entry = newHypoEntry(relid, node->accessMethod, ncolumns);

	entry->unique = node->unique;
	entry->ncolumns = ncolumns;

	/* iterate through columns */
	j = 0;
	foreach(lc, node->indexParams)
	{
		IndexElem  *indexelem = (IndexElem *) lfirst(lc);
		Oid			atttype;
		Oid			opclass;

		appendStringInfo(&indexRelationName, "_");
		appendStringInfo(&indexRelationName, "%s", indexelem->name);
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
								  entry->relam);
		entry->opclass[j] = opclass;
		/* setup the opfamily */
		entry->opfamily[j] = get_opclass_family(opclass);
		/* setup the collation */
		entry->indexcollations[j] = attform->attcollation;

		ReleaseSysCache(tuple);

		entry->opcintype[j] = get_opclass_input_type(opclass);

		entry->reverse_sort[j] = (indexelem->ordering == SORTBY_DESC ? true : false);
		entry->nulls_first[j] = (indexelem->nulls_ordering == SORTBY_NULLS_FIRST ? true : false);

		j++;
	}
	Assert(j == ncolumns);

	hypo_set_indexname(entry, indexRelationName.data);

	addHypoEntry(entry);

	return entry;
}

/* Remove an hypothetical index from the list of hypothetical indexes.
 * pfree all memory that has been allocated.
 */
static bool
entry_remove(Oid indexid)
{
	ListCell   *lc;

	foreach(lc, entries)
	{
		hypoEntry  *entry = (hypoEntry *) lfirst(lc);

		if (entry->oid == indexid)
		{
			pfree(entry->indexname);
			pfree(entry->indexkeys);
			pfree(entry->indexcollations);
			pfree(entry->opfamily);
			pfree(entry->opclass);
			pfree(entry->opcintype);
			pfree(entry->sortopfamily);
			pfree(entry->reverse_sort);
			pfree(entry->nulls_first);
#if PG_VERSION_NUM >= 90500
			pfree(entry->canreturn);
#endif
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
#if PG_VERSION_NUM >= 90300
				  ProcessUtilityContext context,
#endif
				  ParamListInfo params,
#if PG_VERSION_NUM < 90300
				  bool isTopLevel,
#endif
				  DestReceiver *dest,
				  char *completionTag)
{
	isExplain = query_or_expression_tree_walker(parsetree, hypo_query_walker, NULL, 0);

	if (prev_utility_hook)
		prev_utility_hook(parsetree, queryString,
#if PG_VERSION_NUM >= 90300
						  context,
#endif
						  params,
#if PG_VERSION_NUM < 90300
						  isTopLevel,
#endif
						  dest, completionTag);
	else
		standard_ProcessUtility(parsetree, queryString,
#if PG_VERSION_NUM >= 90300
								context,
#endif
								params,
#if PG_VERSION_NUM < 90300
								isTopLevel,
#endif
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
				ListCell   *lc;

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

/* Add an hypothetical index to the list of indexes.
 * Caller should have check that the specified hypoEntry does belong to the
 * specified relation
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
	int			ncolumns,
				i;
	int			ind_avg_width = 0;


	/* create a node */
	index = makeNode(IndexOptInfo);

	index->relam = entry->relam;

	if (index->relam != BTREE_AM_OID)
	{
		/* skip this index if access method is not handled */
		elog(WARNING, "hypopg: Only btree indexes are supported for now!");
		return;
	}

	/* General stuff */
	index->indexoid = entry->oid;
	index->reltablespace = rel->reltablespace;	/* same tablespace as
												 * relation, TODO */
	index->rel = rel;
	index->ncolumns = ncolumns = entry->ncolumns;

	index->indexkeys = (int *) palloc(sizeof(int) * ncolumns);
	index->indexcollations = (Oid *) palloc(sizeof(int) * ncolumns);
	index->opfamily = (Oid *) palloc(sizeof(int) * ncolumns);
	index->opcintype = (Oid *) palloc(sizeof(int) * ncolumns);
#if PG_VERSION_NUM >= 90300
#endif

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
#if PG_VERSION_NUM < 90500
	index->canreturn = entry->canreturn;
#endif
	index->amcanorderbyop = entry->amcanorderbyop;
	index->amoptionalkey = entry->amoptionalkey;
	index->amsearcharray = entry->amsearcharray;
	index->amsearchnulls = entry->amsearchnulls;
	index->amhasgettuple = entry->amhasgettuple;
	index->amhasgetbitmap = entry->amhasgetbitmap;

	if (index->relam == BTREE_AM_OID)
	{
		index->sortopfamily = index->opfamily;
	}

	index->reverse_sort = (bool *) palloc(sizeof(bool) * ncolumns);
	index->nulls_first = (bool *) palloc(sizeof(bool) * ncolumns);
#if PG_VERSION_NUM >= 90500
	index->canreturn = (bool *) palloc(sizeof(bool) * ncolumns);
#endif

	for (i = 0; i < ncolumns; i++)
	{
		index->reverse_sort[i] = entry->reverse_sort[i];
		index->nulls_first[i] = entry->nulls_first[i];
#if PG_VERSION_NUM >= 90500
		index->canreturn[i] = entry->canreturn[i];
#endif
	}

	index->indexprs = NIL;		/* not handled for now, WIP */
	index->indpred = NIL;		/* no partial index handled for now, WIP */

	/*
	 * Build targetlist using the completed indexprs data. copied from
	 * PostgreSQL
	 */
	index->indextlist = build_index_tlist(root, index, relation);

	/*
	 * estimate most of the hypothyetical index stuff, more exactly: tuples,
	 * pages and tree_height (9.3+)
	 */
	hypo_estimate_index(entry, rel);

	index->pages = entry->pages;
	index->tuples = entry->tuples;
#if PG_VERSION_NUM >= 90300
	index->tree_height = entry->tree_height;
#endif

	/*
	 * obviously, setup this tag. However, it's only checked in
	 * selfuncs.c/get_actual_variable_range, so we still need to add
	 * hypothetical indexes *ONLY* in an explain-no-analyze command.
	 */
	index->hypothetical = true;

	/* add our hypothetical index in the relation's indexlist */
	rel->indexlist = lcons(index, rel->indexlist);
}

/* This function will execute the "injectHypotheticalIndex" for every hypothetical
 * index found for each relation if the isExplain flag is setup.
 */
static void
hypo_get_relation_info_hook(PlannerInfo *root,
							Oid relationObjectId,
							bool inhparent,
							RelOptInfo *rel)
{
	if (isExplain)
	{
		Relation	relation;

		relation = heap_open(relationObjectId, NoLock);

		if (relation->rd_rel->relkind == RELKIND_RELATION)
		{
			ListCell   *lc;

			foreach(lc, entries)
			{
				hypoEntry  *entry = (hypoEntry *) lfirst(lc);

				if (entry->relid == relationObjectId)
				{
					/*
					 * hypothetical index found, add it to the relation's
					 * indextlist
					 */
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
	char	   *ret = NULL;

	if (isExplain)
	{
		/*
		 * we're in an explain-only command. Return the name of the
		 * hypothetical index name if it's one of ours, otherwise return NULL
		 */
		ListCell   *lc;

		foreach(lc, entries)
		{
			hypoEntry  *entry = (hypoEntry *) lfirst(lc);

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
 * it supposed to be called from the provided sql function.
 */
Datum
hypopg_add_index_internal(PG_FUNCTION_ARGS)
{
	Oid			relid = PG_GETARG_OID(0);
	char	   *indexname = TextDatumGetCString(PG_GETARG_TEXT_PP(1));
	char	   *accessMethod = TextDatumGetCString(PG_GETARG_TEXT_PP(2));
	int			ncolumns = PG_GETARG_INT32(3);
	short int	indexkeys = PG_GETARG_INT16(4);
	Oid			indexcollations = PG_GETARG_OID(5);
	Oid			opfamily = PG_GETARG_OID(6);
	Oid			opcintype = PG_GETARG_OID(7);

	return entry_store(relid, indexname, accessMethod, ncolumns, indexkeys, indexcollations, opfamily, opcintype);
}

/*
 * List created hypothetical indexes
 */
Datum
hypopg(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	ListCell   *lc;


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
		hypoEntry  *entry = (hypoEntry *) lfirst(lc);
		Datum		values[HYPO_NB_COLS];
		bool		nulls[HYPO_NB_COLS];
		int			j = 0;

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		values[j++] = CStringGetTextDatum(strdup(entry->indexname));
		values[j++] = ObjectIdGetDatum(entry->oid);
		values[j++] = ObjectIdGetDatum(entry->relid);
		values[j++] = Int8GetDatum(entry->ncolumns);
		values[j++] = BoolGetDatum(entry->unique);
		values[j++] = PointerGetDatum(buildint2vector(entry->indexkeys, entry->ncolumns));
		values[j++] = PointerGetDatum(buildoidvector(entry->indexcollations, entry->ncolumns));
		values[j++] = PointerGetDatum(buildoidvector(entry->opclass, entry->ncolumns));
		nulls[j++] = true;		/* no indoption for now, TODO */
		nulls[j++] = true;		/* no hypothetical index on expr for now */
		nulls[j++] = true;		/* no hypothetical index on predicate for now */
		values[j++] = ObjectIdGetDatum(entry->relam);
		Assert(j == HYPO_NB_COLS);

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
	char	   *sql = TextDatumGetCString(PG_GETARG_TEXT_PP(0));
	List	   *parsetree_list;
	ListCell   *parsetree_item;
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	int			i = 1;

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

	parsetree_list = pg_parse_query(sql);

	foreach(parsetree_item, parsetree_list)
	{
		Node	   *parsetree = (Node *) lfirst(parsetree_item);
		Datum values[HYPO_CREATE_COLS];
		bool nulls[HYPO_CREATE_COLS];
		const hypoEntry *entry;

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		if (nodeTag(parsetree) != T_IndexStmt)
		{
			elog(WARNING,
				 "hypopg: SQL order #%d is not a CREATE INDEX statement",
				 i);
		}
		else
		{
			entry = entry_store_parsetree((IndexStmt *) parsetree);
			values[0] = ObjectIdGetDatum(entry->oid);
			values[1] = CStringGetTextDatum(strdup(entry->indexname));

			tuplestore_putvalues(tupstore, tupdesc, values, nulls);
		}
		i++;
	}

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	return (Datum) 0;
}

/*
 * SQL wrapper to drop an hypothetical index.
 */
Datum
hypopg_drop_index(PG_FUNCTION_ARGS)
{
	Oid			indexid = PG_GETARG_OID(0);

	PG_RETURN_BOOL(entry_remove(indexid));
}

/*
 * SQL Wrapper around the hypothetical index size estimation
 */
Datum
hypopg_relation_size(PG_FUNCTION_ARGS)
{
	BlockNumber pages;
	double		tuples;
	Oid			indexid = PG_GETARG_OID(0);
	ListCell   *lc;

	pages = 0;
	tuples = 0;
	foreach(lc, entries)
	{
		hypoEntry  *entry = (hypoEntry *) lfirst(lc);

		if (entry->oid == indexid)
		{
			hypo_estimate_index_simple(entry, &pages, &tuples);
		}
	}

	PG_RETURN_INT64(pages * BLCKSZ);
}


/* Simple function to set the indexname, dealing with max name length, and the
 * ending \0
 */
static void
hypo_set_indexname(hypoEntry *entry, char *indexname)
{
	char		oid[12];		/* store <oid>, oid shouldn't be more than
								 * 9999999999 */
	int			totalsize;

	snprintf(oid, sizeof(oid), "<%d>", entry->oid);

	/* we'll prefix the given indexname with the oid, and reserve a final \0 */
	totalsize = strlen(oid) + strlen(indexname) + 1;

	/* final index name must not exceed NAMEDATALEN */
	if (totalsize > NAMEDATALEN)
		totalsize = NAMEDATALEN;

	/* eventually truncate the given indexname at NAMEDATALEN-1 if needed */
	strncpy(entry->indexname, oid, strlen(oid));
	strncat(entry->indexname, indexname, totalsize - strlen(oid) - 1);
}

/*
 * Fill the pages and tuples information for a given hypoentry.
 */
static void
hypo_estimate_index_simple(hypoEntry *entry, BlockNumber *pages, double *tuples)
{
	RelOptInfo *rel;
	Relation	relation;

	/*
	 * retrieve number of tuples and pages of the related relation, adapted
	 * from plancat.c/get_relation_info().
	 */

	rel = makeNode(RelOptInfo);

	relation = heap_open(entry->relid, AccessShareLock);

	if (!RelationNeedsWAL(relation) && RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("hypopg: cannot access temporary or unlogged relations during recovery")));

	rel->min_attr = FirstLowInvalidHeapAttributeNumber + 1;
	rel->max_attr = RelationGetNumberOfAttributes(relation);
	rel->reltablespace = RelationGetForm(relation)->reltablespace;

	estimate_rel_size(relation, rel->attr_widths - rel->min_attr,
					  &rel->pages, &rel->tuples, &rel->allvisfrac);

	heap_close(relation, NoLock);

	hypo_estimate_index(entry, rel);
	*pages = entry->pages;
	*tuples = entry->tuples;
}


/*
 * Fill the pages and tuples information for a given hypoentry and a given
 * RelOptInfo
 */
static void
hypo_estimate_index(hypoEntry *entry, RelOptInfo *rel)
{
	int			i,
				ind_avg_width = 0;
	int			usable_page_size;
	int			line_size;
	double		bloat_factor;
	int			additional_bloat = 20;

	for (i = 0; i < entry->ncolumns; i++)
	{
		ind_avg_width += get_attavgwidth(entry->relid, entry->indexkeys[i]);
	}

	/*
	 * TODO: handle here when indpred and indexpres will be added, and other
	 * access methods
	 */

	switch (entry->relam)
	{
		case BTREE_AM_OID:
			/* Will need to change this when handling predicate */
			entry->tuples = rel->tuples;

			/*
			 * quick estimating of index size:
			 *
			 * sizeof(PageHeader) : 24 (1 per page) sizeof(BTPageOpaqueData):
			 * 16 (1 per page) sizeof(IndexTupleData): 8 (1 per tuple,
			 * referencing heap) sizeof(ItemIdData): 4 (1 per tuple, storing
			 * the index item) default fillfactor: 90% no NULL handling fixed
			 * additional bloat: 20%
			 *
			 * I'll also need to read more carefully nbtree code to check is
			 * this is accurate enough.
			 *
			 */
			line_size = ind_avg_width +
				+(sizeof(IndexTupleData) * entry->ncolumns)
				+ MAXALIGN(sizeof(ItemIdData) * entry->ncolumns);

			usable_page_size = BLCKSZ - sizeof(PageHeaderData) - sizeof(BTPageOpaqueData);
			bloat_factor = (200.0 - BTREE_DEFAULT_FILLFACTOR + additional_bloat) / 100;

			entry->pages =
				entry->tuples * line_size * bloat_factor / usable_page_size;
#if PG_VERSION_NUM >= 90300
			entry->tree_height = -1;	/* TODO */
#endif
			break;
		default:
			elog(WARNING, "hypopg: access method %d is not supported",
				 entry->relam);
	}
}


/* Copied from backend/optimizer/util/plancat.c, not exported.
 *
 * Build a targetlist representing the columns of the specified index.
 * Each column is represented by a Var for the corresponding base-relation
 * column, or an expression in base-relation Vars, as appropriate.
 *
 * There are never any dropped columns in indexes, so unlike
 * build_physical_tlist, we need no failure case.
 */

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
 * Copied from src/backend/commands/indexcmds.c, not exported.
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

#if PG_VERSION_NUM >= 90300
		namespaceId = LookupExplicitNamespace(schemaname, false);
#else
		namespaceId = LookupExplicitNamespace(schemaname);
#endif
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
	{
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("operator class \"%s\" does not exist for access method \"%s\"",
						NameListToString(opclass), accessMethodName)));
	}

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
