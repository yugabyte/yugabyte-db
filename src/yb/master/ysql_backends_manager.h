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
//

#ifndef YB_MASTER_YSQL_BACKENDS_MANAGER_H
#define YB_MASTER_YSQL_BACKENDS_MANAGER_H

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yb/common/pg_types.h"
#include "yb/gutil/thread_annotations.h"
#include "yb/master/async_rpc_tasks.h"
#include "yb/master/master.h"
#include "yb/master/master_admin.pb.h"
#include "yb/rpc/rpc_context.h"
#include "yb/server/monitored_task.h"

namespace yb {
namespace master {

class BackendsCatalogVersionJob;
class BackendsCatalogVersionTS;

// YSQL Backends Manager receives WaitForYsqlBackendsCatalogVersion requests from clients and
// queries tservers to gather the information to fulfill those requests.  It uses an async model on
// both the client-master RPC path and master-tserver RPC path.  For simplicity, assuming it were a
// fully synchronous model, the flow would look like the following:
// 1. client sends RPC to master to wait for all backends to be on database X catalog version Y.
// 1. master (this YsqlBackendsManager) checks if the result is cached.
//    - If in the versions cache, return the result immediately.
//    - If in the jobs cache, follow that job.
//    - Otherwise, create a new job.
// 1. For a new job, master sends an RPC to each tserver to wait for all backends to be on database
//    X catalog version Y.
// 1. Each tserver polls the local postgres until all backends on database X are at catalog version
//    Y ("at" meaning >= Y) then sends OK response back to master.
// 1. Once master receives OK responses from all tservers, it sends OK response to client.
//
// Notes:
// - "client" is actually both postgres and tserver, where postgres (pg_client) first sends RPC to
//   local tserver, then that tserver (client) sends request to master.
// - tserver polling local postgres is by spawning a postgres connection using unix domain socket
//   then querying yb_pg_stat_activity which contains database oid and catalog version information.
// - Regardless of whether per-db catalog version mode (FLAGS_TEST_enable_db_catalog_version_mode)
//   is on, we only care about the backends of the requested database.  This is because,
//   practically, the supported online schema change DDLs (only CREATE INDEX at the time of writing)
//   don't care about the catalog version on other databases.
//
// There are two caches:
// - latest_known_versions_: for each database, caches the latest known backends catalog version.
//   For example, after succeeding a request on database X catalog version Y, a subsequent request
//   on database X catalog version Y - 1 should immediately succeed.
// - jobs_: for each database+version pair, caches the ongoing job.  This is to prevent duplicate
//   jobs from being spawned: the clients will share a single job if they both requested the same
//   database and version.
//
// In reality, both RPC paths are asynchronous:
// - client-master RPC:
//   - If the db+ver is already known to be satisfied using latest_known_versions_ cache, return
//     success immediately.
//   - If the db+ver is not in the jobs cache, create a new job and launch it.
//   - Wait for the job to reach terminal state up to deadline
//     - If hit terminal state, return result (bad status or zero).
//     - Otherwise, return current number of lagging backends.
// - master-tserver RPC:
//   - Until deadline, poll postgres for backends that are behind.
//     - If the number of backends that are behind decreases, return the result (that number)
//       immediately.  This is so that master has a more up-to-date number, which is sent back to
//       client and shown in master UI /tasks page.
//     - Otherwise, return the result when close to deadline.
// - Note: most waiting is spent on server-side to be able to deliver results as quickly as possible
//   while avoiding excessive network traffic.
//
// Example:
// - Given a three-tserver cluster
//   - ts-1:
//     - db yugabyte, version 5
//     - db yugabyte, version 6
//     - db postgres, version 4
//   - ts-2:
//     - db postgres, version 3
//   - ts-3:
//     - db postgres, version 8
//     - db yugabyte, version 5
// - Interaction can go as follows:
//   1. client A requests db yugabyte, version 6
//      1. master creates job for [DB: yugabyte, V: 6], launches tserver RPCs for that
//         - ts-1 responds that 1 backend is behind (the one on version 5)
//         - ts-2 responds success (there are no backends on yugabyte database)
//         - ts-3 responds that 1 backend is behind (the one on version 5)
//      1. master launches RPCs again for ts-1 and ts-3
//      1. client A deadline is reached, so master returns a total number of 2
//   1. client A requests db yugabyte, version 6 again
//      1. master matches that request with the existing job and waits on that job
//      1. ts-1 responds that 1 backend is still behind
//      1. ts-3 responds that 1 backend is still behind
//   1. client B requests db yugabyte, version 6
//      1. master matches that request with the existing job and waits on that job
//   1. ts-3 backend on db yugabyte version 5 finishes its transaction and goes idle
//      1. master is notified of the number decrease
//   1. client A deadline is reached, so master returns a total number of 1
//   1. client A requests db yugabyte, version 6 again
//   1. client C requests db postgres, version 8
//      1. master creates job for [DB: postgres, V: 8], launches tserver RPCs for that
//         - ts-1 responds that 1 backend is behind (the one on version 4)
//         - ts-2 responds that 1 backend is behind (the one on version 3)
//         - ts-3 responds success
//      1. master launches RPCs again for ts-1 and ts-2
//   1. ts-1 backend on db yugabyte version 4 exits
//      1. master is notified of the number decrease
//      1. master puts the job in complete state
//      1. master notifies waiters (client A and B) of terminal state
//         1. client A receives response that all backends are resolved
//         1. client B receives response that all backends are resolved
//   1. client D requests db yugabyte, version 4
//      1. master responds immediately that all backends are resolved because it cached the previous
//         result
//   ...
//
// - Edge cases:
//   - On master leader failover, caches are destroyed.  When clients send requests to the new
//     master leader, jobs are started from scratch.  This is no big issue since there is no major
//     accumulated progress in the first place.
//   - On tserver network partitioning, master may be unable to reach a tserver to determine whether
//     its backends satisfy the requested db+ver.  In that case, rely on tserver to block its own
//     backends from functioning when its "lease" with master expires.  An example implementation of
//     "lease" is FLAGS_tserver_unresponsive_timeout_ms to determine whether master views the
//     tserver as live.  The blocking is currently not implemented and is required for correctness.
//
// - Ownership: there are multiple in-memory objects that it is worth mentioning the memory model.
//   - YsqlBackendsManager: there is only a single instance of this owned by master
//   - BackendsCatalogVersionJob: a job is shared-owned by the backends manager and the catalog
//     manager jobs_tracker_.  The manager's ownership is released when the job reaches terminal
//     state.  For example, master losing leadership puts the job in aborted (terminal) state.  The
//     jobs tracker's ownership can extend beyond that, and that's good so that we can look at the
//     master UI /tasks page to see the completed job for a while after it is completed.  The jobs
//     tracker releases ownership either when it kicks it out of the list or when master loses
//     leadership and it cleans up the entire list.  Finally, there is a weak pointer to the job by
//     each related BackendsCatalogVersionTS, and that can occasionally take temporary ownership.
//   - BackendsCatalogVersionTS: although it is intuitive for these to be owned by the corresponding
//     job, they are actually (eventually) only owned by the RPC callback.  If the job gets
//     destroyed, it has no immediate effect on this rpc task: once the RPC response is received
//     from tserver and we find that the job doesn't exist, we fall out there.
//
// - State changes:
//   - BackendsCatalogVersionJob: only 5 states are used:
//                             +--> kComplete
//                             |
//     kWaiting --> kRunning --+--> kFailed
//                             |
//                             +--> kAborted
//     - kWaiting: waiting to get launched.  The initial state of every job.
//     - kRunning: launching or in-progress.  The only way to get to this state is
//       CompareAndSwapState from kWaiting, and this only happens on client request.
//     - kComplete: completed successfully.  The only way to get to this state is
//       CompareAndSwapState from kRunning, and this only happens when total number of lagging
//       backends is found to be zero in BackendsCatalogVersionJob::Update.
//     - kFailed: failed and won't retry.  The only way to get to this state is CompareAndSwapState
//       from kRunning, and this only originates from BackendsCatalogVersionJob::Update.
//     - kAborted: job is no longer relevant.  The only ways to get to this state are
//       CompareAndSwapState from kRunning when
//       - master term changed: either from AbortAllJobs by catalog manager bg thread (likely) or
//         BackendsCatalogVersionJob::Update.
//       - job is cleaned up for inactivity: from AbortInactiveJobs by catalog manager bg thread.
//       - AbortAndReturnPrevState: (it should never be called, but it must be implemented)
//   - BackendsCatalogVersionTS:
//     - kScheduling: same as RetryingTSRpcTask
//     - kWaiting: same as RetryingTSRpcTask
//     - kRunning: same as RetryingTSRpcTask
//     - kComplete: possible through TransitionToCompleteState when running; otherwise, same as
//       RetryingTSRpcTask
//     - kFailed: possible through TransitionToFailedState when running; otherwise, same as
//       RetryingTSRpcTask
//     - kAborted: same as RetryingTSRpcTask
//
// - Single-RPC-type design: most async RPCs use one RPC A to start the task and multiple RPCs B to
//   wait on A to finish.  Here, we use only one RPC A for both starting and waiting.  Here are some
//   reasons for the decision.
//   - From a client's perspective, it only wants to do one kind of thing: wait for a condition to
//     be met.  We can abstract the layers and caching on the server side.
//   - Easy to combine duplicate requests.  If two separate clients both want to wait on the same
//     parameters, only a single job needs to be used.  This could be achieved even in a
//     two-RPC-type design, but it becomes trickier in failure cases.  How do we communicate a
//     failure of the job to both clients?  One client may receive the failure, but the other may
//     not for an indefinite period of time.  If we want all clients to receive the failure details,
//     we need to do extra work, such as (a) have clients use unique identifiers to register with
//     the job and send those identifiers on each request or (b) keep the failure details for a
//     fixed length of time hoping all clients send a request within that time.  In (b), once the
//     job is deleted and an unaware client sends a request but doesn't find the job, then it would
//     need to know to resend a type-A RPC if it wants to retry.
//
// - Leader lock: unlike most client-master RPC handlers, the sys catalog leader lock is not held
//   throughout the whole handling.  It is only taken when needed: accessing catalog manager
//   components.  It is possible that leadership is lost while processing without the lock, but
//   that is fine because this will eventually get detected.  There should be no window of time
//   where inconsistencies occur because what matters is that the lock is held when the tservers are
//   selected.  If master loses leadership and new tservers come up, we don't need to worry about
//   not knowing about those tservers because they should be at latest version anyway if the tserver
//   is new.
class YsqlBackendsManager {
 public:
  typedef uint64_t Version;
  typedef std::pair<PgOid, Version> DbVersion;
  // Must be ordered map because pair has no hash function but has ordering.
  typedef std::map<DbVersion, std::shared_ptr<BackendsCatalogVersionJob>> DbVersionToJobMap;

  YsqlBackendsManager(Master* master, ThreadPool* callback_pool);

  Status AccessYsqlBackendsManagerTestRegister(
      const AccessYsqlBackendsManagerTestRegisterRequestPB* req,
      AccessYsqlBackendsManagerTestRegisterResponsePB* resp);

  Status WaitForYsqlBackendsCatalogVersion(
      const WaitForYsqlBackendsCatalogVersionRequestPB* req,
      WaitForYsqlBackendsCatalogVersionResponsePB* resp,
      rpc::RpcContext* rpc)
      EXCLUDES(mutex_);

  Result<int> HandleJobRequest(
      std::shared_ptr<BackendsCatalogVersionJob> job,
      const CoarseTimePoint& deadlnie,
      WaitForYsqlBackendsCatalogVersionResponsePB* resp) EXCLUDES(mutex_);

  void ClearJobIfCached(std::shared_ptr<BackendsCatalogVersionJob> job) EXCLUDES(mutex_);
  void ClearJobIfCachedUnlocked(std::shared_ptr<BackendsCatalogVersionJob> job) REQUIRES(mutex_);

  void TerminateJob(
      std::shared_ptr<BackendsCatalogVersionJob> job,
      server::MonitoredTaskState target_state,
      const Status& status = Status::OK())
      EXCLUDES(mutex_);

  // Abort (and remove references to) all jobs.
  void AbortAllJobs();
  // Abort (and remove references to) all inactive jobs.
  void AbortInactiveJobs();

 private:
  // Test function to do blocking associated with
  // FLAGS_TEST_block_wait_for_ysql_backends_catalog_version.
  void TestDoBlock(int id, const char* func, int line);

  void AbortJobs(const std::vector<std::shared_ptr<BackendsCatalogVersionJob>>& jobs);

  Status HandleSwapToRunning(
      std::shared_ptr<BackendsCatalogVersionJob> job,
      WaitForYsqlBackendsCatalogVersionResponsePB* resp);

  Master* master_;
  ThreadPool* callback_pool_;

  mutable rw_spinlock mutex_;
  DbVersionToJobMap jobs_ GUARDED_BY(mutex_);
  std::unordered_map<PgOid, Version> latest_known_versions_ GUARDED_BY(mutex_);

  std::atomic<int> test_register_;
};

class BackendsCatalogVersionJob : public server::MonitoredTask {
 public:
  typedef YsqlBackendsManager::Version Version;
  typedef YsqlBackendsManager::DbVersion DbVersion;

  BackendsCatalogVersionJob(
      Master* master, ThreadPool* callback_pool, PgOid database_oid, Version target_version)
      : master_(master),
        callback_pool_(callback_pool),
        state_cv_(&state_mutex_),
        database_oid_(database_oid),
        target_version_(target_version),
        epoch_(LeaderEpoch(1)),
        last_access_(CoarseMonoClock::Now()) {}

  std::shared_ptr<BackendsCatalogVersionJob> shared_from_this() {
    return std::static_pointer_cast<BackendsCatalogVersionJob>(
        server::MonitoredTask::shared_from_this());
  }

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kBackendsCatalogVersion;
  }
  std::string type_name() const override { return "Backends Catalog Version"; }
  std::string description() const override;
  server::MonitoredTaskState AbortAndReturnPrevState(const Status& status) override
      EXCLUDES(mutex_);
  bool CompareAndSwapState(server::MonitoredTaskState old_state,
                           server::MonitoredTaskState new_state)
      EXCLUDES(mutex_);
  Result<int> HandleTerminalState() EXCLUDES(mutex_);

  // Put job in kRunning state and kick off TS RPCs.
  Status Launch(LeaderEpoch epoch) EXCLUDES(mutex_);
  // Retry TS RPC.
  Status LaunchTS(TabletServerId ts_uuid, int num_lagging_backends, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);
  // Whether the job hasn't been accessed within expiration time.
  bool IsInactive() const EXCLUDES(mutex_);
  // Whether the current sys catalog leader term matches the term recorded at the start of the job.
  bool IsSameTerm() const EXCLUDES(mutex_);
  // Handle update from TS and possibly send update to YsqlBackendsManager.
  void Update(TabletServerId ts_uuid, Result<int> num_lagging_backends) EXCLUDES(mutex_);

  // Wait for terminal state or deadline, then return the total number of lagging backends (or bad
  // status).
  Result<int> WaitAndGetNumLaggingBackends(const CoarseTimePoint& deadline) EXCLUDES(mutex_);
  // Get the total number of lagging backends.
  int GetNumLaggingBackends() const EXCLUDES(mutex_);

  // Update last access time to current time.
  void Touch() {
    last_access_ = CoarseMonoClock::Now();
  }

  void AddFailureStatus(Status s) EXCLUDES(mutex_) {
    std::lock_guard l(mutex_);
    failure_statuses_.push_back(s);
  }

  Master* master() { return master_; }
  ThreadPool* threadpool() { return callback_pool_; }
  PgOid database_oid() const { return database_oid_; }
  Version target_version() const { return target_version_; }
  const std::vector<Status>& failure_statuses() const {
    return failure_statuses_;
  }

  std::string LogPrefix() const;

 private:
  // dependency vars
  Master* master_;
  ThreadPool* callback_pool_;

  // mutex to protect in-memory objects.
  mutable rw_spinlock mutex_;

  // Condition variable for waiting/signalling updates to state_.
  mutable Mutex state_mutex_;
  ConditionVariable state_cv_;

  const PgOid database_oid_;
  const Version target_version_;
  // Master sys catalog consensus epoch when launching the job.
  LeaderEpoch epoch_ GUARDED_BY(mutex_);
  // Last time this job was accessed.  Used to determine when the job should be cleaned up for lack
  // of activity.  No need to guard with mutex since writes are already guarded by
  // YsqlBackendsManager mutex.
  CoarseTimePoint last_access_;
  // ts uuid to number of lagging backends on that ts (-1 if uninitialized).
  std::unordered_map<std::string, int> ts_map_ GUARDED_BY(mutex_);
  // In case of failures, each failure status.  These can be communicated when responding to the
  // client.
  std::vector<Status> failure_statuses_;
};

class BackendsCatalogVersionTS : public RetryingTSRpcTask {
 public:
  BackendsCatalogVersionTS(
      std::shared_ptr<BackendsCatalogVersionJob> job,
      const std::string& ts_uuid,
      int prev_num_lagging_backends);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kBackendsCatalogVersionTs;
  }
  std::string type_name() const override { return "Backends Catalog Version of TS"; }
  std::string description() const override;

  bool SendRequest(int attempt) override;
  void HandleResponse(int attempt) override;
  void UnregisterAsyncTaskCallback() override;
  TabletId tablet_id() const override { return ""; }

  TabletServerId permanent_uuid() const;

  std::string LogPrefix() const;

 private:
  // Use a weak ptr because this doesn't own job and job can be destroyed at any moment.
  std::weak_ptr<BackendsCatalogVersionJob> job_;
  tserver::WaitForYsqlBackendsCatalogVersionResponsePB resp_;

  // Previously known number of lagging backends.  Used by tserver to determine when that number
  // changes in order to relay the update sooner rather than potentially waiting for the full
  // master-to-tserver RPC deadline.
  const int prev_num_lagging_backends_;

  // Whether the tserver is considered behind.  This is checked and set true on HandleResponse when
  // the response error complains about mismatched schema most likely due to not having run
  // upgrade_ysql.
  bool found_behind_ = false;
  // Whether the tserver is considered dead (expired).  This is checked and set true on
  // HandleResponse.  It may be the case that this is not set and tserver is found dead through a
  // different way (e.g. rpc failure).
  bool found_dead_ = false;
};

}  // namespace master
}  // namespace yb

#endif  // YB_MASTER_YSQL_BACKENDS_MANAGER_H
