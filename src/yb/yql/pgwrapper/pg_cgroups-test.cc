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

#ifdef __linux__

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_cgroup_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/cgroups.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using std::string;

DECLARE_bool(enable_qos);
DECLARE_string(postmaster_cgroup);
DECLARE_int32(TEST_slowdown_pgsql_aggregate_read_ms);

using namespace std::literals;

namespace yb::pgwrapper {

class PgCgroupsTest : public PgMiniTestBase {
 protected:
  Status CheckThreadIdsForPgProcess(Cgroup& cgroup, std::string_view process_name_prefix) {
    // Arbitrary, long enough.
    constexpr size_t kMaxProcessNameLength = 128;

    // We read thread ids (/cgroups.threads), but on Linux, thread ids and process ids share the
    // same namespace. And while thread names only set /proc/$TID/comm when setting thread name,
    // postgres sets /proc/$TID/cmdline (argv) for naming subprocesses
    // (e.g., "postgres: checkpointer").
    for (auto id : VERIFY_RESULT(cgroup.ReadThreadIds())) {
      int fd = open(Format("/proc/$0/cmdline", id).c_str(), O_RDONLY);
      if (fd == -1) {
        if (errno == ENOENT) {
          // Thread no longer exists, ignore.
          continue;
        }
        return STATUS_FROM_ERRNO("Reading cmdline for " + AsString(id), errno);
      }
      ScopeExit s([fd] { close(fd); });

      std::string out(kMaxProcessNameLength, '\0');
      VERIFY_ERRNO_FN_CALL(read, fd, out.data(), kMaxProcessNameLength);

      // /proc/$ID/cmdline format is <arg0><NUL><arg1><NUL>..., and we care only about arg0, so
      // reading `out` as a c-string works to drop the parts we don't care about.
      auto name = std::string_view{out.c_str()};
      LOG(INFO) << "Thread id: " << id << ", cmdline[0]: " << name;
      if (name.substr(0, process_name_prefix.size()) == process_name_prefix) {
        return Status::OK();
      }
    }

    return STATUS_FORMAT(
        NotFound, "no thread with cmdline[0] starting with '$0'", process_name_prefix);
  }

  Status CheckThreadIdsForThreadName(Cgroup& cgroup, std::string_view thread_name_prefix) {
    for (const auto& name : VERIFY_RESULT(cgroup.ReadThreadNames())) {
      LOG(INFO) << "Thread name: " << name;
      if (name.substr(0, thread_name_prefix.size()) == thread_name_prefix) {
        return Status::OK();
      }
    }
    return STATUS_FORMAT(NotFound, "no thread with name starting with '$0'", thread_name_prefix);
  }

  size_t NumTabletServers() override {
    return 1;
  }
};

class PgPostmasterCgroupTest : public PgCgroupsTest {
 protected:
  void SetUp() override {
    auto postmaster_cgroup = Format("$0/postmaster", ASSERT_RESULT(GetProcessCpuCgroupPath()));
    ASSERT_OK(STATUS_FROM_ERRNO_IF_NONZERO_RV(
        Format("mkdir($0)", postmaster_cgroup), mkdir(postmaster_cgroup.c_str(), 0755)));
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_postmaster_cgroup) = postmaster_cgroup;
    PgCgroupsTest::SetUp();
  }
};

TEST_F_EX(PgCgroupsTest, TestPostmasterCgroupFlag, PgPostmasterCgroupTest) {
  ASSERT_OK(WaitFor(
      [this] { return pg_supervisor_->GetState() == YbSubProcessState::kRunning; },
      60s, "Wait for postmaster startup"));
  auto postmaster_pid = pg_supervisor_->ProcessId();
  ASSERT_TRUE(postmaster_pid.has_value());

  ASSERT_OK(Connect());
  auto actual_cgroup = ASSERT_RESULT(GetProcessCpuCgroupPath(
      *postmaster_pid, /*check_controllers=*/false));
  LOG(INFO) << "Postmaster cgroup: " << actual_cgroup << ", expected: " << FLAGS_postmaster_cgroup;
  ASSERT_EQ(actual_cgroup, FLAGS_postmaster_cgroup);
}

class PgQosCgroupsTest : public PgCgroupsTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_qos) = true;
    ASSERT_OK(SetupCgroupManagement(ClearChildCgroups::kTrue));
    PgCgroupsTest::SetUp();
  }

  Result<Cgroup&> db_cgroup(PGConn& conn) {
    auto& tserver = *cluster_->mini_tablet_server(0)->server();
    auto yugabyte_db_oid = VERIFY_RESULT(GetDatabaseOid(&conn, "yugabyte"));
    return tserver.cgroup_manager()->CgroupForDb(yugabyte_db_oid);
  }
};

TEST_F_EX(PgCgroupsTest, TestQosBackend, PgQosCgroupsTest) {
  auto conn = ASSERT_RESULT(Connect());
  auto& cgroup = ASSERT_RESULT_REF(db_cgroup(conn));
  ASSERT_OK(CheckThreadIdsForPgProcess(cgroup, "postgres: postgres yugabyte"));
  ASSERT_OK(CheckThreadIdsForThreadName(cgroup, "shmem_exchange_"));
}

TEST_F_EX(PgCgroupsTest, TestQosParallelWorkers, PgQosCgroupsTest) {
  auto conn = ASSERT_RESULT(Connect());
  auto& cgroup = ASSERT_RESULT_REF(db_cgroup(conn));

  ASSERT_OK(conn.Execute(R"#(
      CREATE TABLE test (id INT NOT NULL, value TEXT)
      PARTITION BY RANGE (id)
  )#"));
  ASSERT_OK(conn.Execute(R"#(
      CREATE TABLE test_p1 PARTITION OF test
      FOR VALUES FROM (1) TO (10001)
  )#"));
  ASSERT_OK(conn.Execute(R"#(
      CREATE TABLE test_p2 PARTITION OF test
      FOR VALUES FROM (10001) TO (20001)
  )#"));
  ASSERT_OK(conn.Execute(R"#(
      INSERT INTO test (id, value)
      SELECT g, 'data_' || g FROM generate_series(1, 20000) g;
  )#"));
  ASSERT_OK(conn.Execute("ANALYZE test"));
  ASSERT_OK(conn.Execute("SET max_parallel_workers_per_gather = 2"));
  ASSERT_OK(conn.Execute("SET min_parallel_table_scan_size = 0"));
  ASSERT_OK(conn.Execute("SET parallel_setup_cost = 0;"));
  ASSERT_OK(conn.Execute("SET parallel_tuple_cost = 0;"));
  ASSERT_OK(conn.Execute("SET enable_parallel_append = on;"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_slowdown_pgsql_aggregate_read_ms) = 5'000;
  std::thread t{[&conn] {
    ASSERT_OK(conn.Fetch("SELECT COUNT(*) FROM test"));
  }};
  ScopeExit join([&t] { t.join(); });

  ASSERT_OK(WaitFor([&] {
    return CheckThreadIdsForPgProcess(cgroup, "postgres: parallel worker for").ok();
  }, 5s, "Wait for parallel worker in database cgroup"));
}

TEST_F_EX(PgCgroupsTest, TestQosRead, PgQosCgroupsTest) {
  auto conn = ASSERT_RESULT(Connect());
  auto& cgroup = ASSERT_RESULT_REF(db_cgroup(conn));

  ASSERT_OK(conn.Execute("CREATE TABLE test (id INT NOT NULL)"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_slowdown_pgsql_aggregate_read_ms) = 5'000;
  std::thread t{[&conn] {
    ASSERT_OK(conn.Fetch("SELECT COUNT(*) FROM test"));
  }};
  ScopeExit join([&t] { t.join(); });

  ASSERT_OK(WaitFor([&] {
    return CheckThreadIdsForThreadName(cgroup, "TabletServer_pool_").ok();
  }, 5s, "Wait for RPC servicer thread in database cgroup"));
}

TEST_F_EX(PgCgroupsTest, TestQosWrite, PgQosCgroupsTest) {
  auto conn = ASSERT_RESULT(Connect());
  auto& cgroup = ASSERT_RESULT_REF(db_cgroup(conn));

  ASSERT_OK(conn.Execute("CREATE TABLE test (id INT NOT NULL)"));

  std::thread t{[&conn] {
    ASSERT_OK(conn.Execute("INSERT INTO test SELECT generate_series(1, 50000)"));
  }};
  ScopeExit join([&t] { t.join(); });

  ASSERT_OK(WaitFor([&] {
    return CheckThreadIdsForThreadName(cgroup, "TabletServer_pool_").ok();
  }, 5s, "Wait for RPC servicer thread in database cgroup"));
}

} // namespace yb::pgwrapper

#endif // __linux__
