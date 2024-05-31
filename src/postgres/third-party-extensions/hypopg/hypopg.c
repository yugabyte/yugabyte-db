/*-------------------------------------------------------------------------
 *
 * hypopg.c: Implementation of hypothetical indexes for PostgreSQL
 *
 * Some functions are imported from PostgreSQL source code, theses are present
 * in hypopg_import.* files.
 *
 * This program is open source, licensed under the PostgreSQL license.
 * For license terms, see the LICENSE file.
 *
 * Copyright (C) 2015-2022: Julien Rouhaud
 *
 *-------------------------------------------------------------------------
 */


#include "postgres.h"
#include "fmgr.h"

#if PG_VERSION_NUM < 120000
#include "access/sysattr.h"
#endif
#include "access/transam.h"
#if PG_VERSION_NUM < 140000
#include "catalog/indexing.h"
#endif
#if PG_VERSION_NUM >= 110000
#include "catalog/partition.h"
#include "nodes/pg_list.h"
#include "utils/lsyscache.h"
#endif
#include "executor/spi.h"
#include "miscadmin.h"
#include "utils/elog.h"

#include "include/hypopg.h"
#include "include/hypopg_import.h"
#include "include/hypopg_index.h"

PG_MODULE_MAGIC;

/*--- Variables exported ---*/

bool		isExplain;
bool		hypo_is_enabled;
bool		hypo_use_real_oids;
MemoryContext HypoMemoryContext;

/*--- Private variables ---*/

static Oid last_oid = InvalidOid;
static Oid min_fake_oid = InvalidOid;
static bool oid_wraparound = false;

/*--- Functions --- */

PGDLLEXPORT void _PG_init(void);

PGDLLEXPORT Datum hypopg_reset(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(hypopg_reset);

static void
			hypo_utility_hook(
#if PG_VERSION_NUM >= 100000
							  PlannedStmt *pstmt,
#else
							  Node *parsetree,
#endif
							  const char *queryString,
#if PG_VERSION_NUM >= 140000
							  bool readOnlyTree,
#endif
#if PG_VERSION_NUM >= 90300
							  ProcessUtilityContext context,
#endif
							  ParamListInfo params,
#if PG_VERSION_NUM >= 100000
							  QueryEnvironment *queryEnv,
#endif
#if PG_VERSION_NUM < 90300
							  bool isTopLevel,
#endif
							  DestReceiver *dest,
#if PG_VERSION_NUM < 130000
							  char *completionTag
#else
							  QueryCompletion *qc
#endif
							  );
static ProcessUtility_hook_type prev_utility_hook = NULL;

static void hypo_executorEnd_hook(QueryDesc *queryDesc);
static ExecutorEnd_hook_type prev_ExecutorEnd_hook = NULL;


static Oid hypo_get_min_fake_oid(void);
static void hypo_get_relation_info_hook(PlannerInfo *root,
										Oid relationObjectId,
										bool inhparent,
										RelOptInfo *rel);
static get_relation_info_hook_type prev_get_relation_info_hook = NULL;

static bool hypo_index_match_table(hypoIndex *entry, Oid relid);
static bool hypo_query_walker(Node *node);

void
_PG_init(void)
{
	/* Install hooks */
	prev_utility_hook = ProcessUtility_hook;
	ProcessUtility_hook = hypo_utility_hook;

	prev_ExecutorEnd_hook = ExecutorEnd_hook;
	ExecutorEnd_hook = hypo_executorEnd_hook;

	prev_get_relation_info_hook = get_relation_info_hook;
	get_relation_info_hook = hypo_get_relation_info_hook;

	prev_explain_get_index_name_hook = explain_get_index_name_hook;
	explain_get_index_name_hook = hypo_explain_get_index_name_hook;

	isExplain = false;
	hypoIndexes = NIL;

	HypoMemoryContext = AllocSetContextCreate(TopMemoryContext,
											  "HypoPG context",
#if PG_VERSION_NUM >= 90600
											  ALLOCSET_DEFAULT_SIZES
#else
											  ALLOCSET_DEFAULT_MINSIZE,
											  ALLOCSET_DEFAULT_INITSIZE,
											  ALLOCSET_DEFAULT_MAXSIZE
#endif
		);

	DefineCustomBoolVariable("hypopg.enabled",
							 "Enable / Disable hypopg",
							 NULL,
							 &hypo_is_enabled,
							 true,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomBoolVariable("hypopg.use_real_oids",
							 "Use real oids rather than the range < 16384",
							 NULL,
							 &hypo_use_real_oids,
							 false,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	EmitWarningsOnPlaceholders("hypopg");
}

/*---------------------------------
 * Return a new OID for an hypothetical index.
 *
 * To avoid locking on pg_class (required to safely call GetNewOidWithIndex or
 * similar) and to be usable on a standby node, use the oids unused in the
 * FirstBootstrapObjectId / FirstNormalObjectId range rather than real oids.
 * For performance, always start with the biggest oid lesser than
 * FirstNormalObjectId.  This way the loop to find an unused oid will only
 * happens once a single backend has created more than ~2.5k hypothetical
 * indexes.
 *
 * For people needing to have thousands of hypothetical indexes at the same
 * time, we also allow to use the initial implementation that relies on real
 * oids, which comes with all the limitations mentioned above.
 */
Oid
hypo_getNewOid(Oid relid)
{
	Oid			newoid = InvalidOid;

	if (hypo_use_real_oids)
	{
		Relation	pg_class;
		Relation	relation;

		/* Open the relation on which we want a new OID */
		relation = table_open(relid, AccessShareLock);

		/* Close the relation and release the lock now */
		table_close(relation, AccessShareLock);

		/* Open pg_class to aks a new OID */
		pg_class = table_open(RelationRelationId, RowExclusiveLock);

		/* ask for a new Oid */
		newoid = GetNewOidWithIndex(pg_class, ClassOidIndexId,
#if PG_VERSION_NUM < 120000
									ObjectIdAttributeNumber
#else
									Anum_pg_class_oid
#endif
									);

		/* Close pg_class and release the lock now */
		table_close(pg_class, RowExclusiveLock);
	}
	else
	{
		/*
		 * First, make sure we know what is the biggest oid smaller than
		 * FirstNormalObjectId present in pg_class.  This can never change so
		 * we cache the value.
		 */
		if (!OidIsValid(min_fake_oid))
			min_fake_oid = hypo_get_min_fake_oid();

		Assert(OidIsValid(min_fake_oid));

		/* Make sure there's enough room to get one more Oid */
		if (list_length(hypoIndexes) >= (FirstNormalObjectId - min_fake_oid))
		{
			ereport(ERROR,
					(errmsg("hypopg: not more oid available"),
					errhint("Remove hypothetical indexes "
						"or enable hypopg.use_real_oids")));
		}

		while(!OidIsValid(newoid))
		{
			CHECK_FOR_INTERRUPTS();

			if (!OidIsValid(last_oid))
				newoid = last_oid = min_fake_oid;
			else
				newoid = ++last_oid;

			/* Check if we just exceeded the fake oids range */
			if (newoid >= FirstNormalObjectId)
			{
				newoid = min_fake_oid;
				last_oid = InvalidOid;
				oid_wraparound = true;
			}

			/*
			 * If we already used all available fake oids, we have to make sure
			 * that the oid isn't used anymore.
			 */
			if (oid_wraparound)
			{
				if (hypo_get_index(newoid) != NULL)
				{
					/* We can't use this oid.  Reset newoid and start again */
					newoid = InvalidOid;
				}
			}
		}
	}

	Assert(OidIsValid(newoid));
	return newoid;
}

/* Reset the state of the fake oid generator. */
void
hypo_reset_fake_oids(void)
{
	Assert(hypoIndexes == NIL);
	last_oid = InvalidOid;
	oid_wraparound = false;
}

/* This function setup the "isExplain" flag for next hooks.
 * If this flag is setup, we can add hypothetical indexes.
 */
void
hypo_utility_hook(
#if PG_VERSION_NUM >= 100000
				  PlannedStmt *pstmt,
#else
				  Node *parsetree,
#endif
				  const char *queryString,
#if PG_VERSION_NUM >= 140000
				  bool readOnlyTree,
#endif
#if PG_VERSION_NUM >= 90300
				  ProcessUtilityContext context,
#endif
				  ParamListInfo params,
#if PG_VERSION_NUM >= 100000
				  QueryEnvironment *queryEnv,
#endif
#if PG_VERSION_NUM < 90300
				  bool isTopLevel,
#endif
				  DestReceiver *dest,
#if PG_VERSION_NUM < 130000
				  char *completionTag
#else
				  QueryCompletion *qc
#endif
				  )
{
	isExplain = query_or_expression_tree_walker(
#if PG_VERSION_NUM >= 100000
												(Node *) pstmt,
#else
												parsetree,
#endif
												hypo_query_walker,
												NULL, 0);

	if (prev_utility_hook)
		prev_utility_hook(
#if PG_VERSION_NUM >= 100000
						  pstmt,
#else
						  parsetree,
#endif
						  queryString,
#if PG_VERSION_NUM >= 140000
						  readOnlyTree,
#endif
#if PG_VERSION_NUM >= 90300
						  context,
#endif
						  params,
#if PG_VERSION_NUM >= 100000
						  queryEnv,
#endif
#if PG_VERSION_NUM < 90300
						  isTopLevel,
#endif
						  dest,
#if PG_VERSION_NUM < 130000
						  completionTag
#else
						  qc
#endif
						  );
	else
		standard_ProcessUtility(
#if PG_VERSION_NUM >= 100000
								pstmt,
#else
								parsetree,
#endif
								queryString,
#if PG_VERSION_NUM >= 140000
								readOnlyTree,
#endif
#if PG_VERSION_NUM >= 90300
								context,
#endif
								params,
#if PG_VERSION_NUM >= 100000
								queryEnv,
#endif
#if PG_VERSION_NUM < 90300
								isTopLevel,
#endif
								dest,
#if PG_VERSION_NUM < 130000
						  completionTag
#else
						  qc
#endif
						  );

}

static bool
hypo_index_match_table(hypoIndex *entry, Oid relid)
{
	/* Hypothetical index on the exact same relation, use it. */
	if (entry->relid == relid)
		return true;
#if PG_VERSION_NUM >= 110000
	/*
	 * If the table is a partition, see if the hypothetical index belongs to
	 * one of the partition parent.
	 */
	if (get_rel_relispartition(relid))
	{
		List *parents = get_partition_ancestors(relid);
		ListCell *lc;

		foreach(lc, parents)
		{
			Oid oid = lfirst_oid(lc);

			if (oid == entry->relid)
				return true;
		}
	}
#endif

	return false;
}

/* Detect if the current utility command is compatible with hypothetical indexes
 * i.e. an EXPLAIN, no ANALYZE
 */
static bool
hypo_query_walker(Node *parsetree)
{
	if (parsetree == NULL)
		return false;

#if PG_VERSION_NUM >= 100000
	parsetree = ((PlannedStmt *) parsetree)->utilityStmt;
	if (parsetree == NULL)
		return false;
#endif
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


/* Reset the isExplain flag after each query */
static void
hypo_executorEnd_hook(QueryDesc *queryDesc)
{
	isExplain = false;

	if (prev_ExecutorEnd_hook)
		prev_ExecutorEnd_hook(queryDesc);
	else
		standard_ExecutorEnd(queryDesc);
}

/*
 * Return the minmum usable oid in the FirstBootstrapObjectId -
 * FirstNormalObjectId range.
 */
static Oid
hypo_get_min_fake_oid(void)
{
	int			ret, nb;
	Oid			oid = InvalidOid;

	/*
	 * Connect to SPI manager
	 */
	if ((ret = SPI_connect()) < 0)
		/* internal error */
		elog(ERROR, "SPI connect failure - returned %d", ret);

	ret = SPI_execute("SELECT max(oid)"
			" FROM pg_catalog.pg_class"
			" WHERE oid < " CppAsString2(FirstNormalObjectId),
			true, 1);
	nb = SPI_processed;

	if (ret != SPI_OK_SELECT || nb == 0)
	{
		SPI_finish();
		elog(ERROR, "hypopg: could not find the minimum fake oid");
	}

	oid = atooid(SPI_getvalue(SPI_tuptable->vals[0],
				 SPI_tuptable->tupdesc,
				 1)) + 1;

	/* release SPI related resources (and return to caller's context) */
	SPI_finish();

	Assert(OidIsValid(oid));
	return oid;
}

/*
 * This function will execute the "hypo_injectHypotheticalIndex" for every
 * hypothetical index found for each relation if the isExplain flag is setup.
 */
static void
hypo_get_relation_info_hook(PlannerInfo *root,
							Oid relationObjectId,
							bool inhparent,
							RelOptInfo *rel)
{
	if (isExplain && hypo_is_enabled)
	{
		Relation	relation;

		/* Open the current relation */
		relation = table_open(relationObjectId, AccessShareLock);

		if (relation->rd_rel->relkind == RELKIND_RELATION
#if PG_VERSION_NUM >= 90300
			|| relation->rd_rel->relkind == RELKIND_MATVIEW
#endif
			)
		{
			ListCell   *lc;

			foreach(lc, hypoIndexes)
			{
				hypoIndex  *entry = (hypoIndex *) lfirst(lc);

				if (hypo_index_match_table(entry, RelationGetRelid(relation)))
				{
					/*
					 * hypothetical index found, add it to the relation's
					 * indextlist
					 */
					hypo_injectHypotheticalIndex(root, relationObjectId,
												 inhparent, rel, relation, entry);
				}
			}
		}

		/* Close the relation release the lock now */
		table_close(relation, AccessShareLock);
	}

	if (prev_get_relation_info_hook)
		prev_get_relation_info_hook(root, relationObjectId, inhparent, rel);
}

/*
 * Reset statistics.
 */
PGDLLEXPORT Datum
hypopg_reset(PG_FUNCTION_ARGS)
{
	hypo_index_reset();
	PG_RETURN_VOID();
}
