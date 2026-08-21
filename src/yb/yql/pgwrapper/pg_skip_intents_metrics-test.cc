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
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/client/async_rpc.h"
#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/util/physical_time.h"
#include "yb/util/timestamp.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_bool(ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks);
DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(ysql_cdcsdk_enable_old_namespace_streams);

METRIC_DECLARE_counter(skip_intents_writes);

namespace yb {
namespace pgwrapper {

class SkipIntentsMetricTest : public pgwrapper::LibPqTestBase {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.emplace_back("--ysql_yb_ddl_transaction_block_enabled=true");
    options->extra_master_flags.emplace_back("--enable_object_locking_for_table_locks=true");
    // Needed for RC tests
    options->extra_master_flags.emplace_back("--yb_enable_read_committed_isolation=true");

    options->extra_tserver_flags.emplace_back("--ysql_yb_ddl_transaction_block_enabled=true");
    options->extra_tserver_flags.emplace_back("--enable_object_locking_for_table_locks=true");
    // Needed for RC tests
    options->extra_tserver_flags.emplace_back("--yb_enable_read_committed_isolation=true");

    options->extra_tserver_flags.emplace_back(
        "--ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks=true");
    options->extra_tserver_flags.push_back(
        "--allowed_preview_flags_csv=ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks");

    // Set a high max batch size to ensure metric tests stay reliable.
    // If the batch size is too low, inserting rows into a single table might
    // get split across multiple batches. This could cause our ASSERT_GE(..., 2)
    // metric checks to pass using only the main table's writes, masking a bug
    // if the optimization failed to apply to the table's indexes.
    options->extra_tserver_flags.emplace_back("--ysql_session_max_batch_size=100000");

  }

  Result<int64_t> GetSkipIntentsCount() {
    int64_t result = 0;
    for (auto* tserver : cluster_->tserver_daemons()) {
      auto count_res = tserver->GetMetric<int64>(
          &METRIC_ENTITY_server, "yb.tabletserver", &METRIC_skip_intents_writes,
          "value");
      // The metric might not be instantiated on a tablet server if it hasn't
      // handled any relevant operations yet, causing GetMetric to return NotFound.
      // We gracefully treat NotFound as a count of 0.
      if (count_res.ok()) {
        result += *count_res;
      } else if (!count_res.status().IsNotFound()) {
        RETURN_NOT_OK(count_res);
      }
    }
    return result;
  }
};

class SkipIntentsBasicTest : public SkipIntentsMetricTest,
                            public ::testing::WithParamInterface<const char*> {
};

TEST_P(SkipIntentsBasicTest, TestCTASMetricsWithIsolation) {
  const char* isolation_level = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  // Set the isolation level for this specific test run
  ASSERT_OK(conn.ExecuteFormat("SET default_transaction_isolation TO '$0'", isolation_level));

  auto initial_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // Step 1: Create source table, which is also a CTAS
  ASSERT_OK(conn.Execute("CREATE TABLE source_tb AS SELECT generate_series(1, 100) AS id"));
  auto interim_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // Step 2: Execute another CTAS
  ASSERT_OK(conn.Execute("CREATE TABLE target_tb AS SELECT * FROM source_tb"));
  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());

  LOG(INFO) << CURRENT_TEST_NAME() << ": Isolation: " << isolation_level << " | Writes: "
            << initial_writes << " -> "
            << interim_writes << " -> "
            << final_writes;

  ASSERT_GT(interim_writes, initial_writes);
  ASSERT_GT(final_writes, interim_writes);
}

TEST_P(SkipIntentsBasicTest, TestAlterTableRewriteMetrics) {
  const char* isolation_level = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  // 1. Set isolation level and enable optimization
  ASSERT_OK(conn.ExecuteFormat("SET default_transaction_isolation TO '$0'", isolation_level));

  // 2. Setup: Create a table and populate it with data
  ASSERT_OK(conn.Execute("CREATE TABLE rewrite_test (id INT PRIMARY KEY, val INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO rewrite_test SELECT g, g FROM generate_series(1, 100) g"));

  // 3. Capture baseline before the rewrite
  // We capture after the INSERT so we only measure the ALTER TABLE impact
  auto initial_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // 4. Trigger Rewrite: Change INT to TEXT
  // This forces Postgres to create a transient heap and rewrite every row
  ASSERT_OK(conn.Execute("ALTER TABLE rewrite_test ALTER COLUMN val TYPE TEXT"));

  // 5. Capture final metrics
  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());

  LOG(INFO) << CURRENT_TEST_NAME() << ": Isolation: " << isolation_level << " | Writes: "
            << initial_writes << " -> "
            << final_writes;

  // 6. Assertions
  // The rewrite should have skipped intents for the new transient heap
  ASSERT_GT(final_writes, initial_writes);
}

TEST_P(SkipIntentsBasicTest, TestAddPKRewriteMetrics) {
  const char* isolation_level = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("SET default_transaction_isolation TO '$0'", isolation_level));

  // 1. Setup: Create a table without a PK and add data
  ASSERT_OK(conn.Execute("CREATE TABLE add_pk_test (id INT, val INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO add_pk_test SELECT g, g FROM generate_series(1, 100) g"));

  auto initial_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // 2. Trigger Rewrite: Add Primary Key
  // YSQL will create a new transient table with the new PK schema and backfill it.
  ASSERT_OK(conn.Execute("ALTER TABLE add_pk_test ADD PRIMARY KEY (id)"));

  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());

  LOG(INFO) << CURRENT_TEST_NAME() << ": Isolation: " << isolation_level
            << " | Writes: " << initial_writes << " -> " << final_writes;

  // The rewrite should skip intents for the transient heap writes
  ASSERT_GT(final_writes, initial_writes);
}

TEST_P(SkipIntentsBasicTest, TestDropPKRewriteMetrics) {
  const char* isolation_level = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("SET default_transaction_isolation TO '$0'", isolation_level));

  // 1. Setup: Create a table WITH a PK and add data
  ASSERT_OK(conn.Execute("CREATE TABLE drop_pk_test (id INT PRIMARY KEY, val INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO drop_pk_test SELECT g, g FROM generate_series(1, 100) g"));

  auto initial_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // 2. Trigger Rewrite: Drop Primary Key
  // This removes the physical clustering by ID and moves data to a non-PK heap (ybrowid).
  ASSERT_OK(conn.Execute("ALTER TABLE drop_pk_test DROP CONSTRAINT drop_pk_test_pkey"));

  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());

  LOG(INFO) << CURRENT_TEST_NAME() << ": Isolation: " << isolation_level
            << " | Writes: " << initial_writes << " -> " << final_writes;

  ASSERT_GT(final_writes, initial_writes);
}

TEST_P(SkipIntentsBasicTest, TestVolatileDefaultRewriteMetrics) {
  const char* isolation_level = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("SET default_transaction_isolation TO '$0'", isolation_level));

  // 1. Setup
  ASSERT_OK(conn.Execute("CREATE TABLE volatile_test (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO volatile_test SELECT g FROM generate_series(1, 1000) g"));

  auto initial_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // 2. Trigger Rewrite: Add column with a VOLATILE default
  // Because random() is different for every row, YB must physically rewrite the table.
  ASSERT_OK(conn.Execute("ALTER TABLE volatile_test ADD COLUMN val FLOAT DEFAULT random()"));

  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());

  LOG(INFO) << CURRENT_TEST_NAME() << ": Isolation: " << isolation_level
            << " | Writes: " << initial_writes << " -> " << final_writes;

  ASSERT_GT(final_writes, initial_writes);
}

TEST_P(SkipIntentsBasicTest, TestVolatileAlterRewriteMetrics) {
  const char* isolation_level = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("SET default_transaction_isolation TO '$0'", isolation_level));

  // 1. Setup: Create 4 tables with 1000 rows each
  for (int i = 1; i <= 4; ++i) {
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE rewrite_t$0 (id INT PRIMARY KEY, val INT)", i));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO rewrite_t$0 SELECT g, g FROM generate_series(1, 1000) g", i));
  }

  // Capture baseline before the rewrites
  auto baseline_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // 2. Trigger Rewrites
  // Adding a column with a volatile default (random()) forces Yugabyte to
  // create a new physical table and copy all existing rows into it.
  for (int i = 1; i <= 4; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
        "ALTER TABLE rewrite_t$0 ADD COLUMN extra_val FLOAT DEFAULT random()", i));
  }

  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());

  LOG(INFO) << CURRENT_TEST_NAME() << ": Isolation: " << isolation_level
            << " | writes: " << baseline_writes
            << " -> " << final_writes;

  // 3. Assertions
  // Each ALTER TABLE should have contributed to the skip count.
  // We expect at least +4 (one for each table rewrite).
  ASSERT_GE(final_writes, baseline_writes + 4);

  // 4. Data Integrity Check
  // Ensure the volatile column was actually populated
  auto val = ASSERT_RESULT(conn.FetchRow<double>("SELECT extra_val FROM rewrite_t1 LIMIT 1"));
  ASSERT_GE(val, 0.0);
}

TEST_P(SkipIntentsBasicTest, TestMultiIndexRewriteMetrics) {
  const char* isolation_level = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("SET default_transaction_isolation TO '$0'", isolation_level));

  // 1. Setup: Create table with 1000 rows and 5 secondary indexes
  ASSERT_OK(conn.Execute(
      "CREATE TABLE index_stress_test (id INT PRIMARY KEY, "
      "c1 INT, c2 INT, c3 INT, c4 INT, c5 INT)"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO index_stress_test SELECT g, g, g, g, g, g "
      "FROM generate_series(1, 1000) g"));

  for (int i = 1; i <= 5; ++i) {
    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX idx$0 ON index_stress_test(c$0)", i));
  }

  // Capture baseline after setup is complete
  auto baseline_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // 2. Trigger Rewrite: Adding a column with a volatile default (random())
  // This forces a physical rewrite of the heap AND a rebuild/backfill of all 5 indexes.
  ASSERT_OK(conn.Execute(
      "ALTER TABLE index_stress_test ADD COLUMN volatile_col FLOAT DEFAULT random()"));

  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());

  LOG(INFO) << CURRENT_TEST_NAME() << ": Isolation: " << isolation_level
            << " | writes: " << baseline_writes
            << " -> " << final_writes;

  // 3. Assertions
  // We expect a significant jump in the skip count.
  // Ideally: +1 (Heap) + 5 (Indexes) = at least 6 skip-intents batches.
  ASSERT_GE(final_writes, baseline_writes + 6);

  // 4. Verification
  auto count = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM index_stress_test"));
  ASSERT_EQ(count, 1000);
}

TEST_P(SkipIntentsBasicTest, TestCreateLikeInsertMetrics) {
  const char* isolation_level = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("SET default_transaction_isolation TO '$0'", isolation_level));

  // 1. Setup Source Table with 1000 rows and an index
  ASSERT_OK(conn.Execute("CREATE TABLE source_tbl (id INT PRIMARY KEY, val INT)"));
  ASSERT_OK(conn.Execute("CREATE INDEX idx_source ON source_tbl(val)"));
  ASSERT_OK(conn.Execute("INSERT INTO source_tbl SELECT g, g FROM generate_series(1, 1000) g"));

  auto initial_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // 2. Perform Clone in a single Transaction
  // We use BEGIN/COMMIT to ensure the CREATE and INSERT share the same transaction context
  ASSERT_OK(conn.Execute("BEGIN"));

  // This triggers internal scans (index builds) which should NOT disable the optimization
  ASSERT_OK(conn.Execute("CREATE TABLE clone_tbl (LIKE source_tbl INCLUDING ALL)"));

  // This is the heavy write operation that should trigger Skip Intents
  ASSERT_OK(conn.Execute("INSERT INTO clone_tbl SELECT * FROM source_tbl"));

  ASSERT_OK(conn.Execute("COMMIT"));

  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());

  LOG(INFO) << CURRENT_TEST_NAME() << ": Isolation: " << isolation_level
            << " | Writes: " << initial_writes << " -> " << final_writes;

  // 3. Assertions
  if (strcmp(isolation_level, "READ COMMITTED") == 0) {
    // final_writes should be at least initial_writes + 2 (1 for heap, 1 for index)
    ASSERT_GE(final_writes, initial_writes + 2);
  } else {
    ASSERT_EQ(initial_writes, 0);
    ASSERT_EQ(final_writes, initial_writes);
  }

  // 4. Verify data integrity

  auto count = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM clone_tbl"));
  ASSERT_EQ(count, 1000);
}

TEST_P(SkipIntentsBasicTest, TestIsolationLevelBehavior) {
  const char* isolation_level = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("SET default_transaction_isolation TO '$0'", isolation_level));

  auto baseline_writes = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE iso_test (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO iso_test SELECT g FROM generate_series(1, 100) g"));
  ASSERT_OK(conn.Execute("COMMIT"));

  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());

  LOG(INFO) << CURRENT_TEST_NAME() << ": Isolation: " << isolation_level
            << " | Writes: " << baseline_writes << " -> " << final_writes;

  if (strcmp(isolation_level, "READ COMMITTED") == 0) {
    // Optimization should trigger here
    ASSERT_GT(final_writes, baseline_writes);
  } else {
    // For Repeatable Read / Serializable, we expect optimization to be OFF
    ASSERT_EQ(final_writes, baseline_writes);
  }
}

TEST_P(SkipIntentsBasicTest, TestChainedOperationsInTxnBlock) {
  const char* isolation_level = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("SET default_transaction_isolation TO '$0'", isolation_level));

  auto baseline_writes = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("BEGIN"));

  // 1. CREATE TABLE
  ASSERT_OK(conn.Execute("CREATE TABLE chained_tb (id INT PRIMARY KEY, val INT)"));

  // 2. INSERT
  ASSERT_OK(conn.Execute("INSERT INTO chained_tb SELECT g, g % 10 FROM generate_series(1, 100) g"));
  auto writes_after_insert = ASSERT_RESULT(GetSkipIntentsCount());

  // 3. CREATE MV
  ASSERT_OK(conn.Execute(
      "CREATE MATERIALIZED VIEW chained_mv AS SELECT val, count(*) FROM chained_tb GROUP BY val"));
  auto writes_after_create_mv = ASSERT_RESULT(GetSkipIntentsCount());

  // 4. More INSERT
  ASSERT_OK(conn.Execute(
      "INSERT INTO chained_tb SELECT g, g % 10 FROM generate_series(101, 150) g"));
  auto writes_after_dml = ASSERT_RESULT(GetSkipIntentsCount());

  // 5. REFRESH MV
  ASSERT_OK(conn.Execute("REFRESH MATERIALIZED VIEW chained_mv"));
  auto writes_after_refresh = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("COMMIT"));

  LOG(INFO) << CURRENT_TEST_NAME() << ": Isolation: " << isolation_level
            << " | baseline: " << baseline_writes
            << ", after insert: " << writes_after_insert
            << ", after create mv: " << writes_after_create_mv
            << ", after dml: " << writes_after_dml
            << ", after refresh mv: " << writes_after_refresh;

  if (strcmp(isolation_level, "READ COMMITTED") == 0) {
    ASSERT_GT(writes_after_insert, baseline_writes);
    ASSERT_GT(writes_after_create_mv, writes_after_insert);
    ASSERT_GT(writes_after_dml, writes_after_create_mv);
    ASSERT_GT(writes_after_refresh, writes_after_dml);
  } else {
    ASSERT_EQ(writes_after_insert, baseline_writes);
    ASSERT_EQ(writes_after_create_mv, baseline_writes);
    ASSERT_EQ(writes_after_dml, baseline_writes);
    ASSERT_EQ(writes_after_refresh, baseline_writes);
  }

  auto tb_count = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM chained_tb"));
  ASSERT_EQ(tb_count, 150);

  auto mv_count = ASSERT_RESULT(conn.FetchRow<PGUint64>(
      "SELECT sum(count)::bigint FROM chained_mv"));
  ASSERT_EQ(mv_count, 150);
}

TEST_P(SkipIntentsBasicTest, TestChainedCreateIndexInTxnBlock) {
  const char* isolation_level = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("SET default_transaction_isolation TO '$0'", isolation_level));

  auto baseline_writes = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("BEGIN"));

  // 1. CREATE TABLE
  ASSERT_OK(conn.Execute("CREATE TABLE chained_idx_tb (id INT, val INT)"));

  // 2. INSERT
  ASSERT_OK(conn.Execute(
      "INSERT INTO chained_idx_tb SELECT g, g % 10 FROM generate_series(1, 100) g"));
  auto writes_after_insert = ASSERT_RESULT(GetSkipIntentsCount());

  // 3. CREATE INDEX
  ASSERT_OK(conn.Execute("CREATE INDEX ON chained_idx_tb(val)"));
  auto writes_after_create_idx = ASSERT_RESULT(GetSkipIntentsCount());

  // 4. More INSERT
  ASSERT_OK(conn.Execute(
      "INSERT INTO chained_idx_tb SELECT g, g % 10 FROM generate_series(101, 150) g"));
  auto writes_after_dml = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("COMMIT"));

  LOG(INFO) << CURRENT_TEST_NAME() << ": Isolation: " << isolation_level
            << " | baseline: " << baseline_writes
            << " -> after_insert: " << writes_after_insert
            << " -> after_create_idx: " << writes_after_create_idx
            << " -> after_dml: " << writes_after_dml;

  if (strcmp(isolation_level, "READ COMMITTED") == 0) {
    ASSERT_GT(writes_after_insert, baseline_writes);
    // Inside a transaction block an implicitly concurrent CREATE INDEX is transparently
    // turned into a non-concurrent one (see ProcessUtilitySlow's T_IndexStmt case). A
    // non-concurrent build populates the index from this backend through ybcinbuild, and
    // those writes target an index relation created by this transaction, so they take the
    // skip intents path. (A concurrent build would instead be backfilled by DocDB itself
    // and would contribute nothing to this metric.)
    ASSERT_GT(writes_after_create_idx, writes_after_insert);
    ASSERT_GT(writes_after_dml, writes_after_create_idx);
  } else {
    ASSERT_EQ(writes_after_insert, baseline_writes);
    ASSERT_EQ(writes_after_create_idx, baseline_writes);
    ASSERT_EQ(writes_after_dml, baseline_writes);
  }

  auto tb_count = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM chained_idx_tb"));
  ASSERT_EQ(tb_count, 150);
}

TEST_P(SkipIntentsBasicTest, TestChainedAlterTableInTxnBlock) {
  const char* isolation_level = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("SET default_transaction_isolation TO '$0'", isolation_level));

  auto baseline_writes = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("BEGIN"));

  // 1. CREATE TABLE
  ASSERT_OK(conn.Execute("CREATE TABLE chained_alter_tb (id INT, val INT)"));

  // 2. INSERT
  ASSERT_OK(conn.Execute(
      "INSERT INTO chained_alter_tb SELECT g, g % 10 FROM generate_series(1, 100) g"));
  auto writes_after_insert = ASSERT_RESULT(GetSkipIntentsCount());

  // 3. ALTER TABLE
  ASSERT_OK(conn.Execute(
      "ALTER TABLE chained_alter_tb ADD COLUMN gen_val INT GENERATED ALWAYS AS (val * 2) STORED"));
  auto writes_after_alter = ASSERT_RESULT(GetSkipIntentsCount());

  // 4. More INSERT
  ASSERT_OK(conn.Execute(
      "INSERT INTO chained_alter_tb (id, val) SELECT g, g % 10 FROM generate_series(101, 150) g"));
  auto writes_after_dml = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("COMMIT"));

  LOG(INFO) << CURRENT_TEST_NAME() << ": Isolation: " << isolation_level
            << " | baseline: " << baseline_writes
            << " -> after_insert: " << writes_after_insert
            << " -> after_alter: " << writes_after_alter
            << " -> after_dml: " << writes_after_dml;

  if (strcmp(isolation_level, "READ COMMITTED") == 0) {
    ASSERT_GT(writes_after_insert, baseline_writes);
    // Adding a stored generated column rewrites the table into a new relfilenode created by
    // this same transaction, so every copied row is written via the fastpath.
    ASSERT_GT(writes_after_alter, writes_after_insert);
    ASSERT_GT(writes_after_dml, writes_after_alter);
  } else {
    ASSERT_EQ(writes_after_insert, baseline_writes);
    ASSERT_EQ(writes_after_alter, baseline_writes);
    ASSERT_EQ(writes_after_dml, baseline_writes);
  }

  auto tb_count = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM chained_alter_tb"));
  ASSERT_EQ(tb_count, 150);
}

TEST_P(SkipIntentsBasicTest, TestChainedDropIdentityInTxnBlock) {
  const char* isolation_level = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("SET default_transaction_isolation TO '$0'", isolation_level));

  auto baseline_writes = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("BEGIN"));

  // 1. CREATE TABLE
  ASSERT_OK(conn.Execute(
      "CREATE TABLE chained_ident_tb (id INT GENERATED ALWAYS AS IDENTITY, val INT)"));

  // 2. INSERT
  ASSERT_OK(conn.Execute("INSERT INTO chained_ident_tb (val) SELECT generate_series(1, 100)"));
  auto writes_after_insert = ASSERT_RESULT(GetSkipIntentsCount());

  // 3. ALTER TABLE DROP IDENTITY
  // ADD IDENTITY, SET IDENTITY, and DROP IDENTITY do NOT cause a table rewrite.
  // They are purely metadata operations (catalog changes) that create/drop/modify
  // the internal sequence and update the pg_attribute catalog entry.
  // Because they don't cause a table rewrite, they neither create a new relfilenode
  // nor write any row of the user table, so the INSERT that follows still targets a
  // relfilenode created earlier in this transaction and stays on the fastpath.
  ASSERT_OK(conn.Execute("ALTER TABLE chained_ident_tb ALTER COLUMN id DROP IDENTITY"));
  auto writes_after_alter = ASSERT_RESULT(GetSkipIntentsCount());

  // 4. More INSERT
  ASSERT_OK(conn.Execute(
      "INSERT INTO chained_ident_tb (id, val) SELECT g, g FROM generate_series(101, 150) g"));
  auto writes_after_dml = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("COMMIT"));

  LOG(INFO) << CURRENT_TEST_NAME() << ": Isolation: " << isolation_level
            << " | baseline: " << baseline_writes
            << " -> after_insert: " << writes_after_insert
            << " -> after_alter: " << writes_after_alter
            << " -> after_dml: " << writes_after_dml;

  if (strcmp(isolation_level, "READ COMMITTED") == 0) {
    ASSERT_GT(writes_after_insert, baseline_writes);
    // DROP IDENTITY writes no row of the user table, so the count is unchanged.
    ASSERT_EQ(writes_after_alter, writes_after_insert);
    ASSERT_GT(writes_after_dml, writes_after_alter);
  }

  auto tb_count = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM chained_ident_tb"));
  ASSERT_EQ(tb_count, 150);
}

TEST_P(SkipIntentsBasicTest, TestChainedForeignKeyInTxnBlock) {
  const char* isolation_level = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("SET default_transaction_isolation TO '$0'", isolation_level));

  auto baseline_writes = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("BEGIN"));

  // 1. CREATE TABLES
  ASSERT_OK(conn.Execute("CREATE TABLE chained_fk_parent (id INT PRIMARY KEY, val INT)"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE chained_fk_child (id INT PRIMARY KEY, "
      "parent_id INT REFERENCES chained_fk_parent(id), val INT)"));

  // 2. INSERT INTO PARENT
  ASSERT_OK(conn.Execute(
      "INSERT INTO chained_fk_parent SELECT g, g FROM generate_series(1, 100) g"));
  auto writes_after_parent_insert = ASSERT_RESULT(GetSkipIntentsCount());

  // 3. INSERT INTO CHILD (triggers a referential integrity read on parent)
  ASSERT_OK(conn.Execute(
      "INSERT INTO chained_fk_child SELECT g, g, g FROM generate_series(1, 50) g"));
  auto writes_after_child_insert = ASSERT_RESULT(GetSkipIntentsCount());

  // 4. More INSERT INTO PARENT
  ASSERT_OK(conn.Execute(
      "INSERT INTO chained_fk_parent SELECT g, g FROM generate_series(101, 150) g"));
  auto writes_after_second_parent_insert = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("COMMIT"));

  LOG(INFO) << CURRENT_TEST_NAME() << ": Isolation: " << isolation_level
            << " | baseline: " << baseline_writes
            << " -> after_parent_insert: " << writes_after_parent_insert
            << " -> after_child_insert: " << writes_after_child_insert
            << " -> after_second_parent_insert: " << writes_after_second_parent_insert;

  if (strcmp(isolation_level, "READ COMMITTED") == 0) {
    ASSERT_GT(writes_after_parent_insert, baseline_writes);
    // The child insert's foreign key check reads the parent rows that were written via
    // the fastpath, and its own writes still take the fastpath.
    ASSERT_GT(writes_after_child_insert, writes_after_parent_insert);
    ASSERT_GT(writes_after_second_parent_insert, writes_after_child_insert);
  } else {
    ASSERT_EQ(writes_after_parent_insert, baseline_writes);
    ASSERT_EQ(writes_after_child_insert, baseline_writes);
    ASSERT_EQ(writes_after_second_parent_insert, baseline_writes);
  }

  auto tb_count = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM chained_fk_child"));
  ASSERT_EQ(tb_count, 50);
}

TEST_P(SkipIntentsBasicTest, TestPartitionedTableMetrics) {
  const char* isolation_level = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("SET default_transaction_isolation TO '$0'", isolation_level));

  auto baseline_writes = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE part_tb (id INT, val INT) PARTITION BY RANGE (id)"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE part_tb_p1 PARTITION OF part_tb FOR VALUES FROM (1) TO (100)"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE part_tb_p2 PARTITION OF part_tb FOR VALUES FROM (100) TO (200)"));

  ASSERT_OK(conn.Execute("INSERT INTO part_tb SELECT g, g FROM generate_series(1, 199) g"));
  auto writes_after_insert = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_OK(conn.Execute("COMMIT"));

  LOG(INFO) << CURRENT_TEST_NAME() << ": Isolation: " << isolation_level
            << " | baseline: " << baseline_writes
            << " -> writes_after_insert: " << writes_after_insert;

  if (strcmp(isolation_level, "READ COMMITTED") == 0) {
    ASSERT_GT(writes_after_insert, baseline_writes);
  } else {
    ASSERT_EQ(writes_after_insert, baseline_writes);
  }

  auto count = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM part_tb"));
  ASSERT_EQ(count, 199);
}

INSTANTIATE_TEST_SUITE_P(, SkipIntentsBasicTest,
    ::testing::Values("READ COMMITTED", "REPEATABLE READ", "SERIALIZABLE")
);

using RefreshParams = std::tuple<const char*, bool>; // <IsolationLevel, IsConcurrent>

class SkipIntentsMatViewTest : public SkipIntentsMetricTest,
                               public ::testing::WithParamInterface<RefreshParams> {
};

TEST_P(SkipIntentsMatViewTest, TestRefreshMetrics) {
  auto [isolation_level, is_concurrent] = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  // 1. Setup Session
  ASSERT_OK(conn.ExecuteFormat("SET default_transaction_isolation TO '$0'", isolation_level));

  // 2. Initial Setup: Table -> MatView -> Unique Index
  ASSERT_OK(conn.Execute(
      "CREATE TABLE base_table AS SELECT g AS id, g % 10 AS val FROM generate_series(1, 1000) g"));
  ASSERT_OK(conn.Execute(
      "CREATE MATERIALIZED VIEW test_mv AS SELECT val, count(*) FROM base_table GROUP BY val"));

  // 3. Capture baseline AFTER setup but BEFORE create unique index.
  auto initial_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // Concurrent refresh requires a unique index on the MatView
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX NONCONCURRENTLY test_mv_idx ON test_mv (val)"));

  // 4. Capture baseline AFTER create unique index but BEFORE refresh
  auto interim_writes = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_GT(initial_writes, 0);
  ASSERT_GT(interim_writes, initial_writes);

  // 5. Refresh the View
  std::string cmd = "REFRESH MATERIALIZED VIEW ";
  if (is_concurrent) cmd += "CONCURRENTLY ";
  cmd += "test_mv";

  LOG(INFO) << "Running: " << cmd << " with isolation: " << isolation_level;
  ASSERT_OK(conn.Execute(cmd));

  // 6. Verify Metrics
  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());

  LOG(INFO) << CURRENT_TEST_NAME() << ": Isolation: " << isolation_level
            << ", is_concurrent: " << is_concurrent
            << " | Writes: "
            << initial_writes << " -> "
            << interim_writes << " -> "
            << final_writes;

  if (!is_concurrent) {
    // Non-concurrent: Uses "Hidden Table + Swap". Should trigger skip-intents.
    ASSERT_GT(final_writes, initial_writes)
        << "Expected skip-intents for standard REFRESH (Hidden Table path)";
  } else {
    // Concurrent: Uses DML (Diff/Merge) on temp tables and existing table.
    // Should NOT trigger skip-intents.
    ASSERT_EQ(final_writes, interim_writes);
  }
}

INSTANTIATE_TEST_SUITE_P(, SkipIntentsMatViewTest,
    ::testing::Combine(
        ::testing::Values("READ COMMITTED", "REPEATABLE READ", "SERIALIZABLE"),
        ::testing::Bool() // true = concurrent, false = non-concurrent
    )
);

class SkipIntentsPublicationTest : public SkipIntentsMetricTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    SkipIntentsMetricTest::UpdateMiniClusterOptions(options);
    // Disable old namespace-level CDCSDK so that only the publication-based
    // check gates skip-intents, without a master RPC fallback.
    options->extra_tserver_flags.emplace_back(
        "--ysql_cdcsdk_enable_old_namespace_streams=false");
  }
};

TEST_F(SkipIntentsPublicationTest, TestSkipIntentsDisabledWithPublication) {
  auto conn = ASSERT_RESULT(Connect());

  // Use READ COMMITTED so that skip-intents is active inside transaction blocks.
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'READ COMMITTED'"));

  // Step 1: Baseline - CTAS should use skip-intents when no publication exists.
  ASSERT_OK(conn.Execute("CREATE TABLE base_t1 AS SELECT generate_series(1, 100) AS id"));
  auto writes_after_baseline = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_GT(writes_after_baseline, 0)
      << "CTAS should use skip-intents when no publication exists";

  // Step 2: Create a publication - this should disable skip-intents for
  // subsequent DDL in the same database.
  ASSERT_OK(conn.Execute("CREATE PUBLICATION test_pub FOR ALL TABLES"));

  auto writes_before_pub_ctas = ASSERT_RESULT(GetSkipIntentsCount());

  // CTAS while a publication exists should NOT use skip-intents.
  ASSERT_OK(conn.Execute("CREATE TABLE pub_t1 AS SELECT generate_series(1, 100) AS id"));
  auto writes_after_pub_ctas = ASSERT_RESULT(GetSkipIntentsCount());

  LOG(INFO) << "With publication: " << writes_before_pub_ctas
            << " -> " << writes_after_pub_ctas;
  ASSERT_EQ(writes_after_pub_ctas, writes_before_pub_ctas)
      << "CTAS should NOT use skip-intents when a publication exists";

  // Step 3: Drop the publication - skip-intents should be re-enabled.
  ASSERT_OK(conn.Execute("DROP PUBLICATION test_pub"));

  auto writes_before_drop_ctas = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("CREATE TABLE nopub_t1 AS SELECT generate_series(1, 100) AS id"));
  auto writes_after_drop_ctas = ASSERT_RESULT(GetSkipIntentsCount());

  LOG(INFO) << "After drop publication: " << writes_before_drop_ctas
            << " -> " << writes_after_drop_ctas;
  ASSERT_GT(writes_after_drop_ctas, writes_before_drop_ctas)
      << "CTAS should use skip-intents after publication is dropped";
}

TEST_F(SkipIntentsPublicationTest, TestSkipIntentsDisabledWithPublicationInTxnBlock) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'READ COMMITTED'"));

  // Create a publication.
  ASSERT_OK(conn.Execute("CREATE PUBLICATION test_pub FOR ALL TABLES"));

  auto writes_before = ASSERT_RESULT(GetSkipIntentsCount());

  // Run a CREATE TABLE + INSERT inside a transaction block while a publication
  // exists.  Neither the table creation nor the inserts should use skip-intents.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE txn_pub_t1 (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO txn_pub_t1 SELECT g FROM generate_series(1, 100) g"));
  ASSERT_OK(conn.Execute("COMMIT"));

  auto writes_after = ASSERT_RESULT(GetSkipIntentsCount());

  LOG(INFO) << "Txn block with publication: " << writes_before
            << " -> " << writes_after;
  ASSERT_EQ(writes_after, writes_before)
      << "Skip-intents should be disabled inside txn block when publication exists";

  // Now drop the publication and retry the same pattern.
  ASSERT_OK(conn.Execute("DROP PUBLICATION test_pub"));

  auto writes_before_nopub = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE txn_nopub_t1 (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO txn_nopub_t1 SELECT g FROM generate_series(1, 100) g"));
  ASSERT_OK(conn.Execute("COMMIT"));

  auto writes_after_nopub = ASSERT_RESULT(GetSkipIntentsCount());

  LOG(INFO) << "Txn block without publication: " << writes_before_nopub
            << " -> " << writes_after_nopub;
  ASSERT_GT(writes_after_nopub, writes_before_nopub)
      << "Skip-intents should work inside txn block when no publication exists";
}

class SkipIntentsCDCSDKTest : public SkipIntentsMetricTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    SkipIntentsMetricTest::UpdateMiniClusterOptions(options);
    // Explicitly enable old namespace-level CDCSDK (it is enabled by default,
    // but just to be explicit for this test).
    options->extra_tserver_flags.emplace_back(
        "--ysql_cdcsdk_enable_old_namespace_streams=true");
  }
};

TEST_F(SkipIntentsCDCSDKTest, TestSkipIntentsDisabledWithLegacyCDCStream) {
  auto conn = ASSERT_RESULT(Connect());

  // Step 1: Baseline - CTAS should use skip-intents when no CDC stream exists.
  ASSERT_OK(conn.Execute("CREATE TABLE base_t1 AS SELECT generate_series(1, 100) AS id"));
  auto writes_after_baseline = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_GT(writes_after_baseline, 0)
      << "CTAS should use skip-intents when no CDC stream exists";

  // Step 2: Create a legacy CDC stream on the yugabyte database.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
      &client->proxy_cache(),
      cluster_->master(0)->bound_rpc_addr());

  cdc::CreateCDCStreamRequestPB req;
  cdc::CreateCDCStreamResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(30));

  req.set_namespace_name("yugabyte");
  req.set_record_type(cdc::CHANGE);
  req.set_checkpoint_type(cdc::EXPLICIT);
  req.set_record_format(cdc::CDCRecordFormat::PROTO);
  req.set_source_type(cdc::CDCSDK);

  ASSERT_OK(cdc_proxy->CreateCDCStream(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error()) << "Failed to create CDC stream: "
                                 << resp.error().status().ShortDebugString();

  auto writes_before_cdc_ctas = ASSERT_RESULT(GetSkipIntentsCount());

  // CTAS while a CDC stream exists should NOT use skip-intents.
  ASSERT_OK(conn.Execute("CREATE TABLE cdc_t1 AS SELECT generate_series(1, 100) AS id"));
  auto writes_after_cdc_ctas = ASSERT_RESULT(GetSkipIntentsCount());

  LOG(INFO) << "With legacy CDC stream: " << writes_before_cdc_ctas
            << " -> " << writes_after_cdc_ctas;
  ASSERT_EQ(writes_after_cdc_ctas, writes_before_cdc_ctas)
      << "CTAS should NOT use skip-intents when a legacy CDC stream exists";
}

/*
 * Reads of a relation created in the current transaction vs skip-intents.
 * Reading such a relation - standalone, from a modifying CTE, or from a self
 * referencing INSERT..SELECT - must neither disable the optimization for later
 * statements nor produce an anomaly, because fastpath operations read at the
 * statement's in_txn_limit. Only a subtransaction (an explicit SAVEPOINT or a
 * PL/pgSQL EXCEPTION block) still disables the optimization, since writes to the
 * regular db cannot be rolled back.
 */
class SkipIntentsSameTxnCreatedRelationTest : public SkipIntentsMetricTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    SkipIntentsMetricTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.emplace_back("--ysql_bypass_anonymous_savepoint_ddl_check=false");
  }
};

TEST_F(SkipIntentsSameTxnCreatedRelationTest, SelectBetweenInsertsStillSkips) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'READ COMMITTED'"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE si_txn_select_relax (id INT PRIMARY KEY)"));
  auto m0 = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_OK(conn.Execute(
      "INSERT INTO si_txn_select_relax SELECT g FROM generate_series(1, 40) g"));
  auto m1 = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_GT(m1, m0) << "First INSERT in txn should use skip-intents";

  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM si_txn_select_relax")),
      40);
  auto m2 = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_EQ(m2, m1) << "Plain SELECT should not change skip-intents write metric";

  ASSERT_OK(conn.Execute(
      "INSERT INTO si_txn_select_relax SELECT g FROM generate_series(41, 80) g"));
  auto m3 = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_GT(m3, m2) << "Second INSERT after standalone SELECT should still use skip-intents";

  ASSERT_OK(conn.Execute("COMMIT"));

  auto n = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM si_txn_select_relax"));
  ASSERT_EQ(n, 80);
}

TEST_F(SkipIntentsSameTxnCreatedRelationTest, ModifyingCteStillSkips) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'READ COMMITTED'"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE si_mcte_guard (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO si_mcte_guard VALUES (1)"));
  auto m_after_seed = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<PGUint64>(
                "WITH w AS (INSERT INTO si_mcte_guard VALUES (2) RETURNING 1) "
                "SELECT count(*) FROM si_mcte_guard")),
            1);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM si_mcte_guard")),
            2);
  auto m_after_mcte_select = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("INSERT INTO si_mcte_guard VALUES (3)"));
  auto m_after_tail_insert = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("COMMIT"));

  LOG(INFO) << "skip_intents_writes: after seed " << m_after_seed
            << ", after modifying-CTE SELECT " << m_after_mcte_select
            << ", after tail INSERT " << m_after_tail_insert;

  ASSERT_GT(m_after_mcte_select, m_after_seed);
  ASSERT_GT(m_after_tail_insert, m_after_mcte_select);

  auto n = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM si_mcte_guard"));
  ASSERT_EQ(n, 3);
}

TEST_F(SkipIntentsSameTxnCreatedRelationTest, SelfInsertSelectStillSkips) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'READ COMMITTED'"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE si_self_ins (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO si_self_ins SELECT g FROM generate_series(1, 25) g"));
  auto m_after_seed = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("INSERT INTO si_self_ins SELECT id + 100 FROM si_self_ins"));
  auto m_after_self_scan_insert = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("INSERT INTO si_self_ins VALUES (99999)"));
  auto m_after_tail_insert = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("COMMIT"));

  LOG(INFO) << "skip_intents_writes: after seed " << m_after_seed
            << ", after INSERT..SELECT self " << m_after_self_scan_insert
            << ", after tail INSERT " << m_after_tail_insert;

  ASSERT_GT(m_after_self_scan_insert, m_after_seed);
  ASSERT_GT(m_after_tail_insert, m_after_self_scan_insert);

  auto n = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM si_self_ins"));
  ASSERT_EQ(n, 51);
}

TEST_F(SkipIntentsSameTxnCreatedRelationTest, ExceptionBlockDisablesFollowingInsert) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'READ COMMITTED'"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE si_exc_guard (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO si_exc_guard SELECT g FROM generate_series(1, 25) g"));
  auto m_after_seed = ASSERT_RESULT(GetSkipIntentsCount());

  // A PL/pgSQL block with EXCEPTION creates an internal savepoint (subtransaction)
  ASSERT_OK(conn.Execute(
      "DO $$ BEGIN\n"
      "  INSERT INTO si_exc_guard VALUES (99);\n"
      "EXCEPTION WHEN OTHERS THEN\n"
      "  NULL;\n"
      "END $$;"));
  auto m_after_exc_block = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("INSERT INTO si_exc_guard VALUES (99999)"));
  auto m_after_tail_insert = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("COMMIT"));

  LOG(INFO) << "skip_intents_writes: after seed " << m_after_seed
            << ", after exception block " << m_after_exc_block
            << ", after tail INSERT " << m_after_tail_insert;

  ASSERT_EQ(m_after_exc_block, m_after_seed)
      << "INSERT inside an EXCEPTION block uses a subtransaction, which disables skip-intents";
  ASSERT_EQ(m_after_tail_insert, m_after_exc_block)
      << "INSERT after an EXCEPTION block should not use skip-intents";

  auto n = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM si_exc_guard"));
  ASSERT_EQ(n, 27);
}

TEST_F(SkipIntentsSameTxnCreatedRelationTest, ExplicitSavepointDisablesOptimization) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'READ COMMITTED'"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE si_sp_guard (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO si_sp_guard SELECT g FROM generate_series(1, 25) g"));
  auto m_after_seed = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("SAVEPOINT sp1"));
  ASSERT_OK(conn.Execute("INSERT INTO si_sp_guard VALUES (99)"));
  auto m_after_sp_insert = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT sp1"));

  ASSERT_OK(conn.Execute("INSERT INTO si_sp_guard VALUES (99999)"));
  auto m_after_tail_insert = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("COMMIT"));

  LOG(INFO) << "skip_intents_writes: after seed " << m_after_seed
            << ", after savepoint insert " << m_after_sp_insert
            << ", after tail INSERT " << m_after_tail_insert;

  ASSERT_EQ(m_after_sp_insert, m_after_seed)
      << "INSERT after SAVEPOINT uses a subtransaction, which disables skip-intents";
  ASSERT_EQ(m_after_tail_insert, m_after_sp_insert)
      << "INSERT after SAVEPOINT / ROLLBACK TO SAVEPOINT should not use skip-intents";

  auto n = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM si_sp_guard"));
  ASSERT_EQ(n, 26);
}

class SkipIntentsSafetyTest : public SkipIntentsMetricTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    SkipIntentsMetricTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.emplace_back(
        "--ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks=false");
    options->extra_tserver_flags.emplace_back(
        "--ysql_yb_enable_ddl_savepoint_support=true");
    AppendCsvFlagValue(options->extra_tserver_flags, "allowed_preview_flags_csv",
                       "ysql_yb_enable_ddl_savepoint_support");
  }
};

TEST_F(SkipIntentsSafetyTest, TestTopLevelConditions) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'READ COMMITTED'"));

  // 1. Explicit transaction block (!IsTransactionBlock() == false)
  LOG(INFO) << "Explicit transaction block";
  auto writes_before = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE si_safety_txn (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO si_safety_txn SELECT g FROM generate_series(1, 10) g"));
  ASSERT_OK(conn.Execute("COMMIT"));
  auto writes_after = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_EQ(writes_after, writes_before)
      << "Optimization should be disabled in explicit transaction block";

  // 2. Subtransaction (GetCurrentTransactionNestLevel() != 1)
  // Tested via PL/pgSQL block with EXCEPTION which creates a subtransaction
  // We don't swallow errors here, to make sure the table creation and insert actually run.
  // Note: this requires ysql_yb_enable_ddl_savepoint_support=true to be set in the cluster options
  // (which we add in UpdateMiniClusterOptions) because the EXCEPTION clause causes the BEGIN block
  // to execute inside a subtransaction, so DDL inside it requires savepoint support.
  LOG(INFO) << "Subtransaction";
  ASSERT_OK(conn.Execute(
      "DO $$ BEGIN\n"
      "  CREATE TABLE si_safety_subtxn (id INT PRIMARY KEY);\n"
      "  INSERT INTO si_safety_subtxn SELECT g FROM generate_series(1, 10) g;\n"
      "EXCEPTION WHEN unique_violation THEN\n"
      "  NULL;\n"
      "END $$;"));
  auto writes_after_subtxn = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_EQ(writes_after_subtxn, writes_after)
      << "Optimization should be disabled in a subtransaction";

  // 3. Triggers (YbGetTriggerDepth() > 0)
  LOG(INFO) << "Triggers";
  ASSERT_OK(conn.Execute("CREATE TABLE si_safety_base (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute(
      "CREATE OR REPLACE FUNCTION trig_func() RETURNS TRIGGER AS $$\n"
      "BEGIN\n"
      "  CREATE TABLE si_safety_trig (id INT PRIMARY KEY);\n"
      "  INSERT INTO si_safety_trig SELECT g FROM generate_series(1, 10) g;\n"
      "  RETURN NEW;\n"
      "END; $$ LANGUAGE plpgsql;"));
  ASSERT_OK(conn.Execute(
      "CREATE TRIGGER test_trig AFTER INSERT ON si_safety_base\n"
      "FOR EACH ROW EXECUTE PROCEDURE trig_func();"));

  auto writes_before_trig = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_OK(conn.Execute("INSERT INTO si_safety_base VALUES (1)"));
  auto writes_after_trig = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_EQ(writes_after_trig, writes_before_trig)
      << "Optimization should be disabled inside a trigger";
}

class SkipIntentsPITRTest : public SkipIntentsMetricTest {
 protected:
  void SetUp() override {
    SkipIntentsMetricTest::SetUp();
    client_ = ASSERT_RESULT(cluster_->CreateClient());
    snapshot_util_.SetProxy(&client_->proxy_cache());
    snapshot_util_.SetCluster(cluster_.get());
  }

  void DoTestPITRAfterSkipIntentsWrites(bool use_txn_block) {
    auto conn = ASSERT_RESULT(Connect());
    const std::string kDbName = "yugabyte";

    if (use_txn_block) {
      ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'READ COMMITTED'"));
      // Pre-existing table to verify restore doesn't break it.
      ASSERT_OK(conn.Execute("CREATE TABLE preexisting (id INT PRIMARY KEY)"));
      ASSERT_OK(conn.Execute(
          "INSERT INTO preexisting SELECT g FROM generate_series(1, 100) g"));
    } else {
      // Create a source table to SELECT from during CTAS.
      ASSERT_OK(conn.Execute("CREATE TABLE preexisting AS SELECT generate_series(1, 500) AS id"));
    }

    // Set up a snapshot schedule on this database.
    const int kSnapshotIntervalSecs = 1;
    auto schedule_id = ASSERT_RESULT(snapshot_util_.CreateSchedule(
        kDbName, client::WaitSnapshot::kTrue,
        MonoDelta::FromSeconds(kSnapshotIntervalSecs)));

    // Capture the restore time BEFORE the writes.
    Timestamp current_time(ASSERT_RESULT(WallClock()->Now()).time_point);
    HybridTime restore_time = HybridTime::FromMicros(current_time.ToInt64());

    // Wait for a snapshot to cover the restore time.
    SleepFor(kSnapshotIntervalSecs * 5s);

    auto writes_before = ASSERT_RESULT(GetSkipIntentsCount());

    if (use_txn_block) {
      ASSERT_OK(conn.Execute("BEGIN"));
      ASSERT_OK(conn.Execute("CREATE TABLE pitr_test (id INT PRIMARY KEY)"));
      ASSERT_OK(conn.Execute(
          "INSERT INTO pitr_test SELECT g FROM generate_series(1, 200) g"));
      ASSERT_OK(conn.Execute("COMMIT"));
    } else {
      ASSERT_OK(conn.Execute("CREATE TABLE pitr_test AS SELECT * FROM preexisting"));
    }

    auto writes_after = ASSERT_RESULT(GetSkipIntentsCount());

    LOG(INFO) << "Skip-intents writes: " << writes_before << " -> " << writes_after;
    ASSERT_GT(writes_after, writes_before)
        << "Should have used skip-intents (data written directly to regular DB)";

    // Verify the table exists and has data before restore.
    auto count = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM pitr_test"));
    if (use_txn_block) {
      ASSERT_EQ(count, 200);
    } else {
      ASSERT_EQ(count, 500);
    }

    // Restore to the time before the writes.
    LOG(INFO) << "Restoring to " << restore_time;
    auto snapshot_id = ASSERT_RESULT(
        snapshot_util_.PickSuitableSnapshot(schedule_id, restore_time));
    ASSERT_OK(snapshot_util_.RestoreSnapshot(snapshot_id, restore_time));
    LOG(INFO) << "Restoration complete";

    // After restore, the created table should not exist in PG catalog.
    // The DocDB table (which received skip-intents writes to regular DB) should
    // be cleaned up because PG catalog no longer references it.
    client::VerifyTableNotExists(client_.get(), kDbName, "pitr_test", 30);

    // Verify via SQL that the table is not accessible.
    conn = ASSERT_RESULT(Connect());
    auto result = conn.Execute("SELECT count(*) FROM pitr_test");
    ASSERT_NOK(result) << "Table pitr_test should not exist after PITR restore";

    // Pre-existing table and its data should be intact.
    auto preexisting_count = ASSERT_RESULT(
        conn.FetchRow<PGUint64>("SELECT count(*) FROM preexisting"));
    if (use_txn_block) {
      ASSERT_EQ(preexisting_count, 100);
    } else {
      ASSERT_EQ(preexisting_count, 500);
    }
  }

  client::SnapshotTestUtil snapshot_util_;
  std::unique_ptr<client::YBClient> client_;
};

// Verify that PITR correctly handles tables whose data was written via the
// skip-intents optimization (directly to regular DB, bypassing the intents
// layer).  After restoring to a point before the CTAS, the table and its data
// should be gone.
TEST_F(SkipIntentsPITRTest, TestPITRAfterSkipIntentsCTAS) {
  DoTestPITRAfterSkipIntentsWrites(/* use_txn_block = */ false);
}

// Same as above but with a transaction block: BEGIN, CREATE TABLE, INSERT,
// COMMIT - then restore to before the transaction.
TEST_F(SkipIntentsPITRTest, TestPITRAfterSkipIntentsTxnBlock) {
  DoTestPITRAfterSkipIntentsWrites(/* use_txn_block = */ true);
}

TEST_F(SkipIntentsPITRTest, TestPITRBeforeCommitForTxnBlock) {
  auto conn = ASSERT_RESULT(Connect());
  const std::string kDbName = "yugabyte";

  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'READ COMMITTED'"));

  // Set up a snapshot schedule.
  const int kSnapshotIntervalSecs = 1;
  auto schedule_id = ASSERT_RESULT(snapshot_util_.CreateSchedule(
      kDbName, client::WaitSnapshot::kTrue,
      MonoDelta::FromSeconds(kSnapshotIntervalSecs)));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE txn_pitr_uncommitted (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO txn_pitr_uncommitted SELECT g FROM generate_series(1, 100) g"));

  // Capture restore time BEFORE the commit
  Timestamp current_time(ASSERT_RESULT(WallClock()->Now()).time_point);
  HybridTime restore_time = HybridTime::FromMicros(current_time.ToInt64());

  // Wait for a snapshot.
  SleepFor(kSnapshotIntervalSecs * 5s);

  ASSERT_OK(conn.Execute("COMMIT"));

  // Verify data exists before restore.
  auto count = ASSERT_RESULT(
      conn.FetchRow<PGUint64>("SELECT count(*) FROM txn_pitr_uncommitted"));
  ASSERT_EQ(count, 100);

  // Restore to before the commit.
  LOG(INFO) << "Restoring to " << restore_time;
  auto snapshot_id = ASSERT_RESULT(
      snapshot_util_.PickSuitableSnapshot(schedule_id, restore_time));
  ASSERT_OK(snapshot_util_.RestoreSnapshot(snapshot_id, restore_time));
  LOG(INFO) << "Restoration complete";

  // The transaction-created table should not exist.
  // The tablet might have been created and written to (since skip-intents writes directly
  // to regular DB), but since the catalog transaction was uncommitted at restore_time,
  // the table will be gone from the catalog, and the orphan tablet will eventually be
  // cleaned up by the master's catalog manager.
  client::VerifyTableNotExists(client_.get(), kDbName, "txn_pitr_uncommitted", 30);

  conn = ASSERT_RESULT(Connect());
  auto result = conn.Execute("SELECT count(*) FROM txn_pitr_uncommitted");
  ASSERT_NOK(result) << "Table txn_pitr_uncommitted should not exist after PITR restore";
}

TEST_F(SkipIntentsMetricTest, ConcurrentPublicationCTAS) {
  auto conn = ASSERT_RESULT(Connect());

  std::atomic<bool> stop_publication_thread{false};
  std::thread pub_thread([this, &stop_publication_thread]() {
    auto bg_conn = ASSERT_RESULT(Connect());
    while (!stop_publication_thread.load(std::memory_order_acquire)) {
      WARN_NOT_OK(bg_conn.Execute("CREATE PUBLICATION test_pub FOR ALL TABLES"),
                  "Failed to create publication");
      WARN_NOT_OK(bg_conn.Execute("DROP PUBLICATION test_pub"),
                  "Failed to drop publication");
    }
  });

  for (int i = 0; i < 20; ++i) {
    auto s = conn.ExecuteFormat("CREATE TABLE ctas_test_$0 AS SELECT generate_series(1, 10) id", i);
    if (!s.ok()) {
      // DDL operations can fail with a restart read error if another DDL (e.g. our concurrent
      // CREATE/DROP PUBLICATION) increments the catalog version. Simply retry in this test.
      if (s.ToString().find("Catalog Version Mismatch") != std::string::npos ||
          s.ToString().find("Restart read required") != std::string::npos ||
          s.ToString().find("could not serialize access due to concurrent update") !=
              std::string::npos) {
        --i; // Retry the same iteration
        continue;
      }
      ASSERT_OK(s);
    }
  }

  stop_publication_thread.store(true, std::memory_order_release);
  pub_thread.join();
}

TEST_F(SkipIntentsMetricTest, TestGucCannotBeChangedInImplicitTxn) {
  auto conn = ASSERT_RESULT(Connect());

  auto result = conn.Execute(
      "DO $$ BEGIN\n"
      "  CREATE TABLE implicit_txn_guc_test AS SELECT 1 AS id;\n"
      "  SET yb_enable_new_relation_fastpath_write_in_txn_blocks = on;\n"
      "END $$;");

  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(
      result.ToString(),
      "cannot be changed inside a transaction block or after any query has been run");
}

TEST_F(SkipIntentsMetricTest, TestGucCanBeChangedByNormalUser) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE USER normal_user"));
  ASSERT_OK(conn.Execute("SET ROLE normal_user"));

  // yb_enable_new_relation_fastpath_write must be on to enable
  // yb_enable_new_relation_fastpath_write_in_txn_blocks
  ASSERT_OK(conn.Execute("SET yb_enable_new_relation_fastpath_write = on"));
  ASSERT_OK(conn.Execute("SET yb_enable_new_relation_fastpath_write_in_txn_blocks = on"));

  auto val1 = ASSERT_RESULT(conn.FetchRow<std::string>(
      "SHOW yb_enable_new_relation_fastpath_write"));
  ASSERT_EQ(val1, "on");

  auto val2 = ASSERT_RESULT(conn.FetchRow<std::string>(
      "SHOW yb_enable_new_relation_fastpath_write_in_txn_blocks"));
  ASSERT_EQ(val2, "on");

  // Now test turning them off
  ASSERT_OK(conn.Execute("SET yb_enable_new_relation_fastpath_write_in_txn_blocks = off"));
  ASSERT_OK(conn.Execute("SET yb_enable_new_relation_fastpath_write = off"));

  auto val3 = ASSERT_RESULT(conn.FetchRow<std::string>(
      "SHOW yb_enable_new_relation_fastpath_write"));
  ASSERT_EQ(val3, "off");

  auto val4 = ASSERT_RESULT(conn.FetchRow<std::string>(
      "SHOW yb_enable_new_relation_fastpath_write_in_txn_blocks"));
  ASSERT_EQ(val4, "off");
}

TEST_F(SkipIntentsMetricTest, TestSkipIntentsInDoBlock) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'read committed'"));

  auto initial_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // Execute a DO block that creates a table and inserts multiple rows.
  // We expect this to use skip-intents.
  ASSERT_OK(conn.Execute(
      "DO $$ BEGIN\n"
      "  CREATE TABLE do_block_test (id INT PRIMARY KEY);\n"
      "  INSERT INTO do_block_test SELECT generate_series(1, 100);\n"
      "END $$;"));

  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_GT(final_writes, initial_writes);
}

TEST_F(SkipIntentsMetricTest, TestSkipIntentsInCallProcedure) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'read committed'"));

  // Create the procedure outside
  ASSERT_OK(conn.Execute(
      "CREATE OR REPLACE PROCEDURE my_procedure() AS $$\n"
      "BEGIN\n"
      "  CREATE TABLE call_proc_test (id INT PRIMARY KEY);\n"
      "  INSERT INTO call_proc_test SELECT generate_series(1, 100);\n"
      "END;\n"
      "$$ LANGUAGE plpgsql;"));

  auto initial_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // Execute the CALL statement. We expect this to use skip-intents.
  ASSERT_OK(conn.Execute("CALL my_procedure()"));

  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_GT(final_writes, initial_writes);
}

TEST_F(SkipIntentsMetricTest, TestSkipIntentsInNestedDoBlock) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'read committed'"));

  ASSERT_OK(conn.Execute(
      "CREATE OR REPLACE PROCEDURE inner_procedure_for_do() AS $$\n"
      "BEGIN\n"
      "  CREATE TABLE nested_do_test (id INT PRIMARY KEY);\n"
      "  INSERT INTO nested_do_test SELECT generate_series(1, 100);\n"
      "END;\n"
      "$$ LANGUAGE plpgsql;"));

  auto initial_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // Execute a DO block that calls the procedure, so the CREATE TABLE and the INSERT run
  // two SPI levels deep. Nesting does not disable the optimization: the inner INSERT is a
  // statement of its own and reads/writes at its own in_txn_limit.
  ASSERT_OK(conn.Execute(
      "DO $$ BEGIN\n"
      "  CALL inner_procedure_for_do();\n"
      "END $$;"));

  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_GT(final_writes, initial_writes);
}

TEST_F(SkipIntentsMetricTest, TestSkipIntentsInNestedCallProcedure) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'read committed'"));

  ASSERT_OK(conn.Execute(
      "CREATE OR REPLACE PROCEDURE inner_procedure_for_call() AS $$\n"
      "BEGIN\n"
      "  CREATE TABLE nested_call_test (id INT PRIMARY KEY);\n"
      "  INSERT INTO nested_call_test SELECT generate_series(1, 100);\n"
      "END;\n"
      "$$ LANGUAGE plpgsql;"));

  ASSERT_OK(conn.Execute(
      "CREATE OR REPLACE PROCEDURE outer_procedure_for_call() AS $$\n"
      "BEGIN\n"
      "  CALL inner_procedure_for_call();\n"
      "END;\n"
      "$$ LANGUAGE plpgsql;"));

  auto initial_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // Execute the CALL statement, which calls the inner procedure, so the CREATE TABLE and the
  // INSERT run two SPI levels deep and must still use the optimization.
  ASSERT_OK(conn.Execute("CALL outer_procedure_for_call()"));

  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_GT(final_writes, initial_writes);
}

TEST_F(SkipIntentsMetricTest, TestSkipIntentsWithBuiltinVolatileFunctions) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'read committed'"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE new_table_builtin (id UUID PRIMARY KEY, "
      "created_at TIMESTAMP, random_val DOUBLE PRECISION)"));

  auto initial_writes = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute(
      "INSERT INTO new_table_builtin (id, created_at, random_val)\n"
      "SELECT gen_random_uuid(), clock_timestamp(), random()\n"
      "FROM generate_series(1, 10000)"));

  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_GT(final_writes, initial_writes);

  ASSERT_OK(conn.Execute("COMMIT"));
}

TEST_F(SkipIntentsMetricTest, TestSkipIntentsWithUserVolatileFunctionInReturning) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'read committed'"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE new_table_user_vol (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute(
      "CREATE OR REPLACE FUNCTION my_func_that_reads_new_table() RETURNS INT AS $$\n"
      "BEGIN\n"
      "  RETURN (SELECT count(*) FROM new_table_user_vol);\n"
      "END;\n"
      "$$ LANGUAGE plpgsql VOLATILE;"));

  auto initial_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // INSERT with a user-defined volatile function in the RETURNING clause that reads the
  // table just written via the fastpath. The read is the first read operation of the
  // statement, so the in_txn_limit is picked only after the buffered INSERT has been
  // flushed and the read observes the inserted row (see the in_txn_limit section of
  // src/yb/yql/pggate/README and GHI #10142).
  auto res = ASSERT_RESULT(conn.FetchRow<int32_t>(
      "INSERT INTO new_table_user_vol VALUES (1) RETURNING my_func_that_reads_new_table()"));
  ASSERT_EQ(res, 1); // RETURNING is evaluated after the tuple is inserted

  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());
  ASSERT_GT(final_writes, initial_writes);

  ASSERT_OK(conn.Execute("COMMIT"));
}

TEST_F(SkipIntentsMetricTest, TestAlterTableRewriteWithTrigger) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'read committed'"));

  // 1. Setup: Create a table with a trigger and populate it
  ASSERT_OK(conn.Execute("CREATE TABLE rewrite_trigger_test (id INT PRIMARY KEY, val INT)"));
  ASSERT_OK(conn.Execute(
      "CREATE OR REPLACE FUNCTION dummy_trigger_func() RETURNS TRIGGER AS $$\n"
      "BEGIN\n"
      "  RETURN NEW;\n"
      "END;\n"
      "$$ LANGUAGE plpgsql;"));
  ASSERT_OK(conn.Execute(
      "CREATE TRIGGER my_dummy_trigger BEFORE INSERT ON rewrite_trigger_test "
      "FOR EACH ROW EXECUTE PROCEDURE dummy_trigger_func()"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO rewrite_trigger_test SELECT g, g FROM generate_series(1, 100) g"));

  auto initial_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // 2. Trigger Rewrite: Add a new column with a volatile default
  // This requires a table rewrite. The original relation has a trigger, and the
  // DEFAULT expression uses a volatile function (random()).
  //
  // During an ALTER TABLE rewrite (ATRewriteTable), PostgreSQL creates a *transient*
  // physical heap to copy the data into, and that heap is created by this transaction,
  // so its rows are written via the fastpath. Neither the trigger on the original
  // relation nor the volatile default can observe an inconsistent state, because the
  // rewrite scan reads the original relation while the writes go to the transient heap.
  auto s = conn.Execute(
      "ALTER TABLE rewrite_trigger_test "
      "ADD COLUMN rand_val DOUBLE PRECISION DEFAULT random()");

  // Verify the statement succeeded
  ASSERT_OK(s);

  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());
  LOG(INFO) << CURRENT_TEST_NAME() << " | Writes: "
            << initial_writes << " -> " << final_writes;

  ASSERT_GT(final_writes, initial_writes);
}

TEST_F(SkipIntentsMetricTest, TestAlterTableRewriteWithUserVolatileFunction) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'read committed'"));

  // 1. Setup: Create a table
  ASSERT_OK(conn.Execute("CREATE TABLE rewrite_user_vol_test (id INT PRIMARY KEY, val INT)"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO rewrite_user_vol_test SELECT g, g FROM generate_series(1, 100) g"));

  // Create a user-defined volatile function
  ASSERT_OK(conn.Execute(
      "CREATE OR REPLACE FUNCTION my_volatile_func() RETURNS DOUBLE PRECISION AS $$\n"
      "BEGIN\n"
      "  RETURN random();\n"
      "END;\n"
      "$$ LANGUAGE plpgsql VOLATILE;"));

  auto initial_writes = ASSERT_RESULT(GetSkipIntentsCount());

  // 2. Trigger Rewrite: Add a new column whose default calls a user-defined volatile
  // function. The rewrite must still use the optimization for the transient heap.
  auto s = conn.Execute(
      "ALTER TABLE rewrite_user_vol_test "
      "ADD COLUMN rand_val DOUBLE PRECISION DEFAULT my_volatile_func()");

  // Verify the statement succeeded
  ASSERT_OK(s);

  auto final_writes = ASSERT_RESULT(GetSkipIntentsCount());
  LOG(INFO) << CURRENT_TEST_NAME() << " | Writes: "
            << initial_writes << " -> " << final_writes;

  // The rewrite copies all 100 rows into the transient heap created by ATRewriteTable in
  // this transaction, so each of them is written via the fastpath.
  ASSERT_EQ(final_writes, initial_writes + 100);
}

TEST_F(SkipIntentsMetricTest, TestCursorOnFastpathRelation) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'read committed'"));

  auto baseline_writes = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE cursor_tb (id INT PRIMARY KEY)"));

  // The cursor is declared before any row exists. The portal picks the in_txn_limit for
  // its reads at the first FETCH rather than at DECLARE, so the rows written below are
  // visible to it.
  ASSERT_OK(conn.Execute("DECLARE cur CURSOR FOR SELECT id FROM cursor_tb ORDER BY id"));

  ASSERT_OK(conn.Execute("INSERT INTO cursor_tb SELECT generate_series(1, 100)"));
  auto writes_after_insert = ASSERT_RESULT(GetSkipIntentsCount());

  auto first_batch = ASSERT_RESULT(conn.FetchRows<int32_t>("FETCH 10 FROM cur"));
  ASSERT_EQ(first_batch.size(), 10);
  ASSERT_EQ(first_batch.front(), 1);
  ASSERT_EQ(first_batch.back(), 10);

  // Reading a fastpath relation through a cursor must not disable the optimization, so
  // the writes of this second INSERT still take the fastpath.
  ASSERT_OK(conn.Execute("INSERT INTO cursor_tb SELECT generate_series(101, 150)"));
  auto writes_after_second_insert = ASSERT_RESULT(GetSkipIntentsCount());

  // The portal's in_txn_limit was frozen by the first FETCH, so the rows written after
  // that FETCH are not visible to the remaining FETCHes.
  auto rest = ASSERT_RESULT(conn.FetchRows<int32_t>("FETCH ALL FROM cur"));
  ASSERT_EQ(rest.size(), 90);
  ASSERT_EQ(rest.front(), 11);
  ASSERT_EQ(rest.back(), 100);

  ASSERT_OK(conn.Execute("CLOSE cur"));
  ASSERT_OK(conn.Execute("COMMIT"));

  LOG(INFO) << CURRENT_TEST_NAME() << " | Writes: " << baseline_writes
            << " -> after_insert: " << writes_after_insert
            << " -> after_second_insert: " << writes_after_second_insert;

  ASSERT_GT(writes_after_insert, baseline_writes);
  ASSERT_GT(writes_after_second_insert, writes_after_insert);

  auto count = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM cursor_tb"));
  ASSERT_EQ(count, 150);
}

TEST_F(SkipIntentsMetricTest, TestCursorWithHoldOnFastpathRelation) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'read committed'"));

  auto baseline_writes = ASSERT_RESULT(GetSkipIntentsCount());

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE cursor_hold_tb (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO cursor_hold_tb SELECT generate_series(1, 100)"));
  auto writes_after_insert = ASSERT_RESULT(GetSkipIntentsCount());

  // COMMIT converts the holdable portal into a static one: PersistHoldablePortal runs the
  // cursor's query and drains every row into the portal's tuplestore. That run happens
  // inside CommitTransaction, while the transaction that created the relation is still
  // open, so it reads the rows written via the fastpath. The FETCH below then only reads
  // back the tuplestore.
  ASSERT_OK(conn.Execute(
      "DECLARE cur_hold CURSOR WITH HOLD FOR SELECT id FROM cursor_hold_tb ORDER BY id"));
  ASSERT_OK(conn.Execute("COMMIT"));

  LOG(INFO) << CURRENT_TEST_NAME() << " | Writes: " << baseline_writes << " -> "
            << writes_after_insert;

  ASSERT_GT(writes_after_insert, baseline_writes);

  // This FETCH runs after COMMIT and serves entirely from the tuplestore, so it issues no
  // DocDB read. It is a plain PostgreSQL check that materialization preserved every row,
  // not a check of the optimization: the rows are ordinary committed rows by now.
  auto rows = ASSERT_RESULT(conn.FetchRows<int32_t>("FETCH ALL FROM cur_hold"));
  ASSERT_EQ(rows.size(), 100);
  ASSERT_EQ(rows.front(), 1);
  ASSERT_EQ(rows.back(), 100);
  ASSERT_OK(conn.Execute("CLOSE cur_hold"));
}

TEST_F(SkipIntentsMetricTest, TestAbortedSubtxnWrite) {
  auto conn = ASSERT_RESULT(Connect());

  for (bool fastpath_enabled : {true, false}) {
    ASSERT_OK(conn.ExecuteFormat(
        "SET yb_enable_new_relation_fastpath_write_in_txn_blocks = $0", fastpath_enabled));
    ASSERT_OK(conn.Execute("SET default_transaction_isolation TO 'READ COMMITTED'"));

    std::string table = fastpath_enabled ? "subtxn_abort_t" : "subtxn_abort_ctl_t";
    ASSERT_OK(conn.Execute("BEGIN"));
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY)", table));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (0)", table));
    auto skips_before = ASSERT_RESULT(GetSkipIntentsCount());
    ASSERT_OK(conn.ExecuteFormat(
        "DO $$$$ BEGIN\n"
        "  INSERT INTO $0 VALUES (1);\n"
        "  PERFORM 1 / 0;\n"
        "EXCEPTION WHEN division_by_zero THEN NULL;\n"
        "END $$$$", table));
    auto skips_after = ASSERT_RESULT(GetSkipIntentsCount());
    ASSERT_OK(conn.Execute("COMMIT"));

    // Machine check: the INSERT inside the EXCEPTION block must not have used the fastpath
    // due to implicit savepoint associated with EXCEPTION block.
    ASSERT_EQ(skips_after - skips_before, 0);

    // Correctness check (PostgreSQL semantics): only row 0 must be visible.
    auto rows = ASSERT_RESULT(conn.FetchRows<int32_t>(
        Format("SELECT k FROM $0 ORDER BY k", table)));
    ASSERT_EQ(rows, (std::vector<int32_t>{0}))
        << "a row written inside an aborted subtransaction is visible after commit with fastpath="
        << fastpath_enabled;
  }
}

} // namespace pgwrapper
} // namespace yb
