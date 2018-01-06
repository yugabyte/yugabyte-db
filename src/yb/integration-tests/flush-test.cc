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

#include <chrono>

#include "yb/integration-tests/test_workload.h"
#include "yb/integration-tests/ts_itest-base.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/rocksdb/memory_monitor.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

using namespace std::literals;

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

  size_t BytesWritten() {
    return workload_->rows_inserted() * kPayloadBytes;
  }

  void WriteAtLeast(size_t size_bytes) {
    workload_->Start();
    LOG(INFO) << "Waiting until we've written at least " << size_bytes << " bytes ...";
    ASSERT_OK(
        WaitFor([this, size_bytes] { return BytesWritten() >= size_bytes; }, 60s, "Write", 10ms));
    workload_->StopAndJoin();
    LOG(INFO) << "Wrote " << BytesWritten() << " bytes.";
  }

  const size_t kTabletLimitMB = 100;
  const size_t kServerLimitMB = 10;
  const size_t kNumTablets = 3;
  const size_t kPayloadBytes = 8 * 1024; // 8KB
  std::unique_ptr<MiniCluster> cluster_;
  std::unique_ptr<TestWorkload> workload_;
};

TEST_F(FlushITest, TestFlushHappens) {
  const size_t total_flushes_before = TotalFlushes();
  WriteAtLeast((kServerLimitMB << 20) + 1);
  ASSERT_OK(WaitFor(
      [this, total_flushes_before] { return TotalFlushes() > total_flushes_before; },
      60s, "Flush", 10ms));
  const size_t flushes_since_write = TotalFlushes() - total_flushes_before;
  LOG(INFO) << "Flushed " << flushes_since_write << " times.";
  ASSERT_GT(flushes_since_write, 0);
}

} // namespace tserver
} // namespace yb
