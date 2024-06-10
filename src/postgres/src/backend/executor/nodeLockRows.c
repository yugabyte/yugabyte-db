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
#include "access/yb_scan.h"
#include "executor/executor.h"
#include "executor/nodeLockRows.h"
#include "foreign/fdwapi.h"
#include "miscadmin.h"
#include "utils/rel.h"


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
	slot = ExecProcNode(outerPlan);

	int n_yb_relations = 0;
	int n_relations = 0;
	foreach(lc, node->lr_arowMarks)
	{
		ExecAuxRowMark *aerm = (ExecAuxRowMark *) lfirst(lc);
		ExecRowMark *erm = aerm->rowmark;
		if (IsYBBackedRelation(erm->relation))
		{
			n_yb_relations++;
		}
		n_relations++;
	}

	if (n_yb_relations > 0 && n_yb_relations != n_relations)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("Mixing Yugabyte relations and not Yugabyte relations with "
						"row locks is not supported")));

	if (n_yb_relations > 0 && XactIsoLevel == XACT_SERIALIZABLE) {
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

		if (IsYBBackedRelation(erm->relation))
		{
			test = YBCLockTuple(
				erm->relation, datum, erm->markType, erm->waitPolicy, estate);
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
				 * TODO(Piyush): If we use EvalPlanQual for READ
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
				 * TODO(Piyush): If handling using EvalPlanQual for READ
				 * COMMITTED in future, replace
				 * IsYBBackedRelation(erm->relation) with
				 * IsolationUsesXactSnapshot().
				 */
				if (IsYBBackedRelation(erm->relation))
				{
					if (erm->waitPolicy == LockWaitError)
					{
						// In case the user has specified NOWAIT, the intention is to error out immediately. If
						// we raise TransactionErrorCode::kConflict, the statement might be retried by our
						// retry logic in yb_attempt_to_restart_on_error().
						ereport(ERROR,
							(errcode(ERRCODE_LOCK_NOT_AVAILABLE),
							 errmsg("could not obtain lock on row in relation \"%s\"",
											RelationGetRelationName(erm->relation))));
					}

					ereport(ERROR,
							(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
							errmsg("could not serialize access due to concurrent update"),
							yb_txn_errcode(YBCGetTxnConflictErrorCode())));
				}

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
			/* Remember locked tuple's TID for EPQ testing and WHERE CURRENT OF */
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
			lrstate->lr_arowMarks = lappend(lrstate->lr_arowMarks, aerm);
		else
			epq_arowmarks = lappend(epq_arowmarks, aerm);
	}

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
