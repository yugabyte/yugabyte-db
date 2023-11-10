/*-------------------------------------------------------------------------
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "postmaster/bgworker.h"

#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/shmem.h"

#include "utils/guc.h"
#include "utils/timestamp.h"

#include "miscadmin.h"
#include "pgstat.h"

PG_MODULE_MAGIC;
PG_FUNCTION_INFO_V1(yb_active_universe_history);

/* GUC variables */
static int circular_buffer_size;
static int auh_sampling_interval_ms;
static int auh_sample_size;

/* Saved hook values in case of unload */
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;

typedef struct YbAuhEntry
{
	TimestampTz	auh_sample_time;
	uint32		wait_event;
	char		wait_event_aux[16];
	float8		sample_rate;
} YbAuhEntry;

typedef struct YbAuh
{
	LWLock	   *lock;			/* Protects the circular buffer */
	int			index;			/* Index to insert new buffer entry */
	int			max_entries;	/* Maximum # of entries in the buffer */
	YbAuhEntry	circular_buffer[FLEXIBLE_ARRAY_MEMBER];
} YbAuh;

YbAuh *yb_auh = NULL;

void _PG_init(void);
void _PG_fini(void);

static int yb_auh_cb_max_entries(void);
static Size yb_auh_memsize(void);
static void yb_auh_startup(void);

static void yb_auh_startup_hook(void);

static volatile sig_atomic_t got_sigterm = false;
static volatile sig_atomic_t got_sighup = false;

void yb_auh_main(Datum);

/*
 * Module load callback
 */
void
_PG_init(void)
{
	if (!process_shared_preload_libraries_in_progress)
		return;

	DefineCustomIntVariable("yb_auh.circular_buffer_size",
							"Size of the circular buffer that stores wait events",
							NULL,
							&circular_buffer_size,
							16 * 1024,
							0,
							INT_MAX,
							PGC_POSTMASTER,
							GUC_NO_SHOW_ALL | GUC_NO_RESET_ALL |
							GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE |
							GUC_UNIT_KB,
							NULL, NULL, NULL);

	DefineCustomIntVariable("yb_auh.sampling_interval",
							"Duration between each sample",
							NULL,
							&auh_sampling_interval_ms,
							1000,
							1,
							INT_MAX,
							PGC_SUSET,
							GUC_UNIT_MS,
							NULL, NULL, NULL);

	DefineCustomIntVariable("yb_auh.sample_size",
							"Number of wait events captured in each sample",
							NULL,
							&auh_sample_size,
							500,
							0,
							INT_MAX,
							PGC_SUSET,
							0,
							NULL, NULL, NULL);

	EmitWarningsOnPlaceholders("yb_auh");

	RequestAddinShmemSpace(yb_auh_memsize());
	RequestNamedLWLockTranche("yb_auh", 1);

	BackgroundWorker worker;
	memset(&worker, 0, sizeof(worker));
	sprintf(worker.bgw_name, "yb_auh collector");
	sprintf(worker.bgw_type, "yb_auh collector");
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS;
	worker.bgw_start_time = BgWorkerStart_PostmasterStart;
	/* Value of 1 allows the background worker for yb_auh to restart */
	worker.bgw_restart_time = 1;
	sprintf(worker.bgw_library_name, "yb_auh");
	sprintf(worker.bgw_function_name, "yb_auh_main");
	worker.bgw_main_arg = (Datum) 0;
	worker.bgw_notify_pid = 0;
	RegisterBackgroundWorker(&worker);

	/*
	 * Install hooks.
	 */
	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = yb_auh_startup_hook;
}

/*
 * Module unload callback
 */
void
_PG_fini(void)
{
	/* Uninstall hooks. */
	shmem_startup_hook = prev_shmem_startup_hook;
}

static void
yb_auh_startup_hook(void)
{
	if (prev_shmem_startup_hook)
		prev_shmem_startup_hook();

	yb_auh_startup();
}

static int
yb_auh_cb_max_entries(void)
{
	return circular_buffer_size * 1024 / sizeof(YbAuhEntry);
}

static Size
yb_auh_memsize(void)
{
	Size		size;

	size = offsetof(YbAuh, circular_buffer);
	size = add_size(size, mul_size(yb_auh_cb_max_entries(),
								   sizeof(YbAuhEntry)));

	return size;
}

static void
yb_auh_startup(void)
{
	bool		found = false;

	yb_auh = ShmemInitStruct("yb_auh_circular_buffer",
							 yb_auh_memsize(),
							 &found);
	if (!found)
	{
		yb_auh->lock = &(GetNamedLWLockTranche("yb_auh"))->lock;
		yb_auh->index = 0;
		yb_auh->max_entries = yb_auh_cb_max_entries();
	}
}

static void
yb_auh_sigterm(SIGNAL_ARGS)
{
	int			save_errno = errno;

	got_sigterm = true;
	SetLatch(MyLatch);

	errno = save_errno;
}

static void
yb_auh_sighup(SIGNAL_ARGS)
{
	int			save_errno = errno;

	got_sighup = true;
	SetLatch(MyLatch);

	errno = save_errno;
}

void
yb_auh_main(Datum main_arg)
{
	ereport(LOG,
			(errmsg("starting bgworker yb_auh collector with max buffer entries %d",
					yb_auh->max_entries)));

	/* Register functions for SIGTERM/SIGHUP management */
	pqsignal(SIGHUP, yb_auh_sighup);
	pqsignal(SIGTERM, yb_auh_sigterm);

	/* We're now ready to receive signals */
	BackgroundWorkerUnblockSignals();

	pgstat_report_appname("yb_auh collector");

	while (!got_sigterm)
	{
		int rc;
		/* Wait necessary amount of time */
		rc = WaitLatch(MyLatch, WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
					   auh_sampling_interval_ms, PG_WAIT_EXTENSION);
		ResetLatch(MyLatch);

		/* Bailout if postmaster has died */
		if (rc & WL_POSTMASTER_DEATH)
			proc_exit(1);

		/* Process signals */
		if (got_sighup)
		{
			/* Process config file */
			got_sighup = false;
			ProcessConfigFile(PGC_SIGHUP);
			ereport(LOG,
					(errmsg("bgworker yb_auh signal: processed SIGHUP")));
		}

		/* TODO(asaha): poll Tserver and PG wait events */
	}
	proc_exit(0);
}
