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

#ifndef YB_YQL_PGSQL_TEST_PG_PSQL_TEST_H_
#define YB_YQL_PGSQL_TEST_PG_PSQL_TEST_H_

#include <dirent.h>

#include "yb/client/client.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/master/mini_master.h"
#include "yb/util/test_util.h"

namespace yb {
namespace pgsql {

class PgPsqlTest : public YBTest {
 public:
  PgPsqlTest();
  virtual ~PgPsqlTest();

  //------------------------------------------------------------------------------------------------
  // Test start and cleanup functions.
  virtual void SetUp() override {
    YBTest::SetUp();
  }
  virtual void TearDown() override {
    YBTest::TearDown();
  }

  //------------------------------------------------------------------------------------------------
  // Create simulated cluster.
  CHECKED_STATUS CreateCluster();
  CHECKED_STATUS DestroyCluster();

  //------------------------------------------------------------------------------------------------
  // Run PostgreSQL test.
  CHECKED_STATUS RunTestSuite(const string& test_suite, bool stop_at_first_failure = false);

  // Run psql command.
  CHECKED_STATUS RunPsql(const string& infile, const string& outfile);

  // Run diff command.
  // Returns true if the given files have the same content. Otherwise, return false.
  bool FilesHaveSameContent(const string& file1, const string& file2);

  //------------------------------------------------------------------------------------------------
  // Run linux shell command.
  CHECKED_STATUS RunShellCmd(const string& cmd, string *output);

  void FindSqlFiles(const string& test_suite, string *suite_path, vector<string> *test_files);

 protected:
  //------------------------------------------------------------------------------------------------
  static const int kNumOfTablets = 3;
  static const std::string kDefaultDatabase;

  // Simulated cluster.
  std::shared_ptr<ExternalMiniCluster> cluster_;
  ExternalMiniClusterOptions cluster_opts_;

  // Hostport for psql to connect.
  HostPort pgsql_host_;

  // PostgreSql test directory.
  std::string yb_root_;

  // PostgreSql test directory.
  std::string test_dir_;

  // psql with full-path-name;
  std::string psql_filename_;

  // test output directory.
  std::string output_dir_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_TEST_PG_PSQL_TEST_H_
