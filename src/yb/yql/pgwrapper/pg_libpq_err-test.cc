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

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

using std::string;

namespace yb {
namespace pgwrapper {

class PgLibPqErrTest : public LibPqTestBase {
};

TEST_F(PgLibPqErrTest, BeginWithoutCommit) {
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
      auto connection = ASSERT_RESULT(Connect());
      ASSERT_OK(connection.Execute("BEGIN ISOLATION LEVEL SERIALIZABLE"));
      auto res = connection.Fetch("SELECT * FROM terr");

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

TEST_F(PgLibPqErrTest, InsertWithoutCommit) {
  constexpr auto kRetryCount = 3;
  constexpr auto kIterations = 10;
  constexpr auto kRowPerSeed = 100;

  // Create table.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE terr (k INT PRIMARY KEY, v INT)"));

  int seed = 0;
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    // Use different seed for primary key to avoid duplicate among thread.
    seed++;

    std::atomic<int> complete{ 0 };
    std::vector<std::thread> threads;
    threads.emplace_back([this, seed, &complete] {
      auto connection = ASSERT_RESULT(Connect());
      ASSERT_OK(connection.Execute("BEGIN"));

      // INSERT.
      int row_count = 0;
      for (int k = 0; k != kRowPerSeed; ++k) {
        // For each row of data, retry a number of times or until success.
        for (int rt = 0; rt < kRetryCount; rt++) {
          auto status = connection.ExecuteFormat("INSERT INTO terr VALUES ($0, $1)",
                                      seed * (k + 1), seed);
          if (!status.ok()) {
            string serr = status.ToString();
            if (serr.find("aborted") != std::string::npos) {
              // If DocDB aborted the execution, the transaction cannot be continued.
              return;
            }
            // Run the statement again as "Try again" was reported.
            ASSERT_TRUE(HasTransactionError(status)) << status;
            continue;
          }

          // INSERT succeeded.
          row_count++;
          break;
        }
      }

      // SELECT: Retry number of times or until success.
      for (int rt = 0; rt < kRetryCount; rt++) {
        auto res = connection.FetchMatrix("SELECT * FROM terr", row_count, 2);
        if (!res.ok()) {
          ASSERT_TRUE(HasTransactionError(res.status())) << res.status();
          continue;
        }

        // SELECT succeeded.
        break;
      }

      ++complete;
    });

    for (auto& thread : threads) {
      thread.join();
    }

    if (complete == 0) {
      continue;
    }

    // Table should have no data as transactions were not committed.
    ASSERT_OK(conn.FetchMatrix("SELECT * FROM terr", 0, 2));
  }
}

TEST_F(PgLibPqErrTest, InsertDuplicateWithoutCommit) {
  constexpr auto kRetryCount = 3;
  constexpr auto kIterations = 10;
  constexpr auto kRowPerSeed = 100;

  // Create table.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE terr (k INT PRIMARY KEY, v INT)"));

  // Use the same insertion seed for all threads to get same row value.
  // This should NOT cause duplicate key error as they are on different transaction.
  int seed = 1;
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    std::atomic<int> complete{ 0 };
    std::vector<std::thread> threads;
    threads.emplace_back([this, seed, &complete] {
      auto connection = ASSERT_RESULT(Connect());
      ASSERT_OK(connection.Execute("BEGIN"));

      // INSERT.
      int row_count = 0;
      for (int k = 0; k != kRowPerSeed; ++k) {
        // For each row of data, retry a number of times or until success.
        for (int rt = 0; rt < kRetryCount; rt++) {
          auto status = connection.ExecuteFormat("INSERT INTO terr VALUES ($0, $1)",
                                           seed * (k + 1), seed);
          if (!status.ok()) {
            string serr = status.ToString();
            if (serr.find("aborted") != std::string::npos) {
              // If DocDB aborted the execution, the transaction cannot be continued.
              return;
            }
            // Run the statement again as "Try again" was reported.
            ASSERT_TRUE(HasTransactionError(status)) << status;
            continue;
          }

          // INSERT succeeded.
          row_count++;
          break;
        }
      }

      // SELECT: Retry number of times or until success.
      for (int rt = 0; rt < kRetryCount; rt++) {
        auto res = connection.FetchMatrix("SELECT * FROM terr", row_count, 2);
        if (!res.ok()) {
          ASSERT_TRUE(HasTransactionError(res.status())) << res.status();
          continue;
        }

        // SELECT succeeded.
        break;
      }

      ++complete;
    });

    for (auto& thread : threads) {
      thread.join();
    }

    if (complete == 0) {
      continue;
    }

    // Table should have no data as transactions were not committed.
    ASSERT_OK(conn.FetchMatrix("SELECT * FROM terr", 0, 2));
  }
}

TEST_F(PgLibPqErrTest, UpdateWithoutCommit) {
  constexpr auto kRetryCount = 3;
  constexpr auto kIterations = 10;
  constexpr auto kRowCount = 100;
  constexpr auto kSeed = 7;

  // Create table.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE terr (k INT PRIMARY KEY, v INT)"));

  // INSERT a few rows.
  int seed = kSeed;
  for (int k = 0; k != kRowCount; ++k) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO terr VALUES ($0, $1)", k, seed));
  }

  for (int iteration = 1; iteration <= kIterations; ++iteration) {
    seed = kSeed + iteration;

    std::atomic<int> complete{ 0 };
    std::vector<std::thread> threads;
    threads.emplace_back([this, seed, kRowCount, &complete] {
      auto connection = ASSERT_RESULT(Connect());
      ASSERT_OK(connection.Execute("BEGIN"));

      // UPDATE using different seeds.
      for (int k = 0; k != kRowCount; ++k) {
        for (int rt = 0; rt < kRetryCount; rt++) {
          auto status = connection.ExecuteFormat("UPDATE terr SET v = $0 WHERE k = $1", seed, k);
          if (!status.ok()) {
            string serr = status.ToString();
            if (serr.find("aborted") != std::string::npos) {
              // If DocDB aborted the execution, the transaction cannot be continued.
              return;
            }
            // Run the statement again as "Try again" was reported.
            ASSERT_TRUE(HasTransactionError(status)) << status;
            continue;
          }
        }
      }

      // SELECT: Retry number of times or until success.
      for (int rt = 0; rt < kRetryCount; rt++) {
        auto rows_res = connection.FetchRows<int32_t, int32_t>("SELECT * FROM terr");
        if (!rows_res.ok()) {
          ASSERT_TRUE(HasTransactionError(rows_res.status())) << rows_res.status();
          continue;
        }

        // Done selecting. Retry not needed.
        ASSERT_EQ(rows_res->size(), kRowCount);
        // Check column 'v' vs 'seed'.
        for (const auto& [_, row_seed] : *rows_res) {
          ASSERT_EQ(row_seed, seed);
        }
        break;
      }

      ++complete;
    });

    for (auto& thread : threads) {
      thread.join();
    }

    if (complete == 0) {
      continue;
    }

    // Table should have no updates as transactions were not committed.
    auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t>("SELECT * FROM terr")));
    ASSERT_EQ(rows.size(), kRowCount);
    // Check column 'v' vs original value 'kSeed'.
    for (const auto& [_, row_seed] : rows) {
      ASSERT_EQ(row_seed, kSeed);
    }
  }
}

TEST_F(PgLibPqErrTest, DeleteWithoutCommit) {
  constexpr auto kRetryCount = 3;
  constexpr auto kIterations = 10;
  constexpr auto kRowCount = 100;
  constexpr auto kSeed = 7;

  // Create table.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE terr (k INT PRIMARY KEY, v INT)"));

  // INSERT a few rows.
  int seed = kSeed;
  for (int k = 0; k != kRowCount; ++k) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO terr VALUES ($0, $1)", k, seed));
  }

  for (int iteration = 1; iteration <= kIterations; ++iteration) {
    std::atomic<int> complete{ 0 };
    std::vector<std::thread> threads;
    threads.emplace_back([this, &complete] {
      auto connection = ASSERT_RESULT(Connect());
      ASSERT_OK(connection.Execute("BEGIN"));

      // DELETE.
      int delete_count = 0;
      for (int k = 0; k != kRowCount; ++k) {
        // For each row of data, retry a number of times or until success.
        for (int rt = 0; rt < kRetryCount; rt++) {
          auto status = connection.ExecuteFormat("DELETE FROM terr WHERE k = $0", k);
          if (!status.ok()) {
            string serr = status.ToString();
            if (serr.find("aborted") != std::string::npos) {
              // If DocDB aborted the execution, the transaction cannot be continued.
              return;
            }
            // Run the statement again as "Try again" was reported.
            ASSERT_TRUE(HasTransactionError(status)) << status;
            continue;
          }

          // DELETE succeeded.
          delete_count++;
          break;
        }
      }

      // SELECT: Retry number of times or until success.
      for (int rt = 0; rt < kRetryCount; rt++) {
        auto res = connection.FetchMatrix("SELECT * FROM terr", kRowCount - delete_count, 2);
        if (!res.ok()) {
          ASSERT_TRUE(HasTransactionError(res.status())) << res.status();
          continue;
        }

        // SELECT succeeded.
        break;
      }

      ++complete;
    });

    for (auto& thread : threads) {
      thread.join();
    }

    if (complete == 0) {
      continue;
    }

    // Table should still have all the rows as transactions were not committed.
    ASSERT_OK(conn.FetchMatrix("SELECT * FROM terr", kRowCount, 2));
  }
}

TEST_F(PgLibPqErrTest, InsertTransactionAborted) {
  constexpr auto kIterations = 10;
  constexpr auto kRowPerSeed = 100;

  // Create table.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE terr (k INT PRIMARY KEY, v INT)"));

  // INSERT.
  int seed = 1;
  for (int k = 0; k != kRowPerSeed; ++k) {
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO terr VALUES ($0, $1)",
                                   seed * (k + 1), seed));
  }

  // Use the same insertion seed for all threads to cause duplicate key.
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    std::atomic<int> complete{ 0 };
    std::vector<std::thread> threads;
    threads.emplace_back([this, seed, &complete] {
      auto connection = ASSERT_RESULT(Connect());
      ASSERT_OK(connection.Execute("BEGIN"));

      // INSERT.
      for (int k = 0; k != kRowPerSeed; ++k) {
        auto status = connection.ExecuteFormat("INSERT INTO terr VALUES ($0, $1)",
                                         seed * (k + 1), seed);
        CHECK(!status.ok()) << "INSERT is expected to fail";

        string serr = status.ToString();
        CHECK(serr.find("duplicate") != std::string::npos ||
              serr.find("aborted") != std::string::npos)
          << "Expecting duplicate key or transaction aborted";
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
