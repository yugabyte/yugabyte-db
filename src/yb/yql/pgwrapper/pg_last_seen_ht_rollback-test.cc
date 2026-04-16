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

#include <gtest/gtest.h>

#include "yb/common/transaction.pb.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

DECLARE_bool(disable_last_seen_ht_rollback);
DECLARE_bool(enable_scan_choices_variable_bloom_filter);
DECLARE_bool(ysql_colocate_database_by_default);
DECLARE_string(ysql_log_statement);
DECLARE_string(ysql_pg_conf_csv);

namespace yb::pgwrapper {

class PgLastSeenHtRollbackTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    // So that read restart errors are not retried internally.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) = MaxQueryLayerRetriesConf(0);
    // Disable colocation so that dummy tables do not interfere with
    // the 'keys' table.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_colocate_database_by_default) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_log_statement) = "all";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_scan_choices_variable_bloom_filter) = false;

    PgMiniTestBase::SetUp();
  }

  void PickReadTime(PGConn &read_conn) {
    ASSERT_OK(read_conn.Execute("CREATE TABLE dummy()"));
    // We pick read time by starting a REPEATABLE READ transaction,
    // and executing a statement that picks a read time.
    ASSERT_OK(read_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    // ASSUMPTION: the statement does not touch the keys table
    // or its tablets.
    auto count_rows = ASSERT_RESULT(
        read_conn.FetchRow<int64_t>("SELECT COUNT(*) FROM dummy"));
    ASSERT_EQ(count_rows, 0);
  }

  void RunSeenHtTest(bool expects_read_restart, std::optional<int> deleted_key) {
    auto setup_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(setup_conn.ExecuteFormat(
        "CREATE TABLE keys(k1 INT, k2 INT, PRIMARY KEY(k1 ASC, k2 ASC))"));

    // Pick read time before the non-conflicting row is inserted.
    auto read_conn = ASSERT_RESULT(Connect());
    // Populate cache before the read time is picked.
    // NOTE: Do not reuse the same keys as below here.
    ASSERT_OK(RunCmd(read_conn, true));
    PickReadTime(read_conn);

    // Insert a non-conflicting row.
    if (deleted_key) {
      ASSERT_OK(setup_conn.ExecuteFormat(
          "INSERT INTO keys(k1, k2) VALUES ($0, $0)", *deleted_key));
      ASSERT_OK(setup_conn.ExecuteFormat(
          "DELETE FROM keys WHERE k1 = $0 AND k2 = $0", *deleted_key));
    }
    ASSERT_OK(setup_conn.Execute("INSERT INTO keys(k1, k2) VALUES (1, 1)"));

    // Run insert-on-conflict.
    auto status = RunCmd(read_conn, false);
    if (expects_read_restart) {
      ASSERT_NOK(status);
      ASSERT_STR_CONTAINS(status.ToString(), "Restart read required");
    } else {
      ASSERT_OK(status);
    }
    ASSERT_OK(read_conn.RollbackTransaction());
  }

  Status RunCmd(PGConn& conn, bool populate_catcache) {
    return populate_catcache ? BatchedInsertOnConflict(conn, 10, 19)
                             : BatchedInsertOnConflict(conn, 0, 9);
  }

  Status BatchedInsertOnConflict(PGConn& conn, int row1, int row2) {
    RETURN_NOT_OK(conn.Execute("SET yb_insert_on_conflict_read_batch_size = 1024"));
    RETURN_NOT_OK(conn.Execute("SET yb_debug_log_docdb_requests = true"));
    std::string values = Format("($0, $0), ($1, $1)", row1, row2);
    return conn.ExecuteFormat(
        "INSERT INTO keys(k1, k2) VALUES $0 ON CONFLICT DO NOTHING", values);
  }

  Status BackwardScan(PGConn& conn, int row1, int row2) {
    RETURN_NOT_OK(conn.Execute("SET yb_debug_log_docdb_requests = true"));
    auto rows = VERIFY_RESULT(conn.FetchRows<int64_t>(Format(
        "SELECT k1 FROM keys WHERE k1 IN ($0, $1) ORDER BY k1 DESC", row1, row2)));
    SCHECK(rows.empty(), InternalError, "Rows not empty");
    return Status::OK();
  }
};

TEST_F(PgLastSeenHtRollbackTest, NoReadRestartWithInsertOnConflict) {
  RunSeenHtTest(false, std::nullopt);
}

TEST_F(PgLastSeenHtRollbackTest, NoReadRestartWithInvisibleKey) {
  RunSeenHtTest(false, 2);
}

TEST_F(PgLastSeenHtRollbackTest, ReadRestartWithDeletedKey) {
  RunSeenHtTest(true, 0);
}

class PgNoHtRollbackTest : public PgLastSeenHtRollbackTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_last_seen_ht_rollback) = true;
    PgLastSeenHtRollbackTest::SetUp();
  }
};

TEST_F_EX(PgLastSeenHtRollbackTest, ReadRestartWithInsertOnConflict, PgNoHtRollbackTest) {
  RunSeenHtTest(true, std::nullopt);
}

TEST_F_EX(PgLastSeenHtRollbackTest, ReadRestartWithInvisibleKey, PgNoHtRollbackTest) {
  RunSeenHtTest(true, 2);
}

} // namespace yb::pgwrapper
