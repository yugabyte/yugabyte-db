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

#include <map>
#include <string>
#include <utility>
#include <chrono>
#include <boost/assign.hpp>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "yb/common/wire_protocol.h"

#include "yb/cdc/cdc_service.h"
#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/transaction.h"
#include "yb/client/yb_op.h"

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/mini_master.h"
#include "yb/master/master.h"
#include "yb/master/master.pb.h"
#include "yb/master/master-test-util.h"

#include "yb/master/cdc_consumer_registry_service.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/tserver/cdc_consumer.h"
#include "yb/util/atomic.h"
#include "yb/util/faststring.h"
#include "yb/util/random.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"

DECLARE_int32(replication_factor);
DECLARE_int32(cdc_read_rpc_timeout_ms);
DECLARE_int32(cdc_write_rpc_timeout_ms);
DECLARE_bool(TEST_twodc_write_hybrid_time);
DECLARE_int32(cdc_wal_retention_time_secs);
DECLARE_bool(TEST_check_broadcast_address);
DECLARE_int32(replication_failure_delay_exponent);
DECLARE_double(respond_write_failed_probability);
DECLARE_int32(cdc_max_apply_batch_num_records);

namespace yb {

using client::YBClient;
using client::YBClientBuilder;
using client::YBColumnSchema;
using client::YBError;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBSession;
using client::YBTable;
using client::YBTableAlterer;
using client::YBTableCreator;
using client::YBTableType;
using client::YBTableName;
using master::MiniMaster;
using tserver::MiniTabletServer;
using tserver::enterprise::CDCConsumer;

namespace enterprise {

constexpr int kRpcTimeout = NonTsanVsTsan(30, 60);
static const std::string kUniverseId = "test_universe";
static const std::string kNamespaceName = "test_namespace";

class TwoDCTest : public YBTest, public testing::WithParamInterface<int> {
 public:
  Result<std::vector<std::shared_ptr<client::YBTable>>>
      SetUpWithParams(std::vector<uint32_t> num_consumer_tablets,
                      std::vector<uint32_t> num_producer_tablets,
                      uint32_t replication_factor) {
    // Allow for one-off network instability by ensuring a single CDC RPC timeout << test timeout.
    FLAGS_cdc_read_rpc_timeout_ms = (kRpcTimeout / 6) * 1000;
    FLAGS_cdc_write_rpc_timeout_ms = (kRpcTimeout / 6) * 1000;
    // Not a useful test for us. It's testing Public+Private IP NW errors and we're only public
    FLAGS_TEST_check_broadcast_address = false;
    FLAGS_cdc_max_apply_batch_num_records = GetParam();

    YBTest::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = replication_factor;
    FLAGS_replication_factor = replication_factor;
    opts.cluster_id = "producer";
    producer_cluster_ = std::make_unique<MiniCluster>(Env::Default(), opts);
    RETURN_NOT_OK(producer_cluster_->StartSync());
    RETURN_NOT_OK(producer_cluster_->WaitForTabletServerCount(replication_factor));

    opts.cluster_id = "consumer";
    consumer_cluster_ = std::make_unique<MiniCluster>(Env::Default(), opts);
    RETURN_NOT_OK(consumer_cluster_->StartSync());
    RETURN_NOT_OK(consumer_cluster_->WaitForTabletServerCount(replication_factor));

    producer_client_ = VERIFY_RESULT(producer_cluster_->CreateClient());
    consumer_client_ = VERIFY_RESULT(consumer_cluster_->CreateClient());

    RETURN_NOT_OK(clock_->Init());
    producer_txn_mgr_.emplace(producer_client_.get(), clock_, client::LocalTabletFilter());
    consumer_txn_mgr_.emplace(consumer_client_.get(), clock_, client::LocalTabletFilter());

    YBSchemaBuilder b;
    b.AddColumn("c0")->Type(INT32)->NotNull()->HashPrimaryKey();

    // Create transactional table.
    TableProperties table_properties;
    table_properties.SetTransactional(true);
    b.SetTableProperties(table_properties);
    CHECK_OK(b.Build(&schema_));

    if (num_consumer_tablets.size() != num_producer_tablets.size()) {
      return STATUS(IllegalState,
                    Format("Num consumer tables: $0 num producer tables: $1 must be equal.",
                           num_consumer_tablets.size(), num_producer_tablets.size()));
    }

    std::vector<YBTableName> tables;
    std::vector<std::shared_ptr<client::YBTable>> yb_tables;
    for (int i = 0; i < num_consumer_tablets.size(); i++) {
      RETURN_NOT_OK(CreateTable(i, num_producer_tablets[i], producer_client_.get(), &tables));
      std::shared_ptr<client::YBTable> producer_table;
      RETURN_NOT_OK(producer_client_->OpenTable(tables[i * 2], &producer_table));
      yb_tables.push_back(producer_table);

      RETURN_NOT_OK(CreateTable(i, num_consumer_tablets[i], consumer_client_.get(), &tables));
      std::shared_ptr<client::YBTable> consumer_table;
      RETURN_NOT_OK(consumer_client_->OpenTable(tables[(i * 2) + 1], &consumer_table));
      yb_tables.push_back(consumer_table);
    }

    return yb_tables;
  }

  Result<YBTableName> CreateTable(YBClient* client, const std::string& namespace_name,
                                  const std::string& table_name, uint32_t num_tablets) {
    YBTableName table(YQL_DATABASE_CQL, namespace_name, table_name);
    RETURN_NOT_OK(client->CreateNamespaceIfNotExists(table.namespace_name(),
                                                     table.namespace_type()));

    // Add a table, make sure it reports itself.
    gscoped_ptr<YBTableCreator> table_creator(client->NewTableCreator());
        RETURN_NOT_OK(table_creator->table_name(table)
                          .schema(&schema_)
                          .table_type(YBTableType::YQL_TABLE_TYPE)
                          .num_tablets(num_tablets)
                          .Create());
    return table;
  }

  Status CreateTable(
      uint32_t idx, uint32_t num_tablets, YBClient* client, std::vector<YBTableName>* tables) {
    auto table = VERIFY_RESULT(CreateTable(client, kNamespaceName, Format("test_table_$0", idx),
                                           num_tablets));
    tables->push_back(table);
    return Status::OK();
  }

  Status SetupUniverseReplication(
      MiniCluster* producer_cluster, MiniCluster* consumer_cluster, YBClient* consumer_client,
      const std::string& universe_id, const std::vector<std::shared_ptr<client::YBTable>>& tables) {
    master::SetupUniverseReplicationRequestPB req;
    master::SetupUniverseReplicationResponsePB resp;

    req.set_producer_id(universe_id);
    string master_addr = producer_cluster->GetMasterAddresses();
    auto hp_vec = VERIFY_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, req.mutable_producer_master_addresses());

    req.mutable_producer_table_ids()->Reserve(tables.size());
    for (const auto& table : tables) {
      req.add_producer_table_ids(table->id());
    }

    auto master_proxy = std::make_shared<master::MasterServiceProxy>(
        &consumer_client->proxy_cache(),
        consumer_cluster->leader_mini_master()->bound_rpc_addr());

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    RETURN_NOT_OK(master_proxy->SetupUniverseReplication(req, &resp, &rpc));
    if (resp.has_error()) {
      return STATUS(IllegalState, "Failed setting up universe replication");
    }
    return Status::OK();
  }

  Status VerifyUniverseReplication(
      MiniCluster* consumer_cluster, YBClient* consumer_client,
      const std::string& universe_id, master::GetUniverseReplicationResponsePB* resp) {
    return LoggedWaitFor([=]() -> Result<bool> {
      master::GetUniverseReplicationRequestPB req;
      req.set_producer_id(universe_id);
      resp->Clear();

      auto master_proxy = std::make_shared<master::MasterServiceProxy>(
          &consumer_client->proxy_cache(),
          consumer_cluster->leader_mini_master()->bound_rpc_addr());
      rpc::RpcController rpc;
      rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

      Status s = master_proxy->GetUniverseReplication(req, resp, &rpc);
      return s.ok() && !resp->has_error() &&
             resp->entry().state() == master::SysUniverseReplicationEntryPB::ACTIVE;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify universe replication");
  }

  Status ToggleUniverseReplication(
      MiniCluster* consumer_cluster, YBClient* consumer_client,
      const std::string& universe_id, bool is_enabled) {
    master::SetUniverseReplicationEnabledRequestPB req;
    master::SetUniverseReplicationEnabledResponsePB resp;

    req.set_producer_id(universe_id);
    req.set_is_enabled(is_enabled);

    auto master_proxy = std::make_shared<master::MasterServiceProxy>(
        &consumer_client->proxy_cache(),
        consumer_cluster->leader_mini_master()->bound_rpc_addr());

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    RETURN_NOT_OK(master_proxy->SetUniverseReplicationEnabled(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return Status::OK();
  }

  Status VerifyUniverseReplicationDeleted(MiniCluster* consumer_cluster, YBClient* consumer_client,
      const std::string& universe_id, int timeout) {
    return LoggedWaitFor([=]() -> Result<bool> {
      master::GetUniverseReplicationRequestPB req;
      master::GetUniverseReplicationResponsePB resp;
      req.set_producer_id(universe_id);

      auto master_proxy = std::make_shared<master::MasterServiceProxy>(
          &consumer_client->proxy_cache(),
          consumer_cluster->leader_mini_master()->bound_rpc_addr());
      rpc::RpcController rpc;
      rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

      Status s = master_proxy->GetUniverseReplication(req, &resp, &rpc);
      return resp.has_error() && resp.error().code() == master::MasterErrorPB::OBJECT_NOT_FOUND;
    }, MonoDelta::FromMilliseconds(timeout), "Verify universe replication deleted");
  }

  Status GetCDCStreamForTable(
      const std::string& table_id, master::ListCDCStreamsResponsePB* resp) {
    return LoggedWaitFor([=]() -> Result<bool> {
      master::ListCDCStreamsRequestPB req;
      req.set_table_id(table_id);
      resp->Clear();

      Status s = producer_cluster_->leader_mini_master()->master()->catalog_manager()->
          ListCDCStreams(&req, resp);
      return s.ok() && !resp->has_error() && resp->streams_size() == 1;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Get CDC stream for table");
  }

  void Destroy() {
    LOG(INFO) << "Destroying CDC Clusters";
    if (consumer_cluster_) {
      consumer_cluster_->Shutdown();
      consumer_cluster_.reset();
    }

    if (producer_cluster_) {
      producer_cluster_->Shutdown();
      producer_cluster_.reset();
    }

    producer_client_.reset();
    consumer_client_.reset();
  }

  void WriteWorkload(uint32_t start, uint32_t end, YBClient* client, const YBTableName& table,
                     bool delete_op = false) {
    auto session = client->NewSession();
    client::TableHandle table_handle;
    ASSERT_OK(table_handle.Open(table, client));
    std::vector<std::shared_ptr<client::YBqlOp>> ops;

    LOG(INFO) << "Writing " << end-start << (delete_op ? " deletes" : " inserts");
    for (uint32_t i = start; i < end; i++) {
      auto op = delete_op ? table_handle.NewDeleteOp() : table_handle.NewInsertOp();
      int32_t key = i;
      auto req = op->mutable_request();
      QLAddInt32HashValue(req, key);
      ASSERT_OK(session->ApplyAndFlush(op));
    }
  }

  uint32_t GetSuccessfulWriteOps(MiniCluster* cluster) {
    uint32_t size = 0;
    for (const auto& mini_tserver : cluster->mini_tablet_servers()) {
      auto* tserver = dynamic_cast<tserver::enterprise::TabletServer*>(
          mini_tserver->server());
      CDCConsumer* cdc_consumer;
      if (tserver && (cdc_consumer = tserver->GetCDCConsumer())) {
        size += cdc_consumer->GetNumSuccessfulWriteRpcs();
      }
    }
    return size;
  }

  void WriteTransactionalWorkload(uint32_t start, uint32_t end, YBClient* client,
                                  client::TransactionManager* txn_mgr, const YBTableName& table) {
    auto session = client->NewSession();
    auto transaction = std::make_shared<client::YBTransaction>(txn_mgr);
    ReadHybridTime read_time;
    ASSERT_OK(transaction->Init(IsolationLevel::SNAPSHOT_ISOLATION, read_time));
    session->SetTransaction(transaction);

    client::TableHandle table_handle;
    ASSERT_OK(table_handle.Open(table, client));
    std::vector<std::shared_ptr<client::YBqlOp>> ops;

    for (uint32_t i = start; i < end; i++) {
      auto op = table_handle.NewDeleteOp();
      int32_t key = i;
      auto req = op->mutable_request();
      QLAddInt32HashValue(req, key);
      ASSERT_OK(session->ApplyAndFlush(op));
    }
    transaction->CommitFuture().get();
  }

  void DeleteWorkload(uint32_t start, uint32_t end, YBClient* client, const YBTableName& table) {
    WriteWorkload(start, end, client, table, true /* delete_op */);
  }

  std::vector<string> ScanToStrings(const YBTableName& table_name, YBClient* client) {
    client::TableHandle table;
    EXPECT_OK(table.Open(table_name, client));
    auto result = ScanTableToStrings(table);
    std::sort(result.begin(), result.end());
    return result;
  }


  Status VerifyWrittenRecords(const YBTableName& producer_table,
                              const YBTableName& consumer_table) {
    return LoggedWaitFor([=]() -> Result<bool> {
      auto producer_results = ScanToStrings(producer_table, producer_client_.get());
      auto consumer_results = ScanToStrings(consumer_table, consumer_client_.get());
      return producer_results == consumer_results;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify written records");
  }

  Status VerifyNumRecords(const YBTableName& table, YBClient* client, int expected_size) {
    return LoggedWaitFor([=]() -> Result<bool> {
      auto results = ScanToStrings(table, client);
      return results.size() == expected_size;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify number of records");
  }

  Status DeleteUniverseReplication(const std::string& universe_id) {
    return DeleteUniverseReplication(universe_id, consumer_client(), consumer_cluster());
  }

  Status DeleteUniverseReplication(
      const std::string& universe_id, YBClient* client, MiniCluster* cluster) {
    master::DeleteUniverseReplicationRequestPB req;
    master::DeleteUniverseReplicationResponsePB resp;

    req.set_producer_id(universe_id);

    auto master_proxy = std::make_shared<master::MasterServiceProxy>(
        &client->proxy_cache(),
        cluster->leader_mini_master()->bound_rpc_addr());

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    RETURN_NOT_OK(master_proxy->DeleteUniverseReplication(req, &resp, &rpc));
    LOG(INFO) << "Delete universe succeeded";
    return Status::OK();
  }

  YBClient* producer_client() {
    return producer_client_.get();
  }

  YBClient* consumer_client() {
    return consumer_client_.get();
  }

  MiniCluster* producer_cluster() {
    return producer_cluster_.get();
  }

  MiniCluster* consumer_cluster() {
    return consumer_cluster_.get();
  }

  client::TransactionManager* producer_txn_mgr() {
    return producer_txn_mgr_.get_ptr();
  }

  client::TransactionManager* consumer_txn_mgr() {
    return consumer_txn_mgr_.get_ptr();
  }

  uint32_t NumProducerTabletsPolled(MiniCluster* cluster) {
    uint32_t size = 0;
    for (const auto& mini_tserver : cluster->mini_tablet_servers()) {
      uint32_t new_size = 0;
      auto* tserver = dynamic_cast<tserver::enterprise::TabletServer*>(
          mini_tserver->server());
      CDCConsumer* cdc_consumer;
      if (tserver && (cdc_consumer = tserver->GetCDCConsumer())) {
        auto tablets_running = cdc_consumer->TEST_producer_tablets_running();
        new_size = tablets_running.size();
      }
      size += new_size;
    }
    return size;
  }

  Status CorrectlyPollingAllTablets(MiniCluster* cluster, uint32_t num_producer_tablets) {
    return LoggedWaitFor([=]() -> Result<bool> {
      static int i = 0;
      constexpr int kNumIterationsWithCorrectResult = 5;
      if (NumProducerTabletsPolled(cluster) == num_producer_tablets) {
        if (i++ == kNumIterationsWithCorrectResult) {
          i = 0;
          return true;
        }
      } else {
        i = 0;
      }
      return false;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Num producer tablets being polled");
  }

  std::unique_ptr<MiniCluster> producer_cluster_;
  std::unique_ptr<MiniCluster> consumer_cluster_;

 private:

  std::unique_ptr<YBClient> producer_client_;
  std::unique_ptr<YBClient> consumer_client_;

  boost::optional<client::TransactionManager> producer_txn_mgr_;
  boost::optional<client::TransactionManager> consumer_txn_mgr_;
  server::ClockPtr clock_{new server::HybridClock()};

  YBSchema schema_;
};

INSTANTIATE_TEST_CASE_P(BatchSize, TwoDCTest, ::testing::Values(1, 100));

TEST_P(TwoDCTest, SetupUniverseReplication) {
  auto tables = ASSERT_RESULT(SetUpWithParams({8, 4}, {6, 6}, 3));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  producer_tables.reserve(tables.size() / 2);
  for (int i = 0; i < tables.size(); i += 2) {
    producer_tables.push_back(tables[i]);
  }
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, &resp));
  ASSERT_EQ(resp.entry().producer_id(), kUniverseId);
  ASSERT_EQ(resp.entry().tables_size(), producer_tables.size());
  for (int i = 0; i < producer_tables.size(); i++) {
    ASSERT_EQ(resp.entry().tables(i), producer_tables[i]->id());
  }

  // Verify that CDC streams were created on producer for all tables.
  for (int i = 0; i < producer_tables.size(); i++) {
    master::ListCDCStreamsResponsePB stream_resp;
    ASSERT_OK(GetCDCStreamForTable(producer_tables[i]->id(), &stream_resp));
    ASSERT_EQ(stream_resp.streams_size(), 1);
    ASSERT_EQ(stream_resp.streams(0).table_id(), producer_tables[i]->id());
  }

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
  Destroy();
}

// Test for #2250 to verify that replication for tables with the same prefix gets set up correctly.
TEST_P(TwoDCTest, SetupUniverseReplicationMultipleTables) {
  // Setup the two clusters without any tables.
  auto tables = ASSERT_RESULT(SetUpWithParams({}, {}, 1));

  // Create tables with the same prefix.
  std::string table_names[2] = {"table", "table_index"};

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  for (int i = 0; i < 2; i++) {
    auto t = ASSERT_RESULT(CreateTable(producer_client(), kNamespaceName, table_names[i], 3));
    std::shared_ptr<client::YBTable> producer_table;
    ASSERT_OK(producer_client()->OpenTable(t, &producer_table));
    producer_tables.push_back(producer_table);
  }

  for (int i = 0; i < 2; i++) {
    ASSERT_RESULT(CreateTable(consumer_client(), kNamespaceName, table_names[i], 3));
  }

  // Setup universe replication on both these tables.
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, &resp));
  ASSERT_EQ(resp.entry().producer_id(), kUniverseId);
  ASSERT_EQ(resp.entry().tables_size(), producer_tables.size());
  for (int i = 0; i < producer_tables.size(); i++) {
    ASSERT_EQ(resp.entry().tables(i), producer_tables[i]->id());
  }

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
  Destroy();
}

TEST_P(TwoDCTest, PollWithConsumerRestart) {
  // Avoid long delays with node failures so we can run with more aggressive test timing
  FLAGS_replication_failure_delay_exponent = 7; // 2^7 == 128ms

  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({4}, {4}, replication_factor));

  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId,
      {tables[0]} /* all producer tables */));

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));

  consumer_cluster_->mini_tablet_server(0)->Shutdown();

  // After shutting down a single consumer node, the other consumers should pick up the slack.
  if (replication_factor > 1) {
    ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));
  }

  ASSERT_OK(consumer_cluster_->mini_tablet_server(0)->Start());

  // After restarting the node.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));

  ASSERT_OK(consumer_cluster_->RestartSync());

  // After consumer cluster restart.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
  Destroy();
}

TEST_P(TwoDCTest, PollWithProducerRestart) {
  // Avoid long delays with node failures so we can run with more aggressive test timing
  FLAGS_replication_failure_delay_exponent = 7; // 2^7 == 128ms

  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({4}, {4}, replication_factor));

  ASSERT_OK(SetupUniverseReplication(
    producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId,
    {tables[0]} /* all producer tables */));

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));

  producer_cluster_->mini_tablet_server(0)->Shutdown();
  // Tablet Server Stop/Start Test needs replication for other polling sources
  if (replication_factor > 1) {
    // Verify that the cluster has rebalanced all the CDC Pollers
    ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));
  }
  ASSERT_OK(producer_cluster_->mini_tablet_server(0)->Start());

  // After starting the node.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));

  ASSERT_OK(producer_cluster_->RestartSync());

  // After producer cluster restart.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
  Destroy();
}

TEST_P(TwoDCTest, ApplyOperations) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  // Use just one tablet here to more easily catch lower-level write issues with this test.
  auto tables = ASSERT_RESULT(SetUpWithParams({1}, {1}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 1));

  WriteWorkload(0, 5, producer_client(), tables[0]->name());

  // Check that all tablets continue to be polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 1));

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
  Destroy();
}

TEST_P(TwoDCTest, ApplyOperationsWithTransactions) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {2}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));

  // Write some transactional rows.
  WriteTransactionalWorkload(0, 5, producer_client(), producer_txn_mgr(), tables[0]->name());

  // Write some non-transactional rows.
  WriteWorkload(6, 10, producer_client(), tables[0]->name());

  // Check that all tablets continue to be polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
  Destroy();
}

TEST_P(TwoDCTest, TestExternalWriteHybridTime) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {2}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));

  // Write 2 rows.
  WriteWorkload(0, 2, producer_client(), tables[0]->name());

  // Ensure that records can be read.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  // Delete 1 record.
  DeleteWorkload(0, 1, producer_client(), tables[0]->name());

  // Ensure that record is deleted on both universes.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  // Delete 2nd record but replicate at a low timestamp (timestamp lower than insertion timestamp).
  FLAGS_TEST_twodc_write_hybrid_time = true;
  DeleteWorkload(1, 2, producer_client(), tables[0]->name());

  // Verify that record exists on consumer universe, but is deleted from producer universe.
  ASSERT_OK(VerifyNumRecords(tables[0]->name(), producer_client(), 0));
  ASSERT_OK(VerifyNumRecords(tables[1]->name(), consumer_client(), 1));

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
  Destroy();
}

TEST_P(TwoDCTest, BiDirectionalWrites) {
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {2}, 1));

  // Setup bi-directional replication.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables_reverse;
  producer_tables_reverse.push_back(tables[1]);
  ASSERT_OK(SetupUniverseReplication(
      consumer_cluster(), producer_cluster(), producer_client(), kUniverseId,
      producer_tables_reverse));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));
  ASSERT_OK(CorrectlyPollingAllTablets(producer_cluster(), 2));

  // Write non-conflicting rows on both clusters.
  WriteWorkload(0, 5, producer_client(), tables[0]->name());
  WriteWorkload(5, 10, consumer_client(), tables[1]->name());

  // Ensure that records are the same on both clusters.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));
  // Ensure that both universes have all 10 records.
  ASSERT_OK(VerifyNumRecords(tables[0]->name(), producer_client(), 10));

  // Write conflicting records on both clusters (1 clusters adds key, another deletes key).
  std::vector<std::thread> threads;
  for (int i = 0; i < 2; ++i) {
    auto client = i == 0 ? producer_client() : consumer_client();
    int index = i;
    bool is_delete = i == 0;
    threads.emplace_back([this, client, index, tables, is_delete] {
      WriteWorkload(10, 20, client, tables[index]->name(), is_delete);
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // Ensure that same records exist on both universes.
  VerifyWrittenRecords(tables[0]->name(), tables[1]->name());

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
  Destroy();
}

TEST_P(TwoDCTest, ToggleReplicationEnabled) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {2}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  // Verify that universe is now ACTIVE
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, &resp));

  // After we know the universe is ACTIVE, make sure all tablets are getting polled.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));

  // Disable the replication and ensure no tablets are being polled
  ASSERT_OK(ToggleUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, false));
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 0));

  // Enable replication and ensure that all the tablets start being polled again
  ASSERT_OK(ToggleUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, true));
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));

  Destroy();
}

TEST_P(TwoDCTest, TestDeleteUniverse) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);

  auto tables = ASSERT_RESULT(SetUpWithParams({8, 4}, {6, 6}, replication_factor));

  ASSERT_OK(SetupUniverseReplication(producer_cluster(), consumer_cluster(), consumer_client(),
      kUniverseId, {tables[0], tables[2]} /* all producer tables */));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, &resp));

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 12));

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));

  ASSERT_OK(VerifyUniverseReplicationDeleted(consumer_cluster(), consumer_client(), kUniverseId,
      FLAGS_cdc_read_rpc_timeout_ms * 2));

  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 0));

  Destroy();
}

TEST_P(TwoDCTest, TestWalRetentionSet) {
  FLAGS_cdc_wal_retention_time_secs = 8 * 3600;

  auto tables = ASSERT_RESULT(SetUpWithParams({8, 4, 4, 12}, {8, 4, 12, 8}, 3));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  producer_tables.reserve(tables.size() / 2);
  for (int i = 0; i < tables.size(); i += 2) {
    producer_tables.push_back(tables[i]);
  }
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, &resp));

  // After creating the cluster, make sure all 32 tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 32));

  cdc::VerifyWalRetentionTime(producer_cluster(), "test_table_", FLAGS_cdc_wal_retention_time_secs);

  Destroy();
}

TEST_P(TwoDCTest, TestProducerUniverseExpansion) {
  // Test that after new node(s) are added to producer universe, we are able to get replicated data
  // from the new node(s).
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {2}, 1));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));

  WriteWorkload(0, 5, producer_client(), tables[0]->name());

  // Add new node and wait for tablets to be rebalanced.
  // After rebalancing, each node will be leader for 1 tablet.
  ASSERT_OK(producer_cluster_->AddTabletServer());
  ASSERT_OK(producer_cluster_->WaitForTabletServerCount(2));
  ASSERT_OK(WaitFor([&] () { return producer_client()->IsLoadBalanced(2); },
                    MonoDelta::FromSeconds(kRpcTimeout), "IsLoadBalanced"));

  // Check that all tablets continue to be polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));

  // Write some more rows. Note that some of these rows will have the new node as the tablet leader.
  WriteWorkload(6, 10, producer_client(), tables[0]->name());

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  Destroy();
}

TEST_P(TwoDCTest, ApplyOperationsRandomFailures) {
  SetAtomicFlag(0.25, &FLAGS_respond_write_failed_probability);

  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({1}, {1}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  // Set up bi-directional replication.
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  consumer_tables.reserve(1);
  consumer_tables.push_back(tables[1]);
  ASSERT_OK(SetupUniverseReplication(
      consumer_cluster(), producer_cluster(), producer_client(), kUniverseId, consumer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 1));
  ASSERT_OK(CorrectlyPollingAllTablets(producer_cluster(), 1));

  // Write 1000 entries to each cluster.
  std::thread t1([&]() { WriteWorkload(0, 1000, producer_client(), tables[0]->name()); });
  std::thread t2([&]() { WriteWorkload(1000, 2000, consumer_client(), tables[1]->name()); });

  t1.join();
  t2.join();

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  // Stop replication on consumer.
  ASSERT_OK(DeleteUniverseReplication(kUniverseId));

  // Stop replication on producer
  ASSERT_OK(DeleteUniverseReplication(kUniverseId, producer_client(), producer_cluster()));
  Destroy();
}

TEST_P(TwoDCTest, TestInsertDeleteWorkloadWithRestart) {
  // Good test for batching, make sure we can handle operations on the same key with different
  // hybrid times. Then, do a restart and make sure we can successfully bootstrap the batched data.
  // In additional, make sure we write exactly num_total_ops / batch_size batches to the cluster to
  // ensure batching is actually enabled.
  constexpr uint32_t num_ops_per_workload = 100;
  constexpr uint32_t num_runs = 5;

  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({1}, {1}, replication_factor));

  WriteWorkload(0, num_ops_per_workload, producer_client(), tables[0]->name());
  for (int i = 0; i < num_runs; i++) {
    WriteWorkload(0, num_ops_per_workload, producer_client(), tables[0]->name(), true);
    WriteWorkload(0, num_ops_per_workload, producer_client(), tables[0]->name());
  }

  // Count the number of ops in total.
  uint32_t expected_num_writes =
      (num_ops_per_workload * (num_runs * 2 + 1)) / FLAGS_cdc_max_apply_batch_num_records;

  LOG(INFO) << "expected num writes: " <<expected_num_writes;

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  AssertLoggedWaitFor([&]() {
    return GetSuccessfulWriteOps(consumer_cluster()) == expected_num_writes;
  }, MonoDelta::FromSeconds(60), "Wait for all batches to finish.");

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  ASSERT_OK(consumer_cluster_->RestartSync());

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));
  // Stop replication on consumer.
  ASSERT_OK(DeleteUniverseReplication(kUniverseId));

  Destroy();
}

} // namespace enterprise
} // namespace yb
