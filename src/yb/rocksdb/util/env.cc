// Copyright (c) YugaByte, Inc.
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
//
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "yb/rocksdb/env.h"

#include <thread>

#include "yb/rocksdb/options.h"

#include "yb/util/result.h"
#include "yb/util/status_log.h"

namespace rocksdb {

Env::~Env() {
}

uint64_t Env::GetThreadID() const {
  std::hash<std::thread::id> hasher;
  return hasher(std::this_thread::get_id());
}

Status Env::ReuseWritableFile(const std::string& fname,
                              const std::string& old_fname,
                              unique_ptr<WritableFile>* result,
                              const EnvOptions& options) {
  Status s = RenameFile(old_fname, fname);
  if (!s.ok()) {
    return s;
  }
  return NewWritableFile(fname, result, options);
}

Status Env::GetChildrenFileAttributes(const std::string& dir,
                                      std::vector<FileAttributes>* result) {
  assert(result != nullptr);
  std::vector<std::string> child_fnames;
  Status s = GetChildren(dir, &child_fnames);
  if (!s.ok()) {
    return s;
  }
  result->resize(child_fnames.size());
  size_t result_size = 0;
  for (size_t i = 0; i < child_fnames.size(); ++i) {
    const std::string path = dir + "/" + child_fnames[i];
    s = GetFileSize(path, &(*result)[result_size].size_bytes);
    if (!s.ok()) {
      if (FileExists(path).IsNotFound()) {
        // The file may have been deleted since we listed the directory
        continue;
      }
      return s;
    }
    (*result)[result_size].name = std::move(child_fnames[i]);
    result_size++;
  }
  result->resize(result_size);
  return Status::OK();
}

yb::Result<uint64_t> Env::GetFileSize(const std::string& fname) {
  uint64_t result;
  RETURN_NOT_OK(GetFileSize(fname, &result));
  return result;
}

bool Env::CleanupFile(const std::string& fname, const std::string& log_prefix) {
  Status s;
  WARN_NOT_OK(s = DeleteFile(fname), log_prefix + "Failed to cleanup " + fname);
  return s.ok();
}

void Env::GetChildrenWarnNotOk(const std::string& dir,
                               std::vector<std::string>* result) {
  WARN_NOT_OK(GetChildren(dir, result), "Failed to get children " + dir);
}

WritableFile::~WritableFile() {
}

Logger::~Logger() {
}

FileLock::~FileLock() {
}

void LogFlush(Logger *info_log) {
  if (info_log) {
    info_log->Flush();
  }
}

void LogWithContext(const char* file,
                    const int line,
                    Logger* info_log,
                    const char* format,
                    ...) {
  if (info_log && info_log->GetInfoLogLevel() <= InfoLogLevel::INFO_LEVEL) {
    va_list ap;
    va_start(ap, format);
    info_log->LogvWithContext(file, line, InfoLogLevel::INFO_LEVEL, format, ap);
    va_end(ap);
  }
}

void Logger::Logv(const InfoLogLevel log_level, const char* format, va_list ap) {
  LogvWithContext(__FILE__, __LINE__, log_level, format, ap);
}

void Logger::LogvWithContext(const char* file,
    const int line,
    const InfoLogLevel log_level,
    const char* format,
    va_list ap) {
  static const char* kInfoLogLevelNames[6] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL", "HEADER"};
  static_assert(
      sizeof(kInfoLogLevelNames) / sizeof(kInfoLogLevelNames[0]) == NUM_INFO_LOG_LEVELS,
      "kInfoLogLevelNames must have an element for each log level");
  if (log_level < log_level_) {
    return;
  }

  if (log_level == InfoLogLevel::INFO_LEVEL) {
    // Doesn't print log level if it is INFO level.
    // This is to avoid unexpected performance regression after we add
    // the feature of log level. All the logs before we add the feature
    // are INFO level. We don't want to add extra costs to those existing
    // logging.
    Logv(format, ap);
  } else {
    char new_format[500];
    snprintf(new_format, sizeof(new_format) - 1, "[%s] %s",
        kInfoLogLevelNames[log_level], format);
    Logv(new_format, ap);
  }
}


void LogWithContext(const char* file,
                    const int line,
                    const InfoLogLevel log_level,
                    Logger* info_log,
                    const char* format,
                    ...) {
  if (info_log && info_log->GetInfoLogLevel() <= log_level) {
    va_list ap;
    va_start(ap, format);

    if (log_level == InfoLogLevel::HEADER_LEVEL) {
      info_log->LogHeaderWithContext(file, line, format, ap);
    } else {
      info_log->LogvWithContext(file, line, log_level, format, ap);
    }

    va_end(ap);
  }
}

void HeaderWithContext(const char* file, const int line,
    Logger *info_log, const char *format, ...) {
  if (info_log) {
    va_list ap;
    va_start(ap, format);
    info_log->LogHeaderWithContext(file, line, format, ap);
    va_end(ap);
  }
}

void DebugWithContext(const char* file, const int line,
    Logger *info_log, const char *format, ...) {
// Log level should be higher than DEBUG, but including the ifndef for compiletime optimization.
#ifndef NDEBUG
  if (info_log && info_log->GetInfoLogLevel() <= InfoLogLevel::DEBUG_LEVEL) {
    va_list ap;
    va_start(ap, format);
    info_log->LogvWithContext(file, line, InfoLogLevel::DEBUG_LEVEL, format, ap);
    va_end(ap);
  }
#endif
}

void InfoWithContext(const char* file, const int line,
    Logger *info_log, const char *format, ...) {
  if (info_log && info_log->GetInfoLogLevel() <= InfoLogLevel::INFO_LEVEL) {
    va_list ap;
    va_start(ap, format);
    info_log->LogvWithContext(file, line, InfoLogLevel::INFO_LEVEL, format, ap);
    va_end(ap);
  }
}

void WarnWithContext(const char* file, const int line,
    Logger *info_log, const char *format, ...) {
  if (info_log && info_log->GetInfoLogLevel() <= InfoLogLevel::WARN_LEVEL) {
    va_list ap;
    va_start(ap, format);
    info_log->LogvWithContext(file, line, InfoLogLevel::WARN_LEVEL, format, ap);
    va_end(ap);
  }
}
void ErrorWithContext(const char* file, const int line,
    Logger *info_log, const char *format, ...) {
  if (info_log && info_log->GetInfoLogLevel() <= InfoLogLevel::ERROR_LEVEL) {
    va_list ap;
    va_start(ap, format);
    info_log->LogvWithContext(file, line, InfoLogLevel::ERROR_LEVEL, format, ap);
    va_end(ap);
  }
}
void FatalWithContext(const char* file, const int line,
    Logger *info_log, const char *format, ...) {
  if (info_log && info_log->GetInfoLogLevel() <= InfoLogLevel::FATAL_LEVEL) {
    va_list ap;
    va_start(ap, format);
    info_log->LogvWithContext(file, line, InfoLogLevel::FATAL_LEVEL, format, ap);
    va_end(ap);
  }
}

void LogFlush(const shared_ptr<Logger>& info_log) {
  if (info_log) {
    info_log->Flush();
  }
}

void LogWithContext(const char* file,
                    const int line,
                    const InfoLogLevel log_level,
                    const shared_ptr<Logger>& info_log,
                    const char* format,
                    ...) {
  if (info_log) {
    va_list ap;
    va_start(ap, format);
    info_log->LogvWithContext(file, line, log_level, format, ap);
    va_end(ap);
  }
}

void HeaderWithContext(
    const char* file,
    const int line,
    const shared_ptr<Logger> &info_log,
    const char *format, ...) {
  if (info_log) {
    va_list ap;
    va_start(ap, format);
    info_log->LogHeaderWithContext(file, line, format, ap);
    va_end(ap);
  }
}

void DebugWithContext(
    const char* file,
    const int line,
    const shared_ptr<Logger> &info_log,
    const char *format, ...) {
  if (info_log) {
    va_list ap;
    va_start(ap, format);
    info_log->LogvWithContext(file, line, InfoLogLevel::DEBUG_LEVEL, format, ap);
    va_end(ap);
  }
}

void InfoWithContext(
    const char* file,
    const int line,
    const shared_ptr<Logger> &info_log,
    const char *format, ...) {
  if (info_log) {
    va_list ap;
    va_start(ap, format);
    info_log->LogvWithContext(file, line, InfoLogLevel::INFO_LEVEL, format, ap);
    va_end(ap);
  }
}

void WarnWithContext(
    const char* file,
    const int line,
    const shared_ptr<Logger> &info_log,
    const char *format, ...) {
  if (info_log) {
    va_list ap;
    va_start(ap, format);
    info_log->LogvWithContext(file, line, InfoLogLevel::WARN_LEVEL, format, ap);
    va_end(ap);
  }
}

void ErrorWithContext(
    const char* file,
    const int line,
    const shared_ptr<Logger> &info_log,
    const char *format, ...) {
  if (info_log) {
    va_list ap;
    va_start(ap, format);
    info_log->LogvWithContext(file, line, InfoLogLevel::ERROR_LEVEL, format, ap);
    va_end(ap);
  }
}

void FatalWithContext(
    const char* file,
    const int line,
    const shared_ptr<Logger> &info_log,
    const char *format, ...) {
  if (info_log) {
    va_list ap;
    va_start(ap, format);
    info_log->LogvWithContext(file, line, InfoLogLevel::FATAL_LEVEL, format, ap);
    va_end(ap);
  }
}

void LogWithContext(const char* file,
                    const int line,
                    const shared_ptr<Logger>& info_log,
                    const char* format,
                    ...) {
  if (info_log) {
    va_list ap;
    va_start(ap, format);
    info_log->LogvWithContext(file, line, InfoLogLevel::INFO_LEVEL, format, ap);
    va_end(ap);
  }
}

Status WriteStringToFile(Env* env, const Slice& data, const std::string& fname,
                         bool should_sync) {
  unique_ptr<WritableFile> file;
  EnvOptions soptions;
  Status s = env->NewWritableFile(fname, &file, soptions);
  if (!s.ok()) {
    return s;
  }
  s = file->Append(data);
  if (s.ok() && should_sync) {
    s = file->Sync();
  }
  if (!s.ok()) {
    env->CleanupFile(fname);
  }
  return s;
}

Status ReadFileToString(Env* env, const std::string& fname, std::string* data) {
  EnvOptions soptions;
  data->clear();
  std::unique_ptr<SequentialFile> file;
  Status s = env->NewSequentialFile(fname, &file, soptions);
  if (!s.ok()) {
    return s;
  }
  static const int kBufferSize = 8192;
  std::unique_ptr<uint8_t[]> space(new uint8_t[kBufferSize]);
  while (true) {
    Slice fragment;
    s = file->Read(kBufferSize, &fragment, space.get());
    if (!s.ok()) {
      break;
    }
    data->append(fragment.cdata(), fragment.size());
    if (fragment.empty()) {
      break;
    }
  }
  return s;
}

Status EnvWrapper::NewSequentialFile(const std::string& f, std::unique_ptr<SequentialFile>* r,
                                     const EnvOptions& options) {
  return target_->NewSequentialFile(f, r, options);
}

Status EnvWrapper::NewRandomAccessFile(const std::string& f,
                                       unique_ptr<RandomAccessFile>* r,
                                       const EnvOptions& options) {
  return target_->NewRandomAccessFile(f, r, options);
}

Status EnvWrapper::NewWritableFile(const std::string& f, unique_ptr<WritableFile>* r,
                                   const EnvOptions& options) {
  return target_->NewWritableFile(f, r, options);
}

Status EnvWrapper::ReuseWritableFile(const std::string& fname,
                                     const std::string& old_fname,
                                     unique_ptr<WritableFile>* r,
                                     const EnvOptions& options) {
  return target_->ReuseWritableFile(fname, old_fname, r, options);
}

Status EnvWrapper::NewDirectory(const std::string& name,
                                unique_ptr<Directory>* result) {
  return target_->NewDirectory(name, result);
}

Status EnvWrapper::FileExists(const std::string& f) {
  return target_->FileExists(f);
}

Status EnvWrapper::GetChildren(const std::string& dir,
                               std::vector<std::string>* r) {
  return target_->GetChildren(dir, r);
}

Status EnvWrapper::GetChildrenFileAttributes(
    const std::string& dir, std::vector<FileAttributes>* result) {
  return target_->GetChildrenFileAttributes(dir, result);
}

Status EnvWrapper::DeleteFile(const std::string& f) {
  return target_->DeleteFile(f);
}

Status EnvWrapper::CreateDir(const std::string& d) {
  return target_->CreateDir(d);
}

Status EnvWrapper::CreateDirIfMissing(const std::string& d) {
  return target_->CreateDirIfMissing(d);
}

Status EnvWrapper::DeleteDir(const std::string& d) {
  return target_->DeleteDir(d);
}

Status EnvWrapper::GetFileSize(const std::string& f, uint64_t* s) {
  return target_->GetFileSize(f, s);
}

Status EnvWrapper::GetFileModificationTime(const std::string& fname,
                               uint64_t* file_mtime) {
  return target_->GetFileModificationTime(fname, file_mtime);
}

Status EnvWrapper::RenameFile(const std::string& s, const std::string& t) {
  return target_->RenameFile(s, t);
}

Status EnvWrapper::LinkFile(const std::string& s, const std::string& t) {
  return target_->LinkFile(s, t);
}

Status EnvWrapper::LockFile(const std::string& f, FileLock** l) {
  return target_->LockFile(f, l);
}

Status EnvWrapper::UnlockFile(FileLock* l) {
  return target_->UnlockFile(l);
}

Status EnvWrapper::GetTestDirectory(std::string* path) {
  return target_->GetTestDirectory(path);
}

Status EnvWrapper::NewLogger(const std::string& fname,
                             shared_ptr<Logger>* result) {
  return target_->NewLogger(fname, result);
}

Status EnvWrapper::GetHostName(char* name, uint64_t len) {
  return target_->GetHostName(name, len);
}

Status EnvWrapper::GetCurrentTime(int64_t* unix_time) {
  return target_->GetCurrentTime(unix_time);
}

Status EnvWrapper::GetAbsolutePath(const std::string& db_path,
                                   std::string* output_path) {
  return target_->GetAbsolutePath(db_path, output_path);
}

EnvWrapper::~EnvWrapper() {
}

namespace {  // anonymous namespace

void AssignEnvOptions(EnvOptions* env_options, const DBOptions& options) {
  env_options->use_os_buffer = options.allow_os_buffer;
  env_options->use_mmap_reads = options.allow_mmap_reads;
  env_options->use_mmap_writes = options.allow_mmap_writes;
  env_options->set_fd_cloexec = options.is_fd_close_on_exec;
  env_options->bytes_per_sync = options.bytes_per_sync;
  env_options->compaction_readahead_size = options.compaction_readahead_size;
  env_options->random_access_max_buffer_size =
      options.random_access_max_buffer_size;
  env_options->rate_limiter = options.rate_limiter.get();
  env_options->writable_file_max_buffer_size =
      options.writable_file_max_buffer_size;
  env_options->allow_fallocate = options.allow_fallocate;
}

}  // anonymous namespace

EnvOptions Env::OptimizeForLogWrite(const EnvOptions& env_options,
                                    const DBOptions& db_options) const {
  EnvOptions optimized_env_options(env_options);
  optimized_env_options.bytes_per_sync = db_options.wal_bytes_per_sync;
  return optimized_env_options;
}

EnvOptions Env::OptimizeForManifestWrite(const EnvOptions& env_options) const {
  return env_options;
}

Status Env::LinkFile(const std::string& src, const std::string& target) {
  return STATUS(NotSupported, "LinkFile is not supported for this Env");
}

EnvOptions::EnvOptions(const DBOptions& options) {
  AssignEnvOptions(this, options);
}

EnvOptions::EnvOptions() {
  DBOptions options;
  AssignEnvOptions(this, options);
}

void WritableFile::PrepareWrite(size_t offset, size_t len) {
  if (preallocation_block_size_ == 0) {
    return;
  }
  // If this write would cross one or more preallocation blocks,
  // determine what the last preallocation block necesessary to
  // cover this write would be and Allocate to that point.
  const auto block_size = preallocation_block_size_;
  size_t new_last_preallocated_block =
    (offset + len + block_size - 1) / block_size;
  if (new_last_preallocated_block > last_preallocated_block_) {
    size_t num_spanned_blocks =
      new_last_preallocated_block - last_preallocated_block_;
    WARN_NOT_OK(
        Allocate(block_size * last_preallocated_block_, block_size * num_spanned_blocks),
        "Failed to pre-allocate space for a file");
    last_preallocated_block_ = new_last_preallocated_block;
  }
}

Status WritableFile::PositionedAppend(const Slice& /* data */, uint64_t /* offset */) {
  return STATUS(NotSupported, "PositionedAppend not supported");
}

// Truncate is necessary to trim the file to the correct size
// before closing. It is not always possible to keep track of the file
// size due to whole pages writes. The behavior is undefined if called
// with other writes to follow.
Status WritableFile::Truncate(uint64_t size) {
  return Status::OK();
}

Status WritableFile::RangeSync(uint64_t offset, uint64_t nbytes) {
  return Status::OK();
}

Status WritableFile::Fsync() {
  return Sync();
}

Status WritableFile::InvalidateCache(size_t offset, size_t length) {
  return STATUS(NotSupported, "InvalidateCache not supported.");
}

Status WritableFile::Allocate(uint64_t offset, uint64_t len) {
  return Status::OK();
}

Status RocksDBFileFactoryWrapper::NewSequentialFile(
    const std::string& f, unique_ptr<SequentialFile>* r,
    const rocksdb::EnvOptions& options) {
  return target_->NewSequentialFile(f, r, options);
}
Status RocksDBFileFactoryWrapper::NewRandomAccessFile(const std::string& f,
                                                      unique_ptr <rocksdb::RandomAccessFile>* r,
                                                      const EnvOptions& options) {
  return target_->NewRandomAccessFile(f, r, options);
}

Status RocksDBFileFactoryWrapper::NewWritableFile(
    const std::string& f, unique_ptr <rocksdb::WritableFile>* r,
    const EnvOptions& options) {
  return target_->NewWritableFile(f, r, options);
}

Status RocksDBFileFactoryWrapper::ReuseWritableFile(const std::string& fname,
                                                    const std::string& old_fname,
                                                    unique_ptr<WritableFile>* result,
                                                    const EnvOptions& options) {
  return target_->ReuseWritableFile(fname, old_fname, result, options);
}

Status RocksDBFileFactoryWrapper::GetFileSize(const std::string& fname, uint64_t* size) {
  return target_->GetFileSize(fname, size);
}

Status WritableFileWrapper::Append(const Slice& data) {
  return target_->Append(data);
}

Status WritableFileWrapper::PositionedAppend(const Slice& data, uint64_t offset) {
  return target_->PositionedAppend(data, offset);
}

Status WritableFileWrapper::Truncate(uint64_t size) {
  return target_->Truncate(size);
}

Status WritableFileWrapper::Close() {
  return target_->Close();
}

Status WritableFileWrapper::Flush() {
  return target_->Flush();
}

Status WritableFileWrapper::Sync() {
  return target_->Sync();
}

Status WritableFileWrapper::Fsync() {
  return target_->Fsync();
}

Status WritableFileWrapper::InvalidateCache(size_t offset, size_t length) {
  return target_->InvalidateCache(offset, length);
}

Status WritableFileWrapper::Allocate(uint64_t offset, uint64_t len) {
  return target_->Allocate(offset, len);
}

Status WritableFileWrapper::RangeSync(uint64_t offset, uint64_t nbytes) {
  return target_->RangeSync(offset, nbytes);
}

}  // namespace rocksdb
