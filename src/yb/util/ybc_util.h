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

// C wrappers around some YB utilities. Suitable for inclusion into C codebases such as our modified
// version of PostgreSQL.

#ifndef YB_UTIL_YBC_UTIL_H
#define YB_UTIL_YBC_UTIL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {

struct varlena;

#endif

typedef struct YBCStatusStruct {
  int code;
  char msg[0];
} YBCStatusStruct;

typedef struct YBCStatusStruct* YBCStatus;

extern YBCStatus YBCStatusOK;
bool YBCStatusIsOK(YBCStatus s);
bool YBCStatusIsNotFound(YBCStatus s);
void YBCFreeStatus(YBCStatus s);

#define CHECKED_YBCSTATUS __attribute__ ((warn_unused_result)) YBCStatus

typedef void* (*YBCPAllocFn)(size_t size);

typedef struct varlena* (*YBCCStringToTextWithLenFn)(const char* c, int size);

// Global initialization of the YugaByte subsystem.
CHECKED_YBCSTATUS YBCInit(
    const char* argv0,
    YBCPAllocFn palloc_fn,
    YBCCStringToTextWithLenFn cstring_to_text_with_len_fn);

// From glog's log_severity.h:
// const int GLOG_INFO = 0, GLOG_WARNING = 1, GLOG_ERROR = 2, GLOG_FATAL = 3;

// Logging macros with printf-like formatting capabilities.
#define YBC_LOG_INFO(...) \
    YBCLogImpl(/* severity */ 0, __FILE__, __LINE__, /* stack_trace */ false, __VA_ARGS__)
#define YBC_LOG_WARNING(...) \
    YBCLogImpl(/* severity */ 1, __FILE__, __LINE__, /* stack_trace */ false, __VA_ARGS__)
#define YBC_LOG_ERROR(...) \
    YBCLogImpl(/* severity */ 2, __FILE__, __LINE__, /* stack_trace */ false, __VA_ARGS__)
#define YBC_LOG_FATAL(...) \
    YBCLogImpl(/* severity */ 3, __FILE__, __LINE__, /* stack_trace */ false, __VA_ARGS__)

// The following functions log the given message formatted similarly to printf followed by a stack
// trace.

#define YBC_LOG_INFO_STACK_TRACE(...) \
    YBCLogImpl(/* severity */ 0, __FILE__, __LINE__, /* stack_trace */ true, __VA_ARGS__)
#define YBC_LOG_WARNING_STACK_TRACE(...) \
    YBCLogImpl(/* severity */ 1, __FILE__, __LINE__, /* stack_trace */ true, __VA_ARGS__)
#define YBC_LOG_ERROR_STACK_TRACE(...) \
    YBCLogImpl(/* severity */ 2, __FILE__, __LINE__, /* stack_trace */ true, __VA_ARGS__)

void YBCLogImpl(int severity,
                const char* file_name,
                int line_number,
                bool stack_trace,
                const char* format, ...);

#ifdef __cplusplus
} // extern "C"
#endif

#endif  // YB_UTIL_YBC_UTIL_H
