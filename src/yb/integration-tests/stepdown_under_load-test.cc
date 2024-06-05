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

#include <atomic>
#include <memory>
#include <vector>

#include "yb/client/client.h"
#include "yb/client/table.h"

#include "yb/common/entity_ids.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/cluster_verifier.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/load_generator.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/master_cluster.proxy.h"

#include "yb/util/test_util.h"

namespace yb {
namespace itest {

using integration_tests::YBTableTestBase;
using std::string;
using std::vector;

namespace {

const auto kDefaultTimeout = MonoDelta::FromSeconds(30);

}  // anonymous namespace


class StepDownUnderLoadTest : public YBTableTestBase {
 public:
  bool use_external_mini_cluster() override { return true; }
  int num_tablets() override { return 1; }
  bool enable_ysql() override { return false; }
};

TEST_F(StepDownUnderLoadTest, TestStepDownUnderLoad) {
  std::atomic_bool stop_requested_flag(false);
  static constexpr int kRows = 1000000;
  static constexpr int kStartKey = 0;
  static constexpr int kWriterThreads = 4;
  static constexpr int kReaderThreads = 4;
  static constexpr int kValueSizeBytes = 16;

  // Tolerate some errors in the load test due to temporary unavailability.
  static constexpr int kMaxWriteErrors = 1000;
  static constexpr int kMaxReadErrors = 1000;

  // Create two separate clients for read and writes.
  auto write_client = CreateYBClient();
  auto read_client = CreateYBClient();
  yb::load_generator::YBSessionFactory write_session_factory(write_client.get(), &table_);
  yb::load_generator::YBSessionFactory read_session_factory(read_client.get(), &table_);

  yb::load_generator::MultiThreadedWriter writer(kRows, kStartKey, kWriterThreads,
                                                 &write_session_factory, &stop_requested_flag,
                                                 kValueSizeBytes, kMaxWriteErrors);

  yb::load_generator::MultiThreadedReader reader(kRows, kReaderThreads, &read_session_factory,
                                                 writer.InsertionPoint(), writer.InsertedKeys(),
                                                 writer.FailedKeys(), &stop_requested_flag,
                                                 kValueSizeBytes, kMaxReadErrors);

  auto* const emc = external_mini_cluster();
  TabletServerMap ts_map = ASSERT_RESULT(itest::CreateTabletServerMap(emc));

  vector<TabletId> tablet_ids;
  {
    const auto ts0_uuid = ts_map.begin()->first;
    auto* const ts0_details = ts_map[ts0_uuid].get();
    ASSERT_OK(ListRunningTabletIds(ts0_details, kDefaultTimeout, &tablet_ids));
    ASSERT_EQ(1, tablet_ids.size());
  }
  const TabletId tablet_id(tablet_ids.front());

  writer.Start();

  reader.set_client_id(write_session_factory.ClientId());
  reader.Start();

  for (int i = 0; i < 10 && !stop_requested_flag; ++i) {
    TServerDetails* leader = nullptr;
    ASSERT_OK(FindTabletLeader(ts_map, tablet_id, kDefaultTimeout, &leader));
    CHECK_NOTNULL(leader);

    // Find a non-leader tablet and restart it.
    const TServerDetails* non_leader = nullptr;
    for (const auto& ts_map_entry : ts_map) {
      const TServerDetails* ts_details = ts_map_entry.second.get();
      if (ts_details->uuid() != leader->uuid()) {
        non_leader = ts_details;
        break;
      }
    }
    ASSERT_NE(non_leader->uuid(), leader->uuid());

    auto *const external_ts = emc->tablet_server_by_uuid(non_leader->uuid());
    external_ts->Shutdown();
    SleepFor(MonoDelta::FromSeconds(3));
    ASSERT_OK(external_ts->Restart());

    while (!emc->tablet_server_by_uuid(non_leader->uuid())->IsProcessAlive()) {
      SleepFor(MonoDelta::FromMilliseconds(50));
    }

    // Step down in favor of the server that was stopped at the previous iteration and therefore
    // has a high chance of not being caught up. This stepdown will most likely be unsuccessful,
    // but might uncover bugs in commit index handling on the new leader.
    ASSERT_OK(FindTabletLeader(ts_map, tablet_id, kDefaultTimeout, &leader));
    CHECK_NOTNULL(leader);
    auto s = LeaderStepDown(leader, tablet_id, non_leader, kDefaultTimeout);
    ASSERT_TRUE(s.ok() || s.IsIllegalState());
  }

  stop_requested_flag = true;  // stop both reader and writer
  writer.WaitForCompletion();
  LOG(INFO) << "Writing complete";

  reader.WaitForCompletion();
  LOG(INFO) << "Reading complete";

  ASSERT_EQ(0, writer.num_write_errors());
  ASSERT_EQ(0, reader.num_read_errors());

  ASSERT_GE(writer.num_writes(), kWriterThreads);

  // Assuming reads are at least as fast as writes.
  ASSERT_GE(reader.num_reads(), kReaderThreads);

  ClusterVerifier cluster_verifier(external_mini_cluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckRowCount(table_->name(), ClusterVerifier::EXACTLY,
      writer.num_writes()));
}

}  // namespace itest
}  // namespace yb
