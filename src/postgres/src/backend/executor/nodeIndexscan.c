/*-------------------------------------------------------------------------
 *
 * nodeIndexscan.c
 *	  Routines to support indexed scans of relations
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/nodeIndexscan.c
 *
 *-------------------------------------------------------------------------
 */
/*
 * INTERFACE ROUTINES
 *		ExecIndexScan			scans a relation using an index
 *		IndexNext				retrieve next tuple using index
 *		IndexNextWithReorder	same, but recheck ORDER BY expressions
 *		ExecInitIndexScan		creates and initializes state info.
 *		ExecReScanIndexScan		rescans the indexed relation.
 *		ExecEndIndexScan		releases all storage.
 *		ExecIndexMarkPos		marks scan position.
 *		ExecIndexRestrPos		restores scan position.
 *		ExecIndexScanEstimate	estimates DSM space needed for parallel index scan
 *		ExecIndexScanInitializeDSM initialize DSM for parallel indexscan
 *		ExecIndexScanReInitializeDSM reinitialize DSM for fresh scan
 *		ExecIndexScanInitializeWorker attach to DSM info in parallel worker
 */
#include "postgres.h"

#include "access/nbtree.h"
#include "access/relscan.h"
#include "access/tableam.h"
#include "catalog/pg_am.h"
#include "catalog/pg_opfamily.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "executor/execdebug.h"
#include "executor/nodeIndexscan.h"
#include "lib/pairingheap.h"
#include "miscadmin.h"
#include "nodes/nodeFuncs.h"
#include "utils/array.h"
#include "utils/datum.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"

/* Yugabyte includes */
#include "access/sysattr.h"
#include "access/xact.h"
#include "access/yb_scan.h"
#include "optimizer/clauses.h"

/*
 * When an ordering operator is used, tuples fetched from the index that
 * need to be reordered are queued in a pairing heap, as ReorderTuples.
 */
typedef struct
{
	pairingheap_node ph_node;
	HeapTuple	htup;
	Datum	   *orderbyvals;
	bool	   *orderbynulls;
} ReorderTuple;

static TupleTableSlot *IndexNext(IndexScanState *node);
static TupleTableSlot *IndexNextWithReorder(IndexScanState *node);
static void EvalOrderByExpressions(IndexScanState *node, ExprContext *econtext);
static bool IndexRecheck(IndexScanState *node, TupleTableSlot *slot);
static int	cmp_orderbyvals(const Datum *adist, const bool *anulls,
							const Datum *bdist, const bool *bnulls,
							IndexScanState *node);
static int	reorderqueue_cmp(const pairingheap_node *a,
							 const pairingheap_node *b, void *arg);
static void reorderqueue_push(IndexScanState *node, TupleTableSlot *slot,
							  Datum *orderbyvals, bool *orderbynulls);
static HeapTuple reorderqueue_pop(IndexScanState *node);
static void yb_init_index_scandesc(IndexScanState *node);
static void yb_agg_pushdown_init_scan_slot(IndexScanState *node);


/* ----------------------------------------------------------------
 *		IndexNext
 *
 *		Retrieve a tuple from the IndexScan node's currentRelation
 *		using the index specified in the IndexScanState information.
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
IndexNext(IndexScanState *node)
{
	EState	   *estate;
	ExprContext *econtext;
	ScanDirection direction;
	IndexScanDesc scandesc;
	TupleTableSlot *slot;

	/*
	 * extract necessary information from index scan node
	 */
	estate = node->ss.ps.state;
	direction = estate->es_direction;
	/* flip direction if this is an overall backward scan */
	if (ScanDirectionIsBackward(((IndexScan *) node->ss.ps.plan)->indexorderdir))
	{
		if (ScanDirectionIsForward(direction))
			direction = BackwardScanDirection;
		else if (ScanDirectionIsBackward(direction))
			direction = ForwardScanDirection;
	}
	/* YB relation scans are optimized for the "Don't care about order" direction. */
	if (IsYBRelation(node->ss.ss_currentRelation) &&
		ScanDirectionIsNoMovement(((IndexScan *) node->ss.ps.plan)->indexorderdir))
	{
		direction = NoMovementScanDirection;
	}
	scandesc = node->iss_ScanDesc;
	econtext = node->ss.ps.ps_ExprContext;
	slot = node->ss.ss_ScanTupleSlot;

	if (scandesc == NULL)
	{
		if (IsYugaByteEnabled() && node->yb_iss_aggrefs)
		{
			yb_agg_pushdown_init_scan_slot(node);
			/* Refresh the local pointer. */
			slot = node->ss.ss_ScanTupleSlot;
		}

		/*
		 * We reach here if the index scan is not parallel, or if we're
		 * serially executing an index scan that was planned to be parallel.
		 */
		scandesc = index_beginscan(node->ss.ss_currentRelation,
								   node->iss_RelationDesc,
								   estate->es_snapshot,
								   node->iss_NumScanKeys,
								   node->iss_NumOrderByKeys);

		node->iss_ScanDesc = scandesc;
		yb_init_index_scandesc(node);

		/*
		 * If no run-time keys to calculate or they are ready, go ahead and
		 * pass the scankeys to the index AM.
		 */
		if (node->iss_NumRuntimeKeys == 0 || node->iss_RuntimeKeysReady)
			index_rescan(scandesc,
						 node->iss_ScanKeys, node->iss_NumScanKeys,
						 node->iss_OrderByKeys, node->iss_NumOrderByKeys);
	}

	/*
	 * Set up any locking that happens at the time of the scan.
	 */
	if (IsYugaByteEnabled()) {
		IndexScan  *plan;
		scandesc->yb_exec_params = &estate->yb_exec_params;
		scandesc->yb_exec_params->rowmark = -1;

		// Add row marks.
		plan = castNode(IndexScan, node->ss.ps.plan);
		if (IsolationIsSerializable() || plan->yb_lock_mechanism == YB_LOCK_CLAUSE_ON_PK)
		{
			/*
			 * In case of SERIALIZABLE isolation level we have to take prefix range locks to disallow
			 * INSERTion of new rows that satisfy the query predicate. So, we set the rowmark on all
			 * read requests sent to tserver instead of locking each tuple one by one in LockRows node.
			 *
			 * For other isolation levels it's sometimes possible to take locks during the index scan
			 * as well.
			 */
			for (int i = 0;
				 estate->es_rowmarks && i < estate->es_range_table_size; i++)
			{
				ExecRowMark *erm = estate->es_rowmarks[i];
				/*
				 * YB_TODO: This block of code is broken on master (GH #20704).
				 * With PG commit f9eb7c14b08d2cc5eda62ffaf37a356c05e89b93,
				 * estate->es_rowmarks is an array with
				 * potentially NULL elements (previously, it was a list). As a
				 * temporary fix till #20704 is addressed, ignore any NULL
				 * element in es_rowmarks.
				 */
				if (!erm)
					continue;
				// Do not propogate non-row-locking row marks.
				if (erm->markType != ROW_MARK_REFERENCE && erm->markType != ROW_MARK_COPY) {
					scandesc->yb_exec_params->rowmark = erm->markType;
					scandesc->yb_exec_params->pg_wait_policy = erm->waitPolicy;
					scandesc->yb_exec_params->docdb_wait_policy =
						YBGetDocDBWaitPolicy(erm->waitPolicy);
				}
			}
		}

		/*
		 * Set reference to slot in scan desc so that YB amgettuple can use it
		 * during aggregate pushdown.
		 */
		if (scandesc->yb_aggrefs)
			scandesc->yb_agg_slot = slot;
	}

	/*
	 * ok, now that we have what we need, fetch the next tuple.
	 */
	MemoryContext oldcontext;

	/*
	 * To handle dead tuple for temp table, we shouldn't store its index
	 * in per-tuple memory context.
	 */
	if (IsYBRelation(node->ss.ss_currentRelation))
		oldcontext = MemoryContextSwitchTo(node->ss.ps.ps_ExprContext->ecxt_per_tuple_memory);

	while (index_getnext_slot(scandesc, direction, slot))
	{
		CHECK_FOR_INTERRUPTS();

		/*
		 * Index aggregate pushdown currently cannot support recheck,
		 * and this should have been prevented by earlier logic.
		 */
		if (IsYugaByteEnabled() && scandesc->yb_aggrefs)
			Assert(!scandesc->xs_recheck);

		/*
		 * If the index was lossy, we have to recheck the index quals using
		 * the fetched tuple.
		 */
		if (scandesc->xs_recheck)
		{
			econtext->ecxt_scantuple = slot;
			if (!ExecQual(node->indexqualorig, econtext))
			{
				ResetExprContext(econtext);
				/* Fails recheck, so drop it and loop back for another */
				InstrCountFiltered2(node, 1);
				continue;
			}
		}
		if (IsYBRelation(node->ss.ss_currentRelation))
			MemoryContextSwitchTo(oldcontext);
		return slot;
	}

	/*
	 * if we get here it means the index scan failed so we are at the end of
	 * the scan..
	 */
	node->iss_ReachedEnd = true;
	if (IsYBRelation(node->ss.ss_currentRelation))
		MemoryContextSwitchTo(oldcontext);
	return ExecClearTuple(slot);
}

/* ----------------------------------------------------------------
 *		IndexNextWithReorder
 *
 *		Like IndexNext, but this version can also re-check ORDER BY
 *		expressions, and reorder the tuples as necessary.
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
IndexNextWithReorder(IndexScanState *node)
{
	EState	   *estate;
	ExprContext *econtext;
	IndexScanDesc scandesc;
	TupleTableSlot *slot;
	ReorderTuple *topmost = NULL;
	bool		was_exact;
	Datum	   *lastfetched_vals;
	bool	   *lastfetched_nulls;
	int			cmp;

	estate = node->ss.ps.state;

	/*
	 * Only forward scan is supported with reordering.  Note: we can get away
	 * with just Asserting here because the system will not try to run the
	 * plan backwards if ExecSupportsBackwardScan() says it won't work.
	 * Currently, that is guaranteed because no index AMs support both
	 * amcanorderbyop and amcanbackward; if any ever do,
	 * ExecSupportsBackwardScan() will need to consider indexorderbys
	 * explicitly.
	 */
	Assert(!ScanDirectionIsBackward(((IndexScan *) node->ss.ps.plan)->indexorderdir));
	Assert(ScanDirectionIsForward(estate->es_direction));

	scandesc = node->iss_ScanDesc;
	econtext = node->ss.ps.ps_ExprContext;
	slot = node->ss.ss_ScanTupleSlot;

	if (scandesc == NULL)
	{
		/*
		 * We reach here if the index scan is not parallel, or if we're
		 * serially executing an index scan that was planned to be parallel.
		 */
		scandesc = index_beginscan(node->ss.ss_currentRelation,
								   node->iss_RelationDesc,
								   estate->es_snapshot,
								   node->iss_NumScanKeys,
								   node->iss_NumOrderByKeys);

		node->iss_ScanDesc = scandesc;
		yb_init_index_scandesc(node);

		/*
		 * If no run-time keys to calculate or they are ready, go ahead and
		 * pass the scankeys to the index AM.
		 */
		if (node->iss_NumRuntimeKeys == 0 || node->iss_RuntimeKeysReady)
			index_rescan(scandesc,
						 node->iss_ScanKeys, node->iss_NumScanKeys,
						 node->iss_OrderByKeys, node->iss_NumOrderByKeys);
	}

	for (;;)
	{
		CHECK_FOR_INTERRUPTS();

		/*
		 * Check the reorder queue first.  If the topmost tuple in the queue
		 * has an ORDER BY value smaller than (or equal to) the value last
		 * returned by the index, we can return it now.
		 */
		if (!pairingheap_is_empty(node->iss_ReorderQueue))
		{
			topmost = (ReorderTuple *) pairingheap_first(node->iss_ReorderQueue);

			if (node->iss_ReachedEnd ||
				cmp_orderbyvals(topmost->orderbyvals,
								topmost->orderbynulls,
								scandesc->xs_orderbyvals,
								scandesc->xs_orderbynulls,
								node) <= 0)
			{
				HeapTuple	tuple;

				tuple = reorderqueue_pop(node);

				/* Pass 'true', as the tuple in the queue is a palloc'd copy */
				ExecForceStoreHeapTuple(tuple, slot, true);
				return slot;
			}
		}
		else if (node->iss_ReachedEnd)
		{
			/* Queue is empty, and no more tuples from index.  We're done. */
			return ExecClearTuple(slot);
		}

		/*
		 * Fetch next tuple from the index.
		 */
next_indextuple:
		if (!index_getnext_slot(scandesc, ForwardScanDirection, slot))
		{
			/*
			 * No more tuples from the index.  But we still need to drain any
			 * remaining tuples from the queue before we're done.
			 */
			node->iss_ReachedEnd = true;
			continue;
		}

		/*
		 * If the index was lossy, we have to recheck the index quals and
		 * ORDER BY expressions using the fetched tuple.
		 */
		if (scandesc->xs_recheck)
		{
			econtext->ecxt_scantuple = slot;
			if (!ExecQualAndReset(node->indexqualorig, econtext))
			{
				/* Fails recheck, so drop it and loop back for another */
				InstrCountFiltered2(node, 1);
				/* allow this loop to be cancellable */
				CHECK_FOR_INTERRUPTS();
				goto next_indextuple;
			}
		}

		if (scandesc->xs_recheckorderby)
		{
			econtext->ecxt_scantuple = slot;
			ResetExprContext(econtext);
			EvalOrderByExpressions(node, econtext);

			/*
			 * Was the ORDER BY value returned by the index accurate?  The
			 * recheck flag means that the index can return inaccurate values,
			 * but then again, the value returned for any particular tuple
			 * could also be exactly correct.  Compare the value returned by
			 * the index with the recalculated value.  (If the value returned
			 * by the index happened to be exact right, we can often avoid
			 * pushing the tuple to the queue, just to pop it back out again.)
			 */
			cmp = cmp_orderbyvals(node->iss_OrderByValues,
								  node->iss_OrderByNulls,
								  scandesc->xs_orderbyvals,
								  scandesc->xs_orderbynulls,
								  node);
			if (cmp < 0)
				elog(ERROR, "index returned tuples in wrong order");
			else if (cmp == 0)
				was_exact = true;
			else
				was_exact = false;
			lastfetched_vals = node->iss_OrderByValues;
			lastfetched_nulls = node->iss_OrderByNulls;
		}
		else
		{
			was_exact = true;
			lastfetched_vals = scandesc->xs_orderbyvals;
			lastfetched_nulls = scandesc->xs_orderbynulls;
		}

		/*
		 * Can we return this tuple immediately, or does it need to be pushed
		 * to the reorder queue?  If the ORDER BY expression values returned
		 * by the index were inaccurate, we can't return it yet, because the
		 * next tuple from the index might need to come before this one. Also,
		 * we can't return it yet if there are any smaller tuples in the queue
		 * already.
		 */
		if (!was_exact || (topmost && cmp_orderbyvals(lastfetched_vals,
													  lastfetched_nulls,
													  topmost->orderbyvals,
													  topmost->orderbynulls,
													  node) > 0))
		{
			/* Put this tuple to the queue */
			reorderqueue_push(node, slot, lastfetched_vals, lastfetched_nulls);
			continue;
		}
		else
		{
			/* Can return this tuple immediately. */
			return slot;
		}
	}

	/*
	 * if we get here it means the index scan failed so we are at the end of
	 * the scan..
	 */
	return ExecClearTuple(slot);
}

/*
 * Calculate the expressions in the ORDER BY clause, based on the heap tuple.
 */
static void
EvalOrderByExpressions(IndexScanState *node, ExprContext *econtext)
{
	int			i;
	ListCell   *l;
	MemoryContext oldContext;

	oldContext = MemoryContextSwitchTo(econtext->ecxt_per_tuple_memory);

	i = 0;
	foreach(l, node->indexorderbyorig)
	{
		ExprState  *orderby = (ExprState *) lfirst(l);

		node->iss_OrderByValues[i] = ExecEvalExpr(orderby,
												  econtext,
												  &node->iss_OrderByNulls[i]);
		i++;
	}

	MemoryContextSwitchTo(oldContext);
}

/*
 * IndexRecheck -- access method routine to recheck a tuple in EvalPlanQual
 */
static bool
IndexRecheck(IndexScanState *node, TupleTableSlot *slot)
{
	ExprContext *econtext;

	/*
	 * extract necessary information from index scan node
	 */
	econtext = node->ss.ps.ps_ExprContext;

	/* Does the tuple meet the indexqual condition? */
	econtext->ecxt_scantuple = slot;
	return ExecQualAndReset(node->indexqualorig, econtext);
}


/*
 * Compare ORDER BY expression values.
 */
static int
cmp_orderbyvals(const Datum *adist, const bool *anulls,
				const Datum *bdist, const bool *bnulls,
				IndexScanState *node)
{
	int			i;
	int			result;

	for (i = 0; i < node->iss_NumOrderByKeys; i++)
	{
		SortSupport ssup = &node->iss_SortSupport[i];

		/*
		 * Handle nulls.  We only need to support NULLS LAST ordering, because
		 * match_pathkeys_to_index() doesn't consider indexorderby
		 * implementation otherwise.
		 */
		if (anulls[i] && !bnulls[i])
			return 1;
		else if (!anulls[i] && bnulls[i])
			return -1;
		else if (anulls[i] && bnulls[i])
			return 0;

		result = ssup->comparator(adist[i], bdist[i], ssup);
		if (result != 0)
			return result;
	}

	return 0;
}

/*
 * Pairing heap provides getting topmost (greatest) element while KNN provides
 * ascending sort.  That's why we invert the sort order.
 */
static int
reorderqueue_cmp(const pairingheap_node *a, const pairingheap_node *b,
				 void *arg)
{
	ReorderTuple *rta = (ReorderTuple *) a;
	ReorderTuple *rtb = (ReorderTuple *) b;
	IndexScanState *node = (IndexScanState *) arg;

	/* exchange argument order to invert the sort order */
	return cmp_orderbyvals(rtb->orderbyvals, rtb->orderbynulls,
						   rta->orderbyvals, rta->orderbynulls,
						   node);
}

/*
 * Helper function to push a tuple to the reorder queue.
 */
static void
reorderqueue_push(IndexScanState *node, TupleTableSlot *slot,
				  Datum *orderbyvals, bool *orderbynulls)
{
	IndexScanDesc scandesc = node->iss_ScanDesc;
	EState	   *estate = node->ss.ps.state;
	MemoryContext oldContext = MemoryContextSwitchTo(estate->es_query_cxt);
	ReorderTuple *rt;
	int			i;

	rt = (ReorderTuple *) palloc(sizeof(ReorderTuple));
	rt->htup = ExecCopySlotHeapTuple(slot);
	rt->orderbyvals =
		(Datum *) palloc(sizeof(Datum) * scandesc->numberOfOrderBys);
	rt->orderbynulls =
		(bool *) palloc(sizeof(bool) * scandesc->numberOfOrderBys);
	for (i = 0; i < node->iss_NumOrderByKeys; i++)
	{
		if (!orderbynulls[i])
			rt->orderbyvals[i] = datumCopy(orderbyvals[i],
										   node->iss_OrderByTypByVals[i],
										   node->iss_OrderByTypLens[i]);
		else
			rt->orderbyvals[i] = (Datum) 0;
		rt->orderbynulls[i] = orderbynulls[i];
	}
	pairingheap_add(node->iss_ReorderQueue, &rt->ph_node);

	MemoryContextSwitchTo(oldContext);
}

/*
 * Helper function to pop the next tuple from the reorder queue.
 */
static HeapTuple
reorderqueue_pop(IndexScanState *node)
{
	HeapTuple	result;
	ReorderTuple *topmost;
	int			i;

	topmost = (ReorderTuple *) pairingheap_remove_first(node->iss_ReorderQueue);

	result = topmost->htup;
	for (i = 0; i < node->iss_NumOrderByKeys; i++)
	{
		if (!node->iss_OrderByTypByVals[i] && !topmost->orderbynulls[i])
			pfree(DatumGetPointer(topmost->orderbyvals[i]));
	}
	pfree(topmost->orderbyvals);
	pfree(topmost->orderbynulls);
	pfree(topmost);

	return result;
}


/* ----------------------------------------------------------------
 *		ExecIndexScan(node)
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
ExecIndexScan(PlanState *pstate)
{
	IndexScanState *node = castNode(IndexScanState, pstate);

	/*
	 * If we have runtime keys and they've not already been set up, do it now.
	 */
	if (node->iss_NumRuntimeKeys != 0 && !node->iss_RuntimeKeysReady)
		ExecReScan((PlanState *) node);

	if (node->iss_NumOrderByKeys > 0)
		return ExecScan(&node->ss,
						(ExecScanAccessMtd) IndexNextWithReorder,
						(ExecScanRecheckMtd) IndexRecheck);
	else
		return ExecScan(&node->ss,
						(ExecScanAccessMtd) IndexNext,
						(ExecScanRecheckMtd) IndexRecheck);
}

/* ----------------------------------------------------------------
 *		ExecReScanIndexScan(node)
 *
 *		Recalculates the values of any scan keys whose value depends on
 *		information known at runtime, then rescans the indexed relation.
 *
 *		Updating the scan key was formerly done separately in
 *		ExecUpdateIndexScanKeys. Integrating it into ReScan makes
 *		rescans of indices and relations/general streams more uniform.
 * ----------------------------------------------------------------
 */
void
ExecReScanIndexScan(IndexScanState *node)
{
	/*
	 * If we are doing runtime key calculations (ie, any of the index key
	 * values weren't simple Consts), compute the new key values.  But first,
	 * reset the context so we don't leak memory as each outer tuple is
	 * scanned.  Note this assumes that we will recalculate *all* runtime keys
	 * on each call.
	 */
	if (node->iss_NumRuntimeKeys != 0)
	{
		ExprContext *econtext = node->iss_RuntimeContext;

		ResetExprContext(econtext);
		ExecIndexEvalRuntimeKeys(econtext,
								 node->iss_RuntimeKeys,
								 node->iss_NumRuntimeKeys);
	}
	node->iss_RuntimeKeysReady = true;

	/* flush the reorder queue */
	if (node->iss_ReorderQueue)
	{
		HeapTuple	tuple;

		while (!pairingheap_is_empty(node->iss_ReorderQueue))
		{
			tuple = reorderqueue_pop(node);
			heap_freetuple(tuple);
		}
	}

	/* reset index scan */
	if (node->iss_ScanDesc)
	{
		yb_init_index_scandesc(node);
		index_rescan(node->iss_ScanDesc,
					 node->iss_ScanKeys, node->iss_NumScanKeys,
					 node->iss_OrderByKeys, node->iss_NumOrderByKeys);
	}
	node->iss_ReachedEnd = false;

	ExecScanReScan(&node->ss);
}


/*
 * ExecIndexEvalRuntimeKeys
 *		Evaluate any runtime key values, and update the scankeys.
 */
void
ExecIndexEvalRuntimeKeys(ExprContext *econtext,
						 IndexRuntimeKeyInfo *runtimeKeys, int numRuntimeKeys)
{
	int			j;
	MemoryContext oldContext;

	/* We want to keep the key values in per-tuple memory */
	oldContext = MemoryContextSwitchTo(econtext->ecxt_per_tuple_memory);

	for (j = 0; j < numRuntimeKeys; j++)
	{
		ScanKey		scan_key = runtimeKeys[j].scan_key;
		ExprState  *key_expr = runtimeKeys[j].key_expr;
		Datum		scanvalue;
		bool		isNull;

		/*
		 * For each run-time key, extract the run-time expression and evaluate
		 * it with respect to the current context.  We then stick the result
		 * into the proper scan key.
		 *
		 * Note: the result of the eval could be a pass-by-ref value that's
		 * stored in some outer scan's tuple, not in
		 * econtext->ecxt_per_tuple_memory.  We assume that the outer tuple
		 * will stay put throughout our scan.  If this is wrong, we could copy
		 * the result into our context explicitly, but I think that's not
		 * necessary.
		 *
		 * It's also entirely possible that the result of the eval is a
		 * toasted value.  In this case we should forcibly detoast it, to
		 * avoid repeat detoastings each time the value is examined by an
		 * index support function.
		 */
		scanvalue = ExecEvalExpr(key_expr,
								 econtext,
								 &isNull);
		if (isNull)
		{
			scan_key->sk_argument = scanvalue;
			scan_key->sk_flags |= SK_ISNULL;
		}
		else
		{
			if (runtimeKeys[j].key_toastable)
				scanvalue = PointerGetDatum(PG_DETOAST_DATUM(scanvalue));
			scan_key->sk_argument = scanvalue;
			scan_key->sk_flags &= ~SK_ISNULL;
		}
	}

	MemoryContextSwitchTo(oldContext);
}

/*
 * ExecIndexEvalArrayKeys
 *		Evaluate any array key values, and set up to iterate through arrays.
 *
 * Returns true if there are array elements to consider; false means there
 * is at least one null or empty array, so no match is possible.  On true
 * result, the scankeys are initialized with the first elements of the arrays.
 */
bool
ExecIndexEvalArrayKeys(ExprContext *econtext,
					   IndexArrayKeyInfo *arrayKeys, int numArrayKeys)
{
	bool		result = true;
	int			j;
	MemoryContext oldContext;

	/* We want to keep the arrays in per-tuple memory */
	oldContext = MemoryContextSwitchTo(econtext->ecxt_per_tuple_memory);

	for (j = 0; j < numArrayKeys; j++)
	{
		ScanKey		scan_key = arrayKeys[j].scan_key;
		ExprState  *array_expr = arrayKeys[j].array_expr;
		Datum		arraydatum;
		bool		isNull;
		ArrayType  *arrayval;
		int16		elmlen;
		bool		elmbyval;
		char		elmalign;
		int			num_elems;
		Datum	   *elem_values;
		bool	   *elem_nulls;

		/*
		 * Compute and deconstruct the array expression. (Notes in
		 * ExecIndexEvalRuntimeKeys() apply here too.)
		 */
		arraydatum = ExecEvalExpr(array_expr,
								  econtext,
								  &isNull);
		if (isNull)
		{
			result = false;
			break;				/* no point in evaluating more */
		}
		arrayval = DatumGetArrayTypeP(arraydatum);
		/* We could cache this data, but not clear it's worth it */
		get_typlenbyvalalign(ARR_ELEMTYPE(arrayval),
							 &elmlen, &elmbyval, &elmalign);
		deconstruct_array(arrayval,
						  ARR_ELEMTYPE(arrayval),
						  elmlen, elmbyval, elmalign,
						  &elem_values, &elem_nulls, &num_elems);
		if (num_elems <= 0)
		{
			result = false;
			break;				/* no point in evaluating more */
		}

		/*
		 * Note: we expect the previous array data, if any, to be
		 * automatically freed by resetting the per-tuple context; hence no
		 * pfree's here.
		 */
		arrayKeys[j].elem_values = elem_values;
		arrayKeys[j].elem_nulls = elem_nulls;
		arrayKeys[j].num_elems = num_elems;
		scan_key->sk_argument = elem_values[0];
		if (elem_nulls[0])
			scan_key->sk_flags |= SK_ISNULL;
		else
			scan_key->sk_flags &= ~SK_ISNULL;
		arrayKeys[j].next_elem = 1;
	}

	MemoryContextSwitchTo(oldContext);

	return result;
}

/*
 * ExecIndexAdvanceArrayKeys
 *		Advance to the next set of array key values, if any.
 *
 * Returns true if there is another set of values to consider, false if not.
 * On true result, the scankeys are initialized with the next set of values.
 */
bool
ExecIndexAdvanceArrayKeys(IndexArrayKeyInfo *arrayKeys, int numArrayKeys)
{
	bool		found = false;
	int			j;

	/*
	 * Note we advance the rightmost array key most quickly, since it will
	 * correspond to the lowest-order index column among the available
	 * qualifications.  This is hypothesized to result in better locality of
	 * access in the index.
	 */
	for (j = numArrayKeys - 1; j >= 0; j--)
	{
		ScanKey		scan_key = arrayKeys[j].scan_key;
		int			next_elem = arrayKeys[j].next_elem;
		int			num_elems = arrayKeys[j].num_elems;
		Datum	   *elem_values = arrayKeys[j].elem_values;
		bool	   *elem_nulls = arrayKeys[j].elem_nulls;

		if (next_elem >= num_elems)
		{
			next_elem = 0;
			found = false;		/* need to advance next array key */
		}
		else
			found = true;
		scan_key->sk_argument = elem_values[next_elem];
		if (elem_nulls[next_elem])
			scan_key->sk_flags |= SK_ISNULL;
		else
			scan_key->sk_flags &= ~SK_ISNULL;
		arrayKeys[j].next_elem = next_elem + 1;
		if (found)
			break;
	}

	return found;
}


/* ----------------------------------------------------------------
 *		ExecEndIndexScan
 * ----------------------------------------------------------------
 */
void
ExecEndIndexScan(IndexScanState *node)
{
	Relation	indexRelationDesc;
	IndexScanDesc indexScanDesc;

	/*
	 * extract information from the node
	 */
	indexRelationDesc = node->iss_RelationDesc;
	indexScanDesc = node->iss_ScanDesc;

	/*
	 * Free the exprcontext(s) ... now dead code, see ExecFreeExprContext
	 */
#ifdef NOT_USED
	ExecFreeExprContext(&node->ss.ps);
	if (node->iss_RuntimeContext)
		FreeExprContext(node->iss_RuntimeContext, true);
#endif

	/*
	 * clear out tuple table slots
	 */
	if (node->ss.ps.ps_ResultTupleSlot)
		ExecClearTuple(node->ss.ps.ps_ResultTupleSlot);
	ExecClearTuple(node->ss.ss_ScanTupleSlot);

	/*
	 * close the index relation (no-op if we didn't open it)
	 */
	if (indexScanDesc)
		index_endscan(indexScanDesc);
	if (indexRelationDesc)
		index_close(indexRelationDesc, NoLock);
}

/* ----------------------------------------------------------------
 *		ExecIndexMarkPos
 *
 * Note: we assume that no caller attempts to set a mark before having read
 * at least one tuple.  Otherwise, iss_ScanDesc might still be NULL.
 * ----------------------------------------------------------------
 */
void
ExecIndexMarkPos(IndexScanState *node)
{
	EState	   *estate = node->ss.ps.state;
	EPQState   *epqstate = estate->es_epq_active;

	if (epqstate != NULL)
	{
		/*
		 * We are inside an EvalPlanQual recheck.  If a test tuple exists for
		 * this relation, then we shouldn't access the index at all.  We would
		 * instead need to save, and later restore, the state of the
		 * relsubs_done flag, so that re-fetching the test tuple is possible.
		 * However, given the assumption that no caller sets a mark at the
		 * start of the scan, we can only get here with relsubs_done[i]
		 * already set, and so no state need be saved.
		 */
		Index		scanrelid = ((Scan *) node->ss.ps.plan)->scanrelid;

		Assert(scanrelid > 0);
		if (epqstate->relsubs_slot[scanrelid - 1] != NULL ||
			epqstate->relsubs_rowmark[scanrelid - 1] != NULL)
		{
			/* Verify the claim above */
			if (!epqstate->relsubs_done[scanrelid - 1])
				elog(ERROR, "unexpected ExecIndexMarkPos call in EPQ recheck");
			return;
		}
	}

	index_markpos(node->iss_ScanDesc);
}

/* ----------------------------------------------------------------
 *		ExecIndexRestrPos
 * ----------------------------------------------------------------
 */
void
ExecIndexRestrPos(IndexScanState *node)
{
	EState	   *estate = node->ss.ps.state;
	EPQState   *epqstate = estate->es_epq_active;

	if (estate->es_epq_active != NULL)
	{
		/* See comments in ExecIndexMarkPos */
		Index		scanrelid = ((Scan *) node->ss.ps.plan)->scanrelid;

		Assert(scanrelid > 0);
		if (epqstate->relsubs_slot[scanrelid - 1] != NULL ||
			epqstate->relsubs_rowmark[scanrelid - 1] != NULL)
		{
			/* Verify the claim above */
			if (!epqstate->relsubs_done[scanrelid - 1])
				elog(ERROR, "unexpected ExecIndexRestrPos call in EPQ recheck");
			return;
		}
	}

	index_restrpos(node->iss_ScanDesc);
}

/* ----------------------------------------------------------------
 *		ExecInitIndexScan
 *
 *		Initializes the index scan's state information, creates
 *		scan keys, and opens the base and index relations.
 *
 *		Note: index scans have 2 sets of state information because
 *			  we have to keep track of the base relation and the
 *			  index relation.
 * ----------------------------------------------------------------
 */
IndexScanState *
ExecInitIndexScan(IndexScan *node, EState *estate, int eflags)
{
	IndexScanState *indexstate;
	Relation	currentRelation;
	LOCKMODE	lockmode;

	/*
	 * create state structure
	 */
	indexstate = makeNode(IndexScanState);
	indexstate->ss.ps.plan = (Plan *) node;
	indexstate->ss.ps.state = estate;
	indexstate->ss.ps.ExecProcNode = ExecIndexScan;

	/*
	 * Miscellaneous initialization
	 *
	 * create expression context for node
	 */
	ExecAssignExprContext(estate, &indexstate->ss.ps);

	/*
	 * open the scan relation
	 */
	currentRelation = ExecOpenScanRelation(estate, node->scan.scanrelid, eflags);

	indexstate->ss.ss_currentRelation = currentRelation;
	indexstate->ss.ss_currentScanDesc = NULL;	/* no heap scan here */

	/*
	 * get the scan type from the relation descriptor.
	 */
	ExecInitScanTupleSlot(estate, &indexstate->ss,
						  RelationGetDescr(currentRelation),
						  table_slot_callbacks(currentRelation));

	/*
	 * Initialize result type and projection.
	 */
	ExecInitResultTypeTL(&indexstate->ss.ps);
	ExecAssignScanProjectionInfo(&indexstate->ss);

	/*
	 * initialize child expressions
	 *
	 * Note: we don't initialize all of the indexqual expression, only the
	 * sub-parts corresponding to runtime keys (see below).  Likewise for
	 * indexorderby, if any.  But the indexqualorig expression is always
	 * initialized even though it will only be used in some uncommon cases ---
	 * would be nice to improve that.  (Problem is that any SubPlans present
	 * in the expression must be found now...)
	 */
	indexstate->ss.ps.qual =
		ExecInitQual(node->scan.plan.qual, (PlanState *) indexstate);
	indexstate->indexqualorig =
		ExecInitQual(node->indexqualorig, (PlanState *) indexstate);
	indexstate->indexorderbyorig =
		ExecInitExprList(node->indexorderbyorig, (PlanState *) indexstate);

	/*
	 * If we are just doing EXPLAIN (ie, aren't going to run the plan), stop
	 * here.  This allows an index-advisor plugin to EXPLAIN a plan containing
	 * references to nonexistent indexes.
	 *
	 * YB note: For aggregate pushdown, we need recheck knowledge to determine
	 * whether aggregates can be pushed down or not.  At the time of writing,
	 * - aggregate pushdown only supports YB relations
	 * - there cannot be a mix of non-YB tables and YB indexes, and vice versa
	 * Use those assumptions to avoid the perf hit on EXPLAIN non-YB relations.
	 */
	if (eflags & EXEC_FLAG_EXPLAIN_ONLY)
		if (!(IsYBRelation(currentRelation) &&
			  (eflags & EXEC_FLAG_YB_AGG_PARENT)))
			return indexstate;

	/* Open the index relation. */
	lockmode = exec_rt_fetch(node->scan.scanrelid, estate)->rellockmode;
	indexstate->iss_RelationDesc = index_open(node->indexid, lockmode);

	/*
	 * Initialize index-specific scan state
	 */
	indexstate->iss_RuntimeKeysReady = false;
	indexstate->iss_RuntimeKeys = NULL;
	indexstate->iss_NumRuntimeKeys = 0;

	/*
	 * build the index scan keys from the index qualification
	 */
	ExecIndexBuildScanKeys((PlanState *) indexstate,
						   indexstate->iss_RelationDesc,
						   node->indexqual,
						   false,
						   &indexstate->iss_ScanKeys,
						   &indexstate->iss_NumScanKeys,
						   &indexstate->iss_RuntimeKeys,
						   &indexstate->iss_NumRuntimeKeys,
						   NULL,	/* no ArrayKeys */
						   NULL);

	/*
	 * For aggregate pushdown purposes, using the scan keys, determine ahead of
	 * beginning the scan whether indexqual recheck might happen, and pass that
	 * information up to the aggregate node.  Only attempt this for YB
	 * relations since pushdown is not supported otherwise.
	 */
	if (IsYBRelation(indexstate->iss_RelationDesc) &&
		(eflags & EXEC_FLAG_YB_AGG_PARENT))
	{
		indexstate->yb_iss_might_recheck =
			yb_index_might_recheck(currentRelation,
								   indexstate->iss_RelationDesc,
								   false /* xs_want_itup */,
								   indexstate->iss_ScanKeys,
								   indexstate->iss_NumScanKeys);

		/* Got the info for aggregate pushdown.  EXPLAIN can return now. */
		if  (eflags & EXEC_FLAG_EXPLAIN_ONLY)
			return indexstate;
	}

	/*
	 * any ORDER BY exprs have to be turned into scankeys in the same way
	 */
	ExecIndexBuildScanKeys((PlanState *) indexstate,
						   indexstate->iss_RelationDesc,
						   node->indexorderby,
						   true,
						   &indexstate->iss_OrderByKeys,
						   &indexstate->iss_NumOrderByKeys,
						   &indexstate->iss_RuntimeKeys,
						   &indexstate->iss_NumRuntimeKeys,
						   NULL,	/* no ArrayKeys */
						   NULL);

	/* Initialize sort support, if we need to re-check ORDER BY exprs */
	if (indexstate->iss_NumOrderByKeys > 0)
	{
		int			numOrderByKeys = indexstate->iss_NumOrderByKeys;
		int			i;
		ListCell   *lco;
		ListCell   *lcx;

		/*
		 * Prepare sort support, and look up the data type for each ORDER BY
		 * expression.
		 */
		Assert(numOrderByKeys == list_length(node->indexorderbyops));
		Assert(numOrderByKeys == list_length(node->indexorderbyorig));
		indexstate->iss_SortSupport = (SortSupportData *)
			palloc0(numOrderByKeys * sizeof(SortSupportData));
		indexstate->iss_OrderByTypByVals = (bool *)
			palloc(numOrderByKeys * sizeof(bool));
		indexstate->iss_OrderByTypLens = (int16 *)
			palloc(numOrderByKeys * sizeof(int16));
		i = 0;
		forboth(lco, node->indexorderbyops, lcx, node->indexorderbyorig)
		{
			Oid			orderbyop = lfirst_oid(lco);
			Node	   *orderbyexpr = (Node *) lfirst(lcx);
			Oid			orderbyType = exprType(orderbyexpr);
			Oid			orderbyColl = exprCollation(orderbyexpr);
			SortSupport orderbysort = &indexstate->iss_SortSupport[i];

			/* Initialize sort support */
			orderbysort->ssup_cxt = GetCurrentMemoryContext();
			orderbysort->ssup_collation = orderbyColl;
			/* See cmp_orderbyvals() comments on NULLS LAST */
			orderbysort->ssup_nulls_first = false;
			/* ssup_attno is unused here and elsewhere */
			orderbysort->ssup_attno = 0;
			/* No abbreviation */
			orderbysort->abbreviate = false;
			PrepareSortSupportFromOrderingOp(orderbyop, orderbysort);

			get_typlenbyval(orderbyType,
							&indexstate->iss_OrderByTypLens[i],
							&indexstate->iss_OrderByTypByVals[i]);
			i++;
		}

		/* allocate arrays to hold the re-calculated distances */
		indexstate->iss_OrderByValues = (Datum *)
			palloc(numOrderByKeys * sizeof(Datum));
		indexstate->iss_OrderByNulls = (bool *)
			palloc(numOrderByKeys * sizeof(bool));

		/* and initialize the reorder queue */
		indexstate->iss_ReorderQueue = pairingheap_allocate(reorderqueue_cmp,
															indexstate);
	}

	/*
	 * If we have runtime keys, we need an ExprContext to evaluate them. The
	 * node's standard context won't do because we want to reset that context
	 * for every tuple.  So, build another context just like the other one...
	 * -tgl 7/11/00
	 */
	if (indexstate->iss_NumRuntimeKeys != 0)
	{
		ExprContext *stdecontext = indexstate->ss.ps.ps_ExprContext;

		ExecAssignExprContext(estate, &indexstate->ss.ps);
		indexstate->iss_RuntimeContext = indexstate->ss.ps.ps_ExprContext;
		indexstate->ss.ps.ps_ExprContext = stdecontext;
	}
	else
	{
		indexstate->iss_RuntimeContext = NULL;
	}

	/*
	 * all done.
	 */
	return indexstate;
}


/*
 * ExecIndexBuildScanKeys
 *		Build the index scan keys from the index qualification expressions
 *
 * The index quals are passed to the index AM in the form of a ScanKey array.
 * This routine sets up the ScanKeys, fills in all constant fields of the
 * ScanKeys, and prepares information about the keys that have non-constant
 * comparison values.  We divide index qual expressions into five types:
 *
 * 1. Simple operator with constant comparison value ("indexkey op constant").
 * For these, we just fill in a ScanKey containing the constant value.
 *
 * 2. Simple operator with non-constant value ("indexkey op expression").
 * For these, we create a ScanKey with everything filled in except the
 * expression value, and set up an IndexRuntimeKeyInfo struct to drive
 * evaluation of the expression at the right times.
 *
 * 3. RowCompareExpr ("(indexkey, indexkey, ...) op (expr, expr, ...)").
 * For these, we create a header ScanKey plus a subsidiary ScanKey array,
 * as specified in access/skey.h.  The elements of the row comparison
 * can have either constant or non-constant comparison values.
 * YB: there is also a row array comparison case (see access/skey.h):
 * ("ROW(indexkey, indexkey, ...) op ANY Array(rowexpr, rowexpr, rowexpr...)").
 * Each rowexpr is expected to have the same set of types in it as the indexkeys
 * do in the lhs row.
 * These are found in the case of batched nested loop joins on multiple keys for
 * now.
 * For now, these cases are generated for batched nested loop joins in
 * yb_zip_batched_exprs() in restrictinfo.c during indexscan
 * plan node generation.
 *
 * 4. ScalarArrayOpExpr ("indexkey op ANY (array-expression)").  If the index
 * supports amsearcharray, we handle these the same as simple operators,
 * setting the SK_SEARCHARRAY flag to tell the AM to handle them.  Otherwise,
 * we create a ScanKey with everything filled in except the comparison value,
 * and set up an IndexArrayKeyInfo struct to drive processing of the qual.
 * (Note that if we use an IndexArrayKeyInfo struct, the array expression is
 * always treated as requiring runtime evaluation, even if it's a constant.)
 *
 * 5. NullTest ("indexkey IS NULL/IS NOT NULL").  We just fill in the
 * ScanKey properly.
 *
 * This code is also used to prepare ORDER BY expressions for amcanorderbyop
 * indexes.  The behavior is exactly the same, except that we have to look up
 * the operator differently.  Note that only cases 1 and 2 are currently
 * possible for ORDER BY.
 *
 * Input params are:
 *
 * planstate: executor state node we are working for
 * index: the index we are building scan keys for
 * quals: indexquals (or indexorderbys) expressions
 * isorderby: true if processing ORDER BY exprs, false if processing quals
 * *runtimeKeys: ptr to pre-existing IndexRuntimeKeyInfos, or NULL if none
 * *numRuntimeKeys: number of pre-existing runtime keys
 *
 * Output params are:
 *
 * *scanKeys: receives ptr to array of ScanKeys
 * *numScanKeys: receives number of scankeys
 * *runtimeKeys: receives ptr to array of IndexRuntimeKeyInfos, or NULL if none
 * *numRuntimeKeys: receives number of runtime keys
 * *arrayKeys: receives ptr to array of IndexArrayKeyInfos, or NULL if none
 * *numArrayKeys: receives number of array keys
 *
 * Caller may pass NULL for arrayKeys and numArrayKeys to indicate that
 * IndexArrayKeyInfos are not supported.
 */
void
ExecIndexBuildScanKeys(PlanState *planstate, Relation index,
					   List *quals, bool isorderby,
					   ScanKey *scanKeys, int *numScanKeys,
					   IndexRuntimeKeyInfo **runtimeKeys, int *numRuntimeKeys,
					   IndexArrayKeyInfo **arrayKeys, int *numArrayKeys)
{
	ListCell   *qual_cell;
	ScanKey		scan_keys;
	IndexRuntimeKeyInfo *runtime_keys;
	IndexArrayKeyInfo *array_keys;
	int			n_scan_keys;
	int			n_runtime_keys;
	int			max_runtime_keys;
	int			n_array_keys;
	int			j;

	/* Allocate array for ScanKey structs: one per qual */
	n_scan_keys = list_length(quals);
	scan_keys = (ScanKey) palloc(n_scan_keys * sizeof(ScanKeyData));

	/*
	 * runtime_keys array is dynamically resized as needed.  We handle it this
	 * way so that the same runtime keys array can be shared between
	 * indexquals and indexorderbys, which will be processed in separate calls
	 * of this function.  Caller must be sure to pass in NULL/0 for first
	 * call.
	 */
	runtime_keys = *runtimeKeys;
	n_runtime_keys = max_runtime_keys = *numRuntimeKeys;

	/* Allocate array_keys as large as it could possibly need to be */
	array_keys = (IndexArrayKeyInfo *)
		palloc0(n_scan_keys * sizeof(IndexArrayKeyInfo));
	n_array_keys = 0;

	/*
	 * for each opclause in the given qual, convert the opclause into a single
	 * scan key
	 */
	j = 0;
	foreach(qual_cell, quals)
	{
		Expr	   *clause = (Expr *) lfirst(qual_cell);
		ScanKey		this_scan_key = &scan_keys[j++];
		Oid			opno;		/* operator's OID */
		RegProcedure opfuncid;	/* operator proc id used in scan */
		Oid			opfamily;	/* opfamily of index column */
		int			op_strategy;	/* operator's strategy number */
		Oid			op_lefttype;	/* operator's declared input types */
		Oid			op_righttype;
		Expr	   *leftop;		/* expr on lhs of operator */
		Expr	   *rightop;	/* expr on rhs ... */
		AttrNumber	varattno;	/* att number used in scan */
		int			indnkeyatts;

		indnkeyatts = IndexRelationGetNumberOfKeyAttributes(index);
		if (IsA(clause, OpExpr))
		{
			/* indexkey op const or indexkey op expression */
			int			flags = 0;
			Datum		scanvalue;

			opno = ((OpExpr *) clause)->opno;
			opfuncid = ((OpExpr *) clause)->opfuncid;

			/*
			 * leftop should be the index key Var, possibly relabeled
			 */
			leftop = (Expr *) get_leftop(clause);

			if (leftop && IsA(leftop, RelabelType))
				leftop = ((RelabelType *) leftop)->arg;

			Assert(leftop != NULL);

			if (IsA(leftop, FuncExpr)
				&& ((FuncExpr *) leftop)->funcid == YB_HASH_CODE_OID)
			{
				flags |= YB_SK_IS_HASHED;
			}

			if (!(IsA(leftop, Var) &&
				  ((Var *) leftop)->varno == INDEX_VAR)
				  && ((flags & YB_SK_IS_HASHED) == 0))
				elog(ERROR, "indexqual doesn't have key on left side");


			if ((flags & YB_SK_IS_HASHED) != 0)
			{
				varattno = InvalidAttrNumber;
				opfamily = INTEGER_LSM_FAM_OID;
			} else {
				varattno = ((Var *) leftop)->varattno;
				if (varattno < 1 || varattno > indnkeyatts)
					elog(ERROR, "bogus index qualification");

				/*
				 * We have to look up the operator's strategy number.  This
				 * provides a cross-check that the operator does match the index.
				 */
				opfamily = index->rd_opfamily[varattno - 1];
			}

			get_op_opfamily_properties(opno, opfamily, isorderby,
									   &op_strategy,
									   &op_lefttype,
									   &op_righttype);

			if (isorderby)
				flags |= SK_ORDER_BY;

			/*
			 * rightop is the constant or variable comparison value
			 */
			rightop = (Expr *) get_rightop(clause);

			if (rightop && IsA(rightop, RelabelType))
				rightop = ((RelabelType *) rightop)->arg;

			Assert(rightop != NULL);

			if (IsA(rightop, Const))
			{
				/* OK, simple constant comparison value */
				scanvalue = ((Const *) rightop)->constvalue;
				if (((Const *) rightop)->constisnull)
					flags |= SK_ISNULL;
			}
			else
			{
				/* Need to treat this one as a runtime key */
				if (n_runtime_keys >= max_runtime_keys)
				{
					if (max_runtime_keys == 0)
					{
						max_runtime_keys = 8;
						runtime_keys = (IndexRuntimeKeyInfo *)
							palloc(max_runtime_keys * sizeof(IndexRuntimeKeyInfo));
					}
					else
					{
						max_runtime_keys *= 2;
						runtime_keys = (IndexRuntimeKeyInfo *)
							repalloc(runtime_keys, max_runtime_keys * sizeof(IndexRuntimeKeyInfo));
					}
				}
				runtime_keys[n_runtime_keys].scan_key = this_scan_key;
				runtime_keys[n_runtime_keys].key_expr =
					ExecInitExpr(rightop, planstate);
				runtime_keys[n_runtime_keys].key_toastable =
					TypeIsToastable(op_righttype);
				n_runtime_keys++;
				scanvalue = (Datum) 0;
			}

			/*
			 * initialize the scan key's fields appropriately
			 */
			ScanKeyEntryInitialize(this_scan_key,
								   flags,
								   varattno,	/* attribute number to scan */
								   op_strategy, /* op's strategy */
								   op_righttype,	/* strategy subtype */
								   ((OpExpr *) clause)->inputcollid,	/* collation */
								   opfuncid,	/* reg proc to use */
								   scanvalue);	/* constant */
		}
		else if (IsA(clause, RowCompareExpr) &&
				 castNode(RowCompareExpr, clause)->rctype != ROWCOMPARE_EQ)
		{
			/* (indexkey, indexkey, ...) op (expression, expression, ...) */
			RowCompareExpr *rc = (RowCompareExpr *) clause;
			ScanKey		first_sub_key;
			int			n_sub_key;
			ListCell   *largs_cell;
			ListCell   *rargs_cell;
			ListCell   *opnos_cell;
			ListCell   *collids_cell;

			Assert(!isorderby);

			first_sub_key = (ScanKey)
				palloc(list_length(rc->opnos) * sizeof(ScanKeyData));
			n_sub_key = 0;

			/* Scan RowCompare columns and generate subsidiary ScanKey items */
			forfour(largs_cell, rc->largs, rargs_cell, castNode(List, rc->rargs),
					opnos_cell, rc->opnos, collids_cell, rc->inputcollids)
			{
				ScanKey		this_sub_key = &first_sub_key[n_sub_key];
				int			flags = SK_ROW_MEMBER;
				Datum		scanvalue;
				Oid			inputcollation;

				leftop = (Expr *) lfirst(largs_cell);
				rightop = (Expr *) lfirst(rargs_cell);
				opno = lfirst_oid(opnos_cell);
				inputcollation = lfirst_oid(collids_cell);

				/*
				 * leftop should be the index key Var, possibly relabeled
				 */
				if (leftop && IsA(leftop, RelabelType))
					leftop = ((RelabelType *) leftop)->arg;

				Assert(leftop != NULL);

				/*
				 * Check for yb_hash_code() and set flag if present.
				 */
				if (IsA(leftop, FuncExpr)
					&& ((FuncExpr *) leftop)->funcid == YB_HASH_CODE_OID)
					flags |= YB_SK_IS_HASHED;

				if (!(IsA(leftop, Var) &&
					((Var *) leftop)->varno == INDEX_VAR)
					&& ((flags & YB_SK_IS_HASHED) == 0))
					elog(ERROR, "indexqual doesn't have key on left side");

				if ((flags & YB_SK_IS_HASHED) != 0)
				{
					varattno = InvalidAttrNumber;
					opfamily = INTEGER_LSM_FAM_OID;
				}
				else
				{
					varattno = ((Var *) leftop)->varattno;
					if (varattno < 1 || varattno > indnkeyatts)
						elog(ERROR, "bogus index qualification");

					if ((index->rd_rel->relam != BTREE_AM_OID && index->rd_rel->relam != LSM_AM_OID) ||
					varattno < 1 || varattno > indnkeyatts)
						elog(ERROR, "bogus RowCompare index qualification");

					/*
					 * We have to look up the operator's strategy number.  This
					 * provides a cross-check that the operator does match the index.
					 */
					opfamily = index->rd_opfamily[varattno - 1];
				}

				get_op_opfamily_properties(opno, opfamily, isorderby,
										   &op_strategy,
										   &op_lefttype,
										   &op_righttype);

				if (op_strategy != rc->rctype)
					elog(ERROR, "RowCompare index qualification contains wrong operator");

				opfuncid = get_opfamily_proc(opfamily,
											 op_lefttype,
											 op_righttype,
											 BTORDER_PROC);
				if (!RegProcedureIsValid(opfuncid))
					elog(ERROR, "missing support function %d(%u,%u) in opfamily %u",
						 BTORDER_PROC, op_lefttype, op_righttype, opfamily);

				/*
				 * rightop is the constant or variable comparison value
				 */
				if (rightop && IsA(rightop, RelabelType))
					rightop = ((RelabelType *) rightop)->arg;

				Assert(rightop != NULL);

				if (IsA(rightop, Const))
				{
					/* OK, simple constant comparison value */
					scanvalue = ((Const *) rightop)->constvalue;
					if (((Const *) rightop)->constisnull)
						flags |= SK_ISNULL;
				}
				else
				{
					/* Need to treat this one as a runtime key */
					if (n_runtime_keys >= max_runtime_keys)
					{
						if (max_runtime_keys == 0)
						{
							max_runtime_keys = 8;
							runtime_keys = (IndexRuntimeKeyInfo *)
								palloc(max_runtime_keys * sizeof(IndexRuntimeKeyInfo));
						}
						else
						{
							max_runtime_keys *= 2;
							runtime_keys = (IndexRuntimeKeyInfo *)
								repalloc(runtime_keys, max_runtime_keys * sizeof(IndexRuntimeKeyInfo));
						}
					}
					runtime_keys[n_runtime_keys].scan_key = this_sub_key;
					runtime_keys[n_runtime_keys].key_expr =
						ExecInitExpr(rightop, planstate);
					runtime_keys[n_runtime_keys].key_toastable =
						TypeIsToastable(op_righttype);
					n_runtime_keys++;
					scanvalue = (Datum) 0;
				}

				/*
				 * initialize the subsidiary scan key's fields appropriately
				 */
				ScanKeyEntryInitialize(this_sub_key,
									   flags,
									   varattno,	/* attribute number */
									   op_strategy, /* op's strategy */
									   op_righttype,	/* strategy subtype */
									   inputcollation,	/* collation */
									   opfuncid,	/* reg proc to use */
									   scanvalue);	/* constant */
				n_sub_key++;
			}

			/* Mark the last subsidiary scankey correctly */
			first_sub_key[n_sub_key - 1].sk_flags |= SK_ROW_END;

			/*
			 * We don't use ScanKeyEntryInitialize for the header because it
			 * isn't going to contain a valid sk_func pointer.
			 */
			MemSet(this_scan_key, 0, sizeof(ScanKeyData));
			this_scan_key->sk_flags = SK_ROW_HEADER;
			this_scan_key->sk_attno = first_sub_key->sk_attno;
			this_scan_key->sk_strategy = rc->rctype;
			/* sk_subtype, sk_collation, sk_func not used in a header */
			this_scan_key->sk_argument = PointerGetDatum(first_sub_key);
		}
		else if (IsA(clause, ScalarArrayOpExpr))
		{
			Assert(!IsYugaByteEnabled() ||
				   IsA(yb_get_saop_left_op(clause), Var));
			/* indexkey op ANY (array-expression) */
			ScalarArrayOpExpr *saop = (ScalarArrayOpExpr *) clause;
			int			flags = 0;
			Datum		scanvalue;

			Assert(!isorderby);

			Assert(saop->useOr);
			opno = saop->opno;
			opfuncid = saop->opfuncid;

			/*
			 * leftop should be the index key Var, possibly relabeled
			 */
			leftop = (Expr *) linitial(saop->args);

			if (leftop && IsA(leftop, RelabelType))
				leftop = ((RelabelType *) leftop)->arg;

			Assert(leftop != NULL);

			if (!(IsA(leftop, Var) &&
				  ((Var *) leftop)->varno == INDEX_VAR))
				elog(ERROR, "indexqual doesn't have key on left side");

			varattno = ((Var *) leftop)->varattno;
			if (varattno < 1 || varattno > indnkeyatts)
				elog(ERROR, "bogus index qualification");

			/*
			 * We have to look up the operator's strategy number.  This
			 * provides a cross-check that the operator does match the index.
			 */
			opfamily = index->rd_opfamily[varattno - 1];

			get_op_opfamily_properties(opno, opfamily, isorderby,
									   &op_strategy,
									   &op_lefttype,
									   &op_righttype);

			/*
			 * rightop is the constant or variable array value
			 */
			rightop = (Expr *) lsecond(saop->args);

			if (rightop && IsA(rightop, RelabelType))
				rightop = ((RelabelType *) rightop)->arg;

			Assert(rightop != NULL);

			if (index->rd_indam->amsearcharray)
			{
				/* Index AM will handle this like a simple operator */
				flags |= SK_SEARCHARRAY;
				if (IsA(rightop, Const))
				{
					/* OK, simple constant comparison value */
					scanvalue = ((Const *) rightop)->constvalue;
					if (((Const *) rightop)->constisnull)
						flags |= SK_ISNULL;
				}
				else
				{
					/* Need to treat this one as a runtime key */
					if (n_runtime_keys >= max_runtime_keys)
					{
						if (max_runtime_keys == 0)
						{
							max_runtime_keys = 8;
							runtime_keys = (IndexRuntimeKeyInfo *)
								palloc(max_runtime_keys * sizeof(IndexRuntimeKeyInfo));
						}
						else
						{
							max_runtime_keys *= 2;
							runtime_keys = (IndexRuntimeKeyInfo *)
								repalloc(runtime_keys, max_runtime_keys * sizeof(IndexRuntimeKeyInfo));
						}
					}
					runtime_keys[n_runtime_keys].scan_key = this_scan_key;
					runtime_keys[n_runtime_keys].key_expr =
						ExecInitExpr(rightop, planstate);

					/*
					 * Careful here: the runtime expression is not of
					 * op_righttype, but rather is an array of same; so
					 * TypeIsToastable() isn't helpful.  However, we can
					 * assume that all array types are toastable.
					 */
					runtime_keys[n_runtime_keys].key_toastable = true;
					n_runtime_keys++;
					scanvalue = (Datum) 0;
				}
			}
			else
			{
				/* Executor has to expand the array value */
				array_keys[n_array_keys].scan_key = this_scan_key;
				array_keys[n_array_keys].array_expr =
					ExecInitExpr(rightop, planstate);
				/* the remaining fields were zeroed by palloc0 */
				n_array_keys++;
				scanvalue = (Datum) 0;
			}

			/*
			 * initialize the scan key's fields appropriately
			 */
			ScanKeyEntryInitialize(this_scan_key,
								   flags,
								   varattno,	/* attribute number to scan */
								   op_strategy, /* op's strategy */
								   op_righttype,	/* strategy subtype */
								   saop->inputcollid,	/* collation */
								   opfuncid,	/* reg proc to use */
								   scanvalue);	/* constant */
		}
		else if (IsA(clause, RowCompareExpr))
		{
			Assert(IsYugaByteEnabled());
			/*
			 * (indexkey, indexkey, ...) op ANY [(expression, expression, ...),
			 *									 (expression, expression, ...),
			 *									 ...]
			 */
			RowCompareExpr *rcexpr = (RowCompareExpr *) clause;
			int			flags = SK_ROW_MEMBER;
			Datum		scanvalue;

			/* used when lhs is a RowExpr */
			ScanKey		first_sub_key;
			ScanKey		this_key;
			int			n_sub_key = 0;
			int			total_keys;

			Assert(!isorderby);

			total_keys = list_length(castNode(List, rcexpr->largs));
			first_sub_key = (ScanKey)
				palloc0(total_keys * sizeof(ScanKeyData));

			while (n_sub_key < total_keys)
			{
				/*
				 * leftop should be the index key Var, possibly relabeled
				 */
				/*
				 * TODO(tanuj): if it really is possibly a RelabelType, this
				 * needs relabel handling
				 */
				varattno = ((Var *) list_nth(rcexpr->largs, n_sub_key))->varattno;
				this_key = &first_sub_key[n_sub_key];
				op_strategy = BTEqualStrategyNumber;
				op_righttype = InvalidOid;
				opno = list_nth_oid(rcexpr->opnos, n_sub_key);
				Oid inputcollid = list_nth_oid(rcexpr->inputcollids, n_sub_key);

				if (varattno < 1 || varattno > indnkeyatts)
					elog(ERROR, "bogus index qualification");

				opfamily = index->rd_opfamily[varattno - 1];

				get_op_opfamily_properties(opno, opfamily, isorderby,
										   &op_strategy,
										   &op_lefttype,
										   &op_righttype);

				if (op_strategy != rcexpr->rctype)
					elog(ERROR, "RowCompare index qualification contains wrong operator");

				opfuncid = get_opfamily_proc(opfamily,
											 op_lefttype,
											 op_righttype,
											 BTORDER_PROC);

				/*
				 * rightop is the constant or variable array value
				 */
				rightop = (Expr *) rcexpr->rargs;

				if (rightop && IsA(rightop, RelabelType))
					rightop = ((RelabelType *) rightop)->arg;

				Assert(rightop != NULL);

				/*
				 * YB: we expect amsearcharray to be implemented and rightop to
				 * always be ArrayExpr.
				 * TODO(jason): clean up the below code to remove unreachable
				 * branches.  It is preserved for now because it is a copy of
				 * the above ScalarArrayOpExpr case.
				 */
				Assert(index->rd_indam->amsearcharray);
				Assert(IsA(rightop, ArrayExpr));
				if (index->rd_indam->amsearcharray)
				{
					/* Index AM will handle this like a simple operator */
					flags |= SK_SEARCHARRAY;
					if (IsA(rightop, Const))
					{
						/* OK, simple constant comparison value */
						scanvalue = ((Const *) rightop)->constvalue;
						if (((Const *) rightop)->constisnull)
							flags |= SK_ISNULL;
					}
					else
					{
						/* Need to treat this one as a runtime key */
						if (n_sub_key == 0)
						{
							if (n_runtime_keys >= max_runtime_keys)
							{
								if (max_runtime_keys == 0)
								{
									max_runtime_keys = 8;
									runtime_keys = (IndexRuntimeKeyInfo *)
										palloc(max_runtime_keys *
											   sizeof(IndexRuntimeKeyInfo));
								}
								else
								{
									max_runtime_keys *= 2;
									runtime_keys = (IndexRuntimeKeyInfo *)
										repalloc(runtime_keys,
												 max_runtime_keys *
												 sizeof(IndexRuntimeKeyInfo));
								}
							}
							runtime_keys[n_runtime_keys].scan_key = this_key;
							runtime_keys[n_runtime_keys].key_expr =
								ExecInitExpr(rightop, planstate);

							/*
							 * Careful here: the runtime expression is not of
							 * op_righttype, but rather is an array of same; so
							 * TypeIsToastable() isn't helpful.  However, we can
							 * assume that all array types are toastable.
							 */
							runtime_keys[n_runtime_keys].key_toastable = true;
							n_runtime_keys++;
							scanvalue = (Datum) 0;
						}
					}
				}
				else
				{
					/* Executor has to expand the array value */
					array_keys[n_array_keys].scan_key = this_key;
					array_keys[n_array_keys].array_expr =
						ExecInitExpr(rightop, planstate);
					/* the remaining fields were zeroed by palloc0 */
					n_array_keys++;
					scanvalue = (Datum) 0;
				}

				/*
				 * initialize the scan key's fields appropriately
				 */
				ScanKeyEntryInitialize(this_key,
									   flags,
									   varattno,  /* attribute number to scan */
									   op_strategy, /* op's strategy */
									   op_righttype,	/* strategy subtype */
									   inputcollid,	/* collation */
									   opfuncid,	/* reg proc to use */
									   scanvalue);	/* constant */
				n_sub_key++;
			}

			/* Mark the last subsidiary scankey correctly */
			first_sub_key[n_sub_key - 1].sk_flags |= SK_ROW_END;

			/*
			* We don't use ScanKeyEntryInitialize for the header because it
			* isn't going to contain a valid sk_func pointer.
			*/
			MemSet(this_scan_key, 0, sizeof(ScanKeyData));
			this_scan_key->sk_flags = SK_ROW_HEADER | SK_SEARCHARRAY;
			this_scan_key->sk_attno = first_sub_key->sk_attno;
			/*
			 * YB: we only support = operator for now, and it shouldn't be
			 * possible to get here with something else.
			 */
			Assert(rcexpr->rctype == BTEqualStrategyNumber);
			this_scan_key->sk_strategy = rcexpr->rctype;
			/* sk_subtype, sk_collation, sk_func not used in a header */
			this_scan_key->sk_argument = PointerGetDatum(first_sub_key);
			/*
			 * TODO(jason): sk_subtype = RECORDOID should not be necessary and
			 * is currently tied to a hack in yb_scan.c.
			 */
			this_scan_key->sk_subtype = RECORDOID;
		}
		else if (IsA(clause, NullTest))
		{
			/* indexkey IS NULL or indexkey IS NOT NULL */
			NullTest   *ntest = (NullTest *) clause;
			int			flags;

			Assert(!isorderby);

			/*
			 * argument should be the index key Var, possibly relabeled
			 */
			leftop = ntest->arg;

			if (leftop && IsA(leftop, RelabelType))
				leftop = ((RelabelType *) leftop)->arg;

			Assert(leftop != NULL);

			if (!(IsA(leftop, Var) &&
				  ((Var *) leftop)->varno == INDEX_VAR))
				elog(ERROR, "NullTest indexqual has wrong key");

			varattno = ((Var *) leftop)->varattno;

			/*
			 * initialize the scan key's fields appropriately
			 */
			switch (ntest->nulltesttype)
			{
				case IS_NULL:
					flags = SK_ISNULL | SK_SEARCHNULL;
					break;
				case IS_NOT_NULL:
					flags = SK_ISNULL | SK_SEARCHNOTNULL;
					break;
				default:
					elog(ERROR, "unrecognized nulltesttype: %d",
						 (int) ntest->nulltesttype);
					flags = 0;	/* keep compiler quiet */
					break;
			}

			ScanKeyEntryInitialize(this_scan_key,
								   flags,
								   varattno,	/* attribute number to scan */
								   InvalidStrategy, /* no strategy */
								   InvalidOid,	/* no strategy subtype */
								   InvalidOid,	/* no collation */
								   InvalidOid,	/* no reg proc for this */
								   (Datum) 0);	/* constant */
		}
		else
			elog(ERROR, "unsupported indexqual type: %d",
				 (int) nodeTag(clause));
	}

	Assert(n_runtime_keys <= max_runtime_keys);

	/* Get rid of any unused arrays */
	if (n_array_keys == 0)
	{
		pfree(array_keys);
		array_keys = NULL;
	}

	/*
	 * Return info to our caller.
	 */
	*scanKeys = scan_keys;
	*numScanKeys = n_scan_keys;
	*runtimeKeys = runtime_keys;
	*numRuntimeKeys = n_runtime_keys;
	if (arrayKeys)
	{
		*arrayKeys = array_keys;
		*numArrayKeys = n_array_keys;
	}
	else if (n_array_keys != 0)
		elog(ERROR, "ScalarArrayOpExpr index qual found where not allowed");
}

/*
 * yb_init_index_scandesc
 *
 *		Initialize Yugabyte specific fields of the IndexScanDesc.
 */
static void
yb_init_index_scandesc(IndexScanState *node)
{
	if (IsYugaByteEnabled())
	{
		IndexScanDesc scandesc = node->iss_ScanDesc;
		EState *estate = node->ss.ps.state;
		IndexScan *plan = (IndexScan *) node->ss.ps.plan;
		scandesc->yb_exec_params = &estate->yb_exec_params;
		scandesc->yb_scan_plan = (Scan *) plan;
		scandesc->yb_rel_pushdown =
			YbInstantiatePushdownParams(&plan->yb_rel_pushdown, estate);
		scandesc->yb_idx_pushdown =
			YbInstantiatePushdownParams(&plan->yb_idx_pushdown, estate);
		scandesc->yb_aggrefs = node->yb_iss_aggrefs;
		scandesc->yb_distinct_prefixlen = plan->yb_distinct_prefixlen;
		scandesc->fetch_ybctids_only = false;
	}
}

/* ----------------------------------------------------------------
 *						Parallel Scan Support
 * ----------------------------------------------------------------
 */

/* ----------------------------------------------------------------
 *		ExecIndexScanEstimate
 *
 *		Compute the amount of space we'll need in the parallel
 *		query DSM, and inform pcxt->estimator about our needs.
 * ----------------------------------------------------------------
 */
void
ExecIndexScanEstimate(IndexScanState *node,
					  ParallelContext *pcxt)
{
	EState	   *estate = node->ss.ps.state;

	node->iss_PscanLen = index_parallelscan_estimate(node->iss_RelationDesc,
													 estate->es_snapshot);
	shm_toc_estimate_chunk(&pcxt->estimator, node->iss_PscanLen);
	shm_toc_estimate_keys(&pcxt->estimator, 1);
}

/* ----------------------------------------------------------------
 *		ExecIndexScanInitializeDSM
 *
 *		Set up a parallel index scan descriptor.
 * ----------------------------------------------------------------
 */
void
ExecIndexScanInitializeDSM(IndexScanState *node,
						   ParallelContext *pcxt)
{
	EState	   *estate = node->ss.ps.state;
	ParallelIndexScanDesc piscan;

	piscan = shm_toc_allocate(pcxt->toc, node->iss_PscanLen);
	index_parallelscan_initialize(node->ss.ss_currentRelation,
								  node->iss_RelationDesc,
								  estate->es_snapshot,
								  piscan);
	shm_toc_insert(pcxt->toc, node->ss.ps.plan->plan_node_id, piscan);
	node->iss_ScanDesc =
		index_beginscan_parallel(node->ss.ss_currentRelation,
								 node->iss_RelationDesc,
								 node->iss_NumScanKeys,
								 node->iss_NumOrderByKeys,
								 piscan);
	yb_init_index_scandesc(node);

	if (node->yb_iss_aggrefs)
		yb_agg_pushdown_init_scan_slot(node);

	/*
	 * If no run-time keys to calculate or they are ready, go ahead and pass
	 * the scankeys to the index AM.
	 */
	if (node->iss_NumRuntimeKeys == 0 || node->iss_RuntimeKeysReady)
		index_rescan(node->iss_ScanDesc,
					 node->iss_ScanKeys, node->iss_NumScanKeys,
					 node->iss_OrderByKeys, node->iss_NumOrderByKeys);
}

/* ----------------------------------------------------------------
 *		ExecIndexScanReInitializeDSM
 *
 *		Reset shared state before beginning a fresh scan.
 * ----------------------------------------------------------------
 */
void
ExecIndexScanReInitializeDSM(IndexScanState *node,
							 ParallelContext *pcxt)
{
	index_parallelrescan(node->iss_ScanDesc);
}

/* ----------------------------------------------------------------
 *		ExecIndexScanInitializeWorker
 *
 *		Copy relevant information from TOC into planstate.
 * ----------------------------------------------------------------
 */
void
ExecIndexScanInitializeWorker(IndexScanState *node,
							  ParallelWorkerContext *pwcxt)
{
	ParallelIndexScanDesc piscan;

	piscan = shm_toc_lookup(pwcxt->toc, node->ss.ps.plan->plan_node_id, false);
	node->iss_ScanDesc =
		index_beginscan_parallel(node->ss.ss_currentRelation,
								 node->iss_RelationDesc,
								 node->iss_NumScanKeys,
								 node->iss_NumOrderByKeys,
								 piscan);
	yb_init_index_scandesc(node);

	if (node->yb_iss_aggrefs)
		yb_agg_pushdown_init_scan_slot(node);

	/*
	 * If no run-time keys to calculate or they are ready, go ahead and pass
	 * the scankeys to the index AM.
	 */
	if (node->iss_NumRuntimeKeys == 0 || node->iss_RuntimeKeysReady)
		index_rescan(node->iss_ScanDesc,
					 node->iss_ScanKeys, node->iss_NumScanKeys,
					 node->iss_OrderByKeys, node->iss_NumOrderByKeys);
}

static void
yb_agg_pushdown_init_scan_slot(IndexScanState *node)
{
	Assert(node->yb_iss_aggrefs);
	/*
	 * For aggregate pushdown, we only read aggregate results from
	 * DocDB and pass that up to the aggregate node (agg pushdown
	 * wouldn't be enabled if we needed to read other expressions). Set
	 * up a dummy scan slot to hold as many attributes as there are
	 * pushed aggregates.
	 */
	TupleDesc tupdesc =
		CreateTemplateTupleDesc(list_length(node->yb_iss_aggrefs));
	ExecInitScanTupleSlot(node->ss.ps.state, &node->ss, tupdesc, &TTSOpsVirtual);
}
