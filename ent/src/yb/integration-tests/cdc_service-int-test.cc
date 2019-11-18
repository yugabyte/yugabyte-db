// Copyright (c) YugaByte, Inc.

#include <boost/lexical_cast.hpp>

#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol-test-util.h"
#include "yb/common/ql_value.h"
#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/client/error.h"
#include "yb/client/table.h"
#include "yb/client/table_handle.h"
#include "yb/client/session.h"
#include "yb/client/yb_table_name.h"
#include "yb/client/yb_op.h"
#include "yb/client/client-test-util.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value_type.h"

#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/master_defaults.h"
#include "yb/rpc/messenger.h"
#include "yb/tablet/tablet.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/slice.h"
#include "yb/yql/cql/ql/util/errcodes.h"
#include "yb/yql/cql/ql/util/statement_result.h"

DECLARE_int32(cdc_wal_retention_time_secs);
DECLARE_int32(cdc_state_checkpoint_update_interval_ms);

METRIC_DECLARE_entity(cdc);
METRIC_DECLARE_gauge_int64(last_read_opid_index);

namespace yb {
namespace cdc {

using client::TableHandle;
using client::YBSessionPtr;
using rpc::RpcController;

const std::string kCDCTestKeyspace = "my_keyspace";
const std::string kCDCTestTableName = "cdc_test_table";
const client::YBTableName kTableName(YQL_DATABASE_CQL, kCDCTestKeyspace, kCDCTestTableName);

class CDCServiceTest : public YBMiniClusterTestBase<MiniCluster> {
 protected:
  void SetUp() override {
    YBMiniClusterTestBase::SetUp();

    MiniClusterOptions opts;
    opts.num_tablet_servers = server_count();
    opts.num_masters = 1;
    cluster_.reset(new MiniCluster(env_.get(), opts));
    ASSERT_OK(cluster_->Start());

    client_ = ASSERT_RESULT(cluster_->CreateClient());
    cdc_proxy_ = std::make_unique<CDCServiceProxy>(
        &client_->proxy_cache(),
        HostPort::FromBoundEndpoint(cluster_->mini_tablet_server(0)->bound_rpc_addr()));

    CreateTable(tablet_count(), &table_);
  }

  void DoTearDown() override {
    Result<bool> exist = client_->TableExists(kTableName);
    ASSERT_OK(exist);

    if (exist.get()) {
      ASSERT_OK(client_->DeleteTable(kTableName));
    }

    client_.reset();

    if (cluster_) {
      cluster_->Shutdown();
      cluster_.reset();
    }

    YBMiniClusterTestBase::DoTearDown();
  }

  void CreateTable(int num_tablets, TableHandle* table);
  void GetTablet(std::string* tablet_id);

  virtual int server_count() { return 1; }
  virtual int tablet_count() { return 1; }

  std::unique_ptr<CDCServiceProxy> cdc_proxy_;
  std::unique_ptr<client::YBClient> client_;
  TableHandle table_;
};

void CDCServiceTest::CreateTable(int num_tablets, TableHandle* table) {
  ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name(),
                                                kTableName.namespace_type()));

  client::YBSchemaBuilder builder;
  builder.AddColumn("key")->Type(INT32)->HashPrimaryKey()->NotNull();
  builder.AddColumn("int_val")->Type(INT32);
  builder.AddColumn("string_val")->Type(STRING);

  TableProperties table_properties;
  table_properties.SetTransactional(true);
  builder.SetTableProperties(table_properties);

  ASSERT_OK(table->Create(kTableName, num_tablets, client_.get(), &builder));
}

void AssertChangeRecords(const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>& changes,
                         int32_t expected_int, std::string expected_str) {
  ASSERT_EQ(changes.size(), 2);
  ASSERT_EQ(changes[0].key(), "int_val");
  ASSERT_EQ(changes[0].value().int32_value(), expected_int);
  ASSERT_EQ(changes[1].key(), "string_val");
  ASSERT_EQ(changes[1].value().string_value(), expected_str);
}

void VerifyCdcState(client::YBClient* client) {
  client::TableHandle table;
  client::YBTableName cdc_state_table(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
  ASSERT_OK(table.Open(cdc_state_table, client));
  ASSERT_EQ(1, boost::size(client::TableRange(table)));
  const auto& row = client::TableRange(table).begin();
  string checkpoint = row->column(2).string_value();
  size_t split = checkpoint.find(".");
  auto index = boost::lexical_cast<int>(checkpoint.substr(split + 1, string::npos));
  // Verify that op id index has been advanced and is not 0.
  ASSERT_GT(index, 0);
}

void CDCServiceTest::GetTablet(std::string* tablet_id) {
  std::vector<TabletId> tablet_ids;
  std::vector<std::string> ranges;
  ASSERT_OK(client_->GetTablets(kTableName, 0 /* max_tablets */, &tablet_ids, &ranges));
  ASSERT_EQ(tablet_ids.size(), tablet_count());
  *tablet_id = tablet_ids[0];
}

TEST_F(CDCServiceTest, TestCreateCDCStream) {
  CDCStreamId stream_id;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id);

  TableId table_id;
  std::unordered_map<std::string, std::string> options;
  ASSERT_OK(client_->GetCDCStream(stream_id, &table_id, &options));
  ASSERT_EQ(table_id, table_.table()->id());
}

TEST_F(CDCServiceTest, TestCreateCDCStreamWithDefaultRententionTime) {
  // Set default WAL retention time to 10 hours.
  FLAGS_cdc_wal_retention_time_secs = 36000;

  CDCStreamId stream_id;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id);

  TableId table_id;
  std::unordered_map<std::string, std::string> options;
  ASSERT_OK(client_->GetCDCStream(stream_id, &table_id, &options));

  // Verify that the wal retention time was set at the tablet level.
  VerifyWalRetentionTime(cluster_.get(), kCDCTestTableName, FLAGS_cdc_wal_retention_time_secs);
}

TEST_F(CDCServiceTest, TestDeleteCDCStream) {
  CDCStreamId stream_id;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id);

  TableId table_id;
  std::unordered_map<std::string, std::string> options;
  ASSERT_OK(client_->GetCDCStream(stream_id, &table_id, &options));
  ASSERT_EQ(table_id, table_.table()->id());

  ASSERT_OK(client_->DeleteCDCStream(stream_id));

  // Check that the stream no longer exists.
  table_id.clear();
  options.clear();
  Status s = client_->GetCDCStream(stream_id, &table_id, &options);
  ASSERT_TRUE(s.IsNotFound());
}

TEST_F(CDCServiceTest, TestGetChanges) {
  CDCStreamId stream_id;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id);

  std::string tablet_id;
  GetTablet(&tablet_id);

  const auto& tserver = cluster_->mini_tablet_server(0)->server();
  // Use proxy for to most accurately simulate normal requests.
  const auto& proxy = tserver->proxy();

  // Insert test rows.
  tserver::WriteRequestPB write_req;
  tserver::WriteResponsePB write_resp;
  write_req.set_tablet_id(tablet_id);
  {
    RpcController rpc;
    AddTestRowInsert(1, 11, "key1", &write_req);
    AddTestRowInsert(2, 22, "key2", &write_req);

    SCOPED_TRACE(write_req.DebugString());
    ASSERT_OK(proxy->Write(write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  // Get CDC changes.
  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  change_req.set_tablet_id(tablet_id);
  change_req.set_stream_id(stream_id);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(0);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(0);

  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());
    ASSERT_EQ(change_resp.records_size(), 2);

    std::pair<int, std::string> expected_results[2] =
        {std::make_pair(11, "key1"), std::make_pair(22, "key2")};
    for (int i = 0; i < change_resp.records_size(); i++) {
      ASSERT_EQ(change_resp.records(i).operation(), CDCRecordPB::WRITE);

      // Check the key.
      ASSERT_NO_FATALS(AssertIntKey(change_resp.records(i).key(), i + 1));

      // Check the change records.
      ASSERT_NO_FATALS(AssertChangeRecords(change_resp.records(i).changes(),
                                           expected_results[i].first,
                                           expected_results[i].second));
    }

    // Verify the CDC Service-level metrics match what we just did.
    auto cdc_service = dynamic_cast<CDCServiceImpl*>(
        tserver->rpc_server()->service_pool("yb.cdc.CDCService")->TEST_get_service().get());
    auto metrics = cdc_service->GetCDCTabletMetrics({"" /* UUID */, stream_id, tablet_id});
    ASSERT_EQ(metrics->last_read_opid_index->value(), metrics->last_readable_opid_index->value());
    ASSERT_EQ(metrics->last_read_opid_index->value(), change_resp.records_size() + 1 /* checkpt */);
    ASSERT_EQ(metrics->rpc_payload_bytes_responded->TotalCount(), 1);
  }

  // Insert another row.
  {
    write_req.Clear();
    write_req.set_tablet_id(tablet_id);
    AddTestRowInsert(3, 33, "key3", &write_req);

    RpcController rpc;
    SCOPED_TRACE(write_req.DebugString());
    ASSERT_OK(proxy->Write(write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  // Get next set of changes.
  // Copy checkpoint received from previous GetChanges CDC request.
  change_req.mutable_from_checkpoint()->CopyFrom(change_resp.checkpoint());
  change_resp.Clear();
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());
    ASSERT_EQ(change_resp.records_size(), 1);
    ASSERT_EQ(change_resp.records(0).operation(), CDCRecordPB_OperationType_WRITE);

    // Check the key.
    ASSERT_NO_FATALS(AssertIntKey(change_resp.records(0).key(), 3));

    // Check the change records.
    ASSERT_NO_FATALS(AssertChangeRecords(change_resp.records(0).changes(), 33, "key3"));
  }

  // Delete a row.
  {
    write_req.Clear();
    write_req.set_tablet_id(tablet_id);
    AddTestRowDelete(1, &write_req);

    RpcController rpc;
    SCOPED_TRACE(write_req.DebugString());
    ASSERT_OK(proxy->Write(write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  // Get next set of changes.
  // Copy checkpoint received from previous GetChanges CDC request.
  change_req.mutable_from_checkpoint()->CopyFrom(change_resp.checkpoint());
  change_resp.Clear();
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());
    ASSERT_EQ(change_resp.records_size(), 1);
    ASSERT_EQ(change_resp.records(0).operation(), CDCRecordPB_OperationType_DELETE);

    // Check the key deleted.
    ASSERT_NO_FATALS(AssertIntKey(change_resp.records(0).key(), 1));
  }
}

TEST_F(CDCServiceTest, TestGetChangesInvalidStream) {
  std::string tablet_id;
  GetTablet(&tablet_id);

  // Get CDC changes for non-existent stream.
  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  change_req.set_tablet_id(tablet_id);
  change_req.set_stream_id("InvalidStreamId");
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(0);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(0);

  RpcController rpc;
  ASSERT_NO_FATALS(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
  ASSERT_TRUE(change_resp.has_error());
}

TEST_F(CDCServiceTest, TestGetCheckpoint) {
  CDCStreamId stream_id;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id);

  std::string tablet_id;
  GetTablet(&tablet_id);

  GetCheckpointRequestPB req;
  GetCheckpointResponsePB resp;

  req.set_tablet_id(tablet_id);
  req.set_stream_id(stream_id);

  {
    RpcController rpc;
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(cdc_proxy_->GetCheckpoint(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
    ASSERT_EQ(resp.checkpoint().op_id().term(), 0);
    ASSERT_EQ(resp.checkpoint().op_id().index(), 0);
  }
}

class CDCServiceTestMultipleServers : public CDCServiceTest {
 public:
  virtual int server_count() override { return 2; }
  virtual int tablet_count() override { return 4; }
};

TEST_F_EX(CDCServiceTest, TestListTablets, CDCServiceTestMultipleServers) {
  CDCStreamId stream_id;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id);

  std::string tablet_id;
  GetTablet(&tablet_id);

  ListTabletsRequestPB req;
  ListTabletsResponsePB resp;

  req.set_stream_id(stream_id);

  // Test a simple query for all tablets.
  {
    RpcController rpc;
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(cdc_proxy_->ListTablets(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());

    ASSERT_EQ(resp.tablets_size(), tablet_count());
    ASSERT_EQ(resp.tablets(0).tablet_id(), tablet_id);
  }

  // Query for tablets only on the first server.  We should only get a subset.
  {
    req.set_local_only(true);
    RpcController rpc;
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(cdc_proxy_->ListTablets(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());

    ASSERT_EQ(resp.tablets_size(), tablet_count() / server_count());
  }
}

TEST_F_EX(CDCServiceTest, TestGetChangesProxyRouting, CDCServiceTestMultipleServers) {
  CDCStreamId stream_id;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id);

  // Figure out [1] all tablets and [2] which ones are local to the first server.
  std::vector<std::string> local_tablets, all_tablets;
  for (bool is_local : {true, false}) {
    RpcController rpc;
    ListTabletsRequestPB req;
    ListTabletsResponsePB resp;
    req.set_stream_id(stream_id);
    req.set_local_only(is_local);
    ASSERT_OK(cdc_proxy_->ListTablets(req, &resp, &rpc));
    ASSERT_FALSE(resp.has_error());
    auto& cur_tablets = is_local ? local_tablets : all_tablets;
    for (int i = 0; i < resp.tablets_size(); ++i) {
      cur_tablets.push_back(resp.tablets(i).tablet_id());
    }
    std::sort(cur_tablets.begin(), cur_tablets.end());
  }
  ASSERT_LT(local_tablets.size(), all_tablets.size());
  ASSERT_LT(0, local_tablets.size());
  {
    // Overlap between these two lists should be all the local tablets
    std::vector<std::string> tablet_intersection;
    std::set_intersection(local_tablets.begin(), local_tablets.end(),
        all_tablets.begin(), all_tablets.end(),
        std::back_inserter(tablet_intersection));
    ASSERT_TRUE(std::equal(local_tablets.begin(), local_tablets.end(),
        tablet_intersection.begin()));
  }
  // Difference should be all tablets on the other server.
  std::vector<std::string> remote_tablets;
  std::set_difference(all_tablets.begin(), all_tablets.end(),
      local_tablets.begin(), local_tablets.end(),
      std::back_inserter(remote_tablets));
  ASSERT_LT(0, remote_tablets.size());
  ASSERT_EQ(all_tablets.size() - local_tablets.size(), remote_tablets.size());

  // Insert test rows, equal amount per tablet.
  int cur_row = 1;
  int to_write = 2;
  for (bool is_local : {true, false}) {
    const auto& tserver = cluster_->mini_tablet_server(is_local?0:1)->server();
    // Use proxy for to most accurately simulate normal requests.
    const auto& proxy = tserver->proxy();
    auto& cur_tablets = is_local ? local_tablets : remote_tablets;
    for (auto& tablet_id : cur_tablets) {
      tserver::WriteRequestPB write_req;
      tserver::WriteResponsePB write_resp;
      write_req.set_tablet_id(tablet_id);
      RpcController rpc;
      for (int i = 1; i <= to_write; ++i) {
        AddTestRowInsert(cur_row, 11 * cur_row, "key" + std::to_string(cur_row), &write_req);
        ++cur_row;
      }

      SCOPED_TRACE(write_req.DebugString());
      ASSERT_OK(proxy->Write(write_req, &write_resp, &rpc));
      SCOPED_TRACE(write_resp.DebugString());
      ASSERT_FALSE(write_resp.has_error());
    }
  }

  // Query for all tablets on the first server. Ensure the non-local ones have errors.
  for (bool is_local : {true, false}) {
    auto& cur_tablets = is_local ? local_tablets : remote_tablets;
    for (auto tablet_id : cur_tablets) {
      std::vector<bool> proxy_options{false};
      // Verify that remote tablet queries work only when proxy forwarding is enabled.
      if (!is_local) proxy_options.push_back(true);
      for (auto use_proxy : proxy_options) {
        GetChangesRequestPB change_req;
        GetChangesResponsePB change_resp;
        change_req.set_tablet_id(tablet_id);
        change_req.set_stream_id(stream_id);
        change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(0);
        change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(0);
        change_req.set_serve_as_proxy(use_proxy);
        RpcController rpc;
        SCOPED_TRACE(change_req.DebugString());
        ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
        SCOPED_TRACE(change_resp.DebugString());
        bool should_error = !(is_local || use_proxy);
        ASSERT_EQ(change_resp.has_error(), should_error);
        if (!should_error) {
          ASSERT_EQ(to_write, change_resp.records_size());
        }
      }
    }
  }

  // Verify the CDC metrics match what we just did.
  const auto& tserver = cluster_->mini_tablet_server(0)->server();
  auto cdc_service = dynamic_cast<CDCServiceImpl*>(
      tserver->rpc_server()->service_pool("yb.cdc.CDCService")->TEST_get_service().get());
  auto server_metrics = cdc_service->GetCDCServerMetrics();
  ASSERT_EQ(server_metrics->cdc_rpc_proxy_count->value(), remote_tablets.size());
}

TEST_F(CDCServiceTest, TestOnlyGetLocalChanges) {
  CDCStreamId stream_id;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id);

  std::string tablet_id;
  GetTablet(&tablet_id);

  const auto& proxy = cluster_->mini_tablet_server(0)->server()->proxy();

  {
    // Insert local test rows.
    tserver::WriteRequestPB write_req;
    tserver::WriteResponsePB write_resp;
    write_req.set_tablet_id(tablet_id);
    RpcController rpc;
    AddTestRowInsert(1, 11, "key1", &write_req);
    AddTestRowInsert(2, 22, "key2", &write_req);

    SCOPED_TRACE(write_req.DebugString());
    ASSERT_OK(proxy->Write(write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  {
    // Insert remote test rows.
    tserver::WriteRequestPB write_req;
    tserver::WriteResponsePB write_resp;
    write_req.set_tablet_id(tablet_id);
    // Apply at the lowest possible hybrid time.
    write_req.set_external_hybrid_time(yb::kInitialHybridTimeValue);

    RpcController rpc;
    AddTestRowInsert(1, 11, "key1_ext", &write_req);
    AddTestRowInsert(3, 33, "key3_ext", &write_req);

    SCOPED_TRACE(write_req.DebugString());
    ASSERT_OK(proxy->Write(write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  auto CheckChangesAndTable = [&]() {
    // Get CDC changes.
    GetChangesRequestPB change_req;
    GetChangesResponsePB change_resp;

    change_req.set_tablet_id(tablet_id);
    change_req.set_stream_id(stream_id);
    change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(0);
    change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(0);

    {
      // Make sure only the two local test rows show up.
      RpcController rpc;
      SCOPED_TRACE(change_req.DebugString());
      ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
      SCOPED_TRACE(change_resp.DebugString());
      ASSERT_FALSE(change_resp.has_error());
      ASSERT_EQ(change_resp.records_size(), 2);

      std::pair<int, std::string> expected_results[2] =
          {std::make_pair(11, "key1"), std::make_pair(22, "key2")};
      for (int i = 0; i < change_resp.records_size(); i++) {
        ASSERT_EQ(change_resp.records(i).operation(), CDCRecordPB::WRITE);

        // Check the key.
        ASSERT_NO_FATALS(AssertIntKey(change_resp.records(i).key(), i + 1));

        // Check the change records.
        ASSERT_NO_FATALS(AssertChangeRecords(change_resp.records(i).changes(),
                                             expected_results[i].first,
                                             expected_results[i].second));
      }
    }

    // Now, fetch the entire table and ensure that we fetch all the keys inserted.
    client::TableHandle table;
    EXPECT_OK(table.Open(table_.table()->name(), client_.get()));
    auto result = ScanTableToStrings(table);
    std::sort(result.begin(), result.end());

    ASSERT_EQ(3, result.size());

    // Make sure that key1 and not key1_ext shows up, since we applied key1_ext at a lower hybrid
    // time.
    ASSERT_EQ("{ int32:1, int32:11, string:\"key1\" }", result[0]);
    ASSERT_EQ("{ int32:2, int32:22, string:\"key2\" }", result[1]);
    ASSERT_EQ("{ int32:3, int32:33, string:\"key3_ext\" }", result[2]);
  };

  ASSERT_NO_FATALS(CheckChangesAndTable());

  ASSERT_OK(cluster_->RestartSync());

  ASSERT_OK(WaitFor([&](){
    std::shared_ptr<tablet::TabletPeer> tablet_peer;
    if (!cluster_->mini_tablet_server(0)->server()->tablet_manager()->
        LookupTablet(tablet_id, &tablet_peer)) {
      return false;
    }
    return tablet_peer->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY;
  }, MonoDelta::FromSeconds(30), "Wait until tablet has a leader."));

  ASSERT_NO_FATALS(CheckChangesAndTable());

}

TEST_F(CDCServiceTest, TestCheckpointUpdatedForRemoteRows) {
  CDCStreamId stream_id;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id);

  std::string tablet_id;
  GetTablet(&tablet_id);

  const auto& proxy = cluster_->mini_tablet_server(0)->server()->proxy();

  {
    // Insert remote test rows.
    tserver::WriteRequestPB write_req;
    tserver::WriteResponsePB write_resp;
    write_req.set_tablet_id(tablet_id);
    // Apply at the lowest possible hybrid time.
    write_req.set_external_hybrid_time(yb::kInitialHybridTimeValue);

    RpcController rpc;
    AddTestRowInsert(1, 11, "key1_ext", &write_req);
    AddTestRowInsert(3, 33, "key3_ext", &write_req);

    SCOPED_TRACE(write_req.DebugString());
    ASSERT_OK(proxy->Write(write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  auto CheckChanges = [&]() {
    // Get CDC changes.
    GetChangesRequestPB change_req;
    GetChangesResponsePB change_resp;

    change_req.set_tablet_id(tablet_id);
    change_req.set_stream_id(stream_id);
    change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(0);
    change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(0);

    {
      // Make sure that checkpoint is updated even when there are no CDC records.
      RpcController rpc;
      SCOPED_TRACE(change_req.DebugString());
      ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
      SCOPED_TRACE(change_resp.DebugString());
      ASSERT_FALSE(change_resp.has_error());
      ASSERT_EQ(change_resp.records_size(), 0);
      ASSERT_GT(change_resp.checkpoint().op_id().index(), 0);
    }
  };

  ASSERT_NO_FATALS(CheckChanges());
}

// Test to ensure that cdc_state table's checkpoint is updated as expected.
// This also tests for #2897 to ensure that cdc_state table checkpoint is not overwritten to 0.0
// in case the consumer does not send from checkpoint.
TEST_F(CDCServiceTest, TestCheckpointUpdate) {
  FLAGS_cdc_state_checkpoint_update_interval_ms = 0;

  CDCStreamId stream_id;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id);

  std::string tablet_id;
  GetTablet(&tablet_id);

  const auto& proxy = cluster_->mini_tablet_server(0)->server()->proxy();

  // Insert test rows.
  tserver::WriteRequestPB write_req;
  tserver::WriteResponsePB write_resp;
  write_req.set_tablet_id(tablet_id);
  {
    RpcController rpc;
    AddTestRowInsert(1, 11, "key1", &write_req);
    AddTestRowInsert(2, 22, "key2", &write_req);

    SCOPED_TRACE(write_req.DebugString());
    ASSERT_OK(proxy->Write(write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  // Get CDC changes.
  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  change_req.set_tablet_id(tablet_id);
  change_req.set_stream_id(stream_id);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(0);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(0);

  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());
    ASSERT_EQ(change_resp.records_size(), 2);
  }

  // Call GetChanges again and pass in checkpoint that producer can mark as committed.
  change_req.mutable_from_checkpoint()->CopyFrom(change_resp.checkpoint());
  change_resp.Clear();
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());
    // No more changes, so 0 records should be received.
    ASSERT_EQ(change_resp.records_size(), 0);
  }

  // Verify that cdc_state table has correct checkpoint.
  ASSERT_NO_FATALS(VerifyCdcState(client_.get()));

  // Call GetChanges again but without any from checkpoint.
  change_req.Clear();
  change_req.set_tablet_id(tablet_id);
  change_req.set_stream_id(stream_id);
  change_resp.Clear();
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());
    // Verify that producer uses the "from_checkpoint" from cdc_state table and does not send back
    // any records.
    ASSERT_EQ(change_resp.records_size(), 0);
  }

  // Verify that cdc_state table's checkpoint is unaffected.
  ASSERT_NO_FATALS(VerifyCdcState(client_.get()));
}

} // namespace cdc
} // namespace yb
