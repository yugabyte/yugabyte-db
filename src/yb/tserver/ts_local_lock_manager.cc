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
#include "yb/docdb/shared_lock_manager.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/scope_exit.h"

using namespace std::literals;
DECLARE_bool(dump_lock_keys);

namespace yb::tablet {

class TSLocalLockManager::Impl {
 public:
  Impl() = default;

  ~Impl() = default;

  Status AcquireObjectLocks(
      const tserver::AcquireObjectLockRequestPB& req, CoarseTimePoint deadline,
      WaitForBootstrap wait) {
    if (wait) {
      RETURN_NOT_OK(
          Wait([this]() -> bool { return is_bootstrapped_; }, deadline, "Waiting to Bootstrap."));
    }

    docdb::ObjectLockOwner object_lock_owner(
        VERIFY_RESULT(FullyDecodeTransactionId(req.txn_id())), req.subtxn_id());
    auto result = VERIFY_RESULT(DetermineObjectsToLock(req.object_locks()));
    if (object_lock_manager_.Lock(object_lock_owner, result.lock_batch, deadline)) {
      return Status::OK();
    }
    std::string batch_str;
    if (FLAGS_dump_lock_keys) {
      batch_str = Format(", batch: $0", result.lock_batch);
    }
    return STATUS_FORMAT(
        TryAgain, "Failed to obtain object locks until deadline: $0$1", deadline, batch_str);
  }

  Status ReleaseObjectLocks(const tserver::ReleaseObjectLockRequestPB& req) {
    docdb::ObjectLockOwner object_lock_owner(
        VERIFY_RESULT(FullyDecodeTransactionId(req.txn_id())), req.subtxn_id());
    if (req.release_all_locks() || !req.subtxn_id()) {
      VLOG(2) << "Release all locks for owner " << AsString(object_lock_owner);
      object_lock_manager_.Unlock(object_lock_owner);
      return Status::OK();
    }

    static auto const key_entry_types =
        {dockv::KeyEntryType::kWeakObjectLock, dockv::KeyEntryType::kStrongObjectLock};
    std::vector<docdb::TrackedLockEntryKey<docdb::ObjectLockManager>> lock_entry_keys;
    for (auto lock : req.object_locks()) {
      for (const auto& type : key_entry_types) {
        lock_entry_keys.push_back(
            {object_lock_owner, {lock.database_oid(), lock.object_oid(), type}});
      }
    }
    object_lock_manager_.Unlock(lock_entry_keys);
    return Status::OK();
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
      RETURN_NOT_OK(AcquireObjectLocks(acquire_req, deadline, tablet::WaitForBootstrap::kFalse));
    }
    MarkBootstrapped();
    return Status::OK();
  }

 private:
  docdb::ObjectLockManager object_lock_manager_;
  std::atomic_bool is_bootstrapped_{false};
};

TSLocalLockManager::TSLocalLockManager() : impl_(new Impl()) {}

TSLocalLockManager::~TSLocalLockManager() {}

Status TSLocalLockManager::AcquireObjectLocks(
    const tserver::AcquireObjectLockRequestPB& req, CoarseTimePoint deadline,
    WaitForBootstrap wait) {
  return impl_->AcquireObjectLocks(req, deadline, wait);
}

Status TSLocalLockManager::ReleaseObjectLocks(const tserver::ReleaseObjectLockRequestPB& req) {
  return impl_->ReleaseObjectLocks(req);
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

} // namespace yb::tablet
