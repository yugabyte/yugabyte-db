// Copyright (c) YugabyteDB, Inc.
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
#include "yb/cdc/cdc_service.h"

#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/schema.h"

#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/session.h"
#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/mini_master.h"
#include "yb/master/master.h"
#include "yb/tserver/heartbeater.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/flags/auto_flags_util.h"
#include "yb/util/string_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

DECLARE_int32(heartbeat_interval_ms);
DECLARE_uint32(auto_flags_apply_delay_ms);
DECLARE_bool(TEST_tserver_disable_heartbeat);
DECLARE_bool(ysql_enable_packed_row);

namespace yb {
namespace cdc {

using namespace std::chrono_literals;
const MonoDelta kTimeout = 20s * kTimeMultiplier;
constexpr int kRpcTimeout = NonTsanVsTsan(60, 120);
const int kNumMasterServers = 1;
const int kNumTServers = 1;
const auto kNamespace = "my_namespace";
const auto kTableName = "my_table";
const auto kKeyColumnName = "key";
const auto kValueColumnName = "value";
const auto kYbTableName = client::YBTableName(YQL_DATABASE_CQL, kNamespace, kTableName);

class XClusterProducerTest : public MiniClusterTestWithClient<MiniCluster> {
 public:
  virtual void SetUp() override {
    MiniClusterTestWithClient<MiniCluster>::SetUp();
    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = kNumTServers;
    opts.num_masters = kNumMasterServers;
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(cluster_->WaitForTabletServerCount(opts.num_tablet_servers));

    ASSERT_OK(CreateClient());
    ASSERT_OK(client_->CreateNamespace(kNamespace));

    tablet_server_ = cluster_->mini_tablet_servers().front().get();
    cdc_proxy_ = std::make_unique<cdc::CDCServiceProxy>(
        &client_->proxy_cache(), HostPort::FromBoundEndpoint(tablet_server_->bound_rpc_addr()));

    ASSERT_OK(CreateTable());
    stream_id_ = ASSERT_RESULT(CreateCDCStream());
  }

  Status CreateTable() {
    client::YBSchema schema;
    client::YBSchemaBuilder b;
    b.AddColumn(kKeyColumnName)->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
    b.AddColumn(kValueColumnName)->Type(DataType::INT32)->NotNull();
    RETURN_NOT_OK(b.Build(&schema));

    std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
    RETURN_NOT_OK(table_creator->table_name(kYbTableName).schema(&schema).wait(true).Create());
    RETURN_NOT_OK(client_->OpenTable(kYbTableName, &table_));

    auto tablet_ids = ListTabletIdsForTable(cluster_.get(), table_->id());
    SCHECK_EQ(tablet_ids.size(), 1, IllegalState, "Expected 1 tablet");
    tablet_id_ = *tablet_ids.begin();

    return Status::OK();
  }

  Result<xrepl::StreamId> CreateCDCStream() {
    CreateCDCStreamRequestPB req;
    CreateCDCStreamResponsePB resp;
    req.set_table_id(table_->id());
    req.set_source_type(XCLUSTER);
    req.set_checkpoint_type(IMPLICIT);
    req.set_record_format(WAL);

    rpc::RpcController rpc;
    RETURN_NOT_OK(cdc_proxy_->CreateCDCStream(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    return xrepl::StreamId::FromString(resp.stream_id());
  }

  Status InsertRows(uint32_t start, uint32_t end) {
    return WriteRows(start, end, /* delete_op */ false);
  }

  Status WriteRows(int32_t start, int32_t end, bool delete_op) {
    auto session = client_->NewSession(kRpcTimeout * 1s);
    client::TableHandle table_handle;
    RETURN_NOT_OK(table_handle.Open(kYbTableName, client_.get()));
    std::vector<client::YBOperationPtr> ops;

    LOG(INFO) << "Writing " << end - start << (delete_op ? " deletes" : " inserts");
    for (int32_t i = start; i < end; i++) {
      auto op = delete_op ? table_handle.NewDeleteOp() : table_handle.NewInsertOp();
      auto req = op->mutable_request();
      QLAddInt32HashValue(req, i);
      table_handle.AddInt32ColumnValue(req, table_handle->schema().Column(1).name(), i);
      ops.push_back(std::move(op));
    }
    RETURN_NOT_OK(session->TEST_ApplyAndFlush(ops));

    return Status::OK();
  }

  Result<cdc::GetChangesResponsePB> GetChanges(
      const OpIdPB& op_id = consensus::MinimumOpId(),
      std::optional<uint32> auto_flags_config_version = std::nullopt) {
    cdc::GetChangesResponsePB resp;
    cdc::GetChangesRequestPB req;
    req.set_stream_id(stream_id_.ToString());
    req.set_tablet_id(tablet_id_);
    if (op_id.index() > 0 || op_id.term() > 0) {
      req.mutable_from_checkpoint()->mutable_op_id()->CopyFrom(op_id);
    }
    if (auto_flags_config_version) {
      req.set_auto_flags_config_version(*auto_flags_config_version);
    }

    rpc::RpcController rpc;
    RETURN_NOT_OK(cdc_proxy_->GetChanges(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    LOG_WITH_FUNC(INFO) << resp.ShortDebugString();
    return resp;
  }

  Result<uint32> GetAutoFlagsConfigVersion() {
    return VERIFY_RESULT(cluster_->GetLeaderMiniMaster())
        ->master()
        ->GetAutoFlagsConfig()
        .config_version();
  }

  tserver::MiniTabletServer* tablet_server_;
  std::unique_ptr<cdc::CDCServiceProxy> cdc_proxy_;
  std::shared_ptr<client::YBTable> table_;
  TabletId tablet_id_;
  xrepl::StreamId stream_id_ = xrepl::StreamId::Nil();
};

// Basic GetChanges RPC test.
TEST_F(XClusterProducerTest, GetChangesBasic) {
  const int kBatchCount = 10;
  auto resp = ASSERT_RESULT(GetChanges());
  ASSERT_EQ(resp.records_size(), 1);
  ASSERT_EQ(resp.records(0).operation(), CDCRecordPB::CHANGE_METADATA);
  ASSERT_EQ(resp.records(0).changes_size(), 0);
  ASSERT_TRUE(resp.has_checkpoint());
  ASSERT_TRUE(resp.checkpoint().has_op_id());
  auto last_op_id = resp.checkpoint().op_id();
  ASSERT_TRUE(resp.has_safe_hybrid_time());

  ASSERT_OK(InsertRows(0, kBatchCount));
  resp = ASSERT_RESULT(GetChanges(last_op_id));
  ASSERT_EQ(resp.records_size(), kBatchCount);

  for (uint32 i = 0; i < kBatchCount; i++) {
    const auto& record = resp.records(i);
    ASSERT_EQ(record.operation(), CDCRecordPB::WRITE);
    ASSERT_EQ(record.key_size(), 1);
    ASSERT_TRUE(record.key(0).has_key());
    ASSERT_TRUE(record.key(0).has_value());
    ASSERT_TRUE(record.key(0).value().has_binary_value());
    ASSERT_EQ(record.changes_size(), FLAGS_ysql_enable_packed_row ? 2 : 1);
    ASSERT_TRUE(record.changes(0).has_key());
    ASSERT_TRUE(record.changes(0).has_value());
    ASSERT_TRUE(record.changes(0).value().has_binary_value());
  }

  ASSERT_GT(resp.checkpoint().op_id().index(), last_op_id.index());
}

// Verify GetChanges errors out when the wrong AutoFlags config version is set.
TEST_F(XClusterProducerTest, GetChangesWithAutoFlags) {
  auto config_version = ASSERT_RESULT(GetAutoFlagsConfigVersion());

  auto resp = ASSERT_RESULT(GetChanges());
  ASSERT_EQ(resp.records_size(), 1);
  auto last_op_id = resp.checkpoint().op_id();

  // We should fail if AutoFlags config version is not valid.
  auto resp_res = GetChanges(last_op_id, kInvalidAutoFlagsConfigVersion);
  ASSERT_NOK(resp_res);
  ASSERT_TRUE(HasSubstring(resp_res.status().ToString(), "AutoFlags config version mismatch"));

  // We should fail if the AutoFlags config version is not the current.
  resp_res = GetChanges(last_op_id, config_version + 1);
  ASSERT_NOK(resp_res);
  ASSERT_TRUE(HasSubstring(resp_res.status().ToString(), "AutoFlags config version mismatch"));
  resp_res = GetChanges(last_op_id, config_version - 1);
  ASSERT_NOK(resp_res);
  ASSERT_TRUE(HasSubstring(resp_res.status().ToString(), "AutoFlags config version mismatch"));

  // We should succeed if the AutoFlags config version is the current.
  resp = ASSERT_RESULT(GetChanges(last_op_id, config_version));
  ASSERT_EQ(resp.records_size(), 0);
}

// Verify GetChanges errors out when the tserver has not heartbeated, even if there is no data to
// get.
TEST_F(XClusterProducerTest, HeartbeatDelayWithoutData) {
  auto config_version = ASSERT_RESULT(GetAutoFlagsConfigVersion());

  auto resp = ASSERT_RESULT(GetChanges());
  ASSERT_EQ(resp.records_size(), 1);
  auto last_op_id = resp.checkpoint().op_id();

  // Set very low auto_flags_apply_delay_ms and disable heartbeats.
  auto old_auto_flags_apply_delay_ms = FLAGS_auto_flags_apply_delay_ms;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_flags_apply_delay_ms) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_disable_heartbeat) = true;
  // Wait for inflight heartbeats to finish.
  SleepFor(1s * kTimeMultiplier);

  // GetChanges without AutoFlags config version should succeed.
  resp = ASSERT_RESULT(GetChanges(last_op_id));
  ASSERT_EQ(resp.records_size(), 0);

  // GetChanges with AutoFlags config version should fail.
  auto resp_res = GetChanges(last_op_id, config_version);
  ASSERT_NOK(resp_res);
  ASSERT_TRUE(HasSubstring(resp_res.status().ToString(), "AutoFlags config is stale"));

  // We should fail even if rows are present.
  ASSERT_OK(InsertRows(0, 1));
  ASSERT_NOK(GetChanges(last_op_id, config_version));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_flags_apply_delay_ms) = old_auto_flags_apply_delay_ms;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_disable_heartbeat) = false;
  tablet_server_->server()->heartbeater()->TriggerASAP();
  // Wait for inflight heartbeat to finish.
  SleepFor(1s * kTimeMultiplier);

  // We should now succeed.
  resp = ASSERT_RESULT(GetChanges(last_op_id, config_version));
  ASSERT_EQ(resp.records_size(), 1);
}

// Verify GetChanges errors out tserver has not heartbeated and there is data to get.
TEST_F(XClusterProducerTest, HeartbeatDelayWithData) {
  auto config_version = ASSERT_RESULT(GetAutoFlagsConfigVersion());

  const auto auto_flags_apply_delay_ms = 3 * MonoTime::kMillisecondsPerSecond * kTimeMultiplier;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_flags_apply_delay_ms) = auto_flags_apply_delay_ms;

  auto resp = ASSERT_RESULT(GetChanges());
  ASSERT_EQ(resp.records_size(), 1);
  auto last_op_id = resp.checkpoint().op_id();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_disable_heartbeat) = true;

  // We should be able to succeed since the lease is still valid.
  resp = ASSERT_RESULT(GetChanges(last_op_id, config_version));
  ASSERT_EQ(resp.records_size(), 0);

  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"GetChanges::AfterFirstValidateAutoFlagsConfigVersion1",
        "XClusterProducerTest::BeforeInsertRows"},
       {"XClusterProducerTest::AfterInsertRows",
        "GetChanges::AfterFirstValidateAutoFlagsConfigVersion2"}});
  yb::SyncPoint::GetInstance()->EnableProcessing();

  Result<GetChangesResponsePB> resp_res = resp;
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this, &resp_res, last_op_id, config_version]() {
    resp_res = GetChanges(last_op_id, config_version);
  });

  // AfterFirstValidateAutoFlagsConfigVersion1 should succeed and we should hit this sync point
  // since lease is still valid.
  TEST_SYNC_POINT("XClusterProducerTest::BeforeInsertRows");
  ASSERT_OK(InsertRows(0, 1));

  LOG(INFO) << "Sleeping for " << auto_flags_apply_delay_ms * 2 << "ms";
  SleepFor(auto_flags_apply_delay_ms * 2ms);
  TEST_SYNC_POINT("XClusterProducerTest::AfterInsertRows");

  thread_holder.JoinAll();
  ASSERT_NOK(resp_res);
  ASSERT_TRUE(HasSubstring(resp_res.status().ToString(), "AutoFlags config is stale"));

  // Enable heartbeats and wait for the first one to go through.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_disable_heartbeat) = false;
  LOG(INFO) << "Sleeping for " << auto_flags_apply_delay_ms * 2 << "ms";
  SleepFor(auto_flags_apply_delay_ms * 2ms);

  // We should not be able to succeed.
  resp = ASSERT_RESULT(GetChanges(last_op_id, config_version));
  ASSERT_EQ(resp.records_size(), 1);
}

// Test the upgrade of an xCluster producer. GetChanges invoked with the older config version should
// fail.
TEST_F(XClusterProducerTest, ProducerUpgrade) {
  auto config_version = ASSERT_RESULT(GetAutoFlagsConfigVersion());
  auto resp = ASSERT_RESULT(GetChanges());
  ASSERT_EQ(resp.records_size(), 1);
  auto last_op_id = resp.checkpoint().op_id();

  resp = ASSERT_RESULT(GetChanges(last_op_id, config_version));
  ASSERT_EQ(resp.records_size(), 0);

  // Force PromoteAutoFlags to that the config version changes.
  master::PromoteAutoFlagsRequestPB promote_auto_flags_req;
  promote_auto_flags_req.set_max_flag_class(ToString(AutoFlagClass::kExternal));
  promote_auto_flags_req.set_promote_non_runtime_flags(true);
  promote_auto_flags_req.set_force(true);
  master::PromoteAutoFlagsResponsePB promote_auto_flags_resp;
  auto leader_master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->master();
  ASSERT_OK(leader_master->catalog_manager_impl()->PromoteAutoFlags(
      &promote_auto_flags_req, &promote_auto_flags_resp));
  ASSERT_FALSE(promote_auto_flags_resp.has_error());
  ASSERT_EQ(promote_auto_flags_resp.new_config_version(), config_version + 1);

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto new_config_version =
            tablet_server_->server()->TEST_GetAutoFlagConfig().config_version();
        return new_config_version == config_version + 1;
      },
      kTimeout, "Wait for config version to change on tserver"));

  // We should fail since the config version has changed.
  ASSERT_NOK(GetChanges(last_op_id, config_version));

  resp = ASSERT_RESULT(GetChanges(last_op_id, config_version + 1));
  ASSERT_EQ(resp.records_size(), 0);
}

}  // namespace cdc
}  // namespace yb
