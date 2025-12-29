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

#include <string>

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

namespace yb::pgwrapper {

class PgHintTableTest : public LibPqTestBase {
 public:
  // Given an EXPLAIN plan in JSON format, returns the node type
  // of the root plan node. For this test, that's the join type.
  static std::string GetRootNodeType(const rapidjson::Document& plan_json) {
    return std::string(plan_json[0]["Plan"]["Node Type"].GetString());
  }

  static std::string GetJoinType(const std::string& explain_json) {
    rapidjson::Document plan_json;
    plan_json.Parse(explain_json.c_str());
    return GetRootNodeType(plan_json);
  }

  static Result<std::string> ExecuteExplainAndGetJoinType(
      PGConn *conn,
      const std::string& explain_query) {
    auto explain_str = VERIFY_RESULT(conn->FetchRow<std::string>(explain_query));
    return GetJoinType(explain_str);
  }

  // The query ID is determined via a fingerprint of the query's post-rewrite AST.
  // We don't expect this to change between runs--the only way this could happen is if the
  // OID of pg_class or pg_attribute were to change.
  static const std::string query;
  static const std::string explain_query;
  static constexpr int64_t query_id = 3737126150833151274;

  static const std::string query_with_param;
  static constexpr int64_t query_with_param_id = -76397991979668011;

  Result<PGConn> ConnectWithHintTable() {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Execute("CREATE EXTENSION IF NOT EXISTS pg_hint_plan"));
    RETURN_NOT_OK(conn.Execute("SET pg_hint_plan.enable_hint_table TO on"));
    RETURN_NOT_OK(conn.Execute("SET pg_hint_plan.yb_use_query_id_for_hinting TO on"));
    return conn;
  }

  Result<std::pair<PGConn, PGConn>> InsertHintsAndRunQueries() {
    // ------------------------------------------------------------------------------------------
    // 1. Setup connections
    // ------------------------------------------------------------------------------------------
    auto conn_query = VERIFY_RESULT(ConnectWithHintTable());
    auto conn_hint = VERIFY_RESULT(ConnectWithHintTable());

    // ------------------------------------------------------------------------------------------
    // 2. Insert hints and run queries to force hint table lookups
    // ------------------------------------------------------------------------------------------
    const int num_queries = 100;
    for (int i = 0; i < num_queries; i++) {
      std::string whitespace(i * 1000, ' ');
      auto hint_value = Format("YbBatchedNL(pg_class $0 pg_attribute)", whitespace);

      RETURN_NOT_OK(conn_hint.ExecuteFormat(
          "INSERT INTO hint_plan.hints (norm_query_string, application_name, hints) "
          "VALUES ('$0', '', '$1')",
          query_id + i, hint_value));

      // Execute the query to force hint cache lookups and refreshes
      auto join_type = VERIFY_RESULT(
          ExecuteExplainAndGetJoinType(&conn_query, "EXPLAIN (ANALYZE, FORMAT JSON) " + query));
      SCHECK_EQ(std::string("YB Batched Nested Loop"), join_type, IllegalState,
                Format("Unexpected join type: %s", join_type));
    }
    LOG(INFO) << "Completed " << num_queries << " queries";
    return std::make_pair(std::move(conn_query), std::move(conn_hint));
  }

  bool IsTransactionalDdlEnabled(pgwrapper::PGConn& conn) const {
    auto txn_ddl_enabled = conn.FetchRow<std::string>("SHOW yb_ddl_transaction_block_enabled;");
    return txn_ddl_enabled.ok() && *txn_ddl_enabled == "on";
  }
};

class PgHintTableTestWithoutHintCache : public PgHintTableTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgHintTableTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back(
        "--ysql_pg_conf_csv=pg_hint_plan.yb_enable_hint_table_cache=off");
  }
};

const std::string PgHintTableTest::query =
  "SELECT * FROM pg_class JOIN pg_attribute ON pg_class.oid = attrelid";
const std::string PgHintTableTest::explain_query = "EXPLAIN (FORMAT JSON) " + query;
const std::string PgHintTableTest::query_with_param =
    "SELECT * FROM pg_class JOIN pg_attribute ON pg_class.oid = attrelid WHERE oid > $1";

TEST_F(PgHintTableTest, ForceBatchedNestedLoop) {
  // ----------------------------------------------------------------------------------------------
  // 1. Start up 2 connections, create extension pg_hint_plan, and set the necessary GUCs
  // ----------------------------------------------------------------------------------------------
  auto conn1 = ASSERT_RESULT(ConnectWithHintTable());
  auto conn2 = ASSERT_RESULT(ConnectWithHintTable());

  // ----------------------------------------------------------------------------------------------
  // 2. On the first connection, run the query and verify it uses a Nested Loop join
  // ----------------------------------------------------------------------------------------------
  const std::string before_plan_str =
      ASSERT_RESULT(conn1.FetchRow<std::string>("EXPLAIN (HINTS, VERBOSE, FORMAT JSON) " + query));
  LOG(INFO) << "Plan before hint str: " << before_plan_str;
  rapidjson::Document before_plan_json;
  before_plan_json.Parse(before_plan_str.c_str());
  ASSERT_STR_EQ("Hash Join", GetRootNodeType(before_plan_json));

  // ----------------------------------------------------------------------------------------------
  // 3. On the second connection, insert a hint to force a Hash Join
  // ----------------------------------------------------------------------------------------------
  // Extract the query ID from the JSON plan
  auto plan_query_id = before_plan_json[0]["Query Identifier"].GetInt64();
  LOG(INFO) << "Query ID from plan: " << plan_query_id;
  ASSERT_EQ(query_id, plan_query_id);

  // Insert the hint entry to force a NestedLoop
  ASSERT_OK(conn2.Execute(Format(
      "INSERT INTO hint_plan.hints (norm_query_string, application_name, hints) VALUES ('$0', '', "
      "'YbBatchedNL(pg_class pg_attribute)')",
      query_id)));

  // Wait for the heartbeat to propagate the invalidation messages for the hint table
  WaitForCatalogVersionToPropagate();

  // ----------------------------------------------------------------------------------------------
  // 4. Re-run the query on the first connection and verify it uses a Batched Nested Loop now
  // ----------------------------------------------------------------------------------------------
  ASSERT_STR_EQ("YB Batched Nested Loop",
    ASSERT_RESULT(ExecuteExplainAndGetJoinType(&conn1, explain_query)));

  // ----------------------------------------------------------------------------------------------
  // 5. Delete the hint from the hint table
  // ----------------------------------------------------------------------------------------------
  ASSERT_OK(conn2.Execute(
      Format("DELETE FROM hint_plan.hints WHERE norm_query_string = '$0'", query_id)));

  // Wait for the heartbeat to propagate the invalidation messages for the hint table
  WaitForCatalogVersionToPropagate();

  // ----------------------------------------------------------------------------------------------
  // 6. Re-run the query on the first connection and verify it's back to the original plan
  // ----------------------------------------------------------------------------------------------
  ASSERT_STR_EQ("Hash Join", ASSERT_RESULT(ExecuteExplainAndGetJoinType(&conn1, explain_query)));
}

TEST_F(PgHintTableTest, SimpleConcurrencyTest) {
  auto conn_explain = ASSERT_RESULT(ConnectWithHintTable());
  auto conn_hint1 = ASSERT_RESULT(ConnectWithHintTable());
  auto conn_hint2 = ASSERT_RESULT(ConnectWithHintTable());

  std::atomic<bool> stop_threads{false};
  std::atomic<int> iterations{0};
  std::vector<std::string> observed_join_types;
  std::mutex join_types_mutex;

  // Thread that sets the hint to batched nested loop
  TestThreadHolder threads;

  // Helper lambda to add a thread that sets a specific hint
  auto add_hint_thread = [&](auto& conn_hint, const std::string& hint) {
    threads.AddThreadFunctor([&stop_threads, &conn_hint, hint]() {
      while (!stop_threads) {
        ASSERT_OK(conn_hint.Execute(Format(
            "INSERT INTO hint_plan.hints (norm_query_string, application_name, hints) "
            "VALUES ('$0', '', '$1') "
            "ON CONFLICT (norm_query_string, application_name) "
            "DO UPDATE SET hints = '$1'",
            query_id, hint)));

        SleepFor(MonoDelta::FromMilliseconds(300 * kTimeMultiplier));
      }
    });
  };

  // Thread that sets the hint to batched nested loop
  add_hint_thread(conn_hint1, "YbBatchedNL(pg_class pg_attribute)");

  // Thread that sets the hint to merge join
  add_hint_thread(conn_hint2, "MergeJoin(pg_class pg_attribute)");

  // Sleep for 3 seconds to ensure the hint threads are running
  SleepFor(MonoDelta::FromSeconds(3 * kTimeMultiplier));

  // Thread that runs EXPLAIN and checks the join type
  threads.AddThreadFunctor([&stop_threads,
                            &conn_explain,
                            &join_types_mutex,
                            &observed_join_types,
                            &iterations]() {
    while (!stop_threads) {
      std::string join_type = ASSERT_RESULT(
        ExecuteExplainAndGetJoinType(&conn_explain, explain_query));
      LOG(INFO) << "Observed join type: " << join_type;

      {
        std::lock_guard<std::mutex> lock(join_types_mutex);
        observed_join_types.push_back(join_type);
      }

      // Verify that we're using either a batched nested loop or a merge join
      ASSERT_TRUE(join_type == "YB Batched Nested Loop" || join_type == "Merge Join")
          << "Unexpected join type: " << join_type;

      iterations++;
      SleepFor(MonoDelta::FromMilliseconds(100 * kTimeMultiplier));
    }
  });

  // Let the test run for a few seconds
  SleepFor(MonoDelta::FromSeconds(10 * kTimeMultiplier));
  stop_threads = true;

  threads.JoinAll();

  // ----------------------------------------------------------------------------------------------
  // 5. Verify results
  // ----------------------------------------------------------------------------------------------
  LOG(INFO) << "Test completed with " << iterations << " iterations";

  // Verify that all join types were either merge join or batched nested loop join
  {
    std::lock_guard<std::mutex> lock(join_types_mutex);
    for (const auto& join_type : observed_join_types) {
      ASSERT_TRUE(join_type == "YB Batched Nested Loop" || join_type == "Merge Join")
          << "Unexpected join type: " << join_type;
    }

    // Make sure we observed at least one join
    ASSERT_FALSE(observed_join_types.empty()) << "No joins were observed during the test";
  }
}

void FailIfNotConcurrentDDLErrors(const Status& status) {
  // Expect a catalog version mismatch error during concurrent operations
  if (!status.ok()) {
    std::string error_msg = status.ToString();
    if (error_msg.find("pgsql error 40001") != std::string::npos ||
        error_msg.find("Catalog Version Mismatch") != std::string::npos ||
        error_msg.find("Restart read required") != std::string::npos ||
        error_msg.find("schema version mismatch") != std::string::npos) {
      LOG(INFO) << "Expected error: " << error_msg;
    } else {
      FAIL() << "Unexpected error: " << error_msg;
    }
  }
}

class PgHintTableTestTableLocksDisabled : public PgHintTableTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // TODO(#28742): Enabling ysql_yb_ddl_transaction_block_enabled causes the test to fail with
    // "could not serialize access due to concurrent update" errors.
    PgHintTableTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back("--enable_object_locking_for_table_locks=false");
    options->extra_master_flags.push_back("--enable_object_locking_for_table_locks=false");
    options->extra_tserver_flags.push_back("--ysql_yb_ddl_transaction_block_enabled=false");
    AppendFlagToAllowedPreviewFlagsCsv(
        options->extra_tserver_flags, "ysql_yb_ddl_transaction_block_enabled");
    options->extra_master_flags.push_back("--ysql_yb_ddl_transaction_block_enabled=false");
    AppendFlagToAllowedPreviewFlagsCsv(
        options->extra_master_flags, "ysql_yb_ddl_transaction_block_enabled");
  }
};

// Test that hints work correctly when ANALYZE is running concurrently
TEST_F_EX(PgHintTableTest, HintWithConcurrentAnalyze, PgHintTableTestTableLocksDisabled) {
  // ----------------------------------------------------------------------------------------------
  // 1. Run concurrent ANALYZE and hint insertion
  // ----------------------------------------------------------------------------------------------
  std::atomic<bool> stop_threads(false);
  std::atomic<int> hint_num(0);
  // Thread to run ANALYZE
  TestThreadHolder threads;

  // Thread to run ANALYZE
  auto conn_analyze = ASSERT_RESULT(ConnectWithHintTable());
  // Thread to insert the hint
  auto conn_hint = ASSERT_RESULT(ConnectWithHintTable());

  threads.AddThreadFunctor([&stop_threads, &conn_analyze]() {
    LOG(INFO) << "Starting ANALYZE thread";

    while (!stop_threads.load()) {
      auto status = conn_analyze.Execute("ANALYZE VERBOSE");

      FailIfNotConcurrentDDLErrors(status);
    }

    LOG(INFO) << "ANALYZE completed";
  });

  threads.AddThreadFunctor([&stop_threads, &hint_num, &conn_hint]() {

    LOG(INFO) << "Starting hint insertion thread";
    while (!stop_threads.load()) {
      auto status = conn_hint.ExecuteFormat(
          "INSERT INTO hint_plan.hints (norm_query_string, application_name, hints) "
          "VALUES ('$0', '', 'MergeJoin(pg_class pg_attribute)') "
          "ON CONFLICT (norm_query_string, application_name) "
          "DO UPDATE SET hints = 'MergeJoin(pg_class pg_attribute)'",
          hint_num);
      FailIfNotConcurrentDDLErrors(status);
      if (status.ok()) {
        hint_num++;
      }
    }
    LOG(INFO) << "Successfully inserted " << hint_num << " hints";
  });

  // Let the test run for a few seconds
  SleepFor(MonoDelta::FromSeconds(10 * kTimeMultiplier));
  stop_threads = true;

  threads.JoinAll();

  // ----------------------------------------------------------------------------------------------
  // 2. Verify that the correct number of hints were inserted
  // ----------------------------------------------------------------------------------------------
  auto conn_hint_table = ASSERT_RESULT(ConnectWithHintTable());
  auto hint_count =
      ASSERT_RESULT(conn_hint_table.FetchRow<int64_t>("SELECT COUNT(*) FROM hint_plan.hints"));
  ASSERT_EQ(hint_count, hint_num) << "Unexpected number of hints in the table";
}

TEST_F(PgHintTableTest, PreparedStatementHintCacheRefresh) {
  // ----------------------------------------------------------------------------------------------
  // Setup connections
  // ----------------------------------------------------------------------------------------------
  auto conn_pstmt = ASSERT_RESULT(ConnectWithHintTable());
  auto conn_hint = ASSERT_RESULT(ConnectWithHintTable());

  // Get initial metrics
  auto initial_metrics = GetJsonMetrics();
  int64_t initial_misses = GetMetricValue(initial_metrics, "HintCacheMisses");
  ASSERT_EQ(initial_misses, 0);
  int64_t initial_hits = GetMetricValue(initial_metrics, "HintCacheHits");
  ASSERT_EQ(initial_hits, 0);
  int64_t initial_refreshes = GetMetricValue(initial_metrics, "HintCacheRefresh");
  ASSERT_EQ(initial_refreshes, 0);

  // ----------------------------------------------------------------------------------------------
  // 1. Do PREPARE
  // ----------------------------------------------------------------------------------------------
  // Create our prepared statement. This does a lookup to the hint table. However,
  // PREPARE isn't considered a query, so the metrics are not updated. We also need to
  // execute a SELECT 1 so that ybpgm_ExecutorEnd is invoked and the metrics are updated.
  // Note that executing the SELECT 1 itself will also result in a cache miss, so we
  // expect to see 2 misses here.
  ASSERT_OK(conn_pstmt.Execute("PREPARE test_stmt AS " + query_with_param));
  ASSERT_OK(conn_pstmt.FetchRow<PGUint32>("SELECT 1"));

  auto prepared_metrics = GetPrometheusMetrics();
  int64_t prepared_misses = GetMetricValue(prepared_metrics, "HintCacheMisses");
  ASSERT_EQ(prepared_misses - initial_misses, 2);
  int64_t prepared_hits = GetMetricValue(prepared_metrics, "HintCacheHits");
  ASSERT_EQ(prepared_hits - initial_hits, 0);
  int64_t prepared_refreshes = GetMetricValue(prepared_metrics, "HintCacheRefresh");
  ASSERT_EQ(prepared_refreshes - initial_refreshes, 1);

  // ----------------------------------------------------------------------------------------------
  // 2. Execute prepared statement 5 times and verify misses
  // ----------------------------------------------------------------------------------------------
  LOG(INFO) << "Executing prepared statement 5 times (should result in misses)";

  // Execute the prepared statement 6 times.
  // Each execution should result in a cache miss.
  // Each of the first 5 executions will generate a custom plan, and the 6th execution will
  // generate the generic plan.
  int64_t custom_plan_misses = 0;
  int64_t custom_plan_hits = 0;
  int64_t custom_plan_refreshes = 0;
  for (int i = 0; i < 6; i++) {
    auto join_type = ASSERT_RESULT(ExecuteExplainAndGetJoinType(
        &conn_pstmt, Format("EXPLAIN (ANALYZE, FORMAT JSON) EXECUTE test_stmt($0)", i)));
    ASSERT_STR_EQ("YB Batched Nested Loop", join_type);
    // Verify that we got 5 misses and no hits
    auto metrics_custom_plan = GetPrometheusMetrics();
    custom_plan_misses = GetMetricValue(metrics_custom_plan, "HintCacheMisses");
    ASSERT_EQ(custom_plan_misses - prepared_misses, i + 1)
        << "Expected " << i << " misses";
    custom_plan_hits = GetMetricValue(metrics_custom_plan, "HintCacheHits");
    ASSERT_EQ(custom_plan_hits - prepared_hits, 0);
    custom_plan_refreshes = GetMetricValue(metrics_custom_plan, "HintCacheRefresh");
    ASSERT_EQ(custom_plan_refreshes - prepared_refreshes, 0);
  }

  // ----------------------------------------------------------------------------------------------
  // 3. Execute prepared statement 5 more times and verify no additional cache hits
  // ----------------------------------------------------------------------------------------------
  LOG(INFO) << "Executing prepared statement 5 more times (should not result in any cache hits)";

  // Execute the prepared statement 5 more times. We shouldn't see any more cache lookups.
  int64_t generic_plan_misses = 0;
  int64_t generic_plan_hits = 0;
  int64_t generic_plan_refreshes = 0;
  for (int i = 7; i < 12; i++) {
    auto join_type = ASSERT_RESULT(ExecuteExplainAndGetJoinType(
        &conn_pstmt, Format("EXPLAIN (ANALYZE, FORMAT JSON) EXECUTE test_stmt($0)", i)));
    ASSERT_STR_EQ("YB Batched Nested Loop", join_type);

    // Verify that we got no additional hits/misses
    auto metrics_generic_plan = GetPrometheusMetrics();
    generic_plan_misses = GetMetricValue(metrics_generic_plan, "HintCacheMisses");
    ASSERT_EQ(generic_plan_misses - custom_plan_misses, 0);

    generic_plan_hits = GetMetricValue(metrics_generic_plan, "HintCacheHits");
    ASSERT_EQ(generic_plan_hits - custom_plan_hits, 0);

    generic_plan_refreshes = GetMetricValue(metrics_generic_plan, "HintCacheRefresh");
    ASSERT_EQ(generic_plan_refreshes - custom_plan_refreshes, 0);
  }

  // ----------------------------------------------------------------------------------------------
  // 4. Insert a hint for the query and verify cache refresh and hit
  // ----------------------------------------------------------------------------------------------
  LOG(INFO) << "Adding hint to the hint table";

  // Insert a hint for the query
  ASSERT_OK(conn_hint.ExecuteFormat(
      "INSERT INTO hint_plan.hints (norm_query_string, application_name, hints) "
      "VALUES ('$0', '', 'MergeJoin(pg_class pg_attribute)')",
      query_with_param_id));

  // Wait for the heartbeat to propagate the invalidation messages for the hint table
  WaitForCatalogVersionToPropagate();
  auto hint_insertion_metrics = GetPrometheusMetrics();

  // The conn_hint backend will have its own hint cache refreshed.
  int64_t hint_insertion_refreshes = GetMetricValue(hint_insertion_metrics, "HintCacheRefresh");
  ASSERT_EQ(hint_insertion_refreshes - generic_plan_refreshes, 1);

  int64_t hint_insertion_hits = GetMetricValue(hint_insertion_metrics, "HintCacheHits");
  ASSERT_EQ(hint_insertion_hits - generic_plan_hits, 0);

  // During hint insertion, we do 1 hint table lookups when transactional ddl is enabled and 3
  // otherwise.
  //
  // When transactional DDL is disabled, we get one for the INSERT itself, and 2 for the 2 queries
  // that are executed as part of the yb_increment_db_catalog_version_with_inval_messages function
  // (one DELETE, one INSERT).
  //
  // When transactional DDL is enabled, the insert and the 2 queries of
  // yb_increment_db_catalog_version_with_inval_messages function are executed in a single
  // transaction, leading to a single cache miss.
  int64_t hint_insertion_misses = GetMetricValue(hint_insertion_metrics, "HintCacheMisses");
  ASSERT_EQ(
      hint_insertion_misses - generic_plan_misses, IsTransactionalDdlEnabled(conn_hint) ? 1 : 3);

  // ----------------------------------------------------------------------------------------------
  // 5. Re-execute the prepared statement and verify the cache gets refreshed and the new hint is
  // used
  // ----------------------------------------------------------------------------------------------
  auto join_type = ASSERT_RESULT(ExecuteExplainAndGetJoinType(
      &conn_pstmt, "EXPLAIN (ANALYZE, FORMAT JSON) EXECUTE test_stmt(10)"));
  ASSERT_STR_EQ("Merge Join", join_type);
  auto new_hint_metrics = GetPrometheusMetrics();

  // Verify that we got 1 cache refresh resulting from the new hint.
  int64_t new_hint_refreshes = GetMetricValue(new_hint_metrics, "HintCacheRefresh");
  ASSERT_EQ(new_hint_refreshes - hint_insertion_refreshes, 1) << "Expected 1 cache refresh";

  // Verify that we got 2 positive hits. After the plan cache is invalidated,
  // we expect 2 positive hits because the planner first redoes the parsing/rewriting (1st lookup),
  // and then generates a new plan (2nd lookup).
  int64_t new_hint_hits = GetMetricValue(new_hint_metrics, "HintCacheHits");
  ASSERT_EQ(new_hint_hits - hint_insertion_hits, 2) << "Expected 2 positive cache hits";

  int64_t new_hint_misses = GetMetricValue(new_hint_metrics, "HintCacheMisses");
  ASSERT_EQ(new_hint_misses - hint_insertion_misses, 0) << "Expected 0 misses";
}

TEST_F(PgHintTableTest, InvalidHint) {
  // ----------------------------------------------------------------------------------------------
  // 1. Setup connections and prepare statement
  // ----------------------------------------------------------------------------------------------
  auto conn_explain = ASSERT_RESULT(ConnectWithHintTable());
  auto conn_hint = ASSERT_RESULT(ConnectWithHintTable());

  // Execute the prepared statement once to establish a baseline
  auto initial_join_type = ASSERT_RESULT(ExecuteExplainAndGetJoinType(
      &conn_explain, "EXPLAIN (ANALYZE, FORMAT JSON) " + query));
  LOG(INFO) << "Initial join type: " << initial_join_type;

  // ----------------------------------------------------------------------------------------------
  // 2. Insert an invalid hint for the query
  // ----------------------------------------------------------------------------------------------
  LOG(INFO) << "Adding invalid hint to the hint table";

  // Insert an invalid hint for the query (invalid join method)
  ASSERT_OK(conn_hint.ExecuteFormat(
      "INSERT INTO hint_plan.hints (norm_query_string, application_name, hints) "
      "VALUES ('$0', '', 'InvalidJoinMethod(pg_class pg_attribute)')",
      query_with_param_id));

  // Wait for the heartbeat to propagate the invalidation messages for the hint table
  WaitForCatalogVersionToPropagate();
  auto invalid_hint_metrics = GetPrometheusMetrics();

  // ----------------------------------------------------------------------------------------------
  // 3. Re-execute the query and verify the invalid hint is ignored
  // ----------------------------------------------------------------------------------------------
  for (int i = 0; i < 10; i++) {
    auto join_type_after_invalid = ASSERT_RESULT(ExecuteExplainAndGetJoinType(
        &conn_explain, "EXPLAIN (ANALYZE, FORMAT JSON) " + query));
    // The join type should remain the same as the initial one
    // since the invalid hint should be ignored
    ASSERT_STR_EQ(initial_join_type, join_type_after_invalid);
  }
}

/*
 * Inserts many large hints into the hint table and verifies that the hint cache memory usage
 * does not grow beyond the expected amount (calculated by summing the size of the hints we add).
 */
TEST_F(PgHintTableTest, HintCacheMemoryLeakTest) {
  // ----------------------------------------------------------------------------------------------
  // 1. Setup connections, add hints and check that the hints are being followed
  // ----------------------------------------------------------------------------------------------
  auto [conn_query, conn_hint] = ASSERT_RESULT(InsertHintsAndRunQueries());

  // ----------------------------------------------------------------------------------------------
  // 2. Check size of YbHintCacheContext and the total size of all memory contexts
  // ----------------------------------------------------------------------------------------------
  for (PGConn* conn : {&conn_query, &conn_hint}) {
    ASSERT_TRUE(ASSERT_RESULT(
        conn->FetchRow<bool>("SELECT pg_log_backend_memory_contexts(pg_backend_pid())")));
    ASSERT_TRUE(ASSERT_RESULT(
        conn->FetchRow<bool>("SELECT yb_log_backend_heap_snapshot(pg_backend_pid())")));
    auto memory_context = ASSERT_RESULT((conn->FetchRow<std::string, PGUint64>(
        "SELECT name, total_bytes FROM pg_backend_memory_contexts "
        "WHERE name = 'YbHintCacheContext'")));

    // Log all memory contexts related to YbHintCache
    LOG(INFO) << "YbHintCacheContext size: " << std::get<1>(memory_context) << " bytes";

    // Verify the memory usage is reasonable (less than 10MB). The sum of the size
    // of the hints we added is around 5MB, so 10MB is a reasonable upper bound.
    ASSERT_LT(std::get<1>(memory_context), 10 * 1024 * 1024)
        << "YbHintCacheContext memory usage exceeds 10MB";

    // Sum the total size of all memory contexts
    auto total_memory = ASSERT_RESULT(conn->FetchRow<PGUint64>(
        "SELECT SUM(total_bytes)::bigint FROM pg_backend_memory_contexts"));
    LOG(INFO) << "Total memory context size: " << total_memory << " bytes";

    // Verify the total memory usage is also reasonable (less than 10MB)
    ASSERT_LT(total_memory, 40 * 1024 * 1024) << "Total memory context usage exceeds 40MB";
  }
}

// Test that the hint cache is disabled when the yb_enable_hint_table_cache flag is set to off.
TEST_F(PgHintTableTestWithoutHintCache, HintCacheDisabled) {
  // ----------------------------------------------------------------------------------------------
  // 1. Setup connections, add hints and check that the hints are being followed
  // ----------------------------------------------------------------------------------------------
  auto [conn_query, conn_hint] = ASSERT_RESULT(InsertHintsAndRunQueries());

  // ----------------------------------------------------------------------------------------------
  // 2. Check that the YbHintCacheContext does not exist on all connections
  // ----------------------------------------------------------------------------------------------
  auto count_query = ASSERT_RESULT(conn_query.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_backend_memory_contexts "
      "WHERE name = 'YbHintCacheContext'"));
  ASSERT_EQ(count_query, 0);
  auto count_hint = ASSERT_RESULT(conn_hint.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_backend_memory_contexts "
      "WHERE name = 'YbHintCacheContext'"));
  ASSERT_EQ(count_hint, 0);
}

}  // namespace yb::pgwrapper
