// Copyright (c) YugaByte, Inc.
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

#pragma once

#include <float.h>
#include <string.h>

#include <chrono>
#include <cstdarg>
#include <sstream>
#include <string>
#include <type_traits>

#include <boost/mpl/and.hpp>

#include "yb/util/status.h"
#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/faststring.h"
#include "yb/util/format.h"
#include "yb/util/path_util.h"
#include "yb/util/tostring.h"
#include "yb/util/type_traits.h"

namespace yb {

YB_DEFINE_ENUM(CopyOption, (kCreateIfMissing)(kUseHardLinks)(kRecursive)(kKeepPermissions))
using CopyOptions = EnumBitSet<CopyOption>;

// TODO(unify_env): Temporary workaround until Env/Files from rocksdb and yb are unified
// (https://github.com/yugabyte/yugabyte-db/issues/1661).

// Following function returns OK if the file at `path` exists.
// NotFound if the named file does not exist, the calling process does not have permission to
//          determine whether this file exists, or if the path is invalid.
// IOError if an IO Error was encountered.
// Uses specified `env` environment implementation to do the actual file existence checking.
inline Status CheckFileExistsResult(const Status& status) {
  return status;
}

inline Status CheckFileExistsResult(bool exists) {
  return exists ? Status::OK() : STATUS(NotFound, "");
}

template <class Env>
inline Status FileExists(Env* env, const std::string& path) {
  return CheckFileExistsResult(env->FileExists(path));
}

using yb::env_util::CopyFile;

// Copies directory from `src_dir` to `dest_dir` using `env`.
// use_hard_links specifies whether to create hard links instead of actual file copying.
// create_if_missing specifies whether to create dest dir if doesn't exist or return an error.
// recursive_copy specifies whether the copy should be recursive.
// Returns error status in case of I/O errors.
template <class TEnv, class... Options>
Status CopyDirectory(
    TEnv* env, const std::string& src_dir, const std::string& dest_dir, Options&&... options_src) {
  CopyOptions options({std::forward<Options>(options_src)...});
  RETURN_NOT_OK_PREPEND(
      FileExists(env, src_dir), Format("Source directory does not exist: $0", src_dir));

  Status s = FileExists(env, dest_dir);
  if (!s.ok()) {
    if (options.Test(CopyOption::kCreateIfMissing)) {
      RETURN_NOT_OK_PREPEND(
          env->CreateDir(dest_dir), Format("Cannot create destination directory: $0", dest_dir));
    } else {
      return s.CloneAndPrepend(Format("Destination directory does not exist: $0", dest_dir));
    }
  }

  // Copy files.
  std::vector<std::string> files;
  RETURN_NOT_OK_PREPEND(
      env->GetChildren(src_dir, &files),
      Format("Cannot get list of files for directory: $0", src_dir));

  for (const std::string& file : files) {
    if (file == "." || file == "..") {
      continue;
    }

    const auto src_path = JoinPathSegments(src_dir, file);
    const auto dest_path = JoinPathSegments(dest_dir, file);

    if (options.Test(CopyOption::kUseHardLinks)) {
      s = env->LinkFile(src_path, dest_path);

      if (s.ok()) {
        continue;
      }
    }

    if (env->DirExists(src_path)) {
      if (options.Test(CopyOption::kRecursive)) {
        auto new_options = CopyOptions(options)
            .Set(CopyOption::kCreateIfMissing);
        RETURN_NOT_OK_PREPEND(
            CopyDirectory(env, src_path, dest_path, new_options),
            Format("Cannot copy directory: $0", src_path));
      }
    } else {
      RETURN_NOT_OK_PREPEND(
          CopyFile(env, src_path, dest_path), Format("Cannot copy file: $0", src_path));
      if (options.Test(CopyOption::kKeepPermissions)) {
        RETURN_NOT_OK(env_util::CopyFilePermissions(src_path, dest_path));
      }
    }
  }

  return Status::OK();
}

template <class TEnv>
Status CopyDirectory(
    TEnv* env, const std::string& src_dir, const std::string& dest_dir) {
  return CopyDirectory(env, src_dir, dest_dir, CopyOption::kRecursive);
}

}  // namespace yb
