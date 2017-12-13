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

#include "yb/util/version_info.h"

#include <fstream>
#include <string>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/istreamwrapper.h>

#include "yb/gutil/strings/substitute.h"
#include "yb/util/env_util.h"
#include "yb/util/path_util.h"
#include "yb/util/version_info.pb.h"

DEFINE_string(version_file_json_path, "",
              "Path to directory containing JSON file with version info.");

using std::string;

namespace yb {

const char* kVersionJsonFileName = "version_metadata.json";

std::once_flag VersionInfo::init_once_;
std::atomic<VersionInfoPB*> VersionInfo::version_data_ = { nullptr };

string VersionInfo::GetGitHash() {
  auto* data = GetVersionData();

  string ret = data->git_hash();
  if (!data->build_clean_repo()) {
    ret += "-dirty";
  }
  return ret;
}

string VersionInfo::GetShortVersionString() {
  auto* data = GetVersionData();

  return strings::Substitute("version $0 build $1 build_type $2",
                             data->version_number(),
                             data->build_number(),
                             data->build_type());
}

string VersionInfo::GetAllVersionInfo() {
  auto* data = GetVersionData();

  string ret = strings::Substitute(
      "version $0\n"
      "build $1"
      "revision $2\n"
      "build_type $3\n"
      "built by $4 at $5 on $6",
      data->version_number(),
      data->build_number(),
      GetGitHash(),
      data->build_type(),
      data->build_username(),
      data->build_timestamp(),
      data->build_hostname());
  if (data->build_id().size() > 0) {
    strings::SubstituteAndAppend(&ret, "\nbuild id $0", data->build_id());
  }
#ifdef ADDRESS_SANITIZER
  ret += "\nASAN enabled";
#endif
#ifdef THREAD_SANITIZER
  ret += "\nTSAN enabled";
#endif
  return ret;
}

void VersionInfo::GetVersionInfoPB(VersionInfoPB* pb) {
  pb->CopyFrom(*GetVersionData());
}

Status VersionInfo::ReadVersionDataFromFile() {
  SCHECK(version_data_.load(std::memory_order_acquire) == nullptr,
         IllegalState, "Cannot reload version data from file...");

  std::string version_file_path = FLAGS_version_file_json_path;
  if (version_file_path.empty()) {
    version_file_path = yb::env_util::GetRootDir("bin");
  }

  std::string config_file_path = JoinPathSegments(version_file_path, kVersionJsonFileName);
  std::ifstream json_file(config_file_path);
  SCHECK(!json_file.fail(),
          IllegalState, strings::Substitute("Could not open JSON file $0", config_file_path));

  rapidjson::IStreamWrapper isw(json_file);
  rapidjson::Document d;
  d.ParseStream(isw);
  SCHECK(!d.HasParseError(),
         IllegalState, strings::Substitute("Failed to parse json. Error: $0 ",
            rapidjson::GetParseError_En(d.GetParseError())));

  std::unique_ptr<VersionInfoPB> pb(new VersionInfoPB);

  const std::map<std::string, std::string*> keys_and_outputs = {
    { "git_hash", pb->mutable_git_hash() },
    { "build_hostname", pb->mutable_build_hostname() },
    { "build_timestamp", pb->mutable_build_timestamp() },
    { "build_username", pb->mutable_build_username() },
    { "build_id", pb->mutable_build_id() },
    { "build_type", pb->mutable_build_type() },
    { "version_number", pb->mutable_version_number() },
    { "build_number", pb->mutable_build_number() }
  };

  for (const auto& entry : keys_and_outputs) {
    const auto& key = entry.first;
    auto* output = entry.second;
    if (!d.HasMember(key.c_str())) {
      return STATUS(IllegalState, strings::Substitute("Key $0 does not exist", key));
    } else if(!d[key.c_str()].IsString()) {
      return STATUS(IllegalState, strings::Substitute("Key $0 is of invalid type $1",
                                                      key, d[key.c_str()].GetType()));
    }
    *output = d[key.c_str()].GetString();
  }

  // Special case the only boolean flag...
  if (!d.HasMember("build_clean_repo")) {
    return STATUS(IllegalState, strings::Substitute("Key $0 does not exist", "build_clean_repo"));
  } else if(!d["build_clean_repo"].IsString()) {
    return STATUS(IllegalState,
                  strings::Substitute("Key $0 is of invalid type $1",
                                      "build_clean_repo", d["build_clean_repo"].GetType()));
  } else {
    pb->set_build_clean_repo(d["build_clean_repo"] == "true");
  }

  version_data_.store(pb.release(), std::memory_order_release);
  return Status::OK();
}

void VersionInfo::Init() {
  std::call_once(init_once_, InitOrDie);
}

VersionInfoPB* VersionInfo::GetVersionData() {
  std::call_once(init_once_, InitOrDie);
  return version_data_.load(std::memory_order_acquire);
}

void VersionInfo::InitOrDie() {
  CHECK_OK(ReadVersionDataFromFile());
}

} // namespace yb
