/*-------------------------------------------------------------------------
 *
 * nodeYbSeqscan.c
 *	  Support routines for sequential scans of Yugabyte relations.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/nodeYbSeqscan.c
 *
 *-------------------------------------------------------------------------
 */
/*
 * INTERFACE ROUTINES
 *		ExecYbSeqScan				sequentially scans a relation.
 *		ExecYbSeqNext				retrieve next tuple in sequential order.
 *		ExecInitYbSeqScan			creates and initializes a seqscan node.
 *		ExecEndYbSeqScan			releases any storage allocated.
 *		ExecReScanYbSeqScan			rescans the relation
 */
#include "postgres.h"

#include "access/relscan.h"
#include "access/tableam.h"
#include "access/xact.h"
#include "access/yb_parallel_scan.h"
#include "access/yb_table_scan.h"
#include "access/yb_table_scan_options.h"
#include "executor/execdebug.h"
#include "executor/nodeYbSeqscan.h"
#include "utils/rel.h"

static TupleTableSlot *YbSeqNext(YbSeqScanState *node);

/* ----------------------------------------------------------------
 *						Scan Support
 * ----------------------------------------------------------------
 */

/* ----------------------------------------------------------------
 *		YbSeqNext
 *
 *		This is a workhorse for ExecYbSeqScan
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
YbSeqNext(YbSeqScanState *node)
{
	EState	   *estate;
	TupleTableSlot *slot;
	ExprContext *econtext;
	MemoryContext oldcontext;
	TableScanDesc tsdesc;

	estate = node->ss.ps.state;
	econtext = node->ss.ps.ps_ExprContext;
	slot = node->ss.ss_ScanTupleSlot;
	tsdesc = node->ss.ss_currentScanDesc;

	/*
	 * Initialize the scandesc upon the first invocation.
	 */
	if (tsdesc == NULL)
	{
		if (node->aggrefs)
		{
			/*
			 * For aggregate pushdown, we read just the aggregates from DocDB
			 * and pass that up to the aggregate node (agg pushdown wouldn't be
			 * enabled if we needed to read more than that).  Set up a dummy
			 * scan slot to hold as many attributes as there are pushed
			 * aggregates.
			 */
			TupleDesc	tupdesc = CreateTemplateTupleDesc(list_length(node->aggrefs));

			ExecInitScanTupleSlot(estate, &node->ss, tupdesc, &TTSOpsVirtual);
			/* Refresh the local pointer. */
			slot = node->ss.ss_ScanTupleSlot;
		}

		YbSeqScan  *plan = (YbSeqScan *) node->ss.ps.plan;
		YbPushdownExprs *yb_pushdown =
			YbInstantiatePushdownExprs(&plan->yb_pushdown, estate);
		int			rowmark = YBC_NO_ROW_MARK;
		LockWaitPolicy wait_policy = LockWaitBlock;

		/*
		 * In case of SERIALIZABLE isolation level we have to take prefix
		 * range locks to disallow INSERTion of new rows that satisfy the
		 * query predicate.  So, we set the rowmark on all read requests
		 * sent to tserver instead of locking each tuple one by one in
		 * LockRows node.
		 */
		if (IsolationIsSerializable())
		{
			for (int i = 0;
				 estate->es_rowmarks && i < estate->es_range_table_size;
				 i++)
			{
				ExecRowMark *erm = estate->es_rowmarks[i];

				/*
				 * YB_TODO: This block of code is broken on master (GH
				 * #20704).  With PG commit
				 * f9eb7c14b08d2cc5eda62ffaf37a356c05e89b93,
				 * estate->es_rowmarks is an array with potentially NULL
				 * elements (previously, it was a list).  As a temporary
				 * fix till #20704 is addressed, ignore any NULL element
				 * in es_rowmarks.
				 */
				if (!erm)
					continue;
				/* Do not propagate non-row-locking row marks. */
				if (erm->markType != ROW_MARK_REFERENCE &&
					erm->markType != ROW_MARK_COPY)
				{
					rowmark = erm->markType;
					wait_policy = erm->waitPolicy;
				}
			}
		}

		YbTableScanOptions yb_options = {
			.pg_scan_plan = (Scan *) plan,
			.rel_pushdown = yb_pushdown,
			.aggrefs = node->aggrefs,
			.exec_params = &estate->yb_exec_params,
			.rowmark = rowmark,
			.wait_policy = wait_policy,
			.pscan = node->pscan,
		};

		node->ss.ss_currentScanDesc = tsdesc =
			table_beginscan_yb(node->ss.ss_currentRelation,
							   estate->es_snapshot,
							   0,	/* nkeys */
							   NULL,	/* keys */
							   &yb_options);
	}

	/*
	 * Aggregate pushdown bypasses tableam: the scan slot has a synthetic
	 * descriptor matching the pushed aggregates, not the relation's real
	 * descriptor.
	 */
	if (node->aggrefs)
	{
		/*
		 * In the case of parallel scan we need to obtain boundaries from the
		 * pscan before the scan is executed. Also empty row from parallel range
		 * scan does not mean scan is done, it means the range is done and we
		 * need to pick up next. No rows from parallel range is possible, hence
		 * the loop.
		 */
		while (true)
		{
			/* Need to execute the request */
			if (!yb_scan_desc_is_exec_done(tsdesc))
			{
				/* Parallel mode: pick up parallel block first */
				if (yb_scan_desc_pscan(tsdesc) != NULL &&
					!yb_scan_desc_apply_next_parallel_range(tsdesc))
					return NULL;
				yb_scan_desc_exec_select(tsdesc);
			}

			/* capture all fetch allocations in the short-lived context */
			oldcontext = MemoryContextSwitchTo(econtext->ecxt_per_tuple_memory);
			yb_scan_desc_fetch_next(tsdesc, slot,
									RelationGetRelid(node->ss.ss_currentRelation));
			MemoryContextSwitchTo(oldcontext);

			/*
			 * No more rows in parallel mode: repeat for next range, else break
			 * to return the result.
			 */
			if (TupIsNull(slot) && yb_scan_desc_pscan(tsdesc) != NULL)
				yb_scan_desc_set_exec_done(tsdesc, false);
			else
				break;
		}
		return slot;
	}

	/*
	 * Non-aggregate-pushdown path: fetch through tableam.
	 * Pggate does not perform tablet-level parallelism when a direction is
	 * set (see CouldBeExecutedInParallel), so use NoMovementScanDirection.
	 */
	oldcontext = MemoryContextSwitchTo(econtext->ecxt_per_tuple_memory);
	table_scan_getnextslot(tsdesc, NoMovementScanDirection, slot);
	MemoryContextSwitchTo(oldcontext);
	return slot;
}

/*
 * YbSeqRecheck -- access method routine to recheck a tuple in EvalPlanQual
 */
static bool
YbSeqRecheck(YbSeqScanState *node, TupleTableSlot *slot)
{
	/*
	 * Note that unlike IndexScan, SeqScan never use keys in heap_beginscan
	 * (and this is very bad) - so, here we do not check are keys ok or not.
	 */
	return true;
}

/* ----------------------------------------------------------------
 *		ExecYbSeqScan(node)
 *
 *		Scans the relation sequentially and returns the next qualifying
 *		tuple.
 *		We call the ExecScan() routine and pass it the appropriate
 *		access method functions.
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
ExecYbSeqScan(PlanState *pstate)
{
	YbSeqScanState *node = castNode(YbSeqScanState, pstate);

	return ExecScan(&node->ss,
					(ExecScanAccessMtd) YbSeqNext,
					(ExecScanRecheckMtd) YbSeqRecheck);
}


/* ----------------------------------------------------------------
 *		ExecInitYbSeqScan
 * ----------------------------------------------------------------
 */
YbSeqScanState *
ExecInitYbSeqScan(YbSeqScan *node, EState *estate, int eflags)
{
	YbSeqScanState *scanstate;

	/*
	 * Once upon a time it was possible to have an outerPlan of a SeqScan, but
	 * not any more.
	 */
	Assert(outerPlan(node) == NULL);
	Assert(innerPlan(node) == NULL);

	/*
	 * create state structure
	 */
	scanstate = makeNode(YbSeqScanState);
	scanstate->ss.ps.plan = (Plan *) node;
	scanstate->ss.ps.state = estate;
	scanstate->ss.ps.ExecProcNode = ExecYbSeqScan;

	/*
	 * Miscellaneous initialization
	 *
	 * create expression context for node
	 */
	ExecAssignExprContext(estate, &scanstate->ss.ps);

	/*
	 * Initialize scan relation.
	 *
	 * Get the relation object id from the relid'th entry in the range table,
	 * open that relation and acquire appropriate lock on it.
	 */
	scanstate->ss.ss_currentRelation =
		ExecOpenScanRelation(estate,
							 node->scan.scanrelid,
							 eflags);

	/* and create slot with the appropriate rowtype */
	ExecInitScanTupleSlot(estate, &scanstate->ss,
						  RelationGetDescr(scanstate->ss.ss_currentRelation),
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

	return scanstate;
}

/* ----------------------------------------------------------------
 *		ExecEndYbSeqScan
 *
 *		frees any storage allocated through C routines.
 * ----------------------------------------------------------------
 */
void
ExecEndYbSeqScan(YbSeqScanState *node)
{
	TableScanDesc tsdesc;

	/*
	 * get information from node
	 */
	tsdesc = node->ss.ss_currentScanDesc;

	/*
	 * Free the exprcontext
	 */
	ExecFreeExprContext(&node->ss.ps);

	/*
	 * clean out the tuple table
	 */
	if (node->ss.ps.ps_ResultTupleSlot)
		ExecClearTuple(node->ss.ps.ps_ResultTupleSlot);
	ExecClearTuple(node->ss.ss_ScanTupleSlot);

	/*
	 * close heap scan
	 */
	if (tsdesc != NULL)
		table_endscan(tsdesc);
}

/* ----------------------------------------------------------------
 *						Join Support
 * ----------------------------------------------------------------
 */

/* ----------------------------------------------------------------
 *		ExecReScanYbSeqScan
 *
 *		Rescans the relation.
 * ----------------------------------------------------------------
 */
void
ExecReScanYbSeqScan(YbSeqScanState *node)
{
	TableScanDesc tsdesc;

	/*
	 * End previous YB scan to reinit it upon the next fetch.
	 */
	tsdesc = node->ss.ss_currentScanDesc;
	if (tsdesc != NULL)
	{
		table_endscan(tsdesc);
		node->ss.ss_currentScanDesc = NULL;
	}

	ExecScanReScan((ScanState *) node);
}

/* ----------------------------------------------------------------
 *						Parallel Scan Support
 * ----------------------------------------------------------------
 */

/* ----------------------------------------------------------------
 *		ExecYbSeqScanEstimate
 *
 *		Compute the amount of space we'll need in the parallel
 *		query DSM, and inform pcxt->estimator about our needs.
 * ----------------------------------------------------------------
 */
void
ExecYbSeqScanEstimate(YbSeqScanState *node,
					  ParallelContext *pcxt)
{
	node->pscan_len = yb_estimate_parallel_size();
	shm_toc_estimate_chunk(&pcxt->estimator, node->pscan_len);
	shm_toc_estimate_keys(&pcxt->estimator, 1);
}

/* ----------------------------------------------------------------
 *		ExecYbSeqScanInitializeDSM
 *
 *		Set up a parallel heap scan descriptor.
 * ----------------------------------------------------------------
 */
void
ExecYbSeqScanInitializeDSM(YbSeqScanState *node,
						   ParallelContext *pcxt)
{
	YBParallelPartitionKeys pscan;

	pscan = shm_toc_allocate(pcxt->toc, node->pscan_len);
	yb_init_partition_key_data(pscan);
	shm_toc_insert(pcxt->toc, node->ss.ps.plan->plan_node_id, pscan);
	ybParallelPrepare(pscan, node->ss.ss_currentRelation, true /* is_forward */ );
	node->pscan = pscan;
}

/* ----------------------------------------------------------------
 *		ExecYbSeqScanReInitializeDSM
 *
 *		Reset shared state before beginning a fresh scan.
 * ----------------------------------------------------------------
 */
void
ExecYbSeqScanReInitializeDSM(YbSeqScanState *node,
							 ParallelContext *pcxt)
{
	YBParallelPartitionKeys pscan = node->pscan;

	yb_rescan_partition_key_data(pscan);
	ybParallelPrepare(pscan, node->ss.ss_currentRelation, true /* is_forward */ );
}

/* ----------------------------------------------------------------
 *		ExecYbSeqScanInitializeWorker
 *
 *		Copy relevant information from TOC into planstate.
 * ----------------------------------------------------------------
 */
void
ExecYbSeqScanInitializeWorker(YbSeqScanState *node,
							  ParallelWorkerContext *pwcxt)
{
	YBParallelPartitionKeys pscan;

	pscan = shm_toc_lookup(pwcxt->toc, node->ss.ps.plan->plan_node_id, false);
	ybParallelPrepare(pscan, node->ss.ss_currentRelation, true /* is_forward */ );
	node->pscan = pscan;
}
