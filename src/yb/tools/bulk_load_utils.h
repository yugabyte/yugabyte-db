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

#ifndef YB_TOOLS_BULK_LOAD_UTILS_H
#define YB_TOOLS_BULK_LOAD_UTILS_H

#include "yb/util/date_time.h"
#include "yb/util/status.h"
#include "yb/util/stol_utils.h"
#include "yb/util/timestamp.h"

namespace yb {
namespace tools {

constexpr const char* kNullStringEscaped = "\\n";
typedef boost::tokenizer< boost::escaped_list_separator<char> , std::string::const_iterator,
    std::string> CsvTokenizer;

Result<Timestamp> TimestampFromString(const std::string& str) {
  auto val = util::CheckedStoll(str);
  if (val.ok()) {
    return DateTime::TimestampFromInt(*val);
  }

  return DateTime::TimestampFromString(str);
}

bool IsNull(std::string str) {
  boost::to_lower(str);
  return str == kNullStringEscaped;
}

CsvTokenizer Tokenize(const std::string& line) {
  boost::escaped_list_separator<char> seps('\\', ',', '\"');
  CsvTokenizer tokenizer(line, seps);
  return tokenizer;
}

} // namespace tools
} // namespace yb

#endif // YB_TOOLS_BULK_LOAD_UTILS_H
