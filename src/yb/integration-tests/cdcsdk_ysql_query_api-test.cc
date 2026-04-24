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

#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_state_table.h"
#include "yb/integration-tests/cdcsdk_ysql_test_base.h"
#include "yb/util/test_macros.h"

namespace yb {
namespace cdc {

class CDCSDKYsqlQueryApiTest : public CDCSDKYsqlTest {
 public:
  void SetUp() override {
    CDCSDKYsqlTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_slot_consumption) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_slot_query_api) = true;
  }
};

// Verify that the GUC yb_enable_replication_slot_query_api gates all 4 SQL functions.
TEST_F(CDCSDKYsqlQueryApiTest, TestQueryApiGucGuard) {
  ASSERT_OK(SetUpWithParams(3, 1, false, true));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("CREATE TABLE test_table (id int PRIMARY KEY, name text)"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR ALL TABLES"));
  ASSERT_OK(conn.Fetch(
      "SELECT * FROM pg_create_logical_replication_slot('test_slot', 'test_decoding')"));

  ASSERT_OK(conn.Execute("SET yb_enable_replication_slot_query_api = false"));

  auto assert_query_api_unavailable = [&](const std::string& query) {
    auto result = conn.Fetch(query);
    ASSERT_NOK(result);
    ASSERT_NE(
        result.status().message().AsStringView().find(
            "getting logical slot changes is unavailable"),
        std::string::npos)
        << result.status().message();
  };

  assert_query_api_unavailable(
      "SELECT * FROM pg_logical_slot_get_changes('test_slot', NULL, NULL)");
  assert_query_api_unavailable(
      "SELECT * FROM pg_logical_slot_peek_changes('test_slot', NULL, NULL)");
  assert_query_api_unavailable(
      "SELECT * FROM pg_logical_slot_get_binary_changes('test_slot', NULL, NULL)");
  assert_query_api_unavailable(
      "SELECT * FROM pg_logical_slot_peek_binary_changes('test_slot', NULL, NULL)");
}

// Verify peek vs get semantics with test_decoding (text output).
TEST_F(CDCSDKYsqlQueryApiTest, TestTextPeekAndGet) {
  ASSERT_OK(SetUpWithParams(3, 1, false, true));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("CREATE TABLE test_table (id int PRIMARY KEY, name text)"));
  ASSERT_OK(conn.Fetch(
      "SELECT * FROM pg_create_logical_replication_slot('text_slot', 'test_decoding')"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'hello')"));

  // Peek returns rows: test_decoding emits BEGIN + INSERT + COMMIT = 3 rows.
  auto peek1 = ASSERT_RESULT(conn.Fetch(
      "SELECT * FROM pg_logical_slot_peek_changes('text_slot', NULL, NULL)"));
  auto peek1_rows = PQntuples(peek1.get());
  ASSERT_EQ(peek1_rows, 3);

  // Peek again -- same rows (no advancement).
  auto peek2 = ASSERT_RESULT(conn.Fetch(
      "SELECT * FROM pg_logical_slot_peek_changes('text_slot', NULL, NULL)"));
  ASSERT_EQ(PQntuples(peek2.get()), peek1_rows);

  // Get consumes and advances.
  auto get1 = ASSERT_RESULT(conn.Fetch(
      "SELECT * FROM pg_logical_slot_get_changes('text_slot', NULL, NULL)"));
  ASSERT_EQ(PQntuples(get1.get()), 3);

  // Get again -- should be empty since checkpoint advanced.
  auto get2 = ASSERT_RESULT(conn.Fetch(
      "SELECT * FROM pg_logical_slot_get_changes('text_slot', NULL, NULL)"));
  ASSERT_EQ(PQntuples(get2.get()), 0);
}

// Verify binary peek/get with pgoutput plugin.
TEST_F(CDCSDKYsqlQueryApiTest, TestBinaryPeekAndGet) {
  ASSERT_OK(SetUpWithParams(3, 1, false, true));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("CREATE TABLE test_table (id int PRIMARY KEY, name text)"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table REPLICA IDENTITY FULL"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR ALL TABLES"));
  ASSERT_OK(conn.Fetch(
      "SELECT * FROM pg_create_logical_replication_slot('bin_slot', 'pgoutput', false)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'hello')"));

  // Peek binary returns rows with bytea data.
  // pgoutput binary message count can vary (relation messages, type messages, etc.),
  // so we only assert > 0 here instead of an exact count.
  auto peek = ASSERT_RESULT(conn.Fetch(
      "SELECT * FROM pg_logical_slot_peek_binary_changes("
      "'bin_slot', NULL, NULL, 'proto_version', '1', 'publication_names', 'pub')"));
  auto peek_rows = PQntuples(peek.get());
  ASSERT_GT(peek_rows, 0);

  // Get binary consumes -- should return same count as peek.
  auto get1 = ASSERT_RESULT(conn.Fetch(
      "SELECT * FROM pg_logical_slot_get_binary_changes("
      "'bin_slot', NULL, NULL, 'proto_version', '1', 'publication_names', 'pub')"));
  ASSERT_EQ(PQntuples(get1.get()), peek_rows);

  // Get binary again -- empty after advancement.
  auto get2 = ASSERT_RESULT(conn.Fetch(
      "SELECT * FROM pg_logical_slot_get_binary_changes("
      "'bin_slot', NULL, NULL, 'proto_version', '1', 'publication_names', 'pub')"));
  ASSERT_EQ(PQntuples(get2.get()), 0);
}

// Verify no-records case returns empty result quickly.
TEST_F(CDCSDKYsqlQueryApiTest, TestNoRecordsReturnsEmpty) {
  ASSERT_OK(SetUpWithParams(3, 1, false, true));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("CREATE TABLE test_table (id int PRIMARY KEY, name text)"));
  ASSERT_OK(conn.Fetch(
      "SELECT * FROM pg_create_logical_replication_slot('empty_slot', 'test_decoding')"));

  // No inserts -- should return 0 rows.
  auto result = ASSERT_RESULT(conn.Fetch(
      "SELECT * FROM pg_logical_slot_get_changes('empty_slot', NULL, NULL)"));
  ASSERT_EQ(PQntuples(result.get()), 0);
}

// Verify upto_nchanges limits the number of returned rows.
TEST_F(CDCSDKYsqlQueryApiTest, TestUptoNchangesLimit) {
  ASSERT_OK(SetUpWithParams(3, 1, false, true));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("CREATE TABLE test_table (id int PRIMARY KEY, name text)"));
  ASSERT_OK(conn.Fetch(
      "SELECT * FROM pg_create_logical_replication_slot('limit_slot', 'test_decoding')"));

  for (int i = 1; i <= 5; i++) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test_table VALUES ($0, 'row$0')", i));
  }

  // Fetch all without limit: 5 auto-commit INSERTs * 3 rows each
  // (BEGIN + INSERT + COMMIT) = 15 rows.
  auto all = ASSERT_RESULT(conn.Fetch(
      "SELECT * FROM pg_logical_slot_peek_changes('limit_slot', NULL, NULL)"));
  auto total_rows = PQntuples(all.get());
  ASSERT_EQ(total_rows, 15);

  // Fetch with upto_nchanges = 2. YB delivers whole transactions atomically, so the
  // actual count may slightly exceed the hint but must be less than the unlimited total.
  auto limited = ASSERT_RESULT(conn.Fetch(
      "SELECT * FROM pg_logical_slot_peek_changes('limit_slot', NULL, 2)"));
  ASSERT_GT(PQntuples(limited.get()), 0);
  ASSERT_LT(PQntuples(limited.get()), total_rows);
}

// Verify pg_logical_slot_get_changes does not leave yb_read_time set in the
// session. After get_changes consumes txn1, a normal SELECT must read at
// current time (seeing txn2's insert), and a follow-up get_changes must still
// be able to pick up txn2 via InitVirtualWal re-establishing yb_read_time.
TEST_F(CDCSDKYsqlQueryApiTest, TestMultiStatementTransaction) {
  ASSERT_OK(SetUpWithParams(3, 1, false, true));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  // Use an extra non-PK column (name) that is NOT touched by the UPDATE. With
  // packed rows enabled, an UPDATE that modifies all non-PK columns is emitted
  // as an INSERT by CDC. Leaving one column untouched forces the UPDATE path.
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_table (id int PRIMARY KEY, val int, name text)"));
  ASSERT_OK(conn.Fetch(
      "SELECT * FROM pg_create_logical_replication_slot('txn_slot', 'test_decoding')"));

  // Txn 1: multi-statement (BEGIN + 2 INSERTs + 1 UPDATE + COMMIT = 5 rows).
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 10, 'a')"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (2, 20, 'b')"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET val = 11 WHERE id = 1"));
  ASSERT_OK(conn.Execute("COMMIT"));

  // Txn 2: single auto-commit insert.
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (3, 3, 'c')"));

  // First get_changes: upto_nchanges=5 consumes exactly txn1 (test_decoding
  // emits 5 rows for txn1: BEGIN + 2 INSERTs + 1 UPDATE + COMMIT).
  auto txn1 = ASSERT_RESULT(conn.FetchRows<std::string>(
      "SELECT data FROM pg_logical_slot_get_changes("
      "'txn_slot', NULL, 5, 'include-xids', '0', 'skip-empty-xacts', '1')"));
  ASSERT_EQ(txn1, (decltype(txn1){
      "BEGIN",
      "table public.test_table: INSERT: id[integer]:1 val[integer]:10 name[text]:'a'",
      "table public.test_table: INSERT: id[integer]:2 val[integer]:20 name[text]:'b'",
      "table public.test_table: UPDATE: id[integer]:1 val[integer]:11 "
          "name[text]:unchanged-toast-datum",
      "COMMIT",
  }));

  // Verify yb_read_time set by get_changes was reset. The SELECT must read at
  // current time (3 rows: txn1's two rows + txn2's INSERT). If yb_read_time
  // wasn't reset, the SELECT would read at the historical time pinned by
  // get_changes (post-txn1, pre-txn2) and return only 2 rows.
  auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t, std::string>(
      "SELECT * FROM test_table ORDER BY id")));
  ASSERT_EQ(rows, (decltype(rows){{1, 11, "a"}, {2, 20, "b"}, {3, 3, "c"}}));

  // Second get_changes: InitVirtualWal re-establishes yb_read_time and pulls txn2.
  auto txn2 = ASSERT_RESULT(conn.FetchRows<std::string>(
      "SELECT data FROM pg_logical_slot_get_changes("
      "'txn_slot', NULL, NULL, 'include-xids', '0', 'skip-empty-xacts', '1')"));
  ASSERT_EQ(txn2, (decltype(txn2){
      "BEGIN",
      "table public.test_table: INSERT: id[integer]:3 val[integer]:3 name[text]:'c'",
      "COMMIT",
  }));
}

}  // namespace cdc
}  // namespace yb
