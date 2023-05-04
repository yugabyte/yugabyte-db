/*-------------------------------------------------------------------------
 *
 * task_states.h
 *    definition of task state functions
 *
 * Copyright (c) 2010-2015, Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#ifndef TASK_STATES_H
#define TASK_STATES_H


#include "job_metadata.h"
#include "libpq-fe.h"
#include "postmaster/bgworker.h"
#include "storage/dsm.h"
#include "utils/timestamp.h"


typedef enum
{
  CRON_TASK_WAITING = 0,
  CRON_TASK_START = 1,
  CRON_TASK_CONNECTING = 2,
  CRON_TASK_SENDING = 3,
  CRON_TASK_RUNNING = 4,
  CRON_TASK_RECEIVING = 5,
  CRON_TASK_DONE = 6,
  CRON_TASK_ERROR = 7,
  CRON_TASK_BGW_START = 8,
  CRON_TASK_BGW_RUNNING = 9,
  CRON_TASK_REMOTE_START = 10,
  CRON_TASK_REMOTE_RUNNING = 11
} CronTaskState;

struct BackgroundWorkerHandle
{
  int slot;
  uint64 generation;
};

typedef struct CronTask
{
  int64 jobId;
  int64 runId;

  // Current state of this job run.
  // Leaders will move REMOTE jobs runs between CRON_TASK_WAITING,
  // CRON_TASK_REMOTE_START, CRON_TASK_REMOTE_RUNNING, CRON_TASK_DONE
  // Workers always moves job runs between CRON_TASK_WAITING,
  // CRON_TASK_BGW_START, CRON_TASK_BGW_RUNNING, CRON_TASK_DONE
  CronTaskState state;

  // Number of runs queued for this job. For any non-leader worker,
  // pendingRunCount should be 0. Only the leader would increment this field
  uint pendingRunCount;

  // Used for pqlib job execution
  PGconn *connection;
  PostgresPollingStatusType pollingStatus;

  // Used as pqlib connection creation deadline and local bgw launch deadline
  // This is also the launch deadline for when a leader expects a worker to
  // have started a job run.
  TimestampTz startDeadline;

  // Used for pqlib job execution
  bool isSocketReady;

  // Whether this job is enabled or not. Matches active field in associated
  // job table row
  bool isActive;

  char *errorMessage;

  // Used when an error occurs in pqlib
  bool freeErrorMessage;

  // Shared memory segment with bgw
  dsm_segment *seg;
  BackgroundWorkerHandle handle;

  // Represents whether this task is being executed locally or not.
  bool runningLocal;
} CronTask;


extern void InitializeTaskStateHash(void);
extern void RefreshTaskHash(void);
extern List * CurrentTaskList(void);
extern void InitializeCronTask(CronTask *task, int64 jobId);
extern void RemoveTask(int64 jobId);
extern void RepeatTask(int64 jobId);

extern void SetTaskDone(CronTask *task);
extern void SetTaskLocalStart(CronTask *task, int64 runId);
extern void SetTaskRemoteRunning(CronTask *task);
extern void ResetTask(CronTask *task);
extern void IncrementTaskRunCount(CronTask *task);

extern void ProcessNewJobs();
extern void UpdateJobRunStatus(List *taskList);


#endif
