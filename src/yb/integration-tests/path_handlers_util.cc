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

#include <regex>
#include <string>
#include <vector>

#include "yb/integration-tests/path_handlers_util.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/curl_util.h"

namespace yb::integration_tests::path_handlers_util {

// Attempts to fetch url until a response with status OK, or until timeout.
// On mac the curl command fails with error "A libcurl function was given a bad argument", but
// succeeds on retries.
Status GetUrl(const std::string& url, faststring* result, MonoDelta timeout) {
  Status status;
  return WaitFor(
      [&]() -> bool {
        EasyCurl curl;
        status = curl.FetchURL(url, result);
        YB_LOG_IF_EVERY_N(WARNING, !status.ok(), 5) << status;

        return status.ok();
      },
      timeout, "Wait for curl response to return with status OK");
}

// Returns the rows in a table with a given id, excluding the header row.
Result<std::vector<std::vector<std::string>>> GetHtmlTableRows(
    const std::string& url, const std::string& html_table_tag_id, bool include_header) {
  faststring result;
  RETURN_NOT_OK(GetUrl(url, &result));
  const auto webpage = result.ToString();
  // Using [^]* to matches all characters instead of .* because . does not match newlines.
  const std::regex table_regex(
      Format("<table[^>]*id='$0'[^>]*>([^]*?)</table>", html_table_tag_id));
  const std::regex row_regex(Format("<tr>([^]*?)</tr>"));
  const std::regex col_regex(Format("<td[^>]*>([^]*?)</td>"));
  const std::regex header_regex("<th[^>]*>([^>]*?)</th>");

  std::smatch match;
  std::regex_search(webpage, match, table_regex);

  // [0] is the full match.
  if (match.size() < 1) {
    LOG(INFO) << "Full webpage: " << webpage;
    return STATUS_FORMAT(NotFound, "Table with id $0 not found", html_table_tag_id);
  }
  // Match[1] is the first capture group, and contains everything inside the <table> tags.
  std::string table = match[1];

  std::vector<std::vector<std::string>> rows;
  // Start at the second row to skip the header.
  auto table_begin = std::sregex_iterator(table.begin(), table.end(), row_regex);
  if (!include_header) {
    ++table_begin;
  }
  for (auto row_it = table_begin; row_it != std::sregex_iterator(); ++row_it) {
    auto row = row_it->str(1);
    std::vector<std::string> cols;
    std::regex regex;
    if (include_header && rows.empty()) {
      regex = header_regex;
    } else {
      regex = col_regex;
    }
    const auto row_begin = std::sregex_iterator(row.begin(), row.end(), regex);
    for (auto col_it = row_begin; col_it != std::sregex_iterator(); ++col_it) {
      cols.push_back(col_it->str(1));
    }
    rows.push_back(std::move(cols));
  }
  return rows;
}

Result<std::vector<std::string>> GetHtmlTableColumn(
    const std::string& url, const std::string& html_table_tag_id,
    const std::string& column_header) {
  auto rows = VERIFY_RESULT(GetHtmlTableRows(url, html_table_tag_id, /* include_header= */ true));
  if (rows.size() < 1) {
    return STATUS_FORMAT(
        NotFound, "Couldn't find table at url $0 with tag id $1", url, html_table_tag_id);
  }
  auto it = std::find(rows[0].begin(), rows[0].end(), column_header);
  if (it == rows[0].end()) {
    return STATUS_FORMAT(
        NotFound, "Couldn't find column with header $0 at url $1 with tag id $2", column_header,
        url, html_table_tag_id);
  }
  auto col_idx = it - rows[0].begin();
  auto rng = rows | std::views::drop(1) |
              std::views::transform([&col_idx](const auto& row) { return row[col_idx]; });
  return std::vector(std::ranges::begin(rng), std::ranges::end(rng));
}

}  // namespace yb::integration_tests::path_handlers_util
