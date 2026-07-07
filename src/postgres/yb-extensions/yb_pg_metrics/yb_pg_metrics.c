/*-------------------------------------------------------------------------
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
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <assert.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/ip.h"
#include "datatype/timestamp.h"
#include "executor/instrument.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "pg_yb_utils.h"
#include "pgstat.h"
#include "postmaster/bgworker.h"
#include "postmaster/postmaster.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/proc.h"
#include "storage/procarray.h"
#include "storage/shmem.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/catcache.h"
#include "utils/datetime.h"
#include "utils/relcache.h"
#include "utils/syscache.h"
#include "yb/yql/pggate/webserver/ybc_pg_webserver_wrapper.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb_internal_conn.h"

#define YSQL_METRIC_PREFIX "yb_ysqlserver_"
#define YSQL_LATENCY_METRIC_PREFIX "handler_latency_yb_ysqlserver_SQLProcessor_"

#define NumBackendStatSlots (MaxBackends + NUM_AUXPROCTYPES)

#define YbPgmDbCatalogCacheMetricsDefaultEntries (MaxConnections + 1)
#define YbPgmDbCatalogCacheMetricsMinEntries 1
#define YbPgmDbCatalogCacheMetricsMaxEntries 10001

PG_MODULE_MAGIC;

typedef enum YbStatementType
{
	Select,
	Insert,
	Delete,
	Update,
	Begin,
	Commit,
	Rollback,
	Other,
	Single_Shard_Transaction,
	SingleShardTransaction,
	Transaction,
	AggregatePushdown,
	CatCacheRefresh,
	CatCacheDeltaRefresh,
	CatCacheMisses,
	HintCacheRefresh,
	HintCacheHits,
	HintCacheMisses,
	AuthorizedConnection,
	RelCachePreload,
	RegularBackendInitLatency,
	AuthBackendInitLatency, /* Connmgr Auth backend only */
	RelCacheInitFileRevalidated,
	RelCacheInitFileRevalidationFailed,
	kMaxStatementType
} YbStatementType;
int			num_entries = kMaxStatementType;
YbcPgmEntry *ybpgm_table = NULL;

/*
 * Stores catalog cache miss metrics for individual DBs for per-DB metrics.
 * db_oid is initialized to InvalidOid when unused.
 */
typedef struct YbpgmCatalogCacheMetricsEntry
{
	pg_atomic_uint32 db_oid;
	pg_atomic_uint64 cache_id_misses[SysCacheSize];
	pg_atomic_uint64 cache_table_misses[YbNumCatalogCacheTables];
	pg_atomic_uint64 cache_list_misses[YbNumCatalogCacheTables];
	pg_atomic_uint64 cache_neg_misses[YbNumCatalogCacheTables];
} YbpgmCatalogCacheMetricsEntry;

/*
 * Array of catalog cache metrics entries for individual DBs. The last entry is used
 * to aggregate and collect all metrics of DBs that does not have a slot in the array
 * when every other slot is claimed. It will always have db_oid set to InvalidOid.
 *
 * This array currently does not have an eviction policy. Any DB that has published metrics
 * and disconnects will take up the slot permanently.
 */
static YbpgmCatalogCacheMetricsEntry *ybpgm_catalog_cache_metrics_array = NULL;

/*
 * Size of ybpgm_catalog_cache_metrics_array, controlled by the
 * yb_pg_metrics.db_catalog_cache_metrics_max_entries GUC.
 */
static int	yb_db_catalog_cache_metrics_max_entries;

/* Statement nesting level is used when setting up dml statements.
 * - Some state variables are set up for the top-level query but not the nested query.
 * - Time recorder is initialized and used for top-level query only.
 */
static int	statement_nesting_level = 0;

/* Block nesting level is used when setting up execution block such as "DO $$ ... END $$;".
 * - Some state variables are set up for the top level block but not the nested blocks.
 */
static int	block_nesting_level = 0;

/*
 * Flag to determine whether a transaction block has been entered.
 */
static bool is_inside_transaction_block = false;

/*
 * Flag to determine whether a DML or Other statement type has been executed.
 * Multiple statements will count as a single transaction within a transaction block.
 * DDL statements which are autonomous will be counted as its own transaction
 * even within a transaction block.
 */
static bool is_statement_executed = false;

char	   *metric_node_name = NULL;
struct WebserverWrapper *webserver = NULL;
int			port = 0;
static bool log_accesses = false;
static bool log_tcmalloc_stats = false;
static int	webserver_profiler_sample_period_bytes = 0;
static int	num_backends = 0;
static YbcRpczEntry *rpcz = NULL;
static MemoryContext ybrpczMemoryContext = NULL;
PgBackendStatus *backendStatusArray = NULL;
extern int	MaxConnections;

static long last_catcache_refresh_val = 0;
static long last_catcache_delta_refresh_val = 0;
static long last_cache_misses_val = 0;
static long last_cache_id_misses_val[SysCacheSize] = {0};
static long last_cache_table_misses_val[YbNumCatalogCacheTables] = {0};
static long last_cache_list_misses_val[YbNumCatalogCacheTables] = {0};
static long last_cache_neg_misses_val[YbNumCatalogCacheTables] = {0};

static long last_hint_cache_refreshes_val = 0;
static long last_hint_cache_hits_val = 0;
static long last_hint_cache_misses_val = 0;

static long last_authorized_connection_val = 0;
static long last_relcache_preload_val = 0;
static long last_relcache_init_file_revalidated_val = 0;
static long last_relcache_init_file_revalidation_failed_val = 0;

static volatile sig_atomic_t got_SIGHUP = false;
static volatile sig_atomic_t got_SIGTERM = false;

void		_PG_init(void);

/*
 * Variables used for storing the previous values of used hooks.
 */
static shmem_request_hook_type prev_shmem_request_hook = NULL;
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;
static ExecutorStart_hook_type prev_ExecutorStart = NULL;
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;
static ExecutorRun_hook_type prev_ExecutorRun = NULL;
static ExecutorFinish_hook_type prev_ExecutorFinish = NULL;
static ProcessUtility_hook_type prev_ProcessUtility = NULL;

static void set_metric_names(void);
static void ybpgm_shmem_request(void);
static void ybpgm_startup_hook(void);
static Size ybpgm_memsize(void);
static bool isTopLevelStatement(void);
static void ybpgm_ExecutorStart(QueryDesc *queryDesc, int eflags);
static void ybpgm_ExecutorRun(QueryDesc *queryDesc, ScanDirection direction,
							  uint64 count, bool execute_once);
static void ybpgm_ExecutorFinish(QueryDesc *queryDesc);
static void ybpgm_ExecutorEnd(QueryDesc *queryDesc);
static void ybpgm_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
								 bool readOnlyTree,
								 ProcessUtilityContext context,
								 ParamListInfo params,
								 QueryEnvironment *queryEnv,
								 DestReceiver *dest, QueryCompletion *qc);
static void ybpgm_Store(YbStatementType type, uint64_t time, uint64_t rows);
static void ybpgm_StoreCount(YbStatementType type, uint64_t time, uint64_t count);

static Size YbpgmCatalogCacheMetricsShmemSize(void);
static YbpgmCatalogCacheMetricsEntry *ybpgm_find_or_claim_db_slot(Oid db_oid);
static void ybpgm_update_db_catalog_cache_metrics(long *current_id_misses,
												  long *current_table_misses,
												  long *current_list_misses,
												  long *current_neg_misses);
static void ybpgm_iterate_db_catalog_cache_metrics(YbcDbCatalogCacheMetricVisitor visitor,
												   void *arg);

static void ws_sighup_handler(SIGNAL_ARGS);
static void ws_sigterm_handler(SIGNAL_ARGS);

/*
 * Function used for checking if the current statement is a top level statement.
 */
bool
isTopLevelStatement(void)
{
	return statement_nesting_level == 0;
}

static void
IncStatementNestingLevel(void)
{
	statement_nesting_level++;
}

static void
DecStatementNestingLevel(void)
{
	statement_nesting_level--;
}

bool
isTopLevelBlock(void)
{
	return block_nesting_level == 0;
}

static void
IncBlockNestingLevel(void)
{
	block_nesting_level++;
}

static void
DecBlockNestingLevel(void)
{
	block_nesting_level--;
}

void
set_metric_names(void)
{
	for (int i = 0; i < kMaxStatementType; i++)
	{
		ybpgm_table[i].table_name[0] = '\0';
		ybpgm_table[i].count_help[0] = '\0';
		ybpgm_table[i].sum_help[0] = '\0';
	}

	strcpy(ybpgm_table[Select].name, YSQL_LATENCY_METRIC_PREFIX "SelectStmt");
	strcpy(ybpgm_table[Insert].name, YSQL_LATENCY_METRIC_PREFIX "InsertStmt");
	strcpy(ybpgm_table[Delete].name, YSQL_LATENCY_METRIC_PREFIX "DeleteStmt");
	strcpy(ybpgm_table[Update].name, YSQL_LATENCY_METRIC_PREFIX "UpdateStmt");
	strcpy(ybpgm_table[Begin].name, YSQL_LATENCY_METRIC_PREFIX "BeginStmt");
	strcpy(ybpgm_table[Commit].name, YSQL_LATENCY_METRIC_PREFIX "CommitStmt");
	strcpy(ybpgm_table[Rollback].name, YSQL_LATENCY_METRIC_PREFIX "RollbackStmt");
	strcpy(ybpgm_table[Other].name, YSQL_LATENCY_METRIC_PREFIX "OtherStmts");
	/* Deprecated. Names with "_"s may cause confusion to metric consumers. */
	strcpy(ybpgm_table[Single_Shard_Transaction].name,
		   YSQL_LATENCY_METRIC_PREFIX "Single_Shard_Transactions");
	strcpy(ybpgm_table[SingleShardTransaction].name,
		   YSQL_LATENCY_METRIC_PREFIX "SingleShardTransactions");
	strcpy(ybpgm_table[Transaction].name, YSQL_LATENCY_METRIC_PREFIX "Transactions");
	strcpy(ybpgm_table[AggregatePushdown].name,
		   YSQL_LATENCY_METRIC_PREFIX "AggregatePushdowns");
	strcpy(ybpgm_table[CatCacheRefresh].name,
		   YSQL_METRIC_PREFIX "CatCacheRefresh");
	strcpy(ybpgm_table[CatCacheDeltaRefresh].name,
		   YSQL_METRIC_PREFIX "CatCacheDeltaRefresh");
	strcpy(ybpgm_table[CatCacheMisses].name, YSQL_METRIC_PREFIX "CatalogCacheMisses");

	strcpy(ybpgm_table[HintCacheRefresh].name,
		   YSQL_METRIC_PREFIX "HintCacheRefresh");
	strcpy(ybpgm_table[HintCacheHits].name,
		   YSQL_METRIC_PREFIX "HintCacheHits");
	strcpy(ybpgm_table[HintCacheMisses].name,
		   YSQL_METRIC_PREFIX "HintCacheMisses");

	strcpy(ybpgm_table[AuthorizedConnection].name,
		   YSQL_METRIC_PREFIX "AuthorizedConnection");
	strcpy(ybpgm_table[RelCachePreload].name,
		   YSQL_METRIC_PREFIX "RelCachePreload");
	strcpy(ybpgm_table[RegularBackendInitLatency].name,
		   YSQL_LATENCY_METRIC_PREFIX "BackendInit");
	strcpy(ybpgm_table[AuthBackendInitLatency].name,
		   YSQL_LATENCY_METRIC_PREFIX "ConnMgrAuthBackendInit");

	strcpy(ybpgm_table[RelCacheInitFileRevalidated].name,
		   YSQL_METRIC_PREFIX "RelCacheInitFileRevalidated");
	strcpy(ybpgm_table[RelCacheInitFileRevalidationFailed].name,
		   YSQL_METRIC_PREFIX "RelCacheInitFileRevalidationFailed");

	strcpy(ybpgm_table[Select].count_help,
		   "Number of SELECT statements that have been executed");
	strcpy(ybpgm_table[Select].sum_help,
		   "Total time (microseconds) spent executing SELECT statements");

	strcpy(ybpgm_table[Insert].count_help,
		   "Number of INSERT statements that have been executed");
	strcpy(ybpgm_table[Insert].sum_help,
		   "Total time (microseconds) spent executing INSERT statements");

	strcpy(ybpgm_table[Delete].count_help,
		   "Number of DELETE statements that have been executed");
	strcpy(ybpgm_table[Delete].sum_help,
		   "Total time (microseconds) spent executing DELETE statements");

	strcpy(ybpgm_table[Update].count_help,
		   "Number of UPDATE statements that have been executed");
	strcpy(ybpgm_table[Update].sum_help,
		   "Total time (microseconds) spent executing UPDATE statements");

	strcpy(ybpgm_table[Begin].count_help,
		   "Number of BEGIN statements that have been executed");
	strcpy(ybpgm_table[Begin].sum_help,
		   "Total time (microseconds) spent executing BEGIN statements");

	strcpy(ybpgm_table[Commit].count_help,
		   "Number of COMMIT statements that have been executed");
	strcpy(ybpgm_table[Commit].sum_help,
		   "Total time (microseconds) spent executing COMMIT statements");

	strcpy(ybpgm_table[Rollback].count_help,
		   "Number of ROLLBACK statements that have been executed");
	strcpy(ybpgm_table[Rollback].sum_help,
		   "Total time (microseconds) spent executing ROLLBACK statements");

	strcpy(ybpgm_table[Other].count_help,
		   "Number of other statements that have been executed");
	strcpy(ybpgm_table[Other].sum_help,
		   "Total time (microseconds) spent executing other statements");

	strcpy(ybpgm_table[Single_Shard_Transaction].count_help,
		   "Number of single shard transactions that have been executed (deprecated)");
	strcpy(ybpgm_table[Single_Shard_Transaction].sum_help,
		   "Total time (microseconds) spent executing single shard transactions (deprecated)");

	strcpy(ybpgm_table[SingleShardTransaction].count_help,
		   "Number of single shard transactions that have been executed");
	strcpy(ybpgm_table[SingleShardTransaction].sum_help,
		   "Total time (microseconds) spent executing single shard transactions");

	strcpy(ybpgm_table[Transaction].count_help,
		   "Number of transactions that have been executed");
	strcpy(ybpgm_table[Transaction].sum_help,
		   "Total time (microseconds) spent executing transactions");

	strcpy(ybpgm_table[AggregatePushdown].count_help,
		   "Number of aggregate pushdowns");
	strcpy(ybpgm_table[AggregatePushdown].sum_help,
		   "Total time (microseconds) spent executing aggregate pushdowns");
	strcpy(ybpgm_table[CatCacheRefresh].count_help,
		   "Number of full catalog cache refreshes");
	strcpy(ybpgm_table[CatCacheRefresh].sum_help,
		   "Not applicable");
	strcpy(ybpgm_table[CatCacheDeltaRefresh].count_help,
		   "Number of incremental catalog cache refreshes");
	strcpy(ybpgm_table[CatCacheDeltaRefresh].sum_help,
		   "Not applicable");


	strcpy(ybpgm_table[CatCacheMisses].count_help,
		   "Total number of catalog cache misses");
	strcpy(ybpgm_table[CatCacheMisses].sum_help, "Not applicable");

	strcpy(ybpgm_table[HintCacheRefresh].count_help,
		   "Number of hint cache refreshes");
	strcpy(ybpgm_table[HintCacheRefresh].sum_help, "Not applicable");

	strcpy(ybpgm_table[HintCacheHits].count_help,
		   "Number of hint cache hits");
	strcpy(ybpgm_table[HintCacheHits].sum_help, "Not applicable");

	strcpy(ybpgm_table[HintCacheMisses].count_help,
		   "Number of hint cache misses");
	strcpy(ybpgm_table[HintCacheMisses].sum_help, "Not applicable");

	strcpy(ybpgm_table[AuthorizedConnection].count_help,
		   "Number of authorized connections");
	strcpy(ybpgm_table[AuthorizedConnection].sum_help, "Not applicable");

	strcpy(ybpgm_table[RelCachePreload].count_help,
		   "Number of relcache preloads");
	strcpy(ybpgm_table[RelCachePreload].sum_help, "Not applicable");

	strcpy(ybpgm_table[RegularBackendInitLatency].count_help,
		   "Number of regular backend connections established");
	strcpy(ybpgm_table[RegularBackendInitLatency].sum_help,
		   "Total time (microseconds) spent initializing regular backend connections");

	strcpy(ybpgm_table[AuthBackendInitLatency].count_help,
		   "Number of auth backend connections established (connection manager only)");
	strcpy(ybpgm_table[AuthBackendInitLatency].sum_help,
		   "Total time (microseconds) spent initializing auth backend connections");

	strcpy(ybpgm_table[RelCacheInitFileRevalidated].count_help,
		   "Number of successful relcache init file revalidations");
	strcpy(ybpgm_table[RelCacheInitFileRevalidated].sum_help, "Not applicable");

	strcpy(ybpgm_table[RelCacheInitFileRevalidationFailed].count_help,
		   "Number of failed relcache init file revalidations");
	strcpy(ybpgm_table[RelCacheInitFileRevalidationFailed].sum_help, "Not applicable");
}

/*
 * Function to calculate milliseconds elapsed from start_time to stop_time.
 */
int64
getElapsedMs(TimestampTz start_time, TimestampTz stop_time)
{
	long		secs;
	int			microsecs;

	TimestampDifference(start_time, stop_time, &secs, &microsecs);

	long		millisecs = (secs * 1000) + (microsecs / 1000);

	return millisecs;
}

void
pullRpczEntries(void)
{
	ybrpczMemoryContext = AllocSetContextCreate(TopMemoryContext,
												"YB RPCz memory context",
												ALLOCSET_SMALL_SIZES);

	MemoryContext oldcontext = MemoryContextSwitchTo(ybrpczMemoryContext);

	rpcz = (YbcRpczEntry *) palloc(sizeof(YbcRpczEntry) * NumBackendStatSlots);

	num_backends = NumBackendStatSlots;
	volatile PgBackendStatus *beentry = backendStatusArray;

	for (int i = 0; i < NumBackendStatSlots; i++)
	{
		/*
		 * To prevent locking overhead, the BackendStatusArray in postgres
		 * maintains a st_changecount field for each entry. This field is
		 * incremented once before a backend starts modifying the entry, and
		 * once after it is done modifying the entry. So, we check if
		 * st_changecount changes while we're copying the entry or if its odd.
		 * The check for odd is needed for when a backend has begun changing
		 * the entry but hasn't finished.
		 */
		int			attempt = 1;

		while (yb_pgstat_log_read_activity(beentry, ++attempt))
		{
			int			before_changecount;
			int			after_changecount;

			before_changecount = beentry->st_changecount;

			rpcz[i].proc_id = beentry->st_procpid;

			/* avoid filling any more fields if invalid */
			if (beentry->st_procpid <= 0)
				break;

			rpcz[i].db_oid = beentry->st_databaseid;
			rpcz[i].query_id = beentry->st_query_id;
			rpcz[i].leader_pid = -1;
			PGPROC	   *proc = NULL;

			if (beentry->st_backendType == B_BACKEND ||
				YbIsInternalConnBackendType(beentry->st_backendType))
				proc = BackendPidGetProc(rpcz[i].proc_id);
			else if (beentry->st_backendType != YB_YSQL_CONN_MGR &&
					 beentry->st_backendType != YB_YSQL_CONN_MGR_CTRL)
			{
				/*
				 * For an auxiliary process, retrieve process info from
				 * AuxiliaryProcs stored in shared-memory.
				 */
				proc = AuxiliaryPidGetProc(beentry->st_procpid);
			}

			if (proc != NULL)
			{
				PGPROC	   *leader = proc->lockGroupLeader;

				if (leader != NULL)
				{
					rpcz[i].leader_pid = leader->pid;
				}
			}

			rpcz[i].query = (char *) palloc(pgstat_track_activity_query_size);
			strcpy(rpcz[i].query, (char *) beentry->st_activity_raw);

			rpcz[i].application_name = (char *) palloc(NAMEDATALEN);
			strcpy(rpcz[i].application_name, (char *) beentry->st_appname);

			rpcz[i].db_name = (char *) palloc(NAMEDATALEN);
			strcpy(rpcz[i].db_name, beentry->st_databasename);

			rpcz[i].process_start_timestamp = beentry->st_proc_start_timestamp;
			rpcz[i].transaction_start_timestamp = beentry->st_xact_start_timestamp;
			rpcz[i].query_start_timestamp = beentry->st_activity_start_timestamp;

			rpcz[i].backend_type = (char *) palloc(40);
			strcpy(rpcz[i].backend_type, GetBackendTypeDesc(beentry->st_backendType));

			rpcz[i].backend_active = 0;
			rpcz[i].backend_status = (char *) palloc(30);
			switch (beentry->st_state)
			{
				case STATE_IDLE:
					strcpy(rpcz[i].backend_status, "idle");
					break;
				case STATE_RUNNING:
					rpcz[i].backend_active = 1;
					strcpy(rpcz[i].backend_status, "active");
					break;
				case STATE_IDLEINTRANSACTION:
					strcpy(rpcz[i].backend_status, "idle in transaction");
					break;
				case STATE_FASTPATH:
					rpcz[i].backend_active = 1;
					strcpy(rpcz[i].backend_status, "fastpath function call");
					break;
				case STATE_IDLEINTRANSACTION_ABORTED:
					strcpy(rpcz[i].backend_status, "idle in transaction (aborted)");
					break;
				case STATE_DISABLED:
					strcpy(rpcz[i].backend_status, "disabled");
					break;
				case STATE_UNDEFINED:
					strcpy(rpcz[i].backend_status, "");
					break;
			}

			char		remote_host[NI_MAXHOST];
			char		remote_port[NI_MAXSERV];
			int			ret;

			remote_host[0] = '\0';
			remote_port[0] = '\0';
			ret = pg_getnameinfo_all((struct sockaddr_storage *) &beentry->st_clientaddr.addr,
									 beentry->st_clientaddr.salen,
									 remote_host, sizeof(remote_host),
									 remote_port, sizeof(remote_port),
									 NI_NUMERICHOST | NI_NUMERICSERV);
			if (ret == 0)
			{
				rpcz[i].host = (char *) palloc(NI_MAXHOST);
				rpcz[i].port = (char *) palloc(NI_MAXSERV);
				clean_ipv6_addr(beentry->st_clientaddr.addr.ss_family, remote_host);
				strcpy(rpcz[i].host, remote_host);
				strcpy(rpcz[i].port, remote_port);
			}
			else
			{
				rpcz[i].host = NULL;
				rpcz[i].port = NULL;
			}
			after_changecount = beentry->st_changecount;

			if (before_changecount == after_changecount &&
				(before_changecount & 1) == 0)
				break;
		}
		beentry++;
	}
	MemoryContextSwitchTo(oldcontext);
}

void
freeRpczEntries(void)
{
	MemoryContextDelete(ybrpczMemoryContext);
	ybrpczMemoryContext = NULL;
}

/* SIGHUP: set flag to re-read config file at next convenient time */
void
ws_sighup_handler(SIGNAL_ARGS)
{
	int			save_errno = errno;

	got_SIGHUP = true;
	SetLatch(MyLatch);

	errno = save_errno;
}


/* SIGTERM: time to die */
static void
ws_sigterm_handler(SIGNAL_ARGS)
{
	int			save_errno = errno;

	got_SIGTERM = true;
	SetLatch(MyLatch);

	errno = save_errno;
}

/*
 * Function that is executed when the YSQL webserver process is started.
 * We don't use the argument "unused", however, a postgres background worker's function
 * is required to have an argument of type Datum.
 */
void
webserver_worker_main(Datum unused)
{
	YBCInitThreading();
	/*
	* We call YBCInit here so that HandleYBStatus can correctly report potential error.
	*/
	HandleYBStatus(YBCInit(NULL /* argv[0] */ ,
						   palloc,
						   NULL /* cstring_to_text_with_len_fn */ ,
						   NULL /* YbSwitchPgGateMemoryContext */ ,
						   NULL /* YbCreatePgGateMemoryContext */ ,
						   NULL /* YbDeletePgGateMemoryContext */ ));

	backendStatusArray = getBackendStatusArray();

	BackgroundWorkerUnblockSignals();

	/*
	* Assert that shared memory is allocated to backendStatusArray before this webserver
	* is started.
	*/
	if (!backendStatusArray)
		ereport(FATAL,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("shared memory not allocated to BackendStatusArray before starting YSQL webserver")));

	webserver = CreateWebserver(ListenAddresses, port);

	RegisterMetrics(ybpgm_table, num_entries, metric_node_name);
	RegisterDbCatalogCacheMetrics(ybpgm_iterate_db_catalog_cache_metrics);

	YbcPostgresCallbacks callbacks;

	callbacks.pullRpczEntries = pullRpczEntries;
	callbacks.freeRpczEntries = freeRpczEntries;
	callbacks.getTimestampTz = GetCurrentTimestamp;
	callbacks.getTimestampTzDiffMs = getElapsedMs;
	callbacks.getTimestampTzToStr = timestamptz_to_str;

	YbcConnectionMetrics conn_metrics;

	conn_metrics.max_conn = &MaxConnections;
	conn_metrics.too_many_conn = yb_too_many_conn;
	conn_metrics.new_conn = yb_new_conn;

	RegisterRpczEntries(&callbacks, &num_backends, &rpcz, &conn_metrics);
	HandleYBStatus(StartWebserver(webserver));

	pqsignal(SIGHUP, ws_sighup_handler);
	pqsignal(SIGTERM, ws_sigterm_handler);

	SetWebserverConfig(webserver, log_accesses, log_tcmalloc_stats,
					   webserver_profiler_sample_period_bytes);

	int			rc;

	while (!got_SIGTERM)
	{
		rc = WaitLatch(MyLatch, WL_LATCH_SET | WL_POSTMASTER_DEATH, -1, PG_WAIT_EXTENSION);
		ResetLatch(MyLatch);

		if (rc & WL_POSTMASTER_DEATH)
			break;

		if (got_SIGHUP)
		{
			got_SIGHUP = false;
			ProcessConfigFile(PGC_SIGHUP);
			SetWebserverConfig(webserver, log_accesses, log_tcmalloc_stats,
							   webserver_profiler_sample_period_bytes);
		}
	}

	if (webserver)
	{
		DestroyWebserver(webserver);
		webserver = NULL;
	}

	if (rpcz != NULL && ybrpczMemoryContext != NULL)
	{
		MemoryContext oldcontext = MemoryContextSwitchTo(ybrpczMemoryContext);

		pfree(rpcz);
		MemoryContextSwitchTo(oldcontext);
	}

	if (rc & WL_POSTMASTER_DEATH)
		proc_exit(1);

	proc_exit(0);
}

/*
 * Module load callback
 */
void
_PG_init(void)
{
	if (!process_shared_preload_libraries_in_progress)
		return;

	/*
	 * Parameters that we expect to receive from the tserver process when it starts up postmaster.
	 * We set the flags GUC_NO_SHOW_ALL, GUC_NO_RESET_ALL, GUC_NOT_IN_SAMPLE, GUC_DISALLOW_IN_FILE
	 * so that these parameters aren’t visible, resettable, or configurable by our end user.
	 */
	DefineCustomStringVariable("yb_pg_metrics.node_name",
							   "Node name for YB metrics",
							   NULL,
							   &metric_node_name,
							   "",
							   PGC_POSTMASTER,
							   (GUC_NO_SHOW_ALL | GUC_NO_RESET_ALL |
								GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE),
							   NULL, NULL, NULL);

	DefineCustomIntVariable("yb_pg_metrics.port",
							"Port for YSQL webserver",
							NULL,
							&port,
							0, 0, INT_MAX,
							PGC_POSTMASTER,
							(GUC_NO_SHOW_ALL | GUC_NO_RESET_ALL |
							 GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE),
							NULL, NULL, NULL);

	DefineCustomBoolVariable("yb_pg_metrics.log_accesses",
							 "Log each request received by the YSQL webserver",
							 NULL,
							 &log_accesses,
							 false,
							 PGC_SUSET,
							 0,
							 NULL, NULL, NULL);

	DefineCustomBoolVariable("yb_pg_metrics.log_tcmalloc_stats",
							 "Log each request received by the YSQL webserver",
							 NULL,
							 &log_tcmalloc_stats,
							 false,
							 PGC_SUSET,
							 0,
							 NULL, NULL, NULL);

	DefineCustomIntVariable("yb_pg_metrics.webserver_profiler_sample_period_bytes",
							"The interval at which Google TCMalloc should "
							"sample allocations in the YSQL webserver. If this"
							" is 0, sampling is disabled. ",
							NULL,
							&webserver_profiler_sample_period_bytes,
							1024 * 1024, 0, INT_MAX,
							PGC_SUSET,
							0,
							NULL, NULL, NULL);

	DefineCustomIntVariable("yb_pg_metrics.db_catalog_cache_metrics_max_entries",
							"Maximum number of entries in the per-DB catalog "
							"cache metrics array.",
							NULL,
							&yb_db_catalog_cache_metrics_max_entries,
							YbPgmDbCatalogCacheMetricsDefaultEntries,
							YbPgmDbCatalogCacheMetricsMinEntries,
							YbPgmDbCatalogCacheMetricsMaxEntries,
							PGC_POSTMASTER,
							0,
							NULL, NULL, NULL);

	BackgroundWorker worker;

	if (!IsBinaryUpgrade)
	{
		/* Registering the YSQL webserver as a background worker */
		MemSet(&worker, 0, sizeof(BackgroundWorker));
		strcpy(worker.bgw_name, "YSQL webserver");
		worker.bgw_flags = BGWORKER_SHMEM_ACCESS;
		worker.bgw_start_time = BgWorkerStart_PostmasterStart;
		/* Value of 1 allows the background worker for webserver to restart */
		worker.bgw_restart_time = 1;
		worker.bgw_main_arg = (Datum) 0;
		strcpy(worker.bgw_library_name, "yb_pg_metrics");
		strcpy(worker.bgw_function_name, "webserver_worker_main");
		worker.bgw_notify_pid = 0;
		if (getenv("FLAGS_yb_webserver_oom_score_adj") != NULL)
			strncpy(worker.bgw_oom_score_adj,
					getenv("FLAGS_yb_webserver_oom_score_adj"),
					BGW_MAXLEN);

		RegisterBackgroundWorker(&worker);
	}

	/*
	 * Set the value of the hooks.
	 */

	prev_shmem_request_hook = shmem_request_hook;
	shmem_request_hook = ybpgm_shmem_request;

	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = ybpgm_startup_hook;

	prev_ExecutorStart = ExecutorStart_hook;
	ExecutorStart_hook = ybpgm_ExecutorStart;

	prev_ExecutorRun = ExecutorRun_hook;
	ExecutorRun_hook = ybpgm_ExecutorRun;

	prev_ExecutorFinish = ExecutorFinish_hook;
	ExecutorFinish_hook = ybpgm_ExecutorFinish;

	prev_ExecutorEnd = ExecutorEnd_hook;
	ExecutorEnd_hook = ybpgm_ExecutorEnd;

	prev_ProcessUtility = ProcessUtility_hook;
	ProcessUtility_hook = ybpgm_ProcessUtility;
}

/*
 * shmem_request hook: request additional shared resources.  We'll allocate or
 * attach to the shared resources in ybpgm_startup_hook().
 */
static void
ybpgm_shmem_request(void)
{
	if (prev_shmem_request_hook)
		prev_shmem_request_hook();

	RequestAddinShmemSpace(ybpgm_memsize());
	RequestAddinShmemSpace(YbpgmCatalogCacheMetricsShmemSize());
	RequestNamedLWLockTranche("yb_pg_metrics", 1);
}

static void
ybpgm_InitPostgres()
{

	/* Conn initialization latency metric */
	long		conn_init_secs;
	int			conn_init_usecs;

	TimestampDifference(MyStartTimestamp, GetCurrentTimestamp(),
						&conn_init_secs, &conn_init_usecs);
	uint64_t	conn_latency_us = (uint64_t) (conn_init_secs * 1000000 + conn_init_usecs);
	YbStatementType conn_type =
		(YbIsAuthBackend() || YbIsAuthPassthroughControlBackend()) ?
			AuthBackendInitLatency :
			RegularBackendInitLatency;

	ybpgm_Store(conn_type, conn_latency_us, 0);

	/* Authorized connections metric */
	long		current_authorized_connections = YbGetAuthorizedConnections();
	long		total_delta = current_authorized_connections - last_authorized_connection_val;

	last_authorized_connection_val = current_authorized_connections;
	ybpgm_StoreCount(AuthorizedConnection, 0, total_delta);

	/* Relcache preloads metric */
	long		current_relcache_preloads = YbGetRelCachePreloads();

	total_delta = current_relcache_preloads - last_relcache_preload_val;
	last_relcache_preload_val = current_relcache_preloads;
	ybpgm_StoreCount(RelCachePreload, 0, total_delta);

	/* Relcache init file revalidation metrics */
	long		current_revalidated = YbGetRelCacheInitFileRevalidated();

	total_delta = current_revalidated - last_relcache_init_file_revalidated_val;
	last_relcache_init_file_revalidated_val = current_revalidated;
	ybpgm_StoreCount(RelCacheInitFileRevalidated, 0, total_delta);

	long		current_revalidation_failed = YbGetRelCacheInitFileRevalidationFailed();

	total_delta = current_revalidation_failed - last_relcache_init_file_revalidation_failed_val;
	last_relcache_init_file_revalidation_failed_val = current_revalidation_failed;
	ybpgm_StoreCount(RelCacheInitFileRevalidationFailed, 0, total_delta);
}

/*
 * Allocate or attach to shared memory.
 */
static void
ybpgm_startup_hook(void)
{
	if (prev_shmem_startup_hook)
		prev_shmem_startup_hook();

	bool		found;

	ybpgm_table = ShmemInitStruct("yb_pg_metrics",
								  num_entries * sizeof(struct YbcPgmEntry),
								  &found);

	ybpgm_catalog_cache_metrics_array =
		ShmemInitStruct("yb_pg_catalog_cache_metrics_array",
						YbpgmCatalogCacheMetricsShmemSize(),
						&found);
	if (!found)
	{
		/*
		 * Initialize every slot's db_oid to InvalidOid so
		 * the find-or-claim scan can recognise empty slots
		 */
		for (int i = 0; i < yb_db_catalog_cache_metrics_max_entries; i++)
		{
			YbpgmCatalogCacheMetricsEntry *slot =
				&ybpgm_catalog_cache_metrics_array[i];

			pg_atomic_init_u32(&slot->db_oid, (uint32) InvalidOid);
			for (int c = 0; c < SysCacheSize; c++)
				pg_atomic_init_u64(&slot->cache_id_misses[c], 0);
			for (int t = 0; t < YbNumCatalogCacheTables; t++)
			{
				pg_atomic_init_u64(&slot->cache_table_misses[t], 0);
				pg_atomic_init_u64(&slot->cache_list_misses[t], 0);
				pg_atomic_init_u64(&slot->cache_neg_misses[t], 0);
			}
		}
	}

	set_metric_names();
	YBCSetUpdateInitPostgresMetricsFn(&ybpgm_InitPostgres);
}

static void
ybpgm_ExecutorStart(QueryDesc *queryDesc, int eflags)
{
	/*
	 * Each PORTAL execution will run the following steps.
	 * 1- ExecutorStart()
	 * 2- Execute statements in the portal. Some statement execution (CURSOR
	 *    execution) can open a nested PORTAL. Our metric routines will ignore
	 *    the nested PORTAL for now.
	 * 3- ExecutorEnd()
	 */
	if (prev_ExecutorStart)
		prev_ExecutorStart(queryDesc, eflags);
	else
		standard_ExecutorStart(queryDesc, eflags);

	/*
	 * PORTAL run can be nested inside another PORTAL, and we only run metric
	 * routines for the top level portal statement. The current design of
	 * using global variable "statement_nesting_level" is very flawed as it
	 * cannot find the starting and ending point of a top statement execution.
	 * For now, as a workaround, "queryDesc" attribute is used as an indicator
	 * for logging metric. Whenever "time value" is not null, it is logged at
	 * the end of a portal run.
	 * - When starting, we allocate "queryDesc->totaltime".
	 * - When ending, we check for "queryDesc->totaltime". If not null, its
	 *   metric is log.
	 */
	if (isTopLevelStatement() && !queryDesc->totaltime)
	{
		MemoryContext oldcxt;

		oldcxt = MemoryContextSwitchTo(queryDesc->estate->es_query_cxt);
		queryDesc->totaltime = InstrAlloc(1, INSTRUMENT_TIMER, false);
		MemoryContextSwitchTo(oldcxt);
	}
}

static void
ybpgm_ExecutorRun(QueryDesc *queryDesc, ScanDirection direction, uint64 count,
				  bool execute_once)
{
	IncStatementNestingLevel();
	PG_TRY();
	{
		if (prev_ExecutorRun)
			prev_ExecutorRun(queryDesc, direction, count, execute_once);
		else
			standard_ExecutorRun(queryDesc, direction, count, execute_once);
		DecStatementNestingLevel();
	}
	PG_CATCH();
	{
		DecStatementNestingLevel();
		PG_RE_THROW();
	}
	PG_END_TRY();
}

static void
ybpgm_ExecutorFinish(QueryDesc *queryDesc)
{
	IncStatementNestingLevel();
	PG_TRY();
	{
		if (prev_ExecutorFinish)
			prev_ExecutorFinish(queryDesc);
		else
			standard_ExecutorFinish(queryDesc);
		DecStatementNestingLevel();
	}
	PG_CATCH();
	{
		DecStatementNestingLevel();
		PG_RE_THROW();
	}
	PG_END_TRY();
}

static void
ybpgm_ExecutorEnd(QueryDesc *queryDesc)
{
	YbStatementType type;

	switch (queryDesc->operation)
	{
		case CMD_SELECT:
			type = Select;
			break;
		case CMD_INSERT:
			type = Insert;
			break;
		case CMD_DELETE:
			type = Delete;
			break;
		case CMD_UPDATE:
			type = Update;
			break;
		default:
			type = Other;
			break;
	}

	if (!yb_is_calling_internal_sql_for_ddl)
		is_statement_executed = true;

	/*
	 * Collecting metric.
	 * - Only processing metric for top level statement in top level portal.
	 *   For example, CURSOR execution can have many nested portal and nested
	 *   statement. The metric for all of the nested items are not processed.
	 * - However, it's difficult to know the starting and ending point of a
	 *   statement, we check for not null "queryDesc->totaltime".
	 * - The design for this metric module for using global state variables is
	 *   very flawed, so we use this not-null check for now.
	 */
	if (isTopLevelStatement() && queryDesc->totaltime)
	{
		InstrEndLoop(queryDesc->totaltime);
		const uint64_t time = (uint64_t) (queryDesc->totaltime->total * 1000000.0);
		const uint64 rows_count = queryDesc->estate->es_processed;

		ybpgm_Store(type, time, rows_count);

		if (queryDesc->estate->yb_es_is_single_row_modify_txn)
		{
			ybpgm_Store(Single_Shard_Transaction, time, rows_count);
			ybpgm_Store(SingleShardTransaction, time, rows_count);
		}

		if (!is_inside_transaction_block)
			ybpgm_Store(Transaction, time, rows_count);

		if (IsA(queryDesc->planstate, AggState) &&
			castNode(AggState, queryDesc->planstate)->yb_pushdown_supported)
			ybpgm_Store(AggregatePushdown, time, rows_count);

		long		current_count = YbGetCatCacheRefreshes();
		long		total_delta = current_count - last_catcache_refresh_val;

		last_catcache_refresh_val = current_count;
		/* Set the time parameter to 0 as we don't have metrics for that. */
		ybpgm_StoreCount(CatCacheRefresh, 0, total_delta);

		current_count = YbGetCatCacheDeltaRefreshes();
		total_delta = current_count - last_catcache_delta_refresh_val;
		last_catcache_delta_refresh_val = current_count;
		/* Set the time parameter to 0 as we don't have metrics for that. */
		ybpgm_StoreCount(CatCacheDeltaRefresh, 0, total_delta);

		long		current_cache_misses = YbGetCatCacheMisses();
		long	   *current_cache_id_misses = YbGetCatCacheIdMisses();
		long	   *current_cache_table_misses = YbGetCatCacheTableMisses();
		long	   *current_cache_list_misses = YbGetCatCacheListMisses();
		long	   *current_cache_neg_misses = YbGetCatCacheNegMisses();

		/*
		 * Per-cache-id / per-table breakdowns are recorded into the per-DB
		 * catalog cache metrics array. ybpgm_update_db_catalog_cache_metrics
		 * is also responsible for advancing last_cache_*_val so the deltas
		 * stay correct across calls.
		 */
		ybpgm_update_db_catalog_cache_metrics(current_cache_id_misses,
											  current_cache_table_misses,
											  current_cache_list_misses,
											  current_cache_neg_misses);

		total_delta = current_cache_misses - last_cache_misses_val;

		last_cache_misses_val = current_cache_misses;

		/*
		 * Currently we set the time parameter to 0 as we don't have metrics
		 * for that available TODO: Get timing metrics for catalog cache
		 * misses
		 */
		ybpgm_StoreCount(CatCacheMisses, 0, total_delta);

		/* Hint cache metrics */
		long		current_hint_cache_refreshes = YbGetHintCacheRefreshes();

		total_delta = current_hint_cache_refreshes - last_hint_cache_refreshes_val;
		last_hint_cache_refreshes_val = current_hint_cache_refreshes;
		ybpgm_StoreCount(HintCacheRefresh, 0, total_delta);

		long		current_hint_cache_hits = YbGetHintCacheHits();

		total_delta = current_hint_cache_hits - last_hint_cache_hits_val;
		last_hint_cache_hits_val = current_hint_cache_hits;
		ybpgm_StoreCount(HintCacheHits, 0, total_delta);

		long		current_hint_cache_misses = YbGetHintCacheMisses();

		total_delta = current_hint_cache_misses - last_hint_cache_misses_val;
		last_hint_cache_misses_val = current_hint_cache_misses;
		ybpgm_StoreCount(HintCacheMisses, 0, total_delta);
	}

	IncStatementNestingLevel();
	PG_TRY();
	{
		if (prev_ExecutorEnd)
			prev_ExecutorEnd(queryDesc);
		else
			standard_ExecutorEnd(queryDesc);
		DecStatementNestingLevel();
	}
	PG_CATCH();
	{
		DecStatementNestingLevel();
		PG_RE_THROW();
	}
	PG_END_TRY();
}

/*
 * Estimate shared memory space needed.
 */
static Size
ybpgm_memsize(void)
{
	Size		size;

	size = MAXALIGN(num_entries * sizeof(struct YbcPgmEntry));

	return size;
}

/*
 * Get the statement type for a transactional statement.
 */
static YbStatementType
ybpgm_getStatementType(TransactionStmt *stmt)
{
	YbStatementType type = Other;

	switch (stmt->kind)
	{
		case TRANS_STMT_BEGIN:
		case TRANS_STMT_START:
			type = Begin;
			break;
		case TRANS_STMT_COMMIT:
		case TRANS_STMT_COMMIT_PREPARED:
			type = Commit;
			break;
		case TRANS_STMT_ROLLBACK:
		case TRANS_STMT_ROLLBACK_TO:
		case TRANS_STMT_ROLLBACK_PREPARED:
			type = Rollback;
			break;
		case TRANS_STMT_SAVEPOINT:
		case TRANS_STMT_RELEASE:
		case TRANS_STMT_PREPARE:
			type = Other;
			break;
		default:
			elog(ERROR, "unrecognized statement kind: %d", stmt->kind);
	}
	return type;
}

/*
 * Hook used for tracking "Other" statements.
 */
static void
ybpgm_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
					 bool readOnlyTree, ProcessUtilityContext context,
					 ParamListInfo params, QueryEnvironment *queryEnv,
					 DestReceiver *dest, QueryCompletion *qc)
{
	if (isTopLevelBlock() &&
		!IsA(pstmt->utilityStmt, ExecuteStmt) &&
		!IsA(pstmt->utilityStmt, PrepareStmt) &&
		!IsA(pstmt->utilityStmt, DeallocateStmt))
	{
		instr_time	start;
		instr_time	end;
		YbStatementType type;

		if (IsA(pstmt->utilityStmt, TransactionStmt))
		{
			TransactionStmt *stmt = (TransactionStmt *) (pstmt->utilityStmt);

			type = ybpgm_getStatementType(stmt);
		}
		else
			type = Other;

		/*
		 * Compute DDL mode before executing the statement. A CALL/DO
		 * containing ROLLBACK will trigger AtAbort_Portals which frees
		 * the outermost portal's cached plan, making pstmt a dangling
		 * pointer by the time the hook's post-processing runs.
		 */
		bool		requires_autonomous_transaction = false;
		YbDdlModeOptional ddl_mode =
			YbGetDdlMode(pstmt, context, &requires_autonomous_transaction);

		INSTR_TIME_SET_CURRENT(start);

		IncBlockNestingLevel();
		PG_TRY();
		{
			if (prev_ProcessUtility)
				prev_ProcessUtility(pstmt, queryString, readOnlyTree, context,
									params, queryEnv, dest, qc);
			else
				standard_ProcessUtility(pstmt, queryString, readOnlyTree,
										context, params, queryEnv, dest, qc);
			DecBlockNestingLevel();
		}
		PG_CATCH();
		{
			DecBlockNestingLevel();
			PG_RE_THROW();
		}
		PG_END_TRY();

		INSTR_TIME_SET_CURRENT(end);
		INSTR_TIME_SUBTRACT(end, start);

		if (ddl_mode.has_value)
			ybpgm_Store(Transaction, INSTR_TIME_GET_MICROSEC(end), 0);
		else if (type == Other)
			is_statement_executed = true;

		if (type == Begin && !is_inside_transaction_block)
		{
			is_inside_transaction_block = true;
			is_statement_executed = false;
		}
		if (type == Rollback)
		{
			is_inside_transaction_block = false;
			is_statement_executed = false;
		}
		/*
		 * TODO: Once savepoint and rollback to specific transaction are supported,
		 * transaction block counter needs to be revisited.
		 * Current logic is to increment non-empty transaction block by 1
		 * if non-DDL statement types executed prior to committing.
		 */
		if (type == Commit)
		{
			if (qc->commandTag != CMDTAG_ROLLBACK &&
				is_inside_transaction_block &&
				is_statement_executed)
			{
				ybpgm_Store(Transaction, INSTR_TIME_GET_MICROSEC(end), 0);
			}
			is_inside_transaction_block = false;
			is_statement_executed = false;
		}

		ybpgm_Store(type, INSTR_TIME_GET_MICROSEC(end), 0 /* rows */ );
	}
	else
	{
		if (prev_ProcessUtility)
			prev_ProcessUtility(pstmt, queryString, readOnlyTree, context,
								params, queryEnv, dest, qc);
		else
			standard_ProcessUtility(pstmt, queryString, readOnlyTree, context,
									params, queryEnv, dest, qc);
	}
}

static void
ybpgm_Store(YbStatementType type, uint64_t time, uint64_t rows)
{
	if (yb_is_calling_internal_sql_for_ddl)
		return;

	struct YbcPgmEntry *entry = &ybpgm_table[type];

	entry->total_time += time;
	entry->calls += 1;
	entry->rows += rows;
}

static void
ybpgm_StoreCount(YbStatementType type, uint64_t time, uint64_t count)
{
	if (yb_is_calling_internal_sql_for_ddl)
		return;

	struct YbcPgmEntry *entry = &ybpgm_table[type];

	entry->total_time += time;
	entry->calls += count;
	entry->rows += count;
}

static Size
YbpgmCatalogCacheMetricsShmemSize(void)
{
	return MAXALIGN(mul_size(yb_db_catalog_cache_metrics_max_entries,
							 sizeof(YbpgmCatalogCacheMetricsEntry)));
}

/*
 * Linear scan to find the slot that either:
 * - already belongs to db_oid,
 * - claim the first empty slot, or
 * - the last slot(catchall slot)
 */

static YbpgmCatalogCacheMetricsEntry *
ybpgm_find_or_claim_db_slot(Oid db_oid)
{
	uint32		want = (uint32) db_oid;

	for (int i = 0; i < yb_db_catalog_cache_metrics_max_entries; i++)
	{
		YbpgmCatalogCacheMetricsEntry *slot =
			&ybpgm_catalog_cache_metrics_array[i];
		uint32		cur = pg_atomic_read_u32(&slot->db_oid);

		/*
		 * Return slot if either the slot is already claimed by the current db_oid or
		 * the last slot (catchall slot) is reached
		 */
		if (cur == want || i == yb_db_catalog_cache_metrics_max_entries - 1)
			return slot;

		/* If a slot with InvalidOid that is not the last slot is found, claim it to record the current DB's metrics */
		if (cur == (uint32) InvalidOid)
		{
			uint32		expected = (uint32) InvalidOid;

			if (pg_atomic_compare_exchange_u32(&slot->db_oid, &expected, want))
				return slot;

			/*
			 * Atomic swap failed, another thread claimed the slot. Use the slot if a
			 * concurrent claim was made by another backend on the same db. Otherwise keep scanning.
			 */
			if (expected == want)
				return slot;
		}
	}

	return NULL;
}

static void
ybpgm_update_db_catalog_cache_metrics(long *current_id_misses,
									  long *current_table_misses,
									  long *current_list_misses,
									  long *current_neg_misses)
{
	/*
	 * Decide whether to attribute deltas to a per-DB slot for this call similar to ybpgm_StoreCount
	 * for the global CatCacheMisses counter: skip recording for internal-DDL helper SQL and
	 * for backends that haven't bound a DB yet.
	 */
	YbpgmCatalogCacheMetricsEntry *entry = NULL;
	const bool	record_per_db = (!yb_is_calling_internal_sql_for_ddl &&
								 OidIsValid(MyDatabaseId) &&
								 ybpgm_catalog_cache_metrics_array != NULL);

	if (record_per_db)
		entry = ybpgm_find_or_claim_db_slot(MyDatabaseId);

	/*
	 * Always advance last_cache_*_val[] (even when no metrics are recorded to a per-DB slot).
	 * The underlying counters are per-process and tick regardless of DDL state, so leaving it
	 * stale would cause increments accumulated during DDL helper SQL to be charged to the
	 * next non-DDL statement that does record metrics.
	 */
	for (int c = 0; c < SysCacheSize; c++)
	{
		long		delta = current_id_misses[c] - last_cache_id_misses_val[c];

		if (entry != NULL && delta > 0)
			pg_atomic_fetch_add_u64(&entry->cache_id_misses[c],
									(int64) delta);
		last_cache_id_misses_val[c] = current_id_misses[c];
	}
	for (int t = 0; t < YbNumCatalogCacheTables; t++)
	{
		long		delta_table = (current_table_misses[t] -
								   last_cache_table_misses_val[t]);
		long		delta_list = (current_list_misses[t] -
								  last_cache_list_misses_val[t]);
		long		delta_neg = (current_neg_misses[t] -
								 last_cache_neg_misses_val[t]);

		if (entry != NULL)
		{
			if (delta_table > 0)
				pg_atomic_fetch_add_u64(&entry->cache_table_misses[t],
										(int64) delta_table);
			if (delta_list > 0)
				pg_atomic_fetch_add_u64(&entry->cache_list_misses[t],
										(int64) delta_list);
			if (delta_neg > 0)
				pg_atomic_fetch_add_u64(&entry->cache_neg_misses[t],
										(int64) delta_neg);
		}

		last_cache_table_misses_val[t] = current_table_misses[t];
		last_cache_list_misses_val[t] = current_list_misses[t];
		last_cache_neg_misses_val[t] = current_neg_misses[t];
	}
}

static void
ybpgm_iterate_db_catalog_cache_metrics(YbcDbCatalogCacheMetricVisitor visitor,
									   void *arg)
{
	if (ybpgm_catalog_cache_metrics_array == NULL || visitor == NULL)
		return;

	for (int i = 0; i < yb_db_catalog_cache_metrics_max_entries; i++)
	{
		YbpgmCatalogCacheMetricsEntry *slot =
			&ybpgm_catalog_cache_metrics_array[i];
		Oid			db_oid = (Oid) pg_atomic_read_u32(&slot->db_oid);

		if (!OidIsValid(db_oid) && i != yb_db_catalog_cache_metrics_max_entries - 1)
			continue;

		for (int c = 0; c < SysCacheSize; c++)
		{
			uint64_t	cnt = pg_atomic_read_u64(&slot->cache_id_misses[c]);

			if (cnt == 0)
				continue;
			visitor((unsigned int) db_oid,
					YSQL_METRIC_PREFIX "CatalogCacheMisses",
					YbGetCatalogCacheIndexName(c),
					cnt, arg);
		}

		for (int t = 0; t < YbNumCatalogCacheTables; t++)
		{
			const char *tname = YbGetCatalogCacheTableNameFromTableId(t);
			uint64_t	tm = pg_atomic_read_u64(&slot->cache_table_misses[t]);
			uint64_t	lm = pg_atomic_read_u64(&slot->cache_list_misses[t]);
			uint64_t	nm = pg_atomic_read_u64(&slot->cache_neg_misses[t]);

			if (tm > 0)
				visitor((unsigned int) db_oid,
						YSQL_METRIC_PREFIX "CatalogCacheTableMisses",
						tname, tm, arg);
			if (lm > 0)
				visitor((unsigned int) db_oid,
						YSQL_METRIC_PREFIX "CatalogCacheListMisses",
						tname, lm, arg);
			if (nm > 0)
				visitor((unsigned int) db_oid,
						YSQL_METRIC_PREFIX "CatalogCacheNegMisses",
						tname, nm, arg);
		}
	}
}
