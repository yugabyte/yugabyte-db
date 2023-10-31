// Copyright (c) Yugabyte, Inc.
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

#include "yb/master/ysql_backends_manager.h"

#include <algorithm>
#include <string>
#include <utility>

#include "yb/common/wire_protocol.h"
#include "yb/common/common_flags.h"

#include "yb/consensus/consensus.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/master_util.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_types.pb.h"

#include "yb/util/flags/flag_tags.h"
#include "yb/util/monotime.h"
#include "yb/util/string_util.h"

using namespace std::chrono_literals;

DECLARE_int32(master_ts_rpc_timeout_ms);

DEFINE_RUNTIME_uint32(ysql_backends_catalog_version_job_expiration_sec, 120, // 2m
    "Expiration time in seconds for a YSQL BackendsCatalogVersionJob. Recommended to set several"
    " times higher than tserver wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms"
    " flag.");
TAG_FLAG(ysql_backends_catalog_version_job_expiration_sec, advanced);

DEFINE_test_flag(bool, assert_no_future_catalog_version, false,
    "Asserts that the clients are never requesting a catalog version which is higher "
    "than what is seen at the master. Used to assert that there are no stale master reads.");
DEFINE_test_flag(bool, block_wait_for_ysql_backends_catalog_version, false, // runtime-settable
    "If true, enable toggleable busy-wait at the beginning of WaitForYsqlBackendsCatalogVersion.");
DEFINE_test_flag(bool, wait_for_ysql_backends_catalog_version_take_leader_lock, true,
    "Take leader lock in WaitForYsqlBackendsCatalogVersion.");

namespace yb {
namespace master {

using server::MonitoredTask;
using server::MonitoredTaskState;
using tserver::TabletServerErrorPB;

YsqlBackendsManager::YsqlBackendsManager(
    Master* master, ThreadPool* callback_pool)
    : master_(master), callback_pool_(callback_pool) {
}

namespace {

// Check the leadership (and initialization of catalog).  Return ok if those conditions are
// satisfied.  If not satisfied, fill the response error code and then return status.
Status CheckLeadership(
    ScopedLeaderSharedLock* l, WaitForYsqlBackendsCatalogVersionResponsePB* resp) {
  if (!l->IsInitializedAndIsLeader()) {
    // Do the same thing as case !l.CheckIsInitializedAndIsLeaderOrRespond(resp, rpc) in
    // MasterServiceBase::HandleOnLeader.
    resp->mutable_error()->set_code(MasterErrorPB::NOT_THE_LEADER);
    return l->first_failed_status();
  }
  return Status::OK();
}

} // namespace

Status YsqlBackendsManager::AccessYsqlBackendsManagerTestRegister(
    const AccessYsqlBackendsManagerTestRegisterRequestPB* req,
    AccessYsqlBackendsManagerTestRegisterResponsePB* resp) {
  LOG_WITH_FUNC(INFO) << req->ShortDebugString();

  if (!req->has_value()) {
    // Read mode.
    resp->set_value(test_register_.load());
  } else {
    // Write mode.
    resp->set_value(test_register_.exchange(req->value()));
  }

  return Status::OK();
}

// This function is called with HANDLE_ON_LEADER_WITHOUT_LOCK, which eventually calls HandleOnLeader
// with hold_catalog_lock=false.  This means it acquires the leader lock to check leadership and
// catalog loaded before calling this function, but it also releases the lock right before calling
// this function.  This function does not depend on leader lock except for two small cases, which
// re-acquire the lock.  It is worth not acquiring the lock for the whole function because it could
// take a long time.
Status YsqlBackendsManager::WaitForYsqlBackendsCatalogVersion(
    const WaitForYsqlBackendsCatalogVersionRequestPB* req,
    WaitForYsqlBackendsCatalogVersionResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG_WITH_FUNC(INFO) << req->ShortDebugString();

  if (PREDICT_FALSE(FLAGS_TEST_block_wait_for_ysql_backends_catalog_version)) {
    TestDoBlock(1 /* id */, __func__, __LINE__);
  }

  const PgOid db_oid = req->database_oid();
  const Version version = req->catalog_version();
  const DbVersion db_version = std::make_pair(db_oid, version);
  const auto LogPrefix = [&db_version]() -> std::string {
    return Format("[DB $0, V $1]: ", db_version.first, db_version.second);
  };

  Version master_version;
  {
    SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());
    if (PREDICT_FALSE(!FLAGS_TEST_wait_for_ysql_backends_catalog_version_take_leader_lock)) {
      l.Unlock();
    } else {
      RETURN_NOT_OK(CheckLeadership(&l, resp));
    }
    Status s;
    // TODO(jason): using the gflag to determine per-db mode may not work for initdb, so make sure
    // to handle that case if initdb ever goes through this codepath.
    if (FLAGS_ysql_enable_db_catalog_version_mode) {
      s = master_->catalog_manager_impl()->GetYsqlDBCatalogVersion(
          db_oid, &master_version, nullptr /* last_breaking_version */);
    } else {
      s = master_->catalog_manager_impl()->GetYsqlCatalogVersion(
          &master_version, nullptr /* last_breaking_version */);
    }
    if (!s.ok()) {
      return SetupError(resp->mutable_error(), s);
    }
  }
  if (master_version < version) {
    LOG_IF(FATAL, FLAGS_TEST_assert_no_future_catalog_version)
        << "Possible stale read by the master ."
        << " master_version " << master_version << " client expected version " << version;
    return SetupError(
        resp->mutable_error(),
        STATUS_FORMAT(
          InvalidArgument,
          "Requested catalog version is too high: req version $0, master version $1",
          version,
          master_version));
  }

  if (PREDICT_FALSE(FLAGS_TEST_block_wait_for_ysql_backends_catalog_version)) {
    TestDoBlock(2 /* id */, __func__, __LINE__);
  }

  std::shared_ptr<BackendsCatalogVersionJob> job;
  {
    std::lock_guard l(mutex_);

    // If it is already known that backends catalog version is sufficient, we're done.
    auto iter = latest_known_versions_.find(db_oid);
    if (iter != latest_known_versions_.end() && iter->second >= version) {
      VLOG_WITH_PREFIX_AND_FUNC(1) << "using cached result";
      resp->set_num_lagging_backends(0);
      return Status::OK();
    }

    // Get the job corresponding to that version if it exists; otherwise, create a new job.
    auto iter2 = jobs_.find(db_version);
    if (iter2 != jobs_.end()) {
      VLOG_WITH_PREFIX_AND_FUNC(1) << "found existing job";
      job = iter2->second;
      job->Touch();
    } else {
      job = std::make_shared<BackendsCatalogVersionJob>(
          master_, callback_pool_, req->database_oid(), version);
      jobs_[db_version] = job;
    }
  }

  Result<int> res = HandleJobRequest(
      job,
      rpc->GetClientDeadline() - MonoDelta::FromMilliseconds(
        FLAGS_wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms),
      resp);
  if (res.ok()) {
    resp->set_num_lagging_backends(*res);
    return Status::OK();
  }
  if (!res.ok() && !resp->mutable_error()->has_code()) {
    return SetupError(resp->mutable_error(), res.status());
  }
  return res.status();
}

Result<int> YsqlBackendsManager::HandleJobRequest(
    std::shared_ptr<BackendsCatalogVersionJob> job,
    const CoarseTimePoint& deadline,
    WaitForYsqlBackendsCatalogVersionResponsePB* resp) {
  const auto state = job->state();
  switch (state) {
    case MonitoredTaskState::kComplete: FALLTHROUGH_INTENDED;
    case MonitoredTaskState::kAborted: FALLTHROUGH_INTENDED;
    case MonitoredTaskState::kFailed: {
      return job->HandleTerminalState();
    }
    case MonitoredTaskState::kWaiting: {
      if (PREDICT_FALSE(FLAGS_TEST_block_wait_for_ysql_backends_catalog_version)) {
        TestDoBlock(3 /* id */, __func__, __LINE__);
      }
      // State could have changed since it was read, so protect with CAS.  For example, two threads
      // concurrently running these two lines in lockstep:
      // const auto state = job->state();
      // if (job->CompareAndSwapState(MonitoredTaskState::kWaiting, MonitoredTaskState::kRunning)) {
      if (job->CompareAndSwapState(MonitoredTaskState::kWaiting, MonitoredTaskState::kRunning)) {
        // In case of failure before successful Launch, job should be aborted to avoid getting stuck
        // in Running state without all master-tserver RPCs launched.
        Status s = HandleSwapToRunning(job, resp);
        if (!s.ok()) {
          TerminateJob(job, MonitoredTaskState::kAborted);
          return s;
        }
      }
      if (PREDICT_FALSE(FLAGS_TEST_block_wait_for_ysql_backends_catalog_version)) {
        TestDoBlock(4 /* id */, __func__, __LINE__);
        // Wait out the period for catalog manager bg task to AbortAllJobs so that the following
        // WaitAndGetNumLaggingBackends will operate on an aborted (and cleared) job.
        SleepFor(MonoDelta::FromMilliseconds(FLAGS_catalog_manager_bg_task_wait_ms * 2));
      }
      return job->WaitAndGetNumLaggingBackends(deadline);
    }
    case MonitoredTaskState::kRunning: {
      // The job is already running, so nothing to do.
      return job->WaitAndGetNumLaggingBackends(deadline);
    }
    case MonitoredTaskState::kScheduling: {
      return STATUS_FORMAT(InternalError, "Unexpected state $0", state);
    }
  }
  LOG(FATAL) << "Internal error: unsupported state " << state;
}

Status YsqlBackendsManager::HandleSwapToRunning(
    std::shared_ptr<BackendsCatalogVersionJob> job,
    WaitForYsqlBackendsCatalogVersionResponsePB* resp) {
  SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());

  auto epoch = l.epoch();
  if (job->state() != MonitoredTaskState::kRunning) {
    // The only reason for this to happen is if catalog manager bg thread does AbortAllJobs or
    // AbortInactiveJobs between CompareAndSwapState and SCOPED_LEADER_SHARED_LOCK above.  Any
    // other state is unexpected.
    DCHECK_EQ(job->state(), MonitoredTaskState::kAborted) << "Unexpected state change";
    return job->HandleTerminalState().status();
  }

  if (PREDICT_FALSE(!FLAGS_TEST_wait_for_ysql_backends_catalog_version_take_leader_lock)) {
    l.Unlock();
    // This unlocking could have some contention between catalog manager bg thread and this
    // thread.  For example, catalog manager bg thread could do one-time cleanup of jobs
    // tracker and then we add the job to the jobs tracker, meaning that job would be missed
    // by the one-time cleanup.  But this is a test flag, so don't worry about it.
  } else {
    RETURN_NOT_OK(CheckLeadership(&l, resp));
  }
  master_->catalog_manager_impl()->jobs_tracker_->AddTask(job);
  RETURN_NOT_OK(job->Launch(epoch));
  return Status::OK();
}

void YsqlBackendsManager::ClearJobIfCached(std::shared_ptr<BackendsCatalogVersionJob> job) {
  std::lock_guard l(mutex_);
  ClearJobIfCachedUnlocked(job);
}

void YsqlBackendsManager::ClearJobIfCachedUnlocked(std::shared_ptr<BackendsCatalogVersionJob> job) {
  const DbVersion db_version = std::make_pair(job->database_oid(), job->target_version());
  const auto LogPrefix = [&db_version]() -> std::string {
    return Format("[DB $0, V $1]: ", db_version.first, db_version.second);
  };

  auto iter = jobs_.find(db_version);
  if (iter != jobs_.end() && iter->second == job) {
    VLOG_WITH_PREFIX(1) << "clear job";
    jobs_.erase(db_version);
  }
}

// Update job to terminal target_state, and do any followup work if applicable.
//
// Requests can come concurrently: the first one wins. A common example of concurrent Update is a
// Complete/Fail and Abort, since Abort can happen at any time.
void YsqlBackendsManager::TerminateJob(
    std::shared_ptr<BackendsCatalogVersionJob> job,
    MonitoredTaskState target_state,
    const Status& status) {
  const DbVersion db_version = std::make_pair(job->database_oid(), job->target_version());
  const auto LogPrefix = [&db_version]() -> std::string {
    return Format("[DB $0, V $1]: ", db_version.first, db_version.second);
  };

  if (FLAGS_TEST_block_wait_for_ysql_backends_catalog_version &&
      target_state == MonitoredTaskState::kComplete) {
    TestDoBlock(5 /* id */, __func__, __LINE__);
  }

  bool did_swap = false;
  if (job->CompareAndSwapState(MonitoredTaskState::kRunning, target_state)) {
    VLOG_WITH_PREFIX(1) << "set job state: " << target_state;
    did_swap = true;
  }

  switch (target_state) {
    case MonitoredTaskState::kComplete:
      if (did_swap) {
        std::lock_guard l(mutex_);

        // Erase job from jobs_ cache.
        ClearJobIfCachedUnlocked(job);

        // Update latest_known_versions_ cache if possible.
        auto iter = latest_known_versions_.find(job->database_oid());
        if (iter == latest_known_versions_.end() || iter->second < job->target_version()) {
          latest_known_versions_[job->database_oid()] = job->target_version();
          LOG_WITH_PREFIX(INFO) << "updated latest known backends catalog version to "
                                << job->target_version();
        }
      }
      return;
    case MonitoredTaskState::kFailed:
      VLOG_WITH_PREFIX(1) << "adding job failure: " << status;
      // Unconditionally add failure status.  If the job is actually set to complete state by a
      // different thread, we won't look at failure statuses anyway.
      job->AddFailureStatus(status);
      return;
    case MonitoredTaskState::kAborted:
      if (did_swap) {
        std::lock_guard l(mutex_);

        // Erase job from jobs_ cache.
        ClearJobIfCachedUnlocked(job);
      }
      return;
    case MonitoredTaskState::kRunning:
    case MonitoredTaskState::kScheduling:
    case MonitoredTaskState::kWaiting:
      break;
  }
  LOG_WITH_PREFIX(FATAL) << "internal error: unsupported state " << target_state;
}

void YsqlBackendsManager::AbortAllJobs() {
  std::vector<std::shared_ptr<BackendsCatalogVersionJob>> jobs;
  {
    std::lock_guard l(mutex_);
    // Make a copy of jobs.
    std::transform(
        jobs_.begin(),
        jobs_.end(),
        std::back_inserter(jobs),
        [](const std::pair<DbVersion, std::shared_ptr<BackendsCatalogVersionJob>>& p) {
          return p.second;
        });
  }
  AbortJobs(std::move(jobs));
}

void YsqlBackendsManager::AbortInactiveJobs() {
  std::vector<std::shared_ptr<BackendsCatalogVersionJob>> inactive_jobs;
  {
    std::lock_guard l(mutex_);
    for (const auto& entry : jobs_) {
      const auto& job = entry.second;
      if (job->IsInactive()) {
        inactive_jobs.push_back(job);
      }
    }
  }
  AbortJobs(std::move(inactive_jobs));
}

void YsqlBackendsManager::AbortJobs(
    const std::vector<std::shared_ptr<BackendsCatalogVersionJob>>& jobs) {
  for (auto job : jobs) {
    // It is possible that the job is in kWaiting state, so this becomes a no-op.  That is fine
    // because the client request thread should make progress on the request and either use a newer
    // leader term if we are still the leader or return leader changed error (see HandleJobRequest).
    // It is also possible that catalog manager thread takes leader lock right after the client
    // request thread turns the state to kRunning.  In that case, this thread would be aborting the
    // job, and the client request thread would recognize that once it takes the leader lock.
    TerminateJob(job, MonitoredTaskState::kAborted);
  }
}

// Upon entry to this function,
// - test_register_ == id means block: test_register_ will be set to -id to signify it is blocked.
// - otherwise, unblocked.
// - test_register_ == 0 is reserved for unblocked since it is the initial value.
// When blocked,
// - test_register_ == -id means still blocked.
// - otherwise, unblocked.
//
// For example, given test_register_ = id = 123, it will get blocked and set test_register_ = -id =
// -123.
// - If we want to unblock and immediately block id=456, set test_register_ to 456.
// - If we want to unblock and immediately block id=123, set test_register_ to 123.
// - If we want to unblock indefinitely, set test_register_ to 0.
void YsqlBackendsManager::TestDoBlock(int id, const char* func, int line) {
  // id should not be set to a reserved value.
  DCHECK_NE(id, 0);

  auto expected = id;
  if (!test_register_.compare_exchange_strong(expected, -id)) {
    // Unsuccessful exchange; test_register_ != expected == id.
    LOG(INFO) << "Not blocking id=" << id;
    return;
  }

  while (test_register_.load() == -id) {
    constexpr auto kSpinWait = 100ms;
    LOG(INFO) << Format("Blocking on id=$0 for $1 at $2:$3", id, kSpinWait, func, line);
    SleepFor(kSpinWait);
  }
  LOG_WITH_FUNC(INFO) << "Done blocking id=" << id;
}

std::string BackendsCatalogVersionJob::description() const {
  std::string base_msg = Format(
      "Wait for YSQL backends connected to database $0 to have catalog version $1",
      database_oid_, target_version_);
  if (IsStateTerminal(state())) {
    return base_msg;
  }
  const int num_lagging_backends = GetNumLaggingBackends();
  return Format("$0: $1",
                base_msg,
                (num_lagging_backends == -1) ? "pending..." : Format("$0 backends remaining",
                                                                     num_lagging_backends));
}

MonitoredTaskState BackendsCatalogVersionJob::AbortAndReturnPrevState(const Status& status) {
  // At the time of writing D19621, there is no code path that reaches here.  This function is
  // implemented because it is needed for the superclass.
  LOG_WITH_PREFIX_AND_FUNC(DFATAL) << "should not reach here";

  auto old_state = state();
  while (!IsStateTerminal(old_state)) {
    if (CompareAndSwapState(old_state, MonitoredTaskState::kAborted)) {
      return old_state;
    }
    old_state = state();
  }
  return old_state;
}

Status BackendsCatalogVersionJob::Launch(LeaderEpoch epoch) {
  LOG_WITH_PREFIX_AND_FUNC(INFO) << "launching tserver RPCs";

  const auto& descs = master_->ts_manager()->GetAllDescriptors();
  // If any new tservers join after this point, they should have up-to-date catalog version.

  std::vector<std::string> ts_uuids;
  ts_uuids.reserve(descs.size());
  {
    // Lock throughout the whole map initialization to prevent readers from viewing a map without
    // all tservers.
    std::lock_guard l(mutex_);

    // Commit term now.
    epoch_ = epoch;

    for (const auto& ts_desc : descs) {
      if (!ts_desc->IsLive()) {
        // Ignore dead tservers since they should be resolved.
        // TODO(#13369): ensure dead tservers abort/block ops until they successfully heartbeat.
        continue;
      }

      const std::string& ts_uuid = ts_desc->permanent_uuid();
      ts_uuids.emplace_back(ts_uuid);
      ts_map_[ts_uuid] = -1;
    }
  }

  for (const auto& ts_uuid : ts_uuids) {
    RETURN_NOT_OK(LaunchTS(ts_uuid, -1 /* num_lagging_backends */, epoch));
  }

  return Status::OK();
}

Status BackendsCatalogVersionJob::LaunchTS(
    TabletServerId ts_uuid, int num_lagging_backends, const LeaderEpoch& epoch) {
  auto task = std::make_shared<BackendsCatalogVersionTS>(
      shared_from_this(), ts_uuid, num_lagging_backends, epoch);
  Status s = threadpool()->SubmitFunc([this, &ts_uuid, task]() {
    Status s = task->Run();
    if (!s.ok()) {
      LOG_WITH_PREFIX(WARNING) << "got bad status " << s.ToString()
                               << " trying to run RPC for TS " << ts_uuid;
      master_->ysql_backends_manager()->TerminateJob(
          shared_from_this(), MonitoredTaskState::kFailed, s);
    }
  });
  if (!s.ok()) {
    LOG_WITH_PREFIX(WARNING) << "got bad status " << s.ToString()
                             << " trying to launch TS " << ts_uuid;
    master_->ysql_backends_manager()->TerminateJob(
        shared_from_this(), MonitoredTaskState::kFailed, s);
    return s;
  }
  return Status::OK();
}

bool BackendsCatalogVersionJob::IsInactive() const {
  const auto time_since_last_access = CoarseMonoClock::Now() - last_access_;
  if (time_since_last_access >
      MonoDelta::FromSeconds(FLAGS_ysql_backends_catalog_version_job_expiration_sec)) {
    VLOG_WITH_PREFIX(2) << "job is inactive";
    return true;
  }
  return false;
}

bool BackendsCatalogVersionJob::IsSameTerm() const {
  const int64_t term = master_->catalog_manager()->leader_ready_term();
  std::lock_guard l(mutex_);
  if (epoch_.leader_term == term) {
    VLOG_WITH_PREFIX(3) << "Sys catalog term is " << term;
    return true;
  }
  LOG_WITH_PREFIX(INFO) << "Sys catalog term is " << term << ", job term is " << epoch_.leader_term;
  return false;
}

Result<int> BackendsCatalogVersionJob::WaitAndGetNumLaggingBackends(
    const CoarseTimePoint& deadline) {
  {
    std::lock_guard l(state_mutex_);
    if (!MonitoredTask::IsStateTerminal(state())) {
      if (state_cv_.WaitUntil(ToSteady(deadline))) {
        return HandleTerminalState();
      }
    }
  }
  if (IsStateTerminal(state())) {
    return HandleTerminalState();
  }
  return GetNumLaggingBackends();
}

Result<int> BackendsCatalogVersionJob::HandleTerminalState() {
  const server::MonitoredTaskState terminal_state = state();
  DCHECK(MonitoredTask::IsStateTerminal(terminal_state)) << terminal_state;
  switch (terminal_state) {
    case MonitoredTaskState::kComplete:
      // No need to clear job from jobs_ cache: this will be done by the thread that updated the
      // state to complete.
      return 0;
    case MonitoredTaskState::kFailed:
      // Clear the job from the jobs_ cache so that next client request to wait on the same db and
      // version will create a new job.  For this client, return bad status so that it knows not to
      // blindly resend the request.
      //
      // It is possible for multiple threads to hit this, leading to multiple bad statuses returned.
      // That is desirable because we want current clients to know there's an issue.  The guarantee
      // is that at least one client will get the bad status: another ongoing client may find the
      // job already cleared on next request, so it may spawn a new job and eventually reach failed
      // state. So in case the failed state is a long-lasting issue, we are guaranteed to notify all
      // clients eventually.
      master_->ysql_backends_manager()->ClearJobIfCached(shared_from_this());

      return STATUS_FORMAT(
          Combined, "job is failed, statuses: $0", VectorToString(failure_statuses()));
    case MonitoredTaskState::kAborted:
      // No need to clear job from jobs_ cache: this will be done by the thread that updated the
      // state to aborted.
      //
      // Since kAborted only happens on leadership loss or expired job, it is retryable (on new
      // master).  So send back a status that client will interpret as retryable (see
      // ClientMasterRpcBase::Finished).
      return STATUS(RemoteError, "job is aborted");
    case MonitoredTaskState::kScheduling: FALLTHROUGH_INTENDED;
    case MonitoredTaskState::kWaiting: FALLTHROUGH_INTENDED;
    case MonitoredTaskState::kRunning:
      break;
  }
  LOG_WITH_PREFIX_AND_FUNC(DFATAL) << "unreachable";
  return STATUS(InternalError, "unreachable");
}

// Update the job with progress from a tserver task.
//
// Requests for each tserver cannot come in concurrently, but requests across tservers can.
void BackendsCatalogVersionJob::Update(TabletServerId ts_uuid, Result<int> num_lagging_backends) {
  if (!IsSameTerm()) {
    master_->ysql_backends_manager()->TerminateJob(
        shared_from_this(), MonitoredTaskState::kAborted);
    return;
  }

  // Check for bad status.
  if (!num_lagging_backends.ok()) {
    auto s = num_lagging_backends.status();
    if (s.IsTryAgain()) {
      int last_known_num_lagging_backends;
      LeaderEpoch epoch;
      {
        std::lock_guard l(mutex_);
        last_known_num_lagging_backends = ts_map_[ts_uuid];
        epoch = epoch_;
      }
      // Ignore returned status since it is already logged/handled.
      (void)LaunchTS(ts_uuid, last_known_num_lagging_backends, epoch);
    } else {
      LOG_WITH_PREFIX(WARNING) << "got bad status " << s.ToString() << " from TS " << ts_uuid;
      master_->ysql_backends_manager()->TerminateJob(
          shared_from_this(), MonitoredTaskState::kFailed, s);
    }
    return;
  }
  DCHECK_GE(*num_lagging_backends, 0);

  // Update num_lagging_backends.
  LeaderEpoch epoch;
  {
    std::lock_guard l(mutex_);
    epoch = epoch_;

#ifndef NDEBUG
    if (ts_map_[ts_uuid] != -1) {
      // The number of backends on an old version should never increase because backends should
      // never go to earlier catalog versions.
      DCHECK(*num_lagging_backends <= ts_map_[ts_uuid]);
    }
#endif
    ts_map_[ts_uuid] = *num_lagging_backends;
  }

  // Retry the wait for this tserver if it has lagging backends.
  if (*num_lagging_backends > 0) {
    VLOG_WITH_PREFIX(2) << "still waiting on " << *num_lagging_backends << " backends of TS "
                        << ts_uuid;
    // Ignore returned status since it is already logged/handled.
    (void)LaunchTS(ts_uuid, *num_lagging_backends, epoch);
    return;
  }
  DCHECK_EQ(*num_lagging_backends, 0);
  VLOG_WITH_PREFIX(1) << "done waiting on backends of TS " << ts_uuid;

  // Report to the manager if all tservers have no lagging backends.
  if (GetNumLaggingBackends() == 0) {
    master_->ysql_backends_manager()->TerminateJob(
        shared_from_this(), MonitoredTaskState::kComplete);
  }
}

// Get total number of lagging backends across tservers.  Caller is encouraged to first check
// whether the job went into a terminal state.  It is possible for the job to go into a terminal
// state before or in the middle of this function, but the result isn't necessarily incorrect:
// - if it went to kComplete state, we may return slightly old result where there are still num
//   lagging backends, or we may return zero result.
// - if it went to kAborted or kFailed state, it most likely would return a result > 0.  Only case
//   it could return zero result is if kAborted right after job Update to zero count but before
//   manager Update of job to kComplete state, but in that case, the zero result is accurate.
int BackendsCatalogVersionJob::GetNumLaggingBackends() const {
  std::lock_guard l(mutex_);

  if (ts_map_.size() == 0) {
    // This can happen if the job just got swapped from waiting to running and did not yet populate
    // the ts map.  Also, of course, it is possible for the job to become aborted at any point.
    DCHECK(state() == MonitoredTaskState::kRunning || state() == MonitoredTaskState::kAborted);
    return -1;
  }

  int total_num_lagging_backends = 0;
  for (const auto& entry : ts_map_) {
    int num_lagging_backends = entry.second;
    if (num_lagging_backends == -1) {
      // If the number on one tserver is unknown, then the total number is unknown.
      return -1;
    }
    DCHECK_GE(num_lagging_backends, 0);
    total_num_lagging_backends += num_lagging_backends;
  }
  return total_num_lagging_backends;
}

// Compare-and-swap state.
bool BackendsCatalogVersionJob::CompareAndSwapState(
    server::MonitoredTaskState old_state,
    server::MonitoredTaskState new_state) {
  std::lock_guard l(state_mutex_);
  if (state_.compare_exchange_strong(old_state, new_state)) {
    if (IsStateTerminal(new_state)) {
      completion_timestamp_ = MonoTime::Now();
      state_cv_.Broadcast();
    }
    return true;
  }
  return false;
}

std::string BackendsCatalogVersionJob::LogPrefix() const {
  return Format("[DB $0, V $1]: ", database_oid_, target_version_);
}

BackendsCatalogVersionTS::BackendsCatalogVersionTS(
    std::shared_ptr<BackendsCatalogVersionJob> job,
    const std::string& ts_uuid,
    int prev_num_lagging_backends, LeaderEpoch epoch)
    : RetryingTSRpcTask(job->master(),
                        job->threadpool(),
                        std::unique_ptr<TSPicker>(new PickSpecificUUID(job->master(), ts_uuid)),
                        nullptr /* table */,
                        std::move(epoch),
                        nullptr /* async_task_throttler */),
      job_(job),
      prev_num_lagging_backends_(prev_num_lagging_backends) {
  DCHECK_NE(ts_uuid, "") << LogPrefix();
}

std::string BackendsCatalogVersionTS::description() const {
  return "BackendsCatalogVersionTS RPC";
}

TabletServerId BackendsCatalogVersionTS::permanent_uuid() const {
  return target_ts_desc_ != nullptr ? target_ts_desc_->permanent_uuid() : "";
}

bool BackendsCatalogVersionTS::SendRequest(int attempt) {
  tserver::WaitForYsqlBackendsCatalogVersionRequestPB req;
  {
    auto job = job_.lock();
    req.set_database_oid(job->database_oid());
    req.set_catalog_version(job->target_version());
    req.set_prev_num_lagging_backends(prev_num_lagging_backends_);
  }

  ts_admin_proxy_->WaitForYsqlBackendsCatalogVersionAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG(1) << "Send " << description() << " to " << permanent_uuid()
          << " (attempt " << attempt << "): " << req.ShortDebugString();
  return true;
}

void BackendsCatalogVersionTS::HandleResponse(int attempt) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << resp_.ShortDebugString();

  // First, check if the tserver is considered dead.
  if (!target_ts_desc_->IsLive()) {
    // A similar check is done in RetryingTSRpcTask::DoRpcCallback.  That check is hit when this RPC
    // failed and tserver is dead.  This check is hit when this RPC succeeded and tserver is dead.
    // TODO(#13369): ensure dead tservers abort/block ops until they successfully heartbeat.
    LOG_WITH_PREFIX(WARNING)
        << "TS " << permanent_uuid() << " is DEAD. Assume backends on that TS"
        << " will be resolved to sufficient catalog version";
    TransitionToCompleteState();
    found_dead_ = true;
    return;
  }

  if (resp_.has_error()) {
    const auto status = StatusFromPB(resp_.error().status());

    bool should_retry = true;
    switch (resp_.error().code()) {
      case TabletServerErrorPB::UNKNOWN_ERROR:
        if (status.IsNetworkError()) {
          if (status.message().ToBuffer().find("sorry, too many clients already") !=
              std::string::npos) {
            // Too many clients means the node is overloaded with connections, so don't add to the
            // load.
            should_retry = false;
          }
          if (status.message().ToBuffer().find("column \"catalog_version\" does not exist") !=
              std::string::npos) {
            // This likely means ysql upgrade hasn't happened yet.  Succeed for backwards
            // compatibility.
            TransitionToCompleteState();
            LOG_WITH_PREFIX(INFO)
                << "wait for backends catalog version failed: " << status
                << ", but ignoring for backwards compatibility";
            found_behind_ = true;
            return;
          }
        }
        break;
      case TabletServerErrorPB::OPERATION_NOT_SUPPORTED:
        // If the server responds with OPERATION_NOT_SUPPORTED, there is a non-transient issue.  For
        // example, the request may be invalid.
        should_retry = false;
        break;
      default:
        break;
    }
    if (should_retry) {
      LOG_WITH_PREFIX(WARNING)
          << "wait for backends catalog version failed: " << status
          << ", code " << resp_.error().code();
      TransitionToCompleteState();
    } else {
      LOG_WITH_PREFIX(WARNING)
          << "wait for backends catalog version failed, no further retry: " << status
          << ", response was " << resp_.ShortDebugString();
      TransitionToFailedState(MonitoredTaskState::kRunning, status);
    }
  } else {
    TransitionToCompleteState();
    VLOG_WITH_PREFIX_AND_FUNC(1) << "RPC complete";
  }
}

void BackendsCatalogVersionTS::UnregisterAsyncTaskCallback() {
  if (state() == MonitoredTaskState::kAborted) {
    LOG_WITH_PREFIX_AND_FUNC(INFO) << "was aborted";
    if (auto job = job_.lock()) {
      job->Update(
          permanent_uuid(), STATUS_FORMAT(Aborted, "TS $0 RPC task was aborted", permanent_uuid()));
    }
    return;
  }

  // There are multiple cases where we consider a tserver to be resolved:
  // - Num lagging backends is zero: directly resolved
  // - Tserver was found dead in HandleResponse: indirectly resolved assuming issue #13369.
  // - Tserver was found dead in DoRpcCallback: (same).
  // - Tserver was found behind in HandleResponse: indirectly resolved for compatibility during
  //   upgrade: it is possible backends are actually behind.
  // - Tserver was found behind in DoRpcCallback: (same).
  bool indirectly_resolved = found_dead_ || found_behind_;
  if (!rpc_.status().ok()) {
    // Only way for rpc status to be not okay is from TransitionToCompleteState in
    // RetryingTSRpcTask::DoRpcCallback.  That can happen in any of the following ways:
    // - ts died.
    // - ts is on an older version that doesn't support the RPC.
    DCHECK_EQ(state(), MonitoredTaskState::kComplete);
    DCHECK(!resp_.has_error()) << LogPrefix() << resp_.error().ShortDebugString();
    indirectly_resolved = true;
  }

  // Find failures.
  Status status;
  if (!indirectly_resolved) {
    // Only live tservers can be considered to have failures since dead or behind tservers are
    // considered resolved.  Dead tservers could still get resp error like catalog version too old,
    // but we don't want to throw error when they are already considered resolved.
    // TODO(#13369): ensure dead tservers abort/block ops until they successfully heartbeat.

    if (resp_.has_error()) {
      if (state() == MonitoredTaskState::kComplete) {
        // Retryable error.
        status = STATUS_FORMAT(
            TryAgain,
            "$0 got retryable error: $1",
            description(), StatusFromPB(resp_.error().status()));
      } else {
        status = StatusFromPB(resp_.error().status());
      }
    } else if (state() != MonitoredTaskState::kComplete) {
      // If state is not Complete and resp has no error, it means HandleResponse never got called.
      // This can happen if an error occurred when trying to reschedule or before receiving a
      // response.
      status = STATUS_FORMAT(InternalError, "$0 in state $1", description(), state());
    }
  }

  if (auto job = job_.lock()) {
    if (indirectly_resolved) {
      // There are three cases of indirectly resolved tservers, outlined in a comment above.
      if (found_dead_ || !target_ts_desc_->IsLive()) {
        // The two tserver-found-dead cases.
        LOG_WITH_PREFIX(INFO)
            << "tserver died, so assuming its backends are at latest catalog version";
      } else {
        // The two tserver-found-behind cases.
        LOG_WITH_PREFIX(INFO) << "tserver behind, so skipping backends catalog version check";
      }
      job->Update(permanent_uuid(), 0);
    } else if (status.ok()) {
      DCHECK(resp_.has_num_lagging_backends());
      job->Update(permanent_uuid(), resp_.num_lagging_backends());
    } else {
      job->Update(permanent_uuid(), status);
    }
  } else {
    LOG_WITH_PREFIX(INFO) << "job was destroyed, so not triggering update";
  }
}

std::string BackendsCatalogVersionTS::LogPrefix() const {
  if (const auto job = job_.lock()) {
    return Format("[DB $0, V $1, TS $2]: ",
                  job->database_oid(), job->target_version(), permanent_uuid());
  } else {
    return Format("[no job, TS $0]: ", permanent_uuid());
  }
}

}  // namespace master
}  // namespace yb
