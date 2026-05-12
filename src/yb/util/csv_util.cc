// Copyright (c) YugabyteDB, Inc.
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

#include "yb/util/csv_util.h"

#include <regex>
#include <string>

#include <boost/algorithm/string/replace.hpp>

#include "yb/util/format.h"
#include "yb/util/pg_util.h"

namespace yb {

Status ReadCSVValues(const std::string& csv, std::vector<std::string>* lines) {
  // Function reads CSV string in the following format:
  // - fields are divided with comma (,)
  // - fields with comma (,) or double-quote (") are quoted with double-quote (")
  // - pair of double-quote ("") in quoted field represents single double-quote (")
  //
  // Examples:
  // 1,"two, 2","three ""3""", four , -> ['1', 'two, 2', 'three "3"', ' four ', '']
  // 1,"two                           -> Malformed CSV (quoted field 'two' is not closed)
  // 1, "two"                         -> Malformed CSV (quoted field 'two' has leading spaces)
  // 1,two "2"                        -> Malformed CSV (field with " must be quoted)
  // 1,"tw"o"                         -> Malformed CSV (no separator after quoted field 'tw')

  const std::regex exp(R"(^(?:([^,"]+)|(?:"((?:[^"]|(?:""))*)\"))(?:(?:,)|(?:$)))");
  auto i = csv.begin();
  const auto end = csv.end();
  std::smatch match;
  while (i != end && std::regex_search(i, end, match, exp)) {
    // Replace pair of double-quote ("") with single double-quote (") in quoted field.
    if (match[2].length() > 0) {
      lines->emplace_back(match[2].first, match[2].second);
      boost::algorithm::replace_all(lines->back(), "\"\"", "\"");
    } else {
      lines->emplace_back(match[1].first, match[1].second);
    }
    i += match.length();
  }
  SCHECK(i == end, InvalidArgument, Format("Malformed CSV '$0'", csv));
  if (!csv.empty() && csv.back() == ',') {
    lines->emplace_back();
  }
  return Status::OK();
}

} // namespace yb
