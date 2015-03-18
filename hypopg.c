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
#include "catalog/heap.h"
#include "catalog/namespace.h"
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
#include "utils/rel.h"
#include "utils/syscache.h"

PG_MODULE_MAGIC;

#define HYPO_MAX_COLS	10 /* # of column an hypothetical index can have */
#define HYPO_NB_COLS		3 /* # of column hypopg() returns */
#define HYPO_NB_INDEXES		50 /* # of hypothetical index a single session can hold */
#define HYPO_MAX_INDEXNAME	4096

bool isExplain = false;
static bool	fix_empty_table = false;

/*
 * Hypothetical index storage, pretty much an IndexOptInfo
 */
typedef struct hypoEntry
{
	Oid			relid;		/* related relation Oid */
	Oid			reltablespace;
	char		indexname[HYPO_MAX_INDEXNAME];	/* hypothetical index name */
	int			ncolumns; /* number of columns, only 1 for now */
	int			indexkeys[HYPO_MAX_COLS]; /* attnums */
	Oid			indexcollations[HYPO_MAX_COLS];
	Oid			opfamily[HYPO_MAX_COLS];
	Oid			opcintype[HYPO_MAX_COLS];
	Oid			sortopfamily[HYPO_MAX_COLS];
	bool		reverse_sort[HYPO_MAX_COLS];
	bool		nulls_first[HYPO_MAX_COLS];
	Oid			relam;
} hypoEntry;

hypoEntry	entries[HYPO_NB_INDEXES];


/*--- Functions --- */

void	_PG_init(void);
void	_PG_fini(void);

Datum	hypopg_reset(PG_FUNCTION_ARGS);
Datum	hypopg_add_index_internal(PG_FUNCTION_ARGS);
Datum	hypopg(PG_FUNCTION_ARGS);
Datum	hypopg_create_index(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(hypopg_reset);
//PG_FUNCTION_INFO_V1(hypopg_add_index_internal);
PG_FUNCTION_INFO_V1(hypopg);
PG_FUNCTION_INFO_V1(hypopg_create_index);

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
						int position);
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
		//if ( /* don't store twice the same index */
		//	(entries[i].relid == relid) &&
		//	(entries[i].indexkeys == indexkeys) &&
		//	(entries[i].relam == relam)
		//)
		//{
		//	/* if index already exists, then raise a warning */
		//	elog(WARNING, "hypopg: Index already existing \"%s\"", indexname);
		//	return false;
		//}

		//if (entries[i].relid == InvalidOid)
		{
			entries[i].relid = relid;
			strcpy(entries[i].indexname, strncat(entries[i].indexname, indexname, HYPO_MAX_INDEXNAME));
			entries[i].relam = relam;
			entries[i].ncolumns = ncolumns;
			entries[i].indexkeys[0] = indexkeys;
			entries[i].indexcollations[0] = indexcollations;
			entries[i].opfamily[0] = opfamily;
			entries[i].opcintype[0] = opcintype;

			return true;
		}
		i++;
	}

	/* if there's no more room, then raise a warning */
	elog(WARNING, "hypopg: no more free entry for storing index \"%s\"", indexname);
	return false;
}

static bool
entry_store_parsetree(IndexStmt *node)
{
	HeapTuple			tuple;
	Form_pg_attribute	attform;
	Oid					relid;
	char				*indexRelationName;
	//char	   *accessMethodName;
	//Oid		   *typeObjectId;
	//Oid		   *collationObjectId;
	//Oid		   *classObjectId;
	Oid					accessMethodId;
	//Oid			namespaceId;
	//Oid			tablespaceId;
	//List	   *indexColNames;
	//Relation	rel;
	//Relation	indexRelation;
	//HeapTuple	tuple;
	//Form_pg_am	accessMethodForm;
	//bool		amcanorder;
	//RegProcedure amoptions;
	//Datum		reloptions;
	//int16	   *coloptions;
	//IndexInfo  *indexInfo;
	//int			numberOfAttributes;
	//TransactionId limitXmin;
	//VirtualTransactionId *old_snapshots;
	//int			n_old_snapshots;
	//LockRelId	heaprelid;
	//LOCKTAG		heaplocktag;
	//LOCKMODE	lockmode;
	//Snapshot	snapshot;
	int			i = 0;
	int			ncolumns;

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
	//accessMethodForm = (Form_pg_am) GETSTRUCT(tuple);

	ReleaseSysCache(tuple);

	/* now add the hypothetical index */
	while (i < HYPO_NB_INDEXES)
	{
		//if ( /* don't store twice the same index */
		//	(entries[i].relid == relid) &&
		//	(entries[i].indexkeys == indexkeys) &&
		//	(entries[i].relam == relam)
		//)
		//{
		//	/* if index already exists, then raise a warning */
		//	elog(WARNING, "hypopg: Index already existing \"%s\"", indexname);
		//	return false;
		//}

		if (entries[i].relid == InvalidOid)
		{
			ListCell	*lc;
			int			j = 0;

			entries[i].relid = relid;
			entries[i].relam = accessMethodId;
			entries[i].ncolumns = ncolumns = list_length(node->indexParams);

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
				entries[i].indexkeys[j] = attform->attnum;

				/* get the atttype */
				atttype = attform->atttypid;
				/* get the opclass */
				opclass = GetIndexOpClass(indexelem->opclass,
						atttype,
						node->accessMethod,
						accessMethodId);
				/* setup the opfamily */
				entries[i].opfamily[j] = get_opclass_family(opclass);

				ReleaseSysCache(tuple);

				entries[i].opcintype[j] = atttype; //TODO
			entries[i].indexcollations[j] = 0; /* TODO */

				j++;
			}
			strcpy(entries[i].indexname, indexRelationName);

			return true;
		}
		i++;
	}

	/* if there's no more room, then raise a warning */
	elog(WARNING, "hypopg: no more free entry for storing index \"%s\"", indexRelationName);
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
		elog(WARNING, "hypopg: Only btree indexes are supported for now!");
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
		index->indexkeys[i] = entries[position].indexkeys[i];
		ind_avg_width += get_attavgwidth(relation->rd_id, index->indexkeys[i]);
		switch (index->relam)
		{
			case BTREE_AM_OID:
				// hardcode int4 cols, WIP
				index->indexcollations[i] = 0; //?? C_COLLATION_OID;
				index->opfamily[i] = entries[position].opfamily[i]; //INTEGER_BTREE_FAM_OID;
				index->opcintype[i] = entries[position].opcintype[i]; //INT4OID; // ??INT4_BTREE_OPS_OID; // btree integer opclass
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
			int i;

			for (i = 0; i < HYPO_NB_INDEXES; i++)
			{
				if (entries[i].relid == InvalidOid)
					break;
				if (entries[i].relid == relationObjectId) {
					// hypothetical index found, add it to the relation's indextlist
					injectHypotheticalIndex(root, relationObjectId, inhparent, rel, relation, i);
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
		values[j++] = ObjectIdGetDatum(entries[i].relam);

		Assert(j == PG_STAT_PLAN_COLS);

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
		i++;
	}

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	return (Datum) 0;
}

/*
 * List created hypothetical indexes
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
