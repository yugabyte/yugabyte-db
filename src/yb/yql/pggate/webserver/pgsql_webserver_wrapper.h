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

#pragma once

#ifdef __cplusplus
#include <atomic>
using int64 = int64_t;
using uint8 = uint8_t;
using uint64 = uint64_t;
#else
#include <stdatomic.h>
#endif

#include "yb/yql/pggate/util/ybc_util.h"

#ifdef __cplusplus
namespace yb::pggate {
extern "C" {
#define YB_ATOMIC_ULLONG std::atomic_ullong
#else
#define YB_ATOMIC_ULLONG atomic_ullong
#endif

struct WebserverWrapper;

#define YB_PG_METRIC_NAME_LEN 120

#define YB_PG_METRIC_NAME_LEN 120

typedef struct ybpgmEntry {
  char name[YB_PG_METRIC_NAME_LEN];
  char table_name[YB_PG_METRIC_NAME_LEN];
  char count_help[YB_PG_METRIC_NAME_LEN];
  char sum_help[YB_PG_METRIC_NAME_LEN];
  YB_ATOMIC_ULLONG calls;
  YB_ATOMIC_ULLONG total_time;
  YB_ATOMIC_ULLONG rows;
} ybpgmEntry;

typedef struct rpczEntry {
  char *query;
  char *application_name;
  int proc_id;
  unsigned int db_oid;
  char *db_name;
  int64 process_start_timestamp;
  int64 transaction_start_timestamp;
  int64 query_start_timestamp;
  char *backend_type;
  uint8 backend_active;
  char *backend_status;
  char *host;
  char *port;
} rpczEntry;

typedef struct YsqlStatementStat {
  char *query;

  // Prefix of Counters in pg_stat_monitor.c

  int64 userid;       /* OID of user who executed the statement */
  int64 dbid;         /* OID of database in which the statement was executed */
  int64 calls;         /* # of times executed */
  double total_time;   /* total execution time, in msec */
  double min_time;     /* minimum execution time in msec */
  double max_time;     /* maximum execution time in msec */
  double mean_time;    /* mean execution time in msec */
  double sum_var_time; /* sum of variances in execution time in msec */
  int64 rows;          /* total # of retrieved or affected rows */
  int64 query_id;     /* query id of the pgssHashKey for the query */
  int64 local_blks_hit; /* Number of local buffer hits */
  int64 local_blks_read; /* Number of local disk blocks read */
  int64 local_blks_dirtied; /* Number of local disk blocks dirtied */
  int64 local_blks_written; /* Number of local disk blocks written */
  int64 temp_blks_read; /* Number of temp blocks read */
  int64 temp_blks_written; /* Number of temp blocks written */
} YsqlStatementStat;

typedef struct {
  void (*pullRpczEntries)();
  void (*freeRpczEntries)();
  int64 (*getTimestampTz)();
  int64 (*getTimestampTzDiffMs)(int64, int64);
  const char *(*getTimestampTzToStr)(int64);
} postgresCallbacks;

typedef struct {
  /* # of connections rejected due to the connection limit. */
  int *too_many_conn;

  /* maximum # of concurrent sql connections allowed. */
  int *max_conn;

  /* # of connections established since start of postmaster. */
  uint64_t *new_conn;
} YbConnectionMetrics;

typedef struct {
  const char* name;
  uint64_t value;
  const char* type;
  const char* help;
} YsqlConnMgrMetric;

struct WebserverWrapper *CreateWebserver(char *listen_addresses, int port);
void DestroyWebserver(struct WebserverWrapper *webserver);
void RegisterMetrics(ybpgmEntry *tab, int num_entries, char *metric_node_name);
void RegisterRpczEntries(
    postgresCallbacks *callbacks, int *num_backends_ptr, rpczEntry **rpczEntriesPointer,
    YbConnectionMetrics *conn_metrics_ptr);
YBCStatus StartWebserver(struct WebserverWrapper *webserver);
void SetWebserverConfig(
    struct WebserverWrapper *webserver,
    bool enable_access_logging, bool enable_tcmalloc_logging,
    int webserver_profiler_sample_freq_bytes);
void RegisterGetYsqlStatStatements(void (*getYsqlStatementStats)(void *));
void RegisterResetYsqlStatStatements(void (*fn)());
void WriteStatArrayElemToJson(void *p1, void *p2);
void WriteStartObjectToJson(void *p1); /* Takes void *cb_arg argument */
void WriteHistArrayBeginToJson(void *p1); /* Takes void *cb_arg argument */
/* Takes void *cb_arg, char *buf, int64_t *count arguments */
void WriteHistElemToJson(void *p1, void *p2, void *p3);
void WriteHistArrayEndToJson(void* p1); /* Takes void *cb_arg argument */
void WriteEndObjectToJson(void *p1); /* Takes void *cb_arg argument */

#ifdef __cplusplus
}  // extern "C"
}  // namespace yb::pggate
#endif
