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

#include "yb/server/pprof-path-handlers.h"

#if YB_GPERFTOOLS_TCMALLOC
#include <gperftools/heap-profiler.h>
#include <gperftools/malloc_extension.h>
#include <gperftools/profiler.h>
#endif  // YB_GPERFTOOLS_TCMALLOC

#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

#include "yb/util/logging.h"

#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/numbers.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/stringpiece.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/sysinfo.h"

#include "yb/server/webserver.h"
#include "yb/server/html_print_helper.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/env.h"
#include "yb/util/monotime.h"
#include "yb/util/size_literals.h"
#include "yb/util/spinlock_profiling.h"
#include "yb/util/status_log.h"
#include "yb/util/status.h"
#include "yb/util/symbolize.h"
#include "yb/util/tcmalloc_profile.h"
#include "yb/util/tcmalloc_util.h"
#include "yb/util/url-coding.h"

DECLARE_bool(enable_process_lifetime_heap_profiling);
DECLARE_string(heap_profile_path);
DECLARE_string(tmp_dir);

using std::endl;
using std::ifstream;
using std::ostringstream;
using std::string;
using std::stringstream;
using std::vector;

namespace yb {

const int PPROF_DEFAULT_SAMPLE_SECS = 30; // pprof default sample time in seconds.

// pprof asks for the url /pprof/cmdline to figure out what application it's profiling.
// The server should respond by sending the executable path.
static void PprofCmdLineHandler(const Webserver::WebRequest& req,
                                      Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  string executable_path;
  Env* env = Env::Default();
  WARN_NOT_OK(env->GetExecutablePath(&executable_path), "Failed to get executable path");
  *output << executable_path;
}

SampleOrder ParseSampleOrder(const Webserver::ArgumentMap& parsed_args) {
  auto order = FindWithDefault(parsed_args, "order_by", "");
  if (order == "bytes") {
    return SampleOrder::kSampledBytes;
  } else if (order == "estimated_bytes") {
    return SampleOrder::kEstimatedBytes;
  }
  return SampleOrder::kSampledCount;
}

namespace {
std::string FormatNumericTableRow(const std::string& value) {
  return Format("<td align=\"right\">$0</td>", value);
}

std::string FormatNumericTableRow(int64_t value) {
  return FormatNumericTableRow(SimpleItoaWithCommas(value));
}

std::string FormatNumericTableRow(std::optional<int64_t> value) {
  if (value) {
    return FormatNumericTableRow(*value);
  }
  return FormatNumericTableRow("N/A");
}

void GenerateTable(std::stringstream* output, const std::vector<Sample>& samples,
    const std::string& title, size_t max_call_stacks, SampleOrder order) {
  // Generate the output table.
  (*output) << std::fixed;
  (*output) << std::setprecision(2);
  (*output) << Format("<b>Top $0 call stacks for:</b> $1<br>\n", max_call_stacks, title);
  if (samples.size() > max_call_stacks) {
    (*output) << Format("$0 call stacks omitted<br>\n", samples.size() - max_call_stacks);
  }
  (*output) << Format("<b>Ordering call stacks by:</b> $0<br>\n", order);
  (*output) << Format(
      "<b>Current sampling frequency:</b> $0 bytes (on average)<br>\n",
      GetTCMallocSamplingPeriod());
  (*output) << Format("Values shown below are for allocations still in use "
      "(i.e., objects that have been deallocated are not included)<br>\n");
  (*output) << "<p>\n";
  (*output) << "<table style=\"border-collapse: collapse\" border=1>\n";
  (*output) << "<style>td, th { padding: 5px; }</style>";
  (*output) << "<tr>\n";
  (*output) << "<th>Estimated Bytes</th>\n";
  (*output) << "<th>Estimated Count</th>\n";
  (*output) << "<th>Avg Bytes Per Allocation</th>\n";
  (*output) << "<th>Sampled Bytes</th>\n";
  (*output) << "<th>Sampled Count</th>\n";
  (*output) << "<th>Call Stack</th>\n";
  (*output) << "</tr>\n";

  for (size_t i = 0; i < std::min(max_call_stacks, samples.size()); ++i) {
    const auto& entry = samples.at(i);
    (*output) << "<tr>";

    (*output) << FormatNumericTableRow(entry.second.estimated_bytes);

    (*output) << FormatNumericTableRow(entry.second.estimated_count);

    std::optional<int64_t> avg_bytes;
    if (entry.second.sampled_count > 0) {
      avg_bytes = std::round(
          static_cast<double>(entry.second.sampled_allocated_bytes) / entry.second.sampled_count);
    }
    (*output) << FormatNumericTableRow(avg_bytes);

    (*output) << FormatNumericTableRow(entry.second.sampled_allocated_bytes);

    (*output) << FormatNumericTableRow(entry.second.sampled_count);

    // Call stack.
    (*output) << Format("<td><pre>$0</pre></td>", EscapeForHtmlToString(entry.first));

    (*output) << "</tr>";
  }
  (*output) << "</table>";
}
} // namespace

// pprof asks for the url /pprof/heap to get heap information. This should be implemented
// by calling HeapProfileStart(filename), continue to do work, and then, some number of
// seconds later, call GetHeapProfile() followed by HeapProfilerStop().
static void PprofHeapHandler(const Webserver::WebRequest& req,
                              Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
#if !YB_TCMALLOC_ENABLED
  (*output) << "Heap profiling is only available if tcmalloc is enabled.";
  return;
#else
  int seconds = ParseLeadingInt32Value(
      FindWithDefault(req.parsed_args, "seconds", ""), PPROF_DEFAULT_SAMPLE_SECS);

#if YB_GOOGLE_TCMALLOC
  // Whether to only report allocations that do not have a corresponding deallocation.
  bool only_growth = ParseLeadingBoolValue(
      FindWithDefault(req.parsed_args, "only_growth", ""), false);
  SampleFilter filter = only_growth ? SampleFilter::kGrowthOnly : SampleFilter::kAllSamples;

  SampleOrder order = ParseSampleOrder(req.parsed_args);

  // Set the sample frequency to this value for the duration of the sample.
  int64_t sample_freq_bytes = ParseLeadingInt64Value(
      FindWithDefault(req.parsed_args, "sample_freq_bytes", ""), 4_KB);
  LOG(INFO) << "Starting a heap profile:"
            << " seconds=" << seconds
            << " only_growth=" << only_growth
            << " sample_freq_bytes=" << sample_freq_bytes;

  const string title = only_growth ? "In use profile" : "Allocation profile";

  Result<tcmalloc::Profile> profile = GetHeapProfile(seconds, sample_freq_bytes);
  if (!profile.ok()) {
    (*output) << profile.status().message();
    return;
  }
  auto samples = AggregateAndSortProfile(*profile, filter, order);
  GenerateTable(output, samples, title, 1000 /* max_call_stacks */, order);
#endif  // YB_GOOGLE_TCMALLOC

#if YB_GPERFTOOLS_TCMALLOC
  // Remote (on-demand) profiling is disabled for gperftools tcmalloc if the process is already
  // being profiled.
  if (FLAGS_enable_process_lifetime_heap_profiling) {
    (*output) << "Heap profiling is running for the process lifetime. Not starting on-demand "
              << "profile.";
    return;
  }

  LOG(INFO) << "Starting a heap profile:"
            << " seconds=" << seconds
            << " path prefix=" << FLAGS_heap_profile_path;

  HeapProfilerStart(FLAGS_heap_profile_path.c_str());
  // Sleep to allow for some samples to be collected.
  SleepFor(MonoDelta::FromSeconds(seconds));
  const char* profile = GetHeapProfile();
  HeapProfilerStop();
  (*output) << profile;
  delete profile;
#endif  // YB_GPERFTOOLS_TCMALLOC
#endif  // YB_TCMALLOC_ENABLED
}

void PprofHeapSnapshotHandler(const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream* output = &resp->output;
  bool peak_heap = ParseLeadingBoolValue(FindWithDefault(req.parsed_args, "peak_heap", ""), false);
  SampleOrder order = ParseSampleOrder(req.parsed_args);

  Result<vector<Sample>> sample_result = GetAggregateAndSortHeapSnapshot(
      order, peak_heap ? HeapSnapshotType::kPeakHeap : HeapSnapshotType::kCurrentHeap);
  if (!sample_result.ok()) {
    (*output) << sample_result.status().message();
    return;
  }
  vector<Sample> samples = sample_result.get();

  string title = peak_heap ? "Peak heap snapshot" : "Current heap snapshot";
  GenerateTable(output, samples, title, 1000 /* max_call_stacks */, order);
}

// pprof asks for the url /pprof/profile?seconds=XX to get cpu-profiling information.
// The server should respond by calling ProfilerStart(), continuing to do its work,
// and then, XX seconds later, calling ProfilerStop().
static void PprofCpuProfileHandler(const Webserver::WebRequest& req,
                                  Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
#if YB_GPERFTOOLS_TCMALLOC
  string secs_str = FindWithDefault(req.parsed_args, "seconds", "");
  int32_t seconds = ParseLeadingInt32Value(secs_str.c_str(), PPROF_DEFAULT_SAMPLE_SECS);
  // Build a temporary file name that is hopefully unique.
  string tmp_prof_file_name = Format("$0/yb_cpu_profile.$1.$2", FLAGS_tmp_dir, getpid(), rand());

  LOG(INFO) << "Starting a cpu profile:"
            << " profiler file name=" << tmp_prof_file_name
            << " seconds=" << seconds;

  ProfilerStart(tmp_prof_file_name.c_str());
  SleepFor(MonoDelta::FromSeconds(seconds));
  ProfilerStop();
  ifstream prof_file(tmp_prof_file_name.c_str(), std::ios::in);
  if (!prof_file.is_open()) {
    (*output) << "Unable to open cpu profile: " << tmp_prof_file_name;
    return;
  }
  (*output) << prof_file.rdbuf();
  prof_file.close();
#else
  (*output) << "CPU profiling is only available with gperftools tcmalloc.";
#endif  // YB_GPERFTOOLS_TCMALLOC
}

// pprof asks for the url /pprof/growth to get heap-profiling delta (growth) information.
// The server should respond by calling:
// MallocExtension::instance()->GetHeapGrowthStacks(&output);
static void PprofGrowthHandler(const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
#if YB_GPERFTOOLS_TCMALLOC
  string heap_growth_stack;
  MallocExtension::instance()->GetHeapGrowthStacks(&heap_growth_stack);
  (*output) << heap_growth_stack;
#else
  (*output) << "Growth profiling is only available with gperftools tcmalloc.";
#endif  // YB_GPERFTOOLS_TCMALLOC
}

// Lock contention profiling
static void PprofContentionHandler(const Webserver::WebRequest& req,
                                    Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  string secs_str = FindWithDefault(req.parsed_args, "seconds", "");
  int32_t seconds = ParseLeadingInt32Value(secs_str.c_str(), PPROF_DEFAULT_SAMPLE_SECS);
  int64_t discarded_samples = 0;

  *output << "--- contention" << endl;
  *output << "sampling period = 1" << endl;
  *output << "cycles/second = " << base::CyclesPerSecond() << endl;

  StartSynchronizationProfiling();
  SleepFor(MonoDelta::FromSeconds(seconds));
  StopSynchronizationProfiling();
  FlushSynchronizationProfile(output, &discarded_samples);

  // pprof itself ignores this value, but we can at least look at it in the textual
  // output.
  *output << "Discarded samples = " << discarded_samples << std::endl;

#if defined(__linux__)
  // procfs only exists on Linux.
  faststring maps;
  WARN_NOT_OK(ReadFileToString(Env::Default(), "/proc/self/maps", &maps),
              "Failed to read /proc/self/maps");
  *output << maps.ToString();
#endif // defined(__linux__)
}


// pprof asks for the url /pprof/symbol to map from hex addresses to variable names.
// When the server receives a GET request for /pprof/symbol, it should return a line
// formatted like: num_symbols: ###
// where ### is the number of symbols found in the binary. For now, the only important
// distinction is whether the value is 0, which it is for executables that lack debug
// information, or not-0).
//
// In addition to the GET request for this url, the server must accept POST requests.
// This means that after the HTTP headers, pprof will pass in a list of hex addresses
// connected by +, like:
//   curl -d '0x0824d061+0x0824d1cf' http://remote_host:80/pprof/symbol
// The server should read the POST data, which will be in one line, and for each hex value
// should write one line of output to the output stream, like so:
// <hex address><tab><function name>
// For instance:
// 0x08b2dabd    _Update
static void PprofSymbolHandler(const Webserver::WebRequest& req,
                                Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  if (req.request_method == "GET") {
    // Per the above comment, pprof doesn't expect to know the actual number of symbols.
    // Any non-zero value indicates that we support symbol lookup.
    (*output) << "num_symbols: 1";
    return;
  }

  int missing_symbols = 0;
  int invalid_addrs = 0;

  // Symbolization request.
  vector<GStringPiece> pieces = strings::Split(req.post_data, "+");
  for (GStringPiece p : pieces) {
    string hex_addr;
    if (!TryStripPrefixString(p, "0x", &hex_addr)) {
      invalid_addrs++;
      continue;
    }
    uint64_t addr;
    if (!safe_strtou64_base(hex_addr.c_str(), &addr, 16)) {
      invalid_addrs++;
      continue;
    }
    char symbol_buf[1024];
    if (GlogSymbolize(reinterpret_cast<void*>(addr), symbol_buf, sizeof(symbol_buf))) {
      *output << p << "\t" << symbol_buf << std::endl;
    } else {
      missing_symbols++;
    }
  }

  LOG(INFO) << strings::Substitute(
      "Handled request for /pprof/symbol: requested=$0 invalid_addrs=$1 missing=$2",
      pieces.size(), invalid_addrs, missing_symbols);
}

static void PprofCallsiteProfileHandler(
    const Webserver::WebRequest& req,
    Webserver::WebResponse* resp) {
  HtmlPrintHelper html_print_helper(resp->output);

  bool reset = ParseLeadingBoolValue(FindWithDefault(req.parsed_args, "reset", ""), false);
  if (req.request_method == "GET") {
    if (reset) {
      ResetCallsiteProfile();
    }
    auto profile = GetCallsiteProfile();

    // Sort by reverse count. The user can sort by other fields by clicking header columns.
    sort(profile.begin(), profile.end(),
         [](const auto& a, const auto& b) {
           return a.count > b.count;
         });

    auto timing_stats = html_print_helper.CreateTablePrinter(
        "timing_stats",
          {"File",
          "Line number",
          "Function",
          "Code",
          "Count",
          "Cycles",
          "Average Cycles",
          "Microseconds",
          "Avg Microseconds"});

    for (const auto& entry : profile) {
      timing_stats.AddRow(
          EscapeForHtmlToString(entry.file_path), entry.line_number,
          EscapeForHtmlToString(entry.function_name), EscapeForHtmlToString(entry.code_line),
          entry.count, entry.total_cycles, StringPrintf("%.3f", entry.avg_cycles), entry.total_usec,
          StringPrintf("%.3f", entry.avg_usec));
    }
    timing_stats.Print();
  }
}

void AddPprofPathHandlers(Webserver* webserver) {
  // Path handlers for remote pprof profiling. For information see:
  // https://gperftools.googlecode.com/svn/trunk/doc/pprof_remote_servers.html
  webserver->RegisterPathHandler("/pprof/cmdline", "", PprofCmdLineHandler, false /* is_styled */,
      false /* is_on_nav_bar */);

#if YB_GOOGLE_TCMALLOC
  bool is_pprof_heap_styled = true;
#else
  bool is_pprof_heap_styled = false;
#endif  // YB_GOOGLE_TCMALLOC
  webserver->RegisterPathHandler("/pprof/heap", "", PprofHeapHandler, is_pprof_heap_styled,
      false /* is_on_nav_bar */);

  webserver->RegisterPathHandler("/pprof/growth", "", PprofGrowthHandler, false /* is_styled */,
      false /* is_on_nav_bar */);
  webserver->RegisterPathHandler("/pprof/profile", "", PprofCpuProfileHandler,
      false /* is_styled */, false /* is_on_nav_bar */);
  webserver->RegisterPathHandler("/pprof/symbol", "", PprofSymbolHandler, false /* is_styled */,
      false /* is_on_nav_bar */);
  webserver->RegisterPathHandler("/pprof/contention", "", PprofContentionHandler,
      false /* is_styled */, false /* is_on_nav_bar */);
  webserver->RegisterPathHandler("/pprof/heap_snapshot", "", PprofHeapSnapshotHandler,
      true /* is_styled */, false /* is_on_nav_bar */);
  webserver->RegisterPathHandler("/pprof/callsite_profile", "", PprofCallsiteProfileHandler,
      true /* is_styled */, false /* is_on_nav_bar */);
}

} // namespace yb
