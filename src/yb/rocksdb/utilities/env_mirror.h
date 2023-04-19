// Copyright (c) 2015, Red Hat, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
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
// MirrorEnv is an Env implementation that mirrors all file-related
// operations to two backing Env's (provided at construction time).
// Writes are mirrored.  For read operations, we do the read from both
// backends and assert that the results match.
//
// This is useful when implementing a new Env and ensuring that the
// semantics and behavior are correct (in that they match that of an
// existing, stable Env, like the default POSIX one).
#pragma once


#include <iostream>
#include <algorithm>
#include <vector>
#include "yb/rocksdb/env.h"

namespace rocksdb {

class SequentialFileMirror;
class RandomAccessFileMirror;
class WritableFileMirror;

class EnvMirror : public EnvWrapper {
  Env* a_, *b_;

 public:
  EnvMirror(Env* a, Env* b) : EnvWrapper(a), a_(a), b_(b) {}

  Status NewSequentialFile(const std::string& f, std::unique_ptr<SequentialFile>* r,
                           const EnvOptions& options) override;
  Status NewRandomAccessFile(const std::string& f,
                             std::unique_ptr<RandomAccessFile>* r,
                             const EnvOptions& options) override;
  Status NewWritableFile(const std::string& f, std::unique_ptr<WritableFile>* r,
                         const EnvOptions& options) override;
  Status ReuseWritableFile(const std::string& fname,
                           const std::string& old_fname,
                           std::unique_ptr<WritableFile>* r,
                           const EnvOptions& options) override;
  Status NewDirectory(const std::string& name,
                      std::unique_ptr<Directory>* result) override;
  Status FileExists(const std::string& f) override;
  Status GetChildren(const std::string& dir,
                     std::vector<std::string>* r) override;
  Status DeleteFile(const std::string& f) override;
  Status CreateDir(const std::string& d) override;
  Status CreateDirIfMissing(const std::string& d) override;
  Status DeleteDir(const std::string& d) override;
  Status GetFileSize(const std::string& f, uint64_t* s) override;

  Status GetFileModificationTime(const std::string& fname,
                                 uint64_t* file_mtime) override;

  Status RenameFile(const std::string& s, const std::string& t) override;

  Status LinkFile(const std::string& s, const std::string& t) override;

  class FileLockMirror : public FileLock {
   public:
    FileLock* a_, *b_;
    FileLockMirror(FileLock* a, FileLock* b) : a_(a), b_(b) {}
  };

  Status LockFile(const std::string& f, FileLock** l) override;

  Status UnlockFile(FileLock* l) override;
};

}  // namespace rocksdb
