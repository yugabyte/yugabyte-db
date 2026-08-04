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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <cerrno>
#include <string>
#include <string_view>

#include "yb/util/status_fwd.h"
#include "yb/util/status_ec.h"

using namespace std::literals;

DECLARE_bool(suicide_on_eio);

namespace yb {

void ErrnoToCString(int err, char *buf, size_t buf_len);

// Return a string representing an errno.
inline std::string ErrnoToString(int err) {
  char buf[512];
  ErrnoToCString(err, buf, sizeof(buf));
  return std::string(buf);
}

struct ErrnoTag : IntegralErrorTag<int32_t> {
  // This category id is part of the wire protocol and should not be changed once released.
  static constexpr CategoryDescriptor kCategory{1, "system error"sv};

  static std::string ToMessage(Value value) {
    return ErrnoToString(value);
  }
};

using Errno = StatusErrorCodeImpl<ErrnoTag>;

namespace internal {

Status StatusFromErrno(const std::string& context, int err_number, const char* file, int line);

Status StatusFromErrnoSpecialEioHandling(
    const std::string& context, int err_number, const char* file, int line);

// A lot of C library functions return zero on success and non-zero on failure, with the actual
// error code stored in errno. This helper constructs a Status based on errno but only if the return
// value (the rv parameter) is non-zero.
Status StatusFromErrnoIfNonZero(const std::string& context, int rv, const char* file, int line);

// For cases where we do actually care about the return value, but the call may return -1 and set
// errno, e.g. open().
template<typename T>
Result<T> ResultWithStatusFromErrno(const std::string& context, T rv, const char* file, int line) {
  if (rv == T(-1)) {
    return StatusFromErrno(context, errno, file, line);
  } else {
    return rv;
  }
}

}  // namespace internal

#define STATUS_FROM_ERRNO(context, err_number) \
    ::yb::internal::StatusFromErrno(context, err_number, __FILE__, __LINE__)

#define STATUS_FROM_ERRNO_SPECIAL_EIO_HANDLING(context, err_number) \
    ::yb::internal::StatusFromErrnoSpecialEioHandling(context, err_number, __FILE__, __LINE__)

// A convenient way to invoke a function that returns an errno-like value, and automatically create
// an error status that includes the function name in case it fails. Note that we are not looking at
// the errno variable here, but at the function's return value.
#define STATUS_FROM_ERRNO_RV_FN_CALL(fn_name, ...) \
    STATUS_FROM_ERRNO(BOOST_PP_STRINGIZE(fn_name), fn_name(__VA_ARGS__))

// Evaluates the given expression's value (typically a call to a C standard library function) and if
// its result is nonzero, returns a status based on errno.
//
// Important: the expression's value is not treated as an error code, it is only compared with zero.
#define STATUS_FROM_ERRNO_IF_NONZERO_RV(context, expr) \
    ::yb::internal::StatusFromErrnoIfNonZero(context, (expr), __FILE__, __LINE__)

#define RETURN_ON_ERRNO_RV_FN_CALL(...) \
    RETURN_NOT_OK(STATUS_FROM_ERRNO_RV_FN_CALL(__VA_ARGS__))

#define RESULT_FROM_ERRNO_FN_CALL(fn_name, ...) \
    ::yb::internal::ResultWithStatusFromErrno( \
        BOOST_PP_STRINGIZE(fn_name), fn_name(__VA_ARGS__), __FILE__, __LINE__)

#define VERIFY_ERRNO_FN_CALL(...) \
    VERIFY_RESULT(RESULT_FROM_ERRNO_FN_CALL(__VA_ARGS__))

#define RETURN_ON_ERRNO_FN_CALL(...) \
    RETURN_NOT_OK(RESULT_FROM_ERRNO_FN_CALL(__VA_ARGS__))

} // namespace yb
