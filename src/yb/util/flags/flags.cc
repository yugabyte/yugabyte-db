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

#include <fstream>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>
#include <boost/algorithm/string/replace.hpp>
#include "yb/gutil/map-util.h"
#include "yb/gutil/once.h"
#include "yb/gutil/strings/split.h"
#include "yb/util/env_util.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/string_case.h"

#if YB_GPERFTOOLS_TCMALLOC
#include <gperftools/heap-profiler.h>
#endif
#if YB_GOOGLE_TCMALLOC
#include <tcmalloc/malloc_extension.h>
#endif

#include <boost/algorithm/string/case_conv.hpp>
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/flags/auto_flags_util.h"
#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/path_util.h"
#include "yb/util/string_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/url-coding.h"
#include "yb/util/version_info.h"

using google::CommandLineFlagInfo;
using std::cout;
using std::endl;
using std::string;
using std::unordered_set;
using std::vector;
using yb::operator"" _MB;

// Because every binary initializes its flags here, we use it as a convenient place
// to offer some global flags as well.
DEFINE_NON_RUNTIME_bool(dump_metrics_json, false,
            "Dump a JSON document describing all of the metrics which may be emitted "
            "by this binary.");
TAG_FLAG(dump_metrics_json, hidden);

DEPRECATE_FLAG(int32, svc_queue_length_default, "11_2022");

// This provides a more accurate representation of default gFlag values for application like
// yb-master which override the hard coded values at process startup time.
DEFINE_NON_RUNTIME_bool(dump_flags_xml, false,
    "Dump a XLM document describing all of gFlags used in this binary. Differs from helpxml by "
    "displaying the current runtime value as the default instead of the hard coded values from the "
    "flag definitions. ");
TAG_FLAG(dump_flags_xml, stable);
TAG_FLAG(dump_flags_xml, advanced);

DEFINE_NON_RUNTIME_bool(help_auto_flag_json, false,
    "Dump a JSON document describing all of the AutoFlags available in this binary.");
TAG_FLAG(help_auto_flag_json, stable);
TAG_FLAG(help_auto_flag_json, advanced);

DEFINE_RUNTIME_string(allowed_preview_flags_csv, "",
    "CSV formatted list of Preview flag names. Flags that are tagged Preview cannot be modified "
    "unless they have been added to this list. By adding flags to this list, you acknowledge any "
    "risks associated with modifying them.");

DEFINE_NON_RUNTIME_string(tmp_dir, "/tmp",
    "Directory to store temporary files. By default, the value of '/tmp' is used.");

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
_TAG_FLAG_RUNTIME(alsologtostderr);

DECLARE_bool(colorlogtostderr);
TAG_FLAG(colorlogtostderr, stable);
_TAG_FLAG_RUNTIME(colorlogtostderr);

DECLARE_bool(drop_log_memory);
TAG_FLAG(drop_log_memory, advanced);
_TAG_FLAG_RUNTIME(drop_log_memory);

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
_TAG_FLAG_RUNTIME(log_prefix);

DECLARE_int32(logbuflevel);
TAG_FLAG(logbuflevel, advanced);
_TAG_FLAG_RUNTIME(logbuflevel);
DECLARE_int32(logbufsecs);
TAG_FLAG(logbufsecs, advanced);
_TAG_FLAG_RUNTIME(logbufsecs);

DECLARE_int32(logemaillevel);
TAG_FLAG(logemaillevel, hidden);
TAG_FLAG(logemaillevel, unsafe);

DECLARE_string(logmailer);
TAG_FLAG(logmailer, hidden);

DECLARE_bool(logtostderr);
TAG_FLAG(logtostderr, stable);
_TAG_FLAG_RUNTIME(logtostderr);

DECLARE_int32(max_log_size);
TAG_FLAG(max_log_size, stable);
_TAG_FLAG_RUNTIME(max_log_size);

DECLARE_int32(minloglevel);
TAG_FLAG(minloglevel, stable);
TAG_FLAG(minloglevel, advanced);
_TAG_FLAG_RUNTIME(minloglevel);

DECLARE_int32(stderrthreshold);
TAG_FLAG(stderrthreshold, stable);
TAG_FLAG(stderrthreshold, advanced);
_TAG_FLAG_RUNTIME(stderrthreshold);

DECLARE_bool(stop_logging_if_full_disk);
TAG_FLAG(stop_logging_if_full_disk, stable);
TAG_FLAG(stop_logging_if_full_disk, advanced);
_TAG_FLAG_RUNTIME(stop_logging_if_full_disk);

DECLARE_int32(v);
TAG_FLAG(v, stable);
TAG_FLAG(v, advanced);
_TAG_FLAG_RUNTIME(v);

DECLARE_string(vmodule);
TAG_FLAG(vmodule, stable);
_TAG_FLAG_RUNTIME(vmodule);
TAG_FLAG(vmodule, advanced);

DECLARE_bool(symbolize_stacktrace);
TAG_FLAG(symbolize_stacktrace, stable);
_TAG_FLAG_RUNTIME(symbolize_stacktrace);
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

DEFINE_NON_RUNTIME_string(dynamically_linked_exe_suffix, "",
    "Suffix to appended to executable names, such as yb-master and yb-tserver during the "
    "generation of Link Time Optimized builds.");
TAG_FLAG(dynamically_linked_exe_suffix, advanced);
TAG_FLAG(dynamically_linked_exe_suffix, hidden);
TAG_FLAG(dynamically_linked_exe_suffix, unsafe);

namespace yb {

// In LTO builds we first generate executable like yb-master and yb-tserver with a suffix
// ("-dynamic") added to their names. These executables are then optimized to produce the
// final executable without the suffix. Certain build targets like gen_flags_metadata and
// gen_auto_flags are run using the dynamic executables to generate metadata files which should
// contain final the program name.
string GetStaticProgramName() {
  auto program_name = BaseName(google::ProgramInvocationShortName());
  if (PREDICT_FALSE(
          !FLAGS_dynamically_linked_exe_suffix.empty() &&
          program_name.ends_with(FLAGS_dynamically_linked_exe_suffix))) {
    boost::replace_last(program_name, FLAGS_dynamically_linked_exe_suffix, "");
  }
  return program_name;
}

// Forward declarations.
namespace flags_internal {
Status ValidateFlagValue(const CommandLineFlagInfo& flag_info, const std::string& value);
}  // namespace flags_internal

namespace {

void AppendXMLTag(const char* tag, const string& txt, string* r) {
  strings::SubstituteAndAppend(r, "<$0>$1</$0>", tag, EscapeForHtmlToString(txt));
}

YB_STRONGLY_TYPED_BOOL(OnlyDisplayDefaultFlagValue);

static string DescribeOneFlagInXML(
    const CommandLineFlagInfo& flag, OnlyDisplayDefaultFlagValue only_display_default_values) {
  unordered_set<FlagTag> tags;
  GetFlagTags(flag.name, &tags);
  // TODO(#14400): Until we make gflags string modifications atomic, we should not be making
  // runtime changes to string flags. However, to not have to do two rounds of auditing, we continue
  // to mark flags as runtime, regardless of their data type, purely based on whether they can
  // logically be modified at runtime.
  //
  // To keep external clients oblivious to this, we strip the runtime tag here, so to external
  // metadata, tooling and Platform automation, all string flags will be treated explicitly as not
  // runtime!
  if (flag.type == "string") {
    auto runtime_it = tags.find(FlagTag::kRuntime);
    if (runtime_it != tags.end()) {
      tags.erase(runtime_it);
    }
  }

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
    AppendXMLTag("default", flag.default_value, &r);
  }

  if (!only_display_default_values) {
    AppendXMLTag("current", flag.current_value, &r);
  }

  AppendXMLTag("type", flag.type, &r);
  AppendXMLTag("tags", JoinStrings(tags_str, ","), &r);
  r += "</flag>";
  return r;
}

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

std::vector<std::string>& FlagsWithDelayedValidation() {
  static std::vector<std::string> flags;
  return flags;
}

bool& CommandLineFlagsParsed() {
  static bool parsed = false;
  return parsed;
}

Status ValidateFlagsRequiringDelayedValidation() {
  CommandLineFlagsParsed() = true;

  for (const auto& flag_name : FlagsWithDelayedValidation()) {
    auto flag_info = google::GetCommandLineFlagInfoOrDie(flag_name.c_str());
    // Flag was already set without any validation. Check if the current value is valid.
    RETURN_NOT_OK(flags_internal::ValidateFlagValue(flag_info, flag_info.current_value));
  }
  FlagsWithDelayedValidation().clear();

  return Status::OK();
}

void DumpFlagsXMLAndExit(OnlyDisplayDefaultFlagValue only_display_default_values) {
  vector<CommandLineFlagInfo> flags;
  GetAllFlags(&flags);

  cout << "<?xml version=\"1.0\"?>" << endl;
  cout << "<AllFlags>" << endl;
  cout << strings::Substitute(
              "<program>$0</program>", EscapeForHtmlToString(GetStaticProgramName()))
       << endl;
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

void DumpAutoFlagsJSONAndExit() {
  // Promote all AutoFlags to ensure the target value passes any flag validation functions. Its ok
  // if the current values change as we don't print them out.
  PromoteAllAutoFlags();

  cout << AutoFlagsUtil::DumpAutoFlagsToJSON(GetStaticProgramName());
  exit(0);
}

void SetFlagDefaultsToCurrent(const std::vector<google::CommandLineFlagInfo>& flag_infos) {
  for (const auto& flag_info : flag_infos) {
    if (!flag_info.is_default) {
      // This is not expected to fail as we are setting default to the already validated current
      // value.
      CHECK(!gflags::SetCommandLineOptionWithMode(
                 flag_info.name.c_str(),
                 flag_info.current_value.c_str(),
                 gflags::FlagSettingMode::SET_FLAGS_DEFAULT)
                 .empty());
    }
  }
}

void InvokeAllCallbacks(const std::vector<google::CommandLineFlagInfo>& flag_infos) {
  for (const auto& flag_info : flag_infos) {
    flags_callback_internal::InvokeCallbacks(flag_info.flag_ptr, flag_info.name);
  }
}

// If this is a preview flag and is being overridden to a non default value, then only allow it if
// it is in allowed_preview_flags.
// Returns true if this is not a preview flag, or if the new_value is the default.
// 'err_msg' is set any time false is returned.
bool IsPreviewFlagUpdateAllowed(
    const CommandLineFlagInfo& flag_info, const unordered_set<FlagTag>& tags,
    const std::string& new_value, const std::string& allowed_preview_flags, std::string* err_msg) {
  if (!ContainsKey(tags, FlagTag::kPreview) || new_value == flag_info.default_value) {
    return true;
  }

  const std::unordered_set<string> allowed_flags = strings::Split(allowed_preview_flags, ",");

  if (!ContainsKey(allowed_flags, flag_info.name)) {
    (*err_msg) = Format(
        "Flag '$0' protects a feature that is currently in preview. In order for it to be "
        "modified, you must acknowledge the risks by adding '$0' to the flag "
        "'allowed_preview_flags_csv'",
        flag_info.name);
    return false;
  }

  return true;
}

// Makes sure that all preview flags which have been overridden are part of the list.
// Allows non-existant flags, so that the allow list can be set in preparation for a yb upgrade.
// Returns false and sets err_msg if the check fails.
// We cannot set this as a validator for FLAGS_allowed_preview_flags_csv since gflags does not allow
// calling 'GetAllFlags' from within a flag validator function.
bool ValidateAllowedPreviewFlagsCsv(std::string* err_msg, const string& allowed_flags_csv) {
  std::vector<google::CommandLineFlagInfo> flag_infos;
  google::GetAllFlags(&flag_infos);

  for (const auto& flag : flag_infos) {
    unordered_set<FlagTag> tags;
    GetFlagTags(flag.name, &tags);
    if (!IsPreviewFlagUpdateAllowed(flag, tags, flag.current_value, allowed_flags_csv, err_msg)) {
      return false;
    }
  }

  return true;
}

// Runs special validations for flag 'allowed_preview_flags_csv', and preview flags.
bool IsFlagUpdateAllowed(
    const CommandLineFlagInfo& flag_info, const unordered_set<FlagTag>& tags,
    const std::string& new_value, std::string* err_msg) {
  if (flag_info.name == "allowed_preview_flags_csv") {
    return ValidateAllowedPreviewFlagsCsv(err_msg, new_value);
  }

  return IsPreviewFlagUpdateAllowed(
      flag_info, tags, new_value, FLAGS_allowed_preview_flags_csv, err_msg);
}

// Validates that the requested updates to vmodule can be made.
bool ValidateVmodule(const char* flag_name, const std::string& value) {
  auto requested_settings = strings::Split(value, ",");
  for (const auto& module_value : requested_settings) {
    if (module_value.empty()) {
      continue;
    }
    vector<string> kv = strings::Split(module_value, "=");
    if (kv.size() != 2 || kv[0].empty() || kv[1].empty()) {
      LOG_FLAG_VALIDATION_ERROR(flag_name, value)
          << "'" << module_value
          << "' is not valid. vmodule should be a comma list of <module_pattern>=<logging_level>";
      return false;
    }

    char* end;
    errno = 0;
    const int64 value = strtol(kv[1].c_str(), &end, 10);
    if (*end != '\0' || errno == ERANGE || value > INT_MAX || value < INT_MIN) {
      LOG_FLAG_VALIDATION_ERROR(flag_name, value)
          << "Invalid logging level '" << kv[1] << "' for module '" << kv[0]
          << "'. Only integer values between " << INT_MIN << " and " << INT_MAX << " are allowed";
      return false;
    }
  }

  return true;
}

void UpdateVmodule() {
  // glog behavior: The first time VLOG is invoked for a file it tries to find a matching pattern in
  // vmodule list. If found it links to that pattern for the rest of the program lifetime. If not
  // found it uses the default logging level from FLAGS_v and gets added to a list of files that
  // don't match any pattern. When SetVLOGLevel is called, all files in this list that match the new
  // pattern get linked to it, and all linked files will use the new logging level.
  // Since files are evaluated at the first VLOG call it is possible that files with similar pattern
  // get linked to different vmodule values if patterns are reordered or removed, which can get
  // confusing to the user. To avoid this we never remove any pattern from vmodule and always keep
  // the order of modules the same as it would have been if it was only set once. Ex: If vmodule is
  // set to "ab=1,a=1" and then changed to "b=2,ab=3" then we will set it to "ab=3,a=0,b=2" and the
  // behavior will be identical to if it was set to this value from the start.

  static std::mutex vmodule_mtx;
  static vector<std::pair<string, int>> vmodule_values GUARDED_BY(vmodule_mtx);
  std::lock_guard l(vmodule_mtx);
  // Set everything to 0
  for (auto& module_value : vmodule_values) {
    module_value.second = 0;
  }

  // Set to new requested values
  auto requested_settings = strings::Split(FLAGS_vmodule, ",");
  for (const auto& module_value : requested_settings) {
    if (module_value.empty()) {
      continue;
    }
    vector<string> kv = strings::Split(module_value, "=");

    // Values has been validated in ValidateVmodule
    const int value = static_cast<int>(strtol(kv[1].c_str(), nullptr /* end ptr */, 10));

    auto it = std::find_if(
        vmodule_values.begin(), vmodule_values.end(),
        [&](std::pair<string, int> const& elem) { return elem.first == kv[0]; });

    if (it == vmodule_values.end()) {
      vmodule_values.push_back({kv[0], value});
    } else {
      it->second = value;
    }
  }

  std::vector<string> module_values_str;
  for (auto elem : vmodule_values) {
    module_values_str.emplace_back(Format("$0=$1", elem.first, elem.second));
  }
  std::string set_vmodules = JoinStrings(module_values_str, ",");

  // Directly invoke SetCommandLineOption instead of SetFlagInternal which would result in infinite
  // recursion.
  google::SetCommandLineOption("vmodule", set_vmodules.c_str());

  // Now update previously set modules
  for (auto elem : vmodule_values) {
    google::SetVLOGLevel(elem.first.c_str(), elem.second);
  }
}

}  // anonymous namespace

void ParseCommandLineFlags(int* argc, char*** argv, bool remove_flags) {
  static GoogleOnceType once_register_vmodule_callback = GOOGLE_ONCE_INIT;
  // We cannot use DEFINE_validator and REGISTER_CALLBACK for vmodule since it is not DEFINED in any
  // yb file, and we cannot guarantee the static initialization order. Instead we register them
  // before we parse flags by which time static initialization is guaranteed to be complete.
  GoogleOnceInit(&once_register_vmodule_callback, []() {
    CHECK(google::RegisterFlagValidator(&FLAGS_vmodule, &ValidateVmodule));  // NOLINT

    flags_callback_internal::RegisterGlobalFlagUpdateCallback(
        &FLAGS_vmodule, "ValidateAndUpdateVmodule", &UpdateVmodule);
  });

  {
    std::vector<google::CommandLineFlagInfo> flag_infos;
    google::GetAllFlags(&flag_infos);

    // gFlags have one hard-coded static default value in all programs that include the file
    // where it was defined. Programs that need custom defaults set the flag at runtime before the
    // call to ParseCommandLineFlags. So the current value is technically the default value used
    // by this program.
    SetFlagDefaultsToCurrent(flag_infos);

    google::ParseCommandLineNonHelpFlags(argc, argv, remove_flags);

    // Run validation that were previously ignored due to DELAY_FLAG_VALIDATION_ON_STARTUP.
    CHECK_OK(ValidateFlagsRequiringDelayedValidation());

    string err_msg;
    if (!ValidateAllowedPreviewFlagsCsv(&err_msg, FLAGS_allowed_preview_flags_csv)) {
      LOG(FATAL) << err_msg;
      return;
    }

    InvokeAllCallbacks(flag_infos);

    // flag_infos is no longer valid as default and current values have changed.
  }

  if (FLAGS_TEST_promote_all_auto_flags) {
    PromoteAllAutoFlags();
  }

  if (FLAGS_helpxml) {
    DumpFlagsXMLAndExit(OnlyDisplayDefaultFlagValue::kFalse);
  } else if (FLAGS_dump_flags_xml) {
    DumpFlagsXMLAndExit(OnlyDisplayDefaultFlagValue::kTrue);
  } else if (FLAGS_help_auto_flag_json) {
    DumpAutoFlagsJSONAndExit();
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

  // Disallow relative path for tmp_dir.
  if (!FLAGS_tmp_dir.starts_with('/')) {
    LOG(FATAL) << "tmp_dir must be an absolute path, found value to be " << FLAGS_tmp_dir;
  }
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

namespace flags_internal {
string SetFlagInternal(
    const void* flag_ptr, const char* flag_name, const string& new_value,
    const gflags::FlagSettingMode set_mode) {
  // The gflags library sets new values of flags without synchronization.
  // TODO: patch gflags to use proper synchronization.
  ANNOTATE_IGNORE_WRITES_BEGIN();
  // Try to set the new value.
  string output_msg = google::SetCommandLineOptionWithMode(flag_name, new_value.c_str(), set_mode);
  ANNOTATE_IGNORE_WRITES_END();

  if (output_msg.empty()) {
    return output_msg;
  }

  flags_callback_internal::InvokeCallbacks(flag_ptr, flag_name);

  return output_msg;
}

Status SetFlagInternal(
    const void* flag_ptr, const char* flag_name, const std::string& new_value) {
  auto res = SetFlagInternal(flag_ptr, flag_name, new_value, google::SET_FLAGS_VALUE);
  SCHECK_FORMAT(!res.empty(), InvalidArgument, "Failed to set flag $0: $1", flag_name, new_value);
  return Status::OK();
}

Status SetFlagDefaultAndCurrentInternal(
    const void* flag_ptr, const char* flag_name, const string& value) {
  // SetCommandLineOptionWithMode returns non-empty string on success
  auto res = gflags::SetCommandLineOptionWithMode(
      flag_name, value.c_str(), gflags::FlagSettingMode::SET_FLAGS_DEFAULT);
  SCHECK_FORMAT(
      !res.empty(), InvalidArgument, "Failed to set flag $0 default to value $1", flag_name, value);

  res = SetFlagInternal(flag_ptr, flag_name, value, google::SET_FLAGS_VALUE);
  SCHECK_FORMAT(
      !res.empty(), InvalidArgument, "Failed to set flag $0 to value $1", flag_name, value);
  return Status::OK();
}

Status SetFlag(const std::string* flag_ptr, const char* flag_name, const std::string& new_value) {
  return SetFlagInternal(flag_ptr, flag_name, new_value);
}

Status SetFlagDefaultAndCurrent(
    const std::string* flag_ptr, const char* flag_name, const std::string& new_value) {
  return SetFlagDefaultAndCurrentInternal(flag_ptr, flag_name, new_value);
}

void WarnFlagDeprecated(const std::string& flagname, const std::string& date_mm_yyyy) {
  gflags::CommandLineFlagInfo info;
  if (!gflags::GetCommandLineFlagInfo(flagname.c_str(), &info)) {
    LOG(DFATAL) << "Internal error -- called WarnFlagDeprecated on undefined flag " << flagname;
    return;
  }
  if (!info.is_default) {
    LOG(WARNING) << "Found explicit setting for deprecated flag " << flagname << ". "
                 << "This flag has been deprecated since " << date_mm_yyyy << ". "
                 << "Please remove this from your configuration, as this may cause the process to "
                 << "crash in future releases.";
  }
}

static const std::string kMaskedFlagValue = "***";

bool IsFlagSensitive(const unordered_set<FlagTag>& tags) {
  return ContainsKey(tags, FlagTag::kSensitive_info);
}

bool IsFlagSensitive(const std::string& flag_name) {
  unordered_set<FlagTag> tags;
  GetFlagTags(flag_name, &tags);
  return IsFlagSensitive(tags);
}

std::string GetMaskedValueIfSensitive(
    const unordered_set<FlagTag>& tags, const std::string& value) {
  if (IsFlagSensitive(tags)) {
    return kMaskedFlagValue;
  }
  return value;
}

std::string GetMaskedValueIfSensitive(const std::string& flag_name, const std::string& value) {
  unordered_set<FlagTag> tags;
  GetFlagTags(flag_name, &tags);
  return GetMaskedValueIfSensitive(tags, value);
}

Status ValidateFlagValue(const CommandLineFlagInfo& flag_info, const std::string& value) {
  unordered_set<FlagTag> tags;
  GetFlagTags(flag_info.name, &tags);
  std::string output_msg;
  // Preview flags require extra validations.
  if (!IsFlagUpdateAllowed(flag_info, tags, value, &output_msg)) {
    return STATUS_FORMAT(InvalidArgument, output_msg);
  }

  // Clear previous errors if any.
  GetFlagValidatorSink().GetMessagesAndClear();

  std::string error_msg;
  if (google::ValidateCommandLineOption(flag_info.name.c_str(), value.c_str(), &error_msg)) {
    return Status::OK();
  }

  auto validation_msgs = GetFlagValidatorSink().GetMessagesAndClear();

  // error_msg originates from gflags, which may contain the value. Therefore, if it is
  // sensitive, mask it.
  // Ex: ERROR: failed validation of new value '1000' for flag 'ysql_conn_mgr_port'
  if (!value.empty() && IsFlagSensitive(flag_info.name)) {
    boost::replace_all(error_msg, Format("'$0'", value), Format("'$0'", kMaskedFlagValue));
  }

  return STATUS_FORMAT(
      InvalidArgument, "$0 : $1", error_msg,
      validation_msgs.empty() ? "Bad value" : JoinStrings(validation_msgs, ";"));
}

Status ValidateFlagValue(const std::string& flag_name, const std::string& value) {
  CommandLineFlagInfo flag_info;
  SCHECK_FORMAT(
      google::GetCommandLineFlagInfo(flag_name.c_str(), &flag_info), NotFound,
      "Flag '$0' does not exist", flag_name);
  return ValidateFlagValue(flag_info, value);
}

SetFlagResult SetFlag(
    const string& flag_name, const string& new_value, const SetFlagForce force, string* old_value,
    string* output_msg) {
  // Validate that the flag exists and get the current value.
  CommandLineFlagInfo flag_info;
  if (!google::GetCommandLineFlagInfo(flag_name.c_str(), &flag_info)) {
    *output_msg = "Flag does not exist";
    return SetFlagResult::NO_SUCH_FLAG;
  }
  const string& old_val = flag_info.current_value;

  // Validate that the flag is runtime-changeable.
  unordered_set<FlagTag> tags;
  GetFlagTags(flag_name, &tags);
  if (!ContainsKey(tags, FlagTag::kRuntime)) {
    if (force) {
      LOG(WARNING) << "Forcing change of non-runtime-safe flag " << flag_name;
    } else {
      *output_msg = "Flag is not safe to change at runtime";
      return SetFlagResult::NOT_SAFE;
    }
  }

  if (!IsFlagUpdateAllowed(flag_info, tags, new_value, output_msg)) {
    return SetFlagResult::BAD_VALUE;
  }

  // Clear previous errors if any.
  GetFlagValidatorSink().GetMessagesAndClear();

  string ret = flags_internal::SetFlagInternal(
      flag_info.flag_ptr, flag_name.c_str(), new_value, google::SET_FLAGS_VALUE);
  auto validation_msgs = GetFlagValidatorSink().GetMessagesAndClear();

  if (ret.empty()) {
    *output_msg = Format(
        "Unable to set flag: $0",
        validation_msgs.empty() ? "Bad value" : JoinStrings(validation_msgs, ";"));

    return SetFlagResult::BAD_VALUE;
  }

  // Callbacks might have changed the value of the flag, so retrieve current value again.
  string final_value;
  // We have already validated the flag_name with GetCommandLineFlagInfo, so this should not fail.
  CHECK(google::GetCommandLineOption(flag_name.c_str(), &final_value));

  LOG(INFO) << "Changed flag '" << flag_name << "' from '"
            << GetMaskedValueIfSensitive(tags, old_val) << "' to '"
            << GetMaskedValueIfSensitive(tags, final_value) << "'";

  *output_msg = ret;
  *old_value = old_val;

  return SetFlagResult::SUCCESS;
}

std::vector<google::CommandLineFlagInfo> GetAllFlags(
    const std::map<std::string, std::string>& custom_varz) {
  std::vector<google::CommandLineFlagInfo> flag_infos;
  google::GetAllFlags(&flag_infos);

  if (custom_varz.empty()) {
    return flag_infos;
  }

  std::unordered_set<std::string> processed_custom_flags;
  // Replace values for existing flags.
  for (auto& flag_info : flag_infos) {
    auto* custom_value = FindOrNull(custom_varz, flag_info.name);
    if (!custom_value) {
      continue;
    }
    if (flag_info.current_value != *custom_value) {
      flag_info.current_value = *custom_value;
      flag_info.is_default = false;
    }
    processed_custom_flags.insert(flag_info.name);
  }

  // Add new flags.
  for (auto const& [flag_name, flag_value] : custom_varz) {
    if (processed_custom_flags.contains(flag_name)) {
      continue;
    }
    google::CommandLineFlagInfo flag_info;
    flag_info.name = flag_name;
    flag_info.current_value = flag_value;
    flag_info.default_value = "";
    flag_info.is_default = false;
    flag_infos.push_back(flag_info);
  }

  return flag_infos;
}

}  // namespace flags_internal

bool ValidatePercentageFlag(const char* flag_name, int value) {
  if (value >= 0 && value <= 100) {
    return true;
  }
  LOG_FLAG_VALIDATION_ERROR(flag_name, value) << "Must be a percentage (0 to 100)";
  return false;
}

bool IsUsageMessageSet() {
  // If it's not initialized it returns: "Warning: SetUsageMessage() never called".
  return !StringStartsWithOrEquals(google::ProgramUsage(), "Warning:");
}

void FlagValidatorSink::send(
    google::LogSeverity severity, const char* full_filename, const char* base_filename, int line,
    const struct ::tm* tm_time, const char* message, size_t message_len) {
  std::lock_guard l(mutex_);
  logged_msgs_.push_back(
      Format("$0:$1] $2", base_filename, line, std::string(message, message_len)));
}

const std::vector<std::string> FlagValidatorSink::GetMessagesAndClear() {
  std::lock_guard l(mutex_);
  auto result = std::move(logged_msgs_);
  logged_msgs_.clear();
  return result;
}

FlagValidatorSink& GetFlagValidatorSink() {
  static FlagValidatorSink sink;
  return sink;
}

bool RecordFlagForDelayedValidation(const std::string& flag_name) {
  if (CommandLineFlagsParsed()) {
    return false;
  }
  FlagsWithDelayedValidation().emplace_back(flag_name);
  return true;
}

// Read the flags xml file and return the list of flag names.
Result<std::unordered_set<std::string>> GetFlagNamesFromXmlFile(const std::string& flag_file_name) {
  std::unordered_set<std::string> flag_names;

  string build_path = yb::env_util::GetRootDir("bin");

  auto full_path = JoinPathSegments(build_path, flag_file_name);
  std::ifstream xml_file(full_path, std::ios_base::in);
  SCHECK(xml_file, IOError, Format("Could not open XML file $0: $1", full_path, strerror(errno)));

  static std::regex re(R"#(<name>(.*?)</name>)#");
  std::string line;
  while (std::getline(xml_file, line)) {
    std::smatch match;
    if (std::regex_search(line, match, re)) {
      flag_names.insert(match.str(1));
    }
  }

  return flag_names;
}

std::unordered_map<FlagType, std::vector<FlagInfo>> GetFlagInfos(
    std::function<bool(const std::string&)> auto_flags_filter,
    std::function<bool(const std::string&)> default_flags_filter,
    const std::map<std::string, std::string>& custom_varz) {
  const std::set<string> node_info_flags{
      "log_filename",    "rpc_bind_addresses", "webserver_interface", "webserver_port",
      "placement_cloud", "placement_region",   "placement_zone"};

  const auto flags = flags_internal::GetAllFlags(custom_varz);
  std::unordered_map<FlagType, std::vector<FlagInfo>> flag_infos;
  for (const auto& flag : flags) {
    std::unordered_set<FlagTag> flag_tags;
    GetFlagTags(flag.name, &flag_tags);

    FlagInfo flag_info;
    flag_info.name = flag.name;
    flag_info.value = flags_internal::GetMaskedValueIfSensitive(flag_tags, flag.current_value);

    auto type = FlagType::kDefault;
    if (node_info_flags.contains(flag.name)) {
      type = FlagType::kNodeInfo;
    } else if (flag.current_value != flag.default_value) {
      type = FlagType::kCustom;
    } else if (flag_tags.contains(FlagTag::kAuto) && auto_flags_filter(flag_info.name)) {
      type = FlagType::kAuto;
      flag_info.is_auto_flag_promoted = IsFlagPromoted(flag, *GetAutoFlagDescription(flag.name));
    }

    if (default_flags_filter && type == FlagType::kDefault &&
        !default_flags_filter(flag_info.name)) {
      continue;
    }

    flag_infos[type].push_back(std::move(flag_info));
  }

  // Sort by type, name ascending
  for (auto& [_, flags] : flag_infos) {
    std::sort(flags.begin(), flags.end(), [](const FlagInfo& lhs, const FlagInfo& rhs) {
      return ToLowerCase(lhs.name) < ToLowerCase(rhs.name);
    });
  }

  return flag_infos;
}

} // namespace yb
