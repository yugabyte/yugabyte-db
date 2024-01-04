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
#include <rapidjson/writer.h>

#include "yb/util/env_util.h"
#include "yb/util/path_util.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/flags.h"

DEFINE_NON_RUNTIME_string(version_file_json_path, "",
              "Path to directory containing JSON file with version info.");

using std::string;

namespace yb {

const char* kVersionJsonFileName = "version_metadata.json";

std::once_flag VersionInfo::init_once_;
std::shared_ptr<const VersionData> VersionInfo::version_data_ = nullptr;

string VersionInfo::GetGitHash() {
  auto data = GetVersionData();

  string ret = data->pb.git_hash();
  if (!data->pb.build_clean_repo()) {
    ret += "-dirty";
  }
  return ret;
}

string VersionInfo::GetShortVersionString() {
  auto data = GetVersionData();

  return strings::Substitute("version $0 build $1 revision $2 build_type $3 built at $4",
                             data->pb.version_number(),
                             data->pb.build_number(),
                             data->pb.git_hash(),
                             data->pb.build_type(),
                             data->pb.build_timestamp());
}

string VersionInfo::GetAllVersionInfo() {
  auto data = GetVersionData();

  string ret = strings::Substitute(
      "version $0\n"
      "build $1\n"
      "revision $2\n"
      "build_type $3\n"
      "built by $4 at $5 on $6",
      data->pb.version_number(),
      data->pb.build_number(),
      GetGitHash(),
      data->pb.build_type(),
      data->pb.build_username(),
      data->pb.build_timestamp(),
      data->pb.build_hostname());
  if (data->pb.build_id().size() > 0) {
    strings::SubstituteAndAppend(&ret, "\nbuild id $0", data->pb.build_id());
  }
#ifdef ADDRESS_SANITIZER
  ret += "\nASAN enabled";
#endif
#ifdef THREAD_SANITIZER
  ret += "\nTSAN enabled";
#endif
  return ret;
}

string VersionInfo::GetAllVersionInfoJson() {
  return std::atomic_load_explicit(&version_data_, std::memory_order_acquire)->json;
}

void VersionInfo::GetVersionInfoPB(VersionInfoPB* pb) {
  pb->CopyFrom(GetVersionData()->pb);
}

Status VersionInfo::ReadVersionDataFromFile() {
  SCHECK(std::atomic_load_explicit(&version_data_, std::memory_order_acquire) == nullptr,
         IllegalState, "Cannot reload version data from file...");

  std::string version_file_path = FLAGS_version_file_json_path;
  if (version_file_path.empty()) {
    version_file_path = yb::env_util::GetRootDir("bin");
  }

  std::string config_file_path = JoinPathSegments(version_file_path, kVersionJsonFileName);
  std::ifstream json_file(config_file_path);
  SCHECK(
      !json_file.fail(), IllegalState,
      strings::Substitute("Could not open JSON file $0: $1", config_file_path, strerror(errno)));

  rapidjson::IStreamWrapper isw(json_file);
  rapidjson::Document d;
  d.ParseStream(isw);
  SCHECK(!d.HasParseError(),
         IllegalState, strings::Substitute("Failed to parse json. Error: $0 ",
            rapidjson::GetParseError_En(d.GetParseError())));

  auto version_data = std::make_shared<VersionData>();

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  d.Accept(writer);

  version_data->json = buffer.GetString();

  const std::map<std::string, std::string*> keys_and_outputs = {
    { "git_hash", version_data->pb.mutable_git_hash() },
    { "build_hostname", version_data->pb.mutable_build_hostname() },
    { "build_timestamp", version_data->pb.mutable_build_timestamp() },
    { "build_username", version_data->pb.mutable_build_username() },
    { "build_id", version_data->pb.mutable_build_id() },
    { "build_type", version_data->pb.mutable_build_type() },
    { "version_number", version_data->pb.mutable_version_number() },
    { "build_number", version_data->pb.mutable_build_number() }
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
    version_data->pb.set_build_clean_repo(d["build_clean_repo"] == "true");
  }

  std::atomic_store_explicit(&version_data_,
                             static_cast<std::shared_ptr<const VersionData>>(version_data),
                             std::memory_order_release);
  return Status::OK();
}

Status VersionInfo::Init() {
  Status status;
  std::call_once(init_once_, InitInternal, &status);
  return status;
}

std::shared_ptr<const VersionData> VersionInfo::GetVersionData() {
  CHECK_OK(Init());
  return std::atomic_load_explicit(&version_data_, std::memory_order_acquire);
}

void VersionInfo::InitInternal(Status* status_dest) {
  *status_dest = ReadVersionDataFromFile();
}

} // namespace yb
