/*-------------------------------------------------------------------------
 *
 * pgarch.c
 *
 *	PostgreSQL WAL archiver
 *
 *	All functions relating to archiver are included here
 *
 *	- All functions executed by archiver process
 *
 *	- archiver is forked from postmaster, and the two
 *	processes then communicate using signals. All functions
 *	executed by postmaster are included in this file.
 *
 *	Initial author: Simon Riggs		simon@2ndquadrant.com
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/postmaster/pgarch.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include "access/xlog.h"
#include "access/xlog_internal.h"
#include "lib/binaryheap.h"
#include "libpq/pqsignal.h"
#include "pgstat.h"
#include "postmaster/interrupt.h"
#include "postmaster/pgarch.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/pmsignal.h"
#include "storage/proc.h"
#include "storage/procsignal.h"
#include "storage/shmem.h"
#include "storage/spin.h"
#include "utils/guc.h"
#include "utils/memutils.h"
#include "utils/ps_status.h"


/* ----------
 * Timer definitions.
 * ----------
 */
#define PGARCH_AUTOWAKE_INTERVAL 60 /* How often to force a poll of the
									 * archive status directory; in seconds. */
#define PGARCH_RESTART_INTERVAL 10	/* How often to attempt to restart a
									 * failed archiver; in seconds. */

/*
 * Maximum number of retries allowed when attempting to archive a WAL
 * file.
 */
#define NUM_ARCHIVE_RETRIES 3

/*
 * Maximum number of retries allowed when attempting to remove an
 * orphan archive status file.
 */
#define NUM_ORPHAN_CLEANUP_RETRIES 3

/*
 * Maximum number of .ready files to gather per directory scan.
 */
#define NUM_FILES_PER_DIRECTORY_SCAN 64

/* Shared memory area for archiver process */
typedef struct PgArchData
{
	int			pgprocno;		/* pgprocno of archiver process */

	/*
	 * Forces a directory scan in pgarch_readyXlog().  Protected by arch_lck.
	 */
	bool		force_dir_scan;

	slock_t		arch_lck;
} PgArchData;

char	   *XLogArchiveLibrary = "";


/* ----------
 * Local data
 * ----------
 */
static time_t last_sigterm_time = 0;
static PgArchData *PgArch = NULL;
static ArchiveModuleCallbacks ArchiveContext;


/*
 * Stuff for tracking multiple files to archive from each scan of
 * archive_status.  Minimizing the number of directory scans when there are
 * many files to archive can significantly improve archival rate.
 *
 * arch_heap is a max-heap that is used during the directory scan to track
 * the highest-priority files to archive.  After the directory scan
 * completes, the file names are stored in ascending order of priority in
 * arch_files.  pgarch_readyXlog() returns files from arch_files until it
 * is empty, at which point another directory scan must be performed.
 *
 * We only need this data in the archiver process, so make it a palloc'd
 * struct rather than a bunch of static arrays.
 */
struct arch_files_state
{
	binaryheap *arch_heap;
	int			arch_files_size;	/* number of live entries in arch_files[] */
	char	   *arch_files[NUM_FILES_PER_DIRECTORY_SCAN];
	/* buffers underlying heap, and later arch_files[], entries: */
	char		arch_filenames[NUM_FILES_PER_DIRECTORY_SCAN][MAX_XFN_CHARS + 1];
};

static struct arch_files_state *arch_files = NULL;

/*
 * Flags set by interrupt handlers for later service in the main loop.
 */
static volatile sig_atomic_t ready_to_stop = false;

/* ----------
 * Local function forward declarations
 * ----------
 */
static void pgarch_waken_stop(SIGNAL_ARGS);
static void pgarch_MainLoop(void);
static void pgarch_ArchiverCopyLoop(void);
static bool pgarch_archiveXlog(char *xlog);
static bool pgarch_readyXlog(char *xlog);
static void pgarch_archiveDone(char *xlog);
static void pgarch_die(int code, Datum arg);
static void HandlePgArchInterrupts(void);
static int	ready_file_comparator(Datum a, Datum b, void *arg);
static void LoadArchiveLibrary(void);
static void pgarch_call_module_shutdown_cb(int code, Datum arg);

/* Report shared memory space needed by PgArchShmemInit */
Size
PgArchShmemSize(void)
{
	Size		size = 0;

	size = add_size(size, sizeof(PgArchData));

	return size;
}

/* Allocate and initialize archiver-related shared memory */
void
PgArchShmemInit(void)
{
	bool		found;

	PgArch = (PgArchData *)
		ShmemInitStruct("Archiver Data", PgArchShmemSize(), &found);

	if (!found)
	{
		/* First time through, so initialize */
		MemSet(PgArch, 0, PgArchShmemSize());
		PgArch->pgprocno = INVALID_PGPROCNO;
		SpinLockInit(&PgArch->arch_lck);
	}
}

/*
 * PgArchCanRestart
 *
 * Return true and archiver is allowed to restart if enough time has
 * passed since it was launched last to reach PGARCH_RESTART_INTERVAL.
 * Otherwise return false.
 *
 * This is a safety valve to protect against continuous respawn attempts if the
 * archiver is dying immediately at launch. Note that since we will retry to
 * launch the archiver from the postmaster main loop, we will get another
 * chance later.
 */
bool
PgArchCanRestart(void)
{
	static time_t last_pgarch_start_time = 0;
	time_t		curtime = time(NULL);

	/*
	 * Return false and don't restart archiver if too soon since last archiver
	 * start.
	 */
	if ((unsigned int) (curtime - last_pgarch_start_time) <
		(unsigned int) PGARCH_RESTART_INTERVAL)
		return false;

	last_pgarch_start_time = curtime;
	return true;
}


/* Main entry point for archiver process */
void
PgArchiverMain(void)
{
	/*
	 * Ignore all signals usually bound to some action in the postmaster,
	 * except for SIGHUP, SIGTERM, SIGUSR1, SIGUSR2, and SIGQUIT.
	 */
	pqsignal(SIGHUP, SignalHandlerForConfigReload);
	pqsignal(SIGINT, SIG_IGN);
	pqsignal(SIGTERM, SignalHandlerForShutdownRequest);
	/* SIGQUIT handler was already set up by InitPostmasterChild */
	pqsignal(SIGALRM, SIG_IGN);
	pqsignal(SIGPIPE, SIG_IGN);
	pqsignal(SIGUSR1, procsignal_sigusr1_handler);
	pqsignal(SIGUSR2, pgarch_waken_stop);

	/* Reset some signals that are accepted by postmaster but not here */
	pqsignal(SIGCHLD, SIG_DFL);

	/* Unblock signals (they were blocked when the postmaster forked us) */
	PG_SETMASK(&UnBlockSig);

	/* We shouldn't be launched unnecessarily. */
	Assert(XLogArchivingActive());

	/* Arrange to clean up at archiver exit */
	on_shmem_exit(pgarch_die, 0);

	/*
	 * Advertise our pgprocno so that backends can use our latch to wake us up
	 * while we're sleeping.
	 */
	PgArch->pgprocno = MyProc->pgprocno;

	/* Create workspace for pgarch_readyXlog() */
	arch_files = palloc(sizeof(struct arch_files_state));
	arch_files->arch_files_size = 0;

	/* Initialize our max-heap for prioritizing files to archive. */
	arch_files->arch_heap = binaryheap_allocate(NUM_FILES_PER_DIRECTORY_SCAN,
												ready_file_comparator, NULL);

	/* Load the archive_library. */
	LoadArchiveLibrary();

	pgarch_MainLoop();

	proc_exit(0);
}

/*
 * Wake up the archiver
 */
void
PgArchWakeup(void)
{
	int			arch_pgprocno = PgArch->pgprocno;

	/*
	 * We don't acquire ProcArrayLock here.  It's actually fine because
	 * procLatch isn't ever freed, so we just can potentially set the wrong
	 * process' (or no process') latch.  Even in that case the archiver will
	 * be relaunched shortly and will start archiving.
	 */
	if (arch_pgprocno != INVALID_PGPROCNO)
		SetLatch(&ProcGlobal->allProcs[arch_pgprocno].procLatch);
}


/* SIGUSR2 signal handler for archiver process */
static void
pgarch_waken_stop(SIGNAL_ARGS)
{
	int			save_errno = errno;

	/* set flag to do a final cycle and shut down afterwards */
	ready_to_stop = true;
	SetLatch(MyLatch);

	errno = save_errno;
}

/*
 * pgarch_MainLoop
 *
 * Main loop for archiver
 */
static void
pgarch_MainLoop(void)
{
	pg_time_t	last_copy_time = 0;
	bool		time_to_stop;

	/*
	 * There shouldn't be anything for the archiver to do except to wait for a
	 * signal ... however, the archiver exists to protect our data, so she
	 * wakes up occasionally to allow herself to be proactive.
	 */
	do
	{
		ResetLatch(MyLatch);

		/* When we get SIGUSR2, we do one more archive cycle, then exit */
		time_to_stop = ready_to_stop;

		/* Check for barrier events and config update */
		HandlePgArchInterrupts();

		/*
		 * If we've gotten SIGTERM, we normally just sit and do nothing until
		 * SIGUSR2 arrives.  However, that means a random SIGTERM would
		 * disable archiving indefinitely, which doesn't seem like a good
		 * idea.  If more than 60 seconds pass since SIGTERM, exit anyway, so
		 * that the postmaster can start a new archiver if needed.
		 */
		if (ShutdownRequestPending)
		{
			time_t		curtime = time(NULL);

			if (last_sigterm_time == 0)
				last_sigterm_time = curtime;
			else if ((unsigned int) (curtime - last_sigterm_time) >=
					 (unsigned int) 60)
				break;
		}

		/* Do what we're here for */
		pgarch_ArchiverCopyLoop();
		last_copy_time = time(NULL);

		/*
		 * Sleep until a signal is received, or until a poll is forced by
		 * PGARCH_AUTOWAKE_INTERVAL having passed since last_copy_time, or
		 * until postmaster dies.
		 */
		if (!time_to_stop)		/* Don't wait during last iteration */
		{
			pg_time_t	curtime = (pg_time_t) time(NULL);
			int			timeout;

			timeout = PGARCH_AUTOWAKE_INTERVAL - (curtime - last_copy_time);
			if (timeout > 0)
			{
				int			rc;

				rc = WaitLatch(MyLatch,
							   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
							   timeout * 1000L,
							   WAIT_EVENT_ARCHIVER_MAIN);
				if (rc & WL_POSTMASTER_DEATH)
					time_to_stop = true;
			}
		}

		/*
		 * The archiver quits either when the postmaster dies (not expected)
		 * or after completing one more archiving cycle after receiving
		 * SIGUSR2.
		 */
	} while (!time_to_stop);
}

/*
 * pgarch_ArchiverCopyLoop
 *
 * Archives all outstanding xlogs then returns
 */
static void
pgarch_ArchiverCopyLoop(void)
{
	char		xlog[MAX_XFN_CHARS + 1];

	/* force directory scan in the first call to pgarch_readyXlog() */
	arch_files->arch_files_size = 0;

	/*
	 * loop through all xlogs with archive_status of .ready and archive
	 * them...mostly we expect this to be a single file, though it is possible
	 * some backend will add files onto the list of those that need archiving
	 * while we are still copying earlier archives
	 */
	while (pgarch_readyXlog(xlog))
	{
		int			failures = 0;
		int			failures_orphan = 0;

		for (;;)
		{
			struct stat stat_buf;
			char		pathname[MAXPGPATH];

			/*
			 * Do not initiate any more archive commands after receiving
			 * SIGTERM, nor after the postmaster has died unexpectedly. The
			 * first condition is to try to keep from having init SIGKILL the
			 * command, and the second is to avoid conflicts with another
			 * archiver spawned by a newer postmaster.
			 */
			if (ShutdownRequestPending || !PostmasterIsAlive())
				return;

			/*
			 * Check for barrier events and config update.  This is so that
			 * we'll adopt a new setting for archive_command as soon as
			 * possible, even if there is a backlog of files to be archived.
			 */
			HandlePgArchInterrupts();

			/* can't do anything if not configured ... */
			if (ArchiveContext.check_configured_cb != NULL &&
				!ArchiveContext.check_configured_cb())
			{
				ereport(WARNING,
						(errmsg("archive_mode enabled, yet archiving is not configured")));
				return;
			}

			/*
			 * Since archive status files are not removed in a durable manner,
			 * a system crash could leave behind .ready files for WAL segments
			 * that have already been recycled or removed.  In this case,
			 * simply remove the orphan status file and move on.  unlink() is
			 * used here as even on subsequent crashes the same orphan files
			 * would get removed, so there is no need to worry about
			 * durability.
			 */
			snprintf(pathname, MAXPGPATH, XLOGDIR "/%s", xlog);
			if (stat(pathname, &stat_buf) != 0 && errno == ENOENT)
			{
				char		xlogready[MAXPGPATH];

				StatusFilePath(xlogready, xlog, ".ready");
				if (unlink(xlogready) == 0)
				{
					ereport(WARNING,
							(errmsg("removed orphan archive status file \"%s\"",
									xlogready)));

					/* leave loop and move to the next status file */
					break;
				}

				if (++failures_orphan >= NUM_ORPHAN_CLEANUP_RETRIES)
				{
					ereport(WARNING,
							(errmsg("removal of orphan archive status file \"%s\" failed too many times, will try again later",
									xlogready)));

					/* give up cleanup of orphan status files */
					return;
				}

				/* wait a bit before retrying */
				pg_usleep(1000000L);
				continue;
			}

			if (pgarch_archiveXlog(xlog))
			{
				/* successful */
				pgarch_archiveDone(xlog);

				/*
				 * Tell the cumulative stats system about the WAL file that we
				 * successfully archived
				 */
				pgstat_report_archiver(xlog, false);

				break;			/* out of inner retry loop */
			}
			else
			{
				/*
				 * Tell the cumulative stats system about the WAL file that we
				 * failed to archive
				 */
				pgstat_report_archiver(xlog, true);

				if (++failures >= NUM_ARCHIVE_RETRIES)
				{
					ereport(WARNING,
							(errmsg("archiving write-ahead log file \"%s\" failed too many times, will try again later",
									xlog)));
					return;		/* give up archiving for now */
				}
				pg_usleep(1000000L);	/* wait a bit before retrying */
			}
		}
	}
}

/*
 * pgarch_archiveXlog
 *
 * Invokes archive_file_cb to copy one archive file to wherever it should go
 *
 * Returns true if successful
 */
static bool
pgarch_archiveXlog(char *xlog)
{
	char		pathname[MAXPGPATH];
	char		activitymsg[MAXFNAMELEN + 16];
	bool		ret;

	snprintf(pathname, MAXPGPATH, XLOGDIR "/%s", xlog);

	/* Report archive activity in PS display */
	snprintf(activitymsg, sizeof(activitymsg), "archiving %s", xlog);
	set_ps_display(activitymsg);

	ret = ArchiveContext.archive_file_cb(xlog, pathname);
	if (ret)
		snprintf(activitymsg, sizeof(activitymsg), "last was %s", xlog);
	else
		snprintf(activitymsg, sizeof(activitymsg), "failed on %s", xlog);
	set_ps_display(activitymsg);

	return ret;
}

/*
 * pgarch_readyXlog
 *
 * Return name of the oldest xlog file that has not yet been archived.
 * No notification is set that file archiving is now in progress, so
 * this would need to be extended if multiple concurrent archival
 * tasks were created. If a failure occurs, we will completely
 * re-copy the file at the next available opportunity.
 *
 * It is important that we return the oldest, so that we archive xlogs
 * in order that they were written, for two reasons:
 * 1) to maintain the sequential chain of xlogs required for recovery
 * 2) because the oldest ones will sooner become candidates for
 * recycling at time of checkpoint
 *
 * NOTE: the "oldest" comparison will consider any .history file to be older
 * than any other file except another .history file.  Segments on a timeline
 * with a smaller ID will be older than all segments on a timeline with a
 * larger ID; the net result being that past timelines are given higher
 * priority for archiving.  This seems okay, or at least not obviously worth
 * changing.
 */
static bool
pgarch_readyXlog(char *xlog)
{
	char		XLogArchiveStatusDir[MAXPGPATH];
	DIR		   *rldir;
	struct dirent *rlde;
	bool		force_dir_scan;

	/*
	 * If a directory scan was requested, clear the stored file names and
	 * proceed.
	 */
	SpinLockAcquire(&PgArch->arch_lck);
	force_dir_scan = PgArch->force_dir_scan;
	PgArch->force_dir_scan = false;
	SpinLockRelease(&PgArch->arch_lck);

	if (force_dir_scan)
		arch_files->arch_files_size = 0;

	/*
	 * If we still have stored file names from the previous directory scan,
	 * try to return one of those.  We check to make sure the status file is
	 * still present, as the archive_command for a previous file may have
	 * already marked it done.
	 */
	while (arch_files->arch_files_size > 0)
	{
		struct stat st;
		char		status_file[MAXPGPATH];
		char	   *arch_file;

		arch_files->arch_files_size--;
		arch_file = arch_files->arch_files[arch_files->arch_files_size];
		StatusFilePath(status_file, arch_file, ".ready");

		if (stat(status_file, &st) == 0)
		{
			strcpy(xlog, arch_file);
			return true;
		}
		else if (errno != ENOENT)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not stat file \"%s\": %m", status_file)));
	}

	/* arch_heap is probably empty, but let's make sure */
	binaryheap_reset(arch_files->arch_heap);

	/*
	 * Open the archive status directory and read through the list of files
	 * with the .ready suffix, looking for the earliest files.
	 */
	snprintf(XLogArchiveStatusDir, MAXPGPATH, XLOGDIR "/archive_status");
	rldir = AllocateDir(XLogArchiveStatusDir);

	while ((rlde = ReadDir(rldir, XLogArchiveStatusDir)) != NULL)
	{
		int			basenamelen = (int) strlen(rlde->d_name) - 6;
		char		basename[MAX_XFN_CHARS + 1];
		char	   *arch_file;

		/* Ignore entries with unexpected number of characters */
		if (basenamelen < MIN_XFN_CHARS ||
			basenamelen > MAX_XFN_CHARS)
			continue;

		/* Ignore entries with unexpected characters */
		if (strspn(rlde->d_name, VALID_XFN_CHARS) < basenamelen)
			continue;

		/* Ignore anything not suffixed with .ready */
		if (strcmp(rlde->d_name + basenamelen, ".ready") != 0)
			continue;

		/* Truncate off the .ready */
		memcpy(basename, rlde->d_name, basenamelen);
		basename[basenamelen] = '\0';

		/*
		 * Store the file in our max-heap if it has a high enough priority.
		 */
		if (arch_files->arch_heap->bh_size < NUM_FILES_PER_DIRECTORY_SCAN)
		{
			/* If the heap isn't full yet, quickly add it. */
			arch_file = arch_files->arch_filenames[arch_files->arch_heap->bh_size];
			strcpy(arch_file, basename);
			binaryheap_add_unordered(arch_files->arch_heap, CStringGetDatum(arch_file));

			/* If we just filled the heap, make it a valid one. */
			if (arch_files->arch_heap->bh_size == NUM_FILES_PER_DIRECTORY_SCAN)
				binaryheap_build(arch_files->arch_heap);
		}
		else if (ready_file_comparator(binaryheap_first(arch_files->arch_heap),
									   CStringGetDatum(basename), NULL) > 0)
		{
			/*
			 * Remove the lowest priority file and add the current one to the
			 * heap.
			 */
			arch_file = DatumGetCString(binaryheap_remove_first(arch_files->arch_heap));
			strcpy(arch_file, basename);
			binaryheap_add(arch_files->arch_heap, CStringGetDatum(arch_file));
		}
	}
	FreeDir(rldir);

	/* If no files were found, simply return. */
	if (arch_files->arch_heap->bh_size == 0)
		return false;

	/*
	 * If we didn't fill the heap, we didn't make it a valid one.  Do that
	 * now.
	 */
	if (arch_files->arch_heap->bh_size < NUM_FILES_PER_DIRECTORY_SCAN)
		binaryheap_build(arch_files->arch_heap);

	/*
	 * Fill arch_files array with the files to archive in ascending order of
	 * priority.
	 */
	arch_files->arch_files_size = arch_files->arch_heap->bh_size;
	for (int i = 0; i < arch_files->arch_files_size; i++)
		arch_files->arch_files[i] = DatumGetCString(binaryheap_remove_first(arch_files->arch_heap));

	/* Return the highest priority file. */
	arch_files->arch_files_size--;
	strcpy(xlog, arch_files->arch_files[arch_files->arch_files_size]);

	return true;
}

/*
 * ready_file_comparator
 *
 * Compares the archival priority of the given files to archive.  If "a"
 * has a higher priority than "b", a negative value will be returned.  If
 * "b" has a higher priority than "a", a positive value will be returned.
 * If "a" and "b" have equivalent values, 0 will be returned.
 */
static int
ready_file_comparator(Datum a, Datum b, void *arg)
{
	char	   *a_str = DatumGetCString(a);
	char	   *b_str = DatumGetCString(b);
	bool		a_history = IsTLHistoryFileName(a_str);
	bool		b_history = IsTLHistoryFileName(b_str);

	/* Timeline history files always have the highest priority. */
	if (a_history != b_history)
		return a_history ? -1 : 1;

	/* Priority is given to older files. */
	return strcmp(a_str, b_str);
}

/*
 * PgArchForceDirScan
 *
 * When called, the next call to pgarch_readyXlog() will perform a
 * directory scan.  This is useful for ensuring that important files such
 * as timeline history files are archived as quickly as possible.
 */
void
PgArchForceDirScan(void)
{
	SpinLockAcquire(&PgArch->arch_lck);
	PgArch->force_dir_scan = true;
	SpinLockRelease(&PgArch->arch_lck);
}

/*
 * pgarch_archiveDone
 *
 * Emit notification that an xlog file has been successfully archived.
 * We do this by renaming the status file from NNN.ready to NNN.done.
 * Eventually, a checkpoint process will notice this and delete both the
 * NNN.done file and the xlog file itself.
 */
static void
pgarch_archiveDone(char *xlog)
{
	char		rlogready[MAXPGPATH];
	char		rlogdone[MAXPGPATH];

	StatusFilePath(rlogready, xlog, ".ready");
	StatusFilePath(rlogdone, xlog, ".done");
	(void) durable_rename(rlogready, rlogdone, WARNING);
}


/*
 * pgarch_die
 *
 * Exit-time cleanup handler
 */
static void
pgarch_die(int code, Datum arg)
{
	PgArch->pgprocno = INVALID_PGPROCNO;
}

/*
 * Interrupt handler for WAL archiver process.
 *
 * This is called in the loops pgarch_MainLoop and pgarch_ArchiverCopyLoop.
 * It checks for barrier events, config update and request for logging of
 * memory contexts, but not shutdown request because how to handle
 * shutdown request is different between those loops.
 */
static void
HandlePgArchInterrupts(void)
{
	if (ProcSignalBarrierPending)
		ProcessProcSignalBarrier();

	/* Perform logging of memory contexts of this process */
	if (LogMemoryContextPending)
		ProcessLogMemoryContextInterrupt();

	if (ConfigReloadPending)
	{
		char	   *archiveLib = pstrdup(XLogArchiveLibrary);
		bool		archiveLibChanged;

		ConfigReloadPending = false;
		ProcessConfigFile(PGC_SIGHUP);

		archiveLibChanged = strcmp(XLogArchiveLibrary, archiveLib) != 0;
		pfree(archiveLib);

		if (archiveLibChanged)
		{
			/*
			 * Ideally, we would simply unload the previous archive module and
			 * load the new one, but there is presently no mechanism for
			 * unloading a library (see the comment above
			 * internal_load_library()).  To deal with this, we simply restart
			 * the archiver.  The new archive module will be loaded when the
			 * new archiver process starts up.  Note that this triggers the
			 * module's shutdown callback, if defined.
			 */
			ereport(LOG,
					(errmsg("restarting archiver process because value of "
							"\"archive_library\" was changed")));

			proc_exit(0);
		}
	}
}

/*
 * LoadArchiveLibrary
 *
 * Loads the archiving callbacks into our local ArchiveContext.
 */
static void
LoadArchiveLibrary(void)
{
	ArchiveModuleInit archive_init;

	memset(&ArchiveContext, 0, sizeof(ArchiveModuleCallbacks));

	/*
	 * If shell archiving is enabled, use our special initialization function.
	 * Otherwise, load the library and call its _PG_archive_module_init().
	 */
	if (XLogArchiveLibrary[0] == '\0')
		archive_init = shell_archive_init;
	else
		archive_init = (ArchiveModuleInit)
			load_external_function(XLogArchiveLibrary,
								   "_PG_archive_module_init", false, NULL);

	if (archive_init == NULL)
		ereport(ERROR,
				(errmsg("archive modules have to define the symbol %s", "_PG_archive_module_init")));

	(*archive_init) (&ArchiveContext);

	if (ArchiveContext.archive_file_cb == NULL)
		ereport(ERROR,
				(errmsg("archive modules must register an archive callback")));

	before_shmem_exit(pgarch_call_module_shutdown_cb, 0);
}

/*
 * Call the shutdown callback of the loaded archive module, if defined.
 */
static void
pgarch_call_module_shutdown_cb(int code, Datum arg)
{
	if (ArchiveContext.shutdown_cb != NULL)
		ArchiveContext.shutdown_cb();
}
