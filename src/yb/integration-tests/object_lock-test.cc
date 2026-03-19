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

#include <algorithm>
#include <functional>
#include <future>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "yb/common/ysql_operation_lease.h"

#include "yb/docdb/lock_util.h"
#include "yb/docdb/object_lock_data.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster_client.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_ddl_client.h"
#include "yb/master/mini_master.h"
#include "yb/master/object_lock_info_manager.h"
#include "yb/master/test_async_rpc_manager.h"
#include "yb/master/ts_manager.h"

#include "yb/rpc/messenger.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/status_callback.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/unique_lock.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::chrono_literals;

DECLARE_bool(TEST_check_broadcast_address);
DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_bool(TEST_tserver_disable_heartbeat);
DECLARE_bool(TEST_skip_launch_release_request);
DECLARE_int32(heartbeat_max_failures_before_backoff);
DECLARE_int32(retrying_ts_rpc_max_delay_ms);
DECLARE_int32(retrying_rpc_max_jitter_ms);
DECLARE_uint64(master_ysql_operation_lease_ttl_ms);
DECLARE_uint64(ysql_lease_refresher_interval_ms);
DECLARE_double(TEST_tserver_ysql_lease_refresh_failure_prob);
DECLARE_bool(enable_load_balancing);
DECLARE_uint64(object_lock_cleanup_interval_ms);
DECLARE_bool(TEST_olm_skip_sending_wait_for_probes);

namespace yb {

namespace {
Status BuildLeaseEpochMismatchErrorStatus(uint64_t client_lease_epoch, uint64_t server_lease_epoch);

bool StatusContainsMessage(const Status& status, std::string_view s);

bool PGSessionKilledStatus(const Status& status);

bool SameCodeAndMessage(const Status& lhs, const Status& rhs);

MATCHER_P(EqualsStatus, expected_status, "") {
  return arg.code() == expected_status.code() &&
         arg.message().ToBuffer() == expected_status.message().ToBuffer();
}

MATCHER_P(EqualsCode, expected_status, "") {
  return arg.code() == expected_status.code();
}

Result<master::YSQLLeaseInfoPB> GetTServerLeaseInfo(MiniCluster& cluster, const std::string& uuid);

Result<master::YSQLLeaseInfoPB> GetTServerLeaseInfo(
    const master::MasterClusterClient& client, const std::string& uuid);
}  // namespace

constexpr uint64_t kDefaultMasterYSQLLeaseTTLMilli = 5 * 1000;
constexpr uint64_t kDefaultYSQLLeaseRefreshIntervalMilli = 500;
const std::string kTServerYsqlLeaseRefreshFlagName = "TEST_tserver_enable_ysql_lease_refresh";
constexpr uint64_t kDefaultMasterObjectLockCleanupIntervalMilli = 500;
const MonoDelta kDefaultInitialLeaseAcquisitionTime = MonoDelta::FromSeconds(10);

class ObjectLockTest : public MiniClusterTestWithClient<MiniCluster> {
 public:
  ObjectLockTest() {}

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_locking_for_table_locks) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ddl_transaction_block_enabled) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_ysql_operation_lease_ttl_ms) =
        kDefaultMasterYSQLLeaseTTLMilli;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_lease_refresher_interval_ms) =
        kDefaultYSQLLeaseRefreshIntervalMilli;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_object_lock_cleanup_interval_ms) = 500;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_olm_skip_sending_wait_for_probes) = true;
    MiniClusterTestWithClient::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    opts.num_masters = num_masters();
    cluster_ = std::make_unique<MiniCluster>(opts);
    ASSERT_OK(cluster_->Start());
    for (auto& ts : cluster_->mini_tablet_servers()) {
      ASSERT_OK(StartTabletServerYSQLPoller(*ts));
    }
    ASSERT_OK(WaitForTabletServersToAcquireYSQLLeases(
        cluster_->mini_tablet_servers(), kDefaultInitialLeaseAcquisitionTime));

    rpc::MessengerBuilder bld("Client");
    client_messenger_ = ASSERT_RESULT(bld.Build());
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(client_messenger_.get());
  }

  std::vector<docdb::ObjectLockOwner> CreateRandomExclusiveLockOwners(int num_txns) {
    std::vector<docdb::ObjectLockOwner> lock_owners;
    lock_owners.reserve(num_txns);
    for (int i = 0; i < num_txns; i++) {
      lock_owners.push_back(docdb::ObjectLockOwner{TransactionId::GenerateRandom(), 1});
    }
    return lock_owners;
  }

  void DoBeforeTearDown() override {
    client_messenger_->Shutdown();
    MiniClusterTestWithClient::DoBeforeTearDown();
  }

  Result<tserver::MiniTabletServer*> AddTabletServer(MonoDelta timeout) {
    auto num_ts = cluster_->num_tablet_servers();
    RETURN_NOT_OK(cluster_->AddTabletServer());
    auto* ts = cluster_->mini_tablet_server(num_ts);
    RETURN_NOT_OK(StartTabletServerYSQLPoller(*ts));
    RETURN_NOT_OK(WaitForTabletServersToAcquireYSQLLeases({ts}, timeout));
    return ts;
  }

  Status RestartTabletServer(tserver::MiniTabletServer& ts, MonoDelta timeout) {
    ts.Shutdown();
    RETURN_NOT_OK(ts.Start());
    RETURN_NOT_OK(StartTabletServerYSQLPoller(ts));
    return WaitForTabletServersToAcquireYSQLLeases({&ts}, timeout);
  }

  Status StartTabletServerYSQLPoller(tserver::MiniTabletServer& ts) {
    return ts.server()->StartYSQLLeaseRefresher();
  }

  Status WaitForTabletServersToAcquireYSQLLeases(
      const MiniCluster::MiniTabletServers& tablet_servers, MonoDelta timeout) {
    std::vector<tserver::MiniTabletServer*> raw_pointer_tss;
    for (const auto& ts : tablet_servers) {
      raw_pointer_tss.push_back(ts.get());
    }
    return WaitForTabletServersToAcquireYSQLLeases(raw_pointer_tss, timeout);
  }

  Status WaitForTabletServersToAcquireYSQLLeases(
      const std::vector<tserver::MiniTabletServer*>& tablet_servers, MonoDelta timeout) {
    return WaitFor(
        [tablet_servers]() -> Result<bool> {
          for (const auto& ts : tablet_servers) {
            auto lease_info = VERIFY_RESULT(ts->server()->GetYSQLLeaseInfo());
            if (!lease_info.is_live) {
              return false;
            }
          }
          return true;
        },
        timeout, "Waiting for all tservers to acquire a lease");
  }

 protected:
  virtual int num_masters() { return 1; }

  tserver::TabletServerServiceProxy TServerProxyFor(const tserver::MiniTabletServer* tserver) {
    return tserver::TabletServerServiceProxy{
        proxy_cache_.get(), HostPort::FromBoundEndpoint(tserver->bound_rpc_addr())};
  }

  tserver::TabletServerServiceProxy TServerProxy(size_t i) {
    return TServerProxyFor(cluster_->mini_tablet_server(i));
  }

  master::MasterDdlProxy MasterProxy(const master::MiniMaster* master) {
    return master::MasterDdlProxy{proxy_cache_.get(), master->bound_rpc_addr()};
  }

  Result<master::MasterDdlProxy> MasterLeaderProxy() {
    return MasterProxy(VERIFY_RESULT(cluster_->GetLeaderMiniMaster()));
  }

  const std::string& TSUuid(size_t ts_idx) const {
    return cluster_->mini_tablet_server(ts_idx)->server()->permanent_uuid();
  }

  const std::string& MasterUuid(size_t idx) const {
    return cluster_->mini_master(idx)->master()->permanent_uuid();
  }

  void testAcquireObjectLockWaitsOnTServer(bool do_master_failover);

  Status WaitForTServerLeaseToExpire(const std::string& uuid, MonoDelta timeout);

 private:
  std::unique_ptr<rpc::Messenger> client_messenger_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
};

using FlagValue = std::variant<int64_t, uint64_t, bool, std::string>;

class FlagMap {
 public:
  explicit FlagMap(std::initializer_list<std::pair<std::string, FlagValue>> flags);
  FlagMap() = default;

  std::vector<std::string> ToFormattedVector();
  void OverrideValues(const FlagMap& overrides);

 private:
  std::unordered_map<std::string, FlagValue> map_;
};

struct ClusterFlags {
  FlagMap tserver_flags;
  FlagMap master_flags;

  void OverrideValues(const ClusterFlags& overrides);
  std::vector<std::string> GetFormattedTServerFlags();
  std::vector<std::string> GetFormattedMasterFlags();
};

class ExternalObjectLockTest : public YBMiniClusterTestBase<ExternalMiniCluster> {
 public:
  void SetUp() override;
  virtual ExternalMiniClusterOptions MakeExternalMiniClusterOptions();
  ClusterFlags BaseFlags();
  virtual ClusterFlags FlagOverrides() { return ClusterFlags(); }
  virtual int ReplicationFactor() { return 3; }
  virtual size_t NumberOfTabletServers() { return 3; }
  virtual bool WaitForTServersToAcceptYSQL() { return true; }
  ExternalTabletServer* tablet_server(size_t index);
  Status WaitForTServerLeaseToExpire(const std::string& ts_uuid, MonoDelta timeout);
  Status WaitForTServerLease(const std::string& ts_uuid, MonoDelta timeout);
};

class ExternalObjectLockTestOneTS : public ExternalObjectLockTest {
 public:
  int ReplicationFactor() override { return 1; }
  size_t NumberOfTabletServers() override { return 1; }
};

auto kTxn1 = docdb::ObjectLockOwner{TransactionId::GenerateRandom(), 1};
auto kTxn2 = docdb::ObjectLockOwner{TransactionId::GenerateRandom(), 1};

constexpr uint64_t kDatabaseID = 1;
constexpr uint64_t kRelationId = 1;
constexpr uint64_t kRelationId2 = 2;
constexpr uint64_t kRelationId3 = 3;
constexpr uint64_t kDefaultObjectId = 0;
constexpr uint64_t kDefaultObjectSubId = 0;
constexpr uint64_t kLeaseEpoch = 1;
const MonoDelta kTimeout = MonoDelta::FromSeconds(8);
constexpr auto kDefaultTestStatusTabletId = "test_status_tablet";

template <typename Request>
Request AcquireRequestFor(
    const std::string& session_host_uuid, const docdb::ObjectLockOwner& owner, uint64_t database_id,
    uint64_t relation_id, TableLockType lock_type, uint64_t lease_epoch, server::ClockPtr clock,
    std::optional<HybridTime> deadline) {
  Request req;
  owner.PopulateLockRequest(&req);
  req.set_status_tablet(kDefaultTestStatusTabletId);
  req.set_session_host_uuid(session_host_uuid);
  req.set_lease_epoch(lease_epoch);
  if (deadline) {
    req.set_ignore_after_hybrid_time(deadline->ToUint64());
  }
  if (clock) {
    req.set_propagated_hybrid_time(clock->Now().ToUint64());
  }
  auto* lock = req.add_object_locks();
  lock->set_database_oid(database_id);
  lock->set_relation_oid(relation_id);
  lock->set_object_oid(kDefaultObjectId);
  lock->set_object_sub_oid(kDefaultObjectSubId);
  lock->set_lock_type(lock_type);
  return req;
}

rpc::RpcController RpcController(MonoDelta rpc_timeout = kTimeout) {
  rpc::RpcController controller;
  controller.set_timeout(rpc_timeout);
  return controller;
}

Status ResolveFutureStatus(std::future<Status>& future) {
  future.wait();
  return future.get();
}

template <typename Request, typename Response, typename Proxy>
void CallProxyMethod(
    Proxy* proxy, const Request& req, Response* resp, rpc::RpcController* controller,
    rpc::ResponseCallback callback);

template <>
void CallProxyMethod(
    master::MasterDdlProxy* proxy, const master::AcquireObjectLocksGlobalRequestPB& req,
    master::AcquireObjectLocksGlobalResponsePB* resp, rpc::RpcController* controller,
    rpc::ResponseCallback callback) {
  proxy->AcquireObjectLocksGlobalAsync(req, resp, controller, std::move(callback));
}

template <>
void CallProxyMethod(
    tserver::TabletServerServiceProxy* proxy, const tserver::AcquireObjectLockRequestPB& req,
    tserver::AcquireObjectLockResponsePB* resp, rpc::RpcController* controller,
    rpc::ResponseCallback callback) {
  proxy->AcquireObjectLocksAsync(req, resp, controller, std::move(callback));
}

template <typename Request, typename Response, typename Proxy>
std::future<Status> AcquireLockAsync(
    Proxy* proxy, const std::string& session_host_uuid, const docdb::ObjectLockOwner& owner,
    uint64_t database_id, uint64_t relation_id, TableLockType type, uint64_t lease_epoch,
    server::ClockPtr clock, std::optional<HybridTime> opt_deadline, MonoDelta rpc_timeout) {
  auto resp = std::make_shared<Response>();
  auto controller = std::make_shared<rpc::RpcController>();
  controller->set_timeout(rpc_timeout);
  auto promise = std::make_shared<std::promise<Status>>();
  auto future = promise->get_future();
  auto req = AcquireRequestFor<Request>(
      session_host_uuid, owner, database_id, relation_id, type, lease_epoch, clock, opt_deadline);
  auto callback = [promise, resp, controller, clock]() {
    if (clock && resp->has_propagated_hybrid_time()) {
      clock->Update(HybridTime(resp->propagated_hybrid_time()));
    }
    if (!controller->status().ok()) {
      promise->set_value(controller->status());
    } else if (resp->has_error()) {
      promise->set_value(ResponseStatus(*resp));
    } else {
      promise->set_value(Status::OK());
    }
  };
  CallProxyMethod(proxy, req, resp.get(), controller.get(), std::move(callback));
  return future;
}

std::future<Status> AcquireLockAsyncAt(
    tserver::TabletServerServiceProxy* proxy, const std::string& session_host_uuid,
    const docdb::ObjectLockOwner& owner, uint64_t database_id, uint64_t relation_id,
    uint64_t lease_epoch = kLeaseEpoch, server::ClockPtr clock = nullptr,
    std::optional<HybridTime> deadline = std::nullopt, MonoDelta rpc_timeout = kTimeout) {
  return AcquireLockAsync<
      tserver::AcquireObjectLockRequestPB, tserver::AcquireObjectLockResponsePB,
      tserver::TabletServerServiceProxy>(
      proxy, session_host_uuid, owner, database_id, relation_id, TableLockType::ACCESS_SHARE,
      lease_epoch, clock, deadline, rpc_timeout);
}

Status AcquireLockAt(
    tserver::TabletServerServiceProxy* proxy, const std::string& session_host_uuid,
    const docdb::ObjectLockOwner& owner, uint64_t database_id, uint64_t relation_id,
    uint64_t lease_epoch = kLeaseEpoch, server::ClockPtr clock = nullptr,
    std::optional<HybridTime> deadline = std::nullopt, MonoDelta rpc_timeout = kTimeout) {
  auto future = AcquireLockAsyncAt(
      proxy, session_host_uuid, owner, database_id, relation_id, lease_epoch, clock, deadline,
      rpc_timeout);
  return ResolveFutureStatus(future);
}

std::future<Status> AcquireLockGloballyAsync(
    master::MasterDdlProxy* proxy, const std::string& session_host_uuid,
    const docdb::ObjectLockOwner& owner, uint64_t database_id, uint64_t relation_id,
    uint64_t lease_epoch = kLeaseEpoch, server::ClockPtr clock = nullptr,
    std::optional<HybridTime> deadline = std::nullopt, MonoDelta rpc_timeout = kTimeout) {
  return AcquireLockAsync<
      master::AcquireObjectLocksGlobalRequestPB, master::AcquireObjectLocksGlobalResponsePB,
      master::MasterDdlProxy>(
      proxy, session_host_uuid, owner, database_id, relation_id, TableLockType::ACCESS_EXCLUSIVE,
      lease_epoch, clock, deadline, rpc_timeout);
}

Status AcquireLockGlobally(
    master::MasterDdlProxy* proxy, const std::string& session_host_uuid,
    const docdb::ObjectLockOwner& owner, uint64_t database_id, uint64_t relation_id,
    uint64_t lease_epoch = kLeaseEpoch, server::ClockPtr clock = nullptr,
    std::optional<HybridTime> deadline = std::nullopt, MonoDelta rpc_timeout = kTimeout) {
  auto future = AcquireLockGloballyAsync(
      proxy, session_host_uuid, owner, database_id, relation_id, lease_epoch, clock, deadline,
      rpc_timeout);
  return ResolveFutureStatus(future);
}

std::future<Status> AcquireLockGloballyAsync(
    client::YBClient* client, const std::string& session_host_uuid,
    const docdb::ObjectLockOwner& owner, uint64_t database_id, uint64_t relation_id,
    uint64_t lease_epoch = kLeaseEpoch, std::optional<HybridTime> opt_deadline = std::nullopt,
    MonoDelta rpc_timeout = kTimeout) {
  auto promise = std::make_shared<std::promise<Status>>();
  auto future = promise->get_future();
  auto req = AcquireRequestFor<master::AcquireObjectLocksGlobalRequestPB>(
      session_host_uuid, owner, database_id, relation_id, TableLockType::ACCESS_EXCLUSIVE,
      lease_epoch, client->Clock(), opt_deadline);
  auto callback = [promise](const Status& s) { promise->set_value(s); };
  client->AcquireObjectLocksGlobalAsync(
      req, std::move(callback), ToCoarse(MonoTime::Now() + rpc_timeout),
      []() { return Status::OK(); } /* should_retry */);
  return future;
}

template <typename Request>
Request ReleaseRequestFor(
    const std::string& session_host_uuid, const docdb::ObjectLockOwner& owner,
    uint64_t lease_epoch = kLeaseEpoch, server::ClockPtr clock = nullptr,
    std::optional<HybridTime> apply_after = std::nullopt) {
  Request req;
  owner.PopulateLockRequest(&req);
  req.set_session_host_uuid(session_host_uuid);
  req.set_lease_epoch(lease_epoch);
  if (apply_after) {
    req.set_apply_after_hybrid_time(apply_after->ToUint64());
  }
  if (clock) {
    req.set_propagated_hybrid_time(clock->Now().ToUint64());
  }
  return req;
}

std::future<Result<tserver::ReleaseObjectLockResponsePB>> ReleaseLockAtAsync(
    tserver::TabletServerServiceProxy* proxy, const std::string& session_host_uuid,
    const docdb::ObjectLockOwner& owner, uint64_t lease_epoch = kLeaseEpoch,
    server::ClockPtr clock = nullptr, std::optional<HybridTime> apply_after = std::nullopt,
    MonoDelta rpc_timeout = kTimeout) {
  auto resp = std::make_shared<tserver::ReleaseObjectLockResponsePB>();
  auto controller = std::make_shared<rpc::RpcController>();
  controller->set_timeout(rpc_timeout);
  auto req = ReleaseRequestFor<tserver::ReleaseObjectLockRequestPB>(
      session_host_uuid, owner, lease_epoch, clock, apply_after);
  auto promise = std::make_shared<std::promise<Result<tserver::ReleaseObjectLockResponsePB>>>();
  auto callback = [promise, controller, resp]() {
    if (!controller->status().ok()) {
      promise->set_value(controller->status());
    } else if (resp->has_error()) {
      promise->set_value(ResponseStatus(*resp));
    } else {
      promise->set_value(*resp);
    }
  };
  proxy->ReleaseObjectLocksAsync(req, resp.get(), controller.get(), std::move(callback));
  return promise->get_future();
}

Status ReleaseLockAt(
    tserver::TabletServerServiceProxy* proxy, const std::string& session_host_uuid,
    const docdb::ObjectLockOwner& owner, uint64_t lease_epoch = kLeaseEpoch,
    server::ClockPtr clock = nullptr, std::optional<HybridTime> apply_after = std::nullopt,
    MonoDelta rpc_timeout = kTimeout) {
  auto result = ReleaseLockAtAsync(
                    proxy, session_host_uuid, owner, lease_epoch, clock, apply_after, rpc_timeout)
                    .get();
  if (clock && result.ok() && result->has_propagated_hybrid_time()) {
    clock->Update(HybridTime(result->propagated_hybrid_time()));
  }
  return ResultToStatus(result);
}

Status ReleaseLockGloballyAt(
    master::MasterDdlProxy* proxy, const std::string& session_host_uuid,
    const docdb::ObjectLockOwner& owner, uint64_t lease_epoch = kLeaseEpoch,
    server::ClockPtr clock = nullptr, std::optional<HybridTime> apply_after = std::nullopt,
    MonoDelta rpc_timeout = kTimeout) {
  master::ReleaseObjectLocksGlobalResponsePB resp;
  rpc::RpcController controller = RpcController();
  controller.set_timeout(rpc_timeout);
  auto req = ReleaseRequestFor<master::ReleaseObjectLocksGlobalRequestPB>(
      session_host_uuid, owner, lease_epoch, clock, apply_after);
  return proxy->ReleaseObjectLocksGlobal(req, &resp, &controller);
}

Status ReleaseLockGlobally(
    client::YBClient* client, const std::string& session_host_uuid,
    const docdb::ObjectLockOwner& owner, uint64_t lease_epoch = kLeaseEpoch,
    std::optional<HybridTime> apply_after = std::nullopt,
    MonoDelta rpc_timeout = kTimeout) {
  auto req = ReleaseRequestFor<master::ReleaseObjectLocksGlobalRequestPB>(
      session_host_uuid, owner, lease_epoch, client->Clock(), apply_after);
  Synchronizer sync;
  client->ReleaseObjectLocksGlobalAsync(
      req, sync.AsStdStatusCallback(), ToCoarse(MonoTime::Now() + rpc_timeout));
  return sync.Wait();
}

YB_DEFINE_ENUM(FailureMode, (NoFailure)(MissingHeartbeatResponses)(RestartMasterDuringRelease));
class ObjectLockTestWithMissingResponsesAndMasterRestartDuringRelease
    : public ObjectLockTest,
      public ::testing::WithParamInterface<FailureMode> {};

TEST_P(
    ObjectLockTestWithMissingResponsesAndMasterRestartDuringRelease, AcquireReleaseLockGlobally) {
  const auto& kSessionHostUuid = TSUuid(0);
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  ASSERT_OK(AcquireLockGlobally(&master_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kRelationId));
  if (GetParam() == FailureMode::MissingHeartbeatResponses) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_ysql_lease_refresh_failure_prob) = 0.5;
  } else if (GetParam() == FailureMode::RestartMasterDuringRelease) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_launch_release_request) = true;
  }
  ASSERT_OK(ReleaseLockGloballyAt(&master_proxy, kSessionHostUuid, kTxn1));
  if (GetParam() == FailureMode::RestartMasterDuringRelease) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_launch_release_request) = false;
    ASSERT_OK(cluster_->mini_master()->Restart(/* wait_until_catalog_manager_is_leader */ true));
  }
  auto master_local_lock_manager = cluster_->mini_master()
                                       ->master()
                                       ->catalog_manager_impl()
                                       ->object_lock_info_manager()
                                       ->TEST_ts_local_lock_manager();
  ASSERT_OK(WaitFor(
      [master_local_lock_manager]() -> bool {
        return master_local_lock_manager->TEST_GrantedLocksSize() == 0;
      },
      kTimeout, "Wait for release to complete"));
  ASSERT_EQ(master_local_lock_manager->TEST_WaitingLocksSize(), 0);
  for (auto ts : cluster_->mini_tablet_servers()) {
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), 0);
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);
  }
}

INSTANTIATE_TEST_CASE_P(
    CauseMissingResponsesAndRestartMasterDuringRelease,
    ObjectLockTestWithMissingResponsesAndMasterRestartDuringRelease,
    ::testing::Values(
        FailureMode::NoFailure, FailureMode::MissingHeartbeatResponses,
        FailureMode::RestartMasterDuringRelease));

void ObjectLockTest::testAcquireObjectLockWaitsOnTServer(bool do_master_failover) {
  const auto& kSessionHostUuid = TSUuid(0);
  // Acquire lock on TServer-0
  auto* tserver0 = cluster_->mini_tablet_server(0);
  auto tserver0_proxy = TServerProxy(0);
  LOG(INFO) << "Taking DML lock on TServer-0, uuid: " << kSessionHostUuid;
  ASSERT_OK(AcquireLockAt(
      &tserver0_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kRelationId));

  ASSERT_EQ(tserver0->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);

  if (do_master_failover) {
    // Disable heartbeats. This is to ensure that the new master will have to rely on
    // the persisted registration information, and not the heartbeat to know about tserver-0
    LOG(INFO) << "Disabling heartbeats from TServers";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_disable_heartbeat) = true;
    auto old_master_id = CHECK_RESULT(cluster_->GetLeaderMiniMaster())->ToString();
    LOG(INFO) << "Doing master_failover. Old master was " << old_master_id;
    CHECK_RESULT(cluster_->StepDownMasterLeader());
    ASSERT_OK(LoggedWaitFor(
        [old_master_id, this]() -> Result<bool> {
          auto new_master_id = VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->ToString();
          LOG(INFO) << "Current master UUID: " << new_master_id;
          return old_master_id != new_master_id;
        },
        kTimeout, "wait for new master leader"));
  }
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  LOG(INFO) << "Requesting DDL lock at master : "
            << ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->ToString();
  auto acquire_future =
      AcquireLockGloballyAsync(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kRelationId);

  // Wait. But the lock acquisition should not be successful.
  EXPECT_OK(WaitFor(
      [tserver0]() -> bool {
        return tserver0->server()->ts_local_lock_manager()->TEST_WaitingLocksSize() > 0;
      },
      kTimeout, "wait for blocking on TServer0"));
  auto wait_for_future = acquire_future.wait_for(0.1s);
  EXPECT_EQ(wait_for_future, std::future_status::timeout);
  if (wait_for_future != std::future_status::timeout) {
    ASSERT_EQ(wait_for_future, std::future_status::ready);
    auto status = acquire_future.get();
    FAIL() << "Acquire should block, instead returned with status: " << status;
  }

  if (do_master_failover) {
    // Cluster verify in TearDown requires heartbeats to be enabled.
    LOG(INFO) << "Re-enabling heartbeats from TServers";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_disable_heartbeat) = false;
  }

  // Release lock at TServer-0
  LOG(INFO) << "Releasing DML lock at TServer-0";
  ASSERT_OK(ReleaseLockAt(&tserver0_proxy, kSessionHostUuid, kTxn1));

  // Verify that lock acquistion at master is successful.
  ASSERT_OK(ResolveFutureStatus(acquire_future));
  ASSERT_EQ(tserver0->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);
}

TEST_F(ObjectLockTest, AcquireObjectLocksWaitsOnTServer) {
  testAcquireObjectLockWaitsOnTServer(false);
}

TEST_F(ObjectLockTest, AcquireAndReleaseDDLLock) {
  const auto& kSessionHostUuid = TSUuid(0);
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  auto master_local_lock_manager = cluster_->mini_master()
                                       ->master()
                                       ->catalog_manager_impl()
                                       ->object_lock_info_manager()
                                       ->TEST_ts_local_lock_manager();
  ASSERT_OK(AcquireLockGlobally(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kRelationId));
  ASSERT_OK(ReleaseLockGloballyAt(&master_proxy, kSessionHostUuid, kTxn2));
  ASSERT_OK(WaitFor(
      [master_local_lock_manager]() -> bool {
        return master_local_lock_manager->TEST_WaitingLocksSize() == 0 &&
               master_local_lock_manager->TEST_GrantedLocksSize() == 0;
      },
      60s, "wait for DDL locks to clear at the master"));

  // Release non-existent lock.
  ASSERT_OK(ReleaseLockGloballyAt(&master_proxy, kSessionHostUuid, kTxn2));
}

void DumpMasterAndTServerLocks(
    MiniCluster* cluster, const std::string& message = "", bool dump_master_html = false,
    bool dump_tserver_html = false) {
  LOG(INFO) << message;
  for (auto& master : cluster->mini_masters()) {
    auto master_local_lock_manager = master->master()
                                         ->catalog_manager_impl()
                                         ->object_lock_info_manager()
                                         ->TEST_ts_local_lock_manager();
    LOG(INFO) << master->ToString()
              << " TestWaitingLocksSize: " << master_local_lock_manager->TEST_WaitingLocksSize()
              << " TestGrantedLocksSize: " << master_local_lock_manager->TEST_GrantedLocksSize();
    if (dump_master_html) {
      master_local_lock_manager->DumpLocksToHtml(LOG(INFO));
    }
  }
  for (auto& ts : cluster->mini_tablet_servers()) {
    LOG(INFO) << ts->ToString() << " TestWaitingLocksSize: "
              << ts->server()->ts_local_lock_manager()->TEST_WaitingLocksSize()
              << " TestGrantedLocksSize: "
              << ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize();
    if (dump_tserver_html) {
      ts->server()->ts_local_lock_manager()->DumpLocksToHtml(LOG(INFO));
    }
  }
}

TEST_F(ObjectLockTest, DDLLockWaitsAtMaster) {
  const auto& kSessionHostUuid = TSUuid(0);
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  ASSERT_OK(AcquireLockGlobally(&master_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kRelationId));
  auto master_local_lock_manager = cluster_->mini_master()
                                       ->master()
                                       ->catalog_manager_impl()
                                       ->object_lock_info_manager()
                                       ->TEST_ts_local_lock_manager();

  DumpMasterAndTServerLocks(cluster_.get(), "After taking lock from session-1 ");
  auto expected_locks = master_local_lock_manager->TEST_GrantedLocksSize();
  ASSERT_GE(expected_locks, 1);
  ASSERT_EQ(master_local_lock_manager->TEST_WaitingLocksSize(), 0);
  for (auto ts : cluster_->mini_tablet_servers()) {
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), expected_locks);
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);
  }

  auto acquire_future =
      AcquireLockGloballyAsync(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kRelationId);

  // Wait for the lock acquisition to wait at master.
  ASSERT_OK(WaitFor(
      [master_local_lock_manager]() -> bool {
        return master_local_lock_manager->TEST_WaitingLocksSize() > 0;
      },
      kTimeout, "Wait for blocking on the master"));

  DumpMasterAndTServerLocks(cluster_.get(), "After requesting lock from session-2 ");
  for (auto ts : cluster_->mini_tablet_servers()) {
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), expected_locks);
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);
  }

  // Release lock from Session-1
  ASSERT_OK(ReleaseLockGloballyAt(&master_proxy, kSessionHostUuid, kTxn1));
  ASSERT_OK(WaitFor(
      [master_local_lock_manager]() -> bool {
        return master_local_lock_manager->TEST_WaitingLocksSize() == 0;
      },
      60s, "wait for DDL unlock to complete at the master"));

  // Verify that lock acquistion for session-2 is successful.
  EXPECT_OK(ResolveFutureStatus(acquire_future));

  DumpMasterAndTServerLocks(
      cluster_.get(), "After releasing lock from session-1 : session-2 should acquire the lock");
  ASSERT_EQ(expected_locks, master_local_lock_manager->TEST_GrantedLocksSize());
  ASSERT_EQ(master_local_lock_manager->TEST_WaitingLocksSize(), 0);
  for (auto ts : cluster_->mini_tablet_servers()) {
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), expected_locks);
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);
  }

  // Release lock from Session-2
  ASSERT_OK(ReleaseLockGloballyAt(&master_proxy, kSessionHostUuid, kTxn2));
  ASSERT_OK(WaitFor(
      [master_local_lock_manager]() -> bool {
        return master_local_lock_manager->TEST_WaitingLocksSize() == 0 &&
               master_local_lock_manager->TEST_GrantedLocksSize() == 0;
      },
      60s, "wait for DDL locks to clear at the master"));
  DumpMasterAndTServerLocks(cluster_.get(), "After releasing all locks");
  ASSERT_EQ(master_local_lock_manager->TEST_GrantedLocksSize(), 0);
  ASSERT_EQ(master_local_lock_manager->TEST_WaitingLocksSize(), 0);
  for (auto ts : cluster_->mini_tablet_servers()) {
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), 0);
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);
  }
}

TEST_F(ObjectLockTest, DDLLocksCleanupAtMaster) {
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  constexpr uint64_t kNumHosts = 2;
  constexpr uint64_t kNumDDLsPerHost = 3;
  constexpr uint64_t kNumObjectsPerDDL = 3;
  constexpr uint64_t kLocksPerHost = kNumDDLsPerHost * kNumObjectsPerDDL;
  constexpr uint64_t kNumLocksTotal = kLocksPerHost * kNumHosts;

  auto ddl_txns = CreateRandomExclusiveLockOwners(kNumDDLsPerHost * kNumHosts);
  for (uint64_t relation_id = 0; relation_id < kNumLocksTotal; relation_id++) {
    auto host_idx = relation_id / kLocksPerHost;
    auto ddl_idx = (relation_id / kNumObjectsPerDDL) % kNumDDLsPerHost;
    ASSERT_OK(AcquireLockGlobally(
        &master_proxy, TSUuid(host_idx), ddl_txns[ddl_idx * kNumHosts + host_idx], kDatabaseID,
        relation_id));
  }

  // Waiting locks should not be cleaned up yet.
  auto master_local_lock_manager = cluster_->mini_master()
                                       ->master()
                                       ->catalog_manager_impl()
                                       ->object_lock_info_manager()
                                       ->TEST_ts_local_lock_manager();
  DumpMasterAndTServerLocks(cluster_.get(), "After taking locks");
  const uint64_t kEntriesPerRequest = docdb::GetEntriesForLockType(ACCESS_EXCLUSIVE).size();
  auto num_locks = kEntriesPerRequest * kNumHosts * kNumDDLsPerHost * kNumObjectsPerDDL;
  ASSERT_EQ(master_local_lock_manager->TEST_GrantedLocksSize(), num_locks);
  ASSERT_EQ(master_local_lock_manager->TEST_WaitingLocksSize(), 0);
  for (auto ts : cluster_->mini_tablet_servers()) {
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), num_locks);
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);
  }

  // Release all locks taken from host-0, session-0
  ASSERT_OK(ReleaseLockGloballyAt(&master_proxy, TSUuid(0), ddl_txns[0]));

  num_locks = kEntriesPerRequest * (kNumHosts * kNumDDLsPerHost - 1) * kNumObjectsPerDDL;
  ASSERT_OK(WaitFor(
      [master_local_lock_manager, num_locks]() -> bool {
        return master_local_lock_manager->TEST_WaitingLocksSize() == 0 &&
               master_local_lock_manager->TEST_GrantedLocksSize() == num_locks;
      },
      60s, "wait for DDL locks to clear at the master"));
  DumpMasterAndTServerLocks(cluster_.get(), "After Releasing locks from host-0, session-0");
  for (auto ts : cluster_->mini_tablet_servers()) {
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), num_locks);
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);
  }

  // Also, Release all locks taken from host-1
  auto cluster_client = master::MasterClusterClient(
      ASSERT_RESULT(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>()));
  auto tserver_entry = ASSERT_RESULT(cluster_client.GetTabletServer(TSUuid(1)));
  ASSERT_TRUE(tserver_entry) << Format(
      "couldn't find tserver $0 entry in list tservers", TSUuid(1));
  auto ts1_lease_epoch = tserver_entry->lease_info().lease_epoch();

  // todo(zdrudi): we maybe want to remove this.
  auto latch = cluster_->mini_master()
                   ->master()
                   ->catalog_manager_impl()
                   ->object_lock_info_manager()
                   ->ReleaseLocksHeldByExpiredLeaseEpoch(TSUuid(1), ts1_lease_epoch);
  latch->WaitFor(kTimeout);

  DumpMasterAndTServerLocks(
      cluster_.get(), "After Releasing locks from host-0, session-0; and also from host-1");
  num_locks = kEntriesPerRequest * ((kNumHosts - 1) * kNumDDLsPerHost - 1) * kNumObjectsPerDDL;
  ASSERT_EQ(master_local_lock_manager->TEST_GrantedLocksSize(), num_locks);
  ASSERT_EQ(master_local_lock_manager->TEST_WaitingLocksSize(), 0);
  for (auto ts : cluster_->mini_tablet_servers()) {
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), num_locks);
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);
  }
}

TEST_F(ObjectLockTest, AcquireObjectLocksRetriesUponMultipleTServerAddition) {
  const auto& kSessionHostUuid = TSUuid(0);
  auto* tserver0 = cluster_->mini_tablet_server(0);
  auto tserver0_proxy = TServerProxyFor(tserver0);
  ASSERT_OK(AcquireLockAt(
      &tserver0_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kRelationId));

  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  auto ddl_future =
      AcquireLockGloballyAsync(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kRelationId);

  // Wait. But the lock acquisition should not be successful.
  ASSERT_OK(WaitFor(
      [tserver0]() -> bool {
        return tserver0->server()->ts_local_lock_manager()->TEST_WaitingLocksSize() > 0;
      },
      kTimeout, "wait for blocking on TServer0"));

  // Add TS-4.
  auto added_tserver = ASSERT_RESULT(AddTabletServer(kTimeout));

  // TS-4 will be bootstrapping from the master's state, so it should
  // have granted the DDL lock.
  ASSERT_OK(WaitFor(
      [added_tserver]() -> bool {
        return added_tserver->server()->ts_local_lock_manager()->TEST_GrantedLocksSize() > 0;
      },
      kTimeout, "wait for bootstrapping on TServer4"));
  ASSERT_EQ(added_tserver->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);

  auto ts_controller = RpcController();
  auto added_tserver_proxy = TServerProxyFor(added_tserver);
  auto ts_future =
      AcquireLockAsyncAt(&added_tserver_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kRelationId);
  // DML will be blocked by the DDL lock granted on TS-4 during bootstrap.
  ASSERT_OK(WaitFor(
      [added_tserver]() -> bool {
        return added_tserver->server()->ts_local_lock_manager()->TEST_WaitingLocksSize() > 0;
      },
      kTimeout, "wait for blocking on TServer4"));
  ASSERT_GE(added_tserver->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), 1);
  ASSERT_GE(added_tserver->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 1);

  // DDL will be waiting to get the lock on TS-1
  ASSERT_GE(tserver0->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 1);

  ASSERT_OK(ReleaseLockAt(&tserver0_proxy, kSessionHostUuid, kTxn1));
  // Verify that DDL lock acquistion is successful.
  ASSERT_OK(ResolveFutureStatus(ddl_future));

  // Release DDL lock
  ASSERT_OK(ReleaseLockGloballyAt(&master_proxy, kSessionHostUuid, kTxn2));

  // Verify that DML lock acquistion is successful.
  ASSERT_OK(ResolveFutureStatus(ts_future));
  ASSERT_GE(added_tserver->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), 1);
  ASSERT_EQ(added_tserver->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);

  // Release DML lock at TS-4
  ASSERT_OK(ReleaseLockAt(&added_tserver_proxy, kSessionHostUuid, kTxn1));
  ASSERT_EQ(added_tserver->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), 0);
}

TEST_F(ObjectLockTest, BootstrapTServersUponAddition) {
  const auto& kSessionHostUuid = TSUuid(0);
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  ASSERT_OK(AcquireLockGlobally(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kRelationId));

  auto* added_tserver = ASSERT_RESULT(AddTabletServer(kTimeout));
  ASSERT_OK(WaitFor(
      [added_tserver]() {
        return added_tserver->server()->ts_local_lock_manager()->TEST_GrantedLocksSize() > 0;
      },
      1s, "Wait for the added TS to bootstrap"));

  auto expected_locks =
      cluster_->mini_tablet_server(0)->server()->ts_local_lock_manager()->TEST_GrantedLocksSize();
  ASSERT_GE(expected_locks, 1);
  // Expect to see that the lock acquisition happens even at the new tserver
  LOG(INFO) << "Counts after acquiring the DDL lock and adding TServers";
  for (auto ts : cluster_->mini_tablet_servers()) {
    LOG(INFO) << ts->ToString() << " TestWaitingLocksSize: "
              << ts->server()->ts_local_lock_manager()->TEST_WaitingLocksSize()
              << " TestGrantedLocksSize: "
              << ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize();
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), expected_locks);
  }

  ASSERT_OK(ReleaseLockGloballyAt(&master_proxy, kSessionHostUuid, kTxn2));
  auto master_local_lock_manager = cluster_->mini_master()
                                       ->master()
                                       ->catalog_manager_impl()
                                       ->object_lock_info_manager()
                                       ->TEST_ts_local_lock_manager();
  ASSERT_OK(WaitFor(
      [master_local_lock_manager]() -> bool {
        return master_local_lock_manager->TEST_WaitingLocksSize() == 0 &&
               master_local_lock_manager->TEST_GrantedLocksSize() == 0;
      },
      60s, "wait for DDL locks to clear at the master"));

  LOG(INFO) << "Counts after releasing the DDL lock";
  expected_locks = 0;
  for (auto ts : cluster_->mini_tablet_servers()) {
    LOG(INFO) << ts->ToString() << " TestWaitingLocksSize: "
              << ts->server()->ts_local_lock_manager()->TEST_WaitingLocksSize()
              << " TestGrantedLocksSize: "
              << ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize();
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), expected_locks);
  }
}

TEST_F(ObjectLockTest, ReleaseExclusiveLocksWhenTServerLeaseExpires) {
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  // Acquire exclusive lock for tserver0. Should succeed.
  auto uuid_to_take_down = TSUuid(0);
  ASSERT_OK(AcquireLockGlobally(&master_proxy, uuid_to_take_down, kTxn1, kDatabaseID, kRelationId));
  auto kBlockingRequestTimeout = MonoDelta::FromSeconds(10);
  // Acquire exclusive lock for tserver1. This should block.
  auto future = AcquireLockGloballyAsync(
      &master_proxy, TSUuid(1), kTxn2, kDatabaseID, kRelationId, kLeaseEpoch, nullptr, std::nullopt,
      kBlockingRequestTimeout);
  // Wait until the request is on the waiting queue.
  ASSERT_OK(WaitFor(
      [&]() -> bool {
        auto master_local_lock_manager = cluster_->mini_master()
                                             ->master()
                                             ->catalog_manager_impl()
                                             ->object_lock_info_manager()
                                             ->TEST_ts_local_lock_manager();
        return master_local_lock_manager->TEST_WaitingLocksSize() > 0;
      },
      kBlockingRequestTimeout, "Wait for acquire lock request to block on the master"));
  // Now bring down tserver0. Eventually the master leader should notice and release its held locks.
  LOG(INFO) << "Shutting down tablet server " << uuid_to_take_down;
  cluster_->mini_tablet_server(0)->Shutdown();
  ASSERT_OK(ResolveFutureStatus(future));
  // Ensure master cleans up expired lease.
  LOG(INFO) << Format("Waiting for tablet server $0 to lose its lease", uuid_to_take_down);
  ASSERT_OK(WaitForTServerLeaseToExpire(uuid_to_take_down, kBlockingRequestTimeout));
  // Bring tserver0 back up so teardown verification is clean.
  ASSERT_OK(cluster_->mini_tablet_server(0)->Start());
}

TEST_F(ObjectLockTest, TServerLeaseExpiresBeforeExclusiveLockRequest) {
  auto kBlockingRequestTimeout = MonoDelta::FromSeconds(10);
  auto idx_to_take_down = 0;
  auto uuid_to_take_down = TSUuid(idx_to_take_down);
  {
    auto* tserver0 = cluster_->mini_tablet_server(idx_to_take_down);
    auto tserver0_proxy = TServerProxyFor(tserver0);
    ASSERT_OK(AcquireLockAt(
        &tserver0_proxy, uuid_to_take_down, kTxn1, kDatabaseID, kRelationId));
  }
  LOG(INFO) << "Shutting down tablet server " << uuid_to_take_down;
  ASSERT_NOTNULL(cluster_->find_tablet_server(uuid_to_take_down))->Shutdown();
  LOG(INFO) << Format("Waiting for tablet server $0 to lose its lease", uuid_to_take_down);
  ASSERT_OK(WaitForTServerLeaseToExpire(uuid_to_take_down, kBlockingRequestTimeout));
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  ASSERT_OK(AcquireLockGlobally(&master_proxy, TSUuid(1), kTxn2, kDatabaseID, kRelationId));
  ASSERT_OK(cluster_->mini_tablet_server(idx_to_take_down)->Start());
}

TEST_F(ObjectLockTest, TServerHeldExclusiveLocksReleasedAfterRestart) {
  // Bump up the lease lifetime to verify the lease is lost when a new tserver process registers.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_ysql_operation_lease_ttl_ms) = 20 * 1000;
  // The lease should be longer than the request timeout.
  ASSERT_GT(FLAGS_master_ysql_operation_lease_ttl_ms, kTimeout.ToMilliseconds());
  auto ts_to_restart_idx = 0;
  auto ts_to_restart_uuid = TSUuid(ts_to_restart_idx);
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  ASSERT_OK(AcquireLockGlobally(
      &master_proxy, ts_to_restart_uuid, kTxn1, kDatabaseID, kRelationId, kLeaseEpoch, nullptr,
      std::nullopt, kTimeout));
  ASSERT_OK(RestartTabletServer(*cluster_->mini_tablet_server(ts_to_restart_idx), kTimeout));
  // The lock should be released when the new tserver process heartbeats to the master leader.
  ASSERT_OK(AcquireLockGlobally(
      &master_proxy, TSUuid(1), kTxn1, kDatabaseID, kRelationId, kLeaseEpoch, nullptr, std::nullopt,
      kTimeout));
}

TEST_F(ObjectLockTest, TServerCanAcquireLocksAfterRestart) {
  auto ts_idx = 0;
  auto ts_uuid = TSUuid(ts_idx);
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  uint64_t lease_epoch = kLeaseEpoch;
  LOG(INFO) << "Acquiring global lock in term " << lease_epoch;
  ASSERT_OK(AcquireLockGlobally(
      &master_proxy, ts_uuid, kTxn1, kDatabaseID, kRelationId, lease_epoch, nullptr, std::nullopt,
      kTimeout));
  ASSERT_OK(RestartTabletServer(*cluster_->mini_tablet_server(ts_idx), kTimeout));
  // The lease epoch should be incremented when the tserver acquires a new lease.
  lease_epoch++;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        LOG(INFO) << "Trying to Acquire global lock in term " << lease_epoch;
        auto status = AcquireLockGlobally(
            &master_proxy, ts_uuid, kTxn1, kDatabaseID, kRelationId2, lease_epoch, nullptr,
            std::nullopt, kTimeout);
        LOG(INFO) << "Got " << status.ToString();
        if (status.ok()) {
          return true;
        }
        auto expected_status = BuildLeaseEpochMismatchErrorStatus(lease_epoch, kLeaseEpoch);
        if (status.code() == expected_status.code() &&
            SameCodeAndMessage(status, expected_status)) {
          return false;
        }
        return status;
      },
      kTimeout, "Try to acquire exclusive lock after tserver restart"));
  // Using the previous lease epoch should fail.
  LOG(INFO) << "Acquiring global lock using a previous/wrong epoch " << kLeaseEpoch
            << " instead of " << lease_epoch;
  auto status = AcquireLockGlobally(
      &master_proxy, ts_uuid, kTxn1, kDatabaseID, kRelationId3, kLeaseEpoch, nullptr, std::nullopt,
      kTimeout);
  LOG(INFO) << "Got " << status.ToString();
  EXPECT_THAT(status, EqualsStatus(BuildLeaseEpochMismatchErrorStatus(kLeaseEpoch, lease_epoch)));
}

class ExternalObjectLockTestExpiry : public ExternalObjectLockTest,
                                     public ::testing::WithParamInterface<bool> {};

TEST_P(ExternalObjectLockTestExpiry, TServerHeldLocksReleasedAfterExpiry) {
  auto kLeaseTimeoutDeadline = MonoDelta::FromSeconds(20);
  ASSERT_GT(
      kLeaseTimeoutDeadline.ToMilliseconds(),
      std::stoll(ASSERT_RESULT(cluster_->GetFlag(
          ASSERT_NOTNULL(cluster_->GetLeaderMaster()), "master_ysql_operation_lease_ttl_ms"))));
  auto ts = tablet_server(0);
  auto master_proxy = cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>();
  if (GetParam()) {
    LOG(INFO) << "Acquiring locally for txn-1";
    auto tserver_proxy = cluster_->GetTServerProxy<tserver::TabletServerServiceProxy>(0);
    ASSERT_OK(AcquireLockAt(
        &tserver_proxy, ts->uuid(), kTxn1, kDatabaseID, kRelationId, kLeaseEpoch, nullptr,
        std::nullopt, kTimeout));
  } else {
    LOG(INFO) << "Acquiring globally for txn-1";
    ASSERT_OK(AcquireLockGlobally(
        &master_proxy, ts->uuid(), kTxn1, kDatabaseID, kRelationId, kLeaseEpoch, nullptr,
        std::nullopt, kTimeout));
  }
  ASSERT_OK(cluster_->SetFlag(ts, kTServerYsqlLeaseRefreshFlagName, "false"));
  ASSERT_OK(WaitForTServerLeaseToExpire(ts->uuid(), kLeaseTimeoutDeadline));
  ASSERT_OK(cluster_->SetFlag(ts, kTServerYsqlLeaseRefreshFlagName, "true"));
  // Wait for the TServer to get a new lease.
  ASSERT_OK(WaitForTServerLease(ts->uuid(), kTimeout));
  // We expect that the locks held locally at the TServer are released when the lease expires.
  LOG(INFO) << "Acquiring globally for txn-2";
  auto other_ts = tablet_server(1);
  ASSERT_OK(AcquireLockGlobally(
      &master_proxy, other_ts->uuid(), kTxn2, kDatabaseID, kRelationId, kLeaseEpoch, nullptr,
      std::nullopt, kTimeout));
}

INSTANTIATE_TEST_CASE_P(DmlOrDdlLocks, ExternalObjectLockTestExpiry, ::testing::Bool());

TEST_F(ExternalObjectLockTest, TServerCanAcquireLocksAfterLeaseExpiry) {
  auto kLeaseTimeoutDeadline = MonoDelta::FromSeconds(20);
  ASSERT_GT(
      kLeaseTimeoutDeadline.ToMilliseconds(),
      std::stoll(ASSERT_RESULT(cluster_->GetFlag(
          ASSERT_NOTNULL(cluster_->GetLeaderMaster()), "master_ysql_operation_lease_ttl_ms"))));
  uint64_t lease_epoch = kLeaseEpoch;
  auto ts = tablet_server(0);
  auto master_proxy = cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>();
  ASSERT_OK(AcquireLockGlobally(
      &master_proxy, ts->uuid(), kTxn1, kDatabaseID, kRelationId, lease_epoch, nullptr,
      std::nullopt, kTimeout));
  ASSERT_OK(cluster_->SetFlag(ts, kTServerYsqlLeaseRefreshFlagName, "false"));
  ASSERT_OK(WaitForTServerLeaseToExpire(ts->uuid(), kLeaseTimeoutDeadline));
  ASSERT_OK(cluster_->SetFlag(ts, kTServerYsqlLeaseRefreshFlagName, "true"));
  // The lease epoch should be incremented when the tserver acquires a new lease.
  ++lease_epoch;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        // Acquire a lock on a different object with the new lease.
        auto status = AcquireLockGlobally(
            &master_proxy, ts->uuid(), kTxn1, kDatabaseID, kRelationId2, lease_epoch, nullptr,
            std::nullopt, kTimeout);
        if (status.ok()) {
          return true;
        }
        auto s = status.message().ToBuffer();
        if (status.IsInvalidArgument() &&
            s.find("but the latest valid lease epoch for this tserver is") != std::string::npos) {
          return false;
        }
        return status;
      },
      kTimeout, "Try to acquire exclusive lock after tserver restart"));
  // Using the previous lease epoch should fail.
  auto status = AcquireLockGlobally(
      &master_proxy, ts->uuid(), kTxn1, kDatabaseID, kRelationId3, kLeaseEpoch, nullptr,
      std::nullopt, kTimeout);
  EXPECT_THAT(status, EqualsStatus(BuildLeaseEpochMismatchErrorStatus(kLeaseEpoch, lease_epoch)));
}

TEST_F(ExternalObjectLockTest, RefreshYsqlLease) {
  auto ts = tablet_server(0);
  auto master_proxy = cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>();

  // Acquire a lock on behalf of another ts.
  ASSERT_OK(AcquireLockGlobally(
      &master_proxy, tablet_server(1)->uuid(), kTxn1, kDatabaseID, kRelationId, kLeaseEpoch,
      nullptr, std::nullopt, kTimeout));

  master::MasterDDLClient ddl_client{cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>()};

  auto lease_refresh_time_ms = MonoTime::Now().GetDeltaSinceMin().ToMilliseconds();

  // Request a lease refresh on behalf of ts with the correct lease epoch in the request.
  // Expect the master to omit most information and set new_lease to false.
  auto info = ASSERT_RESULT(ddl_client.RefreshYsqlLease(
      ts->uuid(), ts->instance_id().instance_seqno(), lease_refresh_time_ms, kLeaseEpoch));
  ASSERT_FALSE(info.new_lease());
  ASSERT_FALSE(info.has_ddl_lock_entries());

  // Request a lease refresh on behalf of ts with no lease epoch in the request.
  // Master should give us a new lease epoch, the acquired lock entries, and
  // new_lease.
  info = ASSERT_RESULT(ddl_client.RefreshYsqlLease(
      ts->uuid(), ts->instance_id().instance_seqno(),
      lease_refresh_time_ms, {}));
  ASSERT_TRUE(info.new_lease());
  ASSERT_EQ(info.lease_epoch(), kLeaseEpoch + 1);
  ASSERT_TRUE(info.has_ddl_lock_entries());
  ASSERT_GE(info.ddl_lock_entries().lock_entries_size(), 1);

  // Request a lease refresh on behalf of ts with the incorrect lease epoch in the request.
  // Master should give us a new lease epoch, the acquired lock entries, and set new_lease.
  info = ASSERT_RESULT(ddl_client.RefreshYsqlLease(
      ts->uuid(), ts->instance_id().instance_seqno(), lease_refresh_time_ms, 0));
  ASSERT_TRUE(info.new_lease());
  ASSERT_EQ(info.lease_epoch(), kLeaseEpoch + 2);
  ASSERT_TRUE(info.has_ddl_lock_entries());
  ASSERT_GE(info.ddl_lock_entries().lock_entries_size(), 1);

}

TEST_F(ExternalObjectLockTest, TServerCrashRestartAndDoesNotReacquireLease) {
  auto ts = tablet_server(0);
  // We do an ungraceful shutdown here so the tserver does not relinquish its lease.
  ts->Shutdown(SafeShutdown::kFalse);
  ASSERT_OK(ts->Restart(true, {{kTServerYsqlLeaseRefreshFlagName, "false"}}));
  auto master_proxy = cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>();
  // This should succeed once the tserver's lease expires.
  ASSERT_OK(AcquireLockGlobally(
      &master_proxy, tablet_server(1)->uuid(), kTxn1, kDatabaseID, kRelationId, kLeaseEpoch,
      nullptr, std::nullopt, kTimeout * 2));
}

class ExternalObjectLockTestLongLeaseTTL : public ExternalObjectLockTest {
  ClusterFlags FlagOverrides() override;
};

TEST_F(ExternalObjectLockTestLongLeaseTTL, TServerCrashRestartAndReacquiresLease) {
  auto ts = tablet_server(0);
  // We do an ungraceful shutdown here so the tserver does not relinquish its lease.
  ts->Shutdown(SafeShutdown::kFalse);
  ASSERT_OK(ts->Restart(true, {{kTServerYsqlLeaseRefreshFlagName, "false"}}));
  auto master_proxy = cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>();
  auto future = AcquireLockGloballyAsync(
      &master_proxy, tablet_server(1)->uuid(), kTxn1, kDatabaseID, kRelationId, kLeaseEpoch,
      nullptr, std::nullopt, kTimeout * 2);
  // Sleep for a bit to allow the tserver to handle some RPCs. We do not expect the future to
  // resolve yet.
  auto first_future_status = future.wait_for(1s);
  EXPECT_EQ(first_future_status, std::future_status::timeout);
  ASSERT_OK(cluster_->SetFlag(ts, kTServerYsqlLeaseRefreshFlagName, "true"));
  auto second_future_status = future.wait_for(10s);
  EXPECT_EQ(second_future_status, std::future_status::ready);
  ASSERT_OK(future.get());
}

class ExternalObjectLockTestLongLeaseTTLOneTS : public ExternalObjectLockTestLongLeaseTTL {
  int ReplicationFactor() override;
  size_t NumberOfTabletServers() override;
};

TEST_F(ExternalObjectLockTestLongLeaseTTLOneTS, SigTerm) {
  auto kLeaseTimeoutDeadline = MonoDelta::FromSeconds(10);
  ASSERT_LT(
      kLeaseTimeoutDeadline.ToMilliseconds(),
      std::stoll(ASSERT_RESULT(cluster_->GetFlag(
          ASSERT_NOTNULL(cluster_->GetLeaderMaster()), "master_ysql_operation_lease_ttl_ms"))));
  auto ts = tablet_server(0);
  auto ts_uuid = ts->uuid();
  ts->Shutdown(SafeShutdown::kTrue);
  // TServer should lose its lease before the lease TTL.
  // Use EXPECT here so we restart the TS in case of failure.
  EXPECT_OK(WaitForTServerLeaseToExpire(ts_uuid, kLeaseTimeoutDeadline));
  ASSERT_OK(ts->Restart());
  // TServer should be able to re-acquire the lease after it restarts.
  ASSERT_OK(WaitForTServerLease(ts_uuid, 10s));
}

class MultiMasterObjectLockTest : public ObjectLockTest {
 protected:
  int num_masters() override { return 3; }

  void SetUp() override {
    ObjectLockTest::SetUp();
  }
};

TEST_F_EX(
    ObjectLockTest, AcquireObjectLocksWaitsOnTServerAcrossMasterFailover,
    MultiMasterObjectLockTest) {
  testAcquireObjectLockWaitsOnTServer(true);
}

TEST_F_EX(ObjectLockTest, AcquireAndReleaseDDLLockAcrossMasterFailover, MultiMasterObjectLockTest) {
  const auto& kSessionHostUuid = TSUuid(0);
  const auto num_ts = cluster_->num_tablet_servers();
  auto* leader_master1 = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  {
    LOG(INFO) << "Acquiring lock on relation " << kRelationId << " from master "
              << leader_master1->ToString();
    auto master_proxy = MasterProxy(leader_master1);
    ASSERT_OK(AcquireLockGlobally(
        &master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kRelationId));
  }

  auto master_local_lock_manager1 = leader_master1->master()
                                        ->catalog_manager_impl()
                                        ->object_lock_info_manager()
                                        ->TEST_ts_local_lock_manager();
  ASSERT_GE(master_local_lock_manager1->TEST_GrantedLocksSize(), 1);
  for (const auto& tserver : cluster_->mini_tablet_servers()) {
    LOG(INFO) << tserver->ToString() << " GrantedLocks "
              << tserver->server()->ts_local_lock_manager()->TEST_GrantedLocksSize();
    ASSERT_GE(tserver->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), 1);
  }

  DumpMasterAndTServerLocks(cluster_.get(), "Before step down");
  LOG(INFO) << "Stepping down from " << leader_master1->ToString();
  ASSERT_OK(cluster_->StepDownMasterLeader());
  ASSERT_OK(cluster_->WaitForTabletServerCount(num_ts));
  auto* leader_master2 = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto master_local_lock_manager2 = leader_master2->master()
                                        ->catalog_manager_impl()
                                        ->object_lock_info_manager()
                                        ->TEST_ts_local_lock_manager();
  ASSERT_GE(master_local_lock_manager2->TEST_GrantedLocksSize(), 1);
  DumpMasterAndTServerLocks(cluster_.get(), "After step down");

  auto* added_tserver = ASSERT_RESULT(AddTabletServer(kTimeout));
  ASSERT_OK(WaitFor(
      [added_tserver]() {
        return added_tserver->server()->ts_local_lock_manager()->TEST_GrantedLocksSize() > 0;
      },
      1s, "Wait for the added TS to bootstrap"));
  LOG(INFO) << added_tserver->ToString() << " GrantedLocks "
            << added_tserver->server()->ts_local_lock_manager()->TEST_GrantedLocksSize();
  ASSERT_GE(added_tserver->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), 1);

  // Release lock
  {
    LOG(INFO) << "Releasing lock on relation " << kRelationId << " at master "
              << leader_master2->ToString();
    auto master_proxy = MasterProxy(leader_master2);
    ASSERT_OK(ReleaseLockGloballyAt(&master_proxy, kSessionHostUuid, kTxn2));
    ASSERT_OK(WaitFor(
        [master_local_lock_manager2]() -> bool {
          return master_local_lock_manager2->TEST_WaitingLocksSize() == 0 &&
                 master_local_lock_manager2->TEST_GrantedLocksSize() == 0;
        },
        60s, "wait for DDL locks to clear at the master"));
  }
}

TEST_F(MultiMasterObjectLockTest, TServerCanAcquireLeaseAfterProlongedPartition) {
  size_t idx = 0;
  // Get the leader master to be located with the tserver which we are going to isolate.
  ASSERT_RESULT(cluster_->StepDownMasterLeader(MasterUuid(idx)));
  CHECK_EQ(idx, cluster_->LeaderMasterIdx());

  auto kLeaseTimeout = MonoDelta::FromMilliseconds(FLAGS_master_ysql_operation_lease_ttl_ms);
  ASSERT_OK(
      WaitForTabletServersToAcquireYSQLLeases(cluster_->mini_tablet_servers(), kLeaseTimeout));
  LOG(INFO) << "Breaking connectivity to all other tservers from tserver/master" << idx;
  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    if (i == idx) {
      continue;
    }
    ASSERT_OK(BreakConnectivity(cluster_.get(), idx, i));
  }

  // This condition is require to recreate the issue in GH #27479
  CHECK_GE(
      kLeaseTimeout * 2, MonoDelta::FromMilliseconds(FLAGS_ysql_lease_refresher_interval_ms) *
                             FLAGS_heartbeat_max_failures_before_backoff);

  SleepFor(kLeaseTimeout * 2);
  CHECK_NE(idx, cluster_->LeaderMasterIdx());

  auto ts_uuid = TSUuid(idx);
  LOG(INFO) << Format("Waiting for tablet server $0 to lose its lease", ts_uuid);
  ASSERT_OK(WaitForTServerLeaseToExpire(ts_uuid, kLeaseTimeout));
  LOG(INFO) << Format("tablet server $0 has lost its lease", ts_uuid);

  LOG(INFO) << "Re-establishing connectivity to all other tservers from tserver/master" << idx;
  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    if (i == idx) {
      continue;
    }
    ASSERT_OK(SetupConnectivity(cluster_.get(), idx, i, Connectivity::kOn));
  }

  LOG(INFO) << "Waiting for lease to be established again";
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        auto lease_info = VERIFY_RESULT(GetTServerLeaseInfo(*cluster_, ts_uuid));
        return lease_info.is_live();
      },
      MonoDelta::FromSeconds(60), "Wait for tserver lease to be live again"));
  LOG(INFO) << "Lease established again";
}

TEST_F(ExternalObjectLockTestOneTS, TabletServerKillsSessionsWhenItAcquiresNewLease) {
  constexpr size_t kTSIdx = 0;
  MonoDelta timeout = MonoDelta::FromSeconds(10);
  auto ts_uuid = tablet_server(kTSIdx)->uuid();
  constexpr std::string_view kTableName = "test_table";
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB("yugabyte", kTSIdx));
  ASSERT_OK(conn.Execute(Format("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTableName)));
  // Disable the tserver's lease expiry check task so acquiring a new lease is what prompts the
  // tserver to kill its pg sessions, not the tserver itself deciding its lease has expired.
  ASSERT_OK(cluster_->SetFlag(
      tablet_server(kTSIdx), "TEST_enable_ysql_operation_lease_expiry_check", "false"));
  ASSERT_OK(cluster_->SetFlag(tablet_server(kTSIdx), kTServerYsqlLeaseRefreshFlagName, "false"));
  auto cluster_client =
      master::MasterClusterClient(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>());
  ASSERT_OK(WaitForTServerLeaseToExpire(ts_uuid, kTimeout));
  ASSERT_OK(
      cluster_->SetFlag(tablet_server(kTSIdx), kTServerYsqlLeaseRefreshFlagName, "true"));
  ASSERT_OK(WaitFor(
      [&conn, kTableName]() -> Result<bool> {
        auto result = conn.FetchRow<int64_t>(Format("SELECT count(*) from $0", kTableName));
        if (result.ok()) {
          return false;
        }
        if (PGSessionKilledStatus(result.status())) {
          return true;
        }
        return result.status();
      },
      timeout, "Wait for pg session to be killed"));
  // Once re-acquiring the lease, the tserver should accept new sessions again.
  ASSERT_OK(WaitFor(
      [this, kTSIdx]() -> Result<bool> {
        auto result = cluster_->ConnectToDB("yugabyte", kTSIdx);
        return result.ok();
      },
      timeout, "Wait for tserver to accept new pg sessions"));
}

TEST_F(ExternalObjectLockTestOneTS, TabletServerKillsSessionsWhenItsLeaseExpires) {
  constexpr size_t kTSIdx = 0;
  MonoDelta timeout = MonoDelta::FromSeconds(10);
  auto ts_uuid = tablet_server(kTSIdx)->uuid();
  constexpr std::string_view kTableName = "test_table";
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB("yugabyte", kTSIdx));
  ASSERT_OK(conn.Execute(Format("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTableName)));
  ASSERT_OK(cluster_->SetFlag(tablet_server(kTSIdx), kTServerYsqlLeaseRefreshFlagName, "false"));
  auto cluster_client =
      master::MasterClusterClient(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>());
  ASSERT_OK(WaitFor(
      [&conn, kTableName]() -> Result<bool> {
        auto result = conn.FetchRow<int64_t>(Format("SELECT count(*) from $0", kTableName));
        if (result.ok()) {
          return false;
        }
        if (PGSessionKilledStatus(result.status())) {
          return true;
        }
        return result.status();
      },
      timeout, "Wait for pg session to be killed"));
  // At this point we shouldn't be able to start a new session.
  ExternalClusterPGConnectionOptions conn_options;
  conn_options.timeout_secs = 2;
  auto conn_result = cluster_->ConnectToDB(std::move(conn_options));
  ASSERT_FALSE(conn_result.ok());
  ASSERT_OK(
      cluster_->SetFlag(tablet_server(kTSIdx), kTServerYsqlLeaseRefreshFlagName, "true"));
  // Once re-acquiring the lease, the tserver should accept new sessions again.
  ASSERT_OK(WaitFor(
      [this, kTSIdx]() -> Result<bool> {
        auto result = cluster_->ConnectToDB("yugabyte", kTSIdx);
        return result.ok();
      },
      timeout, "Wait for tserver to accept new pg sessions"));
}

class ExternalObjectLockTestOneTSWithoutLease : public ExternalObjectLockTestOneTS {
 public:
  ClusterFlags FlagOverrides() override;
  bool WaitForTServersToAcceptYSQL() override { return false; }
};

TEST_F(ExternalObjectLockTestOneTSWithoutLease, TServerRefusesPGSessionsWithoutLeaseOnBoot) {
  constexpr size_t kTSIdx = 0;
  constexpr size_t kTimeoutSeconds = 10;
  MonoDelta timeout = MonoDelta::FromSeconds(kTimeoutSeconds);
  // We disabled the tserver's lease refresh on boot. It shouldn't accept pg sessions.
  Status status;
  ExternalClusterPGConnectionOptions connection_opts;
  connection_opts.tserver_index = kTSIdx;
  connection_opts.timeout_secs = kTimeoutSeconds;
  auto conn_result = cluster_->ConnectToDB(std::move(connection_opts));
  ASSERT_NOK(conn_result);
  // Re-enable the lease refresher to sanity check we can connect to the tserver.
  ASSERT_OK(cluster_->SetFlag(tablet_server(kTSIdx), kTServerYsqlLeaseRefreshFlagName, "true"));
  ASSERT_OK(WaitFor(
      [this, kTSIdx, kTimeoutSeconds]() -> Result<bool> {
        ExternalClusterPGConnectionOptions options;
        options.timeout_secs = kTimeoutSeconds;
        options.tserver_index = kTSIdx;
        auto result = cluster_->ConnectToDB(std::move(options));
        return result.ok();
      },
      timeout, "Wait for tserver to accept new pg sessions"));
}

TEST_F(ExternalObjectLockTestOneTS, ReleaseBlocksUntilBootstrap) {
  //   1. acquire a lock globally.
  //   2. start up a tserver that won't acquire a ysql lease.
  //   3. call release on this new tserver. we expect this to block.
  //   4. start the new tserver's ysql lease poller.
  //   5. the new tserver should acquire a lease and process the release.
  //   6. verify by acquiring a local lock on the same object.
  auto master_proxy = cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>();
  auto ts = cluster_->tablet_server(0);
  ASSERT_OK(AcquireLockGlobally(
      &master_proxy, ts->uuid(), kTxn1, kDatabaseID, kRelationId, kLeaseEpoch, nullptr,
      std::nullopt, kTimeout));
  ASSERT_OK(cluster_->AddTabletServer(
      false,
      {"--TEST_tserver_enable_ysql_lease_refresh=false", "--vmodule=ts_local_lock_manager=1"}, -1,
      false));

  auto added_ts = cluster_->tablet_server(1);
  auto added_ts_proxy = cluster_->GetTServerProxy<tserver::TabletServerServiceProxy>(1);
  auto future = ReleaseLockAtAsync(
      &added_ts_proxy, ts->uuid(), kTxn1, kLeaseEpoch, /* clock */ nullptr,
      /* apply_after */ std::nullopt, MonoDelta::FromSeconds(10));
  ASSERT_OK(LogWaiter(added_ts, "Waiting until object lock manager is bootstrapped")
                .WaitFor(MonoDelta::FromSeconds(2)));
  ASSERT_EQ(future.wait_for(0s), std::future_status::timeout)
      << "Expected release request to block at tserver that has not acquired a lease yet";

  // enable the ysql lease poller on the new ts.
  ASSERT_OK(cluster_->SetFlag(added_ts, kTServerYsqlLeaseRefreshFlagName, "true"));
  ASSERT_RESULT(future.get());

  // The lock should be released at the tserver, and we should be able to acquire a conflicting
  // lock.
  ASSERT_OK(AcquireLockAt(
      &added_ts_proxy, added_ts->uuid(), kTxn2, kDatabaseID, kRelationId, kLeaseEpoch, nullptr,
      std::nullopt, kTimeout));
}

YB_DEFINE_ENUM(DisableLeaseMode, (DisablePoller)(DisablePollerAndChecker));
class ExternalObjectLockTestLeaseLost : public ExternalObjectLockTest,
                                        public ::testing::WithParamInterface<DisableLeaseMode> {
 public:
  ClusterFlags FlagOverrides() override;
  uint64_t ysql_lease_ttl_ms();
  Status ExpectedFailureStatusWithoutLease();
  Status DisableLease(ExternalTabletServer* ts);
  Status EnableLease(ExternalTabletServer* ts);
  Status ToggleLeaseFlags(ExternalTabletServer* ts, bool new_value);
};

// This test simulates a lease re-acquisition / lock acquire race.
// We expect the tserver will reject a lock acquisition request sent by the master after the master
// grants it a new lease, but before the tserver has received the new lease.
// Steps:
//   1. disable the lease poller on the tserver.
//   2. refresh the tserver's lease manually, from the test process
//   3. try to acquire a global lock. this should fail.
//   4. enable the lease poller on the tserver.
//   5. try to acquire a global lock. this should succeed.
TEST_P(ExternalObjectLockTestLeaseLost, TSRejectsLockRequestsAfterLeaseExpiry) {
  auto ts_to_lose_lease = cluster_->tablet_server(0);
  ASSERT_OK(DisableLease(ts_to_lose_lease));
  ASSERT_OK(WaitForTServerLeaseToExpire(
      ts_to_lose_lease->uuid(), MonoDelta::FromMilliseconds(ysql_lease_ttl_ms() * 3)));
  master::MasterDDLClient ddl_client{cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>()};
  auto lease_refresh_info = ASSERT_RESULT(ddl_client.RefreshYsqlLease(
      ts_to_lose_lease->uuid(), ts_to_lose_lease->instance_id().instance_seqno(),
      MonoTime::Now().GetDeltaSinceMin().ToMilliseconds(), std::nullopt));
  auto master_proxy = cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>();
  auto other_ts = cluster_->tablet_server(1);
  ASSERT_THAT(
      AcquireLockGlobally(
          &master_proxy, other_ts->uuid(), kTxn1, kDatabaseID, kRelationId, kLeaseEpoch, nullptr,
          std::nullopt, 1s),
      EqualsStatus(STATUS(TimedOut, "Request timed out")));
  auto ts_proxy = ts_to_lose_lease->Proxy<tserver::TabletServerServiceProxy>();
  // It is difficult to verify release requests fail using the master because of the master's retry
  // logic. Instead we issue a request directly to the tserver.
  {
    tserver::ReleaseObjectLockRequestPB req;
    tserver::ReleaseObjectLockResponsePB resp;
    req.set_session_host_uuid(other_ts->uuid());
    kTxn1.PopulateLockRequest(&req);
    req.set_lease_epoch(1);
    req.set_recipient_lease_epoch(2);
    rpc::RpcController controller;
    controller.set_timeout(5s);
    auto s = ts_proxy->ReleaseObjectLocks(req, &resp, &controller);
    if (s.ok()) {
      s = ResponseStatus(resp);
    }
    ASSERT_THAT(s, EqualsStatus(ExpectedFailureStatusWithoutLease()));
  }
  ASSERT_OK(EnableLease(ts_to_lose_lease));
  ASSERT_OK(WaitFor([ts_to_lose_lease]() -> Result<bool> {
      auto ts_proxy = ts_to_lose_lease->Proxy<tserver::TabletServerServiceProxy>();
      tserver::GetYSQLLeaseInfoRequestPB req;
      tserver::GetYSQLLeaseInfoResponsePB resp;
      auto rpc = RpcController();
      RETURN_NOT_OK(ts_proxy->GetYSQLLeaseInfo(req, &resp, &rpc));
      RETURN_NOT_OK(ResponseStatus(resp));
      return resp.is_live() && resp.lease_epoch() > 1;
      }, 3s, "TServer failed to reacquire lease"));
  ASSERT_OK(AcquireLockGlobally(
      &master_proxy, other_ts->uuid(), kTxn1, kDatabaseID, kRelationId));
}

INSTANTIATE_TEST_CASE_P(
    TestLeaseReacquisitionRace,
    ExternalObjectLockTestLeaseLost,
    ::testing::Values(DisableLeaseMode::DisablePoller, DisableLeaseMode::DisablePollerAndChecker));

class MultiMasterObjectLockTestWithFailover : public MultiMasterObjectLockTest,
                                              public ::testing::WithParamInterface<bool> {
 public:
  Status FailoverLeaderMaster() {
    auto graceful_stepdown = GetParam();
    auto old_master_id = VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->ToString();
    LOG(INFO) << "Doing master_failover. Old master was " << old_master_id;
    if (graceful_stepdown) {
      RETURN_NOT_OK(cluster_->StepDownMasterLeader());
    } else {
      auto old_master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
      old_master->Shutdown();
    }
    RETURN_NOT_OK(LoggedWaitFor(
        [old_master_id, this]() -> Result<bool> {
          auto new_master_id = VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->ToString();
          LOG(INFO) << "Current master UUID: " << new_master_id;
          return old_master_id != new_master_id;
        },
        kTimeout, "wait for new master leader"));
    return Status::OK();
  }
};

TEST_P(MultiMasterObjectLockTestWithFailover, AcquireReleaseDdlLocksThroughYBClient) {
  ASSERT_OK(EnsureClientCreated());
  const auto& kSessionHostUuid = TSUuid(0);
  // Acquire lock on TServer-0
  auto* tserver0 = cluster_->mini_tablet_server(0);
  auto tserver0_proxy = TServerProxy(0);
  LOG(INFO) << "Taking DML lock on TServer-0, uuid: " << kSessionHostUuid;
  ASSERT_OK(AcquireLockAt(&tserver0_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kRelationId));
  auto tserver0_local_lock_manager = tserver0->server()->ts_local_lock_manager();
  ASSERT_GT(tserver0_local_lock_manager->TEST_GrantedLocksSize(), 0);
  ASSERT_EQ(tserver0_local_lock_manager->TEST_WaitingLocksSize(), 0);

  auto ddl_future = AcquireLockGloballyAsync(
      client_.get(), kSessionHostUuid, kTxn2, kDatabaseID, kRelationId, kLeaseEpoch, std::nullopt,
      kTimeout * 5 * kTimeMultiplier);

  ASSERT_OK(WaitFor(
      [tserver0_local_lock_manager]() -> bool {
        return tserver0_local_lock_manager->TEST_WaitingLocksSize() > 0;
      },
      kTimeout * kTimeMultiplier, "wait for request to block on tserver0"));
  auto master_local_lock_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())
                                       ->master()
                                       ->catalog_manager_impl()
                                       ->object_lock_info_manager()
                                       ->TEST_ts_local_lock_manager();
  ASSERT_GT(master_local_lock_manager->TEST_GrantedLocksSize(), 0);
  ASSERT_EQ(master_local_lock_manager->TEST_WaitingLocksSize(), 0);

  ASSERT_OK(FailoverLeaderMaster());

  ASSERT_OK(ReleaseLockAt(&tserver0_proxy, kSessionHostUuid, kTxn1));
  ASSERT_OK(ResolveFutureStatus(ddl_future));
  ASSERT_OK(ReleaseLockGlobally(client_.get(), kSessionHostUuid, kTxn2, kLeaseEpoch));
}

INSTANTIATE_TEST_CASE_P(
    StepdownAndShutdown, MultiMasterObjectLockTestWithFailover, ::testing::Bool());

Status ObjectLockTest::WaitForTServerLeaseToExpire(const std::string& uuid, MonoDelta timeout) {
  auto cluster_client = master::MasterClusterClient(
      VERIFY_RESULT(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>()));
  return WaitFor(
      [&]() -> Result<bool> {
        auto lease_info = VERIFY_RESULT(GetTServerLeaseInfo(cluster_client, uuid));
        return !lease_info.is_live();
      },
      timeout, Format("Timed out waiting for master to clear expired lease on tserver $0", uuid));
}

YB_DEFINE_ENUM(ReleaseOptions, (ReleaseInTest)(ReleaseByMaster)(RestartTServer));

template <typename T>
std::string TestParamToString(const testing::TestParamInfo<T>& param_info) {
  return ToString(param_info.param);
}

class MultiMasterObjectLockTestOutOfOrder : public MultiMasterObjectLockTest,
                                            public ::testing::WithParamInterface<ReleaseOptions> {};

TEST_P(MultiMasterObjectLockTestOutOfOrder, IgnoreDDLAcquireAfterRelease) {
  // 1) Txn1 holds DML at dmlHost
  // 2) Txn2 requests DDL Acquire. Cannot succeed at dmlHost
  // 3) Txn2 releases the DDL.
  //    three possible variations:
  //      a) test explicitly calls release.
  //      b) master deems ddlHost to have failed and releases all locks.
  //      c) [TODO] ddlHost restarts and master releases all locks as a consequence.
  // 4) Txn1 then releases DML lock at dmlHost
  // 5) Ensure that dmlHost is not holding onto any locks.

  // Acquire lock on dest TServer
  const auto& kDmlHostUuid = TSUuid(0);
  auto* dml_host_tserver = cluster_->mini_tablet_server(0);
  auto dml_host_ts_proxy = TServerProxy(0);
  LOG(INFO) << "Taking DML lock on uuid: " << kDmlHostUuid;
  ASSERT_OK(AcquireLockAt(&dml_host_ts_proxy, kDmlHostUuid, kTxn1, kDatabaseID, kRelationId));
  ASSERT_EQ(dml_host_tserver->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);

  server::ClockPtr hybrid_clock(new server::HybridClock());
  ASSERT_OK(hybrid_clock->Init());
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  LOG(INFO) << "Requesting DDL lock at master : "
            << ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->ToString();

  // Give this a large deadline.
  auto kDdlHostUuid = TSUuid(1);  // Don't use a reference. We may restart this TServer.
  auto* ddl_host_tserver = cluster_->mini_tablet_server(1);
  auto ddl_timeout = kTimeout * 5;
  HybridTime ddl_deadline = hybrid_clock->Now().AddMicroseconds(ddl_timeout.ToMicroseconds());
  auto ddl_future = AcquireLockGloballyAsync(
      &master_proxy, kDdlHostUuid, kTxn2, kDatabaseID, kRelationId, kLeaseEpoch, hybrid_clock,
      ddl_deadline, ddl_timeout);
  ASSERT_OK(WaitFor(
      [dml_host_tserver]() -> bool {
        return dml_host_tserver->server()->ts_local_lock_manager()->TEST_WaitingLocksSize() > 0;
      },
      ddl_timeout, "DDL acquire should wait on dest tserver"));

  auto master_catalog_manager_impl =
      ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->master()->catalog_manager_impl();
  auto master_local_lock_manager =
      master_catalog_manager_impl->object_lock_info_manager()->TEST_ts_local_lock_manager();

  DumpMasterAndTServerLocks(cluster_.get(), "Before releasing DDL.");
  auto release_option = GetParam();
  LOG(INFO) << "Releasing all DDL locks that were taken by TServer " << kDdlHostUuid;
  switch(release_option) {
    case ReleaseOptions::ReleaseInTest: {
      // Release DDL locks explicitly.
      LOG(INFO) << "Releasing DDL locks that were attempted to be taken";
      auto apply_after = ddl_deadline;
      auto release_ddl_timeout = ddl_timeout + kTimeout;
      ASSERT_OK(ReleaseLockGloballyAt(
          &master_proxy, kDdlHostUuid, kTxn2, kLeaseEpoch, hybrid_clock, apply_after,
          release_ddl_timeout));
      ASSERT_OK(WaitFor(
          [master_local_lock_manager]() -> bool {
            return master_local_lock_manager->TEST_WaitingLocksSize() == 0 &&
                   master_local_lock_manager->TEST_GrantedLocksSize() == 0;
          },
          60s, "wait for DDL locks to clear at the master"));
      break;
    }
    case ReleaseOptions::ReleaseByMaster: {
      auto ddl_src_tserver_lease_epoch =
          ASSERT_RESULT(GetTServerLeaseInfo(*cluster_, kDdlHostUuid)).lease_epoch();
      auto latch =
          master_catalog_manager_impl->object_lock_info_manager()
              ->ReleaseLocksHeldByExpiredLeaseEpoch(kDdlHostUuid, ddl_src_tserver_lease_epoch);
      auto deadline = MonoTime::Now() + 60s;
      latch->WaitUntil(deadline);
      ASSERT_OK(Wait(
          [master_local_lock_manager]() -> bool {
            return master_local_lock_manager->TEST_WaitingLocksSize() == 0 &&
                  master_local_lock_manager->TEST_GrantedLocksSize() == 0;
          },
          deadline, "wait for DDL locks to clear at the master"));
      break;
    }
    case ReleaseOptions::RestartTServer: {
      ASSERT_OK(ddl_host_tserver->Restart());
      ASSERT_OK(WaitFor(
          [master_local_lock_manager]() -> bool {
            return master_local_lock_manager->TEST_WaitingLocksSize() == 0 &&
                  master_local_lock_manager->TEST_GrantedLocksSize() == 0;
          },
          60s, "wait for DDL locks to clear at the master"));
      break;
    }
    default:
      FATAL_INVALID_ENUM_VALUE(ReleaseOptions, release_option);
  }

  DumpMasterAndTServerLocks(cluster_.get(), "After releasing DDL.");
  // The DML lock is held at dml_host_tserver, and DDL can possibly be waiting
  // in the case where the session host DDL tserver is restarted and
  // `apply_after` is not used. There should be no locks elsewhere.
  ASSERT_EQ(master_local_lock_manager->TEST_GrantedLocksSize(), 0);
  ASSERT_EQ(master_local_lock_manager->TEST_WaitingLocksSize(), 0);
  for (auto ts : cluster_->mini_tablet_servers()) {
    if (ts.get() != dml_host_tserver) {
      ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), 0);
      ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);
    }
  }

  LOG(INFO) << "Releasing DML lock at " << kDmlHostUuid;
  ASSERT_OK(ReleaseLockAt(&dml_host_ts_proxy, kDmlHostUuid, kTxn1));
  // This may be a Timeout, or an InvalidArgument if the TServer's lease expired.
  auto ddl_acquire_status = ResolveFutureStatus(ddl_future);
  LOG(INFO) << "DDL lock acquisition status: " << ddl_acquire_status;
  ASSERT_NOK(ddl_acquire_status);
  DumpMasterAndTServerLocks(cluster_.get(), "After releasing DML.");
  ASSERT_EQ(dml_host_tserver->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), 0);
  ASSERT_EQ(dml_host_tserver->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);
}

INSTANTIATE_TEST_CASE_P(
    ExplicitlyReleaseLocks, MultiMasterObjectLockTestOutOfOrder,
    ::testing::Values(
        ReleaseOptions::ReleaseInTest, ReleaseOptions::ReleaseByMaster,
        ReleaseOptions::RestartTServer),
    TestParamToString<ReleaseOptions>);

class ExternalObjectLockTestLargeTTLDelta : public ExternalObjectLockTest {
 public:
  ClusterFlags FlagOverrides() override;
};

TEST_F(ExternalObjectLockTestLargeTTLDelta, MasterThinksTServerStillHasLease) {
  auto master_proxy = cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>();
  constexpr size_t kTSIdx{0};
  auto ts = tablet_server(kTSIdx);
  ASSERT_OK(AcquireLockGlobally(&master_proxy, ts->uuid(), kTxn1, kDatabaseID, kRelationId));
  ASSERT_OK(cluster_->SetFlag(tablet_server(kTSIdx), kTServerYsqlLeaseRefreshFlagName, "false"));
  {
    ASSERT_OK(WaitFor(
        [ts]() -> Result<bool> {
          auto ts_proxy = ts->Proxy<tserver::TabletServerServiceProxy>();
          tserver::GetYSQLLeaseInfoRequestPB req;
          tserver::GetYSQLLeaseInfoResponsePB resp;
          auto rpc = RpcController();
          RETURN_NOT_OK(ts_proxy->GetYSQLLeaseInfo(req, &resp, &rpc));
          RETURN_NOT_OK(ResponseStatus(resp));
          return !resp.is_live();
        },
        10s * kTimeMultiplier, "TServer never expired its lease"));
  }
  // Sanity check to verify the master still thinks the tserver has a live lease. This is a racy
  // check, but should catch large misconfigurations.
  {
    auto cluster_client =
        master::MasterClusterClient(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>());
    auto ts_info_opt = ASSERT_RESULT(cluster_client.GetTabletServer(ts->uuid()));
    ASSERT_TRUE(ts_info_opt);
    ASSERT_TRUE(ts_info_opt->lease_info().is_live());
  }
  ASSERT_OK(cluster_->SetFlag(tablet_server(kTSIdx), kTServerYsqlLeaseRefreshFlagName, "true"));
  // Once the tserver tries to refresh its lease, the master should notice the tserver thinks its
  // lease has expired and give it a new lease.
  auto other_ts = tablet_server(1);
  ASSERT_OK(AcquireLockGlobally(&master_proxy, other_ts->uuid(), kTxn2, kDatabaseID, kRelationId));
}

namespace {
Status BuildLeaseEpochMismatchErrorStatus(
    uint64_t client_lease_epoch, uint64_t server_lease_epoch) {
  return STATUS_FORMAT(
      InvalidArgument,
      "Requestor has a lease epoch of $0 but the latest valid lease epoch for this tserver is $1",
      client_lease_epoch, server_lease_epoch);
}

bool StatusContainsMessage(const Status& status, std::string_view s) {
  return status.message().ToBuffer().find(s) != std::string::npos;
}

bool PGSessionKilledStatus(const Status& status) {
  constexpr std::string_view closed_message = "server closed the connection unexpectedly";
  constexpr std::string_view shutdown_message = "Object Lock Manager Shutdown";
  return status.IsNetworkError() && (StatusContainsMessage(status, closed_message) ||
                                     StatusContainsMessage(status, shutdown_message));
}

bool SameCodeAndMessage(const Status& lhs, const Status& rhs) {
  return lhs.code() == rhs.code() && lhs.message() == rhs.message();
}

Result<master::YSQLLeaseInfoPB> GetTServerLeaseInfo(MiniCluster& cluster, const std::string& uuid) {
  auto cluster_client = master::MasterClusterClient(
      VERIFY_RESULT(cluster.GetLeaderMasterProxy<master::MasterClusterProxy>()));
  return GetTServerLeaseInfo(cluster_client, uuid);
}

Result<master::YSQLLeaseInfoPB> GetTServerLeaseInfo(
    const master::MasterClusterClient& client, const std::string& uuid) {
  auto tserver_entry_opt = VERIFY_RESULT(client.GetTabletServer(uuid));
  if (!tserver_entry_opt) {
    return STATUS_FORMAT(NotFound, "Couldn't find entry for tserver $0", uuid);
  }
  return tserver_entry_opt->lease_info();
}

}  // namespace

void ExternalObjectLockTest::SetUp() {
  YBMiniClusterTestBase::SetUp();
  cluster_ = std::make_unique<ExternalMiniCluster>(MakeExternalMiniClusterOptions());
  ASSERT_OK(cluster_->Start());
}

ClusterFlags ExternalObjectLockTest::BaseFlags() {
  ClusterFlags cluster_flags;
  cluster_flags.master_flags = FlagMap{
      {"master_ysql_operation_lease_ttl_ms", kDefaultMasterYSQLLeaseTTLMilli},
      {"object_lock_cleanup_interval_ms", kDefaultMasterObjectLockCleanupIntervalMilli},
      {"enable_load_balancing", false},
      {"TEST_olm_skip_sending_wait_for_probes", false}};
  cluster_flags.tserver_flags = FlagMap{
      {"ysql_yb_ddl_transaction_block_enabled", true},
      {"enable_object_locking_for_table_locks", true},
      {"ysql_lease_refresher_interval_ms", kDefaultYSQLLeaseRefreshIntervalMilli},
      {"TEST_olm_skip_sending_wait_for_probes", false}};
  return cluster_flags;
}

ExternalMiniClusterOptions ExternalObjectLockTest::MakeExternalMiniClusterOptions() {
  ExternalMiniClusterOptions opts;
  opts.num_tablet_servers = NumberOfTabletServers();
  opts.replication_factor = ReplicationFactor();
  opts.enable_ysql = true;
  opts.wait_for_tservers_to_accept_ysql_connections = WaitForTServersToAcceptYSQL();
  auto base_flags = BaseFlags();
  base_flags.OverrideValues(FlagOverrides());
  opts.extra_master_flags = base_flags.GetFormattedMasterFlags();
  opts.extra_tserver_flags = base_flags.GetFormattedTServerFlags();
  return opts;
}

ExternalTabletServer* ExternalObjectLockTest::tablet_server(size_t index) {
  return cluster_->tablet_server(index);
}

Status ExternalObjectLockTest::WaitForTServerLeaseToExpire(
    const std::string& ts_uuid, MonoDelta timeout) {
  auto cluster_client =
      master::MasterClusterClient(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>());
  return WaitFor(
      [&cluster_client, &ts_uuid]() -> Result<bool> {
        auto ts_opt = VERIFY_RESULT(cluster_client.GetTabletServer(ts_uuid));
        if (!ts_opt) {
          return STATUS_FORMAT(IllegalState, "Couldn't find expected $0", ts_uuid);
        }
        if (!ts_opt->has_lease_info()) {
          return STATUS_FORMAT(IllegalState, "Lease info not set for ts $0", ts_uuid);
        }
        return !ts_opt->lease_info().is_live();
      },
      timeout, "Wait for master to revoke tserver's lease");
}

Status ExternalObjectLockTest::WaitForTServerLease(const std::string& ts_uuid, MonoDelta timeout) {
  auto cluster_client =
      master::MasterClusterClient(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>());
  return WaitFor(
      [&cluster_client, &ts_uuid]() -> Result<bool> {
        auto ts_opt = VERIFY_RESULT(cluster_client.GetTabletServer(ts_uuid));
        return ts_opt && ts_opt->has_lease_info() && ts_opt->lease_info().is_live();
      },
      timeout, "Wait for master to establish tserver's lease");
}

ClusterFlags ExternalObjectLockTestOneTSWithoutLease::FlagOverrides() {
  ClusterFlags flags;
  flags.tserver_flags = FlagMap{{"TEST_tserver_enable_ysql_lease_refresh", false}};
  return flags;
}

ClusterFlags ExternalObjectLockTestLongLeaseTTL::FlagOverrides() {
  ClusterFlags flags;
  flags.master_flags = FlagMap{{"master_ysql_operation_lease_ttl_ms", 20 * 1000}};
  return flags;
}

ClusterFlags ExternalObjectLockTestLeaseLost::FlagOverrides() {
  ClusterFlags flags;
  flags.master_flags = FlagMap{
      {"master_ysql_operation_lease_ttl_ms", ysql_lease_ttl_ms()},
      {"ysql_operation_lease_ttl_client_buffer_ms", 100}};
  return flags;
}

uint64_t ExternalObjectLockTestLeaseLost::ysql_lease_ttl_ms() { return 3000; }

Status ExternalObjectLockTestLeaseLost::ExpectedFailureStatusWithoutLease() {
  if (GetParam() == DisableLeaseMode::DisablePoller) {
    return STATUS(IllegalState, "TServer does not have a live lease");
  }
  return STATUS(
      IllegalState,
      "Expected lease epoch in request 2 does not match TServer's local lease epoch 1");
}

Status ExternalObjectLockTestLeaseLost::DisableLease(ExternalTabletServer* ts) {
  return ToggleLeaseFlags(ts, false);
}

Status ExternalObjectLockTestLeaseLost::EnableLease(ExternalTabletServer* ts) {
  return ToggleLeaseFlags(ts, true);
}

Status ExternalObjectLockTestLeaseLost::ToggleLeaseFlags(ExternalTabletServer* ts, bool new_value) {
  auto str_value = new_value ? "true" : "false";
  RETURN_NOT_OK(cluster_->SetFlag(ts, kTServerYsqlLeaseRefreshFlagName, str_value));
  if (GetParam() == DisableLeaseMode::DisablePollerAndChecker) {
    RETURN_NOT_OK(
        cluster_->SetFlag(ts, "TEST_enable_ysql_operation_lease_expiry_check", str_value));
  }
  return Status::OK();
}

int ExternalObjectLockTestLongLeaseTTLOneTS::ReplicationFactor() { return 1; }
size_t ExternalObjectLockTestLongLeaseTTLOneTS::NumberOfTabletServers() { return 1; }

ClusterFlags ExternalObjectLockTestLargeTTLDelta::FlagOverrides() {
  return ClusterFlags{
      FlagMap{{"ysql_lease_refresher_interval_ms", 300}},
      FlagMap{
          {"master_ysql_operation_lease_ttl_ms", kDefaultMasterYSQLLeaseTTLMilli},
          {"ysql_operation_lease_ttl_client_buffer_ms", kDefaultMasterYSQLLeaseTTLMilli - 1000}}};
}

std::string FormatFlagValue(const FlagValue& v) {
  if (const auto* uintp = std::get_if<uint64_t>(&v)) {
    return Format("$0", *uintp);
  } else if (const auto* intp = std::get_if<int64_t>(&v)) {
    return Format("$0", *intp);
  } else if (const auto* boolp = std::get_if<bool>(&v)) {
    return *boolp ? "true" : "false";
  } else if (const auto* stringp = std::get_if<std::string>(&v)) {
    return *stringp;
  } else {
    CHECK(false) << "could not format flag value";
    return {};
  }
}

FlagMap::FlagMap(std::initializer_list<std::pair<std::string, FlagValue>> flags)
    : map_{flags.begin(), flags.end()} {}

std::vector<std::string> FlagMap::ToFormattedVector() {
  std::vector<std::string> out;
  for (const auto& [name, value] : map_) {
    out.push_back(Format("--$0=$1", name, FormatFlagValue(value)));
  }
  return out;
}

void FlagMap::OverrideValues(const FlagMap& overrides) {
  for (const auto& [name, value] : overrides.map_) {
    map_[name] = value;
  }
}

void ClusterFlags::OverrideValues(const ClusterFlags& overrides) {
  tserver_flags.OverrideValues(overrides.tserver_flags);
  master_flags.OverrideValues(overrides.master_flags);
}

std::vector<std::string> ClusterFlags::GetFormattedTServerFlags() {
  return tserver_flags.ToFormattedVector();
}

std::vector<std::string> ClusterFlags::GetFormattedMasterFlags() {
  return master_flags.ToFormattedVector();
}

}  // namespace yb
