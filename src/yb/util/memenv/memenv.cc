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
// Modified for yb:
// - use boost mutexes instead of port mutexes
#include "yb/util/memenv/memenv.h"

#include <string.h>

#include <map>
#include <random>
#include <string>
#include <vector>

#include <glog/logging.h>

#include "yb/gutil/map-util.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/strip.h"
#include "yb/gutil/walltime.h"
#include "yb/util/env.h"
#include "yb/util/file_system_mem.h"
#include "yb/util/malloc.h"
#include "yb/util/mutex.h"
#include "yb/util/random.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {

namespace {

using std::string;
using std::vector;
using strings::Substitute;

class RandomAccessFileImpl : public RandomAccessFile {
 public:
  explicit RandomAccessFileImpl(const std::shared_ptr<InMemoryFileState>& file)
    : file_(std::move(file)) {
  }

  ~RandomAccessFileImpl() {
  }

  virtual Status Read(uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const override {
    return file_->Read(offset, n, result, scratch);
  }

  Result<uint64_t> Size() const override {
    return file_->Size();
  }

  Result<uint64_t> INode() const override {
    return 0;
  }

  const std::string& filename() const override {
    return file_->filename();
  }

  size_t memory_footprint() const override {
    // The InMemoryFileState is actually shared between multiple files, but the double
    // counting doesn't matter much since MemEnv is only used in tests.
    return malloc_usable_size(this) + file_->memory_footprint();
  }

 private:
  const std::shared_ptr<InMemoryFileState> file_;
};

class WritableFileImpl : public WritableFile {
 public:
  explicit WritableFileImpl(const std::shared_ptr<InMemoryFileState>& file)
    : file_(file) {
  }

  ~WritableFileImpl() {
  }

  Status PreAllocate(uint64_t size) override {
    return file_->PreAllocate(size);
  }

  Status Append(const Slice& data) override {
    return file_->Append(data);
  }

  // This is a dummy implementation that simply serially appends all
  // slices using regular I/O.
  Status AppendSlices(const Slice* slices, size_t num) override {
    for (const auto* end = slices + num; slices != end; ++slices) {
      RETURN_NOT_OK(file_->Append(*slices));
    }
    return Status::OK();
  }

  Status Close() override { return Status::OK(); }

  Status Flush(FlushMode mode) override { return Status::OK(); }

  Status Sync() override { return Status::OK(); }

  uint64_t Size() const override { return file_->Size(); }

  const string& filename() const override {
    return file_->filename();
  }

 private:
  const std::shared_ptr<InMemoryFileState> file_;
};

class RWFileImpl : public RWFile {
 public:
  explicit RWFileImpl(const std::shared_ptr<InMemoryFileState>& file)
    : file_(file) {
  }

  ~RWFileImpl() {
  }

  virtual Status Read(uint64_t offset, size_t length,
                      Slice* result, uint8_t* scratch) const override {
    return file_->Read(offset, length, result, scratch);
  }

  Status Write(uint64_t offset, const Slice& data) override {
    uint64_t file_size = file_->Size();
    // TODO: Modify InMemoryFileState to allow rewriting.
    if (offset < file_size) {
      return STATUS(NotSupported, "In-memory RW file does not support random writing");
    } else if (offset > file_size) {
      // Fill in the space between with zeroes.
      std::string zeroes(offset - file_size, '\0');
      RETURN_NOT_OK(file_->Append(zeroes));
    }
    return file_->Append(data);
  }

  Status PreAllocate(uint64_t offset, size_t length) override {
    return Status::OK();
  }

  Status PunchHole(uint64_t offset, size_t length) override {
    return Status::OK();
  }

  Status Flush(FlushMode mode, uint64_t offset, size_t length) override {
    return Status::OK();
  }

  Status Sync() override {
    return Status::OK();
  }

  Status Close() override {
    return Status::OK();
  }

  Status Size(uint64_t* size) const override {
    *size = file_->Size();
    return Status::OK();
  }

  const string& filename() const override {
    return file_->filename();
  }

 private:
  const std::shared_ptr<InMemoryFileState> file_;
};

class InMemoryEnv : public EnvWrapper {
 public:
  explicit InMemoryEnv(Env* base_env) : EnvWrapper(base_env) { }

  virtual ~InMemoryEnv() {
  }

  // Partial implementation of the Env interface.
  virtual Status NewSequentialFile(const std::string& fname,
                                   std::unique_ptr<SequentialFile>* result) override {
    MutexLock lock(mutex_);
    if (file_map_.find(fname) == file_map_.end()) {
      return STATUS(IOError, fname, "File not found");
    }

    result->reset(new InMemorySequentialFile(file_map_[fname]));
    return Status::OK();
  }

  virtual Status NewRandomAccessFile(const std::string& fname,
                                     std::unique_ptr<RandomAccessFile>* result) override {
    MutexLock lock(mutex_);
    if (file_map_.find(fname) == file_map_.end()) {
      return STATUS(IOError, fname, "File not found");
    }

    result->reset(new RandomAccessFileImpl(file_map_[fname]));
    return Status::OK();
  }

  virtual Status NewWritableFile(const WritableFileOptions& opts,
                                 const std::string& fname,
                                 std::unique_ptr<WritableFile>* result) override {
    std::unique_ptr<WritableFileImpl> wf;
    RETURN_NOT_OK(CreateAndRegisterNewFile(fname, opts.mode, &wf));
    result->reset(wf.release());
    return Status::OK();
  }

  virtual Status NewWritableFile(const std::string& fname,
                                 std::unique_ptr<WritableFile>* result) override {
    return NewWritableFile(WritableFileOptions(), fname, result);
  }

  virtual Status NewRWFile(const RWFileOptions& opts,
                           const string& fname,
                           std::unique_ptr<RWFile>* result) override {
    std::unique_ptr<RWFileImpl> rwf;
    RETURN_NOT_OK(CreateAndRegisterNewFile(fname, opts.mode, &rwf));
    result->reset(rwf.release());
    return Status::OK();
  }

  virtual Status NewRWFile(const string& fname,
                           std::unique_ptr<RWFile>* result) override {
    return NewRWFile(RWFileOptions(), fname, result);
  }

  virtual Status NewTempWritableFile(const WritableFileOptions& opts,
                                     const std::string& name_template,
                                     std::string* created_filename,
                                     std::unique_ptr<WritableFile>* result) override {
    // Not very random, but InMemoryEnv is basically a test env.
    std::mt19937_64 random(GetCurrentTimeMicros());
    while (true) {
      string stripped;
      if (!TryStripSuffixString(name_template, "XXXXXX", &stripped)) {
        return STATUS(InvalidArgument, "Name template must end with the string XXXXXX",
                                       name_template);
      }
      uint32_t num = random() % 999999; // Ensure it's <= 6 digits long.
      string path = StringPrintf("%s%06u", stripped.c_str(), num);

      MutexLock lock(mutex_);
      if (!ContainsKey(file_map_, path)) {
        CreateAndRegisterNewWritableFileUnlocked<WritableFile, WritableFileImpl>(path, result);
        *created_filename = path;
        return Status::OK();
      }
    }
    // Unreachable.
  }

  bool FileExists(const std::string& fname) override {
    MutexLock lock(mutex_);
    return file_map_.find(fname) != file_map_.end();
  }

  Status GetChildren(const std::string& dir,
                     ExcludeDots exclude_dots,
                     vector<std::string>* result) override {
    MutexLock lock(mutex_);
    result->clear();

    for (const auto& file : file_map_) {
      const std::string& filename = file.first;

      if (filename.size() >= dir.size() + 1 && filename[dir.size()] == '/' &&
          Slice(filename).starts_with(Slice(dir))) {
        result->push_back(filename.substr(dir.size() + 1));
      }
    }

    return Status::OK();
  }

  Status DeleteFile(const std::string& fname) override {
    MutexLock lock(mutex_);
    if (file_map_.find(fname) == file_map_.end()) {
      return STATUS(IOError, fname, "File not found");
    }

    DeleteFileInternal(fname);
    return Status::OK();
  }

  Status CreateDir(const std::string& dirname) override {
    std::unique_ptr<WritableFile> file;
    return NewWritableFile(dirname, &file);
  }

  Status DeleteDir(const std::string& dirname) override {
    return DeleteFile(dirname);
  }

  Status SyncDir(const std::string& dirname) override {
    return Status::OK();
  }

  Status DeleteRecursively(const std::string& dirname) override {
    CHECK(!dirname.empty());
    string dir(dirname);
    if (dir[dir.size() - 1] != '/') {
      dir.push_back('/');
    }

    MutexLock lock(mutex_);

    for (auto i = file_map_.begin(); i != file_map_.end();) {
      const std::string& filename = i->first;

      if (filename.size() >= dir.size() && Slice(filename).starts_with(Slice(dir))) {
        file_map_.erase(i++);
      } else {
        ++i;
      }
    }

    return Status::OK();
  }

  Result<uint64_t> GetFileSize(const std::string& fname) override {
    MutexLock lock(mutex_);
    if (file_map_.find(fname) == file_map_.end()) {
      return STATUS(IOError, fname, "File not found");
    }

    return file_map_[fname]->Size();
  }

  Result<uint64_t> GetFileINode(const std::string& fname) override {
    return 0;
  }

  Result<uint64_t> GetFileSizeOnDisk(const std::string& fname) override {
    return GetFileSize(fname);
  }

  Result<uint64_t> GetBlockSize(const string& fname) override {
    // The default for ext3/ext4 filesystems.
    return 4096;
  }

  virtual Status RenameFile(const std::string& src,
                            const std::string& target) override {
    MutexLock lock(mutex_);
    if (file_map_.find(src) == file_map_.end()) {
      return STATUS(IOError, src, "File not found");
    }

    DeleteFileInternal(target);
    file_map_[target] = file_map_[src];
    file_map_.erase(src);
    return Status::OK();
  }

  virtual Status LockFile(const std::string& fname,
                          FileLock** lock,
                          bool recursive_lock_ok) override {
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

  virtual Status Walk(const std::string& root,
                      DirectoryOrder order,
                      const WalkCallback& cb) override {
    LOG(FATAL) << "Not implemented";
  }

  Status Canonicalize(const string& path, string* result) override {
    *result = path;
    return Status::OK();
  }

  Status GetTotalRAMBytes(int64_t* ram) override {
    LOG(FATAL) << "Not implemented";
  }

  Result<uint64_t> GetFreeSpaceBytes(const std::string& path) override {
    LOG(FATAL) << "Not implemented";
  }

  bool IsEncrypted() const override {
    return false;
  }

 private:
  void DeleteFileInternal(const std::string& fname) {
    if (!ContainsKey(file_map_, fname)) {
      return;
    }
    file_map_.erase(fname);
  }

  // Create new internal representation of a writable file.
  template <typename PtrType, typename ImplType>
  void CreateAndRegisterNewWritableFileUnlocked(const string& path,
                                                std::unique_ptr<PtrType>* result) {
    file_map_[path] = std::make_shared<InMemoryFileState>(path);
    result->reset(new ImplType(file_map_[path]));
  }

  // Create new internal representation of a file.
  template <typename Type>
  Status CreateAndRegisterNewFile(const string& fname,
                                  CreateMode mode,
                                  std::unique_ptr<Type>* result) {
    MutexLock lock(mutex_);
    if (ContainsKey(file_map_, fname)) {
      switch (mode) {
        case CREATE_IF_NON_EXISTING_TRUNCATE:
          FALLTHROUGH_INTENDED;
        case CREATE_NONBLOCK_IF_NON_EXISTING:
          DeleteFileInternal(fname);
          break; // creates a new file below
        case CREATE_NON_EXISTING:
          return STATUS(AlreadyPresent, fname, "File already exists");
        case OPEN_EXISTING:
          result->reset(new Type(file_map_[fname]));
          return Status::OK();
        default:
          return STATUS(NotSupported, Substitute("Unknown create mode $0",
                                                 mode));
      }
    } else if (mode == OPEN_EXISTING) {
      return STATUS(IOError, fname, "File not found");
    }

    CreateAndRegisterNewWritableFileUnlocked<Type, Type>(fname, result);
    return Status::OK();
  }

  // Map from filenames to InMemoryFileState objects, representing a simple file system.
  typedef std::map<std::string, std::shared_ptr<InMemoryFileState>> FileSystem;
  Mutex mutex_;
  FileSystem file_map_;  // Protected by mutex_.
};

}  // namespace

Env* NewMemEnv(Env* base_env) {
  return new InMemoryEnv(base_env);
}

} // namespace yb
