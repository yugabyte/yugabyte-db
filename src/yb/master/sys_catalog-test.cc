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

using std::make_shared;
using std::string;
using std::shared_ptr;
using std::unique_ptr;
using yb::rpc::Messenger;
using yb::rpc::MessengerBuilder;
using yb::rpc::RpcController;

namespace yb {
namespace master {

class SysCatalogTest : public YBTest {
 protected:
  virtual void SetUp() OVERRIDE {
    YBTest::SetUp();

    // Start master with the create flag on.
    mini_master_.reset(
        new MiniMaster(Env::Default(), GetTestPath("Master"), AllocateFreePort(),
                       AllocateFreePort(), true /* is_creating */));
    ASSERT_OK(mini_master_->Start());
    master_ = mini_master_->master();
    ASSERT_OK(master_->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

    // Create a client proxy to it.
    MessengerBuilder bld("Client");
    ASSERT_OK(bld.Build(&client_messenger_));
    proxy_.reset(new MasterServiceProxy(client_messenger_, mini_master_->bound_rpc_addr()));
  }

  virtual void TearDown() OVERRIDE {
    mini_master_->Shutdown();
    YBTest::TearDown();
  }

  shared_ptr<Messenger> client_messenger_;
  gscoped_ptr<MiniMaster> mini_master_;
  Master* master_;
  gscoped_ptr<MasterServiceProxy> proxy_;
};

class TestTableLoader : public TableVisitor {
 public:
  TestTableLoader() {}
  ~TestTableLoader() { Reset(); }

  void Reset() {
    for (TableInfo* ti : tables) {
      ti->Release();
    }
    tables.clear();
  }

  virtual Status Visit(const std::string& table_id, const SysTablesEntryPB& metadata) OVERRIDE {
    // Setup the table info
    TableInfo *table = new TableInfo(table_id);
    TableMetadataLock l(table, TableMetadataLock::WRITE);
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    table->AddRef();
    tables.push_back(table);
    return Status::OK();
  }

  vector<TableInfo* > tables;
};

static bool PbEquals(const google::protobuf::Message& a, const google::protobuf::Message& b) {
  return a.DebugString() == b.DebugString();
}

template<class C>
static bool MetadatasEqual(C* ti_a, C* ti_b) {
  MetadataLock<C> l_a(ti_a, MetadataLock<C>::READ);
  MetadataLock<C> l_b(ti_a, MetadataLock<C>::READ);
  return PbEquals(l_a.data().pb, l_b.data().pb);
}

// Test the sys-catalog tables basic operations (add, update, delete,
// visit)
TEST_F(SysCatalogTest, TestSysCatalogTablesOperations) {
  SysCatalogTable* sys_catalog = master_->catalog_manager()->sys_catalog();

  unique_ptr<TestTableLoader> loader(new TestTableLoader());
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(master_->NumSystemTables(), loader->tables.size());

  // Create new table.
  scoped_refptr<TableInfo> table(new TableInfo("abc"));
  {
    TableMetadataLock l(table.get(), TableMetadataLock::WRITE);
    l.mutable_data()->pb.set_name("testtb");
    l.mutable_data()->pb.set_version(0);
    l.mutable_data()->pb.mutable_replication_info()->mutable_live_replicas()->set_num_replicas(1);
    l.mutable_data()->pb.set_state(SysTablesEntryPB::PREPARING);
    ASSERT_OK(SchemaToPB(Schema(), l.mutable_data()->pb.mutable_schema()));
    // Add the table
    ASSERT_OK(sys_catalog->AddTable(table.get()));

    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(1 + master_->NumSystemTables(), loader->tables.size());
  ASSERT_TRUE(MetadatasEqual(table.get(), loader->tables[0]));

  // Update the table
  {
    TableMetadataLock l(table.get(), TableMetadataLock::WRITE);
    l.mutable_data()->pb.set_version(1);
    l.mutable_data()->pb.set_state(SysTablesEntryPB::DELETING);
    ASSERT_OK(sys_catalog->UpdateTable(table.get()));
    l.Commit();
  }

  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(1 + master_->NumSystemTables(), loader->tables.size());
  ASSERT_TRUE(MetadatasEqual(table.get(), loader->tables[0]));

  // Delete the table
  loader->Reset();
  ASSERT_OK(sys_catalog->DeleteTable(table.get()));
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(master_->NumSystemTables(), loader->tables.size());
}

// Verify that data mutations are not available from metadata() until commit.
TEST_F(SysCatalogTest, TestTableInfoCommit) {
  scoped_refptr<TableInfo> table(new TableInfo("123"));

  // Mutate the table, under the write lock.
  TableMetadataLock writer_lock(table.get(), TableMetadataLock::WRITE);
  writer_lock.mutable_data()->pb.set_name("foo");

  // Changes should not be visible to a reader.
  // The reader can still lock for read, since readers don't block
  // writers in the RWC lock.
  {
    TableMetadataLock reader_lock(table.get(), TableMetadataLock::READ);
    ASSERT_NE("foo", reader_lock.data().name());
  }
  writer_lock.mutable_data()->set_state(SysTablesEntryPB::RUNNING, "running");


  {
    TableMetadataLock reader_lock(table.get(), TableMetadataLock::READ);
    ASSERT_NE("foo", reader_lock.data().pb.name());
    ASSERT_NE("running", reader_lock.data().pb.state_msg());
    ASSERT_NE(SysTablesEntryPB::RUNNING, reader_lock.data().pb.state());
  }

  // Commit the changes
  writer_lock.Commit();

  // Verify that the data is visible
  {
    TableMetadataLock reader_lock(table.get(), TableMetadataLock::READ);
    ASSERT_EQ("foo", reader_lock.data().pb.name());
    ASSERT_EQ("running", reader_lock.data().pb.state_msg());
    ASSERT_EQ(SysTablesEntryPB::RUNNING, reader_lock.data().pb.state());
  }
}

class TestTabletLoader : public TabletVisitor {
 public:
  TestTabletLoader() {}
  ~TestTabletLoader() { Reset(); }

  void Reset() {
    for (TabletInfo* ti : tablets) {
      ti->Release();
    }
    tablets.clear();
  }

  virtual Status Visit(const std::string& tablet_id, const SysTabletsEntryPB& metadata) OVERRIDE {
    // Setup the tablet info
    TabletInfo *tablet = new TabletInfo(nullptr, tablet_id);
    TabletMetadataLock l(tablet, TabletMetadataLock::WRITE);
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    tablet->AddRef();
    tablets.push_back(tablet);
    return Status::OK();
  }

  vector<TabletInfo *> tablets;
};

// Create a new TabletInfo. The object is in uncommitted
// state.
static TabletInfo *CreateTablet(TableInfo *table,
                                const string& tablet_id,
                                const string& start_key,
                                const string& end_key) {
  TabletInfo *tablet = new TabletInfo(table, tablet_id);
  TabletMetadataLock l(tablet, TabletMetadataLock::WRITE);
  l.mutable_data()->pb.set_state(SysTabletsEntryPB::PREPARING);
  l.mutable_data()->pb.mutable_partition()->set_partition_key_start(start_key);
  l.mutable_data()->pb.mutable_partition()->set_partition_key_end(end_key);
  l.mutable_data()->pb.set_table_id(table->id());
  l.Commit();
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
    TabletMetadataLock l1(tablet1.get(), TabletMetadataLock::WRITE);
    TabletMetadataLock l2(tablet2.get(), TabletMetadataLock::WRITE);
    ASSERT_OK(sys_catalog->AddTablets(tablets));
    l1.Commit();
    l2.Commit();

    ASSERT_OK(sys_catalog->Visit(loader.get()));
    ASSERT_EQ(2 + master_->NumSystemTables(), loader->tablets.size());
    ASSERT_TRUE(MetadatasEqual(tablet1.get(), loader->tablets[0]));
    ASSERT_TRUE(MetadatasEqual(tablet2.get(), loader->tablets[1]));
  }

  // Update tablet1
  {
    std::vector<TabletInfo*> tablets;
    tablets.push_back(tablet1.get());

    TabletMetadataLock l1(tablet1.get(), TabletMetadataLock::WRITE);
    l1.mutable_data()->pb.set_state(SysTabletsEntryPB::RUNNING);
    ASSERT_OK(sys_catalog->UpdateTablets(tablets));
    l1.Commit();

    loader->Reset();
    ASSERT_OK(sys_catalog->Visit(loader.get()));
    ASSERT_EQ(2 + master_->NumSystemTables(), loader->tablets.size());
    ASSERT_TRUE(MetadatasEqual(tablet1.get(), loader->tablets[0]));
    ASSERT_TRUE(MetadatasEqual(tablet2.get(), loader->tablets[1]));
  }

  // Add tablet3 and Update tablet1 and tablet2
  {
    std::vector<TabletInfo *> to_add;
    std::vector<TabletInfo *> to_update;

    TabletMetadataLock l3(tablet3.get(), TabletMetadataLock::WRITE);
    to_add.push_back(tablet3.get());
    to_update.push_back(tablet1.get());
    to_update.push_back(tablet2.get());

    TabletMetadataLock l1(tablet1.get(), TabletMetadataLock::WRITE);
    l1.mutable_data()->pb.set_state(SysTabletsEntryPB::REPLACED);
    TabletMetadataLock l2(tablet2.get(), TabletMetadataLock::WRITE);
    l2.mutable_data()->pb.set_state(SysTabletsEntryPB::RUNNING);

    loader->Reset();
    ASSERT_OK(sys_catalog->AddAndUpdateTablets(to_add, to_update));

    l1.Commit();
    l2.Commit();
    l3.Commit();

    ASSERT_OK(sys_catalog->Visit(loader.get()));
    ASSERT_EQ(3 + master_->NumSystemTables(), loader->tablets.size());
    ASSERT_TRUE(MetadatasEqual(tablet1.get(), loader->tablets[0]));
    ASSERT_TRUE(MetadatasEqual(tablet2.get(), loader->tablets[1]));
    ASSERT_TRUE(MetadatasEqual(tablet3.get(), loader->tablets[2]));
  }

  // Delete tablet1 and tablet3 tablets
  {
    std::vector<TabletInfo*> tablets;
    tablets.push_back(tablet1.get());
    tablets.push_back(tablet3.get());

    loader->Reset();
    ASSERT_OK(sys_catalog->DeleteTablets(tablets));
    ASSERT_OK(sys_catalog->Visit(loader.get()));
    ASSERT_EQ(1 + master_->NumSystemTables(), loader->tablets.size());
    ASSERT_TRUE(MetadatasEqual(tablet2.get(), loader->tablets[0]));
  }
}

// Verify that data mutations are not available from metadata() until commit.
TEST_F(SysCatalogTest, TestTabletInfoCommit) {
  scoped_refptr<TabletInfo> tablet(new TabletInfo(nullptr, "123"));

  // Mutate the tablet, the changes should not be visible
  TabletMetadataLock l(tablet.get(), TabletMetadataLock::WRITE);
  PartitionPB* partition = l.mutable_data()->pb.mutable_partition();
  partition->set_partition_key_start("a");
  partition->set_partition_key_end("b");
  l.mutable_data()->set_state(SysTabletsEntryPB::RUNNING, "running");
  {
    // Changes shouldn't be visible, and lock should still be
    // acquired even though the mutation is under way.
    TabletMetadataLock read_lock(tablet.get(), TabletMetadataLock::READ);
    ASSERT_NE("a", read_lock.data().pb.partition().partition_key_start());
    ASSERT_NE("b", read_lock.data().pb.partition().partition_key_end());
    ASSERT_NE("running", read_lock.data().pb.state_msg());
    ASSERT_NE(SysTabletsEntryPB::RUNNING,
              read_lock.data().pb.state());
  }

  // Commit the changes
  l.Commit();

  // Verify that the data is visible
  {
    TabletMetadataLock read_lock(tablet.get(), TabletMetadataLock::READ);
    ASSERT_EQ("a", read_lock.data().pb.partition().partition_key_start());
    ASSERT_EQ("b", read_lock.data().pb.partition().partition_key_end());
    ASSERT_EQ("running", read_lock.data().pb.state_msg());
    ASSERT_EQ(SysTabletsEntryPB::RUNNING,
              read_lock.data().pb.state());
  }
}

class TestClusterConfigLoader : public ClusterConfigVisitor {
 public:
  TestClusterConfigLoader() {}
  ~TestClusterConfigLoader() { Reset(); }

  virtual Status Visit(
      const std::string& fake_id, const SysClusterConfigEntryPB& metadata) OVERRIDE {
    CHECK(!config_info) << "We either got multiple config_info entries, or we didn't Reset()";
    config_info = new ClusterConfigInfo();
    ClusterConfigMetadataLock l(config_info, ClusterConfigMetadataLock::WRITE);
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
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
    ClusterConfigMetadataLock l(loader->config_info, ClusterConfigMetadataLock::READ);
    ASSERT_EQ(l.data().pb.version(), 0);
    ASSERT_EQ(l.data().pb.replication_info().live_replicas().placement_blocks_size(), 0);
  }

  // Test modifications directly through the Sys catalog API.

  // Create a config_info block.
  scoped_refptr<ClusterConfigInfo> config_info(new ClusterConfigInfo());
  {
    ClusterConfigMetadataLock l(config_info.get(), ClusterConfigMetadataLock::WRITE);
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
    ASSERT_OK(sys_catalog->UpdateClusterConfigInfo(config_info.get()));
    l.Commit();
  }

  // Check data from sys_catalog.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_TRUE(loader->config_info);
  ASSERT_TRUE(MetadatasEqual(config_info.get(), loader->config_info));

  {
    ClusterConfigMetadataLock l(config_info.get(), ClusterConfigMetadataLock::WRITE);
    auto pb = l.mutable_data()
                  ->pb.mutable_replication_info()
                  ->mutable_live_replicas()
                  ->mutable_placement_blocks(0);
    auto cloud_info = pb->mutable_cloud_info();
    // Update some config_info info.
    cloud_info->set_placement_cloud("cloud2");
    pb->set_min_num_replicas(200);
    // Update it in the sys_catalog.
    ASSERT_OK(sys_catalog->UpdateClusterConfigInfo(config_info.get()));
    l.Commit();
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
    ClusterConfigMetadataLock l(config_info.get(), ClusterConfigMetadataLock::WRITE);
    auto pb = l.mutable_data()
                  ->pb.mutable_replication_info()
                  ->mutable_live_replicas()
                  ->mutable_placement_blocks(0);
    pb->set_min_num_replicas(300);

    ChangeMasterClusterConfigRequestPB req;
    *req.mutable_cluster_config() = l.mutable_data()->pb;
    ChangeMasterClusterConfigResponsePB resp;
    ASSERT_OK(master_->catalog_manager()->SetClusterConfig(&req, &resp));
    l.Commit();
  }

  // Confirm the in memory state does not match the config we get from the CatalogManager API, due
  // to version mismatch.
  ASSERT_OK(master_->catalog_manager()->GetClusterConfig(&config));
  {
    ClusterConfigMetadataLock l(config_info.get(), ClusterConfigMetadataLock::READ);
    ASSERT_FALSE(PbEquals(l.data().pb, config));
    ASSERT_EQ(l.data().pb.version(), 0);
    ASSERT_EQ(config.version(), 1);
    ASSERT_TRUE(PbEquals(
        l.data().pb.replication_info().live_replicas().placement_blocks(0),
        config.replication_info().live_replicas().placement_blocks(0)));
  }

  // Reload the data again and check that it matches expectations.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_TRUE(loader->config_info);
  {
    ClusterConfigMetadataLock l(loader->config_info, ClusterConfigMetadataLock::READ);
    ASSERT_TRUE(PbEquals(l.data().pb, config));
    ASSERT_TRUE(l.data().pb.has_version());
    ASSERT_EQ(l.data().pb.version(), 1);
    ASSERT_EQ(l.data().pb.replication_info().live_replicas().placement_blocks_size(), 1);
  }
}

class TestNamespaceLoader : public NamespaceVisitor {
 public:
  TestNamespaceLoader() {}
  ~TestNamespaceLoader() { Reset(); }

  void Reset() {
    for (NamespaceInfo* ni : namespaces) {
      ni->Release();
    }
    namespaces.clear();
  }

  virtual Status Visit(const std::string& ns_id, const SysNamespaceEntryPB& metadata) OVERRIDE {
    // Setup the namespace info
    NamespaceInfo* const ns = new NamespaceInfo(ns_id);
    NamespaceMetadataLock l(ns, NamespaceMetadataLock::WRITE);
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

  // 1. CHECK DEFAULT NAMESPACE
  unique_ptr<TestNamespaceLoader> loader(new TestNamespaceLoader());
  ASSERT_OK(sys_catalog->Visit(loader.get()));

  ASSERT_EQ(1 + kNumSystemNamespaces, loader->namespaces.size());

  scoped_refptr<NamespaceInfo> universe_ns(new NamespaceInfo(kDefaultNamespaceId));
  {
    NamespaceMetadataLock l(universe_ns.get(), NamespaceMetadataLock::WRITE);
    l.mutable_data()->pb.set_name(kDefaultNamespaceName);
    l.Commit();
  }

  ASSERT_TRUE(MetadatasEqual(universe_ns.get(), loader->namespaces[0]));

  // 2. CHECK ADD_NAMESPACE
  // Create new namespace.
  scoped_refptr<NamespaceInfo> ns(new NamespaceInfo("deadbeafdeadbeafdeadbeafdeadbeaf"));
  {
    NamespaceMetadataLock l(ns.get(), NamespaceMetadataLock::WRITE);
    l.mutable_data()->pb.set_name("test_ns");
    // Add the namespace
    ASSERT_OK(sys_catalog->AddNamespace(ns.get()));

    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(2 + kNumSystemNamespaces, loader->namespaces.size());
  ASSERT_TRUE(MetadatasEqual(universe_ns.get(), loader->namespaces[0]));
  ASSERT_TRUE(MetadatasEqual(ns.get(), loader->namespaces[1]));

  // 3. CHECK UPDATE_NAMESPACE
  // Update the namespace
  {
    NamespaceMetadataLock l(ns.get(), NamespaceMetadataLock::WRITE);
    l.mutable_data()->pb.set_name("test_ns_new_name");
    ASSERT_OK(sys_catalog->UpdateNamespace(ns.get()));
    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(2 + kNumSystemNamespaces, loader->namespaces.size());
  ASSERT_TRUE(MetadatasEqual(universe_ns.get(), loader->namespaces[0]));
  ASSERT_TRUE(MetadatasEqual(ns.get(), loader->namespaces[1]));

  // 4. CHECK DELETE_NAMESPACE
  // Delete the namespace
  ASSERT_OK(sys_catalog->DeleteNamespace(ns.get()));

  // Verify the result.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(1 + kNumSystemNamespaces, loader->namespaces.size());
  ASSERT_TRUE(MetadatasEqual(universe_ns.get(), loader->namespaces[0]));
}

// Verify that data mutations are not available from metadata() until commit.
TEST_F(SysCatalogTest, TestNamespaceInfoCommit) {
  scoped_refptr<NamespaceInfo> ns(new NamespaceInfo("deadbeafdeadbeafdeadbeafdeadbeaf"));

  // Mutate the namespace, under the write lock.
  NamespaceMetadataLock writer_lock(ns.get(), NamespaceMetadataLock::WRITE);
  writer_lock.mutable_data()->pb.set_name("foo");

  // Changes should not be visible to a reader.
  // The reader can still lock for read, since readers don't block
  // writers in the RWC lock.
  {
    NamespaceMetadataLock reader_lock(ns.get(), NamespaceMetadataLock::READ);
    ASSERT_NE("foo", reader_lock.data().name());
  }

  // Commit the changes
  writer_lock.Commit();

  // Verify that the data is visible
  {
    NamespaceMetadataLock reader_lock(ns.get(), NamespaceMetadataLock::READ);
    ASSERT_EQ("foo", reader_lock.data().name());
  }
}

} // namespace master
} // namespace yb
