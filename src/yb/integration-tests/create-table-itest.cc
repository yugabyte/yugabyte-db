// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/integration-tests/create-table-itest-base.h"

METRIC_DECLARE_entity(server);
METRIC_DECLARE_entity(tablet);
METRIC_DECLARE_gauge_int64(is_raft_leader);
METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerAdminService_CreateTablet);
METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerAdminService_DeleteTablet);

DECLARE_int32(ycql_num_tablets);
DECLARE_int32(yb_num_shards_per_tserver);

namespace yb {

class CreateTableITest : public CreateTableITestBase {};

// TODO(bogdan): disabled until ENG-2687
TEST_F(CreateTableITest, DISABLED_TestCreateRedisTable) {
  const string cloud = "aws";
  const string region = "us-west-1";
  const string zone = "a";

  const int kNumReplicas = 3;
  vector<string> flags = {Substitute("--placement_cloud=$0", cloud),
                          Substitute("--placement_region=$0", region),
                          Substitute("--placement_zone=$0", zone)};
  ASSERT_NO_FATALS(StartCluster(flags, flags, kNumReplicas));

  master::ReplicationInfoPB replication_info;
  replication_info.mutable_live_replicas()->set_num_replicas(kNumReplicas);
  auto* placement_block = replication_info.mutable_live_replicas()->add_placement_blocks();
  auto* cloud_info = placement_block->mutable_cloud_info();
  cloud_info->set_placement_cloud(cloud);
  cloud_info->set_placement_region(region);
  cloud_info->set_placement_zone(zone);
  placement_block->set_min_num_replicas(kNumReplicas);

  // Successful table create.
  ASSERT_OK(
      CreateTableWithPlacement(replication_info, "success_base", YBTableType::REDIS_TABLE_TYPE));
}

// TODO(bogdan): disabled until ENG-2687
TEST_F(CreateTableITest, DISABLED_TestCreateWithPlacement) {
  const string cloud = "aws";
  const string region = "us-west-1";
  const string zone = "a";

  const int kNumReplicas = 3;
  vector<string> flags = {Substitute("--placement_cloud=$0", cloud),
                          Substitute("--placement_region=$0", region),
                          Substitute("--placement_zone=$0", zone)};
  ASSERT_NO_FATALS(StartCluster(flags, flags, kNumReplicas));

  master::ReplicationInfoPB replication_info;
  replication_info.mutable_live_replicas()->set_num_replicas(kNumReplicas);
  auto* placement_block = replication_info.mutable_live_replicas()->add_placement_blocks();
  auto* cloud_info = placement_block->mutable_cloud_info();
  cloud_info->set_placement_cloud(cloud);
  cloud_info->set_placement_region(region);
  cloud_info->set_placement_zone(zone);
  placement_block->set_min_num_replicas(kNumReplicas);

  // Successful table create.
  ASSERT_OK(CreateTableWithPlacement(replication_info, "success_base"));

  // Cannot create table with 4 replicas when only 3 TS available.
  {
    auto copy_replication_info = replication_info;
    copy_replication_info.mutable_live_replicas()->set_num_replicas(kNumReplicas + 1);
    Status s = CreateTableWithPlacement(copy_replication_info, "fail_num_replicas");
    ASSERT_TRUE(s.IsInvalidArgument());
  }

  // Cannot create table in locations we have no servers.
  {
    auto copy_replication_info = replication_info;
    auto* new_placement =
        copy_replication_info.mutable_live_replicas()->mutable_placement_blocks(0);
    new_placement->mutable_cloud_info()->set_placement_zone("b");
    Status s = CreateTableWithPlacement(copy_replication_info, "fail_zone");
    ASSERT_TRUE(s.IsTimedOut());
  }

  // Set cluster config placement and test table placement interaction. Right now, this should fail
  // instantly, as we do not support cluster and table level at the same time.
  ASSERT_OK(client_->SetReplicationInfo(replication_info));
  {
    Status s = CreateTableWithPlacement(replication_info, "fail_table_placement");
    ASSERT_TRUE(s.IsInvalidArgument());
  }
}

// Regression test for an issue seen when we fail to create a majority of the
// replicas in a tablet. Previously, we'd still consider the tablet "RUNNING"
// on the master and finish the table creation, even though that tablet would
// be stuck forever with its minority never able to elect a leader.
TEST_F(CreateTableITest, TestCreateWhenMajorityOfReplicasFailCreation) {
  const int kNumReplicas = 3;
  const int kNumTablets = 1;
  vector<string> ts_flags;
  vector<string> master_flags;
  master_flags.push_back("--tablet_creation_timeout_ms=1000");
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags, kNumReplicas));

  // Shut down 2/3 of the tablet servers.
  cluster_->tablet_server(1)->Shutdown();
  cluster_->tablet_server(2)->Shutdown();

  // Try to create a single-tablet table.
  // This won't succeed because we can't create enough replicas to get
  // a quorum.
  ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name(),
                                                kTableName.namespace_type()));
  std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
  client::YBSchema client_schema(client::YBSchemaFromSchema(GetSimpleTestSchema()));
  ASSERT_OK(table_creator->table_name(kTableName)
            .schema(&client_schema)
            .num_tablets(kNumTablets)
            .wait(false)
            .Create());

  // Sleep until we've seen a couple retries on our live server.
  int64_t num_create_attempts = 0;
  while (num_create_attempts < 3) {
    SleepFor(MonoDelta::FromMilliseconds(100));
    num_create_attempts = ASSERT_RESULT(cluster_->tablet_server(0)->GetMetric<int64>(
        &METRIC_ENTITY_server,
        "yb.tabletserver",
        &METRIC_handler_latency_yb_tserver_TabletServerAdminService_CreateTablet,
        "total_count"));
    LOG(INFO) << "Waiting for the master to retry creating the tablet 3 times... "
              << num_create_attempts << " RPCs seen so far";

    // The CreateTable operation should still be considered in progress, even though
    // we'll be successful at creating a single replica.
    bool in_progress = false;
    ASSERT_OK(client_->IsCreateTableInProgress(kTableName, &in_progress));
    ASSERT_TRUE(in_progress);
  }

  // Once we restart the servers, we should succeed at creating a healthy
  // replicated tablet.
  ASSERT_OK(cluster_->tablet_server(1)->Restart());
  ASSERT_OK(cluster_->tablet_server(2)->Restart());

  // We should eventually finish the table creation we started earlier.
  bool in_progress = false;
  while (in_progress) {
    LOG(INFO) << "Waiting for the master to successfully create the table...";
    ASSERT_OK(client_->IsCreateTableInProgress(kTableName, &in_progress));
    SleepFor(MonoDelta::FromMilliseconds(100));
  }

  // The server that was up from the beginning should be left with only
  // one tablet, eventually, since the tablets which failed to get created
  // properly should get deleted.
  vector<string> tablets;
  int wait_iter = 0;
  while (tablets.size() != kNumTablets && wait_iter++ < 100) {
    LOG(INFO) << "Waiting for only " << kNumTablets << " tablet(s) to be left on TS 0. "
              << "Currently have: " << tablets;
    SleepFor(MonoDelta::FromMilliseconds(100));
    tablets = inspect_->ListTabletsWithDataOnTS(0);
  }
  ASSERT_EQ(tablets.size(), kNumTablets) << "Tablets on TS0: " << tablets;
}

// Ensure that, when a table is created,
// the tablets are well spread out across the machines in the cluster.
TEST_F(CreateTableITest, TestSpreadReplicasEvenly) {
  const int kNumServers = 10;
  const int kNumTablets = 20;
  vector<string> ts_flags;
  vector<string> master_flags;
  ts_flags.push_back("--never_fsync");  // run faster on slow disks
  master_flags.push_back("--enable_load_balancing=false");  // disable load balancing moves
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags, kNumServers));

  ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name(),
                                                kTableName.namespace_type()));
  std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
  client::YBSchema client_schema(client::YBSchemaFromSchema(GetSimpleTestSchema()));
  ASSERT_OK(table_creator->table_name(kTableName)
            .schema(&client_schema)
            .num_tablets(kNumTablets)
            .Create());

  // Load should be equal on all the 10 servers without any deviation.
  for (int ts_idx = 0; ts_idx < kNumServers; ts_idx++) {
    auto num_replicas = inspect_->ListTabletsOnTS(ts_idx).size();
    LOG(INFO) << "TS " << ts_idx << " has " << num_replicas << " tablets";
    ASSERT_EQ(num_replicas, 6);
  }
}

TEST_F(CreateTableITest, TestNoAllocBlacklist) {
  const int kNumServers = 4;
  const int kNumTablets = 24;
  vector<string> ts_flags;
  vector<string> master_flags;
  ts_flags.push_back("--never_fsync");  // run faster on slow disks
  master_flags.push_back("--enable_load_balancing=false");  // disable load balancing moves
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags, kNumServers));
  // add TServer to blacklist
  ASSERT_OK(cluster_->AddTServerToBlacklist(cluster_->master(), cluster_->tablet_server(1)));
  // create table
  ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name(),
                                                kTableName.namespace_type()));
  std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
  client::YBSchema client_schema(client::YBSchemaFromSchema(GetSimpleTestSchema()));
  ASSERT_OK(table_creator->table_name(kTableName)
                .schema(&client_schema)
                .num_tablets(kNumTablets)
                .Create());
  // check that no tablets have been allocated to blacklisted TServer
  ASSERT_EQ(inspect_->ListTabletsOnTS(1).size(), 0);
}

TEST_F(CreateTableITest, TableColocationRemoteBootstrapTest) {
  const int kNumReplicas = 3;
  string parent_table_id;
  string tablet_id;
  vector<string> ts_flags;
  vector<string> master_flags;

  ts_flags.push_back("--follower_unavailable_considered_failed_sec=3");
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags, kNumReplicas));
  ASSERT_OK(
      client_->CreateNamespace("colocation_test", boost::none /* db */, "" /* creator */,
                               "" /* ns_id */, "" /* src_ns_id */,
                               boost::none /* next_pg_oid */, nullptr /* txn */, true));

  {
    string ns_id;
    auto namespaces = ASSERT_RESULT(client_->ListNamespaces(boost::none));
    for (const auto& ns : namespaces) {
      if (ns.name() == "colocation_test") {
        ns_id = ns.id();
        break;
      }
    }
    ASSERT_FALSE(ns_id.empty());
    parent_table_id = master::GetColocatedDbParentTableId(ns_id);
  }

  {
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(WaitFor(
        [&]() -> bool {
          EXPECT_OK(client_->GetTabletsFromTableId(parent_table_id, 0, &tablets));
          return tablets.size() == 1;
        },
        MonoDelta::FromSeconds(30), "Create colocated tablet"));
    tablet_id = tablets[0].tablet_id();
  }

  string rocksdb_dir = JoinPathSegments(
      cluster_->data_root(), "ts-1", "yb-data", "tserver", "data", "rocksdb",
      "table-" + parent_table_id, "tablet-" + tablet_id);
  string wal_dir = JoinPathSegments(
      cluster_->data_root(), "ts-1", "yb-data", "tserver", "wals", "table-" + parent_table_id,
      "tablet-" + tablet_id);
  std::function<Result<bool>()> dirs_exist = [&] {
    return Env::Default()->FileExists(rocksdb_dir) && Env::Default()->FileExists(wal_dir);
  };

  ASSERT_OK(WaitFor(dirs_exist, MonoDelta::FromSeconds(30), "Create data and wal directories"));

  // Stop a tablet server and create a new tablet server. This will trigger a remote bootstrap on
  // the new tablet server.
  cluster_->tablet_server(2)->Shutdown();
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(4, MonoDelta::FromSeconds(20)));

  // Remote bootstrap should create the correct tablet directory for the new tablet server.
  rocksdb_dir = JoinPathSegments(
      cluster_->data_root(), "ts-4", "yb-data", "tserver", "data", "rocksdb",
      "table-" + parent_table_id, "tablet-" + tablet_id);
  wal_dir = JoinPathSegments(
      cluster_->data_root(), "ts-4", "yb-data", "tserver", "wals", "table-" + parent_table_id,
      "tablet-" + tablet_id);
  ASSERT_OK(WaitFor(dirs_exist, MonoDelta::FromSeconds(100), "Create data and wal directories"));
}

// Skipping in TSAN because of an error with initdb in TSAN when ysql is enabled
TEST_F(CreateTableITest, YB_DISABLE_TEST_IN_TSAN(TablegroupRemoteBootstrapTest)) {
  const int kNumReplicas = 3;
  string parent_table_id;
  string tablet_id;
  vector<string> ts_flags;
  vector<string> master_flags;
  string namespace_name = "tablegroup_test_namespace_name";
  TablegroupId tablegroup_id = "11223344556677889900aabbccddeeff";
  TablespaceId tablespace_id = "";
  string namespace_id;

  ts_flags.push_back("--follower_unavailable_considered_failed_sec=3");
  ts_flags.push_back("--ysql_beta_feature_tablegroup=true");
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags, kNumReplicas, 1 /* masters */,
                                true /* enable_ysql (allows load balancing) */));

  ASSERT_OK(client_->CreateNamespace(namespace_name, YQL_DATABASE_PGSQL, "" /* creator */,
                                     "" /* ns_id */, "" /* src_ns_id */,
                                     boost::none /* next_pg_oid */, nullptr /* txn */, false));

  {
    auto namespaces = ASSERT_RESULT(client_->ListNamespaces(boost::none));
    for (const auto& ns : namespaces) {
      if (ns.name() == namespace_name) {
        namespace_id = ns.id();
        break;
      }
    }
    ASSERT_TRUE(IsIdLikeUuid(namespace_id));
  }

  // Since this is just for testing purposes, we do not bother generating a valid PgsqlTablegroupId
  ASSERT_OK(
      client_->CreateTablegroup(namespace_name, namespace_id, tablegroup_id, tablespace_id));

  // Now want to ensure that the newly created tablegroup shows up in the list.
  auto exists = ASSERT_RESULT(client_->TablegroupExists(namespace_name, tablegroup_id));
  ASSERT_TRUE(exists);
  parent_table_id = master::GetTablegroupParentTableId(tablegroup_id);

  {
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(WaitFor(
        [&]() -> bool {
          EXPECT_OK(client_->GetTabletsFromTableId(parent_table_id, 0, &tablets));
          return tablets.size() == 1;
        },
        MonoDelta::FromSeconds(30), "Create tablegroup tablet"));
    tablet_id = tablets[0].tablet_id();
  }

  string rocksdb_dir = JoinPathSegments(
      cluster_->data_root(), "ts-1", "yb-data", "tserver", "data", "rocksdb",
      "table-" + parent_table_id, "tablet-" + tablet_id);
  string wal_dir = JoinPathSegments(
      cluster_->data_root(), "ts-1", "yb-data", "tserver", "wals", "table-" + parent_table_id,
      "tablet-" + tablet_id);
  std::function<Result<bool>()> dirs_exist = [&] {
    return Env::Default()->FileExists(rocksdb_dir) && Env::Default()->FileExists(wal_dir);
  };

  ASSERT_OK(WaitFor(dirs_exist, MonoDelta::FromSeconds(30), "Create data and wal directories"));

  // Stop a tablet server and create a new tablet server. This will trigger a remote bootstrap on
  // the new tablet server.
  cluster_->tablet_server(2)->Shutdown();
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(4, MonoDelta::FromSeconds(20)));

  // Remote bootstrap should create the correct tablet directory for the new tablet server.
  rocksdb_dir = JoinPathSegments(
      cluster_->data_root(), "ts-4", "yb-data", "tserver", "data", "rocksdb",
      "table-" + parent_table_id, "tablet-" + tablet_id);
  wal_dir = JoinPathSegments(
      cluster_->data_root(), "ts-4", "yb-data", "tserver", "wals", "table-" + parent_table_id,
      "tablet-" + tablet_id);
  ASSERT_OK(WaitFor(dirs_exist, MonoDelta::FromSeconds(100), "Create data and wal directories"));
}

TEST_F(CreateTableITest, TestIsRaftLeaderMetric) {
  const int kNumReplicas = 3;
  const int kNumTablets = 1;
  const int kExpectedRaftLeaders = 1;
  vector<string> ts_flags;
  vector<string> master_flags;
  master_flags.push_back("--tablet_creation_timeout_ms=1000");
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags, kNumReplicas));
  ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name(),
                                                kTableName.namespace_type()));
  std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
  client::YBSchema client_schema(client::YBSchemaFromSchema(GetSimpleTestSchema()));

  // create a table
  ASSERT_OK(table_creator->table_name(kTableName)
                .schema(&client_schema)
                .num_tablets(kNumTablets)
                .Create());

  // Count the total Number of Raft Leaders in the cluster. Go through each tablet of every
  // tablet-server and sum up the leaders.
  int64_t kNumRaftLeaders = 0;
  for (size_t i = 0 ; i < kNumReplicas; i++) {
    auto tablet_ids = ASSERT_RESULT(cluster_->GetTabletIds(cluster_->tablet_server(i)));
    for(size_t ti = 0; ti < inspect_->ListTabletsOnTS(i).size(); ti++) {
      const char *tabletId = tablet_ids[ti].c_str();
      kNumRaftLeaders += ASSERT_RESULT(cluster_->tablet_server(i)->GetMetric<int64>(
          &METRIC_ENTITY_tablet, tabletId, &METRIC_is_raft_leader, "value"));
    }
  }
  ASSERT_EQ(kNumRaftLeaders, kExpectedRaftLeaders);
}

// In TSAN, currently, initdb isn't created during build but on first start.
// As a result transaction table gets created without waiting for the requisite
// number of TS.
TEST_F(CreateTableITest, YB_DISABLE_TEST_IN_TSAN(TestTransactionStatusTableCreation)) {
  // Set up an RF 1.
  // Tell the Master leader to wait for 3 TS to join before creating the
  // transaction status table.
  vector<string> master_flags = {
        "--txn_table_wait_min_ts_count=3"
  };
  // We also need to enable ysql.
  ASSERT_NO_FATALS(StartCluster({}, master_flags, 1, 1, true));

  // Check that the transaction table hasn't been created yet.
  YQLDatabase db = YQL_DATABASE_CQL;
  YBTableName transaction_status_table(db, master::kSystemNamespaceId,
                                  master::kSystemNamespaceName, kGlobalTransactionsTableName);
  bool exists = ASSERT_RESULT(client_->TableExists(transaction_status_table));
  ASSERT_FALSE(exists) << "Transaction table exists even though the "
                          "requirement for the minimum number of TS not met";

  // Add two tservers.
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->AddTabletServer());

  auto tbl_exists = [&]() -> Result<bool> {
    return client_->TableExists(transaction_status_table);
  };

  ASSERT_OK(WaitFor(tbl_exists, 30s * kTimeMultiplier,
                    "Transaction table doesn't exist even though the "
                    "requirement for the minimum number of TS met"));
}

TEST_F(CreateTableITest, TestCreateTableWithDefinedPartition) {
  const int kNumReplicas = 3;
  const int kNumTablets = 2;

  const int kNumPartitions = kNumTablets;

  vector<string> ts_flags;
  vector<string> master_flags;
  ts_flags.push_back("--never_fsync");  // run faster on slow disks
  master_flags.push_back("--enable_load_balancing=false");  // disable load balancing moves
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags, kNumReplicas));

  ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name(),
                                                kTableName.namespace_type()));
  std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
  client::YBSchema client_schema(client::YBSchemaFromSchema(GetSimpleTestSchema()));

  // Allocate the partitions.
  Partition partitions[kNumPartitions];
  const uint16_t interval = PartitionSchema::kMaxPartitionKey / (kNumPartitions + 1);

  partitions[0].set_partition_key_end(PartitionSchema::EncodeMultiColumnHashValue(interval));
  partitions[1].set_partition_key_start(PartitionSchema::EncodeMultiColumnHashValue(interval));

  // create a table
  ASSERT_OK(table_creator->table_name(kTableName)
                .schema(&client_schema)
                .num_tablets(kNumTablets)
                .add_partition(partitions[0])
                .add_partition(partitions[1])
                .Create());

  google::protobuf::RepeatedPtrField<yb::master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(
      kTableName, -1, &tablets, /* partition_list_version =*/ nullptr,
      RequireTabletsRunning::kFalse));
  for (int i = 0 ; i < kNumPartitions; ++i) {
    Partition p;
    Partition::FromPB(tablets[i].partition(), &p);
    ASSERT_TRUE(partitions[i].BoundsEqualToPartition(p));
  }
}

TEST_F(CreateTableITest, TestNumTabletsFlags) {
  // Start an RF 3.
  const int kNumReplicas = 3;
  const int kNumTablets = 6;
  const string kNamespaceName = "my_keyspace";
  const YQLDatabase kNamespaceType = YQL_DATABASE_CQL;
  const string kTableName1 = "test-table1";
  const string kTableName2 = "test-table2";
  const string kTableName3 = "test-table3";

  // Set the value of the flags.
  FLAGS_ycql_num_tablets = 1;
  FLAGS_yb_num_shards_per_tserver = 3;
  // Start an RF3.
  ASSERT_NO_FATALS(StartCluster({}, {}, kNumReplicas));

  // Create a namespace for all the tables.
  ASSERT_OK(client_->CreateNamespaceIfNotExists(kNamespaceName, kNamespaceType));
  // One common schema for all the tables.
  client::YBSchema client_schema(client::YBSchemaFromSchema(GetSimpleTestSchema()));

  // Test 1: Create a table with explicit tablet count.
  YBTableName table_name1(kNamespaceType, kNamespaceName, kTableName1);
  std::unique_ptr<client::YBTableCreator> table_creator1(client_->NewTableCreator());
  ASSERT_OK(table_creator1->table_name(table_name1)
                .schema(&client_schema)
                .num_tablets(kNumTablets)
                .wait(true)
                .Create());

  // Verify that number of tablets is 6 instead of 1.
  google::protobuf::RepeatedPtrField<yb::master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(
      table_name1, -1, &tablets, /* partition_list_version =*/ nullptr,
      RequireTabletsRunning::kFalse));
  ASSERT_EQ(tablets.size(), 6);

  // Test 2: Create another table without explicit number of tablets.
  YBTableName table_name2(kNamespaceType, kNamespaceName, kTableName2);
  std::unique_ptr<client::YBTableCreator> table_creator2(client_->NewTableCreator());
  ASSERT_OK(table_creator2->table_name(table_name2)
                .schema(&client_schema)
                .wait(true)
                .Create());

  // Verify that number of tablets is 1.
  tablets.Clear();
  ASSERT_OK(client_->GetTablets(
      table_name2, -1, &tablets, /* partition_list_version =*/ nullptr,
      RequireTabletsRunning::kFalse));
  ASSERT_EQ(tablets.size(), 1);

  // Reset the value of the flag.
  FLAGS_ycql_num_tablets = -1;

  // Test 3: Create a table without explicit tablet count.
  YBTableName table_name3(kNamespaceType, kNamespaceName, kTableName3);
  std::unique_ptr<client::YBTableCreator> table_creator3(client_->NewTableCreator());
  ASSERT_OK(table_creator3->table_name(table_name3)
                .schema(&client_schema)
                .wait(true)
                .Create());

  // Verify that number of tablets is 6 instead of 1.
  tablets.Clear();
  ASSERT_OK(client_->GetTablets(
      table_name3, -1, &tablets, /* partition_list_version =*/ nullptr,
      RequireTabletsRunning::kFalse));
  ASSERT_EQ(tablets.size(), 9);
}

TEST_F(CreateTableITest, OnlyMajorityReplicasWithoutPlacement) {
  const int kNumTablets = 6;
  const string kNamespaceName = "my_keyspace";
  const YQLDatabase kNamespaceType = YQL_DATABASE_CQL;
  const string kTableName = "test-table";
  const string kTableName2 = "test-table2";
  std::unordered_set<int> stopped_tservers;
  int num_tservers = 3;
  int num_alive_tservers = 0;

  // Start an RF3.
  vector<std::string> master_flags = {
    "--tserver_unresponsive_timeout_ms=5000"
  };
  ASSERT_NO_FATALS(StartCluster({}, master_flags, num_tservers));
  num_alive_tservers = 3;
  LOG(INFO) << "Started an RF3 cluster with 3 tservers and 1 master";

  // Stop a node.
  ASSERT_OK(cluster_->tablet_server(2)->Pause());
  LOG(INFO) << "Paused tserver index 2";

  // Wait for the master leader to mark it dead.
  ASSERT_OK(cluster_->WaitForMasterToMarkTSDead(2));
  stopped_tservers.emplace(2);
  --num_alive_tservers;
  LOG(INFO) << "TServer index 2 is now marked DEAD by the leader master";

  // Now issue a create table.
  // Create a namespace.
  ASSERT_OK(client_->CreateNamespaceIfNotExists(kNamespaceName, kNamespaceType));
  LOG(INFO) << "Created YQL Namespace " << kNamespaceName;

  client::YBSchema client_schema(client::YBSchemaFromSchema(GetSimpleTestSchema()));

  YBTableName table_name(kNamespaceType, kNamespaceName, kTableName);
  std::unique_ptr<client::YBTableCreator> table_creator1(client_->NewTableCreator());
  ASSERT_OK(table_creator1->table_name(table_name)
                            .schema(&client_schema)
                            .num_tablets(kNumTablets)
                            .wait(true)
                            .Create());
  LOG(INFO) << "Created table " << kNamespaceName << "." << kTableName;

  // Verify that each tserver contains kNumTablets with kNumTablets/2 leaders.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    for (int i = 0; i < num_tservers; i++) {
      if (stopped_tservers.count(i)) {
        continue;
      }
      if (!VERIFY_RESULT(VerifyTServerTablets(
          i, kNumTablets, kNumTablets / num_alive_tservers, kTableName, true))) {
        return false;
      }
    }
    return true;
  }, 120s * kTimeMultiplier, "Are tablets running", 1s));

  // Stop another node. Create table should now fail.
  ASSERT_OK(cluster_->tablet_server(1)->Pause());
  LOG(INFO) << "Paused tserver index 1";

  // Wait for the master leader to mark it dead.
  ASSERT_OK(cluster_->WaitForMasterToMarkTSDead(1));
  stopped_tservers.emplace(1);
  --num_alive_tservers;
  LOG(INFO) << "TServer index 1 is now marked DEAD by the leader master";

  YBTableName table_name2(kNamespaceType, kNamespaceName, kTableName2);
  std::unique_ptr<client::YBTableCreator> table_creator2(client_->NewTableCreator());
  ASSERT_NOK(table_creator2->table_name(table_name2)
                            .schema(&client_schema)
                            .num_tablets(kNumTablets)
                            .wait(true)
                            .timeout(10s * kTimeMultiplier)
                            .Create());

  // Now resume the paused tservers.
  ASSERT_OK(cluster_->tablet_server(2)->Resume());
  ASSERT_OK(cluster_->tablet_server(1)->Resume());
  stopped_tservers.erase(2);
  stopped_tservers.erase(1);
  ++num_alive_tservers;
  ++num_alive_tservers;
  LOG(INFO) << "Tablet Server 2 and 1 resumed";

  // Verify each tserver getting kNumTablets with leadership of kNumTablets/3.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    for (int i = 0; i < num_tservers; i++) {
      if (!VERIFY_RESULT(VerifyTServerTablets(
          i, kNumTablets, kNumTablets / num_alive_tservers, kTableName, true))) {
        return false;
      }
    }
    return true;
  }, 120s * kTimeMultiplier, "Are tablets running", 1s));
}

TEST_F(CreateTableITest, OnlyMajorityReplicasWithPlacement) {
  const int kNumTablets = 6;
  const string kNamespaceName = "my_keyspace";
  const YQLDatabase kNamespaceType = YQL_DATABASE_CQL;
  const string kTableName1 = "test-table1";
  const string kTableName2 = "test-table2";
  const string kTableName3 = "test-table3";
  const string kTableName4 = "test-table4";
  const string kTableName5 = "test-table5";
  std::unordered_set<int> stopped_tservers;
  int num_tservers = 3;
  int num_alive_tservers = 0;

  vector<std::string> master_flags = {
    "--tserver_unresponsive_timeout_ms=5000"
  };
  vector<std::string> tserver_flags = {
    "--placement_cloud=c",
    "--placement_region=r",
    "--placement_zone=z${index}"
  };

  // Test - 1.
  // Placement Policy: c.r.z1:1, c.r.z2:1, c.r.z3:1 with num_replicas as 3.
  // Available tservers: 1 in c.r.z1 and 1 in c.r.z2.
  // Result: Create Table should succeed.

  // Start an RF3 with tservers placed in "c.r.z0,c.r.z1,c.r.z2".
  ASSERT_NO_FATALS(StartCluster(tserver_flags, master_flags, 3));
  num_alive_tservers = 3;
  LOG(INFO) << "Started an RF3 cluster with 3 tservers in c.r.z0,c.r.z1,c.r.z2 and 1 master";

  // Modify placement info to contain at least one replica in each of the three zones.
  master::ReplicationInfoPB replication_info;
  auto* placement_info = replication_info.mutable_live_replicas();
  PreparePlacementInfo({ {"z0", 1}, {"z1", 1}, {"z2", 1} }, 3, placement_info);

  ASSERT_OK(client_->SetReplicationInfo(replication_info));
  LOG(INFO) << "Set replication info to c.r.z0,c.r.z1,c.r.z2 with num_replicas as 3";

  // Create a namespace.
  ASSERT_OK(client_->CreateNamespaceIfNotExists(kNamespaceName, kNamespaceType));
  LOG(INFO) << "Created YQL Namespace " << kNamespaceName;

  // Create a schema.
  client::YBSchema client_schema(client::YBSchemaFromSchema(GetSimpleTestSchema()));
  LOG(INFO) << "Created schema for tables";

  // Bring down one tserver in z2.
  ASSERT_OK(cluster_->tablet_server(2)->Pause());
  LOG(INFO) << "Paused tserver with index 2";

  // Wait for the master leader to mark them dead.
  ASSERT_OK(cluster_->WaitForMasterToMarkTSDead(2));
  stopped_tservers.emplace(2);
  --num_alive_tservers;
  LOG(INFO) << "Tserver index 2 is now marked DEAD by the leader master";

  // Issue a create table request, it should succeed.
  YBTableName table_name1(kNamespaceType, kNamespaceName, kTableName1);
  std::unique_ptr<client::YBTableCreator> table_creator1(client_->NewTableCreator());
  ASSERT_OK(table_creator1->table_name(table_name1)
                            .schema(&client_schema)
                            .num_tablets(kNumTablets)
                            .wait(true)
                            .Create());
  LOG(INFO) << "Created table " << kNamespaceName << "." << kTableName1;

  // Verify that each tserver contains kNumTablets with kNumTablets/2 leaders.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    for (int i = 0; i < num_tservers; i++) {
      if (stopped_tservers.count(i)) {
        continue;
      }
      if (!VERIFY_RESULT(VerifyTServerTablets(
          i, kNumTablets, kNumTablets / num_alive_tservers, kTableName1, true))) {
        return false;
      }
    }
    return true;
  }, 120s * kTimeMultiplier, "Are tablets running", 1s));

  // Test - 2.
  // Placement Policy: c.r.z1:1, c.r.z2:1, c.r.z3:1 with num_replicas as 3.
  // Available tservers: 1 in c.r.z1.
  // Result: CreateTable will fail because we don't have a raft quorum underneath.

  // Bring down another tserver, create table should now fail.
  ASSERT_OK(cluster_->tablet_server(1)->Pause());
  LOG(INFO) << "Paused tserver with index 1";

  // Wait for the master leader to mark them dead.
  ASSERT_OK(cluster_->WaitForMasterToMarkTSDead(1));
  stopped_tservers.emplace(1);
  --num_alive_tservers;
  LOG(INFO) << "Tserver index 1 is now marked DEAD by the leader master";

  YBTableName table_name2(kNamespaceType, kNamespaceName, kTableName2);
  std::unique_ptr<client::YBTableCreator> table_creator2(client_->NewTableCreator());
  ASSERT_NOK(table_creator2->table_name(table_name2)
                            .schema(&client_schema)
                            .num_tablets(kNumTablets)
                            .wait(true)
                            .timeout(10s * kTimeMultiplier)
                            .Create());

  // Test - 3.
  // Placement Policy: c.r.z1:1, c.r.z2:1, c.r.z3:1 with num_replicas as 3.
  // Available tservers: 2 in c.r.z1.
  // Result: Create Table will not succeed.

  // Add another tserver in c.r.z0. Create table should still fail after adding.
  AddTServerInZone("z0");
  ASSERT_OK(cluster_->WaitForTabletServerCount(++num_tservers, MonoDelta::FromSeconds(20)));
  ASSERT_OK(cluster_->WaitForMasterToMarkTSAlive(3));
  ++num_alive_tservers;

  YBTableName table_name3(kNamespaceType, kNamespaceName, kTableName3);
  std::unique_ptr<client::YBTableCreator> table_creator3(client_->NewTableCreator());
  ASSERT_NOK(table_creator3->table_name(table_name3)
                            .schema(&client_schema)
                            .num_tablets(kNumTablets)
                            .wait(true)
                            .timeout(10s * kTimeMultiplier)
                            .Create());

  // Test - 4.
  // Placement Policy: c.r.z1:1, c.r.z2:1, c.r.z3:1 with num_replicas as 5.
  // Available tservers: 2 in c.r.z1 and 1 in c.r.z2.
  // Result: Create Table should succeed.

  // Increase the number of replicas to 5 with the same placement config.
  master::ReplicationInfoPB replication_info2;
  auto* placement_info2 = replication_info2.mutable_live_replicas();
  PreparePlacementInfo({ {"z0", 1}, {"z1", 1}, {"z2", 1} }, 5, placement_info2);

  ASSERT_OK(client_->SetReplicationInfo(replication_info2));
  LOG(INFO) << "Set replication info to c.r.z0,c.r.z1,c.r.z2 with num_replicas as 5";

  // Now resume tserver 2 and wait for master to mark it alive.
  ASSERT_OK(cluster_->tablet_server(2)->Resume());
  ASSERT_OK(cluster_->WaitForMasterToMarkTSAlive(2));
  ++num_alive_tservers;
  LOG(INFO) << "Tablet Server index 2 resumed";

  // Create table should now succeed.
  YBTableName table_name4(kNamespaceType, kNamespaceName, kTableName4);
  std::unique_ptr<client::YBTableCreator> table_creator4(client_->NewTableCreator());
  ASSERT_OK(table_creator4->table_name(table_name4)
                            .schema(&client_schema)
                            .num_tablets(kNumTablets)
                            .wait(true)
                            .Create());
  // Validate data.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    for (int i = 0; i < num_tservers; i++) {
      if (stopped_tservers.count(i)) {
        continue;
      }
      if (!VERIFY_RESULT(VerifyTServerTablets(
          i, kNumTablets, kNumTablets / num_alive_tservers, kTableName4, true))) {
        return false;
      }
    }
    return true;
  }, 120s * kTimeMultiplier, "Are tablets running", 1s));

  // Test - 5.
  // Placement Policy: c.r.z1:1, c.r.z2:1, c.r.z3:1 with num_replicas as 5 as live replicas
  // and c.r.z4:1 with num_replicas as 1 as read_replica.
  // Available tservers: 2 in c.r.z1 and 1 in c.r.z2.
  // Result: Create Table should succeed despite having 0 read replica nodes.

  // Modify Placement info to contain a read replica also.
  master::ReplicationInfoPB replication_info3;
  auto* placement_info3 = replication_info3.mutable_live_replicas();
  PreparePlacementInfo({ {"z0", 1}, {"z1", 1}, {"z2", 1} }, 5, placement_info3);
  auto* read_placement_info = replication_info3.add_read_replicas();
  read_placement_info->set_placement_uuid("read-replica");
  PreparePlacementInfo({ {"z4", 1} }, 1, read_placement_info);
  ASSERT_OK(client_->SetReplicationInfo(replication_info3));
  LOG(INFO) << "Set replication info to " << replication_info3.ShortDebugString();

  // Try creating a table. It should succeed.
  YBTableName table_name5(kNamespaceType, kNamespaceName, kTableName5);
  std::unique_ptr<client::YBTableCreator> table_creator5(client_->NewTableCreator());
  ASSERT_OK(table_creator1->table_name(table_name5)
                            .schema(&client_schema)
                            .num_tablets(kNumTablets)
                            .wait(true)
                            .Create());

  // Resume tserver 1.
  ASSERT_OK(cluster_->tablet_server(1)->Resume());
  stopped_tservers.erase(1);
  ++num_alive_tservers;
  LOG(INFO) << "Tablet server index 1 resumed";

  // LB should move data to this fourth server also.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    for (int i = 0; i < num_tservers; i++) {
      if (stopped_tservers.count(i)) {
        continue;
      }
      if (!VERIFY_RESULT(VerifyTServerTablets(
          i, kNumTablets, kNumTablets / num_alive_tservers, kTableName4, false))) {
        return false;
      }
    }
    return true;
  }, 120s * kTimeMultiplier, "Are tablets running", 1s));
}

}  // namespace yb
