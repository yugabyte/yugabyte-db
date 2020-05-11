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

#include "yb/client/client-test-util.h"
#include "yb/client/error.h"
#include "yb/client/ql-dml-test-base.h"
#include "yb/client/session.h"
#include "yb/client/transaction.h"
#include "yb/client/txn-test-base.h"

#include "yb/common/ql_expr.h"
#include "yb/common/ql_value.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus_util.h"

#include "yb/master/catalog_manager.h"

#include "yb/yql/cql/ql/util/statement_result.h"

#include "yb/rpc/messenger.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/protobuf_util.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_util.h"

using namespace std::literals;  // NOLINT

DECLARE_int64(db_write_buffer_size);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_bool(do_not_start_election_test_only);

namespace yb {

class TabletSplitITest : public client::TransactionTestBase,
                         public testing::WithParamInterface<IsolationLevel> {
 public:
  void SetUp() override {
    mini_cluster_opt_.num_tablet_servers = 3;
    create_table_ = false;
    SetIsolationLevel(GetParam());
    client::TransactionTestBase::SetUp();
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(client_->messenger());
  }

  // Creates read request for tablet_id which reflects following query (see
  // client::KeyValueTableTest for schema and kXxx constants):
  // SELECT `kValueColumn` FROM `kTableName` WHERE `kKeyColumn` = `key`;
  // Uses YBConsistencyLevel::CONSISTENT_PREFIX as this is default for YQL clients.
  Result<tserver::ReadRequestPB> CreateReadRequest(const TabletId& tablet_id, int32_t key);

  // Creates write request for tablet_id which reflects following query (see
  // client::KeyValueTableTest for schema and kXxx constants):
  // INSERT INTO `kTableName`(`kValueColumn`) VALUES (`value`);
  tserver::WriteRequestPB CreateInsertRequest(
      const TabletId& tablet_id, int32_t key, int32_t value);

  // Writes `num_rows` rows into test table using `CreateInsertRequest`.
  // Returns a pair with min and max hash code written.
  Result<std::pair<docdb::DocKeyHash, docdb::DocKeyHash>> WriteRows(size_t num_rows);

  Result<scoped_refptr<master::TabletInfo>> GetSingleTestTabletInfo(
      const master::Master& leader_master);

  CHECKED_STATUS WaitForTabletSplitCompletion();

  // Checks all tablet replicas expect ones which have been split to have all rows from 1 to
  // `num_rows` and nothing else.
  void CheckPostSplitTabletReplicasData(size_t num_rows);

  // Checks source tablet behaviour after split:
  // - It should reject reads and writes.
  void CheckSourceTabletAfterSplit(const TabletId& source_tablet_id);

 protected:
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
};

namespace {

static constexpr auto kRpcTimeout = 15s * kTimeMultiplier;

} // namespace

Result<tserver::ReadRequestPB> TabletSplitITest::CreateReadRequest(
    const TabletId& tablet_id, int32_t key) {
  tserver::ReadRequestPB req;
  auto op = client::CreateReadOp(key, table_, kValueColumn);
  auto* ql_batch = req.add_ql_batch();
  *ql_batch = op->request();

  std::string partition_key;
  RETURN_NOT_OK(op->GetPartitionKey(&partition_key));
  const auto& hash_code = PartitionSchema::DecodeMultiColumnHashValue(partition_key);
  ql_batch->set_hash_code(hash_code);
  ql_batch->set_max_hash_code(hash_code);
  req.set_tablet_id(tablet_id);
  req.set_consistency_level(YBConsistencyLevel::CONSISTENT_PREFIX);
  return req;
}

tserver::WriteRequestPB TabletSplitITest::CreateInsertRequest(
    const TabletId& tablet_id, int32_t key, int32_t value) {
  tserver::WriteRequestPB req;
  auto op = table_.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);

  {
    auto op_req = op->mutable_request();
    QLAddInt32HashValue(op_req, key);
    table_.AddInt32ColumnValue(op_req, kValueColumn, value);
  }

  auto* ql_batch = req.add_ql_write_batch();
  *ql_batch = op->request();

  std::string partition_key;
  EXPECT_OK(op->GetPartitionKey(&partition_key));
  const auto& hash_code = PartitionSchema::DecodeMultiColumnHashValue(partition_key);
  ql_batch->set_hash_code(hash_code);
  req.set_tablet_id(tablet_id);
  return req;
}

Result<std::pair<docdb::DocKeyHash, docdb::DocKeyHash>> TabletSplitITest::WriteRows(
    size_t num_rows) {
  auto min_hash_code = std::numeric_limits<docdb::DocKeyHash>::max();
  auto max_hash_code = std::numeric_limits<docdb::DocKeyHash>::min();

  LOG(INFO) << "Writing data...";

  auto txn = CreateTransaction();
  auto session = CreateSession();
  for (auto i = 1; i <= num_rows; ++i) {
    client::YBqlWriteOpPtr op =
        VERIFY_RESULT(WriteRow(session, i /* key */, i /* value */, client::WriteOpType::INSERT));
    const auto hash_code = op->GetHashCode();
    min_hash_code = std::min(min_hash_code, hash_code);
    max_hash_code = std::max(max_hash_code, hash_code);
  }
  if (txn) {
    RETURN_NOT_OK(txn->CommitFuture().get());
    LOG(INFO) << "Committed: " << txn->id();
  }

  LOG(INFO) << "Data has been written";
  LOG(INFO) << "min_hash_code = " << min_hash_code;
  LOG(INFO) << "max_hash_code = " << max_hash_code;
  return std::make_pair(min_hash_code, max_hash_code);
}

Status TabletSplitITest::WaitForTabletSplitCompletion() {
  LOG(INFO) << "Waiting for tablet split to be completed...";
  return WaitFor([this] {
      auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
      size_t num_peers_running = 0;
      size_t num_peers_split = 0;
      size_t num_peers_leader_ready = 0;
      for (const auto& peer : peers) {
        if (peer->tablet()->table_type() == TRANSACTION_STATUS_TABLE_TYPE) {
          continue;
        }
        if (!peer->tablet()) {
          VLOG(1) << consensus::MakeTabletLogPrefix(peer->tablet_id(), peer->permanent_uuid())
                  << "no tablet";
          break;
        }
        const auto raft_group_state = peer->state();
        const auto tablet_data_state = peer->tablet()->metadata()->tablet_data_state();
        const auto leader_status = peer->consensus()->GetLeaderStatus();
        VLOG(1) << consensus::MakeTabletLogPrefix(peer->tablet_id(), peer->permanent_uuid())
                << "raft_group_state: " << AsString(raft_group_state)
                << " tablet_data_state: " << AsString(tablet_data_state)
                << " leader status: " << AsString(leader_status);
        if (raft_group_state == tablet::RaftGroupStatePB::RUNNING) {
          ++num_peers_running;
        } else {
          return false;
        }
        num_peers_leader_ready += leader_status == consensus::LeaderStatus::LEADER_AND_READY;
        num_peers_split +=
            tablet_data_state == tablet::TabletDataState::TABLET_DATA_SPLIT_COMPLETED;
      }
      VLOG(1) << "num_peers_running: " << num_peers_running;
      VLOG(1) << "num_peers_split: " << num_peers_split;
      VLOG(1) << "num_peers_leader_ready: " << num_peers_leader_ready;
      const auto replication_factor = cluster_->num_tablet_servers();
      // We expect 3 tablets: 1 original tablet + 2 new after-split tablets.
      const auto expected_num_tablets = 3;
      return num_peers_running == replication_factor * expected_num_tablets &&
             num_peers_split == replication_factor &&
             num_peers_leader_ready == expected_num_tablets;
    }, 10s * kTimeMultiplier, "Wait for tablet split to be completed");
}

void TabletSplitITest::CheckPostSplitTabletReplicasData(size_t num_rows) {
  LOG(INFO) << "Checking post-split tablet replicas data...";
  const auto replication_factor = cluster_->num_tablet_servers();

  std::vector<size_t> keys(num_rows, replication_factor);
  const auto key_column_id = table_.ColumnId(kKeyColumn);
  const auto value_column_id = table_.ColumnId(kValueColumn);
  for (auto peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
    const auto* tablet = peer->tablet();
    if (tablet->table_type() == TRANSACTION_STATUS_TABLE_TYPE ||
        tablet->metadata()->tablet_data_state() ==
            tablet::TabletDataState::TABLET_DATA_SPLIT_COMPLETED) {
      continue;
    }

    const SchemaPtr schema = tablet->metadata()->schema();
    auto client_schema = schema->CopyWithoutColumnIds();
    auto iter = ASSERT_RESULT(tablet->NewRowIterator(client_schema, boost::none));
    QLTableRow row;
    std::unordered_set<size_t> tablet_keys;
    while (ASSERT_RESULT(iter->HasNext())) {
      ASSERT_OK(iter->NextRow(&row));
      auto key_opt = row.GetValue(key_column_id);
      ASSERT_TRUE(key_opt.is_initialized());
      ASSERT_EQ(key_opt, row.GetValue(value_column_id));
      auto key = key_opt->int32_value();
      ASSERT_TRUE(tablet_keys.insert(key).second)
          << "Duplicate key " << key << " in tablet " << tablet->tablet_id();
      ASSERT_GT(keys[key - 1]--, 0)
          << "Extra key " << key << " in tablet " << tablet->tablet_id();
    }
  }
  for (auto key = 1; key <= num_rows; ++key) {
    ASSERT_EQ(keys[key - 1], 0) << "Missing key: " << key;
  }
}

void TabletSplitITest::CheckSourceTabletAfterSplit(const TabletId& source_tablet_id) {
  LOG(INFO) << "Checking source tablet behavior after split...";
  google::FlagSaver saver;
  FLAGS_do_not_start_election_test_only = true;

  size_t tablet_split_insert_error_count = 0;
  size_t not_the_leader_insert_error_count = 0;
  for (auto mini_ts : cluster_->mini_tablet_servers()) {
    auto ts_service_proxy = std::make_unique<tserver::TabletServerServiceProxy>(
        proxy_cache_.get(), HostPort::FromBoundEndpoint(mini_ts->bound_rpc_addr()));

    {
      tserver::ReadRequestPB req = ASSERT_RESULT(CreateReadRequest(source_tablet_id, 1 /* key */));

      rpc::RpcController controller;
      controller.set_timeout(kRpcTimeout);
      tserver::ReadResponsePB resp;
      ts_service_proxy->Read(req, &resp, &controller);

      ASSERT_TRUE(resp.has_error());
      ASSERT_EQ(resp.error().code(), tserver::TabletServerErrorPB::TABLET_SPLIT);
    }

    {
      tserver::WriteRequestPB req =
          CreateInsertRequest(source_tablet_id, 0 /* key */, 0 /* value */);

      rpc::RpcController controller;
      controller.set_timeout(kRpcTimeout);
      tserver::WriteResponsePB resp;
      ts_service_proxy->Write(req, &resp, &controller);

      ASSERT_TRUE(resp.has_error());
      LOG(INFO) << "Error: " << AsString(resp.error());
      switch (resp.error().code()) {
        case tserver::TabletServerErrorPB::UNKNOWN_ERROR:
          ASSERT_EQ(resp.error().status().code(), AppStatusPB::TRY_AGAIN_CODE);
          tablet_split_insert_error_count++;
          break;
        case tserver::TabletServerErrorPB::NOT_THE_LEADER:
          not_the_leader_insert_error_count++;
          break;
        default:
          FAIL() << "Unexpected error: " << AsString(resp.error());
      }
    }
  }
  // Leader should return "try again" error on insert.
  ASSERT_EQ(tablet_split_insert_error_count, 1);
  // Followers should return "not the leader" error.
  ASSERT_EQ(not_the_leader_insert_error_count, cluster_->num_tablet_servers() - 1);
}

Result<scoped_refptr<master::TabletInfo>> TabletSplitITest::GetSingleTestTabletInfo(
    const master::Master& leader_master) {
  std::vector<scoped_refptr<master::TabletInfo>> tablet_infos;
  leader_master.catalog_manager()->GetTableInfo(table_->id())->GetAllTablets(&tablet_infos);

  SCHECK_EQ(tablet_infos.size(), 1, IllegalState, "Expect test table to have only 1 tablet");
  return tablet_infos.front();
}

namespace {

Result<size_t> SelectRowsCount(
    const client::YBSessionPtr& session, const client::TableHandle& table) {
  LOG(INFO) << "Running full scan on test table...";
  session->SetTimeout(5s);
  QLPagingStatePB paging_state;
  size_t row_count = 0;
  for (;;) {
    const auto op = table.NewReadOp();
    auto* const req = op->mutable_request();
    req->set_return_paging_state(true);
    if (paging_state.has_table_id()) {
      if (paging_state.has_read_time()) {
        ReadHybridTime read_time = ReadHybridTime::FromPB(paging_state.read_time());
        if (read_time) {
          session->SetReadPoint(read_time);
        }
      }
      session->SetForceConsistentRead(client::ForceConsistentRead::kTrue);
      *req->mutable_paging_state() = std::move(paging_state);
    }
    Status s;
    RETURN_NOT_OK(WaitFor([&] {
      s = session->ApplyAndFlush(op);
      if (s.ok()) {
        return true;
      }
      for (auto& error : session->GetPendingErrors()) {
        if (error->status().IsTryAgain()) {
          return false;
        }
      }
      return true;
    }, 15s, "Waiting for session flush"));
    RETURN_NOT_OK(s);
    auto rowblock = ql::RowsResult(op.get()).GetRowBlock();
    row_count += rowblock->row_count();
    if (!op->response().has_paging_state()) {
      break;
    }
    paging_state = op->response().paging_state();
  }
  return row_count;
}

} // namespace

// Tests splitting of the single tablet in following steps:
// - Create single-tablet table and populates it with specified number of rows.
// - Do full scan using `select count(*)`.
// - Send SplitTablet RPC to the tablet leader.
// - After tablet split is completed - check that new tablets have exactly the same rows.
// - Check that source tablet is rejecting reads and writes.
// - Do full scan using `select count(*)`.
// - Restart cluster.
// - ClusterVerifier will check cluster integrity at the end of the test.

TEST_P(TabletSplitITest, SplitSingleTablet) {
  constexpr auto kNumRows = 500;

  SetNumTablets(1);
  CreateTable();

  // TODO(tsplit): add delay of applying part of intents after tablet is split.
  // TODO(tsplit): test split during read/write workload.
  // TODO(tsplit): test split during long-running transactions.

  auto min_max_hash_code = ASSERT_RESULT(WriteRows(kNumRows));

  auto rows_count = ASSERT_RESULT(SelectRowsCount(NewSession(), table_));
  ASSERT_EQ(rows_count, kNumRows);

  const auto split_hash_code = (min_max_hash_code.first + min_max_hash_code.second) / 2;
  LOG(INFO) << "Split hash code: " << split_hash_code;

  auto& leader_master = *ASSERT_NOTNULL(cluster_->leader_mini_master()->master());

  auto source_tablet_info = ASSERT_RESULT(GetSingleTestTabletInfo(leader_master));
  const auto source_tablet_id = source_tablet_info->id();

  auto* catalog_mgr = leader_master.catalog_manager();

  ASSERT_OK(catalog_mgr->TEST_SplitTablet(source_tablet_info, split_hash_code));

  ASSERT_OK(WaitForTabletSplitCompletion());

  ASSERT_NO_FATALS(CheckPostSplitTabletReplicasData(kNumRows));

  ASSERT_NO_FATALS(CheckSourceTabletAfterSplit(source_tablet_id));

  master::GetTableLocationsResponsePB resp;
  master::GetTableLocationsRequestPB req;
  client::kTableName.SetIntoTableIdentifierPB(req.mutable_table());
  req.set_max_returned_locations(std::numeric_limits<int32_t>::max());
  ASSERT_OK(catalog_mgr->GetTableLocations(&req, &resp));
  LOG(INFO) << "Table locations:";
  for (auto& tablet : resp.tablet_locations()) {
    LOG(INFO) << "Tablet: " << tablet.tablet_id()
              << " partition: " << tablet.partition().ShortDebugString();
  }

  rows_count = ASSERT_RESULT(SelectRowsCount(NewSession(), table_));
  ASSERT_EQ(rows_count, kNumRows);

  // TODO(tsplit): remove client recreation after required support for post-split tablets is
  // implemented.
  ASSERT_OK(CreateClient());
  ASSERT_OK(WriteRows(kNumRows));

  ASSERT_OK(cluster_->RestartSync());
}

namespace {

PB_ENUM_FORMATTERS(IsolationLevel);

std::string TestParamToString(const testing::TestParamInfo<IsolationLevel>& isolation_level) {
  return ToString(isolation_level.param);
}

} // namespace

INSTANTIATE_TEST_CASE_P(
    IsolationLevel,
    TabletSplitITest,
    ::testing::ValuesIn(GetAllPbEnumValues<IsolationLevel>()),
    TestParamToString);

}  // namespace yb
