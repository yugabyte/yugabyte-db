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

#include "yb/util/stol_utils.h"

namespace yb {
namespace util {

namespace {

// Custom stold with additional dummy parameter (base) to make the function pointer in the
// template below work.
long double custom_stold(const char* str, char **str_end, int base = 10) {
  return std::strtold(str, str_end);
}

CHECKED_STATUS IsFirstCharDigit(Slice slice) {
  if (slice.empty() || isspace(*to_char_ptr(slice.data()))) {
    // disable skip of spaces.
    return STATUS_SUBSTITUTE(InvalidArgument, "$0 is not a valid number", slice.ToDebugString());
  }
  return Status::OK();
}

} // Anonymous namespace

template <typename T>
Status CheckedSton(Slice slice, std::function<T(const char*, char **str_end, int)> StrToNum,
                   T* val) {
  RETURN_NOT_OK(IsFirstCharDigit(slice));
  char* str_end;
  errno = 0;
  *val = StrToNum(to_char_ptr(slice.data()), &str_end, 10);
  // Check errno.
  if (errno != 0) {
    return STATUS_SUBSTITUTE(InvalidArgument, "$0 is not a valid number: $1",
                             slice.ToDebugString(), std::strerror(errno));
  }

  // Check that entire string was processed.
  if (str_end != to_char_ptr(slice.end())) {
    return STATUS_SUBSTITUTE(InvalidArgument, "$0 is not a valid number", slice.ToDebugString());
  }
  return Status::OK();
}

Status CheckedStoi(const string& str, int32_t* val) {
  if (sizeof(int) == 4) {
    // Need special handling for stoi since there is no equivalent strtoi.
    RETURN_NOT_OK(IsFirstCharDigit(str));
    try {
      size_t pos;
      *val = std::stoi(str, &pos);
      if (pos != str.size()) {
        return STATUS_SUBSTITUTE(InvalidArgument, "$0 is not a valid integer", str);
      }
    } catch(std::exception& e) {
      return STATUS_SUBSTITUTE(InvalidArgument, "$0 is not a valid integer", str);
    }
    return Status::OK();
  } else if (sizeof(long) == 4) {
    return CheckedSton(str,
                       std::function<long(const char*, char **str_end, int)>(std::strtol),
                       reinterpret_cast<long*>(val));
  } else {
    return STATUS(IllegalState, "Could not find appropriate int datatype with 4 bytes");
  }
}

Status CheckedStoll(const Slice& slice, int64_t* val) {
  return CheckedSton(slice, std::function<int64_t(const char*, char **str_end, int)>(std::strtoll),
                     val);
}

Status CheckedStold(const Slice& slice, long double* val) {
  return CheckedSton(slice,
                     std::function<long double(const char*, char **str_end, int)>(custom_stold),
                     val);
}

} // namespace util
} // namespace yb
