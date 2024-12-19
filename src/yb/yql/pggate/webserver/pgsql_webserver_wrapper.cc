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

#include "yb/yql/pggate/webserver/pgsql_webserver_wrapper.h"

#include <sys/ipc.h>
#include <sys/shm.h>
#include <math.h>
#include <cassert>

#include <map>
#include <vector>
#include <string>

#include "yb/gutil/map-util.h"

#include "yb/server/default-path-handlers.h"
#include "yb/server/pprof-path-handlers.h"
#include "yb/server/webserver.h"

#include "yb/util/flags.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/metrics_writer.h"
#include "yb/util/tcmalloc_util.h"
#include "yb/util/signal_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_log.h"

#include "yb/yql/pggate/util/ybc-internal.h"
#include "yb/yql/ysql_conn_mgr_wrapper/ysql_conn_mgr_stats.h"

using std::string;

namespace yb::pggate {
DECLARE_string(metric_node_name);

static ybpgmEntry *ybpgm_table;
static int ybpgm_num_entries;
static int *num_backends;
MetricEntity::AttributeMap prometheus_attr;
MetricEntity::AttributeMap ysql_conn_mgr_prometheus_attr;
static void (*pullYsqlStatementStats)(void *);
static void (*resetYsqlStatementStats)();
static rpczEntry **rpczResultPointer;
static YbConnectionMetrics *conn_metrics = NULL;

static postgresCallbacks pgCallbacks;

static const char *EXPORTED_INSTANCE = "exported_instance";
static const char *METRIC_TYPE = "metric_type";
static const char *METRIC_ID = "metric_id";

static const char *METRIC_TYPE_SERVER = "server";
static const char *METRIC_ID_YB_YSQLSERVER = "yb.ysqlserver";

static const char *PSQL_SERVER_CONNECTION_TOTAL = "yb_ysqlserver_connection_total";
static const char *PSQL_SERVER_ACTIVE_CONNECTION_TOTAL = "yb_ysqlserver_active_connection_total";
// This is the total number of connections rejected due to "too many clients already"
static const char *PSQL_SERVER_CONNECTION_OVER_LIMIT = "yb_ysqlserver_connection_over_limit_total";
static const char *PSQL_SERVER_MAX_CONNECTION_TOTAL = "yb_ysqlserver_max_connection_total";
static const char *PSQL_SERVER_NEW_CONNECTION_TOTAL = "yb_ysqlserver_new_connection_total";

// YSQL Connection Manager-specific metric labels
static const char *DATABASE = "database";
static const char *USER = "user";

namespace {

void emitConnectionMetrics(PrometheusWriter *pwriter) {
  pgCallbacks.pullRpczEntries();
  rpczEntry *entry = *rpczResultPointer;

  uint64_t tot_connections = 0;
  uint64_t tot_active_connections = 0;
  for (int i = 0; i < *num_backends; ++i, ++entry) {
    if (entry->proc_id > 0) {
      if (entry->backend_active != 0u) {
        tot_active_connections++;
      }
      tot_connections++;
    }
  }

  std::ostringstream errMsg;
  errMsg << "Cannot publish connection metric to Prometheus-metrics endpoint";

  WARN_NOT_OK(
      pwriter->WriteSingleEntryNonTable(
          prometheus_attr, PSQL_SERVER_ACTIVE_CONNECTION_TOTAL, tot_active_connections, "gauge",
          "Total number of active YSQL connections"),
      errMsg.str());

  WARN_NOT_OK(
      pwriter->WriteSingleEntryNonTable(
          prometheus_attr, PSQL_SERVER_CONNECTION_TOTAL, tot_connections, "gauge",
          "Total number of YSQL connections"),
      errMsg.str());

  if (conn_metrics) {
    if (conn_metrics->max_conn) {
      WARN_NOT_OK(
          pwriter->WriteSingleEntryNonTable(
              prometheus_attr, PSQL_SERVER_MAX_CONNECTION_TOTAL, *conn_metrics->max_conn, "gauge",
              "Maximum number of YSQL connections"),
          errMsg.str());
    }
    if (conn_metrics->too_many_conn) {
      WARN_NOT_OK(
          pwriter->WriteSingleEntryNonTable(
              prometheus_attr, PSQL_SERVER_CONNECTION_OVER_LIMIT, *conn_metrics->too_many_conn,
              "gauge", "Number of YSQL connections rejected due to connection limits"),
          errMsg.str());
    }
    if (conn_metrics->new_conn) {
      WARN_NOT_OK(
          pwriter->WriteSingleEntryNonTable(
              prometheus_attr, PSQL_SERVER_NEW_CONNECTION_TOTAL, *conn_metrics->new_conn, "counter",
              "Number of YSQL connections established since start of postmaster"),
          errMsg.str());
    }
  }

  pgCallbacks.freeRpczEntries();
}

void initSqlServerDefaultLabels(const char *metric_node_name) {
  prometheus_attr[EXPORTED_INSTANCE] = metric_node_name;
  prometheus_attr[METRIC_TYPE] = METRIC_TYPE_SERVER;
  prometheus_attr[METRIC_ID] = METRIC_ID_YB_YSQLSERVER;

  // Database-related attribute will have to be added dynamically.
  ysql_conn_mgr_prometheus_attr = prometheus_attr;
}

static void GetYsqlConnMgrStats(std::vector<ConnectionStats> *stats,
                                uint64_t *last_updated_timestamp) {
  char *stats_shm_key = getenv(YSQL_CONN_MGR_SHMEM_KEY_ENV_NAME);
  if(stats_shm_key == NULL)
    return;

  key_t key = (key_t)atoi(stats_shm_key);
  std::ostringstream errMsg;
  int shmid = shmget(key, 0, 0666);
  if (shmid == -1) {
    errMsg << "Unable to find the stats from the shared memory segment, " << strerror(errno);
    return;
  }

  static const uint32_t num_pools = YSQL_CONN_MGR_MAX_POOLS;

  struct ConnectionStats *shmp = (struct ConnectionStats *)shmat(shmid, NULL, 0);
  if (shmp == NULL) {
    errMsg << "Unable to find the stats from the shared memory segment, " << strerror(errno);
    return;
  }

  for (uint32_t itr = 0; itr < num_pools; itr++) {
    if (strcmp(shmp[itr].database_name, "") == 0
      || strcmp(shmp[itr].user_name, "") == 0 )
      break;
    stats->push_back(shmp[itr]);
  }

  if (last_updated_timestamp != NULL) {
    // Read last updated timestamp from control connection pool struct, present at index 0.
    *last_updated_timestamp = shmp[0].last_updated_timestamp;
  }

  shmdt(shmp);
}

void emitYsqlConnectionManagerMetrics(PrometheusWriter *pwriter) {
  std::vector<YsqlConnMgrMetric> ysql_conn_mgr_metrics;
  std::vector<ConnectionStats> stats_list;
  uint64_t num_pools = 0, last_updated_timestamp = 0;

  GetYsqlConnMgrStats(&stats_list, &last_updated_timestamp);
  num_pools = stats_list.size();

  ysql_conn_mgr_prometheus_attr.erase(DATABASE);
  ysql_conn_mgr_prometheus_attr.erase(USER);

  // Publish the count for number of pools.
  WARN_NOT_OK(
      pwriter->WriteSingleEntry(
          ysql_conn_mgr_prometheus_attr, "ysql_conn_mgr_num_pools", num_pools,
          AggregationFunction::kSum, kServerLevel, "gauge",
          "Number of YSQL Connection Manager pools"),
      "Cannot publish Ysql Connection Manager metric to Prometheus-metrics endpoint");

  // Publish the last time ysql conn mgr metrics are emitted.
  WARN_NOT_OK(
      pwriter->WriteSingleEntry(
          ysql_conn_mgr_prometheus_attr, "ysql_conn_mgr_last_updated_timestamp",
          last_updated_timestamp, AggregationFunction::kSum, kServerLevel, "gauge",
          "Timestamp of last update to YSQL Connection Manager metrics"),
      "Cannot publish Ysql Connection Manager metric to Promotheus-metircs endpoint");

  // Iterate over stats collected for each DB (pool), publish them iteratively.
  for (ConnectionStats stats : stats_list) {
    ysql_conn_mgr_metrics.push_back(
        {"ysql_conn_mgr_active_clients", stats.active_clients, "gauge",
         "Number of active logical connections"});
    ysql_conn_mgr_metrics.push_back(
        {"ysql_conn_mgr_queued_clients", stats.queued_clients, "gauge",
         "Number of logical connections waiting in the queue for a physical connection"});
    ysql_conn_mgr_metrics.push_back(
        {"ysql_conn_mgr_waiting_clients", stats.waiting_clients, "gauge",
         "Number of logical connections that are either idle (i.e. no ongoing transaction) or "
         "waiting for the worker thread to be processed"});
    ysql_conn_mgr_metrics.push_back(
        {"ysql_conn_mgr_active_servers", stats.active_servers, "gauge",
         "Number of physical connections that are currently attached to a logical connection."});
    ysql_conn_mgr_metrics.push_back(
        {"ysql_conn_mgr_idle_servers", stats.idle_servers, "gauge",
         "Number of physical connections that are not attached to any logical connection"});
    ysql_conn_mgr_metrics.push_back(
        {"ysql_conn_mgr_query_rate", stats.query_rate, "gauge",
         "Query rate for last <stats_interval> (set in odyssey config) period of time."});
    ysql_conn_mgr_metrics.push_back(
        {"ysql_conn_mgr_transaction_rate", stats.transaction_rate, "gauge",
         "Transaction rate for last <stats_interval> (set in odyssey config) period of time"});
    ysql_conn_mgr_metrics.push_back(
        {"ysql_conn_mgr_avg_wait_time_ns", stats.avg_wait_time_ns, "gauge",
         "Avg wait time (in nanoseconds) for a logical connection to be attached to a physical "
         "connection"});
    ysql_conn_mgr_prometheus_attr[DATABASE] = stats.database_name;
    ysql_conn_mgr_prometheus_attr[USER] = stats.user_name;

    // Publish collected metrics for the current pool.
    for (auto entry : ysql_conn_mgr_metrics) {
      WARN_NOT_OK(
          pwriter->WriteSingleEntry(
              ysql_conn_mgr_prometheus_attr, entry.name, entry.value, AggregationFunction::kSum,
              kServerLevel, entry.type, entry.help),
          "Cannot publish Ysql Connection Manager metric to Prometheus-metrics endpoint");
    }
    // Clear the collected metrics for the metrics collected for the next pool.
    ysql_conn_mgr_metrics.clear();
  }
}
}  // namespace

static void PgMetricsHandler(const Webserver::WebRequest &req, Webserver::WebResponse *resp) {
  std::stringstream *output = &resp->output;
  JsonWriter::Mode json_mode;
  string arg = FindWithDefault(req.parsed_args, "compact", "false");
  json_mode = ParseLeadingBoolValue(arg.c_str(), false) ? JsonWriter::COMPACT : JsonWriter::PRETTY;

  JsonWriter writer(output, json_mode);
  writer.StartArray();
  writer.StartObject();
  writer.String("type");
  writer.String("server");
  writer.String("id");
  writer.String("yb.ysqlserver");
  writer.String("metrics");
  writer.StartArray();

  for (const auto *entry = ybpgm_table, *end = entry + ybpgm_num_entries; entry != end; ++entry) {
    writer.StartObject();
    writer.String("name");
    writer.String(entry->name);
    writer.String("count");
    writer.Int64(entry->calls);
    writer.String("sum");
    writer.Int64(entry->total_time);
    writer.String("rows");
    writer.Int64(entry->rows);
    if (strlen(entry->table_name) > 0) {
      writer.String("table_name");
      writer.String(entry->table_name);
    }
    writer.EndObject();
  }

  writer.EndArray();
  writer.EndObject();
  writer.EndArray();
}

static void PgStatStatementsHandler(
    const Webserver::WebRequest &req, Webserver::WebResponse *resp) {
  std::stringstream *output = &resp->output;
  JsonWriter::Mode json_mode;
  string arg = FindWithDefault(req.parsed_args, "compact", "false");
  json_mode = ParseLeadingBoolValue(arg.c_str(), false) ? JsonWriter::COMPACT : JsonWriter::PRETTY;
  JsonWriter writer(output, json_mode);

  writer.StartObject();

  writer.String("statements");
  if (pullYsqlStatementStats) {
    writer.StartArray();
    pullYsqlStatementStats(&writer);
    writer.EndArray();
  } else {
    writer.String("PG Stat Statements module is disabled.");
  }

  writer.EndObject();
}

static void PgStatStatementsResetHandler(
    const Webserver::WebRequest &req, Webserver::WebResponse *resp) {
  std::stringstream *output = &resp->output;
  JsonWriter::Mode json_mode;
  string arg = FindWithDefault(req.parsed_args, "compact", "false");
  json_mode = ParseLeadingBoolValue(arg.c_str(), false) ? JsonWriter::COMPACT : JsonWriter::PRETTY;
  JsonWriter writer(output, json_mode);

  writer.StartObject();

  writer.String("statements");
  if (resetYsqlStatementStats) {
    resetYsqlStatementStats();
    writer.String("PG Stat Statements reset.");
  } else {
    writer.String("PG Stat Statements module is disabled.");
  }

  writer.EndObject();
}

static void WriteAsJsonTimestampAndRunningForMs(
    JsonWriter *writer, const std::string &prefix, int64 start_timestamp, int64 snapshot_timestamp,
    bool active) {
  writer->String(prefix + "_start_time");
  writer->String(pgCallbacks.getTimestampTzToStr(start_timestamp));

  if (!active) {
    return;
  }

  writer->String(prefix + "_running_for_ms");
  writer->Int64(pgCallbacks.getTimestampTzDiffMs(start_timestamp, snapshot_timestamp));
}

static void PgRpczHandler(const Webserver::WebRequest &req, Webserver::WebResponse *resp) {
  std::stringstream *output = &resp->output;
  pgCallbacks.pullRpczEntries();
  int64 snapshot_timestamp = pgCallbacks.getTimestampTz();

  JsonWriter::Mode json_mode;
  string arg = FindWithDefault(req.parsed_args, "compact", "false");
  json_mode = ParseLeadingBoolValue(arg.c_str(), false) ? JsonWriter::COMPACT : JsonWriter::PRETTY;
  JsonWriter writer(output, json_mode);
  rpczEntry *entry = *rpczResultPointer;

  writer.StartObject();
  writer.String("connections");
  writer.StartArray();
  for (int i = 0; i < *num_backends; ++i, ++entry) {
    if (entry->proc_id > 0) {
      writer.StartObject();
      if (entry->db_oid) {
        writer.String("db_oid");
        writer.Int64(entry->db_oid);
        writer.String("db_name");
        writer.String(entry->db_name);
      }

      if (strlen(entry->query) > 0) {
        writer.String("query");
        writer.String(entry->query);
      }

      WriteAsJsonTimestampAndRunningForMs(
          &writer, "process", entry->process_start_timestamp, snapshot_timestamp,
          entry->backend_active);

      if (entry->transaction_start_timestamp > 0) {
        WriteAsJsonTimestampAndRunningForMs(
            &writer, "transaction", entry->transaction_start_timestamp, snapshot_timestamp,
            entry->backend_active);
      }

      if (entry->query_start_timestamp > 0) {
        WriteAsJsonTimestampAndRunningForMs(
            &writer, "query", entry->query_start_timestamp, snapshot_timestamp,
            entry->backend_active);
      }

      writer.String("application_name");
      writer.String(entry->application_name);
      writer.String("backend_type");
      writer.String(entry->backend_type);
      writer.String("backend_status");
      writer.String(entry->backend_status);

      if (entry->host) {
        writer.String("host");
        writer.String(entry->host);
      }

      if (entry->port) {
        writer.String("port");
        writer.String(entry->port);
      }

      writer.EndObject();
    }
  }
  writer.EndArray();
  writer.EndObject();
  pgCallbacks.freeRpczEntries();
}

static void PgLogicalRpczHandler(const Webserver::WebRequest &req, Webserver::WebResponse *resp) {
  JsonWriter::Mode json_mode;
  string arg = FindWithDefault(req.parsed_args, "compact", "false");
  json_mode = ParseLeadingBoolValue(arg.c_str(), false) ? JsonWriter::COMPACT : JsonWriter::PRETTY;
  std::stringstream *output = &resp->output;
  JsonWriter writer(output, json_mode);
  std::vector<ConnectionStats> stats_list;
  GetYsqlConnMgrStats(&stats_list, NULL);

  writer.StartObject();
  writer.String("pools");
  writer.StartArray();

  for (const auto &stat : stats_list) {
    writer.StartObject();

    // The type of pool. There are two types of pool in Ysql Connection Manager, "gloabl" and
    // "control".
    writer.String("database_name");
    writer.String(stat.database_name);

    writer.String("user_name");
    writer.String(stat.user_name);

    // Number of logical connections that are attached to any physical connection. A logical
    // connection gets attached to a physical connection during lifetime of a transaction.
    writer.String("active_logical_connections");
    writer.Int64(stat.active_clients);

    // Number of logical connections waiting in the queue to get a physical connection.
    writer.String("queued_logical_connections");
    writer.Int64(stat.queued_clients);

    // Number of logical connections which are either idle (i.e. no ongoing transaction) or waiting
    // for the worker thread to be processed (i.e. waiting for od_router_attach to be called).
    writer.String("waiting_logical_connections");
    writer.Int64(stat.waiting_clients);

    // Number of physical connections which currently attached to a logical connection.
    writer.String("active_physical_connections");
    writer.Int64(stat.active_servers);

    // Number of physical connections which are not attached to any logical connection.
    writer.String("idle_physical_connections");
    writer.Int64(stat.idle_servers);

    // Number of logical connections stuck to a physical connection for the
    // lifetime of the session.
    writer.String("sticky_connections");
    writer.Int64(stat.sticky_connections);

    // Avg wait time for a logical connection to be attached to a physical connection.
    // i.e. queue time + time taken to search and attach a server.
    // This average is taken for the last "stats_interval" (odyssey config) period of time.
    writer.String("avg_wait_time_ns");
    writer.Int64(stat.avg_wait_time_ns);

    // Query rate for last  "stats_interval" (set in odyssey config) period of time.
    writer.String("qps");
    writer.Int64(stat.query_rate);

    // Transaction rate for last "stats_interval" (set in odyssey config) period of time.
    writer.String("tps");
    writer.Int64(stat.transaction_rate);

    writer.EndObject();
  }
  writer.EndArray();
  writer.EndObject();
}

static void PgPrometheusMetricsHandler(
    const Webserver::WebRequest &req, Webserver::WebResponse *resp) {
  std::stringstream *output = &resp->output;
  MetricPrometheusOptions opts;
  opts.export_help_and_type = ExportHelpAndType::kTrue;
  ParseRequestOptions(req, &opts);
  PrometheusWriter writer(output, opts);

  for (int i = 0; i < ybpgm_num_entries; ++i) {
    std::string metric_name = ybpgm_table[i].name;
    std::string table_name = ybpgm_table[i].table_name;
    if (!table_name.empty()) {
      prometheus_attr["table_name"] = table_name;
    }
    WARN_NOT_OK(
        writer.WriteSingleEntry(
            prometheus_attr, metric_name + "_count", ybpgm_table[i].calls,
            AggregationFunction::kSum, kServerLevel, "counter", ybpgm_table[i].count_help),
        "Couldn't write text metrics for Prometheus");

    // Skip over empty metrics.
    if (strcmp(ybpgm_table[i].sum_help, "Not applicable") != 0) {
      WARN_NOT_OK(
          writer.WriteSingleEntry(
              prometheus_attr, metric_name + "_sum", ybpgm_table[i].total_time,
              AggregationFunction::kSum, kServerLevel, "counter", ybpgm_table[i].sum_help),
          "Couldn't write text metrics for Prometheus");
    }
    prometheus_attr.erase("table_name");
  }

  // Publish sql server connection related metrics
  emitConnectionMetrics(&writer);

  // Publish Ysql Connection Manager related metrics
  emitYsqlConnectionManagerMetrics(&writer);
}

extern "C" {
void WriteStartObjectToJson(void *p1) {
  JsonWriter *writer = static_cast<JsonWriter *>(p1);
  writer->StartObject();
}

void WriteStringToJson(void *p1, const char* key, const char* value) {
  JsonWriter *writer = static_cast<JsonWriter *>(p1);
  writer->String(key);
  writer->String(value);
}

void WriteIntToJson(void *p1, const char* key, const int64_t value) {
  JsonWriter *writer = static_cast<JsonWriter *>(p1);
  writer->String(key);
  writer->Int64(value);
}

void WriteDoubleToJson(void *p1, const char* key, const double value) {
  JsonWriter *writer = static_cast<JsonWriter *>(p1);
  writer->String(key);
  writer->Double(value);
}

void WriteIntArrayToJson(void *p1, const char *key, const int64 *values, const size_t size) {
  JsonWriter *writer = static_cast<JsonWriter *>(p1);
  writer->String(key);
  writer->StartArray();
  for (size_t i = 0; i < size; ++i) {
    writer->Int64(values[i]);
  }
  writer->EndArray();
}

void WriteDoubleArrayToJson(void *p1, const char *key, const double *values, const size_t size) {
  JsonWriter *writer = static_cast<JsonWriter *>(p1);
  writer->String(key);
  writer->StartArray();
  for (size_t i = 0; i < size; ++i) {
    writer->Double(values[i]);
  }
  writer->EndArray();
}

void WriteHistArrayBeginToJson(void *p1) {
  JsonWriter *writer = static_cast<JsonWriter *>(p1);
  writer->String("yb_latency_histogram");
  writer->StartArray();
}

void WriteArrayBeginToJson(void *p1, const char* key) {
  JsonWriter *writer = static_cast<JsonWriter *>(p1);
  writer->String(key);
  writer->StartArray();
}

void WriteIntValueObjectToJson(void *p1, void *p2, void *p3) {
  JsonWriter *writer = static_cast<JsonWriter *>(p1);
  char *key = static_cast<char*>(p2);
  int64_t *value = static_cast<int64_t *>(p3);
  writer->StartObject();
  writer->String(key);
  writer->Int64(*value);
  writer->EndObject();
}

void WriteArrayEndToJson(void* p1) {
  JsonWriter *writer = static_cast<JsonWriter *>(p1);
  writer->EndArray();
}

void WriteEndObjectToJson(void *p1) {
  JsonWriter *writer = static_cast<JsonWriter *>(p1);
  writer->EndObject();
}

WebserverWrapper *CreateWebserver(char *listen_addresses, int port) {
  WebserverOptions opts;
  opts.bind_interface = listen_addresses;
  opts.port = port;
  // Important! Since postgres functions aren't generally thread-safe,
  // we shouldn't allow more than one worker thread at a time.
  opts.num_worker_threads = 1;
  return reinterpret_cast<WebserverWrapper *>(new Webserver(opts, "Postgres webserver"));
}

void RegisterMetrics(ybpgmEntry *tab, int num_entries, char *metric_node_name) {
  ybpgm_table = tab;
  ybpgm_num_entries = num_entries;
  initSqlServerDefaultLabels(metric_node_name);
}

void RegisterGetYsqlStatStatements(void (*getYsqlStatementStats)(void *)) {
  pullYsqlStatementStats = getYsqlStatementStats;
}

void RegisterResetYsqlStatStatements(void (*fn)()) {
    resetYsqlStatementStats = fn;
}

void RegisterRpczEntries(
    postgresCallbacks *callbacks, int *num_backends_ptr, rpczEntry **rpczEntriesPointer,
    YbConnectionMetrics *conn_metrics_ptr) {
  pgCallbacks = *callbacks;
  num_backends = num_backends_ptr;
  rpczResultPointer = rpczEntriesPointer;
  conn_metrics = conn_metrics_ptr;
}

YBCStatus StartWebserver(WebserverWrapper *webserver_wrapper) {
  Webserver *webserver = reinterpret_cast<Webserver *>(webserver_wrapper);
  webserver->RegisterPathHandler(
      "/connections", "Ysql Connection Manager Stats", PgLogicalRpczHandler, false, false);
  webserver->RegisterPathHandler("/metrics", "Metrics", PgMetricsHandler, false, false);
  webserver->RegisterPathHandler("/jsonmetricz", "Metrics", PgMetricsHandler, false, false);
  webserver->RegisterPathHandler(
      "/prometheus-metrics", "Metrics", PgPrometheusMetricsHandler, false, false);
  webserver->RegisterPathHandler("/rpcz", "RPCs in progress", PgRpczHandler, false, false);
  webserver->RegisterPathHandler(
      "/statements", "PG Stat Statements", PgStatStatementsHandler, false, false);
  webserver->RegisterPathHandler(
      "/statements-reset", "Reset PG Stat Statements", PgStatStatementsResetHandler, false, false);
  webserver->RegisterPathHandler("/webserver-heap-snapshot", "",
      PprofHeapSnapshotHandler, true /* is_styled */, false /* is_on_nav_bar */);
  webserver->RegisterPathHandler("/memz", "Memory (total)", MemUsageHandler, true, false);

  return ToYBCStatus(WithMaskedYsqlSignals([webserver]() { return webserver->Start(); }));
}

void DestroyWebserver(struct WebserverWrapper *webserver) {
  Webserver *webserver_impl = reinterpret_cast<Webserver *>(webserver);
  delete webserver_impl;
}

void SetWebserverConfig(
    WebserverWrapper *webserver_wrapper, bool enable_access_logging, bool enable_tcmalloc_logging,
    int webserver_profiler_sample_freq_bytes) {
  Webserver *webserver = reinterpret_cast<Webserver *>(webserver_wrapper);
  webserver->SetLogging(enable_access_logging, enable_tcmalloc_logging);

  if (GetTCMallocSamplingFrequency() != webserver_profiler_sample_freq_bytes) {
    SetTCMallocSamplingFrequency(webserver_profiler_sample_freq_bytes);
  }
}
}  // extern "C"

}  // namespace yb::pggate
