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

#include <algorithm>
#include <functional>
#include <future>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "yb/common/ysql_operation_lease.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster_client.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/test_async_rpc_manager.h"

#include "yb/rpc/messenger.h"

#include "yb/docdb/object_lock_data.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/status_callback.h"
#include "yb/util/test_macros.h"
#include "yb/util/unique_lock.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::chrono_literals;

DECLARE_bool(TEST_enable_object_locking_for_table_locks);
DECLARE_bool(TEST_tserver_disable_heartbeat);
DECLARE_bool(persist_tserver_registry);
DECLARE_int32(retrying_ts_rpc_max_delay_ms);
DECLARE_int32(retrying_rpc_max_jitter_ms);
DECLARE_uint64(master_ysql_operation_lease_ttl_ms);

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

}  // namespace

constexpr uint64_t kDefaultMasterYSQLLeaseTTLMilli = 3 * 1000;

class ObjectLockTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  ObjectLockTest() {}

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_object_locking_for_table_locks) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_ysql_operation_lease) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_ysql_operation_lease_ttl_ms) =
        kDefaultMasterYSQLLeaseTTLMilli;
    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    opts.num_masters = num_masters();
    cluster_ = std::make_unique<MiniCluster>(opts);
    ASSERT_OK(cluster_->Start());
    ASSERT_OK(cluster_->WaitForTabletServerCount(opts.num_tablet_servers));

    rpc::MessengerBuilder bld("Client");
    client_messenger_ = ASSERT_RESULT(bld.Build());
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(client_messenger_.get());
  }

  std::vector<docdb::ObjectLockOwner> CreateRandomExclusiveLockOwners(int num_txns) {
    std::vector<docdb::ObjectLockOwner> lock_owners;
    lock_owners.reserve(num_txns);
    for (int i = 0; i < num_txns; i++) {
      lock_owners.push_back(
        docdb::ObjectLockOwner{TransactionId::GenerateRandom(), 1}
      );
    }
    return lock_owners;
  }

  void DoBeforeTearDown() override {
    client_messenger_->Shutdown();
    YBMiniClusterTestBase::DoBeforeTearDown();
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

  void testAcquireObjectLockWaitsOnTServer(bool do_master_failover);

  Status WaitForTServerLeaseToExpire(const std::string& uuid, MonoDelta timeout);

 private:
  std::unique_ptr<rpc::Messenger> client_messenger_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
};

class ExternalObjectLockTest : public YBMiniClusterTestBase<ExternalMiniCluster> {
 public:
  void SetUp() override;
  virtual ExternalMiniClusterOptions MakeExternalMiniClusterOptions();
  Result<pgwrapper::PGConn> ConnectToTabletServer(ExternalTabletServer* ts, size_t timeout_seconds);
  ExternalTabletServer* tablet_server(size_t index);
};

auto kTxn1 = docdb::ObjectLockOwner{TransactionId::GenerateRandom(), 1};
auto kTxn2 = docdb::ObjectLockOwner{TransactionId::GenerateRandom(), 1};

constexpr uint64_t kDatabaseID = 1;
constexpr uint64_t kObjectId = 1;
constexpr uint64_t kObjectId2 = 2;
constexpr uint64_t kObjectId3 = 3;
constexpr uint64_t kLeaseEpoch = 1;
const MonoDelta kTimeout = MonoDelta::FromSeconds(5);

template <typename Request>
Request AcquireRequestFor(
    const std::string& session_host_uuid, const docdb::ObjectLockOwner& owner, uint64_t database_id,
    uint64_t object_id, TableLockType lock_type, uint64_t lease_epoch) {
  Request req;
  owner.PopulateLockRequest(&req);
  req.set_session_host_uuid(session_host_uuid);
  req.set_lease_epoch(lease_epoch);
  auto* lock = req.add_object_locks();
  lock->set_database_oid(database_id);
  lock->set_object_oid(object_id);
  lock->set_lock_type(lock_type);
  return req;
}

rpc::RpcController RpcController(MonoDelta timeout = kTimeout) {
  rpc::RpcController controller;
  controller.set_timeout(timeout);
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
    uint64_t database_id, uint64_t object_id, TableLockType type, uint64_t lease_epoch,
    MonoDelta timeout) {
  auto resp = std::make_shared<Response>();
  auto controller = std::make_shared<rpc::RpcController>();
  controller->set_timeout(timeout);
  auto promise = std::make_shared<std::promise<Status>>();
  auto future = promise->get_future();
  auto req = AcquireRequestFor<Request>(
      session_host_uuid, owner, database_id, object_id, type, lease_epoch);
  auto callback = [promise, resp, controller]() {
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
    const docdb::ObjectLockOwner& owner, uint64_t database_id, uint64_t object_id,
    uint64_t lease_epoch = kLeaseEpoch,
    MonoDelta timeout = kTimeout) {
  return AcquireLockAsync<
      tserver::AcquireObjectLockRequestPB, tserver::AcquireObjectLockResponsePB,
      tserver::TabletServerServiceProxy>(
      proxy, session_host_uuid, owner, database_id, object_id, TableLockType::ACCESS_SHARE,
      lease_epoch, timeout);
}

Status AcquireLockAt(
    tserver::TabletServerServiceProxy* proxy, const std::string& session_host_uuid,
    const docdb::ObjectLockOwner& owner, uint64_t database_id, uint64_t object_id,
    MonoDelta timeout = kTimeout) {
  auto future = AcquireLockAsyncAt(proxy, session_host_uuid, owner, database_id, object_id);
  return ResolveFutureStatus(future);
}

std::future<Status> AcquireLockGloballyAsync(
    master::MasterDdlProxy* proxy, const std::string& session_host_uuid,
    const docdb::ObjectLockOwner& owner, uint64_t database_id, uint64_t object_id,
    uint64_t lease_epoch = kLeaseEpoch,
    MonoDelta timeout = kTimeout) {
  return AcquireLockAsync<
      master::AcquireObjectLocksGlobalRequestPB, master::AcquireObjectLocksGlobalResponsePB,
      master::MasterDdlProxy>(
      proxy, session_host_uuid, owner, database_id, object_id, TableLockType::ACCESS_EXCLUSIVE,
      lease_epoch, timeout);
}

Status AcquireLockGlobally(
    master::MasterDdlProxy* proxy, const std::string& session_host_uuid,
    const docdb::ObjectLockOwner& owner, uint64_t database_id, uint64_t object_id,
    uint64_t lease_epoch = kLeaseEpoch,
    MonoDelta timeout = kTimeout) {
  auto future = AcquireLockGloballyAsync(
      proxy, session_host_uuid, owner, database_id, object_id, lease_epoch, timeout);
  return ResolveFutureStatus(future);
}

template <typename Request>
Request ReleaseRequestFor(
    const std::string& session_host_uuid, const docdb::ObjectLockOwner& owner,
    std::optional<uint64_t> database_id, std::optional<uint64_t> object_id) {
  Request req;
  owner.PopulateLockRequest(&req);
  req.set_session_host_uuid(session_host_uuid);
  // TODO(Amit): Do we support specifiying db id but not object id?
  if (!database_id || !object_id) {
    req.set_release_all_locks(true);
    return req;
  }
  auto* lock = req.add_object_locks();
  lock->set_database_oid(*database_id);
  lock->set_object_oid(*object_id);
  return req;
}

Status ReleaseLockAt(
    tserver::TabletServerServiceProxy* proxy, const std::string& session_host_uuid,
    const docdb::ObjectLockOwner& owner, std::optional<uint64_t> database_id,
    std::optional<uint64_t> object_id) {
  tserver::ReleaseObjectLockResponsePB resp;
  rpc::RpcController controller = RpcController();
  auto req = ReleaseRequestFor<tserver::ReleaseObjectLockRequestPB>(
      session_host_uuid, owner, database_id, object_id);
  return proxy->ReleaseObjectLocks(req, &resp, &controller);
}

Status ReleaseLockGloballyAt(
    master::MasterDdlProxy* proxy, const std::string& session_host_uuid,
    const docdb::ObjectLockOwner& owner, std::optional<uint64_t> database_id,
    std::optional<uint64_t> object_id) {
  master::ReleaseObjectLocksGlobalResponsePB resp;
  rpc::RpcController controller = RpcController();
  auto req = ReleaseRequestFor<master::ReleaseObjectLocksGlobalRequestPB>(
      session_host_uuid, owner, database_id, object_id);
  return proxy->ReleaseObjectLocksGlobal(req, &resp, &controller);
}

TEST_F(ObjectLockTest, AcquireObjectLocks) {
  const auto& kSessionHostUuid = TSUuid(0);
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  ASSERT_OK(AcquireLockGlobally(&master_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kObjectId));
}

TEST_F(ObjectLockTest, ReleaseObjectLocks) {
  const auto& kSessionHostUuid = TSUuid(0);
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  ASSERT_OK(AcquireLockGlobally(&master_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kObjectId));
  ASSERT_OK(ReleaseLockGloballyAt(&master_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kObjectId));
}

void ObjectLockTest::testAcquireObjectLockWaitsOnTServer(bool do_master_failover) {
  const auto& kSessionHostUuid = TSUuid(0);
  // Acquire lock on TServer-0
  auto* tserver0 = cluster_->mini_tablet_server(0);
  auto tserver0_proxy = TServerProxy(0);
  LOG(INFO) << "Taking DML lock on TServer-0, uuid: " << kSessionHostUuid;
  ASSERT_OK(AcquireLockAt(
      &tserver0_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kObjectId));

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
      AcquireLockGloballyAsync(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kObjectId);

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
  ASSERT_OK(ReleaseLockAt(&tserver0_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kObjectId));

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
  ASSERT_OK(AcquireLockGlobally(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kObjectId));
  ASSERT_OK(ReleaseLockGloballyAt(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kObjectId));

  // Release non-existent lock.
  ASSERT_OK(ReleaseLockGloballyAt(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kObjectId2));
}

void DumpMasterAndTServerLocks(
    MiniCluster* cluster, const std::string& message = "", bool dump_master_html = false,
    bool dump_tserver_html = true) {
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
  ASSERT_OK(AcquireLockGlobally(&master_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kObjectId));
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
      AcquireLockGloballyAsync(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kObjectId);

  // Wait for the lock acquisition to wait at master.
  ASSERT_OK(WaitFor(
      [master_local_lock_manager]() -> bool {
        return master_local_lock_manager->TEST_WaitingLocksSize() > 0;
      },
      kTimeout, "Wait for blocking on the master"));

  DumpMasterAndTServerLocks(cluster_.get(), "After requesting lock from session-2 ");
  // Locks for weak intents are granted at the Master. But locks for strong intents are not granted.
  // Neither of the locks are granted by the TServer, as the request is still waiting at the Master.
  auto old_expected_locks = expected_locks;
  expected_locks = master_local_lock_manager->TEST_GrantedLocksSize();
  ASSERT_GE(expected_locks, 1);
  ASSERT_GT(expected_locks, old_expected_locks);
  for (auto ts : cluster_->mini_tablet_servers()) {
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), old_expected_locks);
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);
  }

  // Release lock from Session-1
  ASSERT_OK(ReleaseLockGloballyAt(&master_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kObjectId));

  // Verify that lock acquistion for session-2 is successful.
  ASSERT_OK(ResolveFutureStatus(acquire_future));

  DumpMasterAndTServerLocks(
      cluster_.get(), "After releasing lock from session-1 : session-2 should acquire the lock");
  expected_locks = master_local_lock_manager->TEST_GrantedLocksSize();
  ASSERT_GE(expected_locks, 1);
  ASSERT_EQ(expected_locks, old_expected_locks);
  ASSERT_EQ(master_local_lock_manager->TEST_WaitingLocksSize(), 0);
  for (auto ts : cluster_->mini_tablet_servers()) {
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), old_expected_locks);
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);
  }

  // Release lock from Session-2
  ASSERT_OK(ReleaseLockGloballyAt(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kObjectId));
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
  for (uint64_t object_id = 0; object_id < kNumLocksTotal; object_id++) {
    auto host_idx = object_id / kLocksPerHost;
    auto ddl_idx = (object_id / kNumObjectsPerDDL) % kNumDDLsPerHost;
    ASSERT_OK(AcquireLockGlobally(
        &master_proxy, TSUuid(host_idx), ddl_txns[ddl_idx * kNumHosts + host_idx], kDatabaseID,
        object_id));
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
  ASSERT_OK(
      ReleaseLockGloballyAt(&master_proxy, TSUuid(0), ddl_txns[0], std::nullopt, std::nullopt));

  DumpMasterAndTServerLocks(cluster_.get(), "After Releasing locks from host-0, session-0");
  num_locks = kEntriesPerRequest * (kNumHosts * kNumDDLsPerHost - 1) * kNumObjectsPerDDL;
  ASSERT_EQ(master_local_lock_manager->TEST_GrantedLocksSize(), num_locks);
  ASSERT_EQ(master_local_lock_manager->TEST_WaitingLocksSize(), 0);
  for (auto ts : cluster_->mini_tablet_servers()) {
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), num_locks);
    ASSERT_EQ(ts->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);
  }

  // Also, Release all locks taken from host-1
  auto ts1_lease_epoch =
      ASSERT_RESULT(cluster_->mini_master()->catalog_manager_impl().LookupTSByUUID(TSUuid(1)))
          ->LockForRead()
    ->pb.lease_epoch();

  cluster_->mini_master()
      ->master()
      ->catalog_manager_impl()
      ->object_lock_info_manager()
      ->ReleaseLocksHeldByExpiredLeaseEpoch(TSUuid(1), ts1_lease_epoch, /* wait */ true);

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
      &tserver0_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kObjectId));

  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  auto ddl_future =
      AcquireLockGloballyAsync(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kObjectId);

  // Wait. But the lock acquisition should not be successful.
  ASSERT_OK(WaitFor(
      [tserver0]() -> bool {
        return tserver0->server()->ts_local_lock_manager()->TEST_WaitingLocksSize() > 0;
      },
      kTimeout, "wait for blocking on TServer0"));

  auto num_ts = cluster_->num_tablet_servers();
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(num_ts + 1));

  // Add TS-4.
  auto* added_tserver = cluster_->mini_tablet_server(num_ts);
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
      AcquireLockAsyncAt(&added_tserver_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kObjectId);
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

  ASSERT_OK(ReleaseLockAt(&tserver0_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kObjectId));
  // Verify that DDL lock acquistion is successful.
  ASSERT_OK(ResolveFutureStatus(ddl_future));

  // Release DDL lock
  ASSERT_OK(ReleaseLockGloballyAt(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kObjectId));

  // Verify that DML lock acquistion is successful.
  ASSERT_OK(ResolveFutureStatus(ts_future));
  ASSERT_GE(added_tserver->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), 1);
  ASSERT_EQ(added_tserver->server()->ts_local_lock_manager()->TEST_WaitingLocksSize(), 0);

  // Release DML lock at TS-4
  ASSERT_OK(
      ReleaseLockAt(&added_tserver_proxy, kSessionHostUuid, kTxn1, kDatabaseID, kObjectId));
  ASSERT_EQ(added_tserver->server()->ts_local_lock_manager()->TEST_GrantedLocksSize(), 0);
}

TEST_F(ObjectLockTest, BootstrapTServersUponAddition) {
  const auto& kSessionHostUuid = TSUuid(0);
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  ASSERT_OK(AcquireLockGlobally(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kObjectId));

  auto num_ts = cluster_->num_tablet_servers();
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(num_ts + 1));

  auto* added_tserver = cluster_->mini_tablet_server(num_ts);
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

  ASSERT_OK(ReleaseLockGloballyAt(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kObjectId));

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
  ASSERT_OK(AcquireLockGlobally(&master_proxy, uuid_to_take_down, kTxn1, kDatabaseID, kObjectId));
  auto kBlockingRequestTimeout = MonoDelta::FromSeconds(10);
  // Acquire exclusive lock for tserver1. This should block.
  auto future = AcquireLockGloballyAsync(
      &master_proxy, TSUuid(1), kTxn2, kDatabaseID, kObjectId, kLeaseEpoch,
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
        &tserver0_proxy, uuid_to_take_down, kTxn1, kDatabaseID, kObjectId));
  }
  LOG(INFO) << "Shutting down tablet server " << uuid_to_take_down;
  ASSERT_NOTNULL(cluster_->find_tablet_server(uuid_to_take_down))->Shutdown();
  LOG(INFO) << Format("Waiting for tablet server $0 to lose its lease", uuid_to_take_down);
  ASSERT_OK(WaitForTServerLeaseToExpire(uuid_to_take_down, kBlockingRequestTimeout));
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  ASSERT_OK(AcquireLockGlobally(&master_proxy, TSUuid(1), kTxn2, kDatabaseID, kObjectId));
  ASSERT_OK(cluster_->mini_tablet_server(idx_to_take_down)->Start());
}

TEST_F(ObjectLockTest, TServerLeaseExpiresAfterExclusiveLockRequest) {
  auto kBlockingRequestTimeout = MonoDelta::FromSeconds(20);
  ASSERT_GT(kBlockingRequestTimeout.ToMilliseconds(), FLAGS_master_ysql_operation_lease_ttl_ms);
  auto idx_to_take_down = 0;
  auto uuid_to_take_down = TSUuid(idx_to_take_down);
  {
    auto* tserver0 = cluster_->mini_tablet_server(idx_to_take_down);
    auto tserver0_proxy = TServerProxyFor(tserver0);
    ASSERT_OK(AcquireLockAt(
        &tserver0_proxy, uuid_to_take_down, kTxn1, kDatabaseID, kObjectId));
  }
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  auto future = AcquireLockGloballyAsync(
      &master_proxy, TSUuid(1), kTxn2, kDatabaseID, kObjectId, kLeaseEpoch,
      kBlockingRequestTimeout);

  ASSERT_OK(WaitFor(
      [&]() -> bool {
        return cluster_->mini_tablet_server(idx_to_take_down)
                   ->server()
                   ->ts_local_lock_manager()
                   ->TEST_WaitingLocksSize() > 0;
      },
      kBlockingRequestTimeout,
      "Timed out waiting for acquire lock request to block on the master"));
  LOG(INFO) << "Shutting down tablet server " << uuid_to_take_down;
  ASSERT_NOTNULL(cluster_->find_tablet_server(uuid_to_take_down))->Shutdown();
  // Now wait for the lease to expire. After the lease expires the lock acquisition should succeed.
  LOG(INFO) << Format("Waiting for tablet server $0 to lose its lease", uuid_to_take_down);
  ASSERT_OK(WaitForTServerLeaseToExpire(uuid_to_take_down, kBlockingRequestTimeout));
  ASSERT_OK(ResolveFutureStatus(future));
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
      &master_proxy, ts_to_restart_uuid, kTxn1, kDatabaseID, kObjectId, kLeaseEpoch, kTimeout));
  cluster_->mini_tablet_server(ts_to_restart_idx)->Shutdown();
  ASSERT_OK(cluster_->mini_tablet_server(ts_to_restart_idx)->Start());
  // The lock should be released when the new tserver process heartbeats to the master leader.
  ASSERT_OK(AcquireLockGlobally(
      &master_proxy, TSUuid(1), kTxn1, kDatabaseID, kObjectId, kLeaseEpoch, kTimeout));
}

TEST_F(ObjectLockTest, TServerHeldExclusiveLocksReleasedAfterExpiry) {
  auto kLeaseTimeoutDeadline = MonoDelta::FromSeconds(20);
  ASSERT_GT(kLeaseTimeoutDeadline.ToMilliseconds(), FLAGS_master_ysql_operation_lease_ttl_ms);
  auto ts_idx = 0;
  auto ts_uuid = TSUuid(0);
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  ASSERT_OK(AcquireLockGlobally(
      &master_proxy, ts_uuid, kTxn1, kDatabaseID, kObjectId, kLeaseEpoch, kTimeout));
  cluster_->mini_tablet_server(ts_idx)->FailHeartbeats(true);
  ASSERT_OK(WaitForTServerLeaseToExpire(ts_uuid, kLeaseTimeoutDeadline));
  cluster_->mini_tablet_server(ts_idx)->FailHeartbeats(false);
  // The task to release the locks should be kicked off when the master marks the tserver's lease as
  // expired.
  ASSERT_OK(AcquireLockGlobally(
      &master_proxy, TSUuid(1), kTxn1, kDatabaseID, kObjectId, kLeaseEpoch, kTimeout));
}

TEST_F(ObjectLockTest, TServerCanAcquireLocksAfterRestart) {
  auto ts_idx = 0;
  auto ts_uuid = TSUuid(ts_idx);
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  uint64_t lease_epoch = kLeaseEpoch;
  ASSERT_OK(AcquireLockGlobally(
      &master_proxy, ts_uuid, kTxn1, kDatabaseID, kObjectId, lease_epoch, kTimeout));
  cluster_->mini_tablet_server(ts_idx)->Shutdown();
  ASSERT_OK(cluster_->mini_tablet_server(ts_idx)->Start());
  // The lease epoch should be incremented when the tserver acquires a new lease.
  lease_epoch++;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto status = AcquireLockGlobally(
            &master_proxy, ts_uuid, kTxn1, kDatabaseID, kObjectId2, lease_epoch, kTimeout);
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
  auto status = AcquireLockGlobally(
      &master_proxy, ts_uuid, kTxn1, kDatabaseID, kObjectId3, kLeaseEpoch, kTimeout);
  EXPECT_THAT(status, EqualsStatus(BuildLeaseEpochMismatchErrorStatus(kLeaseEpoch, lease_epoch)));
}

TEST_F(ObjectLockTest, TServerCanAcquireLocksAfterLeaseExpiry) {
  auto kLeaseTimeoutDeadline = MonoDelta::FromSeconds(20);
  ASSERT_GT(kLeaseTimeoutDeadline.ToMilliseconds(), FLAGS_master_ysql_operation_lease_ttl_ms);
  auto ts_idx = 0;
  auto ts_uuid = TSUuid(ts_idx);
  uint64_t lease_epoch = kLeaseEpoch;
  auto master_proxy = ASSERT_RESULT(MasterLeaderProxy());
  ASSERT_OK(AcquireLockGlobally(
      &master_proxy, ts_uuid, kTxn1, kDatabaseID, kObjectId, lease_epoch, kTimeout));
  cluster_->mini_tablet_server(ts_idx)->FailHeartbeats(true);
  ASSERT_OK(WaitForTServerLeaseToExpire(ts_uuid, kLeaseTimeoutDeadline));
  cluster_->mini_tablet_server(ts_idx)->FailHeartbeats(false);
  // The lease epoch should be incremented when the tserver acquires a new lease.
  ++lease_epoch;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        // Acquire a lock on a different object with the new lease.
        auto status = AcquireLockGlobally(
            &master_proxy, ts_uuid, kTxn1, kDatabaseID, kObjectId2, lease_epoch, kTimeout);
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
      &master_proxy, ts_uuid, kTxn1, kDatabaseID, kObjectId3, kLeaseEpoch, kTimeout);
  EXPECT_THAT(status, EqualsStatus(BuildLeaseEpochMismatchErrorStatus(kLeaseEpoch, lease_epoch)));
}

class MultiMasterObjectLockTest : public ObjectLockTest {
 protected:
  int num_masters() override {
    return 3;
  }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_persist_tserver_registry) = true;
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
    LOG(INFO) << "Acquiring lock on object " << kObjectId << " from master "
              << leader_master1->ToString();
    auto master_proxy = MasterProxy(leader_master1);
    ASSERT_OK(AcquireLockGlobally(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kObjectId));
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

  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(num_ts + 1));

  auto* added_tserver = cluster_->mini_tablet_server(num_ts);
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
    LOG(INFO) << "Releasing lock on object " << kObjectId << " at master "
              << leader_master2->ToString();
    auto master_proxy = MasterProxy(leader_master2);
    ASSERT_OK(
        ReleaseLockGloballyAt(&master_proxy, kSessionHostUuid, kTxn2, kDatabaseID, kObjectId));
  }
}

TEST_F(ExternalObjectLockTest, TabletServerKillsSessionsWhenItAcquiresNewLease) {
  constexpr size_t kTSIdx = 0;
  MonoDelta timeout = MonoDelta::FromSeconds(10);
  // We want to test the tserver kills hosted sessions when the master gives it a new lease so
  // disable the lease expiration check task on the tserver.
  auto ts_uuid = tablet_server(kTSIdx)->uuid();
  constexpr std::string_view kTableName = "test_table";
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB("yugabyte", kTSIdx));
  ASSERT_OK(conn.Execute(Format("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTableName)));
  ASSERT_OK(cluster_->SetFlag(tablet_server(kTSIdx), "TEST_tserver_disable_heartbeat", "true"));

  auto cluster_client =
      master::MasterClusterClient(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>());
  // Wait for the lease to be lost.
  ASSERT_OK(WaitFor(
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
      timeout, "Wait for master to revoke tserver's lease"));
  ASSERT_OK(cluster_->SetFlag(tablet_server(kTSIdx), "TEST_tserver_disable_heartbeat", "false"));
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
}

Status ObjectLockTest::WaitForTServerLeaseToExpire(const std::string& uuid, MonoDelta timeout) {
  return WaitFor(
      [&]() -> Result<bool> {
        auto ts_manager = VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->master()->ts_manager();
        auto ts_desc_opt = ts_manager->LookupTSByUUID(uuid);
        if (!ts_desc_opt) {
          return STATUS_FORMAT(IllegalState, "Failed to lookup expected TS: ", uuid);
        }
        auto& ts_desc = *ts_desc_opt;
        auto l = ts_desc->LockForRead();
        return !l->pb.live_client_operation_lease();
      },
      timeout, Format("Timed out waiting for master to clear expired lease on tserver $0", uuid));
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
  constexpr std::string_view message = "server closed the connection unexpectedly";
  return status.IsNetworkError() && StatusContainsMessage(status, message);
}

bool SameCodeAndMessage(const Status& lhs, const Status& rhs) {
  return lhs.code() == rhs.code() && lhs.message() == rhs.message();
}

}  // namespace

void ExternalObjectLockTest::SetUp() {
  YBMiniClusterTestBase::SetUp();
  cluster_ = std::make_unique<ExternalMiniCluster>(MakeExternalMiniClusterOptions());
  ASSERT_OK(cluster_->Start());
}

ExternalMiniClusterOptions ExternalObjectLockTest::MakeExternalMiniClusterOptions() {
  ExternalMiniClusterOptions opts;
  opts.num_tablet_servers = 3;
  opts.enable_ysql = true;
  opts.extra_master_flags = {
      "--TEST_enable_object_locking_for_table_locks", "--TEST_enable_ysql_operation_lease",
      Format("--master_ysql_operation_lease_ttl_ms=$0", kDefaultMasterYSQLLeaseTTLMilli)};
  opts.extra_tserver_flags = {
      "--TEST_enable_object_locking_for_table_locks", "--TEST_enable_ysql_operation_lease"};
  return opts;
}

Result<pgwrapper::PGConn> ExternalObjectLockTest::ConnectToTabletServer(
    ExternalTabletServer* ts, size_t timeout_seconds) {
  auto settings = pgwrapper::PGConnSettings{
      .host = ts->bind_host(),
      .port = ts->ysql_port(),
      .dbname = "yugabyte",
      .connect_timeout = timeout_seconds};
  return pgwrapper::PGConnBuilder(settings).Connect(/* simple_query_protocol */ false);
}

ExternalTabletServer* ExternalObjectLockTest::tablet_server(size_t index) {
  return cluster_->tablet_server(index);
}

}  // namespace yb
