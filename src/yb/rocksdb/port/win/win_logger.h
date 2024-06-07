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

#include <atomic>

#include "yb/rocksdb/env.h"

#include <stdint.h>
#include <Windows.h>

namespace rocksdb {

class Env;

const int kDebugLogChunkSize = 128 * 1024;

class WinLogger : public rocksdb::Logger {
 public:
  WinLogger(uint64_t (*gettid)(), Env* env, HANDLE file,
            const InfoLogLevel log_level = InfoLogLevel::ERROR_LEVEL);

  virtual ~WinLogger();

  WinLogger(const WinLogger&) = delete;

  WinLogger& operator=(const WinLogger&) = delete;

  void close();

  void Flush() override;

  void Logv(const char* format, va_list ap) override;

  size_t GetLogFileSize() const override;

  void DebugWriter(const char* str, int len);

 private:
  HANDLE file_;
  uint64_t (*gettid_)();  // Return the thread id for the current thread
  std::atomic_size_t log_size_;
  std::atomic_uint_fast64_t last_flush_micros_;
  Env* env_;
  bool flush_pending_;

  const static uint64_t flush_every_seconds_ = 5;
};

}  // namespace rocksdb
