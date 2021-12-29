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

#include <string>
#include <vector>

#include "yb/client/client_fwd.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/transaction_pool.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common.pb.h"

#include "yb/gutil/strings/join.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/master_defaults.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_int32(TEST_nodes_per_cloud);
DECLARE_bool(force_global_transactions);
DECLARE_bool(TEST_track_last_transaction);
DECLARE_string(placement_cloud);
DECLARE_string(placement_region);
DECLARE_string(placement_zone);

namespace yb {

namespace client {

namespace {

YB_DEFINE_ENUM(ExpectedLocality, (kLocal)(kGlobal));
YB_STRONGLY_TYPED_BOOL(SetGlobalTransactionsGFlag);
YB_STRONGLY_TYPED_BOOL(SetGlobalTransactionSessionVar);

constexpr auto kDatabaseName = "yugabyte";
constexpr auto kTablePrefix = "test";
const auto kStatusTabletCacheRefreshTimeout = MonoDelta::FromMilliseconds(10000);

} // namespace

// Tests transactions using local transaction tables.
// Locality is currently being determined using the placement_cloud/region/zone gflags,
// which is shared for MiniCluster's tablet servers which run in the same process. This test
// gets around this problem by setting these flags to that of the singular tablet server
// which runs the postgres instance.
class GeoTransactionsTest : public pgwrapper::PgMiniTestBase {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_track_last_transaction) = true;
    // These don't get set in automatically in tests.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_placement_cloud) = "cloud0";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_placement_region) = "rack1";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_placement_zone) = "zone";
    // Put everything in the same cloud.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_nodes_per_cloud) = 5;
    pgwrapper::PgMiniTestBase::SetUp();
    client_ = ASSERT_RESULT(cluster_->CreateClient());
    transaction_pool_ = cluster_->mini_tablet_server(0)->server()->TransactionPool();
    transaction_manager_ = cluster_->mini_tablet_server(0)->server()->TransactionManager();
  }

  virtual int NumTabletServers() override {
    return 3;
  }

  void DoTearDown() override {
    pgwrapper::PgMiniTestBase::DoTearDown();
  }

 protected:
  const std::shared_ptr<tserver::MiniTabletServer> PickPgTabletServer(
      const MiniCluster::MiniTabletServers& servers) override {
    // Force postgres to run on first TS.
    return servers[0];
  }

  YBTableName TableName(int region) {
    return YBTableName(
        YQLDatabase::YQL_DATABASE_PGSQL, kDatabaseName,
        strings::Substitute("$0$1", kTablePrefix, region));
  }

  void CreateTransactionTable(int region) {
    int current_version = transaction_manager_->GetLoadedStatusTabletsVersion();
    LOG(ERROR) << "TXN" << current_version;

    std::string name = strings::Substitute("transactions_$0", region);
    ASSERT_OK(client_->CreateTransactionsStatusTable(name));

    WaitForStatusTabletsVersion(current_version + 1);

    YBTableName table_name(YQL_DATABASE_CQL, yb::master::kSystemNamespaceName, name);
    auto replicas = new master::PlacementInfoPB;
    replicas->set_num_replicas(1);
    auto pb = replicas->add_placement_blocks();
    pb->mutable_cloud_info()->set_placement_cloud("cloud0");
    pb->mutable_cloud_info()->set_placement_region(strings::Substitute("rack$0", region));
    pb->mutable_cloud_info()->set_placement_zone("zone");
    pb->set_min_num_replicas(1);
    ASSERT_OK(client_->ModifyTablePlacementInfo(table_name, replicas));

    WaitForStatusTabletsVersion(current_version + 2);
  }

  void SetupTables() {
    // Create tablespaces and tables.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = true;
    auto conn = ASSERT_RESULT(Connect());
    for (int i = 1; i <= NumTabletServers(); ++i) {
        EXPECT_OK(conn.ExecuteFormat(R"#(
            CREATE TABLESPACE region$0 WITH (replica_placement='{
              "num_replicas": 1,
              "placement_blocks":[{
                "cloud": "cloud0",
                "region": "rack$0",
                "zone": "zone",
                "min_num_replicas": 1
              }]
            }')
        )#", i));
        EXPECT_OK(conn.ExecuteFormat(
            "CREATE TABLE $0$1(value int) TABLESPACE region$1", kTablePrefix, i));
    }
  }

  std::vector<TabletId> GetStatusTablets(int region, bool global) {
    YBTableName table_name;
    if (global) {
      table_name = YBTableName(
          YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
    } else {
      table_name = YBTableName(
          YQL_DATABASE_CQL, master::kSystemNamespaceName,
          yb::Format("transactions_$0", region));
    }
    std::vector<TabletId> tablet_uuids;
    EXPECT_OK(client_->GetTablets(
        table_name, 1000 /* max_tablets */, &tablet_uuids, nullptr /* ranges */));
    return tablet_uuids;
  }

  void CheckInsert(int to_region, SetGlobalTransactionsGFlag set_global_transactions_gflag,
                   SetGlobalTransactionSessionVar session_var, ExpectedLocality expected) {
    auto expected_status_tablets = GetStatusTablets(
        to_region, expected != ExpectedLocality::kLocal);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) =
        (set_global_transactions_gflag == SetGlobalTransactionsGFlag::kTrue);

    auto conn = ASSERT_RESULT(Connect());
    EXPECT_OK(conn.ExecuteFormat("SET force_global_transaction = $0", ToString(session_var)));
    EXPECT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    EXPECT_OK(conn.ExecuteFormat("INSERT INTO $0$1(value) VALUES (0)", kTablePrefix, to_region));
    EXPECT_OK(conn.CommitTransaction());

    auto last_transaction = transaction_pool_->GetLastTransaction();
    auto metadata = last_transaction->GetMetadata().get();
    EXPECT_OK(metadata);
    ASSERT_FALSE(expected_status_tablets.empty());
    ASSERT_TRUE(std::find(expected_status_tablets.begin(),
                          expected_status_tablets.end(),
                          metadata->status_tablet) != expected_status_tablets.end());
  }

  void WaitForStatusTabletsVersion(int version) {
    constexpr auto error =
        "Timed out waiting for transaction manager to update status tablet cache version to $0";
    EXPECT_OK(WaitFor(
        [this, version] {
            return transaction_manager_->GetLoadedStatusTabletsVersion() == version;
        },
        kStatusTabletCacheRefreshTimeout,
        strings::Substitute(error, version)));
  }

 private:
  std::unique_ptr<YBClient> client_;
  TransactionManager* transaction_manager_;
  TransactionPool* transaction_pool_;
};

TEST_F(GeoTransactionsTest, YB_DISABLE_TEST_IN_TSAN(TestTransactionTabletSelection)) {
  SetupTables();

  // No local transaction tablets yet.
  CheckInsert(
      1, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      ExpectedLocality::kGlobal);
  CheckInsert(
      2, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      ExpectedLocality::kGlobal);
  CheckInsert(
      1, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      ExpectedLocality::kGlobal);
  CheckInsert(
      2, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      ExpectedLocality::kGlobal);
  CheckInsert(
      1, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      ExpectedLocality::kGlobal);
  CheckInsert(
      2, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      ExpectedLocality::kGlobal);
  CheckInsert(
      1, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      ExpectedLocality::kGlobal);
  CheckInsert(
      2, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      ExpectedLocality::kGlobal);

  // Create region 2 local transaction table.
  CreateTransactionTable(2);

  // No local transaction tablets in region, but local transaction tablets exist in general.
  CheckInsert(
      1, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      ExpectedLocality::kGlobal);
  CheckInsert(
      2, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      ExpectedLocality::kGlobal);
  CheckInsert(
      1, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      ExpectedLocality::kGlobal);
  CheckInsert(
      2, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      ExpectedLocality::kGlobal);
  CheckInsert(
      1, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      ExpectedLocality::kGlobal);
  CheckInsert(
      2, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      ExpectedLocality::kGlobal);
  CheckInsert(
      1, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      ExpectedLocality::kGlobal);
  CheckInsert(
      2, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      ExpectedLocality::kGlobal);

  // Create region 1 local transaction table.
  CreateTransactionTable(1);

  // Local transaction tablets exist in region.
  // The case of connecting to TS2 with force_global_transactions = false will error out
  // because it is a global transaction, see #10537.
  CheckInsert(
      1, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      ExpectedLocality::kLocal);
  CheckInsert(
      1, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      ExpectedLocality::kGlobal);
  CheckInsert(
      2, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      ExpectedLocality::kGlobal);
  CheckInsert(
      1, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      ExpectedLocality::kGlobal);
  CheckInsert(
      2, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      ExpectedLocality::kGlobal);
  CheckInsert(
      1, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      ExpectedLocality::kGlobal);
  CheckInsert(
      2, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      ExpectedLocality::kGlobal);
}

} // namespace client
} // namespace yb
