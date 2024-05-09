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

#include "yb/client/table_info.h"

#include "yb/docdb/deadline_info.h"

#include "yb/integration-tests/cql_test_base.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/external_mini_cluster_validator.h"
#include "yb/integration-tests/mini_cluster_utils.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tablet/write_query.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/pg_client.proxy.h"
#include "yb/tserver/pg_client_service.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/logging.h"
#include "yb/util/random_util.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using namespace std::literals;

DECLARE_bool(pg_client_use_shared_memory);
DECLARE_bool(ysql_yb_ash_enable_infra);
DECLARE_bool(ysql_yb_enable_ash);

DECLARE_bool(allow_index_table_read_write);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(cql_prepare_child_threshold_ms);
DECLARE_bool(disable_index_backfill);
DECLARE_bool(enable_flush_retryable_requests);
DECLARE_bool(enable_wait_queues);
DECLARE_int32(rpc_slow_query_threshold_ms);
DECLARE_int32(rpc_workers_limit);
DECLARE_int64(transaction_abort_check_interval_ms);
DECLARE_uint64(transaction_manager_workers_limit);
DECLARE_bool(transactions_poll_check_aborted);

DECLARE_bool(TEST_disable_proactive_txn_cleanup_on_abort);
DECLARE_int32(TEST_fetch_next_delay_ms);
DECLARE_uint64(TEST_inject_txn_get_status_delay_ms);
DECLARE_bool(TEST_writequery_stuck_from_callback_leak);

DECLARE_int32(TEST_txn_participant_inject_latency_on_apply_update_txn_ms);
DECLARE_int32(TEST_inject_mvcc_delay_add_leader_pending_ms);
DECLARE_uint32(TEST_yb_ash_sleep_at_wait_state_ms);
DECLARE_uint32(TEST_yb_ash_wait_code_to_sleep_at);
DECLARE_int32(num_concurrent_backfills_allowed);
DECLARE_int32(TEST_slowdown_backfill_by_ms);
DECLARE_int32(memstore_size_mb);
DECLARE_int64(rocksdb_compact_flush_rate_limit_bytes_per_sec);
DECLARE_bool(TEST_export_wait_state_names);
DECLARE_bool(start_cql_proxy);
DECLARE_bool(use_priority_thread_pool_for_flushes);
DECLARE_bool(use_priority_thread_pool_for_compactions);
DECLARE_bool(collect_end_to_end_traces);

DEFINE_test_flag(bool, verify_pull, false,
    "If enabled, this test will check for a stronger condition that the specific wait state code "
    "that we are looking for was entered, and it was also pulled through Ash calls."
    "Inspite of adding a sleeps, requiring this condition can make the test flacky. "
    "Hence, for regular runs we check a weaker condition that the specific code we are looking for "
    "was entered. But, we don't particularly care if that state was visible to Ash calls."
    "However, whenever someone adds a new wait-state, they should run the test at least once with "
    "verify_pull enabled.");

namespace yb {

class WaitStateITest : public pgwrapper::PgMiniTestBase {
 public:
  WaitStateITest() = default;
  virtual ~WaitStateITest() = default;

  size_t NumTabletServers() override { return 1; }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_slow_query_threshold_ms) = kTimeMultiplier * 10000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ash_enable_infra) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_ash) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_export_wait_state_names) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_collect_end_to_end_traces) = true;
    pgwrapper::PgMiniTestBase::SetUp();

    ASSERT_OK(EnsureClientCreated());
    ASSERT_OK(StartCQLServer());
    cql_driver_ = std::make_unique<CppCassandraDriver>(
        std::vector<std::string>{cql_host_}, cql_port_, UsePartitionAwareRouting::kTrue);
  };

  std::unique_ptr<CppCassandraDriver> cql_driver_;
  std::unique_ptr<cqlserver::CQLServer> cql_server_;

 protected:
  // Cassandra has an internal timeout of 12s for the control connection.
  // We seem to run past this in TSAN setting, with pg enabled. The timeout for this control
  // connection isn't changed by updating the cass_cluster_set_{connect/request/resolve}_timeouts
  // So working around the issue by retrying.
  Result<CassandraSession> CqlSessionWithRetries(std::atomic<bool>& stop, const char* location) {
    LOG(INFO) << "Getting cql session for " << location;
    auto result = EstablishSession(cql_driver_.get());
    VLOG(2) << "Got " << (result.ok() ? " Session" : result.status().ToString(true));
    int retry = 0;
    while (!result.ok() && !stop.load(std::memory_order_acquire)) {
      LOG(ERROR) << "Got " << result.status() << " retrying " << ++retry << " for " << location;
      result = EstablishSession(cql_driver_.get());
      VLOG(2) << "Got " << (result.ok() ? " Session" : result.status().ToString(true));
    }
    VLOG(2) << "result is ok " << result.ok() << " stop is "
            << stop.load(std::memory_order_acquire);
    return result;
  }

 private:
  Status StartCQLServer() {
    cql_server_ = CqlTestBase<MiniCluster>::MakeCQLServerForTServer(
        cluster_.get(), /* idx */ 0, client_.get(), &cql_host_, &cql_port_);
    return cql_server_->Start();
  }

  std::string cql_host_;
  uint16_t cql_port_ = 0;
};

class WaitStateTestCheckMethodCounts : public WaitStateITest {
 protected:
  void CreateTables();

  virtual void LaunchWorkers(TestThreadHolder* thread_holder);
  virtual size_t NumKeys() {
    return 1000;
  }
  virtual int NumWriterThreads() {
    return 1;
  }

  void DoAshCalls(std::atomic<bool>& stop);
  void RunTestsAndFetchAshMethodCounts();
  virtual void PrintRowsFromASH();
  virtual void VerifyRowsFromASH() {}
  virtual bool IsDone() EXCLUDES(mutex_) = 0;
  virtual void VerifyCountsUnlocked() REQUIRES(mutex_);
  virtual void PrintCountsUnlocked() REQUIRES(mutex_);
  void UpdateCounts(const tserver::PgActiveSessionHistoryResponsePB& resp) EXCLUDES(mutex_);
  size_t GetMethodCount(const std::string& method) EXCLUDES(mutex_);
  virtual void VerifyResponse(const tserver::PgActiveSessionHistoryResponsePB& resp) {}

  void CreateCqlTables();
  void DoCqlWritesUntilStopped(std::atomic<bool>& stop);
  void DoCqlReadsUntilStopped(std::atomic<bool>& stop);

  void CreatePgTables();
  void DoPgWritesUntilStopped(std::atomic<bool>& stop);
  void DoPgReadsUntilStopped(std::atomic<bool>& stop);
  virtual void MaybeSleepBeforePgWriteCommits() {}

  std::atomic<size_t> num_ash_calls_done_;

  static constexpr float kProbNoMethod = 0.1f;
  using CountsMap = std::unordered_map<std::string, size_t>;
  CountsMap method_counts_ GUARDED_BY(mutex_);
  CountsMap wait_state_code_counts_ GUARDED_BY(mutex_);
  std::unordered_map<std::string, CountsMap> wait_state_code_counts_by_method_ GUARDED_BY(mutex_);
  std::mutex mutex_;
  bool enable_sql_ = true;
  bool enable_cql_ = true;
};

void WaitStateTestCheckMethodCounts::CreateCqlTables() {
  std::atomic_bool is_done{false};
  auto session = ASSERT_RESULT(CqlSessionWithRetries(is_done, __PRETTY_FUNCTION__));
  ASSERT_OK(
      session.ExecuteQuery("CREATE TABLE IF NOT EXISTS t (key INT PRIMARY KEY, value TEXT) WITH "
                           "transactions = { 'enabled' : true }"));
}

void WaitStateTestCheckMethodCounts::DoCqlWritesUntilStopped(std::atomic<bool>& stop) {
  auto session_result = CqlSessionWithRetries(stop, __PRETTY_FUNCTION__);
  const auto kNumKeys = NumKeys();
  for (int i = 0; !stop; i++) {
    WARN_NOT_OK(
        session_result->ExecuteQuery(yb::Format(
            "INSERT INTO t (key, value) VALUES ($0, 'v-$1')", i % kNumKeys,
            std::string(1000, 'A' + i % 26))),
        "Insert failed");
  }
}

void WaitStateTestCheckMethodCounts::DoCqlReadsUntilStopped(std::atomic<bool>& stop) {
  auto session_result = CqlSessionWithRetries(stop, __PRETTY_FUNCTION__);
  const auto kNumKeys = NumKeys();
  for (int i = 0; !stop; i++) {
    WARN_NOT_OK(
        session_result->ExecuteWithResult(
            yb::Format("SELECT value FROM t WHERE key = $0", i % kNumKeys)),
        "Select failed");
  }
}

void WaitStateTestCheckMethodCounts::CreatePgTables() {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, value TEXT)"));
}

void WaitStateTestCheckMethodCounts::DoPgWritesUntilStopped(std::atomic<bool>& stop) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET yb_enable_docdb_tracing=true"));
  const auto kNumKeys = NumKeys();
  for (size_t i = 0; !stop; i++) {
    ASSERT_OK(conn.Execute("BEGIN TRANSACTION"));
    if (i < kNumKeys) {
      WARN_NOT_OK(
          conn.Execute(yb::Format(
              "INSERT INTO t (key, value) VALUES ($0, 'v-$1')", i % kNumKeys,
              std::string(1000, 'A' + i % 26))),
          "Insert failed")
    } else {
      WARN_NOT_OK(
          conn.Execute(yb::Format(
              "UPDATE t SET value = 'v-$1' where key = $0", i % kNumKeys,
              std::string(1000, 'A' + i % 26))),
          "Insert failed")
    }
    MaybeSleepBeforePgWriteCommits();
    WARN_NOT_OK(conn.Execute("COMMIT"), "Insert/Commit failed")
  }
}

void WaitStateTestCheckMethodCounts::DoPgReadsUntilStopped(std::atomic<bool>& stop) {
  auto conn = ASSERT_RESULT(Connect());
  const auto kNumKeys = NumKeys();
  for (int i = 0; !stop; i++) {
    WARN_NOT_OK(
        conn.FetchRows<std::string>(yb::Format("SELECT value FROM t where key = $0", i % kNumKeys)),
        "Select failed");
  }
}

size_t WaitStateTestCheckMethodCounts::GetMethodCount(const std::string& method) {
  std::lock_guard lock(mutex_);
  return method_counts_[method];
}

void WaitStateTestCheckMethodCounts::LaunchWorkers(TestThreadHolder* thread_holder) {
  if (enable_cql_) {
    for (int i = 0; i < NumWriterThreads(); i++) {
      thread_holder->AddThreadFunctor(
          [this, &stop = thread_holder->stop_flag()] { DoCqlWritesUntilStopped(stop); });
    }
    thread_holder->AddThreadFunctor(
        [this, &stop = thread_holder->stop_flag()] { DoCqlReadsUntilStopped(stop); });
  }

  if (enable_sql_) {
    for (int i = 0; i < NumWriterThreads(); i++) {
      thread_holder->AddThreadFunctor(
          [this, &stop = thread_holder->stop_flag()] { DoPgWritesUntilStopped(stop); });
    }
    thread_holder->AddThreadFunctor(
        [this, &stop = thread_holder->stop_flag()] { DoPgReadsUntilStopped(stop); });
  }

  thread_holder->AddThreadFunctor([this, &stop = thread_holder->stop_flag()] {
    DoAshCalls(stop);
  });
}

void WaitStateTestCheckMethodCounts::UpdateCounts(
    const tserver::PgActiveSessionHistoryResponsePB& resp) {
  int idx = 0;
  std::lock_guard lock(mutex_);
  VLOG(1) << "Received " << resp.ShortDebugString();
  for (auto& container :
       {resp.tserver_wait_states(), resp.cql_wait_states(), resp.flush_and_compaction_wait_states(),
        resp.raft_log_appender_wait_states()}) {
    for (auto& entry : container.wait_states()) {
      VLOG(2) << "Entry " << ++idx << " : " << yb::ToString(entry);
      const auto& method =
          (entry.has_aux_info() && entry.aux_info().has_method() ? entry.aux_info().method() : "");
      const auto& wait_state_code = entry.wait_status_code_as_string();
      ++method_counts_[method];
      ++wait_state_code_counts_[wait_state_code];
      ++wait_state_code_counts_by_method_[method][wait_state_code];

      if (method.empty()) {
        LOG(ERROR) << "Found entry without AuxInfo/method." << entry.DebugString();
        // If an RPC does not have the aux/method information, it shouldn't have progressed much.
        if (entry.has_wait_status_code_as_string()) {
          EXPECT_TRUE(
              entry.wait_status_code_as_string() == "kOnCpu_Passive" ||
              entry.wait_status_code_as_string() == "kOnCpu_Active");
        }
      }
    }
  }
}

void WaitStateTestCheckMethodCounts::DoAshCalls(std::atomic<bool>& stop) {
  auto pg_proxy = std::make_unique<tserver::PgClientServiceProxy>(
      &client_->proxy_cache(),
      HostPort::FromBoundEndpoint(cluster_->mini_tablet_server(0)->bound_rpc_addr()));

  tserver::PgActiveSessionHistoryRequestPB req;
  req.set_fetch_tserver_states(true);
  req.set_fetch_flush_and_compaction_states(true);
  req.set_fetch_raft_log_appender_states(true);
  req.set_fetch_cql_states(true);
  tserver::PgActiveSessionHistoryResponsePB resp;
  rpc::RpcController controller;
  while (!stop) {
    WARN_NOT_OK(pg_proxy->ActiveSessionHistory(req, &resp, &controller), "Ash call failed");
    controller.Reset();
    ++num_ash_calls_done_;
    VLOG(1) << "Call " << num_ash_calls_done_.load() << " got " << yb::ToString(resp);
    VerifyResponse(resp);
    UpdateCounts(resp);
    SleepFor(10ms);
  }
}

void WaitStateTestCheckMethodCounts::CreateTables() {
  CreateCqlTables();
  CreatePgTables();
}

void WaitStateTestCheckMethodCounts::RunTestsAndFetchAshMethodCounts() {
  CreateTables();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_inject_mvcc_delay_add_leader_pending_ms) = 5;

  TestThreadHolder thread_holder;
  LaunchWorkers(&thread_holder);

  EXPECT_OK(LoggedWaitFor([this]() { return IsDone(); }, 300s, "Wait to be done", 10ms, 1, 10ms));
  thread_holder.Stop();

  {
    std::lock_guard lock(mutex_);
    PrintCountsUnlocked();
    VerifyCountsUnlocked();
  }
  PrintRowsFromASH();
  VerifyRowsFromASH();
}

void WaitStateTestCheckMethodCounts::PrintRowsFromASH() {
  auto conn = ASSERT_RESULT(Connect());
  auto query = "SELECT count(*) FROM yb_active_session_history;";
  LOG(INFO) << "Running: " << query;
  auto rows = ASSERT_RESULT(conn.FetchAllAsString(query, ",", "\n"));
  LOG(INFO) << " Got :\n" << rows;
}

void WaitStateTestCheckMethodCounts::PrintCountsUnlocked() {
  LOG(INFO) << "Method counts : " << yb::ToString(method_counts_);
  LOG(INFO) << "Wait state counts by method : "
            << yb::CollectionToString(wait_state_code_counts_by_method_, [](const auto& foo) {
                 return yb::Format("\n\t$0", yb::ToString(foo));
               });
  LOG(INFO) << "Wait state counts : "
            << yb::CollectionToString(wait_state_code_counts_, [](const auto& foo) {
                 return yb::Format("\n\t$0", yb::ToString(foo));
               });
  LOG(INFO) << "ash::WaitStateInfo::TEST_EnteredSleep() is "
            << ash::WaitStateInfo::TEST_EnteredSleep();
}

void WaitStateTestCheckMethodCounts::VerifyCountsUnlocked() {
  auto num_ash_calls = num_ash_calls_done_.load();
  ASSERT_LE(method_counts_["Read"], num_ash_calls);
  ASSERT_LE(method_counts_["Write"], num_ash_calls);
  ASSERT_GE(method_counts_["Write"], 1);
  // It is acceptable that some calls may not have populated their aux_info yet.
  // This probability should be very low.
  ASSERT_LE(method_counts_[""], 2 * num_ash_calls * kProbNoMethod);
}

bool WaitStateTestCheckMethodCounts::IsDone() {
  constexpr size_t kNumCalls = 100;
  return num_ash_calls_done_.load(std::memory_order_acquire) >= kNumCalls &&
         GetMethodCount("Write") >= 1;
}

class AshTestPg : public WaitStateTestCheckMethodCounts {
 public:
  AshTestPg() {
    enable_cql_ = false;
    enable_sql_ = true;
  }

 protected:
  void SetUp() override {
    // This test counts number of performed RPC calls, so turn off pg client shared memory.
    FLAGS_pg_client_use_shared_memory = false;
    WaitStateTestCheckMethodCounts::SetUp();
  }

  void VerifyCountsUnlocked() override REQUIRES(mutex_) {
    WaitStateTestCheckMethodCounts::VerifyCountsUnlocked();

    auto num_ash_calls = num_ash_calls_done_.load();
    ASSERT_LE(method_counts_["Perform"], 2 * num_ash_calls);
    ASSERT_GE(method_counts_["Perform"], 1);
  }

  bool IsDone() override EXCLUDES(mutex_) {
    return WaitStateTestCheckMethodCounts::IsDone() && GetMethodCount("Perform") >= 1;
  }
};

TEST_F_EX(WaitStateITest, YB_DISABLE_TEST_IN_TSAN(AshPg), AshTestPg) {
  RunTestsAndFetchAshMethodCounts();
}

class AshTestCql : public WaitStateTestCheckMethodCounts {
 public:
  AshTestCql() {
    enable_cql_ = true;
    enable_sql_ = false;
  }

 protected:
  void VerifyCountsUnlocked() REQUIRES(mutex_) override {
    WaitStateTestCheckMethodCounts::VerifyCountsUnlocked();
    ASSERT_GE(method_counts_["CQLProcessCall"], 1);
  }

  void VerifyRowsFromASH() override {
    std::vector<std::string> queries;
    queries.push_back("SELECT count(*) FROM yb_active_session_history;");
    queries.push_back(
        "SELECT wait_event_component, wait_event_class, wait_event, "
        "CASE WHEN wait_event_aux = '' THEN 'Null' ELSE 'NonNull' END as null_or_not, count(*) "
        "FROM yb_active_session_history "
        "GROUP BY wait_event_component, wait_event_class, wait_event, null_or_not;");
    queries.push_back(
        "SELECT wait_event_component, wait_event_class, wait_event, "
        "CASE WHEN rpc_request_id = 0 THEN 'Zero' ELSE 'NonZero' END as zero_or_not, count(*) "
        "FROM yb_active_session_history "
        "GROUP BY wait_event_component, wait_event_class, wait_event, zero_or_not;");

    auto conn = ASSERT_RESULT(Connect());
    for (const auto& query : queries) {
      LOG(INFO) << "Running: " << query;
      auto rows = ASSERT_RESULT(conn.FetchAllAsString(query, ",", "\n"));
      LOG(INFO) << " Got :\n" << rows;
    }
  }

  void VerifyResponse(const tserver::PgActiveSessionHistoryResponsePB& resp) override {
    for (const auto& wait_state_pb : resp.cql_wait_states().wait_states()) {
      LOG_IF(ERROR, wait_state_pb.metadata().rpc_request_id() == 0)
          << "wait_state_pb is " << wait_state_pb.DebugString();
    }
  }

  bool IsDone() override EXCLUDES(mutex_) {
    return WaitStateTestCheckMethodCounts::IsDone() && GetMethodCount("CQLProcessCall") >= 1;
  }
};

TEST_F_EX(WaitStateITest, YB_DISABLE_TEST_IN_TSAN(AshCql), AshTestCql) {
  RunTestsAndFetchAshMethodCounts();
}

class AshTestWithCompactions : public WaitStateTestCheckMethodCounts {
 public:
  void LaunchWorkers(TestThreadHolder* thread_holder) override {
    if (do_compactions_) {
      thread_holder->AddThreadFunctor(
          [this, &stop = thread_holder->stop_flag()] { DoCompactionsAndFlushes(stop); });
    }

    WaitStateTestCheckMethodCounts::LaunchWorkers(thread_holder);
  }

  virtual void SetCompactionAndFlushRate(int run_id) {}
  void DoCompactionsAndFlushes(std::atomic<bool>& stop);

  void VerifyCountsUnlocked() REQUIRES(mutex_) override;
  void PrintCountsUnlocked() REQUIRES(mutex_) override;
  bool IsDone() EXCLUDES(mutex_) override;

 protected:
  std::atomic<size_t> num_compactions_done_;
  bool do_compactions_ = true;
};

void AshTestWithCompactions::PrintCountsUnlocked() {
  WaitStateTestCheckMethodCounts::PrintCountsUnlocked();
  LOG(INFO) << "Num compactions done " << num_compactions_done_;
}
void AshTestWithCompactions::VerifyCountsUnlocked() {
  ASSERT_GE(method_counts_["Flush"], 1);
  ASSERT_GE(method_counts_["Compaction"], 1);
}

bool AshTestWithCompactions::IsDone() {
  return GetMethodCount("Flush") >= 1 && GetMethodCount("Compaction") >= 1;
}

void AshTestWithCompactions::DoCompactionsAndFlushes(std::atomic<bool>& stop) {
  // Flush every 100ms. Compact every 500ms.
  auto waitTime = MonoDelta::FromMilliseconds(100);
  constexpr int kCompactEveryN = 5;
  for (int i = 0; !stop; i++) {
    SetCompactionAndFlushRate(i);
    if (i % kCompactEveryN == kCompactEveryN - 1) {
      LOG(INFO) << __func__ << "Running compaction";
      WARN_NOT_OK(cluster_->CompactTablets(), "Compaction failed");
      ++num_compactions_done_;
      LOG(INFO) << "Compactions done " << num_compactions_done_;
    } else {
      LOG(INFO) << __func__ << "Running flush";
      ASSERT_OK(cluster_->FlushTablets(
          tablet::FlushMode::kSync,
          tablet::FlushFlags::kAllDbs | tablet::FlushFlags::kNoScopedOperation));
    }
    SleepFor(waitTime);
  }
}

TEST_F_EX(WaitStateITest, YB_DISABLE_TEST_IN_TSAN(AshFlushAndCompactions), AshTestWithCompactions) {
  RunTestsAndFetchAshMethodCounts();
}

class AshTestVerifyOccurrenceBase : public AshTestWithCompactions {
 public:
  explicit AshTestVerifyOccurrenceBase(ash::WaitStateCode code)
      : code_to_look_for_(code), verify_code_was_pulled_(GetAtomicFlag(&FLAGS_TEST_verify_pull)) {}

  void SetUp() override {
    if (verify_code_was_pulled_ && ShouldSleepAtWaitCode()) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_yb_ash_sleep_at_wait_state_ms) = 100;
    }
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_yb_ash_wait_code_to_sleep_at) =
        yb::to_underlying(code_to_look_for_);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_priority_thread_pool_for_compactions) =
        UsePriorityQueueForCompaction();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_priority_thread_pool_for_flushes) =
        UsePriorityQueueForFlush();
    if (code_to_look_for_ == ash::WaitStateCode::kBackfillIndex_WaitForAFreeSlot) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_concurrent_backfills_allowed) = 1;
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_slowdown_backfill_by_ms) = 20;
      enable_sql_ = false;
    }
    if (code_to_look_for_ == ash::WaitStateCode::kRetryableRequests_SaveToDisk) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_flush_retryable_requests) = true;
    }

    if (code_to_look_for_ == ash::WaitStateCode::kRocksDB_Flush ||
        code_to_look_for_ == ash::WaitStateCode::kRocksDB_Compaction) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_memstore_size_mb) = 1;
    }

    if (code_to_look_for_ == ash::WaitStateCode::kConflictResolution_WaitOnConflictingTxns) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = true;
      enable_cql_ = false;
    }

    const auto code_class = ash::Class(to_underlying(code_to_look_for_) >> YB_ASH_CLASS_POSITION);
    do_compactions_ = (code_class == ash::Class::kRocksDB);

    WaitStateITest::SetUp();
  }

  void SetCompactionAndFlushRate(int run_id) override {
    if (code_to_look_for_ == ash::WaitStateCode::kRocksDB_RateLimiter && run_id == 10) {
      SetCompactFlushRateLimitBytesPerSec(cluster_.get(), 1024);
    }
  }

  void VerifyCountsUnlocked() override REQUIRES(mutex_) {
    if (!verify_code_was_pulled_) {
      ASSERT_TRUE(ash::WaitStateInfo::TEST_EnteredSleep());
      return;
    }

    auto code_name = yb::ToString(code_to_look_for_);
    LOG(INFO) << "wait_state_code_counts_[" << code_name << "] is "
              << wait_state_code_counts_[code_name];
    ASSERT_GE(wait_state_code_counts_[code_name], 1);
  }

  bool IsDone() override EXCLUDES(mutex_) {
    if (!verify_code_was_pulled_) {
      return ash::WaitStateInfo::TEST_EnteredSleep();
    }

    auto code_name = yb::ToString(code_to_look_for_);
    std::lock_guard lock(mutex_);
    return wait_state_code_counts_[code_name] >= 1;
  }

  void LaunchWorkers(TestThreadHolder* thread_holder) override;

  bool ShouldSleepAtWaitCode() const {
    // We do not want to trigger a sleep on a reactor thread.
    return code_to_look_for_ != ash::WaitStateCode::kOnCpu_Passive &&
           code_to_look_for_ != ash::WaitStateCode::kRpc_Done;
  }

  virtual bool UsePriorityQueueForFlush() const {
    return false;
  }

  virtual bool UsePriorityQueueForCompaction() const {
    return true;
  }

  size_t NumTabletServers() override {
    switch (code_to_look_for_) {
      case ash::WaitStateCode::kRaft_WaitingForReplication:
      case ash::WaitStateCode::kRaft_ApplyingEdits:
      case ash::WaitStateCode::kReplicaState_TakeUpdateLock:
        return 3;
      default:
        return 1;
    }
  }

  void MaybeSleepBeforePgWriteCommits() override {
    if (code_to_look_for_ == ash::WaitStateCode::kConflictResolution_WaitOnConflictingTxns) {
      return SleepFor(10ms);
    }
  }

  size_t NumKeys() override {
    if (code_to_look_for_ == ash::WaitStateCode::kConflictResolution_WaitOnConflictingTxns ||
        code_to_look_for_ == ash::WaitStateCode::kLockedBatchEntry_Lock) {
      return 1;
    }
    return 1000;
  }

  int NumWriterThreads() override {
    if (code_to_look_for_ == ash::WaitStateCode::kConflictResolution_WaitOnConflictingTxns ||
        code_to_look_for_ == ash::WaitStateCode::kLockedBatchEntry_Lock) {
      return 10;
    }
    if (code_to_look_for_ == ash::WaitStateCode::kBackfillIndex_WaitForAFreeSlot) {
      return 0;
    }
    return 1;
  }

 protected:
  const ash::WaitStateCode code_to_look_for_;
  const bool verify_code_was_pulled_;

  void CreateIndexesUntilStopped(std::atomic<bool>& stop);
  void AddNodesUntilStopped(std::atomic<bool>& stop);
  std::atomic<size_t> num_indexes_created_{0};
};

void AshTestVerifyOccurrenceBase::CreateIndexesUntilStopped(std::atomic<bool>& stop) {
  auto session = ASSERT_RESULT(CqlSessionWithRetries(stop, __PRETTY_FUNCTION__));
  constexpr auto kNamespace = "test";
  const client::YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "t");
  for (int i = 1; !stop; i++) {
    const client::YBTableName idx_name(YQL_DATABASE_CQL, kNamespace, yb::Format("cql_idx_$0", i));
    LOG(INFO) << "Creating a CQL index " << idx_name.ToString();
    EXPECT_OK(
        session.ExecuteQuery(yb::Format("CREATE INDEX $0 ON t (value)", idx_name.table_name())));

    auto perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
        table_name, idx_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE));
    LOG_IF(ERROR, perm != IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE)
        << "Got " << yb::ToString(perm);

    LOG(INFO) << "Created index " << i;
    ++num_indexes_created_;
    SleepFor(10ms);
  }
}

void AshTestVerifyOccurrenceBase::AddNodesUntilStopped(std::atomic<bool>& stop) {
  auto waitTime = MonoDelta::FromMilliseconds(100);
  do {
    ASSERT_OK(cluster_->AddTabletServer());
    WARN_NOT_OK(cluster_->WaitForLoadBalancerToStabilize(60s), "Load did not yet balance");
    waitTime = waitTime * 2;
  } while (WaitFor([&stop]() { return stop.load(); }, waitTime, "Wait to be stopped").IsTimedOut());
}

std::string WaitStateCodeToString(const testing::TestParamInfo<ash::WaitStateCode>& param_info) {
  return yb::ToString(param_info.param);
}

void AshTestVerifyOccurrenceBase::LaunchWorkers(TestThreadHolder* thread_holder) {
  switch (code_to_look_for_) {
    case ash::WaitStateCode::kBackfillIndex_WaitForAFreeSlot:
    case ash::WaitStateCode::kCreatingNewTablet:
    case ash::WaitStateCode::kConsensusMeta_Flush:
    case ash::WaitStateCode::kSaveRaftGroupMetadataToDisk:
      thread_holder->AddThreadFunctor(
          [this, &stop = thread_holder->stop_flag()] { CreateIndexesUntilStopped(stop); });
      break;
    case ash::WaitStateCode::kReplicaState_TakeUpdateLock:
    case ash::WaitStateCode::kRetryableRequests_SaveToDisk:
      thread_holder->AddThreadFunctor(
          [this, &stop = thread_holder->stop_flag()] { AddNodesUntilStopped(stop); });
      break;
    default: {
    }
  }
  AshTestWithCompactions::LaunchWorkers(thread_holder);
}

class AshTestVerifyOccurrence : public AshTestVerifyOccurrenceBase,
                                public ::testing::WithParamInterface<ash::WaitStateCode> {
 public:
  AshTestVerifyOccurrence() : AshTestVerifyOccurrenceBase(GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(
    WaitStateITest, AshTestVerifyOccurrence,
    ::testing::Values(
      ash::WaitStateCode::kOnCpu_Active,
      ash::WaitStateCode::kOnCpu_Passive,
      ash::WaitStateCode::kRpc_Done,
      ash::WaitStateCode::kRetryableRequests_SaveToDisk,
      ash::WaitStateCode::kMVCC_WaitForSafeTime,
      ash::WaitStateCode::kLockedBatchEntry_Lock,
      ash::WaitStateCode::kBackfillIndex_WaitForAFreeSlot,
      ash::WaitStateCode::kCreatingNewTablet,
      ash::WaitStateCode::kSaveRaftGroupMetadataToDisk,
      ash::WaitStateCode::kDumpRunningRpc_WaitOnReactor,
      ash::WaitStateCode::kConflictResolution_ResolveConficts,
      ash::WaitStateCode::kConflictResolution_WaitOnConflictingTxns,
      ash::WaitStateCode::kRaft_WaitingForReplication,
      ash::WaitStateCode::kRaft_ApplyingEdits,
      ash::WaitStateCode::kReplicaState_TakeUpdateLock,
      ash::WaitStateCode::kWAL_Append,
      ash::WaitStateCode::kWAL_Sync,
      ash::WaitStateCode::kConsensusMeta_Flush,
      ash::WaitStateCode::kRocksDB_ReadBlockFromFile,
      ash::WaitStateCode::kRocksDB_OpenFile,
      ash::WaitStateCode::kRocksDB_WriteToFile,
      ash::WaitStateCode::kRocksDB_Flush,
      ash::WaitStateCode::kRocksDB_Compaction,
      ash::WaitStateCode::kRocksDB_PriorityThreadPoolTaskPaused,
      ash::WaitStateCode::kRocksDB_CloseFile,
      ash::WaitStateCode::kRocksDB_RateLimiter,
      ash::WaitStateCode::kRocksDB_NewIterator,
      ash::WaitStateCode::kYCQL_Parse,
      ash::WaitStateCode::kYCQL_Analyze,
      ash::WaitStateCode::kYCQL_Execute,
      ash::WaitStateCode::kYBClient_WaitingOnDocDB,
      ash::WaitStateCode::kYBClient_LookingUpTablet
      ), WaitStateCodeToString);

TEST_P(AshTestVerifyOccurrence, YB_DISABLE_TEST_IN_TSAN(VerifyWaitStateEntered)) {
  RunTestsAndFetchAshMethodCounts();
}

class AshTestWithPriorityQueue
    : public AshTestVerifyOccurrenceBase,
      public ::testing::WithParamInterface<std::tuple<ash::WaitStateCode, bool>> {
 public:
  AshTestWithPriorityQueue()
      : AshTestVerifyOccurrenceBase(std::get<0>(GetParam())),
        use_priority_queue_(std::get<1>(GetParam())) {}

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_yb_ash_sleep_at_wait_state_ms) = 100;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_yb_ash_wait_code_to_sleep_at) =
        yb::to_underlying(code_to_look_for_);

    auto use_priority_queue = std::get<1>(GetParam());
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_priority_thread_pool_for_flushes) = use_priority_queue;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_priority_thread_pool_for_compactions) = use_priority_queue;

    WaitStateITest::SetUp();
  }

  bool UsePriorityQueueForFlush() const override {
    return use_priority_queue_;
  }

  bool UsePriorityQueueForCompaction() const override {
    return use_priority_queue_;
  }

 private:
  const bool use_priority_queue_;

};

std::string WaitStateCodeAndBoolToString(
    const testing::TestParamInfo<std::tuple<ash::WaitStateCode, bool>>& param_info) {
  return yb::Format(
      "$0_$1", yb::ToString(std::get<0>(param_info.param)),
      (std::get<1>(param_info.param) ? "WithPriorityQueue" : "WithoutPriorityQueue"));
}

INSTANTIATE_TEST_SUITE_P(
    WaitStateITest, AshTestWithPriorityQueue,
    ::testing::Combine(
        ::testing::Values(
            ash::WaitStateCode::kRocksDB_Flush,
            ash::WaitStateCode::kRocksDB_Compaction),
        ::testing::Bool()),
    WaitStateCodeAndBoolToString);

TEST_P(AshTestWithPriorityQueue, YB_DISABLE_TEST_IN_TSAN(VerifyWaitStateEntered)) {
  RunTestsAndFetchAshMethodCounts();
}

}  // namespace yb
