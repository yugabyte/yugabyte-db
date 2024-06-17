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
#include "pg_cron.h"
#include "task_states.h"

#include "access/hash.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"

/* YB includes */
#include "pg_yb_utils.h"
#include "catalog/yb_catalog_version.h"

/* forward declarations */
static HTAB * CreateCronTaskHash(void);
static CronTask * GetCronTask(int64 jobId);

/* global variables */
static MemoryContext CronTaskContext = NULL;
static HTAB *CronTaskHash = NULL;

/* settings */
bool LaunchActiveJobs = true;


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
	/* YB Note: Always read the latest entries in the catalog */
	if (IsYugaByteEnabled())
	{
		YBCPgResetCatalogReadTime();
		YbUpdateCatalogCacheVersion(YbGetMasterCatalogVersion());
	}

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

		task = GetCronTask(job->jobId);
		task->isActive = LaunchActiveJobs && job->active;
		task->secondsInterval = job->schedule.secondsInterval;
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

		/*
		 * We only initialize last run when entering into the hash.
		 * The net effect is that the timer for the first run of an
		 * interval job starts when pg_cron first learns about the job.
		 */
		task->lastStartTime = GetCurrentTimestamp();
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
