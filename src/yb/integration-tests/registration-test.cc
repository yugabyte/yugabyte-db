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

#include <memory>
#include <string>

#include "yb/util/flags.h"
#include <gtest/gtest.h>

#include "yb/client/yb_table_name.h"

#include "yb/common/schema.h"

#include "yb/fs/fs_manager.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/mini_cluster_utils.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/master_client.pb.h"
#include "yb/master/mini_master.h"
#include "yb/master/ts_descriptor.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/curl_util.h"
#include "yb/util/faststring.h"
#include "yb/util/metrics.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

DECLARE_int32(heartbeat_interval_ms);
DECLARE_int32(yb_num_shards_per_tserver);
DECLARE_bool(enable_ysql);
DECLARE_int32(TEST_mini_cluster_registration_wait_time_sec);

METRIC_DECLARE_counter(rows_inserted);

namespace yb {

using std::vector;
using std::shared_ptr;
using std::string;
using master::TSDescriptor;
using master::TabletLocationsPB;
using tserver::MiniTabletServer;
using client::YBTableName;

// Tests for the Tablet Server registering with the Master,
// and the master maintaining the tablet descriptor.
class RegistrationTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  RegistrationTest()
      : schema_({ ColumnSchema("c1", DataType::UINT32, ColumnKind::HASH)}) {
  }

  void SetUp() override {
    // Make heartbeats faster to speed test runtime.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_heartbeat_interval_ms) = 10;

    // To prevent automatic creation of the transaction status table.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql) = false;

    YBMiniClusterTestBase::SetUp();

    cluster_.reset(new MiniCluster(MiniClusterOptions()));
    ASSERT_OK(cluster_->Start());
  }

  void DoTearDown() override {
    cluster_->Shutdown();
  }

  void CheckTabletServersPage() {
    EasyCurl c;
    faststring buf;
    std::string addr = yb::ToString(cluster_->mini_master()->bound_http_addr());
    ASSERT_OK(c.FetchURL(strings::Substitute("http://$0/tablet-servers", addr), &buf));

    // Should include the TS UUID
    string expected_uuid =
      cluster_->mini_tablet_server(0)->server()->instance_pb().permanent_uuid();
    ASSERT_STR_CONTAINS(buf.ToString(), expected_uuid);
  }

 protected:
  Schema schema_;
};

TEST_F(RegistrationTest, TestTSRegisters) {
  DontVerifyClusterBeforeNextTearDown();
  // Wait for the TS to register.
  auto descs = ASSERT_RESULT(cluster_->WaitForTabletServerCount(1));
  ASSERT_EQ(1, descs.size());

  // Verify that the registration is sane.
  auto reg = descs[0]->GetRegistration();
  {
    SCOPED_TRACE(reg.ShortDebugString());
    ASSERT_EQ(reg.ShortDebugString().find("0.0.0.0"), string::npos)
      << "Should not include wildcards in registration";
  }

  ASSERT_NO_FATALS(CheckTabletServersPage());

  // Restart the master, so it loses the descriptor, and ensure that the
  // heartbeater thread handles re-registering.
  ASSERT_OK(cluster_->mini_master()->Restart());

  ASSERT_OK(cluster_->WaitForTabletServerCount(1));

  // TODO: when the instance ID / sequence number stuff is implemented,
  // restart the TS and ensure that it re-registers with the newer sequence
  // number.
}

// Test starting multiple tablet servers and ensuring they both register with the master.
TEST_F(RegistrationTest, TestMultipleTS) {
  DontVerifyClusterBeforeNextTearDown();
  ASSERT_OK(cluster_->WaitForTabletServerCount(1));
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(2));
}

// TODO: this doesn't belong under "RegistrationTest" - rename this file
// to something more appropriate - doesn't seem worth having separate
// whole test suites for registration, tablet reports, etc.
TEST_F(RegistrationTest, TestTabletReports) {
  // For debugging, try running with --test-args --vmodule=sys_catalog_writer=2,tablet=3.
  string tablet_id_1;
  string tablet_id_2;
  string table_id_1;
  // Speed up test by having low number of tablets.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_num_shards_per_tserver) = 2;

  ASSERT_OK(cluster_->WaitForTabletServerCount(1));

  MiniTabletServer* ts = cluster_->mini_tablet_server(0);

  auto GetCatalogMetric = [&](CounterPrototype& prototype) -> int64_t {
    auto tablet_peer = cluster_->mini_master()->tablet_peer();
    auto tablet_result = tablet_peer->shared_tablet_safe();
    if (!tablet_result.ok()) {
      LOG(WARNING) << "Failed to get tablet: " << tablet_result.status();
      return 0;
    }
    auto tablet = *tablet_result;
    auto metrics = tablet->GetTabletMetricsEntity();
    return prototype.Instantiate(metrics)->value();
  };
  auto before_rows_inserted = GetCatalogMetric(METRIC_rows_inserted);
  LOG(INFO) << "Begin calculating sys catalog rows inserted";

  // Add a tablet, make sure it reports itself.
  CreateTabletForTesting(
      cluster_.get(),
      YBTableName(YQL_DATABASE_CQL, "my_keyspace", "fake-table"),
      schema_,
      &tablet_id_1,
      &table_id_1);

  TabletLocationsPB locs;
  ASSERT_OK(cluster_->WaitForReplicaCount(tablet_id_1, 1, &locs));
  ASSERT_EQ(1, locs.replicas_size());
  LOG(INFO) << "Tablet successfully reported on " << locs.replicas(0).ts_info().permanent_uuid();

  LOG(INFO) << "Finish calculating sys catalog rows inserted";
  auto after_create_rows_inserted = GetCatalogMetric(METRIC_rows_inserted);
  // Check that we inserted the right number of rows for the first table:
  // - 2 for the namespace
  // - 2 for the table
  // - 3 * FLAGS_yb_num_shards_per_tserver for the tablets:
  //    PREPARING, first heartbeat, leader election heartbeat
  int64_t expected_rows = 2 + 2 + FLAGS_yb_num_shards_per_tserver * 3;
  EXPECT_EQ(expected_rows, after_create_rows_inserted - before_rows_inserted);

  // Add another tablet, make sure it is reported via incremental.
  Schema schema_copy = Schema(schema_);

  // Record the number of rows before the new table.
  before_rows_inserted = GetCatalogMetric(METRIC_rows_inserted);
  LOG(INFO) << "Begin calculating sys catalog rows inserted (2)";
  CreateTabletForTesting(
      cluster_.get(),
      YBTableName(YQL_DATABASE_CQL, "my_keyspace", "fake-table2"),
      schema_copy,
      &tablet_id_2);
  // Sleep for enough to make sure the TS has plenty of time to re-heartbeat.
  auto sleep_duration_sec = MonoDelta::FromSeconds(RegularBuildVsSanitizers(2, 5));
  SleepFor(sleep_duration_sec);
  LOG(INFO) << "Finish calculating sys catalog rows inserted (2)";
  after_create_rows_inserted = GetCatalogMetric(METRIC_rows_inserted);
  // We expect 3 writes per tablet. and 2 write for the table.
  expected_rows = 2 + FLAGS_yb_num_shards_per_tserver * 3;
  EXPECT_EQ(expected_rows, after_create_rows_inserted - before_rows_inserted);
  ASSERT_OK(cluster_->WaitForReplicaCount(tablet_id_2, 1, &locs));

  // Shut down the whole system, bring it back up, and make sure the tablets
  // are reported.
  ts->Shutdown();
  ASSERT_OK(cluster_->mini_master()->Restart());
  ASSERT_OK(ts->Start(tserver::WaitTabletsBootstrapped::kFalse));
  ASSERT_OK(cluster_->WaitForTabletServerCount(1));

  ASSERT_OK(cluster_->WaitForReplicaCount(tablet_id_1, 1, &locs));
  ASSERT_OK(cluster_->WaitForReplicaCount(tablet_id_2, 1, &locs));
  // Sleep for enough to make sure the TS has plenty of time to re-heartbeat.
  SleepFor(sleep_duration_sec);

  // After restart, check that the tablet reports produced the expected number of writes to the
  // catalog table. We expect two updates per tablet, since both replicas should have increased
  // their term on restart.
  EXPECT_EQ(2 * FLAGS_yb_num_shards_per_tserver, GetCatalogMetric(METRIC_rows_inserted));

  // If we restart just the master, it should not write any data to the catalog, since the
  // tablets themselves are not changing term, etc.
  ASSERT_OK(cluster_->mini_master()->Restart());
  // Sleep for enough to make sure the TS has plenty of time to re-heartbeat.
  ASSERT_OK(cluster_->WaitForTabletServerCount(1));
  SleepFor(sleep_duration_sec);
  EXPECT_EQ(0, GetCatalogMetric(METRIC_rows_inserted));

  // TODO: KUDU-870: once the master supports detecting failed/lost replicas,
  // we should add a test case here which removes or corrupts metadata, restarts
  // the TS, and verifies that the master notices the issue.
}

class RegistrationFailedTest : public YBMiniClusterTestBase<MiniCluster> {
  void SetUp() override {
    // Cause waiting for tservers to register to master to fail.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_mini_cluster_registration_wait_time_sec) = 0;

    YBMiniClusterTestBase::SetUp();
    cluster_.reset(new MiniCluster(MiniClusterOptions()));

    // Test that cluster starting fails gracefully.
    Status s = cluster_->Start();
    ASSERT_NOK(s);
    ASSERT_TRUE(s.IsTimedOut()) << s;
    ASSERT_STR_CONTAINS(s.message().ToBuffer(), "TS(s) never registered with master");
  }
};

TEST_F_EX(RegistrationTest, FailRegister, RegistrationFailedTest) {
  // Do nothing: test happens in RegistrationFailedTest::SetUp.
  // Logs should show "Shutdown when mini cluster is not running".
}

} // namespace yb
