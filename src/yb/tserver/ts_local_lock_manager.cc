//
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
//

#include "yb/tserver/ts_local_lock_manager.h"

#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/object_lock_manager.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/poller.h"

#include "yb/server/server_base.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/scope_exit.h"
#include "yb/util/trace.h"
#include "yb/util/unique_lock.h"

using namespace std::literals;
DECLARE_bool(dump_lock_keys);

DEFINE_NON_RUNTIME_int64(olm_poll_interval_ms, 100,
    "Poll interval for Object lock Manager. Waiting requests that are unblocked by other release "
    "requests are independent of this interval since the release schedules unblocking of potential "
    "waiters. Yet this might help release timedout requests soon and also avoid probable issues "
    "with the signaling mechanism if any.");

DECLARE_uint64(refresh_waiter_timeout_ms);

namespace yb::tserver {

YB_STRONGLY_TYPED_BOOL(WaitForBootstrap);

// This class tracks object locks specifically for the pg_locks view.
// An alternative would be to extract and decrypt locks directly from ObjectLockManager,
// but for simplicity, we decided to maintain a separate tracker here.
class ObjectLockTracker {
 public:

  Status TrackLocks(
    const std::vector<ObjectLockContext>& lock_contexts, ObjectLockState lock_state,
    HybridTime wait_start = HybridTime::kInvalid) {
    for (const auto& lock_context : lock_contexts) {
      RETURN_NOT_OK(TrackLock(lock_context, lock_state, wait_start));
    }
    return Status::OK();
  }

  Status TrackLock(
      const ObjectLockContext& lock_context, ObjectLockState lock_state,
      HybridTime wait_start = HybridTime::kInvalid) {
    const auto txn_id = lock_context.txn_id;
    const auto subtxn_id = lock_context.subtxn_id;
    const auto lock_id = LockId{
      .database_oid = lock_context.database_oid,
      .relation_oid = lock_context.relation_oid,
      .object_oid = lock_context.object_oid,
      .object_sub_oid = lock_context.object_sub_oid,
      .lock_type = lock_context.lock_type
    };

    std::lock_guard l(mutex_);
    auto& lock_id_map = lock_map_[txn_id][subtxn_id];

    auto lock_it = lock_id_map.find(lock_id);
    if (lock_it != lock_id_map.end()) {
      auto& existing = lock_it->second;
      existing.counter++;
      if (existing.lock_state == ObjectLockState::WAITING &&
          lock_state == ObjectLockState::GRANTED) {
        // Upgrade from WAITING to GRANTED state and
        // keep the wait_start time as it is for pg_locks view.
        existing.lock_state = ObjectLockState::GRANTED;
      }
      return Status::OK();
    }

    LockInfo new_lock{
      .lock_state = lock_state,
      .wait_start = (lock_state == ObjectLockState::WAITING) ? wait_start : HybridTime::kInvalid,
      .counter = 1
    };
    lock_id_map.emplace(lock_id, new_lock);

    return Status::OK();
  }

  Status UntrackLocks(const std::vector<ObjectLockContext>& lock_contexts) {
    for (const auto& lock_context : lock_contexts) {
      RETURN_NOT_OK(UntrackLock(lock_context));
    }
    return Status::OK();
  }

  Status UntrackLock(const ObjectLockContext& lock_context) {
    if (lock_context.subtxn_id == 0) {
      return UntrackAllLocks(lock_context.txn_id, lock_context.subtxn_id);
    }

    std::lock_guard l(mutex_);
    auto txn_it = lock_map_.find(lock_context.txn_id);
    if (txn_it == lock_map_.end()) {
      return Status::OK();
    }
    auto subtxn_it = txn_it->second.find(lock_context.subtxn_id);
    if (subtxn_it == txn_it->second.end()) {
      return Status::OK();
    }

    auto& lock_id_map = subtxn_it->second;
    const auto lock_id = LockId{
      .database_oid = lock_context.database_oid,
      .relation_oid = lock_context.relation_oid,
      .object_oid = lock_context.object_oid,
      .object_sub_oid = lock_context.object_sub_oid,
      .lock_type = lock_context.lock_type
    };
    auto lock_it = lock_id_map.find(lock_id);
    DCHECK(lock_it != lock_id_map.end());
    if (lock_it->second.counter > 1) {
      lock_it->second.counter--;
      return Status::OK();
    }

    // cleanup
    lock_id_map.erase(lock_it);
    if (lock_id_map.empty()) {
      txn_it->second.erase(subtxn_it);
      if (txn_it->second.empty()) {
        lock_map_.erase(txn_it);
      }
    }

    return Status::OK();
  }

  Status UntrackAllLocks(TransactionId txn_id, SubTransactionId subtxn_id) {
    std::lock_guard l(mutex_);
    if (subtxn_id == 0) {
      // Remove all subtransactions for this transaction
      lock_map_.erase(txn_id);
    } else {
      auto txn_iter = lock_map_.find(txn_id);
      if (txn_iter != lock_map_.end()) {
        txn_iter->second.erase(subtxn_id);
        if (txn_iter->second.empty()) {
          lock_map_.erase(txn_iter);
        }
      }
    }
    return Status::OK();
  }

  void PopulateObjectLocks(
      google::protobuf::RepeatedPtrField<ObjectLockInfoPB>* object_lock_infos) const {
    object_lock_infos->Clear();
    SharedLock l(mutex_);
    for (const auto& [txn_id, subtxn_map] : lock_map_) {
      for (const auto& [subtxn_id, lock_id_map] : subtxn_map) {
        for (const auto& [lock_id, lock_info] : lock_id_map) {
          auto* object_lock_info_pb = object_lock_infos->Add();
          object_lock_info_pb->set_transaction_id(txn_id.data(), txn_id.size());
          object_lock_info_pb->set_subtransaction_id(subtxn_id);
          object_lock_info_pb->set_database_oid(lock_id.database_oid);
          object_lock_info_pb->set_relation_oid(lock_id.relation_oid);
          object_lock_info_pb->set_object_oid(lock_id.object_oid);
          object_lock_info_pb->set_object_sub_oid(lock_id.object_sub_oid);
          object_lock_info_pb->set_mode(lock_id.lock_type);
          object_lock_info_pb->set_lock_state(lock_info.lock_state);
          DCHECK_NE(lock_info.wait_start, HybridTime::kInvalid);
          object_lock_info_pb->set_wait_start_ht(lock_info.wait_start.ToUint64());
        }
      }
    }
  }

 private:
  mutable std::shared_mutex mutex_;

  struct LockId {
    YB_STRUCT_DEFINE_HASH(
        LockId, database_oid, relation_oid, object_oid, object_sub_oid, lock_type);
    auto operator<=>(const LockId&) const = default;
    uint64_t database_oid;
    uint64_t relation_oid;
    uint64_t object_oid;
    uint64_t object_sub_oid;
    TableLockType lock_type;
  };

  struct LockInfo {
    ObjectLockState lock_state;
    HybridTime wait_start = HybridTime::kInvalid;
    size_t counter = 0;
  };

  std::unordered_map<TransactionId,
      std::unordered_map<SubTransactionId,
          std::unordered_map<LockId, LockInfo>>> lock_map_;
};

class TSLocalLockManager::Impl {
 public:
  Impl(
      const server::ClockPtr& clock, TabletServerIf* tablet_server,
      server::RpcServerBase& messenger_server, ThreadPool* thread_pool,
      docdb::ObjectLockSharedStateManager* shared_manager)
      : clock_(clock), server_(tablet_server), messenger_base_(messenger_server),
        object_lock_manager_(thread_pool, messenger_server, shared_manager),
        poller_("TSLocalLockManager", std::bind(&Impl::Poll, this)) {}

  ~Impl() = default;

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

  Status CheckShutdown() {
    return shutdown_
        ? STATUS_FORMAT(ShutdownInProgress, "Object Lock Manager Shutdown") : Status::OK();
  }

  Status AcquireObjectLocks(
      const tserver::AcquireObjectLockRequestPB& req, CoarseTimePoint deadline,
      WaitForBootstrap wait) {
    Synchronizer synchronizer;
    DoAcquireObjectLocksAsync(
        req, deadline, synchronizer.AsStdStatusCallback(), tserver::WaitForBootstrap::kFalse);
    return synchronizer.Wait();
  }

  void DoAcquireObjectLocksAsync(
      const tserver::AcquireObjectLockRequestPB& req, CoarseTimePoint deadline,
      StdStatusCallback&& callback, WaitForBootstrap wait) {
    auto s = PrepareAndExecuteAcquire(req, deadline, callback, wait);
    if (!s.ok()) {
      callback(s);
    }
  }

  Status WaitUntilBootstrapped(CoarseTimePoint deadline) {
    LOG(INFO) << "Waiting until object lock manager is bootstrapped.";
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
    ScopedAddToInProgressTxns add_to_in_progress{this, ToString(txn), deadline};
    RETURN_NOT_OK(add_to_in_progress.status());
    RETURN_NOT_OK(CheckRequestForDeadline(req));
    UpdateLeaseEpochIfNecessary(req.session_host_uuid(), req.lease_epoch());

    auto keys_to_lock = VERIFY_RESULT(DetermineObjectsToLock(req.object_locks()));

    // Track all locks as waiting
    std::vector<ObjectLockContext> lock_contexts;
    lock_contexts.reserve(req.object_locks_size());
    for (const auto& object_lock : req.object_locks()) {
      lock_contexts.emplace_back(
          txn, req.subtxn_id(), object_lock.database_oid(), object_lock.relation_oid(),
          object_lock.object_oid(), object_lock.object_sub_oid(), object_lock.lock_type());
    }
    auto tracker_status =
        lock_tracker_.TrackLocks(lock_contexts, ObjectLockState::WAITING, clock_->Now());
    DCHECK_OK(tracker_status);

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
            Status tracker_status;
            if (status.ok()) {
              tracker_status = lock_tracker_.TrackLocks(lock_contexts, ObjectLockState::GRANTED);
            } else {
              tracker_status = lock_tracker_.UntrackLocks(lock_contexts);
            }
            DCHECK_OK(tracker_status);
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

  Status ReleaseObjectLocks(
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
      server_->SetYsqlDBCatalogVersions(req.db_catalog_version_data());
    }
    object_lock_manager_.Unlock(object_lock_owner);
    auto tracker_status = lock_tracker_.UntrackAllLocks(txn, req.subtxn_id());
    DCHECK_OK(tracker_status);
    return Status::OK();
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
      while (!txns_in_progress_.empty()) {
        WaitOnConditionVariableUntil(&cv_, &lock, CoarseMonoClock::Now() + 5s);
        LOG_WITH_FUNC(WARNING)
            << Format("Waiting for $0 in progress requests at the OLM", txns_in_progress_.size());
      }
    }
    object_lock_manager_.Shutdown();
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
    return Status::OK();
  }

  server::ClockPtr clock() const { return clock_; }

  void PopulateObjectLocks(
      google::protobuf::RepeatedPtrField<ObjectLockInfoPB>* object_lock_infos) const {
    lock_tracker_.PopulateObjectLocks(object_lock_infos);
  }

 private:
  const server::ClockPtr clock_;
  std::atomic_bool is_bootstrapped_{false};
  std::unordered_map<std::string, uint64> max_seen_lease_epoch_ GUARDED_BY(mutex_);
  std::unordered_set<std::string> txns_in_progress_ GUARDED_BY(mutex_);
  std::condition_variable cv_;
  using LockType = std::mutex;
  LockType mutex_;
  TabletServerIf* server_;
  server::RpcServerBase& messenger_base_;
  docdb::ObjectLockManager object_lock_manager_;
  std::atomic<bool> shutdown_{false};
  rpc::Poller poller_;
  ObjectLockTracker lock_tracker_;
};

TSLocalLockManager::TSLocalLockManager(
    const server::ClockPtr& clock, TabletServerIf* tablet_server,
    server::RpcServerBase& messenger_server, ThreadPool* thread_pool,
    docdb::ObjectLockSharedStateManager* shared_manager)
      : impl_(new Impl(
          clock, CHECK_NOTNULL(tablet_server), messenger_server, CHECK_NOTNULL(thread_pool),
          shared_manager)) {}

TSLocalLockManager::~TSLocalLockManager() {}

void TSLocalLockManager::AcquireObjectLocksAsync(
    const tserver::AcquireObjectLockRequestPB& req, CoarseTimePoint deadline,
    StdStatusCallback&& callback) {
  impl_->DoAcquireObjectLocksAsync(req, deadline, std::move(callback), WaitForBootstrap::kTrue);
}

Status TSLocalLockManager::ReleaseObjectLocks(
    const tserver::ReleaseObjectLockRequestPB& req, CoarseTimePoint deadline) {
  if (VLOG_IS_ON(4)) {
    std::stringstream output;
    impl_->DumpLocksToHtml(output);
    VLOG(4) << "Dumping current state Before release : " << output.str();
  }
  auto ret = impl_->ReleaseObjectLocks(req, deadline);
  if (VLOG_IS_ON(3)) {
    std::stringstream output;
    impl_->DumpLocksToHtml(output);
    VLOG(3) << "Dumping current state After release : " << output.str();
  }
  return ret;
}

void TSLocalLockManager::Start(
    docdb::LocalWaitingTxnRegistry* waiting_txn_registry) {
  return impl_->Start(waiting_txn_registry);
}

void TSLocalLockManager::Shutdown() {
  impl_->Shutdown();
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
    google::protobuf::RepeatedPtrField<ObjectLockInfoPB>* object_lock_infos) const {
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

}  // namespace yb::tserver
