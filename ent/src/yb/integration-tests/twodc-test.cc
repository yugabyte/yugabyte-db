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

#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
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
DECLARE_bool(mock_get_changes_response_for_consumer_testing);

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

constexpr int kRpcTimeout = 30;

class TwoDCTest : public YBTest {
 public:
  Result<std::vector<std::shared_ptr<client::YBTable>>>
      SetUpWithParams(std::vector<uint32_t> num_consumer_tablets,
                      std::vector<uint32_t> num_producer_tablets,
                      uint32_t replication_factor) {
    YBTest::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = replication_factor;
    FLAGS_replication_factor = replication_factor;
    FLAGS_mock_get_changes_response_for_consumer_testing = true;
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

    YBSchemaBuilder b;
    b.AddColumn("c0")->Type(INT32)->NotNull()->HashPrimaryKey();
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

  Status CreateTable(
      uint32_t idx, uint32_t num_tablets, YBClient* client, std::vector<YBTableName>* tables) {
    YBTableName table = YBTableName("test_namespace", Format("test_table_$0", idx));
    RETURN_NOT_OK(client->CreateNamespaceIfNotExists(table.namespace_name()));

    // Add a table, make sure it reports itself.
    gscoped_ptr<YBTableCreator> table_creator(client->NewTableCreator());
    RETURN_NOT_OK(table_creator->table_name(table)
                 .schema(&schema_)
                 .table_type(YBTableType::YQL_TABLE_TYPE)
                 .num_tablets(num_tablets)
                 .Create());
    tables->push_back(table);
    return Status::OK();
  }

  Status SetupUniverseReplication(
      const std::string& universe_id, const std::vector<std::shared_ptr<client::YBTable>>& tables) {
    master::SetupUniverseReplicationRequestPB req;
    master::SetupUniverseReplicationResponsePB resp;

    req.set_producer_id(universe_id);
    string master_addr = producer_cluster_->GetMasterAddresses();
    auto hp_vec = VERIFY_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, req.mutable_producer_master_addresses());

    req.mutable_producer_table_ids()->Reserve(tables.size());
    for (const auto& table : tables) {
      req.add_producer_table_ids(table->id());
    }

    auto master_proxy = std::make_shared<master::MasterServiceProxy>(
      &consumer_client_->proxy_cache(),
      consumer_cluster_->leader_mini_master()->bound_rpc_addr());

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    RETURN_NOT_OK(master_proxy->SetupUniverseReplication(req, &resp, &rpc));
    if (resp.has_error()) {
      return STATUS(IllegalState, "Failed setting up universe replication");
    }
    return Status::OK();
  }

  Status GetUniverseReplication(
      const std::string& universe_id, master::GetUniverseReplicationResponsePB* resp) {
    master::GetUniverseReplicationRequestPB req;
    req.set_producer_id(universe_id);

    auto master_proxy = std::make_shared<master::MasterServiceProxy>(
      &consumer_client_->proxy_cache(),
      consumer_cluster_->leader_mini_master()->bound_rpc_addr());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    RETURN_NOT_OK(master_proxy->GetUniverseReplication(req, resp, &rpc));
    if (resp->has_error()) {
      return STATUS(IllegalState, "Failed getting universe replication");
    }
    return Status::OK();
  }

  Status GetCDCStreamForTable(
      const std::string& table_id, master::ListCDCStreamsResponsePB* resp) {
    master::ListCDCStreamsRequestPB req;
    req.set_table_id(table_id);

    RETURN_NOT_OK(producer_cluster_->leader_mini_master()->master()->catalog_manager()->
        ListCDCStreams(&req, resp));
    if (resp->has_error()) {
      return STATUS(IllegalState, "Failed getting CDC stream for table");
    }
    return Status::OK();
  }

  void Destroy() {
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

  void WriteWorkload(uint32_t start, uint32_t end, YBClient* client, const YBTableName& table) {
    auto session = client->NewSession();
    client::TableHandle table_handle;
    ASSERT_OK(table_handle.Open(table, client));
    std::vector<std::shared_ptr<client::YBqlOp>> ops;

    for (uint32_t i = start; i < end; i++) {
      auto op = table_handle.NewInsertOp();
      int32_t key = i;
      auto req = op->mutable_request();
      QLAddInt32HashValue(req, key);
      ASSERT_OK(session->ApplyAndFlush(op));
    }
  }

  std::vector<string> ScanToStrings(const YBTableName& table_name, YBClient* client) {
    client::TableHandle table;
    EXPECT_OK(table.Open(table_name, client));
    auto result = ScanTableToStrings(table);
    std::sort(result.begin(), result.end());
    return result;
  }

  void VerifyWrittenRecords(const YBTableName& table_name) {
    auto producer_results = ScanToStrings(table_name, producer_client_.get());
    auto consumer_results = ScanToStrings(table_name, consumer_client_.get());
    ASSERT_EQ(producer_results, consumer_results);
  }

  Status InitCDCConsumer() {
    master::ListTablesRequestPB tables_req;
    master::ListTablesResponsePB tables_resp;
    tables_req.set_exclude_system_tables(true);

    RETURN_NOT_OK(consumer_cluster_->leader_mini_master()->master()->catalog_manager()->
                  ListTables(&tables_req, &tables_resp));

    auto master_addrs = producer_cluster_->GetMasterAddresses();
    auto consumer_info = VERIFY_RESULT(
        master::enterprise::TEST_GetConsumerProducerTableMap(master_addrs, tables_resp));
    auto universe_uuid = "universe_uuid";

    return consumer_cluster_->leader_mini_master()->master()->catalog_manager()->
           InitCDCConsumer(consumer_info, master_addrs, universe_uuid);
  }

  YBClient* producer_client() {
    return producer_client_.get();
  }

  YBClient* consumer_client() {
    return consumer_client_.get();
  }


  uint32_t NumProducerTabletsPolled() {
    uint32_t size = 0;
    for (const auto& mini_tserver : consumer_cluster_->mini_tablet_servers()) {
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

  Status CorrectlyPollingAllTablets(uint32_t num_producer_tablets) {
    return LoggedWaitFor([=]() -> Result<bool> {
      static int i = 0;
      constexpr int kNumIterationsWithCorrectResult = 5;
      if (NumProducerTabletsPolled() == num_producer_tablets) {
        if (i++ == kNumIterationsWithCorrectResult) {
          i = 0;
          return true;
        }
      } else {
        i = 0;
      }
      return false;
    }, MonoDelta::FromSeconds(30), "Num producer tablets being polled");
  }

  std::unique_ptr<MiniCluster> producer_cluster_;
  std::unique_ptr<MiniCluster> consumer_cluster_;

 private:

  std::unique_ptr<YBClient> producer_client_;
  std::unique_ptr<YBClient> consumer_client_;

  YBSchema schema_;
};

TEST_F(TwoDCTest, SetupUniverseReplication) {
  static const std::string universe_id = "universe_A";
  auto tables = ASSERT_RESULT(SetUpWithParams({8, 4, 4, 12}, {8, 4, 12, 8}, 3));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  producer_tables.reserve(tables.size() / 2);
  for (int i = 0; i < tables.size(); i += 2) {
    producer_tables.push_back(tables[i]);
  }
  ASSERT_OK(SetupUniverseReplication(universe_id, producer_tables));

  // Sleep for some time to give enough time for CDC streams and subscribers to be setup.
  SleepFor(MonoDelta::FromMilliseconds(500));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(GetUniverseReplication(universe_id, &resp));
  ASSERT_EQ(resp.producer_id(), universe_id);
  ASSERT_EQ(resp.producer_tables_size(), producer_tables.size());
  for (int i = 0; i < producer_tables.size(); i++) {
    ASSERT_EQ(resp.producer_tables(i).table_id(), producer_tables[i]->id());
  }

  // Verify that CDC streams were created on producer for all tables.
  for (int i = 0; i < producer_tables.size(); i++) {
    master::ListCDCStreamsResponsePB stream_resp;
    ASSERT_OK(GetCDCStreamForTable(producer_tables[i]->id(), &stream_resp));
    ASSERT_EQ(stream_resp.streams_size(), 1);
    ASSERT_EQ(stream_resp.streams(0).table_id(), producer_tables[i]->id());
  }

  Destroy();
}

TEST_F(TwoDCTest, PollWithConsumerRestart) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({8, 4, 4, 12}, {8, 4, 12, 8}, replication_factor));

  ASSERT_OK(InitCDCConsumer());

  // After creating the cluster, make sure all 32 tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(32));

  consumer_cluster_->mini_tablet_server(0)->Shutdown();

  // After shutting down a consumer node.
  if (replication_factor > 1) {
    ASSERT_OK(CorrectlyPollingAllTablets(32));
  }

  ASSERT_OK(consumer_cluster_->mini_tablet_server(0)->Start());

  // After restarting the node.
  ASSERT_OK(CorrectlyPollingAllTablets(32));

  ASSERT_OK(consumer_cluster_->RestartSync());

  // After consumer cluster restart.
  ASSERT_OK(CorrectlyPollingAllTablets(32));

  Destroy();
}

TEST_F(TwoDCTest, PollWithProducerRestart) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({8, 4, 4, 12}, {8, 4, 12, 8}, replication_factor));

  ASSERT_OK(InitCDCConsumer());

  // After creating the cluster, make sure all 32 tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(32));

  producer_cluster_->mini_tablet_server(0)->Shutdown();

  // After stopping a producer node.
  ASSERT_OK(CorrectlyPollingAllTablets(32));

  ASSERT_OK(producer_cluster_->mini_tablet_server(0)->Start());

  // After starting the node.
  ASSERT_OK(CorrectlyPollingAllTablets(32));

  ASSERT_OK(producer_cluster_->RestartSync());

  // After producer cluster restart.
  ASSERT_OK(CorrectlyPollingAllTablets(32));

  Destroy();


}

} // namespace enterprise
} // namespace yb
