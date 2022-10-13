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
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

using std::string;

namespace yb {

Env::~Env() {
}

Status EnvWrapper::Env::CreateDirs(const std::string& dirname) {
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

Status Env::GetChildren(const std::string& dir, std::vector<std::string>* result) {
  return GetChildren(dir, ExcludeDots::kFalse, result);
}

Result<std::vector<std::string>> Env::GetChildren(
    const std::string& dir, ExcludeDots exclude_dots) {
  std::vector<std::string> result;
  RETURN_NOT_OK(GetChildren(dir, exclude_dots, &result));
  return result;
}

Result<std::string> Env::GetTestDirectory() {
  std::string test_dir;
  RETURN_NOT_OK(GetTestDirectory(&test_dir));
  return test_dir;
}

Result<bool> Env::IsDirectory(const std::string& path) {
  bool result = false;
  RETURN_NOT_OK(IsDirectory(path, &result));
  return result;
}

// Like IsDirectory, but non-existence of the given path is not considered an error.
Result<bool> Env::DoesDirectoryExist(const std::string& path) {
  bool result = false;
  Status status = IsDirectory(path, &result);
  if (status.IsNotFound()) {
    return false;
  }
  if (!status.ok()) {
    return status;
  }
  return result;
}

Result<std::string> Env::Canonicalize(const std::string& path) {
  string result;
  RETURN_NOT_OK(Canonicalize(path, &result));
  return result;
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

Status FileFactoryWrapper::NewSequentialFile(const std::string& fname,
                                             std::unique_ptr<SequentialFile>* result) {
  return target_->NewSequentialFile(fname, result);
}

Status FileFactoryWrapper::NewRandomAccessFile(const std::string& fname,
                                               std::unique_ptr<RandomAccessFile>* result) {
  return target_->NewRandomAccessFile(fname, result);
}

Status FileFactoryWrapper::NewWritableFile(const std::string& fname,
                                           std::unique_ptr<WritableFile>* result) {
  return target_->NewWritableFile(fname, result);
}

Status FileFactoryWrapper::NewWritableFile(const WritableFileOptions& opts,
                                           const std::string& fname,
                                           std::unique_ptr<WritableFile>* result) {
  return target_->NewWritableFile(opts, fname, result);
}

Status FileFactoryWrapper::NewTempWritableFile(const WritableFileOptions& opts,
                                               const std::string& name_template,
                                               std::string* created_filename,
                                               std::unique_ptr<WritableFile>* result) {
  return target_->NewTempWritableFile(opts, name_template, created_filename, result);
}

Status FileFactoryWrapper::NewRWFile(const std::string& fname,
                                     std::unique_ptr<RWFile>* result) {
  return target_->NewRWFile(fname, result);
}

// Like the previous NewRWFile, but allows options to be specified.
Status FileFactoryWrapper::NewRWFile(const RWFileOptions& opts,
                                     const std::string& fname,
                                     std::unique_ptr<RWFile>* result) {
  return target_->NewRWFile(opts, fname, result);
}

Status WritableFile::AppendSlices(const Slice* begin, const Slice* end) {
  return AppendSlices(begin, end - begin);
}

Result<uint64_t> FileFactoryWrapper::GetFileSize(const std::string& fname) {
  return target_->GetFileSize(fname);
}

Status WritableFileWrapper::PreAllocate(uint64_t size) {
  return target_->PreAllocate(size);
}

Status WritableFileWrapper::Append(const Slice& data) {
  return target_->Append(data);
}

Status WritableFileWrapper::AppendSlices(const Slice* slices, size_t num) {
  return target_->AppendSlices(slices, num);
}

Status WritableFileWrapper::Close() {
  return target_->Close();
}

Status WritableFileWrapper::Flush(FlushMode mode) {
  return target_->Flush(mode);
}

Status WritableFileWrapper::Sync() {
  return target_->Sync();
}

Status EnvWrapper::NewSequentialFile(const std::string& f,
    std::unique_ptr<SequentialFile>* r) {
  return target_->NewSequentialFile(f, r);
}

Status EnvWrapper::NewRandomAccessFile(const std::string& f,
                                       std::unique_ptr<RandomAccessFile>* r) {
  return target_->NewRandomAccessFile(f, r);
}
Status EnvWrapper::NewWritableFile(const std::string& f, std::unique_ptr<WritableFile>* r) {
  return target_->NewWritableFile(f, r);
}

Status EnvWrapper::NewWritableFile(const WritableFileOptions& o,
                                   const std::string& f,
                                   std::unique_ptr<WritableFile>* r) {
  return target_->NewWritableFile(o, f, r);
}

Status EnvWrapper::NewTempWritableFile(const WritableFileOptions& o, const std::string& t,
                                       std::string* f, std::unique_ptr<WritableFile>* r) {
  return target_->NewTempWritableFile(o, t, f, r);
}

Status EnvWrapper::NewRWFile(const std::string& f, std::unique_ptr<RWFile>* r) {
  return target_->NewRWFile(f, r);
}

Status EnvWrapper::NewRWFile(const RWFileOptions& o,
                             const std::string& f,
                             std::unique_ptr<RWFile>* r) {
  return target_->NewRWFile(o, f, r);
}

Status EnvWrapper::GetChildren(
    const std::string& dir, ExcludeDots exclude_dots, std::vector<std::string>* r) {
  return target_->GetChildren(dir, exclude_dots, r);
}

Status EnvWrapper::DeleteFile(const std::string& f) {
  return target_->DeleteFile(f);
}

Status EnvWrapper::CreateDir(const std::string& d) {
  return target_->CreateDir(d);
}

Status EnvWrapper::SyncDir(const std::string& d) {
  return target_->SyncDir(d);
}

Status EnvWrapper::DeleteDir(const std::string& d) {
  return target_->DeleteDir(d);
}

Status EnvWrapper::DeleteRecursively(const std::string& d) {
  return target_->DeleteRecursively(d);
}

Result<uint64_t> EnvWrapper::GetFileSize(const std::string& f) {
  return target_->GetFileSize(f);
}

Result<uint64_t> EnvWrapper::GetFileINode(const std::string& f) {
  return target_->GetFileINode(f);
}

Result<uint64_t> EnvWrapper::GetFileSizeOnDisk(const std::string& f) {
  return target_->GetFileSizeOnDisk(f);
}

Result<uint64_t> EnvWrapper::GetBlockSize(const std::string& f) {
  return target_->GetBlockSize(f);
}

Result<Env::FilesystemStats> EnvWrapper::GetFilesystemStatsBytes(const std::string& f) {
  return target_->GetFilesystemStatsBytes(f);
}

Status EnvWrapper::LinkFile(const std::string& s, const std::string& t) {
  return target_->LinkFile(s, t);
}

Result<std::string> EnvWrapper::ReadLink(const std::string& s) {
  return target_->ReadLink(s);
}

Status EnvWrapper::RenameFile(const std::string& s, const std::string& t) {
  return target_->RenameFile(s, t);
}

Status EnvWrapper::LockFile(const std::string& f, FileLock** l, bool r) {
  return target_->LockFile(f, l, r);
}

Status EnvWrapper::UnlockFile(FileLock* l) {
  return target_->UnlockFile(l);
}

Status EnvWrapper::GetTestDirectory(std::string* path) {
  return target_->GetTestDirectory(path);
}

Status EnvWrapper::GetExecutablePath(std::string* path) {
  return target_->GetExecutablePath(path);
}

Status EnvWrapper::IsDirectory(const std::string& path, bool* is_dir) {
  return target_->IsDirectory(path, is_dir);
}

Result<bool> EnvWrapper::IsExecutableFile(const std::string& path) {
  return target_->IsExecutableFile(path);
}

Status EnvWrapper::Walk(const std::string& root,
            DirectoryOrder order,
            const WalkCallback& cb) {
  return target_->Walk(root, order, cb);
}

Status EnvWrapper::Canonicalize(const std::string& path, std::string* result) {
  return target_->Canonicalize(path, result);
}

Status EnvWrapper::GetTotalRAMBytes(int64_t* ram) {
  return target_->GetTotalRAMBytes(ram);
}

Result<uint64_t> EnvWrapper::GetFreeSpaceBytes(const std::string& path) {
  return target_->GetFreeSpaceBytes(path);
}

Result<ResourceLimits> EnvWrapper::GetUlimit(int resource) {
  return target_->GetUlimit(resource);
}

Status EnvWrapper::SetUlimit(int resource, ResourceLimit value) {
  return target_->SetUlimit(resource, value);
}

Status EnvWrapper::SetUlimit(
    int resource, ResourceLimit value, const std::string& resource_name) {
  return target_->SetUlimit(resource, value, resource_name);
}

}  // namespace yb
