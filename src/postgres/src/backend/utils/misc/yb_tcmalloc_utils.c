/*-------------------------------------------------------------------------
 *
 * pg_yb_utils.c
 *	  Utilities for YugaByte/PostgreSQL integration that have to be defined on
 *	  the PostgreSQL side.
 *
 * Copyright (c) YugaByte, Inc.
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
 *	  src/backend/utils/misc/pg_yb_utils.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include "catalog/pg_authid.h"
#include "fmgr.h"
#include "funcapi.h"
#include "nodes/execnodes.h"
#include "pg_yb_utils.h"
#include "storage/ipc.h"
#include "storage/procarray.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/tuplestore.h"
#include "yb/yql/pggate/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb_tcmalloc_utils.h"

int			yb_log_heap_snapshot_on_exit_threshold = -1;

static void YbLogHeapSnapshotProcExit(int status, Datum arg);

static void
			YbPutHeapSnapshotTupleStore(Tuplestorestate *tupstore, TupleDesc tupdesc,
										YbcHeapSnapshotSample *sample);

Datum
yb_set_tcmalloc_sample_period(PG_FUNCTION_ARGS)
{
	int64_t		sample_period_ms = PG_GETARG_INT64(0);

	YBCSetTCMallocSamplingPeriod(sample_period_ms);
	PG_RETURN_VOID();
}

void
yb_backend_heap_snapshot_internal(bool peak_heap, FunctionCallInfo fcinfo)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;

	/*
	 * Only superusers or the YbDbAdmin user are allowed to see the heap
	 * snapshot
	 */
	if (!IsYbDbAdminUser(GetUserId()))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("only superuser or the yb_db_admin user may access the heap snapshot")));

	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that "
						"cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not "
						"allowed in this context")));

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	YbcHeapSnapshotSample *snapshot = NULL;
	int64_t		samples = 0;
	YbcStatus	status = YBCGetHeapSnapshot(&snapshot, &samples, peak_heap);

	HandleYBStatus(status);

	for (int i = 0; i < samples; i++)
	{
		YbPutHeapSnapshotTupleStore(tupstore, tupdesc, &snapshot[i]);
	}

	pfree(snapshot);

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);
}

Datum
yb_backend_heap_snapshot(PG_FUNCTION_ARGS)
{
	yb_backend_heap_snapshot_internal(false, fcinfo);
	return (Datum) 0;
}

Datum
yb_backend_heap_snapshot_peak(PG_FUNCTION_ARGS)
{
	yb_backend_heap_snapshot_internal(true, fcinfo);
	return (Datum) 0;
}

static void
YbPutHeapSnapshotTupleStore(Tuplestorestate *tupstore, TupleDesc tupdesc,
							YbcHeapSnapshotSample *sample)
{
#define YB_BACKEND_HEAP_SNAPSHOT_COLS 6

	Datum		values[YB_BACKEND_HEAP_SNAPSHOT_COLS];
	bool		nulls[YB_BACKEND_HEAP_SNAPSHOT_COLS];

	memset(values, 0, sizeof(values));
	memset(nulls, 0, sizeof(nulls));

	values[0] = sample->estimated_bytes_is_null ?
		(Datum) 0 :
		Int64GetDatum(sample->estimated_bytes);
	nulls[0] = sample->estimated_bytes_is_null;
	values[1] = sample->estimated_count_is_null ?
		(Datum) 0 :
		Int64GetDatum(sample->estimated_count);
	nulls[1] = sample->estimated_count_is_null;
	values[2] = sample->avg_bytes_per_allocation_is_null ?
		(Datum) 0 :
		Int64GetDatum(sample->avg_bytes_per_allocation);
	nulls[2] = sample->avg_bytes_per_allocation_is_null;
	values[3] = sample->sampled_bytes_is_null ?
		(Datum) 0 :
		Int64GetDatum(sample->sampled_bytes);
	nulls[3] = sample->sampled_bytes_is_null;
	values[4] = sample->sampled_count_is_null ?
		(Datum) 0 :
		Int64GetDatum(sample->sampled_count);
	nulls[4] = sample->sampled_count_is_null;
	values[5] = sample->call_stack_is_null ?
		(Datum) 0 :
		CStringGetTextDatum(sample->call_stack);
	nulls[5] = sample->call_stack_is_null;

	tuplestore_putvalues(tupstore, tupdesc, values, nulls);
}

bool
yb_log_backend_heap_snapshot_internal(int pid, bool peak_heap)
{
	PGPROC	   *proc = BackendPidGetProc(pid);

	/*
	 * BackendPidGetProc returns NULL if the pid isn't valid; but by the time
	 * we reach kill(), a process for which we get a valid proc here might
	 * have terminated on its own.  There's no way to acquire a lock on an
	 * arbitrary process to prevent that. But since this mechanism is usually
	 * used to debug a backend running and consuming lots of memory, that it
	 * might end on its own first and its memory contexts are not logged is
	 * not a problem.
	 */
	if (proc == NULL)
	{
		/*
		 * This is just a warning so a loop-through-resultset will not abort
		 * if one backend terminated on its own during the run.
		 */
		ereport(WARNING,
				(errmsg("PID %d is not a PostgreSQL server process", pid)));
		return false;
	}

	/*
	 * Users are allowed to log the heap snapshot of another process if
	 * one of the following is true:
	 * 1. They are a member of the same role that owns the process
	 * 2. They are a superuser
	 * 3. They are the yb_db_admin user
	 */
	if (!has_privs_of_role(GetUserId(), proc->roleId) &&
		!IsYbDbAdminUser(GetUserId()))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("insufficient privileges to log the heap snapshot of process %d", pid)));

	ProcSignalReason reason = peak_heap ? PROCSIG_LOG_HEAP_SNAPSHOT_PEAK : PROCSIG_LOG_HEAP_SNAPSHOT;

	if (SendProcSignal(pid, reason, proc->backendId) < 0)
	{
		ereport(WARNING, (errmsg("could not send signal to process %d: %m", pid)));
		return false;
	}
	return true;
}

/*
 * YSQL function to log the current heap snapshot for a backend process.
 * Mostly copied from pg_log_backend_memory_contexts.
 */
Datum
yb_log_backend_heap_snapshot(PG_FUNCTION_ARGS)
{
	int			pid = PG_GETARG_INT32(0);

	return yb_log_backend_heap_snapshot_internal(pid, false);
}

/*
 * YSQL function to log the peak heap snapshot for a backend process.
 */
Datum
yb_log_backend_heap_snapshot_peak(PG_FUNCTION_ARGS)
{
	int			pid = PG_GETARG_INT32(0);

	return yb_log_backend_heap_snapshot_internal(pid, true);
}

/*
 * HandleLogHeapSnapshotInterrupt
 *		Handle receipt of an interrupt indicating logging of heap snapshot.
 *
 * All the actual work is deferred to ProcessLogHeapSnapshotInterrupt(),
 * because we cannot safely emit a log message inside the signal handler.
 */
void
HandleLogHeapSnapshotInterrupt(void)
{
	InterruptPending = true;
	LogHeapSnapshotPending = true;
	LogHeapSnapshotPeakHeap = false;
	/* latch will be set by procsignal_sigusr1_handler */
}

/*
 * Same as HandleLogHeapSnapshotInterrupt, but sets LogHeapSnapshotPeakHeap
 * to true.
 */
void
HandleLogHeapSnapshotPeakInterrupt(void)
{
	InterruptPending = true;
	LogHeapSnapshotPending = true;
	LogHeapSnapshotPeakHeap = true;
	/* latch will be set by procsignal_sigusr1_handler */
}

/*
 * ProcessLogHeapSnapshotInterrupt
 * 		Perform logging of heap snapshot of this backend process.
 *
 * Any backend that participates in ProcSignal signaling must arrange
 * to call this function if we see LogHeapSnapshotPending set.
 * It is called from CHECK_FOR_INTERRUPTS(), which is enough because
 * the target process for logging of heap snapshot is a backend.
 */
void
ProcessLogHeapSnapshotInterrupt(void)
{
	LogHeapSnapshotPending = false;

	ereport(LOG,
			(errmsg("logging heap snapshot of PID %d", MyProcPid)));

	/*
	 * When a backend process is consuming huge memory, logging all its heap
	 * snapshot might overrun available disk space. To prevent this, we limit
	 * the number of call stacks to log to 100.
	 */
	YBCDumpTcMallocHeapProfile(LogHeapSnapshotPeakHeap, 100);
}

/*
 * Always setup the hook to log the heap snapshot when a backend process exits.
 * YbLogHeapSnapshotProcExit will decide whether to actually log the heap snapshot
 * if the peak RSS is greater than or equal to yb_log_heap_snapshot_on_exit_threshold
 * at the time of exit.
 */
void
YbSetupHeapSnapshotProcExit(void)
{
	on_proc_exit(YbLogHeapSnapshotProcExit, 0);
}

static void
YbLogHeapSnapshotProcExit(int status, Datum arg)
{
	if (yb_log_heap_snapshot_on_exit_threshold >= 0)
	{
		long		peak_rss_kb = YbGetPeakRssKb();

		if (peak_rss_kb >= yb_log_heap_snapshot_on_exit_threshold)
		{
			ereport(LOG,
					(errmsg("peak heap snapshot of PID %d (peak RSS: %ld KB, threshold: %d KB):",
							MyProcPid, peak_rss_kb, yb_log_heap_snapshot_on_exit_threshold)));
			YBCDumpTcMallocHeapProfile(true, 100);
		}
	}
}
