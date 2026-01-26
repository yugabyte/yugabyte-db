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

#include "yb/master/object_lock_info_manager.h"

#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <google/protobuf/util/message_differencer.h>

#include "yb/common/common_flags.h"
#include "yb/common/pg_catversions.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/ysql_operation_lease.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_error.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/ts_manager.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/poller.h"
#include "yb/rpc/rpc_context.h"

#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/to_stream.h"
#include "yb/util/trace.h"

DEFINE_RUNTIME_uint64(master_ysql_operation_lease_ttl_ms, 30 * 1000,
                      "The lifetime of ysql operation lease extensions. The ysql operation lease "
                      "allows tservers to host pg sessions and serve reads and writes to user data "
                      "through the YSQL API.");
TAG_FLAG(master_ysql_operation_lease_ttl_ms, advanced);

DEFINE_RUNTIME_uint64(ysql_operation_lease_ttl_client_buffer_ms, 2 * 1000,
                      "The difference between the duration masters and tservers use for ysql "
                      "operation lease TTLs. This is non-zero to account for clock skew and give "
                      "tservers time to clean up their existing pg sessions before the master "
                      "leader ignores them for exclusive table lock requests.");
TAG_FLAG(ysql_operation_lease_ttl_client_buffer_ms, advanced);

DEFINE_NON_RUNTIME_uint64(object_lock_cleanup_interval_ms, 5000,
                          "The interval between runs of the background cleanup task for "
                          "table-level locks held by unresponsive TServers.");

DEFINE_test_flag(
    bool, skip_launch_release_request, false,
    "If true, skip launching the release request after persisting it to in progress requests.");

DEFINE_test_flag(bool, allow_unknown_txn_release_request, false,
    "If true, do not error out if a release request comes in for an unknown transaction.");

DECLARE_bool(enable_heartbeat_pg_catalog_versions_cache);
DECLARE_int32(send_wait_for_report_interval_ms);
DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(ysql_yb_enable_invalidation_messages);

namespace yb {
namespace master {

using namespace std::literals;
using namespace std::placeholders;
using server::MonitoredTaskState;
using strings::Substitute;
using tserver::AcquireObjectLockRequestPB;
using tserver::AcquireObjectLockResponsePB;
using tserver::ReleaseObjectLockRequestPB;
using tserver::ReleaseObjectLockResponsePB;
using tserver::TabletServerErrorPB;

namespace {

Status ValidateLockRequest(
    const AcquireObjectLockRequestPB& req,
    const std::optional<uint64_t>& requestor_latest_lease_epoch) {
  if (!req.subtxn_id()) {
    return STATUS_FORMAT(
        InvalidArgument, "subtxn_id not set for exclusive object lock req $0",
        req.ShortDebugString());
  }
  if (!requestor_latest_lease_epoch || req.lease_epoch() != *requestor_latest_lease_epoch) {
    return STATUS_FORMAT(
        InvalidArgument,
        "Requestor has a lease epoch of $0 but the latest valid lease epoch for this tserver is $1",
        req.lease_epoch(), requestor_latest_lease_epoch);
  }
  return Status::OK();
}

constexpr auto kTserverRpcsTimeoutDefaultSecs = 60s;

template <typename T>
  requires std::disjunction_v<
               std::is_same<T, AcquireObjectLockRequestPB>,
               std::is_same<T, ReleaseObjectLockRequestPB>>
constexpr bool kIsReleaseRequest = false;

template <>
constexpr bool kIsReleaseRequest<ReleaseObjectLockRequestPB> = true;

}  // namespace

class ObjectLockInfoManager::Impl {
 public:
  Impl(Master& master, CatalogManager& catalog_manager)
      : master_(master),
        catalog_manager_(catalog_manager),
        clock_(master.clock()),
        poller_(std::bind(&Impl::CleanupExpiredLeaseEpochs, this)) {
    CHECK_OK(ThreadPoolBuilder("object_lock_info_manager").Build(&lock_manager_thread_pool_));
    local_lock_manager_ = std::make_shared<tserver::TSLocalLockManager>(
        clock_, master_.tablet_server(), master_, lock_manager_thread_pool_.get(),
        master_.metric_entity());
  }

  void Start() {
    poller_.Start(
        &catalog_manager_.Scheduler(),
        MonoDelta::FromMilliseconds(FLAGS_object_lock_cleanup_interval_ms));
    waiting_txn_registry_ = std::make_unique<docdb::LocalWaitingTxnRegistry>(
        master_.client_future(), clock_, master_.permanent_uuid(), lock_manager_thread_pool_.get());
    waiting_txn_registry_poller_ = std::make_unique<rpc::Poller>(
        master_.permanent_uuid(), std::bind(&docdb::LocalWaitingTxnRegistry::SendWaitForGraph,
        waiting_txn_registry_.get()));
    tserver::TSLocalLockManagerPtr lock_manager = nullptr;
    {
      LockGuard lock(mutex_);
      object_lock_infos_map_.clear();
      if (local_lock_manager_) {
        lock_manager = local_lock_manager_;
      }
    }
    if (lock_manager) {
      lock_manager->Start(waiting_txn_registry_.get());
    }
    waiting_txn_registry_poller_->Start(
        &master_.messenger()->scheduler(), FLAGS_send_wait_for_report_interval_ms * 1ms);
  }

  void Shutdown() {
    poller_.Shutdown();
    tserver::TSLocalLockManager* lock_manager = nullptr;
    {
      LockGuard lock(mutex_);
      object_lock_infos_map_.clear();
      if (local_lock_manager_) {
        lock_manager = local_lock_manager_.get();
      }
    }
    if (lock_manager) {
      lock_manager->Shutdown();
    }
    if (waiting_txn_registry_poller_) {
      waiting_txn_registry_poller_->Shutdown();
      waiting_txn_registry_poller_.reset();
    }
    if (waiting_txn_registry_) {
      waiting_txn_registry_->StartShutdown();
      waiting_txn_registry_->CompleteShutdown();
      waiting_txn_registry_.reset();
    }
  }

  void LockObject(
      AcquireObjectLockRequestPB&& req, CoarseTimePoint deadline, StdStatusCallback&& callback);

  void PopulateDbCatalogVersionCache(ReleaseObjectLockRequestPB& req);
  Status UnlockObject(
      ReleaseObjectLockRequestPB&& req, std::optional<StdStatusCallback>&& callback = std::nullopt,
      std::optional<LeaderEpoch> leader_epoch = std::nullopt);
  Status UnlockObjectSync(
      const ReleaseObjectLocksGlobalRequestPB& master_request,
      tserver::ReleaseObjectLockRequestPB&& tserver_request, CoarseTimePoint deadline);
  void UnlockObject(const TransactionId& txn_id);
  Status MaybePopulateHostInfo(ReleaseObjectLockRequestPB& req) EXCLUDES(mutex_);
  Status PopulateHostInfo(const TransactionId& txn_id, ReleaseObjectLockRequestPB& req)
      EXCLUDES(mutex_);
  bool IsDdlVerificationInProgress(const TransactionId& txn) const;

  Status RefreshYsqlLease(const RefreshYsqlLeaseRequestPB& req,
                          RefreshYsqlLeaseResponsePB& resp,
                          rpc::RpcContext& rpc,
                          const LeaderEpoch& epoch);

  Status RelinquishYsqlLease(const RelinquishYsqlLeaseRequestPB& req,
                             RelinquishYsqlLeaseResponsePB& resp,
                             rpc::RpcContext& rpc,
                             const LeaderEpoch& epoch);

  std::shared_ptr<CountDownLatch> ReleaseLocksHeldByExpiredLeaseEpoch(
      const std::string& tserver_uuid, uint64 max_lease_epoch_to_release,
      std::optional<LeaderEpoch> leader_epoch);

  std::unordered_map<std::string, SysObjectLockEntryPB::LeaseInfoPB> GetLeaseInfos() const
      EXCLUDES(mutex_);

  void BootstrapLocksPostLoad();

  Status PersistRequest(
      LeaderEpoch epoch, const AcquireObjectLockRequestPB& req, const TransactionId& txn_id)
      EXCLUDES(mutex_);
  Status PersistRequest(
      LeaderEpoch epoch, const ReleaseObjectLockRequestPB& req, const TransactionId& txn_id)
      EXCLUDES(mutex_);
  Status AddToInProgress(LeaderEpoch epoch, const ReleaseObjectLockRequestPB& req) EXCLUDES(mutex_);

  tserver::DdlLockEntriesPB ExportObjectLockInfo() EXCLUDES(mutex_);
  tserver::DdlLockEntriesPB ExportObjectLockInfoUnlocked() REQUIRES(mutex_);

  void UpdateObjectLocks(const std::string& tserver_uuid, std::shared_ptr<ObjectLockInfo> info)
      EXCLUDES(mutex_);
  void RelaunchInProgressRequests(const LeaderEpoch& leader_epoch, const std::string& tserver_uuid);
  void Clear() EXCLUDES(mutex_);

  const server::ClockPtr& clock() const {
    return clock_;
  }

  std::shared_ptr<ObjectLockInfo> GetOrCreateObjectLockInfo(const std::string& ts_uuid)
      EXCLUDES(mutex_);

  uint64_t next_request_id() {
    return next_request_id_.fetch_add(1);
  }

  bool TabletServerHasLiveLease(const std::string& ts_uuid) const EXCLUDES(mutex_);
  void UpdateTxnHostSessionMap(
      const TransactionId& txn_id, const std::string& host_session_uuid, uint64_t lease_epoch)
      EXCLUDES(mutex_);
  void RemoveTxnFromHostSessionMap(const TransactionId& txn_id) EXCLUDES(mutex_);

  std::shared_ptr<tserver::TSLocalLockManager> TEST_ts_local_lock_manager() EXCLUDES(mutex_) {
    // No need to acquire the leader lock for testing.
    LockGuard lock(mutex_);
    return local_lock_manager_;
  }

  /*
  The local lock manager is used to acquire and release locks on the master itself.

  We will need to recreate the state of the ts_local_lock_manager on the master
  when the master assumes leadership. This will be done by clearing the TSLocalManager and
  replaying the DDL lock requests
  */
  tserver::TSLocalLockManagerPtr ts_local_lock_manager() EXCLUDES(mutex_) {
    catalog_manager_.AssertLeaderLockAcquiredForReading();
    LockGuard lock(mutex_);
    return local_lock_manager_;
  }

  TSDescriptorVector GetAllTSDescriptorsWithALiveLease() const;

  std::optional<uint64_t> GetLeaseEpoch(const std::string& ts_uuid) EXCLUDES(mutex_);

 private:
  std::shared_ptr<tserver::TSLocalLockManager> ts_local_lock_manager_during_catalog_loading()
      EXCLUDES(mutex_) {
    catalog_manager_.AssertLeaderLockAcquiredForWriting();
    LockGuard lock(mutex_);
    return local_lock_manager_;
  }

  // Called by the poller to mark leases as expired and clean up locks held by expired lease epochs.
  // This should only be called by the poller as it accesses cleanup task state without
  // synchronization.
  void CleanupExpiredLeaseEpochs() EXCLUDES(mutex_);

  Master& master_;
  CatalogManager& catalog_manager_;
  // Used to track in-progress release requests.
  std::atomic<uint64_t> next_request_id_{1};
  const server::ClockPtr clock_;

  using MutexType = std::mutex;
  using LockGuard = std::lock_guard<MutexType>;
  mutable MutexType mutex_;
  std::unordered_map<std::string, std::shared_ptr<ObjectLockInfo>> object_lock_infos_map_
      GUARDED_BY(mutex_);
  struct TxnHostInfo {
    std::string host_session_uuid;
    uint64_t lease_epoch;
  };

  std::unordered_map<TransactionId, TxnHostInfo> txn_host_info_map_ GUARDED_BY(mutex_);
  rpc::Poller poller_;
  std::unique_ptr<ThreadPool> lock_manager_thread_pool_;
  std::shared_ptr<tserver::TSLocalLockManager> local_lock_manager_ GUARDED_BY(mutex_);
  // Only accessed from a single thread for now, so no need for synchronization.
  std::unordered_map<TabletServerId, std::shared_ptr<CountDownLatch>>
      expired_lease_epoch_cleanup_tasks_;
  std::unique_ptr<docdb::LocalWaitingTxnRegistry> waiting_txn_registry_;
  std::unique_ptr<rpc::Poller> waiting_txn_registry_poller_;
};

template <class Req>
class UpdateAll {
 public:
  UpdateAll() = default;
  virtual ~UpdateAll() = default;
  virtual const Req& request() const = 0;
  virtual CoarseTimePoint GetClientDeadline() const = 0;
  virtual bool TabletServerHasLiveLease(const std::string& uuid) = 0;
  virtual std::optional<uint64_t> GetLeaseEpoch(const std::string& uuid) = 0;
  virtual std::string LogPrefix() const = 0;
  virtual const ash::WaitStateInfoPtr& wait_state() const = 0;
};

template <class Req>
class UpdateAllTServers : public std::enable_shared_from_this<UpdateAllTServers<Req>>,
                          public UpdateAll<Req> {
 public:
  // Used for AcquireObjectLock
  UpdateAllTServers(
      Master& master, CatalogManager& catalog_manager,
      ObjectLockInfoManager::Impl& object_lock_info_manager, Req&& req,
      StdStatusCallback&& callback, CoarseTimePoint deadline,
      std::optional<uint64_t> requestor_latest_lease_epoch, LeaderEpoch epoch,
      TransactionId txn_id);
  // Used for ReleaseObjectLock
  UpdateAllTServers(
      Master& master, CatalogManager& catalog_manager,
      ObjectLockInfoManager::Impl& object_lock_info_manager, Req&& req,
      std::optional<StdStatusCallback>&& callback, LeaderEpoch leader_epoch, TransactionId txn_id);

  Status Launch();
  const Req& request() const override {
    return req_;
  }

  CoarseTimePoint GetClientDeadline() const override {
    return deadline_;
  }

  bool TabletServerHasLiveLease(const std::string& uuid) override {
    return object_lock_info_manager_.TabletServerHasLiveLease(uuid);
  }

  std::optional<uint64_t> GetLeaseEpoch(const std::string& uuid) override {
    return object_lock_info_manager_.GetLeaseEpoch(uuid);
  }

  Trace *trace() const {
    return trace_.get();
  }

  const ash::WaitStateInfoPtr& wait_state() const override {
    return wait_state_;
  }

  std::string LogPrefix() const override;

 private:
  void LaunchRpcs();
  void LaunchRpcsFrom(size_t from_idx);
  void Done(size_t i, const Status& s);
  void CheckForDone();
  // Relaunches if there have been new TServers who joined. Returns true if relaunched.
  bool RelaunchIfNecessary();
  void DoneAll();
  Status AfterRpcs();
  void DoCallbackAndRespond(const Status& s);
  Status DoPersistRequestUnlocked(const ScopedLeaderSharedLock& l);
  Status DoPersistRequest();

  std::shared_ptr<RetrySpecificTSRpcTask> TServerTaskFor(
      const TabletServerId& ts_uuid, StdStatusCallback&& callback);

  Master& master_;
  CatalogManager& catalog_manager_;
  ObjectLockInfoManager::Impl& object_lock_info_manager_;
  TSDescriptorVector ts_descriptors_;
  std::atomic<size_t> ts_pending_;
  std::vector<Status> statuses_;
  const Req req_;
  const TransactionId txn_id_;
  LeaderEpoch epoch_;
  std::optional<StdStatusCallback> callback_;
  std::optional<uint64_t> requestor_latest_lease_epoch_;
  CoarseTimePoint deadline_;
  const TracePtr trace_;
  const ash::WaitStateInfoPtr wait_state_;
};

template <class Req, class Resp>
class UpdateTServer : public RetrySpecificTSRpcTask {
 public:
  UpdateTServer(
      Master& master, ThreadPool* callback_pool, const TabletServerId& ts_uuid,
      std::shared_ptr<UpdateAll<Req>> shared_all_tservers, StdStatusCallback&& callback);

  server::MonitoredTaskType type() const override { return server::MonitoredTaskType::kObjectLock; }

  std::string type_name() const override { return "Object Lock"; }

  std::string description() const override {
    return Format("$0 $1", type_name(), LogPrefix());
  }

  std::string LogPrefix() const {
    return Format("$0 for TServer: $1 ", shared_all_tservers_->LogPrefix(), permanent_uuid());
  }

 protected:
  void Finished(const Status& status) override;

  Req request() const;

  bool RetryTaskAfterRPCFailure(const Status& status) override;

 private:
  TabletId tablet_id() const override { return TabletId(); }

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  StdStatusCallback callback_;
  Resp resp_;

  std::shared_ptr<UpdateAll<Req>> shared_all_tservers_;
};

namespace {

const std::string kNotTheMasterLeader = "Master is not the leader";
const std::string kEpochChanged = "Epoch changed";

Status CheckLeaderLockStatus(const ScopedLeaderSharedLock& l, std::optional<LeaderEpoch> epoch) {
  if (!l.IsInitializedAndIsLeader()) {
    return STATUS(IllegalState, kNotTheMasterLeader);
  }
  if (epoch.has_value() && l.epoch() != *epoch) {
    return STATUS(IllegalState, kEpochChanged);
  }
  return Status::OK();
}

AcquireObjectLockRequestPB TserverRequestFor(
    const AcquireObjectLocksGlobalRequestPB& master_request) {
  AcquireObjectLockRequestPB req;
  req.set_txn_id(master_request.txn_id());
  req.set_subtxn_id(master_request.subtxn_id());
  req.set_session_host_uuid(master_request.session_host_uuid());
  req.mutable_object_locks()->CopyFrom(master_request.object_locks());
  req.set_lease_epoch(master_request.lease_epoch());
  if (master_request.has_ignore_after_hybrid_time()) {
    req.set_ignore_after_hybrid_time(master_request.ignore_after_hybrid_time());
  }
  if (master_request.has_propagated_hybrid_time()) {
    req.set_propagated_hybrid_time(master_request.propagated_hybrid_time());
  }
  req.set_status_tablet(master_request.status_tablet());
  return req;
}

ReleaseObjectLockRequestPB TserverRequestFor(
    const ReleaseObjectLocksGlobalRequestPB& master_request, size_t request_id) {
  ReleaseObjectLockRequestPB req;
  req.set_txn_id(master_request.txn_id());
  if (master_request.has_subtxn_id()) {
    req.set_subtxn_id(master_request.subtxn_id());
  }
  req.set_session_host_uuid(master_request.session_host_uuid());
  req.set_lease_epoch(master_request.lease_epoch());
  if (master_request.has_apply_after_hybrid_time()) {
    req.set_apply_after_hybrid_time(master_request.apply_after_hybrid_time());
  }
  if (master_request.has_propagated_hybrid_time()) {
    req.set_propagated_hybrid_time(master_request.propagated_hybrid_time());
  }
  req.set_request_id(request_id);
  return req;
}

template <class Resp>
void FillErrorIfRequired(const Status& status, Resp& resp) {
  if (!status.ok()) {
    if (status.IsIllegalState() && status.message().ToBuffer() == kNotTheMasterLeader) {
      resp.mutable_error()->set_code(MasterErrorPB::NOT_THE_LEADER);
    } else {
      resp.mutable_error()->set_code(MasterErrorPB::UNKNOWN_ERROR);
    }
    StatusToPB(status, resp.mutable_error()->mutable_status());
  }
}

std::string ReleaseObjectLockRequestToString(const ReleaseObjectLockRequestPB& pb) {
  std::stringstream ss;
  auto txn_id = FullyDecodeTransactionId(pb.txn_id());
  ss << "ReleaseObjectLockRequestPB{";
  ss << YB_EXPR_TO_STREAM_COMMA_SEPARATED(
      txn_id, pb.subtxn_id(), pb.session_host_uuid(), pb.lease_epoch(),
      pb.apply_after_hybrid_time(), pb.propagated_hybrid_time(), pb.request_id());
  ss << "}";
  return ss.str();
}

#ifndef NDEBUG

bool CompareReleaseRequestsIgnoringCatalogFields(
    const ReleaseObjectLockRequestPB& req1, const ReleaseObjectLockRequestPB& req2,
    std::string* diff_str = nullptr) {
  // Create a new MessageDifferencer for each call to avoid thread safety issues
  // with static instances
  google::protobuf::util::MessageDifferencer md;

  // Configure fields to ignore
  md.IgnoreField(req1.GetDescriptor()->FindFieldByName("db_catalog_version_data"));
  md.IgnoreField(req1.GetDescriptor()->FindFieldByName("db_catalog_inval_messages_data"));
  md.IgnoreField(req1.GetDescriptor()->FindFieldByName("populate_db_catalog_info"));

  if (diff_str) {
    md.ReportDifferencesToString(diff_str);
  }

  return md.Compare(req1, req2);
}

#endif

// Returns a ReleaseObjectLockRequestPB that is suitable for persisting to the SysCatalog.
// Request_id should have been set by the master when the request is added to the
// in-progress requests map. We do not want to persist db_catalog_* fields here as they
// tend to be large.
// UnlockObject will populate the db_catalog_* fields before constructing UpdateAllTServers.
ReleaseObjectLockRequestPB ReleaseRequestToPersist(const ReleaseObjectLockRequestPB& req) {
  ReleaseObjectLockRequestPB req_to_persist;
  req_to_persist.set_request_id(req.request_id());
  req_to_persist.set_txn_id(req.txn_id());
  if (req.has_subtxn_id()) {
    req_to_persist.set_subtxn_id(req.subtxn_id());
  }
  req_to_persist.set_session_host_uuid(req.session_host_uuid());
  req_to_persist.set_lease_epoch(req.lease_epoch());
  if (req.has_apply_after_hybrid_time()) {
    req_to_persist.set_apply_after_hybrid_time(req.apply_after_hybrid_time());
  }
  if (req.has_propagated_hybrid_time()) {
    req_to_persist.set_propagated_hybrid_time(req.propagated_hybrid_time());
  }
  DCHECK(!req.has_db_catalog_version_data());
  DCHECK(!req.has_db_catalog_inval_messages_data());
  req_to_persist.set_populate_db_catalog_info(req.populate_db_catalog_info());

#ifndef NDEBUG
  DCHECK(CompareReleaseRequestsIgnoringCatalogFields(req, req_to_persist))
      << "Did we add a new field to the ReleaseObjectLockRequestPB? It should either be ignored or "
         "copied here.";
#endif

  return req_to_persist;
}

}  // namespace

ObjectLockInfoManager::ObjectLockInfoManager(Master& master, CatalogManager& catalog_manager)
    : impl_(std::make_unique<ObjectLockInfoManager::Impl>(master, catalog_manager)) {}

ObjectLockInfoManager::~ObjectLockInfoManager() = default;

void ObjectLockInfoManager::Start() {
  impl_->Start();
}

void ObjectLockInfoManager::Shutdown() {
  impl_->Shutdown();
}

void ObjectLockInfoManager::LockObject(
    const AcquireObjectLocksGlobalRequestPB& req, AcquireObjectLocksGlobalResponsePB& resp,
    rpc::RpcContext rpc) {
  // Wrap rpc in a shared_ptr to allow capturing it in a copyable lambda
  auto rpc_ptr = std::make_shared<rpc::RpcContext>(std::move(rpc));
  impl_->LockObject(
      TserverRequestFor(req), rpc_ptr->GetClientDeadline(),
      [&resp, clock = impl_->clock(), rpc_ptr](const Status& s) mutable {
        resp.set_propagated_hybrid_time(clock->Now().ToUint64());
        FillErrorIfRequired(s, resp);
        rpc_ptr->RespondSuccess();
      });
}

void ObjectLockInfoManager::UnlockObject(
    const ReleaseObjectLocksGlobalRequestPB& req, ReleaseObjectLocksGlobalResponsePB& resp,
    rpc::RpcContext rpc) {
  auto request_id = impl_->next_request_id();
  auto tserver_req = TserverRequestFor(req, request_id);
  // wait_for_completion is only used to manually release locks through yb-admin.
  auto s =
      (req.wait_for_completion()
           ? impl_->UnlockObjectSync(req, std::move(tserver_req), rpc.GetClientDeadline())
           : impl_->UnlockObject(std::move(tserver_req)));
  WARN_NOT_OK(s, "Failed to unlock object");
  resp.set_propagated_hybrid_time(impl_->clock()->Now().ToUint64());
  FillErrorIfRequired(s, resp);
  rpc.RespondSuccess();
}

void ObjectLockInfoManager::ReleaseLocksForTxn(const TransactionId& txn_id) {
  impl_->UnlockObject(txn_id);
}

Status ObjectLockInfoManager::RefreshYsqlLease(
    const RefreshYsqlLeaseRequestPB& req, RefreshYsqlLeaseResponsePB& resp, rpc::RpcContext& rpc,
    const LeaderEpoch& epoch) {
  return impl_->RefreshYsqlLease(req, resp, rpc, epoch);
}

Status ObjectLockInfoManager::RelinquishYsqlLease(
    const RelinquishYsqlLeaseRequestPB& req, RelinquishYsqlLeaseResponsePB& resp,
    rpc::RpcContext& rpc, const LeaderEpoch& epoch) {
  return impl_->RelinquishYsqlLease(req, resp, rpc, epoch);
}

tserver::DdlLockEntriesPB ObjectLockInfoManager::ExportObjectLockInfo() {
  return impl_->ExportObjectLockInfo();
}

std::shared_ptr<CountDownLatch> ObjectLockInfoManager::ReleaseLocksHeldByExpiredLeaseEpoch(
    const std::string& tserver_uuid, uint64 max_lease_epoch_to_release,
    std::optional<LeaderEpoch> leader_epoch) {
  return impl_->ReleaseLocksHeldByExpiredLeaseEpoch(
      tserver_uuid, max_lease_epoch_to_release, leader_epoch);
}

std::unordered_map<std::string, SysObjectLockEntryPB::LeaseInfoPB>
ObjectLockInfoManager::GetLeaseInfos() const {
  return impl_->GetLeaseInfos();
}

void ObjectLockInfoManager::BootstrapLocksPostLoad() {
  impl_->BootstrapLocksPostLoad();
}

void ObjectLockInfoManager::UpdateObjectLocks(
    const std::string& tserver_uuid, std::shared_ptr<ObjectLockInfo> info) {
  impl_->UpdateObjectLocks(tserver_uuid, info);
}

void ObjectLockInfoManager::RelaunchInProgressRequests(
    const LeaderEpoch& leader_epoch, const std::string& tserver_uuid) {
  impl_->RelaunchInProgressRequests(leader_epoch, tserver_uuid);
}

void ObjectLockInfoManager::Clear() { impl_->Clear(); }

std::shared_ptr<tserver::TSLocalLockManager> ObjectLockInfoManager::ts_local_lock_manager() {
  return impl_->ts_local_lock_manager();
}

std::shared_ptr<tserver::TSLocalLockManager> ObjectLockInfoManager::TEST_ts_local_lock_manager() {
  return impl_->TEST_ts_local_lock_manager();
}

std::shared_ptr<ObjectLockInfo> ObjectLockInfoManager::Impl::GetOrCreateObjectLockInfo(
    const std::string& ts_uuid) {
  LockGuard lock(mutex_);
  auto it = object_lock_infos_map_.find(ts_uuid);
  if (it != object_lock_infos_map_.end()) {
    return it->second;
  } else {
    std::shared_ptr<ObjectLockInfo> info = std::make_shared<ObjectLockInfo>(ts_uuid);
    object_lock_infos_map_.insert({ts_uuid, info});
    return info;
  }
}

bool ObjectLockInfoManager::Impl::IsDdlVerificationInProgress(const TransactionId& txn) const {
  return catalog_manager_.HasDdlVerificationState(txn);
}

bool ObjectLockInfoManager::Impl::TabletServerHasLiveLease(const std::string& ts_uuid) const {
  LockGuard lock(mutex_);
  auto it = object_lock_infos_map_.find(ts_uuid);
  if (it == object_lock_infos_map_.end()) {
    return false;
  }
  return it->second->LockForRead()->pb.lease_info().live_lease();
}

void ObjectLockInfoManager::Impl::UpdateTxnHostSessionMap(
    const TransactionId& txn_id, const std::string& host_session_uuid, uint64_t lease_epoch) {
  LockGuard lock(mutex_);
  txn_host_info_map_[txn_id] = TxnHostInfo{host_session_uuid, lease_epoch};
}

void ObjectLockInfoManager::Impl::RemoveTxnFromHostSessionMap(const TransactionId& txn_id) {
  LockGuard lock(mutex_);
  txn_host_info_map_.erase(txn_id);
}

TSDescriptorVector ObjectLockInfoManager::Impl::GetAllTSDescriptorsWithALiveLease() const {
  auto descriptors = master_.ts_manager()->GetAllDescriptors();
  LockGuard lock(mutex_);
  std::erase_if(descriptors, [this](const auto& desc) NO_THREAD_SAFETY_ANALYSIS {
    auto it = object_lock_infos_map_.find(desc->id());
    if (it == object_lock_infos_map_.end()) {
      return true;
    }
    return !it->second->LockForRead()->pb.lease_info().live_lease();
  });
  return descriptors;
}

Status ObjectLockInfoManager::Impl::PersistRequest(
    LeaderEpoch epoch, const AcquireObjectLockRequestPB& req, const TransactionId& txn_id) {
  TRACE_FUNC();
  const auto& session_host_uuid = req.session_host_uuid();
  const auto lease_epoch = req.lease_epoch();
  UpdateTxnHostSessionMap(txn_id, session_host_uuid, lease_epoch);
  std::shared_ptr<ObjectLockInfo> object_lock_info = GetOrCreateObjectLockInfo(session_host_uuid);
  auto lock = object_lock_info->LockForWrite();
  auto& txns_map = (*lock.mutable_data()->pb.mutable_lease_epochs())[lease_epoch];
  auto& subtxns_map = (*txns_map.mutable_transactions())[txn_id.ToString()];
  subtxns_map.set_status_tablet(req.status_tablet());
  auto& object_locks_list = (*subtxns_map.mutable_subtxns())[req.subtxn_id()];
  for (const auto& object_lock : req.object_locks()) {
    object_locks_list.add_locks()->CopyFrom(object_lock);
  }
  RETURN_NOT_OK(catalog_manager_.sys_catalog()->Upsert(epoch, object_lock_info));
  lock.Commit();
  return Status::OK();
}

Status ObjectLockInfoManager::Impl::AddToInProgress(
    LeaderEpoch epoch, const ReleaseObjectLockRequestPB& req) {
  TRACE_FUNC();
  VLOG(3) << __PRETTY_FUNCTION__ << ReleaseObjectLockRequestToString(req);
  const auto& session_host_uuid = req.session_host_uuid();
  std::shared_ptr<ObjectLockInfo> object_lock_info = GetOrCreateObjectLockInfo(session_host_uuid);
  DCHECK(req.has_request_id()) << "Request id not set for release request: "
                               << req.ShortDebugString();
  auto lock = object_lock_info->LockForWrite();
  auto iter = lock->pb.in_progress_release_request().find(req.request_id());
  if (iter != lock->pb.in_progress_release_request().end()) {
    const auto& existing_req = iter->second;
    VLOG(0) << "Request already found among persisted in progress requests: "
            << ReleaseObjectLockRequestToString(existing_req);

#ifndef NDEBUG
    std::string diff_str;
    bool are_equal = CompareReleaseRequestsIgnoringCatalogFields(req, existing_req, &diff_str);
    LOG_IF(DFATAL, !are_equal) << "Failed to match "
                               << ReleaseObjectLockRequestToString(existing_req) << " with "
                               << ReleaseObjectLockRequestToString(req) << "\n difference: \n"
                               << diff_str;
#endif

    return Status::OK();
  }
  (*lock.mutable_data()->pb.mutable_in_progress_release_request())[req.request_id()] =
      ReleaseRequestToPersist(req);
  RETURN_NOT_OK(catalog_manager_.sys_catalog()->Upsert(epoch, object_lock_info));
  lock.Commit();
  return Status::OK();
}

Status ObjectLockInfoManager::Impl::PersistRequest(
    LeaderEpoch epoch, const ReleaseObjectLockRequestPB& req, const TransactionId& txn_id) {
  TRACE_FUNC();
  VLOG(3) << __PRETTY_FUNCTION__ << ReleaseObjectLockRequestToString(req);
  const auto& session_host_uuid = req.session_host_uuid();
  const auto lease_epoch = req.lease_epoch();
  const bool erase_txn = !req.subtxn_id();
  if (erase_txn) {
    RemoveTxnFromHostSessionMap(txn_id);
  }
  std::shared_ptr<ObjectLockInfo> object_lock_info = GetOrCreateObjectLockInfo(session_host_uuid);
  auto lock = object_lock_info->LockForWrite();
  auto& txns_map = (*lock.mutable_data()->pb.mutable_lease_epochs())[lease_epoch];
  if (erase_txn) {
    txns_map.mutable_transactions()->erase(txn_id.ToString());
  } else {
    auto& subtxns_map = (*txns_map.mutable_transactions())[txn_id.ToString()];
    subtxns_map.mutable_subtxns()->erase(req.subtxn_id());
  }
  if (txns_map.transactions().empty()) {
    lock.mutable_data()->pb.mutable_lease_epochs()->erase(lease_epoch);
  }
  lock.mutable_data()->pb.mutable_in_progress_release_request()->erase(req.request_id());
  RETURN_NOT_OK(catalog_manager_.sys_catalog()->Upsert(epoch, object_lock_info));
  lock.Commit();
  return Status::OK();
}

tserver::DdlLockEntriesPB ObjectLockInfoManager::Impl::ExportObjectLockInfo() {
  LockGuard lock(mutex_);
  return ExportObjectLockInfoUnlocked();
}

tserver::DdlLockEntriesPB ObjectLockInfoManager::Impl::ExportObjectLockInfoUnlocked() {
  VLOG(2) << __PRETTY_FUNCTION__;
  tserver::DdlLockEntriesPB entries;
  for (const auto& [host_uuid, per_host_entry] : object_lock_infos_map_) {
    auto l = per_host_entry->LockForRead();
    if (!l->pb.lease_info().has_lease_epoch() || !l->pb.lease_info().live_lease()) {
      continue;
    }
    auto txns_map_it = l->pb.lease_epochs().find(l->pb.lease_info().lease_epoch());
    if (txns_map_it == l->pb.lease_epochs().end()) {
      continue;
    }
    for (const auto& [txn_id_str, subtxns_map] : txns_map_it->second.transactions()) {
      auto txn_id = CHECK_RESULT(TransactionId::FromString(txn_id_str));
      for (const auto& [subtxn_id, object_locks_list] : subtxns_map.subtxns()) {
          auto* lock_entries_pb = entries.add_lock_entries();
          lock_entries_pb->set_session_host_uuid(host_uuid);
          lock_entries_pb->set_txn_id(txn_id.data(), txn_id.size());
          lock_entries_pb->set_status_tablet(subtxns_map.status_tablet());
          lock_entries_pb->set_subtxn_id(subtxn_id);
          lock_entries_pb->mutable_object_locks()->MergeFrom(object_locks_list.locks());
        }
    }
  }
  VLOG(3) << "Exported " << yb::ToString(entries);
  return entries;
}

/*
  Taking DDL locks at the master involves 2 steps:
  1. Acquire the locks locally - in the master's TSLocalLockManager.
     The TSLocalLockManager should only contain the DDL lock requests. Thus,
     if there is a conflict detected here, the request waits here before
     requesting the locks from the TServers.
  2. Once the locks are locally acquired -- this guarantees that the lock request
     does not conflict with any other DDL lock request already granted or in progress.
     Thus, we update the persisted state in the SysCatalog, to ensure that any
     new TServer joining the cluster will get the latest state of the locks.
  3. We then proceed to request the locks to be taken at all the registered TServers.
      This is done in parallel. If any of the TServers fail to acquire the lock, the
      request will fail. However the locks that were acquired locally will not be released
      or cleaned up as part of this RPC.
      a) The YBClient may choose to retry this, and run the request to completion (since the
      lock requests are idempotent), or it may bubble up the failure to the Pgclient.
      b) PgClient session is expected to release the lock(s) for any request that it has made,
      ** even if the request/rpc fails **, when the session/transaction finishes/aborts.
         This is easily done using the *release_all_locks* flag in the ReleaseObjectLockRequestPB.
      c) If the PgClient/Session itself fails, these locks will be released when the failure
      is detected and the session is cleaned up.


  The same steps are followed for releasing the locks.
   - But what if the release fails due to miscommunication with the TServer? The Async RPC
     framework will retry the RPC until the TServer loses its YSQL lease.
     If the TServer loses its lease, the TSLocalLockManager should clean up its state and
     bootstrap again from the master based on the persisted state in the SysCatalog.

  Amit: What if we are in a wedged state where
    - incoming RPCs from TServer -> Master (Heartbeats, etc) are fine.
     But,
    - the RPCs from Master -> TServer are failing.
    This is a rare case, but can happen.
    Possible solution:
    We could have a mechanism to detect this and ensure that the master does not
    referesh the YSQL lease for such a TServer which is not able to accept RPCs from the master.
    --- this is not implemented -- just brainstorming. TBD.

  Master failover-
    - The new master should be able to recreate the state of the TSLocalLockManager. However, it
    may not launch in-progress rpc's again? Or should it?
    - The RPCs can be retried by the YBClient/PgClient, or left for the release request to clean up.
*/
void ObjectLockInfoManager::Impl::LockObject(
    AcquireObjectLockRequestPB&& req, CoarseTimePoint deadline, StdStatusCallback&& callback) {
  VLOG(1) << __PRETTY_FUNCTION__ << req.ShortDebugString();

  // Do preparation work before creating UpdateAllTServers
  auto requestor_latest_lease_epoch = GetLeaseEpoch(req.session_host_uuid());

  // Decode transaction ID once
  auto txn_id_result = FullyDecodeTransactionId(req.txn_id());
  if (!txn_id_result.ok()) {
    callback(txn_id_result.status());
    return;
  }
  auto txn_id = std::move(*txn_id_result);

  // Validate the request
  auto validation_status = ValidateLockRequest(req, requestor_latest_lease_epoch);
  if (!validation_status.ok()) {
    callback(validation_status);
    return;
  }

  // Get leader lock and local lock manager
  LeaderEpoch epoch;
  std::shared_ptr<tserver::TSLocalLockManager> local_lock_manager;
  {
    SCOPED_LEADER_SHARED_LOCK(l, &catalog_manager_);
    auto leader_status = CheckLeaderLockStatus(l, std::nullopt);
    if (!leader_status.ok()) {
      callback(leader_status);
      return;
    }
    epoch = l.epoch();
    local_lock_manager = ts_local_lock_manager();
  }

  // Acquire locks locally first
  auto req_shared = std::make_shared<AcquireObjectLockRequestPB>(std::move(req));
  ASH_ENABLE_CONCURRENT_UPDATES();
  auto wait_state_ptr = ash::WaitStateInfo::CurrentWaitState();
  local_lock_manager->AcquireObjectLocksAsync(
      *req_shared, deadline,
      [req_shared, epoch, requestor_latest_lease_epoch, deadline, txn_id,
       wait_state_ptr, callback = std::move(callback), this](Status s) mutable {
        if (s.ok()) {
          s = PersistRequest(epoch, *req_shared, txn_id);
        }
        if (!s.ok()) {
          LOG(WARNING) << "Failed to acquire object locks locally: " << s;
          callback(s);
          return;
        }

        // If local acquisition succeeded, create and launch UpdateAllTServers
        ADOPT_WAIT_STATE(wait_state_ptr);
        auto lock_objects = std::make_shared<UpdateAllTServers<AcquireObjectLockRequestPB>>(
            master_, catalog_manager_, *this, std::move(*req_shared), std::move(callback),
            std::move(deadline), requestor_latest_lease_epoch, std::move(epoch), std::move(txn_id));
        WARN_NOT_OK(lock_objects->Launch(), "Failed to launch lock objects");
      });
}

void ObjectLockInfoManager::Impl::PopulateDbCatalogVersionCache(ReleaseObjectLockRequestPB& req) {
  if (!req.populate_db_catalog_info()) {
    return;
  }
  // TODO: Currently, we fetch and send catalog version of all dbs because the cache invalidation
  // logic on the tserver side expects a full report. Fix it and then optimize the below to only
  // send the catalog version of the db being operated on by the txn.
  DbOidToCatalogVersionMap versions;
  uint64_t fingerprint;
  auto s = catalog_manager_.GetYsqlAllDBCatalogVersions(
      FLAGS_enable_heartbeat_pg_catalog_versions_cache, &versions, &fingerprint);
  if (!s.ok()) {
    // In this case, we fallback to delayed cache invalidation on tserver-master heartbeat path.
    LOG(WARNING) << "Couldn't populate catalog version on exclusive lock release: " << s;
    return;
  }
  if (versions.empty()) {
    return;
  }
  auto* db_catalog_version_data = req.mutable_db_catalog_version_data();
  // The catalog version data may become out of date by the time these requests are
  // retried and processed by the TServer.
  db_catalog_version_data->set_ignore_catalog_version_staleness_check(true);
  for (const auto& it : versions) {
    auto* const catalog_version_pb = db_catalog_version_data->add_db_catalog_versions();
    catalog_version_pb->set_db_oid(it.first);
    catalog_version_pb->set_current_version(it.second.current_version);
    catalog_version_pb->set_last_breaking_version(it.second.last_breaking_version);
  }

  if (!FLAGS_ysql_yb_enable_invalidation_messages && !FLAGS_ysql_enable_db_catalog_version_mode) {
    return;
  }

  // Populate all known invalidation messages
  Result<DbOidVersionToMessageListMap> inval_messages =
    catalog_manager_.GetYsqlCatalogInvalationMessages(false /*use_cache*/);
  if (!inval_messages.ok()) {
    LOG(WARNING) << "Couldn't populate invalidation messages in lock release request " << s;
    return;
  }
  auto* const mutable_messages_data = req.mutable_db_catalog_inval_messages_data();
  for (auto& [db_oid_version, message_list] : *inval_messages) {
    auto* const db_inval_messages = mutable_messages_data->add_db_catalog_inval_messages();
    db_inval_messages->set_db_oid(db_oid_version.first);
    db_inval_messages->set_current_version(db_oid_version.second);
    if (message_list.has_value()) {
      db_inval_messages->set_message_list(std::move(*message_list));
    }
  }

}

Status ObjectLockInfoManager::Impl::UnlockObjectSync(
    const ReleaseObjectLocksGlobalRequestPB& req, ReleaseObjectLockRequestPB&& tserver_req,
    CoarseTimePoint deadline) {
  auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(req.txn_id()));
  // If the txn is scheduled for ddl verification, UnlockObject will be a no-op.
  // Let us first wait to make sure that it is not queued for ddl verification.
  RETURN_NOT_OK(Wait(
      [this, &txn_id] { return !IsDdlVerificationInProgress(txn_id); }, deadline,
      "Waiting for DDL verification to finish"));
  auto promise = std::make_shared<std::promise<Status>>();
  WARN_NOT_OK(
      UnlockObject(
        std::move(tserver_req), [promise](const Status& s) { promise->set_value(s); }),
      "Failed to unlock object");
  auto future = promise->get_future();
  return (
      future.wait_until(deadline) == std::future_status::ready
          ? future.get()
          : STATUS(TimedOut, "Timed out waiting for unlock object to finish"));
}

Status ObjectLockInfoManager::Impl::UnlockObject(
    ReleaseObjectLockRequestPB&& req, std::optional<StdStatusCallback>&& callback,
    std::optional<LeaderEpoch> leader_epoch) {
  VLOG(1) << __PRETTY_FUNCTION__ << ReleaseObjectLockRequestToString(req);

  // Helper lambda to call callback and return status
  auto callback_and_return = [&callback](const Status& status) -> Status {
    if (callback) {
      (*callback)(status);
    }
    return status;
  };

  if (req.session_host_uuid().empty()) {
    // session_host_uuid would be unset for release requests that are manually
    // initiated through yb-admin.
    auto s = MaybePopulateHostInfo(req);
    if (!s.ok()) {
      return callback_and_return(s);
    }
  }

  // Do preparation work before creating UpdateAllTServers
  auto txn_id_result = FullyDecodeTransactionId(req.txn_id());
  if (!txn_id_result.ok()) {
    return callback_and_return(txn_id_result.status());
  }
  auto txn_id = std::move(*txn_id_result);

  // Check if DDL verification is in progress
  if (IsDdlVerificationInProgress(txn_id)) {
    VLOG(1) << "Transaction " << txn_id << " is already scheduled for ddl verification. "
            << "Ignoring release request, as it will be released by the ddl verifier.";
    return callback_and_return(Status::OK());
  }

  VLOG(2) << "Processing release request for transaction " << txn_id << ": "
          << ReleaseObjectLockRequestToString(req);

  // Get leader lock and epoch if not provided
  LeaderEpoch epoch;
  if (leader_epoch.has_value()) {
    epoch = *leader_epoch;
  } else {
    SCOPED_LEADER_SHARED_LOCK(l, &catalog_manager_);
    auto leader_status = CheckLeaderLockStatus(l, std::nullopt);
    if (!leader_status.ok()) {
      return callback_and_return(leader_status);
    }
    epoch = l.epoch();
  }

  // Add to in-progress requests before launching RPCs
  auto add_status = AddToInProgress(epoch, req);
  if (!add_status.ok()) {
    return callback_and_return(add_status);
  }

  // Do this check after adding the request to in progress requests.
  if (PREDICT_FALSE(FLAGS_TEST_skip_launch_release_request)) {
    return callback_and_return(Status::OK());
  }

  // Populate DB catalog version cache right before creating UpdateAllTServers
  PopulateDbCatalogVersionCache(req);

  auto unlock_objects = std::make_shared<UpdateAllTServers<ReleaseObjectLockRequestPB>>(
      master_, catalog_manager_, *this, std::move(req), std::move(callback), std::move(epoch),
      std::move(txn_id));
  return unlock_objects->Launch();
}

Status ObjectLockInfoManager::Impl::MaybePopulateHostInfo(ReleaseObjectLockRequestPB& req) {
  auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(req.txn_id()));
  auto res = PopulateHostInfo(txn_id, req);
  if (!res.ok() && PREDICT_FALSE(FLAGS_TEST_allow_unknown_txn_release_request)) {
    LOG(INFO) << "Release is request for an unknown txn " << txn_id.ToString()
              << ". But, we will still proceed";
    return Status::OK();
  }
  return res;
}

Status ObjectLockInfoManager::Impl::PopulateHostInfo(
    const TransactionId& txn_id, ReleaseObjectLockRequestPB& req) {
  LockGuard lock(mutex_);
  auto it = txn_host_info_map_.find(txn_id);
  if (it == txn_host_info_map_.end()) {
    return STATUS(NotFound, "Transaction not found in host session map");
  }
  auto& txn_host_info = it->second;
  req.set_session_host_uuid(txn_host_info.host_session_uuid);
  req.set_lease_epoch(txn_host_info.lease_epoch);
  return Status::OK();
}

void ObjectLockInfoManager::Impl::UnlockObject(const TransactionId& txn_id) {
  ReleaseObjectLockRequestPB req;
  req.set_txn_id(txn_id.data(), txn_id.size());
  req.set_request_id(next_request_id());
  if (!PopulateHostInfo(txn_id, req).ok()) {
    return;
  }
  WARN_NOT_OK(UnlockObject(std::move(req)), "Failed to enqueue request for unlock object");
}

Status ObjectLockInfoManager::Impl::RefreshYsqlLease(
    const RefreshYsqlLeaseRequestPB& req, RefreshYsqlLeaseResponsePB& resp, rpc::RpcContext& rpc,
    const LeaderEpoch& epoch) {
  if (!req.has_local_request_send_time_ms()) {
    return STATUS(InvalidArgument, "Missing required local_request_send_time_ms");
  }
  auto master_ttl = GetAtomicFlag(&FLAGS_master_ysql_operation_lease_ttl_ms);
  auto buffer = GetAtomicFlag(&FLAGS_ysql_operation_lease_ttl_client_buffer_ms);
  CHECK_GT(master_ttl, buffer);
  resp.mutable_info()->set_lease_expiry_time_ms(
      req.local_request_send_time_ms() + master_ttl - buffer);
  // Sanity check that the tserver has already registered with the same instance_seqno.
  RETURN_NOT_OK(master_.ts_manager()->LookupTS(req.instance()));
  auto object_lock_info = GetOrCreateObjectLockInfo(req.instance().permanent_uuid());
  auto lock_variant = VERIFY_RESULT(object_lock_info->RefreshYsqlOperationLease(
      req.instance(), MonoDelta::FromMilliseconds(master_ttl)));
  if (auto* lease_info = std::get_if<SysObjectLockEntryPB::LeaseInfoPB>(&lock_variant)) {
    resp.mutable_info()->set_lease_epoch(lease_info->lease_epoch());
    if (!req.has_current_lease_epoch() || lease_info->lease_epoch() != req.current_lease_epoch()) {
      *resp.mutable_info()->mutable_ddl_lock_entries() = ExportObjectLockInfo();
      // From the master leader's perspective this is not a new lease. But the tserver may not be
      // aware it has received a new lease because it has not supplied its correct lease epoch.
      LOG(INFO) << Format(
          "TS $0 ($1) has provided $3 instead of its actual lease epoch $4 in its ysql op lease "
          "refresh request. Marking its ysql lease as new",
          req.instance().permanent_uuid(), req.instance().instance_seqno(),
          req.has_current_lease_epoch() ? std::to_string(req.current_lease_epoch()) : "<none>",
          lease_info->lease_epoch());
      resp.mutable_info()->set_new_lease(true);
    } else {
      resp.mutable_info()->set_new_lease(false);
    }
    return Status::OK();
  }
  auto* lockp = std::get_if<ObjectLockInfo::WriteLock>(&lock_variant);
  CHECK_NOTNULL(lockp);
  RETURN_NOT_OK(catalog_manager_.sys_catalog()->Upsert(epoch, object_lock_info));
  resp.mutable_info()->set_new_lease(true);
  resp.mutable_info()->set_lease_epoch(lockp->mutable_data()->pb.lease_info().lease_epoch());
  lockp->Commit();
  *resp.mutable_info()->mutable_ddl_lock_entries() = ExportObjectLockInfo();
  LOG(INFO) << Format(
      "Granting a new ysql op lease to TS $0 ($1). Lease epoch $2", req.instance().permanent_uuid(),
      req.instance().instance_seqno(), resp.info().lease_epoch());
  return Status::OK();
}

Status ObjectLockInfoManager::Impl::RelinquishYsqlLease(
    const RelinquishYsqlLeaseRequestPB& req, RelinquishYsqlLeaseResponsePB& resp,
    rpc::RpcContext& rpc, const LeaderEpoch& epoch) {
  auto object_lock_info = GetOrCreateObjectLockInfo(req.instance().permanent_uuid());
  auto l = object_lock_info->LockForWrite();
  auto& lease_info = l.data().pb.lease_info();
  if (lease_info.instance_seqno() > req.instance().instance_seqno()) {
    // This is a relinquish lease request from a prior instance of the tserver process. Reject.
    return STATUS_FORMAT(
        InvalidArgument,
        "Relinquish lease request for a replaced tserver instance. Instance in request $0, "
        "existing lease info $1",
        req.instance().DebugString(), lease_info.DebugString());
  }
  l.mutable_data()->pb.mutable_lease_info()->set_lease_relinquished(true);
  l.mutable_data()->pb.mutable_lease_info()->set_live_lease(false);
  RETURN_NOT_OK(catalog_manager_.sys_catalog()->Upsert(epoch, object_lock_info));
  l.Commit();
  return Status::OK();
}

std::shared_ptr<CountDownLatch> ObjectLockInfoManager::Impl::ReleaseLocksHeldByExpiredLeaseEpoch(
    const std::string& tserver_uuid, uint64 max_lease_epoch_to_release,
    std::optional<LeaderEpoch> leader_epoch) {
  std::vector<ReleaseObjectLockRequestPB> requests_per_txn;
  VLOG_WITH_FUNC(2) << "Releasing locks for " << tserver_uuid << " up to lease epoch "
                    << max_lease_epoch_to_release;
  {
    const auto& key = tserver_uuid;
    std::shared_ptr<ObjectLockInfo> object_lock_info = GetOrCreateObjectLockInfo(key);
    auto l = object_lock_info->LockForRead();
    for (const auto& [lease_epoch, txns_map] : l->pb.lease_epochs()) {
      if (lease_epoch > max_lease_epoch_to_release) {
        continue;
      }
      requests_per_txn.reserve(requests_per_txn.size() + txns_map.transactions().size());
      for (const auto& [txn_id_str, _] : txns_map.transactions()) {
        requests_per_txn.push_back(ReleaseObjectLockRequestPB());
        auto& request = requests_per_txn.back();
        request.set_session_host_uuid(tserver_uuid);
        auto txn_id = CHECK_RESULT(TransactionId::FromString(txn_id_str));
        request.set_txn_id(txn_id.data(), txn_id.size());
        request.set_lease_epoch(max_lease_epoch_to_release + 1);
        request.set_request_id(next_request_id());
        request.set_populate_db_catalog_info(requests_per_txn.size() == 1);
      }
    }
  }
  VLOG_WITH_FUNC(2) << "Unlocking " << requests_per_txn.size() << " txns";
  auto latch = std::make_shared<CountDownLatch>(requests_per_txn.size());
  for (auto& request : requests_per_txn) {
    auto txn_id = request.txn_id();
    auto session_host_uuid = request.session_host_uuid();
    WARN_NOT_OK(
        UnlockObject(
            std::move(request), [latch](const Status& s) { latch->CountDown(); }, leader_epoch),
        yb::Format("Failed to enqueue request for unlock object $0 $1", session_host_uuid, txn_id));
  }
  return latch;
}

std::unordered_map<std::string, SysObjectLockEntryPB::LeaseInfoPB>
ObjectLockInfoManager::Impl::GetLeaseInfos() const {
  LockGuard lock(mutex_);
  std::unordered_map<std::string, SysObjectLockEntryPB::LeaseInfoPB> result;
  for (const auto& [uuid, object_info] : object_lock_infos_map_) {
    result[uuid] = object_info->LockForRead()->pb.lease_info();
  }
  return result;
}

void ObjectLockInfoManager::Impl::BootstrapLocksPostLoad() {
  CHECK_OK(ts_local_lock_manager_during_catalog_loading()->BootstrapDdlObjectLocks(
      ExportObjectLockInfo()));
}

void ObjectLockInfoManager::Impl::UpdateObjectLocks(
    const std::string& tserver_uuid, std::shared_ptr<ObjectLockInfo> info) {
  // While holding locks concurrently, the established lock order is mutex_ -> LockFor(Read/Write).
  LockGuard lock(mutex_);
  auto [it, inserted] = object_lock_infos_map_.insert_or_assign(tserver_uuid, info);
  DCHECK(inserted) << "UpdateObjectLocks called for an existing tserver_uuid " << tserver_uuid;
  auto object_lock_info = it->second->LockForRead();
  for (const auto& [lease_epoch, txns_map] : object_lock_info.data().pb.lease_epochs()) {
    for (const auto& [txn_id_str, _] : txns_map.transactions()) {
      auto txn_id_res = TransactionId::FromString(txn_id_str);
      if (txn_id_res.ok()) {
        txn_host_info_map_[*txn_id_res] = TxnHostInfo{tserver_uuid, lease_epoch};
      } else {
        LOG(DFATAL) << "Unable to decode transaction id from " << txn_id_str << ": "
                    << txn_id_res.status();
      }
    }
  }

  uint64_t max_seen_request_id = 0;
  for (const auto& [_, request] : object_lock_info.data().pb.in_progress_release_request()) {
    max_seen_request_id = std::max(max_seen_request_id, request.request_id());
  }
  // It is ok if we reuse request ids from requests that are completed.
  next_request_id_ = std::max(next_request_id_.load(), max_seen_request_id + 1);
}

void ObjectLockInfoManager::Impl::RelaunchInProgressRequests(
    const LeaderEpoch& leader_epoch, const std::string& tserver_uuid) {
  std::vector<ReleaseObjectLockRequestPB> requests;
  const auto& key = tserver_uuid;
  {
    std::shared_ptr<ObjectLockInfo> object_lock_info = GetOrCreateObjectLockInfo(key);
    auto l = object_lock_info->LockForRead();
    for (const auto& [_, request] : l.data().pb.in_progress_release_request()) {
      requests.push_back(request);
    }
  }
  VLOG(1) << __func__ << " for " << tserver_uuid << " " << requests.size() << " requests";
  for (auto& request : requests) {
    WARN_NOT_OK(
        UnlockObject(std::move(request), std::nullopt /* callback */, leader_epoch),
        "Failed to enqueue request for unlock object");
  }
}

void ObjectLockInfoManager::Impl::Clear() {
  catalog_manager_.AssertLeaderLockAcquiredForWriting();
  LockGuard lock(mutex_);
  object_lock_infos_map_.clear();
  if (local_lock_manager_) {
    local_lock_manager_->Shutdown();
  }
  local_lock_manager_.reset(new tserver::TSLocalLockManager(
      clock_, master_.tablet_server(), master_, lock_manager_thread_pool_.get(),
      master_.metric_entity()));
  local_lock_manager_->Start(waiting_txn_registry_.get());
}

std::optional<uint64_t> ObjectLockInfoManager::Impl::GetLeaseEpoch(const std::string& ts_uuid) {
  LockGuard lock(mutex_);
  auto it = object_lock_infos_map_.find(ts_uuid);
  if (it == object_lock_infos_map_.end()) {
    return std::nullopt;
  }
  return it->second->LockForRead()->pb.lease_info().lease_epoch();
}

void ObjectLockInfoManager::Impl::CleanupExpiredLeaseEpochs() {
  auto current_time = MonoTime::Now();
  LeaderEpoch leader_epoch;
  {
    SCOPED_LEADER_SHARED_LOCK(l, &catalog_manager_);
    if (!l.IsInitializedAndIsLeader()) {
      return;
    }
    leader_epoch = l.epoch();
  }
  std::vector<std::shared_ptr<ObjectLockInfo>> expiring_leases;
  std::vector<std::shared_ptr<ObjectLockInfo>>
      infos_with_expired_lease_epochs;
  {
    LockGuard lock(mutex_);
    for (const auto& [_, object_info] : object_lock_infos_map_) {
      auto object_info_lock = object_info->LockForRead();
      if (object_info_lock->pb.lease_info().live_lease() &&
          current_time > object_info->ysql_lease_deadline()) {
        expiring_leases.push_back(object_info);
      } else {
        for (const auto& [lease_epoch, _] : object_info_lock->pb.lease_epochs()) {
          if (lease_epoch != object_info_lock->pb.lease_info().lease_epoch() ||
              !object_info_lock->pb.lease_info().live_lease()) {
            infos_with_expired_lease_epochs.push_back(object_info);
            break;
          }
        }
      }
    }
  }
  if (expiring_leases.empty() && infos_with_expired_lease_epochs.empty()) {
    return;
  }
  // Loop again through the expiring leases to confirm mutations by acquiring the write locks.
  std::vector<ObjectLockInfo*> object_infos_to_write;
  std::vector<ObjectLockInfo::WriteLock> write_locks;
  for (const auto& object_info : expiring_leases) {
    auto object_info_lock = object_info->LockForWrite();
    if (object_info_lock->pb.lease_info().live_lease() &&
        current_time > object_info->ysql_lease_deadline()) {
      LOG(INFO) << Format(
          "Tserver $0, instance seqno $1 with ysql lease epoch $2 has just lost its lease",
          object_info->id(), object_info_lock->pb.lease_info().instance_seqno(),
          object_info_lock->pb.lease_info().lease_epoch());
      object_info_lock.mutable_data()->pb.mutable_lease_info()->set_live_lease(false);
      object_infos_to_write.push_back(object_info.get());
      if (object_info_lock->pb.lease_epochs_size() > 0) {
        infos_with_expired_lease_epochs.push_back(object_info);
      }
      write_locks.push_back(std::move(object_info_lock));
    }
  }
  auto write_status = catalog_manager_.sys_catalog()->Upsert(leader_epoch, object_infos_to_write);
  if (!write_status.ok()) {
    LOG(WARNING) << Format(
        "Object lock cleanup task failed to mark leases for tservers $0 as expired, got error: $1",
        yb::ToString(std::ranges::views::transform(
            object_infos_to_write, [](const auto& object_lock) { return object_lock->id(); })),
        write_status);
    return;
  }
  for (auto& lock : write_locks) {
    lock.Commit();
  }
  for (const auto& object_info : infos_with_expired_lease_epochs) {
    auto it = expired_lease_epoch_cleanup_tasks_.find(object_info->id());
    if (it != expired_lease_epoch_cleanup_tasks_.end() && it->second->count() > 0) {
      // Already a task in progress to clean this up.
      continue;
    }
    std::optional<uint64_t> max_lease_epoch_to_clean;
    uint64_t min_lease_epoch_to_keep;
    {
      auto object_info_lock = object_info->LockForRead();
      min_lease_epoch_to_keep = object_info_lock->pb.lease_info().live_lease()
                                    ? object_info_lock->pb.lease_info().lease_epoch()
                                    : std::numeric_limits<uint64_t>::max();
      for (const auto& [lease_epoch, _] : object_info_lock->pb.lease_epochs()) {
        if (lease_epoch >= min_lease_epoch_to_keep) {
          continue;
        }
        max_lease_epoch_to_clean = max_lease_epoch_to_clean
                                       ? std::max(*max_lease_epoch_to_clean, lease_epoch)
                                       : lease_epoch;
      }
      if (!max_lease_epoch_to_clean) {
        continue;
      }
    }
    expired_lease_epoch_cleanup_tasks_[object_info->id()] = ReleaseLocksHeldByExpiredLeaseEpoch(
        object_info->id(), *max_lease_epoch_to_clean, leader_epoch);
  }
  // todo(zdrudi): GC tasks which have finished. maybe do that first in this function.
}

template <class Req>
UpdateAllTServers<Req>::UpdateAllTServers(
    Master& master, CatalogManager& catalog_manager, ObjectLockInfoManager::Impl& olm, Req&& req,
    StdStatusCallback&& callback, CoarseTimePoint deadline,
    std::optional<uint64_t> requestor_latest_lease_epoch, LeaderEpoch epoch, TransactionId txn_id)
    : master_(master),
      catalog_manager_(catalog_manager),
      object_lock_info_manager_(olm),
      req_(std::move(req)),
      txn_id_(std::move(txn_id)),
      epoch_(std::move(epoch)),
      callback_(std::move(callback)),
      requestor_latest_lease_epoch_(requestor_latest_lease_epoch),
      deadline_(deadline),
      trace_(Trace::CurrentTrace()),
      wait_state_(ash::WaitStateInfo::CurrentWaitState()) {
  VLOG(3) << __PRETTY_FUNCTION__;
}

template <class Req>
UpdateAllTServers<Req>::UpdateAllTServers(
    Master& master, CatalogManager& catalog_manager, ObjectLockInfoManager::Impl& olm, Req&& req,
    std::optional<StdStatusCallback>&& callback, LeaderEpoch leader_epoch, TransactionId txn_id)
    : master_(master),
      catalog_manager_(catalog_manager),
      object_lock_info_manager_(olm),
      req_(std::move(req)),
      txn_id_(std::move(txn_id)),
      epoch_(std::move(leader_epoch)),
      callback_(std::move(callback)),
      deadline_(CoarseMonoClock::Now() + kTserverRpcsTimeoutDefaultSecs),
      trace_(Trace::CurrentTrace()),
      wait_state_(ash::WaitStateInfo::CurrentWaitState()) {
  VLOG(3) << __PRETTY_FUNCTION__;
}

template <class Req>
void UpdateAllTServers<Req>::Done(size_t i, const Status& s) {
  if (s.ok() || object_lock_info_manager_.TabletServerHasLiveLease(ts_descriptors_[i]->id())) {
    statuses_[i] = s;
  } else {
    VLOG(3) << Format(
        "Ignoring status for tserver $0 because it does not have a live lease",
        ts_descriptors_[i]->id());
    // If the tablet server does not have a live lease then ignore it.
    statuses_[i] = Status::OK();
  }
  TRACE_TO(
      trace(), "Done $0 ($1) : $2", i, ts_descriptors_[i]->permanent_uuid(),
      statuses_[i].ToString());
  // TODO: There is a potential here for early return if s is not OK.
  if (--ts_pending_ == 0) {
    CheckForDone();
  }
}

template <>
std::shared_ptr<RetrySpecificTSRpcTask>
UpdateAllTServers<AcquireObjectLockRequestPB>::TServerTaskFor(
    const TabletServerId& ts_uuid, StdStatusCallback&& callback) {
  return std::make_shared<
      master::UpdateTServer<AcquireObjectLockRequestPB, AcquireObjectLockResponsePB>>(
      master_, catalog_manager_.AsyncTaskPool(), ts_uuid, this->shared_from_this(),
      std::move(callback));
}

template <>
std::shared_ptr<RetrySpecificTSRpcTask>
UpdateAllTServers<ReleaseObjectLockRequestPB>::TServerTaskFor(
    const TabletServerId& ts_uuid, StdStatusCallback&& callback) {
  return std::make_shared<
      master::UpdateTServer<ReleaseObjectLockRequestPB, ReleaseObjectLockResponsePB>>(
      master_, catalog_manager_.AsyncTaskPool(), ts_uuid, this->shared_from_this(),
      std::move(callback));
}

template <class Req>
std::string UpdateAllTServers<Req>::LogPrefix() const {
  return Format(
      "$0 txn: $1 subtxn_id: $2 ",
      (kIsReleaseRequest<Req> ? "ReleaseObjectLock" : "AcquireObjectLock"), txn_id_.ToString(),
      (req_.has_subtxn_id() ? yb::ToString(req_.subtxn_id()) : "<none>"));
}

template <class Req>
void UpdateAllTServers<Req>::LaunchRpcs() {
  // todo(zdrudi): special case for 0 tservers with a live lease. This doesn't work.
  ts_descriptors_ = object_lock_info_manager_.GetAllTSDescriptorsWithALiveLease();
  statuses_ = std::vector<Status>{ts_descriptors_.size(), STATUS(Uninitialized, "")};
  LaunchRpcsFrom(0);
}

template <class Req>
Status UpdateAllTServers<Req>::Launch() {
  TRACE_FUNC();
  LaunchRpcs();
  return Status::OK();
}

template <class Req>
void UpdateAllTServers<Req>::LaunchRpcsFrom(size_t start_idx) {
  TRACE("Launching for $0 TServers from $1", ts_descriptors_.size(), start_idx);
  ts_pending_ = ts_descriptors_.size() - start_idx;
  VLOG(1) << __func__ << " launching for " << ts_pending_ << " tservers.";
  for (size_t i = start_idx; i < ts_descriptors_.size(); ++i) {
    auto ts_uuid = ts_descriptors_[i]->permanent_uuid();
    VLOG(1) << "Launching for " << ts_uuid;
    auto task = TServerTaskFor(
        ts_uuid,
        std::bind(&UpdateAllTServers<Req>::Done, this->shared_from_this(), i, _1));
    auto s =  catalog_manager_.ScheduleTask(task);
    if (!s.ok()) {
      Done(i,
           s.CloneAndPrepend(Format(
               "Failed to schedule request to UpdateTServer to $0 for $1", ts_uuid,
               request().DebugString())));
    }
  }
}

template <class Req>
void UpdateAllTServers<Req>::DoCallbackAndRespond(const Status& s) {
  TRACE("$0: $1 $2", __func__, (kIsReleaseRequest<Req> ? "Release" : "Acquire"), s.ToString());
  VLOG_WITH_FUNC(2) << (kIsReleaseRequest<Req> ? "Release" : "Acquire") << " " << s.ToString();
  WARN_NOT_OK(
      s, yb::Format(
             "$0Failed.$1", LogPrefix(),
             kIsReleaseRequest<Req>
                 ? " Release request was persisted, and will be retried upon master failover."
                 : ""));
  if (callback_) {
    (*callback_)(s);
  }
}

template <class Req>
void UpdateAllTServers<Req>::CheckForDone() {
  for (size_t i = 0; i < statuses_.size(); ++i) {
    const auto& status = statuses_[i];
    if (!status.ok()) {
      LOG(WARNING) << Format(
                          "Error from tserver $0 for forwarded $1 object lock request",
                          ts_descriptors_[i]->permanent_uuid(),
                          kIsReleaseRequest<Req> ? "release" : "acquire")
                   << status;
      DoCallbackAndRespond(status);
      return;
    }
  }
  if (RelaunchIfNecessary()) {
    return;
  }
  DoneAll();
}

template <class Req>
Status UpdateAllTServers<Req>::DoPersistRequestUnlocked(const ScopedLeaderSharedLock& l) {
  // Persist the request.
  RETURN_NOT_OK(CheckLeaderLockStatus(l, epoch_));
  auto s = object_lock_info_manager_.PersistRequest(epoch_, req_, txn_id_);
  if (!s.ok()) {
    LOG(WARNING) << "Failed to update object lock " << s;
    return s.CloneAndReplaceCode(Status::kRemoteError);
  }
  return Status::OK();
}

template <class Req>
Status UpdateAllTServers<Req>::DoPersistRequest() {
  SCOPED_LEADER_SHARED_LOCK(l, &catalog_manager_);
  return DoPersistRequestUnlocked(l);
}

template <>
Status UpdateAllTServers<AcquireObjectLockRequestPB>::AfterRpcs() {
  TRACE_FUNC();
  return Status::OK();
}

template <>
Status UpdateAllTServers<ReleaseObjectLockRequestPB>::AfterRpcs() {
  TRACE_FUNC();
  VLOG_WITH_FUNC(2);
  SCOPED_LEADER_SHARED_LOCK(l, &catalog_manager_);
  RETURN_NOT_OK(DoPersistRequestUnlocked(l));
  // Update Local State.
  auto local_lock_manager = object_lock_info_manager_.ts_local_lock_manager();
  auto res = local_lock_manager->ReleaseObjectLocks(req_, GetClientDeadline());
  if (!res.ok()) {
    LOG(WARNING) << "Failed to release object lock locally." << res.status();
    return res.status().CloneAndReplaceCode(Status::kRemoteError);
  }
  return Status::OK();
}

template <class Req>
void UpdateAllTServers<Req>::DoneAll() {
  ADOPT_TRACE(trace());
  TRACE_FUNC();
  DoCallbackAndRespond(AfterRpcs());
}

template <>
bool UpdateAllTServers<AcquireObjectLockRequestPB>::RelaunchIfNecessary() {
  return false;
}

template <>
bool UpdateAllTServers<ReleaseObjectLockRequestPB>::RelaunchIfNecessary() {
  TRACE_TO(trace(), "Relaunching");
  auto old_size = ts_descriptors_.size();
  auto current_ts_descriptors = object_lock_info_manager_.GetAllTSDescriptorsWithALiveLease();
  for (const auto& ts_descriptor : current_ts_descriptors) {
    if (std::find(ts_descriptors_.begin(), ts_descriptors_.end(), ts_descriptor) ==
        ts_descriptors_.end()) {
      ts_descriptors_.push_back(ts_descriptor);
      statuses_.push_back(STATUS(Uninitialized, ""));
    }
  }
  if (ts_descriptors_.size() == old_size) {
    return false;
  }

  VLOG(1) << "New TServers were added. Relaunching.";
  LaunchRpcsFrom(old_size);
  return true;
}

template <class Req, class Resp>
UpdateTServer<Req, Resp>::UpdateTServer(
    Master& master, ThreadPool* callback_pool, const TabletServerId& ts_uuid,
    std::shared_ptr<UpdateAll<Req>> shared_all_tservers, StdStatusCallback&& callback)
    : RetrySpecificTSRpcTask(&master, callback_pool, ts_uuid, /* async_task_throttler */ nullptr),
      callback_(std::move(callback)),
      shared_all_tservers_(shared_all_tservers) {
  deadline_.MakeAtMost(ToSteady(shared_all_tservers_->GetClientDeadline()));
}

template <>
bool UpdateTServer<AcquireObjectLockRequestPB, AcquireObjectLockResponsePB>::SendRequest(
    int attempt) {
  VLOG_WITH_PREFIX(3) << __func__ << " attempt " << attempt;
  ADOPT_WAIT_STATE(shared_all_tservers_->wait_state());
  ts_proxy_->AcquireObjectLocksAsync(request(), &resp_, &rpc_, BindRpcCallback());
  return true;
}

template <>
bool UpdateTServer<ReleaseObjectLockRequestPB, ReleaseObjectLockResponsePB>::SendRequest(
    int attempt) {
  VLOG_WITH_PREFIX(3) << __func__ << " attempt " << attempt;
  ADOPT_WAIT_STATE(shared_all_tservers_->wait_state());
  ts_proxy_->ReleaseObjectLocksAsync(request(), &resp_, &rpc_, BindRpcCallback());
  return true;
}

template <class Req, class Resp>
void UpdateTServer<Req, Resp>::HandleResponse(int attempt) {
  VLOG_WITH_PREFIX(3) << __func__ << " response is " << yb::ToString(resp_)
                      << " for tserver: " << permanent_uuid_;
  if (!resp_.has_error()) {
    TransitionToCompleteState();
    return;
  }
  auto status = StatusFromPB(resp_.error().status());
  if (!shared_all_tservers_->TabletServerHasLiveLease(permanent_uuid_)) {
    LOG_WITH_PREFIX(WARNING) << Format(
        "TServer no longer has a live lease. Ignoring this tserver for object lock $0 request, "
        "status: $1",
        kIsReleaseRequest<Req> ? "release" : "acquire", status);
    TransitionToCompleteState();
    return;
  }
  // Upon ysql lease changes, the object lock manager fails outstanding lock requests with
  // ShutdownInProgress. Can retry the request to prevent exclusive lock requests from failing
  // due to ysql lease membership changes w.r.t master leader's view.
  if (status.IsShutdownInProgress()) {
    return;
  }
  // The tserver either doesn't have a live lease or it doesn't have the expected lease epoch.
  // The tserver possibly crashed and restarted, or an acquire lease request raced with this lock
  // request. In either case we retry.
  if (resp_.error().code() == TabletServerErrorPB::INVALID_YSQL_LEASE) {
    return;
  }
  TransitionToFailedState(server::MonitoredTaskState::kRunning, status);
}

template <class Req, class Resp>
void UpdateTServer<Req, Resp>::Finished(const Status& status) {
  VLOG_WITH_PREFIX(3) << __func__ << " (" << status << ")";
  callback_(status);
}

template <class Req>
Req AddRecipientLeaseEpoch(const Req& req, uint64_t recipient_lease_epoch) {
  auto decorated_req = req;
  decorated_req.set_recipient_lease_epoch(recipient_lease_epoch);
  return decorated_req;
}

template <class Req, class Resp>
Req UpdateTServer<Req, Resp>::request() const {
  auto recipient_lease_epoch = shared_all_tservers_->GetLeaseEpoch(permanent_uuid_);
  if (recipient_lease_epoch.has_value()) {
    return AddRecipientLeaseEpoch(shared_all_tservers_->request(), *recipient_lease_epoch);
  }
  return shared_all_tservers_->request();
}

template <class Req, class Resp>
bool UpdateTServer<Req, Resp>::RetryTaskAfterRPCFailure(const Status& status) {
  if (!shared_all_tservers_->TabletServerHasLiveLease(permanent_uuid_)) {
    LOG_WITH_PREFIX(WARNING) << "TServer no longer has a live lease. "
                             << "Ignoring this tserver, rpc status: " << rpc_.status();
    return false;
  }
  return true;
}

}  // namespace master
}  // namespace yb
