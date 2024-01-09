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
//

#include <fstream>
#include <string>

#include "yb/common/json_util.h"
#include "yb/gutil/map-util.h"
#include "yb/util/flags/auto_flags_util.h"
#include "yb/util/env_util.h"
#include "yb/util/path_util.h"
#include "yb/util/status.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/writer.h>

using rapidjson::Document;
using rapidjson::SizeType;
using rapidjson::StringBuffer;
using rapidjson::Value;
using std::string;
using yb::common::AddMember;

#define VALIDATE_MEMBER(doc, member_name, is_type, expected_type_name) \
  SCHECK( \
      doc.HasMember(member_name), NotFound, \
      AutoFlagsJsonParseErrorMsg() + Format("'$0' member not found.", member_name)); \
  SCHECK( \
      doc[member_name].is_type(), InvalidArgument, \
      AutoFlagsJsonParseErrorMsg() + \
          Format("'$0' member should be of type $1.", member_name, expected_type_name))

namespace yb {
const char kAutoFlagsJsonFileName[] = "auto_flags.json";
const char kAutoFlagsMember[] = "auto_flags";
const char kProgramMember[] = "program";
const char kFlagsMember[] = "flags";
const char kNameMember[] = "name";
const char kClassMember[] = "class";
const char kRuntimeMember[] = "is_runtime";

namespace {

// Function wrapper for the prefix error message to avoid static initialization order issues.
string AutoFlagsJsonParseErrorMsg() {
  static const string error_msg = Format("Failed to parse $0.", kAutoFlagsJsonFileName);
  return error_msg;
}

}  // namespace

namespace AutoFlagsUtil {
string DumpAutoFlagsToJSON(const ProcessName& program_name) {
  // Format:
  //  {
  //     "auto_flags": [
  //       {
  //         "program": "<program_name>",
  //         "flags": [
  //           {
  //             "name": "<flag_name>",
  //             "class": "<flag_class>",
  //             "is_runtime": <boolean>
  //           },...]
  //       },...
  //     ]
  //  }

  Document doc;
  doc.SetObject();
  auto& allocator = doc.GetAllocator();

  AddMember(kProgramMember, program_name, &doc, &allocator);

  Value flags(rapidjson::kArrayType);

  auto auto_flags = GetAllAutoFlagsDescription();
  for (const auto flag : auto_flags) {
    Value obj(rapidjson::kObjectType);

    AddMember(kNameMember, flag->name, &obj, &allocator);
    obj.AddMember(kClassMember, Value(yb::to_underlying(flag->flag_class)), allocator);
    obj.AddMember(kRuntimeMember, Value(flag->is_runtime), allocator);

    flags.PushBack(obj, allocator);
  }

  doc.AddMember(kFlagsMember, flags, allocator);

  // Convert JSON document to string
  StringBuffer str_buf;
  rapidjson::PrettyWriter<StringBuffer> writer(str_buf);
  doc.Accept(writer);
  return str_buf.GetString();
}

// Read kAutoFlagsJsonFileName and get the list of available auto flags for all processes.
Result<AutoFlagsInfoMap> GetAvailableAutoFlags() {
  AutoFlagsInfoMap available_flags;
  string auto_flags_file_json_path = yb::env_util::GetRootDir("bin");

  string full_path = JoinPathSegments(auto_flags_file_json_path, kAutoFlagsJsonFileName);
  std::ifstream json_file(full_path);
  SCHECK(
      !json_file.fail(), IOError,
      Format("Could not open JSON file $0: $1", full_path, strerror(errno)));

  rapidjson::IStreamWrapper stream_wrapper(json_file);
  Document doc;
  doc.ParseStream(stream_wrapper);

  SCHECK(
      !doc.HasParseError(), Corruption,
      AutoFlagsJsonParseErrorMsg() +
          Format("Error: $0.", rapidjson::GetParseError_En(doc.GetParseError())));

  VALIDATE_MEMBER(doc, kAutoFlagsMember, IsArray, "array");
  const auto& auto_flags = doc[kAutoFlagsMember];

  for (const auto& elem : auto_flags.GetArray()) {
    VALIDATE_MEMBER(elem, kProgramMember, IsString, "string");
    VALIDATE_MEMBER(elem, kFlagsMember, IsArray, "array");

    auto program_name = elem[kProgramMember].GetString();
    auto& output = available_flags[program_name];

    const auto& flags = elem[kFlagsMember];

    for (const auto& flag : flags.GetArray()) {
      VALIDATE_MEMBER(flag, kNameMember, IsString, "string");
      VALIDATE_MEMBER(flag, kClassMember, IsInt, "int");
      VALIDATE_MEMBER(flag, kRuntimeMember, IsBool, "bool");

      auto flag_class = VERIFY_RESULT_PREPEND(
          yb::UnderlyingToEnumSlow<yb::AutoFlagClass>(flag[kClassMember].GetInt()),
          AutoFlagsJsonParseErrorMsg() +
              Format("Flag Class '{0}' is invalid.", flag[kClassMember].GetString()));

      output.emplace_back(
          flag[kNameMember].GetString(), flag_class,
          RuntimeAutoFlag(flag[kRuntimeMember].GetBool()));
    }
  }

  return available_flags;
}

AutoFlagsInfoMap GetFlagsEligibleForPromotion(
    const AutoFlagsInfoMap& available_flags, const AutoFlagClass max_flag_class,
    const PromoteNonRuntimeAutoFlags promote_non_runtime) {
  AutoFlagsInfoMap eligible_flags;

  for (const auto& process_flag : available_flags) {
    for (const auto& flag : process_flag.second) {
      if (flag.flag_class <= max_flag_class && (flag.is_runtime || promote_non_runtime)) {
        eligible_flags[process_flag.first].emplace_back(flag);
      }
    }
  }

  return eligible_flags;
}

Result<AutoFlagsInfoMap> GetFlagsEligibleForPromotion(
    const AutoFlagClass max_flag_class, const PromoteNonRuntimeAutoFlags promote_non_runtime) {
  auto available_flags = VERIFY_RESULT(GetAvailableAutoFlags());

  return GetFlagsEligibleForPromotion(available_flags, max_flag_class, promote_non_runtime);
}

Result<bool> AreAutoFlagsCompatible(
    const AutoFlagsNameMap& base_flags, const AutoFlagsNameMap& to_check_flags,
    const AutoFlagsInfoMap& auto_flag_infos, AutoFlagClass min_class) {
  // Allowed test flags that we do not care about.
  static const std::set<std::string> kAutoFlagAllowList = {
      "TEST_auto_flags_initialized", "TEST_auto_flags_new_install"};

  for (const auto& [process_name, base_process_flags] : base_flags) {
    auto process_flag_info = FindOrNull(auto_flag_infos, process_name);
    SCHECK(process_flag_info, NotFound, "AutoFlags info for process $0 not found", process_name);

    auto to_check_flags_set = FindOrNull(to_check_flags, process_name);
    uint32 num_flags = 0;

    for (const auto& flag : *process_flag_info) {
      if (!base_process_flags.contains(flag.name)) {
        continue;
      }

      num_flags++;
      if (kAutoFlagAllowList.contains(flag.name)) {
        VLOG_WITH_FUNC(3) << "Skipping flag " << flag.name << " of process " << process_name;
        continue;
      }
      if (flag.flag_class < min_class) {
        VLOG_WITH_FUNC(3) << "Skipping flag " << flag.name << " of process " << process_name
                          << " since its class " << flag.flag_class << " is less than min class "
                          << min_class;
        continue;
      }

      if (!to_check_flags_set || !to_check_flags_set->contains(flag.name)) {
        LOG_WITH_FUNC(INFO) << "Flag " << flag.name << " of process " << process_name
                            << " with class " << flag.flag_class
                            << " not found in config to validate";
        return false;
      }
    }

    SCHECK_EQ(
        num_flags, base_process_flags.size(), NotFound, "AutoFlags info for some flags not found");
  }

  return true;
}

}  // namespace AutoFlagsUtil

}  // namespace yb
