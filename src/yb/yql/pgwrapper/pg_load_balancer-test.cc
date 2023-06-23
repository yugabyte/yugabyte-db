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

#include "yb/common/common_types.pb.h"
#include "yb/gutil/dynamic_annotations.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/mini_master.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tablet/tablet_types.pb.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"

#include "yb/util/tsan_util.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_int32(load_balancer_initial_delay_secs);


using namespace std::literals;

namespace yb {
namespace pgwrapper {

class PgLoadBalancerTest : public PgMiniTestBase {
  virtual size_t NumTabletServers() override {
    return 1;
  }
};

TEST_F(PgLoadBalancerTest, LoadBalanceDuringLongRunningTransaction) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 100;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_bg_task_wait_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_initial_delay_secs) = 20;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t(k INT, v INT) SPLIT INTO 2 TABLETS;"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO t SELECT i, 1 FROM (SELECT generate_series(1, 10000) i) t2;"));

  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

  for (int i = 0; i < 100; ++i) {
    ASSERT_OK(conn.ExecuteFormat("UPDATE t SET v = 2 where k = $0;", i));
  }

  ASSERT_OK(cluster_->AddTabletServer());

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto x = client_->IsLoadBalanced(2);
    return x;
  }, 15s * kTimeMultiplier, "Wait for load balancer to balance to second tserver."));

  SleepFor(
    (FLAGS_tserver_heartbeat_metrics_interval_ms * 2ms +
     FLAGS_load_balancer_initial_delay_secs * 1s +
     FLAGS_catalog_manager_bg_task_wait_ms * 1ms) *
    kTimeMultiplier);

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto peers = cluster_->mini_tablet_server(0)->server()->tablet_manager()->GetTabletPeers();
    for (const auto& peer : peers) {
      if (peer->tablet() &&
          peer->TEST_table_type() == PGSQL_TABLE_TYPE &&
          peer->data_state() == tablet::TABLET_DATA_TOMBSTONED) {
        return false;
      }
    }
    return true;
  }, 15s * kTimeMultiplier, "Wait for tablet shutdown."));

  ASSERT_OK(conn.CommitTransaction());
}

} // namespace pgwrapper
} // namespace yb
