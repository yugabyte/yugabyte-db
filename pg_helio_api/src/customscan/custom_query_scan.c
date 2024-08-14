/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/customscan/custom_query_scan.c
 *
 * Base Implementation and Definitions for a custom query scan for the extension.
 * This is a scan node that holds query level data needed for its processing
 * (e.g. for text indexes, vector indexes, $let etc. )
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>
#include <utils/lsyscache.h>
#include <nodes/extensible.h>
#include <nodes/makefuncs.h>
#include <nodes/nodeFuncs.h>
#include <optimizer/pathnode.h>
#include <optimizer/optimizer.h>
#include <parser/parse_relation.h>
#include <utils/rel.h>
#include <access/detoast.h>
#include <miscadmin.h>
#include <optimizer/paths.h>
#include <access/ginblock.h>

#include "io/helio_bson_core.h"
#include "customscan/helio_custom_query_scan.h"
#include "customscan/custom_scan_registrations.h"
#include "metadata/metadata_cache.h"
#include "query/query_operator.h"
#include "catalog/pg_am.h"
#include "commands/cursor_common.h"
#include "vector/vector_planner.h"
#include "vector/vector_common.h"
#include "vector/vector_spec.h"
#include "utils/mongo_errors.h"
#include "customscan/helio_custom_scan_private.h"


/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */


/*
 * Global that tracks a given query's
 * text index state. This is only Not null
 * during the duration of a query that has a text
 * index.
 */
QueryTextIndexData *QueryTextData = NULL;

/*
 * Global that tracks a given query's
 * vector index state. This is only Not null
 * during the duration of a query that has vector
 * search.
 */
SearchQueryEvalDataWorker *VectorEvaluationData = NULL;

/*
 * This is the input state that is per query.
 * Any pertinent information that is associated with
 * this query goes here.
 * Note Any changes to this data structure also need to
 * be replicated in CopyNodeInputQueryState.
 */
typedef struct InputQueryState
{
	/* Must be the first field */
	ExtensibleNode extensible;

	/* Text/Vector index options */
	union
	{
		QueryTextIndexData queryTextData;
		SearchQueryEvalData querySearchData;
	};

	bool hasQueryTextData;
	bool hasVectorSearchData;
} InputQueryState;


/*
 * The custom Scan State for the HelioApiQueryScan.
 */
typedef struct ExtensionQueryScanState
{
	/* must be first field */
	CustomScanState custom_scanstate;

	/* The execution state of the inner path */
	ScanState *innerScanState;

	/* The planning state of the inner path */
	Plan *innerPlan;

	/* The immutable state for this query */
	InputQueryState *inputState;

	/* The evaluation data to be set to the global state
	 * on a per tuple evaluation basis.
	 */
	SearchQueryEvalDataWorker *vectorEvaluationDataPrivate;
} ExtensionQueryScanState;

/* Name needed for Postgres to register a custom scan */
#define InputContinuationNodeName "ExtensionQueryScanInput"

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static Plan * ExtensionQueryScanPlanCustomPath(PlannerInfo *root,
											   RelOptInfo *rel,
											   struct CustomPath *best_path,
											   List *tlist,
											   List *clauses,
											   List *custom_plans);
static Node * ExtensionQueryScanCreateCustomScanState(CustomScan *cscan);
static void ExtensionQueryScanBeginCustomScan(CustomScanState *node, EState *estate,
											  int eflags);
static TupleTableSlot * ExtensionQueryScanExecCustomScan(CustomScanState *node);
static void ExtensionQueryScanEndCustomScan(CustomScanState *node);
static void ExtensionQueryScanReScanCustomScan(CustomScanState *node);
static void ExtensionQueryScanExplainCustomScan(CustomScanState *node, List *ancestors,
												ExplainState *es);

static void CopyNodeInputQueryState(ExtensibleNode *target_node, const
									ExtensibleNode *source_node);
static void OutInputQueryScanNode(StringInfo str, const struct ExtensibleNode *raw_node);
static void ReadUnsupportedExtensionQueryScanNode(struct ExtensibleNode *node);
static bool EqualUnsupportedExtensionQueryScanNode(const struct ExtensibleNode *a,
												   const struct ExtensibleNode *b);
static List * AddCustomPathCore(List *pathList, InputQueryState *queryState);
static TupleTableSlot * ExtensionQueryScanNext(CustomScanState *node);
static bool ExtensionQueryScanNextRecheck(ScanState *state, TupleTableSlot *slot);


static List * AddCustomPathForVectorCore(PlannerInfo *info, List *pathList,
										 RelOptInfo *rel,
										 InputQueryState *queryState, bool
										 failIfNotFound);


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

/* Declaration of extensibility paths for query processing (See extensible.h) */
static const struct CustomPathMethods ExtensionQueryScanPathMethods = {
	.CustomName = "HelioApiQueryScan",
	.PlanCustomPath = ExtensionQueryScanPlanCustomPath,
};

static const struct CustomScanMethods ExtensionQueryScanMethods = {
	.CustomName = "HelioApiQueryScan",
	.CreateCustomScanState = ExtensionQueryScanCreateCustomScanState
};

static const struct CustomExecMethods ExtensionQueryScanExecuteMethods = {
	.CustomName = "HelioApiQueryScan",
	.BeginCustomScan = ExtensionQueryScanBeginCustomScan,
	.ExecCustomScan = ExtensionQueryScanExecCustomScan,
	.EndCustomScan = ExtensionQueryScanEndCustomScan,
	.ReScanCustomScan = ExtensionQueryScanReScanCustomScan,
	.ExplainCustomScan = ExtensionQueryScanExplainCustomScan,
};

static const ExtensibleNodeMethods InputQueryStateMethods =
{
	InputContinuationNodeName,
	sizeof(InputQueryState),
	CopyNodeInputQueryState,
	EqualUnsupportedExtensionQueryScanNode,
	OutInputQueryScanNode,
	ReadUnsupportedExtensionQueryScanNode
};


/*
 * Registers any custom nodes that the extension Scan produces.
 * This is for any items present in the custom_private field.
 */
void
RegisterQueryScanNodes(void)
{
	RegisterExtensibleNodeMethods(&InputQueryStateMethods);
}


/*
 * Registers a Custom Path Scan for a Vector search query.
 * TODO: Add any Vector search information here.
 */
void
AddExtensionQueryScanForVectorQuery(PlannerInfo *root, RelOptInfo *rel,
									RangeTblEntry *rte,
									const SearchQueryEvalData *searchQueryData)
{
	InputQueryState *inputState = palloc0(sizeof(InputQueryState));
	inputState->querySearchData = *searchQueryData;
	inputState->hasVectorSearchData = true;

	if (EnableVectorPreFilter)
	{
		bool failIfNotFound = true;
		rel->pathlist = AddCustomPathForVectorCore(root, rel->pathlist, rel, inputState,
												   failIfNotFound);

		if (rel->partial_pathlist != NIL)
		{
			failIfNotFound = false;
			rel->partial_pathlist = AddCustomPathForVectorCore(root,
															   rel->partial_pathlist, rel,
															   inputState,
															   failIfNotFound);
		}
	}
	else
	{
		TrySetDefaultSearchParamForCustomScan(&(inputState->querySearchData));
		rel->pathlist = AddCustomPathCore(rel->pathlist, inputState);
		rel->partial_pathlist = AddCustomPathCore(rel->partial_pathlist, inputState);
	}
}


/*
 * Registers a Custom Path Scan for a Text search query.
 * This tracks the Query runtime data so that it can be pushed to
 * each worker executing.
 */
void
AddExtensionQueryScanForTextQuery(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte,
								  QueryTextIndexData *queryTextIndexData)
{
	Assert(queryTextIndexData != NULL);
	InputQueryState *inputState = palloc0(sizeof(InputQueryState));
	inputState->queryTextData = *queryTextIndexData;
	inputState->hasQueryTextData = true;
	rel->pathlist = AddCustomPathCore(rel->pathlist, inputState);
	rel->partial_pathlist = AddCustomPathCore(rel->partial_pathlist, inputState);
}


/* --------------------------------------------------------- */
/* Helper methods exports */
/* --------------------------------------------------------- */


/*
 * Helper method that walks all paths in the rel's pathlist
 * and adds a custom path wrapper that contains the queryState.
 */
static List *
AddCustomPathCore(List *pathList, InputQueryState *queryState)
{
	List *customPlanPaths = NIL;
	ListCell *cell;

	foreach(cell, pathList)
	{
		Path *inputPath = lfirst(cell);

		/* wrap the path in a custom path */
		CustomPath *customPath = makeNode(CustomPath);
		customPath->methods = &ExtensionQueryScanPathMethods;

		Path *path = &customPath->path;
		path->pathtype = T_CustomScan;

		/* copy the parameters from the inner path */
		path->parent = inputPath->parent;

		/* we don't support lateral joins here so required outer is 0 */
		path->param_info = NULL;

		/* Copy scalar values in from the inner path */
		path->rows = inputPath->rows;
		path->startup_cost = inputPath->startup_cost;
		path->total_cost = inputPath->total_cost;

		/* For now the custom path is as parallel safe as its inner path */
		path->parallel_safe = inputPath->parallel_safe;

		/* move the 'projection' from the path to the custom path. */
		path->pathtarget = inputPath->pathtarget;
		customPath->custom_paths = list_make1(inputPath);
		customPath->path.pathkeys = inputPath->pathkeys;

#if (PG_VERSION_NUM >= 150000)

		/* necessary to avoid extra Result node in PG15 */
		customPath->flags = CUSTOMPATH_SUPPORT_PROJECTION;
#endif

		/* store the continuation data */
		queryState->extensible.type = T_ExtensibleNode;
		queryState->extensible.extnodename = InputContinuationNodeName;

		/* Store the input state to be used later.
		 * NOTE: Anything added here must be of type ExtensibleNode and must be registered
		 * with the RegisterNodes method below.
		 */
		customPath->custom_private = list_make1(queryState);
		customPlanPaths = lappend(customPlanPaths, customPath);
	}

	return customPlanPaths;
}


/*
 * Helper method that walks all paths in the rel's pathlist for vector search with pre-filter
 * and checks if the given user specified filter path is matching with any of the index paths.
 */
static List *
AddCustomPathForVectorCore(PlannerInfo *planner, List *pathList, RelOptInfo *rel,
						   InputQueryState *queryState,
						   bool failIfNotFound)
{
	ListCell *cell;

	Path *vectorSearchPath = NULL;

	/* Find the vector search path */
	foreach(cell, pathList)
	{
		Path *inputPath = lfirst(cell);

		if (IsA(inputPath, IndexPath))
		{
			IndexPath *indexPath = (IndexPath *) inputPath;

			const VectorIndexDefinition *indexDef = GetVectorIndexDefinitionByIndexAmOid(
				indexPath->indexinfo->relam);
			if (indexDef != NULL)
			{
				vectorSearchPath = inputPath;
				break;
			}
		}
	}

	if (vectorSearchPath == NULL)
	{
		if (!failIfNotFound)
		{
			return NIL;
		}

		ereport(ERROR, (errcode(MongoBadValue),
						errmsg(
							"Similarity index was not found for a vector similarity search query during planning.")));
	}

	/* Need to figure out default params */
	pgbson *searchBson = NULL;
	if (queryState->querySearchData.SearchParamBson != (Datum) 0)
	{
		searchBson = DatumGetPgBson(queryState->querySearchData.SearchParamBson);
	}

	if (searchBson == NULL || IsPgbsonEmptyDocument(searchBson))
	{
		IndexPath *indexPath = (IndexPath *) vectorSearchPath;

		pgbson *defaultSearchParam = CalculateSearchParamBsonForIndexPath(indexPath);

		queryState->querySearchData.SearchParamBson = PointerGetDatum(defaultSearchParam);
	}

	/* wrap the path in a custom path */
	CustomPath *customPath = makeNode(CustomPath);
	customPath->methods = &ExtensionQueryScanPathMethods;

	Path *path = &customPath->path;
	path->pathtype = T_CustomScan;

	/* copy the parameters from the inner path */
	path->parent = vectorSearchPath->parent;

	/* we don't support lateral joins here so required outer is 0 */
	path->param_info = NULL;

	/* Copy scalar values in from the inner path */
	path->rows = vectorSearchPath->rows;
	path->startup_cost = vectorSearchPath->startup_cost;
	path->total_cost = vectorSearchPath->total_cost;

	/* For now the custom path is as parallel safe as its inner path */
	path->parallel_safe = vectorSearchPath->parallel_safe;

	/* move the 'projection' from the path to the custom path. */
	path->pathtarget = vectorSearchPath->pathtarget;

	/* custom scan inner paths */
	customPath->custom_paths = list_make1(vectorSearchPath);

	customPath->path.pathkeys = vectorSearchPath->pathkeys;

#if (PG_VERSION_NUM >= 150000)

	/* necessary to avoid extra Result node in PG15 */
	customPath->flags = CUSTOMPATH_SUPPORT_PROJECTION;
#endif

	/* store the continuation data */
	queryState->extensible.type = T_ExtensibleNode;
	queryState->extensible.extnodename = InputContinuationNodeName;

	/* Store the input state to be used later.
	 * NOTE: Anything added here must be of type ExtensibleNode and must be registered
	 * with the RegisterNodes method below.
	 */
	customPath->custom_private = list_make1(queryState);

	return list_make1(customPath);
}


/*
 * Given a scan path for the extension path, generates a
 * Custom Plan for the path. Note that the inner path
 * is already planned since it is listed as an inner_path
 * in the custom path above.
 */
static Plan *
ExtensionQueryScanPlanCustomPath(PlannerInfo *root,
								 RelOptInfo *rel,
								 struct CustomPath *best_path,
								 List *tlist,
								 List *clauses,
								 List *custom_plans)
{
	CustomScan *cscan = makeNode(CustomScan);

	/* Initialize and copy necessary data */
	cscan->methods = &ExtensionQueryScanMethods;

	/* The first item is the continuation - we propagate it forward */
	cscan->custom_private = best_path->custom_private;
	cscan->custom_plans = custom_plans;

	/* There should only be 1 plan here */
	Assert(list_length(custom_plans) == 1);

	/* The main plan comes in first */
	Plan *nestedPlan = linitial(custom_plans);

	/* Push the projection down to the inner plan */
	if (tlist != NIL)
	{
		cscan->scan.plan.targetlist = tlist;
	}
	else
	{
		/* Just project stuff from the inner scan */
		List *outerList = NIL;
		ListCell *cell;
		foreach(cell, nestedPlan->targetlist)
		{
			TargetEntry *entry = lfirst(cell);
			Var *var = makeVarFromTargetEntry(1, entry);
			outerList = lappend(outerList, makeTargetEntry((Expr *) var, entry->resno,
														   entry->resname,
														   entry->resjunk));
		}

		cscan->scan.plan.targetlist = outerList;
	}

	/* This is the input to the custom scan */
	cscan->custom_scan_tlist = nestedPlan->targetlist;

#if (PG_VERSION_NUM >= 150000)

	/* necessary to avoid extra Result node in PG15 */
	cscan->flags = CUSTOMPATH_SUPPORT_PROJECTION;
#endif

	return (Plan *) cscan;
}


/*
 * Given a custom scan generated during the plan phase
 * Creates a Custom ScanState that is used during the
 * execution of the plan.
 * This is called at the beginning of query execution
 * by the executor.
 */
static Node *
ExtensionQueryScanCreateCustomScanState(CustomScan *cscan)
{
	ExtensionQueryScanState *queryScanState = (ExtensionQueryScanState *) newNode(
		sizeof(ExtensionQueryScanState), T_CustomScanState);

	CustomScanState *cscanstate = &queryScanState->custom_scanstate;
	cscanstate->methods = &ExtensionQueryScanExecuteMethods;
	cscanstate->custom_ps = NIL;

	/* Here we don't store the custom plan inside the custom_ps of the custom scan state yet
	 * This is done as part of BeginCustomScan */
	Plan *innerPlan = (Plan *) linitial(cscan->custom_plans);
	queryScanState->innerPlan = innerPlan;

	queryScanState->inputState = (InputQueryState *) linitial(cscan->custom_private);
	return (Node *) cscanstate;
}


static void
ExtensionQueryScanBeginCustomScan(CustomScanState *node, EState *estate,
								  int eflags)
{
	/* Initialize the actual state of the plan */
	ExtensionQueryScanState *queryScanState = (ExtensionQueryScanState *) node;

	/* Add any custom per query level stuff here (Setting probes, details for $project)
	 * Remember to clean this up in EndCustomScan.
	 */
	if (queryScanState->inputState->hasQueryTextData)
	{
		QueryTextData = &queryScanState->inputState->queryTextData;
	}

	if (queryScanState->inputState->hasVectorSearchData)
	{
		SearchQueryEvalDataWorker *evalData = palloc0(sizeof(SearchQueryEvalDataWorker));
		evalData->SimilaritySearchOpOid =
			queryScanState->inputState->querySearchData.SimilaritySearchOpOid;
		evalData->SimilarityFuncInfo = CreateFCInfoForScoreCalculation(
			&queryScanState->inputState->querySearchData);
		evalData->VectorPathName = TextDatumGetCString(
			queryScanState->inputState->querySearchData.VectorPathName);

		queryScanState->vectorEvaluationDataPrivate = evalData;

		pgbson *searchParamBson = (pgbson *) DatumGetPointer(
			queryScanState->inputState->querySearchData.SearchParamBson);

		if (searchParamBson != NULL)
		{
			SetSearchParametersToGUC(
				queryScanState->inputState->querySearchData.VectorAccessMethodOid,
				searchParamBson);
		}
	}

	queryScanState->innerScanState = (ScanState *) ExecInitNode(
		queryScanState->innerPlan, estate, eflags);

	/* Store the inner state here so that EXPLAIN works */
	queryScanState->custom_scanstate.custom_ps = list_make1(
		queryScanState->innerScanState);
}


static TupleTableSlot *
ExtensionQueryScanExecCustomScan(CustomScanState *pstate)
{
	ExtensionQueryScanState *node = (ExtensionQueryScanState *) pstate;

	/* Set the vector evaluation data */
	VectorEvaluationData = node->vectorEvaluationDataPrivate;

	/*
	 * Call ExecScan with the next/recheck methods. This handles
	 * Post-processing for projections, custom filters etc.
	 */
	TupleTableSlot *returnSlot = ExecScan(&node->custom_scanstate.ss,
										  (ExecScanAccessMtd) ExtensionQueryScanNext,
										  (ExecScanRecheckMtd)
										  ExtensionQueryScanNextRecheck);

	/* Clean up the vector evaluation data */
	VectorEvaluationData = NULL;

	return returnSlot;
}


static TupleTableSlot *
ExtensionQueryScanNext(CustomScanState *node)
{
	ExtensionQueryScanState *extensionScanState = (ExtensionQueryScanState *) node;

	/* Fetch a tuple from the underlying scan */
	TupleTableSlot *slot = extensionScanState->innerScanState->ps.ExecProcNode(
		(PlanState *) extensionScanState->innerScanState);

	/* We're done scanning, so return NULL */
	if (TupIsNull(slot))
	{
		return slot;
	}

	/* Copy the slot onto our own query state for projection */
	TupleTableSlot *ourSlot = node->ss.ss_ScanTupleSlot;
	return ExecCopySlot(ourSlot, slot);
}


static bool
ExtensionQueryScanNextRecheck(ScanState *state, TupleTableSlot *slot)
{
	ereport(ERROR, (errmsg("Recheck is unexpected on Custom Scan")));
}


static void
ExtensionQueryScanEndCustomScan(CustomScanState *node)
{
	ExtensionQueryScanState *queryScanState = (ExtensionQueryScanState *) node;

	/* reset any scanstate state here */
	QueryTextData = NULL;

	if (queryScanState->vectorEvaluationDataPrivate != NULL)
	{
		pfree(queryScanState->vectorEvaluationDataPrivate->SimilarityFuncInfo);
		pfree(queryScanState->vectorEvaluationDataPrivate->VectorPathName);
		pfree(queryScanState->vectorEvaluationDataPrivate);
	}

	ExecEndNode((PlanState *) queryScanState->innerScanState);
}


static void
ExtensionQueryScanReScanCustomScan(CustomScanState *node)
{
	ExtensionQueryScanState *queryScanState = (ExtensionQueryScanState *) node;

	/* reset any scanstate state here */
	ExecReScan((PlanState *) queryScanState->innerScanState);
}


static void
ExtensionQueryScanExplainCustomScan(CustomScanState *node, List *ancestors,
									ExplainState *es)
{
	/* Add any scan related information here */
	/* show the ivfflat probes that were used. */
	ExtensionQueryScanState *queryScanState = (ExtensionQueryScanState *) node;
	if (queryScanState->inputState->hasVectorSearchData &&
		(pgbson *) queryScanState->inputState->querySearchData.SearchParamBson != NULL)
	{
		ExplainPropertyText("CosmosSearch Custom Params", PgbsonToLegacyJson(
								(pgbson *) queryScanState->inputState->querySearchData.
								SearchParamBson), es);
	}
}


/*
 * Support for comparing two Scan extensible nodes
 * Currently insupported.
 */
static bool
EqualUnsupportedExtensionQueryScanNode(const struct ExtensibleNode *a,
									   const struct ExtensibleNode *b)
{
	ereport(ERROR, (errmsg("Equal for node type CustomQueryScan not implemented")));
}


/*
 * Support for Copying the InputQueryState node
 */
static void
CopyNodeInputQueryState(struct ExtensibleNode *target_node, const struct
						ExtensibleNode *source_node)
{
	InputQueryState *from = (InputQueryState *) source_node;

	InputQueryState *newNode = (InputQueryState *) target_node;
	newNode->extensible.type = T_ExtensibleNode;
	newNode->extensible.extnodename = InputContinuationNodeName;
	newNode->hasQueryTextData = from->hasQueryTextData;
	newNode->hasVectorSearchData = from->hasVectorSearchData;
	if (from->hasQueryTextData)
	{
		newNode->queryTextData.indexOptions = pg_detoast_datum_copy(
			from->queryTextData.indexOptions);
		newNode->queryTextData.query = PointerGetDatum(DatumGetTSQueryCopy(
														   from->queryTextData.query));
	}

	if (from->hasVectorSearchData)
	{
		newNode->querySearchData.SimilaritySearchOpOid =
			from->querySearchData.SimilaritySearchOpOid;
		newNode->querySearchData.QueryVector = PointerGetDatum(PG_DETOAST_DATUM_COPY(
																   from->querySearchData.
																   QueryVector));
		newNode->querySearchData.VectorPathName = PointerGetDatum(PG_DETOAST_DATUM_COPY(
																	  from->
																	  querySearchData.
																	  VectorPathName));
		newNode->querySearchData.VectorAccessMethodOid =
			from->querySearchData.VectorAccessMethodOid;

		if (from->querySearchData.SearchParamBson != 0)
		{
			newNode->querySearchData.SearchParamBson = PointerGetDatum(
				PG_DETOAST_DATUM_COPY(from->querySearchData.SearchParamBson));
		}
	}
}


/*
 * Support for Outputing the InputContinuation node
 */
static void
OutInputQueryScanNode(StringInfo str, const struct ExtensibleNode *raw_node)
{
	/* TODO: This doesn't seem needed */
}


/*
 * Function for reading HelioApiQueryScan node (unsupported)
 */
static void
ReadUnsupportedExtensionQueryScanNode(struct ExtensibleNode *node)
{
	ereport(ERROR, (errmsg("Read for node type CustomQueryScan not implemented")));
}
