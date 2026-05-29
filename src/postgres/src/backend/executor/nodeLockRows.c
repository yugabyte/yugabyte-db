/*-------------------------------------------------------------------------
 *
 * nodeLockRows.c
 *	  Routines to handle FOR UPDATE/FOR SHARE row locking
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/nodeLockRows.c
 *
 *-------------------------------------------------------------------------
 */
/*
 * INTERFACE ROUTINES
 *		ExecLockRows		- fetch locked rows
 *		ExecInitLockRows	- initialize node and subnodes..
 *		ExecEndLockRows		- shutdown node and subnodes
 */

#include "postgres.h"

#include "access/tableam.h"
#include "access/xact.h"
#include "executor/executor.h"
#include "executor/nodeLockRows.h"
#include "foreign/fdwapi.h"
#include "miscadmin.h"
#include "utils/rel.h"

/* YB includes */
#include "access/yb_scan.h"
#include "utils/datum.h"
#include "utils/guc.h"


/*
 * ExecLockRowsBatchSkipLocked
 *
 * Optimized batch path for YB SKIP LOCKED.  Pre-fetches up to batch_size
 * candidate tuples from the scan, sends all their ybctids in a single RPC,
 * and the tserver locks the first unlocked one.
 *
 * Returns the winning slot (with the locked tuple restored) or NULL if all
 * candidates in this batch were skipped (caller should retry) or the scan
 * is exhausted.
 *
 * Candidates after the winner are saved into node->yb_batch_leftover_* so
 * subsequent ExecLockRows calls can try them via the per-row path without
 * losing rows consumed from the scan.
 *
 * *scan_exhausted is set to true when ExecProcNode returned NULL, meaning
 * there are no more rows to try.
 */
static TupleTableSlot *
ExecLockRowsBatchSkipLocked(LockRowsState *node,
							TupleTableSlot *first_slot,
							ExecAuxRowMark *aerm,
							ExecRowMark *erm,
							EState *estate,
							PlanState *outerPlan,
							bool *scan_exhausted)
{
	int				batch_size = yb_skip_locked_batch_size;
	Datum		   *batch_ybctids;
	HeapTuple	   *batch_tuples;
	int				batch_count = 0;
	TupleTableSlot *slot = first_slot;
	int				locked_index;
	TM_Result		result;

	*scan_exhausted = false;

	batch_ybctids = (Datum *) palloc(sizeof(Datum) * batch_size);
	batch_tuples = (HeapTuple *) palloc(sizeof(HeapTuple) * batch_size);

	/*
	 * Collect candidate ybctids.  The first slot was already fetched by the
	 * caller; fetch up to batch_size - 1 more.
	 */
	for (;;)
	{
		Datum	datum;
		bool	isNull;

		datum = ExecGetJunkAttribute(slot, aerm->ctidAttNo, &isNull);
		if (isNull)
			elog(ERROR, "ctid/ybctid is NULL");

		batch_ybctids[batch_count] = datumCopy(datum, false, -1);
		batch_tuples[batch_count] = ExecCopySlotHeapTuple(slot);
		batch_count++;

		if (batch_count >= batch_size)
			break;

		slot = ExecProcNode(outerPlan);
		if (TupIsNull(slot))
		{
			*scan_exhausted = true;
			break;
		}
	}

	if (batch_count == 0)
	{
		pfree(batch_ybctids);
		pfree(batch_tuples);
		return NULL;
	}

	/* Single batch RPC to lock the first available candidate. */
	result = YBCLockTupleBatch(erm->relation, batch_ybctids, batch_count,
							   erm->markType, estate, &locked_index);

	slot = NULL;

	if (result == TM_Ok && locked_index >= 0 && locked_index < batch_count)
	{
		/*
		 * Winner found.  Restore the winning tuple into the outer plan's
		 * result slot so the caller can return it.
		 */
		TupleTableSlot *result_slot = first_slot;

		ExecForceStoreHeapTuple(batch_tuples[locked_index], result_slot, true);
		slot = result_slot;

		/* Free candidates before the winner (they were skipped by tserver). */
		for (int i = 0; i < locked_index; i++)
		{
			pfree(DatumGetPointer(batch_ybctids[i]));
			heap_freetuple(batch_tuples[i]);
		}
		/* Winner ybctid datum no longer needed (tuple is in the slot). */
		pfree(DatumGetPointer(batch_ybctids[locked_index]));

		/*
		 * Save candidates after the winner as leftovers.  These were NOT tried
		 * by the tserver (it stops at the first success), so subsequent
		 * ExecLockRows calls will try them via the per-row path.
		 */
		int leftover_count = batch_count - locked_index - 1;
		if (leftover_count > 0)
		{
			node->yb_batch_leftover_tuples = (HeapTuple *)
				palloc(sizeof(HeapTuple) * leftover_count);
			node->yb_batch_leftover_ybctids = (Datum *)
				palloc(sizeof(Datum) * leftover_count);
			for (int i = 0; i < leftover_count; i++)
			{
				int src = locked_index + 1 + i;
				node->yb_batch_leftover_tuples[i] = batch_tuples[src];
				node->yb_batch_leftover_ybctids[i] = batch_ybctids[src];
			}
			node->yb_batch_leftover_count = leftover_count;
			node->yb_batch_leftover_idx = 0;
		}
	}
	else
	{
		/* All candidates were skipped. Free everything. */
		for (int i = 0; i < batch_count; i++)
		{
			pfree(DatumGetPointer(batch_ybctids[i]));
			heap_freetuple(batch_tuples[i]);
		}
	}

	pfree(batch_ybctids);
	pfree(batch_tuples);
	return slot;
}

/*
 * ExecLockRowsTryLeftover
 *
 * Try to lock the next leftover candidate from a previous batch RPC using
 * the per-row YBCLockTuple path.  Returns a slot if a row was locked, or
 * NULL if the leftover was skipped (caller should try the next one or
 * fall through to the scan).
 */
static TupleTableSlot *
ExecLockRowsTryLeftover(LockRowsState *node,
						ExecRowMark *erm,
						EState *estate,
						TupleTableSlot *result_slot)
{
	while (node->yb_batch_leftover_idx < node->yb_batch_leftover_count)
	{
		int		idx = node->yb_batch_leftover_idx++;
		Datum	ybctid = node->yb_batch_leftover_ybctids[idx];
		HeapTuple tup = node->yb_batch_leftover_tuples[idx];
		TM_Result res;

		res = YBCLockTuple(erm->relation, ybctid, erm->markType,
						   LockWaitSkip, estate);

		pfree(DatumGetPointer(ybctid));

		if (res == TM_Ok)
		{
			ExecForceStoreHeapTuple(tup, result_slot, true);

			/* Free remaining leftovers if this is the last one we need. */
			/* (They'll be freed when the next call consumes or exhausts them.) */
			return result_slot;
		}

		/* Skipped — free this tuple and try the next leftover. */
		heap_freetuple(tup);
	}

	/* All leftovers exhausted. Clean up. */
	if (node->yb_batch_leftover_tuples)
	{
		pfree(node->yb_batch_leftover_tuples);
		node->yb_batch_leftover_tuples = NULL;
	}
	if (node->yb_batch_leftover_ybctids)
	{
		pfree(node->yb_batch_leftover_ybctids);
		node->yb_batch_leftover_ybctids = NULL;
	}
	node->yb_batch_leftover_count = 0;
	node->yb_batch_leftover_idx = 0;

	return NULL;
}

/* ----------------------------------------------------------------
 *		ExecLockRows
 * ----------------------------------------------------------------
 */
static TupleTableSlot *			/* return: a tuple or NULL */
ExecLockRows(PlanState *pstate)
{
	LockRowsState *node = castNode(LockRowsState, pstate);
	TupleTableSlot *slot;
	EState	   *estate;
	PlanState  *outerPlan;
	bool		epq_needed;
	ListCell   *lc;

	CHECK_FOR_INTERRUPTS();

	/*
	 * get information from the node
	 */
	estate = node->ps.state;
	outerPlan = outerPlanState(node);

	/*
	 * Get next tuple from subplan, if any.
	 */
lnext:
	/*
	 * If we have leftover candidates from a previous batch SKIP LOCKED RPC,
	 * try to lock them one-by-one before pulling new rows from the scan.
	 * This is needed for LIMIT > 1: the batch path only returns the first
	 * winner, and leftover candidates may still be lockable.
	 */
	if (node->yb_batch_leftover_count > node->yb_batch_leftover_idx)
	{
		ExecAuxRowMark *aerm = (ExecAuxRowMark *) linitial(node->lr_arowMarks);
		ExecRowMark *erm = aerm->rowmark;
		TupleTableSlot *leftover_result;

		/* Use the outer plan's result slot — same slot the batch path uses. */
		leftover_result = ExecLockRowsTryLeftover(node, erm, estate,
												  outerPlan->ps_ResultTupleSlot);
		if (leftover_result != NULL)
			return leftover_result;
		/* All leftovers exhausted, fall through to scan for more rows. */
	}

	slot = ExecProcNode(outerPlan);

	if (node->yb_are_row_marks_for_yb_rels &&
		XactIsoLevel == XACT_SERIALIZABLE)
	{
		/*
		 * For YB relations, we don't lock tuples using this node in SERIALIZABLE level. Instead we take
		 * predicate locks by setting the row mark in read requests sent to txn participants.
		 */
		return slot;
	}

	if (TupIsNull(slot))
	{
		/* Release any resources held by EPQ mechanism before exiting */
		EvalPlanQualEnd(&node->lr_epqstate);
		return NULL;
	}

	/* We don't need EvalPlanQual unless we get updated tuple version(s) */
	epq_needed = false;

	/*
	 * YB batch SKIP LOCKED optimization: when we have exactly one YB row mark
	 * with SKIP LOCKED policy and batch size > 1, pre-fetch multiple candidates
	 * and lock the first available one in a single RPC.
	 */
	if (node->yb_are_row_marks_for_yb_rels &&
		yb_skip_locked_batch_size > 1 &&
		list_length(node->lr_arowMarks) == 1)
	{
		ExecAuxRowMark *aerm = (ExecAuxRowMark *) linitial(node->lr_arowMarks);
		ExecRowMark *erm = aerm->rowmark;

		if (erm->waitPolicy == LockWaitSkip)
		{
			bool			scan_exhausted;
			TupleTableSlot *result;

			result = ExecLockRowsBatchSkipLocked(node, slot, aerm, erm,
												 estate, outerPlan,
												 &scan_exhausted);
			if (result != NULL)
				return result;

			/* All candidates were skipped. Try more if scan has rows left. */
			if (!scan_exhausted)
				goto lnext;

			/* Scan exhausted, no rows locked. */
			EvalPlanQualEnd(&node->lr_epqstate);
			return NULL;
		}
	}

	/*
	 * Attempt to lock the source tuple(s).  (Note we only have locking
	 * rowmarks in lr_arowMarks.)
	 */
	foreach(lc, node->lr_arowMarks)
	{
		ExecAuxRowMark *aerm = (ExecAuxRowMark *) lfirst(lc);
		ExecRowMark *erm = aerm->rowmark;
		Datum		datum;
		bool		isNull;
		ItemPointerData tid;
		TM_FailureData tmfd;
		LockTupleMode lockmode;
		int			lockflags = 0;
		TM_Result	test;
		TupleTableSlot *markSlot;

		/* clear any leftover test tuple for this rel */
		markSlot = EvalPlanQualSlot(&node->lr_epqstate, erm->relation, erm->rti);
		ExecClearTuple(markSlot);

		/* if child rel, must check whether it produced this row */
		if (erm->rti != erm->prti)
		{
			Oid			tableoid;

			datum = ExecGetJunkAttribute(slot,
										 aerm->toidAttNo,
										 &isNull);
			/* shouldn't ever get a null result... */
			if (isNull)
				elog(ERROR, "tableoid is NULL");
			tableoid = DatumGetObjectId(datum);

			Assert(OidIsValid(erm->relid));
			if (tableoid != erm->relid)
			{
				/* this child is inactive right now */
				erm->ermActive = false;
				ItemPointerSetInvalid(&(erm->curCtid));
				ExecClearTuple(markSlot);
				continue;
			}
		}
		erm->ermActive = true;

		/* fetch the tuple's ctid */
		datum = ExecGetJunkAttribute(slot,
									 aerm->ctidAttNo,
									 &isNull);
		/* shouldn't ever get a null result... */
		if (isNull)
			elog(ERROR, "ctid is NULL");

		/* requests for foreign tables must be passed to their FDW */
		if (erm->relation->rd_rel->relkind == RELKIND_FOREIGN_TABLE)
		{
			FdwRoutine *fdwroutine;
			bool		updated = false;

			fdwroutine = GetFdwRoutineForRelation(erm->relation, false);
			/* this should have been checked already, but let's be safe */
			if (fdwroutine->RefetchForeignRow == NULL)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot lock rows in foreign table \"%s\"",
								RelationGetRelationName(erm->relation))));

			fdwroutine->RefetchForeignRow(estate,
										  erm,
										  datum,
										  markSlot,
										  &updated);
			if (TupIsNull(markSlot))
			{
				/* couldn't get the lock, so skip this row */
				goto lnext;
			}

			/*
			 * if FDW says tuple was updated before getting locked, we need to
			 * perform EPQ testing to see if quals are still satisfied
			 */
			if (updated)
				epq_needed = true;

			continue;
		}

		/* okay, try to lock (and fetch) the tuple */
		tid = *((ItemPointer) DatumGetPointer(datum));
		switch (erm->markType)
		{
			case ROW_MARK_EXCLUSIVE:
				lockmode = LockTupleExclusive;
				break;
			case ROW_MARK_NOKEYEXCLUSIVE:
				lockmode = LockTupleNoKeyExclusive;
				break;
			case ROW_MARK_SHARE:
				lockmode = LockTupleShare;
				break;
			case ROW_MARK_KEYSHARE:
				lockmode = LockTupleKeyShare;
				break;
			default:
				elog(ERROR, "unsupported rowmark type");
				lockmode = LockTupleNoKeyExclusive; /* keep compiler quiet */
				break;
		}

		lockflags = TUPLE_LOCK_FLAG_LOCK_UPDATE_IN_PROGRESS;
		if (!IsolationUsesXactSnapshot())
			lockflags |= TUPLE_LOCK_FLAG_FIND_LAST_VERSION;

		Assert(IsYBBackedRelation(erm->relation) ==
			   node->yb_are_row_marks_for_yb_rels);
		if (node->yb_are_row_marks_for_yb_rels)
		{
			if (!YbCanSkipIntentsWrite(erm->relation))
				test = YBCLockTuple(erm->relation, datum, erm->markType,
									erm->waitPolicy, estate);
			else
			{
				elog(DEBUG1, "Skipping lock acquisition for relation %u since intents are skipped", erm->relation->rd_id);
				test = TM_Ok;
			}
		}
		else
		{
			test = table_tuple_lock(erm->relation, &tid, estate->es_snapshot,
									markSlot, estate->es_output_cid,
									lockmode, erm->waitPolicy,
									lockflags,
									&tmfd);
		}

		switch (test)
		{
			case TM_WouldBlock:
				/* couldn't lock tuple in SKIP LOCKED mode */
				goto lnext;

			case TM_SelfModified:

				/*
				 * The target tuple was already updated or deleted by the
				 * current command, or by a later command in the current
				 * transaction.  We *must* ignore the tuple in the former
				 * case, so as to avoid the "Halloween problem" of repeated
				 * update attempts.  In the latter case it might be sensible
				 * to fetch the updated tuple instead, but doing so would
				 * require changing heap_update and heap_delete to not
				 * complain about updating "invisible" tuples, which seems
				 * pretty scary (table_tuple_lock will not complain, but few
				 * callers expect TM_Invisible, and we're not one of them). So
				 * for now, treat the tuple as deleted and do not process.
				 */
				goto lnext;

			case TM_Ok:

				/*
				 * Got the lock successfully, the locked tuple saved in
				 * markSlot for, if needed, EvalPlanQual testing below.
				 *
				 * YB: TODO(Piyush): If we use EvalPlanQual for READ
				 * COMMITTED in future:
				 * - remove !IsYBBackedRelation(erm->relation)
				 * - In YBCLockTuple():
				 *	- initialize tmfd.traversed to false
				 *	- if the tuple being locked is updated, populate latest
				 *    tuple version in markSlot and set tmfd.traversed to true
				 */
				if (!IsYBBackedRelation(erm->relation) && tmfd.traversed)
					epq_needed = true;
				break;

			case TM_Updated:
				/*
				 * YB: TODO(Piyush): If handling using EvalPlanQual for READ
				 * COMMITTED in future, replace
				 * IsYBBackedRelation(erm->relation) with
				 * IsolationUsesXactSnapshot().
				 */
				if (IsYBBackedRelation(erm->relation))
					YBCHandleConflictError(erm->relation, erm->waitPolicy);

				if (IsolationUsesXactSnapshot())
					ereport(ERROR,
							(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
							 errmsg("could not serialize access due to concurrent update")));
				elog(ERROR, "unexpected table_tuple_lock status: %u",
					 test);
				break;

			case TM_Deleted:
				if (IsolationUsesXactSnapshot())
					ereport(ERROR,
							(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
							 errmsg("could not serialize access due to concurrent update")));
				/* tuple was deleted so don't return it */
				goto lnext;

			case TM_Invisible:
				elog(ERROR, "attempted to lock invisible tuple");
				break;

			default:
				elog(ERROR, "unrecognized table_tuple_lock status: %u",
					 test);
		}

		if (!IsYBBackedRelation(erm->relation))
		{
			/*
			 * Remember locked tuple's TID for EPQ testing and WHERE CURRENT
			 * OF
			 */
			erm->curCtid = tid;
		}
	}

	/*
	 * If we need to do EvalPlanQual testing, do so.
	 */
	if (epq_needed)
	{
		/* Initialize EPQ machinery */
		EvalPlanQualBegin(&node->lr_epqstate);

		/*
		 * To fetch non-locked source rows the EPQ logic needs to access junk
		 * columns from the tuple being tested.
		 */
		EvalPlanQualSetSlot(&node->lr_epqstate, slot);

		/*
		 * And finally we can re-evaluate the tuple.
		 */
		slot = EvalPlanQualNext(&node->lr_epqstate);
		if (TupIsNull(slot))
		{
			/* Updated tuple fails qual, so ignore it and go on */
			goto lnext;
		}
	}

	/* Got all locks, so return the current tuple */
	return slot;
}

/* ----------------------------------------------------------------
 *		ExecInitLockRows
 *
 *		This initializes the LockRows node state structures and
 *		the node's subplan.
 * ----------------------------------------------------------------
 */
LockRowsState *
ExecInitLockRows(LockRows *node, EState *estate, int eflags)
{
	LockRowsState *lrstate;
	Plan	   *outerPlan = outerPlan(node);
	List	   *epq_arowmarks;
	ListCell   *lc;

	/* check for unsupported flags */
	Assert(!(eflags & EXEC_FLAG_MARK));

	/*
	 * create state structure
	 */
	lrstate = makeNode(LockRowsState);
	lrstate->ps.plan = (Plan *) node;
	lrstate->ps.state = estate;
	lrstate->ps.ExecProcNode = ExecLockRows;

	/*
	 * Miscellaneous initialization
	 *
	 * LockRows nodes never call ExecQual or ExecProject, therefore no
	 * ExprContext is needed.
	 */

	/*
	 * Initialize result type.
	 */
	ExecInitResultTypeTL(&lrstate->ps);

	/*
	 * then initialize outer plan
	 */
	outerPlanState(lrstate) = ExecInitNode(outerPlan, estate, eflags);

	/* node returns unmodified slots from the outer plan */
	lrstate->ps.resultopsset = true;
	lrstate->ps.resultops = ExecGetResultSlotOps(outerPlanState(lrstate),
												 &lrstate->ps.resultopsfixed);

	/*
	 * LockRows nodes do no projections, so initialize projection info for
	 * this node appropriately
	 */
	lrstate->ps.ps_ProjInfo = NULL;

	/*
	 * Locate the ExecRowMark(s) that this node is responsible for, and
	 * construct ExecAuxRowMarks for them.  (InitPlan should already have
	 * built the global list of ExecRowMarks.)
	 */
	lrstate->lr_arowMarks = NIL;
	epq_arowmarks = NIL;
	bool		row_lock_for_yb_rel_found = false;
	bool		row_lock_for_non_yb_rel_found = false;

	foreach(lc, node->rowMarks)
	{
		PlanRowMark *rc = lfirst_node(PlanRowMark, lc);
		ExecRowMark *erm;
		ExecAuxRowMark *aerm;

		/* ignore "parent" rowmarks; they are irrelevant at runtime */
		if (rc->isParent)
			continue;

		/* find ExecRowMark and build ExecAuxRowMark */
		erm = ExecFindRowMark(estate, rc->rti, false);
		aerm = ExecBuildAuxRowMark(erm, outerPlan->targetlist);

		/*
		 * Only locking rowmarks go into our own list.  Non-locking marks are
		 * passed off to the EvalPlanQual machinery.  This is because we don't
		 * want to bother fetching non-locked rows unless we actually have to
		 * do an EPQ recheck.
		 */
		if (RowMarkRequiresRowShareLock(erm->markType))
		{
			if (IsYBBackedRelation(erm->relation))
				row_lock_for_yb_rel_found = true;
			else
				row_lock_for_non_yb_rel_found = true;

			lrstate->lr_arowMarks = lappend(lrstate->lr_arowMarks, aerm);
		}
		else
			epq_arowmarks = lappend(epq_arowmarks, aerm);
	}

	if (row_lock_for_yb_rel_found && row_lock_for_non_yb_rel_found)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("mixing Yugabyte relations and not Yugabyte "
						"relations with row locks is not supported")));

	if (row_lock_for_non_yb_rel_found)
		YbSetTxnUsesTempRel();

	lrstate->yb_are_row_marks_for_yb_rels = row_lock_for_yb_rel_found;

	/* Now we have the info needed to set up EPQ state */
	EvalPlanQualInit(&lrstate->lr_epqstate, estate,
					 outerPlan, epq_arowmarks, node->epqParam);

	return lrstate;
}

/* ----------------------------------------------------------------
 *		ExecEndLockRows
 *
 *		This shuts down the subplan and frees resources allocated
 *		to this node.
 * ----------------------------------------------------------------
 */
void
ExecEndLockRows(LockRowsState *node)
{
	/* Free any leftover batch SKIP LOCKED candidates. */
	if (node->yb_batch_leftover_tuples)
	{
		for (int i = node->yb_batch_leftover_idx;
			 i < node->yb_batch_leftover_count; i++)
		{
			pfree(DatumGetPointer(node->yb_batch_leftover_ybctids[i]));
			heap_freetuple(node->yb_batch_leftover_tuples[i]);
		}
		pfree(node->yb_batch_leftover_tuples);
		pfree(node->yb_batch_leftover_ybctids);
		node->yb_batch_leftover_tuples = NULL;
		node->yb_batch_leftover_ybctids = NULL;
	}

	/* We may have shut down EPQ already, but no harm in another call */
	EvalPlanQualEnd(&node->lr_epqstate);
	ExecEndNode(outerPlanState(node));
}


void
ExecReScanLockRows(LockRowsState *node)
{
	/*
	 * if chgParam of subnode is not null then plan will be re-scanned by
	 * first ExecProcNode.
	 */
	if (node->ps.lefttree->chgParam == NULL)
		ExecReScan(node->ps.lefttree);
}

/* ----------------------------------------------------------------
 *		ExecShutdownLockRows
 *
 *		YB: This flushes the explicit row lock buffer once there are no
 *    more rows to be locked.
 *
 * ----------------------------------------------------------------
 */
void
ExecShutdownLockRows(LockRowsState *node)
{
	YBCFlushTupleLocks();
}
