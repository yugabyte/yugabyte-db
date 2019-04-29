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

#include <map>
#include "yb/server/webserver.h"
#include "yb/util/web_callback_registry.h"
#include "yb/server/pgsql_webserver_wrapper.h"
#include "yb/util/jsonwriter.h"
#include "yb/gutil/map-util.h"
#include "yb/util/ybc-internal.h"
#include "yb/util/metrics.h"


DECLARE_string(metric_node_name);

using yb::Webserver;
using yb::JsonWriter;
using yb::PrometheusWriter;
using yb::WebCallbackRegistry;

static ybpgmEntry *ybpgm_table;
static int ybpgm_num_entries;
yb::MetricEntity::AttributeMap prometheus_attr;

static void PgMetricsHandler(const Webserver::WebRequest& req, std::stringstream* output) {
  JsonWriter::Mode json_mode;
  string arg = FindWithDefault(req.parsed_args, "compact", "false");
  json_mode = ParseLeadingBoolValue(arg.c_str(), false) ?
              JsonWriter::COMPACT : JsonWriter::PRETTY;

  JsonWriter writer(output, json_mode);
  writer.StartArray();
  writer.StartObject();
  writer.String("type");
  writer.String("server");
  writer.String("id");
  writer.String("yb.ysqlserver");
  writer.String("metrics");
  writer.StartArray();

  for (int i = 0; i < ybpgm_num_entries; ++i) {
    writer.StartObject();
    writer.String("name");
    writer.String(ybpgm_table[i].name);
    writer.String("count");
    writer.Int64(ybpgm_table[i].calls);
    writer.String("sum");
    writer.Int64(ybpgm_table[i].total_time);
    writer.EndObject();
  }

  writer.EndArray();
  writer.EndObject();
  writer.EndArray();
}

static void PgPrometheusMetricsHandler(const Webserver::WebRequest& req,
                                       std::stringstream* output) {

  PrometheusWriter writer(output);

  // Max size of ybpgm_table name (100 incl \0 char) + max size of "_count"/"_sum" (6 excl \0).
  char copied_name[106];
  for (int i = 0; i < ybpgm_num_entries; ++i) {
    snprintf(copied_name, sizeof(copied_name), "%s%s", ybpgm_table[i].name, "_count");
    WARN_NOT_OK(writer.WriteSingleEntry(prometheus_attr,
                                        copied_name,
                                        ybpgm_table[i].calls),
                                        "Couldn't write text metrics for Prometheus");
    snprintf(copied_name, sizeof(copied_name), "%s%s", ybpgm_table[i].name, "_sum");
    WARN_NOT_OK(writer.WriteSingleEntry(prometheus_attr,
                                        copied_name,
                                        ybpgm_table[i].total_time),
                                        "Couldn't write text metrics for Prometheus");
  }
}

extern "C" {
  WebserverWrapper *CreateWebserver(char *listen_addresses, int port) {
    yb::WebserverOptions opts;
    opts.bind_interface = listen_addresses;
    opts.port = port;
    return reinterpret_cast<WebserverWrapper *> (new Webserver(opts, "Postgres webserver"));
  }

  void RegisterMetrics(ybpgmEntry *tab, int num_entries, char *metric_node_name) {
    ybpgm_table = tab;
    ybpgm_num_entries = num_entries;

    prometheus_attr["exported_instance"] = metric_node_name;
    prometheus_attr["metric_type"] = "server";
    prometheus_attr["metric_id"] = "yb.ysqlserver";
  }

  YBCStatus StartWebserver(WebserverWrapper *webserver_wrapper) {
    Webserver *webserver = reinterpret_cast<Webserver *> (webserver_wrapper);
    webserver->RegisterPathHandler("/metrics", "Metrics", PgMetricsHandler, false, false);
    webserver->RegisterPathHandler("/jsonmetricz", "Metrics", PgMetricsHandler, false, false);
    webserver->RegisterPathHandler("/prometheus-metrics", "Metrics", PgPrometheusMetricsHandler,
                                   false, false);
    return ToYBCStatus(webserver->Start());
  }
};
