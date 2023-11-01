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
//
// Logger implementation that can be shared by all environments
// where enough posix functionality is available.

#pragma once

#include <stdio.h>
#include <time.h>
#include <fcntl.h>

#include <algorithm>
#include <atomic>

#ifdef __linux__
#ifndef FALLOC_FL_KEEP_SIZE
#include <linux/falloc.h>
#endif
#endif

#include "yb/rocksdb/env.h"

#include "yb/rocksdb/port/sys_time.h"

#include "yb/rocksdb/util/io_posix.h"

#include "yb/util/sync_point.h"

#include "yb/util/stats/iostats_context_imp.h"

namespace rocksdb {

const int kDebugLogChunkSize = 128 * 1024;

class PosixLogger : public Logger {
 private:
  FILE* file_;
  uint64_t (*gettid_)();  // Return the thread id for the current thread
  std::atomic_size_t log_size_;
  int fd_;
  static const uint64_t flush_every_seconds_ = 5;
  std::atomic_uint_fast64_t last_flush_micros_;
  Env* env_;
  std::atomic<bool> flush_pending_;
  std::string fname_;
 public:
  PosixLogger(const std::string& fname, FILE* f, uint64_t (*gettid)(), Env* env,
              const InfoLogLevel log_level = InfoLogLevel::ERROR_LEVEL)
      : Logger(log_level),
        file_(f),
        gettid_(gettid),
        log_size_(0),
        fd_(fileno(f)),
        last_flush_micros_(0),
        env_(env),
        flush_pending_(false),
        fname_(fname) {}
  virtual ~PosixLogger() {
    fclose(file_);
  }
  virtual void Flush() override {
    TEST_SYNC_POINT_CALLBACK("PosixLogger::Flush:BeginCallback", nullptr);
    {
      bool expected_flush_pending = true;
      // TODO: use a weaker memory order?
      if (flush_pending_.compare_exchange_strong(expected_flush_pending, false)) {
        fflush(file_);
      }
    }
    last_flush_micros_ = env_->NowMicros();
  }

  using Logger::Logv;
  virtual void Logv(const char* format, va_list ap) override {
    IOSTATS_TIMER_GUARD(logger_nanos);

    const uint64_t thread_id = (*gettid_)();

    // We try twice: the first time with a fixed-size stack allocated buffer,
    // and the second time with a much larger dynamically allocated buffer.
    char buffer[500];
    for (int iter = 0; iter < 2; iter++) {
      char* base;
      int bufsize;
      if (iter == 0) {
        bufsize = sizeof(buffer);
        base = buffer;
      } else {
        bufsize = 30000;
        base = new char[bufsize];
      }
      char* p = base;
      char* limit = base + bufsize;

      struct timeval now_tv;
      gettimeofday(&now_tv, nullptr);
      const time_t seconds = now_tv.tv_sec;
      struct tm t;
      localtime_r(&seconds, &t);
      p += snprintf(p, limit - p,
                    "%04d/%02d/%02d-%02d:%02d:%02d.%06d %" PRIx64 " ",
                    t.tm_year + 1900,
                    t.tm_mon + 1,
                    t.tm_mday,
                    t.tm_hour,
                    t.tm_min,
                    t.tm_sec,
                    static_cast<int>(now_tv.tv_usec),
                    thread_id);

      // Print the message
      if (p < limit) {
        va_list backup_ap;
        va_copy(backup_ap, ap);
        p += vsnprintf(p, limit - p, format, backup_ap);
        va_end(backup_ap);
      }

      // Truncate to available space if necessary
      if (p >= limit) {
        if (iter == 0) {
          continue;       // Try again with larger buffer
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

#ifdef ROCKSDB_FALLOCATE_PRESENT
      // If this write would cross a boundary of kDebugLogChunkSize
      // space, pre-allocate more space to avoid overly large
      // allocations from filesystem allocsize options.
      const size_t log_size = log_size_;
      const size_t last_allocation_chunk =
          ((kDebugLogChunkSize - 1 + log_size) / kDebugLogChunkSize);
      const size_t desired_allocation_chunk =
          ((kDebugLogChunkSize - 1 + log_size + write_size) / kDebugLogChunkSize);
      if (last_allocation_chunk != desired_allocation_chunk) {
        if (fallocate(
                fd_, FALLOC_FL_KEEP_SIZE, 0,
                static_cast<off_t>(desired_allocation_chunk * kDebugLogChunkSize)) != 0) {
          LOG(ERROR) << STATUS_IO_ERROR(fname_, errno)
                     << " desired_allocation_chunk: " << desired_allocation_chunk
                     << " kDebugLogChunkSize: " << kDebugLogChunkSize;
        }
      }
#endif

      size_t sz = fwrite(base, 1, write_size, file_);
      // TODO: use a weaker memory order?
      flush_pending_.store(true);
      assert(sz == write_size);
      if (sz > 0) {
        log_size_ += write_size;
      }
      uint64_t now_micros = static_cast<uint64_t>(now_tv.tv_sec) * 1000000 +
        now_tv.tv_usec;
      if (now_micros - last_flush_micros_ >= flush_every_seconds_ * 1000000) {
        Flush();
      }
      if (base != buffer) {
        delete[] base;
      }
      break;
    }
  }
  size_t GetLogFileSize() const override { return log_size_; }
};

}  // namespace rocksdb
