/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/configs/backround_job_configs.c
 *
 * Initialization of GUCs that control the configuration behavior of background
 * jobs in the system.
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>
#include <utils/guc.h>
#include <limits.h>
#include "configs/config_initialization.h"
#include "metadata/metadata_cache.h"


#define DEFAULT_MAX_INDEX_BUILD_ATTEMPTS 3
int MaxIndexBuildAttempts = DEFAULT_MAX_INDEX_BUILD_ATTEMPTS;

#define DEFAULT_INDEX_BUILD_SCHEDULE_IN_SEC 2
int32_t IndexBuildScheduleInSec = DEFAULT_INDEX_BUILD_SCHEDULE_IN_SEC;

#define DEFAULT_INDEX_BUILD_EVICTION_INTERVAL_IN_SEC 1200
int IndexQueueEvictionIntervalInSec = DEFAULT_INDEX_BUILD_EVICTION_INTERVAL_IN_SEC;

#define DEFAULT_MAX_NUM_ACTIVE_USERS_INDEX_BUILDS 2
int MaxNumActiveUsersIndexBuilds = DEFAULT_MAX_NUM_ACTIVE_USERS_INDEX_BUILDS;

#define DEFAULT_MAX_TTL_DELETE_BATCH_SIZE 10000
int MaxTTLDeleteBatchSize = DEFAULT_MAX_TTL_DELETE_BATCH_SIZE;

#define DEFAULT_TTL_PURGER_STATEMENT_TIMEOUT 60000
int TTLPurgerStatementTimeout = DEFAULT_TTL_PURGER_STATEMENT_TIMEOUT;

#define DEFAULT_TTL_PURGER_LOCK_TIMEOUT 10000
int TTLPurgerLockTimeout = DEFAULT_TTL_PURGER_LOCK_TIMEOUT;

#define DEFAULT_SINGLE_TTL_TASK_TIME_BUDGET 20000
int SingleTTLTaskTimeBudget = DEFAULT_SINGLE_TTL_TASK_TIME_BUDGET;

#define DEFAULT_ENABLE_BG_WORKER false
bool EnableBackgroundWorker = DEFAULT_ENABLE_BG_WORKER;

#define DEFAULT_BG_DATABASE_NAME "postgres"
char *BackgroundWorkerDatabaseName = DEFAULT_BG_DATABASE_NAME;

#define DEFAULT_BG_LATCH_TIMEOUT_SEC 10
int LatchTimeOutSec = DEFAULT_BG_LATCH_TIMEOUT_SEC;

#define DEFAULT_LOG_TTL_PROGRESS_ACTIVITY false
bool LogTTLProgressActivity = DEFAULT_LOG_TTL_PROGRESS_ACTIVITY;


void
InitializeBackgroundJobConfigurations(const char *prefix, const char *newGucPrefix)
{
	DefineCustomIntVariable(
		psprintf("%s.maxTTLDeleteBatchSize", prefix),
		gettext_noop(
			"The max number of delete operations permitted while deleting a batch of expired documents."),
		NULL,
		&MaxTTLDeleteBatchSize,
		DEFAULT_MAX_TTL_DELETE_BATCH_SIZE, 1, INT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.logTTLProgressActivity", prefix),
		gettext_noop(
			"Whether to log activity done by a ttl purger. It's turned off by default to reduce noise."),
		NULL, &LogTTLProgressActivity, DEFAULT_LOG_TTL_PROGRESS_ACTIVITY,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.TTLPurgerStatementTimeout", prefix),
		gettext_noop(
			"Statement timeout in milliseconds of the TTL purger delete query."),
		NULL,
		&TTLPurgerStatementTimeout,
		DEFAULT_TTL_PURGER_STATEMENT_TIMEOUT, 1, INT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.SingleTTLTaskTimeBudget", prefix),
		gettext_noop(
			"Time budget assigned in milliseconds for single invocation of ttl task."),
		NULL,
		&SingleTTLTaskTimeBudget,
		DEFAULT_SINGLE_TTL_TASK_TIME_BUDGET, 1, INT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.TTLPurgerLockTimeout", prefix),
		gettext_noop(
			"Lock timeout in milliseconds of the TTL purger delete query."),
		NULL,
		&TTLPurgerLockTimeout,
		DEFAULT_TTL_PURGER_LOCK_TIMEOUT, 1, INT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.maxNumActiveUsersIndexBuilds", prefix),
		gettext_noop("Max number of active users Index Builds that can run."),
		NULL, &MaxNumActiveUsersIndexBuilds,
		DEFAULT_MAX_NUM_ACTIVE_USERS_INDEX_BUILDS, 1, INT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.maxIndexBuildAttempts", prefix),
		gettext_noop(
			"The maximum number of attempts to build an index for a failed requests."),
		NULL, &MaxIndexBuildAttempts,
		DEFAULT_MAX_INDEX_BUILD_ATTEMPTS, 1, SHRT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.indexBuildScheduleInSec", prefix),
		gettext_noop("The index build cron-job schedule in seconds."),
		NULL, &IndexBuildScheduleInSec,
		DEFAULT_INDEX_BUILD_SCHEDULE_IN_SEC, 1, 60,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.indexQueueEvictionIntervalInSec", prefix),
		gettext_noop(
			"Interval in seconds for skippable build index requests to be evicted from the queue."),
		NULL, &IndexQueueEvictionIntervalInSec,
		DEFAULT_INDEX_BUILD_EVICTION_INTERVAL_IN_SEC, 1, INT_MAX,
		PGC_USERSET,
		GUC_NO_SHOW_ALL | GUC_NOT_IN_SAMPLE,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableBackgroundWorker", newGucPrefix),
		gettext_noop("Enable the extension Background worker."),
		NULL, &EnableBackgroundWorker, DEFAULT_ENABLE_BG_WORKER,
		PGC_SUSET, 0, NULL, NULL, NULL);
}


void
InitDocumentDBBackgroundWorkerGucs(const char *prefix)
{
	DefineCustomStringVariable(
		psprintf("%s_bg_worker_database_name", prefix),
		gettext_noop("Database to which background worker will connect."),
		NULL,
		&BackgroundWorkerDatabaseName,
		DEFAULT_BG_DATABASE_NAME,
		PGC_POSTMASTER,
		GUC_SUPERUSER_ONLY,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.bg_worker_latch_timeout", prefix),
		gettext_noop("Latch timeout inside main thread of bg worker leader."),
		NULL,
		&LatchTimeOutSec,
		DEFAULT_BG_LATCH_TIMEOUT_SEC,
		0,
		200,
		PGC_POSTMASTER,
		GUC_SUPERUSER_ONLY,
		NULL, NULL, NULL);
}
