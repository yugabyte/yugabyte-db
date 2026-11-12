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

#include <algorithm>

#include "yb/common/entity_ids.h"
#include "yb/common/wire_protocol.h"

#include "yb/integration-tests/upgrade-tests/ysql_major_upgrade_test_base.h"

#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_ddl.proxy.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::literals;

namespace yb {

class YsqlMajorUpgradeInitdbSeedTest : public YsqlMajorUpgradeTestBase {
 protected:
  // Runs the catalog upgrade and moves one tserver to PG15, so the PG15 catalog can be read.
  Status UpgradeToMixedMode(bool use_seed) {
    RETURN_NOT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));
    RETURN_NOT_OK(cluster_->SetFlagOnMasters(
        "ysql_major_upgrade_use_initdb_seed", use_seed ? "true" : "false"));
    RETURN_NOT_OK(cluster_->SetFlagOnMasters(
        "TEST_ysql_major_upgrade_require_initdb_seed", use_seed ? "true" : "false"));
    RETURN_NOT_OK(PerformYsqlMajorCatalogUpgrade());
    return RestartTServerInCurrentVersion(*cluster_->tablet_server(kMixedModeTserverPg15));
  }

  // Every row of every pg_catalog table of db_name, as text.
  Result<std::vector<std::string>> DumpCatalogRows(const std::string& db_name) {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB(db_name, kMixedModeTserverPg15));
    // pg_upgrade does not migrate the catalog versions; the master copies them from PG11.
    const auto tables = VERIFY_RESULT(conn.FetchRows<std::string>(
        "SELECT relname::text FROM pg_class WHERE relnamespace = 'pg_catalog'::regnamespace "
        "AND relkind = 'r' AND relname <> 'pg_yb_catalog_version' ORDER BY 1"));
    // Correlation (stakind 3) follows ybrowid order, which is random per insert.
    std::string stats_columns =
        "starelid, staattnum, stainherit, stanullfrac, stawidth, stadistinct";
    for (int i = 1; i <= 5; ++i) {
      stats_columns += Format(
          ", stakind$0, staop$0, stacoll$0, "
          "CASE WHEN stakind$0 = 3 THEN NULL ELSE stanumbers$0 END, stavalues$0", i);
    }
    std::vector<std::string> rows;
    for (const auto& table : tables) {
      const auto query = table == "pg_statistic"
          ? Format("SELECT ROW($0)::text FROM pg_catalog.pg_statistic ORDER BY 1", stats_columns)
          : Format("SELECT t::text FROM pg_catalog.$0 t ORDER BY 1", table);
      const auto table_rows = VERIFY_RESULT(conn.FetchRows<std::string>(query));
      for (const auto& row : table_rows) {
        rows.push_back(Format("$0.$1: $2", db_name, table, row));
      }
    }
    return rows;
  }

  // The current-version catalog tables of template1 as the master lists them.
  Result<std::vector<std::string>> DumpTemplate1Tables() {
    master::ListTablesRequestPB req;
    master::ListTablesResponsePB resp;
    req.mutable_namespace_()->set_name("template1");
    req.mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
    rpc::RpcController rpc;
    rpc.set_timeout(60s);
    RETURN_NOT_OK(
        cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>().ListTables(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    std::vector<std::string> tables;
    for (const auto& table : resp.tables()) {
      if (IsCurrentVersionYsqlCatalogTable(table.id())) {
        tables.push_back(Format(
            "$0 $1 $2 $3", table.id(), table.name(), RelationType_Name(table.relation_type()),
            table.indexed_table_id()));
      }
    }
    return tables;
  }

  Result<std::vector<std::string>> DumpCatalog() {
    auto result = VERIFY_RESULT(DumpTemplate1Tables());
    for (const auto& db_name : {"template1", "yugabyte"}) {
      auto rows = VERIFY_RESULT(DumpCatalogRows(db_name));
      result.insert(result.end(), rows.begin(), rows.end());
    }
    std::sort(result.begin(), result.end());
    return result;
  }
};

// The catalog an upgrade builds from the initdb seed must equal the one it builds with initdb.
TEST_F(YsqlMajorUpgradeInitdbSeedTest, SeedMatchesInitdb) {
  ASSERT_OK(CreateSimpleTable());

  ASSERT_OK(UpgradeToMixedMode(/*use_seed=*/false));
  const auto from_initdb = ASSERT_RESULT(DumpCatalog());
  ASSERT_OK(RollbackUpgradeFromMixedMode());

  ASSERT_OK(UpgradeToMixedMode(/*use_seed=*/true));
  const auto from_seed = ASSERT_RESULT(DumpCatalog());

  std::vector<std::string> only_initdb, only_seed;
  std::set_difference(
      from_initdb.begin(), from_initdb.end(), from_seed.begin(), from_seed.end(),
      std::back_inserter(only_initdb));
  std::set_difference(
      from_seed.begin(), from_seed.end(), from_initdb.begin(), from_initdb.end(),
      std::back_inserter(only_seed));
  ASSERT_TRUE(only_initdb.empty() && only_seed.empty())
      << "Only with initdb: " << AsString(only_initdb) << "\nOnly with the seed: "
      << AsString(only_seed);

  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  ASSERT_OK(InsertRowInSimpleTableAndValidate());
}

// Without a usable seed the upgrade runs the full initdb.
TEST_F(YsqlMajorUpgradeInitdbSeedTest, FallbackToInitdbWithoutSeed) {
  for (auto* master : cluster_->master_daemons()) {
    master->AddExtraFlag("ysql_major_upgrade_initdb_seed_path", "/nonexistent");
  }
  ASSERT_OK(TestUpgradeWithSimpleTable());
}

}  // namespace yb
