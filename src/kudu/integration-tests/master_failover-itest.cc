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

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "kudu/client/client.h"
#include "kudu/client/client-internal.h"
#include "kudu/common/schema.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/strings/util.h"
#include "kudu/integration-tests/external_mini_cluster.h"
#include "kudu/util/net/net_util.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/test_util.h"

namespace kudu {

// Note: this test needs to be in the client namespace in order for
// KuduClient::Data class methods to be visible via FRIEND_TEST macro.
namespace client {

const int kNumTabletServerReplicas = 3;

using sp::shared_ptr;
using std::string;
using std::vector;

class MasterFailoverTest : public KuduTest {
 public:
  enum CreateTableMode {
    kWaitForCreate = 0,
    kNoWaitForCreate = 1
  };

  MasterFailoverTest() {
    opts_.master_rpc_ports = { 11010, 11011, 11012 };
    opts_.num_masters = num_masters_ = opts_.master_rpc_ports.size();
    opts_.num_tablet_servers = kNumTabletServerReplicas;

    // Reduce various timeouts below as to make the detection of
    // leader master failures (specifically, failures as result of
    // long pauses) more rapid.

    // Set max missed heartbeats periods to 1.0 (down from 3.0).
    opts_.extra_master_flags.push_back("--leader_failure_max_missed_heartbeat_periods=1.0");

    // Set the TS->master heartbeat timeout to 1 second (down from 15 seconds).
    opts_.extra_tserver_flags.push_back("--heartbeat_rpc_timeout_ms=1000");
    // Allow one TS heartbeat failure before retrying with back-off (down from 3).
    opts_.extra_tserver_flags.push_back("--heartbeat_max_failures_before_backoff=1");
    // Wait for 500 ms after 'max_consecutive_failed_heartbeats'
    // before trying again (down from 1 second).
    opts_.extra_tserver_flags.push_back("--heartbeat_interval_ms=500");
  }

  virtual void SetUp() OVERRIDE {
    KuduTest::SetUp();
    ASSERT_NO_FATAL_FAILURE(RestartCluster());
  }

  virtual void TearDown() OVERRIDE {
    if (cluster_) {
      cluster_->Shutdown();
    }
    KuduTest::TearDown();
  }

  void RestartCluster() {
    if (cluster_) {
      cluster_->Shutdown();
      cluster_.reset();
    }
    cluster_.reset(new ExternalMiniCluster(opts_));
    ASSERT_OK(cluster_->Start());
    KuduClientBuilder builder;
    ASSERT_OK(cluster_->CreateClient(builder, &client_));
  }

  Status CreateTable(const std::string& table_name, CreateTableMode mode) {
    KuduSchema schema;
    KuduSchemaBuilder b;
    b.AddColumn("key")->Type(KuduColumnSchema::INT32)->NotNull()->PrimaryKey();
    b.AddColumn("int_val")->Type(KuduColumnSchema::INT32)->NotNull();
    b.AddColumn("string_val")->Type(KuduColumnSchema::STRING)->NotNull();
    CHECK_OK(b.Build(&schema));
    gscoped_ptr<KuduTableCreator> table_creator(client_->NewTableCreator());
    return table_creator->table_name(table_name)
        .schema(&schema)
        .timeout(MonoDelta::FromSeconds(90))
        .wait(mode == kWaitForCreate)
        .Create();
  }

  Status RenameTable(const std::string& table_name_orig, const std::string& table_name_new) {
    gscoped_ptr<KuduTableAlterer> table_alterer(client_->NewTableAlterer(table_name_orig));
    return table_alterer
      ->RenameTo(table_name_new)
      ->timeout(MonoDelta::FromSeconds(90))
      ->wait(true)
      ->Alter();
  }

  // Test that we can get the table location information from the
  // master and then open scanners on the tablet server. This involves
  // sending RPCs to both the master and the tablet servers and
  // requires that the table and tablet exist both on the masters and
  // the tablet servers.
  Status OpenTableAndScanner(const std::string& table_name) {
    shared_ptr<KuduTable> table;
    RETURN_NOT_OK_PREPEND(client_->OpenTable(table_name, &table),
                          "Unable to open table " + table_name);
    KuduScanner scanner(table.get());
    RETURN_NOT_OK_PREPEND(scanner.SetProjectedColumns(vector<string>()),
                          "Unable to open an empty projection on " + table_name);
    RETURN_NOT_OK_PREPEND(scanner.Open(),
                          "Unable to open scanner on " + table_name);
    return Status::OK();
  }

 protected:
  int num_masters_;
  ExternalMiniClusterOptions opts_;
  gscoped_ptr<ExternalMiniCluster> cluster_;
  shared_ptr<KuduClient> client_;
};

// Test that synchronous CreateTable (issue CreateTable call and then
// wait until the table has been created) works even when the original
// leader master has been paused.
//
// Temporarily disabled since multi-master isn't supported yet.
// This test fails as of KUDU-1138, since the tablet servers haven't
// registered with the follower master, and thus it's likely to deny
// the CreateTable request thinking there are no TS available.
TEST_F(MasterFailoverTest, DISABLED_TestCreateTableSync) {
  if (!AllowSlowTests()) {
    LOG(INFO) << "This test can only be run in slow mode.";
    return;
  }

  int leader_idx;
  ASSERT_OK(cluster_->GetLeaderMasterIndex(&leader_idx));

  LOG(INFO) << "Pausing leader master";
  cluster_->master(leader_idx)->Pause();
  ScopedResumeExternalDaemon resume_daemon(cluster_->master(leader_idx));

  string table_name = "testCreateTableSync";
  ASSERT_OK(CreateTable(table_name, kWaitForCreate));
  ASSERT_OK(OpenTableAndScanner(table_name));
}

// Test that we can issue a CreateTable call, pause the leader master
// immediately after, then verify that the table has been created on
// the newly elected leader master.
//
// TODO enable this test once flakiness issues are worked out and
// eliminated on test machines.
TEST_F(MasterFailoverTest, DISABLED_TestPauseAfterCreateTableIssued) {
  if (!AllowSlowTests()) {
    LOG(INFO) << "This test can only be run in slow mode.";
    return;
  }

  int leader_idx;
  ASSERT_OK(cluster_->GetLeaderMasterIndex(&leader_idx));

  string table_id = "testPauseAfterCreateTableIssued";
  LOG(INFO) << "Issuing CreateTable for " << table_id;
  ASSERT_OK(CreateTable(table_id, kNoWaitForCreate));

  LOG(INFO) << "Pausing leader master";
  cluster_->master(leader_idx)->Pause();
  ScopedResumeExternalDaemon resume_daemon(cluster_->master(leader_idx));

  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromSeconds(90));
  ASSERT_OK(client_->data_->WaitForCreateTableToFinish(client_.get(),
                                                       table_id, deadline));

  ASSERT_OK(OpenTableAndScanner(table_id));
}

// Test the scenario where we create a table, pause the leader master,
// and then issue the DeleteTable call: DeleteTable should go to the newly
// elected leader master and succeed.
TEST_F(MasterFailoverTest, TestDeleteTableSync) {
  if (!AllowSlowTests()) {
    LOG(INFO) << "This test can only be run in slow mode.";
    return;
  }

  int leader_idx;

  ASSERT_OK(cluster_->GetLeaderMasterIndex(&leader_idx));

  string table_name = "testDeleteTableSync";
  ASSERT_OK(CreateTable(table_name, kWaitForCreate));

  LOG(INFO) << "Pausing leader master";
  cluster_->master(leader_idx)->Pause();
  ScopedResumeExternalDaemon resume_daemon(cluster_->master(leader_idx));

  ASSERT_OK(client_->DeleteTable(table_name));
  shared_ptr<KuduTable> table;
  Status s = client_->OpenTable(table_name, &table);
  ASSERT_TRUE(s.IsNotFound());
}

// Test the scenario where we create a table, pause the leader master,
// and then issue the AlterTable call renaming a table: AlterTable
// should go to the newly elected leader master and succeed, renaming
// the table.
//
// TODO: Add an equivalent async test. Add a test for adding and/or
// renaming a column in a table.
TEST_F(MasterFailoverTest, TestRenameTableSync) {
  if (!AllowSlowTests()) {
    LOG(INFO) << "This test can only be run in slow mode.";
    return;
  }

  int leader_idx;

  ASSERT_OK(cluster_->GetLeaderMasterIndex(&leader_idx));

  string table_name_orig = "testAlterTableSync";
  ASSERT_OK(CreateTable(table_name_orig, kWaitForCreate));

  LOG(INFO) << "Pausing leader master";
  cluster_->master(leader_idx)->Pause();
  ScopedResumeExternalDaemon resume_daemon(cluster_->master(leader_idx));

  string table_name_new = "testAlterTableSyncRenamed";
  ASSERT_OK(RenameTable(table_name_orig, table_name_new));
  shared_ptr<KuduTable> table;
  ASSERT_OK(client_->OpenTable(table_name_new, &table));

  Status s = client_->OpenTable(table_name_orig, &table);
  ASSERT_TRUE(s.IsNotFound());
}

} // namespace client
} // namespace kudu
