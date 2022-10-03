// Copyright (c) YugaByte, Inc.

#include <gflags/gflags_declare.h>

#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol-test-util.h"
#include "yb/common/ql_value.h"

#include "yb/consensus/log.h"
#include "yb/consensus/log_reader.h"

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/client/error.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"
#include "yb/client/client-test-util.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value_type.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/walltime.h"

#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/master_defaults.h"
#include "yb/master/mini_master.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/format.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/slice.h"
#include "yb/util/status_format.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"
#include "yb/yql/cql/ql/util/errcodes.h"
#include "yb/yql/cql/ql/util/statement_result.h"

DECLARE_bool(TEST_record_segments_violate_max_time_policy);
DECLARE_bool(TEST_record_segments_violate_min_space_policy);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(enable_log_retention_by_op_idx);
DECLARE_bool(enable_ysql);
DECLARE_double(leader_failure_max_missed_heartbeat_periods);
DECLARE_int32(cdc_min_replicated_index_considered_stale_secs);
DECLARE_int32(cdc_state_checkpoint_update_interval_ms);
DECLARE_int32(cdc_wal_retention_time_secs);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(follower_unavailable_considered_failed_sec);
DECLARE_int32(log_max_seconds_to_retain);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_int32(log_min_segments_to_retain);
DECLARE_int32(update_min_cdc_indices_interval_secs);
DECLARE_int64(TEST_simulate_free_space_bytes);
DECLARE_int64(log_stop_retaining_min_disk_mb);
DECLARE_uint64(log_segment_size_bytes);
DECLARE_int32(update_metrics_interval_ms);
DECLARE_bool(enable_collect_cdc_metrics);
DECLARE_bool(cdc_enable_replicate_intents);
DECLARE_bool(get_changes_honor_deadline);
DECLARE_int32(cdc_read_rpc_timeout_ms);
DECLARE_int32(TEST_get_changes_read_loop_delay_ms);
DECLARE_double(cdc_read_safe_deadline_ratio);
DECLARE_bool(TEST_xcluster_simulate_have_more_records);
DECLARE_bool(TEST_xcluster_skip_meta_ops);
DECLARE_bool(TEST_cdc_inject_replication_index_update_failure);

DECLARE_double(cdc_get_changes_free_rpc_ratio);
DECLARE_int32(rpc_workers_limit);
DECLARE_uint64(transaction_manager_workers_limit);

METRIC_DECLARE_entity(cdc);
METRIC_DECLARE_gauge_int64(last_read_opid_index);

namespace yb {

namespace log {
class LogReader;
}

namespace cdc {

using client::TableHandle;
using client::YBSessionPtr;
using master::MiniMaster;
using rpc::RpcController;

const std::string kCDCTestKeyspace = "my_keyspace";
const std::string kCDCTestTableName = "cdc_test_table";
const client::YBTableName kTableName(YQL_DATABASE_CQL, kCDCTestKeyspace, kCDCTestTableName);
const client::YBTableName kCdcStateTableName(
    YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);

CDCServiceImpl* CDCService(tserver::TabletServer* tserver) {
  return down_cast<CDCServiceImpl*>(
      tserver->rpc_server()->TEST_service_pool("yb.cdc.CDCService")->TEST_get_service().get());
}

class CDCServiceTest : public YBMiniClusterTestBase<MiniCluster>,
                       public testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    YBMiniClusterTestBase::SetUp();

    MiniClusterOptions opts;
    SetAtomicFlag(false, &FLAGS_enable_ysql);
    SetAtomicFlag(GetParam(), &FLAGS_cdc_enable_replicate_intents);
    SetAtomicFlag(1000, &FLAGS_update_metrics_interval_ms);
    opts.num_tablet_servers = server_count();
    opts.num_masters = 1;
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    client_ = ASSERT_RESULT(cluster_->CreateClient());
    cdc_proxy_ = std::make_unique<CDCServiceProxy>(
        &client_->proxy_cache(),
        HostPort::FromBoundEndpoint(cluster_->mini_tablet_server(0)->bound_rpc_addr()));

    CreateTable(tablet_count(), &table_);
  }

  void DoTearDown() override {
    if (!(stream_id_.empty())) {
      ASSERT_OK(client_->DeleteCDCStream(stream_id_,
                                         false /*force_delete*/,
                                         true /*ignore_errors*/));
      // wait for stream to completely finish deleting
    }
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
  void GetTablets(std::vector<TabletId>* tablet_ids,
                  const client::YBTableName& table_name = kTableName);
  std::string GetTablet(const client::YBTableName& table_name = kTableName);
  void GetChanges(const TabletId& tablet_id, const CDCStreamId& stream_id,
                  int64_t term, int64_t index,
                  bool* has_error = nullptr, ::yb::cdc::CDCErrorPB_Code* code = nullptr);
  void WriteTestRow(int32_t key, int32_t int_val, const string& string_val,
      const TabletId& tablet_id, const std::shared_ptr<tserver::TabletServerServiceProxy>& proxy);
  Status WriteToProxyWithRetries(
      const std::shared_ptr<tserver::TabletServerServiceProxy>& proxy,
      const tserver::WriteRequestPB& req, tserver::WriteResponsePB* resp, RpcController* rpc);
  Status GetChangesWithRetries(
      const GetChangesRequestPB& change_req, GetChangesResponsePB* change_resp,
      int timeout_ms, int max_attempts = 3);
  Status GetChangesInitialSchema(GetChangesRequestPB const& change_req,
                                 CDCCheckpointPB* mutable_checkpoint);
  tserver::MiniTabletServer* GetLeaderForTablet(const std::string& tablet_id);
  virtual int server_count() { return 1; }
  virtual int tablet_count() { return 1; }

  std::unique_ptr<CDCServiceProxy> cdc_proxy_;
  std::unique_ptr<client::YBClient> client_;
  CDCStreamId stream_id_;
  TableHandle table_;
};

INSTANTIATE_TEST_CASE_P(EnableReplicateIntents, CDCServiceTest, ::testing::Bool());

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

void VerifyCdcStateNotEmpty(client::YBClient* client) {
  client::TableHandle table;
  client::YBTableName cdc_state_table(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
  ASSERT_OK(table.Open(cdc_state_table, client));
  ASSERT_EQ(1, boost::size(client::TableRange(table)));
  const auto& row = client::TableRange(table).begin();
  string checkpoint = row->column(master::kCdcCheckpointIdx).string_value();
  auto result = OpId::FromString(checkpoint);
  ASSERT_OK(result);
  OpId op_id = *result;
  // Verify that op id index has been advanced and is not 0.
  ASSERT_GT(op_id.index, 0);
}

void VerifyCdcStateMatches(client::YBClient* client,
                           const CDCStreamId& stream_id,
                           const TabletId& tablet_id,
                           uint64_t term,
                           uint64_t index)  {
  client::TableHandle table;
  client::YBTableName cdc_state_table(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
  ASSERT_OK(table.Open(cdc_state_table, client));
  const auto op = table.NewReadOp();
  auto* const req = op->mutable_request();
  QLAddStringHashValue(req, tablet_id);
  auto cond = req->mutable_where_expr()->mutable_condition();
  cond->set_op(QLOperator::QL_OP_AND);
  QLAddStringCondition(cond, Schema::first_column_id() + master::kCdcStreamIdIdx, QL_OP_EQUAL,
      stream_id);
  table.AddColumns({master::kCdcCheckpoint}, req);

  auto session = client->NewSession();
  ASSERT_OK(session->TEST_ApplyAndFlush(op));

  LOG(INFO) << strings::Substitute("Verifying tablet: $0, stream: $1, op_id: $2",
      tablet_id, stream_id, OpId(term, index).ToString());

  auto row_block = ql::RowsResult(op.get()).GetRowBlock();
  ASSERT_EQ(row_block->row_count(), 1);

  string checkpoint = row_block->row(0).column(0).string_value();
  auto result = OpId::FromString(checkpoint);
  ASSERT_OK(result);
  OpId op_id = *result;

  ASSERT_EQ(op_id.term, term);
  ASSERT_EQ(op_id.index, index);
}

void VerifyStreamDeletedFromCdcState(client::YBClient* client,
                                     const CDCStreamId& stream_id,
                                     const TabletId& tablet_id,
                                     int timeout_secs = 10) {
  client::TableHandle table;
  const client::YBTableName cdc_state_table(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
  ASSERT_OK(table.Open(cdc_state_table, client));

  const auto op = table.NewReadOp();
  auto* const req = op->mutable_request();
  QLAddStringHashValue(req, tablet_id);

  auto cond = req->mutable_where_expr()->mutable_condition();
  cond->set_op(QLOperator::QL_OP_AND);
  QLAddStringCondition(cond, Schema::first_column_id() + master::kCdcStreamIdIdx, QL_OP_EQUAL,
      stream_id);

  table.AddColumns({master::kCdcCheckpoint}, req);
  auto session = client->NewSession();

  // The deletion of cdc_state rows for the specified stream happen in an asynchronous thread,
  // so even if the request has returned, it doesn't mean that the rows have been deleted yet.
  ASSERT_OK(WaitFor([&](){
    EXPECT_OK(session->TEST_ApplyAndFlush(op));
    auto row_block = ql::RowsResult(op.get()).GetRowBlock();
    if (row_block->row_count() == 0) {
      return true;
    }
    return false;
  }, MonoDelta::FromSeconds(timeout_secs) * kTimeMultiplier,
      "Stream rows in cdc_state have been deleted."));
}

void CDCServiceTest::GetTablets(std::vector<TabletId>* tablet_ids,
                                const client::YBTableName& table_name) {
  std::vector<std::string> ranges;
  ASSERT_OK(client_->GetTablets(
      table_name, 0 /* max_tablets */, tablet_ids, &ranges, nullptr /* locations */,
      RequireTabletsRunning::kFalse, master::IncludeInactive::kTrue));
  ASSERT_EQ(tablet_ids->size(), tablet_count());
}

std::string CDCServiceTest::GetTablet(const client::YBTableName& table_name) {
  std::vector<TabletId> tablet_ids;
  GetTablets(&tablet_ids, table_name);
  return tablet_ids[0];
}

void CDCServiceTest::GetChanges(const TabletId& tablet_id, const CDCStreamId& stream_id,
                                int64_t term, int64_t index,
                                bool* has_error, ::yb::cdc::CDCErrorPB_Code* code) {
  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  change_req.set_tablet_id(tablet_id);
  change_req.set_stream_id(stream_id);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(term);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(index);
  change_req.set_serve_as_proxy(true);

  {
    RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(10.0) * kTimeMultiplier);
    SCOPED_TRACE(change_req.DebugString());
    auto s = cdc_proxy_->GetChanges(change_req, &change_resp, &rpc);
    if (!has_error) {
      ASSERT_OK(s);
      ASSERT_FALSE(change_resp.has_error());
    } else if (!s.ok() || change_resp.has_error()) {
      *has_error = true;
      if (code && change_resp.error().has_code()) {
        *code = change_resp.error().code();
      }
      return;
    }
  }
}

void CDCServiceTest::WriteTestRow(int32_t key,
                                  int32_t int_val,
                                  const string& string_val,
                                  const TabletId& tablet_id,
                                  const std::shared_ptr<tserver::TabletServerServiceProxy>& proxy) {
  tserver::WriteRequestPB write_req;
  tserver::WriteResponsePB write_resp;
  write_req.set_tablet_id(tablet_id);

  RpcController rpc;
  AddTestRowInsert(key, int_val, string_val, &write_req);
  SCOPED_TRACE(write_req.DebugString());
  ASSERT_OK(WriteToProxyWithRetries(proxy, write_req, &write_resp, &rpc));
  SCOPED_TRACE(write_resp.DebugString());
  ASSERT_FALSE(write_resp.has_error());
}

Status CDCServiceTest::WriteToProxyWithRetries(
    const std::shared_ptr<tserver::TabletServerServiceProxy>& proxy,
    const tserver::WriteRequestPB& req,
    tserver::WriteResponsePB* resp,
    RpcController* rpc) {
  return LoggedWaitFor(
      [&req, resp, rpc, proxy]() -> Result<bool> {
        auto s = proxy->Write(req, resp, rpc);
        if (s.IsTryAgain() ||
            (resp->has_error() && StatusFromPB(resp->error().status()).IsTryAgain())) {
          rpc->Reset();
          return false;
        }
        RETURN_NOT_OK(s);
        return true;
      },
      MonoDelta::FromSeconds(10) * kTimeMultiplier, "Write test row");
}

Status CDCServiceTest::GetChangesWithRetries(
  const GetChangesRequestPB& req,
  GetChangesResponsePB* resp,
  int timeout_ms,
  int max_attempts) {
  // Keep track of number of attempts.
  int num_attempts = 0;

  auto return_status = LoggedWaitFor(
    [&]() -> Result<bool> {
      // RpcController needs to be rebuilt every time since the deadline is constant.
      RpcController rpc;
      rpc.set_timeout(MonoDelta::FromMilliseconds(timeout_ms));

      // Update return_status to be the status from the latest attempt.
      auto s = cdc_proxy_->GetChanges(req, resp, &rpc);
      ++num_attempts;

      // Exit if we exhausted the number of attempts.
      if (num_attempts >= max_attempts) {
        return s.ok() ? Status::OK() : STATUS_FORMAT(
          TimedOut, "Tried calling GetChanges $0 times with no success.", num_attempts);
      }

      // Try again if we still have attempts left.
      if (!s.ok()) {
        return false;
      }

      return true;
    },
    MonoDelta::FromSeconds(5) * max_attempts * kTimeMultiplier, "Call GetChanges");

  return return_status;
}

Status CDCServiceTest::GetChangesInitialSchema(GetChangesRequestPB const& req_in,
                                               CDCCheckpointPB* mutable_checkpoint) {
  GetChangesRequestPB change_req(req_in);
  GetChangesResponsePB change_resp;
  change_req.set_max_records(1);

  // Consume the META_OP that has the original Schema.
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    RETURN_NOT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    SCHECK(!change_resp.has_error(), IllegalState,
           Format("Response Error: $0", change_resp.error().DebugString()));
    SCHECK_EQ(change_resp.records_size(), 1, IllegalState, "Expected only 1 record");
    SCHECK_EQ(change_resp.records(0).operation(), CDCRecordPB::CHANGE_METADATA,
              IllegalState, "Expected the CHANGE_METADATA related to the initial schema");
    mutable_checkpoint->CopyFrom(change_resp.checkpoint());
  }
  return Status::OK();
}

tserver::MiniTabletServer* CDCServiceTest::GetLeaderForTablet(const std::string& tablet_id) {
  return ::yb::GetLeaderForTablet(cluster_.get(), tablet_id);
}

TEST_P(CDCServiceTest, TestCompoundKey) {
  // Create a table with a compound primary key.
  static const std::string kCDCTestTableCompoundKeyName = "cdc_test_table_compound_key";
  static const client::YBTableName kTableNameCompoundKey(
    YQL_DATABASE_CQL, kCDCTestKeyspace, kCDCTestTableCompoundKeyName);

  client::YBSchemaBuilder builder;
  builder.AddColumn("hash_key")->Type(STRING)->HashPrimaryKey()->NotNull();
  builder.AddColumn("range_key")->Type(STRING)->PrimaryKey()->NotNull();
  builder.AddColumn("val")->Type(INT32);

  TableProperties table_properties;
  table_properties.SetTransactional(true);
  builder.SetTableProperties(table_properties);

  TableHandle table;
  ASSERT_OK(table.Create(kTableNameCompoundKey, tablet_count(), client_.get(), &builder));

  // Create a stream on the table
  CreateCDCStream(cdc_proxy_, table.table()->id(), &stream_id_);

  std::string tablet_id = GetTablet(table.name());

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;
  change_req.set_tablet_id(tablet_id);
  change_req.set_stream_id(stream_id_);

  // Consume the META_OP that has the initial table Schema.
  ASSERT_OK(GetChangesInitialSchema(change_req, change_req.mutable_from_checkpoint()));

  // Now apply two ops with same hash key but different range key in a batch.
  auto session = client_->NewSession();
  for (int i = 0; i < 2; i++) {
    const auto op = table.NewUpdateOp();
    auto* const req = op->mutable_request();
    QLAddStringHashValue(req, "hk");
    QLAddStringRangeValue(req, Format("rk_$0", i));
    table.AddInt32ColumnValue(req, "val", i);
    session->Apply(op);
  }
  ASSERT_OK(session->TEST_Flush());

  // Get CDC changes.
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    ASSERT_FALSE(change_resp.has_error());
    ASSERT_EQ(change_resp.records_size(), 2);
  }

  // Verify the results.
  for (int i = 0; i < change_resp.records_size(); i++) {
    ASSERT_EQ(change_resp.records(i).operation(), CDCRecordPB::WRITE);

    ASSERT_EQ(change_resp.records(i).key_size(), 2);
    // Check the key.
    ASSERT_EQ(change_resp.records(i).key(0).value().string_value(), "hk");
    ASSERT_EQ(change_resp.records(i).key(1).value().string_value(), Format("rk_$0", i));

    ASSERT_EQ(change_resp.records(i).changes_size(), 1);
    ASSERT_EQ(change_resp.records(i).changes(0).value().int32_value(), i);
  }
}

TEST_P(CDCServiceTest, TestCreateCDCStream) {
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);

  NamespaceId ns_id;
  std::vector<TableId> table_ids;
  std::unordered_map<std::string, std::string> options;
  ASSERT_OK(client_->GetCDCStream(stream_id_, &ns_id, &table_ids, &options));
  ASSERT_EQ(table_ids.front(), table_.table()->id());
}

TEST_P(CDCServiceTest, TestCreateCDCStreamWithDefaultRententionTime) {
  // Set default WAL retention time to 10 hours.
  FLAGS_cdc_wal_retention_time_secs = 36000;

  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);

  NamespaceId ns_id;
  std::vector<TableId> table_ids;
  std::unordered_map<std::string, std::string> options;
  ASSERT_OK(client_->GetCDCStream(stream_id_, &ns_id, &table_ids, &options));


  // Verify that the wal retention time was set at the tablet level.
  VerifyWalRetentionTime(cluster_.get(), kCDCTestTableName, FLAGS_cdc_wal_retention_time_secs);
}

TEST_P(CDCServiceTest, TestDeleteCDCStream) {
  FLAGS_cdc_state_checkpoint_update_interval_ms = 0;
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);

  NamespaceId ns_id;
  std::vector<TableId> table_ids;
  std::unordered_map<std::string, std::string> options;
  ASSERT_OK(client_->GetCDCStream(stream_id_, &ns_id, &table_ids, &options));
  ASSERT_EQ(table_ids.front(), table_.table()->id());


  std::vector<std::string> tablet_ids;
  std::vector<std::string> ranges;
  ASSERT_OK(client_->GetTablets(table_.table()->name(), 0 /* max_tablets */, &tablet_ids, &ranges));

  for (const auto& tablet_id : tablet_ids) {
    VerifyCdcStateMatches(client_.get(), stream_id_, tablet_id, 0, 0);
  }

  {
    const auto& tserver = cluster_->mini_tablet_server(0)->server();
    std::string tablet_id = GetTablet(table_.name());
    ASSERT_NO_FATALS(WriteTestRow(0, 10, "key0", tablet_id, tserver->proxy()));
  }

  ASSERT_OK(client_->DeleteCDCStream(stream_id_));

  // Check that the stream still no longer exists.
  ns_id.clear();
  table_ids.clear();
  options.clear();
  Status s = client_->GetCDCStream(stream_id_, &ns_id, &table_ids, &options);
  ASSERT_TRUE(s.IsNotFound());

  for (const auto& tablet_id : tablet_ids) {
    VerifyStreamDeletedFromCdcState(client_.get(), stream_id_, tablet_id);
  }

  // Once the CatalogManager has cleaned up cdc_state, make sure a subsequent call to GetChanges
  // doesn't re-populate cdc_state with cleaned up entries. UpdateCheckpoint will be called since
  // this is the first time we're calling GetChanges.
  bool get_changes_error = false;
  for (const auto& tablet_id : tablet_ids) {
    GetChanges(tablet_id, stream_id_, 0, 0, &get_changes_error);
    ASSERT_FALSE(get_changes_error);
  }

  for (const auto& tablet_id : tablet_ids) {
    VerifyStreamDeletedFromCdcState(client_.get(), stream_id_, tablet_id);
  }
}

TEST_P(CDCServiceTest, TestSafeTime) {
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_collect_cdc_metrics) = true;

  std::string tablet_id = GetTablet();
  auto leader_tserver = GetLeaderForTablet(tablet_id)->server();
  auto tablet_peer = ASSERT_RESULT(leader_tserver->tablet_manager()->GetServingTablet(tablet_id));

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;
  change_req.set_tablet_id(tablet_id);
  change_req.set_stream_id(stream_id_);

  // Consume the META_OP that has the original Schema.
  ASSERT_OK(GetChangesInitialSchema(change_req, change_req.mutable_from_checkpoint()));

  auto ht_0 = ASSERT_RESULT(tablet_peer->LeaderSafeTime()).ToUint64();
  ASSERT_NO_FATALS(WriteTestRow(0, 10, "key0", tablet_id, leader_tserver->proxy()));
  ASSERT_NO_FATALS(WriteTestRow(1, 11, "key0", tablet_id, leader_tserver->proxy()));
  auto ht_1 = ASSERT_RESULT(tablet_peer->LeaderSafeTime()).ToUint64();


  // Get CDC changes.
  FLAGS_TEST_xcluster_simulate_have_more_records = true;
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    ASSERT_FALSE(change_resp.has_error());
    ASSERT_TRUE(change_resp.has_safe_hybrid_time());
    uint64_t safe_hybrid_time = change_resp.safe_hybrid_time();
    ASSERT_TRUE(ht_0 < safe_hybrid_time && safe_hybrid_time < ht_1);
  }

  FLAGS_TEST_xcluster_simulate_have_more_records = false;
  {
    auto pre_get_changes_time = ASSERT_RESULT(tablet_peer->LeaderSafeTime()).ToUint64();;
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
    ASSERT_FALSE(change_resp.has_error());
    ASSERT_TRUE(change_resp.has_safe_hybrid_time());
    uint64_t safe_hybrid_time = change_resp.safe_hybrid_time();
    auto post_get_changes_time = ASSERT_RESULT(tablet_peer->LeaderSafeTime()).ToUint64();
    ASSERT_TRUE(pre_get_changes_time <= safe_hybrid_time &&
                safe_hybrid_time <= post_get_changes_time);
  }

  ASSERT_OK(client_->DeleteCDCStream(stream_id_));
}

TEST_P(CDCServiceTest, TestMetricsOnDeletedReplication) {
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_collect_cdc_metrics) = true;

  std::string tablet_id = GetTablet();

  const auto& tserver = cluster_->mini_tablet_server(0)->server();
  // Use proxy for to most accurately simulate normal requests.
  const auto& proxy = tserver->proxy();

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;
  change_req.set_tablet_id(tablet_id);
  change_req.set_stream_id(stream_id_);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(0);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(0);
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
  }

  // Insert test rows, one at a time so they have different hybrid times.
  tserver::WriteRequestPB write_req;
  tserver::WriteResponsePB write_resp;
  write_req.set_tablet_id(tablet_id);
  {
    RpcController rpc;
    AddTestRowInsert(1, 11, "key1", &write_req);
    AddTestRowInsert(2, 22, "key2", &write_req);
    SCOPED_TRACE(write_req.DebugString());
    ASSERT_OK(WriteToProxyWithRetries(proxy, write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  auto cdc_service = CDCService(tserver);
  // Assert that leader lag > 0.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto metrics = cdc_service->GetCDCTabletMetrics({"" /* UUID */, stream_id_, tablet_id});
    return metrics->async_replication_sent_lag_micros->value() > 0 &&
        metrics->async_replication_committed_lag_micros->value() > 0;
  }, MonoDelta::FromSeconds(10) * kTimeMultiplier, "Wait for Lag > 0"));

  // Now, delete the replication stream and assert that lag is 0.
  ASSERT_OK(client_->DeleteCDCStream(stream_id_));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto metrics = cdc_service->GetCDCTabletMetrics({"" /* UUID */, stream_id_, tablet_id});
    return metrics->async_replication_sent_lag_micros->value() == 0 &&
        metrics->async_replication_committed_lag_micros->value() == 0;
  }, MonoDelta::FromSeconds(10) * kTimeMultiplier, "Wait for Lag = 0"));

  // Now check that UpdateLagMetrics deletes the metric.
  cdc_service->UpdateLagMetrics();
  auto metrics = cdc_service->GetCDCTabletMetrics(
      {"" /* UUID */, stream_id_, tablet_id},
      /* tablet_peer */ nullptr,
      CreateCDCMetricsEntity::kFalse);
  ASSERT_EQ(metrics, nullptr);
}


TEST_P(CDCServiceTest, TestGetChanges) {
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_collect_cdc_metrics) = true;

  std::string tablet_id = GetTablet();

  const auto& tserver = cluster_->mini_tablet_server(0)->server();
  // Use proxy for to most accurately simulate normal requests.
  const auto& proxy = tserver->proxy();

  // Insert test rows, one at a time so they have different hybrid times.
  tserver::WriteRequestPB write_req;
  tserver::WriteResponsePB write_resp;
  write_req.set_tablet_id(tablet_id);
  {
    RpcController rpc;
    AddTestRowInsert(1, 11, "key1", &write_req);
    AddTestRowInsert(2, 22, "key2", &write_req);
    SCOPED_TRACE(write_req.DebugString());
    ASSERT_OK(WriteToProxyWithRetries(proxy, write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  // Get CDC changes.
  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  change_req.set_tablet_id(tablet_id);
  change_req.set_stream_id(stream_id_);

  // Consume the META_OP that has the initial table Schema.
  ASSERT_OK(GetChangesInitialSchema(change_req, change_req.mutable_from_checkpoint()));

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
    auto cdc_service = CDCService(tserver);
    auto metrics = cdc_service->GetCDCTabletMetrics({"" /* UUID */, stream_id_, tablet_id});
    ASSERT_EQ(metrics->last_read_opid_index->value(), metrics->last_readable_opid_index->value());
    ASSERT_EQ(metrics->last_read_opid_index->value(), change_resp.records_size() + 1 /* checkpt */);
    ASSERT_EQ(metrics->rpc_payload_bytes_responded->TotalCount(), 2);
  }

  // Insert another row.
  {
    write_req.Clear();
    write_req.set_tablet_id(tablet_id);
    AddTestRowInsert(3, 33, "key3", &write_req);

    RpcController rpc;
    SCOPED_TRACE(write_req.DebugString());
    ASSERT_OK(WriteToProxyWithRetries(proxy, write_req, &write_resp, &rpc));
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
    ASSERT_OK(WriteToProxyWithRetries(proxy, write_req, &write_resp, &rpc));
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

  // Cleanup stream before shutdown.
  ASSERT_OK(client_->DeleteCDCStream(stream_id_));
  VerifyStreamDeletedFromCdcState(client_.get(), stream_id_, tablet_id);
}

TEST_P(CDCServiceTest, TestGetChangesWithDeadline) {
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);
  FLAGS_log_segment_size_bytes = 100;
  FLAGS_get_changes_honor_deadline = true;
  FLAGS_cdc_read_safe_deadline_ratio = 0.30;

  // Skip the META_OP that has the initial table Schema.  This method avoids LogCache.
  FLAGS_TEST_xcluster_skip_meta_ops = true;

  std::string tablet_id = GetTablet();

  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  change_req.set_tablet_id(tablet_id);
  change_req.set_stream_id(stream_id_);

  const auto& tserver = cluster_->mini_tablet_server(0)->server();
  // Use proxy for to most accurately simulate normal requests.
  const auto& proxy = tserver->proxy();

  // Insert <num_records> test rows.
  const int num_records = 500;
  for (int i = 0; i < num_records; i++) {
    WriteTestRow(i, i, Format("key$0", i), tablet_id, proxy);
  }

  change_req.set_max_records(num_records);

  SCOPED_TRACE(change_req.DebugString());

  {
    // Get CDC changes. Note that the timeout value and read delay
    // should ensure that some, but not all records are read.
    FLAGS_TEST_get_changes_read_loop_delay_ms = 10 * kTimeMultiplier;
    FLAGS_cdc_read_rpc_timeout_ms = 50 * kTimeMultiplier;

    ASSERT_OK(GetChangesWithRetries(change_req, &change_resp,
        FLAGS_cdc_read_rpc_timeout_ms));

    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    // Ensure that partial results are returned
    ASSERT_GT(change_resp.records_size(), 0);
    ASSERT_LT(change_resp.records_size(), num_records);
  }

  {
    // Try again, but use a timeout value large enough such that
    // all records should be read before timeout.
    FLAGS_TEST_get_changes_read_loop_delay_ms = 0;
    FLAGS_cdc_read_rpc_timeout_ms = 30 * 1000 * kTimeMultiplier;

    ASSERT_OK(GetChangesWithRetries(change_req, &change_resp,
                                    FLAGS_cdc_read_rpc_timeout_ms));
    SCOPED_TRACE(change_resp.DebugString());
    ASSERT_FALSE(change_resp.has_error());

    // This time, we expect all records to be read.
    ASSERT_EQ(change_resp.records_size(), num_records);
  }

  // Cleanup stream before shutdown.
  ASSERT_OK(client_->DeleteCDCStream(stream_id_));
  VerifyStreamDeletedFromCdcState(client_.get(), stream_id_, tablet_id);
}

TEST_P(CDCServiceTest, TestGetChangesInvalidStream) {
  std::string tablet_id = GetTablet();

  // Get CDC changes for non-existent stream.
  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  change_req.set_tablet_id(tablet_id);
  change_req.set_stream_id("InvalidStreamId");
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(0);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(0);

  RpcController rpc;
  ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
  ASSERT_TRUE(change_resp.has_error());
}

TEST_P(CDCServiceTest, TestGetCheckpoint) {
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);

  std::string tablet_id = GetTablet();

  GetCheckpointRequestPB req;
  GetCheckpointResponsePB resp;

  req.set_tablet_id(tablet_id);
  req.set_stream_id(stream_id_);

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

class CDCServiceTestMultipleServersOneTablet : public CDCServiceTest {
  virtual int server_count() override { return 3; }
  virtual int tablet_count() override { return 1; }
};

INSTANTIATE_TEST_CASE_P(EnableReplicateIntents, CDCServiceTestMultipleServersOneTablet,
                        ::testing::Bool());

TEST_P(CDCServiceTestMultipleServersOneTablet, TestMetricsAfterServerFailure) {
  // Test that the metric value is not time since epoch after a leadership change.
  SetAtomicFlag(0, &FLAGS_cdc_state_checkpoint_update_interval_ms);
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_collect_cdc_metrics) = false;

  std::string tablet_id = GetTablet();

  GetChangesRequestPB change_req;
  change_req.set_tablet_id(tablet_id);
  change_req.set_stream_id(stream_id_);
  ASSERT_OK(GetChangesInitialSchema(change_req, change_req.mutable_from_checkpoint()));
  auto term = change_req.from_checkpoint().op_id().term();
  auto index = change_req.from_checkpoint().op_id().index();

  tserver::MiniTabletServer* leader_mini_tserver;
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    leader_mini_tserver = GetLeaderForTablet(tablet_id);
    return leader_mini_tserver != nullptr;
  }, MonoDelta::FromSeconds(30) * kTimeMultiplier, "Wait for tablet to have a leader."));
  auto timestamp_before_write = GetCurrentTimeMicros();
  ASSERT_NO_FATALS(WriteTestRow(0, 10, "key0", tablet_id, leader_mini_tserver->server()->proxy()));
  ASSERT_NO_FATALS(GetChanges(tablet_id, stream_id_, term, index));
  ASSERT_OK(leader_mini_tserver->Restart());
  leader_mini_tserver = nullptr;
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    leader_mini_tserver = GetLeaderForTablet(tablet_id);
    return leader_mini_tserver != nullptr;
  }, MonoDelta::FromSeconds(30) * kTimeMultiplier, "Wait for tablet to have a leader."));

  auto leader_tserver = leader_mini_tserver->server();
  auto leader_proxy = std::make_unique<CDCServiceProxy>(
      &client_->proxy_cache(),
      HostPort::FromBoundEndpoint(leader_mini_tserver->bound_rpc_addr()));
  auto cdc_service = CDCService(leader_tserver);
  cdc_service->UpdateLagMetrics();
  auto metrics = cdc_service->GetCDCTabletMetrics({"" /* UUID */, stream_id_, tablet_id});
  auto timestamp_after_write = GetCurrentTimeMicros();
  auto lag_after_write = metrics->async_replication_committed_lag_micros->value();
  ASSERT_GE(lag_after_write, 0);
  ASSERT_LE(lag_after_write, timestamp_after_write - timestamp_before_write);
}

TEST_P(CDCServiceTestMultipleServersOneTablet, TestUpdateLagMetrics) {
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_collect_cdc_metrics) = true;

  std::string tablet_id = GetTablet();

  // Get the leader and a follower for the tablet.
  tserver::MiniTabletServer* leader_mini_tserver = nullptr;
  tserver::MiniTabletServer* follower_mini_tserver = nullptr;

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    leader_mini_tserver = nullptr;
    follower_mini_tserver = nullptr;
    for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
      auto tablet_peer_result =
          cluster_->mini_tablet_server(i)->server()->tablet_manager()->GetServingTablet(tablet_id);
      if (!tablet_peer_result.ok()) {
        continue;
      }
      if ((**tablet_peer_result).LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY) {
        leader_mini_tserver = cluster_->mini_tablet_server(i);
      } else {
        follower_mini_tserver = cluster_->mini_tablet_server(i);
      }
    }
    return leader_mini_tserver != nullptr && follower_mini_tserver != nullptr;
  }, MonoDelta::FromSeconds(30) * kTimeMultiplier, "Wait for tablet to have a leader."));

  auto leader_proxy = std::make_unique<CDCServiceProxy>(
      &client_->proxy_cache(),
      HostPort::FromBoundEndpoint(leader_mini_tserver->bound_rpc_addr()));

  auto follower_proxy = std::make_unique<CDCServiceProxy>(
      &client_->proxy_cache(),
      HostPort::FromBoundEndpoint(follower_mini_tserver->bound_rpc_addr()));

  auto leader_tserver = leader_mini_tserver->server();
  auto follower_tserver = follower_mini_tserver->server();
  // Use proxy for to most accurately simulate normal requests.
  const auto& proxy = leader_tserver->proxy();

  auto cdc_service = CDCService(leader_tserver);
  auto cdc_service_follower = CDCService(follower_tserver);

  // At the start of time, assert both leader and follower at 0 lag.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    {
      // Leader metrics
      auto metrics = cdc_service->GetCDCTabletMetrics({"" /* UUID */, stream_id_, tablet_id});
      if (!(metrics->async_replication_sent_lag_micros->value() == 0 &&
          metrics->async_replication_committed_lag_micros->value() == 0)) {
        return false;
      }
    }
    {
      // Follower metrics
      auto follower_metrics =
          cdc_service_follower->GetCDCTabletMetrics({"" /* UUID */, stream_id_, tablet_id});
      return follower_metrics->async_replication_sent_lag_micros->value() == 0 &&
          follower_metrics->async_replication_committed_lag_micros->value() == 0;
    }
  }, MonoDelta::FromSeconds(10) * kTimeMultiplier, "At start, wait for Lag = 0"));


  // Create the in-memory structures for both follower and leader by polling for the tablet.
  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;
  change_req.set_tablet_id(tablet_id);
  change_req.set_stream_id(stream_id_);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_index(0);
  change_req.mutable_from_checkpoint()->mutable_op_id()->set_term(0);
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(leader_proxy->GetChanges(change_req, &change_resp, &rpc));
    change_resp.Clear();
    rpc.Reset();
    ASSERT_OK(follower_proxy->GetChanges(change_req, &change_resp, &rpc));
  }

  // Insert test rows, one at a time so they have different hybrid times.
  tserver::WriteRequestPB write_req;
  tserver::WriteResponsePB write_resp;
  write_req.set_tablet_id(tablet_id);
  {
    RpcController rpc;
    AddTestRowInsert(1, 11, "key1", &write_req);
    SCOPED_TRACE(write_req.DebugString());
    ASSERT_OK(WriteToProxyWithRetries(proxy, write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  {
    write_req.Clear();
    write_req.set_tablet_id(tablet_id);
    RpcController rpc;
    AddTestRowInsert(2, 22, "key2", &write_req);
    SCOPED_TRACE(write_req.DebugString());
    ASSERT_OK(WriteToProxyWithRetries(proxy, write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  // Assert that leader lag > 0.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto metrics = cdc_service->GetCDCTabletMetrics({"" /* UUID */, stream_id_, tablet_id});
    return metrics->async_replication_sent_lag_micros->value() > 0 &&
        metrics->async_replication_committed_lag_micros->value() > 0;
  }, MonoDelta::FromSeconds(10) * kTimeMultiplier, "Wait for Lag > 0"));

  {
    // Make sure we wait for follower update thread to run at least once.
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_update_metrics_interval_ms));
    // On the follower, we shouldn't create metrics for tablets that we're not leader for, so these
    // should be 0 even if there are un-polled for records.
    auto metrics_follower = cdc_service_follower->
        GetCDCTabletMetrics({"" /* UUID */, stream_id_, tablet_id});
    ASSERT_TRUE(metrics_follower->async_replication_sent_lag_micros->value() == 0 &&
                metrics_follower->async_replication_committed_lag_micros->value() == 0);
  }

  change_req.mutable_from_checkpoint()->CopyFrom(change_resp.checkpoint());
  change_resp.Clear();
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(leader_proxy->GetChanges(change_req, &change_resp, &rpc));
  }

  // When we GetChanges the first time, only the read lag metric should be 0.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto metrics = cdc_service->GetCDCTabletMetrics({"" /* UUID */, stream_id_, tablet_id});
    return metrics->async_replication_sent_lag_micros->value() == 0 &&
        metrics->async_replication_committed_lag_micros->value() > 0;
  }, MonoDelta::FromSeconds(10) * kTimeMultiplier, "Wait for Read Lag = 0"));

  change_req.mutable_from_checkpoint()->CopyFrom(change_resp.checkpoint());
  change_resp.Clear();
  {
    RpcController rpc;
    SCOPED_TRACE(change_req.DebugString());
    ASSERT_OK(leader_proxy->GetChanges(change_req, &change_resp, &rpc));
  }

  // When we GetChanges the second time, both the lag metrics should be 0.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto metrics = cdc_service->GetCDCTabletMetrics({"" /* UUID */, stream_id_, tablet_id});
    return metrics->async_replication_sent_lag_micros->value() == 0 &&
        metrics->async_replication_committed_lag_micros->value() == 0;
  }, MonoDelta::FromSeconds(10) * kTimeMultiplier, "Wait for All Lag = 0"));
}

class CDCServiceTestMultipleServers : public CDCServiceTest {
 public:
  virtual int server_count() override { return 2; }
  virtual int tablet_count() override { return 4; }
};

INSTANTIATE_TEST_CASE_P(EnableReplicateIntents, CDCServiceTestMultipleServers, ::testing::Bool());

TEST_P(CDCServiceTestMultipleServers, TestListTablets) {
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);

  std::string tablet_id = GetTablet();

  ListTabletsRequestPB req;
  ListTabletsResponsePB resp;

  req.set_stream_id(stream_id_);

  auto cdc_proxy_bcast_addr = cluster_->mini_tablet_server(0)->options()->broadcast_addresses[0];
  int cdc_proxy_count = 0;

  // Test a simple query for all tablets.
  {
    RpcController rpc;
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(cdc_proxy_->ListTablets(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());

    ASSERT_EQ(resp.tablets_size(), tablet_count());
    ASSERT_EQ(resp.tablets(0).tablet_id(), tablet_id);

    for (auto& tablet : resp.tablets()) {
      auto owner_tserver = HostPort::FromPB(tablet.tservers(0).broadcast_addresses(0));
      if (owner_tserver == cdc_proxy_bcast_addr) {
        ++cdc_proxy_count;
      }
    }
  }

  // Query for tablets only on the first server.  We should only get a subset.
  {
    req.set_local_only(true);
    RpcController rpc;
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(cdc_proxy_->ListTablets(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
    ASSERT_EQ(resp.tablets_size(), cdc_proxy_count);
  }
}

TEST_P(CDCServiceTestMultipleServers, TestGetChangesProxyRouting) {
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);

  // Figure out [1] all tablets and [2] which ones are local to the first server.
  std::vector<std::string> local_tablets, all_tablets;
  for (bool is_local : {true, false}) {
    RpcController rpc;
    ListTabletsRequestPB req;
    ListTabletsResponsePB resp;
    req.set_stream_id(stream_id_);
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
      ASSERT_OK(WriteToProxyWithRetries(proxy, write_req, &write_resp, &rpc));
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
        bool should_error = !(is_local || use_proxy);
        GetChangesRequestPB change_req;
        GetChangesResponsePB change_resp;
        change_req.set_tablet_id(tablet_id);
        change_req.set_stream_id(stream_id_);
        change_req.set_serve_as_proxy(use_proxy);
        if (!should_error) {
          // Consume the META_OP that has the initial table Schema.
          ASSERT_OK(GetChangesInitialSchema(change_req, change_req.mutable_from_checkpoint()));
        }
        RpcController rpc;
        SCOPED_TRACE(change_req.DebugString());
        ASSERT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &rpc));
        SCOPED_TRACE(change_resp.DebugString());
        ASSERT_EQ(change_resp.has_error(), should_error);
        if (!should_error) {
          ASSERT_EQ(to_write, change_resp.records_size());
        }
      }
    }
  }

  // Verify the CDC metrics match what we just did.
  const auto& tserver = cluster_->mini_tablet_server(0)->server();
  auto cdc_service = CDCService(tserver);
  auto server_metrics = cdc_service->GetCDCServerMetrics();
  ASSERT_EQ(server_metrics->cdc_rpc_proxy_count->value(), remote_tablets.size() * 2);
}

TEST_P(CDCServiceTest, TestOnlyGetLocalChanges) {
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);

  std::string tablet_id = GetTablet();

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
    ASSERT_OK(WriteToProxyWithRetries(proxy, write_req, &write_resp, &rpc));
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
    ASSERT_OK(WriteToProxyWithRetries(proxy, write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  auto CheckChangesAndTable = [&]() {
    // Get CDC changes.
    GetChangesRequestPB change_req;
    GetChangesResponsePB change_resp;

    change_req.set_tablet_id(tablet_id);
    change_req.set_stream_id(stream_id_);

    // Consume the META_OP that has the initial table Schema.
    ASSERT_OK(GetChangesInitialSchema(change_req, change_req.mutable_from_checkpoint()));

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
    auto tablet_peer = cluster_->mini_tablet_server(0)->server()->tablet_manager()->LookupTablet(
        tablet_id);
    if (!tablet_peer) {
      return false;
    }
    return tablet_peer->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY;
  }, MonoDelta::FromSeconds(30) * kTimeMultiplier, "Wait until tablet has a leader."));

  ASSERT_NO_FATALS(CheckChangesAndTable());

  // Cleanup stream before shutdown.
  ASSERT_OK(client_->DeleteCDCStream(stream_id_));
  VerifyStreamDeletedFromCdcState(client_.get(), stream_id_, tablet_id);
}

TEST_P(CDCServiceTest, TestCheckpointUpdatedForRemoteRows) {
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);

  std::string tablet_id = GetTablet();

  GetChangesRequestPB change_req;
  change_req.set_tablet_id(tablet_id);
  change_req.set_stream_id(stream_id_);

  // Consume the META_OP that has the initial table Schema.
  ASSERT_OK(GetChangesInitialSchema(change_req, change_req.mutable_from_checkpoint()));

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
    ASSERT_OK(WriteToProxyWithRetries(proxy, write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  auto CheckChanges = [&]() {
    // Get CDC changes.
    GetChangesResponsePB change_resp;
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

  // Cleanup stream before shutdown.
  ASSERT_OK(client_->DeleteCDCStream(stream_id_));
  VerifyStreamDeletedFromCdcState(client_.get(), stream_id_, tablet_id);
}

// Test to ensure that cdc_state table's checkpoint is updated as expected.
// This also tests for #2897 to ensure that cdc_state table checkpoint is not overwritten to 0.0
// in case the consumer does not send from checkpoint.
TEST_P(CDCServiceTest, TestCheckpointUpdate) {
  FLAGS_cdc_state_checkpoint_update_interval_ms = 0;

  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);

  std::string tablet_id = GetTablet();

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
    ASSERT_OK(WriteToProxyWithRetries(proxy, write_req, &write_resp, &rpc));
    SCOPED_TRACE(write_resp.DebugString());
    ASSERT_FALSE(write_resp.has_error());
  }

  // Get CDC changes.
  GetChangesRequestPB change_req;
  GetChangesResponsePB change_resp;

  change_req.set_tablet_id(tablet_id);
  change_req.set_stream_id(stream_id_);

  // Consume the META_OP that has the initial table Schema.
  ASSERT_OK(GetChangesInitialSchema(change_req, change_req.mutable_from_checkpoint()));

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
  ASSERT_NO_FATALS(VerifyCdcStateNotEmpty(client_.get()));

  // Call GetChanges again but without any from checkpoint.
  change_req.Clear();
  change_req.set_tablet_id(tablet_id);
  change_req.set_stream_id(stream_id_);
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
  ASSERT_NO_FATALS(VerifyCdcStateNotEmpty(client_.get()));

  // Cleanup stream before shutdown.
  ASSERT_OK(client_->DeleteCDCStream(stream_id_));
  VerifyStreamDeletedFromCdcState(client_.get(), stream_id_, tablet_id);
}

namespace {
void WaitForCDCIndex(const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                     int64_t expected_index,
                     int timeout_secs) {
  LOG(INFO) << "Waiting until index equals " << expected_index
            << ". Timeout: " << timeout_secs;
  ASSERT_OK(WaitFor([&](){
    if (tablet_peer->log_available() &&
        tablet_peer->log()->cdc_min_replicated_index() == expected_index &&
        tablet_peer->tablet_metadata()->cdc_min_replicated_index() == expected_index) {
      return true;
    }
    return false;
  }, MonoDelta::FromSeconds(timeout_secs) * kTimeMultiplier,
      "Wait until cdc min replicated index."));
  LOG(INFO) << "Done waiting";
}
} // namespace

class CDCServiceTestMaxRentionTime : public CDCServiceTest {
 public:
  void SetUp() override {
    // Immediately write any index provided by a GetChanges request to cdc_state table.
    FLAGS_cdc_state_checkpoint_update_interval_ms = 0;
    FLAGS_log_min_segments_to_retain = 1;
    FLAGS_log_min_seconds_to_retain = 1;
    FLAGS_cdc_wal_retention_time_secs = 1;
    FLAGS_enable_log_retention_by_op_idx = true;
    FLAGS_log_max_seconds_to_retain = kMaxSecondsToRetain;
    FLAGS_TEST_record_segments_violate_max_time_policy = true;
    FLAGS_update_min_cdc_indices_interval_secs = 1;

    // This will rollover log segments a lot faster.
    FLAGS_log_segment_size_bytes = 100;
    CDCServiceTest::SetUp();
  }
  const int kMaxSecondsToRetain = 30;
};

INSTANTIATE_TEST_CASE_P(EnableReplicateIntents, CDCServiceTestMaxRentionTime, ::testing::Bool());

TEST_P(CDCServiceTestMaxRentionTime, TestLogRetentionByOpId_MaxRentionTime) {
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);

  std::string tablet_id = GetTablet();

  const auto& proxy = cluster_->mini_tablet_server(0)->server()->proxy();

  auto tablet_peer = ASSERT_RESULT(
      cluster_->mini_tablet_server(0)->server()->tablet_manager()->GetTablet(tablet_id));

  // Write a row so that the next GetChanges request doesn't fail.
  WriteTestRow(0, 10, "key0", tablet_id, proxy);

  // Get CDC changes.
  GetChanges(tablet_id, stream_id_, /* term */ 0, /* index */ 0);

  WaitForCDCIndex(tablet_peer, 0, 4 * FLAGS_update_min_cdc_indices_interval_secs);

  MonoTime start = MonoTime::Now();
  // Write a lot more data to generate many log files that can be GCed. This should take less
  // than kMaxSecondsToRetain for the next check to succeed.
  for (int i = 1; i <= 100; i++) {
    WriteTestRow(i, 10 + i, "key" + std::to_string(i), tablet_id, proxy);
  }
  MonoDelta elapsed = MonoTime::Now().GetDeltaSince(start);
  ASSERT_LT(elapsed.ToSeconds(), kMaxSecondsToRetain);
  MonoDelta time_to_sleep = MonoDelta::FromSeconds(kMaxSecondsToRetain + 10) - elapsed;

  // Since we haven't updated the minimum cdc index, and the elapsed time is less than
  // kMaxSecondsToRetain, no log files should be returned.
  log::SegmentSequence segment_sequence;
  ASSERT_OK(tablet_peer->log()->GetSegmentsToGCUnlocked(std::numeric_limits<int64_t>::max(),
                                                        &segment_sequence));
  ASSERT_EQ(segment_sequence.size(), 0);
  LOG(INFO) << "No segments to be GCed because less than " << kMaxSecondsToRetain
            << " seconds have elapsed";

  SleepFor(time_to_sleep);

  ASSERT_OK(tablet_peer->log()->GetSegmentsToGCUnlocked(std::numeric_limits<int64_t>::max(),
                                                              &segment_sequence));
  ASSERT_GT(segment_sequence.size(), 0);
  const auto& segments_violate =
      *(tablet_peer->log()->reader_->TEST_segments_violate_max_time_policy_);
  ASSERT_EQ(segment_sequence.size(), segments_violate.size());
  auto it1 = segment_sequence.begin();
  auto it2 = segments_violate.begin();
  for (; it1 != segment_sequence.end() && it2 != segments_violate.end(); ++it1, ++it2) {
    ASSERT_EQ((*it1)->path(), (*it2)->path());
    LOG(INFO) << "Segment " << (*it1)->path() << " to be GCed";
  }
}

class CDCServiceTestDurableMinReplicatedIndex : public CDCServiceTest {
 public:
  void SetUp() override {
    // Immediately write any index provided by a GetChanges request to cdc_state table.
    FLAGS_cdc_state_checkpoint_update_interval_ms = 0;
    FLAGS_update_min_cdc_indices_interval_secs = 1;
    FLAGS_enable_log_retention_by_op_idx = true;
    CDCServiceTest::SetUp();
  }
};

INSTANTIATE_TEST_CASE_P(EnableReplicateIntents, CDCServiceTestDurableMinReplicatedIndex,
                        ::testing::Bool());

TEST_P(CDCServiceTestDurableMinReplicatedIndex, TestBootstrapProducer) {
  constexpr int kNRows = 100;

  std::string tablet_id = GetTablet();

  const auto& proxy = cluster_->mini_tablet_server(0)->server()->proxy();
  for (int i = 0; i < kNRows; i++) {
    WriteTestRow(i, 10 + i, "key" + std::to_string(i), tablet_id, proxy);
  }

  // Verify that the cdc_min_replicated_index was initialized at the max index.
  auto tablet_peer = ASSERT_RESULT(
      cluster_->mini_tablet_server(0)->server()->tablet_manager()->GetTablet(tablet_id));
  WaitForCDCIndex(tablet_peer, OpId::Max().index, 4 * FLAGS_update_min_cdc_indices_interval_secs);

  // Force the producer bootstrap to fail after updating the tablet replication index entries.
  FLAGS_TEST_cdc_inject_replication_index_update_failure = true;

  // Verify that the producer bootstrap request fails.
  BootstrapProducerRequestPB req;
  BootstrapProducerResponsePB resp;
  req.add_table_ids(table_.table()->id());
  rpc::RpcController rpc;
  ASSERT_OK(cdc_proxy_->BootstrapProducer(req, &resp, &rpc));
  ASSERT_TRUE(resp.has_error());

  // Verify that the cdc_min_replicated_index remains in the initial state. This tests that the
  // the tablet replication index was rolled back after the injected failure.
  tablet_peer = ASSERT_RESULT(
      cluster_->mini_tablet_server(0)->server()->tablet_manager()->GetTablet(tablet_id));
  WaitForCDCIndex(tablet_peer, OpId::Max().index, 4 * FLAGS_update_min_cdc_indices_interval_secs);

  // Clear the error injection.
  FLAGS_TEST_cdc_inject_replication_index_update_failure = false;

  // Verify that the next producer bootstrap request succeeds.
  rpc.Reset();
  ASSERT_OK(cdc_proxy_->BootstrapProducer(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error());

  ASSERT_EQ(resp.cdc_bootstrap_ids().size(), 1);

  string bootstrap_id = resp.cdc_bootstrap_ids(0);

  // Verify that for each of the table's tablets, a new row in cdc_state table with the returned
  // id was inserted.
  client::TableHandle table;
  client::YBTableName cdc_state_table(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
  ASSERT_OK(table.Open(cdc_state_table, client_.get()));
  ASSERT_EQ(1, boost::size(client::TableRange(table)));
  int nrows = 0;
  for (const auto& row : client::TableRange(table)) {
    nrows++;
    stream_id_ = row.column(master::kCdcStreamIdIdx).string_value();
    ASSERT_EQ(stream_id_, bootstrap_id);

    string checkpoint = row.column(master::kCdcCheckpointIdx).string_value();
    auto s = OpId::FromString(checkpoint);
    ASSERT_OK(s);
    OpId op_id = *s;
    // When no writes are present, the checkpoint's index is 1. Plus two for the failed and
    // successful ALTER WAL RETENTION TIME that we issue when cdc is enabled on a table.
    ASSERT_EQ(op_id.index, 3 + kNRows);
  }

  // This table only has one tablet.
  ASSERT_EQ(nrows, 1);

  // Ensure that cdc_min_replicated_index is set to the correct value after Bootstrap.
  tablet_peer = ASSERT_RESULT(
      cluster_->mini_tablet_server(0)->server()->tablet_manager()->GetTablet(tablet_id));
  auto latest_opid = tablet_peer->log()->GetLatestEntryOpId();
  WaitForCDCIndex(tablet_peer, latest_opid.index, 4 * FLAGS_update_min_cdc_indices_interval_secs);
}

TEST_P(CDCServiceTestDurableMinReplicatedIndex, TestLogCDCMinReplicatedIndexIsDurable) {
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);

  std::string tablet_id = GetTablet();

  const auto& proxy = cluster_->mini_tablet_server(0)->server()->proxy();

  auto tablet_peer = ASSERT_RESULT(
      cluster_->mini_tablet_server(0)->server()->tablet_manager()->GetTablet(tablet_id));
  // Write a row so that the next GetChanges request doesn't fail.
  WriteTestRow(0, 10, "key0", tablet_id, proxy);

  // Get CDC changes.
  GetChanges(tablet_id, stream_id_, /* term */ 0, /* index */ 10);

  WaitForCDCIndex(tablet_peer, 10, 4 * FLAGS_update_min_cdc_indices_interval_secs);

  // Restart the entire cluster to verify that the CDC tablet metadata got loaded from disk.
  ASSERT_OK(cluster_->RestartSync());

  ASSERT_OK(WaitFor([&]() {
    auto tablet_peer =
        cluster_->mini_tablet_server(0)->server()->tablet_manager()->LookupTablet(tablet_id);
    if (tablet_peer) {
      if (tablet_peer->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY &&
          tablet_peer->log() != nullptr) {
        LOG(INFO) << "TServer is ready ";
        return true;
      }
    }
    return false;
  }, MonoDelta::FromSeconds(30) * kTimeMultiplier, "Wait until tablet has a leader."));

  // Verify the log and meta min replicated index was loaded correctly from disk.
  ASSERT_EQ(tablet_peer->log()->cdc_min_replicated_index(), 10);
  ASSERT_EQ(tablet_peer->tablet_metadata()->cdc_min_replicated_index(), 10);
}

class CDCServiceTestMinSpace : public CDCServiceTest {
 public:
  void SetUp() override {
    FLAGS_cdc_state_checkpoint_update_interval_ms = 0;
    FLAGS_log_min_segments_to_retain = 1;
    FLAGS_log_min_seconds_to_retain = 1;
    FLAGS_cdc_wal_retention_time_secs = 1;
    FLAGS_enable_log_retention_by_op_idx = true;
    // We want the logs to be GCed because of space, not because they exceeded the maximum time to
    // be retained.
    FLAGS_log_max_seconds_to_retain = 10 * 3600; // 10 hours.
    FLAGS_log_stop_retaining_min_disk_mb = 1;
    FLAGS_TEST_record_segments_violate_min_space_policy = true;

    // This will rollover log segments a lot faster.
    FLAGS_log_segment_size_bytes = 500;
    CDCServiceTest::SetUp();
  }
};

INSTANTIATE_TEST_CASE_P(EnableReplicateIntents, CDCServiceTestMinSpace, ::testing::Bool());

TEST_P(CDCServiceTestMinSpace, TestLogRetentionByOpId_MinSpace) {
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);

  std::string tablet_id = GetTablet();

  const auto& proxy = cluster_->mini_tablet_server(0)->server()->proxy();

  auto tablet_peer = ASSERT_RESULT(
      cluster_->mini_tablet_server(0)->server()->tablet_manager()->GetTablet(tablet_id));
  // Write a row so that the next GetChanges request doesn't fail.
  WriteTestRow(0, 10, "key0", tablet_id, proxy);

  // Get CDC changes.
  GetChanges(tablet_id, stream_id_, /* term */ 0, /* index */ 0);

  WaitForCDCIndex(tablet_peer, 0, 4 * FLAGS_update_min_cdc_indices_interval_secs);

  // Write a lot more data to generate many log files that can be GCed. This should take less
  // than kMaxSecondsToRetain for the next check to succeed.
  for (int i = 1; i <= 5000; i++) {
    WriteTestRow(i, 10 + i, "key" + std::to_string(i), tablet_id, proxy);
  }

  log::SegmentSequence segment_sequence;
  ASSERT_OK(tablet_peer->log()->GetSegmentsToGCUnlocked(std::numeric_limits<int64_t>::max(),
                                                        &segment_sequence));
  ASSERT_EQ(segment_sequence.size(), 0);

  FLAGS_TEST_simulate_free_space_bytes = 128;

  ASSERT_OK(tablet_peer->log()->GetSegmentsToGCUnlocked(std::numeric_limits<int64_t>::max(),
                                                        &segment_sequence));
  ASSERT_GT(segment_sequence.size(), 0);
  const auto& segments_violate =
      *(tablet_peer->log()->reader_->TEST_segments_violate_min_space_policy_);
  ASSERT_EQ(segment_sequence.size(), segments_violate.size());
  auto it1 = segment_sequence.begin();
  auto it2 = segments_violate.begin();
  for (; it1 != segment_sequence.end() && it2 != segments_violate.end(); ++it1, ++it2) {
    ASSERT_EQ((*it1)->path(), (*it2)->path());
    LOG(INFO) << "Segment " << (*it1)->path() << " to be GCed";
  }

  int32_t num_gced(0);
  ASSERT_OK(tablet_peer->log()->GC(std::numeric_limits<int64_t>::max(), &num_gced));
  ASSERT_EQ(num_gced, segment_sequence.size());

  // Read from 0.0.  This should start reading from the beginning of the logs.
  GetChanges(tablet_id, stream_id_, /* term */ 0, /* index */ 0);
}

class CDCLogAndMetaIndex : public CDCServiceTest {
 public:
  void SetUp() override {
    // Immediately write any index provided by a GetChanges request to cdc_state table.
    FLAGS_cdc_state_checkpoint_update_interval_ms = 0;
    FLAGS_update_min_cdc_indices_interval_secs = 1;
    FLAGS_cdc_min_replicated_index_considered_stale_secs = 5;
    FLAGS_enable_log_retention_by_op_idx = true;
    CDCServiceTest::SetUp();
  }
};

INSTANTIATE_TEST_CASE_P(EnableReplicateIntents, CDCLogAndMetaIndex, ::testing::Bool());

TEST_P(CDCLogAndMetaIndex, TestLogAndMetaCdcIndex) {
  constexpr int kNStreams = 5;

  // This will rollover log segments a lot faster.
  FLAGS_log_segment_size_bytes = 100;

  CDCStreamId stream_id[kNStreams];

  for (int i = 0; i < kNStreams; i++) {
    CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id[i]);
  }

  std::string tablet_id = GetTablet();

  const auto &proxy = cluster_->mini_tablet_server(0)->server()->proxy();

  // Insert test rows.
  for (int i = 1; i <= kNStreams; i++) {
    WriteTestRow(i, 10 + i, "key" + std::to_string(i), tablet_id, proxy);
  }

  auto tablet_peer = ASSERT_RESULT(
      cluster_->mini_tablet_server(0)->server()->tablet_manager()->GetTablet(tablet_id));

  // Before any cdc request, the min index should be max value.
  ASSERT_EQ(tablet_peer->log()->cdc_min_replicated_index(), std::numeric_limits<int64_t>::max());
  ASSERT_EQ(tablet_peer->tablet_metadata()->cdc_min_replicated_index(),
            std::numeric_limits<int64_t>::max());

  for (int i = 0; i < kNStreams; i++) {
    // Get CDC changes.
    GetChanges(tablet_id, stream_id[i], /* term */ 0, /* index */ i);
  }

  // After the request succeeded, verify that the min cdc limit was set correctly. In this case
  // it belongs to stream_id[0] with index 0.
  WaitForCDCIndex(tablet_peer, 0, 4 * FLAGS_update_min_cdc_indices_interval_secs);

  // Changing the lowest index from all the streams should also be reflected in the log object.
  GetChanges(tablet_id, stream_id[0], /* term */ 0, /* index */ 4);

  // After the request succeeded, verify that the min cdc limit was set correctly. In this case
  // it belongs to stream_id[1] with index 1.
  WaitForCDCIndex(tablet_peer, 1, 4 * FLAGS_update_min_cdc_indices_interval_secs);

  for (int i = 0; i < kNStreams; i++) {
    ASSERT_OK(client_->DeleteCDCStream(stream_id[i],
                                       true /*force_delete*/,
                                       true /*ignore_errors*/));
  }
}

class CDCLogAndMetaIndexReset : public CDCLogAndMetaIndex {
 public:
  void SetUp() override {
    FLAGS_cdc_min_replicated_index_considered_stale_secs = 5;
    // This will rollover log segments a lot faster.
    FLAGS_log_segment_size_bytes = 100;
    CDCLogAndMetaIndex::SetUp();
  }
};

INSTANTIATE_TEST_CASE_P(EnableReplicateIntents, CDCLogAndMetaIndexReset, ::testing::Bool());

// Test that when all the streams for a specific tablet have been deleted, the log and meta
// cdc min replicated index is reset to max int64.
TEST_P(CDCLogAndMetaIndexReset, TestLogAndMetaCdcIndexAreReset) {
  constexpr int kNStreams = 5;

  // This will rollover log segments a lot faster.
  FLAGS_log_segment_size_bytes = 100;

  CDCStreamId stream_id[kNStreams];

  for (int i = 0; i < kNStreams; i++) {
    CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id[i]);
  }

  std::string tablet_id = GetTablet();

  const auto &proxy = cluster_->mini_tablet_server(0)->server()->proxy();

  // Insert test rows.
  for (int i = 1; i <= kNStreams; i++) {
    WriteTestRow(i, 10 + i, "key" + std::to_string(i), tablet_id, proxy);
  }

  auto tablet_peer = ASSERT_RESULT(
      cluster_->mini_tablet_server(0)->server()->tablet_manager()->GetTablet(tablet_id));

  // Before any cdc request, the min index should be max value.
  ASSERT_EQ(tablet_peer->log()->cdc_min_replicated_index(), std::numeric_limits<int64_t>::max());
  ASSERT_EQ(tablet_peer->tablet_metadata()->cdc_min_replicated_index(),
            std::numeric_limits<int64_t>::max());


  for (int i = 0; i < kNStreams; i++) {
    // Get CDC changes.
    GetChanges(tablet_id, stream_id[i], /* term */ 0, /* index */ 5);
  }

  // After the request succeeded, verify that the min cdc limit was set correctly. In this case
  // all the streams have index 5.
  WaitForCDCIndex(tablet_peer, 5, 4 * FLAGS_update_min_cdc_indices_interval_secs);

  client::TableHandle table;
  client::YBTableName cdc_state_table(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
  ASSERT_OK(table.Open(cdc_state_table, client_.get()));

  auto session = client_->NewSession();
  for (int i = 0; i < kNStreams; i++) {
    const auto delete_op = table.NewDeleteOp();
    auto* delete_req = delete_op->mutable_request();
    QLAddStringHashValue(delete_req, tablet_id);
    QLAddStringRangeValue(delete_req, stream_id[i]);
    session->Apply(delete_op);
  }
  ASSERT_OK(session->TEST_Flush());
  LOG(INFO) << "Successfully deleted all streams from cdc_state";

  SleepFor(MonoDelta::FromSeconds(FLAGS_cdc_min_replicated_index_considered_stale_secs + 1));

  LOG(INFO) << "Done sleeping";
  // RunLogGC should reset cdc min replicated index to max int64 because more than
  // FLAGS_cdc_min_replicated_index_considered_stale_secs seconds have elapsed since the index
  // was last updated.
  ASSERT_OK(tablet_peer->RunLogGC());
  LOG(INFO) << "GC done running";
  ASSERT_EQ(tablet_peer->log()->cdc_min_replicated_index(), std::numeric_limits<int64_t>::max());
  ASSERT_EQ(tablet_peer->tablet_metadata()->cdc_min_replicated_index(),
      std::numeric_limits<int64_t>::max());

  for (int i = 0; i < kNStreams; i++) {
    ASSERT_OK(client_->DeleteCDCStream(stream_id[i],
                                       true /*force_delete*/,
                                       true /*ignore_errors*/));
  }
}

class CDCServiceTestThreeServers : public CDCServiceTest {
 public:
  void SetUp() override {
    // We don't want the tablets to move in the middle of the test.
    FLAGS_enable_load_balancing = false;
    FLAGS_leader_failure_max_missed_heartbeat_periods = 12.0;
    FLAGS_update_min_cdc_indices_interval_secs = 5;
    FLAGS_enable_log_retention_by_op_idx = true;
    FLAGS_client_read_write_timeout_ms = 20 * 1000 * kTimeMultiplier;

    // Always update cdc_state table.
    FLAGS_cdc_state_checkpoint_update_interval_ms = 0;

    FLAGS_follower_unavailable_considered_failed_sec = 20 * kTimeMultiplier;

    CDCServiceTest::SetUp();
  }

  void DoTearDown() override {
    YBMiniClusterTestBase::DoTearDown();
  }

  virtual int server_count() override { return 3; }
  virtual int tablet_count() override { return 3; }

  // Get the first tablet_id for which any peer is a leader.
  void GetFirstTabletIdAndLeaderPeer(TabletId* tablet_id, ssize_t* leader_idx, int timeout_secs);
};

INSTANTIATE_TEST_CASE_P(EnableReplicateIntents, CDCServiceTestThreeServers, ::testing::Bool());

// Sometimes leadership takes a while. Keep retrying until timeout_secs seconds have elapsed.
void CDCServiceTestThreeServers::GetFirstTabletIdAndLeaderPeer(TabletId* tablet_id,
                                                               ssize_t* leader_idx,
                                                               int timeout_secs) {
  std::vector<TabletId> tablet_ids;
  // Verify that we are only returning a tablet that belongs to the table created for this test.
  GetTablets(&tablet_ids);
  ASSERT_EQ(tablet_ids.size(), tablet_count());

  MonoTime now = MonoTime::Now();
  MonoTime deadline = now + MonoDelta::FromSeconds(timeout_secs);
  while(now.ComesBefore(deadline) && (!tablet_id || tablet_id->empty())) {
    for (size_t idx = 0; idx < cluster_->num_tablet_servers(); idx++) {
      auto peers = cluster_->mini_tablet_server(idx)->server()->tablet_manager()->GetTabletPeers();
      ASSERT_GT(peers.size(), 0);

      for (const auto &peer : peers) {
        auto it = std::find(tablet_ids.begin(), tablet_ids.end(), peer->tablet_id());
        if (it != tablet_ids.end() &&
            peer->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY) {
          *tablet_id = peer->tablet_id();
          *leader_idx = idx;
          LOG(INFO) << "Selected tablet " << tablet_id << " for tablet server " << idx;
          break;
        }
      }
    }
    now = MonoTime::Now();
  }
}


// Test that whenever a leader change happens (forced here by shutting down the tablet leader),
// next leader correctly reads the minimum applied cdc index by reading the cdc_state table.
TEST_P(CDCServiceTestThreeServers, TestNewLeaderUpdatesLogCDCAppliedIndex) {
  constexpr int kNRecords = 30;
  constexpr int kGettingLeaderTimeoutSecs = 20;

  TabletId tablet_id;
  // Index of the TS that is the leader for the selected tablet_id.
  ssize_t leader_idx = -1;

  GetFirstTabletIdAndLeaderPeer(&tablet_id, &leader_idx, kGettingLeaderTimeoutSecs);
  ASSERT_FALSE(tablet_id.empty());
  ASSERT_GE(leader_idx, 0);

  const auto &proxy = cluster_->mini_tablet_server(leader_idx)->server()->proxy();
  for (int i = 0; i < kNRecords; i++) {
    WriteTestRow(i, 10 + i, "key" + std::to_string(i), tablet_id, proxy);
  }
  LOG(INFO) << "Inserted " << kNRecords << " records";

  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);
  LOG(INFO) << "Created cdc stream " << stream_id_;

  std::shared_ptr<tablet::TabletPeer> tablet_peer;
  // Check that the index hasn't been updated in any of the peers.
  for (int idx = 0; idx < server_count(); idx++) {
    auto new_tablet_peer =
        cluster_->mini_tablet_server(idx)->server()->tablet_manager()->LookupTablet(tablet_id);
    if (new_tablet_peer) {
      tablet_peer = new_tablet_peer;
      ASSERT_EQ(tablet_peer->log()->cdc_min_replicated_index(), std::numeric_limits<int64>::max());
      ASSERT_EQ(tablet_peer->tablet_metadata()->cdc_min_replicated_index(),
                std::numeric_limits<int64>::max());
    }
  }

  // Kill the tablet leader tserver so that another tserver becomes the leader.
  cluster_->mini_tablet_server(leader_idx)->Shutdown();
  LOG(INFO) << "tserver " << leader_idx << " was shutdown";

  // CDC Proxy is pinned to the first TServer, so we need to update the proxy if we kill that one.
  if (leader_idx == 0) {
    cdc_proxy_ = std::make_unique<CDCServiceProxy>(
        &client_->proxy_cache(),
        HostPort::FromBoundEndpoint(cluster_->mini_tablet_server(1)->bound_rpc_addr()));
  }

  // Wait until GetChanges doesn't return any errors. This means that we are able to write to
  // the cdc_state table.
  ASSERT_OK(WaitFor([&](){
    bool has_error = false;
    GetChanges(tablet_id, stream_id_, /* term */ 0, /* index */ 5, &has_error);
    return !has_error;
  }, MonoDelta::FromSeconds(180) * kTimeMultiplier, "Wait until cdc state table can take writes."));

  std::unique_ptr<CDCServiceProxy> cdc_proxy;
  ASSERT_OK(WaitFor([&](){
    for (int idx = 0; idx < server_count(); idx++) {
      if (idx == leader_idx) {
        // This TServer is shutdown for now.
        continue;
      }
      tablet_peer =
          cluster_->mini_tablet_server(idx)->server()->tablet_manager()->LookupTablet(tablet_id);
      if (tablet_peer && tablet_peer->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY) {
        LOG(INFO) << "Found new leader for tablet " << tablet_id << " in TS " << idx;
        return true;
      }
    }
    return false;
  }, MonoDelta::FromSeconds(30) * kTimeMultiplier, "Wait until tablet has a leader."));

  SleepFor(MonoDelta::FromSeconds((FLAGS_update_min_cdc_indices_interval_secs * 3)));
  LOG(INFO) << "Done sleeping";

  ASSERT_EQ(tablet_peer->log()->cdc_min_replicated_index(), 5);
  ASSERT_EQ(tablet_peer->tablet_metadata()->cdc_min_replicated_index(), 5);

  ASSERT_OK(cluster_->mini_tablet_server(leader_idx)->Start());
  ASSERT_OK(cluster_->mini_tablet_server(leader_idx)->WaitStarted());
}

class CDCServiceLowRpc: public CDCServiceTest {
 public:
  void SetUp() override {
    FLAGS_rpc_workers_limit = 4;
    FLAGS_transaction_manager_workers_limit = 4;
    // Uncommenting below causes test to timeout. We stop throttling with a negative ratio.
    // FLAGS_cdc_get_changes_free_rpc_ratio = -.5;

    CDCServiceTest::SetUp();
  }
};

INSTANTIATE_TEST_CASE_P(EnableReplicateIntents, CDCServiceLowRpc, ::testing::Values(true));

TEST_P(CDCServiceLowRpc, TestGetChangesRpcMax) {
  CreateCDCStream(cdc_proxy_, table_.table()->id(), &stream_id_);

  std::string tablet_id = GetTablet();

  tserver::MiniTabletServer* leader_mini_tserver;
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    leader_mini_tserver = GetLeaderForTablet(tablet_id);
    return leader_mini_tserver != nullptr;
  }, MonoDelta::FromSeconds(30) * kTimeMultiplier, "Wait for tablet to have a leader."));
  ASSERT_NO_FATALS(WriteTestRow(0, 200, "key0", tablet_id, leader_mini_tserver->server()->proxy()));

  // Call GetChanges with 20 threads, even though our setup only allows 7 RPC threads at a time.
  int num_get_changes = 200;
  int num_threads = 20;
  TestThreadHolder thread_holder;
  std::atomic<int> gets(0);
  std::atomic<int> not_ready_errors(0);
  for (int i = 0; i != num_threads; ++i) {
    thread_holder.AddThreadFunctor([this, &gets, &not_ready_errors, tablet_id,
                                    &stop = thread_holder.stop_flag()] {
      for (int cnt = 0; cnt < 10; ++cnt) {
        int value = ++gets;
        bool has_error = false;
        ::yb::cdc::CDCErrorPB_Code code;
        // Call GetChanges for this one row
        GetChanges(tablet_id, stream_id_, 0, value, &has_error, &code);
        // We should either succeed or error specifically because we're being throttled.
        if (has_error) {
          ASSERT_EQ(code, CDCErrorPB_Code_LEADER_NOT_READY);
          ++not_ready_errors;
        }
      }
    });
  }

  // Wait for the threads to finish running GetChanges on all entries.
  while (!thread_holder.stop_flag().load(std::memory_order_acquire)) {
    auto cur = gets.load(std::memory_order_acquire);
    if (cur >= num_get_changes) {
      break;
    }
    LOG(INFO) << "Num getChanges " << cur << " of " << num_get_changes;
    SleepFor(MonoDelta::FromMilliseconds(100));
  }

  thread_holder.Stop();

  // We should have gotten some throttling errors because ran more RPC threads than available.
  LOG(INFO) << "LEADER_NOT_READY errors: " << not_ready_errors.load(std::memory_order_acquire);
}

} // namespace cdc
} // namespace yb
