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
// Copyright (c) 2011-present, Facebook, Inc. All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "yb/rocksdb/util/log_buffer.h"

#include <stdarg.h>

#include "yb/util/logging.h"

#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/port/sys_time.h"

using std::string;

namespace rocksdb {

LogBuffer::LogBuffer(const InfoLogLevel log_level,
                     Logger*info_log)
    : log_level_(log_level), info_log_(info_log) {}

LogBuffer::~LogBuffer() {
  LOG_IF(DFATAL, !IsEmpty())
      << "LogBuffer should be explicitly flushed in order to not lost accumulated log entries.";
}

void LogBuffer::AddLogToBuffer(
    const char* file,
    const int line,
    size_t max_log_size,
    const char* format,
    va_list ap) {
  if (!info_log_ || log_level_ < info_log_->GetInfoLogLevel()) {
    // Skip the level because of its level.
    return;
  }

  char* alloc_mem = arena_.AllocateAligned(max_log_size);
  BufferedLog* buffered_log = new (alloc_mem) BufferedLog();
  char* p = buffered_log->message;
  buffered_log->file_ = file;
  buffered_log->line_ = line;
  char* limit = alloc_mem + max_log_size - 1;

  // store the time
  gettimeofday(&(buffered_log->now_tv), nullptr);

  // Print the message
  if (p < limit) {
    va_list backup_ap;
    va_copy(backup_ap, ap);
    auto n = vsnprintf(p, limit - p + 1, format, backup_ap);
#ifndef OS_WIN
    // MS reports -1 when the buffer is too short
    assert(n >= 0);
#endif
    if (n > 0) {
      p += n;
    } else {
      p = limit;
    }
    va_end(backup_ap);
  }

  if (p > limit) {
    p = limit;
  }

  // Add '\0' to the end
  *p = '\0';

  logs_.push_back(buffered_log);
}

void LogBuffer::FlushBufferToLog() {
  for (BufferedLog* log : logs_) {
    LogWithContext(log->file_, log->line_, log_level_, info_log_, "%s", log->message);
  }
  logs_.clear();
}

void LogToBufferWithContext(
    const char* file,
    const int line,
    LogBuffer* log_buffer,
    size_t max_log_size,
    const char* format,
    ...) {
  if (log_buffer != nullptr) {
    va_list ap;
    va_start(ap, format);
    log_buffer->AddLogToBuffer(file, line, max_log_size, format, ap);
    va_end(ap);
  }
}

void LogToBufferWithContext(
    const char* file,
    const int line,
    LogBuffer* log_buffer,
    const char* format,
    ...) {
  const size_t kDefaultMaxLogSize = 512;
  if (log_buffer != nullptr) {
    va_list ap;
    va_start(ap, format);
    log_buffer->AddLogToBuffer(file, line, kDefaultMaxLogSize, format, ap);
    va_end(ap);
  }
}

}  // namespace rocksdb
