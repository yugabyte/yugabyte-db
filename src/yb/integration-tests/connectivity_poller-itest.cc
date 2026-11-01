// Copyright (c) YugabyteDB, Inc.
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

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "yb/gutil/dynamic_annotations.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/mini_master.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/net/net_util.h"
#include "yb/util/test_macros.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;

DECLARE_uint64(connectivity_check_interval_ms);

namespace yb::integration_tests {

class ConnectivityPollerITest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  void SetUp() override {
    // Set before the tservers start so the poller runs at this interval from its first pass.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_connectivity_check_interval_ms) = connectivity_interval_ms();
    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_masters = num_masters();
    opts.num_tablet_servers = 1;
    cluster_ = std::make_unique<MiniCluster>(opts);
    ASSERT_OK(cluster_->Start());
  }

  virtual size_t num_masters() { return 1; }
  virtual uint64_t connectivity_interval_ms() { return 6000; }

  Result<std::optional<tserver::ConnectivityEntryPB>> GetConnectivityEntry(
      tserver::MiniTabletServer& ts, const std::string& uuid) {
    tserver::TabletServerServiceProxy proxy(
        &ts.server()->proxy_cache(), HostPort(ts.bound_rpc_addr()));
    tserver::ConnectivityStateRequestPB req;
    tserver::ConnectivityStateResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(10s * kTimeMultiplier);
    RETURN_NOT_OK(proxy.ConnectivityState(req, &resp, &controller));
    for (const auto& entry : resp.entries()) {
      if (entry.uuid() == uuid) {
        return entry;
      }
    }
    return std::nullopt;
  }

  Status WaitForMasterEntry(
      tserver::MiniTabletServer& ts, const std::string& uuid, bool alive, MonoDelta timeout) {
    return WaitFor(
        [&]() -> Result<bool> {
          auto entry = VERIFY_RESULT(GetConnectivityEntry(ts, uuid));
          if (!entry || entry->server_type() != tserver::ServerType::MASTER ||
              entry->alive() != alive) {
            return false;
          }
          // A live master carries no failure text; a dead one must say why.
          return entry->last_failure().empty() == alive;
        },
        timeout, Format("Wait for master $0 to be reported alive=$1", uuid, alive));
  }
};

class ConnectivityPollerITest3Masters : public ConnectivityPollerITest {
 public:
  size_t num_masters() override { return 3; }
};

// Half this interval is shorter than a heartbeat interval.
class ConnectivityPollerITestShortInterval : public ConnectivityPollerITest {
 public:
  uint64_t connectivity_interval_ms() override { return 1500; }
};

TEST_F(ConnectivityPollerITest, MasterDown) {
  auto& ts = *cluster_->mini_tablet_server(0);
  auto* master = cluster_->mini_master();
  const auto master_uuid = master->permanent_uuid();
  ASSERT_OK(WaitForMasterEntry(ts, master_uuid, /*alive=*/true, 30s * kTimeMultiplier));

  master->Shutdown();
  ASSERT_OK(WaitForMasterEntry(ts, master_uuid, /*alive=*/false, 30s * kTimeMultiplier));

  ASSERT_OK(master->Start());
  ASSERT_OK(WaitForMasterEntry(ts, master_uuid, /*alive=*/true, 60s * kTimeMultiplier));
}

TEST_F_EX(ConnectivityPollerITest, MasterLeaderChange, ConnectivityPollerITest3Masters) {
  auto& ts = *cluster_->mini_tablet_server(0);
  const auto old_leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->permanent_uuid();
  ASSERT_OK(WaitForMasterEntry(ts, old_leader, /*alive=*/true, 30s * kTimeMultiplier));

  std::string new_leader;
  for (size_t i = 0; i < cluster_->num_masters(); ++i) {
    new_leader = cluster_->mini_master(i)->permanent_uuid();
    if (new_leader != old_leader) {
      break;
    }
  }
  ASSERT_EQ(ASSERT_RESULT(cluster_->StepDownMasterLeader(new_leader)), new_leader);
  ASSERT_OK(WaitForMasterEntry(ts, new_leader, /*alive=*/true, 60s * kTimeMultiplier));
}

TEST_F_EX(ConnectivityPollerITest, MasterStaysAliveWithShortInterval,
          ConnectivityPollerITestShortInterval) {
  auto& ts = *cluster_->mini_tablet_server(0);
  const auto master_uuid = cluster_->mini_master()->permanent_uuid();
  ASSERT_OK(WaitForMasterEntry(ts, master_uuid, /*alive=*/true, 30s * kTimeMultiplier));

  const auto deadline = MonoTime::Now() + 15s * kTimeMultiplier;
  while (MonoTime::Now() < deadline) {
    auto entry = ASSERT_RESULT(GetConnectivityEntry(ts, master_uuid));
    ASSERT_TRUE(entry.has_value());
    ASSERT_TRUE(entry->alive()) << entry->ShortDebugString();
    SleepFor(500ms);
  }
}

}  // namespace yb::integration_tests
