// Copyright (c) Yugabyte, Inc.
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

#include <cmath>
#include <cstdlib>

#include "yb/common/pg_types.h"
#include "yb/common/wire_protocol.h"
#include "yb/master/master_admin.proxy.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/curl_util.h"
#include "yb/util/faststring.h"
#include "yb/util/logging.h"
#include "yb/util/random_util.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"

// Usage: yb_build.sh ... --test-args --verbose=true
DEFINE_NON_RUNTIME_bool(verbose, false, "Add certain verbose logging");

using namespace std::chrono_literals;

namespace yb {
namespace pgwrapper {

class PgBackendsTest : public LibPqTestBase {
 public:
  void SetUp() override {
    LibPqTestBase::SetUp();

    client_ = ASSERT_RESULT(cluster_->CreateClient());
    conn_ = std::make_unique<PGConn>(ASSERT_RESULT(ConnectToDB("yugabyte")));

    const auto yugabyte_db_oid = ASSERT_RESULT(GetDatabaseOid(conn_.get(), "yugabyte"));
    catalog_version_db_oid_ = ASSERT_RESULT(conn_->FetchValue<PGOid>(Format(
        "SELECT db_oid FROM pg_yb_catalog_version "
        "WHERE db_oid in ($0, 1) ORDER BY db_oid DESC LIMIT 1", yugabyte_db_oid)));
  }

  int GetNumMasters() const override {
    return 1;
  }

  int GetNumTabletServers() const override {
    return 1;
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.insert(
        options->extra_master_flags.end(),
        {
          Format("--catalog_manager_bg_task_wait_ms=$0", kCatalogManagerBgTaskWaitMs),
          Format("--master_ts_rpc_timeout_ms=$0", kMasterTsRpcTimeoutSec * 1000),
          "--replication_factor=1",
          "--TEST_master_ui_redirect_to_leader=false",
        });
    options->extra_tserver_flags.emplace_back(
        "--ysql_yb_disable_wait_for_backends_catalog_version=false");
    if (FLAGS_verbose) {
      options->extra_master_flags.insert(
          options->extra_master_flags.end(),
          {
            "--log_ysql_catalog_versions=true",
            "--vmodule=ysql_backends_manager=2"
            ",master_heartbeat_service=2"
            ",transaction_coordinator=2"
            ",transaction_participant=2",
          });
      options->extra_tserver_flags.insert(
          options->extra_tserver_flags.end(),
          {
            "--log_ysql_catalog_versions=true",
            "--vmodule=tablet_service=2",
          });
    }
  }

 protected:
  PgOid catalog_version_db_oid_ = kPgInvalidOid;

  void BumpCatalogVersion(int num_versions) {
    LibPqTestBase::BumpCatalogVersion(num_versions, conn_.get());
  }

  Result<uint64_t> GetCatalogVersion() {
    return GetCatalogVersion(conn_.get());
  }
  Result<uint64_t> GetCatalogVersion(PGConn* conn) {
    // The use of catalog_version_db_oid_ makes the query work for both global catalog version
    // mode and per-db catalog version mode. We assume conn is connected to database 'yugabyte'.
    return conn->FetchValue<PGUint64>(
        Format("SELECT current_version FROM pg_yb_catalog_version WHERE "
               "db_oid = $0", catalog_version_db_oid_));
  }

  Result<std::string> GetJobs(ExternalMaster* master = nullptr) {
    if (!master) {
      master = cluster_->GetLeaderMaster();
    }
    const std::string url = Format(
        "http://$0/tasks",
        HostPort("localhost", master->http_port()).ToString());
    EasyCurl curl;
    faststring dst;
    RETURN_NOT_OK(curl.FetchURL(url, &dst));

    // Extract relevant substring.
    auto html = dst.ToString();
    auto begin = html.find("<tr><th>Job Name");
    SCHECK_NE(begin, std::string::npos, InternalError, "Did not find substring");
    auto end = html.find("</table>", begin);
    SCHECK_NE(end, std::string::npos, InternalError, "Did not find substring");

    auto jobs_html = html.substr(begin, (end - begin));
    LOG(INFO) << "JOBS:\n" << jobs_html;
    return jobs_html;
  }

  void CheckJobCount(uint32_t expected_num_jobs, ExternalMaster* master = nullptr) {
    // The first line in the jobs html is for the jobs table schema (names of the columns of the
    // table).  The remaining lines are for each job.  Assume that jobs are all "Backends Catalog
    // Version".
    auto jobs_html = ASSERT_RESULT(GetJobs(master));
    auto num_jobs = std::count(jobs_html.begin(), jobs_html.end(), '\n') - 1;
    ASSERT_EQ(expected_num_jobs, num_jobs);
  }

  std::unique_ptr<client::YBClient> client_;
  std::unique_ptr<PGConn> conn_;
  static constexpr int kCatalogManagerBgTaskWaitMs = 1000;
  static constexpr int kMasterTsRpcTimeoutSec = 30;
};

// Requests on already-satisfied versions should create jobs that finish quickly.
TEST_F(PgBackendsTest, AlreadySatisfiedVersion) {
  BumpCatalogVersion(2);

  uint64_t master_catalog_version = ASSERT_RESULT(GetCatalogVersion());
  LOG(INFO) << "Got master catalog version " << master_catalog_version;

  for (uint64_t i = 1; i <= master_catalog_version; ++i) {
    LOG(INFO) << "Testing version " << i;
    auto num_backends = ASSERT_RESULT(client_->WaitForYsqlBackendsCatalogVersion("yugabyte", i));
    ASSERT_EQ(0, num_backends);
    CheckJobCount(narrow_cast<uint32_t>(i));
  }
}

// Requests on cached versions should not create jobs.
TEST_F(PgBackendsTest, CachedVersion) {
  BumpCatalogVersion(2);

  uint64_t master_catalog_version = ASSERT_RESULT(GetCatalogVersion());
  LOG(INFO) << "Got master catalog version " << master_catalog_version;

  for (uint64_t i = master_catalog_version; i > 0; --i) {
    LOG(INFO) << "Testing version " << i;
    // Use timeout of zero for all requests besides the first because those should return success
    // immediately due to cached version.
    auto num_backends = ASSERT_RESULT(client_->WaitForYsqlBackendsCatalogVersion(
        "yugabyte", i, (i == master_catalog_version ? MonoDelta()
                                                    : MonoDelta::kZero) /* timeout */));
    ASSERT_EQ(0, num_backends);
    CheckJobCount(1);
  }
}

// Requests on a future version should be rejected.  If they were accepted, master would be
// busy-waiting, and it would not be easy to cancel the job.
TEST_F(PgBackendsTest, FutureVersion) {
  // Use timeout of zero because invalid argument should return immediately.
  auto res = client_->WaitForYsqlBackendsCatalogVersion(
      "yugabyte", 999, MonoDelta::kZero /* timeout */);
  ASSERT_NOK(res);
  const Status& s = res.status();
  ASSERT_TRUE(s.IsInvalidArgument()) << s;
  auto msg = s.message().ToBuffer();
  ASSERT_TRUE(msg.find("Requested catalog version is too high") != std::string::npos) << s;
}

// If usable cached version is not found but usable cached job is, a new job should not be created.
TEST_F(PgBackendsTest, CachedJob) {
  uint64_t master_catalog_version = ASSERT_RESULT(GetCatalogVersion());
  LOG(INFO) << "Got master catalog version " << master_catalog_version;

  TestThreadHolder thread_holder;
  constexpr int kNumThreads = 5;
  CountDownLatch latch(kNumThreads + 1);
  for (int i = 0; i < kNumThreads; ++i) {
    thread_holder.AddThreadFunctor([this, &latch, i, master_catalog_version] {
          latch.CountDown();
          latch.Wait();
          LOG(INFO) << "Launch wait: thread " << i;
          auto num_backends = ASSERT_RESULT(
              client_->WaitForYsqlBackendsCatalogVersion("yugabyte", master_catalog_version + 1));
          // Still waiting on the "BEGIN" backend.
          ASSERT_EQ(1, num_backends);
        });
  }

  LOG(INFO) << "Begin transaction on different connection";
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("BEGIN"));

  BumpCatalogVersion(1);
  latch.CountDown();

  thread_holder.JoinAll();

  LOG(INFO) << "Checking cached job is used";
  CheckJobCount(1);
}

// Check the backends counting on a tserver.
TEST_F(PgBackendsTest, WaitAllBackends) {
  const uint64_t orig_cat_ver = ASSERT_RESULT(GetCatalogVersion());
  LOG(INFO) << "Got master catalog version " << orig_cat_ver;

  std::vector<PGConn> conns;
  constexpr int kNumBackends = 5;
  for (int i = 0; i < kNumBackends; ++i) {
    PGConn conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute("BEGIN"));
    conns.push_back(std::move(conn));
  }
  ASSERT_EQ(kNumBackends, conns.size());

  BumpCatalogVersion(1);
  const uint64_t cat_ver = ASSERT_RESULT(GetCatalogVersion());
  ASSERT_EQ(orig_cat_ver + 1, cat_ver);

  while (conns.size() > 0) {
    auto num_backends = ASSERT_RESULT(
        client_->WaitForYsqlBackendsCatalogVersion("yugabyte", cat_ver));
    ASSERT_EQ(conns.size(), num_backends);

    LOG(INFO) << "Resolve backend " << conns.size();
    ASSERT_OK(conns.back().Execute("COMMIT"));
    conns.pop_back();
  }
  auto num_backends = ASSERT_RESULT(
      client_->WaitForYsqlBackendsCatalogVersion("yugabyte", cat_ver));
  ASSERT_EQ(0, num_backends);
}

// Check that the backends counting ignores other databases.
TEST_F(PgBackendsTest, WaitOnlySameDatabase) {
  std::vector<PGConn> conns;
  std::vector<std::string> db_names = {"postgres", "template1", "yugabyte"};
  std::mt19937 rng{std::random_device()()};
  std::shuffle(db_names.begin(), db_names.end(), rng);
  constexpr int kNumDb = 3;

  // Connect to each database: nth database gets n connections.
  LOG(INFO) << "Connect to each database";
  for (int i = 0; i < kNumDb; ++i) {
    LOG(INFO) << "Connect " << i + 1 << " times to database " << db_names[i];
    for (int j = 0; j <= i; ++j) {
      conns.push_back(ASSERT_RESULT(ConnectToDB(db_names[i])));
    }
  }
  // Sum 1 to kNumDb using formula.  Ensure there are that many connections.
  ASSERT_EQ((kNumDb * (kNumDb + 1)) / 2, conns.size());

  LOG(INFO) << "Begin transaction on each connection";
  for (auto& conn : conns) {
    ASSERT_OK(conn.Execute("BEGIN"));
  }

  LOG(INFO) << "Bump catalog version for each database";
  for (const auto& db_name : db_names) {
    PGConn conn = ASSERT_RESULT(ConnectToDB(db_name));
    LibPqTestBase::BumpCatalogVersion(1, &conn);
  }
  const uint64_t cat_ver = ASSERT_RESULT(GetCatalogVersion());

  LOG(INFO) << "Check wait on each database";
  for (int i = 0; i < kNumDb; ++i) {
    auto num_backends = ASSERT_RESULT(
        client_->WaitForYsqlBackendsCatalogVersion(db_names[i], cat_ver));
    ASSERT_EQ(i + 1, num_backends) << "Unexpected num backends for database " << db_names[i];
  }
}

// Test that multiple waiters immediately resolve.  This tests the condition variable broadcast in
// the implementation.
TEST_F(PgBackendsTest, MultipleWaiters) {
  PGConn conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("BEGIN"));

  BumpCatalogVersion(1);
  const auto cat_ver = ASSERT_RESULT(GetCatalogVersion());
  LOG(INFO) << "Got catalog version " << cat_ver;

  TestThreadHolder thread_holder;
  constexpr int kNumThreads = 5;
  CountDownLatch latch(kNumThreads + 1);
  MonoTime start_time;
  std::array<MonoDelta, kNumThreads> durations;
  for (int i = 0; i < kNumThreads; ++i) {
    thread_holder.AddThreadFunctor(
        [this, i, cat_ver, &latch, &start_time, &durations]() {
          latch.CountDown();
          latch.Wait();
          Result<int> res = client_->WaitForYsqlBackendsCatalogVersion(
              "yugabyte", cat_ver, 1min /* timeout */);
          durations[i] = MonoTime::Now() - start_time;
          LOG(INFO) << "thread " << i << " got res " << res;
          ASSERT_OK(res);
          ASSERT_EQ(0, *res);
        });
  }

  latch.CountDown();
  latch.Wait();
  constexpr auto kSleepTime = 5s;
  SleepFor(kSleepTime);
  start_time = MonoTime::Now();
  ASSERT_OK(conn.Execute("COMMIT"));

  thread_holder.JoinAll();
  // Check mean.  Use a high margin because it takes some time for the information to propagate.
  const auto mean = std::accumulate(durations.begin(),
                                    durations.end(),
                                    MonoDelta::kZero) / kNumThreads;
  LOG(INFO) << "Average duration: " << mean;
  constexpr auto kMargin = RegularBuildVsDebugVsSanitizers(3s, 5s, 5s);
  EXPECT_LT(mean, kMargin);
  // Check deviation.  This is the more important check because it shows that the waiters all
  // resolved at the same time.
  for (const auto& duration : durations) {
    const auto deviation = MonoDelta::FromNanoseconds(
        std::abs(duration.ToNanoseconds() - mean.ToNanoseconds()));
    LOG(INFO) << "duration: " << duration << ", deviation: " << deviation;
    constexpr auto kMaxDeviation = RegularBuildVsDebugVsSanitizers(30ms, 50ms, 50ms);
    EXPECT_LT(deviation, kMaxDeviation);
  }
}

class PgBackendsTestConnLimit : public PgBackendsTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgBackendsTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back(
        "--ysql_pg_conf_csv=max_connections=2,superuser_reserved_connections=0,max_wal_senders=0");
  }
};

TEST_F_EX(PgBackendsTest, ConnectionLimit, PgBackendsTestConnLimit) {
  LOG_WITH_FUNC(INFO) << "Beginning test";
  const uint64_t cat_ver = ASSERT_RESULT(GetCatalogVersion());
  // zero is initially cached by the YsqlBackendsManager, so a version of zero means the following
  // WaitForYsqlBackendsCatalogVersion wouldn't create a job and cause connections to be created.
  // We want connections to be created to test hitting the connection limit.
  ASSERT_NE(cat_ver, 0);

  // Make a second connection, the first being the class conn_.  Since the max is 2, this takes up
  // all connections.
  LOG(INFO) << "Make a second connection";
  {
    PGConn conn = ASSERT_RESULT(Connect());

    // WaitForYsqlBackendsCatalogVersion should fail because there are no available connections it
    // can use.
    LOG(INFO) << "Launch wait for backends catalog version " << cat_ver;
    Result<int> res = client_->WaitForYsqlBackendsCatalogVersion(
        "yugabyte",
        cat_ver,
        MonoDelta::FromSeconds(kMasterTsRpcTimeoutSec) + 5s /* timeout */);
    ASSERT_NOK(res);
    Status s = res.status();
    ASSERT_TRUE(s.IsCombined()) << s;
    ASSERT_STR_CONTAINS(s.message().ToBuffer(), "job is failed");

    LOG(INFO) << "Destroy the second connection";
  }

  LOG(INFO) << "Second attempt should work";
  auto num_backends = ASSERT_RESULT(
      client_->WaitForYsqlBackendsCatalogVersion("yugabyte", cat_ver));
  ASSERT_EQ(0, num_backends);
}

class PgBackendsTestPgTimeout : public PgBackendsTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgBackendsTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.insert(
        options->extra_tserver_flags.end(),
        {
          Format("--wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms=$0",
                 kRpcTimeout.ToMilliseconds()),
          Format("--ysql_yb_wait_for_backends_catalog_version_timeout=$0",
                 kTimeout.ToMilliseconds()),
        });
  }

 protected:
  const MonoDelta kRpcTimeout = 1s;
  const MonoDelta kTimeout = 3s;
};

// Test ysql_yb_wait_for_backends_catalog_version_timeout.
TEST_F_EX(PgBackendsTest,
          PgTimeout,
          PgBackendsTestPgTimeout) {
  LOG(INFO) << "Start connection that will be behind";
  PGConn conn_begin = ASSERT_RESULT(Connect());
  ASSERT_OK(conn_begin.Execute("BEGIN"));

  LOG(INFO) << "Start create index connection";
  PGConn conn_index = ASSERT_RESULT(Connect());
  ASSERT_OK(conn_index.Execute("CREATE TABLE t (i int)"));

  LOG(INFO) << "Do create index";
  const auto start = MonoTime::Now();
  Status s = conn_index.Execute("CREATE INDEX ON t (i)");
  const auto end = MonoTime::Now();
  ASSERT_NOK(s);
  ASSERT_TRUE(s.IsNetworkError()) << s;
  LOG(INFO) << "Error message: " << s.message().ToBuffer();
  ASSERT_STR_CONTAINS(s.message().ToBuffer(), "timed out waiting for postgres backends");

  const auto time_spent = end - start;
  LOG(INFO) << "Time spent: " << time_spent;
  // Add margin to cover time spent in setup (and teardown), such as creating the docdb index.  The
  // timeout timer starts on the first wait-for-backends call, which is after committing the initial
  // state.
  constexpr auto kMargin = 5s;
  ASSERT_LT(time_spent, kTimeout + kRpcTimeout + kMargin);
}

class PgBackendsTestRf3 : public PgBackendsTest {
 public:
  int GetNumMasters() const override {
    return 3;
  }

  int GetNumTabletServers() const override {
    return 3;
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgBackendsTest::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back("--replication_factor=3");
  }

 protected:
  Status TestConcurrentAlterFunc(
      std::function<Status(uint64_t)> wait_for_backends_cat_ver_func, bool should_fail);
};

// Cached version and jobs are lost when master loses leadership.
TEST_F_EX(PgBackendsTest, CacheLost, PgBackendsTestRf3) {
  LOG(INFO) << "Start connection that will be behind";
  PGConn conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("BEGIN"));

  BumpCatalogVersion(1);

  uint64_t master_catalog_version = ASSERT_RESULT(GetCatalogVersion());
  LOG(INFO) << "Got master catalog version " << master_catalog_version;

  auto num_backends = ASSERT_RESULT(
      client_->WaitForYsqlBackendsCatalogVersion("yugabyte", master_catalog_version));
  ASSERT_EQ(1, num_backends);

  LOG(INFO) << "Checking that job was created";
  CheckJobCount(1);

  LOG(INFO) << "Stepping down master leader";
  ExternalMaster* original_master_leader = cluster_->GetLeaderMaster();
  ASSERT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader());
  auto new_leader_time = CoarseMonoClock::Now();

  LOG(INFO) << "Checking that job does not exist on new leader";
  CheckJobCount(0);

  LOG(INFO) << "Checking that cached version is lost by seeing if new job appears";
  num_backends = ASSERT_RESULT(
      client_->WaitForYsqlBackendsCatalogVersion("yugabyte", master_catalog_version - 1));
  ASSERT_EQ(0, num_backends);
  // To check that cached version doesn't exist, we check that cached version was not used in our
  // previous request.  This is evident by the appearance of a new job.
  CheckJobCount(1);

  // We check the original master leader job count at this point because
  // 1. the catalog manager bg task takes some time to clear the jobs tracker, so it is more
  //    efficient to get other test work done during that time.
  // 2. the expected count is different from the new leader's count, so we don't run into the
  //    possible mistake of succeeding if incorrectly reading the new master's version.
  auto time_since_new_leader = CoarseMonoClock::Now() - new_leader_time;
  auto worst_case_time_since_bg_task = MonoDelta::FromMilliseconds(kCatalogManagerBgTaskWaitMs);
  if (time_since_new_leader < worst_case_time_since_bg_task) {
    SleepFor(worst_case_time_since_bg_task - time_since_new_leader);
  }
  LOG(INFO) << "Checking that original master leader jobs are cleared from jobs tracker";
  CheckJobCount(0, original_master_leader);

  LOG(INFO) << "Moving master leader back to original";
  ASSERT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader(original_master_leader->uuid()));

  LOG(INFO) << "Checking that originally cached job is not reused";
  num_backends = ASSERT_RESULT(
      client_->WaitForYsqlBackendsCatalogVersion("yugabyte", master_catalog_version));
  ASSERT_EQ(1, num_backends);
  CheckJobCount(1, original_master_leader);
}

// Waiting should be on all tservers' backends.
TEST_F_EX(PgBackendsTest, WaitAllTservers, PgBackendsTestRf3) {
  const uint64_t orig_cat_ver = ASSERT_RESULT(GetCatalogVersion());
  LOG(INFO) << "Got master catalog version " << orig_cat_ver;

  const auto num_ts = cluster_->num_tablet_servers();
  std::vector<PGConn> conns;
  for (auto* ts : cluster_->tserver_daemons()) {
    PGConn conn = ASSERT_RESULT(ConnectToTs(*ts));
    ASSERT_OK(conn.Execute("BEGIN"));
    conns.push_back(std::move(conn));
  }
  ASSERT_EQ(num_ts, conns.size());

  BumpCatalogVersion(1);
  const uint64_t cat_ver = ASSERT_RESULT(GetCatalogVersion());
  ASSERT_EQ(orig_cat_ver + 1, cat_ver);

  while (conns.size() > 0) {
    auto num_backends = ASSERT_RESULT(
        client_->WaitForYsqlBackendsCatalogVersion("yugabyte", cat_ver));
    // Still waiting on the "BEGIN" backends.
    ASSERT_EQ(conns.size(), num_backends);

    LOG(INFO) << "Resolve backend " << conns.size();
    ASSERT_OK(conns.back().Execute("COMMIT"));
    conns.pop_back();
  }

  auto num_backends = ASSERT_RESULT(
      client_->WaitForYsqlBackendsCatalogVersion("yugabyte", cat_ver));
  ASSERT_EQ(0, num_backends);
}

// Writer threads: insert a row containing knowledge of the true catalog version, backend catalog
// version, and function-returned catalog version.  DDL thread: alter the function with a wait
// between each alter.  If the wait implementation is good, we expect constraints on the versions to
// be unbroken.
Status PgBackendsTestRf3::TestConcurrentAlterFunc(
    std::function<Status(uint64_t)> wait_for_backends_cat_ver_func, bool should_fail) {
  constexpr auto kTableName = "caftab";
  // Create table:
  // - thread: thread idx
  // - true_version: master's catalog version
  // - backend_version: postgres backend's catalog version
  // - func_version: version returned by function (the function returns the catalog version at the
  //   time of creation)
  RETURN_NOT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 (thread int, true_version int, backend_version int, func_version int,"
      " sleep_sec int)",
      kTableName));
  // If the function is already cached, then it should match the backend version; otherwise, it
  // should match the true version.  This is the case even in a transaction.  It's a postgres quirk.
  RETURN_NOT_OK(conn_->ExecuteFormat(
      "ALTER TABLE $0 ADD CHECK (func_version IN (true_version, backend_version))",
      kTableName));
  // The backend cannot be more than one version behind as manually enforced by the
  // WaitForYsqlBackendsCatalogVersion before the catalog version bump.
  RETURN_NOT_OK(conn_->ExecuteFormat(
      "ALTER TABLE $0 ADD CHECK (true_version - backend_version IN (0, 1))",
      kTableName));

  const uint64_t orig_cat_ver = VERIFY_RESULT(GetCatalogVersion());
  constexpr auto kFuncName = "testfunc";
  // CREATE FUNCTION does not incur catalog version bump, so use original catalog version.
  RETURN_NOT_OK(conn_->ExecuteFormat(
      "CREATE FUNCTION $0 () RETURNS int LANGUAGE sql AS 'SELECT $1'", kFuncName, orig_cat_ver));
  const uint64_t orig_cat_ver2 = VERIFY_RESULT(GetCatalogVersion());
  SCHECK_EQ(orig_cat_ver, orig_cat_ver2,
            IllegalState,
            "CREATE FUNCTION should not cause catalog version bump");

  LOG(INFO) << "Spawn write threads";
  TestThreadHolder thread_holder;
  constexpr int kMaxSleepSec = 3;
  constexpr int kNumThreads = 5;
  CountDownLatch latch(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    thread_holder.AddThreadFunctor([this, i, should_fail, &kFuncName, &kTableName, &latch,
                                    &stop = thread_holder.stop_flag()] {
          PGConn conn = ASSERT_RESULT(Connect());
          latch.CountDown();
          LOG(INFO) << "Launch writes: thread " << i;
          while (!stop.load(std::memory_order_acquire)) {
            VLOG(1) << "Begin BEGIN: thread " << i;
            ASSERT_OK(conn.Execute("BEGIN"));
            VLOG(1) << "Finish BEGIN: thread " << i;
            const int sleep_sec = RandomUniformInt(0, kMaxSleepSec);
            LOG(INFO) << "Thread " << i << " sleeping for " << sleep_sec << "s";
            ASSERT_OK(conn.FetchFormat("SELECT pg_sleep($0)", sleep_sec));
            // Selecting directly from pg_yb_catalog_version will always make a master RPC, so it is
            // the "true" version. The use of catalog_version_db_oid_ makes the query work for both
            // global catalog version mode and per-db catalog version mode. We assume conn is
            // connected to database 'yugabyte'.
            Status s = conn.ExecuteFormat(
                "INSERT INTO $0"
                " SELECT $1, v.current_version, yb_pg_stat_get_backend_catalog_version(beid), $2(),"
                " $3"
                " FROM pg_yb_catalog_version v, pg_stat_get_backend_idset() beid"
                " WHERE pg_stat_get_backend_pid(beid) = pg_backend_pid() AND db_oid = $4",
                kTableName, i, kFuncName, sleep_sec, catalog_version_db_oid_);
            if (should_fail && !s.ok()) {
              LOG(INFO) << "Found failure on thread " << i;
              stop.store(true, std::memory_order_release);
            } else {
              ASSERT_OK(s);
            }
            ASSERT_OK(conn.Execute("COMMIT"));
          }
        });
  }

  latch.Wait();
  LOG(INFO) << "Spawn write threads done";

  LOG(INFO) << "Start DDLs";
  constexpr int kNumBumps = RegularBuildVsDebugVsSanitizers(10, 5, 5);
  for (int i = 0; i < kNumBumps; ++i) {
    if (thread_holder.stop_flag().load(std::memory_order_acquire)) {
      break;
    }

    const uint64_t cat_ver = orig_cat_ver + i;
    RETURN_NOT_OK(wait_for_backends_cat_ver_func(cat_ver));

    // CREATE OR REPLACE FUNCTION incurs catalog version bump, so use catalog version plus one.
    RETURN_NOT_OK(conn_->ExecuteFormat(
        "CREATE OR REPLACE FUNCTION $0 () RETURNS int LANGUAGE sql AS 'SELECT $1'",
        kFuncName, cat_ver + 1));
    const uint64_t new_cat_ver = VERIFY_RESULT(GetCatalogVersion());
    SCHECK_EQ(cat_ver + 1, new_cat_ver,
              IllegalState,
              "CREATE OR REPLACE FUNCTION should cause catalog version bump");
  }

  thread_holder.Stop();
  if (VLOG_IS_ON(1)) {
    const auto rows = VERIFY_RESULT((conn_->FetchRows<int32_t, int32_t, int32_t, int32_t, int32_t>(
        Format("SELECT * FROM $0 ORDER BY true_version ASC, backend_version ASC, func_version ASC",
               kTableName))));
    for (const auto& [thread, true_version, backend_version, func_version, sleep_sec] : rows) {
      VLOG(1) << "Row: thread=" << thread
              << ", true_version=" << true_version
              << ", backend_version=" << backend_version
              << ", func_version=" << func_version
              << ", sleep_sec=" << sleep_sec;
    }
  }
  return Status::OK();
}

// Simulate an online schema change situation by having concurrent threads accessing a function and
// one thread modifying the function while waiting for all backends to have up-to-date version
// before moving on to the next modification.
TEST_F_EX(PgBackendsTest, ConcurrentAlterFunc, PgBackendsTestRf3) {
  ASSERT_OK(TestConcurrentAlterFunc(
      [this](uint64_t cat_ver) -> Status {
        auto num_backends = VERIFY_RESULT(client_->WaitForYsqlBackendsCatalogVersion(
            "yugabyte",
            cat_ver,
            RegularBuildVsDebugVsSanitizers(15s, 30s, 30s) /* timeout */));
        SCHECK_EQ(0, num_backends, IllegalState, Format("got $0 backends", num_backends));
        return Status::OK();
      } /* wait_for_backends_cat_ver_func */,
      false /* should_fail */));
}

// The same as above except the waiting mechanism is intentionally broken to a hard-coded 1s wait.
// It is expected to mess up.  In the extreme case, it may not mess up if the random number
// generator gave 0s sleeps for the writer threads, but the chance of that happening is so small it
// is not something to worry about.
TEST_F_EX(PgBackendsTest, ConcurrentAlterFuncNegative, PgBackendsTestRf3) {
  ASSERT_OK(TestConcurrentAlterFunc(
      [](uint64_t cat_ver) -> Status {
        LOG(INFO) << "Not waiting for backends catalog version " << cat_ver
                  << "; sleeping for 1s instead";
        SleepFor(1s);
        return Status::OK();
      } /* wait_for_backends_cat_ver_func */,
      true /* should_fail */));
}

// Renaming the database should not interrupt the progress of WaitForYsqlBackendsCatalogVersion.
TEST_F_EX(PgBackendsTest, RenameDatabase, PgBackendsTestRf3) {
  LOG(INFO) << "Create database and get its oid";
  constexpr auto kDbPrefix = "before";
  ASSERT_OK(conn_->ExecuteFormat("CREATE DATABASE $0_0", kDbPrefix));
  const auto db_oid = ASSERT_RESULT(conn_->FetchValue<PGOid>(Format(
      "SELECT oid FROM pg_database WHERE datname = '$0_0'", kDbPrefix)));

  LOG(INFO) << "Create connection that is behind";
  PGConn conn = ASSERT_RESULT(ConnectToDB(Format("$0_0", kDbPrefix)));
  ASSERT_OK(conn.Execute("BEGIN"));
  BumpCatalogVersion(1);
  uint64_t master_catalog_version = ASSERT_RESULT(GetCatalogVersion());

  LOG(INFO) << "Check that that connection is behind";
  auto num_backends = ASSERT_RESULT(
      client_->WaitForYsqlBackendsCatalogVersion(db_oid, master_catalog_version));
  ASSERT_EQ(1, num_backends);

  CountDownLatch latch(1);
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this, &kDbPrefix, &latch, &stop = thread_holder.stop_flag()] {
        LOG(INFO) << "Begin rename thread";

        // Connect to a different node from the one that has the backend we are waiting on, which is
        // the one that will have ongoing libpq queries for WaitForYsqlBackendsCatalogVersion job.
        // That way, we avoid ALTER DATABASE failing due to other active sessions.
        ExternalTabletServer* ts = cluster_->tserver_daemons()[1];
        PGConn conn = ASSERT_RESULT(ConnectToTs(*ts));

        size_t i = 0;
        while (!stop.load(std::memory_order_acquire)) {
          ASSERT_OK(conn.ExecuteFormat(
              "ALTER DATABASE $0_$1 RENAME TO $0_$2", kDbPrefix, i++, i));
          latch.CountDown();
        }
        SleepFor(10ms);
      });

  latch.Wait();
  LOG(INFO) << "Check WaitForYsqlBackendsCatalogVersion works during renames";
  num_backends = ASSERT_RESULT(
      client_->WaitForYsqlBackendsCatalogVersion(db_oid, master_catalog_version));
  ASSERT_EQ(1, num_backends);
}

TEST_F_EX(PgBackendsTest, YB_DISABLE_TEST_EXCEPT_RELEASE(Stress), PgBackendsTestRf3) {
  constexpr auto kBumpPeriod = 1s;
  constexpr auto kMaxSleep = 3s;
  constexpr auto kTestDuration = 45s;
  const uint64_t orig_cat_ver = ASSERT_RESULT(GetCatalogVersion());
  std::atomic<uint64_t> cat_ver = orig_cat_ver;
  const int num_ts = narrow_cast<int>(cluster_->num_tablet_servers());
  constexpr int kNumTs = 3;
  CHECK_EQ(kNumTs, num_ts);

  LOG(INFO) << "Create table";
  constexpr auto kTableName = "writertab";
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));

  LOG(INFO) << "Construct sleep conn pool";
  constexpr int kNumSleepThreads = 10;
  std::map<int, std::vector<PGConn>> sleep_conn_pool;
  for (int ts_idx = 0; ts_idx < kNumTs; ++ts_idx) {
    ExternalTabletServer* ts = cluster_->tserver_daemons()[ts_idx];
    std::vector<PGConn> v;
    for (int j = 0; j < kNumSleepThreads; ++j) {
      v.emplace_back(ASSERT_RESULT(ConnectToTs(*ts)));
    }
    sleep_conn_pool.insert({ts_idx, std::move(v)});
  }

  LOG(INFO) << "Launch threads";
  TestThreadHolder thread_holder;
  for (int i = 0; i < kNumSleepThreads; ++i) {
    thread_holder.AddThreadFunctor([kMaxSleep, i, &sleep_conn_pool,
                                    &stop = thread_holder.stop_flag()] {
      while (!stop.load(std::memory_order_acquire)) {
        const int ts_idx = RandomUniformInt(0, kNumTs - 1);
        PGConn* conn = &sleep_conn_pool[ts_idx][i];
        const double sleep_sec = RandomUniformReal(0.0, MonoDelta(kMaxSleep).ToSeconds());
        LOG(INFO) << "Sleep thread " << i << " sleeping for " << sleep_sec;
        ASSERT_OK(conn->FetchFormat("SELECT pg_sleep($0)", sleep_sec));
      }
      LOG(INFO) << "Sleep thread " << i << " stopped";
    });
  }
  constexpr int kNumWriteThreads = 15;
  for (int i = 0; i < kNumWriteThreads; ++i) {
    thread_holder.AddThreadFunctor([this, i, &kTableName, &stop = thread_holder.stop_flag()] {
      const int ts_idx = RandomUniformInt(0, kNumTs - 1);
      ExternalTabletServer* ts = cluster_->tserver_daemons()[ts_idx];
      PGConn conn = ASSERT_RESULT(ConnectToTs(*ts));
      while (!stop.load(std::memory_order_acquire)) {
        auto res = conn.FetchFormat("INSERT INTO $0 VALUES ($1)", kTableName, i);
        if (!res.ok()) {
          Status s = res.status();
          ASSERT_TRUE(s.IsNetworkError()) << s;
          // We don't care about the results of these writes.  They are here just to add load to the
          // system.
          WARN_NOT_OK(s, Format("Write $0 failed", i));
        }
      }
      LOG(INFO) << "Write thread " << i << " stopped";
    });
  }
  constexpr int kNumWaitThreads = 15;
  for (int i = 0; i < kNumWaitThreads; ++i) {
    constexpr auto kMargin = 10s;
    thread_holder.AddThreadFunctor([this, kMaxSleep, kTestDuration, kMargin, i, &cat_ver,
                                    &stop = thread_holder.stop_flag()] {
      while (!stop.load(std::memory_order_acquire)) {
        // To prevent clumping of wait threads on the same version, add some random delay.
        const double sleep_sec = RandomUniformReal(0.0, MonoDelta(kMaxSleep).ToSeconds());
        LOG(INFO) << "Wait thread " << i << " sleeping for " << sleep_sec;
        SleepFor(MonoDelta::FromSeconds(sleep_sec));

        const auto cat_ver_snapshot = cat_ver.load(std::memory_order_acquire);
        LOG(INFO) << "Wait thread " << i << " waiting on version " << cat_ver_snapshot;
        auto num_backends = ASSERT_RESULT(
            client_->WaitForYsqlBackendsCatalogVersion(
              "yugabyte", cat_ver_snapshot, kTestDuration + kMargin /* timeout */));
        ASSERT_EQ(0, num_backends) << "wait thread " << i << ", ver " << cat_ver_snapshot;
      }
      LOG(INFO) << "Wait thread " << i << " stopped";
    });
  }

  const auto start_time = CoarseMonoClock::Now();
  while (CoarseMonoClock::Now() - start_time < kTestDuration) {
    BumpCatalogVersion(1);
    LOG(INFO) << "catalog version is now " << cat_ver++;
    SleepFor(kBumpPeriod);
  }

  thread_holder.Stop();
}

class PgBackendsTestRf3DeadFast : public PgBackendsTestRf3 {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgBackendsTestRf3::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back(
        Format("--tserver_unresponsive_timeout_ms=$0", kTsDeadSec * 1000));
  }
 protected:
  static constexpr int kTsDeadSec = 30;
};

TEST_F_EX(PgBackendsTest, LostHeartbeats, PgBackendsTestRf3DeadFast) {
  constexpr auto kUser = "eve";
  ASSERT_OK(conn_->ExecuteFormat("CREATE USER $0", kUser));
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0tab (i int)", kUser));
  ASSERT_OK(conn_->ExecuteFormat("ALTER TABLE $0tab OWNER TO $0", kUser));
  const uint64_t cat_ver = ASSERT_RESULT(GetCatalogVersion());

  LOG(INFO) << "Choose ts besides the one occupied by conn_";
  const int num_ts = narrow_cast<int>(cluster_->num_tablet_servers());
  const int ts_idx = RandomUniformInt(1, num_ts - 1);
  ExternalTabletServer* ts = cluster_->tserver_daemons()[ts_idx];

  LOG(INFO) << "Stop heartbeating for ts " << ts_idx << " (zero-indexed)";
  ASSERT_OK(cluster_->SetFlag(ts, "TEST_tserver_disable_heartbeat", "true"));

  ASSERT_OK(conn_->ExecuteFormat("ALTER TABLE $0tab OWNER TO yugabyte", kUser));

  // Because the tserver is not getting heartbeat responses, its catalog version should be behind,
  // before the above alter.  Since there are no backends on that tserver, it won't find behind
  // backends.  However, the tserver's catalog version is behind, so future non-idle backends would
  // be behind.  Ensure that this tserver catalog version is checked, not just the backends' catalog
  // version.
  LOG(INFO) << "Ensure that that tserver is not considered resolved";
  auto num_backends = ASSERT_RESULT(client_->WaitForYsqlBackendsCatalogVersion(
      "yugabyte", cat_ver + 1, MonoDelta::FromSeconds(kTsDeadSec / 2) /* timeout */));
  ASSERT_EQ(-1, num_backends);

  PGConn conn_user = ASSERT_RESULT(PGConnBuilder({
        .host = ts->bind_host(),
        .port = ts->pgsql_rpc_port(),
        .user = kUser,
      }).Connect());

  // TODO(#13369): check that conn_user becomes blocked when ts is marked dead, and check that that
  // happens after kTsDeadSec time has passed since disabling heartbeat.
}

class PgBackendsTestRf3DeadFaster : public PgBackendsTestRf3 {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgBackendsTestRf3::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back(
        Format("--tserver_unresponsive_timeout_ms=$0", kTsDeadSec * 1000));
  }
 protected:
  Status TestTserverUnresponsive(bool keep_alive);

  static constexpr int kTsDeadSec = 10;
};

// An unresponsive tserver that expires lease should be considered resolved.  Currently, lease
// expiration is interpreted by liveness of the tserver according to master, which is computed using
// gflag tserver_unresponsive_timeout_ms and how long it has been since tserver last heartbeated.
// TODO(#13369): a lease expiration should cause that tserver to block its backends, but that is
// currently not done, so the test asserts the opposite of correctness (the user whose permission
// was revoked is able to select).
Status PgBackendsTestRf3DeadFaster::TestTserverUnresponsive(bool keep_alive) {
  constexpr auto kUser = "eve";
  RETURN_NOT_OK(conn_->ExecuteFormat("CREATE USER $0", kUser));
  RETURN_NOT_OK(conn_->ExecuteFormat("CREATE TABLE $0tab (i int)", kUser));
  RETURN_NOT_OK(conn_->ExecuteFormat("ALTER TABLE $0tab OWNER TO $0", kUser));

  const int num_ts = narrow_cast<int>(cluster_->num_tablet_servers());
  const int ts_idx = RandomUniformInt(0, num_ts - 1);
  LOG(INFO) << "Selected ts index " << ts_idx << " (zero-indexed)";

  ExternalTabletServer* ts = cluster_->tserver_daemons()[ts_idx];
  PGConn conn_user = VERIFY_RESULT(PGConnBuilder({
        .host = ts->bind_host(),
        .port = ts->pgsql_rpc_port(),
        .user = kUser,
      }).Connect());

  LOG(INFO) << "Begin transaction on a connection";
  RETURN_NOT_OK(conn_user.Execute("BEGIN"));
  const uint64_t orig_cat_ver = VERIFY_RESULT(GetCatalogVersion());

  LOG(INFO) << "Revoke permission on table to user (involving catalog version bump)";
  RETURN_NOT_OK(conn_->ExecuteFormat("ALTER TABLE $0tab OWNER TO yugabyte", kUser));
  const uint64_t cat_ver = VERIFY_RESULT(GetCatalogVersion());
  SCHECK_EQ(orig_cat_ver + 1, cat_ver,
            IllegalState,
            "ALTER TABLE should cause catalog version bump");

  auto num_backends = VERIFY_RESULT(
      client_->WaitForYsqlBackendsCatalogVersion("yugabyte", cat_ver));
  // Still waiting on the "BEGIN" backend.
  SCHECK_EQ(1, num_backends, IllegalState, "unexpected num backends");

  LOG(INFO) << "Verify the revoke is in effect";
  PGConn conn_user2 = VERIFY_RESULT(PGConnBuilder({
        .host = ts->bind_host(),
        .port = ts->pgsql_rpc_port(),
        .user = kUser,
      }).Connect());
  auto res = conn_user2.FetchFormat("SELECT * FROM $0tab", kUser);
  SCHECK(!res.ok(), IllegalState, "should not have permission");
  Status s = res.status();
  if (!s.IsNetworkError() ||
      (s.message().ToBuffer().find(Format("permission denied for table $0tab", kUser)) ==
       std::string::npos)) {
    return s;
  }

  if (keep_alive) {
    LOG(INFO) << "Stop heartbeating for that ts";
    RETURN_NOT_OK(cluster_->SetFlag(ts, "TEST_tserver_disable_heartbeat", "true"));
  } else {
    LOG(INFO) << "Shutdown that ts";
    ts->Shutdown();
  }

  LOG(INFO) << "Verify that the disconnect does not immediately result in resolution for that ts";
  num_backends = VERIFY_RESULT(client_->WaitForYsqlBackendsCatalogVersion(
      "yugabyte", cat_ver, MonoDelta::FromSeconds(kTsDeadSec / 2) /* timeout */));
  // Still waiting on the "BEGIN" backend.
  SCHECK_EQ(1, num_backends, IllegalState, "unexpected num backends");

  LOG(INFO) << "Check for eventual resolution of that ts";
  constexpr int kMarginSec = 10;
  num_backends = VERIFY_RESULT(
      client_->WaitForYsqlBackendsCatalogVersion(
        "yugabyte",
        cat_ver,
        MonoDelta::FromSeconds(kTsDeadSec / 2 + kMarginSec
                               + kMasterTsRpcTimeoutSec) /* timeout */));
  SCHECK_EQ(0, num_backends, IllegalState, "unexpected num backends");

  if (keep_alive) {
    LOG(INFO) << "Check that old connection on unresponsive tserver is blocked";
    auto res = conn_user.FetchFormat("SELECT * FROM $0tab", kUser);
    LOG(INFO) << "Actually, old connection should not be blocked yet.  See first task of"
              << " issue #13369.  Also, it works because ALTER TABLE is not a breaking catalog"
              << " change.";
    // TODO(#13369): change to SCHECK(!status.ok()), check status type, status msg.
    RETURN_NOT_OK(res);
  }

  LOG(INFO) << "Make new connection in case conn_'s node was selected to be unresponsive";
  ts = cluster_->tserver_daemons()[(ts_idx + 1) % num_ts];
  PGConn conn = VERIFY_RESULT(ConnectToTs(*ts));
  LibPqTestBase::BumpCatalogVersion(1, &conn);

  LOG(INFO) << "Verify that new wait requests when ts is already dead are immediate";
  num_backends = VERIFY_RESULT(client_->WaitForYsqlBackendsCatalogVersion("yugabyte", cat_ver + 1));
  SCHECK_EQ(0, num_backends, IllegalState, "unexpected num backends");
  return Status::OK();
}

TEST_F_EX(
    PgBackendsTest,
    TserverUnresponsiveShutdown,
    PgBackendsTestRf3DeadFaster) {
  ASSERT_OK(TestTserverUnresponsive(false /* keep_alive */));
}

TEST_F_EX(
    PgBackendsTest,
    TserverUnresponsiveNoShutdown,
    PgBackendsTestRf3DeadFaster) {
  ASSERT_OK(TestTserverUnresponsive(true /* keep_alive */));
}

class PgBackendsTestRf3Block : public PgBackendsTestRf3, public testing::WithParamInterface<int> {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgBackendsTestRf3::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back(
        "--TEST_block_wait_for_ysql_backends_catalog_version=true");
  }

 protected:
  Result<int> AccessYsqlBackendsManagerTestRegister(std::optional<int> value = std::nullopt,
                                                    const ExternalMaster* master = nullptr);
  Status LoggedWaitForTestRegister(int target_value, const ExternalMaster* master = nullptr);

  Result<int> TestLeaderChangeInFlight(bool expect_retry);
};

class PgBackendsTestRf3BlockNoLeaderLock : public PgBackendsTestRf3Block {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgBackendsTestRf3Block::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back(
        "--TEST_wait_for_ysql_backends_catalog_version_take_leader_lock=false");
  }
};

// Get or set YsqlBackendsManager test register.
Result<int> PgBackendsTestRf3Block::AccessYsqlBackendsManagerTestRegister(
    std::optional<int> value,
    const ExternalMaster* master) {
  if (!master) {
    master = cluster_->GetLeaderMaster();
  }
  master::MasterAdminProxy proxy = cluster_->GetProxy<master::MasterAdminProxy>(master);
  rpc::RpcController controller;
  controller.set_timeout(30s);
  master::AccessYsqlBackendsManagerTestRegisterRequestPB req;
  master::AccessYsqlBackendsManagerTestRegisterResponsePB resp;
  if (value) {
    req.set_value(*value);
  }
  RETURN_NOT_OK_PREPEND(proxy.AccessYsqlBackendsManagerTestRegister(req, &resp, &controller),
                        "rpc failed");
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return resp.value();
}

// Wait for test register to hit target value.
Status PgBackendsTestRf3Block::LoggedWaitForTestRegister(
    int target_value, const ExternalMaster* master) {
  return LoggedWaitFor(
      [this, master, target_value]() -> Result<bool> {
        auto register_value = VERIFY_RESULT(
            AccessYsqlBackendsManagerTestRegister(std::nullopt, master));
        return register_value == target_value;
      },
      30s,
      Format("wait for ysql backends manager test register to turn to $0", target_value));
}

// Test:
// 1. Prep a lagging backend.
// 1. Activate blocking.
// 1. Send a WaitForYsqlBackendsCatalogVersion request, and wait for it to enter the block loop.
// 1. Change master leader.
// 1. Deactivate blocking.
// 1. If expect_retry=true, expect a retry request to show up on the new leader; otherwise, don't
//    expect that.
Result<int> PgBackendsTestRf3Block::TestLeaderChangeInFlight(bool expect_retry) {
  const auto block_id = GetParam();
  LOG(INFO) << "Block id is " << block_id;

  PGConn conn = VERIFY_RESULT(Connect());
  RETURN_NOT_OK(conn.Execute("BEGIN"));

  BumpCatalogVersion(1);
  uint64_t master_catalog_version = VERIFY_RESULT(GetCatalogVersion());
  LOG(INFO) << "Got master catalog version " << master_catalog_version;

  LOG(INFO) << "Activating blocking";
  SCHECK_EQ(
      0, VERIFY_RESULT(AccessYsqlBackendsManagerTestRegister(block_id)),
      IllegalState, "unexpected test register");

  CountDownLatch latch(1);
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([&] {
    latch.CountDown();

    ASSERT_OK(LoggedWaitForTestRegister(-block_id));

    // Remember old master leader.
    const ExternalMaster* old_master_leader = cluster_->GetLeaderMaster();

    LOG(INFO) << "Stepping down master leader";
    ASSERT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader());
    LOG(INFO) << "Activating blocking on new master leader";
    ASSERT_EQ(0, ASSERT_RESULT(AccessYsqlBackendsManagerTestRegister(block_id)));
    LOG(INFO) << "Deactivating blocking on old master leader";
    ASSERT_EQ(
        -block_id, ASSERT_RESULT(AccessYsqlBackendsManagerTestRegister(0, old_master_leader)));

    LOG(INFO) << "Resolving the backend that is behind";
    ASSERT_OK(conn.Execute("COMMIT"));

    if (expect_retry) {
      // Unblock on the new master leader.
      ASSERT_OK(LoggedWaitForTestRegister(-block_id));
      LOG(INFO) << "Deactivating blocking on new master leader";
      ASSERT_EQ(-block_id, ASSERT_RESULT(AccessYsqlBackendsManagerTestRegister(0)));
    } else {
      // The new master leader shouldn't receive a retry request.
      while (!thread_holder.stop_flag().load(std::memory_order_acquire)) {
        ASSERT_NE(-block_id, ASSERT_RESULT(AccessYsqlBackendsManagerTestRegister()));
      }
    }
  });

  latch.Wait();

  LOG(INFO) << "Launching wait query";
  auto start = MonoTime::Now();
  auto res = client_->WaitForYsqlBackendsCatalogVersion("yugabyte", master_catalog_version);
  const auto time_taken = MonoTime::Now() - start;
  LOG(INFO) << "Time taken: " << time_taken;

  thread_holder.Stop();

  return res;
}

TEST_P(PgBackendsTestRf3Block, LeaderChangeInFlight) {
  auto num_lagging_backends = ASSERT_RESULT(TestLeaderChangeInFlight(true /* expect_retry */));
  ASSERT_EQ(0, num_lagging_backends);
}

// Negative test for the above LeaderChangeInFlight test where leadership loss handling is disabled.
// This proves that the above test is showing something meaningful.
TEST_P(PgBackendsTestRf3BlockNoLeaderLock, LeaderChangeInFlightNegative) {
  auto res = TestLeaderChangeInFlight(false /* expect_retry */);
  // It is possible (but rare as of D19621) for there to be an issue for processing the request on a
  // master that lost leadership.
  WARN_NOT_OK(res, "Failed processing request on follower");
}

INSTANTIATE_TEST_CASE_P(, PgBackendsTestRf3Block, ::testing::Range(1, 4));
INSTANTIATE_TEST_CASE_P(, PgBackendsTestRf3BlockNoLeaderLock, ::testing::Range(1, 4));

// Test:
// 1. Prep a lagging backend.
// 1. Activate blocking on id=4.  Unlike the above test with id=1,2,3, id=4 is after explicit
//    leadership checks, so we expect the response to be based on discovering the job is aborted
//    instead.
// 1. Send a WaitForYsqlBackendsCatalogVersion request, and wait for it to enter the block loop.
// 1. Change master leader.
// 1. On the old leader, activate blocking on id=5 (deactivating the id=4 block).  This is to
//    prevent eventual resolution of the lagging backend from causing TerminateJob to kComplete
//    state before the leadership-loss bg task causes TerminateJob to kAbort state: we want kAbort
//    state to win.  (It is not guaranteed that TerminateJob to kComplete will be attempted because
//    job could be cleared or leadership changed beforehand.)
// 1. Expect a retry request to show up on the new leader; otherwise, don't expect that.
TEST_F(PgBackendsTestRf3Block, LeaderChangeInFlightLater) {
  PGConn conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("BEGIN"));

  BumpCatalogVersion(1);
  uint64_t master_catalog_version = ASSERT_RESULT(GetCatalogVersion());
  LOG(INFO) << "Got master catalog version " << master_catalog_version;

  LOG(INFO) << "Activating blocking";
  ASSERT_EQ(0, ASSERT_RESULT(AccessYsqlBackendsManagerTestRegister(4)));

  CountDownLatch latch(1);
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([&] {
    latch.CountDown();

    ASSERT_OK(LoggedWaitForTestRegister(-4));

    // Remember old master leader.
    const ExternalMaster* old_master_leader = cluster_->GetLeaderMaster();

    LOG(INFO) << "Stepping down master leader";
    ASSERT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader());
    LOG(INFO) << "Activating blocking on new master leader";
    ASSERT_EQ(0, ASSERT_RESULT(AccessYsqlBackendsManagerTestRegister(1)));
    LOG(INFO) << "Shifting blocking on old master leader";
    ASSERT_EQ(-4, ASSERT_RESULT(AccessYsqlBackendsManagerTestRegister(5, old_master_leader)));

    LOG(INFO) << "Resolving the backend that is behind";
    ASSERT_OK(conn.Execute("COMMIT"));

    // Unblock old master leader is not needed and should not be done because it is not always the
    // case that that block will be hit and prematurely unblocking could lead to transition to
    // kComplete state rather than kAbort state.

    ASSERT_OK(LoggedWaitForTestRegister(-1));
    LOG(INFO) << "Unblock new master leader";
    ASSERT_EQ(-1, ASSERT_RESULT(AccessYsqlBackendsManagerTestRegister(0)));
  });

  latch.Wait();

  LOG(INFO) << "Launching wait query";
  auto start = MonoTime::Now();
  ASSERT_OK(client_->WaitForYsqlBackendsCatalogVersion("yugabyte", master_catalog_version));
  const auto time_taken = MonoTime::Now() - start;
  LOG(INFO) << "Time taken: " << time_taken;

  thread_holder.Stop();
}

} // namespace pgwrapper
} // namespace yb
