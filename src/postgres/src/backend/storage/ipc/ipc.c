/*-------------------------------------------------------------------------
 *
 * ipc.c
 *	  POSTGRES inter-process communication definitions.
 *
 * This file is misnamed, as it no longer has much of anything directly
 * to do with IPC.  The functionality here is concerned with managing
 * exit-time cleanup for either a postmaster or a backend.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/storage/ipc/ipc.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <signal.h>
#include <unistd.h>
#include <stdatomic.h>
#include <sys/stat.h>

#include "miscadmin.h"
#ifdef PROFILE_PID_DIR
#include "postmaster/autovacuum.h"
#endif
#include "storage/dsm.h"
#include "storage/ipc.h"
#include "tcop/tcopprot.h"

#include "pg_yb_utils.h"

/*
 * This flag is set during proc_exit() to change ereport()'s behavior,
 * so that an ereport() from an on_proc_exit routine cannot get us out
 * of the exit procedure.  We do NOT want to go back to the idle loop...
 */
atomic_bool proc_exit_inprogress = false;

/*
 * Set when shmem_exit() is in progress.
 */
atomic_bool shmem_exit_inprogress = false;

/*
 * This flag tracks whether we've called atexit() in the current process
 * (or in the parent postmaster).
 */
static bool atexit_callback_setup = false;

/* local functions */
static void proc_exit_prepare(int code);


/* ----------------------------------------------------------------
 *						exit() handling stuff
 *
 * These functions are in generally the same spirit as atexit(),
 * but provide some additional features we need --- in particular,
 * we want to register callbacks to invoke when we are disconnecting
 * from a broken shared-memory context but not exiting the postmaster.
 *
 * Callback functions can take zero, one, or two args: the first passed
 * arg is the integer exitcode, the second is the Datum supplied when
 * the callback was registered.
 * ----------------------------------------------------------------
 */

#define MAX_ON_EXITS 20

struct ONEXIT
{
	pg_on_exit_callback function;
	Datum		arg;
};

static struct ONEXIT on_proc_exit_list[MAX_ON_EXITS];
static struct ONEXIT on_shmem_exit_list[MAX_ON_EXITS];
static struct ONEXIT before_shmem_exit_list[MAX_ON_EXITS];

static int	on_proc_exit_index,
			on_shmem_exit_index,
			before_shmem_exit_index;


/* ----------------------------------------------------------------
 *		proc_exit
 *
 *		this function calls all the callbacks registered
 *		for it (to free resources) and then calls exit.
 *
 *		This should be the only function to call exit().
 *		-cim 2/6/90
 *
 *		Unfortunately, we can't really guarantee that add-on code
 *		obeys the rule of not calling exit() directly.  So, while
 *		this is the preferred way out of the system, we also register
 *		an atexit callback that will make sure cleanup happens.
 * ----------------------------------------------------------------
 */
void
proc_exit(int code)
{
	/* Clean up everything that must be cleaned up */
	proc_exit_prepare(code);

#ifdef PROFILE_PID_DIR
	{
		/*
		 * If we are profiling ourself then gprof's mcleanup() is about to
		 * write out a profile to ./gmon.out.  Since mcleanup() always uses a
		 * fixed file name, each backend will overwrite earlier profiles. To
		 * fix that, we create a separate subdirectory for each backend
		 * (./gprof/pid) and 'cd' to that subdirectory before we exit() - that
		 * forces mcleanup() to write each profile into its own directory.  We
		 * end up with something like: $PGDATA/gprof/8829/gmon.out
		 * $PGDATA/gprof/8845/gmon.out ...
		 *
		 * To avoid undesirable disk space bloat, autovacuum workers are
		 * discriminated against: all their gmon.out files go into the same
		 * subdirectory.  Without this, an installation that is "just sitting
		 * there" nonetheless eats megabytes of disk space every few seconds.
		 *
		 * Note that we do this here instead of in an on_proc_exit() callback
		 * because we want to ensure that this code executes last - we don't
		 * want to interfere with any other on_proc_exit() callback.  For the
		 * same reason, we do not include it in proc_exit_prepare ... so if
		 * you are exiting in the "wrong way" you won't drop your profile in a
		 * nice place.
		 */
		char		gprofDirName[32];

		if (IsAutoVacuumWorkerProcess())
			snprintf(gprofDirName, 32, "gprof/avworker");
		else
			snprintf(gprofDirName, 32, "gprof/%d", (int) getpid());

		/*
		 * Use mkdir() instead of MakePGDirectory() since we aren't making a
		 * PG directory here.
		 */
		mkdir("gprof", S_IRWXU | S_IRWXG | S_IRWXO);
		mkdir(gprofDirName, S_IRWXU | S_IRWXG | S_IRWXO);
		chdir(gprofDirName);
	}
#endif

	if (IsYugaByteEnabled())
		YBOnPostgresBackendShutdown();

	elog(DEBUG3, "exit(%d)", code);

	exit(code);
}

/*
 * Code shared between proc_exit and the atexit handler.  Note that in
 * normal exit through proc_exit, this will actually be called twice ...
 * but the second call will have nothing to do.
 */
static void
proc_exit_prepare(int code)
{
	/*
	 * Once we set this flag, we are committed to exit.  Any ereport() will
	 * NOT send control back to the main loop, but right back here.
	 */
	proc_exit_inprogress = true;

	/*
	 * Forget any pending cancel or die requests; we're doing our best to
	 * close up shop already.  Note that the signal handlers will not set
	 * these flags again, now that proc_exit_inprogress is set.
	 */
	InterruptPending = false;
	ProcDiePending = false;
	QueryCancelPending = false;
	InterruptHoldoffCount = 1;
	CritSectionCount = 0;

	/*
	 * Also clear the error context stack, to prevent error callbacks from
	 * being invoked by any elog/ereport calls made during proc_exit. Whatever
	 * context they might want to offer is probably not relevant, and in any
	 * case they are likely to fail outright after we've done things like
	 * aborting any open transaction.  (In normal exit scenarios the context
	 * stack should be empty anyway, but it might not be in the case of
	 * elog(FATAL) for example.)
	 */
	error_context_stack = NULL;
	/* For the same reason, reset debug_query_string before it's clobbered */
	debug_query_string = NULL;

	/* do our shared memory exits first */
	shmem_exit(code);

	elog(DEBUG3, "proc_exit(%d): %d callbacks to make",
		 code, on_proc_exit_index);

	/*
	 * call all the registered callbacks.
	 *
	 * Note that since we decrement on_proc_exit_index each time, if a
	 * callback calls ereport(ERROR) or ereport(FATAL) then it won't be
	 * invoked again when control comes back here (nor will the
	 * previously-completed callbacks).  So, an infinite loop should not be
	 * possible.
	 */
	while (--on_proc_exit_index >= 0)
		on_proc_exit_list[on_proc_exit_index].function(code,
													   on_proc_exit_list[on_proc_exit_index].arg);

	on_proc_exit_index = 0;
}

/* ------------------
 * Run all of the on_shmem_exit routines --- but don't actually exit.
 * This is used by the postmaster to re-initialize shared memory and
 * semaphores after a backend dies horribly.  As with proc_exit(), we
 * remove each callback from the list before calling it, to avoid
 * infinite loop in case of error.
 * ------------------
 */
void
shmem_exit(int code)
{
	shmem_exit_inprogress = true;

	/*
	 * Call before_shmem_exit callbacks.
	 *
	 * These should be things that need most of the system to still be up and
	 * working, such as cleanup of temp relations, which requires catalog
	 * access; or things that need to be completed because later cleanup steps
	 * depend on them, such as releasing lwlocks.
	 */
	elog(DEBUG3, "shmem_exit(%d): %d before_shmem_exit callbacks to make",
		 code, before_shmem_exit_index);
	while (--before_shmem_exit_index >= 0)
		before_shmem_exit_list[before_shmem_exit_index].function(code,
																 before_shmem_exit_list[before_shmem_exit_index].arg);
	before_shmem_exit_index = 0;

	/*
	 * Call dynamic shared memory callbacks.
	 *
	 * These serve the same purpose as late callbacks, but for dynamic shared
	 * memory segments rather than the main shared memory segment.
	 * dsm_backend_shutdown() has the same kind of progressive logic we use
	 * for the main shared memory segment; namely, it unregisters each
	 * callback before invoking it, so that we don't get stuck in an infinite
	 * loop if one of those callbacks itself throws an ERROR or FATAL.
	 *
	 * Note that explicitly calling this function here is quite different from
	 * registering it as an on_shmem_exit callback for precisely this reason:
	 * if one dynamic shared memory callback errors out, the remaining
	 * callbacks will still be invoked.  Thus, hard-coding this call puts it
	 * equal footing with callbacks for the main shared memory segment.
	 */
	dsm_backend_shutdown();

	/*
	 * Call on_shmem_exit callbacks.
	 *
	 * These are generally releasing low-level shared memory resources.  In
	 * some cases, this is a backstop against the possibility that the early
	 * callbacks might themselves fail, leading to re-entry to this routine;
	 * in other cases, it's cleanup that only happens at process exit.
	 */
	elog(DEBUG3, "shmem_exit(%d): %d on_shmem_exit callbacks to make",
		 code, on_shmem_exit_index);
	while (--on_shmem_exit_index >= 0)
		on_shmem_exit_list[on_shmem_exit_index].function(code,
														 on_shmem_exit_list[on_shmem_exit_index].arg);
	on_shmem_exit_index = 0;

	shmem_exit_inprogress = false;
}

/* ----------------------------------------------------------------
 *		atexit_callback
 *
 *		Backstop to ensure that direct calls of exit() don't mess us up.
 *
 * Somebody who was being really uncooperative could call _exit(),
 * but for that case we have a "dead man switch" that will make the
 * postmaster treat it as a crash --- see pmsignal.c.
 * ----------------------------------------------------------------
 */
static void
atexit_callback(void)
{
	/* Clean up everything that must be cleaned up */
	/* ... too bad we don't know the real exit code ... */
	proc_exit_prepare(-1);
}

/* ----------------------------------------------------------------
 *		on_proc_exit
 *
 *		this function adds a callback function to the list of
 *		functions invoked by proc_exit().   -cim 2/6/90
 * ----------------------------------------------------------------
 */
void
on_proc_exit(pg_on_exit_callback function, Datum arg)
{
	if (on_proc_exit_index >= MAX_ON_EXITS)
		ereport(FATAL,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg_internal("out of on_proc_exit slots")));

	on_proc_exit_list[on_proc_exit_index].function = function;
	on_proc_exit_list[on_proc_exit_index].arg = arg;

	++on_proc_exit_index;

	if (!atexit_callback_setup)
	{
		atexit(atexit_callback);
		atexit_callback_setup = true;
	}
}

/* ----------------------------------------------------------------
 *		before_shmem_exit
 *
 *		Register early callback to perform user-level cleanup,
 *		e.g. transaction abort, before we begin shutting down
 *		low-level subsystems.
 * ----------------------------------------------------------------
 */
void
before_shmem_exit(pg_on_exit_callback function, Datum arg)
{
	if (before_shmem_exit_index >= MAX_ON_EXITS)
		ereport(FATAL,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg_internal("out of before_shmem_exit slots")));

	before_shmem_exit_list[before_shmem_exit_index].function = function;
	before_shmem_exit_list[before_shmem_exit_index].arg = arg;

	++before_shmem_exit_index;

	if (!atexit_callback_setup)
	{
		atexit(atexit_callback);
		atexit_callback_setup = true;
	}
}

/* ----------------------------------------------------------------
 *		on_shmem_exit
 *
 *		Register ordinary callback to perform low-level shutdown
 *		(e.g. releasing our PGPROC); run after before_shmem_exit
 *		callbacks and before on_proc_exit callbacks.
 * ----------------------------------------------------------------
 */
void
on_shmem_exit(pg_on_exit_callback function, Datum arg)
{
	if (on_shmem_exit_index >= MAX_ON_EXITS)
		ereport(FATAL,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg_internal("out of on_shmem_exit slots")));

	on_shmem_exit_list[on_shmem_exit_index].function = function;
	on_shmem_exit_list[on_shmem_exit_index].arg = arg;

	++on_shmem_exit_index;

	if (!atexit_callback_setup)
	{
		atexit(atexit_callback);
		atexit_callback_setup = true;
	}
}

/* ----------------------------------------------------------------
 *		cancel_before_shmem_exit
 *
 *		this function removes a previously-registered before_shmem_exit
 *		callback.  We only look at the latest entry for removal, as we
 * 		expect callers to add and remove temporary before_shmem_exit
 * 		callbacks in strict LIFO order.
 * ----------------------------------------------------------------
 */
void
cancel_before_shmem_exit(pg_on_exit_callback function, Datum arg)
{
	if (before_shmem_exit_index > 0 &&
		before_shmem_exit_list[before_shmem_exit_index - 1].function
		== function &&
		before_shmem_exit_list[before_shmem_exit_index - 1].arg == arg)
		--before_shmem_exit_index;
	else
		elog(ERROR, "before_shmem_exit callback (%p,0x%llx) is not the latest entry",
			 function, (long long) arg);
}

/* ----------------------------------------------------------------
 *		on_exit_reset
 *
 *		this function clears all on_proc_exit() and on_shmem_exit()
 *		registered functions.  This is used just after forking a backend,
 *		so that the backend doesn't believe it should call the postmaster's
 *		on-exit routines when it exits...
 * ----------------------------------------------------------------
 */
void
on_exit_reset(void)
{
	before_shmem_exit_index = 0;
	on_shmem_exit_index = 0;
	on_proc_exit_index = 0;
	reset_on_dsm_detach();
}

/* ----------------------------------------------------------------
 *		check_on_shmem_exit_lists_are_empty
 *
 *		Debugging check that no shmem cleanup handlers have been registered
 *		prematurely in the current process.
 * ----------------------------------------------------------------
 */
void
check_on_shmem_exit_lists_are_empty(void)
{
	if (before_shmem_exit_index)
		elog(FATAL, "before_shmem_exit has been called prematurely");
	if (on_shmem_exit_index)
		elog(FATAL, "on_shmem_exit has been called prematurely");
	/* Checking DSM detach state seems unnecessary given the above */
}
