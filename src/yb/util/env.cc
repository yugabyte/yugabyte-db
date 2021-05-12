// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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

#include "yb/util/env.h"
#include "yb/util/faststring.h"
#include "yb/util/path_util.h"

namespace yb {

Env::~Env() {
}

CHECKED_STATUS Env::CreateDirs(const std::string& dirname) {
  if (!FileExists(dirname)) {
    RETURN_NOT_OK(CreateDirs(DirName(dirname)));
    RETURN_NOT_OK(CreateDir(dirname));
  }
  return VERIFY_RESULT(IsDirectory(dirname)) ?
      Status::OK() : STATUS_FORMAT(IOError, "Not a directory: $0", dirname);
}

RandomAccessFile::~RandomAccessFile() {
}

Status WritableFile::AppendVector(const std::vector<Slice>& data_vector) {
  return AppendSlices(data_vector.data(), data_vector.size());
}

WritableFile::~WritableFile() {
}

RWFile::~RWFile() {
}

FileLock::~FileLock() {
}

static Status DoWriteStringToFile(Env* env, const Slice& data,
                                  const std::string& fname,
                                  bool should_sync) {
  std::unique_ptr<WritableFile> file;
  Status s = env->NewWritableFile(fname, &file);
  if (!s.ok()) {
    return s;
  }
  s = file->Append(data);
  if (s.ok() && should_sync) {
    s = file->Sync();
  }
  if (s.ok()) {
    s = file->Close();
  }
  file.reset();  // Will auto-close if we did not close above
  if (!s.ok()) {
    WARN_NOT_OK(env->DeleteFile(fname),
                "Failed to delete partially-written file " + fname);
  }
  return s;
}

// TODO: move these utils into env_util
Status WriteStringToFile(Env* env, const Slice& data,
                         const std::string& fname) {
  return DoWriteStringToFile(env, data, fname, false);
}

Status WriteStringToFileSync(Env* env, const Slice& data,
                             const std::string& fname) {
  return DoWriteStringToFile(env, data, fname, true);
}

Status ReadFileToString(Env* env, const std::string& fname, faststring* data) {
  data->clear();
  std::unique_ptr<SequentialFile> file;
  Status s = env->NewSequentialFile(fname, &file);
  if (!s.ok()) {
    return s;
  }
  static const int kBufferSize = 8192;
  std::unique_ptr<uint8_t[]> scratch(new uint8_t[kBufferSize]);
  while (true) {
    Slice fragment;
    s = file->Read(kBufferSize, &fragment, scratch.get());
    if (!s.ok()) {
      break;
    }
    data->append(fragment.data(), fragment.size());
    if (fragment.empty()) {
      break;
    }
  }
  return s;
}

EnvWrapper::~EnvWrapper() {
}

Status DeleteIfExists(const std::string& path, Env* env) {
  if (env->DirExists(path)) {
    return env->DeleteRecursively(path);
  }
  if (env->FileExists(path)) {
    return env->DeleteFile(path);
  }
  return Status::OK();
}

}  // namespace yb
