/*-------------------------------------------------------------------------
 *
 * nodeYbBitmapIndexscan.c
 *	  Routines to support bitmapped index scans of relations
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/nodeYbBitmapIndexscan.c
 *
 *-------------------------------------------------------------------------
 */
/*
 * INTERFACE ROUTINES
 *		MultiExecYbBitmapIndexScan	scans a relation using index.
 *		ExecInitYbBitmapIndexScan		creates and initializes state info.
 *		ExecReScanYbBitmapIndexScan	prepares to rescan the plan.
 *		ExecEndYbBitmapIndexScan		releases all storage.
 */
#include "postgres.h"

#include "executor/execdebug.h"
#include "executor/nodeYbBitmapIndexscan.h"
#include "executor/nodeIndexscan.h"
#include "miscadmin.h"
#include "utils/memutils.h"

/* YB includes. */
#include "pg_yb_utils.h"
#include "access/relscan.h"

static void yb_init_bitmap_index_scandesc(YbBitmapIndexScanState *node);

/* ----------------------------------------------------------------
 *		ExecYbBitmapIndexScan
 *
 *		stub for pro forma compliance
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
ExecYbBitmapIndexScan(PlanState *pstate)
{
	elog(ERROR, "YbBitmapIndexScan node does not support ExecProcNode call convention");
	return NULL;
}

/*
 * yb_init_bitmap_index_scandesc
 *
 *		Initialize Yugabyte specific fields of the IndexScanDesc.
 */
static void
yb_init_bitmap_index_scandesc(YbBitmapIndexScanState *node)
{
	EState	   *estate = node->ss.ps.state;
	YbBitmapIndexScan *plan = (YbBitmapIndexScan *) node->ss.ps.plan;

	IndexScanDesc scandesc = node->biss_ScanDesc;
	scandesc->yb_exec_params = &estate->yb_exec_params;
	scandesc->yb_scan_plan = (Scan *) plan;
	scandesc->fetch_ybctids_only = true;
	scandesc->heapRelation = node->ss.ss_currentRelation;

	const bool is_colocated =
		YbGetTableProperties(node->ss.ss_currentRelation)->is_colocated;
	const bool is_primary =
		scandesc->heapRelation->rd_pkindex == node->biss_RelationDesc->rd_id;

	/* primary keys on colocated indexes don't have a secondary index in their request */
	if (is_colocated && is_primary)
		scandesc->yb_rel_pushdown =
			YbInstantiatePushdownParams(&plan->yb_idx_pushdown, estate);
	else
		scandesc->yb_idx_pushdown =
			YbInstantiatePushdownParams(&plan->yb_idx_pushdown, estate);
}

/* ----------------------------------------------------------------
 *		MultiExecYbBitmapIndexScan(node)
 * ----------------------------------------------------------------
 */
Node *
MultiExecYbBitmapIndexScan(YbBitmapIndexScanState *node)
{
	YbTIDBitmap *bitmap;
	IndexScanDesc scandesc;
	double		nTuples = 0;
	bool		doscan;
	bool		recheck;
	YbScanDesc	ybscan;
	EState	   *estate = node->ss.ps.state;

	/* must provide our own instrumentation support */
	if (node->ss.ps.instrument)
		InstrStartNode(node->ss.ps.instrument);

	if (node->biss_ScanDesc == NULL)
	{
		/*
		* Initialize scan descriptor.
		*/
		node->biss_ScanDesc =
			index_beginscan_bitmap(node->biss_RelationDesc,
								estate->es_snapshot,
								node->biss_NumScanKeys);

		yb_init_bitmap_index_scandesc(node);

		/*
		* If no run-time keys to calculate, go ahead and pass the scankeys to the
		* index AM.
		*/
		if (node->biss_NumRuntimeKeys == 0 && node->biss_NumArrayKeys == 0)
			index_rescan(node->biss_ScanDesc,
						node->biss_ScanKeys, node->biss_NumScanKeys,
						NULL, 0);
	}

	/*
	 * extract necessary information from index scan node
	 */
	scandesc = node->biss_ScanDesc;

	/*
	 * If we have runtime keys and they've not already been set up, do it now.
	 * Array keys are also treated as runtime keys; note that if ExecReScan
	 * returns with biss_RuntimeKeysReady still false, then there is an empty
	 * array key so we should do nothing.
	 */
	if (!node->biss_RuntimeKeysReady &&
		(node->biss_NumRuntimeKeys != 0 || node->biss_NumArrayKeys != 0))
	{
		ExecReScan((PlanState *) node);
		doscan = node->biss_RuntimeKeysReady;
	}
	else
		doscan = true;

	/*
	 * Prepare the result bitmap.  Normally we just create a new one to pass
	 * back; however, our parent node is allowed to store a pre-made one into
	 * node->biss_result, in which case we just OR our tuple IDs into the
	 * existing bitmap.  (This saves needing explicit UNION steps.)
	 */
	if (node->biss_result)
	{
		Assert(IsA(node->biss_result, YbTIDBitmap));
		bitmap = node->biss_result;

		node->biss_result = NULL;	/* reset for next time */
	}
	else
	{
		yb_init_bitmap_index_scandesc(node);
		bitmap = yb_tbm_create(work_mem * 1024L);
	}

	ybscan = (YbScanDesc) scandesc->opaque;
	recheck = YbPredetermineNeedsRecheck(ybscan->relation, ybscan->index,
										 true /* xs_want_itup */,
										 node->biss_ScanKeys,
										 node->biss_NumScanKeys);

	/*
	 * Get TIDs from index and insert into bitmap
	 */
	while (doscan)
	{
		nTuples += (double) yb_index_getbitmap(scandesc, bitmap, recheck);

		CHECK_FOR_INTERRUPTS();

		doscan = ExecIndexAdvanceArrayKeys(node->biss_ArrayKeys,
										   node->biss_NumArrayKeys);
		if (doscan)				/* reset index scan */
			index_rescan(node->biss_ScanDesc,
						 node->biss_ScanKeys, node->biss_NumScanKeys,
						 NULL, 0);
	}

	/* must provide our own instrumentation support */
	if (node->ss.ps.instrument)
		InstrStopNode(node->ss.ps.instrument, nTuples);

	return (Node *) bitmap;
}

/* ----------------------------------------------------------------
 *		ExecReScanYbBitmapIndexScan(node)
 *
 *		Recalculates the values of any scan keys whose value depends on
 *		information known at runtime, then rescans the indexed relation.
 * ----------------------------------------------------------------
 */
void
ExecReScanYbBitmapIndexScan(YbBitmapIndexScanState *node)
{
	ExprContext *econtext = node->biss_RuntimeContext;
	EState		*estate = node->ss.ps.state;

	/*
	 * Reset the runtime-key context so we don't leak memory as each outer
	 * tuple is scanned.  Note this assumes that we will recalculate *all*
	 * runtime keys on each call.
	 */
	if (econtext)
		ResetExprContext(econtext);

	/*
	 * If we are doing runtime key calculations (ie, any of the index key
	 * values weren't simple Consts), compute the new key values.
	 *
	 * Array keys are also treated as runtime keys; note that if we return
	 * with biss_RuntimeKeysReady still false, then there is an empty array
	 * key so no index scan is needed.
	 */
	if (node->biss_NumRuntimeKeys != 0)
		ExecIndexEvalRuntimeKeys(econtext,
								 node->biss_RuntimeKeys,
								 node->biss_NumRuntimeKeys);
	if (node->biss_NumArrayKeys != 0)
		node->biss_RuntimeKeysReady =
			ExecIndexEvalArrayKeys(econtext,
								   node->biss_ArrayKeys,
								   node->biss_NumArrayKeys);
	else
		node->biss_RuntimeKeysReady = true;

	if (node->biss_ScanDesc == NULL)
	{
		node->biss_ScanDesc = index_beginscan_bitmap(node->biss_RelationDesc,
													 estate->es_snapshot,
													 node->biss_NumScanKeys);
		yb_init_bitmap_index_scandesc(node);
	}

	/* reset index scan */
	if (node->biss_RuntimeKeysReady)
		index_rescan(node->biss_ScanDesc,
					 node->biss_ScanKeys, node->biss_NumScanKeys,
					 NULL, 0);
}

/* ----------------------------------------------------------------
 *		ExecEndYbBitmapIndexScan
 * ----------------------------------------------------------------
 */
void
ExecEndYbBitmapIndexScan(YbBitmapIndexScanState *node)
{
	Relation	indexRelationDesc;
	IndexScanDesc indexScanDesc;

	/*
	 * extract information from the node
	 */
	indexRelationDesc = node->biss_RelationDesc;
	indexScanDesc = node->biss_ScanDesc;

	/*
	 * Free the exprcontext ... now dead code, see ExecFreeExprContext
	 */
#ifdef NOT_USED
	if (node->biss_RuntimeContext)
		FreeExprContext(node->biss_RuntimeContext, true);
#endif

	if (IsYugaByteEnabled())
	{
		if (indexScanDesc && indexScanDesc->yb_idx_pushdown)
		{
			pfree(indexScanDesc->yb_idx_pushdown);
			indexScanDesc->yb_idx_pushdown = NULL;
		}

		Relation relation = node->ss.ss_currentRelation;
		if (relation)
			ExecCloseScanRelation(relation);
	}

	/*
	 * close the index relation (no-op if we didn't open it)
	 */
	if (indexScanDesc)
		index_endscan(indexScanDesc);
	if (indexRelationDesc)
		index_close(indexRelationDesc, NoLock);
}

/* ----------------------------------------------------------------
 *		ExecInitYbBitmapIndexScan
 *
 *		Initializes the index scan's state information.
 * ----------------------------------------------------------------
 */
YbBitmapIndexScanState *
ExecInitYbBitmapIndexScan(YbBitmapIndexScan *node, EState *estate, int eflags)
{
	YbBitmapIndexScanState *indexstate;
	bool		relistarget;
	Relation	index;

	/* check for unsupported flags */
	Assert(!(eflags & (EXEC_FLAG_BACKWARD | EXEC_FLAG_MARK)));

	/*
	 * create state structure
	 */
	indexstate = makeNode(YbBitmapIndexScanState);
	indexstate->ss.ps.plan = (Plan *) node;
	indexstate->ss.ps.state = estate;
	indexstate->ss.ps.ExecProcNode = ExecYbBitmapIndexScan;

	/* normally we don't make the result bitmap till runtime */
	indexstate->biss_result = NULL;

	/*
	 * We do not open or lock the base relation here.  We assume that an
	 * ancestor BitmapHeapScan node is holding AccessShareLock (or better) on
	 * the heap relation throughout the execution of the plan tree.
	 */

	indexstate->ss.ss_currentRelation = NULL;
	indexstate->ss.ss_currentScanDesc = NULL;

	/*
	 * Miscellaneous initialization
	 *
	 * We do not need a standard exprcontext for this node, though we may
	 * decide below to create a runtime-key exprcontext
	 */

	/*
	 * initialize child expressions
	 *
	 * We don't need to initialize targetlist or qual since neither are used.
	 *
	 * Note: we don't initialize all of the indexqual expression, only the
	 * sub-parts corresponding to runtime keys (see below).
	 */

	/*
	 * If we are just doing EXPLAIN (ie, aren't going to run the plan), stop
	 * here.  This allows an index-advisor plugin to EXPLAIN a plan containing
	 * references to nonexistent indexes.
	 */
	if (eflags & EXEC_FLAG_EXPLAIN_ONLY)
		return indexstate;

	/*
	 * Open the index and main table relations. Unlike Postgres, Yugabyte needs
	 * both to properly construct the request.
	 *
	 * If the parent table is one of the target relations of the query, then
	 * InitPlan already opened and write-locked the index, so we can avoid
	 * taking another lock here.  Otherwise we need a normal reader's lock.
	 */
	relistarget = ExecRelationIsTargetRelation(estate, node->scan.scanrelid);
	index = index_open(node->indexid, relistarget ? NoLock : AccessShareLock);
	indexstate->ss.ss_currentRelation =
		ExecOpenScanRelation(estate, node->scan.scanrelid, eflags);
	Assert(IsYBRelation(index));

	/*
	 * Initialize index-specific scan state
	 */
	indexstate->biss_RelationDesc = index;
	indexstate->biss_RuntimeKeysReady = false;
	indexstate->biss_RuntimeKeys = NULL;
	indexstate->biss_NumRuntimeKeys = 0;

	/*
	 * build the index scan keys from the index qualification
	 */
	ExecIndexBuildScanKeys((PlanState *) indexstate,
						   indexstate->biss_RelationDesc,
						   node->indexqual,
						   false,
						   &indexstate->biss_ScanKeys,
						   &indexstate->biss_NumScanKeys,
						   &indexstate->biss_RuntimeKeys,
						   &indexstate->biss_NumRuntimeKeys,
						   &indexstate->biss_ArrayKeys,
						   &indexstate->biss_NumArrayKeys);

	/*
	 * If we have runtime keys or array keys, we need an ExprContext to
	 * evaluate them. We could just create a "standard" plan node exprcontext,
	 * but to keep the code looking similar to nodeIndexscan.c, it seems
	 * better to stick with the approach of using a separate ExprContext.
	 */
	if (indexstate->biss_NumRuntimeKeys != 0 ||
		indexstate->biss_NumArrayKeys != 0)
	{
		ExprContext *stdecontext = indexstate->ss.ps.ps_ExprContext;

		ExecAssignExprContext(estate, &indexstate->ss.ps);
		indexstate->biss_RuntimeContext = indexstate->ss.ps.ps_ExprContext;
		indexstate->ss.ps.ps_ExprContext = stdecontext;
	}
	else
	{
		indexstate->biss_RuntimeContext = NULL;
	}

	/*
	 * all done.
	 */
	return indexstate;
}
