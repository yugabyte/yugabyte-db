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

#include "yb/client/table.h"
#include "yb/gutil/strings/join.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/stol_utils.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::chrono_literals;

namespace yb {
namespace pgwrapper {

class PgIndexBackfillTest : public LibPqTestBase {
 public:
  PgIndexBackfillTest() {
    more_master_flags.push_back("--ysql_disable_index_backfill=false");
    more_tserver_flags.push_back("--ysql_disable_index_backfill=false");
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.insert(
        std::end(options->extra_master_flags),
        std::begin(more_master_flags),
        std::end(more_master_flags));
    options->extra_tserver_flags.insert(
        std::end(options->extra_tserver_flags),
        std::begin(more_tserver_flags),
        std::end(more_tserver_flags));
  }

 protected:
  std::vector<std::string> more_master_flags;
  std::vector<std::string> more_tserver_flags;
};

namespace {

// A copy of the same function in pg_libpq-test.cc.  Eventually, issue #6868 should provide a way to
// do this easily for both this file and that.
Result<string> GetTableIdByTableName(
    client::YBClient* client, const string& namespace_name, const string& table_name) {
  const auto tables = VERIFY_RESULT(client->ListTables());
  for (const auto& t : tables) {
    if (t.namespace_name() == namespace_name && t.table_name() == table_name) {
      return t.table_id();
    }
  }
  return STATUS(NotFound, "The table does not exist");
}

Result<bool> IsAtTargetIndexStateFlags(
    PGConn* conn,
    const std::string& index_name,
    const std::map<std::string, bool>& target_index_state_flags) {
  constexpr int kNumIndexStateFlags = 3;

  // Check target_index_state_flags.
  SCHECK_EQ(
      target_index_state_flags.size(),
      kNumIndexStateFlags,
      InvalidArgument,
      Format("$0 should have $1 state flags", "target_index_state_flags", kNumIndexStateFlags));
  SCHECK(
      target_index_state_flags.find("indislive") != target_index_state_flags.end(),
      InvalidArgument,
      Format("$0 should have $1 state flag", "target_index_state_flags", "indislive"));
  SCHECK(
      target_index_state_flags.find("indisready") != target_index_state_flags.end(),
      InvalidArgument,
      Format("$0 should have $1 state flag", "target_index_state_flags", "indisready"));
  SCHECK(
      target_index_state_flags.find("indisvalid") != target_index_state_flags.end(),
      InvalidArgument,
      Format("$0 should have $1 state flag", "target_index_state_flags", "indisvalid"));

  // Get actual index state flags.
  auto res = VERIFY_RESULT(conn->FetchFormat(
      "SELECT $0"
      " FROM pg_class INNER JOIN pg_index ON pg_class.oid = pg_index.indexrelid"
      " WHERE pg_class.relname = '$1'",
      JoinKeysIterator(target_index_state_flags.begin(), target_index_state_flags.end(), ", "),
      index_name));
  if (PQntuples(res.get()) == 0) {
    LOG(WARNING) << index_name << " not found in system tables";
    return false;
  }

  // Check index state flags.
  for (const auto& target_index_state_flag : target_index_state_flags) {
    bool actual_value = VERIFY_RESULT(GetBool(
        res.get(),
        0,
        std::distance(
            target_index_state_flags.begin(),
            target_index_state_flags.find(target_index_state_flag.first))));
    bool expected_value = target_index_state_flag.second;
    if (actual_value < expected_value) {
      LOG(INFO) << index_name
                << " not yet at target index state flag "
                << target_index_state_flag.first;
      return false;
    } else if (actual_value > expected_value) {
      return STATUS(RuntimeError,
                    Format("$0 exceeded target index state flag $1",
                           index_name,
                           target_index_state_flag.first));
    }
  }
  return true;
}

CHECKED_STATUS WaitForBackfillStage(
    PGConn* conn,
    const std::string& index_name,
    const MonoDelta& index_state_flags_update_delay) {
  LOG(INFO) << "Waiting for pg_index indislive to be true";
  RETURN_NOT_OK(WaitFor(
      std::bind(IsAtTargetIndexStateFlags, conn, index_name, std::map<std::string, bool>{
          {"indislive", true},
          {"indisready", false},
          {"indisvalid", false},
        }),
      12s,
      "Wait for pg_index indislive=true",
      MonoDelta::FromMilliseconds(test_util::kDefaultInitialWaitMs),
      test_util::kDefaultWaitDelayMultiplier,
      100ms /* max_delay */));

  LOG(INFO) << "Waiting for pg_index indisready to be true";
  RETURN_NOT_OK(WaitFor(
      std::bind(IsAtTargetIndexStateFlags, conn, index_name, std::map<std::string, bool>{
          {"indislive", true},
          {"indisready", true},
          {"indisvalid", false},
        }),
      index_state_flags_update_delay + 5s,
      "Wait for pg_index indisready=true",
      MonoDelta::FromMilliseconds(test_util::kDefaultInitialWaitMs),
      test_util::kDefaultWaitDelayMultiplier,
      100ms /* max_delay */));

  LOG(INFO) << "Waiting till (approx) the end of the delay after committing indisready true";
  SleepFor(index_state_flags_update_delay);

  return Status::OK();
}

} // namespace

// Make sure that backfill works.
TEST_F(PgIndexBackfillTest, YB_DISABLE_TEST_IN_TSAN(Simple)) {
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (c char, i int, p point)", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ('a', 0, '(1, 2)')", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ('y', -5, '(0, -2)')", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ('b', 100, '(868, 9843)')", kTableName));
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0 (c ASC)", kTableName));

  // Index scan to verify contents of index table.
  const std::string query = Format("SELECT * FROM $0 ORDER BY c", kTableName);
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
  auto res = ASSERT_RESULT(conn.Fetch(query));
  ASSERT_EQ(PQntuples(res.get()), 3);
  ASSERT_EQ(PQnfields(res.get()), 3);
  std::array<int, 3> values = {
    ASSERT_RESULT(GetInt32(res.get(), 0, 1)),
    ASSERT_RESULT(GetInt32(res.get(), 1, 1)),
    ASSERT_RESULT(GetInt32(res.get(), 2, 1)),
  };
  ASSERT_EQ(values[0], 0);
  ASSERT_EQ(values[1], 100);
  ASSERT_EQ(values[2], -5);
}

// Make sure that partial indexes work for index backfill.
TEST_F(PgIndexBackfillTest, YB_DISABLE_TEST_IN_TSAN(Partial)) {
  constexpr int kNumRows = 7;
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j int)", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(-1, -$1, -1))",
      kTableName,
      kNumRows));
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0 (i ASC) WHERE j > -5", kTableName));

  // Index scan to verify contents of index table.
  {
    const std::string query = Format("SELECT j FROM $0 WHERE j > -3 ORDER BY i", kTableName);
    ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
    auto res = ASSERT_RESULT(conn.Fetch(query));
    ASSERT_EQ(PQntuples(res.get()), 2);
    ASSERT_EQ(PQnfields(res.get()), 1);
    std::array<int, 2> values = {
      ASSERT_RESULT(GetInt32(res.get(), 0, 0)),
      ASSERT_RESULT(GetInt32(res.get(), 1, 0)),
    };
    ASSERT_EQ(values[0], -1);
    ASSERT_EQ(values[1], -2);
  }
  {
    const std::string query = Format(
        "SELECT i FROM $0 WHERE j > -5 ORDER BY i DESC LIMIT 2",
        kTableName);
    ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
    auto res = ASSERT_RESULT(conn.Fetch(query));
    ASSERT_EQ(PQntuples(res.get()), 2);
    ASSERT_EQ(PQnfields(res.get()), 1);
    std::array<int, 2> values = {
      ASSERT_RESULT(GetInt32(res.get(), 0, 0)),
      ASSERT_RESULT(GetInt32(res.get(), 1, 0)),
    };
    ASSERT_EQ(values[0], 4);
    ASSERT_EQ(values[1], 3);
  }
}

// Make sure that expression indexes work for index backfill.
TEST_F(PgIndexBackfillTest, YB_DISABLE_TEST_IN_TSAN(Expression)) {
  constexpr int kNumRows = 9;
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j int)", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(11, 10 + $1))",
      kTableName,
      kNumRows));
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0 ((j % i))", kTableName));

  // Index scan to verify contents of index table.
  const std::string query = Format(
      "SELECT j, i, j % i as mod FROM $0 WHERE j % i = 2 ORDER BY i",
      kTableName);
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
  auto res = ASSERT_RESULT(conn.Fetch(query));
  ASSERT_EQ(PQntuples(res.get()), 2);
  ASSERT_EQ(PQnfields(res.get()), 3);
  std::array<std::array<int, 3>, 2> values = {{
    {
      ASSERT_RESULT(GetInt32(res.get(), 0, 0)),
      ASSERT_RESULT(GetInt32(res.get(), 0, 1)),
      ASSERT_RESULT(GetInt32(res.get(), 0, 2)),
    },
    {
      ASSERT_RESULT(GetInt32(res.get(), 1, 0)),
      ASSERT_RESULT(GetInt32(res.get(), 1, 1)),
      ASSERT_RESULT(GetInt32(res.get(), 1, 2)),
    },
  }};
  ASSERT_EQ(values[0][0], 14);
  ASSERT_EQ(values[0][1], 4);
  ASSERT_EQ(values[0][2], 2);
  ASSERT_EQ(values[1][0], 18);
  ASSERT_EQ(values[1][1], 8);
  ASSERT_EQ(values[1][2], 2);
}

// Make sure that unique indexes work when index backfill is enabled.
TEST_F(PgIndexBackfillTest, YB_DISABLE_TEST_IN_TSAN(Unique)) {
  constexpr int kNumRows = 3;
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j int)", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(11, 10 + $1))",
      kTableName,
      kNumRows));
  // Add row that would make j not unique.
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (99, 11)",
      kTableName,
      kNumRows));

  // Create unique index without failure.
  ASSERT_OK(conn.ExecuteFormat("CREATE UNIQUE INDEX ON $0 (i ASC)", kTableName));
  // Index scan to verify contents of index table.
  const std::string query = Format(
      "SELECT * FROM $0 ORDER BY i",
      kTableName);
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
  auto res = ASSERT_RESULT(conn.Fetch(query));
  ASSERT_EQ(PQntuples(res.get()), 4);
  ASSERT_EQ(PQnfields(res.get()), 2);

  // Create unique index with failure.
  Status status = conn.ExecuteFormat("CREATE UNIQUE INDEX ON $0 (j ASC)", kTableName);
  ASSERT_NOK(status);
  auto msg = status.message().ToBuffer();
  ASSERT_TRUE(msg.find("duplicate key value violates unique constraint") != std::string::npos)
      << status;
}

// Make sure that indexes created in postgres nested DDL work and skip backfill (optimization).
TEST_F(PgIndexBackfillTest, YB_DISABLE_TEST_IN_TSAN(NestedDdl)) {
  constexpr int kNumRows = 3;
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j int, UNIQUE (j))", kTableName));

  // Make sure that the index create was not multi-stage.
  std::string table_id =
      ASSERT_RESULT(GetTableIdByTableName(client.get(), kNamespaceName, kTableName));
  std::shared_ptr<client::YBTableInfo> table_info = std::make_shared<client::YBTableInfo>();
  Synchronizer sync;
  ASSERT_OK(client->GetTableSchemaById(table_id, table_info, sync.AsStatusCallback()));
  ASSERT_OK(sync.Wait());
  ASSERT_EQ(table_info->schema.version(), 1);

  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(11, 10 + $1))",
      kTableName,
      kNumRows));

  // Add row that violates unique constraint on j.
  Status status = conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (99, 11)",
      kTableName,
      kNumRows);
  ASSERT_NOK(status);
  auto msg = status.message().ToBuffer();
  ASSERT_TRUE(msg.find("duplicate key value") != std::string::npos) << status;
}

// Make sure that drop index works when index backfill is enabled (skips online schema migration for
// now)
TEST_F(PgIndexBackfillTest, YB_DISABLE_TEST_IN_TSAN(Drop)) {
  constexpr int kNumRows = 5;
  const std::string kNamespaceName = "yugabyte";
  const std::string kIndexName = "i";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j int)", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(11, 10 + $1))",
      kTableName,
      kNumRows));

  // Create index.
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0 ON $1 (i ASC)", kIndexName, kTableName));

  // Drop index.
  ASSERT_OK(conn.ExecuteFormat("DROP INDEX $0", kIndexName));

  // Ensure index is not used for scan.
  const std::string query = Format(
      "SELECT * FROM $0 ORDER BY i",
      kTableName);
  ASSERT_FALSE(ASSERT_RESULT(conn.HasIndexScan(query)));
}

// Make sure deletes to nonexistent rows look like noops to clients.  This may seem too obvious to
// necessitate a test, but logic for backfill is special in that it wants nonexistent index deletes
// to be applied for the backfill process to use them.  This test guards against that logic being
// implemented incorrectly.
TEST_F(PgIndexBackfillTest, YB_DISABLE_TEST_IN_TSAN(NonexistentDelete)) {
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int PRIMARY KEY)", kTableName));

  // Delete to nonexistent row should return no rows.
  auto res = ASSERT_RESULT(conn.FetchFormat("DELETE FROM $0 WHERE i = 1 RETURNING i", kTableName));
  ASSERT_EQ(PQntuples(res.get()), 0);
  ASSERT_EQ(PQnfields(res.get()), 1);
}

// Make sure that index backfill on large tables backfills all data.
TEST_F(PgIndexBackfillTest, YB_DISABLE_TEST_IN_TSAN(Large)) {
  constexpr int kNumRows = 10000;
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));

  // Insert bunch of rows.
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1))",
      kTableName,
      kNumRows));

  // Create index.
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0 (i ASC)", kTableName));

  // All rows should be in the index.
  const std::string query = Format(
      "SELECT COUNT(*) FROM $0 WHERE i > 0",
      kTableName);
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
  auto res = ASSERT_RESULT(conn.Fetch(query));
  ASSERT_EQ(PQntuples(res.get()), 1);
  ASSERT_EQ(PQnfields(res.get()), 1);
  int actual_num_rows = ASSERT_RESULT(GetInt64(res.get(), 0, 0));
  ASSERT_EQ(actual_num_rows, kNumRows);
}

// Make sure that CREATE INDEX NONCONCURRENTLY doesn't use backfill.
TEST_F(PgIndexBackfillTest, YB_DISABLE_TEST_IN_TSAN(Nonconcurrent)) {
  const std::string kIndexName = "x";
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kNamespaceName, kTableName));

  // To determine whether the index uses backfill or not, look at the table schema version before
  // and after.  We can't look at the DocDB index permissions because
  // - if backfill is skipped, index_permissions is unset, and the default value is
  //   INDEX_PERM_READ_WRITE_AND_DELETE
  // - if backfill is used, index_permissions is INDEX_PERM_READ_WRITE_AND_DELETE
  // - GetTableSchemaById offers no way to see whether the default value for index permissions is
  //   set
  std::shared_ptr<client::YBTableInfo> info = std::make_shared<client::YBTableInfo>();
  {
    Synchronizer sync;
    ASSERT_OK(client->GetTableSchemaById(table_id, info, sync.AsStatusCallback()));
    ASSERT_OK(sync.Wait());
  }
  ASSERT_EQ(info->schema.version(), 0);

  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX NONCONCURRENTLY $0 ON $1 (i)",
                               kIndexName,
                               kTableName));

  // If the index used backfill, it would have incremented the table schema version by two or three:
  // - add index info with INDEX_PERM_DELETE_ONLY
  // - update to INDEX_PERM_DO_BACKFILL (as part of issue #6218)
  // - update to INDEX_PERM_READ_WRITE_AND_DELETE
  // If the index did not use backfill, it would have incremented the table schema version by one:
  // - add index info with no DocDB permission (default INDEX_PERM_READ_WRITE_AND_DELETE)
  // Expect that it did not use backfill.
  {
    Synchronizer sync;
    ASSERT_OK(client->GetTableSchemaById(table_id, info, sync.AsStatusCallback()));
    ASSERT_OK(sync.Wait());
  }
  ASSERT_EQ(info->schema.version(), 1);
}

// Test simultaneous CREATE INDEX.
// TODO(jason): update this when closing issue #6269.
TEST_F(PgIndexBackfillTest, YB_DISABLE_TEST_IN_TSAN(CreateIndexSimultaneously)) {
  constexpr int kNumRows = 10;
  constexpr int kNumThreads = 5;
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";
  const std::string query = Format("SELECT * FROM $0 WHERE i = $1", kTableName, 7);

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1))",
      kTableName,
      kNumRows));

  std::vector<std::thread> threads;
  std::vector<Status> statuses;
  LOG(INFO) << "Starting threads";
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&] {
      auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
      statuses.emplace_back(conn.ExecuteFormat("CREATE INDEX ON $0 (i)", kTableName));
    });
  }
  LOG(INFO) << "Joining threads";
  for (auto& thread : threads) {
    thread.join();
  }

  LOG(INFO) << "Inspecting statuses";
  int num_ok = 0;
  ASSERT_EQ(statuses.size(), kNumThreads);
  for (auto& status : statuses) {
    LOG(INFO) << "status: " << status;
    if (status.ok()) {
      num_ok++;
    } else {
      ASSERT_TRUE(status.IsNetworkError()) << status;
      const std::string msg = status.message().ToBuffer();
      ASSERT_TRUE(
          (msg.find("Catalog Version Mismatch") != std::string::npos)
          || (msg.find("Conflicts with higher priority transaction") != std::string::npos)
          || (msg.find("Transaction aborted") != std::string::npos)
          || (msg.find("Unknown transaction, could be recently aborted") != std::string::npos)
          || (msg.find("Transaction metadata missing") != std::string::npos))
        << status.message().ToBuffer();
    }
  }
  ASSERT_EQ(num_ok, 1);

  LOG(INFO) << "Checking postgres schema";
  {
    // Check number of indexes.
    auto res = ASSERT_RESULT(conn.FetchFormat(
        "SELECT indexname FROM pg_indexes WHERE tablename = '$0'", kTableName));
    ASSERT_EQ(PQntuples(res.get()), 1);
    const std::string actual = ASSERT_RESULT(GetString(res.get(), 0, 0));
    const std::string expected = Format("$0_i_idx", kTableName);
    ASSERT_EQ(actual, expected);

    // Check whether index is public using index scan.
    ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
  }
  LOG(INFO) << "Checking DocDB schema";
  std::vector<TableId> orphaned_docdb_index_ids;
  {
    auto client = ASSERT_RESULT(cluster_->CreateClient());
    std::string table_id =
        ASSERT_RESULT(GetTableIdByTableName(client.get(), kNamespaceName, kTableName));
    std::shared_ptr<client::YBTableInfo> table_info = std::make_shared<client::YBTableInfo>();
    Synchronizer sync;
    ASSERT_OK(client->GetTableSchemaById(table_id, table_info, sync.AsStatusCallback()));
    ASSERT_OK(sync.Wait());

    // Check schema version.
    //   kNumThreads (INDEX_PERM_WRITE_AND_DELETE)
    // + 1 (INDEX_PERM_READ_WRITE_AND_DELETE)
    // = kNumThreads + 1
    // TODO(jason): change this when closing #6218 because DO_BACKFILL permission will add another
    // schema version.
    ASSERT_EQ(table_info->schema.version(), kNumThreads + 1);

    // Check number of indexes.
    ASSERT_EQ(table_info->index_map.size(), kNumThreads);

    // Check index permissions.  Also collect orphaned DocDB indexes.
    int num_rwd = 0;
    for (auto& pair : table_info->index_map) {
      VLOG(1) << "table id: " << pair.first;
      IndexPermissions perm = pair.second.index_permissions();
      if (perm == IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE) {
        num_rwd++;
      } else {
        ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_WRITE_AND_DELETE);
        orphaned_docdb_index_ids.emplace_back(pair.first);
      }
    }
    ASSERT_EQ(num_rwd, 1);
  }

  LOG(INFO) << "Removing orphaned DocDB indexes";
  {
    auto client = ASSERT_RESULT(cluster_->CreateClient());
    for (TableId& index_id : orphaned_docdb_index_ids) {
      client::YBTableName indexed_table_name;
      ASSERT_OK(client->DeleteIndexTable(index_id, &indexed_table_name));
    }
  }

  LOG(INFO) << "Checking if index still works";
  {
    ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
    auto res = ASSERT_RESULT(conn.Fetch(query));
    ASSERT_EQ(PQntuples(res.get()), 1);
    auto value = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
    ASSERT_EQ(value, 7);
  }
}

// Override the index backfill test to do alter slowly.
class PgIndexBackfillAlterSlowly : public PgIndexBackfillTest {
 public:
  PgIndexBackfillAlterSlowly() {
    more_tserver_flags.push_back("--TEST_alter_schema_delay_ms=10000");
  }
};

// Override the index backfill test to have different HBA config:
// 1. if any user tries to access the authdb database, enforce md5 auth
// 2. if the postgres user tries to access the yugabyte database, allow it
// 3. if the yugabyte user tries to access the yugabyte database, allow it
// 4. otherwise, disallow it
class PgIndexBackfillAuth : public PgIndexBackfillTest {
 public:
  PgIndexBackfillAuth() {
    more_tserver_flags.push_back(
        Format(
          "--ysql_hba_conf="
          "host $0 all all md5,"
          "host yugabyte postgres all trust,"
          "host yugabyte yugabyte all trust",
          kAuthDbName));
  }

  const std::string kAuthDbName = "authdb";
};

// Test backfill on clusters where the yugabyte role has authentication enabled.
TEST_F_EX(PgIndexBackfillTest,
          YB_DISABLE_TEST_IN_TSAN(Auth),
          PgIndexBackfillAuth) {
  const std::string& kTableName = "t";

  LOG(INFO) << "create " << this->kAuthDbName << " database";
  {
    auto conn = ASSERT_RESULT(ConnectToDB("yugabyte"));
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", this->kAuthDbName));
  }

  LOG(INFO) << "backfill table on " << this->kAuthDbName << " database";
  {
    const std::string& host = pg_ts->bind_host();
    const uint16_t port = pg_ts->pgsql_rpc_port();

    auto conn = ASSERT_RESULT(ConnectUsingString(Format(
        "user=$0 password=$1 host=$2 port=$3 dbname=$4",
        "yugabyte",
        "yugabyte",
        host,
        port,
        this->kAuthDbName)));
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0 (i)", kTableName));
  }
}

// Test whether IsCreateTableDone works when creating an index with backfill enabled.  See issue
// #6234.
TEST_F_EX(PgIndexBackfillTest,
          YB_DISABLE_TEST_IN_TSAN(IsCreateTableDone),
          PgIndexBackfillAlterSlowly) {
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0 (i)", kTableName));
}

// Override the index backfill test to disable transparent retries on cache version mismatch.
class PgIndexBackfillNoRetry : public PgIndexBackfillTest {
 public:
  PgIndexBackfillNoRetry() {
    more_tserver_flags.push_back("--TEST_ysql_disable_transparent_cache_refresh_retry=true");
  }
};

TEST_F_EX(PgIndexBackfillTest,
          YB_DISABLE_TEST_IN_TSAN(DropNoRetry),
          PgIndexBackfillNoRetry) {
  constexpr int kNumRows = 5;
  const std::string kNamespaceName = "yugabyte";
  const std::string kIndexName = "i";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j int)", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(11, 10 + $1))",
      kTableName,
      kNumRows));

  // Create index.
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0 ON $1 (i ASC)", kIndexName, kTableName));

  // Update the table cache entry for the indexed table.
  ASSERT_OK(conn.FetchFormat("SELECT * FROM $0", kTableName));

  // Drop index.
  ASSERT_OK(conn.ExecuteFormat("DROP INDEX $0", kIndexName));

  // Ensure that there is no schema version mismatch for the indexed table.  This is because the
  // above `DROP INDEX` should have invalidated the corresponding table cache entry.  (There also
  // should be no catalog version mismatch because it is updated for the same session after DDL.)
  ASSERT_OK(conn.FetchFormat("SELECT * FROM $0", kTableName));
}

// Override the index backfill test to have delays for testing snapshot too old.
class PgIndexBackfillSnapshotTooOld : public PgIndexBackfillTest {
 public:
  PgIndexBackfillSnapshotTooOld() {
    more_tserver_flags.push_back("--TEST_slowdown_backfill_by_ms=10000");
    more_tserver_flags.push_back("--TEST_ysql_index_state_flags_update_delay_ms=0");
    more_tserver_flags.push_back("--timestamp_history_retention_interval_sec=3");
  }
};

// Make sure that index backfill doesn't care about snapshot too old.  Force a situation where the
// indexed table scan for backfill would occur after the committed history cutoff.  A compaction is
// needed to update this committed history cutoff, and the retention period needs to be low enough
// so that the cutoff is ahead of backfill's safe read time.  See issue #6333.
TEST_F_EX(PgIndexBackfillTest,
          YB_DISABLE_TEST_IN_TSAN(SnapshotTooOld),
          PgIndexBackfillSnapshotTooOld) {
  const std::string kIndexName = "i";
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";
  constexpr int kTimeoutSec = 3;

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  // (Make it one tablet for simplicity.)
  LOG(INFO) << "Create table...";
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (c char) SPLIT INTO 1 TABLETS", kTableName));

  LOG(INFO) << "Get table id for indexed table...";
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kNamespaceName, kTableName));

  // Insert something so that reading it would trigger snapshot too old.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ('s')", kTableName));

  std::thread create_index_thread([&] {
    LOG(INFO) << "Create index...";
    Status s = conn.ExecuteFormat("CREATE INDEX $0 ON $1 (c)", kIndexName, kTableName);
    if (!s.ok()) {
      // We are doomed to fail the test.  Before that, let's see if it turns out to be "snapshot too
      // old" or some other unexpected error.
      ASSERT_TRUE(s.IsNetworkError()) << "got unexpected error: " << s;
      ASSERT_TRUE(s.message().ToBuffer().find("Snapshot too old") != std::string::npos)
          << "got unexpected error: " << s;
      // It is "snapshot too old".  Fail now.
      FAIL() << "got snapshot too old: " << s;
    }
  });

  // Sleep until we are in the interval
  //   (read_time + history_retention_interval, read_time + slowdown_backfill)
  // = (read_time + 3s, read_time + 10s)
  // Choose read_time + 5s.
  LOG(INFO) << "Sleep...";
  SleepFor(1s); // approximate setup time before getting to the backfill stage
  SleepFor(5s);

  LOG(INFO) << "Flush and compact indexed table...";
  ASSERT_OK(client->FlushTables(
      {table_id},
      false /* add_indexes */,
      kTimeoutSec,
      false /* is_compaction */));
  ASSERT_OK(client->FlushTables(
      {table_id},
      false /* add_indexes */,
      kTimeoutSec,
      true /* is_compaction */));

  LOG(INFO) << "Waiting for create index thread to finish...";
  create_index_thread.join();
}

// Override the index backfill test to have slower backfill-related operations
class PgIndexBackfillSlow : public PgIndexBackfillTest {
 public:
  PgIndexBackfillSlow() {
    more_master_flags.push_back("--TEST_slowdown_backfill_alter_table_rpcs_ms=7000");
    more_tserver_flags.push_back("--TEST_ysql_index_state_flags_update_delay_ms=7000");
    more_tserver_flags.push_back("--TEST_slowdown_backfill_by_ms=7000");
  }
  // TODO(jason): add const MonoDelta vars for times, and update all redundant code below.
};

// Make sure that read time (and write time) for backfill works.  Simulate this situation:
//   Session A                                    Session B
//   --------------------------                   ---------------------------------
//   CREATE INDEX
//   - indislive
//   - indisready
//   - backfill
//     - get safe time for read
//                                                UPDATE a row of the indexed table
//     - do the actual backfill
//   - indisvalid
// The backfill should use the values before update when writing to the index.  The update should
// write and delete to the index because of permissions.  Since backfill writes with an ancient
// timestamp, the update should appear to have happened after the backfill.
TEST_F_EX(PgIndexBackfillTest,
          YB_DISABLE_TEST_IN_TSAN(ReadTime),
          PgIndexBackfillSlow) {
  const MonoDelta& kIndexStateFlagsUpdateDelay = MonoDelta::FromMilliseconds(
      ASSERT_RESULT(CheckedStoi(ASSERT_RESULT(
        cluster_->tserver_daemons()[0]->GetFlag("TEST_ysql_index_state_flags_update_delay_ms")))));
  const MonoDelta& kSlowDownBackfillDelay = MonoDelta::FromMilliseconds(
      ASSERT_RESULT(CheckedStoi(ASSERT_RESULT(
        cluster_->tserver_daemons()[0]->GetFlag("TEST_slowdown_backfill_by_ms")))));
  const std::map<std::string, bool> index_state_flags{
    {"indislive", true},
    {"indisready", true},
    {"indisvalid", false},
  };
  const std::string kIndexName = "rn_idx";
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "rn";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j int, PRIMARY KEY (i ASC))", kTableName));
  for (auto pair = std::make_pair(0, 10); pair.first < 6; ++pair.first, ++pair.second) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1, $2)",
                                 kTableName,
                                 pair.first,
                                 pair.second));
  }

  std::vector<std::thread> threads;
  threads.emplace_back([&] {
    auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0 ON $1 (j ASC)", kIndexName, kTableName));
    {
      // Index scan to verify contents of index table.
      const std::string query = Format("SELECT * FROM $0 WHERE j = 113", kTableName);
      ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
      auto res = ASSERT_RESULT(conn.Fetch(query));
      auto lines = PQntuples(res.get());
      ASSERT_EQ(1, lines);
      auto columns = PQnfields(res.get());
      ASSERT_EQ(2, columns);
      auto key = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
      ASSERT_EQ(key, 3);
      // Make sure that the update is visible.
      auto value = ASSERT_RESULT(GetInt32(res.get(), 0, 1));
      ASSERT_EQ(value, 113);
    }
  });
  threads.emplace_back([&] {
    ASSERT_OK(WaitForBackfillStage(&conn, kIndexName, kIndexStateFlagsUpdateDelay));

    // Give the backfill stage enough time to get a read time.
    // TODO(jason): come up with some way to wait until the read time is chosen rather than relying
    // on a brittle sleep.
    LOG(INFO) << "Waiting out half the delay of executing backfill so that we're hopefully after "
              << "getting the safe read time and before executing backfill";
    SleepFor(kSlowDownBackfillDelay / 2);

    {
      auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
      LOG(INFO) << "Updating row";
      ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET j = j + 100 WHERE i = 3", kTableName));
      LOG(INFO) << "Done updating row";
    }

    // It should still be in the backfill stage, hopefully before the actual backfill started.
    {
      auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
      ASSERT_TRUE(ASSERT_RESULT(IsAtTargetIndexStateFlags(&conn, kIndexName, index_state_flags)));
    }
  });

  for (auto& thread : threads) {
    thread.join();
  }
}

// Make sure that updates at each stage of multi-stage CREATE INDEX work.  Simulate this situation:
//   Session A                                    Session B
//   --------------------------                   ---------------------------------
//   CREATE INDEX
//   - indislive
//                                                UPDATE a row of the indexed table
//   - indisready
//                                                UPDATE a row of the indexed table
//   - indisvalid
//                                                UPDATE a row of the indexed table
// Updates should succeed and get written to the index.
TEST_F_EX(PgIndexBackfillTest,
          YB_DISABLE_TEST_IN_TSAN(Permissions),
          PgIndexBackfillSlow) {
  const auto kIndexStateFlagsWaitTime = 10s;
  const auto kThreadWaitTime = 60s;
  const std::array<std::pair<std::map<std::string, bool>, int>, 3> index_state_flags_key_pairs = {
    std::make_pair(std::map<std::string, bool>{
        {"indislive", true},
        {"indisready", false},
        {"indisvalid", false},
      }, 2),
    std::make_pair(std::map<std::string, bool>{
        {"indislive", true},
        {"indisready", true},
        {"indisvalid", false},
      }, 3),
    std::make_pair(std::map<std::string, bool>{
        {"indislive", true},
        {"indisready", true},
        {"indisvalid", true},
      }, 4),
  };
  const std::string kIndexName = "rn_idx";
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "rn";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j int, PRIMARY KEY (i ASC))", kTableName));
  for (auto pair = std::make_pair(0, 10); pair.first < 6; ++pair.first, ++pair.second) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1, $2)",
                                 kTableName,
                                 pair.first,
                                 pair.second));
  }

  std::atomic<int> updates(0);
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([&] {
    LOG(INFO) << "Begin create thread";
    auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0 ON $1 (j ASC)", kIndexName, kTableName));
  });
  thread_holder.AddThreadFunctor([&] {
    LOG(INFO) << "Begin update thread";
    auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
    for (auto pair : index_state_flags_key_pairs) {
      std::map<std::string, bool>& index_state_flags = pair.first;
      int key = pair.second;

      ASSERT_OK(WaitFor(
          std::bind(IsAtTargetIndexStateFlags, &conn, kIndexName, index_state_flags),
          kIndexStateFlagsWaitTime * (index_state_flags["indisvalid"] ? 6 : 1),
          Format("Wait for index state flags to hit target: $0", index_state_flags)));
      LOG(INFO) << "running UPDATE on i = " << key;
      ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET j = j + 100 WHERE i = $1", kTableName, key));
      LOG(INFO) << "done running UPDATE on i = " << key;

      // Make sure permission didn't change yet.
      ASSERT_TRUE(ASSERT_RESULT(IsAtTargetIndexStateFlags(&conn, kIndexName, index_state_flags)));
      updates++;
    }
  });

  thread_holder.WaitAndStop(kThreadWaitTime);

  ASSERT_EQ(updates.load(std::memory_order_acquire), index_state_flags_key_pairs.size());

  for (auto pair : index_state_flags_key_pairs) {
    int key = pair.second;

    // Verify contents of index table.
    const std::string query = Format(
        "WITH j_idx AS (SELECT * FROM $0 ORDER BY j) SELECT j FROM j_idx WHERE i = $1",
        kTableName,
        key);
    ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
    auto res = ASSERT_RESULT(conn.Fetch(query));
    int lines = PQntuples(res.get());
    ASSERT_EQ(1, lines);
    int columns = PQnfields(res.get());
    ASSERT_EQ(1, columns);
    // Make sure that the update is visible.
    int value = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
    ASSERT_EQ(value, key + 110);
  }
}

// Make sure that writes during CREATE UNIQUE INDEX don't cause unique duplicate row errors to be
// thrown.  Simulate this situation:
//   Session A                                    Session B
//   --------------------------                   ---------------------------------
//                                                INSERT a row to the indexed table
//   CREATE UNIQUE INDEX
//                                                INSERT a row to the indexed table
//   - indislive
//                                                INSERT a row to the indexed table
//   - indisready
//                                                INSERT a row to the indexed table
//   - backfill
//                                                INSERT a row to the indexed table
//   - indisvalid
//                                                INSERT a row to the indexed table
// Particularly pay attention to the insert between indisready and backfill.  The insert
// should cause a write to go to the index.  Backfill should choose a read time after this write, so
// it should try to backfill this same row.  Rather than conflicting when we see the row already
// exists in the index during backfill, check whether the rows match, and don't error if they do.
TEST_F_EX(PgIndexBackfillTest,
          YB_DISABLE_TEST_IN_TSAN(CreateUniqueIndexWithOnlineWrites),
          PgIndexBackfillSlow) {
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));

  // Start a thread that continuously inserts distinct values.  The hope is that this would cause
  // inserts to happen at all permissions.
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this, kTableName, &stop = thread_holder.stop_flag()] {
    auto insert_conn = ASSERT_RESULT(Connect());
    int i = 0;
    while (!stop.load(std::memory_order_acquire)) {
      Status status = insert_conn.ExecuteFormat("INSERT INTO $0 VALUES ($1)", kTableName, ++i);
      if (!status.ok()) {
        // Schema version mismatches will likely occur when changing index permissions, and we can
        // just ignore them for the purposes of this test.
        // TODO(jason): no longer expect these errors after closing issue #3979.
        ASSERT_TRUE(status.IsNetworkError()) << status;
        std::string msg = status.message().ToBuffer();
        ASSERT_TRUE(msg.find("schema version mismatch") != std::string::npos) << status;
      }
    }
  });

  // Create unique index (should not complain about duplicate row).
  ASSERT_OK(conn.ExecuteFormat("CREATE UNIQUE INDEX ON $0 (i ASC)", kTableName));

  thread_holder.Stop();
}

// Simulate this situation:
//   Session A                                    Session B
//   ------------------------------------         -------------------------------------------
//   CREATE TABLE (i, j, PRIMARY KEY (i))
//                                                INSERT (1, 'a')
//   CREATE UNIQUE INDEX (j)
//   - DELETE_ONLY perm
//                                                DELETE (1, 'a')
//                                                (delete (1, 'a') to index)
//                                                INSERT (2, 'a')
//   - WRITE_DELETE perm
//   - BACKFILL perm
//     - get safe time for read
//                                                INSERT (3, 'a')
//                                                (insert (3, 'a') to index)
//     - do the actual backfill
//                                                (insert (2, 'a') to index--detect conflict)
//   - READ_WRITE_DELETE perm
// This test is for issue #6208.
TEST_F_EX(PgIndexBackfillTest,
          YB_DISABLE_TEST_IN_TSAN(CreateUniqueIndexWriteAfterSafeTime),
          PgIndexBackfillSlow) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const std::string& kIndexName = "i";
  const std::string& kNamespaceName = "yugabyte";
  const std::string& kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j char, PRIMARY KEY (i))", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 'a')", kTableName));

  std::thread create_index_thread([&] {
    LOG(INFO) << "Creating index";
    auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
    Status s = conn.ExecuteFormat("CREATE UNIQUE INDEX $0 ON $1 (j ASC)", kIndexName, kTableName);
    ASSERT_NOK(s);
    ASSERT_TRUE(s.IsNetworkError());
    ASSERT_TRUE(s.message().ToBuffer().find("duplicate key value")
                != std::string::npos) << s;
  });

  {
    std::map<std::string, bool> index_state_flags = {
      {"indislive", true},
      {"indisready", false},
      {"indisvalid", false},
    };

    LOG(INFO) << "Wait for indislive index state flag";
    // Deadline is 12s
    ASSERT_OK(WaitFor(
        std::bind(IsAtTargetIndexStateFlags, &conn, kIndexName, index_state_flags),
        12s,
        Format("Wait for index state flags to hit target: $0", index_state_flags)));

    LOG(INFO) << "Do insert and delete";
    ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE i = 1", kTableName));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (2, 'a')", kTableName));

    LOG(INFO) << "Check we're not yet at indisready index state flag";
    ASSERT_TRUE(ASSERT_RESULT(IsAtTargetIndexStateFlags(&conn, kIndexName, index_state_flags)));
  }

  {
    std::map<std::string, bool> index_state_flags = {
      {"indislive", true},
      {"indisready", true},
      {"indisvalid", false},
    };

    LOG(INFO) << "Wait for indisready index state flag";
    // Deadline is
    //   7s for sleep between indislive and indisready
    // + 5s for extra
    // = 12s
    ASSERT_OK(WaitFor(
        std::bind(IsAtTargetIndexStateFlags, &conn, kIndexName, index_state_flags),
        12s,
        Format("Wait for index state flags to hit target: $0", index_state_flags)));
  }

  {
    LOG(INFO) << "Wait for backfill (approx)";
    // 7s for sleep between indisready and backfill
    SleepFor(7s);

    LOG(INFO) << "Wait to get safe time for backfill (approx)";
    //   7s for sleep between get safe time and do backfill
    // / 2 to get the middle
    // = 3s
    SleepFor(3s);

    LOG(INFO) << "Do insert";
    // Deadline is
    //   4s for remainder of 7s sleep between get safe time and do backfill
    // + 5s for extra
    // = 9s
    CoarseBackoffWaiter waiter(CoarseMonoClock::Now() + 9s, CoarseMonoClock::Duration::max());
    while (true) {
      Status status = conn.ExecuteFormat("INSERT INTO $0 VALUES (3, 'a')", kTableName);
      LOG(INFO) << "Got " << yb::ToString(status);
      if (status.ok()) {
        break;
      } else {
        ASSERT_FALSE(status.IsIllegalState() &&
                     status.message().ToBuffer().find("Duplicate value") != std::string::npos)
            << "The insert should come before backfill, so it should not cause duplicate conflict.";
        ASSERT_TRUE(waiter.Wait());
      }
    }
  }

  LOG(INFO) << "Wait for CREATE INDEX to finish";
  create_index_thread.join();

  // Check.
  {
    CoarseBackoffWaiter waiter(CoarseMonoClock::Now() + 10s, CoarseMonoClock::Duration::max());
    while (true) {
      auto result = conn.FetchFormat("SELECT count(*) FROM $0", kTableName);
      if (result.ok()) {
        auto res = ASSERT_RESULT(result);
        const int64_t main_table_size = ASSERT_RESULT(GetInt64(res.get(), 0, 0));
        ASSERT_EQ(main_table_size, 2);
        break;
      }
      ASSERT_TRUE(result.status().IsQLError()) << result.status();
      ASSERT_TRUE(result.status().message().ToBuffer().find("schema version mismatch")
                  != std::string::npos) << result.status();
      ASSERT_TRUE(waiter.Wait());
    }
  }
}

// Simulate this situation:
//   Session A                                    Session B
//   ------------------------------------         -------------------------------------------
//   CREATE TABLE (i, j, PRIMARY KEY (i))
//                                                INSERT (1, 'a')
//   CREATE UNIQUE INDEX (j)
//   - indislive
//   - indisready
//   - backfill stage
//     - get safe time for read
//                                                DELETE (1, 'a')
//                                                (delete (1, 'a') to index)
//     - do the actual backfill
//       (insert (1, 'a') to index)
//   - indisvalid
// This test is for issue #6811.  Remember, backfilled rows get written with write time = safe time,
// so they should have an MVCC timestamp lower than that of the deletion.  If deletes to the index
// aren't written, then this test will always fail because the backfilled row has no delete to cover
// it.  If deletes to the index aren't retained, then this test will fail if compactions get rid of
// the delete before the backfilled row gets written.
TEST_F_EX(PgIndexBackfillTest,
          YB_DISABLE_TEST_IN_TSAN(RetainDeletes),
          PgIndexBackfillSlow) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const std::map<std::string, bool> index_state_flags{
    {"indislive", true},
    {"indisready", true},
    {"indisvalid", false},
  };
  const MonoDelta& kIndexStateFlagsUpdateDelay = MonoDelta::FromMilliseconds(
      ASSERT_RESULT(CheckedStoi(ASSERT_RESULT(
        cluster_->tserver_daemons()[0]->GetFlag("TEST_ysql_index_state_flags_update_delay_ms")))));
  const MonoDelta& kSlowDownBackfillDelay = MonoDelta::FromMilliseconds(
      ASSERT_RESULT(CheckedStoi(ASSERT_RESULT(
        cluster_->tserver_daemons()[0]->GetFlag("TEST_slowdown_backfill_by_ms")))));
  const std::string& kIndexName = "i";
  const std::string& kNamespaceName = "yugabyte";
  const std::string& kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j char, PRIMARY KEY (i))", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 'a')", kTableName));

  std::thread create_index_thread([&] {
    LOG(INFO) << "Creating index";
    auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
    ASSERT_OK(conn.ExecuteFormat("CREATE UNIQUE INDEX $0 ON $1 (j ASC)", kIndexName, kTableName));
  });

  ASSERT_OK(WaitForBackfillStage(&conn, kIndexName, kIndexStateFlagsUpdateDelay));

  LOG(INFO) << "Waiting out half the delay of executing backfill";
  SleepFor(kSlowDownBackfillDelay / 2);

  LOG(INFO) << "Deleting row";
  ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE i = 1", kTableName));

  // It should still be in the backfill stage.
  ASSERT_TRUE(ASSERT_RESULT(IsAtTargetIndexStateFlags(&conn, kIndexName, index_state_flags)));

  LOG(INFO) << "Waiting for create index thread to complete";
  create_index_thread.join();

  // Check.
  const Result<PGResultPtr>& result = conn.FetchFormat(
      "SELECT count(*) FROM $0 WHERE j = 'a'", kTableName);
  if (result.ok()) {
    int count = ASSERT_RESULT(GetInt64(result.get().get(), 0, 0));
    ASSERT_EQ(count, 0);
  } else if (result.status().IsNetworkError()) {
    Status s = result.status();
    const std::string msg = s.message().ToBuffer();
    if (msg.find("Given ybctid is not associated with any row in table") == std::string::npos) {
      FAIL() << "unexpected status: " << s;
    }
    FAIL() << "delete to index was not present by the time backfill happened: " << s;
  } else {
    Status s = result.status();
    FAIL() << "unexpected status: " << s;
  }
}

// Override the index backfill slow test to have smaller WaitUntilIndexPermissionsAtLeast deadline.
class PgIndexBackfillSlowClientDeadline : public PgIndexBackfillSlow {
 public:
  PgIndexBackfillSlowClientDeadline() {
    more_tserver_flags.push_back("--backfill_index_client_rpc_timeout_ms=3000");
  }
};

// Make sure that the postgres timeout when waiting for backfill to finish causes the index to not
// become public.  Simulate this situation:
//   CREATE INDEX
//   - indislive
//   - indisready
//   - backfill
//     - get safe time for read
//   - (timeout)
TEST_F_EX(PgIndexBackfillTest,
          YB_DISABLE_TEST_IN_TSAN(WaitBackfillTimeout),
          PgIndexBackfillSlowClientDeadline) {
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  Status status = conn.ExecuteFormat("CREATE INDEX ON $0 (i)", kTableName);
  ASSERT_TRUE(status.IsNetworkError()) << "Got " << status;
  const std::string msg = status.message().ToBuffer();
  ASSERT_TRUE(msg.find("Timed out waiting for Backfill Index") != std::string::npos)
      << status;

  // Make sure that the index is not public.
  ASSERT_FALSE(ASSERT_RESULT(conn.HasIndexScan(Format(
      "SELECT * FROM $0 WHERE i = 1",
      kTableName))));
}

// Make sure that you can still drop an index that failed to fully create.
TEST_F_EX(PgIndexBackfillTest,
          YB_DISABLE_TEST_IN_TSAN(DropAfterFail),
          PgIndexBackfillSlowClientDeadline) {
  const std::string kIndexName = "x";
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  Status status = conn.ExecuteFormat("CREATE INDEX $0 ON $1 (i)", kIndexName, kTableName);
  ASSERT_TRUE(status.IsNetworkError()) << "Got " << status;
  const std::string msg = status.message().ToBuffer();
  ASSERT_TRUE(msg.find("Timed out waiting for Backfill Index") != std::string::npos)
      << status;

  // Make sure that the index exists in DocDB metadata.
  auto tables = ASSERT_RESULT(client->ListTables());
  bool found = false;
  for (const auto& table : tables) {
    if (table.namespace_name() == kNamespaceName && table.table_name() == kIndexName) {
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found);

  ASSERT_OK(conn.ExecuteFormat("DROP INDEX $0", kIndexName));

  // Make sure that the index is gone.
  // Check postgres metadata.
  auto res = ASSERT_RESULT(conn.FetchFormat(
      "SELECT COUNT(*) FROM pg_class WHERE relname = '$0'", kIndexName));
  int value = ASSERT_RESULT(GetInt64(res.get(), 0, 0));
  ASSERT_EQ(value, 0);
  // Check DocDB metadata.
  tables = ASSERT_RESULT(client->ListTables());
  for (const auto& table : tables) {
    ASSERT_FALSE(table.namespace_name() == kNamespaceName && table.table_name() == kIndexName);
  }
}

// Override the index backfill slow test to have more than one master and 30s BackfillIndex client
// timeout.
class PgIndexBackfillSlowMultiMaster : public PgIndexBackfillSlow {
 public:
  PgIndexBackfillSlowMultiMaster() {
    more_tserver_flags.push_back("--backfill_index_client_rpc_timeout_ms=30000");
  }

  int GetNumMasters() const override { return 3; }
};

// Make sure that master leader change during backfill causes the index to not become public and
// doesn't cause any weird hangups or other issues.  Simulate this situation:
//   Thread A                                     Thread B
//   --------------------------                   ----------------------
//   CREATE INDEX
//   - indislive
//   - indisready
//   - backfill
//     - get safe time for read
//                                                master leader stepdown
// TODO(jason): update this test when handling master leader changes during backfill (issue #6218).
TEST_F_EX(PgIndexBackfillTest,
          YB_DISABLE_TEST_IN_TSAN(MasterLeaderStepdown),
          PgIndexBackfillSlowMultiMaster) {
  const MonoDelta& kIndexStateFlagsUpdateDelay = MonoDelta::FromMilliseconds(
      ASSERT_RESULT(CheckedStoi(ASSERT_RESULT(
        cluster_->tserver_daemons()[0]->GetFlag("TEST_ysql_index_state_flags_update_delay_ms")))));
  const MonoDelta& kSlowDownBackfillDelay = MonoDelta::FromMilliseconds(
      ASSERT_RESULT(CheckedStoi(ASSERT_RESULT(
        cluster_->tserver_daemons()[0]->GetFlag("TEST_slowdown_backfill_by_ms")))));
  const std::map<std::string, bool> index_state_flags{
    {"indislive", true},
    {"indisready", true},
    {"indisvalid", false},
  };
  const std::string kIndexName = "x";
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));

  std::thread create_index_thread([&] {
    auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
    // The CREATE INDEX should get master leader change during backfill so that its
    // WaitUntilIndexPermissionsAtLeast call starts querying the new leader.  Since the new leader
    // will be inactive at the WRITE_AND_DELETE docdb permission, it will wait until the deadline,
    // which is set to 30s.
    Status status = conn.ExecuteFormat("CREATE INDEX $0 ON $1 (i)", kIndexName, kTableName);
    ASSERT_TRUE(status.IsNetworkError()) << "Got " << status;
    const std::string msg = status.message().ToBuffer();
    ASSERT_TRUE(msg.find("Timed out waiting for Backfill Index") != std::string::npos)
        << status;
  });

  ASSERT_OK(WaitForBackfillStage(&conn, kIndexName, kIndexStateFlagsUpdateDelay));

  LOG(INFO) << "Waiting out half the delay of executing backfill";
  SleepFor(kSlowDownBackfillDelay / 2);

  LOG(INFO) << "Doing master leader stepdown";
  tserver::TabletServerErrorPB::Code error_code;
  ASSERT_OK(cluster_->StepDownMasterLeader(&error_code));

  // It should still be in the backfill stage.
  ASSERT_TRUE(ASSERT_RESULT(IsAtTargetIndexStateFlags(&conn, kIndexName, index_state_flags)));

  LOG(INFO) << "Waiting for create index thread to complete";
  create_index_thread.join();
}

// Make sure that DROP INDEX during backfill is handled well.  Simulate this situation:
//   Thread A                                     Thread B
//   --------------------------                   ----------------------
//   CREATE INDEX
//   - indislive
//   - indisready
//   - backfill
//     - get safe time for read
//                                                DROP INDEX
TEST_F_EX(PgIndexBackfillTest,
          YB_DISABLE_TEST_IN_TSAN(DropWhileBackfilling),
          PgIndexBackfillSlowMultiMaster) {
  const std::string kIndexName = "x";
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));

  std::thread create_index_thread([&] {
    auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
    Status status = conn.ExecuteFormat("CREATE INDEX $0 ON $1 (i)", kIndexName, kTableName);
    // Expect timeout because
    // DROP INDEX is currently not online and removes the index info from the indexed table
    // ==> the WaitUntilIndexPermissionsAtLeast will keep failing and retrying GetTableSchema on the
    // index.
    ASSERT_TRUE(status.IsNetworkError()) << "Got " << status;
    const std::string msg = status.message().ToBuffer();
    ASSERT_TRUE(msg.find("Timed out waiting for Backfill Index") != std::string::npos)
        << status;
  });

  // Sleep for
  //   7s (delay after committing pg_index indislive)
  // + 7s (delay after committing pg_index indisready)
  // + 3s (to be somewhere in the middle of the 7s delay before doing backfill)
  // = 17s
  LOG(INFO) << "Waiting 17s";
  std::this_thread::sleep_for(17s);
  LOG(INFO) << "Done waiting 17s";

  auto conn2 = ASSERT_RESULT(ConnectToDB(kNamespaceName));
  ASSERT_OK(conn2.ExecuteFormat("DROP INDEX $0", kIndexName));

  LOG(INFO) << "Waiting for create index thread to complete";
  create_index_thread.join();
}

} // namespace pgwrapper
} // namespace yb
