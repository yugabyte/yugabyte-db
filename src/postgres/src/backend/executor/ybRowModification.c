/*-------------------------------------------------------------------------
 *
 * ybRowModification.c
 *	  Command ids of the YB rows a statement modifies, YB's counterpart of
 *	  the cmax of a heap tuple.
 *
 * A heap tuple records in cmax the command that updated or deleted it.  When
 * a command tries to modify a row again, heap_update and heap_delete report
 * TM_SelfModified, and the executor skips the row if the same command
 * modified it, or raises an error if a later command did.  That happens when
 * a join returns a target row more than once (UPDATE ... FROM, DELETE ...
 * USING), when the ModifyTable nodes of writable CTEs target the same row,
 * and when a BEFORE trigger or a volatile function modifies the row that its
 * command is about to modify.  An INSERT ... ON CONFLICT DO UPDATE likewise
 * finds a row that the same command inserted or updated invisible, and fails.
 *
 * DocDB records the command id of each write in its intent, but a write
 * cannot consult it without waiting for the previous writes to the row: they
 * are buffered.  So the backend remembers, for the ModifyTable nodes that may
 * modify a row more than once, the command id that last modified each row,
 * and reports TM_SelfModified from it the way heap_update would.
 *
 * Only a ModifyTable node in progress can modify a row again, so the entries
 * are kept while one is: from its initialization to its last row.  A
 * modification is recorded only when another can follow it within the same
 * statement: when the node's subplan can return a target row more than once,
 * when another ModifyTable node of the statement (a writable CTE) targets the
 * same table, when the node runs inside another node in progress (in a
 * trigger or a function), and for ON CONFLICT DO UPDATE.
 *
 * A row lock likewise skips a row that its command or a later one modified,
 * as heap_lock_tuple reports TM_SelfModified for it.  A locking scan, like a
 * FOR UPDATE cursor, can outlive the command that opened it, so while one is
 * open every modification is recorded and the entries are kept.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *	  src/backend/executor/ybRowModification.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "access/xact.h"
#include "catalog/partition.h"
#include "common/hashfn.h"
#include "executor/tuptable.h"
#include "executor/ybModifyTable.h"
#include "executor/ybRowModification.h"
#include "nodes/plannodes.h"
#include "parser/parsetree.h"
#include "utils/datum.h"
#include "utils/hsearch.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "ybctid.h"

typedef struct YbRowKey
{
	Oid			relid;			/* parent and child rows can share a ybctid */
	Datum		ybctid;
} YbRowKey;

/* A modification of a row by command cmax in subtransaction subid. */
typedef struct YbModificationRecord
{
	CommandId	cmax;
	SubTransactionId subid;
	YbRowModificationKind kind;
} YbModificationRecord;

/*
 * The latest modification of a row is inline.  Earlier ones made in
 * subtransactions that are still open are kept in "older", oldest first, so
 * that a rollback to a savepoint restores the modification before it.
 */
typedef struct YbRowModification
{
	YbRowKey	key;
	YbModificationRecord latest;
	List	   *older;
} YbRowModification;

typedef struct YbSubXactDepth
{
	SubTransactionId subid;
	int			depth;
	int			lock_rows;
} YbSubXactDepth;

static MemoryContext yb_row_modifications_cxt = NULL;
static HTAB *yb_row_modifications = NULL;

/* ModifyTable nodes initialized that have not modified all their rows. */
static int	yb_modify_table_depth = 0;

/* LockRows nodes of YB relations initialized and not yet ended. */
static int	yb_lock_rows_open = 0;

/* yb_modify_table_depth when each open subtransaction started. */
static List *yb_subxact_depths = NIL;

static bool yb_callbacks_registered = false;

static uint32
YbRowKeyHash(const void *key, Size keysize)
{
	const YbRowKey *k = key;

	return hash_combine(murmurhash32(k->relid),
						datum_image_hash(k->ybctid, false, -1));
}

static int
YbRowKeyMatch(const void *key1, const void *key2, Size keysize)
{
	const YbRowKey *k1 = key1;
	const YbRowKey *k2 = key2;

	return !(k1->relid == k2->relid &&
			 datum_image_eq(k1->ybctid, k2->ybctid, false, -1));
}

static void
YbForgetRowModifications(void)
{
	if (yb_row_modifications_cxt)
		MemoryContextDelete(yb_row_modifications_cxt);
	yb_row_modifications_cxt = NULL;
	yb_row_modifications = NULL;
}

static void
YbRowModificationXactCallback(XactEvent event, void *arg)
{
	/* The memory belongs to the transaction, which is ending. */
	yb_row_modifications_cxt = NULL;
	yb_row_modifications = NULL;
	yb_modify_table_depth = 0;
	yb_lock_rows_open = 0;
	yb_subxact_depths = NIL;
}

/*
 * Drops the modifications that the aborted subtransaction and its committed
 * children made: those of subtransactions numbered mySubid or later.
 */
static void
YbAbortSubXactRowModifications(SubTransactionId mySubid)
{
	HASH_SEQ_STATUS status;
	YbRowModification *entry;

	if (!yb_row_modifications)
		return;

	hash_seq_init(&status, yb_row_modifications);
	while ((entry = hash_seq_search(&status)) != NULL)
	{
		if (entry->latest.subid < mySubid)
			continue;

		while (entry->older != NIL)
		{
			YbModificationRecord *record = llast(entry->older);
			bool		restore = record->subid < mySubid;

			if (restore)
				entry->latest = *record;
			entry->older = list_delete_last(entry->older);
			pfree(record);
			if (restore)
				break;
		}

		if (entry->latest.subid >= mySubid)
		{
			Pointer		ybctid = DatumGetPointer(entry->key.ybctid);

			hash_search(yb_row_modifications, &entry->key, HASH_REMOVE, NULL);
			pfree(ybctid);
		}
	}
}

static void
YbRowModificationSubXactCallback(SubXactEvent event,
								 SubTransactionId mySubid,
								 SubTransactionId parentSubid, void *arg)
{
	switch (event)
	{
		case SUBXACT_EVENT_START_SUB:
			{
				MemoryContext oldcxt = MemoryContextSwitchTo(TopTransactionContext);
				YbSubXactDepth *saved = palloc(sizeof(YbSubXactDepth));

				saved->subid = mySubid;
				saved->depth = yb_modify_table_depth;
				saved->lock_rows = yb_lock_rows_open;
				yb_subxact_depths = lappend(yb_subxact_depths, saved);
				MemoryContextSwitchTo(oldcxt);
				break;
			}
		case SUBXACT_EVENT_COMMIT_SUB:
		case SUBXACT_EVENT_ABORT_SUB:
			if (yb_subxact_depths != NIL)
			{
				YbSubXactDepth *saved = llast(yb_subxact_depths);

				if (saved->subid == mySubid)
				{
					/*
					 * The nodes that the error interrupted, and the cursors
					 * that the subtransaction opened, never end: count them
					 * out.
					 */
					if (event == SUBXACT_EVENT_ABORT_SUB)
					{
						yb_modify_table_depth = saved->depth;
						yb_lock_rows_open = saved->lock_rows;
					}
					yb_subxact_depths = list_delete_last(yb_subxact_depths);
					pfree(saved);
				}
			}
			if (event == SUBXACT_EVENT_ABORT_SUB)
				YbAbortSubXactRowModifications(mySubid);
			break;
		case SUBXACT_EVENT_PRE_COMMIT_SUB:
			break;
	}
}

/*
 * Whether the plan scans one of the target relations, given by their range
 * table indexes.
 */
static bool
YbPlanScansTarget(Plan *plan, Bitmapset *targets)
{
	ListCell   *lc;

	if (plan == NULL)
		return false;

	switch (nodeTag(plan))
	{
		case T_Append:
			foreach(lc, ((Append *) plan)->appendplans)
			{
				if (YbPlanScansTarget(lfirst(lc), targets))
					return true;
			}
			return false;
		case T_MergeAppend:
			foreach(lc, ((MergeAppend *) plan)->mergeplans)
			{
				if (YbPlanScansTarget(lfirst(lc), targets))
					return true;
			}
			return false;
		case T_SubqueryScan:
			return YbPlanScansTarget(((SubqueryScan *) plan)->subplan, targets);
		case T_SeqScan:
		case T_YbSeqScan:
		case T_SampleScan:
		case T_IndexScan:
		case T_IndexOnlyScan:
		case T_BitmapHeapScan:
		case T_YbBitmapTableScan:
		case T_TidScan:
		case T_TidRangeScan:
		case T_ForeignScan:
			return bms_is_member(((Scan *) plan)->scanrelid, targets);
		default:
			break;
	}

	return (YbPlanScansTarget(outerPlan(plan), targets) ||
			YbPlanScansTarget(innerPlan(plan), targets));
}

/*
 * Whether the plan can return a row of the target more than once: a join
 * other than a semi or anti join can match it with several rows, unless the
 * target is on its outer side and the planner proved that each outer row
 * matches at most one inner row.
 */
static bool
YbPlanMayRepeatRows(Plan *plan, Bitmapset *targets)
{
	ListCell   *lc;

	if (plan == NULL)
		return false;

	switch (nodeTag(plan))
	{
		case T_NestLoop:
		case T_MergeJoin:
		case T_HashJoin:
		case T_YbBatchedNestLoop:
			{
				Join	   *join = (Join *) plan;

				if (join->jointype != JOIN_SEMI &&
					join->jointype != JOIN_ANTI &&
					(YbPlanScansTarget(innerPlan(plan), targets) ||
					 (!join->inner_unique &&
					  YbPlanScansTarget(outerPlan(plan), targets))))
					return true;
				break;
			}
		case T_Append:
			foreach(lc, ((Append *) plan)->appendplans)
			{
				if (YbPlanMayRepeatRows(lfirst(lc), targets))
					return true;
			}
			break;
		case T_MergeAppend:
			foreach(lc, ((MergeAppend *) plan)->mergeplans)
			{
				if (YbPlanMayRepeatRows(lfirst(lc), targets))
					return true;
			}
			break;
		case T_SubqueryScan:
			return YbPlanMayRepeatRows(((SubqueryScan *) plan)->subplan, targets);
		default:
			break;
	}

	return (YbPlanMayRepeatRows(outerPlan(plan), targets) ||
			YbPlanMayRepeatRows(innerPlan(plan), targets));
}

/*
 * The relation whose rows a target relation of the statement holds: the root
 * of its partition tree, which is what an INSERT into a partitioned table
 * names while its rows go to the partitions.
 */
static Oid
YbTargetTreeRoot(PlannedStmt *pstmt, int rtindex)
{
	Oid			relid = rt_fetch(rtindex, pstmt->rtable)->relid;

	if (get_rel_relispartition(relid))
		relid = llast_oid(get_partition_ancestors(relid));
	return relid;
}

/*
 * Whether another ModifyTable node of the statement targets a relation of
 * the same partition tree as the node does.
 */
static bool
YbSharesTargetWithOtherNode(PlannedStmt *pstmt, ModifyTable *node)
{
	List	   *own = NIL;
	ListCell   *lc;
	bool		result = false;

	foreach(lc, node->resultRelations)
		own = list_append_unique_oid(own,
									 YbTargetTreeRoot(pstmt, lfirst_int(lc)));

	foreach(lc, pstmt->resultRelations)
	{
		if (list_member_int(node->resultRelations, lfirst_int(lc)))
			continue;
		if (list_member_oid(own, YbTargetTreeRoot(pstmt, lfirst_int(lc))))
		{
			result = true;
			break;
		}
	}

	list_free(own);
	return result;
}

static void
YbRegisterRowModificationCallbacks(void)
{
	if (!yb_callbacks_registered)
	{
		RegisterXactCallback(YbRowModificationXactCallback, NULL);
		RegisterSubXactCallback(YbRowModificationSubXactCallback, NULL);
		yb_callbacks_registered = true;
	}
}

/*
 * Whether the statement locks rows, as SELECT ... FOR UPDATE does.  Its
 * LockRows nodes may not be initialized yet when the ModifyTable nodes of its
 * writable CTEs are.
 */
static bool
YbStatementLocksRows(PlannedStmt *pstmt)
{
	ListCell   *lc;

	foreach(lc, pstmt->rowMarks)
	{
		if (RowMarkRequiresRowShareLock(lfirst_node(PlanRowMark, lc)->markType))
			return true;
	}
	return false;
}

/*
 * Called when a ModifyTable node is initialized.
 */
void
YbBeginRowModifications(ModifyTableState *mtstate)
{
	EState	   *estate = mtstate->ps.state;
	ModifyTable *node = (ModifyTable *) mtstate->ps.plan;

	YbRegisterRowModificationCallbacks();

	/* No node in progress or locking scan could modify or lock a row again. */
	if (yb_modify_table_depth == 0 && yb_lock_rows_open == 0)
		YbForgetRowModifications();

	if (yb_modify_table_depth > 0 || yb_lock_rows_open > 0 ||
		node->onConflictAction == ONCONFLICT_UPDATE ||
		YbStatementLocksRows(estate->es_plannedstmt))
		mtstate->yb_track_row_modifications = true;
	else if (estate->es_plannedstmt->hasModifyingCTE &&
			 YbSharesTargetWithOtherNode(estate->es_plannedstmt, node))
		mtstate->yb_track_row_modifications = true;
	else if (node->operation == CMD_UPDATE || node->operation == CMD_DELETE)
	{
		Bitmapset  *targets = NULL;
		ListCell   *lc;

		foreach(lc, node->resultRelations)
			targets = bms_add_member(targets, lfirst_int(lc));
		mtstate->yb_track_row_modifications =
			YbPlanMayRepeatRows(outerPlan(node), targets);
		bms_free(targets);
	}
	else
		mtstate->yb_track_row_modifications = false;
	mtstate->yb_modifying_rows = true;
	++yb_modify_table_depth;
}

/*
 * Called when a ModifyTable node has modified all its rows, and when it ends.
 */
void
YbEndRowModifications(ModifyTableState *mtstate)
{
	if (!mtstate->yb_modifying_rows)
		return;

	mtstate->yb_modifying_rows = false;
	if (yb_modify_table_depth > 0)
		--yb_modify_table_depth;
}

static YbRowModification *
YbFindRowModification(Relation rel, Datum ybctid)
{
	YbRowKey	key = {RelationGetRelid(rel), ybctid};

	if (!yb_row_modifications || !ybctid)
		return NULL;

	return hash_search(yb_row_modifications, &key, HASH_FIND, NULL);
}

/*
 * A node that does not fetch its target rows, like a single-row UPDATE or
 * DELETE, has no ybctid in the plan slot: compute it from the key columns of
 * keySlot, so that the row can be checked and recorded.
 */
void
YbSetRowModificationKey(ModifyTableState *mtstate, Relation rel,
						TupleTableSlot *planSlot, TupleTableSlot *keySlot)
{
	if (!mtstate->yb_skip_fetch_target_tuple ||
		TABLETUPLE_YBCTID(planSlot) != 0 ||
		(!mtstate->yb_track_row_modifications && !yb_row_modifications))
		return;

	TABLETUPLE_YBCTID(planSlot) = YBCComputeYBTupleIdFromSlot(rel, keySlot);
}

/*
 * Records that the node's command modified the row in the slot.
 */
void
YbRecordRowModification(ModifyTableState *mtstate, Relation rel,
						TupleTableSlot *slot, YbRowModificationKind kind)
{
	YbRowKey	key;
	YbRowModification *entry;
	YbModificationRecord record;
	bool		found;
	MemoryContext oldcxt;

	if (!mtstate->yb_track_row_modifications)
		return;

	key.relid = RelationGetRelid(rel);
	key.ybctid = YBCGetYBTupleIdFromSlot(slot);
	if (!key.ybctid)
		return;

	record.cmax = mtstate->ps.state->es_output_cid;
	record.subid = GetCurrentSubTransactionId();
	record.kind = kind;

	if (!yb_row_modifications)
	{
		HASHCTL		ctl;

		yb_row_modifications_cxt =
			AllocSetContextCreate(TopTransactionContext,
								  "YbRowModifications",
								  ALLOCSET_DEFAULT_SIZES);
		ctl.keysize = sizeof(YbRowKey);
		ctl.entrysize = sizeof(YbRowModification);
		ctl.hash = YbRowKeyHash;
		ctl.match = YbRowKeyMatch;
		ctl.hcxt = yb_row_modifications_cxt;
		yb_row_modifications =
			hash_create("YbRowModifications", 256, &ctl,
						HASH_ELEM | HASH_FUNCTION | HASH_COMPARE |
						HASH_CONTEXT);
	}

	oldcxt = MemoryContextSwitchTo(yb_row_modifications_cxt);
	entry = hash_search(yb_row_modifications, &key, HASH_ENTER, &found);
	if (!found)
	{
		entry->key.ybctid = datumCopy(key.ybctid, false, -1);
		entry->older = NIL;
	}
	else if (record.subid > entry->latest.subid)
	{
		YbModificationRecord *older = palloc(sizeof(YbModificationRecord));

		*older = entry->latest;
		entry->older = lappend(entry->older, older);
	}
	else
	{
		/*
		 * The subtransactions numbered above the current one that modified
		 * the row have committed into it, so their modifications are now
		 * its own.
		 */
		while (entry->older != NIL &&
			   ((YbModificationRecord *) llast(entry->older))->subid >=
			   record.subid)
		{
			pfree(llast(entry->older));
			entry->older = list_delete_last(entry->older);
		}
	}
	entry->latest = record;
	MemoryContextSwitchTo(oldcxt);
}

/*
 * What heap_update or heap_delete would report for the row of the plan slot:
 * TM_SelfModified if the executing command or a later one modified it, and
 * TM_Invisible if one of them inserted it, which only a node that does not
 * fetch its target rows can try.
 */
TM_Result
YbGetRowModificationResult(EState *estate, Relation rel,
						   TupleTableSlot *planSlot, TM_FailureData *tmfd)
{
	YbRowModification *entry;

	if (!yb_row_modifications)
		return TM_Ok;

	entry = YbFindRowModification(rel, YBCGetYBTupleIdFromSlot(planSlot));
	if (!entry || entry->latest.cmax < estate->es_output_cid)
		return TM_Ok;

	if (entry->latest.kind == YB_ROW_INSERTED)
		return TM_Invisible;

	ItemPointerSetInvalid(&tmfd->ctid);
	tmfd->xmax = GetTopTransactionIdIfAny();
	tmfd->cmax = entry->latest.cmax;
	tmfd->traversed = false;
	return TM_SelfModified;
}

/*
 * The check that GetTupleForTrigger makes before BEFORE ROW UPDATE or DELETE
 * triggers fire, for a YB row: returns false if the command already modified
 * the row, which the caller then skips, and raises an error if a later
 * command did.
 */
bool
YbCheckTupleForTrigger(EState *estate, Relation rel, HeapTuple trigtuple,
					   TM_Result *tmresult, TM_FailureData *tmfd)
{
	YbRowModification *entry = YbFindRowModification(rel,
													 HEAPTUPLE_YBCTID(trigtuple));

	if (!entry || entry->latest.cmax < estate->es_output_cid ||
		entry->latest.kind == YB_ROW_INSERTED)
		return true;

	if (entry->latest.cmax != estate->es_output_cid)
		ereport(ERROR,
				(errcode(ERRCODE_TRIGGERED_DATA_CHANGE_VIOLATION),
				 errmsg("tuple to be updated was already modified by an operation triggered by the current command"),
				 errhint("Consider using an AFTER trigger instead of a BEFORE trigger to propagate changes to other rows.")));

	if (tmresult)
		*tmresult = TM_SelfModified;
	if (tmfd)
	{
		ItemPointerSetInvalid(&tmfd->ctid);
		tmfd->xmax = GetTopTransactionIdIfAny();
		tmfd->cmax = entry->latest.cmax;
		tmfd->traversed = false;
	}
	return false;
}

/*
 * Whether the executing command or a later one deleted the row in the slot.
 */
bool
YbIsRowDeletedByCommand(EState *estate, Relation rel, TupleTableSlot *slot)
{
	YbRowModification *entry;
	HeapTuple	tuple;
	bool		should_free;

	if (!yb_row_modifications)
		return false;

	tuple = ExecFetchSlotHeapTuple(slot, false, &should_free);
	entry = YbFindRowModification(rel, HEAPTUPLE_YBCTID(tuple));
	if (should_free)
		heap_freetuple(tuple);

	return (entry && entry->latest.kind == YB_ROW_DELETED &&
			entry->latest.cmax >= estate->es_output_cid);
}

/*
 * ON CONFLICT DO UPDATE fails on a conflicting row whose current version the
 * executing command or a later one wrote, which table_tuple_lock finds
 * invisible.  A row that such a command deleted is not checked: the dirty
 * snapshot of the arbiter check would not have found it, but a batched read
 * of the conflicting rows can have.
 */
void
YbCheckOnConflictRowNotModified(EState *estate, Relation rel, Datum ybctid)
{
	YbRowModification *entry = YbFindRowModification(rel, ybctid);

	if (entry && entry->latest.kind != YB_ROW_DELETED &&
		entry->latest.cmax >= estate->es_output_cid)
		ereport(ERROR,
				(errcode(ERRCODE_CARDINALITY_VIOLATION),
		/* translator: %s is a SQL command name */
				 errmsg("%s command cannot affect row a second time",
						"ON CONFLICT DO UPDATE"),
				 errhint("Ensure that no rows proposed for insertion within the same command have duplicate constrained values.")));
}

/*
 * Called when a LockRows node of YB relations is initialized and when it
 * ends.
 */
void
YbBeginLockRows(void)
{
	YbRegisterRowModificationCallbacks();
	++yb_lock_rows_open;
}

void
YbEndLockRows(void)
{
	if (yb_lock_rows_open > 0)
		--yb_lock_rows_open;
}

/*
 * Whether the executing command or a later one updated or deleted the row
 * that a row lock is about to lock, which heap_lock_tuple reports as
 * TM_SelfModified, so that the row is skipped.
 */
bool
YbIsLockedRowModified(EState *estate, Relation rel, Datum ybctid)
{
	YbRowModification *entry = YbFindRowModification(rel, ybctid);

	return (entry && entry->latest.kind != YB_ROW_INSERTED &&
			entry->latest.cmax >= estate->es_output_cid);
}
