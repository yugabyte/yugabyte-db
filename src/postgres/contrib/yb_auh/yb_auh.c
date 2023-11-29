// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

#include "postgres.h"
#include "postmaster/bgworker.h"
#include "postmaster/postmaster.h"

#include "parser/analyze.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/procarray.h"
#include "storage/proc.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "storage/shm_toc.h"
#include "utils/guc.h"
#include "utils/timestamp.h"
#include "fmgr.h"
#include "funcapi.h"

#include "miscadmin.h"
#include "storage/lwlock.h"
#include "storage/shm_mq.h"
#include "storage/shm_toc.h"
#include "storage/shmem.h"
#include "pgstat.h"
#include "pg_yb_utils.h"

#include "yb/yql/pggate/util/ybc_stat.h"
#include "yb/yql/pggate/ybc_pggate.h"

PG_MODULE_MAGIC;
PG_FUNCTION_INFO_V1(pg_active_universe_history);
PG_FUNCTION_INFO_V1(yb_tables);

#define PG_ACTIVE_UNIVERSE_HISTORY_COLS        12

typedef struct ybauhEntry {
  TimestampTz auh_sample_time;
  uint64_t top_level_request_id[2];
  uint64_t request_id;
  uint8_t wait_component;
  uint32_t wait_event;
  char wait_event_aux[16];
  uint64_t top_level_node_id[2];
  uint32 client_node_host;
  uint16 client_node_port;
  long query_id;
  TimestampTz start_ts_of_wait_event;
  float8 sample_rate;
} ybauhEntry;

typedef struct ybtableInfo {
  const char* table_id;
  const char* table_name;
  const char* table_type;
  const char* relation_type;
  const char* namespace_id;
  const char* namespace_name;
  const char* database_type;
  const char* pgschema_name;
  bool colocated;
  const char* parent_table_id;
} ybtableInfo;

/* counters */
typedef struct circularBufferIndex
{
  int index;
} circularBufferIndex;

/* Current nesting depth of ExecutorRun+ProcessUtility calls */
static int	nested_level = 0;

/* Saved hook values in case of unload */
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;
static post_parse_analyze_hook_type prev_post_parse_analyze_hook = NULL;
static ExecutorRun_hook_type prev_ExecutorRun = NULL;
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;
static ProcessUtility_hook_type prev_ProcessUtility = NULL;

ybauhEntry *AUHEntryArray = NULL;
LWLock *auh_entry_array_lock;

circularBufferIndex *CircularBufferIndexArray = NULL;
static int circular_buf_size = 0;
static int circular_buf_size_kb = 16*1024;

static int auh_sampling_interval_ms = 1000;
static int auh_sample_size = 5;
/* Entry point of library loading */
void _PG_init(void);
void yb_auh_main(Datum);
static Size yb_auh_memsize(void);
static Size yb_auh_circularBufferIndexSize(void);
static void ybauh_startup_hook(void);
static void ybauh_post_parse_analyze(ParseState *pstate, Query *query);
static void ybauh_ExecutorRun(QueryDesc *queryDesc,
				 ScanDirection direction,
				 uint64 count, bool execute_once);
static void ybauh_ExecutorEnd(QueryDesc *queryDesc);
static void ybauh_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
					ProcessUtilityContext context, ParamListInfo params,
					QueryEnvironment *queryEnv,
					DestReceiver *dest, char *completionTag);
static void pg_active_universe_history_internal(FunctionCallInfo fcinfo);
static void auh_entry_store(TimestampTz auh_time,
                            const uint64_t* top_level_request_id,
                            uint64_t request_id,
                            uint8 wait_component,
                            uint32 wait_event,
                            const char* wait_event_aux,
                            const uint64_t* top_level_node_id,
                            uint32 client_node_ip,
                            uint16 client_node_port,
                            long query_id,
                            TimestampTz start_ts_of_wait_event,
                            float8 sample_rate);
static void pg_collect_samples(TimestampTz auh_sample_time, uint16 num_procs_to_sample);
static void tserver_collect_samples(TimestampTz auh_sample_time, uint16 num_rpcs_to_sample);

static volatile sig_atomic_t got_sigterm = false;
static volatile sig_atomic_t got_sighup = false;

void
_PG_init(void)
{
  BackgroundWorker worker;

  if (!process_shared_preload_libraries_in_progress)
    return;
  DefineCustomIntVariable("yb_auh.circular_buf_size_kb", "Size of circular buffer in KBs",
                          "Default value is 16 MB",
                          &circular_buf_size_kb, 16*1024, 0, INT_MAX, PGC_POSTMASTER,
                          GUC_NO_SHOW_ALL | GUC_NO_RESET_ALL | GUC_NOT_IN_SAMPLE
                              | GUC_DISALLOW_IN_FILE,
                          NULL, NULL, NULL);
  DefineCustomIntVariable("yb_auh.sampling_interval_ms", "Duration (in milliseconds) between each pull.",
                          "Default value is 1000 millisecond", &auh_sampling_interval_ms,
                          1000, 1, INT_MAX, PGC_SIGHUP,
                          GUC_NO_SHOW_ALL | GUC_NO_RESET_ALL | GUC_NOT_IN_SAMPLE
                              | GUC_DISALLOW_IN_FILE,
                          NULL, NULL, NULL);

  DefineCustomIntVariable("yb_auh.sample_size", "Sample size of threads to be added to the buffer",
                          NULL, &auh_sample_size,
                          50, 0, INT_MAX, PGC_SIGHUP,
                          0, NULL, NULL, NULL);

  RequestAddinShmemSpace(yb_auh_memsize());
  RequestNamedLWLockTranche("auh_entry_array", 1);
  RequestAddinShmemSpace(yb_auh_circularBufferIndexSize());
  RequestNamedLWLockTranche("auh_circular_buffer_array", 1);

  memset(&worker, 0, sizeof(worker));
  sprintf(worker.bgw_name, "AUH controller");
  worker.bgw_flags = BGWORKER_SHMEM_ACCESS;
  worker.bgw_start_time = BgWorkerStart_PostmasterStart;
  /* Value of 1 allows the background worker for webserver to restart */
  worker.bgw_restart_time = 1;
  worker.bgw_main_arg = (Datum) 0;
  sprintf(worker.bgw_library_name, "yb_auh");
  sprintf(worker.bgw_function_name, "yb_auh_main");
  worker.bgw_notify_pid = 0;
  RegisterBackgroundWorker(&worker);

  /*
   * Install hooks.
   */
  prev_shmem_startup_hook = shmem_startup_hook;
  shmem_startup_hook = ybauh_startup_hook;
  prev_post_parse_analyze_hook = post_parse_analyze_hook;
  post_parse_analyze_hook = ybauh_post_parse_analyze;
  prev_ExecutorRun = ExecutorRun_hook;
  ExecutorRun_hook = ybauh_ExecutorRun;
  prev_ExecutorEnd = ExecutorEnd_hook;
  ExecutorEnd_hook = ybauh_ExecutorEnd;
  prev_ProcessUtility = ProcessUtility_hook;
  ProcessUtility_hook = ybauh_ProcessUtility;
}

/*
 * Module unload callback
 */
void
_PG_fini(void)
{
  /* Uninstall hooks. */
  shmem_startup_hook = prev_shmem_startup_hook;
  post_parse_analyze_hook = prev_post_parse_analyze_hook;
  ExecutorRun_hook = prev_ExecutorRun;
  ExecutorEnd_hook = prev_ExecutorEnd;
  ProcessUtility_hook = prev_ProcessUtility;
}

static void
yb_auh_sigterm(SIGNAL_ARGS)
{
  int save_errno = errno;
  got_sigterm = true;
  SetLatch(MyLatch);
  errno = save_errno;
}

static void
yb_auh_sighup(SIGNAL_ARGS)
{
  int save_errno = errno;
  got_sighup = true;
  SetLatch(MyLatch);
  errno = save_errno;
}

void
yb_auh_main(Datum main_arg) {
  // TODO:
  MyAuxProcType = YbAUHProcess;
  YBInitPostgresBackend("postgres", "", "hemant");

  ereport(LOG, (errmsg("starting bgworker yb_auh with buffer size %d", circular_buf_size)));

  /* Register functions for SIGTERM/SIGHUP management */
  pqsignal(SIGHUP, yb_auh_sighup);
  pqsignal(SIGTERM, yb_auh_sigterm);

  /* We're now ready to receive signals */
  BackgroundWorkerUnblockSignals();

  pgstat_report_appname("yb_auh collector");

  while (!got_sigterm) {
    int rc;
    TimestampTz auh_sample_time;
    MemoryContext uppercxt;
    /* Wait necessary amount of time */
    rc = WaitLatch(MyLatch, WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
                   auh_sampling_interval_ms, PG_WAIT_EXTENSION);
    ResetLatch(MyLatch);
    /* bailout if postmaster has died */
    if (rc & WL_POSTMASTER_DEATH)
      proc_exit(1);

    /* Process signals */
    if (got_sighup) {
      /* Process config file */
      got_sighup = false;
      ProcessConfigFile(PGC_SIGHUP);
      ereport(LOG, (errmsg("bgworker pg_auh signal: processed SIGHUP")));
    }

    if (got_sigterm) {
      /* Simply exit */
      ereport(LOG, (errmsg("bgworker pg_auh signal: processed SIGTERM")));
      proc_exit(0);
    }

    uppercxt = CurrentMemoryContext;

    auh_sample_time = GetCurrentTimestamp();

    MemoryContext oldcxt = MemoryContextSwitchTo(uppercxt);

    pg_collect_samples(auh_sample_time, auh_sample_size);
    tserver_collect_samples(auh_sample_time, auh_sample_size);

    MemoryContextSwitchTo(oldcxt);
    /* No problems, so clean exit */
  }
  proc_exit(0);
}

uint8 kPgAshComponent = 1;

static void pg_collect_samples(TimestampTz auh_sample_time, uint16 num_procs_to_sample)
{
  size_t procCount = 0;
  PgProcAuhNode *nodes_head = pg_collect_samples_proc(&procCount);
  PgProcAuhNode *current = nodes_head;
  float8 sample_rate = 0;
  if (nodes_head != NULL && procCount != 0)
  {
    sample_rate = (float)Min(num_procs_to_sample, procCount) / procCount;
  }
  while (current != NULL)
  {
    PGProcAUHEntryList proc = current->data;
    if (random() < sample_rate * MAX_RANDOM_VALUE)
    {
      auh_entry_store(auh_sample_time, proc.top_level_request_id, 0, kPgAshComponent,
                      proc.wait_event_info, "", proc.top_level_node_id,
                      proc.client_node_host, proc.client_node_port,
                      proc.queryid, auh_sample_time, sample_rate);
    }
    current = current->next;
  }
  freeLinkedList(nodes_head);
}

static void tserver_collect_samples(TimestampTz auh_sample_time, uint16 num_rpcs_to_sample)
{
  //TODO:
  YBCAUHDescriptor *rpcs = NULL;
  size_t numrpcs = 0;
  HandleYBStatus(YBCActiveUniverseHistory(&rpcs, &numrpcs));
  float8 sample_rate= 0;
  if(numrpcs != 0)
    sample_rate = (float)Min(num_rpcs_to_sample, numrpcs)/numrpcs;
  for (int i = 0; i < numrpcs; i++) {
    if(random() <= sample_rate * MAX_RANDOM_VALUE){
      auh_entry_store(auh_sample_time, rpcs[i].metadata.top_level_request_id,
                    rpcs[i].metadata.current_request_id, rpcs[i].metadata.component, rpcs[i].wait_status_code,
                    (rpcs[i].aux_info.tablet_id[0] == '\0' ? rpcs[i].aux_info.table_id : rpcs[i].aux_info.tablet_id),
                    rpcs[i].metadata.top_level_node_id,
                    rpcs[i].metadata.client_node_host, rpcs[i].metadata.client_node_port,
                    rpcs[i].metadata.query_id, auh_sample_time, sample_rate);
    }
  }
}

static Size
yb_auh_memsize(void)
{
  Size		size;
  circular_buf_size = (circular_buf_size_kb * 1024) / sizeof(struct ybauhEntry);
  size = MAXALIGN(sizeof(struct ybauhEntry) * circular_buf_size);

  return size;
}

static Size
yb_auh_circularBufferIndexSize(void)
{
  Size            size;
  /* CircularBufferIndexArray */
  size = MAXALIGN(sizeof(struct circularBufferIndex) * 1);
  return size;
}

static void auh_entry_store(TimestampTz auh_time,
                            const uint64_t* top_level_request_id,
                            uint64_t request_id,
                            uint8_t wait_component,
                            uint32 wait_event,
                            const char* wait_event_aux,
                            const uint64_t* top_level_node_id,
                            uint32 client_node_host,
                            uint16 client_node_port,
                            long query_id,
                            TimestampTz start_ts_of_wait_event,
                            float8 sample_rate)
{
  LWLockAcquire(auh_entry_array_lock, LW_EXCLUSIVE);
  int inserted;
  if (!AUHEntryArray) { return; }

  CircularBufferIndexArray[0].index = (CircularBufferIndexArray[0].index % circular_buf_size) + 1;
  inserted = CircularBufferIndexArray[0].index - 1;

  AUHEntryArray[inserted].auh_sample_time = auh_time;
  AUHEntryArray[inserted].wait_component = wait_component;
  AUHEntryArray[inserted].wait_event = wait_event;
  AUHEntryArray[inserted].request_id = request_id;

  if (top_level_request_id)
  {
    AUHEntryArray[inserted].top_level_request_id[0] = top_level_request_id[0];
    AUHEntryArray[inserted].top_level_request_id[1] = top_level_request_id[1];
  }
  else
  {
    AUHEntryArray[inserted].top_level_request_id[0] = 0;
    AUHEntryArray[inserted].top_level_request_id[1] = 0;
  }

  int len = Min(strlen(wait_event_aux) + 1, 15);
  memcpy(AUHEntryArray[inserted].wait_event_aux, wait_event_aux, len);
  AUHEntryArray[inserted].wait_event_aux[len] = '\0';

  if (top_level_node_id)
  {
    AUHEntryArray[inserted].top_level_node_id[0] = top_level_node_id[0];
    AUHEntryArray[inserted].top_level_node_id[1] = top_level_node_id[1];
  }
  else
  {
    AUHEntryArray[inserted].top_level_node_id[0] = 0;
    AUHEntryArray[inserted].top_level_node_id[1] = 0;
  }

  AUHEntryArray[inserted].client_node_host = client_node_host;
  AUHEntryArray[inserted].client_node_port = client_node_port;
  AUHEntryArray[inserted].query_id = query_id;
  AUHEntryArray[inserted].start_ts_of_wait_event = start_ts_of_wait_event;
  AUHEntryArray[inserted].sample_rate = sample_rate;
  LWLockRelease(auh_entry_array_lock);
}

static void
ybauh_startup_hook(void)
{
  if (prev_shmem_startup_hook)
    prev_shmem_startup_hook();

  bool found;
  AUHEntryArray = ShmemInitStruct("auh_entry_array",
                                   sizeof(struct ybauhEntry) * circular_buf_size,
                                   &found);

  CircularBufferIndexArray = ShmemInitStruct("auh_circular_buffer_array",
                                       sizeof(struct circularBufferIndex) * 1,
                                       &found);
  auh_entry_array_lock = &(GetNamedLWLockTranche("auh_entry_array"))->lock;
}

static void
ybauh_post_parse_analyze(ParseState *pstate, Query *query)
{
	if (prev_post_parse_analyze_hook)
		prev_post_parse_analyze_hook(pstate, query);

  MyProc->queryid = query->queryId;
	YBCSetQueryId(query->queryId);
}

static void
ybauh_ExecutorRun(QueryDesc *queryDesc, ScanDirection direction, uint64 count,
				 bool execute_once)
{
  uint64 queryId = queryDesc->plannedstmt->queryId;
  MyProc->queryid = queryId;
  YBCSetQueryId(queryId);
  nested_level++;
	PG_TRY();
	{
		if (prev_ExecutorRun)
			prev_ExecutorRun(queryDesc, direction, count, execute_once);
		else
			standard_ExecutorRun(queryDesc, direction, count, execute_once);
    nested_level--;
	}
	PG_CATCH();
	{
    nested_level--;
		PG_RE_THROW();
	}
	PG_END_TRY();
}

static void
ybauh_ExecutorEnd(QueryDesc *queryDesc)
{
  // reset per query AUH parameters
  MyProc->queryid = 0;
  YBCSetQueryId(0);
  MyProc->top_level_request_id[0] = '\0';
  MyProc->wait_event_info = 0;

	if (prev_ExecutorEnd)
		prev_ExecutorEnd(queryDesc);
	else
		standard_ExecutorEnd(queryDesc);
}

/*
 * ProcessUtility hook
 */
static void
ybauh_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
					ProcessUtilityContext context,
					ParamListInfo params, QueryEnvironment *queryEnv,
					DestReceiver *dest, char *completionTag)
{
  uint64		queryId = pstmt->queryId;
  MyProc->queryid = queryId;
  YBCSetQueryId(queryId);
  nested_level++;
  PG_TRY();
  {
    if (prev_ProcessUtility)
      prev_ProcessUtility(pstmt, queryString,
                context, params, queryEnv,
                dest, completionTag);
    else
      standard_ProcessUtility(pstmt, queryString,
                  context, params, queryEnv,
                  dest, completionTag);
    nested_level--;
  }
  PG_CATCH();
  {
    nested_level--;
    PG_RE_THROW();
  }
  PG_END_TRY();
}

static void 
yb_tables_internal(FunctionCallInfo fcinfo)
{
  ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
  TupleDesc       tupdesc;
  Tuplestorestate *tupstore;
  MemoryContext per_query_ctx;
  MemoryContext oldcontext;

  if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
    ereport(ERROR,
            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                errmsg("set-valued function called in context that cannot accept a set")));
  if (!(rsinfo->allowedModes & SFRM_Materialize))
    ereport(ERROR,
            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                errmsg("materialize mode required, but it is not " \
					   "allowed in this context")));

  if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
    elog(ERROR, "return type must be a row type");

  per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
  oldcontext = MemoryContextSwitchTo(per_query_ctx); 

  tupstore = tuplestore_begin_heap(true, false, work_mem);
  rsinfo->returnMode = SFRM_Materialize;
  rsinfo->setResult = tupstore;
  rsinfo->setDesc = tupdesc;

  MemoryContextSwitchTo(oldcontext);

  YBCTableIDMetadataInfo *infolist = NULL;
	size_t size = 0;
  
  HandleYBStatus(YBCTableIDMetadata(&infolist, &size));
  
  int i;
  for (i = 0; i < size; i++) {

    Datum values[10];
    bool isnull[10];
    int j = 0;

    memset(values, 0, sizeof(values));
    memset(isnull, 0, sizeof(isnull));

    // table_id
    if (infolist[i].table_id != NULL)
        values[j] = CStringGetTextDatum(infolist[i].table_id);
    else
        isnull[j] = true;
    j++;

    // table_name
    if (infolist[i].table_name != NULL)
        values[j] = CStringGetTextDatum(infolist[i].table_name);
    else
        isnull[j] = true;
    j++;

    // table_type
    if (infolist[i].table_type != NULL)
        values[j] = CStringGetTextDatum(infolist[i].table_type);
    else
        isnull[j] = true;
    j++;

    // relation_type
    if (infolist[i].relation_type != NULL)
        values[j] = CStringGetTextDatum(infolist[i].relation_type);
    else
        isnull[j] = true;
    j++;

    // namespace_id
    if (infolist[i].namespace_.id != NULL)
        values[j] = CStringGetTextDatum(infolist[i].namespace_.id);
    else
        isnull[j] = true;
    j++;

    // namespace_name
    if (infolist[i].namespace_.name != NULL)
        values[j] = CStringGetTextDatum(infolist[i].namespace_.name);
    else
        isnull[j] = true;
    j++;

    //database_type 
    if (infolist[i].namespace_.database_type != NULL)
        values[j] = CStringGetTextDatum(infolist[i].namespace_.database_type);
    else
        isnull[j] = true;
    j++;

    // pgschema_name
    if (infolist[i].pgschema_name != NULL)
        values[j] = CStringGetTextDatum(infolist[i].pgschema_name);
    else
        isnull[j] = true;
    j++;

    // colocated
    values[j++] = BoolGetDatum(infolist[i].colocated_info.colocated);

    // parent_table_id
    if (infolist[i].colocated_info.parent_table_id != NULL)
        values[j] = CStringGetTextDatum(infolist[i].colocated_info.parent_table_id);
    else
        isnull[j] = true;

    tuplestore_putvalues(tupstore, tupdesc, values, isnull);
  }

  tuplestore_donestoring(tupstore);
}

Datum
yb_tables(PG_FUNCTION_ARGS)
{
	yb_tables_internal(fcinfo);
  return (Datum) 0;
}

static void
pg_active_universe_history_internal(FunctionCallInfo fcinfo)
{
  ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
  TupleDesc       tupdesc;
  Tuplestorestate *tupstore;
  MemoryContext per_query_ctx;
  MemoryContext oldcontext;
  int i;

  /* Entry array must exist already */
  if (!AUHEntryArray)
    ereport(ERROR,
            (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                errmsg("pg_active_universe_history must be loaded via shared_preload_libraries")));

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
    elog(ERROR, "return type must be a row type");

  tupstore = tuplestore_begin_heap(true, false, work_mem);
  rsinfo->returnMode = SFRM_Materialize;
  rsinfo->setResult = tupstore;
  rsinfo->setDesc = tupdesc;

  MemoryContextSwitchTo(oldcontext);
  LWLockAcquire(auh_entry_array_lock, LW_SHARED);

  for (i = 0; i < circular_buf_size; i++)
  {
    Datum           values[PG_ACTIVE_UNIVERSE_HISTORY_COLS];
    bool            nulls[PG_ACTIVE_UNIVERSE_HISTORY_COLS];
    int                     j = 0;
    const char *event_type, *event, *event_component;

    memset(values, 0, sizeof(values));
    memset(nulls, 0, sizeof(nulls));

    // auh_time
    if (TimestampTzGetDatum(AUHEntryArray[i].auh_sample_time))
      values[j++] = TimestampTzGetDatum(AUHEntryArray[i].auh_sample_time);
    else
      break;

    char top_level_request_id[33];
    uint128_to_char(AUHEntryArray[i].top_level_request_id, top_level_request_id);
    // top level request id
    if (AUHEntryArray[i].top_level_request_id[0] != '\0')
      values[j++] = CStringGetTextDatum(top_level_request_id);
    else
      nulls[j++] = true;

    // request id
    if (AUHEntryArray[i].request_id)
      values[j++] = Int64GetDatum(AUHEntryArray[i].request_id);
    else
      nulls[j++] = true;

    // Wait event, wait event component, wait event class
    event_type = pgstat_get_wait_event_type(AUHEntryArray[i].wait_event);
    event = pgstat_get_wait_event(AUHEntryArray[i].wait_event);
    event_component = ybcstat_get_wait_event_component(AUHEntryArray[i].wait_event);

    if (event_component)
      values[j++] = CStringGetTextDatum(event_component);
    else
      nulls[j++] = true;

    if (event_type)
      values[j++] = CStringGetTextDatum(event_type);
    else
      nulls[j++] = true;

    if (event)
      values[j++] = CStringGetTextDatum(event);
    else
      nulls[j++] = true;

    // wait event's auxillary info
    if (AUHEntryArray[i].wait_event_aux[0] != '\0')
      values[j++] = CStringGetTextDatum(AUHEntryArray[i].wait_event_aux);
    else
      nulls[j++] = true;

    char top_level_node_id[33];
    uint128_to_char(AUHEntryArray[i].top_level_node_id, top_level_node_id);
    // top level node id
    if (AUHEntryArray[i].top_level_node_id[0] != '\0')
      values[j++] = CStringGetTextDatum(top_level_node_id);
    else
      nulls[j++] = true;

    // query id
    if (AUHEntryArray[i].query_id)
      values[j++] = Int64GetDatum(AUHEntryArray[i].query_id);
    else
      nulls[j++] = true;

    // Originating client node
    if (AUHEntryArray[i].client_node_host != 0 && AUHEntryArray[i].client_node_port != 0)
    {
      char client_node_ip[22];
      client_node_ip_to_string(AUHEntryArray[i].client_node_host, AUHEntryArray[i].client_node_port, client_node_ip);
      values[j++] = CStringGetTextDatum(client_node_ip);
    }
    else
      nulls[j++] = true;
    // start timestamp of wait event
    if (TimestampTzGetDatum(AUHEntryArray[i].start_ts_of_wait_event))
      values[j++] = TimestampTzGetDatum(AUHEntryArray[i].start_ts_of_wait_event);
    else
      break;

    // Sample rate
    // TODO: sample rate is throwing an error in certain mac environments
    // Disabling it for now.
    if (AUHEntryArray[i].sample_rate)
      values[j++] = Float8GetDatum(AUHEntryArray[i].sample_rate);
    else
      nulls[j++] = true;

    tuplestore_putvalues(tupstore, tupdesc, values, nulls);
  }
  /* clean up and return the tuplestore */
  tuplestore_donestoring(tupstore);
  LWLockRelease(auh_entry_array_lock);

}

Datum
pg_active_universe_history(PG_FUNCTION_ARGS)
{
  pg_active_universe_history_internal(fcinfo);
  return (Datum) 0;
}
