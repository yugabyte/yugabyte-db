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

#include <glog/logging.h>

#include "yb/gutil/map-util.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/metrics.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_Read);
DECLARE_int32(ysql_session_max_batch_size);

namespace yb {
namespace pgwrapper {
namespace {

const std::string kPKTable = "pk_table";
const std::string kFKTable = "fk_table";
const std::string kConstraintName = "fk2pk";

class PgFKeyTest : public PgMiniTestBase {
 protected:
  using DeltaFunctor = std::function<Status()>;

  int NumTabletServers() override {
    return 1;
  }

  Result<uint64_t> ReadRPCCountDelta(const DeltaFunctor& functor) const {
    const auto initial_count = GetReadRPCCount();
    RETURN_NOT_OK(functor());
    return GetReadRPCCount() - initial_count;
  }

 private:
  uint64_t GetReadRPCCount() const {
    const auto metric_map =
        cluster_->mini_tablet_server(0)->server()->metric_entity()->UnsafeMetricsMapForTests();
    return down_cast<Histogram*>(FindOrDie(
        metric_map,
        &METRIC_handler_latency_yb_tserver_TabletServerService_Read).get())->TotalCount();
  }
};

CHECKED_STATUS InsertItems(
    PGConn* conn, const std::string& table, size_t first_item, size_t last_item) {
  return conn->ExecuteFormat(
      "INSERT INTO $0 SELECT s, s FROM generate_series($1, $2) AS s", table, first_item, last_item);
}

struct DefaultTag {};
constexpr static DefaultTag kDefault;

template<class T, T DefValue>
struct Option {
  Option()
      : Option(DefValue) {
  }

  Option(const DefaultTag&) // NOLINT
      : Option() {
  }

  Option(const T& value_) // NOLINT
      : value(value_) {
  }

  operator const T&() const {
    return value;
  }

  const T value;
};

constexpr static const char DEFAULT_FK_TYPE[] = "INT";

struct Options {
  Option<const char*, DEFAULT_FK_TYPE> fk_type;
  Option<bool, false> temp_tables;
  Option<size_t, 100> last_item;
};

CHECKED_STATUS PrepareTables(PGConn* conn, const Options& options = Options()) {
  const char* table_type = options.temp_tables ? "TEMP TABLE" : "TABLE";
  RETURN_NOT_OK(conn->ExecuteFormat(
      "CREATE $0 $1(k INT PRIMARY KEY, v INT) SPLIT INTO 1 TABLETS",
      table_type, kPKTable));
  RETURN_NOT_OK(conn->ExecuteFormat(
      "CREATE $0 $1(k INT PRIMARY KEY, pk $2) SPLIT INTO 1 TABLETS",
      table_type, kFKTable, options.fk_type));
  RETURN_NOT_OK(InsertItems(conn, kPKTable, 1, options.last_item));
  return InsertItems(conn, kFKTable, 1, options.last_item);
}

CHECKED_STATUS AddFKConstraint(PGConn* conn, bool skip_check = false) {
  return conn->ExecuteFormat(
      "ALTER TABLE $0 ADD CONSTRAINT $1 FOREIGN KEY(pk) REFERENCES $2(k)$3",
      kFKTable, kConstraintName, kPKTable, skip_check ? " NOT VALID" : "");
}

CHECKED_STATUS CheckAddFKCorrectness(PGConn* conn, bool temp_tables) {
  const size_t pk_fk_item_delta = 10;
  const auto last_pk_item = FLAGS_ysql_session_max_batch_size - pk_fk_item_delta / 2;
  RETURN_NOT_OK(PrepareTables(
      conn, Options {
                .fk_type = kDefault,
                .temp_tables = temp_tables,
                .last_item = last_pk_item
            }));
  const size_t first_fk_extra_item = last_pk_item + 1;
  // Add items into FK table which are absent in PK table. All attempts to add FK constraint must
  // fail until all corresponding items will be added into PK table.
  RETURN_NOT_OK(InsertItems(
      conn, kFKTable, first_fk_extra_item, first_fk_extra_item + pk_fk_item_delta - 1));
  for (size_t i = 0; i < pk_fk_item_delta; ++i) {
    if (AddFKConstraint(conn).ok()) {
      return STATUS(IllegalState, "AddFKConstraint should fail, but it doesn't");
    }
    const size_t new_pk_item = last_pk_item + 1 + i;
    RETURN_NOT_OK(InsertItems(conn, kPKTable, new_pk_item, new_pk_item));
  }
  return AddFKConstraint(conn);
}

} // namespace

// Test checks the number of RPC in case adding foreign key constraint to non empty table.
TEST_F(PgFKeyTest, YB_DISABLE_TEST_IN_TSAN(AddFKConstraintRPCCount)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareTables(&conn));
  const auto add_fk_rpc_count = ASSERT_RESULT(ReadRPCCountDelta([&conn]() {
    return AddFKConstraint(&conn);
  }));
  ASSERT_EQ(add_fk_rpc_count, 2);
}

// Test checks the number of RPC in case adding foreign key constraint with delayed validation
// to non empty table.
TEST_F(PgFKeyTest,
       YB_DISABLE_TEST_IN_TSAN(AddFKConstraintDelayedValidationRPCCount)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareTables(&conn));
  const auto add_fk_rpc_count = ASSERT_RESULT(ReadRPCCountDelta([&conn]() {
    return AddFKConstraint(&conn, true /* skip_check */);
  }));
  ASSERT_EQ(add_fk_rpc_count, 0);

  /* Note: VALIDATE CONSTRAINT is not yet supported. Uncomment next lines after fixing of #3946
  const auto validate_fk_rpc_count = ASSERT_RESULT(ReadRPCCountDelta([&conn]() {
    return conn.Execute("ALTER TABLE child VALIDATE CONSTRAINT child2parent");
  }));

  ASSERT_EQ(validate_fk_rpc_count, 2);*/

  // Check that VALIDATE CONSTRAINT is not supported
  ASSERT_STR_CONTAINS(
      conn.ExecuteFormat(
          "ALTER TABLE $0 VALIDATE CONSTRAINT $1", kFKTable, kConstraintName).ToString(),
      "not supported yet");
}

// Test checks FK correctness in case of FK check requires type casting.
// In this case RPC optimization can't be used.
TEST_F(PgFKeyTest, YB_DISABLE_TEST_IN_TSAN(AddFKConstraintWithTypeCast)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareTables(
      &conn, Options {
                .fk_type = "BIGINT",
                .temp_tables = kDefault,
                .last_item = 20
             }));
  ASSERT_OK(InsertItems(&conn, kFKTable, 21, 21));
  ASSERT_NOK(AddFKConstraint(&conn));
  ASSERT_OK(InsertItems(&conn, kPKTable, 21, 21));
  const auto add_fk_rpc_count = ASSERT_RESULT(ReadRPCCountDelta([&conn]() {
    return AddFKConstraint(&conn);
  }));
  ASSERT_EQ(add_fk_rpc_count, 43);
}

// Test checks FK check correctness with respect to internal buffering
TEST_F(PgFKeyTest, YB_DISABLE_TEST_IN_TSAN(AddFKCorrectness)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CheckAddFKCorrectness(&conn, false /* temp_tables */));
}

// Test checks FK check correctness on temp tables (no optimizations is used in this case)
TEST_F(PgFKeyTest, YB_DISABLE_TEST_IN_TSAN(AddFKCorrectnessOnTempTables)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CheckAddFKCorrectness(&conn, true /* temp_tables */));
}

} // namespace pgwrapper
} // namespace yb
