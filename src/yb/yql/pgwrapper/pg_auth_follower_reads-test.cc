// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/snapshot_test_util.h"

#include "yb/common/wire_protocol.h"
#include "yb/common/ysql_auth_catalog_read.h"

#include "yb/master/master.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog.h"

#include "yb/rpc/messenger.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/pg_client_service.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver.messages.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/flags.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/metrics.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(TEST_enable_pg_client_mock);
DECLARE_bool(TEST_skip_election_when_fail_detected);
DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(enable_ysql_conn_mgr);
DECLARE_bool(ysql_enable_auth_catalog_follower_reads);
DECLARE_bool(ysql_enable_auto_analyze);
DECLARE_bool(ysql_enable_concurrent_ddl);
DECLARE_bool(ysql_enable_profile);
DECLARE_bool(ysql_enable_read_request_cache_for_connection_auth);
DECLARE_bool(ysql_enable_read_request_caching);
DECLARE_bool(ysql_enable_relcache_init_optimization);
DECLARE_bool(ysql_yb_enable_invalidation_messages);
DECLARE_int32(TEST_delay_sys_catalog_restore_on_followers_secs);
DECLARE_string(ysql_hba_conf_csv);
DECLARE_string(ysql_pg_conf_csv);
DECLARE_uint32(pg_cache_response_trust_auth_lifetime_limit_ms);
DECLARE_uint32(pg_response_cache_size_percentage);
DECLARE_uint64(pg_response_cache_size_bytes);
DECLARE_uint64(ysql_catalog_prefetch_row_limit);

METRIC_DECLARE_counter(pg_response_cache_hits);
METRIC_DECLARE_counter(pg_response_cache_queries);
METRIC_DECLARE_counter(pg_response_cache_renew_soft);
METRIC_DECLARE_counter(ysql_auth_catalog_follower_reads);
METRIC_DECLARE_counter(ysql_auth_catalog_leader_reads);
METRIC_DECLARE_counter(ysql_auth_catalog_snapshot_acquisitions);

using namespace std::literals;

namespace yb::pgwrapper {
namespace {

constexpr auto kOldPassword = "dummy_password_old";
constexpr auto kNewPassword = "dummy_password_new";
constexpr uint32_t kAuthIdOid = 1260;
constexpr uint32_t kAuthMembersOid = 1261;
constexpr uint32_t kDatabaseOid = 1262;
constexpr uint32_t kDbRoleSettingOid = 2964;

struct Requests {
  std::vector<tserver::PgPerformRequestPB> performs;
  std::vector<tserver::ReadRequestPB> reads;
};

// A dispatched mock retains its callback after Handle destruction. Close entry and drain before
// any state captured by reference can go out of scope.
class ScopedPerformMock {
 public:
  enum class Phase { kBefore, kAfter };
  using Hook = std::function<Status(
      const tserver::PgPerformRequestMsg*, tserver::PgPerformResponseMsg*,
      tserver::PgClientMockCallContext*)>;

  ScopedPerformMock(tserver::TabletServer* server, Phase phase, Hook hook)
      : state_(std::make_shared<State>()) {
    auto guarded_hook = [state = state_, hook = std::move(hook)](
                            const tserver::PgPerformRequestMsg* req,
                            tserver::PgPerformResponseMsg* resp,
                            tserver::PgClientMockCallContext* context) -> Status {
      {
        std::lock_guard lock(state->mutex);
        if (state->stopped) {
          return Status::OK();
        }
        ++state->active;
      }
      auto done = ScopeExit([&] {
        std::lock_guard lock(state->mutex);
        --state->active;
        state->drained.notify_all();
      });
      return hook(req, resp, context);
    };
    auto* mock = server->TEST_GetPgClientServiceMock();
    handle_ = phase == Phase::kBefore ? mock->MockPerformBefore(guarded_hook)
                                     : mock->MockPerformAfter(guarded_hook);
  }

  ~ScopedPerformMock() { Stop(); }

  ScopedPerformMock(const ScopedPerformMock&) = delete;
  ScopedPerformMock& operator=(const ScopedPerformMock&) = delete;

  void Stop() {
    std::unique_lock lock(state_->mutex);
    state_->stopped = true;
    handle_.reset();
    state_->drained.wait(lock, [&] { return state_->active == 0; });
  }

 private:
  struct State {
    std::mutex mutex;
    std::condition_variable drained;
    bool stopped = false;
    size_t active = 0;
  };

  std::shared_ptr<State> state_;
  std::optional<tserver::PgClientServiceMockImpl::Handle> handle_;
};

// Mocks observe or mutate the wire request; the real service and shared-memory path still run.
class RequestTrace {
 public:
  using ReadHook = std::function<void(const tserver::ReadRequestPB&)>;
  using PerformHook = std::function<void(tserver::PgPerformRequestPB*)>;

  explicit RequestTrace(
      tserver::TabletServer* server, ReadHook hook = {}, PerformHook perform_hook = {})
      : state_(std::make_shared<State>()),
        perform_mock_(server, ScopedPerformMock::Phase::kBefore,
            [state = state_, perform_hook](
                const tserver::PgPerformRequestMsg* req, tserver::PgPerformResponseMsg*,
                tserver::PgClientMockCallContext*) {
              auto request = req->ToGoogleProtobuf();
              if (perform_hook) {
                perform_hook(&request);
                const_cast<tserver::PgPerformRequestMsg*>(req)->CopyFrom(request);
              }
              std::lock_guard lock(state->mutex);
              state->requests.performs.push_back(std::move(request));
              return Status::OK();
            }) {
    auto* sync = SyncPoint::GetInstance();
    sync->SetCallBack("ReadRpc::CallRemoteMethod", [state = state_, hook](void* arg) {
      const auto request = static_cast<tserver::ReadRequestMsg*>(arg)->ToGoogleProtobuf();
      {
        std::lock_guard lock(state->mutex);
        state->requests.reads.push_back(request);
      }
      if (hook) {
        hook(request);
      }
    });
    sync->EnableProcessing();
  }

  ~RequestTrace() { Stop(); }

  void Stop() {
    perform_mock_.Stop();
    SyncPoint::GetInstance()->DisableProcessing();
    SyncPoint::GetInstance()->ClearAllCallBacks();
  }

  Requests Get() const {
    std::lock_guard lock(state_->mutex);
    return state_->requests;
  }

  void Clear() {
    std::lock_guard lock(state_->mutex);
    state_->requests = {};
  }

 private:
  struct State {
    std::mutex mutex;
    Requests requests;
  };

  std::shared_ptr<State> state_;
  ScopedPerformMock perform_mock_;
};

bool ReadsRelation(const tserver::ReadRequestPB& req, uint32_t oid) {
  for (const auto& read : req.pgsql_batch()) {
    if (read.table_id() == GetPgsqlTableId(kTemplate1Oid, oid)) {
      return true;
    }
  }
  return false;
}

bool HasAuthIdContinuation(const tserver::ReadRequestPB& req) {
  if (!req.ysql_auth_catalog_read()) {
    return false;
  }
  for (const auto& read : req.pgsql_batch()) {
    if (read.table_id() == GetPgsqlTableId(kTemplate1Oid, kAuthIdOid) &&
        (read.has_paging_state() || read.index_request().has_paging_state())) {
      return true;
    }
  }
  return false;
}

bool HasAuthIdContinuation(const tserver::PgPerformRequestPB& req) {
  if (!req.options().ysql_auth_catalog_read()) {
    return false;
  }
  for (const auto& op : req.ops()) {
    if (op.has_read() && op.read().table_id() == GetPgsqlTableId(kTemplate1Oid, kAuthIdOid) &&
        (op.read().has_paging_state() || op.read().index_request().has_paging_state())) {
      return true;
    }
  }
  return false;
}

Result<HybridTime> CheckSingleSnapshot(const Requests& requests) {
  HybridTime snapshot;
  for (const auto& perform : requests.performs) {
    const auto& options = perform.options();
    if (!options.ysql_auth_catalog_read()) {
      continue;
    }
    SCHECK(options.use_legacy_catalog_session() && !options.has_caching_info(), IllegalState,
           "Auth Perform must use the uncached catalog session");
    const auto read_time = ReadHybridTime::FromPB(options.read_time_options().read_time());
    if (!snapshot.is_valid()) {
      snapshot = read_time.read;
    }
    SCHECK(read_time == ReadHybridTime::SingleTime(snapshot), IllegalState,
           "Auth Perform changed its snapshot");
  }
  SCHECK(snapshot.is_valid() && !snapshot.is_special(), IllegalState, "No auth Perform observed");
  size_t marked_reads = 0;
  for (const auto& req : requests.reads) {
    if (!req.ysql_auth_catalog_read()) {
      continue;
    }
    ++marked_reads;
    SCHECK(ReadHybridTime::FromReadTimePB(req) == ReadHybridTime::SingleTime(snapshot),
           IllegalState, "Auth Read changed its snapshot");
    for (const auto& read : req.pgsql_batch()) {
      SCHECK(IsYsqlAuthCatalogRead(read), IllegalState, "Unexpected marked relation or read shape");
      SCHECK(YsqlAuthCatalogPagingMatchesReadTime(read, snapshot), IllegalState,
             "Auth continuation changed its snapshot");
    }
  }
  SCHECK_GT(marked_reads, 0, IllegalState, "No marked outer Read observed");
  return snapshot;
}

void AssertUnmarked(const Requests& requests) {
  for (const auto& req : requests.performs) {
    ASSERT_FALSE(req.options().ysql_auth_catalog_read()) << req.ShortDebugString();
  }
  for (const auto& req : requests.reads) {
    ASSERT_FALSE(req.ysql_auth_catalog_read()) << req.ShortDebugString();
    ASSERT_EQ(req.consistency_level(), YBConsistencyLevel::STRONG);
  }
}

}  // namespace

class PgAuthFollowerReadsTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_pg_client_mock) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql_conn_mgr) = false;
    if (EnableRouting()) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auth_catalog_follower_reads) = true;
    }
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_profile) = EnableProfiles();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_relcache_init_optimization) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_read_request_caching) = EnableCache();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_read_request_cache_for_connection_auth) =
        EnableCache();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_invalidation_messages) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_locking_for_table_locks) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_concurrent_ddl) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_response_cache_size_percentage) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_response_cache_size_bytes) = 5 * 1024 * 1024;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_hba_conf_csv) =
        "host all postgres all trust,host all +auth_group all md5,host all all all reject";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) = PgConf();
    ASSERT_NO_FATAL_FAILURE(PgMiniTestBase::SetUp());
  }

  size_t NumMasters() override { return 3; }
  size_t NumTabletServers() override { return 1; }
  Result<PGConn> Connect() const override { return ConnectToDB("yugabyte"); }
  virtual bool EnableRouting() const { return true; }
  virtual bool EnableProfiles() const { return false; }
  virtual bool EnableCache() const { return false; }
  virtual std::string PgConf() const { return {}; }

  tserver::TabletServer* server() const {
    return cluster_->mini_tablet_server(0)->server();
  }

  Result<client::internal::RemoteTabletPtr> CatalogTablet() {
    auto promise = std::make_shared<std::promise<Result<client::internal::RemoteTabletPtr>>>();
    auto future = promise->get_future();
    server()->client()->LookupTabletById(
        master::kSysCatalogTabletId, nullptr, master::IncludeHidden::kTrue,
        master::IncludeDeleted::kFalse, CoarseMonoClock::Now() + 30s * kTimeMultiplier,
        [promise](const auto& result) { promise->set_value(result); }, client::UseCache::kFalse);
    SCHECK(future.wait_for(30s * kTimeMultiplier) == std::future_status::ready, TimedOut,
           "Look up the authentication client's catalog replicas");
    return future.get();
  }

  uint64_t MasterCounter(const CounterPrototype& metric) const {
    uint64_t result = 0;
    for (size_t i = 0; i < cluster_->num_masters(); ++i) {
      result += metric.Instantiate(cluster_->mini_master(i)->master()->metric_entity())->value();
    }
    return result;
  }

  uint64_t Snapshots() const {
    return MasterCounter(METRIC_ysql_auth_catalog_snapshot_acquisitions);
  }

  uint64_t FollowerReads() const {
    return MasterCounter(METRIC_ysql_auth_catalog_follower_reads);
  }

  uint64_t LeaderReads() const {
    return MasterCounter(METRIC_ysql_auth_catalog_leader_reads);
  }

  Status CreateRoles(PGConn* admin) {
    RETURN_NOT_OK(admin->Execute("CREATE ROLE auth_group"));
    RETURN_NOT_OK(admin->ExecuteFormat("CREATE ROLE auth_user LOGIN PASSWORD '$0'", kOldPassword));
    return admin->Execute("GRANT auth_group TO auth_user");
  }

  Result<PGConn> ConnectUser(
      const std::string& password = kOldPassword, const std::string& db = "yugabyte") const {
    auto settings = MakeConnSettings(db);
    settings.user = "auth_user";
    settings.password = password;
    settings.connect_timeout = 30 * kTimeMultiplier;
    // A failed login must not silently become a new auth attempt with another snapshot.
    settings.should_stop = [] { return true; };
    return PGConnBuilder(settings).Connect();
  }

  void AssertDenied(const std::string& password, const std::string& message,
                    const std::string& db = "yugabyte") {
    const auto snapshots = Snapshots();
    ASSERT_NOK_STR_CONTAINS(ConnectUser(password, db), message);
    ASSERT_EQ(Snapshots(), snapshots + 1);
  }

  void AssertFreshLogin(const std::string& password) {
    const auto snapshots = Snapshots();
    const auto followers = FollowerReads();
    auto conn = ASSERT_RESULT(ConnectUser(password));
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>("SELECT current_user")), "auth_user");
    ASSERT_EQ(Snapshots(), snapshots + 1);
    // These are admission counters, so require a completed login as well.
    ASSERT_GT(FollowerReads(), followers);
  }

  void AssertNoAuthRouting() {
    ASSERT_EQ(Snapshots(), 0);
    ASSERT_EQ(FollowerReads(), 0);
    ASSERT_EQ(LeaderReads(), 0);
  }
};

TEST_F(PgAuthFollowerReadsTest, FreshPasswordLoginMembershipAndConnectPrivileges) {
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateRoles(&admin));
  ASSERT_NO_FATAL_FAILURE(AssertFreshLogin(kOldPassword));

  ASSERT_OK(admin.ExecuteFormat("ALTER ROLE auth_user PASSWORD '$0'", kNewPassword));
  ASSERT_NO_FATAL_FAILURE(AssertDenied(kOldPassword, "password authentication failed"));
  ASSERT_NO_FATAL_FAILURE(AssertFreshLogin(kNewPassword));

  ASSERT_OK(admin.Execute("ALTER ROLE auth_user NOLOGIN"));
  ASSERT_NO_FATAL_FAILURE(AssertDenied(kNewPassword, "is not permitted to log in"));
  ASSERT_OK(admin.Execute("ALTER ROLE auth_user LOGIN"));

  ASSERT_NO_FATAL_FAILURE(AssertFreshLogin(kNewPassword));
  ASSERT_OK(admin.Execute("GRANT auth_group TO auth_user"));

  ASSERT_OK(admin.Execute("CREATE DATABASE auth_db"));
  ASSERT_OK(admin.Execute("REVOKE CONNECT ON DATABASE auth_db FROM PUBLIC"));
  ASSERT_OK(admin.Execute("GRANT CONNECT ON DATABASE auth_db TO auth_group"));
  ASSERT_RESULT(ConnectUser(kNewPassword, "auth_db"));
  ASSERT_OK(admin.Execute("REVOKE CONNECT ON DATABASE auth_db FROM auth_group"));
  ASSERT_NO_FATAL_FAILURE(
      AssertDenied(kNewPassword, "permission denied for database", "auth_db"));
}

TEST_F(PgAuthFollowerReadsTest, AuthMarkerDoesNotLeakToOrdinaryQueries) {
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateRoles(&admin));
  ASSERT_OK(admin.Execute("CREATE TABLE auth_data (k int PRIMARY KEY)"));
  ASSERT_OK(admin.Execute("INSERT INTO auth_data VALUES (1)"));
  ASSERT_OK(admin.Execute("GRANT SELECT ON auth_data TO auth_user"));

  RequestTrace trace(server());
  const auto snapshots = Snapshots();
  auto conn = ASSERT_RESULT(ConnectUser());
  ASSERT_EQ(Snapshots(), snapshots + 1);
  const auto startup = trace.Get();
  bool marked_perform = false;
  for (const auto& req : startup.performs) {
    marked_perform |= req.options().ysql_auth_catalog_read();
  }
  ASSERT_TRUE(marked_perform);
  trace.Clear();
  const auto followers = FollowerReads();
  const auto leaders = LeaderReads();
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT k FROM auth_data")), 1);
  ASSERT_GT(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM pg_auth_members")), 0);
  const auto ordinary = trace.Get();
  ASSERT_FALSE(ordinary.performs.empty());
  ASSERT_NO_FATAL_FAILURE(AssertUnmarked(ordinary));
  ASSERT_EQ(Snapshots(), snapshots + 1);
  ASSERT_EQ(FollowerReads(), followers);
  ASSERT_EQ(LeaderReads(), leaders);
}

TEST_F(PgAuthFollowerReadsTest, DatabaseAuthorizationAndSettingsKeepSnapshot) {
  // Exercise cached phase-3 prefetch configuration without opting auth into TRUST_CACHE_AUTH.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_read_request_caching) = true;
  ASSERT_OK(RestartPostgres());
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateRoles(&admin));
  ASSERT_OK(admin.Execute("CREATE DATABASE auth_db"));
  ASSERT_OK(admin.Execute("REVOKE CONNECT ON DATABASE auth_db FROM PUBLIC"));
  ASSERT_OK(admin.Execute("GRANT CONNECT ON DATABASE auth_db TO auth_group"));
  ASSERT_OK(admin.Execute("ALTER ROLE auth_user SET work_mem = '1MB'"));
  ASSERT_OK(admin.Execute("ALTER ROLE auth_user IN DATABASE auth_db SET work_mem = '2MB'"));
  ASSERT_OK(admin.Execute("ALTER DATABASE auth_db SET statement_timeout = '11s'"));
  const auto db_oid = ASSERT_RESULT(admin.FetchRow<PGOid>(
      "SELECT oid FROM pg_database WHERE datname = 'auth_db'"));

  // Leave a real shared response-cache entry at a different snapshot from the tested login.
  const auto cache_queries =
      METRIC_pg_response_cache_queries.Instantiate(server()->metric_entity())->value();
  {
    auto warm = ASSERT_RESULT(CreateInternalPGConnBuilder(
        server()->pgsql_proxy_bind_address(), "auth_db", "postgres",
        server()->GetSharedMemoryPostgresAuthKey(), std::nullopt).Connect());
  }
  ASSERT_GT(METRIC_pg_response_cache_queries.Instantiate(server()->metric_entity())->value(),
            cache_queries);

  CountDownLatch paused(1), resume(1);
  std::atomic<bool> pause_once{true};
  Status login_status;
  std::optional<PGConn> conn;
  TestThreadHolder threads;
  RequestTrace trace(server(), [&](const auto& req) {
    if (req.ysql_auth_catalog_read() && ReadsRelation(req, kAuthIdOid) &&
        pause_once.exchange(false)) {
      paused.CountDown();
      resume.Wait();
    }
  });
  auto cleanup = ScopeExit([&] { resume.CountDown(); threads.JoinAll(); });
  const auto snapshots = Snapshots();
  threads.AddThread([&] {
    auto result = ConnectUser(kOldPassword, "auth_db");
    if (!result.ok()) {
      login_status = result.status();
      return;
    }
    conn.emplace(std::move(*result));
  });
  ASSERT_TRUE(paused.WaitFor(30s * kTimeMultiplier));
  ASSERT_EQ(Snapshots(), snapshots + 1);

  ASSERT_OK(admin.Execute("REVOKE CONNECT ON DATABASE auth_db FROM auth_group"));
  ASSERT_OK(admin.Execute("ALTER ROLE auth_user SET work_mem = '3MB'"));
  ASSERT_OK(admin.Execute("ALTER ROLE auth_user IN DATABASE auth_db SET work_mem = '4MB'"));
  ASSERT_OK(admin.Execute("ALTER DATABASE auth_db SET statement_timeout = '22s'"));
  const auto version = ASSERT_RESULT(admin.FetchRow<int64_t>(Format(
      "SELECT current_version FROM pg_yb_catalog_version WHERE db_oid = $0", db_oid)));
  ASSERT_OK(WaitFor([&] {
    uint64_t current_version = 0, last_breaking_version = 0;
    server()->get_ysql_db_catalog_version(
        db_oid, &current_version, &last_breaking_version, false /* use_cache */);
    return current_version >= static_cast<uint64_t>(version);
  }, 30s * kTimeMultiplier, "Publish invalidations while authentication is pinned"));

  resume.CountDown();
  threads.JoinAll();
  ASSERT_OK(login_status);
  ASSERT_TRUE(conn);
  ASSERT_EQ(Snapshots(), snapshots + 1);
  const auto requests = trace.Get();
  const auto snapshot = ASSERT_RESULT(CheckSingleSnapshot(requests));
  size_t database_reads = 0, settings_reads = 0, leader_only_reads = 0;
  for (const auto& req : requests.reads) {
    if (ReadHybridTime::FromReadTimePB(req).read != snapshot) {
      continue;
    }
    ASSERT_EQ(ReadHybridTime::FromReadTimePB(req), ReadHybridTime::SingleTime(snapshot));
    database_reads += ReadsRelation(req, kDatabaseOid);
    settings_reads += ReadsRelation(req, kDbRoleSettingOid);
    for (const auto& read : req.pgsql_batch()) {
      if (!IsYsqlAuthCatalogRead(read)) {
        ASSERT_FALSE(req.ysql_auth_catalog_read());
        ASSERT_EQ(req.consistency_level(), YBConsistencyLevel::STRONG);
        ++leader_only_reads;
      }
    }
  }
  ASSERT_GE(database_reads, 2);  // Initial auth prefetch and phase 3.
  ASSERT_GT(settings_reads, 0);
  ASSERT_GT(leader_only_reads, 0);

  trace.Clear();
  ASSERT_EQ(ASSERT_RESULT(conn->FetchRow<std::string>("SHOW work_mem")), "2MB");
  ASSERT_EQ(ASSERT_RESULT(conn->FetchRow<std::string>("SHOW statement_timeout")), "11s");
  // The auth cache version must not have consumed invalidations while still reading at T.
  ASSERT_FALSE(ASSERT_RESULT(conn->FetchRow<bool>(
      "SELECT has_database_privilege(current_user, current_database(), 'CONNECT')")));
  ASSERT_GT(ASSERT_RESULT(conn->FetchRow<int64_t>("SELECT count(*) FROM pg_db_role_setting")), 0);
  ASSERT_NO_FATAL_FAILURE(AssertUnmarked(trace.Get()));
  ASSERT_EQ(Snapshots(), snapshots + 1);

  ASSERT_NO_FATAL_FAILURE(
      AssertDenied(kOldPassword, "permission denied for database", "auth_db"));
  ASSERT_OK(admin.Execute("GRANT CONNECT ON DATABASE auth_db TO auth_group"));
  auto next = ASSERT_RESULT(ConnectUser(kOldPassword, "auth_db"));
  ASSERT_EQ(ASSERT_RESULT(next.FetchRow<std::string>("SHOW work_mem")), "4MB");
  ASSERT_EQ(ASSERT_RESULT(next.FetchRow<std::string>("SHOW statement_timeout")), "22s");
}

class PgAuthFollowerPagingTest : public PgAuthFollowerReadsTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_catalog_prefetch_row_limit) = 8;
    PgAuthFollowerReadsTest::SetUp();
  }

  std::string PgConf() const override { return "yb_fetch_row_limit=8"; }
};

TEST_F(PgAuthFollowerPagingTest, PagesAndOnDemandMembershipKeepOneSnapshot) {
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateRoles(&admin));
  for (int i = 0; i != 16; ++i) {
    ASSERT_OK(admin.ExecuteFormat("CREATE ROLE auth_padding_$0", i));
    ASSERT_OK(admin.ExecuteFormat("GRANT auth_group TO auth_padding_$0", i));
  }

  CountDownLatch paused(1), resume(1);
  std::atomic<bool> pause_once{true};
  Status login_status;
  TestThreadHolder threads;
  RequestTrace trace(server(), [&](const auto& req) {
    if (HasAuthIdContinuation(req) && pause_once.exchange(false)) {
      paused.CountDown();
      resume.Wait();
    }
  });
  // Release the callback before RequestTrace waits for callbacks and before joining the worker.
  auto cleanup = ScopeExit([&] { resume.CountDown(); threads.JoinAll(); });
  const auto snapshots = Snapshots();
  const auto followers = FollowerReads();
  threads.AddThread([&] { login_status = ResultToStatus(ConnectUser()); });
  ASSERT_TRUE(paused.WaitFor(30s * kTimeMultiplier));
  ASSERT_EQ(Snapshots(), snapshots + 1);

  ASSERT_OK(admin.ExecuteFormat("ALTER ROLE auth_user PASSWORD '$0'", kNewPassword));
  ASSERT_OK(admin.Execute("REVOKE auth_group FROM auth_user"));
  resume.CountDown();
  threads.JoinAll();
  ASSERT_OK(login_status);
  ASSERT_EQ(Snapshots(), snapshots + 1);
  ASSERT_GT(FollowerReads(), followers);

  const auto requests = trace.Get();
  const auto snapshot = ASSERT_RESULT(CheckSingleSnapshot(requests));
  size_t auth_pages = 0;
  size_t membership_reads = 0;
  for (const auto& req : requests.reads) {
    auth_pages += HasAuthIdContinuation(req);
    if (ReadsRelation(req, kAuthMembersOid) &&
        ReadHybridTime::FromReadTimePB(req).read == snapshot) {
      ASSERT_EQ(ReadHybridTime::FromReadTimePB(req), ReadHybridTime::SingleTime(snapshot));
      ++membership_reads;
    }
  }
  ASSERT_GT(auth_pages, 1);
  // HBA loads this table on demand, after the password pages and after the concurrent revoke.
  ASSERT_GT(membership_reads, 0);

  ASSERT_NO_FATAL_FAILURE(AssertDenied(kNewPassword, "pg_hba.conf rejects connection"));
  ASSERT_OK(admin.Execute("GRANT auth_group TO auth_user"));
  ASSERT_NO_FATAL_FAILURE(AssertDenied(kOldPassword, "password authentication failed"));
  ASSERT_NO_FATAL_FAILURE(AssertFreshLogin(kNewPassword));
}

TEST_F(PgAuthFollowerReadsTest, FollowerRejectionFallsBackAtSameSnapshot) {
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateRoles(&admin));
  std::atomic<bool> rejected{false};
  std::atomic<bool> retried{false};
  auto restore = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auth_catalog_follower_reads) = true;
  });
  RequestTrace trace(server(), [&](const auto& req) {
    if (!req.ysql_auth_catalog_read()) {
      return;
    }
    if (req.consistency_level() == YBConsistencyLevel::CONSISTENT_PREFIX &&
        !rejected.exchange(true)) {
      // The snapshot and Perform validation already succeeded. Exercise a real serving-gate
      // rejection, not a fabricated response or a follower-read bypass.
      ASSERT_OK(SET_FLAG(ysql_enable_auth_catalog_follower_reads, false));
    } else if (req.consistency_level() == YBConsistencyLevel::STRONG) {
      ASSERT_OK(SET_FLAG(ysql_enable_auth_catalog_follower_reads, true));
      retried = true;
    }
  });
  const auto snapshots = Snapshots();
  const auto leaders = LeaderReads();
  ASSERT_RESULT(ConnectUser());
  ASSERT_TRUE(rejected);
  ASSERT_TRUE(retried);
  ASSERT_EQ(Snapshots(), snapshots + 1);
  ASSERT_GT(LeaderReads(), leaders);
  ASSERT_RESULT(CheckSingleSnapshot(trace.Get()));
}

TEST_F(PgAuthFollowerReadsTest, FollowerNetworkFailureFallsBackAtSameSnapshot) {
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateRoles(&admin));
  const auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  std::vector<IpAddress> follower_addresses;
  for (size_t i = 0; i < cluster_->num_masters(); ++i) {
    if (cluster_->mini_master(i) == leader) {
      continue;
    }
    for (auto private_address : {server::Private::kTrue, server::Private::kFalse}) {
      follower_addresses.push_back(ASSERT_RESULT(
          HostToAddress(server::TEST_RpcAddress(i + 1, private_address))));
    }
  }
  auto* messenger = server()->messenger();
  auto restore = ScopeExit([&] {
    for (const auto& address : follower_addresses) {
      messenger->RestoreConnectivityTo(address);
    }
  });
  // Close the tserver's follower connections, but leave master-to-master replication and the
  // leader lease intact. Both possible follower choices are unavailable to this PG client.
  for (const auto& address : follower_addresses) {
    messenger->BreakConnectivityTo(address);
  }
  RequestTrace trace(server());
  const auto snapshots = Snapshots();
  const auto followers = FollowerReads();
  const auto leaders = LeaderReads();
  ASSERT_RESULT(ConnectUser());
  ASSERT_EQ(Snapshots(), snapshots + 1);
  ASSERT_EQ(FollowerReads(), followers);
  ASSERT_GT(LeaderReads(), leaders);
  const auto requests = trace.Get();
  ASSERT_RESULT(CheckSingleSnapshot(requests));
  bool follower_attempt = false;
  bool leader_retry = false;
  for (const auto& req : requests.reads) {
    if (req.ysql_auth_catalog_read()) {
      follower_attempt |= req.consistency_level() == YBConsistencyLevel::CONSISTENT_PREFIX;
      leader_retry |= req.consistency_level() == YBConsistencyLevel::STRONG;
    }
  }
  ASSERT_TRUE(follower_attempt);
  ASSERT_TRUE(leader_retry);
}

TEST_F(PgAuthFollowerReadsTest, FreshnessSurvivesFailoverAndFullRestart) {
  {
    auto admin = ASSERT_RESULT(Connect());
    ASSERT_OK(CreateRoles(&admin));
    ASSERT_NO_FATAL_FAILURE(AssertFreshLogin(kOldPassword));
    const auto* original = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
    std::string target;
    for (size_t i = 0; i < cluster_->num_masters(); ++i) {
      if (cluster_->mini_master(i) != original) {
        target = cluster_->mini_master(i)->permanent_uuid();
        break;
      }
    }
    ASSERT_FALSE(target.empty());
    ASSERT_OK(cluster_->StepDownMasterLeader(target));
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      return VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->permanent_uuid() == target;
    }, 30s * kTimeMultiplier, "Elect the chosen auth snapshot leader"));
    ASSERT_OK(admin.ExecuteFormat("ALTER ROLE auth_user PASSWORD '$0'", kNewPassword));
    ASSERT_NO_FATAL_FAILURE(AssertDenied(kOldPassword, "password authentication failed"));
    ASSERT_NO_FATAL_FAILURE(AssertFreshLogin(kNewPassword));
  }

  // Unlike RestartSync's rolling restart, stop every peer before starting any of them.
  cluster_->StopSync();
  ASSERT_OK(cluster_->Start());
  // PG starts asynchronously; only this readiness connection may retry.
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_NO_FATAL_FAILURE(AssertDenied(kOldPassword, "password authentication failed"));
  ASSERT_NO_FATAL_FAILURE(AssertFreshLogin(kNewPassword));
  ASSERT_OK(admin.ExecuteFormat("ALTER ROLE auth_user PASSWORD '$0'", kOldPassword));
  ASSERT_NO_FATAL_FAILURE(AssertDenied(kNewPassword, "password authentication failed"));
  ASSERT_NO_FATAL_FAILURE(AssertFreshLogin(kOldPassword));
}

class PgAuthFollowerDefaultOffTest : public PgAuthFollowerReadsTest {
 protected:
  bool EnableRouting() const override { return false; }
};

TEST_F(PgAuthFollowerDefaultOffTest, RoutingIsOffByDefault) {
  ASSERT_FALSE(FLAGS_ysql_enable_auth_catalog_follower_reads);
  RequestTrace trace(server());
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateRoles(&admin));
  ASSERT_RESULT(ConnectUser());
  ASSERT_NO_FATAL_FAILURE(AssertNoAuthRouting());
  ASSERT_NO_FATAL_FAILURE(AssertUnmarked(trace.Get()));
}

class PgAuthFollowerProfileTest : public PgAuthFollowerReadsTest {
 protected:
  bool EnableProfiles() const override { return true; }
};

TEST_F(PgAuthFollowerProfileTest, LoginProfilesStayOnLeader) {
  RequestTrace trace(server());
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateRoles(&admin));
  ASSERT_OK(admin.Execute("CREATE PROFILE auth_profile LIMIT FAILED_LOGIN_ATTEMPTS 3"));
  ASSERT_OK(admin.Execute("ALTER USER auth_user PROFILE auth_profile"));
  ASSERT_RESULT(ConnectUser());
  ASSERT_NOK_STR_CONTAINS(ConnectUser(kNewPassword), "password authentication failed");
  ASSERT_RESULT(ConnectUser());
  ASSERT_NO_FATAL_FAILURE(AssertNoAuthRouting());
  ASSERT_NO_FATAL_FAILURE(AssertUnmarked(trace.Get()));
}

TEST_F(PgAuthFollowerReadsTest, InternalAndUnixSocketConnectionsStayOnLeader) {
  const auto snapshots = Snapshots();
  const auto followers = FollowerReads();
  const auto leaders = LeaderReads();
  RequestTrace trace(server());
  for (const auto kind : {std::string_view{}, YbInternalConnKindWireName::kRelcacheInit,
                          YbInternalConnKindWireName::kAutoAnalyze}) {
    auto conn = ASSERT_RESULT(CreateInternalPGConnBuilder(
        server()->pgsql_proxy_bind_address(), "yugabyte", "postgres",
        server()->GetSharedMemoryPostgresAuthKey(), std::nullopt, kind).Connect());
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT 1")), 1);
  }
  ASSERT_EQ(Snapshots(), snapshots);
  ASSERT_EQ(FollowerReads(), followers);
  ASSERT_EQ(LeaderReads(), leaders);
  ASSERT_NO_FATAL_FAILURE(AssertUnmarked(trace.Get()));
}

class PgAuthFollowerResponseCacheTest : public PgAuthFollowerReadsTest {
 protected:
  bool EnableCache() const override { return true; }

  uint64_t CacheCounter(const CounterPrototype& metric) const {
    return metric.Instantiate(server()->metric_entity())->value();
  }
};

TEST_F(PgAuthFollowerResponseCacheTest, CacheMissHitAndExpiryNeverAcquireAuthSnapshot) {
  ASSERT_OK(WaitFor([&] {
    uint64_t current_version = 0, last_breaking_version = 0;
    server()->get_ysql_db_catalog_version(
        kTemplate1Oid, &current_version, &last_breaking_version, false /* use_cache */);
    return current_version > 0;
  }, 30s * kTimeMultiplier, "Publish shared catalog version before cached authentication"));

  RequestTrace trace(server());
  auto queries = CacheCounter(METRIC_pg_response_cache_queries);
  auto hits = CacheCounter(METRIC_pg_response_cache_hits);
  ASSERT_RESULT(Connect());
  ASSERT_GT(CacheCounter(METRIC_pg_response_cache_queries) - queries,
            CacheCounter(METRIC_pg_response_cache_hits) - hits);
  ASSERT_RESULT(Connect());
  queries = CacheCounter(METRIC_pg_response_cache_queries);
  hits = CacheCounter(METRIC_pg_response_cache_hits);
  ASSERT_RESULT(Connect());
  // Auth prefetch plus ordinary catalog preload, as in PgCatalogPerfTest's cache fixture.
  ASSERT_EQ(CacheCounter(METRIC_pg_response_cache_queries) - queries, 2);
  ASSERT_EQ(CacheCounter(METRIC_pg_response_cache_hits) - hits, 2);
  ASSERT_NO_FATAL_FAILURE(AssertNoAuthRouting());
  ASSERT_NO_FATAL_FAILURE(AssertUnmarked(trace.Get()));

  // Only restart PG: the tserver keeps the same populated cache. New backends now request a
  // one-millisecond TTL, so the existing auth entry expires without a race-ordering sleep.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_cache_response_trust_auth_lifetime_limit_ms) = 1;
  ASSERT_OK(RestartPostgres());
  trace.Clear();
  const auto renewals = CacheCounter(METRIC_pg_response_cache_renew_soft);
  ASSERT_RESULT(Connect());
  ASSERT_GT(CacheCounter(METRIC_pg_response_cache_renew_soft), renewals);
  bool cached_auth = false;
  for (const auto& req : trace.Get().performs) {
    for (const auto& op : req.ops()) {
      if (op.has_read() && op.read().table_id() == GetPgsqlTableId(kTemplate1Oid, kAuthIdOid)) {
        cached_auth |= req.options().has_caching_info();
      }
    }
  }
  ASSERT_TRUE(cached_auth);
  ASSERT_NO_FATAL_FAILURE(AssertNoAuthRouting());
  ASSERT_NO_FATAL_FAILURE(AssertUnmarked(trace.Get()));
}

TEST_F(PgAuthFollowerReadsTest, SafeTimeLagFallsBackAtFreshSnapshot) {
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateRoles(&admin));
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  // Master 0 shares the tserver's IP; partitioning it would also block DDL object-lock RPCs.
  const size_t lagging_idx = cluster_->mini_master(1) == leader ? 2 : 1;
  auto* lagging = cluster_->mini_master(lagging_idx);
  auto tablet = ASSERT_RESULT(lagging->master()->sys_catalog().tablet_peer()->shared_tablet());
  auto admissions = METRIC_ysql_auth_catalog_follower_reads.Instantiate(
      lagging->master()->metric_entity());
  const auto admitted_before = admissions->value();

  // Keep the other follower in quorum, but prevent the selected follower from receiving either
  // writes or propagated safe time. Suppress its failure detector, not the leader's lease checks.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_election_when_fail_detected) = true;
  std::vector<IpAddress> addresses;
  for (auto privacy : {server::Private::kTrue, server::Private::kFalse}) {
    addresses.push_back(ASSERT_RESULT(
        HostToAddress(server::TEST_RpcAddress(lagging_idx + 1, privacy))));
  }
  auto restore = ScopeExit([&] {
    for (const auto& address : addresses) {
      leader->messenger().RestoreConnectivityTo(address);
    }
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_election_when_fail_detected) = false;
  });
  for (const auto& address : addresses) {
    leader->messenger().BreakConnectivityTo(address);
  }
  ASSERT_OK(admin.ExecuteFormat("ALTER ROLE auth_user PASSWORD '$0'", kNewPassword));

  // Use the same RemoteTablet as the authentication client's ReadRpc. Excluding the healthy
  // follower affects routing only; neither master's serving checks are bypassed.
  auto remote = ASSERT_RESULT(CatalogTablet());
  auto replicas = remote->GetRemoteTabletServers();
  ASSERT_EQ(replicas.size(), 3);
  for (auto* replica : replicas) {
    if (replica->permanent_uuid() != leader->permanent_uuid() &&
        replica->permanent_uuid() != lagging->permanent_uuid()) {
      ASSERT_TRUE(remote->MarkReplicaFailed(replica, STATUS(NetworkError, "Route to lagged peer")));
    }
  }
  const auto old_vlog = google::SetVLOGLevel("tablet_rpc", 4);
  auto restore_vlog = ScopeExit([&] { google::SetVLOGLevel("tablet_rpc", old_vlog); });
  StringWaiterLogSink rejected("Authentication catalog snapshot is not safe");
  RequestTrace trace(server());
  const auto snapshots = Snapshots();
  const auto leaders = LeaderReads();
  auto conn = ASSERT_RESULT(ConnectUser(kNewPassword));
  ASSERT_EQ(Snapshots(), snapshots + 1);
  ASSERT_TRUE(rejected.IsEventOccurred());
  ASSERT_EQ(admissions->value(), admitted_before);
  ASSERT_GT(LeaderReads(), leaders);
  const auto requests = trace.Get();
  const auto snapshot = ASSERT_RESULT(CheckSingleSnapshot(requests));
  const auto lagging_safe_time = ASSERT_RESULT(tablet->SafeTime(tablet::RequireLease::kFalse));
  ASSERT_LT(lagging_safe_time, snapshot);
  bool follower_attempt = false, strong_retry = false;
  for (const auto& req : requests.reads) {
    if (req.ysql_auth_catalog_read()) {
      follower_attempt |= req.consistency_level() == YBConsistencyLevel::CONSISTENT_PREFIX;
      strong_retry |= req.consistency_level() == YBConsistencyLevel::STRONG;
    }
  }
  ASSERT_TRUE(follower_attempt);
  ASSERT_TRUE(strong_retry);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>("SELECT current_user")), "auth_user");
}

TEST_F(PgAuthFollowerPagingTest, InFlightFailoverPreservesSnapshot) {
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateRoles(&admin));
  for (int i = 0; i != 16; ++i) {
    ASSERT_OK(admin.ExecuteFormat("CREATE ROLE auth_padding_$0", i));
  }
  auto* old_leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  master::MiniMaster* new_leader = nullptr;
  for (size_t i = 0; i < cluster_->num_masters(); ++i) {
    if (cluster_->mini_master(i) != old_leader) {
      new_leader = cluster_->mini_master(i);
      break;
    }
  }
  ASSERT_NE(new_leader, nullptr);
  auto remote = ASSERT_RESULT(CatalogTablet());
  auto* old_replica = remote->LeaderTServer();
  ASSERT_NE(old_replica, nullptr);
  ASSERT_EQ(old_replica->permanent_uuid(), old_leader->permanent_uuid());

  CountDownLatch paused(1), resume(1), completed(1);
  std::atomic<bool> pause_once{true}, stale_leader_retry{false};
  Status login_status;
  TestThreadHolder threads;
  RequestTrace trace(server(), [&](const auto& req) {
    if (HasAuthIdContinuation(req) && pause_once.exchange(false)) {
      paused.CountDown();
      resume.Wait();
    }
    if (req.ysql_auth_catalog_read() && !pause_once &&
        req.consistency_level() == YBConsistencyLevel::STRONG &&
        !stale_leader_retry.exchange(true)) {
      ASSERT_OK(SET_FLAG(ysql_enable_auth_catalog_follower_reads, true));
      ASSERT_EQ(remote->LeaderTServer(), old_replica);
    }
  });
  auto cleanup = ScopeExit([&] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auth_catalog_follower_reads) = true;
    resume.CountDown();
    trace.Stop();
    threads.JoinAll();
  });
  const auto snapshots = Snapshots();
  threads.AddThread([&] {
    login_status = ResultToStatus(ConnectUser());
    completed.CountDown();
  });
  ASSERT_TRUE(paused.WaitFor(30s * kTimeMultiplier));
  ASSERT_EQ(Snapshots(), snapshots + 1);
  ASSERT_OK(cluster_->StepDownMasterLeader(new_leader->permanent_uuid()));
  ASSERT_OK(new_leader->WaitUntilCatalogManagerIsLeaderAndReadyForTests());
  ASSERT_EQ(ASSERT_RESULT(cluster_->GetLeaderMiniMaster()), new_leader);
  ASSERT_OK(admin.ExecuteFormat("ALTER ROLE auth_user PASSWORD '$0'", kNewPassword));
  ASSERT_OK(admin.Execute("REVOKE auth_group FROM auth_user"));

  // Admin writes refreshed the shared metadata. Restore the old leader and reject the paused
  // CONSISTENT_PREFIX request so its first STRONG retry must discover the election itself.
  ASSERT_TRUE(remote->MarkTServerAsLeader(old_replica));
  const auto old_admissions = METRIC_ysql_auth_catalog_leader_reads.Instantiate(
      old_leader->master()->metric_entity())->value();
  const auto new_admissions = METRIC_ysql_auth_catalog_leader_reads.Instantiate(
      new_leader->master()->metric_entity())->value();
  ASSERT_OK(SET_FLAG(ysql_enable_auth_catalog_follower_reads, false));
  resume.CountDown();
  ASSERT_TRUE(completed.WaitFor(30s * kTimeMultiplier));
  trace.Stop();
  threads.JoinAll();
  ASSERT_OK(login_status);
  ASSERT_TRUE(stale_leader_retry);
  ASSERT_EQ(Snapshots(), snapshots + 1);
  ASSERT_EQ(METRIC_ysql_auth_catalog_leader_reads.Instantiate(
      old_leader->master()->metric_entity())->value(), old_admissions);
  ASSERT_GT(METRIC_ysql_auth_catalog_leader_reads.Instantiate(
      new_leader->master()->metric_entity())->value(), new_admissions);
  const auto requests = trace.Get();
  ASSERT_RESULT(CheckSingleSnapshot(requests));
  size_t strong_continuations = 0;
  for (const auto& req : requests.reads) {
    strong_continuations += HasAuthIdContinuation(req) &&
        req.consistency_level() == YBConsistencyLevel::STRONG;
  }
  ASSERT_GE(strong_continuations, 2);
  ASSERT_NO_FATAL_FAILURE(AssertDenied(kNewPassword, "pg_hba.conf rejects connection"));
  ASSERT_OK(admin.Execute("GRANT auth_group TO auth_user"));
  ASSERT_NO_FATAL_FAILURE(AssertDenied(kOldPassword, "password authentication failed"));
  ASSERT_NO_FATAL_FAILURE(AssertFreshLogin(kNewPassword));
}

TEST_F(PgAuthFollowerReadsTest, MarkedPerformRejectsInvalidOptionsAndOperations) {
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateRoles(&admin));
  ASSERT_OK(admin.Execute("CREATE TABLE auth_write_guard (k int PRIMARY KEY)"));
  tserver::PgPerformOpPB write, indexed_read;
  {
    RequestTrace trace(server());
    ASSERT_RESULT(ConnectUser());
    ASSERT_RESULT(CheckSingleSnapshot(trace.Get()));
    trace.Clear();
    ASSERT_OK(admin.Execute("INSERT INTO auth_write_guard VALUES (1)"));
    for (const auto& req : trace.Get().performs) {
      for (const auto& op : req.ops()) {
        if (op.has_write()) {
          write = op;
        }
      }
    }
    ASSERT_OK(admin.Execute("SET enable_seqscan = off"));
    trace.Clear();
    ASSERT_RESULT(admin.FetchRow<std::string>(
        "SELECT rolpassword FROM pg_authid WHERE rolname = 'auth_user'"));
    for (const auto& req : trace.Get().performs) {
      for (const auto& op : req.ops()) {
        if (op.has_read() && op.read().has_index_request() &&
            op.read().table_id() == GetPgsqlTableId(kTemplate1Oid, kAuthIdOid)) {
          indexed_read = op;
        }
      }
    }
    ASSERT_OK(admin.Execute("RESET enable_seqscan"));
  }
  ASSERT_TRUE(write.has_write());
  ASSERT_OK(admin.Execute("DELETE FROM auth_write_guard"));
  ASSERT_TRUE(indexed_read.has_read());
  ASSERT_TRUE(indexed_read.read().has_index_request());

  struct Mutation {
    const char* name;
    const char* message;
    std::function<void(tserver::PgPerformRequestPB*)> apply;
  };
  const std::vector<Mutation> mutations = {
      {"missing time", "explicit, immutable snapshot", [](auto* req) {
         req->mutable_options()->mutable_read_time_options()->clear_read_time();
       }},
      {"non-single time", "exact read time", [](auto* req) {
         auto* time = req->mutable_options()->mutable_read_time_options()->mutable_read_time();
         time->set_global_limit_ht(time->read_ht() + 1);
       }},
      {"transaction", "transaction or table-lock state", [](auto* req) {
         req->mutable_options()->set_isolation(IsolationLevel::SNAPSHOT_ISOLATION);
       }},
      {"historical session", "transaction or table-lock state", [](auto* req) {
         req->mutable_options()->set_use_historical_read_session(true);
       }},
      {"historical transaction", "transaction or table-lock state", [](auto* req) {
         req->mutable_options()->set_historical_read_transaction_id(std::string(16, '\0'));
       }},
      {"historical session and transaction", "transaction or table-lock state", [](auto* req) {
         req->mutable_options()->set_use_historical_read_session(true);
         req->mutable_options()->set_historical_read_transaction_id(std::string(16, '\0'));
       }},
      {"restart", "explicit, immutable snapshot", [](auto* req) {
         req->mutable_options()->mutable_read_time_options()->set_restart_transaction(true);
       }},
      {"reset time", "explicit, immutable snapshot", [](auto* req) {
         req->mutable_options()->mutable_read_time_options()->set_read_time_manipulation(
             tserver::ReadTimeManipulation::RESTART);
       }},
      {"table locks", "transaction or table-lock state", [](auto* req) {
         req->mutable_options()->set_is_using_table_locks(true);
       }},
      {"write time", "cannot write or use a transaction", [](auto* req) {
         req->set_write_time(req->options().read_time_options().read_time().read_ht());
       }},
      {"write", "only supports pure catalog reads", [&](auto* req) {
         req->clear_ops();
         *req->add_ops() = write;
       }},
      {"mixed batch", "only supports pure catalog reads", [&](auto* req) {
         *req->add_ops() = write;
       }},
      {"row lock", "only supports pure catalog reads", [](auto* req) {
         req->mutable_ops(0)->mutable_read()->set_row_mark_type(RowMarkType::ROW_MARK_EXCLUSIVE);
       }},
      {"paging time", "Conflicting authentication catalog paging read time", [](auto* req) {
         const auto time = ReadHybridTime::FromPB(req->options().read_time_options().read_time());
         ReadHybridTime::SingleTime(time.read.Incremented()).ToPB(
             req->mutable_ops(0)->mutable_read()->mutable_paging_state()->mutable_read_time());
       }},
      {"nested paging time", "Conflicting authentication catalog paging read time", [&](auto* req) {
         const auto time = ReadHybridTime::FromPB(req->options().read_time_options().read_time());
         *req->mutable_ops(0) = indexed_read;
         ReadHybridTime::SingleTime(time.read.Incremented()).ToPB(
             req->mutable_ops(0)->mutable_read()->mutable_index_request()->mutable_paging_state()
                 ->mutable_read_time());
       }},
  };

  for (const auto& mutation : mutations) {
    SCOPED_TRACE(mutation.name);
    std::atomic<bool> injected{false}, invalid_argument{false};
    std::atomic<size_t> reads_after_mutation{0};
    ScopedPerformMock after(server(), ScopedPerformMock::Phase::kAfter,
        [&](const auto* req, const auto* resp, auto*) {
          if (req->options().ysql_auth_catalog_read()) {
            invalid_argument =
                resp->has_status() && StatusFromPB(resp->status()).IsInvalidArgument();
          }
          return Status::OK();
        });
    RequestTrace trace(server(), [&](const auto&) {
      if (injected) {
        ++reads_after_mutation;
      }
    }, [&](auto* req) {
      if (req->options().ysql_auth_catalog_read() && !injected.exchange(true)) {
        ASSERT_FALSE(req->ops().empty());
        ASSERT_TRUE(req->ops(0).has_read());
        mutation.apply(req);
      }
    });
    const auto snapshots = Snapshots();
    const auto followers = FollowerReads();
    const auto leaders = LeaderReads();
    const auto queries = METRIC_pg_response_cache_queries.Instantiate(
        server()->metric_entity())->value();
    ASSERT_NOK_STR_CONTAINS(ConnectUser(), mutation.message);
    ASSERT_TRUE(injected);
    ASSERT_TRUE(invalid_argument);
    ASSERT_EQ(reads_after_mutation, 0);
    ASSERT_EQ(Snapshots(), snapshots + 1);
    ASSERT_EQ(FollowerReads(), followers);
    ASSERT_EQ(LeaderReads(), leaders);
    ASSERT_EQ(METRIC_pg_response_cache_queries.Instantiate(
        server()->metric_entity())->value(), queries);
    trace.Stop();
    ASSERT_EQ(ASSERT_RESULT(admin.FetchRow<int64_t>("SELECT count(*) FROM auth_write_guard")), 0);
  }
  ASSERT_NO_FATAL_FAILURE(AssertFreshLogin(kOldPassword));
}

class PgAuthFollowerIdleTest : public PgAuthFollowerReadsTest {};

TEST_F(PgAuthFollowerIdleTest, IdleCatalogUsesFollowersWithProductionTimeouts) {
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateRoles(&admin));
  // No catalog writes occur during these attempts. The acquisition's fresh floor must become
  // safe on followers through idle Raft heartbeats, within the production RPC budget.
  for (int attempt = 0; attempt != 3; ++attempt) {
    RequestTrace trace(server());
    ASSERT_NO_FATAL_FAILURE(AssertFreshLogin(kOldPassword));
    ASSERT_RESULT(CheckSingleSnapshot(trace.Get()));
  }
}

TEST_F(PgAuthFollowerResponseCacheTest, MarkedPerformCannotUseWarmResponseCache) {
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateRoles(&admin));
  const auto version = ASSERT_RESULT(admin.FetchRow<int64_t>(Format(
      "SELECT current_version FROM pg_yb_catalog_version WHERE db_oid = $0", kTemplate1Oid)));
  ASSERT_OK(WaitFor([&] {
    uint64_t current_version = 0, last_breaking_version = 0;
    server()->get_ysql_db_catalog_version(
        kTemplate1Oid, &current_version, &last_breaking_version, false /* use_cache */);
    return current_version >= static_cast<uint64_t>(version);
  }, 30s * kTimeMultiplier, "Publish catalog version before warming authentication cache"));

  tserver::PgPerformRequestPB warm_request;
  {
    RequestTrace trace(server());
    ASSERT_RESULT(ConnectUser());
    const auto hits = CacheCounter(METRIC_pg_response_cache_hits);
    trace.Clear();
    ASSERT_RESULT(ConnectUser());
    ASSERT_GT(CacheCounter(METRIC_pg_response_cache_hits), hits);
    for (const auto& req : trace.Get().performs) {
      for (const auto& op : req.ops()) {
        if (op.has_read() && op.read().table_id() == GetPgsqlTableId(kTemplate1Oid, kAuthIdOid) &&
            req.options().has_caching_info()) {
          warm_request = req;
        }
      }
    }
  }
  ASSERT_TRUE(warm_request.options().has_caching_info());

  auto proxy = ASSERT_RESULT(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>());
  master::GetYsqlAuthCatalogReadTimeRequestPB req;
  master::GetYsqlAuthCatalogReadTimeResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(30s * kTimeMultiplier);
  ASSERT_OK(proxy.GetYsqlAuthCatalogReadTime(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
  const auto snapshot = ReadHybridTime::SingleTime(HybridTime(resp.read_time()));

  std::atomic<bool> injected{false}, invalid_argument{false};
  std::atomic<size_t> reads_after_mutation{0};
  std::atomic<uint64_t> hits_at_mutation{0}, queries_at_mutation{0};
  ScopedPerformMock after(server(), ScopedPerformMock::Phase::kAfter,
      [&](const auto* request, const auto* response, auto*) {
        if (request->options().ysql_auth_catalog_read()) {
          invalid_argument = response->has_status() &&
              StatusFromPB(response->status()).IsInvalidArgument();
        }
        return Status::OK();
      });
  RequestTrace trace(server(), [&](const auto&) {
    if (injected) {
      ++reads_after_mutation;
    }
  }, [&](auto* request) {
    const auto& caching = request->options().caching_info();
    const auto& warm_caching = warm_request.options().caching_info();
    if (request->options().has_caching_info() &&
        caching.key_group() == warm_caching.key_group() &&
        caching.key_value() == warm_caching.key_value() && !injected.exchange(true)) {
      auto* options = request->mutable_options();
      // Preserve the real warm key and otherwise supply a valid marked Perform envelope.
      auto cached = options->caching_info();
      options->Clear();
      options->set_use_legacy_catalog_session(true);
      options->set_ysql_auth_catalog_read(true);
      snapshot.ToPB(options->mutable_read_time_options()->mutable_read_time());
      *options->mutable_caching_info() = std::move(cached);
      // Earlier startup requests may legitimately use the cache before this request is marked.
      hits_at_mutation = CacheCounter(METRIC_pg_response_cache_hits);
      queries_at_mutation = CacheCounter(METRIC_pg_response_cache_queries);
    }
  });
  const auto snapshots = Snapshots();
  const auto followers = FollowerReads();
  const auto leaders = LeaderReads();
  ASSERT_NOK_STR_CONTAINS(ConnectUser(), "require an uncached legacy catalog session");
  ASSERT_TRUE(injected);
  ASSERT_TRUE(invalid_argument);
  ASSERT_EQ(reads_after_mutation, 0);
  ASSERT_EQ(CacheCounter(METRIC_pg_response_cache_hits), hits_at_mutation.load());
  ASSERT_EQ(CacheCounter(METRIC_pg_response_cache_queries), queries_at_mutation.load());
  ASSERT_EQ(Snapshots(), snapshots);
  ASSERT_EQ(FollowerReads(), followers);
  ASSERT_EQ(LeaderReads(), leaders);
}

class PgAuthFollowerFaultTest : public PgAuthFollowerPagingTest {
 protected:
  enum class Fault { kSnapshotTooOld, kMissingEcho, kChangedEcho, kReadRestart };

  Status CreatePagedRoles(PGConn* admin) {
    RETURN_NOT_OK(CreateRoles(admin));
    for (int i = 0; i != 16; ++i) {
      RETURN_NOT_OK(admin->ExecuteFormat("CREATE ROLE auth_padding_$0", i));
    }
    return Status::OK();
  }

  void AbortAfterPage(PGConn* admin, Fault fault) {
    CountDownLatch paused(1), resume(1), completed(1);
    std::atomic<bool> pause_once{true}, armed{false}, injected{false};
    std::atomic<uint64_t> snapshot{0};
    Status login_status;
    TestThreadHolder threads;
    RequestTrace trace(server(), [&](const auto& request) {
      if (HasAuthIdContinuation(request) && pause_once.exchange(false)) {
        snapshot = request.read_time().read_ht();
        paused.CountDown();
        resume.Wait();
      }
    });
    ScopedPerformMock after(server(), ScopedPerformMock::Phase::kAfter,
        [&](const auto* request, auto* response, auto*) -> Status {
          if (fault == Fault::kReadRestart || !armed ||
              !HasAuthIdContinuation(request->ToGoogleProtobuf()) || injected.exchange(true)) {
            return Status::OK();
          }
          RETURN_NOT_OK(ResponseStatus(*response));
          SCHECK(response->has_catalog_read_time(), IllegalState, "Expected a real page response");
          switch (fault) {
            case Fault::kSnapshotTooOld:
              return STATUS(SnapshotTooOld, "Injected expired authentication page snapshot");
            case Fault::kMissingEcho:
              response->clear_catalog_read_time();
              break;
            case Fault::kChangedEcho:
              ReadHybridTime::SingleTime(HybridTime(snapshot.load()).Incremented()).ToPB(
                  response->mutable_catalog_read_time());
              break;
            case Fault::kReadRestart:
              break;
          }
          return Status::OK();
        });
    if (fault == Fault::kReadRestart) {
      SyncPoint::GetInstance()->SetCallBack("ReadRpc::NotifyBatcher", [&](void* arg) {
        if (!armed || injected) {
          return;
        }
        auto* response = static_cast<tserver::ReadResponseMsg*>(arg);
        for (const auto& page : response->pgsql_batch()) {
          if (page.has_paging_state() &&
              page.paging_state().read_time().read_ht() == snapshot && !injected.exchange(true)) {
            ReadHybridTime::SingleTime(HybridTime(snapshot.load()).Incremented()).ToPB(
                response->mutable_restart_read_time());
            return;
          }
        }
      });
    }
    auto cleanup = ScopeExit([&] {
      armed = false;
      resume.CountDown();
      after.Stop();
      trace.Stop();
      threads.JoinAll();
    });
    const auto snapshots = Snapshots();
    threads.AddThread([&] {
      login_status = ResultToStatus(ConnectUser());
      completed.CountDown();
    });
    ASSERT_TRUE(paused.WaitFor(30s * kTimeMultiplier));
    ASSERT_EQ(Snapshots(), snapshots + 1);
    ASSERT_OK(admin->ExecuteFormat("ALTER ROLE auth_user PASSWORD '$0'", kNewPassword));
    armed = true;
    resume.CountDown();
    ASSERT_TRUE(completed.WaitFor(30s * kTimeMultiplier));
    armed = false;
    after.Stop();
    trace.Stop();
    threads.JoinAll();
    ASSERT_TRUE(injected);
    switch (fault) {
      case Fault::kSnapshotTooOld:
        ASSERT_NOK_STR_CONTAINS(login_status, "Injected expired authentication page snapshot");
        break;
      case Fault::kMissingEcho:
      case Fault::kChangedEcho:
        ASSERT_NOK_STR_CONTAINS(login_status, "response did not preserve the fixed snapshot");
        break;
      case Fault::kReadRestart:
        ASSERT_NOK_STR_CONTAINS(login_status, "Authentication catalog read cannot restart");
        break;
    }
    ASSERT_EQ(Snapshots(), snapshots + 1);
    const auto requests = trace.Get();
    const auto failed_snapshot = ASSERT_RESULT(CheckSingleSnapshot(requests));
    ASSERT_EQ(failed_snapshot.ToUint64(), snapshot.load());
    uint64_t session_id = 0;
    size_t pages = 0;
    for (const auto& request : requests.performs) {
      if (request.options().ysql_auth_catalog_read() && !session_id) {
        session_id = request.session_id();
      }
      if (session_id && request.session_id() == session_id) {
        ASSERT_TRUE(request.options().ysql_auth_catalog_read());
        ASSERT_FALSE(request.options().has_caching_info());
        ++pages;
      }
    }
    ASSERT_GE(pages, 2);

    RequestTrace next_trace(server());
    ASSERT_RESULT(ConnectUser(kNewPassword));
    ASSERT_EQ(Snapshots(), snapshots + 2);
    ASSERT_GT(ASSERT_RESULT(CheckSingleSnapshot(next_trace.Get())), failed_snapshot);
    ASSERT_NO_FATAL_FAILURE(AssertDenied(kOldPassword, "password authentication failed"));
    ASSERT_OK(admin->ExecuteFormat("ALTER ROLE auth_user PASSWORD '$0'", kOldPassword));
  }
};

TEST_F(PgAuthFollowerFaultTest, SnapshotTooOldAbortsWholeAttempt) {
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreatePagedRoles(&admin));
  ASSERT_NO_FATAL_FAILURE(AbortAfterPage(&admin, Fault::kSnapshotTooOld));
}

TEST_F(PgAuthFollowerFaultTest, MissingOrChangedSnapshotEchoAbortsWholeAttempt) {
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreatePagedRoles(&admin));
  for (const auto fault : {Fault::kMissingEcho, Fault::kChangedEcho}) {
    SCOPED_TRACE(static_cast<int>(fault));
    ASSERT_NO_FATAL_FAILURE(AbortAfterPage(&admin, fault));
  }
}

TEST_F(PgAuthFollowerFaultTest, ReadRestartAbortsWholeAttempt) {
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreatePagedRoles(&admin));
  ASSERT_NO_FATAL_FAILURE(AbortAfterPage(&admin, Fault::kReadRestart));
}

// PITR stays available with follower routing. Pre-restore catalogs accept the newer password;
// restored catalogs reject it.
TEST_F(PgAuthFollowerReadsTest, SharedCatalogRestoreReachesFollowerLogins) {
  auto admin = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateRoles(&admin));
  client::SnapshotTestUtil snapshots(*cluster_, cluster_->proxy_cache());
  const auto schedule =
      ASSERT_RESULT(snapshots.CreateSchedule("template1", client::WaitSnapshot::kFalse));
  const auto snapshot = ASSERT_RESULT(snapshots.WaitScheduleSnapshot(schedule));
  const auto restore_at = HybridTime::FromPB(snapshot.entry().snapshot_hybrid_time());
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->Now() > restore_at;
  }, 30s * kTimeMultiplier, "Pass the snapshot time"));
  ASSERT_OK(admin.ExecuteFormat("ALTER ROLE auth_user PASSWORD '$0'", kNewPassword));
  ASSERT_OK(admin.Execute("REVOKE auth_group FROM auth_user"));
  ASSERT_NO_FATAL_FAILURE(AssertDenied(kNewPassword, "pg_hba.conf rejects connection"));

  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  // Master 0 shares the tserver's IP; partitioning it would also block DDL object-lock RPCs.
  const size_t lagging_idx = cluster_->mini_master(1) == leader ? 2 : 1;
  auto* lagging = cluster_->mini_master(lagging_idx);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_election_when_fail_detected) = true;
  std::vector<IpAddress> addresses;
  for (auto privacy : {server::Private::kTrue, server::Private::kFalse}) {
    addresses.push_back(ASSERT_RESULT(
        HostToAddress(server::TEST_RpcAddress(lagging_idx + 1, privacy))));
  }
  auto heal = [&] {
    for (const auto& address : addresses) {
      leader->messenger().RestoreConnectivityTo(address);
    }
  };
  auto cleanup = ScopeExit([&] {
    heal();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_election_when_fail_detected) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_sys_catalog_restore_on_followers_secs) = 0;
  });
  for (const auto& address : addresses) {
    leader->messenger().BreakConnectivityTo(address);
  }
  ASSERT_OK(snapshots.RestoreSnapshotSchedule(schedule, restore_at));

  // Refresh replica state, which clears earlier failure marks, and route to the lagging follower.
  auto prefer_lagging = [&]() -> Status {
    auto remote = VERIFY_RESULT(CatalogTablet());
    for (auto* replica : remote->GetRemoteTabletServers()) {
      if (replica->permanent_uuid() != leader->permanent_uuid() &&
          replica->permanent_uuid() != lagging->permanent_uuid()) {
        remote->MarkReplicaFailed(replica, STATUS(NetworkError, "Route to lagged peer"));
      }
    }
    return Status::OK();
  };
  ASSERT_OK(prefer_lagging());
  ASSERT_NO_FATAL_FAILURE(AssertDenied(kNewPassword, "password authentication failed"));

  // Catch up while the restore apply is delayed: its safe time can pass T before the apply.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_sys_catalog_restore_on_followers_secs) = 3;
  heal();
  SleepFor(MonoDelta::FromMilliseconds(500));
  ASSERT_OK(prefer_lagging());
  ASSERT_NO_FATAL_FAILURE(AssertDenied(kNewPassword, "password authentication failed"));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_sys_catalog_restore_on_followers_secs) = 0;
  auto tablet = ASSERT_RESULT(lagging->master()->sys_catalog().tablet_peer()->shared_tablet());
  const auto caught_up = leader->Now();
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(tablet->SafeTime(tablet::RequireLease::kFalse)) >= caught_up;
  }, 30s * kTimeMultiplier, "Apply the restore on the lagging follower"));
  ASSERT_OK(prefer_lagging());
  ASSERT_NO_FATAL_FAILURE(AssertFreshLogin(kOldPassword));
}

}  // namespace yb::pgwrapper
