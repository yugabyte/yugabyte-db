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

#include <gtest/gtest.h>

#include "yb/integration-tests/external_mini_cluster-itest-base.h"

#include "yb/util/debug.h"
#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/faststring.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"

using std::string;
using std::vector;

namespace yb {

// If the env variable CONFLICT_RESOLVE_KEYS_OUTPUT_PATH is set, the output files will be put to the
// specified directory without checking the output files. This can be used to generate the output
// files for new test:
// Step 1: Create a new sql file with the test scenario
//         in src/yb/integration-tests/conflict_resolve_keys_verification/sql/.
// Step 2: Add a new test case in the file.
// Step 3: Set the env variable CONFLICT_RESOLVE_KEYS_OUTPUT_PATH. For example, set it to your
//         code path: $BUILDROOT/src/yb/integration-tests/conflict_resolve_keys_verification/sql/
// Step 4: Run this test and generate the output file. verify that the output file is as expected.
// Step 5: Unset the env variable and runs the test again to make sure the test works.
std::string generate_output_path;
constexpr auto dump_to_file = "/tmp/conflict_resolve_dump_keys.out";
std::vector<std::pair<std::string, std::string>> dump_gflag_values = {
  { "TEST_file_to_dump_in_memory_keys", dump_to_file },
  { "TEST_file_to_dump_keys_checked_for_conflict_in_intents_db", dump_to_file },
  { "TEST_file_to_dump_keys_checked_for_conflict_in_regular_db", dump_to_file },
  { "TEST_file_to_dump_docdb_writes", dump_to_file }};

class ConflictResolveKeysVerificationITest : public ExternalMiniClusterITestBase {
 public:
  void TearDown() override {
    ExternalMiniClusterITestBase::TearDown();
  }

  void SetUp() override {
    // The output are different in sanitizer modes, mac and debug modes, so skip the tests in those
    // modes for now.
    // TODO (GH 28660): handle asan/tsan, debug, mac.
    if (IsSanitizer() || kIsDebug || kIsMac) {
      GTEST_SKIP() << "Skipping the tests in non-release builds or sanitizers or mac";
    }

    const char* val = std::getenv("CONFLICT_RESOLVE_KEYS_OUTPUT_PATH");
    generate_output_path = val ? std::string(val) : std::string();

    const auto sub_dir = "test_conflict_resolve_keys_verification_sql";
    test_sql_dir_ = JoinPathSegments(env_util::GetRootDir(sub_dir), sub_dir, "sql");
    std::vector<std::string> extra_tserver_flags = {
      "--ysql_enable_packed_row=true",
      "--TEST_docdb_log_write_batches=true",
      "--TEST_no_schedule_remove_intents=true",
      "--ysql_enable_auto_analyze=false",
      "--ysql_yb_ddl_transaction_block_enabled=true",
      "--enable_object_locking_for_table_locks=true",
      "--allowed_preview_flags_csv=skip_prefix_locks"
    };
    // Start cluster with the specified flags
    StartCluster(extra_tserver_flags);
  }

 protected:
  void StartCluster(const vector<string>& extra_tserver_flags) {
    ExternalMiniClusterOptions opts;
    opts.num_tablet_servers = 1;
    opts.extra_tserver_flags = extra_tserver_flags;
    opts.extra_master_flags = {"--replication_factor=1"};
    opts.enable_ysql = true;
    cluster_.reset(new ExternalMiniCluster(opts));
    ASSERT_OK(cluster_->Start());
    inspect_.reset(new itest::ExternalMiniClusterFsInspector(cluster_.get()));
    ts_map_ = ASSERT_RESULT(itest::CreateTabletServerMap(cluster_.get()));
    client_ = ASSERT_RESULT(cluster_->CreateClient());
  }

  void ExecutePgFile(const std::string& file_path,
                     const std::string& database_name = "yugabyte") {
    std::vector<std::string> args;
    args.push_back(GetPgToolPath("ysqlsh"));
    args.push_back("--host");
    args.push_back(cluster_->ysql_hostport(0).host());
    args.push_back("--port");
    args.push_back(AsString(cluster_->ysql_hostport(0).port()));
    // Fail the script on the first error.
    args.push_back("--variable=ON_ERROR_STOP=1");
    args.push_back("-f");
    args.push_back(file_path);
    args.push_back("-d");
    args.push_back(database_name);

    auto s = CallAdminVec(args);
    LOG(INFO) << "Command output: " << s;

    // Assert that the script executed without any errors.
    ASSERT_OK(s);
  }

  Result<std::string> CallAdminVec(const std::vector<std::string>& args) {
    std::string output, error;
    LOG(INFO) << "Execute: " << AsString(args);
    auto status = Subprocess::Call(args, &output, &error);
    if (!status.ok()) {
      return status.CloneAndAppend(error);
    }
    return output;
  }

  void RunTestsForFile(const string& sql_name) {
    auto sql_path = JoinPathSegments(test_sql_dir_, sql_name);
    std::vector<bool> skip_prefix_locks_config = {false, true};
    std::vector<std::string> isolations = {"serializable", "repeatable read"};
    for (auto skip_prefix_locks : skip_prefix_locks_config) {
      for (auto isolation : isolations) {
        if (prepare_table_row_) {
          PrepareTable();
        }
        // enable dump after prepare table to avoid noise on dumped files
        EnableDumping();
        RunTestForFile(sql_path, skip_prefix_locks, isolation);
        DisableDumping();

        // runs once only if not an explicit txn
        if (!use_explicit_txn_) {
          break;
        }
      }
    }
  }

  void RunTestForFile(const string& sql_path, bool skip_prefix_locks,
                      const std::string& isolation) {
    LOG(INFO) << "RunTestForFile. sql_path=" << sql_path
              << ", skip_prefix_locks=" << skip_prefix_locks << ", isolation=" << isolation;
    ASSERT_OK(cluster_->SetFlag(cluster_->tablet_server(0), "skip_prefix_locks",
                                skip_prefix_locks ? "true" : "false"));

    const string tmp_sql_path = "/tmp/conflict_resolve_keys_verification.tmp.sql";

    // Generate a tmp sql file based on 'sql_path' and isolation
    Env* env = Env::Default();
    faststring content;
    ASSERT_OK(ReadFileToString(env, sql_path, &content));
    std::string content_str = content.ToString();
    std::string isolation_folder = isolation == "serializable" ? "serializable" : "snapshot";
    const std::string isolation_pattern = "xxx";
    size_t pos = content_str.find(isolation_pattern);
    if (pos != std::string::npos) {
      LOG(INFO) << "Pattern " << isolation_pattern << "found in SQL file: " << sql_path;
      content_str.replace(pos, isolation_pattern.size(), isolation);
    } else {
      // includes: truncate colocate table, advisory lock, single row txn
      isolation_folder = "others";
    }

    // Make the output more consistent for the table with index
    content_str =
      "set ysql_max_in_flight_ops=1; set ysql_session_max_batch_size=1;\n" + content_str;
    ASSERT_OK(WriteStringToFile(env, content_str, tmp_sql_path));

    std::string target_output_file = Format(
       "$0/expected/$1/$2/target.out", test_sql_dir_,
       skip_prefix_locks ? "skip_prefix_locks_enabled" : "skip_prefix_locks_disabled",
       isolation_folder);
    std::vector<std::string> files;
    files.reserve(dump_gflag_values.size());
    for (const auto& pair : dump_gflag_values) {
        files.push_back(pair.second);
    }
    for (auto& file : files) {
      ASSERT_OK(DeleteIfExists(file, env));
    }

    ExecutePgFile(tmp_sql_path);
    // wait for a while so that the txn is applied
    SleepFor(3s);
    auto data = NormalizeFileContent(dump_to_file);
    ASSERT_OK(WriteStringToFile(env, data, target_output_file));
    ASSERT_TRUE(env->FileExists(target_output_file));

    const std::string expected_output_file = Format(
      "$0/expected/$1/$2/$3.out",
      generate_output_path.empty() ? test_sql_dir_ : generate_output_path,
      skip_prefix_locks ? "skip_prefix_locks_enabled" : "skip_prefix_locks_disabled",
      isolation_folder, BaseName(sql_path));
    if (!generate_output_path.empty()) {
      LOG(INFO) << "Writing to file: " << expected_output_file;
      ASSERT_OK(env->RenameFile(target_output_file, expected_output_file));
    } else {
      ASSERT_TRUE(CompareFiles(target_output_file, expected_output_file))
          << "two files are different: " << target_output_file << "," << expected_output_file;
    }
  }

  std::string NormalizeLogLine(const std::string& input) {
    LOG(INFO) << "before normalize: " << input;
    std::string result = input;

    if (input.find("TXN REV") != std::string::npos ||
        input.find("Frontiers") != std::string::npos) {
      result = "";
    }

    // Remove the leading string up to first ']:' (exclusive)
    size_t first_colon = result.find("]:");
    if (first_colon != std::string::npos) {
      result = result.substr(first_colon + 2);
    }

    // Replace all UUID-like patterns with zeros
    // Pattern: 8-4-4-4-12 hex digits separated by hyphens
    std::regex uuid_pattern(R"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})");
    result = std::regex_replace(result, uuid_pattern, "********-****-****-****-************");

    // Replace '{ physical: <number> }' with '{ physical: 0 }'
    std::regex physical_pattern(R"(\{\s*physical:\s*\d+(?:\s+logical:\s*\d+)?\s*\})");
    result = std::regex_replace(result, physical_pattern, "{ physical: 0 }");

    // replace [HT{ days: 20323 time: 00:36:36.771470 }] to [HT{ days: 0 time: 0 }]
    std::regex ht_pattern(R"(HT\{[^\}]*\})");
    result = std::regex_replace(result, ht_pattern, "HT{ days: 0 time: 0 }");

    // Replace 'status_tablet: <hex_string>' with 'status_tablet: 0'
    std::regex status_tablet_pattern(R"(status_tablet:\s*[0-9a-f]+)");
    result = std::regex_replace(result, status_tablet_pattern, "status_tablet: 0");

    // Replace 'priority: <number>' with 'priority: 0'
    std::regex priority_pattern(R"(priority:\s*\d+)");
    result = std::regex_replace(result, priority_pattern, "priority: 0");

    LOG(INFO) << "after normalize: " << result;
    return result;
  }

  bool ContainsAnyPattern(const std::string& input) {
    return std::regex_search(
        input, std::regex("(in_memory|intent_check|regular_check|intent_write|regular_write)"));
  }

  bool NeedSort(const std::string& input) {
    return std::regex_search(input, std::regex("(regular_write)"));
  }

  std::string SortFileByGroups(const std::string& content) {
    // Parse content into groups
    std::vector<std::string> groups;
    std::vector<std::string> groups_need_sort;
    std::istringstream stream(content);
    std::string line;
    std::string current_group;
    bool in_group = false;

    while (std::getline(stream, line)) {
      // Check if this line matches the group pattern
      if (ContainsAnyPattern(line)) {
        // Save previous group if exists
        if (in_group && !current_group.empty()) {
          auto& g = NeedSort(current_group) ? groups_need_sort : groups;
          g.push_back(current_group);
        }
        // Start new group
        current_group = line + "\n";
        in_group = true;
      } else if (in_group) {
        // Add line to current group
        current_group += line + "\n";
      }
    }

    // Don't forget the last group
    if (in_group && !current_group.empty()) {
      auto& g = NeedSort(current_group) ? groups_need_sort : groups;
      g.push_back(current_group);
    }

    // Sort groups
    std::sort(groups_need_sort.begin(), groups_need_sort.end());
    groups.insert(groups.end(), groups_need_sort.begin(), groups_need_sort.end());

    // Build sorted content
    std::string sorted_content;
    for (const auto& group : groups) {
      sorted_content += group;
    }

    LOG(INFO) << "Sorted " << groups.size() << " groups";
    return sorted_content;
  }

  std::string NormalizeFileContent(const std::string& file_path) {
    Env* env = Env::Default();
    if (!env->FileExists(file_path)) {
      LOG(INFO) << "The file doesn't exist: " << file_path;
      return "";
    }
    faststring content;
    CHECK_OK(ReadFileToString(env, file_path, &content));

    std::string content_str = content.ToString();
    std::string normalized_content;

    // Split content into lines and normalize each line
    std::istringstream stream(content_str);
    std::string line;
    while (std::getline(stream, line)) {
      std::string normalized_line = NormalizeLogLine(line);
      if (normalized_line.empty() && !line.empty()) {
        continue;
      }
      normalized_content += normalized_line + "\n";
    }

    return normalized_content;
  }

  bool CompareFiles(const std::string& actual_file, const std::string& expected_file) {
    Env* env = Env::Default();

    // Read both files
    faststring content_actual, content_expected;
    EXPECT_OK(ReadFileToString(env, actual_file, &content_actual));
    EXPECT_OK(ReadFileToString(env, expected_file, &content_expected));

    // Convert to strings
    std::string str_actual = SortFileByGroups(content_actual.ToString());
    std::string str_expected = SortFileByGroups(content_expected.ToString());

    if (str_actual != str_expected) {
      LOG(ERROR) << "actual: " << content_actual << ". expected: " << content_expected;
      LOG(ERROR) << "After sorted. actual: " << str_actual << ". expected: " << str_expected;
    }

    // Compare content
    return str_actual == str_expected;
  }

  void EnableDumping() {
    for (auto& pair : dump_gflag_values) {
      ASSERT_OK(cluster_->SetFlag(cluster_->tablet_server(0), pair.first, pair.second));
    }
  }

  void DisableDumping() {
    for (auto& pair : dump_gflag_values) {
      ASSERT_OK(cluster_->SetFlag(cluster_->tablet_server(0), pair.first, ""));
    }
  }

  // Create a table and insert a row without dumping. This is for the tests
  // which needs a row.
  void PrepareTable() {
    auto sql_path = JoinPathSegments(test_sql_dir_, "prepare_table.sql");
    ExecutePgFile(sql_path);
  }

  std::string test_sql_dir_;
  // Set to false if the test doesn't need to require creating a table and inserting a row
  // beforehand.
  bool prepare_table_row_ = true;
  bool use_explicit_txn_ = true;
};

TEST_F(ConflictResolveKeysVerificationITest, Insert) {
  const std::string sql_file = "insert.sql";
  prepare_table_row_ = false;
  RunTestsForFile(sql_file);
}

TEST_F(ConflictResolveKeysVerificationITest, InsertWithIndex) {
  const std::string sql_file = "insert_with_index.sql";
  prepare_table_row_ = false;
  RunTestsForFile(sql_file);
}

TEST_F(ConflictResolveKeysVerificationITest, InsertWithUniqueIndex) {
  const std::string sql_file = "insert_with_unique_index.sql";
  prepare_table_row_ = false;
  RunTestsForFile(sql_file);
}

TEST_F(ConflictResolveKeysVerificationITest, InsertWithUniqueIndexComposite) {
  const std::string sql_file = "insert_with_unique_index_composite.sql";
  prepare_table_row_ = false;
  RunTestsForFile(sql_file);
}

TEST_F(ConflictResolveKeysVerificationITest, InsertWithUniqueIndexDistinctNull) {
  const std::string sql_file = "insert_with_unique_index_distinct_null.sql";
  prepare_table_row_ = false;
  RunTestsForFile(sql_file);
}

TEST_F(ConflictResolveKeysVerificationITest, InsertWithUniqueIndexNotDistinctNull) {
  const std::string sql_file = "insert_with_unique_index_not_distinct_null.sql";
  prepare_table_row_ = false;
  RunTestsForFile(sql_file);
}

// Disable update test for tsan and asan tests because the column id are different.
TEST_F(ConflictResolveKeysVerificationITest, Update) {
  const std::string sql_file = "update.sql";
  RunTestsForFile(sql_file);
}

TEST_F(ConflictResolveKeysVerificationITest, Delete) {
  const std::string sql_file = "delete.sql";
  RunTestsForFile(sql_file);
}

TEST_F(ConflictResolveKeysVerificationITest, Select) {
  const std::string sql_file = "select.sql";
  RunTestsForFile(sql_file);
}

TEST_F(ConflictResolveKeysVerificationITest, SelectForUpdate) {
  const std::string sql_file = "select_for_update.sql";
  RunTestsForFile(sql_file);
}


TEST_F(ConflictResolveKeysVerificationITest, SelectForNoKeyUpdate) {
  const std::string sql_file = "select_for_no_key_update.sql";
  RunTestsForFile(sql_file);
}

TEST_F(ConflictResolveKeysVerificationITest, SelectForShare) {
  const std::string sql_file = "select_for_share.sql";
  RunTestsForFile(sql_file);
}

TEST_F(ConflictResolveKeysVerificationITest, SelectForKeyShare) {
  const std::string sql_file = "select_for_key_share.sql";
  RunTestsForFile(sql_file);
}

TEST_F(ConflictResolveKeysVerificationITest, TruncateColocatedTable) {
  const std::string sql_file = "truncate_colocated_table.sql";
  prepare_table_row_ = false;
  use_explicit_txn_ = false;
  RunTestsForFile(sql_file);
}

TEST_F(ConflictResolveKeysVerificationITest, InsertHashOnlyTable) {
  const std::string sql_file = "insert_hash_only_pk.sql";
  RunTestsForFile(sql_file);
}

TEST_F(ConflictResolveKeysVerificationITest, InsertRangeOnlyTable) {
  const std::string sql_file = "insert_range_only_pk.sql";
  RunTestsForFile(sql_file);
}

TEST_F(ConflictResolveKeysVerificationITest, FastPath) {
  const std::string sql_file = "fast_path.sql";
  prepare_table_row_ = false;
  use_explicit_txn_ = false;
  RunTestsForFile(sql_file);
}

} // namespace yb
