/*--------------------------------------------------------------------------------------------------
 *
 * yb_osc_trigger.c
 *    Change-capture trigger that replays DML on an online schema change job's
 *    source table onto its target while the backfill runs.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * IDENTIFICATION
 *    src/backend/yb_online_schema_change/yb_osc_trigger.c
 *--------------------------------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "access/htup_details.h"
#include "access/table.h"
#include "access/xact.h"
#include "catalog/catalog.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/pg_trigger.h"
#include "commands/trigger.h"
#include "executor/executor.h"
#include "executor/ybModifyTable.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "parser/parse_node.h"
#include "parser/parse_relation.h"
#include "pg_yb_utils.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/hsearch.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/tuplestore.h"
#include "yb/yql/pggate/ybc_gflags.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb_osc_internal.h"


/*
 * A trigger with transition tables can have only one event, hence one AFTER
 * STATEMENT trigger per event.  Each trigger reads only the transition
 * tuplestores for its own event, so no row is replayed twice.
 *
 * A firing writes tombstones for its old rows and then inserts its new rows,
 * and a tombstone erases any earlier insert of the same target key, so the
 * target matches the source only if firings are replayed in the order their
 * changes were made.  That holds for consecutive statements, and within one
 * ModifyTable node, whose AFTER STATEMENT events fire DELETE, then UPDATE,
 * then INSERT.  It does not hold in general.  The events of the ModifyTable
 * nodes of one statement fire in the order the nodes finish, so a
 * data-modifying WITH can fire an INSERT before a DELETE.  A cascading foreign
 * key action runs on a new snapshot, can delete rows the statement inserted,
 * and its event is queued behind the statement's own.  And a statement run by
 * a trigger or a function within another statement fires its events before the
 * enclosing statement's.  MERGE, which could fire all three events from one
 * node, is rejected by the YB grammar
 * (OnlineSchemaChangeTriggerMergeUnsupported).
 *
 * yb_online_schema_change_trigger detects the orders it cannot replay and
 * raises an error, which rolls the statement back.  All events of a statement
 * fire with the statement's EState, and a statement run within it starts with
 * a CommandCounterIncrement, so it has a larger es_output_cid.  Per target,
 * the trigger remembers the largest es_output_cid it has replayed in the
 * current transaction and the EState of the latest statement that inserted.  A
 * firing is rejected if a statement with a larger es_output_cid has already
 * been replayed into the target, or if it has old rows and an earlier firing
 * of the same statement inserted into the target.
 */
typedef struct YbOscTriggerSpec
{
	const char *suffix;
	int16		event;
	bool		hasOld;
	bool		hasNew;
} YbOscTriggerSpec;

static const YbOscTriggerSpec kOscTriggers[] = {
	{"_ins", TRIGGER_TYPE_INSERT, false, true},
	{"_upd", TRIGGER_TYPE_UPDATE, true, true},
	{"_del", TRIGGER_TYPE_DELETE, true, false},
};

#define NUM_OSC_TRIGGERS (sizeof(kOscTriggers) / sizeof(kOscTriggers[0]))

/*
 * Enter the DDL state that YBTxnDdlProcessUtility enters for CREATE TRIGGER
 * in a transaction block, so that the catalog version is bumped and other
 * backends rebuild the table's trigger list.  Online schema change requires
 * transactional DDL, so the change commits with the enclosing transaction.
 */
static void
YbOscTriggerDdlBegin(void)
{
	if (!(yb_enable_ddl_savepoint_infra &&
		  *YBCGetGFlags()->ysql_yb_enable_ddl_savepoint_support) &&
		YBTransactionContainsNonReadCommittedSavepoint(false /* skip_backward_compat_escape_hatch */ ))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("interleaving SAVEPOINT & DDL in transaction"
						" disallowed without DDL savepoint support"),
				 errhint("Consider enabling ysql_yb_enable_ddl_savepoint_support.")));
	YBAddDdlTxnState(YB_DDL_MODE_VERSION_INCREMENT);
}

static void
YbOscTriggerDdlEnd(void)
{
	CommandCounterIncrement();
	YBMergeDdlTxnState();
}

static TriggerTransition *
MakeTransitionTable(const char *name, bool isNew)
{
	TriggerTransition *tt = makeNode(TriggerTransition);

	tt->name = pstrdup(name);
	tt->isNew = isNew;
	tt->isTable = true;
	return tt;
}

/*
 * Attach the capture triggers to srcRel.  The target and transform OIDs are
 * passed as trigger arguments so that a firing does not have to look up the
 * online schema change job.
 *
 * The triggers are internal, as a foreign key's are: ALTER TABLE ... DISABLE
 * TRIGGER USER skips them, DISABLE TRIGGER ALL needs a superuser, and pg_dump
 * leaves them out, so a dump taken during a job does not restore triggers
 * whose arguments name OIDs of the original database.  PostgreSQL appends
 * each one's OID to its name.  Each also depends internally on its own
 * table, as a foreign key's triggers depend on their constraint, so DROP
 * TRIGGER is refused while dropping the table still drops the trigger.
 */
void
YbOscCreateTrigger(Relation srcRel, const char *triggerName,
				   Oid targetOid, Oid transformOid)
{
	ObjectAddress owner;

	YbOscTriggerDdlBegin();

	ObjectAddressSet(owner, RelationRelationId, RelationGetRelid(srcRel));

	for (size_t i = 0; i < NUM_OSC_TRIGGERS; i++)
	{
		const YbOscTriggerSpec *spec = &kOscTriggers[i];
		CreateTrigStmt *stmt = makeNode(CreateTrigStmt);

		stmt->trigname = psprintf("%s%s", triggerName, spec->suffix);
		stmt->relation = makeRangeVar(get_namespace_name(RelationGetNamespace(srcRel)),
									  pstrdup(RelationGetRelationName(srcRel)),
									  -1);
		stmt->funcname = list_make2(makeString("pg_catalog"),
									makeString("yb_online_schema_change_trigger"));
		stmt->args = list_make2(makeString(psprintf("%u", targetOid)),
								makeString(psprintf("%u", transformOid)));
		stmt->row = false;
		stmt->timing = TRIGGER_TYPE_AFTER;
		stmt->events = spec->event;
		if (spec->hasOld)
			stmt->transitionRels = lappend(stmt->transitionRels,
										   MakeTransitionTable("yb_osc_old_rows", false));
		if (spec->hasNew)
			stmt->transitionRels = lappend(stmt->transitionRels,
										   MakeTransitionTable("yb_osc_new_rows", true));

		ObjectAddress trig = CreateTrigger(stmt, NULL /* queryString */ ,
										   RelationGetRelid(srcRel),
										   InvalidOid, InvalidOid, InvalidOid,
										   F_YB_ONLINE_SCHEMA_CHANGE_TRIGGER,
										   InvalidOid, NULL /* whenClause */ ,
										   true /* isInternal */ ,
										   false /* in_partition */ );

		recordDependencyOn(&trig, &owner, DEPENDENCY_INTERNAL);
	}

	YbOscTriggerDdlEnd();
}

/*
 * Add the capture triggers on relOid to objs, releasing the internal
 * dependency that would refuse their drop.  They are found by function rather
 * than by name, so that a renamed trigger is found too.
 */
static void
YbOscCollectTriggersToDrop(Oid relOid, ObjectAddresses *objs)
{
	Relation	tgrel = table_open(TriggerRelationId, AccessShareLock);
	ScanKeyData key;
	SysScanDesc scan;
	HeapTuple	tuple;

	ScanKeyInit(&key, Anum_pg_trigger_tgrelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(relOid));
	scan = systable_beginscan(tgrel, TriggerRelidNameIndexId, true, NULL, 1,
							  &key);
	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		Form_pg_trigger tg = (Form_pg_trigger) GETSTRUCT(tuple);
		ObjectAddress addr;

		if (tg->tgfoid != F_YB_ONLINE_SCHEMA_CHANGE_TRIGGER)
			continue;
		ObjectAddressSet(addr, TriggerRelationId, tg->oid);
		add_exact_object_address(&addr, objs);
		(void) deleteDependencyRecordsForClass(TriggerRelationId, tg->oid,
											   RelationRelationId,
											   DEPENDENCY_INTERNAL);
	}
	systable_endscan(scan);
	table_close(tgrel, AccessShareLock);
	CommandCounterIncrement();
}

/*
 * Detach the capture triggers from srcRel.  A retry after a partial failure
 * finds nothing left to drop.
 */
void
YbOscDropTrigger(Relation srcRel)
{
	ObjectAddresses *objs = new_object_addresses();

	YbOscTriggerDdlBegin();

	YbOscCollectTriggersToDrop(RelationGetRelid(srcRel), objs);
	performMultipleDeletions(objs, DROP_RESTRICT, PERFORM_DELETION_INTERNAL);
	free_object_addresses(objs);

	YbOscTriggerDdlEnd();
}

/*
 * Per-firing state: executor scaffolding for the target relation and the
 * transform call, shared by the tombstone and insert passes.
 */
typedef struct YbOscReplayState
{
	Relation	target_rel;
	EState	   *estate;
	ResultRelInfo *rri;
	ExprContext *econtext;
	TupleTableSlot *src_slot;	/* reads the transition tuplestores */
	TupleTableSlot *dst_slot;	/* holds one transformed target row */

	/*
	 * The transform's call on the source row, which returns the whole target
	 * row.  A stored generated column of the target is not computed.
	 */
	ExprState  *callstate;

	/* GUC nest level of the forced yb_speculatively_execute_pl_statements */
	int			guc_nest_level;
} YbOscReplayState;

/*
 * Set up the target as a result relation the way COPY FROM does, so that
 * ExecConstraints and the index routines work on it, and prepare the
 * transform's call on the source row in ecxt_scantuple.
 */
static void
YbOscReplayBegin(YbOscReplayState *replay, Relation srcRel,
				 Relation targetRel, Oid transformOid)
{
	YbOscTransform *xf = YbOscCompileTransform(transformOid, srcRel,
											   targetRel);

	replay->target_rel = targetRel;
	replay->estate = CreateExecutorState();

	ParseState *pstate = make_parsestate(NULL);
	ParseNamespaceItem *nsitem =
		addRangeTableEntryForRelation(pstate, targetRel, RowExclusiveLock,
									  NULL, false, false);

	ExecInitRangeTable(replay->estate, list_make1(nsitem->p_rte));
	free_parsestate(pstate);

	replay->rri = makeNode(ResultRelInfo);
	ExecInitResultRelation(replay->estate, replay->rri, 1);
	ExecOpenIndices(replay->rri, false);

	replay->estate->es_output_cid = GetCurrentCommandId(true);

	replay->src_slot = MakeSingleTupleTableSlot(RelationGetDescr(srcRel),
											&TTSOpsMinimalTuple);
	replay->dst_slot = ExecInitExtraTupleSlot(replay->estate,
										  RelationGetDescr(targetRel),
										  &TTSOpsVirtual);

	replay->econtext = GetPerTupleExprContext(replay->estate);
	replay->econtext->ecxt_scantuple = replay->src_slot;

	replay->callstate = ExecInitExpr(xf->call, NULL);

	replay->guc_nest_level = NewGUCNestLevel();
	YbOscForceSpeculativePl();
}

/* Restore the session's yb_speculatively_execute_pl_statements. */
static void
YbOscReplayRestoreGucs(YbOscReplayState *replay)
{
	if (replay->guc_nest_level > 0)
		AtEOXact_GUC(true, replay->guc_nest_level);
	replay->guc_nest_level = 0;
}

static void
YbOscReplayEnd(YbOscReplayState *replay)
{
	YbOscReplayRestoreGucs(replay);
	ExecDropSingleTupleTableSlot(replay->src_slot);
	ExecCloseResultRelations(replay->estate);
	ExecCloseRangeTableRelations(replay->estate);
	/*
	 * dst_slot holds a reference on the target's TupleDesc, and
	 * FreeExecutorState does not release es_tupleTable.
	 */
	ExecResetTupleTable(replay->estate->es_tupleTable, false /* shouldFree */ );
	FreeExecutorState(replay->estate);
}

/*
 * Transform each row of a transition table and insert it into the target, or
 * with blindDelete, write tombstones for it (see YbExecBlindDeleteRow).
 * Returns the number of rows replayed.
 */
static uint64_t
YbOscReplayTransitionTable(YbOscReplayState *replay,
						   Tuplestorestate *tstore, bool blindDelete)
{
	uint64_t	nrows = 0;

	tuplestore_rescan(tstore);
	while (tuplestore_gettupleslot(tstore, true /* forward */ ,
								   false /* copy */ , replay->src_slot))
	{
		CHECK_FOR_INTERRUPTS();

		/*
		 * The previous row's writes were already serialized into the pggate
		 * buffer, so its datums can be freed.
		 */
		ResetPerTupleExprContext(replay->estate);

		/*
		 * ExecInsertIndexTuples and ExecDeleteIndexTuples point the shared
		 * per-tuple ecxt_scantuple at the target slot to evaluate index
		 * expressions and predicates.
		 */
		replay->econtext->ecxt_scantuple = replay->src_slot;

		TupleTableSlot *dstSlot = replay->dst_slot;

		ExecClearTuple(dstSlot);

		MemoryContext oldcxt =
			MemoryContextSwitchTo(replay->econtext->ecxt_per_tuple_memory);
		bool		rowIsNull;
		Datum		row = ExecEvalExpr(replay->callstate, replay->econtext,
									   &rowIsNull);

		if (rowIsNull)
			ereport(ERROR,
					(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
					 errmsg("transform function returned NULL")));

		HeapTupleHeader td = DatumGetHeapTupleHeader(row);
		HeapTupleData tuple;

		tuple.t_len = HeapTupleHeaderGetDatumLength(td);
		ItemPointerSetInvalid(&tuple.t_self);
		tuple.t_tableOid = InvalidOid;
		tuple.t_data = td;
		heap_deform_tuple(&tuple, dstSlot->tts_tupleDescriptor,
						  dstSlot->tts_values, dstSlot->tts_isnull);
		MemoryContextSwitchTo(oldcxt);

		ExecStoreVirtualTuple(dstSlot);
		dstSlot->tts_tableOid = RelationGetRelid(replay->target_rel);

		if (blindDelete)
			YbExecBlindDeleteRow(replay->rri, dstSlot, replay->estate);
		else
			YbExecInsertRow(replay->rri, dstSlot, replay->estate);
		nrows++;
	}

	return nrows;
}

/*
 * Replay order per target table: the largest es_output_cid replayed into it,
 * and the EState of the latest statement that inserted into it.  A statement
 * that YB retries runs again with the same es_output_cid, so the inserting
 * statement is identified by its EState rather than by its command ID.
 */
typedef struct YbOscTargetOrder
{
	Oid			target_oid;		/* hash key */
	CommandId	last_cid;
	EState	   *insert_estate;
} YbOscTargetOrder;

/*
 * Kept in a child of TopTransactionContext, because command IDs restart in
 * every transaction.
 */
static HTAB *targetOrderHash = NULL;

static void
YbOscForgetTargetOrders(void *arg)
{
	targetOrderHash = NULL;
}

/*
 * Reset callback on an inserting statement's es_query_cxt, so that a later
 * EState allocated at the same address is not taken for the same statement.
 */
static void
YbOscForgetInsertEState(void *arg)
{
	HASH_SEQ_STATUS status;
	YbOscTargetOrder *entry;

	if (targetOrderHash == NULL)
		return;

	hash_seq_init(&status, targetOrderHash);
	while ((entry = hash_seq_search(&status)) != NULL)
	{
		if (entry->insert_estate == (EState *) arg)
			entry->insert_estate = NULL;
	}
}

static YbOscTargetOrder *
YbOscGetTargetOrder(Oid targetOid)
{
	if (targetOrderHash == NULL)
	{
		MemoryContext cxt = AllocSetContextCreate(TopTransactionContext,
												  "yb osc target replay order",
												  ALLOCSET_SMALL_SIZES);
		MemoryContextCallback *cb = MemoryContextAlloc(cxt, sizeof(*cb));
		HASHCTL		ctl;

		cb->func = YbOscForgetTargetOrders;
		cb->arg = NULL;
		MemoryContextRegisterResetCallback(cxt, cb);

		ctl.keysize = sizeof(Oid);
		ctl.entrysize = sizeof(YbOscTargetOrder);
		ctl.hcxt = cxt;
		targetOrderHash = hash_create("yb osc target replay order", 16, &ctl,
									  HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);
	}

	bool		found;
	YbOscTargetOrder *entry = hash_search(targetOrderHash, &targetOid,
										  HASH_ENTER, &found);

	if (!found)
	{
		entry->last_cid = FirstCommandId;
		entry->insert_estate = NULL;
	}
	return entry;
}

/*
 * Raise if replaying this firing now could apply its changes over newer ones
 * (see the comment above kOscTriggers).  Returns the entry to update after
 * the replay, or NULL if there is nothing to track.
 */
static YbOscTargetOrder *
YbOscCheckReplayOrder(TriggerData *trigdata, Oid targetOid,
					  bool hasOld, bool hasNew)
{
	EState	   *estate = trigdata->estate;

	if (estate == NULL || !(hasOld || hasNew))
		return NULL;

	YbOscTargetOrder *order = YbOscGetTargetOrder(targetOid);
	const char *srcName = RelationGetRelationName(trigdata->tg_relation);

	if (order->last_cid > estate->es_output_cid)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot modify table \"%s\" this way while an online schema change is copying it",
						srcName),
				 errdetail("A statement run within this statement, for example "
						   "by a trigger or a function, modified the table first, "
						   "and the changes of the enclosing statement cannot be "
						   "copied after it."),
				 errhint("Make the change in a separate statement, or run the "
						 "statement after the schema change completes.")));

	if (hasOld && order->insert_estate == estate)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot modify table \"%s\" this way while an online schema change is copying it",
						srcName),
				 errdetail("The statement deletes or updates rows of the table "
						   "after inserting rows into it, for example through a "
						   "data-modifying WITH query or a cascading foreign key."),
				 errhint("Split the statement into one statement per "
						 "modification, or run it after the schema change "
						 "completes.")));

	return order;
}

/* Record a replayed firing for YbOscCheckReplayOrder. */
static void
YbOscRecordReplay(YbOscTargetOrder *order, EState *estate, bool hasNew)
{
	order->last_cid = Max(order->last_cid, estate->es_output_cid);
	if (hasNew && order->insert_estate != estate)
	{
		MemoryContextCallback *cb = MemoryContextAlloc(estate->es_query_cxt,
													   sizeof(*cb));

		cb->func = YbOscForgetInsertEState;
		cb->arg = estate;
		MemoryContextRegisterResetCallback(estate->es_query_cxt, cb);
		order->insert_estate = estate;
	}
}

/*
 * yb_online_schema_change_trigger
 *
 * AFTER STATEMENT trigger function for the capture triggers.  Arguments are
 * the target table OID and the transform function OID.  Replays the old
 * transition table as tombstones and then the new transition table as
 * inserts.
 */
PG_FUNCTION_INFO_V1(yb_online_schema_change_trigger);

Datum
yb_online_schema_change_trigger(PG_FUNCTION_ARGS)
{
	TriggerData *trigdata = (TriggerData *) fcinfo->context;

	if (!CALLED_AS_TRIGGER(fcinfo))
		ereport(ERROR, (errmsg("yb_online_schema_change_trigger called as non-trigger")));

	if (!TRIGGER_FIRED_FOR_STATEMENT(trigdata->tg_event))
		ereport(ERROR, (errmsg("yb_online_schema_change_trigger must be fired for each statement")));

	if (trigdata->tg_trigger->tgnargs < 2)
		ereport(ERROR, (errmsg("yb_online_schema_change_trigger requires target table and transform function")));

	Oid			targetOid = (Oid) strtoul(trigdata->tg_trigger->tgargs[0], NULL, 10);
	Oid			transformOid = (Oid) strtoul(trigdata->tg_trigger->tgargs[1], NULL, 10);

	Relation	targetRel = table_open(targetOid, RowExclusiveLock);

	if (IsSystemRelation(targetRel))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied: \"%s\" is a system catalog",
						RelationGetRelationName(targetRel))));
	if (targetRel->rd_rel->relkind != RELKIND_RELATION)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("target \"%s\" is not a table",
						RelationGetRelationName(targetRel))));

	bool		hasOld = (trigdata->tg_oldtable != NULL &&
						  tuplestore_tuple_count(trigdata->tg_oldtable) > 0);
	bool		hasNew = (trigdata->tg_newtable != NULL &&
						  tuplestore_tuple_count(trigdata->tg_newtable) > 0);
	YbOscTargetOrder *order = YbOscCheckReplayOrder(trigdata, targetOid,
													hasOld, hasNew);

	YbOscReplayState replay;
	uint64_t	nrows = 0;

	YbOscReplayBegin(&replay, trigdata->tg_relation, targetRel, transformOid);

	/*
	 * Tombstones must come before inserts.  For UPDATE t SET id = id + 1 over
	 * rows {1,2}, the old table is {1,2} and the new table is {2,3}; inserting
	 * first would let the tombstone for key 2 erase the new row 2.
	 */
	if (TRIGGER_FIRED_BY_DELETE(trigdata->tg_event) ||
		TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
	{
		if (trigdata->tg_oldtable == NULL)
			ereport(ERROR, (errmsg("missing old transition table")));

		nrows += YbOscReplayTransitionTable(&replay, trigdata->tg_oldtable,
											true /* blindDelete */ );
	}

	if (TRIGGER_FIRED_BY_INSERT(trigdata->tg_event) ||
		TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
	{
		if (trigdata->tg_newtable == NULL)
			ereport(ERROR, (errmsg("missing new transition table")));

		nrows += YbOscReplayTransitionTable(&replay, trigdata->tg_newtable,
											false /* blindDelete */ );
	}

	/*
	 * Flush so that a DocDB error from the replay, typically a unique violation
	 * from a transform that maps two source rows to one target row, is reported
	 * while this firing is still on the stack rather than at the end of the
	 * user's statement.  The flush only affects error reporting, not
	 * correctness: the buffer keeps writes to the same row in order, and
	 * buffered operations do not reference the EState.  So skip it when
	 * yb_speculatively_execute_pl_statements asks for trigger writes to be
	 * batched with their parent DML; flushing would turn a PL/pgSQL loop of
	 * single-row INSERTs back into one flush per iteration.
	 */
	YbOscReplayRestoreGucs(&replay);
	if (!yb_speculatively_execute_pl_statements)
		YBFlushBufferedOperations(YBCMakeFlushDebugContextCopyBatch(nrows,
																   RelationGetRelationName(targetRel)));

	YbOscReplayEnd(&replay);

	if (order != NULL)
		YbOscRecordReplay(order, trigdata->estate, hasNew);

	table_close(targetRel, RowExclusiveLock);
	return PointerGetDatum(NULL);
}
