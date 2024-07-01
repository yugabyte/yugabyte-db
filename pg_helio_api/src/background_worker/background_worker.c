/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/backgroundworker/helio_background_worker.c
 *
 * Implementation of Background worker.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <catalog/pg_extension.h>
#include <nodes/pg_list.h>
#include <tcop/utility.h>
#include <postmaster/interrupt.h>
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

#include "commands/connection_management.h"
#include "metadata/metadata_cache.h"
#include "api_hooks.h"

#define ONE_SEC_IN_MS 1000L

/*
 * The main background worker shmem struct.  On shared memory we store this main
 * struct. This struct keeps:
 *
 * latch	Sharable latch
 */
typedef struct BackgroundWorkerShmemStruct
{
	Latch latch;
} BackgroundWorkerShmemStruct;

PGDLLEXPORT void HelioBackgroundWorkerMain(Datum);
extern char *BackgroundWorkerDatabaseName;
extern int LatchTimeOutSec;

static const char *HelioBackgroundWorkerLeaderName = "helio_bg_worker_leader";
static bool BackgroundWorkerReloadConfig = false;

/* Shared memory segment for BackgroundWorker */
static BackgroundWorkerShmemStruct *BackgroundWorkerShmem;
static Size BackgroundWorkerShmemSize(void);
static void BackgroundWorkerShmemInit(void);
static void BackgroundWorkerKill(int code, Datum arg);

/* flags set by signal handlers */
static volatile sig_atomic_t got_sigterm = false;
static void background_worker_sigterm(SIGNAL_ARGS);
static void background_worker_sighup(SIGNAL_ARGS);


/*
 * Helio background worker entry point.
 */
void
HelioBackgroundWorkerMain(Datum main_arg)
{
	char *databaseName = BackgroundWorkerDatabaseName;

	/* Establish signal handlers before unblocking signals. */
	pqsignal(SIGINT, SIG_IGN);
	pqsignal(SIGTERM, background_worker_sigterm);
	pqsignal(SIGHUP, background_worker_sighup);

	/* We're now ready to receive signals */
	BackgroundWorkerUnblockSignals();
	BackgroundWorkerInitializeConnection(databaseName, NULL, 0);

	ereport(LOG, (errmsg("Starting %s with databaseName %s",
						 HelioBackgroundWorkerLeaderName, databaseName)));
	pgstat_report_appname(HelioBackgroundWorkerLeaderName);

	/* Own the latch once everything is ready */
	BackgroundWorkerShmemInit();
	OwnLatch(&BackgroundWorkerShmem->latch);

	/* Set on-detach hook so that our PID will be cleared on exit. */
	on_shmem_exit(BackgroundWorkerKill, 0);

	/*
	 * Main loop: do this until SIGTERM is received and processed by
	 * ProcessInterrupts.
	 */

	int waitResult;
	int latchTimeOut = LatchTimeOutSec;

	while (!got_sigterm)
	{
		/*
		 * Background workers mustn't call usleep() or any direct equivalent:
		 * instead, they may wait on their process latch, which sleeps as
		 * necessary, but is awakened if postmaster dies.  That way the
		 * background process goes away immediately in an emergency.
		 */
		waitResult = 0;
		if (BackgroundWorkerReloadConfig)
		{
			/* read the latest value of helio_bg_worker.disable_schedule_only_jobs */
			ProcessConfigFile(PGC_SIGHUP);
			BackgroundWorkerReloadConfig = false;
		}

		waitResult = WaitLatch(&BackgroundWorkerShmem->latch,
							   WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
							   latchTimeOut * ONE_SEC_IN_MS,
							   WAIT_EVENT_PG_SLEEP);
		ResetLatch(&BackgroundWorkerShmem->latch);

		/* An interrupt may have occurred while we were waiting. */
		CHECK_FOR_INTERRUPTS();

		if (waitResult & WL_LATCH_SET)
		{
			/* Event received for latch */
		}

		if ((waitResult & WL_TIMEOUT))
		{
			/* Event received for schedules */
		}

		latchTimeOut = LatchTimeOutSec;
	}

	/* when sigterm comes, try cancel all currently open connections */
	ereport(LOG, (errmsg("%s is shutting down.",
						 HelioBackgroundWorkerLeaderName)));
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
 *		Allocate and initialize Background worker-related shared memory
 */
static void
BackgroundWorkerShmemInit(void)
{
	bool found;
	BackgroundWorkerShmem = (BackgroundWorkerShmemStruct *) ShmemInitStruct(
		"Helio Background Worker data",
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
 * Signal handler for SIGTERM
 *		Set a flag to let the main loop to terminate, and set our latch to wake
 *		it up.
 */
static void
background_worker_sigterm(SIGNAL_ARGS)
{
	got_sigterm = true;
	ereport(LOG,
			(errmsg("Terminating \"%s\" due to administrator command",
					HelioBackgroundWorkerLeaderName)));

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
