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

#include "yb/integration-tests/cluster_verifier.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/test_workload.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/tserver_admin.proxy.h"

#include "yb/util/size_literals.h"
#include "yb/util/test_util.h"

using std::string;
using std::vector;
using namespace std::literals;

namespace yb {

class TsRecoveryITest : public YBTest {
 public:
  void TearDown() override {
    if (cluster_) cluster_->Shutdown();
    YBTest::TearDown();
  }

 protected:
  void StartCluster(const vector<string>& extra_tserver_flags = vector<string>(),
                    int num_tablet_servers = 1);

  std::unique_ptr<ExternalMiniCluster> cluster_;
};

void TsRecoveryITest::StartCluster(const vector<string>& extra_tserver_flags,
                                   int num_tablet_servers) {
  ExternalMiniClusterOptions opts;
  opts.num_tablet_servers = num_tablet_servers;
  opts.extra_tserver_flags = extra_tserver_flags;
  if (num_tablet_servers < 3) {
    opts.extra_master_flags.push_back("--replication_factor=1");
  }
  cluster_.reset(new ExternalMiniCluster(opts));
  ASSERT_OK(cluster_->Start());
}

// Test that we replay from the recovery directory, if it exists.
TEST_F(TsRecoveryITest, TestCrashDuringLogReplay) {
  const std::string crash_flag = "--TEST_fault_crash_during_log_replay=0.05";
  ASSERT_NO_FATALS(StartCluster({ crash_flag }));

  TestWorkload work(cluster_.get());
  work.set_num_write_threads(4);
  work.set_write_batch_size(1);
  work.set_write_timeout_millis(100);
  work.set_timeout_allowed(true);
  work.Setup();
  work.Start();
  while (work.rows_inserted() < 200) {
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  work.StopAndJoin();

  // Now restart the server, which will result in log replay, which will crash
  // mid-replay with very high probability since we wrote at least 200 log
  // entries and we're injecting a fault 5% of the time.
  cluster_->tablet_server(0)->Shutdown();

  // Restart might crash very quickly and actually return a bad status, so we
  // ignore the result.
  WARN_NOT_OK(cluster_->tablet_server(0)->Restart(), "Restart failed");

  // Wait for the process to crash during log replay.
  for (int i = 0; i < 3000 && cluster_->tablet_server(0)->IsProcessAlive(); i++) {
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  ASSERT_FALSE(cluster_->tablet_server(0)->IsProcessAlive()) << "TS didn't crash!";

  cluster_->tablet_server(0)->Shutdown();
  // Now remove the crash flag, so the next replay will complete, and restart
  // the server once more.
  auto& flags = *cluster_->tablet_server(0)->mutable_flags();
  flags.erase(std::remove_if(flags.begin(), flags.end(),
      [&](std::string& flag){ return flag == crash_flag; }));
  ASSERT_OK(cluster_->tablet_server(0)->Restart());

  ClusterVerifier cluster_verifier(cluster_.get());
  ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckRowCountWithRetries(work.table_name(),
                                       ClusterVerifier::AT_LEAST,
                                       work.rows_inserted(),
                                       MonoDelta::FromSeconds(30)));
}

TEST_F(TsRecoveryITest, CrashAfterLogSegmentPreAllocationg) {
  ASSERT_NO_FATALS(StartCluster({
      "--log_segment_size_bytes=2000",
      "--log_min_seconds_to_retain=0",
      "--retryable_request_timeout_secs=0",
      "--db_write_buffer_size=2000",
      "--TEST_log_fault_after_segment_allocation_min_replicate_index=10" }));

  auto& tserver = *cluster_->tablet_server(0);

  TestWorkload work(cluster_.get());
  work.set_num_write_threads(4);
  work.set_write_timeout_millis(100);
  work.set_timeout_allowed(true);
  work.set_payload_bytes(1_KB);
  work.Setup();
  work.Start();
  auto proxy = cluster_->GetProxy<tserver::TabletServerAdminServiceProxy>(&tserver);
  while (tserver.IsProcessAlive()) {
    tserver::FlushTabletsRequestPB req;
    req.set_dest_uuid(tserver.uuid());
    req.set_all_tablets(true);
    req.set_operation(tserver::FlushTabletsRequestPB::LOG_GC);
    tserver::FlushTabletsResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(30s);
    WARN_NOT_OK(proxy.FlushTablets(req, &resp, &controller), "FlushTablets failed");
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  work.StopAndJoin();

  EraseIf([](const auto& flag) {
    return flag.find("TEST_log_fault_after_segment_allocation_min_replicate_index") !=
           std::string::npos;
  }, tserver.mutable_flags());

  ASSERT_OK(tserver.Restart());

  ClusterVerifier cluster_verifier(cluster_.get());
  ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
}

}  // namespace yb
