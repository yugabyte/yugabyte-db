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
    .port = pg_ts->pgsql_rpc_port(),
    .dbname = db_name,
    .user = user
  }).Connect(simple_query_protocol);
}

Result<PGConn> LibPqTestBase::ConnectToTs(const ExternalTabletServer& pg_ts) {
  return PGConnBuilder({
    .host = pg_ts.bind_host(),
    .port = pg_ts.pgsql_rpc_port(),
  }).Connect();
}

Result<PGConn> LibPqTestBase::ConnectUsingString(
    const string& conn_str, CoarseTimePoint deadline, bool simple_query_protocol) {
  return PGConn::Connect(
    conn_str, deadline, simple_query_protocol, std::string() /* conn_str_for_log */);
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
  return conn->FetchValue<PGOid>(
      Format("SELECT oid FROM pg_database WHERE datname = '$0'", db_name));
}

void LibPqTestBase::BumpCatalogVersion(int num_versions, PGConn* conn) {
  LOG(INFO) << "Do " << num_versions << " breaking catalog version bumps";
  for (int i = 0; i < num_versions; ++i) {
    ASSERT_OK(conn->Execute("ALTER ROLE yugabyte SUPERUSER"));
  }
}

void LibPqTestBase::UpdateMiniClusterFailOnConflict(ExternalMiniClusterOptions* options) {
  // This test depends on fail-on-conflict concurrency control to perform its validation.
  // TODO(wait-queues): https://github.com/yugabyte/yugabyte-db/issues/17871
  options->extra_tserver_flags.push_back("--enable_wait_queues=false");
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

        ASSERT_OK(connection.Execute("BEGIN"));
        ASSERT_OK(connection.Execute("SET TRANSACTION ISOLATION LEVEL SERIALIZABLE"));

        auto res = connection.Fetch("SELECT * FROM t");
        if (!res.ok()) {
          // res will have failed status here for conflicting transactions as long as we are using
          // fail-on-conflict concurrency control. Hence this test runs with wait-on-conflict
          // disabled.
          ASSERT_TRUE(HasTransactionError(res.status())) << res.status();
          return;
        }
        auto columns = PQnfields(res->get());
        ASSERT_EQ(2, columns);

        auto lines = PQntuples(res->get());
        ASSERT_EQ(kKeys, lines);
        for (int j = 0; j != lines; ++j) {
          if (ASSERT_RESULT(GetValue<int32_t>(res->get(), j, 1)) == color) {
            continue;
          }

          auto key = ASSERT_RESULT(GetValue<int32_t>(res->get(), j, 0));
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

    auto res = ASSERT_RESULT(conn.Fetch("SELECT * FROM t"));
    auto columns = PQnfields(res.get());
    ASSERT_EQ(2, columns);

    auto lines = PQntuples(res.get());
    ASSERT_EQ(kKeys, lines);

    std::vector<int32_t> zeroes, ones;
    for (int i = 0; i != lines; ++i) {
      auto key = ASSERT_RESULT(GetValue<int32_t>(res.get(), i, 0));
      auto current = ASSERT_RESULT(GetValue<int32_t>(res.get(), i, 1));
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
