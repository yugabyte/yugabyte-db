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
#include "yb/util/bytes_formatter.h"
#include "yb/gutil/stringprintf.h"

using std::string;

namespace yb {

namespace {

Status InitInternal(const char* argv0) {
  // Use InitGoogleLoggingSafeBasic() instead of InitGoogleLoggingSafe() to avoid calling
  // google::InstallFailureSignalHandler(). This will prevent interference with PostgreSQL's
  // own signal handling.
  yb::InitGoogleLoggingSafeBasic(argv0);

  // Allow putting gflags into a file and specifying that file's path as an env variable.
  const char* pg_flagfile_path = getenv("YB_PG_FLAGFILE");
  if (pg_flagfile_path) {
    char* arguments[] = {
        const_cast<char*>(argv0),
        const_cast<char*>("--flagfile"),
        const_cast<char*>(pg_flagfile_path)
    };
    char** argv_ptr = arguments;
    int argc = arraysize(arguments);
    gflags::ParseCommandLineFlags(&argc, &argv_ptr, /* remove_flags */ false);
  }

  // Also allow overriding flags on the command line using the appropriate environment variables.
  std::vector<google::CommandLineFlagInfo> flag_infos;
  google::GetAllFlags(&flag_infos);
  for (auto& flag_info : flag_infos) {
    string env_var_name = "FLAGS_" + flag_info.name;
    const char* env_var_value = getenv(env_var_name.c_str());
    if (env_var_value) {
#ifndef NDEBUG
      LOG(INFO) << "Setting flag " << flag_info.name << " to the value of the env var "
                << env_var_name << ": " << env_var_value;
#endif
      google::SetCommandLineOption(flag_info.name.c_str(), env_var_value);
    }
  }

  RETURN_NOT_OK(CheckCPUFlags());

  return Status::OK();
}

} // anonymous namespace

extern "C" {

YBCStatus YBCStatus_OK = nullptr;
bool YBCStatusIsOK(YBCStatus s) {
  return (s == nullptr || s->code == Status::Code::kOk);
}

bool YBCStatusIsNotFound(YBCStatus s) {
  return (s->code == Status::Code::kNotFound);
}

bool YBCStatusIsAlreadyPresent(YBCStatus s) {
  return (s->code == Status::Code::kAlreadyPresent);
}

void YBCFreeStatus(YBCStatus s) {
  FreeYBCStatus(s);
}

YBCStatus YBCInit(const char* argv0,
                  YBCPAllocFn palloc_fn,
                  YBCCStringToTextWithLenFn cstring_to_text_with_len_fn) {
  YBCSetPAllocFn(palloc_fn);
  YBCSetCStringToTextWithLenFn(cstring_to_text_with_len_fn);
  return ToYBCStatus(yb::InitInternal(argv0));
}

void YBCLogImpl(
    google::LogSeverity severity,
    const char* file,
    int line,
    bool with_stack_trace,
    const char* format,
    ...) {
  va_list argptr;
  va_start(argptr, format); \
  string buf;
  StringAppendV(&buf, format, argptr);
  va_end(argptr);
  google::LogMessage log_msg(file, line, severity);
  log_msg.stream() << buf;
  if (with_stack_trace) {
    log_msg.stream() << "\n" << yb::GetStackTrace();
  }
}

const char* YBCFormatBytesAsStr(const char* data, size_t size) {
  return YBCPAllocStdString(util::FormatBytesAsStr(data, size));
}

} // extern "C"

} // namespace yb
