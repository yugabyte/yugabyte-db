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
#include <vector>

#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/table_handle.h"

#include "yb/gutil/mathlimits.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/integration-tests/test_workload.h"

#include "yb/util/metrics.h"
#include "yb/util/pstack_watcher.h"
#include "yb/util/random.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_util.h"

METRIC_DECLARE_entity(tablet);
METRIC_DECLARE_counter(leader_memory_pressure_rejections);
METRIC_DECLARE_counter(follower_memory_pressure_rejections);

using strings::Substitute;
using std::vector;
using namespace std::literals;

namespace yb {

using client::YBClient;
using client::YBClientBuilder;
using client::YBTable;
using client::YBTableName;

class ClientStressTest : public YBMiniClusterTestBase<ExternalMiniCluster> {
 public:
  void SetUp() override {
    YBMiniClusterTestBase::SetUp();

    ExternalMiniClusterOptions opts = default_opts();
    if (multi_master()) {
      opts.num_masters = 3;
      opts.master_rpc_ports = { 0, 0, 0 };
    }
    opts.num_tablet_servers = 3;
    cluster_.reset(new ExternalMiniCluster(opts));
    ASSERT_OK(cluster_->Start());
  }

  void DoTearDown() override {
    alarm(0);
    cluster_->Shutdown();
    YBMiniClusterTestBase::DoTearDown();
  }

 protected:
  virtual bool multi_master() const {
    return false;
  }

  virtual ExternalMiniClusterOptions default_opts() const {
    return ExternalMiniClusterOptions();
  }
};

// Stress test a case where most of the operations are expected to time out.
// This is a regression test for various bugs we've seen in timeout handling,
// especially with concurrent requests.
TEST_F(ClientStressTest, TestLookupTimeouts) {
  const int kSleepMillis = AllowSlowTests() ? 5000 : 100;

  TestWorkload work(cluster_.get());
  work.set_num_write_threads(64);
  work.set_write_timeout_millis(10);
  work.set_timeout_allowed(true);
  work.Setup();
  work.Start();
  SleepFor(MonoDelta::FromMilliseconds(kSleepMillis));
}

// Override the base test to run in multi-master mode.
class ClientStressTest_MultiMaster : public ClientStressTest {
 protected:
  bool multi_master() const override {
    return true;
  }
};

// Stress test a case where most of the operations are expected to time out.
// This is a regression test for KUDU-614 - it would cause a deadlock prior
// to fixing that bug.
TEST_F(ClientStressTest_MultiMaster, TestLeaderResolutionTimeout) {
  TestWorkload work(cluster_.get());
  work.set_num_write_threads(64);

  // This timeout gets applied to the master requests. It's lower than the
  // amount of time that we sleep the masters, to ensure they timeout.
  work.set_client_default_rpc_timeout_millis(250);
  // This is the time budget for the whole request. It has to be longer than
  // the above timeout so that the client actually attempts to resolve
  // the leader.
  work.set_write_timeout_millis(280);
  work.set_timeout_allowed(true);
  work.Setup();

  work.Start();

  ASSERT_OK(cluster_->tablet_server(0)->Pause());
  ASSERT_OK(cluster_->tablet_server(1)->Pause());
  ASSERT_OK(cluster_->tablet_server(2)->Pause());
  ASSERT_OK(cluster_->master(0)->Pause());
  ASSERT_OK(cluster_->master(1)->Pause());
  ASSERT_OK(cluster_->master(2)->Pause());
  SleepFor(MonoDelta::FromMilliseconds(300));
  ASSERT_OK(cluster_->tablet_server(0)->Resume());
  ASSERT_OK(cluster_->tablet_server(1)->Resume());
  ASSERT_OK(cluster_->tablet_server(2)->Resume());
  ASSERT_OK(cluster_->master(0)->Resume());
  ASSERT_OK(cluster_->master(1)->Resume());
  ASSERT_OK(cluster_->master(2)->Resume());
  SleepFor(MonoDelta::FromMilliseconds(100));

  // Set an explicit timeout. This test has caused deadlocks in the past.
  // Also make sure to dump stacks before the alarm goes off.
  PstackWatcher watcher(MonoDelta::FromSeconds(30));
  alarm(60);
}


// Override the base test to start a cluster with a low memory limit.
class ClientStressTest_LowMemory : public ClientStressTest {
 protected:
  ExternalMiniClusterOptions default_opts() const override {
    // There's nothing scientific about this number; it must be low enough to
    // trigger memory pressure request rejection yet high enough for the
    // servers to make forward progress.
    //
    // Note that if this number is set too low, the test will fail in a CHECK in TestWorkload
    // after retries are exhausted when writing an entry. This happened e.g. when a substantial
    // upfront memory overhead was introduced by adding a large lock-free queue in PrepareThread.
    const int kMemLimitBytes = RegularBuildVsSanitizers(64_MB, 2_MB);
    ExternalMiniClusterOptions opts;
    opts.extra_tserver_flags.push_back(Substitute("--memory_limit_hard_bytes=$0", kMemLimitBytes));
    opts.extra_tserver_flags.emplace_back("--memory_limit_soft_percentage=0");

    return opts;
  }
};

// Stress test where, due to absurdly low memory limits, many client requests
// are rejected, forcing the client to retry repeatedly.
// TODO(mbautin): switch this test to QL (RocksDB-backed) after we implement proper memory
// tracking for RocksDB (https://yugabyte.atlassian.net/browse/ENG-442).
TEST_F(ClientStressTest_LowMemory, TestMemoryThrottling) {
  // Sanitized tests run much slower, so we don't want to wait for as many
  // rejections before declaring the test to be passed.
  const int64_t kMinRejections = RegularBuildVsSanitizers(100, 20);

  const MonoDelta kMaxWaitTime = MonoDelta::FromSeconds(60);

  TestWorkload work(cluster_.get());
  work.set_write_batch_size(RegularBuildVsSanitizers(50, 5));

  work.Setup();
  work.Start();

  // Wait until we've rejected some number of requests.
  MonoTime deadline = MonoTime::Now() + kMaxWaitTime;
  while (true) {
    int64_t total_num_rejections = 0;

    // It can take some time for the tablets (and their metric entities) to
    // appear on every server. Rather than explicitly wait for that above,
    // we'll just treat the lack of a metric as non-fatal. If the entity
    // or metric is truly missing, we'll eventually timeout and fail.
    for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
      int64_t value;
      Status s = cluster_->tablet_server(i)->GetInt64Metric(
          &METRIC_ENTITY_tablet,
          nullptr,
          &METRIC_leader_memory_pressure_rejections,
          "value",
          &value);
      if (!s.IsNotFound()) {
        ASSERT_OK(s);
        total_num_rejections += value;
      }
      s = cluster_->tablet_server(i)->GetInt64Metric(
          &METRIC_ENTITY_tablet,
          nullptr,
          &METRIC_follower_memory_pressure_rejections,
          "value",
          &value);
      if (!s.IsNotFound()) {
        ASSERT_OK(s);
        total_num_rejections += value;
      }
    }
    if (total_num_rejections >= kMinRejections) {
      break;
    } else if (deadline.ComesBefore(MonoTime::Now())) {
      FAIL() << "Ran for " << kMaxWaitTime.ToString() << ", deadline expired and only saw "
             << total_num_rejections << " memory rejections";
    }
    SleepFor(MonoDelta::FromMilliseconds(200));
  }
}

}  // namespace yb
