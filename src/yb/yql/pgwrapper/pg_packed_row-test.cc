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

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_int32(history_cutoff_propagation_interval_ms);
DECLARE_int32(max_packed_row_columns);
DECLARE_int32(timestamp_history_retention_interval_sec);

namespace yb {
namespace pgwrapper {

class PgPackedRowTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    FLAGS_max_packed_row_columns = 10;
    FLAGS_timestamp_history_retention_interval_sec = 0;
    FLAGS_history_cutoff_propagation_interval_ms = 1;
    PgMiniTestBase::SetUp();
  }
};

TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(PackedRow)) {
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

TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(RandomPackedRow)) {
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

TEST_F(PgPackedRowTest, YB_DISABLE_TEST_IN_TSAN(PackedRowSchemaChange)) {
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

} // namespace pgwrapper
} // namespace yb
