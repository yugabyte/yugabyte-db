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

#include "yb/util/format.h"
#include "yb/util/tostring.h"

namespace yb {

std::string MillisecondsToString(int64_t milliseconds) {
  const char* sign = "";
  if (milliseconds < 0) {
    sign = "-";
    milliseconds = -milliseconds;
  }
  int64_t seconds = milliseconds / 1000;
  milliseconds -= seconds * 1000;
  return StringPrintf("%s%" PRId64 ".%03" PRId64 "s", sign, seconds, milliseconds);
}

std::string CStringArrayToString(char** elements, size_t length) {
  std::string result = "[";
  for (size_t i = 0; i < length; ++i) {
    result += Format("$0$1", (i) ? "," : "", elements[i]);
  }
  result += "]";
  return result;
}

} // namespace yb
