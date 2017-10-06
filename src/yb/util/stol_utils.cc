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
long double custom_stold(const std::string& str, std::size_t* pos, int base = 10) {
  return std::stold(str, pos);
}

} // Anonymous namespace

template <typename T>
Status CheckedSton(const std::string& str, T (*F)(const std::string& , std::size_t*, int), T* val) {
  try {
    size_t pos;
    *val = F(str, &pos, 10);
    if (pos != str.size()) {
      return STATUS_SUBSTITUTE(InvalidArgument, "$0 is not a valid integer", str);
    }
  } catch(std::exception& e) {
    return STATUS_SUBSTITUTE(InvalidArgument, "$0 is not a valid integer", str);
  }
  return Status::OK();
}

Status CheckedStoi(const string& str, int32_t* val) {
  if (sizeof(int) == 4) {
    return CheckedSton(str, std::stoi, val);
  } else if (sizeof(long) == 4) {
    return CheckedSton(str, std::stol, reinterpret_cast<long*>(val));
  } else {
    return STATUS(IllegalState, "Could not find appropriate int datatype with 4 bytes");
  }
}

Status CheckedStoll(const string& str, int64_t* val) {
  return CheckedSton(str, std::stoll, reinterpret_cast<long long*>(val));
}

Status CheckedStold(const string& str, long double* val) {
  return CheckedSton(str, custom_stold, val);
}

} // namespace util
} // namespace yb
