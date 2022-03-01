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

#include "yb/cdc/cdc_service.proxy.h"

#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"

#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/tablet-split-itest-base.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tools/admin-test-base.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/util/thread.h"

DECLARE_int32(cdc_state_table_num_tablets);
DECLARE_bool(enable_tablet_split_of_xcluster_replicated_tables);
DECLARE_uint64(snapshot_coordinator_poll_interval_ms);
DECLARE_bool(TEST_validate_all_tablet_candidates);
DECLARE_bool(TEST_xcluster_consumer_fail_after_process_split_op);

namespace yb {

class CdcTabletSplitITest : public TabletSplitITest {
 public:
  void SetUp() override {
    FLAGS_cdc_state_table_num_tablets = 1;
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

      is_create_req.mutable_table()->set_table_name(master::kCdcStateTableName);
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
      const string& cluster_id, client::TableHandle* table) {
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
        1,  // num_tablets
        cluster_client.get(),
        table);
    return cluster;
  }
};

TEST_F(CdcTabletSplitITest, GetChangesOnSplitParentTablet) {
  constexpr auto kNumRows = kDefaultNumRows;
  // Create a cdc stream for this tablet.
  auto cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(&client_->proxy_cache(),
      HostPort::FromBoundEndpoint(cluster_->mini_tablet_servers().front()->bound_rpc_addr()));
  CDCStreamId stream_id;
  cdc::CreateCDCStream(cdc_proxy, table_->id(), &stream_id);
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
  change_req.set_stream_id(stream_id);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(0);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(0);

  rpc::RpcController rpc;
  ASSERT_OK(cdc_proxy->GetChanges(change_req, &change_resp, &rpc));
  ASSERT_FALSE(change_resp.has_error());

  // Test that if the tablet leadership of the parent tablet changes we can still call GetChanges.
  StepDownAllTablets(cluster_.get());

  rpc.Reset();
  ASSERT_OK(cdc_proxy->GetChanges(change_req, &change_resp, &rpc));
  ASSERT_FALSE(change_resp.has_error()) << change_resp.ShortDebugString();

  // Now let the table get deleted by the background task. Need to lower the wal_retention_secs.
  master::AlterTableRequestPB alter_table_req;
  master::AlterTableResponsePB alter_table_resp;
  alter_table_req.mutable_table()->set_table_id(table_->id());
  alter_table_req.set_wal_retention_secs(1);

  master::MasterDdlProxy master_proxy(
      &client_->proxy_cache(), ASSERT_RESULT(cluster_->GetLeaderMasterBoundRpcAddr()));
  rpc.Reset();
  ASSERT_OK(master_proxy.AlterTable(alter_table_req, &alter_table_resp, &rpc));

  SleepFor(MonoDelta::FromMilliseconds(2 * FLAGS_snapshot_coordinator_poll_interval_ms));

  // Try to do a GetChanges again, it should fail.
  rpc.Reset();
  ASSERT_OK(cdc_proxy->GetChanges(change_req, &change_resp, &rpc));
  ASSERT_TRUE(change_resp.has_error());
}

// For testing xCluster setups. Since most test utility functions expect there to be only one
// cluster, they implicitly use cluster_ / client_ / table_ everywhere. For this test, we default
// those to point to the producer cluster, but allow calls to SwitchToProducer/Consumer, to swap
// those to point to the other cluster.
class XClusterTabletSplitITest : public CdcTabletSplitITest {
 public:
  void SetUp() override {
    CdcTabletSplitITest::SetUp();

    // Also create the consumer cluster.
    consumer_cluster_ = ASSERT_RESULT(CreateNewUniverseAndTable("consumer", &consumer_table_));
    consumer_client_ = ASSERT_RESULT(consumer_cluster_->CreateClient());

    ASSERT_OK(tools::RunAdminToolCommand(
        consumer_cluster_->GetMasterAddresses(), "setup_universe_replication", kProducerClusterId,
        cluster_->GetMasterAddresses(), table_->id()));
  }

 protected:
  void DoBeforeTearDown() override {
    SwitchToConsumer();
    ASSERT_OK(tools::RunAdminToolCommand(
        cluster_->GetMasterAddresses(), "delete_universe_replication", kProducerClusterId));

    // Shutdown producer cluster here, CdcTabletSplitITest will shutdown cluster_ (consumer).
    producer_cluster_->Shutdown();
    CdcTabletSplitITest::DoBeforeTearDown();
  }

  void SwitchToProducer() {
    if (!producer_cluster_) {
      return;
    }
    // cluster_ is currently the consumer.
    consumer_cluster_ = std::move(cluster_);
    consumer_client_ = std::move(client_);
    consumer_table_ = std::move(table_);
    cluster_ = std::move(producer_cluster_);
    client_ = std::move(producer_client_);
    table_ = std::move(producer_table_);
    LOG(INFO) << "Swapped to the producer cluster.";
  }

  void SwitchToConsumer() {
    if (!consumer_cluster_) {
      return;
    }
    // cluster_ is currently the producer.
    producer_cluster_ = std::move(cluster_);
    producer_client_ = std::move(client_);
    producer_table_ = std::move(table_);
    cluster_ = std::move(consumer_cluster_);
    client_ = std::move(consumer_client_);
    table_ = std::move(consumer_table_);
    LOG(INFO) << "Swapped to the consumer cluster.";
  }

  CHECKED_STATUS CheckForNumRowsOnConsumer(size_t expected_num_rows) {
    client::YBClient* consumer_client(consumer_cluster_ ? consumer_client_.get() : client_.get());
    client::TableHandle* consumer_table(consumer_cluster_ ? &consumer_table_ : &table_);

    client::YBSessionPtr consumer_session = consumer_client->NewSession();
    consumer_session->SetTimeout(60s);
    size_t num_rows = 0;
    Status s = WaitFor([&]() -> Result<bool> {
      num_rows = VERIFY_RESULT(SelectRowsCount(consumer_session, *consumer_table));
      return num_rows == expected_num_rows;
    }, MonoDelta::FromSeconds(60), "Wait for data to be replicated");

    LOG(INFO) << "Found " << num_rows << " rows on consumer, expected " << expected_num_rows;

    return s;
  }

  CHECKED_STATUS SplitAllTablets(
      int cur_num_tablets, bool parent_tablet_protected_from_deletion = true) {
    // Splits all tablets for cluster_.
    auto* catalog_mgr = VERIFY_RESULT(catalog_manager());
    auto tablet_peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
    EXPECT_EQ(tablet_peers.size(), cur_num_tablets);
    for (const auto& peer : tablet_peers) {
      const auto source_tablet_ptr = peer->tablet();
      EXPECT_NE(source_tablet_ptr, nullptr);
      const auto& source_tablet = *source_tablet_ptr;
      RETURN_NOT_OK(SplitTablet(catalog_mgr, source_tablet));
    }
    size_t expected_non_split_tablets = cur_num_tablets * 2;
    size_t expected_split_tablets = parent_tablet_protected_from_deletion
                                    ? cur_num_tablets * 2 - 1
                                    : 0;
    return WaitForTabletSplitCompletion(expected_non_split_tablets, expected_split_tablets);
  }

  // Only one set of these is valid at any time.
  // The other cluster is accessible via cluster_ / client_ / table_.
  std::unique_ptr<MiniCluster> consumer_cluster_;
  std::unique_ptr<client::YBClient> consumer_client_;
  client::TableHandle consumer_table_;

  std::unique_ptr<MiniCluster> producer_cluster_;
  std::unique_ptr<client::YBClient> producer_client_;
  client::TableHandle producer_table_;

  const string kProducerClusterId = "producer";
};

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

  // Split the tablet on the producer. Note that parent tablet will only be HIDDEN and not deleted.
  ASSERT_OK(SplitTabletAndValidate(
      split_hash_code, kDefaultNumRows, /* parent_tablet_protected_from_deletion */ true));

  // Write another set of rows, and make sure the consumer picks up on the changes.
  ASSERT_RESULT(WriteRows(kDefaultNumRows, kDefaultNumRows + 1));

  ASSERT_OK(CheckForNumRowsOnConsumer(2 * kDefaultNumRows));
}

TEST_F(XClusterTabletSplitITest, MultipleSplitsDuringPausedReplication) {
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
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 2));
  ASSERT_RESULT(WriteRows(kDefaultNumRows, 2 * kDefaultNumRows + 1));
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 4));

  // Now re-enable replication.
  ASSERT_OK(tools::RunAdminToolCommand(
      consumer_cluster_->GetMasterAddresses(), "set_universe_replication_enabled",
      kProducerClusterId, "1"));

  // Ensure all the rows are all replicated on the consumer.
  ASSERT_OK(CheckForNumRowsOnConsumer(3 * kDefaultNumRows));

  // Write another set of rows, and make sure the consumer picks up on the changes.
  ASSERT_RESULT(WriteRows(kDefaultNumRows, 3 * kDefaultNumRows + 1));

  ASSERT_OK(CheckForNumRowsOnConsumer(4 * kDefaultNumRows));
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
    auto producer_session = client_->NewSession();
    producer_session->SetTimeout(60s);
    int32_t key = kDefaultNumRows;
    while (!stop) {
      key = (key + 1);
      ASSERT_RESULT(client::kv_table_test::WriteRow(
          &producer_table, producer_session, key, key,
          client::WriteOpType::INSERT, client::Flush::kTrue));
    }
  });

  // Perform tablet splits on both sides.
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 1));
  SwitchToConsumer();
  ASSERT_OK(SplitAllTablets(
      /* cur_num_tablets */ 1, /* parent_tablet_protected_from_deletion */ false));
  SwitchToProducer();
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 2));
  SwitchToConsumer();
  ASSERT_OK(SplitAllTablets(
      /* cur_num_tablets */ 2, /* parent_tablet_protected_from_deletion */ false));
  SwitchToProducer();

  // Stop writes.
  stop.store(true, std::memory_order_release);
  write_thread.join();

  // Verify that both sides have the same number of rows.
  client::YBSessionPtr producer_session = client_->NewSession();
  producer_session->SetTimeout(60s);
  size_t num_rows = ASSERT_RESULT(SelectRowsCount(producer_session, table_));

  ASSERT_OK(CheckForNumRowsOnConsumer(num_rows));
}

TEST_F(XClusterTabletSplitITest, ConsumerClusterFailureWhenProcessingSplitOp) {
  ASSERT_RESULT(WriteRowsAndFlush(kDefaultNumRows));
  ASSERT_OK(CheckForNumRowsOnConsumer(kDefaultNumRows));

  // Force consumer to fail after processing the split op.
  SetAtomicFlag(true, &FLAGS_TEST_xcluster_consumer_fail_after_process_split_op);

  // Perform a split.
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 1));
  // Write some additional rows.
  ASSERT_RESULT(WriteRows(kDefaultNumRows, kDefaultNumRows + 1));

  // Wait for a bit, as the consumer keeps trying to process the split_op but fails.
  SleepFor(10s);
  // Check that these new rows aren't replicated since we're stuck on the split_op.
  ASSERT_OK(CheckForNumRowsOnConsumer(kDefaultNumRows));

  // Allow for the split op to be processed properly, and check that everything is replicated.
  SetAtomicFlag(false, &FLAGS_TEST_xcluster_consumer_fail_after_process_split_op);
  ASSERT_OK(CheckForNumRowsOnConsumer(2 * kDefaultNumRows));

  ASSERT_RESULT(WriteRows(kDefaultNumRows, 2 * kDefaultNumRows + 1));
  ASSERT_OK(CheckForNumRowsOnConsumer(3 * kDefaultNumRows));
}

class XClusterBootstrapTabletSplitITest : public XClusterTabletSplitITest {
 public:
  void SetUp() override {
    CdcTabletSplitITest::SetUp();

    // Create the consumer cluster, but don't setup the universe replication yet.
    consumer_cluster_ = ASSERT_RESULT(CreateNewUniverseAndTable("consumer", &consumer_table_));
    consumer_client_ = ASSERT_RESULT(consumer_cluster_->CreateClient());
  }

 protected:
  Result<string> BootstrapProducer() {
    const int kStreamUuidLength = 32;
    string output = VERIFY_RESULT(tools::RunAdminToolCommand(
        cluster_->GetMasterAddresses(), "bootstrap_cdc_producer", table_->id()));
    // Get the bootstrap id (output format is "table id: 123, CDC bootstrap id: 123\n").
    string bootstrap_id = output.substr(output.find_last_of(' ') + 1, kStreamUuidLength);
    return bootstrap_id;
  }

  CHECKED_STATUS SetupReplication(const string& bootstrap_id = "") {
    VERIFY_RESULT(tools::RunAdminToolCommand(
        consumer_cluster_->GetMasterAddresses(), "setup_universe_replication", kProducerClusterId,
        cluster_->GetMasterAddresses(), table_->id(), bootstrap_id));
    return Status::OK();
  }
};

TEST_F(XClusterBootstrapTabletSplitITest, BootstrapWithSplits) {
  // Start by writing some rows to the producer.
  ASSERT_RESULT(WriteRowsAndFlush(kDefaultNumRows));

  string bootstrap_id = ASSERT_RESULT(BootstrapProducer());

  // Instead of doing a backup, we'll just rewrite the same rows to the consumer.
  SwitchToConsumer();
  ASSERT_RESULT(WriteRowsAndFlush(kDefaultNumRows));
  SwitchToProducer();

  // Now before setting up replication, lets perform some splits and write some more rows.
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 1));
  ASSERT_RESULT(WriteRows(kDefaultNumRows, kDefaultNumRows + 1));
  ASSERT_OK(SplitAllTablets(/* cur_num_tablets */ 2));

  // Now setup replication.
  ASSERT_OK(SetupReplication(bootstrap_id));

  // Replication should work fine.
  ASSERT_OK(CheckForNumRowsOnConsumer(2 * kDefaultNumRows));

  // Perform an additional write + split afterwards.
  ASSERT_RESULT(WriteRows(kDefaultNumRows, 2 * kDefaultNumRows + 1));
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
  Result<docdb::DocKeyHash> SplitTabletAndCheckForNotSupported(bool restart_server) {
    auto split_hash_code = VERIFY_RESULT(WriteRowsAndGetMiddleHashCode(kDefaultNumRows));
    auto s = SplitTabletAndValidate(split_hash_code, kDefaultNumRows);
    EXPECT_NOT_OK(s);
    EXPECT_TRUE(s.status().IsNotSupported()) << s.status();

    if (restart_server) {
      // Now try to restart the cluster and check that tablet splitting still fails.
      RETURN_NOT_OK(cluster_->RestartSync());

      s = SplitTabletAndValidate(split_hash_code, kDefaultNumRows);
      EXPECT_NOT_OK(s);
      EXPECT_TRUE(s.status().IsNotSupported()) << s.status();
    }

    return split_hash_code;
  }
};

TEST_F(NotSupportedTabletSplitITest, SplittingWithCdcStream) {
  // Create a cdc stream for this tablet.
  auto cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(&client_->proxy_cache(),
      HostPort::FromBoundEndpoint(cluster_->mini_tablet_servers().front()->bound_rpc_addr()));
  CDCStreamId stream_id;
  cdc::CreateCDCStream(cdc_proxy, table_->id(), &stream_id);
  // Ensure that the cdc_state table is ready before inserting rows and splitting.
  ASSERT_OK(WaitForCdcStateTableToBeReady());

  LOG(INFO) << "Created a CDC stream for table " << table_.name().table_name()
            << " with stream id " << stream_id;

  // Try splitting this tablet.
  ASSERT_RESULT(SplitTabletAndCheckForNotSupported(false /* restart_server */));
}

TEST_F(NotSupportedTabletSplitITest, SplittingWithXClusterReplicationOnProducer) {
  // Default cluster_ will be our producer.
  // Create a consumer universe and table, then setup universe replication.
  client::TableHandle consumer_cluster_table;
  auto consumer_cluster =
      ASSERT_RESULT(CreateNewUniverseAndTable("consumer", &consumer_cluster_table));

  ASSERT_OK(tools::RunAdminToolCommand(consumer_cluster->GetMasterAddresses(),
                                       "setup_universe_replication",
                                       "",  // Producer cluster id (default is set to "").
                                       cluster_->GetMasterAddresses(),
                                       table_->id()));

  // Try splitting this tablet, and restart the server to ensure split still fails after a restart.
  const auto split_hash_code =
      ASSERT_RESULT(SplitTabletAndCheckForNotSupported(true /* restart_server */));

  // Now delete replication and verify that the tablet can now be split.
  ASSERT_OK(tools::RunAdminToolCommand(
      consumer_cluster->GetMasterAddresses(), "delete_universe_replication", ""));
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
      ASSERT_RESULT(CreateNewUniverseAndTable(kProducerClusterId, &producer_cluster_table));

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

}  // namespace yb
