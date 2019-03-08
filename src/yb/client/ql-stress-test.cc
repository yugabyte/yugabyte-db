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

#include <boost/scope_exit.hpp>

#include "yb/client/client.h"
#include "yb/client/ql-dml-test-base.h"
#include "yb/client/table_handle.h"

#include "yb/consensus/raft_consensus.h"
#include "yb/consensus/retryable_requests.h"

#include "yb/docdb/consensus_frontier.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/yql/cql/ql/util/statement_result.h"
#include "yb/util/bfql/gen_opcodes.h"

#include "yb/rocksdb/utilities/checkpoint.h"
#include "yb/rocksdb/metadata.h"

#include "yb/tablet/tablet_options.h"
#include "yb/docdb/docdb_rocksdb_util.h"

DECLARE_double(respond_write_failed_probability);
DECLARE_bool(detect_duplicates_for_retryable_requests);
DECLARE_int32(raft_heartbeat_interval_ms);
DECLARE_bool(combine_batcher_errors);
DECLARE_bool(flush_rocksdb_on_shutdown);

using namespace std::literals;

using rocksdb::checkpoint::CreateCheckpoint;
using rocksdb::UserFrontierPtr;
using yb::tablet::TabletOptions;
using yb::docdb::InitRocksDBOptions;

namespace yb {
namespace client {

namespace {

const std::string kValueColumn = "v";

}

class QLStressTest : public QLDmlTestBase {
 public:
  QLStressTest() {
  }

  void SetUp() override {
    QLDmlTestBase::SetUp();

    YBSchemaBuilder b;
    InitSchemaBuilder(&b);

    ASSERT_OK(table_.Create(kTableName, NumTablets(), client_.get(), &b));
  }

  virtual int NumTablets() {
    return CalcNumTablets(3);
  }

  virtual void InitSchemaBuilder(YBSchemaBuilder* builder) {
    builder->AddColumn("h")->Type(INT32)->HashPrimaryKey()->NotNull();
    builder->AddColumn(kValueColumn)->Type(STRING);
  }

  YBqlWriteOpPtr InsertRow(const YBSessionPtr& session, int32_t key, const std::string& value) {
    auto op = table_.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, key);
    table_.AddStringColumnValue(req, kValueColumn, value);
    EXPECT_OK(session->Apply(op));
    return op;
  }

  CHECKED_STATUS WriteRow(const YBSessionPtr& session, int32_t key, const std::string& value) {
    auto op = InsertRow(session, key, value);
    RETURN_NOT_OK(session->Flush());
    if (op->response().status() != QLResponsePB::YQL_STATUS_OK) {
      return STATUS_FORMAT(
          RemoteError, "Write failed: $0", QLResponsePB::QLStatus_Name(op->response().status()));
    }

    return Status::OK();
  }

  YBqlReadOpPtr SelectRow(const YBSessionPtr& session, int32_t key) {
    auto op = table_.NewReadOp();
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, key);
    table_.AddColumns({kValueColumn}, req);
    EXPECT_OK(session->Apply(op));
    return op;
  }

  Result<QLValue> ReadRow(const YBSessionPtr& session, int32_t key) {
    auto op = SelectRow(session, key);
    RETURN_NOT_OK(session->Flush());
    if (op->response().status() != QLResponsePB::YQL_STATUS_OK) {
      return STATUS_FORMAT(
          RemoteError, "Read failed: $0", QLResponsePB::QLStatus_Name(op->response().status()));
    }
    auto rowblock = ql::RowsResult(op.get()).GetRowBlock();
    if (rowblock->row_count() != 1) {
      return STATUS_FORMAT(NotFound, "Bad count for $0, count: $1", key, rowblock->row_count());
    }
    const auto& row = rowblock->row(0);
    return row.column(0);
  }

  void VerifyFlushedFrontiers();

  void TestRetryWrites(bool restarts);

  bool CheckRetryableRequestsCounts(size_t* total_entries, size_t* total_leaders);

  TableHandle table_;

  int checkpoint_index_ = 0;
};

bool QLStressTest::CheckRetryableRequestsCounts(size_t* total_entries, size_t* total_leaders) {
  *total_entries = 0;
  *total_leaders = 0;
  bool result = true;
  size_t replicated_limit = FLAGS_detect_duplicates_for_retryable_requests ? 1 : 0;
  for (int i = 0; i != cluster_->num_tablet_servers(); ++i) {
    auto peers = cluster_->GetTabletPeers(i);
    for (const auto& peer : peers) {
      auto leader = peer->LeaderStatus() != consensus::LeaderStatus::NOT_LEADER;
      size_t tablet_entries = peer->tablet()->TEST_CountRocksDBRecords();
      auto raft_consensus = down_cast<consensus::RaftConsensus*>(peer->consensus());
      auto request_counts = raft_consensus->TEST_CountRetryableRequests();
      LOG(INFO) << "T " << peer->tablet()->tablet_id() << " P " << peer->permanent_uuid()
                << ", entries: " << tablet_entries
                << ", running: " << request_counts.running
                << ", replicated: " << request_counts.replicated
                << ", leader: " << leader;
      if (leader) {
        *total_entries += tablet_entries;
        ++*total_leaders;
      }
      // Last write request could be rejected as duplicate, so followers would not be able to
      // cleanup replicated requests.
      if (request_counts.running != 0 || (leader && request_counts.replicated > replicated_limit)) {
        result = false;
      }
    }
  }

  return result;
}

void QLStressTest::TestRetryWrites(bool restarts) {
  const size_t kConcurrentWrites = 5;

  SetAtomicFlag(0.25, &FLAGS_respond_write_failed_probability);

  std::vector<std::thread> write_threads;
  std::atomic<int32_t> key_source(0);
  std::atomic<bool> stop_requested(false);
  while (write_threads.size() < kConcurrentWrites) {
    write_threads.emplace_back([this, &key_source, &stop_requested] {
      auto session = NewSession();
      while (!stop_requested.load(std::memory_order_acquire)) {
        int32_t key = key_source.fetch_add(1, std::memory_order_acq_rel);

        auto op = InsertRow(session, key, Format("value_$0", key));
        auto flush_status = session->Flush();
        if (flush_status.ok()) {
          ASSERT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_OK);
          continue;
        }
        ASSERT_TRUE(flush_status.IsIOError()) << "Status: " << flush_status;
        ASSERT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_RUNTIME_ERROR);
        ASSERT_EQ(op->response().error_message(), "Duplicate request");
      }
    });
  }

  std::thread restart_thread;
  if (restarts) {
    restart_thread = std::thread([this, &stop_requested] {
      int it = 0;
      while (!stop_requested.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(5s);
        ASSERT_OK(cluster_->mini_tablet_server(++it % cluster_->num_tablet_servers())->Restart());
      }
    });
  }

  std::this_thread::sleep_for(restarts ? 60s : 15s);

  stop_requested.store(true, std::memory_order_release);

  for (auto& thread : write_threads) {
    thread.join();
  }

  if (restart_thread.joinable()) {
    restart_thread.join();
  }

  int written_keys = key_source.load(std::memory_order_acquire);
  auto session = NewSession();
  for (int key = 0; key != written_keys; ++key) {
    auto value = ASSERT_RESULT(ReadRow(session, key));
    ASSERT_EQ(value.string_value(), Format("value_$0", key));
  }

  size_t total_entries = 0;
  size_t total_leaders = 0;
  ASSERT_OK(WaitFor(
      std::bind(&QLStressTest::CheckRetryableRequestsCounts, this, &total_entries, &total_leaders),
      15s, "Retryable requests cleanup"));

  ASSERT_EQ(total_leaders, table_.table()->GetPartitions().size());

  // We have 2 entries per row.
  if (FLAGS_detect_duplicates_for_retryable_requests) {
    ASSERT_EQ(total_entries, written_keys * 2);
  } else {
    // If duplicate request tracking is disabled, then total_entries should be greater than
    // written keys, otherwise test does not work.
    ASSERT_GT(total_entries, written_keys * 2);
  }

  ASSERT_GE(written_keys, RegularBuildVsSanitizers(100, 40));
}

TEST_F(QLStressTest, RetryWrites) {
  FLAGS_detect_duplicates_for_retryable_requests = true;
  TestRetryWrites(false /* restarts */);
}

TEST_F(QLStressTest, RetryWritesWithRestarts) {
  FLAGS_detect_duplicates_for_retryable_requests = true;
  TestRetryWrites(true /* restarts */);
}

TEST_F(QLStressTest, RetryWritesDisabled) {
  FLAGS_detect_duplicates_for_retryable_requests = false;
  TestRetryWrites(false /* restarts */);
}

class QLStressTestIntValue : public QLStressTest {
 private:
  void InitSchemaBuilder(YBSchemaBuilder* builder) override {
    builder->AddColumn("h")->Type(INT32)->HashPrimaryKey()->NotNull();
    builder->AddColumn(kValueColumn)->Type(INT64);
  }
};

// This test does 100 concurrent increments of the same row.
// It is expected that resulting value will be equal to 100.
TEST_F_EX(QLStressTest, Increment, QLStressTestIntValue) {
  const auto kIncrements = 100;
  const auto kKey = 1;

  auto session = NewSession();
  {
    auto op = table_.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, kKey);
    table_.AddInt64ColumnValue(req, kValueColumn, 0);
    ASSERT_OK(session->ApplyAndFlush(op));
    ASSERT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_OK);
  }

  std::vector<YBqlWriteOpPtr> write_ops;
  std::vector<std::shared_future<Status>> futures;

  auto value_column_id = table_.ColumnId(kValueColumn);
  for (int i = 0; i != kIncrements; ++i) {
    auto op = table_.NewWriteOp(QLWriteRequestPB::QL_STMT_UPDATE);
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, kKey);
    req->mutable_column_refs()->add_ids(value_column_id);
    auto* column_value = req->add_column_values();
    column_value->set_column_id(value_column_id);
    auto* bfcall = column_value->mutable_expr()->mutable_bfcall();
    bfcall->set_opcode(to_underlying(bfql::BFOpcode::OPCODE_AddI64I64_80));
    bfcall->add_operands()->set_column_id(value_column_id);
    bfcall->add_operands()->mutable_value()->set_int64_value(1);
    write_ops.push_back(op);
  }

  for (const auto& op : write_ops) {
    ASSERT_OK(session->Apply(op));
    futures.push_back(session->FlushFuture());
  }

  for (size_t i = 0; i != write_ops.size(); ++i) {
    ASSERT_OK(futures[i].get());
    ASSERT_EQ(write_ops[i]->response().status(), QLResponsePB::YQL_STATUS_OK);
  }

  auto value = ASSERT_RESULT(ReadRow(session, kKey));
  ASSERT_EQ(value.int64_value(), kIncrements) << value.ToString();
}

class QLStressTestSingleTablet : public QLStressTest {
 private:
  int NumTablets() override {
    return 1;
  }
};

class QLStressTestThreeTablets : public QLStressTest {
 private:
  int NumTablets() override {
    return 3;
  }
};

// This test has the following scenario:
// Add some operations to the old leader, but don't add to other nodes.
// Switch leadership to a new leader, but don't accept updates from new leader by old leader.
// Also don't replicate no op by the new leader.
// Switch leadership back to the old leader.
// New leader should successfully accept old operations from old leader.
TEST_F_EX(QLStressTest, ShortTimeLeaderDoesNotReplicateNoOp, QLStressTestSingleTablet) {
  auto session = NewSession();
  ASSERT_OK(WriteRow(session, 0, "value0"));

  auto leaders = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
  ASSERT_EQ(1, leaders.size());
  auto old_leader = leaders[0];

  auto followers = ListTabletPeers(cluster_.get(), ListPeersFilter::kNonLeaders);
  ASSERT_EQ(2, followers.size());
  tablet::TabletPeerPtr temp_leader = followers[0];
  tablet::TabletPeerPtr always_follower = followers[1];

  ASSERT_OK(WaitFor([old_leader, always_follower]() -> Result<bool> {
    auto leader_op_id = VERIFY_RESULT(old_leader->consensus()->GetLastReceivedOpId());
    auto follower_op_id = VERIFY_RESULT(always_follower->consensus()->GetLastReceivedOpId());
    return follower_op_id.index() == leader_op_id.index();
  }, 5s, "Follower catch up"));

  for (const auto& follower : followers) {
    down_cast<consensus::RaftConsensus*>(follower->consensus())->TEST_RejectMode(
        consensus::RejectMode::kAll);
  }

  InsertRow(session, 1, "value1");
  auto flush_future = session->FlushFuture();

  InsertRow(session, 2, "value2");
  auto flush_future2 = session->FlushFuture();

  // Give leader some time to receive operation.
  // TODO wait for specific event.
  std::this_thread::sleep_for(1s);

  LOG(INFO) << "Step down old leader " << old_leader->permanent_uuid()
            << " in favor of " << temp_leader->permanent_uuid();

  ASSERT_OK(StepDown(old_leader, temp_leader->permanent_uuid(), ForceStepDown::kFalse));

  down_cast<consensus::RaftConsensus*>(old_leader->consensus())->TEST_RejectMode(
      consensus::RejectMode::kAll);
  down_cast<consensus::RaftConsensus*>(temp_leader->consensus())->TEST_RejectMode(
      consensus::RejectMode::kNone);
  down_cast<consensus::RaftConsensus*>(always_follower->consensus())->TEST_RejectMode(
      consensus::RejectMode::kNonEmpty);

  ASSERT_OK(WaitForLeaderOfSingleTablet(
      cluster_.get(), temp_leader, 20s, "Waiting for new leader"));

  // Give new leader some time to request lease.
  // TODO wait for specific event.
  std::this_thread::sleep_for(3s);
  auto temp_leader_safe_time = temp_leader->tablet()->SafeTime(tablet::RequireLease::kTrue);
  LOG(INFO) << "Safe time: " << temp_leader_safe_time;
  ASSERT_FALSE(temp_leader_safe_time.is_valid());

  LOG(INFO) << "Transferring leadership from " << temp_leader->permanent_uuid()
            << " back to " << old_leader->permanent_uuid();
  ASSERT_OK(StepDown(temp_leader, old_leader->permanent_uuid(), ForceStepDown::kTrue));

  ASSERT_OK(WaitForLeaderOfSingleTablet(
      cluster_.get(), old_leader, 20s, "Waiting old leader to restore leadership"));

  down_cast<consensus::RaftConsensus*>(always_follower->consensus())->TEST_RejectMode(
      consensus::RejectMode::kNone);

  ASSERT_OK(WriteRow(session, 3, "value3"));

  ASSERT_OK(flush_future.get());
  ASSERT_OK(flush_future2.get());
}

namespace {

void VerifyFlushedFrontier(const UserFrontierPtr& frontier, OpId* op_id) {
  ASSERT_TRUE(frontier);
  if (frontier) {
    *op_id = down_cast<docdb::ConsensusFrontier*>(frontier.get())->op_id();
    ASSERT_GT(op_id->term, 0);
    ASSERT_GT(op_id->index, 0);
  }
}

}  // anonymous namespace
void QLStressTest::VerifyFlushedFrontiers() {
  for (const auto& mini_tserver : cluster_->mini_tablet_servers()) {
    auto peers = mini_tserver->server()->tablet_manager()->GetTabletPeers();
    for (const auto& peer : peers) {
      rocksdb::DB* db = peer->tablet()->TEST_db();
      OpId op_id;
      ASSERT_NO_FATALS(VerifyFlushedFrontier(db->GetFlushedFrontier(), &op_id));

      // Also check that if we checkpoint this DB and open the checkpoint separately, the
      // flushed frontier non-zero as well.
      std::string checkpoint_dir;
      ASSERT_OK(Env::Default()->GetTestDirectory(&checkpoint_dir));
      checkpoint_dir += Format("/checkpoint_$0", checkpoint_index_);
      checkpoint_index_++;

      ASSERT_OK(CreateCheckpoint(db, checkpoint_dir));

      rocksdb::Options options;

      InitRocksDBOptions(&options, "test_tablet", nullptr, TabletOptions());
      std::unique_ptr<rocksdb::DB> checkpoint_db;
      rocksdb::DB* checkpoint_db_raw_ptr = nullptr;

      options.create_if_missing = false;
      ASSERT_OK(rocksdb::DB::Open(options, checkpoint_dir, &checkpoint_db_raw_ptr));
      checkpoint_db.reset(checkpoint_db_raw_ptr);
      OpId checkpoint_op_id;
      ASSERT_NO_FATALS(
          VerifyFlushedFrontier(checkpoint_db->GetFlushedFrontier(), &checkpoint_op_id));
      ASSERT_OK(Env::Default()->DeleteRecursively(checkpoint_dir));

      ASSERT_LE(op_id, checkpoint_op_id);
    }
  }
}

TEST_F_EX(QLStressTest, FlushCompact, QLStressTestSingleTablet) {
  std::atomic<bool> stop(false);

  std::thread writer([this, &stop] {
    auto session = NewSession();
    int key = 0;
    std::string value = "value_";
    while (!stop.load(std::memory_order_acquire)) {
      ASSERT_OK(WriteRow(session, key, value + std::to_string(key)));
      ++key;
    }
  });

  BOOST_SCOPE_EXIT(&stop, &writer) {
    stop.store(true);
    writer.join();
  } BOOST_SCOPE_EXIT_END;

  auto start_time = MonoTime::Now();
  const auto kTimeout = MonoDelta::FromSeconds(60);
  int num_iter = 0;
  while (MonoTime::Now() - start_time < kTimeout) {
    ++num_iter;
    std::this_thread::sleep_for(1s);
    ASSERT_OK(cluster_->FlushTablets());
    ASSERT_NO_FATALS(VerifyFlushedFrontiers());
    std::this_thread::sleep_for(1s);
    auto compact_status = cluster_->CompactTablets();
    LOG_IF(INFO, !compact_status.ok()) << "Compaction failed: " << compact_status;
    ASSERT_NO_FATALS(VerifyFlushedFrontiers());
  }
  ASSERT_GE(num_iter, 5);
}

TEST_F_EX(QLStressTest, LogReplayAfterCompaction, QLStressTestSingleTablet) {
  SetAtomicFlag(false, &FLAGS_flush_rocksdb_on_shutdown);
  auto session = NewSession();
  int num_writes = 1000;
  for (int j = 0; j < num_writes; ++j) {
    ASSERT_OK(WriteRow(session, j, "value" + std::to_string(j)));
  }
  vector<OpId> op_ids;
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync));
  ASSERT_NO_FATALS(VerifyFlushedFrontiers());
  for (const auto& mini_tserver : cluster_->mini_tablet_servers()) {
    auto peers = mini_tserver->server()->tablet_manager()->GetTabletPeers();
    for (const auto &peer : peers) {
      rocksdb::DB *db = peer->tablet()->TEST_db();
      OpId op_id;
      ASSERT_NO_FATALS(VerifyFlushedFrontier(db->GetFlushedFrontier(), &op_id));
      op_ids.push_back(op_id);
    }
  }

  ASSERT_OK(cluster_->CompactTablets());
  int num_unflushed_writes = 2000;
  for (int j = 100; j < 100+num_unflushed_writes; ++j) {
    ASSERT_OK(WriteRow(session, j, "value" + std::to_string(j)));
  }
  ASSERT_OK(cluster_->RestartSync());
  int i = 0;
  for (const auto& mini_tserver : cluster_->mini_tablet_servers()) {
    auto peers = mini_tserver->server()->tablet_manager()->GetTabletPeers();
    for (const auto &peer : peers) {
      rocksdb::DB *db = peer->tablet()->TEST_db();
      OpId op_id;
      ASSERT_NO_FATALS(VerifyFlushedFrontier(db->GetFlushedFrontier(), &op_id));
      OpId expected = op_ids[i];
      ASSERT_EQ(op_id, expected);
      i++;
    }
    ASSERT_WITHIN_INCLUSIVE(num_unflushed_writes - 5, num_unflushed_writes + 5,
        mini_tserver->server()->tablet_manager()->num_replayed());
  }
}

TEST_F_EX(QLStressTest, LogReplayAfterCompactionThreeTablets, QLStressTestThreeTablets) {
  SetAtomicFlag(false, &FLAGS_flush_rocksdb_on_shutdown);
  auto session = NewSession();
  int num_writes = 1000;
  for (int j = 0; j < num_writes; ++j) {
    ASSERT_OK(WriteRow(session, j, "value" + std::to_string(j)));
  }
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync));
  ASSERT_NO_FATALS(VerifyFlushedFrontiers());
  for (const auto& mini_tserver : cluster_->mini_tablet_servers()) {
    auto peers = mini_tserver->server()->tablet_manager()->GetTabletPeers();
    for (const auto &peer : peers) {
      rocksdb::DB *db = peer->tablet()->TEST_db();
      OpId op_id;
      ASSERT_NO_FATALS(VerifyFlushedFrontier(db->GetFlushedFrontier(), &op_id));
    }
  }
  ASSERT_OK(cluster_->CompactTablets());
  int num_unflushed_writes = 2000;
  for (int j = 100; j < 100+num_unflushed_writes; ++j) {
    ASSERT_OK(WriteRow(session, j, "value" + std::to_string(j)));
  }
  ASSERT_OK(cluster_->RestartSync());
  for (const auto& mini_tserver : cluster_->mini_tablet_servers()) {
    ASSERT_WITHIN_INCLUSIVE(num_unflushed_writes - 5, num_unflushed_writes + 5,
                            mini_tserver->server()->tablet_manager()->num_replayed());
  }
}

// The scenario of this test is the following:
// We do writes in background.
// Isolate leader for 10 seconds.
// Restore connectivity.
// Check that old leader was able to catch up after the partition is healed.
TEST_F_EX(QLStressTest, OldLeaderCatchUpAfterNetworkPartition, QLStressTestSingleTablet) {
  FLAGS_combine_batcher_errors = true;

  tablet::TabletPeer* leader_peer = nullptr;
  std::atomic<int> key(0);
  {
    std::atomic<bool> stop(false);

    std::thread writer([this, &stop, &key] {
      auto session = NewSession();
      std::string value_prefix = "value_";
      while (!stop.load(std::memory_order_acquire)) {
        ASSERT_OK(WriteRow(session, key, value_prefix + std::to_string(key)));
        ++key;
      }
    });

    BOOST_SCOPE_EXIT(&stop, &writer) {
      stop.store(true);
      writer.join();
    } BOOST_SCOPE_EXIT_END;

    tserver::MiniTabletServer* leader = nullptr;
    for (int i = 0; i != cluster_->num_tablet_servers(); ++i) {
      auto current = cluster_->mini_tablet_server(i);
      auto peers = current->server()->tablet_manager()->GetTabletPeers();
      ASSERT_EQ(peers.size(), 1);
      if (peers.front()->LeaderStatus() != consensus::LeaderStatus::NOT_LEADER) {
        leader = current;
        leader_peer = peers.front().get();
        break;
      }
    }

    ASSERT_NE(leader, nullptr);

    std::this_thread::sleep_for(5s * yb::kTimeMultiplier);

    auto pre_isolate_op_id = leader_peer->GetLatestLogEntryOpId();
    LOG(INFO) << "Isolate, last op id: " << pre_isolate_op_id << ", key: " << key;
    ASSERT_EQ(pre_isolate_op_id.term, 1);
    ASSERT_GT(pre_isolate_op_id.index, key);
    leader->SetIsolated(true);
    std::this_thread::sleep_for(10s * yb::kTimeMultiplier);

    auto pre_restore_op_id = leader_peer->GetLatestLogEntryOpId();
    LOG(INFO) << "Restore, last op id: " << pre_restore_op_id << ", key: " << key;
    ASSERT_EQ(pre_restore_op_id.term, 1);
    ASSERT_GE(pre_restore_op_id.index, pre_isolate_op_id.index);
    ASSERT_LE(pre_restore_op_id.index, pre_isolate_op_id.index + 10);
    leader->SetIsolated(false);
    std::this_thread::sleep_for(5s * yb::kTimeMultiplier);
  }

  ASSERT_OK(WaitFor([leader_peer, &key] {
    return leader_peer->GetLatestLogEntryOpId().index > key;
  }, 5s, "Old leader has enough operations"));

  auto finish_op_id = leader_peer->GetLatestLogEntryOpId();
  LOG(INFO) << "Finish, last op id: " << finish_op_id << ", key: " << key;
  ASSERT_GT(finish_op_id.term, 1);
  ASSERT_GT(finish_op_id.index, key);
}

} // namespace client
} // namespace yb
