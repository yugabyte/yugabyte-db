/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/infrastructure/job_management.h
 *
 * Common declarations for the pg_documentdb job management.
 *
 *-------------------------------------------------------------------------
 */

#ifndef DOCUMENTDB_JOB_MANAGEMENT_H
#define DOCUMENTDB_JOB_MANAGEMENT_H

#define DOCUMENTDB_INDEX_BUILD_JOB1_JOBID 90
#define DOCUMENTDB_INDEX_BUILD_JOB2_JOBID 91

extern bool EnableBackgroundWorker;
extern bool EnableBackgroundWorkerJobs;
extern bool EnableBgWorkerMetricsEmission;
extern bool IndexBuildsScheduledOnBgWorker;

/* index build tasks */
void UnscheduleIndexBuildTasks(char *extensionPrefix);
void ScheduleIndexBuildTasks(char *extensionPrefix);
void RegisterIndexBuildBackgroundWorkerJobs(void);

#endif
