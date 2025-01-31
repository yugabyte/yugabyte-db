//
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
//

#include <shared_mutex>
#include <thread>

#include <boost/optional/optional.hpp>
#include <boost/optional/optional_io.hpp>

#include "yb/client/client-test-util.h"
#include "yb/client/error.h"
#include "yb/client/ql-dml-test-base.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/log.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/dockv/doc_key.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_test_util.h"

#include "yb/gutil/casts.h"

#include "yb/integration-tests/test_workload.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_ddl.proxy.h"

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/types.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/server/skewed_clock.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_retention_policy.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_error.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/tserver/tserver_xcluster_context_if.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/random_util.h"
#include "yb/util/range.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_format.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/cql/ql/util/statement_result.h"

using std::vector;
using std::string;

using namespace std::literals; // NOLINT

DECLARE_string(compression_type);
DECLARE_int64(db_block_size_bytes);
DECLARE_int64(db_write_buffer_size);
DECLARE_uint64(initial_seqno);
DECLARE_int32(leader_lease_duration_ms);
DECLARE_string(time_source);
DECLARE_int32(retryable_request_timeout_secs);
DECLARE_bool(enable_lease_revocation);
DECLARE_bool(rocksdb_disable_compactions);
DECLARE_int32(rocksdb_level0_slowdown_writes_trigger);
DECLARE_int32(rocksdb_level0_stop_writes_trigger);
DECLARE_bool(flush_rocksdb_on_shutdown);
DECLARE_int32(memstore_size_mb);
DECLARE_int64(global_memstore_size_mb_max);
DECLARE_int32(yb_num_shards_per_tserver);
DECLARE_int32(ysql_num_shards_per_tserver);
DECLARE_int32(transaction_table_num_tablets);
DECLARE_int32(transaction_table_num_tablets_per_tserver);
DECLARE_uint64(sst_files_hard_limit);
DECLARE_uint64(sst_files_soft_limit);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int32(raft_heartbeat_interval_ms);
DECLARE_int32(history_cutoff_propagation_interval_ms);
DECLARE_double(leader_failure_max_missed_heartbeat_periods);
DECLARE_string(regular_tablets_data_block_key_value_encoding);
DECLARE_bool(ycql_enable_packed_row);
DECLARE_bool(ysql_enable_packed_row);

DECLARE_bool(TEST_allow_stop_writes);
DECLARE_int32(TEST_backfill_sabotage_frequency);
DECLARE_int32(TEST_delay_execute_async_ms);
DECLARE_bool(TEST_log_cache_skip_eviction);
DECLARE_int32(TEST_preparer_batch_inject_latency_ms);
DECLARE_int32(TEST_tablet_inject_latency_on_apply_write_txn_ms);
DECLARE_int32(TEST_fetch_next_delay_ms);
DECLARE_string(TEST_fetch_next_delay_column);

namespace yb {
namespace client {

using ql::RowsResult;

namespace {

const std::string kKeyColumn = "key"s;
const std::string kRangeKey1Column = "range_key1"s;
const std::string kRangeKey2Column = "range_key2"s;
const std::string kValueColumn = "int_val"s;
const YBTableName kTable1Name(YQL_DATABASE_CQL, "my_keyspace", "ql_client_test_table1");
const YBTableName kTable2Name(YQL_DATABASE_CQL, "my_keyspace", "ql_client_test_table2");

int32_t ValueForKey(int32_t key) {
  return key * 2;
}

const int kTotalKeys = 250;
const int kBigSeqNo = 100500;

} // namespace

class QLTabletTest : public QLDmlTestBase<MiniCluster> {
 protected:
  void SetUp() override {
    server::SkewedClock::Register();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_time_source) = server::SkewedClock::kName;
    QLDmlTestBase::SetUp();
  }

  void CreateTables(uint64_t initial_seqno1, uint64_t initial_seqno2) {
    google::FlagSaver saver;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_initial_seqno) = initial_seqno1;
    CreateTable(kTable1Name, &table1_);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_initial_seqno) = initial_seqno2;
    CreateTable(kTable2Name, &table2_);
  }

  std::shared_ptr<YBqlWriteOp> CreateSetValueOp(
      int32_t key, int32_t value, const TableHandle& table) {
    const auto op = table.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, key);
    table.AddInt32ColumnValue(req, kValueColumn, value);
    return op;
  }

  void SetValue(const YBSessionPtr& session, int32_t key, int32_t value, const TableHandle& table) {
    auto op = CreateSetValueOp(key, value, table);
    ASSERT_OK(session->TEST_ApplyAndFlush(op));
    ASSERT_EQ(QLResponsePB::YQL_STATUS_OK, op->response().status())
        << op->response().error_message();
  }

  boost::optional<int32_t> GetValue(
      const YBSessionPtr& session, int32_t key, const TableHandle& table) {
    const auto op = CreateReadOp(key, table);
    EXPECT_OK(session->TEST_ApplyAndFlush(op));
    auto rowblock = RowsResult(op.get()).GetRowBlock();
    if (rowblock->row_count() == 0) {
      return boost::none;
    }
    EXPECT_EQ(1, rowblock->row_count());
    const auto& value = rowblock->row(0).column(0);
    EXPECT_TRUE(value.value().has_int32_value()) << "Value: " << value.value().ShortDebugString();
    return value.int32_value();
  }

  std::shared_ptr<YBqlReadOp> CreateReadOp(int32_t key, const TableHandle& table) {
    return client::CreateReadOp(key, table, kValueColumn);
  }

  void CreateTable(
      const YBTableName& table_name, TableHandle* table, int num_tablets = 0,
      bool transactional = false) {
    YBSchemaBuilder builder;
    builder.AddColumn(kKeyColumn)->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
    builder.AddColumn(kValueColumn)->Type(DataType::INT32);

    if (num_tablets == 0) {
      num_tablets = CalcNumTablets(3);
    }
    if (transactional) {
      TableProperties table_properties;
      table_properties.SetTransactional(true);
      builder.SetTableProperties(table_properties);
    }
    ASSERT_OK(table->Create(table_name, num_tablets, client_.get(), &builder));
  }

  YBSessionPtr CreateSession() {
    auto session = client_->NewSession(15s);
    return session;
  }

  void FillTable(int begin, int end, const TableHandle& table) {
    ASSERT_OK(WaitForTableLeaders(cluster_.get(), table->id(), 10s * kTimeMultiplier));
    ASSERT_OK(WaitAllReplicasSynchronizedWithLeader(cluster_.get(), 10s * kTimeMultiplier));
    LOG(INFO) << "Start filling table";
    {
      auto session = CreateSession();
      for (int i = begin; i != end; ++i) {
        SetValue(session, i, ValueForKey(i), table);
      }
    }
    LOG(INFO) << "Filled table";
    VerifyTable(begin, end, table);
    LOG(INFO) << "Verified table";
    ASSERT_OK(WaitSync(begin, end, table));
    LOG(INFO) << "All replicas are in-sync";
  }

  Status BatchedFillTable(
      const int begin, const int end, const int batch_size, const TableHandle& table) {
    {
      auto session = CreateSession();
      for (int i = begin; i != end;) {
        auto op = CreateSetValueOp(i, ValueForKey(i), table);
        ++i;
        if ((i - begin) % batch_size == 0 || i == end) {
          RETURN_NOT_OK(session->TEST_ApplyAndFlush(op));
        } else {
          session->Apply(op);
        }
      }
    }
    return WaitSync(begin, end, table);
  }

  void VerifyTable(int begin, int end, const TableHandle& table) {
    auto session = CreateSession();
    for (int i = begin; i != end; ++i) {
      auto value = GetValue(session, i, table);
      ASSERT_TRUE(value.is_initialized()) << "i: " << i << ", table: " << table->name().ToString();
      ASSERT_EQ(ValueForKey(i), *value) << "i: " << i << ", table: " << table->name().ToString();
    }
  }

  typedef std::pair<std::vector<std::string>, std::unordered_set<std::string>> TabletIdsAndReplicas;

  Result<TabletIdsAndReplicas> GetTabletIdsAndReplicas(const TableHandle& table) {
    std::vector<std::string> tablet_ids;
    std::unordered_set<std::string> replicas;

    auto tablet_infos = GetTabletInfos(table.name());
    if (!tablet_infos) {
      return STATUS_FORMAT(NotFound,
                           "No tablet information found for $0",
                           table->name());
    }
    for (auto tablet_info : *tablet_infos) {
      tablet_ids.emplace_back(tablet_info->tablet_id());
      auto replica_map = tablet_info->GetReplicaLocations();
      for (auto it = replica_map->begin(); it != replica_map->end(); it++) {
        replicas.insert(it->first);
      }
    }
    return TabletIdsAndReplicas(tablet_ids, replicas);
  }

  Status WaitSync(int begin, int end, const TableHandle& table) {
    auto deadline = MonoTime::Now() + MonoDelta::FromSeconds(30);
    TabletIdsAndReplicas info = VERIFY_RESULT(GetTabletIdsAndReplicas(table));
    std::vector<std::string> tablet_ids = info.first;
    std::unordered_set<std::string> replicas = info.second;
    for (const auto& replica : replicas) {
      RETURN_NOT_OK(DoWaitSync(deadline, tablet_ids, replica, begin, end, table));
    }
    return Status::OK();
  }

  Status DoWaitSync(
      const MonoTime& deadline,
      const std::vector<std::string>& tablet_ids,
      const std::string& replica,
      int begin,
      int end,
      const TableHandle& table) {
    auto tserver = cluster_->find_tablet_server(replica);
    if (!tserver) {
      return STATUS_FORMAT(NotFound, "Tablet server for $0 not found", replica);
    }
    auto endpoint = tserver->server()->rpc_server()->GetBoundAddresses().front();
    auto proxy = std::make_unique<tserver::TabletServerServiceProxy>(
        &tserver->server()->proxy_cache(), HostPort::FromBoundEndpoint(endpoint));

    auto condition = [&]() -> Result<bool> {
      for (int i = begin; i != end; ++i) {
        bool found = false;
        for (const std::string& tablet_id : tablet_ids) {
          tserver::ReadRequestPB req;
          {
            std::string partition_key;
            auto op = CreateReadOp(i, table);
            RETURN_NOT_OK(op->GetPartitionKey(&partition_key));
            auto* ql_batch = req.add_ql_batch();
            *ql_batch = op->request();
            auto hash_code = dockv::PartitionSchema::DecodeMultiColumnHashValue(partition_key);
            ql_batch->set_hash_code(hash_code);
            ql_batch->set_max_hash_code(hash_code);
          }

          rpc::RpcController controller;
          controller.set_timeout(MonoDelta::FromSeconds(1));
          req.set_tablet_id(tablet_id);
          req.set_consistency_level(YBConsistencyLevel::CONSISTENT_PREFIX);

          tserver::ReadResponsePB resp;
          RETURN_NOT_OK(proxy->Read(req, &resp, &controller));
          if (resp.has_error()) {
            RETURN_NOT_OK(
                StatusFromPB(resp.error().status())
                    .CloneAndAddErrorCode(tserver::TabletServerError(resp.error().code())));
          }
          const auto& ql_batch = resp.ql_batch(0);
          if (ql_batch.status() != QLResponsePB_QLStatus_YQL_STATUS_OK) {
            return STATUS_FORMAT(RemoteError,
                                 "Bad resp status: $0",
                                 QLResponsePB_QLStatus_Name(ql_batch.status()));
          }
          Schema projection;
          std::vector<ColumnId> column_refs;
          column_refs.emplace_back(table.ColumnId(kValueColumn));
          Schema total_schema = client::internal::GetSchema(table->schema());
          RETURN_NOT_OK(total_schema.CreateProjectionByIdsIgnoreMissing(column_refs, &projection));
          std::shared_ptr<std::vector<ColumnSchema>> columns =
              std::make_shared<std::vector<ColumnSchema>>(YBSchema(projection).columns());

          ql::RowsResult result(
              table->name(), columns,
              VERIFY_RESULT(controller.ExtractSidecar(ql_batch.rows_data_sidecar())));
          auto row_block = result.GetRowBlock();
          if (row_block->row_count() == 1) {
            if (found) {
              return STATUS_FORMAT(Corruption, "Key found twice: $0", i);
            }
            auto value = row_block->row(0).column(0).int32_value();
            if (value != ValueForKey(i)) {
              return STATUS_FORMAT(Corruption,
                                   "Wrong value for key: $0, expected: $1",
                                   value,
                                   ValueForKey(i));
            }
            found = true;
          }
        }
        if (!found) {
          LOG(INFO) << "Key not found: " << i;
          return false;
        }
      }
      return true;
    };

    return Wait(condition, deadline, "Waiting for replication");
  }

  Status VerifyConsistency(
      int begin, int end, const TableHandle& table, int expected_rows_mismatched = 0) {
    auto deadline = MonoTime::Now() + MonoDelta::FromSeconds(30 * kTimeMultiplier);
    TabletIdsAndReplicas info = VERIFY_RESULT(GetTabletIdsAndReplicas(table));
    std::vector<std::string> tablet_ids = info.first;
    std::unordered_set<std::string> replicas = info.second;

    for (const auto& replica : replicas) {
      RETURN_NOT_OK(
          DoVerify(deadline, tablet_ids, replica, begin, end, table, expected_rows_mismatched));
    }
    return Status::OK();
  }

  Status DoVerify(
      const MonoTime& deadline,
      const std::vector<std::string>& tablet_ids,
      const std::string& replica,
      const int begin,
      const int end,
      const TableHandle& table,
      const size_t expected_rows_mismatched) {
    auto tserver = cluster_->find_tablet_server(replica);
    if (!tserver) {
      return STATUS_FORMAT(NotFound, "Tablet server for $0 not found", replica);
    }
    auto endpoint = tserver->server()->rpc_server()->GetBoundAddresses().front();
    auto proxy = std::make_unique<tserver::TabletServerServiceProxy>(
        &tserver->server()->proxy_cache(), HostPort::FromBoundEndpoint(endpoint));

    CHECK_EQ(tablet_ids.size(), 1);
    for (const string& tablet_id : tablet_ids) {
      tserver::VerifyTableRowRangeRequestPB req;
      tserver::VerifyTableRowRangeResponsePB resp;

      req.set_tablet_id(tablet_id);
      req.set_num_rows(end - begin + 1);
      req.clear_start_key();  // empty string indicates start scan from start
      // read_time: if left empty, the safe time retrieved will be used instead

      rpc::RpcController controller;
      controller.set_deadline(deadline);
      RETURN_NOT_OK(proxy->VerifyTableRowRange(req, &resp, &controller));

      if (resp.consistency_stats().size() == 0) {
        return STATUS_FORMAT(
            NotFound,
            "Individual index consistency state missing for table $0.",
            table.table()->id());
      }
      for (auto it = resp.consistency_stats().begin(); it != resp.consistency_stats().end(); it++) {
        if (it->second != expected_rows_mismatched) {
          return STATUS_FORMAT(
              Corruption,
              "Incorrect number of rows reported to be dropped for index $0 built on table $1:"
              "found $2 rows reported when $3 rows mismatched was expected.",
              it->first, table.table()->id(), it->second, expected_rows_mismatched);
        }
      }
    }

    return Status::OK();
  }

  Status Import() {
    std::this_thread::sleep_for(1s); // Wait until all tablets a synced and flushed.
    EXPECT_OK(cluster_->FlushTablets());

    auto source_infos = VERIFY_RESULT(GetTabletInfos(kTable1Name));
    auto dest_infos = VERIFY_RESULT(GetTabletInfos(kTable2Name));
    EXPECT_EQ(source_infos.size(), dest_infos.size());
    for (size_t i = 0; i != source_infos.size(); ++i) {
      std::string start1, end1, start2, end2;
      {
        auto& metadata = source_infos[i]->metadata();
        SharedLock<std::remove_reference<decltype(metadata)>::type> lock(metadata);
        const auto& partition = metadata.state().pb.partition();
        start1 = partition.partition_key_start();
        end1 = partition.partition_key_end();
      }
      {
        auto& metadata = dest_infos[i]->metadata();
        SharedLock<std::remove_reference<decltype(metadata)>::type> lock(metadata);
        const auto& partition = metadata.state().pb.partition();
        start2 = partition.partition_key_start();
        end2 = partition.partition_key_end();
      }
      EXPECT_EQ(start1, start2);
      EXPECT_EQ(end1, end2);
    }
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      auto* tablet_manager = cluster_->mini_tablet_server(i)->server()->tablet_manager();
      for (size_t j = 0; j != source_infos.size(); ++j) {
        auto source_peer = tablet_manager->LookupTablet(source_infos[j]->id());
        EXPECT_NE(nullptr, source_peer);
        auto source_dir = source_peer->tablet()->metadata()->rocksdb_dir();
        auto dest_peer = tablet_manager->LookupTablet(dest_infos[j]->id());
        EXPECT_NE(nullptr, dest_peer);
        auto status = dest_peer->tablet()->ImportData(source_dir);
        if (!status.ok() && !status.IsNotFound()) {
          return status;
        }
      }
    }
    return Status::OK();
  }

  Result<scoped_refptr<master::TableInfo>> GetTableInfo(const YBTableName& table_name) {
    auto& catalog_manager =
        VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
    auto all_tables = catalog_manager.GetTables(master::GetTablesMode::kAll);
    for (const auto& table : all_tables) {
      if (table->name() == table_name.table_name()) {
        return table;
      }
    }
    return STATUS_FORMAT(NotFound, "Table $0 not found", table_name);
  }

  Result<master::TabletInfos> GetTabletInfos(const YBTableName& table_name) {
    return VERIFY_RESULT(GetTableInfo(table_name))->GetTablets();
  }

  Status WaitForTableCreation(const YBTableName& table_name,
                              master::IsCreateTableDoneResponsePB *resp) {
    return LoggedWaitFor([this, table_name, resp]() -> Result<bool> {
      master::IsCreateTableDoneRequestPB req;
      req.mutable_table()->set_table_name(table_name.table_name());
      req.mutable_table()->mutable_namespace_()->set_name(table_name.namespace_name());
      resp->Clear();

      master::MasterDdlProxy master_proxy(
          &client_->proxy_cache(), VERIFY_RESULT(cluster_->GetLeaderMasterBoundRpcAddr()));
      rpc::RpcController rpc;
      rpc.set_timeout(MonoDelta::FromSeconds(30));

      Status s = master_proxy.IsCreateTableDone(req, resp, &rpc);
      return s.ok() && !resp->has_error();
    }, MonoDelta::FromSeconds(30), "Table Creation");
  }

  // Make sure long reads are aborted by operation.
  void TestLongReadAbort(std::function<Status()> operation) {
    constexpr auto kNumRows = 100;

    ASSERT_NO_FATALS(
        CreateTable(kTable1Name, &table1_, /* num_tablets = */ 1, /* transactional = */ true));
    LOG(INFO) << "Created table";

    ASSERT_NO_FATALS(FillTable(0, kNumRows, table1_));
    LOG(INFO) << "Inserted " << kNumRows << " rows";

    std::vector<std::shared_ptr<tablet::Tablet>> shared_tablets;
    {
      for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
        shared_tablets.push_back(peer->shared_tablet());
      }
    }

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fetch_next_delay_ms) = 100;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fetch_next_delay_column) = "int_val";

    std::thread counter([this]{
      auto session = CreateSession();

      const auto start_time = CoarseMonoClock::now();

      // Use high enough timeout to test aborting reads.
      const auto rows_count_result =
          CountRows(session, table1_, kNumRows * FLAGS_TEST_fetch_next_delay_ms * 1ms * 10);
      LOG(INFO) << "Rows count result: " << rows_count_result;
      // Request should be aborted during tablet shutdown.
      ASSERT_FALSE(rows_count_result.ok());

      const auto time_elapsed = CoarseMonoClock::now() - start_time;
      // Abort should be fast.
      ASSERT_LE(time_elapsed, 1s * kTimeMultiplier);
    });

    // Wait for test table scan start. It could be delayed, because FLAGS_TEST_fetch_next_delay_ms
    // impacts master perf as well.
    RegexWaiterLogSink log_waiter(R"#(.*Delaying read for.*int_val.*)#");
    ASSERT_OK(log_waiter.WaitFor(30s));

    ASSERT_OK(operation());

    counter.join();
  }

  void TestDeletePartialKey(int num_range_keys_in_delete);

  void CreateAndVerifyIndexConsistency(int expected_number_rows_mismatched);

  TableHandle table1_;
  TableHandle table2_;
};

TEST_F(QLTabletTest, ImportToEmpty) {
  CreateTables(0, kBigSeqNo);

  FillTable(0, kTotalKeys, table1_);
  ASSERT_OK(Import());
  VerifyTable(0, kTotalKeys, table1_);
  VerifyTable(0, kTotalKeys, table2_);
}

TEST_F(QLTabletTest, ImportToNonEmpty) {
  CreateTables(0, kBigSeqNo);

  FillTable(0, kTotalKeys, table1_);
  FillTable(kTotalKeys, 2 * kTotalKeys, table2_);
  ASSERT_OK(Import());
  VerifyTable(0, 2 * kTotalKeys, table2_);
}

TEST_F(QLTabletTest, ImportToEmptyAndRestart) {
  CreateTables(0, kBigSeqNo);

  FillTable(0, kTotalKeys, table1_);
  ASSERT_OK(Import());
  VerifyTable(0, kTotalKeys, table2_);

  ASSERT_OK(cluster_->RestartSync());
  VerifyTable(0, kTotalKeys, table1_);
  VerifyTable(0, kTotalKeys, table2_);
}

TEST_F(QLTabletTest, ImportToNonEmptyAndRestart) {
  CreateTables(0, kBigSeqNo);

  FillTable(0, kTotalKeys, table1_);
  FillTable(kTotalKeys, 2 * kTotalKeys, table2_);

  ASSERT_OK(Import());
  VerifyTable(0, 2 * kTotalKeys, table2_);

  ASSERT_OK(cluster_->RestartSync());
  VerifyTable(0, kTotalKeys, table1_);
  VerifyTable(0, 2 * kTotalKeys, table2_);
}

TEST_F(QLTabletTest, LateImport) {
  CreateTables(kBigSeqNo, 0);

  FillTable(0, kTotalKeys, table1_);
  ASSERT_NOK(Import());
}

TEST_F(QLTabletTest, OverlappedImport) {
  CreateTables(kBigSeqNo - 2, kBigSeqNo);

  FillTable(0, kTotalKeys, table1_);
  FillTable(kTotalKeys, 2 * kTotalKeys, table2_);
  ASSERT_NOK(Import());
}

void QLTabletTest::CreateAndVerifyIndexConsistency(const int expected_number_rows_mismatched) {
  CreateTable(kTable1Name, &table1_, 1, true);
  FillTable(0, kTotalKeys, table1_);

  TableHandle index_table;
  kv_table_test::CreateIndex(
      yb::client::Transactional::kTrue, 1, false, table1_, client_.get(), &index_table);

  ASSERT_OK(client_->WaitUntilIndexPermissionsAtLeast(
      kTable1Name, index_table.name(), INDEX_PERM_READ_WRITE_AND_DELETE, 100ms /* max_wait */));
  ASSERT_OK(VerifyConsistency(
      0, kTotalKeys - 1, table1_,
      expected_number_rows_mismatched));  // no missing indexed rows check
  ASSERT_OK(VerifyConsistency(
      0, kTotalKeys - 1, index_table, expected_number_rows_mismatched));  // no orphans check
}

TEST_F(QLTabletTest, VerifyIndexRange) { CreateAndVerifyIndexConsistency(0); }

TEST_F(QLTabletTest, VerifyIndexRangeWithInconsistentTable) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_backfill_sabotage_frequency) = 10;
  const int kRowsDropped = kTotalKeys / FLAGS_TEST_backfill_sabotage_frequency;
  CreateAndVerifyIndexConsistency(kRowsDropped);
}

// Test expected number of tablets for transactions table - added for #2293.
TEST_F(QLTabletTest, TransactionsTableTablets) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_num_shards_per_tserver) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_num_shards_per_tserver) = 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_num_tablets) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_num_tablets_per_tserver) = 4;

  YBSchemaBuilder builder;
  builder.AddColumn(kKeyColumn)->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
  builder.AddColumn(kValueColumn)->Type(DataType::INT32);

  // Create transactional table.
  TableProperties table_properties;
  table_properties.SetTransactional(true);
  builder.SetTableProperties(table_properties);

  TableHandle table;
  ASSERT_OK(table.Create(kTable1Name, 8, client_.get(), &builder));

  // Wait for transactions table to be created.
  YBTableName table_name(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
  master::IsCreateTableDoneResponsePB resp;
  ASSERT_OK(WaitForTableCreation(table_name, &resp));
  ASSERT_TRUE(resp.done());

  auto tablets = ASSERT_RESULT(GetTabletInfos(table_name));
  ASSERT_EQ(
      tablets.size(),
      cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver);
}

void DoStepDowns(MiniCluster* cluster) {
  for (int j = 0; j != 5; ++j) {
    StepDownAllTablets(cluster);
    std::this_thread::sleep_for(5s);
  }
}

void VerifyLogIndicies(MiniCluster* cluster) {
  for (size_t i = 0; i != cluster->num_tablet_servers(); ++i) {
    auto peers = cluster->mini_tablet_server(i)->server()->tablet_manager()->GetTabletPeers();

    for (const auto& peer : peers) {
      int64_t index = ASSERT_RESULT(peer->GetEarliestNeededLogIndex());
      auto consensus = ASSERT_RESULT(peer->GetConsensus());
      ASSERT_EQ(consensus->GetLastCommittedOpId().index, index);
    }
  }
}

namespace {

constexpr auto kRetryableRequestTimeoutSecs = 4;

} // namespace

TEST_F(QLTabletTest, GCLogWithoutWrites) {
  SetAtomicFlag(kRetryableRequestTimeoutSecs, &FLAGS_retryable_request_timeout_secs);

  TableHandle table;
  CreateTable(kTable1Name, &table);

  FillTable(0, kTotalKeys, table);

  std::this_thread::sleep_for(1s * (kRetryableRequestTimeoutSecs + 1));
  ASSERT_OK(cluster_->FlushTablets());
  DoStepDowns(cluster_.get());
  VerifyLogIndicies(cluster_.get());
}

TEST_F(QLTabletTest, GCLogWithRestartWithoutWrites) {
  SetAtomicFlag(kRetryableRequestTimeoutSecs, &FLAGS_retryable_request_timeout_secs);

  TableHandle table;
  CreateTable(kTable1Name, &table);

  FillTable(0, kTotalKeys, table);

  std::this_thread::sleep_for(1s * (kRetryableRequestTimeoutSecs + 1));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_OK(cluster_->RestartSync());

  DoStepDowns(cluster_.get());
  VerifyLogIndicies(cluster_.get());
}

TEST_F(QLTabletTest, LeaderLease) {
  SetAtomicFlag(false, &FLAGS_enable_lease_revocation);

  TableHandle table;
  CreateTable(kTable1Name, &table);

  LOG(INFO) << "Filling table";
  FillTable(0, kTotalKeys, table);

  auto old_lease_ms = GetAtomicFlag(&FLAGS_leader_lease_duration_ms);
  SetAtomicFlag(60 * 1000, &FLAGS_leader_lease_duration_ms);
  // Wait for lease to sync.
  std::this_thread::sleep_for(2ms * old_lease_ms);

  LOG(INFO) << "Step down";
  StepDownAllTablets(cluster_.get());

  LOG(INFO) << "Write value";
  auto session = CreateSession();
  const auto op = table.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
  auto* const req = op->mutable_request();
  QLAddInt32HashValue(req, 1);
  table.AddInt32ColumnValue(req, kValueColumn, 1);
  auto status = session->TEST_ApplyAndFlush(op);
  ASSERT_TRUE(status.IsTimedOut()) << "Status: " << status;
}

// This test tries to catch situation when some entries were applied and flushed in RocksDB,
// but is not present in persistent logs.
//
// If that happens than we would get situation that after restart some node has records
// in RocksDB, but does not have log records for it. And would not be able to restore last
// hybrid time, also this node would not be able to remotely bootstrap other nodes.
//
// So we just delay one of follower logs and write a random key.
// Checking that flushed op id in RocksDB does not exceed last op id in logs.
TEST_F(QLTabletTest, WaitFlush) {
  google::FlagSaver saver;

  constexpr int kNumTablets = 1; // Use single tablet to increase chance of bad scenario.
  // Use small memtable to induce background flush on each write.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) = 10;

  TestWorkload workload(cluster_.get());
  workload.set_table_name(kTable1Name);
  workload.set_write_timeout_millis(30000);
  workload.set_num_tablets(kNumTablets);
  workload.set_num_write_threads(1);
  workload.set_write_batch_size(1);
  workload.set_payload_bytes(128);
  workload.Setup();
  workload.Start();

  std::vector<tablet::TabletPeerPtr> peers;

  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    auto tserver_peers =
        cluster_->mini_tablet_server(i)->server()->tablet_manager()->GetTabletPeers();
    ASSERT_EQ(tserver_peers.size(), 1);
    peers.push_back(tserver_peers.front());
  }

  bool leader_found = false;
  while (!leader_found) {
    for (size_t i = 0; i != peers.size(); ++i) {
      if (peers[i]->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY) {
        peers[(i + 1) % peers.size()]->log()->TEST_SetSleepDuration(500ms);
        leader_found = true;
        break;
      }
    }
  }

  auto deadline = CoarseMonoClock::Now() + 20s * kTimeMultiplier;
  while (CoarseMonoClock::Now() <= deadline) {
    for (const auto& peer : peers) {
      auto flushed_op_id = ASSERT_RESULT(peer->tablet()->MaxPersistentOpId()).regular;
      auto latest_entry_op_id = peer->log()->GetLatestEntryOpId();
      ASSERT_LE(flushed_op_id.index, latest_entry_op_id.index);
    }
    SleepFor(MonoDelta::FromMilliseconds(10));
  }

  for (const auto& peer : peers) {
    auto flushed_op_id = ASSERT_RESULT(peer->tablet()->MaxPersistentOpId()).regular;
    ASSERT_GE(flushed_op_id.index, 100);
  }

  workload.StopAndJoin();
}


TEST_F(QLTabletTest, BoundaryValues) {
  constexpr size_t kTotalThreads = 8;
  constexpr int kTotalRows = 10000;

  TableHandle table;
  CreateTable(kTable1Name, &table, 1);

  std::vector<std::thread> threads;
  std::atomic<int> idx(0);
  for (size_t t = 0; t != kTotalThreads; ++t) {
    threads.emplace_back([this, &idx, &table] {
      auto session = CreateSession();
      for (;;) {
        auto i = idx++;
        if (i >= kTotalRows) {
          break;
        }

        SetValue(session, i, -i, table);
      }
    });
  }
  const auto kSleepTime = NonTsanVsTsan(5s, 1s);
  std::this_thread::sleep_for(kSleepTime);
  LOG(INFO) << "Flushing tablets";
  ASSERT_OK(cluster_->FlushTablets());
  std::this_thread::sleep_for(kSleepTime);
  LOG(INFO) << "GC logs";
  ASSERT_OK(cluster_->CleanTabletLogs());
  LOG(INFO) << "Wait for threads";
  for (auto& thread : threads) {
    thread.join();
  }
  std::this_thread::sleep_for(kSleepTime * 5);
  ASSERT_OK(cluster_->RestartSync());

  size_t total_rows = 0;
  for (const auto& row : TableRange(table)) {
    EXPECT_EQ(row.column(0).int32_value(), -row.column(1).int32_value());
    ++total_rows;
  }
  ASSERT_EQ(kTotalRows, total_rows);

  ASSERT_OK(cluster_->FlushTablets());
  std::this_thread::sleep_for(kSleepTime);

  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    auto peers = cluster_->mini_tablet_server(i)->server()->tablet_manager()->GetTabletPeers();
    ASSERT_EQ(1, peers.size());
    auto& peer = *peers[0];
    auto op_id = peer.log()->GetLatestEntryOpId();
    auto* db = peer.tablet()->regular_db();
    int64_t max_index = 0;
    int64_t min_index = std::numeric_limits<int64_t>::max();
    for (const auto& file : db->GetLiveFilesMetaData()) {
      LOG(INFO) << "File: " << yb::ToString(file);
      max_index = std::max(
          max_index,
          down_cast<docdb::ConsensusFrontier&>(*file.largest.user_frontier).op_id().index);
      min_index = std::min(
          min_index,
          down_cast<docdb::ConsensusFrontier&>(*file.smallest.user_frontier).op_id().index);
    }

    // Allow several entries for non write ops.
    ASSERT_GE(max_index, op_id.index - 5);
    ASSERT_LE(min_index, 5);
  }
}

// There was bug with MvccManager when clocks were skewed.
// Client tries to read from follower and max safe time is requested w/o any limits,
// so new operations could be added with HT lower than returned.
TEST_F(QLTabletTest, SkewedClocks) {
  google::FlagSaver saver;

  auto delta_changers = SkewClocks(cluster_.get(), 50ms);

  TestWorkload workload(cluster_.get());
  workload.set_table_name(kTable1Name);
  workload.set_write_timeout_millis(30000);
  workload.set_num_tablets(12);
  workload.set_num_write_threads(2);
  workload.set_write_batch_size(1);
  workload.set_payload_bytes(128);
  workload.Setup();
  workload.Start();

  while (workload.rows_inserted() < 100) {
    std::this_thread::sleep_for(10ms);
  }

  TableHandle table;
  ASSERT_OK(table.Open(kTable1Name, client_.get()));
  auto session = CreateSession();

  for (int i = 0; i != 1000; ++i) {
    auto op = table.NewReadOp();
    auto req = op->mutable_request();
    QLAddInt32HashValue(req, i);
    auto value_column_id = table.ColumnId(kValueColumn);
    req->add_selected_exprs()->set_column_id(value_column_id);
    req->mutable_column_refs()->add_ids(value_column_id);

    QLRSColDescPB *rscol_desc = req->mutable_rsrow_desc()->add_rscol_descs();
    rscol_desc->set_name(kValueColumn);
    table.ColumnType(kValueColumn)->ToQLTypePB(rscol_desc->mutable_ql_type());
    op->set_yb_consistency_level(YBConsistencyLevel::CONSISTENT_PREFIX);
    ASSERT_OK(session->TEST_ApplyAndFlush(op));
    ASSERT_EQ(QLResponsePB::YQL_STATUS_OK, op->response().status());
  }

  workload.StopAndJoin();

  cluster_->Shutdown(); // Need to shutdown cluster before resetting clock back.
  cluster_.reset();
}

TEST_F(QLTabletTest, LeaderChange) {
  const int32_t kKey = 1;
  const int32_t kValue1 = 2;
  const int32_t kValue2 = 3;
  const int32_t kValue3 = 4;
  const int kNumTablets = 1;

  TableHandle table;
  CreateTable(kTable1Name, &table, kNumTablets);
  auto session = client_->NewSession(60s);

  // Write kValue1
  SetValue(session, kKey, kValue1, table);

  std::string leader_id;
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    auto server = cluster_->mini_tablet_server(i)->server();
    auto peers = server->tablet_manager()->GetTabletPeers();
    for (const auto& peer : peers) {
      if (peer->LeaderStatus() != consensus::LeaderStatus::NOT_LEADER) {
        leader_id = server->permanent_uuid();
        break;
      }
    }
  }

  LOG(INFO) << "Current leader: " << leader_id;

  ASSERT_NE(leader_id, "");

  LOG(INFO) << "CAS " << kValue1 << " => " << kValue2;
  const auto write_op = table.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
  auto* const req = write_op->mutable_request();
  QLAddInt32HashValue(req, kKey);
  table.AddInt32ColumnValue(req, kValueColumn, kValue2);

  table.SetColumn(req->add_column_values(), kValueColumn);
  table.SetInt32Condition(
    req->mutable_if_expr()->mutable_condition(), kValueColumn, QL_OP_EQUAL, kValue1);
  req->mutable_column_refs()->add_ids(table.ColumnId(kValueColumn));
  session->Apply(write_op);

  SetAtomicFlag(30000, &FLAGS_TEST_delay_execute_async_ms);
  auto flush_future = session->FlushFuture();
  std::this_thread::sleep_for(2s);

  SetAtomicFlag(0, &FLAGS_TEST_delay_execute_async_ms);

  LOG(INFO) << "Step down old leader";
  StepDownAllTablets(cluster_.get());

  // Write other key to refresh leader cache.
  // Otherwise we would hang of locking the key.
  LOG(INFO) << "Write other key";
  SetValue(session, kKey + 1, kValue1, table);

  LOG(INFO) << "Write " << kValue3;
  SetValue(session, kKey, kValue3, table);

  ASSERT_EQ(GetValue(session, kKey, table), kValue3);

  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    auto server = cluster_->mini_tablet_server(i)->server();
    auto peers = server->tablet_manager()->GetTabletPeers();
    bool found = false;
    for (const auto& peer : peers) {
      if (peer->LeaderStatus() != consensus::LeaderStatus::NOT_LEADER) {
        LOG(INFO) << "Request step down: " << server->permanent_uuid() << " => " << leader_id;
        consensus::LeaderStepDownRequestPB stepdown_request;
        stepdown_request.set_tablet_id(peer->tablet_id());
        stepdown_request.set_new_leader_uuid(leader_id);
        consensus::LeaderStepDownResponsePB resp;
        ASSERT_OK(ASSERT_RESULT(peer->GetConsensus())->StepDown(&stepdown_request, &resp));
        found = true;
        break;
      }
    }
    if (found) {
      break;
    }
  }

  ASSERT_OK(flush_future.get().status);
  ASSERT_EQ(QLResponsePB::YQL_STATUS_OK, write_op->response().status());

  ASSERT_EQ(GetValue(session, kKey, table), kValue3);
}

void QLTabletTest::TestDeletePartialKey(int num_range_keys_in_delete) {
  YBSchemaBuilder builder;
  builder.AddColumn(kKeyColumn)->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
  builder.AddColumn(kRangeKey1Column)->Type(DataType::INT32)->PrimaryKey()->NotNull();
  builder.AddColumn(kRangeKey2Column)->Type(DataType::INT32)->PrimaryKey()->NotNull();
  builder.AddColumn(kValueColumn)->Type(DataType::INT32);

  TableHandle table;
  ASSERT_OK(table.Create(kTable1Name, 1 /* num_tablets */, client_.get(), &builder));

  const auto kValue1 = 2;
  const auto kValue2 = 3;

  auto session1 = CreateSession();
  auto session2 = CreateSession();
  for (int key = 1; key != kTotalKeys; ++key) {
    {
      const auto op = table.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
      auto* const req = op->mutable_request();
      QLAddInt32HashValue(req, key);
      QLAddInt32RangeValue(req, key);
      QLAddInt32RangeValue(req, key);
      table.AddInt32ColumnValue(req, kValueColumn, kValue1);
      ASSERT_OK(session1->TEST_ApplyAndFlush(op));
      ASSERT_EQ(QLResponsePB::YQL_STATUS_OK, op->response().status());
    }

    const auto op_del = table.NewWriteOp(QLWriteRequestPB::QL_STMT_DELETE);
    {
      auto* const req = op_del->mutable_request();
      QLAddInt32HashValue(req, key);
      for (int i = 0; i != num_range_keys_in_delete; ++i) {
        QLAddInt32RangeValue(req, key);
      }
      session1->Apply(op_del);
    }

    const auto op_update = table.NewWriteOp(QLWriteRequestPB::QL_STMT_UPDATE);
    {
      auto* const req = op_update->mutable_request();
      QLAddInt32HashValue(req, key);
      QLAddInt32RangeValue(req, key);
      QLAddInt32RangeValue(req, key);
      table.AddInt32ColumnValue(req, kValueColumn, kValue2);
      req->mutable_if_expr()->mutable_condition()->set_op(QL_OP_EXISTS);
      session2->Apply(op_update);
    }
    auto future_del = session1->FlushFuture();
    auto future_update = session2->FlushFuture();
    ASSERT_OK(future_del.get().status);
    ASSERT_OK(future_update.get().status);
    ASSERT_EQ(QLResponsePB::YQL_STATUS_OK, op_del->response().status());
    ASSERT_EQ(QLResponsePB::YQL_STATUS_OK, op_update->response().status());

    auto stored_value = GetValue(session1, key, table);
    ASSERT_TRUE(!stored_value) << "Key: " << key << ", value: " << *stored_value;
  }
}

TEST_F(QLTabletTest, DeleteByHashKey) {
  TestDeletePartialKey(0);
}

TEST_F(QLTabletTest, DeleteByHashAndPartialRangeKey) {
  TestDeletePartialKey(1);
}

TEST_F(QLTabletTest, ManySstFilesBootstrap) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_flush_rocksdb_on_shutdown) = false;

  int key = 0;
  {
    google::FlagSaver flag_saver;

    size_t original_rocksdb_level0_stop_writes_trigger = 48;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_sst_files_hard_limit) =
        std::numeric_limits<uint64_t>::max() / 4;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_sst_files_soft_limit) = FLAGS_sst_files_hard_limit;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_stop_writes_trigger) = 10000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_slowdown_writes_trigger) = 10000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;
    CreateTable(kTable1Name, &table1_, 1);

    auto session = CreateSession();
    auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
    ASSERT_EQ(peers.size(), 1);
    LOG(INFO) << "Leader: " << peers[0]->permanent_uuid();
    int stop_key = 0;
    for (;;) {
      auto meta = peers[0]->tablet()->regular_db()->GetLiveFilesMetaData();
      LOG(INFO) << "Total files: " << meta.size();

      ++key;
      SetValue(session, key, ValueForKey(key), table1_);
      if (meta.size() <= original_rocksdb_level0_stop_writes_trigger) {
        ASSERT_OK(peers[0]->tablet()->Flush(tablet::FlushMode::kSync));
        stop_key = key + 10;
      } else if (key >= stop_key) {
        break;
      }
    }
  }

  cluster_->Shutdown();

  LOG(INFO) << "Starting cluster";

  ASSERT_OK(cluster_->StartSync());

  LOG(INFO) << "Verify table";

  VerifyTable(1, key, table1_);
}

class QLTabletTestSmallMemstore : public QLTabletTest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_memstore_size_mb) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_global_memstore_size_mb_max) = 1;
    QLTabletTest::SetUp();
  }
};

TEST_F_EX(QLTabletTest, DoubleFlush, QLTabletTestSmallMemstore) {
  SetAtomicFlag(false, &FLAGS_TEST_allow_stop_writes);

  TestWorkload workload(cluster_.get());
  workload.set_table_name(kTable1Name);
  workload.set_write_timeout_millis(30000);
  workload.set_num_tablets(1);
  workload.set_num_write_threads(10);
  workload.set_write_batch_size(1);
  workload.set_payload_bytes(1_KB);
  workload.Setup();
  workload.Start();

  while (workload.rows_inserted() < RegularBuildVsSanitizers(60000, 20000)) {
    std::this_thread::sleep_for(10ms);
  }

  workload.StopAndJoin();

  // Flush on rocksdb shutdown could produce second immutable memtable, that will stop writes.
  SetAtomicFlag(true, &FLAGS_TEST_allow_stop_writes);
  cluster_->Shutdown(); // Need to shutdown cluster before resetting clock back.
  cluster_.reset();
}

TEST_F(QLTabletTest, OperationMemTracking) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_log_cache_skip_eviction) = true;

  constexpr ssize_t kValueSize = 64_KB;
  const auto kWaitInterval = 50ms;

  YBSchemaBuilder builder;
  builder.AddColumn(kKeyColumn)->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
  builder.AddColumn(kValueColumn)->Type(DataType::STRING);

  TableHandle table;
  ASSERT_OK(table.Create(kTable1Name, CalcNumTablets(3), client_.get(), &builder));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tablet_inject_latency_on_apply_write_txn_ms) = 1000;

  const auto op = table.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
  auto* const req = op->mutable_request();
  QLAddInt32HashValue(req, 42);
  auto session = CreateSession();
  table.AddStringColumnValue(req, kValueColumn, std::string(kValueSize, 'X'));
  session->Apply(op);
  auto future = session->FlushFuture();
  auto server_tracker = MemTracker::GetRootTracker()->FindChild("server 1");
  auto tablets_tracker = server_tracker->FindChild("Tablets_overhead");
  auto log_tracker = server_tracker->FindChild("LogCache");

  std::chrono::steady_clock::time_point deadline;
  bool tracked_by_tablets = false;
  bool tracked_by_log_cache = false;
  for (;;) {
    // The consumption get order is important, otherwise we could get into situation where
    // mem tracking changed between gets.
    auto log_cache_consumption = log_tracker->consumption();
    tracked_by_log_cache = tracked_by_log_cache || log_cache_consumption >= kValueSize;
    int64_t operation_tracker_consumption = 0;
    for (auto& child : tablets_tracker->ListChildren()) {
      operation_tracker_consumption += child->FindChild("operation_tracker")->consumption();
    }

    tracked_by_tablets = tracked_by_tablets || operation_tracker_consumption >= kValueSize;
    LOG(INFO) << "Operation tracker consumption: " << operation_tracker_consumption
              << ", log cache consumption: " << log_cache_consumption;
    // We have overhead in both log cache and tablets.
    // So if value is double tracked then sum consumption will be higher than double value size.
    ASSERT_LE(operation_tracker_consumption + log_cache_consumption, kValueSize * 2)
        << DumpMemoryUsage();
    if (std::chrono::steady_clock::time_point() == deadline) { // operation did not finish yet
      if (future.wait_for(kWaitInterval) == std::future_status::ready) {
        LOG(INFO) << "Value written";
        deadline = std::chrono::steady_clock::now() + 3s;
        ASSERT_OK(future.get().status);
        ASSERT_EQ(QLResponsePB::YQL_STATUS_OK, op->response().status());
      }
    } else if (deadline < std::chrono::steady_clock::now() || tracked_by_log_cache) {
      break;
    } else {
      std::this_thread::sleep_for(kWaitInterval);
    }
  }

  ASSERT_TRUE(tracked_by_tablets);
  ASSERT_TRUE(tracked_by_log_cache);
}

// Checks the existance of the BlockBasedTable memtracker and verifies that its size is greater
// than zero after creating a table and flushing it.  Then deletes the table, and verifies that
// the memtracker is removed.
TEST_F(QLTabletTest, BlockCacheMemTracking) {
  const auto kSleepTime = NonTsanVsTsan(5s, 1s);
  constexpr size_t kTotalRows = 10000;
  constexpr size_t kBatchSize = 250;
  const string kBlockTrackerName = "BlockBasedTable";

  TableHandle table;
  CreateTable(kTable1Name, &table, 1);
  ASSERT_OK(BatchedFillTable(0, kTotalRows, kBatchSize, table));

  auto server_tracker = MemTracker::GetRootTracker()->FindChild("server 1");
  auto block_cache_tracker = server_tracker->FindChild(kBlockTrackerName);
  ASSERT_TRUE(block_cache_tracker);

  std::this_thread::sleep_for(kSleepTime);
  LOG(INFO) << "Flushing tablets";
  ASSERT_OK(cluster_->FlushTablets());
  std::this_thread::sleep_for(kSleepTime);

  auto block_cache_children = block_cache_tracker->ListChildren();
  // check that there is exactly one child memtracker
  ASSERT_EQ(block_cache_children.size(), 1);
  // check that the child memtracker has a consumption greater than zero
  ASSERT_GT(block_cache_children[0]->consumption(), 0);

  LOG(INFO) << "Deleting table";
  ASSERT_OK(client_->DeleteTable(kTable1Name, true));
  std::this_thread::sleep_for(kSleepTime);

  // after table deletion, assert that there are no longer any tablet-level block cache memtrackers
  block_cache_tracker = server_tracker->FindChild(kBlockTrackerName);
  ASSERT_TRUE(block_cache_tracker);
  ASSERT_TRUE(block_cache_tracker->ListChildren().empty());
}

// Checks history cutoff for cluster against previous state.
// Committed history cutoff should not go backward.
// Updates committed_history_cutoff with current state.
void VerifyHistoryCutoff(MiniCluster* cluster, HybridTime* prev_committed,
                         const std::string& trace) {
  SCOPED_TRACE(trace);
  const auto base_delta_us =
      -FLAGS_timestamp_history_retention_interval_sec * MonoTime::kMicrosecondsPerSecond;
  constexpr auto kExtraDeltaMs = 200;
  // Allow one 2 Raft rounds + processing delta to replicate operation, update committed and
  // propagate it.
  const auto committed_delta_us =
      base_delta_us -
      (FLAGS_raft_heartbeat_interval_ms * 2 + kExtraDeltaMs) * MonoTime::kMicrosecondsPerMillisecond
          * kTimeMultiplier;

  HybridTime committed = HybridTime::kMin;
  auto deadline = CoarseMonoClock::now() + 5s * kTimeMultiplier;
  for (;;) {
    ASSERT_LE(CoarseMonoClock::now(), deadline);
    auto peers = ListTabletPeers(cluster, ListPeersFilter::kAll);
    std::sort(peers.begin(), peers.end(), [](const auto& lhs, const auto& rhs) {
      return lhs->permanent_uuid() < rhs->permanent_uuid();
    });
    if (peers.size() != cluster->num_tablet_servers()) {
      std::this_thread::sleep_for(100ms);
      continue;
    }
    bool complete = false;
    for (size_t i = 0; i < peers.size(); ++i) {
      auto peer = peers[i];
      SCOPED_TRACE(Format("Peer: $0, Trace: $1", peer->permanent_uuid(), trace));
      if (peer->state() != tablet::RaftGroupStatePB::RUNNING) {
        complete = false;
        break;
      }
      auto cutoff = peer->tablet()->RetentionPolicy()->GetRetentionDirective()
          .history_cutoff;
      ASSERT_EQ(cutoff.cotables_cutoff_ht, HybridTime::kInvalid);
      auto peer_history_cutoff = cutoff.primary_cutoff_ht;
      committed = std::max(peer_history_cutoff, committed);
      auto min_allowed = std::min(peer->clock_ptr()->Now().AddMicroseconds(committed_delta_us),
                                  peer->tablet()->mvcc_manager()->LastReplicatedHybridTime());
      if (peer_history_cutoff < min_allowed) {
        LOG(INFO) << "Committed did not catch up for " << peer->permanent_uuid() << ": "
                  << peer_history_cutoff << " vs " << min_allowed;
        complete = false;
        break;
      }
      if (ASSERT_RESULT(peer->GetConsensus())->GetLeaderStatus() ==
          consensus::LeaderStatus::LEADER_AND_READY) {
        complete = true;
      }
    }
    if (complete) {
      break;
    }
    std::this_thread::sleep_for(100ms);
  }
  ASSERT_GE(committed, *prev_committed);
  *prev_committed = committed;
}

// Basic check for history cutoff evolution
TEST_F(QLTabletTest, HistoryCutoff) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = kTimeMultiplier;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_history_cutoff_propagation_interval_ms) = 100;

  CreateTable(kTable1Name, &table1_, /* num_tablets= */ 1);
  HybridTime committed_history_cutoff = HybridTime::kMin;
  FillTable(0, 10, table1_);
  ASSERT_NO_FATALS(VerifyHistoryCutoff(cluster_.get(), &committed_history_cutoff, "After write"));

  // Check that we restore committed state after restart.
  std::array<HybridTime, 3> peer_committed;
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    auto peers = cluster_->mini_tablet_server(i)->server()->tablet_manager()->GetTabletPeers();
    ASSERT_EQ(peers.size(), 1);
    auto cutoff =
        peers[0]->tablet()->RetentionPolicy()->GetRetentionDirective().history_cutoff;
    ASSERT_EQ(cutoff.cotables_cutoff_ht, HybridTime::kInvalid);
    peer_committed[i] = cutoff.primary_cutoff_ht;
    LOG(INFO) << "Peer: " << peers[0]->permanent_uuid() << ", index: " << i
              << ", committed: " << peer_committed[i];
    cluster_->mini_tablet_server(i)->Shutdown();
  }

  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    ASSERT_OK(cluster_->mini_tablet_server(i)->Start(tserver::WaitTabletsBootstrapped::kFalse));
    for (;;) {
      auto tserver = cluster_->mini_tablet_server(i)->server();
      auto peers = tserver->tablet_manager()->GetTabletPeers();
      ASSERT_LE(peers.size(), 1);
      if (peers.empty() || peers[0]->state() != tablet::RaftGroupStatePB::RUNNING) {
        std::this_thread::sleep_for(100ms);
        continue;
      }
      if (!tserver->GetXClusterContext()
               .GetSafeTime(peers[0]->tablet()->metadata()->namespace_id())
               .ok()) {
        std::this_thread::sleep_for(100ms);
        continue;
      }
      SCOPED_TRACE(Format("Peer: $0, index: $1", peers[0]->permanent_uuid(), i));
      auto cutoff = peers[0]->tablet()->RetentionPolicy()->
          GetRetentionDirective().history_cutoff;
      ASSERT_EQ(cutoff.cotables_cutoff_ht, HybridTime::kInvalid);
      ASSERT_GE(cutoff.primary_cutoff_ht, peer_committed[i]);
      break;
    }
    cluster_->mini_tablet_server(i)->Shutdown();
  }

  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    ASSERT_OK(cluster_->mini_tablet_server(i)->Start(tserver::WaitTabletsBootstrapped::kFalse));
  }
  ASSERT_NO_FATALS(VerifyHistoryCutoff(cluster_.get(), &committed_history_cutoff, "After restart"));

  // Wait to check history cutoff advance w/o operations.
  std::this_thread::sleep_for(
      FLAGS_timestamp_history_retention_interval_sec * 1s +
      FLAGS_history_cutoff_propagation_interval_ms * 3ms);
  ASSERT_NO_FATALS(VerifyHistoryCutoff(cluster_.get(), &committed_history_cutoff, "Final"));
}

class QLTabletRf1Test : public QLTabletTest {
 public:
  void SetUp() override {
    mini_cluster_opt_.num_masters = 1;
    mini_cluster_opt_.num_tablet_servers = 1;
    QLTabletTest::SetUp();
  }
};

class QLTabletRf1TestToggleEnablePackedRow :
    public QLTabletRf1Test, public ::testing::WithParamInterface<bool> {
 public:
  void SetFlags() override {
    QLTabletRf1Test::SetFlags();
    ASSERT_OK(SET_FLAG(ycql_enable_packed_row, GetParam()));
    ASSERT_OK(SET_FLAG(ysql_enable_packed_row, GetParam()));
  }
};

INSTANTIATE_TEST_SUITE_P(, QLTabletRf1TestToggleEnablePackedRow, ::testing::Bool());

// For this test we don't need actually RF3 setup which also makes test flaky because of
// https://github.com/yugabyte/yugabyte-db/issues/4663.
TEST_P(QLTabletRf1TestToggleEnablePackedRow, GetMiddleKey) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) = 20_KB;

  TestWorkload workload(cluster_.get());
  workload.set_table_name(kTable1Name);
  workload.set_write_timeout_millis(30000);
  workload.set_num_tablets(1);
  workload.set_num_write_threads(2);
  workload.set_write_batch_size(1);
  workload.set_payload_bytes(16);
  workload.Setup();

  LOG(INFO) << "Starting workload ...";
  Stopwatch s(Stopwatch::ALL_THREADS);
  s.start();
  workload.Start();

  const auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
  ASSERT_EQ(peers.size(), 1);
  const auto& tablet = *ASSERT_NOTNULL(peers[0]->tablet());

  // We want some compactions to happen, so largest SST file will become large enough for its
  // approximate middle key to roughly split the whole tablet into two parts that are close in size.
  // Need to have the largest file to be more than 50% of total size of all SST -- setting to 600KB
  // is enough to achive this condition (should give ~65% of total size when packed rows are on).
  while (tablet.regular_db()->GetCurrentVersionDataSstFilesSize() <
         implicit_cast<size_t>(30 * FLAGS_db_write_buffer_size)) {
    std::this_thread::sleep_for(100ms);
  }

  workload.StopAndJoin();
  s.stop();
  LOG(INFO) << "Workload stopped, it took: " << AsString(s.elapsed());

  LOG(INFO) << "Rows inserted: " << workload.rows_inserted();
  LOG(INFO) << "Number of SST files: " << tablet.regular_db()->GetCurrentVersionNumSSTFiles();

  ASSERT_OK(cluster_->FlushTablets());

  const auto encoded_split_key = ASSERT_RESULT(tablet.GetEncodedMiddleSplitKey());
  LOG(INFO) << "Encoded split key: " << Slice(encoded_split_key).ToDebugString();

  if (tablet.metadata()->partition_schema()->IsHashPartitioning()) {
    dockv::DocKey split_key;
    Slice key_slice = encoded_split_key;
    ASSERT_OK(split_key.DecodeFrom(&key_slice, dockv::DocKeyPart::kUpToHashCode));
    ASSERT_TRUE(key_slice.empty()) << "Extra bytes after decoding: " << key_slice.ToDebugString();
    ASSERT_EQ(split_key.hashed_group().size() + split_key.range_group().size(), 0)
        << "Hash-based partition: middle key should only have encoded hash code";
    LOG(INFO) << "Split key: " << AsString(split_key);
  } else {
    dockv::SubDocKey split_key;
    ASSERT_OK(split_key.FullyDecodeFrom(encoded_split_key, dockv::HybridTimeRequired::kFalse));
    ASSERT_EQ(split_key.num_subkeys(), 0)
        << "Range-based partition: middle doc key should not have sub doc key components";
    LOG(INFO) << "Split key: " << AsString(split_key);
  }

  // Checking number of keys less/bigger than the approximate middle key.
  size_t total_keys = 0;
  size_t num_keys_less = 0;

  rocksdb::ReadOptions read_opts;
  read_opts.query_id = rocksdb::kDefaultQueryId;
  std::unique_ptr<rocksdb::Iterator> iter(tablet.regular_db()->NewIterator(read_opts));

  for (iter->SeekToFirst(); ASSERT_RESULT(iter->CheckedValid()); iter->Next()) {
    Slice key = iter->key();
    if (key.Less(encoded_split_key)) {
      ++num_keys_less;
    }
    ++total_keys;
  }

  LOG(INFO) << "Total keys: " << total_keys;
  LOG(INFO) << "Number of keys less than approximate middle key: " << num_keys_less;
  const auto num_keys_less_percent = 100 * num_keys_less / total_keys;

  LOG(INFO) << Format(
      "Number of keys less than approximate middle key: $0 ($1%)", num_keys_less,
      num_keys_less_percent);

  ASSERT_GE(num_keys_less_percent, 40);
  ASSERT_LE(num_keys_less_percent, 60);
}

namespace {

std::vector<OpId> GetLastAppliedOpIds(const std::vector<tablet::TabletPeerPtr>& peers) {
  std::vector<OpId> last_applied_op_ids;
  for (auto& peer : peers) {
    const auto last_applied_op_id = CHECK_RESULT(peer->GetConsensus())->GetLastAppliedOpId();
    VLOG(1) << "Peer: " << AsString(peer->permanent_uuid())
            << ", last applied op ID: " << AsString(last_applied_op_id);
    last_applied_op_ids.push_back(last_applied_op_id);
  }
  return last_applied_op_ids;
}

Result<OpId> GetAllAppliedOpId(const std::vector<tablet::TabletPeerPtr>& peers) {
  for (auto& peer : peers) {
    if (peer->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY) {
      return VERIFY_RESULT(peer->GetRaftConsensus())->GetAllAppliedOpId();
    }
  }
  return STATUS(NotFound, "No leader found");
}

Status WaitForAppliedOpIdsStabilized(
    const std::vector<tablet::TabletPeerPtr>& peers, const MonoDelta& timeout) {
  std::vector<OpId> prev_last_applied_op_ids;
  return WaitFor(
      [&]() {
        std::vector<OpId> last_applied_op_ids = GetLastAppliedOpIds(peers);
        LOG(INFO) << "last_applied_op_ids: " << AsString(last_applied_op_ids);
        if (last_applied_op_ids == prev_last_applied_op_ids) {
          return true;
        }
        prev_last_applied_op_ids = last_applied_op_ids;
        return false;
      },
      timeout, "Waiting for applied op IDs to stabilize", 2000ms * kTimeMultiplier, 1);
}

} // namespace

TEST_F(QLTabletTest, LastAppliedOpIdTracking) {
  constexpr auto kAppliesTimeout = 10s * kTimeMultiplier;

  TableHandle table;
  CreateTable(kTable1Name, &table, /* num_tablets =*/1);
  auto session = client_->NewSession(60s);

  LOG(INFO) << "Writing data...";
  int key = 0;
  for (; key < 10; ++key) {
    SetValue(session, key, key, table);
  }
  LOG(INFO) << "Writing completed";

  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);

  ASSERT_OK(WaitForAppliedOpIdsStabilized(peers, kAppliesTimeout));
  auto last_applied_op_ids = GetLastAppliedOpIds(peers);
  LOG(INFO) << "last_applied_op_ids: " << AsString(last_applied_op_ids);
  auto all_applied_op_id = ASSERT_RESULT(GetAllAppliedOpId(peers));
  LOG(INFO) << "all_applied_op_id: " << AsString(all_applied_op_id);
  for (const auto& last_applied_op_id : last_applied_op_ids) {
    ASSERT_EQ(last_applied_op_id, all_applied_op_id);
  }

  LOG(INFO) << "Shutting down TS-0";
  cluster_->mini_tablet_server(0)->Shutdown();

  peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);

  LOG(INFO) << "Writing more data...";
  for (; key < 20; ++key) {
    SetValue(session, key, key, table);
  }
  LOG(INFO) << "Writing completed";

  ASSERT_OK(WaitForAppliedOpIdsStabilized(peers, kAppliesTimeout));
  auto new_all_applied_op_id = ASSERT_RESULT(GetAllAppliedOpId(peers));
  // We expect turned off TS to lag behind and not let all applied OP ids to advance.
  // In case TS-0 was leader, all_applied_op_id will be 0 on a new leader until it hears from TS-0.
  ASSERT_TRUE(new_all_applied_op_id == all_applied_op_id || new_all_applied_op_id.empty());

  // Save max applied op ID.
  last_applied_op_ids = GetLastAppliedOpIds(peers);
  auto max_applied_op_id = OpId::Min();
  for (const auto& last_applied_op_id : last_applied_op_ids) {
    max_applied_op_id = std::max(max_applied_op_id, last_applied_op_id);
  }
  ASSERT_GT(max_applied_op_id, all_applied_op_id);

  LOG(INFO) << "Restarting TS-0";
  ASSERT_OK(cluster_->mini_tablet_server(0)->Start(tserver::WaitTabletsBootstrapped::kFalse));

  // TS-0 should catch up on applied ops.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return VERIFY_RESULT(GetAllAppliedOpId(peers)) == max_applied_op_id;
      },
      kAppliesTimeout, "Waiting for all ops to apply"));
  last_applied_op_ids = GetLastAppliedOpIds(peers);
  for (const auto& last_applied_op_id : last_applied_op_ids) {
    ASSERT_EQ(last_applied_op_id, max_applied_op_id);
  }
}

TEST_F(QLTabletTest, SlowPrepare) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_preparer_batch_inject_latency_ms) = 100;

  const int kNumTablets = 1;

  auto session = client_->NewSession(60s);

  TestWorkload workload(cluster_.get());
  workload.set_table_name(kTable1Name);
  workload.set_write_timeout_millis(30000 * kTimeMultiplier);
  workload.set_num_tablets(kNumTablets);
  workload.set_num_write_threads(2);
  workload.set_write_batch_size(1);
  workload.Setup();
  workload.Start();

  std::this_thread::sleep_for(2s);
  StepDownAllTablets(cluster_.get());

  workload.StopAndJoin();
}

TEST_F(QLTabletTest, ElectUnsynchronizedFollower) {
  TableHandle table;
  CreateTable(kTable1Name, &table, 1);

  auto unsynchronized_follower = cluster_->mini_tablet_server(0)->server()->permanent_uuid();
  LOG(INFO) << "Unsynchronized follower: " << unsynchronized_follower;
  cluster_->mini_tablet_server(0)->Shutdown();

  auto session = CreateSession();
  SetValue(session, 1, -1, table);

  auto leader_idx = ASSERT_RESULT(ServerWithLeaders(cluster_.get()));
  LOG(INFO) << "Leader: " << cluster_->mini_tablet_server(leader_idx)->server()->permanent_uuid();
  auto follower_idx = 1 ^ 2 ^ leader_idx;
  LOG(INFO) << "Turning off follower: "
            << cluster_->mini_tablet_server(follower_idx)->server()->permanent_uuid();
  cluster_->mini_tablet_server(follower_idx)->Shutdown();
  auto peers =
      cluster_->mini_tablet_server(leader_idx)->server()->tablet_manager()->GetTabletPeers();
  ASSERT_EQ(peers.size(), 1);
  {
    google::FlagSaver flag_saver;
    consensus::LeaderStepDownRequestPB req;
    req.set_tablet_id(peers.front()->tablet_id());
    req.set_force_step_down(true);
    req.set_new_leader_uuid(unsynchronized_follower);
    consensus::LeaderStepDownResponsePB resp;

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_leader_failure_max_missed_heartbeat_periods) = 10000;
    ASSERT_OK(ASSERT_RESULT(peers.front()->GetRaftConsensus())->StepDown(&req, &resp));
    ASSERT_FALSE(resp.has_error()) << resp.error().ShortDebugString();
  }

  ASSERT_OK(cluster_->mini_tablet_server(0)->Start(tserver::WaitTabletsBootstrapped::kFalse));

  ASSERT_NO_FATALS(SetValue(session, 2, -2, table));

  ASSERT_OK(cluster_->mini_tablet_server(follower_idx)->Start(
    tserver::WaitTabletsBootstrapped::kFalse));
}

TEST_F(QLTabletTest, FollowerRestartDuringWrite) {
  TableHandle table;
  CreateTable(kTable1Name, &table, 1);

  for (auto iter = 0; iter != 6; ++iter) {
    auto session = CreateSession();
    SetValue(session, 1, -1, table);

    auto leader_idx = ASSERT_RESULT(ServerWithLeaders(cluster_.get()));
    LOG(INFO) << "Leader: " << cluster_->mini_tablet_server(leader_idx)->server()->permanent_uuid();
    auto follower_idx = (leader_idx + 1) % cluster_->num_tablet_servers();
    auto follower = cluster_->mini_tablet_server(follower_idx)->server();
    LOG(INFO) << "Follower: "  << follower->permanent_uuid();
    auto follower_peers = follower->tablet_manager()->GetTabletPeers();
    for (const auto& peer : follower_peers) {
      ASSERT_RESULT(peer->GetRaftConsensus())
          ->TEST_DelayUpdate(FLAGS_raft_heartbeat_interval_ms / 2 * 1ms);
    }

    SetValue(session, 2, -2, table);
    std::this_thread::sleep_for(FLAGS_raft_heartbeat_interval_ms / 2 * 1ms);
    SetValue(session, 3, -3, table);

    // Shutdown follower, so it would not accept updates and exponential backoff will turn to send
    // empty operations.
    cluster_->mini_tablet_server(follower_idx)->Shutdown();

    // Wait exponential backoff goes to empty operations.
    std::this_thread::sleep_for(FLAGS_raft_heartbeat_interval_ms * 3ms);

    SetValue(session, 4, -4, table);

    ASSERT_OK(cluster_->mini_tablet_server(follower_idx)->Start(
      tserver::WaitTabletsBootstrapped::kFalse));

    // Wait until newly started follower receive a new operation.
    // Without fix for GH #7145 it would crash in this case.
    std::this_thread::sleep_for(FLAGS_raft_heartbeat_interval_ms * 3ms);

    ASSERT_OK(cluster_->RestartSync());
  }
}

TEST_F_EX(QLTabletTest, DataBlockKeyValueEncoding, QLTabletRf1Test) {
  // Key encoding gives benefits only when keys are nearly similar, for instance different columns
  // of the same row.
  // For packed row the benefit is smaller, so we disable packed row for this test, to check
  // whether encoding works at all.
  docdb::DisableYcqlPackedRow();

  constexpr auto kNumRows = 4000;
  constexpr auto kNumRowsPerBatch = 100;
  // We are testing delta encoding, but not compression.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_compression_type) = "NoCompression";

  struct SstSizes {
    size_t regular_table = 0;
    size_t index_table = 0;
  };
  std::unordered_map<rocksdb::KeyValueEncodingFormat, SstSizes, EnumHash> sst_sizes;

  constexpr auto kEncodingSharedPrefix =
      rocksdb::KeyValueEncodingFormat::kKeyDeltaEncodingSharedPrefix;
  constexpr auto kEncodingThreeSharedParts =
      rocksdb::KeyValueEncodingFormat::kKeyDeltaEncodingThreeSharedParts;

  for (auto encoding : {kEncodingSharedPrefix, kEncodingThreeSharedParts}) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_regular_tablets_data_block_key_value_encoding) =
        KeyValueEncodingFormatToString(encoding);
    const YBTableName table_name(
        YQL_DATABASE_CQL, "my_keyspace", Format("ql_client_test_table_$0", encoding));
    TableHandle table;
    CreateTable(table_name, &table, /* num_tablets = */ 1);
    ASSERT_OK(BatchedFillTable(/* begin = */ 0, /* end = */ kNumRows, kNumRowsPerBatch, table));

    TableHandle index_table;
    kv_table_test::CreateIndex(
        yb::client::Transactional::kTrue, /* indexed_column_index = */ 1,
        /* use_mangled_names = */ false, table, client_.get(), &index_table);
    ASSERT_OK(client_->WaitUntilIndexPermissionsAtLeast(
        table_name, index_table.name(), INDEX_PERM_READ_WRITE_AND_DELETE, /* max_wait = */ 10s));

    ASSERT_OK(cluster_->FlushTablets());

    auto get_tablet_size = [](tablet::Tablet* tablet) -> Result<size_t> {
      RETURN_NOT_OK(tablet->Flush(tablet::FlushMode::kSync));
      RETURN_NOT_OK(tablet->ForceManualRocksDBCompact());
      return tablet->GetCurrentVersionSstFilesSize();
    };

    for (const auto& tablet_peer : ListTableTabletPeers(cluster_.get(), table->id())) {
      sst_sizes[encoding].regular_table += ASSERT_RESULT(get_tablet_size(tablet_peer->tablet()));
    }
    for (const auto& tablet_peer : ListTableTabletPeers(cluster_.get(), index_table->id())) {
      sst_sizes[encoding].index_table += ASSERT_RESULT(get_tablet_size(tablet_peer->tablet()));
    }
  }
  ASSERT_GT(
      1.0 * sst_sizes[kEncodingSharedPrefix].regular_table /
          sst_sizes[kEncodingThreeSharedParts].regular_table,
      1.3);
  ASSERT_EQ(sst_sizes[kEncodingSharedPrefix].index_table,
            sst_sizes[kEncodingThreeSharedParts].index_table);
}

TEST_F_EX(QLTabletTest, CompactDeletedColumn, QLTabletRf1Test) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_history_cutoff_propagation_interval_ms) = 1;

  constexpr int kKeys = 100;
  const std::string kStringColumn = "str_column";

  YBSchemaBuilder builder;
  builder.AddColumn(kKeyColumn)->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
  builder.AddColumn(kValueColumn)->Type(DataType::INT32);
  builder.AddColumn(kStringColumn)->Type(DataType::STRING);
  TableHandle table;
  ASSERT_OK(table.Create(kTable1Name, 1, client_.get(), &builder));
  auto session = CreateSession();
  for (int key : Range(kKeys)) {
    const auto op = table.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, key);
    table.AddInt32ColumnValue(req, kValueColumn, -key);
    table.AddStringColumnValue(req, kStringColumn, RandomHumanReadableString(1_KB));
    session->Apply(op);
  }
  ASSERT_OK(session->TEST_Flush());
  ASSERT_OK(cluster_->FlushTablets());
  uint64_t files_size = 0;
  for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
    files_size += peer->tablet()->GetCurrentVersionSstFilesUncompressedSize();
  }

  ASSERT_OK(client_->NewTableAlterer(kTable1Name)->DropColumn(kStringColumn)->Alter());

  ASSERT_OK(cluster_->CompactTablets());

  uint64_t new_files_size = 0;
  for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
    new_files_size += peer->tablet()->GetCurrentVersionSstFilesUncompressedSize();
  }

  LOG(INFO) << "Old files size: " << files_size << ", new files size: " << new_files_size;
  ASSERT_LE(new_files_size * 2, files_size);
}

TEST_F_EX(QLTabletTest, ShortPKCompactionTime, QLTabletRf1Test) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;

  ActivateCompactionTimeLogging(cluster_.get());

  constexpr int kKeys = RegularBuildVsDebugVsSanitizers(100, 10, 1) * 1000;
  constexpr int kFiles = 4;
  constexpr int kTabletFlushStep = kKeys / kFiles;

  YBSchemaBuilder builder;
  builder.AddColumn(kKeyColumn)->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
  builder.AddColumn(kValueColumn)->Type(DataType::INT32);
  TableHandle table;
  ASSERT_OK(table.Create(kTable1Name, 1, client_.get(), &builder));
  auto session = CreateSession();
  int next_tablet_flush = kTabletFlushStep;
  for (int key : Range(kKeys)) {
    const auto op = table.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, key);
    table.AddInt32ColumnValue(req, kValueColumn, -key);
    session->Apply(op);
    if ((key & 1023) == 0) {
      ASSERT_OK(session->TEST_Flush());
      if (key >= next_tablet_flush) {
        ASSERT_OK(cluster_->FlushTablets());
        next_tablet_flush = key + kTabletFlushStep;
      }
    }
  }
  ASSERT_OK(session->TEST_Flush());

  ASSERT_OK(cluster_->CompactTablets());
}

TEST_F_EX(QLTabletTest, CorruptData, QLTabletRf1Test) {
  // This test corrupts cluster by intention.
  DontVerifyClusterBeforeNextTearDown();

  constexpr auto kNumRows = 1000;

  CreateTable(kTable1Name, &table1_, /* num_tablets = */ 1, /* transactional = */ false);
  LOG(INFO) << "Created table";

  FillTable(0, kNumRows, table1_);
  LOG(INFO) << "Inserted " << kNumRows << " rows";

  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(cluster_->RestartSync());

  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
  for (const auto& peer : peers) {
    if (!peer->tablet()) {
      continue;
    }
    auto* db = peer->tablet()->regular_db();
    for (const auto& sst_file : db->GetLiveFilesMetaData()) {
      LOG(INFO) << "Found SST file: " << AsString(sst_file);
      const auto path_to_corrupt = sst_file.DataFilePath();
      LOG(INFO) << "Corrupting file: " << path_to_corrupt;
      ASSERT_OK(CorruptFile(
          path_to_corrupt, /* offset = */ -1024, /* bytes_to_corrupt = */ 512,
          CorruptionType::kXor55));
    }
  }

  auto session = CreateSession();
  const auto rows_count_result = CountRows(session, table1_);
  LOG(INFO) << "Rows count result: " << rows_count_result;
  ASSERT_FALSE(rows_count_result.ok());
}

// Make sure long reads are aborted by table drop.
TEST_F_EX(QLTabletTest, DeleteTableDuringLongRead, QLTabletRf1Test) {
  TestLongReadAbort([&]{
    return client_->DeleteTable(
        table1_.table()->id(), /* wait = */ true, /* txn = */ nullptr,
        CoarseMonoClock::now() + 1s * kTimeMultiplier);
  });
}

// Make sure long reads are aborted by table truncate.
TEST_F_EX(QLTabletTest, TruncateTableDuringLongRead, QLTabletRf1Test) {
  TestLongReadAbort([&]{
    return client_->TruncateTable(table1_.table()->id(), /* wait = */ true);
  });
}

class GetTabletKeyRangesTest : public QLTabletRf1TestToggleEnablePackedRow {
 protected:
  void SetUp() override {
    FLAGS_db_block_size_bytes = 4_KB;
    FLAGS_db_write_buffer_size = 200_KB;
    QLTabletRf1TestToggleEnablePackedRow::SetUp();
  }

  Result<TableHandle> CreateTestTable() {
    TableHandle table;

    YBSchemaBuilder builder;
    // TODO(get_table_key_ranges): also test for hash-based sharding as soon as it is properly
    // supported by GetTabletKeyRanges.
    builder.AddColumn(kKeyColumn)->Type(DataType::INT32)->PrimaryKey()->NotNull();
    builder.AddColumn(kValueColumn)->Type(DataType::STRING);
    RETURN_NOT_OK(table.Create(kTable1Name, /* num_tablets = */ 1, client_.get(), &builder));
    return table;
  }

  Result<tablet::TabletPtr> WriteData(
      const TableHandle& table, std::function<std::shared_ptr<YBqlWriteOp>()> write_op_generator) {
    std::atomic<size_t> num_rows_inserted{0};
    TestThreadHolder write_threads;

    LOG(INFO) << "Starting workload ...";
    Stopwatch s(Stopwatch::ALL_THREADS);
    s.start();

    for (auto t = 0; t < kNumWriteThreads; ++t) {
      write_threads.AddThreadFunctor([this, &stop_requested = write_threads.stop_flag(),
                                      &num_rows_inserted, &write_op_generator] {
        auto session = CreateSession();
        size_t num_ops_applied = 0;

        while (!stop_requested) {
          auto write_op = write_op_generator();
          session->Apply(write_op);
          if (++num_ops_applied == kWriteBatchSize) {
            const auto flush_status = session->TEST_FlushAndGetOpsErrors();
            ASSERT_OK(flush_status.status);
            ASSERT_EQ(flush_status.errors.size(), 0);
            num_rows_inserted.fetch_add(num_ops_applied);
            num_ops_applied = 0;
          }
        }
      });
    }

    const auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
    SCHECK_EQ(peers.size(), 1, InternalError, "Expected single tablet");
    const auto tablet = peers[0]->shared_tablet();
    SCHECK_NOTNULL(tablet);
    auto* db = tablet->regular_db();

    while (std::cmp_less(
               db->GetCurrentVersionSstFilesUncompressedSize(),
               kNumFlushes * FLAGS_db_write_buffer_size) ||
           db->GetCurrentVersionNumSSTFiles() < kNumSstFiles) {
      std::this_thread::sleep_for(100ms);
    }

    write_threads.Stop();
    s.stop();
    LOG(INFO) << "Workload stopped, it took: " << AsString(s.elapsed());

    LOG(INFO) << "Rows inserted: " << num_rows_inserted;
    LOG(INFO) << "Number of SST files: " << db->GetCurrentVersionNumSSTFiles();
    LOG(INFO) << "SST data size: " << db->GetCurrentVersionSstFilesUncompressedSize();
    return tablet;
  }

  static constexpr auto kValueSize = 16;
  static constexpr auto kWriteBatchSize = 128;
  static constexpr auto kNumWriteThreads = 2;

  static constexpr auto kNumSstFiles = 4;
  static constexpr auto kNumFlushes = 15;

  static constexpr auto kNumWorkers = 5;
  static constexpr auto kMinMaxKeysRatioLimit = 0.75;
};

INSTANTIATE_TEST_SUITE_P(, GetTabletKeyRangesTest, ::testing::Bool());

namespace {

Status CalcKeysDistributionAcrossWorkers(
    tablet::Tablet* tablet, std::vector<std::string> range_end_keys, const size_t num_workers,
    const double min_max_keys_ratio_limit, tablet::Direction direction) {
  std::vector<size_t> keys_per_worker(num_workers);

  auto iter = CreateRocksDBIterator(
      tablet->doc_db().regular, tablet->doc_db().key_bounds,
      docdb::BloomFilterOptions::Inactive(), rocksdb::kDefaultQueryId,
      /* file_filter = */ nullptr, /* iterate_upper_bound = */ nullptr,
      rocksdb::CacheRestartBlockKeys { direction == tablet::Direction::kBackward });
  if (direction == tablet::Direction::kForward) {
    iter.SeekToFirst();
  } else {
    iter.SeekToLast();
  }
  std::string current_range_start_key;

  auto range_key_iter = range_end_keys.begin();
  size_t current_range_idx = 0;
  size_t entries_in_current_range = 0;
  while (iter.Valid() || range_key_iter != range_end_keys.end()) {
    while (range_key_iter != range_end_keys.end() &&
           (!iter.Valid() || (!range_key_iter->empty() && (direction == tablet::Direction::kForward
                                                               ? iter.key() >= *range_key_iter
                                                               : iter.key() <= *range_key_iter)))) {
      LOG(INFO) << "Range #" << current_range_idx << "["
                << Slice(current_range_start_key).ToDebugHexString() << ", "
                << Slice(*range_key_iter).ToDebugHexString() << ") has " << entries_in_current_range
                << " rocksdb records";
      keys_per_worker[current_range_idx % num_workers] += entries_in_current_range;
      current_range_start_key = *range_key_iter;
      ++range_key_iter;
      ++current_range_idx;
      entries_in_current_range = 0;
    }
    if (iter.Valid()) {
      ++entries_in_current_range;
      if (direction == tablet::Direction::kForward) {
        iter.Next();
      } else {
        iter.Prev();
      }
    }
  }
  RETURN_NOT_OK(iter.status());

  LOG(INFO) << "Keys per worker: " << AsString(keys_per_worker);

  const auto [min_keys_per_worker, max_keys_per_worker] =
      std::minmax_element(keys_per_worker.begin(), keys_per_worker.end());
  const double ratio = 1.0 * *min_keys_per_worker / *max_keys_per_worker;

  SCHECK_GE(
      ratio, min_max_keys_ratio_limit, InternalError,
      Format(
          "Expected min/max keys per worker ratio to be not less than $0 but got $1, "
          "min_keys_per_worker: $2, max_keys_per_worker: $3",
          min_max_keys_ratio_limit, ratio, *min_keys_per_worker, *max_keys_per_worker));
  return Status::OK();
}

Status CheckRangeKeysOrder(
    const std::vector<std::string>& range_boundary_keys, tablet::Direction direction) {
  SCHECK_GE(
      range_boundary_keys.size(), size_t(1), InternalError, "We should have at least 1 range.");

  for (size_t i = 0; i + 2 < range_boundary_keys.size(); ++i) {
    switch (direction) {
      case tablet::Direction::kForward:
        SCHECK_LT(range_boundary_keys[i], range_boundary_keys[i + 1], InternalError, "Wrong order");
        continue;
      case tablet::Direction::kBackward:
        SCHECK_GT(range_boundary_keys[i], range_boundary_keys[i + 1], InternalError, "Wrong order");
        continue;
    }
    FATAL_INVALID_ENUM_VALUE(tablet::Direction, direction);
  }
  return Status::OK();
}

Status CheckBoundariesAreExcluded(
    const std::vector<std::string>& test_range_keys, tablet::Tablet* tablet,
    uint64_t max_num_ranges, uint64_t range_size_bytes, tablet::Direction direction,
    uint32_t max_key_length) {
  auto is_not_empty = [](const std::string& key) { return !key.empty(); };
  Slice first_key = test_range_keys.front();
  if (first_key.empty()) {
    SCHECK_EQ(
        test_range_keys.size(), 1, InternalError,
        "Shouldn't have empty first range key when it is not the only one.");
    return Status::OK();
  }
  Slice last_key =
      *std::find_if(test_range_keys.rbegin(), test_range_keys.rend(), is_not_empty);

  Slice lower_bound_key = std::min(first_key, last_key);
  Slice upper_bound_key = std::max(first_key, last_key);

  LOG(INFO) << "Check for boundaries to be exclusive, lower_bound_key: "
            << lower_bound_key.ToDebugHexString()
            << " upper_bound_key: " << upper_bound_key.ToDebugHexString();

  std::vector<std::string> range_keys;
  RETURN_NOT_OK(tablet->TEST_GetTabletKeyRanges(
      lower_bound_key, upper_bound_key, max_num_ranges, range_size_bytes, direction,
      max_key_length, [&range_keys](Slice key) {
        LOG(INFO) << "Got range boundary key: " << key.ToDebugHexString();
        range_keys.push_back(key.ToBuffer());
      }));

  RETURN_NOT_OK(CheckRangeKeysOrder(range_keys, direction));

  for (auto& key : range_keys) {
    if (key.empty()) {
      continue;
    }
    SCHECK_GT(key, lower_bound_key, InternalError, "Expected key to be greater than lower bound");
    SCHECK_LT(key, upper_bound_key, InternalError, "Expected key to be less than upper bound");
  }
  return Status::OK();
}

} // namespace

TEST_P(GetTabletKeyRangesTest, Distribution) {
  auto table = ASSERT_RESULT(CreateTestTable());

  auto tablet = ASSERT_RESULT(WriteData(table, [&table](){
    auto insert = table.NewInsertOp();
    auto req = insert->mutable_request();
    const auto key = RandomUniformInt<int32_t>();

    QLAddInt32RangeValue(req, key);
    table.AddStringColumnValue(req, kValueColumn, RandomString(kValueSize));
    return insert;
  }));
  auto* db = tablet->regular_db();

  NO_PENDING_FATALS();

  const auto range_size_bytes = db->GetCurrentVersionSstFilesUncompressedSize() / 50;

  for (auto direction : {tablet::Direction::kForward, tablet::Direction::kBackward}) {
    for (uint32_t max_key_length : {1024, 16, 8, 4, 2}) {
      LOG(INFO) << "max_key_length: " << max_key_length << " direction: " << AsString(direction);

      // List of range end/start keys depending on whether is_forward is true/false.
      std::vector<std::string> range_boundary_keys;
      auto add_range_boundary_key = [&range_boundary_keys](Slice key) {
        LOG(INFO) << "Got range boundary key: " << key.ToDebugHexString();
        range_boundary_keys.push_back(key.ToBuffer());
      };

      ASSERT_OK(tablet->TEST_GetTabletKeyRanges(
          Slice(), Slice(), std::numeric_limits<uint64_t>::max(), range_size_bytes,
          direction, max_key_length, add_range_boundary_key));

      LOG(INFO) << "Ranges count: " << range_boundary_keys.size();

      ASSERT_OK(CheckRangeKeysOrder(range_boundary_keys, direction));
      ASSERT_EQ(range_boundary_keys.back(), "");

      // TODO(get_table_key_ranges): For now we are returning full DocKeys and skipping ones that
      // are longer than max_key_length, because truncated DocKeys are not supported as lower/upper
      // bounds for scans. So, if DocKeys are longer than max_key_length we can have very uneven
      // distribution across ranges/workers.
      const auto min_max_keys_ratio_limit = max_key_length > 8 ? kMinMaxKeysRatioLimit : 0;

      ASSERT_OK(CalcKeysDistributionAcrossWorkers(
          tablet.get(), range_boundary_keys, kNumWorkers, min_max_keys_ratio_limit, direction));

      // Get ranges in multiple batches, verify it covers the whole space.
      constexpr auto kNumRangesPerBatch = 5;
      std::string start_key = "";

      std::vector<string> range_boundary_keys_from_batches;
      for (;;) {
        std::vector<string> range_boundary_keys_batch;
        LOG(INFO) << "Getting tablet key ranges starting from: "
                  << Slice(start_key).ToDebugHexString() << " direction: " << AsString(direction);

        {
          auto lower_bound_key = direction == tablet::Direction::kForward ? start_key : Slice();
          auto upper_bound_key = direction == tablet::Direction::kForward ? Slice() : start_key;

          ASSERT_OK(tablet->TEST_GetTabletKeyRanges(
              lower_bound_key, upper_bound_key, kNumRangesPerBatch,
              range_size_bytes, direction, max_key_length, [&range_boundary_keys_batch](Slice key) {
                LOG(INFO) << "Got range boundary key: " << key.ToDebugHexString();
                range_boundary_keys_batch.push_back(key.ToBuffer());
              }));

          ASSERT_LE(range_boundary_keys_batch.size(), kNumRangesPerBatch);

          // We should have at least 1 range.
          ASSERT_GE(range_boundary_keys_batch.size(), 1);

          for (const auto& key : range_boundary_keys_batch) {
            if (!key.empty()) {
              ASSERT_GT(key, lower_bound_key);
            }
            if (!upper_bound_key.empty()) {
              ASSERT_LT(key, upper_bound_key);
            }
            range_boundary_keys_from_batches.push_back(key);
          }
        }

        ASSERT_OK(CheckBoundariesAreExcluded(
            range_boundary_keys, tablet.get(), kNumRangesPerBatch, range_size_bytes, direction,
            max_key_length));

        if (range_boundary_keys_batch.back().empty()) {
          // We've reached the end.
          break;
        }

        ASSERT_EQ(range_boundary_keys_batch.size(), kNumRangesPerBatch);

        // Use last returned key as start key for next batch of ranges.
        start_key = range_boundary_keys_batch.back();
      }

      ASSERT_OK(CalcKeysDistributionAcrossWorkers(
          tablet.get(), range_boundary_keys_from_batches, kNumWorkers, min_max_keys_ratio_limit,
          direction));
    }
  }
}

TEST_P(GetTabletKeyRangesTest, Boundaries) {
  constexpr auto kNumDifferentKeys = 3;
  constexpr auto kMaxKeyLength = 1024;

  auto table = ASSERT_RESULT(CreateTestTable());

  std::atomic<int32_t> ops_generated{0};

  auto tablet = ASSERT_RESULT(WriteData(table, [&table, &ops_generated](){
    const auto ops_already_generated = ops_generated.fetch_add(1);
    const auto key = ops_already_generated % kNumDifferentKeys;

    auto insert = table.NewInsertOp();
    auto req = insert->mutable_request();

    QLAddInt32RangeValue(req, key);
    table.AddStringColumnValue(req, kValueColumn, RandomString(kValueSize));
    return insert;
  }));
  auto* db = tablet->regular_db();

  NO_PENDING_FATALS();

  const auto range_size_bytes = db->GetCurrentVersionSstFilesUncompressedSize() / 50;

  for (auto direction : {tablet::Direction::kForward, tablet::Direction::kBackward}) {
    LOG(INFO) << "direction: " << AsString(direction);

    std::vector<std::string> range_boundary_keys;
    auto add_range_boundary_key = [&range_boundary_keys](Slice key) {
      LOG(INFO) << "Got range boundary key: " << key.ToDebugHexString();
      range_boundary_keys.push_back(key.ToBuffer());
    };

    ASSERT_OK(tablet->TEST_GetTabletKeyRanges(
        Slice(), Slice(), std::numeric_limits<uint64_t>::max(), range_size_bytes, direction,
        kMaxKeyLength, add_range_boundary_key));

    LOG(INFO) << "Ranges count: " << range_boundary_keys.size();

    ASSERT_OK(CheckRangeKeysOrder(range_boundary_keys, direction));
    ASSERT_TRUE(range_boundary_keys.back().empty());
    ASSERT_GT(range_boundary_keys.size(), 2);

    ASSERT_OK(CheckBoundariesAreExcluded(
        range_boundary_keys, tablet.get(), std::numeric_limits<uint64_t>::max(), range_size_bytes,
        direction, kMaxKeyLength));
  }
}

} // namespace client
} // namespace yb
