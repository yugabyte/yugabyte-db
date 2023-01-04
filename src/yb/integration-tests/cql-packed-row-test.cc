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

#include "yb/integration-tests/cql_test_base.h"
#include "yb/integration-tests/packed_row_test_base.h"

using namespace std::literals;

namespace yb {

class CqlPackedRowTest : public PackedRowTestBase<CqlTestBase<MiniCluster>> {
 public:
  virtual ~CqlPackedRowTest() = default;
};

TEST_F(CqlPackedRowTest, Simple) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));

  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT) WITH tablets = 1"));
  ASSERT_OK(session.ExecuteQuery("INSERT INTO t (key, v1, v2) VALUES (1, 'one', 'two')"));

  auto value = ASSERT_RESULT(session.ExecuteAndRenderToString(
      "SELECT v1, v2 FROM t WHERE key = 1"));
  ASSERT_EQ(value, "one,two");

  ASSERT_OK(session.ExecuteQuery("UPDATE t SET v2 = 'three' where key = 1"));
  value = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT v1, v2 FROM t WHERE key = 1"));
  ASSERT_EQ(value, "one,three");

  ASSERT_OK(session.ExecuteQuery("DELETE FROM t WHERE key = 1"));
  value = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT * FROM t"));
  ASSERT_EQ(value, "");

  ASSERT_OK(session.ExecuteQuery("INSERT INTO t (key, v2, v1) VALUES (1, 'four', 'five')"));
  value = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT v1, v2 FROM t WHERE key = 1"));
  ASSERT_EQ(value, "five,four");

  ASSERT_NO_FATALS(CheckNumRecords(cluster_.get(), 4));
}

TEST_F(CqlPackedRowTest, Collections) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));

  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 LIST<TEXT>, v3 TEXT) WITH tablets = 1"));
  ASSERT_OK(session.ExecuteQuery(
      "INSERT INTO t (key, v1, v2, v3) VALUES (1, 'one', ['two', 'three'], 'four')"));

  auto value = ASSERT_RESULT(session.ExecuteAndRenderToString(
      "SELECT v1, v2, v3 FROM t WHERE key = 1"));
  ASSERT_EQ(value, "one,[two, three],four");

  ASSERT_OK(session.ExecuteQuery("UPDATE t SET v2 = v2 + ['five'] where key = 1"));
  value = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT v1, v2, v3 FROM t WHERE key = 1"));
  ASSERT_EQ(value, "one,[two, three, five],four");

  ASSERT_OK(session.ExecuteQuery("DELETE FROM t WHERE key = 1"));
  value = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT * FROM t"));
  ASSERT_EQ(value, "");

  ASSERT_OK(session.ExecuteQuery(
      "INSERT INTO t (key, v3, v2) VALUES (1, 'six', ['seven', 'eight'])"));
  value = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT v1, v2, v3 FROM t WHERE key = 1"));
  ASSERT_EQ(value, "NULL,[seven, eight],six");

  ASSERT_OK(session.ExecuteQuery("UPDATE t SET v1 = 'nine' where key = 1"));
  value = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT v1, v2, v3 FROM t WHERE key = 1"));
  ASSERT_EQ(value, "nine,[seven, eight],six");

  ASSERT_OK(cluster_->CompactTablets());

  value = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT v1, v2, v3 FROM t WHERE key = 1"));
  ASSERT_EQ(value, "nine,[seven, eight],six");

  ASSERT_NO_FATALS(CheckNumRecords(cluster_.get(), 4));
}

TEST_F(CqlPackedRowTest, TTL) {
  constexpr auto kSmallTtl = 1 * kTimeMultiplier;
  constexpr auto kBigTtl = kSmallTtl * 2;
  constexpr auto kTimeout = 1s * kSmallTtl + 50ms * kTimeMultiplier;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));

  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t (key INT PRIMARY KEY, value TEXT) WITH tablets = 1"));
  ASSERT_OK(session.ExecuteQueryFormat(
      "INSERT INTO t (key, value) VALUES (1, 'one') USING TTL $0", kSmallTtl));

  std::this_thread::sleep_for(kTimeout);

  auto value = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT * FROM t"));
  ASSERT_EQ(value, "");

  ASSERT_OK(session.ExecuteQueryFormat(
      "INSERT INTO t (key, value) VALUES (2, 'two') USING TTL $0", kBigTtl));

  ASSERT_OK(session.ExecuteQueryFormat(
      "UPDATE t USING TTL $0 SET value = 'dva' WHERE key = 2", kSmallTtl));

  auto deadline = std::chrono::steady_clock::now() + kTimeout;

  ASSERT_OK(cluster_->CompactTablets());

  std::this_thread::sleep_until(deadline);

  value = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT * FROM t"));
  ASSERT_EQ(value, "2,NULL");

  std::this_thread::sleep_for(kTimeout);

  value = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT * FROM t"));
  ASSERT_EQ(value, "");

  ASSERT_OK(session.ExecuteQueryFormat(
      "INSERT INTO t (key, value) VALUES (3, 'three') USING TTL $0", kSmallTtl));

  ASSERT_OK(session.ExecuteQueryFormat(
      "UPDATE t USING TTL $0 SET value = 'tri' WHERE key = 3", kBigTtl));

  std::this_thread::sleep_for(kTimeout);

  value = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT * FROM t"));
  ASSERT_EQ(value, "3,tri");
}

TEST_F(CqlPackedRowTest, WriteTime) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));

  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT) WITH tablets = 1"));
  ASSERT_OK(session.ExecuteQuery("INSERT INTO t (key, v1, v2) VALUES (1, 'one', 'odin')"));

  int64_t v1time, v2time;
  auto processor = [&v1time, &v2time](const CassandraRow& row) {
    row.Get(0, &v1time);
    row.Get(1, &v2time);
  };

  ASSERT_OK(session.ExecuteAndProcessOneRow(
      "SELECT writetime(v1), writetime(v2) FROM t", processor));
  ASSERT_EQ(v1time, v2time);

  ASSERT_OK(session.ExecuteQueryFormat("UPDATE t SET v2 = 'dva' WHERE key = 1"));

  ASSERT_OK(session.ExecuteAndProcessOneRow(
      "SELECT writetime(v1), writetime(v2) FROM t", processor));
  ASSERT_LT(v1time, v2time);
  auto old_v1time = v1time;
  auto old_v2time = v2time;

  ASSERT_OK(cluster_->CompactTablets());

  ASSERT_OK(session.ExecuteAndProcessOneRow(
      "SELECT writetime(v1), writetime(v2) FROM t", processor));
  ASSERT_EQ(old_v1time, v1time);
  ASSERT_EQ(old_v2time, v2time);
}

} // namespace yb
