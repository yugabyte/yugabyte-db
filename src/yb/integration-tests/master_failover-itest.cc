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

#include <string>
#include <vector>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/client-internal.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"

#include "yb/common/schema.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/util.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/util/net/net_util.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"

using namespace std::literals;

namespace yb {

// Note: this test needs to be in the client namespace in order for
// YBClient::Data class methods to be visible via FRIEND_TEST macro.
namespace client {

const int kNumTabletServerReplicas = 3;

using std::shared_ptr;
using std::string;
using std::vector;
using client::YBTableName;

class MasterFailoverTest : public YBTest {
 public:
  enum CreateTableMode {
    kWaitForCreate = 0,
    kNoWaitForCreate = 1
  };

  MasterFailoverTest() {
    opts_.master_rpc_ports = { 0, 0, 0 };
    opts_.num_masters = num_masters_ = opts_.master_rpc_ports.size();
    opts_.num_tablet_servers = kNumTabletServerReplicas;

    // Reduce various timeouts below as to make the detection of
    // leader master failures (specifically, failures as result of
    // long pauses) more rapid.

    // Set the TS->master heartbeat timeout to 1 second (down from 15 seconds).
    opts_.extra_tserver_flags.push_back("--heartbeat_rpc_timeout_ms=1000");
    // Allow one TS heartbeat failure before retrying with back-off (down from 3).
    opts_.extra_tserver_flags.push_back("--heartbeat_max_failures_before_backoff=1");
    // Wait for 500 ms after 'max_consecutive_failed_heartbeats'
    // before trying again (down from 1 second).
    opts_.extra_tserver_flags.push_back("--heartbeat_interval_ms=500");
  }

  void SetUp() override {
    YBTest::SetUp();
    ASSERT_NO_FATALS(RestartCluster());
  }

  void TearDown() override {
    client_.reset();
    if (cluster_) {
      cluster_->Shutdown();
    }
    YBTest::TearDown();
  }

  void RestartCluster() {
    if (cluster_) {
      cluster_->Shutdown();
      cluster_.reset();
    }
    cluster_.reset(new ExternalMiniCluster(opts_));
    ASSERT_OK(cluster_->Start());
    client_ = ASSERT_RESULT(cluster_->CreateClient());
  }

  Status CreateTable(const YBTableName& table_name, CreateTableMode mode) {
    YBSchema schema;
    YBSchemaBuilder b;
    b.AddColumn("key")->Type(INT32)->NotNull()->PrimaryKey();
    b.AddColumn("int_val")->Type(INT32)->NotNull();
    b.AddColumn("string_val")->Type(STRING)->NotNull();
    CHECK_OK(b.Build(&schema));
    gscoped_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
    return table_creator->table_name(table_name)
        .schema(&schema)
        .timeout(MonoDelta::FromSeconds(90))
        .wait(mode == kWaitForCreate)
        .Create();
  }

  Status RenameTable(const YBTableName& table_name_orig, const YBTableName& table_name_new) {
    gscoped_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(table_name_orig));
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
  Status OpenTableAndScanner(const YBTableName& table_name) {
    client::TableHandle table;
    RETURN_NOT_OK_PREPEND(table.Open(table_name, client_.get()),
                          "Unable to open table " + table_name.ToString());
    client::TableRange range(table);
    auto it = range.begin();
    if (it != range.end()) {
      ++it;
    }

    return Status::OK();
  }

 protected:
  int num_masters_;
  ExternalMiniClusterOptions opts_;
  gscoped_ptr<ExternalMiniCluster> cluster_;
  std::unique_ptr<YBClient> client_;
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
  ASSERT_OK(cluster_->master(leader_idx)->Pause());
  ScopedResumeExternalDaemon resume_daemon(cluster_->master(leader_idx));

  YBTableName table_name(YQL_DATABASE_CQL, "testCreateTableSync");
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

  YBTableName table_name(YQL_DATABASE_CQL, "testPauseAfterCreateTableIssued");
  LOG(INFO) << "Issuing CreateTable for " << table_name.ToString();
  ASSERT_OK(CreateTable(table_name, kNoWaitForCreate));

  LOG(INFO) << "Pausing leader master";
  ASSERT_OK(cluster_->master(leader_idx)->Pause());
  ScopedResumeExternalDaemon resume_daemon(cluster_->master(leader_idx));

  auto deadline = CoarseMonoClock::Now() + 90s;
  ASSERT_OK(client_->data_->WaitForCreateTableToFinish(client_.get(), table_name, "" /* table_id */,
                                                       deadline));

  ASSERT_OK(OpenTableAndScanner(table_name));
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

  YBTableName table_name(YQL_DATABASE_CQL, "testDeleteTableSync");
  ASSERT_OK(CreateTable(table_name, kWaitForCreate));

  LOG(INFO) << "Pausing leader master";
  ASSERT_OK(cluster_->master(leader_idx)->Pause());
  ScopedResumeExternalDaemon resume_daemon(cluster_->master(leader_idx));

  ASSERT_OK(client_->DeleteTable(table_name));
  shared_ptr<YBTable> table;
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

  YBTableName table_name_orig(YQL_DATABASE_CQL, "testAlterTableSync");
  ASSERT_OK(CreateTable(table_name_orig, kWaitForCreate));

  LOG(INFO) << "Pausing leader master";
  ASSERT_OK(cluster_->master(leader_idx)->Pause());
  ScopedResumeExternalDaemon resume_daemon(cluster_->master(leader_idx));

  YBTableName table_name_new(YQL_DATABASE_CQL, "testAlterTableSyncRenamed");
  ASSERT_OK(RenameTable(table_name_orig, table_name_new));
  shared_ptr<YBTable> table;
  ASSERT_OK(client_->OpenTable(table_name_new, &table));

  Status s = client_->OpenTable(table_name_orig, &table);
  ASSERT_TRUE(s.IsNotFound());
}

}  // namespace client
}  // namespace yb
