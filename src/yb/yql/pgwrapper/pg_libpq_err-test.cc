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

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {
namespace pgwrapper {

class PgLibPqErrTest : public LibPqTestBase {
};

TEST_F(PgLibPqErrTest, YB_DISABLE_TEST_IN_TSAN(BeginWithoutCommit)) {
  constexpr auto kIterations = 10;

  // Create table and insert some rows.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE terr (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO terr (k, v) VALUES (1, 1)"));

  // Run "BEGIN; SELECT;" blocks on several connections.
  // When there's many threads, Postgres didn't have enough time to clear its memory contexts
  // before exiting. This test is to show that Postgres's handles to SELECT statements will be
  // released during the exiting process.
  //
  // NOTES
  // - Transaction manager (TxnMan) holds reference to SELECT statement objects.
  // - Postgres will have to release SELECT handles when exiting. Otherwise, TxnMan process will
  //   not destroy the SELECT statement objects as it assumes Postgres process needs the handles.
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    std::atomic<int> complete{ 0 };
    std::vector<std::thread> threads;
    threads.emplace_back([this, iteration, &complete] {
      // Exec BEGIN block.
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.Execute("BEGIN ISOLATION LEVEL SERIALIZABLE"));
      auto res = conn.Fetch("SELECT * FROM terr");

      // Test early termination.
      // - Just return for one of the thread.
      // - We are not meant to test SELECT, so ignore its error for timeout, abort, ...
      if (iteration == 3 || !res.ok()) {
        return;
      }

      ++complete;
    });

    for (auto& thread : threads) {
      thread.join();
    }

    if (complete == 0) {
      continue;
    }
  }
}

} // namespace pgwrapper
} // namespace yb
