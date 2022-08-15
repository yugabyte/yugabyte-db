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
//

#include "yb/docdb/doc_read_context.h"

#include "yb/integration-tests/packed_row_test_base.h"

#include "yb/master/mini_master.h"

#include "yb/rocksdb/db/db_impl.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/range.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using namespace std::literals;

DECLARE_bool(ysql_enable_packed_row);
DECLARE_int32(history_cutoff_propagation_interval_ms);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_uint64(ysql_packed_row_size_limit);

namespace yb {
namespace pgwrapper {

class PgPackedRowTest : public PackedRowTestBase<PgMiniTestBase> {
 protected:
  void TestCompaction(const std::string& expr_suffix);
};

TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(Simple)) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, v1, v2) VALUES (1, 'one', 'two')"));

  auto value = ASSERT_RESULT(conn.FetchRowAsString("SELECT v1, v2 FROM t WHERE key = 1"));
  ASSERT_EQ(value, "one, two");

  ASSERT_OK(conn.Execute("UPDATE t SET v2 = 'three' where key = 1"));
  value = ASSERT_RESULT(conn.FetchRowAsString("SELECT v1, v2 FROM t WHERE key = 1"));
  ASSERT_EQ(value, "one, three");

  ASSERT_OK(conn.Execute("DELETE FROM t WHERE key = 1"));
  ASSERT_OK(conn.FetchMatrix("SELECT * FROM t", 0, 3));

  ASSERT_OK(conn.Execute("INSERT INTO t (key, v1, v2) VALUES (1, 'four', 'five')"));
  value = ASSERT_RESULT(conn.FetchRowAsString("SELECT v1, v2 FROM t WHERE key = 1"));
  ASSERT_EQ(value, "four, five");
}

TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(Update)) {
  // Test update with and without packed row enabled.

  auto conn = ASSERT_RESULT(Connect());

  // Insert one row, row will be packed.
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, v1, v2) VALUES (1, 'one', 'two')"));
  auto value = ASSERT_RESULT(conn.FetchRowAsString("SELECT v1, v2 FROM t WHERE key = 1"));
  ASSERT_EQ(value, "one, two");
  CheckNumRecords(cluster_.get(), /* expected_num_records = */ 1);

  // Update the row with column size exceeds limit size for paced row,
  // will insert two new entries to docdb.
  constexpr size_t kValueLimit = 512;
  const std::string kBigValue(kValueLimit, 'B');
  ASSERT_OK(conn.ExecuteFormat(
      "UPDATE t SET v1 = '$0', v2 = '$1' where key = 1", kBigValue, kBigValue));
  value = ASSERT_RESULT(conn.FetchRowAsString("SELECT v1, v2 FROM t WHERE key = 1"));
  ASSERT_EQ(value, Format("$0, $1", kBigValue, kBigValue));
  CheckNumRecords(cluster_.get(), /* expected_num_records = */ 3);

  // Update the row with two small strings, updated row will be packed.
  ASSERT_OK(conn.Execute("UPDATE t SET v1 = 'four', v2 = 'three' where key = 1"));
  value = ASSERT_RESULT(conn.FetchRowAsString("SELECT v1, v2 FROM t WHERE key = 1"));
  ASSERT_EQ(value, "four, three");
  CheckNumRecords(cluster_.get(), /* expected_num_records = */ 4);

  // Disable packed row, and after update, should have two entries inserted to docdb.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;

  ASSERT_OK(conn.Execute("UPDATE t SET v1 = 'six', v2 = 'five' where key = 1"));
  value = ASSERT_RESULT(conn.FetchRowAsString("SELECT v1, v2 FROM t WHERE key = 1"));
  ASSERT_EQ(value, "six, five");
  CheckNumRecords(cluster_.get(), /* expected_num_records = */ 6);
}

TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(UpdateReturning)) {
  // Test UPDATE...RETURNING with packed row enabled.

  auto conn = ASSERT_RESULT(Connect());

  // Insert one row, row will be packed.
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, v1, v2) VALUES (1, 'one', 'two')"));
  auto value = ASSERT_RESULT(conn.FetchRowAsString("SELECT v1, v2 FROM t WHERE key = 1"));
  ASSERT_EQ(value, "one, two");
  CheckNumRecords(cluster_.get(), /* expected_num_records = */ 1);

  // Update the row and return it.
  value = ASSERT_RESULT(conn.FetchRowAsString(
      "UPDATE t SET v1 = 'three', v2 = 'four' where key = 1 RETURNING v1, v2"));
  ASSERT_EQ(value, "three, four");
  CheckNumRecords(cluster_.get(), /* expected_num_records = */ 2);
}

TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(Random)) {
  constexpr int kModifications = 4000;
  constexpr int kKeys = 50;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 INT, v2 INT) SPLIT INTO 1 TABLETS"));

  std::unordered_map<int, std::pair<int, int>> key_state;

  std::mt19937_64 rng(42);

  for (int i = 1; i <= kModifications; ++i) {
    auto key = RandomUniformInt(1, kKeys, &rng);
    if (!key_state.count(key)) {
      auto v1 = RandomUniformInt<int>(&rng);
      auto v2 = RandomUniformInt<int>(&rng);
      VLOG(1) << "Insert, key: " << key << ", " << v1 << ", " << v2;
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO t (key, v1, v2) VALUES ($0, $1, $2)", key, v1, v2));
      key_state.emplace(key, std::make_pair(v1, v2));
    } else {
      switch (RandomUniformInt(0, 3, &rng)) {
        case 0:
          VLOG(1) << "Delete, key: " << key;
          ASSERT_OK(conn.ExecuteFormat("DELETE FROM t WHERE key = $0", key));
          key_state.erase(key);
          break;
        case 1: {
          auto v1 = RandomUniformInt<int>(&rng);
          VLOG(1) << "Update, key: " << key << ", v1 = " << v1;
          ASSERT_OK(conn.ExecuteFormat("UPDATE t SET v1 = $1 WHERE key = $0", key, v1));
          key_state[key].first = v1;
          break;
        }
        case 2: {
          auto v2 = RandomUniformInt<int>(&rng);
          VLOG(1) << "Update, key: " << key << ", v2 = " << v2;
          ASSERT_OK(conn.ExecuteFormat("UPDATE t SET v2 = $1 WHERE key = $0", key, v2));
          key_state[key].second = v2;
          break;
        }
        case 3: {
          auto v1 = RandomUniformInt<int>(&rng);
          auto v2 = RandomUniformInt<int>(&rng);
          VLOG(1) << "Update, key: " << key << ", v1 = " << v1 << ", v2 = " << v2;
          ASSERT_OK(conn.ExecuteFormat(
              "UPDATE t SET v1 = $1, v2 = $2 WHERE key = $0", key, v1, v2));
          key_state[key] = std::make_pair(v1, v2);
          break;
        }
      }
    }
    auto result = ASSERT_RESULT(conn.Fetch("SELECT * FROM t"));
    auto state_copy = key_state;
    for (int row = 0, num_rows = PQntuples(result.get()); row != num_rows; ++row) {
      auto key = ASSERT_RESULT(GetInt32(result.get(), row, 0));
      SCOPED_TRACE(Format("Key: $0", key));
      auto it = state_copy.find(key);
      ASSERT_NE(it, state_copy.end());
      auto v1 = ASSERT_RESULT(GetInt32(result.get(), row, 1));
      ASSERT_EQ(it->second.first, v1);
      auto v2 = ASSERT_RESULT(GetInt32(result.get(), row, 2));
      ASSERT_EQ(it->second.second, v2);
      state_copy.erase(key);
    }
    ASSERT_TRUE(state_copy.empty()) << AsString(state_copy);
    if (i % 200 == 0 || i == kModifications) {
      LOG(INFO) << "Compacting after " << i << " operations";
      ASSERT_OK(cluster_->CompactTablets());
    } else if (i % 50 == 0) {
      ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync));
    }
  }
  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
  for (const auto& peer : peers) {
    if (!peer->tablet()->TEST_db()) {
      continue;
    }
    std::unordered_set<std::string> values;
    peer->tablet()->TEST_DocDBDumpToContainer(tablet::IncludeIntents::kTrue, &values);
    std::vector<std::string> sorted_values(values.begin(), values.end());
    std::sort(sorted_values.begin(), sorted_values.end());
    for (const auto& line : sorted_values) {
      LOG(INFO) << "Record: " << line;
    }
    ASSERT_EQ(values.size(), key_state.size());
  }
}

TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(SchemaChange)) {
  constexpr int kKey = 10;
  constexpr int kValue1 = 10;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 INT, v2 INT) SPLIT INTO 1 TABLETS"));

  ASSERT_OK(conn.ExecuteFormat("INSERT INTO t (key, v1, v2) VALUES ($0, $1, $1)", kKey, kValue1));

  ASSERT_OK(conn.Execute("ALTER TABLE t ADD COLUMN v3 TEXT"));
  ASSERT_OK(conn.Execute("ALTER TABLE t DROP COLUMN v2"));

  ASSERT_OK(conn.ExecuteFormat("UPDATE t SET v1 = -v1 WHERE key = $0", kKey));

  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync));
  ASSERT_OK(cluster_->CompactTablets());

  auto value = ASSERT_RESULT(conn.FetchValue<std::string>(
      Format("SELECT v3 FROM t WHERE key = $0", kKey)));
  ASSERT_EQ(value, "");
}

// Check that we GC old schemas. I.e. when there are no more packed rows with this schema version.
TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(SchemaGC)) {
  constexpr int kModifications = 1200;

  auto conn = ASSERT_RESULT(Connect());
  std::map<int, int> columns; // Active columns. Maps to step when column was added.
  int next_column_idx = 0;

  std::mt19937_64 rng(42);

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY) SPLIT INTO 1 TABLETS"));

  for (int i = 1; i <= kModifications; ++i) {
    auto alter_schema = i % 25 == 0;
    auto last_iteration = i == kModifications;
    if (alter_schema) {
      if (columns.empty() || RandomUniformBool(&rng)) {
        columns.emplace(++next_column_idx, i);
        ASSERT_OK(conn.ExecuteFormat("ALTER TABLE t ADD COLUMN v$0 INT", next_column_idx));
      } else {
        auto it = columns.begin();
        std::advance(it, RandomUniformInt<size_t>(0, columns.size() - 1));
        ASSERT_OK(conn.ExecuteFormat("ALTER TABLE t DROP COLUMN v$0", it->first));
        columns.erase(it);
      }
    }

    std::string expr = "INSERT INTO t (key";
    std::string values = std::to_string(i);
    for (const auto& p : columns) {
      expr += Format(", v$0", p.first);
      values += Format(", $0", p.first * kModifications + i);
    }
    expr += ") VALUES (";
    expr += values;
    expr += ")";
    ASSERT_OK(conn.Execute(expr));

    if (i % 100 == 0 || last_iteration) {
      ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync));
      if (i % 400 == 0 || last_iteration) {
        ASSERT_OK(cluster_->CompactTablets());
      }
    }

    if (alter_schema || last_iteration) {
      auto res = ASSERT_RESULT(conn.FetchMatrix(
          "SELECT * FROM t", i, narrow_cast<int>(columns.size() + 1)));
      for (int row = 0; row != i; ++row) {
        int key = ASSERT_RESULT(GetValue<int>(res.get(), row, 0));
        int idx = 0;
        for (const auto& p : columns) {
          auto is_null = PQgetisnull(res.get(), row, ++idx);
          ASSERT_EQ(is_null, key < p.second);
          if (is_null) {
            continue;
          }
          auto value = ASSERT_RESULT(GetValue<int>(res.get(), row, idx));
          ASSERT_EQ(value, p.first * kModifications + key);
        }
      }
    }
  }

  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
  for (const auto& peer : peers) {
    if (peer->table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
      continue;
    }
    auto files = peer->tablet()->doc_db().regular->GetLiveFilesMetaData();
    auto table_info = peer->tablet_metadata()->primary_table_info();
    ASSERT_EQ(table_info->doc_read_context->schema_packing_storage.SchemaCount(), 1);
  }
}

void PgPackedRowTest::TestCompaction(const std::string& expr_suffix) {
  constexpr int kKeys = 10;
  constexpr size_t kValueLen = 32;

  auto conn = ASSERT_RESULT(ConnectToDB("test"));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE t1 (key INT PRIMARY KEY, value TEXT) $0", expr_suffix));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE t2 (key INT PRIMARY KEY, value INT) $0", expr_suffix));

  std::mt19937_64 rng(42);

  std::string t1;
  std::string t2;
  for (auto key : Range(kKeys)) {
    if (key) {
      t1 += ";";
      t2 += ";";
    }
    auto t1_val = RandomHumanReadableString(kValueLen, &rng);
    t1 += Format("$0,$1", key, t1_val);
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t1 (key, value) VALUES ($0, '')", key));
    ASSERT_OK(conn.ExecuteFormat("UPDATE t1 SET value = '$1' WHERE key = $0", key, t1_val));

    auto t2_val = RandomUniformInt<int32_t>(&rng);
    t2 += Format("$0,$1", key, t2_val);
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t2 (key, value) VALUES ($0, 0)", key));
    ASSERT_OK(conn.ExecuteFormat("UPDATE t2 SET value = $1 WHERE key = $0", key, t2_val));
  }

  for (int step = 0; step <= 2; ++step) {
    SCOPED_TRACE(Format("Step: $0", step));

    if (step == 1) {
      ASSERT_OK(cluster_->FlushTablets());
    } else if (step == 2) {
      ASSERT_OK(cluster_->CompactTablets());
    }

    auto value = ASSERT_RESULT(conn.FetchAllAsString("SELECT * FROM t1 ORDER BY key", ",", ";"));
    ASSERT_EQ(value, t1);
    value = ASSERT_RESULT(conn.FetchAllAsString("SELECT * FROM t2 ORDER BY key", ",", ";"));
    ASSERT_EQ(value, t2);
  }
}

TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(TableGroup)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE DATABASE test"));
  conn = ASSERT_RESULT(ConnectToDB("test"));
  ASSERT_OK(conn.Execute("CREATE TABLEGROUP tg"));

  TestCompaction("TABLEGROUP tg");
}

TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(Colocated)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE DATABASE test WITH colocated = true"));
  TestCompaction("WITH (colocated = true)");
}

TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(CompactAfterTransaction)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE test (key BIGSERIAL PRIMARY KEY, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, 'one')"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (2, 'two')"));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("UPDATE test SET value = 'odin' WHERE key = 1"));
  ASSERT_OK(conn.Execute("UPDATE test SET value = 'dva' WHERE key = 2"));
  ASSERT_OK(conn.CommitTransaction());
  ASSERT_OK(cluster_->CompactTablets());
  auto value = ASSERT_RESULT(conn.FetchAllAsString("SELECT * FROM test ORDER BY key"));
  ASSERT_EQ(value, "1, odin; 2, dva");
}

TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(Serial)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE sbtest1(id SERIAL, PRIMARY KEY (id))"));
}

TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(PackDuringCompaction)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;

  const auto kNumKeys = 10;
  const auto kKeys = Range(1, kNumKeys + 1);

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 INT NOT NULL) SPLIT INTO 1 TABLETS"));

  std::string all_rows;
  for (auto i : kKeys) {
    auto expr = Format("$0, $0, -$0", i);
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t (key, v1, v2) VALUES ($0)", expr));
    if (!all_rows.empty()) {
      all_rows += "; ";
    }
    all_rows += expr;
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;

  ASSERT_OK(cluster_->CompactTablets());

  ASSERT_NO_FATALS(CheckNumRecords(cluster_.get(), kNumKeys));

  auto fetched_rows = ASSERT_RESULT(conn.FetchAllAsString("SELECT * FROM t ORDER BY key"));
  ASSERT_EQ(fetched_rows, all_rows);
}

// Check that we correctly interpret packed row size limit.
TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(BigValue)) {
  constexpr size_t kValueLimit = 512;
  const std::string kBigValue(kValueLimit, 'B');
  const std::string kHalfBigValue(kValueLimit / 2, 'H');
  const std::string kSmallValue(kValueLimit / 4, 'S');

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_packed_row_size_limit) = kValueLimit;

  auto conn = ASSERT_RESULT(Connect());
  std::array<std::string, 2> values = {kBigValue, kHalfBigValue};

  auto check_state = [this, &conn, &values](size_t expected_num_records) -> Status {
    RETURN_NOT_OK(cluster_->CompactTablets());
    auto fetched_rows = VERIFY_RESULT(conn.FetchAllAsString("SELECT v1, v2 FROM t"));
    SCHECK_EQ(
        fetched_rows, Format("$0, $1", values[0], values[1]), IllegalState, "Wrong DB content");
    CheckNumRecords(cluster_.get(), expected_num_records);
    return Status::OK();
  };

  auto update_value = [&conn, &values, &check_state](
      size_t idx, const std::string& new_value, size_t expected_num_records) -> Status {
    RETURN_NOT_OK(conn.ExecuteFormat("UPDATE t SET v$0 = '$1' WHERE key = 1", idx + 1, new_value));
    values[idx] = new_value;
    return check_state(expected_num_records);
  };

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT) SPLIT INTO 1 TABLETS"));

  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t (key, v1, v2) VALUES (1, '$0', '$1')", values[0], values[1]));

  ASSERT_OK(check_state(2));
  ASSERT_OK(update_value(1, kBigValue, 3));
  ASSERT_OK(update_value(0, kHalfBigValue, 2));

  ASSERT_OK(conn.Execute("DELETE FROM t WHERE key = 1"));

  values[0] = kSmallValue;
  values[1] = kHalfBigValue;

  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t (key, v1, v2) VALUES (1, '$0', '$1')", values[0], values[1]));

  ASSERT_OK(update_value(0, kHalfBigValue, 2));
}

TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(AddColumn)) {
  {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute("CREATE DATABASE test WITH colocated = true"));
  }

  auto conn = ASSERT_RESULT(ConnectToDB("test"));

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, ival INT) WITH (colocated = true)"));
  ASSERT_OK(conn.Execute("CREATE INDEX t_idx ON t(ival)"));

  auto conn2 = ASSERT_RESULT(ConnectToDB("test"));
  ASSERT_OK(conn2.Execute("INSERT INTO t (key, ival) VALUES (1, 1)"));

  ASSERT_OK(conn.Execute("ALTER TABLE t ADD COLUMN v1 INT"));

  ASSERT_OK(conn2.Execute("INSERT INTO t (key, ival) VALUES (2, 2)"));
}

// Checks repacking of columns then would not fit into limit with new schema due to added columns.
TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(PackOverflow)) {
  constexpr int kRange = 32;

  FLAGS_ysql_packed_row_size_limit = 128;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT) SPLIT INTO 1 TABLETS"));

  for (auto key : Range(0, kRange + 1)) {
    auto len = FLAGS_ysql_packed_row_size_limit - kRange / 2 + key;
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO t VALUES ($0, '$1')", key, RandomHumanReadableString(len)));
  }

  ASSERT_OK(conn.Execute("ALTER TABLE t ADD COLUMN v2 TEXT"));

  ASSERT_OK(cluster_->CompactTablets());
}

TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(AddDropColumn)) {
  constexpr int kKeys = 15;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY) SPLIT INTO 1 TABLETS"));

  TestThreadHolder thread_holder;

  CountDownLatch alter_latch(1);

  thread_holder.AddThread([this, &stop_flag = thread_holder.stop_flag(), &alter_latch] {
    std::set<int> columns;
    int current_column = 0;
    auto conn = ASSERT_RESULT(Connect());
    bool signalled = false;
    while (!stop_flag.load()) {
      if (columns.empty() || (columns.size() < 10 && RandomUniformBool())) {
        columns.insert(++current_column);
        ASSERT_OK(conn.ExecuteFormat("ALTER TABLE t ADD COLUMN v$0 INT", current_column));
      } else {
        auto column_it = RandomIterator(columns);
        ASSERT_OK(conn.ExecuteFormat("ALTER TABLE t DROP COLUMN v$0", *column_it));
        columns.erase(column_it);
      }
      if (!signalled) {
        alter_latch.CountDown();
        signalled = true;
      }
      std::this_thread::sleep_for(100ms * kTimeMultiplier);
    }
  });

  alter_latch.Wait();

  for (auto key : Range(kKeys)) {
    LOG(INFO) << "Insert key: " << key;
    auto status = conn.ExecuteFormat("INSERT INTO t VALUES ($0)", key);
    if (!status.ok()) {
      LOG(INFO) << "Insert failed for " << key << ": " << status;
      // TODO temporary workaround for YSQL issue #8096.
      if (status.ToString().find("Invalid column number") != std::string::npos) {
        conn = ASSERT_RESULT(Connect());
        continue;
      }
      ASSERT_OK(status);
    }
  }

  thread_holder.Stop();
}

TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(CoveringIndex)) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX t_idx ON t(v2) INCLUDE (v1)"));

  ASSERT_OK(conn.Execute("INSERT INTO t (key, v1, v2) VALUES (1, 'one', 'odin')"));
}

} // namespace pgwrapper
} // namespace yb
