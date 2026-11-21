/*--------------------------------------------------------------------------------------------------
 *
 * yb_osc_catalog.c
 *    Reads and writes of the pg_yb_online_schema_change system catalog.
 *
 *    Only begin, finish and abort write the catalog, each as the DDL of the
 *    caller's transaction (RunAsDdlTransaction), so the writes go through the
 *    catalog API as any DDL's do.  The catalog has no syscache, so every scan
 *    takes a fresh catalog snapshot and sees what other sessions committed.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * IDENTIFICATION
 *    src/backend/yb_online_schema_change/yb_osc_catalog.c
 *--------------------------------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "access/htup_details.h"
#include "access/stratnum.h"
#include "access/table.h"
#include "access/xact.h"
#include "catalog/indexing.h"
#include "catalog/partition.h"
#include "catalog/pg_yb_online_schema_change.h"
#include "miscadmin.h"
#include "pg_yb_utils.h"
#include "tcop/pquery.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/queryjumble.h"
#include "utils/regproc.h"
#include "utils/syscache.h"
#include "utils/timestamp.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb_osc_internal.h"


/*
 * Mixing in the pid makes collisions between backends unlikely, and lastJobId
 * keeps the ids of one backend strictly increasing.  The primary key on
 * job_id rejects any collision that remains.
 */
static int64
GenerateJobId(void)
{
	uint64_t	ht = YBCGetCurrentHybridTimeLsn();
	uint64_t	mixed = ht ^ (((uint64_t) MyProcPid) << 1);
	int64		jobId = (int64) (mixed & INT64_MAX);

	if (jobId == 0)
		jobId = 1;

	static int64 lastJobId = 0;

	if (jobId <= lastJobId)
	{
		if (lastJobId == INT64_MAX)
			ereport(ERROR, (errmsg("online schema change job id overflow")));
		jobId = lastJobId + 1;
	}
	lastJobId = jobId;
	return jobId;
}

static char *
JobText(HeapTuple tuple, TupleDesc tupdesc, AttrNumber attnum)
{
	bool		isnull;
	Datum		value = heap_getattr(tuple, attnum, tupdesc, &isnull);

	return isnull ? NULL : TextDatumGetCString(value);
}

static char *
JobStatus(HeapTuple tuple, TupleDesc tupdesc)
{
	char	   *status = JobText(tuple, tupdesc, Anum_pg_yb_online_schema_change_status);

	Assert(status != NULL);
	return status;
}

const char *
YbOscCutoverModeName(YbOscCutoverMode mode)
{
	return mode == YB_OSC_CUTOVER_SWAP ? YB_OSC_CUTOVER_MODE_SWAP :
		YB_OSC_CUTOVER_MODE_RENAME;
}

YbOscCutoverMode
YbOscParseCutoverMode(const char *name)
{
	if (strcmp(name, YB_OSC_CUTOVER_MODE_SWAP) == 0)
		return YB_OSC_CUTOVER_SWAP;
	if (strcmp(name, YB_OSC_CUTOVER_MODE_RENAME) == 0)
		return YB_OSC_CUTOVER_RENAME;
	elog(ERROR, "unknown online schema change cutover mode \"%s\"", name);
	return YB_OSC_CUTOVER_RENAME;	/* keep compiler quiet */
}

static void
FillJob(HeapTuple tuple, TupleDesc tupdesc, YbOscJob *job)
{
	Form_pg_yb_online_schema_change form =
		(Form_pg_yb_online_schema_change) GETSTRUCT(tuple);

	job->job_id = form->job_id;
	job->source_table_oid = form->source_table_oid;
	job->target_table_oid = form->target_table_oid;
	job->transform_fn_oid = form->transform_fn_oid;
	job->status = JobStatus(tuple, tupdesc);
	job->origin = JobText(tuple, tupdesc, Anum_pg_yb_online_schema_change_origin);
	char	   *cutoverMode = JobText(tuple, tupdesc,
										  Anum_pg_yb_online_schema_change_cutover_mode);

	job->cutover_mode = YbOscParseCutoverMode(cutoverMode);
	job->query = JobText(tuple, tupdesc, Anum_pg_yb_online_schema_change_query);
}

/*
 * Text of the client statement now running, the way pg_stat_activity.query
 * reports it but cut to the one statement of a multi-statement string, or NULL
 * if there is none.
 */
static char *
CurrentStatementText(void)
{
	PlannedStmt *pstmt;
	int			location;
	int			len;
	const char *text;

	if (ActivePortal == NULL || ActivePortal->sourceText == NULL ||
		ActivePortal->stmts == NIL)
		return NULL;
	pstmt = linitial_node(PlannedStmt, ActivePortal->stmts);
	location = pstmt->stmt_location;
	len = pstmt->stmt_len;
	text = CleanQuerytext(ActivePortal->sourceText, &location, &len);
	return pnstrdup(text, len);
}

/*
 * Insert a new "active" job and return its job_id.
 */
int64
YbOscInsertJob(Oid srcOid, Oid dstOid, Oid transformOid,
			   const YbOscBeginOptions *opts)
{
	Relation	catalog = table_open(YbOnlineSchemaChangeRelationId, RowExclusiveLock);
	Datum		values[Natts_pg_yb_online_schema_change];
	bool		nulls[Natts_pg_yb_online_schema_change] = {0};
	TimestampTz now = GetCurrentTransactionStartTimestamp();
	int64		jobId = GenerateJobId();

	values[Anum_pg_yb_online_schema_change_job_id - 1] = Int64GetDatum(jobId);
	values[Anum_pg_yb_online_schema_change_source_table_oid - 1] = ObjectIdGetDatum(srcOid);
	values[Anum_pg_yb_online_schema_change_target_table_oid - 1] = ObjectIdGetDatum(dstOid);
	values[Anum_pg_yb_online_schema_change_transform_fn_oid - 1] =
		ObjectIdGetDatum(transformOid);
	values[Anum_pg_yb_online_schema_change_created_at - 1] = TimestampTzGetDatum(now);
	values[Anum_pg_yb_online_schema_change_updated_at - 1] = TimestampTzGetDatum(now);
	values[Anum_pg_yb_online_schema_change_status - 1] =
		CStringGetTextDatum(YB_OSC_STATUS_ACTIVE);
	values[Anum_pg_yb_online_schema_change_origin - 1] =
		CStringGetTextDatum(opts->origin);
	values[Anum_pg_yb_online_schema_change_cutover_mode - 1] =
		CStringGetTextDatum(YbOscCutoverModeName(opts->cutover_mode));
	const char *query = opts->query ? opts->query : CurrentStatementText();

	if (query)
		values[Anum_pg_yb_online_schema_change_query - 1] = CStringGetTextDatum(query);
	else
		nulls[Anum_pg_yb_online_schema_change_query - 1] = true;

	HeapTuple	tuple = heap_form_tuple(RelationGetDescr(catalog), values, nulls);

	CatalogTupleInsert(catalog, tuple);
	heap_freetuple(tuple);
	table_close(catalog, RowExclusiveLock);
	return jobId;
}

/*
 * Set job jobId's status.
 */
void
YbOscSetStatus(int64 jobId, const char *status)
{
	Relation	catalog = table_open(YbOnlineSchemaChangeRelationId, RowExclusiveLock);
	ScanKeyData key;

	ScanKeyInit(&key, Anum_pg_yb_online_schema_change_job_id,
				BTEqualStrategyNumber, F_INT8EQ, Int64GetDatum(jobId));

	SysScanDesc scan = systable_beginscan(catalog, YbOnlineSchemaChangeJobIdIndexId,
										  true /* indexOK */ , NULL /* snapshot */ ,
										  1 /* nkeys */ , &key);
	HeapTuple	tuple = systable_getnext(scan);

	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "online schema change job %lld not found", (long long) jobId);

	Datum		values[Natts_pg_yb_online_schema_change] = {0};
	bool		nulls[Natts_pg_yb_online_schema_change] = {0};
	bool		replaces[Natts_pg_yb_online_schema_change] = {0};

	values[Anum_pg_yb_online_schema_change_status - 1] = CStringGetTextDatum(status);
	replaces[Anum_pg_yb_online_schema_change_status - 1] = true;
	values[Anum_pg_yb_online_schema_change_updated_at - 1] =
		TimestampTzGetDatum(GetCurrentTransactionStartTimestamp());
	replaces[Anum_pg_yb_online_schema_change_updated_at - 1] = true;

	HeapTuple	newTuple = heap_modify_tuple(tuple, RelationGetDescr(catalog),
											 values, nulls, replaces);

	CatalogTupleUpdate(catalog, &tuple->t_self, newTuple);
	heap_freetuple(newTuple);

	systable_endscan(scan);
	table_close(catalog, RowExclusiveLock);
}

/*
 * Fetch job jobId, or return false if there is no such job.  The strings are
 * allocated in the caller's memory context.
 */
bool
YbOscFetchJob(int64 jobId, YbOscJob *job)
{
	Relation	catalog = table_open(YbOnlineSchemaChangeRelationId, AccessShareLock);
	ScanKeyData key;
	bool		found = false;

	ScanKeyInit(&key, Anum_pg_yb_online_schema_change_job_id,
				BTEqualStrategyNumber, F_INT8EQ, Int64GetDatum(jobId));

	SysScanDesc scan = systable_beginscan(catalog, YbOnlineSchemaChangeJobIdIndexId,
										  true /* indexOK */ , NULL /* snapshot */ ,
										  1 /* nkeys */ , &key);
	HeapTuple	tuple = systable_getnext(scan);

	if (HeapTupleIsValid(tuple))
	{
		FillJob(tuple, RelationGetDescr(catalog), job);
		found = true;
	}

	systable_endscan(scan);
	table_close(catalog, AccessShareLock);
	return found;
}

/*
 * Returns the id of an active job whose source or target is one of relOids,
 * or 0.  A full scan: the catalog holds one row per job.
 */
int64
YbOscFindActiveJob(List *relOids)
{
	Relation	catalog = table_open(YbOnlineSchemaChangeRelationId, AccessShareLock);
	SysScanDesc scan = systable_beginscan(catalog, InvalidOid, false /* indexOK */ ,
										  NULL /* snapshot */ , 0 /* nkeys */ , NULL);
	HeapTuple	tuple;
	int64		jobId = 0;

	while (jobId == 0 && HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		Form_pg_yb_online_schema_change job =
			(Form_pg_yb_online_schema_change) GETSTRUCT(tuple);

		if (!list_member_oid(relOids, job->source_table_oid) &&
			!list_member_oid(relOids, job->target_table_oid))
			continue;
		if (strcmp(JobStatus(tuple, RelationGetDescr(catalog)),
				   YB_OSC_STATUS_ACTIVE) == 0)
			jobId = job->job_id;
	}

	systable_endscan(scan);
	table_close(catalog, AccessShareLock);
	return jobId;
}

/*
 * Return true if an active online schema change job is anchored on any
 * relation of either hierarchy or on an ancestor of either root.  Two
 * hierarchies overlap exactly when one root is an ancestor-or-self of the
 * other, which this covers.
 */
bool
YbOscHasActiveJob(Oid srcOid, Oid dstOid)
{
	List	   *oids = list_concat_copy(YbOscHierarchyNodes(srcOid, NoLock),
										YbOscHierarchyNodes(dstOid, NoLock));

	oids = list_concat(oids, get_partition_ancestors(srcOid));
	oids = list_concat(oids, get_partition_ancestors(dstOid));

	return YbOscFindActiveJob(oids) != 0;
}

/*
 * Refuse a partition-topology change under a relation that is the source or
 * target of an active online schema change job, or a descendant of one.
 *
 * A leaf created mid-job would have no capture trigger and no copy -- silent
 * data loss at cutover -- and ATTACH and DETACH change the hierarchy the job
 * spans the same way.
 */
void
YbOscCheckTopologyChange(Oid parentOid, const char *ddl)
{
	if (!IsYugaByteEnabled())
		return;

	/*
	 * Every partition DDL in the database passes through here, including on a
	 * cluster whose YSQL upgrade has not yet created
	 * pg_yb_online_schema_change.
	 */
	if (!SearchSysCacheExists1(RELOID, ObjectIdGetDatum(YbOnlineSchemaChangeRelationId)))
		return;

	List	   *oids = lappend_oid(get_partition_ancestors(parentOid), parentOid);
	int64		jobId = YbOscFindActiveJob(oids);

	if (jobId != 0)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot %s \"%s\" while online schema change job %lld is active",
						ddl, get_rel_name(parentOid), (long long) jobId),
				 errhint("Finish or abort the job, then retry.")));
}

/*
 * Returns the id of an active job whose transform is fnOid, or 0.  A full scan:
 * the catalog has no index on the transform, and holds one row per job.
 */
static int64
FindActiveJobByTransform(Oid fnOid)
{
	Relation	catalog = table_open(YbOnlineSchemaChangeRelationId, AccessShareLock);
	ScanKeyData key;
	int64		jobId = 0;

	ScanKeyInit(&key, Anum_pg_yb_online_schema_change_transform_fn_oid,
				BTEqualStrategyNumber, F_OIDEQ, ObjectIdGetDatum(fnOid));

	SysScanDesc scan = systable_beginscan(catalog, InvalidOid, false, NULL, 1, &key);
	HeapTuple	tuple;

	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		if (strcmp(JobStatus(tuple, RelationGetDescr(catalog)),
				   YB_OSC_STATUS_ACTIVE) == 0)
		{
			jobId = ((Form_pg_yb_online_schema_change) GETSTRUCT(tuple))->job_id;
			break;
		}
	}

	systable_endscan(scan);
	table_close(catalog, AccessShareLock);
	return jobId;
}

/*
 * Refuse a DDL that would change or remove function fnOid while it is the
 * transform of an active online schema change job.
 *
 * Begin checks the transform's body once (YbOscValidate), and the copy and the
 * change-capture trigger rely on that check instead of checking each row, so
 * the function must stay as it was until the job ends.
 *
 * A DDL that commits before begin does is seen by begin's check.  One that
 * runs concurrently with begin conflicts with it on the catalog version, which
 * both increment, so one of them fails.
 */
void
YbOscCheckTransformChange(Oid fnOid, const char *ddl)
{
	if (!IsYugaByteEnabled() || YBCIsInitDbModeEnvVarSet())
		return;

	/*
	 * Every function DDL passes through here, including on a cluster whose
	 * YSQL upgrade has not yet created pg_yb_online_schema_change.
	 */
	if (!SearchSysCacheExists1(RELOID, ObjectIdGetDatum(YbOnlineSchemaChangeRelationId)))
		return;

	int64		jobId = FindActiveJobByTransform(fnOid);

	if (jobId != 0)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot %s function %s while it is the transform of online schema change job %lld",
						ddl, format_procedure(fnOid), (long long) jobId),
				 errdetail("The job checked the function's definition when it began and "
						   "relies on it until it ends."),
				 errhint("Finish or abort the job, then retry.")));
}
