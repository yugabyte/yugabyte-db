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
#ifndef KUDU_UTIL_VERSION_INFO_H
#define KUDU_UTIL_VERSION_INFO_H

#include <string>

#include "kudu/gutil/macros.h"

namespace kudu {

class VersionInfoPB;

// Static functions related to fetching information about the current build.
class VersionInfo {
 public:
  // Get a short version string ("kudu 1.2.3 (rev abcdef...)")
  static std::string GetShortVersionString();

  // Get a multi-line string including version info, build time, etc.
  static std::string GetAllVersionInfo();

  // Set the version info in 'pb'.
  static void GetVersionInfoPB(VersionInfoPB* pb);
 private:
  // Get the git hash for this build. If the working directory was dirty when
  // Kudu was built, also appends "-dirty".
  static std::string GetGitHash();

  DISALLOW_IMPLICIT_CONSTRUCTORS(VersionInfo);
};

} // namespace kudu
#endif /* KUDU_UTIL_VERSION_INFO_H */
