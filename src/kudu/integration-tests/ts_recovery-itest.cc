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

#include "kudu/integration-tests/cluster_verifier.h"
#include "kudu/integration-tests/external_mini_cluster.h"
#include "kudu/integration-tests/test_workload.h"
#include "kudu/util/test_util.h"

#include <string>

using std::string;

namespace kudu {

class TsRecoveryITest : public KuduTest {
 public:
  virtual void TearDown() OVERRIDE {
    if (cluster_) cluster_->Shutdown();
    KuduTest::TearDown();
  }

 protected:
  void StartCluster(const vector<string>& extra_tserver_flags = vector<string>(),
                    int num_tablet_servers = 1);

  gscoped_ptr<ExternalMiniCluster> cluster_;
};

void TsRecoveryITest::StartCluster(const vector<string>& extra_tserver_flags,
                                   int num_tablet_servers) {
  ExternalMiniClusterOptions opts;
  opts.num_tablet_servers = num_tablet_servers;
  opts.extra_tserver_flags = extra_tserver_flags;
  cluster_.reset(new ExternalMiniCluster(opts));
  ASSERT_OK(cluster_->Start());
}

// Test crashing a server just before appending a COMMIT message.
// We then restart the server and ensure that all rows successfully
// inserted before the crash are recovered.
TEST_F(TsRecoveryITest, TestRestartWithOrphanedReplicates) {
  NO_FATALS(StartCluster());
  cluster_->SetFlag(cluster_->tablet_server(0),
                    "fault_crash_before_append_commit", "0.05");

  TestWorkload work(cluster_.get());
  work.set_num_replicas(1);
  work.set_num_write_threads(4);
  work.set_write_timeout_millis(100);
  work.set_timeout_allowed(true);
  work.Setup();
  work.Start();

  // Wait for the process to crash due to the injected fault.
  while (cluster_->tablet_server(0)->IsProcessAlive()) {
    SleepFor(MonoDelta::FromMilliseconds(10));
  }

  // Stop the writers.
  work.StopAndJoin();

  // Restart the server, and it should recover.
  cluster_->tablet_server(0)->Shutdown();
  ASSERT_OK(cluster_->tablet_server(0)->Restart());


  // TODO(KUDU-796): after a restart, we may have to replay some
  // orphaned replicates from the log. However, we currently
  // allow reading while those are being replayed, which means we
  // can "go back in time" briefly. So, we have some retries here.
  // When KUDU-796 is fixed, remove the retries.
  ClusterVerifier v(cluster_.get());
  NO_FATALS(v.CheckRowCountWithRetries(work.table_name(),
                                       ClusterVerifier::AT_LEAST,
                                       work.rows_inserted(),
                                       MonoDelta::FromSeconds(20)));
}

// Test that we replay from the recovery directory, if it exists.
TEST_F(TsRecoveryITest, TestCrashDuringLogReplay) {
  NO_FATALS(StartCluster({ "--fault_crash_during_log_replay=0.05" }));

  TestWorkload work(cluster_.get());
  work.set_num_replicas(1);
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
  ignore_result(cluster_->tablet_server(0)->Restart());

  // Wait for the process to crash during log replay.
  for (int i = 0; i < 3000 && cluster_->tablet_server(0)->IsProcessAlive(); i++) {
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  ASSERT_FALSE(cluster_->tablet_server(0)->IsProcessAlive()) << "TS didn't crash!";

  // Now remove the crash flag, so the next replay will complete, and restart
  // the server once more.
  cluster_->tablet_server(0)->Shutdown();
  cluster_->tablet_server(0)->mutable_flags()->clear();
  ASSERT_OK(cluster_->tablet_server(0)->Restart());

  ClusterVerifier v(cluster_.get());
  NO_FATALS(v.CheckRowCountWithRetries(work.table_name(),
                                       ClusterVerifier::AT_LEAST,
                                       work.rows_inserted(),
                                       MonoDelta::FromSeconds(30)));
}

} // namespace kudu
