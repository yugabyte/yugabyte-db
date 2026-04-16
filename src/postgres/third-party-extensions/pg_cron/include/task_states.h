/*-------------------------------------------------------------------------
 *
 * task_states.h
 *	  definition of task state functions
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
#include "storage/shm_mq.h"
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
	CRON_TASK_BGW_RUNNING = 9
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
	CronTaskState state;
	uint pendingRunCount;
	PGconn *connection;
	PostgresPollingStatusType pollingStatus;
	TimestampTz startDeadline;
	TimestampTz lastStartTime;
	uint32 secondsInterval;
	bool isSocketReady;
	bool isActive;
	char *errorMessage;
	bool freeErrorMessage;
	shm_mq_handle *sharedMemoryQueue;
	dsm_segment *seg;
	BackgroundWorkerHandle handle;
} CronTask;


extern void InitializeTaskStateHash(void);
extern void RefreshTaskHash(void);
extern List * CurrentTaskList(void);
extern void InitializeCronTask(CronTask *task, int64 jobId);
extern void RemoveTask(int64 jobId);


#endif
