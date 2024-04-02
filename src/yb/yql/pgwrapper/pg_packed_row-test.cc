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

#include <optional>

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/docdb_debug.h"

#include "yb/integration-tests/packed_row_test_base.h"

#include "yb/master/mini_master.h"

#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/sst_dump_tool.h"

#include "yb/tablet/kv_formatter.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/range.h"
#include "yb/util/string_util.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

using namespace std::literals;

DECLARE_bool(TEST_dcheck_for_missing_schema_packing);
DECLARE_bool(TEST_keep_intent_doc_ht);
DECLARE_bool(TEST_skip_aborting_active_transactions_during_schema_change);
DECLARE_bool(ysql_enable_pack_full_row_update);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(ysql_enable_packed_row_for_colocated_table);
DECLARE_bool(ysql_use_packed_row_v2);
DECLARE_int32(history_cutoff_propagation_interval_ms);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_uint64(rocksdb_universal_compaction_always_include_size_threshold);
DECLARE_uint64(ysql_packed_row_size_limit);

namespace yb {
namespace pgwrapper {

class PgPackedRowTest : public PackedRowTestBase<PgMiniTestBase>,
                        public testing::WithParamInterface<dockv::PackedRowVersion> {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_use_packed_row_v2) =
        GetParam() == dockv::PackedRowVersion::kV2;
    PackedRowTestBase<PgMiniTestBase>::SetUp();
  }

  void TestCompaction(size_t num_keys, const std::string& expr_suffix);
  void TestColocated(size_t num_keys, int num_expected_records);
  void TestSstDump(bool specify_metadata, std::string* output);
  void TestAppliedSchemaVersion(bool colocated);
};

TEST_P(PgPackedRowTest, Simple) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, v1, v2) VALUES (1, 'one', 'two')"));

  auto row = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(
      "SELECT v1, v2 FROM t WHERE key = 1")));
  ASSERT_EQ(row, (decltype(row){"one", "two"}));

  ASSERT_OK(conn.Execute("UPDATE t SET v2 = 'three' where key = 1"));
  row = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(
      "SELECT v1, v2 FROM t WHERE key = 1")));
  ASSERT_EQ(row, (decltype(row){"one", "three"}));

  ASSERT_OK(conn.Execute("DELETE FROM t WHERE key = 1"));
  ASSERT_OK(conn.FetchMatrix("SELECT * FROM t", 0, 3));

  ASSERT_OK(conn.Execute("INSERT INTO t (key, v1, v2) VALUES (1, 'four', 'five')"));
  row = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(
      "SELECT v1, v2 FROM t WHERE key = 1")));
  ASSERT_EQ(row, (decltype(row){"four", "five"}));
}

TEST_P(PgPackedRowTest, Update) {
  // Test update with and without packed row enabled.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_pack_full_row_update) = true;

  auto conn = ASSERT_RESULT(Connect());

  // Insert one row, row will be packed.
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, v1, v2) VALUES (1, 'one', 'two')"));
  auto row = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(
      "SELECT v1, v2 FROM t WHERE key = 1")));
  ASSERT_EQ(row, (decltype(row){"one", "two"}));
  CheckNumRecords(cluster_.get(), /* expected_num_records = */ 1);

  // Update the row with column size exceeds limit size for paced row,
  // will insert two new entries to docdb.
  constexpr size_t kValueLimit = 512;
  const std::string kBigValue(kValueLimit, 'B');
  ASSERT_OK(conn.ExecuteFormat(
      "UPDATE t SET v1 = '$0', v2 = '$1' where key = 1", kBigValue, kBigValue));
  row = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(
      "SELECT v1, v2 FROM t WHERE key = 1")));
  ASSERT_EQ(row, (decltype(row){kBigValue, kBigValue}));
  CheckNumRecords(cluster_.get(), /* expected_num_records = */ 3);

  // Update the row with two small strings, updated row will be packed.
  ASSERT_OK(conn.Execute("UPDATE t SET v1 = 'four', v2 = 'three' where key = 1"));
  row = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(
      "SELECT v1, v2 FROM t WHERE key = 1")));
  ASSERT_EQ(row, (decltype(row){"four", "three"}));
  CheckNumRecords(cluster_.get(), /* expected_num_records = */ 4);

  // Disable packed row, and after update, should have two entries inserted to docdb.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;

  ASSERT_OK(conn.Execute("UPDATE t SET v1 = 'six', v2 = 'five' where key = 1"));
  row = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(
      "SELECT v1, v2 FROM t WHERE key = 1")));
  ASSERT_EQ(row, (decltype(row){"six", "five"}));
  CheckNumRecords(cluster_.get(), /* expected_num_records = */ 6);
}

// Alter 2 tables and performs compactions concurrently. See #13846 for details.
TEST_P(PgPackedRowTest, AlterTable) {
  static const auto kExpectedErrors = {
      "Try again",
      "Snapshot too old",
      "Network error"
  };

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 1 * kTimeMultiplier;

  // TODO (#19975): Enable read committed isolation
  auto conn = ASSERT_RESULT(
      SetDefaultTransactionIsolation(Connect(), IsolationLevel::SNAPSHOT_ISOLATION));

  TestThreadHolder thread_holder;
  for (int i = 0; i != 2; ++i) {
    thread_holder.AddThreadFunctor([this, i, &stop = thread_holder.stop_flag()] {
      auto table_name = Format("test_$0", i);
      // TODO (#19975): Enable read committed isolation
      auto conn = ASSERT_RESULT(
          SetDefaultTransactionIsolation(Connect(), IsolationLevel::SNAPSHOT_ISOLATION));
      std::vector<int> columns;
      int column_idx = 0;
      ASSERT_OK(conn.ExecuteFormat(
          "CREATE TABLE $0 (key INT PRIMARY KEY) SPLIT INTO 1 TABLETS", table_name));
      while (!stop.load()) {
        if (columns.empty() || RandomUniformBool()) {
          auto status = conn.ExecuteFormat(
              "ALTER TABLE $0 ADD COLUMN column_$1 INT", table_name, column_idx);
          if (status.ok()) {
            LOG(INFO) << table_name << ", added column: " << column_idx;
            columns.push_back(column_idx);
          } else {
            auto msg = status.ToString();
            LOG(INFO) << table_name << ", failed to add column " << column_idx << ": " << msg;
            ASSERT_TRUE(HasSubstring(msg, kExpectedErrors)) << msg;
          }
          ++column_idx;
        } else {
          size_t idx = RandomUniformInt<size_t>(0, columns.size() - 1);
          auto status = conn.ExecuteFormat(
              "ALTER TABLE $0 DROP COLUMN column_$1", table_name, columns[idx]);
          if (status.ok() ||
              status.ToString().find("The specified column does not exist") != std::string::npos) {
            LOG(INFO) << table_name << ", dropped column: " << columns[idx] << ", " << status;
            columns[idx] = columns.back();
            columns.pop_back();
          } else {
            auto msg = status.ToString();
            LOG(INFO) << table_name << ", failed to drop column " << columns[idx] << ": " << msg;
            ASSERT_TRUE(HasSubstring(msg, kExpectedErrors)) << msg;
          }
        }
      }
    });
  }

  auto deadline = CoarseMonoClock::now() + 90s;

  while (!thread_holder.stop_flag().load() && CoarseMonoClock::now() < deadline) {
    ASSERT_OK(cluster_->mini_master()->tablet_peer()->tablet()->ForceManualRocksDBCompact());
  }

  thread_holder.Stop();
}

TEST_P(PgPackedRowTest, UpdateReturning) {
  // Test UPDATE...RETURNING with packed row enabled.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_pack_full_row_update) = true;

  auto conn = ASSERT_RESULT(Connect());

  // Insert one row, row will be packed.
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, v1, v2) VALUES (1, 'one', 'two')"));
  auto row = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(
      "SELECT v1, v2 FROM t WHERE key = 1")));
  ASSERT_EQ(row, (decltype(row){"one", "two"}));
  CheckNumRecords(cluster_.get(), /* expected_num_records = */ 1);

  // Update the row and return it.
  row = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(
      "UPDATE t SET v1 = 'three', v2 = 'four' where key = 1 RETURNING v1, v2")));
  ASSERT_EQ(row, (decltype(row){"three", "four"}));
  CheckNumRecords(cluster_.get(), /* expected_num_records = */ 2);
}

TEST_P(PgPackedRowTest, Random) {
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
    auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t, int32_t>("SELECT * FROM t")));
    auto state_copy = key_state;
    for (const auto& [key, value1, value2] : rows) {
      SCOPED_TRACE(Format("Key: $0", key));
      auto it = state_copy.find(key);
      ASSERT_NE(it, state_copy.end());
      ASSERT_EQ(it->second.first, value1);
      ASSERT_EQ(it->second.second, value2);
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
    if (!peer->tablet()->regular_db()) {
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

TEST_P(PgPackedRowTest, SchemaChange) {
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

  auto value = ASSERT_RESULT(conn.FetchRow<std::optional<std::string>>(
      Format("SELECT v3 FROM t WHERE key = $0", kKey)));
  ASSERT_EQ(value, std::nullopt);
}

// Check that we GC old schemas. I.e. when there are no more packed rows with this schema version.
TEST_P(PgPackedRowTest, SchemaGC) {
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
        auto key = ASSERT_RESULT(GetValue<int32_t>(res.get(), row, 0));
        int idx = 0;
        for (const auto& p : columns) {
          auto opt_value = ASSERT_RESULT(GetValue<std::optional<int>>(res.get(), row, ++idx));
          const auto is_null = !opt_value;
          ASSERT_EQ(is_null, key < p.second) << ", key: " << key << ", p.second: " << p.second;
          if (is_null) {
            continue;
          }
          ASSERT_EQ(*opt_value, p.first * kModifications + key);
        }
      }
    }
  }

  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
  for (const auto& peer : peers) {
    if (peer->TEST_table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
      continue;
    }
    auto files = peer->tablet()->regular_db()->GetLiveFilesMetaData();
    auto table_info = peer->tablet_metadata()->primary_table_info();
    ASSERT_EQ(table_info->doc_read_context->schema_packing_storage.SchemaCount(), 1);
  }
}

void PgPackedRowTest::TestCompaction(size_t num_keys, const std::string& expr_suffix) {
  constexpr size_t kValueLen = 32;

  auto conn = ASSERT_RESULT(ConnectToDB("test"));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE t1 (key INT PRIMARY KEY, value TEXT) $0", expr_suffix));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE t2 (key INT PRIMARY KEY, value INT) $0", expr_suffix));

  std::mt19937_64 rng(42);

  std::vector<std::tuple<int32_t, std::string>> expected_rows_t1(num_keys);
  std::vector<std::tuple<int32_t, int32_t>> expected_rows_t2(num_keys);
  for (size_t i = 0; i < num_keys; ++i) {
    auto key = i + 1;
    auto t1_val = RandomHumanReadableString(kValueLen, &rng);
    expected_rows_t1[i] = {key, t1_val};
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t1 (key, value) VALUES ($0, '')", key));
    ASSERT_OK(conn.ExecuteFormat("UPDATE t1 SET value = '$1' WHERE key = $0", key, t1_val));

    auto t2_val = RandomUniformInt<int32_t>(&rng);
    expected_rows_t2[i] = {key, t2_val};
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

    auto rows_t1 = ASSERT_RESULT((conn.FetchRows<int32_t, std::string>(
        "SELECT * FROM t1 ORDER BY key")));
    ASSERT_EQ(rows_t1, expected_rows_t1);
    auto rows_t2 = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t>(
        "SELECT * FROM t2 ORDER BY key")));
    ASSERT_EQ(rows_t2, expected_rows_t2);
  }
}

void PgPackedRowTest::TestColocated(size_t num_keys, int num_expected_records) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE DATABASE test WITH colocated = true"));
  TestCompaction(num_keys, "WITH (colocated = true)");
  CheckNumRecords(cluster_.get(), num_expected_records);
}

TEST_P(PgPackedRowTest, TableGroup) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE DATABASE test"));
  conn = ASSERT_RESULT(ConnectToDB("test"));
  ASSERT_OK(conn.Execute("CREATE TABLEGROUP tg"));

  TestCompaction(/* num_keys = */ 10, "TABLEGROUP tg");
}

TEST_P(PgPackedRowTest, Colocated) {
  TestColocated(/* num_keys = */ 10, /* num_expected_records = */ 20);
}

TEST_P(PgPackedRowTest, ColocatedCompactionPackRowDisabled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row_for_colocated_table) = false;
  TestColocated(/* num_keys = */ 10, /* num_expected_records = */ 40);
}

TEST_P(PgPackedRowTest, ColocatedPackRowDisabled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row_for_colocated_table) = false;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE DATABASE test WITH colocated = true"));
  conn = ASSERT_RESULT(ConnectToDB("test"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (key INT PRIMARY KEY, value TEXT, payload TEXT) WITH (colocated = true)"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 (key, value, payload) VALUES (1, '', '')"));
  // The only row should not be packed.
  CheckNumRecords(cluster_.get(), 3);
  // Trigger full row update.
  ASSERT_OK(conn.Execute("UPDATE t1 SET value = '1', payload = '1' WHERE key = 1"));
  // The updated row should not be packed.
  CheckNumRecords(cluster_.get(), 5);

  // Enable pack row for colocated table and trigger compaction.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row_for_colocated_table) = true;
  ASSERT_OK(cluster_->CompactTablets());
  CheckNumRecords(cluster_.get(), 1);
}

TEST_P(PgPackedRowTest, CompactAfterTransaction) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_keep_intent_doc_ht) = true;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE test (key BIGSERIAL PRIMARY KEY, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, 'one')"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (2, 'two')"));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("UPDATE test SET value = 'odin' WHERE key = 1"));
  ASSERT_OK(conn.Execute("UPDATE test SET value = 'dva' WHERE key = 2"));
  ASSERT_OK(conn.CommitTransaction());
  ASSERT_OK(cluster_->CompactTablets());
  auto rows = ASSERT_RESULT((conn.FetchRows<int64_t, std::string>(
      "SELECT * FROM test ORDER BY key")));
  ASSERT_EQ(rows, (decltype(rows){{1, "odin"}, {2, "dva"}}));
}

TEST_P(PgPackedRowTest, Serial) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE sbtest1(id SERIAL, PRIMARY KEY (id))"));
}

TEST_P(PgPackedRowTest, PackDuringCompaction) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;

  const auto kNumKeys = 10;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 INT NOT NULL) SPLIT INTO 1 TABLETS"));

  std::vector<std::tuple<int32_t, std::string, int32_t>> expected_rows(kNumKeys);
  for (size_t i = 0; i < kNumKeys; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t (key, v1, v2) VALUES ($0, $0, -$0)", i));
    expected_rows[i] = {i, std::to_string(i), -i};
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;

  ASSERT_OK(cluster_->CompactTablets());

  ASSERT_NO_FATALS(CheckNumRecords(cluster_.get(), kNumKeys));

  auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, std::string, int32_t>(
      "SELECT * FROM t ORDER BY key")));
  ASSERT_EQ(rows, expected_rows);
}

// Check that we correctly interpret packed row size limit.
TEST_P(PgPackedRowTest, BigValue) {
  constexpr size_t kValueLimit = 512;
  const std::string kBigValue(kValueLimit, 'B');
  const std::string kHalfBigValue(kValueLimit / 2, 'H');
  const std::string kSmallValue(kValueLimit / 4, 'S');

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_packed_row_size_limit) = kValueLimit;

  auto conn = ASSERT_RESULT(Connect());
  std::array<std::string, 2> values = {kBigValue, kHalfBigValue};

  auto check_state = [this, &conn, &values](size_t expected_num_records) -> Status {
    RETURN_NOT_OK(cluster_->CompactTablets());
    auto row = VERIFY_RESULT((conn.FetchRow<std::string, std::string>(
        "SELECT v1, v2 FROM t")));
    SCHECK_EQ(row, std::tuple_cat(values), IllegalState, "Wrong DB content");
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

TEST_P(PgPackedRowTest, AddColumn) {
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
TEST_P(PgPackedRowTest, PackOverflow) {
  constexpr int kRange = 32;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_packed_row_size_limit) = 128;
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

TEST_P(PgPackedRowTest, AddDropColumn) {
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
    if (!(status.ok() || IsRetryable(status))) {
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

TEST_P(PgPackedRowTest, CoveringIndex) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX t_idx ON t(v2) INCLUDE (v1)"));

  ASSERT_OK(conn.Execute("INSERT INTO t (key, v1, v2) VALUES (1, 'one', 'odin')"));
}

TEST_P(PgPackedRowTest, Transaction) {
  // Set retention interval to 0, to repack all recently flushed entries.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;

  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_TEST_skip_aborting_active_transactions_during_schema_change) = true;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key) VALUES (1)"));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("INSERT INTO t (key) VALUES (2)"));
  ASSERT_OK(conn.Execute("ALTER TABLE t ADD COLUMN v TEXT"));
  ASSERT_OK(cluster_->CompactTablets(docdb::SkipFlush::kTrue));
  ASSERT_OK(conn.CommitTransaction());

  ASSERT_OK(cluster_->CompactTablets());
}

TEST_P(PgPackedRowTest, CleanupIntentDocHt) {
  // Set retention interval to 0, to repack all recently flushed entries.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, value INT) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 2)"));

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("UPDATE t SET value = 3 WHERE key = 1"));
  ASSERT_OK(conn.CommitTransaction());

  ASSERT_OK(WaitFor([this] {
    return CountIntents(cluster_.get()) == 0;
  }, 10s, "Intents cleanup"));

  ASSERT_OK(cluster_->CompactTablets());

  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
  for (const auto& peer : peers) {
    if (!peer->tablet()->regular_db()) {
      continue;
    }
    auto dump = peer->tablet()->TEST_DocDBDumpStr(tablet::IncludeIntents::kTrue);
    LOG(INFO) << "Dump: " << dump;
    ASSERT_EQ(dump.find("intent doc ht"), std::string::npos);
  }
}

void PgPackedRowTest::TestAppliedSchemaVersion(bool colocated) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_universal_compaction_always_include_size_threshold) =
      1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row_for_colocated_table) = true;

  auto conn = ASSERT_RESULT(Connect());
  if (colocated) {
    ASSERT_OK(conn.Execute("CREATE DATABASE test WITH colocated = true"));
    conn = ASSERT_RESULT(ConnectToDB("test"));
  }
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE t (key TEXT PRIMARY KEY, v1 INT, v2 INT)$0",
      colocated ? "" : "SPLIT INTO 1 TABLETS"));
  if (colocated) {
    ASSERT_OK(conn.Execute("CREATE INDEX t_v1 ON t (v1)"));
    ASSERT_OK(conn.Execute("CREATE INDEX t_v2 ON t (v2)"));
    ASSERT_OK(conn.Execute("CREATE INDEX t_v12 ON t (v1, v2)"));
    ASSERT_OK(conn.Execute("CREATE INDEX t_v21 ON t (v2, v1)"));
  }

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t (key, v1, v2) VALUES ('$0', $1, $2)",
      RandomHumanReadableString(512_KB), RandomUniformInt<int32_t>(), RandomUniformInt<int32_t>()));
  ASSERT_OK(conn.CommitTransaction());

  std::this_thread::sleep_for(5s);
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_OK(conn.Execute("ALTER TABLE t ADD COLUMN v TEXT"));

  for (int i = 0; i != FLAGS_rocksdb_level0_file_num_compaction_trigger * 2; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t (key) VALUES ('$0')", i));
    ASSERT_OK(cluster_->FlushTablets());
  }

  ASSERT_OK(cluster_->CompactTablets());
}

TEST_P(PgPackedRowTest, AppliedSchemaVersion) {
  TestAppliedSchemaVersion(false);
}

TEST_P(PgPackedRowTest, AppliedSchemaVersionWithColocation) {
  TestAppliedSchemaVersion(true);
}

TEST_P(PgPackedRowTest, UpdateToNull) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE test(v1 INT, v2 INT) SPLIT INTO 1 TABLETS"));

  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, 1)"));
  ASSERT_OK(conn.Execute("UPDATE test SET v2 = NULL"));

  auto content = ASSERT_RESULT(conn.FetchRow<std::optional<int32_t>>("SELECT v2 FROM test"));
  ASSERT_EQ(content, std::nullopt);

  ASSERT_OK(cluster_->CompactTablets());

  content = ASSERT_RESULT(conn.FetchRow<std::optional<int32_t>>("SELECT v2 FROM test"));
  ASSERT_EQ(content, std::nullopt);
}

TEST_P(PgPackedRowTest, UpdateToNullWithPK) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, value TEXT) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, value) VALUES (1, 'hello')"));
  ASSERT_OK(conn.Execute("UPDATE t SET value = NULL WHERE key = 1"));

  DumpDocDB(cluster_.get(), ListPeersFilter::kLeaders);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::optional<std::string>>("SELECT value FROM t")),
            std::nullopt);

  ASSERT_OK(cluster_->CompactTablets());

  DumpDocDB(cluster_.get(), ListPeersFilter::kLeaders);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::optional<std::string>>("SELECT value FROM t")),
            std::nullopt);
}

class TestKVFormatter : public tablet::KVFormatter {
 public:
  std::string Format(
      const Slice& key, const Slice& value, docdb::StorageDbType type) const override {
    auto result = tablet::KVFormatter::Format(key, value, type);
    auto b = result.find("HT{");
    auto e = result.find("}", b);
    entries_ += result.substr(0, b + 3);
    entries_ += result.substr(e);
    entries_ += '\n';
    return result;
  }

  const std::string& entries() const {
    return entries_;
  }

 private:
  mutable std::string entries_;
};

void PgPackedRowTest::TestSstDump(bool specify_metadata, std::string* output) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE test(v1 INT PRIMARY KEY, v2 TEXT) SPLIT INTO 1 TABLETS"));

  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, 'one')"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (2, 'two')"));
  ASSERT_OK(conn.Execute("ALTER TABLE test ADD COLUMN v3 TEXT"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (3, 'three', 'tri')"));
  ASSERT_OK(conn.Execute("ALTER TABLE test DROP COLUMN v2"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (4, 'chetyre')"));

  ASSERT_OK(cluster_->FlushTablets());

  std::string fname;
  std::string metapath;
  for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders)) {
    auto tablet = peer->shared_tablet();
    if (!tablet || !tablet->regular_db()) {
      continue;
    }
    for (const auto& file : tablet->regular_db()->GetLiveFilesMetaData()) {
      fname = file.BaseFilePath();
      metapath = ASSERT_RESULT(tablet->metadata()->FilePath());
      LOG(INFO) << "File: " << fname << ", metapath: " << metapath;
    }
  }

  ASSERT_FALSE(fname.empty());

  std::vector<std::string> args = {
    "./sst_dump",
    Format("--file=$0", fname),
    "--output_format=decoded_regulardb",
    "--command=scan",
  };

  if (specify_metadata) {
    args.push_back(Format("--formatter_tablet_metadata=$0", metapath));
  }

  std::vector<char*> usage;
  for (auto& arg : args) {
    usage.push_back(arg.data());
  }

  TestKVFormatter formatter;
  rocksdb::SSTDumpTool tool(&formatter);
  ASSERT_FALSE(tool.Run(narrow_cast<int>(usage.size()), usage.data()));

  *output = formatter.entries();
}

TEST_P(PgPackedRowTest, SstDump) {
  std::string output;
  ASSERT_NO_FATALS(TestSstDump(true, &output));

  constexpr auto kV1 = kFirstColumnIdRep + 1;
  constexpr auto kV2 = kV1 + 1;

  ASSERT_STR_EQ_VERBOSE_TRIMMED(util::ApplyEagerLineContinuation(
      Format(R"#(
          SubDocKey(DocKey(0x1210, [1], []), [HT{}]) -> { $0: "one" }
          SubDocKey(DocKey(0x9eaf, [4], []), [HT{}]) -> { $1: "chetyre" }
          SubDocKey(DocKey(0xc0c4, [2], []), [HT{}]) -> { $0: "two" }
          SubDocKey(DocKey(0xfca0, [3], []), [HT{}]) -> { $0: "three" $1: "tri" }
      )#", kV1, kV2)),
      output);
}

TEST_P(PgPackedRowTest, SstDumpNoMetadata) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_dcheck_for_missing_schema_packing) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_use_packed_row_v2) = false;

  std::string output;
  ASSERT_NO_FATALS(TestSstDump(false, &output));

  ASSERT_STR_EQ_VERBOSE_TRIMMED(util::ApplyEagerLineContinuation(
      R"#(
          SubDocKey(DocKey(0x1210, [1], []), [HT{}]) -> PACKED_ROW[0](04000000536F6E65)
          SubDocKey(DocKey(0x9eaf, [4], []), [HT{}]) -> PACKED_ROW[2](080000005363686574797265)
          SubDocKey(DocKey(0xc0c4, [2], []), [HT{}]) -> PACKED_ROW[0](040000005374776F)
          SubDocKey(DocKey(0xfca0, [3], []), [HT{}]) -> \
              PACKED_ROW[1](060000000A00000053746872656553747269)
      )#"),
      output);
}

std::string PackedRowVersionToString(
    const testing::TestParamInfo<dockv::PackedRowVersion>& param_info) {
  return AsString(param_info.param);
}

INSTANTIATE_TEST_SUITE_P(
    PackingVersion, PgPackedRowTest, ::testing::ValuesIn(dockv::kPackedRowVersionArray),
    PackedRowVersionToString);

} // namespace pgwrapper
} // namespace yb
