//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
#pragma once

#include <atomic>
#include <map>
#include <string>
#include <vector>

#include "yb/rocksdb/env.h"
#include "yb/rocksdb/port/port.h"

namespace rocksdb {

class MemFile;
class MockEnv : public EnvWrapper {
 public:
  explicit MockEnv(Env* base_env);

  virtual ~MockEnv();

  // Partial implementation of the Env interface.
  virtual Status NewSequentialFile(const std::string& fname,
                                   std::unique_ptr<SequentialFile>* result,
                                   const EnvOptions& soptions) override;

  virtual Status NewRandomAccessFile(const std::string& fname,
                                     std::unique_ptr<RandomAccessFile>* result,
                                     const EnvOptions& soptions) override;

  virtual Status NewWritableFile(const std::string& fname,
                                 std::unique_ptr<WritableFile>* result,
                                 const EnvOptions& env_options) override;

  virtual Status NewDirectory(const std::string& name,
                              std::unique_ptr<Directory>* result) override;

  virtual Status FileExists(const std::string& fname) override;

  virtual Status GetChildren(const std::string& dir,
                             std::vector<std::string>* result) override;

  void DeleteFileInternal(const std::string& fname);

  virtual Status DeleteFile(const std::string& fname) override;

  virtual Status CreateDir(const std::string& dirname) override;

  virtual Status CreateDirIfMissing(const std::string& dirname) override;

  virtual Status DeleteDir(const std::string& dirname) override;

  virtual Status GetFileSize(const std::string& fname,
                             uint64_t* file_size) override;

  virtual Status GetFileModificationTime(const std::string& fname,
                                         uint64_t* time) override;

  virtual Status RenameFile(const std::string& src,
                            const std::string& target) override;

  virtual Status LinkFile(const std::string& src,
                          const std::string& target) override;

  virtual Status NewLogger(const std::string& fname,
                           std::shared_ptr<Logger>* result) override;

  virtual Status LockFile(const std::string& fname, FileLock** flock) override;

  virtual Status UnlockFile(FileLock* flock) override;

  virtual Status GetTestDirectory(std::string* path) override;

  // Results of these can be affected by FakeSleepForMicroseconds()
  virtual Status GetCurrentTime(int64_t* unix_time) override;
  virtual uint64_t NowMicros() override;
  virtual uint64_t NowNanos() override;

  // Non-virtual functions, specific to MockEnv
  Status Truncate(const std::string& fname, size_t size);

  Status CorruptBuffer(const std::string& fname);

  // Doesn't really sleep, just affects output of GetCurrentTime(), NowMicros()
  // and NowNanos()
  void FakeSleepForMicroseconds(int64_t micros);

 private:
  std::string NormalizePath(const std::string path);

  // Map from filenames to MemFile objects, representing a simple file system.
  typedef std::map<std::string, MemFile*> FileSystem;
  port::Mutex mutex_;
  FileSystem file_map_;  // Protected by mutex_.

  std::atomic<int64_t> fake_sleep_micros_;
};

}  // namespace rocksdb
