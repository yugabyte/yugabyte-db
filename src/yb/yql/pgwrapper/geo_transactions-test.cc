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
#include "yb/tserver/tablet_server.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/mini_master.h"
#include "yb/master/master_client.pb.h"
#include "yb/yql/pgwrapper/geo_transactions_test_base.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/tsan_util.h"

using std::string;

DECLARE_int32(master_ts_rpc_timeout_ms);
DECLARE_bool(auto_create_local_transaction_tables);
DECLARE_bool(auto_promote_nonlocal_transactions_to_global);
DECLARE_bool(force_global_transactions);
DECLARE_bool(transaction_tables_use_preferred_zones);

using namespace std::literals;

namespace yb {

namespace client {

namespace {

YB_DEFINE_ENUM(ExpectedLocality, (kLocal)(kGlobal)(kNoCheck));
YB_STRONGLY_TYPED_BOOL(SetGlobalTransactionsGFlag);
YB_STRONGLY_TYPED_BOOL(SetGlobalTransactionSessionVar);
YB_STRONGLY_TYPED_BOOL(WaitForHashChange);
YB_STRONGLY_TYPED_BOOL(InsertToLocalFirst);
// 90 leaders per zone and a total of 3 zones so 270 leader distributions. Worst-case even if the LB
// is doing 1 leader move per run (it does more than that in practice) then at max it will take 270
// runs i.e. 270 secs (1 run = 1 sec)
const auto kWaitLeaderDistributionTimeout = MonoDelta::FromMilliseconds(270000);

} // namespace

class GeoTransactionsTest : public GeoTransactionsTestBase {
 protected:
  void SetupTablesWithAlter(size_t tables_per_region) {
    // Create tablespaces and tables.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = true;
    tables_per_region_ = tables_per_region;

    auto conn = ASSERT_RESULT(Connect());
    bool wait_for_version = ANNOTATE_UNPROTECTED_READ(FLAGS_auto_create_local_transaction_tables);
    auto current_version = GetCurrentVersion();
    for (size_t i = 1; i <= NumRegions(); ++i) {
      ASSERT_OK(conn.ExecuteFormat(R"#(
          CREATE TABLESPACE tablespace$0 WITH (replica_placement='{
            "num_replicas": 1,
            "placement_blocks":[{
              "cloud": "cloud0",
              "region": "rack$0",
              "zone": "zone",
              "min_num_replicas": 1
            }]
          }')
      )#", i));

      for (size_t j = 1; j <= tables_per_region; ++j) {
        ASSERT_OK(conn.ExecuteFormat(
            "CREATE TABLE $0$1_$2(value int)", kTablePrefix, i, j));
        ASSERT_OK(conn.ExecuteFormat(
            "ALTER TABLE $0$1_$2 SET TABLESPACE tablespace$1", kTablePrefix, i, j));
      }

      WaitForLoadBalanceCompletion();
      if (wait_for_version) {
        WaitForStatusTabletsVersion(current_version + 1);
        ++current_version;
      }
    }
  }

  void ValidateAllTabletLeaderinZone(std::vector<TabletId> tablet_uuids, int region) {
    std::string region_str = yb::Format("rack$0", region);
    auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
    for (const auto& tablet_id : tablet_uuids) {
      auto table_info = ASSERT_RESULT(catalog_manager.GetTabletInfo(tablet_id));
      auto leader = ASSERT_RESULT(table_info->GetLeader());
      auto server_reg_pb = leader->GetRegistration();
      ASSERT_EQ(server_reg_pb.common().cloud_info().placement_region(), region_str);
    }
  }

  Result<uint32_t> GetTablespaceOidForRegion(int region) {
    auto conn = EXPECT_RESULT(Connect());
    uint32_t tablespace_oid = EXPECT_RESULT(conn.FetchValue<int32_t>(strings::Substitute(
        "SELECT oid FROM pg_catalog.pg_tablespace WHERE spcname = 'tablespace$0'", region)));
    return tablespace_oid;
  }

  Result<std::vector<TabletId>> GetStatusTablets(int region, ExpectedLocality locality) {
    YBTableName table_name;
    if (locality == ExpectedLocality::kNoCheck) {
      return std::vector<TabletId>();
    } else if (locality == ExpectedLocality::kGlobal) {
      table_name = YBTableName(
          YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
    } else if (ANNOTATE_UNPROTECTED_READ(FLAGS_auto_create_local_transaction_tables)) {
      auto tablespace_oid = EXPECT_RESULT(GetTablespaceOidForRegion(region));
      table_name = YBTableName(
          YQL_DATABASE_CQL, master::kSystemNamespaceName,
          yb::Format("transactions_$0", tablespace_oid));
    } else {
      table_name = YBTableName(
          YQL_DATABASE_CQL, master::kSystemNamespaceName,
          yb::Format("transactions_region$0", region));
    }
    std::vector<TabletId> tablet_uuids;
    RETURN_NOT_OK(client_->GetTablets(
        table_name, 1000 /* max_tablets */, &tablet_uuids, nullptr /* ranges */));
    return tablet_uuids;
  }

  void CheckSuccess(int to_region, SetGlobalTransactionsGFlag set_global_transactions_gflag,
                   SetGlobalTransactionSessionVar session_var, InsertToLocalFirst local_first,
                   ExpectedLocality expected) {
    auto expected_status_tablets = ASSERT_RESULT(GetStatusTablets(to_region, expected));
    if (expected != ExpectedLocality::kNoCheck) {
      ASSERT_FALSE(expected_status_tablets.empty());
    }
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) =
        (set_global_transactions_gflag == SetGlobalTransactionsGFlag::kTrue);

    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat("SET force_global_transaction = $0", ToString(session_var)));
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    if (local_first) {
      ASSERT_OK(conn.ExecuteFormat(
          "INSERT INTO $0$1_1(value) VALUES (0)", kTablePrefix, kLocalRegion));
    }
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0$1_1(value) VALUES (0)", kTablePrefix, to_region));
    ASSERT_OK(conn.CommitTransaction());

    if (expected != ExpectedLocality::kNoCheck) {
      auto last_transaction = transaction_pool_->TEST_GetLastTransaction();
      auto metadata = last_transaction->GetMetadata(TransactionRpcDeadline()).get();
      ASSERT_OK(metadata);
      ASSERT_TRUE(std::find(expected_status_tablets.begin(),
                            expected_status_tablets.end(),
                            metadata->status_tablet) != expected_status_tablets.end());
    }

    ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    if (local_first) {
      ASSERT_OK(conn.FetchFormat("SELECT * FROM $0$1_1", kTablePrefix, kLocalRegion));
    }
    ASSERT_OK(conn.FetchFormat("SELECT * FROM $0$1_1", kTablePrefix, to_region));
    ASSERT_OK(conn.CommitTransaction());

    if (expected != ExpectedLocality::kNoCheck) {
      auto last_transaction = transaction_pool_->TEST_GetLastTransaction();
      auto metadata = last_transaction->GetMetadata(TransactionRpcDeadline()).get();
      ASSERT_OK(metadata);
      ASSERT_TRUE(std::find(expected_status_tablets.begin(),
                            expected_status_tablets.end(),
                            metadata->status_tablet) != expected_status_tablets.end());
    }

    ASSERT_OK(conn.StartTransaction(IsolationLevel::READ_COMMITTED));
    if (local_first) {
      ASSERT_OK(conn.FetchFormat("SELECT * FROM $0$1_1", kTablePrefix, kLocalRegion));
    }
    ASSERT_OK(conn.FetchFormat("SELECT * FROM $0$1_1", kTablePrefix, to_region));
    ASSERT_OK(conn.CommitTransaction());
  }

  void CheckAbort(int to_region, SetGlobalTransactionsGFlag set_global_transactions_gflag,
                  SetGlobalTransactionSessionVar session_var, InsertToLocalFirst local_first,
                  size_t num_aborts) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = set_global_transactions_gflag;

    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat("SET force_global_transaction = $0", ToString(session_var)));
    for (size_t i = 0; i < num_aborts; ++i) {
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
      if (local_first) {
        ASSERT_OK(conn.ExecuteFormat(
            "INSERT INTO $0$1_1(value) VALUES (0)", kTablePrefix, kLocalRegion));
      }
      ASSERT_NOK(conn.ExecuteFormat(
          "INSERT INTO $0$1_1(value) VALUES (0)", kTablePrefix, to_region));
      ASSERT_OK(conn.RollbackTransaction());
    }

    for (size_t i = 0; i < num_aborts; ++i) {
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
      if (local_first) {
        ASSERT_OK(conn.FetchFormat("SELECT * FROM $0$1_1", kTablePrefix, kLocalRegion));
      }
      ASSERT_NOK(conn.FetchFormat("SELECT * FROM $0$1_1", kTablePrefix, to_region));
      ASSERT_OK(conn.RollbackTransaction());
    }

    for (size_t i = 0; i < num_aborts; ++i) {
      ASSERT_OK(conn.StartTransaction(IsolationLevel::READ_COMMITTED));
      if (local_first) {
        ASSERT_OK(conn.FetchFormat("SELECT * FROM $0$1_1", kTablePrefix, kLocalRegion));
      }
      ASSERT_OK(conn.FetchFormat("SELECT * FROM $0$1_1", kTablePrefix, to_region));
      ASSERT_OK(conn.CommitTransaction());
    }
  }

  // Get the leader replica count and total replica count of a group of tablets belongs to a table
  // on a tserver.
  Result<std::pair<size_t, size_t>> GetTServerReplicaCount(
      tserver::MiniTabletServer* tserver,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets) {
    size_t leader_count = 0;
    size_t total_count = 0;

    for (const auto& tablet : tablets) {
      for (const auto& replica : tablet.replicas()) {
        if (replica.ts_info().permanent_uuid() == tserver->server()->permanent_uuid()) {
          if (replica.role() == PeerRole::LEADER) {
            leader_count++;
          }
          total_count++;
        }
      }
    }
    return std::make_pair(leader_count, total_count);
  }

  Result<bool> VerifyTableReplicaDistributionInZone(
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const std::vector<size_t>& current_zone_tserver_indexes,
      bool is_table_in_current_zone) {
    const auto expected_leaders_per_server = tablets.size() / current_zone_tserver_indexes.size();
    for (auto tserver_idx : current_zone_tserver_indexes) {
      const auto& [leader_count, total_count] =
          VERIFY_RESULT(GetTServerReplicaCount(cluster_->mini_tablet_server(tserver_idx), tablets));

      if (is_table_in_current_zone) {
        // If table is pinned to the same zone as this tserver, check that replicas are evenly
        // distributed.
        if (leader_count != expected_leaders_per_server ||
            static_cast<int>(total_count) != tablets.size()) {
          return false;
        }
      } else if (total_count != 0) {
        // If table is pinned to a different zone and has replicas on this tserver, then load
        // balancer is not respecting tablespaces.
        return false;
      }
    }
    return true;
  }

  // Verify the replicas of each table should be evenly distributed across each zone.
  Result<bool> VerifyReplicaDistribution(
      const std::vector<YBTableName> tables,
      const std::vector<std::pair<std::string, std::vector<size_t>>>& servers_group_by_zone) {
    for (const auto& table : tables) {
      google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
      RETURN_NOT_OK(
          client_->GetTabletsFromTableId(table.table_id(), /* max_tablets = */ 0, &tablets));
      for (const auto& [current_zone_table_name, current_zone_tserver_indexes] :
           servers_group_by_zone) {
        bool is_table_in_current_zone = table.table_name() == current_zone_table_name;
        if (!VERIFY_RESULT(VerifyTableReplicaDistributionInZone(
                tablets, current_zone_tserver_indexes, is_table_in_current_zone))) {
          return false;
        }
      }
    }
    return true;
  }
};

TEST_F(GeoTransactionsTest, YB_DISABLE_TEST_IN_TSAN(TestTransactionTabletSelection)) {
  constexpr int tables_per_region = 1;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_promote_nonlocal_transactions_to_global) = false;
  SetupTablesAndTablespaces(tables_per_region);

  // No local transaction tablets yet.
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);

  CreateTransactionTable(kOtherRegion);

  // No local transaction tablets in region, but local transaction tablets exist in general.
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);

  CreateTransactionTable(kLocalRegion);

  // Local transaction tablets exist in region.
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kLocal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kFalse, ExpectedLocality::kGlobal);
  CheckAbort(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, 1 /* num_aborts */);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_promote_nonlocal_transactions_to_global) = true;

  // Promotion now allowed. We do not check the status tablet for the promoted case in this test,
  // because the transaction object we have access to here is from the original take request sent
  // to the tserver, which is normally discarded and thus not kept up to date.
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kLocal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kNoCheck);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
}

TEST_F(GeoTransactionsTest, YB_DISABLE_TEST_IN_TSAN(TestNonlocalAbort)) {
  constexpr int tables_per_region = 1;
  constexpr size_t num_aborts = 500;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_promote_nonlocal_transactions_to_global) = false;

  SetupTablesAndTablespaces(tables_per_region);

  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);

  CreateTransactionTable(kLocalRegion);

  CheckAbort(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, num_aborts);
}

TEST_F(GeoTransactionsTest, YB_DISABLE_TEST_IN_TSAN(TestMultiRegionTransactionTable)) {
  constexpr int tables_per_region = 1;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = false;

  SetupTablesAndTablespaces(tables_per_region);

  CreateMultiRegionTransactionTable();

  // Should be treated the same as no transaction table.
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
}

TEST_F(GeoTransactionsTest, YB_DISABLE_TEST_IN_TSAN(TestAutomaticLocalTransactionTableCreation)) {
  constexpr int tables_per_region = 1;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_promote_nonlocal_transactions_to_global) = false;
  SetupTablesAndTablespaces(tables_per_region);

  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kLocal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kFalse, ExpectedLocality::kGlobal);
  CheckAbort(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, 1 /* num_aborts */);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);

  DropTablesAndTablespaces();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = false;
  SetupTablesAndTablespaces(tables_per_region);

  // Transaction tables created earlier should no longer have a placement and should be deleted.
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
}

TEST_F(GeoTransactionsTest,
       YB_DISABLE_TEST_IN_TSAN(TestAutomaticLocalTransactionTableCreationWithAlter)) {
  constexpr int tables_per_region = 1;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = true;
  SetupTablesWithAlter(tables_per_region);

  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kLocal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
  CheckSuccess(
      kOtherRegion, SetGlobalTransactionsGFlag::kTrue, SetGlobalTransactionSessionVar::kTrue,
      InsertToLocalFirst::kTrue, ExpectedLocality::kGlobal);
}

TEST_F(GeoTransactionsTest, YB_DISABLE_TEST_IN_TSAN(TestTransactionTableDeletion)) {
  constexpr int tables_per_region = 2;
  const auto long_txn_time = 10000ms;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_promote_nonlocal_transactions_to_global) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_ts_rpc_timeout_ms) = 5000;
  SetupTablesAndTablespaces(tables_per_region);

  CreateTransactionTable(kLocalRegion);

  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kFalse, ExpectedLocality::kLocal);

  std::vector<pgwrapper::PGConn> connections;
  for (int i = 0; i < 2; ++i) {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0$1_2(value) VALUES (1000)", kTablePrefix, kLocalRegion));
    connections.push_back(std::move(conn));
  }

  // This deletion should not go through until the long-running transactions end.
  StartDeleteTransactionTable(kLocalRegion);

  // New transactions should not use the table being deleted, even though it has not finished
  // deleting yet.
  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kFalse, ExpectedLocality::kGlobal);

  std::this_thread::sleep_for(long_txn_time);

  CheckSuccess(
      kLocalRegion, SetGlobalTransactionsGFlag::kFalse, SetGlobalTransactionSessionVar::kFalse,
      InsertToLocalFirst::kFalse, ExpectedLocality::kGlobal);

  ASSERT_OK(connections[0].CommitTransaction());
  ASSERT_OK(connections[1].RollbackTransaction());

  WaitForDeleteTransactionTableToFinish(kLocalRegion);

  // Restart to force participants to query transaction status for aborted transaction.
  ASSERT_OK(ShutdownTabletServersByRegion(kLocalRegion));
  ASSERT_OK(StartTabletServersByRegion(kLocalRegion));

  // Check data.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  int64_t count = EXPECT_RESULT(conn.FetchValue<int64_t>(strings::Substitute(
        "SELECT COUNT(*) FROM $0$1_1", kTablePrefix, kLocalRegion)));
  ASSERT_EQ(3, count);
  count = EXPECT_RESULT(conn.FetchValue<int64_t>(strings::Substitute(
        "SELECT COUNT(*) FROM $0$1_2", kTablePrefix, kLocalRegion)));
  ASSERT_EQ(1, count);
}

TEST_F(GeoTransactionsTest, YB_DISABLE_TEST_IN_TSAN(TestPreferredZone)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_tables_use_preferred_zones) = true;

  // Create tablespaces and tables.
  auto conn = ASSERT_RESULT(Connect());
  auto current_version = GetCurrentVersion();
  string table_name = kTablePrefix;

  std::string placement_blocks1;
  for (size_t i = 1; i <= NumRegions(); ++i) {
    placement_blocks1 += strings::Substitute(
        R"#($0{
              "cloud": "cloud0",
              "region": "rack$1",
              "zone": "zone",
              "min_num_replicas": 1,
              "leader_preference": $1
            })#",
        i > 1 ? "," : "", i);
  }

  std::string tablespace1_sql = strings::Substitute(
      R"#(
          CREATE TABLESPACE tablespace1 WITH (replica_placement='{
            "num_replicas": $0,
            "placement_blocks": [$1]}')
            )#",
      NumTabletServers(), placement_blocks1);

  std::string placement_blocks2;
  for (size_t i = 1; i <= NumRegions(); ++i) {
    placement_blocks2 += strings::Substitute(
        R"#($0{
              "cloud": "cloud0",
              "region": "rack$1",
              "zone": "zone",
              "min_num_replicas": 1,
              "leader_preference": $2
            })#",
        i > 1 ? "," : "", i, i == NumRegions() ? 1 : (i + 1));
  }

  std::string tablespace2_sql = strings::Substitute(
      R"#(
          CREATE TABLESPACE tablespace2 WITH (replica_placement='{
            "num_replicas": $0,
            "placement_blocks": [$1]}')
            )#",
      NumRegions(), placement_blocks2);

  ASSERT_OK(conn.Execute(tablespace1_sql));
  ASSERT_OK(conn.Execute(tablespace2_sql));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0(value int) TABLESPACE tablespace1", table_name));

  auto table_id = ASSERT_RESULT(GetTableIDFromTableName(table_name));
  auto tablet_uuid_set = ListTabletIdsForTable(cluster_.get(), table_id);
  auto table_uuids = std::vector<TabletId>(tablet_uuid_set.begin(), tablet_uuid_set.end());

  WaitForStatusTabletsVersion(++current_version);
  WaitForLoadBalanceCompletion();

  auto status_tablet_ids = ASSERT_RESULT(GetStatusTablets(1, ExpectedLocality::kLocal));
  ValidateAllTabletLeaderinZone(table_uuids, 1);
  ValidateAllTabletLeaderinZone(status_tablet_ids, 1);

  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 SET TABLESPACE tablespace2", table_name));

  WaitForStatusTabletsVersion(++current_version);
  WaitForLoadBalanceCompletion();

  status_tablet_ids = ASSERT_RESULT(GetStatusTablets(2, ExpectedLocality::kLocal));
  ValidateAllTabletLeaderinZone(table_uuids, 3);
  ValidateAllTabletLeaderinZone(status_tablet_ids, 3);

  ASSERT_OK(ShutdownTabletServersByRegion(3));
  WaitForLoadBalanceCompletion();
  ValidateAllTabletLeaderinZone(table_uuids, 1);
  ValidateAllTabletLeaderinZone(status_tablet_ids, 1);
}

// Create a geo-partitioned table with 3 partitions, each pinned to a different zone, with 3 tablet
// servers in each zone. Test that within each zone, leaders are evenly distributed.
TEST_F(GeoTransactionsTest, YB_DISABLE_TEST_IN_TSAN(TestLeaderDistribution)) {
  auto conn = ASSERT_RESULT(Connect());
  constexpr auto kRegionName = "new_rack";
  constexpr auto kGeoPartitionedTableName = "test_geo_partitioned_parent";
  constexpr auto kPartitionPrefix = "test_geo_partitioned_partition_";
  constexpr size_t kNumZones = 3;
  constexpr size_t kNumServersEachZone = 3;
  constexpr size_t kNumTabletsEachPartition = 90;

  // Create parent table.
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0(geo_partition VARCHAR) PARTITION BY LIST (geo_partition)",
      kGeoPartitionedTableName));

  std::vector<std::pair<std::string, std::vector<size_t>>> servers_group_by_zone(
      kNumZones, std::pair("", std::vector<size_t>(kNumServersEachZone)));

  // Create a table partition for each zone.
  for (size_t zone_idx = 0; zone_idx < kNumZones; ++zone_idx) {
    const auto zone_name = Format("z$0", zone_idx);
    const auto partition_name = kPartitionPrefix + zone_name;
    servers_group_by_zone[zone_idx].first = partition_name;
    // Create t-servers located in this zone.
    for (size_t tserver_idx = 0; tserver_idx < kNumServersEachZone; tserver_idx++) {
      auto options = EXPECT_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
      options.SetPlacement("cloud0", kRegionName, zone_name);
      ASSERT_OK(cluster_->AddTabletServer(options));
      servers_group_by_zone[zone_idx].second[tserver_idx] = cluster_->num_tablet_servers() - 1;
    }
    ASSERT_OK(cluster_->WaitForAllTabletServers());

    // Create tablespace pinned to this zone.
    const auto placement_block = Format(
        R"#({
              "cloud": "cloud0",
              "region": "$0",
              "zone": "$1",
              "min_num_replicas": $2
            })#",
        kRegionName, zone_name, kNumServersEachZone);
    const auto tablespace_name = Format("tablespace_$0", zone_name);
    const auto tablespace_sql = Format(
        R"#(
            CREATE TABLESPACE $0 WITH (replica_placement='{
              "num_replicas": $1,
              "placement_blocks": [$2]}')
            )#",
        tablespace_name, kNumServersEachZone, placement_block);
    ASSERT_OK(conn.Execute(tablespace_sql));

    // Create table partition pinned to tablespace.
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 PARTITION OF $1(geo_partition) FOR VALUES IN ('$2') "
        "TABLESPACE $3 split into $4 tablets",
        partition_name, kGeoPartitionedTableName, zone_name, tablespace_name,
        kNumTabletsEachPartition));
  }

  // Verify that leaders are distributed evenly on a per table per zone basis.
  const auto tables = ASSERT_RESULT(client_->ListTables(kPartitionPrefix));
  ASSERT_EQ(tables.size(), kNumZones);
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> { return VerifyReplicaDistribution(tables, servers_group_by_zone); },
      kWaitLeaderDistributionTimeout* kTimeMultiplier,
      "Timeout waiting for leaders to be evenly distributed"));
}

} // namespace client
} // namespace yb
