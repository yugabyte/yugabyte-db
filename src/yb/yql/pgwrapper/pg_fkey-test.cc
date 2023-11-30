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

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/protobuf_util.h"
#include "yb/util/metrics.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_Read);
METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_Write);
METRIC_DECLARE_histogram(handler_latency_yb_tserver_PgClientService_Perform);
DECLARE_uint64(ysql_session_max_batch_size);
DECLARE_bool(TEST_ysql_ignore_add_fk_reference);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(enable_wait_queues);
DECLARE_string(ysql_pg_conf_csv);

namespace yb::pgwrapper {
namespace {

const std::string kPKTable = "pk_table";
const std::string kFKTable = "fk_table";
const std::string kConstraintName = "fk2pk";

struct RpcCountMetric {
  size_t read;
  size_t write;
  size_t perform;
};

struct RpcCountMetricDescriber : public MetricWatcherDeltaDescriberTraits<RpcCountMetric, 3> {
  explicit RpcCountMetricDescriber(std::reference_wrapper<const MetricEntity::MetricMap> map)
      : descriptors{
          Descriptor{
              &delta.read, map, METRIC_handler_latency_yb_tserver_TabletServerService_Read},
          Descriptor{
              &delta.write, map, METRIC_handler_latency_yb_tserver_TabletServerService_Write},
          Descriptor{
              &delta.perform, map, METRIC_handler_latency_yb_tserver_PgClientService_Perform}}
  {}

  DeltaType delta;
  Descriptors descriptors;
};

class PgFKeyTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    FLAGS_enable_automatic_tablet_splitting = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) = MaxQueryLayerRetriesConf(0);
    PgMiniTestBase::SetUp();
    rpc_count_.emplace(GetMetricMap(*cluster_->mini_tablet_server(0)->server()));
  }

  size_t NumTabletServers() override {
    return 1;
  }

  std::optional<MetricWatcher<RpcCountMetricDescriber>> rpc_count_;
};

Status InsertItems(
    PGConn* conn, const std::string_view& table, size_t first_item, size_t last_item) {
  return conn->ExecuteFormat(
      "INSERT INTO $0 SELECT s, s FROM generate_series($1, $2) AS s", table, first_item, last_item);
}

struct Options {
  std::string fk_type = "INT";
  bool temp_tables = false;
  size_t last_item = 100;
};

Status CreateTables(PGConn* conn, bool temp_tables = false, std::string_view fk_type = "INT") {
  const char* table_type = temp_tables ? "TEMP TABLE" : "TABLE";
  RETURN_NOT_OK(conn->ExecuteFormat(
      "CREATE $0 $1(k INT PRIMARY KEY, v INT) SPLIT INTO 1 TABLETS",
      table_type, kPKTable));
  return conn->ExecuteFormat(
      "CREATE $0 $1(k INT PRIMARY KEY, pk $2) SPLIT INTO 1 TABLETS",
      table_type, kFKTable, fk_type);
}

Status PrepareTables(PGConn* conn, const Options& options = Options()) {
  RETURN_NOT_OK(CreateTables(conn, options.temp_tables, options.fk_type));
  RETURN_NOT_OK(InsertItems(conn, kPKTable, 1, options.last_item));
  return InsertItems(conn, kFKTable, 1, options.last_item);
}

Status AddFKConstraint(PGConn* conn, bool skip_check = false) {
  return conn->ExecuteFormat(
      "ALTER TABLE $0 ADD CONSTRAINT $1 FOREIGN KEY(pk) REFERENCES $2(k)$3",
      kFKTable, kConstraintName, kPKTable, skip_check ? " NOT VALID" : "");
}

Status CheckAddFKCorrectness(PGConn* conn, bool temp_tables) {
  const size_t pk_fk_item_delta = 10;
  const auto last_pk_item = FLAGS_ysql_session_max_batch_size - pk_fk_item_delta / 2;
  RETURN_NOT_OK(PrepareTables(conn,
                              Options {
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

class PgFKeyTestNoFKCache : public PgFKeyTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_ysql_ignore_add_fk_reference) = true;
    PgFKeyTest::SetUp();
  }
};

Status PrepareTablesForMultipleFKs(PGConn* conn) {
  for (size_t i = 0; i < 3; ++i) {
    RETURN_NOT_OK(conn->ExecuteFormat("CREATE TABLE $0_$1(k INT PRIMARY KEY)", kPKTable, i + 1));
  }
  return conn->ExecuteFormat("CREATE TABLE $0(k INT PRIMARY KEY, "
                             "                pk_1 INT REFERENCES $1_1(k),"
                             "                pk_2 INT REFERENCES $1_2(k),"
                             "                pk_3 INT REFERENCES $1_3(k))",
                             kFKTable,
                             kPKTable);
}

inline size_t NumBatches(size_t operations) {
  return std::ceil(static_cast<double>(operations) / FLAGS_ysql_session_max_batch_size);
}

Status SetNoLimitForFetch(PGConn* conn) {
  RETURN_NOT_OK(conn->Execute("SET yb_fetch_size_limit = 0"));
  return conn->Execute("SET yb_fetch_row_limit = 0");
}

class PgFKeyTestConcurrentModification : public PgFKeyTest,
                                         public testing::WithParamInterface<IsolationLevel> {
 protected:
  struct PGConnWithTxnPriority {
    PGConn conn;
    const bool has_high_priority_txn;
  };

  struct State {
    State(
        PGConnWithTxnPriority&& high_priority_txn_conn_,
        PGConnWithTxnPriority&& low_priority_txn_conn_)
        : high_priority_txn_conn(std::move(high_priority_txn_conn_)),
          low_priority_txn_conn(std::move(low_priority_txn_conn_)) {}

    PGConnWithTxnPriority high_priority_txn_conn;
    PGConnWithTxnPriority low_priority_txn_conn;
  };

  void SetUp() override {
    // This test depends on fail-on-conflict concurrency control to perform its validation.
    // TODO(wait-queues): https://github.com/yugabyte/yugabyte-db/issues/17871
    FLAGS_enable_wait_queues = false;
    PgFKeyTest::SetUp();
    auto aux_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(CreateTables(&aux_conn));
    ASSERT_OK(AddFKConstraint(&aux_conn));
    state_.emplace(ASSERT_RESULT(MakeConnWithPriority(true)),
                   ASSERT_RESULT(MakeConnWithPriority(false)));
    const auto clear_tables_query = Format("TRUNCATE $0, $1", kFKTable, kPKTable);
    // Warm up internal caches.
    for (auto* conn : {&state_->high_priority_txn_conn.conn, &state_->low_priority_txn_conn.conn}) {
      ASSERT_OK(InsertItems(conn, kPKTable, 1, 2));
      ASSERT_OK(InsertItems(conn, kFKTable, 2, 2));
      ASSERT_OK(conn->Execute(kDeletePKQuery));
      ASSERT_OK(aux_conn.Execute(clear_tables_query));
    }
    ASSERT_OK(InsertItems(&aux_conn, kPKTable, 1, kItemsCount));
  }

  void ReferencingBeforeDelete(
      PGConnWithTxnPriority* conn_for_referencing, PGConnWithTxnPriority* conn_for_delete) {
    ASSERT_NE(conn_for_referencing->has_high_priority_txn,
              conn_for_delete->has_high_priority_txn);
    auto& ref_conn = conn_for_referencing->conn;
    SCOPED_TRACE(Format(
        "referencing conn high priority: $0 delete conn high priority: $1 isolation level: $2",
        conn_for_referencing->has_high_priority_txn, conn_for_delete->has_high_priority_txn,
        GetParam()));

    ASSERT_OK(ref_conn.StartTransaction(GetParam()));
    const auto rpc_count = ASSERT_RESULT(rpc_count_->Delta(
        [&ref_conn] { return InsertItems(&ref_conn, kFKTable, 1, kItemsCount); }));
    auto num_batches = NumBatches(kItemsCount);
    ASSERT_EQ(rpc_count.read, num_batches);
    ASSERT_EQ(rpc_count.write, num_batches);
    ASSERT_EQ(rpc_count.perform, rpc_count.read + rpc_count.write - 1);

    const IsolationLevel effective_isolation = ASSERT_RESULT(EffectiveIsolationLevel(&ref_conn));

    auto delete_res = conn_for_delete->conn.Execute(kDeletePKQuery);
    auto ref_res = ref_conn.CommitTransaction();

    // There is no concept of priorities in read committed isolation, all transactions used th same
    // priority which is 1.0 from the high priority bucket.
    if ((effective_isolation == IsolationLevel::READ_COMMITTED) ||
         conn_for_referencing->has_high_priority_txn) {
      ASSERT_OK(ref_res);
      ASSERT_NOK(delete_res);
    } else {
      ASSERT_NOK(ref_res);
      ASSERT_OK(delete_res);
    }
  }

  void DeleteBeforeReferencing(
      PGConnWithTxnPriority* conn_for_delete, PGConnWithTxnPriority* conn_for_referencing) {
    ASSERT_NE(conn_for_referencing->has_high_priority_txn, conn_for_delete->has_high_priority_txn);
    auto& delete_conn = conn_for_delete->conn;
    SCOPED_TRACE(Format(
        "referencing conn high priority: $0 delete conn high priority: $1 isolation level: $2",
        conn_for_referencing->has_high_priority_txn, conn_for_delete->has_high_priority_txn,
        GetParam()));

    ASSERT_OK(delete_conn.StartTransaction(GetParam()));
    const auto rpc_count = ASSERT_RESULT(rpc_count_->Delta(
        [&delete_conn] { return delete_conn.Execute(kDeletePKQuery); }));
    ASSERT_EQ(rpc_count.read, 3);
    ASSERT_EQ(rpc_count.write, 1);
    ASSERT_EQ(rpc_count.perform, rpc_count.read + rpc_count.write);

    const IsolationLevel effective_isolation = ASSERT_RESULT(EffectiveIsolationLevel(&delete_conn));

    auto ref_res = InsertItems(&conn_for_referencing->conn, kFKTable, 1, kItemsCount);
    auto delete_res = delete_conn.CommitTransaction();

    // There is no concept of priorities in read committed isolation, all transactions use the same
    // priority which is 1.0 from the high priority bucket.
    if (conn_for_referencing->has_high_priority_txn &&
        (effective_isolation != IsolationLevel::READ_COMMITTED)) {
      ASSERT_OK(ref_res);
      ASSERT_NOK(delete_res);
    } else {
      ASSERT_NOK(ref_res);
      ASSERT_OK(delete_res);
    }
  }

  Result<PGConnWithTxnPriority> MakeConnWithPriority(bool high_priority_txn) {
    return PGConnWithTxnPriority{
        .conn = VERIFY_RESULT(SetDefaultTransactionIsolation(
            (*(high_priority_txn ? &SetHighPriTxn : &SetLowPriTxn))(Connect()),
            IsolationLevel::SNAPSHOT_ISOLATION)),
        .has_high_priority_txn = high_priority_txn};
  }

  std::optional<State> state_;
  static const std::string kDeletePKQuery;
  static constexpr size_t kItemsCount = 5;
};

PB_ENUM_FORMATTERS(IsolationLevel);

template <typename T>
std::string TestParamToString(const testing::TestParamInfo<T>& param_info) {
  return ToString(param_info.param);
}

INSTANTIATE_TEST_CASE_P(
    PgFKeyTest, PgFKeyTestConcurrentModification,
    testing::Values(IsolationLevel::SNAPSHOT_ISOLATION,
                    IsolationLevel::SERIALIZABLE_ISOLATION,
                    IsolationLevel::READ_COMMITTED),
    TestParamToString<IsolationLevel>);

const std::string PgFKeyTestConcurrentModification::kDeletePKQuery =
    Format("DELETE FROM $0 WHERE k = 1", kPKTable);

} // namespace

// Test checks the number of RPC in case adding foreign key constraint to non empty table.
TEST_F(PgFKeyTest, AddFKConstraintRPCCount) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareTables(&conn));
  const auto add_fk_rpc_count = ASSERT_RESULT(rpc_count_->Delta(
      [&conn] { return AddFKConstraint(&conn); })).read;
  ASSERT_EQ(add_fk_rpc_count, 2);
}

// Test checks the number of RPC in case adding foreign key constraint with delayed validation
// to non empty table.
TEST_F(PgFKeyTest,
       AddFKConstraintDelayedValidationRPCCount) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareTables(&conn));
  const auto add_fk_rpc_count = ASSERT_RESULT(rpc_count_->Delta(
      [&conn] { return AddFKConstraint(&conn, true /* skip_check */); })).read;
  ASSERT_EQ(add_fk_rpc_count, 0);

  /* Note: VALIDATE CONSTRAINT is not yet supported. Uncomment next lines after fixing of #3946
  const auto validate_fk_rpc_count = ASSERT_RESULT(rpc_count_->Delta(
      [&conn] { return conn.Execute("ALTER TABLE child VALIDATE CONSTRAINT child2parent"); })).read;

  ASSERT_EQ(validate_fk_rpc_count, 2);*/

  // Check that VALIDATE CONSTRAINT is not supported
  ASSERT_STR_CONTAINS(
      conn.ExecuteFormat(
          "ALTER TABLE $0 VALIDATE CONSTRAINT $1", kFKTable, kConstraintName).ToString(),
      "not supported yet");
}

// Test checks FK correctness in case of FK check requires type casting.
// In this case RPC optimization can't be used.
TEST_F(PgFKeyTest, AddFKConstraintWithTypeCast) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareTables(&conn,
                          Options {
                              .fk_type = "BIGINT",
                              .last_item = 20
                          }));
  ASSERT_OK(InsertItems(&conn, kFKTable, 21, 21));
  ASSERT_NOK(AddFKConstraint(&conn));
  ASSERT_OK(InsertItems(&conn, kPKTable, 21, 21));
  const auto add_fk_rpc_count = ASSERT_RESULT(rpc_count_->Delta(
      [&conn] { return AddFKConstraint(&conn); })).read;
  ASSERT_EQ(add_fk_rpc_count, 43);
}

// Test checks FK check correctness with respect to internal buffering
TEST_F(PgFKeyTest, AddFKCorrectness) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CheckAddFKCorrectness(&conn, false /* temp_tables */));
}

// Test checks FK check correctness on temp tables (no optimizations is used in this case)
TEST_F(PgFKeyTest, AddFKCorrectnessOnTempTables) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CheckAddFKCorrectness(&conn, true /* temp_tables */));
}

// Test checks the number of RPC in case of multiple FK on same table.
TEST_F(PgFKeyTest, MultipleFKConstraintRPCCount) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareTablesForMultipleFKs(&conn));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0_1 VALUES(11), (12), (13);"
                               "INSERT INTO $0_2 VALUES(21), (22), (23);"
                               "INSERT INTO $0_3 VALUES(31), (32), (33);", kPKTable));
  // Warmup catalog cache to load info related for triggers before estimating RPC count.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES(1, 11, 21, 31)", kFKTable));
  ASSERT_OK(conn.ExecuteFormat("TRUNCATE $0", kFKTable));
  ASSERT_OK(conn.FetchFormat("SELECT * FROM $0", kFKTable));
  const auto insert_fk_rpc_count = ASSERT_RESULT(rpc_count_->Delta([&conn] {
    return conn.ExecuteFormat(
      "INSERT INTO $0 VALUES(1, 11, 21, 31), (2, 12, 22, 32), (3, 13, 23, 33)", kFKTable);
  })).perform;
  ASSERT_EQ(insert_fk_rpc_count, 1);
}

// Test checks that insertion into table with large number of foreign keys doesn't fail.
TEST_F(PgFKeyTest, InsertWithLargeNumberOfFK) {
  auto conn = ASSERT_RESULT(Connect());
  constexpr size_t fk_count = 3;
  constexpr size_t insert_count = 100;
  const auto parent_table_prefix = kPKTable + "_";
  std::string fk_keys, insert_fk_columns;
  fk_keys.reserve(255);
  insert_fk_columns.reserve(255);
  for (size_t i = 1; i <= fk_count; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0$1(k INT PRIMARY KEY)", parent_table_prefix, i));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0$1 SELECT s FROM generate_series(1, $2) AS s",
        parent_table_prefix, i, insert_count));
    if (!fk_keys.empty()) {
      fk_keys += ", ";
      insert_fk_columns += ", ";
    }
    fk_keys += Format("fk_$0 INT REFERENCES $1$0(k)", i, parent_table_prefix);
    insert_fk_columns += "s";
  }

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0(k INT PRIMARY KEY, $1)", kFKTable, fk_keys));
  for (size_t i = 0; i < 50; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 SELECT $1 + s, $2 FROM generate_series(1, $3) AS s",
      kFKTable, i * insert_count, insert_fk_columns, insert_count));
  }
}

// Test checks number of read/write/perform rpcs in case of inserts into table with FK trigger.
TEST_F(PgFKeyTest, YB_DISABLE_TEST_IN_TSAN(InsertBatching)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTables(&conn));
  ASSERT_OK(AddFKConstraint(&conn));
  constexpr size_t kFKItemCount = 10000;
  constexpr size_t kPKItemCountDivider = 2;
  constexpr size_t kPKItemCount = kFKItemCount / kPKItemCountDivider;
  ASSERT_OK(InsertItems(&conn, kPKTable, 0, kPKItemCount));
  const auto query_template = Format(
      "INSERT INTO $0 SELECT s, s / $1 FROM generate_series($$0, $$1) AS s",
      kFKTable, kPKItemCountDivider);
  // Warm up internal caches.
  ASSERT_OK(conn.ExecuteFormat(query_template, 0, 0));
  const auto rpc_count = ASSERT_RESULT(rpc_count_->Delta([&conn, &query_template, kFKItemCount] {
    return conn.ExecuteFormat(query_template, 1, kFKItemCount);
  }));
  ASSERT_EQ(rpc_count.read, NumBatches(kPKItemCount));
  ASSERT_EQ(rpc_count.write, NumBatches(kFKItemCount));
  ASSERT_EQ(rpc_count.perform, rpc_count.read + rpc_count.write - 1);
}

// Test checks number of read/write/perform rpcs in case of inserts into table
// with deferred FK trigger.
TEST_F(PgFKeyTest, YB_DISABLE_TEST_IN_TSAN(InsertBatchingDeferredTrigger)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTables(&conn));
  constexpr size_t kItemCount = 10000;
  ASSERT_OK(InsertItems(&conn, kPKTable, 0, kItemCount));
  constexpr size_t kFKTableCount = 3;
  for (size_t i = 0; i < kFKTableCount; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0_$1(k INT, pk INT REFERENCES $2(k) " \
        "DEFERRABLE INITIALLY DEFERRED) SPLIT INTO 1 TABLETS", kFKTable, i, kPKTable));
    // Warm up internal caches.
    ASSERT_OK(InsertItems(&conn, Format("$0_$1", kFKTable, i), 0, 0));
  }
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  auto rpc_count = ASSERT_RESULT(rpc_count_->Delta([&conn] {
    for (size_t i = 0; i < kFKTableCount; ++i) {
      RETURN_NOT_OK(InsertItems(&conn, Format("$0_$1", kFKTable, i), 1, kItemCount));
    }
    return conn.CommitTransaction();
  }));
  const auto num_batches = NumBatches(kItemCount);
  // Read for trigger must be called once due to internal FK cache.
  ASSERT_EQ(rpc_count.read, num_batches);
  ASSERT_EQ(rpc_count.write, num_batches * kFKTableCount);
  ASSERT_EQ(rpc_count.perform, rpc_count.read + rpc_count.write);
}

// Test checks number of read/write/perform rpcs in case of update table with FK trigger.
TEST_F(PgFKeyTest, YB_DISABLE_TEST_IN_TSAN(UpdateBatching)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(SetNoLimitForFetch(&conn));
  ASSERT_OK(CreateTables(&conn));
  ASSERT_OK(AddFKConstraint(&conn));
  constexpr size_t kItemCount = 10000;
  ASSERT_OK(InsertItems(&conn, kPKTable, 0, kItemCount));
  ASSERT_OK(InsertItems(&conn, kFKTable, 0, kItemCount));
  const auto query_template = Format("UPDATE $0 SET pk = pk + 1 WHERE k < $$0", kFKTable);
  // Warm up internal caches.
  ASSERT_OK(conn.ExecuteFormat(query_template, 1));
  const auto rpc_count = ASSERT_RESULT(rpc_count_->Delta(
      [&conn, &query_template] { return conn.ExecuteFormat(query_template, kItemCount - 1); }));
  const auto num_batches = NumBatches(kItemCount - 1);
  ASSERT_EQ(rpc_count.read, num_batches + 1);
  ASSERT_EQ(rpc_count.write, num_batches);
  ASSERT_EQ(rpc_count.perform, rpc_count.read + rpc_count.write - 1);
}

// Test checks rows written by buffered write operations are read successfully while
// performing FK constraint check.
TEST_F_EX(PgFKeyTest, BufferedWriteOfReferencedRows, PgFKeyTestNoFKCache) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareTablesForMultipleFKs(&conn));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE PROCEDURE test(start_idx INT, end_idx INT) LANGUAGE plpgsql AS $$$$ "
      "BEGIN"
      "  INSERT INTO $0_1 SELECT s FROM generate_series(start_idx, end_idx) AS s;"
      "  INSERT INTO $0_2 SELECT s FROM generate_series(start_idx, end_idx) AS s;"
      "  INSERT INTO $0_3 SELECT s FROM generate_series(start_idx, end_idx) AS s;"
      "  INSERT INTO $1 SELECT s, s, s, s FROM generate_series(start_idx, end_idx) AS s;"
      "END;$$$$",
      kPKTable, kFKTable));
  for (size_t i = 0; i < 10; ++i) {
    const size_t start_idx = i * 1000 + 1;
    const size_t end_idx = start_idx + 100;
    ASSERT_OK(conn.ExecuteFormat("CALL test($0, $1)", start_idx, end_idx));
  }
}

// Test checks that batching of FK check doesn't break transaction conflict detection
// in scenario when referenced rows get deleted (low priority txn)
// after being referenced (high priority txn).
TEST_P(PgFKeyTestConcurrentModification, HighPriorityReferencingBeforeLowPriorityDelete) {
  ReferencingBeforeDelete(&state_->high_priority_txn_conn, &state_->low_priority_txn_conn);
}

// Test checks that batching of FK check doesn't break transaction conflict detection
// in scenario when referenced rows get deleted (high priority txn)
// after being referenced (low priority txn).
TEST_P(PgFKeyTestConcurrentModification, LowPriorityReferencingBeforeHighPriorityDelete) {
  ReferencingBeforeDelete(&state_->low_priority_txn_conn, &state_->high_priority_txn_conn);
}

// Test checks that batching of FK check doesn't break transaction conflict detection
// in scenario when rows get referenced (low priority txn) after being deleted (high priority txn).
TEST_P(PgFKeyTestConcurrentModification, HighPriorityDeleteBeforeLowPriorityReferencing) {
  DeleteBeforeReferencing(&state_->high_priority_txn_conn, &state_->low_priority_txn_conn);
}

// Test checks that batching of FK check doesn't break transaction conflict detection
// in scenario when rows get referenced (high priority txn) after being deleted (low priority txn).
TEST_P(PgFKeyTestConcurrentModification, LowPriorityDeleteBeforeHighPriorityReferencing) {
  DeleteBeforeReferencing(&state_->low_priority_txn_conn, &state_->high_priority_txn_conn);
}

} // namespace yb::pgwrapper
