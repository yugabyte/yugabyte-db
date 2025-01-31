// Copyright (c) YugabyteDB, Inc.
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

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

#include <string>

#include <boost/interprocess/mapped_region.hpp>

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/pg_client_service.h"
#include "yb/tserver/pg_shared_mem_pool.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::literals;

DECLARE_bool(pg_client_use_shared_memory);
DECLARE_bool(TEST_pg_client_crash_on_shared_memory_send);
DECLARE_bool(TEST_skip_remove_tserver_shared_memory_object);
DECLARE_int32(ysql_client_read_write_timeout_ms);
DECLARE_int32(pg_client_extra_timeout_ms);
DECLARE_int32(TEST_transactional_read_delay_ms);
DECLARE_uint64(big_shared_memory_segment_expiration_time_ms);
DECLARE_uint64(big_shared_memory_segment_session_expiration_time_ms);

namespace yb::pgwrapper {

class PgSharedMemTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    FLAGS_pg_client_use_shared_memory = true;
    FLAGS_pg_client_extra_timeout_ms = 0;
    FLAGS_ysql_client_read_write_timeout_ms = GetReadWriteTimeout();
    FLAGS_big_shared_memory_segment_session_expiration_time_ms = 1000;
    FLAGS_big_shared_memory_segment_expiration_time_ms = 1000;
    PgMiniTestBase::SetUp();
  }

  virtual int GetReadWriteTimeout() const {
    return RegularBuildVsSanitizers(2, 20) * 1000;
  }

  std::pair<size_t, size_t> SumBigSharedMemUsage() const {
    std::pair<size_t, size_t> result(0, 0);
    for (const auto& mini_server : cluster_->mini_tablet_servers()) {
      auto server_tracker = mini_server->mem_tracker();
      auto allocated_tracker = server_tracker->FindChild(
          tserver::PgSharedMemoryPool::kAllocatedMemTrackerId);
      auto available_tracker = allocated_tracker->FindChild(
          tserver::PgSharedMemoryPool::kAvailableMemTrackerId);
      result.first += allocated_tracker->consumption();
      result.second += available_tracker->consumption();
    }
    return result;
  }
};

TEST_F(PgSharedMemTest, Simple) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, value) VALUES (1, 'hello')"));

  auto value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
  ASSERT_EQ(value, "hello");
}

TEST_F(PgSharedMemTest, Restart) {
  FLAGS_TEST_skip_remove_tserver_shared_memory_object = true;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key) VALUES (1)"));

  ASSERT_OK(RestartCluster());

  conn = ASSERT_RESULT(Connect());
  auto value = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT * FROM t"));
  ASSERT_EQ(value, 1);
}

TEST_F(PgSharedMemTest, TimeOut) {
  constexpr auto kNumRows = 100;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY) SPLIT INTO 1 TABLETS"));

  for (auto delay : {true, false}) {
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t SELECT generate_series(1, $0)", kNumRows));

    FLAGS_TEST_transactional_read_delay_ms =
        delay ? FLAGS_ysql_client_read_write_timeout_ms * 2 : 0;
    auto result = conn.FetchRow<int64_t>(
        "SELECT SUM(key) FROM t WHERE key > 0 OR key < 0");
    if (delay) {
      ASSERT_NOK(result);
      ASSERT_OK(conn.RollbackTransaction());
    } else {
      ASSERT_OK(result);
      ASSERT_EQ(*result, kNumRows * (kNumRows + 1) / 2);
      ASSERT_OK(conn.CommitTransaction());
    }
  }
}

TEST_F(PgSharedMemTest, BigData) {
  auto no_allocated_segments_functor = [this] {
    auto usage = SumBigSharedMemUsage();
    LOG(INFO) << "Allocated big shared mem bytes: " << usage.first;
    return usage.first == 0;
  };

  auto conn = ASSERT_RESULT(Connect());
  auto value = RandomHumanReadableString(boost::interprocess::mapped_region::get_page_size());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO t (key, value) VALUES (1, '$0')", value));

  ASSERT_OK(WaitFor(no_allocated_segments_functor, 5s, "No allocated segments"));

  auto result = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
  ASSERT_EQ(result, value);

  auto usage = SumBigSharedMemUsage();
  ASSERT_GT(usage.first, 0); // allocated big shared memory segment
  ASSERT_EQ(usage.second, 0); // big shared memory segment in use

  auto segment_in_pool_functor = [this] {
    auto usage = SumBigSharedMemUsage();
    LOG(INFO) << "Big shared mem bytes, allocated: " << usage.first << ", available: "
              << usage.second;
    return usage.first == usage.second;
  };

  ASSERT_OK(WaitFor(segment_in_pool_functor, 5s, "Connection released big shared memory segment"));
  auto new_usage = SumBigSharedMemUsage();
  ASSERT_GT(new_usage.first, 0); // Check segment still in pool.

  result = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
  ASSERT_EQ(result, value);
  new_usage = SumBigSharedMemUsage();
  ASSERT_EQ(new_usage, usage); // Check segment taken from pool.

  ASSERT_OK(WaitFor(segment_in_pool_functor, 5s, "Connection released big shared memory segment"));

  ASSERT_OK(WaitFor(no_allocated_segments_functor, 5s, "Big shared memory segment released"));
}

TEST_F(PgSharedMemTest, Crash) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key) VALUES (1)"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pg_client_crash_on_shared_memory_send) = true;
  ASSERT_OK(RestartCluster());

  auto settings = MakeConnSettings();
  settings.connect_timeout = 5;
  auto conn_result = PGConnBuilder(settings).Connect();
  ASSERT_NOK(conn_result);
  ASSERT_TRUE(conn_result.status().IsNetworkError());
}

TEST_F(PgSharedMemTest, Batches) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY)"));
  ASSERT_OK(SetMaxBatchSize(&conn, 4));
  ASSERT_OK(conn.Execute("INSERT INTO t SELECT GENERATE_SERIES(1, 10)"));

  auto value = AsString(ASSERT_RESULT(conn.FetchRows<int32_t>("SELECT * FROM t ORDER BY key")));
  ASSERT_EQ(value, "[1, 2, 3, 4, 5, 6, 7, 8, 9, 10]");
}

class PgSharedMemBigTimeoutTest : public PgSharedMemTest {
 protected:
  int GetReadWriteTimeout() const override {
    return 120000;
  }
};

TEST_F_EX(PgSharedMemTest, LongRead, PgSharedMemBigTimeoutTest) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY) SPLIT INTO 1 TABLETS"));

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1)"));

  FLAGS_TEST_transactional_read_delay_ms = 65000;
  auto result = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT * FROM t"));
  ASSERT_EQ(result, 1);
  ASSERT_OK(conn.CommitTransaction());
}

TEST_F(PgSharedMemTest, ConnectionShutdown) {
  {
    auto conn = ASSERT_RESULT(Connect());

    ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY) SPLIT INTO 1 TABLETS"));
    ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1)"));
  }

  auto threads_before = CountManagedThreads();
  size_t threads_mid = 0;
  constexpr size_t kNumIterations = 16;

  for (int i = 0; i != kNumIterations; ++i) {
    auto conn = ASSERT_RESULT(Connect());
    auto result = ASSERT_RESULT(conn.FetchAllAsString("SELECT * FROM t"));
    ASSERT_EQ(result, "1");
    if (i == kNumIterations / 2) {
      threads_mid = CountManagedThreads();
    }
  }

  std::this_thread::sleep_for(1s * kTimeMultiplier);

  auto threads_after = CountManagedThreads();

  LOG(INFO) << "Threads: " << threads_before << ", " << threads_mid << ", " << threads_after;

  ASSERT_LE(threads_after, threads_mid);

  auto* client_service = cluster_->mini_tablet_server(0)->server()->TEST_GetPgClientService();
  ASSERT_LE(client_service->TEST_SessionsCount(), 1);
}

} // namespace yb::pgwrapper
