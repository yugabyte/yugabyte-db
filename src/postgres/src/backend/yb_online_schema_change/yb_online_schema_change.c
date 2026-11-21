/*--------------------------------------------------------------------------------------------------
 *
 * yb_online_schema_change.c
 *    The online schema change job lifecycle: begin, wait for the copy, cut
 *    over and abort, as SQL functions and as the C entry points that ALTER
 *    TABLE ... CONCURRENTLY drives (utils/yb_online_schema_change.h), plus
 *    the progress the yb_online_schema_change view shows.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * IDENTIFICATION
 *    src/backend/yb_online_schema_change/yb_online_schema_change.c
 *--------------------------------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "access/table.h"
#include "catalog/pg_class.h"
#include "commands/yb_alter_concurrently.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "pg_yb_utils.h"
#include "storage/lmgr.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/regproc.h"
#include "utils/syscache.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb_osc_internal.h"


PG_FUNCTION_INFO_V1(yb_begin_mirror_table);
PG_FUNCTION_INFO_V1(yb_wait_mirror_table);
PG_FUNCTION_INFO_V1(yb_finish_mirror_table);
PG_FUNCTION_INFO_V1(yb_abort_mirror_table);
PG_FUNCTION_INFO_V1(yb_online_schema_change_progress);


typedef struct YbOscStepArgs
{
	Oid			srcOid;
	Oid			dstOid;
	Oid			transformOid;
	int64		jobId;
} YbOscStepArgs;

static const YbOscBeginOptions manual_begin_options = {
	.cutover_mode = YB_OSC_CUTOVER_RENAME,
	.origin = YB_OSC_ORIGIN_MANUAL,
	.query = NULL,
};

/*
 * Online schema change runs only with transactional DDL.  Each step then
 * commits atomically with the transaction that runs it, and the locks it
 * takes are object locks that hold across the cluster.  The check is made in
 * every transaction, because the object locking infrastructure can be turned
 * off with a configuration reload.
 */
void
YbOscCheckTransactionalDdl(void)
{
	if (!YBIsDdlTransactionBlockEnabled())
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("online schema change requires transactional DDL"),
				 errhint("Set ysql_yb_ddl_transaction_block_enabled and "
						 "enable_object_locking_for_table_locks to true.")));
}

/*
 * Run step as the DDL of the enclosing transaction, the way a DDL statement
 * in a transaction block runs: its catalog changes commit with that
 * transaction.
 */
static void
RunAsDdlTransaction(void (*step) (YbOscStepArgs *), YbOscStepArgs *args)
{
	YbOscCheckTransactionalDdl();
	YBAddDdlTxnState(YB_DDL_MODE_NO_ALTERING);
	step(args);
	YBMergeDdlTxnState();
}

/* The check ALTER TABLE makes (ATSimplePermissions). */
static bool
CanAlterTable(Oid relOid)
{
	return pg_class_ownercheck(relOid, GetUserId()) || IsYbDbAdminUser(GetUserId());
}

static void
CheckCanAlterTable(Oid relOid)
{
	if (!CanAlterTable(relOid))
		aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_TABLE, get_rel_name(relOid));
}

/*
 * The SQL-callable lifecycle functions are open to anyone who may ALTER both
 * tables, and run as the caller.  A table dropped while its job ran has no
 * owner left to check, and is skipped so that the job can still be aborted.
 */
static void
CheckLifecycleCaller(Oid srcOid, Oid dstOid)
{
	if (SearchSysCacheExists1(RELOID, ObjectIdGetDatum(srcOid)))
		CheckCanAlterTable(srcOid);
	if (SearchSysCacheExists1(RELOID, ObjectIdGetDatum(dstOid)))
		CheckCanAlterTable(dstOid);
}

/*
 * The cutover renames both tables, and ALTER TABLE ... RENAME also requires
 * CREATE on the schema.  Checked when the job begins rather than when the
 * copy is done.
 */
static void
CheckCanRenameTables(Oid srcOid)
{
	Oid			nspOid = get_rel_namespace(srcOid);

	if (pg_namespace_aclcheck(nspOid, GetUserId(), ACL_CREATE) != ACLCHECK_OK)
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied for schema %s", get_namespace_name(nspOid)),
				 errdetail("The cutover renames the tables, which requires CREATE on their "
						   "schema, as ALTER TABLE ... RENAME does.")));
}

static void
FetchJobOrError(int64 jobId, YbOscJob *job)
{
	if (!YbOscFetchJob(jobId, job))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("online schema change job %lld does not exist", (long long) jobId)));
}

/* Fetch job jobId and check that the caller may run its lifecycle. */
static void
FetchJobForCaller(int64 jobId, YbOscJob *job)
{
	FetchJobOrError(jobId, job);
	CheckLifecycleCaller(job->source_table_oid, job->target_table_oid);
}

static void
ErrorIfEnded(const YbOscJob *job)
{
	if (strcmp(job->status, YB_OSC_STATUS_ACTIVE) != 0)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("online schema change job %lld is %s",
						(long long) job->job_id, job->status)));
}

/*
 * The master's state of job jobId's copy.  Returns false, with the error in
 * *error if that is not NULL, when the master cannot say.
 */
static bool
FetchBackfillStatus(Oid srcOid, int64 jobId, YbcPgTableBackfillStatus *bf,
					YbcStatus *error)
{
	YbcStatus	status;
	HeapTuple	tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(srcOid));

	if (!HeapTupleIsValid(tuple))
	{
		if (error)
			*error = NULL;
		return false;
	}

	/* The master knows the table by relfilenode, as YbGetRelfileNodeId. */
	Oid			relfilenode = ((Form_pg_class) GETSTRUCT(tuple))->relfilenode;

	ReleaseSysCache(tuple);

	status = YBCPgGetTableBackfillStatus(MyDatabaseId,
										 OidIsValid(relfilenode) ? relfilenode : srcOid,
										 jobId, bf);
	if (status == NULL)
		return true;
	if (error)
		*error = status;
	else
		YBCFreeStatus(status);
	return false;
}

/* The master's state of an active job's copy, or an error if it cannot say. */
static void
FetchCopyStatus(const YbOscJob *job, YbcPgTableBackfillStatus *bf)
{
	YbcStatus	error = NULL;

	if (FetchBackfillStatus(job->source_table_oid, job->job_id, bf, &error))
		return;
	if (error)
		HandleYBStatus(error);
	ereport(ERROR,
			(errcode(ERRCODE_UNDEFINED_TABLE),
			 errmsg("source table of online schema change job %lld no longer exists",
					(long long) job->job_id)));
}

bool
YbOscGetCopyState(Oid srcOid, int64 jobId, YbcPgTableBackfillState *state,
				  char **errorMessage)
{
	YbcPgTableBackfillStatus bf;

	if (!FetchBackfillStatus(srcOid, jobId, &bf, NULL))
		return false;
	*state = bf.state;
	if (errorMessage)
		*errorMessage = bf.error_message ? pstrdup(bf.error_message) : NULL;
	return true;
}

/*
 * An ALTER TABLE ... CONCURRENTLY job's own DDL runs with this raised.
 * YBCExecAlterTable marks each alter it issues, and the master leaves such
 * alters out of DDL-transaction verification, which at a swap cutover would
 * look up the dropped shadow.
 */
static bool
IsAlterTableJob(const char *origin)
{
	return strcmp(origin, YB_OSC_ORIGIN_ALTER_TABLE) == 0;
}

/*
 * A copy that the master reports as aborted while the job is still active was
 * aborted by a transaction that then rolled back.  Nothing will move it on,
 * so it counts as failed, and the abort can be retried.
 */
static void
ErrorIfCopyFailed(int64 jobId, const YbcPgTableBackfillStatus *bf)
{
	if (bf->state == YB_TABLE_BACKFILL_FAILED ||
		bf->state == YB_TABLE_BACKFILL_ABORTED)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("the copy of online schema change job %lld failed", (long long) jobId),
				 bf->error_message ? errdetail("%s", bf->error_message) :
				 bf->state == YB_TABLE_BACKFILL_ABORTED ?
				 errdetail("The copy was aborted.") : 0,
				 errhint("Abort the job with yb_abort_mirror_table.")));
}

/* The yb_online_schema_change view's status for an active job's copy. */
static const char *
CopyStatusName(const YbcPgTableBackfillStatus *bf)
{
	switch (bf->state)
	{
		case YB_TABLE_BACKFILL_RUNNING:
			return "backfilling";
		case YB_TABLE_BACKFILL_SUCCEEDED:
			return "ready";
		case YB_TABLE_BACKFILL_FAILED:
		case YB_TABLE_BACKFILL_ABORTED:
			return "failed";
	}
	return "failed";			/* keep compiler quiet */
}

/*
 * Validate the pair, record the job, create the capture trigger on the source
 * and ask the master to start the distributed backfill.  See
 * utils/yb_online_schema_change.h.
 */
static int64
BeginJob(Oid srcOid, Oid dstOid, Oid transformOid, const YbOscBeginOptions *opts)
{
	/*
	 * ExclusiveLock blocks DML on the source until this transaction commits.
	 * Writes committed before that are read by the backfill and writes after
	 * it fire the trigger, so no write falls between the two.  Every node of
	 * a partition hierarchy is locked, root first as PostgreSQL does, because
	 * a statement that names a partition locks only that partition.
	 */
	Relation	srcRel = table_open(srcOid, ExclusiveLock);
	Relation	dstRel = table_open(dstOid, ExclusiveLock);
	List	   *srcNodes = YbOscHierarchyNodes(srcOid, ExclusiveLock);
	List	   *dstNodes = YbOscHierarchyNodes(dstOid, ExclusiveLock);

	/* Before validation, which would reject the target the job is filling. */
	if (YbOscHasActiveJob(srcOid, dstOid))
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("an active online schema change job already exists for the given tables")));

	YbOscValidate(srcRel, dstRel, transformOid, opts);

	/*
	 * Only a lower bound: the local clock can lag the tablet leaders that
	 * assign commit timestamps.  The master reads at the maximum of the
	 * source tablets' safe times, which is at least this value.
	 */
	uint64_t	backfillTimeHt = YBCGetCurrentHybridTimeLsn();

	int64		jobId = YbOscInsertJob(srcOid, dstOid, transformOid, opts);

	char		triggerName[NAMEDATALEN];

	snprintf(triggerName, sizeof(triggerName), "%s%lld",
			 YB_OSC_TRIGGER_NAME_PREFIX, (long long) jobId);
	YbOscCreateTrigger(srcRel, triggerName, dstOid, transformOid);

	/*
	 * The copy runs per source leaf, and the retention pins go on the target
	 * leaves.
	 */
	List	   *srcLeaves = YbOscHierarchyLeaves(srcNodes);
	List	   *dstLeaves = YbOscHierarchyLeaves(dstNodes);
	YbcPgOnlineSchemaChangeTableLists lists = {
		.source_leaf_oids = YbOscRelfileNodeArray(srcLeaves),
		.num_source_leaf_oids = list_length(srcLeaves),
		.target_leaf_oids = YbOscRelfileNodeArray(dstLeaves),
		.num_target_leaf_oids = list_length(dstLeaves),
	};

	HandleYBStatus(YBCPgStartTableBackfill(MyDatabaseId,
										   YbGetRelfileNodeId(srcRel),
										   YbGetRelfileNodeId(dstRel),
										   transformOid,
										   backfillTimeHt, jobId,
										   &lists));

	table_close(dstRel, NoLock);
	table_close(srcRel, NoLock);

	return jobId;
}

void
YbOscValidateJob(Oid srcOid, Oid dstOid, Oid transformOid,
				 const YbOscBeginOptions *opts)
{
	Relation	srcRel = table_open(srcOid, NoLock);
	Relation	dstRel = table_open(dstOid, NoLock);

	YbOscValidate(srcRel, dstRel, transformOid, opts);
	table_close(dstRel, NoLock);
	table_close(srcRel, NoLock);
}

int64
YbOscBegin(Oid srcOid, Oid dstOid, Oid transformOid, const YbOscBeginOptions *opts)
{
	int64		jobId;

	if (opts == NULL)
		opts = &manual_begin_options;
	if (!IsAlterTableJob(opts->origin))
		return BeginJob(srcOid, dstOid, transformOid, opts);

	yb_osc_internal_ddl_depth++;
	PG_TRY();
	{
		jobId = BeginJob(srcOid, dstOid, transformOid, opts);
	}
	PG_FINALLY();
	{
		yb_osc_internal_ddl_depth--;
	}
	PG_END_TRY();
	return jobId;
}

static void
BeginOnlineSchemaChange(YbOscStepArgs *args)
{
	args->jobId = YbOscBegin(args->srcOid, args->dstOid, args->transformOid,
							 NULL /* opts */ );
}

/*
 * yb_begin_mirror_table(source_table, target_table, transform_fn)
 *
 * Validate the pair, record the job, create the capture trigger on the source
 * and ask the master to start the distributed backfill.  Returns the job id,
 * which the other lifecycle functions take.
 */
Datum
yb_begin_mirror_table(PG_FUNCTION_ARGS)
{
	YbOscStepArgs args = {
		.srcOid = PG_GETARG_OID(0),
		.dstOid = PG_GETARG_OID(1),
		.transformOid = PG_GETARG_OID(2),
	};

	if (!OidIsValid(args.srcOid) || !OidIsValid(args.dstOid))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("source and target tables must be valid")));
	if (args.srcOid == args.dstOid)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("source and target tables must be different")));

	CheckLifecycleCaller(args.srcOid, args.dstOid);
	CheckCanRenameTables(args.srcOid);

	RunAsDdlTransaction(BeginOnlineSchemaChange, &args);

	PG_RETURN_INT64(args.jobId);
}

/*
 * yb_wait_mirror_table(job_id)
 *
 * Wait for the job's copy to finish, then add the source's foreign keys to
 * the target and validate them, which leaves the job ready to finish.  The
 * foreign keys are added here rather than at the cutover so that their
 * validation scan does not run under the cutover's AccessExclusiveLock.
 * Running it again is safe: foreign keys already on the target are skipped.
 */
Datum
yb_wait_mirror_table(PG_FUNCTION_ARGS)
{
	int64		jobId = PG_GETARG_INT64(0);
	YbOscJob	job;
	YbcPgTableBackfillStatus bf;

	FetchJobForCaller(jobId, &job);
	ErrorIfEnded(&job);

	for (;;)
	{
		FetchCopyStatus(&job, &bf);
		if (bf.state != YB_TABLE_BACKFILL_RUNNING)
			break;
		CHECK_FOR_INTERRUPTS();
		pg_usleep(100000L);		/* 100ms */
	}
	ErrorIfCopyFailed(jobId, &bf);

	/*
	 * Does not block DML, but conflicts with the AccessExclusiveLock of a
	 * concurrent finish or abort.  The relations are not opened: the ALTER
	 * TABLE that adds the foreign keys fails on a table this session holds
	 * open (CheckTableNotInUse).
	 */
	LockRelationOid(job.source_table_oid, ShareUpdateExclusiveLock);
	LockRelationOid(job.target_table_oid, ShareUpdateExclusiveLock);
	(void) YbOscHierarchyNodes(job.source_table_oid, ShareUpdateExclusiveLock);
	(void) YbOscHierarchyNodes(job.target_table_oid, ShareUpdateExclusiveLock);

	/* A finish or an abort may have committed while this waited. */
	FetchJobOrError(jobId, &job);
	ErrorIfEnded(&job);

	/* A swap keeps the source, and with it the source's foreign keys. */
	if (job.cutover_mode == YB_OSC_CUTOVER_RENAME)
		YbOscPromoteForeignKeys(job.source_table_oid, job.target_table_oid);

	PG_RETURN_VOID();
}

/*
 * Cut over: drop the capture trigger and hand the copied rows over, as the
 * job's cutover_mode says.  rename: rename the source to yb_osc_old_<job_id>
 * and give the target the source's name.  Only the roots are renamed;
 * partitions follow them by OID.  The old source is left for the operator to
 * inspect and drop.  swap: keep the source's OID while YbOscCutover replays
 * the job's ALTER TABLE statement on it, exchanges the DocDB storage of the
 * two tables and drops the target.
 *
 * AccessExclusiveLock on every node of both hierarchies (DML addressed to a
 * leaf locks only that leaf) keeps a write from landing on the source between
 * the trigger drop and the rename or swap, where it would be lost.  The
 * cutover is part of the caller's transaction: if it fails, the job stays
 * active and finish can be run again.
 */
static void
FinishJob(int64 jobId, const YbOscJob *fetched)
{
	Oid			srcOid = fetched->source_table_oid;
	Oid			dstOid = fetched->target_table_oid;

	Relation	srcRel = table_open(srcOid, AccessExclusiveLock);
	Relation	dstRel = table_open(dstOid, AccessExclusiveLock);

	(void) YbOscHierarchyNodes(srcOid, AccessExclusiveLock);
	(void) YbOscHierarchyNodes(dstOid, AccessExclusiveLock);

	/* Again under the locks: a concurrent finish or abort may have ended it. */
	YbOscJob	job;

	FetchJobOrError(jobId, &job);
	ErrorIfEnded(&job);

	YbcPgTableBackfillStatus bf;

	FetchCopyStatus(&job, &bf);
	if (bf.state == YB_TABLE_BACKFILL_RUNNING)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("the copy of online schema change job %lld is still running",
						(long long) jobId),
				 errhint("Wait for it with yb_wait_mirror_table.")));
	ErrorIfCopyFailed(jobId, &bf);

	/*
	 * The key witness may be a secondary unique index, which unlike a YB
	 * primary key can be dropped while the job runs, so check again before the
	 * swap.
	 */
	YbOscGetKeyWitnessAttnums(srcRel, dstRel);

	bool		swap = job.cutover_mode == YB_OSC_CUTOVER_SWAP;

	/* A swap keeps the source, and with it the source's foreign keys. */
	if (!swap)
		YbOscCheckForeignKeysPromoted(srcOid, dstOid);

	YbOscDropTrigger(srcRel);

	if (swap)
	{
		/*
		 * Recorded first, so that the transform the cutover drops is no
		 * longer the transform of an active job.
		 */
		YbOscSetStatus(jobId, YB_OSC_STATUS_FINISHED);
		CommandCounterIncrement();

		/* Closed, keeping the locks; see CheckTableNotInUse. */
		table_close(dstRel, NoLock);
		table_close(srcRel, NoLock);

		/*
		 * The replayed ALTER TABLE runs below ProcessUtility, so the catalog
		 * version bump it needs is recorded here: a yb_finish_mirror_table
		 * call opens its DDL transaction without one.
		 */
		YBAddModificationAspects(YB_DDL_MODE_VERSION_INCREMENT);
		YbOscCutover(srcOid, dstOid, jobId, job.query);
		return;
	}

	char		oldName[NAMEDATALEN];

	snprintf(oldName, sizeof(oldName), "%s%lld",
			 YB_OSC_OLD_TABLE_PREFIX, (long long) jobId);

	const char *srcName = pstrdup(RelationGetRelationName(srcRel));

	/*
	 * Closed, keeping the locks, around the ALTER TABLE that drops the foreign
	 * keys; see CheckTableNotInUse.
	 */
	table_close(dstRel, NoLock);
	table_close(srcRel, NoLock);

	YbOscDropForeignKeys(srcOid);

	srcRel = table_open(srcOid, NoLock);
	dstRel = table_open(dstOid, NoLock);

	YbOscRenameTable(srcRel, oldName);
	YbOscRenameTable(dstRel, srcName);

	YbOscSetStatus(jobId, YB_OSC_STATUS_FINISHED);

	table_close(dstRel, AccessExclusiveLock);
	table_close(srcRel, AccessExclusiveLock);
}

void
YbOscFinish(int64 jobId)
{
	YbOscJob	job;

	FetchJobOrError(jobId, &job);
	if (!IsAlterTableJob(job.origin))
	{
		FinishJob(jobId, &job);
		return;
	}

	yb_osc_internal_ddl_depth++;
	PG_TRY();
	{
		FinishJob(jobId, &job);
	}
	PG_FINALLY();
	{
		yb_osc_internal_ddl_depth--;
	}
	PG_END_TRY();
}

static void
FinishOnlineSchemaChange(YbOscStepArgs *args)
{
	YbOscFinish(args->jobId);
}

/*
 * yb_finish_mirror_table(job_id)
 *
 * YbOscFinish as one DDL transaction: the manual lifecycle's cutover, and the
 * recovery for an ALTER TABLE ... CONCURRENTLY job whose statement died before
 * cutting over.  A live statement holds a session ShareUpdateExclusiveLock on
 * the source, so a finish from another session waits for its cutover and
 * then finds the job finished.
 */
Datum
yb_finish_mirror_table(PG_FUNCTION_ARGS)
{
	YbOscStepArgs args = {.jobId = PG_GETARG_INT64(0)};
	YbOscJob	job;

	YbOscCheckTransactionalDdl();
	FetchJobForCaller(args.jobId, &job);
	args.srcOid = job.source_table_oid;
	args.dstOid = job.target_table_oid;

	RunAsDdlTransaction(FinishOnlineSchemaChange, &args);

	PG_RETURN_VOID();
}

static void
ExecUtility(const char *sql)
{
	if (SPI_connect() != SPI_OK_CONNECT)
		ereport(ERROR, (errmsg("SPI_connect failed")));

	if (SPI_execute(sql, false, 0) != SPI_OK_UTILITY)
		ereport(ERROR, (errmsg("online schema change cleanup DDL failed: %s", sql)));

	SPI_finish();
}

/*
 * Drop what an ALTER TABLE ... CONCURRENTLY job created for itself: the
 * transform function and the shadow target, each unless already gone.
 */
static void
DropAlterTableJobObjects(const YbOscJob *job)
{
	if (SearchSysCacheExists1(PROCOID, ObjectIdGetDatum(job->transform_fn_oid)))
		ExecUtility(psprintf("DROP FUNCTION %s",
							 format_procedure_qualified(job->transform_fn_oid)));

	char	   *dstName = get_rel_name(job->target_table_oid);

	if (dstName != NULL)
	{
		char	   *nspName = get_namespace_name(get_rel_namespace(job->target_table_oid));

		ExecUtility(psprintf("DROP TABLE %s CASCADE",
							 quote_qualified_identifier(nspName, dstName)));
	}
}

/*
 * Stop an active job, whatever its copy is doing: stop the copy, drop the
 * capture trigger and mark the job "aborted".  The source is left as it was.
 * A manual job's target is left for the operator to drop; an ALTER TABLE ...
 * CONCURRENTLY job's target is a shadow it created, and is dropped along with
 * its transform.
 */
static void
AbortJob(int64 jobId, const YbOscJob *fetched)
{
	Oid			srcOid = fetched->source_table_oid;

	/*
	 * A source dropped while the job ran took the capture triggers and the
	 * master's job entry with it, so only the row is left to update.
	 */
	Relation	srcRel = try_table_open(srcOid, AccessExclusiveLock);

	if (srcRel != NULL)
		(void) YbOscHierarchyNodes(srcOid, AccessExclusiveLock);

	/* Again under the lock: a concurrent finish or abort may have ended it. */
	YbOscJob	job;

	FetchJobOrError(jobId, &job);
	ErrorIfEnded(&job);

	if (srcRel != NULL)
	{
		/*
		 * The master is told first.  It durably marks a copy that has not
		 * ended ABORTED, so a copy finishing at that moment cannot end in
		 * success, and a new master leader does not resume it.
		 */
		HandleYBStatus(YBCPgAbortTableBackfill(MyDatabaseId,
											   YbGetRelfileNodeId(srcRel),
											   jobId));
		YbOscDropTrigger(srcRel);
		table_close(srcRel, NoLock);
	}

	YbOscSetStatus(jobId, YB_OSC_STATUS_ABORTED);

	if (IsAlterTableJob(job.origin))
	{
		/* The transform is no longer that of an active job once this is seen. */
		CommandCounterIncrement();
		DropAlterTableJobObjects(&job);
	}
}

void
YbOscAbort(int64 jobId)
{
	YbOscJob	job;

	FetchJobOrError(jobId, &job);
	if (!IsAlterTableJob(job.origin))
	{
		AbortJob(jobId, &job);
		return;
	}

	yb_osc_internal_ddl_depth++;
	PG_TRY();
	{
		AbortJob(jobId, &job);
	}
	PG_FINALLY();
	{
		yb_osc_internal_ddl_depth--;
	}
	PG_END_TRY();
}

static void
AbortOnlineSchemaChange(YbOscStepArgs *args)
{
	YbOscAbort(args->jobId);
}

/*
 * yb_abort_mirror_table(job_id)
 *
 * YbOscAbort as one DDL transaction.
 */
Datum
yb_abort_mirror_table(PG_FUNCTION_ARGS)
{
	YbOscStepArgs args = {.jobId = PG_GETARG_INT64(0)};
	YbOscJob	job;

	YbOscCheckTransactionalDdl();
	FetchJobForCaller(args.jobId, &job);
	args.srcOid = job.source_table_oid;
	args.dstOid = job.target_table_oid;

	RunAsDdlTransaction(AbortOnlineSchemaChange, &args);

	PG_RETURN_VOID();
}

/*
 * yb_online_schema_change_progress(job_id)
 *
 * The state and progress of an active job's copy, as the master reports them;
 * the yb_online_schema_change view shows these.  NULL for any other job, or if
 * the master cannot be reached.  The failure detail can quote the row that
 * failed, so it goes only to a caller who may ALTER the source.
 */
Datum
yb_online_schema_change_progress(PG_FUNCTION_ARGS)
{
	int64		jobId = PG_GETARG_INT64(0);
	YbOscJob	job;
	YbcPgTableBackfillStatus bf;
	TupleDesc	tupdesc;

	if (!YbOscFetchJob(jobId, &job) ||
		strcmp(job.status, YB_OSC_STATUS_ACTIVE) != 0 ||
		!FetchBackfillStatus(job.source_table_oid, jobId, &bf, NULL))
		PG_RETURN_NULL();

	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	Datum		values[5];
	bool		nulls[5] = {false, false, false, false, true};

	values[0] = CStringGetTextDatum(CopyStatusName(&bf));
	values[1] = Int64GetDatum((int64) bf.rows_done);
	values[2] = Int32GetDatum(bf.tablets_done);
	values[3] = Int32GetDatum(bf.tablets_total);
	if (CanAlterTable(job.source_table_oid))
	{
		if (bf.error_message)
		{
			values[4] = CStringGetTextDatum(bf.error_message);
			nulls[4] = false;
		}
		else if (bf.state == YB_TABLE_BACKFILL_ABORTED)
		{
			values[4] = CStringGetTextDatum("The copy was aborted.");
			nulls[4] = false;
		}
	}

	PG_RETURN_DATUM(HeapTupleGetDatum(heap_form_tuple(tupdesc, values, nulls)));
}
