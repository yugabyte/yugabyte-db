/*-------------------------------------------------------------------------
 *
 * nodeYbBitmapTablescan.c
 *	  Routines to support bitmapped scans of relations
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 * Portions Copyright (c) Yugabyte, Inc.
 *
 * IDENTIFICATION
 *	  src/backend/executor/nodeYbBitmapTablescan.c
 *
 *-------------------------------------------------------------------------
 */
/*
 * INTERFACE ROUTINES
 *		ExecYbBitmapTableScan			scans a relation using bitmap info
 *		ExecYbBitmapTableNext			workhorse for above
 *		ExecInitYbBitmapTableScan		creates and initializes state info.
 *		ExecReScanYbBitmapTableScan	prepares to rescan the plan.
 *		ExecEndYbBitmapTableScan		releases all storage.
 */
#include "postgres.h"

#include "access/relscan.h"
#include "access/tableam.h"
#include "access/yb_scan.h"
#include "executor/executor.h"
#include "executor/nodeYbBitmapTablescan.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"


static TupleTableSlot *YbBitmapTableNext(YbBitmapTableScanState *node);
static TableScanDesc CreateYbBitmapTableScanDesc(YbBitmapTableScanState *scanstate);

/* ----------------------------------------------------------------
 *		YbBitmapTableNext
 *
 *		Retrieve next tuple from the YbBitmapTableScan node's currentRelation
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
YbBitmapTableNext(YbBitmapTableScanState *node)
{
	YbTIDBitmap  *ybtbm;
	TableScanDesc tsdesc;
	TupleTableSlot *slot;
	YbTBMIterateResult *ybtbmres;
	ExprContext *econtext;
	MemoryContext oldcontext;
	YbScanDesc ybScan;

	/*
	 * extract necessary information from index scan node
	 */
	econtext = node->ss.ps.ps_ExprContext;
	slot = node->ss.ss_ScanTupleSlot;
	ybtbm = node->ybtbm;
	ybtbmres = node->ybtbmres;

	/*
	 * If we haven't yet performed the underlying index scan, do it, and begin
	 * the iteration over the bitmap.
	 */
	if (!node->initialized)
	{
		ybtbm = (YbTIDBitmap *) MultiExecProcNode(outerPlanState(node));

		if (!ybtbm || !IsA(ybtbm, YbTIDBitmap))
			elog(ERROR, "unrecognized result from subplan");

		node->ybtbm = ybtbm;
		node->ybtbmiterator = yb_tbm_begin_iterate(ybtbm);
		node->ybtbmres = ybtbmres = NULL;
		node->initialized = true;
		node->work_mem_exceeded = ybtbm->work_mem_exceeded;
		node->average_ybctid_bytes = yb_tbm_get_average_bytes(ybtbm);
		node->recheck_required |= ybtbm->recheck;
		node->skipped_tuples = 0;
	}

	if (!node->ss.ss_currentScanDesc)
		node->ss.ss_currentScanDesc = CreateYbBitmapTableScanDesc(node);
	tsdesc = node->ss.ss_currentScanDesc;

	ybScan = (YbScanDesc) tsdesc;

	/*
	 * Special case: if we don't need the results (e.g. COUNT), just return as
	 * many null values as we have ybctids.
	 */
	if (node->can_skip_fetch && !node->recheck_required && !node->work_mem_exceeded)
	{
		if (++node->skipped_tuples > yb_tbm_get_size(ybtbm))
			return ExecClearTuple(slot);
		/*
		 * If we don't have to fetch the tuple, just return nulls.
		 */
		return ExecStoreAllNullTuple(slot);
	}

	/*
	 * If the bitmaps have exceeded work_mem just select everything from the
	 * main table. The correct remote filters have already been applied.
	 */
	if (node->work_mem_exceeded && !ybScan->is_exec_done)
	{
		HandleYBStatus(YBCPgExecSelect(ybScan->handle, ybScan->exec_params));
		ybScan->is_exec_done = true;
	}

	while (true)
	{
		/*
		 * If we have run out of tuples from our prefetched list, launch a new
		 * request for the next fetch_row_limit tuples.
		 * Note that while DocDB's responses would respect our row and size
		 * limits regardless of how many ybctids we send in a request, we want
		 * to limit the number of ybctids we bind to a request to limit our
		 * request size.
		 */
		if (!node->work_mem_exceeded && TupIsNull(slot))
		{
			if (ybtbmres)
				yb_tbm_free_iter_result(ybtbmres);

			const int ybctid_size = node->average_ybctid_bytes > 0
				? node->average_ybctid_bytes : 26;
			const int row_limit = ybScan->exec_params->yb_fetch_row_limit;
			const int size_limit = ybScan->exec_params->yb_fetch_size_limit /
								   ybctid_size;

			const int count = Min(row_limit > 0 ? row_limit : INT_MAX,
								  size_limit > 0 ? size_limit : INT_MAX);
			node->ybtbmres = ybtbmres = yb_tbm_iterate(node->ybtbmiterator,
													   count);
			if (!ybtbmres)
				break;

			/* Fetch the next yb_fetch_row_limit ybctids */
			HandleYBStatus(YBCPgFetchRequestedYbctids(ybScan->handle,
										   			  ybScan->exec_params,
													  ybtbmres->ybctid_vector));
		}

		/* We have yb_fetch_row_limit rows fetched, get them one by one */
		while (true)
		{
			/* capture all fetch allocations in the short-lived context */
			oldcontext = MemoryContextSwitchTo(econtext->ecxt_per_tuple_memory);
			ybFetchNext(ybScan->handle, slot,
						RelationGetRelid(node->ss.ss_currentRelation));
			MemoryContextSwitchTo(oldcontext);

			if (ybtbmres)
				++ybtbmres->index;

			/*
			 * If we have run out results, exit this loop to fetch the next
			 * batch.
			 */
			if (TupIsNull(slot))
				break;

			/*
			 * If we are using lossy info, we have to recheck the qual
			 * conditions at every tuple.
			 * Although ExecScan rechecks, it checks only node->qual, not
			 * the index conditions
			 */
			if (node->work_mem_exceeded)
			{
				econtext->ecxt_scantuple = slot;
				if (!ExecQualAndReset(node->fallback_local_quals, econtext))
				{
					/* Fails filter, so drop it and loop back for another */
					InstrCountFiltered1(node, 1);
					ExecClearTuple(slot);
					continue;
				}
			}
			else if (node->recheck_required)
			{
				econtext->ecxt_scantuple = slot;
				if (!ExecQualAndReset(node->recheck_local_quals, econtext))
				{
					/* Fails recheck, so drop it and loop back for another */
					InstrCountFiltered2(node, 1);
					ExecClearTuple(slot);
					continue;
				}
			}

			/* OK to return this tuple */
			return slot;
		}

		/* we have gone through all the tuples from the full scan, quit. */
		if (node->work_mem_exceeded)
			return ExecClearTuple(slot);
	}

	/*
	 * if we get here it means we are at the end of the scan..
	 */
	return ExecClearTuple(slot);
}

/*
 * YbBitmapTableRecheck -- access method routine to recheck a tuple in
 * EvalPlanQual
 */
static bool
YbBitmapTableRecheck(YbBitmapTableScanState *node, TupleTableSlot *slot)
{
	ExprContext *econtext;

	/*
	 * extract necessary information from index scan node
	 */
	econtext = node->ss.ps.ps_ExprContext;

	/* Does the tuple meet the original qual conditions? */
	econtext->ecxt_scantuple = slot;
	return ExecQualAndReset(node->fallback_local_quals, econtext);
}

/* ----------------------------------------------------------------
 *		ExecYbBitmapTableScan(node)
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
ExecYbBitmapTableScan(PlanState *pstate)
{
	YbBitmapTableScanState *node = castNode(YbBitmapTableScanState, pstate);

	return ExecScan(&node->ss,
					(ExecScanAccessMtd) YbBitmapTableNext,
					(ExecScanRecheckMtd) YbBitmapTableRecheck);
}

static TableScanDesc
CreateYbBitmapTableScanDesc(YbBitmapTableScanState *scanstate)
{
	YbScanDesc		ybScan;
	PushdownExprs  *yb_pushdown;
	TableScanDesc tsdesc;
	YbBitmapTableScan *plan = (YbBitmapTableScan *) scanstate->ss.ps.plan;
	bool			has_targets;

	yb_pushdown = YbInstantiatePushdownParams(
			scanstate->work_mem_exceeded ? &plan->fallback_pushdown
										 : &plan->rel_pushdown,
			scanstate->ss.ps.state);

	ybScan = ybcBeginScan(scanstate->ss.ss_currentRelation,
						  NULL /* index */,
						  false /* xs_want_itup */,
						  0 /* nkeys */,
						  NULL /* keys */,
						  (Scan *) plan /* pg_scan_plan */,
						  yb_pushdown /* rel_pushdown */,
						  NULL /* idx_pushdown */,
						  NULL /* aggrefs */,
						  0 /* distinct_prefixlen */,
						  &scanstate->ss.ps.state->yb_exec_params,
						  true /* is_internal_scan */,
						  false /* fetch_ybctids_only */);

	if (yb_pushdown)
		pfree(yb_pushdown);

	/* Set up Postgres sys table scan description */
	tsdesc = (TableScanDesc) ybScan;
	tsdesc->rs_rd = scanstate->ss.ss_currentRelation;
	tsdesc->rs_snapshot = scanstate->ss.ps.state->es_snapshot;
	tsdesc->rs_flags = SO_TYPE_BITMAPSCAN;

	/*
	 * We can potentially skip sending a request to the table if we do not need
	 * any columns of the table, either for checking non-indexable quals or for
	 * returning data.  This test is a bit simplistic, as it checks the
	 * stronger condition that there's no qual or return tlist at all.  But in
	 * most cases it's probably not worth working harder than that.
	 *
	 * The PG version of this test looked only at qual and tlist. In Yugabyte,
	 * the target list from PGGate is more accurate and not much more work to
	 * look at, so look at that instead.
	 */
	HandleYBStatus(YBCPgDmlHasRegularTargets(ybScan->handle, &has_targets));

	scanstate->can_skip_fetch = (plan->scan.plan.qual == NIL &&
								 plan->rel_pushdown.quals == NIL &&
								 !has_targets && !scanstate->recheck_required);

	if (scanstate->recheck_required && !scanstate->work_mem_exceeded)
	{
		PushdownExprs *recheck_pushdown = YbInstantiatePushdownParams(
			&plan->recheck_pushdown,
			scanstate->ss.ps.state);
		if (recheck_pushdown)
		{
			YbDmlAppendQuals(recheck_pushdown->quals,
							 true /* is_primary */, ybScan->handle);
			YbDmlAppendColumnRefs(recheck_pushdown->colrefs,
								  true /* is_primary */, ybScan->handle);
			pfree(recheck_pushdown);
		}
	}

	return tsdesc;
}

/* ----------------------------------------------------------------
 *		ExecReScanYbBitmapTableScan(node)
 * ----------------------------------------------------------------
 */
void
ExecReScanYbBitmapTableScan(YbBitmapTableScanState *node)
{
	PlanState  *outerPlan = outerPlanState(node);

	TableScanDesc tsdesc;

	/* rescan to release any page pin */
	tsdesc = node->ss.ss_currentScanDesc;
	/*
	 * YB initializes ss_currentScanDesc in YbBitmapTableNext rather than
	 * ExecInitYbBitmapTableScan, so the following if condition is needed.
	 */
	if (tsdesc)
	{
		/*
		 * For rescan, end the previous scan. Set the old scan to null so we
		 * recreate it when we need to.
		 */
		ybc_heap_endscan(tsdesc);
		node->ss.ss_currentScanDesc = NULL;
	}

	/* release bitmaps and buffers if any */
	if (node->ybtbmres)
		yb_tbm_free_iter_result(node->ybtbmres);
	if (node->ybtbmiterator)
		yb_tbm_end_iterate(node->ybtbmiterator);
	if (node->ybtbm)
		yb_tbm_free(node->ybtbm);

	node->ybtbm = NULL;
	node->ybtbmiterator = NULL;
	node->ybtbmres = NULL;
	node->initialized = false;

	ExecScanReScan(&node->ss);

	/*
	 * if chgParam of subnode is not null then plan will be re-scanned by
	 * first ExecProcNode.
	 */
	if (outerPlan->chgParam == NULL)
		ExecReScan(outerPlan);
}

/* ----------------------------------------------------------------
 *		ExecEndYbBitmapTableScan
 * ----------------------------------------------------------------
 */
void
ExecEndYbBitmapTableScan(YbBitmapTableScanState *node)
{
	TableScanDesc tsdesc;

	/*
	 * extract information from the node
	 */
	tsdesc = node->ss.ss_currentScanDesc;

	/*
	 * Free the exprcontext
	 */
	ExecFreeExprContext(&node->ss.ps);

	/*
	 * clear out tuple table slots
	 */
	if (node->ss.ps.ps_ResultTupleSlot)
		ExecClearTuple(node->ss.ps.ps_ResultTupleSlot);
	ExecClearTuple(node->ss.ss_ScanTupleSlot);

	/*
	 * close down subplans
	 */
	ExecEndNode(outerPlanState(node));

	/*
	 * release bitmaps and buffers if any
	 */
	if (node->ybtbmres)
		yb_tbm_free_iter_result(node->ybtbmres);
	if (node->ybtbmiterator)
		yb_tbm_end_iterate(node->ybtbmiterator);
	if (node->ybtbm)
		yb_tbm_free(node->ybtbm);

	/*
	 * close heap scan
	 */
	if (tsdesc != NULL)
		ybc_heap_endscan(tsdesc);
}

/* ----------------------------------------------------------------
 *		ExecInitYbBitmapTableScan
 *
 *		Initializes the scan's state information.
 * ----------------------------------------------------------------
 */
YbBitmapTableScanState *
ExecInitYbBitmapTableScan(YbBitmapTableScan *node, EState *estate, int eflags)
{
	YbBitmapTableScanState *scanstate;
	Relation				currentRelation;

	/* check for unsupported flags */
	Assert(!(eflags & (EXEC_FLAG_BACKWARD | EXEC_FLAG_MARK)));

	/*
	 * Assert caller didn't ask for an unsafe snapshot --- see comments at
	 * head of file.
	 */
	Assert(IsMVCCSnapshot(estate->es_snapshot));

	/*
	 * create state structure
	 */
	scanstate = makeNode(YbBitmapTableScanState);
	scanstate->ss.ps.plan = (Plan *) node;
	scanstate->ss.ps.state = estate;
	scanstate->ss.ps.ExecProcNode = ExecYbBitmapTableScan;

	scanstate->ybtbm = NULL;
	scanstate->ybtbmiterator = NULL;
	scanstate->ybtbmres = NULL;
	scanstate->recheck_required = false;
	scanstate->fallback_local_quals = NULL;
	/* may be updated below */
	scanstate->initialized = false;

	/*
	 * Miscellaneous initialization
	 *
	 * create expression context for node
	 */
	ExecAssignExprContext(estate, &scanstate->ss.ps);

	/*
	 * open the base relation and acquire appropriate lock on it.
	 */
	currentRelation = ExecOpenScanRelation(estate, node->scan.scanrelid,
										   eflags);

	/*
	 * initialize child nodes
	 *
	 * We do this after ExecOpenScanRelation because the child nodes will open
	 * indexscans on our relation's indexes, and we want to be sure we have
	 * acquired a lock on the relation first.
	 */
	outerPlanState(scanstate) = ExecInitNode(outerPlan(node), estate, eflags);

	/*
	 * get the scan type from the relation descriptor.
	 */
	ExecInitScanTupleSlot(estate, &scanstate->ss,
						  RelationGetDescr(currentRelation),
						  &TTSOpsVirtual);

	/*
	 * Initialize result type and projection.
	 */
	ExecInitResultTypeTL(&scanstate->ss.ps);
	ExecAssignScanProjectionInfo(&scanstate->ss);

	/*
	 * initialize child expressions
	 */
	scanstate->ss.ps.qual =
		ExecInitQual(node->scan.plan.qual, (PlanState *) scanstate);
	scanstate->recheck_local_quals =
		ExecInitQual(node->recheck_local_quals, (PlanState *) scanstate);
	scanstate->fallback_local_quals =
		ExecInitQual(node->fallback_local_quals, (PlanState *) scanstate);

	scanstate->ss.ss_currentRelation = currentRelation;

	/*
	 * all done.
	 */
	return scanstate;
}
