// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "yb/server/default-path-handlers.h"

#include <sys/stat.h>

#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include <set>

#include <boost/algorithm/string.hpp>
#include "yb/util/flags/auto_flags_util.h"
#include "yb/util/string_case.h"

#if YB_TCMALLOC_ENABLED
#include <gperftools/malloc_extension.h>
#endif

#include "yb/fs/fs_manager.h"

#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rpc/secure.h"
#include "yb/rpc/secure_stream.h"
#include "yb/server/html_print_helper.h"
#include "yb/server/pprof-path-handlers.h"
#include "yb/server/server_base.h"
#include "yb/server/webserver.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/histogram.pb.h"
#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/memory/memory.h"
#include "yb/util/metrics.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"
#include "yb/util/stack_trace_tracker.h"
#include "yb/util/url-coding.h"
#include "yb/util/version_info.h"
#include "yb/util/version_info.pb.h"

DEFINE_RUNTIME_uint64(web_log_bytes, 1024 * 1024,
    "The maximum number of bytes to display on the debug webserver's log page");
TAG_FLAG(web_log_bytes, advanced);

DEFINE_RUNTIME_bool(export_help_and_type_in_prometheus_metrics, true,
    "Include #TYPE and #HELP in Prometheus metrics output by default");

DEFINE_RUNTIME_uint32(max_prometheus_metric_entries, UINT32_MAX,
    "The maximum number of Prometheus metric entries returned in each scrape. Note that if "
    "adding a metric with all its entities would exceed the limit, then we will drop them all."
    "Thus, the actual number of metric entries returned might be smaller than the limit.");

DECLARE_bool(track_stack_traces);
DECLARE_bool(TEST_mini_cluster_mode);

namespace yb {

using boost::replace_all;
using std::ifstream;
using std::string;
using std::endl;
using std::map;
using std::vector;
using strings::Substitute;

using namespace std::placeholders;

namespace {

// Html/Text formatting tags
struct Tags {
  string pre_tag, end_pre_tag, line_break, header, end_header, table, end_table, row, end_row,
      table_header, end_table_header, cell, end_cell;

  // If as_text is true, set the html tags to a corresponding raw text representation.
  explicit Tags(bool as_text) {
    if (as_text) {
      pre_tag = "";
      end_pre_tag = "\n";
      line_break = "\n";
      header = "";
      end_header = "\n";
      table = "";
      end_table = "\n";
      row = "";
      end_row = "\n";
      table_header = "";
      end_table_header = "";
      cell = "";
      end_cell = "|";
    } else {
      pre_tag = "<pre>";
      end_pre_tag = "</pre>";
      line_break = "<br/>";
      header = "<h2>";
      end_header = "</h2>";
      table = "<table class='table table-striped'>";
      end_table = "</table>";
      row = "<tr>";
      end_row = "</tr>";
      table_header = "<th>";
      end_table_header = "</th>";
      cell = "<td>";
      end_cell = "</td>";
    }
  }
};

// Writes the last FLAGS_web_log_bytes of the INFO logfile to a webpage
// Note to get best performance, set GLOG_logbuflevel=-1 to prevent log buffering
static void LogsHandler(const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  bool as_text = (req.parsed_args.find("raw") != req.parsed_args.end());
  Tags tags(as_text);
  string logfile;
  GetFullLogFilename(google::INFO, &logfile);
  (*output) << tags.header <<"INFO logs" << tags.end_header << endl;
  (*output) << "Log path is: " << EscapeForHtmlToString(logfile) << endl;

  struct stat file_stat;
  if (stat(logfile.c_str(), &file_stat) == 0) {
    size_t size = file_stat.st_size;
    size_t seekpos = size < FLAGS_web_log_bytes ? 0L : size - FLAGS_web_log_bytes;
    ifstream log(logfile.c_str(), std::ios::in);
    // Note if the file rolls between stat and seek, this could fail
    // (and we could wind up reading the whole file). But because the
    // file is likely to be small, this is unlikely to be an issue in
    // practice.
    log.seekg(seekpos);
    (*output) << tags.line_break << "Showing last " << FLAGS_web_log_bytes
              << " bytes of log" << endl;
    (*output) << tags.line_break << tags.pre_tag;
    EscapeForHtml(&log, output);
    (*output) << tags.end_pre_tag;

  } else {
    (*output) << tags.line_break << "Couldn't open INFO log file: "
              << EscapeForHtmlToString(logfile);
  }
}

void ConvertFlagsToJson(
    const std::unordered_map<FlagType, std::vector<FlagInfo>>& flag_infos,
    std::stringstream* output) {
  JsonWriter jw(output, JsonWriter::COMPACT);
  jw.StartObject();
  jw.String("flags");
  jw.StartArray();

  for (const auto& [type, flags] : flag_infos) {
    for (const auto& flag : flags) {
      jw.StartObject();
      jw.String("name");
      jw.String(flag.name);
      jw.String("value");
      jw.String(flag.value);
      jw.String("type");
      // Remove the prefix 'k' from the type name
      jw.String(ToString(type).substr(1));
      jw.EndObject();
    }
  }

  jw.EndArray();
  jw.EndObject();
}

std::unordered_map<FlagType, std::vector<FlagInfo>> GetFlagInfos(
    const Webserver::WebRequest& req, Webserver* webserver, bool filter_default_flags) {
  std::map<std::string, std::string> custom_varz;
  if (FLAGS_TEST_mini_cluster_mode) {
    const string* custom_varz_ptr = FindOrNull(req.parsed_args, "TEST_custom_varz");
    if (custom_varz_ptr != nullptr) {
      SplitStringToMapUsing(*custom_varz_ptr, "\n", &custom_varz);
    }
  }

  std::function<bool(const std::string&)> default_flag_filter = nullptr;
  if (filter_default_flags) {
    default_flag_filter = [webserver](const std::string& flag_name) {
      return webserver->ContainsFlag(flag_name);
    };
  }

  return yb::GetFlagInfos(
      [webserver](const std::string& flag_name) { return webserver->ContainsAutoFlag(flag_name); },
      std::move(default_flag_filter), custom_varz);
}

// Registered to handle "/api/v1/varz", and prints out all command-line flags and their values in
// JSON format.
static void GetFlagsJsonHandler(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp, Webserver* webserver) {
  const auto flag_infos = GetFlagInfos(req, webserver, /*filter_default_flags=*/false);
  ConvertFlagsToJson(std::move(flag_infos), &resp->output);
}

// Registered to handle "/varz", and prints out all command-line flags and their values in tabular
// format. If "raw" argument was passed ("/varz?raw") then prints it in "--name=value" format.
static void FlagsHandler(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp, Webserver* webserver) {
  std::stringstream& output = resp->output;
  const auto is_raw = req.parsed_args.contains("raw");

  auto flag_infos = GetFlagInfos(req, webserver, /*filter_default_flags=*/!is_raw);
  if (is_raw) {
    for (const auto& [_, flags] : flag_infos) {
      for (const auto& flag : flags) {
        output << "--" << flag.name << "=" << flag.value << endl;
      }
    }
    return;
  }

  Tags tags(false /* as_text */);

  HtmlPrintHelper html_print_helper(output);
  for (const auto& type : FlagTypeList()) {
    if (!ContainsKey(flag_infos, type)) {
      continue;
    }

    auto field_set = html_print_helper.CreateFieldset(ToString(type).substr(1) + " Flags");

    std::vector<std::string> column_names = {"Name", "Value"};
    if (type == FlagType::kAuto) {
      column_names.push_back("State");
    }
    auto html_table = html_print_helper.CreateTablePrinter(ToString(type), column_names);
    for (auto& flag : flag_infos.at(type)) {
      if (type == FlagType::kAuto) {
        html_table.AddRow(
            flag.name, flag.value, (flag.is_auto_flag_promoted ? "PROMOTED" : "NOT PROMOTED"));
      } else {
        html_table.AddRow(flag.name, flag.value);
      }
    }
    html_table.Print();
  }
}

// Registered to handle "/status", and simply returns empty JSON.
static void StatusHandler(const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  (*output) << "{}";
}

static void JsonOutputMemTrackers(const std::vector<MemTrackerData>& trackers,
                                  std::stringstream *output,
                                  int max_depth,
                                  bool use_full_path) {
  JsonWriter jw(output, JsonWriter::COMPACT);
  for (auto it = trackers.begin(); it != trackers.end(); it++) {
    // If the data.depth >= max_depth, skip the info.
    const auto data = *it;
    if (data.depth > max_depth) {
      continue;
    }
    const auto& tracker = data.tracker;
    const std::string tracker_id = use_full_path ? tracker->ToString() : tracker->id();
    // Output the object
    jw.StartObject();
    jw.String("id");
    jw.String(tracker_id);
    jw.String("limit_bytes");
    jw.Int64(tracker->limit());
    jw.String("current_consumption_bytes");
    jw.Int64(tracker->consumption());
    jw.String("peak_consumption_bytes");
    jw.Int64(tracker->peak_consumption());

    // UpdateConsumption returns true if consumption is taken from external source,
    // for instance tcmalloc stats. So we should show only it in this case.
    if (data.consumption_excluded_from_ancestors && !data.tracker->UpdateConsumption()) {
      jw.String("full_consumption_bytes");
      jw.Int64(tracker->consumption() + data.consumption_excluded_from_ancestors);
    }

    jw.String("children");
    jw.StartArray();
    const auto next_tracker = std::next(it, 1);
    if (next_tracker == trackers.end()) {
      for (int i = 0; i < data.depth + 1; ++i) {
        jw.EndArray();
        jw.EndObject();
      }
    } else if ((*next_tracker).depth <= data.depth) {
      for (int i = 0; i < data.depth - (*next_tracker).depth + 1; ++i) {
        jw.EndArray();
        jw.EndObject();
      }
    }
  }
}

static void HtmlOutputMemTrackers(const std::vector<MemTrackerData>& trackers,
                                  std::stringstream *output,
                                  int max_depth,
                                  bool use_full_path) {
  *output << "<h1>Memory usage by subsystem</h1>\n";
  *output << "<table class='table table-striped' id='memtrackerstable'>\n";
  *output << "  <tr><th>Id</th><th>Current Consumption</th>"
      "<th>Peak consumption</th><th>Limit</th></tr>\n";
  for (auto it = trackers.begin(); it != trackers.end(); it++) {
    // If the data.depth >= max_depth, skip the info.
    const auto data = *it;
    if (data.depth > max_depth) {
      continue;
    }
    const auto& tracker = data.tracker;
    const std::string limit_str =
        tracker->limit() == -1 ? "none" : HumanReadableNumBytes::ToString(tracker->limit());
    const std::string current_consumption_str =
        HumanReadableNumBytes::ToString(tracker->consumption());
    const std::string peak_consumption_str =
        HumanReadableNumBytes::ToString(tracker->peak_consumption());
    const std::string tracker_id =
        EscapeForHtmlToString(use_full_path ? tracker->ToString() : tracker->id());
    // GetPeakRootConsumption() in client-stress-test.cc depends on the HTML formatting.
    // Update the test, in case this changes in future.
    if (data.depth < 2) {
      *output << Format(
        "  <tr data-depth=\"$0\" class=\"level$0 collapse\" style=\"display: table-row;\">\n",
        data.depth);
    } else if (data.depth == 2) {
      *output << Format(
        "  <tr data-depth=\"$0\" class=\"level$0 expand\" style=\"display: table-row;\">\n",
        data.depth);
    } else {
      *output << Format(
        "  <tr data-depth=\"$0\" class=\"level$0 expand\" style=\"display: none;\">\n",
        data.depth);
    }
    const auto next_tracker = std::next(it, 1);
    if (next_tracker != trackers.end() && (*next_tracker).depth > data.depth && data.depth != 0) {
      *output << "    <td><span class=\"toggle\"></span>" << tracker_id << "</td>";
    } else if (next_tracker != trackers.end() && (*next_tracker).depth > data.depth
               && data.depth == 0) {
      *output << "    <td><span class=\"toggle collapse\"></span>" << tracker_id << "</td>";
    } else {
      *output << "    <td>" << tracker_id << "</td>";
    }

    // UpdateConsumption returns true if consumption is taken from external source,
    // for instance tcmalloc stats. So we should show only it in this case.
    if (!data.consumption_excluded_from_ancestors || data.tracker->UpdateConsumption()) {
      *output << Format("<td>$0</td>", current_consumption_str);
    } else {
      auto full_consumption_str = HumanReadableNumBytes::ToString(
          tracker->consumption() + data.consumption_excluded_from_ancestors);
      *output << Format("<td>$0 ($1)</td>", current_consumption_str, full_consumption_str);
    }
    *output << Format("<td>$0</td><td>$1</td>\n", peak_consumption_str, limit_str);
    *output << "  </tr>\n";
  }

  *output << "</table>\n";
}

// Registered to handle "/mem-trackers", and prints out to handle memory tracker information.
static void MemTrackersHandler(const Webserver::WebRequest& req,
                               Webserver::WebResponse* resp,
                               bool isJson) {
  std::stringstream *output = &resp->output;

  int max_depth = INT_MAX;
  string depth = FindWithDefault(req.parsed_args, "max_depth", "");
  if (!depth.empty()) {
    max_depth = std::stoi(depth);
  }
  string full_path_arg = FindWithDefault(req.parsed_args, "show_full_path", "true");
  bool use_full_path = ParseLeadingBoolValue(full_path_arg.c_str(), true);

  std::vector<MemTrackerData> trackers;
  CollectMemTrackerData(MemTracker::GetRootTracker(), 0, &trackers);

  if (isJson) {
    JsonOutputMemTrackers(trackers, output, max_depth, use_full_path);
  } else {
    HtmlOutputMemTrackers(trackers, output, max_depth, use_full_path);
  }
}

static Result<MetricLevel> MetricLevelFromName(const std::string& level) {
  if (level == "debug") {
    return MetricLevel::kDebug;
  } else if (level == "info") {
    return MetricLevel::kInfo;
  } else if (level == "warn") {
    return MetricLevel::kWarn;
  }
  return STATUS(NotSupported, Substitute("Unknown Metric Level $0", level));
}

template<class Value>
void SetParsedValue(Value* v, const Result<Value>& result) {
  if (result.ok()) {
    *v = *result;
  } else {
    LOG(WARNING) << "Can't parse option: " << result.status();
  }
}

static void ParseRequestOptions(const Webserver::WebRequest& req,
                                MetricPrometheusOptions *prometheus_opts,
                                MetricJsonOptions *json_opts = nullptr,
                                JsonWriter::Mode *json_mode = nullptr) {
  auto ParseMetricOptions = [](const Webserver::WebRequest& req,
                               MetricOptions *metric_opts) {
    if (const string* metrics_p = FindOrNull(req.parsed_args, "metrics")) {
      metric_opts->general_metrics_allowlist = SplitStringUsing(*metrics_p, ",");
    }

    string arg = FindWithDefault(req.parsed_args, "reset_histograms", "true");
    metric_opts->reset_histograms = ParseLeadingBoolValue(arg.c_str(), true);

    arg = FindWithDefault(req.parsed_args, "level", "debug");
    SetParsedValue(&metric_opts->level, MetricLevelFromName(arg));
  };

  string arg;
  if (json_opts) {
    ParseMetricOptions(req, json_opts);

    arg = FindWithDefault(req.parsed_args, "include_raw_histograms", "false");
    json_opts->include_raw_histograms = ParseLeadingBoolValue(arg.c_str(), false);

    arg = FindWithDefault(req.parsed_args, "include_schema", "false");
    json_opts->include_schema_info = ParseLeadingBoolValue(arg.c_str(), false);
  }

  if (prometheus_opts) {
    ParseMetricOptions(req, prometheus_opts);

    if (const std::string* arg_p = FindOrNull(req.parsed_args, "show_help")) {
      prometheus_opts->export_help_and_type =
          ExportHelpAndType(ParseLeadingBoolValue(arg_p->c_str(), false));
    }

    if (const std::string* arg_p = FindOrNull(req.parsed_args, "max_metric_entries")) {
        try {
          if (arg_p->starts_with('-')) {
            throw std::invalid_argument("Input value is negative");
          }
          prometheus_opts->max_metric_entries = static_cast<uint32_t>(std::stoul(*arg_p));
        } catch (const std::exception& e) {
          LOG(WARNING) << "Prometheus metric endpoint URL parameter max_metric_entries=" << *arg_p
                       << ". Failed to convert its value to unsigned 32 bits integer: "
                       << e.what();
        }
    }

    prometheus_opts->version = FindWithDefault(req.parsed_args, "version",
        kFilterVersionOne);

    if (prometheus_opts->version == kFilterVersionTwo) {
      // Set it to accept all metrics, because we ignore metrics URL parameter when using v2.
      prometheus_opts->general_metrics_allowlist = std::nullopt;

      auto FindHandlingAllOrNone = [&](
          const std::string& arg, const std::string& default_value) -> std::string {
        std::string regex_string = FindWithDefault(req.parsed_args, arg, default_value);
        if (regex_string == "ALL") {
          return ".*";
        } else if (regex_string == "NONE") {
          return "";
        }
        return regex_string;
      };

      prometheus_opts->table_allowlist_string = FindHandlingAllOrNone("table_allowlist", "ALL");

      prometheus_opts->table_blocklist_string = FindHandlingAllOrNone("table_blocklist", "NONE");

      prometheus_opts->server_allowlist_string = FindHandlingAllOrNone("server_allowlist", "ALL");

      prometheus_opts->server_blocklist_string = FindHandlingAllOrNone("server_blocklist", "NONE");
    } else {
      prometheus_opts->priority_regex_string = FindWithDefault(
          req.parsed_args, "priority_regex", ".*");
      LOG_IF(WARNING, prometheus_opts->version != kFilterVersionOne)
          << "Prometheus endpoint URL parameter version=" << prometheus_opts->version
          << " is not recognized. Only v1 or v2 can be accepted.";
    }
  }

  if (json_mode) {
    arg = FindWithDefault(req.parsed_args, "compact", "false");
    *json_mode =
        ParseLeadingBoolValue(arg.c_str(), false) ? JsonWriter::COMPACT : JsonWriter::PRETTY;
  }
}

static void WriteMetricsAsJson(const MetricRegistry* const metrics,
                               const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  MetricJsonOptions opts;
  JsonWriter::Mode json_mode;
  ParseRequestOptions(req, /* prometheus opts */ nullptr, &opts, &json_mode);
  std::stringstream* output = &resp->output;
  JsonWriter writer(output, json_mode);

  WARN_NOT_OK(metrics->WriteAsJson(&writer, opts), "Couldn't write JSON metrics over HTTP");
}

static void WriteMetricsForPrometheus(const MetricRegistry* const metrics,
                                      const Webserver::WebRequest& req,
                                      Webserver::WebResponse* resp) {
  MetricPrometheusOptions opts;
  opts.export_help_and_type =
      ExportHelpAndType(GetAtomicFlag(&FLAGS_export_help_and_type_in_prometheus_metrics));
  opts.max_metric_entries = GetAtomicFlag(&FLAGS_max_prometheus_metric_entries);
  ParseRequestOptions(req, &opts);

  std::stringstream* output = &resp->output;

  std::set<std::string> prototypes;
  metrics->get_all_prototypes(prototypes);

  PrometheusWriter writer(output, opts);
  WARN_NOT_OK(metrics->WriteForPrometheus(&writer, opts),
      "Couldn't write text metrics for Prometheus");
}

static void HandleGetVersionInfo(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;

  VersionInfoPB version_info;
  VersionInfo::GetVersionInfoPB(&version_info);

  JsonWriter jw(output, JsonWriter::COMPACT);
  jw.StartObject();

  jw.String("build_id");
  jw.String(version_info.build_id());
  jw.String("build_type");
  jw.String(version_info.build_type());
  jw.String("build_number");
  jw.String(version_info.build_number());
  jw.String("build_timestamp");
  jw.String(version_info.build_timestamp());
  jw.String("build_username");
  jw.String(version_info.build_username());
  jw.String("version_number");
  jw.String(version_info.version_number());
  jw.String("build_hostname");
  jw.String(version_info.build_hostname());
  jw.String("git_revision");
  jw.String(version_info.git_hash());

  jw.EndObject();
}

template<typename WeightFormatter = std::identity>
static void StackTraceTrackerHandler(
    const Webserver::WebRequest& req,
    Webserver::WebResponse* resp,
    std::string_view title,
    const std::unordered_map<StackTraceTrackingGroup, std::string>& groups,
    std::string_view weight_header,
    WeightFormatter format_weight = {}) {
  std::stringstream& output = resp->output;

  if (!GetAtomicFlag(&FLAGS_track_stack_traces)) {
    output << "track_stack_traces must be turned on to use this page.";
    return;
  }

  Tags tags(false /* as_text */);
  HtmlPrintHelper html_print_helper(output);

  auto traces = GetTrackedStackTraces();
  std::sort(traces.begin(), traces.end(),
            [](const auto& left, const auto& right) { return left.weight > right.weight; });

  output << tags.header << title << tags.end_header;

  auto tracking_start = GetLastStackTraceTrackerResetTime();
  auto tracking_end = MonoTime::Now();
  output << "Tracking Period: "
         << tracking_start.ToFormattedString() << " to " << tracking_end.ToFormattedString()
         << " (" << (tracking_end - tracking_start).ToString() << ")" << tags.line_break;
  output << "<a href=\"/reset-stack-traces\">Reset Tracking</a>" << tags.line_break;

  std::vector<std::string> column_names;
  if (groups.size() > 1) {
    column_names.emplace_back("Type");
  }
  column_names.emplace_back("Count");
  column_names.emplace_back(weight_header);
  column_names.emplace_back("Stack Trace");

  auto stack_traces = html_print_helper.CreateTablePrinter("stack_traces", column_names);

  for (const auto& entry : traces) {
    auto group_itr = groups.find(entry.group);
    if (entry.count == 0 || group_itr == groups.end()) {
      continue;
    }

    auto count = entry.count;
    auto weight = format_weight(entry.weight);
    auto stack = tags.pre_tag + EscapeForHtmlToString(entry.symbolized_trace) + tags.end_pre_tag;

    if (groups.size() > 1) {
      stack_traces.AddRow(group_itr->second, count, weight, stack);
    } else {
      stack_traces.AddRow(count, weight, stack);
    }
  }

  stack_traces.Print();
}

static void IOStackTraceHandler(const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  StackTraceTrackerHandler(
      req, resp, "I/O stack traces",
      {{StackTraceTrackingGroup::kReadIO, "Read"}, {StackTraceTrackingGroup::kWriteIO, "Write"}},
      "Bytes", &HumanReadableNumBytes::ToString);
}

static void DebugStackTraceHandler(const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  StackTraceTrackerHandler(
      req, resp, "Tracked stack traces",
      {{StackTraceTrackingGroup::kDebugging, "Debugging"}},
      "Weight");
}

static void ResetStackTraceHandler(const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  ResetTrackedStackTraces();

  Tags tags(false /* as_text */);
  resp->output << "Tracked stack traces reset." << tags.line_break
               << "<a href=\"javascript:window.location=document.referrer\">Back</a>";
}

} // anonymous namespace

// Registered to handle "/memz", and prints out memory allocation statistics.
void MemUsageHandler(const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  bool as_text = (req.parsed_args.find("raw") != req.parsed_args.end());
  Tags tags(as_text);

  (*output) << tags.pre_tag;
#ifndef YB_TCMALLOC_ENABLED
  (*output) << "Memory tracking is not available unless tcmalloc is enabled.";
#else
  auto tmp = TcMallocStats();
  if (!as_text) {
    tmp = EscapeForHtmlToString(tmp);
  }
  // Replace new lines with <br> for html.
  replace_all(tmp, "\n", tags.line_break);
  (*output) << tmp << tags.end_pre_tag;
#endif
}

void AddDefaultPathHandlers(Webserver* webserver) {
  webserver->RegisterPathHandler("/logs", "Logs", LogsHandler, true, false);
  webserver->RegisterPathHandler(
      "/varz", "Flags", std::bind(&FlagsHandler, _1, _2, webserver), true, false);
  webserver->RegisterPathHandler("/status", "Status", StatusHandler, false, false);
  webserver->RegisterPathHandler("/memz", "Memory (total)", MemUsageHandler, true, false);
  webserver->RegisterPathHandler("/mem-trackers", "Memory (detail)",
                                 std::bind(&MemTrackersHandler, _1, _2, false /* isJson */),
                                 true, false);
  webserver->RegisterPathHandler("/api/v1/mem-trackers", "Memory (detail) JSON",
                                 std::bind(&MemTrackersHandler, _1, _2, true /* isJson */),
                                 false, false);
  webserver->RegisterPathHandler(
      "/api/v1/varz", "Flags", std::bind(&GetFlagsJsonHandler, _1, _2, webserver), false, false);
  webserver->RegisterPathHandler("/api/v1/version-info", "Build Version Info",
                                 HandleGetVersionInfo, false, false);
  webserver->RegisterPathHandler("/io-stack-traces", "I/O Stack Traces",
                                 IOStackTraceHandler, true, false);
  webserver->RegisterPathHandler("/debug-stack-traces", "Debugging Stack Traces",
                                 DebugStackTraceHandler, true, false);
  webserver->RegisterPathHandler("/reset-stack-traces", "Reset Stack Traces",
                                 ResetStackTraceHandler, true, false);

  AddPprofPathHandlers(webserver);
}

void RegisterMetricsJsonHandler(Webserver* webserver, const MetricRegistry* const metrics) {
  Webserver::PathHandlerCallback callback = std::bind(WriteMetricsAsJson, metrics, _1, _2);
  Webserver::PathHandlerCallback prometheus_callback = std::bind(
      WriteMetricsForPrometheus, metrics, _1, _2);
  bool not_styled = false;
  bool not_on_nav_bar = false;
  webserver->RegisterPathHandler("/metrics", "Metrics", callback, not_styled, not_on_nav_bar);

  // The old name -- this is preserved for compatibility with older releases of
  // monitoring software which expects the old name.
  webserver->RegisterPathHandler("/jsonmetricz", "Metrics", callback, not_styled, not_on_nav_bar);

  webserver->RegisterPathHandler(
      "/prometheus-metrics", "Metrics", prometheus_callback, not_styled, not_on_nav_bar);
}

// Registered to handle "/drives", and prints out paths usage
static void PathUsageHandler(FsManager* fsmanager,
                             const Webserver::WebRequest& req,
                             Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  *output << "<h1>Drives usage by subsystem</h1>\n";
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Path</th><th>Used Space</th>"
      "<th>Total Space</th></tr>\n";

  Env* env = fsmanager->env();
  for (const auto& path : fsmanager->GetFsRootDirs()) {
    const auto stats = env->GetFilesystemStatsBytes(path);
    if (!stats.ok()) {
      LOG(WARNING) << stats.status();
      *output << Format("  <tr><td>$0</td><td colspan=\"2\">$1</td></tr>\n",
                        EscapeForHtmlToString(path),
                        EscapeForHtmlToString(stats.status().message().ToString()));
      continue;
    }
    const std::string used_space_str = HumanReadableNumBytes::ToString(stats->used_space);
    const std::string total_space_str = HumanReadableNumBytes::ToString(stats->total_space);
    *output << Format("  <tr><td>$0</td><td>$1</td><td>$2</td></tr>\n",
                      EscapeForHtmlToString(path), used_space_str, total_space_str);
  }
  *output << "</table>\n";
}

void RegisterPathUsageHandler(Webserver* webserver, FsManager* fsmanager) {
  Webserver::PathHandlerCallback callback = std::bind(PathUsageHandler, fsmanager, _1, _2);
  webserver->RegisterPathHandler("/drives", "Drives", callback, true, false);
}

// Registered to handle "/tls", and prints out certificate details
static void CertificateHandler(server::RpcServerBase* server,
                             const Webserver::WebRequest& req,
                             Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  bool as_text = (req.parsed_args.find("raw") != req.parsed_args.end());
  Tags tags(as_text);
  (*output) << tags.header << "TLS Settings" << tags.end_header << endl;

  (*output) << tags.pre_tag;

  (*output) << "Node to node encryption enabled: "
      << (yb::rpc::IsNodeToNodeEncryptionEnabled() ? "true" : "false");

  (*output) << tags.line_break << "Client to server encryption enabled: "
      << (yb::rpc::IsClientToServerEncryptionEnabled() ? "true" : "false");

  (*output) << tags.line_break << "Allow insecure connections: "
      << (yb::rpc::AllowInsecureConnections() ? "on" : "off");

  (*output) << tags.line_break << "SSL Protocols: " << yb::rpc::GetSSLProtocols();

  (*output) << tags.line_break << "Cipher list: " << yb::rpc::GetCipherList();

  (*output) << tags.line_break << "Ciphersuites: " << yb::rpc::GetCipherSuites();

  (*output) << tags.end_pre_tag;

  auto details = server->GetCertificateDetails();

  if(!details.empty()) {
    (*output) << tags.header << "Certificate details" << tags.end_header << endl;

    (*output) << tags.pre_tag << EscapeForHtmlToString(details) << tags.end_pre_tag << endl;
  }
}

void RegisterTlsHandler(Webserver* webserver, server::RpcServerBase* server) {
  Webserver::PathHandlerCallback callback = std::bind(CertificateHandler, server, _1, _2);
  webserver->RegisterPathHandler("/tls", "TLS", callback,
    true /*is_styled*/, false /*is_on_nav_bar*/);
}

} // namespace yb
