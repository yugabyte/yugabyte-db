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

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus.proxy.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/load_balancer_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"

#include "yb/yql/pgwrapper/pg_wrapper.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using std::string;

using namespace std::literals;

namespace yb {
namespace integration_tests {

const auto kDefaultTimeout = 30000ms;
constexpr int kNumTables = 3;
constexpr int kMovesPerTable = 1;
constexpr int kNumTxnTablets = 6;

// We need multiple tables in order to test load_balancer_max_concurrent_moves_per_table.
class LoadBalancerColocatedTablesTest : public YBTableTestBase {
 protected:
  bool use_yb_admin_client() override { return true; }

  bool use_external_mini_cluster() override { return true; }

  bool enable_ysql() override {
    // Create the transaction status table.
    return true;
  }

  // Only used for non-colocated tables, colocated tables should still share the parent tablet.
  int num_tablets() override {
    return 5;
  }

  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    opts->extra_tserver_flags.push_back("--placement_cloud=c");
    opts->extra_tserver_flags.push_back("--placement_region=r");
    opts->extra_tserver_flags.push_back("--placement_zone=z${index}");
    opts->extra_master_flags.push_back("--ysql_legacy_colocated_database_creation=false");
    opts->extra_master_flags.push_back("--load_balancer_max_concurrent_moves=10");
    opts->extra_master_flags.push_back("--load_balancer_max_concurrent_moves_per_table="
                                       + std::to_string(kMovesPerTable));
    opts->extra_master_flags.push_back("--enable_global_load_balancing=true");
    // This value needs to be divisible by three so that the transaction tablets are evenly divided
    // amongst the three tservers that we end up creating.
    opts->extra_master_flags.push_back("--transaction_table_num_tablets="
                                       + std::to_string(kNumTxnTablets));
  }

  virtual void CreateTables() {
    auto conn = ASSERT_RESULT(ConnectToDB());
    for (int i = 1; i <= kNumTables; ++i) {
      std::string dbname = "my_database-" + std::to_string(i);
      auto res = ASSERT_RESULT(conn.FetchFormat("SELECT * FROM pg_database WHERE datname = '$0'",
                                                dbname));
      // Create the database if not exists.
      if (PQntuples(res.get()) == 0) {
        ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE \"$0\" WITH COLOCATION = true", dbname));
      }

      conn = ASSERT_RESULT(ConnectToDB(dbname));
      std::string tbl_name = "kv-table-test-" + std::to_string(i);
      ASSERT_OK(conn.ExecuteFormat("CREATE TABLE \"$0\" (k BYTEA PRIMARY KEY, v BYTEA NOT NULL)",
                                   tbl_name));

      // Get the colocated database oid and the colocated table oid.
      const auto db_oid = ASSERT_RESULT(conn.FetchValue<pgwrapper::PGOid>(Format(
          "SELECT oid FROM pg_database WHERE datname = '$0'", dbname)));
      const auto table_oid = ASSERT_RESULT(conn.FetchValue<pgwrapper::PGOid>(Format(
          "SELECT oid FROM pg_class WHERE relname = '$0'", tbl_name)));
      table_names_.emplace_back(YQL_DATABASE_PGSQL,
                                GetPgsqlNamespaceId(db_oid),
                                dbname,
                                GetPgsqlTableId(db_oid, table_oid),
                                tbl_name);
    }
  }

  void DeleteTables() {
    for (const auto& tn : table_names_) {
      ASSERT_OK(client_->DeleteTable(tn));
    }
    table_names_.clear();
  }

  void CreateTable() override {
    if (!table_exists_) {
      CreateTables();
      table_exists_ = true;
    }
  }

  void DeleteTable() override {
    if (table_exists_) {
      DeleteTables();
      table_exists_ = false;
    }
  }

  Result<pgwrapper::PGConn> ConnectToDB(
      const std::string& dbname = "", bool simple_query_protocol = false) {
    return pgwrapper::PGConnBuilder({
      .host = external_mini_cluster_->pgsql_hostport(0).host(),
      .port = external_mini_cluster_->pgsql_hostport(0).port(),
      .dbname = dbname
    }).Connect(simple_query_protocol);
  }

  // Test helper function used for both colocated database and tablegroup global load balancing
  // test. CreateTables() establishes the different initial test state for colocated database and
  // tablegroup, respectively.
  void TestGlobalLoadBalancingWithColocatedTables() {
    const int rf = 3;
    ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", rf, ""));

    // Add two tservers to z0 and wait for everything to be balanced (globally and per table).
    std::vector<std::string> extra_opts;
    extra_opts.push_back("--placement_cloud=c");
    extra_opts.push_back("--placement_region=r");
    extra_opts.push_back("--placement_zone=z0");
    ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_opts));
    ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_opts));
    ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_tablet_servers() + 2,
        kDefaultTimeout));

    // Wait for load balancing to complete.
    WaitForLoadBalanceCompletion();

    // Assert that each table is balanced, and that we are globally balanced (Before global load
    // balancing, colocated parent tablets would not move) - Each colocated database or tablegroup
    // should have its tablet on a different TS in z0.
    const std::vector<uint32_t> z0_tserver_loads =
        ASSERT_RESULT(GetTserverLoads(/* ts_idxs */ {0, 3, 4}));
    ASSERT_TRUE(AreLoadsBalanced(z0_tserver_loads));
    ASSERT_EQ(z0_tserver_loads[0], 1);
    ASSERT_EQ(z0_tserver_loads[1], 1);
    ASSERT_EQ(z0_tserver_loads[2], 1);
  }
};

// See issue #4871 about the disable in TSAN.
TEST_F(LoadBalancerColocatedTablesTest,
       YB_DISABLE_TEST_IN_TSAN(GlobalLoadBalancingWithColocatedTables)) {
  // Start with 3 colocated databases with one table each, so 3 tablets in total.
  TestGlobalLoadBalancingWithColocatedTables();
}

class LoadBalancerTablegroupsTest : public LoadBalancerColocatedTablesTest {
  void CreateTables() override {
    std::unordered_map<string, string> ns_id_to_tg_id;
    for (int i = 1; i <= kNumTables; ++i) {
      // Autogenerated ids will fail the IsPgsqlId() CHECKs so we need to generate oids.
      // Currently just using 1111, 2222, 3333, etc.
      const uint32_t db_oid = i * 1000 + i * 100 + i * 10 + i;
      const uint32_t tablegroup_oid = db_oid * kNumTables + 1;
      const uint32_t table_oid = tablegroup_oid * kNumTxnTablets + 1;
      table_names_.emplace_back(
          YQL_DATABASE_PGSQL,
          GetPgsqlNamespaceId(db_oid),
          "my_database-" + std::to_string(i),
          GetPgsqlTableId(db_oid, table_oid),
          "kv-table-test-" + std::to_string(i));

      ns_id_to_tg_id.emplace(
          GetPgsqlNamespaceId(db_oid), GetPgsqlTablegroupId(db_oid, tablegroup_oid));
    }

    for (const auto& tn : table_names_) {
      ASSERT_OK(client_->CreateNamespaceIfNotExists(
          tn.namespace_name(),
          tn.namespace_type(),
          "",                /* creator_role_name */
          tn.namespace_id(), /* namespace_id */
          "",                /* source_namespace_id */
          boost::none,       /* next_pg_oid */
          false /* colocated */));

      ASSERT_OK(client_->CreateTablegroup(
          tn.namespace_name(),
          tn.namespace_id(),
          ns_id_to_tg_id[tn.namespace_id()],
          "" /* tablespace_id */,
          nullptr /* txn */));
      client::YBSchemaBuilder b;
      b.AddColumn("k")->Type(DataType::BINARY)->NotNull()->PrimaryKey();
      b.AddColumn("v")->Type(DataType::BINARY)->NotNull();
      ASSERT_OK(b.Build(&schema_));

      ASSERT_OK(NewTableCreator()
                    ->table_name(tn)
                    .table_id(tn.table_id())
                    .schema(&schema_)
                    .tablegroup_id(ns_id_to_tg_id[tn.namespace_id()])
                    .is_colocated_via_database(false)
                    .Create());
    }
  }

  std::unique_ptr<client::YBTableCreator> NewTableCreator() override {
    std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
    table_creator->table_type(client::YBTableType::PGSQL_TABLE_TYPE);
    return table_creator;
  }
};

TEST_F(LoadBalancerTablegroupsTest, GlobalLoadBalancingWithTablegroups) {
  // Start with 3 tables, 1 table per tablegroup.
  TestGlobalLoadBalancingWithColocatedTables();
}

class LoadBalancerLegacyColocatedDBColocatedTablesTest : public LoadBalancerColocatedTablesTest {
  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    LoadBalancerColocatedTablesTest::CustomizeExternalMiniCluster(opts);
    opts->extra_master_flags.push_back("--ysql_legacy_colocated_database_creation=true");
  }

  void CreateTables() override {
    for (int i = 1; i <= kNumTables; ++i) {
      // Autogenerated ids will fail the IsPgsqlId() CHECKs so we need to generate oids.
      // Currently just using 1111, 2222, 3333, etc.
      const uint32_t db_oid = i * 1000 + i * 100 + i * 10 + i;
      const uint32_t table_oid = db_oid * kNumTables + 1;
      table_names_.emplace_back(YQL_DATABASE_PGSQL,
                                GetPgsqlNamespaceId(db_oid),
                                "my_database-" + std::to_string(i),
                                GetPgsqlTableId(db_oid, table_oid),
                                "kv-table-test-" + std::to_string(i));
    }

    for (const auto& tn : table_names_) {
      ASSERT_OK(client_->CreateNamespaceIfNotExists(tn.namespace_name(),
                                                    tn.namespace_type(),
                                                    "",                 /* creator_role_name */
                                                    tn.namespace_id(),  /* namespace_id */
                                                    "",                 /* source_namespace_id */
                                                    boost::none,        /* next_pg_oid */
                                                    true                /* colocated */));

      client::YBSchemaBuilder b;
      b.AddColumn("k")->Type(DataType::BINARY)->NotNull()->PrimaryKey();
      b.AddColumn("v")->Type(DataType::BINARY)->NotNull();
      ASSERT_OK(b.Build(&schema_));

      ASSERT_OK(NewTableCreator()->table_name(tn)
                                  .table_id(tn.table_id())
                                  .schema(&schema_)
                                  .is_colocated_via_database(true)
                                  .Create());
    }
  }

  // Modified to create SQL tables.
  std::unique_ptr<client::YBTableCreator> NewTableCreator() override {
    std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
    if (num_tablets() > 0) {
      table_creator->num_tablets(num_tablets());
    }
    table_creator->table_type(client::YBTableType::PGSQL_TABLE_TYPE);
    return table_creator;
  }
};

TEST_F(LoadBalancerLegacyColocatedDBColocatedTablesTest,
       GlobalLoadBalancingWithLegacyColocatedDBColocatedTables) {
  // Start with 3 legacy colocated databases with one table each, so 3 tablets in total.
  TestGlobalLoadBalancingWithColocatedTables();
}

} // namespace integration_tests
} // namespace yb
