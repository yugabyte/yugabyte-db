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

#include "yb/client/transaction.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/transaction_pool.h"
#include "yb/client/yb_table_name.h"

#include "yb/integration-tests/cql_test_base.h"
#include "yb/integration-tests/mini_cluster_utils.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_manager.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"

DECLARE_bool(auto_promote_nonlocal_transactions_to_global);
DECLARE_bool(ycql_use_local_transaction_tables);
DECLARE_bool(TEST_track_last_transaction);
DECLARE_int32(load_balancer_max_concurrent_adds);
DECLARE_int32(load_balancer_max_concurrent_removals);
DECLARE_int32(load_balancer_max_concurrent_moves);
DECLARE_int32(load_balancer_max_concurrent_moves_per_table);
DECLARE_int32(TEST_nodes_per_cloud);
DECLARE_string(placement_cloud);
DECLARE_string(placement_region);
DECLARE_string(placement_zone);
DECLARE_string(TEST_transaction_manager_preferred_tablet);

namespace yb {

namespace client {

namespace {

YB_DEFINE_ENUM(ExpectedResult, (kLocal)(kGlobal)(kAbort));

constexpr auto kNamespace = "test";
constexpr auto kTablePrefix = "test";
constexpr auto kLocalRegion = 1;
constexpr auto kOtherRegion = 2;
const auto kStatusTabletCacheRefreshTimeout = MonoDelta::FromMilliseconds(20000);
const auto kWaitLoadBalancerTimeout = MonoDelta::FromMilliseconds(30000);


} // namespace

// Locality is currently being determined using the placement_cloud/region/zone gflags,
// which is shared for MiniCluster's tablet servers which run in the same process. This test
// gets around this problem by setting these flags to that of the tablet server the CQL driver
// connects to.
class CqlGeoTransactionsTest: public CqlTestBase<MiniCluster> {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ycql_use_local_transaction_tables) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_track_last_transaction) = true;
    // These don't get set in automatically in tests.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_placement_cloud) = "cloud0";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_placement_region) = "rack1";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_placement_zone) = "zone";
    // Put everything in the same cloud.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_nodes_per_cloud) = 5;
    // We wait for the load balancer whenever it gets triggered anyways, so there's
    // no concerns about the load balancer taking too many resources.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_adds) = 10;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_removals) = 10;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_moves) = 10;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_moves_per_table) = 10;

    CqlTestBase::SetUp();

    client_ = ASSERT_RESULT(cluster_->CreateClient());
    transaction_pool_ = nullptr;
    auto mini_ts = cluster_->mini_tablet_server(0);
    transaction_pool_ = &mini_ts->server()->TransactionPool();
    transaction_manager_ = &mini_ts->server()->TransactionManager();
    ASSERT_NE(transaction_pool_, nullptr);
  }

 protected:
  uint64_t GetCurrentVersion() {
    return transaction_manager_->GetLoadedStatusTabletsVersion();
  }

  void MakePlacementInfo(master::PlacementInfoPB* placement_info, int region) {
    placement_info->set_num_replicas(1);
    auto pb = placement_info->add_placement_blocks();
    pb->mutable_cloud_info()->set_placement_cloud("cloud0");
    pb->mutable_cloud_info()->set_placement_region(strings::Substitute("rack$0", region));
    pb->mutable_cloud_info()->set_placement_zone("zone");
    pb->set_min_num_replicas(1);
  }

  void CreateTransactionTable(int region) {
    auto current_version = GetCurrentVersion();

    std::string name = strings::Substitute("transactions_region$0", region);
    master::ReplicationInfoPB replication_info;
    MakePlacementInfo(replication_info.mutable_live_replicas(), region);
    ASSERT_OK(client_->CreateTransactionsStatusTable(name, &replication_info));

    WaitForStatusTabletsVersion(current_version + 1);
  }

  void SetupTables() {
    // Create keyspace and tables.
    auto session = ASSERT_RESULT(EstablishSession(driver_.get()));

    for (int i = 1; i <= num_tablet_servers(); ++i) {
      YBTableName table_name{YQL_DATABASE_CQL, kNamespace,
                             strings::Substitute("$0$1", kTablePrefix, i)};
      master::PlacementInfoPB placement_info;
      MakePlacementInfo(&placement_info, i);

      ASSERT_OK(session.ExecuteQuery(strings::Substitute(
          "CREATE TABLE $0(value int, PRIMARY KEY (value)) WITH transactions = { 'enabled': true }",
          table_name.table_name())));
      ASSERT_OK(client_->ModifyTablePlacementInfo(table_name, std::move(placement_info)));
      WaitForLoadBalanceCompletion();
    }

    // Wait for system.transactions to be created.
    WaitForStatusTabletsVersion(1);
  }

  void WaitForStatusTabletsVersion(uint64_t version) {
    constexpr auto error =
        "Timed out waiting for transaction manager to update status tablet cache version to $0";
    ASSERT_OK(WaitFor(
        [this, version] { return GetCurrentVersion() == version; },
        kStatusTabletCacheRefreshTimeout,
        strings::Substitute(error, version)));
  }

  void WaitForLoadBalanceCompletion() {
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      bool is_idle = VERIFY_RESULT(client_->IsLoadBalancerIdle());
      return !is_idle;
    }, kWaitLoadBalancerTimeout, "Timeout waiting for load balancer to start"));

    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      return client_->IsLoadBalancerIdle();
    }, kWaitLoadBalancerTimeout, "Timeout waiting for load balancer to go idle"));
  }

  Result<std::vector<TabletId>> GetStatusTablets(int region, bool global) {
    YBTableName table_name;
    if (global) {
      table_name = YBTableName(
          YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
    } else {
      table_name = YBTableName(
          YQL_DATABASE_CQL, master::kSystemNamespaceName,
          strings::Substitute("transactions_region$0", region));
    }
    std::vector<TabletId> tablet_uuids;
    RETURN_NOT_OK(client_->GetTablets(
        table_name, 1000 /* max_tablets */, &tablet_uuids, nullptr /* ranges */));
    return tablet_uuids;
  }

  int UnusedRowValue() {
    return next_row_value_++;
  }

  void CheckInsert(int region, ExpectedResult expected) {
    auto expected_status_tablets = ASSERT_RESULT(GetStatusTablets(
        region, expected == ExpectedResult::kGlobal));
    ASSERT_FALSE(expected_status_tablets.empty());

    auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
    auto status = session.ExecuteQuery(strings::Substitute(R"#(
       BEGIN TRANSACTION
       INSERT INTO $0$1 (value) VALUES ($2);
       END TRANSACTION;
    )#", kTablePrefix, region, UnusedRowValue()));
    if (expected == ExpectedResult::kAbort) {
      ASSERT_NOK(status);
    } else {
      ASSERT_OK(status);
      auto last_transaction = transaction_pool_->TEST_GetLastTransaction();
      auto metadata = last_transaction->GetMetadata(TransactionRpcDeadline()).get();
      ASSERT_OK(metadata);
      ASSERT_TRUE(std::find(expected_status_tablets.begin(),
                            expected_status_tablets.end(),
                            metadata->status_tablet) != expected_status_tablets.end());
    }
  }

  std::unique_ptr<YBClient> client_;
  TransactionManager* transaction_manager_;
  TransactionPool* transaction_pool_;
  int next_row_value_ = 0;
};

TEST_F(CqlGeoTransactionsTest, TestTransactionTabletSelection) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_promote_nonlocal_transactions_to_global) = false;
  SetupTables();

  // No local transaction tablets yet.
  CheckInsert(kLocalRegion, ExpectedResult::kGlobal);
  CheckInsert(kOtherRegion, ExpectedResult::kGlobal);

  CreateTransactionTable(kOtherRegion);

  // No local transaction tablets in region, but local transaction tablets exist in general.
  CheckInsert(kLocalRegion, ExpectedResult::kGlobal);
  CheckInsert(kOtherRegion, ExpectedResult::kGlobal);

  CreateTransactionTable(kLocalRegion);

  // Local transaction tablets exist in region.
  CheckInsert(kLocalRegion, ExpectedResult::kLocal);
  CheckInsert(kOtherRegion, ExpectedResult::kAbort);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_promote_nonlocal_transactions_to_global) = true;
  CheckInsert(kLocalRegion, ExpectedResult::kLocal);
  CheckInsert(kOtherRegion, ExpectedResult::kGlobal);
}

TEST_F(CqlGeoTransactionsTest, TestInitialGlobalOp) {
  SetupTables();
  CreateTransactionTable(kLocalRegion);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_promote_nonlocal_transactions_to_global) = true;
  // Not using the transaction pool.
  CheckInsert(kOtherRegion, ExpectedResult::kGlobal);
  // Using the transaction pool.
  CheckInsert(kOtherRegion, ExpectedResult::kGlobal);
}

TEST_F(CqlGeoTransactionsTest, AddTransactionTablet) {
  SetupTables();

  auto current_version = GetCurrentVersion();

  auto original_status_tablets = ASSERT_RESULT(GetStatusTablets(kOtherRegion, true /* global */));
  std::sort(original_status_tablets.begin(), original_status_tablets.end());
  ASSERT_FALSE(original_status_tablets.empty());

  auto global_txn_table = YBTableName(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
  auto global_txn_table_id = ASSERT_RESULT(client::GetTableId(client_.get(), global_txn_table));
  ASSERT_OK(client_->AddTransactionStatusTablet(global_txn_table_id));

  WaitForStatusTabletsVersion(current_version + 1);

  auto new_status_tablets = ASSERT_RESULT(GetStatusTablets(kOtherRegion, true /* global */));
  std::sort(new_status_tablets.begin(), new_status_tablets.end());
  std::vector<TabletId> new_tablets;
  std::set_difference(
      new_status_tablets.begin(), new_status_tablets.end(),
      original_status_tablets.begin(), original_status_tablets.end(),
      std::inserter(new_tablets, new_tablets.begin()));
  ASSERT_EQ(1, new_tablets.size());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_manager_preferred_tablet) = new_tablets[0];

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery(strings::Substitute(R"#(
     BEGIN TRANSACTION
     INSERT INTO $0$1 (value) VALUES ($2);
     END TRANSACTION;
  )#", kTablePrefix, kOtherRegion, UnusedRowValue())));

  auto last_transaction = transaction_pool_->TEST_GetLastTransaction();
  auto metadata = last_transaction->GetMetadata(TransactionRpcDeadline()).get();
  ASSERT_OK(metadata);
  ASSERT_EQ(new_tablets[0], metadata->status_tablet);
}

} // namespace client
} // namespace yb
