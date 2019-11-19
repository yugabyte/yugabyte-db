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
 * Copyright (C) 2015-2018: Julien Rouhaud
 *
 *-------------------------------------------------------------------------
 */


#include "postgres.h"
#include "fmgr.h"

#include "include/hypopg.h"
#include "include/hypopg_import.h"
#include "include/hypopg_index.h"

PG_MODULE_MAGIC;

/*--- Variables exported ---*/

bool		isExplain;
bool		hypo_is_enabled;
MemoryContext HypoMemoryContext;

/*--- Functions --- */

PGDLLEXPORT void _PG_init(void);
PGDLLEXPORT void _PG_fini(void);

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
							  char *completionTag);
static ProcessUtility_hook_type prev_utility_hook = NULL;

static void hypo_executorEnd_hook(QueryDesc *queryDesc);
static ExecutorEnd_hook_type prev_ExecutorEnd_hook = NULL;


static void hypo_get_relation_info_hook(PlannerInfo *root,
										Oid relationObjectId,
										bool inhparent,
										RelOptInfo *rel);
static get_relation_info_hook_type prev_get_relation_info_hook = NULL;

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

}

void
_PG_fini(void)
{
	/* uninstall hooks */
	ProcessUtility_hook = prev_utility_hook;
	ExecutorEnd_hook = prev_ExecutorEnd_hook;
	get_relation_info_hook = prev_get_relation_info_hook;
	explain_get_index_name_hook = prev_explain_get_index_name_hook;

}

/*---------------------------------
 * Wrapper around GetNewRelFileNode
 * Return a new OID for an hypothetical index.
 */
Oid
hypo_getNewOid(Oid relid)
{
	Relation	pg_class;
	Relation	relation;
	Oid			newoid;
	Oid			reltablespace;
	char		relpersistence;

	/* Open the relation on which we want a new OID */
	relation = table_open(relid, AccessShareLock);

	reltablespace = relation->rd_rel->reltablespace;
	relpersistence = relation->rd_rel->relpersistence;

	/* Close the relation and release the lock now */
	table_close(relation, AccessShareLock);

	/* Open pg_class to aks a new OID */
	pg_class = table_open(RelationRelationId, RowExclusiveLock);

	/* ask for a new relfilenode */
	newoid = GetNewRelFileNode(reltablespace, pg_class, relpersistence);

	/* Close pg_class and release the lock now */
	table_close(pg_class, RowExclusiveLock);

	return newoid;
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
				  char *completionTag)
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
						  dest, completionTag);
	else
		standard_ProcessUtility(
#if PG_VERSION_NUM >= 100000
								pstmt,
#else
								parsetree,
#endif
								queryString,
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

				if (entry->relid == relationObjectId)
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
