/*-------------------------------------------------------------------------
 *
 * yb_ash.c
 *    Utilities for Active Session History/Yugabyte (Postgres layer) integration
 *    that have to be defined on the PostgreSQL side.
 *
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
 *
 * IDENTIFICATION
 *	  src/backend/utils/misc/yb_ash.c
 *
 *-------------------------------------------------------------------------
 */

#include "yb_ash.h"

#include "executor/executor.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "parser/analyze.h"
#include "pgstat.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/procarray.h"
#include "storage/shmem.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/timestamp.h"
#include "utils/uuid.h"

#include "pg_yb_utils.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb/yql/pggate/util/ybc_util.h"

/* GUC variables */
int yb_ash_circular_buffer_size;
int yb_ash_sampling_interval_ms;
int yb_ash_sample_size;

/* Saved hook values in case of unload */
static post_parse_analyze_hook_type prev_post_parse_analyze_hook = NULL;
static ExecutorStart_hook_type prev_ExecutorStart = NULL;
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;
static ProcessUtility_hook_type prev_ProcessUtility = NULL;

/* Flags set by interrupt handlers for later service in the main loop. */
static volatile sig_atomic_t got_sigterm = false;
static volatile sig_atomic_t got_sighup = false;

/* Struct to store ASH samples in the circular buffer. */
typedef struct YbAshSample {
	/*
	 * Metadata of the sample.
	 * yql_endpoint_tserver_uuid and rpc_request_id are also part of the metadata,
	 * but the reason to not store them inside YBCAshMetadata is that these remain
	 * constant in PG for all the samples of a particular node. So we don't store it
	 * in YBCAshMetadata, which is stored in the procarray to save shared memory.
	 */
	YBCAshMetadata metadata;

	/*
	 * UUID of the TServer where the query generated.
	 * This remains constant for PG samples on a node, but can differ for TServer
	 * samples as TServer can be processing requests from other nodes.
	 */
	unsigned char yql_endpoint_tserver_uuid[16];

	/*
	 * A single query can generate multiple RPCs, this is used to differentiate
	 * those RPCs. This will always be 0 for PG samples
	 */
	int64_t rpc_request_id;

	/* Auxiliary information about the sample. */
	char aux_info[16];

	/* 32-bit wait event code of the sample. */
	uint32_t wait_event_code;

	/*
	 * If a certain number of samples are available and we capture a portion of
	 * them, the sample weight is the reciprocal of the captured portion or 1,
	 * whichever is maximum.
	 */
	double sample_weight;

	/* Timestamp when the sample was captured. */
	TimestampTz sample_time;
} YbAshSample;

typedef struct YbAsh
{
	LWLock		lock;			/* Protects the circular buffer */
	int			index;			/* Index to insert new buffer entry */
	int			max_entries;	/* Maximum # of entries in the buffer */
	YbAshSample circular_buffer[FLEXIBLE_ARRAY_MEMBER];
} YbAsh;

static YbAsh *yb_ash = NULL;

static int yb_ash_cb_max_entries(void);
static void yb_set_ash_metadata(uint64_t query_id);
static void yb_unset_ash_metadata();

static void yb_ash_post_parse_analyze(ParseState *pstate, Query *query);
static void yb_ash_ExecutorStart(QueryDesc *queryDesc, int eflags);
static void yb_ash_ExecutorEnd(QueryDesc *queryDesc);
static void yb_ash_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
								  ProcessUtilityContext context, ParamListInfo params,
								  QueryEnvironment *queryEnv, DestReceiver *dest,
								  char *completionTag);

static const unsigned char *get_yql_endpoint_tserver_uuid();
static void copy_pgproc_sample_fields(PGPROC *proc);
static void copy_non_pgproc_sample_fields(float8 sample_weight, TimestampTz sample_time);

static void uchar_to_uuid(unsigned char *in, pg_uuid_t *out);
static void client_ip_to_string(unsigned char *client_addr, uint16 client_port,
								uint8_t addr_family, char *client_ip);

void
YbAshRegister(void)
{
	BackgroundWorker worker;
	memset(&worker, 0, sizeof(worker));
	sprintf(worker.bgw_name, "yb_ash collector");
	sprintf(worker.bgw_type, "yb_ash collector");
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_ConsistentState;
	/* Value of 1 allows the background worker for yb_ash to restart */
	worker.bgw_restart_time = 1;
	sprintf(worker.bgw_library_name, "postgres");
	sprintf(worker.bgw_function_name, "YbAshMain");
	worker.bgw_main_arg = (Datum) 0;
	worker.bgw_notify_pid = 0;
	RegisterBackgroundWorker(&worker);
}

void
YbAshInstallHooks(void)
{
	prev_post_parse_analyze_hook = post_parse_analyze_hook;
	post_parse_analyze_hook = yb_ash_post_parse_analyze;

	prev_ExecutorStart = ExecutorStart_hook;
	ExecutorStart_hook = yb_ash_ExecutorStart;

	prev_ExecutorEnd = ExecutorEnd_hook;
	ExecutorEnd_hook = yb_ash_ExecutorEnd;

	prev_ProcessUtility = ProcessUtility_hook;
	ProcessUtility_hook = yb_ash_ProcessUtility;
}

void
YbAshSetSessionId(uint64 session_id)
{
	LWLockAcquire(&MyProc->yb_ash_metadata_lock, LW_EXCLUSIVE);
	MyProc->yb_ash_metadata.session_id = session_id;
	LWLockRelease(&MyProc->yb_ash_metadata_lock);
}

static int
yb_ash_cb_max_entries(void)
{
	return yb_ash_circular_buffer_size * 1024 / sizeof(YbAshSample);
}

/*
 * YbAshShmemSize
 *		Compute space needed for ASH-related shared memory
 */
Size
YbAshShmemSize(void)
{
	Size		size;

	size = offsetof(YbAsh, circular_buffer);
	size = add_size(size, mul_size(yb_ash_cb_max_entries(),
								   sizeof(YbAshSample)));

	return size;
}

/*
 * YbAshShmemInit
 *		Allocate and initialize ASH-related shared memory
 */
void
YbAshShmemInit(void)
{
	bool		found = false;

	yb_ash = ShmemInitStruct("yb_ash_circular_buffer",
							 YbAshShmemSize(),
							 &found);

	LWLockRegisterTranche(LWTRANCHE_YB_ASH_CIRCULAR_BUFFER, "yb_ash_circular_buffer");

	if (!found)
	{
		LWLockInitialize(&yb_ash->lock, LWTRANCHE_YB_ASH_CIRCULAR_BUFFER);
		yb_ash->index = 0;
		yb_ash->max_entries = yb_ash_cb_max_entries();
		MemSet(yb_ash->circular_buffer, 0, yb_ash->max_entries * sizeof(YbAshSample));
	}
}

static void
yb_ash_post_parse_analyze(ParseState *pstate, Query *query)
{
	if (prev_post_parse_analyze_hook)
		prev_post_parse_analyze_hook(pstate, query);

	/* query_id will be set to zero if pg_stat_statements is disabled. */
	yb_set_ash_metadata(query->queryId);
}

static void
yb_ash_ExecutorStart(QueryDesc *queryDesc, int eflags)
{
	/*
	 * In case of prepared statements, the 'Parse' phase might be skipped.
	 * We set the ASH metadata here if it's not been set yet.
	 */
	if (MyProc->yb_is_ash_metadata_set == false)
		yb_set_ash_metadata(queryDesc->plannedstmt->queryId);

	if (prev_ExecutorStart)
		prev_ExecutorStart(queryDesc, eflags);
	else
		standard_ExecutorStart(queryDesc, eflags);
}

static void
yb_ash_ExecutorEnd(QueryDesc *queryDesc)
{
	if (prev_ExecutorEnd)
		prev_ExecutorEnd(queryDesc);
	else
		standard_ExecutorEnd(queryDesc);

	/*
	 * Unset ASH metadata. Utility statements do not go through this
	 * code path.
	 */
	yb_unset_ash_metadata();
}

static void
yb_ash_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
					  ProcessUtilityContext context, ParamListInfo params,
					  QueryEnvironment *queryEnv, DestReceiver *dest,
					  char *completionTag)
{
	if (prev_ProcessUtility)
		prev_ProcessUtility(pstmt, queryString,
							context, params, queryEnv,
							dest, completionTag);
	else
		standard_ProcessUtility(pstmt, queryString,
								context, params, queryEnv,
								dest, completionTag);

	/*
	 * Unset ASH metadata in case of utility statements. This function
	 * might recurse, and we only want to unset in the last step.
	 */
	if (YBGetDdlNestingLevel() == 0)
		yb_unset_ash_metadata();
}

static void
yb_set_ash_metadata(uint64_t query_id)
{
	LWLockAcquire(&MyProc->yb_ash_metadata_lock, LW_EXCLUSIVE);

	MyProc->yb_ash_metadata.query_id = query_id;
	YBCGenerateAshRootRequestId(MyProc->yb_ash_metadata.root_request_id);
	MyProc->yb_is_ash_metadata_set = true;

	LWLockRelease(&MyProc->yb_ash_metadata_lock);
}

static void
yb_unset_ash_metadata()
{
	LWLockAcquire(&MyProc->yb_ash_metadata_lock, LW_EXCLUSIVE);

	/*
	 * when yb_is_ash_metadata_set is set to false, it ensures that we
	 * won't read the ASH metadata. So it's fine to not null out the values.
	 */
	MyProc->yb_is_ash_metadata_set = false;

	LWLockRelease(&MyProc->yb_ash_metadata_lock);
}

static void
yb_ash_sigterm(SIGNAL_ARGS)
{
	int			save_errno = errno;

	got_sigterm = true;
	SetLatch(MyLatch);

	errno = save_errno;
}

static void
yb_ash_sighup(SIGNAL_ARGS)
{
	int			save_errno = errno;

	got_sighup = true;
	SetLatch(MyLatch);

	errno = save_errno;
}

void
YbAshMain(Datum main_arg)
{
	ereport(LOG,
			(errmsg("starting bgworker yb_ash collector with max buffer entries %d",
					yb_ash->max_entries)));

	/* Register functions for SIGTERM/SIGHUP management */
	pqsignal(SIGHUP, yb_ash_sighup);
	pqsignal(SIGTERM, yb_ash_sigterm);

	/* We're now ready to receive signals */
	BackgroundWorkerUnblockSignals();

	BackgroundWorkerInitializeConnection(NULL, NULL, 0);

	pgstat_report_appname("yb_ash collector");

	while (!got_sigterm)
	{
		TimestampTz	sample_time;
		int 		rc;
		/* Wait necessary amount of time */
		rc = WaitLatch(MyLatch, WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
					   yb_ash_sampling_interval_ms, PG_WAIT_EXTENSION);
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
					(errmsg("bgworker yb_ash signal: processed SIGHUP")));
		}

		sample_time = GetCurrentTimestamp();

		if (yb_ash_sample_size > 0)
		{
			LWLockAcquire(&yb_ash->lock, LW_EXCLUSIVE);
			YbStoreAshSamples(sample_time);
			/* TODO: get tserver samples */
			LWLockRelease(&yb_ash->lock);
		}
	}
	proc_exit(0);
}

static const unsigned char *
get_yql_endpoint_tserver_uuid()
{
	static const unsigned char *local_tserver_uuid = NULL;
	if (!local_tserver_uuid && IsYugaByteEnabled())
		local_tserver_uuid = YBCGetLocalTserverUuid();
	return local_tserver_uuid;
}

/*
 * Returns true if another sample should be stored in the circular buffer.
 */
bool
YbAshStoreSample(PGPROC *proc, int num_procs, TimestampTz sample_time,
				 int *samples_stored)
{
	/*
	 * If there are less samples available than the sample size, the sample
	 * weight must be 1.
	 */
	float8 sample_weight = Max(num_procs, yb_ash_sample_size) * 1.0 / yb_ash_sample_size;

	copy_pgproc_sample_fields(proc);
	copy_non_pgproc_sample_fields(sample_weight, sample_time);

	if (++yb_ash->index == yb_ash->max_entries)
		yb_ash->index = 0;

	if (++(*samples_stored) == yb_ash_sample_size)
		return false;

	return true;
}

static void
copy_pgproc_sample_fields(PGPROC *proc)
{
	YbAshSample *cb_sample = &yb_ash->circular_buffer[yb_ash->index];

	/* TODO: Add aux info to circular buffer once it's available */
	LWLockAcquire(&proc->yb_ash_metadata_lock, LW_SHARED);
	memcpy(&cb_sample->metadata, &proc->yb_ash_metadata, sizeof(YBCAshMetadata));
	LWLockRelease(&proc->yb_ash_metadata_lock);

	cb_sample->wait_event_code = proc->wait_event_info;
}

static void
copy_non_pgproc_sample_fields(float8 sample_weight, TimestampTz sample_time)
{
	YbAshSample *cb_sample = &yb_ash->circular_buffer[yb_ash->index];

	/* yql_endpoint_tserver_uuid is constant for all PG samples */
	if (get_yql_endpoint_tserver_uuid())
		memcpy(cb_sample->yql_endpoint_tserver_uuid,
			   get_yql_endpoint_tserver_uuid(),
			   sizeof(cb_sample->yql_endpoint_tserver_uuid));

	/* rpc_request_id is 0 for PG samples */
	cb_sample->rpc_request_id = 0;
	cb_sample->sample_weight = sample_weight;
	cb_sample->sample_time = sample_time;
}

Datum
yb_active_session_history(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	int			i;
#define ACTIVE_SESSION_HISTORY_COLS 12

	/* ASH must be loaded first */
	if (!yb_ash)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("TEST_yb_enable_ash gflag must be enabled")));

	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));

	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not " \
						"allowed in this context")));

	/* Switch context to construct returned data structures */
	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	/* Build a tuple descriptor */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		ereport(ERROR,
				(errmsg_internal("return type must be a row type")));

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	LWLockAcquire(&yb_ash->lock, LW_SHARED);

	for (i = 0; i < yb_ash->max_entries; ++i)
	{
		Datum		values[ACTIVE_SESSION_HISTORY_COLS];
		bool		nulls[ACTIVE_SESSION_HISTORY_COLS];
		int			j = 0;
		pg_uuid_t	root_request_id;
		pg_uuid_t	yql_endpoint_tserver_uuid;
		/* 22 bytes required for ipv4 and 48 for ipv6 (including null character) */
		char		client_node_ip[48];

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		YbAshSample *sample = &yb_ash->circular_buffer[i];
		YBCAshMetadata *metadata = &sample->metadata;

		if (sample->sample_time != 0)
			values[j++] = TimestampTzGetDatum(sample->sample_time);
		else
			break; /* The circular buffer is not fully filled yet */

		uchar_to_uuid(metadata->root_request_id, &root_request_id);
		values[j++] = UUIDPGetDatum(&root_request_id);

		if (sample->rpc_request_id != 0)
			values[j++] = Int64GetDatum(sample->rpc_request_id);
		else
			nulls[j++] = true;

		values[j++] = CStringGetTextDatum(YBCGetWaitEventComponent(sample->wait_event_code));
		values[j++] = CStringGetTextDatum(pgstat_get_wait_event_type(sample->wait_event_code));
		values[j++] = CStringGetTextDatum(pgstat_get_wait_event(sample->wait_event_code));

		uchar_to_uuid(sample->yql_endpoint_tserver_uuid, &yql_endpoint_tserver_uuid);
		values[j++] = UUIDPGetDatum(&yql_endpoint_tserver_uuid);

		values[j++] = UInt64GetDatum(metadata->query_id);
		values[j++] = UInt64GetDatum(metadata->session_id);

		if (metadata->addr_family == AF_INET || metadata->addr_family == AF_INET6)
		{
			client_ip_to_string(metadata->client_addr, metadata->client_port, metadata->addr_family,
								client_node_ip);
			values[j++] = CStringGetTextDatum(client_node_ip);
		}
		else
		{
			Assert(metadata->addr_family == AF_UNIX);
			nulls[j++] = true;
		}

		if (sample->aux_info[0] != '\0')
			values[j++] = CStringGetTextDatum(sample->aux_info);
		else
			nulls[j++] = true;

		values[j++] = Float4GetDatum(sample->sample_weight);

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

	LWLockRelease(&yb_ash->lock);

#undef ACTIVE_SESSION_HISTORY_COLS

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	return (Datum) 0;
}

static void
uchar_to_uuid(unsigned char *in, pg_uuid_t *out)
{
	memcpy(out->data, in, UUID_LEN);
}

static void
client_ip_to_string(unsigned char *client_addr, uint16 client_port,
					uint8_t addr_family, char *client_ip)
{
	if (addr_family == AF_INET)
	{
		sprintf(client_ip, "%d.%d.%d.%d:%d",
				client_addr[0], client_addr[1], client_addr[2], client_addr[3],
				client_port);
	}
	else
	{
		sprintf(client_ip,
				"[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]:%d",
				client_addr[0], client_addr[1], client_addr[2], client_addr[3],
				client_addr[4], client_addr[5], client_addr[6], client_addr[7],
				client_addr[8], client_addr[9], client_addr[10], client_addr[11],
				client_addr[12], client_addr[13], client_addr[14], client_addr[15],
				client_port);
	}
}
