/*-------------------------------------------------------------------------
 *
 * src/task_states.c
 *
 * Logic for storing and manipulating cron task states.
 *
 * Copyright (c) 2016, Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"

#include "cron.h"
#include "cron_job.h"
#include "pg_cron.h"
#include "task_states.h"

#include "access/genam.h"
#include "access/hash.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/xact.h"
#include "access/xlog.h"
#include "catalog/pg_collation_d.h"
#include "executor/spi.h"
#include "pgstat.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/hsearch.h"
#include "utils/inval.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"

#if (PG_VERSION_NUM < 120000)
#define table_open(r, l) heap_open(r, l)
#define table_close(r, l) heap_close(r, l)
#endif


/* forward declarations */
static HTAB * CreateCronTaskHash(void);
static CronTask * GetCronTask(int64 jobId);

/* global variables */
static MemoryContext CronTaskContext = NULL;
static HTAB *CronTaskHash = NULL;


/*
 * InitializeTaskStateHash initializes the hash for storing task states.
 */
void
InitializeTaskStateHash(void)
{
  CronTaskContext = AllocSetContextCreate(CurrentMemoryContext,
                        "pg_cron task context",
                        ALLOCSET_DEFAULT_MINSIZE,
                        ALLOCSET_DEFAULT_INITSIZE,
                        ALLOCSET_DEFAULT_MAXSIZE);

  CronTaskHash = CreateCronTaskHash();
}


/*
 * CreateCronTaskHash creates the hash for storing cron task states.
 */
static HTAB *
CreateCronTaskHash(void)
{
  HTAB *taskHash = NULL;
  HASHCTL info;
  int hashFlags = 0;

  memset(&info, 0, sizeof(info));
  info.keysize = sizeof(int64);
  info.entrysize = sizeof(CronTask);
  info.hash = tag_hash;
  info.hcxt = CronTaskContext;
  hashFlags = (HASH_ELEM | HASH_FUNCTION | HASH_CONTEXT);

  taskHash = hash_create("pg_cron tasks", 32, &info, hashFlags);

  return taskHash;
}


/*
 * RefreshTaskHash reloads the cron jobs from the cron.job table.
 * If a job that has an active task has been removed, the task
 * is marked as inactive by this function.
 */
void
RefreshTaskHash(void)
{
  List *jobList = NIL;
  ListCell *jobCell = NULL;
  CronTask *task = NULL;
  HASH_SEQ_STATUS status;

  ResetJobMetadataCache();

  hash_seq_init(&status, CronTaskHash);

  /* mark all tasks as inactive */
  while ((task = hash_seq_search(&status)) != NULL)
  {
    task->isActive = false;
  }

  jobList = LoadCronJobList();

  /* mark tasks that still have a job as active */
  foreach(jobCell, jobList)
  {
    CronJob *job = (CronJob *) lfirst(jobCell);

    CronTask *task = GetCronTask(job->jobId);
    task->isActive = job->active;
  }

  CronJobCacheValid = true;
}


/*
 * GetCronTask gets the current task with the given job ID.
 */
static CronTask *
GetCronTask(int64 jobId)
{
  CronTask *task = NULL;
  int64 hashKey = jobId;
  bool isPresent = false;

  task = hash_search(CronTaskHash, &hashKey, HASH_ENTER, &isPresent);
  if (!isPresent)
  {
    InitializeCronTask(task, jobId);
  }

  return task;
}


/*
 * InitializeCronTask intializes a CronTask struct.
 */
void
InitializeCronTask(CronTask *task, int64 jobId)
{
  task->runId = 0;
  task->jobId = jobId;
  task->state = CRON_TASK_WAITING;
  task->pendingRunCount = 0;
  task->connection = NULL;
  task->pollingStatus = 0;
  task->startDeadline = 0;
  task->isSocketReady = false;
  task->isActive = true;
  task->errorMessage = NULL;
  task->freeErrorMessage = false;
  task->runningLocal = false;
}


/*
 * CurrentTaskList extracts the current list of tasks from the
 * cron task hash.
 */
List *
CurrentTaskList(void)
{
  List *taskList = NIL;
  CronTask *task = NULL;
  HASH_SEQ_STATUS status;

  hash_seq_init(&status, CronTaskHash);

  while ((task = hash_seq_search(&status)) != NULL)
  {
    taskList = lappend(taskList, task);
  }

  return taskList;
}


/*
 * RemoveTask remove the task for the given job ID.
 */
void
RemoveTask(int64 jobId)
{
  bool isPresent = false;

  hash_search(CronTaskHash, &jobId, HASH_REMOVE, &isPresent);
}

/*
 * ProcessNewJobs queues up the new jobs assigned for this worker
 */
void
ProcessNewJobs()
{
  Relation cronJobRunTable = NULL;

  SysScanDesc scanDescriptor = NULL;
  int scanKeyCount = 2;
  ScanKeyData scanKey[scanKeyCount];
  bool indexOK = false;
  HeapTuple heapTuple = NULL;
  TupleDesc tupleDescriptor = NULL;
  MemoryContext originalContext = CurrentMemoryContext;

  Datum nodeNameDatum = CStringGetTextDatum(MyNodeName);
  Datum statusDatum =
    CStringGetTextDatum(GetCronStatus(CRON_STATUS_STARTING));

  SetCurrentStatementStartTimestamp();
  StartTransactionCommand();
  PushActiveSnapshot(GetTransactionSnapshot());

  /*
   * If the pg_cron extension has not been created yet or
   * we are on a hot standby, the job run details table is treated as
   * being empty.
   */
  if (!PgCronHasBeenLoaded() || RecoveryInProgress())
  {
    PopActiveSnapshot();
    CommitTransactionCommand();
    MemoryContextSwitchTo(originalContext);
    pgstat_report_activity(STATE_IDLE, NULL);
    return;
  }

  cronJobRunTable = table_open(CronJobRunRelationId(), AccessShareLock);
  // Need to look for nodename = me and status = starting
  ScanKeyInit(&scanKey[0], Anum_job_run_details_nodename,
        BTEqualStrategyNumber, F_TEXTEQ, nodeNameDatum);
  ScanKeyInit(&scanKey[1], Anum_job_run_details_status,
        BTEqualStrategyNumber, F_TEXTEQ, statusDatum);

  scanDescriptor = systable_beginscan(cronJobRunTable, InvalidOid, indexOK,
                    NULL, scanKeyCount, scanKey);

  tupleDescriptor = RelationGetDescr(cronJobRunTable);

  heapTuple = systable_getnext(scanDescriptor);
  while (HeapTupleIsValid(heapTuple))
  {
    MemoryContext oldContext = NULL;
    bool isNull = false;

    oldContext = MemoryContextSwitchTo(CronTaskContext);

    Datum jobIdDatum = heap_getattr(heapTuple, Anum_job_run_details_jobid,
                 tupleDescriptor, &isNull);
    int64 jobId = DatumGetInt64(jobIdDatum);
    Datum runIdDatum = heap_getattr(heapTuple, Anum_job_run_details_runid,
                 tupleDescriptor, &isNull);
    int64 runId = DatumGetInt64(runIdDatum);
    heapTuple = systable_getnext(scanDescriptor);
    ereport(DEBUG3,(errmsg("Got job " INT64_FORMAT " run " INT64_FORMAT,
              jobId, runId)));
    CronTask *task = GetCronTask(jobId);
    // We should never be in the done state outside of ManageCronTask
    switch (task->state)
    {
      case CRON_TASK_REMOTE_START:
        // leader assigned work to themselves
      case CRON_TASK_WAITING:
        // non-leader worker got a job
      case CRON_TASK_DONE:
        // leader/non-leader worker just completed the job run
        // but didn't get a change to reset the task yet
        break;
      default:
        // job is already running
        ereport(WARNING, (errmsg("Unexpected job " INT64_FORMAT " run "
                     INT64_FORMAT " found while job run in "
                     "state %d. Ignoring ",
                     jobId, runId, task->state)));
        continue;
    }
    SetTaskLocalStart(task, runId);

    MemoryContextSwitchTo(oldContext);
  }

  systable_endscan(scanDescriptor);
  table_close(cronJobRunTable, AccessShareLock);

  PopActiveSnapshot();
  CommitTransactionCommand();
  MemoryContextSwitchTo(originalContext);
  pgstat_report_activity(STATE_IDLE, NULL);
}

/*
 * UpdateJobRunStatus determines if there are any tasks that completed
 */
void
UpdateJobRunStatus(List *taskList)
{  
  StringInfoData querybuf;
  MemoryContext originalContext = CurrentMemoryContext;

  SetCurrentStatementStartTimestamp();
  StartTransactionCommand();
  PushActiveSnapshot(GetTransactionSnapshot());

  if (!PgCronHasBeenLoaded() || RecoveryInProgress() ||
    !JobRunDetailsTableExists())
  {
    PopActiveSnapshot();
    CommitTransactionCommand();
    MemoryContextSwitchTo(originalContext);
    return;
  }

  initStringInfo(&querybuf);

  /* Open SPI context. */
  if (SPI_connect() != SPI_OK_CONNECT)
    elog(ERROR, "SPI_connect failed");


  // We want to find the status of the job runs we are currently monitoring
  appendStringInfo(&querybuf,
    "SELECT jobid, runid, status FROM %s.%s WHERE runid IN (",
    CRON_SCHEMA_NAME, JOB_RUN_DETAILS_TABLE_NAME);
  
  bool found = false;
  ListCell *taskCell = NULL;
  foreach(taskCell, taskList)
  {
    CronTask *task = (CronTask *) lfirst(taskCell);
    // Ignore local running tasks because those will naturally move their
    // state to CRON_TASK_DONE
    if (task->state == CRON_TASK_REMOTE_START ||
      task->state == CRON_TASK_REMOTE_RUNNING)
    {
      appendStringInfo(&querybuf, INT64_FORMAT", ", task->runId);
      found = true;
    }
  }

  if (!found)
  {
    elog(DEBUG3, "No job runs awaiting completion!");
    SPI_finish();
    PopActiveSnapshot();
    CommitTransactionCommand();
    MemoryContextSwitchTo(originalContext);
    pgstat_report_activity(STATE_IDLE, NULL);
    return;
  }
  // Remove extra comma and space
  querybuf.len -= 2;
  querybuf.data[querybuf.len] = '\0';
  appendStringInfo(&querybuf, ")");

  pgstat_report_activity(STATE_RUNNING, querybuf.data);

  if(SPI_execute(querybuf.data, true, 0) != SPI_OK_SELECT ||
     SPI_tuptable == NULL)
    elog(ERROR, "SPI_exec failed: %s", querybuf.data);

  SPITupleTable *tuptable = SPI_tuptable;
  TupleDesc tupdesc = tuptable->tupdesc;

  for (uint64 i = 0; i < SPI_processed; i++) {
        HeapTuple tuple = tuptable->vals[i];
    MemoryContext oldContext = NULL;
    bool isNull;
    
    oldContext = MemoryContextSwitchTo(CronTaskContext);

        const Datum jobIdDatum = SPI_getbinval(tuple, tupdesc, 1, &isNull);
    Assert(!isNull);
    const int64 jobId = DatumGetInt64(jobIdDatum);
    const Datum runIdDatum = SPI_getbinval(tuple, tupdesc, 2, &isNull);
    Assert(!isNull);
    const int64 runId = DatumGetInt64(runIdDatum);
    const char *status = SPI_getvalue(tuple, tupdesc, 3);
    CronTask *task = GetCronTask(jobId);
    if (strcmp(status, GetCronStatus(CRON_STATUS_SUCCEEDED)) == 0 ||
      strcmp(status, GetCronStatus(CRON_STATUS_FAILED)) == 0)
    {
      elog(LOG, "Job " INT64_FORMAT " Run " INT64_FORMAT " complete",
         jobId, runId);
      if (task->isActive) {
        SetTaskDone(task);
      } else {
        RemoveTask(jobId);
      }
    }
    else if (strcmp(status, GetCronStatus(CRON_STATUS_RUNNING)) == 0)
    {
      elog(LOG, "Job " INT64_FORMAT " Run " INT64_FORMAT " running",
         jobId, runId);
      SetTaskRemoteRunning(task);
    }
    MemoryContextSwitchTo(oldContext);
  }

  pfree(querybuf.data);

  SPI_finish();
  PopActiveSnapshot();
  CommitTransactionCommand();
  MemoryContextSwitchTo(originalContext);
  pgstat_report_activity(STATE_IDLE, NULL);
}

/*
 * SetTaskDone sets a task to the done state
 */
void
SetTaskDone(CronTask *task)
{
  task->state = CRON_TASK_DONE;
}

/*
 * SetTaskLocalStart sets a task to be executed locally
 */
void
SetTaskLocalStart(CronTask *task, int64 runId)
{
  ResetTask(task);
  if (UseBackgroundWorkers)
    task->state = CRON_TASK_BGW_START;
  else
    task->state = CRON_TASK_START;
  
  task->runId = runId;
  // Set runningLocal to be true so that this task will follow the
  // state transitions for a worker
  task->runningLocal = true;
}

/*
 * SetTaskRemoteRunning sets a task to the remote running state
 */
void
SetTaskRemoteRunning(CronTask *task)
{
  task->state = CRON_TASK_REMOTE_RUNNING;
}

/*
 * ResetTask resets the state of a job run.
 * Pending run count is NOT affected
 */
void
ResetTask(CronTask *task)
{
  int currentPendingRunCount = task->pendingRunCount;

  InitializeCronTask(task, task->jobId);

  /*
   * We keep the number of runs that should have started while
   * the task was still running. If >0, this will trigger another
   * run immediately.
   */
  task->pendingRunCount = currentPendingRunCount;
}

/*
 * IncrementTaskRunCount adds one to the pending run count
 */
void
IncrementTaskRunCount(CronTask *task)
{
  task->pendingRunCount += 1;
}

/*
 * RepeatTask resets the job run and adds one to the pending run count
 */
void
RepeatTask(int64 jobId)
{
  CronTask *task = GetCronTask(jobId);
  SetTaskDone(task);
  IncrementTaskRunCount(task);
}
