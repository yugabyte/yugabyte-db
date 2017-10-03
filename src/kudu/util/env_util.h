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
#ifndef KUDU_UTIL_ENV_UTIL_H
#define KUDU_UTIL_ENV_UTIL_H

#include <memory>
#include <string>

#include "kudu/gutil/macros.h"
#include "kudu/util/env.h"

namespace kudu {
namespace env_util {

Status OpenFileForWrite(Env *env, const std::string &path,
                        std::shared_ptr<WritableFile> *file);

Status OpenFileForWrite(const WritableFileOptions& opts,
                        Env *env, const std::string &path,
                        std::shared_ptr<WritableFile> *file);

Status OpenFileForRandom(Env *env, const std::string &path,
                         std::shared_ptr<RandomAccessFile> *file);

Status OpenFileForSequential(Env *env, const std::string &path,
                             std::shared_ptr<SequentialFile> *file);

// Read exactly 'n' bytes from the given file. If fewer than 'n' bytes
// are read, returns an IOError. This differs from the underlying
// RandomAccessFile::Read(), which may return a "short read".
//
// Similar to RandomAccessFile::Read(), '*result' is modified to point
// to the bytes which were read. These bytes may be a copy placed in
// the 'scratch' buffer, or result may point into the underlying file
// (e.g. via mmap or other zero-copy mechanism).
//
// NOTE: even if this returns an error, some data _may_ be read into
// the provided scratch buffer, but no guarantee that that will be the
// case.
Status ReadFully(RandomAccessFile* file, uint64_t offset, size_t n,
                 Slice* result, uint8_t* scratch);

// Creates the directory given by 'path', unless it already exists.
//
// If 'created' is not NULL, sets it to true if the directory was
// created, false otherwise.
Status CreateDirIfMissing(Env* env, const std::string& path,
                          bool* created = NULL);

// Copy the contents of file source_path to file dest_path.
// This is not atomic, and if there is an error while reading or writing,
// a partial copy may be left in 'dest_path'. Does not fsync the parent
// directory of dest_path -- if you need durability then do that yourself.
Status CopyFile(Env* env, const std::string& source_path, const std::string& dest_path,
                WritableFileOptions opts);

// Deletes a file or directory when this object goes out of scope.
//
// The deletion may be cancelled by calling .Cancel().
// This is typically useful for cleaning up temporary files if the
// creation of the tmp file may fail.
class ScopedFileDeleter {
 public:
  ScopedFileDeleter(Env* env, std::string path);
  ~ScopedFileDeleter();

  // Do not delete the file when this object goes out of scope.
  void Cancel() {
    should_delete_ = false;
  }

 private:
  Env* const env_;
  const std::string path_;
  bool should_delete_;

  DISALLOW_COPY_AND_ASSIGN(ScopedFileDeleter);
};

} // namespace env_util
} // namespace kudu

#endif
