// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "kudu/util/env.h"
#include "kudu/util/faststring.h"

namespace kudu {

Env::~Env() {
}

SequentialFile::~SequentialFile() {
}

RandomAccessFile::~RandomAccessFile() {
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
  gscoped_ptr<WritableFile> file;
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
  gscoped_ptr<SequentialFile> file;
  Status s = env->NewSequentialFile(fname, &file);
  if (!s.ok()) {
    return s;
  }
  static const int kBufferSize = 8192;
  gscoped_ptr<uint8_t[]> scratch(new uint8_t[kBufferSize]);
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

}  // namespace kudu
