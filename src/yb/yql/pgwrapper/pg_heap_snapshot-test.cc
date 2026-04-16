//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include <chrono>

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

#include "yb/yql/pggate/ybc_pggate.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

DECLARE_int32(heartbeat_interval_ms);
DECLARE_bool(ysql_yb_enable_invalidation_messages);

using std::string;

using namespace std::chrono_literals;

namespace yb::pgwrapper {

class PgHeapSnapshotTest : public PgMiniTestBase {
 public:
  void SetUp() override {
    if (CURRENT_TEST_CASE_AND_TEST_NAME_STR() == "PgHeapSnapshotTest.TestYsqlHeapSnapshotSimple") {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_invalidation_messages) = false;
    }
    LOG(INFO) << "FLAGS_ysql_yb_enable_invalidation_messages: "
              << FLAGS_ysql_yb_enable_invalidation_messages;
    PgMiniTestBase::SetUp();
  }

 protected:
  auto PgConnect(const std::string& username) {
    auto settings = MakeConnSettings();
    settings.user = username;
    return pgwrapper::PGConnBuilder(settings).Connect();
  }
};

TEST_F(PgHeapSnapshotTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYsqlHeapSnapshotSimple)) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  ASSERT_OK(conn1.Execute("CREATE TABLE t1 (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn1.Execute("CREATE TABLE t2 (k INT PRIMARY KEY, v INT)"));

  // Turn on tcmalloc sampling on conn1.
  ASSERT_OK(conn1.Execute("SET yb_tcmalloc_sample_period = '1kB'"));

  // Trigger several cache refreshes by doing DDLs on conn2 followed by a DML on conn1.
  for (int i = 2; i <= 6; i++) {
    ASSERT_OK(conn2.Execute(Format("ALTER TABLE t1 ADD COLUMN v$0 INT", i)));
    // Sleep for 2 heartbeats to ensure that the catalog version has been propagated.
    std::this_thread::sleep_for(FLAGS_heartbeat_interval_ms * 1ms * 2);
    ASSERT_RESULT(conn1.Fetch("SELECT * FROM t2"));
  }

  // Check the YSQL heap snapshot for conn1.
  auto res = ASSERT_RESULT(conn1.Fetch("SELECT * FROM yb_backend_heap_snapshot()"));
  ASSERT_GT(PQntuples(res.get()), 0) << "Expected more than 0 rows from yb_backend_heap_snapshot";

  // Set the sample frequency to 8 bytes for more granular sampling
  ASSERT_OK(conn1.Execute("SET yb_tcmalloc_sample_period = '8B'"));

  // Trigger several cache refreshes by doing DDLs on conn2 followed by a DML on conn1.
  for (int i = 2; i <= 6; i++) {
    ASSERT_OK(conn2.Execute(Format("ALTER TABLE t1 DROP COLUMN v$0", i)));
    // Sleep for 2 heartbeats to ensure that the catalog version has been propagated.
    std::this_thread::sleep_for(FLAGS_heartbeat_interval_ms * 1ms * 2);
    ASSERT_RESULT(conn1.Fetch("SELECT * FROM t2"));
  }

  // Check the YSQL heap snapshot again with the new sampling frequency
  res = ASSERT_RESULT(conn1.Fetch("SELECT * FROM yb_backend_heap_snapshot()"));
  ASSERT_GT(PQntuples(res.get()), 10)
      << "Expected more than 10 rows from yb_backend_heap_snapshot with 8B sampling";

  ASSERT_OK(conn1.Execute("DROP TABLE t1"));
  ASSERT_OK(conn1.Execute("DROP TABLE t2"));
}

TEST_F(PgHeapSnapshotTest, TestYsqlHeapSnapshotPermissions) {
  auto conn1 = ASSERT_RESULT(Connect());

  ASSERT_OK(conn1.Execute("CREATE USER test_user"));

  auto conn2 = ASSERT_RESULT(PgConnect("test_user"));

  auto res = conn2.Fetch("SELECT * FROM yb_backend_heap_snapshot()");
  ASSERT_TRUE(!res.ok()) << "Expected an error when executing yb_backend_heap_snapshot() "
                         << "with insufficient permissions";
}

TEST_F(PgHeapSnapshotTest, TestYsqlLogHeapSnapshot) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  // Get the backend PID from conn1
  auto pid = ASSERT_RESULT(conn1.FetchRow<PGUint32>("SELECT pg_backend_pid()"));
  ASSERT_GT(pid, 0) << "Expected a valid PID";

  // Call yb_log_backend_heap_snapshot from conn2 targeting conn1's PID
  auto return_value =
      ASSERT_RESULT(conn2.FetchRow<bool>(Format("SELECT yb_log_backend_heap_snapshot($0)", pid)));
  ASSERT_EQ(return_value, true) << "Expected yb_log_backend_heap_snapshot to return true";

  // Test calling with pg_backend_pid()
  return_value = ASSERT_RESULT(
      conn2.FetchRow<bool>(Format("SELECT yb_log_backend_heap_snapshot(pg_backend_pid())")));
  ASSERT_EQ(return_value, true)
      << "Expected yb_log_backend_heap_snapshot to return true when targeting the current process";

  // Also test peak heap snapshot
  return_value = ASSERT_RESULT(
      conn2.FetchRow<bool>(Format("SELECT yb_log_backend_heap_snapshot_peak($0)", pid)));
  ASSERT_EQ(return_value, true) << "Expected yb_log_backend_heap_snapshot_peak to return true";

  // Test with invalid PID
  return_value =
      ASSERT_RESULT(conn2.FetchRow<bool>(Format("SELECT yb_log_backend_heap_snapshot(0)")));
  ASSERT_EQ(return_value, false)
      << "Expected yb_log_backend_heap_snapshot to return false for invalid PID";
}

TEST_F(PgHeapSnapshotTest, TestYsqlLogHeapSnapshotPermissions) {
  // Create superuser connections
  auto conn_superuser1 = ASSERT_RESULT(PgConnect("yugabyte"));
  auto conn_superuser2 = ASSERT_RESULT(PgConnect("yugabyte"));
  auto superuser_pid = ASSERT_RESULT(conn_superuser1.FetchRow<PGUint32>("SELECT pg_backend_pid()"));
  ASSERT_GT(superuser_pid, 0) << "Expected a valid PID";


  // Create test users
  ASSERT_OK(conn_superuser1.Execute("CREATE USER test_user1"));
  ASSERT_OK(conn_superuser1.Execute("CREATE USER test_user2"));

  // Connect as test_user1
  auto conn_test1 = ASSERT_RESULT(PgConnect("test_user1"));
  auto test_user1_pid = ASSERT_RESULT(conn_test1.FetchRow<PGUint32>("SELECT pg_backend_pid()"));
  ASSERT_GT(test_user1_pid, 0) << "Expected a valid PID";

  // Connect as test_user2
  auto conn_test2 = ASSERT_RESULT(PgConnect("test_user2"));
  auto test_user2_pid = ASSERT_RESULT(conn_test2.FetchRow<PGUint32>("SELECT pg_backend_pid()"));
  ASSERT_GT(test_user2_pid, 0) << "Expected a valid PID";

  // Test logging the superuser's heap snapshot with test_user1
  // Should fail due to insufficient permissions
  auto res =
      conn_test1.FetchRow<bool>(Format("SELECT yb_log_backend_heap_snapshot($0)", superuser_pid));

  ASSERT_TRUE(!res.ok()) << "Expected an error when executing yb_log_backend_heap_snapshot() "
                         << "with insufficient permissions";

  // Test logging a superuser's heap snapshot with another superuser backend
  auto return_value = ASSERT_RESULT(conn_superuser2.FetchRow<bool>(
      Format("SELECT yb_log_backend_heap_snapshot($0)", superuser_pid)));
  ASSERT_EQ(return_value, true)
      << "Expected yb_log_backend_heap_snapshot to return true for superuser";

  // Test logging test_user2's heap snapshot with test_user1
  // Should fail due to insufficient permissions
  res = conn_test1.FetchRow<bool>(
    Format("SELECT yb_log_backend_heap_snapshot($0)", test_user2_pid));
  ASSERT_TRUE(!res.ok()) << "Expected an error when executing yb_log_backend_heap_snapshot() "
                         << "on different user's backend";

  // Grant test_user1 superuser privileges
  ASSERT_OK(conn_superuser1.Execute("ALTER USER test_user1 SUPERUSER"));

  // Test logging test_user2's heap snapshot with test_user1 - should succeed now
  res = conn_test1.FetchRow<bool>(
    Format("SELECT yb_log_backend_heap_snapshot($0)", test_user2_pid));
  ASSERT_TRUE(res.ok()) << "Expected success when executing yb_log_backend_heap_snapshot() "
                        << "with same role permissions";
}

}  // namespace yb::pgwrapper
