/*-------------------------------------------------------------------------
 *
 * nodeYbSeqscan.c
 *	  Support routines for sequential scans of Yugabyte relations.
 *
 * Copyright (c) YugaByte, Inc.
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
#include "access/xact.h"
#include "access/yb_scan.h"
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
	TableScanDesc tsdesc;
	EState	   *estate;
	TupleTableSlot *slot;
	ExprContext *econtext;
	MemoryContext oldcontext;
	YbScanDesc ybScan;

	/*
	 * get information from the estate and scan state
	 */
	tsdesc = node->ss.ss_currentScanDesc;
	estate = node->ss.ps.state;
	econtext = node->ss.ps.ps_ExprContext;
	slot = node->ss.ss_ScanTupleSlot;

	/*
	 * Initialize the scandesc upon the first invocation.
	 * The scan only needs its ybscan field, so eventually we may use it
	 * directly and ignore the ss_currentScanDesc.
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
			TupleDesc tupdesc = CreateTemplateTupleDesc(list_length(node->aggrefs));
			ExecInitScanTupleSlot(estate, &node->ss, tupdesc, &TTSOpsVirtual);
			/* Refresh the local pointer. */
			slot = node->ss.ss_ScanTupleSlot;
		}

		YbSeqScan *plan = (YbSeqScan *) node->ss.ps.plan;
		PushdownExprs *yb_pushdown =
			YbInstantiatePushdownParams(&plan->yb_pushdown, estate);
		YbScanDesc ybScan = ybcBeginScan(node->ss.ss_currentRelation,
										 NULL /* index */,
										 false /* xs_want_itup */,
										 0 /* nkeys */,
										 NULL /* key */,
										 (Scan *) plan,
										 yb_pushdown /* rel_pushdown */,
										 NULL /* idx_pushdown */,
										 node->aggrefs,
										 0 /* distinct_prefixlen */,
										 &estate->yb_exec_params,
										 false /* is_internal_scan */,
										 false /* fetch_ybctids_only */);
		ybScan->pscan = node->pscan;

		tsdesc = (TableScanDesc) ybScan;
		tsdesc->rs_snapshot = estate->es_snapshot;
		tsdesc->rs_flags = SO_TYPE_SEQSCAN;

		node->ss.ss_currentScanDesc = tsdesc;
	}

	/*
	 * Set up any locking that happens at the time of the scan.
	 */
	if (IsYugaByteEnabled() && IsolationIsSerializable())
	{
		/*
		 * In case of SERIALIZABLE isolation level we have to take prefix range
		 * locks to disallow INSERTion of new rows that satisfy the query
		 * predicate. So, we set the rowmark on all read requests sent to
		 * tserver instead of locking each tuple one by one in LockRows node.
		 */
		for (int i = 0; estate->es_rowmarks && i < estate->es_range_table_size;
			 i++)
		{
			ExecRowMark *erm = estate->es_rowmarks[i];
			/*
			 * YB_TODO: This block of code is broken on master (GH #20704). With
			 * PG commit f9eb7c14b08d2cc5eda62ffaf37a356c05e89b93,
			 * estate->es_rowmarks is an array with
			 * potentially NULL elements (previously, it was a list). As a
			 * temporary fix till #20704 is addressed, ignore any NULL element
			 * in es_rowmarks.
			 */
			if (!erm)
				continue;
			/* Do not propagate non-row-locking row marks. */
			if (erm->markType != ROW_MARK_REFERENCE &&
				erm->markType != ROW_MARK_COPY)
			{
				/* YB_TODO(jason): move ybScan = (YbScanDesc)tsdesc; higher. */
				YbScanDesc ybScan = (YbScanDesc) tsdesc;

				ybScan->exec_params->rowmark = erm->markType;
				ybScan->exec_params->pg_wait_policy = erm->waitPolicy;
				YBSetRowLockPolicy(
					&ybScan->exec_params->docdb_wait_policy,
					erm->waitPolicy);
			}
		}
	}

	ybScan = (YbScanDesc)tsdesc;
	/*
	 * In the case of parallel scan we need to obtain boundaries from the pscan
	 * before the scan is executed. Also empty row from parallel range scan does
	 * not mean scan is done, it means the range is done and we need to pick up
	 * next. No rows from parallel range is possible, hence the loop.
	 */
	while (true)
	{
		/* Need to execute the request */
		if (!ybScan->is_exec_done)
		{
			/* Parallel mode: pick up parallel block first */
			if (ybScan->pscan != NULL)
			{
				YBParallelPartitionKeys parallel_scan = ybScan->pscan;
				const char *low_bound;
				size_t low_bound_size;
				const char *high_bound;
				size_t high_bound_size;
				/*
				 * If range is found, apply the boundaries, false means the scan
				 * is done for that worker.
				 */
				if (ybParallelNextRange(parallel_scan,
										&low_bound, &low_bound_size,
										&high_bound, &high_bound_size))
				{
					HandleYBStatus(YBCPgDmlBindRange(
						ybScan->handle, low_bound, low_bound_size, high_bound,
						high_bound_size));
					if (low_bound)
						pfree((void *) low_bound);
					if (high_bound)
						pfree((void *) high_bound);
				}
				else
					return NULL;
				/*
				 * Use unlimited fetch.
				 * Parallel scan range is already of limited size, it is
				 * unlikely to exceed the message size, but may save some RPCs.
				 */
				ybScan->exec_params->limit_use_default = true;
				ybScan->exec_params->yb_fetch_row_limit = 0;
				ybScan->exec_params->yb_fetch_size_limit = 0;
			}
			HandleYBStatus(YBCPgExecSelect(
				ybScan->handle, ybScan->exec_params));
			ybScan->is_exec_done = true;
		}

		/* capture all fetch allocations in the short-lived context */
		oldcontext = MemoryContextSwitchTo(econtext->ecxt_per_tuple_memory);
		ybFetchNext(ybScan->handle, slot,
					RelationGetRelid(node->ss.ss_currentRelation));
		MemoryContextSwitchTo(oldcontext);

		/*
		 * No more rows in parallel mode: repeat for next range, else break to
		 * return the result.
		 */
		if (TupIsNull(slot) && ybScan->pscan != NULL)
			ybScan->is_exec_done = false;
		else
			break;
	}
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
		ybc_heap_endscan(tsdesc);
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
		ybc_heap_endscan(tsdesc);
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
	EState	   *estate = node->ss.ps.state;
	YBParallelPartitionKeys pscan;

	pscan = shm_toc_allocate(pcxt->toc, node->pscan_len);
	yb_init_partition_key_data(pscan);
	shm_toc_insert(pcxt->toc, node->ss.ps.plan->plan_node_id, pscan);
	ybParallelPrepare(pscan, node->ss.ss_currentRelation,
					  &estate->yb_exec_params, true /* is_forward */);
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
	EState	   *estate = node->ss.ps.state;
	YBParallelPartitionKeys pscan = node->pscan;
	yb_init_partition_key_data(pscan);
	ybParallelPrepare(pscan, node->ss.ss_currentRelation,
					  &estate->yb_exec_params, true /* is_forward */);
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
	EState	   *estate = node->ss.ps.state;

	pscan = shm_toc_lookup(pwcxt->toc, node->ss.ps.plan->plan_node_id, false);
	ybParallelPrepare(pscan, node->ss.ss_currentRelation,
					  &estate->yb_exec_params, true /* is_forward */);
	node->pscan = pscan;
}
