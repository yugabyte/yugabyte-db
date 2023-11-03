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
#pragma once

#include <mutex>
#include <string>

#include "yb/gutil/macros.h"

#include "yb/util/status_fwd.h"
#include "yb/util/version_info.pb.h"

namespace yb {

struct VersionData {
  VersionInfoPB pb;
  std::string json;
};

// Static functions related to fetching information about the current build.
class VersionInfo {
 public:
  // Get a short version string ("yb 1.2.3 (rev abcdef...)")
  static std::string GetShortVersionString();

  // Get a multi-line string including version info, build time, etc.
  static std::string GetAllVersionInfo();

  // Get a json object string including version info, build time, etc.
  static std::string GetAllVersionInfoJson();

  // Set the version info in 'pb'.
  static void GetVersionInfoPB(VersionInfoPB* pb);

  // Init version data.
  static Status Init();

 private:
  // Get the git hash for this build. If the working directory was dirty when
  // YB was built, also appends "-dirty".
  static std::string GetGitHash();

  static std::shared_ptr<const VersionData> GetVersionData();
  static Status ReadVersionDataFromFile();

  // Performs the initialization and stores its status into the given variable.
  static void InitInternal(Status* status);

  // Use this for lazy initialization.
  static std::once_flag init_once_;

  static std::shared_ptr<const VersionData> version_data_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(VersionInfo);
};

} // namespace yb
