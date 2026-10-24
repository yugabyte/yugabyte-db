/*-------------------------------------------------------------------------
 *
 * yb_oom_cancel.c
 *	  Canceling a statement at the request of a userspace OOM killer.
 *
 * When the node runs low on memory, a userspace OOM killer (earlyoom) asks
 * the largest backend to give memory back by sending it SIGUSR1 through
 * sigqueue() with YB_OOM_CANCEL_SIGVAL.  The backend logs a heap snapshot,
 * records the statement in yb_terminated_queries, and fails it with an
 * out-of-memory error, as if malloc had returned NULL.  The session survives.
 * If memory does not recover, the OOM killer falls back to killing the
 * process.
 *
 * SIGUSR1 is used because a plain SIGUSR1 is harmless to every postgres
 * process: procsignal_sigusr1_handler acts only on reasons flagged in shared
 * memory, which an outside process cannot set.  PostgreSQL itself sends
 * SIGUSR1 with kill(), so si_code SI_QUEUE plus the value identifies the OOM
 * killer's request.  Processes that do not know the protocol, including
 * backends of older releases, ignore it, and the OOM killer escalates.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *	  src/backend/utils/misc/yb_oom_cancel.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <signal.h>

#include "miscadmin.h"
#include "storage/proc.h"
#include "storage/procsignal.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb_oom_cancel.h"
#include "yb_terminated_queries.h"

volatile sig_atomic_t YbOomCancelPending = false;

static volatile sig_atomic_t oom_cancel_sender_pid = 0;
static bool release_free_memory_pending = false;

static void
yb_sigusr1_handler(int signo, siginfo_t *info, void *context)
{
	if (info->si_code == SI_QUEUE &&
		info->si_value.sival_int == YB_OOM_CANCEL_SIGVAL)
	{
		oom_cancel_sender_pid = info->si_pid;
		YbOomCancelPending = true;
		InterruptPending = true;
	}

	/* Handles PostgreSQL's own reasons and sets MyLatch. */
	procsignal_sigusr1_handler(signo);
}

/*
 * Replace the SIGUSR1 handler with one that also accepts OOM cancel requests.
 * Call it after the process has installed procsignal_sigusr1_handler.
 */
void
YbSetupOomCancelSignalHandler(void)
{
	struct sigaction act;

	act.sa_sigaction = yb_sigusr1_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO | SA_RESTART;
	if (sigaction(SIGUSR1, &act, NULL) < 0)
		elog(WARNING, "could not install SIGUSR1 handler: %m");
}

/*
 * Called from ProcessInterrupts() when an OOM cancel request is pending.
 * Follows the same rules as a query cancel: wait until the backend is not
 * reading a client message, and do not fail anything when it is idle.
 */
void
YbProcessOomCancelInterrupt(bool doing_command_read)
{
	int			sender_pid = oom_cancel_sender_pid;
	char		reason[128];

	if (QueryCancelHoldoffCount != 0)
	{
		/* Retry as soon as the message has been read. */
		InterruptPending = true;
		return;
	}

	YbOomCancelPending = false;

	if (doing_command_read)
	{
		/*
		 * No statement to cancel.  Caches stay, but memory freed by earlier
		 * statements can still go back to the OS.
		 */
		ereport(LOG,
				(errmsg("idle backend received OOM cancel request from PID %d, releasing free memory",
						sender_pid)));
		YBCTCMallocReleaseFreeMemory(PG_INT64_MAX);
		return;
	}

	ereport(LOG,
			(errmsg("canceling statement due to OOM cancel request from PID %d, heap snapshot follows",
					sender_pid)));
	YBCDumpTcMallocHeapProfile(false /* peak_heap */ , 100);

	snprintf(reason, sizeof(reason),
			 "canceled by userspace OOM killer (PID %d)", sender_pid);
	yb_report_query_termination(reason, MyProcPid);

	/*
	 * Aborting the transaction frees its memory, but TCMalloc keeps the pages.
	 * Have the error recovery path return them, or RSS does not drop and the
	 * OOM killer escalates anyway.
	 */
	release_free_memory_pending = true;

	LockErrorCleanup();
	ereport(ERROR,
			(errcode(ERRCODE_OUT_OF_MEMORY),
			 errmsg("out of memory"),
			 errdetail("The statement was canceled by the userspace OOM killer (PID %d) because the node is low on memory.",
					   sender_pid)));
}

/*
 * Called by the top-level error recovery after the transaction is aborted.
 */
void
YbReleaseFreeMemoryAfterOomCancel(void)
{
	if (!release_free_memory_pending)
		return;

	release_free_memory_pending = false;
	YBCTCMallocReleaseFreeMemory(PG_INT64_MAX);
}
