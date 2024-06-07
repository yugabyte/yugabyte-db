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

#include "yb/tools/bulk_load_utils.h"

#include <boost/algorithm/string.hpp>

#include "yb/util/date_time.h"
#include "yb/util/flags.h"
#include "yb/util/stol_utils.h"

using std::string;

DEFINE_NON_RUNTIME_string(csv_delimiter, ",", "The character used to separate different columns.");
DEFINE_NON_RUNTIME_string(csv_quote_character, "\"",
    "The character used to quote a column that may have a delimiter.");
DEFINE_NON_RUNTIME_string(skipped_cols, "", "Comma separated list of 0-indexed columns to skip");

namespace {
static bool CSVSeparatorValidator(const char* flagname, const string& value) {
  if (value.size() != 1) {
    LOG(INFO) << "Expect " << flagname << " to be 1 character long";
    return false;
  }
  return true;
}
}

DEFINE_validator(csv_delimiter, &CSVSeparatorValidator);
DEFINE_validator(csv_quote_character, &CSVSeparatorValidator);

namespace yb {
namespace tools {

Result<Timestamp> TimestampFromString(const std::string& str) {
  auto val = CheckedStoll(str);
  if (val.ok()) {
    return DateTime::TimestampFromInt(*val);
  }

  return DateTime::TimestampFromString(str);
}

bool IsNull(std::string str) {
  boost::algorithm::to_lower(str);
  return str == kNullStringEscaped;
}

CsvTokenizer Tokenize(const std::string& line) {
  return Tokenize(line, FLAGS_csv_delimiter[0], FLAGS_csv_quote_character[0]);
}

CsvTokenizer Tokenize(const std::string& line, char delimiter = ',', char quote_char = '"') {
  boost::escaped_list_separator<char> seps('\\', delimiter, quote_char);
  CsvTokenizer tokenizer(line, seps);
  return tokenizer;
}

std::set<int> SkippedColumns() {
  return SkippedColumns(FLAGS_skipped_cols);
}

std::set<int> SkippedColumns(const string& columns_to_skip) {
  std::set<int> skipped_cols;
  CsvTokenizer tokenizer = Tokenize(columns_to_skip, ',');
  for (auto it = tokenizer.begin(); it != tokenizer.end(); it++) {
    auto col = CheckedStoi(*it);
    CHECK(col.ok());
    skipped_cols.insert(*col);
  }
  return skipped_cols;
}

}  // namespace tools
}  // namespace yb
