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

#include "yb/util/errno.h"
#include <errno.h>

#include <string.h>

#include "yb/util/flags.h"
#include "yb/util/status.h"

DEFINE_UNKNOWN_bool(suicide_on_eio, true,
            "Kill the process if an I/O operation results in EIO");
TAG_FLAG(suicide_on_eio, advanced);

namespace yb {

void ErrnoToCString(int err, char *buf, size_t buf_len) {
  CHECK_GT(buf_len, 0);
#if !defined(__GLIBC__) || \
  ((_POSIX_C_SOURCE >= 200112 || _XOPEN_SOURCE >= 600) && !defined(_GNU_SOURCE))
  // Using POSIX version 'int strerror_r(...)'.
  int ret = strerror_r(err, buf, buf_len);
  if (ret && ret != ERANGE && ret != EINVAL) {
    strncpy(buf, "unknown error", buf_len);
    buf[buf_len - 1] = '\0';
  }
#else
  // Using GLIBC version
  char* ret = strerror_r(err, buf, buf_len);
  if (ret != buf) {
    strncpy(buf, ret, buf_len);
    buf[buf_len - 1] = '\0';
  }
#endif
}

static const std::string kErrnoCategoryName = "system error";

static StatusCategoryRegisterer errno_category_registerer(
    StatusCategoryDescription::Make<ErrnoTag>(&kErrnoCategoryName));

namespace internal {

Status StatusFromErrno(const std::string& context, int err_number, const char* file, int line) {
  if (err_number == 0)
    return Status::OK();

  Errno err(err_number);
  switch (err_number) {
    case ENOENT:
      return Status(Status::kNotFound, file, line, context, err);
    case EEXIST:
      return Status(Status::kAlreadyPresent, file, line, context, err);
    case EOPNOTSUPP:
      return Status(Status::kNotSupported, file, line, context, err);
  }
  return Status(Status::kIOError, file, line, context, err);
}

// TODO: reconsider this approach to handling EIO.
Status StatusFromErrnoSpecialEioHandling(
    const std::string& context, int err_number, const char* file, int line) {
  if (err_number == EIO && FLAGS_suicide_on_eio) {
    // TODO: This is very, very coarse-grained. A more comprehensive
    // approach is described in KUDU-616.
    LOG(FATAL) << "Fatal I/O error, context: " << context;
  }
  return internal::StatusFromErrno(context, err_number, file, line);
}

// A lot of C library functions return zero on success and non-zero on failure, with the actual
// error code stored in errno. This helper constructs a Status based on errno but only if the return
// value (the rv parameter) is non-zero.
Status StatusFromErrnoIfNonZero(const std::string& context, int rv, const char* file, int line) {
  if (rv == 0)
    return Status::OK();
  decltype(errno) cached_errno = errno;
  if (cached_errno == 0) {
    return Status(
        Status::kIllegalState, file, line, Format("$0: return value is $1 but errno is zero",
        context, rv));
  }
  return StatusFromErrno(context, cached_errno, file, line);
}

}  // namespace internal
} // namespace yb
