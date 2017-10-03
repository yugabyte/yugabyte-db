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

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <vector>

#include "kudu/client/client.h"
#include "kudu/common/schema.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/integration-tests/mini_cluster.h"
#include "kudu/master/catalog_manager.h"
#include "kudu/master/master.h"
#include "kudu/master/mini_master.h"
#include "kudu/util/test_util.h"

using std::vector;

namespace kudu {
namespace master {

using client::KuduClient;
using client::KuduClientBuilder;
using client::KuduColumnSchema;
using client::KuduScanner;
using client::KuduSchema;
using client::KuduSchemaBuilder;
using client::KuduTable;
using client::KuduTableCreator;
using client::sp::shared_ptr;

const char * const kTableId1 = "testMasterReplication-1";
const char * const kTableId2 = "testMasterReplication-2";

const int kNumTabletServerReplicas = 3;

class MasterReplicationTest : public KuduTest {
 public:
  MasterReplicationTest() {
    // Hard-coded ports for the masters. This is safe, as this unit test
    // runs under a resource lock (see CMakeLists.txt in this directory).
    // TODO we should have a generic method to obtain n free ports.
    opts_.master_rpc_ports = { 11010, 11011, 11012 };

    opts_.num_masters = num_masters_ = opts_.master_rpc_ports.size();
    opts_.num_tablet_servers = kNumTabletServerReplicas;
  }

  virtual void SetUp() OVERRIDE {
    KuduTest::SetUp();
    cluster_.reset(new MiniCluster(env_.get(), opts_));
    ASSERT_OK(cluster_->Start());
    ASSERT_OK(cluster_->WaitForTabletServerCount(kNumTabletServerReplicas));
  }

  virtual void TearDown() OVERRIDE {
    if (cluster_) {
      cluster_->Shutdown();
      cluster_.reset();
    }
    KuduTest::TearDown();
  }

  Status RestartCluster() {
    cluster_->Shutdown();
    RETURN_NOT_OK(cluster_->Start());
    RETURN_NOT_OK(cluster_->WaitForTabletServerCount(kNumTabletServerReplicas));
    return Status::OK();
  }

  // This method is meant to be run in a separate thread.
  void StartClusterDelayed(int64_t micros) {
    LOG(INFO) << "Sleeping for "  << micros << " micro seconds...";
    SleepFor(MonoDelta::FromMicroseconds(micros));
    LOG(INFO) << "Attempting to start the cluster...";
    CHECK_OK(cluster_->Start());
    CHECK_OK(cluster_->WaitForTabletServerCount(kNumTabletServerReplicas));
  }

  void ListMasterServerAddrs(vector<string>* out) {
    for (int i = 0; i < num_masters_; i++) {
      out->push_back(cluster_->mini_master(i)->bound_rpc_addr_str());
    }
  }

  Status CreateClient(shared_ptr<KuduClient>* out) {
    KuduClientBuilder builder;
    for (int i = 0; i < num_masters_; i++) {
      if (!cluster_->mini_master(i)->master()->IsShutdown()) {
        builder.add_master_server_addr(cluster_->mini_master(i)->bound_rpc_addr_str());
      }
    }
    return builder.Build(out);
  }


  Status CreateTable(const shared_ptr<KuduClient>& client,
                     const std::string& table_name) {
    KuduSchema schema;
    KuduSchemaBuilder b;
    b.AddColumn("key")->Type(KuduColumnSchema::INT32)->NotNull()->PrimaryKey();
    b.AddColumn("int_val")->Type(KuduColumnSchema::INT32)->NotNull();
    b.AddColumn("string_val")->Type(KuduColumnSchema::STRING)->NotNull();
    CHECK_OK(b.Build(&schema));
    gscoped_ptr<KuduTableCreator> table_creator(client->NewTableCreator());
    return table_creator->table_name(table_name)
        .schema(&schema)
        .Create();
  }

  void VerifyTableExists(const std::string& table_id) {
    LOG(INFO) << "Verifying that " << table_id << " exists on leader..";
    ASSERT_TRUE(cluster_->leader_mini_master()->master()
                ->catalog_manager()->TableNameExists(table_id));
  }

 protected:
  int num_masters_;
  MiniClusterOptions opts_;
  gscoped_ptr<MiniCluster> cluster_;
};

// Basic test. Verify that:
//
// 1) We can start multiple masters in a distributed configuration and
// that the clients and tablet servers can connect to the leader
// master.
//
// 2) We can create a table (using the standard client APIs) on the
// the leader and ensure that the appropriate table/tablet info is
// replicated to the newly elected leader.
TEST_F(MasterReplicationTest, TestSysTablesReplication) {
  shared_ptr<KuduClient> client;

  // Create the first table.
  ASSERT_OK(CreateClient(&client));
  ASSERT_OK(CreateTable(client, kTableId1));

  // TODO: once fault tolerant DDL is in, remove the line below.
  ASSERT_OK(CreateClient(&client));

  ASSERT_OK(cluster_->WaitForTabletServerCount(kNumTabletServerReplicas));

  // Repeat the same for the second table.
  ASSERT_OK(CreateTable(client, kTableId2));
  ASSERT_NO_FATAL_FAILURE(VerifyTableExists(kTableId2));
}

// When all masters are down, test that we can timeout the connection
// attempts after a specified deadline.
TEST_F(MasterReplicationTest, TestTimeoutWhenAllMastersAreDown) {
  vector<string> master_addrs;
  ListMasterServerAddrs(&master_addrs);

  cluster_->Shutdown();

  shared_ptr<KuduClient> client;
  KuduClientBuilder builder;
  builder.master_server_addrs(master_addrs);
  builder.default_rpc_timeout(MonoDelta::FromMilliseconds(100));
  Status s = builder.Build(&client);
  EXPECT_TRUE(!s.ok());
  EXPECT_TRUE(s.IsTimedOut());

  // We need to reset 'cluster_' so that TearDown() can run correctly.
  cluster_.reset();
}

// Shut the cluster down, start initializing the client, and then
// bring the cluster back up during the initialization (but before the
// timeout can elapse).
TEST_F(MasterReplicationTest, TestCycleThroughAllMasters) {
  vector<string> master_addrs;
  ListMasterServerAddrs(&master_addrs);

  // Shut the cluster down and ...
  cluster_->Shutdown();
  // ... start the cluster after a delay.
  scoped_refptr<kudu::Thread> start_thread;
  ASSERT_OK(Thread::Create("TestCycleThroughAllMasters", "start_thread",
                                  &MasterReplicationTest::StartClusterDelayed,
                                  this,
                                  100 * 1000, // start after 100 millis.
                                  &start_thread));

  // Verify that the client doesn't give up even though the entire
  // cluster is down for 100 milliseconds.
  shared_ptr<KuduClient> client;
  KuduClientBuilder builder;
  builder.master_server_addrs(master_addrs);
  builder.default_admin_operation_timeout(MonoDelta::FromSeconds(15));
  EXPECT_OK(builder.Build(&client));

  ASSERT_OK(ThreadJoiner(start_thread.get()).Join());
}

} // namespace master
} // namespace kudu
