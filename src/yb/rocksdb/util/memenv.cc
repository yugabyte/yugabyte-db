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
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "yb/rocksdb/env.h"
#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/util/mutexlock.h"
#include "yb/util/file_system_mem.h"
#include "yb/util/status.h"

using std::unique_ptr;

namespace rocksdb {

typedef yb::InMemoryFileState InMemoryFileState;


namespace {

std::string NormalizeFileName(const std::string fname) {
  if (fname.find("//") == std::string::npos) {
    return fname;
  }
  std::string out_name = "";
  bool is_slash = false;
  for (char c : fname) {
    if (c == '/' && is_slash) {
      continue;
    }
    out_name.append(1, c);
    if (c == '/') {
      is_slash = true;
    } else {
      is_slash = false;
    }
  }
  return out_name;
}

class WritableFileImpl : public WritableFile {
 public:
  explicit WritableFileImpl(std::shared_ptr<InMemoryFileState> file) : file_(std::move(file)) {}

  ~WritableFileImpl() {}

  Status Append(const Slice& data) override {
    return file_->Append(data);
  }
  Status Truncate(uint64_t size) override {
    return Status::OK();
  }
  Status Close() override { return Status::OK(); }
  Status Flush() override { return Status::OK(); }
  Status Sync() override { return Status::OK(); }

  const std::string& filename() const override {
    return file_->filename();
  }

 private:
  std::shared_ptr<InMemoryFileState> file_;
};

class InMemoryDirectory : public Directory {
 public:
  Status Fsync() override { return Status::OK(); }
};

class InMemoryEnv : public EnvWrapper {
 public:
  explicit InMemoryEnv(Env* base_env) : EnvWrapper(base_env) { }

  virtual ~InMemoryEnv() {}

  // Partial implementation of the Env interface.
  virtual Status NewSequentialFile(const std::string& fname,
                                   unique_ptr<SequentialFile>* result,
                                   const EnvOptions& soptions) override {
    std::string nfname = NormalizeFileName(fname);
    MutexLock lock(&mutex_);
    if (file_map_.find(fname) == file_map_.end()) {
      *result = NULL;
      return STATUS(IOError, fname, "File not found");
    }

    result->reset(new yb::InMemorySequentialFile(file_map_[nfname]));
    return Status::OK();
  }

  virtual Status NewRandomAccessFile(const std::string& fname,
                                     unique_ptr<RandomAccessFile>* result,
                                     const EnvOptions& soptions) override {
    std::string nfname = NormalizeFileName(fname);
    MutexLock lock(&mutex_);
    if (file_map_.find(nfname) == file_map_.end()) {
      *result = NULL;
      return STATUS(IOError, fname, "File not found");
    }

    result->reset(new yb::InMemoryRandomAccessFile(file_map_[nfname]));
    return Status::OK();
  }

  virtual Status NewWritableFile(const std::string& fname,
                                 unique_ptr<WritableFile>* result,
                                 const EnvOptions& soptions) override {
    std::string nfname = NormalizeFileName(fname);
    MutexLock lock(&mutex_);
    if (file_map_.find(nfname) != file_map_.end()) {
      DeleteFileInternal(nfname);
    }

    auto file = std::make_shared<InMemoryFileState>(fname);
    file_map_[nfname] = file;

    result->reset(new WritableFileImpl(file));
    return Status::OK();
  }

  virtual Status NewDirectory(const std::string& name,
                              unique_ptr<Directory>* result) override {
    result->reset(new InMemoryDirectory());
    return Status::OK();
  }

  Status FileExists(const std::string& fname) override {
    std::string nfname = NormalizeFileName(fname);
    MutexLock lock(&mutex_);
    if (file_map_.find(nfname) != file_map_.end()) {
      return Status::OK();
    } else {
      return STATUS(NotFound, "");
    }
  }

  virtual Status GetChildren(const std::string& dir,
                             std::vector<std::string>* result) override {
    MutexLock lock(&mutex_);
    result->clear();

    for (FileSystem::iterator i = file_map_.begin(); i != file_map_.end(); ++i) {
      const std::string& filename = i->first;

      if (filename.size() >= dir.size() + 1 && filename[dir.size()] == '/' &&
          Slice(filename).starts_with(Slice(dir))) {
        result->push_back(filename.substr(dir.size() + 1));
      }
    }

    return Status::OK();
  }

  void DeleteFileInternal(const std::string& fname) {
    if (file_map_.find(fname) == file_map_.end()) {
      return;
    }

    file_map_.erase(fname);
  }

  Status DeleteFile(const std::string& fname) override {
    std::string nfname = NormalizeFileName(fname);
    MutexLock lock(&mutex_);
    if (file_map_.find(nfname) == file_map_.end()) {
      return STATUS(IOError, fname, "File not found");
    }

    DeleteFileInternal(nfname);
    return Status::OK();
  }

  Status CreateDir(const std::string& dirname) override {
    return Status::OK();
  }

  Status CreateDirIfMissing(const std::string& dirname) override {
    return Status::OK();
  }

  Status DeleteDir(const std::string& dirname) override {
    return Status::OK();
  }

  virtual Status GetFileSize(const std::string& fname,
                             uint64_t* file_size) override {
    std::string nfname = NormalizeFileName(fname);
    MutexLock lock(&mutex_);

    if (file_map_.find(nfname) == file_map_.end()) {
      return STATUS(IOError, fname, "File not found");
    }

    *file_size = file_map_[nfname]->Size();
    return Status::OK();
  }

  virtual Status GetFileModificationTime(const std::string& fname,
                                         uint64_t* time) override {
    return STATUS(NotSupported, "getFileMTime", "Not supported in MemEnv");
  }

  virtual Status RenameFile(const std::string& src,
                            const std::string& dest) override {
    std::string nsrc = NormalizeFileName(src);
    std::string ndest = NormalizeFileName(dest);
    MutexLock lock(&mutex_);
    if (file_map_.find(nsrc) == file_map_.end()) {
      return STATUS(IOError, src, "File not found");
    }

    DeleteFileInternal(dest);
    file_map_[ndest] = file_map_[nsrc];
    file_map_.erase(nsrc);
    return Status::OK();
  }

  Status LockFile(const std::string& fname, FileLock** lock) override {
    *lock = new FileLock;
    return Status::OK();
  }

  Status UnlockFile(FileLock* lock) override {
    delete lock;
    return Status::OK();
  }

  Status GetTestDirectory(std::string* path) override {
    *path = "/test";
    return Status::OK();
  }

 private:
  // Map from filenames to InMemoryFileState objects, representing a simple file system.
  typedef std::map<std::string, std::shared_ptr<InMemoryFileState>> FileSystem;
  port::Mutex mutex_;
  FileSystem file_map_;  // Protected by mutex_.
};

}  // namespace

Env* NewMemEnv(Env* base_env) {
  return new InMemoryEnv(base_env);
}


}  // namespace rocksdb
