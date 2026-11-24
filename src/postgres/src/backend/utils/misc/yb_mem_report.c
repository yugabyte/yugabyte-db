/*-------------------------------------------------------------------------
 *
 * yb_mem_report.c
 *	  Test-only report of the idle and peak memory of every postgres process.
 *
 * With the test flag --TEST_ysql_pg_mem_report, which the tserver exports as
 * FLAGS_TEST_ysql_pg_mem_report, every postmaster child records its memory in
 * a slot in shared memory: its idle anonymous RSS after each statement that
 * leaves it outside a transaction, and its peak RSS (VmHWM).  When the
 * postmaster reaps the child, it takes the larger of that peak and the one
 * wait4() reports, compares both numbers with the --TEST_ysql_pg_mem_*_kb
 * budgets, and logs one YB_PG_MEM_RECORD line.  Test harnesses collect these
 * lines and fail the test on a record over budget; the line format is
 * documented in src/yb/yql/pgwrapper/pg_mem_report.h.
 *
 * A process that crosses a budget also logs a TCMalloc heap snapshot, so that
 * the failing test comes with the allocation stacks.
 *
 * Only the postmaster writes the records.  The slot outlives the process, so
 * a crash or SIGKILL keeps the last numbers it stored.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * IDENTIFICATION
 *	  src/backend/utils/misc/yb_mem_report.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "access/xact.h"
#include "libpq/libpq-be.h"
#include "miscadmin.h"
#include "port/atomics.h"
#include "postmaster/bgworker.h"
#include "postmaster/postmaster.h"
#include "storage/ipc.h"
#include "storage/shmem.h"
#include "utils/backend_status.h"
#include "utils/guc.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb_mem_report.h"

#define YB_MEM_REPORT_STMT_LEN 256
#define YB_MEM_REPORT_NAME_LEN 64

/* A peak at least this much above the last one gets the statement recorded. */
#define YB_MEM_REPORT_STMT_PEAK_STEP_KB 1024

/* Heap snapshot limits: per kind and process, and per postmaster. */
#define YB_MEM_REPORT_MAX_SNAPSHOTS_PER_KIND 3
#define YB_MEM_REPORT_MAX_SNAPSHOTS_WITH_STACKS 20
#define YB_MEM_REPORT_SNAPSHOT_STACKS 100

typedef enum YbMemReportKind
{
	YB_MEM_REPORT_IDLE = 0,
	YB_MEM_REPORT_PEAK = 1,
	YB_MEM_REPORT_NUM_KINDS
} YbMemReportKind;

/* Written only by the process that owns it, read by the postmaster after the process exits. */
typedef struct YbMemReportSlot
{
	pid_t		pid;			/* 0 when free */
	BackendType backend_type;
	int64		start_ns;
	int64		idle_anon_kb;	/* max over idle points */
	int64		idle_rss_kb;	/* max over idle points */
	int64		first_idle_anon_kb;
	int64		idle_samples;
	int64		peak_kb;
	int64		peak_shared_kb; /* file + shmem pages when the peak was seen */
	uint32		snapshots;		/* bit per YbMemReportKind */
	char		bgw_type[BGW_MAXLEN];
	char		db[YB_MEM_REPORT_NAME_LEN];
	char		app[YB_MEM_REPORT_NAME_LEN];
	char		stmt[YB_MEM_REPORT_STMT_LEN];
} YbMemReportSlot;

typedef struct YbMemReportShared
{
	pg_atomic_uint32 num_snapshots_with_stacks;
	int			num_child_slots;
	int			num_slots;
	YbMemReportSlot slots[FLEXIBLE_ARRAY_MEMBER];
} YbMemReportShared;

/* One record or event line, formatted without palloc so that reaper() can build it. */
typedef struct YbMemReportLine
{
	char		data[2048];
	int			len;
} YbMemReportLine;

bool		yb_mem_report_enabled = false;

/* Set by the postmaster at startup and inherited by its children. */
static bool enforce = false;
static int64 backend_idle_budget_kb = 0;
static int64 backend_peak_budget_kb = 0;
static int64 parallel_worker_peak_budget_kb = 0;
static int64 bgworker_peak_budget_kb = 0;
static int64 aux_peak_budget_kb = 0;
static int64 postmaster_peak_budget_kb = 0;
static YbMemReportShared *report_shared = NULL;

/* State of the current child process. */
static int64 my_start_ns = 0;
static YbMemReportSlot *my_slot = NULL;
static bool my_slot_resolved = false;
static bool my_budgets_resolved = false;
static int64 my_idle_budget_kb = 0;
static int64 my_peak_budget_kb = 0;
static int64 attributed_peak_kb = 0;
static int	statm_fd = -1;
static int64 page_kb = 0;
static int	num_snapshots[YB_MEM_REPORT_NUM_KINDS];
static int64 last_snapshot_kb[YB_MEM_REPORT_NUM_KINDS];

static bool
env_bool(const char *name)
{
	const char *value = getenv(name);

	return value != NULL && (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
}

static int64
env_int64(const char *name)
{
	const char *value = getenv(name);

	return value != NULL ? strtoll(value, NULL, 10) : 0;
}

static int64
now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return (int64) ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static void
budgets_for_type(BackendType type, const char *bgw_type,
				 int64 *idle_budget_kb, int64 *peak_budget_kb)
{
	*idle_budget_kb = 0;
	*peak_budget_kb = 0;
	switch (type)
	{
		case B_INVALID:
		case B_LOGGER:
			break;
		case B_BG_WORKER:
			*peak_budget_kb = strcmp(bgw_type, "parallel worker") == 0 ?
				parallel_worker_peak_budget_kb : bgworker_peak_budget_kb;
			break;
		case B_AUTOVAC_LAUNCHER:
		case B_AUTOVAC_WORKER:
		case B_BG_WRITER:
		case B_CHECKPOINTER:
		case B_STARTUP:
		case B_WAL_RECEIVER:
		case B_WAL_WRITER:
		case B_ARCHIVER:
			*peak_budget_kb = aux_peak_budget_kb;
			break;
		default:
			/* Client backends, walsenders, and the YB connection-like types. */
			*idle_budget_kb = backend_idle_budget_kb;
			*peak_budget_kb = backend_peak_budget_kb;
			break;
	}
}

static void
line_appendf(YbMemReportLine *line, const char *fmt,...)
pg_attribute_printf(2, 3);

static void
line_appendf(YbMemReportLine *line, const char *fmt,...)
{
	va_list		args;
	int			room = sizeof(line->data) - line->len;
	int			n;

	if (room <= 1)
		return;
	va_start(args, fmt);
	n = vsnprintf(line->data + line->len, room, fmt, args);
	va_end(args);
	if (n > 0)
		line->len += Min(n, room - 1);
}

/* Appends " key=value", double-quoting the value and escaping \ " and newlines. */
static void
line_append_quoted(YbMemReportLine *line, const char *key, const char *value)
{
	const char *p;

	line_appendf(line, " %s=\"", key);
	for (p = value; *p != '\0'; p++)
	{
		/* Keep room for the closing quote. */
		if (line->len >= (int) sizeof(line->data) - 4)
			break;
		if (*p == '"' || *p == '\\')
		{
			line->data[line->len++] = '\\';
			line->data[line->len++] = *p;
		}
		else if (*p == '\n')
		{
			line->data[line->len++] = '\\';
			line->data[line->len++] = 'n';
		}
		else if ((unsigned char) *p < ' ')
			line->data[line->len++] = ' ';
		else
			line->data[line->len++] = *p;
	}
	line->data[line->len++] = '"';
	line->data[line->len] = '\0';
}

static const char *
kinds_string(bool idle, bool peak)
{
	if (idle && peak)
		return "idle,peak";
	if (idle)
		return "idle";
	if (peak)
		return "peak";
	return "-";
}

/* Logs the YB_PG_MEM_RECORD line of one process. slot may be NULL. */
static void
log_record(pid_t pid, const char *type_desc, const YbMemReportSlot *slot,
		   int64 maxrss_kb, int exit_status,
		   int64 idle_budget_kb, int64 peak_budget_kb)
{
	YbMemReportLine line = {0};
	char		type[64];
	char	   *p;
	int64		peak_kb = Max(maxrss_kb, slot ? slot->peak_kb : 0);
	int64		idle_anon_kb = slot ? slot->idle_anon_kb : 0;
	bool		idle_over = idle_budget_kb > 0 && idle_anon_kb > idle_budget_kb;
	bool		peak_over = peak_budget_kb > 0 && peak_kb > peak_budget_kb;

	strlcpy(type, type_desc, sizeof(type));
	for (p = type; *p != '\0'; p++)
	{
		if (*p == ' ')
			*p = '_';
	}

	line_appendf(&line,
				 "YB_PG_MEM_RECORD pm=%d pid=%d type=%s start_ns=" INT64_FORMAT
				 " exit=%d idle_anon_kb=" INT64_FORMAT " idle_budget_kb=" INT64_FORMAT
				 " first_idle_anon_kb=" INT64_FORMAT " idle_rss_kb=" INT64_FORMAT
				 " idle_samples=" INT64_FORMAT " peak_kb=" INT64_FORMAT
				 " peak_budget_kb=" INT64_FORMAT " peak_shared_kb=" INT64_FORMAT
				 " over=%s snap=%s",
				 (int) PostmasterPid, (int) pid, type,
				 slot ? slot->start_ns : 0, exit_status, idle_anon_kb, idle_budget_kb,
				 slot ? slot->first_idle_anon_kb : 0, slot ? slot->idle_rss_kb : 0,
				 slot ? slot->idle_samples : 0, peak_kb, peak_budget_kb,
				 slot ? slot->peak_shared_kb : 0,
				 kinds_string(idle_over, peak_over),
				 kinds_string(slot && (slot->snapshots & (1 << YB_MEM_REPORT_IDLE)),
							  slot && (slot->snapshots & (1 << YB_MEM_REPORT_PEAK))));
	line_append_quoted(&line, "bgw", slot ? slot->bgw_type : "");
	line_append_quoted(&line, "db", slot ? slot->db : "");
	line_append_quoted(&line, "app", slot ? slot->app : "");
	line_append_quoted(&line, "stmt", slot ? slot->stmt : "");

	ereport(LOG_SERVER_ONLY,
			(errmsg_internal("%s", line.data),
			 errhidestmt(true),
			 errhidecontext(true)));
}

static void
postmaster_exit(int code, Datum arg)
{
	struct rusage ru;

	getrusage(RUSAGE_SELF, &ru);
	log_record(MyProcPid, "postmaster", NULL, ru.ru_maxrss, code,
			   0, postmaster_peak_budget_kb);
}

/*
 * Reads the flags the tserver exports.  Called by the postmaster before it
 * sizes shared memory.
 */
void
YbMemReportPostmasterInit(void)
{
	yb_mem_report_enabled = env_bool("FLAGS_TEST_ysql_pg_mem_report");
	if (!yb_mem_report_enabled)
		return;

	enforce = env_bool("FLAGS_TEST_ysql_pg_mem_enforce");
	backend_idle_budget_kb = env_int64("FLAGS_TEST_ysql_pg_mem_backend_idle_kb");
	backend_peak_budget_kb = env_int64("FLAGS_TEST_ysql_pg_mem_backend_peak_kb");
	parallel_worker_peak_budget_kb =
		env_int64("FLAGS_TEST_ysql_pg_mem_parallel_worker_peak_kb");
	bgworker_peak_budget_kb = env_int64("FLAGS_TEST_ysql_pg_mem_bgworker_peak_kb");
	aux_peak_budget_kb = env_int64("FLAGS_TEST_ysql_pg_mem_aux_peak_kb");
	postmaster_peak_budget_kb = env_int64("FLAGS_TEST_ysql_pg_mem_postmaster_peak_kb");

	on_proc_exit(postmaster_exit, (Datum) 0);

	ereport(LOG_SERVER_ONLY,
			(errmsg_internal("YB_PG_MEM_START pm=%d enforce=%d",
							 MyProcPid, enforce ? 1 : 0)));
}

Size
YbMemReportShmemSize(void)
{
	if (!yb_mem_report_enabled)
		return 0;
	return add_size(offsetof(YbMemReportShared, slots),
					mul_size(MaxLivePostmasterChildren() + NUM_AUXPROCTYPES,
							 sizeof(YbMemReportSlot)));
}

void
YbMemReportShmemInit(void)
{
	bool		found;

	if (!yb_mem_report_enabled)
		return;

	report_shared = ShmemInitStruct("YB memory report", YbMemReportShmemSize(), &found);
	if (!found)
	{
		memset(report_shared, 0, YbMemReportShmemSize());
		pg_atomic_init_u32(&report_shared->num_snapshots_with_stacks, 0);
		report_shared->num_child_slots = MaxLivePostmasterChildren();
		report_shared->num_slots = report_shared->num_child_slots + NUM_AUXPROCTYPES;
	}
}

/*
 * Backends and background workers use their postmaster child slot, which the
 * postmaster frees only after it has reaped the process and logged its
 * record.  Auxiliary processes, which have no child slot, use one slot per
 * auxiliary process type.  Resolved on first use, since MyAuxProcType is set
 * after InitPostmasterChild().
 */
static YbMemReportSlot *
get_my_slot(void)
{
	int			index = -1;

	if (my_slot_resolved)
		return my_slot;
	my_slot_resolved = true;

	if (report_shared == NULL)
		return NULL;
	if (MyPMChildSlot > 0)
		index = MyPMChildSlot - 1;
	else if (MyAuxProcType != NotAnAuxProcess)
		index = report_shared->num_child_slots + MyAuxProcType;
	if (index < 0 || index >= report_shared->num_slots)
		return NULL;

	my_slot = &report_shared->slots[index];
	memset(my_slot, 0, sizeof(YbMemReportSlot));
	my_slot->start_ns = my_start_ns;
	my_slot->pid = MyProcPid;
	return my_slot;
}

/* Reads resident and shared (file + shmem) pages from /proc/self/statm. */
static bool
read_statm(int64 *rss_kb, int64 *shared_kb)
{
	char		buf[128];
	ssize_t		n;
	long		size,
				resident,
				shared_pages;

	if (statm_fd < 0)
	{
		statm_fd = open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
		page_kb = sysconf(_SC_PAGESIZE) / 1024;
		if (statm_fd < 0)
			return false;
	}
	n = pread(statm_fd, buf, sizeof(buf) - 1, 0);
	if (n <= 0)
		return false;
	buf[n] = '\0';
	if (sscanf(buf, "%ld %ld %ld", &size, &resident, &shared_pages) != 3)
		return false;
	*rss_kb = resident * page_kb;
	*shared_kb = shared_pages * page_kb;
	return true;
}

/*
 * Logs a heap snapshot the first time a value crosses its budget, and again
 * each time it grows another 25%, up to a limit.
 */
static void
maybe_log_heap_snapshot(YbMemReportSlot *slot, YbMemReportKind kind,
						int64 value_kb, int64 budget_kb)
{
	bool		with_stacks;

	if (budget_kb <= 0 || value_kb <= budget_kb)
		return;
	if (num_snapshots[kind] >= YB_MEM_REPORT_MAX_SNAPSHOTS_PER_KIND)
		return;
	if (num_snapshots[kind] > 0 &&
		value_kb < last_snapshot_kb[kind] + last_snapshot_kb[kind] / 4)
		return;

	num_snapshots[kind]++;
	last_snapshot_kb[kind] = value_kb;
	slot->snapshots |= 1 << kind;
	with_stacks = pg_atomic_fetch_add_u32(&report_shared->num_snapshots_with_stacks, 1) <
		YB_MEM_REPORT_MAX_SNAPSHOTS_WITH_STACKS;

	ereport(LOG_SERVER_ONLY,
			(errmsg_internal("YB_PG_MEM_SNAPSHOT pm=%d pid=%d kind=%s value_kb=" INT64_FORMAT
							 " budget_kb=" INT64_FORMAT " stacks=%d",
							 (int) PostmasterPid, MyProcPid,
							 kind == YB_MEM_REPORT_PEAK ? "peak" : "idle",
							 value_kb, budget_kb, with_stacks ? 1 : 0),
			 errhidestmt(true),
			 errhidecontext(true)));
	if (with_stacks)
		YBCDumpTcMallocHeapProfile(kind == YB_MEM_REPORT_PEAK,
								   YB_MEM_REPORT_SNAPSHOT_STACKS);
}

/*
 * Updates this process's slot.  after_statement: called right after a
 * statement, so peak growth can be charged to it.  idle: outside a
 * transaction, so RSS is what the process keeps between statements.
 */
static void
sample(bool after_statement, bool idle)
{
	YbMemReportSlot *slot = get_my_slot();
	struct rusage ru;
	int64		peak_kb;
	int64		rss_kb = -1;
	int64		shared_kb = -1;
	bool		have_statm;

	if (slot == NULL)
		return;

	slot->backend_type = MyBackendType;
	if (MyBgworkerEntry != NULL)
		strlcpy(slot->bgw_type, MyBgworkerEntry->bgw_type, sizeof(slot->bgw_type));
	if (MyProcPort != NULL && MyProcPort->database_name != NULL)
		strlcpy(slot->db, MyProcPort->database_name, sizeof(slot->db));
	if (application_name != NULL)
		strlcpy(slot->app, application_name, sizeof(slot->app));
	if (!my_budgets_resolved)
	{
		budgets_for_type(MyBackendType, slot->bgw_type,
						 &my_idle_budget_kb, &my_peak_budget_kb);
		my_budgets_resolved = true;
	}

	getrusage(RUSAGE_SELF, &ru);
	peak_kb = ru.ru_maxrss;
	have_statm = read_statm(&rss_kb, &shared_kb);

	if (peak_kb > slot->peak_kb)
	{
		slot->peak_kb = peak_kb;
		if (have_statm)
			slot->peak_shared_kb = shared_kb;
	}

	/*
	 * The first sample, at the first ReadyForQuery, is the baseline left by
	 * connection startup; later growth is charged to the statement that just
	 * ran.
	 */
	if (after_statement)
	{
		if (attributed_peak_kb == 0)
			attributed_peak_kb = peak_kb;
		else if (peak_kb >= attributed_peak_kb + YB_MEM_REPORT_STMT_PEAK_STEP_KB)
		{
			attributed_peak_kb = peak_kb;
			if (MyBEEntry != NULL && MyBEEntry->st_activity_raw != NULL)
				strlcpy(slot->stmt, MyBEEntry->st_activity_raw, sizeof(slot->stmt));
		}
	}

	if (idle && have_statm)
	{
		int64		anon_kb = rss_kb - shared_kb;

		if (slot->idle_samples == 0)
			slot->first_idle_anon_kb = anon_kb;
		slot->idle_samples++;
		slot->idle_anon_kb = Max(slot->idle_anon_kb, anon_kb);
		slot->idle_rss_kb = Max(slot->idle_rss_kb, rss_kb);
		maybe_log_heap_snapshot(slot, YB_MEM_REPORT_IDLE, anon_kb, my_idle_budget_kb);
	}
	maybe_log_heap_snapshot(slot, YB_MEM_REPORT_PEAK, peak_kb, my_peak_budget_kb);
}

static void
child_exit(int code, Datum arg)
{
	sample(false, false);
}

/* Called from InitPostmasterChild(). */
void
YbMemReportInitChild(void)
{
	if (!yb_mem_report_enabled)
		return;
	my_start_ns = now_ns();
	on_proc_exit(child_exit, (Datum) 0);
}

/*
 * Called after every statement, once ReadyForQuery has been sent and TCMalloc
 * has released free memory, so idle RSS is what the backend keeps.
 */
void
YbMemReportAfterStatement(void)
{
	if (!yb_mem_report_enabled)
		return;
	sample(true, !IsTransactionOrTransactionBlock());
}

/*
 * waitpid(-1, exitstatus, WNOHANG) for reaper().  With the report on, uses
 * wait4() to get the child's peak RSS and logs the child's record, before
 * reaper() releases its child slot.
 */
int
YbMemReportWaitpid(int *exitstatus)
{
	struct rusage ru;
	pid_t		pid;
	YbMemReportSlot *slot = NULL;
	int64		idle_budget_kb;
	int64		peak_budget_kb;
	int			i;

	if (!yb_mem_report_enabled)
		return waitpid(-1, exitstatus, WNOHANG);

	pid = wait4(-1, exitstatus, WNOHANG, &ru);
	if (pid <= 0)
		return pid;

	for (i = 0; report_shared != NULL && i < report_shared->num_slots; i++)
	{
		if (report_shared->slots[i].pid == pid)
		{
			slot = &report_shared->slots[i];
			break;
		}
	}

	if (slot == NULL)
	{
		log_record(pid, "unknown", NULL, ru.ru_maxrss, *exitstatus, 0, 0);
		return pid;
	}

	/* The child stored it, but bound the copy in case it was corrupted. */
	slot->bgw_type[sizeof(slot->bgw_type) - 1] = '\0';
	slot->db[sizeof(slot->db) - 1] = '\0';
	slot->app[sizeof(slot->app) - 1] = '\0';
	slot->stmt[sizeof(slot->stmt) - 1] = '\0';
	budgets_for_type(slot->backend_type, slot->bgw_type,
					 &idle_budget_kb, &peak_budget_kb);
	log_record(pid, GetBackendTypeDesc(slot->backend_type), slot,
			   ru.ru_maxrss, *exitstatus, idle_budget_kb, peak_budget_kb);
	slot->pid = 0;
	return pid;
}
