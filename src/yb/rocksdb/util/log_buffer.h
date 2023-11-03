// Copyright (c) 2011-present, Facebook, Inc. All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
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


#pragma once

#include "yb/rocksdb/env.h"
#include "yb/rocksdb/util/arena.h"
#include "yb/rocksdb/util/autovector.h"

#define LOG_TO_BUFFER(...) LogToBufferWithContext(__FILE__, __LINE__, ##__VA_ARGS__)

namespace rocksdb {

class Logger;

// A class to buffer info log entries and flush them in the end.
class LogBuffer {
 public:
  // log_level: the log level for all the logs
  // info_log:  logger to write the logs to
  LogBuffer(const InfoLogLevel log_level, Logger* info_log);

  ~LogBuffer();

  // Add a log entry to the buffer. Use default max_log_size.
  // max_log_size indicates maximize log size, including some metadata.
  void AddLogToBuffer(
      const char* file,
      const int line,
      size_t max_log_size,
      const char* format,
      va_list ap);

  size_t IsEmpty() const { return logs_.empty(); }

  // Flush all buffered log to the info log.
  void FlushBufferToLog();

  static constexpr size_t HeaderSize() {
    return offsetof(BufferedLog, message);
  }
 private:
  // One log entry with its timestamp
  struct BufferedLog {
    const char* file_;
    int line_;
    struct timeval now_tv;  // Timestamp of the log
    char message[1];        // Beginning of log message
  };

  const InfoLogLevel log_level_;
  Logger* info_log_;
  Arena arena_;
  autovector<BufferedLog*> logs_;
};

// Add log to the LogBuffer for a delayed info logging. It can be used when
// we want to add some logs inside a mutex.
// max_log_size indicates maximize log size, including some metadata.
extern void LogToBufferWithContext(
    const char* file,
    const int line,
    LogBuffer* log_buffer,
    size_t max_log_size,
    const char* format,
    ...);
// Same as previous function, but with default max log size.
extern void LogToBufferWithContext(
    const char* file,
    const int line,
    LogBuffer* log_buffer,
    const char* format,
    ...);

}  // namespace rocksdb
