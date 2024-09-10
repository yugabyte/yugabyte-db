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

#include <future>
#include <gtest/gtest.h>

#include "yb/common/transaction.pb.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/flags.h"
#include "yb/util/monotime.h"
#include "yb/util/stopwatch.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

DECLARE_string(ysql_pg_conf_csv);
DECLARE_string(vmodule);
DECLARE_string(ysql_log_statement);
DECLARE_int32(ysql_log_min_duration_statement);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(ysql_colocate_database_by_default);

namespace yb::pgwrapper {

// This test suite tests the local limit mechanism.
//
// Local limit is YugabyteDB's way of reducing read restarts whenever
// the read is certain that a write happened after the read started
// even if the write has a commit time within the ambiguity window
// (read_time, global_limit].
//
// Whenever, a read RPC arrives at a node for the first time, it picks
// a local time based on the safe time of the node. This is the local limit
// for that node. Any writes that were replicated AFTER this local limit
// happen strictly after the user issued the read.
//
// The writes can occur
// 1. either as part of a fast path insert where the write is replicated
//    directly on the regular DB.
// 2. or as part of a distributed txn where the write is replicated on
//    the intents DB first.
//
// In either case, the time with which the write is replicated
// is considered for the happens after relationship. This means that
// the write contains the timestamp. For simplicity, we will call this
// timestamp, the intent time, even though this write could be directly
// to regular DB.
//
// Recalling the above discussion,
// When the intent time is more than the local limit, we can be certain
// that the write happened after the read started.
// Otherwise, the write could be ambiguous.
//
// Formally,
// 1. intent_time > local_limit => No read restart error.
// 2. intent_time <= local_limit => Read restart if commit time \in (read_time, global_limit]
//
// Notice that the condition: intent_time \in (read_time, local_limit],
// 1. When false can still raise a read restart error. For example,
//    intent time < local limit
//    && intent time < read time
//    && commit time \in (read_time, global_limit]
//    does not satisfy the condition but is ambiguous.
// 2. When true can still not raise a read restart error. For example,
//    intent time \in (read_time, local_limit]
//    && commit time > global_limit
//    is outside the ambiguity window and
//    should not raise a read restart error.
//
// However, in fast path writes, the intent time is the same as
// the commit time. Therefore, we can check whether
// intent time \in (read_time, local_limit].
//
// In the special case where the read time is the same or higher than
// the local limit, we see no read restart errors against fast path writes.
class PgLocalLimitOptimizationTest : public PgMiniTestBase {
 public:
  void SetUp() override {
    // So that read restart errors are not retried internally.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) =
        MaxQueryLayerRetriesConf(0);
    // Disable automatic tablet splitting to control the exact number
    // of tablets that we use.
    // The tests depend on the exact tablet distribution.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
    // Disable colocation so that dummy tables do not interfere with
    // the 'keys' table.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_colocate_database_by_default) = false;
    // Easier debugging.
    // ASSERT_OK(SET_FLAG(vmodule, "read_query=1,pgsql_operation=1,pg_client_session=3"));
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_log_statement) = "all";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_log_min_duration_statement) = 0;
    PgMiniTestBase::SetUp();
  }

 protected:
  // 100k is a good number in practice.
  static constexpr auto kNumInitialRows = 100000;
  // Chosen to ensure that the insert is concurrent with the read.
  // To elaborate, we ensure that the INSERT statement is issued after the
  // SELECT statement but the INSERT completes before the read reaches the
  // last row (the newly inserted one).
  // This is done to verify visibility of the inserted row.
  static constexpr auto kInsertDelay = 10;

  enum class ScanCmd {
    kOrdered, // Sequential RPCs to all tablets, read time is picked on docdb
              // of first tablet.
    kCount,   // Parallel RPCs from PG to all tablets, so read time is picked
              // on the PgClientSession proxy.
  };

  // Subroutine to execute a scan concurrently with an insert.
  //
  // This is to simulate read restart scenarios
  // where typically the scan starts before the insert
  // but the insert finishes before the scan.
  //
  // The insert commits at a time that is within the ambiguity window.
  // However, we expect the local limit mechanism to detect that
  // the insert is concurrent with the read and hence can be ignored.
  //
  // Read restart error is meant to prevent scenarios where the insert
  // completes before the SELECT statement is issued, but the SELECT
  // doesn't see the insert because it has a higher commit time due to
  // clock skew.
  //
  // However, in this case, the insert is concurrent with read. Therefore,
  // no read restart error is expected.
  //
  // Order of operations
  // -------------------
  // 1. Setup the table with some initial rows.
  //    We want enough rows so that the scan does not finish too quickly
  //    and the insert is actually concurrent with the select.
  //    100k rows is a good number for this purpose, in practice.
  // 2. Setup select and insert connections with catalog caches populated.
  // 3. Spawn a separate thread to execute the read query.
  // 4. Concurrently, wait for the read to start and then insert a row.
  // 5. Wait for the read to finish.
  //
  // As mentioned above, we expect no read restart errors since the
  // insert is concurrent with the read.
  void InsertRowConcurrentlyWithTableScan() {
    auto setup_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(setup_conn.Execute(Format(
      "CREATE TABLE keys (k INT, PRIMARY KEY(k ASC))$0",
      is_single_tablet_ ? "" : " SPLIT AT VALUES ((1))")));
    ASSERT_OK(setup_conn.Execute(Format(
      "INSERT INTO keys(k) SELECT GENERATE_SERIES(1, $0)", kNumInitialRows)));

    // Setup read connection and populate catalog cache.
    auto read_conn = ASSERT_RESULT(Connect());
    if (is_single_page_scan_) {
      // Force the scan in a single page ...
      ASSERT_OK(read_conn.Execute(Format(
        "SET yb_fetch_row_limit = $0", 2 * kNumInitialRows)));
    } else {
      // ... or multiple pages.
      ASSERT_OK(read_conn.Execute(Format(
        "SET yb_fetch_row_limit = $0", kNumInitialRows / 100)));
    }
    PopulateReadConnCache(read_conn);

    // Setup insert connection and populate catalog cache.
    // This inserts one additional row, so the number of rows
    // is now kNumInitialRows + 1.
    auto insert_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(insert_conn.Execute("INSERT INTO keys(k) VALUES (0)"));

    // Execute the read query.
    if (is_read_time_picked_before_table_scan_) {
      PickReadTime(read_conn);
    }
    CountDownLatch read_thread_started(1);
    auto table_scan = std::async(std::launch::async, [&]() {
      // Signal insert to proceed
      read_thread_started.CountDown();
      Stopwatch stopwatch;
      stopwatch.start();
      // We expect that there is no read restart error here
      // despite the concurrent insert.
      //
      // The test assumes that
      // The row is inserted before the SELECT reaches the end
      // of all keys. As a result, the read actually sees the newly
      // inserted row. However, the scan discards the inserted row
      // from the ambiguity window consideration for read-after-commit
      // guarantee. This is because the scan is aware that the insert
      // happened concurrently with the scan, from local limit.
      RunScanCmd(read_conn);
      stopwatch.stop();
      // Assert that the select ran for long enough for the insert to
      // finish before the select finished.
      EXPECT_GT(stopwatch.elapsed().wall_millis(), 3 * kInsertDelay);
    });

    read_thread_started.Wait();
    SleepFor(kInsertDelay * 1ms);
    ASSERT_OK(insert_conn.Execute(Format(
      "INSERT INTO keys(k) VALUES ($0)", 3 * kNumInitialRows)));

    table_scan.get();

    // Ensure that there is only a single tablet.
    auto num_tablets = ASSERT_RESULT(read_conn.FetchRow<int64_t>(
      "SELECT num_tablets FROM yb_table_properties('keys'::regclass)"));
    ASSERT_EQ(num_tablets, is_single_tablet_ ? 1 : 2);
    // Ensure that keys table is not colocated with any other table.
    auto is_colocated = ASSERT_RESULT(read_conn.FetchRow<bool>(
      "SELECT is_colocated FROM yb_table_properties('keys'::regclass)"));
    ASSERT_FALSE(is_colocated);
  }

  // Populate the catalog cache based on what command is run in RunScanCmd.
  void PopulateReadConnCache(PGConn &read_conn) {
    switch(scan_cmd_) {
      case ScanCmd::kOrdered: {
        auto rows = ASSERT_RESULT(read_conn.FetchRows<int32_t>(
          "SELECT k FROM keys ORDER BY k LIMIT 1"));
        ASSERT_EQ(rows.size(), 1);
      } break;
      case ScanCmd::kCount: {
        auto rows = ASSERT_RESULT(read_conn.FetchRows<int64_t>(
          "SELECT COUNT(*) FROM keys"));
        ASSERT_EQ(rows.size(), 1);
      } break;
    }
  }

  // Dispatch the scan command based on the scan_cmd enum.
  void RunScanCmd(PGConn &read_conn) {
    switch(scan_cmd_) {
      case ScanCmd::kOrdered: {
        auto rows = ASSERT_RESULT(read_conn.FetchRows<int32_t>(
          "SELECT k FROM keys ORDER BY k"));
        ASSERT_EQ(rows.size(), kNumInitialRows + 1);
      } break;
      case ScanCmd::kCount: {
        // In this case, the read time is picked locally at the query layer
        // and passed to the storage layer.
        auto count_rows = ASSERT_RESULT(read_conn.FetchRow<int64_t>(
          "SELECT COUNT(*) FROM keys"));
        ASSERT_EQ(count_rows, kNumInitialRows + 1);
      } break;
    }
  }

  void PickReadTime(PGConn &read_conn) {
    ASSERT_OK(read_conn.Execute("CREATE TABLE dummy()"));
    // We pick read time by starting a REPEATABLE READ transaction,
    // and executing a statement that picks a read time.
    ASSERT_OK(read_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    // ASSUMPTION: the statement does not touch the keys table
    // or its tablets.
    auto count_rows = ASSERT_RESULT(
      read_conn.FetchRow<int64_t>("SELECT COUNT(*) FROM dummy"));
    ASSERT_EQ(count_rows, 0);
  }

  bool is_single_tablet_ = true;
  bool is_single_page_scan_ = true;
  ScanCmd scan_cmd_ = ScanCmd::kOrdered;
  bool is_read_time_picked_before_table_scan_ = false;
};

// Single page scans never raise read restart errors.
//
// 1. When against fast path inserts
//   no read restart errors are raised
//   at the storage layer either. (This test).
// 2. When against distributed writes
//   the read RPC is retried internally at the storage layer
//   with advanced read time. However, the local limit
//   should not change after a restart.
//
// In a single page scan (and no distributed txn), the read time
// is picked by the storage layer. The storage layer will
// pick the same time for both read time and local limit.
//
// 1. If the local limit is smaller than the insert time, then the
// insert can no longer be in the ambiguity window
// (by definition of local limit).
// 2. Otherwise, if the local limit is larger than the insert time,
// then the read time is larger than the insert time as well.
// The scan will then observe the insert.
//
// Therefore, the fast path insert will never be within
// the ambiguity window of the single page scan. (This test).
//
// The fast path insert will also not be within the ambiguity
// window of a multi-page single-tablet scan.
TEST_F(PgLocalLimitOptimizationTest, SinglePageScan) {
  // Test Config
  is_single_tablet_ = true;
  is_single_page_scan_ = true;
  scan_cmd_ = ScanCmd::kOrdered;

  // Run Test
  InsertRowConcurrentlyWithTableScan();
}

// Before #22821, in a multi-page scan, for each subsequent page scan,
// the read time was set by pggate explicitly based on the used time
// returned by the response for the previous page.
//
// This behavior of overriding the read time also resets the per-tablet
// local limit map. There is no reason for pggate to send read time
// explicitly since the read time does not change across multiple pages.
//
// This test ensures that there is no read restart error just because the
// scan spans multiple pages. Fails without #22821.
TEST_F(PgLocalLimitOptimizationTest, MultiPageScan) {
  // Test Config
  is_single_tablet_ = true;
  is_single_page_scan_ = false;
  scan_cmd_ = ScanCmd::kOrdered;

  // Run Test
  InsertRowConcurrentlyWithTableScan();
}

// In a multi-tablet scan, the read time is
// 1. Either picked on the local tserver proxy. (This test).
// 2. Or picked on the first tserver that the scan hits.
//
// In either case, if the storage layer node receives a read RPC
// for the first time, we should set the local limit
// to the current safe time since the read RPC can ignore writes
// that occur after the read RPC arrives at the node. We can do this
// because the read RPC arrives only after the user issues a statement/query
// to YugabyteDB.
//
// This test ensures that we do not receive a read restart error
// when the insert arrives after the read RPC.
// Fails without fix for #22158.
TEST_F(PgLocalLimitOptimizationTest, ReadTimePickedOnLocalProxy) {
  // Test Config
  is_single_tablet_ = false;
  is_single_page_scan_ = true;
  scan_cmd_ = ScanCmd::kCount;

  // Run Test
  InsertRowConcurrentlyWithTableScan();
}

// This tests the scenario where the read time is picked on some remote
// node. We should still override the local limit as discussed above.
// Otherwise, we would receive a read restart error.
// Fails without fix for #22158.
TEST_F(PgLocalLimitOptimizationTest, ReadTimePickedOnRemoteNode) {
  // Test Config
  is_single_tablet_ = false;
  is_single_page_scan_ = true;
  scan_cmd_ = ScanCmd::kOrdered;

  // Run Test
  InsertRowConcurrentlyWithTableScan();
}

// We test the case where the read time is picked in a previous
// statement. Happens in a REPEATABLE READ transaction.
//
// In a REPEATABLE READ transaction, when the scan hits a different
// tablet than the previous statement, the local limit is picked anew.
// Fails without fix for #22158.
TEST_F(PgLocalLimitOptimizationTest, ReadTimePickedBeforeTableScan) {
  // Test Config
  is_single_tablet_ = true;
  is_single_page_scan_ = true;
  scan_cmd_ = ScanCmd::kOrdered;
  is_read_time_picked_before_table_scan_ = true;

  // Run Test
  InsertRowConcurrentlyWithTableScan();
}

} // namespace yb::pgwrapper
