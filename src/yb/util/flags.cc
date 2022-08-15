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


#include <string>
#include <unordered_set>
#include <vector>

#ifdef TCMALLOC_ENABLED
#include <gperftools/heap-profiler.h>
#endif

#include <boost/algorithm/string/case_conv.hpp>
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/auto_flags_util.h"
#include "yb/util/flag_tags.h"
#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/path_util.h"
#include "yb/util/url-coding.h"
#include "yb/util/version_info.h"

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

DEFINE_int32(svc_queue_length_default, 50, "Default RPC queue length for a service");
TAG_FLAG(svc_queue_length_default, advanced);

// This provides a more accurate representation of default gFlag values for application like
// yb-master which override the hard coded values at process startup time.
DEFINE_bool(
    dump_flags_xml, false,
    "Dump a XLM document describing all of gFlags used in this binary. Differs from helpxml by "
    "displaying the current runtime value as the default instead of the hard coded values from the "
    "flag definitions. ");
TAG_FLAG(dump_flags_xml, stable);
TAG_FLAG(dump_flags_xml, advanced);

DEFINE_bool(help_auto_flag_json, false,
    "Dump a JSON document describing all of the AutoFlags available in this binary.");
TAG_FLAG(help_auto_flag_json, stable);
TAG_FLAG(help_auto_flag_json, advanced);

DECLARE_bool(TEST_promote_all_auto_flags);

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

namespace yb {
namespace {

void AppendXMLTag(const char* tag, const string& txt, string* r) {
  strings::SubstituteAndAppend(r, "<$0>$1</$0>", tag, EscapeForHtmlToString(txt));
}

YB_STRONGLY_TYPED_BOOL(OnlyDisplayDefaultFlagValue);

static string DescribeOneFlagInXML(
    const CommandLineFlagInfo& flag, OnlyDisplayDefaultFlagValue only_display_default_values) {
  unordered_set<FlagTag> tags;
  GetFlagTags(flag.name, &tags);

  if (only_display_default_values && tags.contains(FlagTag::kHidden)) {
    return {};
  }

  vector<string> tags_str;
  std::transform(tags.begin(), tags.end(), std::back_inserter(tags_str), [](const FlagTag tag) {
    // Convert "kEnum_val" to "enum_val"
    auto name = ToString(tag).erase(0, 1);
    boost::algorithm::to_lower(name);
    return name;
  });

  auto auto_flag_desc = GetAutoFlagDescription(flag.name);

  string r("<flag>");
  AppendXMLTag("file", flag.filename, &r);
  AppendXMLTag("name", flag.name, &r);
  AppendXMLTag("meaning", flag.description, &r);

  if (auto_flag_desc) {
    AppendXMLTag("class", ToString(auto_flag_desc->flag_class), &r);
    if (!only_display_default_values) {
      AppendXMLTag(
          "state", IsFlagPromoted(flag, *auto_flag_desc) ? "promoted" : "not-promoted", &r);
    }
    AppendXMLTag("initial", auto_flag_desc->initial_val, &r);
    AppendXMLTag("target", auto_flag_desc->target_val, &r);
  } else {
    if (only_display_default_values) {
      // gFlags have one hard-coded static default value in all programs that include the file
      // where it was defined. Programs that need custom defaults set the flag at runtime before the
      // call to ParseCommandLineFlags. So the current value is technically the default value used
      // by this program.
      AppendXMLTag("default", flag.current_value, &r);
    } else {
      AppendXMLTag("default", flag.default_value, &r);
    }
  }

  if (!only_display_default_values) {
    AppendXMLTag("current", flag.current_value, &r);
  }

  AppendXMLTag("type", flag.type, &r);
  AppendXMLTag("tags", JoinStrings(tags_str, ","), &r);
  r += "</flag>";
  return r;
}

namespace {
struct sort_flags_by_name {
  inline bool operator()(const CommandLineFlagInfo& flag1, const CommandLineFlagInfo& flag2) {
    const auto& a = flag1.name;
    const auto& b = flag2.name;
    for (size_t i = 0; i < a.size() && i < b.size(); i++) {
      if (std::tolower(a[i]) != std::tolower(b[i]))
        return (std::tolower(a[i]) < std::tolower(b[i]));
    }
    return a.size() < b.size();
  }
};
}  // namespace

void DumpFlagsXMLAndExit(OnlyDisplayDefaultFlagValue only_display_default_values) {
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

  std::sort(flags.begin(), flags.end(), sort_flags_by_name());

  for (const CommandLineFlagInfo& flag : flags) {
    const auto flag_info = DescribeOneFlagInXML(flag, only_display_default_values);
    if (!flag_info.empty()) {
      cout << flag_info << std::endl;
    }
  }

  cout << "</AllFlags>" << endl;
  exit(0);
}

void ShowVersionAndExit() {
  cout << VersionInfo::GetShortVersionString() << endl;
  exit(0);
}

} // anonymous namespace

int ParseCommandLineFlags(int* argc, char*** argv, bool remove_flags) {
  int ret = google::ParseCommandLineNonHelpFlags(argc, argv, remove_flags);

  if (FLAGS_TEST_promote_all_auto_flags) {
    PromoteAllAutoFlags();
  }

  if (FLAGS_helpxml) {
    DumpFlagsXMLAndExit(OnlyDisplayDefaultFlagValue::kFalse);
  } else if (FLAGS_dump_flags_xml) {
    DumpFlagsXMLAndExit(OnlyDisplayDefaultFlagValue::kTrue);
  } else if (FLAGS_help_auto_flag_json) {
    cout << AutoFlagsUtil::DumpAutoFlagsToJSON();
    exit(0);
  } else if (FLAGS_dump_metrics_json) {
    std::stringstream s;
    JsonWriter w(&s, JsonWriter::PRETTY);
    WriteRegistryAsJson(&w);
    std::cout << s.str() << std::endl;
    exit(0);
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

bool RefreshFlagsFile(const std::string& filename) {
  // prog_name is a placeholder that isn't really used by ReadFromFlags.
  // TODO: Find a better way to refresh flags from the file, ReadFromFlagsFile is going to be
  // deprecated.
  const char* prog_name = "yb";
  if (!google::ReadFromFlagsFile(filename, prog_name, false /* errors_are_fatal */)) {
    return false;
  }

  if (FLAGS_TEST_promote_all_auto_flags) {
    PromoteAllAutoFlags();
  }

  return true;
}

} // namespace yb
