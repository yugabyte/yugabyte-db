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
#include "yb/yql/pgwrapper/libpq_test_base.h"

#include <string>

#include "yb/common/common.pb.h"
#include "yb/common/pgsql_error.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/size_literals.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

using std::string;

using namespace std::literals;

DECLARE_int64(external_mini_cluster_max_log_bytes);

namespace yb {
namespace pgwrapper {

void LibPqTestBase::SetUp() {
  // YSQL has very verbose logging in case of conflicts
  // TODO: reduce the verbosity of that logging.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_external_mini_cluster_max_log_bytes) = 512_MB;
  PgWrapperTestBase::SetUp();
}

Result<PGConn> LibPqTestBase::Connect(bool simple_query_protocol) {
  return ConnectToDB(std::string() /* db_name */, simple_query_protocol);
}

Result<PGConn> LibPqTestBase::ConnectToDB(const string& db_name, bool simple_query_protocol) {
  return ConnectToDBAsUser(db_name, PGConnSettings::kDefaultUser, simple_query_protocol);
}

Result<PGConn> LibPqTestBase::ConnectToDBAsUser(
    const string& db_name, const string& user, bool simple_query_protocol) {
  return PGConnBuilder({
    .host = pg_ts->bind_host(),
    .port = pg_ts->ysql_port(),
    .dbname = db_name,
    .user = user
  }).Connect(simple_query_protocol);
}

Result<PGConn> LibPqTestBase::ConnectToTs(const ExternalTabletServer& pg_ts) {
  return PGConnBuilder({
    .host = pg_ts.bind_host(),
    .port = pg_ts.ysql_port(),
  }).Connect();
}

Result<PGConn> LibPqTestBase::ConnectUsingString(
    const string& conn_str, CoarseTimePoint deadline, bool simple_query_protocol) {
  return PGConn::Connect(
    conn_str, deadline, simple_query_protocol, std::string() /* conn_str_for_log */);
}

Result<PGConn> LibPqTestBase::ConnectToDBWithReplication(const std::string& db_name) {
  return PGConnBuilder({
    .host = pg_ts->bind_host(),
    .port = pg_ts->pgsql_rpc_port(),
    .dbname = db_name,
    .replication = "database"
  }).Connect(true /* simple_query_protocol */);
}

bool LibPqTestBase::TransactionalFailure(const Status& status) {
  const uint8_t* pgerr = status.ErrorData(PgsqlErrorTag::kCategory);
  if (pgerr == nullptr) {
    return false;
  }
  YBPgErrorCode code = PgsqlErrorTag::Decode(pgerr);
  return code == YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE;
}

Result<PgOid> GetDatabaseOid(PGConn* conn, const std::string& db_name) {
  return conn->FetchRow<PGOid>(
      Format("SELECT oid FROM pg_database WHERE datname = '$0'", db_name));
}

// Bump catalog version num_bumps times using conn.  After each bump, wait for the new catalog
// version to propagate to conn in order to avoid catalog version mismatch errors.
// Prerequisites:
// - conn should not be in the middle of a transaction
// - there should be no other concurrent catalog version bumps
Status LibPqTestBase::BumpCatalogVersion(int num_bumps, PGConn* conn,
                                         const std::string& alter_value) {
  const auto query = "SELECT catalog_version FROM pg_stat_activity WHERE pid = pg_backend_pid()";
  auto initial_catalog_version = VERIFY_RESULT(conn->FetchRow<int64_t>(query));
  LOG(INFO) << "Do " << num_bumps << " breaking catalog version bumps starting at "
            << initial_catalog_version;
  if (alter_value.empty()) {
    for (int i = 1; i <= num_bumps; ++i) {
      RETURN_NOT_OK(IncrementAllDBCatalogVersions(
          *conn, IsBreakingCatalogVersionChange::kTrue /* is_breaking */));
      auto target_catalog_version = initial_catalog_version + i;
      RETURN_NOT_OK(LoggedWaitFor(
          [conn, target_catalog_version, &query]() -> Result<bool> {
            auto current_catalog_version = VERIFY_RESULT(conn->FetchRow<int64_t>(query));
            if (current_catalog_version == target_catalog_version) {
              return true;
            }
            if (current_catalog_version < target_catalog_version) {
              return false;
            }
            return STATUS_FORMAT(
                IllegalState,
                "unexpected catalog version $0 > target $1:"
                " does the test do concurrent DDLs without synchronization?",
                current_catalog_version, target_catalog_version);
          },
          10s,
          Format("wait for catalog version $0 to propagate", target_catalog_version)));
    }
    return Status::OK();
  }
  // Some tests cannot tolerate the added wait if using increment_all_db_catalog_versions.
  SCHECK_EQ(num_bumps, 1, InvalidArgument, "cannot bump more than one version with alter_value");
  return conn->ExecuteFormat("ALTER ROLE yugabyte $0", alter_value);
}

Result<std::string> GetPGVersionString(PGConn* conn) {
  return conn->FetchRow<string>("SELECT version()");
}

void LibPqTestBase::UpdateMiniClusterFailOnConflict(ExternalMiniClusterOptions* options) {
  // This test depends on fail-on-conflict concurrency control to perform its validation.
  // TODO(wait-queues): https://github.com/yugabyte/yugabyte-db/issues/17871
  options->extra_tserver_flags.push_back("--enable_wait_queues=false");
  // Set the max query layer retries to 2 to speed up the test.
  options->extra_tserver_flags.push_back("--ysql_pg_conf_csv=" + MaxQueryLayerRetriesConf(2));
}

// Test that repeats example from this article:
// https://blogs.msdn.microsoft.com/craigfr/2007/05/16/serializable-vs-snapshot-isolation-level/
//
// Multiple rows with values 0 and 1 are stored in table.
// Two concurrent transaction fetches all rows from table and does the following.
// First transaction changes value of all rows with value 0 to 1.
// Second transaction changes value of all rows with value 1 to 0.
// As outcome we should have rows with the same value.
//
// The described procedure is repeated multiple times to increase probability of catching bug,
// w/o running test multiple times.
// If min_duration_seconds > 0, the test will repeat the procedure until the test duration
// exceeds min_duration_seconds. This is used to ensure the test runs concurrently and overlaps
// with another transaction and verify the test result isn't affected by the other transaction.
void LibPqTestBase::SerializableColoringHelper(int min_duration_seconds) {
  constexpr auto kKeys = RegularBuildVsSanitizers(10, 20);
  constexpr auto kColors = 2;
  constexpr auto kIterations = 20;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, color INT)"));

  auto iterations_left = kIterations;
  auto start_time = MonoTime::Now();
  auto test_duration = MonoDelta::FromSeconds(min_duration_seconds);
  for (int iteration = 0; iterations_left > 0; ++iteration) {
    auto iteration_title = Format("Iteration: $0", iteration);
    SCOPED_TRACE(iteration_title);
    LOG(INFO) << iteration_title;

    auto s = conn.Execute("DELETE FROM t");
    if (!s.ok()) {
      ASSERT_TRUE(HasTransactionError(s)) << s;
      continue;
    }
    for (int k = 0; k != kKeys; ++k) {
      int32_t color = RandomUniformInt(0, kColors - 1);
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO t (key, color) VALUES ($0, $1)", k, color));
    }

    std::atomic<int> complete{ 0 };
    std::vector<std::thread> threads;
    for (int i = 0; i != kColors; ++i) {
      int32_t color = i;
      threads.emplace_back([this, color, kKeys, &complete] {
        auto connection = ASSERT_RESULT(Connect());

        // TODO(#12494): undo this change
        // ASSERT_OK(connection.Execute("BEGIN"));
        // ASSERT_OK(connection.Execute("SET TRANSACTION ISOLATION LEVEL SERIALIZABLE"));
        ASSERT_OK(connection.Execute("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE"));

        auto rows_res = connection.FetchRows<int32_t, int32_t>("SELECT * FROM t");
        if (!rows_res.ok()) {
          // res will have failed status here for conflicting transactions as long as we are using
          // fail-on-conflict concurrency control. Hence, this test runs with wait-on-conflict
          // disabled.
          ASSERT_TRUE(HasTransactionError(rows_res.status())) << rows_res.status();
          return;
        }
        ASSERT_EQ(rows_res->size(), kKeys);
        for (const auto& [key, row_color] : *rows_res) {
          if (row_color == color) {
            continue;
          }

          auto status = connection.ExecuteFormat(
              "UPDATE t SET color = $1 WHERE key = $0", key, color);
          if (!status.ok()) {
            auto msg = status.message().ToBuffer();
            ASSERT_TRUE(HasTransactionError(status)) << status;
            break;
          }
        }

        auto status = connection.Execute("COMMIT");
        if (!status.ok()) {
          ASSERT_EQ(PgsqlError(status), YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE) << status;
          return;
        }

        ++complete;
      });
    }

    for (auto& thread : threads) {
      thread.join();
    }

    if (complete == 0) {
      continue;
    }

    auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t>("SELECT * FROM t")));
    std::vector<int32_t> zeroes, ones;
    const auto rows_sz = rows.size();
    zeroes.reserve(rows_sz);
    ones.reserve(rows_sz);
    for (const auto& [key, current] : rows) {
      if (current == 0) {
        zeroes.push_back(key);
      } else {
        ones.push_back(key);
      }
    }

    std::sort(ones.begin(), ones.end());
    std::sort(zeroes.begin(), zeroes.end());

    LOG(INFO) << "Zeroes: " << yb::ToString(zeroes) << ", ones: " << yb::ToString(ones);
    ASSERT_TRUE(zeroes.empty() || ones.empty());

    --iterations_left;
    if (iterations_left > 0) {
      continue;
    }
    // If the caller asks for a minimum test duration, continue the next iteration.
    if (min_duration_seconds > 0) {
      auto time_passed = MonoTime::Now() - start_time;
      if (time_passed < test_duration) {
        LOG(INFO) << "Minimum test duration not reached yet: "
                  << time_passed << "/" << test_duration;
        iterations_left = 1;
      }
    }
  }
}

} // namespace pgwrapper
} // namespace yb
