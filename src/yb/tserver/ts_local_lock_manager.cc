//
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
//

#include "yb/tserver/ts_local_lock_manager.h"

#include "yb/client/client.h"

#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/object_lock_manager.h"
#include "yb/docdb/object_lock_shared_state_manager.h"

#include "yb/master/master_ddl.pb.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/poller.h"

#include "yb/server/server_base.h"

#include "yb/tserver/service_util.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/scope_exit.h"
#include "yb/util/trace.h"
#include "yb/util/unique_lock.h"

using namespace std::literals;
DECLARE_bool(dump_lock_keys);
DECLARE_bool(ysql_yb_enable_invalidation_messages);

DEFINE_NON_RUNTIME_int64(olm_poll_interval_ms, 100,
    "Poll interval for Object lock Manager. Waiting requests that are unblocked by other release "
    "requests are independent of this interval since the release schedules unblocking of potential "
    "waiters. Yet this might help release timedout requests soon and also avoid probable issues "
    "with the signaling mechanism if any.");

DEPRECATE_FLAG(int32, ysql_max_retries_for_release_object_lock_requests, "07_2025");

DEFINE_test_flag(bool, block_acquires_to_simulate_out_of_order, false,
    "Will cause the acquire objects codepath to block indefinitely, until "
    " the gflag TEST_release_blocked_acquires is set to true. ");
DEFINE_test_flag(bool, release_blocked_acquires_to_simulate_out_of_order, false,
    "Will cause the blocked acquire objects handling to unblock");

DECLARE_int32(tserver_yb_client_default_timeout_ms);
DECLARE_uint64(refresh_waiter_timeout_ms);

namespace yb::tserver {

YB_STRONGLY_TYPED_BOOL(WaitForBootstrap);

namespace {

void ReleaseWithRetriesGlobalNow(
    yb::client::YBClient& client, std::weak_ptr<TSLocalLockManager> lock_manager_weak,
    const std::shared_ptr<master::ReleaseObjectLocksGlobalRequestPB>& release_req,
    int attempt = 1) {
  // Practically speaking, if the TServer cannot reach the master/leader for the ysql lease
  // interval it can safely give up. The Master is responsible for cleaning up the locks for any
  // tserver that loses its lease. We have additional retries just to be safe. Also the timeout
  // used here defaults to 60s, which is much larger than the default lease interval of 28sec.
  // Even if the lease interval were changed to something much larger, it is ok because the
  // release request will be retried as long as the ts local lock manager is live.
  auto deadline =
      MonoTime::Now() + MonoDelta::FromMilliseconds(FLAGS_tserver_yb_client_default_timeout_ms);
  auto ptr = lock_manager_weak.lock();
  if (!ptr) {
    LOG(INFO) << "Session is no longer valid. Most likely lease epoch "
              << release_req->lease_epoch() << " is not valid. Will not retry "
              << " Release request " << (VLOG_IS_ON(2) ? release_req->ShortDebugString() : "");
    return;
  } else if (ptr->IsShutdownInProgress()) {
    LOG(INFO) << "Shutdown in progress. Will not retry "
              << " Release request " << (VLOG_IS_ON(2) ? release_req->ShortDebugString() : "");
    return;
  }
  client.ReleaseObjectLocksGlobalAsync(
      *release_req,
      [&client, lock_manager_weak, release_req, attempt](const Status& s) {
        if (s.ok()) {
          VLOG(1) << "Release global request done. "
                  << (VLOG_IS_ON(2) ? release_req->ShortDebugString() : "");
          return;
        } else {
          VLOG_WITH_FUNC(1) << "Release global locks failed. Will retry."
                            << " attempt : " << attempt << " status " << s
                            << (VLOG_IS_ON(2) ? release_req->ShortDebugString() : "");
          ReleaseWithRetriesGlobalNow(client, lock_manager_weak, release_req, attempt + 1);
        }
      },
      ToCoarse(deadline));
}

void ReleaseObjectLocksForLostMessages(
    std::reference_wrapper<yb::client::YBClient> client,
    std::weak_ptr<TSLocalLockManager> lock_manager_weak,
    const std::shared_ptr<master::ReleaseObjectLocksGlobalRequestPB>& release_req,
    const Status& status) {
  if (!status.ok()) {
    LOG_WITH_FUNC(ERROR) << "Aborting Release request"
                         << (VLOG_IS_ON(1) ? release_req->ShortDebugString() : "") << " due to "
                         << status;
    return;
  }
  VLOG_WITH_FUNC(1) << " for : " << release_req->ShortDebugString();
  ReleaseWithRetriesGlobalNow(client.get(), lock_manager_weak, release_req);
}

}  // namespace

class TSLocalLockManager::Impl {
 public:
  Impl(
      const server::ClockPtr& clock, TabletServerIf* tablet_server,
      server::RpcServerBase& messenger_server, ThreadPool* thread_pool,
      const MetricEntityPtr& metric_entity, std::shared_ptr<ObjectLockTracker> lock_tracker,
      docdb::ObjectLockSharedStateManager* shared_manager)
      : clock_(clock), server_(tablet_server), messenger_base_(messenger_server),
        object_lock_manager_(thread_pool, messenger_server, metric_entity, shared_manager),
        poller_("TSLocalLockManager", std::bind(&Impl::Poll, this)) {
    if (lock_tracker) {
      lock_tracker_ = std::move(lock_tracker);
    } else {
      lock_tracker_ = std::make_shared<ObjectLockTracker>();
    }
  }

  ~Impl() = default;

  void TrackDeadlineForGlobalAcquire(
      const TransactionId& txn_id, const SubTransactionId& subtxn_id,
      CoarseTimePoint apply_after_ht) {
    std::lock_guard l(mutex_);
    max_deadline_across_acquires_[txn_id][subtxn_id] =
        std::max(apply_after_ht, max_deadline_across_acquires_[txn_id][subtxn_id]);
  }

  std::optional<CoarseTimePoint> MaxDeadlineForTxn(
      const TransactionId& txn_id, std::optional<SubTransactionId> subtxn_id) {
    std::lock_guard l(mutex_);
    auto it = max_deadline_across_acquires_.find(txn_id);
    if (it == max_deadline_across_acquires_.end()) {
      return std::nullopt;
    }
    if (subtxn_id.has_value()) {
      // If subtxn_id is provided, we release only that specific subtransaction.
      auto sub_it = it->second.find(*subtxn_id);
      if (sub_it == it->second.end()) {
        return std::nullopt;
      }
      return sub_it->second;
    }
    // If subtxn_id is not provided, we release all subtransactions for this txn.
    // This is useful when we want to release all locks for a txn that has lost messages.
    auto& max_deadline_map = it->second;
    // There should be at least one subtransaction, as we expect CleanupMaxDeadlineForTxn to
    // delete the entry if there are no subtransactions left.
    DCHECK(!max_deadline_map.empty())
        << "No subtransactions found for txn_id: " << txn_id.ToString();
    // Get the max max_deadline for all subtxns.
    auto max_deadline = max_deadline_map.begin()->second;
    for (auto& [_, subtxn_max_deadline] : max_deadline_map) {
      if (subtxn_max_deadline > max_deadline) {
        max_deadline = subtxn_max_deadline;
      }
    }
    return max_deadline;
  }

  void CleanupMaxDeadlineForTxn(
      const TransactionId& txn_id, std::optional<SubTransactionId> subtxn_id) {
    std::lock_guard l(mutex_);
    auto it = max_deadline_across_acquires_.find(txn_id);
    if (it == max_deadline_across_acquires_.end()) {
      return;
    }

    if (!subtxn_id.has_value()) {
      // If subtxn_id is not provided, we release all subtransactions for this txn.
      // This is useful when we want to release all locks for a txn that has lost messages.
      max_deadline_across_acquires_.erase(it);
      return;
    }

    auto& subtxn_map = it->second;
    subtxn_map.erase(*subtxn_id);
    if (subtxn_map.empty()) {
      max_deadline_across_acquires_.erase(it);
    }
  }

  void ScheduleReleaseForLostMessages(
      yb::client::YBClient& client, std::weak_ptr<TSLocalLockManager> lock_manager_weak,
      const TransactionId& txn_id, std::optional<SubTransactionId> subtxn_id,
      const std::shared_ptr<master::ReleaseObjectLocksGlobalRequestPB>& release_req) {
    auto apply_after_ht = MaxDeadlineForTxn(txn_id, subtxn_id);
    if (!apply_after_ht) {
      return;
    }
    messenger_base_.messenger()->scheduler().Schedule(
        std::bind(
            ReleaseObjectLocksForLostMessages, std::reference_wrapper<client::YBClient>(client),
            lock_manager_weak, release_req, std::placeholders::_1),
        ToSteady(*apply_after_ht));
    CleanupMaxDeadlineForTxn(txn_id, subtxn_id);
  }

  Status CheckRequestForDeadline(const tserver::AcquireObjectLockRequestPB& req) {
    TRACE_FUNC();
    server::UpdateClock(req, clock_.get());
    auto now_ht = clock_->Now();
    if (req.has_ignore_after_hybrid_time() && req.ignore_after_hybrid_time() <= now_ht.ToUint64()) {
      VLOG(2) << "Ignoring request which is past its deadline: " << req.DebugString() << " now_ht "
              << now_ht << " now_ht.ToUint64() " << now_ht.ToUint64();
      return STATUS_FORMAT(IllegalState, "Ignoring request which is past its deadline.");
    }
    auto max_seen_lease_epoch = GetMaxSeenLeaseEpoch(req.session_host_uuid());
    if (req.lease_epoch() < max_seen_lease_epoch) {
      TRACE("Requestor has an old lease epoch, rejecting the request.");
      return STATUS_FORMAT(
          InvalidArgument,
          "Requestor has a lease epoch of $0 but the latest valid lease epoch for "
          "this tserver is $1",
          req.lease_epoch(), max_seen_lease_epoch);
    }
    return Status::OK();
  }

  Status CheckShutdown() const {
    return shutdown_
        ? STATUS_FORMAT(ShutdownInProgress, "Object Lock Manager Shutdown") : Status::OK();
  }

  Status AcquireObjectLocks(
      const tserver::AcquireObjectLockRequestPB& req, CoarseTimePoint deadline,
      WaitForBootstrap wait) {
    Synchronizer synchronizer;
    DoAcquireObjectLocksAsync(req, deadline, synchronizer.AsStdStatusCallback(), wait);
    return synchronizer.Wait();
  }

  void DoAcquireObjectLocksAsync(
      const tserver::AcquireObjectLockRequestPB& req, CoarseTimePoint deadline,
      StdStatusCallback&& callback, WaitForBootstrap wait) {
    auto s = PrepareAndExecuteAcquire(req, deadline, callback, wait);
    if (!s.ok()) {
      VLOG_WITH_FUNC(1) << "failed with " << s;
      callback(s);
    }
  }

  void WaitIfNecessaryForSimulatingOutOfOrderRequestsInTests(
      const tserver::AcquireObjectLockRequestPB& req, CoarseTimePoint& deadline) {
    if (!FLAGS_TEST_block_acquires_to_simulate_out_of_order) {
      VLOG_WITH_FUNC(3) << "Disabled.";
      return;
    }

    VLOG(1) << "Blocking acquire request to simulate out-of-order requests: "
            << req.ShortDebugString();
    TRACE("Blocking acquire request to simulate out-of-order requests");
    const auto wait_start = CoarseMonoClock::Now();
    while (!FLAGS_TEST_release_blocked_acquires_to_simulate_out_of_order) {
      constexpr auto kSpinWait = 100ms;
      VLOG(2) << Format("Blocking $0 for $1", __func__, kSpinWait);
      SleepFor(kSpinWait);
    }
    TRACE("Unblocked acquire request");
    const auto now = CoarseMonoClock::Now();
    const auto spin_wait_duration = now - wait_start;
    const auto previous_deadline = deadline;
    // Update the deadline so that this request does not get rejected later on due
    // to the delay caused here.
    deadline = previous_deadline + spin_wait_duration;
    VLOG(1) << "Unblocking acquire request. Updated deadline from "
            << ToStringRelativeToNow(previous_deadline, now)
            << " to " << ToStringRelativeToNow(deadline, now);
  }

  Status WaitUntilBootstrapped(CoarseTimePoint deadline) {
    VLOG_IF(1, !is_bootstrapped_) << "Waiting until object lock manager is bootstrapped.";
    return Wait(
        [this]() -> bool {
          bool ret = is_bootstrapped_;
          VTRACE(2, "Is bootstrapped: $0", ret);
          return ret;
        },
        deadline, "Waiting to Bootstrap.");
  }

  Status PrepareAndExecuteAcquire(
      const tserver::AcquireObjectLockRequestPB& req, CoarseTimePoint deadline,
      StdStatusCallback& callback, WaitForBootstrap wait) {
    TRACE_FUNC();
    RETURN_NOT_OK(CheckShutdown());
    auto txn = VERIFY_RESULT(FullyDecodeTransactionId(req.txn_id()));
    docdb::ObjectLockOwner object_lock_owner(txn, req.subtxn_id());
    VLOG(3) << object_lock_owner.ToString() << " Acquiring lock : " << req.ShortDebugString();
    if (wait) {
      RETURN_NOT_OK(WaitUntilBootstrapped(deadline));
    }
    TRACE("Through wait for bootstrap.");
    WaitIfNecessaryForSimulatingOutOfOrderRequestsInTests(req, deadline);
    ScopedAddToInProgressTxns add_to_in_progress{this, ToString(txn), deadline};
    RETURN_NOT_OK(add_to_in_progress.status());
    RETURN_NOT_OK(CheckRequestForDeadline(req));
    UpdateLeaseEpochIfNecessary(req.session_host_uuid(), req.lease_epoch());

    auto keys_to_lock = VERIFY_RESULT(DetermineObjectsToLock(req.object_locks()));

    DVLOG_WITH_FUNC(4) << "Dumping before acquire : " << DumpLocksToHtml();
    // Track all locks as waiting
    std::vector<ObjectLockContext> lock_contexts;
    lock_contexts.reserve(req.object_locks_size());
    for (const auto& object_lock : req.object_locks()) {
      lock_contexts.emplace_back(
          txn, req.subtxn_id(), object_lock.database_oid(), object_lock.relation_oid(),
          object_lock.object_oid(), object_lock.object_sub_oid(), object_lock.lock_type());
    }
    lock_tracker_->TrackLocks(lock_contexts, ObjectLockState::WAITING, clock_->Now());

    LOG_IF_WITH_FUNC(DFATAL, req.status_tablet().empty())
        << "Expected non-empty status tablet for lock req: " << req.ShortDebugString()
        << ". Could lead to tserver crash if this lock blocks other requests.";
    object_lock_manager_.Lock(
      docdb::LockData{
          .key_to_lock = std::move(keys_to_lock),
          .deadline = std::min(
              deadline,
              (CoarseTimePoint) (CoarseMonoClock::Now() + FLAGS_refresh_waiter_timeout_ms * 1ms)),
          .object_lock_owner = std::move(object_lock_owner),
          .status_tablet = req.status_tablet(),
          .start_time = MonoTime::FromUint64(req.propagated_hybrid_time()),
          .callback = [this, lock_contexts = std::move(lock_contexts),
                       cb = std::move(callback)](Status status) -> void {
            if (status.ok()) {
              DVLOG_WITH_FUNC(3) << "Dumping after locks were acquired : " << DumpLocksToHtml();
              lock_tracker_->TrackLocks(lock_contexts, ObjectLockState::GRANTED);
            } else {
              DVLOG_WITH_FUNC(3) << "Lock acquire failed for " << yb::ToString(lock_contexts)
                                << " with " << status << " current state : "
                                << DumpLocksToHtml();
              lock_tracker_->UntrackLocks(lock_contexts);
            }
            // Call the original callback with the final status
            cb(status);
          }
      });
    return Status::OK();
  }

  Status WaitToApplyIfNecessary(
      const tserver::ReleaseObjectLockRequestPB& req, CoarseTimePoint deadline) {
    server::UpdateClock(req, clock_.get());
    if (!req.has_apply_after_hybrid_time()) {
      return Status::OK();
    }
    auto sleep_until = HybridTime(req.apply_after_hybrid_time());
    RETURN_NOT_OK(WaitUntil(clock_.get(), sleep_until, deadline));
    return Status::OK();
  }

  class ScopedAddToInProgressTxns {
   public:
    ScopedAddToInProgressTxns(Impl* impl, const std::string& txn_id, CoarseTimePoint deadline)
        : impl_(impl), txn_id_(txn_id) {
      status_ = impl_->AddToInProgressTxns(txn_id, deadline);
    }

    ~ScopedAddToInProgressTxns() {
      if (status_.ok()) {
        impl_->RemoveFromInProgressTxns(txn_id_);
      }
    }

    Status status() const {
      return status_;
    }

   private:
    Impl* impl_;
    std::string txn_id_;
    Status status_;
  };

  Result<docdb::TxnBlockedTableLockRequests> ReleaseObjectLocks(
      const tserver::ReleaseObjectLockRequestPB& req, CoarseTimePoint deadline) {
    RETURN_NOT_OK(CheckShutdown());
    RETURN_NOT_OK(WaitUntilBootstrapped(deadline));
    auto txn = VERIFY_RESULT(FullyDecodeTransactionId(req.txn_id()));
    docdb::ObjectLockOwner object_lock_owner(txn, req.subtxn_id());
    VLOG(3) << object_lock_owner.ToString() << " Releasing locks : " << req.ShortDebugString();

    UpdateLeaseEpochIfNecessary(req.session_host_uuid(), req.lease_epoch());
    RETURN_NOT_OK(WaitToApplyIfNecessary(req, deadline));
    ScopedAddToInProgressTxns add_to_in_progress{this, ToString(txn), deadline};
    RETURN_NOT_OK(add_to_in_progress.status());
    // In case of exclusive locks, invalidate the db table cache before releasing them.
    if (req.has_db_catalog_version_data()) {
      if (FLAGS_ysql_yb_enable_invalidation_messages && req.has_db_catalog_inval_messages_data()) {
        VLOG(4) << "Received inval msgs during lock release "
        << tserver::CatalogInvalMessagesDataDebugString(req.db_catalog_inval_messages_data());

        server_->SetYsqlDBCatalogVersionsWithInvalMessages(
          req.db_catalog_version_data(),
          req.db_catalog_inval_messages_data());
      } else {
        VLOG(4) << "Received cat version without inval msgs during lock release "
                << req.db_catalog_version_data().ShortDebugString();
        server_->SetYsqlDBCatalogVersions(req.db_catalog_version_data());
        // If we only have catalog versions but not invalidation messages, it means that
        // the master lock release request was only able to read pg_yb_catalog_version,
        // but the reading of pg_yb_invalidation_messages failed.
        // Clear the fingerprint so that next heartbeat response from master
        // can read pg_yb_invalidation_messages again.
        server_->ResetCatalogVersionsFingerprint();
      }
    }

    auto was_a_blocker = object_lock_manager_.Unlock(object_lock_owner);
    lock_tracker_->UntrackAllLocks(txn, req.subtxn_id());
    return was_a_blocker;
  }

  void Poll() {
    object_lock_manager_.Poll();
  }

  void Start(docdb::LocalWaitingTxnRegistry* waiting_txn_registry) {
    object_lock_manager_.Start(waiting_txn_registry);
    poller_.Start(
        &messenger_base_.messenger()->scheduler(), 1ms * FLAGS_olm_poll_interval_ms);
  }

  void Shutdown() {
    shutdown_ = true;
    poller_.Shutdown();
    {
      yb::UniqueLock<LockType> lock(mutex_);
      if (complete_shutdown_started_) {
        return;
      }
      complete_shutdown_started_ = true;
      while (!txns_in_progress_.empty()) {
        WaitOnConditionVariableUntil(&cv_, &lock, CoarseMonoClock::Now() + 5s);
        LOG_WITH_FUNC(WARNING)
            << Format("Waiting for $0 in progress requests at the OLM", txns_in_progress_.size());
      }
    }
    object_lock_manager_.Shutdown();
  }

  void StartShutdown() {
    shutdown_ = true;
    poller_.Pause();
  }

  void UpdateLeaseEpochIfNecessary(const std::string& uuid, uint64_t lease_epoch) EXCLUDES(mutex_) {
    TRACE_FUNC();
    std::lock_guard lock(mutex_);
    auto it = max_seen_lease_epoch_.find(uuid);
    if (it == max_seen_lease_epoch_.end()) {
      max_seen_lease_epoch_[uuid] = lease_epoch;
    } else {
      it->second = std::max(it->second, lease_epoch);
    }
  }

  uint64_t GetMaxSeenLeaseEpoch(const std::string& uuid) EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    auto it = max_seen_lease_epoch_.find(uuid);
    if (it != max_seen_lease_epoch_.end()) {
      return it->second;
    }
    return 0;
  }

  Status AddToInProgressTxns(const std::string& txn_id, const CoarseTimePoint& deadline)
      EXCLUDES(mutex_) {
    TRACE_FUNC();
    yb::UniqueLock lock(mutex_);
    while (txns_in_progress_.find(txn_id) != txns_in_progress_.end()) {
      if (deadline <= CoarseMonoClock::Now()) {
        LOG(WARNING) << "Failed to add txn " << txn_id << " to in progress txns until deadline: "
                     << ToString(deadline);
        TRACE("Failed to add by deadline.");
        return STATUS_FORMAT(
            TryAgain, "Failed to add txn $0 to in progress txns until deadline: $1", txn_id,
            deadline);
      }
      if (deadline != CoarseTimePoint::max()) {
        WaitOnConditionVariableUntil(&cv_, &lock, deadline);
      } else {
        WaitOnConditionVariable(&cv_, &lock);
      }
    }
    txns_in_progress_.insert(txn_id);
    TRACE("Added");
    return Status::OK();
  }

  void RemoveFromInProgressTxns(const std::string& txn_id) EXCLUDES(mutex_) {
    TRACE_FUNC();
    {
      std::lock_guard lock(mutex_);
      txns_in_progress_.erase(txn_id);
    }
    TRACE("Removed from in progress txn.");
    cv_.notify_all();
  }

  void MarkBootstrapped() {
    is_bootstrapped_ = true;
  }

  bool IsBootstrapped() const {
    return is_bootstrapped_;
  }

  size_t TEST_GrantedLocksSize() {
    return object_lock_manager_.TEST_GrantedLocksSize();
  }

  size_t TEST_WaitingLocksSize() {
    return object_lock_manager_.TEST_WaitingLocksSize();
  }

  std::unordered_map<docdb::ObjectLockPrefix, docdb::LockState>
      TEST_GetLockStateMapForTxn(const TransactionId& txn) const {
    return object_lock_manager_.TEST_GetLockStateMapForTxn(txn);
  }

  std::string DumpLocksToHtml() {
    std::stringstream output;
    DumpLocksToHtml(output);
    return output.str();
  }

  void DumpLocksToHtml(std::ostream& out) {
    object_lock_manager_.DumpStatusHtml(out);
  }

  Status BootstrapDdlObjectLocks(const tserver::DdlLockEntriesPB& entries) {
    VLOG(2) << __func__ << " using " << yb::ToString(entries.lock_entries());
    if (IsBootstrapped()) {
      return STATUS(IllegalState, "TSLocalLockManager is already bootstrapped.");
    }
    for (const auto& acquire_req : entries.lock_entries()) {
      // This call should not block on anything.
      RETURN_NOT_OK(AcquireObjectLocks(
          acquire_req, CoarseMonoClock::Now() + 1s, tserver::WaitForBootstrap::kFalse));
    }
    MarkBootstrapped();
    VLOG_WITH_FUNC(2) << "success.";
    return Status::OK();
  }

  server::ClockPtr clock() const { return clock_; }

  void PopulateObjectLocks(
      google::protobuf::RepeatedPtrField<ObjectLockInfoPB>* object_lock_infos) {
    // We need to load and track the fastpath object locks from shared memory first
    object_lock_manager_.ConsumePendingSharedLockRequests();
    lock_tracker_->PopulateObjectLocks(object_lock_infos);
  }

 private:
  const server::ClockPtr clock_;
  std::atomic_bool is_bootstrapped_{false};
  std::unordered_map<std::string, uint64> max_seen_lease_epoch_ GUARDED_BY(mutex_);
  std::unordered_set<std::string> txns_in_progress_ GUARDED_BY(mutex_);
  // This map tracks acquire requests by transaction/subtxn_id their
  // corresponding (max) deadline, so that we can launch a clean up task to
  // release any potential out-of-order locks past the deadline.
  std::unordered_map<TransactionId, std::unordered_map<SubTransactionId, CoarseTimePoint>>
      max_deadline_across_acquires_ GUARDED_BY(mutex_);
  std::condition_variable cv_;
  using LockType = std::mutex;
  LockType mutex_;
  TabletServerIf* server_;
  server::RpcServerBase& messenger_base_;
  docdb::ObjectLockManager object_lock_manager_;
  std::atomic<bool> shutdown_{false};
  bool complete_shutdown_started_ GUARDED_BY(mutex_) {false};
  rpc::Poller poller_;
  std::shared_ptr<ObjectLockTracker> lock_tracker_;
};

TSLocalLockManager::TSLocalLockManager(
    const server::ClockPtr& clock, TabletServerIf* tablet_server,
    server::RpcServerBase& messenger_server, ThreadPool* thread_pool,
    const MetricEntityPtr& metric_entity, std::shared_ptr<ObjectLockTracker> lock_tracker,
    docdb::ObjectLockSharedStateManager* shared_manager)
      : impl_(new Impl(
          clock, CHECK_NOTNULL(tablet_server), messenger_server, CHECK_NOTNULL(thread_pool),
          metric_entity, lock_tracker, shared_manager)) {}

TSLocalLockManager::~TSLocalLockManager() {}

void TSLocalLockManager::AcquireObjectLocksAsync(
    const tserver::AcquireObjectLockRequestPB& req, CoarseTimePoint deadline,
    StdStatusCallback&& callback) {
  impl_->DoAcquireObjectLocksAsync(req, deadline, std::move(callback), WaitForBootstrap::kTrue);
}

Result<docdb::TxnBlockedTableLockRequests> TSLocalLockManager::ReleaseObjectLocks(
    const tserver::ReleaseObjectLockRequestPB& req, CoarseTimePoint deadline) {
  DVLOG_WITH_FUNC(4) << "Dumping before release : " << impl_->DumpLocksToHtml();
  auto ret = impl_->ReleaseObjectLocks(req, deadline);
  DVLOG_WITH_FUNC(3) << "Dumping after release : " << impl_->DumpLocksToHtml();
  return ret;
}

void TSLocalLockManager::Start(
    docdb::LocalWaitingTxnRegistry* waiting_txn_registry) {
  return impl_->Start(waiting_txn_registry);
}

void TSLocalLockManager::Shutdown() { impl_->Shutdown(); }

void TSLocalLockManager::StartShutdown() { impl_->StartShutdown(); }

bool TSLocalLockManager::IsShutdownInProgress() const {
  return !impl_->CheckShutdown().ok();
}

void TSLocalLockManager::DumpLocksToHtml(std::ostream& out) {
  return impl_->DumpLocksToHtml(out);
}

size_t TSLocalLockManager::TEST_GrantedLocksSize() {
  return impl_->TEST_GrantedLocksSize();
}

bool TSLocalLockManager::IsBootstrapped() const {
  return impl_->IsBootstrapped();
}

server::ClockPtr TSLocalLockManager::clock() const {
  return impl_->clock();
}

void TSLocalLockManager::PopulateObjectLocks(
    google::protobuf::RepeatedPtrField<ObjectLockInfoPB>* object_lock_infos) {
  impl_->PopulateObjectLocks(object_lock_infos);
}

size_t TSLocalLockManager::TEST_WaitingLocksSize() {
  return impl_->TEST_WaitingLocksSize();
}

Status TSLocalLockManager::BootstrapDdlObjectLocks(const tserver::DdlLockEntriesPB& entries) {
  return impl_->BootstrapDdlObjectLocks(entries);
}

void TSLocalLockManager::TEST_MarkBootstrapped() {
  impl_->MarkBootstrapped();
}

std::unordered_map<docdb::ObjectLockPrefix, docdb::LockState>
    TSLocalLockManager::TEST_GetLockStateMapForTxn(const TransactionId& txn) const {
  return impl_->TEST_GetLockStateMapForTxn(txn);
}

void TSLocalLockManager::TrackDeadlineForGlobalAcquire(
    const TransactionId& txn_id, const SubTransactionId& subtxn_id,
    CoarseTimePoint apply_after_ht) {
  impl_->TrackDeadlineForGlobalAcquire(txn_id, subtxn_id, apply_after_ht);
}

void TSLocalLockManager::ScheduleReleaseForLostMessages(
    yb::client::YBClient& client, std::weak_ptr<TSLocalLockManager> lock_manager_weak,
    const TransactionId& txn_id, std::optional<SubTransactionId> subtxn_id,
    const std::shared_ptr<master::ReleaseObjectLocksGlobalRequestPB>& release_req) {
  impl_->ScheduleReleaseForLostMessages(client, lock_manager_weak, txn_id, subtxn_id, release_req);
}

void ReleaseWithRetriesGlobal(
    yb::client::YBClient& client, std::weak_ptr<TSLocalLockManager> lock_manager_weak,
    const TransactionId& txn_id, std::optional<SubTransactionId> subtxn_id,
    const std::shared_ptr<master::ReleaseObjectLocksGlobalRequestPB>& release_req) {
  auto ptr = lock_manager_weak.lock();
  // If the lock manager is no longer available, we cannot proceed with the release.
  // This can happen if the LocalTServer has lost its lease.
  // In such cases, the LocalTServer need not attempt to release the locks.
  // The master will handle the cleanup of global locks for the expired lease epoch(s).
  if (!ptr) {
    return;
  }

  ptr->ScheduleReleaseForLostMessages(client, lock_manager_weak, txn_id, subtxn_id, release_req);

  ReleaseWithRetriesGlobalNow(client, lock_manager_weak, release_req);
}

void AcquireObjectLockLocallyWithRetries(
    std::weak_ptr<TSLocalLockManager> lock_manager, AcquireObjectLockRequestPB&& req,
    CoarseTimePoint deadline, StdStatusCallback&& lock_cb,
    std::function<Status(CoarseTimePoint)> check_txn_running) {
  auto retry_cb = [lock_manager, req, lock_cb = std::move(lock_cb), deadline,
                   check_txn_running](const Status& s) mutable {
    if (!s.IsTryAgain()) {
      return lock_cb(s);
    }
    auto txn_status = check_txn_running(deadline);
    if (!txn_status.ok()) {
      // Transaction has already failed.
      return lock_cb(txn_status);
    }
    if (CoarseMonoClock::Now() < deadline) {
      return AcquireObjectLockLocallyWithRetries(
          lock_manager, std::move(req), deadline, std::move(lock_cb), check_txn_running);
    }
    return lock_cb(s);
  };
  auto shared_lock_manager = lock_manager.lock();
  if (!shared_lock_manager) {
    return retry_cb(STATUS_FORMAT(
        IllegalState,
        "TsLocalLockManager unavailable. Lease corresponding to this request/session has "
        "expired."));
  }
  shared_lock_manager->AcquireObjectLocksAsync(req, deadline, std::move(retry_cb));
}
}  // namespace yb::tserver
