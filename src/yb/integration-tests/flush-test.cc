// Copyright (c) YugaByte, Inc.
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
#include "yb/integration-tests/test_workload.h"
#include "yb/integration-tests/ts_itest-base.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/rocksdb/memory_monitor.h"

DECLARE_int64(global_memstore_size_percentage);
DECLARE_int64(global_memstore_size_mb_max);
DECLARE_int32(memstore_size_mb);

namespace yb {
namespace client {
class YBTableName;
}
namespace tserver {

class FlushITest : public YBTest {
 public:
  FlushITest() {}
  void SetUp() override {
    FLAGS_memstore_size_mb = kTabletLimitMB;
    // Set the global memstore to kServerLimitMB.
    FLAGS_global_memstore_size_percentage = 100;
    FLAGS_global_memstore_size_mb_max = kServerLimitMB;
    YBTest::SetUp();

    // Start cluster
    MiniClusterOptions opts;
    opts.num_tablet_servers = 1;
    cluster_.reset(new MiniCluster(env_.get(), opts));
    ASSERT_OK(cluster_->Start());
    ASSERT_OK(cluster_->WaitForTabletServerCount(opts.num_tablet_servers));

    // Setup workload
    workload_.reset(new TestWorkload(cluster_.get()));
    workload_->set_timeout_allowed(true);
    workload_->set_payload_bytes(kPayloadBytes);
    workload_->set_write_batch_size(1);
    workload_->set_num_write_threads(4);
    workload_->set_num_replicas(1);
    workload_->set_num_tablets(kNumTablets);
    workload_->Setup();
  }

  void TearDown() override {
    workload_->StopAndJoin();
    cluster_->Shutdown();
    YBTest::TearDown();
  }

 protected:

  size_t TotalFlushes() {
    size_t total_flushes = 0;
    std::vector<scoped_refptr<tablet::TabletPeer>> tablet_peers;
    cluster_->mini_tablet_server(0)->server()->tablet_manager()->GetTabletPeers(&tablet_peers);
    for (auto& peer : tablet_peers) {
      total_flushes += peer->tablet()->flush_stats()->num_flushes();
    }
    return total_flushes;
  }

  void WriteAtLeast(size_t size_bytes) {
    workload_->Start();
    LOG(INFO) << "Waiting until we've written at least " << size_bytes << " bytes ...";
    while (workload_->rows_inserted() * kPayloadBytes < size_bytes) {
      SleepFor(MonoDelta::FromMilliseconds(10));
    }
    workload_->StopAndJoin();
    LOG(INFO) << "Wrote " << size_bytes << " bytes.";
  }

  const size_t kTabletLimitMB = 100;
  const size_t kServerLimitMB = 10;
  const size_t kNumTablets = 3;
  const size_t kPayloadBytes = 8 * 1024; // 8KB
  gscoped_ptr<MiniCluster> cluster_;
  gscoped_ptr<TestWorkload> workload_;
};

TEST_F(FlushITest, TestFlushHappens) {
  size_t total_flushes_before = TotalFlushes();
  WriteAtLeast((kServerLimitMB << 20) + 1);
  size_t total_flushes = TotalFlushes() - total_flushes_before;
  LOG(INFO) << "Flushed " << total_flushes << " times.";
  while (TotalFlushes() - total_flushes_before == 0) {
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  ASSERT_GT(total_flushes, 0);
}

} // namespace tserver
} // namespace yb
