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

#include "yb/client/ql-dml-test-base.h"
#include "yb/client/table_handle.h"

#include "yb/consensus/consensus.pb.h"

#include "yb/docdb/consensus_frontier.h"

#include "yb/integration-tests/test_workload.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"

#include "yb/tablet/tablet.h"

#include "yb/server/test_clock.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/random_util.h"

#include "yb/yql/cql/ql/util/statement_result.h"

using namespace std::literals; // NOLINT

DECLARE_uint64(initial_seqno);
DECLARE_int32(leader_lease_duration_ms);
DECLARE_int64(db_write_buffer_size);
DECLARE_bool(use_test_clock);

namespace yb {
namespace client {

using ql::RowsResult;

namespace {

const std::string kKey = "key"s;
const std::string kValue = "int_val"s;
const YBTableName kTable1Name("my_keyspace", "ql_client_test_table1");
const YBTableName kTable2Name("my_keyspace", "ql_client_test_table2");

int32_t ValueForKey(int32_t key) {
  return key * 2;
}

const int kTotalKeys = 250;
const int kBigSeqNo = 100500;

} // namespace

class QLTabletTest : public QLDmlTestBase {
 protected:
  void SetUp() override {
    FLAGS_use_test_clock = true;
    QLDmlTestBase::SetUp();
  }

  void CreateTables(uint64_t initial_seqno1, uint64_t initial_seqno2) {
    google::FlagSaver saver;
    FLAGS_initial_seqno = initial_seqno1;
    CreateTable(kTable1Name, &table1_);
    FLAGS_initial_seqno = initial_seqno2;
    CreateTable(kTable2Name, &table2_);
  }

  void SetValue(const YBSessionPtr& session, int32_t key, int32_t value, TableHandle* table) {
    const auto op = table->NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, key);
    table->AddInt32ColumnValue(req, kValue, value);
    ASSERT_OK(session->Apply(op));
    ASSERT_EQ(QLResponsePB::YQL_STATUS_OK, op->response().status());
  }

  boost::optional<int32_t> GetValue(const YBSessionPtr& session, int32_t key, TableHandle* table) {
    const auto op = CreateReadOp(key, table);
    EXPECT_OK(session->Apply(op));
    auto rowblock = RowsResult(op.get()).GetRowBlock();
    if (rowblock->row_count() == 0) {
      return boost::none;
    }
    EXPECT_EQ(1, rowblock->row_count());
    return rowblock->row(0).column(0).int32_value();
  }

  std::shared_ptr<YBqlReadOp> CreateReadOp(int32_t key, TableHandle* table) {
    auto op = table->NewReadOp();
    auto req = op->mutable_request();
    QLAddInt32HashValue(req, key);
    auto value_column_id = table->ColumnId(kValue);
    req->add_selected_exprs()->set_column_id(value_column_id);
    req->mutable_column_refs()->add_ids(value_column_id);

    QLRSColDescPB *rscol_desc = req->mutable_rsrow_desc()->add_rscol_descs();
    rscol_desc->set_name(kValue);
    table->ColumnType(kValue)->ToQLTypePB(rscol_desc->mutable_ql_type());
    return op;
  }

  void CreateTable(const YBTableName& table_name, TableHandle* table, int num_tablets = 0) {
    YBSchemaBuilder builder;
    builder.AddColumn(kKey)->Type(INT32)->HashPrimaryKey()->NotNull();
    builder.AddColumn(kValue)->Type(INT32);

    if (num_tablets == 0) {
      num_tablets = CalcNumTablets(3);
    }
    ASSERT_OK(table->Create(table_name, num_tablets, client_.get(), &builder));
  }

  YBSessionPtr CreateSession() {
    auto session = client_->NewSession();
    session->SetTimeout(15s);
    return session;
  }

  void FillTable(int begin, int end, TableHandle* table) {
    {
      auto session = CreateSession();
      for (int i = begin; i != end; ++i) {
        SetValue(session, i, ValueForKey(i), table);
      }
    }
    VerifyTable(begin, end, table);
    ASSERT_OK(WaitSync(begin, end, table));
  }

  void VerifyTable(int begin, int end, TableHandle* table) {
    auto session = CreateSession();
    for (int i = begin; i != end; ++i) {
      auto value = GetValue(session, i, table);
      ASSERT_TRUE(value.is_initialized()) << "i: " << i << ", table: " << table->name().ToString();
      ASSERT_EQ(ValueForKey(i), *value) << "i: " << i << ", table: " << table->name().ToString();
    }
  }

  CHECKED_STATUS WaitSync(int begin, int end, TableHandle* table) {
    auto deadline = MonoTime::Now() + MonoDelta::FromSeconds(30);

    master::GetTableLocationsRequestPB req;
    master::GetTableLocationsResponsePB resp;
    req.set_max_returned_locations(std::numeric_limits<uint32_t>::max());
    table->name().SetIntoTableIdentifierPB(req.mutable_table());
    RETURN_NOT_OK(
        cluster_->mini_master()->master()->catalog_manager()->GetTableLocations(&req, &resp));
    std::vector<master::TabletLocationsPB> tablets;
    std::unordered_set<std::string> replicas;
    for (const auto& tablet : resp.tablet_locations()) {
      tablets.emplace_back(tablet);
      for (const auto& replica : tablet.replicas()) {
        replicas.insert(replica.ts_info().permanent_uuid());
      }
    }
    for (const auto& replica : replicas) {
      RETURN_NOT_OK(DoWaitSync(deadline, tablets, replica, begin, end, table));
    }
    return Status::OK();
  }

  CHECKED_STATUS DoWaitSync(const MonoTime& deadline,
                            const std::vector<master::TabletLocationsPB>& tablets,
                            const std::string& replica,
                            int begin,
                            int end,
                            TableHandle* table) {
    auto tserver = cluster_->find_tablet_server(replica);
    if (!tserver) {
      return STATUS_FORMAT(NotFound, "Tablet server for $0 not found", replica);
    }
    auto endpoint = tserver->server()->rpc_server()->GetBoundAddresses().front();
    auto proxy = std::make_unique<tserver::TabletServerServiceProxy>(
        tserver->server()->messenger(), endpoint);

    auto condition = [&]() -> Result<bool> {
      // int total_rows = 0;
      for (int i = begin; i != end; ++i) {
        bool found = false;
        for (const auto& tablet : tablets) {
          tserver::ReadRequestPB req;
          {
            std::string partition_key;
            auto op = CreateReadOp(i, table);
            RETURN_NOT_OK(op->GetPartitionKey(&partition_key));
            auto* ql_batch = req.add_ql_batch();
            *ql_batch = op->request();
            const auto& hash_code = PartitionSchema::DecodeMultiColumnHashValue(partition_key);
            ql_batch->set_hash_code(hash_code);
            ql_batch->set_max_hash_code(hash_code);
          }

          tserver::ReadResponsePB resp;
          rpc::RpcController controller;
          controller.set_timeout(MonoDelta::FromSeconds(1));
          req.set_tablet_id(tablet.tablet_id());
          req.set_consistency_level(YBConsistencyLevel::CONSISTENT_PREFIX);
          proxy->Read(req, &resp, &controller);

          const auto& ql_batch = resp.ql_batch(0);
          if (ql_batch.status() != QLResponsePB_QLStatus_YQL_STATUS_OK) {
            return STATUS_FORMAT(RemoteError,
                                 "Bad resp status: $0",
                                 QLResponsePB_QLStatus_Name(ql_batch.status()));
          }
          std::shared_ptr<std::vector<ColumnSchema>>
            columns = std::make_shared<std::vector<ColumnSchema>>(table->schema().columns());
          Slice data;
          RETURN_NOT_OK(controller.GetSidecar(ql_batch.rows_data_sidecar(), &data));
          yb::ql::RowsResult result(table->name(), columns, data.ToBuffer());
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
          return STATUS_FORMAT(NotFound, "Key not found: $0", i);
        }
      }
      return true;
    };

    return Wait(condition, deadline, "Waiting for replication");
  }

  CHECKED_STATUS Import() {
    std::this_thread::sleep_for(1s); // Wait until all tablets a synced and flushed.
    EXPECT_OK(cluster_->FlushTablets());

    auto source_infos = GetTabletInfos(kTable1Name);
    auto dest_infos = GetTabletInfos(kTable2Name);
    EXPECT_EQ(source_infos.size(), dest_infos.size());
    for (size_t i = 0; i != source_infos.size(); ++i) {
      std::string start1, end1, start2, end2;
      {
        auto& metadata = source_infos[i]->metadata();
        std::shared_lock<std::remove_reference<decltype(metadata)>::type> lock(metadata);
        const auto& partition = metadata.state().pb.partition();
        start1 = partition.partition_key_start();
        end1 = partition.partition_key_end();
      }
      {
        auto& metadata = dest_infos[i]->metadata();
        std::shared_lock<std::remove_reference<decltype(metadata)>::type> lock(metadata);
        const auto& partition = metadata.state().pb.partition();
        start2 = partition.partition_key_start();
        end2 = partition.partition_key_end();
      }
      EXPECT_EQ(start1, start2);
      EXPECT_EQ(end1, end2);
    }
    for (int i = 0; i != cluster_->num_tablet_servers(); ++i) {
      auto* tablet_manager = cluster_->mini_tablet_server(i)->server()->tablet_manager();
      for (size_t j = 0; j != source_infos.size(); ++j) {
        tablet::TabletPeerPtr source_peer, dest_peer;
        tablet_manager->LookupTablet(source_infos[j]->id(), &source_peer);
        EXPECT_NE(nullptr, source_peer);
        auto source_dir = source_peer->tablet()->metadata()->rocksdb_dir();
        tablet_manager->LookupTablet(dest_infos[j]->id(), &dest_peer);
        EXPECT_NE(nullptr, dest_peer);
        auto status = dest_peer->tablet()->ImportData(source_dir);
        if (!status.ok() && !status.IsNotFound()) {
          return status;
        }
      }
    }
    return Status::OK();
  }

  scoped_refptr<master::TableInfo> GetTableInfo(const YBTableName& table_name) {
    auto* catalog_manager = cluster_->leader_mini_master()->master()->catalog_manager();
    std::vector<scoped_refptr<master::TableInfo>> all_tables;
    catalog_manager->GetAllTables(&all_tables);
    scoped_refptr<master::TableInfo> table_info;
    for (auto& table : all_tables) {
      if (table->name() == table_name.table_name()) {
        return table;
      }
    }
    return nullptr;
  }

  std::vector<scoped_refptr<master::TabletInfo>> GetTabletInfos(const YBTableName& table_name) {
    auto table_info = GetTableInfo(table_name);
    EXPECT_NE(nullptr, table_info);
    std::vector<scoped_refptr<master::TabletInfo>> tablets;
    table_info->GetAllTablets(&tablets);
    return tablets;
  }

  TableHandle table1_;
  TableHandle table2_;
};

TEST_F(QLTabletTest, ImportToEmpty) {
  CreateTables(0, kBigSeqNo);

  FillTable(0, kTotalKeys, &table1_);
  ASSERT_OK(Import());
  VerifyTable(0, kTotalKeys, &table1_);
  VerifyTable(0, kTotalKeys, &table2_);
}

TEST_F(QLTabletTest, ImportToNonEmpty) {
  CreateTables(0, kBigSeqNo);

  FillTable(0, kTotalKeys, &table1_);
  FillTable(kTotalKeys, 2 * kTotalKeys, &table2_);
  ASSERT_OK(Import());
  VerifyTable(0, 2 * kTotalKeys, &table2_);
}

TEST_F(QLTabletTest, ImportToEmptyAndRestart) {
  CreateTables(0, kBigSeqNo);

  FillTable(0, kTotalKeys, &table1_);
  ASSERT_OK(Import());
  VerifyTable(0, kTotalKeys, &table2_);

  ASSERT_OK(cluster_->RestartSync());
  VerifyTable(0, kTotalKeys, &table1_);
  VerifyTable(0, kTotalKeys, &table2_);
}

TEST_F(QLTabletTest, ImportToNonEmptyAndRestart) {
  CreateTables(0, kBigSeqNo);

  FillTable(0, kTotalKeys, &table1_);
  FillTable(kTotalKeys, 2 * kTotalKeys, &table2_);

  ASSERT_OK(Import());
  VerifyTable(0, 2 * kTotalKeys, &table2_);

  ASSERT_OK(cluster_->RestartSync());
  VerifyTable(0, kTotalKeys, &table1_);
  VerifyTable(0, 2 * kTotalKeys, &table2_);
}

TEST_F(QLTabletTest, LateImport) {
  CreateTables(kBigSeqNo, 0);

  FillTable(0, kTotalKeys, &table1_);
  ASSERT_NOK(Import());
}

TEST_F(QLTabletTest, OverlappedImport) {
  CreateTables(kBigSeqNo - 2, kBigSeqNo);

  FillTable(0, kTotalKeys, &table1_);
  FillTable(kTotalKeys, 2 * kTotalKeys, &table2_);
  ASSERT_NOK(Import());
}

void StepDownAllTablets(MiniCluster* cluster) {
  for (int i = 0; i != cluster->num_tablet_servers(); ++i) {
    std::vector<tablet::TabletPeerPtr> peers;
    cluster->mini_tablet_server(i)->server()->tablet_manager()->GetTabletPeers(&peers);
    for (const auto& peer : peers) {
      consensus::LeaderStepDownRequestPB req;
      req.set_tablet_id(peer->tablet_id());
      consensus::LeaderStepDownResponsePB resp;
      ASSERT_OK(peer->consensus()->StepDown(&req, &resp));
    }
  }
}

void DoStepDowns(MiniCluster* cluster) {
  for (int j = 0; j != 5; ++j) {
    StepDownAllTablets(cluster);
    std::this_thread::sleep_for(5s);
  }
}

void VerifyLogIndicies(MiniCluster* cluster) {
  for (int i = 0; i != cluster->num_tablet_servers(); ++i) {
    std::vector<tablet::TabletPeerPtr> peers;
    cluster->mini_tablet_server(i)->server()->tablet_manager()->GetTabletPeers(&peers);

    for (const auto& peer : peers) {
      consensus::OpId op_id;
      ASSERT_OK(peer->consensus()->GetLastOpId(consensus::OpIdType::COMMITTED_OPID, &op_id));
      int64_t index = -1;
      ASSERT_OK(peer->GetEarliestNeededLogIndex(&index));
      ASSERT_EQ(op_id.index(), index);
    }
  }
}

TEST_F(QLTabletTest, GCLogWithoutWrites) {
  TableHandle table;
  CreateTable(kTable1Name, &table);

  FillTable(0, kTotalKeys, &table);

  std::this_thread::sleep_for(5s);
  ASSERT_OK(cluster_->FlushTablets());
  DoStepDowns(cluster_.get());
  VerifyLogIndicies(cluster_.get());
}

TEST_F(QLTabletTest, GCLogWithRestartWithoutWrites) {
  TableHandle table;
  CreateTable(kTable1Name, &table);

  FillTable(0, kTotalKeys, &table);

  std::this_thread::sleep_for(5s);
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_OK(cluster_->RestartSync());

  DoStepDowns(cluster_.get());
  VerifyLogIndicies(cluster_.get());
}

TEST_F(QLTabletTest, LeaderLease) {
  google::FlagSaver saver;

  TableHandle table;
  CreateTable(kTable1Name, &table);

  LOG(INFO) << "Filling table";
  FillTable(0, kTotalKeys, &table);

  auto old_lease_ms = GetAtomicFlag(&FLAGS_leader_lease_duration_ms);
  SetAtomicFlag(60 * 1000, &FLAGS_leader_lease_duration_ms);
  // Wait for lease to sync
  std::this_thread::sleep_for(2ms * old_lease_ms);

  LOG(INFO) << "Step down";
  StepDownAllTablets(cluster_.get());

  LOG(INFO) << "Write value";
  auto session = CreateSession();
  const auto op = table.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
  auto* const req = op->mutable_request();
  QLAddInt32HashValue(req, 1);
  table.AddInt32ColumnValue(req, kValue, 1);
  auto status = session->Apply(op);
  ASSERT_TRUE(status.IsIOError()) << "Status: " << status;
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
  FLAGS_db_write_buffer_size = 10; // Use small memtable to induce background flush on each write.

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

  for (int i = 0; i != cluster_->num_tablet_servers(); ++i) {
    std::vector<tablet::TabletPeerPtr> tserver_peers;
    cluster_->mini_tablet_server(i)->server()->tablet_manager()->GetTabletPeers(&tserver_peers);
    ASSERT_EQ(tserver_peers.size(), 1);
    peers.push_back(tserver_peers.front());
  }

  bool leader_found = false;
  while (!leader_found) {
    for (size_t i = 0; i != peers.size(); ++i) {
      if (peers[i]->LeaderStatus() == consensus::Consensus::LeaderStatus::LEADER_AND_READY) {
        peers[(i + 1) % peers.size()]->log()->TEST_SetSleepDuration(500ms);
        leader_found = true;
        break;
      }
    }
  }

  auto deadline = std::chrono::steady_clock::now() + 20s;
  while (std::chrono::steady_clock::now() <= deadline) {
    for (const auto& peer : peers) {
      auto flushed_op_id = peer->tablet()->MaxPersistentOpId();
      ASSERT_OK(flushed_op_id);
      auto latest_entry_op_id = peer->log()->GetLatestEntryOpId();
      ASSERT_LE(flushed_op_id->index, latest_entry_op_id.index);
    }
  }

  for (const auto& peer : peers) {
    auto flushed_op_id = peer->tablet()->MaxPersistentOpId();
    ASSERT_OK(flushed_op_id);
    ASSERT_GE(flushed_op_id->index, 100);
  }

  workload.StopAndJoin();
}


TEST_F(QLTabletTest, BoundaryValues) {
  constexpr size_t kTotalThreads = 8;
  constexpr size_t kTotalRows = 10000;

  TableHandle table;
  CreateTable(kTable1Name, &table, 1);

  std::vector<std::thread> threads;
  std::atomic<int32_t> idx(0);
  for (size_t t = 0; t != kTotalThreads; ++t) {
    threads.emplace_back([this, &idx, &table] {
      auto session = CreateSession();
      for(;;) {
        int32_t i = idx++;
        if (i >= kTotalRows) {
          break;
        }

        SetValue(session, i, -i, &table);
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

  for (int i = 0; i != cluster_->num_tablet_servers(); ++i) {
    std::vector<tablet::TabletPeerPtr> peers;
    cluster_->mini_tablet_server(i)->server()->tablet_manager()->GetTabletPeers(&peers);
    ASSERT_EQ(1, peers.size());
    auto& peer = *peers[0];
    auto op_id = peer.log()->GetLatestEntryOpId();
    auto* db = peer.tablet()->TEST_db();
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

    // Allow several entries for non write ops
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
    auto value_column_id = table.ColumnId(kValue);
    req->add_selected_exprs()->set_column_id(value_column_id);
    req->mutable_column_refs()->add_ids(value_column_id);

    QLRSColDescPB *rscol_desc = req->mutable_rsrow_desc()->add_rscol_descs();
    rscol_desc->set_name(kValue);
    table.ColumnType(kValue)->ToQLTypePB(rscol_desc->mutable_ql_type());
    op->set_yb_consistency_level(YBConsistencyLevel::CONSISTENT_PREFIX);
    ASSERT_OK(session->Apply(op));
    ASSERT_EQ(QLResponsePB::YQL_STATUS_OK, op->response().status());
  }

  workload.StopAndJoin();

  cluster_->Shutdown(); // Need to shutdown cluster before resetting clock back.
  cluster_.reset();
}

} // namespace client
} // namespace yb
