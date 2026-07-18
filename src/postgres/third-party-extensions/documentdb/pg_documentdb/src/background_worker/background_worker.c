/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/backgroundworker/background_worker.c
 *
 * Implementation of Background worker.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <catalog/pg_extension.h>
#include <catalog/namespace.h>
#include <nodes/pg_list.h>
#include <tcop/utility.h>
#include <postmaster/interrupt.h>
#include <libpq-fe.h>
#include <storage/latch.h>
#include <miscadmin.h>
#include <postmaster/bgworker.h>
#include <storage/shmem.h>
#include <storage/ipc.h>
#include <postmaster/postmaster.h>
#include <utils/backend_status.h>
#include <utils/wait_event.h>
#include <utils/memutils.h>
#include <utils/timestamp.h>
#include <utils/builtins.h>
#include <access/xact.h>
#include <utils/snapmgr.h>
#include <catalog/pg_proc_d.h>
#include "utils/query_utils.h"
#include "utils/documentdb_errors.h"
#include "utils/syscache.h"
#include "utils/lsyscache.h"
#include "utils/acl.h"
#include "parser/parse_func.h"
#include "nodes/makefuncs.h"

#include "api_hooks.h"
#include "api_hooks_def.h"
#include "background_worker/background_worker_job.h"
#include "commands/connection_management.h"
#include "infrastructure/job_management.h"
#include "metadata/metadata_cache.h"
#include "utils/error_utils.h"
#include "utils/index_utils.h"

#define ONE_SEC_IN_MS 1000L

/*
 * The main background worker shmem struct.  On shared memory we store this main
 * struct. This struct keeps:
 *
 * latch Sharable latch
 */
typedef struct BackgroundWorkerShmemStruct
{
	Latch latch;
} BackgroundWorkerShmemStruct;

PGDLLEXPORT void DocumentDBBackgroundWorkerMain(Datum);

extern char *BackgroundWorkerDatabaseName;
extern char *LocalhostConnectionString;

extern int LatchTimeOutSec;
extern int BackgroundWorkerJobTimeoutThresholdSec;

static bool BackgroundWorkerReloadConfig = false;

/* Shared memory segment for BackgroundWorker */
static BackgroundWorkerShmemStruct *BackgroundWorkerShmem;
static Size BackgroundWorkerShmemSize(void);
static void BackgroundWorkerShmemInit(void);
static void BackgroundWorkerKill(int code, Datum arg);

/* Flags set by signal handlers */
static volatile sig_atomic_t got_sigterm = false;
static void background_worker_sigterm(SIGNAL_ARGS);
static void background_worker_sighup(SIGNAL_ARGS);

static char ExtensionBackgroundWorkerLeaderName[50];

/*
 * Background worker job states.
 */
typedef enum
{
	/* Job is not executing and is waiting to start. */
	JOB_IDLE = 0,

	/* Connection was established and query is executing. */
	JOB_RUNNING = 1,
} BackgroundWorkerJobState;

/*
 * Boolean representation that accounts for absence of information (Undefined).
 */
typedef enum BackgroundWorkerBoolOption
{
	BackgroundWorkerBoolOption_Undefined = -1,

	BackgroundWorkerBoolOption_False = 0,

	BackgroundWorkerBoolOption_True = 1,
} BackgroundWorkerBoolOption;

/*
 * Background worker job execution object.
 */
typedef struct
{
	/* For 1:1 mapping between BackgroundWorkerJob and BackgroundWorkerJobExecution. */
	BackgroundWorkerJob job;

	/* Last time when job started execution. */
	TimestampTz lastStartTime;

	/* PG connection object instance. */
	PGconn *connection;

	/* SQL command query generated from job command and argument. */
	char *commandQuery;

	/* Job state. */
	BackgroundWorkerJobState state;
} BackgroundWorkerJobExecution;

extern void RegisterBackgroundWorkerJobAllowedCommand(BackgroundWorkerJobCommand command);

static BackgroundWorkerBoolOption IsCoordinator =
	BackgroundWorkerBoolOption_Undefined;

/* Background worker job functions*/
static void ValidateJob(BackgroundWorkerJob job);
static void ManageJobsLifeCycle(List *jobExecutions, char *userName, char *databaseName);
static void ExecuteJob(BackgroundWorkerJobExecution *jobExec, char *userName,
					   char *databaseName, TimestampTz currentTime);
static void CheckJobCompletion(BackgroundWorkerJobExecution *jobExec);
static void FreeJobExecutions(List *jobExecutions);
static bool CheckIfMetadataCoordinator(void);
static bool CheckIfJobCommandIsAllowed(BackgroundWorkerJobCommand command);
static bool CanExecuteJob(BackgroundWorkerJobExecution *jobExec, TimestampTz currentTime);
static bool CheckIfRoleExists(const char *roleName);
static List * GenerateJobExecutions(void);
static BackgroundWorkerJobExecution * CreateJobExecutionObj(BackgroundWorkerJob job);
static char * GenerateCommandQuery(BackgroundWorkerJob job, MemoryContext stableContext);
static void CancelJobIfTimeIsUp(BackgroundWorkerJobExecution *jobExec, TimestampTz
								currentTime);
static void WaitForBackgroundWorkerDependencies(void);

/*
 * The allowed commands registry should not be exposed outside this c file to avoid unpredictable behavior.
 */
#define MAX_BACKGROUND_WORKER_ALLOWED_COMMANDS 4
static BackgroundWorkerJobCommand
	AllowedCommandRegistry[MAX_BACKGROUND_WORKER_ALLOWED_COMMANDS];
static int AllowedCommandEntries = 0;


/*
 * The jobs registry should not be exposed outside this c file to avoid unpredictable behavior.
 */
#define MAX_BACKGROUND_WORKER_JOBS 5
static BackgroundWorkerJob JobRegistry[MAX_BACKGROUND_WORKER_JOBS];
static int JobEntries = 0;


/* Default implementation of the hook. Presently just returns a const.
 * XXX: maybe this can be a GUC itself
 */
inline static int
GetDefaultScheduleIntervalInSeconds(void)
{
	return 60;
}


/*
 * DocumentDB background worker entry point.
 */
void
DocumentDBBackgroundWorkerMain(Datum main_arg)
{
	char *databaseName = BackgroundWorkerDatabaseName;

	/* Establish signal handlers before unblocking signals. */
	pqsignal(SIGINT, SIG_IGN);
	pqsignal(SIGTERM, background_worker_sigterm);
	pqsignal(SIGHUP, background_worker_sighup);

	/* We're now ready to receive signals */
	BackgroundWorkerUnblockSignals();

	/*
	 * Initialize background worker connection as the superuser.
	 * This role will only be used to access catalog tables and
	 * the SysCache
	 */
	BackgroundWorkerInitializeConnection(databaseName, NULL, 0);

	if (strlen(ExtensionObjectPrefixV2) + strlen("_bg_worker_leader") + 1 >
		sizeof(ExtensionBackgroundWorkerLeaderName))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"Unexpected - ExtensionObjectPrefix is too long for background worker leader name"),
						errdetail_log(
							"Unexpected - ExtensionObjectPrefix %s is too long for background worker leader name",
							ExtensionObjectPrefixV2)));
	}
	snprintf(ExtensionBackgroundWorkerLeaderName,
			 sizeof(ExtensionBackgroundWorkerLeaderName),
			 "%s_bg_worker_leader", ExtensionObjectPrefixV2);

	pgstat_report_appname(ExtensionBackgroundWorkerLeaderName);

	/* Own the latch once everything is ready */
	BackgroundWorkerShmemInit();
	OwnLatch(&BackgroundWorkerShmem->latch);

	/* Set on-detach hook so that our PID will be cleared on exit. */
	on_shmem_exit(BackgroundWorkerKill, 0);

	/*
	 * Wait until BackgroundWorkerRole prerequisites are met.
	 */
	WaitForBackgroundWorkerDependencies();

	ereport(LOG, (errmsg("Starting %s with databaseName %s and role %s",
						 ExtensionBackgroundWorkerLeaderName, databaseName,
						 ApiBgWorkerRole)));

	/*
	 * Main loop: do this until SIGTERM is received and processed by
	 * ProcessInterrupts.
	 */

	int waitResult;
	int latchTimeOut = LatchTimeOutSec;

	/* Create list of job executions */
	List *jobExecutions = NIL;

	while (!got_sigterm)
	{
		/*
		 * The background worker job framework is controlled by a GUC
		 * that enables or disables job executions. The control flow
		 * below exists to adjust the internal state gracefuly when the
		 * GUC value changes in real time.
		 */
		if (jobExecutions != NIL)
		{
			if (!EnableBackgroundWorkerJobs)
			{
				FreeJobExecutions(jobExecutions);
				jobExecutions = NIL;
			}
		}
		else if (EnableBackgroundWorkerJobs)
		{
			jobExecutions = GenerateJobExecutions();
		}

		/*
		 * Background workers mustn't call usleep() or any direct equivalent:
		 * instead, they may wait on their process latch, which sleeps as
		 * necessary, but is awakened if postmaster dies.  That way the
		 * background process goes away immediately in an emergency.
		 */
		waitResult = 0;
		if (BackgroundWorkerReloadConfig)
		{
			/* read the latest value of {ExtensionObjectPrefix}_bg_worker.disable_schedule_only_jobs */
			ProcessConfigFile(PGC_SIGHUP);
			BackgroundWorkerReloadConfig = false;
		}

		waitResult = WaitLatch(&BackgroundWorkerShmem->latch,
							   WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
							   latchTimeOut * ONE_SEC_IN_MS,
							   WAIT_EVENT_PG_SLEEP);
		ResetLatch(&BackgroundWorkerShmem->latch);

		/* An interrupt might have taken place during the waiting process. */
		CHECK_FOR_INTERRUPTS();

#if PG_VERSION_NUM >= 180000
		ProcessMainLoopInterrupts();
#else
		HandleMainLoopInterrupts();
#endif

		if (waitResult & WL_LATCH_SET)
		{
			/* Event received for latch */
		}

		if (waitResult & WL_TIMEOUT)
		{
			/* Event received for schedules */
			ManageJobsLifeCycle(jobExecutions, ApiBgWorkerRole, databaseName);
		}

		latchTimeOut = LatchTimeOutSec;
	}

	if (jobExecutions != NIL)
	{
		/* Cleanup lists */
		FreeJobExecutions(jobExecutions);
		jobExecutions = NIL;
	}

	/* when sigterm comes, try cancel all currently open connections */
	ereport(LOG, (errmsg("%s is currently shutting down.",
						 ExtensionBackgroundWorkerLeaderName)));
}


/*
 * Registers a command that jobs are allowed to execute.
 */
void
RegisterBackgroundWorkerJobAllowedCommand(BackgroundWorkerJobCommand command)
{
	if (!process_shared_preload_libraries_in_progress)
	{
		ereport(ERROR, (errmsg(
							"Registering a new background worker allowed command must happen during shared_preload_libraries")));
	}

	if (AllowedCommandEntries >= MAX_BACKGROUND_WORKER_ALLOWED_COMMANDS)
	{
		ereport(ERROR,
				(errmsg("Only %d background worker allowed commands are permitted",
						MAX_BACKGROUND_WORKER_ALLOWED_COMMANDS)));
	}

	AllowedCommandRegistry[AllowedCommandEntries++] = command;
}


/*
 * Registers a job to be executed periodically.
 */
void
RegisterBackgroundWorkerJob(BackgroundWorkerJob job)
{
	if (!process_shared_preload_libraries_in_progress)
	{
		ereport(ERROR, (errmsg(
							"Registering a new background worker job must happen during shared_preload_libraries")));
	}

	if (!EnableBackgroundWorker)
	{
		ereport(ERROR, (errmsg(
							"Cannot register background worker job when background worker is disabled")));
	}

	if (JobEntries >= MAX_BACKGROUND_WORKER_JOBS)
	{
		ereport(ERROR,
				(errmsg("Only %d background worker jobs are permitted",
						MAX_BACKGROUND_WORKER_JOBS)));
	}

	if (job.get_schedule_interval_in_seconds_hook == NULL)
	{
		/*
		 * If the hook is not set, use the default schedule interval.
		 * Useful for jobs that do not require dynamic scheduling.
		 */
		job.get_schedule_interval_in_seconds_hook = GetDefaultScheduleIntervalInSeconds;
	}

	/* Fails if job is not valid. */
	ValidateJob(job);

	JobRegistry[JobEntries++] = job;
}


/*
 * ManageJobsLifeCycle walks through the list of jobs and takes action based on their state.
 */
static void
ManageJobsLifeCycle(List *jobExecutions, char *userName, char *databaseName)
{
	TimestampTz currentTime = GetCurrentTimestamp();
	ListCell *jobExecCell = NULL;

	/*
	 * Manages each job execution's state. Execute if possible and
	 * complete when done.
	 */
	foreach(jobExecCell, jobExecutions)
	{
		BackgroundWorkerJobExecution *jobExec = (BackgroundWorkerJobExecution *) lfirst(
			jobExecCell);

		/* Cancels job in case the job is running is open and timeout was reached. */
		CancelJobIfTimeIsUp(jobExec, currentTime);

		/* Check if job completed in case the job is running. */
		CheckJobCompletion(jobExec);

		/* Executes job if it hasn't started and the scheduled interval was reached. */
		if (CanExecuteJob(jobExec, currentTime))
		{
			ExecuteJob(jobExec, userName, databaseName, currentTime);
		}
	}
}


/*
 * Checks if a given job is eligible to start.
 */
static bool
CanExecuteJob(BackgroundWorkerJobExecution *jobExec, TimestampTz currentTime)
{
	if (jobExec->job.toBeExecutedOnMetadataCoordinatorOnly &&
		!CheckIfMetadataCoordinator())
	{
		/* Do not run the job (marked to be run on coordinator only) on worker */
		return false;
	}

	int scheduleIntervalInSeconds = jobExec->job.get_schedule_interval_in_seconds_hook();

	/*
	 * Executions do not start from t0, they always start from t0 + interval.
	 * We are assuming that job schedule intervals are a multiple of LatchTimeoutSec, therefore we do
	 * not have to handle odd intervals such as LatchTimeoutSec of 10 seconds and job interval of 15 seconds.
	 */
	return jobExec->state == JOB_IDLE &&
		   scheduleIntervalInSeconds > 0 &&
		   TimestampDifferenceExceeds(jobExec->lastStartTime, currentTime,
									  scheduleIntervalInSeconds * ONE_SEC_IN_MS);
}


/*
 * Checks if job execution completed by using the LibPQ API.
 * If positive, closes the job PG connection and resets it.
 */
static void
CheckJobCompletion(BackgroundWorkerJobExecution *jobExec)
{
	PGconn *conn = jobExec->connection;
	if (jobExec->state == JOB_IDLE)
	{
		return;
	}

	/* Checks if command is busy. If not, close connection and reset it. */
	PG_TRY();
	{
		if (PQconsumeInput(conn) == 0)
		{
			PGConnReportError(conn, NULL, ERROR);
		}

		if (!PQisBusy(conn))
		{
			PQfinish(conn);
			jobExec->connection = NULL;
			jobExec->state = JOB_IDLE;
		}
	}
	PG_CATCH();
	{
		/* Clear error context since we don't use it. */
		FlushErrorState();

		/* We fail gracefuly and close the connection. */
		PQfinish(conn);

		/* Set state to idle so it can run in the next iteration. */
		jobExec->connection = NULL;
		jobExec->state = JOB_IDLE;

		ereport(WARNING, (errmsg(
							  "Failed to execute background worker job %s with id %d. Could not consume input from the connection.",
							  jobExec->job.jobName, jobExec->job.jobId)));
	}
	PG_END_TRY();
}


/*
 * Wait until the background worker prerequisistes are met. We currently wait
 * for the BackgroundWorkerRole to be created.
 */
static void
WaitForBackgroundWorkerDependencies(void)
{
	int waitResult;
	int waitTimeoutInSec = 10;
	bool roleExists = false;

	while (!roleExists && !got_sigterm)
	{
		waitResult = 0;
		waitResult = WaitLatch(&BackgroundWorkerShmem->latch,
							   WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
							   waitTimeoutInSec * ONE_SEC_IN_MS,
							   WAIT_EVENT_PG_SLEEP);
		ResetLatch(&BackgroundWorkerShmem->latch);

		/* An interrupt might have taken place during the waiting process. */
		CHECK_FOR_INTERRUPTS();

#if PG_VERSION_NUM >= 180000
		ProcessMainLoopInterrupts();
#else
		HandleMainLoopInterrupts();
#endif

		if (waitResult & WL_TIMEOUT)
		{
			/* Check if background worker start condition is met. */
			const char *roleName = ApiBgWorkerRole;
			roleExists = CheckIfRoleExists(roleName);
			if (!roleExists)
			{
				ereport(WARNING, errmsg("BackgroundWorkerRole %s does not exist.",
										roleName));
			}
		}
	}
}


/*
 * Executes job command through LibPQ.
 */
static void
ExecuteJob(BackgroundWorkerJobExecution *jobExec, char *userName, char *databaseName,
		   TimestampTz currentTime)
{
	PGconn *conn = NULL;
	StringInfo localhostConnStr = makeStringInfo();

	/*
	 * The job execution consists of creating a LibPQ connection an sending its
	 * command query through it. In case of failure the connection is closed and
	 * is not assigned to the job.
	 */
	PG_TRY();
	{
		appendStringInfo(localhostConnStr,
						 "%s port=%d user=%s dbname=%s application_name='%s'",
						 LocalhostConnectionString, PostPortNumber,
						 userName,
						 databaseName,
						 jobExec->job.jobName);

		char *connStr = localhostConnStr->data;

		conn = PQconnectStart(connStr);
		if (conn == NULL)
		{
			/*
			 * We don't expect PQconnectStart to return NULL unless OOM happened.
			 */
			ereport(ERROR, (errmsg(
								"could not establish connection during background job execution, possibly "
								"due to OOM")));
		}

		const int argNonBlocking = 1;
		PQsetnonblocking(conn, argNonBlocking);

		PGConnFinishConnectionEstablishment(conn);

		if (PQstatus(conn) != CONNECTION_OK)
		{
			PGConnReportError(conn, NULL, ERROR);
		}

		const char *query = jobExec->commandQuery;

		/* We currently limit the number of arguments to be at most 1. */
		int nParams = jobExec->job.argument.isNull ? 0 : 1;
		Oid paramTypes[1] = { jobExec->job.argument.argType };
		const char *parameterValues[1] = { jobExec->job.argument.argValue };

		/* Result in text format. */
		int resultFormat = 0;

		/* We try to send the query. If it fails, report error and retry on the next latch event. */
		if (!PQsendQueryParams(conn, query, nParams, paramTypes, parameterValues, NULL,
							   NULL,
							   resultFormat))
		{
			PGConnReportError(conn, NULL, ERROR);
		}

		/* Query was sent successfuly. Assign connection to job. */
		jobExec->connection = conn;
		jobExec->state = JOB_RUNNING;
		jobExec->lastStartTime = currentTime;
	}
	PG_CATCH();
	{
		/* Clear error context since we don't use it. */
		FlushErrorState();

		/* We fail gracefuly and only check if the connection needs to be closed. */
		if (conn != NULL)
		{
			PQfinish(conn);
		}

		/* Set state to idle so it can run in the next iteration. */
		jobExec->state = JOB_IDLE;

		ereport(WARNING, (errmsg(
							  "Failed to execute background worker job id %d. Could not establish connection and send query.",
							  jobExec->job.jobId)));
	}
	PG_END_TRY();

	pfree(localhostConnStr->data);
}


/*
 * CancelJobIfTimeIsUp cancels the running job if its timeout was reached i.e. BackgroundWorkerjob.timeoutInSeconds.
 * If connectionTimeout <= 0 OR the job has no active connection, do nothing.
 */
static void
CancelJobIfTimeIsUp(BackgroundWorkerJobExecution *jobExec, TimestampTz currentTime)
{
	int timeoutInSeconds = jobExec->job.timeoutInSeconds;
	PGconn *conn = jobExec->connection;
	if (jobExec->state == JOB_IDLE ||
		timeoutInSeconds <= 0)
	{
		return;
	}

	if (TimestampDifferenceExceeds(
			jobExec->lastStartTime,
			currentTime,
			timeoutInSeconds * ONE_SEC_IN_MS))
	{
		if (PGConnXactIsActive(conn))
		{
			PGConnTryCancel(conn);
		}

		PQfinish(conn);
		jobExec->connection = NULL;
		jobExec->state = JOB_IDLE;

		ereport(LOG, (errmsg(
						  "Canceled background worker job %s with id %d because of connection timeout of %d seconds.",
						  jobExec->job.jobName, jobExec->job.jobId, timeoutInSeconds)));
	}
}


/*
 * ValidateJob validates a background worker job object and fails if it's not valid
 */
static void
ValidateJob(BackgroundWorkerJob job)
{
	if (job.jobName == NULL || job.jobName[0] == '\0')
	{
		ereport(ERROR, (errmsg("Background worker job name can not be NULL")));
	}

	if (job.command.name == NULL || job.command.name[0] == '\0')
	{
		ereport(ERROR, (errmsg("Background worker job command name can not be NULL")));
	}

	if (job.command.schema == NULL || job.command.schema[0] == '\0')
	{
		ereport(ERROR, (errmsg("Background worker job command schema can not be NULL")));
	}

	if (job.argument.isNull == false && (job.argument.argType == 0 ||
										 job.argument.argValue == NULL))
	{
		ereport(ERROR, (errmsg(
							"Background worker job argument can not be NULL when isnull is set to false.")));
	}

	const int scheduleIntervalInSeconds = job.get_schedule_interval_in_seconds_hook();

	if (scheduleIntervalInSeconds <= 0 ||
		scheduleIntervalInSeconds < LatchTimeOutSec ||
		scheduleIntervalInSeconds % LatchTimeOutSec != 0)
	{
		ereport(ERROR, (errmsg(
							"Schedule interval of background worker job \'%s\' is either <= 0 "
							"or less than value of latch_timeout=%d "
							"or not a multiple of latch_timeout=%d",
							job.jobName, LatchTimeOutSec, LatchTimeOutSec)));
	}

	/* This is added because we rely on TimestampDifferenceExceeds to find whether to schedule the job which takes int (time in ms) */
	int threshold = (int) INT_MAX / ONE_SEC_IN_MS;
	if (scheduleIntervalInSeconds > threshold)
	{
		ereport(ERROR, (errmsg(
							"Schedule interval of background worker job \'%s\' cannot be larger than %d seconds",
							job.jobName, threshold)));
	}

	/* Enforce that job timeout cannot be less or equal to 0 seconds. */
	if (job.timeoutInSeconds <= 0)
	{
		ereport(ERROR, (errmsg(
							"Timeout of background worker job \'%s\' cannot be <= 0 seconds",
							job.jobName)));
	}

	/* Enforce that job timeout cannot be larger than threshold. */
	if (job.timeoutInSeconds > BackgroundWorkerJobTimeoutThresholdSec)
	{
		ereport(ERROR, (errmsg(
							"Timeout of background worker job \'%s\' cannot be larger than %d seconds",
							job.jobName, BackgroundWorkerJobTimeoutThresholdSec)));
	}

	if (!CheckIfJobCommandIsAllowed(job.command))
	{
		ereport(ERROR, (errmsg("Background worker job command is not allowed")));
	}
}


/*
 * Checks if the given command is allowed to be executed. We keep a hardcoded list of
 * allowed commands to safekeep the background worker job framework.
 */
static bool
CheckIfJobCommandIsAllowed(BackgroundWorkerJobCommand command)
{
	for (int i = 0; i < AllowedCommandEntries; i++)
	{
		BackgroundWorkerJobCommand allowedCommand = AllowedCommandRegistry[i];

		/* Return true if the job is present in the allowed commands list. */
		if (strcmp(allowedCommand.name, command.name) == 0 &&
			strcmp(allowedCommand.schema, command.schema) == 0)
		{
			return true;
		}
	}

	return false;
}


/*
 * Iterates JobRegistry array and returns a List of BackgroundWorkerJobExecution.
 * There's a 1:1 match between both entities.
 */
static List *
GenerateJobExecutions(void)
{
	List *jobExecutions = NIL;

	for (int i = 0; i < JobEntries; i++)
	{
		BackgroundWorkerJobExecution *jobExec = CreateJobExecutionObj(JobRegistry[i]);

		/*
		 * Check for nullity. NULL is returned if an error happened while creating
		 * a BackgroundWorkerJobExecution from a BackgroundWorkerJob.
		 */
		if (jobExec == NULL)
		{
			ereport(WARNING, (errmsg(
								  "Skipping background worker job %s with id %d because an execution instance could not be generated.",
								  JobRegistry[i].jobName, JobRegistry[i].jobId)));
		}
		else
		{
			jobExecutions = lappend(jobExecutions, jobExec);
		}
	}

	return jobExecutions;
}


/*
 * Receives a background worker job and returns an background worker job execution.
 * object. We need it to keep track of execution states and database connection.
 */
static BackgroundWorkerJobExecution *
CreateJobExecutionObj(BackgroundWorkerJob job)
{
	BackgroundWorkerJobExecution *jobExec = NULL;
	char *commandQuery = NULL;

	commandQuery = GenerateCommandQuery(job, CurrentMemoryContext);
	if (commandQuery == NULL)
	{
		return NULL;
	}

	jobExec = palloc(sizeof(BackgroundWorkerJobExecution));
	jobExec->lastStartTime = GetCurrentTimestamp();
	jobExec->job = job;
	jobExec->connection = NULL;
	jobExec->commandQuery = commandQuery;
	jobExec->state = JOB_IDLE;

	return jobExec;
}


/*
 * Cleaning up list objects and its contents.
 */
static void
FreeJobExecutions(List *jobExecutions)
{
	ListCell *jobExecCell = NULL;
	foreach(jobExecCell, jobExecutions)
	{
		BackgroundWorkerJobExecution *jobExec = (BackgroundWorkerJobExecution *) lfirst(
			jobExecCell);

		/* Close PG connection if not NULL. */
		if (jobExec->connection != NULL)
		{
			PQfinish(jobExec->connection);
			jobExec->connection = NULL;
		}
	}
	list_free_deep(jobExecutions);
	jobExecutions = NIL;
}


/*
 * Checks if current node is the coordinator.
 */
static bool
CheckIfMetadataCoordinator(void)
{
	if (IsCoordinator == BackgroundWorkerBoolOption_Undefined)
	{
		SetCurrentStatementStartTimestamp();
		StartTransactionCommand();
		PushActiveSnapshot(GetTransactionSnapshot());

		IsCoordinator = IsMetadataCoordinator() ?
						BackgroundWorkerBoolOption_True :
						BackgroundWorkerBoolOption_False;

		PopActiveSnapshot();
		CommitTransactionCommand();
	}

	return IsCoordinator == BackgroundWorkerBoolOption_True;
}


/*
 * Generate a SQL command string for a background worker job.
 */
static char *
GenerateCommandQuery(BackgroundWorkerJob job, MemoryContext stableContext)
{
	SetCurrentStatementStartTimestamp();
	PopAllActiveSnapshots();
	StartTransactionCommand();
	PushActiveSnapshot(GetTransactionSnapshot());

	MemoryContext oldMemContext = CurrentMemoryContext;

	/* declared volatile because of the longjmp in PG_CATCH */
	volatile char *commandQuery = NULL;
	volatile int errorCode = 0;
	volatile ErrorData *edata = NULL;

	PG_TRY();
	{
		/* Build ObjectWithArgs structure for LookupFuncWithArgs */
		ObjectWithArgs *funcWithArgs = makeNode(ObjectWithArgs);
		funcWithArgs->objname = list_make2(makeString(pstrdup(job.command.schema)),
										   makeString(pstrdup(job.command.name)));
		funcWithArgs->args_unspecified = false;

		if (job.argument.isNull)
		{
			funcWithArgs->objargs = NIL;
		}
		else
		{
			TypeName *argTypeName = makeTypeNameFromOid(job.argument.argType, -1);
			funcWithArgs->objargs = list_make1(argTypeName);
		}
		funcWithArgs->objfuncargs = NIL;

		bool missingOK = true;

		/* Use LookupFuncWithArgs with OBJECT_ROUTINE to find both functions and procedures */
		Oid functionOid =
			LookupFuncWithArgs(OBJECT_ROUTINE, funcWithArgs, missingOK);

		if (!OidIsValid(functionOid))
		{
			ereport(ERROR, (errmsg(
								"Failed to process background worker job %s with id %d. Could not find command in catalog.",
								job.jobName, job.jobId)));
		}

		char procType = get_func_prokind(functionOid);

		/* The command prefix changes depending on the procType (Function or Procedure). */
		char *commandPrefix = procType == 'p' ? "CALL" : "SELECT";
		char *parameter = job.argument.isNull ? "" : "$1";
		char *tempQuery = psprintf("%s %s.%s(%s);", commandPrefix, job.command.schema,
								   job.command.name, parameter);

		/* Switch to the stable memory context and copy the query string there BEFORE committing */
		MemoryContextSwitchTo(stableContext);
		commandQuery = pstrdup(tempQuery);
		MemoryContextSwitchTo(oldMemContext);

		PopActiveSnapshot();
		CommitTransactionCommand();
	}
	PG_CATCH();
	{
		MemoryContextSwitchTo(stableContext);
		edata = CopyErrorDataAndFlush();
		errorCode = edata->sqlerrcode;
		MemoryContextSwitchTo(oldMemContext);

		ereport(LOG, (errcode(errorCode),
					  errmsg(
						  "couldn't construct command for the background worker job execution:"
						  "file: %s, line: %d, message_id: %s",
						  edata->filename, edata->lineno, edata->message_id)));

		PopAllActiveSnapshots();
		AbortCurrentTransaction();
	}
	PG_END_TRY();

	return (char *) commandQuery;
}


/*
 * Report shared-memory space needed by BackgroundWorkerShmemInit
 */
static Size
BackgroundWorkerShmemSize(void)
{
	Size size;
	size = sizeof(BackgroundWorkerShmemStruct);
	size = MAXALIGN(size);
	return size;
}


/*
 * BackgroundWorkerShmemInit
 * Allocate and initialize Background worker-related shared memory
 */
static void
BackgroundWorkerShmemInit(void)
{
	bool found;
	BackgroundWorkerShmem = (BackgroundWorkerShmemStruct *) ShmemInitStruct(
		"DocumentDB Background Worker data",
		BackgroundWorkerShmemSize(),
		&found);
	if (!found)
	{
		/* First time through, so initialize */
		MemSet(BackgroundWorkerShmem, 0, BackgroundWorkerShmemSize());
		InitSharedLatch(&BackgroundWorkerShmem->latch);
	}
}


/*
 * Set on-detach hook so that our PID will be cleared on exit.
 */
static void
BackgroundWorkerKill(int code, Datum arg)
{
	Assert(BackgroundWorkerShmem != NULL);

	/*
	 * Clear BackgroundWorkerShmem first; then disown the latch.  This is so that signal
	 * handlers won't try to touch the latch after it's no longer ours.
	 */
	BackgroundWorkerShmemStruct *backgroundWorkerShmem = BackgroundWorkerShmem;
	BackgroundWorkerShmem = NULL;
	DisownLatch(&backgroundWorkerShmem->latch);
}


/*
 * Searches PG role in SysCache. Returns true if found.
 */
static bool
CheckIfRoleExists(const char *roleName)
{
	if (roleName == NULL)
	{
		return false;
	}

	SetCurrentStatementStartTimestamp();
	StartTransactionCommand();
	PushActiveSnapshot(GetTransactionSnapshot());

	bool missingOk = true;
	Oid roleId = get_role_oid(roleName, missingOk);

	PopActiveSnapshot();
	CommitTransactionCommand();

	return OidIsValid(roleId);
}


/*
 * Signal handler for SIGTERM
 * Set a flag to let the main loop to terminate, and set our latch to wake
 * it up.
 */
static void
background_worker_sigterm(SIGNAL_ARGS)
{
	got_sigterm = true;
	ereport(LOG,
			(errmsg("Terminating \"%s\" due to administrator command",
					ExtensionBackgroundWorkerLeaderName)));

	if (BackgroundWorkerShmem != NULL)
	{
		SetLatch(&BackgroundWorkerShmem->latch);
	}
}


/*
 * Signal handler for SIGHUP
 */
static void
background_worker_sighup(SIGNAL_ARGS)
{
	BackgroundWorkerReloadConfig = true;
	if (BackgroundWorkerShmem != NULL)
	{
		SetLatch(&BackgroundWorkerShmem->latch);
	}
}
