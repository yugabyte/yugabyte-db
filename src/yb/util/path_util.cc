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
#include <fcntl.h>

#include <string>
#if defined(__APPLE__)
#include <sys/param.h>
#endif // defined(__APPLE__)

#if defined(__linux__)
#include <linux/falloc.h>
#include <sys/sysinfo.h>
#endif

#include "yb/util/env_util.h"
#include "yb/util/errno.h"
#include "yb/util/logging.h"
#include "yb/util/malloc.h"
#include "yb/util/thread_restrictions.h"

using std::string;

namespace yb {

static const char* const kTmpTemplateSuffix = ".tmp.XXXXXX";

void AppendPathSegments(std::string* out, const std::string &b) {
  CHECK(!out->empty()) << "empty first component: " << *out;
  CHECK(!b.empty() && b[0] != '/')
    << "second path component must be non-empty and relative: "
    << b;
  if ((*out)[out->size() - 1] != '/') {
    *out += '/';
  }
  *out += b;
}

Status FileCreationError(const std::string& path_dir, int err_number) {
  switch (err_number) {
    case EACCES: FALLTHROUGH_INTENDED;
    case EPERM: FALLTHROUGH_INTENDED;
    case EINVAL:
      return STATUS(NotSupported, path_dir, Errno(err_number));
  }
  return Status::OK();
}

string DirName(const string& path) {
#if defined(__APPLE__)
  char buffer[MAXPATHLEN];
  return dirname_r(path.c_str(), buffer);
#else
  std::unique_ptr<char[], FreeDeleter> path_copy(strdup(path.c_str()));
  return dirname(path_copy.get());
#endif // defined(__APPLE__)
}

string BaseName(const string& path) {
#if defined(__APPLE__)
  char buffer[MAXPATHLEN];
  return basename_r(path.c_str(), buffer);
#else
  std::unique_ptr<char[], FreeDeleter> path_copy(strdup(path.c_str()));
  return basename(path_copy.get());
#endif
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

Status CheckODirectTempFileCreationInDir(Env* env,
                                         const std::string& dir_path) {
  std::string name_template;
  if (!dir_path.empty() && dir_path.back() == '/') {
    name_template = dir_path + kTmpTemplateSuffix;
  } else {
    name_template = dir_path + '/' + kTmpTemplateSuffix;
  }
  ThreadRestrictions::AssertIOAllowed();
  std::unique_ptr<char[]> fname(new char[name_template.size() + 1]);
  ::snprintf(fname.get(), name_template.size() + 1, "%s", name_template.c_str());
#if defined(__linux__)
  int fd = -1;
  fd = ::mkostemp(fname.get(), O_DIRECT);

  if (fd < 0) {
    return FileCreationError(dir_path, errno);
  }

  if (unlink(fname.get()) != 0) {
    return FileCreationError(dir_path, errno);
  }
#endif
  return Status::OK();
}

Result<string> path_utils::GetToolPath(const string& rel_path, const string& tool_name) {
  string exe;
  RETURN_NOT_OK(Env::Default()->GetExecutablePath(&exe));
  const string binroot = JoinPathSegments(DirName(exe), rel_path);
  const string tool_path = JoinPathSegments(binroot, tool_name);
  if (!Env::Default()->FileExists(tool_path)) {
    return STATUS_FORMAT(IOError, Format("$0 tool not found at: $1", tool_name, tool_path));
  }
  return tool_path;
}

} // namespace yb
