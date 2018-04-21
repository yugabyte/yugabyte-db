//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/test/pg_psql_test.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>

#include "yb/util/env_util.h"
#include "yb/client/client.h"

namespace yb {
namespace pgsql {

using std::string;
using std::vector;
using std::shared_ptr;
using client::YBClient;
using client::YBSession;
using client::YBClientBuilder;

//--------------------------------------------------------------------------------------------------
// NOTE on "pgsql_testdb".
// - The database "pgsql_testdb" is the default database that must be created by the SQL script in
//   directory "src/yb/yql/pgsql/test/prepare".
// - Each of other SQL test file should create its own databases, use them, and drop them when the
//   its test cases finish.
const string PgPsqlTest::kDefaultDatabase("pgsql_testdb");

PgPsqlTest::PgPsqlTest() {
  yb_root_ = yb::env_util::GetRootDir("build");

  test_dir_ = yb_root_ + "/src/yb/yql/pgsql/test";
  LOG(INFO) << "Test directory is " << test_dir_;
  CHECK_NE(access(test_dir_.c_str(), R_OK), -1) << "Failed to access directory " << test_dir_;

  psql_filename_ = yb_root_ + "/bin/psql";
  LOG(INFO) << "Running tests using " << psql_filename_;
  CHECK_NE(access(psql_filename_.c_str(), F_OK), -1) << "Failed to access file " << psql_filename_;

  CHECK_NE(access("/tmp", F_OK ), -1) << "Directory /tmp is not available";
  char dir_template[] = "/tmp/yugabyte_pgtest_XXXXXX";
  output_dir_ = mkdtemp(dir_template);

  cluster_opts_.num_tablet_servers = kNumOfTablets;
  cluster_opts_.data_root_counter = 0;
  CHECK_OK(CreateCluster());
}

PgPsqlTest::~PgPsqlTest() {
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgPsqlTest::CreateCluster() {
  // Destroy existing cluster before creating new one.
  CHECK_OK(DestroyCluster());

  // Start mini-cluster with given number of tservers (default: 1), config client options
  cluster_ = std::make_shared<ExternalMiniCluster>(cluster_opts_);
  cluster_opts_.data_root_counter++;
  CHECK_OK(cluster_->Start());
  pgsql_host_ = cluster_->pgsql_hostport(0);
  LOG(INFO) << "Started proxy server at " << pgsql_host_;

  // Sleep to make sure the cluster is ready before accepting client messages.
  sleep(1);

  // Prepare test database.
  // Set 'stop_at_first_failure' to true so that we don't stuck in infinite loop of creating
  // cluster when running "prepare" suite fails to complete.
  LOG(INFO) << "Prepare database to run tests";
  CHECK_OK(RunTestSuite("prepare", true /* stop_at_first_failure */));

  return Status::OK();
}

CHECKED_STATUS PgPsqlTest::DestroyCluster() {
  if (cluster_ != nullptr) {
    cluster_->Shutdown();
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgPsqlTest::RunTestSuite(const string& test_suite, bool stop_at_first_failure) {
  LOG(INFO) << "Running tests in suite " << test_suite;

  string suite_dir;
  string suite_result;
  vector<string> filenames;
  FindSqlFiles(test_suite, &suite_dir, &filenames);

  bool has_failures = false;
  string result_dir = suite_dir + "/result";
  for (const string& filename : filenames) {
    // Prepare output file.
    string infile = strings::Substitute("$0/$1", suite_dir, filename);

    string test_name = filename.substr(0, filename.size() - 4);
    string outfile = strings::Substitute("$0/$1.$2", output_dir_, test_name, "txt");
    string resfile = strings::Substitute("$0/$1.$2", result_dir, test_name, "txt");

    // Run test.
    LOG(INFO) << "Running test " << infile;
    CHECK_OK(RunPsql(infile, outfile));

    // Check result.
    string result_msg;
    if (!FilesHaveSameContent(outfile, resfile)) {
      // Log failure.
      has_failures = true;
      result_msg = strings::Substitute("  Test $0 FAILED\n", test_name);
      LOG(INFO) << "Result:" << result_msg;

      // Record failure.
      suite_result += result_msg;
      if (stop_at_first_failure) {
        break;
      }

      // Recreate cluster before running the next SQL file.
      CHECK_OK(CreateCluster());
    } else {
      result_msg = strings::Substitute("  Test $0 PASSED\n", test_name);
      LOG(INFO) << "Result:" << result_msg;
    }
  }

  if (has_failures) {
    string errmsg = strings::Substitute("Test suite $0 FAILED\n", test_suite);
    LOG(INFO) << errmsg << suite_result;
    return STATUS(RuntimeError, errmsg);
  } else {
    LOG(INFO) << "Test suite " << test_suite << " PASSED";
  }

  return Status::OK();
}

CHECKED_STATUS PgPsqlTest::RunPsql(const string& infile, const string& outfile) {
  // Run psql pgsql_testdb -f infile -o outfile.
  string cmd;
  if (outfile.empty()) {
    cmd = strings::Substitute("$0 $1 -h=$2 -p=$3 -f $4",
                              psql_filename_, kDefaultDatabase,
                              pgsql_host_.host(), pgsql_host_.port(),
                              infile);
  } else {
    cmd = strings::Substitute("$0 $1 -h=$2 -p=$3 -f $4 -o $5",
                              psql_filename_, kDefaultDatabase,
                              pgsql_host_.host(), pgsql_host_.port(),
                              infile, outfile);
  }

  string output;
  CHECK_OK(RunShellCmd(cmd, &output));
  LOG(INFO) << output;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgPsqlTest::RunShellCmd(const string& cmd, string *output) {
  LOG(INFO) << "Run shell command: " << cmd;

  FILE *pipe = popen(cmd.c_str(), "r");
  if (pipe == nullptr) {
    return STATUS(RuntimeError, strings::Substitute("Failed to execute command <$0>", cmd));
  }

  string result;
  const int kBufSize = 4096;
  char buffer[kBufSize];
  int read_size = kBufSize;
  while (!feof(pipe) && read_size == kBufSize) {
    read_size = fread(buffer, 1, kBufSize, pipe);
    output->append(buffer, read_size);
  }
  pclose(pipe);

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

void PgPsqlTest::FindSqlFiles(const string& test_suite,
                              string *suite_path,
                              vector<string> *test_files) {
  if (test_suite.empty()) {
    *suite_path = test_dir_;
  } else {
    *suite_path = strings::Substitute("$0/$1", test_dir_, test_suite);
  }

  DIR *tdir = opendir(suite_path->c_str());
  CHECK(tdir) << strings::Substitute("Cannot open $0. Test suite doesn't exist", *suite_path);

  while (true) {
    struct dirent *ent = readdir(tdir);
    if (ent == NULL) {
      break;
    }

    if (ent->d_type & DT_DIR) {
      continue;
    }

    // Check if filename has 'sql' extension.
    string file_name = ent->d_name;
    const size_t ext_idx = file_name.find_last_of('.');
    if (ext_idx + 4 == file_name.size() && file_name.substr(ext_idx + 1, 3) == "sql") {
      test_files->push_back(file_name);
    }
  }

  closedir(tdir);
}

bool PgPsqlTest::FilesHaveSameContent(const string& file1, const string& file2) {
  const string cmd = strings::Substitute("diff $0 $1", file1, file2);
  string diff_result;
  CHECK_OK(RunShellCmd(cmd, &diff_result));

  if (diff_result.empty()) {
    return true;
  }

  LOG(INFO) << "Comparing log files(" << file1 << ", " << file2 << "):\n" << diff_result;
  return false;
}

}  // namespace pgsql
}  // namespace yb
