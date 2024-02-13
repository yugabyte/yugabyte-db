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

#include "yb/yql/pggate/ysql_bench_metrics_handler/ysql_bench_metrics_handler.h"

#include <map>
#include <vector>
#include <string>

#include "yb/gutil/map-util.h"
#include "yb/server/webserver.h"

#include "yb/util/logging.h"
#include "yb/util/metrics_writer.h"
#include "yb/util/signal_util.h"
#include "yb/yql/pggate/util/ybc-internal.h"

using std::string;

namespace yb::pggate {
DECLARE_string(metric_node_name);

static YsqlBenchMetricEntry *ysql_bench_metric_entry;

MetricEntity::AttributeMap prometheus_attr;

static const char *EXPORTED_INSTANCE = "exported_instance";
static const char *METRIC_TYPE = "metric_type";
static const char *METRIC_ID = "metric_id";

static const char *METRIC_TYPE_YSQL_BENCH = "ysql_bench";
static const char *METRIC_ID_YSQL_BENCH = "ysql_bench";

namespace {

void initYSQLBenchDefaultLabels(const char *metric_node_name) {
  prometheus_attr[EXPORTED_INSTANCE] = metric_node_name;
  prometheus_attr[METRIC_TYPE] = METRIC_TYPE_YSQL_BENCH;
  prometheus_attr[METRIC_ID] = METRIC_ID_YSQL_BENCH;
}

}  // namespace

static void PgPrometheusMetricsHandler(
    const Webserver::WebRequest &req, Webserver::WebResponse *resp) {
  std::stringstream *output = &resp->output;
  MetricPrometheusOptions metricOptions;
  PrometheusWriter writer(output, metricOptions);

  WARN_NOT_OK(
      writer.WriteSingleEntry(
          prometheus_attr, "ysql_bench_average_latency",
          ysql_bench_metric_entry->average_latency, AggregationFunction::kSum,
          kServerLevel),
      "Couldn't write text metrics for Prometheus");
  WARN_NOT_OK(
      writer.WriteSingleEntry(
          prometheus_attr, "ysql_bench_success_count",
          ysql_bench_metric_entry->success_count, AggregationFunction::kSum,
          kServerLevel),
      "Couldn't write text metrics for Prometheus");
  WARN_NOT_OK(
      writer.WriteSingleEntry(
          prometheus_attr, "ysql_bench_failure_count",
          ysql_bench_metric_entry->failure_count, AggregationFunction::kSum,
          kServerLevel),
      "Couldn't write text metrics for Prometheus");
  WARN_NOT_OK(
      writer.WriteSingleEntry(
          prometheus_attr, "ysql_bench_latency_sum",
          ysql_bench_metric_entry->latency_sum, AggregationFunction::kSum,
          kServerLevel),
      "Couldn't write text metrics for Prometheus");
  WARN_NOT_OK(
      writer.WriteSingleEntry(
          prometheus_attr, "ysql_bench_success_count_sum",
          ysql_bench_metric_entry->success_count_sum, AggregationFunction::kSum,
          kServerLevel),
      "Couldn't write text metrics for Prometheus");
  WARN_NOT_OK(
      writer.WriteSingleEntry(
          prometheus_attr, "ysql_bench_failure_count_sum",
          ysql_bench_metric_entry->failure_count_sum, AggregationFunction::kSum,
          kServerLevel),
      "Couldn't write text metrics for Prometheus");
}

extern "C" {

WebserverWrapper *CreateWebserver(char *listen_addresses, int port) {
  WebserverOptions opts;
  opts.bind_interface = listen_addresses;
  opts.port = port;
  opts.num_worker_threads = 1;
  return reinterpret_cast<WebserverWrapper *>(new Webserver(opts, "ysql_bench metrics webserver"));
}

int StartWebserver(WebserverWrapper *webserver_wrapper) {
  Webserver *webserver = reinterpret_cast<Webserver *>(webserver_wrapper);
  webserver->RegisterPathHandler(
      "/prometheus-metrics", "Metrics", PgPrometheusMetricsHandler, false, false);
  auto status = WithMaskedYsqlSignals([webserver]() { return webserver->Start(); });
  if (!status.ok()) {
    LOG(ERROR) << "Error starting webserver: " << status.ToString();
    return 1;
  }

  return 0;
}

void StopWebserver(WebserverWrapper *webserver_wrapper) {
  Webserver *webserver = reinterpret_cast<Webserver *>(webserver_wrapper);
  webserver->Stop();
}

void InitGoogleLogging(char *prog_name) {
  yb::InitGoogleLoggingSafeBasic(prog_name);
}

void RegisterMetrics(YsqlBenchMetricEntry *metric_entry, char *metric_node_name) {
  ysql_bench_metric_entry = metric_entry;
  initYSQLBenchDefaultLabels(metric_node_name);
}
}  // extern "C"

}  // namespace yb::pggate
