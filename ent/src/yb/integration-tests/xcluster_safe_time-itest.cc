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

#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_replication.pb.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/flags.h"
#include "yb/util/tsan_util.h"
#include "yb/integration-tests/twodc_test_base.h"
#include "yb/client/table.h"

using namespace std::chrono_literals;

DECLARE_int32(xcluster_safe_time_update_interval_secs);
DECLARE_bool(TEST_xcluster_simulate_have_more_records);
DECLARE_bool(enable_load_balancing);

namespace yb {
using client::YBSchema;
using client::YBTable;
using client::YBTableName;

namespace enterprise {
const int kMasterCount = 3;
const int kTServerCount = 3;
const int kTabletCount = 3;
const string kTableName = "test_table";

class XClusterSafeTimeTest : public TwoDCTestBase {
 public:
  void SetUp() override {
    // Disable LB as we use the leader tservers, and dont want tablets moving during the test
    FLAGS_enable_load_balancing = false;
    SetAtomicFlag(1, &FLAGS_xcluster_safe_time_update_interval_secs);
    safe_time_propagation_timeout_ =
        FLAGS_xcluster_safe_time_update_interval_secs * 5s * kTimeMultiplier;

    TwoDCTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_masters = kMasterCount;
    opts.num_tablet_servers = kTServerCount;
    ASSERT_OK(InitClusters(opts));

    YBSchema schema;
    client::YBSchemaBuilder b;
    b.AddColumn("c0")->Type(INT32)->NotNull()->HashPrimaryKey();
    CHECK_OK(b.Build(&schema));

    auto producer_cluster_future = std::async(std::launch::async, [&] {
      producer_table_name_ = VERIFY_RESULT(
          CreateTable(producer_client(), kNamespaceName, kTableName, kTabletCount, &schema));
      return producer_client()->OpenTable(producer_table_name_, &producer_table_);
    });

    auto consumer_cluster_future = std::async(std::launch::async, [&] {
      consumer_table_name_ = VERIFY_RESULT(
          CreateTable(consumer_client(), kNamespaceName, kTableName, kTabletCount, &schema));
      return consumer_client()->OpenTable(consumer_table_name_, &consumer_table_);
    });

    ASSERT_OK(producer_cluster_future.get());
    ASSERT_OK(consumer_cluster_future.get());

    ASSERT_OK(GetNamespaceId());

    std::vector<TabletId> producer_tablet_ids;
    if (producer_table_) {
      ASSERT_OK(producer_cluster_.client_->GetTablets(
          producer_table_->name(), static_cast<int32_t>(kTabletCount), &producer_tablet_ids,
          NULL));
      ASSERT_GT(producer_tablet_ids.size(), 0);
    }
    auto producer_leader_tserver =
        GetLeaderForTablet(producer_cluster(), producer_tablet_ids[0])->server();
    producer_tablet_peer_ = ASSERT_RESULT(
        producer_leader_tserver->tablet_manager()->GetServingTablet(producer_tablet_ids[0]));

    std::vector<TabletId> consumer_tablet_ids;
    if (consumer_table_) {
      ASSERT_OK(consumer_cluster_.client_->GetTablets(
          consumer_table_->name(), static_cast<int32_t>(kTabletCount), &consumer_tablet_ids,
          NULL));
      ASSERT_GT(consumer_tablet_ids.size(), 0);
    }

    consumer_leader_tserver_ =
        GetLeaderForTablet(consumer_cluster(), consumer_tablet_ids[0])->server();

    ASSERT_OK(SetupUniverseReplication({producer_table_}));

    // Verify that universe was setup on consumer.
    master::GetUniverseReplicationResponsePB resp;
    ASSERT_OK(VerifyUniverseReplication(kUniverseId, &resp));
    ASSERT_EQ(resp.entry().producer_id(), kUniverseId);
    ASSERT_EQ(resp.entry().tables_size(), 1);
    ASSERT_EQ(resp.entry().tables(0), producer_table_->id());

    // Initial wait is higher as it may need to wait for the create the table to complete
    const client::YBTableName safe_time_table_name(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kXClusterSafeTimeTableName);
    ASSERT_OK(consumer_client()->WaitForCreateTableToFinish(
        safe_time_table_name, CoarseMonoClock::Now() + MonoDelta::FromMinutes(1)));
  }

  Status GetNamespaceId() {
    master::GetNamespaceInfoResponsePB resp;

    RETURN_NOT_OK(consumer_client()->GetNamespaceInfo(
        std::string() /* namespace_id */, kNamespaceName, YQL_DATABASE_CQL, &resp));

    namespace_id_ = resp.namespace_().id();
    return Status::OK();
  }

  void WriteWorkload(uint32_t start, uint32_t end, YBClient* client, const YBTableName& table) {
    auto session = client->NewSession();
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
    RETURN_NOT_OK(CorrectlyPollingAllTablets(consumer_cluster(), kTabletCount));

    return WaitFor(
        [&]() -> Result<bool> {
          auto safe_time_status = consumer_leader_tserver_->GetXClusterSafeTime(namespace_id_);
          if (!safe_time_status.ok() && safe_time_status.status().IsNotFound()) {
            return false;
          }
          CHECK_OK(safe_time_status);
          auto safe_time = safe_time_status.get();
          return safe_time.is_valid() && safe_time > min_safe_time;
        },
        safe_time_propagation_timeout_,
        Format("Wait for safe_time to move above $0", min_safe_time.ToDebugString()));
  }

  Status WaitForNotFoundSafeTime() {
    return WaitFor(
        [&]() -> Result<bool> {
          auto safe_time_status = consumer_leader_tserver_->GetXClusterSafeTime(namespace_id_);
          if (!safe_time_status.ok() && safe_time_status.status().IsNotFound()) {
            return true;
          }
          return false;
        },
        safe_time_propagation_timeout_,
        Format("Wait for safe_time to get removed"));
  }

 protected:
  MonoDelta safe_time_propagation_timeout_;
  YBTableName producer_table_name_;
  YBTableName consumer_table_name_;
  std::shared_ptr<YBTable> producer_table_;
  std::shared_ptr<YBTable> consumer_table_;
  string namespace_id_;
  std::shared_ptr<tablet::TabletPeer> producer_tablet_peer_;
  tserver::TabletServer* consumer_leader_tserver_;
};

TEST_F(XClusterSafeTimeTest, ComputeSafeTime) {
  // 1. Make sure safe time is initialized.
  auto ht_1 = ASSERT_RESULT(producer_tablet_peer_->LeaderSafeTime());
  ASSERT_OK(WaitForSafeTime(ht_1));

  // 2. Write some data
  LOG(INFO) << "Writing records for table " << producer_table_->name().ToString();
  WriteWorkload(0, 100, producer_client(), producer_table_->name());
  auto ht_2 = ASSERT_RESULT(producer_tablet_peer_->LeaderSafeTime());

  // 3. Make sure safe time has progressed
  ASSERT_OK(WaitForSafeTime(ht_2));
  ASSERT_TRUE(VerifyWrittenRecords());

  // 4. Make sure safe time is cleaned up when we pause replication
  ASSERT_OK(ToggleUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, false));
  ASSERT_OK(WaitForNotFoundSafeTime());

  // 5.  Make sure safe time is reset when we resume replication
  ASSERT_OK(ToggleUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, true));
  auto ht_4 = ASSERT_RESULT(producer_tablet_peer_->LeaderSafeTime());
  ASSERT_OK(WaitForSafeTime(ht_4));

  // 6.  Make sure safe time is cleaned up when we delete replication
  ASSERT_OK(DeleteUniverseReplication());
  ASSERT_OK(VerifyUniverseReplicationDeleted(
      consumer_cluster(), consumer_client(), kUniverseId, FLAGS_cdc_read_rpc_timeout_ms * 2));
  ASSERT_OK(WaitForNotFoundSafeTime());
}

TEST_F(XClusterSafeTimeTest, LagInSafeTime) {
  // 1. Make sure safe time is initialized.
  auto ht_1 = ASSERT_RESULT(producer_tablet_peer_->LeaderSafeTime());
  ASSERT_OK(WaitForSafeTime(ht_1));

  // 2. Simulate replication lag and make sure safe time does not move
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulate_have_more_records) = true;
  WriteWorkload(0, 100, producer_client(), producer_table_->name());
  auto ht_2 = ASSERT_RESULT(producer_tablet_peer_->LeaderSafeTime());

  // 3. Make sure safe time has not progressed beyond the write
  ASSERT_NOK(WaitForSafeTime(ht_2));

  // 4. Make sure safe time has progressed
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulate_have_more_records) = false;
  ASSERT_OK(WaitForSafeTime(ht_2));
}

}  // namespace enterprise
}  // namespace yb
