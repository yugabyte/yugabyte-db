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
#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/util/flags.h"
#include <gtest/gtest.h>

#include "yb/common/ql_value.h"
#include "yb/common/schema.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/cdc/cdc_state_table.h"
#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_rpc.h"
#include "yb/client/yb_op.h"
#include "yb/consensus/log.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/xcluster/xcluster_test_base.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_defaults.h"
#include "yb/master/mini_master.h"
#include "yb/master/master_replication.proxy.h"

#include "yb/master/cdc_consumer_registry_service.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/xcluster_consumer.h"
#include "yb/tserver/xcluster_poller.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/faststring.h"
#include "yb/util/metrics.h"
#include "yb/util/random.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"
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
DECLARE_bool(allow_ycql_transactional_xcluster);
DECLARE_int32(async_replication_idle_delay_ms);
DECLARE_int32(async_replication_max_idle_wait);
DECLARE_int32(async_replication_polling_delay_ms);
DECLARE_int32(cdc_wal_retention_time_secs);
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
DECLARE_int32(replication_failure_delay_exponent);
DECLARE_int64(rpc_throttle_threshold_bytes);
DECLARE_int32(transaction_table_num_tablets);
DECLARE_int32(transaction_table_num_tablets);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_bool(use_client_to_server_encryption);
DECLARE_bool(use_node_to_node_encryption);
DECLARE_bool(xcluster_wait_on_ddl_alter);
DECLARE_int32(yb_num_shards_per_tserver);
DECLARE_bool(TEST_xcluster_disable_delete_old_pollers);
DECLARE_bool(enable_log_retention_by_op_idx);
DECLARE_bool(TEST_xcluster_disable_poller_term_check);

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

class XClusterTestNoParam : public XClusterTestBase {
 public:
  Result<std::vector<std::shared_ptr<client::YBTable>>> SetUpWithParams(
      const std::vector<uint32_t>& num_consumer_tablets,
      const std::vector<uint32_t>& num_producer_tablets,
      uint32_t replication_factor,
      uint32_t num_masters = 1,
      uint32_t num_tservers = 1) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_num_tablets) = 1;
    XClusterTestBase::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_num_shards_per_tserver) = 1;
    bool transactional_table = GetTestParam().transactional_table;
    num_tservers = std::max(num_tservers, replication_factor);

    MiniClusterOptions opts;
    opts.num_tablet_servers = num_tservers;
    opts.num_masters = num_masters;
    opts.transaction_table_num_tablets = FLAGS_transaction_table_num_tablets;
    RETURN_NOT_OK(InitClusters(opts));

    RETURN_NOT_OK(clock_->Init());
    producer_cluster_.txn_mgr_.emplace(producer_client(), clock_, client::LocalTabletFilter());
    consumer_cluster_.txn_mgr_.emplace(consumer_client(), clock_, client::LocalTabletFilter());

    YBSchemaBuilder b;
    b.AddColumn("c0")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();

    // Create transactional table.
    TableProperties table_properties;
    table_properties.SetTransactional(transactional_table);
    b.SetTableProperties(table_properties);
    CHECK_OK(b.Build(&schema_));

    YBSchema consumer_schema;
    table_properties.SetDefaultTimeToLive(0);
    b.SetTableProperties(table_properties);
    CHECK_OK(b.Build(&consumer_schema));

    if (num_consumer_tablets.size() != num_producer_tablets.size()) {
      return STATUS(IllegalState,
                    Format("Num consumer tables: $0 num producer tables: $1 must be equal.",
                           num_consumer_tablets.size(), num_producer_tablets.size()));
    }

    std::vector<YBTableName> tables;
    std::vector<std::shared_ptr<client::YBTable>> yb_tables;
    for (uint32_t i = 0; i < num_consumer_tablets.size(); i++) {
      RETURN_NOT_OK(CreateTable(i, num_producer_tablets[i], producer_client(), &tables));

      std::shared_ptr<client::YBTable> producer_table;
      RETURN_NOT_OK(producer_client()->OpenTable(tables[i * 2], &producer_table));
      yb_tables.push_back(producer_table);

      RETURN_NOT_OK(CreateTable(i, num_consumer_tablets[i], consumer_client(),
                                consumer_schema, &tables));
      std::shared_ptr<client::YBTable> consumer_table;
      RETURN_NOT_OK(consumer_client()->OpenTable(tables[(i * 2) + 1], &consumer_table));
      yb_tables.push_back(consumer_table);
    }

    RETURN_NOT_OK(WaitForLoadBalancersToStabilize());

    return yb_tables;
  }

  virtual XClusterTestParams GetTestParam() {
    return XClusterTestParams(false /* transactional_table */);
  }

  Result<YBTableName> CreateTable(
      YBClient* client, const std::string& namespace_name, const std::string& table_name,
      uint32_t num_tablets) {
    return XClusterTestBase::CreateTable(client, namespace_name, table_name, num_tablets, &schema_);
  }

  Status CreateTable(
      uint32_t idx, uint32_t num_tablets, YBClient* client, std::vector<YBTableName>* tables) {
    auto table = VERIFY_RESULT(
        CreateTable(client, kNamespaceName, Format("test_table_$0", idx), num_tablets));
    tables->push_back(table);
    return Status::OK();
  }

  Status CreateTable(uint32_t idx, uint32_t num_tablets, YBClient* client, YBSchema schema,
                     std::vector<YBTableName>* tables) {
    auto table = VERIFY_RESULT(XClusterTestBase::CreateTable(
        client, kNamespaceName, Format("test_table_$0", idx), num_tablets, &schema));
    tables->push_back(table);
    return Status::OK();
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
      ASSERT_OK(session->TEST_ApplyAndFlush(op));
    }
  }

  void DeleteWorkload(uint32_t start, uint32_t end, YBClient* client, const YBTableName& table) {
    WriteWorkload(start, end, client, table, true /* delete_op */);
  }

  Status VerifyWrittenRecords(const YBTableName& producer_table,
                              const YBTableName& consumer_table,
                              int timeout_secs = kRpcTimeout) {
    std::vector<std::string> producer_results, consumer_results;
    const auto s = LoggedWaitFor(
        [this, producer_table, consumer_table, &producer_results,
         &consumer_results]() -> Result<bool> {
          producer_results = ScanTableToStrings(producer_table, producer_client());
          consumer_results = ScanTableToStrings(consumer_table, consumer_client());
          return producer_results == consumer_results;
        },
        MonoDelta::FromSeconds(timeout_secs), "Verify written records");
    if (!s.ok()) {
      LOG(ERROR) << "Producer records: " << JoinStrings(producer_results, ",")
                 << ";Consumer records: " << JoinStrings(consumer_results, ",");
    }
    return s;
  }

  Status VerifyNumRecords(const YBTableName& table, YBClient* client, size_t expected_size) {
    return LoggedWaitFor([ table, client, expected_size]() -> Result<bool> {
      auto results = ScanTableToStrings(table, client);
      return results.size() == expected_size;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify number of records");
  }

  Result<SessionTransactionPair> CreateSessionWithTransaction(
      YBClient* client, client::TransactionManager* txn_mgr) {
    auto session = client->NewSession();
    auto transaction = std::make_shared<client::YBTransaction>(txn_mgr);
    ReadHybridTime read_time;
    RETURN_NOT_OK(transaction->Init(IsolationLevel::SNAPSHOT_ISOLATION, read_time));
    session->SetTransaction(transaction);
    return std::make_pair(session, transaction);
  }

  void WriteIntents(uint32_t start, uint32_t end, YBClient* client,
                    const std::shared_ptr<YBSession>& session, const YBTableName& table,
                    bool delete_op = false) {
    client::TableHandle table_handle;
    ASSERT_OK(table_handle.Open(table, client));
    std::vector<std::shared_ptr<client::YBqlOp>> ops;

    for (uint32_t i = start; i < end; i++) {
      auto op = delete_op ? table_handle.NewDeleteOp() : table_handle.NewInsertOp();
      int32_t key = i;
      auto req = op->mutable_request();
      QLAddInt32HashValue(req, key);
      ASSERT_OK(session->TEST_ApplyAndFlush(op));
    }
  }

  void WriteTransactionalWorkload(
      uint32_t start, uint32_t end, YBClient* client, client::TransactionManager* txn_mgr,
      const YBTableName& table, bool delete_op = false) {
    auto pair = ASSERT_RESULT(CreateSessionWithTransaction(client, txn_mgr));
    ASSERT_NO_FATALS(WriteIntents(start, end, client, pair.first, table, delete_op));
    ASSERT_OK(pair.second->CommitFuture().get());
  }

  void VerifyReplicationError(
      const std::string& consumer_table_id,
      const xrepl::StreamId& stream_id,
      const boost::optional<ReplicationErrorPb> expected_replication_error) {
    // 1. Verify that the RPC contains the expected error.
    master::GetReplicationStatusRequestPB req;
    master::GetReplicationStatusResponsePB resp;

    req.set_universe_id(kReplicationGroupId.ToString());

    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(),
        ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

    rpc::RpcController rpc;
    ASSERT_OK(WaitFor([&] () -> Result<bool> {
      rpc.Reset();
      rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
      if (!master_proxy->GetReplicationStatus(req, &resp, &rpc).ok()) {
        return false;
      }

      if (resp.has_error()) {
        return false;
      }

      if (resp.statuses_size() == 0 || (resp.statuses()[0].table_id() != consumer_table_id &&
                                        resp.statuses()[0].stream_id() != stream_id.ToString())) {
        return false;
      }

      if (expected_replication_error) {
        return resp.statuses()[0].errors_size() == 1 &&
                resp.statuses()[0].errors()[0].error() == *expected_replication_error;
      } else {
        return resp.statuses()[0].errors_size() == 0;
      }
    }, MonoDelta::FromSeconds(30), "Waiting for replication error"));

    // 2. Verify that the yb-admin output contains the expected error.
    auto admin_out =
      ASSERT_RESULT(CallAdmin(consumer_cluster(), "get_replication_status", kReplicationGroupId));
    if (expected_replication_error) {
      ASSERT_TRUE(admin_out.find(
        Format("error: $0", ReplicationErrorPb_Name(*expected_replication_error))) !=
          std::string::npos);
    } else {
      ASSERT_TRUE(admin_out.find("error:") == std::string::npos);
    }
  }

  Result<xrepl::StreamId> GetCDCStreamID(const std::string& producer_table_id) {
    master::ListCDCStreamsResponsePB stream_resp;
    RETURN_NOT_OK(GetCDCStreamForTable(producer_table_id, &stream_resp));

    if (stream_resp.streams_size() != 1) {
      return STATUS(IllegalState,
                    Format("Expected 1 stream, have $0", stream_resp.streams_size()));
    }

    if (stream_resp.streams(0).table_id().Get(0) != producer_table_id) {
      return STATUS(IllegalState,
                    Format("Expected table id $0, have $1", producer_table_id,
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

    auto tables = VERIFY_RESULT(SetUpWithParams({8, 4}, {6, 6}, 3));

    std::vector<std::shared_ptr<client::YBTable>> producer_tables;
    std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
    producer_tables.reserve(tables.size() / 2);
    consumer_tables.reserve(tables.size() / 2);
    // tables contains both producer and consumer universe tables (alternately).
    for (size_t i = 0; i < tables.size(); i++) {
      if (i % 2 == 0) {
        producer_tables.push_back(tables[i]);
      } else {
        consumer_tables.push_back(tables[i]);
      }
    }

    RETURN_NOT_OK(SetupUniverseReplication(producer_tables, {LeaderOnly::kTrue, transactional}));

    // Verify that universe was setup on consumer.
    master::GetUniverseReplicationResponsePB resp;
    RETURN_NOT_OK(VerifyUniverseReplication(&resp));
    CHECK_EQ(resp.entry().producer_id(), kReplicationGroupId);
    CHECK_EQ(resp.entry().tables_size(), producer_tables.size());
    for (uint32_t i = 0; i < producer_tables.size(); i++) {
      CHECK_EQ(resp.entry().tables(i), producer_tables[i]->id());
    }

    // Verify that CDC streams were created on producer for all tables.
    for (size_t i = 0; i < producer_tables.size(); i++) {
      master::ListCDCStreamsResponsePB stream_resp;
      RETURN_NOT_OK(GetCDCStreamForTable(producer_tables[i]->id(), &stream_resp));
      CHECK_EQ(stream_resp.streams_size(), 1);
      CHECK_EQ(stream_resp.streams(0).table_id().Get(0), producer_tables[i]->id());
    }

    for (size_t i = 0; i < producer_tables.size(); i++) {
      WriteWorkload(0, 5, producer_client(), producer_tables[i]->name());
      RETURN_NOT_OK(VerifyWrittenRecords(producer_tables[i]->name(), consumer_tables[i]->name()));
    }

    RETURN_NOT_OK(DeleteUniverseReplication());
    return Status::OK();
  }

  void WriteWorkloadAndVerifyWrittenRows(
      const std::shared_ptr<client::YBTable>& producer_table,
      const std::shared_ptr<client::YBTable>& consumer_table, uint32_t start, uint32_t end,
      bool replication_enabled = true, int timeout = kRpcTimeout) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(start, end, producer_client(), producer_table->name());
    if (replication_enabled) {
      ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name(), timeout));
    } else {
      ASSERT_NOK(VerifyWrittenRecords(producer_table->name(), consumer_table->name(), timeout));
    }
  }

  // Empty stream_ids will pause all streams.
  void SetPauseAndVerifyWrittenRows(
      const std::vector<xrepl::StreamId>& stream_ids,
      const std::vector<std::shared_ptr<client::YBTable>>& producer_tables,
      const std::vector<std::shared_ptr<client::YBTable>>& consumer_tables, uint32_t start,
      uint32_t end, bool pause = true) {
    ASSERT_OK(PauseResumeXClusterProducerStreams(stream_ids, pause));
    // Needs to sleep to wait for heartbeat to propogate.
    SleepFor(3s * kTimeMultiplier);

    // If stream_ids is empty, then write and test replication on all streams, otherwise write and
    // test on only those selected in stream_ids.
    size_t size = stream_ids.size() ? stream_ids.size() : producer_tables.size();
    for (size_t i = 0; i < size; i++) {
      const auto& producer_table = producer_tables[i];
      const auto& consumer_table = consumer_tables[i];
      // Reduce the timeout time when we don't expect replication to be successful.
      int timeout = pause ? 10 : kRpcTimeout;
      WriteWorkloadAndVerifyWrittenRows(
          producer_table, consumer_table, start, end, !pause, timeout);
    }
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

  Status TestSetupNamespaceReplicationWithBootstrap(
      const std::vector<uint32_t>& tables_vector,
      master::SetupNamespaceReplicationWithBootstrapResponsePB* resp,
      bool use_invalid_namespace = false) {
    auto tables = VERIFY_RESULT(SetUpWithParams(tables_vector, tables_vector, 3));

    std::unique_ptr<client::YBClient> client;
    std::unique_ptr<cdc::CDCServiceProxy> producer_cdc_proxy;
    client = VERIFY_RESULT(consumer_cluster()->CreateClient());
    producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
        &client->proxy_cache(),
        HostPort::FromBoundEndpoint(producer_cluster()->mini_tablet_server(0)->bound_rpc_addr()));

    // tables contains both producer and consumer universe tables (alternately).
    // Pick out just the producer tables from the list.
    std::vector<std::shared_ptr<client::YBTable>> producer_tables;
    std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
    producer_tables.reserve(tables.size() / 2);
    consumer_tables.reserve(tables.size() / 2);
    for (size_t i = 0; i < tables.size(); i++) {
      if (i % 2 == 0) {
        producer_tables.push_back(tables[i]);
      } else {
        consumer_tables.push_back(tables[i]);
      }
    }

    // 1. Write some data so that we can verify that only new records get replicated
    for (const auto& producer_table : producer_tables) {
      LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
      WriteWorkload(0, 100, producer_client(), producer_table->name());
    }

    master::NamespaceIdentifierPB producer_namespace;
    if (use_invalid_namespace) {
      producer_namespace.set_id("invalid_id");
      producer_namespace.set_name("invalid_name");
      producer_namespace.set_database_type(::yb::YQLDatabase::YQL_DATABASE_UNKNOWN);
    } else {
      SCHECK(!producer_tables.empty(), IllegalState, "Producer tables are empty");
      producer_tables.front()->name().SetIntoNamespaceIdentifierPB(&producer_namespace);
    }

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
      const std::size_t& tables_count,
      const int tablets_per_table) {
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

 private:
  server::ClockPtr clock_{new server::HybridClock()};

  YBSchema schema_;
};

class XClusterTest : public XClusterTestNoParam,
                     public testing::WithParamInterface<XClusterTestParams> {
 public:
  XClusterTestParams GetTestParam() override { return GetParam(); }
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
  auto tables = ASSERT_RESULT(SetUpWithParams({1}, {1}, 1));
  rpc::RpcController rpc;
  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  {
    rpc.Reset();
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    master::SetupUniverseReplicationRequestPB setup_universe_req;
    master::SetupUniverseReplicationResponsePB setup_universe_resp;
    ASSERT_OK(master_proxy->SetupUniverseReplication(
      setup_universe_req, &setup_universe_resp, &rpc));
    ASSERT_TRUE(setup_universe_resp.has_error());
    std::string prefix = "Producer universe ID must be provided";
    ASSERT_TRUE(setup_universe_resp.error().status().message().substr(0, prefix.size()) == prefix);
  }

  {
    rpc.Reset();
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    master::SetupUniverseReplicationRequestPB setup_universe_req;
    setup_universe_req.set_producer_id(kReplicationGroupId.ToString());
    master::SetupUniverseReplicationResponsePB setup_universe_resp;
    ASSERT_OK(master_proxy->SetupUniverseReplication(
      setup_universe_req, &setup_universe_resp, &rpc));
    ASSERT_TRUE(setup_universe_resp.has_error());
    std::string prefix = "Producer master address must be provided";
    ASSERT_TRUE(setup_universe_resp.error().status().message().substr(0, prefix.size()) == prefix);
  }

  {
    rpc.Reset();
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    master::SetupUniverseReplicationRequestPB setup_universe_req;
    setup_universe_req.set_producer_id(kReplicationGroupId.ToString());
    string master_addr = producer_cluster()->GetMasterAddresses();
    auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, setup_universe_req.mutable_producer_master_addresses());
    setup_universe_req.add_producer_table_ids("a");
    setup_universe_req.add_producer_table_ids("b");
    setup_universe_req.add_producer_bootstrap_ids("c");
    master::SetupUniverseReplicationResponsePB setup_universe_resp;
    ASSERT_OK(master_proxy->SetupUniverseReplication(
      setup_universe_req, &setup_universe_resp, &rpc));
    ASSERT_TRUE(setup_universe_resp.has_error());
    std::string prefix = "Number of bootstrap ids must be equal to number of tables";
    ASSERT_TRUE(setup_universe_resp.error().status().message().substr(0, prefix.size()) == prefix);
  }

  {
    rpc.Reset();
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    master::SetupUniverseReplicationRequestPB setup_universe_req;
    master::SetupUniverseReplicationResponsePB setup_universe_resp;
    setup_universe_req.set_producer_id(kReplicationGroupId.ToString());
    string master_addr = consumer_cluster()->GetMasterAddresses();
    auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, setup_universe_req.mutable_producer_master_addresses());

    setup_universe_req.add_producer_table_ids("prod_table_id_1");
    setup_universe_req.add_producer_table_ids("prod_table_id_2");
    setup_universe_req.add_producer_bootstrap_ids("prod_bootstrap_id_1");
    setup_universe_req.add_producer_bootstrap_ids("prod_bootstrap_id_2");

    ASSERT_OK(master_proxy->SetupUniverseReplication(
      setup_universe_req, &setup_universe_resp, &rpc));
    ASSERT_TRUE(setup_universe_resp.has_error());
    std::string substring = "belongs to the target universe";
    ASSERT_TRUE(setup_universe_resp.error().status().message().find(substring) != string::npos);
  }

  {
    rpc.Reset();
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    master::SetupUniverseReplicationRequestPB setup_universe_req;
    master::SetupUniverseReplicationResponsePB setup_universe_resp;
    master::SysClusterConfigEntryPB cluster_info;
    auto& cm = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->catalog_manager();
    CHECK_OK(cm.GetClusterConfig(&cluster_info));
    setup_universe_req.set_producer_id(cluster_info.cluster_uuid());

    string master_addr = producer_cluster()->GetMasterAddresses();
    auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, setup_universe_req.mutable_producer_master_addresses());

    setup_universe_req.add_producer_table_ids("prod_table_id_1");
    setup_universe_req.add_producer_table_ids("prod_table_id_2");
    setup_universe_req.add_producer_bootstrap_ids("prod_bootstrap_id_1");
    setup_universe_req.add_producer_bootstrap_ids("prod_bootstrap_id_2");

    ASSERT_OK(master_proxy->SetupUniverseReplication(
      setup_universe_req, &setup_universe_resp, &rpc));
    ASSERT_TRUE(setup_universe_resp.has_error());
    std::string prefix = "The request UUID and cluster UUID are identical.";
    ASSERT_TRUE(setup_universe_resp.error().status().message().substr(0, prefix.size()) == prefix);
  }
}

TEST_P(XClusterTest, SetupNamespaceReplicationWithBootstrap) {
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  master::SetupNamespaceReplicationWithBootstrapResponsePB resp;

  ASSERT_OK(TestSetupNamespaceReplicationWithBootstrap(tables_vector, &resp));
  ASSERT_FALSE(resp.has_error());
  ASSERT_OK(VerifyCdcStateTableEntriesExistForTables(tables_vector.size(), kNTabletsPerTable));
  // TODO: Test SetupUniverseReplication once the flow has been completed.
}

TEST_P(XClusterTest, SetupNamespaceReplicationWithBootstrapFailure) {
  std::vector<uint32_t> tables_vector;
  master::SetupNamespaceReplicationWithBootstrapResponsePB resp;

  ASSERT_OK(TestSetupNamespaceReplicationWithBootstrap(
      tables_vector, &resp,
      /* use_invalid_namespace = */ true));
  ASSERT_TRUE(resp.has_error());
}

TEST_P(XClusterTest, SetupUniverseReplicationWithProducerBootstrapId) {
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 3));

  std::unique_ptr<client::YBClient> client;
  std::unique_ptr<cdc::CDCServiceProxy> producer_cdc_proxy;
  client = ASSERT_RESULT(consumer_cluster()->CreateClient());
  producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
      &client->proxy_cache(),
      HostPort::FromBoundEndpoint(producer_cluster()->mini_tablet_server(0)->bound_rpc_addr()));

  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  producer_tables.reserve(tables.size() / 2);
  consumer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); i++) {
    if (i % 2 == 0) {
      producer_tables.push_back(tables[i]);
    } else {
      consumer_tables.push_back(tables[i]);
    }
  }

  // 1. Write some data so that we can verify that only new records get replicated
  for (const auto& producer_table : producer_tables) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(0, 100, producer_client(), producer_table->name());
  }

  cdc::BootstrapProducerRequestPB req;
  cdc::BootstrapProducerResponsePB resp;

  for (const auto& producer_table : producer_tables) {
    req.add_table_ids(producer_table->id());
  }

  rpc::RpcController rpc;
  ASSERT_OK(producer_cdc_proxy->BootstrapProducer(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error());

  ASSERT_EQ(resp.cdc_bootstrap_ids().size(), producer_tables.size());

  int table_idx = 0;
  for (const auto& bootstrap_id : resp.cdc_bootstrap_ids()) {
    LOG(INFO) << "Got bootstrap id " << bootstrap_id
              << " for table " << producer_tables[table_idx++]->name().table_name();
  }
  ASSERT_OK(VerifyCdcStateTableEntriesExistForTables(tables_vector.size(), kNTabletsPerTable));

  // Map table -> bootstrap_id. We will need when setting up replication.
  std::unordered_map<TableId, std::string> table_bootstrap_ids;
  for (int i = 0; i < resp.cdc_bootstrap_ids_size(); i++) {
    table_bootstrap_ids[req.table_ids(i)] = resp.cdc_bootstrap_ids(i);
  }

  // 2. Setup replication.
  master::SetupUniverseReplicationRequestPB setup_universe_req;
  master::SetupUniverseReplicationResponsePB setup_universe_resp;
  setup_universe_req.set_producer_id(kReplicationGroupId.ToString());
  string master_addr = producer_cluster()->GetMasterAddresses();
  auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
  HostPortsToPBs(hp_vec, setup_universe_req.mutable_producer_master_addresses());

  setup_universe_req.mutable_producer_table_ids()->Reserve(
      narrow_cast<int>(producer_tables.size()));
  for (const auto& producer_table : producer_tables) {
    setup_universe_req.add_producer_table_ids(producer_table->id());
    const auto& iter = table_bootstrap_ids.find(producer_table->id());
    ASSERT_NE(iter, table_bootstrap_ids.end());
    setup_universe_req.add_producer_bootstrap_ids(iter->second);
  }

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  rpc.Reset();
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  ASSERT_OK(master_proxy->SetupUniverseReplication(
    setup_universe_req, &setup_universe_resp, &rpc));
  ASSERT_FALSE(setup_universe_resp.has_error());

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(&get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<int32_t>(tables_vector.size() * kNTabletsPerTable)));

  // 4. Write more data.
  for (const auto& producer_table : producer_tables) {
    WriteWorkload(1000, 1005, producer_client(), producer_table->name());
  }

  // 5. Verify that only new writes get replicated to consumer since we bootstrapped the producer
  // after we had already written some data, therefore the old data (whatever was there before we
  // bootstrapped the producer) should not be replicated.
  auto data_replicated_correctly = [&]() {
    for (const auto& consumer_table : consumer_tables) {
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
  ASSERT_OK(WaitFor([&]() { return data_replicated_correctly(); },
                    MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));
}

// Test for #2250 to verify that replication for tables with the same prefix gets set up correctly.
TEST_P(XClusterTest, SetupUniverseReplicationMultipleTables) {
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
  ASSERT_OK(SetupUniverseReplication(producer_tables));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().producer_id(), kReplicationGroupId);
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
  auto tables = ASSERT_RESULT(SetUpWithParams({}, {}, 1));
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
        ASSERT_RESULT(CreateTable(consumer_client(), kNamespaceName, cur_table, 3));
        auto t = ASSERT_RESULT(CreateTable(producer_client(), kNamespaceName, cur_table, 3));
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
      ASSERT_EQ(resp.entry().producer_id(), kReplicationGroupId);
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
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, kReplicationFactor));

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

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  producer_tables.reserve(tables.size() / 2);
  consumer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); i++) {
    if (i % 2 == 0) {
      producer_tables.push_back(tables[i]);
    } else {
      consumer_tables.push_back(tables[i]);
    }
  }

  // Bootstrap producer tables
  std::unordered_map<TableId, string> table_bootstrap_ids;  // Map table -> bootstrap_id.
  {
    cdc::BootstrapProducerRequestPB bootstrap_req;
    cdc::BootstrapProducerResponsePB bootstrap_resp;
    for (const auto& table : producer_tables) {
      bootstrap_req.add_table_ids(table->id());
    }

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    ASSERT_OK(producer_cdc_proxy->BootstrapProducer(bootstrap_req, &bootstrap_resp, &rpc));
    ASSERT_FALSE(bootstrap_resp.has_error());
    ASSERT_EQ(bootstrap_resp.cdc_bootstrap_ids().size(), producer_tables.size());

    for (int i = 0; i < bootstrap_resp.cdc_bootstrap_ids_size(); i++) {
      table_bootstrap_ids[bootstrap_req.table_ids(i)] = bootstrap_resp.cdc_bootstrap_ids(i);
    }
  }

  // Set up replication with bootstrap IDs.
  {
    master::SetupUniverseReplicationRequestPB setup_universe_req;
    master::SetupUniverseReplicationResponsePB setup_universe_resp;
    setup_universe_req.set_producer_id(kReplicationGroupId.ToString());
    string master_addrs = producer_cluster()->GetMasterAddresses();
    auto hps = ASSERT_RESULT(HostPort::ParseStrings(master_addrs, 0));
    HostPortsToPBs(hps, setup_universe_req.mutable_producer_master_addresses());
    for (size_t i = 0; i < kNumTables; i++) {
      setup_universe_req.add_producer_table_ids(producer_tables[i]->id());
      const auto& it = table_bootstrap_ids.find(producer_tables[i]->id());
      ASSERT_NE(it, table_bootstrap_ids.end());
      setup_universe_req.add_producer_bootstrap_ids(it->second);
    }

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    ASSERT_OK(consumer_master_proxy->SetupUniverseReplication(
        setup_universe_req, &setup_universe_resp, &rpc));
    ASSERT_FALSE(setup_universe_resp.has_error());

    // Verify that replication is correctly setup, and consumer has all tables.
    ASSERT_OK(LoggedWaitFor([&]() {
      master::GetUniverseReplicationResponsePB get_resp;
      return VerifyUniverseReplication(&get_resp).ok() &&
             get_resp.entry().tables_size() == kNumTables;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify table created with setup."));
  }

  // Verify that all streams are set to ACTIVE state.
  {
    string state_active = master::SysCDCStreamEntryPB::State_Name(
        master::SysCDCStreamEntryPB_State::SysCDCStreamEntryPB_State_ACTIVE);
    master::ListCDCStreamsRequestPB list_req;
    master::ListCDCStreamsResponsePB list_resp;
    list_req.set_id_type(yb::master::IdTypePB::TABLE_ID);

    ASSERT_OK(LoggedWaitFor([&]() {
        rpc::RpcController rpc;
        rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
        return producer_master_proxy->ListCDCStreams(list_req, &list_resp, &rpc).ok() &&
            !list_resp.has_error() &&
            list_resp.streams_size() == kNumTables;  // One stream per table.
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify stream creation"));

    std::vector<string> stream_states;
    for (int i = 0; i < list_resp.streams_size(); i++) {
      auto options = list_resp.streams(i).options();
      for (const auto& option : options) {
        if (option.has_key() && option.key() == "state" && option.has_value()) {
          stream_states.push_back(option.value());
        }
      }
    }

    ASSERT_TRUE(stream_states.size() == kNumTables && std::all_of(
        stream_states.begin(), stream_states.end(),
        [&](string state) { return state == state_active; }));
  }

  // Verify that replication is working.
  for (int i = 0; i < kNumTables; i++) {
    WriteWorkload(0, 5*(i+1), producer_client(), producer_tables[i]->name());
    ASSERT_OK(VerifyWrittenRecords(producer_tables[i]->name(), consumer_tables[i]->name()));
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
  auto tables = ASSERT_RESULT(SetUpWithParams({}, {}, replication_factor, 1, tserver_count));
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
      std::vector<std::shared_ptr<client::YBTable>> producer_tables;
      for (int i = 0; i < table_count * amplification[a]; i++) {
        std::string cur_table =
            table_prefix + std::to_string(amplification[a]) + "-" + std::to_string(i);
        ASSERT_RESULT(CreateTable(consumer_client(), kNamespaceName, cur_table,
                                  tablet_count));
        auto t = ASSERT_RESULT(CreateTable(producer_client(), kNamespaceName, cur_table,
                                           tablet_count));
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

        auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
            &producer_client()->proxy_cache(),
            ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

        master::IsBootstrapRequiredRequestPB req;
        master::IsBootstrapRequiredResponsePB resp;
        for (const auto& producer_table : producer_tables) {
          req.add_table_ids(producer_table->id());
        }
        rpc::RpcController rpc;
        rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

        ASSERT_OK(master_proxy->IsBootstrapRequired(req, &resp, &rpc));
        SCOPED_TRACE(resp.DebugString());
        ASSERT_FALSE(resp.has_error());
        ASSERT_EQ(resp.results_size(), producer_tables.size());
        for (const auto& result : resp.results()) {
          ASSERT_TRUE(result.has_bootstrap_required() && !result.bootstrap_required());
        }

        is_bootstrap_required_latency[a] = CoarseMonoClock::Now() - start_time;
        LOG(INFO) << "IsBootstrapRequired [" << a << "] took: "
                  << is_bootstrap_required_latency[a].ToSeconds() << "s";
      }

      // Performance test of BootstrapProducer.
      cdc::BootstrapProducerResponsePB boot_resp;
      {
        cdc::BootstrapProducerRequestPB req;

        for (const auto& producer_table : producer_tables) {
          req.add_table_ids(producer_table->id());
        }

        auto start_time = CoarseMonoClock::Now();

        auto producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
            &producer_client()->proxy_cache(),
            HostPort::FromBoundEndpoint(
                producer_cluster()->mini_tablet_server(0)->bound_rpc_addr()));
        rpc::RpcController rpc;
        ASSERT_OK(producer_cdc_proxy->BootstrapProducer(req, &boot_resp, &rpc));
        ASSERT_FALSE(boot_resp.has_error());
        ASSERT_EQ(boot_resp.cdc_bootstrap_ids().size(), producer_tables.size());

        bootstrap_latency[a] = CoarseMonoClock::Now() - start_time;
        LOG(INFO) << "BootstrapProducer [" << a << "] took: " << bootstrap_latency[a].ToSeconds()
                  << "s";
      }

      // Performance test of SetupReplication, with Bootstrap IDs.
      {
        auto start_time = CoarseMonoClock::Now();

        // Calling the SetupUniverse API directly so we can use producer_bootstrap_ids.
        master::SetupUniverseReplicationRequestPB req;
        master::SetupUniverseReplicationResponsePB resp;
        req.set_producer_id(kReplicationGroupId.ToString());
        auto master_addrs = producer_cluster()->GetMasterAddresses();
        auto vec = ASSERT_RESULT(HostPort::ParseStrings(master_addrs, 0));
        HostPortsToPBs(vec, req.mutable_producer_master_addresses());
        for (const auto& table : producer_tables) {
          req.add_producer_table_ids(table->id());
        }
        for (const auto& bootstrap_id : boot_resp.cdc_bootstrap_ids()) {
          req.add_producer_bootstrap_ids(bootstrap_id);
        }

        auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
            &consumer_client()->proxy_cache(),
            ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
        ASSERT_OK(WaitFor(
            [&]() -> Result<bool> {
              rpc::RpcController rpc;
              rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
              if (!master_proxy->SetupUniverseReplication(req, &resp, &rpc).ok()) {
                return false;
              }
              if (resp.has_error()) {
                return false;
              }
              return true;
            },
            MonoDelta::FromSeconds(30), "Setup universe replication"));

        // Verify that universe was setup on consumer.
        {
          master::GetUniverseReplicationResponsePB resp;
          ASSERT_OK(VerifyUniverseReplication( &resp));
          ASSERT_EQ(resp.entry().producer_id(), kReplicationGroupId);
          ASSERT_EQ(resp.entry().tables_size(), producer_tables.size());
          for (uint32_t i = 0; i < producer_tables.size(); i++) {
            ASSERT_EQ(resp.entry().tables(i), producer_tables[i]->id());
          }
        }

        setup_latency[a] = CoarseMonoClock::Now() - start_time;
        LOG(INFO) << "SetupReplication [" << a << "] took: " << setup_latency[a].ToSeconds() << "s";
      }

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_failure_delay_exponent) = 7; // 2^7 == 128ms

  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({4}, {4}, replication_factor));

  ASSERT_OK(SetupUniverseReplication({tables[0]} /* all producer tables */));

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));

  consumer_cluster()->mini_tablet_server(0)->Shutdown();

  // After shutting down a single consumer node, the other consumers should pick up the slack.
  if (replication_factor > 1) {
    ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));
  }

  ASSERT_OK(consumer_cluster()->mini_tablet_server(0)->Start(
    tserver::WaitTabletsBootstrapped::kFalse));

  // After restarting the node.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));

  ASSERT_OK(consumer_cluster()->RestartSync());

  // After consumer cluster restart.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, PollWithProducerNodesRestart) {
  // Avoid long delays with node failures so we can run with more aggressive test timing
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_failure_delay_exponent) = 7; // 2^7 == 128ms

  uint32_t replication_factor = 3, tablet_count = 4, master_count = 3;
  auto tables = ASSERT_RESULT(
      SetUpWithParams({tablet_count}, {tablet_count}, replication_factor, master_count));
  ASSERT_OK(
      SetupUniverseReplication({tables[0]} /* all producer tables */,
                               {LeaderOnly::kFalse, Transactional::kFalse}));

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));

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
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));
  WriteWorkload(0, 5, producer_client(), tables[0]->name());
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  // Restart the Producer TServer and verify that rebalancing happens.
  ASSERT_OK(old_master->Start());
  ASSERT_OK(producer_cluster()->mini_tablet_server(0)->Start(
    tserver::WaitTabletsBootstrapped::kFalse));
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));
  WriteWorkload(6, 10, producer_client(), tables[0]->name());
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  // Cleanup.
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, PollWithProducerClusterRestart) {
  // Avoid long delays with node failures so we can run with more aggressive test timing
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_failure_delay_exponent) = 7; // 2^7 == 128ms

  uint32_t replication_factor = 3, tablet_count = 4;
  auto tables = ASSERT_RESULT(
      SetUpWithParams({tablet_count}, {tablet_count}, replication_factor));

  ASSERT_OK(SetupUniverseReplication({tables[0]} /* all producer tables */));

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));

  // Restart the ENTIRE Producer cluster.
  ASSERT_OK(producer_cluster()->RestartSync());

  // After producer cluster restart.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 4));
  WriteWorkload(0, 5, producer_client(), tables[0]->name());
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  // Cleanup.
  ASSERT_OK(DeleteUniverseReplication());
}


TEST_P(XClusterTest, PollAndObserveIdleDampening) {
  uint32_t replication_factor = 3, tablet_count = 1, master_count = 1;
  auto tables = ASSERT_RESULT(
      SetUpWithParams({tablet_count}, {tablet_count}, replication_factor,  master_count));

  ASSERT_OK(SetupUniverseReplication({tables[0]}, {LeaderOnly::kFalse, Transactional::kFalse}));

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 1));

  // Write some Info and query GetChanges to setup the CDCTabletMetrics.
  WriteWorkload(0, 5, producer_client(), tables[0]->name());
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  /*****************************************************************
   * Find the CDC Tablet Metrics, which we will use for this test. *
   *****************************************************************/
  // Find the stream.
  master::ListCDCStreamsResponsePB stream_resp;
  ASSERT_OK(GetCDCStreamForTable(tables[0]->id(), &stream_resp));
  ASSERT_EQ(stream_resp.streams_size(), 1);
  ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), tables[0]->id());
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
    ASSERT_OK(WaitFor([this, &tablet_id, &table = tables[0], &ts_uuid, &data_mutex] {
        producer_client()->LookupTabletById(
            tablet_id,
            table,
            // TODO(tablet splitting + xCluster): After splitting integration is working (+ metrics
            // support), then set this to kTrue.
            master::IncludeInactive::kFalse,
            master::IncludeDeleted::kFalse,
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
      }, MonoDelta::FromSeconds(10), "Get TS for Tablet"));

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
  std::shared_ptr<cdc::CDCTabletMetrics> metrics = std::static_pointer_cast<cdc::CDCTabletMetrics>(
      cdc_service->GetCDCTabletMetrics({{}, stream_id, tablet_id}));

  /***********************************
   * Setup Complete.  Starting test. *
   ***********************************/
  // Log the first heartbeat count for baseline
  auto first_heartbeat_count = metrics->rpc_heartbeats_responded->value();
  LOG(INFO) << "first_heartbeat_count = " << first_heartbeat_count;

  // Write some Info to the producer, which should be consumed quickly by GetChanges.
  WriteWorkload(6, 10, producer_client(), tables[0]->name());
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

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
  WriteWorkload(11, 15, producer_client(), tables[0]->name());
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

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
  auto tables = ASSERT_RESULT(SetUpWithParams({1}, {1}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(producer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 1));

  WriteWorkload(0, 5, producer_client(), tables[0]->name());

  // Check that all tablets continue to be polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 1));

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  ASSERT_OK(DeleteUniverseReplication());
}

class XClusterTestTransactionalOnly : public XClusterTest {};

INSTANTIATE_TEST_CASE_P(
    XClusterTestParams, XClusterTestTransactionalOnly,
    ::testing::Values(XClusterTestParams(true /* transactional_table */)));

TEST_P(XClusterTestTransactionalOnly, SetupUniverseReplicationWithTLSEncryption) {
  ASSERT_OK(TestSetupUniverseReplication(EnableTLSEncryption::kTrue, Transactional::kTrue));
}

TEST_P(XClusterTestTransactionalOnly, FailedSetupSystemUniverseReplication) {
  // Make sure we cannot setup replication on System tables like the transaction status table.
  constexpr int kNumTablets = 1;
  auto tables =
      ASSERT_RESULT(SetUpWithParams({kNumTablets}, {kNumTablets}, 1 /* replication_factor */));
  auto& producer_table = tables[0];

  static const client::YBTableName transaction_table_name(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
  std::shared_ptr<client::YBTable> producer_transaction_table;
  ASSERT_OK(producer_client()->OpenTable(transaction_table_name, &producer_transaction_table));

  ASSERT_NOK(SetupUniverseReplication(
      {producer_table, producer_transaction_table}, {LeaderOnly::kTrue, Transactional::kFalse}));

  ASSERT_NOK(SetupUniverseReplication(
      {producer_table, producer_transaction_table}, {LeaderOnly::kTrue, Transactional::kTrue}));
}

TEST_P(XClusterTestTransactionalOnly, ApplyOperationsWithTransactions) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {2}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(producer_tables));

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

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTestTransactionalOnly, UpdateWithinTransaction) {
  constexpr int kNumTablets = 1;
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({kNumTablets}, {kNumTablets}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(producer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), kNumTablets));

  auto txn = ASSERT_RESULT(CreateSessionWithTransaction(producer_client(), producer_txn_mgr()));
  for (bool del : {false, true}) {
    WriteIntents(1, 5, producer_client(), txn.first, tables[0]->name(), del);
  }
  ASSERT_OK(txn.second->CommitFuture().get());

  txn.first->SetTransaction(nullptr);
  client::TableHandle table_handle;
  ASSERT_OK(table_handle.Open(tables[0]->name(), producer_client()));
  auto op = table_handle.NewInsertOp();
  auto req = op->mutable_request();
  QLAddInt32HashValue(req, 0);
  ASSERT_OK(txn.first->TEST_ApplyAndFlush(op));

  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  // Check that all tablets continue to be polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), kNumTablets));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTestTransactionalOnly, TransactionsWithRestart) {
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {2}, 3));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables = { tables[0] };
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  ASSERT_OK(SetupUniverseReplication(producer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));

  auto txn = ASSERT_RESULT(CreateSessionWithTransaction(producer_client(), producer_txn_mgr()));
  // Write some transactional rows.
  WriteTransactionalWorkload(
      0, 5, producer_client(), producer_txn_mgr(), tables[0]->name(), /* delete_op */ false);

  WriteWorkload(6, 10, producer_client(), tables[0]->name());

  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));
  std::this_thread::sleep_for(5s);
  ASSERT_OK(consumer_cluster()->FlushTablets(
      tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));
  LOG(INFO) << "Restart";
  ASSERT_OK(consumer_cluster()->RestartSync());
  std::this_thread::sleep_for(5s);
  LOG(INFO) << "Commit";
  ASSERT_OK(txn.second->CommitFuture().get());

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTestTransactionalOnly, MultipleTransactions) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({1}, {1}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(producer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 1));

  auto txn_0 = ASSERT_RESULT(CreateSessionWithTransaction(producer_client(), producer_txn_mgr()));
  auto txn_1 = ASSERT_RESULT(CreateSessionWithTransaction(producer_client(), producer_txn_mgr()));

  ASSERT_NO_FATALS(WriteIntents(0, 5, producer_client(), txn_0.first, tables[0]->name()));
  ASSERT_NO_FATALS(WriteIntents(5, 10, producer_client(), txn_0.first, tables[0]->name()));
  ASSERT_NO_FATALS(WriteIntents(10, 15, producer_client(), txn_1.first, tables[0]->name()));
  ASSERT_NO_FATALS(WriteIntents(10, 20, producer_client(), txn_1.first, tables[0]->name()));

  ASSERT_OK(WaitFor([&]() {
    return CountIntents(consumer_cluster()) > 0;
  }, MonoDelta::FromSeconds(kRpcTimeout), "Consumer cluster replicated intents"));

  // Make sure that none of the intents replicated have been committed.
  auto consumer_results = ScanTableToStrings(tables[1]->name(), consumer_client());
  ASSERT_EQ(consumer_results.size(), 0);

  ASSERT_OK(txn_0.second->CommitFuture().get());
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  ASSERT_OK(txn_1.second->CommitFuture().get());
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));
  ASSERT_OK(WaitFor([&]() {
    return CountIntents(consumer_cluster()) == 0;
  }, MonoDelta::FromSeconds(kRpcTimeout), "Consumer cluster cleaned up intents"));
}

TEST_P(XClusterTestTransactionalOnly, CleanupAbortedTransactions) {
  static const int kNumRecordsPerBatch = 5;
  const uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({1 /* num_consumer_tablets */},
                                              {1 /* num_producer_tablets */},
                                              replication_factor));
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(producer_tables));
  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 1 /* num_producer_tablets */));
  auto txn_0 = ASSERT_RESULT(CreateSessionWithTransaction(producer_client(), producer_txn_mgr()));
  ASSERT_NO_FATALS(WriteIntents(0, kNumRecordsPerBatch, producer_client(), txn_0.first,
                                tables[0]->name()));
  // Wait for records to be replicated.
  ASSERT_OK(WaitFor([&]() {
    return CountIntents(consumer_cluster()) == kNumRecordsPerBatch * replication_factor;
  }, MonoDelta::FromSeconds(kRpcTimeout), "Consumer cluster created intents"));
  ASSERT_OK(consumer_cluster()->FlushTablets());
  // Then, set timeout to 0 and make sure we do cleanup on the next compaction.
  SetAtomicFlag(0, &FLAGS_external_intent_cleanup_secs);
  ASSERT_NO_FATALS(WriteIntents(kNumRecordsPerBatch, kNumRecordsPerBatch * 2, producer_client(),
                                txn_0.first, tables[0]->name()));
  // Wait for records to be replicated.
  ASSERT_OK(WaitFor([&]() {
    return CountIntents(consumer_cluster()) == 2 * kNumRecordsPerBatch * replication_factor;
  }, MonoDelta::FromSeconds(kRpcTimeout), "Consumer cluster created intents"));
  ASSERT_OK(consumer_cluster()->CompactTablets());
  ASSERT_OK(WaitFor([&]() {
    return CountIntents(consumer_cluster()) == 0;
  }, MonoDelta::FromSeconds(kRpcTimeout), "Consumer cluster cleaned up intents"));
  txn_0.second->Abort();
}

// Make sure when we compact a tablet, we retain intents.
TEST_P(XClusterTestTransactionalOnly, NoCleanupOfTransactionsInFlushedFiles) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({1}, {1}, replication_factor));
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(producer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 1));
  auto txn_0 = ASSERT_RESULT(CreateSessionWithTransaction(producer_client(), producer_txn_mgr()));
  ASSERT_NO_FATALS(WriteIntents(0, 5, producer_client(), txn_0.first, tables[0]->name()));
  auto consumer_results = ScanTableToStrings(tables[1]->name(), consumer_client());
  ASSERT_EQ(consumer_results.size(), 0);
  ASSERT_OK(consumer_cluster()->FlushTablets());
  ASSERT_NO_FATALS(WriteIntents(5, 10, producer_client(), txn_0.first, tables[0]->name()));
  ASSERT_OK(consumer_cluster()->FlushTablets());
  ASSERT_OK(consumer_cluster()->CompactTablets());
  // Wait for 5 seconds to make sure background CleanupIntents thread doesn't cleanup intents on the
  // consumer.
  SleepFor(MonoDelta::FromSeconds(5));
  ASSERT_OK(txn_0.second->CommitFuture().get());
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));
  ASSERT_OK(WaitFor([&]() {
    return CountIntents(consumer_cluster()) == 0;
  }, MonoDelta::FromSeconds(kRpcTimeout), "Consumer cluster cleaned up intents"));
}


TEST_P(XClusterTestTransactionalOnly, ManyToOneTabletMapping) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {5}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(producer_tables));
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 5));

  WriteTransactionalWorkload(0, 100, producer_client(), producer_txn_mgr(), tables[0]->name());
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name(), 60 /* timeout_secs */));
}

TEST_P(XClusterTestTransactionalOnly, OneToManyTabletMapping) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({5}, {2}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(producer_tables));
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));
  WriteTransactionalWorkload(0, 50, producer_client(), producer_txn_mgr(), tables[0]->name());
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name(), 60 /* timeout_secs */));
}

TEST_P(XClusterTestTransactionalOnly, WithBootstrap) {
  constexpr int kNumTablets = 1;
  auto tables =
      ASSERT_RESULT(SetUpWithParams({kNumTablets}, {kNumTablets}, 1 /* replication_factor */));
  auto& producer_table = tables[0];
  auto& consumer_table = tables[1];

  // 1. Write batch_size rows transactionally. This batch should never get replicated as we only
  // bootstrap after this.
  const uint32 batch_size = 10;
  WriteTransactionalWorkload(
      0, batch_size, producer_client(), producer_txn_mgr(), producer_table->name());

  // 2. Bootstrap user table.
  cdc::BootstrapProducerRequestPB req;
  cdc::BootstrapProducerResponsePB resp;
  req.add_table_ids(producer_table->id());

  rpc::RpcController rpc;
  auto producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
      &producer_client()->proxy_cache(),
      HostPort::FromBoundEndpoint(producer_cluster()->mini_tablet_server(0)->bound_rpc_addr()));
  ASSERT_OK(producer_cdc_proxy->BootstrapProducer(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error());

  ASSERT_EQ(resp.cdc_bootstrap_ids().size(), 1);

  const auto& bootstrap_id = ASSERT_RESULT(xrepl::StreamId::FromString(resp.cdc_bootstrap_ids(0)));
  LOG(INFO) << "Got bootstrap id " << bootstrap_id << " for table "
            << producer_table->name().table_name();

  // 3. Write batch_size more rows transactionally.
  WriteTransactionalWorkload(
      batch_size, 2 * batch_size, producer_client(), producer_txn_mgr(), producer_table->name());

  // 4. Flush the table and run log GC.
  ASSERT_OK(FlushProducerTabletsAndGCLog());

  // 5. Setup replication.
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kReplicationGroupId,
      {producer_table}, {bootstrap_id}, {LeaderOnly::kTrue, Transactional::kTrue}));
  master::GetUniverseReplicationResponsePB verify_repl_resp;
  ASSERT_OK(VerifyUniverseReplication(kReplicationGroupId, &verify_repl_resp));
  ASSERT_OK(ChangeXClusterRole(cdc::XClusterRole::STANDBY));

  // 6. Verify we got the rows from step 3 but not 1.
  ASSERT_OK(VerifyNumRecords(consumer_table->name(), consumer_client(), batch_size));
  ASSERT_NOK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));

  // 7. Write some more rows and make sure they are replicated.
  WriteTransactionalWorkload(
      2 * batch_size, 3 * batch_size, producer_client(), producer_txn_mgr(),
      producer_table->name());
  ASSERT_OK(VerifyNumRecords(consumer_table->name(), consumer_client(), 2 * batch_size));
}

TEST_P(XClusterTest, TestExternalWriteHybridTime) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {2}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(producer_tables));

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_write_hybrid_time) = true;
  DeleteWorkload(1, 2, producer_client(), tables[0]->name());

  // Verify that record exists on consumer universe, but is deleted from producer universe.
  ASSERT_OK(VerifyNumRecords(tables[0]->name(), producer_client(), 0));
  ASSERT_OK(VerifyNumRecords(tables[1]->name(), consumer_client(), 1));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, BiDirectionalWrites) {
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {2}, 1));

  // Setup bi-directional replication.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(producer_tables));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables_reverse;
  producer_tables_reverse.push_back(tables[1]);
  ASSERT_OK(SetupReverseUniverseReplication(producer_tables_reverse));

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
  WriteWorkload(0, 5, consumer_client(), tables[1]->name());
  WriteWorkload(5, 10, producer_client(), tables[0]->name(), true /* is_delete */);

  // Ensure that same records exist on both universes.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, BiDirectionalWritesWithLargeBatches) {
  // Simulate large batches by dropping FLAGS_consensus_max_batch_size_bytes to limit the size of
  // the batches we read in GetChanges / ReadReplicatedMessagesForCDC.
  // We want to test that we don't get stuck if an entire batch messages read are all filtered out
  // (currently we only filter out external writes) and the checkpoint isn't updated.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_consensus_max_batch_size_bytes) = 1_KB;
  constexpr auto kNumRowsToWrite = 500;
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {2}, 1));
  auto& producer_table = tables[0];
  auto& consumer_table = tables[1];

  // Setup bi-directional replication.
  ASSERT_OK(SetupUniverseReplication({producer_table}));
  ASSERT_OK(SetupReverseUniverseReplication({consumer_table}));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));
  ASSERT_OK(CorrectlyPollingAllTablets(producer_cluster(), 2));

  // Write some rows on one cluster.
  WriteWorkload(0, kNumRowsToWrite, producer_client(), producer_table->name());

  // Ensure that records are the same on both clusters.
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  // Ensure that both universes have all records.
  ASSERT_OK(VerifyNumRecords(producer_table->name(), producer_client(), kNumRowsToWrite));

  // Now write some rows on the other cluster.
  WriteWorkload(kNumRowsToWrite, 2 * kNumRowsToWrite, consumer_client(), consumer_table->name());

  // Ensure that same records exist on both universes.
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  // Ensure that both universes have all records.
  ASSERT_OK(VerifyNumRecords(producer_table->name(), producer_client(), 2 * kNumRowsToWrite));
}

TEST_P(XClusterTest, AlterUniverseReplicationMasters) {
  // Tablets = Servers + 1 to stay simple but ensure round robin gives a tablet to everyone.
  uint32_t t_count = 2;
  int master_count = 3;
  auto tables = ASSERT_RESULT(SetUpWithParams(
      {t_count, t_count}, {t_count, t_count}, 1,  master_count));

  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables{tables[0], tables[2]},
    initial_tables{tables[0]};

  // SetupUniverseReplication only utilizes 1 master.
  ASSERT_OK(SetupUniverseReplication(initial_tables));

  master::GetUniverseReplicationResponsePB v_resp;
  ASSERT_OK(VerifyUniverseReplication(&v_resp));
  ASSERT_EQ(v_resp.entry().producer_master_addresses_size(), 1);
  ASSERT_EQ(HostPortFromPB(v_resp.entry().producer_master_addresses(0)),
            ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), t_count));

  LOG(INFO) << "Alter Replication to include all Masters";
  // Alter Replication to include the other masters.
  {
    master::AlterUniverseReplicationRequestPB alter_req;
    master::AlterUniverseReplicationResponsePB alter_resp;
    alter_req.set_producer_id(kReplicationGroupId.ToString());

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
    ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
      master::GetUniverseReplicationResponsePB tmp_resp;
      return VerifyUniverseReplication(&tmp_resp).ok() &&
             tmp_resp.entry().producer_master_addresses_size() == master_count;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify master count increased."));
    ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), t_count));
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
    alter_req.set_producer_id(kReplicationGroupId.ToString());
    alter_req.add_producer_table_ids_to_add(producer_tables[1]->id());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(),
        ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
    ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));
    ASSERT_FALSE(alter_resp.has_error());

    // Verify that the consumer now has both tables in the universe.
    ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
      master::GetUniverseReplicationResponsePB tmp_resp;
      return VerifyUniverseReplication(&tmp_resp).ok() &&
             tmp_resp.entry().tables_size() == 2;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify table created with alter."));
    ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), t_count * 2));
  }

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, AlterUniverseReplicationTables) {
  // Setup the consumer and producer cluster.
  auto tables = ASSERT_RESULT(SetUpWithParams({3, 3}, {3, 3}, 1));
  std::vector<std::shared_ptr<client::YBTable>> producer_tables{tables[0], tables[2]};
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables{tables[1], tables[3]};

  // Setup universe replication on the first table.
  auto initial_table = { producer_tables[0] };
  ASSERT_OK(SetupUniverseReplication(initial_table));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB v_resp;
  ASSERT_OK(VerifyUniverseReplication(&v_resp));
  ASSERT_EQ(v_resp.entry().producer_id(), kReplicationGroupId);
  ASSERT_EQ(v_resp.entry().tables_size(), 1);
  ASSERT_EQ(v_resp.entry().tables(0), producer_tables[0]->id());

  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 3));

  // 'add_table'. Add the next table with the alter command.
  {
    master::AlterUniverseReplicationRequestPB alter_req;
    master::AlterUniverseReplicationResponsePB alter_resp;
    alter_req.set_producer_id(kReplicationGroupId.ToString());
    alter_req.add_producer_table_ids_to_add(producer_tables[1]->id());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(),
        ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
    ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));
    ASSERT_FALSE(alter_resp.has_error());

    // Verify that the consumer now has both tables in the universe.
    ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
      master::GetUniverseReplicationResponsePB tmp_resp;
      return VerifyUniverseReplication(&tmp_resp).ok() &&
             tmp_resp.entry().tables_size() == 2;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify table created with alter."));
    ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 6));
  }

  // Write some rows to the new table on the Producer. Ensure that the Consumer gets it.
  WriteWorkload(6, 10, producer_client(), producer_tables[1]->name());
  ASSERT_OK(VerifyWrittenRecords(producer_tables[1]->name(), consumer_tables[1]->name()));

  // 'remove_table'. Remove the original table, leaving only the new one.
  {
    master::AlterUniverseReplicationRequestPB alter_req;
    master::AlterUniverseReplicationResponsePB alter_resp;
    alter_req.set_producer_id(kReplicationGroupId.ToString());
    alter_req.add_producer_table_ids_to_remove(producer_tables[0]->id());
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
          return VerifyUniverseReplication(&v_resp).ok() &&
                 v_resp.entry().tables_size() == 1;
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Verify table removed with alter."));
    ASSERT_EQ(v_resp.entry().tables(0), producer_tables[1]->id());
    ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 3));
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
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, kReplicationFactor));

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

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  producer_tables.reserve(tables.size() / 2);
  consumer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); i++) {
    if (i % 2 == 0) {
      producer_tables.push_back(tables[i]);
    } else {
      consumer_tables.push_back(tables[i]);
    }
  }

  // Bootstrap producer tables.
  std::unordered_map<TableId, string> table_bootstrap_ids;  // Map table -> bootstrap_id.
  {
    cdc::BootstrapProducerRequestPB bootstrap_req;
    cdc::BootstrapProducerResponsePB bootstrap_resp;
    for (const auto& table : producer_tables) {
      bootstrap_req.add_table_ids(table->id());
    }

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    ASSERT_OK(producer_cdc_proxy->BootstrapProducer(bootstrap_req, &bootstrap_resp, &rpc));
    ASSERT_FALSE(bootstrap_resp.has_error());
    ASSERT_EQ(bootstrap_resp.cdc_bootstrap_ids().size(), producer_tables.size());

    for (int i = 0; i < bootstrap_resp.cdc_bootstrap_ids_size(); i++) {
      table_bootstrap_ids[bootstrap_req.table_ids(i)] = bootstrap_resp.cdc_bootstrap_ids(i);
    }
  }

  // Set up replication for one table first.
  {
    master::SetupUniverseReplicationRequestPB setup_universe_req;
    master::SetupUniverseReplicationResponsePB setup_universe_resp;
    setup_universe_req.set_producer_id(kReplicationGroupId.ToString());
    string master_addrs = producer_cluster()->GetMasterAddresses();
    auto hps = ASSERT_RESULT(HostPort::ParseStrings(master_addrs, 0));
    HostPortsToPBs(hps, setup_universe_req.mutable_producer_master_addresses());
    setup_universe_req.add_producer_table_ids(producer_tables[0]->id());
    const auto& it = table_bootstrap_ids.find(producer_tables[0]->id());
    ASSERT_NE(it, table_bootstrap_ids.end());
    setup_universe_req.add_producer_bootstrap_ids(it->second);

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    ASSERT_OK(consumer_master_proxy->SetupUniverseReplication(
        setup_universe_req, &setup_universe_resp, &rpc));
    ASSERT_FALSE(setup_universe_resp.has_error());
  }

  // Set up replication for the remaining tables using alter universe.
  {
    master::AlterUniverseReplicationRequestPB alter_req;
    master::AlterUniverseReplicationResponsePB alter_resp;
    alter_req.set_producer_id(kReplicationGroupId.ToString());
    for (size_t i = 1; i < kNumTables; i++) {
      alter_req.add_producer_table_ids_to_add(producer_tables[i]->id());
      const auto& it = table_bootstrap_ids.find(producer_tables[i]->id());
      ASSERT_NE(it, table_bootstrap_ids.end());
      alter_req.add_producer_bootstrap_ids_to_add(it->second);
    }

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    ASSERT_OK(consumer_master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));
    ASSERT_FALSE(alter_resp.has_error());

    // Verify that replication is correctly setup, and consumer has all tables.
    ASSERT_OK(LoggedWaitFor([&]() {
      master::GetUniverseReplicationResponsePB get_resp;
      return VerifyUniverseReplication(&get_resp).ok() &&
             get_resp.entry().tables_size() == kNumTables;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify table created with alter."));
  }

  // Verify that all streams are set to ACTIVE state.
  {
    string state_active = master::SysCDCStreamEntryPB::State_Name(
        master::SysCDCStreamEntryPB_State::SysCDCStreamEntryPB_State_ACTIVE);
    master::ListCDCStreamsRequestPB list_req;
    master::ListCDCStreamsResponsePB list_resp;
    list_req.set_id_type(yb::master::IdTypePB::TABLE_ID);

    ASSERT_OK(LoggedWaitFor([&]() {
        rpc::RpcController rpc;
        rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
        return producer_master_proxy->ListCDCStreams(list_req, &list_resp, &rpc).ok() &&
            !list_resp.has_error() &&
            list_resp.streams_size() == kNumTables;  // One stream per table.
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify stream creation"));

    std::vector<string> stream_states;
    for (int i = 0; i < list_resp.streams_size(); i++) {
      auto options = list_resp.streams(i).options();
      for (const auto& option : options) {
        if (option.has_key() && option.key() == "state" && option.has_value()) {
          stream_states.push_back(option.value());
        }
      }
    }
    ASSERT_TRUE(stream_states.size() == kNumTables && std::all_of(
        stream_states.begin(), stream_states.end(),
        [&](string state) { return state == state_active; }));
  }

  // Verify that replication is working.
  for (int i = 0; i < kNumTables; i++) {
    WriteWorkload(0, 5*(i+1), producer_client(), producer_tables[i]->name());
    ASSERT_OK(VerifyWrittenRecords(producer_tables[i]->name(), consumer_tables[i]->name()));
  }

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, ToggleReplicationEnabled) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {2}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(producer_tables));

  // Verify that universe is now ACTIVE
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));

  // After we know the universe is ACTIVE, make sure all tablets are getting polled.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));

  // Disable the replication and ensure no tablets are being polled
  ASSERT_OK(
      ToggleUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, false));
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 0));

  // Enable replication and ensure that all the tablets start being polled again
  ASSERT_OK(
      ToggleUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, true));
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));
}

TEST_P(XClusterTest, TestDeleteUniverse) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);

  auto tables = ASSERT_RESULT(SetUpWithParams({8, 4}, {6, 6}, replication_factor));

  ASSERT_OK(SetupUniverseReplication({tables[0], tables[2]} /* all producer tables */));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 12));

  ASSERT_OK(DeleteUniverseReplication());

  ASSERT_OK(VerifyUniverseReplicationDeleted(
      consumer_cluster(), consumer_client(), kReplicationGroupId,
      FLAGS_cdc_read_rpc_timeout_ms * 2));

  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 0));
}

TEST_P(XClusterTest, TestWalRetentionSet) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_wal_retention_time_secs) = 8 * 3600;

  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({8, 4, 4, 12}, {8, 4, 12, 8}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  producer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); i += 2) {
    producer_tables.push_back(tables[i]);
  }
  ASSERT_OK(SetupUniverseReplication(producer_tables));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));

  // After creating the cluster, make sure all 32 tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 32));

  cdc::VerifyWalRetentionTime(producer_cluster(), "test_table_", FLAGS_cdc_wal_retention_time_secs);

  YBTableName table_name(YQL_DATABASE_CQL, kNamespaceName, "test_table_0");

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
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {2}, 1));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(producer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));

  WriteWorkload(0, 5, producer_client(), tables[0]->name());

  // Add new node and wait for tablets to be rebalanced.
  // After rebalancing, each node will be leader for 1 tablet.
  ASSERT_OK(producer_cluster()->AddTabletServer());
  ASSERT_OK(producer_cluster()->WaitForTabletServerCount(2));
  ASSERT_OK(WaitFor([&] () { return producer_client()->IsLoadBalanced(2); },
                    MonoDelta::FromSeconds(kRpcTimeout), "IsLoadBalanced"));

  // Check that all tablets continue to be polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));

  // Write some more rows. Note that some of these rows will have the new node as the tablet leader.
  WriteWorkload(6, 10, producer_client(), tables[0]->name());

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));
}

TEST_P(XClusterTest, TestAlterDDLBasic) {
  SetAtomicFlag(true, &FLAGS_xcluster_wait_on_ddl_alter);

  uint32_t replication_factor = 1;
  // Use just one tablet here to more easily catch lower-level write issues with this test.
  auto tables = ASSERT_RESULT(SetUpWithParams({1}, {1}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kReplicationGroupId,
      producer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 1));

  WriteWorkload(0, 5, producer_client(), tables[0]->name());

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  // Alter the CQL Table on the Producer to Add a New Column.
  LOG(INFO) << "Alter the Producer.";
  {
    std::unique_ptr<YBTableAlterer> table_alterer(producer_client()->
                                                  NewTableAlterer(tables[0]->name()));
    table_alterer->AddColumn("contact_name")->Type(DataType::STRING);
    ASSERT_OK(table_alterer->Alter());
  }

  // Write some data with the New Schema on the Producer.
  {
    auto session = producer_client()->NewSession();
    client::TableHandle table_handle;
    ASSERT_OK(table_handle.Open(tables[0]->name(), producer_client()));
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
  auto stream_id = ASSERT_RESULT(GetCDCStreamID(tables[0]->id()));

  // Verify that the replication status for the consumer table contains a schema mismatch error.
  VerifyReplicationError(
    tables[1]->id(), stream_id, ReplicationErrorPb::REPLICATION_SCHEMA_MISMATCH);

  // Alter the CQL Table on the Consumer next to match.
  LOG(INFO) << "Alter the Consumer.";
  {
    std::unique_ptr<YBTableAlterer> table_alterer(consumer_client()->
                                                  NewTableAlterer(tables[1]->name()));
    table_alterer->AddColumn("contact_name")->Type(DataType::STRING);
    ASSERT_OK(table_alterer->Alter());
  }

  // Verify that the Producer data is now sent over.
  LOG(INFO) << "Verify matching records.";
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  // Verify that the replication status for the consumer table does not contain an error.
  VerifyReplicationError(tables[1]->id(), stream_id, boost::optional<ReplicationErrorPb>());

  // Stop replication on the Consumer.
  ASSERT_OK(DeleteUniverseReplication(kReplicationGroupId));
}

TEST_P(XClusterTest, TestAlterDDLWithRestarts) {
  SetAtomicFlag(true, &FLAGS_xcluster_wait_on_ddl_alter);

  uint32_t replication_factor = 3;
  auto tables = ASSERT_RESULT(SetUpWithParams({1}, {1}, replication_factor, 2, 3));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables = {tables[0]};
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kReplicationGroupId,
      producer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 1));

  WriteWorkload(0, 5, producer_client(), tables[0]->name());

  // Check that all tablets continue to be polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 1));

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  // Alter the CQL Table on the Producer to Add a New Column.
  LOG(INFO) << "Alter the Producer.";
  {
    std::unique_ptr<YBTableAlterer> table_alterer(
        producer_client()->NewTableAlterer(tables[0]->name()));
    table_alterer->AddColumn("contact_name")->Type(DataType::STRING);
    ASSERT_OK(table_alterer->Alter());
  }

  // Write some data with the New Schema on the Producer.
  {
    auto session = producer_client()->NewSession();
    client::TableHandle table_handle;
    ASSERT_OK(table_handle.Open(tables[0]->name(), producer_client()));
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
    auto producer_results = ScanTableToStrings(tables[0]->name(), producer_client());
    auto consumer_results = ScanTableToStrings(tables[1]->name(), consumer_client());
    ASSERT_EQ(producer_results.size(), 10);
    ASSERT_EQ(consumer_results.size(), 5);
  }

  // Shutdown the Consumer TServer that contains the tablet leader.  Verify balance to new server.
  {
    auto tablet_ids = ListTabletIdsForTable(consumer_cluster(), tables[1]->id());
    ASSERT_EQ(tablet_ids.size(), 1);
    auto old_ts = FindTabletLeader(consumer_cluster(), *tablet_ids.begin());
    old_ts->Shutdown();
    const MonoTime deadline = MonoTime::Now() + 10s * kTimeMultiplier;
    ASSERT_OK(WaitUntilTabletHasLeader(consumer_cluster(), *tablet_ids.begin(), deadline));
    decltype(old_ts) new_ts = nullptr;
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      new_ts = FindTabletLeader(consumer_cluster(), *tablet_ids.begin());
      return new_ts != nullptr;
    }, MonoDelta::FromSeconds(10), "FindTabletLeader"));
    ASSERT_NE(old_ts, new_ts);
  }

  // Verify that the new Consumer TServer is in the same state as before.
  LOG(INFO) << "Verify that Consumer doesn't have inserts (after restart).";
  {
    auto producer_results = ScanTableToStrings(tables[0]->name(), producer_client());
    auto consumer_results = ScanTableToStrings(tables[1]->name(), consumer_client());
    ASSERT_EQ(producer_results.size(), 10);
    ASSERT_EQ(consumer_results.size(), 5);
  }

  // Alter the CQL Table on the Consumer next to match.
  LOG(INFO) << "Alter the Consumer.";
  {
    std::unique_ptr<YBTableAlterer> table_alterer(consumer_client()->
        NewTableAlterer(tables[1]->name()));
    table_alterer->AddColumn("contact_name")->Type(DataType::STRING);
    ASSERT_OK(table_alterer->Alter());
  }

  // Verify that the Producer data is now sent over.
  LOG(INFO) << "Verify matching records.";
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  // Stop replication on the Consumer.
  ASSERT_OK(DeleteUniverseReplication(kReplicationGroupId));
}

TEST_P(XClusterTest, ApplyOperationsRandomFailures) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  // Use unequal table count so we have M:N mapping and output to multiple tablets.
  auto tables = ASSERT_RESULT(SetUpWithParams({3}, {5}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(producer_tables));

  // Set up bi-directional replication.
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  consumer_tables.reserve(1);
  consumer_tables.push_back(tables[1]);
  ASSERT_OK(SetupReverseUniverseReplication(consumer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 5));
  ASSERT_OK(CorrectlyPollingAllTablets(producer_cluster(), 3));

  SetAtomicFlag(0.25, &FLAGS_TEST_respond_write_failed_probability);

  // Write 1000 entries to each cluster.
  std::thread t1([&]() { WriteWorkload(0, 1000, producer_client(), tables[0]->name()); });
  std::thread t2([&]() { WriteWorkload(1000, 2000, consumer_client(), tables[1]->name()); });

  t1.join();
  t2.join();

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

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
  auto tables = ASSERT_RESULT(SetUpWithParams({1}, {1}, replication_factor));

  // This test depends on the write op metrics from the tservers. If the load balancer changes the
  // leader of a consumer tablet, it may re-write replication records and break the test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;

  WriteWorkload(0, num_ops_per_workload, producer_client(), tables[0]->name());
  for (size_t i = 0; i < num_runs; i++) {
    WriteWorkload(0, num_ops_per_workload, producer_client(), tables[0]->name(), true);
    WriteWorkload(0, num_ops_per_workload, producer_client(), tables[0]->name());
  }

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(producer_tables));

  ASSERT_OK(LoggedWaitFor(
      [&]() { return GetSuccessfulWriteOps(consumer_cluster()) == 1; }, MonoDelta::FromSeconds(60),
      "Wait for all batches to finish."));

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  ASSERT_OK(consumer_cluster()->RestartSync());

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));
  // Stop replication on consumer.
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, TestDeleteCDCStreamWithMissingStreams) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);

  auto tables = ASSERT_RESULT(SetUpWithParams({8, 4}, {6, 6}, replication_factor));

  ASSERT_OK(SetupUniverseReplication({tables[0], tables[2]} /* all producer tables */));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 12));

  // Delete the CDC stream on the producer for a table.
  master::ListCDCStreamsResponsePB stream_resp;
  ASSERT_OK(GetCDCStreamForTable(tables[0]->id(), &stream_resp));
  ASSERT_EQ(stream_resp.streams_size(), 1);
  ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), tables[0]->id());
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
  ASSERT_OK(producer_proxy->DeleteCDCStream(
      delete_cdc_stream_req, &delete_cdc_stream_resp, &rpc));

  // Try to delete the universe.
  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
  rpc.Reset();
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  master::DeleteUniverseReplicationRequestPB delete_universe_req;
  master::DeleteUniverseReplicationResponsePB delete_universe_resp;
  delete_universe_req.set_producer_id(kReplicationGroupId.ToString());
  delete_universe_req.set_ignore_errors(false);
  ASSERT_OK(
      master_proxy->DeleteUniverseReplication(delete_universe_req, &delete_universe_resp, &rpc));
  // Ensure that the error message describes the missing stream and related table.
  ASSERT_TRUE(delete_universe_resp.has_error());
  std::string prefix = "Could not find the following streams:";
  const auto error_str = delete_universe_resp.error().status().message();
  ASSERT_TRUE(error_str.substr(0, prefix.size()) == prefix);
  ASSERT_NE(error_str.find(stream_id), string::npos);
  ASSERT_NE(error_str.find(tables[0]->id()), string::npos);

  // Force the delete.
  rpc.Reset();
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  delete_universe_req.set_ignore_errors(true);
  ASSERT_OK(
      master_proxy->DeleteUniverseReplication(delete_universe_req, &delete_universe_resp, &rpc));

  // Ensure that the delete is now succesful.
  ASSERT_OK(VerifyUniverseReplicationDeleted(
      consumer_cluster(), consumer_client(), kReplicationGroupId,
      FLAGS_cdc_read_rpc_timeout_ms * 2));

  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 0));
}

TEST_P(XClusterTest, TestAlterWhenProducerIsInaccessible) {
  auto tables = ASSERT_RESULT(SetUpWithParams({1}, {1}, 1));

  ASSERT_OK(SetupUniverseReplication({tables[0]} /* all producer tables */));

  // Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));

  // Stop the producer master.
  producer_cluster()->mini_master(0)->Shutdown();

  // Try to alter replication.
  master::AlterUniverseReplicationRequestPB alter_req;
  master::AlterUniverseReplicationResponsePB alter_resp;
  alter_req.set_producer_id(kReplicationGroupId.ToString());
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
  auto tables = ASSERT_RESULT(SetUpWithParams({8, 4}, {6, 6}, 3));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  producer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); i += 2) {
    producer_tables.push_back(tables[i]);
  }

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  // manually call SetupUniverseReplication to ensure it fails
  master::SetupUniverseReplicationRequestPB req;
  master::SetupUniverseReplicationResponsePB resp;
  string master_addr = producer_cluster()->GetMasterAddresses();
  auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
  HostPortsToPBs(hp_vec, req.mutable_producer_master_addresses());
  req.set_producer_id(kReplicationGroupId.ToString());
  req.mutable_producer_table_ids()->Reserve(1);
  req.add_producer_table_ids("Fake Table Id");

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  ASSERT_OK(master_proxy->SetupUniverseReplication(req, &resp, &rpc));
  // Sleep to allow the universe to be marked as failed
  std::this_thread::sleep_for(2s);

  master::GetUniverseReplicationRequestPB new_req;
  new_req.set_producer_id(kReplicationGroupId.ToString());
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
  auto tables = ASSERT_RESULT(SetUpWithParams({8, 4}, {6, 6}, 3));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  producer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); i += 2) {
    producer_tables.push_back(tables[i]);
  }

  ASSERT_OK(SetupUniverseReplication({producer_tables[0]}));

  // Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
    &consumer_client()->proxy_cache(),
    ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  // Delete The Table
  master::DeleteUniverseReplicationRequestPB alter_req;
  master::DeleteUniverseReplicationResponsePB alter_resp;
  alter_req.set_producer_id(kReplicationGroupId.ToString());
  rpc::RpcController rpc;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_exit_unfinished_deleting) = true;
  ASSERT_OK(master_proxy->DeleteUniverseReplication(alter_req, &alter_resp, &rpc));

  // Check that deletion was incomplete
  master::GetUniverseReplicationRequestPB new_req;
  new_req.set_producer_id(kReplicationGroupId.ToString());
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
  new_req.set_producer_id(kReplicationGroupId.ToString());
  Status s = master_proxy->GetUniverseReplication(new_req, &new_resp, &rpc);
  ASSERT_OK(s);
  ASSERT_TRUE(new_resp.has_error());
}


TEST_P(XClusterTest, TestFailedAlterUniverseOnRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_exit_unfinished_merging) = true;

  // Setup the consumer and producer cluster.
  auto tables = ASSERT_RESULT(SetUpWithParams({3, 3}, {3, 3}, 1));
  std::vector<std::shared_ptr<client::YBTable>> producer_tables{tables[0], tables[2]};
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables{tables[1], tables[3]};
  ASSERT_OK(SetupUniverseReplication({producer_tables[0]}));

  // Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
    &consumer_client()->proxy_cache(),
    ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  // Make sure only 1 table is included in replication
  master::GetUniverseReplicationRequestPB new_req;
  new_req.set_producer_id(kReplicationGroupId.ToString());
  master::GetUniverseReplicationResponsePB new_resp;
  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  ASSERT_OK(master_proxy->GetUniverseReplication(new_req, &new_resp, &rpc));
  ASSERT_EQ(new_resp.entry().tables_size(), 1);

  // Add the other table
  master::AlterUniverseReplicationRequestPB alter_req;
  master::AlterUniverseReplicationResponsePB alter_resp;
  alter_req.set_producer_id(kReplicationGroupId.ToString());
  alter_req.add_producer_table_ids_to_add(producer_tables[1]->id());
  rpc.Reset();

  ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));

  // Restart the ENTIRE Consumer cluster.
  ASSERT_OK(consumer_cluster()->RestartSync());

  // Wait for alter universe to be deleted on start up
  ASSERT_OK(
      WaitForSetupUniverseReplicationCleanUp(GetAlterReplicationGroupId(kReplicationGroupId)));

  // Change should not have gone through
  new_req.set_producer_id(kReplicationGroupId.ToString());
  rpc.Reset();
  ASSERT_OK(master_proxy->GetUniverseReplication(new_req, &new_resp, &rpc));
  ASSERT_NE(new_resp.entry().tables_size(), 2);

  // Check that the unfinished alter universe was deleted on start up
  rpc.Reset();
  new_req.set_producer_id(GetAlterReplicationGroupId(kReplicationGroupId).ToString());
  Status s = master_proxy->GetUniverseReplication(new_req, &new_resp, &rpc);
  ASSERT_OK(s);
  ASSERT_TRUE(new_resp.has_error());
}

TEST_P(XClusterTest, TestAlterUniverseRemoveTableAndDrop) {
  // Create 2 tables and start replication.
  auto tables = ASSERT_RESULT(SetUpWithParams({1, 1}, {1, 1}, 1));
  std::vector<std::shared_ptr<client::YBTable>> producer_tables{tables[0], tables[2]};
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables{tables[1], tables[3]};
  ASSERT_OK(SetupUniverseReplication(producer_tables));

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
  alter_req.set_producer_id(kReplicationGroupId.ToString());
  alter_req.add_producer_table_ids_to_remove(producer_tables[0]->id());

  ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));

  // Now attempt to drop the table on both sides.
  ASSERT_OK(producer_client()->DeleteTable(producer_tables[0]->name()));
  ASSERT_OK(consumer_client()->DeleteTable(consumer_tables[0]->name()));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, TestNonZeroLagMetricsWithoutGetChange) {
  auto tables = ASSERT_RESULT(SetUpWithParams({1}, {1}, 1));
  std::shared_ptr<client::YBTable> producer_table = tables[0];
  std::shared_ptr<client::YBTable> consumer_table = tables[1];

  // Stop the consumer tserver before setting up replication.
  consumer_cluster()->mini_tablet_server(0)->Shutdown();

  ASSERT_OK(SetupUniverseReplication({producer_table}));
  SleepFor(MonoDelta::FromSeconds(5));  // Wait for the stream to setup.

  // Obtain CDC stream id.
  master::ListCDCStreamsResponsePB stream_resp;
  ASSERT_OK(GetCDCStreamForTable(producer_table->id(), &stream_resp));
  ASSERT_FALSE(stream_resp.has_error());
  ASSERT_EQ(stream_resp.streams_size(), 1);
  ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), producer_table->id());
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
        HostPort::FromBoundEndpoint(
            producer_cluster()->mini_tablet_server(0)->bound_rpc_addr()));
    ASSERT_OK(producer_cdc_proxy->ListTablets(tablets_req, &tablets_resp, &rpc));
    ASSERT_FALSE(tablets_resp.has_error());
    ASSERT_EQ(tablets_resp.tablets_size(), 1);
    tablet_id = tablets_resp.tablets(0).tablet_id();
  }

  // Check that the CDC enabled flag is true.
  tserver::TabletServer* cdc_ts =
      producer_cluster()->mini_tablet_server(0)->server();
  auto cdc_service = dynamic_cast<cdc::CDCServiceImpl*>(
      cdc_ts->rpc_server()->TEST_service_pool("yb.cdc.CDCService")->TEST_get_service().get());

  ASSERT_OK(WaitFor([&]() { return cdc_service->CDCEnabled(); },
                    MonoDelta::FromSeconds(30), "IsCDCEnabled"));

  // Check that the time_since_last_getchanges metric is updated, even without GetChanges.
  std::shared_ptr<cdc::CDCTabletMetrics> metrics;
  ASSERT_OK(WaitFor(
      [&]() {
        metrics = std::static_pointer_cast<cdc::CDCTabletMetrics>(
            cdc_service->GetCDCTabletMetrics({{}, stream_id, tablet_id}));
        if (!metrics) {
          return false;
        }
        return metrics->time_since_last_getchanges->value() > 0;
      },
      MonoDelta::FromSeconds(30),
      "Retrieve CDC metrics and check time_since_last_getchanges > 0."));

  // Write some data to producer, and check that the lag metric is non-zero on producer tserver,
  // and no GetChanges is received.
  int i = 0;
  ASSERT_OK(WaitFor(
      [&]() {
        WriteWorkload(i, i+1, producer_client(), producer_table->name());
        i++;
        return metrics->async_replication_committed_lag_micros->value() != 0 &&
            metrics->async_replication_sent_lag_micros->value() != 0;
      },
      MonoDelta::FromSeconds(30),
      "Whether lag != 0 when no GetChanges is received."));

  // Bring up the consumer tserver and verify that replication is successful.
  ASSERT_OK(consumer_cluster()->mini_tablet_server(0)->Start(
    tserver::WaitTabletsBootstrapped::kFalse));
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, DeleteTableChecksCQL) {
  // Create 3 tables with 1 tablet each.
  constexpr int kNT = 1;
  std::vector<uint32_t> tables_vector = {kNT, kNT, kNT};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 1));
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  producer_tables.reserve(tables.size() / 2);
  consumer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); ++i) {
    if (i % 2 == 0) {
      producer_tables.push_back(tables[i]);
    } else {
      consumer_tables.push_back(tables[i]);
    }
  }

  // 1. Write some data.
  for (const auto& producer_table : producer_tables) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(0, 100, producer_client(), producer_table->name());
  }

  // Verify data is written on the producer.
  for (const auto& producer_table : producer_tables) {
    auto producer_results = ScanTableToStrings(producer_table->name(), producer_client());
    ASSERT_EQ(100, producer_results.size());
  }

  // Set aside one table for AlterUniverseReplication.
  std::shared_ptr<client::YBTable> producer_alter_table, consumer_alter_table;
  producer_alter_table = producer_tables.back();
  producer_tables.pop_back();
  consumer_alter_table = consumer_tables.back();
  consumer_tables.pop_back();

  // 2a. Setup replication.
  ASSERT_OK(SetupUniverseReplication(producer_tables));

  // Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(&get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<uint32_t>(producer_tables.size() * kNT)));

  // 2b. Alter Replication
  {
    auto* consumer_leader_mini_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(),
        consumer_leader_mini_master->bound_rpc_addr());
    master::AlterUniverseReplicationRequestPB alter_req;
    master::AlterUniverseReplicationResponsePB alter_resp;
    alter_req.set_producer_id(kReplicationGroupId.ToString());
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
          return tmp_resp.entry().tables_size() == static_cast<int64>(producer_tables.size() + 1);
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Verify table created with alter."));

    ASSERT_OK(CorrectlyPollingAllTablets(
        consumer_cluster(), narrow_cast<uint32_t>((producer_tables.size() + 1) * kNT)));
  }
  producer_tables.push_back(producer_alter_table);
  consumer_tables.push_back(consumer_alter_table);

  auto data_replicated_correctly = [&](size_t num_results) -> Result<bool> {
    for (const auto& consumer_table : consumer_tables) {
      LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
      auto consumer_results = ScanTableToStrings(consumer_table->name(), consumer_client());

      if (num_results != consumer_results.size()) {
        return false;
      }
    }
    return true;
  };
  ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(100); },
                    MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

  // Attempt to destroy the producer and consumer tables.
  for (size_t i = 0; i < producer_tables.size(); ++i) {
    string producer_table_id = producer_tables[i]->id();
    string consumer_table_id = consumer_tables[i]->id();
    ASSERT_NOK(producer_client()->DeleteTable(producer_table_id));
    ASSERT_NOK(consumer_client()->DeleteTable(consumer_table_id));
  }

  // Delete replication, afterwards table deletions are no longer blocked.
  ASSERT_OK(DeleteUniverseReplication());

  for (size_t i = 0; i < producer_tables.size(); ++i) {
    string producer_table_id = producer_tables[i]->id();
    string consumer_table_id = consumer_tables[i]->id();
    ASSERT_OK(producer_client()->DeleteTable(producer_table_id));
    ASSERT_OK(consumer_client()->DeleteTable(consumer_table_id));
  }
}

TEST_P(XClusterTest, SetupNSUniverseReplicationExtraConsumerTables) {
  // Initial setup: create 2 tables on each side.
  constexpr int kNTabletsPerTable = 3;
  std::vector<uint32_t> table_vector = {kNTabletsPerTable, kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(table_vector, table_vector, 1));
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  producer_tables.reserve(4);
  consumer_tables.reserve(4);
  for (size_t i = 0; i < tables.size(); i++) {
    if (i % 2 == 0) {
      producer_tables.push_back(tables[i]);
    } else {
      consumer_tables.push_back(tables[i]);
    }
  }

  // Create 2 more consumer tables.
  for (int i = 2; i < 4; i++) {
    auto t = ASSERT_RESULT(CreateTable(
        consumer_client(), kNamespaceName, Format("test_table_$0", i), kNTabletsPerTable));
    consumer_tables.push_back({});
    ASSERT_OK(consumer_client()->OpenTable(t, &consumer_tables.back()));
  }

  // Setup NS universe replication. Only the first 2 consumer tables will be replicated.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ns_replication_sync_retry_secs) = 1;
  ASSERT_OK(SetupNSUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(),
      kReplicationGroupId, kNamespaceName, YQLDatabase::YQL_DATABASE_CQL));
  ASSERT_OK(VerifyNSUniverseReplication(
      consumer_cluster(), consumer_client(),
      kReplicationGroupId, narrow_cast<int>(producer_tables.size())));

  // Create the additional 2 tables on producer. Verify that they are added automatically.
  for (int i = 2; i < 4; i++) {
    auto t = ASSERT_RESULT(CreateTable(
        producer_client(), kNamespaceName, Format("test_table_$0", i), kNTabletsPerTable));
    producer_tables.push_back({});
    ASSERT_OK(producer_client()->OpenTable(t, &producer_tables.back()));
  }
  ASSERT_OK(VerifyNSUniverseReplication(
      consumer_cluster(), consumer_client(),
      kReplicationGroupId, narrow_cast<int>(consumer_tables.size())));

  // Write some data and verify replication.
  for (size_t i = 0; i < producer_tables.size(); i++) {
    const auto& producer_table = producer_tables[i];
    const auto& consumer_table = consumer_tables[i];
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(0, narrow_cast<int>(10 * (i + 1)), producer_client(), producer_table->name());
    ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  }
  ASSERT_OK(DeleteUniverseReplication(kReplicationGroupId));
}

TEST_P(XClusterTest, SetupNSUniverseReplicationExtraProducerTables) {
  // Initial setup: create 2 tables on each side.
  constexpr int kNTabletsPerTable = 3;
  std::vector<uint32_t> table_vector = {kNTabletsPerTable, kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(table_vector, table_vector, 1));
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  producer_tables.reserve(4);
  consumer_tables.reserve(4);
  for (size_t i = 0; i < tables.size(); i++) {
    if (i % 2 == 0) {
      producer_tables.push_back(tables[i]);
    } else {
      consumer_tables.push_back(tables[i]);
    }
  }

  // Create 2 more producer tables.
  for (int i = 2; i < 4; i++) {
    auto t = ASSERT_RESULT(CreateTable(
        producer_client(), kNamespaceName, Format("test_table_$0", i), kNTabletsPerTable));
    producer_tables.push_back({});
    ASSERT_OK(producer_client()->OpenTable(t, &producer_tables.back()));
  }

  // Setup NS universe replication. Only the first 2 producer tables will be replicated.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ns_replication_sync_backoff_secs) = 1;
  ASSERT_OK(SetupNSUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(),
      kReplicationGroupId, kNamespaceName, YQLDatabase::YQL_DATABASE_CQL));
  ASSERT_OK(VerifyNSUniverseReplication(
      consumer_cluster(), consumer_client(),
      kReplicationGroupId, narrow_cast<int>(consumer_tables.size())));

  // Create the additional 2 tables on consumer. Verify that they are added automatically.
  for (int i = 2; i < 4; i++) {
    auto t = ASSERT_RESULT(CreateTable(
        consumer_client(), kNamespaceName, Format("test_table_$0", i), kNTabletsPerTable));
    consumer_tables.push_back({});
    ASSERT_OK(consumer_client()->OpenTable(t, &consumer_tables.back()));
  }
  ASSERT_OK(VerifyNSUniverseReplication(
      consumer_cluster(), consumer_client(),
      kReplicationGroupId, narrow_cast<int>(producer_tables.size())));

  // Write some data and verify replication.
  for (size_t i = 0; i < producer_tables.size(); i++) {
    const auto& producer_table = producer_tables[i];
    const auto& consumer_table = consumer_tables[i];
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(0, narrow_cast<int>(10 * (i + 1)), producer_client(), producer_table->name());
    ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  }
  ASSERT_OK(DeleteUniverseReplication(kReplicationGroupId));
}

TEST_P(XClusterTest, SetupNSUniverseReplicationTwoNamespace) {
  // Create 2 tables in one namespace.
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> table_vector = {kNTabletsPerTable, kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(table_vector, table_vector, 1));

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
      producer_cluster(), consumer_cluster(), consumer_client(),
      kReplicationGroupId, kNamespaceName2, YQLDatabase::YQL_DATABASE_CQL));
  SleepFor(MonoDelta::FromSeconds(5)); // Let the bg thread run a few times.
  ASSERT_OK(VerifyNSUniverseReplication(
      consumer_cluster(), consumer_client(),
      kReplicationGroupId, narrow_cast<int>(producer_tables.size())));

  // Write some data and verify replication.
  for (size_t i = 0; i < producer_tables.size(); i++) {
    const auto& producer_table = producer_tables[i];
    const auto& consumer_table = consumer_tables[i];
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(0, narrow_cast<int>(10 * (i + 1)), producer_client(), producer_table->name());
    ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  }
  ASSERT_OK(DeleteUniverseReplication(kReplicationGroupId));
}

class XClusterTestWaitForReplicationDrain : public XClusterTest {
 public:
  void SetUpTablesAndReplication(
      std::vector<std::shared_ptr<client::YBTable>>* producer_tables,
      std::vector<std::shared_ptr<client::YBTable>>* consumer_tables,
      uint32_t num_tables = 3,
      uint32_t num_tablets_per_table = 3,
      uint32_t replication_factor = 3,
      uint32_t num_masters = 1,
      uint32_t num_tservers = 3) {
    std::vector<uint32_t> table_vector(num_tables);
    for (size_t i = 0; i < num_tables; i++) {
      table_vector[i] = num_tablets_per_table;
    }
    producer_tables->clear();
    consumer_tables->clear();
    producer_tables->reserve(num_tables);
    consumer_tables->reserve(num_tables);

    // Set up tables.
    auto tables = ASSERT_RESULT(SetUpWithParams(
        table_vector, table_vector, replication_factor, num_masters, num_tservers));
    for (size_t i = 0; i < tables.size(); i++) {
      if (i % 2 == 0) {
        producer_tables->push_back(tables[i]);
      } else {
        consumer_tables->push_back(tables[i]);
      }
    }

    // Set up replication.
    master::GetUniverseReplicationResponsePB resp;
    ASSERT_OK(SetupUniverseReplication(*producer_tables));
    ASSERT_OK(VerifyUniverseReplication(
        consumer_cluster(), consumer_client(), kReplicationGroupId, &resp));
  }

  void TearDown() override {
    ASSERT_OK(DeleteUniverseReplication());
    XClusterTest::TearDown();
  }

  std::shared_ptr<std::promise<Status>> WaitForReplicationDrainAsync(
    const std::shared_ptr<master::MasterReplicationProxy>& master_proxy,
    const master::WaitForReplicationDrainRequestPB& req,
    int expected_num_nondrained,
    int timeout_secs = kRpcTimeout) {
    // Return a promise that will be set to a status indicating whether the API completes
    // with the expected response.
    auto promise = std::make_shared<std::promise<Status>>();
    std::thread async_task(
        [this, expected_num_nondrained, timeout_secs, promise, &master_proxy, &req]() {
          auto s = WaitForReplicationDrain(
              master_proxy, req, expected_num_nondrained, timeout_secs);
          promise->set_value(s);
        });
    async_task.detach();
    return promise;
  }
};

INSTANTIATE_TEST_CASE_P(
    XClusterTestParams, XClusterTestWaitForReplicationDrain,
    ::testing::Values(XClusterTestParams(true /* transactional_table */),
                      XClusterTestParams(false /* transactional_table */)));

TEST_P(XClusterTestWaitForReplicationDrain, TestBlockGetChanges) {
  constexpr uint32_t kNumTables = 3;
  constexpr uint32_t kNumTablets = 3;
  constexpr int kRpcTimeoutShort = 30;
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  master::WaitForReplicationDrainRequestPB req;

  SetUpTablesAndReplication(&producer_tables, &consumer_tables, kNumTables, kNumTablets);
  PopulateWaitForReplicationDrainRequest(producer_tables, &req);
  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &producer_client()->proxy_cache(),
      ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  // 1. Replication is caught-up initially.
  ASSERT_OK(WaitForReplicationDrain(master_proxy, req, 0));

  // 2. Replication is caught-up when some data is written.
  for (uint32_t i = 0; i < producer_tables.size(); i++) {
    WriteWorkload(0, 50*(i+1), producer_client(), producer_tables[i]->name());
    ASSERT_OK(VerifyWrittenRecords(producer_tables[i]->name(), consumer_tables[i]->name()));
  }
  ASSERT_OK(WaitForReplicationDrain(master_proxy, req, 0));

  // 3. Replication is not caught-up when GetChanges are blocked while producer
  // keeps taking writes.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_get_changes) = true;
  for (uint32_t i = 0; i < producer_tables.size(); i++) {
    WriteWorkload(50*(i+1), 100*(i+1), producer_client(), producer_tables[i]->name());
  }
  ASSERT_OK(WaitForReplicationDrain(
      master_proxy, req, kNumTables * kNumTablets, kRpcTimeoutShort));

  // 4. Replication is caught-up when GetChanges are unblocked.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_get_changes) = false;
  ASSERT_OK(WaitForReplicationDrain(master_proxy, req, 0));
}

TEST_P(XClusterTestWaitForReplicationDrain, TestWithTargetTime) {
  constexpr uint32_t kNumTables = 3;
  constexpr uint32_t kNumTablets = 3;
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  master::WaitForReplicationDrainRequestPB req;

  SetUpTablesAndReplication(&producer_tables, &consumer_tables, kNumTables, kNumTablets);
  PopulateWaitForReplicationDrainRequest(producer_tables, &req);
  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &producer_client()->proxy_cache(),
      ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  // 1. Write some data and verify that replication is caught-up.
  auto past_checkpoint = GetCurrentTimeMicros();
  for (uint32_t i = 0; i < producer_tables.size(); i++) {
    WriteWorkload(0, 50*(i+1), producer_client(), producer_tables[i]->name());
  }
  ASSERT_OK(WaitForReplicationDrain(master_proxy, req, 0));

  // 2. Verify that replication is caught-up to a checkpoint in the past.
  req.set_target_time(past_checkpoint);
  ASSERT_OK(WaitForReplicationDrain(master_proxy, req, 0));

  // 3. Set a checkpoint in the future. Verify that API waits until passing this
  // checkpoint before responding.
  int timeout_secs = kRpcTimeout;
  auto time_to_wait = MonoDelta::FromSeconds(timeout_secs / 2.0);
  auto future_checkpoint = GetCurrentTimeMicros() + time_to_wait.ToMicroseconds();
  req.set_target_time(future_checkpoint);
  ASSERT_OK(WaitForReplicationDrain(master_proxy, req, 0));
  ASSERT_GT(GetCurrentTimeMicros(), future_checkpoint);
}

TEST_P(XClusterTestWaitForReplicationDrain, TestProducerChange) {
  constexpr uint32_t kNumTables = 3;
  constexpr uint32_t kNumTablets = 3;
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  master::WaitForReplicationDrainRequestPB req;

  SetUpTablesAndReplication(&producer_tables, &consumer_tables, kNumTables, kNumTablets);
  PopulateWaitForReplicationDrainRequest(producer_tables, &req);
  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &producer_client()->proxy_cache(),
      ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  // 1. Write some data and verify that replication is caught-up.
  for (uint32_t i = 0; i < producer_tables.size(); i++) {
    WriteWorkload(0, 50*(i+1), producer_client(), producer_tables[i]->name());
  }
  ASSERT_OK(WaitForReplicationDrain(master_proxy, req, 0));

  // 2. Verify that producer shutdown does not impact the API.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_hang_wait_replication_drain) = true;
  auto drain_api_promise = WaitForReplicationDrainAsync(master_proxy, req, 0);
  auto drain_api_future = drain_api_promise->get_future();
  producer_cluster()->mini_tablet_server(0)->Shutdown();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_hang_wait_replication_drain) = false;
  ASSERT_OK(producer_cluster()->mini_tablet_server(0)->Start(
    tserver::WaitTabletsBootstrapped::kFalse));
  ASSERT_OK(drain_api_future.get());

  // 3. Verify that producer rebalancing does not impact the API.
  auto num_tservers = narrow_cast<int>(producer_cluster()->num_tablet_servers());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_hang_wait_replication_drain) = true;
  drain_api_promise = WaitForReplicationDrainAsync(master_proxy, req, 0);
  drain_api_future = drain_api_promise->get_future();
  ASSERT_OK(producer_cluster()->AddTabletServer());
  ASSERT_OK(producer_cluster()->WaitForTabletServerCount(num_tservers + 1));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_hang_wait_replication_drain) = false;
  ASSERT_OK(drain_api_future.get());
}

TEST_P(XClusterTest, TestPrematureLogGC) {
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
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, kReplicationFactor));

  std::shared_ptr<client::YBTable> producer_table = tables[0];
  std::shared_ptr<client::YBTable> consumer_table = tables[1];

  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kReplicationGroupId,
      {producer_table}));

  // Write enough records to the producer to populate multiple WAL segments.
  constexpr int kNumWriteRecords = 100;
  WriteWorkload(0, kNumWriteRecords, producer_client(), producer_table->name());

  // Verify that all records were written and replicated.
  ASSERT_OK(VerifyNumRecords(producer_table->name(), producer_client(), kNumWriteRecords));
  ASSERT_OK(VerifyNumRecords(consumer_table->name(), consumer_client(), kNumWriteRecords));

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
  WriteWorkload(kNumWriteRecords, 2 * kNumWriteRecords, producer_client(), producer_table->name());
  ASSERT_OK(VerifyNumRecords(producer_table->name(), producer_client(), 2 * kNumWriteRecords));

  // Unflushed WAL segments can not be garbage collected. Flush all tablets WALs now.
  ASSERT_OK(producer_cluster()->FlushTablets(
      tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));

  // Garbage collect all Tablet WALs. This will GC most of the segments, but will leave at least one
  // segment in each WAL (determined by FLAGS_log_min_segments_to_retain). The remaining segment may
  // contain records.
  ASSERT_OK(producer_cluster()->CleanTabletLogs());

  // Re-enable the replication poll and wait long enough for multiple cycles to run.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdc_skip_replication_poll) = false;
  SleepFor(MonoDelta::FromSeconds(3));

  // Verify that at least one of the recently written records failed to replicate.
  auto results = ScanTableToStrings(consumer_table->name(), consumer_client());
  ASSERT_GE(results.size(), kNumWriteRecords);
  ASSERT_LT(results.size(), 2 * kNumWriteRecords);

  // Get the stream id.
  auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_table->id()));

  // Verify that the GetReplicationStatus RPC contains the 'REPLICATION_MISSING_OP_ID' error.
  VerifyReplicationError(
    consumer_table->id(), stream_id, ReplicationErrorPb::REPLICATION_MISSING_OP_ID);
}

TEST_P(XClusterTest, PausingAndResumingReplicationFromProducerSingleTable) {
  constexpr int kNTabletsPerTable = 3;
  constexpr int kNumTables = 1;
  uint32_t kReplicationFactor = NonTsanVsTsan(3, 1);
  std::vector<uint32_t> tables_vector(kNumTables);
  for (size_t i = 0; i < kNumTables; i++) {
    tables_vector[i] = kNTabletsPerTable;
  }
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, kReplicationFactor));
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;

  producer_tables.push_back(tables[0]);
  consumer_tables.push_back(tables[1]);

  // Empty case.
  ASSERT_OK(PauseResumeXClusterProducerStreams({}, true));
  ASSERT_OK(PauseResumeXClusterProducerStreams({}, false));

  // Invalid ID case.
  ASSERT_NOK(PauseResumeXClusterProducerStreams({xrepl::StreamId::GenerateRandom()}, true));
  ASSERT_NOK(PauseResumeXClusterProducerStreams({xrepl::StreamId::GenerateRandom()}, false));

  ASSERT_OK(SetupUniverseReplication(producer_tables));
  master::IsSetupUniverseReplicationDoneResponsePB is_resp;
  ASSERT_OK(WaitForSetupUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId, &is_resp));

  std::vector<xrepl::StreamId> stream_ids;

  WriteWorkloadAndVerifyWrittenRows(producer_tables[0], consumer_tables[0], 0, 10);

  // Test pausing all streams (by passing empty streams list) when there is only one stream.
  SetPauseAndVerifyWrittenRows({}, producer_tables, consumer_tables, 10, 20);

  // Test resuming all streams (by passing empty streams list) when there is only one stream.
  SetPauseAndVerifyWrittenRows({}, producer_tables, consumer_tables, 20, 30, false);

  // Test pausing stream by ID when there is only one stream by passing non-empty stream_ids.
  auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_tables[0]->id()));
  stream_ids.push_back(stream_id);
  SetPauseAndVerifyWrittenRows(stream_ids, producer_tables, consumer_tables, 30, 40);

  // Test resuming stream by ID when there is only one stream by passing non-empty stream_ids.
  SetPauseAndVerifyWrittenRows(stream_ids, producer_tables, consumer_tables, 40, 50, false);

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
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, kReplicationFactor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  // Testing on multiple tables.
  producer_tables.reserve(tables.size() / 2);
  consumer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); i++) {
    if (i % 2 == 0) {
      producer_tables.push_back(tables[i]);
    } else {
      consumer_tables.push_back(tables[i]);
    }
  }

  std::vector<xrepl::StreamId> stream_ids;

  ASSERT_OK(SetupUniverseReplication(producer_tables));
  master::IsSetupUniverseReplicationDoneResponsePB is_resp;
  ASSERT_OK(WaitForSetupUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId, &is_resp));

  SetPauseAndVerifyWrittenRows({}, producer_tables, consumer_tables, 0, 10, false);
  // Test pausing all streams (by passing empty streams list) when there are multiple streams.
  SetPauseAndVerifyWrittenRows({}, producer_tables, consumer_tables, 10, 20);
  // Test resuming all streams (by passing empty streams list) when there are multiple streams.
  SetPauseAndVerifyWrittenRows({}, producer_tables, consumer_tables, 20, 30, false);
  // Add the stream IDs of the first two tables to test pausing and resuming just those ones.
  for (size_t i = 0; i < producer_tables.size() - 1; ++i) {
    auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_tables[i]->id()));
    stream_ids.push_back(stream_id);
  }
  // Test pausing replication on the first two tables by passing stream_ids containing their
  // corresponding xcluster streams.
  SetPauseAndVerifyWrittenRows(stream_ids, producer_tables, consumer_tables, 30, 40);
  // Verify that the remaining streams are unpaused still.
  for (size_t i = stream_ids.size(); i < producer_tables.size(); ++i) {
    WriteWorkloadAndVerifyWrittenRows(producer_tables[i], consumer_tables[i], 30, 40);
  }
  // Test resuming replication on the first two tables by passing stream_ids containing their
  // corresponding xcluster streams.
  SetPauseAndVerifyWrittenRows(stream_ids, producer_tables, consumer_tables, 40, 50, false);
  // Verify that the previously unpaused streams are still replicating.
  for (size_t i = stream_ids.size(); i < producer_tables.size(); ++i) {
    WriteWorkloadAndVerifyWrittenRows(producer_tables[i], consumer_tables[i], 40, 50);
  }
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_P(XClusterTest, LeaderFailoverTest) {
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

  const uint32_t kReplicationFactor = 3, kTabletCount = 1, kNumMasters = 1, kNumTservers = 3;
  auto tables = ASSERT_RESULT(SetUpWithParams(
      {kTabletCount}, {kTabletCount}, kReplicationFactor, kNumMasters, kNumTservers));
  const auto& producer_table = tables[0];
  const auto& consumer_table = tables[1];
  ASSERT_OK(
      SetupUniverseReplication({producer_table}, {LeaderOnly::kFalse, Transactional::kFalse}));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;

  // After creating the cluster, make sure all tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), kTabletCount));

  auto tablet_ids = ListTabletIdsForTable(consumer_cluster(), consumer_table->id());
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
  WriteWorkload(0, kNumWriteRecords, producer_client(), producer_table->name());
  ASSERT_OK(VerifyNumRecords(consumer_table->name(), consumer_client(), kNumWriteRecords));

  // Failover to new tserver.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_disable_poller_term_check) = true;
  ASSERT_OK(itest::LeaderStepDown(old_ts, tablet_id, new_ts, kTimeout));
  ASSERT_OK(itest::WaitUntilLeader(new_ts, tablet_id, kTimeout));
  auto new_tserver = FindTabletLeader(consumer_cluster(), tablet_id);

  WriteWorkload(kNumWriteRecords, 2 * kNumWriteRecords, producer_client(), producer_table->name());
  ASSERT_OK(VerifyNumRecords(consumer_table->name(), consumer_client(), 2 * kNumWriteRecords));

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

  WriteWorkload(
      2 * kNumWriteRecords, 3 * kNumWriteRecords, producer_client(), producer_table->name());

  auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_table->id()));
  VerifyReplicationError(
      consumer_table->id(), stream_id, ReplicationErrorPb::REPLICATION_MISSING_OP_ID);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_disable_poller_term_check) = false;
  ASSERT_OK(VerifyNumRecords(consumer_table->name(), consumer_client(), 3 * kNumWriteRecords));
}

// Verify the xCluster Pollers shutdown immediately even if the Poll delay is very long.
TEST_F_EX(XClusterTest, PollerShutdownWithLongPollDelay, XClusterTestNoParam) {
  // Make Pollers enter a long sleep state.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdc_skip_replication_poll) = true;
  ASSERT_OK(SET_FLAG(async_replication_idle_delay_ms, 10 * 60 * 1000));  // 10 minutes
  const auto kTimeout = 10s * kTimeMultiplier;

  const auto kTabletCount = 3;
  auto tables = ASSERT_RESULT(SetUpWithParams({kTabletCount}, {kTabletCount}, 3));
  ASSERT_EQ(tables.size(), 2);

  std::vector<std::shared_ptr<client::YBTable>> producer_tables{tables[0]};
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables{tables[1]};

  ASSERT_OK(SetupUniverseReplication(producer_tables));

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

} // namespace yb
