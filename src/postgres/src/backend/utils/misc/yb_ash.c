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

#include "postgres.h"

#include <arpa/inet.h>

#include "access/hash.h"
#include "common/ip.h"
#include "executor/executor.h"
#include "funcapi.h"
#include "libpq/libpq-be.h"
#include "miscadmin.h"
#include "optimizer/planner.h"
#include "parser/scansup.h"
#include "pg_yb_utils.h"
#include "pgstat.h"
#include "postmaster/bgworker.h"
#include "postmaster/interrupt.h"
#include "replication/walsender.h"
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
#include "yb/yql/pggate/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb_ash.h"
#include "yb_query_diagnostics.h"

/* The number of columns in different versions of the view.
 * V7 adds plan_id (correlates with pg_stat_plans). */
#define ACTIVE_SESSION_HISTORY_COLS_V1 12
#define ACTIVE_SESSION_HISTORY_COLS_V2 13
#define ACTIVE_SESSION_HISTORY_COLS_V3 14
#define ACTIVE_SESSION_HISTORY_COLS_V4 15
#define ACTIVE_SESSION_HISTORY_COLS_V5 16
#define ACTIVE_SESSION_HISTORY_COLS_V6 17
#define ACTIVE_SESSION_HISTORY_COLS_V7 18

#define ACTIVE_SESSION_HISTORY_IN_PARAMS_V1 0
#define ACTIVE_SESSION_HISTORY_IN_PARAMS_V2 2

#define MAX_NESTED_QUERY_LEVEL 64

#define set_query_id() (nested_level == 0 || \
	(yb_ash_track_nested_queries != NULL && yb_ash_track_nested_queries()))

/* GUC variables */
bool		yb_enable_ash;
int			yb_ash_circular_buffer_size;
int			yb_ash_sampling_interval_ms;
int			yb_ash_sample_size;

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
	YbcAshSample circular_buffer[FLEXIBLE_ARRAY_MEMBER];
} YbAsh;

typedef struct YbAshNestedQueryIdStack
{
	int			top_index;		/* top index of the stack, -1 for empty stack */
	/* number of query ids not pushed due to the stack size being full */
	int			num_query_ids_not_pushed;
	uint64		query_ids[MAX_NESTED_QUERY_LEVEL];
	uint64		plan_ids[MAX_NESTED_QUERY_LEVEL];
} YbAshNestedQueryIdStack;

static YbAsh *yb_ash = NULL;
static YbAshNestedQueryIdStack query_id_stack;
static int	nested_level = 0;
static bool pop_query_id_before_push = false;
static uint64 query_id_to_be_popped_before_push = 0;

static void YbAshInstallHooks(void);
static int	yb_ash_cb_max_entries(void);
static void YbAshSetQueryId(uint64 query_id, uint64 plan_id);
static void YbAshResetQueryId(uint64 query_id);
static uint64 yb_ash_utility_query_id(const char *query, int query_len,
									  int query_location,
									  bool is_sensitive_stmt);
static void YbAshAcquireBufferLock(bool exclusive);
static void YbAshReleaseBufferLock();
static bool YbAshNestedQueryIdStackPush(uint64 query_id, uint64 plan_id);
static uint64 YbAshNestedQueryIdStackPop(uint64 query_id, uint64 *prev_plan_id);

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

static void YbAshMaybeReplaceSample(PGPROC *proc, int num_procs, TimestampTz sample_time,
									int samples_considered);
static YbcWaitEventInfo YbGetWaitEventInfo(const PGPROC *proc);
static void copy_pgproc_sample_fields(PGPROC *proc, int index);
static void copy_non_pgproc_sample_fields(TimestampTz sample_time, int index);
static void YbAshIncrementCircularBufferIndex(void);
static YbcAshSample *YbAshGetNextCircularBufferSlot(void);

static void uchar_to_uuid(unsigned char *in, pg_uuid_t *out);
static void client_ip_to_string(unsigned char *client_addr, uint16 client_port,
								uint8_t addr_family, char *client_ip);
static void GetAshRangeIndexes(TimestampTz start_time, TimestampTz end_time,
							   int *start_index, int *end_index,
							   bool is_end_time_exclusive);

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
	query_id_stack.query_ids[0] = YbAshGetConstQueryId();
	query_id_stack.plan_ids[0] = 0;
	query_id_stack.num_query_ids_not_pushed = 0;

	EnableQueryId();
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
	return yb_ash_circular_buffer_size * 1024 / sizeof(YbcAshSample);
}

/*
 * Push a (query_id, plan_id) pair to the stack. In case the stack is full,
 * we increment a counter to maintain the number of entries which were
 * supposed to be pushed but couldn't be. So that later, when we are supposed
 * to pop from the stack, we know how many no-op pop operations to perform.
 */
static bool
YbAshNestedQueryIdStackPush(uint64 query_id, uint64 plan_id)
{
	if (query_id_stack.top_index < MAX_NESTED_QUERY_LEVEL - 1)
	{
		++query_id_stack.top_index;
		query_id_stack.query_ids[query_id_stack.top_index] = query_id;
		query_id_stack.plan_ids[query_id_stack.top_index] = plan_id;
		return true;
	}

	ereport(LOG,
			(errmsg("ASH stack for nested query ids is full")));
	++query_id_stack.num_query_ids_not_pushed;
	return false;
}

/*
 * Pop the top entry from the stack and return the previous query_id.
 * Also sets *prev_plan_id to the previous entry's plan_id.
 */
static uint64
YbAshNestedQueryIdStackPop(uint64 query_id, uint64 *prev_plan_id)
{
	*prev_plan_id = 0;

	if (query_id_stack.num_query_ids_not_pushed > 0)
	{
		--query_id_stack.num_query_ids_not_pushed;
		return 0;
	}

	if (pop_query_id_before_push && query_id == query_id_to_be_popped_before_push)
		Assert(query_id_stack.top_index > 0 &&
			   query_id_stack.query_ids[query_id_stack.top_index] == query_id);

	/*
	 * When an extra ExecutorEnd is called during PortalCleanup,
	 * we shouldn't pop the incorrect query_id from the stack.
	 */
	if (query_id_stack.top_index > 0 &&
		query_id_stack.query_ids[query_id_stack.top_index] == query_id)
	{
		--query_id_stack.top_index;
		*prev_plan_id = query_id_stack.plan_ids[query_id_stack.top_index];
		return query_id_stack.query_ids[query_id_stack.top_index];
	}

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
								   sizeof(YbcAshSample)));

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
		MemSet(yb_ash->circular_buffer, 0, yb_ash->max_entries * sizeof(YbcAshSample));
	}
}

static void
yb_ash_ExecutorStart(QueryDesc *queryDesc, int eflags)
{
	uint64		query_id;
	uint64		plan_id = 0;

	if (yb_enable_ash)
	{
		/* Query id can be zero here only if pg_stat_statements is disabled */
		query_id = (queryDesc->plannedstmt->queryId != 0 ?
					queryDesc->plannedstmt->queryId :
					yb_ash_utility_query_id(queryDesc->sourceText,
											queryDesc->plannedstmt->stmt_len,
											queryDesc->plannedstmt->stmt_location,
											false /* is_sensitive_stmt */ ));
		if (queryDesc->plannedstmt->commandType != CMD_UTILITY)
			plan_id = ybGetPlanId(queryDesc->plannedstmt);
		YbAshSetQueryId(query_id, plan_id);
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
	skip_nested_level = (IsA(parsetree, PrepareStmt) ||
						 IsA(parsetree, ExecuteStmt) ||
						 IsA(parsetree, DeallocateStmt));

	if (!skip_nested_level)
	{
		if (yb_enable_ash)
		{
			/*
			 * UTILITY statements can have password tokens that require
			 * redaction
			 */
			query_id = (pstmt->queryId != 0 ?
						pstmt->queryId :
						yb_ash_utility_query_id(queryString,
												pstmt->stmt_len,
												pstmt->stmt_location,
												true /* is_sensitive_stmt */ ));
			YbAshSetQueryId(query_id, /* plan_id */ 0);
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

uint64
YbAshGetConstQueryId()
{
	YbcAshConstQueryIdType type = QUERY_ID_TYPE_DEFAULT;

	if (am_walsender)
		type = QUERY_ID_TYPE_WALSENDER;
	else if (IsBackgroundWorker)
		type = QUERY_ID_TYPE_BACKGROUND_WORKER;

	return YBCGetConstQueryId(type);
}

static void
YbAshSetQueryId(uint64 query_id, uint64 plan_id)
{
	if (set_query_id())
	{
		if (pop_query_id_before_push)
		{
			uint64		unused_plan_id;

			YbAshNestedQueryIdStackPop(query_id_to_be_popped_before_push, &unused_plan_id);
			pop_query_id_before_push = false;
			query_id_to_be_popped_before_push = 0;
		}
		if (YbAshNestedQueryIdStackPush(query_id, plan_id))
		{
			LWLockAcquire(&MyProc->yb_ash_metadata_lock, LW_EXCLUSIVE);
			MyProc->yb_ash_metadata.query_id = query_id;
			MyProc->yb_ash_metadata.plan_id = plan_id;
			LWLockRelease(&MyProc->yb_ash_metadata_lock);
		}
	}
}

static void
YbAshResetQueryId(uint64 query_id)
{
	if (set_query_id())
	{
		uint64		prev_plan_id;
		uint64		prev_query_id = YbAshNestedQueryIdStackPop(query_id, &prev_plan_id);

		if (prev_query_id != 0)
		{
			if (prev_query_id == YbAshGetConstQueryId())
			{
				/*
				 * Re-push the current entry. The popped entry's plan_id is
				 * still in the array at top_index + 1 since Pop doesn't
				 * clear popped slots.
				 */
				uint64		popped_plan_id = query_id_stack.plan_ids[query_id_stack.top_index + 1];

				query_id_to_be_popped_before_push = query_id;
				pop_query_id_before_push = true;
				YbAshNestedQueryIdStackPush(query_id, popped_plan_id);
			}
			else
			{
				LWLockAcquire(&MyProc->yb_ash_metadata_lock, LW_EXCLUSIVE);
				MyProc->yb_ash_metadata.query_id = prev_query_id;
				MyProc->yb_ash_metadata.plan_id = prev_plan_id;
				LWLockRelease(&MyProc->yb_ash_metadata_lock);
			}
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

	/* Populate current effective user id into ASH metadata */
	MyProc->yb_ash_metadata.user_id = GetUserId();
}

void
YbAshUnsetMetadata(void)
{
	/*
	 * Some queryids may not be popped from the stack if YbAshResetQueryId
	 * returns an error. Reset the stack here. We can remove this if we
	 * make query_id atomic
	 */
	if (pop_query_id_before_push)
	{
		uint64		prev_plan_id;
		uint64		prev_query_id = YbAshNestedQueryIdStackPop(query_id_to_be_popped_before_push,
																		   &prev_plan_id);

		pop_query_id_before_push = false;
		query_id_to_be_popped_before_push = 0;

		if (prev_query_id != 0)
		{
			LWLockAcquire(&MyProc->yb_ash_metadata_lock, LW_EXCLUSIVE);
			MyProc->yb_ash_metadata.query_id = prev_query_id;
			MyProc->yb_ash_metadata.plan_id = prev_plan_id;
			LWLockRelease(&MyProc->yb_ash_metadata_lock);
		}
	}

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
yb_ash_utility_query_id(const char *query, int query_len, int query_location,
						bool is_sensitive_stmt)

{
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

	/*
	 * Use the redacted query for checking purposes.
	 * The query string may include multiple statements, so consider only the
	 * substring that we are interested in for redaction. Note that the
	 * substring in question does not contain a semi-colon at the end.
	 */
	if (is_sensitive_stmt)
		query = YbGetRedactedQueryString(pnstrdup(query, query_len), &query_len);

	return DatumGetUInt64(hash_any_extended((const unsigned char *) query,
											query_len, 0));
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
		TimestampTz sample_time;
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

static YbcWaitEventInfo
YbGetWaitEventInfo(const PGPROC *proc)
{
	static uint32 waiting_on_tserver_code = -1;

	if (waiting_on_tserver_code == -1)
		waiting_on_tserver_code = YBCWaitEventForWaitingOnTServer();

	YbcWaitEventInfo info = {waiting_on_tserver_code, 0};

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
	YbcAshSample *cb_sample = &yb_ash->circular_buffer[index];

	LWLockAcquire(&proc->yb_ash_metadata_lock, LW_SHARED);
	memcpy(&cb_sample->metadata, &proc->yb_ash_metadata, sizeof(YbcAshMetadata));
	LWLockRelease(&proc->yb_ash_metadata_lock);

	YbcWaitEventInfo info = YbGetWaitEventInfo(proc);

	cb_sample->encoded_wait_event_code = info.wait_event;
	cb_sample->aux_info[0] = info.rpc_code;
	cb_sample->aux_info[1] = '\0';
}

/* We don't fill the sample weight here. Check YbAshFillSampleWeight */
static void
copy_non_pgproc_sample_fields(TimestampTz sample_time, int index)
{
	YbcAshSample *cb_sample = &yb_ash->circular_buffer[index];
	int64_t		rss_mem_bytes = 0;
	int64_t		pss_mem_bytes = 0;

	/* top_level_node_id is constant for all PG samples */
	Assert(YbGetLocalTServerUuid() != NULL);
	memcpy(cb_sample->top_level_node_id, YbGetLocalTServerUuid(),
		   sizeof(cb_sample->top_level_node_id));

	/* rpc_request_id is 0 for PG samples */
	cb_sample->rpc_request_id = 0;
	cb_sample->sample_time = sample_time;

	YbPgGetCurRssPssMemUsage(cb_sample->metadata.pid, &rss_mem_bytes, &pss_mem_bytes);
	cb_sample->metadata.pss_mem_bytes = (pss_mem_bytes != -1) ? pss_mem_bytes : rss_mem_bytes;
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
static YbcAshSample *
YbAshGetNextCircularBufferSlot(void)
{
	YbcAshSample *slot = &yb_ash->circular_buffer[yb_ash->index];

	YbAshIncrementCircularBufferIndex();
	return slot;
}

/*
 * If available, reads input from the user. Input parameters:
 *
 * start_time: Out of all the rows returned, the lowest sample_time should not
 * be less than start_time. If this is NULL, rows will be returned from the
 * earliest available time in the circular buffer
 *
 * end_time: Out of all the rows returned, the highest sample_time should not
 * be more than or equal to end_time. If this is NULL, rows will be returned
 * till the most recent available time in the circular buffer
 *
 * The start index is always inclusive and the end index is always exclusive.
 */
static void
GetAshIndexesFromInput(FunctionCallInfo fcinfo, int *start_idx, int *end_idx)
{
	int			index = yb_ash->index;
	static int	nargs = -1;
	bool		is_start_time_null = true;
	bool		is_end_time_null = true;
	TimestampTz start_time = 0;
	TimestampTz end_time = 0;

	*start_idx = -1;
	*end_idx = -1;

	if (nargs == -1)
		nargs = YbGetNumberOfFunctionInputParameters(F_YB_ACTIVE_SESSION_HISTORY);

	if (nargs == ACTIVE_SESSION_HISTORY_IN_PARAMS_V2)
	{
		is_start_time_null = PG_ARGISNULL(0);
		is_end_time_null = PG_ARGISNULL(1);

		if (!is_start_time_null)
			start_time = PG_GETARG_TIMESTAMPTZ(0);

		if (!is_end_time_null)
			end_time = PG_GETARG_TIMESTAMPTZ(1);

		/* only return error if user gave start_time > end_time */
		if (start_time > end_time && !is_start_time_null && !is_end_time_null)
		{
			YbAshReleaseBufferLock();
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("yb_active_session_history: start time is greater "
							"than end time "),
					 errdetail("Start time: %s vs End time: %s",
							   timestamptz_to_str(start_time),
							   timestamptz_to_str(end_time))));
		}
	}

	/*
	 * Either the schema is of older version, or the input
	 * parameters were not given.
	 */
	if (is_start_time_null)
	{
		if (yb_ash->circular_buffer[index].sample_time != 0)
			*start_idx = index;
		else /* buffer has not wrapped around */
			*start_idx = 0;
	}

	if (is_end_time_null)
		*end_idx = index; /* always return exclusive index */

	/*
	 * Find the indices that were not set in the above step
	 * for the given time range.
	 */
	GetAshRangeIndexes(start_time, end_time, start_idx, end_idx,
					   !is_end_time_null);
}

static void
yb_active_session_history_init(FunctionCallInfo fcinfo,
							   FuncCallContext *funcctx)
{
	MemoryContext oldcontext;
	TupleDesc	tupdesc;
	int			start_index;
	int			end_index;
	int		   *cur_index;

	funcctx = SRF_FIRSTCALL_INIT();
	oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		ereport(ERROR,
				(errmsg_internal("return type must be a row type")));

	funcctx->tuple_desc = BlessTupleDesc(tupdesc);

	/* Acquire buffer lock (will be released when iteration completes) */
	YbAshAcquireBufferLock(false /* exclusive */ );

	GetAshIndexesFromInput(fcinfo, &start_index, &end_index);

	if (start_index == -1 || end_index == -1)	/* No data */
		funcctx->max_calls = 0;
	else if (start_index > end_index)	/* Range wraps around */
		funcctx->max_calls = (yb_ash->max_entries - start_index) + end_index;
	else						/* Simple sequential range */
	{
		if (start_index != end_index)
			funcctx->max_calls = end_index - start_index;
		else
		{
			/*
			 * If start index and end index are same, either the buffer is
			 * empty, or the buffer has wrapped around and we need to scan
			 * the entire buffer.
			 */
			if (yb_ash->circular_buffer[start_index].sample_time != 0)
				funcctx->max_calls = yb_ash->max_entries;
			else
				funcctx->max_calls = 0;
		}
	}

	cur_index = (int *) palloc(sizeof(int));
	*cur_index = start_index;
	funcctx->user_fctx = cur_index;

	MemoryContextSwitchTo(oldcontext);
}

Datum
yb_active_session_history(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx = NULL;
	static int	ncols = 0;

	/* ASH must be loaded first */
	if (!yb_ash)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("ysql_yb_enable_ash gflag must be enabled")));

	if (ncols < ACTIVE_SESSION_HISTORY_COLS_V7)
		ncols = YbGetNumberOfFunctionOutputColumns(F_YB_ACTIVE_SESSION_HISTORY);

	/* Stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
		yb_active_session_history_init(fcinfo, funcctx);

	funcctx = SRF_PERCALL_SETUP();
	if (funcctx->call_cntr < funcctx->max_calls)
	{
		Datum		values[ncols];
		bool		nulls[ncols];
		HeapTuple	tuple;
		int		   *cur_index = funcctx->user_fctx;

		int			j = 0;

		YbcAshSample *sample = &yb_ash->circular_buffer[*cur_index];
		YbcAshMetadata *metadata = &sample->metadata;

		pg_uuid_t	root_request_id;
		pg_uuid_t	top_level_node_id;

		if (sample->sample_time == 0)
		{
			/* The circular buffer is not fully filled yet */
			YbAshReleaseBufferLock();
			SRF_RETURN_DONE(funcctx);
		}

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		/* Build the output tuple values */
		values[j++] = TimestampTzGetDatum(sample->sample_time);

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
			char		client_node_ip[48];	/* 22 bytes for ipv4, 48 for ipv6 */
			client_ip_to_string(metadata->client_addr, metadata->client_port,
								metadata->addr_family, client_node_ip);
			values[j++] = CStringGetTextDatum(client_node_ip);
		}
		else
		{
			/*
			 * internal operations such as flushes and compactions are not
			 * tied to any client and they might have the addr_family as
			 * AF_UNSPEC
			 */
			Assert(metadata->addr_family == AF_UNIX || metadata->addr_family == AF_UNSPEC);
			nulls[j++] = true;
		}

		if (sample->aux_info[0] != '\0')
		{
			/*
			 * In PG samples, the wait event aux buffer will be
			 * [ash::PggateRPC, 0, ...], the 0-th index contains the rpc enum
			 * value, the 1-st and subsequent indexes contains 0.
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

		if (ncols >= ACTIVE_SESSION_HISTORY_COLS_V4)
			values[j++] =
				UInt32GetDatum(YBCAshNormalizeComponentForTServerEvents(sample->encoded_wait_event_code, true));

		if (ncols >= ACTIVE_SESSION_HISTORY_COLS_V5)
			values[j++] = Int64GetDatum(metadata->pss_mem_bytes);

		if (ncols >= ACTIVE_SESSION_HISTORY_COLS_V6)
			values[j++] = ObjectIdGetDatum(metadata->user_id);

		if (ncols >= ACTIVE_SESSION_HISTORY_COLS_V7)
			values[j++] = UInt64GetDatum(metadata->plan_id);

		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);

		if (++(*cur_index) == yb_ash->max_entries)
			*cur_index = 0;

		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
	}
	else
	{
		YbAshReleaseBufferLock();
		SRF_RETURN_DONE(funcctx);
	}
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
 * Performs binary search on a range of the circular buffer to find
 * an index.
 *
 * Both left and right are inclusive bounds.
 *
 * If is_lower_bound is true, this function searches for the first
 * element in the range [left, right] which is not ordered before target.
 *
 * If is_lower_bound is false, this function searches for the first
 * element in the range [left, right] which is ordered after target.
 *
 * For example, consider this buffer of timestamps
 * [1, 1, 3, 3, 5, 5]
 * If target is 3 and is_lower_bound is true, this function would
 * return 2 (the index of the first occurence of 3)
 * If target is 3 and is_lower_bound is false, this function would
 * return 4 (the index after the last occurence of 3)
 */
static int
BinarySearchAshIndex(TimestampTz target_time, int left, int right,
					 bool is_lower_bound)
{
	YbcAshSample *cb = yb_ash->circular_buffer;
	Assert(left <= right && left >= 0 && right < yb_ash->max_entries);
	Assert(target_time >= cb[left].sample_time &&
		   target_time <= cb[right].sample_time);

	while (left <= right)
	{
		int			mid = left + (right - left) / 2;
		TimestampTz mid_time = cb[mid].sample_time;
		bool		condition = is_lower_bound
								? (mid_time < target_time)
								: (mid_time <= target_time);

		if (condition)
			left = mid + 1;
		else
			right = mid - 1;
	}

	return left;
}

/*
 * Gets start_index and end_index of the ASH circular buffer range
 * for the given time range. The start index is always inclusive
 * and the end index is always exclusive.
 *
 * is_end_time_exclusive controls whether the range is for
 * [start_time, end_time] or [start_time, end_time)
 */
static void
GetAshRangeIndexes(TimestampTz start_time, TimestampTz end_time,
				   int *start_index, int *end_index,
				   bool is_end_time_exclusive)
{
	YbcAshSample *cb = yb_ash->circular_buffer;
	int			index = yb_ash->index;
	int			max_entries = yb_ash->max_entries;

	int			max_time_index = (index - 1 + max_entries) % max_entries;
	int			min_time_index = cb[index].sample_time ? index : 0;
	TimestampTz buffer_min_time = cb[min_time_index].sample_time;
	TimestampTz buffer_max_time = cb[max_time_index].sample_time;
	TimestampTz buffer_first_entry_time = cb[0].sample_time;

	/* Time range is not there in the buffer */
	if ((start_time > 0 && start_time > buffer_max_time) ||
		(end_time > 0 && end_time < buffer_min_time))
		return;

	/* Find the start_index, if not already set */
	if (*start_index == -1)
	{
		if (start_time <= buffer_min_time)
			*start_index = min_time_index;
		else if (start_time < buffer_first_entry_time)
			*start_index = BinarySearchAshIndex(start_time, index,
												max_entries - 1,
												true /* is_lower_bound */ );
		else
			*start_index = BinarySearchAshIndex(start_time, 0, max_time_index,
												true /* is_lower_bound */ );
	}

	/* Find the end_index, if not already set */
	if (*end_index == -1)
	{
		if (end_time > buffer_max_time)
			*end_index = max_time_index + 1;
		else if (end_time < buffer_first_entry_time)
			*end_index = BinarySearchAshIndex(end_time, index, max_entries - 1,
											  is_end_time_exclusive);
		else
			*end_index = BinarySearchAshIndex(end_time, 0, max_time_index,
											  is_end_time_exclusive);
	}

	/* if upper bound is used, end index can go out of bounds */
	if (*end_index == max_entries)
		*end_index = 0;

	Assert(*start_index >= 0 && *start_index < max_entries);
	Assert(*end_index >= 0 && *end_index < max_entries);
}
