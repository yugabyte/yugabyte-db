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

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_types.pb.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/server/server_base.pb.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/test_macros.h"

using namespace std::chrono_literals;

DECLARE_uint32(TEST_abort_create_table);

namespace yb {

class CreateAbortTest : public pgwrapper::PgMiniTestBase {

 public:
  virtual size_t NumTabletServers() override {
    return 1;
  }

  // Returns true iff the table/index appears in the client's ListTables() output.
  bool TableExistsInClientList(const std::string& name) {
    return GetTableIDFromTableName(name).ok();
  }

  // Returns true iff the table/index exists in the catalog manager.
  bool TableExistsInCatalog(master::CatalogManager& cm,
    const std::string& name, const std::string& namespace_name = "yugabyte") {
    auto table_info = cm.GetTableInfoFromNamespaceNameAndTableName(
        YQL_DATABASE_PGSQL, namespace_name, name);
    return table_info != nullptr;
  }

  // Returns true iff the table/index is not queryable via PG and
  // the error is because table/index "does not exist".
  bool TableNotQueryable(pgwrapper::PGConn& conn, const std::string& name) {
    auto s = conn.Execute("SELECT 1 FROM " + name + " LIMIT 1");
    return !s.ok() && s.ToString().find("does not exist") != std::string::npos;
  }

};

TEST_F(CreateAbortTest, TestAbortTableCreation) {
  // Create a client to access the cluster.
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  LOG(INFO) << "INFO_A: Initial cluster state:number of tablet servers: "
    << cluster_->num_tablet_servers()
    << ", test flag: " << FLAGS_TEST_abort_create_table;

  // test assumptions
  ASSERT_EQ(cluster_->num_tablet_servers(), 1);
  ASSERT_EQ(FLAGS_TEST_abort_create_table, 0);

  auto cconn = ASSERT_RESULT(Connect());
  auto qconn = ASSERT_RESULT(Connect());
  auto& cm = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager_impl();

  // create table should abort if the test flag is 1
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_abort_create_table) = 1;
  auto s = cconn.Execute("CREATE TABLE test_create_abort_t1 (k1 int primary key, k2 int)");
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.ToString(), "TEST: Aborting due to FLAGS_TEST_abort_create_table");
  ASSERT_FALSE(TableExistsInCatalog(cm, "test_create_abort_t1"));
  ASSERT_TRUE(TableNotQueryable(qconn, "test_create_abort_t1"));
  ASSERT_FALSE(TableExistsInClientList("test_create_abort_t1"));

  // the test flag only applies to table names that start with
  // 'test_create_abort_' prefix and should not impact any other table.
  ASSERT_OK(cconn.Execute("CREATE TABLE rand_table (k1 int primary key, k2 int)"));

  // create table should not abort if the test flag is 2
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_abort_create_table) = 2;
  ASSERT_OK(cconn.Execute("CREATE TABLE test_create_abort_t2 (k1 int primary key, k2 int)"));

  // create table should not abort if the test flag is 3
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_abort_create_table) = 3;
  ASSERT_OK(cconn.Execute("CREATE TABLE test_create_abort_t3 (k1 int primary key, k2 int)"));

  // restore things back
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_abort_create_table) = 0;
  ASSERT_OK(cconn.Execute("DROP TABLE rand_table"));
  ASSERT_OK(cconn.Execute("DROP TABLE test_create_abort_t2"));
  ASSERT_OK(cconn.Execute("DROP TABLE test_create_abort_t3"));
}

TEST_F(CreateAbortTest, TestAbortIndexCreation) {
  // Create a client to access the cluster.
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  LOG(INFO) << "INFO_A: Initial cluster state:number of tablet servers: "
    << cluster_->num_tablet_servers()
    << ", test flag: " << FLAGS_TEST_abort_create_table;

  // test assumptions
  ASSERT_EQ(cluster_->num_tablet_servers(), 1);
  ASSERT_EQ(FLAGS_TEST_abort_create_table, 0);

  auto cconn = ASSERT_RESULT(Connect());
  auto qconn = ASSERT_RESULT(Connect());
  auto& cm = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager_impl();

  // setup: create tables on which indexes will be created
  ASSERT_OK(cconn.Execute("CREATE TABLE test_create_abort_t1 (k1 int primary key, k2 int)"));
  ASSERT_OK(cconn.Execute("CREATE TABLE test_create_abort_t3 (k1 int primary key, k2 int)"));

    // create index should abort if the test flag is 1
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_abort_create_table) = 1;
  auto s = cconn.Execute("CREATE INDEX test_create_abort_t1_idx1 ON test_create_abort_t1 (k2)");
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.ToString(), "TEST: Aborting due to FLAGS_TEST_abort_create_table");
  ASSERT_FALSE(TableExistsInCatalog(cm, "test_create_abort_t1_idx1"));
  ASSERT_TRUE(TableNotQueryable(qconn, "test_create_abort_t1_idx1"));
  ASSERT_FALSE(TableExistsInClientList("test_create_abort_t1_idx1"));

  // create index with a different name should not abort
  ASSERT_OK(cconn.Execute("CREATE INDEX rand_table_idx ON test_create_abort_t1 (k2)"));

  // set the test flag to 2
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_abort_create_table) = 2;
  // create index should abort if the test flag is 2
  s = cconn.Execute("CREATE INDEX test_create_abort_t3_idx1 ON test_create_abort_t3 (k2)");
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.ToString(), "TEST: Aborting due to FLAGS_TEST_abort_create_table");
  ASSERT_FALSE(TableExistsInCatalog(cm, "test_create_abort_t3_idx1"));
  ASSERT_TRUE(TableNotQueryable(qconn, "test_create_abort_t3_idx1"));
  ASSERT_FALSE(TableExistsInClientList("test_create_abort_t3_idx1"));

  // create index should abort if the test flag is 3
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_abort_create_table) = 3;
  s = cconn.Execute("CREATE INDEX test_create_abort_t3_idx1 ON test_create_abort_t3 (k2)");
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.ToString(), "TEST: Aborting due to FLAGS_TEST_abort_create_table");
  ASSERT_FALSE(TableExistsInCatalog(cm, "test_create_abort_t3_idx1"));
  ASSERT_TRUE(TableNotQueryable(qconn, "test_create_abort_t3_idx1"));
  ASSERT_FALSE(TableExistsInClientList("test_create_abort_t3_idx1"));

  // restore things back
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_abort_create_table) = 0;
  ASSERT_OK(cconn.Execute("DROP INDEX rand_table_idx"));
  ASSERT_OK(cconn.Execute("DROP TABLE test_create_abort_t1"));
  ASSERT_OK(cconn.Execute("DROP TABLE test_create_abort_t3"));
}

}  // namespace yb
