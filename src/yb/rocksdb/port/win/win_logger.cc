//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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
//
// Logger implementation that can be shared by all environments
// where enough posix functionality is available.

#include "yb/rocksdb/port/win/win_logger.h"

#include <algorithm>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <atomic>

#include "yb/rocksdb/env.h"

#include "yb/rocksdb/port/sys_time.h"
#include "yb/rocksdb/util/iostats_context_imp.h"

namespace rocksdb {

WinLogger::WinLogger(uint64_t (*gettid)(), Env* env, HANDLE file,
                     const InfoLogLevel log_level)
    : Logger(log_level),
      gettid_(gettid),
      log_size_(0),
      last_flush_micros_(0),
      env_(env),
      flush_pending_(false),
      file_(file) {}

void WinLogger::DebugWriter(const char* str, int len) {
  DWORD bytesWritten = 0;
  BOOL ret = WriteFile(file_, str, len, &bytesWritten, NULL);
  if (ret == FALSE) {
    std::string errSz = GetWindowsErrSz(GetLastError());
    fprintf(stderr, errSz.c_str());
  }
}

WinLogger::~WinLogger() { close(); }

void WinLogger::close() { CloseHandle(file_); }

void WinLogger::Flush() {
  if (flush_pending_) {
    flush_pending_ = false;
    // With Windows API writes go to OS buffers directly so no fflush needed
    // unlike with C runtime API. We don't flush all the way to disk
    // for perf reasons.
  }

  last_flush_micros_ = env_->NowMicros();
}

void WinLogger::Logv(const char* format, va_list ap) {
  IOSTATS_TIMER_GUARD(logger_nanos);

  const uint64_t thread_id = (*gettid_)();

  // We try twice: the first time with a fixed-size stack allocated buffer,
  // and the second time with a much larger dynamically allocated buffer.
  char buffer[500];
  std::unique_ptr<char[]> largeBuffer;
  for (int iter = 0; iter < 2; ++iter) {
    char* base;
    int bufsize;
    if (iter == 0) {
      bufsize = sizeof(buffer);
      base = buffer;
    } else {
      bufsize = 30000;
      largeBuffer.reset(new char[bufsize]);
      base = largeBuffer.get();
    }

    char* p = base;
    char* limit = base + bufsize;

    struct timeval now_tv;
    gettimeofday(&now_tv, nullptr);
    const time_t seconds = now_tv.tv_sec;
    struct tm t;
    localtime_s(&t, &seconds);
    p += snprintf(p, limit - p, "%04d/%02d/%02d-%02d:%02d:%02d.%06d %llx ",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour,
                  t.tm_min, t.tm_sec, static_cast<int>(now_tv.tv_usec),
                  static_cast<long long unsigned int>(thread_id));

    // Print the message
    if (p < limit) {
      va_list backup_ap;
      va_copy(backup_ap, ap);
      int done = vsnprintf(p, limit - p, format, backup_ap);
      if (done > 0) {
        p += done;
      } else {
        continue;
      }
      va_end(backup_ap);
    }

    // Truncate to available space if necessary
    if (p >= limit) {
      if (iter == 0) {
        continue;  // Try again with larger buffer
      } else {
        p = limit - 1;
      }
    }

    // Add newline if necessary
    if (p == base || p[-1] != '\n') {
      *p++ = '\n';
    }

    assert(p <= limit);
    const size_t write_size = p - base;

    DWORD bytesWritten = 0;
    BOOL ret = WriteFile(file_, base, static_cast<DWORD>(write_size),
      &bytesWritten, NULL);
    if (ret == FALSE) {
      std::string errSz = GetWindowsErrSz(GetLastError());
      fprintf(stderr, errSz.c_str());
    }

    flush_pending_ = true;
    assert((bytesWritten == write_size) || (ret == FALSE));
    if (bytesWritten > 0) {
      log_size_ += write_size;
    }

    uint64_t now_micros =
        static_cast<uint64_t>(now_tv.tv_sec) * 1000000 + now_tv.tv_usec;
    if (now_micros - last_flush_micros_ >= flush_every_seconds_ * 1000000) {
      flush_pending_ = false;
      // With Windows API writes go to OS buffers directly so no fflush needed
      // unlike with C runtime API. We don't flush all the way to disk
      // for perf reasons.
      last_flush_micros_ = now_micros;
    }
    break;
  }
}

size_t WinLogger::GetLogFileSize() const { return log_size_; }

}  // namespace rocksdb
