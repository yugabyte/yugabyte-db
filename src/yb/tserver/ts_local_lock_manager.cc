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
#include "yb/util/unique_lock.h"

using namespace std::literals;
DECLARE_bool(dump_lock_keys);

namespace yb::tserver {

class TSLocalLockManager::Impl {
 public:
  explicit Impl(const server::ClockPtr& clock) : clock_(clock) {}

  ~Impl() = default;

  Status CheckRequestForDeadline(const tserver::AcquireObjectLockRequestPB& req) {
    server::UpdateClock(req, clock_.get());
    auto now_ht = clock_->Now();
    if (req.has_ignore_after_hybrid_time() && req.ignore_after_hybrid_time() <= now_ht.ToUint64()) {
      VLOG(2) << "Ignoring request which is past its deadline: " << req.DebugString() << " now_ht "
              << now_ht << " now_ht.ToUint64() " << now_ht.ToUint64();
      return STATUS_FORMAT(IllegalState, "Ignoring request which is past its deadline.");
    }
    auto max_seen_lease_epoch = GetMaxSeenLeaseEpoch(req.session_host_uuid());
    if (req.lease_epoch() < max_seen_lease_epoch) {
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
    if (wait) {
      RETURN_NOT_OK(
          Wait([this]() -> bool { return is_bootstrapped_; }, deadline, "Waiting to Bootstrap."));
    }
    auto txn = VERIFY_RESULT(FullyDecodeTransactionId(req.txn_id()));
    ScopedAddToInProgressTxns add_to_in_progress{this, ToString(txn), deadline};
    RETURN_NOT_OK(add_to_in_progress.status());
    RETURN_NOT_OK(CheckRequestForDeadline(req));
    UpdateLeaseEpochIfNecessary(req.session_host_uuid(), req.lease_epoch());

    docdb::ObjectLockOwner object_lock_owner(txn, req.subtxn_id());
    auto keys_to_lock = VERIFY_RESULT(DetermineObjectsToLock(req.object_locks()));
    if (object_lock_manager_.Lock(object_lock_owner, keys_to_lock.lock_batch, deadline)) {
      return Status::OK();
    }
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
    UpdateLeaseEpochIfNecessary(req.session_host_uuid(), req.lease_epoch());
    RETURN_NOT_OK(WaitToApplyIfNecessary(req, deadline));
    auto txn = VERIFY_RESULT(FullyDecodeTransactionId(req.txn_id()));
    ScopedAddToInProgressTxns add_to_in_progress{this, ToString(txn), deadline};
    RETURN_NOT_OK(add_to_in_progress.status());
    docdb::ObjectLockOwner object_lock_owner(txn, req.subtxn_id());
    object_lock_manager_.Unlock(object_lock_owner);
    return Status::OK();
  }

  void UpdateLeaseEpochIfNecessary(const std::string& uuid, uint64_t lease_epoch) EXCLUDES(mutex_) {
    std::lock_guard<LockType> lock(mutex_);
    auto it = max_seen_lease_epoch_.find(uuid);
    if (it == max_seen_lease_epoch_.end()) {
      max_seen_lease_epoch_[uuid] = lease_epoch;
    } else {
      it->second = std::max(it->second, lease_epoch);
    }
  }

  uint64_t GetMaxSeenLeaseEpoch(const std::string& uuid) EXCLUDES(mutex_) {
    std::lock_guard<LockType> lock(mutex_);
    auto it = max_seen_lease_epoch_.find(uuid);
    if (it != max_seen_lease_epoch_.end()) {
      return it->second;
    }
    return 0;
  }

  Status AddToInProgressTxns(const std::string& txn_id, const CoarseTimePoint& deadline)
      EXCLUDES(mutex_) {
    yb::UniqueLock<LockType> lock(mutex_);
    while (txns_in_progress_.find(txn_id) != txns_in_progress_.end()) {
      if (deadline <= CoarseMonoClock::Now()) {
        LOG(ERROR) << "Failed to add txn " << txn_id << " to in progress txns until deadline: "
                    << ToString(deadline);
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
    return Status::OK();
  }

  void RemoveFromInProgressTxns(const std::string& txn_id) EXCLUDES(mutex_) {
    {
      std::lock_guard<LockType> lock(mutex_);
      txns_in_progress_.erase(txn_id);
    }
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
    // TODO(amit): 1) When we implement YSQL leases, we need to clear out the locks, and
    // re-bootstrap. For now, we are not doing that, the only time this should be happening
    // is when a tserver registers with the master for the first time.
    // 2) If the tserver is already bootstrapped from a master, we should not be bootstrapping
    // again. However, even if we are bootstrap again, it should be safe to do so. Once we implement
    // persistence of TServer Registration at the master, we can avoid this.
    if (IsBootstrapped()) {
      LOG_WITH_FUNC(INFO) << "TSLocalLockManager is already bootstrapped. Ignoring the request.";
      return Status::OK();
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

};

TSLocalLockManager::TSLocalLockManager(const server::ClockPtr& clock) : impl_(new Impl(clock)) {}

TSLocalLockManager::~TSLocalLockManager() {}

Status TSLocalLockManager::AcquireObjectLocks(
    const tserver::AcquireObjectLockRequestPB& req, CoarseTimePoint deadline,
    WaitForBootstrap wait) {
  return impl_->AcquireObjectLocks(req, deadline, wait);
}

Status TSLocalLockManager::ReleaseObjectLocks(
    const tserver::ReleaseObjectLockRequestPB& req, CoarseTimePoint deadline) {
  return impl_->ReleaseObjectLocks(req, deadline);
}

void TSLocalLockManager::DumpLocksToHtml(std::ostream& out) {
  return impl_->DumpLocksToHtml(out);
}

size_t TSLocalLockManager::TEST_GrantedLocksSize() const {
  return impl_->TEST_GrantedLocksSize();
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
