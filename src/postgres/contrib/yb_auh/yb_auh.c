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

#include "storage/ipc.h"
#include "storage/latch.h"
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

PG_MODULE_MAGIC;
PG_FUNCTION_INFO_V1(pg_active_universe_history);

#define PG_ACTIVE_UNIVERSE_HISTORY_COLS        3

typedef struct ybauhEntry {
  TimestampTz auh_sample_time;
  char top_level_request_id[16];
  char wait_event[8];
} ybauhEntry;

/* counters */
typedef struct circularBufferIndex
{
  int index;
} circularBufferIndex;

ybauhEntry *AUHEntryArray = NULL;
circularBufferIndex *CircularBufferIndexArray = NULL;
static int circular_buf_size = 1000;
static int auh_sampling_interval = 1;

/* Entry point of library loading */
void _PG_init(void);
void yb_auh_main(Datum);
static Size yb_auh_memsize(void);
static Size yb_auh_circularBufferIndexSize(void);
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;
static void ybauh_startup_hook(void);
static void pg_active_universe_history_internal(FunctionCallInfo fcinfo);


static void auh_entry_store(TimestampTz auh_time,
                              const char* top_level_request_id,
                              const char *wait_event);

static volatile sig_atomic_t got_sigterm = false;
static volatile sig_atomic_t got_sighup = false;

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

  ereport(LOG, (errmsg("starting bgworker yb_auh")));

  /* Register functions for SIGTERM/SIGHUP management */
  pqsignal(SIGHUP, yb_auh_sighup);
  pqsignal(SIGTERM, yb_auh_sigterm);

  /* We're now ready to receive signals */
  BackgroundWorkerUnblockSignals();

  while (!got_sigterm) {
    int rc;
    TimestampTz auh_sample_time;
    MemoryContext uppercxt;

    /* Wait necessary amount of time */
    rc = WaitLatch(MyLatch, WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
                   auh_sampling_interval * 1000L, PG_WAIT_EXTENSION);
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

    auh_entry_store(auh_sample_time, "123", "WAITING");

    MemoryContextSwitchTo(oldcxt);
    /* No problems, so clean exit */
  }
  proc_exit(0);
}

void
_PG_init(void)
{
  BackgroundWorker worker;

  if (!process_shared_preload_libraries_in_progress)
    return;
  // TODO: add these in pgwrapper.c
  DefineCustomIntVariable("yb_auh.circular_buf_size", "Size of circular buffer",
                          "Default value is 1000",
                          &circular_buf_size, 1000, 0, INT_MAX, PGC_POSTMASTER,
                          GUC_NO_SHOW_ALL | GUC_NO_RESET_ALL | GUC_NOT_IN_SAMPLE
                              | GUC_DISALLOW_IN_FILE,
                          NULL, NULL, NULL);
  DefineCustomIntVariable("yb_auh.sampling_interval", "Duration (in seconds) between each pull.",
                          "Default value is 1 second", &auh_sampling_interval,
                          1, 1, INT_MAX, PGC_SIGHUP,
                          GUC_NO_SHOW_ALL | GUC_NO_RESET_ALL | GUC_NOT_IN_SAMPLE
                              | GUC_DISALLOW_IN_FILE,
                          NULL, NULL, NULL);
  ereport(LOG, (errmsg("yb_auh collector started 2")));

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
  prev_shmem_startup_hook = shmem_startup_hook;
  shmem_startup_hook = ybauh_startup_hook;
}

static Size
yb_auh_memsize(void)
{
  Size		size;

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
                            const char* top_level_request_id,
                            const char *wait_event)
{
  int inserted;
  if (!AUHEntryArray) { return; }

  CircularBufferIndexArray[0].index = (CircularBufferIndexArray[0].index % circular_buf_size) + 1;
  inserted = CircularBufferIndexArray[0].index - 1;

  AUHEntryArray[inserted].auh_sample_time = auh_time;
  memcpy(AUHEntryArray[inserted].wait_event, wait_event, Min(strlen(wait_event) + 1, 8));
  memcpy(AUHEntryArray[inserted].top_level_request_id, top_level_request_id,
         Min(strlen(top_level_request_id) + 1, 15));
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

  for (i = 0; i < circular_buf_size; i++)
  {
    Datum           values[PG_ACTIVE_UNIVERSE_HISTORY_COLS];
    bool            nulls[PG_ACTIVE_UNIVERSE_HISTORY_COLS];
    int                     j = 0;

    memset(values, 0, sizeof(values));
    memset(nulls, 0, sizeof(nulls));

    // auh_time
    if (TimestampTzGetDatum(AUHEntryArray[i].auh_sample_time))
      values[j++] = TimestampTzGetDatum(AUHEntryArray[i].auh_sample_time);
    else
      break;

    // top level request id
    if (AUHEntryArray[i].top_level_request_id[0] != '\0')
      values[j++] = CStringGetTextDatum(AUHEntryArray[i].top_level_request_id);
    else
      nulls[j++] = true;

    // wait event
    if (AUHEntryArray[i].wait_event[0] != '\0')
      values[j++] = CStringGetTextDatum(AUHEntryArray[i].wait_event);
    else
      nulls[j++] = true;

    tuplestore_putvalues(tupstore, tupdesc, values, nulls);
  }

  /* clean up and return the tuplestore */
  tuplestore_donestoring(tupstore);
}

Datum
pg_active_universe_history(PG_FUNCTION_ARGS)
{
  pg_active_universe_history_internal(fcinfo);
  return (Datum) 0;
}
