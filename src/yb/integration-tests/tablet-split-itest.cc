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

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

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

#include "yb/docdb/doc_key.h"
#include "yb/docdb/docdb_rocksdb_util.h"

#include "yb/gutil/dynamic_annotations.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/redis_table_test_base.h"
#include "yb/integration-tests/test_workload.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.pb.h"
#include "yb/master/master_error.h"

#include "yb/tablet/tablet_fwd.h"
#include "yb/tserver/tserver_admin.pb.h"
#include "yb/util/atomic.h"
#include "yb/util/status.h"
#include "yb/util/tsan_util.h"
#include "yb/yql/cql/ql/util/statement_result.h"

#include "yb/rpc/messenger.h"

#include "yb/tablet/tablet_metadata.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/protobuf_util.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"

using namespace std::literals;  // NOLINT

DECLARE_int64(db_block_size_bytes);
DECLARE_int64(db_write_buffer_size);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_bool(rocksdb_disable_compactions);
DECLARE_bool(TEST_do_not_start_election_test_only);
DECLARE_int32(TEST_apply_tablet_split_inject_delay_ms);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_int32(leader_lease_duration_ms);
DECLARE_int32(raft_heartbeat_interval_ms);
DECLARE_double(leader_failure_max_missed_heartbeat_periods);
DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_bool(TEST_skip_deleting_split_tablets);
DECLARE_int32(replication_factor);
DECLARE_int32(tablet_split_limit_per_table);
DECLARE_bool(TEST_pause_before_post_split_compation);
DECLARE_int32(TEST_slowdown_backfill_alter_table_rpcs_ms);
DECLARE_int32(rocksdb_base_background_compactions);
DECLARE_int32(rocksdb_max_background_compactions);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_int64(tablet_split_low_phase_shard_count_per_node);
DECLARE_int64(tablet_split_high_phase_shard_count_per_node);
DECLARE_int64(tablet_split_low_phase_size_threshold_bytes);
DECLARE_int64(tablet_split_high_phase_size_threshold_bytes);
DECLARE_int64(tablet_force_split_threshold_bytes);
DECLARE_bool(TEST_disable_split_tablet_candidate_processing);
DECLARE_int32(process_split_tablet_candidates_interval_msec);
DECLARE_bool(TEST_disable_cleanup_split_tablet);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_bool(TEST_validate_all_tablet_candidates);
DECLARE_bool(TEST_select_all_tablets_for_split);

namespace yb {

namespace {

Result<size_t> SelectRowsCount(
    const client::YBSessionPtr& session, const client::TableHandle& table) {
  LOG(INFO) << "Running full scan on test table...";
  session->SetTimeout(5s * kTimeMultiplier);
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
    RETURN_NOT_OK(session->ApplyAndFlush(op));
    auto rowblock = ql::RowsResult(op.get()).GetRowBlock();
    row_count += rowblock->row_count();
    if (!op->response().has_paging_state()) {
      break;
    }
    paging_state = op->response().paging_state();
  }
  return row_count;
}

void DumpTableLocations(
    master::CatalogManager* catalog_mgr, const client::YBTableName& table_name) {
  master::GetTableLocationsResponsePB resp;
  master::GetTableLocationsRequestPB req;
  table_name.SetIntoTableIdentifierPB(req.mutable_table());
  req.set_max_returned_locations(std::numeric_limits<int32_t>::max());
  ASSERT_OK(catalog_mgr->GetTableLocations(&req, &resp));
  LOG(INFO) << "Table locations:";
  for (auto& tablet : resp.tablet_locations()) {
    LOG(INFO) << "Tablet: " << tablet.tablet_id()
              << " partition: " << tablet.partition().ShortDebugString();
  }
}

} // namespace

template <class MiniClusterType>
class TabletSplitITestBase : public client::TransactionTestBase<MiniClusterType> {
 public:
  void SetUp() override {
    this->SetNumTablets(3);
    this->create_table_ = false;
    this->mini_cluster_opt_.num_tablet_servers = GetRF();
    client::TransactionTestBase<MiniClusterType>::SetUp();
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(this->client_->messenger());
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
  Result<std::pair<docdb::DocKeyHash, docdb::DocKeyHash>> WriteRows(
      size_t num_rows = 2000, size_t start_key = 1);

  Result<docdb::DocKeyHash> WriteRowsAndGetMiddleHashCode(size_t num_rows) {
    auto min_max_hash_code = VERIFY_RESULT(WriteRows(num_rows, 1));
    const auto split_hash_code = (min_max_hash_code.first + min_max_hash_code.second) / 2;
    LOG(INFO) << "Split hash code: " << split_hash_code;

    RETURN_NOT_OK(CheckRowsCount(num_rows));

    return split_hash_code;
  }

  Result<scoped_refptr<master::TabletInfo>> GetSingleTestTabletInfo(
      master::CatalogManager* catalog_manager);

  void CreateSingleTablet() {
    this->SetNumTablets(1);
    this->CreateTable();
  }

  CHECKED_STATUS CheckRowsCount(size_t expected_num_rows) {
    auto rows_count = VERIFY_RESULT(SelectRowsCount(this->NewSession(), this->table_));
    SCHECK_EQ(rows_count, expected_num_rows, InternalError, "Got unexpected rows count");
    return Status::OK();
  }

  Result<TableId> GetTestTableId() {
    return VERIFY_RESULT(this->client_->GetYBTableInfo(client::kTableName)).table_id;
  }

  // Wait for all peers to complete post-split compaction.
  void WaitForTestTableTabletsCompactionFinish(MonoDelta timeout);

  // Returns all tablet peers in the cluster which are marked as being in
  // TABLET_DATA_SPLIT_COMPLETED state. In most of the test cases below, this corresponds to the
  // post-split parent/source tablet peers.
  Result<std::vector<tablet::TabletPeerPtr>> ListSplitCompleteTabletPeers();

  // Returns all tablet peers in the cluster which are not part of a transaction table and which are
  // not in TABLET_DATA_SPLIT_COMPLETED state. In most of the test cases below, this corresponds to
  // post-split children tablet peers.
  Result<std::vector<tablet::TabletPeerPtr>> ListPostSplitChildrenTabletPeers();

  // Verify that the db has queued, is running, or did complete a post split compaction.
  void VerifyTriggeredPostSplitCompaction(int num_peers);

  // Returns the bytes read at the RocksDB layer by each split child tablet.
  Result<uint64_t> GetActiveTabletsBytesRead();

  // Returns the bytes written at the RocksDB layer by the split parent tablet.
  Result<uint64_t> GetInactiveTabletsBytesWritten();

  // Checks active tablet replicas (all expect ones that have been split) to have all rows from 1 to
  // `num_rows` and nothing else.
  // If num_replicas_online is 0, uses replication factor.
  CHECKED_STATUS CheckPostSplitTabletReplicasData(
      size_t num_rows, size_t num_replicas_online = 0, size_t num_active_tablets = 2);

  // Make sure table contains only keys 1...num_keys without gaps.
  void CheckTableKeysInRange(const size_t num_keys);

 protected:
  virtual int64_t GetRF() { return 3; }

  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
};

class TabletSplitITest : public TabletSplitITestBase<MiniCluster> {
 public:
  void SetUp() override {
    FLAGS_cleanup_split_tablets_interval_sec = 1;
    FLAGS_enable_automatic_tablet_splitting = false;
    FLAGS_TEST_validate_all_tablet_candidates = true;
    FLAGS_TEST_select_all_tablets_for_split = true;
    TabletSplitITestBase<MiniCluster>::SetUp();
  }

  Result<TabletId> CreateSingleTabletAndSplit(size_t num_rows) {
    CreateSingleTablet();
    const auto split_hash_code = VERIFY_RESULT(WriteRowsAndGetMiddleHashCode(num_rows));
    return SplitTabletAndValidate(split_hash_code, num_rows);
  }

  master::CatalogManager* catalog_manager() {
    return CHECK_NOTNULL(this->cluster_->leader_mini_master()->master())->catalog_manager();
  }

  // By default we wait until all split tablets are cleanup. expected_split_tablets could be
  // overridden if needed to test behaviour of split tablet when its deletion is disabled.
  // If num_replicas_online is 0, uses replication factor.
  void WaitForTabletSplitCompletion(
      const size_t expected_non_split_tablets, const size_t expected_split_tablets = 0,
      size_t num_replicas_online = 0, const client::YBTableName& table = client::kTableName);

  Result<TabletId> SplitSingleTablet(docdb::DocKeyHash split_hash_code) {
    auto* catalog_mgr = catalog_manager();

    auto source_tablet_info = VERIFY_RESULT(GetSingleTestTabletInfo(catalog_mgr));
    const auto source_tablet_id = source_tablet_info->id();

    RETURN_NOT_OK(catalog_mgr->TEST_SplitTablet(source_tablet_info, split_hash_code));
    return source_tablet_id;
  }

  Result<TabletId> SplitTabletAndValidate(docdb::DocKeyHash split_hash_code, size_t num_rows) {
    auto source_tablet_id = VERIFY_RESULT(SplitSingleTablet(split_hash_code));

    const auto expected_split_tablets = FLAGS_TEST_skip_deleting_split_tablets ? 1 : 0;

    WaitForTabletSplitCompletion(/* expected_non_split_tablets =*/ 2, expected_split_tablets);

    RETURN_NOT_OK(CheckPostSplitTabletReplicasData(num_rows));

    if (expected_split_tablets > 0) {
      RETURN_NOT_OK(CheckSourceTabletAfterSplit(source_tablet_id));
    }

    return source_tablet_id;
  }

  // Checks source tablet behaviour after split:
  // - It should reject reads and writes.
  CHECKED_STATUS CheckSourceTabletAfterSplit(const TabletId& source_tablet_id);

  // Tests appropriate client requests structure update at YBClient side.
  // split_depth specifies how deep should we split original tablet until trying to write again.
  void SplitClientRequestsIds(int split_depth);
};

class TabletSplitITestWithIsolationLevel : public TabletSplitITest,
                                           public testing::WithParamInterface<IsolationLevel> {
 public:
  void SetUp() override {
    SetIsolationLevel(GetParam());
    TabletSplitITest::SetUp();
  }
};

namespace {

static constexpr auto kRpcTimeout = 60s * kTimeMultiplier;

} // namespace

template <class MiniClusterType>
Result<tserver::ReadRequestPB> TabletSplitITestBase<MiniClusterType>::CreateReadRequest(
    const TabletId& tablet_id, int32_t key) {
  tserver::ReadRequestPB req;
  auto op = client::CreateReadOp(key, this->table_, this->kValueColumn);
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

template <class MiniClusterType>
tserver::WriteRequestPB TabletSplitITestBase<MiniClusterType>::CreateInsertRequest(
    const TabletId& tablet_id, int32_t key, int32_t value) {
  tserver::WriteRequestPB req;
  auto op = this->table_.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);

  {
    auto op_req = op->mutable_request();
    QLAddInt32HashValue(op_req, key);
    this->table_.AddInt32ColumnValue(op_req, this->kValueColumn, value);
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

template <class MiniClusterType>
Result<std::pair<docdb::DocKeyHash, docdb::DocKeyHash>>
    TabletSplitITestBase<MiniClusterType>::WriteRows(
        const size_t num_rows, const size_t start_key) {
  auto min_hash_code = std::numeric_limits<docdb::DocKeyHash>::max();
  auto max_hash_code = std::numeric_limits<docdb::DocKeyHash>::min();

  LOG(INFO) << "Writing " << num_rows << " rows...";

  auto txn = this->CreateTransaction();
  auto session = this->CreateSession();
  for (auto i = start_key; i < start_key + num_rows; ++i) {
    client::YBqlWriteOpPtr op = VERIFY_RESULT(
        this->WriteRow(session, i /* key */, i /* value */, client::WriteOpType::INSERT));
    const auto hash_code = op->GetHashCode();
    min_hash_code = std::min(min_hash_code, hash_code);
    max_hash_code = std::max(max_hash_code, hash_code);
    YB_LOG_EVERY_N_SECS(INFO, 10) << "Rows written: " << start_key << "..." << i;
  }
  if (txn) {
    RETURN_NOT_OK(txn->CommitFuture().get());
    LOG(INFO) << "Committed: " << txn->id();
  }

  LOG(INFO) << num_rows << " rows has been written";
  LOG(INFO) << "min_hash_code = " << min_hash_code;
  LOG(INFO) << "max_hash_code = " << max_hash_code;
  return std::make_pair(min_hash_code, max_hash_code);
}

void TabletSplitITest::WaitForTabletSplitCompletion(
    const size_t expected_non_split_tablets,
    const size_t expected_split_tablets,
    size_t num_replicas_online,
    const client::YBTableName& table) {
  if (num_replicas_online == 0) {
    num_replicas_online = FLAGS_replication_factor;
  }

  LOG(INFO) << "Waiting for tablet split to be completed... ";
  LOG(INFO) << "expected_non_split_tablets: " << expected_non_split_tablets;
  LOG(INFO) << "expected_split_tablets: " << expected_split_tablets;

  const auto expected_total_tablets = expected_non_split_tablets + expected_split_tablets;
  LOG(INFO) << "expected_total_tablets: " << expected_total_tablets;

  std::vector<tablet::TabletPeerPtr> peers;
  auto s = WaitFor([&] {
      peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
      size_t num_peers_running = 0;
      size_t num_peers_split = 0;
      size_t num_peers_leader_ready = 0;
      for (const auto& peer : peers) {
        const auto tablet = peer->shared_tablet();
        const auto consensus = peer->shared_consensus();
        if (!tablet || !consensus) {
          break;
        }
        if (tablet->metadata()->table_name() != table.table_name() ||
            tablet->table_type() == TRANSACTION_STATUS_TABLE_TYPE) {
          continue;
        }
        const auto raft_group_state = peer->state();
        const auto tablet_data_state = tablet->metadata()->tablet_data_state();
        const auto leader_status = consensus->GetLeaderStatus(/* allow_stale =*/true);
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

    return num_peers_running == num_replicas_online * expected_total_tablets &&
           num_peers_split == num_replicas_online * expected_split_tablets &&
           num_peers_leader_ready == expected_total_tablets;
  }, 40s * kTimeMultiplier, "Wait for tablet split to be completed");
  if (!s.ok()) {
    for (const auto& peer : peers) {
      const auto tablet = peer->shared_tablet();
      const auto consensus = peer->shared_consensus();
      if (!tablet || !consensus) {
        LOG(INFO) << consensus::MakeTabletLogPrefix(peer->tablet_id(), peer->permanent_uuid())
                  << "no tablet";
        continue;
      }
      if (tablet->table_type() == TRANSACTION_STATUS_TABLE_TYPE) {
        continue;
      }
      LOG(INFO) << consensus::MakeTabletLogPrefix(peer->tablet_id(), peer->permanent_uuid())
                << "raft_group_state: " << AsString(peer->state())
                << " tablet_data_state: "
                << TabletDataState_Name(tablet->metadata()->tablet_data_state())
                << " leader status: "
                << AsString(consensus->GetLeaderStatus(/* allow_stale =*/true));
    }
    LOG(FATAL) << "Crashing test to avoid waiting on deadlock. Received error: " << s.ToString();
  }

  DumpTableLocations(catalog_manager(), table);
}

template <class MiniClusterType>
void TabletSplitITestBase<MiniClusterType>::WaitForTestTableTabletsCompactionFinish(
    MonoDelta timeout) {
  ASSERT_OK(WaitFor([this] ()-> Result<bool> {
    bool all_fully_compacted = true;
    auto list = ListPostSplitChildrenTabletPeers();
    if (!list.ok()) {
      return false;
    }
    VLOG(1) << "Checking Tablets Peers:";
    for (auto peer : list.get()) {
      bool compacted = peer->tablet_metadata()->has_been_fully_compacted();
      VLOG(1) << peer->tablet_id() << " " << peer->permanent_uuid()
             << (compacted ? " is compacted" : " is not compacted");
      if (!compacted) {
        all_fully_compacted = false;
        break;
      }
    }
    return all_fully_compacted;
    }, timeout * kTimeMultiplier, "Wait for post tablet split compaction to be completed"));
}

template <class MiniClusterType>
Result<std::vector<tablet::TabletPeerPtr>>
    TabletSplitITestBase<MiniClusterType>::ListSplitCompleteTabletPeers() {
  return ListTableInactiveSplitTabletPeers(this->cluster_.get(), VERIFY_RESULT(GetTestTableId()));
}

template <class MiniClusterType>
Result<std::vector<tablet::TabletPeerPtr>>
    TabletSplitITestBase<MiniClusterType>::ListPostSplitChildrenTabletPeers() {
  return ListTableActiveTabletPeers(this->cluster_.get(), VERIFY_RESULT(GetTestTableId()));
}

bool HasPendingCompaction(rocksdb::DB* db) {
  uint64_t compaction_pending = 0;
  db->GetIntProperty("rocksdb.compaction-pending", &compaction_pending);
  return compaction_pending > 0;
}

bool HasRunningCompaction(rocksdb::DB* db) {
  uint64_t running_compactions = 0;
  db->GetIntProperty("rocksdb.num-running-compactions", &running_compactions);
  return running_compactions > 0;
}

template <class MiniClusterType>
void TabletSplitITestBase<MiniClusterType>::VerifyTriggeredPostSplitCompaction(int num_peers) {
  for (auto peer : ASSERT_RESULT(ListPostSplitChildrenTabletPeers())) {
    const auto* tablet = peer->tablet();
    auto* db = tablet->TEST_db();
    auto has_triggered_compaction = HasPendingCompaction(db) ||
        HasRunningCompaction(db) ||
        tablet->metadata()->has_been_fully_compacted();
    EXPECT_TRUE(has_triggered_compaction);
    num_peers--;
  }
  EXPECT_EQ(num_peers, 0);
}

template <class MiniClusterType>
Result<uint64_t> TabletSplitITestBase<MiniClusterType>::GetActiveTabletsBytesRead() {
  uint64_t read_bytes_1 = 0, read_bytes_2 = 0;
  auto peers = ListTableActiveTabletLeadersPeers(
      this->cluster_.get(), VERIFY_RESULT(GetTestTableId()));
  for (auto peer : peers) {
    auto this_peer_read_bytes = peer->tablet()->regulardb_statistics()->getTickerCount(
        rocksdb::Tickers::COMPACT_READ_BYTES);
    if (read_bytes_1 == 0) {
      read_bytes_1 = this_peer_read_bytes;
    } else if (read_bytes_2 == 0) {
      read_bytes_2 = this_peer_read_bytes;
    } else {
      if (this_peer_read_bytes != read_bytes_1 && this_peer_read_bytes != read_bytes_2) {
        return STATUS_FORMAT(IllegalState,
            "Expected this peer's read bytes ($0) to equal one of the existing peer's read bytes "
            "($1 or $2)",
            this_peer_read_bytes, read_bytes_1, read_bytes_2);
      }
    }
  }
  if (read_bytes_1 <= 0 || read_bytes_2 <= 0) {
    return STATUS_FORMAT(IllegalState,
        "Peer's read bytes should be greater than zero. Found $0 and $1",
        read_bytes_1, read_bytes_2);
  }
  return read_bytes_1 + read_bytes_2;
}

template <class MiniClusterType>
Result<uint64_t> TabletSplitITestBase<MiniClusterType>::GetInactiveTabletsBytesWritten() {
  uint64_t write_bytes = 0;
  for (auto peer : VERIFY_RESULT(ListSplitCompleteTabletPeers())) {
    auto this_peer_written_bytes = peer->tablet()->regulardb_statistics()->getTickerCount(
        rocksdb::Tickers::COMPACT_WRITE_BYTES);
    if (write_bytes == 0) write_bytes = this_peer_written_bytes;
    if (write_bytes != this_peer_written_bytes) {
      return STATUS_FORMAT(IllegalState,
          "Expected the number of written bytes at each peer to be the same. Found one with $0 and "
          "another with $1",
          write_bytes, this_peer_written_bytes);
    }
  }
  return write_bytes;
}

template <class MiniClusterType>
Status TabletSplitITestBase<MiniClusterType>::CheckPostSplitTabletReplicasData(
    size_t num_rows, size_t num_replicas_online, size_t num_active_tablets) {
  LOG(INFO) << "Checking post-split tablet replicas data...";

  if (num_replicas_online == 0) {
      num_replicas_online = FLAGS_replication_factor;
  }

  const auto test_table_id = VERIFY_RESULT(GetTestTableId());
  std::vector<tablet::TabletPeerPtr> active_leader_peers;
  RETURN_NOT_OK(LoggedWaitFor([&] {
    active_leader_peers = ListTableActiveTabletLeadersPeers(this->cluster_.get(), test_table_id);
    LOG(INFO) << "active_leader_peers.size(): " << active_leader_peers.size();
    return active_leader_peers.size() == num_active_tablets;
  }, 30s * kTimeMultiplier, "Waiting for leaders ..."));

  std::unordered_map<TabletId, OpId> last_on_leader;
  for (auto peer : active_leader_peers) {
    last_on_leader[peer->tablet_id()] = peer->shared_consensus()->GetLastReceivedOpId();
  }

  const auto active_peers = ListTableActiveTabletPeers(this->cluster_.get(), test_table_id);

  std::vector<size_t> keys(num_rows, num_replicas_online);
  std::unordered_map<size_t, std::vector<std::string>> key_replicas;
  const auto key_column_id = this->table_.ColumnId(this->kKeyColumn);
  const auto value_column_id = this->table_.ColumnId(this->kValueColumn);
  for (auto peer : active_peers) {
    RETURN_NOT_OK(LoggedWaitFor(
        [&] {
          return peer->shared_consensus()->GetLastAppliedOpId() >=
                 last_on_leader[peer->tablet_id()];
        },
        15s * kTimeMultiplier,
        Format(
            "Waiting for tablet replica $0 to apply all ops from leader ...", peer->LogPrefix())));
    LOG(INFO) << "Last applied op id for " << peer->LogPrefix() << ": "
              << AsString(peer->shared_consensus()->GetLastAppliedOpId());

    const auto shared_tablet = peer->shared_tablet();
    const SchemaPtr schema = shared_tablet->metadata()->schema();
    auto client_schema = schema->CopyWithoutColumnIds();
    auto iter = VERIFY_RESULT(shared_tablet->NewRowIterator(client_schema, boost::none));
    QLTableRow row;
    std::unordered_set<size_t> tablet_keys;
    while (VERIFY_RESULT(iter->HasNext())) {
      RETURN_NOT_OK(iter->NextRow(&row));
      auto key_opt = row.GetValue(key_column_id);
      SCHECK(key_opt.is_initialized(), InternalError, "Key is not initialized");
      SCHECK_EQ(key_opt, row.GetValue(value_column_id), InternalError, "Wrong value for key");
      auto key = key_opt->int32_value();
      SCHECK(
          tablet_keys.insert(key).second,
          InternalError,
          Format("Duplicate key $0 in tablet $1", key, shared_tablet->tablet_id()));
      SCHECK_GT(
          keys[key - 1]--,
          0,
          InternalError,
          Format("Extra key $0 in tablet $1", key, shared_tablet->tablet_id()));
      key_replicas[key - 1].push_back(peer->LogPrefix());
    }
  }
  for (auto key = 1; key <= num_rows; ++key) {
    const auto key_missing_in_replicas = keys[key - 1];
    if (key_missing_in_replicas > 0) {
      LOG(INFO) << Format("Key $0 replicas: $1", key, key_replicas[key - 1]);
      return STATUS_FORMAT(
          InternalError, "Missing key: $0 in $1 replicas", key, key_missing_in_replicas);
    }
  }
  return Status::OK();
}

Status TabletSplitITest::CheckSourceTabletAfterSplit(const TabletId& source_tablet_id) {
  LOG(INFO) << "Checking source tablet behavior after split...";
  google::FlagSaver saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_do_not_start_election_test_only) = true;

  size_t tablet_split_insert_error_count = 0;
  size_t not_the_leader_insert_error_count = 0;
  size_t ts_online_count = 0;
  for (auto mini_ts : this->cluster_->mini_tablet_servers()) {
    if (!mini_ts->is_started()) {
      continue;
    }
    ++ts_online_count;
    auto ts_service_proxy = std::make_unique<tserver::TabletServerServiceProxy>(
        proxy_cache_.get(), HostPort::FromBoundEndpoint(mini_ts->bound_rpc_addr()));

    {
      tserver::ReadRequestPB req = VERIFY_RESULT(CreateReadRequest(source_tablet_id, 1 /* key */));

      rpc::RpcController controller;
      controller.set_timeout(kRpcTimeout);
      tserver::ReadResponsePB resp;
      RETURN_NOT_OK(ts_service_proxy->Read(req, &resp, &controller));

      SCHECK(resp.has_error(), InternalError, "Expected error on read from split tablet");
      SCHECK_EQ(
          resp.error().code(),
          tserver::TabletServerErrorPB::TABLET_SPLIT,
          InternalError,
          "Expected error on read from split tablet to be "
          "tserver::TabletServerErrorPB::TABLET_SPLIT");
    }

    {
      tserver::WriteRequestPB req =
          CreateInsertRequest(source_tablet_id, 0 /* key */, 0 /* value */);

      rpc::RpcController controller;
      controller.set_timeout(kRpcTimeout);
      tserver::WriteResponsePB resp;
      RETURN_NOT_OK(ts_service_proxy->Write(req, &resp, &controller));

      SCHECK(resp.has_error(), InternalError, "Expected error on write to split tablet");
      LOG(INFO) << "Error: " << AsString(resp.error());
      switch (resp.error().code()) {
        case tserver::TabletServerErrorPB::TABLET_SPLIT:
          SCHECK_EQ(
              resp.error().status().code(),
              AppStatusPB::ILLEGAL_STATE,
              InternalError,
              "tserver::TabletServerErrorPB::TABLET_SPLIT error should have "
              "AppStatusPB::ILLEGAL_STATE on write to split tablet");
          tablet_split_insert_error_count++;
          break;
        case tserver::TabletServerErrorPB::NOT_THE_LEADER:
          not_the_leader_insert_error_count++;
          break;
        default:
          return STATUS_FORMAT(InternalError, "Unexpected error: $0", resp.error());
      }
    }
  }
  SCHECK_EQ(
      tablet_split_insert_error_count, 1, InternalError,
      "Leader should return \"try again\" error on insert.");
  SCHECK_EQ(
      not_the_leader_insert_error_count, ts_online_count - 1, InternalError,
      "Followers should return \"not the leader\" error.");
  return Status::OK();
}

template <class MiniClusterType>
Result<scoped_refptr<master::TabletInfo>>
    TabletSplitITestBase<MiniClusterType>::GetSingleTestTabletInfo(
        master::CatalogManager* catalog_mgr) {
  std::vector<scoped_refptr<master::TabletInfo>> tablet_infos;
  catalog_mgr->GetTableInfo(this->table_->id())->GetAllTablets(&tablet_infos);

  SCHECK_EQ(tablet_infos.size(), 1, IllegalState, "Expect test table to have only 1 tablet");
  return tablet_infos.front();
}

// Tests splitting of the single tablet in following steps:
// - Create single-tablet table and populates it with specified number of rows.
// - Do full scan using `select count(*)`.
// - Send SplitTablet RPC to the tablet leader.
// - After tablet split is completed - check that new tablets have exactly the same rows.
// - Check that source tablet is rejecting reads and writes.
// - Do full scan using `select count(*)`.
// - Restart cluster.
// - ClusterVerifier will check cluster integrity at the end of the test.

TEST_P(TabletSplitITestWithIsolationLevel, SplitSingleTablet) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = true;

  // TODO(tsplit): add delay of applying part of intents after tablet is split.
  // TODO(tsplit): test split during long-running transactions.

  constexpr auto kNumRows = 500;

  const auto source_tablet_id = ASSERT_RESULT(CreateSingleTabletAndSplit(kNumRows));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = false;

  ASSERT_NO_FATALS(WaitForTabletSplitCompletion(/* expected_non_split_tablets =*/ 2));

  ASSERT_OK(CheckRowsCount(kNumRows));

  ASSERT_OK(WriteRows(kNumRows, kNumRows + 1));

  ASSERT_OK(cluster_->RestartSync());

  ASSERT_OK(CheckPostSplitTabletReplicasData(kNumRows * 2));
}

TEST_F(TabletSplitITest, SplitTabletIsAsync) {
  constexpr auto kNumRows = 500;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_post_split_compation) = true;

  ASSERT_OK(CreateSingleTabletAndSplit(kNumRows));

  for (auto peer : ASSERT_RESULT(ListPostSplitChildrenTabletPeers())) {
    EXPECT_FALSE(peer->tablet()->metadata()->has_been_fully_compacted());
  }
  std::this_thread::sleep_for(1s * kTimeMultiplier);
  for (auto peer : ASSERT_RESULT(ListPostSplitChildrenTabletPeers())) {
    EXPECT_FALSE(peer->tablet()->metadata()->has_been_fully_compacted());
  }
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_post_split_compation) = false;
  ASSERT_NO_FATALS(WaitForTestTableTabletsCompactionFinish(5s * kTimeMultiplier));
}

TEST_F(TabletSplitITest, ParentTabletCleanup) {
  constexpr auto kNumRows = 500;

  ASSERT_OK(CreateSingleTabletAndSplit(kNumRows));

  // This will make client first try to access deleted tablet and that should be handled correctly.
  ASSERT_OK(CheckRowsCount(kNumRows));
}

TEST_F(TabletSplitITest, TestInitiatesCompactionAfterSplit) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = true;
  constexpr auto kNumRows = 10000;

  CreateSingleTablet();

  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));

  ASSERT_OK(SplitTabletAndValidate(split_hash_code, kNumRows));

  auto pre_split_bytes_written = ASSERT_RESULT(GetInactiveTabletsBytesWritten());
  auto post_split_bytes_read = ASSERT_RESULT((GetActiveTabletsBytesRead()));
  ASSERT_GE(pre_split_bytes_written, post_split_bytes_read);
}

TEST_F(TabletSplitITest, TestLoadBalancerAndSplit) {
  constexpr auto kNumRows = 10000;

  // Keep tablets without compaction after split.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_post_split_compation) = true;

  CreateSingleTablet();

  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));

  ASSERT_OK(SplitTabletAndValidate(split_hash_code, kNumRows));

  auto test_table_id = ASSERT_RESULT(client_->GetYBTableInfo(client::kTableName)).table_id;
  // Verify that heartbeat contains flag should_disable_lb_move for all tablets of the test
  // table on each tserver to have after split.
  for (int i = 0; i != cluster_->num_tablet_servers(); ++i) {
    auto server = cluster_->mini_tablet_server(i)->server();
    if (!server) { // Server is shut down.
      continue;
    }
    auto* ts_manager = server->tablet_manager();
    std::set<TabletId> tablets;
    for (auto& peer : ts_manager->GetTabletPeers()) {
      if (peer->tablet_metadata()->table_id() == test_table_id) {
        tablets.insert(peer->tablet()->tablet_id());
      }
    }

    master::TabletReportPB report;
    ts_manager->GenerateTabletReport(&report);
    for (const auto& reported_tablet : report.updated_tablets()) {
      if (tablets.find(reported_tablet.tablet_id()) == tablets.end()) {
        continue;
      }
      EXPECT_TRUE(reported_tablet.should_disable_lb_move());
    }
  }

  // Wait for the flag should_disable_lb_move to be propagated to master through heartbeat.
  SleepFor(MonoDelta::FromMilliseconds(2 * FLAGS_heartbeat_interval_ms));

  // Add new tserver in to force load balancer moves.
  auto newts = cluster_->num_tablet_servers();
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(newts + 1));
  const auto newts_uuid = cluster_->mini_tablet_server(newts)->server()->permanent_uuid();

  // Wait for the LB run.
  SleepFor(MonoDelta::FromMilliseconds(2 * FLAGS_catalog_manager_bg_task_wait_ms));

  // Verify none test table replica on the new tserver.
  scoped_refptr<master::TableInfo> tbl_info = catalog_manager()->GetTableInfo(test_table_id);
  vector<scoped_refptr<master::TabletInfo>> tablets;
  tbl_info->GetAllTablets(&tablets);
  bool foundReplica = false;
  for (const auto& tablet : tablets) {
    auto replica_map = tablet->GetReplicaLocations();
    if (replica_map->find(newts_uuid) != replica_map->end()) {
      foundReplica = true;
      break;
    }
  }
  EXPECT_FALSE(foundReplica);

  // Verify that custom placement info is honored when tablets are splitted.
  ASSERT_OK(cluster_->AddTServerToBlacklist(cluster_->mini_master(),
                                            cluster_->mini_tablet_server(1)));

  // Wait for test table replica on the new tserver.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
                        tbl_info = catalog_manager()->GetTableInfo(test_table_id);
                        tbl_info->GetAllTablets(&tablets);
                        foundReplica = false;
                        for (const auto& tablet : tablets) {
                          auto replica_map = tablet->GetReplicaLocations();
                          if (replica_map->find(newts_uuid) != replica_map->end()) {
                            foundReplica = true;
                            break;
                          }
                      }
                      return foundReplica;
                    }, 60s * kTimeMultiplier, "WaitForLBToBeProcessed"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_post_split_compation) = false;

  ASSERT_NO_FATALS(WaitForTestTableTabletsCompactionFinish(5s * kTimeMultiplier));
}

// Start tablet split, create Index to start backfill while split operation in progress
// and check backfill state.
TEST_F(TabletSplitITest, TestBackfillDuringSplit) {
  constexpr auto kNumRows = 10000;
  FLAGS_TEST_apply_tablet_split_inject_delay_ms = 200 * kTimeMultiplier;

  CreateSingleTablet();
  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));
  auto* catalog_mgr = catalog_manager();
  auto table = catalog_mgr->GetTableInfo(table_->id());
  auto source_tablet_info = ASSERT_RESULT(GetSingleTestTabletInfo(catalog_mgr));
  const auto source_tablet_id = source_tablet_info->id();

  // Send SplitTablet RPC to the tablet leader.
  ASSERT_OK(catalog_mgr->TEST_SplitTablet(source_tablet_info, split_hash_code));

  int indexed_column_index = 1;
  const client::YBTableName index_name(
        YQL_DATABASE_CQL, table_.name().namespace_name(),
        table_.name().table_name() + '_' +
          table_.schema().Column(indexed_column_index).name() + "_idx");
  // Create index while split operation in progress
  PrepareIndex(client::Transactional(GetIsolationLevel() != IsolationLevel::NON_TRANSACTIONAL),
               index_name, indexed_column_index);

  // Check that source table is not backfilling and wait for tablet split completion
  ASSERT_FALSE(table->IsBackfilling());
  WaitForTabletSplitCompletion(2);
  ASSERT_OK(CheckPostSplitTabletReplicasData(kNumRows));

  ASSERT_OK(index_.Open(index_name, client_.get()));
  ASSERT_OK(WaitFor([&] {
    auto rows_count = SelectRowsCount(NewSession(), index_);
    if (!rows_count.ok()) {
      return false;
    }
    return *rows_count == kNumRows;
  }, 30s * kTimeMultiplier, "Waiting for backfill index"));
}

// Create Index to start backfill, check split is not working while backfill in progress
// and check backfill state.
TEST_F(TabletSplitITest, TestSplitDuringBackfill) {
  constexpr auto kNumRows = 10000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = false;
  FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms = 200 * kTimeMultiplier;

  CreateSingleTablet();
  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));

  int indexed_column_index = 1;
  const client::YBTableName index_name(
        YQL_DATABASE_CQL, table_.name().namespace_name(),
        table_.name().table_name() + '_' +
          table_.schema().Column(indexed_column_index).name() + "_idx");
  // Create index and start backfill
  PrepareIndex(client::Transactional(GetIsolationLevel() != IsolationLevel::NON_TRANSACTIONAL),
               index_name, indexed_column_index);

  auto* catalog_mgr = catalog_manager();
  auto table = catalog_mgr->GetTableInfo(table_->id());
  auto source_tablet_info = ASSERT_RESULT(GetSingleTestTabletInfo(catalog_mgr));
  const auto source_tablet_id = source_tablet_info->id();

  // Check that source table is backfilling
  ASSERT_OK(WaitFor([&] {
    return table->IsBackfilling();
  }, 30s * kTimeMultiplier, "Waiting for start backfill index"));

  // Send SplitTablet RPC to the tablet leader while backfill in progress
  ASSERT_NOK(catalog_mgr->TEST_SplitTablet(source_tablet_info, split_hash_code));

  ASSERT_OK(WaitFor([&] {
    return !table->IsBackfilling();
  }, 30s * kTimeMultiplier, "Waiting for backfill index"));
  ASSERT_OK(catalog_mgr->TEST_SplitTablet(source_tablet_info, split_hash_code));
  WaitForTabletSplitCompletion(2);
  ASSERT_OK(CheckPostSplitTabletReplicasData(kNumRows));
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/4312 reproducing a deadlock
// between TSTabletManager::ApplyTabletSplit and Heartbeater::Thread::TryHeartbeat.
TEST_F(TabletSplitITest, SlowSplitSingleTablet) {
  const auto leader_failure_timeout = FLAGS_leader_failure_max_missed_heartbeat_periods *
        FLAGS_raft_heartbeat_interval_ms;

  FLAGS_TEST_apply_tablet_split_inject_delay_ms = 200 * kTimeMultiplier;
  // We want heartbeater to be called during tablet split apply to reproduce deadlock bug.
  FLAGS_heartbeat_interval_ms = FLAGS_TEST_apply_tablet_split_inject_delay_ms / 3;
  // We reduce FLAGS_leader_lease_duration_ms for ReplicaState::GetLeaderState to avoid always
  // reusing results from cache on heartbeat, otherwise it won't lock ReplicaState mutex.
  FLAGS_leader_lease_duration_ms = FLAGS_TEST_apply_tablet_split_inject_delay_ms / 2;
  // Reduce raft_heartbeat_interval_ms for leader lease to be reliably replicated.
  FLAGS_raft_heartbeat_interval_ms = FLAGS_leader_lease_duration_ms / 2;
  // Keep leader failure timeout the same to avoid flaky losses of leader with short heartbeats.
  FLAGS_leader_failure_max_missed_heartbeat_periods =
      leader_failure_timeout / FLAGS_raft_heartbeat_interval_ms;

  constexpr auto kNumRows = 50;

  ASSERT_OK(CreateSingleTabletAndSplit(kNumRows));
}

template <class MiniClusterType>
void TabletSplitITestBase<MiniClusterType>::CheckTableKeysInRange(const size_t num_keys) {
  client::TableHandle table;
  ASSERT_OK(table.Open(client::kTableName, this->client_.get()));

  std::vector<int32> keys;
  for (const auto& row : client::TableRange(table)) {
    keys.push_back(row.column(0).int32_value());
  }

  LOG(INFO) << "Total rows read: " << keys.size();

  std::sort(keys.begin(), keys.end());
  int32 prev_key = 0;
  for (const auto& key : keys) {
    if (key != prev_key + 1) {
      LOG(ERROR) << "Keys missed: " << prev_key + 1 << "..." << key - 1;
    }
    prev_key = key;
  }
  LOG(INFO) << "Last key: " << prev_key;

  ASSERT_EQ(prev_key, num_keys);
  ASSERT_EQ(keys.size(), num_keys);
}

namespace {

void DumpWorkloadStats(const TestWorkload& workload) {
  LOG(INFO) << "Rows inserted: " << workload.rows_inserted();
  LOG(INFO) << "Rows insert failed: " << workload.rows_insert_failed();
  LOG(INFO) << "Rows read ok: " << workload.rows_read_ok();
  LOG(INFO) << "Rows read empty: " << workload.rows_read_empty();
  LOG(INFO) << "Rows read error: " << workload.rows_read_error();
  LOG(INFO) << "Rows read try again: " << workload.rows_read_try_again();
}

CHECKED_STATUS SplitTablet(master::CatalogManager* catalog_mgr, const tablet::Tablet& tablet) {
  const auto& tablet_id = tablet.tablet_id();
  LOG(INFO) << "Tablet: " << tablet_id;
  LOG(INFO) << "Number of SST files: " << tablet.TEST_db()->GetCurrentVersionNumSSTFiles();
  std::string properties;
  tablet.TEST_db()->GetProperty(rocksdb::DB::Properties::kAggregatedTableProperties, &properties);
  LOG(INFO) << "DB properties: " << properties;

  return catalog_mgr->SplitTablet(tablet_id);
}

CHECKED_STATUS DoSplitTablet(master::CatalogManager* catalog_mgr, const tablet::Tablet& tablet) {
  const auto& tablet_id = tablet.tablet_id();
  LOG(INFO) << "Tablet: " << tablet_id;
  LOG(INFO) << "Number of SST files: " << tablet.TEST_db()->GetCurrentVersionNumSSTFiles();
  std::string properties;
  tablet.TEST_db()->GetProperty(rocksdb::DB::Properties::kAggregatedTableProperties, &properties);
  LOG(INFO) << "DB properties: " << properties;

  const auto encoded_split_key = VERIFY_RESULT(tablet.GetEncodedMiddleSplitKey());
  const auto doc_key_hash = VERIFY_RESULT(docdb::DecodeDocKeyHash(encoded_split_key)).value();
  LOG(INFO) << "Middle hash key: " << doc_key_hash;
  const auto partition_split_key = PartitionSchema::EncodeMultiColumnHashValue(doc_key_hash);

  return catalog_mgr->TEST_SplitTablet(tablet_id, encoded_split_key, partition_split_key);
}

} // namespace

TEST_F(TabletSplitITest, SplitTabletDuringReadWriteLoad) {
  constexpr auto kNumTablets = 3;

  FLAGS_db_write_buffer_size = 100_KB;

  TestWorkload workload(cluster_.get());
  workload.set_table_name(client::kTableName);
  workload.set_write_timeout_millis(MonoDelta(kRpcTimeout).ToMilliseconds());
  workload.set_num_tablets(kNumTablets);
  workload.set_num_read_threads(4);
  workload.set_num_write_threads(2);
  workload.set_write_batch_size(50);
  workload.set_payload_bytes(16);
  workload.set_sequential_write(true);
  workload.set_retry_on_restart_required_error(true);
  workload.set_read_only_written_keys(true);
  workload.Setup();

  const auto test_table_id = ASSERT_RESULT(GetTestTableId());

  std::vector<tablet::TabletPeerPtr> peers;
  ASSERT_OK(LoggedWaitFor([&] {
    peers = ListTableActiveTabletLeadersPeers(cluster_.get(), test_table_id);
    return peers.size() == kNumTablets;
  }, 30s * kTimeMultiplier, "Waiting for leaders ..."));

  LOG(INFO) << "Starting workload ...";
  workload.Start();

  for (const auto& peer : peers) {
    ASSERT_OK(LoggedWaitFor(
        [&peer] {
          const auto data_size =
              peer->tablet()->TEST_db()->GetCurrentVersionSstFilesUncompressedSize();
          YB_LOG_EVERY_N_SECS(INFO, 5) << "Data written: " << data_size;
          return data_size > (FLAGS_rocksdb_level0_file_num_compaction_trigger + 1) *
                                 FLAGS_db_write_buffer_size;
        },
        60s * kTimeMultiplier, Format("Writing data to split (tablet $0) ...", peer->tablet_id())));
  }

  DumpWorkloadStats(workload);

  auto* catalog_mgr = catalog_manager();

  for (const auto& peer : peers) {
    const auto& source_tablet = *ASSERT_NOTNULL(peer->tablet());
    ASSERT_OK(SplitTablet(catalog_mgr, source_tablet));
  }

  ASSERT_NO_FATALS(WaitForTabletSplitCompletion(/* expected_non_split_tablets =*/ kNumTablets * 2));

  DumpTableLocations(catalog_mgr, client::kTableName);

  // Generate some more read/write traffic after tablets are split and after that we check data
  // for consistency and that failures rates are acceptable.
  std::this_thread::sleep_for(5s);

  LOG(INFO) << "Stopping workload ...";
  workload.StopAndJoin();

  DumpWorkloadStats(workload);

  ASSERT_NO_FATALS(CheckTableKeysInRange(workload.rows_inserted()));

  const auto insert_failure_rate = 1.0 * workload.rows_insert_failed() / workload.rows_inserted();
  const auto read_failure_rate = 1.0 * workload.rows_read_error() / workload.rows_read_ok();
  const auto read_try_again_rate = 1.0 * workload.rows_read_try_again() / workload.rows_read_ok();

  ASSERT_LT(insert_failure_rate, 0.01);
  ASSERT_LT(read_failure_rate, 0.01);
  // TODO(tsplit): lower this threshold as internal (without reaching client app) read retries
  //  implemented for split tablets.
  ASSERT_LT(read_try_again_rate, 0.1);
  ASSERT_EQ(workload.rows_read_empty(), 0);

  // TODO(tsplit): Check with different isolation levels.
  // TODO(tsplit): Add more splits during writes, so we have tablets with split_depth > 1.

  ASSERT_OK(cluster_->RestartSync());
}

void TabletSplitITest::SplitClientRequestsIds(int split_depth) {
  // Set data block size low enough, so we have enough data blocks for middle key
  // detection to work correctly.
  FLAGS_db_block_size_bytes = 1_KB;
  const auto kNumRows = 50 * (1 << split_depth);

  SetNumTablets(1);
  CreateTable();

  ASSERT_OK(WriteRows(kNumRows, 1));

  ASSERT_OK(CheckRowsCount(kNumRows));

  auto* catalog_mgr = catalog_manager();

  for (int i = 0; i < split_depth; ++i) {
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
    ASSERT_EQ(peers.size(), 1 << i);
    for (const auto& peer : peers) {
      const auto tablet = peer->shared_tablet();
      ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));
      tablet->ForceRocksDBCompactInTest();
      ASSERT_OK(SplitTablet(catalog_mgr, *tablet));
    }

    ASSERT_NO_FATALS(WaitForTabletSplitCompletion(
        /* expected_non_split_tablets =*/ 1 << (i + 1)));
  }

  Status s;
  ASSERT_OK(WaitFor([&] {
    s = ResultToStatus(WriteRows(1, 1));
    return !s.IsTryAgain();
  }, 60s * kTimeMultiplier, "Waiting for successful write"));
  ASSERT_OK(s);
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/5415.
// Client knows about split parent for final tablets.
TEST_F(TabletSplitITest, SplitClientRequestsIdsDepth1) {
  SplitClientRequestsIds(1);
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/5415.
// Client doesn't know about split parent for final tablets.
TEST_F(TabletSplitITest, SplitClientRequestsIdsDepth2) {
  SplitClientRequestsIds(2);
}

TEST_F(TabletSplitITest, SplitSingleTabletWithLimit) {
  FLAGS_db_block_size_bytes = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = false;
  const auto kSplitDepth = 3;
  const auto kNumRows = 50 * (1 << kSplitDepth);
  FLAGS_tablet_split_limit_per_table = (1 << kSplitDepth) - 1;

  CreateSingleTablet();
  ASSERT_OK(WriteRows(kNumRows, 1));
  ASSERT_OK(CheckRowsCount(kNumRows));

  auto* catalog_mgr = catalog_manager();

  master::TableIdentifierPB table_id_pb;
  table_id_pb.set_table_id(table_->id());
  bool reached_split_limit = false;

  for (int i = 0; i < kSplitDepth; ++i) {
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
    bool expect_split = false;
    for (const auto& peer : peers) {
      const auto tablet = peer->shared_tablet();
      ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));
      tablet->ForceRocksDBCompactInTest();
      auto table_info = ASSERT_RESULT(catalog_mgr->FindTable(table_id_pb));

      expect_split = table_info->NumTablets() < FLAGS_tablet_split_limit_per_table;

      if (expect_split) {
        ASSERT_OK(DoSplitTablet(catalog_mgr, *tablet));
      } else {
        const auto split_status = DoSplitTablet(catalog_mgr, *tablet);
        ASSERT_EQ(master::MasterError(split_status),
                  master::MasterErrorPB::REACHED_SPLIT_LIMIT);
        reached_split_limit = true;
      }
    }
    if (expect_split) {
      ASSERT_NO_FATALS(WaitForTabletSplitCompletion(
          /* expected_non_split_tablets =*/1 << (i + 1)));
    }
  }

  ASSERT_TRUE(reached_split_limit);

  Status s;
  ASSERT_OK(WaitFor([&] {
    s = ResultToStatus(WriteRows(1, 1));
    return !s.IsTryAgain();
  }, 60s * kTimeMultiplier, "Waiting for successful write"));

  auto table_info = ASSERT_RESULT(catalog_mgr->FindTable(table_id_pb));
  ASSERT_EQ(table_info->NumTablets(), FLAGS_tablet_split_limit_per_table);
}

TEST_F(TabletSplitITest, SplitDuringReplicaOffline) {
  constexpr auto kNumRows = 500;

  SetNumTablets(1);
  CreateTable();

  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));

  auto rows_count = ASSERT_RESULT(SelectRowsCount(NewSession(), table_));
  ASSERT_EQ(rows_count, kNumRows);

  auto* catalog_mgr = catalog_manager();

  auto source_tablet_info = ASSERT_RESULT(GetSingleTestTabletInfo(catalog_mgr));
  const auto source_tablet_id = source_tablet_info->id();

  cluster_->mini_tablet_server(0)->Shutdown();

  LOG(INFO) << "Stopped TS-1";

  ASSERT_OK(catalog_mgr->TEST_SplitTablet(source_tablet_info, split_hash_code));

  ASSERT_NO_FATALS(WaitForTabletSplitCompletion(
      /* expected_non_split_tablets =*/ 2, /* expected_split_tablets =*/ 1,
      /* num_replicas_online =*/ 2));

  ASSERT_OK(CheckPostSplitTabletReplicasData(kNumRows, 2));

  ASSERT_OK(CheckSourceTabletAfterSplit(source_tablet_id));

  DumpTableLocations(catalog_mgr, client::kTableName);

  rows_count = ASSERT_RESULT(SelectRowsCount(NewSession(), table_));
  ASSERT_EQ(rows_count, kNumRows);

  ASSERT_OK(WriteRows(kNumRows, kNumRows + 1));

  LOG(INFO) << "Starting TS-1";

  ASSERT_OK(cluster_->mini_tablet_server(0)->Start());

  // This time we expect all replicas to be online.
  ASSERT_NO_FATALS(WaitForTabletSplitCompletion(
      /* expected_non_split_tablets =*/ 2, /* expected_split_tablets =*/ 0));

  Status s;
  ASSERT_OK_PREPEND(LoggedWaitFor([&] {
      s = CheckPostSplitTabletReplicasData(kNumRows * 2);
      return s.IsOk();
    }, 30s * kTimeMultiplier, "Waiting for TS-1 to catch up ..."), AsString(s));
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/6890.
// Writes data to the tablet, splits it and then tries to do full scan with `select count(*)`
// using two different instances of YBTable one after another.
TEST_F(TabletSplitITest, DifferentYBTableInstances) {
  constexpr auto kNumRows = 500;

  CreateSingleTablet();

  client::TableHandle table1, table2;
  for (auto* table : {&table1, &table2}) {
    ASSERT_OK(table->Open(client::kTableName, client_.get()));
  }

  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));
  const auto source_tablet_id = ASSERT_RESULT(SplitTabletAndValidate(split_hash_code, kNumRows));

  ASSERT_NO_FATALS(WaitForTabletSplitCompletion(/* expected_non_split_tablets =*/ 2));

  auto rows_count = ASSERT_RESULT(SelectRowsCount(NewSession(), table1));
  ASSERT_EQ(rows_count, kNumRows);

  rows_count = ASSERT_RESULT(SelectRowsCount(NewSession(), table2));
  ASSERT_EQ(rows_count, kNumRows);
}

class TabletSplitYedisTableTest : public integration_tests::RedisTableTestBase {
 protected:
  int num_tablets() override { return 1; }
};

TEST_F(TabletSplitYedisTableTest, BlockSplittingYedisTablet) {
  constexpr int kNumRows = 10000;

  for (int i = 0; i < kNumRows; ++i) {
    PutKeyValue(Format("$0", i), Format("$0", i));
  }

  for (const auto& peer : ListTableActiveTabletPeers(mini_cluster(), table_->id())) {
    ASSERT_OK(peer->shared_tablet()->Flush(tablet::FlushMode::kSync));
  }

  for (const auto& peer : ListTableActiveTabletLeadersPeers(mini_cluster(), table_->id())) {
    auto catalog_manager = CHECK_NOTNULL(
        this->mini_cluster()->leader_mini_master()->master())->catalog_manager();

    auto s = DoSplitTablet(catalog_manager, *peer->shared_tablet());
    EXPECT_NOT_OK(s);
    EXPECT_TRUE(s.IsNotSupported()) << s.ToString();
  }
}

class AutomaticTabletSplitITest : public TabletSplitITest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 100;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_process_split_tablet_candidates_interval_msec) = 1;
    TabletSplitITest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_split_tablet_candidate_processing) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_select_all_tablets_for_split) = false;
  }
};

TEST_F(AutomaticTabletSplitITest, AutomaticTabletSplitting) {
  constexpr int kNumRowsPerBatch = 1000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 1_MB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;

  CreateSingleTablet();

  int key = 1;
  while (true) {
    ASSERT_OK(WriteRows(kNumRowsPerBatch, key));
    key += kNumRowsPerBatch;
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
    if (peers.size() == 2) {
      for (const auto& peer : peers) {
        auto peer_size = peer->shared_tablet()->GetCurrentVersionSstFilesSize();
        // Since we've disabled compactions, each post-split subtablet should be larger than the
        // split size threshold.
        EXPECT_GE(peer_size, FLAGS_tablet_split_low_phase_size_threshold_bytes);
      }
      break;
    }
    ASSERT_EQ(peers.size(), 1);
    auto active_tablet_leader = peers.at(0)->shared_tablet();
    ASSERT_OK(active_tablet_leader->Flush(tablet::FlushMode::kSync));
    auto current_size = active_tablet_leader->GetCurrentVersionSstFilesSize();
    if (current_size > FLAGS_tablet_split_low_phase_size_threshold_bytes) {
      ASSERT_NO_FATALS(WaitForTabletSplitCompletion(/* expected_non_split_tablets =*/ 2));
      break;
    }
  }

  // Since compaction is off, the tablets should not be further split since they won't have had
  // their post split compaction. Assert this is true by tripling the number of keys written and
  // seeing the number of tablets not grow.
  auto triple_keys = key * 2;
  while (key < triple_keys) {
    ASSERT_OK(WriteRows(kNumRowsPerBatch, key));
    key += kNumRowsPerBatch;
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
    EXPECT_EQ(peers.size(), 2);
  }
}

TEST_F(AutomaticTabletSplitITest, AutomaticTabletSplittingMovesToNextPhase) {
  constexpr int kNumRowsPerBatch = 5000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 50_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_size_threshold_bytes) = 100_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_shard_count_per_node) = 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;

  auto this_phase_tablet_lower_limit = FLAGS_tablet_split_low_phase_shard_count_per_node
      * cluster_->num_tablet_servers();
  auto this_phase_tablet_upper_limit = FLAGS_tablet_split_high_phase_shard_count_per_node
      * cluster_->num_tablet_servers();

  int num_tablets = this_phase_tablet_lower_limit;
  SetNumTablets(num_tablets);
  CreateTable();

  int key = 1;
  while (num_tablets < this_phase_tablet_upper_limit) {
    ASSERT_OK(WriteRows(kNumRowsPerBatch, key));
    key += kNumRowsPerBatch;
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
    num_tablets = peers.size();
    for (const auto& peer : peers) {
      ASSERT_OK(peer->shared_tablet()->Flush(tablet::FlushMode::kSync));
      auto size = peer->shared_tablet()->GetCurrentVersionSstFilesSize();
      if (size > FLAGS_tablet_split_high_phase_size_threshold_bytes) {
        ASSERT_NO_FATALS(WaitForTabletSplitCompletion(num_tablets + 1));
        num_tablets++;
      }
    }
  }
  EXPECT_EQ(num_tablets, this_phase_tablet_upper_limit);
}

TEST_F(AutomaticTabletSplitITest, AutomaticTabletSplittingMultiPhase) {
  constexpr int kNumRowsPerBatch = RegularBuildVsSanitizers(5000, 1000);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 10_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_size_threshold_bytes) = 20_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_shard_count_per_node) = 2;
  // Disable automatic compactions, but continue to allow manual compactions.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_base_background_compactions) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_background_compactions) = 0;

  SetNumTablets(1);
  CreateTable();

  int key = 1;
  auto num_peers = 1;
  const auto num_tservers = cluster_->num_tablet_servers();

  auto test_phase = [&key, &num_peers, this](int tablet_count_limit, int split_threshold_bytes) {
    while (num_peers < tablet_count_limit) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_split_tablet_candidate_processing) = true;
      ASSERT_OK(WriteRows(kNumRowsPerBatch, key));
      key += kNumRowsPerBatch;
      auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
      if (peers.size() > num_peers) {
        // If a new tablet was formed, it means one of the tablets from the last iteration was
        // split. In that case, verify that some peer has greater than the current split threshold
        // bytes on disk. Note that it would not have compacted away the post split orphaned bytes
        // since automatic compactions are off, so we expect this verification to pass with
        // certainty.
        num_peers = peers.size();

        auto found_large_tablet = false;
        for (const auto& peer : peers) {
          auto peer_size = peer->shared_tablet()->GetCurrentVersionSstFilesSize();
          // Since we've disabled compactions, each post-split subtablet should be larger than the
          // split size threshold.
          if (peer_size > split_threshold_bytes) {
            found_large_tablet = true;
          }
        }
        ASSERT_TRUE(found_large_tablet)
            << "We have split a tablet but upon inspection none of them were large enough to "
            << "split.";
      }

      for (const auto& peer : peers) {
        ASSERT_OK(peer->shared_tablet()->Flush(tablet::FlushMode::kSync));
        // Compact each tablet to remove the orphaned post-split data so that it can be split again.
        ASSERT_OK(peer->shared_tablet()->ForceFullRocksDBCompact());
      }
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_split_tablet_candidate_processing) = false;
      // Wait for two rounds of split tablet processing intervals to ensure any split candidates
      // have had time to be processed and drained from the queue of TabletSplitManager.
      auto sleep_time =
          FLAGS_process_split_tablet_candidates_interval_msec * 2ms * kTimeMultiplier;
      std::this_thread::sleep_for(sleep_time);
    }
  };
  test_phase(
    FLAGS_tablet_split_low_phase_shard_count_per_node * num_tservers,
    FLAGS_tablet_split_low_phase_size_threshold_bytes);
  test_phase(
    FLAGS_tablet_split_high_phase_shard_count_per_node * num_tservers,
    FLAGS_tablet_split_high_phase_size_threshold_bytes);
}

class TabletSplitSingleServerITest : public TabletSplitITest {
 protected:
  int64_t GetRF() override { return 1; }

  Result<tablet::TabletPeerPtr> GetSingleTabletLeaderPeer() {
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
    SCHECK_EQ(peers.size(), 1, IllegalState, "Expected only a single tablet leader.");
    return peers.at(0);
  }
};

TEST_F(TabletSplitSingleServerITest, TabletServerGetSplitKey) {
  constexpr auto kNumRows = 2000;
  // Setup table with rows.
  CreateSingleTablet();
  ASSERT_OK(WriteRowsAndGetMiddleHashCode(kNumRows));
  const auto source_tablet_id = ASSERT_RESULT(GetSingleTestTabletInfo(catalog_manager()))->id();

  // Flush tablet and directly compute expected middle key.
  auto tablet_peer = ASSERT_RESULT(GetSingleTabletLeaderPeer());
  ASSERT_OK(tablet_peer->shared_tablet()->Flush(tablet::FlushMode::kSync));
  auto middle_key = ASSERT_RESULT(tablet_peer->shared_tablet()->GetEncodedMiddleSplitKey());
  auto expected_middle_key_hash = CHECK_RESULT(docdb::DocKey::DecodeHash(middle_key));

  // Send RPC.
  auto tserver = cluster_->mini_tablet_server(0);
  auto ts_admin_service_proxy = std::make_unique<tserver::TabletServerAdminServiceProxy>(
    proxy_cache_.get(), HostPort::FromBoundEndpoint(tserver->bound_rpc_addr()));
  tserver::GetSplitKeyRequestPB req;
  req.set_tablet_id(source_tablet_id);
  req.set_dest_uuid(tablet_peer->permanent_uuid());
  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);
  tserver::GetSplitKeyResponsePB resp;
  ASSERT_OK(ts_admin_service_proxy->GetSplitKey(req, &resp, &controller));

  // Validate response.
  CHECK(!resp.has_error()) << resp.error().DebugString();
  auto decoded_split_key_hash = CHECK_RESULT(docdb::DocKey::DecodeHash(resp.split_encoded_key()));
  CHECK_EQ(decoded_split_key_hash, expected_middle_key_hash);
  auto decoded_partition_key_hash = PartitionSchema::DecodeMultiColumnHashValue(
      resp.split_partition_key());
  CHECK_EQ(decoded_partition_key_hash, expected_middle_key_hash);
}

TEST_F(TabletSplitSingleServerITest, TabletServerSplitAlreadySplitTablet) {
  constexpr auto kNumRows = 2000;

  CreateSingleTablet();
  auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));
  auto tablet_peer = ASSERT_RESULT(GetSingleTabletLeaderPeer());
  const auto tserver_uuid = tablet_peer->permanent_uuid();

  SetAtomicFlag(true, &FLAGS_TEST_skip_deleting_split_tablets);
  const auto source_tablet_id = ASSERT_RESULT(SplitSingleTablet(split_hash_code));
  WaitForTabletSplitCompletion(
      /* expected_non_split_tablets =*/ 2, /* expected_split_tablets = */ 1);

  auto send_split_request = [this, &tserver_uuid, &source_tablet_id]()
      -> Result<tserver::SplitTabletResponsePB> {
    auto tserver = cluster_->mini_tablet_server(0);
    auto ts_admin_service_proxy = std::make_unique<tserver::TabletServerAdminServiceProxy>(
      proxy_cache_.get(), HostPort::FromBoundEndpoint(tserver->bound_rpc_addr()));
    tserver::SplitTabletRequestPB req;
    req.set_dest_uuid(tserver_uuid);
    req.set_tablet_id(source_tablet_id);
    req.set_new_tablet1_id(Format("$0$1", source_tablet_id, "1"));
    req.set_new_tablet2_id(Format("$0$1", source_tablet_id, "2"));
    req.set_split_partition_key("abc");
    req.set_split_encoded_key("def");
    rpc::RpcController controller;
    controller.set_timeout(kRpcTimeout);
    tserver::SplitTabletResponsePB resp;
    RETURN_NOT_OK(ts_admin_service_proxy->SplitTablet(req, &resp, &controller));
    return resp;
  };

  // If the parent tablet is still around, this should trigger an AlreadyPresent error
  auto resp = ASSERT_RESULT(send_split_request());
  EXPECT_TRUE(resp.has_error());
  EXPECT_TRUE(StatusFromPB(resp.error().status()).IsAlreadyPresent()) << resp.error().DebugString();

  SetAtomicFlag(false, &FLAGS_TEST_skip_deleting_split_tablets);
  WaitForTabletSplitCompletion(/* expected_non_split_tablets =*/ 2);

  // If the parent tablet has been cleaned up, this should trigger a Not Found error.
  resp = ASSERT_RESULT(send_split_request());
  EXPECT_TRUE(resp.has_error());
  EXPECT_TRUE(StatusFromPB(resp.error().status()).IsNotFound()) << resp.error().DebugString();
}

class TabletSplitExternalMiniClusterITest : public TabletSplitITestBase<ExternalMiniCluster> {
 public:
  void SetUp() override {
    auto tserver_flags = {
      "--cleanup_split_tablets_interval_sec=1",
      "--tserver_heartbeat_metrics_interval_ms=100"
    };
    for (const auto& flag : tserver_flags) {
      this->mini_cluster_opt_.extra_tserver_flags.push_back(flag);
    }

    auto master_flags = {
      "--TEST_disable_split_tablet_candidate_processing=true",
      "--tablet_split_low_phase_shard_count_per_node=-1",
      "--tablet_split_high_phase_shard_count_per_node=-1",
      "--tablet_split_low_phase_size_threshold_bytes=-1",
      "--tablet_split_high_phase_size_threshold_bytes=-1",
      "--tablet_force_split_threshold_bytes=-1",
    };
    for (const auto& flag : master_flags) {
      this->mini_cluster_opt_.extra_master_flags.push_back(flag);
    }

    TabletSplitITestBase<ExternalMiniCluster>::SetUp();
  }

  CHECKED_STATUS SplitTablet(const std::string& tablet_id) {
    master::SplitTabletRequestPB req;
    req.set_tablet_id(tablet_id);
    master::SplitTabletResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(30s * kTimeMultiplier);

    RETURN_NOT_OK(cluster_->master_proxy()->SplitTablet(req, &resp, &rpc));
    return Status::OK();
  }

  Result<std::set<TabletId>> GetTestTableTabletIds(int tserver_idx) {
    std::set<TabletId> tablet_ids;
    auto res = VERIFY_RESULT(cluster_->GetTablets(cluster_->tablet_server(tserver_idx)));
    for (const auto& tablet : res) {
      if (tablet.table_name() == table_->name().table_name()) {
        tablet_ids.insert(tablet.tablet_id());
      }
    }
    return tablet_ids;
  }

  Result<std::set<TabletId>> GetTestTableTabletIds() {
    std::set<TabletId> tablet_ids;
    for (int i = 0; i < cluster_->num_tablet_servers(); ++i) {
      auto res = VERIFY_RESULT(GetTestTableTabletIds(i));
      for (const auto& id : res) {
        tablet_ids.insert(id);
      }
    }
    return tablet_ids;
  }

  CHECKED_STATUS WaitForTablets(int num_tablets, int tserver_idx) {
    return WaitFor([&]() -> Result<bool> {
      auto res = VERIFY_RESULT(GetTestTableTabletIds(tserver_idx));
      return res.size() == num_tablets;
    }, 20s * kTimeMultiplier, Format("Waiting for tablet count: $0", num_tablets));
  }

  CHECKED_STATUS WaitForTablets(int num_tablets) {
    return WaitFor([&]() -> Result<bool> {
      auto res = VERIFY_RESULT(GetTestTableTabletIds());
      return res.size() == num_tablets;
    }, 20s * kTimeMultiplier, Format("Waiting for tablet count: $0", num_tablets));
  }

  Result<TabletId> GetOnlyTabletId(int tserver_idx) {
    auto tablet_ids = VERIFY_RESULT(GetTestTableTabletIds(tserver_idx));
    if (tablet_ids.size() != 1) {
      return STATUS(InternalError, "Expected one tablet");
    }
    return *tablet_ids.begin();
  }

  Result<TabletId> GetOnlyTabletId() {
    auto tablet_ids = VERIFY_RESULT(GetTestTableTabletIds());
    if (tablet_ids.size() != 1) {
      return STATUS(InternalError, Format("Expected one tablet, got $0", tablet_ids.size()));
    }
    return *tablet_ids.begin();
  }
};

TEST_F(TabletSplitExternalMiniClusterITest, Simple) {
  CreateSingleTablet();
  CHECK_OK(WriteRows());
  auto tablet_id = CHECK_RESULT(GetOnlyTabletId());
  CHECK_OK(SplitTablet(tablet_id));
  ASSERT_OK(WaitForTablets(3));
}

TEST_F(TabletSplitExternalMiniClusterITest, CrashesAfterChildLogCopy) {
  ASSERT_OK(cluster_->SetFlagOnMasters("unresponsive_ts_rpc_retry_limit", "0"));

  CreateSingleTablet();
  CHECK_OK(WriteRows());
  const auto tablet_id = CHECK_RESULT(GetOnlyTabletId());

  // We will fault one of the non-leader servers after it performs a WAL Log copy from parent to
  // the first child, but before it can mark the child as TABLET_DATA_READY.
  const auto leader_idx = CHECK_RESULT(cluster_->GetTabletLeaderIndex(tablet_id));
  const auto faulted_follower_idx = (leader_idx + 2) % 3;
  const auto non_faulted_follower_idx = (leader_idx + 1) % 3;

  auto faulted_follower = cluster_->tablet_server(faulted_follower_idx);
  CHECK_OK(cluster_->SetFlag(
      faulted_follower, "TEST_fault_crash_in_split_after_log_copied", "1.0"));

  CHECK_OK(SplitTablet(tablet_id));
  CHECK_OK(cluster_->WaitForTSToCrash(faulted_follower));

  CHECK_OK(faulted_follower->Restart());

  ASSERT_OK(cluster_->WaitForTabletsRunning(faulted_follower, 20s * kTimeMultiplier));
  ASSERT_OK(WaitForTablets(3));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return WriteRows().ok();
  }, 20s * kTimeMultiplier, "Write rows after faulted follower resurrection."));

  auto non_faulted_follower = cluster_->tablet_server(non_faulted_follower_idx);
  non_faulted_follower->Shutdown();
  CHECK_OK(cluster_->WaitForTSToCrash(non_faulted_follower));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return WriteRows().ok();
  }, 20s * kTimeMultiplier, "Write rows after requiring bootstraped node consensus."));

  CHECK_OK(non_faulted_follower->Restart());
}

TEST_F(TabletSplitExternalMiniClusterITest, RemoteBootstrapsFromNodeWithUncommittedSplitOp) {
  // If a new tablet is created and split with one node completely uninvolved, then when that node
  // rejoins it will have to do a remote bootstrap.

  const auto server_to_bootstrap_idx = 0;
  std::vector<int> other_servers;
  for (int i = 0; i < cluster_->num_tablet_servers(); ++i) {
    if (i != server_to_bootstrap_idx) {
      other_servers.push_back(i);
    }
  }

  ASSERT_OK(cluster_->SetFlagOnMasters("unresponsive_ts_rpc_retry_limit", "0"));

  auto server_to_bootstrap = cluster_->tablet_server(server_to_bootstrap_idx);
  server_to_bootstrap->Shutdown();
  CHECK_OK(cluster_->WaitForTSToCrash(server_to_bootstrap));

  CreateSingleTablet();
  CHECK_OK(WriteRows());

  const auto other_server_idx = *other_servers.begin();
  const auto tablet_id = CHECK_RESULT(GetOnlyTabletId(other_server_idx));
  const auto leader_idx = CHECK_RESULT(cluster_->GetTabletLeaderIndex(tablet_id));
  const auto server_to_kill_idx = 3 - leader_idx - server_to_bootstrap_idx;
  auto server_to_kill = cluster_->tablet_server(server_to_kill_idx);

  auto leader = cluster_->tablet_server(leader_idx);
  CHECK_OK(cluster_->SetFlag(
      server_to_kill, "TEST_fault_crash_in_split_before_log_flushed", "1.0"));
  CHECK_OK(cluster_->SetFlag(leader, "TEST_fault_crash_in_split_before_log_flushed", "1.0"));
  CHECK_OK(SplitTablet(tablet_id));

  // The leader is guaranteed to attempt to apply the split operation and crash.
  CHECK_OK(cluster_->WaitForTSToCrash(leader));
  // The other follower may or may not attempt to apply the split operation. We shut it down here so
  // that it cannot be used for remote bootstrap.
  server_to_kill->Shutdown();

  CHECK_OK(leader->Restart());
  CHECK_OK(server_to_bootstrap->Restart());

  ASSERT_OK(cluster_->WaitForTabletsRunning(leader, 20s * kTimeMultiplier));
  ASSERT_OK(cluster_->WaitForTabletsRunning(server_to_bootstrap, 20s * kTimeMultiplier));
  CHECK_OK(server_to_kill->Restart());
  ASSERT_OK(WaitForTablets(3, server_to_bootstrap_idx));
  ASSERT_OK(WaitForTablets(3, leader_idx));
  ASSERT_OK(WaitForTablets(3, server_to_kill_idx));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return WriteRows().ok();
  }, 20s * kTimeMultiplier, "Write rows after split."));

  server_to_kill->Shutdown();
  CHECK_OK(cluster_->WaitForTSToCrash(server_to_kill));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return WriteRows().ok();
  }, 20s * kTimeMultiplier, "Write rows after requiring bootstraped node consensus."));

  CHECK_OK(server_to_kill->Restart());
}

namespace {

PB_ENUM_FORMATTERS(IsolationLevel);

std::string TestParamToString(const testing::TestParamInfo<IsolationLevel>& isolation_level) {
  return ToString(isolation_level.param);
}

} // namespace

INSTANTIATE_TEST_CASE_P(
    TabletSplitITest,
    TabletSplitITestWithIsolationLevel,
    ::testing::ValuesIn(GetAllPbEnumValues<IsolationLevel>()),
    TestParamToString);

}  // namespace yb
