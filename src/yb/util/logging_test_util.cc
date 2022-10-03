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

#include "yb/util/logging_test_util.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/result.h"

namespace yb {

const char* StringWaiterLogSink::kWaitingMessage = "Waiting for log record";

Status StringWaiterLogSink::WaitFor(MonoDelta timeout) {
  constexpr auto kInitialWaitPeriod = 100ms;
  const auto message = Format("$0 '$1'...", kWaitingMessage, string_to_wait_);
  LOG(INFO) << message;
  return ::yb::WaitFor(
      [this] { return event_occurred_.load(); }, timeout, message, kInitialWaitPeriod);
}

void StringWaiterLogSink::send(
    google::LogSeverity severity, const char* full_filename, const char* base_filename, int line,
    const struct ::tm* tm_time, const char* message, size_t message_len) {
  auto log_message = ToString(severity, base_filename, line, tm_time, message, message_len);
  if (log_message.find(string_to_wait_) != std::string::npos &&
      log_message.find(kWaitingMessage) == std::string::npos) {
    event_occurred_ = true;
  }
}

}  // namespace yb
