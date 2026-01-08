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
#include <string_view>

#include "yb/util/countdown_latch.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/format.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using namespace std::literals;

namespace yb::pgwrapper {
namespace {

auto FetchBackendPid(PGConn& conn) {
  return conn.FetchRow<PGUint32>("SELECT pg_backend_pid()");
}

auto TerminateBackend(PGConn& conn, size_t backend_id) {
  return conn.FetchRow<bool>(Format("SELECT pg_terminate_backend($0)", backend_id));
}

Result<std::string> WaitForPidExitReason(size_t pid) {
  std::string out;
  RETURN_NOT_OK(Subprocess::Call({"strace", "-e", "exit", "-p", AsString(pid)}, nullptr, &out));
  constexpr auto kTail = " +++\n"sv;
  constexpr auto kHead = "+++ "sv;
  auto head_pos = out.rfind(kHead);
  SCHECK(
      head_pos != std::string::npos && out.ends_with(kTail),
      IllegalState, "Unexpected output '$0'", out);
  const auto start = head_pos + kHead.length();
  return out.substr(start, out.length() - start - kTail.length());
}

bool IsSegfaultExitReason(std::string_view exit_reason) {
  return exit_reason.contains("SIGSEGV");
}

} // namespace

class PgTerminateBackendTest : public PgMiniTestBase {
 protected:
  size_t NumTabletServers() override {
    return 1;
  }
};

// The test checks cleanup procedure of terminated process with conflicting txn/subtxn.
TEST_F(PgTerminateBackendTest, ConflictingSubTxn) {
  auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
  ASSERT_OK(conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE aux_t (k INT PRIMARY KEY, v INT)"));
  auto aux_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));
  auto aux_backend_pid = ASSERT_RESULT(FetchBackendPid(aux_conn));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES(1, 1), (2, 2), (3, 3)"));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  ASSERT_OK(aux_conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  ASSERT_OK(conn.Execute("UPDATE t SET v = v + 1"));
  {
    CountDownLatch latch{1};
    TestThreadHolder threads;
    threads.AddThreadFunctor([&aux_conn, &latch] {
      ASSERT_OK((aux_conn.Execute("INSERT INTO aux_t VALUES(30, 30)")));
      ASSERT_OK(aux_conn.Execute("SAVEPOINT a"));
      latch.CountDown();
      ASSERT_NOK((aux_conn.FetchRow<int32_t, int32_t>("SELECT * FROM t WHERE k = 1")));
    });
    threads.AddThreadFunctor([aux_backend_pid] {
      const auto exit_reason = ASSERT_RESULT(WaitForPidExitReason(aux_backend_pid));
      LOG(INFO) << "Backend exit reason: " << exit_reason;
      ASSERT_FALSE(IsSegfaultExitReason(exit_reason));
    });
    latch.Wait();
    std::this_thread::sleep_for(2000ms);
    ASSERT_OK(TerminateBackend(conn, aux_backend_pid));
  }
  ASSERT_OK(conn.CommitTransaction());
}

} // namespace yb::pgwrapper
