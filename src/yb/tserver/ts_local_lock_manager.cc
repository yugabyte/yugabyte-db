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
#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/scope_exit.h"
#include "yb/util/trace.h"
#include "yb/util/unique_lock.h"

using namespace std::literals;
DECLARE_bool(dump_lock_keys);

namespace yb::tserver {

class TSLocalLockManager::Impl {
 public:
  Impl(const server::ClockPtr& clock, TabletServerIf* server) : clock_(clock), server_(server) {}

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

  Status AcquireObjectLocks(
      const tserver::AcquireObjectLockRequestPB& req, CoarseTimePoint deadline,
      WaitForBootstrap wait) {
    TRACE_FUNC();
    auto txn = VERIFY_RESULT(FullyDecodeTransactionId(req.txn_id()));
    docdb::ObjectLockOwner object_lock_owner(txn, req.subtxn_id());
    VLOG(3) << object_lock_owner.ToString() << " Acquiring lock : " << req.ShortDebugString();
    if (wait) {
      VTRACE(1, "Waiting for bootstrap.");
      RETURN_NOT_OK(Wait(
          [this]() -> bool {
            bool ret = is_bootstrapped_;
            VTRACE(2, "Is bootstrapped: $0", ret);
            return ret;
          },
          deadline, "Waiting to Bootstrap."));
    }
    TRACE("Through wait for bootstrap.");
    ScopedAddToInProgressTxns add_to_in_progress{this, ToString(txn), deadline};
    RETURN_NOT_OK(add_to_in_progress.status());
    RETURN_NOT_OK(CheckRequestForDeadline(req));
    UpdateLeaseEpochIfNecessary(req.session_host_uuid(), req.lease_epoch());

    auto keys_to_lock = VERIFY_RESULT(DetermineObjectsToLock(req.object_locks()));
    if (object_lock_manager_.Lock(keys_to_lock.lock_batch, deadline, object_lock_owner)) {
      TRACE("Successfully obtained object locks.");
      return Status::OK();
    }
    TRACE("Could not get the object locks.");
    std::string batch_str;
    if (FLAGS_dump_lock_keys) {
      batch_str = Format(", batch: $0", keys_to_lock.lock_batch);
    }
    return STATUS_FORMAT(
        TryAgain, "Failed to obtain object locks until deadline: $0$1", deadline, batch_str);
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
    auto txn = VERIFY_RESULT(FullyDecodeTransactionId(req.txn_id()));
    docdb::ObjectLockOwner object_lock_owner(txn, req.subtxn_id());
    VLOG(3) << object_lock_owner.ToString()
            << " Releasing locks : " << req.ShortDebugString();

    UpdateLeaseEpochIfNecessary(req.session_host_uuid(), req.lease_epoch());
    RETURN_NOT_OK(WaitToApplyIfNecessary(req, deadline));
    ScopedAddToInProgressTxns add_to_in_progress{this, ToString(txn), deadline};
    RETURN_NOT_OK(add_to_in_progress.status());
    // In case of exclusive locks, invalidate the db table cache before releasing them.
    if (req.has_db_catalog_version_data()) {
      server_->SetYsqlDBCatalogVersions(req.db_catalog_version_data());
    }
    object_lock_manager_.Unlock(object_lock_owner);
    return Status::OK();
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
        LOG(ERROR) << "Failed to add txn " << txn_id << " to in progress txns until deadline: "
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

  size_t TEST_GrantedLocksSize() const {
    return object_lock_manager_.TEST_GrantedLocksSize();
  }

  size_t TEST_WaitingLocksSize() const {
    return object_lock_manager_.TEST_WaitingLocksSize();
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
      CoarseTimePoint deadline = CoarseMonoClock::Now() + 1s;
      RETURN_NOT_OK(AcquireObjectLocks(acquire_req, deadline, tserver::WaitForBootstrap::kFalse));
    }
    MarkBootstrapped();
    return Status::OK();
  }

  server::ClockPtr clock() const { return clock_; }

 private:
  const server::ClockPtr clock_;
  docdb::ObjectLockManager object_lock_manager_;
  std::atomic_bool is_bootstrapped_{false};
  std::unordered_map<std::string, uint64> max_seen_lease_epoch_ GUARDED_BY(mutex_);
  std::unordered_set<std::string> txns_in_progress_ GUARDED_BY(mutex_);
  std::condition_variable cv_;
  using LockType = std::mutex;
  LockType mutex_;
  TabletServerIf* server_;
};

TSLocalLockManager::TSLocalLockManager(const server::ClockPtr& clock, TabletServerIf* server)
    : impl_(new Impl(clock, server)) {}

TSLocalLockManager::~TSLocalLockManager() {}

Status TSLocalLockManager::AcquireObjectLocks(
    const tserver::AcquireObjectLockRequestPB& req, CoarseTimePoint deadline,
    WaitForBootstrap wait) {
  if (VLOG_IS_ON(4)) {
    std::stringstream output;
    impl_->DumpLocksToHtml(output);
    VLOG(4) << "Dumping current state Before acquire : " << output.str();
  }
  auto ret = impl_->AcquireObjectLocks(req, deadline, wait);
  if (VLOG_IS_ON(3)) {
    std::stringstream output;
    impl_->DumpLocksToHtml(output);
    VLOG(3) << "Acquire " << (ret.ok() ? "succeded" : "failed")
            << ". Dumping current state After acquire : " << output.str();
  }
  return ret;
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

void TSLocalLockManager::DumpLocksToHtml(std::ostream& out) {
  return impl_->DumpLocksToHtml(out);
}

size_t TSLocalLockManager::TEST_GrantedLocksSize() const {
  return impl_->TEST_GrantedLocksSize();
}

bool TSLocalLockManager::IsBootstrapped() const {
  return impl_->IsBootstrapped();
}

size_t TSLocalLockManager::TEST_WaitingLocksSize() const {
  return impl_->TEST_WaitingLocksSize();
}

Status TSLocalLockManager::BootstrapDdlObjectLocks(const tserver::DdlLockEntriesPB& entries) {
  return impl_->BootstrapDdlObjectLocks(entries);
}

void TSLocalLockManager::TEST_MarkBootstrapped() {
  impl_->MarkBootstrapped();
}

server::ClockPtr TSLocalLockManager::clock() const {
  return impl_->clock();
}

} // namespace yb::tserver
