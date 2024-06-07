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

#include <algorithm>
#include <memory>
#include <vector>

#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/stl_util.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/sys_catalog-test_base.h"
#include "yb/master/sys_catalog.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/status.h"

using namespace std::literals;

using std::make_shared;
using std::string;
using std::shared_ptr;
using std::unique_ptr;
using std::vector;

DECLARE_string(cluster_uuid);

namespace yb {
namespace master {

TEST_F(SysCatalogTest, TestPrepareDefaultClusterConfig) {
  // Verify that a cluster cannot be created with an invalid uuid.
  ExternalMiniClusterOptions opts;
  opts.num_masters = 1;
  opts.num_tablet_servers = 1;
  opts.extra_master_flags.push_back("--FLAGS_cluster_uuid=invalid_uuid");
  auto external_mini_cluster = std::make_unique<ExternalMiniCluster>(opts);
  Status s = external_mini_cluster->Start();
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.message().ToBuffer(), "Unable to start Master");

  auto dir = GetTestPath("Master") + "valid_cluster_uuid_test";
  ASSERT_OK(Env::Default()->CreateDir(dir));
  std::unique_ptr<MiniMaster> mini_master =
      std::make_unique<MiniMaster>(Env::Default(), dir, AllocateFreePort(), AllocateFreePort(), 0);


  // Test that config.cluster_uuid gets set to the value that we specify through flag cluster_uuid.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cluster_uuid) = Uuid::Generate().ToString();
  ASSERT_OK(mini_master->Start());
  auto master = mini_master->master();
  ASSERT_OK(master->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  SysClusterConfigEntryPB config =
      ASSERT_RESULT(master->catalog_manager()->GetClusterConfig());

  // Verify that the cluster uuid was set in the config.
  ASSERT_EQ(FLAGS_cluster_uuid, config.cluster_uuid());

  mini_master->Shutdown();

  // Test that config.cluster_uuid gets set to a valid uuid when cluster_uuid flag is empty.
  dir = GetTestPath("Master") + "empty_cluster_uuid_test";
  ASSERT_OK(Env::Default()->CreateDir(dir));
  mini_master =
      std::make_unique<MiniMaster>(Env::Default(), dir, AllocateFreePort(), AllocateFreePort(), 0);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cluster_uuid) = "";
  ASSERT_OK(mini_master->Start());
  master = mini_master->master();
  ASSERT_OK(master->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  config = ASSERT_RESULT(master->catalog_manager()->GetClusterConfig());

  // Check that the cluster_uuid was set.
  ASSERT_FALSE(config.cluster_uuid().empty());

  // Check that the cluster uuid is valid.
  ASSERT_OK(Uuid::FromString(config.cluster_uuid()));

  mini_master->Shutdown();
}

// Test the sys-catalog tables basic operations (add, update, delete,
// visit)
TEST_F(SysCatalogTest, TestSysCatalogTablesOperations) {
  unique_ptr<TestTableLoader> loader(new TestTableLoader());
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_EQ(kNumSystemTables, loader->tables.size());

  // Create new table.
  const std::string table_id = "abc";
  scoped_refptr<TableInfo> table = CreateUncommittedTable(table_id);
  // Add the table
  auto epoch = LeaderEpoch(kLeaderTerm, sys_catalog_->pitr_count());
  ASSERT_OK(sys_catalog_->Upsert(epoch, table));
  table->mutable_metadata()->CommitMutation();

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));

  ASSERT_EQ(1 + kNumSystemTables, loader->tables.size());
  ASSERT_METADATA_EQ(table.get(), loader->tables[table_id]);

  // Update the table
  {
    auto l = table->LockForWrite();
    l.mutable_data()->pb.set_version(1);
    l.mutable_data()->pb.set_state(SysTablesEntryPB::DELETING);
    ASSERT_OK(sys_catalog_->Upsert(epoch, table));
    l.Commit();
  }

  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_EQ(1 + kNumSystemTables, loader->tables.size());
  ASSERT_METADATA_EQ(table.get(), loader->tables[table_id]);

  // Delete the table
  loader->Reset();
  ASSERT_OK(sys_catalog_->Delete(epoch, table));
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_EQ(kNumSystemTables, loader->tables.size());
}

// Test the sys-catalog tablets basic operations (add, update, delete, visit)
TEST_F(SysCatalogTest, TestSysCatalogTabletsOperations) {
  scoped_refptr<TableInfo> table(
      master_->catalog_manager()->NewTableInfo("abc", /* colocated */ false));
  // This leaves all three in StartMutation.
  scoped_refptr<TabletInfo> tablet1(CreateUncommittedTablet(table.get(), "123", "a", "b"));
  scoped_refptr<TabletInfo> tablet2(CreateUncommittedTablet(table.get(), "456", "b", "c"));
  scoped_refptr<TabletInfo> tablet3(CreateUncommittedTablet(table.get(), "789", "c", "d"));

  unique_ptr<TestTabletLoader> loader(new TestTabletLoader());
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_EQ(kNumSystemTables, loader->tablets.size());
  auto epoch = LeaderEpoch(kLeaderTerm, sys_catalog_->pitr_count());

  // Add tablet1 and tablet2
  {
    std::vector<TabletInfo*> tablets;
    tablets.push_back(tablet1.get());
    tablets.push_back(tablet2.get());

    loader->Reset();
    ASSERT_OK(sys_catalog_->Upsert(epoch, tablets));
    tablet1->mutable_metadata()->CommitMutation();
    tablet2->mutable_metadata()->CommitMutation();

    ASSERT_OK(sys_catalog_->Visit(loader.get()));
    ASSERT_EQ(2 + kNumSystemTables, loader->tablets.size());
    ASSERT_METADATA_EQ(tablet1.get(), loader->tablets[tablet1->id()]);
    ASSERT_METADATA_EQ(tablet2.get(), loader->tablets[tablet2->id()]);
  }

  // Update tablet1
  {
    std::vector<TabletInfo*> tablets;
    tablets.push_back(tablet1.get());

    auto l1 = tablet1->LockForWrite();
    l1.mutable_data()->pb.set_state(SysTabletsEntryPB::RUNNING);
    ASSERT_OK(sys_catalog_->Upsert(epoch, tablets));
    l1.Commit();

    loader->Reset();
    ASSERT_OK(sys_catalog_->Visit(loader.get()));
    ASSERT_EQ(2 + kNumSystemTables, loader->tablets.size());
    ASSERT_METADATA_EQ(tablet1.get(), loader->tablets[tablet1->id()]);
    ASSERT_METADATA_EQ(tablet2.get(), loader->tablets[tablet2->id()]);
  }

  // Add tablet3 and Update tablet1 and tablet2
  {
    std::vector<TabletInfo *> to_add;
    std::vector<TabletInfo *> to_update;

    to_add.push_back(tablet3.get());
    to_update.push_back(tablet1.get());
    to_update.push_back(tablet2.get());

    auto l1 = tablet1->LockForWrite();
    l1.mutable_data()->pb.set_state(SysTabletsEntryPB::REPLACED);
    auto l2 = tablet2->LockForWrite();
    l2.mutable_data()->pb.set_state(SysTabletsEntryPB::RUNNING);

    loader->Reset();
    ASSERT_OK(sys_catalog_->Upsert(epoch, to_add, to_update));

    l1.Commit();
    l2.Commit();
    // This was still open from the initial create!
    tablet3->mutable_metadata()->CommitMutation();

    ASSERT_OK(sys_catalog_->Visit(loader.get()));
    ASSERT_EQ(3 + kNumSystemTables, loader->tablets.size());
    ASSERT_METADATA_EQ(tablet1.get(), loader->tablets[tablet1->id()]);
    ASSERT_METADATA_EQ(tablet2.get(), loader->tablets[tablet2->id()]);
    ASSERT_METADATA_EQ(tablet3.get(), loader->tablets[tablet3->id()]);
  }

  // Delete tablet1 and tablet3 tablets
  {
    std::vector<TabletInfo*> tablets;
    tablets.push_back(tablet1.get());
    tablets.push_back(tablet3.get());

    loader->Reset();
    ASSERT_OK(sys_catalog_->Delete(epoch, tablets));
    ASSERT_OK(sys_catalog_->Visit(loader.get()));
    ASSERT_EQ(1 + kNumSystemTables, loader->tablets.size());
    ASSERT_METADATA_EQ(tablet2.get(), loader->tablets[tablet2->id()]);
  }
}

// Test the sys-catalog tables basic operations (add, update, delete, visit)
TEST_F(SysCatalogTest, TestSysCatalogPlacementOperations) {
  unique_ptr<TestClusterConfigLoader> loader(new TestClusterConfigLoader());
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_TRUE(loader->config_info);
  {
    auto l = loader->config_info->LockForRead();
    ASSERT_EQ(l->pb.version(), 0);
    ASSERT_EQ(l->pb.replication_info().live_replicas().placement_blocks_size(), 0);
  }

  // Test modifications directly through the Sys catalog API.

  // Create a config_info block.
  std::shared_ptr<ClusterConfigInfo> config_info(make_shared<ClusterConfigInfo>());
  {
    auto l = config_info->LockForWrite();
    auto* live_replicas = l.mutable_data()->pb.mutable_replication_info()->mutable_live_replicas();
    auto pb = live_replicas->add_placement_blocks();
    auto cloud_info = pb->mutable_cloud_info();
    cloud_info->set_placement_cloud("cloud");
    cloud_info->set_placement_region("region");
    cloud_info->set_placement_zone("zone");
    pb->set_min_num_replicas(100);
    live_replicas->set_num_replicas(100);

    // Set it in the sys_catalog_. It already has the default entry, so we use update.
    ASSERT_OK(sys_catalog_->Upsert(kLeaderTerm, config_info.get()));
    l.Commit();
  }

  // Check data from sys_catalog_.
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_TRUE(loader->config_info);
  ASSERT_METADATA_EQ(config_info.get(), loader->config_info.get());

  {
    auto l = config_info->LockForWrite();
    auto* live_replicas = l.mutable_data()->pb.mutable_replication_info()->mutable_live_replicas();
    auto pb = live_replicas->mutable_placement_blocks(0);
    auto cloud_info = pb->mutable_cloud_info();
    // Update some config_info info.
    cloud_info->set_placement_cloud("cloud2");
    pb->set_min_num_replicas(200);
    live_replicas->set_num_replicas(100);
    // Update it in the sys_catalog_.
    ASSERT_OK(sys_catalog_->Upsert(kLeaderTerm, config_info.get()));
    l.Commit();
  }

  // Check data from sys_catalog_.
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_TRUE(loader->config_info);
  ASSERT_METADATA_EQ(config_info.get(), loader->config_info.get());

  // Test data through the CatalogManager API.

  // Get the config from the CatalogManager and test it is the default, as we didn't use the
  // CatalogManager to update it.
  SysClusterConfigEntryPB config =
      ASSERT_RESULT(master_->catalog_manager()->GetClusterConfig());
  ASSERT_EQ(config.version(), 0);
  ASSERT_EQ(config.replication_info().live_replicas().placement_blocks_size(), 0);

  // Update a field in the previously used in memory state and set through proper API.
  {
    auto l = config_info->LockForWrite();
    auto* live_replicas = l.mutable_data()->pb.mutable_replication_info()->mutable_live_replicas();
    auto pb = live_replicas->mutable_placement_blocks(0);
    pb->set_min_num_replicas(300);
    live_replicas->set_num_replicas(300);

    ChangeMasterClusterConfigRequestPB req;
    *req.mutable_cluster_config() = l.mutable_data()->pb;
    ChangeMasterClusterConfigResponsePB resp;

    // Verify that we receive an error when trying to change the cluster uuid.
    req.mutable_cluster_config()->set_cluster_uuid("some-cluster-uuid");
    auto status = master_->catalog_manager()->SetClusterConfig(&req, &resp);
    ASSERT_TRUE(status.IsInvalidArgument());
    req.mutable_cluster_config()->set_cluster_uuid(config.cluster_uuid());

    // Verify that we receive an error when trying to change the universe uuid.
    req.mutable_cluster_config()->set_universe_uuid("some-universe-uuid");
    status = master_->catalog_manager()->SetClusterConfig(&req, &resp);
    ASSERT_TRUE(status.IsInvalidArgument());
    req.mutable_cluster_config()->set_universe_uuid(config.universe_uuid());

    // Setting the cluster and universe uuid correctly should make the request succeed.
    ASSERT_OK(master_->catalog_manager()->SetClusterConfig(&req, &resp));
    l.Commit();
  }

  // Confirm the in memory state does not match the config we get from the CatalogManager API, due
  // to version mismatch.
  config = ASSERT_RESULT(master_->catalog_manager()->GetClusterConfig());
  {
    auto l = config_info->LockForRead();
    ASSERT_FALSE(PbEquals(l->pb, config));
    ASSERT_EQ(l->pb.version(), 0);
    ASSERT_EQ(config.version(), 1);
    ASSERT_TRUE(PbEquals(
        l->pb.replication_info().live_replicas().placement_blocks(0),
        config.replication_info().live_replicas().placement_blocks(0)));
  }

  // Reload the data again and check that it matches expectations.
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_TRUE(loader->config_info);
  {
    auto l = loader->config_info->LockForRead();
    ASSERT_TRUE(PbEquals(l->pb, config));
    ASSERT_TRUE(l->pb.has_version());
    ASSERT_EQ(l->pb.version(), 1);
    ASSERT_EQ(l->pb.replication_info().live_replicas().placement_blocks_size(), 1);
  }
}

// Test the sys-catalog namespaces basic operations (add, update, delete, visit)
TEST_F(SysCatalogTest, TestSysCatalogNamespacesOperations) {
  // 1. CHECK SYSTEM NAMESPACE COUNT
  unique_ptr<TestNamespaceLoader> loader(new TestNamespaceLoader());
  ASSERT_OK(sys_catalog_->Visit(loader.get()));

  ASSERT_EQ(kNumSystemNamespaces, loader->namespaces.size());

  // 2. CHECK ADD_NAMESPACE
  // Create new namespace.
  scoped_refptr<NamespaceInfo> ns(
      new NamespaceInfo("deadbeafdeadbeafdeadbeafdeadbeaf", /*tasks_tracker=*/nullptr));
  {
    auto l = ns->LockForWrite();
    l.mutable_data()->pb.set_name("test_ns");
    // Add the namespace
    ASSERT_OK(sys_catalog_->Upsert(kLeaderTerm, ns));

    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_EQ(1 + kNumSystemNamespaces, loader->namespaces.size());
  ASSERT_METADATA_EQ(ns.get(), loader->namespaces[loader->namespaces.size() - 1]);

  // 3. CHECK UPDATE_NAMESPACE
  // Update the namespace
  {
    auto l = ns->LockForWrite();
    l.mutable_data()->pb.set_name("test_ns_new_name");
    ASSERT_OK(sys_catalog_->Upsert(kLeaderTerm, ns));
    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_EQ(1 + kNumSystemNamespaces, loader->namespaces.size());
  ASSERT_METADATA_EQ(ns.get(), loader->namespaces[loader->namespaces.size() - 1]);

  // 4. CHECK DELETE_NAMESPACE
  // Delete the namespace
  ASSERT_OK(sys_catalog_->Delete(kLeaderTerm, ns));

  // Verify the result.
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_EQ(kNumSystemNamespaces, loader->namespaces.size());
}

// Test the sys-catalog redis config basic operations (add, visit, drop)
TEST_F(SysCatalogTest, TestSysCatalogRedisConfigOperations) {
  unique_ptr<TestRedisConfigLoader> loader(new TestRedisConfigLoader());
  ASSERT_OK(sys_catalog_->Visit(loader.get()));

  // Set redis config information
  SysRedisConfigEntryPB config_entry;
  config_entry.set_key("key1");
  config_entry.add_args("value1.1");
  config_entry.add_args("value1.2");

  // 1. CHECK Add entry
  scoped_refptr<RedisConfigInfo> rci = new RedisConfigInfo("key1");
  {
    auto l = rci->LockForWrite();
    l.mutable_data()->pb = std::move(config_entry);
    // Add the redis config
    ASSERT_OK(sys_catalog_->Upsert(kLeaderTerm, rci));
    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  // The default config is empty
  ASSERT_EQ(1, loader->config_entries.size());
  ASSERT_METADATA_EQ(rci.get(), loader->config_entries[0]);

  // Update the same config.
  SysRedisConfigEntryPB* metadata;
  rci->mutable_metadata()->StartMutation();
  metadata = &rci->mutable_metadata()->mutable_dirty()->pb;

  metadata->clear_args();
  metadata->add_args("value1b");

  ASSERT_OK(sys_catalog_->Upsert(kLeaderTerm, rci));
  rci->mutable_metadata()->CommitMutation();

  // Verify config entries.
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  //  The default config is empty.
  ASSERT_EQ(1, loader->config_entries.size());
  ASSERT_METADATA_EQ(rci.get(), loader->config_entries[0]);

  // Add another key
  {
    // Set redis config information
    SysRedisConfigEntryPB config_entry;
    config_entry.set_key("key2");
    config_entry.add_args("value2.1");
    config_entry.add_args("value2.2");

    // 1. CHECK Add entry
    scoped_refptr<RedisConfigInfo> rci2 = new RedisConfigInfo("key2");
    {
      auto l = rci2->LockForWrite();
      l.mutable_data()->pb = std::move(config_entry);
      // Add the redis config
      ASSERT_OK(sys_catalog_->Upsert(kLeaderTerm, rci2));
      l.Commit();
    }

    // Verify it showed up.
    loader->Reset();
    ASSERT_OK(sys_catalog_->Visit(loader.get()));
    // The default config is empty
    ASSERT_EQ(2, loader->config_entries.size());
    ASSERT_METADATA_EQ(rci2.get(), loader->config_entries[1]);

    // 2. CHECK DELETE RedisConfig
    ASSERT_OK(sys_catalog_->Delete(kLeaderTerm, rci2));

    // Verify the result.
    loader->Reset();
    ASSERT_OK(sys_catalog_->Visit(loader.get()));
    ASSERT_EQ(1, loader->config_entries.size());
  }
  // 2. CHECK DELETE RedisConfig
  ASSERT_OK(sys_catalog_->Delete(kLeaderTerm, rci));

  // Verify the result.
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_EQ(0, loader->config_entries.size());
}

// Test the sys-catalog sys-config basic operations (add, visit, drop).
TEST_F(SysCatalogTest, TestSysCatalogSysConfigOperations) {
  // 1. Verify that when master initializes:
  //   a. "security-config" entry is set up with roles_version = 0.
  //   b. "ysql-catalog-configuration" entry is set up with version = 0 and the transactional YSQL
  //      sys catalog flag is set to true.
  //   c. "transaction-tables-config" entry is set up with version = 0.
  scoped_refptr<SysConfigInfo> security_config = new SysConfigInfo(kSecurityConfigType);
  {
    auto l = security_config->LockForWrite();
    l.mutable_data()->pb.mutable_security_config()->set_roles_version(0);
    l.mutable_data()->pb.mutable_security_config()->set_cassandra_user_created(true);
    l.Commit();
  }
  scoped_refptr<SysConfigInfo> ysql_catalog_config = new SysConfigInfo(kYsqlCatalogConfigType);
  {
    auto l = ysql_catalog_config->LockForWrite();
    auto& ysql_catalog_config_pb = *l.mutable_data()->pb.mutable_ysql_catalog_config();
    ysql_catalog_config_pb.set_version(0);
    ysql_catalog_config_pb.set_transactional_sys_catalog_enabled(true);
    l.Commit();
  }
  scoped_refptr<SysConfigInfo> transaction_tables_config =
      new SysConfigInfo(kTransactionTablesConfigType);
  {
    auto l = transaction_tables_config->LockForWrite();
    auto& transaction_tables_config_pb = *l.mutable_data()->pb.mutable_transaction_tables_config();
    transaction_tables_config_pb.set_version(0);
    l.Commit();
  }
  unique_ptr<TestSysConfigLoader> loader(new TestSysConfigLoader());
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_EQ(3, loader->sys_configs.size());
  ASSERT_METADATA_EQ(security_config.get(), loader->sys_configs[0]);
  ASSERT_METADATA_EQ(transaction_tables_config.get(), loader->sys_configs[1]);
  ASSERT_METADATA_EQ(ysql_catalog_config.get(), loader->sys_configs[2]);

  // 2. Add a new SysConfigEntryPB and verify it shows up.
  scoped_refptr<SysConfigInfo> test_config = new SysConfigInfo("test-security-configuration");
  {
    auto l = test_config->LockForWrite();
    l.mutable_data()->pb.mutable_security_config()->set_roles_version(1234);

    // Add the test_config.
    ASSERT_OK(sys_catalog_->Upsert(kLeaderTerm, test_config));
    l.Commit();
  }
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_EQ(4, loader->sys_configs.size());
  ASSERT_METADATA_EQ(security_config.get(), loader->sys_configs[0]);
  ASSERT_METADATA_EQ(test_config.get(), loader->sys_configs[1]);
  ASSERT_METADATA_EQ(transaction_tables_config.get(), loader->sys_configs[2]);
  ASSERT_METADATA_EQ(ysql_catalog_config.get(), loader->sys_configs[3]);

  // 2. Remove the SysConfigEntry and verify that it got removed.
  ASSERT_OK(sys_catalog_->Delete(kLeaderTerm, test_config));
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_EQ(3, loader->sys_configs.size());
  ASSERT_METADATA_EQ(security_config.get(), loader->sys_configs[0]);
  ASSERT_METADATA_EQ(transaction_tables_config.get(), loader->sys_configs[1]);
  ASSERT_METADATA_EQ(ysql_catalog_config.get(), loader->sys_configs[2]);
}

// Test the sys-catalog role/permissions basic operations (add, visit, drop)
TEST_F(SysCatalogTest, TestSysCatalogRoleOperations) {
  unique_ptr<TestRoleLoader> loader(new TestRoleLoader());
  ASSERT_OK(sys_catalog_->Visit(loader.get()));

  // Set role information
  SysRoleEntryPB role_entry;
  role_entry.set_role("test_role");
  role_entry.set_can_login(false);
  role_entry.set_is_superuser(true);
  role_entry.set_salted_hash("test_password");

  // 1. CHECK ADD_ROLE
  scoped_refptr<RoleInfo> rl = new RoleInfo("test_role");
  {
    auto l = rl->LockForWrite();
    l.mutable_data()->pb = std::move(role_entry);
    // Add the role
    ASSERT_OK(sys_catalog_->Upsert(kLeaderTerm, rl));
    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  // The first role is the default cassandra role
  ASSERT_EQ(2, loader->roles.size());
  ASSERT_METADATA_EQ(rl.get(), loader->roles[1]);

  // Adding permissions
  SysRoleEntryPB* metadata;
  rl->mutable_metadata()->StartMutation();
  metadata = &rl->mutable_metadata()->mutable_dirty()->pb;
  // Set permission information;
  ResourcePermissionsPB* currentResource;
  currentResource = metadata->add_resources();
  currentResource->set_canonical_resource("data/keyspace1/table1");
  currentResource->set_resource_name("table1");
  currentResource->set_namespace_name("keyspace1");
  currentResource->set_resource_type(ResourceType::TABLE);

  currentResource->add_permissions(PermissionType::SELECT_PERMISSION);
  currentResource->add_permissions(PermissionType::MODIFY_PERMISSION);
  currentResource->add_permissions(PermissionType::AUTHORIZE_PERMISSION);

  currentResource = metadata->add_resources();
  currentResource->set_canonical_resource("roles/test_role");
  currentResource->set_resource_type(ResourceType::ROLE);

  currentResource->add_permissions(PermissionType::DROP_PERMISSION);

  ASSERT_OK(sys_catalog_->Upsert(kLeaderTerm, rl));
  rl->mutable_metadata()->CommitMutation();

  // Verify permissions
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  // The first role is the default cassandra role
  ASSERT_EQ(2, loader->roles.size());
  ASSERT_METADATA_EQ(rl.get(), loader->roles[1]);

  // 2. CHECK DELETE Role
  ASSERT_OK(sys_catalog_->Delete(kLeaderTerm, rl));

  // Verify the result.
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_EQ(1, loader->roles.size());
}


// Test the sys-catalog udtype basic operations (add, delete, visit)
TEST_F(SysCatalogTest, TestSysCatalogUDTypeOperations) {
  unique_ptr<TestUDTypeLoader> loader(new TestUDTypeLoader());
  ASSERT_OK(sys_catalog_->Visit(loader.get()));

  // 1. CHECK ADD_UDTYPE
  scoped_refptr<UDTypeInfo> tp(new UDTypeInfo("deadbeafdeadbeafdeadbeafdeadbeaf"));
  {
    auto l = tp->LockForWrite();
    l.mutable_data()->pb.set_name("test_tp");
    l.mutable_data()->pb.set_namespace_id(kSystemNamespaceId);
    // Add the udtype
    ASSERT_OK(sys_catalog_->Upsert(kLeaderTerm, tp));
    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_EQ(1, loader->udtypes.size());
  ASSERT_METADATA_EQ(tp.get(), loader->udtypes[0]);

  // 2. CHECK DELETE_UDTYPE
  ASSERT_OK(sys_catalog_->Delete(kLeaderTerm, tp));

  // Verify the result.
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_EQ(0, loader->udtypes.size());
}

// If a table crashed in the DELETING state, its tablets should be migrated to the DELETED state.
TEST_F(SysCatalogTest, TestOrphanedTabletsDeleted) {
  auto table = CreateUncommittedTable(/* table_id */ "123", SysTablesEntryPB::DELETING);
  const std::unordered_map<SysTabletsEntryPB_State, TabletId> state_to_id = {
    {SysTabletsEntryPB::UNKNOWN, "tablet_unknown"},
    {SysTabletsEntryPB::PREPARING, "tablet_preparing"},
    {SysTabletsEntryPB::CREATING, "tablet_creating"},
    {SysTabletsEntryPB::RUNNING, "tablet_running"},
  };

  vector<TabletInfoPtr> tablets;
  for (auto& p : state_to_id) {
    auto tablet = CreateUncommittedTablet(table.get(), p.second /* tablet_id */);
    tablet->mutable_metadata()->mutable_dirty()->pb.set_state(p.first);
    tablets.push_back(tablet);
  }
  auto epoch = LeaderEpoch(kLeaderTerm, sys_catalog_->pitr_count());
  ASSERT_OK(sys_catalog_->Upsert(epoch, table, tablets));

  // Restart the cluster and wait for the background task to set the tablet's in-memory and
  // persisted states to DELETED.
  ASSERT_OK(RestartMaster());
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    TestTabletLoader loader;
    RETURN_NOT_OK(sys_catalog_->Visit(&loader));
    for (auto& p : state_to_id) {
      auto in_mem_tablet =
          VERIFY_RESULT(master_->catalog_manager()->GetTabletInfo(p.second /* tablet_id */));
      auto* persisted_tablet = loader.tablets[p.second /* tablet_id */];

      if ((in_mem_tablet->LockForRead()->pb.state() != SysTabletsEntryPB_State_DELETED) ||
          (persisted_tablet->LockForRead()->pb.state() != SysTabletsEntryPB_State_DELETED)) {
        return false;
      }
    }
    return true;
  }, 10s * kTimeMultiplier, "Wait for tablet to be deleted in memory and on disk."));
}

// If a tablet's table_ids does not contain the primary table, it should be migrated.
TEST_F(SysCatalogTest, TestTableIdsHasAtLeastOneTable) {
  TestTabletLoader loader;
  const string table_id = "123";
  const string tablet_id = "456";

  auto table = CreateUncommittedTable(table_id, SysTablesEntryPB::RUNNING);
  auto tablet = CreateUncommittedTablet(table.get(), tablet_id);
  tablet->mutable_metadata()->mutable_dirty()->pb.set_state(SysTabletsEntryPB::RUNNING);
  tablet->mutable_metadata()->mutable_dirty()->pb.clear_table_ids();
  auto epoch = LeaderEpoch(kLeaderTerm, sys_catalog_->pitr_count());
  ASSERT_OK(sys_catalog_->Upsert(epoch, table, tablet));

  // Restart the cluster and wait for the background task to update the tablet's in-memory and
  // persisted table_ids field to be non-empty.
  ASSERT_OK(RestartMaster());
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    TestTabletLoader loader;
    RETURN_NOT_OK(sys_catalog_->Visit(&loader));
    auto in_mem_tablet = VERIFY_RESULT(master_->catalog_manager()->GetTabletInfo(tablet_id));
    auto* persisted_tablet = loader.tablets[tablet_id];

    if ((in_mem_tablet->LockForRead()->pb.table_ids_size() != 1) ||
        (in_mem_tablet->LockForRead()->pb.table_ids(0) != table_id) ||
        (persisted_tablet->LockForRead()->pb.table_ids_size() != 1) ||
        (persisted_tablet->LockForRead()->pb.table_ids(0) != table_id)) {
      return false;
    }
    return true;
  }, 10s * kTimeMultiplier, "Wait for table_ids field to be updated in memory and on disk."));
}

// Test the tasks tracker in catalog manager.
TEST_F(SysCatalogTest, TestCatalogManagerTasksTracker) {
  // Configure number of tasks flag and keep time flag.
  SetAtomicFlag(100, &FLAGS_tasks_tracker_num_tasks);
  SetAtomicFlag(100, &FLAGS_tasks_tracker_keep_time_multiplier);

  unique_ptr<TestTableLoader> loader(new TestTableLoader());
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_EQ(kNumSystemTables, loader->tables.size());

  // Create new table.
  const std::string table_id = "abc";
  scoped_refptr<TableInfo> table = CreateUncommittedTable(table_id);
  // Add the table.
  auto epoch = LeaderEpoch(kLeaderTerm, sys_catalog_->pitr_count());
  ASSERT_OK(sys_catalog_->Upsert(epoch, table));
  table->mutable_metadata()->CommitMutation();

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog_->Visit(loader.get()));

  ASSERT_EQ(1 + kNumSystemTables, loader->tables.size());
  ASSERT_METADATA_EQ(table.get(), loader->tables[table_id]);

  // Add tasks to the table (more than can fit in the cbuf).
  for (int task_id = 0; task_id < FLAGS_tasks_tracker_num_tasks + 10; ++task_id) {
    scoped_refptr<TabletInfo> tablet(new TabletInfo(table, kSysCatalogTableId));
    auto task = std::make_shared<AsyncTruncate>(
        master_, master_->catalog_manager()->AsyncTaskPool(), tablet, epoch);
    table->AddTask(task);
  }

  // Verify initial cbuf size is correct.
  ASSERT_EQ(master_->catalog_manager()->GetRecentTasks().size(), FLAGS_tasks_tracker_num_tasks);

  // Wait for background task to run (length of two wait intervals).
  usleep(2 * (1000 * FLAGS_catalog_manager_bg_task_wait_ms));

  // Verify that tasks were not cleaned up.
  ASSERT_EQ(master_->catalog_manager()->GetRecentTasks().size(), FLAGS_tasks_tracker_num_tasks);

  // Set keep time flag to small multiple of the wait interval.
  SetAtomicFlag(1, &FLAGS_tasks_tracker_keep_time_multiplier);

  // Wait for background task to run (length of two wait intervals).
  usleep(2 * (1000 * FLAGS_catalog_manager_bg_task_wait_ms));

  // Verify that tasks were cleaned up.
  ASSERT_EQ(master_->catalog_manager()->GetRecentTasks().size(), 0);

  // Cleanup tasks.
  table->AbortTasksAndClose();
}

// Test migration of the TableInfo namespace_name field.
TEST_F(SysCatalogTest, TestNamespaceNameMigration) {
  // First create a new namespace to add our table to.
  unique_ptr<TestNamespaceLoader> ns_loader(new TestNamespaceLoader());
  ASSERT_OK(sys_catalog_->Visit(ns_loader.get()));
  ASSERT_EQ(kNumSystemNamespaces, ns_loader->namespaces.size());

  auto epoch = LeaderEpoch(kLeaderTerm, sys_catalog_->pitr_count());
  scoped_refptr<NamespaceInfo> ns(
      new NamespaceInfo("deadbeafdeadbeafdeadbeafdeadbeaf", /*tasks_tracker=*/nullptr));
  {
    auto l = ns->LockForWrite();
    l.mutable_data()->pb.set_name("test_ns");
    ASSERT_OK(sys_catalog_->Upsert(epoch, ns));
    l.Commit();
  }

  // Now create a new table and add it to that namespace.
  unique_ptr<TestTableLoader> loader(new TestTableLoader());
  ASSERT_OK(sys_catalog_->Visit(loader.get()));
  ASSERT_EQ(kNumSystemTables, loader->tables.size());
  const std::string table_id = "testtableid";
  scoped_refptr<TableInfo> table = CreateUncommittedTable(table_id);

  // Only set the namespace id and clear the namespace name for this table.
  {
    auto* metadata = &table->mutable_metadata()->mutable_dirty()->pb;
    metadata->clear_namespace_name();
    metadata->set_namespace_id(ns->id());
  }

  // Add the table.
  ASSERT_OK(sys_catalog_->Upsert(epoch, table));
  table->mutable_metadata()->CommitMutation();

  // Restart the cluster and wait for the background task to update the persistent state of the
  // table's namespace_name.
  ASSERT_OK(RestartMaster());
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        loader->Reset();
        RETURN_NOT_OK(sys_catalog_->Visit(loader.get()));
        auto in_mem_ns_name = master_->catalog_manager()->GetTableInfo(table_id)->namespace_name();
        auto persisted_ns_name = loader->tables[table_id]->LockForRead()->namespace_name();
        return !in_mem_ns_name.empty() && in_mem_ns_name == ns->name() &&
               !persisted_ns_name.empty() && persisted_ns_name == ns->name();
      },
      10s * kTimeMultiplier, "Wait for table's namespace_name to be set in memory and on disk."));
}

TEST_F(SysCatalogTest, TestTabletMemTracker) {
  const auto& tablet_mem_tracker =
      sys_catalog_->TEST_GetTabletPeer()->shared_tablet()->mem_tracker();
  ASSERT_EQ("Tablets_overhead", tablet_mem_tracker->parent()->id());
  ASSERT_EQ("mem_tracker_server_Tablets_overhead_PerTablet", tablet_mem_tracker->metric_name());
}

} // namespace master
} // namespace yb
