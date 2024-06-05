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

#include <set>

#include <boost/tokenizer.hpp>

#include "yb/util/status_fwd.h"
#include "yb/util/timestamp.h"

namespace yb {
namespace tools {

constexpr const char* kNullStringEscaped = "\\n";
typedef boost::tokenizer< boost::escaped_list_separator<char> , std::string::const_iterator,
    std::string> CsvTokenizer;

Result<Timestamp> TimestampFromString(const std::string& str);

bool IsNull(std::string str);

std::set<int> SkippedColumns();
std::set<int> SkippedColumns(const std::string& cols_to_skip);

CsvTokenizer Tokenize(const std::string& line);
CsvTokenizer Tokenize(const std::string& line, char delimiter, char quote_char);

} // namespace tools
} // namespace yb
