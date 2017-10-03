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

#include "kudu/util/flags.h"

#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include <gflags/gflags.h>
#include <gperftools/heap-profiler.h>

#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/metrics.h"
#include "kudu/util/path_util.h"
#include "kudu/util/url-coding.h"
#include "kudu/util/version_info.h"

using google::CommandLineFlagInfo;
using std::cout;
using std::endl;
using std::string;
using std::unordered_set;

// Because every binary initializes its flags here, we use it as a convenient place
// to offer some global flags as well.
DEFINE_bool(dump_metrics_json, false,
            "Dump a JSON document describing all of the metrics which may be emitted "
            "by this binary.");
TAG_FLAG(dump_metrics_json, hidden);

DEFINE_bool(enable_process_lifetime_heap_profiling, false, "Enables heap "
    "profiling for the lifetime of the process. Profile output will be stored in the "
    "directory specified by -heap_profile_path. Enabling this option will disable the "
    "on-demand/remote server profile handlers.");
TAG_FLAG(enable_process_lifetime_heap_profiling, stable);
TAG_FLAG(enable_process_lifetime_heap_profiling, advanced);

DEFINE_string(heap_profile_path, "", "Output path to store heap profiles. If not set " \
    "profiles are stored in /tmp/<process-name>.<pid>.<n>.heap.");
TAG_FLAG(heap_profile_path, stable);
TAG_FLAG(heap_profile_path, advanced);

// Tag a bunch of the flags that we inherit from glog/gflags.

//------------------------------------------------------------
// GLog flags
//------------------------------------------------------------
// Most of these are considered stable. The ones related to email are
// marked unsafe because sending email inline from a server is a pretty
// bad idea.
DECLARE_string(alsologtoemail);
TAG_FLAG(alsologtoemail, hidden);
TAG_FLAG(alsologtoemail, unsafe);

// --alsologtostderr is deprecated in favor of --stderrthreshold
DECLARE_bool(alsologtostderr);
TAG_FLAG(alsologtostderr, hidden);
TAG_FLAG(alsologtostderr, runtime);

DECLARE_bool(colorlogtostderr);
TAG_FLAG(colorlogtostderr, stable);
TAG_FLAG(colorlogtostderr, runtime);

DECLARE_bool(drop_log_memory);
TAG_FLAG(drop_log_memory, advanced);
TAG_FLAG(drop_log_memory, runtime);

DECLARE_string(log_backtrace_at);
TAG_FLAG(log_backtrace_at, advanced);

DECLARE_string(log_dir);
TAG_FLAG(log_dir, stable);

DECLARE_string(log_link);
TAG_FLAG(log_link, stable);
TAG_FLAG(log_link, advanced);

DECLARE_bool(log_prefix);
TAG_FLAG(log_prefix, stable);
TAG_FLAG(log_prefix, advanced);
TAG_FLAG(log_prefix, runtime);

DECLARE_int32(logbuflevel);
TAG_FLAG(logbuflevel, advanced);
TAG_FLAG(logbuflevel, runtime);
DECLARE_int32(logbufsecs);
TAG_FLAG(logbufsecs, advanced);
TAG_FLAG(logbufsecs, runtime);

DECLARE_int32(logemaillevel);
TAG_FLAG(logemaillevel, hidden);
TAG_FLAG(logemaillevel, unsafe);

DECLARE_string(logmailer);
TAG_FLAG(logmailer, hidden);

DECLARE_bool(logtostderr);
TAG_FLAG(logtostderr, stable);
TAG_FLAG(logtostderr, runtime);

DECLARE_int32(max_log_size);
TAG_FLAG(max_log_size, stable);
TAG_FLAG(max_log_size, runtime);

DECLARE_int32(minloglevel);
TAG_FLAG(minloglevel, stable);
TAG_FLAG(minloglevel, advanced);
TAG_FLAG(minloglevel, runtime);

DECLARE_int32(stderrthreshold);
TAG_FLAG(stderrthreshold, stable);
TAG_FLAG(stderrthreshold, advanced);
TAG_FLAG(stderrthreshold, runtime);

DECLARE_bool(stop_logging_if_full_disk);
TAG_FLAG(stop_logging_if_full_disk, stable);
TAG_FLAG(stop_logging_if_full_disk, advanced);
TAG_FLAG(stop_logging_if_full_disk, runtime);

DECLARE_int32(v);
TAG_FLAG(v, stable);
TAG_FLAG(v, advanced);
TAG_FLAG(v, runtime);

DECLARE_string(vmodule);
TAG_FLAG(vmodule, stable);
TAG_FLAG(vmodule, advanced);

DECLARE_bool(symbolize_stacktrace);
TAG_FLAG(symbolize_stacktrace, stable);
TAG_FLAG(symbolize_stacktrace, runtime);
TAG_FLAG(symbolize_stacktrace, advanced);

//------------------------------------------------------------
// GFlags flags
//------------------------------------------------------------
DECLARE_string(flagfile);
TAG_FLAG(flagfile, stable);

DECLARE_string(fromenv);
TAG_FLAG(fromenv, stable);
TAG_FLAG(fromenv, advanced);

DECLARE_string(tryfromenv);
TAG_FLAG(tryfromenv, stable);
TAG_FLAG(tryfromenv, advanced);

DECLARE_string(undefok);
TAG_FLAG(undefok, stable);
TAG_FLAG(undefok, advanced);

DECLARE_int32(tab_completion_columns);
TAG_FLAG(tab_completion_columns, stable);
TAG_FLAG(tab_completion_columns, hidden);

DECLARE_string(tab_completion_word);
TAG_FLAG(tab_completion_word, stable);
TAG_FLAG(tab_completion_word, hidden);

DECLARE_bool(help);
TAG_FLAG(help, stable);

DECLARE_bool(helpfull);
// We hide -helpfull because it's the same as -help for now.
TAG_FLAG(helpfull, stable);
TAG_FLAG(helpfull, hidden);

DECLARE_string(helpmatch);
TAG_FLAG(helpmatch, stable);
TAG_FLAG(helpmatch, advanced);

DECLARE_string(helpon);
TAG_FLAG(helpon, stable);
TAG_FLAG(helpon, advanced);

DECLARE_bool(helppackage);
TAG_FLAG(helppackage, stable);
TAG_FLAG(helppackage, advanced);

DECLARE_bool(helpshort);
TAG_FLAG(helpshort, stable);
TAG_FLAG(helpshort, advanced);

DECLARE_bool(helpxml);
TAG_FLAG(helpxml, stable);
TAG_FLAG(helpxml, advanced);

DECLARE_bool(version);
TAG_FLAG(version, stable);

namespace kudu {
namespace {

void AppendXMLTag(const char* tag, const string& txt, string* r) {
  strings::SubstituteAndAppend(r, "<$0>$1</$0>", tag, EscapeForHtmlToString(txt));
}

static string DescribeOneFlagInXML(const CommandLineFlagInfo& flag) {
  unordered_set<string> tags;
  GetFlagTags(flag.name, &tags);

  string r("<flag>");
  AppendXMLTag("file", flag.filename, &r);
  AppendXMLTag("name", flag.name, &r);
  AppendXMLTag("meaning", flag.description, &r);
  AppendXMLTag("default", flag.default_value, &r);
  AppendXMLTag("current", flag.current_value, &r);
  AppendXMLTag("type", flag.type, &r);
  AppendXMLTag("tags", JoinStrings(tags, ","), &r);
  r += "</flag>";
  return r;
}

void DumpFlagsXML() {
  vector<CommandLineFlagInfo> flags;
  GetAllFlags(&flags);

  cout << "<?xml version=\"1.0\"?>" << endl;
  cout << "<AllFlags>" << endl;
  cout << strings::Substitute(
      "<program>$0</program>",
      EscapeForHtmlToString(BaseName(google::ProgramInvocationShortName()))) << endl;
  cout << strings::Substitute(
      "<usage>$0</usage>",
      EscapeForHtmlToString(google::ProgramUsage())) << endl;

  for (const CommandLineFlagInfo& flag : flags) {
    cout << DescribeOneFlagInXML(flag) << std::endl;
  }

  cout << "</AllFlags>" << endl;
  exit(1);
}

void ShowVersionAndExit() {
  cout << VersionInfo::GetAllVersionInfo() << endl;
  exit(0);
}

} // anonymous namespace

int ParseCommandLineFlags(int* argc, char*** argv, bool remove_flags) {
  int ret = google::ParseCommandLineNonHelpFlags(argc, argv, remove_flags);

  if (FLAGS_helpxml) {
    DumpFlagsXML();
  } else if (FLAGS_dump_metrics_json) {
    MetricPrototypeRegistry::get()->WriteAsJsonAndExit();
  } else if (FLAGS_version) {
    ShowVersionAndExit();
  } else {
    google::HandleCommandLineHelpFlags();
  }

  if (FLAGS_heap_profile_path.empty()) {
    FLAGS_heap_profile_path = strings::Substitute(
        "/tmp/$0.$1", google::ProgramInvocationShortName(), getpid());
  }

#ifdef TCMALLOC_ENABLED
  if (FLAGS_enable_process_lifetime_heap_profiling) {
    HeapProfilerStart(FLAGS_heap_profile_path.c_str());
  }
#endif

  return ret;
}

} // namespace kudu
