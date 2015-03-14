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
#include "utils/guc.h"
#include "utils/rel.h"

static const uint32 PGHYPO_FILE_HEADER = 0x6879706f;

PG_MODULE_MAGIC;

#define HYPOTHETICAL_INDEX_OID	0;
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
 */
typedef struct hypoEntry
{
	Oid			indexid;	/* hypothetical index Oid */
	Oid			relid;		/* related relation Oid */
    char		*indexname;	/* hypothetical index name */
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

static Size hypo_memsize(void);
static void entry_reset(void);
static void entry_store(Oid indexid, Oid relid, char *indexname);

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

static void addHypotheticalIndexes(PlannerInfo *root,
						Oid relationObjectId,
						bool inhparent,
						RelOptInfo *rel,
						Relation relation);
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
		entry_store(1, 1, "hypo_toto");

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

	/* Mark all entries indexid with InvalidOid */
	entry = hypoEntries;
	for (i = 0; i < hypo_max_indexes ; i++)
	{
		entry->indexid = InvalidOid;
		SpinLockInit(&entry->mutex);
		entry++;
	}

	LWLockRelease(hypo->lock);
	return;
}

static void
entry_store(Oid indexid, Oid relid, char *indexname)
{
	int i = 0;
	hypoEntry *entry;
	bool found = false;

	entry = hypoEntries;

	LWLockAcquire(hypo->lock, LW_SHARED);

	while (i < hypo_max_indexes && !found)
	{
		if (entry->indexid == indexid || entry->indexid == InvalidOid) {
			SpinLockAcquire(&entry->mutex);
			entry->indexid = indexid;
			entry->relid = relid;
			entry->indexname = indexname;
			SpinLockRelease(&entry->mutex);
			found = true;
			break;
		}
		entry++;
		i++;
	}

	/* if there's no more room, then raise a warning */	
	if (!found)
	{
		elog(WARNING, "pg_hypo: no more free entry for storing index %s (%d)", indexname, indexid);
	}

	LWLockRelease(hypo->lock);
	return;
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
addHypotheticalIndexes(PlannerInfo *root, Oid relationObjectId, bool inhparent, RelOptInfo *rel, Relation relation)
{
	IndexOptInfo *index;
	int ncolumns, i;

	/* create a node */
	index = makeNode(IndexOptInfo);

	index->relam = BTREE_AM_OID; // more to be added

	if (index->relam != BTREE_AM_OID)
	{
		elog(WARNING, "pg_hypo: Only btree indexes are supported for now");
		return;
	}

	// General stuff
	index->indexoid = HYPOTHETICAL_INDEX_OID;
	index->reltablespace = rel->reltablespace; // same as relation
	index->rel = rel;
	index->ncolumns = ncolumns = 1; // only 1 col indexes for now

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
				index->opfamily[i] = INTEGER_BTREE_FAM_OID;
				index->opcintype[i] = INT4OID; // ??INT4_BTREE_OPS_OID; // btree integer opclass
				break;
		}
	}
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
			// Call the main function which will add hypothetical indexes if needed
			addHypotheticalIndexes(root, relationObjectId, inhparent, rel, relation);
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
			if (entry->indexid == indexId || entry->indexid == InvalidOid) {
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
