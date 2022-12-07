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

// Utilities to be used in the main() function.

#pragma once

#include <fcntl.h>
#include <string.h>

#include <cstdarg>
#include <cstdlib>
#include <iostream>

#include "yb/gutil/strings/split.h"

#include "yb/util/status_fwd.h"
#include "yb/util/env.h"
#include "yb/util/faststring.h"
#include "yb/util/fault_injection.h"
#include "yb/util/logging.h"
#include "yb/util/path_util.h"

namespace yb {

// If the given status is not OK, log or print the status and return from the main() function.
#define LOG_AND_RETURN_FROM_MAIN_NOT_OK(status) do { \
    auto&& _status = (status); \
    if (!_status.ok()) { \
      if (IsLoggingInitialized()) { \
        LOG(FATAL) << ToStatus(_status); \
      } else { \
        std::cerr << ToStatus(_status) << std::endl; \
      } \
      return EXIT_FAILURE; \
    } \
  } while (false)


// Given a status, return a copy of it.
// For use in the above macro, so it works with both Status and Result.
inline Status ToStatus(const Status& status) {
  return status;
}

// Generic template to extract status from a result, to be used in the above macro.
template<class T>
Status ToStatus(const Result<T>& result) {
  return result.status();
}

} // namespace yb
