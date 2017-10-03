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

#include "kudu/util/user.h"

#include <sys/types.h>
#include <errno.h>
#include <pwd.h>
#include <unistd.h>

#include <string>

#include <glog/logging.h>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/util/errno.h"
#include "kudu/util/status.h"

using std::string;

namespace kudu {

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
    return Status::RuntimeError("Malloc failed", ErrnoToString(errno), errno);
  }

  int ret = getpwuid_r(getuid(), &pwd, buf.get(), bufsize, &result);
  if (result == nullptr) {
    if (ret == 0) {
      return Status::NotFound("Current logged-in user not found! This is an unexpected error.");
    } else {
      // Errno in ret
      return Status::RuntimeError("Error calling getpwuid_r()", ErrnoToString(ret), ret);
    }
  }

  *user_name = pwd.pw_name;

  return Status::OK();
}

} // namespace kudu
