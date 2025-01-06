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

#include <arpa/inet.h>

#include "access/hash.h"
#include "common/ip.h"
#include "executor/executor.h"
#include "funcapi.h"
#include "libpq/libpq-be.h"
#include "miscadmin.h"
#include "parser/scansup.h"
#include "pgstat.h"
#include "postmaster/bgworker.h"
#include "postmaster/interrupt.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/procarray.h"
#include "storage/shmem.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/timestamp.h"
#include "utils/uuid.h"

#include "pg_yb_utils.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb/yql/pggate/util/ybc_util.h"
#include "yb_query_diagnostics.h"

/* The number of columns in different versions of the view */
#define ACTIVE_SESSION_HISTORY_COLS_V1 12
#define ACTIVE_SESSION_HISTORY_COLS_V2 13
#define ACTIVE_SESSION_HISTORY_COLS_V3 14

#define MAX_NESTED_QUERY_LEVEL 64

#define set_query_id() (nested_level == 0 || \
	(yb_ash_track_nested_queries != NULL && yb_ash_track_nested_queries()))

/* GUC variables */
bool yb_enable_ash;
int yb_ash_circular_buffer_size;
int yb_ash_sampling_interval_ms;
int yb_ash_sample_size;

/* Saved hook values in case of unload */
static ExecutorStart_hook_type prev_ExecutorStart = NULL;
static ExecutorRun_hook_type prev_ExecutorRun = NULL;
static ExecutorFinish_hook_type prev_ExecutorFinish = NULL;
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;
static ProcessUtility_hook_type prev_ProcessUtility = NULL;

YbAshTrackNestedQueries yb_ash_track_nested_queries = NULL;

typedef struct YbAsh
{
	LWLock		lock;			/* Protects the circular buffer */
	int			index;			/* Index to insert new buffer entry */
	int			max_entries;	/* Maximum # of entries in the buffer */
	YBCAshSample circular_buffer[FLEXIBLE_ARRAY_MEMBER];
} YbAsh;

typedef struct YbAshNestedQueryIdStack
{
	int			top_index;		/* top index of the stack, -1 for empty stack */
	/* number of query ids not pushed due to the stack size being full */
	int			num_query_ids_not_pushed;
	uint64		query_ids[MAX_NESTED_QUERY_LEVEL];
} YbAshNestedQueryIdStack;

static YbAsh *yb_ash = NULL;
static YbAshNestedQueryIdStack query_id_stack;
static int nested_level = 0;

static void YbAshInstallHooks(void);
static int yb_ash_cb_max_entries(void);
static void YbAshSetQueryId(uint64 query_id);
static void YbAshResetQueryId(uint64 query_id);
static uint64 yb_ash_utility_query_id(const char *query, int query_len,
									  int query_location);
static void YbAshAcquireBufferLock(bool exclusive);
static void YbAshReleaseBufferLock();
static bool YbAshNestedQueryIdStackPush(uint64 query_id);
static uint64 YbAshNestedQueryIdStackPop(uint64 query_id);

static void yb_ash_ExecutorStart(QueryDesc *queryDesc, int eflags);
static void yb_ash_ExecutorRun(QueryDesc *queryDesc,
							   ScanDirection direction,
							   uint64 count, bool execute_once);
static void yb_ash_ExecutorFinish(QueryDesc *queryDesc);
static void yb_ash_ExecutorEnd(QueryDesc *queryDesc);
static void yb_ash_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
								  bool readOnlyTree,
								  ProcessUtilityContext context, ParamListInfo params,
								  QueryEnvironment *queryEnv, DestReceiver *dest,
								  QueryCompletion *qc);

static const unsigned char *get_top_level_node_id();
static void YbAshMaybeReplaceSample(PGPROC *proc, int num_procs, TimestampTz sample_time,
									int samples_considered);
static YBCWaitEventInfo YbGetWaitEventInfo(const PGPROC *proc);
static void copy_pgproc_sample_fields(PGPROC *proc, int index);
static void copy_non_pgproc_sample_fields(TimestampTz sample_time, int index);
static void YbAshIncrementCircularBufferIndex(void);
static YBCAshSample *YbAshGetNextCircularBufferSlot(void);

static void uchar_to_uuid(unsigned char *in, pg_uuid_t *out);
static void client_ip_to_string(unsigned char *client_addr, uint16 client_port,
								uint8_t addr_family, char *client_ip);
static void PrintUuidToBuffer(StringInfo buffer, unsigned char *uuid);
static int BinarySearchAshIndex(TimestampTz target_time, int left, int right);
static void GetAshRangeIndexes(TimestampTz start_time, TimestampTz end_time, int64 query_id,
							   int *start_index, int *end_index, char *description);
static void FormatAshSampleAsCsv(YBCAshSample *ash_data_buffer, int total_elements_to_dump,
								 StringInfo buffer);
static YBCAshSample *ExtractAshDataFromRange(int start_index, int end_index,
											 int *total_elements_to_dump);
void GetAshDataForQueryDiagnosticsBundle(TimestampTz start_time, TimestampTz end_time,
										 int64 query_id, StringInfo output_buffer,
										 char *description);

bool
yb_ash_circular_buffer_size_check_hook(int *newval, void **extra, GucSource source)
{
	/* Autocompute yb_ash_circular_buffer_size if zero */
	if (*newval == 0)
		*newval = YBCGetCircularBufferSizeInKiBs();

	return true;
}

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
YbAshInit(void)
{
	YbAshInstallHooks();
	/* Keep the default query id in the stack */
	query_id_stack.top_index = 0;
	query_id_stack.query_ids[0] =
		YBCGetConstQueryId(QUERY_ID_TYPE_DEFAULT);
	query_id_stack.num_query_ids_not_pushed = 0;
}

void
YbAshInstallHooks(void)
{
	prev_ExecutorStart = ExecutorStart_hook;
	ExecutorStart_hook = yb_ash_ExecutorStart;

	prev_ExecutorRun = ExecutorRun_hook;
	ExecutorRun_hook = yb_ash_ExecutorRun;

	prev_ExecutorEnd = ExecutorEnd_hook;
	ExecutorEnd_hook = yb_ash_ExecutorEnd;

	prev_ExecutorFinish = ExecutorFinish_hook;
	ExecutorFinish_hook = yb_ash_ExecutorFinish;

	prev_ProcessUtility = ProcessUtility_hook;
	ProcessUtility_hook = yb_ash_ProcessUtility;
}

/*
 * This function doesn't need locks, check the comments of
 * YbAshSetOneTimeMetadata.
 */
void
YbAshSetDatabaseId(Oid database_id)
{
	Assert(MyProc->yb_is_ash_metadata_set == false);
	MyProc->yb_ash_metadata.database_id = database_id;
}

static int
yb_ash_cb_max_entries(void)
{
	Assert(yb_ash_circular_buffer_size != 0);
	return yb_ash_circular_buffer_size * 1024 / sizeof(YBCAshSample);
}

/*
 * Push a query id to the stack. In case the stack is full, we increment
 * a counter to maintain the number of query ids which were supposed to be
 * pushed but couldn't be pushed. So that later, when we are supposed to pop
 * from the stack, we know how many no-op pop operations we have to perform.
 */
static bool
YbAshNestedQueryIdStackPush(uint64 query_id)
{
	if (query_id_stack.top_index < MAX_NESTED_QUERY_LEVEL - 1)
	{
		query_id_stack.query_ids[++query_id_stack.top_index] = query_id;
		return true;
	}

	ereport(LOG,
			(errmsg("ASH stack for nested query ids is full")));
	++query_id_stack.num_query_ids_not_pushed;
	return false;
}

/*
 * Pop and return the top query id from the stack
 */
static uint64
YbAshNestedQueryIdStackPop(uint64 query_id)
{
	if (query_id_stack.num_query_ids_not_pushed > 0)
	{
		--query_id_stack.num_query_ids_not_pushed;
		return 0;
	}

	/*
	 * When an extra ExecutorEnd is called during PortalCleanup,
	 * we shouldn't pop the incorrect query_id from the stack.
	 */
	if (query_id_stack.top_index > 0 &&
		query_id_stack.query_ids[query_id_stack.top_index] == query_id)
		return query_id_stack.query_ids[--query_id_stack.top_index];

	return 0;
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
								   sizeof(YBCAshSample)));

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

	if (!found)
	{
		LWLockInitialize(&yb_ash->lock, LWTRANCHE_YB_ASH_CIRCULAR_BUFFER);
		yb_ash->index = 0;
		yb_ash->max_entries = yb_ash_cb_max_entries();
		MemSet(yb_ash->circular_buffer, 0, yb_ash->max_entries * sizeof(YBCAshSample));
	}
}

static void
yb_ash_ExecutorStart(QueryDesc *queryDesc, int eflags)
{
	uint64 query_id;

	if (yb_enable_ash)
	{
		/* Query id can be zero here only if pg_stat_statements is disabled */
		query_id = queryDesc->plannedstmt->queryId != 0
				   ? queryDesc->plannedstmt->queryId
				   : yb_ash_utility_query_id(queryDesc->sourceText,
											 queryDesc->plannedstmt->stmt_len,
											 queryDesc->plannedstmt->stmt_location);
		YbAshSetQueryId(query_id);
	}

	PG_TRY();
	{
		if (prev_ExecutorStart)
			prev_ExecutorStart(queryDesc, eflags);
		else
			standard_ExecutorStart(queryDesc, eflags);
	}
	PG_CATCH();
	{
		if (yb_enable_ash)
			YbAshResetQueryId(query_id);

		PG_RE_THROW();
	}
	PG_END_TRY();
}

static void
yb_ash_ExecutorRun(QueryDesc *queryDesc, ScanDirection direction, uint64 count,
				   bool execute_once)
{
	++nested_level;
	PG_TRY();
	{
		if (prev_ExecutorRun)
			prev_ExecutorRun(queryDesc, direction, count, execute_once);
		else
			standard_ExecutorRun(queryDesc, direction, count, execute_once);
		--nested_level;
	}
	PG_CATCH();
	{
		--nested_level;

		if (yb_enable_ash)
			YbAshResetQueryId(queryDesc->plannedstmt->queryId);

		PG_RE_THROW();
	}
	PG_END_TRY();
}

static void
yb_ash_ExecutorFinish(QueryDesc *queryDesc)
{
	++nested_level;
	PG_TRY();
	{
		if (prev_ExecutorFinish)
			prev_ExecutorFinish(queryDesc);
		else
			standard_ExecutorFinish(queryDesc);
		--nested_level;
	}
	PG_CATCH();
	{
		--nested_level;

		if (yb_enable_ash)
			YbAshResetQueryId(queryDesc->plannedstmt->queryId);

		PG_RE_THROW();
	}
	PG_END_TRY();
}

static void
yb_ash_ExecutorEnd(QueryDesc *queryDesc)
{
	PG_TRY();
	{
		if (prev_ExecutorEnd)
			prev_ExecutorEnd(queryDesc);
		else
			standard_ExecutorEnd(queryDesc);

		if (yb_enable_ash)
			YbAshResetQueryId(queryDesc->plannedstmt->queryId);
	}
	PG_CATCH();
	{
		if (yb_enable_ash)
			YbAshResetQueryId(queryDesc->plannedstmt->queryId);

		PG_RE_THROW();
	}
	PG_END_TRY();
}

static void
yb_ash_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
					  bool readOnlyTree,
					  ProcessUtilityContext context, ParamListInfo params,
					  QueryEnvironment *queryEnv, DestReceiver *dest,
					  QueryCompletion *qc)
{
	uint64		query_id;
	bool		skip_nested_level;
	Node	   *parsetree = pstmt->utilityStmt;

	/*
	 * We don't want to set query id if the node is PREPARE, EXECUTE or
	 * DEALLOCATE because pg_stat_statements also doesn't do it. Check
	 * comments in pgss_ProcessUtility for more info.
	 */
	skip_nested_level = IsA(parsetree, PrepareStmt) || IsA(parsetree, ExecuteStmt) ||
						IsA(parsetree, DeallocateStmt);

	if (!skip_nested_level)
	{
		if (yb_enable_ash)
		{
			query_id = pstmt->queryId != 0
					   ? pstmt->queryId
					   : yb_ash_utility_query_id(queryString,
												 pstmt->stmt_len,
												 pstmt->stmt_location);
			YbAshSetQueryId(query_id);
		}
		++nested_level;
	}

	PG_TRY();
	{
		if (prev_ProcessUtility)
			prev_ProcessUtility(pstmt, queryString,
								readOnlyTree,
								context, params, queryEnv,
								dest, qc);
		else
			standard_ProcessUtility(pstmt, queryString,
									readOnlyTree,
									context, params, queryEnv,
									dest, qc);
		if (!skip_nested_level)
		{
			--nested_level;
			if (yb_enable_ash)
				YbAshResetQueryId(query_id);
		}
	}
	PG_CATCH();
	{
		if (!skip_nested_level)
		{
			--nested_level;
			if (yb_enable_ash)
				YbAshResetQueryId(query_id);
		}
		PG_RE_THROW();
	}
	PG_END_TRY();
}

static void
YbAshSetQueryId(uint64 query_id)
{
	if (set_query_id())
	{
		if (YbAshNestedQueryIdStackPush(query_id))
		{
			LWLockAcquire(&MyProc->yb_ash_metadata_lock, LW_EXCLUSIVE);
			MyProc->yb_ash_metadata.query_id = query_id;
			LWLockRelease(&MyProc->yb_ash_metadata_lock);
		}
	}
}

static void
YbAshResetQueryId(uint64 query_id)
{
	if (set_query_id())
	{
		uint64 prev_query_id = YbAshNestedQueryIdStackPop(query_id);
		if (prev_query_id != 0)
		{
			LWLockAcquire(&MyProc->yb_ash_metadata_lock, LW_EXCLUSIVE);
			MyProc->yb_ash_metadata.query_id = prev_query_id;
			LWLockRelease(&MyProc->yb_ash_metadata_lock);
		}
	}
}

/*
 * This function doesn't need locks, check the comments of
 * YbAshSetOneTimeMetadata.
 */
void
YbAshSetMetadata(void)
{
	/* The stack should have the default query id at the start of a request */
	Assert(query_id_stack.top_index == 0);
	Assert(MyProc->yb_is_ash_metadata_set == false);

	YBCGenerateAshRootRequestId(MyProc->yb_ash_metadata.root_request_id);
}

void
YbAshUnsetMetadata(void)
{
	/*
	 * Some queryids may not be popped from the stack if YbAshResetQueryId
	 * returns an error. Reset the stack here. We can remove this if we
	 * make query_id atomic
	 */
	query_id_stack.top_index = 0;
	query_id_stack.num_query_ids_not_pushed = 0;

	LWLockAcquire(&MyProc->yb_ash_metadata_lock, LW_EXCLUSIVE);
	MemSet(MyProc->yb_ash_metadata.root_request_id, 0,
		sizeof(MyProc->yb_ash_metadata.root_request_id));
	LWLockRelease(&MyProc->yb_ash_metadata_lock);
}

/*
 * Sets the client address, port and pid for ASH metadata.
 * If the address family is not AF_INET or AF_INET6, then the PGPPROC ASH metadata
 * fields for client address and port don't mean anything. Otherwise, if
 * pg_getnameinfo_all returns non-zero value, a warning is printed with the error
 * code and ASH keeps working without client address and port for the current PG
 * backend.
 *
 * Until MyProc->yb_is_ash_metadata_set is set to true, the backend won't be sampled,
 * that means there will be no readers or writers of the fields proctected by the lock,
 * that's why this function is safe without locks.
 */
void
YbAshSetOneTimeMetadata()
{
	Assert(MyProc->yb_is_ash_metadata_set == false);

	/* Set the address family, pid and null the client_addr and client_port */
	MyProc->yb_ash_metadata.pid = MyProcPid;
	MyProc->yb_ash_metadata.addr_family = AF_UNSPEC;
	MemSet(MyProc->yb_ash_metadata.client_addr, 0, 16);
	MyProc->yb_ash_metadata.client_port = 0;

	/* Background workers and bootstrap processing may have null MyProcPort */
	if (MyProcPort == NULL)
	{
		Assert(MyProc->isBackgroundWorker == true);
		return;
	}

	MyProc->yb_ash_metadata.addr_family = MyProcPort->raddr.addr.ss_family;

	switch (MyProcPort->raddr.addr.ss_family)
	{
		case AF_INET:
#ifdef HAVE_IPV6
		case AF_INET6:
#endif
			break;
		default:
			return;
	}

	char		remote_host[NI_MAXHOST];
	int			ret;

	ret = pg_getnameinfo_all(&MyProcPort->raddr.addr, MyProcPort->raddr.salen,
							 remote_host, sizeof(remote_host),
							 NULL, 0,
							 NI_NUMERICHOST | NI_NUMERICSERV);

	if (ret != 0)
	{
		ereport(WARNING,
				(errmsg("pg_getnameinfo_all while setting ash metadata failed"),
				 errdetail("%s\naddress family: %u",
						   gai_strerror(ret),
						   MyProcPort->raddr.addr.ss_family)));
		return;
	}

	clean_ipv6_addr(MyProcPort->raddr.addr.ss_family, remote_host);

	/* Setting ip address */
	inet_pton(MyProcPort->raddr.addr.ss_family, remote_host,
			  MyProc->yb_ash_metadata.client_addr);

	/* Setting port */
	MyProc->yb_ash_metadata.client_port = atoi(MyProcPort->remote_port);
}

void
YbAshSetMetadataForBgworkers(void)
{
	YBCGenerateAshRootRequestId(MyProc->yb_ash_metadata.root_request_id);
	MyProc->yb_ash_metadata.query_id =
		YBCGetConstQueryId(QUERY_ID_TYPE_BACKGROUND_WORKER);
	MyProc->yb_is_ash_metadata_set = true;
}

/*
 * Calculate the query id for utility statements. This takes parts of pgss_store
 * from pg_stat_statements.
 */
static uint64
yb_ash_utility_query_id(const char *query, int query_len, int query_location)
{
	const char *redacted_query;
	int			redacted_query_len;

	Assert(query != NULL);

	if (query_location >= 0)
	{
		Assert(query_location <= strlen(query));
		query += query_location;
		/* Length of 0 (or -1) means "rest of string" */
		if (query_len <= 0)
			query_len = strlen(query);
		else
			Assert(query_len <= strlen(query));
	}
	else
	{
		/* If query location is unknown, distrust query_len as well */
		query_location = 0;
		query_len = strlen(query);
	}

	/*
	 * Discard leading and trailing whitespace, too.  Use scanner_isspace()
	 * not libc's isspace(), because we want to match the lexer's behavior.
	 */
	while (query_len > 0 && scanner_isspace(query[0]))
		query++, query_location++, query_len--;
	while (query_len > 0 && scanner_isspace(query[query_len - 1]))
		query_len--;

	/* Use the redacted query for checking purposes. */
	YbGetRedactedQueryString(query, query_len, &redacted_query, &redacted_query_len);

	return DatumGetUInt64(hash_any_extended((const unsigned char *) redacted_query,
											redacted_query_len, 0));
}

static void
YbAshAcquireBufferLock(bool exclusive)
{
	LWLockAcquire(&yb_ash->lock, exclusive ? LW_EXCLUSIVE : LW_SHARED);
}

static void
YbAshReleaseBufferLock()
{
	LWLockRelease(&yb_ash->lock);
}

void
YbAshMain(Datum main_arg)
{
	Assert(yb_ash_circular_buffer_size != 0);
	ereport(LOG,
			(errmsg("starting bgworker yb_ash collector with circular buffer size %d bytes",
					yb_ash_circular_buffer_size * 1024)));

	/* Register functions for SIGTERM/SIGHUP management */
	pqsignal(SIGHUP, SignalHandlerForConfigReload);
	pqsignal(SIGTERM, SignalHandlerForShutdownRequest);
	pqsignal(SIGINT, SignalHandlerForShutdownRequest);
	pqsignal(SIGQUIT, SignalHandlerForCrashExit);

	/* We're now ready to receive signals */
	BackgroundWorkerUnblockSignals();

	BackgroundWorkerInitializeConnection(NULL, NULL, 0);

	/* We will always get CPU events in ASH, so don't sample ASH collector */
	MyProc->yb_is_ash_metadata_set = false;

	pgstat_report_appname("yb_ash collector");

	while (true)
	{
		TimestampTz	sample_time;
		int			rc;
		/* Wait necessary amount of time */
		rc = WaitLatch(MyLatch, WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
					   yb_ash_sampling_interval_ms, WAIT_EVENT_YB_ASH_MAIN);
		ResetLatch(MyLatch);

		/* Bailout if postmaster has died */
		if (rc & WL_POSTMASTER_DEATH)
			proc_exit(1);

		HandleMainLoopInterrupts();

		if (yb_enable_ash && yb_ash_sample_size > 0)
		{
			sample_time = GetCurrentTimestamp();

			/*
			 * The circular buffer lock is acquired in exclusive mode inside
			 * YBCStoreTServerAshSamples after getting the ASH samples from
			 * tserver and just before copying it into the buffer. The lock
			 * is released after we copy the PG samples.
			 */
			YBCStoreTServerAshSamples(&YbAshAcquireBufferLock,
									  &YbAshGetNextCircularBufferSlot,
									  sample_time);
			YbStorePgAshSamples(sample_time);
			YbAshReleaseBufferLock();
		}
	}
	proc_exit(0);
}

static const unsigned char *
get_top_level_node_id()
{
	static const unsigned char *local_tserver_uuid = NULL;
	if (!local_tserver_uuid && IsYugaByteEnabled())
		local_tserver_uuid = YBCGetLocalTserverUuid();
	return local_tserver_uuid;
}

/*
 * Increments the index to insert in the circular buffer.
 */
static void
YbAshIncrementCircularBufferIndex(void)
{
	if (++yb_ash->index == yb_ash->max_entries)
		yb_ash->index = 0;
}

static void
YbAshMaybeReplaceSample(PGPROC *proc, int num_procs, TimestampTz sample_time,
						int samples_considered)
{
	int			random_index;
	int			replace_index;

	random_index = YBCGetRandomUniformInt(1, samples_considered);

	if (random_index > yb_ash_sample_size)
		return;

	/*
	 * -1 because yb_ash->index points to where the next sample should
	 * be stored.
	 */
	replace_index = yb_ash->index - (yb_ash_sample_size - random_index) - 1;

	if (replace_index < 0)
		replace_index += yb_ash->max_entries;

	YbAshStoreSample(proc, num_procs, sample_time, replace_index);
}

void
YbAshMaybeIncludeSample(PGPROC *proc, int num_procs, TimestampTz sample_time,
						int *samples_considered)
{
	if (++(*samples_considered) <= yb_ash_sample_size)
		YbAshStoreSample(proc, num_procs, sample_time, yb_ash->index);
	else
		YbAshMaybeReplaceSample(proc, num_procs, sample_time, *samples_considered);
}

void
YbAshStoreSample(PGPROC *proc, int num_procs, TimestampTz sample_time, int index)
{
	copy_pgproc_sample_fields(proc, index);
	copy_non_pgproc_sample_fields(sample_time, index);
	YbAshIncrementCircularBufferIndex();
}

static YBCWaitEventInfo
YbGetWaitEventInfo(const PGPROC *proc)
{
	static uint32 waiting_on_tserver_code = -1;

	if (waiting_on_tserver_code == -1)
		waiting_on_tserver_code = YBCWaitEventForWaitingOnTServer();

	YBCWaitEventInfo info = {waiting_on_tserver_code, 0};

	for (size_t attempt = 0; attempt < 32; ++attempt)
	{
		const uint32 wait_event = proc->wait_event_info;
		const uint16 rpc_code = proc->yb_rpc_code;

		if (wait_event != waiting_on_tserver_code)
		{
			info.wait_event = wait_event;
			break;
		}

		if (rpc_code != 0)
		{
			info.rpc_code = rpc_code;
			break;
		}
	}

	return info;
}

static void
copy_pgproc_sample_fields(PGPROC *proc, int index)
{
	YBCAshSample *cb_sample = &yb_ash->circular_buffer[index];

	LWLockAcquire(&proc->yb_ash_metadata_lock, LW_SHARED);
	memcpy(&cb_sample->metadata, &proc->yb_ash_metadata, sizeof(YBCAshMetadata));
	LWLockRelease(&proc->yb_ash_metadata_lock);

	YBCWaitEventInfo info = YbGetWaitEventInfo(proc);
	cb_sample->encoded_wait_event_code = info.wait_event;
	cb_sample->aux_info[0] = info.rpc_code;
	cb_sample->aux_info[1] = '\0';
}

/* We don't fill the sample weight here. Check YbAshFillSampleWeight */
static void
copy_non_pgproc_sample_fields(TimestampTz sample_time, int index)
{
	YBCAshSample *cb_sample = &yb_ash->circular_buffer[index];

	/* top_level_node_id is constant for all PG samples */
	if (get_top_level_node_id())
		memcpy(cb_sample->top_level_node_id,
			   get_top_level_node_id(),
			   sizeof(cb_sample->top_level_node_id));

	/* rpc_request_id is 0 for PG samples */
	cb_sample->rpc_request_id = 0;
	cb_sample->sample_time = sample_time;
}

/*
 * While inserting samples into the circular buffer, we don't know the actual
 * number of samples considered. So after inserting all the samples, we go back
 * and update the sample weight
 */
void
YbAshFillSampleWeight(int samples_considered)
{
	int			samples_inserted;
	float		sample_weight;
	int			index;

	samples_inserted = Min(samples_considered, yb_ash_sample_size);
	sample_weight = Max(samples_considered, yb_ash_sample_size) * 1.0 / yb_ash_sample_size;
	index = yb_ash->index - 1;

	while (samples_inserted--)
	{
		if (index < 0)
			index += yb_ash->max_entries;

		yb_ash->circular_buffer[index--].sample_weight = sample_weight;
	}
}

/*
 * Returns a pointer to the circular buffer slot where the sample should be
 * inserted and increments the index.
 */
static YBCAshSample *
YbAshGetNextCircularBufferSlot(void)
{
	YBCAshSample *slot = &yb_ash->circular_buffer[yb_ash->index];
	YbAshIncrementCircularBufferIndex();
	return slot;
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
	static int  ncols = 0;

	if (ncols < ACTIVE_SESSION_HISTORY_COLS_V3)
		ncols = YbGetNumberOfFunctionOutputColumns(F_YB_ACTIVE_SESSION_HISTORY);

	/* ASH must be loaded first */
	if (!yb_ash)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("ysql_yb_enable_ash gflag must be enabled")));

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

	YbAshAcquireBufferLock(false /* exclusive */);

	for (i = 0; i < yb_ash->max_entries; ++i)
	{
		Datum		values[ncols];
		bool		nulls[ncols];
		int			j = 0;
		pg_uuid_t	root_request_id;
		pg_uuid_t	top_level_node_id;
		/* 22 bytes required for ipv4 and 48 for ipv6 (including null character) */
		char		client_node_ip[48];

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		YBCAshSample *sample = &yb_ash->circular_buffer[i];
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

		values[j++] =
			CStringGetTextDatum(YBCGetWaitEventComponent(sample->encoded_wait_event_code));
		values[j++] =
			CStringGetTextDatum(YBCGetWaitEventClass(sample->encoded_wait_event_code));
		values[j++] =
			CStringGetTextDatum(pgstat_get_wait_event(sample->encoded_wait_event_code));

		uchar_to_uuid(sample->top_level_node_id, &top_level_node_id);
		values[j++] = UUIDPGetDatum(&top_level_node_id);

		values[j++] = UInt64GetDatum(metadata->query_id);
		values[j++] = Int32GetDatum(metadata->pid);

		if (metadata->addr_family == AF_INET || metadata->addr_family == AF_INET6)
		{
			client_ip_to_string(metadata->client_addr, metadata->client_port, metadata->addr_family,
								client_node_ip);
			values[j++] = CStringGetTextDatum(client_node_ip);
		}
		else
		{
			/*
			 * internal operations such as flushes and compactions are not tied to any client
			 * and they might have the addr_family as AF_UNSPEC
			 */
			Assert(metadata->addr_family == AF_UNIX || metadata->addr_family == AF_UNSPEC);
			nulls[j++] = true;
		}

		if (sample->aux_info[0] != '\0')
		{
			/*
			 * In PG samples, the wait event aux buffer will be [ash::PggateRPC, 0, ...],
			 * the 0-th index contains the rpc enum value, the 1-st and subsequent indexes contains 0.
			 */
			values[j++] = sample->aux_info[0] != 0 && sample->aux_info[1] == 0
				? CStringGetTextDatum(YBCGetPggateRPCName(sample->aux_info[0]))
				: CStringGetTextDatum(sample->aux_info);
		}
		else
			nulls[j++] = true;

		values[j++] = Float4GetDatum(sample->sample_weight);

		if (ncols >= ACTIVE_SESSION_HISTORY_COLS_V2)
			values[j++] =
				CStringGetTextDatum(pgstat_get_wait_event_type(sample->encoded_wait_event_code));

		if (ncols >= ACTIVE_SESSION_HISTORY_COLS_V3)
			values[j++] = ObjectIdGetDatum(metadata->database_id);

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

	YbAshReleaseBufferLock();

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

/*
 * Print UUID to buffer with hyphens at specific positions.
 */
static void
PrintUuidToBuffer(StringInfo buffer, unsigned char *uuid)
{
	for (int i = 0; i < UUID_LEN; ++i)
	{
		appendStringInfo(buffer, "%02x", uuid[i]);
		/* Add hyphens to format the UUID in the standard pattern */
		if (i == 3 || i == 5 || i == 7 || i == 9)
			appendStringInfoChar(buffer, '-');
	}
}

/*
 * GetAshDataForQueryDiagnosticsBundle
 * 		This function is a part of queryDiagnostics feature and is called
 * 		at the end of the diagnostics_interval to fetch ASH's data.
 * 		This function retrieves ASH's data for the specified time range and formats it in CSV format.
 */
void
GetAshDataForQueryDiagnosticsBundle(TimestampTz start_time, TimestampTz end_time,
									int64 query_id, StringInfo output_buffer,
									char *description)
{
	YBCAshSample *ash_data_buffer = NULL;
	int			total_elements_to_dump = 0;
	int			start_index = -1;
	int			end_index = -1;

	Assert(start_time < end_time);

	YbAshAcquireBufferLock(false /* exclusive */);

	GetAshRangeIndexes(start_time, end_time, query_id, &start_index,
					   &end_index, description);

	if (start_index != -1 && end_index != -1)
		ash_data_buffer = ExtractAshDataFromRange(start_index, end_index,
												  &total_elements_to_dump);

	YbAshReleaseBufferLock();

	if (ash_data_buffer)
	{
		FormatAshSampleAsCsv(ash_data_buffer, total_elements_to_dump, output_buffer);
		pfree(ash_data_buffer);
	}
}

/*
 * BinarySearchAshIndex
 * 		Performs binary search on a range of the circular buffer to find an index.
 *
 * Returns:
 * 		Index of the element within circular buffer
 *		whose sample_time is just less than or equal to target_time.
 */
static int
BinarySearchAshIndex(TimestampTz target_time, int left, int right)
{
	Assert(left <= right);
	Assert(left >= 0 && right < yb_ash->max_entries);
	Assert(target_time >= yb_ash->circular_buffer[left].sample_time);
	Assert(target_time < yb_ash->circular_buffer[right].sample_time);

	while (left <= right)
	{
		int			mid = left + (right - left) / 2;
		TimestampTz mid_time = yb_ash->circular_buffer[mid].sample_time;

		if (target_time < mid_time)
			right = mid - 1;
		else
			left = mid + 1;
	}

	return right;
}

/*
 * GetAshRangeIndexes
 * 		Gives [start_index, end_index] ASH circular buffer range for the given time range.
 *
 * Parameters:
 * 		start_time - The start of the time range whose ASH data is to be fetched
 * 		end_time - The end of the time range whose ASH data is to be fetched
 * 		start_index - Pointer to store the start index corresponding to start_time
 * 		end_index - Pointer to store the end index corresponding to end_time
 *		query_id - Unique identifier for each query, required for logging errors
 *		description - Pointer to store error/warning descriptions
 */
static void
GetAshRangeIndexes(TimestampTz start_time, TimestampTz end_time, int64 query_id,
				   int *start_index, int *end_index, char *description)
{
	int			max_time_index = (yb_ash->index - 1 + yb_ash->max_entries) % yb_ash->max_entries;
	int			min_time_index = yb_ash->circular_buffer[yb_ash->index].sample_time ?
								 yb_ash->index : 0;
	TimestampTz buffer_min_time = yb_ash->circular_buffer[min_time_index].sample_time;
	TimestampTz buffer_max_time = yb_ash->circular_buffer[max_time_index].sample_time;
	TimestampTz buffer_first_entry_time = yb_ash->circular_buffer[0].sample_time;

	Assert(start_index != NULL);
	Assert(end_index != NULL);

	/* Time range is not there in the buffer */
	if (start_time > buffer_max_time || end_time < buffer_min_time)
	{
		YbQueryDiagnosticsAppendToDescription(description, (end_time < buffer_min_time) ?
							"ASH circular buffer has wrapped around, unable to fetch ASH data;" :
							"No data available in ASH for the given time range;");
		return;
	}

	/* Find the start_index */
	if (start_time <= buffer_min_time)
		*start_index = min_time_index;
	else if (start_time < buffer_first_entry_time)
		*start_index = BinarySearchAshIndex(start_time, yb_ash->index,
											yb_ash->max_entries - 1);
	else
		*start_index = BinarySearchAshIndex(start_time, 0, max_time_index);

	/* Find the end_index */
	if (end_time >= buffer_max_time)
		*end_index = max_time_index;
	else if (end_time < buffer_first_entry_time)
		*end_index = BinarySearchAshIndex(end_time, yb_ash->index,
										  yb_ash->max_entries - 1);
	else
		*end_index = BinarySearchAshIndex(end_time, 0, max_time_index);

	if (yb_ash->circular_buffer[*start_index].sample_time != start_time)
		*start_index = (*start_index + 1) % yb_ash->max_entries;

	Assert(*start_index >= 0 && *start_index < yb_ash->max_entries);
	Assert(*end_index >= 0 && *end_index < yb_ash->max_entries);
}

/*
 * ExtractAshDataFromRange
 * 		Extract ASH data from the circular buffer for a given range.
 *
 * Returns:
 * 		Pointer to the extracted ASH data buffer
 */
static YBCAshSample *
ExtractAshDataFromRange(int start_index, int end_index, int *total_elements_to_dump)
{
	YBCAshSample *ash_data_buffer;

	if (start_index > end_index)
	{
		/* Range wraps around the circular buffer */
		int			tail_segment_size = yb_ash->max_entries - start_index;
		int			head_segment_size = end_index + 1;

		*total_elements_to_dump = head_segment_size + tail_segment_size;
		Assert(*total_elements_to_dump > 0);

		ash_data_buffer = (YBCAshSample *) palloc((*total_elements_to_dump) *
												  sizeof(YBCAshSample));
		Assert(ash_data_buffer != NULL);

		memcpy(ash_data_buffer, &yb_ash->circular_buffer[start_index],
			   tail_segment_size * sizeof(YBCAshSample));
		memcpy(ash_data_buffer + tail_segment_size, yb_ash->circular_buffer,
			   head_segment_size * sizeof(YBCAshSample));
	}
	else
	{
		*total_elements_to_dump = end_index - start_index + 1;
		Assert(*total_elements_to_dump > 0);

		ash_data_buffer = (YBCAshSample *) palloc((*total_elements_to_dump) *
												  sizeof(YBCAshSample));
		Assert(ash_data_buffer != NULL);

		memcpy(ash_data_buffer, &yb_ash->circular_buffer[start_index],
			   (*total_elements_to_dump) * sizeof(YBCAshSample));
	}

	return ash_data_buffer;
}

static void
FormatAshSampleAsCsv(YBCAshSample *ash_data_buffer, int total_elements_to_dump,
					 StringInfo output_buffer)
{
	Assert(output_buffer != NULL);
	Assert(total_elements_to_dump > 0);

	if (total_elements_to_dump)
		appendStringInfoString(output_buffer, "sample_time,root_request_id,rpc_request_id,"
												"wait_event_component,wait_event_class,wait_event,"
												"top_level_node_id,query_id,pid,"
												"client_node_ip,wait_event_aux,sample_weight,"
												"wait_event_type,ysql_dbid\n");

	for (int i = 0; i < total_elements_to_dump; ++i)
	{
		char		client_node_ip[48];
		YBCAshSample *sample = &ash_data_buffer[i];

		if (sample->metadata.addr_family == AF_INET || sample->metadata.addr_family == AF_INET6)
			client_ip_to_string(sample->metadata.client_addr,
								sample->metadata.client_port,
								sample->metadata.addr_family, client_node_ip);
		else
		{
			Assert(sample->metadata.addr_family == AF_UNIX ||
				   sample->metadata.addr_family == AF_UNSPEC);
			client_node_ip[0] = '\0';
		}

		appendStringInfo(output_buffer, "%s,", timestamptz_to_str(sample->sample_time));
		PrintUuidToBuffer(output_buffer, sample->metadata.root_request_id);
		appendStringInfo(output_buffer, ",%ld,%s,%s,%s,",
						 (int64) sample->rpc_request_id,
						 YBCGetWaitEventComponent(sample->encoded_wait_event_code),
						 YBCGetWaitEventClass(sample->encoded_wait_event_code),
						 pgstat_get_wait_event(sample->encoded_wait_event_code));

		/* Top level node id */
		PrintUuidToBuffer(output_buffer, sample->top_level_node_id);
		appendStringInfo(output_buffer, ",%ld,%d,%s,%s,%f,%s,%d\n",
						 (int64) sample->metadata.query_id,
						 sample->metadata.pid,
						 client_node_ip,
						 sample->aux_info,
						 sample->sample_weight,
						 pgstat_get_wait_event_type(sample->encoded_wait_event_code),
						 sample->metadata.database_id);
	}
}
