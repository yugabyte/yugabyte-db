/*
 * collector.c
 *		Collector of wait event history and profile.
 *
 * Copyright (c) 2015-2016, Postgres Professional
 *
 * IDENTIFICATION
 *	  contrib/pg_wait_sampling/pg_wait_sampling.c
 */
#include "postgres.h"

#include "catalog/pg_type.h"
#if PG_VERSION_NUM >= 130000
#include "common/hashfn.h"
#endif
#include "funcapi.h"
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/procarray.h"
#include "storage/procsignal.h"
#include "storage/shm_mq.h"
#include "storage/shm_toc.h"
#include "storage/spin.h"
#include "utils/memutils.h"
#include "utils/resowner.h"
#include "pgstat.h"

#include "compat.h"
#include "pg_wait_sampling.h"

static volatile sig_atomic_t shutdown_requested = false;

static void handle_sigterm(SIGNAL_ARGS);

/*
 * Register background worker for collecting waits history.
 */
void
pgws_register_wait_collector(void)
{
	BackgroundWorker worker;

	/* Set up background worker parameters */
	memset(&worker, 0, sizeof(worker));
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS;
	worker.bgw_start_time = BgWorkerStart_ConsistentState;
	worker.bgw_restart_time = 1;
	worker.bgw_notify_pid = 0;
	snprintf(worker.bgw_library_name, BGW_MAXLEN, "pg_wait_sampling");
	snprintf(worker.bgw_function_name, BGW_MAXLEN, CppAsString(pgws_collector_main));
	snprintf(worker.bgw_name, BGW_MAXLEN, "pg_wait_sampling collector");
	worker.bgw_main_arg = (Datum) 0;
	RegisterBackgroundWorker(&worker);
}

/*
 * Allocate memory for waits history.
 */
static void
alloc_history(History *observations, int count)
{
	observations->items = (HistoryItem *) palloc0(sizeof(HistoryItem) * count);
	observations->index = 0;
	observations->count = count;
	observations->wraparound = false;
}

/*
 * Reallocate memory for changed number of history items.
 */
static void
realloc_history(History *observations, int count)
{
	HistoryItem	   *newitems;
	int				copyCount,
					i,
					j;

	/* Allocate new array for history */
	newitems = (HistoryItem *) palloc0(sizeof(HistoryItem) * count);

	/* Copy entries from old array to the new */
	if (observations->wraparound)
		copyCount = observations->count;
	else
		copyCount = observations->index;

	copyCount = Min(copyCount, count);

	i = 0;
	if (observations->wraparound)
		j = observations->index + 1;
	else
		j = 0;
	while (i < copyCount)
	{
		if (j >= observations->count)
			j = 0;
		memcpy(&newitems[i], &observations->items[j], sizeof(HistoryItem));
		i++;
		j++;
	}

	/* Switch to new history array */
	pfree(observations->items);
	observations->items = newitems;
	observations->index = copyCount;
	observations->count = count;
	observations->wraparound = false;
}

static void
handle_sigterm(SIGNAL_ARGS)
{
	int save_errno = errno;
	shutdown_requested = true;
	if (MyProc)
		SetLatch(&MyProc->procLatch);
	errno = save_errno;
}

/*
 * Get next item of history with rotation.
 */
static HistoryItem *
get_next_observation(History *observations)
{
	HistoryItem *result;

	if (observations->index >= observations->count)
	{
		observations->index = 0;
		observations->wraparound = true;
	}
	result = &observations->items[observations->index];
	observations->index++;
	return result;
}

/*
 * Read current waits from backends and write them to history array
 * and/or profile hash.
 */
static void
probe_waits(History *observations, HTAB *profile_hash,
			bool write_history, bool write_profile, bool profile_pid)
{
	int			i,
				newSize;
	TimestampTz	ts = GetCurrentTimestamp();

	/* Realloc waits history if needed */
	newSize = pgws_collector_hdr->historySize;
	if (observations->count != newSize)
		realloc_history(observations, newSize);

	/* Iterate PGPROCs under shared lock */
	LWLockAcquire(ProcArrayLock, LW_SHARED);
	for (i = 0; i < ProcGlobal->allProcCount; i++)
	{
		HistoryItem		item,
					   *observation;
		PGPROC		   *proc = &ProcGlobal->allProcs[i];

		if (proc->pid == 0)
			continue;

		if (proc->wait_event_info == 0)
			continue;

		/* Collect next wait event sample */
		item.pid = proc->pid;
		item.wait_event_info = proc->wait_event_info;

		if (pgws_collector_hdr->profileQueries)
			item.queryId = pgws_proc_queryids[i];
		else
			item.queryId = 0;

		item.ts = ts;

		/* Write to the history if needed */
		if (write_history)
		{
			observation = get_next_observation(observations);
			*observation = item;
		}

		/* Write to the profile if needed */
		if (write_profile)
		{
			ProfileItem	   *profileItem;
			bool			found;

			if (!profile_pid)
				item.pid = 0;

			profileItem = (ProfileItem *) hash_search(profile_hash, &item, HASH_ENTER, &found);
			if (found)
				profileItem->count++;
			else
				profileItem->count = 1;
		}
	}
	LWLockRelease(ProcArrayLock);
}

/*
 * Send waits history to shared memory queue.
 */
static void
send_history(History *observations, shm_mq_handle *mqh)
{
	Size	count,
			i;
	shm_mq_result	mq_result;

	if (observations->wraparound)
		count = observations->count;
	else
		count = observations->index;

	mq_result = shm_mq_send_compat(mqh, sizeof(count), &count, false, true);
	if (mq_result == SHM_MQ_DETACHED)
	{
		ereport(WARNING,
				(errmsg("pg_wait_sampling collector: "
						"receiver of message queue has been detached")));
		return;
	}
	for (i = 0; i < count; i++)
	{
		mq_result = shm_mq_send_compat(mqh,
								sizeof(HistoryItem),
								&observations->items[i],
								false,
								true);
		if (mq_result == SHM_MQ_DETACHED)
		{
			ereport(WARNING,
					(errmsg("pg_wait_sampling collector: "
							"receiver of message queue has been detached")));
			return;
		}
	}
}

/*
 * Send profile to shared memory queue.
 */
static void
send_profile(HTAB *profile_hash, shm_mq_handle *mqh)
{
	HASH_SEQ_STATUS	scan_status;
	ProfileItem	   *item;
	Size			count = hash_get_num_entries(profile_hash);
	shm_mq_result	mq_result;

	mq_result = shm_mq_send_compat(mqh, sizeof(count), &count, false, true);
	if (mq_result == SHM_MQ_DETACHED)
	{
		ereport(WARNING,
				(errmsg("pg_wait_sampling collector: "
						"receiver of message queue has been detached")));
		return;
	}
	hash_seq_init(&scan_status, profile_hash);
	while ((item = (ProfileItem *) hash_seq_search(&scan_status)) != NULL)
	{
		mq_result = shm_mq_send_compat(mqh, sizeof(ProfileItem), item, false,
									   true);
		if (mq_result == SHM_MQ_DETACHED)
		{
			hash_seq_term(&scan_status);
			ereport(WARNING,
					(errmsg("pg_wait_sampling collector: "
							"receiver of message queue has been detached")));
			return;
		}
	}
}

/*
 * Make hash table for wait profile.
 */
static HTAB *
make_profile_hash()
{
	HASHCTL hash_ctl;

	hash_ctl.hash = tag_hash;
	hash_ctl.hcxt = TopMemoryContext;

	if (pgws_collector_hdr->profileQueries)
		hash_ctl.keysize = offsetof(ProfileItem, count);
	else
		hash_ctl.keysize = offsetof(ProfileItem, queryId);

	hash_ctl.entrysize = sizeof(ProfileItem);
	return hash_create("Waits profile hash", 1024, &hash_ctl,
					   HASH_FUNCTION | HASH_ELEM);
}

/*
 * Delta between two timestamps in milliseconds.
 */
static int64
millisecs_diff(TimestampTz tz1, TimestampTz tz2)
{
	long	secs;
	int		microsecs;

	TimestampDifference(tz1, tz2, &secs, &microsecs);

	return secs * 1000 + microsecs / 1000;

}

/*
 * Main routine of wait history collector.
 */
void
pgws_collector_main(Datum main_arg)
{
	HTAB		   *profile_hash = NULL;
	History			observations;
	MemoryContext	old_context,
					collector_context;
	TimestampTz		current_ts,
					history_ts,
					profile_ts;

	/*
	 * Establish signal handlers.
	 *
	 * We want CHECK_FOR_INTERRUPTS() to kill off this worker process just as
	 * it would a normal user backend.  To make that happen, we establish a
	 * signal handler that is a stripped-down version of die().  We don't have
	 * any equivalent of the backend's command-read loop, where interrupts can
	 * be processed immediately, so make sure ImmediateInterruptOK is turned
	 * off.
	 *
	 * We also want to respond to the ProcSignal notifications.  This is done
	 * in the upstream provided procsignal_sigusr1_handler, which is
	 * automatically used if a bgworker connects to a database.  But since our
	 * worker doesn't connect to any database even though it calls
	 * InitPostgres, which will still initializze a new backend and thus
	 * partitipate to the ProcSignal infrastructure.
	 */
	pqsignal(SIGTERM, handle_sigterm);
	pqsignal(SIGUSR1, procsignal_sigusr1_handler);
	BackgroundWorkerUnblockSignals();
	InitPostgresCompat(NULL, InvalidOid, NULL, InvalidOid, false, false, NULL);
	SetProcessingMode(NormalProcessing);

	/* Make pg_wait_sampling recognisable in pg_stat_activity */
	pgstat_report_appname("pg_wait_sampling collector");

	profile_hash = make_profile_hash();
	pgws_collector_hdr->latch = &MyProc->procLatch;

	CurrentResourceOwner = ResourceOwnerCreate(NULL, "pg_wait_sampling collector");
	collector_context = AllocSetContextCreate(TopMemoryContext,
			"pg_wait_sampling context", ALLOCSET_DEFAULT_SIZES);
	old_context = MemoryContextSwitchTo(collector_context);
	alloc_history(&observations, pgws_collector_hdr->historySize);
	MemoryContextSwitchTo(old_context);

	ereport(LOG, (errmsg("pg_wait_sampling collector started")));

	/* Start counting time for history and profile samples */
	profile_ts = history_ts = GetCurrentTimestamp();

	while (1)
	{
		int				rc;
		shm_mq_handle  *mqh;
		int64			history_diff,
						profile_diff;
		int				history_period,
						profile_period;
		bool			write_history,
						write_profile;

		/* We need an explicit call for at least ProcSignal notifications. */
		CHECK_FOR_INTERRUPTS();

		/* Wait calculate time to next sample for history or profile */
		current_ts = GetCurrentTimestamp();

		history_diff = millisecs_diff(history_ts, current_ts);
		profile_diff = millisecs_diff(profile_ts, current_ts);
		history_period = pgws_collector_hdr->historyPeriod;
		profile_period = pgws_collector_hdr->profilePeriod;

		write_history = (history_diff >= (int64)history_period);
		write_profile = (profile_diff >= (int64)profile_period);

		if (write_history || write_profile)
		{
			probe_waits(&observations, profile_hash,
						write_history, write_profile, pgws_collector_hdr->profilePid);

			if (write_history)
			{
				history_ts = current_ts;
				history_diff = 0;
			}

			if (write_profile)
			{
				profile_ts = current_ts;
				profile_diff = 0;
			}
		}

		/* Shutdown if requested */
		if (shutdown_requested)
			break;

		/*
		 * Wait until next sample time or request to do something through
		 * shared memory.
		 */
#if PG_VERSION_NUM >= 100000
		rc = WaitLatch(&MyProc->procLatch, WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
				Min(history_period - (int)history_diff,
					profile_period - (int)profile_diff), PG_WAIT_EXTENSION);
#else
		rc = WaitLatch(&MyProc->procLatch, WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
				Min(history_period - (int)history_diff,
					profile_period - (int)profile_diff));
#endif

		if (rc & WL_POSTMASTER_DEATH)
			proc_exit(1);

		ResetLatch(&MyProc->procLatch);

		/* Handle request if any */
		if (pgws_collector_hdr->request != NO_REQUEST)
		{
			LOCKTAG		tag;
			SHMRequest	request;

			pgws_init_lock_tag(&tag, PGWS_COLLECTOR_LOCK);

			LockAcquire(&tag, ExclusiveLock, false, false);
			request = pgws_collector_hdr->request;
			pgws_collector_hdr->request = NO_REQUEST;

			if (request == HISTORY_REQUEST || request == PROFILE_REQUEST)
			{
				shm_mq_result	mq_result;

				/* Send history or profile */
				shm_mq_set_sender(pgws_collector_mq, MyProc);
				mqh = shm_mq_attach(pgws_collector_mq, NULL, NULL);
				mq_result = shm_mq_wait_for_attach(mqh);
				switch (mq_result)
				{
					case SHM_MQ_SUCCESS:
						switch (request)
						{
							case HISTORY_REQUEST:
								send_history(&observations, mqh);
								break;
							case PROFILE_REQUEST:
								send_profile(profile_hash, mqh);
								break;
							default:
								Assert(false);
						}
						break;
					case SHM_MQ_DETACHED:
						ereport(WARNING,
								(errmsg("pg_wait_sampling collector: "
										"receiver of message queue have been "
										"detached")));
						break;
					default:
						Assert(false);
				}
				shm_mq_detach_compat(mqh, pgws_collector_mq);
			}
			else if (request == PROFILE_RESET)
			{
				/* Reset profile hash */
				hash_destroy(profile_hash);
				profile_hash = make_profile_hash();
			}
			LockRelease(&tag, ExclusiveLock, false);
		}
	}

	MemoryContextReset(collector_context);

	/*
	 * We're done.  Explicitly detach the shared memory segment so that we
	 * don't get a resource leak warning at commit time.  This will fire any
	 * on_dsm_detach callbacks we've registered, as well.  Once that's done,
	 * we can go ahead and exit.
	 */
	ereport(LOG, (errmsg("pg_wait_sampling collector shutting down")));
	proc_exit(0);
}
