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
#include "yb/common/tablespace_parser.h"
#include "yb/util/test_tablespace_util.h"
#include "yb/tserver/tablet_server.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/mini_master.h"
#include "yb/master/master_client.pb.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/tools/yb-admin_client.h"
#include "yb/yql/pgwrapper/geo_transactions_test_base.h"
#include "yb/util/scope_exit.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/tsan_util.h"

using std::string;

DECLARE_int32(master_ts_rpc_timeout_ms);
DECLARE_bool(auto_create_local_transaction_tables);
DECLARE_bool(auto_promote_nonlocal_transactions_to_global);
DECLARE_bool(force_global_transactions);
DECLARE_bool(transaction_tables_use_preferred_zones);
DECLARE_bool(TEST_skip_election_when_fail_detected);

using namespace std::literals;

namespace yb {

namespace client {

namespace {

// 90 leaders per zone and a total of 3 zones so 270 leader distributions. Worst-case even if the LB
// is doing 1 leader move per run (it does more than that in practice) then at max it will take 270
// runs i.e. 270 secs (1 run = 1 sec)
const auto kWaitLeaderDistributionTimeout = MonoDelta::FromMilliseconds(270000);

} // namespace

enum class WildCardTestOption { kCloud, kRegion, kZone };

using ::yb::test::BuildPlacementString;
using ::yb::test::PlacementBlock;
using ::yb::test::Tablespace;

class PgTablespacesTest : public GeoTransactionsTestBase {
 private:
  // We start out with 3 tservers, but individual tests may add more.
  int num_tservers_ = 3;
 public:
  void SetupObjects(pgwrapper::PGConn *conn, std::string tablespace_name) {
    /*
    * Create a table and an index/matview on a table in the given tablespace.
    */
    ASSERT_OK(conn->ExecuteFormat("CREATE TABLE $0(value int) TABLESPACE $1",
      kTableName, tablespace_name));
    ASSERT_OK(conn->ExecuteFormat("CREATE INDEX $0 ON $1(value) TABLESPACE $2",
      kIndexName, kTableName, tablespace_name));
    ASSERT_OK(conn->ExecuteFormat("CREATE MATERIALIZED VIEW $0 TABLESPACE $1 AS "
                                "SELECT * FROM $2", kMatViewName, tablespace_name, kTableName));
  }

  Status AlterObjectsSetTablespace(
      pgwrapper::PGConn *conn, const std::string& new_tablespace_name) {
    RETURN_NOT_OK(conn->ExecuteFormat(
        "ALTER TABLE $0 SET TABLESPACE $1", kTableName, new_tablespace_name));
    RETURN_NOT_OK(conn->ExecuteFormat(
        "ALTER INDEX $0 SET TABLESPACE $1", kIndexName, new_tablespace_name));
    RETURN_NOT_OK(conn->ExecuteFormat(
        "ALTER MATERIALIZED VIEW $0 SET TABLESPACE $1", kMatViewName, new_tablespace_name));
    return Status::OK();
  }

  // Helper function to get tablets for a given table.
  Result<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>>
  GetTabletsForTable(const std::string& table_name) {
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    const auto yb_table_names = VERIFY_RESULT(client_->ListTables(table_name));

    if (yb_table_names.size() != 1) {
      return STATUS_FORMAT(InvalidArgument, "Expected exactly one table with name $0", table_name);
    }
    const auto& yb_table = yb_table_names[0];

    YB_RETURN_NOT_OK(client_->GetTabletsFromTableId(yb_table.table_id(),
                                                    /* max_tablets = */ 0, &tablets));
    return tablets;
  }

  // Get the number of replicas that are placed in the given block.
  int64_t CountMatchingReplicas(const std::vector<CloudInfoPB>& replica_cloud_infos,
                                const PlacementBlock& placement_block) {
    return std::count_if(replica_cloud_infos.begin(), replica_cloud_infos.end(),
                          [&placement_block](const CloudInfoPB& replica_cloud_info) {
                            return placement_block.MatchesReplica(replica_cloud_info);
                          });
  }

  // Given a table name and a list of placement blocks, verify that the tablets satisfy
  // the minimum number of replicas in each placement block and no replicas outside of the blocks.
  void VerifyTablePlacement(
    const std::string& table_name,
    const std::vector<PlacementBlock>& placement_blocks,
    const std::vector<PlacementBlock>& read_replica_placement_blocks,
    size_t total_num_replicas) {
    auto tablets = EXPECT_RESULT(GetTabletsForTable(table_name));

    if (!read_replica_placement_blocks.empty()) {
      std::ostringstream read_replica_blocks_stream;
      read_replica_blocks_stream << "[";
      for (size_t i = 0; i < read_replica_placement_blocks.size(); ++i) {
        if (i > 0) {
          read_replica_blocks_stream << ", ";
        }
        read_replica_blocks_stream << read_replica_placement_blocks[i].ToJson();
      }
      read_replica_blocks_stream << "]";
    }

    for (const auto& tablet : tablets) {
      SCOPED_TRACE(
          Format("VerifyTablePlacement: table=$0 tablet=$1", table_name, tablet.tablet_id()));
      std::vector<CloudInfoPB> live_replica_cloud_infos;
      std::vector<CloudInfoPB> read_replica_cloud_infos;
      live_replica_cloud_infos.reserve(tablet.replicas().size());
      read_replica_cloud_infos.reserve(tablet.replicas().size());
      for (const auto& replica : tablet.replicas()) {
        const auto& replica_cloud_info = replica.ts_info().cloud_info();
        if (replica.role() == PeerRole::READ_REPLICA) {
          read_replica_cloud_infos.push_back(replica_cloud_info);
        } else {
          live_replica_cloud_infos.push_back(replica_cloud_info);
        }
      }

      EXPECT_EQ(total_num_replicas, live_replica_cloud_infos.size())
          << "Tablet has " << live_replica_cloud_infos.size()
          << " live replicas, expected live replica count is " << total_num_replicas;
      if (tablet.has_expected_live_replicas()) {
        EXPECT_EQ(tablet.expected_live_replicas(),
                  static_cast<int>(live_replica_cloud_infos.size()))
            << "Not enough live replicas for placement policy";
      }

      for (const auto& placement_block : placement_blocks) {
        const int64_t num_matching_live_replicas =
            CountMatchingReplicas(live_replica_cloud_infos, placement_block);
        EXPECT_GE(num_matching_live_replicas, placement_block.min_num_replicas())
            << "Not enough live replicas for placement block: " << placement_block.ToJson();
      }

      if (read_replica_placement_blocks.empty()) {
        EXPECT_TRUE(read_replica_cloud_infos.empty())
            << "Tablet has unexpected read replica placements.";

        if (tablet.has_expected_read_replicas()) {
          EXPECT_EQ(0, tablet.expected_read_replicas());
        }
        continue;
      }

      if (tablet.has_expected_read_replicas()) {
        EXPECT_EQ(tablet.expected_read_replicas(),
                  static_cast<int>(read_replica_cloud_infos.size()));
      }

      EXPECT_FALSE(read_replica_cloud_infos.empty())
          << "Tablet does not have any read replicas despite having read replica configured.";

      for (const auto& placement_block : read_replica_placement_blocks) {
        const int64_t num_matching_read_replicas =
            CountMatchingReplicas(read_replica_cloud_infos, placement_block);
        EXPECT_GE(num_matching_read_replicas, placement_block.min_num_replicas())
            << "Not enough read replicas with placement: " << placement_block.ToJson();
      }

      for (const auto& cloud_info : read_replica_cloud_infos) {
        const bool matches_block = std::any_of(
            read_replica_placement_blocks.begin(), read_replica_placement_blocks.end(),
            [&cloud_info](const PlacementBlock& placement_block) {
              return placement_block.MatchesReplica(cloud_info);
            });
        EXPECT_TRUE(matches_block)
            << "Read replica is placed outside of the expected read replica placement blocks.";
      }
    }
  }

  // Verifies that the table, index, and matview are placed according to placement_blocks and
  // read replica placement blocks (if any).
  void VerifyObjectPlacement(
    const std::vector<PlacementBlock>& placement_blocks,
    size_t total_num_replicas,
    const std::vector<PlacementBlock>& read_replica_placement_blocks = {}) {
    for (const auto& object_name : object_names_) {
      VerifyTablePlacement(
          object_name, placement_blocks, read_replica_placement_blocks,
          total_num_replicas);
    }
  }

  void VerifyObjectPlacement(const Tablespace& tablespace) {
    VerifyObjectPlacement(
        tablespace.PlacementBlocks(),
        tablespace.NumReplicas(),
        tablespace.ReadReplicaPlacementBlocks());
  }

  void GeneratePlacementBlocks(
      WildCardTestOption wildcard_opt, int num_replicas,
      std::vector<PlacementBlock>* placement_blocks) {
    placement_blocks->clear();
    switch (wildcard_opt) {
      case WildCardTestOption::kCloud:
        // No placement blocks means any cloud/region/zone is ok.
        break;
      case WildCardTestOption::kRegion:
        placement_blocks->push_back(PlacementBlock("cloud0", "*", "*", num_replicas));
        break;

      case WildCardTestOption::kZone:
        placement_blocks->push_back(PlacementBlock("cloud0", "region1", "*", 2));
        if (num_replicas > 2)
          placement_blocks->push_back(PlacementBlock("cloud0", "region2", "*", 1));
        break;

      default:
        LOG(FATAL) << "Invalid wildcard option " << int(wildcard_opt);
        break;
    }
  }

  std::string GetWildcardYbAdminString(
      const std::vector<PlacementBlock>& placement_blocks) {
    return BuildPlacementString(placement_blocks);
  }

  // Adds additional tservers starting from region 4, 5, 6, ...
  Status AddAdditionalTabletServers(int num_tservers, bool read_replica = false) {
    for (int i = 1; i <= num_tservers; ++i) {
      const int region_id = num_tservers_ + i;
      auto options = VERIFY_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
      options.SetPlacement("cloud0", Format("region$0", region_id), "zone");
      if (read_replica) {
        options.placement_uuid = "read_replica";
        LOG(INFO) << "Creating read replica tserver in region " << region_id;
      } else {
        LOG(INFO) << "Creating live tserver in region " << region_id;
      }
      RETURN_NOT_OK(cluster_->AddTabletServer(options));
    }
    RETURN_NOT_OK(cluster_->WaitForAllTabletServers());
    num_tservers_ += num_tservers;
    return Status::OK();
  }

  void TestWildcardPlacement(bool use_yb_admin, WildCardTestOption wildcard_opt) {
    /* The base test creates tservers with
      (cloud0, region1, zone),
      (cloud0, region2, zone),
      (cloud0, region3, zone)
     We are adding two tservers afterwards
      (cloud0, region1, zone1)
      (cloud0, region1, zone2)
     */
    std::unique_ptr<tools::ClusterAdminClient> yb_admin_client;
    if (use_yb_admin) {
      std::string master_addrs = cluster_->GetMasterAddresses();
      yb_admin_client = std::make_unique<tools::ClusterAdminClient>(
        master_addrs,
         MonoDelta::FromSeconds(20));
      ASSERT_TRUE(yb_admin_client);
      ASSERT_OK(yb_admin_client->Init());
    }

    auto options = EXPECT_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
    options.SetPlacement("cloud0", "region1", "zone1");
    ASSERT_OK(cluster_->AddTabletServer(options));

    options.SetPlacement("cloud0", "region1", "zone2");
    ASSERT_OK(cluster_->AddTabletServer(options));

    static constexpr auto kTableName = "test_table";
    static constexpr auto kTablespace1 = "ts1";
    static constexpr auto kTablespace2 = "ts2";

    /* Create a RF2 wildcard placement table, verify simple SQL cmds */
    std::vector<PlacementBlock> placement_blocks;
    int num_replicas = 2;
    GeneratePlacementBlocks(wildcard_opt, num_replicas, &placement_blocks);
    if (!use_yb_admin) {
      Tablespace tablespace(kTablespace1, num_replicas, placement_blocks);
      auto conn = ASSERT_RESULT(Connect());
      auto create_cmd = tablespace.CreateCmd();
      LOG(INFO) << "Creating tablespace using " << create_cmd;
      ASSERT_OK(conn.Execute(create_cmd));
    } else {
      ASSERT_OK(
        yb_admin_client->ModifyPlacementInfo(
          GetWildcardYbAdminString(placement_blocks),
          num_replicas, "" /*optional uuid*/));
    }
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (k INT PRIMARY KEY) $1 $2", kTableName,
        (use_yb_admin ? "" : "TABLESPACE "), (use_yb_admin ? "" : kTablespace1)));

    VerifyTablePlacement(kTableName, placement_blocks, /* read_replica_placement_blocks = */ {},
                         num_replicas);

    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1)", kTableName));
    auto rows = ASSERT_RESULT(
        conn.FetchRows<int32_t>(yb::Format("SELECT k FROM $0 ORDER BY k", kTableName)));
    ASSERT_EQ(rows, std::vector({1}));

    /* Change the table to RF3 wildcard placement, verify simple SQL cmds */
    num_replicas = 3;
    GeneratePlacementBlocks(wildcard_opt, num_replicas, &placement_blocks);
    if (!use_yb_admin) {
      Tablespace tablespace(kTablespace2, num_replicas, placement_blocks);
      auto conn = ASSERT_RESULT(Connect());
      auto create_cmd = tablespace.CreateCmd();
      LOG(INFO) << "Creating tablespace using " << create_cmd;
      ASSERT_OK(conn.Execute(create_cmd));
      ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 SET TABLESPACE $1", kTableName, kTablespace2));
    } else {
      ASSERT_OK(
        yb_admin_client->ModifyPlacementInfo(
          GetWildcardYbAdminString(placement_blocks),
          num_replicas, "" /*optional uuid*/));
    }
    WaitForLoadBalanceCompletion();

    VerifyTablePlacement(kTableName, placement_blocks, /* read_replica_placement_blocks = */ {},
                         num_replicas);

    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (2)", kTableName));
    rows = ASSERT_RESULT(
        conn.FetchRows<int32_t>(yb::Format("SELECT k FROM $0 ORDER BY k", kTableName)));
    ASSERT_EQ(rows, std::vector({1, 2}));
  }

 private:
  const std::vector<std::string> object_names_ = {kTableName, kIndexName, kMatViewName};
};

// Test that the leader preference is respected for indexes/matviews.
TEST_F(PgTablespacesTest, TestPreferredZone) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_tables_use_preferred_zones) = true;

  // Create tablespaces and tables.
  auto conn = ASSERT_RESULT(Connect());
  auto current_version = GetCurrentVersion();
  string table_name = kTableName;
  string index_name = kIndexName;
  string mv_name = kMatViewName;

  // Create two tablespaces.
  std::vector<PlacementBlock> placement_blocks_1;
  std::vector<PlacementBlock> placement_blocks_2;

  // In tablespace 1, region 1 is most preferred.
  // In tablespace 2, region 3 is most preferred, with region 1 being the next most preferred.
  for (size_t region_id = 1; region_id <= NumRegions(); ++region_id) {
    placement_blocks_1.emplace_back(region_id, /* minNumReplicas = */ 1,
      /* leaderPreference = */ region_id);
    placement_blocks_2.emplace_back(region_id, /* minNumReplicas = */ 1,
      /* leaderPreference = */ region_id == NumRegions() ? 1 : (region_id + 1));
  }

  int32_t num_replicas = static_cast<int32_t>(NumTabletServers());
  Tablespace tablespace_1("tablespace1", num_replicas, placement_blocks_1);
  Tablespace tablespace_2("tablespace2", num_replicas, placement_blocks_2);

  ASSERT_OK(conn.Execute(tablespace_1.CreateCmd()));
  ASSERT_OK(conn.Execute(tablespace_2.CreateCmd()));

  SetupObjects(&conn, "tablespace1");

  auto table_id = ASSERT_RESULT(GetTableIDFromTableName(table_name));
  auto table_tablet_uuid_set = ListTabletIdsForTable(cluster_.get(), table_id);
  auto table_uuids = std::vector<TabletId>(table_tablet_uuid_set.begin(),
    table_tablet_uuid_set.end());

  auto index_id = ASSERT_RESULT(GetTableIDFromTableName(index_name));
  auto index_tablet_uuid_set = ListTabletIdsForTable(cluster_.get(), index_id);
  auto index_uuids = std::vector<TabletId>(index_tablet_uuid_set.begin(),
    index_tablet_uuid_set.end());

    auto mv_id = ASSERT_RESULT(GetTableIDFromTableName(mv_name));
  auto mv_tablet_uuid_set = ListTabletIdsForTable(cluster_.get(), mv_id);
  auto mv_uuids = std::vector<TabletId>(mv_tablet_uuid_set.begin(), mv_tablet_uuid_set.end());

  WaitForStatusTabletsVersion(++current_version);

  // Verify that all the tablet leaders are in region 1.
  auto status_tablet_ids = ASSERT_RESULT(GetStatusTablets(1, ExpectedLocality::kLocal));
  auto WaitAllInZone = [&](int zone) {
    return WaitFor([&, zone] {
      // Now the tablet leaders should be in region 1, which is the next most preferred block
      // for tablespace 2.
      return AllTabletLeaderInZone(table_uuids, zone) &&
             AllTabletLeaderInZone(mv_uuids, zone) &&
             AllTabletLeaderInZone(index_uuids, zone) &&
             AllTabletLeaderInZone(status_tablet_ids, zone);
    }, 30s, Format("All in zone $0", zone));
  };

  ASSERT_OK(WaitAllInZone(1));

  /*
   * Move the table/index/matview to tablespace2.
   */
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 SET TABLESPACE tablespace2", table_name));
  ASSERT_OK(conn.ExecuteFormat("ALTER INDEX $0 SET TABLESPACE tablespace2", index_name));
  ASSERT_OK(conn.ExecuteFormat("ALTER MATERIALIZED VIEW $0 SET TABLESPACE tablespace2", mv_name));

  WaitForStatusTabletsVersion(++current_version);

  // Now the tablet leaders should be in region 3, which is the most preferred block
  // for tablespace 2.
  status_tablet_ids = ASSERT_RESULT(GetStatusTablets(2, ExpectedLocality::kLocal));
  ASSERT_OK(WaitAllInZone(3));

  /*
   * Shut down region 3.
   */
  ASSERT_OK(ShutdownTabletServersByRegion(3));
  ASSERT_OK(WaitAllInZone(1));
}

// Test that we can ALTER TABLE SET TABLESPACE to a tablespace that has a majority, but not all,
// of the placement blocks available.
TEST_F(PgTablespacesTest, TestAlterTableMajority) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_tables_use_preferred_zones) = true;

  // Create table, index, and matview.
  auto conn = ASSERT_RESULT(Connect());

  // Create two tablespaces.
  // valid_ts has all the placement blocks available.
  // majority_ts has all but one of the placement blocks available.
  std::vector<PlacementBlock> valid_placement_blocks;
  std::vector<PlacementBlock> majority_placement_blocks;
  for (size_t region_id = 1; region_id < NumRegions(); ++region_id) {
    size_t leader_preference = region_id;
    valid_placement_blocks.emplace_back(region_id, 1, leader_preference);
    majority_placement_blocks.emplace_back(region_id, 1, leader_preference);
  }

  // Add a third placement block that is available for valid_ts.
  valid_placement_blocks.emplace_back(3, 1, 3);
  // The third placement block is invalid for majority_ts.
  majority_placement_blocks.emplace_back(0, 1, 1);

  const int32_t total_num_replicas = static_cast<int32_t>(NumTabletServers());
  Tablespace valid_ts("valid_ts", total_num_replicas, valid_placement_blocks);
  Tablespace majority_ts("majority_ts", total_num_replicas, majority_placement_blocks);

  ASSERT_OK(conn.Execute(valid_ts.CreateCmd()));
  ASSERT_OK(conn.Execute(majority_ts.CreateCmd()));

  SetupObjects(&conn, "valid_ts");

  // Verify that the replicas for the table are distributed in valid_ts.
  VerifyObjectPlacement(valid_placement_blocks, total_num_replicas);

  // This ALTER TABLE should succeed because the majority (2/3)
  // of the placement blocks are available.
  ASSERT_OK(
      conn.ExecuteFormat("ALTER TABLE $0 SET TABLESPACE majority_ts", kTableName));
  ASSERT_OK(
      conn.ExecuteFormat("ALTER INDEX $0 SET TABLESPACE majority_ts", kIndexName));
  ASSERT_OK(
      conn.ExecuteFormat("ALTER MATERIALIZED VIEW $0 SET TABLESPACE majority_ts", kMatViewName));

  // Even though we have moved the table to majority_ts, the replicas should still be in the
  // valid_placement_blocks because there are only 2 valid placement blocks in majority_ts.
  VerifyObjectPlacement(valid_placement_blocks, total_num_replicas);
}

// Tests the following:
// 1. Create a table.
// 2. ALTER TABLE SET TABLESPACE to a tablespace.
// 3. Remove a node that is in the tablespace. Verify the placement still holds.
// 4. Add the node back. Verify the placement still holds.
TEST_F(PgTablespacesTest, TestAlterTableScaling) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_tables_use_preferred_zones) = true;

  // Create table, index, and matview.
  auto conn = ASSERT_RESULT(Connect());

  // Create a tablespace with all the placement blocks available.
  std::vector<PlacementBlock> placement_blocks;
  for (size_t region_id = 1; region_id <= NumRegions(); ++region_id) {
    // For simplicity, set the leader preference to be the same as the region ID.
    size_t leader_preference = region_id;
    placement_blocks.emplace_back(region_id, /* minNumReplicas = */ 1, leader_preference);
  }
  const int32_t total_num_replicas = static_cast<int32_t>(NumTabletServers());
  Tablespace ts("ts", total_num_replicas, placement_blocks);

  ASSERT_OK(conn.Execute(ts.CreateCmd()));

  SetupObjects(&conn, "ts");

  WaitForLoadBalanceCompletion();

  // Verify that the replicas for the table are distributed in ts.
  VerifyObjectPlacement(placement_blocks, total_num_replicas);

  // Remove the node in the last region.
  int shutdown_region = static_cast<int>(NumRegions());
  LOG(INFO) << "Test: Shutting down tablet servers in region " << shutdown_region;
  ASSERT_OK(ShutdownTabletServersByRegion(shutdown_region));

  // Add the node back.
  LOG(INFO) << "Test: Starting tablet servers in region " << shutdown_region;
  ASSERT_OK(StartTabletServersByRegion(shutdown_region));

  WaitForLoadBalanceCompletion();

  VerifyObjectPlacement(placement_blocks, total_num_replicas);
}

TEST_F(PgTablespacesTest, TombstonedTabletInYbLocalTablets) {
  static constexpr auto kTableName = "test_table";
  static constexpr auto kTablespace1 = "ts1";
  static constexpr auto kTablespace2 = "ts2";
  const auto tablet_state_query = Format(
      "SELECT state FROM yb_local_tablets WHERE table_name = '$0' LIMIT 1",
      kTableName);

  std::vector<PlacementBlock> placement_blocks_1;
  std::vector<PlacementBlock> placement_blocks_2;

  placement_blocks_1.emplace_back(/* regionId = */ 1, /* minNumReplicas = */ 1);
  placement_blocks_2.emplace_back(/* regionId = */ 2, /* minNumReplicas = */ 1);

  Tablespace tablespace_1(kTablespace1, /* numReplicas = */ 1, placement_blocks_1);
  Tablespace tablespace_2(kTablespace2, /* numReplicas = */ 1, placement_blocks_2);

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(tablespace_1.CreateCmd()));
  ASSERT_OK(conn.Execute(tablespace_2.CreateCmd()));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT) TABLESPACE $1", kTableName, kTablespace1));
  auto old_tablet_state = ASSERT_RESULT(
      conn.FetchRow<std::string>(tablet_state_query));
  ASSERT_STR_EQ(old_tablet_state, "TABLET_DATA_READY");
  ASSERT_OK(conn.ExecuteFormat(
      "ALTER TABLE $0 SET TABLESPACE $1", kTableName, kTablespace2));
  SleepFor(10s * kTimeMultiplier);
  auto new_tablet_state = ASSERT_RESULT(
      conn.FetchRow<std::string>(tablet_state_query));
  ASSERT_STR_EQ(new_tablet_state, "TABLET_DATA_TOMBSTONED");
}

TEST_F(PgTablespacesTest, TestWildcardCloudTablespace) {
  TestWildcardPlacement(false, WildCardTestOption::kCloud);
}

TEST_F(PgTablespacesTest, TestWildcardCloudYbAdmin) {
  TestWildcardPlacement(true, WildCardTestOption::kCloud);
}

TEST_F(PgTablespacesTest, TestWildcardRegionTablespace) {
  TestWildcardPlacement(false, WildCardTestOption::kRegion);
}

TEST_F(PgTablespacesTest, TestWildcardRegionYbAdmin) {
  TestWildcardPlacement(true, WildCardTestOption::kRegion);
}

TEST_F(PgTablespacesTest, TestWildcardZoneTablespace) {
  TestWildcardPlacement(false, WildCardTestOption::kZone);
}

TEST_F(PgTablespacesTest, TestWildcardZoneYbAdmin) {
  TestWildcardPlacement(true, WildCardTestOption::kZone);
}

} // namespace client
} // namespace yb
