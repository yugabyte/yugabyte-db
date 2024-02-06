/*-------------------------------------------------------------------------
 *
 * job_metadata.h
 *	  definition of job metadata functions
 *
 * Copyright (c) 2010-2015, Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#ifndef JOB_METADATA_H
#define JOB_METADATA_H


#include "nodes/pg_list.h"
#if (PG_VERSION_NUM < 120000)
#include "datatype/timestamp.h"
#endif

typedef enum
{
	CRON_STATUS_STARTING,
	CRON_STATUS_RUNNING,
	CRON_STATUS_SENDING,
	CRON_STATUS_CONNECTING,
	CRON_STATUS_SUCCEEDED,
	CRON_STATUS_FAILED
} CronStatus;

/* job metadata data structure */
typedef struct CronJob
{
	int64 jobId;
	char *scheduleText;
	entry schedule;
	char *command;
	char *nodeName;
	int nodePort;
	char *database;
	char *userName;
	bool active;
	char *jobName;
} CronJob;


/* global settings */
extern char *CronHost;
extern bool CronJobCacheValid;
extern bool EnableSuperuserJobs;


/* functions for retrieving job metadata */
extern void InitializeJobMetadataCache(void);
extern void ResetJobMetadataCache(void);
extern List * LoadCronJobList(void);
extern CronJob * GetCronJob(int64 jobId);

extern void InsertJobRunDetail(int64 runId, int64 *jobId, char *database, char *username, char *command, char *status);
extern void UpdateJobRunDetail(int64 runId, int32 *job_pid, char *status, char *return_message, TimestampTz *start_time,
									TimestampTz *end_time);
extern int64 NextRunId(void);
extern void MarkPendingRunsAsFailed(void);
extern char *GetCronStatus(CronStatus cronstatus);

#endif
