/*
 * pg_wait_sampling.c
 *		Track information about wait events.
 *
 * Copyright (c) 2015-2017, Postgres Professional
 *
 * IDENTIFICATION
 *	  contrib/pg_wait_sampling/pg_wait_sampling.c
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "access/twophase.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "optimizer/planner.h"
#include "pgstat.h"
#include "postmaster/autovacuum.h"
#if PG_VERSION_NUM >= 120000
#include "replication/walsender.h"
#endif
#include "storage/ipc.h"
#include "storage/pg_shmem.h"
#include "storage/procarray.h"
#include "storage/shm_mq.h"
#include "storage/shm_toc.h"
#include "storage/spin.h"
#include "utils/builtins.h"
#include "utils/datetime.h"
#include "utils/guc_tables.h"
#include "utils/guc.h"
#include "utils/memutils.h" /* TopMemoryContext.  Actually for PG 9.6 only,
							 * but there should be no harm for others. */

#include "compat.h"
#include "pg_wait_sampling.h"

PG_MODULE_MAGIC;

void		_PG_init(void);

static bool shmem_initialized = false;

/* Hooks */
static ExecutorEnd_hook_type	prev_ExecutorEnd = NULL;
static planner_hook_type		planner_hook_next = NULL;

/* Pointers to shared memory objects */
shm_mq				   *pgws_collector_mq = NULL;
uint64				   *pgws_proc_queryids = NULL;
CollectorShmqHeader	   *pgws_collector_hdr = NULL;

/* Receiver (backend) local shm_mq pointers and lock */
static shm_mq *recv_mq = NULL;
static shm_mq_handle *recv_mqh = NULL;
static LOCKTAG queueTag;

#if PG_VERSION_NUM >= 150000
static shmem_request_hook_type prev_shmem_request_hook = NULL;
#endif
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;
static PGPROC * search_proc(int backendPid);
static PlannedStmt *pgws_planner_hook(Query *parse,
#if PG_VERSION_NUM >= 130000
		const char *query_string,
#endif
		int cursorOptions, ParamListInfo boundParams);
static void pgws_ExecutorEnd(QueryDesc *queryDesc);

/*
 * Calculate max processes count.
 *
 * The value has to be in sync with ProcGlobal->allProcCount, initialized in
 * InitProcGlobal() (proc.c).
 *
 */
static int
get_max_procs_count(void)
{
	int count = 0;

	/* First, add the maximum number of backends (MaxBackends). */
#if PG_VERSION_NUM >= 150000
	/*
	 * On pg15+, we can directly access the MaxBackends variable, as it will
	 * have already been initialized in shmem_request_hook.
	 */
	Assert(MaxBackends > 0);
	count += MaxBackends;
#else
	/*
	 * On older versions, we need to compute MaxBackends: bgworkers, autovacuum
	 * workers and launcher.
	 * This has to be in sync with the value computed in
	 * InitializeMaxBackends() (postinit.c)
	 *
	 * Note that we need to calculate the value as it won't initialized when we
	 * need it during _PG_init().
	 *
	 * Note also that the value returned during _PG_init() might be different
	 * from the value returned later if some third-party modules change one of
	 * the underlying GUC.  This isn't ideal but can't lead to a crash, as the
	 * value returned during _PG_init() is only used to ask for additional
	 * shmem with RequestAddinShmemSpace(), and postgres has an extra 100kB of
	 * shmem to compensate some small unaccounted usage.  So if the value later
	 * changes, we will allocate and initialize the new (and correct) memory
	 * size, which will either work thanks for the extra 100kB of shmem, of
	 * fail (and prevent postgres startup) due to an out of shared memory
	 * error.
	 */
	count += MaxConnections + autovacuum_max_workers + 1
			+ max_worker_processes;

	/*
	 * Starting with pg12, wal senders aren't part of MaxConnections anymore
	 * and have to be accounted for.
	 */
#if PG_VERSION_NUM >= 120000
	count += max_wal_senders;
#endif		/* pg 12+ */
#endif		/* pg 15- */
	/* End of MaxBackends calculation. */

	/* Add AuxiliaryProcs */
	count += NUM_AUXILIARY_PROCS;

	return count;
}

/*
 * Estimate amount of shared memory needed.
 */
static Size
pgws_shmem_size(void)
{
	shm_toc_estimator	e;
	Size				size;
	int					nkeys;

	shm_toc_initialize_estimator(&e);

	nkeys = 3;

	shm_toc_estimate_chunk(&e, sizeof(CollectorShmqHeader));
	shm_toc_estimate_chunk(&e, (Size) COLLECTOR_QUEUE_SIZE);
	shm_toc_estimate_chunk(&e, sizeof(uint64) * get_max_procs_count());

	shm_toc_estimate_keys(&e, nkeys);
	size = shm_toc_estimate(&e);

	return size;
}

static bool
shmem_int_guc_check_hook(int *newval, void **extra, GucSource source)
{
	if (UsedShmemSegAddr == NULL)
		return false;
	return true;
}

static bool
shmem_bool_guc_check_hook(bool *newval, void **extra, GucSource source)
{
	if (UsedShmemSegAddr == NULL)
		return false;
	return true;
}

/*
 * This union allows us to mix the numerous different types of structs
 * that we are organizing.
 */
typedef union
{
	struct config_generic generic;
	struct config_bool _bool;
	struct config_real real;
	struct config_int integer;
	struct config_string string;
	struct config_enum _enum;
} mixedStruct;

/*
 * Setup new GUCs or modify existsing.
 */
static void
setup_gucs()
{
	struct config_generic **guc_vars;
	int			numOpts,
				i;
	bool		history_size_found = false,
				history_period_found = false,
				profile_period_found = false,
				profile_pid_found = false,
				profile_queries_found = false;

	get_guc_variables_compat(&guc_vars, &numOpts);

	for (i = 0; i < numOpts; i++)
	{
		mixedStruct *var = (mixedStruct *) guc_vars[i];
		const char *name = var->generic.name;

		if (var->generic.flags & GUC_CUSTOM_PLACEHOLDER)
			continue;

		if (!strcmp(name, "pg_wait_sampling.history_size"))
		{
			history_size_found = true;
			var->integer.variable = &pgws_collector_hdr->historySize;
			pgws_collector_hdr->historySize = 5000;
		}
		else if (!strcmp(name, "pg_wait_sampling.history_period"))
		{
			history_period_found = true;
			var->integer.variable = &pgws_collector_hdr->historyPeriod;
			pgws_collector_hdr->historyPeriod = 10;
		}
		else if (!strcmp(name, "pg_wait_sampling.profile_period"))
		{
			profile_period_found = true;
			var->integer.variable = &pgws_collector_hdr->profilePeriod;
			pgws_collector_hdr->profilePeriod = 10;
		}
		else if (!strcmp(name, "pg_wait_sampling.profile_pid"))
		{
			profile_pid_found = true;
			var->_bool.variable = &pgws_collector_hdr->profilePid;
			pgws_collector_hdr->profilePid = true;
		}
		else if (!strcmp(name, "pg_wait_sampling.profile_queries"))
		{
			profile_queries_found = true;
			var->_bool.variable = &pgws_collector_hdr->profileQueries;
			pgws_collector_hdr->profileQueries = true;
		}
	}

	if (!history_size_found)
		DefineCustomIntVariable("pg_wait_sampling.history_size",
				"Sets size of waits history.", NULL,
				&pgws_collector_hdr->historySize, 5000, 100, INT_MAX,
				PGC_SUSET, 0, shmem_int_guc_check_hook, NULL, NULL);

	if (!history_period_found)
		DefineCustomIntVariable("pg_wait_sampling.history_period",
				"Sets period of waits history sampling.", NULL,
				&pgws_collector_hdr->historyPeriod, 10, 1, INT_MAX,
				PGC_SUSET, 0, shmem_int_guc_check_hook, NULL, NULL);

	if (!profile_period_found)
		DefineCustomIntVariable("pg_wait_sampling.profile_period",
				"Sets period of waits profile sampling.", NULL,
				&pgws_collector_hdr->profilePeriod, 10, 1, INT_MAX,
				PGC_SUSET, 0, shmem_int_guc_check_hook, NULL, NULL);

	if (!profile_pid_found)
		DefineCustomBoolVariable("pg_wait_sampling.profile_pid",
				"Sets whether profile should be collected per pid.", NULL,
				&pgws_collector_hdr->profilePid, true,
				PGC_SUSET, 0, shmem_bool_guc_check_hook, NULL, NULL);

	if (!profile_queries_found)
		DefineCustomBoolVariable("pg_wait_sampling.profile_queries",
				"Sets whether profile should be collected per query.", NULL,
				&pgws_collector_hdr->profileQueries, true,
				PGC_SUSET, 0, shmem_bool_guc_check_hook, NULL, NULL);

	if (history_size_found
		|| history_period_found
		|| profile_period_found
		|| profile_pid_found
		|| profile_queries_found)
	{
		ProcessConfigFile(PGC_SIGHUP);
	}
}

#if PG_VERSION_NUM >= 150000
/*
 * shmem_request hook: request additional shared memory resources.
 *
 * If you change code here, don't forget to also report the modifications in
 * _PG_init() for pg14 and below.
 */
static void
pgws_shmem_request(void)
{
	if (prev_shmem_request_hook)
		prev_shmem_request_hook();

	RequestAddinShmemSpace(pgws_shmem_size());
}
#endif

/*
 * Distribute shared memory.
 */
static void
pgws_shmem_startup(void)
{
	bool		found;
	Size		segsize = pgws_shmem_size();
	void	   *pgws;
	shm_toc	   *toc;

	pgws = ShmemInitStruct("pg_wait_sampling", segsize, &found);

	if (!found)
	{
		toc = shm_toc_create(PG_WAIT_SAMPLING_MAGIC, pgws, segsize);

		pgws_collector_hdr = shm_toc_allocate(toc, sizeof(CollectorShmqHeader));
		shm_toc_insert(toc, 0, pgws_collector_hdr);
		pgws_collector_mq = shm_toc_allocate(toc, COLLECTOR_QUEUE_SIZE);
		shm_toc_insert(toc, 1, pgws_collector_mq);
		pgws_proc_queryids = shm_toc_allocate(toc,
									sizeof(uint64) * get_max_procs_count());
		shm_toc_insert(toc, 2, pgws_proc_queryids);
		MemSet(pgws_proc_queryids, 0, sizeof(uint64) * get_max_procs_count());

		/* Initialize GUC variables in shared memory */
		setup_gucs();
	}
	else
	{
		toc = shm_toc_attach(PG_WAIT_SAMPLING_MAGIC, pgws);

#if PG_VERSION_NUM >= 100000
		pgws_collector_hdr = shm_toc_lookup(toc, 0, false);
		pgws_collector_mq = shm_toc_lookup(toc, 1, false);
		pgws_proc_queryids = shm_toc_lookup(toc, 2, false);
#else
		pgws_collector_hdr = shm_toc_lookup(toc, 0);
		pgws_collector_mq = shm_toc_lookup(toc, 1);
		pgws_proc_queryids = shm_toc_lookup(toc, 2);
#endif
	}

	shmem_initialized = true;

	if (prev_shmem_startup_hook)
		prev_shmem_startup_hook();
}

/*
 * Check shared memory is initialized. Report an error otherwise.
 */
static void
check_shmem(void)
{
	if (!shmem_initialized)
	{
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
						errmsg("pg_wait_sampling shared memory wasn't initialized yet")));
	}
}

static void
pgws_cleanup_callback(int code, Datum arg)
{
	elog(DEBUG3, "pg_wait_sampling cleanup: detaching shm_mq and releasing queue lock");
	shm_mq_detach_compat(recv_mqh, recv_mq);
	LockRelease(&queueTag, ExclusiveLock, false);
}

/*
 * Module load callback
 */
void
_PG_init(void)
{
	if (!process_shared_preload_libraries_in_progress)
		return;

#if PG_VERSION_NUM < 150000
	/*
	 * Request additional shared resources.  (These are no-ops if we're not in
	 * the postmaster process.)  We'll allocate or attach to the shared
	 * resources in pgws_shmem_startup().
	 *
	 * If you change code here, don't forget to also report the modifications
	 * in pgsp_shmem_request() for pg15 and later.
	 */
	RequestAddinShmemSpace(pgws_shmem_size());
#endif

	pgws_register_wait_collector();

	/*
	 * Install hooks.
	 */
#if PG_VERSION_NUM >= 150000
	prev_shmem_request_hook = shmem_request_hook;
	shmem_request_hook		= pgws_shmem_request;
#endif
	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook		= pgws_shmem_startup;
	planner_hook_next		= planner_hook;
	planner_hook			= pgws_planner_hook;
	prev_ExecutorEnd		= ExecutorEnd_hook;
	ExecutorEnd_hook		= pgws_ExecutorEnd;
}

/*
 * Find PGPROC entry responsible for given pid assuming ProcArrayLock was
 * already taken.
 */
static PGPROC *
search_proc(int pid)
{
	int i;

	if (pid == 0)
		return MyProc;

	for (i = 0; i < ProcGlobal->allProcCount; i++)
	{
		PGPROC	*proc = &ProcGlobal->allProcs[i];
		if (proc->pid && proc->pid == pid)
		{
			return proc;
		}
	}

	ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
					errmsg("backend with pid=%d not found", pid)));
	return NULL;
}

typedef struct
{
	HistoryItem	   *items;
	TimestampTz		ts;
} WaitCurrentContext;

PG_FUNCTION_INFO_V1(pg_wait_sampling_get_current);
Datum
pg_wait_sampling_get_current(PG_FUNCTION_ARGS)
{
	FuncCallContext 	*funcctx;
	WaitCurrentContext 	*params;

	check_shmem();

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext		oldcontext;
		TupleDesc			tupdesc;

		funcctx = SRF_FIRSTCALL_INIT();

		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
		params = (WaitCurrentContext *)palloc0(sizeof(WaitCurrentContext));
		params->ts = GetCurrentTimestamp();

		funcctx->user_fctx = params;
		tupdesc = CreateTemplateTupleDescCompat(4, false);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "pid",
						   INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "type",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "event",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 4, "queryid",
						   INT8OID, -1, 0);

		funcctx->tuple_desc = BlessTupleDesc(tupdesc);

		LWLockAcquire(ProcArrayLock, LW_SHARED);

		if (!PG_ARGISNULL(0))
		{
			HistoryItem	   *item;
			PGPROC		   *proc;

			proc = search_proc(PG_GETARG_UINT32(0));
			params->items = (HistoryItem *) palloc0(sizeof(HistoryItem));
			item = &params->items[0];
			item->pid = proc->pid;
			item->wait_event_info = proc->wait_event_info;
			item->queryId = pgws_proc_queryids[proc - ProcGlobal->allProcs];
			funcctx->max_calls = 1;
		}
		else
		{
			int		procCount = ProcGlobal->allProcCount,
					i,
					j = 0;

			params->items = (HistoryItem *) palloc0(sizeof(HistoryItem) * procCount);
			for (i = 0; i < procCount; i++)
			{
				PGPROC *proc = &ProcGlobal->allProcs[i];

				if (proc != NULL && proc->pid != 0 && proc->wait_event_info)
				{
					params->items[j].pid = proc->pid;
					params->items[j].wait_event_info = proc->wait_event_info;
					params->items[j].queryId = pgws_proc_queryids[i];
					j++;
				}
			}
			funcctx->max_calls = j;
		}

		LWLockRelease(ProcArrayLock);

		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();
	params = (WaitCurrentContext *) funcctx->user_fctx;

	if (funcctx->call_cntr < funcctx->max_calls)
	{
		HeapTuple	tuple;
		Datum		values[4];
		bool		nulls[4];
		const char *event_type,
				   *event;
		HistoryItem *item;

		item = &params->items[funcctx->call_cntr];

		/* Make and return next tuple to caller */
		MemSet(values, 0, sizeof(values));
		MemSet(nulls, 0, sizeof(nulls));

		event_type = pgstat_get_wait_event_type(item->wait_event_info);
		event = pgstat_get_wait_event(item->wait_event_info);
		values[0] = Int32GetDatum(item->pid);
		if (event_type)
			values[1] = PointerGetDatum(cstring_to_text(event_type));
		else
			nulls[1] = true;
		if (event)
			values[2] = PointerGetDatum(cstring_to_text(event));
		else
			nulls[2] = true;

		values[3] = UInt64GetDatum(item->queryId);
		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);

		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
	}
	else
	{
		SRF_RETURN_DONE(funcctx);
	}
}

typedef struct
{
	Size			count;
	ProfileItem	   *items;
} Profile;

void
pgws_init_lock_tag(LOCKTAG *tag, uint32 lock)
{
	tag->locktag_field1 = PG_WAIT_SAMPLING_MAGIC;
	tag->locktag_field2 = lock;
	tag->locktag_field3 = 0;
	tag->locktag_field4 = 0;
	tag->locktag_type = LOCKTAG_USERLOCK;
	tag->locktag_lockmethodid = USER_LOCKMETHOD;
}

static void *
receive_array(SHMRequest request, Size item_size, Size *count)
{
	LOCKTAG			collectorTag;
	shm_mq_result	res;
	Size			len,
					i;
	void		   *data;
	Pointer			result,
					ptr;
	MemoryContext	oldctx;

	/* Ensure nobody else trying to send request to queue */
	pgws_init_lock_tag(&queueTag, PGWS_QUEUE_LOCK);
	LockAcquire(&queueTag, ExclusiveLock, false, false);

	pgws_init_lock_tag(&collectorTag, PGWS_COLLECTOR_LOCK);
	LockAcquire(&collectorTag, ExclusiveLock, false, false);
	recv_mq = shm_mq_create(pgws_collector_mq, COLLECTOR_QUEUE_SIZE);
	pgws_collector_hdr->request = request;
	LockRelease(&collectorTag, ExclusiveLock, false);

	if (!pgws_collector_hdr->latch)
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
						errmsg("pg_wait_sampling collector wasn't started")));

	SetLatch(pgws_collector_hdr->latch);

	shm_mq_set_receiver(recv_mq, MyProc);

	/*
	 * We switch to TopMemoryContext, so that recv_mqh is allocated there
	 * and is guaranteed to survive until before_shmem_exit callbacks are
	 * fired.  Anyway, shm_mq_detach() will free handler on its own.
	 *
	 * NB: we do not pass `seg` to shm_mq_attach(), so it won't set its own
	 * callback, i.e. we do not interfere here with shm_mq_detach_callback().
	 */
	oldctx = MemoryContextSwitchTo(TopMemoryContext);
	recv_mqh = shm_mq_attach(recv_mq, NULL, NULL);
	MemoryContextSwitchTo(oldctx);

	/*
	 * Now we surely attached to the shm_mq and got collector's attention.
	 * If anything went wrong (e.g. Ctrl+C received from the client) we have
	 * to cleanup some things, i.e. detach from the shm_mq, so collector was
	 * able to continue responding to other requests.
	 *
	 * PG_ENSURE_ERROR_CLEANUP() guaranties that cleanup callback will be
	 * fired for both ERROR and FATAL.
	 */
	PG_ENSURE_ERROR_CLEANUP(pgws_cleanup_callback, 0);
	{
		res = shm_mq_receive(recv_mqh, &len, &data, false);
		if (res != SHM_MQ_SUCCESS || len != sizeof(*count))
			elog(ERROR, "error reading mq");

		memcpy(count, data, sizeof(*count));

		result = palloc(item_size * (*count));
		ptr = result;

		for (i = 0; i < *count; i++)
		{
			res = shm_mq_receive(recv_mqh, &len, &data, false);
			if (res != SHM_MQ_SUCCESS || len != item_size)
				elog(ERROR, "error reading mq");

			memcpy(ptr, data, item_size);
			ptr += item_size;
		}
	}
	PG_END_ENSURE_ERROR_CLEANUP(pgws_cleanup_callback, 0);

	/* We still have to detach and release lock during normal operation. */
	shm_mq_detach_compat(recv_mqh, recv_mq);
	LockRelease(&queueTag, ExclusiveLock, false);

	return result;
}


PG_FUNCTION_INFO_V1(pg_wait_sampling_get_profile);
Datum
pg_wait_sampling_get_profile(PG_FUNCTION_ARGS)
{
	Profile			   *profile;
	FuncCallContext	   *funcctx;

	check_shmem();

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext		oldcontext;
		TupleDesc			tupdesc;

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* Receive profile from shmq */
		profile = (Profile *) palloc0(sizeof(Profile));
		profile->items = (ProfileItem *) receive_array(PROFILE_REQUEST,
										sizeof(ProfileItem), &profile->count);

		funcctx->user_fctx = profile;
		funcctx->max_calls = profile->count;

		/* Make tuple descriptor */
		tupdesc = CreateTemplateTupleDescCompat(5, false);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "pid",
						   INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "type",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "event",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 4, "queryid",
						   INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 5, "count",
						   INT8OID, -1, 0);
		funcctx->tuple_desc = BlessTupleDesc(tupdesc);

		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();

	profile = (Profile *) funcctx->user_fctx;

	if (funcctx->call_cntr < funcctx->max_calls)
	{
		/* for each row */
		Datum		values[5];
		bool		nulls[5];
		HeapTuple	tuple;
		ProfileItem *item;
		const char *event_type,
				   *event;

		item = &profile->items[funcctx->call_cntr];

		MemSet(values, 0, sizeof(values));
		MemSet(nulls, 0, sizeof(nulls));

		/* Make and return next tuple to caller */
		event_type = pgstat_get_wait_event_type(item->wait_event_info);
		event = pgstat_get_wait_event(item->wait_event_info);
		values[0] = Int32GetDatum(item->pid);
		if (event_type)
			values[1] = PointerGetDatum(cstring_to_text(event_type));
		else
			nulls[1] = true;
		if (event)
			values[2] = PointerGetDatum(cstring_to_text(event));
		else
			nulls[2] = true;

		if (pgws_collector_hdr->profileQueries)
			values[3] = UInt64GetDatum(item->queryId);
		else
			values[3] = (Datum) 0;

		values[4] = UInt64GetDatum(item->count);

		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);

		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
	}
	else
	{
		/* nothing left */
		SRF_RETURN_DONE(funcctx);
	}
}

PG_FUNCTION_INFO_V1(pg_wait_sampling_reset_profile);
Datum
pg_wait_sampling_reset_profile(PG_FUNCTION_ARGS)
{
	LOCKTAG		collectorTag;

	check_shmem();

	pgws_init_lock_tag(&queueTag, PGWS_QUEUE_LOCK);

	LockAcquire(&queueTag, ExclusiveLock, false, false);

	pgws_init_lock_tag(&collectorTag, PGWS_COLLECTOR_LOCK);
	LockAcquire(&collectorTag, ExclusiveLock, false, false);
	pgws_collector_hdr->request = PROFILE_RESET;
	LockRelease(&collectorTag, ExclusiveLock, false);

	SetLatch(pgws_collector_hdr->latch);

	LockRelease(&queueTag, ExclusiveLock, false);

	PG_RETURN_VOID();
}

PG_FUNCTION_INFO_V1(pg_wait_sampling_get_history);
Datum
pg_wait_sampling_get_history(PG_FUNCTION_ARGS)
{
	History				*history;
	FuncCallContext		*funcctx;

	check_shmem();

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext	oldcontext;
		TupleDesc		tupdesc;

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* Receive history from shmq */
		history = (History *) palloc0(sizeof(History));
		history->items = (HistoryItem *) receive_array(HISTORY_REQUEST,
										sizeof(HistoryItem), &history->count);

		funcctx->user_fctx = history;
		funcctx->max_calls = history->count;

		/* Make tuple descriptor */
		tupdesc = CreateTemplateTupleDescCompat(5, false);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "pid",
						   INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "sample_ts",
						   TIMESTAMPTZOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "type",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 4, "event",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 5, "queryid",
						   INT8OID, -1, 0);
		funcctx->tuple_desc = BlessTupleDesc(tupdesc);

		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();

	history = (History *) funcctx->user_fctx;

	if (history->index < history->count)
	{
		HeapTuple	tuple;
		HistoryItem *item;
		Datum		values[5];
		bool		nulls[5];
		const char *event_type,
				   *event;

		item = &history->items[history->index];

		/* Make and return next tuple to caller */
		MemSet(values, 0, sizeof(values));
		MemSet(nulls, 0, sizeof(nulls));

		event_type = pgstat_get_wait_event_type(item->wait_event_info);
		event = pgstat_get_wait_event(item->wait_event_info);
		values[0] = Int32GetDatum(item->pid);
		values[1] = TimestampTzGetDatum(item->ts);
		if (event_type)
			values[2] = PointerGetDatum(cstring_to_text(event_type));
		else
			nulls[2] = true;
		if (event)
			values[3] = PointerGetDatum(cstring_to_text(event));
		else
			nulls[3] = true;

		values[4] = UInt64GetDatum(item->queryId);
		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);

		history->index++;
		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
	}
	else
	{
		/* nothing left */
		SRF_RETURN_DONE(funcctx);
	}

	PG_RETURN_VOID();
}

/*
 * planner_hook hook, save queryId for collector
 */
static PlannedStmt *
pgws_planner_hook(Query *parse,
#if PG_VERSION_NUM >= 130000
				  const char *query_string,
#endif
				  int cursorOptions,
				  ParamListInfo boundParams)
{
	if (MyProc)
	{
		int i = MyProc - ProcGlobal->allProcs;
#if PG_VERSION_NUM >= 110000
		/*
		 * since we depend on queryId we need to check that its size
		 * is uint64 as we coded in pg_wait_sampling
		 */
		StaticAssertExpr(sizeof(parse->queryId) == sizeof(uint64),
				"queryId size is not uint64");
#else
		StaticAssertExpr(sizeof(parse->queryId) == sizeof(uint32),
				"queryId size is not uint32");
#endif
		if (!pgws_proc_queryids[i])
			pgws_proc_queryids[i] = parse->queryId;

	}

	/* Invoke original hook if needed */
	if (planner_hook_next)
		return planner_hook_next(parse,
#if PG_VERSION_NUM >= 130000
				query_string,
#endif
				cursorOptions, boundParams);

	return standard_planner(parse,
#if PG_VERSION_NUM >= 130000
				query_string,
#endif
			cursorOptions, boundParams);
}

/*
 * ExecutorEnd hook: clear queryId
 */
static void
pgws_ExecutorEnd(QueryDesc *queryDesc)
{
	if (MyProc)
		pgws_proc_queryids[MyProc - ProcGlobal->allProcs] = UINT64CONST(0);

	if (prev_ExecutorEnd)
		prev_ExecutorEnd(queryDesc);
	else
		standard_ExecutorEnd(queryDesc);
}
