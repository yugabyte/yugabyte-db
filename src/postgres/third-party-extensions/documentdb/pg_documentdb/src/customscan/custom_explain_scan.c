/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/customscan/custom_explain_scan.c
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
#include <access/relscan.h>
#include <optimizer/tlist.h>

#if PG_VERSION_NUM >= 180000
#include <commands/explain_format.h>
#endif

#include "io/bson_core.h"
#include "planner/documentdb_planner.h"
#include "customscan/custom_scan_registrations.h"
#include "metadata/metadata_cache.h"
#include "query/query_operator.h"
#include "catalog/pg_am.h"
#include "commands/cursor_common.h"
#include "utils/documentdb_errors.h"
#include "customscan/bson_custom_query_scan.h"
#include "index_am/index_am_utils.h"
#include "index_am/documentdb_rum.h"


/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */


typedef struct ExplainInputQueryState
{
	/* Must be the first field */
	ExtensibleNode extensible;
} ExplainInputQueryState;


/*
 * The custom Scan State for the DocumentDBApiQueryScan.
 */
typedef struct ExplainQueryScanState
{
	/* must be first field */
	CustomScanState custom_scanstate;

	/* The execution state of the inner path */
	ScanState *innerScanState;

	/* The planning state of the inner path */
	Plan *innerPlan;

	ExplainInputQueryState *inputQueryState;
} ExplainQueryScanState;

/* Name needed for Postgres to register a custom scan */
#define InputContinuationNodeName "DocumentsExplainQueryScanInput"

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static Plan * ExplainQueryScanPlanCustomPath(PlannerInfo *root,
											 RelOptInfo *rel,
											 struct CustomPath *best_path,
											 List *tlist,
											 List *clauses,
											 List *custom_plans);
static Node * ExplainQueryScanCreateCustomScanState(CustomScan *cscan);
static void ExplainQueryScanBeginCustomScan(CustomScanState *node, EState *estate,
											int eflags);
static TupleTableSlot * ExplainQueryScanExecCustomScan(CustomScanState *node);
static void ExplainQueryScanEndCustomScan(CustomScanState *node);
static void ExplainQueryScanReScanCustomScan(CustomScanState *node);
static void ExplainQueryScanExplainCustomScan(CustomScanState *node, List *ancestors,
											  ExplainState *es);

static void CopyNodeInputQueryState(ExtensibleNode *target_node, const
									ExtensibleNode *source_node);
static void OutInputQueryScanNode(StringInfo str, const struct ExtensibleNode *raw_node);
static void ReadUnsupportedExtensionQueryScanNode(struct ExtensibleNode *node);
static bool EqualUnsupportedExtensionQueryScanNode(const struct ExtensibleNode *a,
												   const struct ExtensibleNode *b);
static TupleTableSlot * ExplainQueryScanNext(CustomScanState *node);
static bool ExplainQueryScanNextRecheck(ScanState *state, TupleTableSlot *slot);
static List * AddExplainCustomPathCore(List *pathList);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

/* Declaration of extensibility paths for query processing (See extensible.h) */
static const struct CustomPathMethods ExplainQueryScanPathMethods = {
	.CustomName = "DocumentDBApiExplainQueryScan",
	.PlanCustomPath = ExplainQueryScanPlanCustomPath,
};

static const struct CustomScanMethods ExplainQueryScanMethods = {
	.CustomName = "DocumentDBApiExplainQueryScan",
	.CreateCustomScanState = ExplainQueryScanCreateCustomScanState
};

static const struct CustomExecMethods ExplainQueryScanExecuteMethods = {
	.CustomName = "DocumentDBApiExplainQueryScan",
	.BeginCustomScan = ExplainQueryScanBeginCustomScan,
	.ExecCustomScan = ExplainQueryScanExecCustomScan,
	.EndCustomScan = ExplainQueryScanEndCustomScan,
	.ReScanCustomScan = ExplainQueryScanReScanCustomScan,
	.ExplainCustomScan = ExplainQueryScanExplainCustomScan,
};


static const ExtensibleNodeMethods InputQueryStateMethods =
{
	InputContinuationNodeName,
	sizeof(ExplainInputQueryState),
	CopyNodeInputQueryState,
	EqualUnsupportedExtensionQueryScanNode,
	OutInputQueryScanNode,
	ReadUnsupportedExtensionQueryScanNode
};

extern bool EnableExtendedExplainOnAnalyzeOff;


/*
 * Registers any custom nodes that the extension Scan produces.
 * This is for any items present in the custom_private field.
 */
void
RegisterExplainScanNodes(void)
{
	RegisterExtensibleNodeMethods(&InputQueryStateMethods);
	RegisterCustomScanMethods(&ExplainQueryScanMethods);
}


void
AddExplainCustomScanWrapper(PlannerInfo *root, RelOptInfo *rel,
							RangeTblEntry *rte)
{
	rel->pathlist = AddExplainCustomPathCore(rel->pathlist);
}


/* --------------------------------------------------------- */
/* Helper methods exports */
/* --------------------------------------------------------- */


/*
 * Helper method that walks all paths in the rel's pathlist
 * and adds a custom path wrapper that contains the queryState.
 */
static List *
AddExplainCustomPathCore(List *pathList)
{
	List *customPlanPaths = NIL;
	ListCell *cell;

	foreach(cell, pathList)
	{
		bool isValidPath = false;
		Path *inputPath = lfirst(cell);

		if (inputPath->pathtype == T_IndexScan ||
			inputPath->pathtype == T_IndexOnlyScan)
		{
			IndexPath *indexPath = (IndexPath *) inputPath;
			isValidPath = IsBsonRegularIndexAm(indexPath->indexinfo->relam);
		}
		else if (inputPath->pathtype == T_BitmapHeapScan)
		{
			BitmapHeapPath *bitmapHeapPath = (BitmapHeapPath *) inputPath;
			if (bitmapHeapPath->bitmapqual->pathtype == T_IndexScan)
			{
				IndexPath *indexPath = (IndexPath *) bitmapHeapPath->bitmapqual;
				isValidPath = IsBsonRegularIndexAm(indexPath->indexinfo->relam);
			}
			else if (bitmapHeapPath->bitmapqual->pathtype == T_BitmapAnd)
			{
				/* BitmapAnd is valid if all its children are valid */
				BitmapAndPath *bitmapAndPath =
					(BitmapAndPath *) bitmapHeapPath->bitmapqual;
				ListCell *bitmapCell;
				isValidPath = true;
				foreach(bitmapCell, bitmapAndPath->bitmapquals)
				{
					Path *childPath = (Path *) lfirst(bitmapCell);
					if (childPath->pathtype != T_IndexScan ||
						!IsBsonRegularIndexAm(
							((IndexPath *) childPath)->indexinfo->relam))
					{
						isValidPath = false;
						break;
					}
				}
			}
			else if (bitmapHeapPath->bitmapqual->pathtype == T_BitmapOr)
			{
				/* BitmapOr is valid if all its children are valid */
				BitmapOrPath *bitmapOrPath = (BitmapOrPath *) bitmapHeapPath->bitmapqual;
				ListCell *bitmapCell;
				isValidPath = true;
				foreach(bitmapCell, bitmapOrPath->bitmapquals)
				{
					Path *childPath = (Path *) lfirst(bitmapCell);
					if (childPath->pathtype != T_IndexScan ||
						!IsBsonRegularIndexAm(
							((IndexPath *) childPath)->indexinfo->relam))
					{
						isValidPath = false;
						break;
					}
				}
			}
		}
		else
		{
			/* we only wrap IndexScan and BitmapHeapScan */
			isValidPath = false;
		}

		if (inputPath->param_info != NULL)
		{
			isValidPath = false;
		}

		if (!isValidPath)
		{
			customPlanPaths = lappend(customPlanPaths, inputPath);
			continue;
		}

		/* wrap the path in a custom path */
		CustomPath *customPath = makeNode(CustomPath);
		customPath->methods = &ExplainQueryScanPathMethods;

		ExplainInputQueryState *queryState = palloc0(sizeof(ExplainInputQueryState));

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

		/* Copy the param paths */
		path->param_info = inputPath->param_info;
		customPath->custom_paths = list_make1(inputPath);
		customPath->path.pathkeys = inputPath->pathkeys;

#if (PG_VERSION_NUM >= 150000)

		/* necessary to avoid extra Result node in PG15 */
		customPath->flags = CUSTOMPATH_SUPPORT_PROJECTION;
#endif

		/* Save the continuation data into storage */
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
 * Given a scan path for the extension path, generates a
 * Custom Plan for the path. Note that the inner path
 * is already planned since it is listed as an inner_path
 * in the custom path above.
 */
static Plan *
ExplainQueryScanPlanCustomPath(PlannerInfo *root,
							   RelOptInfo *rel,
							   struct CustomPath *best_path,
							   List *tlist,
							   List *clauses,
							   List *custom_plans)
{
	CustomScan *cscan = makeNode(CustomScan);

	/* Initialize and copy necessary data */
	cscan->methods = &ExplainQueryScanMethods;

	/* The first item is the continuation - we propagate it forward */
	cscan->custom_private = best_path->custom_private;
	cscan->custom_plans = custom_plans;

	/* Only one plan is allowed here */
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
ExplainQueryScanCreateCustomScanState(CustomScan *cscan)
{
	ExplainQueryScanState *queryScanState = (ExplainQueryScanState *) newNode(
		sizeof(ExplainQueryScanState), T_CustomScanState);

	CustomScanState *cscanstate = &queryScanState->custom_scanstate;
	cscanstate->methods = &ExplainQueryScanExecuteMethods;
	cscanstate->custom_ps = NIL;

	/* Here we don't store the custom plan inside the custom_ps of the custom scan state yet
	 * This is done as part of BeginCustomScan */
	Plan *innerPlan = (Plan *) linitial(cscan->custom_plans);
	queryScanState->innerPlan = innerPlan;

	queryScanState->inputQueryState = (ExplainInputQueryState *) linitial(
		cscan->custom_private);
	return (Node *) cscanstate;
}


static void
ExplainQueryScanBeginCustomScan(CustomScanState *node, EState *estate,
								int eflags)
{
	/* Initialize the current state of the plan */
	ExplainQueryScanState *queryScanState = (ExplainQueryScanState *) node;

	queryScanState->innerScanState = (ScanState *) ExecInitNode(
		queryScanState->innerPlan, estate, eflags);

	/* Store the inner state here so that EXPLAIN works */
	queryScanState->custom_scanstate.custom_ps = list_make1(
		queryScanState->innerScanState);
}


static TupleTableSlot *
ExplainQueryScanExecCustomScan(CustomScanState *pstate)
{
	ExplainQueryScanState *node = (ExplainQueryScanState *) pstate;

	/*
	 * Call ExecScan with the next/recheck methods. This handles
	 * Post-processing for projections, custom filters etc.
	 */
	TupleTableSlot *returnSlot = ExecScan(&node->custom_scanstate.ss,
										  (ExecScanAccessMtd) ExplainQueryScanNext,
										  (ExecScanRecheckMtd)
										  ExplainQueryScanNextRecheck);

	return returnSlot;
}


static TupleTableSlot *
ExplainQueryScanNext(CustomScanState *node)
{
	ExplainQueryScanState *extensionScanState = (ExplainQueryScanState *) node;

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
ExplainQueryScanNextRecheck(ScanState *state, TupleTableSlot *slot)
{
	ereport(ERROR, (errmsg("Recheck is unexpected on Custom Scan")));
}


static void
ExplainQueryScanEndCustomScan(CustomScanState *node)
{
	ExplainQueryScanState *queryScanState = (ExplainQueryScanState *) node;
	ExecEndNode((PlanState *) queryScanState->innerScanState);
}


static void
ExplainQueryScanReScanCustomScan(CustomScanState *node)
{
	ExplainQueryScanState *queryScanState = (ExplainQueryScanState *) node;

	/* reset any scanstate state here */
	ExecReScan((PlanState *) queryScanState->innerScanState);
}


static void
ExplainIndexScanState(IndexScanDesc indexScan, Oid indexOid, List *indexQuals,
					  List *indexOrderBy, ScanDirection indexScanDir, ExplainState *es)
{
	ExplainOpenGroup("index_top_level", NULL, true, es);
	const char *indexName = ExtensionExplainGetIndexName(indexOid);
	ExplainPropertyText("indexName", indexName, es);
	if (indexScan == NULL)
	{
		if (EnableExtendedExplainOnAnalyzeOff)
		{
			/* Explain without analyze, try to explain based on the quals and orderby */
			Relation index_rel = index_open(indexOid, NoLock);
			if (IsCompositeOpClass(index_rel))
			{
				ExplainRawCompositeScan(index_rel, indexQuals, indexOrderBy, indexScanDir,
										es);
			}
			index_close(index_rel, NoLock);
		}
	}
	else
	{
		/* Add any index scan related information here */
		if (IsCompositeOpClass(indexScan->indexRelation))
		{
			ExplainCompositeScan(indexScan, es);
		}
		else if (IsBsonRegularIndexAm(indexScan->indexRelation->rd_rel->relam))
		{
			/* Do stuff to explain regular index am */
			ExplainRegularIndexScan(indexScan, es);
		}
	}

	ExplainCloseGroup("index_top_level", NULL, true, es);
}


static void
WalkAndExplainScanState(PlanState *scanState, ExplainState *es)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();
	if (IsA(scanState, IndexScanState))
	{
		IndexScanState *indexScanState = (IndexScanState *) scanState;
		IndexScan *scan = (IndexScan *) indexScanState->ss.ps.plan;
		ExplainIndexScanState(indexScanState->iss_ScanDesc, scan->indexid,
							  scan->indexqual, scan->indexorderby, scan->indexorderdir,
							  es);
	}
	else if (IsA(scanState, IndexOnlyScanState))
	{
		IndexOnlyScanState *indexOnlyScanState = (IndexOnlyScanState *) scanState;
		IndexOnlyScan *scan = (IndexOnlyScan *) indexOnlyScanState->ss.ps.plan;
		ExplainIndexScanState(indexOnlyScanState->ioss_ScanDesc, scan->indexid,
							  scan->indexqual, scan->indexorderby, scan->indexorderdir,
							  es);
	}
	else if (IsA(scanState, BitmapIndexScanState))
	{
		BitmapIndexScanState *bitmapIndexScanState = (BitmapIndexScanState *) scanState;
		BitmapIndexScan *scan = (BitmapIndexScan *) bitmapIndexScanState->ss.ps.plan;
		List *indexOrderBy = NIL;
		ExplainIndexScanState(bitmapIndexScanState->biss_ScanDesc, scan->indexid,
							  scan->indexqual, indexOrderBy, NoMovementScanDirection, es);
	}
	else if (IsA(scanState, BitmapAndState))
	{
		BitmapAndState *bitmapAndState = (BitmapAndState *) scanState;
		for (int i = 0; i < bitmapAndState->nplans; i++)
		{
			WalkAndExplainScanState(bitmapAndState->bitmapplans[i], es);
		}
	}
	else if (IsA(scanState, BitmapOrState))
	{
		BitmapOrState *bitmapOrState = (BitmapOrState *) scanState;
		for (int i = 0; i < bitmapOrState->nplans; i++)
		{
			WalkAndExplainScanState(bitmapOrState->bitmapplans[i], es);
		}
	}

	if (scanState->lefttree != NULL)
	{
		WalkAndExplainScanState(scanState->lefttree, es);
	}
	if (scanState->righttree != NULL)
	{
		WalkAndExplainScanState(scanState->righttree, es);
	}
}


static void
ExplainQueryScanExplainCustomScan(CustomScanState *node, List *ancestors,
								  ExplainState *es)
{
	/* Add any scan related information here */
	ExplainQueryScanState *queryScanState = (ExplainQueryScanState *) node;

	ExplainOpenGroup("custom_scan", "IndexDetails", false, es);
	WalkAndExplainScanState(&queryScanState->innerScanState->ps, es);
	ExplainCloseGroup("custom_scan", "IndexDetails", false, es);
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
	ExplainInputQueryState *newNode = (ExplainInputQueryState *) target_node;
	newNode->extensible.type = T_ExtensibleNode;
	newNode->extensible.extnodename = InputContinuationNodeName;
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
 * Function for reading DocumentDBApiQueryScan node (unsupported)
 */
static void
ReadUnsupportedExtensionQueryScanNode(struct ExtensibleNode *node)
{
	ExplainInputQueryState *newNode = (ExplainInputQueryState *) node;
	newNode->extensible.type = T_ExtensibleNode;
	newNode->extensible.extnodename = InputContinuationNodeName;
}
