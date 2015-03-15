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
#include "catalog/pg_collation.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_opfamily.h"
#include "catalog/pg_type.h"
#include "commands/explain.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodes.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/plancat.h"
#include "pgstat.h"
#include "storage/bufmgr.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "storage/spin.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/rel.h"

PG_MODULE_MAGIC;

static const uint32 PGHYPO_FILE_HEADER = 0x6879706f;

#define HYPO_MAX_COLS	1
#define HYPO_NB_COLS		4
#if PG_VERSION_NUM >= 90300
#define HYPO_DUMP_FILE "pg_stat/pg_hypo.stat"
#else
#define HYPO_DUMP_FILE "global/pg_hypo.stat"
#endif

bool isExplain = false;
static bool	fix_empty_table = false;
static int hypo_max_indexes;	/* max # hypothetical indexes to store */

/*
 * Hypothetical index storage
 * An hypothetical index is defined by
 *   - dbid
 *   - indexid
 */
typedef struct hypoEntry
{
	Oid			dbid;		/* on which database is the index */
	Oid			indexid;	/* hypothetical index Oid */
	Oid			relid;		/* related relation Oid */
    char		indexname[NAMEDATALEN];	/* hypothetical index name */
	Oid			relam;
	int			ncolumns; /* number of columns, only 1 for now */
	int			indexkeys; /* attnum */
	Oid			indexcollations;
	Oid			opfamily;
	Oid			opcintype;
	slock_t		mutex;		/* protects fields */
} hypoEntry;

/*
 * Global shared state
 */
typedef struct hypoSharedState
{
	LWLockId	lock;			/* protects array search/modification */
} hypoSharedState;

/* Links to shared memory state */
static hypoSharedState *hypo = NULL;
static hypoEntry *hypoEntries = NULL;


/*--- Functions --- */

void	_PG_init(void);
void	_PG_fini(void);

Datum	pg_hypo_reset(PG_FUNCTION_ARGS);
Datum	pg_hypo_add_index_internal(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pg_hypo_reset);
PG_FUNCTION_INFO_V1(pg_hypo_add_index_internal);
PG_FUNCTION_INFO_V1(pg_hypo);

static Size hypo_memsize(void);
static void entry_reset(void);
static bool entry_store(Oid dbid,
			Oid indexid,
			Oid relid,
			char *indexname,
			Oid relam,
			int ncolumns,
			int indexkeys,
			int indexcollations,
			Oid opfamily,
			Oid opcintype);

static void hypo_shmem_startup(void);
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;

static void hypo_shmem_shutdown(int code, Datum arg);

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
						hypoEntry entry);
static bool hypo_query_walker(Node *node);

static List *
build_index_tlist(PlannerInfo *root, IndexOptInfo *index,
				  Relation heapRelation);

void
_PG_init(void)
{
	/* Define custom GUC variables */
	DefineCustomIntVariable( "pg_hypo.max_indexes",
	  "Define how many hypothetical indexes will be stored.",
							NULL,
							&hypo_max_indexes,
							200,
							1,
							INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	RequestAddinShmemSpace(hypo_memsize());
	RequestAddinLWLocks(1);

	/* Install hooks */
	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = hypo_shmem_startup;

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
hypo_shmem_startup(void)
{
	bool		found;
	FILE		*file;
	int			i,t;
	uint32		header;
	int32		num;
	hypoEntry	*entry;
	hypoEntry	*buffer = NULL;

	if (prev_shmem_startup_hook)
		prev_shmem_startup_hook();

	/* reset in case this is a restart within the postmaster */
	hypo = NULL;

	/* Create or attach to the shared memory state */
	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

	/* global access lock */
	hypo = ShmemInitStruct("pg_hypo",
					sizeof(hypoSharedState),
					&found);

	if (!found)
	{
		/* First time through ... */
		hypo->lock = LWLockAssign();
	}

	/* allocate stats shared memory structure */
	hypoEntries = ShmemInitStruct("pg_hypo stats",
					sizeof(hypoEntry) * hypo_max_indexes,
					&found);

	if (!found)
	{
		entry_reset();

	}

	LWLockRelease(AddinShmemInitLock);

	if (!IsUnderPostmaster)
		on_shmem_exit(hypo_shmem_shutdown, (Datum) 0);

	/* Load stat file, don't care about locking */
	file = AllocateFile(HYPO_DUMP_FILE, PG_BINARY_R);
	if (file == NULL)
	{
		if (errno == ENOENT)
			return;			/* ignore not-found error */
		goto error;
	}

	// TODO buffer = (pgskEntry *) palloc(sizeof(pgskEntry));

	// TODO /* check is header is valid */
	// TODO if (fread(&header, sizeof(uint32), 1, file) != 1 ||
	// TODO 	header != PGSK_FILE_HEADER)
	// TODO 	goto error;

	// TODO /* get number of entries */
	// TODO if (fread(&num, sizeof(int32), 1, file) != 1)
	// TODO 	goto error;

	// TODO entry = pgskEntries;
	// TODO for (i = 0; i < num ; i++)
	// TODO {
	// TODO 	if (fread(buffer, offsetof(pgskEntry, mutex), 1, file) != 1)
	// TODO 		goto error;

	// TODO 	entry->dbid = buffer->dbid;
	// TODO 	for (t = 0; t < CMD_NOTHING; t++)
	// TODO 	{
	// TODO 		entry->reads[t] = buffer->reads[t];
	// TODO 		entry->writes[t] = buffer->writes[t];
	// TODO 		entry->utime[t] = buffer->utime[t];
	// TODO 		entry->stime[t] = buffer->stime[t];
	// TODO 	}
	// TODO 	/* don't initialize spinlock, already done */
	// TODO 	entry++;
	// TODO }

	pfree(buffer);
	FreeFile(file);

	/*
	 * Remove the file so it's not included in backups/replication slaves,
	 * etc. A new file will be written on next shutdown.
	 */
	unlink(HYPO_DUMP_FILE);

	return;

error:
	ereport(LOG,
			(errcode_for_file_access(),
			 errmsg("could not read pg_stat_kcache file \"%s\": %m",
					HYPO_DUMP_FILE)));
	if (buffer)
		pfree(buffer);
	if (file)
		FreeFile(file);
	/* delete bogus file, don't care of errors in this case */
	unlink(HYPO_DUMP_FILE);
}

/*
 * shmem_shutdown hook: dump statistics into file.
 *
 */
static void
hypo_shmem_shutdown(int code, Datum arg)
{
	//TODO
}

static Size
hypo_memsize(void)
{
	Size	size;

	size = MAXALIGN(sizeof(hypoSharedState)) + MAXALIGN(sizeof(hypoEntry)) * hypo_max_indexes;
	
	return size;
}

static void
entry_reset(void)
{
	int i;
	hypoEntry  *entry;

	LWLockAcquire(hypo->lock, LW_EXCLUSIVE);

	/* Mark all entries dbid and indexid with InvalidOid */
	entry = hypoEntries;
	for (i = 0; i < hypo_max_indexes ; i++)
	{
		entry->dbid = InvalidOid;
		entry->indexid = InvalidOid;
		SpinLockInit(&entry->mutex);
		entry++;
	}

	LWLockRelease(hypo->lock);
	return;
}

static bool
entry_store(Oid dbid,
			Oid indexid,
			Oid relid,
			char *indexname,
			Oid relam,
			int ncolumns,
			int indexkeys,
			int indexcollations,
			Oid opfamily,
			Oid opcintype)
{
	int i = 0;
	hypoEntry *entry;
	int ret = 2;

	/* Make sure user didn't try to add too many columns */
	if (ncolumns > HYPO_MAX_COLS)
		return false;

	entry = hypoEntries;

	LWLockAcquire(hypo->lock, LW_SHARED);

	while (i < hypo_max_indexes && ret == 2)
	{
		if ( /* don't store twice the same index */
			(entry->dbid == dbid) &&
			(entry->relid == relid) &&
			(entry->indexkeys == indexkeys) &&
			(entry->relam == relam)
		)
		{
			ret = 1;
			break;
		}

		if (entry->dbid == InvalidOid) {
			SpinLockAcquire(&entry->mutex);
			entry->dbid = dbid;
			entry->indexid = i+1;
			entry->relid = relid;
			strncpy(entry->indexname, indexname, NAMEDATALEN);
			entry->relam = relam;
			entry->ncolumns = ncolumns;
			entry->indexkeys = indexkeys;
			entry->indexcollations = indexcollations;
			entry->opfamily = opfamily;
			entry->opcintype = opcintype;
			SpinLockRelease(&entry->mutex);
			ret = 0;
			break;
		}
		entry++;
		i++;
	}

	LWLockRelease(hypo->lock);

	/* if there's no more room, then raise a warning */	
	switch(ret)
	{
		case 0:
			return true;
			break;
		case 1:
			elog(WARNING, "pg_hypo: Index already existing \"%s\"", indexname, indexid);
			return false;
			break;
		case 2:
			elog(WARNING, "pg_hypo: no more free entry for storing index \"%s\"", indexname, indexid);
			return false;
		default:
			return false;
			break;
	}
	return true;
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
addHypotheticalIndex(PlannerInfo *root, Oid relationObjectId, bool inhparent, RelOptInfo *rel, Relation relation, hypoEntry entry)
{
	IndexOptInfo *index;
	int ncolumns, i;


	/* create a node */
	index = makeNode(IndexOptInfo);

	index->relam = entry.relam;

	if (index->relam != BTREE_AM_OID)
	{
		elog(WARNING, "pg_hypo: Only btree indexes are supported for now!");
		return;
	}

	// General stuff
	index->indexoid = entry.indexid;
	index->reltablespace = rel->reltablespace; // same as relation
	index->rel = rel;
	index->ncolumns = ncolumns = entry.ncolumns;

	index->indexkeys = (int *) palloc(sizeof(int) * ncolumns);
	index->indexcollations = (Oid *) palloc(sizeof(int) * ncolumns);
	index->opfamily = (Oid *) palloc(sizeof(int) * ncolumns);
	index->opcintype = (Oid *) palloc(sizeof(int) * ncolumns);

	for (i = 0; i < ncolumns; i++)
	{
		index->indexkeys[i] = 1; // 1st col, WIP
		switch (index->relam)
		{
			case BTREE_AM_OID:
				// hardcode int4 cols, WIP
				index->indexcollations[i] = 0; //?? C_COLLATION_OID;
				index->opfamily[i] = entry.opfamily; //INTEGER_BTREE_FAM_OID;
				index->opcintype[i] = entry.opcintype; //INT4OID; // ??INT4_BTREE_OPS_OID; // btree integer opclass
				break;
		}
	}

	if (index->relam == BTREE_AM_OID)
	{
		index->canreturn = true;
		index->amcanorderbyop = false;
		index->amoptionalkey = true;
		index->amsearcharray = true;
		index->amsearchnulls = true;
		index->amhasgettuple = true;
		index->amhasgetbitmap = true;
		index->unique = false;
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
	index->indpred = NIL; // not handled for now, WIP
	/* Build targetlist using the completed indexprs data, stolen from PostgreSQL */
	index->indextlist = build_index_tlist(root, index, relation);

	if (index->indpred == NIL)
	{
		index->pages = rel->tuples / 10; // Should compute if col width and other stuff, WIP
		index->tuples = rel->tuples; // Same
	}

	index->hypothetical = true;

	rel->indexlist = lcons(index, rel->indexlist);
}

/* This function will execute the "addHypotheticalIndexes" for every relation
 * if the isExplain flag is setup.
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
			hypoEntry *entry;
			hypoEntry current;
			int i;

			/* Search for all indexes on specified relation */
			entry = hypoEntries;

			LWLockAcquire(hypo->lock, LW_SHARED);

			while (i < hypo_max_indexes)
			{
				if (entry->dbid == InvalidOid)
					break;
				if (entry->relid == relationObjectId && entry->dbid == MyDatabaseId) {
					SpinLockAcquire(&entry->mutex);

					current.dbid = entry->dbid;
					current.indexid = entry->indexid;
					current.relid = entry->relid;
					strncpy(current.indexname, entry->indexname, NAMEDATALEN);
					current.relam = entry->relam;
					current.ncolumns = entry->ncolumns;
					current.indexkeys = entry->indexkeys;
					current.indexcollations = entry->indexcollations;
					current.opfamily = entry->opfamily;
					current.opcintype = entry->opcintype;

					SpinLockRelease(&entry->mutex);
					// Call the main function which will add hypothetical indexes if needed
					addHypotheticalIndex(root, relationObjectId, inhparent, rel, relation, current);
				}
				entry++;
				i++;
			}

			LWLockRelease(hypo->lock);
		}

		heap_close(relation, NoLock);
	}

	if (prev_get_relation_info_hook)
		prev_get_relation_info_hook(root, relationObjectId, inhparent, rel);
}

/* Generate an hypothetical name if needed.
 */
static const char *
hypo_explain_get_index_name_hook(Oid indexId)
{
	char *ret = NULL;
	if (isExplain)
	{
		/* we're in an explain-only command. Return the name of the
		   * hypothetical index name if it's one of ours, otherwise return
		   * NULL, explain_get_index_name will do it's job.
		 */
		int i = 0;
		hypoEntry *entry;
		bool found = false;

		entry = hypoEntries;

		LWLockAcquire(hypo->lock, LW_SHARED);

		while (i < hypo_max_indexes && !found)
		{
			if (entry->indexid == InvalidOid)
				break;
			if (entry->indexid == indexId) {
				SpinLockAcquire(&entry->mutex);
				ret = entry->indexname;
				SpinLockRelease(&entry->mutex);
				break;
			}
			entry++;
			i++;
		}
		LWLockRelease(hypo->lock);
	}
	return ret;
}

/*
 * Reset statistics.
 */
Datum
pg_hypo_reset(PG_FUNCTION_ARGS)
{
	if (!hypo)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("pg_hypo must be loaded via shared_preload_libraries")));

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
	Oid		indexid = PG_GETARG_OID(0);
	Oid		relid = PG_GETARG_OID(1);
	char	*indexname = TextDatumGetCString(PG_GETARG_TEXT_PP(2));
	Oid		relam = PG_GETARG_OID(3);
	int		ncolumns = PG_GETARG_INT32(4);
	int		indexkeys = PG_GETARG_INT32(5);
	Oid		indexcollations = PG_GETARG_OID(6);
	Oid		opfamily = PG_GETARG_OID(7);
	Oid		opcintype = PG_GETARG_OID(8);

	if (!hypo)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("pg_hypo must be loaded via shared_preload_libraries")));

	return entry_store(MyDatabaseId, indexid, relid, indexname, relam, ncolumns, indexkeys, indexcollations, opfamily, opcintype);
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
	hypoEntry		*entry;
	TupleDesc		tupdesc;
	Tuplestorestate	*tupstore;
	int				i = 0;


	if (!hypo)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("pg_hypo must be loaded via shared_preload_libraries")));
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

	LWLockAcquire(hypo->lock, LW_SHARED);

	entry = hypoEntries;
	while (i < hypo_max_indexes)
	{
		Datum		values[HYPO_NB_COLS];
		bool		nulls[HYPO_NB_COLS];
		int			j = 0;

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		if (entry->dbid == InvalidOid)
			break;
		SpinLockAcquire(&entry->mutex);

		values[j++] = ObjectIdGetDatum(entry->dbid);
		values[j++] = CStringGetTextDatum(strdup(entry->indexname));
		values[j++] = ObjectIdGetDatum(entry->relid);
		values[j++] = Int32GetDatum(entry->indexkeys);
		values[j++] = ObjectIdGetDatum(entry->relam);

		SpinLockRelease(&entry->mutex);

		Assert(j == PG_STAT_PLAN_COLS);

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
		entry++;
		i++;
	}

	LWLockRelease(hypo->lock);

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
