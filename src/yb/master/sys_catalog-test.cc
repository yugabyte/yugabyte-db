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

#include "yb/util/net/sockaddr.h"
#include "yb/util/status.h"

using std::make_shared;
using std::string;
using std::shared_ptr;
using std::unique_ptr;
using yb::rpc::Messenger;
using yb::rpc::MessengerBuilder;
using yb::rpc::RpcController;

DECLARE_string(cluster_uuid);

namespace yb {
namespace master {

class TestTableLoader : public Visitor<PersistentTableInfo> {
 public:
  TestTableLoader() {}
  ~TestTableLoader() { Reset(); }

  void Reset() {
    for (const auto& entry :  tables) {
      entry.second->Release();
    }
    tables.clear();
  }

  Status Visit(const std::string& table_id, const SysTablesEntryPB& metadata) override {
    // Setup the table info
    TableInfo *table = new TableInfo(table_id);
    auto l = table->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    table->AddRef();
    tables[table->id()] = table;
    return Status::OK();
  }

  std::map<std::string, TableInfo*> tables;
};

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
  FLAGS_cluster_uuid = Uuid::Generate().ToString();
  ASSERT_OK(mini_master->Start());
  auto master = mini_master->master();
  ASSERT_OK(master->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  SysClusterConfigEntryPB config;
  ASSERT_OK(master->catalog_manager()->GetClusterConfig(&config));

  // Verify that the cluster uuid was set in the config.
  ASSERT_EQ(FLAGS_cluster_uuid, config.cluster_uuid());

  mini_master->Shutdown();

  // Test that config.cluster_uuid gets set to a valid uuid when cluster_uuid flag is empty.
  dir = GetTestPath("Master") + "empty_cluster_uuid_test";
  ASSERT_OK(Env::Default()->CreateDir(dir));
  mini_master =
      std::make_unique<MiniMaster>(Env::Default(), dir, AllocateFreePort(), AllocateFreePort(), 0);
  FLAGS_cluster_uuid = "";
  ASSERT_OK(mini_master->Start());
  master = mini_master->master();
  ASSERT_OK(master->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  ASSERT_OK(master->catalog_manager()->GetClusterConfig(&config));

  // Check that the cluster_uuid was set.
  ASSERT_FALSE(config.cluster_uuid().empty());

  // Check that the cluster uuid is valid.
  ASSERT_OK(Uuid::FromString(config.cluster_uuid()));

  mini_master->Shutdown();
}

// Test the sys-catalog tables basic operations (add, update, delete,
// visit)
TEST_F(SysCatalogTest, TestSysCatalogTablesOperations) {
  SysCatalogTable* sys_catalog = master_->catalog_manager()->sys_catalog();

  unique_ptr<TestTableLoader> loader(new TestTableLoader());
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(kNumSystemTables, loader->tables.size());

  // Create new table.
  const std::string table_id = "abc";
  scoped_refptr<TableInfo> table = master_->catalog_manager()->NewTableInfo(table_id);
  {
    auto l = table->LockForWrite();
    l.mutable_data()->pb.set_name("testtb");
    l.mutable_data()->pb.set_version(0);
    l.mutable_data()->pb.mutable_replication_info()->mutable_live_replicas()->set_num_replicas(1);
    l.mutable_data()->pb.set_state(SysTablesEntryPB::PREPARING);
    SchemaToPB(Schema(), l.mutable_data()->pb.mutable_schema());
    // Add the table
    ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, table));

    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));

  ASSERT_EQ(1 + kNumSystemTables, loader->tables.size());
  ASSERT_METADATA_EQ(table.get(), loader->tables[table_id]);

  // Update the table
  {
    auto l = table->LockForWrite();
    l.mutable_data()->pb.set_version(1);
    l.mutable_data()->pb.set_state(SysTablesEntryPB::DELETING);
    ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, table));
    l.Commit();
  }

  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(1 + kNumSystemTables, loader->tables.size());
  ASSERT_METADATA_EQ(table.get(), loader->tables[table_id]);

  // Delete the table
  loader->Reset();
  ASSERT_OK(sys_catalog->Delete(kLeaderTerm, table));
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(kNumSystemTables, loader->tables.size());
}

// Verify that data mutations are not available from metadata() until commit.
TEST_F(SysCatalogTest, TestTableInfoCommit) {
  scoped_refptr<TableInfo> table(master_->catalog_manager()->NewTableInfo("123"));

  // Mutate the table, under the write lock.
  auto writer_lock = table->LockForWrite();
  writer_lock.mutable_data()->pb.set_name("foo");

  // Changes should not be visible to a reader.
  // The reader can still lock for read, since readers don't block
  // writers in the RWC lock.
  {
    auto reader_lock = table->LockForRead();
    ASSERT_NE("foo", reader_lock->name());
  }
  writer_lock.mutable_data()->set_state(SysTablesEntryPB::RUNNING, "running");


  {
    auto reader_lock = table->LockForRead();
    ASSERT_NE("foo", reader_lock->pb.name());
    ASSERT_NE("running", reader_lock->pb.state_msg());
    ASSERT_NE(SysTablesEntryPB::RUNNING, reader_lock->pb.state());
  }

  // Commit the changes
  writer_lock.Commit();

  // Verify that the data is visible
  {
    auto reader_lock = table->LockForRead();
    ASSERT_EQ("foo", reader_lock->pb.name());
    ASSERT_EQ("running", reader_lock->pb.state_msg());
    ASSERT_EQ(SysTablesEntryPB::RUNNING, reader_lock->pb.state());
  }
}

class TestTabletLoader : public Visitor<PersistentTabletInfo> {
 public:
  TestTabletLoader() {}
  ~TestTabletLoader() { Reset(); }

  void Reset() {
    for (const auto& entry : tablets) {
      entry.second->Release();
    }
    tablets.clear();
  }

  Status Visit(const std::string& tablet_id, const SysTabletsEntryPB& metadata) override {
    // Setup the tablet info
    TabletInfo *tablet = new TabletInfo(nullptr, tablet_id);
    auto l = tablet->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    tablet->AddRef();
    tablets[tablet->id()] = tablet;
    return Status::OK();
  }

  std::map<std::string, TabletInfo*> tablets;
};

// Create a new TabletInfo. The object is in uncommitted
// state.
static TabletInfo *CreateTablet(TableInfo *table,
                                const string& tablet_id,
                                const string& start_key,
                                const string& end_key) {
  TabletInfo *tablet = new TabletInfo(table, tablet_id);
  tablet->mutable_metadata()->StartMutation();
  auto* metadata = &tablet->mutable_metadata()->mutable_dirty()->pb;
  metadata->set_state(SysTabletsEntryPB::PREPARING);
  metadata->mutable_partition()->set_partition_key_start(start_key);
  metadata->mutable_partition()->set_partition_key_end(end_key);
  metadata->set_table_id(table->id());
  return tablet;
}

// Test the sys-catalog tablets basic operations (add, update, delete,
// visit)
TEST_F(SysCatalogTest, TestSysCatalogTabletsOperations) {
  scoped_refptr<TableInfo> table(master_->catalog_manager()->NewTableInfo("abc"));
  // This leaves all three in StartMutation.
  scoped_refptr<TabletInfo> tablet1(CreateTablet(table.get(), "123", "a", "b"));
  scoped_refptr<TabletInfo> tablet2(CreateTablet(table.get(), "456", "b", "c"));
  scoped_refptr<TabletInfo> tablet3(CreateTablet(table.get(), "789", "c", "d"));

  SysCatalogTable* sys_catalog = master_->catalog_manager()->sys_catalog();

  unique_ptr<TestTabletLoader> loader(new TestTabletLoader());
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(kNumSystemTables, loader->tablets.size());

  // Add tablet1 and tablet2
  {
    std::vector<TabletInfo*> tablets;
    tablets.push_back(tablet1.get());
    tablets.push_back(tablet2.get());

    loader->Reset();
    ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, tablets));
    tablet1->mutable_metadata()->CommitMutation();
    tablet2->mutable_metadata()->CommitMutation();

    ASSERT_OK(sys_catalog->Visit(loader.get()));
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
    ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, tablets));
    l1.Commit();

    loader->Reset();
    ASSERT_OK(sys_catalog->Visit(loader.get()));
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
    ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, to_add, to_update));

    l1.Commit();
    l2.Commit();
    // This was still open from the initial create!
    tablet3->mutable_metadata()->CommitMutation();

    ASSERT_OK(sys_catalog->Visit(loader.get()));
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
    ASSERT_OK(sys_catalog->Delete(kLeaderTerm, tablets));
    ASSERT_OK(sys_catalog->Visit(loader.get()));
    ASSERT_EQ(1 + kNumSystemTables, loader->tablets.size());
    ASSERT_METADATA_EQ(tablet2.get(), loader->tablets[tablet2->id()]);
  }
}

// Verify that data mutations are not available from metadata() until commit.
TEST_F(SysCatalogTest, TestTabletInfoCommit) {
  scoped_refptr<TabletInfo> tablet(new TabletInfo(nullptr, "123"));

  // Mutate the tablet, the changes should not be visible
  auto l = tablet->LockForWrite();
  PartitionPB* partition = l.mutable_data()->pb.mutable_partition();
  partition->set_partition_key_start("a");
  partition->set_partition_key_end("b");
  l.mutable_data()->set_state(SysTabletsEntryPB::RUNNING, "running");
  {
    // Changes shouldn't be visible, and lock should still be
    // acquired even though the mutation is under way.
    auto read_lock = tablet->LockForRead();
    ASSERT_NE("a", read_lock->pb.partition().partition_key_start());
    ASSERT_NE("b", read_lock->pb.partition().partition_key_end());
    ASSERT_NE("running", read_lock->pb.state_msg());
    ASSERT_NE(SysTabletsEntryPB::RUNNING,
              read_lock->pb.state());
  }

  // Commit the changes
  l.Commit();

  // Verify that the data is visible
  {
    auto read_lock = tablet->LockForRead();
    ASSERT_EQ("a", read_lock->pb.partition().partition_key_start());
    ASSERT_EQ("b", read_lock->pb.partition().partition_key_end());
    ASSERT_EQ("running", read_lock->pb.state_msg());
    ASSERT_EQ(SysTabletsEntryPB::RUNNING,
              read_lock->pb.state());
  }
}

class TestClusterConfigLoader : public Visitor<PersistentClusterConfigInfo> {
 public:
  TestClusterConfigLoader() {}
  ~TestClusterConfigLoader() { Reset(); }

  virtual Status Visit(
      const std::string& fake_id, const SysClusterConfigEntryPB& metadata) override {
    CHECK(!config_info) << "We either got multiple config_info entries, or we didn't Reset()";
    config_info = std::make_shared<ClusterConfigInfo>();
    auto l = config_info->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    return Status::OK();
  }

  void Reset() {
    config_info.reset();
  }

  std::shared_ptr<ClusterConfigInfo> config_info = nullptr;
};

// Test the sys-catalog tables basic operations (add, update, delete, visit)
TEST_F(SysCatalogTest, TestSysCatalogPlacementOperations) {
  SysCatalogTable* sys_catalog = master_->catalog_manager()->sys_catalog();

  unique_ptr<TestClusterConfigLoader> loader(new TestClusterConfigLoader());
  ASSERT_OK(sys_catalog->Visit(loader.get()));
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
    auto pb = l.mutable_data()
                  ->pb.mutable_replication_info()
                  ->mutable_live_replicas()
                  ->add_placement_blocks();
    auto cloud_info = pb->mutable_cloud_info();
    cloud_info->set_placement_cloud("cloud");
    cloud_info->set_placement_region("region");
    cloud_info->set_placement_zone("zone");
    pb->set_min_num_replicas(100);

    // Set it in the sys_catalog. It already has the default entry, so we use update.
    ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, config_info.get()));
    l.Commit();
  }

  // Check data from sys_catalog.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_TRUE(loader->config_info);
  ASSERT_METADATA_EQ(config_info.get(), loader->config_info.get());

  {
    auto l = config_info->LockForWrite();
    auto pb = l.mutable_data()
                  ->pb.mutable_replication_info()
                  ->mutable_live_replicas()
                  ->mutable_placement_blocks(0);
    auto cloud_info = pb->mutable_cloud_info();
    // Update some config_info info.
    cloud_info->set_placement_cloud("cloud2");
    pb->set_min_num_replicas(200);
    // Update it in the sys_catalog.
    ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, config_info.get()));
    l.Commit();
  }

  // Check data from sys_catalog.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_TRUE(loader->config_info);
  ASSERT_METADATA_EQ(config_info.get(), loader->config_info.get());

  // Test data through the CatalogManager API.

  // Get the config from the CatalogManager and test it is the default, as we didn't use the
  // CatalogManager to update it.
  SysClusterConfigEntryPB config;
  ASSERT_OK(master_->catalog_manager()->GetClusterConfig(&config));
  ASSERT_EQ(config.version(), 0);
  ASSERT_EQ(config.replication_info().live_replicas().placement_blocks_size(), 0);

  // Update a field in the previously used in memory state and set through proper API.
  {
    auto l = config_info->LockForWrite();
    auto pb = l.mutable_data()
                  ->pb.mutable_replication_info()
                  ->mutable_live_replicas()
                  ->mutable_placement_blocks(0);
    pb->set_min_num_replicas(300);

    ChangeMasterClusterConfigRequestPB req;
    *req.mutable_cluster_config() = l.mutable_data()->pb;
    ChangeMasterClusterConfigResponsePB resp;

    // Verify that we receive an error when trying to change the cluster uuid.
    req.mutable_cluster_config()->set_cluster_uuid("some-cluster-uuid");
    auto status = master_->catalog_manager()->SetClusterConfig(&req, &resp);
    ASSERT_TRUE(status.IsInvalidArgument());

    // Setting the cluster uuid should make the request succeed.
    req.mutable_cluster_config()->set_cluster_uuid(config.cluster_uuid());

    ASSERT_OK(master_->catalog_manager()->SetClusterConfig(&req, &resp));
    l.Commit();
  }

  // Confirm the in memory state does not match the config we get from the CatalogManager API, due
  // to version mismatch.
  ASSERT_OK(master_->catalog_manager()->GetClusterConfig(&config));
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
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_TRUE(loader->config_info);
  {
    auto l = loader->config_info->LockForRead();
    ASSERT_TRUE(PbEquals(l->pb, config));
    ASSERT_TRUE(l->pb.has_version());
    ASSERT_EQ(l->pb.version(), 1);
    ASSERT_EQ(l->pb.replication_info().live_replicas().placement_blocks_size(), 1);
  }
}

class TestNamespaceLoader : public Visitor<PersistentNamespaceInfo> {
 public:
  TestNamespaceLoader() {}
  ~TestNamespaceLoader() { Reset(); }

  void Reset() {
    for (NamespaceInfo* ni : namespaces) {
      ni->Release();
    }
    namespaces.clear();
  }

  Status Visit(const std::string& ns_id, const SysNamespaceEntryPB& metadata) override {
    // Setup the namespace info
    NamespaceInfo* const ns = new NamespaceInfo(ns_id);
    auto l = ns->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    ns->AddRef();
    namespaces.push_back(ns);
    return Status::OK();
  }

  vector<NamespaceInfo*> namespaces;
};

// Test the sys-catalog namespaces basic operations (add, update, delete, visit)
TEST_F(SysCatalogTest, TestSysCatalogNamespacesOperations) {
  SysCatalogTable* const sys_catalog = master_->catalog_manager()->sys_catalog();

  // 1. CHECK SYSTEM NAMESPACE COUNT
  unique_ptr<TestNamespaceLoader> loader(new TestNamespaceLoader());
  ASSERT_OK(sys_catalog->Visit(loader.get()));

  ASSERT_EQ(kNumSystemNamespaces, loader->namespaces.size());

  // 2. CHECK ADD_NAMESPACE
  // Create new namespace.
  scoped_refptr<NamespaceInfo> ns(new NamespaceInfo("deadbeafdeadbeafdeadbeafdeadbeaf"));
  {
    auto l = ns->LockForWrite();
    l.mutable_data()->pb.set_name("test_ns");
    // Add the namespace
    ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, ns));

    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(1 + kNumSystemNamespaces, loader->namespaces.size());
  ASSERT_METADATA_EQ(ns.get(), loader->namespaces[loader->namespaces.size() - 1]);

  // 3. CHECK UPDATE_NAMESPACE
  // Update the namespace
  {
    auto l = ns->LockForWrite();
    l.mutable_data()->pb.set_name("test_ns_new_name");
    ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, ns));
    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(1 + kNumSystemNamespaces, loader->namespaces.size());
  ASSERT_METADATA_EQ(ns.get(), loader->namespaces[loader->namespaces.size() - 1]);

  // 4. CHECK DELETE_NAMESPACE
  // Delete the namespace
  ASSERT_OK(sys_catalog->Delete(kLeaderTerm, ns));

  // Verify the result.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(kNumSystemNamespaces, loader->namespaces.size());
}

// Verify that data mutations are not available from metadata() until commit.
TEST_F(SysCatalogTest, TestNamespaceInfoCommit) {
  scoped_refptr<NamespaceInfo> ns(new NamespaceInfo("deadbeafdeadbeafdeadbeafdeadbeaf"));

  // Mutate the namespace, under the write lock.
  auto writer_lock = ns->LockForWrite();
  writer_lock.mutable_data()->pb.set_name("foo");

  // Changes should not be visible to a reader.
  // The reader can still lock for read, since readers don't block
  // writers in the RWC lock.
  {
    auto reader_lock = ns->LockForRead();
    ASSERT_NE("foo", reader_lock->name());
  }

  // Commit the changes
  writer_lock.Commit();

  // Verify that the data is visible
  {
    auto reader_lock = ns->LockForRead();
    ASSERT_EQ("foo", reader_lock->name());
  }
}

class TestUDTypeLoader : public Visitor<PersistentUDTypeInfo> {
 public:
  TestUDTypeLoader() {}
  ~TestUDTypeLoader() { Reset(); }

  void Reset() {
    for (UDTypeInfo* tp : udtypes) {
      tp->Release();
    }
    udtypes.clear();
  }

  Status Visit(const std::string& udtype_id, const SysUDTypeEntryPB& metadata) override {
    // Setup the udtype info
    UDTypeInfo* const tp = new UDTypeInfo(udtype_id);
    auto l = tp->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    tp->AddRef();
    udtypes.push_back(tp);
    return Status::OK();
  }

  vector<UDTypeInfo*> udtypes;
};

class TestRedisConfigLoader : public Visitor<PersistentRedisConfigInfo> {
 public:
  TestRedisConfigLoader() {}
  ~TestRedisConfigLoader() { Reset(); }

  void Reset() {
    for (RedisConfigInfo* rci : config_entries) {
      rci->Release();
    }
    config_entries.clear();
  }

  Status Visit(const std::string& key, const SysRedisConfigEntryPB& metadata) override {
    // Setup the redis config info
    RedisConfigInfo* const rci = new RedisConfigInfo(key);
    auto l = rci->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    rci->AddRef();
    config_entries.push_back(rci);
    return Status::OK();
  }

  vector<RedisConfigInfo*> config_entries;
};

// Test the sys-catalog redis config basic operations (add, visit, drop)
TEST_F(SysCatalogTest, TestSysCatalogRedisConfigOperations) {
  SysCatalogTable* const sys_catalog = master_->catalog_manager()->sys_catalog();

  unique_ptr<TestRedisConfigLoader> loader(new TestRedisConfigLoader());
  ASSERT_OK(sys_catalog->Visit(loader.get()));

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
    ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, rci));
    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  // The default config is empty
  ASSERT_EQ(1, loader->config_entries.size());
  ASSERT_METADATA_EQ(rci.get(), loader->config_entries[0]);

  // Update the same config.
  SysRedisConfigEntryPB* metadata;
  rci->mutable_metadata()->StartMutation();
  metadata = &rci->mutable_metadata()->mutable_dirty()->pb;

  metadata->clear_args();
  metadata->add_args("value1b");

  ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, rci));
  rci->mutable_metadata()->CommitMutation();

  // Verify config entries.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
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
      ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, rci2));
      l.Commit();
    }

    // Verify it showed up.
    loader->Reset();
    ASSERT_OK(sys_catalog->Visit(loader.get()));
    // The default config is empty
    ASSERT_EQ(2, loader->config_entries.size());
    ASSERT_METADATA_EQ(rci2.get(), loader->config_entries[1]);

    // 2. CHECK DELETE RedisConfig
    ASSERT_OK(sys_catalog->Delete(kLeaderTerm, rci2));

    // Verify the result.
    loader->Reset();
    ASSERT_OK(sys_catalog->Visit(loader.get()));
    ASSERT_EQ(1, loader->config_entries.size());
  }
  // 2. CHECK DELETE RedisConfig
  ASSERT_OK(sys_catalog->Delete(kLeaderTerm, rci));

  // Verify the result.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(0, loader->config_entries.size());
}

class TestSysConfigLoader : public Visitor<PersistentSysConfigInfo> {
 public:
  TestSysConfigLoader() {}
  ~TestSysConfigLoader() { Reset(); }

  void Reset() {
    for (SysConfigInfo* sys_config : sys_configs) {
      sys_config->Release();
    }
    sys_configs.clear();
  }

  Status Visit(const string& id, const SysConfigEntryPB& metadata) override {

    // Setup the sysconfig info.
    SysConfigInfo* const sys_config = new SysConfigInfo(id /* config_type */);
    auto l = sys_config->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    sys_config->AddRef();
    sys_configs.push_back(sys_config);
    LOG(INFO) << " Current SysConfigInfo: " << sys_config->ToString();
    return Status::OK();
  }

  vector<SysConfigInfo*> sys_configs;
};

// Test the sys-catalog sys-config basic operations (add, visit, drop).
TEST_F(SysCatalogTest, TestSysCatalogSysConfigOperations) {
  SysCatalogTable* const sys_catalog = master_->catalog_manager()->sys_catalog();

  // 1. Verify that when master initializes:
  //   a. "security-config" entry is set up with roles_version = 0.
  //   b. "ysql-catalog-configuration" entry is set up with version = 0 and the transactional YSQL
  //      sys catalog flag is set to true.
  //   c. "transaction-tables-config" entry is set up with version = 0.
  scoped_refptr<SysConfigInfo> security_config = new SysConfigInfo(kSecurityConfigType);
  {
    auto l = security_config->LockForWrite();
    l.mutable_data()->pb.mutable_security_config()->set_roles_version(0);
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
  ASSERT_OK(sys_catalog->Visit(loader.get()));
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
    ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, test_config));
    l.Commit();
  }
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(4, loader->sys_configs.size());
  ASSERT_METADATA_EQ(security_config.get(), loader->sys_configs[0]);
  ASSERT_METADATA_EQ(test_config.get(), loader->sys_configs[1]);
  ASSERT_METADATA_EQ(transaction_tables_config.get(), loader->sys_configs[2]);
  ASSERT_METADATA_EQ(ysql_catalog_config.get(), loader->sys_configs[3]);

  // 2. Remove the SysConfigEntry and verify that it got removed.
  ASSERT_OK(sys_catalog->Delete(kLeaderTerm, test_config));
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(3, loader->sys_configs.size());
  ASSERT_METADATA_EQ(security_config.get(), loader->sys_configs[0]);
  ASSERT_METADATA_EQ(transaction_tables_config.get(), loader->sys_configs[1]);
  ASSERT_METADATA_EQ(ysql_catalog_config.get(), loader->sys_configs[2]);
}

class TestRoleLoader : public Visitor<PersistentRoleInfo> {
 public:
  TestRoleLoader() {}
  ~TestRoleLoader() { Reset(); }

  void Reset() {
    for (RoleInfo* rl : roles) {
      rl->Release();
    }
    roles.clear();
  }

  Status Visit(const RoleName& role_name, const SysRoleEntryPB& metadata) override {

    // Setup the role info
    RoleInfo* const rl = new RoleInfo(role_name);
    auto l = rl->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    rl->AddRef();
    roles.push_back(rl);
    LOG(INFO) << " Current Role: " << rl->ToString();
    return Status::OK();
  }

  vector<RoleInfo*> roles;
};

// Test the sys-catalog role/permissions basic operations (add, visit, drop)
TEST_F(SysCatalogTest, TestSysCatalogRoleOperations) {
  SysCatalogTable* const sys_catalog = master_->catalog_manager()->sys_catalog();

  unique_ptr<TestRoleLoader> loader(new TestRoleLoader());
  ASSERT_OK(sys_catalog->Visit(loader.get()));

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
    ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, rl));
    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
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

  ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, rl));
  rl->mutable_metadata()->CommitMutation();

  // Verify permissions
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  // The first role is the default cassandra role
  ASSERT_EQ(2, loader->roles.size());
  ASSERT_METADATA_EQ(rl.get(), loader->roles[1]);

  // 2. CHECK DELETE Role
  ASSERT_OK(sys_catalog->Delete(kLeaderTerm, rl));

  // Verify the result.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(1, loader->roles.size());
}


// Test the sys-catalog udtype basic operations (add, delete, visit)
TEST_F(SysCatalogTest, TestSysCatalogUDTypeOperations) {
  SysCatalogTable* const sys_catalog = master_->catalog_manager()->sys_catalog();

  unique_ptr<TestUDTypeLoader> loader(new TestUDTypeLoader());
  ASSERT_OK(sys_catalog->Visit(loader.get()));

  // 1. CHECK ADD_UDTYPE
  scoped_refptr<UDTypeInfo> tp(new UDTypeInfo("deadbeafdeadbeafdeadbeafdeadbeaf"));
  {
    auto l = tp->LockForWrite();
    l.mutable_data()->pb.set_name("test_tp");
    l.mutable_data()->pb.set_namespace_id(kSystemNamespaceId);
    // Add the udtype
    ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, tp));
    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(1, loader->udtypes.size());
  ASSERT_METADATA_EQ(tp.get(), loader->udtypes[0]);

  // 2. CHECK DELETE_UDTYPE
  ASSERT_OK(sys_catalog->Delete(kLeaderTerm, tp));

  // Verify the result.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(0, loader->udtypes.size());
}

// Test the tasks tracker in catalog manager.
TEST_F(SysCatalogTest, TestCatalogManagerTasksTracker) {
  // Configure number of tasks flag and keep time flag.
  SetAtomicFlag(100, &FLAGS_tasks_tracker_num_tasks);
  SetAtomicFlag(100, &FLAGS_tasks_tracker_keep_time_multiplier);

  SysCatalogTable* sys_catalog = master_->catalog_manager()->sys_catalog();

  unique_ptr<TestTableLoader> loader(new TestTableLoader());
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(kNumSystemTables, loader->tables.size());

  // Create new table.
  const std::string table_id = "abc";
  scoped_refptr<TableInfo> table = master_->catalog_manager()->NewTableInfo(table_id);
  {
    auto l = table->LockForWrite();
    l.mutable_data()->pb.set_name("testtb");
    l.mutable_data()->pb.set_version(0);
    l.mutable_data()->pb.mutable_replication_info()->mutable_live_replicas()->set_num_replicas(1);
    l.mutable_data()->pb.set_state(SysTablesEntryPB::PREPARING);
    SchemaToPB(Schema(), l.mutable_data()->pb.mutable_schema());
    // Add the table.
    ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, table));

    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));

  ASSERT_EQ(1 + kNumSystemTables, loader->tables.size());
  ASSERT_METADATA_EQ(table.get(), loader->tables[table_id]);

  // Add tasks to the table (more than can fit in the cbuf).
  for (int task_id = 0; task_id < FLAGS_tasks_tracker_num_tasks + 10; ++task_id) {
    scoped_refptr<TabletInfo> tablet(new TabletInfo(table, kSysCatalogTableId));
    auto task = std::make_shared<AsyncTruncate>(
        master_, master_->catalog_manager()->AsyncTaskPool(), tablet);
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

} // namespace master
} // namespace yb
