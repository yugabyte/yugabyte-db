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

// 90 leaders per zone and a total of 3 zones so 270 leader distributions. Worst-case even if the LB
// is doing 1 leader move per run (it does more than that in practice) then at max it will take 270
// runs i.e. 270 secs (1 run = 1 sec)
const auto kWaitLeaderDistributionTimeout = MonoDelta::FromMilliseconds(270000);

} // namespace

class PlacementBlock {
 public:
    std::string cloud;
    std::string region;
    std::string zone;
    size_t minNumReplicas;
    std::optional<size_t> leaderPreference;

    std::string json;

    PlacementBlock(std::string cloud, std::string region, std::string zone, size_t minNumReplicas,
                   std::optional<size_t> leaderPreference = std::nullopt)
        : cloud(std::move(cloud)), region(std::move(region)), zone(std::move(zone)),
          minNumReplicas(minNumReplicas), leaderPreference(leaderPreference) {}

    // Creates a placement block with the given region ID. For the purposes of this test, the cloud
    // is always "cloud0" and the zone is always "zone". The region is "rack<regionId>".
    PlacementBlock(size_t regionId, size_t minNumReplicas,
                   std::optional<size_t> leaderPreference = std::nullopt)
        : cloud("cloud0"), zone("zone"), minNumReplicas(minNumReplicas),
          leaderPreference(leaderPreference) {
        region = "rack" + std::to_string(regionId);
        json = generateJson();
    }

  const std::string& toJson() const {
    return json;
  }

  // Checks if the cloud, region, and zone match those of a CloudInfo protobuf.
  bool MatchesReplica(const CloudInfoPB& replica_cloud_info) const {
    return replica_cloud_info.placement_cloud() == cloud &&
           replica_cloud_info.placement_region() == region &&
           replica_cloud_info.placement_zone() == zone;
  }

 private:
    /*
    * Returns the JSON representation of the placement block. For example:
    * {
    *   "cloud": "aws",
    *   "region": "us-east-1",
    *   "zone": "us-east-1a",
    *   "min_num_replicas": 1,
    *   "leader_preference": 1
    * }
    */
    std::string generateJson() const {
        std::ostringstream os;
        os << "{\"cloud\":\"" << cloud << "\",\"region\":\"" << region << "\",\"zone\":\"" << zone
           << "\",\"min_num_replicas\":" << minNumReplicas;

        if (leaderPreference) {
            os << ",\"leader_preference\":" << *leaderPreference;
        }

        os << "}";
        return os.str();
    }
};

class Tablespace {
 public:
    std::string name;
    size_t numReplicas;
    std::vector<PlacementBlock> placementBlocks;

    std::string json;
    std::string createCmd;

    Tablespace(std::string name, size_t numReplicas, std::vector<PlacementBlock> placementBlocks)
    : name(name),
      numReplicas(numReplicas),
      placementBlocks(placementBlocks) {
        json = generateJson();
        createCmd = generateCreateCmd();
    }

    const std::vector<PlacementBlock>& getPlacementBlocks() const {
        return placementBlocks;
    }

    const std::string& toJson() const {
        return json;
    }

    const std::string& getCreateCmd() const {
        return createCmd;
    }

 private:
    /*
    * Generates the JSON representation of the tablespace. For example:
    * {
    *   "num_replicas": 1,
    *   "placement_blocks": [
    *     {
    *       "cloud": "aws",
    *       "region": "us-east-1",
    *       "zone": "us-east-1a",
    *       "min_num_replicas": 1,
    *       "leader_preference": 1
    *     }
    *   ]
    * }
    */
    std::string generateJson() const {
        std::ostringstream os;
        os << "{\"num_replicas\":" << numReplicas << ",\"placement_blocks\":[";

        for (size_t i = 0; i < placementBlocks.size(); ++i) {
            os << placementBlocks[i].toJson();
            if (i < placementBlocks.size() - 1) {
                os << ",";
            }
        }

        os << "]}";
        return os.str();
    }

    // Generates the SQL command to create this tablespace.
    std::string generateCreateCmd() const {
        std::ostringstream os;
        os << "CREATE TABLESPACE " << name << " WITH (replica_placement='" << toJson() << "');";
        return os.str();
    }
};

class PgTablespacesTest : public GeoTransactionsTestBase {
 public:
  void SetupObjects(pgwrapper::PGConn *conn, std::string tablespace_name) {
    std::string table_name = kTablePrefix + "_tbl";
    std::string index_name = kTablePrefix + "_idx";
    std::string mv_name = kTablePrefix + "_mv";
    /*
    * Create a table and an index/matview on a table in the given tablespace.
    */
    ASSERT_OK(conn->ExecuteFormat("CREATE TABLE $0(value int) TABLESPACE $1",
      table_name, tablespace_name));
    ASSERT_OK(conn->ExecuteFormat("CREATE INDEX $0 ON $1(value) TABLESPACE $2",
      index_name, table_name, tablespace_name));
    ASSERT_OK(conn->ExecuteFormat("CREATE MATERIALIZED VIEW $0 TABLESPACE $1 AS "
                                "SELECT * FROM $2", mv_name, tablespace_name, table_name));
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
  void VerifyMinNumReplicas(const std::string& table_name,
                            const std::vector<PlacementBlock>& placement_blocks) {
    auto tablets = ASSERT_RESULT(GetTabletsForTable(table_name));
    for (const auto& tablet : tablets) {
      std::vector<CloudInfoPB> replica_cloud_infos;
      for (const auto& replica : tablet.replicas()) {
        replica_cloud_infos.push_back(replica.ts_info().cloud_info());
      }

      for (const auto& placement_block : placement_blocks) {
        int64_t num_matching_replicas = CountMatchingReplicas(replica_cloud_infos, placement_block);
        ASSERT_GE(num_matching_replicas, placement_block.minNumReplicas)
            << "Not enough replicas with placement: " << placement_block.toJson()
            << " for table: " << table_name;
      }

      // Verify that there are no replicas outside of the given placement blocks.
      for (const auto& cloud_info : replica_cloud_infos) {
        bool replica_matches_placement_block =
            std::any_of(placement_blocks.begin(), placement_blocks.end(),
                        [&cloud_info](const auto& placement_block) {
                            return placement_block.MatchesReplica(cloud_info);
                        });

        ASSERT_TRUE(replica_matches_placement_block)
          << "Found replica for table: " << table_name
          << " that does not match the required placement. "
          << "Replica has placement: " << cloud_info.ShortDebugString();
      }
    }
  }

  // Verifies that the table, index, and matview are placed according to placement_blocks.
  void VerifyObjectPlacement(const std::vector<PlacementBlock>& placement_blocks) {
    VerifyMinNumReplicas(kTablePrefix + "_tbl", placement_blocks);
    VerifyMinNumReplicas(kTablePrefix + "_idx", placement_blocks);
    VerifyMinNumReplicas(kTablePrefix + "_mv", placement_blocks);
  }
};

// Test that the leader preference is respected for indexes/matviews.
TEST_F(PgTablespacesTest, TestPreferredZone) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_tables_use_preferred_zones) = true;

  // Create tablespaces and tables.
  auto conn = ASSERT_RESULT(Connect());
  auto current_version = GetCurrentVersion();
  string table_name = kTablePrefix + "_tbl";
  string index_name = kTablePrefix + "_idx";
  string mv_name = kTablePrefix + "_mv";

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

  size_t num_replicas = NumTabletServers();
  Tablespace tablespace_1("tablespace1", num_replicas, placement_blocks_1);
  Tablespace tablespace_2("tablespace2", num_replicas, placement_blocks_2);

  ASSERT_OK(conn.Execute(tablespace_1.getCreateCmd()));
  ASSERT_OK(conn.Execute(tablespace_2.getCreateCmd()));

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
  WaitForLoadBalanceCompletion();

  // Verify that all the tablet leaders are in region 1.
  auto status_tablet_ids = ASSERT_RESULT(GetStatusTablets(1, ExpectedLocality::kLocal));
  ValidateAllTabletLeaderinZone(table_uuids, 1);
  ValidateAllTabletLeaderinZone(index_uuids, 1);
  ValidateAllTabletLeaderinZone(mv_uuids, 1);
  ValidateAllTabletLeaderinZone(status_tablet_ids, 1);

  /*
   * Move the table/index/matview to tablespace2.
   */
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 SET TABLESPACE tablespace2", table_name));
  ASSERT_OK(conn.ExecuteFormat("ALTER INDEX $0 SET TABLESPACE tablespace2", index_name));
  ASSERT_OK(conn.ExecuteFormat("ALTER MATERIALIZED VIEW $0 SET TABLESPACE tablespace2", mv_name));

  WaitForStatusTabletsVersion(++current_version);
  WaitForLoadBalanceCompletion();

  // Now the tablet leaders should be in region 3, which is the most preferred block
  // for tablespace 2.
  status_tablet_ids = ASSERT_RESULT(GetStatusTablets(2, ExpectedLocality::kLocal));
  ValidateAllTabletLeaderinZone(table_uuids, 3);
  ValidateAllTabletLeaderinZone(mv_uuids, 3);
  ValidateAllTabletLeaderinZone(index_uuids, 3);
  ValidateAllTabletLeaderinZone(status_tablet_ids, 3);

  /*
   * Shut down region 3.
   */
  ASSERT_OK(ShutdownTabletServersByRegion(3));
  WaitForLoadBalanceCompletion();

  // Now the tablet leaders should be in region 1, which is the next most preferred block
  // for tablespace 2.
  ValidateAllTabletLeaderinZone(table_uuids, 1);
  ValidateAllTabletLeaderinZone(mv_uuids, 1);
  ValidateAllTabletLeaderinZone(index_uuids, 1);
  ValidateAllTabletLeaderinZone(status_tablet_ids, 1);
}

// Test that we can ALTER TABLE SET TABLESPACE to a tablespace that has a majority, but not all,
// of the placement blocks available.
TEST_F(PgTablespacesTest, TestAlterTableMajority) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_tables_use_preferred_zones) = true;

  // Create table, index, and matview.
  auto conn = ASSERT_RESULT(Connect());
  string table_name = kTablePrefix + "_tbl";
  string index_name = kTablePrefix + "_idx";
  string mv_name = kTablePrefix + "_mv";

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

  const size_t total_num_replicas = NumTabletServers();
  Tablespace valid_ts("valid_ts", total_num_replicas, valid_placement_blocks);
  Tablespace majority_ts("majority_ts", total_num_replicas, majority_placement_blocks);

  ASSERT_OK(conn.Execute(valid_ts.getCreateCmd()));
  ASSERT_OK(conn.Execute(majority_ts.getCreateCmd()));

  SetupObjects(&conn, "valid_ts");

  WaitForLoadBalanceCompletion();

  // Verify that the replicas for the table are distributed in valid_ts.
  VerifyObjectPlacement(valid_placement_blocks);

  // This ALTER TABLE should succeed because the majority (2/3)
  // of the placement blocks are available.
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 SET TABLESPACE majority_ts", table_name));
  ASSERT_OK(conn.ExecuteFormat("ALTER INDEX $0 SET TABLESPACE majority_ts", index_name));
  ASSERT_OK(conn.ExecuteFormat("ALTER MATERIALIZED VIEW $0 SET TABLESPACE majority_ts", mv_name));

  WaitForLoadBalanceCompletion();

  // Even though we have moved the table to majority_ts, the replicas should still be in the
  // valid_placement_blocks because there are only 2 valid placement blocks in majority_ts.
  VerifyObjectPlacement(valid_placement_blocks);
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
  string table_name = kTablePrefix + "_tbl";
  string index_name = kTablePrefix + "_idx";
  string mv_name = kTablePrefix + "_mv";

  // Create a tablespace with all the placement blocks available.
  std::vector<PlacementBlock> placement_blocks;
  for (size_t region_id = 1; region_id <= NumRegions(); ++region_id) {
    // For simplicity, set the leader preference to be the same as the region ID.
    size_t leader_preference = region_id;
    placement_blocks.emplace_back(region_id, /* minNumReplicas = */ 1, leader_preference);
  }
  const size_t total_num_replicas = NumTabletServers();
  Tablespace ts("ts", total_num_replicas, placement_blocks);

  ASSERT_OK(conn.Execute(ts.getCreateCmd()));

  SetupObjects(&conn, "ts");

  WaitForLoadBalanceCompletion();

  // Verify that the replicas for the table are distributed in ts.
  VerifyObjectPlacement(placement_blocks);

  // Remove the node in the last region.
  int shutdown_region = static_cast<int>(NumRegions());
  LOG(INFO) << "Test: Shutting down tablet servers in region " << shutdown_region;
  ASSERT_OK(ShutdownTabletServersByRegion(shutdown_region));

  // Add the node back.
  LOG(INFO) << "Test: Starting tablet servers in region " << shutdown_region;
  ASSERT_OK(StartTabletServersByRegion(shutdown_region));

  WaitForLoadBalanceCompletion();

  VerifyObjectPlacement(placement_blocks);
}

} // namespace client
} // namespace yb
