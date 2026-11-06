/*-------------------------------------------------------------------------
 *
 * yb_memory_watcher.c
 *	  Background worker that reports which PostgreSQL processes hold the
 *	  memory when the node or cgroup is low on memory.
 *
 * The OOM risk is set by the memory left on the node or in the cgroup, not by
 * PostgreSQL's own total (which would take summing PSS across backends), so
 * that is what the worker polls.  It is also what an OOM killer acts on, which
 * keeps the threshold comparable to the OOM killer's.  While available memory
 * is below yb_memory_watcher_threshold_pct, at most once per
 * yb_memory_watcher_report_interval the worker:
 *
 * - logs the processes holding the most anonymous memory, with their queries,
 *	 which is enough on its own when there are simply too many connections;
 * - asks the process with the most anonymous memory to log its heap snapshot,
 *	 for a slow leak;
 * - asks the process whose peak RSS grew the most since the previous report to
 *	 log its peak heap snapshot, for a temporary spike.  TCMalloc keeps the
 *	 lifetime peak profile, so it describes the recent spike only for a
 *	 process whose peak just grew.
 *
 * Only these outliers dump, so hundreds of idle backends do not flood the
 * log.  Per-process numbers come from /proc/<pid>/status, which unlike smaps
 * does not take the target's mmap lock.
 *
 * This only reports.  Killing a process is left to the OOM killer, which
 * also sees processes other than PostgreSQL; under heavy pressure this worker
 * can stall in reclaim like any other process, so its threshold should sit
 * above the OOM killer's.
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
 *	  src/backend/utils/misc/yb_memory_watcher.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "lib/stringinfo.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "postmaster/interrupt.h"
#include "storage/fd.h"
#include "storage/latch.h"
#include "storage/proc.h"
#include "storage/procarray.h"
#include "storage/procsignal.h"
#include "utils/backend_status.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"
#include "utils/timestamp.h"
#include "utils/wait_event.h"
#include "yb_memory_watcher.h"

/* Number of processes listed in each report */
#define YB_MEMORY_WATCHER_TOP_N 10

/* Bytes of each process's query text included in the report */
#define YB_MEMORY_WATCHER_QUERY_LEN 256

/* GUC variables */
bool		yb_enable_memory_watcher = false;
int			yb_memory_watcher_interval_ms = 1000;
int			yb_memory_watcher_threshold_pct = 15;
int			yb_memory_watcher_report_interval_s = 60;

typedef struct YbMemoryWatcherProc
{
	int			pid;
	BackendType backend_type;
	TimestampTz start_time;
	int64		rss_anon_kb;
	int64		hwm_kb;
	int64		hwm_growth_kb;
	char		dbname[NAMEDATALEN];
	char		appname[NAMEDATALEN];
	char		query[YB_MEMORY_WATCHER_QUERY_LEN];
} YbMemoryWatcherProc;

/* Peak RSS of a process as of the previous report */
typedef struct YbMemoryWatcherPeak
{
	int			pid;			/* hash key */
	TimestampTz start_time;		/* tells a reused pid from the old process */
	int64		hwm_kb;
	uint64		report;			/* last report that saw the process */
} YbMemoryWatcherPeak;

static YbMemoryWatcherProc *procs = NULL;
static HTAB *peaks = NULL;
static uint64 report_count = 0;

void
YbMemoryWatcherRegister(void)
{
	BackgroundWorker worker;

	memset(&worker, 0, sizeof(worker));
	sprintf(worker.bgw_name, "yb_memory_watcher");
	sprintf(worker.bgw_type, "yb_memory_watcher");
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_ConsistentState;
	worker.bgw_restart_time = 1;
	sprintf(worker.bgw_library_name, "postgres");
	sprintf(worker.bgw_function_name, "YbMemoryWatcherMain");
	worker.bgw_main_arg = (Datum) 0;
	worker.bgw_notify_pid = 0;
	RegisterBackgroundWorker(&worker);
}

/*
 * Reads the first number in a file, as in cgroup's memory.max and
 * memory.current.  Returns false for "max" (no limit) or a missing file.
 */
static bool
read_int64_file(const char *path, int64 *value)
{
	FILE	   *file = AllocateFile(path, "r");
	long long	v;
	bool		ok;

	if (file == NULL)
		return false;
	ok = fscanf(file, "%lld", &v) == 1;
	FreeFile(file);
	if (ok)
		*value = v;
	return ok;
}

/*
 * Reads "<key> <value>" style files (/proc/meminfo, /proc/<pid>/status,
 * memory.stat).  Each key that is found sets the matching value; the others
 * are left alone.
 */
static void
read_keyed_file(const char *path, int nkeys, const char *const *keys,
				int64 *values)
{
	FILE	   *file = AllocateFile(path, "r");
	char		line[256];

	if (file == NULL)
		return;
	while (fgets(line, sizeof(line), file))
	{
		for (int i = 0; i < nkeys; i++)
		{
			size_t		len = strlen(keys[i]);

			if (strncmp(line, keys[i], len) == 0 &&
				(line[len] == ':' || line[len] == ' '))
			{
				values[i] = strtoll(line + len + 1, NULL, 10);
				break;
			}
		}
	}
	FreeFile(file);
}

/*
 * Lowers *available_kb / *total_kb to the tightest memory limit among this
 * process's cgroup v2 and its ancestors, if that is tighter than the node.
 * Page cache that can be dropped (inactive_file) counts as available, as it
 * does in MemAvailable.
 */
static void
apply_cgroup_limits(int64 *total_kb, int64 *available_kb,
					char *source, size_t source_len)
{
	FILE	   *file = AllocateFile("/proc/self/cgroup", "r");
	char		line[MAXPGPATH];
	char		cgroup[MAXPGPATH] = "";

	if (file == NULL)
		return;
	while (fgets(line, sizeof(line), file))
	{
		if (strncmp(line, "0::", 3) == 0)
		{
			strlcpy(cgroup, line + 3, sizeof(cgroup));
			cgroup[strcspn(cgroup, "\n")] = '\0';
			break;
		}
	}
	FreeFile(file);

	/* Strip one path component per iteration, ending at the root. */
	while (cgroup[0] != '\0')
	{
		char		path[MAXPGPATH];
		int64		max_bytes;
		int64		current_bytes;
		char	   *slash;

		snprintf(path, sizeof(path), "/sys/fs/cgroup%s/memory.max", cgroup);
		if (read_int64_file(path, &max_bytes) && max_bytes > 0)
		{
			static const char *const stat_keys[] = {"inactive_file"};
			int64		inactive_file = 0;

			snprintf(path, sizeof(path), "/sys/fs/cgroup%s/memory.current",
					 cgroup);
			if (read_int64_file(path, &current_bytes))
			{
				int64		limit_kb = max_bytes / 1024;
				int64		cgroup_available_kb;

				snprintf(path, sizeof(path), "/sys/fs/cgroup%s/memory.stat",
						 cgroup);
				read_keyed_file(path, 1, stat_keys, &inactive_file);
				cgroup_available_kb =
					Max(max_bytes - current_bytes + inactive_file, 0) / 1024;

				/* Compare available/total ratios without dividing. */
				if ((double) cgroup_available_kb * *total_kb <
					(double) *available_kb * limit_kb)
				{
					*total_kb = limit_kb;
					*available_kb = cgroup_available_kb;
					snprintf(source, source_len, "cgroup %s", cgroup);
				}
			}
		}

		slash = strrchr(cgroup, '/');
		if (slash == NULL)
			break;
		*slash = '\0';
	}
}

/*
 * Returns false if neither the node nor a cgroup reports its memory.
 */
static bool
get_available_memory(int64 *total_kb, int64 *available_kb,
					 char *source, size_t source_len)
{
	static const char *const keys[] = {"MemTotal", "MemAvailable"};
	int64		values[2] = {-1, -1};

	read_keyed_file("/proc/meminfo", 2, keys, values);
	if (values[0] <= 0 || values[1] < 0)
		return false;
	*total_kb = values[0];
	*available_kb = values[1];
	strlcpy(source, "node", source_len);

	apply_cgroup_limits(total_kb, available_kb, source, source_len);
	return true;
}

/*
 * Copies the identity and query of every backend except this one from the
 * shared backend status array.  This reads the array directly rather than
 * through pgstat_read_current_status(), which also reads each backend's smaps.
 */
static int
collect_processes(void)
{
	PgBackendStatus *beentry = getBackendStatusArray();
	int			nprocs = 0;

	for (int i = 1; i <= MaxBackends; i++, beentry++)
	{
		volatile PgBackendStatus *vbeentry = beentry;
		YbMemoryWatcherProc *proc = &procs[nprocs];
		int			attempt = 1;

		while (yb_pgstat_log_read_activity(vbeentry, ++attempt))
		{
			int			before_changecount;
			int			after_changecount;

			pgstat_begin_read_activity(vbeentry, before_changecount);

			proc->pid = vbeentry->st_procpid;
			if (proc->pid > 0)
			{
				proc->backend_type = vbeentry->st_backendType;
				proc->start_time = vbeentry->st_proc_start_timestamp;
				strlcpy(proc->dbname,
						vbeentry->st_databasename ?
						(char *) vbeentry->st_databasename : "",
						NAMEDATALEN);
				strlcpy(proc->appname, (char *) vbeentry->st_appname,
						NAMEDATALEN);
				strlcpy(proc->query, (char *) vbeentry->st_activity_raw,
						YB_MEMORY_WATCHER_QUERY_LEN);
			}

			pgstat_end_read_activity(vbeentry, after_changecount);

			if (pgstat_read_activity_complete(before_changecount,
											  after_changecount))
				break;

			CHECK_FOR_INTERRUPTS();
		}

		if (proc->pid <= 0 || proc->pid == MyProcPid)
			continue;

		/* strlcpy may have cut a multibyte character. */
		proc->query[pg_mbcliplen(proc->query, strlen(proc->query),
								 YB_MEMORY_WATCHER_QUERY_LEN - 1)] = '\0';
		for (char *c = proc->query; *c; c++)
		{
			if (*c == '\n' || *c == '\r' || *c == '\t')
				*c = ' ';
		}
		nprocs++;
	}

	return nprocs;
}

static bool
read_process_memory(YbMemoryWatcherProc *proc)
{
	static const char *const keys[] = {"RssAnon", "VmHWM"};
	int64		values[2] = {-1, -1};
	char		path[64];

	snprintf(path, sizeof(path), "/proc/%d/status", proc->pid);
	read_keyed_file(path, 2, keys, values);
	if (values[0] < 0 || values[1] < 0)
		return false;			/* exited since we read its status entry */
	proc->rss_anon_kb = values[0];
	proc->hwm_kb = values[1];
	return true;
}

/*
 * Sets each process's peak RSS growth since the previous report and forgets
 * processes that have exited.  A process seen for the first time grew from
 * zero.
 */
static void
update_peaks(int nprocs)
{
	HASH_SEQ_STATUS status;
	YbMemoryWatcherPeak *peak;

	report_count++;
	for (int i = 0; i < nprocs; i++)
	{
		YbMemoryWatcherProc *proc = &procs[i];
		bool		found;

		peak = hash_search(peaks, &proc->pid, HASH_ENTER, &found);
		if (!found || peak->start_time != proc->start_time)
		{
			peak->start_time = proc->start_time;
			peak->hwm_kb = 0;
		}
		proc->hwm_growth_kb = proc->hwm_kb - peak->hwm_kb;
		peak->hwm_kb = proc->hwm_kb;
		peak->report = report_count;
	}

	hash_seq_init(&status, peaks);
	while ((peak = hash_seq_search(&status)) != NULL)
	{
		if (peak->report != report_count)
			hash_search(peaks, &peak->pid, HASH_REMOVE, NULL);
	}
}

static int
compare_rss_anon_desc(const void *a, const void *b)
{
	int64		ra = ((const YbMemoryWatcherProc *) a)->rss_anon_kb;
	int64		rb = ((const YbMemoryWatcherProc *) b)->rss_anon_kb;

	return (ra < rb) - (ra > rb);
}

static void
request_heap_snapshot(int pid, bool peak)
{
	PGPROC	   *proc = BackendPidGetProc(pid);

	/* Only backends and background workers handle the request. */
	if (proc == NULL)
		return;
	if (SendProcSignal(pid,
					   peak ? PROCSIG_LOG_HEAP_SNAPSHOT_PEAK : PROCSIG_LOG_HEAP_SNAPSHOT,
					   proc->backendId) < 0)
		ereport(WARNING,
				(errmsg("could not send signal to process %d: %m", pid)));
}

static void
report(int64 total_kb, int64 available_kb, const char *source)
{
	int			nprocs = collect_processes();
	int			nread = 0;
	int64		total_rss_anon_kb = 0;
	YbMemoryWatcherProc *peak_proc = NULL;
	StringInfoData detail;

	for (int i = 0; i < nprocs; i++)
	{
		if (read_process_memory(&procs[i]))
		{
			if (nread != i)
				procs[nread] = procs[i];
			total_rss_anon_kb += procs[nread].rss_anon_kb;
			nread++;
		}
	}
	nprocs = nread;

	update_peaks(nprocs);
	qsort(procs, nprocs, sizeof(YbMemoryWatcherProc), compare_rss_anon_desc);

	initStringInfo(&detail);
	appendStringInfoString(&detail,
						   "     pid  anon_rss_kb  peak_rss_kb  backend_type / database / application: query");
	for (int i = 0; i < nprocs; i++)
	{
		YbMemoryWatcherProc *proc = &procs[i];

		if (proc->hwm_growth_kb > 0 &&
			(peak_proc == NULL || proc->hwm_growth_kb > peak_proc->hwm_growth_kb))
			peak_proc = proc;

		if (i < YB_MEMORY_WATCHER_TOP_N)
			appendStringInfo(&detail,
							 "\n%8d %12" INT64_MODIFIER "d %12" INT64_MODIFIER "d  %s / %s / %s: %s",
							 proc->pid, proc->rss_anon_kb, proc->hwm_kb,
							 GetBackendTypeDesc(proc->backend_type),
							 proc->dbname, proc->appname, proc->query);
	}

	ereport(LOG,
			(errmsg("low memory: " INT64_FORMAT " kB of " INT64_FORMAT
					" kB available in %s; %d PostgreSQL processes hold "
					INT64_FORMAT " kB of anonymous memory",
					available_kb, total_kb, source, nprocs, total_rss_anon_kb),
			 errdetail_internal("%s", detail.data)));
	pfree(detail.data);

	/*
	 * Both requests set the same pending flag in the target, so one process
	 * would log only one of the two snapshots.
	 */
	if (nprocs > 0 && &procs[0] != peak_proc)
	{
		ereport(LOG,
				(errmsg("asking PID %d, which holds the most anonymous memory ("
						INT64_FORMAT " kB), to log its heap snapshot",
						procs[0].pid, procs[0].rss_anon_kb)));
		request_heap_snapshot(procs[0].pid, false);
	}
	if (peak_proc != NULL)
	{
		ereport(LOG,
				(errmsg("asking PID %d, whose peak RSS grew the most since the "
						"last report (by " INT64_FORMAT " kB), to log its peak "
						"heap snapshot",
						peak_proc->pid, peak_proc->hwm_growth_kb)));
		request_heap_snapshot(peak_proc->pid, true);
	}
}

void
YbMemoryWatcherMain(Datum main_arg)
{
	HASHCTL		ctl;
	TimestampTz last_report = 0;

	pqsignal(SIGHUP, SignalHandlerForConfigReload);
	pqsignal(SIGTERM, SignalHandlerForShutdownRequest);
	pqsignal(SIGINT, SignalHandlerForShutdownRequest);
	pqsignal(SIGQUIT, SignalHandlerForCrashExit);
	BackgroundWorkerUnblockSignals();

	BackgroundWorkerInitializeConnection(NULL, NULL, 0);

	/* Like the ASH collector, keep this worker's idle loop out of ASH. */
	MyProc->yb_is_ash_metadata_set = false;

	pgstat_report_appname("yb_memory_watcher");

	/* Allocate up front: the reports run when memory is short. */
	procs = MemoryContextAlloc(TopMemoryContext,
							   MaxBackends * sizeof(YbMemoryWatcherProc));
	ctl.keysize = sizeof(int);
	ctl.entrysize = sizeof(YbMemoryWatcherPeak);
	peaks = hash_create("yb_memory_watcher peaks", MaxBackends, &ctl,
						HASH_ELEM | HASH_BLOBS);

	while (true)
	{
		int64		total_kb;
		int64		available_kb;
		char		source[MAXPGPATH];
		TimestampTz now;

		(void) WaitLatch(MyLatch,
						 WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
						 yb_memory_watcher_interval_ms,
						 WAIT_EVENT_YB_MEMORY_WATCHER_MAIN);
		ResetLatch(MyLatch);

		HandleMainLoopInterrupts();

		if (!get_available_memory(&total_kb, &available_kb, source,
								  sizeof(source)) ||
			available_kb * 100 >= total_kb * yb_memory_watcher_threshold_pct)
			continue;

		now = GetCurrentTimestamp();
		if (last_report != 0 &&
			!TimestampDifferenceExceeds(last_report, now,
										yb_memory_watcher_report_interval_s * 1000))
			continue;
		last_report = now;

		report(total_kb, available_kb, source);
	}
}
