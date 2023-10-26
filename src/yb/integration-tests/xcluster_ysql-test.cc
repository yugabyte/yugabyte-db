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
#include <chrono>
#include <map>
#include <string>
#include <vector>
#include <unordered_set>
#include <utility>

#include <boost/assign.hpp>

#include <gtest/gtest.h>

#include "yb/integration-tests/xcluster_ysql_test_base.h"

#include "yb/common/colocated_util.h"
#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/log.h"
#include "yb/consensus/log_reader.h"

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.proxy.h"
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
#include "yb/client/yb_op.h"

#include "yb/docdb/docdb.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/xcluster_test_base.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/cdc_consumer_registry_service.h"
#include "yb/master/mini_master.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_util.h"
#include "yb/master/master_replication.proxy.h"

#include "yb/rpc/rpc_controller.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/faststring.h"
#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

using std::string;

DECLARE_int32(replication_factor);
DECLARE_int32(cdc_max_apply_batch_num_records);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_bool(enable_delete_truncate_xcluster_replicated_table);
DECLARE_int32(update_min_cdc_indices_interval_secs);
DECLARE_int32(log_cache_size_limit_mb);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_uint64(log_segment_size_bytes);
DECLARE_int64(remote_bootstrap_rate_limit_bytes_per_sec);
DECLARE_int32(log_max_seconds_to_retain);
DECLARE_int32(log_min_segments_to_retain);
DECLARE_bool(check_bootstrap_required);
DECLARE_bool(enable_load_balancing);
DECLARE_string(pgsql_proxy_bind_address);
DECLARE_bool(ysql_disable_index_backfill);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(ysql_enable_packed_row_for_colocated_table);
DECLARE_uint64(ysql_packed_row_size_limit);
DECLARE_bool(xcluster_wait_on_ddl_alter);
DECLARE_bool(TEST_disable_apply_committed_transactions);
DECLARE_uint64(ysql_session_max_batch_size);
DECLARE_bool(use_libbacktrace);
DECLARE_uint64(consensus_max_batch_size_bytes);
DECLARE_uint64(aborted_intent_cleanup_ms);
DECLARE_uint32(external_intent_cleanup_secs);
DECLARE_bool(TEST_force_get_checkpoint_from_cdc_state);
DECLARE_bool(ysql_legacy_colocated_database_creation);
DECLARE_bool(TEST_cdc_skip_replication_poll);
DECLARE_int32(rpc_workers_limit);
DECLARE_int32(tablet_server_svc_queue_length);
DECLARE_uint32(external_transaction_retention_window_secs);
DECLARE_bool(TEST_create_table_with_empty_pgschema_name);

namespace yb {

using namespace std::chrono_literals;
using client::YBClient;
using client::YBTable;
using client::YBTableName;
using master::GetNamespaceInfoResponsePB;

using pgwrapper::GetInt32;
using pgwrapper::GetValue;
using pgwrapper::PGConn;
using pgwrapper::PGResultPtr;
using pgwrapper::ToString;

static const client::YBTableName producer_transaction_table_name(
    YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);

class XClusterYsqlTest : public XClusterYsqlTestBase {
 public:
  void SetUp() override {
    XClusterYsqlTestBase::SetUp();
    FLAGS_ysql_legacy_colocated_database_creation = false;
  }

  void ValidateRecordsXClusterWithCDCSDK(bool update_min_cdc_indices_interval = false,
                                      bool enable_cdc_sdk_in_producer = false,
                                      bool do_explict_transaction = false);

  void ValidateSimpleReplicationWithPackedRowsUpgrade(
      std::vector<uint32_t> consumer_tablet_counts, std::vector<uint32_t> producer_tablet_counts,
      uint32_t num_tablet_servers = 1, bool range_partitioned = false);

  void TestReplicationWithPackedColumns(bool colocated, bool bootstrap);

  Result<std::vector<std::shared_ptr<client::YBTable>>> SetUpWithParams(
      std::vector<uint32_t> num_consumer_tablets,
      std::vector<uint32_t> num_producer_tablets,
      uint32_t replication_factor,
      uint32_t num_masters = 1,
      bool colocated = false,
      boost::optional<std::string> tablegroup_name = boost::none,
      const bool ranged_partitioned = false) {
    RETURN_NOT_OK(Initialize(replication_factor, num_masters));

    if (num_consumer_tablets.size() != num_producer_tablets.size()) {
      return STATUS(IllegalState,
                    Format("Num consumer tables: $0 num producer tables: $1 must be equal.",
                           num_consumer_tablets.size(), num_producer_tablets.size()));
    }

    RETURN_NOT_OK(RunOnBothClusters(
        [&](Cluster* cluster) { return CreateDatabase(cluster, kNamespaceName, colocated); }));

    if (tablegroup_name.has_value()) {
      RETURN_NOT_OK(RunOnBothClusters([&](Cluster* cluster) {
        return CreateTablegroup(cluster, kNamespaceName, tablegroup_name.get());
      }));
    }

    std::vector<YBTableName> tables;
    std::vector<std::shared_ptr<client::YBTable>> yb_tables;
    for (uint32_t i = 0; i < num_consumer_tablets.size(); i++) {
      RETURN_NOT_OK(CreateYsqlTable(
          i, num_producer_tablets[i], &producer_cluster_, &tables, tablegroup_name, colocated,
          ranged_partitioned));
      std::shared_ptr<client::YBTable> producer_table;
      RETURN_NOT_OK(producer_client()->OpenTable(tables[i * 2], &producer_table));
      yb_tables.push_back(producer_table);

      RETURN_NOT_OK(CreateYsqlTable(
          i, num_consumer_tablets[i], &consumer_cluster_, &tables, tablegroup_name, colocated,
          ranged_partitioned));
      std::shared_ptr<client::YBTable> consumer_table;
      RETURN_NOT_OK(consumer_client()->OpenTable(tables[(i * 2) + 1], &consumer_table));
      yb_tables.push_back(consumer_table);
    }

    return yb_tables;
  }

  static Status CreateDatabase(Cluster* cluster,
                               const std::string& namespace_name = kNamespaceName,
                               bool colocated = false) {
    auto conn = EXPECT_RESULT(cluster->Connect());
    EXPECT_OK(conn.ExecuteFormat("CREATE DATABASE $0$1",
                                 namespace_name, colocated ? " colocation = true" : ""));
    return Status::OK();
  }

  std::string GetCompleteTableName(const YBTableName& table) {
    // Append schema name before table name, if schema is available.
    return table.has_pgschema_name()
        ? Format("$0.$1", table.pgschema_name(), table.table_name())
        : table.table_name();
  }

  Result<TableId> GetColocatedDatabaseParentTableId() {
    if (FLAGS_ysql_legacy_colocated_database_creation) {
      // Legacy colocated database
      GetNamespaceInfoResponsePB ns_resp;
      RETURN_NOT_OK(producer_client()->GetNamespaceInfo("", kNamespaceName, YQL_DATABASE_PGSQL,
                                                    &ns_resp));
      return GetColocatedDbParentTableId(ns_resp.namespace_().id());
    }
    // Colocated database
    return GetTablegroupParentTable(&producer_cluster_, kNamespaceName);
  }

  /*
   * TODO (#11597): Given one is not able to get tablegroup ID by name, currently this works by
   * getting the first available tablegroup appearing in the namespace.
   */
  Result<TableId> GetTablegroupParentTable(Cluster* cluster, const std::string& namespace_name) {
    // Lookup the namespace id from the namespace name.
    std::string namespace_id;
    // Whether the database named namespace_name is a colocated database.
    bool colocated_database;
    master::MasterDdlProxy master_proxy(
        &cluster->client_->proxy_cache(),
        VERIFY_RESULT(cluster->mini_cluster_->GetLeaderMiniMaster())->bound_rpc_addr());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    {
      master::ListNamespacesRequestPB req;
      master::ListNamespacesResponsePB resp;

      RETURN_NOT_OK(master_proxy.ListNamespaces(req, &resp, &rpc));
      if (resp.has_error()) {
        return STATUS(IllegalState, "Failed to get namespace info");
      }

      // Find and return the namespace id.
      bool namespaceFound = false;
      for (const auto& entry : resp.namespaces()) {
        if (entry.name() == namespace_name) {
          namespaceFound = true;
          namespace_id = entry.id();
          break;
        }
      }

      if (!namespaceFound) {
        return STATUS(IllegalState, "Failed to find namespace");
      }
    }

    {
      master::GetNamespaceInfoRequestPB req;
      master::GetNamespaceInfoResponsePB resp;

      req.mutable_namespace_()->set_id(namespace_id);

      rpc.Reset();
      RETURN_NOT_OK(master_proxy.GetNamespaceInfo(req, &resp, &rpc));
      if (resp.has_error()) {
        return STATUS(IllegalState, "Failed to get namespace info");
      }
      colocated_database = resp.colocated();
    }

    master::ListTablegroupsRequestPB req;
    master::ListTablegroupsResponsePB resp;

    req.set_namespace_id(namespace_id);
    rpc.Reset();
    RETURN_NOT_OK(master_proxy.ListTablegroups(req, &resp, &rpc));
    if (resp.has_error()) {
      return STATUS(IllegalState, "Failed listing tablegroups");
    }

    // Find and return the tablegroup.
    if (resp.tablegroups().empty()) {
      return STATUS(IllegalState,
                    Format("Unable to find tablegroup in namespace $0", namespace_name));
    }

    if (colocated_database)
      return GetColocationParentTableId(resp.tablegroups()[0].id());
    return GetTablegroupParentTableId(resp.tablegroups()[0].id());
  }

  Status CreateTablegroup(Cluster* cluster,
                          const std::string& namespace_name,
                          const std::string& tablegroup_name) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(namespace_name));
    EXPECT_OK(conn.ExecuteFormat("CREATE TABLEGROUP $0", tablegroup_name));
    return Status::OK();
  }

  void WriteWorkload(
      uint32_t start, uint32_t end, Cluster* cluster, const YBTableName& table,
      bool delete_op = false, bool use_transaction = false) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(table.namespace_name()));
    std::string table_name_str = GetCompleteTableName(table);

    LOG(INFO) << "Writing " << end - start << (delete_op ? " deletes" : " inserts")
              << " using transaction " << use_transaction;
    if (use_transaction) {
      EXPECT_OK(conn.ExecuteFormat("BEGIN"));
    }
    for (uint32_t i = start; i < end; i++) {
      if (delete_op) {
        EXPECT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE $1 = $2",
                                     table_name_str, kKeyColumnName, i));
      } else {
        // TODO(#6582) transactions currently don't work, so don't use ON CONFLICT DO NOTHING now.
        EXPECT_OK(conn.ExecuteFormat("INSERT INTO $0($1) VALUES ($2)", // ON CONFLICT DO NOTHING",
                                     table_name_str, kKeyColumnName, i));
      }
    }
    if (use_transaction) {
      EXPECT_OK(conn.ExecuteFormat("COMMIT"));
    }
  }

  void WriteGenerateSeries(uint32_t start, uint32_t end, Cluster* cluster,
                           const YBTableName& table) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(table.namespace_name()));
    auto generate_string = Format("INSERT INTO $0 VALUES (generate_series($1, $2))",
                                  table.table_name(), start, end);
    ASSERT_OK(conn.ExecuteFormat(generate_string));
  }

  void WriteTransactionalWorkload(uint32_t start, uint32_t end, Cluster* cluster,
                                  const YBTableName& table, bool commit_transaction = true) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(table.namespace_name()));
    std::string table_name_str = GetCompleteTableName(table);
    EXPECT_OK(conn.Execute("BEGIN"));
    for (uint32_t i = start; i < end; i++) {
      EXPECT_OK(conn.ExecuteFormat("INSERT INTO $0($1) VALUES ($2) ON CONFLICT DO NOTHING",
                                   table_name_str, kKeyColumnName, i));
    }
    if (commit_transaction) {
      EXPECT_OK(conn.Execute("COMMIT"));
    } else {
      EXPECT_OK(conn.Execute("ABORT"));
    }
  }

  void DeleteWorkload(uint32_t start, uint32_t end, Cluster* cluster, const YBTableName& table) {
    WriteWorkload(start, end, cluster, table, true /* delete_op */);
  }

  PGResultPtr ScanToStrings(const YBTableName& table_name, Cluster* cluster) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(table_name.namespace_name()));
    std::string table_name_str = GetCompleteTableName(table_name);
    auto result = EXPECT_RESULT(conn.FetchFormat(
        "SELECT * FROM $0 ORDER BY $1", table_name_str, kKeyColumnName));
    return result;
  }

  Status VerifyWrittenRecords(const YBTableName& producer_table,
                              const YBTableName& consumer_table) {
    return LoggedWaitFor([this, producer_table, consumer_table]() -> Result<bool> {
      auto producer_results = ScanToStrings(producer_table, &producer_cluster_);
      auto consumer_results = ScanToStrings(consumer_table, &consumer_cluster_);
      if (PQntuples(producer_results.get()) != PQntuples(consumer_results.get())) {
        return false;
      }
      for (int i = 0; i < PQntuples(producer_results.get()); ++i) {
        auto prod_val = EXPECT_RESULT(ToString(producer_results.get(), i, 0));
        auto cons_val = EXPECT_RESULT(ToString(consumer_results.get(), i, 0));
        if (prod_val != cons_val) {
          return false;
        }
      }
      return true;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify written records");
  }

  Status VerifyNumRecords(const YBTableName& table, Cluster* cluster, int expected_size) {
    return LoggedWaitFor([this, table, cluster, expected_size]() -> Result<bool> {
      auto results = ScanToStrings(table, cluster);
      return PQntuples(results.get()) == expected_size;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify number of records");
  }

  void VerifyExternalTxnIntentsStateEmpty() {
    tserver::TSTabletManager::TabletPtrs tablet_ptrs;
    for (auto& mini_tserver : consumer_cluster()->mini_tablet_servers()) {
      mini_tserver->server()->tablet_manager()->GetTabletPeers(&tablet_ptrs);
      for (auto& tablet : tablet_ptrs) {
        ASSERT_EQ(0, tablet->GetExternalTxnIntentsState()->EntryCount());
      }
    }
  }

  Status TruncateTable(Cluster* cluster,
                       std::vector<string> table_ids) {
    RETURN_NOT_OK(cluster->client_->TruncateTables(table_ids));
    return Status::OK();
  }

  Result<YBTableName> CreateMaterializedView(Cluster* cluster, const YBTableName& table) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(table.namespace_name()));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE MATERIALIZED VIEW $0_mv AS SELECT COUNT(*) FROM $0", table.table_name()));
    return GetYsqlTable(
      cluster, table.namespace_name(), table.pgschema_name(), table.table_name() + "_mv");
  }

  void BumpUpSchemaVersionsWithAlters(const std::vector<std::shared_ptr<client::YBTable>>& tables) {
    // Perform some ALTERs to bump up the schema versions and cause schema version mismatch
    // between producer and consumer before setting up replication.
    {
      for (const auto& consumer_table : tables) {
        auto tbl = consumer_table->name();
        for (size_t i = 0; i < 4; i++) {
          auto p_conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(tbl.namespace_name()));
          ASSERT_OK(p_conn.ExecuteFormat("ALTER TABLE $0 OWNER TO yugabyte;", tbl.table_name()));
          if (i % 2 == 0) {
            auto c_conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(tbl.namespace_name()));
            ASSERT_OK(c_conn.ExecuteFormat("ALTER TABLE $0 OWNER TO yugabyte;", tbl.table_name()));
          }
        }
      }
    }
  }

  void TestColocatedDatabaseReplication(bool compact = false, bool use_transaction = false) {
    SetAtomicFlag(true, &FLAGS_xcluster_wait_on_ddl_alter);
    constexpr auto kRecordBatch = 5;
    auto count = 0;
    constexpr int kNTabletsPerColocatedTable = 1;
    constexpr int kNTabletsPerTable = 3;
    std::vector<uint32_t> tables_vector = {kNTabletsPerColocatedTable, kNTabletsPerColocatedTable};
    // Create two colocated tables on each cluster.
    auto colocated_tables =
        ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 3, 1, true /* colocated */));
    const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

    // Also create an additional non-colocated table in each database.
    auto non_colocated_table = ASSERT_RESULT(CreateYsqlTable(&producer_cluster_,
                                                             kNamespaceName,
                                                             "" /* schema_name */,
                                                             "test_table_2",
                                                             boost::none /* tablegroup */,
                                                             kNTabletsPerTable,
                                                             false /* colocated */));
    std::shared_ptr<client::YBTable> non_colocated_producer_table;
    ASSERT_OK(producer_client()->OpenTable(non_colocated_table, &non_colocated_producer_table));
    non_colocated_table = ASSERT_RESULT(CreateYsqlTable(&consumer_cluster_,
                                                        kNamespaceName,
                                                        "" /* schema_name */,
                                                        "test_table_2",
                                                        boost::none /* tablegroup */,
                                                        kNTabletsPerTable,
                                                        false /* colocated */));
    std::shared_ptr<client::YBTable> non_colocated_consumer_table;
    ASSERT_OK(consumer_client()->OpenTable(non_colocated_table, &non_colocated_consumer_table));

    // colocated_tables contains both producer and consumer universe tables (alternately).
    // Pick out just the producer tables from the list.
    std::vector<std::shared_ptr<client::YBTable>> producer_tables;
    std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
    std::vector<std::shared_ptr<client::YBTable>> colocated_producer_tables;
    std::vector<std::shared_ptr<client::YBTable>> colocated_consumer_tables;
    producer_tables.reserve(colocated_tables.size() / 2 + 1);
    consumer_tables.reserve(colocated_tables.size() / 2 + 1);
    colocated_producer_tables.reserve(colocated_tables.size() / 2);
    colocated_consumer_tables.reserve(colocated_tables.size() / 2);
    for (size_t i = 0; i < colocated_tables.size(); ++i) {
      if (i % 2 == 0) {
        producer_tables.push_back(colocated_tables[i]);
        colocated_producer_tables.push_back(colocated_tables[i]);
      } else {
        consumer_tables.push_back(colocated_tables[i]);
        colocated_consumer_tables.push_back(colocated_tables[i]);
      }
    }
    producer_tables.push_back(non_colocated_producer_table);
    consumer_tables.push_back(non_colocated_consumer_table);

    BumpUpSchemaVersionsWithAlters(consumer_tables);

    // 1. Write some data to all tables.
    for (const auto& producer_table : producer_tables) {
      LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
      WriteWorkload(
          count, count + kRecordBatch, &producer_cluster_, producer_table->name(),
          /* delete_op = */ false, use_transaction);
    }
    count += kRecordBatch;

    // 2. Setup replication for only the colocated tables.
    // Get the producer colocated parent table id.
    auto colocated_parent_table_id = ASSERT_RESULT(GetColocatedDatabaseParentTableId());

    rpc::RpcController rpc;
    master::SetupUniverseReplicationRequestPB setup_universe_req;
    master::SetupUniverseReplicationResponsePB setup_universe_resp;
    setup_universe_req.set_producer_id(kUniverseId);
    string master_addr = producer_cluster()->GetMasterAddresses();
    auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, setup_universe_req.mutable_producer_master_addresses());
    // Only need to add the colocated parent table id.
    setup_universe_req.mutable_producer_table_ids()->Reserve(1);
    setup_universe_req.add_producer_table_ids(colocated_parent_table_id);
    auto* consumer_leader_mini_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(),
        consumer_leader_mini_master->bound_rpc_addr());

    rpc.Reset();
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    ASSERT_OK(master_proxy->SetupUniverseReplication(setup_universe_req, &setup_universe_resp,
                                                     &rpc));
    ASSERT_FALSE(setup_universe_resp.has_error());

    // 3. Verify everything is setup correctly.
    master::GetUniverseReplicationResponsePB get_universe_replication_resp;
    ASSERT_OK(VerifyUniverseReplication(kUniverseId, &get_universe_replication_resp));
    ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), kNTabletsPerColocatedTable));

    // 4. Check that colocated tables are being replicated.
    auto data_replicated_correctly = [&](int num_results, bool onlyColocated) -> Result<bool> {
      auto &tables = onlyColocated ? colocated_consumer_tables : consumer_tables;
      for (const auto& consumer_table : tables) {
        LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
        auto consumer_results = ScanToStrings(consumer_table->name(), &consumer_cluster_);

        if (num_results != PQntuples(consumer_results.get())) {
          return false;
        }
        int result;
        for (int i = 0; i < num_results; ++i) {
          result = VERIFY_RESULT(GetInt32(consumer_results.get(), i, 0));
          if (i != result) {
            return false;
          }
        }
      }
      return true;
    };
    ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(count, true); },
                      MonoDelta::FromSeconds(20 * kTimeMultiplier), "IsDataReplicatedCorrectly"));
    // Ensure that the non colocated table is not replicated.
    auto non_coloc_results = ScanToStrings(non_colocated_consumer_table->name(),
                                           &consumer_cluster_);
    ASSERT_EQ(0, PQntuples(non_coloc_results.get()));

    // 5. Add the regular table to replication.
    // Prepare and send AlterUniverseReplication command.
    master::AlterUniverseReplicationRequestPB alter_req;
    master::AlterUniverseReplicationResponsePB alter_resp;
    alter_req.set_producer_id(kUniverseId);
    alter_req.add_producer_table_ids_to_add(non_colocated_producer_table->id());

    rpc.Reset();
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));
    ASSERT_FALSE(alter_resp.has_error());
    // Wait until we have 2 tables (colocated tablet + regular table) logged.
    ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
      master::GetUniverseReplicationResponsePB tmp_resp;
      return VerifyUniverseReplication(kUniverseId, &tmp_resp).ok() &&
            tmp_resp.entry().tables_size() == 2;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify table created with alter."));

    ASSERT_OK(CorrectlyPollingAllTablets(
        consumer_cluster(), kNTabletsPerColocatedTable + kNTabletsPerTable));
    // Check that all data is replicated for the new table as well.
    ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(count, false); },
                      MonoDelta::FromSeconds(20 * kTimeMultiplier), "IsDataReplicatedCorrectly"));

    // 6. Add additional data to all tables
    for (const auto& producer_table : producer_tables) {
      LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
      WriteWorkload(
          count, count + kRecordBatch, &producer_cluster_, producer_table->name(),
          /* delete_op = */ false, use_transaction);
    }
    count += kRecordBatch;

    // 7. Verify all tables are properly replicated.
    ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(count, false); },
                      MonoDelta::FromSeconds(20 * kTimeMultiplier), "IsDataReplicatedCorrectly"));

    // Test Add Colocated Table, which is an ALTER operation.
    std::shared_ptr<client::YBTable> new_colocated_producer_table, new_colocated_consumer_table;

    // Add a Colocated Table on the Producer for an existing Replication stream.
    uint32_t idx = static_cast<uint32_t>(tables_vector.size()) + 1;
    {
      const int co_id = (idx) * 111111;
      auto table = ASSERT_RESULT(CreateYsqlTable(&producer_cluster_, kNamespaceName, "",
          Format("test_table_$0", idx), boost::none, kNTabletsPerColocatedTable, true, co_id));
      ASSERT_OK(producer_client()->OpenTable(table, &new_colocated_producer_table));
    }

    // 2. Write data so we have some entries on the new colocated table.
    WriteWorkload(
        0, kRecordBatch, &producer_cluster_, new_colocated_producer_table->name(),
        /* delete_op = */ false, use_transaction);

    {
      // Matching schema to consumer should succeed.
      const int co_id = (idx) * 111111;
      auto table = ASSERT_RESULT(CreateYsqlTable(&consumer_cluster_, kNamespaceName, "",
          Format("test_table_$0", idx), boost::none, kNTabletsPerColocatedTable, true, co_id));
      ASSERT_OK(consumer_client()->OpenTable(table, &new_colocated_consumer_table));
    }

    // 5. Verify the new schema Producer entries are added to Consumer.
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      LOG(INFO) << "Checking records for table " << new_colocated_consumer_table->name().ToString();
      auto consumer_results = ScanToStrings(new_colocated_consumer_table->name(),
                                            &consumer_cluster_);
      auto num_results = kRecordBatch;
      if (num_results != PQntuples(consumer_results.get())) {
        return false;
      }
      int result;
      for (int i = 0; i < num_results; ++i) {
        result = VERIFY_RESULT(GetInt32(consumer_results.get(), i, 0));
        if (i != result) {
          return false;
        }
      }
      return true;
    }, MonoDelta::FromSeconds(20 * kTimeMultiplier), "IsDataReplicatedCorrectly"));

    // 6. Drop the new table and ensure that data is getting replicated correctly for
    // the other tables
    ASSERT_OK(DropYsqlTable(&producer_cluster_, kNamespaceName, "", Format("test_table_$0", idx)));
    LOG(INFO) << Format("Dropped test_table_$0 on Producer side", idx);

    // 7. Add additional data to the original tables.
    for (const auto& producer_table : producer_tables) {
      LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
      WriteWorkload(count, count + kRecordBatch, &producer_cluster_, producer_table->name());
    }
    count += kRecordBatch;

    // 8. Verify all tables are properly replicated.
    ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(count, false); },
                      MonoDelta::FromSeconds(20 * kTimeMultiplier), "IsDataReplicatedCorrectly"));

    if (compact) {
      BumpUpSchemaVersionsWithAlters(consumer_tables);

      ASSERT_OK(consumer_cluster()->FlushTablets());
      ASSERT_OK(consumer_cluster()->CompactTablets());

      ASSERT_OK(WaitFor(
          [&]() -> Result<bool> { return data_replicated_correctly(count, false); },
          MonoDelta::FromSeconds(20 * kTimeMultiplier),
          "IsDataReplicatedCorrectlyAfterCompaction"));
    }
  }
};

TEST_F(XClusterYsqlTest, GenerateSeries) {
  auto tables = ASSERT_RESULT(SetUpWithParams({4}, {4}, 3, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  auto producer_table = tables[0];
  auto consumer_table = tables[1];
  ASSERT_OK(SetupUniverseReplication(kUniverseId, {producer_table}));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(kUniverseId, &resp));
  ASSERT_EQ(resp.entry().producer_id(), kUniverseId);
  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), producer_table->id());

  ASSERT_NO_FATALS(WriteGenerateSeries(0, 50, &producer_cluster_, producer_table->name()));

  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));

  VerifyExternalTxnIntentsStateEmpty();
}

constexpr int kTransactionalConsistencyTestDurationSecs = 30;

class XClusterYSqlTestConsistentTransactionsTest : public XClusterYsqlTest {
 public:
  void SetUp() override {
    XClusterYsqlTest::SetUp();
  }

  void MultiTransactionConsistencyTest(
      uint32_t transaction_size, uint32_t num_transactions, const YBTableName& producer_table,
      const YBTableName& consumer_table, bool commit_all_transactions) {
    // Have one writer thread and one reader thread. For each read, assert
    // - atomicity: that the total number of records read mod the transaction size is 0 to ensure
    // that we have no half transactional cuts.
    // - ordering: that the records returned are always [0, num_records_reads] to ensure that no
    // later transaction is readable before an earlier one.
    auto total_intent_records = transaction_size * num_transactions;
    auto total_committed_records =
        commit_all_transactions ? total_intent_records : total_intent_records / 2;
    auto test_thread_holder = TestThreadHolder();
    test_thread_holder.AddThread([&]() {
      auto commit_transaction = true;
      for (uint32_t i = 0; i < total_intent_records; i += transaction_size) {
        ASSERT_NO_FATALS(WriteTransactionalWorkload(
            i, i + transaction_size, &producer_cluster_, producer_table,
            commit_all_transactions || commit_transaction));
        commit_transaction = !commit_transaction;
        LOG(INFO) << "Wrote records: " << i + transaction_size;
      }
    });

    test_thread_holder.AddThread([&]() {
      uint32_t num_read_records = 0;
      while (num_read_records < total_committed_records) {
        auto consumer_results = ScanToStrings(consumer_table, &consumer_cluster_);
        num_read_records = PQntuples(consumer_results.get());
        ASSERT_EQ(num_read_records % transaction_size, 0);
        LOG(INFO) << "Read records: " << num_read_records;
        if (commit_all_transactions) {
          for (uint32_t i = 0; i < num_read_records; ++i) {
            auto val = ASSERT_RESULT(GetInt32(consumer_results.get(), i, 0));
            ASSERT_EQ(val, i);
          }
        }
      }
      ASSERT_EQ(num_read_records, total_committed_records);
    });

    test_thread_holder.JoinAll();
  }

  void AsyncTransactionConsistencyTest(const YBTableName& producer_table,
                                       const YBTableName& consumer_table,
                                       TestThreadHolder* test_thread_holder,
                                       MonoDelta duration) {
    // Create a writer thread for transactions of size 10 and and read thread to validate
    // transactional atomicity. Run both threads for duration.
    const auto transaction_size = 10;
    test_thread_holder->AddThread([this, &producer_table, duration]() {
      int32_t key = 0;
      auto producer_conn =  ASSERT_RESULT(
          producer_cluster_.ConnectToDB(producer_table.namespace_name()));
      auto now = CoarseMonoClock::Now();
      while (CoarseMonoClock::Now() < now + duration) {
        ASSERT_OK(producer_conn.ExecuteFormat(
            "insert into $0 values(generate_series($1, $2))",
            GetCompleteTableName(producer_table), key, key + transaction_size - 1));
        key += transaction_size;
      }
      // Assert at least 100 transactions were written.
      ASSERT_GE(key, transaction_size * 100);
    });

    test_thread_holder->AddThread([this, &consumer_table, duration]() {
      auto consumer_conn = ASSERT_RESULT(
          consumer_cluster_.ConnectToDB(consumer_table.namespace_name()));
      auto now = CoarseMonoClock::Now();
      while (CoarseMonoClock::Now() < now + duration) {
        auto result = ASSERT_RESULT(consumer_conn.FetchFormat(
            "select count(*) from $0", GetCompleteTableName(consumer_table)));
        auto count = ASSERT_RESULT(GetValue<int64_t>(result.get(), 0, 0));
        ASSERT_EQ(count % transaction_size , 0);
      }
    });
  }

  virtual Result<std::pair<client::YBTablePtr, client::YBTablePtr>> CreateClusterAndTable(
      int num_masters = 3) {
    auto tables = VERIFY_RESULT(SetUpWithParams({4}, {4}, 3, num_masters));
    auto producer_table = tables[0];
    auto consumer_table = tables[1];
    return std::make_pair(producer_table, consumer_table);
  }

  Status SetupReplicationAndWaitForValidSafeTime(
      const std::pair<client::YBTablePtr, client::YBTablePtr>& tables) {
    auto producer_table = tables.first;
    auto consumer_table = tables.second;
    std::vector<std::shared_ptr<client::YBTable>> tables_to_replicate = {producer_table};
    RETURN_NOT_OK(SetupUniverseReplication(
        kUniverseId, tables_to_replicate, {LeaderOnly::kTrue, Transactional::kTrue}));
    // Verify that universe was setup on consumer.
    master::GetUniverseReplicationResponsePB resp;
    RETURN_NOT_OK(VerifyUniverseReplication(kUniverseId, &resp));
    RETURN_NOT_OK(ChangeXClusterRole(cdc::XClusterRole::STANDBY));
    RETURN_NOT_OK(WaitForValidSafeTimeOnAllTServers(consumer_table->name().namespace_id()));
    return Status::OK();
  }

  Result<std::pair<client::YBTablePtr, client::YBTablePtr>> CreateTableAndSetupReplication(
      int num_masters = 3) {
    const auto tables = VERIFY_RESULT(CreateClusterAndTable(num_masters));
    RETURN_NOT_OK(SetupReplicationAndWaitForValidSafeTime(tables));
    return tables;
  }

  Status WaitForIntentsCleanedUpOnConsumer() {
    return WaitFor(
        [&]() {
          if (CountIntents(consumer_cluster()) == 0) {
            VerifyExternalTxnIntentsStateEmpty();
            return true;
          }
          return false;
        },
        MonoDelta::FromSeconds(30), "Intents cleaned up");
  }
};

constexpr uint32_t kTransactionSize = 50;
constexpr uint32_t kNumTransactions = 100;

TEST_F(XClusterYSqlTestConsistentTransactionsTest, ConsistentTransactions) {
  auto tables_pair = ASSERT_RESULT(CreateTableAndSetupReplication());

  ASSERT_NO_FATALS(MultiTransactionConsistencyTest(
      kTransactionSize, kNumTransactions, tables_pair.first->name(), tables_pair.second->name(),
      true /* commit_all_transactions */));

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, LargeTransaction) {
  auto tables_pair = ASSERT_RESULT(CreateTableAndSetupReplication());

  ASSERT_NO_FATALS(MultiTransactionConsistencyTest(
      10000, 1, tables_pair.first->name(), tables_pair.second->name(),
      true /* commit_all_transactions */));

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, ManySmallTransactions) {
  auto tables_pair = ASSERT_RESULT(CreateTableAndSetupReplication());

  ASSERT_NO_FATALS(MultiTransactionConsistencyTest(
      2, 500, tables_pair.first->name(), tables_pair.second->name(),
      true /* commit_all_transactions */));

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, UncommittedTransactions) {
  auto tables_pair = ASSERT_RESULT(CreateTableAndSetupReplication());

  ASSERT_NO_FATALS(MultiTransactionConsistencyTest(
      kTransactionSize, kNumTransactions, tables_pair.first->name(), tables_pair.second->name(),
      false /* commit_all_transactions */));

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, NonTransactionalWorkload) {
  // Write 10000 rows non-transactionally to ensure there's no regression for non-transactional
  // workloads.
  auto tables_pair = ASSERT_RESULT(CreateTableAndSetupReplication());

  ASSERT_NO_FATALS(WriteWorkload(0, 10000, &producer_cluster_, tables_pair.first->name()));

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, TransactionSpanningMultipleBatches) {
  // Write a large transaction spanning multiple write batches and then delete all rows on both
  // producer and consumer and ensure we still maintain read consistency and can properly apply
  // intents.
  auto tables_pair = ASSERT_RESULT(CreateTableAndSetupReplication());
  auto producer_table = tables_pair.first;
  auto consumer_table = tables_pair.second;

  auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(producer_table->name().namespace_name()));
  std::string table_name_str = GetCompleteTableName(producer_table->name());
  ASSERT_OK(conn.ExecuteFormat("insert into $0 values(generate_series(0, 20000))", table_name_str));
  auto tablet_peer_leaders = ListTabletPeers(producer_cluster(), ListPeersFilter::kLeaders);
  ASSERT_OK(WaitForIntentsCleanedUpOnConsumer());
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));

  ASSERT_OK(conn.ExecuteFormat("delete from $0", table_name_str));
  ASSERT_OK(WaitForIntentsCleanedUpOnConsumer());
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, TransactionsWithUpdates) {
  // Write a transactional workload of updates with valdation for 30s and ensure there are no
  // FATALs and that we maintain consistent reads.
  const auto namespace_name = "demo";
  const auto table_name = "account_balance";

  ASSERT_OK(Initialize(3 /* replication_factor */, 1 /* num_masters */));

  ASSERT_OK(RunOnBothClusters(
      [&](Cluster* cluster) { return CreateDatabase(cluster, namespace_name); }));
  ASSERT_OK(RunOnBothClusters([&](Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
    return conn.ExecuteFormat("create table $0(id int, name text, salary int);", table_name);
  }));

  auto table_name_with_id_list = ASSERT_RESULT(producer_client()->ListTables(table_name));
  ASSERT_EQ(table_name_with_id_list.size(), 1);
  auto table_name_with_id = table_name_with_id_list[0];
  ASSERT_TRUE(table_name_with_id.has_table_id());
  auto yb_table = ASSERT_RESULT(producer_client()->OpenTable(table_name_with_id.table_id()));

  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));
  ASSERT_OK(
      SetupUniverseReplication(kUniverseId, {yb_table}, {LeaderOnly::kTrue, Transactional::kTrue}));
  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(kUniverseId, &resp));
  ASSERT_OK(ChangeXClusterRole(cdc::XClusterRole::STANDBY));
  ASSERT_OK(WaitForValidSafeTimeOnAllTServers(table_name_with_id.namespace_id()));

  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));

  static const int num_users = 3;
  for (int i = 0; i < num_users; i++) {
    ASSERT_OK(producer_conn.ExecuteFormat(
        "INSERT INTO account_balance VALUES($0, 'user$0', 1000000)", i));
  }

  auto result = ASSERT_RESULT(producer_conn.FetchFormat("select sum(salary) from account_balance"));
  ASSERT_EQ(PQntuples(result.get()), 1);
  auto total_salary = ASSERT_RESULT(GetValue<int64_t>(result.get(), 0, 0));

  // Transactional workload
  auto test_thread_holder = TestThreadHolder();
  test_thread_holder.AddThread([this, namespace_name]() {
    std::string update_query;
    for (int i = 0; i < num_users - 1; i++) {
      update_query += Format(
          "UPDATE account_balance SET salary = salary - 500 WHERE name = 'user$0';", i);
    }
    update_query += Format(
        "UPDATE account_balance SET salary = salary + $0 WHERE name = 'user$1';",
        500 * (num_users - 1), num_users - 1);
    auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
    auto now = CoarseMonoClock::Now();
    while (CoarseMonoClock::Now() < now + MonoDelta::FromSeconds(30)) {
      ASSERT_OK(producer_conn.ExecuteFormat("BEGIN TRANSACTION; $0; COMMIT;", update_query));
    }
  });


  // Read validate workload.
  test_thread_holder.AddThread([this, namespace_name, total_salary]() {
    auto now = CoarseMonoClock::Now();
    while (CoarseMonoClock::Now() < now + MonoDelta::FromSeconds(30)) {
      auto consumer_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
      auto result = ASSERT_RESULT(consumer_conn.FetchFormat(
          "select sum(salary) from account_balance"));
      ASSERT_EQ(PQntuples(result.get()), 1);
      Result<int64_t> current_salary_result = GetValue<int64_t>(result.get(), 0, 0);
      if (!current_salary_result.ok()) {
        continue;
      }
      ASSERT_EQ(total_salary, *current_salary_result);
    }
  });

  test_thread_holder.JoinAll();
  ASSERT_OK(WaitForIntentsCleanedUpOnConsumer());
  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, AddServerBetweenTransactions) {
  auto tables_pair = ASSERT_RESULT(CreateTableAndSetupReplication());
  auto producer_table = tables_pair.first;
  auto consumer_table = tables_pair.second;

  auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);
  auto test_thread_holder = TestThreadHolder();
  ASSERT_NO_FATALS(AsyncTransactionConsistencyTest(
      producer_table->name(), consumer_table->name(), &test_thread_holder, duration));

  // Sleep for half duration to ensure that the workloads AsyncTransactionConsistencyTest are
  // running.
  SleepFor(duration/2);
  ASSERT_OK(consumer_cluster()->AddTabletServer());
  ASSERT_OK(
      consumer_cluster()->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout)));

  test_thread_holder.JoinAll();

  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, AddServerIntraTransaction) {
  auto tables_pair = ASSERT_RESULT(CreateTableAndSetupReplication());
  auto producer_table = tables_pair.first;
  auto consumer_table = tables_pair.second;

  auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(producer_table->name().namespace_name()));
  std::string table_name_str = GetCompleteTableName(producer_table->name());

  auto test_thread_holder = TestThreadHolder();
  test_thread_holder.AddThread([&conn, &table_name_str] {
    ASSERT_OK(conn.Execute("BEGIN"));
    int32_t key = 0;
    int32_t step = 10;
    auto now = CoarseMonoClock::Now();
    while (CoarseMonoClock::Now() < now + MonoDelta::FromSeconds(30)) {
      ASSERT_OK(conn.ExecuteFormat(
          "insert into $0 values(generate_series($1, $2))", table_name_str, key, key + step - 1));
      key += step;
    }
    ASSERT_OK(conn.Execute("COMMIT"));
  });

  // Sleep for half the duration of the workload (30s) to ensure that the workload is running before
  // adding a server.
  SleepFor(MonoDelta::FromSeconds(15));
  ASSERT_OK(consumer_cluster()->AddTabletServer());
  ASSERT_OK(
      consumer_cluster()->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout)));

  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, RefreshCheckpointAfterRestart) {
  auto tables_pair = ASSERT_RESULT(CreateTableAndSetupReplication());
  FLAGS_TEST_force_get_checkpoint_from_cdc_state = true;
  auto producer_table = tables_pair.first;
  auto consumer_table = tables_pair.second;

  auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(producer_table->name().namespace_name()));
  std::string table_name_str = GetCompleteTableName(producer_table->name());

  auto test_thread_holder = TestThreadHolder();
  test_thread_holder.AddThread([&conn, &table_name_str] {
    ASSERT_OK(conn.Execute("BEGIN"));
    int32_t key = 0;
    int32_t step = 10;
    auto now = CoarseMonoClock::Now();
    while (CoarseMonoClock::Now() < now + MonoDelta::FromSeconds(30)) {
      ASSERT_OK(conn.ExecuteFormat(
          "insert into $0 values(generate_series($1, $2))", table_name_str, key, key + step - 1));
      key += step;
    }
    ASSERT_OK(conn.Execute("COMMIT"));
  });

  test_thread_holder.JoinAll();
  ASSERT_OK(WaitForIntentsCleanedUpOnConsumer());
  ASSERT_OK(consumer_cluster()->AddTabletServer());
  ASSERT_OK(
      consumer_cluster()->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout)));

  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}


TEST_F(XClusterYSqlTestConsistentTransactionsTest, RestartServer) {
  auto tables_pair = ASSERT_RESULT(CreateTableAndSetupReplication());
  auto producer_table = tables_pair.first;
  auto consumer_table = tables_pair.second;

  auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);
  auto test_thread_holder = TestThreadHolder();
  ASSERT_NO_FATALS(AsyncTransactionConsistencyTest(
      producer_table->name(), consumer_table->name(), &test_thread_holder, duration));

  // Sleep for half duration to ensure that the workloads AsyncTransactionConsistencyTest are
  // running.
  SleepFor(duration/2);
  auto restart_idx = consumer_cluster_.pg_ts_idx_ == 0 ? 1 : 0;
  consumer_cluster()->mini_tablet_server(restart_idx)->Shutdown();
  ASSERT_OK(
      consumer_cluster()->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout)));

  test_thread_holder.JoinAll();
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}


TEST_F(XClusterYSqlTestConsistentTransactionsTest, MasterLeaderRestart) {
  auto tables_pair = ASSERT_RESULT(CreateTableAndSetupReplication(3 /* num_masters */));
  auto producer_table = tables_pair.first;
  auto consumer_table = tables_pair.second;

  auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);
  auto test_thread_holder = TestThreadHolder();
  ASSERT_NO_FATALS(AsyncTransactionConsistencyTest(
      producer_table->name(), consumer_table->name(), &test_thread_holder, duration));

  // Sleep for half duration to ensure that the workloads AsyncTransactionConsistencyTest are
  // running.
  SleepFor(duration/2);
  auto* mini_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
  mini_master->Shutdown();
  ASSERT_OK(
      consumer_cluster()->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout)));
  test_thread_holder.JoinAll();
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, TransactionsSpanningConsensusMaxBatchSize) {
  FLAGS_consensus_max_batch_size_bytes = 8_KB;
  auto tables_pair = ASSERT_RESULT(CreateTableAndSetupReplication());
  auto producer_table = tables_pair.first;
  auto consumer_table = tables_pair.second;

  auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);
  auto test_thread_holder = TestThreadHolder();
  ASSERT_NO_FATALS(AsyncTransactionConsistencyTest(
      producer_table->name(), consumer_table->name(), &test_thread_holder, duration));

  auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(producer_table->name().namespace_name()));
  for (int i = 0; i < 6; i++) {
    SleepFor(duration/6);
    ASSERT_OK(conn.ExecuteFormat("delete from $0", GetCompleteTableName(producer_table->name())));
  }

  test_thread_holder.JoinAll();
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, ReplicationPause) {
  auto tables_pair = ASSERT_RESULT(CreateTableAndSetupReplication());
  auto producer_table = tables_pair.first;
  auto consumer_table = tables_pair.second;

  auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);
  auto test_thread_holder = TestThreadHolder();
  ASSERT_NO_FATALS(AsyncTransactionConsistencyTest(
      producer_table->name(), consumer_table->name(), &test_thread_holder, duration));
  SleepFor(duration/2);
  // Pause replication here for half the duration of the workload.
  ASSERT_OK(ToggleUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, false));
  SleepFor(duration/2);
  // Resume replication.
  ASSERT_OK(ToggleUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, true));
  test_thread_holder.JoinAll();
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, GarbageCollectExpiredTransactions) {
  // This test ensures that transactions older than the retention window are cleaned up on both
  // the coordinator and participant.
  auto tables_pair = ASSERT_RESULT(CreateTableAndSetupReplication());
  auto producer_table = tables_pair.first;
  auto consumer_table = tables_pair.second;

  // Write 2 transactions that are both not committed.
  ASSERT_NO_FATALS(WriteTransactionalWorkload(
      0, 49, &producer_cluster_, producer_table->name(), false /* commit_tranasction */));
  master::WaitForReplicationDrainRequestPB req;
  PopulateWaitForReplicationDrainRequest({tables_pair.first}, &req);
  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &producer_client()->proxy_cache(),
      ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
  ASSERT_OK(WaitForReplicationDrain(master_proxy, req, 0 /* expected_num_nondrained */));
  ASSERT_OK(consumer_cluster()->FlushTablets());
  ASSERT_NO_FATALS(WriteTransactionalWorkload(
      50, 99, &producer_cluster_, producer_table->name(), false /* commit_tranasction */));
  ASSERT_OK(WaitForReplicationDrain(master_proxy, req, 0 /* expected_num_nondrained */));
  ASSERT_OK(consumer_cluster()->FlushTablets());
  // Delete universe replication now so that new external transactions from the transaction pool
  // do not come in.
  ASSERT_OK(DeleteUniverseReplication(kUniverseId));

  // Trigger a compaction and ensure that the transaction has been cleaned up on the participant.
  FLAGS_aborted_intent_cleanup_ms = 0;
  FLAGS_external_intent_cleanup_secs = 0;
  ASSERT_OK(consumer_cluster()->FlushTablets());
  ASSERT_OK(consumer_cluster()->CompactTablets());
  ASSERT_OK(WaitForIntentsCleanedUpOnConsumer());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, UnevenTxnStatusTablets) {
  // Keep same tablet count for normal tablets.
  auto tables_pair = ASSERT_RESULT(CreateClusterAndTable());
  auto producer_table = tables_pair.first;
  auto consumer_table = tables_pair.second;
  const auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);

  auto setup_write_verify_delete = [&](bool perform_bootstrap = false) {
    if (!perform_bootstrap) {
      ASSERT_OK(SetupReplicationAndWaitForValidSafeTime(tables_pair));
    } else {
      auto bootstrap_ids =
          ASSERT_RESULT(BootstrapProducer(producer_cluster(), producer_client(), {producer_table}));
      ASSERT_OK(SetupUniverseReplication(
          producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, {producer_table},
          bootstrap_ids, {LeaderOnly::kFalse, Transactional::kTrue}));
      ASSERT_OK(ChangeXClusterRole(cdc::XClusterRole::STANDBY));
      ASSERT_OK(WaitForValidSafeTimeOnAllTServers(consumer_table->name().namespace_id()));
    }

    auto test_thread_holder = TestThreadHolder();
    ASSERT_NO_FATALS(AsyncTransactionConsistencyTest(
        producer_table->name(), consumer_table->name(), &test_thread_holder, duration));
    test_thread_holder.JoinAll();
    ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
    ASSERT_OK(DeleteUniverseReplication(kUniverseId));
  };

  // Create an additional transaction tablet on the producer before starting replication.
  auto global_txn_table_id = ASSERT_RESULT(client::GetTableId(
      producer_client(), producer_transaction_table_name));
  ASSERT_OK(producer_client()->AddTransactionStatusTablet(global_txn_table_id));

  setup_write_verify_delete();

  // Now add 2 txn tablets on the consumer, then rerun test.
  global_txn_table_id = ASSERT_RESULT(client::GetTableId(
      consumer_client(), producer_transaction_table_name));
  ASSERT_OK(consumer_client()->AddTransactionStatusTablet(global_txn_table_id));
  ASSERT_OK(consumer_client()->AddTransactionStatusTablet(global_txn_table_id));
  // Reset the role and data before setting up replication again.
  ASSERT_OK(ChangeXClusterRole(cdc::XClusterRole::ACTIVE));
  ASSERT_OK(WaitForRoleChangeToPropogateToAllTServers(cdc::XClusterRole::ACTIVE));
  ASSERT_OK(RunOnBothClusters([&](Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(producer_table->name().namespace_name()));
    return conn.ExecuteFormat("delete from $0;", producer_table->name().table_name());
  }));

  setup_write_verify_delete(true /* perform_bootstrap */);
}

class XClusterYSqlTestStressTest : public XClusterYSqlTestConsistentTransactionsTest {
  void SetUp() override {
    FLAGS_rpc_workers_limit = 8;
    FLAGS_tablet_server_svc_queue_length = 10;
    XClusterYSqlTestConsistentTransactionsTest::SetUp();
  }
};

TEST_F(XClusterYSqlTestStressTest, ApplyTranasctionThrottling) {
  // After a boostrap or a network partition, it is possible that there many unreplicated
  // transactions that the consumer receives at once. Specifically, the consumer's
  // coordinator must commit and then apply many transactions at once. Ensure that there is
  // sufficient throttling on the coordinator to prevent RPC bottlenecks in this situation.
  auto tables_pair = ASSERT_RESULT(CreateTableAndSetupReplication());
  // Pause replication to allow unreplicated data to accumulate.
  FLAGS_TEST_cdc_skip_replication_poll = true;
  auto producer_table = tables_pair.first;
  auto consumer_table = tables_pair.second;
  auto table_name_str =  GetCompleteTableName(producer_table->name());
  auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(producer_table->name().namespace_name()));
  int key = 0;
  int step = 10;
  // Write 30s worth of transactions.
  auto now = CoarseMonoClock::Now();
  while (CoarseMonoClock::Now() < now + MonoDelta::FromSeconds(30)) {
    ASSERT_OK(conn.ExecuteFormat(
        "insert into $0 values(generate_series($1, $2))", table_name_str, key, key + step - 1));
    key += step;
  }
  // Enable replication and ensure that the coordinator can handle 30s worth of data all at once.
  FLAGS_TEST_cdc_skip_replication_poll = false;
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

TEST_F(XClusterYsqlTest, GenerateSeriesMultipleTransactions) {
  // Use a 4 -> 1 mapping to ensure that multiple transactions are processed by the same tablet.
  auto tables = ASSERT_RESULT(SetUpWithParams({1}, {4}, 3, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  auto producer_table = tables[0];
  auto consumer_table = tables[1];
  ASSERT_NO_FATALS(WriteGenerateSeries(0, 50, &producer_cluster_, producer_table->name()));
  ASSERT_NO_FATALS(WriteGenerateSeries(51, 100, &producer_cluster_, producer_table->name()));
  ASSERT_NO_FATALS(WriteGenerateSeries(101, 150, &producer_cluster_, producer_table->name()));
  ASSERT_NO_FATALS(WriteGenerateSeries(151, 200, &producer_cluster_, producer_table->name()));
  ASSERT_OK(SetupUniverseReplication(kUniverseId, {producer_table}));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(kUniverseId, &resp));
  ASSERT_EQ(resp.entry().producer_id(), kUniverseId);
  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), producer_table->id());
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));

  VerifyExternalTxnIntentsStateEmpty();
}

TEST_F(XClusterYsqlTest, ChangeRole) {
  // 1. Test that an existing universe without replication of txn status table cannot become a
  // STANDBY.
  auto tables = ASSERT_RESULT(SetUpWithParams({1}, {1}, 3, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));
  auto producer_table = tables[0];
  auto consumer_table = tables[1];
  ASSERT_OK(SetupUniverseReplication(kUniverseId, {producer_table}));

  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(kUniverseId, &resp));
  ASSERT_EQ(resp.entry().producer_id(), kUniverseId);

  ASSERT_NOK(ChangeXClusterRole(cdc::XClusterRole::STANDBY));
  ASSERT_OK(DeleteUniverseReplication(kUniverseId));

  // 2. Test that a universe cannot change a role to its same role.
  ASSERT_OK(SetupUniverseReplication(
      kUniverseId, {producer_table}, {LeaderOnly::kFalse, Transactional::kTrue}));

  ASSERT_OK(VerifyUniverseReplication(kUniverseId, &resp));
  ASSERT_EQ(resp.entry().producer_id(), kUniverseId);

  ASSERT_NOK(ChangeXClusterRole(cdc::XClusterRole::ACTIVE));

  // 3. Test that a change role to STANDBY mode succeeds.
  ASSERT_OK(ChangeXClusterRole(cdc::XClusterRole::STANDBY));

  // 4. Test that a change of role back to ACTIVE mode succeeds.
  ASSERT_OK(ChangeXClusterRole(cdc::XClusterRole::ACTIVE));
  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

TEST_F(XClusterYsqlTest, SetupUniverseReplication) {
  auto tables = ASSERT_RESULT(SetUpWithParams({8, 4}, {6, 6}, 3, 1, false /* colocated */));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  producer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); i += 2) {
    producer_tables.push_back(tables[i]);
  }
  ASSERT_OK(SetupUniverseReplication(kUniverseId, producer_tables));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(kUniverseId, &resp));
  ASSERT_EQ(resp.entry().producer_id(), kUniverseId);
  ASSERT_EQ(resp.entry().tables_size(), producer_tables.size());
  for (size_t i = 0; i < producer_tables.size(); i++) {
    ASSERT_EQ(resp.entry().tables(narrow_cast<int>(i)), producer_tables[i]->id());
  }

  // Verify that CDC streams were created on producer for all tables.
  for (size_t i = 0; i < producer_tables.size(); i++) {
    master::ListCDCStreamsResponsePB stream_resp;
    ASSERT_OK(GetCDCStreamForTable(producer_tables[i]->id(), &stream_resp));
    ASSERT_EQ(stream_resp.streams_size(), 1);
    ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), producer_tables[i]->id());
  }

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

TEST_F(XClusterYsqlTest, SetupUniverseReplicationWithYbAdmin) {
  auto tables = ASSERT_RESULT(SetUpWithParams({1}, {1}, 3, 1));
  auto producer_table = tables[0];
  const string kProducerClusterId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  ASSERT_OK(CallAdmin(
      consumer_cluster(),
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster()->GetMasterAddresses(),
      producer_table->id(),
      "transactional"));

  ASSERT_OK(CallAdmin(consumer_cluster(), "change_xcluster_role", "STANDBY"));
  ASSERT_OK(CallAdmin(consumer_cluster(), "delete_universe_replication", kProducerClusterId));
}

void XClusterYsqlTest::ValidateSimpleReplicationWithPackedRowsUpgrade(
    std::vector<uint32_t> consumer_tablet_counts, std::vector<uint32_t> producer_tablet_counts,
    uint32_t num_tablet_servers, bool range_partitioned) {
  FLAGS_ysql_enable_packed_row = false;

  constexpr auto kNumRecords = 1000;
  auto tables = ASSERT_RESULT(SetUpWithParams(
      consumer_tablet_counts, producer_tablet_counts, num_tablet_servers, 1, false, boost::none,
      range_partitioned));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
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
    WriteWorkload(0, kNumRecords, &producer_cluster_, producer_table->name());
  }

  // Verify data is written on the producer.
  for (const auto& producer_table : producer_tables) {
    auto producer_results = ScanToStrings(producer_table->name(), &producer_cluster_);
    ASSERT_EQ(kNumRecords, PQntuples(producer_results.get()));
    int result;
    for (int i = 0; i < kNumRecords; ++i) {
      result = ASSERT_RESULT(GetInt32(producer_results.get(), i, 0));
      ASSERT_EQ(i, result);
    }
  }

  // 2. Setup replication.
  ASSERT_OK(SetupUniverseReplication(kUniverseId, producer_tables));

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(kUniverseId, &get_universe_replication_resp));
  uint32_t num_producer_tablets = 0;
  for (const auto count : producer_tablet_counts) {
    num_producer_tablets += count;
  }
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), num_producer_tablets));

  auto data_replicated_correctly = [&](int num_results) -> Result<bool> {
    for (const auto& consumer_table : consumer_tables) {
      LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
      auto consumer_results = ScanToStrings(consumer_table->name(), &consumer_cluster_);
      auto consumer_results_size = PQntuples(consumer_results.get());
      if (num_results != consumer_results_size) {
        return false;
      }
      int result;
      for (int i = 0; i < num_results; ++i) {
        result = VERIFY_RESULT(GetInt32(consumer_results.get(), i, 0));
        if (i != result) {
          return false;
        }
      }
    }
    return true;
  };
  ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(kNumRecords); },
                    MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

  // 4. Write more data.
  for (const auto& producer_table : producer_tables) {
    WriteWorkload(kNumRecords, kNumRecords + 5, &producer_cluster_, producer_table->name());
  }

  // 5. Make sure this data is also replicated now.
  ASSERT_OK(WaitFor(
      [&]() { return data_replicated_correctly(kNumRecords + 5); }, MonoDelta::FromSeconds(20),
      "IsDataReplicatedCorrectly"));

  // Enable packing
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_packed_row_size_limit) = 1_KB;

  // Disable the replication and ensure no tablets are being polled
  ASSERT_OK(ToggleUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, false));
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 0));

  // 6. Write packed data.
  for (const auto& producer_table : producer_tables) {
    WriteWorkload(kNumRecords + 5, kNumRecords + 10, &producer_cluster_, producer_table->name());
  }

  // 7. Disable packing and resume replication
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;
  ASSERT_OK(ToggleUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, true));
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), num_producer_tablets));
  ASSERT_OK(WaitFor(
      [&]() { return data_replicated_correctly(kNumRecords + 10); }, MonoDelta::FromSeconds(20),
      "IsDataReplicatedCorrectly"));

  // 8. Write some non-packed data on consumer.
  for (const auto& consumer_table : consumer_tables) {
    WriteWorkload(kNumRecords + 10, kNumRecords + 15, &producer_cluster_, consumer_table->name());
  }

  // 9. Make sure full scan works now.
  ASSERT_OK(WaitFor(
      [&]() { return data_replicated_correctly(kNumRecords + 15); }, MonoDelta::FromSeconds(20),
      "IsDataReplicatedCorrectly"));

  // 10. Re-enable Packed Columns and add a column and drop it so that schema stays the same but the
  // schema_version is different on the Producer and Consumer
  FLAGS_ysql_enable_packed_row = true;
  {
    string new_col = "dummy";
    auto tbl = consumer_tables[0]->name();
    auto conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 INT", tbl.table_name(), new_col));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN $1", tbl.table_name(), new_col));
  }

  // 11. Write some packed rows on producer and verify that those can be read from consumer
  for (const auto& producer_table : producer_tables) {
    WriteWorkload(kNumRecords + 15, kNumRecords + 20, &producer_cluster_, producer_table->name());
  }

  // 12. Verify that all the data can be read now.
  ASSERT_OK(WaitFor(
      [&]() { return data_replicated_correctly(kNumRecords + 20); }, MonoDelta::FromSeconds(20),
      "IsDataReplicatedCorrectly"));

  // 13. Compact the table and validate data.
  ASSERT_OK(consumer_cluster()->CompactTablets());

  ASSERT_OK(WaitFor(
      [&]() { return data_replicated_correctly(kNumRecords + 20); }, MonoDelta::FromSeconds(20),
      "IsDataReplicatedCorrectly"));
}

TEST_F(XClusterYsqlTest, SimpleReplication) {
  ValidateSimpleReplicationWithPackedRowsUpgrade(
      /* consumer_tablet_counts */ {1, 1}, /* producer_tablet_counts */ {1, 1});
}

TEST_F(XClusterYsqlTest, SimpleReplicationWithUnevenTabletCounts) {
  ValidateSimpleReplicationWithPackedRowsUpgrade(
      /* consumer_tablet_counts */ {5, 3}, /* producer_tablet_counts */ {3, 5},
      /* num_tablet_servers */ 3);
}

TEST_F(XClusterYsqlTest, SimpleReplicationWithRangedPartitions) {
  ValidateSimpleReplicationWithPackedRowsUpgrade(
      /* consumer_tablet_counts */ {1, 1}, /* producer_tablet_counts */ {1, 1},
      /* num_tablet_servers */ 1, /* range_partitioned */ true);
}

TEST_F(XClusterYsqlTest, SimpleReplicationWithRangedPartitionsAndUnevenTabletCounts) {
  ValidateSimpleReplicationWithPackedRowsUpgrade(
      /* consumer_tablet_counts */ {5, 3}, /* producer_tablet_counts */ {3, 5},
      /* num_tablet_servers */ 3, /* range_partitioned */ true);
}

TEST_F(XClusterYsqlTest, ReplicationWithBasicDDL) {
  SetAtomicFlag(true, &FLAGS_xcluster_wait_on_ddl_alter);
  FLAGS_ysql_enable_packed_row = true;
  string new_column = "contact_name";

  constexpr auto kRecordBatch = 5;
  auto count = 0;
  constexpr int kNTabletsPerTable = 4;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // Tables contains both producer and consumer universe tables (alternately).
  ASSERT_EQ(tables.size(), 2);
  std::shared_ptr<client::YBTable> producer_table(tables[0]), consumer_table(tables[1]);

  /***************************/
  /********   SETUP   ********/
  /***************************/
  // 1. Write some data.
  LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
  WriteWorkload(count, count + kRecordBatch, &producer_cluster_, producer_table->name());
  count += kRecordBatch;

  // 2. Setup replication.
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, {producer_table}));

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId,
      &get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<uint32_t>(tables_vector.size() * kNTabletsPerTable)));

  auto data_replicated_correctly = [&](int num_results) -> Result<bool> {
    LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
    auto consumer_results = ScanToStrings(consumer_table->name(), &consumer_cluster_);
    auto consumer_results_size = PQntuples(consumer_results.get());
    LOG(INFO) << "data_replicated_correctly Found = " << consumer_results_size;
    if (num_results != consumer_results_size) {
      return false;
    }
    int result;
    for (int i = 0; i < num_results; ++i) {
      result = VERIFY_RESULT(GetInt32(consumer_results.get(), i, 0));
      if (i != result) {
        return false;
      }
    }
    return true;
  };
  ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(count); },
      MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

  // 4. Write more data.
  WriteWorkload(count, count + kRecordBatch, &producer_cluster_, producer_table->name());
  count += kRecordBatch;

  // 5. Make sure this data is also replicated now.
  ASSERT_OK(WaitFor([&]() { return data_replicated_correctly(count); },
      MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

  /***************************/
  /******* ADD COLUMN ********/
  /***************************/

  // Pause Replication so we can batch up the below GetChanges information.
  ASSERT_OK(ToggleUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, false));
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 0));

  // Write some new data to the producer.
  WriteWorkload(count, count + kRecordBatch, &producer_cluster_, producer_table->name());
  count += kRecordBatch;

  // 1. ALTER Table on the Producer.
  {
    auto tbl = producer_table->name();
    auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 VARCHAR",
                                 tbl.table_name(), new_column));
  }

  // 2. Write more data so we have some entries with the new schema.
  WriteWorkload(count, count + kRecordBatch, &producer_cluster_, producer_table->name());

  // Resume Replication.
  ASSERT_OK(ToggleUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, true));

  // We read the first batch of writes with the old schema, but not the new schema writes.
  LOG(INFO) << "Consumer count after Producer ALTER halted polling = "
            << EXPECT_RESULT(data_replicated_correctly(count));

  // 4. ALTER Table on the Consumer.
  {
    // Matching schema to producer should succeed.
    auto tbl = consumer_table->name();
    auto conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 VARCHAR",
                                 tbl.table_name(), new_column));
  }

  // 5. Verify Replication continued and new schema Producer entries are added to Consumer.
  count += kRecordBatch;
  ASSERT_OK(WaitFor([&]() { return data_replicated_correctly(count); },
            MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

  /***************************/
  /****** RENAME COLUMN ******/
  /***************************/
  // 1. ALTER Table to Remove the Column on Producer.
  {
    auto tbl = producer_table->name();
    auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 RENAME COLUMN $1 TO $2_new",
                                 tbl.table_name(), new_column, new_column));
  }

  // 2. Write more data so we have some entries with the new schema.
  WriteWorkload(count, count + kRecordBatch, &producer_cluster_, producer_table->name());

  // 3. Verify ALTER was parsed by Consumer, which stopped replication and hasn't read the new data.
  LOG(INFO) << "Consumer count after Producer ALTER halted polling = "
            << EXPECT_RESULT(data_replicated_correctly(count));

  // 4. ALTER Table on the Consumer.
  {
    auto tbl = consumer_table->name();
    auto conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(tbl.namespace_name()));
    // Matching schema to producer should succeed.
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 RENAME COLUMN $1 TO $2_new",
                                 tbl.table_name(), new_column, new_column));
  }

  // 5. Verify Replication continued and new schema Producer entries are added to Consumer.
  count += kRecordBatch;
  ASSERT_OK(WaitFor([&]() { return data_replicated_correctly(count); },
            MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

  new_column = new_column + "_new";

  /***************************/
  /****** BATCH ADD COLS *****/
  /***************************/
  // 1. ALTER Table on the Producer.
  {
    auto tbl = producer_table->name();
    auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN BATCH_1 VARCHAR, "
                                                "ADD COLUMN BATCH_2 VARCHAR, "
                                                "ADD COLUMN BATCH_3 INT", tbl.table_name()));
  }

  // 2. Write more data so we have some entries with the new schema.
  WriteWorkload(count, count + kRecordBatch, &producer_cluster_, producer_table->name());

  // 3. Verify ALTER was parsed by Consumer, which stopped replication and hasn't read the new data.
  LOG(INFO) << "Consumer count after Producer ALTER halted polling = "
            << EXPECT_RESULT(data_replicated_correctly(count));

  // 4. ALTER Table on the Consumer.
  {
    auto tbl = consumer_table->name();
    auto conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(tbl.namespace_name()));
    // Subsequent Matching schema to producer should succeed.
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN BATCH_1 VARCHAR, "
                                                "ADD COLUMN BATCH_2 VARCHAR, "
                                                "ADD COLUMN BATCH_3 INT", tbl.table_name()));
  }

  // 5. Verify Replication continued and new schema Producer entries are added to Consumer.
  count += kRecordBatch;
  ASSERT_OK(WaitFor([&]() { return data_replicated_correctly(count); },
      MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

  /***************************/
  /**** DROP/RE-ADD COLUMN ***/
  /***************************/
  //  1. Run on Producer: DROP NewCol, Add Data,
  //                      ADD NewCol again (New ID), Add Data.
  {
    auto tbl = producer_table->name();
    auto tname = tbl.table_name();
    auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN $1", tname, new_column));
    WriteWorkload(count, count + kRecordBatch, &producer_cluster_, producer_table->name());
    count += kRecordBatch;
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 VARCHAR", tname, new_column));
    WriteWorkload(count, count + kRecordBatch, &producer_cluster_, producer_table->name());
  }

  //  2. Expectations: Replication should add Data 1x, then block because IDs don't match.
  //                   DROP is non-blocking,
  //                   re-ADD blocks until IDs match even though the  Name & Type match.
  LOG(INFO) << "Consumer count after Producer ALTER halted polling = "
            << EXPECT_RESULT(data_replicated_correctly(count));
  {
    auto tbl = consumer_table->name();
    auto tname = tbl.table_name();
    auto conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN $1", tname, new_column));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 VARCHAR", tname, new_column));
  }
  count += kRecordBatch;
  ASSERT_OK(WaitFor([&]() { return data_replicated_correctly(count); },
            MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

  /***************************/
  /****** REVERSE ORDER ******/
  /***************************/
  auto new_int_col = "age";

  //  1. Run on Producer: DROP NewCol, ADD NewInt
  //                      Add Data.
  {
    auto tbl = producer_table->name();
    auto tname = tbl.table_name();
    auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN $1", tname, new_column));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 INT", tname, new_int_col));
    WriteWorkload(count, count + kRecordBatch, &producer_cluster_, producer_table->name());
  }

  //  2. Run on Consumer: ADD NewInt, DROP NewCol
  {
    auto tbl = consumer_table->name();
    auto tname = tbl.table_name();
    auto conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(tbl.namespace_name()));
    // But subsequently running the same order should pass.
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN $1", tname, new_column));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 INT", tname, new_int_col));
  }

  //  3. Expectations: Replication should not block & add Data.
  count += kRecordBatch;
  ASSERT_OK(WaitFor([&]() { return data_replicated_correctly(count); },
      MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

}

TEST_F(XClusterYsqlTest, ReplicationWithCreateIndexDDL) {
  SetAtomicFlag(true, &FLAGS_xcluster_wait_on_ddl_alter);
  FLAGS_ysql_disable_index_backfill = false;
  string new_column = "alt";
  constexpr auto kIndexName = "TestIndex";

  constexpr auto kRecordBatch = 5;
  auto count = 0;
  constexpr int kNTabletsPerTable = 4;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // Tables contains both producer and consumer universe tables (alternately).
  ASSERT_EQ(tables.size(), 2);
  std::shared_ptr<client::YBTable> producer_table(tables[0]), consumer_table(tables[1]);

  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, {producer_table}));
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(
      consumer_cluster(), consumer_client(), kUniverseId, &get_universe_replication_resp));

  auto producer_conn =
      EXPECT_RESULT(producer_cluster_.ConnectToDB(producer_table->name().namespace_name()));
  auto consumer_conn =
      EXPECT_RESULT(consumer_cluster_.ConnectToDB(consumer_table->name().namespace_name()));

  // Add a second column & populate with data.
  ASSERT_OK(producer_conn.ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN $1 int", producer_table->name().table_name(), new_column));
  ASSERT_OK(consumer_conn.ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN $1 int", consumer_table->name().table_name(), new_column));
  ASSERT_OK(producer_conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series($1,      $1 + $2), "
      "generate_series($1 + 11, $1 + $2 + 11))",
      producer_table->name().table_name(), count, kRecordBatch - 1));
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  count += kRecordBatch;

  // Create an Index on the second column.
  ASSERT_OK(producer_conn.ExecuteFormat(
      "CREATE INDEX $0 ON $1 ($2 ASC)", kIndexName, producer_table->name().table_name(),
      new_column));

  const std::string query =
      Format("SELECT * FROM $0 ORDER BY $1", producer_table->name().table_name(), new_column);
  ASSERT_TRUE(ASSERT_RESULT(producer_conn.HasIndexScan(query)));
  PGResultPtr res = ASSERT_RESULT(producer_conn.Fetch(query));
  ASSERT_EQ(PQntuples(res.get()), count);
  ASSERT_EQ(PQnfields(res.get()), 2);

  // Verify that the Consumer is still getting new traffic after the index is created.
  ASSERT_OK(producer_conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series($1,      $1 + $2), "
      "generate_series($1 + 11, $1 + $2 + 11))",
      producer_table->name().table_name(), count, kRecordBatch - 1));
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  count += kRecordBatch;

  // Drop the Index.
  ASSERT_OK(producer_conn.ExecuteFormat("DROP INDEX $0", kIndexName));

  // The main Table should no longer list having an index.
  ASSERT_FALSE(ASSERT_RESULT(producer_conn.HasIndexScan(query)));
  res = ASSERT_RESULT(producer_conn.Fetch(query));
  ASSERT_EQ(PQntuples(res.get()), count);
  ASSERT_EQ(PQnfields(res.get()), 2);

  // Verify that we're still getting traffic to the Consumer after the index drop.
  ASSERT_OK(producer_conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series($1,      $1 + $2), "
      "generate_series($1 + 11, $1 + $2 + 11))",
      producer_table->name().table_name(), count, kRecordBatch - 1));
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
}

TEST_F(XClusterYsqlTest, SetupUniverseReplicationWithProducerBootstrapId) {
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 3));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));
  auto* producer_leader_mini_master = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster());
  auto producer_master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &producer_client()->proxy_cache(),
      producer_leader_mini_master->bound_rpc_addr());

  std::unique_ptr<client::YBClient> client;
  client = ASSERT_RESULT(consumer_cluster()->CreateClient());

  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
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

  // 1. Write some data so that we can verify that only new records get replicated.
  for (const auto& producer_table : producer_tables) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(0, 100, &producer_cluster_, producer_table->name());
  }

  SleepFor(MonoDelta::FromSeconds(10));

  auto bootstrap_ids =
      ASSERT_RESULT(BootstrapProducer(producer_cluster(), producer_client(), producer_tables));
  ASSERT_EQ(bootstrap_ids.size(), producer_tables.size());

  int table_idx = 0;
  for (const auto& bootstrap_id : bootstrap_ids) {
    LOG(INFO) << "Got bootstrap id " << bootstrap_id
              << " for table " << producer_tables[table_idx++]->name().table_name();
  }

  std::unordered_map<std::string, int> tablet_bootstraps;

  // Verify that for each of the table's tablets, a new row in cdc_state table with the returned
  // id was inserted.
  client::TableHandle table;
  client::YBTableName cdc_state_table(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
  ASSERT_OK(table.Open(cdc_state_table, producer_client()));

  // 2 tables with 8 tablets each.
  ASSERT_EQ(tables_vector.size() * kNTabletsPerTable, boost::size(client::TableRange(table)));
  for (const auto& row : client::TableRange(table)) {
    string stream_id = row.column(0).string_value();
    tablet_bootstraps[stream_id]++;

    string checkpoint = row.column(2).string_value();
    auto s = OpId::FromString(checkpoint);
    ASSERT_OK(s);
    OpId op_id = *s;
    ASSERT_GT(op_id.index, 0);

    LOG(INFO) << "Bootstrap id " << stream_id
              << " for tablet " << row.column(1).string_value();
  }

  ASSERT_EQ(tablet_bootstraps.size(), producer_tables.size());
  // Check that each bootstrap id has 8 tablets.
  for (const auto& e : tablet_bootstraps) {
    ASSERT_EQ(e.second, kNTabletsPerTable);
  }

  // Map table -> bootstrap_id. We will need when setting up replication.
  std::unordered_map<TableId, std::string> table_bootstrap_ids;
  for (size_t i = 0; i < bootstrap_ids.size(); i++) {
    table_bootstrap_ids[producer_tables[i]->id()] = bootstrap_ids[i];
  }

  // 2. Setup replication.
  master::SetupUniverseReplicationRequestPB setup_universe_req;
  master::SetupUniverseReplicationResponsePB setup_universe_resp;
  setup_universe_req.set_producer_id(kUniverseId);
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

  auto* consumer_leader_mini_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
  master::MasterReplicationProxy master_proxy(
      &consumer_client()->proxy_cache(),
      consumer_leader_mini_master->bound_rpc_addr());

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  ASSERT_OK(master_proxy.SetupUniverseReplication(setup_universe_req, &setup_universe_resp, &rpc));
  ASSERT_FALSE(setup_universe_resp.has_error());

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(kUniverseId, &get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<uint32_t>(tables_vector.size() * kNTabletsPerTable)));

  // 4. Write more data.
  for (const auto& producer_table : producer_tables) {
    WriteWorkload(1000, 1005, &producer_cluster_, producer_table->name());
  }

  // 5. Verify that only new writes get replicated to consumer since we bootstrapped the producer
  // after we had already written some data, therefore the old data (whatever was there before we
  // bootstrapped the producer) should not be replicated.
  auto data_replicated_correctly = [&]() -> Result<bool> {
    for (const auto& consumer_table : consumer_tables) {
      LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
      auto consumer_results = ScanToStrings(consumer_table->name(), &consumer_cluster_);

      if (5 != PQntuples(consumer_results.get())) {
        return false;
      }
      int result;
      for (int i = 0; i < 5; ++i) {
        result = VERIFY_RESULT(GetInt32(consumer_results.get(), i, 0));
        if ((1000 + i) != result) {
          return false;
        }
      }
    }
    return true;
  };

  ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(); },
                    MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));
}

TEST_F(XClusterYsqlTest, ColocatedDatabaseReplication) {
  TestColocatedDatabaseReplication();
}

TEST_F(XClusterYsqlTest, LegacyColocatedDatabaseReplication) {
  FLAGS_ysql_legacy_colocated_database_creation = true;
  TestColocatedDatabaseReplication();
}

TEST_F(XClusterYsqlTest, ColocatedDatabaseReplicationWithPacked) {
  FLAGS_ysql_enable_packed_row = true;
  FLAGS_ysql_enable_packed_row_for_colocated_table = true;
  TestColocatedDatabaseReplication();
}

TEST_F(XClusterYsqlTest, ColocatedDatabaseReplicationWithPackedAndCompact) {
  FLAGS_ysql_enable_packed_row = true;
  FLAGS_ysql_enable_packed_row_for_colocated_table = true;
  TestColocatedDatabaseReplication(/* compact = */ true);
}

TEST_F(XClusterYsqlTest, PackedColocatedDatabaseReplicationWithTransactions) {
  FLAGS_ysql_enable_packed_row = true;
  FLAGS_ysql_enable_packed_row_for_colocated_table = true;
  TestColocatedDatabaseReplication(/* compact= */ true, /* use_transaction = */ true);
}

TEST_F(XClusterYsqlTest, LegacyColocatedDatabaseReplicationWithPacked) {
  FLAGS_ysql_enable_packed_row = true;
  FLAGS_ysql_enable_packed_row_for_colocated_table = true;
  FLAGS_ysql_legacy_colocated_database_creation = true;
  TestColocatedDatabaseReplication(/*compact=*/true);
}

TEST_F(XClusterYsqlTest, TestColocatedTablesReplicationWithLargeTableCount) {
    constexpr int kNTabletsPerColocatedTable = 1;
    std::vector<uint32_t> tables_vector;
    for (int i = 0; i < 30; i++) {
        tables_vector.push_back(kNTabletsPerColocatedTable);
    }

    // Create colocated tables on each cluster
    auto colocated_tables =
        ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 3, 1, true /* colocated */));
    const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

    // colocated_tables contains both producer and consumer universe tables (alternately).
    // Pick out just the producer tables from the list.
    std::vector<std::shared_ptr<client::YBTable>> producer_tables;
    std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
    std::vector<std::shared_ptr<client::YBTable>> colocated_producer_tables;
    std::vector<std::shared_ptr<client::YBTable>> colocated_consumer_tables;
    producer_tables.reserve(colocated_tables.size() / 2 + 1);
    consumer_tables.reserve(colocated_tables.size() / 2 + 1);
    for (size_t i = 0; i < colocated_tables.size(); ++i) {
      if (i % 2 == 0) {
        producer_tables.push_back(colocated_tables[i]);
      } else {
        consumer_tables.push_back(colocated_tables[i]);
      }
    }

    auto producer_table = producer_tables[0];
    auto consumer_table = consumer_tables[0];
    WriteWorkload(0, 50, &producer_cluster_, producer_table->name());

    // 2. Setup replication for only the colocated tables.
    // Get the producer colocated parent table id.
    auto colocated_parent_table_id = ASSERT_RESULT(GetColocatedDatabaseParentTableId());

    rpc::RpcController rpc;
    master::SetupUniverseReplicationRequestPB setup_universe_req;
    master::SetupUniverseReplicationResponsePB setup_universe_resp;
    setup_universe_req.set_producer_id(kUniverseId);
    string master_addr = producer_cluster()->GetMasterAddresses();
    auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, setup_universe_req.mutable_producer_master_addresses());
    // Only need to add the colocated parent table id.
    setup_universe_req.mutable_producer_table_ids()->Reserve(1);
    setup_universe_req.add_producer_table_ids(colocated_parent_table_id);
    auto* consumer_leader_mini_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(),
        consumer_leader_mini_master->bound_rpc_addr());

    rpc.Reset();
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    ASSERT_OK(master_proxy->SetupUniverseReplication(setup_universe_req, &setup_universe_resp,
                                                     &rpc));
    ASSERT_FALSE(setup_universe_resp.has_error());

    // 3. Verify everything is setup correctly.
    master::GetUniverseReplicationResponsePB get_universe_replication_resp;
    ASSERT_OK(VerifyUniverseReplication(kUniverseId, &get_universe_replication_resp));
    ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), kNTabletsPerColocatedTable));
    ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));

}


TEST_F(XClusterYsqlTest, ColocatedDatabaseDifferentColocationIds) {
  auto colocated_tables = ASSERT_RESULT(SetUpWithParams({}, {}, 3, 1, true /* colocated */));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // Create two tables with different colocation ids.
  auto conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(kNamespaceName));
  auto table_info = ASSERT_RESULT(CreateYsqlTable(&producer_cluster_,
                                              kNamespaceName,
                                              "" /* schema_name */,
                                              "test_table_0",
                                              boost::none /* tablegroup */,
                                              1 /* num_tablets */,
                                              true /* colocated */,
                                              123456 /* colocation_id */));
  ASSERT_RESULT(CreateYsqlTable(&consumer_cluster_,
                            kNamespaceName,
                            "" /* schema_name */,
                            "test_table_0",
                            boost::none /* tablegroup */,
                            1 /* num_tablets */,
                            true /* colocated */,
                            123457 /* colocation_id */));
  std::shared_ptr<client::YBTable> producer_table;
  ASSERT_OK(producer_client()->OpenTable(table_info, &producer_table));

  // Try to setup replication, should fail on schema validation due to different colocation ids.
  ASSERT_NOK(SetupUniverseReplication(kUniverseId, {producer_table}));
}

TEST_F(XClusterYsqlTest, TablegroupReplication) {
  std::vector<uint32_t> tables_vector = {1, 1};
  boost::optional<std::string> kTablegroupName("mytablegroup");
  auto tables = ASSERT_RESULT(
      SetUpWithParams(tables_vector, tables_vector, 1, 1, false /* colocated */, kTablegroupName));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
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

  // 1. Write some data to all tables.
  for (const auto& producer_table : producer_tables) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(0, 100, &producer_cluster_, producer_table->name());
  }

  // 2. Setup replication for the tablegroup.
  auto tablegroup_parent_table_id = ASSERT_RESULT(GetTablegroupParentTable(&producer_cluster_,
                                                                           kNamespaceName));
  LOG(INFO) << "Tablegroup id to replicate: " << tablegroup_parent_table_id;

  rpc::RpcController rpc;
  master::SetupUniverseReplicationRequestPB setup_universe_req;
  master::SetupUniverseReplicationResponsePB setup_universe_resp;
  setup_universe_req.set_producer_id(kUniverseId);
  string master_addr = producer_cluster()->GetMasterAddresses();
  auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
  HostPortsToPBs(hp_vec, setup_universe_req.mutable_producer_master_addresses());
  setup_universe_req.mutable_producer_table_ids()->Reserve(1);
  setup_universe_req.add_producer_table_ids(tablegroup_parent_table_id);

  auto* consumer_leader_mini_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      consumer_leader_mini_master->bound_rpc_addr());

  rpc.Reset();
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  ASSERT_OK(master_proxy->SetupUniverseReplication(setup_universe_req, &setup_universe_resp, &rpc));
  ASSERT_FALSE(setup_universe_resp.has_error());

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(kUniverseId, &get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 1));

  // 4. Check that tables are being replicated.
  auto data_replicated_correctly = [&](std::vector<std::shared_ptr<client::YBTable>> tables,
                                       int num_results) -> Result<bool> {
    for (const auto& consumer_table : tables) {
      LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
      auto consumer_results = ScanToStrings(consumer_table->name(), &consumer_cluster_);

      if (num_results != PQntuples(consumer_results.get())) {
        return false;
      }
      int result;
      for (int i = 0; i < num_results; ++i) {
        result = VERIFY_RESULT(GetInt32(consumer_results.get(), i, 0));
        if (i != result) {
          return false;
        }
      }
    }
    return true;
  };

  ASSERT_OK(WaitFor([&]() { return data_replicated_correctly(consumer_tables, 100); },
                    MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

  // 5. Write more data.
  for (const auto& producer_table : producer_tables) {
    WriteWorkload(100, 105, &producer_cluster_, producer_table->name());
  }

  ASSERT_OK(WaitFor([&]() { return data_replicated_correctly(consumer_tables, 105); },
                    MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

  ASSERT_TRUE(FLAGS_xcluster_wait_on_ddl_alter);
  // Add a new table to the existing Tablegroup.  This is essentially an ALTER TABLE.
  std::vector<std::shared_ptr<client::YBTable>> producer_new_table;
  {
    std::vector<YBTableName> tables;
    std::shared_ptr<client::YBTable> new_tablegroup_producer_table;
    uint32_t idx = static_cast<uint32_t>(producer_tables.size()) + 1;
    ASSERT_OK(CreateYsqlTable(idx, 1, &producer_cluster_, &tables, kTablegroupName));
    ASSERT_OK(producer_client()->OpenTable(tables[0], &new_tablegroup_producer_table));
    producer_new_table.push_back(new_tablegroup_producer_table);
  }

  for (const auto& producer_table : producer_tables) {
    WriteWorkload(105, 110, &producer_cluster_, producer_table->name());
  }

  // Add the compatible table.  Ensure that writes to the table are now replicated.
  std::vector<std::shared_ptr<client::YBTable>> consumer_new_table;
  {
    std::vector<YBTableName> tables;
    std::shared_ptr<client::YBTable> new_tablegroup_consumer_table;
    uint32_t idx = static_cast<uint32_t>(consumer_tables.size()) + 1;
    ASSERT_OK(CreateYsqlTable(idx, 1, &consumer_cluster_, &tables, kTablegroupName));
    ASSERT_OK(consumer_client()->OpenTable(tables[0], &new_tablegroup_consumer_table));
    consumer_new_table.push_back(new_tablegroup_consumer_table);
  }

  // Replication should work on this new table.
  WriteWorkload(0, 10, &producer_cluster_, producer_new_table.begin()->get()->name());
  ASSERT_OK(WaitFor([&]() { return data_replicated_correctly(consumer_new_table, 10); },
            MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));
}

TEST_F(XClusterYsqlTest, TablegroupReplicationMismatch) {
  ASSERT_OK(Initialize(1 /* replication_factor */));

  boost::optional<std::string> tablegroup_name("mytablegroup");

  ASSERT_OK(CreateDatabase(&producer_cluster_, kNamespaceName, false /* colocated */));
  ASSERT_OK(CreateDatabase(&consumer_cluster_, kNamespaceName, false /* colocated */));
  ASSERT_OK(CreateTablegroup(&producer_cluster_, kNamespaceName, tablegroup_name.get()));
  ASSERT_OK(CreateTablegroup(&consumer_cluster_, kNamespaceName, tablegroup_name.get()));

  // We intentionally set up so that the number of producer and consumer tables don't match.
  // The replication should fail during validation.
  const uint32_t num_producer_tables = 2;
  const uint32_t num_consumer_tables = 3;
  std::vector<YBTableName> tables;
  for (uint32_t i = 0; i < num_producer_tables; i++) {
    ASSERT_OK(CreateYsqlTable(i, 1 /* num_tablets */, &producer_cluster_,
                          &tables, tablegroup_name, false /* colocated */));
  }
  for (uint32_t i = 0; i < num_consumer_tables; i++) {
    ASSERT_OK(CreateYsqlTable(i, 1 /* num_tablets */, &consumer_cluster_,
                          &tables, tablegroup_name, false /* colocated */));
  }

  auto tablegroup_parent_table_id = ASSERT_RESULT(GetTablegroupParentTable(&producer_cluster_,
                                                                           kNamespaceName));

  // Try to set up replication.
  rpc::RpcController rpc;
  master::SetupUniverseReplicationRequestPB setup_universe_req;
  master::SetupUniverseReplicationResponsePB setup_universe_resp;
  setup_universe_req.set_producer_id(kUniverseId);
  string master_addr = producer_cluster()->GetMasterAddresses();
  auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
  HostPortsToPBs(hp_vec, setup_universe_req.mutable_producer_master_addresses());
  setup_universe_req.mutable_producer_table_ids()->Reserve(1);
  setup_universe_req.add_producer_table_ids(tablegroup_parent_table_id);

  auto* consumer_leader_mini_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      consumer_leader_mini_master->bound_rpc_addr());

  rpc.Reset();
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  ASSERT_OK(master_proxy->SetupUniverseReplication(setup_universe_req, &setup_universe_resp, &rpc));
  ASSERT_FALSE(setup_universe_resp.has_error());

  // The schema validation should fail.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_NOK(VerifyUniverseReplication(&get_universe_replication_resp));
}

// Checks that in regular replication set up, bootstrap is not required
TEST_F(XClusterYsqlTest, IsBootstrapRequiredNotFlushed) {
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 1));

  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
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

  std::unique_ptr<client::YBClient> client;
  std::unique_ptr<cdc::CDCServiceProxy> producer_cdc_proxy;
  client = ASSERT_RESULT(consumer_cluster()->CreateClient());
  producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
      &client->proxy_cache(),
      HostPort::FromBoundEndpoint(producer_cluster()->mini_tablet_server(0)->bound_rpc_addr()));

  ASSERT_OK(WaitForLoadBalancersToStabilize());

  std::vector<TabletId> tablet_ids;
  if (producer_tables[0]) {
    ASSERT_OK(producer_cluster_.client_->GetTablets(
        producer_tables[0]->name(), (int32_t)3, &tablet_ids, NULL));
    ASSERT_GT(tablet_ids.size(), 0);
  }

  // 1. Setup replication without any data.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_check_bootstrap_required) = true;
  ASSERT_OK(SetupUniverseReplication(producer_tables));
  master::GetUniverseReplicationResponsePB verify_resp;
  ASSERT_OK(VerifyUniverseReplication(&verify_resp));

  master::ListCDCStreamsResponsePB stream_resp;
  ASSERT_OK(GetCDCStreamForTable(producer_tables[0]->id(), &stream_resp));
  ASSERT_EQ(stream_resp.streams_size(), 1);
  auto& stream_id = stream_resp.streams(0).stream_id();

  // 2. Write some data.
  for (const auto& producer_table : producer_tables) {
    WriteWorkload(0, 100, &producer_cluster_, producer_table->name());
  }

  // Verify data is written on the producer.
  for (const auto& producer_table : producer_tables) {
    auto producer_results = ScanToStrings(producer_table->name(), &producer_cluster_);
    ASSERT_EQ(100, PQntuples(producer_results.get()));
    int result;
    for (int i = 0; i < 100; ++i) {
      result = ASSERT_RESULT(GetInt32(producer_results.get(), i, 0));
      ASSERT_EQ(i, result);
    }
  }

  // 3. IsBootstrapRequired on already replicating streams should return false.
  rpc::RpcController rpc;
  cdc::IsBootstrapRequiredRequestPB req;
  cdc::IsBootstrapRequiredResponsePB resp;
  req.set_stream_id(stream_id);
  req.add_tablet_ids(tablet_ids[0]);

  ASSERT_OK(producer_cdc_proxy->IsBootstrapRequired(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error());
  ASSERT_FALSE(resp.bootstrap_required());

  auto should_bootstrap = ASSERT_RESULT(producer_cluster_.client_->IsBootstrapRequired(
                                          {producer_tables[0]->id()}, stream_id));
  ASSERT_FALSE(should_bootstrap);

  // 4. IsBootstrapRequired without a valid stream should return true.
  should_bootstrap =
      ASSERT_RESULT(producer_cluster_.client_->IsBootstrapRequired({producer_tables[0]->id()}));
  ASSERT_TRUE(should_bootstrap);

  // 4. Setup replication with data should fail.
  ASSERT_NOK(SetupUniverseReplication("replication-group-2", producer_tables));
}

// Checks that with missing logs, replication will require bootstrapping
TEST_F(XClusterYsqlTest, IsBootstrapRequiredFlushed) {
  FLAGS_enable_load_balancing = false;
  FLAGS_log_cache_size_limit_mb = 1;
  FLAGS_log_segment_size_bytes = 5_KB;
  FLAGS_log_min_segments_to_retain = 1;
  FLAGS_log_min_seconds_to_retain = 1;

  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
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

  auto table = producer_tables[0];
  // Write some data.
  WriteWorkload(0, 50, &producer_cluster_, table->name());

  auto tablet_ids = ListTabletIdsForTable(producer_cluster(), table->id());
  ASSERT_EQ(tablet_ids.size(), 1);
  auto tablet_to_flush = *tablet_ids.begin();

  std::unique_ptr<client::YBClient> client;
  std::unique_ptr<cdc::CDCServiceProxy> producer_cdc_proxy;
  client = ASSERT_RESULT(consumer_cluster()->CreateClient());
  producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
      &client->proxy_cache(),
      HostPort::FromBoundEndpoint(producer_cluster()->mini_tablet_server(0)->bound_rpc_addr()));
  ASSERT_OK(WaitFor([this, tablet_to_flush]() -> Result<bool> {
    LOG(INFO) << "Cleaning tablet logs";
    RETURN_NOT_OK(producer_cluster()->CleanTabletLogs());
    auto leaders = ListTabletPeers(producer_cluster(), ListPeersFilter::kLeaders);
    if (leaders.empty()) {
      return false;
    }
    // locate the leader with the expected logs
    tablet::TabletPeerPtr tablet_peer = leaders.front();
    for (auto leader : leaders) {
      LOG(INFO) << leader->tablet_id() << " @OpId " << leader->GetLatestLogEntryOpId().index;
      if (leader->tablet_id() == tablet_to_flush) {
        tablet_peer = leader;
        break;
      }
    }
    SCHECK(tablet_peer, InternalError, "Missing tablet peer with the WriteWorkload");

    RETURN_NOT_OK(producer_cluster()->FlushTablets());
    RETURN_NOT_OK(producer_cluster()->CleanTabletLogs());

    // Check that first log was garbage collected, so remote bootstrap will be required.
    consensus::ReplicateMsgs replicates;
    int64_t starting_op;
    return !tablet_peer->log()->GetLogReader()->ReadReplicatesInRange(
        1, 2, 0, &replicates, &starting_op).ok();
  }, MonoDelta::FromSeconds(30), "Logs cleaned"));

  auto leaders = ListTabletPeers(producer_cluster(), ListPeersFilter::kLeaders);
  // locate the leader with the expected logs
  tablet::TabletPeerPtr tablet_peer = leaders.front();
  for (auto leader : leaders) {
    if (leader->tablet_id() == tablet_to_flush) {
      tablet_peer = leader;
      break;
    }
  }

  // IsBootstrapRequired for this specific tablet should fail.
  rpc::RpcController rpc;
  cdc::IsBootstrapRequiredRequestPB req;
  cdc::IsBootstrapRequiredResponsePB resp;
  req.add_tablet_ids(tablet_peer->log()->tablet_id());

  ASSERT_OK(producer_cdc_proxy->IsBootstrapRequired(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error());
  ASSERT_TRUE(resp.bootstrap_required());

  // The high level API should also fail.
  auto should_bootstrap = ASSERT_RESULT(producer_client()->IsBootstrapRequired({table->id()}));
  ASSERT_TRUE(should_bootstrap);

  // Setup replication should fail if this check is enabled.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_check_bootstrap_required) = true;
  auto s = SetupUniverseReplication(kUniverseId, producer_tables);
  ASSERT_NOK(s);
  ASSERT_TRUE(s.IsIllegalState());
}

// TODO adapt rest of xcluster-test.cc tests.

TEST_F(XClusterYsqlTest, DeleteTableChecks) {
  constexpr int kNT = 1; // Tablets per table.
  std::vector<uint32_t> tables_vector = {kNT, kNT, kNT}; // Each entry is a table. (Currently 3)
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
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
    WriteWorkload(0, 100, &producer_cluster_, producer_table->name());
  }

  // Verify data is written on the producer.
  for (const auto& producer_table : producer_tables) {
    auto producer_results = ScanToStrings(producer_table->name(), &producer_cluster_);
    ASSERT_EQ(100, PQntuples(producer_results.get()));
    int result;
    for (int i = 0; i < 100; ++i) {
      result = ASSERT_RESULT(GetInt32(producer_results.get(), i, 0));
      ASSERT_EQ(i, result);
    }
  }

  // Set aside one table for AlterUniverseReplication.
  std::shared_ptr<client::YBTable> producer_alter_table, consumer_alter_table;
  producer_alter_table = producer_tables.back();
  producer_tables.pop_back();
  consumer_alter_table = consumer_tables.back();
  consumer_tables.pop_back();

  // 2a. Setup replication.
  ASSERT_OK(SetupUniverseReplication(kUniverseId, producer_tables));

  // Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(kUniverseId, &get_universe_replication_resp));
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
    alter_req.set_producer_id(kUniverseId);
    alter_req.add_producer_table_ids_to_add(producer_alter_table->id());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));
    ASSERT_FALSE(alter_resp.has_error());
    // Wait until we have the new table listed in the existing universe config.
    ASSERT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          master::GetUniverseReplicationResponsePB tmp_resp;
          RETURN_NOT_OK(VerifyUniverseReplication(kUniverseId, &tmp_resp));
          return tmp_resp.entry().tables_size() == static_cast<int64>(producer_tables.size() + 1);
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Verify table created with alter."));

    ASSERT_OK(CorrectlyPollingAllTablets(
        consumer_cluster(), narrow_cast<uint32_t>((producer_tables.size() + 1) * kNT)));
  }
  producer_tables.push_back(producer_alter_table);
  consumer_tables.push_back(consumer_alter_table);

  auto data_replicated_correctly = [&](int num_results) -> Result<bool> {
    for (const auto& consumer_table : consumer_tables) {
      LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
      auto consumer_results = ScanToStrings(consumer_table->name(), &consumer_cluster_);

      if (num_results != PQntuples(consumer_results.get())) {
        return false;
      }
      int result;
      for (int i = 0; i < num_results; ++i) {
        result = VERIFY_RESULT(GetInt32(consumer_results.get(), i, 0));
        if (i != result) {
          return false;
        }
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
    // GH issue #12003, allow deletion of YSQL tables under replication for now.
    ASSERT_OK(producer_client()->DeleteTable(producer_table_id));
    ASSERT_OK(consumer_client()->DeleteTable(consumer_table_id));
  }

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));

  // TODO(jhe) re-enable these checks after we disallow deletion of YSQL xCluster tables, part
  // of gh issue #753.

  // for (size_t i = 0; i < producer_tables.size(); ++i) {
  //   string producer_table_id = producer_tables[i]->id();
  //   string consumer_table_id = consumer_tables[i]->id();
  //   ASSERT_OK(producer_client()->DeleteTable(producer_table_id));
  //   ASSERT_OK(consumer_client()->DeleteTable(consumer_table_id));
  // }
}

TEST_F(XClusterYsqlTest, TruncateTableChecks) {
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
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
    WriteWorkload(0, 100, &producer_cluster_, producer_table->name());
  }

  // Verify data is written on the producer.
  for (const auto& producer_table : producer_tables) {
    auto producer_results = ScanToStrings(producer_table->name(), &producer_cluster_);
    ASSERT_EQ(100, PQntuples(producer_results.get()));
    int result;
    for (int i = 0; i < 100; ++i) {
      result = ASSERT_RESULT(GetInt32(producer_results.get(), i, 0));
      ASSERT_EQ(i, result);
    }
  }

  // 2. Setup replication.
  ASSERT_OK(SetupUniverseReplication(kUniverseId, producer_tables));

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(kUniverseId, &get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<uint32_t>(tables_vector.size() * kNTabletsPerTable)));

  auto data_replicated_correctly = [&](int num_results) -> Result<bool> {
    for (const auto& consumer_table : consumer_tables) {
      LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
      auto consumer_results = ScanToStrings(consumer_table->name(), &consumer_cluster_);

      if (num_results != PQntuples(consumer_results.get())) {
        return false;
      }
      int result;
      for (int i = 0; i < num_results; ++i) {
        result = VERIFY_RESULT(GetInt32(consumer_results.get(), i, 0));
        if (i != result) {
          return false;
        }
      }
    }
    return true;
  };
  ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(100); },
                    MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

  // Attempt to Truncate the producer and consumer tables.
  string producer_table_id = producer_tables[0]->id();
  string consumer_table_id = consumer_tables[0]->id();
  ASSERT_NOK(TruncateTable(&producer_cluster_, {producer_table_id}));
  ASSERT_NOK(TruncateTable(&consumer_cluster_, {consumer_table_id}));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_delete_truncate_xcluster_replicated_table) = true;
  ASSERT_OK(TruncateTable(&producer_cluster_, {producer_table_id}));
  ASSERT_OK(TruncateTable(&consumer_cluster_, {consumer_table_id}));
}

TEST_F(XClusterYsqlTest, SetupReplicationWithMaterializedViews) {
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  producer_tables.reserve(tables.size() / 2);
  std::shared_ptr<client::YBTable> producer_mv;
  std::shared_ptr<client::YBTable> consumer_mv;
  for (size_t i = 0; i < 2; ++i) {
    if (i % 2 == 0) {
      WriteWorkload(0, 5, &producer_cluster_, tables[i]->name());
      ASSERT_OK(producer_client()->OpenTable(
          ASSERT_RESULT(CreateMaterializedView(&producer_cluster_, tables[i]->name())),
          &producer_mv));
      producer_tables.push_back(producer_mv);
    } else {
      ASSERT_OK(consumer_client()->OpenTable(
          ASSERT_RESULT(CreateMaterializedView(&consumer_cluster_, tables[i]->name())),
          &consumer_mv));
    }
  }

  auto s = SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables);

  ASSERT_NOK(s);
  ASSERT_TRUE(s.IsNotSupported());
  ASSERT_STR_CONTAINS(s.ToString(), "Replication is not supported for materialized view");
}

void XClusterYsqlTest::TestReplicationWithPackedColumns(bool colocated, bool bootstrap) {
  FLAGS_ysql_enable_packed_row = true;
  FLAGS_ysql_enable_packed_row_for_colocated_table = true;
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 1, 1, colocated));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));
  auto producer_table = tables[0];
  auto consumer_table = tables[1];
  auto tbl = consumer_table->name();

  BumpUpSchemaVersionsWithAlters({consumer_table});

  {
    // Perform a DDL so that there are already some change metadata ops in the WAL to process
    auto p_conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(p_conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN pre_setup TEXT", tbl.table_name()));

    auto c_conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(c_conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN pre_setup TEXT", tbl.table_name()));
  }

  auto producer_table_id = producer_table->id();
  if (colocated) {
    producer_table_id = ASSERT_RESULT(GetColocatedDatabaseParentTableId());
  }

  // If bootstrap is requested, run bootstrap
  std::vector<CDCStreamId> bootstrap_ids = {};
  if (bootstrap) {
    bootstrap_ids = ASSERT_RESULT(
        BootstrapProducer(producer_cluster(), producer_client(), {producer_table_id}));
  }

  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, {producer_table_id},
      bootstrap_ids, {LeaderOnly::kTrue, Transactional::kTrue}));

  ASSERT_NO_FATALS(WriteGenerateSeries(0, 50, &producer_cluster_, producer_table->name()));

  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, &resp));
  ASSERT_EQ(resp.entry().producer_id(), kUniverseId);
  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), producer_table_id);
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));

  // Alter the table on the Consumer side by adding a column
  {
    string new_col = "new_col";
    auto conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 TEXT", tbl.table_name(), new_col));
  }

  // Verify single value inserts are replicated correctly and can be read
  ASSERT_NO_FATALS(WriteWorkload(51, 52, &producer_cluster_, producer_table->name()));
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));

  // 5. Verify batch inserts are replicated correctly and can be read
  ASSERT_NO_FATALS(WriteGenerateSeries(52, 100, &producer_cluster_, producer_table->name()));
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));

  // Verify transactional inserts are replicated correctly and can be read
  WriteTransactionalWorkload(101, 150, &producer_cluster_, producer_table->name());
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));

  // Alter the table on the producer side by adding the same column and insert some rows
  // and verify
  {
    string new_col = "new_col";
    auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 TEXT", tbl.table_name(), new_col));
  }

  // Verify single value inserts are replicated correctly and can be read
  ASSERT_NO_FATALS(WriteWorkload(151, 152, &producer_cluster_, producer_table->name()));
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));

  // Verify batch inserts are replicated correctly and can be read
  ASSERT_NO_FATALS(WriteGenerateSeries(152, 200, &producer_cluster_, producer_table->name()));
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));

  // Verify transactional inserts are replicated correctly and can be read
  WriteTransactionalWorkload(201, 250, &producer_cluster_, producer_table->name());
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));

  {
    // Verify I/U/D
    auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES(251,'Hello', 'World')", tbl.table_name()));
    ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));

    ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET new_col = 'Hi' WHERE key = 251", tbl.table_name()));
    ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));

    ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE key = 251", tbl.table_name()));
    ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  }

  // Bump up schema version on Producer and verify.
  {
    auto alter_owner_str = Format("ALTER TABLE $0 OWNER TO yugabyte;", tbl.table_name());
    auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat(alter_owner_str));
    ASSERT_NO_FATALS(WriteWorkload(251, 300, &producer_cluster_, producer_table->name()));
    ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
  }

  // Alter table on consumer side to generate new schema version.
  {
    string new_col = "new_col_2";
    auto conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 TEXT", tbl.table_name(), new_col));
  }

  ASSERT_OK(consumer_cluster()->FlushTablets());

  ASSERT_NO_FATALS(WriteWorkload(301, 350, &producer_cluster_, producer_table->name()));
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
}

TEST_F(XClusterYsqlTest, ReplicationWithPackedColumnsAndSchemaVersionMismatch) {
  TestReplicationWithPackedColumns(false /* colocated */, false /* boostrap */);
}

TEST_F(XClusterYsqlTest, ReplicationWithPackedColumnsAndSchemaVersionMismatchColocated) {
  TestReplicationWithPackedColumns(true /* colocated */, false /* boostrap */);
}

TEST_F(XClusterYsqlTest, ReplicationWithPackedColumnsAndBootstrap) {
  TestReplicationWithPackedColumns(false /* colocated */, true /* boostrap */);
}

TEST_F(XClusterYsqlTest, ReplicationWithPackedColumnsAndBootstrapColocated) {
  TestReplicationWithPackedColumns(true /* colocated */, true /* boostrap */);
}

TEST_F(XClusterYsqlTest, ReplicationWithDefaultProducerSchemaVersion) {
  FLAGS_ysql_enable_packed_row = true;
  FLAGS_ysql_enable_packed_row_for_colocated_table = true;

  const auto namespace_name = "demo";
  const auto table_name = "test_table";
  ASSERT_OK(Initialize(3 /* replication_factor */, 1 /* num_masters */));

  ASSERT_OK(RunOnBothClusters(
      [&](Cluster* cluster) { return CreateDatabase(cluster, namespace_name); }));

  // Create producer/consumer clusters with different schemas and then
  // modify schema on consumer to match producer schema.
  auto p_conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(p_conn.ExecuteFormat("create table $0(key int)", table_name));

  auto c_conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(c_conn.ExecuteFormat("create table $0(key int, name text)", table_name));
  ASSERT_OK(c_conn.ExecuteFormat("alter table $0 drop column name", table_name));

  // Producer schema version will be 0 and consumer schema version will be 2.
  auto producer_table_name_with_id_list = ASSERT_RESULT(producer_client()->ListTables(table_name));
  ASSERT_EQ(producer_table_name_with_id_list.size(), 1);
  auto producer_table_name_with_id = producer_table_name_with_id_list[0];
  ASSERT_TRUE(producer_table_name_with_id.has_table_id());
  auto producer_table = ASSERT_RESULT(producer_client()->OpenTable(
      producer_table_name_with_id.table_id()));

  auto consumer_table_name_with_id_list = ASSERT_RESULT(consumer_client()->ListTables(table_name));
  ASSERT_EQ(consumer_table_name_with_id_list.size(), 1);
  auto consumer_table_name_with_id = consumer_table_name_with_id_list[0];
  ASSERT_TRUE(consumer_table_name_with_id.has_table_id());
  auto consumer_table = ASSERT_RESULT(consumer_client()->OpenTable(
      consumer_table_name_with_id.table_id()));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));
  ASSERT_OK(SetupUniverseReplication(kUniverseId, {producer_table}));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(kUniverseId, &resp));
  ASSERT_EQ(resp.entry().producer_id(), kUniverseId);
  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), producer_table->id());

  // Verify that schema version is fixed up correctly and target can be read.
  ASSERT_NO_FATALS(WriteGenerateSeries(0, 50, &producer_cluster_, producer_table->name()));
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
}

TEST_F(XClusterYsqlTest, ValidateSchemaPackingGCDuringNetworkPartition) {
  // During network partition, the following can happen:
  // 1. Source and Target are in-sync.
  // 2. Source performs a schema modification generating a new schema version : X.
  // 3. A compatible schema change is made on the target resulting in xcluster schema mapping - X:Y
  // 4. No data has been written to source with schema 'X' or due to a network partition rows
  //    written with schema version 'X' never made it to the target.
  // 5. If schema packing GC runs at this time on target, it will Garbage collect 'Y' as there is
  //    no data written to target Y. However this is not correct, rows will make it to the target
  //    once replication resumes or rows are written on the target
  // The test validates that GC of schema packings on target does not GC any schema versions that
  // XCluster target is aware of (which happens as a result of the ChangeMetadataOp).
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;

  const auto namespace_name = "demo";
  const auto table_name = "test_table";
  ASSERT_OK(Initialize(3 /* replication_factor */, 1 /* num_masters */));

  ASSERT_OK(
      RunOnBothClusters([&](Cluster* cluster) { return CreateDatabase(cluster, namespace_name); }));

  // Create producer/consumer clusters with different schemas and then
  // modify schema on consumer to match producer schema.
  {
    auto p_conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
    ASSERT_OK(p_conn.ExecuteFormat("create table $0(key int)", table_name));

    auto c_conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
    ASSERT_OK(c_conn.ExecuteFormat("create table $0(key int)", table_name));
  }

  // Producer schema version will be 0 and consumer schema version will be 0.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables_, consumer_tables_;
  auto producer_table_name_with_id_list = ASSERT_RESULT(producer_client()->ListTables(table_name));
  ASSERT_EQ(producer_table_name_with_id_list.size(), 1);
  auto producer_table_name_with_id = producer_table_name_with_id_list[0];
  ASSERT_TRUE(producer_table_name_with_id.has_table_id());
  auto producer_table_ =
      ASSERT_RESULT(producer_client()->OpenTable(producer_table_name_with_id.table_id()));
  producer_tables_.push_back(producer_table_);

  auto consumer_table_name_with_id_list = ASSERT_RESULT(consumer_client()->ListTables(table_name));
  ASSERT_EQ(consumer_table_name_with_id_list.size(), 1);
  auto consumer_table_name_with_id = consumer_table_name_with_id_list[0];
  ASSERT_TRUE(consumer_table_name_with_id.has_table_id());
  auto consumer_table_ =
      ASSERT_RESULT(consumer_client()->OpenTable(consumer_table_name_with_id.table_id()));
  consumer_tables_.push_back(consumer_table_);

  // Bump up the schema versions.
  // Producer schema version will be 4 and consumer schema version will be 2.
  BumpUpSchemaVersionsWithAlters({producer_table_});

  ASSERT_OK(SetupUniverseReplication(producer_tables_));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().producer_id(), kUniverseId);
  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), producer_table_->id());

  // Modify the schema on source and target so that they are in sync, but don't insert rows.
  // Producer schema version will be 5. Consumer schema version will be 4.
  // Test is verifying that we can read rows written Schema version 3 as well
  // and they do not get GC'ed.
  {
    auto p_conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
    ASSERT_OK(p_conn.ExecuteFormat("alter table $0 add column n1 text", table_name));

    auto c_conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
    ASSERT_OK(c_conn.ExecuteFormat("alter table $0 add column n1 text", table_name));
    ASSERT_OK(c_conn.ExecuteFormat("alter table $0 add column n2 text", table_name));
  }

  // Force a schema packings GC on the target by performing a compaction.
  ASSERT_OK(consumer_cluster()->FlushTablets());
  ASSERT_OK(consumer_cluster()->CompactTablets());

  // Verify data gets replicated correctly.
  {
    auto p_conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
    for(int i = 51; i < 60; ++i) {
      ASSERT_OK(p_conn.ExecuteFormat("INSERT INTO $0(key, n1) VALUES (51,'foo')", table_name));
    }
  }

  ASSERT_OK(VerifyWrittenRecords(producer_table_->name(), consumer_table_->name()));
}

void PrepareChangeRequest(
    cdc::GetChangesRequestPB* change_req, const CDCStreamId& stream_id,
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets) {
  change_req->set_stream_id(stream_id);
  change_req->set_tablet_id(tablets.Get(0).tablet_id());
  change_req->mutable_from_cdc_sdk_checkpoint()->set_term(0);
  change_req->mutable_from_cdc_sdk_checkpoint()->set_index(0);
  change_req->mutable_from_cdc_sdk_checkpoint()->set_key("");
  change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(0);
}

void PrepareChangeRequest(
    cdc::GetChangesRequestPB* change_req, const CDCStreamId& stream_id,
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
    const cdc::CDCSDKCheckpointPB& cp) {
  change_req->set_stream_id(stream_id);
  change_req->set_tablet_id(tablets.Get(0).tablet_id());
  change_req->mutable_from_cdc_sdk_checkpoint()->set_term(cp.term());
  change_req->mutable_from_cdc_sdk_checkpoint()->set_index(cp.index());
  change_req->mutable_from_cdc_sdk_checkpoint()->set_key(cp.key());
  change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(cp.write_id());
}

Result<cdc::GetChangesResponsePB> GetChangesFromCDC(
    const std::unique_ptr<cdc::CDCServiceProxy>& cdc_proxy, const CDCStreamId& stream_id,
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
    const cdc::CDCSDKCheckpointPB* cp = nullptr) {
  cdc::GetChangesRequestPB change_req;
  cdc::GetChangesResponsePB change_resp;

  if (!cp) {
    PrepareChangeRequest(&change_req, stream_id, tablets);
  } else {
    PrepareChangeRequest(&change_req, stream_id, tablets, *cp);
  }

  rpc::RpcController get_changes_rpc;
  RETURN_NOT_OK(cdc_proxy->GetChanges(change_req, &change_resp, &get_changes_rpc));

  if (change_resp.has_error()) {
    return StatusFromPB(change_resp.error().status());
  }

  return change_resp;
}

// Initialize a CreateCDCStreamRequest to be used while creating a DB stream ID.
void InitCreateStreamRequest(
    cdc::CreateCDCStreamRequestPB* create_req,
    const cdc::CDCCheckpointType& checkpoint_type,
    const std::string& namespace_name) {
  create_req->set_namespace_name(namespace_name);
  create_req->set_checkpoint_type(checkpoint_type);
  create_req->set_record_type(cdc::CDCRecordType::CHANGE);
  create_req->set_record_format(cdc::CDCRecordFormat::PROTO);
  create_req->set_source_type(cdc::CDCSDK);
}

// This creates a DB stream on the database kNamespaceName by default.
Result<CDCStreamId> CreateDBStream(const std::unique_ptr<cdc::CDCServiceProxy>& cdc_proxy,
                                   cdc::CDCCheckpointType checkpoint_type) {
  cdc::CreateCDCStreamRequestPB req;
  cdc::CreateCDCStreamResponsePB resp;

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

  InitCreateStreamRequest(&req, checkpoint_type, kNamespaceName);
  RETURN_NOT_OK(cdc_proxy->CreateCDCStream(req, &resp, &rpc));
  return resp.db_stream_id();
}

void PrepareSetCheckpointRequest(
    cdc::SetCDCCheckpointRequestPB* set_checkpoint_req, const CDCStreamId stream_id,
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets) {
  set_checkpoint_req->set_stream_id(stream_id);
  set_checkpoint_req->set_initial_checkpoint(true);
  set_checkpoint_req->set_tablet_id(tablets.Get(0).tablet_id());
  set_checkpoint_req->mutable_checkpoint()->mutable_op_id()->set_term(0);
  set_checkpoint_req->mutable_checkpoint()->mutable_op_id()->set_index(0);
}

Status SetInitialCheckpoint(const std::unique_ptr<cdc::CDCServiceProxy>& cdc_proxy,
    YBClient* client,
    const CDCStreamId& stream_id,
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets) {
  rpc::RpcController set_checkpoint_rpc;
  cdc::SetCDCCheckpointRequestPB set_checkpoint_req;
  cdc::SetCDCCheckpointResponsePB set_checkpoint_resp;
  auto deadline = CoarseMonoClock::now() + client->default_rpc_timeout();
  set_checkpoint_rpc.set_deadline(deadline);
  PrepareSetCheckpointRequest(&set_checkpoint_req, stream_id, tablets);

  return cdc_proxy->SetCDCCheckpoint(set_checkpoint_req,
                                      &set_checkpoint_resp,
                                      &set_checkpoint_rpc);
}

void XClusterYsqlTest::ValidateRecordsXClusterWithCDCSDK(bool update_min_cdc_indices_interval,
                                                   bool enable_cdc_sdk_in_producer,
                                                   bool do_explict_transaction) {
  constexpr int kNTabletsPerTable = 1;
  // Change the default value from 60 secs to 1 secs.
  if (update_min_cdc_indices_interval) {
    // Intent should not be cleaned up, even if updatepeers thread keeps updating the
    // minimum checkpoint op_id to all the tablet peers every second, because the
    // intents are still not consumed by the clients.
    FLAGS_update_min_cdc_indices_interval_secs = 1;
  }
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // Tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
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

  // 2. Setup replication.
  ASSERT_OK(SetupUniverseReplication(kUniverseId, producer_tables));

  // 3. Create cdc proxy according the flag.
  rpc::ProxyCache* proxy_cache;
  Endpoint endpoint;
  if (enable_cdc_sdk_in_producer) {
    proxy_cache = &producer_client()->proxy_cache();
    endpoint = producer_cluster()->mini_tablet_servers().front()->bound_rpc_addr();
  } else {
    proxy_cache = &consumer_client()->proxy_cache();
    endpoint = consumer_cluster()->mini_tablet_servers().front()->bound_rpc_addr();
  }
  std::unique_ptr<cdc::CDCServiceProxy> sdk_proxy =
      std::make_unique<cdc::CDCServiceProxy>(proxy_cache, HostPort::FromBoundEndpoint(endpoint));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  std::shared_ptr<client::YBTable> cdc_enabled_table;
  YBClient* client;
  if (enable_cdc_sdk_in_producer) {
    cdc_enabled_table = producer_tables[0];
    client = producer_client();
  } else {
    cdc_enabled_table = consumer_tables[0];
    client = consumer_client();
  }
  ASSERT_OK(client->GetTablets(cdc_enabled_table->name(),
                               0,
                               &tablets, /* partition_list_version =*/
                               nullptr));
  ASSERT_EQ(tablets.size(), 1);
  CDCStreamId db_stream_id = ASSERT_RESULT(CreateDBStream(sdk_proxy,
                                                          cdc::CDCCheckpointType::IMPLICIT));
  ASSERT_OK(SetInitialCheckpoint(sdk_proxy, client, db_stream_id, tablets));

  // 1. Write some data.
  const auto& producer_table = producer_tables[0];
  LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
  if (do_explict_transaction) {
    WriteTransactionalWorkload(0, 10, &producer_cluster_, producer_table->name());
  } else {
    WriteWorkload(0, 10, &producer_cluster_, producer_table->name());
  }

  // Verify data is written on the producer.
  int batch_insert_count = 10;
  auto producer_results = ScanToStrings(producer_table->name(), &producer_cluster_);
  ASSERT_EQ(batch_insert_count, PQntuples(producer_results.get()));
  int result;

  for (int i = 0; i < batch_insert_count; ++i) {
    result = ASSERT_RESULT(GetInt32(producer_results.get(), i, 0));
    ASSERT_EQ(i, result);
  }

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(kUniverseId, &get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<uint32_t>(tables_vector.size() * kNTabletsPerTable)));

  const auto &consumer_table = consumer_tables[0];
  auto data_replicated_correctly = [&](int num_results) -> Result<bool> {
    LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
    auto consumer_results = ScanToStrings(consumer_table->name(), &consumer_cluster_);

    if (num_results != PQntuples(consumer_results.get())) {
      return false;
    }
    int result;
    for (int i = 0; i < num_results; ++i) {
      result = VERIFY_RESULT(GetInt32(consumer_results.get(), i, 0));
      if (i != result) {
        return false;
      }
    }
    return true;
  };

  ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(10); },
                    MonoDelta::FromSeconds(30), "IsDataReplicatedCorrectly"));

  // Call GetChanges for CDCSDK
  cdc::GetChangesRequestPB change_req;
  cdc::GetChangesResponsePB change_resp;

  // Get first change request.
  rpc::RpcController change_rpc;
  PrepareChangeRequest(&change_req, db_stream_id, tablets);
  change_resp = ASSERT_RESULT(GetChangesFromCDC(sdk_proxy, db_stream_id, tablets));
  uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
  uint32_t ins_count = 0;
  uint32_t expected_record = 0;
  uint32_t expected_record_count = 10;

  LOG(INFO) << "Record received after the first call to GetChanges: " << record_size;
  for (uint32_t i = 0; i < record_size; ++i) {
    const cdc::CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
    if (record.row_message().op() == cdc::RowMessage::INSERT) {
      ASSERT_EQ(record.row_message().new_tuple(0).datum_int32(), expected_record++);
      ins_count++;
    }

  }
  LOG(INFO) << "Got " << ins_count << " insert records";
  ASSERT_EQ(expected_record_count, ins_count);

  // Do more insert into producer.
  LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
  if (do_explict_transaction) {
    WriteTransactionalWorkload(10, 20, &producer_cluster_, producer_table->name());
  } else {
    WriteWorkload(10, 20, &producer_cluster_, producer_table->name());
  }
  // Verify data is written on the producer, which should previous plus
  // current new insert.
  producer_results = ScanToStrings(producer_table->name(), &producer_cluster_);
  ASSERT_EQ(batch_insert_count * 2, PQntuples(producer_results.get()));
  for (int i = 10; i < 20; ++i) {
    result = ASSERT_RESULT(GetInt32(producer_results.get(), i, 0));
    ASSERT_EQ(i, result);
  }
  // 5. Make sure this data is also replicated now.
  ASSERT_OK(WaitFor([&]() { return data_replicated_correctly(20); },
                    MonoDelta::FromSeconds(30), "IsDataReplicatedCorrectly"));
  cdc::GetChangesRequestPB change_req2;
  cdc::GetChangesResponsePB change_resp2;

  // Checkpoint from previous GetChanges call.
  cdc::CDCSDKCheckpointPB cp = change_resp.cdc_sdk_checkpoint();
  PrepareChangeRequest(&change_req2, db_stream_id, tablets, cp);
  change_resp = ASSERT_RESULT(GetChangesFromCDC(sdk_proxy, db_stream_id, tablets, &cp));

  record_size = change_resp.cdc_sdk_proto_records_size();
  ins_count = 0;
  expected_record = 10;
  for (uint32_t i = 0; i < record_size; ++i) {
    const cdc::CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
    if (record.row_message().op() == cdc::RowMessage::INSERT) {
      ASSERT_EQ(record.row_message().new_tuple(0).datum_int32(), expected_record++);
      ins_count++;
    }
  }
  LOG(INFO) << "Got " << ins_count << " insert records";
  ASSERT_EQ(expected_record_count, ins_count);
}

TEST_F(XClusterYsqlTest, XClusterWithCDCSDKEnabled) {
  FLAGS_ysql_enable_packed_row = false;
  ValidateRecordsXClusterWithCDCSDK(false, false, false);
}

TEST_F(XClusterYsqlTest, XClusterWithCDCSDKPackedRowsEnabled) {
  FLAGS_ysql_enable_packed_row = true;
  FLAGS_ysql_packed_row_size_limit = 1_KB;
  ValidateRecordsXClusterWithCDCSDK(false, false, false);
}

TEST_F(XClusterYsqlTest, XClusterWithCDCSDKExplictTransaction) {
  FLAGS_ysql_enable_packed_row = false;
  ValidateRecordsXClusterWithCDCSDK(false, true, true);
}

TEST_F(XClusterYsqlTest, XClusterWithCDCSDKExplictTranPackedRows) {
  FLAGS_ysql_enable_packed_row = true;
  FLAGS_ysql_packed_row_size_limit = 1_KB;
  ValidateRecordsXClusterWithCDCSDK(false, true, true);
}

TEST_F(XClusterYsqlTest, XClusterWithCDCSDKUpdateCDCInterval) {
  ValidateRecordsXClusterWithCDCSDK(true, true, false);
}

TEST_F(XClusterYsqlTest, DeletingDatabaseContainingReplicatedTable) {
  constexpr int kNTabletsPerTable = 1;
  const int num_tables = 3;

  auto tables = ASSERT_RESULT(SetUpWithParams({1, 1}, {1, 1}, 1));
  // Additional namespaces.
  const string kNamespaceName2 = "test_namespace2";

  // Create the additional databases.
  auto producer_db_2 = CreateDatabase(&producer_cluster_, kNamespaceName2, false);
  auto consumer_db_2 = CreateDatabase(&consumer_cluster_, kNamespaceName2, false);

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<YBTableName> producer_table_names;
  std::vector<YBTableName> consumer_table_names;

  auto create_tables = [this, &producer_tables](
                           const string namespace_name, Cluster& cluster,
                           bool is_replicated_producer, std::vector<YBTableName>& table_names) {
    for (int i = 0; i < num_tables; i++) {
      auto table = ASSERT_RESULT(CreateYsqlTable(
          &cluster, namespace_name, Format("test_schema_$0", i), Format("test_table_$0", i),
          boost::none /* tablegroup */, kNTabletsPerTable));
      // For now, only replicate the second table for test_namespace.
      if (is_replicated_producer && i == 1) {
        std::shared_ptr<client::YBTable> yb_table;
        ASSERT_OK(producer_client()->OpenTable(table, &yb_table));
        producer_tables.push_back(yb_table);
      }
    }
  };
  // Create the tables in the producer test_namespace database that will contain some replicated
  // tables.
  create_tables(kNamespaceName, producer_cluster_, true, producer_table_names);
  // Create non replicated tables in the producer's test_namespace2 database. This is done to
  // ensure that its deletion isn't affected by other producer databases that are a part of
  // replication.
  create_tables(kNamespaceName2, producer_cluster_, false, producer_table_names);
  // Create non replicated tables in the consumer's test_namespace3 database. This is done to
  // ensure that its deletion isn't affected by other consumer databases that are a part of
  // replication.
  create_tables(kNamespaceName2, consumer_cluster_, false, consumer_table_names);
  // Create tables in the consumer's test_namesapce database, only have the second table be
  // replicated.
  create_tables(kNamespaceName, consumer_cluster_, false, consumer_table_names);

  // Setup universe replication for the tables.
  ASSERT_OK(SetupUniverseReplication(producer_tables));
  master::IsSetupUniverseReplicationDoneResponsePB is_resp;
  ASSERT_OK(WaitForSetupUniverseReplication(
      consumer_cluster(), consumer_client(), kUniverseId, &is_resp));

  ASSERT_NOK(producer_client()->DeleteNamespace(kNamespaceName, YQL_DATABASE_PGSQL));
  ASSERT_NOK(consumer_client()->DeleteNamespace(kNamespaceName, YQL_DATABASE_PGSQL));
  ASSERT_OK(producer_client()->DeleteNamespace(kNamespaceName2, YQL_DATABASE_PGSQL));
  ASSERT_OK(consumer_client()->DeleteNamespace(kNamespaceName2, YQL_DATABASE_PGSQL));

  ASSERT_OK(DeleteUniverseReplication());

  ASSERT_OK(producer_client()->DeleteNamespace(kNamespaceName, YQL_DATABASE_PGSQL));
  ASSERT_OK(consumer_client()->DeleteNamespace(kNamespaceName, YQL_DATABASE_PGSQL));
}

struct XClusterPgSchemaNameParams {
  XClusterPgSchemaNameParams(
      bool empty_schema_name_on_producer_, bool empty_schema_name_on_consumer_)
      : empty_schema_name_on_producer(empty_schema_name_on_producer_),
        empty_schema_name_on_consumer(empty_schema_name_on_consumer_) {}

  bool empty_schema_name_on_producer;
  bool empty_schema_name_on_consumer;
};

class XClusterPgSchemaNameTest : public XClusterYsqlTest,
                                 public testing::WithParamInterface<XClusterPgSchemaNameParams> {};

INSTANTIATE_TEST_CASE_P(
    XClusterPgSchemaNameParams, XClusterPgSchemaNameTest,
    ::testing::Values(
        XClusterPgSchemaNameParams(true, true),
        XClusterPgSchemaNameParams(true, false),
        XClusterPgSchemaNameParams(false, true),
        XClusterPgSchemaNameParams(false, false)));

TEST_P(XClusterPgSchemaNameTest, SetupSameNameDifferentSchemaUniverseReplication) {
  constexpr int kNumTables = 3;
  constexpr int kNTabletsPerTable = 3;
  auto tables = ASSERT_RESULT(SetUpWithParams({}, {}, 1));

  auto schema_name = [](int i) { return Format("test_schema_$0", i); };

  FLAGS_TEST_create_table_with_empty_pgschema_name = GetParam().empty_schema_name_on_producer;
  // Create 3 producer tables with the same name but different schema-name.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<YBTableName> producer_table_names;
  for (int i = 0; i < kNumTables; i++) {
    auto t = ASSERT_RESULT(CreateYsqlTable(
        &producer_cluster_, kNamespaceName, schema_name(i), "test_table_1",
        boost::none /* tablegroup */, kNTabletsPerTable));
    // Need to set pgschema_name ourselves if it was not set due to flag.
    t.set_pgschema_name(schema_name(i));
    producer_table_names.push_back(t);

    std::shared_ptr<client::YBTable> producer_table;
    ASSERT_OK(producer_client()->OpenTable(t, &producer_table));
    producer_tables.push_back(producer_table);
  }

  FLAGS_TEST_create_table_with_empty_pgschema_name = GetParam().empty_schema_name_on_consumer;
  // Create 3 consumer tables with similar setting but in reverse order to complicate the test.
  std::vector<YBTableName> consumer_table_names;
  consumer_table_names.reserve(kNumTables);
  for (int i = kNumTables - 1; i >= 0; i--) {
    auto t = ASSERT_RESULT(CreateYsqlTable(
        &consumer_cluster_, kNamespaceName, schema_name(i), "test_table_1",
        boost::none /* tablegroup */, kNTabletsPerTable));
    t.set_pgschema_name(schema_name(i));
    consumer_table_names.push_back(t);

    std::shared_ptr<client::YBTable> consumer_table;
    ASSERT_OK(consumer_client()->OpenTable(t, &consumer_table));
  }
  std::reverse(consumer_table_names.begin(), consumer_table_names.end());

  // Setup universe replication for the 3 tables.
  ASSERT_OK(SetupUniverseReplication(producer_tables));

  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(kUniverseId, &resp));
  ASSERT_EQ(resp.entry().producer_id(), kUniverseId);

  // Write different numbers of records to the 3 producers, and verify that the
  // corresponding receivers receive the records.
  for (int i = 0; i < kNumTables; i++) {
    WriteWorkload(0, 2 * (i + 1), &producer_cluster_, producer_table_names[i]);
    ASSERT_OK(VerifyWrittenRecords(producer_table_names[i], consumer_table_names[i]));
  }

  ASSERT_OK(DeleteUniverseReplication());
}

class XClusterYsqlTestReadOnly : public XClusterYsqlTest {
  using Connections = std::vector<pgwrapper::PGConn>;

 protected:
  void SetUp() override {
    XClusterYsqlTest::SetUp();
  }

  static const std::string kTableName;

  Result<Connections> PrepareClusters() {
    RETURN_NOT_OK(Initialize(3 /* replication factor */));
    const std::vector<std::string> namespaces{kNamespaceName, "test_namespace2"};
    for (const auto& namespace_name : namespaces) {
      RETURN_NOT_OK(RunOnBothClusters([&namespace_name](Cluster* cluster) -> Status {
        RETURN_NOT_OK(CreateDatabase(cluster, namespace_name));
        auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
        return conn.ExecuteFormat("CREATE TABLE $0(id INT PRIMARY KEY, balance INT)", kTableName);
      }));
    }
    auto table_names = VERIFY_RESULT(producer_client()->ListTables(kTableName));
    auto tables = std::vector<std::shared_ptr<client::YBTable>>();
    tables.reserve(table_names.size());
    for (const auto& table_name : table_names) {
      tables.push_back(VERIFY_RESULT(producer_client()->OpenTable(table_name.table_id())));
      namespace_ids_.insert(table_name.namespace_id());
    }
    const auto universe_id = VERIFY_RESULT(GetUniverseId(&producer_cluster_));
    RETURN_NOT_OK(SetupUniverseReplication(
        universe_id, tables, {LeaderOnly::kTrue, Transactional::kTrue}));
    master::GetUniverseReplicationResponsePB resp;
    RETURN_NOT_OK(VerifyUniverseReplication(universe_id, &resp));
    return ConnectConsumers(namespaces);
  }

  Status SetRoleToStandbyAndWaitForValidSafeTime() {
    RETURN_NOT_OK(ChangeXClusterRole(cdc::XClusterRole::STANDBY));
    auto deadline = PropagationDeadline();
    for (const auto& namespace_id : namespace_ids_) {
      RETURN_NOT_OK(
          WaitForValidSafeTimeOnAllTServers(namespace_id, nullptr /* cluster */, deadline));
    }
    return Status::OK();
  }

  Status SetRoleToActive() {
    RETURN_NOT_OK(ChangeXClusterRole(cdc::XClusterRole::ACTIVE));
    return WaitForRoleChangeToPropogateToAllTServers(cdc::XClusterRole::ACTIVE);
  }

 private:
  Result<Connections> ConnectConsumers(const std::vector<std::string>& namespaces) {
    Connections connections;
    connections.reserve(namespaces.size());
    for (const auto& namespace_name : namespaces) {
      connections.push_back(VERIFY_RESULT(consumer_cluster_.ConnectToDB(namespace_name)));
    }
    return connections;
  }

  std::unordered_set<std::string> namespace_ids_;
};

const std::string XClusterYsqlTestReadOnly::kTableName{"test_table"};

TEST_F_EX(XClusterYsqlTest, DmlOperationsBlockedOnStandbyCluster, XClusterYsqlTestReadOnly) {
  auto consumer_conns = ASSERT_RESULT(PrepareClusters());
  auto query_patterns = {
      "INSERT INTO $0 VALUES($1, 100)",
      "UPDATE $0 SET balance = 0 WHERE id = $1",
      "DELETE FROM $0 WHERE id = $1"};
  std::vector<std::string> queries;
  queries.reserve(query_patterns.size());
  for (const auto& pattern : query_patterns) {
    queries.push_back(Format(pattern, kTableName, 1));
  }

  for (auto& conn : consumer_conns) {
    for (const auto& query : queries) {
      ASSERT_OK(conn.Execute(query));
    }
  }

  ASSERT_OK(SetRoleToStandbyAndWaitForValidSafeTime());

  // Test that INSERT, UPDATE, and DELETE operations fail while the cluster is on STANDBY mode.
  for (auto& conn : consumer_conns) {
    for (const auto& query : queries) {
      const auto status = conn.Execute(query);
      ASSERT_NOK(status);
      ASSERT_STR_CONTAINS(
          status.ToString(), "Data modification by DML is forbidden with STANDBY xCluster role");
    }
    ASSERT_OK(conn.FetchFormat("SELECT * FROM $0", kTableName));
  }

  ASSERT_OK(SetRoleToActive());

  // Test that DML operations are allowed again once the cluster is set to ACTIVE mode.
  for (auto& conn : consumer_conns) {
    for (const auto& query : queries) {
      ASSERT_OK(conn.Execute(query));
    }
  }
}

TEST_F_EX(XClusterYsqlTest, DdlAndReadOperationsAllowedOnStandbyCluster, XClusterYsqlTestReadOnly) {
  auto consumer_conns = ASSERT_RESULT(PrepareClusters());
  ASSERT_OK(SetRoleToStandbyAndWaitForValidSafeTime());
  constexpr auto* kNewTestTableName = "new_test_table";
  constexpr auto* kNewDBPrefix = "test_db_";

  for (size_t i = 0; i < consumer_conns.size(); ++i) {
    auto& conn = consumer_conns[i];
    // Test that creating databases is still allowed.
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0$1", kNewDBPrefix, i));
    // Test that altering databases is still allowed.
    ASSERT_OK(conn.ExecuteFormat("ALTER DATABASE $0$1 RENAME TO renamed_$0$1", kNewDBPrefix, i));
    // Test that creating tables is still allowed.
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (id INT, balance INT)", kNewTestTableName));
    // Test that altering tables is still allowed.
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD name VARCHAR(60)", kNewTestTableName));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP balance", kNewTestTableName));
    // Test that reading from tables is still allowed.
    ASSERT_RESULT(conn.FetchFormat("SELECT * FROM $0", kNewTestTableName));
  }
}

}  // namespace yb
