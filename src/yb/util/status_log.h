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

#pragma once

#include "yb/util/logging.h"

#include "yb/util/status.h"

#define YB_DFATAL_OR_RETURN_NOT_OK(s) do { \
    LOG_IF(DFATAL, !s.ok()) << s; \
    YB_RETURN_NOT_OK(s); \
  } while (0);

#define YB_DFATAL_OR_RETURN_ERROR_IF(condition, s) do { \
    if (PREDICT_FALSE(condition)) { \
      DCHECK(!s.ok()) << "Invalid OK status"; \
      LOG(DFATAL) << s; \
      return s; \
    } \
  } while (0);

// Emit a warning if 'to_call' returns a bad status.
#define YB_WARN_NOT_OK(to_call, warning_prefix) do { \
    auto&& _s = (to_call); \
    if (PREDICT_FALSE(!_s.ok())) { \
      YB_LOG(WARNING) << (warning_prefix) << ": " << StatusToString(_s);  \
    } \
  } while (0);

#define WARN_WITH_PREFIX_NOT_OK(to_call, warning_prefix) do { \
    auto&& _s = (to_call); \
    if (PREDICT_FALSE(!_s.ok())) { \
      YB_LOG(WARNING) << LogPrefix() << (warning_prefix) << ": " << StatusToString(_s); \
    } \
  } while (0);

// Emit a error if 'to_call' returns a bad status.
#define ERROR_NOT_OK(to_call, error_prefix) do { \
    auto&& _s = (to_call); \
    if (PREDICT_FALSE(!_s.ok())) { \
      YB_LOG(ERROR) << (error_prefix) << ": " << StatusToString(_s);  \
    } \
  } while (0);

// Log the given status and return immediately.
#define YB_LOG_AND_RETURN(level, status) do { \
    ::yb::Status _s = (status); \
    YB_LOG(level) << _s.ToString(); \
    return _s; \
  } while (0);

// If 'to_call' returns a bad status, CHECK immediately with a logged message of 'msg' followed by
// the status.
#define YB_CHECK_OK_PREPEND(to_call, msg) do { \
  auto&& _s = (to_call); \
  YB_CHECK(_s.ok()) << (msg) << ": " << StatusToString(_s); \
  } while (0);

// If the status is bad, CHECK immediately, appending the status to the logged message.
#define YB_CHECK_OK(s) YB_CHECK_OK_PREPEND(s, "Bad status")

// If status is not OK, this will FATAL in debug mode, or return the error otherwise.
#define DFATAL_OR_RETURN_NOT_OK YB_DFATAL_OR_RETURN_NOT_OK
#define DFATAL_OR_RETURN_ERROR_IF  YB_DFATAL_OR_RETURN_ERROR_IF
#define WARN_NOT_OK             YB_WARN_NOT_OK
#define LOG_AND_RETURN          YB_LOG_AND_RETURN
#define CHECK_OK_PREPEND        YB_CHECK_OK_PREPEND
#define CHECK_OK                YB_CHECK_OK

// These are standard glog macros.
#define YB_LOG              LOG
#define YB_CHECK            CHECK

// Checks that result is ok, extracts result value is case of success.
#define CHECK_RESULT(expr) \
  RESULT_CHECKER_HELPER(expr, CHECK_OK(__result))
