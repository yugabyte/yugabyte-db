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

#include <initializer_list>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>

#include "yb/util/range.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"

namespace yb::pgwrapper {

using namespace std::literals;

namespace {
constexpr auto kTableName = "t1"sv;

Status CheckIsExpectedError(const Status& status, std::string_view expected_error_substr) {
  SCHECK(
      !status.ok() && ToString(status).find(expected_error_substr) != std::string::npos,
      IllegalState,
      "Unexpected status: '$0', expected error substr is '$1'", status, expected_error_substr);
  return Status::OK();
}

struct UnsupportedTxnLevelInfo {
  constexpr UnsupportedTxnLevelInfo(
    std::string_view level_, std::string_view expected_error_substr_)
    : level(level_), expected_error_substr(expected_error_substr_) {}

  std::string_view level;
  std::string_view expected_error_substr;
};

template<class Container>
auto MakeSet(const Container& c) {
  return std::set<std::decay_t<decltype(*c.begin())>>(c.begin(), c.end());
}

template<class Container>
auto MakeSetResult(const Result<Container>& res) -> Result<decltype(MakeSet(*res))> {
  RETURN_NOT_OK(res);
  return MakeSet(*res);
}

class PgExportSnapshotTest : public LibPqTestBase {
  using UnsupportedTxnLevelFunctor = std::function<Status(PGConn&)>;

  static Status RunOnUnsupportedTxnLevel(
      const UnsupportedTxnLevelInfo& txn_info, PGConn& conn,
      const UnsupportedTxnLevelFunctor& functor) {
    RETURN_NOT_OK(conn.ExecuteFormat("BEGIN TRANSACTION ISOLATION LEVEL $0", txn_info.level));
    RETURN_NOT_OK(CheckIsExpectedError(functor(conn), txn_info.expected_error_substr));
    return conn.RollbackTransaction();
  }

 protected:
  void SetUp() override {
    LibPqTestBase::SetUp();
    for (auto* conn : {&conn1_, &conn2_}) {
      conn->emplace(ASSERT_RESULT(Connect()));
    }
  }


  template <class... Args>
  static auto FetchAllAsSet(PGConn& conn, std::string_view table) {
    return MakeSetResult(conn.FetchRows<Args...>(Format("SELECT * FROM $0", table)));
  }

  static void ValidateRows(PGConn& conn, const std::set<int>& expected_rows) {
    const auto rows = ASSERT_RESULT(FetchAllAsSet<int>(conn, kTableName));
    ASSERT_EQ(rows, expected_rows);
  }

  static Result<std::string> ExportSnapshot(PGConn& conn) {
    return conn.FetchRow<std::string>("SELECT pg_export_snapshot()");
  }

  static Status ImportSnapshot(PGConn& conn, const std::string& snapshot_id) {
    return conn.ExecuteFormat("SET TRANSACTION SNAPSHOT '$0'", snapshot_id);
  }

  Status PrepareTable() {
    return conn1_->ExecuteFormat(
        "CREATE TABLE $0(col INT);"
        "INSERT INTO $0 VALUES (1), (2)", kTableName);
  }

  static Status RunOnUnsupportedTxnLevels(
      PGConn& conn, const UnsupportedTxnLevelFunctor& functor) {
    static constexpr std::array<UnsupportedTxnLevelInfo, 3> kUnsupportedTxnLevels = {
        {{"SERIALIZABLE", "cannot export/import snapshot in SERIALIZABLE Isolation Level"},
         {"READ COMMITTED", "cannot export/import snapshot in READ COMMITTED Isolation Level"},
         {"REPEATABLE READ DEFERRABLE",
          "cannot export/import snapshot in DEFERRABLE transaction"}}};

    for (const auto& txn_info : kUnsupportedTxnLevels) {
      LOG(INFO) << "Checking unsupported txn level '" << txn_info.level << "'";
      RETURN_NOT_OK_PREPEND(
          RunOnUnsupportedTxnLevel(txn_info, conn, functor),
          Format("Failure on '$0' txn level", txn_info.level));
    }
    return Status::OK();
  }

  std::optional<PGConn> conn1_;
  std::optional<PGConn> conn2_;
};

} // namespace

TEST_F(PgExportSnapshotTest, InvalidSnapshotId) {
  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);
  const std::regex pg_txn_snapshot_id_pattern(R"(^([a-f0-9]{32})-([a-f0-9]{32})$)");
  std::smatch match;
  std::regex_match(snapshot_id, match, pg_txn_snapshot_id_pattern);
  const auto tserver_uuid = match.str(1);
  const auto local_snapshot_id = match.str(2);

  auto make_invalid_snapshot_id_tester = [&conn = *conn2_](std::string_view expected_error_substr) {
    return [&conn, expected_error_substr](const auto& snapshot_id) {
      RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
      RETURN_NOT_OK(CheckIsExpectedError(ImportSnapshot(conn, snapshot_id), expected_error_substr));
      return conn.RollbackTransaction();
    };
  };

  {
    const auto test_invalid_snapshot_id =
        make_invalid_snapshot_id_tester("invalid snapshot identifier");

    // Snapshot Id can only contain 0-9, a-f, -
    ASSERT_OK(test_invalid_snapshot_id("snapshot_id"));
  }

  {
    auto test_invalid_snapshot_id = make_invalid_snapshot_id_tester("Could not find Snapshot");

    // Local snapshot id does not exist
    auto missing_local_snapshot_id = snapshot_id;
    missing_local_snapshot_id.back() = missing_local_snapshot_id.back() == 'a' ? 'b' : 'a';
    ASSERT_OK(test_invalid_snapshot_id(missing_local_snapshot_id));

    // Invalid Tserver UUid
    auto missing_tserver_uuid = snapshot_id;
    missing_tserver_uuid.front() = missing_tserver_uuid.front() == 'a' ? 'b' : 'a';
    ASSERT_OK(test_invalid_snapshot_id(missing_tserver_uuid));
  }

  {
    auto test_invalid_snapshot_id = make_invalid_snapshot_id_tester("Invalid Snapshot Id");

    // Invalid Uuid for local_snapshot_id
    auto invalid_local_snapshot_id = snapshot_id + 'a';
    ASSERT_OK(test_invalid_snapshot_id(invalid_local_snapshot_id));

    // Invalid Snapshot Id Structure
    ASSERT_OK(test_invalid_snapshot_id(Format("$0-abc", tserver_uuid)));

    // Invalid Snapshot Id Structure
    ASSERT_OK(test_invalid_snapshot_id(
        Format("$0-abc-$2-abc", tserver_uuid, local_snapshot_id)));
  }
}

TEST_F(PgExportSnapshotTest, ExportImportSnapshot) {
  ASSERT_OK(PrepareTable());
  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);
  auto expected_rows = ASSERT_RESULT(FetchAllAsSet<int>(*conn1_, kTableName));

  LOG(INFO) << "Sleeping for 1s";
  // In the implementation of export snapshot we only pick (and set the same in importing
  // transaction) read time and global limit which is (read time + 500 ms), so if there is write
  // in 500ms after exporting snapshot it can cause a read restart error in importing transaction.
  // Hence sleeping here to ensure no read restart errors.
  SleepFor(MonoDelta::FromSeconds(1));

  // Insert an entry using the other session after exporting snapshot, this row
  // should not be present after importing snapshot.
  ASSERT_OK(conn2_->ExecuteFormat("INSERT INTO $0 VALUES(100)", kTableName));

  ASSERT_OK(conn2_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(ImportSnapshot(*conn2_, snapshot_id));

  for ([[maybe_unused]] auto i : Range(3)) {
    // Run multiple times to make sure that read time is not overwritten even after multiple
    // queries.
    ValidateRows(*conn2_, expected_rows);
  }

  ASSERT_OK(conn2_->ExecuteFormat("INSERT INTO $0 VALUES(1000)", kTableName));
  expected_rows.insert(1000);
  // We should see our own write.
  ValidateRows(*conn2_, expected_rows);

  ASSERT_OK(conn2_->CommitTransaction());
  // New transaction should not reuse the old imported snapshot.
  expected_rows.insert(100);
  ValidateRows(*conn2_, expected_rows);
}

// This test is used for testing the situation in which read time is picked on remote tserver
// and passed back to PgClientSession using used_read_time logic.
TEST_F(PgExportSnapshotTest, ExportSnapshotAfterSelect) {
  ASSERT_OK(PrepareTable());
  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  auto expected_rows = ASSERT_RESULT(FetchAllAsSet<int>(*conn1_, kTableName));

  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);

  LOG(INFO) << "Sleeping for 1s";
  SleepFor(MonoDelta::FromSeconds(1));

  // Insert an entry using the other session after exporting snapshot, this row
  // should not be present after importing snapshot.
  ASSERT_OK(conn2_->ExecuteFormat("INSERT INTO $0 VALUES(100)", kTableName));

  ASSERT_OK(conn2_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(ImportSnapshot(*conn2_, snapshot_id));
  ValidateRows(*conn2_, expected_rows);
  ASSERT_OK(conn2_->CommitTransaction());

  // SELECT from conn1 must return the same result.
  ValidateRows(*conn1_, expected_rows);

  // SELECT from conn2 after COMMIT must include the row added after exporting.
  expected_rows.insert(100);
  ValidateRows(*conn2_, expected_rows);
}

TEST_F(PgExportSnapshotTest, ExportSnapshotAfterMultipleSelects) {
  ASSERT_OK(PrepareTable());
  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto expected_rows = ASSERT_RESULT(FetchAllAsSet<int>(*conn1_, kTableName));
  ASSERT_RESULT(FetchAllAsSet<int>(*conn1_, kTableName));

  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);

  LOG(INFO) << "Sleeping for 1s";
  SleepFor(MonoDelta::FromSeconds(1));

  // Insert an entry using the other session after exporting snapshot, this row
  // should not be present after importing snapshot.
  ASSERT_OK(conn2_->ExecuteFormat("INSERT INTO $0 VALUES(100)", kTableName));

  ASSERT_OK(conn2_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(ImportSnapshot(*conn2_, snapshot_id));
  ValidateRows(*conn2_, expected_rows);
}

TEST_F(PgExportSnapshotTest, ImportUnaffectedByExportCommit) {
  ASSERT_OK(PrepareTable());
  const auto expected_rows = ASSERT_RESULT(FetchAllAsSet<int>(*conn1_, kTableName));

  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn1_->ExecuteFormat("INSERT INTO $0 VALUES(1000)", kTableName));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);

  LOG(INFO) << "Sleeping for 1s";
  SleepFor(MonoDelta::FromSeconds(1));

  // Insert an entry using the other session after exporting snapshot, this row
  // should not be present after importing snapshot.
  ASSERT_OK(conn2_->ExecuteFormat("INSERT INTO $0 VALUES(100)", kTableName));

  ASSERT_OK(conn2_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(ImportSnapshot(*conn2_, snapshot_id));
  ValidateRows(*conn2_, expected_rows);

  ASSERT_OK(conn1_->CommitTransaction());

  // Even after conn1 commits, newly committed row should not be visible
  ValidateRows(*conn2_, expected_rows);
}

TEST_F(PgExportSnapshotTest, ImportSnapshotAfterFirstStmt) {
  ASSERT_OK(PrepareTable());
  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);

  ASSERT_OK(conn2_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_RESULT(conn2_->FetchRows<int>("SELECT * FROM t1"));
  ASSERT_NOK_STR_CONTAINS(
      ImportSnapshot(*conn2_, snapshot_id),
      "SET TRANSACTION SNAPSHOT must be called before any query");
}

TEST_F(PgExportSnapshotTest, ImportUsingOtherTserver) {
  auto conn_other_tserver = ASSERT_RESULT(ConnectToTs(*cluster_->tserver_daemons().back()));
  ASSERT_OK(PrepareTable());
  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);
  auto expected_rows = ASSERT_RESULT(FetchAllAsSet<int>(*conn1_, kTableName));

  LOG(INFO) << "Sleeping for 1s";
  SleepFor(MonoDelta::FromSeconds(1));

  // Insert an entry using the other session after exporting snapshot, this row
  // should not be present after importing snapshot.
  ASSERT_OK(conn_other_tserver.ExecuteFormat("INSERT INTO $0 VALUES(100)", kTableName));

  ASSERT_OK(conn_other_tserver.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(ImportSnapshot(conn_other_tserver, snapshot_id));
  ValidateRows(conn_other_tserver, expected_rows);

  ASSERT_OK(conn_other_tserver.CommitTransaction());
  expected_rows.insert(100);
  ValidateRows(conn_other_tserver, expected_rows);
}

TEST_F(PgExportSnapshotTest, InsertAfterFirstExport) {
  ASSERT_OK(PrepareTable());
  auto expected_rows = ASSERT_RESULT(FetchAllAsSet<int>(*conn1_, kTableName));

  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);

  LOG(INFO) << "Sleeping for 1s";
  SleepFor(MonoDelta::FromSeconds(1));

  // Insert an entry using the other session after exporting snapshot, this row
  // should not be present after importing snapshot.
  ASSERT_OK(conn2_->ExecuteFormat("INSERT INTO $0 VALUES(100)", kTableName));

  ASSERT_OK(conn1_->ExecuteFormat("INSERT INTO $0 VALUES(3)", kTableName));
  expected_rows.insert(3);
  ValidateRows(*conn1_, expected_rows);

  ASSERT_OK(conn1_->CommitTransaction());

  expected_rows.insert(100);
  ValidateRows(*conn1_, expected_rows);
}

TEST_F(PgExportSnapshotTest, InvalidExport) {
  ASSERT_OK(RunOnUnsupportedTxnLevels(
      *conn1_, [](PGConn& conn) { return ResultToStatus(ExportSnapshot(conn)); }));
}

TEST_F(PgExportSnapshotTest, InvalidImport) {
  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);

  ASSERT_OK(RunOnUnsupportedTxnLevels(
      *conn2_,
      [&snapshot_id](PGConn& conn) { return ImportSnapshot(conn, snapshot_id); }));
}

TEST_F(PgExportSnapshotTest, ImportToDifferentDatabase) {
  const auto& dbname = "db_snapshot_test";
  ASSERT_OK(conn1_->ExecuteFormat("CREATE DATABASE $0", dbname));
  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);

  auto conn_otherDB = ASSERT_RESULT(ConnectToDB(dbname));
  ASSERT_OK(conn_otherDB.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_NOK_STR_CONTAINS(
      ImportSnapshot(conn_otherDB, snapshot_id),
      "cannot import a snapshot from a different database");
}

TEST_F(PgExportSnapshotTest, ImportSnapshotWithYbReadTime) {
  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);

  ASSERT_OK(conn2_->ExecuteFormat("SET yb_read_time TO $0", GetCurrentTimeMicros()));
  ASSERT_OK(conn2_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_NOK_STR_CONTAINS(
      ImportSnapshot(*conn2_, snapshot_id),
      "Cannot set both 'transaction snapshot' and 'yb_read_time' in the same transaction.");
}

TEST_F(PgExportSnapshotTest, ImportSnapshotWithFollowerReads) {
  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);

  ASSERT_OK(conn2_->ExecuteFormat("SET yb_read_from_followers = true"));
  ASSERT_OK(conn2_->Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY"));
  ASSERT_NOK_STR_CONTAINS(
      ImportSnapshot(*conn2_, snapshot_id),
      "Cannot set both 'transaction snapshot' and 'yb_read_from_followers' in the same "
      "transaction.");
}

TEST_F(PgExportSnapshotTest, ImportSnapshotWithRelaxedReadAfterCommitVisibility) {
  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);

  ASSERT_OK(conn2_->ExecuteFormat("SET yb_read_after_commit_visibility TO relaxed"));
  ASSERT_OK(conn2_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_NOK_STR_CONTAINS(
      ImportSnapshot(*conn2_, snapshot_id),
      "Cannot set both 'transaction snapshot' and 'yb_read_after_commit_visibility' in the same "
      "transaction.");
}

TEST_F(PgExportSnapshotTest, ExportAfterImportSnapshot) {
  auto conn3 = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareTable());

  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);

  const auto expected_rows = ASSERT_RESULT(FetchAllAsSet<int>(*conn1_, kTableName));

  LOG(INFO) << "Sleeping for 1s";
  SleepFor(MonoDelta::FromSeconds(1));

  // Insert an entry using the other session after exporting snapshot, this row
  // should not be present after importing snapshot.
  ASSERT_OK(conn2_->ExecuteFormat("INSERT INTO $0 VALUES(100)", kTableName));

  ASSERT_OK(conn2_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(ImportSnapshot(*conn2_, snapshot_id));
  const auto snapshot_id2 = ASSERT_RESULT(ExportSnapshot(*conn2_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id2);
  ASSERT_OK(conn2_->ExecuteFormat("INSERT INTO $0 VALUES(1000)", kTableName));

  ASSERT_OK(conn1_->CommitTransaction());

  ASSERT_OK(conn3.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(ImportSnapshot(conn3, snapshot_id2));
  ValidateRows(conn3, expected_rows);
}

TEST_F(PgExportSnapshotTest, ImportAfterUpdate) {
  ASSERT_OK(conn1_->ExecuteFormat(
      "CREATE TABLE $0(col INT, col1 INT);"
      "INSERT INTO $0 VALUES (1, 10), (2, 20)", kTableName));

  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);
  const auto expected_rows =
      ASSERT_RESULT((FetchAllAsSet<int32_t, int32_t>(*conn1_, kTableName)));

  LOG(INFO) << "Sleeping for 1s";
  SleepFor(MonoDelta::FromSeconds(1));

  // Update an entry using the other session after exporting snapshot, this row
  // should be as before after importing snapshot.
  ASSERT_OK(conn2_->ExecuteFormat("UPDATE $0 SET col1 = 30 WHERE col = 2", kTableName));
  const auto expected_rows_after_commit =
      ASSERT_RESULT((FetchAllAsSet<int32_t, int32_t>(*conn2_, kTableName)));

  ASSERT_OK(conn2_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(ImportSnapshot(*conn2_, snapshot_id));
  auto actual_rows = ASSERT_RESULT((FetchAllAsSet<int32_t, int32_t>(*conn2_, kTableName)));
  ASSERT_EQ(actual_rows, expected_rows);

  ASSERT_OK(conn2_->CommitTransaction());
  actual_rows = ASSERT_RESULT((FetchAllAsSet<int32_t, int32_t>(*conn2_, kTableName)));
  ASSERT_EQ(actual_rows, expected_rows_after_commit);
}

TEST_F(PgExportSnapshotTest, CheckImportUncertainityWindow) {
  ASSERT_OK(PrepareTable());

  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);

  // Insert an entry using the other session after exporting snapshot, this row
  // should not be present after importing snapshot.
  ASSERT_OK(conn2_->ExecuteFormat("INSERT INTO $0 VALUES(100)", kTableName));

  // This sleep ensures the following scenario is verified:
  // - If an insert occurs within 500ms of exporting a snapshot, a read restart error should occur
  //   (i.e., the global limit is reached).
  // - This error is expected even if we wait for more than 500ms after the insert
  //   before importing the snapshot.

  LOG(INFO) << "Sleeping for 1s";
  SleepFor(MonoDelta::FromSeconds(1));

  ASSERT_OK(conn2_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(ImportSnapshot(*conn2_, snapshot_id));
  ASSERT_NOK_STR_CONTAINS(
      conn2_->ExecuteFormat("SELECT * FROM $0", kTableName), "Restart read required");
}

TEST_F(PgExportSnapshotTest, CheckImportInTxnLimit) {
  ASSERT_OK(conn1_->Execute(
    "CREATE TABLE t1(col INT PRIMARY KEY, col0 INT);"
    "CREATE TABLE t2(col INT PRIMARY KEY, col0 INT);"
    "INSERT INTO t1 VALUES (1, 1);"
    "INSERT INTO t2 VALUES (2, 2)"));

  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);

  LOG(INFO) << "Sleeping for 1s";
  SleepFor(MonoDelta::FromSeconds(1));

  ASSERT_OK(conn2_->Execute(
    "INSERT INTO t1 VALUES (100, 100);"
    "INSERT INTO t2 VALUES (200, 200)"));

  ASSERT_OK(conn2_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(ImportSnapshot(*conn2_, snapshot_id));
  ASSERT_OK(conn2_->Execute(
      "INSERT INTO t2 SELECT col + 2, col0 FROM t1 UNION ALL SELECT col + 2, col0 FROM t2"));
  const auto actual_rows = ASSERT_RESULT((FetchAllAsSet<int32_t, int32_t>(*conn2_, "t2")));
  ASSERT_EQ(actual_rows, (decltype(actual_rows){{2, 2}, {3, 1}, {4, 2}}));
}

TEST_F(PgExportSnapshotTest, ExportMultipleSnapshots) {
  ASSERT_OK(PrepareTable());
  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id1 = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id1);
  const auto snapshot_id2 = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id2);
  auto expected_rows = ASSERT_RESULT(FetchAllAsSet<int>(*conn1_, kTableName));

  LOG(INFO) << "Sleeping for 1s";
  SleepFor(MonoDelta::FromSeconds(1));

  // Insert an entry using the other session after exporting snapshot, this row
  // should not be present after importing snapshot.
  ASSERT_OK(conn2_->ExecuteFormat("INSERT INTO $0 VALUES(100)", kTableName));

  ASSERT_OK(conn2_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(ImportSnapshot(*conn2_, snapshot_id2));
  ValidateRows(*conn2_, expected_rows);
}

TEST_F(PgExportSnapshotTest, CheckCleanupAfterExportingTxnCommit) {
  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);
  ASSERT_OK(conn1_->CommitTransaction());

  ASSERT_OK(conn2_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_NOK_STR_CONTAINS(ImportSnapshot(*conn2_, snapshot_id), "Could not find Snapshot");
  ASSERT_OK(conn2_->RollbackTransaction());
}

TEST_F(PgExportSnapshotTest, CheckCleanupAfterExportingTxnAbort) {
  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);
  ASSERT_OK(conn1_->RollbackTransaction());

  ASSERT_OK(conn2_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_NOK_STR_CONTAINS(ImportSnapshot(*conn2_, snapshot_id), "Could not find Snapshot");
  ASSERT_OK(conn2_->RollbackTransaction());
}

TEST_F(PgExportSnapshotTest, CheckCleanupAfterPgBackendReset) {
  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);
  conn1_->Reset();

  ASSERT_OK(conn2_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_NOK_STR_CONTAINS(ImportSnapshot(*conn2_, snapshot_id), "Could not find Snapshot");
  ASSERT_OK(conn2_->RollbackTransaction());
}

TEST_F(PgExportSnapshotTest, CheckCleanupAfterExportingTserverRestart) {
  ASSERT_OK(conn1_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto snapshot_id = ASSERT_RESULT(ExportSnapshot(*conn1_));
  LOG(INFO) << Format("Snapshot Exported: $0", snapshot_id);
  cluster_->tablet_server(0)->Shutdown();
  ASSERT_OK(cluster_->tablet_server(0)->Restart());
  ASSERT_OK(
      cluster_->WaitForTabletsRunning(cluster_->tablet_server(0), MonoDelta::FromSeconds(60)));

  auto connSameTserver_afterRestart = ASSERT_RESULT(Connect());
  auto connOtherTserver_afterRestart =
      ASSERT_RESULT(ConnectToTs(*cluster_->tserver_daemons().back()));

  ASSERT_OK(connSameTserver_afterRestart.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_NOK_STR_CONTAINS(
      ImportSnapshot(connSameTserver_afterRestart, snapshot_id), "Could not find Snapshot");
  ASSERT_OK(connSameTserver_afterRestart.RollbackTransaction());

  ASSERT_OK(connOtherTserver_afterRestart.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_NOK_STR_CONTAINS(
      ImportSnapshot(connOtherTserver_afterRestart, snapshot_id), "Could not find Snapshot");
  ASSERT_OK(connOtherTserver_afterRestart.RollbackTransaction());
}

} // namespace yb::pgwrapper
