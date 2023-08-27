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

#include "yb/bfql/gen_opcodes.h"

#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/ql-dml-test-base.h"
#include "yb/client/rejection_score_source.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_handle.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/consensus/log.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/consensus/retryable_requests.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/dockv/doc_key.h"
#include "yb/docdb/docdb_rocksdb_util.h"

#include "yb/rocksdb/metadata.h"
#include "yb/rocksdb/utilities/checkpoint.h"

#include "yb/rpc/messenger.h"

#include "yb/server/hybrid_clock.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_options.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/debug-util.h"
#include "yb/util/format.h"
#include "yb/util/metrics.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/cql/ql/util/statement_result.h"

using std::string;

DECLARE_bool(TEST_combine_batcher_errors);
DECLARE_bool(allow_preempting_compactions);
DECLARE_bool(detect_duplicates_for_retryable_requests);
DECLARE_bool(enable_ondisk_compression);
DECLARE_bool(ycql_enable_packed_row);
DECLARE_double(TEST_respond_write_failed_probability);
DECLARE_double(transaction_max_missed_heartbeat_periods);
DECLARE_int32(TEST_max_write_waiters);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(log_cache_size_limit_mb);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_int32(raft_heartbeat_interval_ms);
DECLARE_int32(retryable_request_range_time_limit_secs);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_int32(rocksdb_level0_slowdown_writes_trigger);
DECLARE_int32(rocksdb_max_background_compactions);
DECLARE_int32(rocksdb_universal_compaction_min_merge_width);
DECLARE_int32(rocksdb_universal_compaction_size_ratio);
DECLARE_int64(db_write_buffer_size);
DECLARE_int64(remote_bootstrap_rate_limit_bytes_per_sec);
DECLARE_int64(rocksdb_compact_flush_rate_limit_bytes_per_sec);
DECLARE_int64(transaction_rpc_timeout_ms);
DECLARE_uint64(log_segment_size_bytes);
DECLARE_uint64(sst_files_hard_limit);
DECLARE_uint64(sst_files_soft_limit);

METRIC_DECLARE_counter(majority_sst_files_rejections);

using namespace std::literals;

using rocksdb::checkpoint::CreateCheckpoint;
using rocksdb::UserFrontierPtr;
using yb::tablet::TabletOptions;
using yb::docdb::InitRocksDBOptions;

DECLARE_bool(enable_ysql);

namespace yb {
namespace client {

namespace {

const std::string kValueColumn = "v";

}

class QLStressTest : public QLDmlTestBase<MiniCluster> {
 public:
  QLStressTest() {
  }

  void SetUp() override {
    // To prevent automatic creation of the transaction status table.
    SetAtomicFlag(false, &FLAGS_enable_ysql);

    ASSERT_NO_FATALS(QLDmlTestBase::SetUp());

    YBSchemaBuilder b;
    InitSchemaBuilder(&b);
    CompleteSchemaBuilder(&b);

    ASSERT_OK(table_.Create(kTableName, NumTablets(), client_.get(), &b));
    ASSERT_OK(WaitForTabletLeaders());
  }

  virtual void CompleteSchemaBuilder(YBSchemaBuilder* b) {}

  virtual int NumTablets() {
    return CalcNumTablets(3);
  }

  virtual void InitSchemaBuilder(YBSchemaBuilder* builder) {
    builder->AddColumn("h")->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
    builder->AddColumn(kValueColumn)->Type(DataType::STRING);
  }

  Status WaitForTabletLeaders() {
    const MonoTime deadline = MonoTime::Now() + 10s * kTimeMultiplier;
    for (const auto& tablet_id : ListTabletIdsForTable(cluster_.get(), table_->id())) {
      RETURN_NOT_OK(WaitUntilTabletHasLeader(cluster_.get(), tablet_id, deadline));
    }
    return Status::OK();
  }

  YBqlWriteOpPtr InsertRow(const YBSessionPtr& session,
                           const TableHandle& table,
                           int32_t key,
                           const std::string& value) {
    auto op = table.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, key);
    table.AddStringColumnValue(req, kValueColumn, value);
    session->Apply(op);
    return op;
  }

  Status WriteRow(const YBSessionPtr& session,
                  const TableHandle& table,
                  int32_t key,
                  const std::string& value) {
    auto op = InsertRow(session, table, key, value);
    RETURN_NOT_OK(session->TEST_Flush());
    if (op->response().status() != QLResponsePB::YQL_STATUS_OK) {
      return STATUS_FORMAT(
          RemoteError, "Write failed: $0", QLResponsePB::QLStatus_Name(op->response().status()));
    }

    return Status::OK();
  }

  YBqlReadOpPtr SelectRow(const YBSessionPtr& session, const TableHandle& table, int32_t key) {
    auto op = table.NewReadOp();
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, key);
    table.AddColumns({kValueColumn}, req);
    session->Apply(op);
    return op;
  }

  Result<QLValue> ReadRow(const YBSessionPtr& session, const TableHandle& table, int32_t key) {
    auto op = SelectRow(session, table, key);
    RETURN_NOT_OK(session->TEST_Flush());
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

  YBqlWriteOpPtr InsertRow(const YBSessionPtr& session,
                           int32_t key,
                           const std::string& value) {
    return QLStressTest::InsertRow(session, table_, key, value);
  }

  Status WriteRow(const YBSessionPtr& session,
                  int32_t key,
                  const std::string& value) {
    return QLStressTest::WriteRow(session, table_, key, value);
  }

  YBqlReadOpPtr SelectRow(const YBSessionPtr& session, int32_t key) {
    return QLStressTest::SelectRow(session, table_, key);
  }

  Result<QLValue> ReadRow(const YBSessionPtr& session, int32_t key) {
    return QLStressTest::ReadRow(session, table_, key);
  }

  TransactionManager CreateTxnManager();

  void VerifyFlushedFrontiers();

  void TestRetryWrites(bool restarts);

  bool CheckRetryableRequestsCountsAndLeaders(size_t total_leaders, size_t* total_entries);

  void AddWriter(
      std::string value_prefix, std::atomic<int>* key, TestThreadHolder* thread_holder,
      const std::chrono::nanoseconds& sleep_duration = std::chrono::nanoseconds(),
      bool allow_failures = false, TransactionManager* txn_manager = nullptr,
      double transactional_write_probability = 0.0);

  void TestWriteRejection();

  TableHandle table_;

  int checkpoint_index_ = 0;
};

/*
 * Create a lot of tables and check that each of them are usable (can read/write to them).
 * Test enough rows/keys to ensure that most tablets will be hit.
 */
TEST_F(QLStressTest, LargeNumberOfTables) {
  int num_tables = NonTsanVsTsan(20, 10);
  int num_tablets_per_table = NonTsanVsTsan(3, 1);
  auto session = NewSession();
  for (int i = 0; i < num_tables; i++) {
    YBSchemaBuilder b;
    InitSchemaBuilder(&b);
    CompleteSchemaBuilder(&b);
    TableHandle table;
    client::YBTableName table_name(
        YQL_DATABASE_CQL, "my_keyspace", "ql_client_test_table_" + std::to_string(i));
    ASSERT_OK(table.Create(table_name, num_tablets_per_table, client_.get(), &b));

    int num_rows = num_tablets_per_table * 5;
    for (int key = i; key < i + num_rows; key++) {
      string value = "value_" + std::to_string(key);
      ASSERT_OK(WriteRow(session, table, key, value));
      auto read_value = ASSERT_RESULT(ReadRow(session, table, key));
      ASSERT_EQ(read_value.string_value(), value) << read_value.ToString();
    }
  }
}

bool QLStressTest::CheckRetryableRequestsCountsAndLeaders(
      size_t expected_leaders, size_t* total_entries) {
  size_t total_leaders = 0;
  *total_entries = 0;
  bool result = true;
  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
  for (const auto& peer : peers) {
    auto leader = peer->LeaderStatus() != consensus::LeaderStatus::NOT_LEADER;
    if (!peer->tablet() || peer->tablet()->metadata()->table_id() != table_.table()->id()) {
      continue;
    }
    const auto tablet_entries = EXPECT_RESULT(peer->tablet()->TEST_CountRegularDBRecords());
    auto raft_consensus = EXPECT_RESULT(peer->GetRaftConsensus());
    auto request_counts = raft_consensus->TEST_CountRetryableRequests();
    LOG(INFO) << "T " << peer->tablet()->tablet_id() << " P " << peer->permanent_uuid()
              << ", entries: " << tablet_entries
              << ", running: " << request_counts.running
              << ", replicated: " << request_counts.replicated
              << ", leader: " << leader
              << ", term: " << raft_consensus->LeaderTerm();
    if (leader) {
      *total_entries += tablet_entries;
      ++total_leaders;
    }

    // When duplicates detection is enabled, we use the global min running request id shared by
    // all tablets for the client so that cleanup of requests on one tablet can be withheld by
    // requests on a different tablet. The upper bound of residual requests is not deterministic.
    if (request_counts.running != 0 ||
        (!FLAGS_detect_duplicates_for_retryable_requests &&
         leader && request_counts.replicated > 0)) {
      result = false;
    }
  }

  if (total_leaders != expected_leaders) {
    LOG(INFO) << "Expected " << expected_leaders << " leaders, found " << total_leaders;
    return false;
  }

  if (result && FLAGS_detect_duplicates_for_retryable_requests) {
    auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
    for (const auto& peer : peers) {
      if (peer->tablet()->metadata()->table_id() != table_.table()->id()) {
        continue;
      }
      auto db = peer->tablet()->regular_db();
      rocksdb::ReadOptions read_opts;
      read_opts.query_id = rocksdb::kDefaultQueryId;
      std::unique_ptr<rocksdb::Iterator> iter(db->NewIterator(read_opts));
      std::unordered_map<std::string, std::string> keys;

      for (iter->SeekToFirst(); EXPECT_RESULT(iter->CheckedValid()); iter->Next()) {
        Slice key = iter->key();
        EXPECT_OK(DocHybridTime::DecodeFromEnd(&key));
        auto emplace_result = keys.emplace(key.ToBuffer(), iter->key().ToBuffer());
        if (!emplace_result.second) {
          LOG(ERROR)
              << "Duplicate key: " << dockv::SubDocKey::DebugSliceToString(iter->key())
              << " vs " << dockv::SubDocKey::DebugSliceToString(emplace_result.first->second);
        }
      }
    }
  }

  return result;
}

TransactionManager QLStressTest::CreateTxnManager() {
  server::ClockPtr clock(new server::HybridClock(WallClock()));
  EXPECT_OK(clock->Init());
  return TransactionManager(client_.get(), clock, client::LocalTabletFilter());
}

void QLStressTest::TestRetryWrites(bool restarts) {
  const size_t kConcurrentWrites = 5;
  // Used only when table is transactional.
  const double kTransactionalWriteProbability = 0.5;

  SetAtomicFlag(0.25, &FLAGS_TEST_respond_write_failed_probability);

  const bool transactional = table_.table()->schema().table_properties().is_transactional();
  boost::optional<TransactionManager> txn_manager;
  if (transactional) {
    txn_manager = CreateTxnManager();
  }

  TestThreadHolder thread_holder;
  std::atomic<int32_t> key_source(0);
  for (int i = 0; i != kConcurrentWrites; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &key_source, &stop_requested = thread_holder.stop_flag(),
         &txn_manager, kTransactionalWriteProbability] {
      auto session = NewSession();
      while (!stop_requested.load(std::memory_order_acquire)) {
        int32_t key = key_source.fetch_add(1, std::memory_order_acq_rel);
        YBTransactionPtr txn;
        if (txn_manager &&
            RandomActWithProbability(kTransactionalWriteProbability)) {
          txn = std::make_shared<YBTransaction>(txn_manager.get_ptr());
          ASSERT_OK(txn->Init(IsolationLevel::SNAPSHOT_ISOLATION));
          session->SetTransaction(txn);
        } else {
          session->SetTransaction(nullptr);
        }

        auto op = InsertRow(session, key, Format("value_$0", key));
        auto flush_status = session->TEST_FlushAndGetOpsErrors();
        const auto& status = flush_status.status;
        if (status.ok()) {
          ASSERT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_OK);

          if (txn) {
            auto commit_status = txn->CommitFuture().get();
            if (!commit_status.ok()) {
              LOG(INFO) << "Commit failed, key: " << key << ", txn: " << txn->id()
                        << ", commit failed: " << commit_status;
              ASSERT_TRUE(commit_status.IsExpired());
            }
          }
          continue;
        }
        ASSERT_TRUE(status.IsIOError()) << "Status: " << AsString(status);
        ASSERT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_RUNTIME_ERROR);
        ASSERT_EQ(op->response().error_message(), "Duplicate request");
      }
    });
  }

  if (restarts) {
    thread_holder.AddThread(RestartsThread(cluster_.get(), 5s, &thread_holder.stop_flag()));
  }

  thread_holder.WaitAndStop(restarts ? 60s : 15s);

  int written_keys = key_source.load(std::memory_order_acquire);
  auto session = NewSession();
  for (int key = 0; key != written_keys; ++key) {
    auto value = ASSERT_RESULT(ReadRow(session, key));
    ASSERT_EQ(value.string_value(), Format("value_$0", key));
  }

  size_t total_entries = 0;
  size_t expected_leaders = table_.table()->GetPartitionCount();
  ASSERT_OK(WaitFor(
      std::bind(&QLStressTest::CheckRetryableRequestsCountsAndLeaders, this,
                expected_leaders, &total_entries),
      15s, "Retryable requests cleanup and leader wait"));

  size_t entries_per_row = FLAGS_ycql_enable_packed_row ? 1 : 2;
  if (FLAGS_detect_duplicates_for_retryable_requests) {
    ASSERT_EQ(total_entries, written_keys * entries_per_row);
  } else {
    // If duplicate request tracking is disabled, then total_entries should be greater than
    // written keys, otherwise test does not work.
    ASSERT_GT(total_entries, written_keys * entries_per_row);
  }

  ASSERT_GE(written_keys, RegularBuildVsSanitizers(100, 40));
}

TEST_F(QLStressTest, RetryWrites) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_detect_duplicates_for_retryable_requests) = true;
  TestRetryWrites(false /* restarts */);
}

TEST_F(QLStressTest, RetryWritesWithRestarts) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_detect_duplicates_for_retryable_requests) = true;
  TestRetryWrites(true /* restarts */);
}

void SetTransactional(YBSchemaBuilder* builder) {
  TableProperties table_properties;
  table_properties.SetTransactional(true);
  builder->SetTableProperties(table_properties);
}

class QLTransactionalStressTest : public QLStressTest {
 public:
  void SetUp() override {
    FLAGS_transaction_rpc_timeout_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(1min).count();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_max_missed_heartbeat_periods) = 1000000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_retryable_request_range_time_limit_secs) = 600;
    ASSERT_NO_FATALS(QLStressTest::SetUp());
  }

  void CompleteSchemaBuilder(YBSchemaBuilder* builder) override {
    SetTransactional(builder);
  }
};

TEST_F_EX(QLStressTest, RetryTransactionalWrites, QLTransactionalStressTest) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_detect_duplicates_for_retryable_requests) = true;
  TestRetryWrites(false /* restarts */);
}

TEST_F_EX(QLStressTest, RetryTransactionalWritesWithRestarts, QLTransactionalStressTest) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_detect_duplicates_for_retryable_requests) = true;
  TestRetryWrites(true /* restarts */);
}

TEST_F(QLStressTest, RetryWritesDisabled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_detect_duplicates_for_retryable_requests) = false;
  TestRetryWrites(false /* restarts */);
}

class QLStressTestIntValue : public QLStressTest {
 private:
  void InitSchemaBuilder(YBSchemaBuilder* builder) override {
    builder->AddColumn("h")->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
    builder->AddColumn(kValueColumn)->Type(DataType::INT64);
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
    ASSERT_OK(session->TEST_ApplyAndFlush(op));
    ASSERT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_OK);
  }

  std::vector<YBqlWriteOpPtr> write_ops;
  std::vector<std::shared_future<FlushStatus>> futures;

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
    session->Apply(op);
    futures.push_back(session->FlushFuture());
  }

  for (size_t i = 0; i != write_ops.size(); ++i) {
    ASSERT_OK(futures[i].get().status);
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

  ASSERT_OK(WaitFor(
      [old_leader, always_follower]() -> Result<bool> {
        auto leader_op_id = VERIFY_RESULT(old_leader->GetConsensus())->GetLastReceivedOpId();
        auto follower_op_id = VERIFY_RESULT(always_follower->GetConsensus())->GetLastReceivedOpId();
        return follower_op_id == leader_op_id;
      },
      5s, "Follower catch up"));

  for (const auto& follower : followers) {
    ASSERT_RESULT(follower->GetRaftConsensus())->TEST_RejectMode(consensus::RejectMode::kAll);
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

  ASSERT_RESULT(old_leader->GetRaftConsensus())->TEST_RejectMode(consensus::RejectMode::kAll);
  ASSERT_RESULT(temp_leader->GetRaftConsensus())->TEST_RejectMode(consensus::RejectMode::kNone);
  ASSERT_RESULT(always_follower->GetRaftConsensus())
      ->TEST_RejectMode(consensus::RejectMode::kNonEmpty);

  ASSERT_OK(WaitForLeaderOfSingleTablet(
      cluster_.get(), temp_leader, 20s, "Waiting for new leader"));

  // Give new leader some time to request lease.
  // TODO wait for specific event.
  std::this_thread::sleep_for(3s);
  auto temp_leader_safe_time = ASSERT_RESULT(
      temp_leader->tablet()->SafeTime(tablet::RequireLease::kTrue));
  LOG(INFO) << "Safe time: " << temp_leader_safe_time;

  LOG(INFO) << "Transferring leadership from " << temp_leader->permanent_uuid()
            << " back to " << old_leader->permanent_uuid();
  ASSERT_OK(StepDown(temp_leader, old_leader->permanent_uuid(), ForceStepDown::kTrue));

  ASSERT_OK(WaitForLeaderOfSingleTablet(
      cluster_.get(), old_leader, 20s, "Waiting old leader to restore leadership"));

  ASSERT_RESULT(always_follower->GetRaftConsensus())->TEST_RejectMode(consensus::RejectMode::kNone);

  ASSERT_OK(WriteRow(session, 3, "value3"));

  ASSERT_OK(flush_future.get().status);
  ASSERT_OK(flush_future2.get().status);
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
      rocksdb::DB* db = peer->tablet()->regular_db();
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
      auto tablet_options = TabletOptions();
      tablet_options.rocksdb_env = db->GetEnv();
      InitRocksDBOptions(&options, "", nullptr, tablet_options);
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
  std::atomic<int> key;

  TestThreadHolder thread_holder;

  AddWriter("value_", &key, &thread_holder);

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

// The scenario of this test is the following:
// We do writes in background.
// Isolate leader for 10 seconds.
// Restore connectivity.
// Check that old leader was able to catch up after the partition is healed.
TEST_F_EX(QLStressTest, OldLeaderCatchUpAfterNetworkPartition, QLStressTestSingleTablet) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_combine_batcher_errors) = true;

  tablet::TabletPeer* leader_peer = nullptr;
  std::atomic<int> key(0);
  {
    TestThreadHolder thread_holder;

    AddWriter("value_", &key, &thread_holder);

    std::this_thread::sleep_for(5s * yb::kTimeMultiplier);

    tserver::MiniTabletServer* leader = nullptr;
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
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

    auto pre_isolate_op_id = leader_peer->GetLatestLogEntryOpId();
    LOG(INFO) << "Isolate, last op id: " << pre_isolate_op_id << ", key: " << key;
    ASSERT_GE(pre_isolate_op_id.term, 1);
    ASSERT_GT(pre_isolate_op_id.index, key);
    leader->Isolate();
    std::this_thread::sleep_for(10s * yb::kTimeMultiplier);

    auto pre_restore_op_id = leader_peer->GetLatestLogEntryOpId();
    LOG(INFO) << "Restore, last op id: " << pre_restore_op_id << ", key: " << key;
    ASSERT_EQ(pre_restore_op_id.term, pre_isolate_op_id.term);
    ASSERT_GE(pre_restore_op_id.index, pre_isolate_op_id.index);
    ASSERT_LE(pre_restore_op_id.index, pre_isolate_op_id.index + 10);
    ASSERT_OK(leader->Reconnect());

    thread_holder.WaitAndStop(5s * yb::kTimeMultiplier);
  }

  ASSERT_OK(WaitFor([leader_peer, &key] {
    return leader_peer->GetLatestLogEntryOpId().index > key;
  }, 5s, "Old leader has enough operations"));

  auto finish_op_id = leader_peer->GetLatestLogEntryOpId();
  LOG(INFO) << "Finish, last op id: " << finish_op_id << ", key: " << key;
  ASSERT_GT(finish_op_id.term, 1);
  ASSERT_GT(finish_op_id.index, key);
}

TEST_F_EX(QLStressTest, SlowUpdateConsensus, QLStressTestSingleTablet) {
  std::atomic<int> key(0);

  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kNonLeaders);
  ASSERT_EQ(peers.size(), 2);

  ASSERT_RESULT(peers[0]->GetRaftConsensus())->TEST_DelayUpdate(20s);

  TestThreadHolder thread_holder;
  AddWriter(std::string(100_KB, 'X'), &key, &thread_holder, 100ms);

  thread_holder.WaitAndStop(30s);

  ASSERT_RESULT(peers[0]->GetRaftConsensus())->TEST_DelayUpdate(0s);

  int64_t max_peak_consumption = 0;
  for (size_t i = 1; i <= cluster_->num_tablet_servers(); ++i) {
    auto server_tracker = MemTracker::FindTracker(Format("server $0", i));
    auto call_tracker = MemTracker::FindTracker("Call", server_tracker);
    auto inbound_rpc_tracker = MemTracker::FindTracker("Inbound RPC", call_tracker);
    max_peak_consumption = std::max(max_peak_consumption, inbound_rpc_tracker->peak_consumption());
  }
  LOG(INFO) << "Peak consumption: " << max_peak_consumption;
  ASSERT_LE(max_peak_consumption, 150_MB);
}

template <int kSoftLimit, int kHardLimit>
class QLStressTestDelayWrite : public QLStressTestSingleTablet {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) = 1_KB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_sst_files_soft_limit) = kSoftLimit;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_sst_files_hard_limit) = kHardLimit;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = 6;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_universal_compaction_min_merge_width) = 2;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_universal_compaction_size_ratio) = 1000;
    QLStressTestSingleTablet::SetUp();
  }

  client::YBSessionPtr NewSession() override {
    auto result = QLStressTestSingleTablet::NewSession();
    result->SetTimeout(5s);
    return result;
  }
};

void QLStressTest::AddWriter(
    std::string value_prefix, std::atomic<int>* key, TestThreadHolder* thread_holder,
    const std::chrono::nanoseconds& sleep_duration,
    bool allow_failures, TransactionManager* txn_manager, double transactional_write_probability) {
  thread_holder->AddThreadFunctor([this, &stop = thread_holder->stop_flag(), key, sleep_duration,
                                   value_prefix = std::move(value_prefix), allow_failures,
                                   txn_manager, transactional_write_probability] {
    auto session = NewSession();
    session->SetRejectionScoreSource(std::make_shared<RejectionScoreSource>());
    ASSERT_TRUE(txn_manager || transactional_write_probability == 0.0);

    while (!stop.load(std::memory_order_acquire)) {
      YBTransactionPtr txn;
      if (txn_manager && RandomActWithProbability(transactional_write_probability)) {
        txn = std::make_shared<YBTransaction>(txn_manager);
        ASSERT_OK(txn->Init(IsolationLevel::SNAPSHOT_ISOLATION));
        session->SetTransaction(txn);
      } else {
        session->SetTransaction(nullptr);
      }
      auto new_key = *key + 1;
      auto status = WriteRow(session, new_key, value_prefix + std::to_string(new_key));
      if (status.ok() && txn) {
        status = txn->CommitFuture().get();
      }

      if (!allow_failures) {
        ASSERT_OK(status);
      } else if (!status.ok()) {
        LOG(WARNING) << "Write failed: " << status;
      }
      if (status.ok()) {
        ++*key;
      }
      if (sleep_duration.count() > 0) {
        std::this_thread::sleep_for(sleep_duration);
      }
    }
  });
}

void QLStressTest::TestWriteRejection() {
  constexpr int kWriters = IsDebug() ? 10 : 20;
  constexpr int kKeyBase = 10000;

  std::array<std::atomic<int>, kWriters> keys;

  const std::string value_prefix = std::string(1_KB, 'X');

  TestThreadHolder thread_holder;
  for (int i = 0; i != kWriters; ++i) {
    keys[i] = i * kKeyBase;
    AddWriter(value_prefix, &keys[i], &thread_holder, 0s, true /* allow_failures */);
  }

  thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag(), &keys, &value_prefix] {
    auto session = NewSession();
    while (!stop.load(std::memory_order_acquire)) {
      int idx = RandomUniformInt(0, kWriters - 1);
      auto had_key = keys[idx].load(std::memory_order_acquire);
      if (had_key == kKeyBase * idx) {
        std::this_thread::sleep_for(50ms);
        continue;
      }
      auto value = ASSERT_RESULT(ReadRow(session, had_key)).string_value();
      ASSERT_EQ(value, value_prefix + std::to_string(had_key));
    }
  });

  int last_keys_written = 0;
  int first_keys_written_after_rejections_started_to_appear = -1;
  auto last_keys_written_update_time = CoarseMonoClock::now();
  uint64_t last_rejections = 0;
  bool has_writes_after_rejections = false;
  for (;;) {
    std::this_thread::sleep_for(1s);
    int keys_written = 0;
    for (int i = 0; i != kWriters; ++i) {
      keys_written += keys[i].load() - kKeyBase * i;
    }
    LOG(INFO) << "Total keys written: " << keys_written;
    if (keys_written == last_keys_written) {
      ASSERT_LE(CoarseMonoClock::now() - last_keys_written_update_time, 20s);
      continue;
    }
    if (last_rejections != 0) {
      if (first_keys_written_after_rejections_started_to_appear < 0) {
        first_keys_written_after_rejections_started_to_appear = keys_written;
      } else if (keys_written > first_keys_written_after_rejections_started_to_appear) {
        has_writes_after_rejections = true;
      }
    }
    last_keys_written = keys_written;
    last_keys_written_update_time = CoarseMonoClock::now();

    uint64_t total_rejections = 0;
    for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
      int64_t rejections = 0;
      auto peers = cluster_->mini_tablet_server(i)->server()->tablet_manager()->GetTabletPeers();
      for (const auto& peer : peers) {
        auto counter = METRIC_majority_sst_files_rejections.Instantiate(
            peer->tablet()->GetTabletMetricsEntity());
        rejections += counter->value();
      }
      total_rejections += rejections;
    }
    LOG(INFO) << "Total rejections: " << total_rejections;
    last_rejections = total_rejections;

    if (keys_written >= RegularBuildVsSanitizers(1000, 100) &&
        (IsSanitizer() || total_rejections >= 10) &&
        has_writes_after_rejections) {
      break;
    }
  }

  thread_holder.Stop();

  ASSERT_OK(WaitFor([cluster = cluster_.get()] {
    auto peers = ListTabletPeers(cluster, ListPeersFilter::kAll);
    OpId first_op_id;
    for (const auto& peer : peers) {
      auto consensus_result = peer->GetConsensus();
      if (!consensus_result) {
        return false;
      }
      auto current = consensus_result.get()->GetLastCommittedOpId();
      if (!first_op_id) {
        first_op_id = current;
      } else if (current != first_op_id) {
        return false;
      }
    }
    return true;
  }, 30s, "Waiting tablets to sync up"));
}

typedef QLStressTestDelayWrite<4, 10> QLStressTestDelayWrite_4_10;

TEST_F_EX(QLStressTest, DelayWrite, QLStressTestDelayWrite_4_10) {
  TestWriteRejection();
}

// Soft limit == hard limit to test write stop and recover after it.
typedef QLStressTestDelayWrite<6, 6> QLStressTestDelayWrite_6_6;

TEST_F_EX(QLStressTest, WriteStop, QLStressTestDelayWrite_6_6) {
  TestWriteRejection();
}

class QLStressTestLongRemoteBootstrap : public QLStressTestSingleTablet {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_cache_size_limit_mb) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_segment_size_bytes) = 96_KB;
    QLStressTestSingleTablet::SetUp();
  }
};

TEST_F_EX(QLStressTest, LongRemoteBootstrap, QLStressTestLongRemoteBootstrap) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_seconds_to_retain) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_remote_bootstrap_rate_limit_bytes_per_sec) = 1_MB;

  cluster_->mini_tablet_server(0)->Shutdown();

  ASSERT_OK(WaitFor([this] {
    auto leaders = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
    if (leaders.empty()) {
      return false;
    }
    LOG(INFO) << "Tablet id: " << leaders.front()->tablet_id();
    return true;
  }, 30s, "Leader elected"));

  std::atomic<int> key(0);

  TestThreadHolder thread_holder;
  constexpr size_t kValueSize = 32_KB;
  AddWriter(std::string(kValueSize, 'X'), &key, &thread_holder, 100ms);

  std::this_thread::sleep_for(20s); // Wait some time to have logs.

  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    RETURN_NOT_OK(cluster_->CleanTabletLogs());
    auto leaders = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
    if (leaders.empty()) {
      return false;
    }

    RETURN_NOT_OK(leaders.front()->tablet()->Flush(tablet::FlushMode::kSync));
    RETURN_NOT_OK(leaders.front()->RunLogGC());

    // Check that first log was garbage collected, so remote bootstrap will be required.
    consensus::ReplicateMsgs replicates;
    int64_t starting_op_segment_seq_num;
    return !leaders.front()->log()->GetLogReader()->ReadReplicatesInRange(
        100, 101, 0, &replicates, &starting_op_segment_seq_num).ok();
  }, 30s, "Logs cleaned"));

  LOG(INFO) << "Bring replica back, keys written: " << key.load(std::memory_order_acquire);
  ASSERT_OK(cluster_->mini_tablet_server(0)->Start(tserver::WaitTabletsBootstrapped::kFalse));

  thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag()] {
    while (!stop.load(std::memory_order_acquire)) {
      ASSERT_OK(cluster_->FlushTablets());
      ASSERT_OK(cluster_->CleanTabletLogs());
      std::this_thread::sleep_for(100ms);
    }
  });

  ASSERT_OK(WaitAllReplicasHaveIndex(cluster_.get(), key.load(std::memory_order_acquire), 40s));
  LOG(INFO) << "All replicas ready";

  ASSERT_OK(WaitFor([this]()->Result<bool> {
    bool result = true;
    auto followers = ListTabletPeers(cluster_.get(), ListPeersFilter::kNonLeaders);
    LOG(INFO) << "Num followers: " << followers.size();
    for (const auto& peer : followers) {
      auto log_cache_size = VERIFY_RESULT(peer->GetRaftConsensus())->LogCacheSize();
      LOG(INFO) << "T " << peer->tablet_id() << " P " << peer->permanent_uuid()
                << ", log cache size: " << log_cache_size;
      if (log_cache_size != 0) {
        result = false;
      }
    }
    return result;
  }, 5s, "All followers cleanup cache"));

  // Write some more values and check that replica still in touch.
  std::this_thread::sleep_for(5s);
  ASSERT_OK(WaitAllReplicasHaveIndex(cluster_.get(), key.load(std::memory_order_acquire), 1s));
}

class QLStressDynamicCompactionPriorityTest : public QLStressTest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_allow_preempting_compactions) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) = 16_KB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ondisk_compression) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_background_compactions) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec) = 160_KB;
    QLStressTest::SetUp();
  }

  int NumTablets() override {
    return 1;
  }

  void InitSchemaBuilder(YBSchemaBuilder* builder) override {
    builder->AddColumn("h")->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
    builder->AddColumn(kValueColumn)->Type(DataType::STRING);
  }
};

TEST_F_EX(QLStressTest, DynamicCompactionPriority, QLStressDynamicCompactionPriorityTest) {
  YBSchemaBuilder b;
  InitSchemaBuilder(&b);
  CompleteSchemaBuilder(&b);

  TableHandle table2;
  ASSERT_OK(table2.Create(YBTableName(kTableName.namespace_type(),
                                      kTableName.namespace_name(),
                                      kTableName.table_name() + "_2"),
                          NumTablets(), client_.get(), &b));

  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this, &table2, &stop = thread_holder.stop_flag()] {
    auto session = NewSession();
    int key = 1;
    std::string value(FLAGS_db_write_buffer_size, 'X');
    int left_writes_to_current_table = 0;
    TableHandle* table = nullptr;
    while (!stop.load(std::memory_order_acquire)) {
      if (left_writes_to_current_table == 0) {
        table = RandomUniformBool() ? &table_ : &table2;
        left_writes_to_current_table = RandomUniformInt(1, std::max(1, key / 5));
      } else {
        --left_writes_to_current_table;
      }
      const auto op = table->NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
      auto* const req = op->mutable_request();
      QLAddInt32HashValue(req, key);
      table_.AddStringColumnValue(req, kValueColumn, value);
      session->Apply(op);
      ASSERT_OK(session->TEST_Flush());
      ASSERT_OK(CheckOp(op.get()));
      std::this_thread::sleep_for(100ms);
      ++key;
    }
  });

  thread_holder.WaitAndStop(60s);

  auto delete_start = CoarseMonoClock::now();
  ASSERT_OK(client_->DeleteTable(table_->id(), true));
  MonoDelta delete_time(CoarseMonoClock::now() - delete_start);
  LOG(INFO) << "Delete time: " << delete_time;
  ASSERT_LE(delete_time, 10s);
}

class QLStressTestTransactionalSingleTablet : public QLStressTestSingleTablet {
  void CompleteSchemaBuilder(YBSchemaBuilder* builder) override {
    SetTransactional(builder);
  }
};

// Verify that we don't have too many write waiters.
// Uses FLAGS_TEST_max_write_waiters to fail debug check when there are too many waiters.
TEST_F_EX(QLStressTest, RemoveIntentsDuringWrite, QLStressTestTransactionalSingleTablet) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_max_write_waiters) = 5;

  constexpr int kWriters = 10;
  constexpr int kKeyBase = 10000;

  std::array<std::atomic<int>, kWriters> keys;

  auto txn_manager = CreateTxnManager();
  TestThreadHolder thread_holder;
  for (int i = 0; i != kWriters; ++i) {
    keys[i] = i * kKeyBase;
    AddWriter(
        "value_", &keys[i], &thread_holder, 0s, false /* allow_failures */, &txn_manager, 1.0);
  }

  thread_holder.WaitAndStop(3s);
}

TEST_F_EX(QLStressTest, SyncOldLeader, QLStressTestSingleTablet) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_raft_heartbeat_interval_ms) = 100 * kTimeMultiplier;
  constexpr int kOldLeaderWriteKeys = 100;
  // Should be less than amount of pending operations at the old leader.
  // So it is much smaller than keys written to the old leader.
  constexpr int kNewLeaderWriteKeys = 10;

  TestThreadHolder thread_holder;

  client_->messenger()->TEST_SetOutboundIpBase(ASSERT_RESULT(HostToAddress("127.0.0.1")));

  auto session = NewSession();
  // Perform write to make sure we have a leader.
  ASSERT_OK(WriteRow(session, 0, "value"));

  session->SetTimeout(10s);
  std::vector<std::future<FlushStatus>> futures;
  int key;
  for (key = 1; key <= kOldLeaderWriteKeys; ++key) {
    InsertRow(session, key, std::to_string(key));
    futures.push_back(session->FlushFuture());
  }

  auto old_leader = ASSERT_RESULT(ServerWithLeaders(cluster_.get()));
  LOG(INFO) << "Isolate old leader: "
            << cluster_->mini_tablet_server(old_leader)->server()->permanent_uuid();
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    if (i != old_leader) {
      ASSERT_OK(SetupConnectivity(cluster_.get(), i, old_leader, Connectivity::kOff));
    }
  }

  int written_to_new_leader = 0;
  while (written_to_new_leader < kNewLeaderWriteKeys) {
    ++key;
    auto write_status = WriteRow(session, key, std::to_string(key));
    if (write_status.ok()) {
      ++written_to_new_leader;
    } else {
      // Some writes could fail, while operations are being send to the old leader.
      LOG(INFO) << "Write " << key << " failed: " << write_status;
    }
  }

  auto peers = cluster_->GetTabletPeers(old_leader);
  // Reject all non empty update consensuses, to activate consensus exponential backoff,
  // and get into situation where leader sends empty request.
  for (const auto& peer : peers) {
    ASSERT_RESULT(peer->GetRaftConsensus())->TEST_RejectMode(consensus::RejectMode::kNonEmpty);
  }

  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    if (i != old_leader) {
      ASSERT_OK(SetupConnectivity(cluster_.get(), i, old_leader, Connectivity::kOn));
    }
  }

  // Wait until old leader receive update consensus with empty ops.
  std::this_thread::sleep_for(5s * kTimeMultiplier);

  for (const auto& peer : peers) {
    ASSERT_RESULT(peer->GetRaftConsensus())->TEST_RejectMode(consensus::RejectMode::kNone);
  }

  // Wait all writes to complete.
  for (auto& future : futures) {
    WARN_NOT_OK(future.get().status, "Write failed");
  }

  thread_holder.Stop();
}

} // namespace client
} // namespace yb
