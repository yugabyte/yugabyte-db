/*-------------------------------------------------------------------------
 *
 * yb_online_schema_change.h
 *	  C entry points of the online schema change job lifecycle.
 *
 * The SQL functions yb_begin_mirror_table / yb_finish_mirror_table /
 * yb_abort_mirror_table run YbOscBegin / YbOscFinish / YbOscAbort as one DDL
 * transaction each, after checking that the caller may ALTER both tables.
 * ALTER TABLE ... CONCURRENTLY drives the same job through these directly,
 * inside DDL transactions of its own, with options that select the swap
 * cutover.  Each must be called in an open YB DDL transaction.
 *
 * Copyright (c) YugabyteDB, Inc.
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
 * src/include/utils/yb_online_schema_change.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef YB_ONLINE_SCHEMA_CHANGE_H
#define YB_ONLINE_SCHEMA_CHANGE_H

#include "postgres.h"

#include "nodes/pg_list.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

/* Values stored in pg_yb_online_schema_change.status. */
#define YB_OSC_STATUS_ACTIVE			"active"
#define YB_OSC_STATUS_FINISHED			"finished"
#define YB_OSC_STATUS_ABORTED			"aborted"

/* Values stored in pg_yb_online_schema_change.origin. */
#define YB_OSC_ORIGIN_MANUAL			"manual"
#define YB_OSC_ORIGIN_ALTER_TABLE		"alter_table"

/* Values stored in pg_yb_online_schema_change.cutover_mode. */
#define YB_OSC_CUTOVER_MODE_RENAME		"rename"
#define YB_OSC_CUTOVER_MODE_SWAP		"swap"

/*
 * How finish hands the copied data over.
 *
 * RENAME: the target takes the source's name and the source is renamed
 * aside.  SWAP: the source keeps its OID, so every OID-keyed object survives;
 * the job's ALTER TABLE statement is applied to it, the two tables exchange
 * DocDB storage, and the target, a job-owned shadow, is dropped.
 */
typedef enum YbOscCutoverMode
{
	YB_OSC_CUTOVER_RENAME,
	YB_OSC_CUTOVER_SWAP,
} YbOscCutoverMode;

typedef struct YbOscBeginOptions
{
	YbOscCutoverMode cutover_mode;
	const char *origin;			/* YB_OSC_ORIGIN_* */
	const char *query;			/* text of the statement that started the job,
								 * which a swap cutover re-runs; NULL means
								 * the client statement now running */
} YbOscBeginOptions;

/* A row of pg_yb_online_schema_change.  Strings are palloc'd. */
typedef struct YbOscJob
{
	int64		job_id;
	Oid			source_table_oid;
	Oid			target_table_oid;
	Oid			transform_fn_oid;
	char	   *status;			/* YB_OSC_STATUS_* */
	char	   *origin;			/* YB_OSC_ORIGIN_* */
	YbOscCutoverMode cutover_mode;
	char	   *query;			/* statement that started the job, or NULL */
} YbOscJob;

/* Raise an error unless the current transaction has transactional DDL. */
extern void YbOscCheckTransactionalDdl(void);

/*
 * Start an online schema change job src -> dst via transformOid.  Takes
 * ExclusiveLock on every node of both hierarchies until the end of the
 * transaction.  opts == NULL means the manual defaults (rename cutover, origin
 * "manual").  Returns the job id.
 */
extern int64 YbOscBegin(Oid srcOid, Oid dstOid, Oid transformOid,
						const YbOscBeginOptions *opts);

/*
 * The checks YbOscBegin makes before it starts the job, for a caller that
 * wants their errors before it commits what the job will use.
 */
extern void YbOscValidateJob(Oid srcOid, Oid dstOid, Oid transformOid,
							 const YbOscBeginOptions *opts);

/* The row of job jobId. */
extern bool YbOscFetchJob(int64 jobId, YbOscJob *job);

/* The id of an active job whose source or target is in relOids, or 0. */
extern int64 YbOscFindActiveJob(List *relOids);

/*
 * The master's state of an active job's copy.  Returns false when the master
 * cannot be asked, so that a poller can try again.  *errorMessage, if given,
 * receives the failure detail of a failed copy, or NULL.
 */
extern bool YbOscGetCopyState(Oid srcOid, int64 jobId,
							  YbcPgTableBackfillState *state,
							  char **errorMessage);

/*
 * Cut job jobId over, or roll it back.  Both take AccessExclusiveLock on the
 * source hierarchy for the rest of the transaction.  Abort also drops the
 * shadow table and the transform function of an ALTER TABLE ...
 * CONCURRENTLY job.
 */
extern void YbOscFinish(int64 jobId);
extern void YbOscAbort(int64 jobId);

#endif							/* YB_ONLINE_SCHEMA_CHANGE_H */
