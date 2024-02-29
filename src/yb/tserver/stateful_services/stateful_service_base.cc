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

#include "yb/tserver/stateful_services/stateful_service_base.h"

#include <chrono>

#include "yb/client/session.h"

#include "yb/consensus/consensus.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/bind_helpers.h"

#include "yb/master/master_defaults.h"

#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/callsite_profiling.h"
#include "yb/util/logging.h"
#include "yb/util/string_case.h"
#include "yb/util/unique_lock.h"

using namespace std::chrono_literals;

DEFINE_RUNTIME_uint32(stateful_service_periodic_task_interval_ms, 10000,
    "Frequency of the stateful service periodic task. 0 indicates that the task should not run.");

namespace yb {
using tablet::TabletPeerPtr;

namespace stateful_service {

#define RETURN_IF_SHUTDOWN \
  do { \
    if (shutdown_) { \
      return; \
    } \
  } while (0)

#define LOG_TASK_IDLE_AND_RETURN_IF(condition, reason) \
  do { \
    if (condition) { \
      VLOG(1) << ServiceName() << " periodic task entering idle mode due to: " << reason; \
      return; \
    } \
  } while (0)

#define LOG_TASK_IDLE_AND_RETURN_IF_SHUTDOWN LOG_TASK_IDLE_AND_RETURN_IF(shutdown_, "Shutdown")

StatefulServiceBase::StatefulServiceBase(
    const StatefulServiceKind service_kind,
    const std::shared_future<client::YBClient*>& client_future)
    : service_name_(StatefulServiceKind_Name(service_kind) + "_service"),
      service_kind_(service_kind),
      table_name_(GetStatefulServiceTableName(service_kind)),
      client_future_(client_future) {}

StatefulServiceBase::~StatefulServiceBase() {
  LOG_IF(DFATAL, initialized_ && !shutdown_) << "Shutdown was not called for " << ServiceName();
}

Status StatefulServiceBase::Init(tserver::TSTabletManager* ts_manager) {
  std::lock_guard lock(task_enqueue_mutex_);
  auto thread_pool_builder = ThreadPoolBuilder(ServiceName() + "PeriodicTask");
  thread_pool_builder.set_max_threads(1);

  RETURN_NOT_OK_PREPEND(
      thread_pool_builder.Build(&thread_pool_),
      Format("Failed to create thread pool for $0", ServiceName()));

  thread_pool_token_ = thread_pool_->NewToken(ThreadPool::ExecutionMode::SERIAL);

  task_interval_ms_flag_callback_reg_ = VERIFY_RESULT(RegisterFlagUpdateCallback(
      &FLAGS_stateful_service_periodic_task_interval_ms, ServiceName(),
      [this]() { StartPeriodicTaskIfNeeded(); }));

  RETURN_NOT_OK(ts_manager->RegisterServiceCallback(
      ServiceKind(), Bind(&StatefulServiceBase::RaftConfigChangeCallback, Unretained(this))));
  initialized_ = true;
  return Status::OK();
}

void StatefulServiceBase::Shutdown() {
  {
    std::lock_guard lock(service_state_mutex_);
    if (shutdown_) {
      return;
    }

    // ProcessTaskPeriodically checks for shutdown under this lock before waiting on
    // task_wait_cond_.
    shutdown_ = true;
  }

  YB_PROFILE(task_wait_cond_.notify_all());

  {
    std::lock_guard lock(task_enqueue_mutex_);
    if (thread_pool_token_) {
      thread_pool_token_->Shutdown();
    }

    if (thread_pool_) {
      thread_pool_->Shutdown();
    }
  }

  task_interval_ms_flag_callback_reg_.Deregister();

  // Wait for any activation tasks to finish before deactivating.
  DoDeactivate();
}

void StatefulServiceBase::RaftConfigChangeCallback(TabletPeerPtr peer) {
  CHECK_NOTNULL(peer);
  RETURN_IF_SHUTDOWN;

  // We cannot perform intensive work or waits in the callback thread. Store the necessary info and
  // schedule task in our own thread pool.
  {
    std::lock_guard lock(service_state_mutex_);
    new_tablet_peer_ = peer;
    raft_config_changed_ = true;
  }

  // Schedule task and run it immediately.
  StartPeriodicTaskIfNeeded();
}

namespace {
bool* GetThreadLocalIsActivator() {
  BLOCK_STATIC_THREAD_LOCAL(bool, is_activation_thread, false);
  return is_activation_thread;
}

class ScopedSetIsActivatorThread {
 public:
  ScopedSetIsActivatorThread() {
    DCHECK(!*GetThreadLocalIsActivator()) << "Nested activation threads are not supported";
    *GetThreadLocalIsActivator() = true;
  }
  ~ScopedSetIsActivatorThread() { *GetThreadLocalIsActivator() = false; }
};
}  // namespace

bool StatefulServiceBase::IsActive() const {
  return !shutdown_ && (is_active_ || *GetThreadLocalIsActivator());
}

Result<int64_t> StatefulServiceBase::GetLeaderTerm() {
  SCHECK(IsActive(), ServiceUnavailable, GetServiceNotActiveErrorStr());

  TabletPeerPtr tablet_peer;
  int64_t leader_term;
  {
    SharedLock lock(service_state_mutex_);

    SCHECK(leader_term_ != OpId::kUnknownTerm, ServiceUnavailable, GetServiceNotActiveErrorStr());
    tablet_peer = tablet_peer_;
    leader_term = leader_term_;
  }

  // Make sure term and lease are still valid.
  SCHECK(
      tablet_peer->LeaderTerm() == leader_term, ServiceUnavailable, GetServiceNotActiveErrorStr());

  return leader_term;
}

void StatefulServiceBase::DoDeactivate() {
  {
    std::lock_guard lock(service_state_mutex_);
    is_active_ = false;
    if (leader_term_ == OpId::kUnknownTerm) {
      return;
    }
    leader_term_ = OpId::kUnknownTerm;
  }

  LOG(INFO) << "Deactivating " << ServiceName();
  DrainForDeactivation();
  Deactivate();
}

void StatefulServiceBase::ActivateOrDeactivateServiceIfNeeded() {
  RETURN_IF_SHUTDOWN;

  TabletPeerPtr new_tablet_peer;
  {
    std::lock_guard lock(service_state_mutex_);
    if (!raft_config_changed_) {
      return;
    }
    raft_config_changed_ = false;
    new_tablet_peer.swap(new_tablet_peer_);
  }

  // If we lost the lease, or new leader was picked during the call to WaitForLeaderLeaseAndGetTerm
  // then we may get kUnknownTerm. RaftConfigChangeCallback will scheduled the next work if we
  // become leader again.
  int64_t new_leader_term = WaitForLeaderLeaseAndGetTerm(new_tablet_peer);

  RETURN_IF_SHUTDOWN;

  {
    SharedLock lock(service_state_mutex_);
    if (leader_term_ == new_leader_term && tablet_peer_ == new_tablet_peer) {
      // No change in local peers state.
      return;
    }
  }

  // Always deactivate even if we are reactivating. Since the term changed a different node could
  // have become active and changed the state.
  DoDeactivate();

  if (new_leader_term != OpId::kUnknownTerm) {
    LOG(INFO) << "Activating " << ServiceName() << " on term " << new_leader_term;
    {
      std::lock_guard lock(service_state_mutex_);
      leader_term_ = new_leader_term;
      tablet_peer_ = new_tablet_peer;
    }

    {
      ScopedSetIsActivatorThread se;
      Activate();
    }
    is_active_ = true;
  }
}

int64_t StatefulServiceBase::WaitForLeaderLeaseAndGetTerm(TabletPeerPtr tablet_peer) {
  if (!tablet_peer) {
    return OpId::kUnknownTerm;
  }

  const auto& tablet_id = tablet_peer->tablet_id();
  auto consensus_result = tablet_peer->GetConsensus();
  if (!consensus_result) {
    VLOG(1) << ServiceName() << " Received notification of tablet leader change "
            << "but tablet no longer running. Tablet ID: " << tablet_id;
    return OpId::kUnknownTerm;
  }
  auto& consensus = consensus_result.get();

  auto leader_status = consensus->CheckIsActiveLeaderAndHasLease();
  // The possible outcomes are:
  // OK: We are leader and have a lease.
  // LeaderNotReadyToServe, LeaderHasNoLease: We are leader but don't have a lease yet.
  // IllegalState: We are not leader.
  if (leader_status.ok() || leader_status.IsLeaderHasNoLease() ||
      leader_status.IsLeaderNotReadyToServe()) {
    VLOG_WITH_FUNC(1) << ServiceName() << " started waiting for leader lease of tablet "
                      << tablet_id;
    CoarseBackoffWaiter waiter(CoarseTimePoint::max(), 1s /* max_wait */, 100ms /* base_delay */);
    while (!shutdown_) {
      auto status =
          consensus->WaitForLeaderLeaseImprecise(CoarseMonoClock::now() + waiter.DelayForNow());
      waiter.NextAttempt();

      // The possible outcomes are:
      // OK: Peer acquired the lease.
      // TimedOut: Peer is still the leader but still waiting for a lease.
      // IllegalState/Other errors: Peer is no longer the leader.
      if (status.ok()) {
        auto term = consensus->LeaderTerm();
        if (term != OpId::kUnknownTerm) {
          VLOG_WITH_FUNC(1) << ServiceName() << " completed waiting for leader lease of tablet "
                            << tablet_id << " with term " << term;
          return term;
        }
        // We either lost the lease or the leader changed. Go back to
        // WaitForLeaderLeaseImprecise to see which one it was.
      } else if (!status.IsTimedOut()) {
        LOG_WITH_FUNC(WARNING) << ServiceName() << " failed waiting for leader lease of tablet "
                               << tablet_id << ": " << status;
        return OpId::kUnknownTerm;
      }

      YB_LOG_EVERY_N_SECS(INFO, 10) << ServiceName() << " waiting for new leader of tablet "
                                    << tablet_id << " to acquire the lease";
    }
    VLOG_WITH_FUNC(1) << ServiceName() << " completed waiting for leader lease of tablet "
                      << tablet_id << " due to shutdown";
  } else {
    VLOG_WITH_FUNC(1) << ServiceName() << " tablet " << tablet_id << " is a follower";
  }

  return OpId::kUnknownTerm;
}

void StatefulServiceBase::StartPeriodicTaskIfNeeded() {
  LOG_TASK_IDLE_AND_RETURN_IF_SHUTDOWN;

  {
    std::lock_guard lock(task_enqueue_mutex_);
    if (task_enqueued_) {
      return;
    }

    CHECK(thread_pool_token_) << "Init must be called before starting the task";

    // It is ok to schedule a new task even when we have a running task. The thread pool token uses
    // serial execution, and the task will sleep before returning. So it is always guaranteed that
    // we only run one task at a time and that it will wait the required amount before running
    // again. The advantage of this model is that we dont hold on to the thread when there is no
    // work to do.
    task_enqueued_ = true;
    Status s = thread_pool_token_->SubmitFunc(
        std::bind(&StatefulServiceBase::ProcessTaskPeriodically, this));
    if (!s.ok()) {
      task_enqueued_ = false;
      LOG(ERROR) << "Failed to schedule " << ServiceName() << " periodic task :" << s;
    }
  }

  // Wake up the thread if its sleeping and process the task immediately.
  YB_PROFILE(task_wait_cond_.notify_all());
}

uint32 StatefulServiceBase::PeriodicTaskIntervalMs() const {
  return GetAtomicFlag(&FLAGS_stateful_service_periodic_task_interval_ms);
}

void StatefulServiceBase::ProcessTaskPeriodically() {
  {
    std::lock_guard lock(task_enqueue_mutex_);
    task_enqueued_ = false;
  }

  LOG_TASK_IDLE_AND_RETURN_IF_SHUTDOWN;

  ActivateOrDeactivateServiceIfNeeded();

  LOG_TASK_IDLE_AND_RETURN_IF(!IsActive(), "Service is no longer active on this node");

  auto wait_time_ms = PeriodicTaskIntervalMs();
  LOG_TASK_IDLE_AND_RETURN_IF(wait_time_ms == 0, "Task interval is 0");

  auto further_computation_needed_result = RunPeriodicTask();

  if (!further_computation_needed_result.ok()) {
    LOG_WITH_FUNC(WARNING) << ServiceName() << " periodic task failed: "
                           << further_computation_needed_result.status();
  } else {
    LOG_TASK_IDLE_AND_RETURN_IF(!further_computation_needed_result.get(), "No more work left");
  }

  // Delay before running the task again.
  {
    UniqueLock lock(service_state_mutex_);
    LOG_TASK_IDLE_AND_RETURN_IF_SHUTDOWN;
    task_wait_cond_.wait_for(GetLockForCondition(&lock), wait_time_ms * 1ms);
  }

  StartPeriodicTaskIfNeeded();
}

Result<std::shared_ptr<client::YBSession>> StatefulServiceBase::GetYBSession(MonoDelta delta) {
  auto session = GetYBClient()->NewSession(delta);
  session->SetLeaderTerm(VERIFY_RESULT(GetLeaderTerm()));
  return session;
}

Result<client::TableHandle*> StatefulServiceBase::GetServiceTable() {
  std::lock_guard l(table_handle_mutex_);

  if (!table_handle_) {
    auto table = std::make_unique<client::TableHandle>();
    RETURN_NOT_OK(table->Open(TableName(), GetYBClient()));
    table_handle_.swap(table);
  }

  return table_handle_.get();
}

client::YBTableName GetStatefulServiceTableName(const StatefulServiceKind& service_kind) {
  return client::YBTableName(
      YQL_DATABASE_CQL, master::kSystemNamespaceName,
      ToLowerCase(StatefulServiceKind_Name(service_kind)) + "_table");
}
}  // namespace stateful_service
}  // namespace yb
