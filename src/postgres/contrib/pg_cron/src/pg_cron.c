/*-------------------------------------------------------------------------
 *
 * src/pg_cron.c
 *
 * Implementation of the pg_cron task scheduler.
 * Wording:
 *     - A job is a scheduling definition of a task
 *     - A task is what is actually executed within the database engine
 *
 * Copyright (c) 2016, Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */
#include <sys/resource.h>

#include "c.h"
#include "postgres.h"
#include "fmgr.h"

/* these are always necessary for a bgworker */
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shm_mq.h"
#include "storage/shm_toc.h"
#include "storage/shmem.h"

/* these headers are used by this particular worker's code */

#define MAIN_PROGRAM
#include "cron.h"

#include "job_metadata.h"
#include "pg_cron.h"
#include "task_states.h"


#ifdef HAVE_POLL_H
#include <poll.h>
#elif defined(HAVE_SYS_POLL_H)
#include <sys/poll.h>
#endif

#include "sys/time.h"
#include "time.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/printtup.h"
#include "access/xact.h"
#include "access/xlog.h"
#include "catalog/pg_extension.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "commands/async.h"
#include "commands/dbcommands.h"
#include "commands/extension.h"
#include "commands/sequence.h"
#include "commands/trigger.h"
#include "lib/stringinfo.h"
#include "libpq-fe.h"
#include "libpq/pqmq.h"
#include "libpq/pqsignal.h"
#include "mb/pg_wchar.h"
#include "parser/analyze.h"
#include "pgstat.h"
#include "postmaster/postmaster.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/portal.h"
#include "utils/ps_status.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
#include "utils/timeout.h"
#include "utils/timestamp.h"
#if (PG_VERSION_NUM >= 100000)
#include "utils/varlena.h"
#endif
#include "tcop/pquery.h"
#include "tcop/utility.h"
#include "libpq/pqformat.h"
#include "utils/builtins.h"

#if (PG_VERSION_NUM < 120000)
#define table_open(r, l) heap_open(r, l)
#define table_close(r, l) heap_close(r, l)
#endif

PG_MODULE_MAGIC;

#ifndef MAXINT8LEN
#define MAXINT8LEN 20
#endif

/* Table-of-contents constants for our dynamic shared memory segment. */
#define PG_CRON_MAGIC      0x51028080
#define PG_CRON_KEY_DATABASE  0
#define PG_CRON_KEY_USERNAME  1
#define PG_CRON_KEY_COMMAND    2
#define PG_CRON_KEY_QUEUE    3
#define PG_CRON_NKEYS      4

/* ways in which the clock can change between main loop iterations */
typedef enum
{
  CLOCK_JUMP_BACKWARD = 0,
  CLOCK_PROGRESSED = 1,
  CLOCK_JUMP_FORWARD = 2,
  CLOCK_CHANGE = 3
} ClockProgress;

/* forward declarations */
void _PG_init(void);
void _PG_fini(void);
static void pg_cron_sigterm(SIGNAL_ARGS);
static void pg_cron_sighup(SIGNAL_ARGS);
static void pg_cron_background_worker_sigterm(SIGNAL_ARGS);
void PgCronLauncherMain(Datum arg);
void CronBackgroundWorker(Datum arg);

static void StartAllPendingRuns(List *taskList, TimestampTz currentTime);
static void StartPendingRuns(CronTask *task, ClockProgress clockProgress,
               TimestampTz lastMinute, TimestampTz currentTime);
static int MinutesPassed(TimestampTz startTime, TimestampTz stopTime);
static int SecondsPassed(TimestampTz startTime, TimestampTz stopTime);
static TimestampTz TimestampMinuteStart(TimestampTz time);
static TimestampTz TimestampMinuteEnd(TimestampTz time);
static bool ShouldRunTask(entry *schedule, TimestampTz currentMinute,
              bool doWild, bool doNonWild);

static void WaitForCronTasks(List *taskList);
static void WaitForLatch(int timeoutMs);
static void PollForTasks(List *taskList);
static bool CanStartTask(CronTask *task);
static void ManageCronTasks(List *taskList, TimestampTz currentTime);
static void ManageCronTask(CronTask *task, TimestampTz currentTime);
static void ExecuteSqlString(const char *sql);
static void GetTaskFeedback(PGresult *result, CronTask *task);
static void GetBgwTaskFeedback(shm_mq_handle *responseq, CronTask *task, bool running);

static bool jobCanceled(CronTask *task);
static bool jobStartupTimeout(CronTask *task, TimestampTz currentTime);
static char* pg_cron_cmdTuples(char *msg);
static void bgw_generate_returned_message(StringInfoData *display_msg, ErrorData edata);

static bool CheckForLeaderStateChange(bool prevLeaderState, TimestampTz currentTime, List* task_list);
static void ManageCronTaskWaiting(CronTask *task, const CronJob *cronJob);
static void UpdateKnownNodesAndWorkload(TimestampTz currentTime, bool force);
static void CheckTimedOutTasks(List *taskList, TimestampTz currentTime);

/* global settings */
char *CronTableDatabaseName = "system_platform";
// maximum length of c-string nodenames including the null character
// necessary because HTAB requires a fixed key size to allocate space for the
// storing the key.
// MODIFY worker_load.h IF YOU CHANGE THIS VALUE
const int MaxNodenameLength = 64;
// time in seconds between queries for the available nodes in the cluster and
// the cluster load
int ClusterStatusQueryPeriod = 30;
// time in seconds between queries for the leader state of this cron worker
int LeaderStatusQueryPeriod = 30;
// nodename used to identify this cron worker. same as the one given
// to the tserver associated with our parent postgres process
const char* MyNodeName = NULL;
bool UseBackgroundWorkers = true;
static bool CronLogStatement = true;
static bool CronLogRun = true;
static bool CronReloadConfig = false;

/* flags set by signal handlers */
static volatile sig_atomic_t got_sigterm = false;

/* global variables */
/* maximum connection time and time for bgw slot to be available in ms */
static const int CronTaskStartTimeout = 10000;
// maximum time in s to wait for job completion not including
// CronTaskStartTimeout
static const int MaxJobCompletionDelay = 10;
static const int MaxWait = 1000; /* maximum time in ms that poll() can block */
static bool RebootJobsScheduled = false;
/* Number of jobs running in the cluster. For now we are not updating this */
static int RunningTaskCount = 0;
static int MaxRunningTasks = 0;
static int CronLogMinMessages = WARNING;

static const struct config_enum_entry cron_message_level_options[] = {
  {"debug5", DEBUG5, false},
  {"debug4", DEBUG4, false},
  {"debug3", DEBUG3, false},
  {"debug2", DEBUG2, false},
  {"debug1", DEBUG1, false},
  {"debug", DEBUG2, true},
  {"info", INFO, false},
  {"notice", NOTICE, false},
  {"warning", WARNING, false},
  {"error", ERROR, false},
  {"log", LOG, false},
  {"fatal", FATAL, false},
  {"panic", PANIC, false},
  {NULL, 0, false}
};

static const char *cron_error_severity(int elevel);

/*
 * _PG_init gets called when the extension is loaded.
 */
void
_PG_init(void)
{
  BackgroundWorker worker;

  if (IsBinaryUpgrade)
  {
    return;
  }

  if (!process_shared_preload_libraries_in_progress)
  {
    ereport(ERROR, (errmsg("pg_cron can only be loaded via shared_preload_libraries"),
            errhint("Add pg_cron to the shared_preload_libraries "
                "configuration variable in postgresql.conf.")));
  }

  DefineCustomStringVariable(
    "cron.database_name",
    gettext_noop("Database in which pg_cron metadata is kept."),
    NULL,
    &CronTableDatabaseName,
    "system_platform",
    PGC_POSTMASTER,
    GUC_SUPERUSER_ONLY,
    NULL, NULL, NULL);

  DefineCustomBoolVariable(
    "cron.log_statement",
    gettext_noop("Log all cron statements prior to execution."),
    NULL,
    &CronLogStatement,
    true,
    PGC_POSTMASTER,
    GUC_SUPERUSER_ONLY,
    NULL, NULL, NULL);

  // We always log runs because we use the job_run_details to inform
  // other workers about what work they need to do
  // DefineCustomBoolVariable(
  //   "cron.log_run",
  //   gettext_noop("Log all jobs runs into the job_run_details table"),
  //   NULL,
  //   &CronLogRun,
  //   true,
  //   PGC_POSTMASTER,
  //   GUC_SUPERUSER_ONLY,
  //   NULL, NULL, NULL);

  DefineCustomBoolVariable(
    "cron.enable_superuser_jobs",
    gettext_noop("Allow jobs to be scheduled as superuser"),
    NULL,
    &EnableSuperuserJobs,
    true,
    PGC_USERSET,
    GUC_SUPERUSER_ONLY,
    NULL, NULL, NULL);

  // For now, we will only support background workers so this setting will not be used.
  // DefineCustomStringVariable(
  //   "cron.host",
  //   gettext_noop("Hostname to connect to postgres."),
  //   gettext_noop("This setting has no effect when background workers are used."),
  //   &CronHost,
  //   "localhost",
  //   PGC_POSTMASTER,
  //   GUC_SUPERUSER_ONLY,
  //   NULL, NULL, NULL);

  DefineCustomIntVariable(
    "cron.cluster_status_query_period",
    gettext_noop("Seconds between queries for available nodes in the cluster."),
    NULL,
    &ClusterStatusQueryPeriod,
    30,
    0,
    60,
    PGC_POSTMASTER,
    GUC_SUPERUSER_ONLY,
    NULL, NULL, NULL);

  DefineCustomIntVariable(
    "cron.leader_status_query_period",
    gettext_noop("Seconds between queries for the leader status of the node."),
    NULL,
    &LeaderStatusQueryPeriod,
    30,
    0,
    60,
    PGC_POSTMASTER,
    GUC_SUPERUSER_ONLY,
    NULL, NULL, NULL);

  DefineCustomBoolVariable(
    "cron.use_background_workers",
    gettext_noop("Use background workers instead of client sessions."),
    NULL,
    &UseBackgroundWorkers,
    true,
    PGC_POSTMASTER,
    GUC_SUPERUSER_ONLY,
    NULL, NULL, NULL);

  if (!UseBackgroundWorkers)
    DefineCustomIntVariable(
      "cron.max_running_jobs",
      gettext_noop("Maximum number of jobs that can run concurrently."),
      NULL,
      &MaxRunningTasks,
      (MaxConnections < 32) ? MaxConnections : 32,
      0,
      MaxConnections,
      PGC_POSTMASTER,
      GUC_SUPERUSER_ONLY,
      NULL, NULL, NULL);
  else
    DefineCustomIntVariable(
      "cron.max_running_jobs",
      gettext_noop("Maximum number of jobs that can run concurrently."),
      NULL,
      &MaxRunningTasks,
      (max_worker_processes - 1 < 5) ? max_worker_processes - 1 : 5,
      0,
      max_worker_processes - 1,
      PGC_POSTMASTER,
      GUC_SUPERUSER_ONLY,
      NULL, NULL, NULL);

  DefineCustomEnumVariable(
    "cron.log_min_messages",
    gettext_noop("log_min_messages for the launcher bgworker."),
    NULL,
    &CronLogMinMessages,
    WARNING,
    cron_message_level_options,
    PGC_SIGHUP,
    GUC_SUPERUSER_ONLY,
    NULL, NULL, NULL);

  /* set up common data for all our workers */
  worker.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
  worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
  worker.bgw_restart_time = 1;
#if (PG_VERSION_NUM < 100000)
  worker.bgw_main = PgCronLauncherMain;
#endif
  worker.bgw_main_arg = Int32GetDatum(0);
  worker.bgw_notify_pid = 0;
  sprintf(worker.bgw_library_name, "pg_cron");
  sprintf(worker.bgw_function_name, "PgCronLauncherMain");
  snprintf(worker.bgw_name, BGW_MAXLEN, "pg_cron launcher");
#if (PG_VERSION_NUM >= 110000)
  snprintf(worker.bgw_type, BGW_MAXLEN, "pg_cron launcher");
#endif
  MyNodeName = YBGetCurrentMetricNodeName();
  Assert(MyNodeName != NULL);
  RegisterBackgroundWorker(&worker);
}


/*
 * Signal handler for SIGTERM
 *    Set a flag to let the main loop to terminate, and set our latch to wake
 *    it up.
 */
static void
pg_cron_sigterm(SIGNAL_ARGS)
{
  got_sigterm = true;

  if (MyProc != NULL)
  {
    SetLatch(&MyProc->procLatch);
  }
}


/*
 * Signal handler for SIGHUP
 *    Set a flag to tell the main loop to reload the cron jobs.
 */
static void
pg_cron_sighup(SIGNAL_ARGS)
{
  CronJobCacheValid = false;
  CronReloadConfig = true;

  if (MyProc != NULL)
  {
    SetLatch(&MyProc->procLatch);
  }
}

/*
 * pg_cron_cmdTuples -
 *      mainly copy/pasted from PQcmdTuples
 *      If the last command was INSERT/UPDATE/DELETE/MOVE/FETCH/COPY, return
 *      a string containing the number of inserted/affected tuples. If not,
 *      return "".
 *
 *      XXX: this should probably return an int
 */

static char *
pg_cron_cmdTuples(char *msg)
{
        char       *p,
                           *c;

        if (!msg)
                return "";

        if (strncmp(msg, "INSERT ", 7) == 0)
        {
                p = msg + 7;
                /* INSERT: skip oid and space */
                while (*p && *p != ' ')
                        p++;
                if (*p == 0)
                        goto interpret_error;   /* no space? */
                p++;
        }
        else if (strncmp(msg, "SELECT ", 7) == 0 ||
                         strncmp(msg, "DELETE ", 7) == 0 ||
                         strncmp(msg, "UPDATE ", 7) == 0)
                p = msg + 7;
        else if (strncmp(msg, "FETCH ", 6) == 0)
                p = msg + 6;
        else if (strncmp(msg, "MOVE ", 5) == 0 ||
                         strncmp(msg, "COPY ", 5) == 0)
                p = msg + 5;
        else
                return "";

        /* check that we have an integer (at least one digit, nothing else) */
        for (c = p; *c; c++)
        {
                if (!isdigit((unsigned char) *c))
                        goto interpret_error;
        }
        if (c == p)
                goto interpret_error;

        return p;

interpret_error:
  ereport(LOG, (errmsg("could not interpret result from server: %s", msg)));
        return "";
}

/*
 * cron_error_severity --- get string representing elevel
 */
static const char *
cron_error_severity(int elevel)
{
  const char *elevel_char;

  switch (elevel)
  {
    case DEBUG1:
      elevel_char = "DEBUG1";
      break;
    case DEBUG2:
      elevel_char = "DEBUG2";
      break;
    case DEBUG3:
      elevel_char = "DEBUG3";
      break;
    case DEBUG4:
      elevel_char = "DEBUG4";
      break;
    case DEBUG5:
      elevel_char = "DEBUG5";
      break;
    case LOG:
      elevel_char = "LOG";
      break;
    case INFO:
      elevel_char = "INFO";
      break;
    case NOTICE:
      elevel_char = "NOTICE";
      break;
    case WARNING:
      elevel_char = "WARNING";
      break;
    case ERROR:
      elevel_char = "ERROR";
      break;
    case FATAL:
      elevel_char = "FATAL";
      break;
    case PANIC:
      elevel_char = "PANIC";
      break;
    default:
      elevel_char = "???";
      break;
  }

  return elevel_char;
}

/*
 * bgw_generate_returned_message -
 *      generates the message to be inserted into the job_run_details table
 *      first part is comming from error_severity (elog.c)
 */
static void
bgw_generate_returned_message(StringInfoData *display_msg, ErrorData edata)
{
  const char *prefix;

  switch (edata.elevel)
  {
    case DEBUG1:
    case DEBUG2:
    case DEBUG3:
    case DEBUG4:
    case DEBUG5:
      prefix = gettext_noop("DEBUG");
      break;
    case LOG:
#if (PG_VERSION_NUM >= 100000)
    case LOG_SERVER_ONLY:
#endif
      prefix = gettext_noop("LOG");
      break;
    case INFO:
      prefix = gettext_noop("INFO");
      break;
    case NOTICE:
      prefix = gettext_noop("NOTICE");
      break;
    case WARNING:
      prefix = gettext_noop("WARNING");
      break;
    case ERROR:
      prefix = gettext_noop("ERROR");
      break;
    case FATAL:
      prefix = gettext_noop("FATAL");
      break;
    case PANIC:
      prefix = gettext_noop("PANIC");
      break;
    default:
      prefix = "???";
      break;
  }

  appendStringInfo(display_msg, "%s: %s", prefix, edata.message);
  if (edata.detail != NULL)
    appendStringInfo(display_msg, "\nDETAIL: %s", edata.detail);

  if (edata.hint != NULL)
    appendStringInfo(display_msg, "\nHINT: %s", edata.hint);

  if (edata.context != NULL)
    appendStringInfo(display_msg, "\nCONTEXT: %s", edata.context);
}

/*
 * Signal handler for SIGTERM for background workers
 *     When we receive a SIGTERM, we set InterruptPending and ProcDiePending
 *     just like a normal backend.  The next CHECK_FOR_INTERRUPTS() will do the
 *     right thing.
 */
static void
pg_cron_background_worker_sigterm(SIGNAL_ARGS)
{
  int save_errno = errno;

  if (MyProc)
    SetLatch(&MyProc->procLatch);

  if (!proc_exit_inprogress)
  {
    InterruptPending = true;
    ProcDiePending = true;
  }

  errno = save_errno;
}


/*
 * PgCronLauncherMain is the main entry-point for the background worker
 * that performs tasks.
 */
void
PgCronLauncherMain(Datum arg)
{
  MemoryContext CronLoopContext = NULL;
  struct rlimit limit;

  /* Establish signal handlers before unblocking signals. */
  pqsignal(SIGHUP, pg_cron_sighup);
  pqsignal(SIGINT, SIG_IGN);
  pqsignal(SIGTERM, pg_cron_sigterm);

  /* We're now ready to receive signals */
  BackgroundWorkerUnblockSignals();

  /* Connect to our database */
#if (PG_VERSION_NUM < 110000)
  BackgroundWorkerInitializeConnection(CronTableDatabaseName, NULL);
#else
  BackgroundWorkerInitializeConnection(CronTableDatabaseName, NULL, 0);
#endif

  /* Make pg_cron recognisable in pg_stat_activity */
  pgstat_report_appname("pg_cron scheduler");

  /*
   * Mark anything that was in progress before the database restarted as
   * failed.
   */
  MarkPendingRunsAsFailed();

  /* Determine how many tasks we can run concurrently */
  if (MaxConnections < MaxRunningTasks)
  {
    MaxRunningTasks = MaxConnections;
  }

  if (max_files_per_process < MaxRunningTasks)
  {
    MaxRunningTasks = max_files_per_process;
  }

  if (getrlimit(RLIMIT_NOFILE, &limit) != 0 &&
    limit.rlim_cur < (uint32) MaxRunningTasks)
  {
    MaxRunningTasks = limit.rlim_cur;
  }

  if (UseBackgroundWorkers && max_worker_processes - 1 < MaxRunningTasks)
  {
    MaxRunningTasks = max_worker_processes - 1;
  }

  if (MaxRunningTasks <= 0)
  {
    MaxRunningTasks = 1;
  }


  CronLoopContext = AllocSetContextCreate(CurrentMemoryContext,
                        "pg_cron loop context",
                        ALLOCSET_DEFAULT_MINSIZE,
                        ALLOCSET_DEFAULT_INITSIZE,
                        ALLOCSET_DEFAULT_MAXSIZE);
  InitializeJobMetadataCache();
  InitializeTaskStateHash();

  ereport(LOG, (errmsg("pg_cron scheduler started")));

  /* set the desired log_min_messages */
  SetConfigOption("log_min_messages", cron_error_severity(CronLogMinMessages),
                    PGC_POSTMASTER, PGC_S_OVERRIDE);

  MemoryContextSwitchTo(CronLoopContext);
  bool amILeader = false;
  while (!got_sigterm)
  {
    List *taskList = NIL;
    TimestampTz currentTime = 0;

    AcceptInvalidationMessages();

    if (!CronJobCacheValid)
    {
      ereport(DEBUG2, (errmsg("Cron Table Changed!")));
      RefreshTaskHash();
    }

    if (CronReloadConfig)
    {
      /* set the desired log_min_messages */
      ProcessConfigFile(PGC_SIGHUP);
      SetConfigOption("log_min_messages", cron_error_severity(CronLogMinMessages),
                        PGC_POSTMASTER, PGC_S_OVERRIDE);
      CronReloadConfig = false;
    }

    taskList = CurrentTaskList();
    currentTime = GetCurrentTimestamp();

    // Currently, there are 2 possible reasons why we are signalled
    // that the job run table changed:
    // 1. This cron worker was assigned a job. We expect the job run
    // table to contain rows with status == 'starting' and
    // nodename == MyNodeName.
    // 2. If we are the leader, a cron worker (including ourselves) might
    // have completed their assigned task. We need to be informed of this
    // so that if necessary, we can launch another job run that has been
    // queued up. See StartAllPendingRuns for how this could happen
    if (CronJobRunTableChanged)
    {
      ereport(DEBUG2, (errmsg("Cron Run Table Changed!")));
      ProcessNewJobs();
      if (amILeader)
      {
        UpdateJobRunStatus(taskList);
      }
      CronJobRunTableChanged = false;
    }

    // TODO: perhaps we can avoid the dual meaning of CronJobRunTableChanged
    // flag being set by introducing a third kind of relation invalidation
    // message. If we can reserve a 3rd relation id that we invalidate,
    // we can use that to create another flag which can be individually set.
    // Note that because postgres processes normally ignore foreign
    // invalidation messages that come from different pids other than the
    // postgres process itself, other postgres processes shouldn't get
    // the new relation invalidation message.

    // Check periodically for a leader state change
    amILeader = CheckForLeaderStateChange(amILeader, currentTime, taskList);

    if (amILeader)
    {
      // Determine which workers are available and the current load
      UpdateKnownNodesAndWorkload(currentTime, false);
      // Check if any jobs are scheduled to run now
      StartAllPendingRuns(taskList, currentTime);
    }

    // Start and monitor tasks
    WaitForCronTasks(taskList);
    ManageCronTasks(taskList, currentTime);

    if (amILeader) {
      // If necessary deal with tasks that have not started / are
      // still running
      CheckTimedOutTasks(taskList, currentTime);
    }

    MemoryContextReset(CronLoopContext);
  }

  ereport(LOG, (errmsg("pg_cron scheduler shutting down")));

  proc_exit(0);
}


/*
 * UpdateKnownNodesAndWorkload periodically queries master for the set
 * of active tservers (by extension postgres cron workers) and queries
 * for the number of jobs assigned to each worker. force option
 * allows for an immediate refresh of known nodes and workload.
 */
static void
UpdateKnownNodesAndWorkload(TimestampTz currentTime, bool force)
{
  static TimestampTz lastQueryTime = 0;

  int secondsPassed = SecondsPassed(lastQueryTime, currentTime);
  if (!force && secondsPassed < ClusterStatusQueryPeriod)
  {
    /* wait for next query */
    return;
  }
  lastQueryTime = currentTime;
}

/*
 * StartPendingRuns goes through the list of tasks and kicks of
 * runs for tasks that should start, taking clock changes into
 * into consideration.
 */
static void
StartAllPendingRuns(List *taskList, TimestampTz currentTime)
{
  static TimestampTz lastMinute = 0;

  int minutesPassed = 0;
  ListCell *taskCell = NULL;
  ClockProgress clockProgress;

  if (!RebootJobsScheduled)
  {
    /* find jobs with @reboot as a schedule */
    foreach(taskCell, taskList)
    {
      CronTask *task = (CronTask *) lfirst(taskCell);
      CronJob *cronJob = GetCronJob(task->jobId);
      entry *schedule = &cronJob->schedule;

      if (schedule->flags & WHEN_REBOOT)
      {
        task->pendingRunCount += 1;
      }
    }

    RebootJobsScheduled = true;
  }

  if (lastMinute == 0)
  {
    lastMinute = TimestampMinuteStart(currentTime);
  }

  minutesPassed = MinutesPassed(lastMinute, currentTime);
  if (minutesPassed == 0)
  {
    /* wait for new minute */
    return;
  }

  /* use Vixie cron logic for clock jumps */
  if (minutesPassed > (3*MINUTE_COUNT))
  {
    /* clock jumped forward by more than 3 hours */
    clockProgress = CLOCK_CHANGE;
  }
  else if (minutesPassed > 5)
  {
    /* clock went forward by more than 5 minutes (DST?) */
    clockProgress = CLOCK_JUMP_FORWARD;
  }
  else if (minutesPassed > 0)
  {
    /* clock went forward by 1-5 minutes */
    clockProgress = CLOCK_PROGRESSED;
  }
  else if (minutesPassed > -(3*MINUTE_COUNT))
  {
    /* clock jumped backwards by less than 3 hours (DST?) */
    clockProgress = CLOCK_JUMP_BACKWARD;
  }
  else
  {
    /* clock jumped backwards 3 hours or more */
    clockProgress = CLOCK_CHANGE;
  }

  foreach(taskCell, taskList)
  {
    CronTask *task = (CronTask *) lfirst(taskCell);

    if (!task->isActive)
    {
      /*
       * The job has been unscheduled, so we should not schedule
       * new runs. The task will be safely removed on the next call
       * to ManageCronTask.
       */
      continue;
    }

    StartPendingRuns(task, clockProgress, lastMinute, currentTime);
  }

  /*
   * If the clock jump backwards then we avoid repeating the fixed-time
   * tasks by preserving the last minute from before the clock jump,
   * until the clock has caught up (clockProgress will be
   * CLOCK_JUMP_BACKWARD until then).
   */
  if (clockProgress != CLOCK_JUMP_BACKWARD)
  {
    lastMinute = TimestampMinuteStart(currentTime);
  }
}


/*
 * StartPendingRuns kicks off pending runs for a task if it
 * should start, taking clock changes into consideration.
 */
static void
StartPendingRuns(CronTask *task, ClockProgress clockProgress,
         TimestampTz lastMinute, TimestampTz currentTime)
{
  CronJob *cronJob = GetCronJob(task->jobId);
  entry *schedule = &cronJob->schedule;
  TimestampTz virtualTime = lastMinute;
  TimestampTz currentMinute = TimestampMinuteStart(currentTime);

  switch (clockProgress)
  {
    case CLOCK_PROGRESSED:
    {
      /*
       * case 1: minutesPassed is a small positive number
       * run jobs for each virtual minute until caught up.
       */

      do
      {
        virtualTime = TimestampTzPlusMilliseconds(virtualTime,
                              60*1000);

        if (ShouldRunTask(schedule, virtualTime, true, true))
        {
          task->pendingRunCount += 1;
        }
      }
      while (virtualTime < currentMinute);

      break;
    }

    case CLOCK_JUMP_FORWARD:
    {
      /*
       * case 2: minutesPassed is a medium-sized positive number,
       * for example because we went to DST run wildcard
       * jobs once, then run any fixed-time jobs that would
       * otherwise be skipped if we use up our minute
       * (possible, if there are a lot of jobs to run) go
       * around the loop again so that wildcard jobs have
       * a chance to run, and we do our housekeeping
       */

      /* run fixed-time jobs for each minute missed */
      do
      {
        virtualTime = TimestampTzPlusMilliseconds(virtualTime,
                              60*1000);

        if (ShouldRunTask(schedule, virtualTime, false, true))
        {
          task->pendingRunCount += 1;
        }

      } while (virtualTime < currentMinute);

      /* run wildcard jobs for current minute */
      if (ShouldRunTask(schedule, currentMinute, true, false))
      {
        task->pendingRunCount += 1;
      }

      break;
    }

    case CLOCK_JUMP_BACKWARD:
    {
      /*
       * case 3: timeDiff is a small or medium-sized
       * negative num, eg. because of DST ending just run
       * the wildcard jobs. The fixed-time jobs probably
       * have already run, and should not be repeated
       * virtual time does not change until we are caught up
       */

      if (ShouldRunTask(schedule, currentMinute, true, false))
      {
        task->pendingRunCount += 1;
      }

      break;
    }

    default:
    {
      /*
       * other: time has changed a *lot*, skip over any
       * intermediate fixed-time jobs and go back to
       * normal operation.
       */
      if (ShouldRunTask(schedule, currentMinute, true, true))
      {
        task->pendingRunCount += 1;
      }
    }
  }
}


/*
 * MinutesPassed returns the number of minutes between startTime and
 * stopTime rounded down to the closest integer.
 */
static int
MinutesPassed(TimestampTz startTime, TimestampTz stopTime)
{
  return SecondsPassed(startTime, stopTime) / 60;
}

/*
 * SecondsPassed returns the number of seconds between startTime and
 * stopTime rounded down to the closest integer.
 */
static int
SecondsPassed(TimestampTz startTime, TimestampTz stopTime)
{
  int microsPassed = 0;
  long secondsPassed = 0;

  TimestampDifference(startTime, stopTime,
            &secondsPassed, &microsPassed);

  return secondsPassed;
}


/*
 * TimestampMinuteEnd returns the timestamp at the start of the
 * current minute for the given time.
 */
static TimestampTz
TimestampMinuteStart(TimestampTz time)
{
  TimestampTz result = 0;

#ifdef HAVE_INT64_TIMESTAMP
  result = time - time % 60000000;
#else
  result = (long) time - (long) time % 60;
#endif

  return result;
}


/*
 * TimestampMinuteEnd returns the timestamp at the start of the
 * next minute from the given time.
 */
static TimestampTz
TimestampMinuteEnd(TimestampTz time)
{
  TimestampTz result = TimestampMinuteStart(time);

#ifdef HAVE_INT64_TIMESTAMP
  result += 60000000;
#else
  result += 60;
#endif

  return result;
}


/*
 * ShouldRunTask returns whether a job should run in the current
 * minute according to its schedule.
 */
static bool
ShouldRunTask(entry *schedule, TimestampTz currentTime, bool doWild,
        bool doNonWild)
{
  time_t currentTime_t = timestamptz_to_time_t(currentTime);
  struct tm *tm = gmtime(&currentTime_t);

  int minute = tm->tm_min -FIRST_MINUTE;
  int hour = tm->tm_hour -FIRST_HOUR;
  int dayOfMonth = tm->tm_mday -FIRST_DOM;
  int month = tm->tm_mon +1 -FIRST_MONTH;
  int dayOfWeek = tm->tm_wday -FIRST_DOW;

  if (bit_test(schedule->minute, minute) &&
      bit_test(schedule->hour, hour) &&
      bit_test(schedule->month, month) &&
      ( ((schedule->flags & DOM_STAR) || (schedule->flags & DOW_STAR))
        ? (bit_test(schedule->dow,dayOfWeek) && bit_test(schedule->dom,dayOfMonth))
        : (bit_test(schedule->dow,dayOfWeek) || bit_test(schedule->dom,dayOfMonth)))) {
    if ((doNonWild && !(schedule->flags & (MIN_STAR|HR_STAR)))
        || (doWild && (schedule->flags & (MIN_STAR|HR_STAR))))
    {
      return true;
    }
  }

  return false;
}


/*
 * WaitForCronTasks blocks waiting for any active task for at most
 * 1 second.
 */
static void
WaitForCronTasks(List *taskList)
{
  int taskCount = list_length(taskList);

  if (taskCount > 0)
  {
    PollForTasks(taskList);
  }
  else
  {
    WaitForLatch(MaxWait);
  }
}


/*
 * WaitForLatch waits for the given number of milliseconds unless a signal
 * is received or postmaster shuts down.
 */
static void
WaitForLatch(int timeoutMs)
{
  int rc = 0;
  int waitFlags = WL_LATCH_SET | WL_POSTMASTER_DEATH | WL_TIMEOUT;

  /* nothing to do, wait for new jobs */
#if (PG_VERSION_NUM >= 100000)
  rc = WaitLatch(MyLatch, waitFlags, timeoutMs, PG_WAIT_EXTENSION);
#else
  rc = WaitLatch(MyLatch, waitFlags, timeoutMs);
#endif

  ResetLatch(MyLatch);

  if (rc & WL_POSTMASTER_DEATH)
  {
    /* postmaster died and we should bail out immediately */
    proc_exit(1);
  }
}


/*
 * PollForTasks calls poll() for the sockets of all tasks. It checks for
 * read or write events based on the pollingStatus of the task.
 */
static void
PollForTasks(List *taskList)
{
  TimestampTz currentTime = 0;
  TimestampTz nextEventTime = 0;
  int pollTimeout = 0;
  long waitSeconds = 0;
  int waitMicros = 0;
  CronTask **polledTasks = NULL;
  struct pollfd *pollFDs = NULL;
  int pollResult = 0;

  int taskIndex = 0;
  int taskCount = list_length(taskList);
  int activeTaskCount = 0;
  ListCell *taskCell = NULL;

  polledTasks = (CronTask **) palloc0(taskCount * sizeof(CronTask *));
  pollFDs = (struct pollfd *) palloc0(taskCount * sizeof(struct pollfd));

  currentTime = GetCurrentTimestamp();

  /*
   * At the latest, wake up when the next minute starts.
   */
  nextEventTime = TimestampMinuteEnd(currentTime);

  foreach(taskCell, taskList)
  {
    CronTask *task = (CronTask *) lfirst(taskCell);
    PostgresPollingStatusType pollingStatus = task->pollingStatus;
    struct pollfd *pollFileDescriptor = &pollFDs[activeTaskCount];

    if (activeTaskCount >= MaxRunningTasks)
    {
      /* already polling the maximum number of tasks */
      break;
    }

    if (task->state == CRON_TASK_ERROR || task->state == CRON_TASK_DONE ||
      CanStartTask(task))
    {
      /* there is work to be done, don't wait */
      pfree(polledTasks);
      pfree(pollFDs);
      return;
    }

    if (task->state == CRON_TASK_WAITING && task->pendingRunCount == 0)
    {
      /* don't poll idle tasks */
      continue;
    }

    if (task->state == CRON_TASK_CONNECTING ||
      task->state == CRON_TASK_SENDING)
    {
      /*
       * We need to wake up when a timeout expires.
       * Take the minimum of nextEventTime and task->startDeadline.
       */
      if (TimestampDifferenceExceeds(task->startDeadline, nextEventTime, 0))
      {
        nextEventTime = task->startDeadline;
      }
    }

    /* we plan to poll this task */
    pollFileDescriptor = &pollFDs[activeTaskCount];
    polledTasks[activeTaskCount] = task;

    if (task->state == CRON_TASK_CONNECTING ||
      task->state == CRON_TASK_SENDING ||
      task->state == CRON_TASK_BGW_RUNNING ||
      task->state == CRON_TASK_RUNNING)
    {
      PGconn *connection = task->connection;
      int pollEventMask = 0;

      /*
       * Set the appropriate mask for poll, based on the current polling
       * status of the task, controlled by ManageCronTask.
       */

      if (pollingStatus == PGRES_POLLING_READING)
      {
        pollEventMask = POLLERR | POLLIN;
      }
      else if (pollingStatus == PGRES_POLLING_WRITING)
      {
        pollEventMask = POLLERR | POLLOUT;
      }

      pollFileDescriptor->fd = PQsocket(connection);
      pollFileDescriptor->events = pollEventMask;
    }
    else
    {
      /*
       * Task is not running.
       */

      pollFileDescriptor->fd = -1;
      pollFileDescriptor->events = 0;
    }

    pollFileDescriptor->revents = 0;

    activeTaskCount++;
  }

  /*
   * Find the first time-based event, which is either the start of a new
   * minute or a timeout.
   */
  TimestampDifference(currentTime, nextEventTime, &waitSeconds, &waitMicros);

  pollTimeout = waitSeconds * 1000 + waitMicros / 1000;
  if (pollTimeout <= 0)
  {
    pfree(polledTasks);
    pfree(pollFDs);
    return;
  }
  else if (pollTimeout > MaxWait)
  {
    /*
     * We never wait more than 1 second, this gives us a chance to react
     * to external events like a TERM signal and job changes.
     */

    pollTimeout = MaxWait;
  }

  if (activeTaskCount == 0)
  {
    /* turns out there's nothing to do, just wait for something to happen */
    WaitForLatch(pollTimeout);

    pfree(polledTasks);
    pfree(pollFDs);
    return;
  }

  pollResult = poll(pollFDs, activeTaskCount, pollTimeout);
  if (pollResult < 0)
  {
    /*
     * This typically happens in case of a signal, though we should
     * probably check errno in case something bad happened.
     */

    pfree(polledTasks);
    pfree(pollFDs);
    return;
  }

  for (taskIndex = 0; taskIndex < activeTaskCount; taskIndex++)
  {
    CronTask *task = polledTasks[taskIndex];
    struct pollfd *pollFileDescriptor = &pollFDs[taskIndex];

    task->isSocketReady = pollFileDescriptor->revents &
                pollFileDescriptor->events;
  }

  pfree(polledTasks);
  pfree(pollFDs);
}


/*
 * CanStartTask determines whether a task is ready to be started because
 * it has pending runs and we are running less than MaxRunningTasks.
 */
static bool
CanStartTask(CronTask *task)
{
  return task->state == CRON_TASK_WAITING && task->pendingRunCount > 0 &&
       RunningTaskCount < MaxRunningTasks;
}


/*
 * ManageCronTasks proceeds the state machines of the given list of tasks.
 */
static void
ManageCronTasks(List *taskList, TimestampTz currentTime)
{
  ListCell *taskCell = NULL;

  // Manage tasks with designated nodes first to load balance correctly.
  foreach(taskCell, taskList)
  {
    CronTask *task = (CronTask *) lfirst(taskCell);
    int64 jobId = task->jobId;
    CronJob *cronJob = GetCronJob(jobId);
    if (cronJob != NULL && cronJob->nodeName != NULL)
    {
      ManageCronTask(task, currentTime);
    }
  }
  foreach(taskCell, taskList)
  {
    CronTask *task = (CronTask *) lfirst(taskCell);
    int64 jobId = task->jobId;
    CronJob *cronJob = GetCronJob(jobId);
    if (cronJob != NULL && cronJob->nodeName == NULL)
    {
      ManageCronTask(task, currentTime);
    }
  }
}


/*
 * ManageCronTask implements the cron task state machine.
 */
static void
ManageCronTask(CronTask *task, TimestampTz currentTime)
{
  CronTaskState checkState = task->state;
  int64 jobId = task->jobId;
  CronJob *cronJob = GetCronJob(jobId);
  PGconn *connection = task->connection;
  ConnStatusType connectionStatus = CONNECTION_BAD;
  TimestampTz start_time;

  switch (checkState)
  {
    case CRON_TASK_WAITING:
    {
      /* check if job has been removed */
      if (!task->isActive)
      {
        /* remove task as well */
        RemoveTask(jobId);
        break;
      }

      if (!CanStartTask(task))
      {
        break;
      }

      task->pendingRunCount -= 1;

      ManageCronTaskWaiting(task, cronJob);
      break;

      /////////////////////////////////

      if (UseBackgroundWorkers)
        task->state = CRON_TASK_BGW_START;
      else
        task->state = CRON_TASK_START;

      RunningTaskCount++;

      /* Add new entry to audit table. */
      task->runId = NextRunId();
      if (CronLogRun)
        InsertJobRunDetail(task->runId, &cronJob->jobId,
                    cronJob->database,
                    cronJob->userName,
                    cronJob->command, GetCronStatus(CRON_STATUS_STARTING),
                    NULL, NULL, NULL, NULL);
    }

    case CRON_TASK_REMOTE_START:
      Assert(!task->runningLocal);
      break;

    case CRON_TASK_REMOTE_RUNNING:
      Assert(!task->runningLocal);
      break;

    case CRON_TASK_START:
    {
      /* as there is no break at the end of the previous case
       * to not add an extra second, then do another check here
       */
      if (!UseBackgroundWorkers)
      {
        const char *clientEncoding = GetDatabaseEncodingName();
        char nodePortString[12];
        TimestampTz startDeadline = 0;

        const char *keywordArray[] = {
          "host",
          "port",
          "fallback_application_name",
          "client_encoding",
          "dbname",
          "user",
          NULL
          };
        const char *valueArray[] = {
          cronJob->nodeName,
          nodePortString,
          "pg_cron",
          clientEncoding,
          cronJob->database,
          cronJob->userName,
          NULL
        };
        sprintf(nodePortString, "%d", cronJob->nodePort);

        Assert(sizeof(keywordArray) == sizeof(valueArray));

        if (CronLogStatement)
        {
          char *command = cronJob->command;

          ereport(LOG, (errmsg("cron job " INT64_FORMAT " %s: %s",
                   jobId, GetCronStatus(CRON_STATUS_STARTING), command)));
        }

        connection = PQconnectStartParams(keywordArray, valueArray, false);
        PQsetnonblocking(connection, 1);

        connectionStatus = PQstatus(connection);
        if (connectionStatus == CONNECTION_BAD)
        {
          /* make sure we call PQfinish on the connection */
          task->connection = connection;

          task->errorMessage = "connection failed";
          task->pollingStatus = 0;
          task->state = CRON_TASK_ERROR;
          break;
        }

        startDeadline = TimestampTzPlusMilliseconds(currentTime,
                      CronTaskStartTimeout);

        task->startDeadline = startDeadline;
        task->connection = connection;
        task->pollingStatus = PGRES_POLLING_WRITING;
        task->state = CRON_TASK_CONNECTING;

        if (CronLogRun)
          UpdateJobRunDetail(task->runId, NULL, GetCronStatus(CRON_STATUS_CONNECTING), NULL, NULL, NULL);

        break;
      }
    }

    case CRON_TASK_BGW_START:
    {
      Assert(task->runningLocal);
      Assert(task->runId != 0);
      BackgroundWorker worker;
      pid_t pid;
      shm_toc_estimator e;
      shm_toc *toc;
      char *database;
      char *username;
      char *command;
      MemoryContext oldcontext;
      shm_mq *mq;
      Size segsize;
      BackgroundWorkerHandle *handle;
      BgwHandleStatus status;
      bool registered;
      TimestampTz startDeadline = 0;

      /* break in the previous case has not been reached
       * checking just for extra precaution
       */
      Assert(UseBackgroundWorkers);
      #if PG_VERSION_NUM < 100000
        Assert(CurrentResourceOwner == NULL);
        CurrentResourceOwner = ResourceOwnerCreate(NULL, "pg_cron_worker");
      #endif

      #define QUEUE_SIZE ((Size) 65536)

      /*
       * Create the shared memory that we will pass to the background
       * worker process.  We use DSM_CREATE_NULL_IF_MAXSEGMENTS so that we
       * do not ERROR here.  This way, we can mark the job as failed and
       * keep the launcher process running normally.
       */
      shm_toc_initialize_estimator(&e);
      shm_toc_estimate_chunk(&e, strlen(cronJob->database) + 1);
      shm_toc_estimate_chunk(&e, strlen(cronJob->userName) + 1);
      shm_toc_estimate_chunk(&e, strlen(cronJob->command) + 1);
      shm_toc_estimate_chunk(&e, QUEUE_SIZE);
      shm_toc_estimate_keys(&e, PG_CRON_NKEYS);
      segsize = shm_toc_estimate(&e);

      task->seg = dsm_create(segsize, DSM_CREATE_NULL_IF_MAXSEGMENTS);
      if (task->seg == NULL)
      {
        task->state = CRON_TASK_ERROR;
        task->errorMessage = "unable to create a DSM segment; more "
                "details may be available in the server log";

        ereport(WARNING,
          (errmsg("max number of DSM segments may has been reached")));

        break;
      }

      toc = shm_toc_create(PG_CRON_MAGIC, dsm_segment_address(task->seg), segsize);

      database = shm_toc_allocate(toc, strlen(cronJob->database) + 1);
      strcpy(database, cronJob->database);
      shm_toc_insert(toc, PG_CRON_KEY_DATABASE, database);

      username = shm_toc_allocate(toc, strlen(cronJob->userName) + 1);
      strcpy(username, cronJob->userName);
      shm_toc_insert(toc, PG_CRON_KEY_USERNAME, username);

      command = shm_toc_allocate(toc, strlen(cronJob->command) + 1);
      strcpy(command, cronJob->command);
      shm_toc_insert(toc, PG_CRON_KEY_COMMAND, command);

      mq = shm_mq_create(shm_toc_allocate(toc, QUEUE_SIZE), QUEUE_SIZE);
      shm_toc_insert(toc, PG_CRON_KEY_QUEUE, mq);
      shm_mq_set_receiver(mq, MyProc);

      /*
       * Attach the queue before launching a worker, so that we'll automatically
       * detach the queue if we error out.  (Otherwise, the worker might sit
       * there trying to write the queue long after we've gone away.)
       */
      oldcontext = MemoryContextSwitchTo(TopMemoryContext);
      shm_mq_attach(mq, task->seg, NULL);
      MemoryContextSwitchTo(oldcontext);

      /*
       * Prepare the background worker.
       *
       */
      memset(&worker, 0, sizeof(BackgroundWorker));
      worker.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
      worker.bgw_start_time = BgWorkerStart_ConsistentState;
      worker.bgw_restart_time = BGW_NEVER_RESTART;
      sprintf(worker.bgw_library_name, "pg_cron");
      sprintf(worker.bgw_function_name, "CronBackgroundWorker");
#if (PG_VERSION_NUM >= 110000)
      snprintf(worker.bgw_type, BGW_MAXLEN, "pg_cron");
#endif
      snprintf(worker.bgw_name, BGW_MAXLEN, "pg_cron worker");
      worker.bgw_main_arg = UInt32GetDatum(dsm_segment_handle(task->seg));
      worker.bgw_notify_pid = MyProcPid;

      /*
       * Start the worker process.
       */
      if (CronLogStatement)
      {
        ereport(LOG, (errmsg("cron job " INT64_FORMAT " %s: %s",
                     jobId, GetCronStatus(CRON_STATUS_STARTING), command)));
      }

      // Update cron job status first so that if this node goes down,
      // and this job run's status is "starting",
      // the cron leader can be certain that they can safely reassign
      // this job run to someone else.
      // This covers the case where a bg worker spawns and completes the
      // job and then this node goes down before the status is
      // updated to "running" / "complete" / "failed"
      start_time = GetCurrentTimestamp();
      if (CronLogRun)
        UpdateJobRunDetail(task->runId, NULL, GetCronStatus(CRON_STATUS_RUNNING), NULL, &start_time, NULL);

      /* If no no background worker slots are currently available
       * let's try until we reach jobStartupTimeout
       */
      startDeadline = TimestampTzPlusMilliseconds(currentTime,
                    CronTaskStartTimeout);
      task->startDeadline = startDeadline;
      do
      {
        registered = RegisterDynamicBackgroundWorker(&worker, &handle);
      }
      while (!registered && !jobStartupTimeout(task, GetCurrentTimestamp()));

      if (!registered)
      {
        dsm_detach(task->seg);
        task->seg = NULL;
        task->state = CRON_TASK_ERROR;
        task->errorMessage = "could not start background process; more "
                   "details may be available in the server log";
        ereport(WARNING,
          (errcode(ERRCODE_CONFIGURATION_LIMIT_EXCEEDED),
          errmsg("out of background worker slots"),
          errhint("You might need to increase max_worker_processes.")));
        break;
      }

      task->startDeadline = 0;
      task->handle = *handle;
      status = WaitForBackgroundWorkerStartup(&task->handle, &pid);
      if (status != BGWH_STARTED && status != BGWH_STOPPED)
      {
        dsm_detach(task->seg);
        task->seg = NULL;
        task->state = CRON_TASK_ERROR;
        task->errorMessage = "could not start background process; more "
                   "details may be available in the server log";
        break;
      }

      start_time = GetCurrentTimestamp();

      // Now update the pid once the bgworker has been created
      if (CronLogRun)
        UpdateJobRunDetail(task->runId, &pid, NULL, NULL, NULL, NULL);

      task->state = CRON_TASK_BGW_RUNNING;
      break;
    }

    case CRON_TASK_CONNECTING:
    {
      PostgresPollingStatusType pollingStatus = 0;

      Assert(!UseBackgroundWorkers);

      /* check if job has been removed */
      if (jobCanceled(task))
        break;

      /* check if timeout has been reached */
      if (jobStartupTimeout(task, currentTime))
        break;

      /* check if connection is still alive */
      connectionStatus = PQstatus(connection);
      if (connectionStatus == CONNECTION_BAD)
      {
        task->errorMessage = "connection failed";
        task->pollingStatus = 0;
        task->state = CRON_TASK_ERROR;
        break;
      }

      /* check if socket is ready to send */
      if (!task->isSocketReady)
      {
        break;
      }

      /* check whether a connection has been established */
      pollingStatus = PQconnectPoll(connection);
      if (pollingStatus == PGRES_POLLING_OK)
      {
        pid_t pid;
        /* wait for socket to be ready to send a query */
        task->pollingStatus = PGRES_POLLING_WRITING;

        task->state = CRON_TASK_SENDING;

        pid = (pid_t) PQbackendPID(connection);
        if (CronLogRun)
          UpdateJobRunDetail(task->runId, &pid, GetCronStatus(CRON_STATUS_SENDING), NULL, NULL, NULL);
      }
      else if (pollingStatus == PGRES_POLLING_FAILED)
      {
        task->errorMessage = "connection failed";
        task->pollingStatus = 0;
        task->state = CRON_TASK_ERROR;
      }
      else
      {
        /*
         * Connection is still being established.
         *
         * On the next WaitForTasks round, we wait for reading or writing
         * based on the status returned by PQconnectPoll, see:
         * https://www.postgresql.org/docs/9.5/static/libpq-connect.html
         */
        task->pollingStatus = pollingStatus;
      }

      break;
    }

    case CRON_TASK_SENDING:
    {
      char *command = cronJob->command;
      int sendResult = 0;

      Assert(!UseBackgroundWorkers);

      /* check if job has been removed */
      if (jobCanceled(task))
        break;

      /* check if timeout has been reached */
      if (jobStartupTimeout(task, currentTime))
        break;

      /* check if socket is ready to send */
      if (!task->isSocketReady)
      {
        break;
      }

      /* check if connection is still alive */
      connectionStatus = PQstatus(connection);
      if (connectionStatus == CONNECTION_BAD)
      {
        task->errorMessage = "connection lost";
        task->pollingStatus = 0;
        task->state = CRON_TASK_ERROR;
        break;
      }

      sendResult = PQsendQuery(connection, command);
      if (sendResult == 1)
      {
        /* wait for socket to be ready to receive results */
        task->pollingStatus = PGRES_POLLING_READING;

        /* command is underway, stop using timeout */
        task->startDeadline = 0;
        task->state = CRON_TASK_RUNNING;

        start_time = GetCurrentTimestamp();
        if (CronLogRun)
          UpdateJobRunDetail(task->runId, NULL, GetCronStatus(CRON_STATUS_RUNNING), NULL, &start_time, NULL);
      }
      else
      {
        /* not yet ready to send */
      }

      break;
    }

    case CRON_TASK_RUNNING:
    {
      int connectionBusy = 0;
      PGresult *result = NULL;
      Assert(!UseBackgroundWorkers);

      /* check if job has been removed */
      if (jobCanceled(task))
        break;

      /* check if connection is still alive */
      connectionStatus = PQstatus(connection);
      if (connectionStatus == CONNECTION_BAD)
      {
        task->errorMessage = "connection lost";
        task->pollingStatus = 0;
        task->state = CRON_TASK_ERROR;
        break;
      }

      /* check if socket is ready to send */
      if (!task->isSocketReady)
      {
        break;
      }

      PQconsumeInput(connection);

      connectionBusy = PQisBusy(connection);
      if (connectionBusy)
      {
        /* still waiting for results */
        break;
      }

      while ((result = PQgetResult(connection)) != NULL)
      {
        GetTaskFeedback(result, task);
      }

      PQfinish(connection);

      task->connection = NULL;
      task->pollingStatus = 0;
      task->isSocketReady = false;

      task->state = CRON_TASK_DONE;
      if(!IsYugaByteEnabled())
      {
        // Workers don't manage their number of running tasks
        // Only the leader is concerned of this.
        RunningTaskCount--;
      }
      break;
    }

    case CRON_TASK_BGW_RUNNING:
    {
      Assert(task->runningLocal);
      Assert(task->runId != 0);
      pid_t pid;
      shm_mq_handle *responseq;
      shm_mq *mq;
      shm_toc *toc;

      Assert(UseBackgroundWorkers);
      /* check if job has been removed */
      if (jobCanceled(task))
      {
        ereport(DEBUG1,(errmsg("job cancelled " INT64_FORMAT " run " INT64_FORMAT, task->jobId, task->runId)));
        TerminateBackgroundWorker(&task->handle);
        WaitForBackgroundWorkerShutdown(&task->handle);
        dsm_detach(task->seg);
        task->seg = NULL;

        break;
      }

      toc = shm_toc_attach(PG_CRON_MAGIC, dsm_segment_address(task->seg));
      #if PG_VERSION_NUM < 100000
        mq = shm_toc_lookup(toc, PG_CRON_KEY_QUEUE);
      #else
        mq = shm_toc_lookup(toc, PG_CRON_KEY_QUEUE, false);
      #endif
      responseq = shm_mq_attach(mq, task->seg, NULL);

      /* still waiting for job to complete */
      if (GetBackgroundWorkerPid(&task->handle, &pid) != BGWH_STOPPED)
      {
        GetBgwTaskFeedback(responseq, task, true);
        shm_mq_detach(responseq);
        break;
      }

      GetBgwTaskFeedback(responseq, task, false);

      task->state = CRON_TASK_DONE;
      dsm_detach(task->seg);
      task->seg = NULL;
      if (!IsYugaByteEnabled())
      {
        // Workers don't manage their number of running tasks
        // Only the leader is concerned of this.
        RunningTaskCount--;
      }

      break;
    }

    case CRON_TASK_ERROR:
    {
      Assert(task->runningLocal);
      Assert(task->runId != 0);
      if (connection != NULL)
      {
        PQfinish(connection);
        task->connection = NULL;
      }

      if (!task->isActive)
      {
        RemoveTask(jobId);
      }

      if (task->errorMessage != NULL)
      {
        if (CronLogRun)
          UpdateJobRunDetail(task->runId, NULL, GetCronStatus(CRON_STATUS_FAILED), task->errorMessage, NULL, NULL);

        ereport(LOG, (errmsg("cron job " INT64_FORMAT " %s",
                   jobId, task->errorMessage)));


        if (task->freeErrorMessage)
        {
          free(task->errorMessage);
        }
      }
      else
      {
        ereport(LOG, (errmsg("cron job " INT64_FORMAT " %s", jobId, GetCronStatus(CRON_STATUS_FAILED))));
      }

      task->startDeadline = 0;
      task->isSocketReady = false;
      task->state = CRON_TASK_DONE;

      if (!IsYugaByteEnabled())
      {
        // Workers don't manage their number of running tasks
        // Only the leader is concerned of this.
        RunningTaskCount--;
      }

      /* fall through to CRON_TASK_DONE */
    }

    case CRON_TASK_DONE:
    default:
    {
      ResetTask(task);
    }
  }
}

/*
 * ManageCronTaskWaiting handles jobs who are ready to be executed
 */
static void
ManageCronTaskWaiting(CronTask *task, const CronJob *cronJob)
{
  ereport(DEBUG1, (errmsg("job " INT64_FORMAT " runCount %d",
             task->jobId, task->pendingRunCount)));
  const char* nodename = cronJob->nodeName;
  task->runId = NextRunId();
  if (nodename)
  {
    // Node that a job is designated for is currently unavailable
    // Mark this job run as a failure
    TimestampTz currTime = GetCurrentTimestamp();
    InsertJobRunDetail(task->runId, &cronJob->jobId,
      cronJob->database,
      cronJob->userName,
      cronJob->command, GetCronStatus(CRON_STATUS_FAILED),
      nodename,
      "Node was not available during assignment.",
      &currTime,
      &currTime);
    task->state = CRON_TASK_DONE;
  } else {
    // Job run can be executed on any node or node that a job is designated
    // for exists. Assign this job run
    Assert(nodename != NULL);
    InsertJobRunDetail(task->runId, &cronJob->jobId,
      cronJob->database,
      cronJob->userName,
      cronJob->command, GetCronStatus(CRON_STATUS_STARTING),
      nodename, NULL, NULL, NULL);
    // We always consider all jobs (including job runs assigned to ourselves)
    // to initially be remotely monitored (job is not executed by this
    // process's child process). Once we have picked up the
    // JobRunTableChangeCallback signal, runningLocal will switch to true
    // and the state will move to CRON_TASK_START. See ProcessNewJobs
    Assert(!task->runningLocal);
    task->state = CRON_TASK_REMOTE_START;
  }
}

/*
 * ManageCronTask implements the cron task state machine.
 */
static void
CheckTimedOutTasks(List *taskList, TimestampTz currentTime)
{
  // Check for tasks that have not completed once per minute
  TimestampTz lastMinute = TimestampMinuteStart(currentTime);
  static TimestampTz lastQueryMinute = 0;
  if (lastQueryMinute == 0)
  {
    lastQueryMinute = lastMinute;
  }
  if (lastQueryMinute == lastMinute)
  {
    // Wait for next minute
    return;
  }

  int secondsPassed = SecondsPassed(lastMinute, currentTime);
  // CronTaskStartTimeout accounts for the time a worker might
  // spend waiting for a bg worker slot to free up
  // MaxJobCompletionDelay accounts for the time the worker spends updating
  // the job run status
  if (secondsPassed < CronTaskStartTimeout / 1000 + MaxJobCompletionDelay)
  {
    // Allow some time before we take action for job runs
    // that have not completed
    return;
  }
  lastQueryMinute = lastMinute;

  // Update status of job runs that have not completed
  UpdateJobRunStatus(taskList);

  ListCell *taskCell = NULL;

  foreach(taskCell, taskList)
  {
    CronTask *task = (CronTask *) lfirst(taskCell);
    // Ignore local worker status as it will naturally move towards
    // CRON_TASK_DONE
    if (!task->runningLocal)
    {
      if(task->state == CRON_TASK_REMOTE_START)
      {
        ereport(WARNING, (errmsg("job " INT64_FORMAT " run "
            INT64_FORMAT " has not started!",
            task->jobId, task->runId)));
      }
      else if(task->state == CRON_TASK_REMOTE_RUNNING)
      {
        ereport(WARNING, (errmsg("job " INT64_FORMAT " run "
            INT64_FORMAT " is still running!",
            task->jobId, task->runId)));
      }
    }
  }
}

static void
GetTaskFeedback(PGresult *result, CronTask *task)
{

  TimestampTz end_time;
  ExecStatusType executionStatus;

  end_time = GetCurrentTimestamp();
  executionStatus = PQresultStatus(result);

  switch (executionStatus)
  {
    case PGRES_COMMAND_OK:
    {
      char *cmdStatus = PQcmdStatus(result);
      char *cmdTuples = PQcmdTuples(result);

      if (CronLogRun)
        UpdateJobRunDetail(task->runId, NULL, GetCronStatus(CRON_STATUS_SUCCEEDED), cmdStatus, NULL, &end_time);

      if (CronLogStatement)
      {
        ereport(LOG, (errmsg("cron job " INT64_FORMAT " COMMAND completed: %s %s",
                   task->jobId, cmdStatus, cmdTuples)));
      }

      break;
    }

    case PGRES_BAD_RESPONSE:
    case PGRES_FATAL_ERROR:
    {
      task->errorMessage = strdup(PQresultErrorMessage(result));
      task->freeErrorMessage = true;
      task->pollingStatus = 0;
      task->state = CRON_TASK_ERROR;

      if (CronLogRun)
        UpdateJobRunDetail(task->runId, NULL, GetCronStatus(CRON_STATUS_FAILED), task->errorMessage, NULL, &end_time);

      PQclear(result);

      return;
    }

    case PGRES_COPY_IN:
    case PGRES_COPY_OUT:
    case PGRES_COPY_BOTH:
    {
      /* cannot handle COPY input/output */
      task->errorMessage = "COPY not supported";
      task->pollingStatus = 0;
      task->state = CRON_TASK_ERROR;

      if (CronLogRun)
        UpdateJobRunDetail(task->runId, NULL, GetCronStatus(CRON_STATUS_FAILED), task->errorMessage, NULL, &end_time);

      PQclear(result);

      return;
    }

    case PGRES_TUPLES_OK:
    case PGRES_EMPTY_QUERY:
    case PGRES_SINGLE_TUPLE:
    case PGRES_NONFATAL_ERROR:
    default:
    {
      int tupleCount = PQntuples(result);
      char *rowString = ngettext("row", "rows",
                       tupleCount);
      char  rows[MAXINT8LEN + 1];
      char  outputrows[MAXINT8LEN + 4 + 1];

      pg_lltoa(tupleCount, rows);
      snprintf(outputrows, sizeof(outputrows), "%s %s", rows, rowString);

      if (CronLogRun)
        UpdateJobRunDetail(task->runId, NULL, GetCronStatus(CRON_STATUS_SUCCEEDED), outputrows, NULL, &end_time);

      if (CronLogStatement)
      {
        ereport(LOG, (errmsg("cron job " INT64_FORMAT " completed: "
                   "%d %s",
                   task->jobId, tupleCount,
                   rowString)));
      }

      break;
    }

  }

  PQclear(result);
}

static void
GetBgwTaskFeedback(shm_mq_handle *responseq, CronTask *task, bool running)
{

  TimestampTz end_time;

  Size            nbytes;
  void       *data;
  char            msgtype;
  StringInfoData  msg;
  shm_mq_result res;

  end_time = GetCurrentTimestamp();
  /*
   * Message-parsing routines operate on a null-terminated StringInfo,
   * so we must construct one.
   */
  for (;;)
  {
    /* Get next message. */
    res = shm_mq_receive(responseq, &nbytes, &data, false);

    if (res != SHM_MQ_SUCCESS)
      break;
    initStringInfo(&msg);
    resetStringInfo(&msg);
    enlargeStringInfo(&msg, nbytes);
    msg.len = nbytes;
    memcpy(msg.data, data, nbytes);
    msg.data[nbytes] = '\0';
    msgtype = pq_getmsgbyte(&msg);
    switch (msgtype)
    {
      case 'N':
      case 'E':
        {
          ErrorData  edata;
          StringInfoData  display_msg;

          pq_parse_errornotice(&msg, &edata);
          initStringInfo(&display_msg);
          bgw_generate_returned_message(&display_msg, edata);

          if (CronLogRun)
          {

            if (edata.elevel >= ERROR)
              UpdateJobRunDetail(task->runId, NULL, GetCronStatus(CRON_STATUS_FAILED), display_msg.data, NULL, &end_time);
            else if (running)
              UpdateJobRunDetail(task->runId, NULL, NULL, display_msg.data, NULL, NULL);
            else
              UpdateJobRunDetail(task->runId, NULL, GetCronStatus(CRON_STATUS_SUCCEEDED), display_msg.data, NULL, &end_time);
          }

          ereport(LOG, (errmsg("cron job " INT64_FORMAT ": %s",
                   task->jobId, display_msg.data)));
          pfree(display_msg.data);

          break;
        }
      case 'T':
          break;
      case 'C':
        {
          const char  *tag = pq_getmsgstring(&msg);
          char *nonconst_tag;
          char *cmdTuples;

          nonconst_tag = strdup(tag);

          if (CronLogRun)
            UpdateJobRunDetail(task->runId, NULL, GetCronStatus(CRON_STATUS_SUCCEEDED), nonconst_tag, NULL, &end_time);

          if (CronLogStatement) {
            cmdTuples = pg_cron_cmdTuples(nonconst_tag);
            ereport(LOG, (errmsg("cron job " INT64_FORMAT " COMMAND completed: %s %s",
                       task->jobId, nonconst_tag, cmdTuples)));
          }

          free(nonconst_tag);
          break;
        }
      case 'A':
      case 'D':
      case 'G':
      case 'H':
      case 'W':
      case 'Z':
          break;
      default:
          elog(WARNING, "unknown message type: %c (%zu bytes)",
             msg.data[0], nbytes);
          break;
    }
    pfree(msg.data);
  }
}

/*
 * Background worker logic.
 */
void
CronBackgroundWorker(Datum main_arg)
{
  dsm_segment *seg;
  shm_toc *toc;
  char *database;
  char *username;
  char *command;
  shm_mq *mq;
  shm_mq_handle *responseq;

  pqsignal(SIGTERM, pg_cron_background_worker_sigterm);
  BackgroundWorkerUnblockSignals();

  /* Set up a memory context and resource owner. */
  Assert(CurrentResourceOwner == NULL);
  CurrentResourceOwner = ResourceOwnerCreate(NULL, "pg_cron");
  CurrentMemoryContext = AllocSetContextCreate(TopMemoryContext,
                         "pg_cron worker",
                         ALLOCSET_DEFAULT_MINSIZE,
                         ALLOCSET_DEFAULT_INITSIZE,
                         ALLOCSET_DEFAULT_MAXSIZE);

  /* Set up a dynamic shared memory segment. */
  seg = dsm_attach(DatumGetInt32(main_arg));
  if (seg == NULL)
    ereport(ERROR,
        (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
         errmsg("unable to map dynamic shared memory segment")));
  toc = shm_toc_attach(PG_CRON_MAGIC, dsm_segment_address(seg));
  if (toc == NULL)
    ereport(ERROR,
        (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
         errmsg("bad magic number in dynamic shared memory segment")));

  #if PG_VERSION_NUM < 100000
    database = shm_toc_lookup(toc, PG_CRON_KEY_DATABASE);
    username = shm_toc_lookup(toc, PG_CRON_KEY_USERNAME);
    command = shm_toc_lookup(toc, PG_CRON_KEY_COMMAND);
    mq = shm_toc_lookup(toc, PG_CRON_KEY_QUEUE);
  #else
    database = shm_toc_lookup(toc, PG_CRON_KEY_DATABASE, false);
    username = shm_toc_lookup(toc, PG_CRON_KEY_USERNAME, false);
    command = shm_toc_lookup(toc, PG_CRON_KEY_COMMAND, false);
    mq = shm_toc_lookup(toc, PG_CRON_KEY_QUEUE, false);
  #endif

  shm_mq_set_sender(mq, MyProc);
  responseq = shm_mq_attach(mq, seg, NULL);
  pq_redirect_to_shm_mq(seg, responseq);

#if (PG_VERSION_NUM < 110000)
  BackgroundWorkerInitializeConnection(database, username);
#else
  BackgroundWorkerInitializeConnection(database, username, 0);
#endif

  /* Prepare to execute the query. */
  SetCurrentStatementStartTimestamp();
  debug_query_string = command;
  pgstat_report_activity(STATE_RUNNING, command);
  StartTransactionCommand();
  if (StatementTimeout > 0)
    enable_timeout_after(STATEMENT_TIMEOUT, StatementTimeout);
  else
    disable_timeout(STATEMENT_TIMEOUT, false);

  /* Execute the query. */
  ExecuteSqlString(command);

  /* Post-execution cleanup. */
  disable_timeout(STATEMENT_TIMEOUT, false);
  CommitTransactionCommand();
  pgstat_report_activity(STATE_IDLE, command);
  pgstat_report_stat(true);

  /* Signal that we are done. */
  ReadyForQuery(DestRemote);

  dsm_detach(seg);
  proc_exit(0);
}

/*
 * Execute given SQL string without SPI or a libpq session.
 */
static void
ExecuteSqlString(const char *sql)
{
  List *raw_parsetree_list;
  ListCell *lc1;
  bool isTopLevel;
  int commands_remaining;
  MemoryContext parsecontext;
  MemoryContext oldcontext;

  /*
   * Parse the SQL string into a list of raw parse trees.
   *
   * Because we allow statements that perform internal transaction control,
   * we can't do this in TopTransactionContext; the parse trees might get
   * blown away before we're done executing them.
   */
  parsecontext = AllocSetContextCreate(TopMemoryContext,
                     "pg_cron parse/plan",
                     ALLOCSET_DEFAULT_MINSIZE,
                     ALLOCSET_DEFAULT_INITSIZE,
                     ALLOCSET_DEFAULT_MAXSIZE);
  oldcontext = MemoryContextSwitchTo(parsecontext);
  raw_parsetree_list = pg_parse_query(sql);
  commands_remaining = list_length(raw_parsetree_list);
  isTopLevel = commands_remaining == 1;
  MemoryContextSwitchTo(oldcontext);

  /*
   * Do parse analysis, rule rewrite, planning, and execution for each raw
   * parsetree.  We must fully execute each query before beginning parse
   * analysis on the next one, since there may be interdependencies.
   */
  foreach(lc1, raw_parsetree_list)
  {
    #if PG_VERSION_NUM < 100000
      Node *parsetree = (Node *) lfirst(lc1);
    #else
      RawStmt *parsetree = (RawStmt *)  lfirst(lc1);
    #endif

    #if PG_VERSION_NUM < 130000
      const char *commandTag;
      char completionTag[COMPLETION_TAG_BUFSIZE];
    #else
      CommandTag commandTag;
      QueryCompletion qc;
    #endif

    List *querytree_list;
    List *plantree_list;
    bool snapshot_set = false;
    Portal portal;
    DestReceiver *receiver;
    int16 format = 1;

    /*
     * We don't allow transaction-control commands like COMMIT and ABORT
     * here.  The entire SQL statement is executed as a single transaction
     * which commits if no errors are encountered.
     */
    if (IsA(parsetree, TransactionStmt))
      ereport(ERROR,
          (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
           errmsg("transaction control statements are not allowed in pg_cron")));

    /*
     * Get the command name for use in status display (it also becomes the
     * default completion tag, down inside PortalRun).  Set ps_status and
     * do any special start-of-SQL-command processing needed by the
     * destination.
     */
    #if PG_VERSION_NUM < 100000
      commandTag = CreateCommandTag(parsetree);
    #else
      commandTag = CreateCommandTag(parsetree->stmt);
    #endif


    #if PG_VERSION_NUM < 130000
      set_ps_display(commandTag, false);
    #else
      set_ps_display(GetCommandTagName(commandTag));
    #endif

    BeginCommand(commandTag, DestNone);

    /* Set up a snapshot if parse analysis/planning will need one. */
    if (analyze_requires_snapshot(parsetree))
    {
      PushActiveSnapshot(GetTransactionSnapshot());
      snapshot_set = true;
    }

    /*
     * OK to analyze, rewrite, and plan this query.
     *
     * As with parsing, we need to make sure this data outlives the
     * transaction, because of the possibility that the statement might
     * perform internal transaction control.
     */
    oldcontext = MemoryContextSwitchTo(parsecontext);
    #if PG_VERSION_NUM >= 100000
      querytree_list = pg_analyze_and_rewrite(parsetree, sql, NULL, 0,NULL);
    #else
      querytree_list = pg_analyze_and_rewrite(parsetree, sql, NULL, 0);
    #endif

    #if PG_VERSION_NUM < 130000
      plantree_list = pg_plan_queries(querytree_list, 0, NULL);
    #else
      plantree_list = pg_plan_queries(querytree_list, sql, 0, NULL);
    #endif

    /* Done with the snapshot used for parsing/planning */
    if (snapshot_set)
      PopActiveSnapshot();

    /* If we got a cancel signal in analysis or planning, quit */
    CHECK_FOR_INTERRUPTS();

    /*
     * Execute the query using the unnamed portal.
     */
    portal = CreatePortal("", true, true);
    /* Don't display the portal in pg_cursors */
    portal->visible = false;
    PortalDefineQuery(portal, NULL, sql, commandTag, plantree_list, NULL);
    PortalStart(portal, NULL, 0, InvalidSnapshot);
    PortalSetResultFormat(portal, 1, &format);    /* binary format */

    --commands_remaining;
    receiver = CreateDestReceiver(DestNone);

    /*
     * Only once the portal and destreceiver have been established can
     * we return to the transaction context.  All that stuff needs to
     * survive an internal commit inside PortalRun!
     */
    MemoryContextSwitchTo(oldcontext);

    /* Here's where we actually execute the command. */
    #if PG_VERSION_NUM < 100000
      (void) PortalRun(portal, FETCH_ALL, isTopLevel, receiver, receiver, completionTag);
    #elif PG_VERSION_NUM < 130000
      (void) PortalRun(portal, FETCH_ALL, isTopLevel,true, receiver, receiver, completionTag);
    #else
      (void) PortalRun(portal, FETCH_ALL, isTopLevel, true, receiver, receiver, &qc);
    #endif

    /* Clean up the receiver. */
    (*receiver->rDestroy) (receiver);

    /*
     * Send a CommandComplete message even if we suppressed the query
     * results.  The user backend will report these in the absence of
     * any true query results.
     */
    #if PG_VERSION_NUM < 130000
      EndCommand(completionTag, DestRemote);
    #else
      EndCommand(&qc, DestRemote, false);
    #endif

    /* Clean up the portal. */
    PortalDrop(portal, false);
  }

  /* Be sure to advance the command counter after the last script command */
  CommandCounterIncrement();
}

/*
 * If a task is not marked as active, set an appropriate error state on the task
 * and return true. Note that this should only be called after a task has
 * already been launched.
 */
static bool
jobCanceled(CronTask *task)
{
    Assert(task->state == CRON_TASK_CONNECTING || \
            task->state == CRON_TASK_SENDING || \
            task->state == CRON_TASK_BGW_RUNNING || \
            task->state == CRON_TASK_RUNNING);

    if (task->isActive)
        return false;
    else
    {
        /* Use the American spelling for consistency with PG code. */
        task->errorMessage = "job canceled";
        task->state = CRON_TASK_ERROR;

        /*
         * Technically, pollingStatus is only used by when UseBackgroundWorkers
         * is false, but no damage in setting it in both cases.
         */
        task->pollingStatus = 0;
        return true;
    }
}

/*
 * If a task has hit it's startup deadline, set an appropriate error state on
 * the task and return true. Note that this should only be called after a task
 * has already been launched.
 */
static bool
jobStartupTimeout(CronTask *task, TimestampTz currentTime)
{
    Assert(task->state == CRON_TASK_CONNECTING || \
            task->state == CRON_TASK_SENDING || \
            task->state == CRON_TASK_BGW_START);

    if (TimestampDifferenceExceeds(task->startDeadline, currentTime, 0))
    {
        task->errorMessage = "job startup timeout";
        task->pollingStatus = 0;
        task->state = CRON_TASK_ERROR;
        return true;
    }
    else
        return false;
}


/*
 * Periodically query for leader state and handle initializations for
 * switching between worker and leader status
 */
static bool
CheckForLeaderStateChange(
  bool prevLeaderState, TimestampTz currentTime, List* task_list)
{
  bool newLeaderState = prevLeaderState;
  static TimestampTz lastCheck = 0;
  if (SecondsPassed(lastCheck, currentTime) >= LeaderStatusQueryPeriod ||
    lastCheck == 0) {
    lastCheck = currentTime;
    if (newLeaderState != prevLeaderState) {
      // State transition
      if (newLeaderState) {
        // Worker to Leader
        // Only need to know the available nodes and current cluster
        // load in case we need to immediately start assigning job runs
        UpdateKnownNodesAndWorkload(currentTime, true);
      } else {
        // As a worker, we always expect pending runs to be 0.
        // We accept losing any queued pending runs as it is not
        // normal for runs to queue up. Occurs because time suddenly
        // moved fw by several minutes or execution of a job takes
        // longer than the scheduling period of the job (e.g. job takes
        // 2 minutes to execute but is scheduled to run every minute).
        // Any remote job runs we were monitoring should be forgotten
        // so we reset the task.
        // Any job runs where runningLocal is true implies the job run
        // is being executed locally. These runs still need to be
        // monitored.
        ListCell *taskCell = NULL;
        foreach(taskCell, task_list)
        {
          CronTask *task = (CronTask *) lfirst(taskCell);
          task->pendingRunCount = 0;
          if (!task->runningLocal)
          {
            ResetTask(task);
          }
        }
      }
    }
    ereport(LOG, (errmsg("Leader status %d", newLeaderState)));
  }
  return newLeaderState;
}
