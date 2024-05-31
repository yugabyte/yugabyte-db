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

#include "yb/rocksdb/db/auto_roll_logger.h"

#include "yb/rocksdb/util/mutexlock.h"

#include "yb/util/path_util.h"
#include "yb/util/status_log.h"

using std::string;

namespace rocksdb {

AutoRollLogger::AutoRollLogger(Env* env, const std::string& dbname,
               const std::string& db_log_dir, size_t log_max_size,
               size_t log_file_time_to_roll,
               const InfoLogLevel log_level)
    : Logger(log_level),
      dbname_(dbname),
      db_log_dir_(db_log_dir),
      env_(env),
      status_(Status::OK()),
      kMaxLogFileSize(log_max_size),
      kLogFileTimeToRoll(log_file_time_to_roll),
      cached_now(static_cast<uint64_t>(env_->NowMicros() * 1e-6)),
      ctime_(cached_now),
      cached_now_access_count(0),
      call_NowMicros_every_N_records_(100),
      mutex_() {
  CHECK_OK(env->GetAbsolutePath(dbname, &db_absolute_path_));
  log_fname_ = InfoLogFileName(dbname_, db_absolute_path_, db_log_dir_);
  RollLogFile();
  CHECK_OK(ResetLogger());
}

// -- AutoRollLogger
Status AutoRollLogger::ResetLogger() {
  DEBUG_ONLY_TEST_SYNC_POINT("AutoRollLogger::ResetLogger:BeforeNewLogger");
  status_ = env_->NewLogger(log_fname_, &logger_);
  DEBUG_ONLY_TEST_SYNC_POINT("AutoRollLogger::ResetLogger:AfterNewLogger");

  if (!status_.ok()) {
    return status_;
  }

  if (logger_->GetLogFileSize() == Logger::kDoNotSupportGetLogFileSize) {
    status_ = STATUS(NotSupported,
        "The underlying logger doesn't support GetLogFileSize()");
  }
  if (status_.ok()) {
    cached_now = static_cast<uint64_t>(env_->NowMicros() * 1e-6);
    ctime_ = cached_now;
    cached_now_access_count = 0;
  }

  return status_;
}

void AutoRollLogger::RollLogFile() {
  // This function is called when log is rotating. Two rotations
  // can happen quickly (NowMicro returns same value). To not overwrite
  // previous log file we increment by one micro second and try again.
  if (!env_->FileExists(log_fname_).ok()) {
    return;
  }
  uint64_t now = env_->NowMicros();
  std::string old_fname;
  do {
    old_fname = OldInfoLogFileName(
      dbname_, now, db_absolute_path_, db_log_dir_);
    now++;
  } while (env_->FileExists(old_fname).ok());
  CHECK_OK(env_->RenameFile(log_fname_, old_fname));
}

string AutoRollLogger::ValistToString(const char* format, va_list args) const {
  // Any log messages longer than 1024 will get truncated.
  // The user is responsible for chopping longer messages into multi line log
  static const int MAXBUFFERSIZE = 1024;
  char buffer[MAXBUFFERSIZE];

  int count = vsnprintf(buffer, MAXBUFFERSIZE, format, args);
  (void) count;
  assert(count >= 0);

  return buffer;
}

void AutoRollLogger::LogInternal(const char* format, ...) {
  mutex_.AssertHeld();
  va_list args;
  va_start(args, format);
  logger_->Logv(format, args);
  va_end(args);
}

void AutoRollLogger::Logv(const char* format, va_list ap) {
  assert(GetStatus().ok());

  std::shared_ptr<Logger> logger;
  {
    MutexLock l(&mutex_);
    if ((kLogFileTimeToRoll > 0 && LogExpired()) ||
        (kMaxLogFileSize > 0 && logger_->GetLogFileSize() >= kMaxLogFileSize)) {
      RollLogFile();
      Status s = ResetLogger();
      if (!s.ok()) {
        // can't really log the error if creating a new LOG file failed
        return;
      }

      WriteHeaderInfo();
    }

    // pin down the current logger_ instance before releasing the mutex.
    logger = logger_;
  }

  // Another thread could have put a new Logger instance into logger_ by now.
  // However, since logger is still hanging on to the previous instance
  // (reference count is not zero), we don't have to worry about it being
  // deleted while we are accessing it.
  // Note that logv itself is not mutex protected to allow maximum concurrency,
  // as thread safety should have been handled by the underlying logger.
  logger->Logv(format, ap);
}

void AutoRollLogger::WriteHeaderInfo() {
  mutex_.AssertHeld();
  for (auto& header : headers_) {
    LogInternal("%s", header.c_str());
  }
}

void AutoRollLogger::LogHeaderWithContext(const char* file, const int line,
    const char *format, va_list args) {
  // header message are to be retained in memory. Since we cannot make any
  // assumptions about the data contained in va_list, we will retain them as
  // strings
  va_list tmp;
  va_copy(tmp, args);
  string data = ValistToString(format, tmp);
  va_end(tmp);

  MutexLock l(&mutex_);
  headers_.push_back(data);

  // Log the original message to the current log
  logger_->LogvWithContext(file, line, InfoLogLevel::HEADER_LEVEL, format, args);
}

bool AutoRollLogger::LogExpired() {
  if (cached_now_access_count >= call_NowMicros_every_N_records_) {
    cached_now = static_cast<uint64_t>(env_->NowMicros() * 1e-6);
    cached_now_access_count = 0;
  }

  ++cached_now_access_count;
  return cached_now >= ctime_ + kLogFileTimeToRoll;
}

Status CreateDirs(Env* env, const std::string& dir) {
  if (dir == "/" || env->DirExists(dir)) {
    return Status::OK();
  }

  RETURN_NOT_OK(CreateDirs(env, yb::DirName(dir)));
  return env->CreateDir(dir);
}

Status CreateLoggerFromOptions(const std::string& dbname,
                               const DBOptions& options,
                               std::shared_ptr<Logger>* logger) {
  if (options.info_log) {
    *logger = options.info_log;
    return Status::OK();
  }

  Env* env = options.env;
  std::string db_absolute_path;
  RETURN_NOT_OK(env->GetAbsolutePath(dbname, &db_absolute_path));
  std::string fname =
      InfoLogFileName(dbname, db_absolute_path, options.db_log_dir);

  RETURN_NOT_OK(CreateDirs(env, yb::DirName(fname)));  // In case it does not exist
  // Currently we only support roll by time-to-roll and log size
  if (options.log_file_time_to_roll > 0 || options.max_log_file_size > 0) {
    AutoRollLogger* result = new AutoRollLogger(
        env, dbname, options.db_log_dir, options.max_log_file_size,
        options.log_file_time_to_roll, options.info_log_level);
    Status s = result->GetStatus();
    if (!s.ok()) {
      delete result;
    } else {
      logger->reset(result);
    }
    return s;
  } else {
    // Open a log file in the same directory as the db
    if (env->FileExists(fname).ok()) {
      RETURN_NOT_OK(env->RenameFile(
          fname, OldInfoLogFileName(dbname, env->NowMicros(), db_absolute_path,
                                    options.db_log_dir)));
    }
    auto s = env->NewLogger(fname, logger);
    if (logger->get() != nullptr) {
      (*logger)->SetInfoLogLevel(options.info_log_level);
    }
    return s;
  }
}

}  // namespace rocksdb
