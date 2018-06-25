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

#include "yb/util/ybc_util.h"

#include <stdarg.h>

#include "yb/util/ybc-internal.h"
#include "yb/util/logging.h"
#include "yb/util/init.h"
#include "yb/util/version_info.h"
#include "yb/util/status.h"
#include "yb/util/debug-util.h"

using std::string;

namespace yb {

namespace {

Status InitInternal(const char* argv0) {
  RETURN_NOT_OK(CheckCPUFlags());
  yb::InitGoogleLoggingSafeBasic(argv0);
  google::InstallFailureSignalHandler();
  return Status::OK();
}

constexpr size_t kFormattingBufSize = 16384;

} // anonymous namespace

extern "C" {

int YBCInit(const char* server_type, const char* argv0, YBCPAllocFn palloc_fn) {
  YBCSetPAllocFn(palloc_fn);
  yb::Status s = yb::InitInternal(argv0);
  if (s.ok()) {
    return true;
  }
  LOG(WARNING) << "YugaByte initialization failed: " << s;
  return false;
}

#define DEFINE_YBC_LOG_FUNCTION(level_capitalized, level_caps, extra_content) \
  void BOOST_PP_CAT(YBCLog, level_capitalized)(const char* format, ...) { \
    va_list argptr; \
    va_start(argptr, format); \
    char buf[kFormattingBufSize]; \
    vsnprintf(buf, sizeof(buf), format, argptr); \
    va_end(argptr); \
    LOG(level_caps) << buf << extra_content; \
  }

DEFINE_YBC_LOG_FUNCTION(Info, INFO, "")
DEFINE_YBC_LOG_FUNCTION(Warning, WARNING, "")
DEFINE_YBC_LOG_FUNCTION(Error, ERROR, "")
DEFINE_YBC_LOG_FUNCTION(Fatal, FATAL, "")

DEFINE_YBC_LOG_FUNCTION(InfoStackTrace,    INFO,    GetStackTraceWithoutTopFrame())
DEFINE_YBC_LOG_FUNCTION(WarningStackTrace, WARNING, GetStackTraceWithoutTopFrame())
DEFINE_YBC_LOG_FUNCTION(ErrorStackTrace,   ERROR,   GetStackTraceWithoutTopFrame())

#undef DEFINE_YBC_LOG_FUNCTION

YBCStatus YBCTestStatus() {
  auto s = STATUS(IllegalState, "Something not quite right");
  return ToYBCStatus(s);
}

} // extern "C"

} // namespace yb
