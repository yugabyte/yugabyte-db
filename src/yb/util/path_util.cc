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

#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/gutil/gscoped_ptr.h"

#if defined(__APPLE__)
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

std::string GetYbDataPath(const std::string& root) {
  return JoinPathSegments(root, "yb-data");
}

std::string GetServerTypeDataPath(
    const std::string& root, const std::string& server_type) {
  return JoinPathSegments(GetYbDataPath(root), server_type);
}

Status SetupRootDir(
    Env* env, const std::string& root, const std::string& server_type, std::string* out_dir,
    bool* created) {
  RETURN_NOT_OK_PREPEND(env_util::CreateDirIfMissing(env, root, created),
                        "Unable to create FS path component " + root);
  *out_dir = GetYbDataPath(root);
  RETURN_NOT_OK_PREPEND(env_util::CreateDirIfMissing(env, *out_dir, created),
                        "Unable to create FS path component " + *out_dir);
  *out_dir = GetServerTypeDataPath(root, server_type);
  RETURN_NOT_OK_PREPEND(env_util::CreateDirIfMissing(env, *out_dir, created),
                        "Unable to create FS path component " + *out_dir);
  return Status::OK();
}

} // namespace yb
