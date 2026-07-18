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

#include <chrono>

#include "yb/util/countdown_latch.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(TEST_emulate_op_lost_on_write);
DECLARE_uint64(pg_client_session_expiration_ms);

namespace yb::pgwrapper {

class PgOpProcessingOrderTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_emulate_op_lost_on_write) = true;
    PgMiniTestBase::SetUp();
  }

  size_t NumTabletServers() override {
    return 1;
  }
};

// The test checks connection shutdown time when it waits for the arriving of operation which will
// never come (i.e. operation is lost)
TEST_F(PgOpProcessingOrderTest, ShutdownTimeWithLostOp) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE test (k INT PRIMARY KEY)"));
  auto insert_duration = 0ms;
  constexpr auto kDelayBeforeClusterStop = 3000ms;
  {
    CountDownLatch latch{1};
    TestThreadHolder threads;
    threads.AddThreadFunctor([&conn, &latch, &insert_duration] {
      latch.CountDown();
      const auto start = std::chrono::steady_clock::now();
      // It is exected that INSERT will wait for the lost operation
      ASSERT_NOK(conn.FetchRows<int32_t>("INSERT INTO test VALUES(1)"));
      insert_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);
    });
    latch.Wait();
    std::this_thread::sleep_for(kDelayBeforeClusterStop);
    const auto start = std::chrono::steady_clock::now();
    cluster_->StopSync();
    const auto stop_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    ASSERT_LT(stop_duration, 0.8 * FLAGS_pg_client_session_expiration_ms * 1ms)
        << "Expected server to shutdown faster than operation wait timeout";
  }
  ASSERT_GT(insert_duration, kDelayBeforeClusterStop)
      << "Expected INSERT do be delayed due to operation lost";
}

TEST_F(PgOpProcessingOrderTest, BackendTerminationTimeWithLostOp) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE test (k INT PRIMARY KEY)"));
  auto aux_conn = ASSERT_RESULT(Connect());
  auto aux_background_pid = ASSERT_RESULT(aux_conn.FetchRow<int32_t>("SELECT pg_backend_pid()"));
  auto insert_duration = 0ms;
  constexpr auto kDelayBeforeTermination = 3000ms;
  {
    CountDownLatch latch{1};
    TestThreadHolder threads;
    threads.AddThreadFunctor([&aux_conn, &latch, &insert_duration] {
      latch.CountDown();
      const auto start = std::chrono::steady_clock::now();
      // It is exected that INSERT will wait for the lost operation
      ASSERT_NOK(aux_conn.FetchRows<int32_t>("INSERT INTO test VALUES(1)"));
      insert_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);
    });
    latch.Wait();
    std::this_thread::sleep_for(kDelayBeforeTermination);
    ASSERT_OK(conn.FetchRow<bool>(Format("SELECT pg_terminate_backend($0)", aux_background_pid)));
  }
  ASSERT_GT(insert_duration, kDelayBeforeTermination)
      << "Expected INSERT do be delayed due to operation lost";
  ASSERT_LT(insert_duration, 0.8 * FLAGS_pg_client_session_expiration_ms * 1ms)
      << "Expected backed process will die faster than operation wait timeout";
}

}  // namespace yb::pgwrapper
