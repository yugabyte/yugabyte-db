/*-------------------------------------------------------------------------
 *
 * execIndexing.c
 *	  routines for inserting index tuples and enforcing unique and
 *	  exclusion constraints.
 *
 * ExecInsertIndexTuples() is the main entry point.  It's called after
 * inserting a tuple to the heap, and it inserts corresponding index tuples
 * into all indexes.  At the same time, it enforces any unique and
 * exclusion constraints:
 *
 * Unique Indexes
 * --------------
 *
 * Enforcing a unique constraint is straightforward.  When the index AM
 * inserts the tuple to the index, it also checks that there are no
 * conflicting tuples in the index already.  It does so atomically, so that
 * even if two backends try to insert the same key concurrently, only one
 * of them will succeed.  All the logic to ensure atomicity, and to wait
 * for in-progress transactions to finish, is handled by the index AM.
 *
 * If a unique constraint is deferred, we request the index AM to not
 * throw an error if a conflict is found.  Instead, we make note that there
 * was a conflict and return the list of indexes with conflicts to the
 * caller.  The caller must re-check them later, by calling index_insert()
 * with the UNIQUE_CHECK_EXISTING option.
 *
 * Exclusion Constraints
 * ---------------------
 *
 * Exclusion constraints are different from unique indexes in that when the
 * tuple is inserted to the index, the index AM does not check for
 * duplicate keys at the same time.  After the insertion, we perform a
 * separate scan on the index to check for conflicting tuples, and if one
 * is found, we throw an error and the transaction is aborted.  If the
 * conflicting tuple's inserter or deleter is in-progress, we wait for it
 * to finish first.
 *
 * There is a chance of deadlock, if two backends insert a tuple at the
 * same time, and then perform the scan to check for conflicts.  They will
 * find each other's tuple, and both try to wait for each other.  The
 * deadlock detector will detect that, and abort one of the transactions.
 * That's fairly harmless, as one of them was bound to abort with a
 * "duplicate key error" anyway, although you get a different error
 * message.
 *
 * If an exclusion constraint is deferred, we still perform the conflict
 * checking scan immediately after inserting the index tuple.  But instead
 * of throwing an error if a conflict is found, we return that information
 * to the caller.  The caller must re-check them later by calling
 * check_exclusion_constraint().
 *
 * Speculative insertion
 * ---------------------
 *
 * Speculative insertion is a two-phase mechanism used to implement
 * INSERT ... ON CONFLICT DO UPDATE/NOTHING.  The tuple is first inserted
 * to the heap and update the indexes as usual, but if a constraint is
 * violated, we can still back out the insertion without aborting the whole
 * transaction.  In an INSERT ... ON CONFLICT statement, if a conflict is
 * detected, the inserted tuple is backed out and the ON CONFLICT action is
 * executed instead.
 *
 * Insertion to a unique index works as usual: the index AM checks for
 * duplicate keys atomically with the insertion.  But instead of throwing
 * an error on a conflict, the speculatively inserted heap tuple is backed
 * out.
 *
 * Exclusion constraints are slightly more complicated.  As mentioned
 * earlier, there is a risk of deadlock when two backends insert the same
 * key concurrently.  That was not a problem for regular insertions, when
 * one of the transactions has to be aborted anyway, but with a speculative
 * insertion we cannot let a deadlock happen, because we only want to back
 * out the speculatively inserted tuple on conflict, not abort the whole
 * transaction.
 *
 * When a backend detects that the speculative insertion conflicts with
 * another in-progress tuple, it has two options:
 *
 * 1. back out the speculatively inserted tuple, then wait for the other
 *	  transaction, and retry. Or,
 * 2. wait for the other transaction, with the speculatively inserted tuple
 *	  still in place.
 *
 * If two backends insert at the same time, and both try to wait for each
 * other, they will deadlock.  So option 2 is not acceptable.  Option 1
 * avoids the deadlock, but it is prone to a livelock instead.  Both
 * transactions will wake up immediately as the other transaction backs
 * out.  Then they both retry, and conflict with each other again, lather,
 * rinse, repeat.
 *
 * To avoid the livelock, one of the backends must back out first, and then
 * wait, while the other one waits without backing out.  It doesn't matter
 * which one backs out, so we employ an arbitrary rule that the transaction
 * with the higher XID backs out.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/execIndexing.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "access/relscan.h"
#include "access/tableam.h"
#include "access/xact.h"
#include "catalog/index.h"
#include "executor/executor.h"
#include "nodes/nodeFuncs.h"
#include "storage/lmgr.h"
#include "utils/snapmgr.h"

/* Yugabyte includes */
#include "catalog/pg_am_d.h"
#include "executor/ybcModifyTable.h"
#include "funcapi.h"
#include "utils/relcache.h"

/* waitMode argument to check_exclusion_or_unique_constraint() */
typedef enum
{
	CEOUC_WAIT,
	CEOUC_NOWAIT,
	CEOUC_LIVELOCK_PREVENTING_WAIT
} CEOUC_WAIT_MODE;

static bool check_exclusion_or_unique_constraint(Relation heap, Relation index,
												 IndexInfo *indexInfo,
												 ItemPointer tupleid,
												 Datum *values, bool *isnull,
												 EState *estate, bool newIndex,
												 CEOUC_WAIT_MODE waitMode,
												 bool errorOK,
												 ItemPointer conflictTid,
												 TupleTableSlot **ybConflictSlot);

static bool index_recheck_constraint(Relation index, Oid *constr_procs,
									 Datum *existing_values, bool *existing_isnull,
									 Datum *new_values);
static bool index_unchanged_by_update(ResultRelInfo *resultRelInfo,
									  EState *estate, IndexInfo *indexInfo,
									  Relation indexRelation);
static bool index_expression_changed_walker(Node *node,
											Bitmapset *allUpdatedCols);

static void yb_batch_fetch_conflicting_rows(int idx,
											ResultRelInfo *resultRelInfo,
											YbInsertOnConflictBatchState *yb_ioc_state,
											EState *estate);

/* ----------------------------------------------------------------
 *		ExecOpenIndices
 *
 *		Find the indices associated with a result relation, open them,
 *		and save information about them in the result ResultRelInfo.
 *
 *		At entry, caller has already opened and locked
 *		resultRelInfo->ri_RelationDesc.
 * ----------------------------------------------------------------
 */
void
ExecOpenIndices(ResultRelInfo *resultRelInfo, bool speculative)
{
	Relation	resultRelation = resultRelInfo->ri_RelationDesc;
	List	   *indexoidlist;
	ListCell   *l;
	int			len,
				i;
	RelationPtr relationDescs;
	IndexInfo **indexInfoArray;

	resultRelInfo->ri_NumIndices = 0;

	/* fast path if no indexes */
	if (!RelationGetForm(resultRelation)->relhasindex)
		return;

	/*
	 * Get cached list of index OIDs
	 */
	indexoidlist = RelationGetIndexList(resultRelation);
	len = list_length(indexoidlist);
	if (len == 0)
		return;

	/*
	 * allocate space for result arrays
	 */
	relationDescs = (RelationPtr) palloc(len * sizeof(Relation));
	indexInfoArray = (IndexInfo **) palloc(len * sizeof(IndexInfo *));

	resultRelInfo->ri_NumIndices = len;
	resultRelInfo->ri_IndexRelationDescs = relationDescs;
	resultRelInfo->ri_IndexRelationInfo = indexInfoArray;

	/*
	 * For each index, open the index relation and save pg_index info. We
	 * acquire RowExclusiveLock, signifying we will update the index.
	 *
	 * Note: we do this even if the index is not indisready; it's not worth
	 * the trouble to optimize for the case where it isn't.
	 */
	i = 0;
	foreach(l, indexoidlist)
	{
		Oid			indexOid = lfirst_oid(l);
		Relation	indexDesc;
		IndexInfo  *ii;

		indexDesc = index_open(indexOid, RowExclusiveLock);

		/* extract index key information from the index's pg_index info */
		ii = BuildIndexInfo(indexDesc);

		/*
		 * If the indexes are to be used for speculative insertion, add extra
		 * information required by unique index entries.
		 */
		if (speculative && ii->ii_Unique)
			BuildSpeculativeIndexInfo(indexDesc, ii);

		relationDescs[i] = indexDesc;
		indexInfoArray[i] = ii;
		i++;
	}

	list_free(indexoidlist);
}

/* ----------------------------------------------------------------
 *		ExecCloseIndices
 *
 *		Close the index relations stored in resultRelInfo
 * ----------------------------------------------------------------
 */
void
ExecCloseIndices(ResultRelInfo *resultRelInfo)
{
	int			i;
	int			numIndices;
	RelationPtr indexDescs;

	numIndices = resultRelInfo->ri_NumIndices;
	indexDescs = resultRelInfo->ri_IndexRelationDescs;

	for (i = 0; i < numIndices; i++)
	{
		if (indexDescs[i] == NULL)
			continue;			/* shouldn't happen? */

		/* Drop lock acquired by ExecOpenIndices */
		index_close(indexDescs[i], RowExclusiveLock);
	}

	/*
	 * XXX should free indexInfo array here too?  Currently we assume that
	 * such stuff will be cleaned up automatically in FreeExecutorState.
	 */
}

/* ----------------------------------------------------------------
 *		YbExecDoInsertIndexTuple
 *
 *		This routine performs insertion of an index tuple of 'indexRelation'
 *		that is identified by a combination of the base table CTID ('tuple->t_ybctid')
 *		and data in the base table's tuple slot ('slot').
 *		This routine has been refactored out of ExecInsertIndexTuples so that
 *		Yugabyte's index update routine can invoke this routine directly without
 *		needing to duplicate a bunch of checks in ExecInsertIndexTuples.
 *		This routine is invoked by both Yugabyte and non-YB relations.
 * ----------------------------------------------------------------
 */
static bool
YbExecDoInsertIndexTuple(ResultRelInfo *resultRelInfo,
						 Relation indexRelation,
						 IndexInfo *indexInfo,
						 TupleTableSlot *slot,
						 EState *estate,
						 bool noDupErr,
						 bool *specConflict,
						 List *arbiterIndexes,
						 bool update,
						 ItemPointer tupleid)
{
	bool		applyNoDupErr;
	IndexUniqueCheck checkUnique;
	bool		indexUnchanged;
	bool		satisfiesConstraint;
	bool		deferredCheck = false;
	Datum		values[INDEX_MAX_KEYS];
	bool		isnull[INDEX_MAX_KEYS];
	Relation	heapRelation;
	bool		isYBRelation;

	heapRelation = resultRelInfo->ri_RelationDesc;
	isYBRelation = IsYBRelation(heapRelation);

	/*
	 * FormIndexDatum fills in its values and isnull parameters with the
	 * appropriate values for the column(s) of the index.
	 */
	FormIndexDatum(indexInfo,
				   slot,
				   estate,
				   values,
				   isnull);

	/*
	 * After updating INSERT ON CONFLICT batching map, PK is no longer
	 * relevant from here on.
	 */
	if (isYBRelation && indexRelation->rd_index->indisprimary)
		return deferredCheck;

	/* Check whether to apply noDupErr to this index */
	applyNoDupErr = noDupErr &&
		(arbiterIndexes == NIL ||
			list_member_oid(arbiterIndexes,
							indexRelation->rd_index->indexrelid));

	/*
	 * The index AM does the actual insertion, plus uniqueness checking.
	 *
	 * For an immediate-mode unique index, we just tell the index AM to
	 * throw error if not unique.
	 *
	 * For a deferrable unique index, we tell the index AM to just detect
	 * possible non-uniqueness, and we add the index OID to the result
	 * list if further checking is needed.
	 *
	 * For a speculative insertion (used by INSERT ... ON CONFLICT), do
	 * the same as for a deferrable unique index.
	 */
	if (!indexRelation->rd_index->indisunique)
		checkUnique = UNIQUE_CHECK_NO;
	else if (applyNoDupErr)
		checkUnique = UNIQUE_CHECK_PARTIAL;
	else if (indexRelation->rd_index->indimmediate)
		checkUnique = UNIQUE_CHECK_YES;
	else
		checkUnique = UNIQUE_CHECK_PARTIAL;

	/*
	 * There's definitely going to be an index_insert() call for this
	 * index.  If we're being called as part of an UPDATE statement,
	 * consider if the 'indexUnchanged' = true hint should be passed.
	 *
	 * YB Note: In case of a Yugabyte relation, we have already computed if
	 * the index is unchanged (partly at planning time, and partly during
	 * execution in ExecUpdate). Further, the result of this computation is
	 * not just a hint, but is enforced by skipping RPCs to the storage
	 * layer. Hence this variable is not relevant for Yugabyte relations and
	 * will always evaluate to false.
	 */
	indexUnchanged = !isYBRelation && update && index_unchanged_by_update(resultRelInfo,
																		  estate,
																		  indexInfo,
																		  indexRelation);

	satisfiesConstraint =
		index_insert(indexRelation, /* index relation */
					 values,	/* array of index Datums */
					 isnull,	/* null flags */
					 tupleid,	/* tid of heap tuple */
					 slot->tts_ybctid,
					 heapRelation,	/* heap relation */
					 checkUnique,	/* type of uniqueness check to do */
					 indexUnchanged,	/* UPDATE without logical change? */
					 indexInfo,
					 false /* yb_shared_insert */);

	/*
	 * If the index has an associated exclusion constraint, check that.
	 * This is simpler than the process for uniqueness checks since we
	 * always insert first and then check.  If the constraint is deferred,
	 * we check now anyway, but don't throw error on violation or wait for
	 * a conclusive outcome from a concurrent insertion; instead we'll
	 * queue a recheck event.  Similarly, noDupErr callers (speculative
	 * inserters) will recheck later, and wait for a conclusive outcome
	 * then.
	 *
	 * An index for an exclusion constraint can't also be UNIQUE (not an
	 * essential property, we just don't allow it in the grammar), so no
	 * need to preserve the prior state of satisfiesConstraint.
	 */
	if (indexInfo->ii_ExclusionOps != NULL)
	{
		bool		violationOK;
		CEOUC_WAIT_MODE waitMode;

		if (applyNoDupErr)
		{
			violationOK = true;
			waitMode = CEOUC_LIVELOCK_PREVENTING_WAIT;
		}
		else if (!indexRelation->rd_index->indimmediate)
		{
			violationOK = true;
			waitMode = CEOUC_NOWAIT;
		}
		else
		{
			violationOK = false;
			waitMode = CEOUC_WAIT;
		}

		satisfiesConstraint =
			check_exclusion_or_unique_constraint(heapRelation,
												 indexRelation, indexInfo,
												 tupleid, values, isnull,
												 estate, false,
												 waitMode, violationOK, NULL,
												 NULL /* ybConflictSlot */);
	}

	if ((checkUnique == UNIQUE_CHECK_PARTIAL ||
			indexInfo->ii_ExclusionOps != NULL) &&
		!satisfiesConstraint)
	{
		/*
		 * This should not happen for YB relations which neither support
		 * exclusion constraints nor honor UNIQUE_CHECK_PARTIAL.
		 */
		Assert(!IsYBRelation(indexRelation));

		/*
		 * The tuple potentially violates the uniqueness or exclusion
		 * constraint, so make a note of the index so that we can re-check
		 * it later.  Speculative inserters are told if there was a
		 * speculative conflict, since that always requires a restart.
		 */
		deferredCheck = true;
		if (indexRelation->rd_index->indimmediate && specConflict)
			*specConflict = true;
	}

	return deferredCheck;
}

/* ----------------------------------------------------------------
 *		ExecInsertIndexTuples
 *
 *		This routine takes care of inserting index tuples
 *		into all the relations indexing the result relation
 *		when a heap tuple is inserted into the result relation.
 *
 *		When 'update' is true, executor is performing an UPDATE
 *		that could not use an optimization like heapam's HOT (in
 *		more general terms a call to table_tuple_update() took
 *		place and set 'update_indexes' to true).  Receiving this
 *		hint makes us consider if we should pass down the
 *		'indexUnchanged' hint in turn.  That's something that we
 *		figure out for each index_insert() call iff 'update' is
 *		true.  (When 'update' is false we already know not to pass
 *		the hint to any index.)
 *
 *		Unique and exclusion constraints are enforced at the same
 *		time.  This returns a list of index OIDs for any unique or
 *		exclusion constraints that are deferred and that had
 *		potential (unconfirmed) conflicts.  (if noDupErr == true,
 *		the same is done for non-deferred constraints, but report
 *		if conflict was speculative or deferred conflict to caller)
 *
 *		If 'arbiterIndexes' is nonempty, noDupErr applies only to
 *		those indexes.  NIL means noDupErr applies to all indexes.
 * ----------------------------------------------------------------
 */
List *
ExecInsertIndexTuples(ResultRelInfo *resultRelInfo,
					  TupleTableSlot *slot,
					  EState *estate,
					  bool update,
					  bool noDupErr,
					  bool *specConflict,
					  List *arbiterIndexes)
{
	ItemPointer tupleid = &slot->tts_tid;
	List	   *result = NIL;
	int			i;
	int			numIndices;
	RelationPtr relationDescs;
	Relation	heapRelation;
	IndexInfo **indexInfoArray;
	ExprContext *econtext;
	bool		isYBRelation;

	/*
	 * Get information from the result relation info structure.
	 */
	numIndices = resultRelInfo->ri_NumIndices;
	relationDescs = resultRelInfo->ri_IndexRelationDescs;
	indexInfoArray = resultRelInfo->ri_IndexRelationInfo;
	heapRelation = resultRelInfo->ri_RelationDesc;
	isYBRelation = IsYBRelation(heapRelation);

	if (!isYBRelation)
		Assert(ItemPointerIsValid(tupleid));
	else
		Assert(slot->tts_ybctid ||
			   !YBCRelInfoHasSecondaryIndices(resultRelInfo));

	/* Sanity check: slot must belong to the same rel as the resultRelInfo. */
	Assert(slot->tts_tableOid == RelationGetRelid(heapRelation));

	/*
	 * We will use the EState's per-tuple context for evaluating predicates
	 * and index expressions (creating it if it's not already there).
	 */
	econtext = GetPerTupleExprContext(estate);

	/* Arrange for econtext's scan tuple to be the tuple under test */
	econtext->ecxt_scantuple = slot;

	/*
	 * for each index, form and insert the index tuple
	 */
	for (i = 0; i < numIndices; i++)
	{
		Relation	indexRelation = relationDescs[i];
		IndexInfo  *indexInfo;

		indexInfo = indexInfoArray[i];
		Assert(indexInfo->ii_ReadyForInserts ==
			   indexRelation->rd_index->indisready);

		/*
		 * No need to update YugaByte primary key which is intrinic part of
		 * the base table.
		 *
		 * TODO(neil) The following YB check might not be needed due to later work on indexes.
		 * We keep this check for now as this bugfix will be backported to ealier releases.
		 */
		if (isYBRelation && indexRelation->rd_index->indisprimary)
			continue;

		/* If the index is marked as read-only, ignore it */
		if (!indexInfo->ii_ReadyForInserts)
			continue;

		/* Check for partial index */
		if (indexInfo->ii_Predicate != NIL)
		{
			if (!YbIsPartialIndexPredicateSatisfied(indexInfo, estate))
				continue;
		}

		if (YbExecDoInsertIndexTuple(resultRelInfo, indexRelation, indexInfo,
									 slot, estate, noDupErr,
									 specConflict, arbiterIndexes, update,
									 tupleid))
			result = lappend_oid(result, RelationGetRelid(indexRelation));
	}

	return result;
}

/* ----------------------------------------------------------------
 *		YbExecDoDeleteIndexTuple
 *
 *		This routine performs deletion of an index tuple of 'indexRelation'
 *		that is identified by a combination of the base table CTID ('ybctid')
 *		and data in the base table's tuple slot ('slot').
 *		This routine has been refactored out of ExecDeleteIndexTuples so that
 *		Yugabyte's index update routine can invoke this routine directly without
 *		needing to duplicate a bunch of checks in ExecDeleteIndexTuples.
 *		This routine is currently only invoked by Yugabyte relations.
 * ----------------------------------------------------------------
 */
static void
YbExecDoDeleteIndexTuple(ResultRelInfo *resultRelInfo,
						 Relation indexRelation,
						 IndexInfo *indexInfo,
						 TupleTableSlot *slot,
						 Datum ybctid,
						 EState *estate)
{
	Datum		values[INDEX_MAX_KEYS];
	bool		isnull[INDEX_MAX_KEYS];
	Relation	heapRelation;

	heapRelation = resultRelInfo->ri_RelationDesc;

	/*
	 * FormIndexDatum fills in its values and isnull parameters with the
	 * appropriate values for the column(s) of the index.
	 */
	FormIndexDatum(indexInfo, slot, estate, values, isnull);

	if (!(IsYBRelation(heapRelation) && indexRelation->rd_index->indisprimary))
	{
		MemoryContext oldContext = MemoryContextSwitchTo(GetPerTupleMemoryContext(estate));
		yb_index_delete(indexRelation, /* index relation */
						values,	/* array of index Datums */
						isnull,	/* null flags */
						ybctid,	/* ybctid */
						heapRelation,	/* heap relation */
						indexInfo);	/* index AM may need this */
		MemoryContextSwitchTo(oldContext);
	}
}

/* ----------------------------------------------------------------
 *		ExecDeleteIndexTuples
 *
 *		This routine takes care of deleting index tuples
 *		from all the relations indexing the result relation
 *		when a heap tuple is updated or deleted in the result relation.
 *      This is used only for relations and indexes backed by YugabyteDB.
 * ----------------------------------------------------------------
 */
void
ExecDeleteIndexTuples(ResultRelInfo *resultRelInfo, Datum ybctid, HeapTuple tuple, EState *estate)
{
	int			i;
	int			numIndices;
	RelationPtr relationDescs;
	Relation	heapRelation;
	IndexInfo **indexInfoArray;
	ExprContext *econtext;
	TupleTableSlot	*slot;
	bool		isYBRelation;

	/*
	 * Get information from the result relation info structure.
	 */
	numIndices = resultRelInfo->ri_NumIndices;
	relationDescs = resultRelInfo->ri_IndexRelationDescs;
	indexInfoArray = resultRelInfo->ri_IndexRelationInfo;
	heapRelation = resultRelInfo->ri_RelationDesc;
	isYBRelation = IsYBRelation(heapRelation);

	/*
	 * We will use the EState's per-tuple context for evaluating predicates
	 * and index expressions (creating it if it's not already there).
	 */
	econtext = GetPerTupleExprContext(estate);

	/*
	 * Arrange for econtext's scan tuple to be the tuple under test using
	 * a temporary slot.
	 */
	slot = MakeSingleTupleTableSlot(RelationGetDescr(heapRelation), &TTSOpsHeapTuple);
	slot = ExecStoreHeapTuple(tuple, slot, false);
	econtext->ecxt_scantuple = slot;

	/*
	 * For each index, form the index tuple to delete and delete it from
	 * the index.
	 */
	for (i = 0; i < numIndices; i++)
	{
		Relation	indexRelation = relationDescs[i];
		IndexInfo  *indexInfo;

		/*
		 * No need to update YugaByte primary key which is intrinic part of
		 * the base table.
		 *
		 * TODO(neil) This function is obsolete and removed from Postgres's original code.
		 * - We need to update YugaByte's code path to stop using this function.
		 * - As a result, we don't need distinguish between Postgres and YugaByte here.
		 *   I update this code only for clarity.
		 */
		if (isYBRelation && indexRelation->rd_index->indisprimary)
			continue;

		indexInfo = indexInfoArray[i];
		Assert(indexInfo->ii_ReadyForInserts ==
			   indexRelation->rd_index->indisready);

		/*
		 * If the index is not ready for deletes and index backfill is enabled,
		 * ignore it
		 */
		if (!*YBCGetGFlags()->ysql_disable_index_backfill && !indexRelation->rd_index->indislive)
			continue;
		/*
		 * If the index is marked as read-only and index backfill is disabled,
		 * ignore it
		 */
		if (*YBCGetGFlags()->ysql_disable_index_backfill && !indexInfo->ii_ReadyForInserts)
			continue;

		/* Check for partial index */
		if (indexInfo->ii_Predicate != NIL)
		{
			if (!YbIsPartialIndexPredicateSatisfied(indexInfo, estate))
				continue;
		}

		YbExecDoDeleteIndexTuple(resultRelInfo, indexRelation, indexInfo,
								 slot, ybctid, estate);
	}

	/* Drop the temporary slot */
	ExecDropSingleTupleTableSlot(slot);
}

static void
YbExecDoUpdateIndexTuple(ResultRelInfo *resultRelInfo,
						 Relation indexRelation,
						 IndexInfo *indexInfo,
						 TupleTableSlot *slot,
						 Datum oldYbctid,
						 Datum newYbctid,
						 EState *estate)
{
	Datum		values[INDEX_MAX_KEYS];
	bool		isnull[INDEX_MAX_KEYS];

	/*
	 * Normally, we get here both for primary and secondary indexes so that we
	 * both update the on conflict batching map and do the actual update.  See
	 * analagous YbExecDoInsertIndexTuple and YbExecDoDeleteIndexTuple.  In
	 * this case, we do not update the on conflict batching map since this
	 * update does not change the index's keys.  Then, what's left is doing the
	 * actual update, and that is irrelevant for PK indexes which are baked
	 * into the main table.
	 */
	Assert(IsYBRelation(indexRelation));
	if (indexRelation->rd_index->indisprimary)
		return;

	/*
	* FormIndexDatum fills in its values and isnull parameters with the
	* appropriate values for the column(s) of the index.
	*/
	FormIndexDatum(indexInfo, slot, estate, values, isnull);

	MemoryContext oldContext = MemoryContextSwitchTo(GetPerTupleMemoryContext(estate));
	yb_index_update(indexRelation, /* index relation */
					values,	/* array of index Datums */
					isnull,	/* null flags */
					oldYbctid,	/* old ybctid */
					newYbctid,	/* ybctid */
					resultRelInfo->ri_RelationDesc,	/* heap relation */
					indexInfo);	/* index AM may need this */
	MemoryContextSwitchTo(oldContext);
}

List *
YbExecUpdateIndexTuples(ResultRelInfo *resultRelInfo,
						TupleTableSlot *slot,
						Datum ybctid,
						HeapTuple oldtuple,
						ItemPointer tupleid,
						EState *estate,
						Bitmapset *updatedCols,
						bool is_pk_updated,
						bool is_inplace_update_enabled)
{
	int			i;
	int			numIndices;
	RelationPtr relationDescs;
	IndexInfo **indexInfoArray;
	ExprContext *econtext;
	TupleTableSlot	*deleteSlot;
	List	   *insertIndexes = NIL; /* A list of indexes whose tuples need to be reinserted */
	List	   *deleteIndexes = NIL; /* A list of indexes whose tuples need to be deleted */
	List	   *result = NIL;
	Datum		newYbctid = is_pk_updated ?
							YBCGetYBTupleIdFromSlot(slot) :
							(Datum) NULL;

	/*
	 * Get information from the result relation info structure.
	 */
	numIndices = resultRelInfo->ri_NumIndices;
	relationDescs = resultRelInfo->ri_IndexRelationDescs;
	indexInfoArray = resultRelInfo->ri_IndexRelationInfo;

	Assert(IsYBRelation(resultRelInfo->ri_RelationDesc));

	/*
	 * We will use the EState's per-tuple context for evaluating predicates
	 * and index expressions (creating it if it's not already there).
	 */
	econtext = GetPerTupleExprContext(estate);

	/*
	 * Arrange for econtext's scan tuple to be the tuple under test using
	 * a temporary slot.
	 */
	deleteSlot = ExecStoreHeapTuple(
		oldtuple,
		MakeSingleTupleTableSlot(
			RelationGetDescr(resultRelInfo->ri_RelationDesc),
			&TTSOpsHeapTuple),
		false);

	for (i = 0; i < numIndices; i++)
	{
		Relation	indexRelation = relationDescs[i];
		IndexInfo  *indexInfo;
		const AttrNumber offset =
			YBGetFirstLowInvalidAttributeNumber(resultRelInfo->ri_RelationDesc);

		/*
		 * For an update command check if we need to skip index.
		 * For that purpose, we check if the relid of the index is part of the
		 * skip list.
		 */
		if (indexRelation == NULL ||
			list_member_oid(estate->yb_skip_entities.index_list,
							RelationGetRelid(indexRelation)))
			continue;
		
		Form_pg_index indexData = indexRelation->rd_index;
		/*
		 * Primary key is a part of the base relation in Yugabyte and does not
		 * need to be updated here.
		 */
		if (indexData->indisprimary)
			continue;

		indexInfo = indexInfoArray[i];

		/*
		 * If the index is not yet ready for insert we shouldn't attempt to
		 * add new entries, but should delete the old entry from a live index,
		 * because newer transaction may have seen it ready, and inserted the
		 * record into the index.
		 */
		if (!indexInfo->ii_ReadyForInserts)
		{
			if (indexRelation->rd_index->indislive)
				deleteIndexes = lappend_int(deleteIndexes, i);
			continue;
		}

		/*
		 * Check for partial index -
		 * There are four different update scenarios for an index with a predicate:
		 * 1. Both the old and new tuples satisfy the predicate - In this case, the index tuple
		 *    may either be updated in-place or deleted and reinserted depending on whether the
		 *    key columns are modified.
		 * 2. Neither the old nor the new tuple satisfy the predicate - In this case, the
		 *    update of this index can be skipped altogether.
		 * 3. The old tuple satisfies the predicate but the new tuple does not - In this case,
		 *    the index tuple corresponding to the old tuple just needs to be deleted.
		 * 4. The old tuple does not satisfy the predicate but the new tuple does - In this case,
		 *    a new index tuple corresponding to the new tuple needs to be inserted.
		 */
		if (indexInfo->ii_Predicate != NIL)
		{
			ExprState  *predicate = ExecPrepareQual(indexInfo->ii_Predicate, estate);
			bool		deleteApplicable = false;
			bool		insertApplicable = false;

			/*
			 * If predicate state not set up yet, create it (in the estate's
			 * per-query context)
			 */
			econtext->ecxt_scantuple = deleteSlot;
			deleteApplicable = ExecQual(predicate, econtext);

			econtext->ecxt_scantuple = slot;
			insertApplicable = ExecQual(predicate, econtext);

			if (deleteApplicable != insertApplicable)
			{
				/*
				 * Update is not possible as only one of (deletes, inserts) is
				 * applicable. Bail out of further checks.
				 */
				if (deleteApplicable)
					deleteIndexes = lappend_int(deleteIndexes, i);

				if (insertApplicable)
					insertIndexes = lappend_int(insertIndexes, i);

				continue;
			}

			if (!deleteApplicable)
			{
				/* Neither deletes nor updates applicable. Nothing to be done for this index. */
				continue;
			}

			if (CheckUpdateExprOrPred(updatedCols, indexRelation, Anum_pg_index_indpred, offset))
			{
				deleteIndexes = lappend_int(deleteIndexes, i);
				insertIndexes = lappend_int(insertIndexes, i);
				continue;
			}
		}

		/*
		 * Check if any of the columns associated with the expression index have
		 * been modified. This can be done without evaluating the expression
		 * itself.
		 * Note that an expression index can have other key columns in addition
		 * to the expression(s). That is, an expression index can be defined
		 * like so:
		 * CREATE INDEX ON table (f1(a, b), f2(b, c), d, e) INCLUDE (f, g, h);
		 * Such an index can be updated inplace, only if none of (a, b, c, d, e)
		 * have been modified.
		 */
		if (indexInfo->ii_Expressions != NIL)
		{
			if (CheckUpdateExprOrPred(updatedCols, indexRelation, Anum_pg_index_indexprs, offset))
			{
				deleteIndexes = lappend_int(deleteIndexes, i);
				insertIndexes = lappend_int(insertIndexes, i);
				continue;
			}
		}

		if (!(is_inplace_update_enabled &&
			  indexRelation->rd_indam->ybamcanupdatetupleinplace))
		{
			deleteIndexes = lappend_int(deleteIndexes, i);
			insertIndexes = lappend_int(insertIndexes, i);
			continue;
		}

		/*
		 * In the following scenarios, the index tuple can be modified (updated)
		 * in-place, without the need to delete and reinsert the tuple:
		 * - The index is a covering index (number of key columns < number of columns in the index),
		 *   only the non-key columns need to be updated, and the primary key is not updated.
		 * - The index is a unique index and only the non-key columns of the index need to be
		 *   updated (irrespective of whether the primary key is updated).
		 */
		if ((indexData->indnkeyatts == indexData->indnatts || is_pk_updated) &&
			(!indexData->indisunique))
		{
			deleteIndexes = lappend_int(deleteIndexes, i);
			insertIndexes = lappend_int(insertIndexes, i);
			continue;
		}

		/*
		 * The index operations in this function get enqueued into a buffer. The
		 * buffer is flushed prematurely when there are two operations to the
		 * same row. This leads to additional roundtrips to the storage layer
		 * which can be avoided. A tuple is identified by a sequence of its key
		 * columns. In the case where the key columns are specified in an update
		 * query, but remain unmodified, the update is modeled as a
		 * DELETE + INSERT operation when the optimization to detect unmodified
		 * columns is disabled. In such cases, the DELETE and INSERT operations
		 * conflict with each other since the tuple's key columns remain unchanged.
		 * Consider the following example of a relation with four indexes that
		 * has columns C1, C2, C3 of a tuple modified (updated) by a query:
		 * Index I1 on (C1, C2)
		 * Index I2 on (C4, C5, C3)
		 * Index I3 on (C4, C5) INCLUDES (C1, C2)
		 * Index I4 on (C4) INCLUDES (C5)
		 *
		 * The order of operations should be:
		 * (1) Buffer UPDATE tuple of I3
		 * (2) Buffer DELETE tuple of I1
		 * (3) Buffer DELETE tuple of I2
		 * (-) -- Flush --
		 * (4) Buffer INSERT tuple of I1
		 * (5) Buffer INSERT tuple of I2
		 * (-) -- Flush --
		 * Operations related to I4 are skipped altogether because none of the
		 * columns in I4 are updated.
		 *
		 * To achieve this, we compute the list of all indexes whose key columns
		 * are updated. These need the DELETE + INSERT. For all indexes, first
		 * issue the deletes, followed by the inserts.
		 */

		int j = 0;
		for (; j < indexData->indnkeyatts; j++)
		{
			const AttrNumber bms_idx = indexData->indkey.values[j] - offset;
			if (bms_is_member(bms_idx, updatedCols))
				break;
		}

		if (j < indexRelation->rd_index->indnkeyatts)
		{
			deleteIndexes = lappend_int(deleteIndexes, i);
			insertIndexes = lappend_int(insertIndexes, i);
			continue;
		}

		/*
		 * This tuple updates only non-key columns of the index. This implies
		 * that the tuple will continue to satisfy all uniqueness and exclusion
		 * constraints on the index after the update. The index need not be
		 * rechecked. The ON CONFLICT map need not be updated.
		 */
		econtext->ecxt_scantuple = slot;
		YbExecDoUpdateIndexTuple(resultRelInfo, indexRelation, indexInfo,
								 slot, ybctid, newYbctid, estate);
	}

	ListCell	*lc;
	int			index;

	econtext->ecxt_scantuple = deleteSlot;
	foreach(lc, deleteIndexes)
	{
		index = lfirst_int(lc);
		YbExecDoDeleteIndexTuple(resultRelInfo, relationDescs[index],
								 indexInfoArray[index], deleteSlot, ybctid,
								 estate);
	}

	econtext->ecxt_scantuple = slot;
	foreach(lc, insertIndexes)
	{
		index = lfirst_int(lc);
		if (YbExecDoInsertIndexTuple(resultRelInfo, relationDescs[index],
									 indexInfoArray[index], slot, estate,
									 false /* noDupErr */,
									 NULL /* specConflict */,
									 NIL /* arbiterIndexes */,
									 true /* update */,
									 tupleid))
			result = lappend_oid(result, RelationGetRelid(relationDescs[index]));
	}

	/* Drop the temporary slots */
	ExecDropSingleTupleTableSlot(deleteSlot);

	return result;
}

/* ----------------------------------------------------------------
 *		ExecCheckIndexConstraints
 *
 *		This routine checks if a tuple violates any unique or
 *		exclusion constraints.  Returns true if there is no conflict.
 *		Otherwise returns false, and the TID of the conflicting
 *		tuple is returned in *conflictTid.
 *
 *		If 'arbiterIndexes' is given, only those indexes are checked.
 *		NIL means all indexes.
 *
 *		Note that this doesn't lock the values in any way, so it's
 *		possible that a conflicting tuple is inserted immediately
 *		after this returns.  But this can be used for a pre-check
 *		before insertion.
 * ----------------------------------------------------------------
 */
bool
ExecCheckIndexConstraints(ResultRelInfo *resultRelInfo, TupleTableSlot *slot,
						  EState *estate, ItemPointer conflictTid,
						  List *arbiterIndexes,
						  TupleTableSlot **ybConflictSlot)
{
	int			i;
	int			numIndices;
	RelationPtr relationDescs;
	Relation	heapRelation;
	IndexInfo **indexInfoArray;
	ExprContext *econtext;
	Datum		values[INDEX_MAX_KEYS];
	bool		isnull[INDEX_MAX_KEYS];
	ItemPointerData invalidItemPtr;
	bool		checkedIndex = false;

	ItemPointerSetInvalid(conflictTid);
	ItemPointerSetInvalid(&invalidItemPtr);

	/*
	 * Get information from the result relation info structure.
	 */
	numIndices = resultRelInfo->ri_NumIndices;
	relationDescs = resultRelInfo->ri_IndexRelationDescs;
	indexInfoArray = resultRelInfo->ri_IndexRelationInfo;
	heapRelation = resultRelInfo->ri_RelationDesc;

	/*
	 * We will use the EState's per-tuple context for evaluating predicates
	 * and index expressions (creating it if it's not already there).
	 */
	econtext = GetPerTupleExprContext(estate);

	/* Arrange for econtext's scan tuple to be the tuple under test */
	econtext->ecxt_scantuple = slot;

	/*
	 * For each index, form index tuple and check if it satisfies the
	 * constraint.
	 */
	for (i = 0; i < numIndices; i++)
	{
		Relation	indexRelation = relationDescs[i];
		IndexInfo  *indexInfo = indexInfoArray[i];
		bool		satisfiesConstraint;

		if (!YbShouldCheckUniqueOrExclusionIndex(indexInfo, indexRelation,
												 heapRelation, arbiterIndexes))
			continue;

		checkedIndex = true;

		/* Check for partial index */
		if (indexInfo->ii_Predicate != NIL)
		{
			if (!YbIsPartialIndexPredicateSatisfied(indexInfo, estate))
				continue;
		}

		/*
			* FormIndexDatum fills in its values and isnull parameters with the
			* appropriate values for the column(s) of the index.
			*/
		FormIndexDatum(indexInfo,
						slot,
						estate,
						values,
						isnull);

		satisfiesConstraint =
			check_exclusion_or_unique_constraint(heapRelation, indexRelation,
													indexInfo, &invalidItemPtr,
													values, isnull, estate, false,
													CEOUC_WAIT, true,
													conflictTid,
													ybConflictSlot);
		if (!satisfiesConstraint)
			return false;
	}

	if (arbiterIndexes != NIL && !checkedIndex)
		elog(ERROR, "unexpected failure to find arbiter index");

	return true;
}

/*
 * Check for violation of an exclusion or unique constraint
 *
 * heap: the table containing the new tuple
 * index: the index supporting the constraint
 * indexInfo: info about the index, including the exclusion properties
 * tupleid: heap TID of the new tuple we have just inserted (invalid if we
 *		haven't inserted a new tuple yet)
 * values, isnull: the *index* column values computed for the new tuple
 * estate: an EState we can do evaluation in
 * newIndex: if true, we are trying to build a new index (this affects
 *		only the wording of error messages)
 * waitMode: whether to wait for concurrent inserters/deleters
 * violationOK: if true, don't throw error for violation
 * conflictTid: if not-NULL, the TID of the conflicting tuple is returned here
 *
 * Returns true if OK, false if actual or potential violation
 *
 * 'waitMode' determines what happens if a conflict is detected with a tuple
 * that was inserted or deleted by a transaction that's still running.
 * CEOUC_WAIT means that we wait for the transaction to commit, before
 * throwing an error or returning.  CEOUC_NOWAIT means that we report the
 * violation immediately; so the violation is only potential, and the caller
 * must recheck sometime later.  This behavior is convenient for deferred
 * exclusion checks; we need not bother queuing a deferred event if there is
 * definitely no conflict at insertion time.
 *
 * CEOUC_LIVELOCK_PREVENTING_WAIT is like CEOUC_NOWAIT, but we will sometimes
 * wait anyway, to prevent livelocking if two transactions try inserting at
 * the same time.  This is used with speculative insertions, for INSERT ON
 * CONFLICT statements. (See notes in file header)
 *
 * If violationOK is true, we just report the potential or actual violation to
 * the caller by returning 'false'.  Otherwise we throw a descriptive error
 * message here.  When violationOK is false, a false result is impossible.
 *
 * Note: The indexam is normally responsible for checking unique constraints,
 * so this normally only needs to be used for exclusion constraints.  But this
 * function is also called when doing a "pre-check" for conflicts on a unique
 * constraint, when doing speculative insertion.  Caller may use the returned
 * conflict TID to take further steps.
 */
static bool
check_exclusion_or_unique_constraint(Relation heap, Relation index,
									 IndexInfo *indexInfo,
									 ItemPointer tupleid,
									 Datum *values, bool *isnull,
									 EState *estate, bool newIndex,
									 CEOUC_WAIT_MODE waitMode,
									 bool violationOK,
									 ItemPointer conflictTid,
									 TupleTableSlot **ybConflictSlot)
{
	Oid		   *constr_procs;
	uint16	   *constr_strats;
	Oid		   *index_collations = index->rd_indcollation;
	int			indnkeyatts = IndexRelationGetNumberOfKeyAttributes(index);
	IndexScanDesc index_scan;
	ScanKeyData scankeys[INDEX_MAX_KEYS];
	SnapshotData DirtySnapshot;
	int			i;
	bool		conflict;
	bool		found_self;
	ExprContext *econtext;
	TupleTableSlot *existing_slot;
	TupleTableSlot *save_scantuple;

	if (indexInfo->ii_ExclusionOps)
	{
		constr_procs = indexInfo->ii_ExclusionProcs;
		constr_strats = indexInfo->ii_ExclusionStrats;
	}
	else
	{
		constr_procs = indexInfo->ii_UniqueProcs;
		constr_strats = indexInfo->ii_UniqueStrats;
	}

	/*
	 * If any of the input values are NULL, and the index uses the default
	 * nulls-are-distinct mode, the constraint check is assumed to pass (i.e.,
	 * we assume the operators are strict).  Otherwise, we interpret the
	 * constraint as specifying IS NULL for each column whose input value is
	 * NULL.
	 */
	if (!indexInfo->ii_NullsNotDistinct)
	{
		for (i = 0; i < indnkeyatts; i++)
		{
			if (isnull[i])
				return true;
		}
	}

	/*
	 * Search the tuples that are in the index for any violations, including
	 * tuples that aren't visible yet.
	 */
	InitDirtySnapshot(DirtySnapshot);

	for (i = 0; i < indnkeyatts; i++)
	{
		ScanKeyEntryInitialize(&scankeys[i],
							   isnull[i] ? SK_ISNULL | SK_SEARCHNULL : 0,
							   i + 1,
							   /*
								* YB expects invalid strategy for NULL search.
								* See YbShouldPushdownScanPrimaryKey.
								*/
							   isnull[i] ? InvalidStrategy : constr_strats[i],
							   InvalidOid,
							   index_collations[i],
							   constr_procs[i],
							   values[i]);
	}

	/*
	 * Need a TupleTableSlot to put existing tuples in.
	 *
	 * To use FormIndexDatum, we have to make the econtext's scantuple point
	 * to this slot.  Be sure to save and restore caller's value for
	 * scantuple.
	 */
	existing_slot = table_slot_create(heap, NULL);

	econtext = GetPerTupleExprContext(estate);
	save_scantuple = econtext->ecxt_scantuple;
	econtext->ecxt_scantuple = existing_slot;

	/*
	 * May have to restart scan from this point if a potential conflict is
	 * found.
	 */
retry:
	conflict = false;
	found_self = false;
	index_scan = index_beginscan(heap, index, &DirtySnapshot, indnkeyatts, 0);
	index_rescan(index_scan, scankeys, indnkeyatts, NULL, 0);

	while (index_getnext_slot(index_scan, ForwardScanDirection, existing_slot))
	{
		TransactionId xwait;
		XLTW_Oper	reason_wait;
		Datum		existing_values[INDEX_MAX_KEYS];
		bool		existing_isnull[INDEX_MAX_KEYS];
		char	   *error_new;
		char	   *error_existing;

		/*
		 * Ignore the entry for the tuple we're trying to check.
		 */
		if (ItemPointerIsValid(tupleid) &&
			ItemPointerEquals(tupleid, &existing_slot->tts_tid))
		{
			if (found_self)		/* should not happen */
				elog(ERROR, "found self tuple multiple times in index \"%s\"",
					 RelationGetRelationName(index));
			found_self = true;
			continue;
		}

		/*
		 * Extract the index column values and isnull flags from the existing
		 * tuple.
		 */
		FormIndexDatum(indexInfo, existing_slot, estate,
					   existing_values, existing_isnull);

		/* If lossy indexscan, must recheck the condition */
		if (index_scan->xs_recheck)
		{
			if (!index_recheck_constraint(index,
										  constr_procs,
										  existing_values,
										  existing_isnull,
										  values))
				continue;		/* tuple doesn't actually match, so no
								 * conflict */
		}

		/*
		 * At this point we have either a conflict or a potential conflict.
		 *
		 * If an in-progress transaction is affecting the visibility of this
		 * tuple, we need to wait for it to complete and then recheck (unless
		 * the caller requested not to).  For simplicity we do rechecking by
		 * just restarting the whole scan --- this case probably doesn't
		 * happen often enough to be worth trying harder, and anyway we don't
		 * want to hold any index internal locks while waiting.
		 */
		/*
		 * YugaByte manages transaction at a lower level, so we don't need to execute the following
		 * code block.
		 * TODO(Mikhail) Verify correctness in YugaByte transaction management for on-conflict.
		 */
		if (!IsYBRelation(heap)) {
			xwait = TransactionIdIsValid(DirtySnapshot.xmin) ?
				DirtySnapshot.xmin : DirtySnapshot.xmax;

			if (TransactionIdIsValid(xwait) &&
				(waitMode == CEOUC_WAIT ||
				 (waitMode == CEOUC_LIVELOCK_PREVENTING_WAIT &&
				  DirtySnapshot.speculativeToken &&
				  TransactionIdPrecedes(GetCurrentTransactionId(), xwait))))
			{
				reason_wait = indexInfo->ii_ExclusionOps ?
					XLTW_RecheckExclusionConstr : XLTW_InsertIndex;
				index_endscan(index_scan);
				if (DirtySnapshot.speculativeToken)
					SpeculativeInsertionWait(DirtySnapshot.xmin,
											 DirtySnapshot.speculativeToken);
				else
					XactLockTableWait(xwait, heap,
									  &existing_slot->tts_tid, reason_wait);
				goto retry;
			}
		}

		/*
		 * We have a definite conflict (or a potential one, but the caller
		 * didn't want to wait).  Return it to caller, or report it.
		 */
		if (violationOK)
		{
			conflict = true;
			if (IsYBRelation(heap)) {
				Assert(!*ybConflictSlot);
				*ybConflictSlot = existing_slot;
			}
			if (conflictTid)
				*conflictTid = existing_slot->tts_tid;
			break;
		}

		error_new = BuildIndexValueDescription(index, values, isnull);
		error_existing = BuildIndexValueDescription(index, existing_values,
													existing_isnull);
		if (newIndex)
			ereport(ERROR,
					(errcode(ERRCODE_EXCLUSION_VIOLATION),
					 errmsg("could not create exclusion constraint \"%s\"",
							RelationGetRelationName(index)),
					 error_new && error_existing ?
					 errdetail("Key %s conflicts with key %s.",
							   error_new, error_existing) :
					 errdetail("Key conflicts exist."),
					 errtableconstraint(heap,
										RelationGetRelationName(index))));
		else
			ereport(ERROR,
					(errcode(ERRCODE_EXCLUSION_VIOLATION),
					 errmsg("conflicting key value violates exclusion constraint \"%s\"",
							RelationGetRelationName(index)),
					 error_new && error_existing ?
					 errdetail("Key %s conflicts with existing key %s.",
							   error_new, error_existing) :
					 errdetail("Key conflicts with existing key."),
					 errtableconstraint(heap,
										RelationGetRelationName(index))));
	}

	index_endscan(index_scan);

	/*
	 * Ordinarily, at this point the search should have found the originally
	 * inserted tuple (if any), unless we exited the loop early because of
	 * conflict.  However, it is possible to define exclusion constraints for
	 * which that wouldn't be true --- for instance, if the operator is <>. So
	 * we no longer complain if found_self is still false.
	 */

	econtext->ecxt_scantuple = save_scantuple;
	/*
	 * YB: ordinarily, PG frees existing slot here.  But for YB, we need it for
	 * the DO UPDATE part (PG only needs conflictTid which is not palloc'd).
	 * If ybConflictSlot is filled, we found a conflict and need to extend the
	 * memory lifetime till the DO UPDATE part is finished.  The memory will be
	 * freed after that at the end of ExecInsert.
	 * TODO(jason): this is not necessary for DO NOTHING, so it could be freed
	 * here as a minor optimization in that case.
	 */
	if (!*ybConflictSlot)
		ExecDropSingleTupleTableSlot(existing_slot);
	return !conflict;
}

/*
 * Check for violation of an exclusion constraint
 *
 * This is a dumbed down version of check_exclusion_or_unique_constraint
 * for external callers. They don't need all the special modes.
 */
void
check_exclusion_constraint(Relation heap, Relation index,
						   IndexInfo *indexInfo,
						   ItemPointer tupleid,
						   Datum *values, bool *isnull,
						   EState *estate, bool newIndex)
{
	(void) check_exclusion_or_unique_constraint(heap, index, indexInfo, tupleid,
												values, isnull,
												estate, newIndex,
												CEOUC_WAIT, false, NULL,
												NULL /* ybConflictSlot */);
}

/*
 * Check existing tuple's index values to see if it really matches the
 * exclusion condition against the new_values.  Returns true if conflict.
 */
static bool
index_recheck_constraint(Relation index, Oid *constr_procs,
						 Datum *existing_values, bool *existing_isnull,
						 Datum *new_values)
{
	int			indnkeyatts = IndexRelationGetNumberOfKeyAttributes(index);
	int			i;

	for (i = 0; i < indnkeyatts; i++)
	{
		/* Assume the exclusion operators are strict */
		if (existing_isnull[i])
			return false;

		if (!DatumGetBool(OidFunctionCall2Coll(constr_procs[i],
											   index->rd_indcollation[i],
											   existing_values[i],
											   new_values[i])))
			return false;
	}

	return true;
}

/*
 * Check if ExecInsertIndexTuples() should pass indexUnchanged hint.
 *
 * When the executor performs an UPDATE that requires a new round of index
 * tuples, determine if we should pass 'indexUnchanged' = true hint for one
 * single index.
 */
static bool
index_unchanged_by_update(ResultRelInfo *resultRelInfo, EState *estate,
						  IndexInfo *indexInfo, Relation indexRelation)
{
	Bitmapset  *updatedCols;
	Bitmapset  *extraUpdatedCols;
	Bitmapset  *allUpdatedCols;
	bool		hasexpression = false;
	List	   *idxExprs;

	/*
	 * Check cache first
	 */
	if (indexInfo->ii_CheckedUnchanged)
		return indexInfo->ii_IndexUnchanged;
	indexInfo->ii_CheckedUnchanged = true;

	/*
	 * Check for indexed attribute overlap with updated columns.
	 *
	 * Only do this for key columns.  A change to a non-key column within an
	 * INCLUDE index should not be counted here.  Non-key column values are
	 * opaque payload state to the index AM, a little like an extra table TID.
	 *
	 * Note that row-level BEFORE triggers won't affect our behavior, since
	 * they don't affect the updatedCols bitmaps generally.  It doesn't seem
	 * worth the trouble of checking which attributes were changed directly.
	 */
	updatedCols = ExecGetUpdatedCols(resultRelInfo, estate);
	extraUpdatedCols = ExecGetExtraUpdatedCols(resultRelInfo, estate);
	for (int attr = 0; attr < indexInfo->ii_NumIndexKeyAttrs; attr++)
	{
		int			keycol = indexInfo->ii_IndexAttrNumbers[attr];

		if (keycol <= 0)
		{
			/*
			 * Skip expressions for now, but remember to deal with them later
			 * on
			 */
			hasexpression = true;
			continue;
		}

		if (bms_is_member(keycol - FirstLowInvalidHeapAttributeNumber,
						  updatedCols) ||
			bms_is_member(keycol - FirstLowInvalidHeapAttributeNumber,
						  extraUpdatedCols))
		{
			/* Changed key column -- don't hint for this index */
			indexInfo->ii_IndexUnchanged = false;
			return false;
		}
	}

	/*
	 * When we get this far and index has no expressions, return true so that
	 * index_insert() call will go on to pass 'indexUnchanged' = true hint.
	 *
	 * The _absence_ of an indexed key attribute that overlaps with updated
	 * attributes (in addition to the total absence of indexed expressions)
	 * shows that the index as a whole is logically unchanged by UPDATE.
	 */
	if (!hasexpression)
	{
		indexInfo->ii_IndexUnchanged = true;
		return true;
	}

	/*
	 * Need to pass only one bms to expression_tree_walker helper function.
	 * Avoid allocating memory in common case where there are no extra cols.
	 */
	if (!extraUpdatedCols)
		allUpdatedCols = updatedCols;
	else
		allUpdatedCols = bms_union(updatedCols, extraUpdatedCols);

	/*
	 * We have to work slightly harder in the event of indexed expressions,
	 * but the principle is the same as before: try to find columns (Vars,
	 * actually) that overlap with known-updated columns.
	 *
	 * If we find any matching Vars, don't pass hint for index.  Otherwise
	 * pass hint.
	 */
	idxExprs = RelationGetIndexExpressions(indexRelation);
	hasexpression = index_expression_changed_walker((Node *) idxExprs,
													allUpdatedCols);
	list_free(idxExprs);
	if (extraUpdatedCols)
		bms_free(allUpdatedCols);

	if (hasexpression)
	{
		indexInfo->ii_IndexUnchanged = false;
		return false;
	}

	/*
	 * Deliberately don't consider index predicates.  We should even give the
	 * hint when result rel's "updated tuple" has no corresponding index
	 * tuple, which is possible with a partial index (provided the usual
	 * conditions are met).
	 */
	indexInfo->ii_IndexUnchanged = true;
	return true;
}

/*
 * Indexed expression helper for index_unchanged_by_update().
 *
 * Returns true when Var that appears within allUpdatedCols located.
 */
static bool
index_expression_changed_walker(Node *node, Bitmapset *allUpdatedCols)
{
	if (node == NULL)
		return false;

	if (IsA(node, Var))
	{
		Var		   *var = (Var *) node;

		if (bms_is_member(var->varattno - FirstLowInvalidHeapAttributeNumber,
						  allUpdatedCols))
		{
			/* Var was updated -- indicates that we should not hint */
			return true;
		}

		/* Still haven't found a reason to not pass the hint */
		return false;
	}

	return expression_tree_walker(node, index_expression_changed_walker,
								  (void *) allUpdatedCols);
}

/*
 * Build ri_YbConflictMap for each index.
 */
void
YbBatchFetchConflictingRows(ResultRelInfo *resultRelInfo,
							YbInsertOnConflictBatchState *yb_ioc_state,
							EState *estate,
							List *arbiterIndexes)
{
	int			i;
	int			numIndices;
	RelationPtr relationDescs;
	Relation	heapRelation;
	IndexInfo **indexInfoArray;

	/*
	 * Get information from the result relation info structure.
	 */
	numIndices = resultRelInfo->ri_NumIndices;
	relationDescs = resultRelInfo->ri_IndexRelationDescs;
	indexInfoArray = resultRelInfo->ri_IndexRelationInfo;
	heapRelation = resultRelInfo->ri_RelationDesc;

	for (i = 0; i < numIndices; i++)
	{
		Relation	indexRelation = relationDescs[i];
		IndexInfo  *indexInfo;

		if (indexRelation == NULL)
			continue;

		indexInfo = indexInfoArray[i];

		if (!YbShouldCheckUniqueOrExclusionIndex(indexInfo, indexRelation,
												 heapRelation, arbiterIndexes))
			continue;

		Assert(indexInfo->ii_ReadyForInserts ==
			   indexRelation->rd_index->indisready);

		Assert(resultRelInfo->ri_RelationDesc == heapRelation);
		Assert(resultRelInfo->ri_IndexRelationDescs[i] == indexRelation);
		Assert(resultRelInfo->ri_IndexRelationInfo[i] == indexInfo);
		yb_batch_fetch_conflicting_rows(i, resultRelInfo, yb_ioc_state, estate);
	}
}

/*
 * For each slot in an INSERT ON CONFLICT batch (resultRelInfo->ri_Slots),
 * lookup index entries (corresponding to index idx) that conflict with any of
 * those slots.  This is sent as a single batch read request to the index.
 * Store the conflicting slots into ri_YbConflictMap for future use.
 *
 * Parts copied from check_exclusion_or_unique_constraint.
 */
static void
yb_batch_fetch_conflicting_rows(int idx, ResultRelInfo *resultRelInfo,
								YbInsertOnConflictBatchState *yb_ioc_state,
								EState *estate)
{
	Relation	heap = resultRelInfo->ri_RelationDesc;
	Relation	index = resultRelInfo->ri_IndexRelationDescs[idx];
	IndexInfo  *indexInfo = resultRelInfo->ri_IndexRelationInfo[idx];
	Oid		   *constr_procs;
	uint16	   *constr_strats;
	Oid		   *index_collations = index->rd_indcollation;
	int			indnkeyatts = IndexRelationGetNumberOfKeyAttributes(index);
	IndexScanDesc index_scan;
	ScanKeyData scankeys[INDEX_MAX_KEYS];
	int			i;
	ExprContext *econtext;
	TupleTableSlot *existing_slot;
	TupleTableSlot *save_scantuple;
	TupleTableSlot **slots = yb_ioc_state->slots;
	int			num_slots = yb_ioc_state->num_slots;

	if (indexInfo->ii_ExclusionOps)
	{
		constr_procs = indexInfo->ii_ExclusionProcs;
		constr_strats = indexInfo->ii_ExclusionStrats;
	}
	else
	{
		constr_procs = indexInfo->ii_UniqueProcs;
		constr_strats = indexInfo->ii_UniqueStrats;
	}

	/*
	 *
	 * To use FormIndexDatum, we have to make the econtext's scantuple point
	 * to this slot.  Be sure to save and restore caller's value for
	 * scantuple.
	 */
	econtext = GetPerTupleExprContext(estate);
	save_scantuple = econtext->ecxt_scantuple;

	/*
	 * Get index values for each slot.  Two cases:
	 * - The index has a single key: use SAOP (scalar array op):
	 *   key IN [1, 2, 3]
	 *   For each slot, collect the value in Datum format.
	 * - The index has more than one key: use row array comparison:
	 *   (key1, key2) IN [(1, 2), (3, 4), (5, 6)]
	 *   For each slot, collect the values in tuple (converted to Datum) format.
	 * While it should be possible to use a single-element row array comparison
	 * instead of SAOP to avoid having two cases,
	 * - it hits an error in PgDmlRead::IsAllPrimaryKeysBound for single range
	 *   key indexes (the root cause appearing to be that this was overlooked
	 *   when initially supporting row array comparison because compound BNL
	 *   was never activated for single keys)
	 * - it is likely less performant
	 */
	Datum dvalues[num_slots];
	bool dnulls[num_slots];
	int array_len = 0;
	for (i = 0; i < num_slots; ++i)
	{
		Datum		values[INDEX_MAX_KEYS];
		bool		isnull[INDEX_MAX_KEYS];

		/*
		 * To use FormIndexDatum, we have to make the econtext's scantuple point
		 * to this slot.
		 */
		TupleTableSlot *slot = slots[i];
		if (slot->tts_tableOid != resultRelInfo->ri_RelationDesc->rd_id)
			continue;

		econtext->ecxt_scantuple = slot;

		/* Check for partial index */
		if (indexInfo->ii_Predicate != NIL)
		{
			if (!YbIsPartialIndexPredicateSatisfied(indexInfo, estate))
				continue;
		}

		/*
		 * FormIndexDatum fills in its values and isnull parameters with the
		 * appropriate values for the column(s) of the index.
		 */
		FormIndexDatum(indexInfo,
					   slot,
					   estate,
					   values,
					   isnull);

		Assert(!indexInfo->ii_NullsNotDistinct);

		bool found_null = false;
		for (int j = 0; j < indnkeyatts; j++)
		{
			if (isnull[j])
			{
				found_null = true;
				break;
			}
		}
		if (found_null)
			continue;

		if (indnkeyatts == 1)
		{
			dvalues[array_len] = values[0];
			dnulls[array_len++] = isnull[0];
		}
		else
		{
			Assert(indnkeyatts > 1);

			/* YB: derived from ExecEvalRow */
			HeapTuple	tuple;

			/*
			 * This can happen for expression indexes.  Not sure why.
			 * TODO(jason): maybe this shouldn't be done and we should use a
			 * copy of the tupdesc.
			 */
			if (index->rd_att->tdtypeid == 0)
				index->rd_att->tdtypeid = RECORDOID;

			BlessTupleDesc(index->rd_att);
			tuple = heap_form_tuple(index->rd_att,
									values,
									isnull);

			dvalues[array_len] = HeapTupleGetDatum(tuple);
			dnulls[array_len++] = false;
		}
	}

	/*
	 * Optimization to bail out early in case there is no batch read RPC to
	 * send.  An ON CONFLICT batching map will not be created for this index.
	 */
	if (array_len == 0)
	{
		econtext->ecxt_scantuple = save_scantuple;
		return;
	}

	/*
	 * Create the array used for the RHS of the batch read RPC.
	 * Parts copied from ExecEvalArrayExpr.
	 */
	ArrayType *result;
	int			ndims = 0;
	int			dims[MAXDIM];
	int			lbs[MAXDIM];
	Oid			elmtype;
	int			elmlen;
	bool		elmbyval;
	char		elmalign;

	ndims = 1;
	dims[0] = array_len;
	lbs[0] = 1;
	if (indnkeyatts == 1)
	{
		FormData_pg_attribute att = index->rd_att->attrs[0];
		elmtype = att.atttypid;
		elmlen = att.attlen;
		elmbyval = att.attbyval;
		elmalign = att.attalign;
	}
	else
	{
		Assert(indnkeyatts > 1);

		elmtype = RECORDOID;
		elmlen = -1;
		elmbyval = false;
		elmalign = TYPALIGN_DOUBLE;
	}

	result = construct_md_array(dvalues, dnulls, ndims, dims, lbs,
								elmtype, elmlen, elmbyval, elmalign);

	/* Fill the scan key used for the batch read RPC. */
	ScanKeyData this_scan_key_data;
	ScanKey this_scan_key = &this_scan_key_data;
	if (indnkeyatts == 1)
	{
		ScanKeyEntryInitialize(this_scan_key,
							   SK_SEARCHARRAY,
							   1,
							   constr_strats[0],
							   elmtype,
							   index_collations[0],	/* TODO(jason): check this */
							   constr_procs[0],
							   PointerGetDatum(result));
	}
	else
	{
		Assert(indnkeyatts > 1);

		for (i = 0; i < indnkeyatts; ++i)
		{
			ScanKeyEntryInitialize(&scankeys[i],
								   SK_ROW_MEMBER | SK_SEARCHARRAY,
								   i + 1,
								   constr_strats[i],
								   InvalidOid,
								   index_collations[i],
								   constr_procs[i],
								   0 /* argument */);
		}
		scankeys[0].sk_argument = PointerGetDatum(result);
		scankeys[indnkeyatts - 1].sk_flags |= SK_ROW_END;

		/*
		 * Copied from ExecIndexBuildScanKeys
		 *   else if (IsA(clause, RowCompareExpr))
		 */
		MemSet(this_scan_key, 0, sizeof(ScanKeyData));
		this_scan_key->sk_flags = SK_ROW_HEADER | SK_SEARCHARRAY;
		this_scan_key->sk_attno = scankeys[0].sk_attno;
		this_scan_key->sk_strategy = BTEqualStrategyNumber;
		/* sk_subtype, sk_collation, sk_func not used in a header */
		this_scan_key->sk_argument = PointerGetDatum(scankeys);
		/*
		 * TODO(jason): sk_subtype = RECORDOID should not be necessary and
		 * is currently tied to a hack in yb_scan.c.
		 */
		this_scan_key->sk_subtype = RECORDOID;
	}

	/*
	 * Need a TupleTableSlot to put existing tuples in.
	 *
	 * To use FormIndexDatum, we have to make the econtext's scantuple point
	 * to this slot.
	 */
	existing_slot = table_slot_create(heap, NULL);
	econtext->ecxt_scantuple = existing_slot;

	index_scan = index_beginscan(heap, index, estate->es_snapshot, 1, 0);
	index_rescan(index_scan, this_scan_key, 1, NULL, 0);

	while (index_getnext_slot(index_scan, ForwardScanDirection, existing_slot))
	{
		Datum		existing_values[INDEX_MAX_KEYS];
		bool		existing_isnull[INDEX_MAX_KEYS];
		MemoryContext oldcontext;

		/*
		 * Extract the index column values and isnull flags from the existing
		 * tuple.
		 */
		FormIndexDatum(indexInfo, existing_slot, estate,
					   existing_values, existing_isnull);

		/*
		 * Irrespective of how distinctness of NULLs are treated by the index,
		 * the index keys having NULL values are filtered out above, and will
		 * not be a part of the index scan result.
		 */
		Assert(!YbIsAnyIndexKeyColumnNull(indexInfo, existing_isnull));

		oldcontext = MemoryContextSwitchTo(estate->es_query_cxt);
		YBCPgInsertOnConflictKeyInfo info = {existing_slot};
		YBCPgYBTupleIdDescriptor *descr =
			YBCBuildNonNullUniqueIndexYBTupleId(index, existing_values);
		HandleYBStatus(YBCPgAddInsertOnConflictKey(descr, yb_ioc_state, &info));
		MemoryContextSwitchTo(oldcontext);

		existing_slot = table_slot_create(heap, NULL);
		econtext->ecxt_scantuple = existing_slot;
	}

	index_endscan(index_scan);

	econtext->ecxt_scantuple = save_scantuple;
	ExecDropSingleTupleTableSlot(existing_slot);
}

bool
YbIsAnyIndexKeyColumnNull(IndexInfo *indexInfo, bool isnull[INDEX_MAX_KEYS])
{
	for (int i = 0; i < indexInfo->ii_NumIndexKeyAttrs; i++)
	{
		if (isnull[i])
			return true;
	}

	return false;
}

/*
 * YbShouldCheckUniqueOrExclusionIndex
 *
 * Function to determine if the given index satisfies prerequisites for a
 * unique or exclusion constraint check.
 * Logic has been lifted from ExecCheckIndexConstraints.
 */
bool
YbShouldCheckUniqueOrExclusionIndex(IndexInfo *indexInfo,
									Relation indexRelation,
									Relation heapRelation,
									List *arbiterIndexes)
{
	if (indexRelation == NULL)
		return false;

	Assert(indexInfo->ii_ReadyForInserts ==
		indexRelation->rd_index->indisready);

	if (!indexInfo->ii_Unique && !indexInfo->ii_ExclusionOps)
		return false;

	/* If the index is marked as read-only, ignore it */
	if (!indexInfo->ii_ReadyForInserts)
		return false;

	/* When specific arbiter indexes requested, only examine them */
	if (arbiterIndexes != NIL &&
		!list_member_oid(arbiterIndexes,
						 indexRelation->rd_index->indexrelid))
		return false;

	if (!indexRelation->rd_index->indimmediate)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("ON CONFLICT does not support deferrable unique constraints/exclusion constraints as arbiters"),
				 errtableconstraint(heapRelation,
									RelationGetRelationName(indexRelation))));

	return true;
}
