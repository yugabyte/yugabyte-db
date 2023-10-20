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

#include "yb/cdc/cdc_service.h"
#include "yb/client/client-test-util.h"
#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/table.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/xcluster/xcluster_consumer_metrics.h"
#include "yb/master/xcluster/xcluster_safe_time_service.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/tsan_util.h"

using std::string;
using namespace std::chrono_literals;

DECLARE_int32(TEST_xcluster_simulated_lag_ms);
DECLARE_bool(enable_load_balancing);
DECLARE_uint32(xcluster_safe_time_log_outliers_interval_secs);
DECLARE_uint32(xcluster_safe_time_slow_tablet_delta_secs);

namespace yb {
using client::YBSchema;
using client::YBTable;
using client::YBTableName;
using OK = Status::OK;

const int kMasterCount = 3;
const int kTServerCount = 3;
const int kTabletCount = 3;
const string kTableName = "test_table";

class XClusterSafeTimeTest : public XClusterTestBase {
  typedef XClusterTestBase super;

 public:
  void SetUp() override {
    // Disable LB as we dont want tablets moving during the test.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_safe_time_log_outliers_interval_secs) = 10;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_safe_time_slow_tablet_delta_secs) = 10;
    super::SetUp();
    MiniClusterOptions opts;
    opts.num_masters = kMasterCount;
    opts.num_tablet_servers = kTServerCount;
    ASSERT_OK(InitClusters(opts));

    YBSchema schema;
    client::YBSchemaBuilder b;
    b.AddColumn("c0")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
    ASSERT_OK(b.Build(&schema));

    auto producer_cluster_future = std::async(std::launch::async, [&] {
      producer_table_name_ = VERIFY_RESULT(
          CreateTable(producer_client(), namespace_name, kTableName, kTabletCount, &schema));
      return producer_client()->OpenTable(producer_table_name_, &producer_table_);
    });

    auto consumer_cluster_future = std::async(std::launch::async, [&] {
      consumer_table_name_ = VERIFY_RESULT(
          CreateTable(consumer_client(), namespace_name, kTableName, kTabletCount, &schema));
      return consumer_client()->OpenTable(consumer_table_name_, &consumer_table_);
    });

    ASSERT_OK(producer_cluster_future.get());
    ASSERT_OK(consumer_cluster_future.get());

    ASSERT_OK(GetNamespaceId());

    std::vector<TabletId> producer_tablet_ids;
    ASSERT_OK(producer_cluster_.client_->GetTablets(
        producer_table_->name(), 0 /* max_tablets */, &producer_tablet_ids, NULL));
    ASSERT_EQ(producer_tablet_ids.size(), kTabletCount);

    auto producer_leader_tserver =
        GetLeaderForTablet(producer_cluster(), producer_tablet_ids[0])->server();
    producer_tablet_peer_ = ASSERT_RESULT(
        producer_leader_tserver->tablet_manager()->GetServingTablet(producer_tablet_ids[0]));

    ASSERT_OK(
        SetupUniverseReplication({producer_table_}, {LeaderOnly::kTrue, Transactional::kTrue}));

    // Verify that universe was setup on consumer.
    master::GetUniverseReplicationResponsePB resp;
    ASSERT_OK(VerifyUniverseReplication(kReplicationGroupId, &resp));
    ASSERT_EQ(resp.entry().producer_id(), kReplicationGroupId);
    ASSERT_EQ(resp.entry().tables_size(), 1);

    ASSERT_OK(ChangeXClusterRole(cdc::XClusterRole::STANDBY));

    // Initial wait is higher as it may need to wait for the create the table to complete.
    const client::YBTableName safe_time_table_name(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kXClusterSafeTimeTableName);
    ASSERT_OK(consumer_client()->WaitForCreateTableToFinish(
        safe_time_table_name, CoarseMonoClock::Now() + MonoDelta::FromMinutes(1)));
  }

  HybridTime GetProducerSafeTime() { return CHECK_RESULT(producer_tablet_peer_->LeaderSafeTime()); }

  Status GetNamespaceId() {
    master::GetNamespaceInfoResponsePB resp;

    RETURN_NOT_OK(consumer_client()->GetNamespaceInfo(
        std::string() /* namespace_id */, namespace_name, YQL_DATABASE_CQL, &resp));

    namespace_id_ = resp.namespace_().id();
    return OK();
  }

  void WriteWorkload(uint32_t start, uint32_t end, YBClient* client, const YBTableName& table) {
    auto session = client->NewSession(kRpcTimeout * 1s);
    client::TableHandle table_handle;
    ASSERT_OK(table_handle.Open(table, client));
    std::vector<std::shared_ptr<client::YBqlOp>> ops;

    LOG(INFO) << "Writing " << end - start << " inserts";
    for (uint32_t i = start; i < end; i++) {
      auto op = table_handle.NewInsertOp();
      auto req = op->mutable_request();
      QLAddInt32HashValue(req, static_cast<int32_t>(i));
      ASSERT_OK(session->TEST_ApplyAndFlush(op));
    }
  }

  bool VerifyWrittenRecords() {
    auto producer_results = ScanTableToStrings(producer_table_name_, producer_client());
    auto consumer_results = ScanTableToStrings(consumer_table_name_, consumer_client());
    return producer_results == consumer_results;
  }

  Status WaitForSafeTime(HybridTime min_safe_time) {
    RETURN_NOT_OK(CorrectlyPollingAllTablets(kTabletCount));

    return XClusterTestBase::WaitForSafeTime(namespace_id_, min_safe_time);
  }

  Status WaitForNotFoundSafeTime() {
    auto* tserver = consumer_cluster()->mini_tablet_servers().front()->server();
    return WaitFor(
        [&]() -> Result<bool> {
          auto safe_time_result = GetSafeTime(tserver, namespace_id_);
          if (safe_time_result && !safe_time_result.get()) {
            return true;
          }
          return false;
        },
        propagation_timeout_,
        Format("Wait for safe_time to get removed"));
  }

  void VerifyHistoryCutoffTime() {
    tserver::TSTabletManager::TabletPtrs tablet_ptrs;
    for (auto& mini_tserver : consumer_cluster()->mini_tablet_servers()) {
      auto* ts_manager = mini_tserver->server()->tablet_manager();
      auto* tserver = consumer_cluster()->mini_tablet_servers().front()->server();
      /*auto tablet_peers =*/ts_manager->GetTabletPeers(&tablet_ptrs);
      auto safe_time_result = GetSafeTime(tserver, namespace_id_);
      if (!safe_time_result) {
        FAIL() << "Expected safe time should be present.";
        return;
      }
      ASSERT_TRUE(safe_time_result.get().has_value());
      auto safe_time = *safe_time_result.get();
      for (auto& tablet : tablet_ptrs) {
        if (tablet->metadata()->namespace_id() == namespace_id_) {
          ASSERT_EQ(safe_time, ts_manager->AllowedHistoryCutoff(
              tablet->metadata()).primary_cutoff_ht);
        }
      }
    }
  }

 protected:
  YBTableName producer_table_name_;
  YBTableName consumer_table_name_;
  std::shared_ptr<YBTable> producer_table_;
  std::shared_ptr<YBTable> consumer_table_;
  string namespace_id_;
  std::shared_ptr<tablet::TabletPeer> producer_tablet_peer_;
};

TEST_F(XClusterSafeTimeTest, ComputeSafeTime) {
  // 1. Make sure safe time is initialized.
  auto ht_1 = GetProducerSafeTime();
  ASSERT_OK(WaitForSafeTime(ht_1));

  // 2. Write some data.
  LOG(INFO) << "Writing records for table " << producer_table_->name().ToString();
  WriteWorkload(0, 100, producer_client(), producer_table_->name());
  auto ht_2 = GetProducerSafeTime();

  // 3. Make sure safe time has progressed.
  ASSERT_OK(WaitForSafeTime(ht_2));
  ASSERT_TRUE(VerifyWrittenRecords());

  // 4. Make sure safe time does not advance when we pause replication.
  auto ht_before_pause = GetProducerSafeTime();
  ASSERT_OK(WaitForSafeTime(ht_before_pause));
  ASSERT_OK(
      ToggleUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, false));
  // Wait for Pollers to Stop.
  SleepFor(2s * kTimeMultiplier);
  auto ht_after_pause = GetProducerSafeTime();
  auto* tserver = consumer_cluster()->mini_tablet_servers().front()->server();
  auto safe_time_after_pause = ASSERT_RESULT(GetSafeTime(tserver, namespace_id_));
  ASSERT_TRUE(safe_time_after_pause);
  ASSERT_TRUE(safe_time_after_pause->is_valid());
  ASSERT_GE(safe_time_after_pause, ht_before_pause);
  ASSERT_LT(safe_time_after_pause, ht_after_pause);

  // 5.  Make sure safe time is reset when we resume replication.
  ASSERT_OK(
      ToggleUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, true));
  auto ht_4 = GetProducerSafeTime();
  ASSERT_OK(WaitForSafeTime(ht_4));

  // 6. Make sure safe time is cleaned up when we switch to ACTIVE role.
  ASSERT_OK(ChangeXClusterRole(cdc::XClusterRole::ACTIVE));
  ASSERT_OK(WaitForNotFoundSafeTime());

  // 7.  Make sure safe time is reset when we switch back to STANDBY role.
  ASSERT_OK(ChangeXClusterRole(cdc::XClusterRole::STANDBY));
  auto ht_5 = GetProducerSafeTime();
  ASSERT_OK(WaitForSafeTime(ht_5));

  // 8.  Make sure safe time is cleaned up when we delete replication.
  ASSERT_OK(DeleteUniverseReplication());
  ASSERT_OK(VerifyUniverseReplicationDeleted(
      consumer_cluster(),
      consumer_client(),
      kReplicationGroupId,
      FLAGS_cdc_read_rpc_timeout_ms * 2));
  ASSERT_OK(WaitForNotFoundSafeTime());
}

TEST_F(XClusterSafeTimeTest, LagInSafeTime) {
  // 1. Make sure safe time is initialized.
  auto ht_1 = GetProducerSafeTime();
  ASSERT_OK(WaitForSafeTime(ht_1));

  // 2. Simulate replication lag and make sure safe time does not move.
  SetAtomicFlag(-1, &FLAGS_TEST_xcluster_simulated_lag_ms);
  WriteWorkload(0, 100, producer_client(), producer_table_->name());
  auto ht_2 = GetProducerSafeTime();

  // 3. Make sure safe time has not progressed beyond the write.
  ASSERT_NOK(WaitForSafeTime(ht_2));

  // 4. Make sure safe time has progressed.
  SetAtomicFlag(0, &FLAGS_TEST_xcluster_simulated_lag_ms);
  ASSERT_OK(WaitForSafeTime(ht_2));
}

TEST_F(XClusterSafeTimeTest, ConsumerHistoryCutoff) {
  // Make sure safe time is initialized.
  auto ht_1 = GetProducerSafeTime();
  ASSERT_OK(WaitForSafeTime(ht_1));

  // Insert some data to producer.
  WriteWorkload(0, 10, producer_client(), producer_table_->name());
  auto ht_2 = GetProducerSafeTime();

  // Make sure safe time has progressed, this helps ensures that there is some safetime present on
  // the tservers.
  ASSERT_OK(WaitForSafeTime(ht_2));

  // Simulate replication lag and make sure safe time does not move.
  ASSERT_OK(SET_FLAG(TEST_xcluster_simulated_lag_ms, -1));
  WriteWorkload(10, 20, producer_client(), producer_table_->name());
  auto ht_3 = GetProducerSafeTime();

  // 5. Make sure safe time has not progressed beyond the write.
  ASSERT_NOK(WaitForSafeTime(ht_3));

  // Verify the history cutoff is adjusted based on the lag time.
  VerifyHistoryCutoffTime();

  // Make sure safe time has progressed.
  ASSERT_OK(SET_FLAG(TEST_xcluster_simulated_lag_ms, 0));
  ASSERT_OK(WaitForSafeTime(ht_3));

  // Ensure history cutoff time has progressed.
  VerifyHistoryCutoffTime();
}
}  // namespace yb
