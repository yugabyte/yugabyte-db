/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/customscan/rum_custom_join_scan.c
 *
 * Join scan for RUM indexes for conjunction queries that optimizes the lookup
 * so that it's fast across N indexes.
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
#include <access/genam.h>

#include "io/helio_bson_core.h"
#include "customscan/helio_custom_scan.h"
#include "customscan/custom_scan_registrations.h"
#include "metadata/metadata_cache.h"
#include "query/query_operator.h"
#include "catalog/pg_am.h"
#include "commands/cursor_common.h"
#include "vector/vector_planner.h"
#include "vector/vector_common.h"
#include "utils/mongo_errors.h"


/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

/* Name needed for Postgres to register a custom scan */
#define InputContinuationNodeName "HelioRumJoinScanInput"

typedef struct RumCustomJoinInputState
{
	/* Must be the first field */
	ExtensibleNode extensible;
} RumCustomJoinInputState;


typedef struct HelioRumCustomJoinScanState
{
	/* must be first field */
	CustomScanState custom_scanstate;

	Plan *innerPlan;

	ScanState *innerScanState;

	RumCustomJoinInputState *inputState;

	bool initializedInnerTidBitmap;
} HelioRumCustomJoinScanState;

extern bool EnableMultiIndexRumJoin;
static int64 (*MultiAndGetBitmapFunc) (IndexScanDesc *, int32, TIDBitmap *) = NULL;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static Plan * ExtensionRumJoinScanPlanCustomPath(PlannerInfo *root,
												 RelOptInfo *rel,
												 struct CustomPath *best_path,
												 List *tlist,
												 List *clauses,
												 List *custom_plans);

static Node * ExtensionRumJoinScanCreateCustomScanState(CustomScan *cscan);

static void ExtensionRumJoinScanBeginCustomScan(CustomScanState *node, EState *estate,
												int eflags);
static TupleTableSlot * ExtensionRumJoinScanExecCustomScan(CustomScanState *pstate);
static TupleTableSlot * ExtensionRumJoinScanNext(CustomScanState *node);
static bool ExtensionRumJoinScanNextRecheck(ScanState *state, TupleTableSlot *slot);
static void ExtensionRumJoinScanEndCustomScan(CustomScanState *node);
static void ExtensionRumJoinScanReScanCustomScan(CustomScanState *node);
static void ExtensionRumJoinScanExplainCustomScan(CustomScanState *node, List *ancestors,
												  ExplainState *es);
static void CopyNodeRumJoinInputState(struct ExtensibleNode *target_node,
									  const struct ExtensibleNode *source_node);
static void ReadUnsupportedExtensionRumJoinScannNode(struct ExtensibleNode *node);
static void OutInputRumJoinScanNode(StringInfo str, const struct
									ExtensibleNode *raw_node);
static bool EqualUnsupportedExtensionRumJoinScanNode(const struct ExtensibleNode *a,
													 const struct ExtensibleNode *b);
static CustomPath * CreateCustomJoinPathCore(BitmapHeapPath *bitmapAndPath,
											 RelOptInfo *rel, RangeTblEntry *rte);
static void InitializeBitmapHeapScanState(BitmapHeapScanState *scanState);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

/* Declaration of extensibility paths for query processing (See extensible.h) */
static const struct CustomPathMethods ExtensionRumJoinScanPathMethods = {
	.CustomName = "HelioApiRumJoinScan",
	.PlanCustomPath = ExtensionRumJoinScanPlanCustomPath,
};

static const struct CustomScanMethods ExtensionRumJoinScanMethods = {
	.CustomName = "HelioApiRumJoinScan",
	.CreateCustomScanState = ExtensionRumJoinScanCreateCustomScanState
};

static const struct CustomExecMethods ExtensionRumJoinScanExecuteMethods = {
	.CustomName = "HelioApiRumJoinScan",
	.BeginCustomScan = ExtensionRumJoinScanBeginCustomScan,
	.ExecCustomScan = ExtensionRumJoinScanExecCustomScan,
	.EndCustomScan = ExtensionRumJoinScanEndCustomScan,
	.ReScanCustomScan = ExtensionRumJoinScanReScanCustomScan,
	.ExplainCustomScan = ExtensionRumJoinScanExplainCustomScan,
};

static const ExtensibleNodeMethods InputQueryStateMethods =
{
	InputContinuationNodeName,
	sizeof(RumCustomJoinInputState),
	CopyNodeRumJoinInputState,
	EqualUnsupportedExtensionRumJoinScanNode,
	OutInputRumJoinScanNode,
	ReadUnsupportedExtensionRumJoinScannNode
};


/*
 * Registers any custom nodes that the extension Scan produces.
 * This is for any items present in the custom_private field.
 */
void
RegisterRumJoinScanNodes(void)
{
	bool missingOk = false;
	void **ignoreLibFileHandle = NULL;
	MultiAndGetBitmapFunc =
		load_external_function("$libdir/rum", "multiandgetbitmap", !missingOk,
							   ignoreLibFileHandle);

	/* Only add the custom scan IF the function is available in the build of rum */
	if (MultiAndGetBitmapFunc != NULL)
	{
		RegisterExtensibleNodeMethods(&InputQueryStateMethods);
	}
}


/*
 * Registers a Custom Path Scan for a Bitmap And of RUM indexes
 * such that it optimizes the performance of scanning the N indexes.
 */
Path *
CreateRumJoinScanPathForBitmapAnd(PlannerInfo *root, RelOptInfo *rel,
								  RangeTblEntry *rte,
								  BitmapHeapPath *heapPath)
{
	if (EnableMultiIndexRumJoin && MultiAndGetBitmapFunc != NULL)
	{
		return (Path *) CreateCustomJoinPathCore(heapPath, rel, rte);
	}
	else
	{
		return (Path *) heapPath;
	}
}


bool
IsRumJoinScanPath(Path *path)
{
	if (!IsA(path, CustomPath))
	{
		return false;
	}

	CustomPath *customPath = (CustomPath *) path;
	return customPath->methods == &ExtensionRumJoinScanPathMethods;
}


/* --------------------------------------------------------- */
/* Helper methods exports */
/* --------------------------------------------------------- */


/*
 * Helper method that takes the Bitmap And path and creates a CustomPath
 * with the join implementation that allows for fast scan intersections.
 */
static CustomPath *
CreateCustomJoinPathCore(BitmapHeapPath *bitmapAndPath, RelOptInfo *rel,
						 RangeTblEntry *rte)
{
	/* wrap the path in a custom path */
	Path *inputPath = &bitmapAndPath->path;
	CustomPath *customPath = makeNode(CustomPath);
	customPath->methods = &ExtensionRumJoinScanPathMethods;

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

	/* Extract the base rel for the query */
	Relation tableRel = RelationIdGetRelation(rte->relid);

	/* Point the nested scan's projection to the base table's projection */
	inputPath->pathtarget = BuildBaseRelPathTarget(tableRel, rel->relid);

	/* Ensure you close the rel */
	RelationClose(tableRel);

	customPath->custom_paths = list_make1(inputPath);
	customPath->path.pathkeys = inputPath->pathkeys;

#if (PG_VERSION_NUM >= 150000)

	/* necessary to avoid extra Result node in PG15 */
	customPath->flags = CUSTOMPATH_SUPPORT_PROJECTION;
#endif

	RumCustomJoinInputState *joinState = palloc0(sizeof(RumCustomJoinInputState));
	joinState->extensible.extnodename = InputContinuationNodeName;
	joinState->extensible.type = T_ExtensibleNode;
	customPath->custom_private = list_make1(joinState);

	return customPath;
}


/*
 * Given a scan path for the extension path, generates a
 * Custom Plan for the path. Note that the inner path
 * is already planned since it is listed as an inner_path
 * in the custom path above.
 */
static Plan *
ExtensionRumJoinScanPlanCustomPath(PlannerInfo *root,
								   RelOptInfo *rel,
								   struct CustomPath *best_path,
								   List *tlist,
								   List *clauses,
								   List *custom_plans)
{
	CustomScan *cscan = makeNode(CustomScan);

	/* Initialize and copy necessary data */
	cscan->methods = &ExtensionRumJoinScanMethods;

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
ExtensionRumJoinScanCreateCustomScanState(CustomScan *cscan)
{
	HelioRumCustomJoinScanState *queryScanState = (HelioRumCustomJoinScanState *) newNode(
		sizeof(HelioRumCustomJoinScanState), T_CustomScanState);

	CustomScanState *cscanstate = &queryScanState->custom_scanstate;
	cscanstate->methods = &ExtensionRumJoinScanExecuteMethods;
	cscanstate->custom_ps = NIL;

	/* Here we don't store the custom plan inside the custom_ps of the custom scan state yet
	 * This is done as part of BeginCustomScan */
	Plan *innerPlan = (Plan *) linitial(cscan->custom_plans);
	queryScanState->innerPlan = innerPlan;

	queryScanState->inputState = (RumCustomJoinInputState *) linitial(
		cscan->custom_private);
	return (Node *) cscanstate;
}


static void
ExtensionRumJoinScanBeginCustomScan(CustomScanState *node, EState *estate,
									int eflags)
{
	/* Initialize the actual state of the plan */
	HelioRumCustomJoinScanState *queryScanState = (HelioRumCustomJoinScanState *) node;

	queryScanState->innerScanState = (ScanState *) ExecInitNode(
		queryScanState->innerPlan, estate, eflags);
	queryScanState->initializedInnerTidBitmap = false;

	/* Store the inner state here so that EXPLAIN works */
	queryScanState->custom_scanstate.custom_ps = list_make1(
		queryScanState->innerScanState);
}


static TupleTableSlot *
ExtensionRumJoinScanExecCustomScan(CustomScanState *pstate)
{
	HelioRumCustomJoinScanState *node = (HelioRumCustomJoinScanState *) pstate;

	/*
	 * Call ExecScan with the next/recheck methods. This handles
	 * Post-processing for projections, custom filters etc.
	 */
	TupleTableSlot *returnSlot = ExecScan(&node->custom_scanstate.ss,
										  (ExecScanAccessMtd) ExtensionRumJoinScanNext,
										  (ExecScanRecheckMtd)
										  ExtensionRumJoinScanNextRecheck);

	return returnSlot;
}


static TupleTableSlot *
ExtensionRumJoinScanNext(CustomScanState *node)
{
	HelioRumCustomJoinScanState *extensionScanState =
		(HelioRumCustomJoinScanState *) node;

	if (!extensionScanState->initializedInnerTidBitmap)
	{
		BitmapHeapScanState *scanStateInner =
			(BitmapHeapScanState *) extensionScanState->innerScanState;
		if (!scanStateInner->initialized)
		{
			InitializeBitmapHeapScanState(scanStateInner);
		}

		extensionScanState->initializedInnerTidBitmap = true;
	}

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
ExtensionRumJoinScanNextRecheck(ScanState *state, TupleTableSlot *slot)
{
	ereport(ERROR, (errmsg("Recheck is unexpected on Custom Scan")));
}


static void
ExtensionRumJoinScanEndCustomScan(CustomScanState *node)
{
	HelioRumCustomJoinScanState *queryScanState = (HelioRumCustomJoinScanState *) node;
	ExecEndNode((PlanState *) queryScanState->innerScanState);
}


static void
ExtensionRumJoinScanReScanCustomScan(CustomScanState *node)
{
	HelioRumCustomJoinScanState *queryScanState = (HelioRumCustomJoinScanState *) node;

	/* reset any scanstate state here */
	ExecReScan((PlanState *) queryScanState->innerScanState);
}


static void
ExtensionRumJoinScanExplainCustomScan(CustomScanState *node, List *ancestors,
									  ExplainState *es)
{ }


/*
 * Function for reading HelioApiQueryScan node (unsupported)
 */
static void
ReadUnsupportedExtensionRumJoinScannNode(struct ExtensibleNode *node)
{
	ereport(ERROR, (errmsg("Read for node type not implemented")));
}


/*
 * Support for comparing two Scan extensible nodes
 * Currently insupported.
 */
static bool
EqualUnsupportedExtensionRumJoinScanNode(const struct ExtensibleNode *a,
										 const struct ExtensibleNode *b)
{
	ereport(ERROR, (errmsg("Equal for node type not implemented")));
}


/*
 * Support for Copying the InputQueryState node
 */
static void
CopyNodeRumJoinInputState(struct ExtensibleNode *target_node, const struct
						  ExtensibleNode *source_node)
{
	/* RumCustomJoinInputState *from = (RumCustomJoinInputState *) source_node; */
	RumCustomJoinInputState *newNode = (RumCustomJoinInputState *) target_node;
	newNode->extensible.type = T_ExtensibleNode;
	newNode->extensible.extnodename = InputContinuationNodeName;
}


/*
 * Support for Outputing the InputContinuation node
 */
static void
OutInputRumJoinScanNode(StringInfo str, const struct ExtensibleNode *raw_node)
{
	/* TODO: This doesn't seem needed */
}


static TIDBitmap *
ExecuteMultiProcBitmapRumAnd(PlanState *innerPlan)
{
	TIDBitmap *tbm = tbm_create(work_mem * 1024L, NULL);

	BitmapAndState *andState = (BitmapAndState *) innerPlan;

	IndexScanDesc *scanArray = palloc(sizeof(IndexScanDesc) * andState->nplans);
	for (int i = 0; i < andState->nplans; i++)
	{
		BitmapIndexScanState *indexScanState =
			(BitmapIndexScanState *) andState->bitmapplans[i];
		scanArray[i] = indexScanState->biss_ScanDesc;
	}

	MultiAndGetBitmapFunc(scanArray, andState->nplans, tbm);

	return tbm;
}


static void
RunBitmapSerialScan(BitmapHeapScanState *scanState)
{
	TIDBitmap *tbm = ExecuteMultiProcBitmapRumAnd(outerPlanState(scanState));

	if (!tbm || !IsA(tbm, TIDBitmap))
	{
		elog(ERROR, "unrecognized result from subplan");
	}

	scanState->tbm = tbm;
	scanState->tbmiterator = tbm_begin_iterate(tbm);
	scanState->tbmres = NULL;

#ifdef USE_PREFETCH
	if (scanState->prefetch_maximum > 0)
	{
		scanState->prefetch_iterator = tbm_begin_iterate(tbm);
		scanState->prefetch_pages = 0;
		scanState->prefetch_target = -1;
	}
#endif                          /* USE_PREFETCH */
}


static void
InitializeBitmapHeapScanState(BitmapHeapScanState *scanState)
{
	if (scanState->pstate)
	{
		/* TODO: in planner.c also scan partial_paths to support parallel scans
		 * How do we want to inject parallel bitmap heap scan here?
		 */
		ereport(ERROR, (errmsg("Parallel custom scan not supported yet")));
	}
	else
	{
		RunBitmapSerialScan(scanState);
	}

	scanState->initialized = true;
}
