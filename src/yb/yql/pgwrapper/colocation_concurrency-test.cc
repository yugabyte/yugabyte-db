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

#include "yb/util/test_macros.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/common/pgsql_error.h"

namespace yb {
namespace pgwrapper {

class ColocationConcurrencyTest : public LibPqTestBase {
 protected:
  const int num_iterations = 15;
  const std::string database_name = "yugabyte";
  const std::string colocated_db_name = "colocated_db";

  void CreateTables(yb::pgwrapper::PGConn* conn, const int num_tables);

  void InsertDataIntoTable(yb::pgwrapper::PGConn* conn, std::string table_name, int num_rows = 50);
};

void ColocationConcurrencyTest::CreateTables(yb::pgwrapper::PGConn* conn, const int num_tables) {
  for (int i = 1; i <= num_tables; ++i) {
    ASSERT_OK(conn->ExecuteFormat("CREATE TABLE t$0 (i int, j int)", i));
  }
}

void ColocationConcurrencyTest::InsertDataIntoTable(
    yb::pgwrapper::PGConn* conn, std::string table_name, int num_rows) {
  for (int i = 0; i < num_rows; ++i) {
    // Retry since concurrent DML operations sometimes run into Resource unavailable error.
    Status s = conn->ExecuteFormat("INSERT INTO $0 values ($1, $2)", table_name, i, i + 1);
    while (!s.ok()) {
      s = conn->ExecuteFormat("INSERT INTO $0 values ($1, $2)", table_name, i, i + 1);
    }
  }

  auto curr_rows = ASSERT_RESULT(conn->FetchValue<PGUint64>(
      Format("SELECT COUNT(*) FROM $0", table_name)));
  ASSERT_EQ(curr_rows, num_rows);
}

// Concurrent DML on table 1 + truncate table 2, where table 1 & 2 are colocated.
TEST_F(ColocationConcurrencyTest, YB_DISABLE_TEST_IN_TSAN(InsertAndTruncateOnSeparateTables)) {
  PGConn conn1 = ASSERT_RESULT(ConnectToDB(database_name));
  ASSERT_OK(conn1.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", colocated_db_name));
  conn1 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  PGConn conn2 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  CreateTables(&conn1, 2);

  for (int i = 0; i < num_iterations; ++i) {
    // Insert 50 rows in t1.
    InsertDataIntoTable(&conn1, "t1" /* table_name */);

    std::atomic<bool> done = false;
    int counter = 0;

    // Insert rows in t2 on a separate thread.
    std::thread insertion_thread([&conn2, &done, &counter] {
      while (!done) {
        ASSERT_OK(conn2.ExecuteFormat("INSERT INTO t2 VALUES ($0, $1)", counter, ++counter));
      }
    });

    // Truncate table t1 on main thread.
    ASSERT_OK(conn1.Execute("TRUNCATE TABLE t1"));
    done = true;
    insertion_thread.join();

    // Verify t1 is empty after truncate.
    auto curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t1"));
    ASSERT_EQ(curr_rows, 0);

    // Verify t2 has rows equal to the counter.
    curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t2"));
    ASSERT_EQ(curr_rows, counter);

    // Reset the tables for next iteration.
    ASSERT_OK(conn1.Execute("TRUNCATE TABLE t2"));
  }
}

// Concurrent DML on table A + truncate table B using generate_series, where table A & B are
// colocated.
TEST_F(
    ColocationConcurrencyTest,
    YB_DISABLE_TEST_IN_TSAN(InsertUsingGenerateSeriesAndTruncateOnSeparateTables)) {
  PGConn conn1 = ASSERT_RESULT(ConnectToDB(database_name));
  ASSERT_OK(conn1.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", colocated_db_name));
  conn1 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  PGConn conn2 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  CreateTables(&conn1, 2);

  for (int i = 0; i < num_iterations; ++i) {
    // Insert 50 rows in t1.
    InsertDataIntoTable(&conn1, "t1" /* table_name */);

    std::atomic<bool> done = false;
    int counter = 0;

    // Insert rows in t2 using generate_series on a separate thread.
    std::thread insertion_thread([&conn2, &done, &counter] {
      while (!done) {
        ASSERT_OK(conn2.ExecuteFormat(
            "INSERT INTO t2 values (generate_series($0,$1), generate_series($0,$1))",
            counter,
            ++counter));
      }
    });

    // Truncate t1 on main thread.
    ASSERT_OK(conn1.Execute("TRUNCATE TABLE t1"));
    done = true;
    insertion_thread.join();

    // Verify t1 is empty after truncate.
    auto curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t1"));
    ASSERT_EQ(curr_rows, 0);

    // Verify t2 has rows equal to counter.
    curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t2"));
    ASSERT_EQ(curr_rows, counter);

    // Reset the tables for next iteration.
    ASSERT_OK(conn1.Execute("TRUNCATE TABLE t2"));
  }
}

// Concurrent DML on table 1 + index backfill on table 2, where table 1 & 2 are colocated.
TEST_F(ColocationConcurrencyTest, YB_DISABLE_TEST_IN_TSAN(InsertAndIndexBackfillOnSeparateTables)) {
  PGConn conn1 = ASSERT_RESULT(ConnectToDB(database_name));
  ASSERT_OK(conn1.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", colocated_db_name));
  conn1 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  PGConn conn2 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  CreateTables(&conn1, 2);

  for (int i = 0; i < num_iterations; ++i) {
    // Insert 50 rows in t1.
    InsertDataIntoTable(&conn1, "t1" /* table_name */);

    std::atomic<bool> done = false;
    int counter = 0;

    // Insert rows in t2 on a separate thread.
    std::thread insertion_thread([&conn2, &done, &counter] {
      while (!done) {
        Status s = conn2.ExecuteFormat("INSERT INTO t2 VALUES ($0, $1)", counter, counter);
        // Retry if DML operations fails.
        while (!s.ok()) {
          s = conn2.ExecuteFormat("INSERT INTO t2 VALUES ($0, $1)", counter, counter);
        }
        ++counter;
      }
    });

    // create index on t1 on a main thread.
    ASSERT_OK(conn1.Execute("CREATE INDEX CONCURRENTLY t1_idx ON t1(i ASC)"));
    done = true;
    insertion_thread.join();

    // Verify contents of t1_idx.
    const std::string query = Format("SELECT * FROM t1 ORDER BY i");
    ASSERT_TRUE(ASSERT_RESULT(conn1.HasIndexScan(query)));
    PGResultPtr res = ASSERT_RESULT(conn1.Fetch(query));
    ASSERT_EQ(PQntuples(res.get()), 50);
    ASSERT_EQ(PQnfields(res.get()), 2);
    for (int i = 0; i < 50; ++i) {
      ASSERT_EQ(i, ASSERT_RESULT(GetInt32(res.get(), i, 0)));
    }

    // Verify t2 has rows equal to counter.
    auto curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t2"));
    ASSERT_EQ(curr_rows, counter);

    // Reset the tables for next iteration.
    ASSERT_OK(conn1.ExecuteFormat("DROP INDEX t1_idx"));
    ASSERT_OK(conn1.ExecuteFormat("TRUNCATE TABLE t1"));
    ASSERT_OK(conn1.ExecuteFormat("TRUNCATE TABLE t2"));
  }
}

// Remove GTEST_SKIP() after #15081 is resolved.
// Concurrent DML(Insert,Update, delete) + index backfill on same table, where table is colocated.
TEST_F(ColocationConcurrencyTest, YB_DISABLE_TEST_IN_TSAN(UpdateAndIndexBackfillOnSameTable)) {
  GTEST_SKIP();
  PGConn conn1 = ASSERT_RESULT(ConnectToDB(database_name));
  ASSERT_OK(conn1.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", colocated_db_name));
  conn1 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  PGConn conn2 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  CreateTables(&conn1, 1);

  for (int i = 0; i < num_iterations; ++i) {
    // Insert 50 rows in t1.
    InsertDataIntoTable(&conn1, "t1" /* table_name */);

    std::atomic<bool> done = false;
    int counter = 0;

    std::thread dml_thread([&conn2, &done, &counter] {
      // Insert, Update and delete rows in t1. This sometimes throw a Network error stating resource
      // unavailable. To overcome this, we retry the DML operation.
      while (!done) {
        Status s =
            conn2.ExecuteFormat("INSERT INTO t1 VALUES ($0, $1)", counter + 50, counter + 50);
        while (!s.ok()) {
          s = conn2.ExecuteFormat("INSERT INTO t1 VALUES ($0, $1)", counter + 50, counter + 50);
        }
        s = conn2.ExecuteFormat("UPDATE t1 SET j = j + $0 WHERE i > 0", counter);
        while (!s.ok()) {
          s = conn2.ExecuteFormat("UPDATE t1 SET j = j + $0 WHERE i > 0", counter);
        }
        s = conn2.ExecuteFormat("DELETE FROM t1 where i=$0", counter);
        while (!s.ok()) {
          s = conn2.ExecuteFormat("DELETE FROM t1 where i=$0", counter);
        }

        counter++;
      }
    });

    // create index on t1 in a separate thread.
    Status s = conn1.Execute("CREATE INDEX CONCURRENTLY t1_idx ON t1(i ASC)");
    while (!s.ok()) {
      s = conn1.Execute("CREATE INDEX CONCURRENTLY t1_idx ON t1(i ASC)");
    }
    done = true;
    dml_thread.join();

    // Verify contents of t1_idx.
    std::string query = "SELECT * FROM t1 ORDER BY i";
    ASSERT_TRUE(ASSERT_RESULT(conn1.HasIndexScan(query)));
    PGResultPtr res = ASSERT_RESULT(conn1.Fetch(query));
    ASSERT_EQ(PQntuples(res.get()), 50);
    ASSERT_EQ(PQnfields(res.get()), 2);

    // Reset the table for next iteration.
    ASSERT_OK(conn1.ExecuteFormat("DROP INDEX t1_idx"));
    ASSERT_OK(conn1.ExecuteFormat("TRUNCATE TABLE t1"));
  }
}

// Concurrent DDL (Create + Truncate) on different colocated tables.
TEST_F(ColocationConcurrencyTest, YB_DISABLE_TEST_IN_TSAN(CreateAndTruncateOnSeparateTables)) {
  PGConn conn1 = ASSERT_RESULT(ConnectToDB(database_name));
  ASSERT_OK(conn1.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", colocated_db_name));
  conn1 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  PGConn conn2 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  CreateTables(&conn1, 2);

  for (int i = 0; i < num_iterations; ++i) {
    // Insert 50 rows in t1.
    InsertDataIntoTable(&conn1, "t1" /* table_name */);

    // Insert 50 rows in t2.
    InsertDataIntoTable(&conn1, "t2" /* table_name */);

    // create index on t1 on a separate thread.
    std::thread create_index_thread(
        [&conn2] { ASSERT_OK(conn2.Execute("CREATE INDEX CONCURRENTLY t1_idx ON t1(i ASC)")); });

    ASSERT_OK(conn1.Execute("TRUNCATE TABLE t2"));
    create_index_thread.join();

    // Verify contents of t1_idx.
    const std::string query = Format("SELECT * FROM t1 ORDER BY i");
    ASSERT_TRUE(ASSERT_RESULT(conn1.HasIndexScan(query)));
    PGResultPtr res = ASSERT_RESULT(conn1.Fetch(query));
    ASSERT_EQ(PQntuples(res.get()), 50);
    ASSERT_EQ(PQnfields(res.get()), 2);
    for (int i = 0; i < 50; ++i) {
      ASSERT_EQ(i, ASSERT_RESULT(GetInt32(res.get(), i, 0)));
    }

    // Verify t2 has 0 rows
    auto curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t2"));
    ASSERT_EQ(curr_rows, 0);

    // Reset the tables for next iteration.
    ASSERT_OK(conn1.ExecuteFormat("DROP INDEX t1_idx"));
    ASSERT_OK(conn1.ExecuteFormat("TRUNCATE TABLE t1"));
  }
}

// Concurrent DMLs (delete, update) on different colocated tables, with unique constraints.
TEST_F(ColocationConcurrencyTest, YB_DISABLE_TEST_IN_TSAN(UpdateAndDeleteOnSeparateTables)) {
  PGConn conn1 = ASSERT_RESULT(ConnectToDB(database_name));
  ASSERT_OK(conn1.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", colocated_db_name));
  conn1 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  PGConn conn2 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  ASSERT_OK(conn1.ExecuteFormat("CREATE TABLE t1 (a int, b int, UNIQUE(a,b))"));
  ASSERT_OK(conn1.ExecuteFormat("CREATE TABLE t2 (i int UNIQUE, j int)"));

  for (int i = 0; i < num_iterations; ++i) {
    // Insert 50 rows in t1.
    InsertDataIntoTable(&conn1, "t1" /* table_name */);

    InsertDataIntoTable(&conn1, "t2" /* table_name */);

    std::atomic<bool> done = false;
    int counter = 0;

    // Update operation on t1 in a separate thread.
    std::thread update_values_thread([&conn1, &done, &counter] {
      while (!done) {
        Status s = conn1.ExecuteFormat("UPDATE t1 SET a = a * $0 WHERE b%2 = 0", counter);
        while (!s.ok()) {
          s = conn1.ExecuteFormat("UPDATE t1 SET a = a * $0 WHERE b%2 = 0", counter);
        }
        ++counter;
      }
    });

    // Delete 25 rows in t2
    ASSERT_OK(conn2.Execute("DELETE FROM t2 where i%2 = 0"));
    done = true;
    update_values_thread.join();

    // Verify t1 has 50 rows.
    auto curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t1"));
    ASSERT_EQ(curr_rows, 50);

    // Verify t2 has 25 rows
    curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t2"));
    ASSERT_EQ(curr_rows, 25);

    // Reset the tables for next iteration.
    ASSERT_OK(conn1.ExecuteFormat("TRUNCATE TABLE t1"));
    ASSERT_OK(conn1.ExecuteFormat("TRUNCATE TABLE t2"));
  }
}

// TODO: Remove GTEST_SKIP() after 14468 is resolved.
// Concurrent DDL (create/alter/drop) + DML operation on same colocated table.
TEST_F(ColocationConcurrencyTest, YB_DISABLE_TEST_IN_TSAN(AlterAndUpdateOnSameTable)) {
  GTEST_SKIP();
  PGConn conn1 = ASSERT_RESULT(ConnectToDB(database_name));
  ASSERT_OK(conn1.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", colocated_db_name));
  conn1 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  PGConn conn2 = ASSERT_RESULT(ConnectToDB(colocated_db_name));

  for (int i = 0; i < num_iterations; ++i) {
    // Insert 50 rows in t1.
    ASSERT_OK(conn1.ExecuteFormat("CREATE TABLE t1 (i int, j int)"));
    InsertDataIntoTable(&conn1, "t1" /* table_name */);

    std::atomic<bool> done = false;
    int counter = 0;

    // Update rows in t1 on a separate thread.
    std::thread update_thread([&conn2, &done, &counter] {
      while (!done) {
        Status s = conn2.ExecuteFormat("UPDATE t1 SET j = j * $0 WHERE i > 0", counter);
        while (!s.ok()) {
          s = conn2.ExecuteFormat("UPDATE t1 SET j = j * $0 WHERE i > 0", counter);
        }
        ++counter;
      }
    });

    // Add a column in t1 on the main thread.
    Status s = conn1.Execute("ALTER TABLE t1 ADD COLUMN k INT DEFAULT 0");
    while (!s.ok()) {
      s = conn1.Execute("ALTER TABLE t1 ADD COLUMN k INT DEFAULT 0");
    }
    done = true;
    update_thread.join();

    // Verify that t1 has 3 columns.
    std::string query =
        "SELECT column_name, data_type FROM information_schema.columns WHERE table_name = 't1'";
    PGResultPtr res = ASSERT_RESULT(conn1.Fetch(query));
    ASSERT_EQ(PQntuples(res.get()), 3);

    // Verify t1 has 50 rows.
    auto curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t1"));
    ASSERT_EQ(curr_rows, 50);

    // Reset the table for next iteration.
    ASSERT_OK(conn1.ExecuteFormat("DROP TABLE t1"));
  }
}

// Concurrent transactions on different colocated tables.
TEST_F(ColocationConcurrencyTest, YB_DISABLE_TEST_IN_TSAN(TxnsOnSeparateTables)) {
  PGConn conn1 = ASSERT_RESULT(ConnectToDB(database_name));
  ASSERT_OK(conn1.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", colocated_db_name));
  conn1 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  PGConn conn2 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  CreateTables(&conn1, 2);

  for (int i = 0; i < num_iterations; ++i) {
    std::atomic<bool> done = false;
    int counter = 0;

    // Insert 50 rows in t1 on a separate thread.
    std::thread insert_thread([&conn2, &done, &counter] {
      ASSERT_OK(conn2.Execute("BEGIN"));
      while (!done) {
        ASSERT_OK(conn2.ExecuteFormat("INSERT INTO t2 values ($0)", ++counter));
      }
      ASSERT_OK(conn2.Execute("COMMIT"));
    });

    ASSERT_OK(conn1.Execute("BEGIN"));
    for (int j = 0; j < 50; j++) {
      ASSERT_OK(conn1.ExecuteFormat("INSERT INTO t1 values ($0)", j));
    }
    ASSERT_OK(conn1.Execute("COMMIT"));
    done = true;
    insert_thread.join();

    // Verify t1 has 50 rows.
    auto curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t1"));
    ASSERT_EQ(curr_rows, 50);

    // Verify t2 has rows equal to counter.
    curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t2"));
    ASSERT_EQ(curr_rows, counter);

    // Reset the tables for next iteration.
    ASSERT_OK(conn1.ExecuteFormat("TRUNCATE TABLE t1"));
    ASSERT_OK(conn1.ExecuteFormat("TRUNCATE TABLE t2"));
  }
}

// Concurrent DMLs on two tables with Foreign key relationship.
TEST_F(ColocationConcurrencyTest, YB_DISABLE_TEST_IN_TSAN(InsertOnTablesWithFK)) {
  PGConn conn1 = ASSERT_RESULT(ConnectToDB(database_name));
  ASSERT_OK(conn1.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", colocated_db_name));
  conn1 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  PGConn conn2 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  ASSERT_OK(conn1.ExecuteFormat("CREATE TABLE t1 (a int PRIMARY KEY, b int)"));
  ASSERT_OK(
      conn1.ExecuteFormat("CREATE TABLE t2 (i int, j int REFERENCES t1(a) ON DELETE CASCADE)"));

  for (int i = 0; i < num_iterations; ++i) {
    // Insert 50 rows in t1.
    InsertDataIntoTable(&conn1, "t1" /* table_name */, 500);

    std::atomic<bool> done = false;
    int counter = 0;

    // Insert rows in t2 on a separate thread.
    std::thread insertion_thread([&conn2, &done, &counter] {
      while (!done) {
        Status s = conn2.ExecuteFormat("INSERT INTO t2 VALUES ($0, $1)", counter, ++counter);
        if (!s.ok()) {
          s = conn2.ExecuteFormat("INSERT INTO t2 VALUES ($0, $1)", counter, ++counter);
        }
        // Verify insert prevention due to FK constraints.
        s = conn2.Execute("INSERT INTO t2 VALUES (999, 999)");
        ASSERT_FALSE(s.ok());
        ASSERT_EQ(PgsqlError(s), YBPgErrorCode::YB_PG_FOREIGN_KEY_VIOLATION);
        ASSERT_STR_CONTAINS(s.ToString(), "violates foreign key constraint");
      }
    });

    for (int j = 500; j < 600; ++j) {
      Status s = conn1.ExecuteFormat("INSERT INTO t1 values ($0, $1)", j, j + 1);
      if (!s.ok()) {
        s = conn1.ExecuteFormat("INSERT INTO t1 values ($0, $1)", j, j + 1);
      }
    }

    // Verify for CASCADE behaviour.
    ASSERT_OK(conn1.Execute("DELETE FROM t1 where a = 10"));
    done = true;
    insertion_thread.join();

    // Verify t1 has 599 (600 - 1) rows.
    auto curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t1"));
    ASSERT_EQ(curr_rows, 599);

    // Verify t2 has counter - 1 rows
    curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t2"));
    ASSERT_EQ(curr_rows, counter - 1);

    // Reset the tables for next iteration.
    ASSERT_OK(conn1.Execute("TRUNCATE TABLE t1 CASCADE"));
    curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t2"));
    ASSERT_EQ(curr_rows, 0);
  }
}

// Transaction on two tables with FK relationship.
TEST_F(ColocationConcurrencyTest, YB_DISABLE_TEST_IN_TSAN(TransactionsOnTablesWithFK)) {
  PGConn conn1 = ASSERT_RESULT(ConnectToDB(database_name));
  ASSERT_OK(conn1.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", colocated_db_name));
  conn1 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  PGConn conn2 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  ASSERT_OK(conn1.ExecuteFormat("CREATE TABLE t1 (a int PRIMARY KEY, b int)"));
  ASSERT_OK(
      conn1.ExecuteFormat("CREATE TABLE t2 (i int, j int REFERENCES t1(a) ON DELETE CASCADE)"));

  for (int i = 0; i < num_iterations; ++i) {
    // Insert 50 rows in t1.
    InsertDataIntoTable(&conn1, "t1" /* table_name */, 500);

    std::atomic<bool> done = false;
    int counter = 0;

    // Insert rows int t2 on a separate thread.
    std::thread insertion_thread([&conn2, &done, &counter] {
      ASSERT_OK(conn2.Execute("BEGIN"));
      while (!done) {
        Status s = conn2.ExecuteFormat("INSERT INTO t2 values ($0, $1)", counter, counter + 1);
        if (!s.ok()) {
          s = conn2.ExecuteFormat("INSERT INTO t2 values ($0, $1)", counter, counter + 1);
        }
        ++counter;
      }
      ASSERT_OK(conn2.Execute("COMMIT"));

      // Verify insert prevention due to FK constraints.
      Status s = conn2.Execute("INSERT INTO t2 VALUES (1000, 1250)");
      ASSERT_FALSE(s.ok());
      ASSERT_EQ(PgsqlError(s), YBPgErrorCode::YB_PG_FOREIGN_KEY_VIOLATION);
      ASSERT_STR_CONTAINS(s.ToString(), "violates foreign key constraint");
    });

    ASSERT_OK(conn1.Execute("BEGIN"));
    for (int j = 500; j < 550; ++j) {
      Status s = conn1.ExecuteFormat("INSERT INTO t1 values ($0, $1)", j, j + 1);
      if (!s.ok()) {
        s = conn1.ExecuteFormat("INSERT INTO t1 values ($0, $1)", j, j + 1);
      }
    }
    ASSERT_OK(conn1.Execute("COMMIT"));
    done = true;
    insertion_thread.join();

    // Verify for CASCADE behaviour.
    ASSERT_OK(conn1.Execute("DELETE FROM t1 where a = 10"));

    auto curr_rows =
        ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t2 where j = 10"));
    ASSERT_EQ(curr_rows, 0);

    curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t1 where a =10"));
    ASSERT_EQ(curr_rows, 0);

    // Verify t1 has 549 (550 - 1) rows.
    curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t1"));
    ASSERT_EQ(curr_rows, 549);

    // Verify t2 has rows equal to counter - 1.
    curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t2"));
    ASSERT_EQ(curr_rows, counter - 1);

    // Reset the tables for next iteration.
    ASSERT_OK(conn1.Execute("TRUNCATE TABLE t1 CASCADE"));
    curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t2"));
    ASSERT_EQ(curr_rows, 0);
  }
}

}  // namespace pgwrapper
}  // namespace yb
