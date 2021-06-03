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

#include "yb/util/user.h"

#include <sys/types.h>
#include <errno.h>
#include <pwd.h>
#include <unistd.h>

#include <string>

#include <glog/logging.h>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/util/errno.h"
#include "yb/util/status.h"

using std::string;

namespace yb {

Status GetLoggedInUser(string* user_name) {
  DCHECK(user_name != nullptr);

  struct passwd pwd;
  struct passwd *result;

  size_t bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (bufsize == -1) {  // Value was indeterminate.
    bufsize = 16384;    // Should be more than enough, per the man page.
  }

  gscoped_ptr<char[], FreeDeleter> buf(static_cast<char *>(malloc(bufsize)));
  if (buf.get() == nullptr) {
    return STATUS(RuntimeError, "Malloc failed", Errno(errno));
  }

  auto uid = getuid();
  int ret = getpwuid_r(uid, &pwd, buf.get(), bufsize, &result);
  if (result == nullptr) {
    if (ret == 0) {
      return STATUS_FORMAT(
          NotFound, "Current logged-in user $0 not found! This is an unexpected error.", uid);
    } else {
      // Errno in ret
      return STATUS(RuntimeError, "Error calling getpwuid_r()", Errno(ret));
    }
  }

  *user_name = pwd.pw_name;

  return Status::OK();
}

} // namespace yb
