// Copyright (c) YugabyteDB, Inc.
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

// Dumps clockbound's clock information as json.
//
// chronyc does not have enough information to accurately
// determine the clock error in case of PTP. However,
// clockbound fetches information from the PHC device file
// for the same.

extern "C" {
#include <clockbound.h>
}

#include <ctime>
#include <iostream>

#include <rapidjson/document.h>

#include "yb/common/json_util.h"

#include "yb/util/format.h"
#include "yb/util/scope_exit.h"

namespace yb::tools {

namespace {

// Returns exit code 1
//
// Example output:
// {
//   "error": "Segment not initialized"
// }
int DumpError(const std::string& error) {
  rapidjson::Document json_error;
  json_error.SetObject();
  auto& allocator = json_error.GetAllocator();
  common::AddMember("error", error, &json_error, &allocator);
  std::cout << common::PrettyWriteRapidJsonToString(json_error) << std::endl;
  return 1;
}

std::string ClockboundErrToString(const clockbound_err& err) {
  switch (err.kind) {
    case CLOCKBOUND_ERR_NONE:
      return "Internal error: clockbound err cannot be none";
    case CLOCKBOUND_ERR_SYSCALL:
      return Format(
          "clockbound syscall failed with error: $0, and detail: $1",
          strerror(err.sys_errno), err.detail);
    case CLOCKBOUND_ERR_SEGMENT_NOT_INITIALIZED:
      return "Segment not initialized";
    case CLOCKBOUND_ERR_SEGMENT_MALFORMED:
      return "Segment malformed";
    case CLOCKBOUND_ERR_CAUSALITY_BREACH:
      return "Segment and clock reads out of order";
    default:
      return "Internal error: unrecognized clockbound err";
  }
}

std::string ClockStatusToString(const clockbound_clock_status& status) {
  switch (status) {
    case CLOCKBOUND_STA_UNKNOWN:
      return "UNKNOWN";
    case CLOCKBOUND_STA_SYNCHRONIZED:
      return "SYNCHRONIZED";
    case CLOCKBOUND_STA_FREE_RUNNING:
      return "FREE_RUNNING";
    default:
      return "INVALID";
  }
}

void AddTimespecField(
    rapidjson::Value& obj, const char* name, const timespec& ts,
    rapidjson::Document::AllocatorType& allocator) {
  rapidjson::Value timespec_obj(rapidjson::kObjectType);
  timespec_obj.AddMember<int64_t>("tv_sec", ts.tv_sec, allocator);
  timespec_obj.AddMember<int64_t>("tv_nsec", ts.tv_nsec, allocator);
  obj.AddMember(rapidjson::StringRef(name), timespec_obj, allocator);
}

// Returns exit code 0
// Dumps the result of clockbound_now to stdout as json.
int DumpNowResult(const clockbound_now_result& now_result) {
  rapidjson::Document json_now;
  json_now.SetObject();
  auto& allocator = json_now.GetAllocator();
  AddTimespecField(json_now, "earliest", now_result.earliest, allocator);
  AddTimespecField(json_now, "latest", now_result.latest, allocator);
  std::string clock_status = ClockStatusToString(now_result.clock_status);
  common::AddMember("clock_status", clock_status, &json_now, &allocator);
  std::cout << common::PrettyWriteRapidJsonToString(json_now) << std::endl;
  return 0;
}

int DumpClockboundNow() {
  // Open clockbound ctx
  clockbound_err open_err;
  auto ctx = clockbound_open(CLOCKBOUND_SHM_DEFAULT_PATH, &open_err);
  if (ctx == nullptr) {
    return DumpError(ClockboundErrToString(open_err));
  }
  auto scope_exit = ScopeExit([ctx] { clockbound_close(ctx); });
  // Fetch clockbound now.
  clockbound_now_result now_result;
  auto err = clockbound_now(ctx, &now_result);
  if (err != nullptr) {
    return DumpError(ClockboundErrToString(*err));
  }
  return DumpNowResult(now_result);
}

} // anonymous namespace

} // namespace yb::tools

// Example output:
// {
//   "earliest": {
//     "tv_sec": 1633097600,
//     "tv_nsec": 123456789
//   },
//   "latest": {
//     "tv_sec": 1633097700,
//     "tv_nsec": 987654321
//   },
//   "clock_status": "SYNCHRONIZED"
// }
//
// Example error:
// {
//   "error": "Usage: ./clockbound_dump"
// }
int main(int argc, char** argv) {
  // No arguments.
  if (argc != 1) {
    return yb::tools::DumpError("Usage: " + std::string(argv[0]));
  }
  return yb::tools::DumpClockboundNow();
}
