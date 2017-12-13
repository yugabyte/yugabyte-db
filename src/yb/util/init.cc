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

#include "yb/util/init.h"

#include <string>

#include "yb/gutil/cpu.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/flag_tags.h"
#include "yb/util/path_util.h"
#include "yb/util/status.h"
#include "yb/util/version_info.h"

using std::string;

DEFINE_string(fs_data_dirs, "",
              "Comma-separated list of data directories. This argument must be specified.");
TAG_FLAG(fs_data_dirs, stable);

namespace yb {

const char* kTopLevelDataDirName = "yb-data";

Status BadCPUStatus(const base::CPU& cpu, const char* instruction_set) {
  return STATUS(NotSupported, strings::Substitute(
      "The CPU on this system ($0) does not support the $1 instruction "
      "set which is required for running YB.",
      cpu.cpu_brand(), instruction_set));
}

Status CheckCPUFlags() {
  base::CPU cpu;
  if (!cpu.has_sse42()) {
    return BadCPUStatus(cpu, "SSE4.2");
  }

  if (!cpu.has_ssse3()) {
    return BadCPUStatus(cpu, "SSSE3");
  }

  return Status::OK();
}

Status SetupLogDir(const std::string& server_type) {
  // If no log_dir specified, create the yugabyte specific directory structure and set the flag.
  if (FLAGS_log_dir.empty()) {
    std::vector<std::string> data_paths = strings::Split(
        FLAGS_fs_data_dirs, ",", strings::SkipEmpty());
    // Need at least one entry as we're picking the first one to drop the logs into.
    CHECK(data_paths.size() >= 1) << "Flag fs_data_dirs needs at least 1 path in csv format!";
    bool created;
    std::string out_dir;
    SetupRootDir(Env::Default(), data_paths[0], server_type, &out_dir, &created);
    // Create the actual log dir.
    out_dir = JoinPathSegments(out_dir, "logs");
    RETURN_NOT_OK_PREPEND(env_util::CreateDirIfMissing(Env::Default(), out_dir, &created),
                          "Unable to create FSManager path component " + out_dir);
    // Set the log dir.
    FLAGS_log_dir = out_dir;
  }
  // If we have a custom specified log_dir, use that.
  return Status::OK();
}

void InitYBOrDie(const std::string& server_type) {
  CHECK_OK(CheckCPUFlags());
  CHECK_OK(SetupLogDir(server_type));
  VersionInfo::Init();
}

} // namespace yb
