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

#include <algorithm>
#include <boost/algorithm/string/join.hpp>

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/cdc/cdc_state_table.h"

#include "yb/client/client_fwd.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"
#include "yb/dockv/partition.h"
#include "yb/common/ql_value.h"
#include "yb/common/wire_protocol.h"
#include "yb/docdb/docdb_test_util.h"
#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/tablet-split-itest-base.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tools/admin-test-base.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/logging.h"
#include "yb/util/thread.h"
#include "yb/util/tostring.h"
#include "yb/util/tsan_util.h"

using std::string;
using std::min;

DECLARE_int32(cdc_state_table_num_tablets);
DECLARE_bool(enable_tablet_split_of_xcluster_replicated_tables);
DECLARE_uint64(snapshot_coordinator_poll_interval_ms);
DECLARE_bool(TEST_validate_all_tablet_candidates);
DECLARE_bool(TEST_xcluster_consumer_fail_after_process_split_op);
DECLARE_int32(cdc_parent_tablet_deletion_task_retry_secs);
DECLARE_bool(enable_tablet_split_of_xcluster_bootstrapping_tables);
DECLARE_int32(cdc_state_checkpoint_update_interval_ms);
DECLARE_bool(enable_collect_cdc_metrics);
DECLARE_int32(update_metrics_interval_ms);

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_int64(tablet_split_low_phase_shard_count_per_node);
DECLARE_int64(tablet_split_low_phase_size_threshold_bytes);
DECLARE_int64(tablet_force_split_threshold_bytes);
DECLARE_int64(db_write_buffer_size);

namespace yb {
using test::Partitioning;

template <class TabletSplitBase>
class XClusterTabletSplitITestBase : public TabletSplitBase {
  using MiniClusterType = typename std::conditional<
      std::is_same<TabletSplitITest, TabletSplitBase>::value,
      MiniCluster,
      ExternalMiniCluster>::type;
 protected:
  Status SetupReplication(const string& bootstrap_id = "") {
    SwitchToProducer();
    VERIFY_RESULT(tools::RunAdminToolCommand(
        consumer_cluster_->GetMasterAddresses(), "setup_universe_replication", kProducerClusterId,
        TabletSplitBase::cluster_->GetMasterAddresses(), TabletSplitBase::table_->id(),
        bootstrap_id));
    return Status::OK();
  }

  Result<string> BootstrapProducer() {
    SwitchToProducer();
    const int kStreamUuidLength = 32;
    string output = VERIFY_RESULT(tools::RunAdminToolCommand(
        TabletSplitBase::cluster_->GetMasterAddresses(),
        "bootstrap_cdc_producer",
        TabletSplitBase::table_->id()));
    // Get the bootstrap id (output format is "table id: 123, CDC bootstrap id: 123\n").
    string bootstrap_id = output.substr(output.find_last_of(' ') + 1, kStreamUuidLength);
    return bootstrap_id;
  }

  Status CheckForNumRowsOnConsumer(size_t expected_num_rows) {
    const auto timeout = MonoDelta::FromSeconds(60 * kTimeMultiplier);
    client::YBClient* consumer_client(
        consumer_cluster_ ? consumer_client_.get() : TabletSplitBase::client_.get());
    client::TableHandle* consumer_table(
        consumer_cluster_ ? &consumer_table_ : &(TabletSplitBase::table_));

    auto consumer_session = consumer_client->NewSession(timeout);
    size_t num_rows = 0;
    Status s = WaitFor([&]() -> Result<bool> {
      auto num_rows_result = CountRows(consumer_session, *consumer_table);
      if (!num_rows_result.ok()) {
        LOG(WARNING) << "Encountered error during CountRows " << num_rows_result;
        return false;
      }
      num_rows = num_rows_result.get();
      return num_rows == expected_num_rows;
    }, timeout, "Wait for data to be replicated");

    LOG(INFO) << "Found " << num_rows << " rows on consumer, expected " << expected_num_rows;

    return s;
  }

  virtual void SwitchToProducer() {
    if (!producer_cluster_) {
      return;
    }
    // cluster_ is currently the consumer.
    consumer_cluster_ = std::move(TabletSplitBase::cluster_);
    consumer_client_ = std::move(TabletSplitBase::client_);
    consumer_table_ = std::move(TabletSplitBase::table_);
    TabletSplitBase::cluster_ = std::move(producer_cluster_);
    TabletSplitBase::client_ = std::move(producer_client_);
    TabletSplitBase::table_ = std::move(producer_table_);
    LOG(INFO) << "Swapped to the producer cluster.";
  }

  virtual void SwitchToConsumer() {
    if (!consumer_cluster_) {
      return;
    }
    // cluster_ is currently the producer.
    producer_cluster_ = std::move(TabletSplitBase::cluster_);
    producer_client_ = std::move(TabletSplitBase::client_);
    producer_table_ = std::move(TabletSplitBase::table_);
    TabletSplitBase::cluster_ = std::move(consumer_cluster_);
    TabletSplitBase::client_ = std::move(consumer_client_);
    TabletSplitBase::table_ = std::move(consumer_table_);
    LOG(INFO) << "Swapped to the consumer cluster.";
  }

  // Only one set of these is valid at any time.
  // The other cluster is accessible via cluster_ / client_ / table_.
  std::unique_ptr<MiniClusterType> consumer_cluster_;
  std::unique_ptr<client::YBClient> consumer_client_;
  client::TableHandle consumer_table_;

  std::unique_ptr<MiniClusterType> producer_cluster_;
  std::unique_ptr<client::YBClient> producer_client_;
  client::TableHandle producer_table_;

  const string kProducerClusterId = "producer";
};


class CdcTabletSplitITest : public XClusterTabletSplitITestBase<TabletSplitITest> {
 public:
  void SetUp() override {
    google::SetVLOGLevel("cdc*", 4);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_table_num_tablets) = 1;
    // Set before creating tests so that the first run doesn't wait 30s.
    // Lowering to 5s here to speed up tests.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 5;
    TabletSplitITest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_xcluster_replicated_tables) = true;

    CreateSingleTablet();
  }

 protected:
  Status WaitForCdcStateTableToBeReady() {
    return WaitFor([&]() -> Result<bool> {
      master::IsCreateTableDoneRequestPB is_create_req;
      master::IsCreateTableDoneResponsePB is_create_resp;

      is_create_req.mutable_table()->set_table_name(cdc::kCdcStateTableName);
      is_create_req.mutable_table()->mutable_namespace_()->set_name(master::kSystemNamespaceName);
      master::MasterDdlProxy master_proxy(
          &client_->proxy_cache(), VERIFY_RESULT(cluster_->GetLeaderMasterBoundRpcAddr()));
      rpc::RpcController rpc;
      rpc.set_timeout(MonoDelta::FromSeconds(30));

      auto s = master_proxy.IsCreateTableDone(is_create_req, &is_create_resp, &rpc);
      return s.ok() && !is_create_resp.has_error() && is_create_resp.done();
    }, MonoDelta::FromSeconds(30), "Wait for cdc_state table creation to finish");
  }

  Result<std::unique_ptr<MiniCluster>> CreateNewUniverseAndTable(
      const string& cluster_id, const string& cluster_prefix, client::TableHandle* table) {
    TEST_SetThreadPrefixScoped prefix_se(cluster_prefix);

    // First create the new cluster.
    MiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    opts.cluster_id = cluster_id;
    std::unique_ptr<MiniCluster> cluster = std::make_unique<MiniCluster>(opts);
    RETURN_NOT_OK(cluster->Start());
    RETURN_NOT_OK(cluster->WaitForTabletServerCount(3));
    auto cluster_client = VERIFY_RESULT(cluster->CreateClient());

    // Create an identical table on the new cluster.
    client::kv_table_test::CreateTable(
        client::Transactional(GetIsolationLevel() != IsolationLevel::NON_TRANSACTIONAL),
        NumTablets(),  // num_tablets
        cluster_client.get(),
        table);
    return cluster;
  }

  Status GetChangesWithRetries(
      cdc::CDCServiceProxy* cdc_proxy, const cdc::GetChangesRequestPB& change_req,
      cdc::GetChangesResponsePB* change_resp) {
    // Retry on LeaderNotReadyToServe errors.
    return WaitFor(
        [&]() -> Result<bool> {
          rpc::RpcController rpc;
          auto status = cdc_proxy->GetChanges(change_req, change_resp, &rpc);

          if (status.ok() && change_resp->has_error()) {
            status = StatusFromPB(change_resp->error().status());
          }

          if (status.IsLeaderNotReadyToServe()) {
            return false;
          }

          RETURN_NOT_OK(status);
          return true;
        },
        60s * kTimeMultiplier,
        "GetChanges timed out waiting for Leader to get ready");
  }
};

TEST_F(CdcTabletSplitITest, GetChangesOnSplitParentTablet) {
  docdb::DisableYcqlPackedRow();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  constexpr auto kNumRows = kDefaultNumRows;
  // Create a cdc stream for this tablet.
  auto cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(&client_->proxy_cache(),
      HostPort::FromBoundEndpoint(cluster_->mini_tablet_servers().front()->bound_rpc_addr()));
  auto stream_id = ASSERT_RESULT(cdc::CreateCDCStream(cdc_proxy, table_->id()));
  // Ensure that the cdc_state table is ready before inserting rows and splitting.
  ASSERT_OK(WaitForCdcStateTableToBeReady());

  LOG(INFO) << "Created a CDC stream for table " << table_.name().table_name()
            << " with stream id " << stream_id;

  // Write some rows to the tablet.
  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));
  const auto source_tablet_id = ASSERT_RESULT(SplitTabletAndValidate(
      split_hash_code, kNumRows, /* parent_tablet_protected_from_deletion */ true));

  // Ensure that a GetChanges still works on the source tablet.
  cdc::GetChangesRequestPB change_req;
  cdc::GetChangesResponsePB change_resp;

  change_req.set_tablet_id(source_tablet_id);
  change_req.set_stream_id(stream_id.ToString());
  // Skip over the initial schema ops, to fetch the SPLIT_OP and trigger cdc_state children updates.
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(2);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(0);

  // Might need to retry since we are performing stepdowns and thus could get LeaderNotReadyToServe.
  ASSERT_OK(GetChangesWithRetries(cdc_proxy.get(), change_req, &change_resp));

  // Test that if the tablet leadership of the parent tablet changes we can still call GetChanges.
  StepDownAllTablets(cluster_.get());

  ASSERT_OK(GetChangesWithRetries(cdc_proxy.get(), change_req, &change_resp));

  // Also verify that the entries in cdc_state for the children tablets are properly initialized.
  // They should have the checkpoint set to the split_op, but not have any replication times yet as
  // they have not been polled for yet.
  const auto child_tablet_ids = ListActiveTabletIdsForTable(cluster_.get(), table_->id());
  cdc::CDCStateTable cdc_state_table(client_.get());
  Status s;
  int children_found = 0;
  OpId split_op_checkpoint;
  for (auto row_result : ASSERT_RESULT(
           cdc_state_table.GetTableRange(cdc::CDCStateTableEntrySelector().IncludeAll(), &s))) {
    ASSERT_OK(row_result);
    auto& row = *row_result;
    if (child_tablet_ids.contains(row.key.tablet_id)) {
      ASSERT_TRUE(row.checkpoint);
      ASSERT_GT(row.checkpoint->index, 0);
      ++children_found;
      if (split_op_checkpoint.empty()) {
        split_op_checkpoint = *row.checkpoint;
      } else {
        // Verify that both children have the same checkpoint set.
        ASSERT_EQ(*row.checkpoint, split_op_checkpoint);
      }
    }
  }
  ASSERT_OK(s);
  ASSERT_EQ(children_found, 2);

  // Now let the parent tablet get deleted by the background task.
  // To do so, we need to issue a GetChanges to both children tablets.
  for (const auto& child_tablet_id : child_tablet_ids) {
    cdc::GetChangesRequestPB child_change_req;
    cdc::GetChangesResponsePB child_change_resp;
    child_change_req.set_tablet_id(child_tablet_id);
    child_change_req.set_stream_id(stream_id.ToString());

    ASSERT_OK(GetChangesWithRetries(cdc_proxy.get(), child_change_req, &child_change_resp));
    // Ensure that we get back no records since nothing has been written to the children and we
    // shouldn't be re-replicating any rows from our parent.
    ASSERT_EQ(child_change_resp.records_size(), 0);
    ASSERT_GT(child_change_resp.checkpoint().op_id().index(), split_op_checkpoint.index);
  }

  SleepFor(MonoDelta::FromMilliseconds(2 * FLAGS_snapshot_coordinator_poll_interval_ms));

  // Try to do a GetChanges again, it should fail due to not finding the deleted parent tablet.
  rpc::RpcController rpc;
  ASSERT_NOK(GetChangesWithRetries(cdc_proxy.get(), change_req, &change_resp));
  ASSERT_TRUE(change_resp.has_error());
  const auto status = StatusFromPB(change_resp.error().status());
  // Depending on if the parent tablet has been inputted into every cdc_service's tablet_checkpoint_
  // map, we may either return NotFound (pass all CheckTabletValidForStream checks, but then can't
  // find tablet) or TabletSplit (from failing CheckTabletValidForStream on some tserver since the
  // tablet can't be found).
  // Either of these statuses is fine and means that this tablet no longer exists and was deleted.
  LOG(INFO) << "GetChanges status: " << status;
  ASSERT_TRUE(status.IsNotFound() || status.IsTabletSplit());
}

// For testing xCluster setups. Since most test utility functions expect there to be only one
// cluster, they implicitly use cluster_ / client_ / table_ everywhere. For this test, we default
// those to point to the producer cluster, but allow calls to SwitchToProducer/Consumer, to swap
// those to point to the other cluster.
class XClusterTabletSplitITest : public CdcTabletSplitITest {
 public:
  void SetUp() override {
    {
      TEST_SetThreadPrefixScoped prefix_se("P");
      CdcTabletSplitITest::SetUp();
    }

    // Also create the consumer cluster.
    consumer_cluster_ = ASSERT_RESULT(CreateNewUniverseAndTable("consumer", "C", &consumer_table_));

    consumer_client_ = ASSERT_RESULT(consumer_cluster_->CreateClient());

    ASSERT_OK(SetupReplication());
  }

  void DeleteReplication() {
    SwitchToProducer();
    ASSERT_OK(tools::RunAdminToolCommand(
        consumer_cluster_->GetMasterAddresses(), "delete_universe_replication",
        kProducerClusterId));
  }

 protected:
  void DoBeforeTearDown() override {
    // Stop trying to process metrics when shutting down.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_collect_cdc_metrics) = false;
    ValidateOverlap();
    DeleteReplication();

    SwitchToConsumer();

    {
      TEST_SetThreadPrefixScoped prefix_se("C");
      cluster_->Shutdown();
    }

    SwitchToProducer();
    CdcTabletSplitITest::DoBeforeTearDown();
  }

  void DoTearDown() override {
    TEST_SetThreadPrefixScoped prefix_se("P");
    CdcTabletSplitITest::DoTearDown();
  }

  Status WaitForOngoingSplitsToComplete(bool wait_for_parent_deletion) {
    auto master_admin_proxy = std::make_unique<master::MasterAdminProxy>(
        proxy_cache_.get(), client_->GetMasterLeaderAddress());
    RETURN_NOT_OK(WaitFor(
        std::bind(&TabletSplitITestBase::IsSplittingComplete, this, master_admin_proxy.get(),
                  wait_for_parent_deletion),
        30s, "Wait for ongoing tablet splits to complete."));
    return Status::OK();
  }

  Status SplitAllTablets(
      int cur_num_tablets, bool parent_tablet_protected_from_deletion = false) {
    // Splits all tablets for cluster_.
    auto* catalog_mgr = VERIFY_RESULT(catalog_manager());
    // Wait for parents to be hidden before trying to split children.
    RETURN_NOT_OK(WaitForOngoingSplitsToComplete(/* wait_for_parent_deletion */ true));
    auto tablet_ids = ListActiveTabletIdsForTable(cluster_.get(), table_->id());
    EXPECT_EQ(tablet_ids.size(), cur_num_tablets);
    for (const auto& tablet_id : tablet_ids) {
      RETURN_NOT_OK(catalog_mgr->SplitTablet(
          tablet_id, master::ManualSplit::kTrue, catalog_mgr->GetLeaderEpochInternal()));
    }
    size_t expected_non_split_tablets = cur_num_tablets * 2;
    size_t expected_split_tablets = parent_tablet_protected_from_deletion
                                    ? cur_num_tablets * 2 - 1
                                    : 0;
    return WaitForTabletSplitCompletion(expected_non_split_tablets, expected_split_tablets);
  }

  auto GetConsumerMap() {
    auto& cm = EXPECT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
    auto cluster_info = EXPECT_RESULT(cm.GetClusterConfig());
    auto producer_map = cluster_info.mutable_consumer_registry()->mutable_producer_map();
    auto it = producer_map->find(kProducerClusterId);
    EXPECT_NE(it, producer_map->end());
    EXPECT_EQ(it->second.stream_map().size(), 1);
    return it->second.stream_map().begin()->second.consumer_producer_tablet_map();
  }

  void ValidateOverlap() {
    const auto timeout = MonoDelta::FromSeconds(60 * kTimeMultiplier);
    SwitchToProducer();
    // Just need to get all active tablets, since leaders may be moving.
    auto producer_tablet_peers = ListTableActiveTabletPeers(
        cluster_.get(), table_->name().table_id());
    std::unordered_set<TabletId> producer_tablet_ids;
    for (const auto& peer : producer_tablet_peers) {
      producer_tablet_ids.insert(peer->tablet_id());
    }
    size_t producer_tablet_count = producer_tablet_ids.size();

    SwitchToConsumer();
    ASSERT_OK(cdc::CorrectlyPollingAllTablets(cluster_.get(), producer_tablet_count, timeout));
    auto consumer_tablet_peers = ListTableActiveTabletPeers(
        cluster_.get(), table_->name().table_id());
    std::unordered_set<TabletId> consumer_tablet_ids;
    for (const auto& peer : consumer_tablet_peers) {
      consumer_tablet_ids.insert(peer->tablet_id());
    }
    size_t consumer_tablet_count = consumer_tablet_ids.size();

    auto tablet_map = GetConsumerMap();
    LOG(INFO) << "Consumer Map: \n";
    for (const auto& elem : tablet_map) {
      std::vector<string> start_keys, end_keys;
      std::transform(
          elem.second.start_key().begin(), elem.second.start_key().end(),
          std::back_inserter(start_keys),
          [](std::string s) -> string { return Slice(s).ToDebugHexString(); });
      std::transform(
          elem.second.end_key().begin(), elem.second.end_key().end(), std::back_inserter(end_keys),
          [](std::string s) -> string { return Slice(s).ToDebugHexString(); });

      LOG(INFO) << elem.first << ", [" << boost::algorithm::join(elem.second.tablets(), ",")
                << "], [" << boost::algorithm::join(start_keys, ",") << "], ["
                << boost::algorithm::join(end_keys, ",") << "]\n";
    }
    ASSERT_LE(tablet_map.size(), min(producer_tablet_count, consumer_tablet_count));

    int producer_tablets = 0;
    for (auto& mapping : tablet_map) {
      auto consumer_tablet = std::find_if(
          consumer_tablet_peers.begin(), consumer_tablet_peers.end(),
          [&](const auto& tablet) { return tablet->tablet_id() == mapping.first; });
      ASSERT_NE(consumer_tablet, consumer_tablet_peers.end());

      for (auto& mapped_producer_tablet : mapping.second.tablets()) {
        producer_tablets++;
        auto producer_tablet = std::find_if(
            producer_tablet_peers.begin(), producer_tablet_peers.end(),
            [&](const auto& tablet) { return tablet->tablet_id() == mapped_producer_tablet; });
        ASSERT_NE(producer_tablet, producer_tablet_peers.end());

        ASSERT_TRUE(dockv::PartitionSchema::HasOverlap(
            (*consumer_tablet)->tablet_metadata()->partition()->partition_key_start(),
            (*consumer_tablet)->tablet_metadata()->partition()->partition_key_end(),
            (*producer_tablet)->tablet_metadata()->partition()->partition_key_start(),
            (*producer_tablet)->tablet_metadata()->partition()->partition_key_end()));
      }
    }

    ASSERT_EQ(producer_tablets, producer_tablet_count);
  }
};

class xClusterTabletMapTest : public XClusterTabletSplitITest,
                              public testing::WithParamInterface<Partitioning> {
 public:
  void SetUp() override {}

  void RunSetUp(int producer_tablet_count, int consumer_tablet_count) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_table_num_tablets) = 1;

    {
      TEST_SetThreadPrefixScoped prefix_se("P");
      TabletSplitITest::SetUp();
    }

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_xcluster_replicated_tables) = true;

    SetNumTablets(producer_tablet_count);
    Schema schema;
    client::kv_table_test::BuildSchema(GetParam(), &schema);
    schema.mutable_table_properties()->SetTransactional(
        GetIsolationLevel() != IsolationLevel::NON_TRANSACTIONAL);
    ASSERT_OK(client::kv_table_test::CreateTable(schema, NumTablets(), client_.get(), &table_));

    SetNumTablets(consumer_tablet_count);
    // Also create the consumer cluster.
    // First create the new cluster.
    MiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    opts.cluster_id = "consumer";
    consumer_cluster_ = std::make_unique<MiniCluster>(opts);
    {
      TEST_SetThreadPrefixScoped prefix_se("C");
      ASSERT_OK(consumer_cluster_->Start());
    }

    ASSERT_OK(consumer_cluster_->WaitForTabletServerCount(3));
    consumer_client_ = ASSERT_RESULT(consumer_cluster_->CreateClient());

    // Create an identical table on the new cluster.
    ASSERT_OK(client::kv_table_test::CreateTable(
        schema,
        NumTablets(),  // num_tablets
        consumer_client_.get(),
        &consumer_table_));

    ASSERT_OK(SetupReplication());
    ASSERT_RESULT(WriteRowsAndFlush(kDefaultNumRows, 1));
  }
};

// ValidateOverlap() is called before teardown for all these tests.
TEST_P(xClusterTabletMapTest, SingleTableCountMapTest) {
  RunSetUp(1, 1);
}

TEST_P(xClusterTabletMapTest, SameTableCountMapTest) {
  RunSetUp(4, 4);
}

TEST_P(xClusterTabletMapTest, MoreProducerTablets) {
  RunSetUp(8, 2);
}

TEST_P(xClusterTabletMapTest, MoreConsumerTablets) {
  RunSetUp(3, 8);
}

TEST_F(XClusterTabletSplitITest, SplittingWithXClusterReplicationOnConsumer) {
  // Perform a split on the consumer side and ensure replication still works.

  // To begin with, cluster_ will be our producer.
  // Write some rows to the producer.
  auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kDefaultNumRows));

  // Wait until the rows are all replicated on the consumer.
  ASSERT_OK(CheckForNumRowsOnConsumer(kDefaultNumRows));

  SwitchToConsumer();

  // Perform a split on the CONSUMER cluster.
  ASSERT_OK(SplitTabletAndValidate(split_hash_code, kDefaultNumRows));

  SwitchToProducer();

  // Write another set of rows, and make sure the new poller picks up on the changes.
  ASSERT_RESULT(WriteRows(kDefaultNumRows, kDefaultNumRows + 1));

  ASSERT_OK(CheckForNumRowsOnConsumer(2 * kDefaultNumRows));
}

TEST_F(XClusterTabletSplitITest, SplittingWithXClusterReplicationOnProducer) {
  // Perform a split on the producer side and ensure replication still works.

  // Default cluster_ will be our producer.
  auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kDefaultNumRows));

  // Wait until the rows are all replicated on the consumer.
  ASSERT_OK(CheckForNumRowsOnConsumer(kDefaultNumRows));

  // Split the tablet on the producer.
  ASSERT_OK(SplitTabletAndValidate(split_hash_code, kDefaultNumRows));

  // Write another set of rows, and make sure the consumer picks up on the changes.
  ASSERT_RESULT(WriteRows(kDefaultNumRows, kDefaultNumRows + 1));

  ASSERT_OK(CheckForNumRowsOnConsumer(2 * kDefaultNumRows));
}

TEST_F(XClusterTabletSplitITest, MultipleSplitsDuringPausedReplication) {
  google::SetVLOGLevel("cdc*", 4);
  // Simulate network partition with paused replication, then perform multiple splits on producer
  // before re-enabling replication. Should be able to handle all of the splits.

  // Default cluster_ will be our producer.
  // Start with replication disabled.
  ASSERT_OK(tools::RunAdminToolCommand(
      consumer_cluster_->GetMasterAddresses(), "set_universe_replication_enabled",
      kProducerClusterId, "0"));

  // Perform one tablet split.
  auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kDefaultNumRows));
  ASSERT_OK(SplitTabletAndValidate(
      split_hash_code, kDefaultNumRows, /* parent_tablet_protected_from_deletion */ true));

  // Write some more rows, and then perform another split on both children.
  ASSERT_RESULT(WriteRows(kDefaultNumRows, kDefaultNumRows + 1));
  ASSERT_OK(SplitAllTablets(
      /* cur_num_tablets */ 2, /* parent_tablet_protected_from_deletion */ true));
  ASSERT_RESULT(WriteRows(kDefaultNumRows, 2 * kDefaultNumRows + 1));
  ASSERT_OK(SplitAllTablets(
      /* cur_num_tablets */ 4, /* parent_tablet_protected_from_deletion */ true));

  // Now re-enable replication.
  ASSERT_OK(tools::RunAdminToolCommand(
      consumer_cluster_->GetMasterAddresses(), "set_universe_replication_enabled",
      kProducerClusterId, "1"));

  // Ensure all the rows are all replicated on the consumer.
  ASSERT_OK(CheckForNumRowsOnConsumer(3 * kDefaultNumRows));

  // Write another set of rows, and make sure the consumer picks up on the changes.
  ASSERT_RESULT(WriteRows(kDefaultNumRows, 3 * kDefaultNumRows + 1));

  ASSERT_OK(CheckForNumRowsOnConsumer(4 * kDefaultNumRows));

  // Check that parent tablets get deleted once children begin being polled for.
  ASSERT_OK(WaitForTabletSplitCompletion(8));
}

TEST_F(XClusterTabletSplitITest, MultipleSplitsInSequence) {
  // Handle case where there are multiple SPLIT_OPs immediately after each other.
  // This is to test when we receive an older SPLIT_OP that has already been processed, and its
  // children have also been processed - see the "Unable to find matching source tablet" warning.

  // Default cluster_ will be our producer.
  ASSERT_RESULT(WriteRowsAndFlush(kDefaultNumRows));

  ASSERT_OK(CheckForNumRowsOnConsumer(kDefaultNumRows));

  // Perform one tablet split.
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 1));

  // Perform another tablet split immediately after.
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 2));

  // Write some more rows and check that everything is replicated correctly.
  ASSERT_RESULT(WriteRows(kDefaultNumRows, kDefaultNumRows + 1));
  ASSERT_OK(CheckForNumRowsOnConsumer(2 * kDefaultNumRows));
}

TEST_F(XClusterTabletSplitITest, SplittingOnProducerAndConsumer) {
  // Test splits on both producer and consumer while writes to the producer are happening.

  // Default cluster_ will be our producer.
  // Start by writing some rows and waiting for them to be replicated.
  ASSERT_RESULT(WriteRowsAndFlush(kDefaultNumRows));
  ASSERT_OK(CheckForNumRowsOnConsumer(kDefaultNumRows));

  // Setup a new thread for continuous writing to producer.
  std::atomic<bool> stop(false);
  std::thread write_thread([this, &stop] {
    CDSAttacher attacher;
    client::TableHandle producer_table;
    ASSERT_OK(producer_table.Open(table_->name(), client_.get()));
    auto producer_session = client_->NewSession(60s);
    int32_t key = kDefaultNumRows + 1;
    while (!stop) {
      auto res = client::kv_table_test::WriteRow(
          &producer_table, producer_session, key, key,
          client::WriteOpType::INSERT, client::Flush::kTrue);
      if (!res.ok() && res.status().IsNotFound()) {
        LOG(INFO) << "Encountered NotFound error on write : " << res;
      } else {
        ASSERT_OK(res);
        key++;
      }
    }
  });

  // Perform tablet splits on both sides.
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 1));
  SwitchToConsumer();
  ASSERT_OK(FlushTestTable());
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 1));
  SwitchToProducer();
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 2));
  SwitchToConsumer();
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 2));
  SwitchToProducer();

  // Stop writes.
  stop.store(true, std::memory_order_release);
  write_thread.join();

  // Verify that both sides have the same number of rows.
  client::YBSessionPtr producer_session = client_->NewSession(60s);
  size_t num_rows = ASSERT_RESULT(CountRows(producer_session, table_));

  ASSERT_OK(CheckForNumRowsOnConsumer(num_rows));
}

TEST_F(XClusterTabletSplitITest, ConsumerClusterFailureWhenProcessingSplitOp) {
  ASSERT_RESULT(WriteRowsAndFlush(kDefaultNumRows));
  ASSERT_OK(CheckForNumRowsOnConsumer(kDefaultNumRows));

  // Force consumer to fail after processing the split op.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_consumer_fail_after_process_split_op) = true;

  // Perform a split.
  // Since the SPLIT_OP is not being processed yet, the parent tablet should still be present.
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 1,
                            /* parent_tablet_protected_from_deletion */ true));
  // Write some additional rows.
  ASSERT_RESULT(WriteRows(kDefaultNumRows, kDefaultNumRows + 1));

  // Wait for a bit, as the consumer keeps trying to process the split_op but fails.
  SleepFor(10s);
  // Check that these new rows aren't replicated since we're stuck on the split_op.
  ASSERT_OK(CheckForNumRowsOnConsumer(kDefaultNumRows));

  // Allow for the split op to be processed properly, and check that everything is replicated.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_consumer_fail_after_process_split_op) = false;
  ASSERT_OK(CheckForNumRowsOnConsumer(2 * kDefaultNumRows));

  // Verify that parent tablet got deleted once children were polled for.
  ASSERT_OK(WaitForTabletSplitCompletion(2));

  ASSERT_RESULT(WriteRows(kDefaultNumRows, 2 * kDefaultNumRows + 1));
  ASSERT_OK(CheckForNumRowsOnConsumer(3 * kDefaultNumRows));
}

class XClusterTabletSplitMetricsTest : public XClusterTabletSplitITest {
 public:
  void SetUp() override {
    // Update metrics more frequently.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_metrics_interval_ms) = 1000;
    XClusterTabletSplitITest::SetUp();
  }

  std::pair<int64_t, int64_t> FetchMaxReplicationLag(const xrepl::StreamId stream_id) {
    int64_t committed_lag_micros = 0, sent_lag_micros = 0;
    auto tablet_ids = ListTabletIdsForTable(cluster_.get(), table_->id());

    for (auto tserver : cluster_->mini_tablet_servers()) {
      auto cdc_service = tserver->server()->GetCDCService();

      for (const auto& tablet_id : tablet_ids) {
        // Fetch the metrics, but ensure we don't create new metric entities.
        const auto metrics = GetXClusterTabletMetrics(
            *cdc_service, tablet_id, stream_id, cdc::CreateMetricsEntityIfNotFound::kFalse);
        if (metrics) {
          committed_lag_micros = std::max(
              committed_lag_micros, metrics.get()->async_replication_committed_lag_micros->value());
          sent_lag_micros =
              std::max(sent_lag_micros, metrics.get()->async_replication_sent_lag_micros->value());
          LOG(INFO) << tablet_id << ": "
                    << metrics.get()->async_replication_committed_lag_micros->value() << " "
                    << metrics.get()->async_replication_sent_lag_micros->value();
        } else {
          LOG(INFO) << tablet_id << ": no metrics";
        }
      }
    }
    return std::make_pair(committed_lag_micros, sent_lag_micros);
  }

  Result<xrepl::StreamId> GetCDCStreamID(const std::string& producer_table_id) {
    master::ListCDCStreamsResponsePB stream_resp;

    RETURN_NOT_OK(LoggedWaitFor(
        [this, producer_table_id, &stream_resp]() -> Result<bool> {
          master::ListCDCStreamsRequestPB req;
          req.set_table_id(producer_table_id);
          stream_resp.Clear();

          auto leader_mini_master = cluster_->GetLeaderMiniMaster();
          if (!leader_mini_master.ok()) {
            return false;
          }
          Status s = (*leader_mini_master)->catalog_manager().ListCDCStreams(&req, &stream_resp);
          return s.ok() && !stream_resp.has_error() && stream_resp.streams_size() == 1;
        },
        kRpcTimeout, "Get CDC stream for table"));

    if (stream_resp.streams(0).table_id().Get(0) != producer_table_id) {
      return STATUS(
          IllegalState, Format(
                            "Expected table id $0, have $1", producer_table_id,
                            stream_resp.streams(0).table_id().Get(0)));
    }
    return xrepl::StreamId::FromString(stream_resp.streams(0).stream_id());
  }
};

TEST_F(XClusterTabletSplitMetricsTest, VerifyReplicationLagMetricsOnChildren) {
  // Perform a split on the producer side and ensure that replication lag metrics continue growing.

  // Default cluster_ will be our producer.
  auto stream_id = ASSERT_RESULT(GetCDCStreamID(table_->id()));
  auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kDefaultNumRows));

  // Wait until the rows are all replicated on the consumer.
  ASSERT_OK(CheckForNumRowsOnConsumer(kDefaultNumRows));

  // Pause replication.
  ASSERT_OK(tools::RunAdminToolCommand(
      consumer_cluster_->GetMasterAddresses(), "set_universe_replication_enabled",
      kProducerClusterId, "0"));

  {
    auto [committed_lag_micros, sent_lag_micros] = FetchMaxReplicationLag(stream_id);
    LOG(INFO) << "Replication lag is : " << committed_lag_micros << ", " << sent_lag_micros;
    ASSERT_EQ(committed_lag_micros, 0);
    ASSERT_EQ(sent_lag_micros, 0);
  }

  SleepFor(FLAGS_update_metrics_interval_ms * 2ms);  // Wait for metrics to update.
  {
    auto [committed_lag_micros, sent_lag_micros] = FetchMaxReplicationLag(stream_id);
    LOG(INFO) << "Replication lag is : " << committed_lag_micros << ", " << sent_lag_micros;
    ASSERT_EQ(committed_lag_micros, 0);
    ASSERT_EQ(sent_lag_micros, 0);
  }

  // Split the tablet on the producer.
  ASSERT_OK(
      SplitTabletAndValidate(split_hash_code, kDefaultNumRows, true /* keep_parent_tablet */));

  SleepFor(FLAGS_update_metrics_interval_ms * 2ms);  // Wait for metrics to update.
  auto [old_committed_lag_micros, old_sent_lag_micros] = FetchMaxReplicationLag(stream_id);
  LOG(INFO) << "Replication lag is : " << old_committed_lag_micros << ", " << old_sent_lag_micros;
  ASSERT_GT(old_committed_lag_micros, 0);
  ASSERT_GT(old_sent_lag_micros, 0);

  for (int i = 1; i <= 2; ++i) {
    // Write another set of rows, these will only go to the children tablets.
    ASSERT_RESULT(WriteRows(kDefaultNumRows, (i * kDefaultNumRows) + 1));
    if (i == 1) {
      ASSERT_OK(SplitAllTablets(2 * i /* cur_num_tablets */, true /* keep_parent_tablet */));
    }

    SleepFor(FLAGS_update_metrics_interval_ms * 2ms);  // Wait for metrics to update.
    {
      auto [new_committed_lag_micros, new_sent_lag_micros] = FetchMaxReplicationLag(stream_id);
      LOG(INFO) << "Replication lag is : " << new_committed_lag_micros << ", "
                << new_sent_lag_micros;
      ASSERT_GT(new_committed_lag_micros, old_committed_lag_micros);
      ASSERT_GT(new_sent_lag_micros, old_sent_lag_micros);
      old_committed_lag_micros = new_committed_lag_micros;
      old_sent_lag_micros = new_sent_lag_micros;
    }
  }

  // Resume replication, ensure that lag goes down to 0.
  ASSERT_OK(tools::RunAdminToolCommand(
      consumer_cluster_->GetMasterAddresses(), "set_universe_replication_enabled",
      kProducerClusterId, "1"));
  ASSERT_OK(CheckForNumRowsOnConsumer(3 * kDefaultNumRows));
  SleepFor(FLAGS_update_metrics_interval_ms * 2ms);  // Wait for metrics to update.
  {
    auto [committed_lag_micros, sent_lag_micros] = FetchMaxReplicationLag(stream_id);
    LOG(INFO) << "Replication lag is : " << committed_lag_micros << ", " << sent_lag_micros;
    ASSERT_EQ(committed_lag_micros, 0);
    ASSERT_EQ(sent_lag_micros, 0);
  }
}

class XClusterExternalTabletSplitITest :
    public XClusterTabletSplitITestBase<TabletSplitExternalMiniClusterITest> {
 public:
  void SetUp() override {
    this->mini_cluster_opt_.num_masters = num_masters();
    TabletSplitExternalMiniClusterITest::SetUp();

    // Also create the consumer cluster.
    this->mini_cluster_opt_.data_root_counter = 0;
    consumer_cluster_ = std::make_unique<ExternalMiniCluster>(this->mini_cluster_opt_);
    ASSERT_OK(consumer_cluster_->Start());
    consumer_client_ = ASSERT_RESULT(consumer_cluster_->CreateClient());
    LOG(INFO) << cluster_->num_masters();
    LOG(INFO) << consumer_cluster_->num_masters();

    // Create table on both sides.
    CreateSingleTablet();
    SwitchToConsumer();
    CreateSingleTablet();
    SwitchToProducer();

    ASSERT_OK(SetupReplication());
  }

  int num_masters() {
    // Need multiple masters to test master failovers.
    return 3;
  }

 protected:
  void SetFlags() override {
    TabletSplitExternalMiniClusterITest::SetFlags();
    mini_cluster_opt_.extra_master_flags.push_back(
        "--enable_tablet_split_of_xcluster_replicated_tables=true");
  }

  void DoBeforeTearDown() override {
    SwitchToConsumer();
    ASSERT_OK(tools::RunAdminToolCommand(
        cluster_->GetMasterAddresses(), "delete_universe_replication", kProducerClusterId));
    SleepFor(5s);
    cluster_->Shutdown();

    SwitchToProducer();
    XClusterTabletSplitITestBase<TabletSplitExternalMiniClusterITest>::DoBeforeTearDown();
  }

  Status WaitForMasterFailover(size_t original_master_leader_idx) {
    return WaitFor(
        [&]() -> Result<bool> {
          auto s = cluster_->GetLeaderMasterIndex();
          if (s.ok()) {
            return original_master_leader_idx != s.get();
          }
          LOG(WARNING) << "Encountered error while waiting for master failover: " << s;
          return false;
        },
        MonoDelta::FromSeconds(60), "Wait for master failover.");
  }
};

TEST_F(XClusterExternalTabletSplitITest, MasterFailoverDuringProducerPostSplitOps) {
  auto parent_tablet = ASSERT_RESULT(GetOnlyTestTabletId());
  // Set crash flag on producer master leader so that we force master failover.
  auto original_master_leader_idx = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());
  ASSERT_OK(cluster_->SetFlag(
      cluster_->GetLeaderMaster(), "TEST_fault_crash_after_registering_split_children", "1.0"));

  // Write some rows.
  ASSERT_RESULT(WriteRowsAndFlush(kDefaultNumRows));
  ASSERT_OK(CheckForNumRowsOnConsumer(kDefaultNumRows));

  // Enable automatic tablet splitting to trigger a split, and retry it after the failover.
  ASSERT_OK(cluster_->SetFlagOnMasters("enable_automatic_tablet_splitting", "true"));

  ASSERT_OK(WaitForMasterFailover(original_master_leader_idx));
  ASSERT_OK(WaitForTablets(3));

  // Verify that all the children tablets are present in cdc_state, parent may get deleted.
  auto tablet_ids = ASSERT_RESULT(GetTestTableTabletIds(0));
  tablet_ids.erase(parent_tablet);

  client::YBClient* producer_client(
      producer_cluster_ ? producer_client_.get() : client_.get());

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        std::unordered_set<TabletId> tablet_ids_map(tablet_ids.begin(), tablet_ids.end());
        cdc::CDCStateTable cdc_state_table(producer_client);
        Status s;
        for (auto row_result :
             VERIFY_RESULT(cdc_state_table.GetTableRange({} /* just key columns */, &s))) {
          RETURN_NOT_OK(row_result);
          auto& row = *row_result;
          tablet_ids_map.erase(row.key.tablet_id);
        }
        RETURN_NOT_OK(s);

        if (!tablet_ids_map.empty()) {
          LOG(WARNING) << "Did not find tablet_ids in system.cdc_state: "
                       << ToString(tablet_ids_map);
          return false;
        }
        return true;
      },
      MonoDelta(30s), "Wait for children entries in cdc_state."));

  // Verify that writes to children tablets are properly polled for.
  ASSERT_RESULT(WriteRowsAndFlush(kDefaultNumRows, kDefaultNumRows + 1));
  ASSERT_OK(CheckForNumRowsOnConsumer(2 * kDefaultNumRows));
}

TEST_F(XClusterExternalTabletSplitITest, MasterFailoverDuringConsumerPostSplitOps) {
  // Write some rows.
  ASSERT_RESULT(WriteRowsAndFlush(kDefaultNumRows));
  ASSERT_OK(CheckForNumRowsOnConsumer(kDefaultNumRows));

  // Set crash flag on consumer master leader so that we force master failover.
  SwitchToConsumer();
  auto original_master_leader_idx = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());
  ASSERT_OK(cluster_->SetFlag(
      cluster_->GetLeaderMaster(), "TEST_fault_crash_after_registering_split_children", "1.0"));

  ASSERT_OK(WaitForTestTableIntentsApplied());
  ASSERT_OK(FlushTestTable());

  // Enable automatic tablet splitting to trigger a split, and retry it after the failover.
  ASSERT_OK(cluster_->SetFlagOnMasters("enable_automatic_tablet_splitting", "true"));

  ASSERT_OK(WaitForMasterFailover(original_master_leader_idx));
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> { return VERIFY_RESULT(GetTestTableTabletIds()).size() >= 3; },
      20s * kTimeMultiplier, Format("Waiting for tablet count to be at least 3.")));

  // Verify that writes flow to the children tablets properly.
  SwitchToProducer();
  ASSERT_RESULT(WriteRowsAndFlush(kDefaultNumRows, kDefaultNumRows + 1));
  ASSERT_OK(CheckForNumRowsOnConsumer(2 * kDefaultNumRows));
}

class XClusterAutomaticTabletSplitITest : public XClusterTabletSplitITest {
 public:
  void SetUp() override {
    XClusterTabletSplitITest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 16;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_force_split_threshold_bytes) = 10_KB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) =
        FLAGS_tablet_force_split_threshold_bytes;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) =
        FLAGS_tablet_force_split_threshold_bytes;
  }

 protected:
  Result<size_t> GetNumActiveTablets(master::CatalogManagerIf* catalog_mgr) {
    master::GetTableLocationsResponsePB resp;
    master::GetTableLocationsRequestPB req;
    table_->name().SetIntoTableIdentifierPB(req.mutable_table());
    req.set_max_returned_locations(std::numeric_limits<int32_t>::max());
    RETURN_NOT_OK(catalog_mgr->GetTableLocations(&req, &resp));
    return resp.tablet_locations_size();
  }
};

// This test is very flaky in TSAN as we spend a long time waiting for children tablets to be
// ready, and will often then time out.
TEST_F(XClusterAutomaticTabletSplitITest, AutomaticTabletSplitting) {
  constexpr auto num_active_tablets = 6;

  // Setup a new thread for continuous writing to producer.
  std::atomic<bool> stop(false);
  int32_t rows_written = 0;
  std::thread write_thread([this, &stop, &rows_written] {
    CDSAttacher attacher;
    client::TableHandle producer_table;
    ASSERT_OK(producer_table.Open(table_->name(), client_.get()));
    auto producer_session = client_->NewSession(60s);
    while (!stop) {
      rows_written = (rows_written + 1);
      ASSERT_RESULT(client::kv_table_test::WriteRow(
          &producer_table, producer_session, rows_written, rows_written,
          client::WriteOpType::INSERT, client::Flush::kTrue));
    }
  });

  auto* catalog_mgr = ASSERT_RESULT(catalog_manager());
  ASSERT_OK(WaitFor([&]() {
    auto res = GetNumActiveTablets(catalog_mgr);
    if (!res.ok()) {
      LOG(WARNING) << "Found error fetching active tablets: " << res;
      return false;
    }
    YB_LOG_EVERY_N_SECS(INFO, 3) << "Number of active tablets: " << res.get();
    return res.get() >= num_active_tablets;
  }, 300s * kTimeMultiplier, "Wait for enough tablets to split"));

  // Stop writes.
  stop.store(true, std::memory_order_release);
  write_thread.join();

  LOG(INFO) << "Wrote " << rows_written << " rows to the producer cluster.";

  // Verify that both sides have the same number of rows.
  ASSERT_OK(CheckForNumRowsOnConsumer(rows_written));

  // Disable splitting before shutting down, to prevent more splits from occurring.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  // Wait for splitting to complete before validating overlaps.
  ASSERT_OK(WaitForOngoingSplitsToComplete(/* wait_for_parent_deletion */ false));
}

class XClusterBootstrapTabletSplitITest : public XClusterTabletSplitITest {
 public:
  void SetUp() override {
    CdcTabletSplitITest::SetUp();

    // Create the consumer cluster, but don't setup the universe replication yet.
    consumer_cluster_ = ASSERT_RESULT(CreateNewUniverseAndTable("consumer", "C", &consumer_table_));
    consumer_client_ = ASSERT_RESULT(consumer_cluster_->CreateClient());
    // Since we write transactionally to the consumer, also need to create a txn manager too.
    consumer_transaction_manager_.emplace(
        consumer_client_.get(), clock_, client::LocalTabletFilter());
  }

 protected:
  void SwitchToProducer() override {
    if (!producer_cluster_) {
      return;
    }
    consumer_transaction_manager_ = std::move(transaction_manager_);
    transaction_manager_ = std::move(producer_transaction_manager_);
    XClusterTabletSplitITest::SwitchToProducer();
  }

  void SwitchToConsumer() override {
    if (!consumer_cluster_) {
      return;
    }
    producer_transaction_manager_ = std::move(transaction_manager_);
    transaction_manager_ = std::move(consumer_transaction_manager_);
    XClusterTabletSplitITest::SwitchToConsumer();
  }

  boost::optional<client::TransactionManager> consumer_transaction_manager_;
  boost::optional<client::TransactionManager> producer_transaction_manager_;
};

// TODO(jhe) Re-enable this test. Currently disabled as we disable all splits when a table is being
// bootstrapped for xCluster.
TEST_F(XClusterBootstrapTabletSplitITest, YB_DISABLE_TEST(BootstrapWithSplits)) {
  // Start by writing some rows to the producer.
  ASSERT_RESULT(WriteRowsAndFlush(kDefaultNumRows));

  string bootstrap_id = ASSERT_RESULT(BootstrapProducer());

  // Instead of doing a backup, we'll just rewrite the same rows to the consumer.
  SwitchToConsumer();
  ASSERT_RESULT(WriteRowsAndFlush(kDefaultNumRows));
  SwitchToProducer();
  ASSERT_OK(CheckForNumRowsOnConsumer(kDefaultNumRows));

  // Now before setting up replication, lets perform some splits and write some more rows.
  // Since there's no replication ongoing, the parent tablets won't be deleted yet.
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 1,
                            /* parent_tablet_protected_from_deletion */ true));
  ASSERT_RESULT(WriteRows(kDefaultNumRows, kDefaultNumRows + 1));
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 2,
                            /* parent_tablet_protected_from_deletion */ true));

  // Now setup replication.
  ASSERT_OK(SetupReplication(bootstrap_id));

  // Replication should work fine.
  ASSERT_OK(CheckForNumRowsOnConsumer(2 * kDefaultNumRows));

  // Verify that parent tablet got deleted once children were polled for.
  ASSERT_OK(WaitForTabletSplitCompletion(4));

  // Perform an additional write + split afterwards.
  ASSERT_RESULT(WriteRows(kDefaultNumRows, 2 * kDefaultNumRows + 1));
  // This split will also ensure that all the parent tablets end up getting deleted.
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 4));

  ASSERT_OK(CheckForNumRowsOnConsumer(3 * kDefaultNumRows));
}

class NotSupportedTabletSplitITest : public CdcTabletSplitITest {
 public:
  void SetUp() override {
    CdcTabletSplitITest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_xcluster_replicated_tables) = false;
  }

 protected:
  Result<docdb::DocKeyHash> SplitTabletAndCheckForNotSupported(bool restart_server = false) {
    auto split_hash_code = VERIFY_RESULT(WriteRowsAndGetMiddleHashCode(kDefaultNumRows));
    auto s = SplitTabletAndValidate(split_hash_code, kDefaultNumRows);
    EXPECT_NOK(s);
    EXPECT_TRUE(s.status().IsNotSupported()) << s.status();

    if (restart_server) {
      // Now try to restart the cluster and check that tablet splitting still fails.
      RETURN_NOT_OK(cluster_->RestartSync());

      s = SplitTabletAndValidate(split_hash_code, kDefaultNumRows);
      EXPECT_NOK(s);
      EXPECT_TRUE(s.status().IsNotSupported()) << s.status();
    }

    return split_hash_code;
  }
};

TEST_F(NotSupportedTabletSplitITest, SplittingWithCdcStream) {
  // Create a cdc stream for this tablet.
  auto cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(&client_->proxy_cache(),
      HostPort::FromBoundEndpoint(cluster_->mini_tablet_servers().front()->bound_rpc_addr()));
  auto stream_id = ASSERT_RESULT(cdc::CreateCDCStream(cdc_proxy, table_->id()));
  // Ensure that the cdc_state table is ready before inserting rows and splitting.
  ASSERT_OK(WaitForCdcStateTableToBeReady());

  LOG(INFO) << "Created a CDC stream for table " << table_.name().table_name()
            << " with stream id " << stream_id;

  // Try splitting this tablet.
  ASSERT_RESULT(SplitTabletAndCheckForNotSupported());
}

TEST_F(NotSupportedTabletSplitITest, SplittingWithBootstrappedStream) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_xcluster_replicated_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_xcluster_bootstrapping_tables) = false;
  // Default cluster_ will be our producer.
  // Create a consumer universe and table, then setup universe replication.
  client::TableHandle consumer_cluster_table;
  consumer_cluster_ =
      ASSERT_RESULT(CreateNewUniverseAndTable("consumer", "C", &consumer_cluster_table));

  const string bootstrap_id = ASSERT_RESULT(BootstrapProducer());

  // Try splitting this tablet.
  const auto split_hash_code = ASSERT_RESULT(SplitTabletAndCheckForNotSupported());

  // Now complete the setup and ensure the split does work.
  ASSERT_OK(SetupReplication(bootstrap_id));
  ASSERT_OK(SplitTabletAndValidate(split_hash_code, kDefaultNumRows));
}

TEST_F(NotSupportedTabletSplitITest, SplittingWithXClusterReplicationOnProducer) {
  // Default cluster_ will be our producer.
  // Create a consumer universe and table, then setup universe replication.
  client::TableHandle consumer_cluster_table;
  auto consumer_cluster =
      ASSERT_RESULT(CreateNewUniverseAndTable("consumer", "C", &consumer_cluster_table));

  ASSERT_OK(tools::RunAdminToolCommand(
      consumer_cluster->GetMasterAddresses(), "setup_universe_replication", kProducerClusterId,
      cluster_->GetMasterAddresses(), table_->id()));

  // Try splitting this tablet, and restart the server to ensure split still fails after a restart.
  const auto split_hash_code =
      ASSERT_RESULT(SplitTabletAndCheckForNotSupported(true /* restart_server */));

  // Now delete replication and verify that the tablet can now be split.
  ASSERT_OK(tools::RunAdminToolCommand(
      consumer_cluster->GetMasterAddresses(), "delete_universe_replication", kProducerClusterId));
  // Deleting cdc streams is async so wait for that to complete.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return SplitTabletAndValidate(split_hash_code, kDefaultNumRows).ok();
  }, 20s * kTimeMultiplier, "Split tablet after deleting xCluster replication"));

  consumer_cluster->Shutdown();
}

TEST_F(NotSupportedTabletSplitITest, SplittingWithXClusterReplicationOnConsumer) {
  // Default cluster_ will be our consumer.
  // Create a producer universe and table, then setup universe replication.
  const string kProducerClusterId = "producer";
  client::TableHandle producer_cluster_table;
  auto producer_cluster =
      ASSERT_RESULT(CreateNewUniverseAndTable(kProducerClusterId, "P", &producer_cluster_table));

  ASSERT_OK(tools::RunAdminToolCommand(cluster_->GetMasterAddresses(),
                                       "setup_universe_replication",
                                       kProducerClusterId,
                                       producer_cluster->GetMasterAddresses(),
                                       producer_cluster_table->id()));

  // Try splitting this tablet, and restart the server to ensure split still fails after a restart.
  const auto split_hash_code =
      ASSERT_RESULT(SplitTabletAndCheckForNotSupported(true /* restart_server */));

  // Now delete replication and verify that the tablet can now be split.
  ASSERT_OK(tools::RunAdminToolCommand(
      cluster_->GetMasterAddresses(), "delete_universe_replication", kProducerClusterId));
  ASSERT_OK(SplitTabletAndValidate(split_hash_code, kDefaultNumRows));

  producer_cluster->Shutdown();
}

namespace {
template <typename T>
std::string TestParamToString(const testing::TestParamInfo<T>& param_info) {
  return ToString(param_info.param);
}
}  // namespace

INSTANTIATE_TEST_CASE_P(
    xClusterTabletMapTestITest,
    xClusterTabletMapTest,
    ::testing::ValuesIn(test::kPartitioningArray),
    TestParamToString<Partitioning>);

}  // namespace yb
