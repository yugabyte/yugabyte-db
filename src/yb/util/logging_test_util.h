// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
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

#ifndef YB_UTIL_LOGGING_TEST_UTIL_H
#define YB_UTIL_LOGGING_TEST_UTIL_H

#include <atomic>
#include <chrono>
#include <string>
#include <vector>

#include <glog/logging.h>

#include "yb/util/monotime.h"
#include "yb/util/status_fwd.h"

namespace yb {

using namespace std::literals;

// GLog sink that keeps an internal buffer of messages that have been logged.
class StringVectorSink : public google::LogSink {
 public:
  void send(google::LogSeverity severity, const char* full_filename,
            const char* base_filename, int line,
            const struct ::tm* tm_time,
            const char* message, size_t message_len) override {
    logged_msgs_.push_back(ToString(severity, base_filename, line,
                                    tm_time, message, message_len));
  }

  const std::vector<std::string>& logged_msgs() const {
    return logged_msgs_;
  }

 private:
  std::vector<std::string> logged_msgs_;
};

// GLog sink that waits for specified string to appear in log.
class StringWaiterLogSink : public google::LogSink {
 public:
  explicit StringWaiterLogSink(const std::string& string_to_wait)
      : string_to_wait_(string_to_wait) {
    google::AddLogSink(this);
  }

  // Wait for string_to_wait to occur in log.
  CHECKED_STATUS WaitFor(MonoDelta timeout);

  void send(
      google::LogSeverity severity, const char* full_filename, const char* base_filename, int line,
      const struct ::tm* tm_time, const char* message, size_t message_len) override;

  bool IsEventOccurred() { return event_occurred_; }

  ~StringWaiterLogSink() { google::RemoveLogSink(this); }

 private:
  static const char* kWaitingMessage;
  std::string string_to_wait_;
  std::atomic<bool> event_occurred_{false};
};

// RAII wrapper around registering a LogSink with GLog.
struct ScopedRegisterSink {
  explicit ScopedRegisterSink(google::LogSink* s) : s_(s) {
    google::AddLogSink(s_);
  }
  ~ScopedRegisterSink() {
    google::RemoveLogSink(s_);
  }

  google::LogSink* s_;
};

} // namespace yb

#endif  // YB_UTIL_LOGGING_TEST_UTIL_H
