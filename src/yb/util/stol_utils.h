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

#ifndef YB_UTIL_STOL_UTILS_H
#define YB_UTIL_STOL_UTILS_H

#include "yb/util/status.h"

namespace yb {
namespace util {

template <typename T>
CHECKED_STATUS CheckedSton(const std::string& str, T* (*F)(const std::string& , std::size_t*, int),
                           T* val);

CHECKED_STATUS CheckedStoi(const std::string& str, int32_t* val);

CHECKED_STATUS CheckedStoll(const std::string& str, int64_t* val);

CHECKED_STATUS CheckedStold(const std::string& str, long double* val);

} // namespace util
} // namespace yb

#endif // YB_UTIL_STOL_UTILS_H
