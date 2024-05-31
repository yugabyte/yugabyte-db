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
#include <map>
#include <string>
#include <utility>
#include <chrono>
#include <boost/assign.hpp>
#include <gtest/gtest.h>

#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/cdc/cdc_state_table.h"
#include "yb/cdc/xcluster_util.h"
#include "yb/cdc/xrepl_stream_metadata.h"

#include "yb/client/client-test-util.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/table.h"
#include "yb/client/transaction_rpc.h"
#include "yb/client/transaction.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_value.h"
#include "yb/common/schema.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/log.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/xcluster/xcluster_ycql_test_base.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/xcluster_consumer_registry_service.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/server/hybrid_clock.h"

#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet.h"

#include "yb/tserver/xcluster_consumer_if.h"
#include "yb/tserver/xcluster_poller.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/curl_util.h"
#include "yb/util/faststring.h"
#include "yb/util/flags.h"
#include "yb/util/jsonreader.h"
#include "yb/util/metrics.h"
#include "yb/util/random.h"
#include "yb/util/status_log.h"
#include "yb/util/status.h"
#include "yb/util/stopwatch.h"

using std::string;

using namespace std::literals;

DECLARE_bool(TEST_block_get_changes);
DECLARE_bool(TEST_cdc_skip_replication_poll);
DECLARE_bool(TEST_disable_apply_committed_transactions);
DECLARE_bool(TEST_disable_cleanup_applied_transactions);
DECLARE_bool(TEST_disable_wal_retention_time);
DECLARE_bool(TEST_exit_unfinished_deleting);
DECLARE_bool(TEST_exit_unfinished_merging);
DECLARE_bool(TEST_fail_setup_system_universe_replication);
DECLARE_bool(TEST_hang_wait_replication_drain);
DECLARE_double(TEST_respond_write_failed_probability);
DECLARE_bool(TEST_xcluster_write_hybrid_time);
DECLARE_uint64(TEST_yb_inbound_big_calls_parse_delay_ms);
DECLARE_bool(allow_insecure_connections);
DECLARE_int32(async_replication_idle_delay_ms);
DECLARE_uint32(async_replication_max_idle_wait);
DECLARE_int32(async_replication_polling_delay_ms);
DECLARE_uint32(cdc_wal_retention_time_secs);
DECLARE_string(certs_dir);
DECLARE_string(certs_for_cdc_dir);
DECLARE_bool(check_bootstrap_required);
DECLARE_uint64(consensus_max_batch_size_bytes);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(enable_ysql);
DECLARE_uint32(external_intent_cleanup_secs);
DECLARE_int32(global_log_cache_size_limit_mb);
DECLARE_int32(log_cache_size_limit_mb);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_int32(log_min_segments_to_retain);
DECLARE_uint64(log_segment_size_bytes);
DECLARE_int64(log_stop_retaining_min_disk_mb);
DECLARE_int32(ns_replication_sync_backoff_secs);
DECLARE_int32(ns_replication_sync_retry_secs);
DECLARE_uint32(replication_failure_delay_exponent);
DECLARE_int64(rpc_throttle_threshold_bytes);
DECLARE_int32(transaction_table_num_tablets);
DECLARE_int32(transaction_table_num_tablets);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_bool(use_client_to_server_encryption);
DECLARE_bool(use_node_to_node_encryption);
DECLARE_bool(xcluster_wait_on_ddl_alter);
DECLARE_bool(TEST_xcluster_disable_delete_old_pollers);
DECLARE_bool(enable_log_retention_by_op_idx);
DECLARE_bool(TEST_xcluster_disable_poller_term_check);
DECLARE_bool(TEST_xcluster_fail_to_send_create_snapshot_request);
DECLARE_bool(TEST_xcluster_fail_create_producer_snapshot);
DECLARE_int32(update_metrics_interval_ms);
DECLARE_int32(update_min_cdc_indices_interval_secs);
DECLARE_bool(enable_update_local_peer_min_index);
DECLARE_bool(TEST_xcluster_fail_snapshot_transfer);
DECLARE_bool(TEST_xcluster_fail_restore_consumer_snapshot);
DECLARE_bool(TEST_xcluster_simulate_get_changes_response_error);
DECLARE_double(TEST_xcluster_simulate_random_failure_after_apply);
DECLARE_uint32(cdcsdk_retention_barrier_no_revision_interval_secs);
DECLARE_int32(heartbeat_interval_ms);

namespace yb {

using client::YBClient;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBSession;
using client::YBTable;
using client::YBTableAlterer;
using client::YBTableName;
using master::MiniMaster;

using SessionTransactionPair = std::pair<client::YBSessionPtr, client::YBTransactionPtr>;

struct XClusterTestParams {
  explicit XClusterTestParams(bool transactional_table_)
      : transactional_table(transactional_table_) {}

  bool transactional_table;  // For XCluster + CQL only. All YSQL tables are transactional.
};

class XClusterTestNoParam : public XClusterYcqlTestBase {
 public:
  virtual Status SetUpWithParams(
      const std::vector<uint32_t>& num_consumer_tablets,
      const std::vector<uint32_t>& num_producer_tablets, uint32_t replication_factor,
      uint32_t num_masters = 1, uint32_t num_tservers = 1) override {
    return XClusterYcqlTestBase::SetUpWithParams(
        num_consumer_tablets, num_producer_tablets, replication_factor, num_masters, num_tservers);
  }

  virtual Status SetUpWithParams() override { return XClusterYcqlTestBase::SetUpWithParams(); }

  Status SetUpWithParams(
      const std::vector<uint32_t>& num_tablets_per_table, uint32_t replication_factor) {
    return SetUpWithParams(
        num_tablets_per_table, num_tablets_per_table, replication_factor, 1 /* num_masters */,
        1 /* num_tservers */);
  }

  virtual bool UseTransactionalTables() override { return false; }

  Result<YBTableName> CreateTable(
      YBClient* client, const std::string& namespace_name, const std::string& table_name,
      uint32_t num_tablets) {
    return XClusterTestBase::CreateTable(client, namespace_name, table_name, num_tablets, &schema_);
  }

  Result<YBTableName> CreateTable(
      uint32_t idx, uint32_t num_tablets, YBClient* client, YBSchema schema) {
    return XClusterTestBase::CreateTable(
        client, namespace_name, Format("test_table_$0", idx), num_tablets, &schema);
  }

  Result<SessionTransactionPair> CreateSessionWithTransaction(
      YBClient* client, client::TransactionManager* txn_mgr) {
    auto session = client->NewSession(kRpcTimeout * 1s);
    auto transaction = std::make_shared<client::YBTransaction>(txn_mgr);
    ReadHybridTime read_time;
    RETURN_NOT_OK(transaction->Init(IsolationLevel::SNAPSHOT_ISOLATION, read_time));
    session->SetTransaction(transaction);
    return std::make_pair(session, transaction);
  }

  Status InsertIntentsOnProducer(
      const std::shared_ptr<YBSession>& session, uint32_t start, uint32_t end,
      std::shared_ptr<client::YBTable> producer_table = {}) {
    if (!producer_table) {
      producer_table = producer_table_;
    }
    return WriteIntents(session, start, end, producer_client(), producer_table);
  }

  Status DeleteIntentsOnProducer(
      const std::shared_ptr<YBSession>& session, uint32_t start, uint32_t end,
      std::shared_ptr<client::YBTable> producer_table = {}) {
    if (!producer_table) {
      producer_table = producer_table_;
    }
    return WriteIntents(
        session, start, end, producer_client(), producer_table, true /* delete_op */);
  }

  Status InsertTransactionalBatchOnProducer(
      uint32_t start, uint32_t end, std::shared_ptr<client::YBTable> producer_table = {}) {
    if (!producer_table) {
      producer_table = producer_table_;
    }
    auto [session, txn] =
        VERIFY_RESULT(CreateSessionWithTransaction(producer_client(), producer_txn_mgr()));
    RETURN_NOT_OK(WriteIntents(session, start, end, producer_client(), producer_table));
    return txn->CommitFuture().get();
  }

  Status SetupReplication() { return SetupUniverseReplication(producer_tables_); }

  Result<xrepl::StreamId> GetCDCStreamID(const std::string& producer_table_id) {
    master::ListCDCStreamsResponsePB stream_resp;
    RETURN_NOT_OK(GetCDCStreamForTable(producer_table_id, &stream_resp));

    if (stream_resp.streams_size() != 1) {
      return STATUS(IllegalState, Format("Expected 1 stream, have $0", stream_resp.streams_size()));
    }

    if (stream_resp.streams(0).table_id().Get(0) != producer_table_id) {
      return STATUS(
          IllegalState, Format(
                            "Expected table id $0, have $1", producer_table_id,
                            stream_resp.streams(0).table_id().Get(0)));
    }

    return xrepl::StreamId::FromString(stream_resp.streams(0).stream_id());
  }

  YB_STRONGLY_TYPED_BOOL(EnableTLSEncryption);
  Status TestSetupUniverseReplication(
      EnableTLSEncryption enable_tls_encryption,
      Transactional transactional = Transactional::kFalse) {
    if (enable_tls_encryption) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_node_to_node_encryption) = true;
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_client_to_server_encryption) = true;
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_allow_insecure_connections) = false;
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_certs_dir) = GetCertsDir();
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_certs_for_cdc_dir) =
          JoinPathSegments(FLAGS_certs_dir, "xCluster");
      // XClusterTestBase::SetupUniverseReplication will copying the certs to the sub directories.
    }

    RETURN_NOT_OK(SetUpWithParams({8, 4}, {6, 6}, 3));

    RETURN_NOT_OK(SetupUniverseReplication(producer_tables_, {LeaderOnly::kTrue, transactional}));

    // Verify that universe was setup on consumer.
    master::GetUniverseReplicationResponsePB resp;
    RETURN_NOT_OK(VerifyUniverseReplication(&resp));
    SCHECK_EQ(
        resp.entry().replication_group_id(), kReplicationGroupId, InternalError,
        Format(
            "Unexpected replication group id: actual $0, expected $1",
            resp.entry().replication_group_id(), kReplicationGroupId));
    SCHECK_EQ(
        resp.entry().tables_size(), producer_tables_.size(), InternalError,
        Format(
            "Unexpected tables size: actual $0, expected $1", resp.entry().tables_size(),
            producer_tables_.size()));
    for (uint32_t i = 0; i < producer_tables_.size(); i++) {
      SCHECK_EQ(
          resp.entry().tables(i), producer_tables_[i]->id(), InternalError,
          Format(
              "Unexpected table entry: actual $0, expected $1", resp.entry().tables(i),
              producer_tables_[i]->id()));
    }

    // Verify that CDC streams were created on producer for all tables.
    for (size_t i = 0; i < producer_tables_.size(); i++) {
      master::ListCDCStreamsResponsePB stream_resp;
      RETURN_NOT_OK(GetCDCStreamForTable(producer_tables_[i]->id(), &stream_resp));
      SCHECK_EQ(
          stream_resp.streams_size(), 1, InternalError,
          Format("Unexpected streams size: actual $0, expected $1", stream_resp.streams_size(), 1));
      SCHECK_EQ(
          stream_resp.streams(0).table_id().Get(0), producer_tables_[i]->id(), InternalError,
          Format(
              "Unexpected table_id: actual $0, expected $1",
              stream_resp.streams(0).table_id().Get(0), producer_tables_[i]->id()));
    }

    for (size_t i = 0; i < producer_tables_.size(); i++) {
      RETURN_NOT_OK(InsertRowsInProducer(0, 5, producer_tables_[i]));
      RETURN_NOT_OK(VerifyRowsMatch(producer_tables_[i]));
    }

    return DeleteUniverseReplication();
  }

  Status InsertRowsAndVerify(
      uint32_t start, uint32_t end, std::shared_ptr<client::YBTable> producer_table = {},
      bool replication_enabled = true, int timeout = kRpcTimeout) {
    if (!producer_table) {
      producer_table = producer_table_;
    }
    LOG(INFO) << "Writing rows in table " << producer_table->name().ToString();
    RETURN_NOT_OK(InsertRowsInProducer(start, end, producer_table));
    if (replication_enabled) {
      return VerifyRowsMatch(producer_table, timeout);
    }

    auto s = VerifyRowsMatch(producer_table, timeout);
    SCHECK(!s.ok(), IllegalState, "Unexpected success when replication is disabled");
    return Status::OK();
  }

  // Empty producer_tables_to_pause will pause all streams.
  Status SetPauseAndVerifyInsert(
      uint32_t start, uint32_t end,
      const std::unordered_set<client::YBTable*>& producer_tables_to_pause = {},
      bool pause = true) {
    std::vector<xrepl::StreamId> stream_ids;
    for (auto producer_table : producer_tables_to_pause) {
      auto stream_id = VERIFY_RESULT(GetCDCStreamID(producer_table->id()));
      stream_ids.emplace_back(std::move(stream_id));
    }

    RETURN_NOT_OK(PauseResumeXClusterProducerStreams(stream_ids, pause));
    // Needs to sleep to wait for heartbeat to propogate.
    SleepFor(3s * kTimeMultiplier);

    for (auto& producer_table : producer_tables_) {
      bool table_paused = pause && (producer_tables_to_pause.empty() ||
                                    producer_tables_to_pause.contains(producer_table.get()));
      // Reduce the timeout time when we don't expect replication to be successful.
      int timeout = table_paused ? 10 : kRpcTimeout;
      RETURN_NOT_OK(InsertRowsAndVerify(start, end, producer_table, !table_paused, timeout));
    }
    return Status::OK();
  }

  Status FlushProducerTabletsAndGCLog() {
    RETURN_NOT_OK(producer_cluster()->FlushTablets());
    RETURN_NOT_OK(producer_cluster()->CompactTablets());
    for (size_t i = 0; i < producer_cluster()->num_tablet_servers(); ++i) {
      for (const auto& tablet_peer : producer_cluster()->GetTabletPeers(i)) {
        RETURN_NOT_OK(tablet_peer->RunLogGC());
      }
    }
    return Status::OK();
  }

  Status SetUpReplicationWithBootstrap(const int& num_tablets_per_table) {
    // Setup without tables.
    RETURN_NOT_OK(SetUpWithParams({}, {}, 3));

    // Only create tables on producer.
    std::vector<std::string> table_names = {"test_table_0", "test_table_1"};
    for (uint32_t i = 0; i < table_names.size(); i++) {
      auto t = VERIFY_RESULT(
          CreateTable(producer_client(), namespace_name, table_names[i], num_tablets_per_table));
      std::shared_ptr<client::YBTable> producer_table;
      RETURN_NOT_OK(producer_client()->OpenTable(t, &producer_table));
      producer_tables_.push_back(producer_table);
    }

    // Write some data
    for (auto& producer_table : producer_tables_) {
      RETURN_NOT_OK(InsertRowsInProducer(0, 100, producer_table));
    }

    return Status::OK();
  }

  Status SendSetupNamespaceReplicationRequest(
      const std::vector<std::shared_ptr<client::YBTable>>& tables,
      master::SetupNamespaceReplicationWithBootstrapResponsePB* resp) {
    master::NamespaceIdentifierPB producer_namespace;
    SCHECK(!tables.empty(), IllegalState, "Tables can't be empty");
    tables.front()->name().SetIntoNamespaceIdentifierPB(&producer_namespace);

    rpc::RpcController rpc;
    master::SetupNamespaceReplicationWithBootstrapRequestPB req;
    req.set_replication_id(kReplicationGroupId.ToString());
    req.mutable_producer_namespace()->CopyFrom(producer_namespace);
    string master_addr = producer_cluster()->GetMasterAddresses();
    auto hp_vec = VERIFY_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, req.mutable_producer_master_addresses());

    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(),
        VERIFY_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    return master_proxy->SetupNamespaceReplicationWithBootstrap(req, resp, &rpc);
  }

  Status VerifyCdcStateTableEntriesExistForTables(
      const std::size_t& tables_count, const int tablets_per_table) {
    std::unordered_map<xrepl::StreamId, int> stream_tablet_count;

    // Verify that for each of the table's tablets, a new row in cdc_state table with the
    // returned id was inserted.
    cdc::CDCStateTable cdc_state_table(producer_client());
    Status s;
    auto table_range = VERIFY_RESULT(
        cdc_state_table.GetTableRange(cdc::CDCStateTableEntrySelector().IncludeCheckpoint(), &s));
    SCHECK_EQ(
        std::distance(table_range.begin(), table_range.end()), tables_count * tablets_per_table,
        InternalError, "Mismatched size");

    for (auto row_result : table_range) {
      RETURN_NOT_OK(row_result);
      auto& row = *row_result;
      stream_tablet_count[row.key.stream_id]++;
      SCHECK(row.checkpoint, InternalError, "Invalid row checkpoint");
      SCHECK(row.checkpoint->index > 0, InternalError, "Row checkpoint index should be > 0");

      LOG(INFO) << "Bootstrap id " << row.key.stream_id << " for tablet " << row.key.tablet_id
                << " "
                << " has checkpoint " << *row.checkpoint;
    }

    SCHECK_EQ(
        stream_tablet_count.size(), tables_count, InternalError,
        "Tablet bootstraps and producer tables should be same size");
    for (const auto& e : stream_tablet_count) {
      SCHECK_EQ(e.second, tablets_per_table, InternalError, "Expected same num tablets per table");
    }
    return Status::OK();
  }

  Status VerifyNumSnapshots(client::YBClient* client, const size_t num_snapshots) {
    return LoggedWaitFor(
        [=]() -> Result<bool> {
          auto snapshots = VERIFY_RESULT(client->ListSnapshots());
          SCHECK_EQ(
              snapshots.size(), num_snapshots, IllegalState,
              Format("Expected $0 snapshots but received $1", num_snapshots, snapshots.size()));
          return true;
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Verify num snapshots");
  }

  Status VerifyNumCDCStreams(
      client::YBClient* client, yb::MiniCluster* cluster, const size_t num_streams) {
    return LoggedWaitFor(
        [=]() -> Result<bool> {
          rpc::RpcController rpc;
          master::ListCDCStreamsRequestPB req;
          master::ListCDCStreamsResponsePB resp;
          rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

          auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
              &client->proxy_cache(),
              VERIFY_RESULT(cluster->GetLeaderMiniMaster())->bound_rpc_addr());
          RETURN_NOT_OK(master_proxy->ListCDCStreams(req, &resp, &rpc));

          SCHECK_EQ(
              resp.streams_size(), num_streams, IllegalState,
              Format("Expected $0 snapshots but received $1", num_streams, resp.streams_size()));
          return true;
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Verify num CDC streams");
  }

  Status PopulateConsumerTables(const master::NamespaceIdentifierPB& producer_namespace) {
    master::NamespaceIdentifierPB consumer_namespace;
    consumer_namespace.CopyFrom(producer_namespace);
    consumer_namespace.clear_id();
    const auto consumer_tables =
        VERIFY_RESULT(consumer_client()->ListUserTables(consumer_namespace));

    SCHECK_EQ(
        consumer_tables.size(), producer_tables_.size(), IllegalState,
        "Number of consumer and producer tables do not match!");

    std::unordered_map<TableName, YBTableName> consumer_table_name_map;
    for (const auto& t : consumer_tables) {
      consumer_table_name_map[t.table_name()] = t;
    }

    // Populate consumer tables in the same order as producer tables such that the helper functions
    // VerifyRowsMatch work as intended.
    for (const auto& producer_table : producer_tables_) {
      const auto& t = consumer_table_name_map[producer_table->name().table_name()];

      std::shared_ptr<client::YBTable> consumer_table;
      RETURN_NOT_OK(consumer_client()->OpenTable(t, &consumer_table));
      consumer_tables_.push_back(consumer_table);
    }
    return Status::OK();
  }

  Status WaitForReplicationBootstrap(
      MiniCluster* consumer_cluster, YBClient* consumer_client,
      const xcluster::ReplicationGroupId& replication_group_id) {
    return LoggedWaitFor(
        [=]() -> Result<bool> {
          master::IsSetupNamespaceReplicationWithBootstrapDoneRequestPB req;
          master::IsSetupNamespaceReplicationWithBootstrapDoneResponsePB resp;
          req.set_replication_group_id(replication_group_id.ToString());

          auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
              &consumer_client->proxy_cache(),
              VERIFY_RESULT(consumer_cluster->GetLeaderMiniMaster())->bound_rpc_addr());
          rpc::RpcController rpc;
          rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

          RETURN_NOT_OK(
              master_proxy->IsSetupNamespaceReplicationWithBootstrapDone(req, &resp, &rpc));

          return resp.has_done() && resp.done();
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Is setup replication done");
  }

  Status VerifyReplicationBootstrapCleanupOnFailure(
      const xcluster::ReplicationGroupId& replication_group_id) {
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    master::IsSetupNamespaceReplicationWithBootstrapDoneRequestPB req;
    req.set_replication_group_id(replication_group_id.ToString());

    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(),
        VERIFY_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
    std::string error_msg = "Could not find universe replication bootstrap";

    RETURN_NOT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          rpc.Reset();

          master::IsSetupNamespaceReplicationWithBootstrapDoneResponsePB resp;
          RETURN_NOT_OK(
              master_proxy->IsSetupNamespaceReplicationWithBootstrapDone(req, &resp, &rpc));
          if (resp.has_error()) {
            return resp.error().status().message().find(error_msg) != string::npos;
          }
          return false;
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Is replication bootstrap cleanup done"));

    RETURN_NOT_OK(VerifyNumSnapshots(producer_client(), /* num_snapshots = */ 0));
    RETURN_NOT_OK(VerifyNumSnapshots(consumer_client(), /* num_snapshots = */ 0));
    RETURN_NOT_OK(
        VerifyNumCDCStreams(producer_client(), producer_cluster(), /* num_streams = */ 0));
    return Status::OK();
  }

  Status TestSetupReplicationWithBootstrapFailure() {
    constexpr int kNTabletsPerTable = 1;
    RETURN_NOT_OK(SetUpReplicationWithBootstrap(kNTabletsPerTable));

    master::SetupNamespaceReplicationWithBootstrapResponsePB resp;

    RETURN_NOT_OK(SendSetupNamespaceReplicationRequest(producer_tables_, &resp));
    return VerifyReplicationBootstrapCleanupOnFailure(kReplicationGroupId);
  }

 private:

  Status WriteIntents(
      const std::shared_ptr<YBSession>& session, uint32_t start, uint32_t end, YBClient* client,
      const std::shared_ptr<client::YBTable>& table, bool delete_op = false) {
    client::TableHandle table_handle;
    RETURN_NOT_OK(table_handle.Open(table->name(), client));
    std::vector<client::YBOperationPtr> ops;

    LOG(INFO) << (delete_op ? "Deleting" : "Inserting") << " transactional batch of key range ["
              << start << ", " << end << ") into" << table->name().ToString();
    for (uint32_t i = start; i < end; i++) {
      auto op = delete_op ? table_handle.NewDeleteOp() : table_handle.NewInsertOp();
      int32_t key = i;
      auto req = op->mutable_request();
      QLAddInt32HashValue(req, key);
      ops.emplace_back(std::move(op));
    }
    return session->TEST_ApplyAndFlush(ops);
  }

};

class XClusterTest : public XClusterTestNoParam,
                     public testing::WithParamInterface<XClusterTestParams> {
 public:
  virtual bool UseTransactionalTables() override { return GetParam().transactional_table; }
};

INSTANTIATE_TEST_CASE_P(
    XClusterTestParams, XClusterTest,
    ::testing::Values(
        XClusterTestParams(true /* transactional_table */),
        XClusterTestParams(false /* transactional_table */)));

TEST_P(XClusterTest, SetupUniverseReplication) {
  ASSERT_OK(TestSetupUniverseReplication(EnableTLSEncryption::kFalse));
}

TEST_P(XClusterTest, SetupUniverseReplicationWithTLSEncryption) {
  ASSERT_OK(TestSetupUniverseReplication(EnableTLSEncryption::kTrue));
}

TEST_P(XClusterTest, SetupUniverseReplicationErrorChecking) {
  ASSERT_OK(SetUpWithParams({1}, 1));
  rpc::RpcController rpc;
  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  {
    rpc.Reset();
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    master::SetupUniverseReplicationRequestPB setup_universe_req;
    master::SetupUniverseReplicationResponsePB setup_universe_resp;
    ASSERT_OK(
        master_proxy->SetupUniverseReplication(setup_universe_req, &setup_universe_resp, &rpc));
    ASSERT_TRUE(setup_universe_resp.has_error());
    std::string prefix = "Producer universe ID must be provided";
    ASSERT_TRUE(setup_universe_resp.error().status().message().substr(0, prefix.size()) == prefix);
  }

  {
    rpc.Reset();
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    master::SetupUniverseReplicationRequestPB setup_universe_req;
    setup_universe_req.set_replication_group_id(kReplicationGroupId.ToString());
    master::SetupUniverseReplicationResponsePB setup_universe_resp;
    ASSERT_OK(
        master_proxy->SetupUniverseReplication(setup_universe_req, &setup_universe_resp, &rpc));
    ASSERT_TRUE(setup_universe_resp.has_error());
    std::string prefix = "Producer master address must be provided";
    ASSERT_TRUE(setup_universe_resp.error().status().message().substr(0, prefix.size()) == prefix);
  }

  {
    rpc.Reset();
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    master::SetupUniverseReplicationRequestPB setup_universe_req;
    setup_universe_req.set_replication_group_id(kReplicationGroupId.ToString());
    string master_addr = producer_cluster()->GetMasterAddresses();
    auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, setup_universe_req.mutable_producer_master_addresses());
    setup_universe_req.add_producer_table_ids("a");
    setup_universe_req.add_producer_table_ids("b");
    setup_universe_req.add_producer_bootstrap_ids("c");
    master::SetupUniverseReplicationResponsePB setup_universe_resp;
    ASSERT_OK(
        master_proxy->SetupUniverseReplication(setup_universe_req, &setup_universe_resp, &rpc));
    ASSERT_TRUE(setup_universe_resp.has_error());
    std::string prefix = "Number of bootstrap ids must be equal to number of tables";
    ASSERT_TRUE(setup_universe_resp.error().status().message().substr(0, prefix.size()) == prefix);
  }

  {
    rpc.Reset();
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    master::SetupUniverseReplicationRequestPB setup_universe_req;
    master::SetupUniverseReplicationResponsePB setup_universe_resp;
    setup_universe_req.set_replication_group_id(kReplicationGroupId.ToString());
    string master_addr = consumer_cluster()->GetMasterAddresses();
    auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, setup_universe_req.mutable_producer_master_addresses());

    setup_universe_req.add_producer_table_ids("prod_table_id_1");
    setup_universe_req.add_producer_table_ids("prod_table_id_2");
    setup_universe_req.add_producer_bootstrap_ids("prod_bootstrap_id_1");
    setup_universe_req.add_producer_bootstrap_ids("prod_bootstrap_id_2");

    ASSERT_OK(
        master_proxy->SetupUniverseReplication(setup_universe_req, &setup_universe_resp, &rpc));
    ASSERT_TRUE(setup_universe_resp.has_error());
    std::string substring = "belongs to the target universe";
    ASSERT_TRUE(setup_universe_resp.error().status().message().find(substring) != string::npos);
  }

  {
    rpc.Reset();
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    master::SetupUniverseReplicationRequestPB setup_universe_req;
    master::SetupUniverseReplicationResponsePB setup_universe_resp;
    auto& cm = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->catalog_manager();
    auto cluster_info = ASSERT_RESULT(cm.GetClusterConfig());
    setup_universe_req.set_replication_group_id(cluster_info.cluster_uuid());

    string master_addr = producer_cluster()->GetMasterAddresses();
    auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, setup_universe_req.mutable_producer_master_addresses());

    setup_universe_req.add_producer_table_ids("prod_table_id_1");
    setup_universe_req.add_producer_table_ids("prod_table_id_2");
    setup_universe_req.add_producer_bootstrap_ids("prod_bootstrap_id_1");
    setup_universe_req.add_producer_bootstrap_ids("prod_bootstrap_id_2");

    ASSERT_OK(
        master_proxy->SetupUniverseReplication(setup_universe_req, &setup_universe_resp, &rpc));
    ASSERT_TRUE(setup_universe_resp.has_error());
    std::string prefix = "The request UUID and cluster UUID are identical.";
    ASSERT_TRUE(setup_universe_resp.error().status().message().substr(0, prefix.size()) == prefix);
  }
}

TEST_P(XClusterTest, YB_DISABLE_TEST(SetupNamespaceReplicationWithBootstrap)) {
  constexpr int kNTabletsPerTable = 1;
  ASSERT_OK(SetUpReplicationWithBootstrap(kNTabletsPerTable));

  // Check pre-command state.
  ASSERT_OK(VerifyNumSnapshots(producer_client(), 0));
  ASSERT_OK(VerifyNumSnapshots(consumer_client(), 0));

  master::SetupNamespaceReplicationWithBootstrapResponsePB resp;
  ASSERT_OK(SendSetupNamespaceReplicationRequest(producer_tables_, &resp));
  ASSERT_FALSE(resp.has_error());

  ASSERT_OK(
      WaitForReplicationBootstrap(consumer_cluster(), consumer_client(), kReplicationGroupId));
  ASSERT_OK(VerifyCdcStateTableEntriesExistForTables(producer_tables_.size(), kNTabletsPerTable));
  ASSERT_OK(VerifyNumSnapshots(producer_client(), /* num_snapshots = */ 1));

  master::NamespaceIdentifierPB producer_namespace;
  producer_tables_.front()->name().SetIntoNamespaceIdentifierPB(&producer_namespace);
  ASSERT_OK(PopulateConsumerTables(producer_namespace));
  ASSERT_OK(VerifyRowsMatch());

  // Verify that universe was setup on consumer.
    master::GetUniverseReplicationResponsePB replication_resp;
  ASSERT_OK(VerifyUniverseReplication(&replication_resp));
  ASSERT_EQ(replication_resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(replication_resp.entry().tables_size(), producer_tables_.size());
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<int32_t>(producer_tables_.size() * kNTabletsPerTable)));

  // Write some data so that we can verify that new records get replicated.
  for (auto& producer_table : producer_tables_) {
    ASSERT_OK(InsertRowsAndVerify(101, 150, producer_table));
  }

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, YB_DISABLE_TEST(SetupNamespaceReplicationWithBootstrapFailToSendSnapshot)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_fail_to_send_create_snapshot_request) = true;
  ASSERT_OK(TestSetupReplicationWithBootstrapFailure());
}

TEST_P(
    XClusterTest,
    YB_DISABLE_TEST(SetupNamespaceReplicationWithBootstrapFailCreateProducerSnapshot)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_fail_create_producer_snapshot) = true;
  ASSERT_OK(TestSetupReplicationWithBootstrapFailure());
}

TEST_P(XClusterTest, YB_DISABLE_TEST(SetupNamespaceReplicationWithBootstrapFailTransferSnapshot)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_fail_snapshot_transfer) = true;
  ASSERT_OK(TestSetupReplicationWithBootstrapFailure());
}

TEST_P(XClusterTest, YB_DISABLE_TEST(SetupNamespaceReplicationWithBootstrapFailRestoreSnapshot)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_fail_restore_consumer_snapshot) = true;
  ASSERT_OK(TestSetupReplicationWithBootstrapFailure());
}

TEST_P(XClusterTest, YB_DISABLE_TEST(SetupNamespaceReplicationWithBootstrapRequestFailures)) {
  constexpr int kNTabletsPerTable = 1;
  ASSERT_OK(SetUpReplicationWithBootstrap(kNTabletsPerTable));

  rpc::RpcController rpc;
  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  master::NamespaceIdentifierPB producer_namespace;
  producer_namespace.set_id("invalid_id");
  producer_namespace.set_name("invalid_name");
  producer_namespace.set_database_type(YQL_DATABASE_UNKNOWN);

  {
    // Don't provide replication ID.
    rpc.Reset();
    master::SetupNamespaceReplicationWithBootstrapRequestPB req;
    master::SetupNamespaceReplicationWithBootstrapResponsePB resp;
    req.set_replication_id("");
    req.mutable_producer_namespace()->CopyFrom(producer_namespace);
    ASSERT_OK(master_proxy->SetupNamespaceReplicationWithBootstrap(req, &resp, &rpc));
    ASSERT_TRUE(resp.has_error());
    std::string substring = "Replication ID must be provided";
    ASSERT_TRUE(resp.error().status().message().find(substring) != string::npos);
  }

  {
    // Don't provide producer master addresses.
    rpc.Reset();
    master::SetupNamespaceReplicationWithBootstrapRequestPB req;
    master::SetupNamespaceReplicationWithBootstrapResponsePB resp;
    req.set_replication_id(kReplicationGroupId.ToString());
    req.mutable_producer_namespace()->CopyFrom(producer_namespace);
    ASSERT_OK(master_proxy->SetupNamespaceReplicationWithBootstrap(req, &resp, &rpc));
    ASSERT_TRUE(resp.has_error());
    std::string substring = "Producer master address must be provided";
    ASSERT_TRUE(resp.error().status().message().find(substring) != string::npos);
  }

  {
    // Provide replication name identical to consumer universe UUID.
    rpc.Reset();
    master::SetupNamespaceReplicationWithBootstrapRequestPB req;
    master::SetupNamespaceReplicationWithBootstrapResponsePB resp;

    auto& cm = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->catalog_manager();
    auto cluster_info = ASSERT_RESULT(cm.GetClusterConfig());
    req.set_replication_id(cluster_info.cluster_uuid());
    req.mutable_producer_namespace()->CopyFrom(producer_namespace);
    string master_addr = producer_cluster()->GetMasterAddresses();
    auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, req.mutable_producer_master_addresses());

    ASSERT_OK(master_proxy->SetupNamespaceReplicationWithBootstrap(req, &resp, &rpc));
    ASSERT_TRUE(resp.has_error());
    std::string substring = "Replication name cannot be the target universe UUID";
    ASSERT_TRUE(resp.error().status().message().find(substring) != string::npos);
  }

  {
    // Provide duplicate addresses between consumer & producer universes.
    rpc.Reset();
    master::SetupNamespaceReplicationWithBootstrapRequestPB req;
    master::SetupNamespaceReplicationWithBootstrapResponsePB resp;

    req.set_replication_id(kReplicationGroupId.ToString());
    req.mutable_producer_namespace()->CopyFrom(producer_namespace);
    string master_addr = consumer_cluster()->GetMasterAddresses();
    auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, req.mutable_producer_master_addresses());

    ASSERT_OK(master_proxy->SetupNamespaceReplicationWithBootstrap(req, &resp, &rpc));
    ASSERT_TRUE(resp.has_error());
    std::string substring = "belongs to the target universe";
    ASSERT_TRUE(resp.error().status().message().find(substring) != string::npos);
  }

  {
    // Provide invalid namespace identifier.
    rpc.Reset();
    master::SetupNamespaceReplicationWithBootstrapRequestPB req;
    master::SetupNamespaceReplicationWithBootstrapResponsePB resp;

    req.set_replication_id(kReplicationGroupId.ToString());
    req.mutable_producer_namespace()->CopyFrom(producer_namespace);
    string master_addr = producer_cluster()->GetMasterAddresses();
    auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, req.mutable_producer_master_addresses());

    ASSERT_OK(master_proxy->SetupNamespaceReplicationWithBootstrap(req, &resp, &rpc));
    ASSERT_TRUE(resp.has_error());
  }
}

TEST_P(XClusterTest, SetupUniverseReplicationWithProducerBootstrapId) {
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(tables_vector, 3));

  // 1. Write some data so that we can verify that only new records get replicated
  for (auto& producer_table : producer_tables_) {
    ASSERT_OK(InsertRowsInProducer(0, 100, producer_table));
  }

  auto bootstrap_ids =
      ASSERT_RESULT(BootstrapProducer(producer_cluster(), producer_client(), producer_tables_));

  for (size_t i = 0; i < bootstrap_ids.size(); i++) {
    LOG(INFO) << "Got bootstrap id " << bootstrap_ids[i] << " for table "
              << producer_tables_[i]->name().table_name();
  }

  ASSERT_OK(VerifyCdcStateTableEntriesExistForTables(producer_tables_.size(), kNTabletsPerTable));

  // 2. Setup replication.
  ASSERT_OK(SetupUniverseReplication(producer_tables_, bootstrap_ids));

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(&get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<int32_t>(tables_vector.size() * kNTabletsPerTable)));

  // 4. Write more data.
  for (auto& producer_table : producer_tables_) {
    ASSERT_OK(InsertRowsInProducer(1000, 1005, producer_table));
  }

  // 5. Verify that only new writes get replicated to consumer since we bootstrapped the producer
  // after we had already written some data, therefore the old data (whatever was there before we
  // bootstrapped the producer) should not be replicated.
  auto data_replicated_correctly = [&]() {
    for (const auto& consumer_table : consumer_tables_) {
      LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
      std::vector<std::string> expected_results;
      for (int key = 1000; key < 1005; key++) {
        expected_results.emplace_back("{ int32:" + std::to_string(key) + " }");
      }
      std::sort(expected_results.begin(), expected_results.end());

      auto consumer_results = ScanTableToStrings(consumer_table->name(), consumer_client());
      std::sort(consumer_results.begin(), consumer_results.end());

      if (expected_results.size() != consumer_results.size()) {
        return false;
      }

      for (size_t idx = 0; idx < expected_results.size(); idx++) {
        if (expected_results[idx] != consumer_results[idx]) {
          return false;
        }
      }
    }
    return true;
  };
  ASSERT_OK(WaitFor(
      [&]() { return data_replicated_correctly(); }, MonoDelta::FromSeconds(20),
      "IsDataReplicatedCorrectly"));
}

// Test for #2250 to verify that replication for tables with the same prefix gets set up correctly.
TEST_P(XClusterTest, SetupUniverseReplicationMultipleTables) {
  // Setup the two clusters without any tables.
  ASSERT_OK(SetUpWithParams({}, 1));

  // Create tables with the same prefix.
  std::string table_names[2] = {"table", "table_index"};

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  for (int i = 0; i < 2; i++) {
    auto t = ASSERT_RESULT(CreateTable(producer_client(), namespace_name, table_names[i], 3));
    std::shared_ptr<client::YBTable> producer_table;
    ASSERT_OK(producer_client()->OpenTable(t, &producer_table));
    producer_tables.push_back(producer_table);
  }

  for (int i = 0; i < 2; i++) {
    ASSERT_RESULT(CreateTable(consumer_client(), namespace_name, table_names[i], 3));
  }

  // Setup universe replication on both these tables.
  ASSERT_OK(SetupUniverseReplication(producer_tables));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), producer_tables.size());
  for (uint32_t i = 0; i < producer_tables.size(); i++) {
    ASSERT_EQ(resp.entry().tables(i), producer_tables[i]->id());
  }

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, SetupUniverseReplicationLargeTableCount) {
  if (IsSanitizer()) {
    LOG(INFO) << "Skipping slow test";
    return;
  }

  // Setup the two clusters without any tables.
  ASSERT_OK(SetUpWithParams({}, 1));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  // Create a large number of tables to test the performance of setup_replication.
  int table_count = 2;
  int amplification[2] = {1, 5};
  MonoDelta setup_latency[2];
  std::string table_prefix = "stress_table_";
  bool passed_test = false;

  for (int retries = 0; retries < 3 && !passed_test; ++retries) {
    for (int a : {0, 1}) {
      std::vector<std::shared_ptr<client::YBTable>> producer_tables;
      for (int i = 0; i < table_count * amplification[a]; i++) {
        std::string cur_table =
            table_prefix + std::to_string(amplification[a]) + "-" + std::to_string(i);
        ASSERT_RESULT(CreateTable(consumer_client(), namespace_name, cur_table, 3));
        auto t = ASSERT_RESULT(CreateTable(producer_client(), namespace_name, cur_table, 3));
        std::shared_ptr<client::YBTable> producer_table;
        ASSERT_OK(producer_client()->OpenTable(t, &producer_table));
        producer_tables.push_back(producer_table);
      }

      // Add delays to all rpc calls to simulate live environment and ensure the test is IO bound.
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_yb_inbound_big_calls_parse_delay_ms) = 200;
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_throttle_threshold_bytes) = 200;

      auto start_time = CoarseMonoClock::Now();

      // Setup universe replication on all tables.
      ASSERT_OK(SetupUniverseReplication(producer_tables));

      // Verify that universe was setup on consumer.
      master::GetUniverseReplicationResponsePB resp;
      ASSERT_OK(VerifyUniverseReplication(&resp));
      ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
      ASSERT_EQ(resp.entry().tables_size(), producer_tables.size());
      for (uint32_t i = 0; i < producer_tables.size(); i++) {
        ASSERT_EQ(resp.entry().tables(i), producer_tables[i]->id());
      }

      setup_latency[a] = CoarseMonoClock::Now() - start_time;
      LOG(INFO) << "SetupReplication [" << a << "] took: " << setup_latency[a].ToSeconds() << "s";

      // Remove delays for cleanup and next setup.
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_yb_inbound_big_calls_parse_delay_ms) = 0;

      ASSERT_OK(DeleteUniverseReplication());
    }

    // We increased our table count by 5x, but we shouldn't have a linear latency increase.
    passed_test = (setup_latency[1] < setup_latency[0] * 3);
  }

  ASSERT_TRUE(passed_test);
}

TEST_P(XClusterTest, SetupUniverseReplicationBootstrapStateUpdate) {
  constexpr int kNTabletsPerTable = 3;
  constexpr int kNumTables = 5;
  constexpr int kReplicationFactor = 3;
  std::vector<uint32_t> tables_vector(kNumTables);
  for (size_t i = 0; i < kNumTables; i++) {
    tables_vector[i] = kNTabletsPerTable;
  }
  ASSERT_OK(SetUpWithParams(tables_vector, kReplicationFactor));

  auto client = ASSERT_RESULT(consumer_cluster()->CreateClient());
  auto producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
      &client->proxy_cache(),
      HostPort::FromBoundEndpoint(producer_cluster()->mini_tablet_server(0)->bound_rpc_addr()));
  auto consumer_master_proxy = std::make_unique<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
  auto producer_master_proxy = std::make_unique<master::MasterReplicationProxy>(
      &producer_client()->proxy_cache(),
      ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  // Bootstrap producer tables
  auto bootstrap_ids =
      ASSERT_RESULT(BootstrapProducer(producer_cluster(), producer_client(), producer_tables_));

  // Set up replication with bootstrap IDs.
  ASSERT_OK(SetupUniverseReplication(producer_tables_, bootstrap_ids));

  // Verify that all streams are set to ACTIVE state.
  {
    string state_active = master::SysCDCStreamEntryPB::State_Name(
        master::SysCDCStreamEntryPB_State::SysCDCStreamEntryPB_State_ACTIVE);
    master::ListCDCStreamsRequestPB list_req;
    master::ListCDCStreamsResponsePB list_resp;
    list_req.set_id_type(yb::master::IdTypePB::TABLE_ID);

    ASSERT_OK(LoggedWaitFor(
        [&]() {
          rpc::RpcController rpc;
          rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
          return producer_master_proxy->ListCDCStreams(list_req, &list_resp, &rpc).ok() &&
                 !list_resp.has_error() &&
                 list_resp.streams_size() == kNumTables;  // One stream per table.
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Verify stream creation"));

    std::vector<string> stream_states;
    for (int i = 0; i < list_resp.streams_size(); i++) {
      auto options = list_resp.streams(i).options();
      for (const auto& option : options) {
        if (option.has_key() && option.key() == "state" && option.has_value()) {
          stream_states.push_back(option.value());
        }
      }
    }

    ASSERT_TRUE(
        stream_states.size() == kNumTables &&
        std::all_of(stream_states.begin(), stream_states.end(), [&](string state) {
          return state == state_active;
        }));
  }

  // Verify that replication is working.
  for (int i = 0; i < kNumTables; i++) {
    ASSERT_OK(InsertRowsAndVerify(0, 5 * (i + 1), producer_tables_[i]));
  }

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, BootstrapAndSetupLargeTableCount) {
  if (IsSanitizer()) {
    LOG(INFO) << "Skipping slow test";
    return;
  }

  // Main variables that we tweak in performance profiling
  int table_count = 2;
  int tablet_count = 3;
  int tserver_count = 1;
  uint64_t rpc_delay_ms = 200;

  // Setup the two clusters without any tables.
  int replication_factor = 3;
  ASSERT_OK(SetUpWithParams({}, {}, replication_factor, 1, tserver_count));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  // Create a medium, then large number of tables to test the performance of our CLI commands.
  int amplification[2] = {1, 5};
  MonoDelta is_bootstrap_required_latency[2];
  MonoDelta bootstrap_latency[2];
  MonoDelta setup_latency[2];
  std::string table_prefix = "stress_table_";
  bool passed_test = false;

  for (int retries = 0; retries < 1 && !passed_test; ++retries) {
    for (int a : {0, 1}) {
      YBTables producer_tables;
      for (int i = 0; i < table_count * amplification[a]; i++) {
        std::string cur_table =
            table_prefix + std::to_string(amplification[a]) + "-" + std::to_string(i);
        ASSERT_RESULT(CreateTable(consumer_client(), namespace_name, cur_table, tablet_count));
        auto t =
            ASSERT_RESULT(CreateTable(producer_client(), namespace_name, cur_table, tablet_count));
        std::shared_ptr<client::YBTable> producer_table;
        ASSERT_OK(producer_client()->OpenTable(t, &producer_table));
        producer_tables.push_back(producer_table);
      }

      ASSERT_OK(WaitForLoadBalancersToStabilize());

      // Add delays to all rpc calls to simulate live environment and ensure the test is IO bound.
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_yb_inbound_big_calls_parse_delay_ms) = rpc_delay_ms;
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_throttle_threshold_bytes) = 200;

      // Performance test of IsBootstrapRequired.
      {
        auto start_time = CoarseMonoClock::Now();

        std::vector<TableId> table_ids;
        for (const auto& producer_table : producer_tables) {
          table_ids.emplace_back(producer_table->id());
        }
        ASSERT_FALSE(ASSERT_RESULT(producer_client()->IsBootstrapRequired(table_ids)));

        is_bootstrap_required_latency[a] = CoarseMonoClock::Now() - start_time;
        LOG(INFO) << "IsBootstrapRequired [" << a
                  << "] took: " << is_bootstrap_required_latency[a].ToSeconds() << "s";
      }

      // Performance test of BootstrapProducer.
      auto start_time = CoarseMonoClock::Now();

      auto bootstrap_ids =
          ASSERT_RESULT(BootstrapProducer(producer_cluster(), producer_client(), producer_tables));

      bootstrap_latency[a] = CoarseMonoClock::Now() - start_time;
      LOG(INFO) << "BootstrapProducer [" << a << "] took: " << bootstrap_latency[a].ToSeconds()
                << "s";

      // Performance test of SetupReplication, with Bootstrap IDs.
      start_time = CoarseMonoClock::Now();

      ASSERT_OK(SetupUniverseReplication(

          producer_tables, bootstrap_ids));
      setup_latency[a] = CoarseMonoClock::Now() - start_time;

      LOG(INFO) << "SetupReplication [" << a << "] took: " << setup_latency[a].ToSeconds() << "s";

      // Remove delays for cleanup and next setup.
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_yb_inbound_big_calls_parse_delay_ms) = 0;

      ASSERT_OK(DeleteUniverseReplication());
    }
    // We increased our table count by 5x, but we shouldn't have a linear latency increase.
    // ASSERT_LT(bootstrap_latency[1], bootstrap_latency[0] * 5);
    passed_test = (setup_latency[1] < setup_latency[0] * 3);
  }
  ASSERT_TRUE(passed_test);
}

TEST_P(XClusterTest, PollWithConsumerRestart) {
  // Avoid long delays with node failures so we can run with more aggressive test timing
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_failure_delay_exponent) = 7;  // 2^7 == 128ms

  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  ASSERT_OK(SetUpWithParams({4}, replication_factor));

  ASSERT_OK(SetupReplication());

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(4));

  consumer_cluster()->mini_tablet_server(0)->Shutdown();

  // After shutting down a single consumer node, the other consumers should pick up the slack.
  if (replication_factor > 1) {
    ASSERT_OK(CorrectlyPollingAllTablets(4));
  }

  ASSERT_OK(
      consumer_cluster()->mini_tablet_server(0)->Start(tserver::WaitTabletsBootstrapped::kFalse));

  // After restarting the node.
  ASSERT_OK(CorrectlyPollingAllTablets(4));

  ASSERT_OK(consumer_cluster()->RestartSync());

  // After consumer cluster restart.
  ASSERT_OK(CorrectlyPollingAllTablets(4));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, PollWithProducerNodesRestart) {
  // Avoid long delays with node failures so we can run with more aggressive test timing
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_failure_delay_exponent) = 7;  // 2^7 == 128ms

  uint32_t replication_factor = 3, tablet_count = 4, master_count = 3;
  ASSERT_OK(SetUpWithParams({tablet_count}, {tablet_count}, replication_factor, master_count));
  ASSERT_OK(
      SetupUniverseReplication(producer_tables_, {LeaderOnly::kFalse, Transactional::kFalse}));

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(4));

  // Stop the Master and wait for failover.
  LOG(INFO) << "Failover to new Master";
  MiniMaster* old_master = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster());
  ASSERT_OK(old_master->WaitUntilCatalogManagerIsLeaderAndReadyForTests());
  ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->Shutdown();
  MiniMaster* new_master = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster());
  ASSERT_NE(nullptr, new_master);
  ASSERT_NE(old_master, new_master);
  ASSERT_OK(producer_cluster()->WaitForAllTabletServers());

  // Stop a TServer on the Producer after failing its master.
  producer_cluster()->mini_tablet_server(0)->Shutdown();
  // This Verifies:
  // 1. Consumer successfully transitions over to using the new master for Tablet lookup.
  // 2. Consumer cluster has rebalanced all the CDC Pollers
  ASSERT_OK(CorrectlyPollingAllTablets(4));
  ASSERT_OK(InsertRowsAndVerify(0, 5));

  // Restart the Producer TServer and verify that rebalancing happens.
  ASSERT_OK(old_master->Start());
  ASSERT_OK(
      producer_cluster()->mini_tablet_server(0)->Start(tserver::WaitTabletsBootstrapped::kFalse));
  ASSERT_OK(CorrectlyPollingAllTablets(4));
  ASSERT_OK(InsertRowsAndVerify(6, 10));

  // Cleanup.
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, PollWithProducerClusterRestart) {
  // Avoid long delays with node failures so we can run with more aggressive test timing
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_failure_delay_exponent) = 7;  // 2^7 == 128ms

  uint32_t replication_factor = 3, tablet_count = 4;
  ASSERT_OK(SetUpWithParams({tablet_count}, replication_factor));

  ASSERT_OK(SetupReplication());

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(4));

  // Restart the ENTIRE Producer cluster.
  ASSERT_OK(producer_cluster()->RestartSync());

  // After producer cluster restart.
  ASSERT_OK(CorrectlyPollingAllTablets(4));
  ASSERT_OK(InsertRowsAndVerify(0, 5));

  // Cleanup.
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, PollAndObserveIdleDampening) {
  uint32_t replication_factor = 3, tablet_count = 1, master_count = 1;
  ASSERT_OK(SetUpWithParams({tablet_count}, {tablet_count}, replication_factor, master_count));
  ASSERT_OK(
      SetupUniverseReplication({producer_table_}, {LeaderOnly::kFalse, Transactional::kFalse}));

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(1));

  // Write some Info and query GetChanges to setup the XClusterTabletMetrics.
  ASSERT_OK(InsertRowsAndVerify(0, 5));

  /*****************************************************************
   * Find the CDC Tablet Metrics, which we will use for this test. *
   *****************************************************************/
  // Find the stream.
  master::ListCDCStreamsResponsePB stream_resp;
  ASSERT_OK(GetCDCStreamForTable(producer_table_->id(), &stream_resp));
  ASSERT_EQ(stream_resp.streams_size(), 1);
  ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), producer_table_->id());
  auto stream_id = ASSERT_RESULT(xrepl::StreamId::FromString(stream_resp.streams(0).stream_id()));

  // Find the tablet id for the stream.
  TabletId tablet_id;
  {
    yb::cdc::ListTabletsRequestPB tablets_req;
    yb::cdc::ListTabletsResponsePB tablets_resp;
    rpc::RpcController rpc;
    tablets_req.set_stream_id(stream_id.ToString());

    auto producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
        &producer_client()->proxy_cache(),
        HostPort::FromBoundEndpoint(producer_cluster()->mini_tablet_server(0)->bound_rpc_addr()));
    ASSERT_OK(producer_cdc_proxy->ListTablets(tablets_req, &tablets_resp, &rpc));
    ASSERT_FALSE(tablets_resp.has_error());
    ASSERT_EQ(tablets_resp.tablets_size(), 1);
    tablet_id = tablets_resp.tablets(0).tablet_id();
  }

  // Find the TServer that is hosting this tablet.
  tserver::TabletServer* cdc_ts = nullptr;
  std::string ts_uuid;
  std::mutex data_mutex;
  {
    ASSERT_OK(WaitFor(
        [this, &tablet_id, &table = producer_table_, &ts_uuid, &data_mutex] {
          producer_client()->LookupTabletById(
              tablet_id, table,
              // TODO(tablet splitting + xCluster): After splitting integration is working (+
              // metrics support), then set this to kTrue.
              master::IncludeInactive::kFalse, master::IncludeDeleted::kFalse,
              CoarseMonoClock::Now() + MonoDelta::FromSeconds(3),
              [&ts_uuid, &data_mutex](const Result<client::internal::RemoteTabletPtr>& result) {
                if (result.ok()) {
                  std::lock_guard l(data_mutex);
                  ts_uuid = (*result)->LeaderTServer()->permanent_uuid();
                }
              },
              client::UseCache::kFalse);
          std::lock_guard l(data_mutex);
          return !ts_uuid.empty();
        },
        MonoDelta::FromSeconds(10), "Get TS for Tablet"));

    for (auto ts : producer_cluster()->mini_tablet_servers()) {
      if (ts->server()->permanent_uuid() == ts_uuid) {
        cdc_ts = ts->server();
        break;
      }
    }
  }
  ASSERT_ONLY_NOTNULL(cdc_ts);

  // Find the CDCTabletMetric associated with the above pair.
  auto cdc_service = dynamic_cast<cdc::CDCServiceImpl*>(
      cdc_ts->rpc_server()->TEST_service_pool("yb.cdc.CDCService")->TEST_get_service().get());
  auto metrics = ASSERT_RESULT(GetXClusterTabletMetrics(*cdc_service, tablet_id, stream_id));

  /***********************************
   * Setup Complete.  Starting test. *
   ***********************************/
  // Log the first heartbeat count for baseline
  auto first_heartbeat_count = metrics->rpc_heartbeats_responded->value();
  LOG(INFO) << "first_heartbeat_count = " << first_heartbeat_count;

  // Write some Info to the producer, which should be consumed quickly by GetChanges.
  ASSERT_OK(InsertRowsAndVerify(6, 10));

  // Sleep for the idle timeout.
  SleepFor(MonoDelta::FromMilliseconds(FLAGS_async_replication_idle_delay_ms));
  auto active_heartbeat_count = metrics->rpc_heartbeats_responded->value();
  LOG(INFO) << "active_heartbeat_count  = " << active_heartbeat_count;
  // The new heartbeat count should be at least 3 (idle_wait)
  ASSERT_GE(active_heartbeat_count - first_heartbeat_count, FLAGS_async_replication_max_idle_wait);

  // Now, wait past update request frequency, so we should be using idle timing.
  auto multiplier = 2;
  SleepFor(MonoDelta::FromMilliseconds(FLAGS_async_replication_idle_delay_ms * multiplier));
  auto idle_heartbeat_count = metrics->rpc_heartbeats_responded->value();
  ASSERT_LE(idle_heartbeat_count - active_heartbeat_count, multiplier + 1 /*allow subtle race*/);
  LOG(INFO) << "idle_heartbeat_count = " << idle_heartbeat_count;

  // Write some more data to the producer and call GetChanges with some real data.
  ASSERT_OK(InsertRowsAndVerify(11, 15));

  // Sleep for the idle timeout and Verify that the idle behavior ended now that we have new data.
  SleepFor(MonoDelta::FromMilliseconds(FLAGS_async_replication_idle_delay_ms));
  active_heartbeat_count = metrics->rpc_heartbeats_responded->value();
  LOG(INFO) << "active_heartbeat_count  = " << active_heartbeat_count;
  // The new heartbeat count should be at least 3 (idle_wait)
  ASSERT_GE(active_heartbeat_count - idle_heartbeat_count, FLAGS_async_replication_max_idle_wait);

  // Cleanup.
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, ApplyOperations) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  // Use just one tablet here to more easily catch lower-level write issues with this test.
  ASSERT_OK(SetUpWithParams({1}, replication_factor));

  ASSERT_OK(SetupReplication());

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(1));

  ASSERT_OK(InsertRowsInProducer(0, 5));

  // Check that all tablets continue to be polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(1));

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyRowsMatch());

  ASSERT_OK(DeleteUniverseReplication());
}

class XClusterTestTransactionalOnly : public XClusterTestNoParam {
  virtual bool UseTransactionalTables() override { return true; }
};

TEST_F(XClusterTestTransactionalOnly, SetupUniverseReplicationWithTLSEncryption) {
  ASSERT_OK(TestSetupUniverseReplication(EnableTLSEncryption::kTrue, Transactional::kTrue));
}

TEST_F(XClusterTestTransactionalOnly, FailedSetupSystemUniverseReplication) {
  // Make sure we cannot setup replication on System tables like the transaction status table.
  constexpr int kNumTablets = 1;
  ASSERT_OK(SetUpWithParams({kNumTablets}, 1 /* replication_factor */));

  static const client::YBTableName transaction_table_name(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
  std::shared_ptr<client::YBTable> producer_transaction_table;
  ASSERT_OK(producer_client()->OpenTable(transaction_table_name, &producer_transaction_table));

  ASSERT_NOK(SetupUniverseReplication(
      {producer_table_, producer_transaction_table}, {LeaderOnly::kTrue, Transactional::kFalse}));

  ASSERT_NOK(SetupUniverseReplication(
      {producer_table_, producer_transaction_table}, {LeaderOnly::kTrue, Transactional::kTrue}));
}

TEST_F(XClusterTestTransactionalOnly, FailedSetupMissingTable) {
  // Make sure we cannot setup replication when consumer is missing a table.
  constexpr int kNumTablets = 1;
  ASSERT_OK(SetUpWithParams({kNumTablets}, 1 /* replication_factor */));
  auto extra_producer_table_name =
      ASSERT_RESULT(CreateTable(producer_client(), namespace_name, "extra_table_", kNumTablets));

  std::shared_ptr<client::YBTable> extra_producer_table;
  ASSERT_OK(producer_client()->OpenTable(extra_producer_table_name, &extra_producer_table));

  auto status = SetupUniverseReplication({producer_table_, extra_producer_table});
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "Could not find matching table for yugabyte.extra_table_");
}

TEST_F(XClusterTestTransactionalOnly, ApplyOperationsWithTransactions) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  ASSERT_OK(SetUpWithParams({2}, replication_factor));

  ASSERT_OK(SetupReplication());

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(2));

  // Write some transactional rows.
  ASSERT_OK(InsertTransactionalBatchOnProducer(0, 5));

  // Write some non-transactional rows.
  ASSERT_OK(InsertRowsInProducer(6, 10));

  // Check that all tablets continue to be polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(2));

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyRowsMatch());

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterTestTransactionalOnly, UpdateWithinTransaction) {
  constexpr int kNumTablets = 1;
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  ASSERT_OK(SetUpWithParams({kNumTablets}, replication_factor));

  ASSERT_OK(SetupReplication());

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(kNumTablets));

  auto [session, txn] =
      ASSERT_RESULT(CreateSessionWithTransaction(producer_client(), producer_txn_mgr()));
  ASSERT_OK(InsertIntentsOnProducer(session, 1, 5));
  ASSERT_OK(InsertIntentsOnProducer(session, 1, 5));
  ASSERT_OK(txn->CommitFuture().get());

  session->SetTransaction(nullptr);
  client::TableHandle table_handle;
  ASSERT_OK(table_handle.Open(producer_table_->name(), producer_client()));
  auto op = table_handle.NewInsertOp();
  auto req = op->mutable_request();
  QLAddInt32HashValue(req, 0);
  ASSERT_OK(session->TEST_ApplyAndFlush(op));

  ASSERT_OK(VerifyRowsMatch());

  // Check that all tablets continue to be polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(kNumTablets));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterTestTransactionalOnly, TransactionsWithRestart) {
  ASSERT_OK(SetUpWithParams({2}, 3));

  ASSERT_OK(SetupReplication());

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(2));

  auto txn = ASSERT_RESULT(CreateSessionWithTransaction(producer_client(), producer_txn_mgr()));
  // Write some transactional rows.
  ASSERT_OK(InsertTransactionalBatchOnProducer(0, 5));

  ASSERT_OK(InsertRowsInProducer(6, 10));

  ASSERT_OK(CorrectlyPollingAllTablets(2));
  std::this_thread::sleep_for(5s);
  ASSERT_OK(
      consumer_cluster()->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));
  LOG(INFO) << "Restart";
  ASSERT_OK(consumer_cluster()->RestartSync());
  std::this_thread::sleep_for(5s);
  LOG(INFO) << "Commit";
  ASSERT_OK(txn.second->CommitFuture().get());

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyRowsMatch());

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterTestTransactionalOnly, MultipleTransactions) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  ASSERT_OK(SetUpWithParams({1}, replication_factor));

  ASSERT_OK(SetupReplication());

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(1));

  auto [session1, txn1] =
      ASSERT_RESULT(CreateSessionWithTransaction(producer_client(), producer_txn_mgr()));
  auto [session2, txn2] =
      ASSERT_RESULT(CreateSessionWithTransaction(producer_client(), producer_txn_mgr()));

  ASSERT_OK(InsertIntentsOnProducer(session1, 0, 5));
  ASSERT_OK(InsertIntentsOnProducer(session1, 5, 10));
  ASSERT_OK(InsertIntentsOnProducer(session2, 10, 15));
  ASSERT_OK(InsertIntentsOnProducer(session2, 10, 20));

  ASSERT_OK(WaitFor(
      [&]() { return CountIntents(consumer_cluster()) > 0; }, MonoDelta::FromSeconds(kRpcTimeout),
      "Consumer cluster replicated intents"));

  // Make sure that none of the intents replicated have been committed.
  auto consumer_results = ScanTableToStrings(consumer_table_->name(), consumer_client());
  ASSERT_EQ(consumer_results.size(), 0);

  ASSERT_OK(txn1->CommitFuture().get());
  ASSERT_OK(VerifyRowsMatch());

  ASSERT_OK(txn2->CommitFuture().get());
  ASSERT_OK(VerifyRowsMatch());
  ASSERT_OK(WaitFor(
      [&]() { return CountIntents(consumer_cluster()) == 0; }, MonoDelta::FromSeconds(kRpcTimeout),
      "Consumer cluster cleaned up intents"));
}

TEST_F(XClusterTestTransactionalOnly, CleanupAbortedTransactions) {
  static const int kNumRecordsPerBatch = 5;
  const uint32_t replication_factor = NonTsanVsTsan(3, 1);
  ASSERT_OK(SetUpWithParams({1 /* num_consumer_tablets */}, replication_factor));

  ASSERT_OK(SetupReplication());

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(1 /* num_producer_tablets */));
  auto [session, txn] =
      ASSERT_RESULT(CreateSessionWithTransaction(producer_client(), producer_txn_mgr()));
  ASSERT_OK(InsertIntentsOnProducer(session, 0, kNumRecordsPerBatch));
  // Wait for records to be replicated.
  ASSERT_OK(WaitFor(
      [&]() {
        return CountIntents(consumer_cluster()) == kNumRecordsPerBatch * replication_factor;
      },
      MonoDelta::FromSeconds(kRpcTimeout), "Consumer cluster created intents"));
  ASSERT_OK(consumer_cluster()->FlushTablets());
  // Then, set timeout to 0 and make sure we do cleanup on the next compaction.
  SetAtomicFlag(0, &FLAGS_external_intent_cleanup_secs);
  ASSERT_OK(InsertIntentsOnProducer(session, kNumRecordsPerBatch, kNumRecordsPerBatch * 2));
  // Wait for records to be replicated.
  ASSERT_OK(WaitFor(
      [&]() {
        return CountIntents(consumer_cluster()) == 2 * kNumRecordsPerBatch * replication_factor;
      },
      MonoDelta::FromSeconds(kRpcTimeout), "Consumer cluster created intents"));
  ASSERT_OK(consumer_cluster()->CompactTablets());
  ASSERT_OK(WaitFor(
      [&]() { return CountIntents(consumer_cluster()) == 0; }, MonoDelta::FromSeconds(kRpcTimeout),
      "Consumer cluster cleaned up intents"));
  txn->Abort();
}

// Make sure when we compact a tablet, we retain intents.
TEST_F(XClusterTestTransactionalOnly, NoCleanupOfTransactionsInFlushedFiles) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  ASSERT_OK(SetUpWithParams({1}, replication_factor));

  ASSERT_OK(SetupReplication());

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(1));
  auto [session, txn] =
      ASSERT_RESULT(CreateSessionWithTransaction(producer_client(), producer_txn_mgr()));
  ASSERT_OK(InsertIntentsOnProducer(session, 0, 5));
  auto consumer_results = ScanTableToStrings(consumer_table_->name(), consumer_client());
  ASSERT_EQ(consumer_results.size(), 0);
  ASSERT_OK(consumer_cluster()->FlushTablets());
  ASSERT_OK(InsertIntentsOnProducer(session, 5, 10));
  ASSERT_OK(consumer_cluster()->FlushTablets());
  ASSERT_OK(consumer_cluster()->CompactTablets());
  // Wait for 5 seconds to make sure background CleanupIntents thread doesn't cleanup intents on the
  // consumer.
  SleepFor(MonoDelta::FromSeconds(5));
  ASSERT_OK(txn->CommitFuture().get());
  ASSERT_OK(VerifyRowsMatch());
  ASSERT_OK(WaitFor(
      [&]() { return CountIntents(consumer_cluster()) == 0; }, MonoDelta::FromSeconds(kRpcTimeout),
      "Consumer cluster cleaned up intents"));
}

TEST_F(XClusterTestTransactionalOnly, ManyToOneTabletMapping) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  ASSERT_OK(SetUpWithParams({2}, {5}, replication_factor));

  ASSERT_OK(SetupReplication());
  ASSERT_OK(CorrectlyPollingAllTablets(5));

  ASSERT_OK(InsertTransactionalBatchOnProducer(0, 100));
  ASSERT_OK(VerifyRowsMatch());
}

TEST_F(XClusterTestTransactionalOnly, OneToManyTabletMapping) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  ASSERT_OK(SetUpWithParams({5}, {2}, replication_factor));

  ASSERT_OK(SetupReplication());

  ASSERT_OK(CorrectlyPollingAllTablets(2));
  ASSERT_OK(InsertTransactionalBatchOnProducer(0, 50));
  ASSERT_OK(VerifyRowsMatch());
}

TEST_F(XClusterTestTransactionalOnly, WithBootstrap) {
  constexpr int kNumTablets = 1;
  ASSERT_OK(SetUpWithParams({kNumTablets}, 1 /* replication_factor */));

  // 1. Write batch_size rows transactionally. This batch should never get replicated as we only
  // bootstrap after this.
  const uint32 batch_size = 10;
  ASSERT_OK(InsertTransactionalBatchOnProducer(0, batch_size));

  // 2. Bootstrap user table.
  auto bootstrap_ids =
      ASSERT_RESULT(BootstrapProducer(producer_cluster(), producer_client(), producer_tables_));

  const auto& bootstrap_id = bootstrap_ids.front();
  LOG(INFO) << "Got bootstrap id " << bootstrap_id << " for table "
            << producer_table_->name().table_name();

  // 3. Write batch_size more rows transactionally.
  ASSERT_OK(InsertTransactionalBatchOnProducer(batch_size, 2 * batch_size));

  // 4. Flush the table and run log GC.
  ASSERT_OK(FlushProducerTabletsAndGCLog());

  // 5. Setup replication.
  ASSERT_OK(SetupUniverseReplication(
      producer_tables_, bootstrap_ids, {LeaderOnly::kTrue, Transactional::kTrue}));
  master::GetUniverseReplicationResponsePB verify_repl_resp;
  ASSERT_OK(VerifyUniverseReplication(&verify_repl_resp));

  // 6. Verify we got the rows from step 3 but not 1.
  ASSERT_OK(VerifyNumRecordsOnConsumer(batch_size));
  ASSERT_NOK(VerifyRowsMatch());

  // 7. Write some more rows and make sure they are replicated.
  ASSERT_OK(InsertTransactionalBatchOnProducer(2 * batch_size, 3 * batch_size));
  ASSERT_OK(VerifyNumRecordsOnConsumer(2 * batch_size));
}

TEST_P(XClusterTest, TestExternalWriteHybridTime) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  ASSERT_OK(SetUpWithParams({2}, replication_factor));

  ASSERT_OK(SetupReplication());

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(2));

  // Write 2 rows.
  ASSERT_OK(InsertRowsAndVerify(0, 2));

  // Delete 1 record.
  ASSERT_OK(DeleteRows(0, 1));

  // Ensure that record is deleted on both universes.
  ASSERT_OK(VerifyRowsMatch());

  // Delete 2nd record but replicate at a low timestamp (timestamp lower than insertion timestamp).
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_write_hybrid_time) = true;
  ASSERT_OK(DeleteRows(1, 2));

  // Verify that record exists on consumer universe, but is deleted from producer universe.
  ASSERT_OK(VerifyNumRecordsOnProducer(0));
  ASSERT_OK(VerifyNumRecordsOnConsumer(1));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, BiDirectionalWrites) {
  ASSERT_OK(SetUpWithParams({2}, 1));

  ASSERT_OK(SetupReplication());

  ASSERT_OK(SetupReverseUniverseReplication(consumer_tables_));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(2));
  ASSERT_OK(CorrectlyPollingAllTablets(producer_cluster(), 2));

  // Write non-conflicting rows on both clusters.
  ASSERT_OK(InsertRowsInProducer(0, 5));
  ASSERT_OK(InsertRowsInConsumer(5, 10));

  // Ensure that records are the same on both clusters.
  ASSERT_OK(VerifyRowsMatch());
  // Ensure that both universes have all 10 records.
  ASSERT_OK(VerifyNumRecordsOnProducer(10));

  // Write conflicting records on both clusters (1 clusters adds key, another deletes key).
  ASSERT_OK(InsertRowsInConsumer(0, 5));
  ASSERT_OK(DeleteRows(5, 10));

  // Ensure that same records exist on both universes.
  ASSERT_OK(VerifyRowsMatch());

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, BiDirectionalWritesWithLargeBatches) {
  // Simulate large batches by dropping FLAGS_consensus_max_batch_size_bytes to limit the size of
  // the batches we read in GetChanges / ReadReplicatedMessagesForCDC.
  // We want to test that we don't get stuck if an entire batch messages read are all filtered out
  // (currently we only filter out external writes) and the checkpoint isn't updated.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_consensus_max_batch_size_bytes) = 1_KB;
  constexpr auto kNumRowsToWrite = 500;
  ASSERT_OK(SetUpWithParams({2}, 1));

  // Setup bi-directional replication.
  ASSERT_OK(SetupReplication());
  ASSERT_OK(SetupReverseUniverseReplication(consumer_tables_));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(2));
  ASSERT_OK(CorrectlyPollingAllTablets(producer_cluster(), 2));

  // Write some rows on one cluster.
  ASSERT_OK(InsertRowsAndVerify(0, kNumRowsToWrite));

  // Ensure that records are the same on both clusters.
  ASSERT_OK(VerifyRowsMatch());
  // Ensure that both universes have all records.
  ASSERT_OK(VerifyNumRecordsOnProducer(kNumRowsToWrite));

  // Now write some rows on the other cluster.
  ASSERT_OK(InsertRowsInConsumer(kNumRowsToWrite, 2 * kNumRowsToWrite));

  // Ensure that same records exist on both universes.
  ASSERT_OK(VerifyRowsMatch());
  // Ensure that both universes have all records.
  ASSERT_OK(VerifyNumRecordsOnProducer(2 * kNumRowsToWrite));
}

TEST_P(XClusterTest, AlterUniverseReplicationMasters) {
  // Tablets = Servers + 1 to stay simple but ensure round robin gives a tablet to everyone.
  uint32_t t_count = 2;
  int master_count = 3;
  ASSERT_OK(SetUpWithParams({t_count, t_count}, {t_count, t_count}, 1, master_count));

  YBTables initial_tables{producer_table_};

  // SetupUniverseReplication only utilizes 1 master.
  ASSERT_OK(SetupUniverseReplication(initial_tables));

  master::GetUniverseReplicationResponsePB v_resp;
  ASSERT_OK(VerifyUniverseReplication(&v_resp));
  ASSERT_EQ(v_resp.entry().producer_master_addresses_size(), 1);
  ASSERT_EQ(
      HostPortFromPB(v_resp.entry().producer_master_addresses(0)),
      ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(t_count));

  LOG(INFO) << "Alter Replication to include all Masters";
  // Alter Replication to include the other masters.
  {
    master::AlterUniverseReplicationRequestPB alter_req;
    master::AlterUniverseReplicationResponsePB alter_resp;
    alter_req.set_replication_group_id(kReplicationGroupId.ToString());

    // GetMasterAddresses returns 3 masters.
    string master_addr = producer_cluster()->GetMasterAddresses();
    auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, alter_req.mutable_producer_master_addresses());

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(),
        ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
    ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));
    ASSERT_FALSE(alter_resp.has_error());

    // Verify that the consumer now has all masters.
    ASSERT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          master::GetUniverseReplicationResponsePB tmp_resp;
          return VerifyUniverseReplication(&tmp_resp).ok() &&
                 tmp_resp.entry().producer_master_addresses_size() == master_count;
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Verify master count increased."));
    ASSERT_OK(CorrectlyPollingAllTablets(t_count));
  }

  // Stop the old master.
  LOG(INFO) << "Failover to new Master";
  MiniMaster* old_master = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster());
  ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->Shutdown();
  MiniMaster* new_master = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster());
  ASSERT_NE(nullptr, new_master);
  ASSERT_NE(old_master, new_master);
  ASSERT_OK(producer_cluster()->WaitForAllTabletServers());

  LOG(INFO) << "Add Table after Master Failover";
  // Add a new table to replication and ensure that it can read using the new master config.
  {
    master::AlterUniverseReplicationRequestPB alter_req;
    master::AlterUniverseReplicationResponsePB alter_resp;
    alter_req.set_replication_group_id(kReplicationGroupId.ToString());
    alter_req.add_producer_table_ids_to_add(producer_tables_[1]->id());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(),
        ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
    ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));
    ASSERT_FALSE(alter_resp.has_error());

    // Verify that the consumer now has both tables in the universe.
    ASSERT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          master::GetUniverseReplicationResponsePB tmp_resp;
          return VerifyUniverseReplication(&tmp_resp).ok() && tmp_resp.entry().tables_size() == 2;
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Verify table created with alter."));
    ASSERT_OK(CorrectlyPollingAllTablets(t_count * 2));
  }

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, AlterUniverseReplicationTables) {
  // Setup the consumer and producer cluster.
  ASSERT_OK(SetUpWithParams({3, 3}, 1));

  // Setup universe replication on the first table.
  auto initial_table = {producer_table_};
  ASSERT_OK(SetupUniverseReplication(initial_table));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB v_resp;
  ASSERT_OK(VerifyUniverseReplication(&v_resp));
  ASSERT_EQ(v_resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(v_resp.entry().tables_size(), 1);
  ASSERT_EQ(v_resp.entry().tables(0), producer_table_->id());

  ASSERT_OK(CorrectlyPollingAllTablets(3));

  // 'add_table'. Add the next table with the alter command.
  {
    master::AlterUniverseReplicationRequestPB alter_req;
    master::AlterUniverseReplicationResponsePB alter_resp;
    alter_req.set_replication_group_id(kReplicationGroupId.ToString());
    alter_req.add_producer_table_ids_to_add(producer_tables_[1]->id());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(),
        ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
    ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));
    ASSERT_FALSE(alter_resp.has_error());

    // Verify that the consumer now has both tables in the universe.
    ASSERT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          master::GetUniverseReplicationResponsePB tmp_resp;
          return VerifyUniverseReplication(&tmp_resp).ok() && tmp_resp.entry().tables_size() == 2;
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Verify table created with alter."));
    ASSERT_OK(CorrectlyPollingAllTablets(6));
  }

  // Write some rows to the new table on the Producer. Ensure that the Consumer gets it.
  ASSERT_OK(InsertRowsAndVerify(6, 10, producer_tables_[1]));

  // 'remove_table'. Remove the original table, leaving only the new one.
  {
    master::AlterUniverseReplicationRequestPB alter_req;
    master::AlterUniverseReplicationResponsePB alter_resp;
    alter_req.set_replication_group_id(kReplicationGroupId.ToString());
    alter_req.add_producer_table_ids_to_remove(producer_table_->id());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(),
        ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
    ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));
    ASSERT_FALSE(alter_resp.has_error());

    // Verify that the consumer now has only the new table created by the previous alter.
    ASSERT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          return VerifyUniverseReplication(&v_resp).ok() && v_resp.entry().tables_size() == 1;
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Verify table removed with alter."));
    ASSERT_EQ(v_resp.entry().tables(0), producer_tables_[1]->id());
    ASSERT_OK(CorrectlyPollingAllTablets(3));
  }

  LOG(INFO) << "All alter tests passed.  Tearing down...";

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, AlterUniverseReplicationBootstrapStateUpdate) {
  constexpr int kNTabletsPerTable = 3;
  constexpr int kNumTables = 5;
  constexpr int kReplicationFactor = 3;
  std::vector<uint32_t> tables_vector(kNumTables);
  for (size_t i = 0; i < kNumTables; i++) {
    tables_vector[i] = kNTabletsPerTable;
  }
  ASSERT_OK(SetUpWithParams(tables_vector, kReplicationFactor));

  auto client = ASSERT_RESULT(consumer_cluster()->CreateClient());
  auto producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
      &client->proxy_cache(),
      HostPort::FromBoundEndpoint(producer_cluster()->mini_tablet_server(0)->bound_rpc_addr()));
  auto consumer_master_proxy = std::make_unique<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
  auto producer_master_proxy = std::make_unique<master::MasterReplicationProxy>(
      &producer_client()->proxy_cache(),
      ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  // Bootstrap producer tables.
  auto bootstrap_ids =
      ASSERT_RESULT(BootstrapProducer(producer_cluster(), producer_client(), producer_tables_));

  // Set up replication with bootstrap IDs.
  ASSERT_OK(SetupUniverseReplication(

      {producer_tables_.front()}, {bootstrap_ids.front()}));

  // Set up replication for the remaining tables using alter universe.
  {
    master::AlterUniverseReplicationRequestPB alter_req;
    master::AlterUniverseReplicationResponsePB alter_resp;
    alter_req.set_replication_group_id(kReplicationGroupId.ToString());
    for (size_t i = 1; i < kNumTables; i++) {
      alter_req.add_producer_table_ids_to_add(producer_tables_[i]->id());
      alter_req.add_producer_bootstrap_ids_to_add(bootstrap_ids[i].ToString());
    }

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    ASSERT_OK(consumer_master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));
    ASSERT_FALSE(alter_resp.has_error());

    // Verify that replication is correctly setup, and consumer has all tables.
    ASSERT_OK(LoggedWaitFor(
        [&]() {
          master::GetUniverseReplicationResponsePB get_resp;
          return VerifyUniverseReplication(&get_resp).ok() &&
                 get_resp.entry().tables_size() == kNumTables;
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Verify table created with alter."));
  }

  // Verify that all streams are set to ACTIVE state.
  {
    string state_active = master::SysCDCStreamEntryPB::State_Name(
        master::SysCDCStreamEntryPB_State::SysCDCStreamEntryPB_State_ACTIVE);
    master::ListCDCStreamsRequestPB list_req;
    master::ListCDCStreamsResponsePB list_resp;
    list_req.set_id_type(yb::master::IdTypePB::TABLE_ID);

    ASSERT_OK(LoggedWaitFor(
        [&]() {
          rpc::RpcController rpc;
          rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
          return producer_master_proxy->ListCDCStreams(list_req, &list_resp, &rpc).ok() &&
                 !list_resp.has_error() &&
                 list_resp.streams_size() == kNumTables;  // One stream per table.
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Verify stream creation"));

    std::vector<string> stream_states;
    for (int i = 0; i < list_resp.streams_size(); i++) {
      auto options = list_resp.streams(i).options();
      for (const auto& option : options) {
        if (option.has_key() && option.key() == "state" && option.has_value()) {
          stream_states.push_back(option.value());
        }
      }
    }
    ASSERT_TRUE(
        stream_states.size() == kNumTables &&
        std::all_of(stream_states.begin(), stream_states.end(), [&](string state) {
          return state == state_active;
        }));
  }

  // Verify that replication is working.
  for (int i = 0; i < kNumTables; i++) {
    ASSERT_OK(InsertRowsAndVerify(0, 5 * (i + 1), producer_tables_[i]));
  }

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, ToggleReplicationEnabled) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  ASSERT_OK(SetUpWithParams({2}, replication_factor));

  ASSERT_OK(SetupReplication());

  // Verify that universe is now ACTIVE
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));

  // After we know the universe is ACTIVE, make sure all tablets are getting polled.
  ASSERT_OK(CorrectlyPollingAllTablets(2));

  // Disable the replication and ensure no tablets are being polled
  ASSERT_OK(
      ToggleUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, false));
  ASSERT_OK(CorrectlyPollingAllTablets(0));

  // Enable replication and ensure that all the tablets start being polled again
  ASSERT_OK(
      ToggleUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, true));
  ASSERT_OK(CorrectlyPollingAllTablets(2));
}

TEST_P(XClusterTest, TestDeleteUniverse) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);

  ASSERT_OK(SetUpWithParams({8, 4}, {6, 6}, replication_factor));

  ASSERT_OK(SetupReplication());

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(12));

  ASSERT_OK(DeleteUniverseReplication());

  ASSERT_OK(VerifyUniverseReplicationDeleted(
      consumer_cluster(), consumer_client(), kReplicationGroupId,
      FLAGS_cdc_read_rpc_timeout_ms * 2));

  ASSERT_OK(CorrectlyPollingAllTablets(0));
}

TEST_P(XClusterTest, TestWalRetentionSet) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_wal_retention_time_secs) = 8 * 3600;

  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  ASSERT_OK(SetUpWithParams({8, 4, 4, 12}, {8, 4, 12, 8}, replication_factor));

  ASSERT_OK(SetupReplication());

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));

  // After creating the cluster, make sure all 32 tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(32));

  cdc::VerifyWalRetentionTime(producer_cluster(), "test_table_", FLAGS_cdc_wal_retention_time_secs);

  YBTableName table_name(YQL_DATABASE_CQL, namespace_name, "test_table_0");

  // Issue an ALTER TABLE request on the producer to verify that it doesn't crash.
  auto table_alterer = producer_client()->NewTableAlterer(table_name);
  table_alterer->AddColumn("new_col")->Type(DataType::INT32);
  ASSERT_OK(table_alterer->timeout(MonoDelta::FromSeconds(kRpcTimeout))->Alter());

  // Verify that the table got altered on the producer.
  YBSchema schema;
  dockv::PartitionSchema partition_schema;
  ASSERT_OK(producer_client()->GetTableSchema(table_name, &schema, &partition_schema));

  ASSERT_NE(static_cast<int>(Schema::kColumnNotFound), schema.FindColumn("new_col"));
}

TEST_P(XClusterTest, TestProducerUniverseExpansion) {
  // Test that after new node(s) are added to producer universe, we are able to get replicated data
  // from the new node(s).
  ASSERT_OK(SetUpWithParams({2}, 1));

  ASSERT_OK(SetupReplication());

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(2));

  ASSERT_OK(InsertRowsInProducer(0, 5));

  // Add new node and wait for tablets to be rebalanced.
  // After rebalancing, each node will be leader for 1 tablet.
  ASSERT_OK(producer_cluster()->AddTabletServer());
  ASSERT_OK(producer_cluster()->WaitForTabletServerCount(2));
  ASSERT_OK(WaitFor(
      [&]() { return producer_client()->IsLoadBalanced(2); }, MonoDelta::FromSeconds(kRpcTimeout),
      "IsLoadBalanced"));

  // Check that all tablets continue to be polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(2));

  // Write some more rows. Note that some of these rows will have the new node as the tablet leader.
  ASSERT_OK(InsertRowsAndVerify(6, 10));
}

TEST_P(XClusterTest, TestAlterDDLBasic) {
  SetAtomicFlag(true, &FLAGS_xcluster_wait_on_ddl_alter);

  uint32_t replication_factor = 1;
  // Use just one tablet here to more easily catch lower-level write issues with this test.
  ASSERT_OK(SetUpWithParams({1}, replication_factor));

  ASSERT_OK(SetupReplication());

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(1));

  ASSERT_OK(InsertRowsAndVerify(0, 5));

  auto xcluster_consumer =
      consumer_cluster()->mini_tablet_server(0)->server()->GetXClusterConsumer();

  auto replication_error_count_before_alter =
      xcluster_consumer->TEST_metric_replication_error_count()->value();
  // Alter the CQL Table on the Producer to Add a New Column.
  LOG(INFO) << "Alter the Producer.";
  {
    std::unique_ptr<YBTableAlterer> table_alterer(
        producer_client()->NewTableAlterer(producer_table_->name()));
    table_alterer->AddColumn("contact_name")->Type(DataType::STRING);
    ASSERT_OK(table_alterer->Alter());
  }

  // Write some data with the New Schema on the Producer.
  {
    auto session = producer_client()->NewSession(kRpcTimeout * 1s);
    client::TableHandle table_handle;
    ASSERT_OK(table_handle.Open(producer_table_->name(), producer_client()));
    std::vector<std::shared_ptr<client::YBqlOp>> ops;
    uint32_t start = 5, end = 10;

    LOG(INFO) << "Writing " << end - start << " inserts";
    for (uint32_t i = start; i < end; i++) {
      auto op = table_handle.NewInsertOp();
      auto req = op->mutable_request();
      QLAddInt32HashValue(req, i);
      table_handle.AddStringColumnValue(req, "contact_name", "YugaByte");
      ASSERT_OK(session->TEST_ApplyAndFlush(op));
    }
  }

  // Get the stream id.
  auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_table_->id()));

  // Verify that the replication status for the consumer table contains a schema mismatch error.
  ASSERT_OK(VerifyReplicationError(
      consumer_table_->id(), stream_id, ReplicationErrorPb::REPLICATION_SCHEMA_MISMATCH));
  ASSERT_GT(xcluster_consumer->TEST_metric_replication_error_count()->value(),
      replication_error_count_before_alter);

  // Alter the CQL Table on the Consumer next to match.
  LOG(INFO) << "Alter the Consumer.";
  {
    std::unique_ptr<YBTableAlterer> table_alterer(
        consumer_client()->NewTableAlterer(consumer_table_->name()));
    table_alterer->AddColumn("contact_name")->Type(DataType::STRING);
    ASSERT_OK(table_alterer->Alter());
  }

  // Verify that the Producer data is now sent over.
  LOG(INFO) << "Verify matching records.";
  ASSERT_OK(VerifyRowsMatch());

  // Verify that the replication status for the consumer table does not contain an error.
  ASSERT_OK(VerifyReplicationError(consumer_table_->id(), stream_id, std::nullopt));

  // Stop replication on the Consumer.
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, TestAlterDDLWithRestarts) {
  SetAtomicFlag(true, &FLAGS_xcluster_wait_on_ddl_alter);

  uint32_t replication_factor = 3;
  ASSERT_OK(SetUpWithParams({1}, {1}, replication_factor, 2, 3));

  ASSERT_OK(SetupReplication());

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(1));

  ASSERT_OK(InsertRowsInProducer(0, 5));

  // Check that all tablets continue to be polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(1));

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyRowsMatch());

  // Alter the CQL Table on the Producer to Add a New Column.
  LOG(INFO) << "Alter the Producer.";
  {
    std::unique_ptr<YBTableAlterer> table_alterer(
        producer_client()->NewTableAlterer(producer_table_->name()));
    table_alterer->AddColumn("contact_name")->Type(DataType::STRING);
    ASSERT_OK(table_alterer->Alter());
  }

  // Write some data with the New Schema on the Producer.
  {
    auto session = producer_client()->NewSession(kRpcTimeout * 1s);
    client::TableHandle table_handle;
    ASSERT_OK(table_handle.Open(producer_table_->name(), producer_client()));
    std::vector<std::shared_ptr<client::YBqlOp>> ops;
    uint32_t start = 5, end = 10;

    LOG(INFO) << "Writing " << end - start << " inserts";
    for (uint32_t i = start; i < end; i++) {
      auto op = table_handle.NewInsertOp();
      auto req = op->mutable_request();
      QLAddInt32HashValue(req, i);
      table_handle.AddStringColumnValue(req, "contact_name", "YugaByte");
      ASSERT_OK(session->TEST_ApplyAndFlush(op));
    }
  }

  // Verify that the Consumer doesn't have new data even though it has the pending_schema/ALTER.
  LOG(INFO) << "Verify that Consumer doesn't have inserts (before restart).";
  {
    auto producer_results = ScanTableToStrings(producer_table_->name(), producer_client());
    auto consumer_results = ScanTableToStrings(consumer_table_->name(), consumer_client());
    ASSERT_EQ(producer_results.size(), 10);
    ASSERT_EQ(consumer_results.size(), 5);
  }

  // Shutdown the Consumer TServer that contains the tablet leader.  Verify balance to new server.
  {
    auto tablet_ids = ListTabletIdsForTable(consumer_cluster(), consumer_table_->id());
    ASSERT_EQ(tablet_ids.size(), 1);
    auto old_ts = FindTabletLeader(consumer_cluster(), *tablet_ids.begin());
    old_ts->Shutdown();
    const auto deadline = CoarseMonoClock::Now() + 10s * kTimeMultiplier;
    ASSERT_OK(WaitUntilTabletHasLeader(consumer_cluster(), *tablet_ids.begin(), deadline));
    decltype(old_ts) new_ts = nullptr;
    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          new_ts = FindTabletLeader(consumer_cluster(), *tablet_ids.begin());
          return new_ts != nullptr;
        },
        MonoDelta::FromSeconds(10), "FindTabletLeader"));
    ASSERT_NE(old_ts, new_ts);
  }

  // Verify that the new Consumer TServer is in the same state as before.
  LOG(INFO) << "Verify that Consumer doesn't have inserts (after restart).";
  {
    auto producer_results = ScanTableToStrings(producer_table_->name(), producer_client());
    auto consumer_results = ScanTableToStrings(consumer_table_->name(), consumer_client());
    ASSERT_EQ(producer_results.size(), 10);
    ASSERT_EQ(consumer_results.size(), 5);
  }

  // Alter the CQL Table on the Consumer next to match.
  LOG(INFO) << "Alter the Consumer.";
  {
    std::unique_ptr<YBTableAlterer> table_alterer(
        consumer_client()->NewTableAlterer(consumer_table_->name()));
    table_alterer->AddColumn("contact_name")->Type(DataType::STRING);
    ASSERT_OK(table_alterer->Alter());
  }

  // Verify that the Producer data is now sent over.
  LOG(INFO) << "Verify matching records.";
  ASSERT_OK(VerifyRowsMatch());

  // Stop replication on the Consumer.
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, ApplyOperationsRandomFailures) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  // Use unequal table count so we have M:N mapping and output to multiple tablets.
  ASSERT_OK(SetUpWithParams({3}, {5}, replication_factor));

  ASSERT_OK(SetupReplication());

  // Set up bi-directional replication.
  ASSERT_OK(SetupReverseUniverseReplication(consumer_tables_));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(5));
  ASSERT_OK(CorrectlyPollingAllTablets(producer_cluster(), 3));

  SetAtomicFlag(0.25, &FLAGS_TEST_respond_write_failed_probability);

  // Write 1000 entries to each cluster.
  Status t1_s, t2_s;
  std::thread t1([&]() { t1_s = InsertRowsInProducer(0, 1000); });
  std::thread t2([&]() { t2_s = InsertRowsInConsumer(1000, 2000); });

  t1.join();
  t2.join();
  ASSERT_OK(t1_s);
  ASSERT_OK(t2_s);

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyRowsMatch());

  // Stop replication on consumer.
  ASSERT_OK(DeleteUniverseReplication());

  // Stop replication on producer
  ASSERT_OK(DeleteUniverseReplication(kReplicationGroupId, producer_client(), producer_cluster()));
}

TEST_P(XClusterTest, TestInsertDeleteWorkloadWithRestart) {
  // Good test for batching, make sure we can handle operations on the same key with different
  // hybrid times. Then, do a restart and make sure we can successfully bootstrap the batched data.
  // In additional, make sure we write exactly num_total_ops / batch_size batches to the cluster to
  // ensure batching is actually enabled.
  constexpr uint32_t num_ops_per_workload = 100;
  constexpr uint32_t num_runs = 5;

  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  ASSERT_OK(SetUpWithParams({1}, replication_factor));

  // This test depends on the write op metrics from the tservers. If the load balancer changes the
  // leader of a consumer tablet, it may re-write replication records and break the test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;

  ASSERT_OK(InsertRowsInProducer(0, num_ops_per_workload));
  for (size_t i = 0; i < num_runs; i++) {
    ASSERT_OK(DeleteRows(0, num_ops_per_workload));
    ASSERT_OK(InsertRowsInProducer(0, num_ops_per_workload));
  }

  ASSERT_OK(SetupReplication());

  ASSERT_OK(LoggedWaitFor(
      [&]() { return GetSuccessfulWriteOps(consumer_cluster()) == 1; }, MonoDelta::FromSeconds(60),
      "Wait for all batches to finish."));

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyRowsMatch());

  ASSERT_OK(consumer_cluster()->RestartSync());

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyRowsMatch());
  // Stop replication on consumer.
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, TestDeleteCDCStreamWithMissingStreams) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);

  ASSERT_OK(SetUpWithParams({8, 4}, {6, 6}, replication_factor));

  ASSERT_OK(SetupReplication());

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(12));

  // Delete the CDC stream on the producer for a table.
  master::ListCDCStreamsResponsePB stream_resp;
  ASSERT_OK(GetCDCStreamForTable(producer_table_->id(), &stream_resp));
  ASSERT_EQ(stream_resp.streams_size(), 1);
  ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), producer_table_->id());
  auto stream_id = stream_resp.streams(0).stream_id();

  rpc::RpcController rpc;
  auto producer_proxy = std::make_shared<master::MasterReplicationProxy>(
      &producer_client()->proxy_cache(),
      ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  master::DeleteCDCStreamRequestPB delete_cdc_stream_req;
  master::DeleteCDCStreamResponsePB delete_cdc_stream_resp;
  delete_cdc_stream_req.add_stream_id(stream_id);
  delete_cdc_stream_req.set_force_delete(true);

  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  ASSERT_OK(producer_proxy->DeleteCDCStream(delete_cdc_stream_req, &delete_cdc_stream_resp, &rpc));

  // Try to delete the universe.
  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
  rpc.Reset();
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  master::DeleteUniverseReplicationRequestPB delete_universe_req;
  master::DeleteUniverseReplicationResponsePB delete_universe_resp;
  delete_universe_req.set_replication_group_id(kReplicationGroupId.ToString());
  delete_universe_req.set_ignore_errors(false);
  ASSERT_OK(
      master_proxy->DeleteUniverseReplication(delete_universe_req, &delete_universe_resp, &rpc));
  // Ensure that DeleteUniverseReplication ignores any errors due to missing streams
  ASSERT_FALSE(delete_universe_resp.has_error());

  // Ensure that the delete is now succesful.
  ASSERT_OK(VerifyUniverseReplicationDeleted(
      consumer_cluster(), consumer_client(), kReplicationGroupId,
      FLAGS_cdc_read_rpc_timeout_ms * 2));

  ASSERT_OK(CorrectlyPollingAllTablets(0));
}

TEST_P(XClusterTest, TestAlterWhenProducerIsInaccessible) {
  ASSERT_OK(SetUpWithParams({1}, 1));

  ASSERT_OK(SetupReplication());

  // Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));

  // Stop the producer master.
  producer_cluster()->mini_master(0)->Shutdown();

  // Try to alter replication.
  master::AlterUniverseReplicationRequestPB alter_req;
  master::AlterUniverseReplicationResponsePB alter_resp;
  alter_req.set_replication_group_id(kReplicationGroupId.ToString());
  alter_req.add_producer_table_ids_to_add("123");  // Doesn't matter as we cannot connect.
  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  // Ensure that we just return an error and don't have a fatal.
  ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));
  ASSERT_TRUE(alter_resp.has_error());
}

TEST_P(XClusterTest, TestFailedUniverseDeletionOnRestart) {
  ASSERT_OK(SetUpWithParams({8, 4}, {6, 6}, 3));

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  // manually call SetupUniverseReplication to ensure it fails
  master::SetupUniverseReplicationRequestPB req;
  master::SetupUniverseReplicationResponsePB resp;
  string master_addr = producer_cluster()->GetMasterAddresses();
  auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
  HostPortsToPBs(hp_vec, req.mutable_producer_master_addresses());
  req.set_replication_group_id(kReplicationGroupId.ToString());
  req.mutable_producer_table_ids()->Reserve(1);
  req.add_producer_table_ids("Fake Table Id");

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  ASSERT_OK(master_proxy->SetupUniverseReplication(req, &resp, &rpc));
  // Sleep to allow the universe to be marked as failed
  std::this_thread::sleep_for(2s);

  master::GetUniverseReplicationRequestPB new_req;
  new_req.set_replication_group_id(kReplicationGroupId.ToString());
  master::GetUniverseReplicationResponsePB new_resp;
  rpc.Reset();
  ASSERT_OK(master_proxy->GetUniverseReplication(new_req, &new_resp, &rpc));
  ASSERT_TRUE(new_resp.entry().state() == master::SysUniverseReplicationEntryPB::FAILED);

  // Restart the ENTIRE Consumer cluster.
  ASSERT_OK(consumer_cluster()->RestartSync());

  // Should delete on restart
  ASSERT_OK(WaitForSetupUniverseReplicationCleanUp(kReplicationGroupId));
  rpc.Reset();
  Status s = master_proxy->GetUniverseReplication(new_req, &new_resp, &rpc);
  ASSERT_OK(s);
  ASSERT_TRUE(new_resp.has_error());
}

TEST_P(XClusterTest, TestFailedDeleteOnRestart) {
  // Setup the consumer and producer cluster.
  ASSERT_OK(SetUpWithParams({8, 4}, {6, 6}, 3));

  ASSERT_OK(SetupReplication());

  // Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  // Delete The Table
  master::DeleteUniverseReplicationRequestPB alter_req;
  master::DeleteUniverseReplicationResponsePB alter_resp;
  alter_req.set_replication_group_id(kReplicationGroupId.ToString());
  rpc::RpcController rpc;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_exit_unfinished_deleting) = true;
  ASSERT_OK(master_proxy->DeleteUniverseReplication(alter_req, &alter_resp, &rpc));

  // Check that deletion was incomplete
  master::GetUniverseReplicationRequestPB new_req;
  new_req.set_replication_group_id(kReplicationGroupId.ToString());
  master::GetUniverseReplicationResponsePB new_resp;
  rpc.Reset();
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  ASSERT_OK(master_proxy->GetUniverseReplication(new_req, &new_resp, &rpc));
  ASSERT_EQ(new_resp.entry().state(), master::SysUniverseReplicationEntryPB::DELETING);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_exit_unfinished_deleting) = false;

  // Restart the ENTIRE Consumer cluster.
  ASSERT_OK(consumer_cluster()->RestartSync());

  // Wait for incomplete delete universe to be deleted on start up
  ASSERT_OK(WaitForSetupUniverseReplicationCleanUp(kReplicationGroupId));

  // Check that the unfinished alter universe was deleted on start up
  rpc.Reset();
  new_req.set_replication_group_id(kReplicationGroupId.ToString());
  Status s = master_proxy->GetUniverseReplication(new_req, &new_resp, &rpc);
  ASSERT_OK(s);
  ASSERT_TRUE(new_resp.has_error());
}

TEST_P(XClusterTest, TestFailedAlterUniverseOnRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_exit_unfinished_merging) = true;

  // Setup the consumer and producer cluster.
  ASSERT_OK(SetUpWithParams({3, 3}, 1));
  ASSERT_OK(SetupUniverseReplication({producer_table_}));

  // Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  // Make sure only 1 table is included in replication
  master::GetUniverseReplicationRequestPB new_req;
  new_req.set_replication_group_id(kReplicationGroupId.ToString());
  master::GetUniverseReplicationResponsePB new_resp;
  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  ASSERT_OK(master_proxy->GetUniverseReplication(new_req, &new_resp, &rpc));
  ASSERT_EQ(new_resp.entry().tables_size(), 1);

  // Add the other table
  master::AlterUniverseReplicationRequestPB alter_req;
  master::AlterUniverseReplicationResponsePB alter_resp;
  alter_req.set_replication_group_id(kReplicationGroupId.ToString());
  alter_req.add_producer_table_ids_to_add(producer_tables_[1]->id());
  rpc.Reset();

  ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));

  // Restart the ENTIRE Consumer cluster.
  ASSERT_OK(consumer_cluster()->RestartSync());

  // Wait for alter universe to be deleted on start up
  ASSERT_OK(WaitForSetupUniverseReplicationCleanUp(
      xcluster::GetAlterReplicationGroupId(kReplicationGroupId)));

  // Change should not have gone through
  new_req.set_replication_group_id(kReplicationGroupId.ToString());
  rpc.Reset();
  ASSERT_OK(master_proxy->GetUniverseReplication(new_req, &new_resp, &rpc));
  ASSERT_NE(new_resp.entry().tables_size(), 2);

  // Check that the unfinished alter universe was deleted on start up
  rpc.Reset();
  new_req.set_replication_group_id(
      xcluster::GetAlterReplicationGroupId(kReplicationGroupId).ToString());
  Status s = master_proxy->GetUniverseReplication(new_req, &new_resp, &rpc);
  ASSERT_OK(s);
  ASSERT_TRUE(new_resp.has_error());
}

TEST_P(XClusterTest, TestAlterUniverseRemoveTableAndDrop) {
  // Create 2 tables and start replication.
  ASSERT_OK(SetUpWithParams({1, 1}, 1));

  ASSERT_OK(SetupReplication());

  // Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  // Remove one table via alteruniversereplication.
  master::AlterUniverseReplicationRequestPB alter_req;
  master::AlterUniverseReplicationResponsePB alter_resp;
  rpc::RpcController rpc;
  alter_req.set_replication_group_id(kReplicationGroupId.ToString());
  alter_req.add_producer_table_ids_to_remove(producer_table_->id());

  ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));

  // Now attempt to drop the table on both sides.
  ASSERT_OK(producer_client()->DeleteTable(producer_table_->name()));
  ASSERT_OK(consumer_client()->DeleteTable(consumer_table_->name()));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, TestNonZeroLagMetricsWithoutGetChange) {
  ASSERT_OK(SetUpWithParams({1}, 1));

  // Stop the consumer tserver before setting up replication.
  consumer_cluster()->mini_tablet_server(0)->Shutdown();

  ASSERT_OK(SetupReplication());
  SleepFor(MonoDelta::FromSeconds(5));  // Wait for the stream to setup.

  // Obtain CDC stream id.
  master::ListCDCStreamsResponsePB stream_resp;
  ASSERT_OK(GetCDCStreamForTable(producer_table_->id(), &stream_resp));
  ASSERT_FALSE(stream_resp.has_error());
  ASSERT_EQ(stream_resp.streams_size(), 1);
  ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), producer_table_->id());
  auto stream_id = ASSERT_RESULT(xrepl::StreamId::FromString(stream_resp.streams(0).stream_id()));

  // Obtain producer tablet id.
  TabletId tablet_id;
  {
    yb::cdc::ListTabletsRequestPB tablets_req;
    yb::cdc::ListTabletsResponsePB tablets_resp;
    rpc::RpcController rpc;
    tablets_req.set_stream_id(stream_id.ToString());

    auto producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
        &producer_client()->proxy_cache(),
        HostPort::FromBoundEndpoint(producer_cluster()->mini_tablet_server(0)->bound_rpc_addr()));
    ASSERT_OK(producer_cdc_proxy->ListTablets(tablets_req, &tablets_resp, &rpc));
    ASSERT_FALSE(tablets_resp.has_error());
    ASSERT_EQ(tablets_resp.tablets_size(), 1);
    tablet_id = tablets_resp.tablets(0).tablet_id();
  }

  // Check that the CDC enabled flag is true.
  tserver::TabletServer* cdc_ts = producer_cluster()->mini_tablet_server(0)->server();
  auto cdc_service = dynamic_cast<cdc::CDCServiceImpl*>(
      cdc_ts->rpc_server()->TEST_service_pool("yb.cdc.CDCService")->TEST_get_service().get());

  ASSERT_OK(WaitFor(
      [&]() { return cdc_service->CDCEnabled(); }, MonoDelta::FromSeconds(30), "IsCDCEnabled"));

  // Check that the time_since_last_getchanges metric is updated, even without GetChanges.
  std::shared_ptr<xrepl::XClusterTabletMetrics> metrics;
  ASSERT_OK(WaitFor(
      [&]() {
        auto metrics_result = GetXClusterTabletMetrics(*cdc_service, tablet_id, stream_id);
        if (!metrics_result) {
          return false;
        }
        metrics = metrics_result.get();
        return metrics->time_since_last_getchanges->value() > 0;
      },
      MonoDelta::FromSeconds(30),
      "Retrieve CDC metrics and check time_since_last_getchanges > 0."));

  // Write some data to producer, and check that the lag metric is non-zero on producer tserver,
  // and no GetChanges is received.
  int i = 0;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        RETURN_NOT_OK(InsertRowsInProducer(i, i + 1));
        i++;
        return metrics->async_replication_committed_lag_micros->value() != 0 &&
               metrics->async_replication_sent_lag_micros->value() != 0;
      },
      MonoDelta::FromSeconds(30), "Whether lag != 0 when no GetChanges is received."));

  // Bring up the consumer tserver and verify that replication is successful.
  ASSERT_OK(
      consumer_cluster()->mini_tablet_server(0)->Start(tserver::WaitTabletsBootstrapped::kFalse));
  ASSERT_OK(VerifyRowsMatch());

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, DeleteTableChecksCQL) {
  // Create 3 tables with 1 tablet each.
  constexpr int kNT = 1;
  std::vector<uint32_t> tables_vector = {kNT, kNT, kNT};
  ASSERT_OK(SetUpWithParams(tables_vector, 1));

  // 1. Write some data.
  for (auto& producer_table : producer_tables_) {
    ASSERT_OK(InsertRowsInProducer(0, 100, producer_table));
  }

  // Verify data is written on the producer.
  for (const auto& producer_table : producer_tables_) {
    auto producer_results = ScanTableToStrings(producer_table->name(), producer_client());
    ASSERT_EQ(100, producer_results.size());
  }

  // Set aside one table for AlterUniverseReplication.
  std::shared_ptr<client::YBTable> producer_alter_table, consumer_alter_table;
  producer_alter_table = producer_tables_.back();
  producer_tables_.pop_back();
  consumer_alter_table = consumer_tables_.back();
  consumer_tables_.pop_back();

  // 2a. Setup replication.
  ASSERT_OK(SetupReplication());

  // Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(&get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<uint32_t>(producer_tables_.size() * kNT)));

  // 2b. Alter Replication
  {
    auto* consumer_leader_mini_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(), consumer_leader_mini_master->bound_rpc_addr());
    master::AlterUniverseReplicationRequestPB alter_req;
    master::AlterUniverseReplicationResponsePB alter_resp;
    alter_req.set_replication_group_id(kReplicationGroupId.ToString());
    alter_req.add_producer_table_ids_to_add(producer_alter_table->id());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));
    ASSERT_FALSE(alter_resp.has_error());
    // Wait until we have the new table listed in the existing universe config.
    ASSERT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          master::GetUniverseReplicationResponsePB tmp_resp;
          RETURN_NOT_OK(VerifyUniverseReplication(&tmp_resp));
          return tmp_resp.entry().tables_size() == static_cast<int64>(producer_tables_.size() + 1);
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Verify table created with alter."));

    ASSERT_OK(CorrectlyPollingAllTablets(
        consumer_cluster(), narrow_cast<uint32_t>((producer_tables_.size() + 1) * kNT)));
  }
  producer_tables_.push_back(producer_alter_table);
  consumer_tables_.push_back(consumer_alter_table);

  auto data_replicated_correctly = [&](size_t num_results) -> Result<bool> {
    for (const auto& consumer_table : consumer_tables_) {
      LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
      auto consumer_results = ScanTableToStrings(consumer_table->name(), consumer_client());

      if (num_results != consumer_results.size()) {
        return false;
      }
    }
    return true;
  };
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> { return data_replicated_correctly(100); }, MonoDelta::FromSeconds(20),
      "IsDataReplicatedCorrectly"));

  // Attempt to destroy the producer and consumer tables.
  for (size_t i = 0; i < producer_tables_.size(); ++i) {
    string producer_table_id = producer_tables_[i]->id();
    string consumer_table_id = consumer_tables_[i]->id();
    ASSERT_NOK(producer_client()->DeleteTable(producer_table_id));
    ASSERT_NOK(consumer_client()->DeleteTable(consumer_table_id));
  }

  // Delete replication, afterwards table deletions are no longer blocked.
  ASSERT_OK(DeleteUniverseReplication());

  for (size_t i = 0; i < producer_tables_.size(); ++i) {
    string producer_table_id = producer_tables_[i]->id();
    string consumer_table_id = consumer_tables_[i]->id();
    ASSERT_OK(producer_client()->DeleteTable(producer_table_id));
    ASSERT_OK(consumer_client()->DeleteTable(consumer_table_id));
  }
}

TEST_P(XClusterTest, SetupNSUniverseReplicationExtraConsumerTables) {
  // Initial setup: create 2 tables on each side.
  constexpr int kNTabletsPerTable = 3;
  std::vector<uint32_t> table_vector = {kNTabletsPerTable, kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(table_vector, 1));

  // Create 2 more consumer tables.
  for (int i = 2; i < 4; i++) {
    auto t = ASSERT_RESULT(CreateTable(
        consumer_client(), namespace_name, Format("test_table_$0", i), kNTabletsPerTable));
    consumer_tables_.push_back({});
    ASSERT_OK(consumer_client()->OpenTable(t, &consumer_tables_.back()));
  }

  // Setup NS universe replication. Only the first 2 consumer tables will be replicated.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ns_replication_sync_retry_secs) = 1;
  ASSERT_OK(SetupNSUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kReplicationGroupId,
      namespace_name, YQLDatabase::YQL_DATABASE_CQL));
  ASSERT_OK(VerifyNSUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId,
      narrow_cast<int>(producer_tables_.size())));

  // Create the additional 2 tables on producer. Verify that they are added automatically.
  for (int i = 2; i < 4; i++) {
    auto t = ASSERT_RESULT(CreateTable(
        producer_client(), namespace_name, Format("test_table_$0", i), kNTabletsPerTable));
    producer_tables_.push_back({});
    ASSERT_OK(producer_client()->OpenTable(t, &producer_tables_.back()));
  }
  ASSERT_OK(VerifyNSUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId,
      narrow_cast<int>(consumer_tables_.size())));

  // Write some data and verify replication.
  for (size_t i = 0; i < producer_tables_.size(); i++) {
    ASSERT_OK(InsertRowsAndVerify(0, narrow_cast<int>(10 * (i + 1)), producer_tables_[i]));
  }
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, SetupNSUniverseReplicationExtraProducerTables) {
  // Initial setup: create 2 tables on each side.
  constexpr int kNTabletsPerTable = 3;
  std::vector<uint32_t> table_vector = {kNTabletsPerTable, kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(table_vector, 1));

  // Create 2 more producer tables.
  for (int i = 2; i < 4; i++) {
    auto t = ASSERT_RESULT(CreateTable(
        producer_client(), namespace_name, Format("test_table_$0", i), kNTabletsPerTable));
    producer_tables_.push_back({});
    ASSERT_OK(producer_client()->OpenTable(t, &producer_tables_.back()));
  }

  // Setup NS universe replication. Only the first 2 producer tables will be replicated.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ns_replication_sync_backoff_secs) = 1;
  ASSERT_OK(SetupNSUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kReplicationGroupId,
      namespace_name, YQLDatabase::YQL_DATABASE_CQL));
  ASSERT_OK(VerifyNSUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId,
      narrow_cast<int>(consumer_tables_.size())));

  // Create the additional 2 tables on consumer. Verify that they are added automatically.
  for (int i = 2; i < 4; i++) {
    auto t = ASSERT_RESULT(CreateTable(
        consumer_client(), namespace_name, Format("test_table_$0", i), kNTabletsPerTable));
    consumer_tables_.push_back({});
    ASSERT_OK(consumer_client()->OpenTable(t, &consumer_tables_.back()));
  }
  ASSERT_OK(VerifyNSUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId,
      narrow_cast<int>(producer_tables_.size())));

  // Write some data and verify replication.
  for (size_t i = 0; i < producer_tables_.size(); i++) {
    ASSERT_OK(InsertRowsAndVerify(0, narrow_cast<int>(10 * (i + 1)), producer_tables_[i]));
  }
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, SetupNSUniverseReplicationTwoNamespace) {
  // Create 2 tables in one namespace.
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> table_vector = {kNTabletsPerTable, kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(table_vector, 1));

  // Create 2 tables in another namespace.
  string kNamespaceName2 = "test_namespace_2";
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  producer_tables.reserve(2);
  consumer_tables.reserve(2);
  for (int i = 0; i < 2; i++) {
    auto ptable = ASSERT_RESULT(CreateTable(
        producer_client(), kNamespaceName2, Format("test_table_$0", i), kNTabletsPerTable));
    producer_tables.push_back({});
    ASSERT_OK(producer_client()->OpenTable(ptable, &producer_tables.back()));

    auto ctable = ASSERT_RESULT(CreateTable(
        consumer_client(), kNamespaceName2, Format("test_table_$0", i), kNTabletsPerTable));
    consumer_tables.push_back({});
    ASSERT_OK(consumer_client()->OpenTable(ctable, &consumer_tables.back()));
  }

  // Setup NS universe replication for the second namespace. Verify that the tables under the
  // first namespace will not be added to the replication.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ns_replication_sync_backoff_secs) = 1;
  ASSERT_OK(SetupNSUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kReplicationGroupId,
      kNamespaceName2, YQLDatabase::YQL_DATABASE_CQL));
  SleepFor(MonoDelta::FromSeconds(5));  // Let the bg thread run a few times.
  ASSERT_OK(VerifyNSUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId,
      narrow_cast<int>(producer_tables.size())));

  // Write some data and verify replication.
  for (size_t i = 0; i < producer_tables.size(); i++) {
    ASSERT_OK(InsertRowsAndVerify(0, narrow_cast<int>(10 * (i + 1)), producer_tables[i]));
  }
  ASSERT_OK(DeleteUniverseReplication());
}

class XClusterTestWaitForReplicationDrain : public XClusterTest {
 public:
  void SetUpTablesAndReplication(
      uint32_t num_tables = 3, uint32_t num_tablets_per_table = 3, uint32_t replication_factor = 3,
      uint32_t num_masters = 1, uint32_t num_tservers = 3) {
    std::vector<uint32_t> table_vector(num_tables);
    for (size_t i = 0; i < num_tables; i++) {
      table_vector[i] = num_tablets_per_table;
    }
    producer_tables_.clear();
    consumer_tables_.clear();

    // Set up tables.
    ASSERT_OK(
        SetUpWithParams(table_vector, table_vector, replication_factor, num_masters, num_tservers));

    // Set up replication.
    master::GetUniverseReplicationResponsePB resp;
    ASSERT_OK(SetupReplication());
    ASSERT_OK(VerifyUniverseReplication(
        consumer_cluster(), consumer_client(), kReplicationGroupId, &resp));
  }

  void TearDown() override {
    ASSERT_OK(DeleteUniverseReplication());
    XClusterTest::TearDown();
  }

  std::shared_ptr<std::promise<Status>> WaitForReplicationDrainAsync() {
    // Return a promise that will be set to a status indicating whether the API completes
    // with the expected response.
    auto promise = std::make_shared<std::promise<Status>>();
    std::thread async_task([this, promise]() {
      auto s = WaitForReplicationDrain();
      promise->set_value(s);
    });
    async_task.detach();
    return promise;
  }
};

INSTANTIATE_TEST_CASE_P(
    XClusterTestParams, XClusterTestWaitForReplicationDrain,
    ::testing::Values(
        XClusterTestParams(true /* transactional_table */),
        XClusterTestParams(false /* transactional_table */)));

TEST_P(XClusterTestWaitForReplicationDrain, TestBlockGetChanges) {
  constexpr uint32_t kNumTables = 3;
  constexpr uint32_t kNumTablets = 3;
  constexpr int kRpcTimeoutShort = 30;

  SetUpTablesAndReplication(kNumTables, kNumTablets);
  // 1. Replication is caught-up initially.
  ASSERT_OK(WaitForReplicationDrain());

  // 2. Replication is caught-up when some data is written.
  for (uint32_t i = 0; i < producer_tables_.size(); i++) {
    ASSERT_OK(InsertRowsAndVerify(0, 50 * (i + 1), producer_tables_[i]));
  }
  ASSERT_OK(WaitForReplicationDrain());

  // 3. Replication is not caught-up when GetChanges are blocked while producer
  // keeps taking writes.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_get_changes) = true;
  for (uint32_t i = 0; i < producer_tables_.size(); i++) {
    ASSERT_OK(InsertRowsInProducer(50 * (i + 1), 100 * (i + 1), producer_tables_[i]));
  }
  ASSERT_OK(WaitForReplicationDrain(
      /* expected_num_nondrained */ kNumTables * kNumTablets, kRpcTimeoutShort));

  // 4. Replication is caught-up when GetChanges are unblocked.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_get_changes) = false;
  ASSERT_OK(WaitForReplicationDrain());
}

TEST_P(XClusterTestWaitForReplicationDrain, TestConsumerPollFailureMetric) {
  constexpr uint32_t kNumTables = 3;
  constexpr uint32_t kNumTablets = 3;
  constexpr int kRpcTimeoutShort = 30;

  SetUpTablesAndReplication(kNumTables, kNumTablets);
  ASSERT_OK(WaitForReplicationDrain());

  auto xcluster_consumer =
      consumer_cluster()->mini_tablet_server(0)->server()->GetXClusterConsumer();
  ASSERT_EQ(xcluster_consumer->TEST_metric_apply_failure_count()->value(), 0);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulate_get_changes_response_error) = true;
  for (uint32_t i = 0; i < producer_tables_.size(); i++) {
    ASSERT_OK(InsertRowsInProducer(0, 50 * (i + 1), producer_tables_[i]));
  }

  ASSERT_OK(WaitForReplicationDrain(
      /* expected_num_nondrained */ kNumTables * kNumTablets, kRpcTimeoutShort));

  ASSERT_GE(xcluster_consumer->TEST_metric_poll_failure_count()->value(), kNumTables * kNumTablets);
}

TEST_P(XClusterTestWaitForReplicationDrain, TestWithTargetTime) {
  constexpr uint32_t kNumTables = 3;
  constexpr uint32_t kNumTablets = 3;

  SetUpTablesAndReplication(kNumTables, kNumTablets);

  // 1. Write some data and verify that replication is caught-up.
  auto past_checkpoint = GetCurrentTimeMicros();
  for (uint32_t i = 0; i < producer_tables_.size(); i++) {
    ASSERT_OK(InsertRowsInProducer(0, 50 * (i + 1), producer_tables_[i]));
  }
  ASSERT_OK(WaitForReplicationDrain());

  // 2. Verify that replication is caught-up to a checkpoint in the past.
  ASSERT_OK(WaitForReplicationDrain(
      /* expected_num_nondrained */ 0, /* timeout_secs */ kRpcTimeout,
      /* target_time */ past_checkpoint));

  // 3. Set a checkpoint in the future. Verify that API waits until passing this
  // checkpoint before responding.
  int timeout_secs = kRpcTimeout;
  auto time_to_wait = MonoDelta::FromSeconds(timeout_secs / 2.0);
  auto future_checkpoint = GetCurrentTimeMicros() + time_to_wait.ToMicroseconds();
  ASSERT_OK(WaitForReplicationDrain(
      /* expected_num_nondrained */ 0, /* timeout_secs */ kRpcTimeout,
      /* target_time */ future_checkpoint));
  ASSERT_GT(GetCurrentTimeMicros(), future_checkpoint);
}

TEST_P(XClusterTestWaitForReplicationDrain, TestProducerChange) {
  constexpr uint32_t kNumTables = 3;
  constexpr uint32_t kNumTablets = 3;

  SetUpTablesAndReplication(kNumTables, kNumTablets);
  // 1. Write some data and verify that replication is caught-up.
  for (uint32_t i = 0; i < producer_tables_.size(); i++) {
    ASSERT_OK(InsertRowsInProducer(0, 50 * (i + 1), producer_tables_[i]));
  }
  ASSERT_OK(WaitForReplicationDrain());

  // 2. Verify that producer shutdown does not impact the API.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_hang_wait_replication_drain) = true;
  auto drain_api_promise = WaitForReplicationDrainAsync();
  auto drain_api_future = drain_api_promise->get_future();
  producer_cluster()->mini_tablet_server(0)->Shutdown();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_hang_wait_replication_drain) = false;
  ASSERT_OK(
      producer_cluster()->mini_tablet_server(0)->Start(tserver::WaitTabletsBootstrapped::kFalse));
  ASSERT_OK(drain_api_future.get());

  // 3. Verify that producer rebalancing does not impact the API.
  auto num_tservers = narrow_cast<int>(producer_cluster()->num_tablet_servers());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_hang_wait_replication_drain) = true;
  drain_api_promise = WaitForReplicationDrainAsync();
  drain_api_future = drain_api_promise->get_future();
  ASSERT_OK(producer_cluster()->AddTabletServer());
  ASSERT_OK(producer_cluster()->WaitForTabletServerCount(num_tservers + 1));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_hang_wait_replication_drain) = false;
  ASSERT_OK(drain_api_future.get());
}

TEST_F_EX(XClusterTest, TestPrematureLogGC, XClusterTestNoParam) {
  // Allow WAL segments to be garbage collected regardless of their lifetime.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_wal_retention_time) = true;

  // Set a small WAL segment to ensure that we create many segments in this test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_segment_size_bytes) = 500;

  // Allow the maximum number of WAL segments to be considered for garbage collection.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 1;

  // Don't cache any of the WAL segments.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_cache_size_limit_mb) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_global_log_cache_size_limit_mb) = 0;

  // Increase the frequency of the metrics heartbeat.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 250;

  // Disable the disk space GC policy.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_stop_retaining_min_disk_mb) = 0;

  constexpr int kNTabletsPerTable = 1;
  constexpr int kReplicationFactor = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(tables_vector, kReplicationFactor));

  ASSERT_OK(SetupReplication());

  // Write enough records to the producer to populate multiple WAL segments.
  constexpr int kNumWriteRecords = 100;
  ASSERT_OK(InsertRowsInProducer(0, kNumWriteRecords));

  // Verify that all records were written and replicated.
  ASSERT_OK(VerifyNumRecordsOnProducer(kNumWriteRecords));
  ASSERT_OK(VerifyNumRecordsOnConsumer(kNumWriteRecords));

  // Disable polling on the consumer so that we can forcefully GC the wal segments without racing
  // against the replication poll.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdc_skip_replication_poll) = true;

  // Sleep long enough to ensure the last consumer poll completes.
  SleepFor(MonoDelta::FromSeconds(5));

  // Set a large minimum disk space policy so that all WAL segments will be garbage collected for
  // violating the disk space policy.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_stop_retaining_min_disk_mb) =
      std::numeric_limits<int64_t>::max();

  // Write another batch of records.
  ASSERT_OK(InsertRowsInProducer(kNumWriteRecords, 2 * kNumWriteRecords));
  ASSERT_OK(VerifyNumRecordsOnProducer(2 * kNumWriteRecords));

  // Unflushed WAL segments can not be garbage collected. Flush all tablets WALs now.
  ASSERT_OK(
      producer_cluster()->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));

  // Garbage collect all Tablet WALs. This will GC most of the segments, but will leave at least one
  // segment in each WAL (determined by FLAGS_log_min_segments_to_retain). The remaining segment may
  // contain records.
  ASSERT_OK(producer_cluster()->CleanTabletLogs());

  // Re-enable the replication poll and wait long enough for multiple cycles to run.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdc_skip_replication_poll) = false;
  SleepFor(MonoDelta::FromSeconds(3));

  // Verify that at least one of the recently written records failed to replicate.
  auto results = ScanTableToStrings(consumer_table_->name(), consumer_client());
  ASSERT_GE(results.size(), kNumWriteRecords);
  ASSERT_LT(results.size(), 2 * kNumWriteRecords);

  // Get the stream id.
  auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_table_->id()));

  // Verify that the GetReplicationStatus RPC contains the 'REPLICATION_MISSING_OP_ID' error.
  ASSERT_OK(VerifyReplicationError(
      consumer_table_->id(), stream_id, ReplicationErrorPb::REPLICATION_MISSING_OP_ID));
}

TEST_P(XClusterTest, PausingAndResumingReplicationFromProducerSingleTable) {
  constexpr int kNTabletsPerTable = 3;
  constexpr int kNumTables = 1;
  uint32_t kReplicationFactor = NonTsanVsTsan(3, 1);
  std::vector<uint32_t> tables_vector(kNumTables);
  for (size_t i = 0; i < kNumTables; i++) {
    tables_vector[i] = kNTabletsPerTable;
  }
  ASSERT_OK(SetUpWithParams(tables_vector, kReplicationFactor));

  // Empty case.
  ASSERT_OK(PauseResumeXClusterProducerStreams({}, true));
  ASSERT_OK(PauseResumeXClusterProducerStreams({}, false));

  // Invalid ID case.
  ASSERT_NOK(PauseResumeXClusterProducerStreams({xrepl::StreamId::GenerateRandom()}, true));
  ASSERT_NOK(PauseResumeXClusterProducerStreams({xrepl::StreamId::GenerateRandom()}, false));

  ASSERT_OK(SetupReplication());
  master::IsSetupUniverseReplicationDoneResponsePB is_resp;
  ASSERT_OK(WaitForSetupUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId, &is_resp));

  std::vector<xrepl::StreamId> stream_ids;

  ASSERT_OK(InsertRowsAndVerify(0, 10));

  // Test pausing all streams (by passing empty streams list) when there is only one stream.
  ASSERT_OK(SetPauseAndVerifyInsert(10, 20));

  // Test resuming all streams (by passing empty streams list) when there is only one stream.
  ASSERT_OK(SetPauseAndVerifyInsert(20, 30, {}, false));

  // Test pausing stream by ID when there is only one stream by passing non-empty stream_ids.
  ASSERT_OK(SetPauseAndVerifyInsert(30, 40, {producer_table_.get()}));

  // Test resuming stream by ID when there is only one stream by passing non-empty stream_ids.
  ASSERT_OK(SetPauseAndVerifyInsert(40, 50, {producer_table_.get()}, false));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, PausingAndResumingReplicationFromProducerMultiTable) {
  constexpr int kNTabletsPerTable = 3;
  constexpr int kNumTables = 3;
  uint32_t kReplicationFactor = NonTsanVsTsan(3, 1);
  std::vector<uint32_t> tables_vector(kNumTables);
  for (size_t i = 0; i < kNumTables; i++) {
    tables_vector[i] = kNTabletsPerTable;
  }
  ASSERT_OK(SetUpWithParams(tables_vector, kReplicationFactor));

  ASSERT_OK(SetupReplication());
  master::IsSetupUniverseReplicationDoneResponsePB is_resp;
  ASSERT_OK(WaitForSetupUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId, &is_resp));

  ASSERT_OK(SetPauseAndVerifyInsert(0, 10, {}, false));
  // Test pausing all streams (by passing empty streams list) when there are multiple streams.
  ASSERT_OK(SetPauseAndVerifyInsert(10, 20));
  // Test resuming all streams (by passing empty streams list) when there are multiple streams.
  ASSERT_OK(SetPauseAndVerifyInsert(20, 30, {}, false));

  // Add the table IDs of the first two tables to test pausing and resuming just those ones.
  std::unordered_set<client::YBTable*> tables_to_pause;
  for (size_t i = 0; i < producer_tables_.size() - 1; ++i) {
    tables_to_pause.insert(producer_tables_[i].get());
  }
  // Test pausing replication on the first two tables by passing stream_ids containing their
  // corresponding xcluster streams.
  ASSERT_OK(SetPauseAndVerifyInsert(30, 40, tables_to_pause));
  // Verify that the remaining streams are unpaused still.
  for (size_t i = tables_to_pause.size(); i < producer_tables_.size(); ++i) {
    ASSERT_OK(InsertRowsAndVerify(30, 40, producer_tables_[i]));
  }
  // Test resuming replication on the first two tables by passing stream_ids containing their
  // corresponding xcluster streams.
  ASSERT_OK(SetPauseAndVerifyInsert(40, 50, tables_to_pause, false));
  // Verify that the previously unpaused streams are still replicating.
  for (size_t i = tables_to_pause.size(); i < producer_tables_.size(); ++i) {
    ASSERT_OK(InsertRowsAndVerify(40, 50, producer_tables_[i]));
  }
  ASSERT_OK(DeleteUniverseReplication());
}

// This test is flaky. See #18437.
TEST_P(XClusterTest, YB_DISABLE_TEST(LeaderFailoverTest)) {
  // When the consumer tablet leader moves around (like during an upgrade) the pollers can start and
  // stop on multiple nodes. This test makes sure that during such poller movement we do not get
  // replication errors.
  // 1. We start polling from node A and then failover the consumer leader to node B. We disable
  // deletion of old pollers to simulate a slow xcluster consumer on node A.
  // 2. Poller from node B gets some data and log on producer gets GCed.
  // 3. We failback the consumer leader to node A. Now the same poller from step1 is reused. If this
  // tries to reuse its old checkpoint OpId then it will get a REPLICATION_MISSING_OP_ID error. If
  // it instead checks for its local leader term then it will detect the leader move and poisons
  // itself, causing the xcluster consumer to delete and create a new poller. This new poller does
  // not have any cached checkpoint so it will rely on the producer sending the correct data from
  // the previous checkpoint OpId it received from node B.

  // Dont remove pollers when leaders move.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_disable_delete_old_pollers) = true;
  // Dont increase Poll delay on failures as it is expected.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_failure_delay_exponent) = 0;
  // The below flags are required for fast log gc.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_segment_size_bytes) = 500;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_seconds_to_retain) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_wal_retention_time_secs) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;

  const uint32_t kReplicationFactor = 3, kTabletCount = 1, kNumMasters = 1, kNumTservers = 3;
  ASSERT_OK(SetUpWithParams(
      {kTabletCount}, {kTabletCount}, kReplicationFactor, kNumMasters, kNumTservers));
  ASSERT_OK(
      SetupUniverseReplication(producer_tables_, {LeaderOnly::kFalse, Transactional::kFalse}));

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(kTabletCount));

  auto tablet_ids = ListTabletIdsForTable(consumer_cluster(), consumer_table_->id());
  ASSERT_EQ(tablet_ids.size(), 1);
  const auto tablet_id = *tablet_ids.begin();
  const auto kTimeout = 10s * kTimeMultiplier;

  auto leader_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
  master::MasterClusterProxy master_proxy(
      &consumer_client()->proxy_cache(), leader_master->bound_rpc_addr());
  auto ts_map =
      ASSERT_RESULT(itest::CreateTabletServerMap(master_proxy, &consumer_client()->proxy_cache()));

  itest::TServerDetails* old_ts = nullptr;
  ASSERT_OK(FindTabletLeader(ts_map, tablet_id, kTimeout, &old_ts));

  itest::TServerDetails* new_ts = nullptr;
  for (auto& [ts_id, ts_details] : ts_map) {
    if (ts_id != old_ts->uuid()) {
      new_ts = ts_details.get();
      break;
    }
  }

  constexpr int kNumWriteRecords = 100;
  ASSERT_OK(InsertRowsInProducer(0, kNumWriteRecords));
  ASSERT_OK(VerifyNumRecordsOnConsumer(kNumWriteRecords));

  // Failover to new tserver.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_disable_poller_term_check) = true;
  ASSERT_OK(itest::LeaderStepDown(old_ts, tablet_id, new_ts, kTimeout));
  ASSERT_OK(itest::WaitUntilLeader(new_ts, tablet_id, kTimeout));
  auto new_tserver = FindTabletLeader(consumer_cluster(), tablet_id);

  ASSERT_OK(InsertRowsInProducer(kNumWriteRecords, 2 * kNumWriteRecords));
  ASSERT_OK(VerifyNumRecordsOnConsumer(2 * kNumWriteRecords));

  // GC log on producer.
  // Note: Ideally cdc checkpoint should advance but we do not see that with our combination of
  // flags so disable FLAGS_enable_log_retention_by_op_idx for the duration of the flush instead.
  SetAtomicFlag(false, &FLAGS_enable_log_retention_by_op_idx);
  SleepFor(2s * kTimeMultiplier);
  ASSERT_OK(FlushProducerTabletsAndGCLog());
  SetAtomicFlag(true, &FLAGS_enable_log_retention_by_op_idx);

  // Failback to old tserver.
  ASSERT_OK(itest::LeaderStepDown(new_ts, tablet_id, old_ts, kTimeout));
  ASSERT_OK(itest::WaitUntilLeader(old_ts, tablet_id, kTimeout));

  // Delete old pollers so that we can properly shutdown the servers.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_disable_delete_old_pollers) = false;

  ASSERT_OK(LoggedWaitFor(
      [&new_tserver]() {
        auto new_tserver_pollers = new_tserver->server()->GetXClusterConsumer()->TEST_ListPollers();
        return new_tserver_pollers.size() == 0;
      },
      kTimeout, "Waiting for pollers from new tserver to stop"));

  ASSERT_OK(InsertRowsInProducer(2 * kNumWriteRecords, 3 * kNumWriteRecords));

  auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_table_->id()));
  ASSERT_OK(VerifyReplicationError(
      consumer_table_->id(), stream_id, ReplicationErrorPb::REPLICATION_MISSING_OP_ID));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_disable_poller_term_check) = false;
  ASSERT_OK(VerifyNumRecordsOnConsumer(3 * kNumWriteRecords));
}

Status VerifyMetaCacheObjectIsValid(
    const rapidjson::Value* object, const JsonReader& json_reader,  const char* member_name) {
  const rapidjson::Value* meta_cache = nullptr;
  EXPECT_OK(json_reader.ExtractObject(object, member_name, &meta_cache));
  EXPECT_TRUE(meta_cache->HasMember("tablets"));
  std::vector<const rapidjson::Value*> tablets;
  return json_reader.ExtractObjectArray(meta_cache, "tablets", &tablets);
}

void VerifyMetaCacheWithXClusterConsumerSetUp(const std::string& produced_json) {
  JsonReader json_reader(produced_json);
  EXPECT_OK(json_reader.Init());
  const rapidjson::Value* object = nullptr;
  EXPECT_OK(json_reader.ExtractObject(json_reader.root(), nullptr, &object));
  EXPECT_EQ(rapidjson::kObjectType, CHECK_NOTNULL(object)->GetType());

  EXPECT_OK(VerifyMetaCacheObjectIsValid(object, json_reader, "MainMetaCache"));
  bool found_xcluster_member = false;
  for (auto it = object->MemberBegin(); it != object->MemberEnd();
       ++it) {
    std::string member_name = it->name.GetString();
    if (member_name.starts_with("XClusterConsumerRemote_")) {
      found_xcluster_member = true;
    }
    EXPECT_OK(VerifyMetaCacheObjectIsValid(object, json_reader, member_name.c_str()));
  }
  EXPECT_TRUE(found_xcluster_member)
      << "No member name starting with XClusterConsumerRemote_ is found";
}

TEST_F_EX(XClusterTest, ListMetaCacheAfterXClusterSetup, XClusterTestNoParam) {
  ASSERT_OK(SetUpWithParams({1}, 3));
  std::string table_name = "table";

  auto table = ASSERT_RESULT(CreateTable(producer_client(), namespace_name, table_name, 3));
  std::shared_ptr<client::YBTable> producer_table;
  ASSERT_OK(producer_client()->OpenTable(table, &producer_table));
  ASSERT_RESULT(CreateTable(consumer_client(), namespace_name, table_name, 3));

  ASSERT_OK(SetupUniverseReplication({producer_table}));

  // Verify that universe replication was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  // wait 5 seconds for metacache to populate
  SleepFor(5000ms);

  for (const auto& tserver : consumer_cluster()->mini_tablet_servers()) {
    auto tserver_endpoint = tserver->bound_http_addr();
    auto query_endpoint = "http://" + AsString(tserver_endpoint) + "/api/v1/meta-cache";
    faststring result;
    ASSERT_OK(EasyCurl().FetchURL(query_endpoint, &result));
    VerifyMetaCacheWithXClusterConsumerSetUp(result.ToString());
  }
  ASSERT_OK(DeleteUniverseReplication());
}

// Verify the xCluster Pollers shutdown immediately even if the Poll delay is very long.
TEST_F_EX(XClusterTest, PollerShutdownWithLongPollDelay, XClusterTestNoParam) {
  // Make Pollers enter a long sleep state.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdc_skip_replication_poll) = true;
  ASSERT_OK(SET_FLAG(async_replication_idle_delay_ms, 10 * 60 * 1000));  // 10 minutes
  const auto kTimeout = 10s * kTimeMultiplier;

  const auto kTabletCount = 3;
  ASSERT_OK(SetUpWithParams({kTabletCount}, {kTabletCount}, 3));

  ASSERT_OK(SetupUniverseReplication(producer_tables_));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));

  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), kTabletCount));

  // Wait for the Pollers to sleep.
  ASSERT_OK(LoggedWaitFor(
      [&]() {
        uint32 sleeping_pollers = 0;
        for (const auto& mini_tserver : consumer_cluster()->mini_tablet_servers()) {
          auto* server = mini_tserver->server();
          auto tserver_pollers = server->GetXClusterConsumer()->TEST_ListPollers();
          for (auto& poller : tserver_pollers) {
            if (poller->TEST_Is_Sleeping()) {
              sleeping_pollers++;
            }
          }
        }
        LOG(INFO) << YB_STRUCT_TO_STRING(sleeping_pollers);
        return sleeping_pollers == kTabletCount;
      },
      kTimeout, Format("Waiting for $0 pollers to sleep", kTabletCount)));

  ASSERT_OK(DeleteUniverseReplication());

  // Wait for the Pollers to stop.
  for (const auto& mini_tserver : consumer_cluster()->mini_tablet_servers()) {
    auto* server = mini_tserver->server();
    ASSERT_OK(LoggedWaitFor(
        [&server]() {
          auto tserver_pollers = server->GetXClusterConsumer()->TEST_ListPollers();
          LOG(INFO) << "TServer: " << server->ToString() << ", Pollers: " << tserver_pollers.size();
          return tserver_pollers.size() == 0;
        },
        kTimeout, Format("Waiting for pollers from $0 to stop", server->ToString())));
  }
}

// Validate that cdc checkpoint is set on the log when a new peer is added.
// This test creates a cluster with 4 tservers, and a table with RF3. It then adds a fourth peer and
// ensures the cdc checkpoint is set on all tservers.
TEST_F_EX(XClusterTest, CdcCheckpointPeerMove, XClusterTestNoParam) {
  google::SetVLOGLevel("tablet", 1);
  google::SetVLOGLevel("tablet_metadata", 1);
  const uint32_t kReplicationFactor = 3, kTabletCount = 1, kNumMasters = 1, kNumTservers = 4;
  ASSERT_OK(SetUpWithParams(
      {kTabletCount}, {kTabletCount}, kReplicationFactor, kNumMasters, kNumTservers));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_retention_barrier_no_revision_interval_secs) = 2;
  ASSERT_TRUE(FLAGS_enable_update_local_peer_min_index);

  ASSERT_OK(SetupReplication());

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(kTabletCount));

  auto tablet_ids = ListTabletIdsForTable(producer_cluster(), producer_table_->id());
  ASSERT_EQ(tablet_ids.size(), 1);
  const auto& tablet_id = *tablet_ids.begin();
  const auto kTimeout = 10s * kTimeMultiplier;

  auto leader_master = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster());
  master::MasterClusterProxy master_proxy(
      &producer_client()->proxy_cache(), leader_master->bound_rpc_addr());
  auto ts_map =
      ASSERT_RESULT(itest::CreateTabletServerMap(master_proxy, &producer_client()->proxy_cache()));

  auto peers = ASSERT_RESULT(itest::FindTabletPeers(ts_map, tablet_id, kTimeout));

  itest::TServerDetails* new_follower = nullptr;
  for (auto& [ts_id, ts_details] : ts_map) {
    if (!peers.contains(ts_details.get())) {
      new_follower = ts_details.get();
      break;
    }
  }
  ASSERT_TRUE(new_follower != nullptr);

  itest::TServerDetails* leader_ts = nullptr;
  ASSERT_OK(FindTabletLeader(ts_map, tablet_id, kTimeout, &leader_ts));

  LOG(INFO) << "Leader: " << leader_ts->ToString();
  LOG(INFO) << "Followers: " << yb::ToString(peers);
  LOG(INFO) << "Adding a new Peer " << new_follower->ToString();

  ASSERT_OK(itest::AddServer(
      leader_ts, tablet_id, new_follower, consensus::PeerMemberType::PRE_VOTER, boost::none,
      kTimeout));

  int64_t min_expected_checkpoint = 1, max_found_checkpoint = -1;

  auto wait_for_checkpoint = [this, tablet_id, &min_expected_checkpoint, &max_found_checkpoint]() {
    return LoggedWaitFor(
        [&]() {
          int peer_count = 0;
          int64_t min_found = std::numeric_limits<int64_t>::max();
          int64_t max_found = std::numeric_limits<int64_t>::min();
          for (auto& tablet_server : producer_cluster()->mini_tablet_servers()) {
            for (const auto& tablet_peer :
                 tablet_server->server()->tablet_manager()->GetTabletPeers()) {
              if (tablet_peer->tablet_id() != tablet_id) {
                continue;
              }
              peer_count++;
              auto cdc_checkpoint = tablet_peer->get_cdc_min_replicated_index();
              LOG(INFO) << "TServer: " << tablet_server->server()->ToString()
                        << ", CDC min replicated index: " << cdc_checkpoint;
              if (cdc_checkpoint == std::numeric_limits<int64_t>::max() ||
                  cdc_checkpoint < min_expected_checkpoint) {
                return false;
              }
              min_found = std::min(cdc_checkpoint, min_found);
              max_found = std::max(cdc_checkpoint, max_found);
            }
          }

          if (peer_count != kNumTservers) {
            LOG(INFO) << "Only got cdc checkpoint from " << peer_count
                      << " peers. Expected from: " << kNumTservers;
            return false;
          }
          if (min_found != max_found) {
            LOG(INFO) << "Some peers are not yet at the latest checkpoint. Min: " << min_found
                      << ", Max: " << max_found;
            return false;
          }

          max_found_checkpoint = max_found;
          return true;
        },
        FLAGS_update_metrics_interval_ms * 3ms * kTimeMultiplier,
        Format("Waiting for CDC checkpoint to be at least $0", min_expected_checkpoint));
  };

  ASSERT_OK(wait_for_checkpoint());
  ASSERT_GE(max_found_checkpoint, min_expected_checkpoint);
  min_expected_checkpoint = max_found_checkpoint + 1;

  ASSERT_OK(InsertRowsInProducer(0, 100));
  ASSERT_OK(VerifyRowsMatch());
  ASSERT_OK(wait_for_checkpoint());
  ASSERT_GE(max_found_checkpoint, min_expected_checkpoint);

  for (auto& tablet_server : producer_cluster()->mini_tablet_servers()) {
    for (const auto& tablet_peer : tablet_server->server()->tablet_manager()->GetTabletPeers()) {
      if (tablet_peer->tablet_id() != tablet_id) {
        continue;
      }
      auto cdc_sdk_intents_barrier = tablet_peer->cdc_sdk_min_checkpoint_op_id();
      auto cdc_sdk_history_barrier = tablet_peer->get_cdc_sdk_safe_time();
      LOG(INFO) << "TServer: " << tablet_server->server()->ToString()
                << ", CDCSDK Intents barrier: " << cdc_sdk_intents_barrier
                << ", CDCSDK history barrier: " << cdc_sdk_history_barrier;
      ASSERT_EQ(cdc_sdk_intents_barrier, OpId::Invalid());
      ASSERT_EQ(cdc_sdk_history_barrier, HybridTime::kInvalid);
    }
  }
}

TEST_F_EX(XClusterTest, FetchBootstrapCheckpointsFromLeaders, XClusterTestNoParam) {
  // Setup with many tablets to increase the chance that we hit any ordering issues.
  const uint32_t kReplicationFactor = 3, kTabletCount = 10, kNumMasters = 1, kNumTservers = 3;
  ASSERT_OK(SetUpWithParams(
      {kTabletCount}, {kTabletCount}, kReplicationFactor, kNumMasters, kNumTservers));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  ASSERT_TRUE(FLAGS_enable_update_local_peer_min_index);

  ASSERT_OK(InsertRowsInProducer(0, 2));

  ASSERT_OK(producer_cluster()->AddTabletServer());
  ASSERT_OK(producer_cluster()->WaitForTabletServerCount(4));

  // Bootstrap producer tables.
  // Force using the tserver we just added as the cdc service proxy. Since load balancing is off,
  // this node will not have any tablet peers, so we will have to fetch all the bootstrap ids from
  // other nodes.
  auto bootstrap_ids = ASSERT_RESULT(BootstrapProducer(
      producer_cluster(), producer_client(), producer_tables_, /* proxy_tserver_index */ 3));

  // Set up replication with bootstrap IDs.
  ASSERT_OK(SetupUniverseReplication(producer_tables_, bootstrap_ids));

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(kTabletCount));
  ASSERT_OK(InsertRowsInProducer(3, 100));
  ASSERT_OK(VerifyNumRecordsOnConsumer(97));
}

TEST_F_EX(XClusterTest, RandomFailuresAfterApply, XClusterTestTransactionalOnly) {
  // Fail one third of the Applies.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulate_random_failure_after_apply) = 0.3;
  constexpr int kNumTablets = 3;
  constexpr int kBatchSize = 100;
  ASSERT_OK(SetUpWithParams({kNumTablets}, 3));
  ASSERT_OK(SetupReplication());
  ASSERT_OK(CorrectlyPollingAllTablets(kNumTablets));

  // Write some non-transactional rows.
  int batch_count = 0;
  for (int i = 0; i < 5; i++) {
    ASSERT_OK(InsertRowsInProducer(batch_count * kBatchSize, (batch_count + 1) * kBatchSize));
    batch_count++;
  }
  ASSERT_OK(VerifyNumRecordsOnProducer(batch_count * kBatchSize));
  ASSERT_OK(VerifyRowsMatch());

  // Write some transactional rows.
  for (int i = 0; i < 5; i++) {
    ASSERT_OK(InsertTransactionalBatchOnProducer(
        batch_count * kBatchSize, (batch_count + 1) * kBatchSize));
    batch_count++;
  }
  ASSERT_OK(VerifyNumRecordsOnProducer(batch_count * kBatchSize));
  ASSERT_OK(VerifyRowsMatch());

  // Write some transactional rows with multiple batches.
  auto [session, txn] =
      ASSERT_RESULT(CreateSessionWithTransaction(producer_client(), producer_txn_mgr()));

  for (int i = 0; i < 5; i++) {
    ASSERT_OK(
        InsertIntentsOnProducer(session, batch_count * kBatchSize, (batch_count + 1) * kBatchSize));
    batch_count++;
  }
  ASSERT_OK(txn->CommitFuture().get());
  ASSERT_OK(VerifyNumRecordsOnProducer(batch_count * kBatchSize));
  ASSERT_OK(VerifyRowsMatch());

  auto xcluster_consumer =
      consumer_cluster()->mini_tablet_server(0)->server()->GetXClusterConsumer();
  ASSERT_GT(xcluster_consumer->TEST_metric_apply_failure_count()->value(), 0);
}

TEST_F_EX(XClusterTest, IsBootstrapRequired, XClusterTestNoParam) {
  // This test makes sure IsBootstrapRequired returns false when tables are empty and true when
  // table has data. Adding and removing an empty table to replication should also not require us to
  // bootstrap.

  const uint32_t kReplicationFactor = 3, kTabletCount = 1, kNumMasters = 1, kNumTservers = 3;
  ASSERT_OK(SetUpWithParams(
      {kTabletCount}, {kTabletCount}, kReplicationFactor, kNumMasters, kNumTservers));
  ASSERT_OK(WaitForLoadBalancersToStabilize());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;

  // Empty table should not require bootstrap.
  ASSERT_FALSE(ASSERT_RESULT(producer_client()->IsBootstrapRequired({producer_table_->id()})));

  // Leader failover should not require bootstrap.
  const auto kTimeout = 10s * kTimeMultiplier;
  auto tablet_ids = ListTabletIdsForTable(producer_cluster(), producer_table_->id());
  ASSERT_EQ(tablet_ids.size(), 1);
  const auto& tablet_id = *tablet_ids.begin();

  auto leader_master = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster());
  master::MasterClusterProxy master_proxy(
      &producer_client()->proxy_cache(), leader_master->bound_rpc_addr());
  auto ts_map =
      ASSERT_RESULT(itest::CreateTabletServerMap(master_proxy, &producer_client()->proxy_cache()));

  itest::TServerDetails* leader_ts = nullptr;
  ASSERT_OK(FindTabletLeader(ts_map, tablet_id, kTimeout, &leader_ts));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_disable_poller_term_check) = true;
  ASSERT_OK(itest::LeaderStepDown(leader_ts, tablet_id, nullptr, kTimeout));
  itest::TServerDetails* new_leader_ts = nullptr;
  ASSERT_OK(FindTabletLeader(ts_map, tablet_id, kTimeout, &new_leader_ts));

  ASSERT_FALSE(ASSERT_RESULT(producer_client()->IsBootstrapRequired({producer_table_->id()})));

  // Adding table to replication should not make it require bootstrap.
  ASSERT_OK(SetupReplication());
  ASSERT_FALSE(ASSERT_RESULT(producer_client()->IsBootstrapRequired({producer_table_->id()})));

  // Table with data should require a bootstrap.
  ASSERT_OK(InsertRowsInProducer(0, 100));
  ASSERT_TRUE(ASSERT_RESULT(producer_client()->IsBootstrapRequired({producer_table_->id()})));

  // After we delete all rows, we should not require a bootstrap.
  ASSERT_OK(DeleteRows(0, 100));
  ASSERT_FALSE(ASSERT_RESULT(producer_client()->IsBootstrapRequired({producer_table_->id()})));

  // Delete and recreate replication and make sure bootstrap is not required.
  ASSERT_OK(DeleteUniverseReplication());
  ASSERT_FALSE(ASSERT_RESULT(producer_client()->IsBootstrapRequired({producer_table_->id()})));
  ASSERT_OK(SetupReplication());
  ASSERT_FALSE(ASSERT_RESULT(producer_client()->IsBootstrapRequired({producer_table_->id()})));

  // Table with data should require a bootstrap.
  ASSERT_OK(InsertRowsInProducer(0, 1));
  ASSERT_TRUE(ASSERT_RESULT(producer_client()->IsBootstrapRequired({producer_table_->id()})));
}

TEST_F_EX(XClusterTest, TestStats, XClusterTestNoParam) {
  const uint32_t kReplicationFactor = 1, kTabletCount = 1, kNumMasters = 1, kNumTservers = 1;
  ASSERT_OK(SetUpWithParams(
      {kTabletCount}, {kTabletCount}, kReplicationFactor, kNumMasters, kNumTservers));
  ASSERT_OK(SetupReplication());
  ASSERT_OK(CorrectlyPollingAllTablets(kTabletCount));

  auto source_cdc_service = producer_cluster()->mini_tablet_server(0)->server()->GetCDCService();
  auto* target_xc_consumer =
      consumer_cluster()->mini_tablet_server(0)->server()->GetXClusterConsumer();

  ASSERT_OK(LoggedWaitFor(
      [&source_cdc_service]() {
        auto stats = source_cdc_service->GetAllStreamTabletStats();
        return !stats.empty() && stats[0].avg_poll_delay_ms > 0;
      },
      MonoDelta::FromSeconds(kRpcTimeout), "Waiting for initial source Stats to populate"));
  ASSERT_OK(LoggedWaitFor(
      [&target_xc_consumer]() {
        auto stats = target_xc_consumer->GetPollerStats();
        return !stats.empty() && stats[0].avg_poll_delay_ms > 0;
      },
      MonoDelta::FromSeconds(kRpcTimeout), "Waiting for initial target Stats to populate"));

  // Make sure stats on source and target match.
  auto source_stats = source_cdc_service->GetAllStreamTabletStats();
  ASSERT_EQ(source_stats.size(), 1);
  auto initial_index = source_stats[0].sent_index;
  auto initial_records_sent = source_stats[0].records_sent;
  auto initial_time = source_stats[0].last_poll_time;
  ASSERT_GT(source_stats[0].avg_poll_delay_ms, 0);
  ASSERT_TRUE(source_stats[0].last_poll_time);
  ASSERT_EQ(source_stats[0].sent_index, source_stats[0].latest_index);
  ASSERT_TRUE(source_stats[0].status.ok());

  auto target_stats = target_xc_consumer->GetPollerStats();
  ASSERT_EQ(target_stats.size(), 1);
  ASSERT_GT(target_stats[0].avg_poll_delay_ms, 0);
  ASSERT_TRUE(target_stats[0].status.ok());

  ASSERT_EQ(source_stats[0].records_sent, target_stats[0].records_received);
  ASSERT_EQ(source_stats[0].mbs_sent, target_stats[0].mbs_received);
  ASSERT_EQ(source_stats[0].sent_index, target_stats[0].received_index);

  ASSERT_OK(InsertRowsInProducer(0, 100));
  ASSERT_OK(VerifyRowsMatch());

  // Make sure stats show data was sent and received.
  source_stats = source_cdc_service->GetAllStreamTabletStats();
  ASSERT_EQ(source_stats.size(), 1);
  ASSERT_GT(source_stats[0].avg_poll_delay_ms, 0);
  ASSERT_GT(source_stats[0].records_sent, 0);
  ASSERT_GT(source_stats[0].mbs_sent, 0);
  ASSERT_GT(source_stats[0].sent_index, initial_index);
  ASSERT_GT(source_stats[0].records_sent, initial_records_sent);
  ASSERT_GT(source_stats[0].last_poll_time, initial_time);
  ASSERT_EQ(source_stats[0].sent_index, source_stats[0].latest_index);
  ASSERT_TRUE(source_stats[0].status.ok());

  target_stats = target_xc_consumer->GetPollerStats();
  ASSERT_EQ(target_stats.size(), 1);
  ASSERT_GT(target_stats[0].records_received, 0);
  ASSERT_GT(target_stats[0].mbs_received, 0);
  ASSERT_GT(target_stats[0].avg_poll_delay_ms, 0);
  ASSERT_TRUE(target_stats[0].status.ok());

  ASSERT_EQ(source_stats[0].records_sent, target_stats[0].records_received);
  ASSERT_EQ(source_stats[0].mbs_sent, target_stats[0].mbs_received);
  ASSERT_EQ(source_stats[0].sent_index, target_stats[0].received_index);
}

TEST_F_EX(XClusterTest, VerifyReplicationError, XClusterTestNoParam) {
  // Disable polling so that errors don't get cleared by successful polls.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdc_skip_replication_poll) = true;
  ASSERT_OK(SetUpWithParams(
      {1}, {1}, /* replication_factor */ 1, /* num_masters */ 3, /* num_tservers */ 1));
  ASSERT_OK(SetupReplication());
  ASSERT_OK(CorrectlyPollingAllTablets(1));
  const auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_table_->id()));
  ASSERT_OK(VerifyReplicationError(
      consumer_table_->id(), stream_id, ReplicationErrorPb::REPLICATION_ERROR_UNINITIALIZED));

  const auto replication_error1 = ReplicationErrorPb::REPLICATION_SCHEMA_MISMATCH;
  const auto replication_error2 = ReplicationErrorPb::REPLICATION_MISSING_OP_ID;

  // Store an error in the Poller.
  auto xcluster_consumer =
      consumer_cluster()->mini_tablet_server(0)->server()->GetXClusterConsumer();
  auto pollers = xcluster_consumer->TEST_ListPollers();
  ASSERT_EQ(pollers.size(), 1);
  auto poller = pollers[0];
  poller->StoreReplicationError(replication_error1);

  // Verify the error propagated to master.
  ASSERT_OK(VerifyReplicationError(consumer_table_->id(), stream_id, replication_error1));

  // Verify the error persists across master fail overs and restarts.
  auto old_master = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster());
  ASSERT_OK(producer_cluster()->StepDownMasterLeader());
  ASSERT_OK(VerifyReplicationError(consumer_table_->id(), stream_id, replication_error1));

  // Set a different error in the Poller.
  poller->StoreReplicationError(replication_error2);

  // Verify the new error propagated to the master.
  ASSERT_OK(VerifyReplicationError(consumer_table_->id(), stream_id, replication_error2));

  // Fallback to old master and make sure it is updated.
  ASSERT_OK(producer_cluster()->StepDownMasterLeader(old_master->permanent_uuid()));
  ASSERT_OK(VerifyReplicationError(consumer_table_->id(), stream_id, replication_error2));

  // Clear the error in the Poller.
  poller->StoreReplicationError(ReplicationErrorPb::REPLICATION_OK);

  // Verify the error is cleared in the master.
  ASSERT_OK(VerifyReplicationError(consumer_table_->id(), stream_id, std::nullopt));

  ASSERT_EQ(2, xcluster_consumer->TEST_metric_replication_error_count()->value());
}

// Make sure the full replication error report is sent to a new master leader even when the metric
// collection is skipped on the very first heartbeat to the new master.
TEST_F_EX(XClusterTest, ReplicationErrorAfterMasterFailover, XClusterTestNoParam) {
  // Send metrics report including the xCluster status in every heartbeat.
  const auto short_metric_report_interval_ms = 250;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_heartbeat_interval_ms) = short_metric_report_interval_ms * 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) =
      short_metric_report_interval_ms;

  ASSERT_OK(SetUpWithParams(
      {1}, {1}, /* replication_factor */ 1, /* num_masters */ 3, /* num_tservers */ 1));
  ASSERT_OK(SetupReplication());
  ASSERT_OK(CorrectlyPollingAllTablets(1));
  const auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_table_->id()));
  ASSERT_OK(VerifyReplicationError(consumer_table_->id(), stream_id, std::nullopt));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) =
      MonoTime::kMillisecondsPerHour;
  SleepFor(FLAGS_heartbeat_interval_ms * 2ms);

  ASSERT_OK(consumer_cluster()->StepDownMasterLeader());
  ASSERT_OK(consumer_cluster()->WaitForAllTabletServers());

  ASSERT_OK(VerifyReplicationError(
      consumer_table_->id(), stream_id, ReplicationErrorPb::REPLICATION_ERROR_UNINITIALIZED));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) =
      short_metric_report_interval_ms;
  ASSERT_OK(VerifyReplicationError(consumer_table_->id(), stream_id, std::nullopt));
}

// Test deleting inbound replication group without performing source stream cleanup.
TEST_F_EX(XClusterTest, DeleteWithoutStreamCleanup, XClusterTestNoParam) {
  constexpr int kNTabletsPerTable = 1;
  constexpr int kReplicationFactor = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(tables_vector, kReplicationFactor));

  ASSERT_OK(SetupReplication());

  ASSERT_OK(VerifyNumCDCStreams(producer_client(), producer_cluster(), /* num_streams = */ 1));
  auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_table_->id()));

  // Delete with skip_producer_stream_deletion set.
  master::DeleteUniverseReplicationRequestPB req;
  master::DeleteUniverseReplicationResponsePB resp;
  req.set_replication_group_id(kReplicationGroupId.ToString());
  req.set_skip_producer_stream_deletion(true);

  auto consumer_master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
  {
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    ASSERT_OK(consumer_master_proxy->DeleteUniverseReplication(req, &resp, &rpc));
  }
  ASSERT_FALSE(resp.has_error());

  // Make sure source stream still exists.
  ASSERT_OK(VerifyNumCDCStreams(producer_client(), producer_cluster(), /* num_streams = */ 1));

  // Delete the stream manually from the source.
  master::DeleteCDCStreamRequestPB delete_cdc_stream_req;
  master::DeleteCDCStreamResponsePB delete_cdc_stream_resp;
  delete_cdc_stream_req.add_stream_id(stream_id.ToString());
  delete_cdc_stream_req.set_force_delete(true);
  {
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    auto producer_proxy = std::make_shared<master::MasterReplicationProxy>(
        &producer_client()->proxy_cache(),
        ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
    ASSERT_OK(
        producer_proxy->DeleteCDCStream(delete_cdc_stream_req, &delete_cdc_stream_resp, &rpc));
  }

  ASSERT_OK(VerifyNumCDCStreams(producer_client(), producer_cluster(), /* num_streams = */ 0));
}

TEST_F_EX(XClusterTest, DeleteWhenSourceIsDown, XClusterTestNoParam) {
  SetAtomicFlag(true, &FLAGS_xcluster_wait_on_ddl_alter);

  // Create 2 tables with 3 tablets each.
  ASSERT_OK(SetUpWithParams({3, 3}, /*replication_factor=*/3));
  ASSERT_OK(SetupReplication());

  master::ListCDCStreamsRequestPB list_streams_req;
  master::ListCDCStreamsResponsePB list_streams_resp;
  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &producer_client()->proxy_cache(),
      ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
  {
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    ASSERT_OK(master_proxy->ListCDCStreams(list_streams_req, &list_streams_resp, &rpc));
  }
  ASSERT_EQ(list_streams_resp.streams_size(), 2);

  producer_cluster()->StopSync();

  {
    auto consumer_master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(),
        ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(2 * kRpcTimeout));
    master::DeleteUniverseReplicationRequestPB delete_req;
    master::DeleteUniverseReplicationResponsePB delete_resp;
    delete_req.set_ignore_errors(true);
    delete_req.set_replication_group_id(kReplicationGroupId.ToString());

    ASSERT_OK(consumer_master_proxy->DeleteUniverseReplication(delete_req, &delete_resp, &rpc));
    ASSERT_FALSE(delete_resp.has_error()) << delete_resp.DebugString();
  }

  ASSERT_OK(VerifyUniverseReplicationDeleted(
      consumer_cluster(), consumer_client(), kReplicationGroupId, kRpcTimeout));
  ASSERT_OK(CorrectlyPollingAllTablets(0));

  ASSERT_OK(producer_cluster()->StartSync());

  master::DeleteCDCStreamRequestPB delete_cdc_stream_req;
  master::DeleteCDCStreamResponsePB delete_cdc_stream_resp;
  for (const auto& stream : list_streams_resp.streams()) {
    delete_cdc_stream_req.add_stream_id(stream.stream_id());
  }
  delete_cdc_stream_req.set_force_delete(true);

  {
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    ASSERT_OK(master_proxy->DeleteCDCStream(delete_cdc_stream_req, &delete_cdc_stream_resp, &rpc));
  }
}

TEST_F_EX(XClusterTest, TestYbAdmin, XClusterTestNoParam) {
  // Create 2 tables with 1 tablet each.
  ASSERT_OK(SetUpWithParams({1, 1}, /*replication_factor=*/1));

  auto result = ASSERT_RESULT(CallAdmin(consumer_cluster(), "list_universe_replications"));
  ASSERT_STR_NOT_CONTAINS(result, kReplicationGroupId.ToString());

  ASSERT_OK(SetupReplication());

  result =
      ASSERT_RESULT(CallAdmin(consumer_cluster(), "list_universe_replications", "NonExistent"));
  ASSERT_STR_NOT_CONTAINS(result, kReplicationGroupId.ToString());

  // This is not a db scoped replication group so it should not be listed.
  result = ASSERT_RESULT(CallAdmin(
      consumer_cluster(), "list_universe_replications",
      producer_tables_[0]->name().namespace_id()));
  ASSERT_STR_NOT_CONTAINS(result, kReplicationGroupId.ToString());

  result = ASSERT_RESULT(CallAdmin(consumer_cluster(), "list_universe_replications"));
  ASSERT_STR_CONTAINS(result, kReplicationGroupId.ToString());

  result = ASSERT_RESULT(
      CallAdmin(consumer_cluster(), "get_universe_replication_info", kReplicationGroupId));
  ASSERT_STR_CONTAINS(result, kReplicationGroupId.ToString());
  constexpr auto host_port_str = "host: \"$0\" port: $1";
  const auto& source_addr =
      ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr();
  ASSERT_STR_CONTAINS(result, Format(host_port_str, source_addr.host(), source_addr.port()));
  const auto& consumer_addr =
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr();
  ASSERT_STR_NOT_CONTAINS(
      result, Format(host_port_str, consumer_addr.host(), consumer_addr.port()));
  ASSERT_STR_CONTAINS(result, xcluster::ShortReplicationType(XCLUSTER_NON_TRANSACTIONAL));
  ASSERT_STR_CONTAINS(result, producer_tables_[0]->id());
  ASSERT_STR_CONTAINS(result, producer_tables_[1]->id());

  ASSERT_OK(DeleteUniverseReplication());

  ASSERT_OK(SetupUniverseReplication(producer_tables_, {LeaderOnly::kFalse, Transactional::kTrue}));

  result = ASSERT_RESULT(
      CallAdmin(consumer_cluster(), "get_universe_replication_info", kReplicationGroupId));
  ASSERT_STR_CONTAINS(result, kReplicationGroupId.ToString());
  ASSERT_STR_CONTAINS(result, xcluster::ShortReplicationType(XCLUSTER_YSQL_TRANSACTIONAL));
  ASSERT_STR_CONTAINS(result, producer_tables_[0]->id());
  ASSERT_STR_CONTAINS(result, producer_tables_[1]->id());
}

}  // namespace yb
