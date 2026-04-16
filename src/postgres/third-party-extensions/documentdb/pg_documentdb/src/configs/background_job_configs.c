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

#define DEFAULT_TTL_TASK_MAX_RUNTIME_IN_MS 60000
int TTLTaskMaxRunTimeInMS = DEFAULT_TTL_TASK_MAX_RUNTIME_IN_MS;

#define DEFAULT_TTL_DELETE_SATURATION_RATIO_THRESHOLD 0.9
double TTLDeleteSaturationThreshold = DEFAULT_TTL_DELETE_SATURATION_RATIO_THRESHOLD;

#define DEFAULT_SLOW_TTL_BATCH_DELETE_THRESHOLD_IN_MS 10000
int TTLSlowBatchDeleteThresholdInMS = DEFAULT_SLOW_TTL_BATCH_DELETE_THRESHOLD_IN_MS;

/* Enable by default on 1.109 */
#define DEFAULT_REPEAT_PURGE_INDEXES_FOR_TTL_TASK false
bool RepeatPurgeIndexesForTTLTask = DEFAULT_REPEAT_PURGE_INDEXES_FOR_TTL_TASK;

#define DEFAULT_ENABLE_TTL_DESC_SORT false
bool EnableTTLDescSort = DEFAULT_ENABLE_TTL_DESC_SORT;

#define DEFAULT_ENABLE_BG_WORKER true
bool EnableBackgroundWorker = DEFAULT_ENABLE_BG_WORKER;

#define DEFAULT_ENABLE_BG_WORKER_JOBS true
bool EnableBackgroundWorkerJobs = DEFAULT_ENABLE_BG_WORKER_JOBS;

#define DEFAULT_BG_WORKER_JOB_TIMEOUT_THRESHOLD_SEC 300
int BackgroundWorkerJobTimeoutThresholdSec = DEFAULT_BG_WORKER_JOB_TIMEOUT_THRESHOLD_SEC;

#define DEFAULT_BG_DATABASE_NAME "postgres"
char *BackgroundWorkerDatabaseName = DEFAULT_BG_DATABASE_NAME;

#define DEFAULT_BG_LATCH_TIMEOUT_SEC 1
int LatchTimeOutSec = DEFAULT_BG_LATCH_TIMEOUT_SEC;

#define DEFAULT_LOG_TTL_PROGRESS_ACTIVITY false
bool LogTTLProgressActivity = DEFAULT_LOG_TTL_PROGRESS_ACTIVITY;

#define DEFAULT_ENABLE_SELECTIVE_TTL_LOGGING true
bool EnableSelectiveTTLLogging = DEFAULT_ENABLE_SELECTIVE_TTL_LOGGING;

#define DEFAULT_ENABLE_TTL_BATCH_OBSERVABILITY true
bool EnableTTLBatchObservability = DEFAULT_ENABLE_TTL_BATCH_OBSERVABILITY;

#define DEFAULT_FORCE_INDEX_SCAN_TTL_TASK true
bool ForceIndexScanForTTLTask = DEFAULT_FORCE_INDEX_SCAN_TTL_TASK;

#define DEFAULT_USE_INDEX_HINTS_TTL_TASK true
bool UseIndexHintsForTTLTask = DEFAULT_USE_INDEX_HINTS_TTL_TASK;

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

	DefineCustomBoolVariable(
		psprintf("%s.enableSelectiveTTLLogging", prefix),
		gettext_noop(
			"Whether to log highly saturated or slow ttl batches. It's turned off by default to reduce noise."),
		NULL, &EnableSelectiveTTLLogging, DEFAULT_ENABLE_SELECTIVE_TTL_LOGGING,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableTTLBatchObservability", prefix),
		gettext_noop(
			"Whether to calculate and emit feature counters ttl_saturated_batches and ttl_slow_batches."),
		NULL, &EnableTTLBatchObservability, DEFAULT_ENABLE_TTL_BATCH_OBSERVABILITY,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.forceIndexScanForTTLTask", prefix),
		gettext_noop(
			"Whether to force Index Scan for TTL task by locally disabling Sequential Scan and Bitmap Index Scan"),
		NULL, &ForceIndexScanForTTLTask, DEFAULT_FORCE_INDEX_SCAN_TTL_TASK,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.useIndexHintsForTTLTask", prefix),
		gettext_noop(
			"Whether to force ordered Index Scan via Index Hints for TTL task"),
		NULL, &UseIndexHintsForTTLTask, DEFAULT_USE_INDEX_HINTS_TTL_TASK,
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
		psprintf("%s.TTLTaskMaxRunTimeInMS", newGucPrefix),
		gettext_noop(
			"Time budget assigned in milliseconds for single invocation of ttl task."),
		NULL,
		&TTLTaskMaxRunTimeInMS,
		DEFAULT_TTL_TASK_MAX_RUNTIME_IN_MS, 1, INT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);


	DefineCustomBoolVariable(
		psprintf("%s.repeatPurgeIndexesForTTLTask", newGucPrefix),
		gettext_noop(
			"Whether to keep deleting documents in batches until `TTLTaskMaxRunTimeInMS` is reach per TTL task invocation."),
		NULL,
		&RepeatPurgeIndexesForTTLTask,
		DEFAULT_REPEAT_PURGE_INDEXES_FOR_TTL_TASK,
		PGC_SUSET,
		0,
		NULL, NULL, NULL);

	DefineCustomRealVariable(
		psprintf("%s.TTLDeleteSaturationThreshold", prefix),
		gettext_noop(
			"Logging threshold for ttl delete saturation ratio defined as total rows deleted in an invocation divided by the batch size."),
		NULL,
		&TTLDeleteSaturationThreshold,
		DEFAULT_TTL_DELETE_SATURATION_RATIO_THRESHOLD, 0.0, 1.0,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.TTLSlowBatchDeleteThresholdInMS", prefix),
		gettext_noop(
			"Threshold for considering a single batch of ttl deletes to be slow."),
		NULL,
		&TTLSlowBatchDeleteThresholdInMS,
		DEFAULT_SLOW_TTL_BATCH_DELETE_THRESHOLD_IN_MS, 0, INT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.SingleTTLTaskTimeBudget", prefix),
		gettext_noop(
			"Time budget assigned in milliseconds for TTL task to purge one batch of documents from each eligible TTL indexes once."),
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

	DefineCustomBoolVariable(
		psprintf("%s.enableTTLDescSort", newGucPrefix),
		gettext_noop(
			"Whether or not to enable TTL descending sort on field."),
		NULL,
		&EnableTTLDescSort,
		DEFAULT_ENABLE_TTL_DESC_SORT,
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
		PGC_POSTMASTER, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableBackgroundWorkerJobs", newGucPrefix),
		gettext_noop("Enable the execution of the pre-defined background worker jobs."),
		NULL, &EnableBackgroundWorkerJobs, DEFAULT_ENABLE_BG_WORKER_JOBS,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.backgroundWorkerJobTimeoutThresholdSec", newGucPrefix),
		gettext_noop(
			"Maximum allowed value in seconds for a background worker job timeout."),
		NULL, &BackgroundWorkerJobTimeoutThresholdSec,
		DEFAULT_BG_WORKER_JOB_TIMEOUT_THRESHOLD_SEC, 1, INT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);
}


void
InitDocumentDBBackgroundWorkerConfigurations(const char *prefix)
{
	DefineCustomStringVariable(
		psprintf("%s.bg_worker_database_name", prefix),
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
