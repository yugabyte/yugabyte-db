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

#include <string>

#include "yb/generated/version_defines.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/version_info.pb.h"

using std::string;

namespace yb {

string VersionInfo::GetGitHash() {
  string ret = YB_GIT_HASH;
  if (!YB_BUILD_CLEAN_REPO) {
    ret += "-dirty";
  }
  return ret;
}

string VersionInfo::GetShortVersionString() {
  return strings::Substitute("yb $0 (rev $1)",
                             YB_VERSION_STRING,
                             GetGitHash());
}

string VersionInfo::GetAllVersionInfo() {
  string ret = strings::Substitute(
      "yb $0\n"
      "revision $1\n"
      "build type $2\n"
      "built by $3 at $4 on $5",
      YB_VERSION_STRING,
      GetGitHash(),
      YB_BUILD_TYPE,
      YB_BUILD_USERNAME,
      YB_BUILD_TIMESTAMP,
      YB_BUILD_HOSTNAME);
  if (strlen(YB_BUILD_ID) > 0) {
    strings::SubstituteAndAppend(&ret, "\nbuild id $0", YB_BUILD_ID);
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
  pb->set_git_hash(YB_GIT_HASH);
  pb->set_build_hostname(YB_BUILD_HOSTNAME);
  pb->set_build_timestamp(YB_BUILD_TIMESTAMP);
  pb->set_build_username(YB_BUILD_USERNAME);
  pb->set_build_clean_repo(YB_BUILD_CLEAN_REPO);
  pb->set_build_id(YB_BUILD_ID);
  pb->set_build_type(YB_BUILD_TYPE);
  pb->set_version_string(YB_VERSION_STRING);
}

} // namespace yb
