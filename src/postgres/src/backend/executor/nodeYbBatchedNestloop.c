/*-------------------------------------------------------------------------
 *
 * nodeYbBatchedNestLoop.c
 *	  Implementation of Yugabyte's batched nested loop join.
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
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
 * src/postgres/src/backend/executor/nodeYbBatchedNestLoop.c
 *
 *-------------------------------------------------------------------------
 */
/*
 *	 INTERFACE ROUTINES
 *		ExecYbBatchedNestLoop	 - process a YbBatchedNestLoop join of two plans
 *		ExecInitYbBatchedNestLoop - initialize the join
 *		ExecEndYbBatchedNestLoop  - shut down the join
 */

#include "postgres.h"

#include <math.h>

#include "executor/execdebug.h"
#include "executor/executor.h"
#include "executor/nodeYbBatchedNestloop.h"
#include "access/relation.h"
#include "miscadmin.h"
#include "utils/memutils.h"
#include "utils/tuplesort.h"


bool yb_bnl_enable_hashing = true;

/* Methods to help keep track of outer tuple batches */
bool CreateBatch(YbBatchedNestLoopState *bnlstate, ExprContext *econtext);
int GetMaxBatchSize(YbBatchedNestLoop *plan);
int GetCurrentBatchSize(YbBatchedNestLoopState *bnlstate);

/* Local join methods that use the tuplestore batching strategy */
bool FlushTupleTS(YbBatchedNestLoopState *bnlstate, ExprContext *econtext);
bool GetNewOuterTupleTS(YbBatchedNestLoopState *bnlstate, ExprContext *econtext);
void ResetBatchTS(YbBatchedNestLoopState *bnlstate, ExprContext *econtext);
void RegisterOuterMatchTS(YbBatchedNestLoopState *bnlstate,
						  ExprContext *econtext);
void AddTupleToOuterBatchTS(YbBatchedNestLoopState *bnlstate,
							TupleTableSlot *slot);
void FreeBatchTS(YbBatchedNestLoopState *bnlstate);
void EndTS(YbBatchedNestLoopState *bnlstate);

/* Local join methods that use the hash table batching strategy */
bool FlushTupleHash(YbBatchedNestLoopState *bnlstate, ExprContext *econtext);
bool GetNewOuterTupleHash(YbBatchedNestLoopState *bnlstate, ExprContext *econtext);
void ResetBatchHash(YbBatchedNestLoopState *bnlstate, ExprContext *econtext);
void RegisterOuterMatchHash(YbBatchedNestLoopState *bnlstate,
							ExprContext *econtext);
void AddTupleToOuterBatchHash(YbBatchedNestLoopState *bnlstate,
							  TupleTableSlot *slot);
void FreeBatchHash(YbBatchedNestLoopState *bnlstate);
void EndHash(YbBatchedNestLoopState *bnlstate);
static TupleTableSlot *ProcessSorting(YbBatchedNestLoopState *bnlstate);
static void EndSorting(YbBatchedNestLoopState *bnlstate);

static bool
PutSorting(YbBatchedNestLoopState *bnlstate, TupleTableSlot *slot);
/* Wrappers for invoking local join methods with the correct strategy */
#define REGISTER_LOCAL_JOIN_FN(fn, strategy) bnlstate->fn##Impl = &fn##strategy
#define LOCAL_JOIN_FN(fn, node, ...) (*node->fn##Impl)(node, ## __VA_ARGS__)


/* ----------------------------------------------------------------
 *		ExecYbBatchedNestLoop(node)
 *
 *		Returns the tuple joined from inner and outer tuples which
 *		satisfies the qualification clause.
 *
 *		This performs the operations of nodenestloop.c in a batched fashion.
 *
 *		In order to execute in batched mode, we use a state here to denote
 *		the status of our current batch of outer tuples. The various states and
 *		overall workflow are outlined as follows:
 *
 *		- BNL_INIT:	   The current tuple batch is invalid and we must create a
 *						fresh batch and then transition to BNL_NEWINNER.
 *
 *		- BNL_NEWINNER: We need a new inner tuple. This can occur as a result of
 *						the outer tuple batch being freshly populated or the
 *						previous inner tuple running out of matches in the
 *						current outer tuple batch. We transition to BNL_MATCHING
 *						from here if new inner tuples are found or BNL_FLUSHING
 *						if else.
 *
 *		- BNL_MATCHING: The current tuple batch is valid and we have a valid
 *						inner tuple that we can match with outer tuples
 *						from the current batch. Once an inner tuple runs
 *						out of outer tuples in the current batch to match with,
 *						we go back to BNL_NEWINNER to retrieve a new inner
 *						tuple.
 *
 *		- BNL_FLUSHING: The current tuple batch is valid and we have run out of
 *						matching inner tuples. If this is an outer/anti join, we
 *						are iterating over tuples that have not been matched
 *						with any inner tuple. Once we run out of unmatched outer
 *						tuples here, we invalidate the current batch by
 *						transitioning to BNL_INIT.
 *
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
ExecYbBatchedNestLoop(PlanState *pstate)
{
	YbBatchedNestLoopState *bnlstate = castNode(YbBatchedNestLoopState, pstate);
	YbBatchedNestLoop   *batchnl;
	PlanState  *innerPlan;
	TupleTableSlot *innerTupleSlot;
	ExprState  *joinqual;
	ExprState  *otherqual;
	ExprContext *econtext;

	CHECK_FOR_INTERRUPTS();

	/*
	 * get information from the node
	 */
	elog(DEBUG2, "getting info from node");

	batchnl = (YbBatchedNestLoop *) bnlstate->js.ps.plan;
	joinqual = bnlstate->js.joinqual;
	otherqual = bnlstate->js.ps.qual;
	econtext = bnlstate->js.ps.ps_ExprContext;
	innerPlan = innerPlanState(bnlstate);

	/*
	 * Reset per-tuple memory context to free any expression evaluation
	 * storage allocated in the previous tuple cycle.
	 */
	ResetExprContext(econtext);
	/*
	 * Ok, everything is setup for the join so now loop until we return a
	 * qualifying join tuple.
	 */
	elog(DEBUG2, "entering main loop");

	bnlstate->batch_size = GetMaxBatchSize(batchnl);

	if (!pstate->state->yb_exec_params.limit_use_default)
	{
		uint32_t limit = pstate->state->yb_exec_params.limit_count;
		limit = ceil(limit * batchnl->first_batch_factor);
		if (limit > 0 && limit < GetMaxBatchSize(batchnl))
		{
			if (!bnlstate->is_first_batch_done)
				bnlstate->batch_size = limit;

			limit = GetCurrentBatchSize(bnlstate);
			pstate->state->yb_exec_params.limit_count = limit;
		}
	}

	Assert(bnlstate->batch_size > 0);

	for (;;)
	{
		/*
		 * We process the current batch and populate econtext->ecxt_outertuple
		 * according to the operations listed in the batching comments of this
		 * method.
		 */
		switch (bnlstate->bnl_currentstatus)
		{
			case BNL_INIT:
				if (bnlstate->bnl_needs_sorting)
				{
					TupleTableSlot *result = ProcessSorting(bnlstate);
					if (result)
						return result;
				}

				if (CreateBatch(bnlstate, econtext))
				{
					/* Transition */
					bnlstate->is_first_batch_done = true;
					bnlstate->batch_size = GetMaxBatchSize(batchnl);
					bnlstate->bnl_currentstatus = BNL_NEWINNER;
				}
				else
					return NULL;

				/*
				 * now rescan the inner plan
				 */
				elog(DEBUG2, "rescanning inner plan");
				ExecReScan(innerPlan);

				switch_fallthrough();
			case BNL_NEWINNER:
				/*
				 * we have an outerTuple batch, try to get the next inner tuple.
				 */
				elog(DEBUG2, "getting new inner tuple");

				innerTupleSlot = ExecProcNode(innerPlan);
				econtext->ecxt_innertuple = innerTupleSlot;

				LOCAL_JOIN_FN(ResetBatch, bnlstate, econtext);

				if (TupIsNull(innerTupleSlot))
				{
					/* No more inner tuples: transition */
					bnlstate->bnl_currentstatus = BNL_FLUSHING;
					continue;
				}

				bnlstate->bnl_currentstatus = BNL_MATCHING;

				switch_fallthrough();
			case BNL_MATCHING:
				Assert(!TupIsNull(econtext->ecxt_innertuple));

				if (!LOCAL_JOIN_FN(GetNewOuterTuple, bnlstate, econtext))
				{
					bnlstate->bnl_currentstatus = BNL_NEWINNER;
					continue;
				}
				break;
			case BNL_FLUSHING:
				if (batchnl->nl.join.jointype == JOIN_INNER ||
					batchnl->nl.join.jointype == JOIN_SEMI)
				{
					/* This state is not applicable here: transition */
					bnlstate->bnl_currentstatus = BNL_INIT;
					continue;
				}

				if (!LOCAL_JOIN_FN(FlushTuple, bnlstate, econtext))
				{
					/* tuplestate should be clean */
					bnlstate->bnl_currentstatus = BNL_INIT;
					continue;
				}

				break;
			default:
				Assert(false);
		}

		Assert(!TupIsNull(econtext->ecxt_outertuple));

		innerTupleSlot = econtext->ecxt_innertuple;

		if (bnlstate->bnl_currentstatus == BNL_FLUSHING)
		{
			elog(DEBUG2, "no inner tuple, need new outer tuple");

			Assert(bnlstate->js.jointype == JOIN_LEFT ||
				   bnlstate->js.jointype == JOIN_ANTI);
			/*
			 * We are doing an outer join and there were no join matches
			 * for this outer tuple.  Generate a fake join tuple with
			 * nulls for the inner tuple, and return it if it passes the
			 * non-join quals.
			 */
			econtext->ecxt_innertuple = bnlstate->nl_NullInnerTupleSlot;

			elog(DEBUG2, "testing qualification for outer-join tuple");

			if (otherqual == NULL || ExecQual(otherqual, econtext))
			{
				/*
				 * qualification was satisfied so we project and return
				 * the slot containing the result tuple using
				 * ExecProject().
				 */
				elog(DEBUG2, "qualification succeeded, projecting tuple");
				TupleTableSlot *slot = ExecProject(bnlstate->js.ps.ps_ProjInfo);
				if (PutSorting(bnlstate, slot))
					continue;

				return slot;
			}
			else
				InstrCountFiltered2(bnlstate, 1);

			/*
			 * Otherwise just return to top of loop for a new outer tuple.
			 */
			continue;
		}

		/*
		 * at this point we have a new pair of inner and outer tuples so we
		 * test the inner and outer tuples to see if they satisfy the node's
		 * qualification.
		 *
		 * Only the joinquals determine MatchedOuter status, but all quals
		 * must pass to actually return the tuple.
		 */
		elog(DEBUG2, "testing qualification");

		if (ExecQual(joinqual, econtext))
		{
			LOCAL_JOIN_FN(RegisterOuterMatch, bnlstate, econtext);
			/* In an antijoin, we never return a matched tuple */

			if (bnlstate->js.jointype == JOIN_ANTI)
			{
				/*
				 * This outer tuple has been matched so never think about
				 * this outer tuple again.
				 */
				continue;		/* return to top of loop */
			}

			if (otherqual == NULL || ExecQual(otherqual, econtext))
			{
				/*
				 * qualification was satisfied so we project and return the
				 * slot containing the result tuple using ExecProject().
				 */
				elog(DEBUG2, "qualification succeeded, projecting tuple");

				TupleTableSlot *slot = ExecProject(bnlstate->js.ps.ps_ProjInfo);
				if (PutSorting(bnlstate, slot))
					continue;

				return slot;
			}
			else
				InstrCountFiltered2(bnlstate, 1);
		}
		else
			InstrCountFiltered1(bnlstate, 1);

		/*
		 * Tuple fails qual, so free per-tuple memory and try again.
		 */
		ResetExprContext(econtext);

		elog(DEBUG2, "qualification failed, looping");
	}
}

static void
EndSorting(YbBatchedNestLoopState *bnlstate)
{
	if (!bnlstate->bnl_needs_sorting)
		return;

	Tuplesortstate *sorter = bnlstate->bnl_tuple_sort;
	bnlstate->bnl_is_sorted = false;
	if (sorter)
	{
		tuplesort_end(sorter);
		bnlstate->bnl_tuple_sort = NULL;
	}
}

static bool
PutSorting(YbBatchedNestLoopState *bnlstate, TupleTableSlot *slot)
{
	if (bnlstate->bnl_tuple_sort)
	{
		tuplesort_puttupleslot(bnlstate->bnl_tuple_sort, (slot));
		return true;
	}
	return false;
}

/*
 * If we are running a sorted BNL, this method encapsulates sorting-related
 * logic. If the sorter has tuples left to be returned, each invocation of this
 * method flushes a tuple by returning it in sorted order. Else, it frees any
 * previously-held sorting resources and starts anew.
 */
static TupleTableSlot *
ProcessSorting(YbBatchedNestLoopState *bnlstate)
{
	TupleTableSlot *result_slot = bnlstate->js.ps.ps_ResultTupleSlot;
	Tuplesortstate *sorter = bnlstate->bnl_tuple_sort;
	if (!bnlstate->bnl_needs_sorting)
		return NULL;

	YbBatchedNestLoop *batchnl_plan =
		(YbBatchedNestLoop *) bnlstate->js.ps.plan;
	if (sorter)
	{
		if (!bnlstate->bnl_is_sorted)
			tuplesort_performsort(sorter);

		bnlstate->bnl_is_sorted = true;

		/* Flush sorter. */
		bool result = tuplesort_gettupleslot(sorter, true, false, result_slot,
											 NULL);
		Assert(!result || !TTS_EMPTY(result_slot));
		if (result)
			return result_slot;

		/* Done flushing sorter, make new tuples now. */
		EndSorting(bnlstate);
	}

	if (!bnlstate->bnl_tuple_sort)
	{
		TupleDesc result_desc =
			result_slot->tts_tupleDescriptor;
		sorter = tuplesort_begin_heap(result_desc,
									  batchnl_plan->numSortCols,
									  batchnl_plan->sortColIdx,
									  batchnl_plan->sortOperators,
									  batchnl_plan->collations,
									  batchnl_plan->nullsFirst,
									  work_mem,
									  NULL,
									  false);

		if (bnlstate->bound)
			tuplesort_set_bound(sorter, bnlstate->bound);

		bnlstate->bnl_tuple_sort = sorter;
	}
	return NULL;
}

/*
 * Whether or not we are using the hash batching strategy. We go with
 * the hash strategy if we have at least one hashable clause in our join
 * condition as signified by num_hashClauseInfos.
 */
static bool inline
UseHash(YbBatchedNestLoop *plan, YbBatchedNestLoopState *nl)
{
	return yb_bnl_enable_hashing && plan->num_hashClauseInfos > 0;
}

/*
 * Initialize batch state for the hashing strategy
 */
static void
InitHash(YbBatchedNestLoopState *bnlstate)
{
	EState *estate = bnlstate->js.ps.state;
	YbBatchedNestLoop *plan = (YbBatchedNestLoop*) bnlstate->js.ps.plan;
	ExprContext *econtext = CreateExprContext(estate);
	TupleDesc outer_tdesc = outerPlanState(bnlstate)->ps_ResultTupleDesc;

	const TupleTableSlotOps * innerops = bnlstate->js.ps.innerops;
	bool inneropsfixed = bnlstate->js.ps.inneropsfixed;
	bool inneropsset = bnlstate->js.ps.inneropsset;

	Assert(UseHash(plan, bnlstate));

	int num_hashClauseInfos = plan->num_hashClauseInfos;
	Oid *eqops = palloc(num_hashClauseInfos * (sizeof(Oid)));

	bnlstate->numLookupAttrs = num_hashClauseInfos;
	bnlstate->innerAttrs =
		palloc(num_hashClauseInfos * sizeof(AttrNumber));
	ExprState **keyexprs = palloc(num_hashClauseInfos * (sizeof(ExprState*)));
	List *outerParamExprs = NULL;
	List *hashExprs = NULL;
	YbBNLHashClauseInfo *current_hinfo = plan->hashClauseInfos;

	for (int i = 0; i < num_hashClauseInfos; i++)
	{
		Oid eqop = current_hinfo->hashOp;
		Assert(OidIsValid(eqop));
		eqops[i] = eqop;
		bnlstate->innerAttrs[i] = current_hinfo->innerHashAttNo;
		Expr *outerExpr = current_hinfo->outerParamExpr;
		keyexprs[i] = ExecInitExpr(outerExpr, (PlanState *) bnlstate);
		outerParamExprs = lappend(outerParamExprs, outerExpr);
		hashExprs = lappend(hashExprs, current_hinfo->orig_expr);
		current_hinfo++;
	}
	Oid *eqFuncOids;
	execTuplesHashPrepare(num_hashClauseInfos, eqops, &eqFuncOids,
						  &bnlstate->innerHashFunctions,
						  &bnlstate->outerHashFunctions);

	/*
	 * Since hash table stores MinimalTuple, both LHS and RHS operands of the
	 * hash table comparator will be MinimalTuple. The operands will be stored
	 * in ecxt_innertuple and ecxt_outertuple (see TupleHashTableMatch).
	 * Therefore, both innerops and outerops must be TTSOpsMinimalTuple when
	 * compiling the hash table comparator. outerops is already handled in
	 * ExecInitYbBatchedNestLoop. Temporarily set the innerops to
	 * &TTSOpsMinimalTuple.
	 */
	bnlstate->js.ps.innerops = &TTSOpsMinimalTuple;
	bnlstate->js.ps.inneropsfixed = true;
	bnlstate->js.ps.inneropsset = true;

	ExprState *tab_eq_fn =
		ybPrepareOuterExprsEqualFn(outerParamExprs,
								   eqops,
								   (PlanState *) bnlstate);

	/* revert to original innerops */
	bnlstate->js.ps.innerops = innerops;
	bnlstate->js.ps.inneropsfixed = inneropsfixed;
	bnlstate->js.ps.inneropsset = inneropsset;

	/* Per batch memory context for the hash table to work with */
	MemoryContext tablecxt =
		AllocSetContextCreate(GetCurrentMemoryContext(),
							  "BNL_HASHTABLE",
							  ALLOCSET_DEFAULT_SIZES);

	bnlstate->hashtable =
		YbBuildTupleHashTableExt(&bnlstate->js.ps, outer_tdesc,
								 num_hashClauseInfos, keyexprs, tab_eq_fn,
								 eqFuncOids, bnlstate->outerHashFunctions,
								 GetMaxBatchSize(plan), 0,
								 econtext->ecxt_per_query_memory, tablecxt,
								 econtext->ecxt_per_tuple_memory, econtext,
								 false);
	bnlstate->ht_lookup_fn = ExecInitQual(hashExprs, (PlanState *) bnlstate);

	bnlstate->hashiterinit = false;
	bnlstate->current_hash_entry = NULL;
}

bool
FlushTupleHash(YbBatchedNestLoopState *bnlstate, ExprContext *econtext)
{
	/* Initialize hash iterator if not done so already */
	if (!bnlstate->hashiterinit)
	{
		InitTupleHashIterator(bnlstate->hashtable, &bnlstate->hashiter);
		bnlstate->hashiterinit = true;
		bnlstate->current_hash_entry = NULL;
	}

	/* Find the current/next bucket that we'll be using */
	TupleHashEntry entry = bnlstate->current_hash_entry;
	if (entry == NULL)
		entry = ScanTupleHashTable(bnlstate->hashtable, &bnlstate->hashiter);
	while (entry != NULL)
	{
		YbNLBucketInfo *binfo = entry->additional;
		while (binfo->current != NULL)
		{
			YbBucketTupleInfo *btinfo = lfirst(binfo->current);
			binfo->current = lnext(binfo->tuples, binfo->current);

			while (btinfo != NULL && !(btinfo->matched))
			{
				ExecStoreMinimalTuple(btinfo->tuple,
									  econtext->ecxt_outertuple,
									  false);
				bnlstate->current_hash_entry = entry;
				bnlstate->current_ht_tuple = btinfo;
				return true;
			}
		}
		entry = ScanTupleHashTable(bnlstate->hashtable, &bnlstate->hashiter);
	}
	TermTupleHashIterator(&bnlstate->hashiter);
	bnlstate->hashiterinit = false;
	bnlstate->current_hash_entry = NULL;
	bnlstate->current_ht_tuple = NULL;
	return false;
}

bool
GetNewOuterTupleHash(YbBatchedNestLoopState *bnlstate, ExprContext *econtext)
{
	TupleTableSlot *inner = econtext->ecxt_innertuple;
	TupleHashTable ht = bnlstate->hashtable;
	ExprState *eq = bnlstate->ht_lookup_fn;

	TupleHashEntry data;
	data = FindTupleHashEntry(ht,
							  inner,
							  eq,
							  bnlstate->innerHashFunctions,
							  bnlstate->innerAttrs);
	if (data == NULL)
	{
		/* Inner plan returned a tuple that doesn't match with anything. */
		InstrCountFiltered1(bnlstate, 1);
		return false;
	}

	YbNLBucketInfo *binfo = (YbNLBucketInfo*) data->additional;
	while (binfo->current != NULL)
	{
		YbBucketTupleInfo *curr_btinfo = lfirst(binfo->current);
		/* Change the bucket's state for the next invocation of this method */
		binfo->current = lnext(binfo->tuples, binfo->current);

		/* We found a bucket with more matching tuples to be outputted. */
		YbBucketTupleInfo *btinfo = (YbBucketTupleInfo *) curr_btinfo;

		/*
		 * This has already been matched so no need to look at this again in a
		 * semijoin.
		 */
		if (bnlstate->js.single_match && btinfo->matched)
		{
			continue;
		}

		ExecStoreMinimalTuple(btinfo->tuple, econtext->ecxt_outertuple, false);

		bnlstate->current_ht_tuple = btinfo;

		Assert(data != NULL);
		return true;
	}

	/*
	 * There are no more matches for the current inner tuple so reset
	 * this bucket's state and return false.
	 */
	binfo->current = list_head(binfo->tuples);
	return false;
}

/*
 * Resets any iteration on this batch.
 */
void
ResetBatchHash(YbBatchedNestLoopState *bnlstate, ExprContext *econtext)
{
	if (bnlstate->hashiterinit)
	{
		bnlstate->hashiterinit = false;
		TermTupleHashIterator(&bnlstate->hashiterinit);
	}
	bnlstate->current_hash_entry = NULL;
	bnlstate->current_ht_tuple = NULL;
}

/*
 * Marks the current outer tuple as matched.
 * "Current outer tuple" refers to the outer tuple most recently returned by
 * GetNewOuterTupleHash.
 */
void
RegisterOuterMatchHash(YbBatchedNestLoopState *bnlstate, ExprContext *econtext)
{
	Assert(bnlstate->current_ht_tuple != NULL);
	bnlstate->current_ht_tuple->matched = true;
}

/*
 * Add the tuple in slot to the batch hash table in the appropriate bucket.
 */
void
AddTupleToOuterBatchHash(YbBatchedNestLoopState *bnlstate,
						 TupleTableSlot *slot)
{
	TupleHashTable ht = bnlstate->hashtable;
	bool isnew = false;

	Assert(!TupIsNull(slot));
	TupleHashEntry orig_data = LookupTupleHashEntry(ht, slot, &isnew, NULL);
	Assert(orig_data != NULL);
	Assert(orig_data->firstTuple != NULL);
	MemoryContext cxt = MemoryContextSwitchTo(ht->tablecxt);
	MinimalTuple tuple;
	if (isnew)
	{
		/* We must create a new bucket. */
		orig_data->additional = palloc0(sizeof(YbNLBucketInfo));
		tuple = orig_data->firstTuple;
	}
	YbNLBucketInfo *binfo = (YbNLBucketInfo *) orig_data->additional;
	List *tl = binfo->tuples;
	if (!isnew)
	{
		/* Bucket already exists. */
		tuple = ExecCopySlotMinimalTuple(slot);
	}

	YbBucketTupleInfo *tupinfo = palloc0(sizeof(YbBucketTupleInfo));
	tupinfo->tuple = tuple;
	tupinfo->matched = false;

	binfo->tuples = list_append_unique_ptr(tl, tupinfo);
	binfo->current = list_head(binfo->tuples);
	ExecStoreMinimalTuple(tuple, slot, false);
	MemoryContextSwitchTo(cxt);
}

/*
 * Clean up hash state.
 */
void
FreeBatchHash(YbBatchedNestLoopState *bnlstate)
{
	Assert(bnlstate->hashtable != NULL);
	bnlstate->hashiterinit = false;
	ResetTupleHashTable(bnlstate->hashtable);
	MemoryContextReset(bnlstate->hashtable->tablecxt);
	bnlstate->current_hash_entry = NULL;
}

/*
 * Clean up and end hash state.
 */
void
EndHash(YbBatchedNestLoopState *bnlstate)
{
	(void)bnlstate;
	MemoryContextDelete(bnlstate->hashtable->tablecxt);
	return;
}

void
InitTS(YbBatchedNestLoopState *bnlstate)
{
	bnlstate->bnl_tupleStoreState =
		tuplestore_begin_heap(true, false, work_mem);
}


void
AddTupleToOuterBatchTS(YbBatchedNestLoopState *bnlstate,
					   TupleTableSlot *slot)
{
	tuplestore_puttupleslot(bnlstate->bnl_tupleStoreState,
							slot);
	bnlstate->bnl_batchMatchedInfo =
		lappend_int(bnlstate->bnl_batchMatchedInfo, 0);
	tuplestore_gettupleslot(bnlstate->bnl_tupleStoreState, true, false, slot);
}

bool
FlushTupleTS(YbBatchedNestLoopState *bnlstate, ExprContext *econtext)
{
	Assert(bnlstate->bnl_tupleStoreState != NULL);
	while (bnlstate->bnl_batchTupNo < tuplestore_tuple_count(bnlstate->bnl_tupleStoreState))
	{
		ListCell *lc = list_nth_cell(bnlstate->bnl_batchMatchedInfo,
									 bnlstate->bnl_batchTupNo);
		if (lfirst_int(lc) == 0)
		{
			GetNewOuterTupleTS(bnlstate, econtext);
			return true;
		}
		tuplestore_skiptuples(bnlstate->bnl_tupleStoreState, 1, true);
		bnlstate->bnl_batchTupNo++;
	}
	return false;
}

void
RegisterOuterMatchTS(YbBatchedNestLoopState *bnlstate, ExprContext *econtext)
{
	Assert(bnlstate->bnl_tupleStoreState != NULL);
	(void) econtext;
	ListCell *lc = list_nth_cell(bnlstate->bnl_batchMatchedInfo,
								 bnlstate->bnl_batchTupNo - 1);
	lfirst_int(lc) = 1;
	return;
}

bool
GetNewOuterTupleTS(YbBatchedNestLoopState *bnlstate, ExprContext *econtext)
{
	Tuplestorestate *outertuples = bnlstate->bnl_tupleStoreState;
	while (!tuplestore_ateof(outertuples)
		&& tuplestore_tuple_count(outertuples) > 0
		&& tuplestore_gettupleslot(outertuples,
								   true,
								   false,
								   econtext->ecxt_outertuple))
	{
		int current_tup_no = bnlstate->bnl_batchTupNo;
		bnlstate->bnl_batchTupNo++;

		/*
		 * This has already been matched so no need to look at this again in a
		 * semijoin.
		 */
		if (bnlstate->js.single_match)
		{
			ListCell *lc = list_nth_cell(bnlstate->bnl_batchMatchedInfo,
										 current_tup_no);
			if (lfirst_int(lc) > 0)
			{
				continue;
			}
		}
		return true;
	}
	return false;
}

void
ResetBatchTS(YbBatchedNestLoopState *bnlstate, ExprContext *econtext)
{
	Tuplestorestate *outertuples = bnlstate->bnl_tupleStoreState;
	Assert(outertuples != NULL);
	tuplestore_rescan(outertuples);
	bnlstate->bnl_batchTupNo = 0;
}

void
FreeBatchTS(YbBatchedNestLoopState *bnlstate)
{
	Tuplestorestate *outertuples = bnlstate->bnl_tupleStoreState;
	if (!outertuples)
	{
		return;
	}

	tuplestore_clear(bnlstate->bnl_tupleStoreState);
	list_free(bnlstate->bnl_batchMatchedInfo);
	bnlstate->bnl_batchMatchedInfo = NIL;
}

/*
 * Clean up and end tuplestore state.
 */
void
EndTS(YbBatchedNestLoopState *bnlstate)
{
	tuplestore_end(bnlstate->bnl_tupleStoreState);
	list_free(bnlstate->bnl_batchMatchedInfo);
	bnlstate->bnl_batchMatchedInfo = NIL;
}

bool
CreateBatch(YbBatchedNestLoopState *bnlstate, ExprContext *econtext)
{
	YbBatchedNestLoop   *batchnl = (YbBatchedNestLoop *) bnlstate->js.ps.plan;
	TupleTableSlot *outerTupleSlot = NULL;
	PlanState  *outerPlan = outerPlanState(bnlstate);
	PlanState  *innerPlan = innerPlanState(bnlstate);
	LOCAL_JOIN_FN(FreeBatch, bnlstate);

	for (int batchno = 0; batchno < GetMaxBatchSize(batchnl); batchno++)
	{
		bool have_outer_tuple = false;
		elog(DEBUG2, "getting new outer tuple");
		if (batchno < GetCurrentBatchSize(bnlstate) &&
			!bnlstate->bnl_outerdone)
		{
			outerTupleSlot = ExecProcNode(outerPlan);
			/*
			 * We want to wrap up our current batch if the outerPlan has
			 * just been exhausted but don't want future invocations of
			 * CreateBatch to attempt ExecProcNode on outerPlan.
			 */
			have_outer_tuple = !TupIsNull(outerTupleSlot);
			bnlstate->bnl_outerdone = !have_outer_tuple;
		}

		/*
		 * if there are no more outer tuples, then the join is complete..
		 */
		if (bnlstate->bnl_outerdone)
		{
			if (batchno == 0)
			{
				elog(DEBUG2, "no outer tuple, ending join");
				return false;
			}
		}
		else
		{
			/*
			 * have_outer_tuple can be false even if bnlstate->bnl_outerdone
			 * is false if the pushed down LIMIT leads our first batch to be
			 * smaller than the outer table size. While we decrease the size
			 * of our first batch, we can't decrease the size of the inner
			 * index scan's corresponding IN list and must fill in the remaining
			 * values with NULLs.
			 */
			if (have_outer_tuple)
			{
				elog(DEBUG2, "saving new outer tuple information");
				if (TTS_IS_MINIMALTUPLE(outerTupleSlot))
					econtext->ecxt_outertuple = outerTupleSlot;
				else
					ExecCopySlot(econtext->ecxt_outertuple, outerTupleSlot);
				LOCAL_JOIN_FN(AddTupleToOuterBatch, bnlstate, econtext->ecxt_outertuple);
			}
		}

		/*
		 * fetch the values of any outer Vars that must be passed to the
		 * inner scan, and store them in the appropriate PARAM_EXEC slots.
		 */
		ListCell *lc;
		foreach(lc, batchnl->nl.nestParams)
		{
			NestLoopParam *nlp = (NestLoopParam *) lfirst(lc);
			int paramno = nlp->paramno + batchno;
			ParamExecData *prm;

			prm = &(econtext->ecxt_param_exec_vals[paramno]);
			/* Param value should be an OUTER_VAR var */
			Assert(IsA(nlp->paramval, Var));
			Assert(nlp->paramval->varno == OUTER_VAR);
			Assert(nlp->paramval->varattno > 0);
			if (have_outer_tuple)
			{
				prm->value = slot_getattr(econtext->ecxt_outertuple,
										  nlp->paramval->varattno,
										  &(prm->isnull));
			}
			else
			{
				prm->isnull = true;
			}
			/* Flag parameter value as changed */
			innerPlan->chgParam = bms_add_member(innerPlan->chgParam,
												 paramno);
		}
	}

	LOCAL_JOIN_FN(ResetBatch, bnlstate, econtext);

	return true;
}

int
GetCurrentBatchSize(YbBatchedNestLoopState *bnlstate)
{
	return bnlstate->batch_size;
}

int
GetMaxBatchSize(YbBatchedNestLoop *plan)
{
	Assert(list_length(plan->nl.nestParams) > 0);
	NestLoopParam *nlp =
		(NestLoopParam *) linitial(plan->nl.nestParams);
	Assert(nlp->yb_batch_size > 1);
	return nlp->yb_batch_size;
}


/* ----------------------------------------------------------------
 *		ExecInitYbBatchedNestLoop
 * ----------------------------------------------------------------
 */
YbBatchedNestLoopState *
ExecInitYbBatchedNestLoop(YbBatchedNestLoop *plan, EState *estate, int eflags)
{
	YbBatchedNestLoopState *bnlstate;

	/* check for unsupported flags */
	Assert(!(eflags & (EXEC_FLAG_BACKWARD | EXEC_FLAG_MARK)));

	NL1_printf("ExecInitYbBatchedNestLoop: %s\n",
			   "initializing node");

	/*
	 * create state structure
	 */
	bnlstate = makeNode(YbBatchedNestLoopState);
	bnlstate->js.ps.plan = (Plan *) plan;
	bnlstate->js.ps.state = estate;
	bnlstate->js.ps.ExecProcNode = ExecYbBatchedNestLoop;
	bnlstate->bnl_tupleStoreState = NULL;

	/*
	 * Miscellaneous initialization
	 *
	 * create expression context for node
	 */
	ExecAssignExprContext(estate, &bnlstate->js.ps);

	/*
	 * initialize child nodes
	 *
	 * If we have no parameters to pass into the inner rel from the outer,
	 * tell the inner child that cheap rescans would be good.  If we do have
	 * such parameters, then there is no point in REWIND support at all in the
	 * inner child, because it will always be rescanned with fresh parameter
	 * values.
	 */
	outerPlanState(bnlstate) = ExecInitNode(outerPlan(plan), estate, eflags);
	if (plan->nl.nestParams == NIL)
		eflags |= EXEC_FLAG_REWIND;
	else
		eflags &= ~EXEC_FLAG_REWIND;
	innerPlanState(bnlstate) = ExecInitNode(innerPlan(plan), estate, eflags);

	PlanState *outerPlan = outerPlanState(bnlstate);
	if (outerPlan->resultopsset && outerPlan->resultops != &TTSOpsMinimalTuple)
	{
		/* the outer tuple always has to be a minimal tuple */
		bnlstate->js.ps.outerops = &TTSOpsMinimalTuple;
		bnlstate->js.ps.outeropsfixed = true;
		bnlstate->js.ps.outeropsset = true;
		bnlstate->js.ps.ps_ExprContext->ecxt_outertuple =
			ExecInitExtraTupleSlot(estate,
								   outerPlan->ps_ResultTupleDesc,
								   &TTSOpsMinimalTuple);
	}

	/*
	 * Initialize result slot, type and projection.
	 */
	ExecInitResultTupleSlotTL(&bnlstate->js.ps, &TTSOpsMinimalTuple);
	ExecAssignProjectionInfo(&bnlstate->js.ps, NULL);

	/*
	 * initialize child expressions
	 */
	bnlstate->js.ps.qual =
		ExecInitQual(plan->nl.join.plan.qual, (PlanState *) bnlstate);
	bnlstate->js.jointype = plan->nl.join.jointype;
	bnlstate->js.joinqual =
		ExecInitQual(plan->nl.join.joinqual, (PlanState *) bnlstate);

	/*
	 * detect whether we need only consider the first matching inner tuple
	 */
	bnlstate->js.single_match = (plan->nl.join.inner_unique ||
								plan->nl.join.jointype == JOIN_SEMI);

	bnlstate->is_first_batch_done = false;

	/* set up null tuples for outer joins, if needed */
	switch (plan->nl.join.jointype)
	{
		case JOIN_INNER:
		case JOIN_SEMI:
			break;
		case JOIN_LEFT:
		case JOIN_ANTI:
			bnlstate->nl_NullInnerTupleSlot =
				ExecInitNullTupleSlot(estate,
									  ExecGetResultType(innerPlanState(bnlstate)),
									  &TTSOpsVirtual);
			break;
		default:
			elog(ERROR, "unrecognized join type: %d",
				 (int) plan->nl.join.jointype);
	}

	/*
	 * finally, reset the outer tuple batch state.
	 */

	NL1_printf("ExecInitYbBatchedNestLoop: %s\n",
			   "node initialized");

	bnlstate->bnl_currentstatus = BNL_INIT;
	bnlstate->bnl_batchMatchedInfo = NIL;
	bnlstate->bnl_batchTupNo = 0;
	bnlstate->bnl_outerdone = false;

	/* Initialize sorting structures. */
	bnlstate->bnl_needs_sorting = plan->numSortCols > 0;
	bnlstate->bnl_is_sorted = false;
	bnlstate->bnl_tuple_sort = NULL;

	if (UseHash(plan, bnlstate))
	{
		InitHash(bnlstate);
		REGISTER_LOCAL_JOIN_FN(FlushTuple, Hash);
		REGISTER_LOCAL_JOIN_FN(FlushTuple, Hash);
		REGISTER_LOCAL_JOIN_FN(GetNewOuterTuple, Hash);
		REGISTER_LOCAL_JOIN_FN(ResetBatch, Hash);
		REGISTER_LOCAL_JOIN_FN(RegisterOuterMatch, Hash);
		REGISTER_LOCAL_JOIN_FN(AddTupleToOuterBatch, Hash);
		REGISTER_LOCAL_JOIN_FN(FreeBatch, Hash);
		REGISTER_LOCAL_JOIN_FN(End, Hash);
	}
	else
	{
		InitTS(bnlstate);
		REGISTER_LOCAL_JOIN_FN(FlushTuple, TS);
		REGISTER_LOCAL_JOIN_FN(FlushTuple, TS);
		REGISTER_LOCAL_JOIN_FN(GetNewOuterTuple, TS);
		REGISTER_LOCAL_JOIN_FN(ResetBatch, TS);
		REGISTER_LOCAL_JOIN_FN(RegisterOuterMatch, TS);
		REGISTER_LOCAL_JOIN_FN(AddTupleToOuterBatch, TS);
		REGISTER_LOCAL_JOIN_FN(FreeBatch, TS);
		REGISTER_LOCAL_JOIN_FN(End, TS);
	}

	return bnlstate;
}

/* ----------------------------------------------------------------
 *		ExecEndYbBatchedNestLoop
 *
 *		closes down scans and frees allocated storage
 * ----------------------------------------------------------------
 */
void
ExecEndYbBatchedNestLoop(YbBatchedNestLoopState *bnlstate)
{
	NL1_printf("ExecEndYbBatchedNestLoop: %s\n",
			   "ending node processing");

	EndSorting(bnlstate);
	LOCAL_JOIN_FN(End, bnlstate);

	/*
	 * Free the exprcontext
	 */
	ExecFreeExprContext(&bnlstate->js.ps);

	/*
	 * clean out the tuple table
	 */
	ExecClearTuple(bnlstate->js.ps.ps_ResultTupleSlot);

	/*
	 * close down subplans
	 */
	ExecEndNode(outerPlanState(bnlstate));
	ExecEndNode(innerPlanState(bnlstate));

	NL1_printf("ExecEndYbBatchedNestLoop: %s\n",
			   "node processing ended");
}

/* ----------------------------------------------------------------
 *		ExecReScanYbBatchedNestLoop
 * ----------------------------------------------------------------
 */
void
ExecReScanYbBatchedNestLoop(YbBatchedNestLoopState *bnlstate)
{
	PlanState  *outerPlan = outerPlanState(bnlstate);

	/*
	 * If outerPlan->chgParam is not null then plan will be automatically
	 * re-scanned by first ExecProcNode.
	 */
	if (outerPlan->chgParam == NULL)
		ExecReScan(outerPlan);

	bnlstate->is_first_batch_done = false;
	EndSorting(bnlstate);

	LOCAL_JOIN_FN(FreeBatch, bnlstate);
	bnlstate->bnl_outerdone = false;
	bnlstate->bnl_currentstatus = BNL_INIT;

	/*
	 * innerPlan is re-scanned for each new outer tuple and MUST NOT be
	 * re-scanned from here or you'll get troubles from inner index scans when
	 * outer Vars are used as run-time keys...
	 */
}
