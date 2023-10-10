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
// Logger implementation that can be shared by all environments
// where enough posix functionality is available.


#pragma once

#include <assert.h>

#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <stack>
#include <string>
#include <type_traits>

#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/port/util_logger.h"
#include "yb/rocksdb/util/mutexlock.h"

#include "yb/util/cache_metrics.h"
#include "yb/util/sync_point.h"

namespace rocksdb {

// Rolls the log file by size and/or time
class AutoRollLogger : public Logger {
 public:
  AutoRollLogger(Env* env, const std::string& dbname,
                 const std::string& db_log_dir, size_t log_max_size,
                 size_t log_file_time_to_roll,
                 const InfoLogLevel log_level = InfoLogLevel::INFO_LEVEL);

  using Logger::Logv;
  void Logv(const char* format, va_list ap) override;

  // Write a header entry to the log. All header information will be written
  // again every time the log rolls over.
  virtual void LogHeaderWithContext(const char* file, const int line,
      const char *format, va_list ap) override;

  // check if the logger has encountered any problem.
  Status GetStatus() {
    return status_;
  }

  size_t GetLogFileSize() const override {
    std::shared_ptr<Logger> logger;
    {
      MutexLock l(&mutex_);
      // pin down the current logger_ instance before releasing the mutex.
      logger = logger_;
    }
    return logger->GetLogFileSize();
  }

  void Flush() override {
    std::shared_ptr<Logger> logger;
    {
      MutexLock l(&mutex_);
      // pin down the current logger_ instance before releasing the mutex.
      logger = logger_;
    }
    DEBUG_ONLY_TEST_SYNC_POINT("AutoRollLogger::Flush:PinnedLogger");
    if (logger) {
      logger->Flush();
    }
  }

  virtual ~AutoRollLogger() {
  }

  void SetCallNowMicrosEveryNRecords(uint64_t call_NowMicros_every_N_records) {
    call_NowMicros_every_N_records_ = call_NowMicros_every_N_records;
  }

  // Expose the log file path for testing purpose
  std::string TEST_log_fname() const {
    return log_fname_;
  }

 private:
  bool LogExpired();
  Status ResetLogger();
  void RollLogFile();
  // Log message to logger without rolling
  void LogInternal(const char* format, ...);
  // Serialize the va_list to a string
  std::string ValistToString(const char* format, va_list args) const;
  // Write the logs marked as headers to the new log file
  void WriteHeaderInfo();

  std::string log_fname_; // Current active info log's file name.
  std::string dbname_;
  std::string db_log_dir_;
  std::string db_absolute_path_;
  Env* env_;
  std::shared_ptr<Logger> logger_;
  // current status of the logger
  Status status_;
  const size_t kMaxLogFileSize;
  const size_t kLogFileTimeToRoll;
  // header information
  std::list<std::string> headers_;
  // to avoid frequent env->NowMicros() calls, we cached the current time
  uint64_t cached_now;
  uint64_t ctime_;
  uint64_t cached_now_access_count;
  uint64_t call_NowMicros_every_N_records_;
  mutable port::Mutex mutex_;
};

// Facade to craete logger automatically
Status CreateLoggerFromOptions(const std::string& dbname,
                               const DBOptions& options,
                               std::shared_ptr<Logger>* logger);

}  // namespace rocksdb
