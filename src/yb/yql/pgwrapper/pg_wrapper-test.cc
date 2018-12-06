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

#include "yb/yql/pgwrapper/pg_wrapper.h"

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/result.h"
#include "yb/util/path_util.h"
#include "yb/util/net/net_util.h"
#include "yb/util/subprocess.h"
#include "yb/util/string_trim.h"

#include "yb/client/table_handle.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

using std::string;
using std::vector;
using std::unique_ptr;

using yb::util::TrimStr;
using yb::util::TrimTrailingWhitespaceFromEveryLine;

using namespace std::literals;

namespace yb {
namespace pgwrapper {

class PgWrapperTest : public YBMiniClusterTestBase<ExternalMiniCluster> {
 protected:
  virtual void SetUp() override {
    YBMiniClusterTestBase::SetUp();

    ExternalMiniClusterOptions opts;
    opts.start_pgsql_proxy = true;

    // Test that we can start PostgreSQL servers on non-colliding ports within each tablet server.
    opts.num_tablet_servers = 3;

    cluster_.reset(new ExternalMiniCluster(opts));
    ASSERT_OK(cluster_->Start());
  }
};

TEST_F(PgWrapperTest, TestStartStop) {
  ExternalTabletServer* ts0 = cluster_->tablet_server(0);

  const vector<std::pair<string, string>> kStatements {
    {"CREATE TABLE mytbl (k INT PRIMARY KEY, v TEXT)",
     "CREATE TABLE"},
    {"INSERT INTO mytbl (k, v) VALUES (100, 'foo')",
     "INSERT 0 1"},
    {"INSERT INTO mytbl (k, v) VALUES (200, 'bar')",
     "INSERT 0 1"},
    {
      "SELECT k, v FROM mytbl ORDER BY k",
      R"#(
  k  |  v
-----+-----
 100 | foo
 200 | bar
(2 rows)
)#"
    }
  };

  for (const auto& statement_and_expected : kStatements) {
    const auto& statement = statement_and_expected.first;
    const auto& expected = statement_and_expected.second;
    vector<string> argv {
      GetPostgresInstallRoot() + "/bin/psql",
      "-h", ts0->bind_host(),
      "-p", std::to_string(ts0->pgsql_rpc_port()),
      "-U", "postgres",
      "-c", statement
    };
    string psql_stdout;
    LOG(INFO) << "Executing statement: " << statement;
    ASSERT_OK(Subprocess::Call(argv, &psql_stdout));
    LOG(INFO) << "Output from statement {{ " << statement << " }}:\n"
              << psql_stdout;
    ASSERT_EQ(
        TrimStr(TrimTrailingWhitespaceFromEveryLine(expected)),
        TrimStr(TrimTrailingWhitespaceFromEveryLine(psql_stdout)));
  }
}

}  // namespace pgwrapper
}  // namespace yb
