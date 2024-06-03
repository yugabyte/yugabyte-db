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
#include "yb/util/env_util.h"

#include <memory>
#include <string>

#include <boost/container/small_vector.hpp>

#include "yb/gutil/strings/util.h"
#include "yb/util/env.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"

using strings::Substitute;
using std::shared_ptr;
using std::string;

namespace yb {
namespace env_util {

// We use this suffix in the "external build directory" mode, i.e. the source could be in
// ~/code/yugabyte the build directories for different configurations will all be in
// ~/code/yugabyte__build. This will prevent IDEs from trying to parse build artifacts.
const string kExternalBuildDirSuffix = "__build";

namespace {

// FindRootDir returns both a Status and a path. When the Status is not OK, we have failed to find
// the suitable "Yugabyte distribution root" directory, and the path returned is a default path
// ("" if we could not get the directory of the current executable, or the directory
// of the current executable) which is returned by GetRootDir.
std::pair<Status, std::string> FindRootDir(const std::string& search_for_dir) {
  char* yb_home = getenv("YB_HOME");
  if (yb_home) {
    return {Status::OK(), yb_home};
  }

  // If YB_HOME is not set, we use the path where the binary is located
  // (e.g., /opt/yugabyte/tserver/bin/yb-tserver) to determine the doc root.
  // To find "www"'s location, we search whether "www" exists at each directory, starting with
  // the directory where the current binary (yb-tserver, or yb-master) is located.
  // During each iteration, we keep going up one directory and do the search again.
  // If we can't find a directory that contains "www", we return a default value for now.
  std::string executable_path;
  auto status = Env::Default()->GetExecutablePath(&executable_path);
  if (!status.ok()) {
    return {status, ""};
  }

  auto path = executable_path;
  while (path != "/") {
    path = DirName(path);

    boost::container::small_vector<string, 2> candidates { path };
    if (HasSuffixString(path, kExternalBuildDirSuffix)) {
      candidates.push_back(std::string(path.begin(), path.end() - kExternalBuildDirSuffix.size()));
      if (candidates.back().back() == '/') {
        // path was of the ".../__build" form instead of ".../<some_name>__build", ignore it.
        candidates.pop_back();
      }
    }
    for (const auto& candidate_path : candidates) {
      auto sub_dir = JoinPathSegments(candidate_path, search_for_dir);
      bool is_dir = false;
      auto status = Env::Default()->IsDirectory(sub_dir, &is_dir);
      if (!status.ok()) {
        continue;
      }
      if (is_dir) {
        return {Status::OK(), candidate_path};
      }
    }
  }

  return {
      STATUS_SUBSTITUTE(
          NotFound,
          "Unable to find '$0' directory by starting the search at path $1 and walking up "
          "directory structure",
          search_for_dir,
          DirName(executable_path)),
      DirName(DirName(executable_path))};
}

} // namespace

std::string GetRootDir(const std::string& search_for_dir) {
  auto [status, path] = FindRootDir(search_for_dir);
  if (!status.ok()) {
    LOG(ERROR) << status.ToString();
  }
  return path;
}

Result<std::string> GetRootDirResult(const std::string& search_for_dir) {
  auto [status, path] = FindRootDir(search_for_dir);
  RETURN_NOT_OK(status);
  return path;
}

Status OpenFileForWrite(Env* env, const string& path,
                        shared_ptr<WritableFile>* file) {
  return OpenFileForWrite(WritableFileOptions(), env, path, file);
}

Status OpenFileForWrite(const WritableFileOptions& opts,
                        Env *env, const string &path,
                        shared_ptr<WritableFile> *file) {
  std::unique_ptr<WritableFile> w;
  RETURN_NOT_OK(env->NewWritableFile(opts, path, &w));
  file->reset(w.release());
  return Status::OK();
}

Status OpenFileForRandom(Env *env, const string &path,
                         shared_ptr<RandomAccessFile> *file) {
  std::unique_ptr<RandomAccessFile> r;
  RETURN_NOT_OK(env->NewRandomAccessFile(path, &r));
  file->reset(r.release());
  return Status::OK();
}

Status OpenFileForSequential(Env *env, const string &path,
                             shared_ptr<SequentialFile> *file) {
  std::unique_ptr<SequentialFile> r;
  RETURN_NOT_OK(env->NewSequentialFile(path, &r));
  file->reset(r.release());
  return Status::OK();
}

Status ReadFully(RandomAccessFile* file, uint64_t offset, size_t n,
                 Slice* result, uint8_t* scratch) {

  bool first_read = true;

  size_t rem = n;
  uint8_t* dst = scratch;
  while (rem > 0) {
    Slice this_result;
    RETURN_NOT_OK_PREPEND(
        file->Read(offset, rem, &this_result, dst),
        Format("Failed to read $0 bytes at $1", rem, offset));
    DCHECK_LE(this_result.size(), rem);
    if (this_result.size() == 0) {
      // EOF
      return STATUS(IOError, Substitute("EOF trying to read $0 bytes at offset $1",
                                        n, offset));
    }

    if (first_read && this_result.size() == n) {
      // If it's the first read, we can return a zero-copy array.
      *result = this_result;
      return Status::OK();
    }
    first_read = false;

    // Otherwise, we're going to have to do more reads and stitch
    // each read together.
    this_result.relocate(dst);
    dst += this_result.size();
    rem -= this_result.size();
    offset += this_result.size();
  }
  DCHECK_EQ(0, rem);
  *result = Slice(scratch, n);
  return Status::OK();
}

Status CreateDirIfMissing(Env* env, const string& path, bool* created) {
  Status s = env->CreateDir(path);
  if (created != nullptr) {
    *created = s.ok();
  }
  return s.IsAlreadyPresent() ? Status::OK() : s;
}

Status CopyFile(Env* env, const string& source_path, const string& dest_path,
                WritableFileOptions opts) {
  std::unique_ptr<SequentialFile> source;
  RETURN_NOT_OK(env->NewSequentialFile(source_path, &source));
  uint64_t size = VERIFY_RESULT(env->GetFileSize(source_path));

  std::unique_ptr<WritableFile> dest;
  RETURN_NOT_OK(env->NewWritableFile(opts, dest_path, &dest));
  RETURN_NOT_OK(dest->PreAllocate(size));

  const int32_t kBufferSize = 1024 * 1024;
  std::unique_ptr<uint8_t[]> scratch(new uint8_t[kBufferSize]);

  uint64_t bytes_read = 0;
  while (bytes_read < size) {
    uint64_t max_bytes_to_read = std::min<uint64_t>(size - bytes_read, kBufferSize);
    Slice data;
    RETURN_NOT_OK(source->Read(max_bytes_to_read, &data, scratch.get()));
    RETURN_NOT_OK(dest->Append(data));
    bytes_read += data.size();
  }
  return Status::OK();
}

ScopedFileDeleter::ScopedFileDeleter(Env* env, std::string path)
    : env_(DCHECK_NOTNULL(env)), path_(std::move(path)), should_delete_(true) {}

ScopedFileDeleter::~ScopedFileDeleter() {
  if (should_delete_) {
    bool is_dir;
    Status s = env_->IsDirectory(path_, &is_dir);
    WARN_NOT_OK(s, Substitute(
        "Failed to determine if path is a directory: $0", path_));
    if (!s.ok()) {
      return;
    }
    if (is_dir) {
      WARN_NOT_OK(env_->DeleteDir(path_),
                  Substitute("Failed to remove directory: $0", path_));
    } else {
      WARN_NOT_OK(env_->DeleteFile(path_),
          Substitute("Failed to remove file: $0", path_));
    }
  }
}

} // namespace env_util
} // namespace yb
