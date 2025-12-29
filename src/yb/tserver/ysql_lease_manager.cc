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

#include "yb/rpc/messenger.h"
#include "yb/rpc/scheduler.h"

#include "yb/tserver/pg_client_service.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_local_lock_manager.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/ysql_lease_manager.h"
#include "yb/tserver/ysql_lease_poller.h"

#include "yb/util/locks.h"
#include "yb/util/mutex.h"

using namespace std::literals;

DEFINE_test_flag(
    bool, enable_ysql_operation_lease_expiry_check, true,
    "Whether tservers should monitor their ysql op lease and kill their hosted pg "
    "sessions when it expires. Only available as a flag for tests.");

DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(enable_ysql);

namespace yb::tserver {

class YSQLLeaseManager::Impl {
 public:
  Impl(
      TabletServer& server, YSQLLeaseManager& interface,
      server::MasterAddressesPtr master_addresses);

  Status ProcessLeaseUpdate(const master::RefreshYsqlLeaseInfoPB& lease_refresh_info)
      EXCLUDES(lease_toggle_lock_, lock_);

  Status StartYSQLLeaseRefresher();
  void StartTSLocalLockManager() EXCLUDES(lock_);

  void UpdateMasterAddresses(const server::MasterAddressesPtr& master_addresses);

  Status Stop();

  std::future<Status> RelinquishLease(MonoDelta timeout) const;
  YSQLLeaseInfo GetYSQLLeaseInfo() EXCLUDES(lock_);


  TSLocalLockManagerPtr ts_local_lock_manager() const EXCLUDES(lock_);

 private:
  tserver::TSLocalLockManagerPtr ResetAndGetTSLocalLockManager() EXCLUDES(lock_);
  void StartTSLocalLockManagerUnlocked() REQUIRES(lock_);

  // ============================================================================
  // START: check_ysql_lease_ methods
  // These methods are run on the check_ysql_lease_ task.
  // They expire the lease when the lease TTL runs out.
  // ============================================================================
  void ScheduleCheckLeaseWithNoLiveLease();
  void ScheduleCheckLease(CoarseTimePoint next_check_time);
  void CheckLeaseStatus();
  std::optional<CoarseTimePoint> CheckLeaseStatusInner() EXCLUDES(lease_toggle_lock_, lock_);
  // ============================================================================
  // END: check_ysql_lease_ methods
  // ============================================================================

  // State is guarded by two locks, lease_toggle_lock_ and lock_.
  // Locking order is:
  //   lease_toggle_lock_, lock_
  // lease_toggle_lock_ is used to synchronize updates to lease liveness.
  // There are two tasks that change lease liveness:
  //   lease_client_ runs a thread which calls ProcessLeaseUpdate and can flip the lease to live.
  //   check_lease_ runs a task which calls CheckLeaseStatus and can flip the lease to expired.
  TabletServer& server_;
  mutable rw_spinlock lock_;
  tserver::TSLocalLockManagerPtr ts_local_lock_manager_ GUARDED_BY(lock_);
  std::unique_ptr<YsqlLeaseClient> lease_client_;
  rpc::ScheduledTaskTracker check_lease_;
  // Held while toggling the lease from expired to live and from live to expired.
  mutable Mutex lease_toggle_lock_;
  CoarseTimePoint lease_expiry_time_ GUARDED_BY(lease_toggle_lock_);
  bool lease_is_live_ GUARDED_BY(lock_){false};
  uint64_t lease_epoch_ GUARDED_BY(lock_){0};
};

// ============================================================================
// Class YSQLLeaseManager::Impl, public methods
// ============================================================================

YSQLLeaseManager::Impl::Impl(
    TabletServer& server, YSQLLeaseManager& interface,
    server::MasterAddressesPtr master_addresses)
    : server_(server),
      lease_client_(std::make_unique<YsqlLeaseClient>(server, interface, master_addresses)),
      check_lease_("check_ysql_lease_liveness", &server_.messenger()->scheduler()) {
  ScheduleCheckLeaseWithNoLiveLease();
}

Status YSQLLeaseManager::Impl::ProcessLeaseUpdate(
    const master::RefreshYsqlLeaseInfoPB& lease_refresh_info) {
  VLOG(2) << __func__;
  // We hold lease_toggle_lock_ for function scope to sychronize with the lease checker.
  // We do not hold lock_ for function scope so that RPCs are not blocked for too long.
  MutexLock l(lease_toggle_lock_);
  lease_expiry_time_ =
      CoarseTimePoint{std::chrono::milliseconds(lease_refresh_info.lease_expiry_time_ms())};
  if (lease_expiry_time_ < CoarseMonoClock::Now()) {
    // This function is passed the timestamp from before the RefreshYsqlLeaseRpc is sent.  So it
    // is possible the RPC takes longer than the lease TTL the master gave us, in which case
    // this tserver still does not have a live lease.
    return Status::OK();
  }
  bool restart_pg = false;
  {
    std::lock_guard lock(lock_);
    if (!lease_is_live_ || lease_refresh_info.new_lease() ||
        lease_epoch_ != lease_refresh_info.lease_epoch()) {
      LOG_IF(
          INFO, lease_refresh_info.new_lease() || lease_epoch_ != lease_refresh_info.lease_epoch())
          << Format(
                 "Received new lease epoch $0 from the master leader. Clearing all pg sessions.",
                 lease_refresh_info.lease_epoch());
      LOG_IF(INFO, !lease_refresh_info.new_lease() && !lease_is_live_) << Format(
          "Master leader refreshed our lease for epoch $0. We thought this lease had "
          "expired but it hadn't. Restarting pg.",
          lease_epoch_);
      restart_pg = true;
    }
    lease_is_live_ = true;
    lease_epoch_ = lease_refresh_info.lease_epoch();
  }

  // It is safer to end the pg-sessions after resetting the local lock manager.
  // This way, if a new session gets created it will also be reset. But that is better than
  // having it the other way around, and having an old-session that is not reset.
  auto lock_manager = ts_local_lock_manager();
  if (lease_refresh_info.new_lease() && lock_manager) {
    if (lock_manager->IsBootstrapped()) {
      // Reset the local lock manager to bootstrap from the given DDL lock entries.
      lock_manager = ResetAndGetTSLocalLockManager();
    }
    RETURN_NOT_OK(lock_manager->BootstrapDdlObjectLocks(lease_refresh_info.ddl_lock_entries()));
  }
  if (restart_pg) {
    WARN_NOT_OK(server_.RestartPG(), "Failed to restart PG postmaster.");
  }
  return Status::OK();
}

Status YSQLLeaseManager::Impl::StartYSQLLeaseRefresher() { return lease_client_->Start(); }

void YSQLLeaseManager::Impl::StartTSLocalLockManager() {
  std::lock_guard l(lock_);
  StartTSLocalLockManagerUnlocked();
}

void YSQLLeaseManager::Impl::UpdateMasterAddresses(
    const server::MasterAddressesPtr& master_addresses) {
  lease_client_->set_master_addresses(master_addresses);
}

Status YSQLLeaseManager::Impl::Stop() {
  check_lease_.StartShutdown();
  check_lease_.CompleteShutdown();
  auto lock_manager = ts_local_lock_manager();
  if (lock_manager) {
    lock_manager->Shutdown();
  }
  return lease_client_->Stop();
}

std::future<Status> YSQLLeaseManager::Impl::RelinquishLease(MonoDelta timeout) const {
  return lease_client_->RelinquishLease(timeout);
}

YSQLLeaseInfo YSQLLeaseManager::Impl::GetYSQLLeaseInfo() {
  SharedLock lock(lock_);
  YSQLLeaseInfo lease_info;
  lease_info.is_live = lease_is_live_;
  lease_info.lease_epoch = lease_is_live_ ? lease_epoch_ : 0;
  return lease_info;
}

TSLocalLockManagerPtr YSQLLeaseManager::Impl::ts_local_lock_manager() const {
  std::lock_guard l(lock_);
  return ts_local_lock_manager_;
}

// ============================================================================
// Class YSQLLeaseManager::Impl, private methods
// ============================================================================

tserver::TSLocalLockManagerPtr YSQLLeaseManager::Impl::ResetAndGetTSLocalLockManager() {
  if (auto old_lock_manager = ts_local_lock_manager(); old_lock_manager) {
    old_lock_manager->Shutdown();
  }
  std::lock_guard l(lock_);
  StartTSLocalLockManagerUnlocked();
  return ts_local_lock_manager_;
}

void YSQLLeaseManager::Impl::StartTSLocalLockManagerUnlocked() {
  if (server_.options().server_type != TabletServerOptions::kServerType ||
      !FLAGS_enable_object_locking_for_table_locks || !FLAGS_enable_ysql) {
    return;
  }

  ts_local_lock_manager_ = std::make_shared<tserver::TSLocalLockManager>(
      server_.Clock(), &server_ /* TabletServerIf* */, server_ /* RpcServerBase& */,
      server_.tablet_manager()->waiting_txn_pool(), server_.MetricEnt(),
      server_.object_lock_tracker(), server_.object_lock_shared_state_manager());
  ts_local_lock_manager_->Start(server_.tablet_manager()->waiting_txn_registry());
}

void YSQLLeaseManager::Impl::ScheduleCheckLeaseWithNoLiveLease() {
  ScheduleCheckLease(CoarseMonoClock::now() + 1s);
}

void YSQLLeaseManager::Impl::ScheduleCheckLease(CoarseTimePoint next_check_time) {
  check_lease_.Schedule(
      [this, next_check_time](const Status& status) {
        if (!status.ok()) {
          return;
        }
        if (CoarseMonoClock::now() < next_check_time) {
          ScheduleCheckLease(next_check_time);
          return;
        }
        CheckLeaseStatus();
      },
      next_check_time - CoarseMonoClock::now());
}

void YSQLLeaseManager::Impl::CheckLeaseStatus() {
  if (PREDICT_FALSE(!FLAGS_TEST_enable_ysql_operation_lease_expiry_check)) {
    ScheduleCheckLeaseWithNoLiveLease();
    return;
  }
  auto lease_expiry = CheckLeaseStatusInner();
  if (lease_expiry) {
    ScheduleCheckLease(*lease_expiry);
  } else {
    ScheduleCheckLeaseWithNoLiveLease();
  }
}

std::optional<CoarseTimePoint> YSQLLeaseManager::Impl::CheckLeaseStatusInner() {
  // We hold lease_toggle_lock_ for function scope to sychronize with the lease refresher.
  // We do not hold lock_ for function scope so that RPCs are not blocked for too long.
  MutexLock l(lease_toggle_lock_);
  {
    std::lock_guard lock(lock_);
    if (!lease_is_live_) {
      return {};
    }
    if (CoarseMonoClock::now() < lease_expiry_time_) {
      return lease_expiry_time_;
    }
    lease_is_live_ = false;
  }
  // todo(zdrudi): make this a fatal?
  LOG(INFO) << "Lease has expired, killing pg sessions.";
  WARN_NOT_OK(server_.KillPg(), "Couldn't stop PG");
  return {};
}

// ============================================================================
// Class YSQLLeaseManager, public methods
// ============================================================================

YSQLLeaseManager::YSQLLeaseManager(
    TabletServer& server, server::MasterAddressesPtr master_addresses)
    : impl_(std::make_unique<YSQLLeaseManager::Impl>(server, *this, master_addresses)) {}

YSQLLeaseManager::~YSQLLeaseManager() = default;

YSQLLeaseInfo YSQLLeaseManager::GetYSQLLeaseInfo() const { return impl_->GetYSQLLeaseInfo(); }

Status YSQLLeaseManager::StartYSQLLeaseRefresher() { return impl_->StartYSQLLeaseRefresher(); }

void YSQLLeaseManager::StartTSLocalLockManager() {
  return impl_->StartTSLocalLockManager();
}

Status YSQLLeaseManager::ProcessLeaseUpdate(
    const master::RefreshYsqlLeaseInfoPB& lease_refresh_info) {
  return impl_->ProcessLeaseUpdate(lease_refresh_info);
}

void YSQLLeaseManager::UpdateMasterAddresses(const server::MasterAddressesPtr& master_addresses) {
  return impl_->UpdateMasterAddresses(master_addresses);
}

Status YSQLLeaseManager::Stop() { return impl_->Stop(); }

std::future<Status> YSQLLeaseManager::RelinquishLease(MonoDelta timeout) const {
  return impl_->RelinquishLease(timeout);
}

TSLocalLockManagerPtr YSQLLeaseManager::ts_local_lock_manager() const {
  return impl_->ts_local_lock_manager();
}

}  // namespace yb::tserver
