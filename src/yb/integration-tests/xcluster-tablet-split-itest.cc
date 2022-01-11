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
#include "yb/tools/admin-test-base.h"
#include "yb/tserver/mini_tablet_server.h"

DECLARE_int32(cdc_state_table_num_tablets);
DECLARE_bool(enable_tablet_split_of_xcluster_replicated_tables);
DECLARE_uint64(snapshot_coordinator_poll_interval_ms);
DECLARE_bool(TEST_validate_all_tablet_candidates);

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
        consumer_cluster_->GetMasterAddresses(), "setup_universe_replication", "producer",
        cluster_->GetMasterAddresses(), table_->id()));
  }

 protected:
  void DoBeforeTearDown() override {
    if (consumer_cluster_) {
      consumer_cluster_->Shutdown();
    } else {
      producer_cluster_->Shutdown();
    }

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

  // Only one set of these is valid at any time.
  // The other cluster is accessible via cluster_ / client_ / table_.
  std::unique_ptr<MiniCluster> consumer_cluster_;
  std::unique_ptr<client::YBClient> consumer_client_;
  client::TableHandle consumer_table_;

  std::unique_ptr<MiniCluster> producer_cluster_;
  std::unique_ptr<client::YBClient> producer_client_;
  client::TableHandle producer_table_;
};

TEST_F(XClusterTabletSplitITest, SplittingWithXClusterReplicationOnConsumer) {
  // To begin with, cluster_ will be our producer.
  // Write some rows to the producer.
  auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kDefaultNumRows));

  // Wait until the rows are all replicated on the consumer.
  client::YBSessionPtr consumer_session = consumer_client_->NewSession();
  consumer_session->SetTimeout(60s);
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto num_rows = VERIFY_RESULT(SelectRowsCount(consumer_session, consumer_table_));
    return num_rows == kDefaultNumRows;
  }, MonoDelta::FromSeconds(60), "Wait for data to be replicated"));

  SwitchToConsumer();

  // Perform a split on the CONSUMER cluster.
  ASSERT_OK(SplitTabletAndValidate(split_hash_code, kDefaultNumRows));

  SwitchToProducer();

  // Write another set of rows, and make sure the new poller picks up on the changes.
  ASSERT_RESULT(WriteRows(kDefaultNumRows, kDefaultNumRows + 1));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto num_rows = VERIFY_RESULT(SelectRowsCount(consumer_session, consumer_table_));
    return num_rows == 2 * kDefaultNumRows;
  }, MonoDelta::FromSeconds(60), "Wait for data to be replicated"));
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
