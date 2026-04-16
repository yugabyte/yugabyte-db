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

#include <atomic>

#include <gtest/gtest.h>

#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/format.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/string_util.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_macros.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/split.h"

#include "yb/yql/pgwrapper/pg_wrapper_test_base.h"

namespace yb::pgwrapper {

namespace {

const std::string kCookie = "_HiStOrY_V2_";

std::string LibeditEncodeHistoryLine(const std::string& line) {
  std::string encoded;
  encoded.reserve(line.size() * 2);
  for (char ch : line) {
    switch (ch) {
      case ' ':
        encoded += "\\040";
        break;
      case '\\':
        encoded += "\\134";
        break;
      default:
        encoded.push_back(ch);
        break;
    }
  }
  return encoded;
}

}  // namespace

class YsqlshHistoryTest : public PgWrapperTestBase {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgWrapperTestBase::UpdateMiniClusterOptions(options);
    options->wait_for_tservers_to_accept_ysql_connections = true;
  }

  std::string WriteTempFile(const std::string& relative_path, const std::string& contents) {
    const auto absolute_path = GetTestPath(relative_path);
    CHECK_OK(WriteStringToFile(Env::Default(), contents, absolute_path));
    return absolute_path;
  }

  // Generates a history file with sequential SELECT statements (SELECT 1;, SELECT 2;, ...).
  // If with_cookie is true, prepends the _HiStOrY_V2_ header.
  std::string GenerateHistoryFile(
      const std::string& relative_path, size_t num_entries, bool with_cookie) {
    std::string contents;
    if (with_cookie) {
      contents = kCookie + "\n";
    }
    for (size_t i = 1; i <= num_entries; ++i) {
      contents +=
          LibeditEncodeHistoryLine(Format("SELECT 'line number ' || '$0' AS result;", i)) + "\n";
    }
    return WriteTempFile(relative_path, contents);
  }

  Result<std::vector<std::string>> ReadLines(const std::string& path) {
    faststring contents;
    RETURN_NOT_OK(ReadFileToString(Env::Default(), path, &contents));
    std::vector<std::string> lines = strings::Split(contents.ToString(), "\n");
    if (!lines.empty() && lines.back().empty()) {
      lines.pop_back();
    }
    return lines;
  }

  Result<std::string> RunYsqlshWithHistory(
      const std::string& history_path, const std::string& stdin_input,
      const std::vector<std::string>& extra_flags = {}) {
    const auto ysqlsh_path = VERIFY_RESULT(path_utils::GetPgToolPath("ysqlsh"));

    const auto input_path =
        WriteTempFile(Format("ysqlsh_input_$0.txt", next_input_id_++), stdin_input);

    const std::string extra = extra_flags.empty() ? "" : " " + JoinStrings(extra_flags, " ");

    // Use `script` so ysqlsh sees a pty and enables history.
    // Linux requires -c to specify the command; macOS doesn't support -c but accepts it as a
    // positional argument.
    // Note: paths are embedded directly into the shell command without escaping, so this breaks if
    // any path contains single quotes, double quotes, or other shell metacharacters.
    const auto command = Format(
        "set -euo pipefail\n"
        "export PSQL_HISTORY=\"$0\"\n"
        "export HISTFILE=\"$0\"\n"
        "cat \"$1\" | script -q -c '\"$2\" -h $3 -p $4 -U yugabyte -X$5'\n",
        history_path, input_path, ysqlsh_path, pg_ts->bind_host(), pg_ts->ysql_port(), extra);

    std::vector<std::string> argv = {"/bin/bash", "-lc", command};
    std::string output;
    std::string error;
    RETURN_NOT_OK(Subprocess::Call(argv, &output, &error));
    return output;
  }

  // Sends ESC [ A (up arrow) to the terminal n times.
  // This is used to access the history in ysqlsh.
  std::string UpArrowCommand(int n) {
    std::string result;
    for (int i = 0; i < n; ++i) {
      result += "\033[A";
    }
    return result;
  }

 private:
  std::atomic<int> next_input_id_{0};
};

// History is 5 lines long. This test presses the up arrow 5 times to execute the
// oldest command in the history.
TEST_F(YsqlshHistoryTest, YB_DISABLE_TEST_IN_SANITIZERS(SimpleHistoryNoCookie)) {
  const auto histfile = GenerateHistoryFile("no_cookie.hist", 5, false);

  const auto output = ASSERT_RESULT(RunYsqlshWithHistory(histfile, UpArrowCommand(5) + "\n\\q\n"));

  LOG(INFO) << "output: " << output;

  ASSERT_TRUE(output.find("line number 1") != std::string::npos);
  ASSERT_TRUE(output.find(kCookie) == std::string::npos);
}

// History file has cookie header + 5 entries. Press up 6 times to execute the oldest.
// Cookie should not appear in output.
TEST_F(YsqlshHistoryTest, YB_DISABLE_TEST_IN_SANITIZERS(CookieHandledOnlyAtHeader)) {
  const auto histfile = GenerateHistoryFile("with_cookie.hist", 5, true);

  const auto output = ASSERT_RESULT(RunYsqlshWithHistory(histfile, UpArrowCommand(6) + "\n\\q\n"));

  LOG(INFO) << "output: " << output;

  ASSERT_TRUE(output.find("line number 1") != std::string::npos);
  ASSERT_TRUE(output.find(kCookie) == std::string::npos);
}

// History file has cookie in the middle (not at header). Cookie should appear as a regular command.
TEST_F(YsqlshHistoryTest, YB_DISABLE_TEST_IN_SANITIZERS(CookieInMiddleAppearsAsCommand)) {
  // Create history: SELECT 1, cookie, SELECT 2
  const auto histfile = WriteTempFile(
      "middle_cookie.hist",
      Format(
          "$0\n"
          "$2\n"
          "$1\n",
          LibeditEncodeHistoryLine("SELECT 'line number ' || '1' AS result;"),
          LibeditEncodeHistoryLine("SELECT 'line number ' || '2' AS result;"),
          kCookie));

  // Press up 3 times to get to the oldest entry (SELECT 1), execute it
  const auto output = ASSERT_RESULT(RunYsqlshWithHistory(histfile, UpArrowCommand(3) + "\n\\q\n"));

  LOG(INFO) << "output: " << output;

  ASSERT_TRUE(output.find("line number 1") != std::string::npos);
}

// Start with 5 history entries (no cookie). Set HISTSIZE to 3, run a command.
// After truncation, file should keep the most recent entries without a cookie header.
TEST_F(YsqlshHistoryTest, YB_DISABLE_TEST_IN_SANITIZERS(TruncationKeepsTailWithoutCookie)) {
  const auto histfile = GenerateHistoryFile("truncate.hist", 5, false);

  // Press up to recall the most recent command (line 5), execute it, then quit.
  // This adds to history and triggers truncation when HISTSIZE is 3.
  const auto output = ASSERT_RESULT(RunYsqlshWithHistory(
      histfile, UpArrowCommand(1) + "\n\\q\n", {"--set=HISTSIZE=3"}));

  LOG(INFO) << "output: " << output;

  ASSERT_TRUE(output.find("line number 5") != std::string::npos);

  const auto lines = ASSERT_RESULT(ReadLines(histfile));
  LOG(INFO) << "history file lines: " << JoinStrings(lines, " | ");

  // We expect the history file to have 4 entries, even though we set HISTSIZE to 3.
  // This is existing libedit behavior, the history file ends up with HISTSIZE + 1 entries.
  ASSERT_EQ(lines.size(), 4);
  ASSERT_NE(lines.front(), kCookie);
}

}  // namespace yb::pgwrapper
