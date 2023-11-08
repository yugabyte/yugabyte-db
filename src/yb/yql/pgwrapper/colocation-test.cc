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

#include "yb/common/pgsql_error.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/test_macros.h"
#include "yb/util/thread.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"

namespace yb {
namespace pgwrapper {

// Tests that exercise functionality specific to the colocation feature.

class ColocatedDBTest : public LibPqTestBase {
 protected:
  int GetNumMasters() const override { return 3; }
  Result<PGConn> CreateColocatedDB(std::string db_name) {
    PGConn conn = VERIFY_RESULT(ConnectToDB("yugabyte"));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", db_name));
    return ConnectToDB(db_name);
  }
};

TEST_F(ColocatedDBTest, TestMasterToTServerRetryAddTableToTablet) {
  auto conn = ASSERT_RESULT(CreateColocatedDB("test_db"));

  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_duplicate_addtabletotablet_request", "true"));
  ASSERT_OK(conn.Execute("CREATE TABLE test_tbl1 (key INT PRIMARY KEY, value TEXT)"));
}

TEST_F(ColocatedDBTest, MasterFailoverRetryAddTableToTablet) {
  const std::string db_name = "test_db";
  auto conn = ASSERT_RESULT(CreateColocatedDB(db_name));

  ASSERT_OK(cluster_->SetFlag(
      cluster_->GetLeaderMaster(), "TEST_stuck_add_tablet_to_table_task_enabled", "true"));

  Status s;
  std::thread insertion_thread([&] {
    s = conn.Execute("CREATE TABLE test_tbl1 (key INT PRIMARY KEY, value INT)");
  });

  // Wait for table to reach PREPARING state.
  auto conn2 = ASSERT_RESULT(ConnectToDB(db_name));
  ASSERT_NOK(WaitFor(
      [&]() {
        auto s = conn2.Fetch("SELECT * FROM test_tbl1");
        return s.ok();
      },
      MonoDelta::FromSeconds(10),
      "Wait for table to get created"));

  ASSERT_OK(cluster_->SetFlag(
      cluster_->GetLeaderMaster(), "TEST_stuck_add_tablet_to_table_task_enabled", "false"));

  ASSERT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader());

  insertion_thread.join();
  ASSERT_OK(s);
  ASSERT_OK(conn.Execute("INSERT INTO test_tbl1 values (1, 1)"));
}

TEST_F(ColocatedDBTest, ListColocatedTables) {
  constexpr auto ns_name = "test_db";
  auto conn = ASSERT_RESULT(CreateColocatedDB(ns_name));
  ASSERT_OK(conn.Execute("CREATE TABLE colo_tbl (key INT PRIMARY KEY, value INT)"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE reg_tbl (key INT PRIMARY KEY, value INT) WITH (COLOCATION = FALSE)"));

  master::ListTablesRequestPB req;
  master::ListTablesResponsePB resp;
  req.mutable_namespace_()->set_name(ns_name);
  req.mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
  auto proxy = cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>();
  rpc::RpcController controller;
  ASSERT_OK(proxy.ListTables(req, &resp, &controller));
  const master::ListTablesResponsePB_TableInfo *parent = nullptr, *colo = nullptr, *reg = nullptr;
  for (const auto& table_info : resp.tables()) {
    if (table_info.name() == "colo_tbl") {
      colo = &table_info;
    } else if (table_info.name() == "reg_tbl") {
      reg = &table_info;
    } else if (table_info.relation_type() == master::COLOCATED_PARENT_TABLE_RELATION) {
      parent = &table_info;
    }
  }
  ASSERT_TRUE(parent != nullptr);
  ASSERT_TRUE(colo != nullptr);
  ASSERT_TRUE(reg != nullptr);
  EXPECT_FALSE(reg->has_colocated_info());
  EXPECT_TRUE(colo->colocated_info().colocated());
  EXPECT_EQ(colo->colocated_info().parent_table_id(), parent->id());
  EXPECT_TRUE(parent->colocated_info().colocated());
  EXPECT_FALSE(parent->colocated_info().has_parent_table_id());
}

class ColocationConcurrencyTest : public ColocatedDBTest {
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
TEST_F(ColocationConcurrencyTest, InsertAndTruncateOnSeparateTables) {
  PGConn conn1 = ASSERT_RESULT(CreateColocatedDB(colocated_db_name));
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
    InsertUsingGenerateSeriesAndTruncateOnSeparateTables) {
  PGConn conn1 = ASSERT_RESULT(CreateColocatedDB(colocated_db_name));
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
TEST_F(ColocationConcurrencyTest, InsertAndIndexBackfillOnSeparateTables) {
  PGConn conn1 = ASSERT_RESULT(CreateColocatedDB(colocated_db_name));
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
    const auto query = Format("SELECT * FROM t1 ORDER BY i");
    ASSERT_TRUE(ASSERT_RESULT(conn1.HasIndexScan(query)));
    const auto rows = ASSERT_RESULT((conn1.FetchRows<int32_t, int32_t>(query)));
    ASSERT_EQ(rows.size(), 50);
    auto index = 0;
    for (const auto& [i_val, _] : rows) {
      ASSERT_EQ(i_val, index++);
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
TEST_F(ColocationConcurrencyTest, UpdateAndIndexBackfillOnSameTable) {
  GTEST_SKIP();
  PGConn conn1 = ASSERT_RESULT(CreateColocatedDB(colocated_db_name));
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
    ASSERT_OK(conn1.FetchMatrix(query, 50, 2));

    // Reset the table for next iteration.
    ASSERT_OK(conn1.ExecuteFormat("DROP INDEX t1_idx"));
    ASSERT_OK(conn1.ExecuteFormat("TRUNCATE TABLE t1"));
  }
}

// Concurrent DDL (Create + Truncate) on different colocated tables.
TEST_F(ColocationConcurrencyTest, CreateAndTruncateOnSeparateTables) {
  PGConn conn1 = ASSERT_RESULT(CreateColocatedDB(colocated_db_name));
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
    const auto query = Format("SELECT * FROM t1 ORDER BY i");
    ASSERT_TRUE(ASSERT_RESULT(conn1.HasIndexScan(query)));
    const auto rows = ASSERT_RESULT((conn1.FetchRows<int32_t, int32_t>(query)));
    ASSERT_EQ(rows.size(), 50);
    auto index = 0;
    for (const auto& [i_val, _] : rows) {
      ASSERT_EQ(i_val, index++);
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
TEST_F(ColocationConcurrencyTest, UpdateAndDeleteOnSeparateTables) {
  PGConn conn1 = ASSERT_RESULT(CreateColocatedDB(colocated_db_name));
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
TEST_F(ColocationConcurrencyTest, AlterAndUpdateOnSameTable) {
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
    const auto query =
        "SELECT column_name, data_type FROM information_schema.columns WHERE table_name = 't1'";
    ASSERT_OK(conn1.FetchMatrix(query, 3, 2));

    // Verify t1 has 50 rows.
    auto curr_rows = ASSERT_RESULT(conn1.FetchValue<int64_t>("SELECT COUNT(*) FROM t1"));
    ASSERT_EQ(curr_rows, 50);

    // Reset the table for next iteration.
    ASSERT_OK(conn1.ExecuteFormat("DROP TABLE t1"));
  }
}

// Concurrent transactions on different colocated tables.
TEST_F(ColocationConcurrencyTest, TxnsOnSeparateTables) {
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
TEST_F(ColocationConcurrencyTest, InsertOnTablesWithFK) {
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
TEST_F(ColocationConcurrencyTest, TransactionsOnTablesWithFK) {
  PGConn conn1 = ASSERT_RESULT(ConnectToDB(database_name));
  ASSERT_OK(conn1.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", colocated_db_name));
  conn1 = ASSERT_RESULT(ConnectToDB(colocated_db_name));
  ASSERT_OK(conn1.ExecuteFormat("CREATE TABLE t1 (a int PRIMARY KEY, b int)"));
  ASSERT_OK(
      conn1.ExecuteFormat("CREATE TABLE t2 (i int, j int REFERENCES t1(a) ON DELETE CASCADE)"));
  PGConn conn2 = ASSERT_RESULT(ConnectToDB(colocated_db_name));

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
