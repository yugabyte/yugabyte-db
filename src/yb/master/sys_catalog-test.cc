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

#include <gtest/gtest.h>

#include "yb/common/wire_protocol.h"
#include "yb/gutil/stl_util.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog.h"
#include "yb/server/rpc_server.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"
#include "yb/rpc/messenger.h"
#include "yb/common/common.pb.h"

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

class SysCatalogTest : public YBTest {
 protected:
  void SetUp() override {
    YBTest::SetUp();

    // Start master with the create flag on.
    mini_master_.reset(
        new MiniMaster(Env::Default(), GetTestPath("Master"), AllocateFreePort(),
                       AllocateFreePort()));
    ASSERT_OK(mini_master_->Start());
    master_ = mini_master_->master();
    ASSERT_OK(master_->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

    // Create a client proxy to it.
    MessengerBuilder bld("Client");
    ASSERT_OK(bld.Build().MoveTo(&client_messenger_));
    proxy_.reset(new MasterServiceProxy(client_messenger_, mini_master_->bound_rpc_addr()));
  }

  void TearDown() override {
    mini_master_->Shutdown();
    YBTest::TearDown();
  }

  shared_ptr<Messenger> client_messenger_;
  gscoped_ptr<MiniMaster> mini_master_;
  Master* master_;
  gscoped_ptr<MasterServiceProxy> proxy_;
};

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
    l->mutable_data()->pb.CopyFrom(metadata);
    l->Commit();
    table->AddRef();
    tables[table->id()] = table;
    return Status::OK();
  }

  std::map<std::string, TableInfo*> tables;
};

static bool PbEquals(const google::protobuf::Message& a, const google::protobuf::Message& b) {
  return a.DebugString() == b.DebugString();
}

template<class C>
static bool MetadatasEqual(C* ti_a, C* ti_b) {
  auto l_a = ti_a->LockForRead();
  auto l_b = ti_b->LockForRead();
  return PbEquals(l_a->data().pb, l_b->data().pb);
}

TEST_F(SysCatalogTest, TestPrepareDefaultClusterConfig) {

  FLAGS_cluster_uuid = "invalid_uuid";

  CatalogManager catalog_manager(nullptr);

  ASSERT_NOK(catalog_manager.PrepareDefaultClusterConfig());

  auto dir = GetTestPath("Master") + "valid_cluster_uuid_test";
  ASSERT_OK(Env::Default()->CreateDir(dir));
  std::unique_ptr<MiniMaster> mini_master =
      std::make_unique<MiniMaster>(Env::Default(), dir, AllocateFreePort(), AllocateFreePort());


  // Test that config.cluster_uuid gets set to the value that we specify through flag cluster_uuid.
  FLAGS_cluster_uuid = to_string(Uuid::Generate());
  ASSERT_OK(mini_master->Start());
  auto master = mini_master->master();
  ASSERT_OK(master->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  SysClusterConfigEntryPB config;
  ASSERT_OK(master->catalog_manager()->GetClusterConfig(&config));

  // Verify that the cluster uuid was set in the config.
  ASSERT_EQ(FLAGS_cluster_uuid, config.cluster_uuid());

  master->Shutdown();

  // Test that config.cluster_uuid gets set to a valid uuid when cluster_uuid flag is empty.
  dir = GetTestPath("Master") + "empty_cluster_uuid_test";
  ASSERT_OK(Env::Default()->CreateDir(dir));
  mini_master =
      std::make_unique<MiniMaster>(Env::Default(), dir, AllocateFreePort(), AllocateFreePort());
  FLAGS_cluster_uuid = "";
  ASSERT_OK(mini_master->Start());
  master = mini_master->master();
  ASSERT_OK(master->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  ASSERT_OK(master->catalog_manager()->GetClusterConfig(&config));

  // Check that the cluster_uuid was set.
  ASSERT_FALSE(config.cluster_uuid().empty());

  // Check that the cluster uuid is valid.
  Uuid uuid;
  ASSERT_OK(uuid.FromString(config.cluster_uuid()));

  master->Shutdown();
}

// Test the sys-catalog tables basic operations (add, update, delete,
// visit)
TEST_F(SysCatalogTest, TestSysCatalogTablesOperations) {
  SysCatalogTable* sys_catalog = master_->catalog_manager()->sys_catalog();

  unique_ptr<TestTableLoader> loader(new TestTableLoader());
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(master_->NumSystemTables(), loader->tables.size());

  // Create new table.
  const std::string table_id = "abc";
  scoped_refptr<TableInfo> table(new TableInfo(table_id));
  {
    auto l = table->LockForWrite();
    l->mutable_data()->pb.set_name("testtb");
    l->mutable_data()->pb.set_version(0);
    l->mutable_data()->pb.mutable_replication_info()->mutable_live_replicas()->set_num_replicas(1);
    l->mutable_data()->pb.set_state(SysTablesEntryPB::PREPARING);
    ASSERT_OK(SchemaToPB(Schema(), l->mutable_data()->pb.mutable_schema()));
    // Add the table
    ASSERT_OK(sys_catalog->AddItem(table.get()));

    l->Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));

  ASSERT_EQ(1 + master_->NumSystemTables(), loader->tables.size());
  ASSERT_TRUE(MetadatasEqual(table.get(), loader->tables[table_id]));

  // Update the table
  {
    auto l = table->LockForWrite();
    l->mutable_data()->pb.set_version(1);
    l->mutable_data()->pb.set_state(SysTablesEntryPB::DELETING);
    ASSERT_OK(sys_catalog->UpdateItem(table.get()));
    l->Commit();
  }

  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(1 + master_->NumSystemTables(), loader->tables.size());
  ASSERT_TRUE(MetadatasEqual(table.get(), loader->tables[table_id]));

  // Delete the table
  loader->Reset();
  ASSERT_OK(sys_catalog->DeleteItem(table.get()));
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(master_->NumSystemTables(), loader->tables.size());
}

// Verify that data mutations are not available from metadata() until commit.
TEST_F(SysCatalogTest, TestTableInfoCommit) {
  scoped_refptr<TableInfo> table(new TableInfo("123"));

  // Mutate the table, under the write lock.
  auto writer_lock = table->LockForWrite();
  writer_lock->mutable_data()->pb.set_name("foo");

  // Changes should not be visible to a reader.
  // The reader can still lock for read, since readers don't block
  // writers in the RWC lock.
  {
    auto reader_lock = table->LockForRead();
    ASSERT_NE("foo", reader_lock->data().name());
  }
  writer_lock->mutable_data()->set_state(SysTablesEntryPB::RUNNING, "running");


  {
    auto reader_lock = table->LockForRead();
    ASSERT_NE("foo", reader_lock->data().pb.name());
    ASSERT_NE("running", reader_lock->data().pb.state_msg());
    ASSERT_NE(SysTablesEntryPB::RUNNING, reader_lock->data().pb.state());
  }

  // Commit the changes
  writer_lock->Commit();

  // Verify that the data is visible
  {
    auto reader_lock = table->LockForRead();
    ASSERT_EQ("foo", reader_lock->data().pb.name());
    ASSERT_EQ("running", reader_lock->data().pb.state_msg());
    ASSERT_EQ(SysTablesEntryPB::RUNNING, reader_lock->data().pb.state());
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
    l->mutable_data()->pb.CopyFrom(metadata);
    l->Commit();
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
  auto l = tablet->LockForWrite();
  l->mutable_data()->pb.set_state(SysTabletsEntryPB::PREPARING);
  l->mutable_data()->pb.mutable_partition()->set_partition_key_start(start_key);
  l->mutable_data()->pb.mutable_partition()->set_partition_key_end(end_key);
  l->mutable_data()->pb.set_table_id(table->id());
  l->Commit();
  return tablet;
}

// Test the sys-catalog tablets basic operations (add, update, delete,
// visit)
TEST_F(SysCatalogTest, TestSysCatalogTabletsOperations) {
  scoped_refptr<TableInfo> table(new TableInfo("abc"));
  scoped_refptr<TabletInfo> tablet1(CreateTablet(table.get(), "123", "a", "b"));
  scoped_refptr<TabletInfo> tablet2(CreateTablet(table.get(), "456", "b", "c"));
  scoped_refptr<TabletInfo> tablet3(CreateTablet(table.get(), "789", "c", "d"));

  SysCatalogTable* sys_catalog = master_->catalog_manager()->sys_catalog();

  unique_ptr<TestTabletLoader> loader(new TestTabletLoader());
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(master_->NumSystemTables(), loader->tablets.size());

  // Add tablet1 and tablet2
  {
    std::vector<TabletInfo*> tablets;
    tablets.push_back(tablet1.get());
    tablets.push_back(tablet2.get());

    loader->Reset();
    auto l1 = tablet1->LockForWrite();
    auto l2 = tablet2->LockForWrite();
    ASSERT_OK(sys_catalog->AddItems(tablets));
    l1->Commit();
    l2->Commit();

    ASSERT_OK(sys_catalog->Visit(loader.get()));
    ASSERT_EQ(2 + master_->NumSystemTables(), loader->tablets.size());
    ASSERT_TRUE(MetadatasEqual(tablet1.get(), loader->tablets[tablet1->id()]));
    ASSERT_TRUE(MetadatasEqual(tablet2.get(), loader->tablets[tablet2->id()]));
  }

  // Update tablet1
  {
    std::vector<TabletInfo*> tablets;
    tablets.push_back(tablet1.get());

    auto l1 = tablet1->LockForWrite();
    l1->mutable_data()->pb.set_state(SysTabletsEntryPB::RUNNING);
    ASSERT_OK(sys_catalog->UpdateItems(tablets));
    l1->Commit();

    loader->Reset();
    ASSERT_OK(sys_catalog->Visit(loader.get()));
    ASSERT_EQ(2 + master_->NumSystemTables(), loader->tablets.size());
    ASSERT_TRUE(MetadatasEqual(tablet1.get(), loader->tablets[tablet1->id()]));
    ASSERT_TRUE(MetadatasEqual(tablet2.get(), loader->tablets[tablet2->id()]));
  }

  // Add tablet3 and Update tablet1 and tablet2
  {
    std::vector<TabletInfo *> to_add;
    std::vector<TabletInfo *> to_update;

    auto l3 = tablet3->LockForWrite();
    to_add.push_back(tablet3.get());
    to_update.push_back(tablet1.get());
    to_update.push_back(tablet2.get());

    auto l1 = tablet1->LockForWrite();
    l1->mutable_data()->pb.set_state(SysTabletsEntryPB::REPLACED);
    auto l2 = tablet2->LockForWrite();
    l2->mutable_data()->pb.set_state(SysTabletsEntryPB::RUNNING);

    loader->Reset();
    ASSERT_OK(sys_catalog->AddAndUpdateItems(to_add, to_update));

    l1->Commit();
    l2->Commit();
    l3->Commit();

    ASSERT_OK(sys_catalog->Visit(loader.get()));
    ASSERT_EQ(3 + master_->NumSystemTables(), loader->tablets.size());
    ASSERT_TRUE(MetadatasEqual(tablet1.get(), loader->tablets[tablet1->id()]));
    ASSERT_TRUE(MetadatasEqual(tablet2.get(), loader->tablets[tablet2->id()]));
    ASSERT_TRUE(MetadatasEqual(tablet3.get(), loader->tablets[tablet3->id()]));
  }

  // Delete tablet1 and tablet3 tablets
  {
    std::vector<TabletInfo*> tablets;
    tablets.push_back(tablet1.get());
    tablets.push_back(tablet3.get());

    loader->Reset();
    ASSERT_OK(sys_catalog->DeleteItems(tablets));
    ASSERT_OK(sys_catalog->Visit(loader.get()));
    ASSERT_EQ(1 + master_->NumSystemTables(), loader->tablets.size());
    ASSERT_TRUE(MetadatasEqual(tablet2.get(), loader->tablets[tablet2->id()]));
  }
}

// Verify that data mutations are not available from metadata() until commit.
TEST_F(SysCatalogTest, TestTabletInfoCommit) {
  scoped_refptr<TabletInfo> tablet(new TabletInfo(nullptr, "123"));

  // Mutate the tablet, the changes should not be visible
  auto l = tablet->LockForWrite();
  PartitionPB* partition = l->mutable_data()->pb.mutable_partition();
  partition->set_partition_key_start("a");
  partition->set_partition_key_end("b");
  l->mutable_data()->set_state(SysTabletsEntryPB::RUNNING, "running");
  {
    // Changes shouldn't be visible, and lock should still be
    // acquired even though the mutation is under way.
    auto read_lock = tablet->LockForRead();
    ASSERT_NE("a", read_lock->data().pb.partition().partition_key_start());
    ASSERT_NE("b", read_lock->data().pb.partition().partition_key_end());
    ASSERT_NE("running", read_lock->data().pb.state_msg());
    ASSERT_NE(SysTabletsEntryPB::RUNNING,
              read_lock->data().pb.state());
  }

  // Commit the changes
  l->Commit();

  // Verify that the data is visible
  {
    auto read_lock = tablet->LockForRead();
    ASSERT_EQ("a", read_lock->data().pb.partition().partition_key_start());
    ASSERT_EQ("b", read_lock->data().pb.partition().partition_key_end());
    ASSERT_EQ("running", read_lock->data().pb.state_msg());
    ASSERT_EQ(SysTabletsEntryPB::RUNNING,
              read_lock->data().pb.state());
  }
}

class TestClusterConfigLoader : public Visitor<PersistentClusterConfigInfo> {
 public:
  TestClusterConfigLoader() {}
  ~TestClusterConfigLoader() { Reset(); }

  virtual Status Visit(
      const std::string& fake_id, const SysClusterConfigEntryPB& metadata) override {
    CHECK(!config_info) << "We either got multiple config_info entries, or we didn't Reset()";
    config_info = new ClusterConfigInfo();
    auto l = config_info->LockForWrite();
    l->mutable_data()->pb.CopyFrom(metadata);
    l->Commit();
    config_info->AddRef();
    return Status::OK();
  }

  void Reset() {
    if (config_info) {
      config_info->Release();
      config_info = nullptr;
    }
  }

  ClusterConfigInfo* config_info = nullptr;
};

// Test the sys-catalog tables basic operations (add, update, delete, visit)
TEST_F(SysCatalogTest, TestSysCatalogPlacementOperations) {
  SysCatalogTable* sys_catalog = master_->catalog_manager()->sys_catalog();

  unique_ptr<TestClusterConfigLoader> loader(new TestClusterConfigLoader());
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_TRUE(loader->config_info);
  {
    auto l = loader->config_info->LockForRead();
    ASSERT_EQ(l->data().pb.version(), 0);
    ASSERT_EQ(l->data().pb.replication_info().live_replicas().placement_blocks_size(), 0);
  }

  // Test modifications directly through the Sys catalog API.

  // Create a config_info block.
  scoped_refptr<ClusterConfigInfo> config_info(new ClusterConfigInfo());
  {
    auto l = config_info->LockForWrite();
    auto pb = l->mutable_data()
                  ->pb.mutable_replication_info()
                  ->mutable_live_replicas()
                  ->add_placement_blocks();
    auto cloud_info = pb->mutable_cloud_info();
    cloud_info->set_placement_cloud("cloud");
    cloud_info->set_placement_region("region");
    cloud_info->set_placement_zone("zone");
    pb->set_min_num_replicas(100);

    // Set it in the sys_catalog. It already has the default entry, so we use update.
    ASSERT_OK(sys_catalog->UpdateItem(config_info.get()));
    l->Commit();
  }

  // Check data from sys_catalog.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_TRUE(loader->config_info);
  ASSERT_TRUE(MetadatasEqual(config_info.get(), loader->config_info));

  {
    auto l = config_info->LockForWrite();
    auto pb = l->mutable_data()
                  ->pb.mutable_replication_info()
                  ->mutable_live_replicas()
                  ->mutable_placement_blocks(0);
    auto cloud_info = pb->mutable_cloud_info();
    // Update some config_info info.
    cloud_info->set_placement_cloud("cloud2");
    pb->set_min_num_replicas(200);
    // Update it in the sys_catalog.
    ASSERT_OK(sys_catalog->UpdateItem(config_info.get()));
    l->Commit();
  }

  // Check data from sys_catalog.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_TRUE(loader->config_info);
  ASSERT_TRUE(MetadatasEqual(config_info.get(), loader->config_info));

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
    auto pb = l->mutable_data()
                  ->pb.mutable_replication_info()
                  ->mutable_live_replicas()
                  ->mutable_placement_blocks(0);
    pb->set_min_num_replicas(300);

    ChangeMasterClusterConfigRequestPB req;
    *req.mutable_cluster_config() = l->mutable_data()->pb;
    ChangeMasterClusterConfigResponsePB resp;

    // Verify that we receive an error when trying to change the cluster uuid.
    req.mutable_cluster_config()->set_cluster_uuid("some-cluster-uuid");
    auto status = master_->catalog_manager()->SetClusterConfig(&req, &resp);
    ASSERT_TRUE(status.IsInvalidArgument());

    // Setting the cluster uuid should make the request succeed.
    req.mutable_cluster_config()->set_cluster_uuid(config.cluster_uuid());

    ASSERT_OK(master_->catalog_manager()->SetClusterConfig(&req, &resp));
    l->Commit();
  }

  // Confirm the in memory state does not match the config we get from the CatalogManager API, due
  // to version mismatch.
  ASSERT_OK(master_->catalog_manager()->GetClusterConfig(&config));
  {
    auto l = config_info->LockForRead();
    ASSERT_FALSE(PbEquals(l->data().pb, config));
    ASSERT_EQ(l->data().pb.version(), 0);
    ASSERT_EQ(config.version(), 1);
    ASSERT_TRUE(PbEquals(
        l->data().pb.replication_info().live_replicas().placement_blocks(0),
        config.replication_info().live_replicas().placement_blocks(0)));
  }

  // Reload the data again and check that it matches expectations.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_TRUE(loader->config_info);
  {
    auto l = loader->config_info->LockForRead();
    ASSERT_TRUE(PbEquals(l->data().pb, config));
    ASSERT_TRUE(l->data().pb.has_version());
    ASSERT_EQ(l->data().pb.version(), 1);
    ASSERT_EQ(l->data().pb.replication_info().live_replicas().placement_blocks_size(), 1);
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
    l->mutable_data()->pb.CopyFrom(metadata);
    l->Commit();
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
    l->mutable_data()->pb.set_name("test_ns");
    // Add the namespace
    ASSERT_OK(sys_catalog->AddItem(ns.get()));

    l->Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(1 + kNumSystemNamespaces, loader->namespaces.size());
  ASSERT_TRUE(MetadatasEqual(ns.get(), loader->namespaces[loader->namespaces.size() - 1]));

  // 3. CHECK UPDATE_NAMESPACE
  // Update the namespace
  {
    auto l = ns->LockForWrite();
    l->mutable_data()->pb.set_name("test_ns_new_name");
    ASSERT_OK(sys_catalog->UpdateItem(ns.get()));
    l->Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(1 + kNumSystemNamespaces, loader->namespaces.size());
  ASSERT_TRUE(MetadatasEqual(ns.get(), loader->namespaces[loader->namespaces.size() - 1]));

  // 4. CHECK DELETE_NAMESPACE
  // Delete the namespace
  ASSERT_OK(sys_catalog->DeleteItem(ns.get()));

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
  writer_lock->mutable_data()->pb.set_name("foo");

  // Changes should not be visible to a reader.
  // The reader can still lock for read, since readers don't block
  // writers in the RWC lock.
  {
    auto reader_lock = ns->LockForRead();
    ASSERT_NE("foo", reader_lock->data().name());
  }

  // Commit the changes
  writer_lock->Commit();

  // Verify that the data is visible
  {
    auto reader_lock = ns->LockForRead();
    ASSERT_EQ("foo", reader_lock->data().name());
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
    l->mutable_data()->pb.CopyFrom(metadata);
    l->Commit();
    tp->AddRef();
    udtypes.push_back(tp);
    return Status::OK();
  }

  vector<UDTypeInfo*> udtypes;
};


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
    l->mutable_data()->pb.CopyFrom(metadata);
    l->Commit();
    rl->AddRef();
    roles.push_back(rl);
    LOG(INFO) << " Current Role:" << rl->ToString();
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
    l->mutable_data()->pb = std::move(role_entry);
    // Add the role
    ASSERT_OK(sys_catalog->AddItem(rl.get()));
    l->Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  // The first role is the default cassandra role
  ASSERT_EQ(2, loader->roles.size());
  ASSERT_TRUE(MetadatasEqual(rl.get(), loader->roles[1]));

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

  ASSERT_OK(sys_catalog->UpdateItem(rl.get()));
  rl->mutable_metadata()->CommitMutation();

  // Verify permissions
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  // The first role is the default cassandra role
  ASSERT_EQ(2, loader->roles.size());
  ASSERT_TRUE(MetadatasEqual(rl.get(), loader->roles[1]));

  // 2. CHECK DELETE Role
  ASSERT_OK(sys_catalog->DeleteItem(rl.get()));

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
    l->mutable_data()->pb.set_name("test_tp");
    l->mutable_data()->pb.set_namespace_id(kSystemNamespaceId);
    // Add the udtype
    ASSERT_OK(sys_catalog->AddItem(tp.get()));
    l->Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(1, loader->udtypes.size());
  ASSERT_TRUE(MetadatasEqual(tp.get(), loader->udtypes[0]));

  // 2. CHECK DELETE_UDTYPE
  ASSERT_OK(sys_catalog->DeleteItem(tp.get()));

  // Verify the result.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(0, loader->udtypes.size());
}

} // namespace master
} // namespace yb
