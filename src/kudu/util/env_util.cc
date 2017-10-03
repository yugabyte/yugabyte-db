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

#include <algorithm>
#include <memory>

#include <glog/logging.h>
#include <string>

#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/env.h"
#include "kudu/util/env_util.h"
#include "kudu/util/status.h"

using strings::Substitute;
using std::shared_ptr;

namespace kudu {
namespace env_util {

Status OpenFileForWrite(Env* env, const string& path,
                        shared_ptr<WritableFile>* file) {
  return OpenFileForWrite(WritableFileOptions(), env, path, file);
}

Status OpenFileForWrite(const WritableFileOptions& opts,
                        Env *env, const string &path,
                        shared_ptr<WritableFile> *file) {
  gscoped_ptr<WritableFile> w;
  RETURN_NOT_OK(env->NewWritableFile(opts, path, &w));
  file->reset(w.release());
  return Status::OK();
}

Status OpenFileForRandom(Env *env, const string &path,
                         shared_ptr<RandomAccessFile> *file) {
  gscoped_ptr<RandomAccessFile> r;
  RETURN_NOT_OK(env->NewRandomAccessFile(path, &r));
  file->reset(r.release());
  return Status::OK();
}

Status OpenFileForSequential(Env *env, const string &path,
                             shared_ptr<SequentialFile> *file) {
  gscoped_ptr<SequentialFile> r;
  RETURN_NOT_OK(env->NewSequentialFile(path, &r));
  file->reset(r.release());
  return Status::OK();
}

Status ReadFully(RandomAccessFile* file, uint64_t offset, size_t n,
                 Slice* result, uint8_t* scratch) {

  bool first_read = true;

  int rem = n;
  uint8_t* dst = scratch;
  while (rem > 0) {
    Slice this_result;
    RETURN_NOT_OK(file->Read(offset, rem, &this_result, dst));
    DCHECK_LE(this_result.size(), rem);
    if (this_result.size() == 0) {
      // EOF
      return Status::IOError(Substitute("EOF trying to read $0 bytes at offset $1",
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
  gscoped_ptr<SequentialFile> source;
  RETURN_NOT_OK(env->NewSequentialFile(source_path, &source));
  uint64_t size;
  RETURN_NOT_OK(env->GetFileSize(source_path, &size));

  gscoped_ptr<WritableFile> dest;
  RETURN_NOT_OK(env->NewWritableFile(opts, dest_path, &dest));
  RETURN_NOT_OK(dest->PreAllocate(size));

  const int32_t kBufferSize = 1024 * 1024;
  gscoped_ptr<uint8_t[]> scratch(new uint8_t[kBufferSize]);

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
} // namespace kudu
