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

#include "yb/util/path_util.h"

// Use the POSIX version of dirname(3).
#include <libgen.h>

#include <string>

#include <glog/logging.h>

#include "yb/util/errno.h"
#include "yb/gutil/gscoped_ptr.h"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mutex>
#endif // defined(__APPLE__)

using std::string;

namespace yb {

std::string JoinPathSegments(const std::string &a,
                             const std::string &b) {
  CHECK(!a.empty()) << "empty first component: " << a;
  CHECK(!b.empty() && b[0] != '/')
    << "second path component must be non-empty and relative: "
    << b;
  if (a[a.size() - 1] == '/') {
    return a + b;
  } else {
    return a + "/" + b;
  }
}

string DirName(const string& path) {
  gscoped_ptr<char[], FreeDeleter> path_copy(strdup(path.c_str()));
#if defined(__APPLE__)
  static std::mutex lock;
  std::lock_guard<std::mutex> l(lock);
#endif // defined(__APPLE__)
  return ::dirname(path_copy.get());
}

string BaseName(const string& path) {
  gscoped_ptr<char[], FreeDeleter> path_copy(strdup(path.c_str()));
  return basename(path_copy.get());
}

Result<string> GetExecutablePath() {
  char path[PATH_MAX];
#if defined(__APPLE__)
  uint32_t size = PATH_MAX;
  if (_NSGetExecutablePath(path, &size) != 0) {
    return STATUS(InternalError, "Got error from _NSGetExecutablePath(). Unable to determine path");
  }

  auto real_path = realpath(path, NULL);
  if (!real_path) {
    return STATUS_FORMAT(InternalError, "Got error from realpath(). Error: [$0] $1", errno,
        ErrnoToString(errno));
  }
  return string(real_path);
#else
  auto count = readlink("/proc/self/exe", path, PATH_MAX);
  if (count == -1) {
    return STATUS_FORMAT(InternalError, "Error reading /proc/self/exe for determining path. "
        "Error: [$0] $1", errno, ErrnoToString(errno));
  }
  return string(path, count);
#endif // defined(__APPLE__)
}

} // namespace yb
