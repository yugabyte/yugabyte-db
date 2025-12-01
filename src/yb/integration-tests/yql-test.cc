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

#include "yb/dockv/partition.h"

#include "yb/integration-tests/cql_test_base.h"
#include "yb/integration-tests/cql_test_util.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/cql/cqlserver/cql_server.h"

using std::string;
using namespace std::literals;

namespace yb::integration_tests {

class YqlTest : public pgwrapper::PgMiniTestBase {
 public:
  virtual ~YqlTest() = default;

 protected:
  void SetUp() override {
    pgwrapper::PgMiniTestBase::SetUp();

    // Start a YCQL server
    std::string cql_host;
    uint16_t cql_port = 0;
    ycql_server_ = yb::CqlTestBase<yb::MiniCluster>::MakeCQLServerForTServer(
        cluster_.get(), /* ts_idx */ 0, client_.get(), &cql_host, &cql_port);
    ASSERT_OK(ycql_server_->Start());

    // Create a YCQL session
    yb::CppCassandraDriver driver({cql_host}, cql_port, yb::UsePartitionAwareRouting::kTrue);
    ycql_session_ = ASSERT_RESULT(yb::EstablishSession(&driver));
  }

  // Clean up resources
  void DoTearDown() override {
    if (ycql_server_) {
      ycql_server_->Shutdown();
    }

    YBMiniClusterTestBase::DoTearDown();
  }

  std::unique_ptr<cqlserver::CQLServer> ycql_server_;
  CassandraSession ycql_session_;
};

TEST_F(YqlTest, TabletMetadataViewsWithYcqlAndYsql) {
  // Create YSQL table
  auto pg_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(pg_conn.Execute("CREATE TABLE ysql_test_table (id INT PRIMARY KEY, name TEXT)"));
  ASSERT_OK(pg_conn.Execute("INSERT INTO ysql_test_table VALUES (1, 'ysql_data')"));

  // Create YCQL table
  ASSERT_OK(ycql_session_.ExecuteQuery(
    "CREATE TABLE IF NOT EXISTS ycql_test_table (id INT PRIMARY KEY, name TEXT)"));
  ASSERT_OK(ycql_session_.ExecuteQuery(
    "INSERT INTO ycql_test_table (id, name) VALUES (1, 'ycql_data')"));

  // Query yb_get_tablet_metadata() - should show both YCQL and YSQL tables
  auto all_tablets_result = ASSERT_RESULT(pg_conn.FetchRows<std::string>(
      "SELECT object_name FROM yb_get_tablet_metadata() "
      "WHERE object_name IN ('ysql_test_table', 'ycql_test_table') "
      "ORDER BY object_name"));

  // Check that both table names are present in the results
  bool found_ysql = false, found_ycql = false;
  for (const auto& row : all_tablets_result) {
    if (row == "ysql_test_table") found_ysql = true;
    if (row == "ycql_test_table") found_ycql = true;
  }
  ASSERT_TRUE(found_ysql) << "ysql_test_table not found in yb_get_tablet_metadata()";
  ASSERT_TRUE(found_ycql) << "ycql_test_table not found in yb_get_tablet_metadata()";

  // Query yb_tablet_metadata view - should show only YSQL tables
  auto ysql_only_result = ASSERT_RESULT(pg_conn.FetchRows<std::string>(
      "SELECT relname FROM yb_tablet_metadata "
      "WHERE relname IN ('ysql_test_table', 'ycql_test_table') "
      "ORDER BY relname"));

  // Check that only YSQL table is present and YCQL table is not
  bool found_ysql_in_view = false, found_ycql_in_view = false;
  for (const auto& row : ysql_only_result) {
    if (row == "ysql_test_table") found_ysql_in_view = true;
    if (row == "ycql_test_table") found_ycql_in_view = true;
  }
  ASSERT_TRUE(found_ysql_in_view) << "ysql_test_table not found in yb_tablet_metadata view";
  ASSERT_FALSE(found_ycql_in_view) << "ycql_test_table should not be in yb_tablet_metadata view";
}

} // namespace yb::integration_tests
