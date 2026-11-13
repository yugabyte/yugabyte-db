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

#include "yb/master/online_schema_change.h"

#include <algorithm>
#include <set>
#include <utility>

#include "yb/tserver/tserver_admin.proxy.h"

#include "yb/common/pg_types.h"
#include "yb/common/wire_protocol.h"

#include "yb/master/backfill_index.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/tablet_split_manager.h"

#include "yb/server/clock.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/status_log.h"

DECLARE_int32(index_backfill_rpc_max_delay_ms);
DECLARE_int32(index_backfill_rpc_max_retries);
DECLARE_int32(index_backfill_tablet_split_completion_timeout_sec);
DECLARE_int32(leader_lease_duration_ms);
DECLARE_uint64(max_clock_skew_usec);
DECLARE_int32(ysql_index_backfill_rpc_timeout_ms);

DEFINE_RUNTIME_uint32(online_schema_change_backfill_resume_interval_ms, 60 * 1000,
    "How often the master leader rescans every table for online schema change jobs that are "
    "durably unfinished but that it is not driving yet, and for the pins of finished jobs. A new "
    "leader term rescans at once. Jobs it already drives are visited on every bg tasks tick "
    "regardless.");

DEFINE_RUNTIME_uint32(online_schema_change_backfill_max_resumes, 20,
    "How many leader terms may take over a single online schema change before it is failed. A "
    "job that reaches this budget is failing repeatedly rather than making progress, so it is "
    "failed the way any copy failure fails it: the delete-marker retention pin comes off, and "
    "postgres sees the job as failed. 0 disables the budget.");

DEFINE_RUNTIME_uint32(online_schema_change_backfill_pin_release_margin_ms, 10 * 1000,
    "After an online schema change copy succeeds, how much longer than "
    "ysql_index_backfill_rpc_timeout_ms, the leader lease and the max clock skew its "
    "delete-marker retention pin stays. A chunk RPC that an earlier leader sent can still be "
    "writing on a tserver after the copy is recorded as done; the tserver checks the RPC deadline "
    "between pages, so this margin covers the last page. Releasing the pin before such a write "
    "lands lets compaction drop a tombstone that hides the row the write brings back.");

DEFINE_test_flag(uint32, online_schema_change_backfill_replay_chunks_per_tablet, 0,
    "Throw away the first N chunk responses of each source tablet: neither the resume key nor the "
    "rows are recorded, so the next chunk re-copies exactly the range the dropped one copied. "
    "Models a chunk the tserver completed whose response was lost, or a leader that failed over "
    "before persisting the key -- the case an online schema change has to be idempotent against.");

namespace yb {
namespace master {

namespace {

server::MonitoredTaskState MonitoredStateFor(OnlineSchemaChangeJobPB::State state) {
  switch (state) {
    case OnlineSchemaChangeJobPB::SUCCESS: return server::MonitoredTaskState::kComplete;
    default:                               return server::MonitoredTaskState::kFailed;
  }
}

std::optional<OnlineSchemaChangeJobPB> ReadJobEntry(const TableInfo& source_table) {
  auto l = source_table.LockForRead();
  if (!l->pb.has_online_schema_change_job()) {
    return std::nullopt;
  }
  return l->pb.online_schema_change_job();
}

// Epochs only move forward on one master; a smaller one is from a request that started before a
// leader change.
bool IsLaterEpoch(const LeaderEpoch& lhs, const LeaderEpoch& rhs) {
  return lhs.leader_term != rhs.leader_term ? lhs.leader_term > rhs.leader_term
                                            : lhs.pitr_count > rhs.pitr_count;
}

std::string JobPrefix(const TableInfo& source_table, int64_t pg_job_id) {
  return Format("Online schema change job $0 on $1: ", pg_job_id, source_table.ToString());
}

}  // namespace

bool IsOnlineSchemaChangeJobResumable(OnlineSchemaChangeJobPB::State state) {
  switch (state) {
    case OnlineSchemaChangeJobPB::COMPUTING_READ_TIME:
    case OnlineSchemaChangeJobPB::IN_PROGRESS:
      return true;
    case OnlineSchemaChangeJobPB::SUCCESS:
    case OnlineSchemaChangeJobPB::FAILED:
    case OnlineSchemaChangeJobPB::CANCELLED:
      return false;
  }
  return false;
}

Status ValidateOnlineSchemaChangeJob(const OnlineSchemaChangeJobPB& job) {
  SCHECK(job.has_pg_job_id() && job.pg_job_id() != 0, Corruption,
         "Online schema change job entry has no job id");
  SCHECK_FORMAT(job.has_state(), Corruption,
                "Online schema change job $0 has no state", job.pg_job_id());
  SCHECK_FORMAT(
      job.has_backfilling_timestamp() && HybridTime(job.backfilling_timestamp()).is_valid(),
      Corruption, "Online schema change job $0 has no read time", job.pg_job_id());
  SCHECK_FORMAT(job.has_source_table_oid() && job.source_table_oid() != kPgInvalidOid, Corruption,
                "Online schema change job $0 has no source table oid", job.pg_job_id());
  SCHECK_FORMAT(job.has_target_table_oid() && job.target_table_oid() != kPgInvalidOid, Corruption,
                "Online schema change job $0 has no target table oid", job.pg_job_id());
  SCHECK_FORMAT(job.has_transform_fn_oid() && job.transform_fn_oid() != kPgInvalidOid, Corruption,
                "Online schema change job $0 has no transform function oid", job.pg_job_id());
  return Status::OK();
}

std::string OnlineSchemaChangeTabletKey(int64_t pg_job_id) {
  return Format("online_schema_change_$0", pg_job_id);
}

std::optional<int64_t> CopyingOnlineSchemaChangeJobReading(const TableInfo& table) {
  auto l = table.LockForRead();
  if (l->pb.has_online_schema_change_job() &&
      IsOnlineSchemaChangeJobResumable(l->pb.online_schema_change_job().state())) {
    return l->pb.online_schema_change_job().pg_job_id();
  }
  return std::nullopt;
}

Result<OnlineSchemaChangeProgress> ReadOnlineSchemaChangeProgress(
    const std::vector<scoped_refptr<TableInfo>>& source_leaves, int64_t pg_job_id) {
  const auto key = OnlineSchemaChangeTabletKey(pg_job_id);
  OnlineSchemaChangeProgress progress;
  for (const auto& leaf : source_leaves) {
    for (const auto& tablet : VERIFY_RESULT(leaf->GetTablets())) {
      ++progress.tablets_total;
      auto l = tablet->LockForRead();
      // Empty means drained; absent means not started.
      const auto it = l->pb.backfilled_until().find(key);
      if (it != l->pb.backfilled_until().end() && it->second.empty()) {
        ++progress.tablets_done;
      }
      const auto rows = l->pb.backfilled_rows().find(key);
      if (rows != l->pb.backfilled_rows().end()) {
        progress.rows_done += rows->second;
      }
    }
  }
  return progress;
}

// ------------------------------------------------------------------------------------------------
// OnlineSchemaChangeJob
// ------------------------------------------------------------------------------------------------

OnlineSchemaChangeJob::OnlineSchemaChangeJob(const TableInfoPtr& source_table, int64_t pg_job_id)
    : description_(Format("Online schema change job $0 copying $1", pg_job_id,
                          source_table->ToString())) {
  state_ = server::MonitoredTaskState::kRunning;
}

server::MonitoredTaskState OnlineSchemaChangeJob::AbortAndReturnPrevState(
    const Status& status, bool call_task_finisher) {
  auto old_state = state();
  Finish(server::MonitoredTaskState::kAborted);
  return old_state;
}

void OnlineSchemaChangeJob::Finish(server::MonitoredTaskState new_state) {
  auto old_state = state();
  while (!IsStateTerminal(old_state)) {
    if (state_.compare_exchange_strong(old_state, new_state)) {
      completion_timestamp_ = MonoTime::Now();
      return;
    }
  }
}

// ------------------------------------------------------------------------------------------------
// OnlineSchemaChangeController
// ------------------------------------------------------------------------------------------------

// Only the pass that ReconcileJob runs touches the fields above the mutex line, so they need no
// lock; the RPC callbacks touch only the ones below it, under the controller's mutex_.
struct OnlineSchemaChangeController::Job {
  Job(TableInfoPtr table, int64_t job_id)
      : source_table(std::move(table)), pg_job_id(job_id),
        prefix(JobPrefix(*source_table, pg_job_id)) {}

  const TableInfoPtr source_table;
  const int64_t pg_job_id;
  const std::string prefix;
  std::shared_ptr<OnlineSchemaChangeJob> monitored;

  bool adopted = false;
  // The tables the copy reads and writes. A missing target is left out of a job that only has to
  // be stopped.
  bool resolved = false;
  std::vector<scoped_refptr<TableInfo>> source_leaves;
  std::vector<scoped_refptr<TableInfo>> target_leaves;

  // --- Under mutex_. ---
  // A pass runs; a visit meanwhile sets dirty for it to go again.
  bool running = false;
  bool dirty = false;
  // The takeover has been counted against online_schema_change_backfill_max_resumes, or the job
  // started in this leader term.
  bool counted = false;
  int failed_passes = 0;
  CoarseTimePoint retry_after;
  // The GetSafeTime round of COMPUTING_READ_TIME: the max so far, and how many are outstanding.
  bool safe_time_sent = false;
  size_t safe_time_pending = 0;
  HybridTime read_time;
  // The first RPC failure, which the next pass writes as FAILED.
  Status failure;
  // The chunk in flight per source tablet. Null between the send decision and the task's creation.
  std::unordered_map<TabletId, std::shared_ptr<TableBackfillChunk>> chunks;
  std::unordered_map<TabletId, uint32_t> replays_for_test;
};

OnlineSchemaChangeController::OnlineSchemaChangeController(
    Master* master, CatalogManager* catalog_manager)
    : master_(master), catalog_manager_(catalog_manager) {}

OnlineSchemaChangeController::~OnlineSchemaChangeController() = default;

bool OnlineSchemaChangeController::PinReleaseDue(
    const OnlineSchemaChangeJobPB& entry, HybridTime now) {
  if (entry.state() != OnlineSchemaChangeJobPB::SUCCESS || !entry.has_finished_at()) {
    return true;
  }
  // The terminal write and the release may be on different masters, whose clocks differ by up to
  // the max skew.
  const auto delay = MonoDelta::FromMilliseconds(
      static_cast<int64_t>(FLAGS_ysql_index_backfill_rpc_timeout_ms) +
      FLAGS_leader_lease_duration_ms + FLAGS_online_schema_change_backfill_pin_release_margin_ms) +
      MonoDelta::FromMicroseconds(static_cast<int64_t>(FLAGS_max_clock_skew_usec));
  return now >= HybridTime(entry.finished_at()).AddDelta(delay);
}

void OnlineSchemaChangeController::RunOnce(const LeaderEpoch& epoch) {
  const auto now = CoarseMonoClock::Now();
  bool discover = false;
  std::vector<JobPtr> jobs;
  std::vector<JobPtr> dropped;
  {
    std::lock_guard lock(mutex_);
    if (!(epoch_ == epoch)) {
      dropped = SwitchEpochUnlocked(epoch);
    }
    if (now >= next_discovery_) {
      discover = true;
      next_discovery_ = now + FLAGS_online_schema_change_backfill_resume_interval_ms * 1ms;
    }
  }
  DropJobs(dropped);
  if (discover) {
    Discover(epoch);
  }
  {
    std::lock_guard lock(mutex_);
    for (const auto& [_, job] : jobs_) {
      if (now >= job->retry_after) {
        jobs.push_back(job);
      }
    }
  }
  for (const auto& job : jobs) {
    ReconcileJob(job, epoch);
  }
}

void OnlineSchemaChangeController::Reset() {
  std::vector<JobPtr> dropped;
  {
    std::lock_guard lock(mutex_);
    dropped = SwitchEpochUnlocked(LeaderEpoch());
  }
  DropJobs(dropped);
}

std::vector<OnlineSchemaChangeController::JobPtr>
OnlineSchemaChangeController::SwitchEpochUnlocked(const LeaderEpoch& epoch) {
  std::vector<JobPtr> dropped;
  for (auto& [_, job] : jobs_) {
    dropped.push_back(job);
  }
  jobs_.clear();
  epoch_ = epoch;
  next_discovery_ = CoarseTimePoint();
  return dropped;
}

void OnlineSchemaChangeController::DropJobs(const std::vector<JobPtr>& jobs) {
  for (const auto& job : jobs) {
    AbortChunks(*job);
    job->monitored->Finish(server::MonitoredTaskState::kAborted);
  }
}

OnlineSchemaChangeController::JobPtr OnlineSchemaChangeController::FindJob(
    const TableInfoPtr& source_table, const LeaderEpoch& epoch, bool add) {
  // Read before taking mutex_, which is never held with a table lock.
  const auto entry = add ? ReadJobEntry(*source_table) : std::nullopt;
  std::vector<JobPtr> dropped;
  JobPtr job;
  {
    std::lock_guard lock(mutex_);
    if (!(epoch_ == epoch)) {
      // An RPC of a new leader term can arrive before the bg tasks loop has seen the term.
      if (!add || !IsLaterEpoch(epoch, epoch_)) {
        return nullptr;
      }
      dropped = SwitchEpochUnlocked(epoch);
    }
    auto it = jobs_.find(source_table->id());
    if (it != jobs_.end() && (!entry || it->second->pg_job_id == entry->pg_job_id())) {
      return it->second;
    }
    if (!entry) {
      return nullptr;
    }
    // A later job replaced the entry of the one this master still knows.
    if (it != jobs_.end()) {
      dropped.push_back(it->second);
      jobs_.erase(it);
    }
    job = std::make_shared<Job>(source_table, entry->pg_job_id());
    job->monitored = std::make_shared<OnlineSchemaChangeJob>(source_table, entry->pg_job_id());
    jobs_.emplace(source_table->id(), job);
  }
  DropJobs(dropped);
  catalog_manager_->jobs_tracker_->AddTask(job->monitored);
  return job;
}

void OnlineSchemaChangeController::Forget(
    const JobPtr& job, server::MonitoredTaskState monitored_state) {
  {
    std::lock_guard lock(mutex_);
    auto it = jobs_.find(job->source_table->id());
    if (it != jobs_.end() && it->second == job) {
      jobs_.erase(it);
    }
  }
  AbortChunks(*job);
  job->monitored->Finish(monitored_state);
}

Status OnlineSchemaChangeController::Start(
    const TableInfoPtr& source_table, const OnlineSchemaChangeJobPB& job_pb,
    const LeaderEpoch& epoch) {
  RETURN_NOT_OK(ValidateOnlineSchemaChangeJob(job_pb));
  const auto pg_job_id = job_pb.pg_job_id();
  {
    auto l = source_table->LockForWrite();
    auto& pb = l.mutable_data()->pb;
    bool dirty = false;
    if (pb.has_online_schema_change_job() &&
        pb.online_schema_change_job().pg_job_id() == pg_job_id) {
      // A retried StartTableBackfill finds the entry its first attempt wrote.
      const auto state = pb.online_schema_change_job().state();
      if (!IsOnlineSchemaChangeJobResumable(state)) {
        return STATUS_FORMAT(
            AlreadyPresent, "Online schema change job $0 on table $1 already finished in state $2",
            pg_job_id, source_table->ToString(), OnlineSchemaChangeJobPB::State_Name(state));
      }
    } else if (pb.has_online_schema_change_job() &&
               IsOnlineSchemaChangeJobResumable(pb.online_schema_change_job().state())) {
      return STATUS_FORMAT(
          AlreadyPresent, "Online schema change job $0 is already in progress on table $1",
          pb.online_schema_change_job().pg_job_id(), source_table->ToString());
    } else {
      // Replaces the previous job's entry, which was kept for an abort issued from 'ready'.
      // Postgres starts this job only after that one is no longer active.
      auto* entry = pb.mutable_online_schema_change_job();
      *entry = job_pb;
      entry->set_state(OnlineSchemaChangeJobPB::COMPUTING_READ_TIME);
      dirty = true;
    }
    if (dirty) {
      RETURN_NOT_OK_PREPEND(
          catalog_manager_->sys_catalog_->Upsert(epoch, source_table),
          "Failed to persist online schema change job.");
      l.Commit();
    }
  }

  auto job = FindJob(source_table, epoch, /* add = */ true);
  SCHECK(job, IllegalState, Format("Leader epoch $0 is no longer current", epoch));
  SCHECK_EQ(job->pg_job_id, pg_job_id, IllegalState, "Job entry was replaced concurrently");
  // The pin must be in place before the response, which lets postgres start writing through the
  // capture trigger. Waits for a pass already running, so that its adoption
  // has finished too.
  {
    std::unique_lock lock(mutex_);
    job->counted = true;
  }
  ReconcileJob(job, epoch);
  {
    std::unique_lock lock(mutex_);
    pass_done_.wait(lock, [&job] { return !job->running; });
  }
  const auto entry = ReadJobEntry(*source_table);
  if (entry && entry->pg_job_id() == pg_job_id &&
      entry->state() == OnlineSchemaChangeJobPB::FAILED) {
    return STATUS_FORMAT(
        IllegalState, "Online schema change job $0 failed to start: $1", pg_job_id,
        entry->error_message());
  }
  LOG(INFO) << job->prefix << "Started, read time floor "
            << HybridTime(job_pb.backfilling_timestamp());
  return Status::OK();
}

void OnlineSchemaChangeController::Reconcile(
    const TableInfoPtr& source_table, const LeaderEpoch& epoch) {
  if (auto job = FindJob(source_table, epoch, /* add = */ true)) {
    ReconcileJob(job, epoch);
  }
}

void OnlineSchemaChangeController::ReconcileJob(const JobPtr& job, const LeaderEpoch& epoch) {
  {
    std::lock_guard lock(mutex_);
    auto it = jobs_.find(job->source_table->id());
    if (!(epoch_ == epoch) || it == jobs_.end() || it->second != job) {
      return;
    }
    if (job->running) {
      job->dirty = true;
      return;
    }
    job->running = true;
    job->dirty = false;
  }
  for (;;) {
    auto again = ReconcileOnce(job, epoch);
    std::lock_guard lock(mutex_);
    if (again.ok()) {
      job->failed_passes = 0;
      job->retry_after = CoarseTimePoint();
    } else {
      // Retried with backoff by the ticks, so a persistent failure, e.g. of sys_catalog writes,
      // costs one pass a minute rather than one a second.
      ++job->failed_passes;
      job->retry_after = CoarseMonoClock::Now() + std::min<MonoDelta>(
          MonoDelta::FromSeconds(1 << std::min(job->failed_passes - 1, 6)),
          MonoDelta::FromSeconds(60));
      YB_LOG_EVERY_N_SECS(WARNING, 10) << job->prefix << again.status();
    }
    if ((again.ok() && *again) || job->dirty) {
      job->dirty = false;
      continue;
    }
    job->running = false;
    pass_done_.notify_all();
    return;
  }
}

Result<bool> OnlineSchemaChangeController::ReconcileOnce(
    const JobPtr& job_ptr, const LeaderEpoch& epoch) {
  auto& job = *job_ptr;
  {
    std::lock_guard lock(mutex_);
    auto it = jobs_.find(job.source_table->id());
    if (!(epoch_ == epoch) || it == jobs_.end() || it->second != job_ptr) {
      return false;
    }
  }
  const auto entry = ReadJobEntry(*job.source_table);
  if (!entry || entry->pg_job_id() != job.pg_job_id) {
    // The source was dropped, or a later job replaced the entry.
    Forget(job_ptr, server::MonitoredTaskState::kAborted);
    return false;
  }
  if (auto valid = ValidateOnlineSchemaChangeJob(*entry); !valid.ok()) {
    Forget(job_ptr, server::MonitoredTaskState::kFailed);
    return valid;
  }
  if (!IsOnlineSchemaChangeJobResumable(entry->state())) {
    AbortChunks(job);
    if (!job.resolved) {
      RETURN_NOT_OK(ResolveTables(job, *entry, /* skip_missing = */ true));
    }
    if (VERIFY_RESULT(Release(job, *entry, epoch))) {
      Forget(job_ptr, MonitoredStateFor(entry->state()));
    }
    return false;
  }

  if (!job.adopted && VERIFY_RESULT(Adopt(job, *entry, epoch))) {
    return true;
  }

  Status failure;
  {
    std::lock_guard lock(mutex_);
    failure = job.failure;
  }
  if (!failure.ok()) {
    return WriteTerminal(job, OnlineSchemaChangeJobPB::FAILED, failure, epoch);
  }
  if (entry->cancel_requested()) {
    return WriteTerminal(job, OnlineSchemaChangeJobPB::CANCELLED, Status::OK(), epoch);
  }
  if (entry->state() == OnlineSchemaChangeJobPB::COMPUTING_READ_TIME) {
    return ComputeReadTime(job_ptr, *entry, epoch);
  }
  return Copy(job_ptr, *entry, epoch);
}

Result<bool> OnlineSchemaChangeController::Adopt(
    Job& job, const OnlineSchemaChangeJobPB& entry, const LeaderEpoch& epoch) {
  bool counted;
  {
    std::lock_guard lock(mutex_);
    counted = job.counted;
  }
  if (!counted) {
    // A cancelled job is only taken over to be stopped, so it does not count.
    if (FLAGS_online_schema_change_backfill_max_resumes > 0 && !entry.cancel_requested() &&
        entry.resume_count() >= FLAGS_online_schema_change_backfill_max_resumes) {
      return WriteTerminal(
          job, OnlineSchemaChangeJobPB::FAILED,
          STATUS_FORMAT(
              Aborted,
              "Online schema change backfill has been resumed $0 times without finishing its copy "
              "(online_schema_change_backfill_max_resumes = $1)",
              entry.resume_count(), FLAGS_online_schema_change_backfill_max_resumes),
          epoch);
    }
    {
      auto l = job.source_table->LockForWrite();
      auto& pb = l.mutable_data()->pb;
      if (!pb.has_online_schema_change_job() ||
          pb.online_schema_change_job().pg_job_id() != job.pg_job_id ||
          !IsOnlineSchemaChangeJobResumable(pb.online_schema_change_job().state())) {
        return true;
      }
      auto* job_pb = pb.mutable_online_schema_change_job();
      job_pb->set_resume_count(job_pb->resume_count() + 1);
      RETURN_NOT_OK_PREPEND(
          catalog_manager_->sys_catalog_->Upsert(epoch, job.source_table),
          "Failed to persist the resume count");
      l.Commit();
    }
    LOG(INFO) << job.prefix << "Taking over in state "
              << OnlineSchemaChangeJobPB::State_Name(entry.state());
    std::lock_guard lock(mutex_);
    job.counted = true;
  }

  // A cancelled job copies nothing, so there is nothing to pin, and a target that has since been
  // dropped must not block the cancel.
  const bool cancelling = entry.cancel_requested();
  auto status = ResolveTables(job, entry, cancelling);
  if (status.ok() && !cancelling) {
    status = PinTarget(job, epoch);
  }
  if (!status.ok()) {
    LOG(WARNING) << job.prefix << "Failed to pin the target: " << status;
    return WriteTerminal(job, OnlineSchemaChangeJobPB::FAILED, status, epoch);
  }
  job.adopted = true;
  return false;
}

Status OnlineSchemaChangeController::ResolveTables(
    Job& job, const OnlineSchemaChangeJobPB& entry, bool skip_missing) {
  // A missing target fails the job.
  auto target = catalog_manager_->GetOnlineSchemaChangeTargetTable(
      job.source_table->namespace_id(), entry.target_table_oid());
  job.source_leaves = {job.source_table};
  job.target_leaves.clear();
  if (target.ok()) {
    job.target_leaves = {*target};
  } else if (!skip_missing || !target.status().IsNotFound()) {
    return target.status();
  }
  job.resolved = true;
  return Status::OK();
}

Status OnlineSchemaChangeController::PinTarget(const Job& job, const LeaderEpoch& epoch) {
  // Before any copy runs, and before the capture trigger starts writing tombstones. Idempotent, so
  // a new leader re-asserts it.
  for (const auto& table : job.target_leaves) {
    RETURN_NOT_OK(
        catalog_manager_->PinOnlineSchemaChangeTargetDeleteMarkers(table, job.pg_job_id, epoch));
  }
  return Status::OK();
}

bool OnlineSchemaChangeController::SourceSplitsSettled(const Job& job) {
  // Takes the split manager's run lock, so an automatic split that passed validation before the
  // job entry was written is seen by the time this returns. A manual split skips the run lock but
  // is validated again in DoSplitTablet under the source's write lock, which the entry write also
  // takes, so it either sees the entry or has registered its children, which this also sees.
  const auto deadline = CoarseMonoClock::Now() + 100ms;
  for (const auto& leaf : job.source_leaves) {
    if (!master_->tablet_split_manager().IsTabletSplittingComplete(
            *leaf, false /* wait_for_parent_deletion */, deadline)) {
      return false;
    }
  }
  return true;
}

Result<bool> OnlineSchemaChangeController::ComputeReadTime(
    const JobPtr& job_ptr, const OnlineSchemaChangeJobPB& entry, const LeaderEpoch& epoch) {
  auto& job = *job_ptr;
  {
    std::unique_lock lock(mutex_);
    if (job.safe_time_sent) {
      if (job.safe_time_pending > 0) {
        return false;
      }
      const auto read_time = job.read_time;
      job.safe_time_sent = false;
      lock.unlock();
      // Once a chunk may have read at the read time, the time must survive a failover.
      if (VERIFY_RESULT(WriteReadTime(job, read_time, epoch))) {
        LOG(INFO) << job.prefix << "Starting the copy at read time " << read_time;
      }
      return true;
    }
  }

  // The split manager refuses new splits of the source once the entry exists (see
  // CopyingOnlineSchemaChangeJobReading); this waits out the ones it started before. The wait is
  // timed from the client's read time floor, so a new leader does not start it over.
  if (!SourceSplitsSettled(job)) {
    const auto deadline = HybridTime(entry.backfilling_timestamp()).GetPhysicalValueMicros() +
                          FLAGS_index_backfill_tablet_split_completion_timeout_sec * 1000000LL;
    if (master_->clock()->Now().GetPhysicalValueMicros() > deadline) {
      return WriteTerminal(
          job, OnlineSchemaChangeJobPB::FAILED,
          STATUS(TimedOut, "Tablet splits of the source did not complete; cannot safely backfill "
                           "the target table."),
          epoch);
    }
    return false;
  }
  // A tablet that is not RUNNING has no leader to serve the safe-time or chunk RPCs. Without
  // leaves the round would have no tablet to complete it.
  Status running =
      job.source_leaves.empty()
          ? STATUS(IllegalState, "Online schema change backfill tables were not resolved")
          : Status::OK();
  for (const auto& leaf : job.source_leaves) {
    if (running.ok()) {
      running = leaf->CheckAllActiveTabletsRunning();
    }
  }
  if (!running.ok()) {
    return WriteTerminal(job, OnlineSchemaChangeJobPB::FAILED, running, epoch);
  }

  // Asks every source tablet leader for a safe time (>= the client's floor) and reads at the max,
  // as the index backfill does. Only the leaders assign commit times, so no other clock can bound
  // the commits the scan must see.
  TabletInfos tablets;
  for (const auto& leaf : job.source_leaves) {
    auto leaf_tablets = VERIFY_RESULT(leaf->GetTablets());
    tablets.insert(tablets.end(), leaf_tablets.begin(), leaf_tablets.end());
  }
  const HybridTime floor(entry.backfilling_timestamp());
  auto min_cutoff = master_->clock()->Now();
  min_cutoff.MakeAtLeast(floor);
  {
    std::lock_guard lock(mutex_);
    job.safe_time_sent = true;
    job.safe_time_pending = tablets.size();
    job.read_time = floor;
  }
  LOG(INFO) << job.prefix << "Computing the read time across " << tablets.size() << " tablets";
  const std::weak_ptr<Job> weak_job = job_ptr;
  for (const auto& tablet : tablets) {
    Status sent;
    if (TEST_send_safe_time_) {
      sent = TEST_send_safe_time_(tablet, min_cutoff);
    } else {
      sent = std::make_shared<GetSafeTimeForTablet>(
          master_, catalog_manager_->AsyncTaskPool(), tablet, min_cutoff, epoch,
          /* wait_state */ nullptr,
          // Transactions that take table locks are fenced by the client's table lock, so
          // GetSafeTime does not abort them.
          /* only_abort_txns_not_using_table_locks */ true,
          Format("Online schema change backfill (job $0)", job.pg_job_id),
          [this, weak_job, epoch](const Status& status, HybridTime safe_time) {
            OnSafeTime(weak_job, epoch, status, safe_time);
          })->Launch();
    }
    if (!sent.ok()) {
      OnSafeTime(weak_job, epoch, sent, HybridTime());
    }
  }
  return false;
}

void OnlineSchemaChangeController::OnSafeTime(
    const std::weak_ptr<Job>& weak_job, const LeaderEpoch& epoch, const Status& status,
    HybridTime safe_time) {
  auto job = weak_job.lock();
  if (!job) {
    return;
  }
  bool round_over;
  {
    std::lock_guard lock(mutex_);
    if (!(epoch_ == epoch) || !job->safe_time_sent) {
      return;
    }
    if (status.ok()) {
      job->read_time.MakeAtLeast(safe_time);
    } else if (job->failure.ok()) {
      job->failure = status.CloneAndPrepend("Could not compute a safe read time");
    }
    if (job->safe_time_pending > 0) {
      --job->safe_time_pending;
    }
    round_over = job->safe_time_pending == 0 || !status.ok();
  }
  if (round_over) {
    ReconcileJob(job, epoch);
  }
}

Result<bool> OnlineSchemaChangeController::Copy(
    const JobPtr& job_ptr, const OnlineSchemaChangeJobPB& entry, const LeaderEpoch& epoch) {
  auto& job = *job_ptr;
  const auto key = OnlineSchemaChangeTabletKey(job.pg_job_id);
  struct Pending {
    TabletInfoPtr tablet;
    uint32_t leaf_oid;
    std::string start_key;
  };
  std::vector<Pending> pending;
  size_t total = 0;
  // The leaf OID comes from the leaf the tablets are enumerated under, not from the tablet's own
  // table, which for a colocated leaf would be the colocation parent.
  for (const auto& leaf : job.source_leaves) {
    const auto leaf_oid = VERIFY_RESULT(GetPgsqlTableOid(leaf->id()));
    for (auto& tablet : VERIFY_RESULT(leaf->GetTablets())) {
      ++total;
      std::optional<std::string> resume_key;
      {
        auto l = tablet->LockForRead();
        // Empty means drained; absent means not started.
        const auto it = l->pb.backfilled_until().find(key);
        if (it != l->pb.backfilled_until().end()) {
          resume_key = it->second;
        }
      }
      if (resume_key && resume_key->empty()) {
        continue;
      }
      pending.push_back({std::move(tablet), leaf_oid, resume_key.value_or(std::string())});
    }
  }
  if (pending.empty()) {
    LOG(INFO) << job.prefix << "All " << total << " source tablets have been copied";
    return WriteTerminal(job, OnlineSchemaChangeJobPB::SUCCESS, Status::OK(), epoch);
  }

  const std::weak_ptr<Job> weak_job = job_ptr;
  for (auto& p : pending) {
    tserver::TableBackfillChunkRequestPB req;
    req.set_tablet_id(p.tablet->tablet_id());
    req.set_read_at_hybrid_time(entry.backfilling_timestamp());
    req.set_namespace_name(entry.database_name());
    req.set_source_table_oid(p.leaf_oid);
    req.set_target_table_oid(entry.target_table_oid());
    req.set_transform_fn_oid(entry.transform_fn_oid());
    req.set_pg_job_id(job.pg_job_id);
    req.set_start_key(p.start_key);

    std::shared_ptr<TableBackfillChunk> task;
    if (!TEST_send_chunk_) {
      task = std::make_shared<TableBackfillChunk>(
          master_, catalog_manager_->AsyncTaskPool(), p.tablet, std::move(req), epoch,
          [this, weak_job, epoch, tablet = p.tablet](
              const Status& status, const tserver::TableBackfillChunkResponsePB& resp) {
            OnChunkDone(weak_job, epoch, tablet, status, resp);
          });
    }
    {
      std::lock_guard lock(mutex_);
      if (!job.chunks.emplace(p.tablet->id(), task).second) {
        continue;
      }
    }
    auto sent = task ? task->Launch() : TEST_send_chunk_(p.tablet, req);
    if (!sent.ok()) {
      OnChunkDone(weak_job, epoch, p.tablet, sent, tserver::TableBackfillChunkResponsePB());
    }
  }
  return false;
}

void OnlineSchemaChangeController::OnChunkDone(
    const std::weak_ptr<Job>& weak_job, const LeaderEpoch& epoch, const TabletInfoPtr& tablet,
    const Status& status, const tserver::TableBackfillChunkResponsePB& resp) {
  auto job = weak_job.lock();
  if (!job) {
    return;
  }
  // The tablet stays in flight until its response has been persisted. A pass in between would
  // otherwise resend it from the old key, and that duplicate, still retrying once its twin has
  // drained the tablet and the job has succeeded, could land after the pin is gone.
  bool drop_for_test = false;
  {
    std::lock_guard lock(mutex_);
    if (!(epoch_ == epoch) || !job->chunks.contains(tablet->id())) {
      return;
    }
    if (!status.ok()) {
      if (job->failure.ok()) {
        job->failure = status.CloneAndPrepend(Format("Copying tablet $0", tablet->id()));
      }
    } else if (auto& dropped = job->replays_for_test[tablet->id()];
               dropped < FLAGS_TEST_online_schema_change_backfill_replay_chunks_per_tablet) {
      ++dropped;
      drop_for_test = true;
    }
  }
  Status persisted;
  if (drop_for_test) {
    LOG(INFO) << job->prefix << "TEST: dropping the response of a chunk of tablet " << tablet->id()
              << " that copied " << resp.num_rows_processed() << " rows";
  } else if (status.ok()) {
    persisted = PersistChunk(
        *job, tablet, resp.backfilled_until(), resp.num_rows_processed(), epoch);
  }
  {
    std::lock_guard lock(mutex_);
    job->chunks.erase(tablet->id());
  }
  if (!persisted.ok()) {
    // The next tick resends the chunk from the last persisted key; the rows it rewrites are the
    // same.
    YB_LOG_EVERY_N_SECS(WARNING, 10) << job->prefix << "Could not persist the progress of tablet "
                                     << tablet->id() << ": " << persisted;
    return;
  }
  ReconcileJob(job, epoch);
}

Status OnlineSchemaChangeController::PersistChunk(
    const Job& job, const TabletInfoPtr& tablet, const std::string& backfilled_until,
    uint64_t num_rows, const LeaderEpoch& epoch) {
  // A late response must not write its key back after the job's release has cleared it.
  const auto entry = ReadJobEntry(*job.source_table);
  if (!entry || entry->pg_job_id() != job.pg_job_id ||
      entry->state() != OnlineSchemaChangeJobPB::IN_PROGRESS) {
    return Status::OK();
  }
  const auto key = OnlineSchemaChangeTabletKey(job.pg_job_id);
  auto l = tablet->LockForWrite();
  auto& pb = l.mutable_data()->pb;
  (*pb.mutable_backfilled_until())[key] = backfilled_until;
  (*pb.mutable_backfilled_rows())[key] += num_rows;
  RETURN_NOT_OK(catalog_manager_->sys_catalog_->Upsert(epoch, tablet));
  l.Commit();
  if (backfilled_until.empty()) {
    VLOG(1) << job.prefix << "Done copying tablet " << tablet->id();
  }
  return Status::OK();
}

Result<bool> OnlineSchemaChangeController::WriteReadTime(
    const Job& job, HybridTime read_time, const LeaderEpoch& epoch) {
  auto l = job.source_table->LockForWrite();
  auto& pb = l.mutable_data()->pb;
  if (!pb.has_online_schema_change_job()) {
    return false;
  }
  auto* entry = pb.mutable_online_schema_change_job();
  // A cancel recorded meanwhile wins; the next pass writes CANCELLED.
  if (entry->pg_job_id() != job.pg_job_id ||
      entry->state() != OnlineSchemaChangeJobPB::COMPUTING_READ_TIME ||
      entry->cancel_requested()) {
    return false;
  }
  entry->set_state(OnlineSchemaChangeJobPB::IN_PROGRESS);
  entry->set_backfilling_timestamp(read_time.ToUint64());
  RETURN_NOT_OK_PREPEND(
      catalog_manager_->sys_catalog_->Upsert(epoch, job.source_table),
      "Failed to persist online schema change backfill read time");
  l.Commit();
  return true;
}

Result<bool> OnlineSchemaChangeController::WriteTerminal(
    const Job& job, OnlineSchemaChangeJobPB::State state, const Status& failure,
    const LeaderEpoch& epoch) {
  // Read before the source's write lock: it takes the tablets' locks.
  const auto progress =
      VERIFY_RESULT(ReadOnlineSchemaChangeProgress(job.source_leaves, job.pg_job_id));

  auto l = job.source_table->LockForWrite();
  auto& pb = l.mutable_data()->pb;
  if (!pb.has_online_schema_change_job()) {
    return true;
  }
  auto* entry = pb.mutable_online_schema_change_job();
  // A terminal entry is never overwritten.
  if (entry->pg_job_id() != job.pg_job_id ||
      !IsOnlineSchemaChangeJobResumable(entry->state())) {
    return true;
  }
  // An outcome can be decided before a cancel lands. Postgres may already show that cancel, so
  // the outcome must be CANCELLED.
  if (entry->cancel_requested()) {
    state = OnlineSchemaChangeJobPB::CANCELLED;
  }
  entry->set_state(state);
  entry->set_num_rows_processed(progress.rows_done);
  entry->set_num_tablets_done(progress.tablets_done);
  entry->set_num_tablets_total(progress.tablets_total);
  if (state == OnlineSchemaChangeJobPB::FAILED && !failure.ok()) {
    entry->set_error_message(failure.message().ToBuffer());
  }
  entry->set_finished_at(master_->clock()->Now().ToUint64());
  RETURN_NOT_OK_PREPEND(
      catalog_manager_->sys_catalog_->Upsert(epoch, job.source_table),
      Format("Failed to persist online schema change job state $0",
             OnlineSchemaChangeJobPB::State_Name(state)));
  l.Commit();
  if (failure.ok()) {
    LOG(INFO) << job.prefix << "Finished as " << OnlineSchemaChangeJobPB::State_Name(state);
  } else {
    LOG(WARNING) << job.prefix << "Finished as " << OnlineSchemaChangeJobPB::State_Name(state)
                 << ": " << failure;
  }
  return true;
}

Result<bool> OnlineSchemaChangeController::Release(
    Job& job, const OnlineSchemaChangeJobPB& entry, const LeaderEpoch& epoch) {
  if (!PinReleaseDue(entry, master_->clock()->Now())) {
    return false;
  }
  // Every release is tried; the first failure is returned.
  Status status;
  const auto keep_first = [&status](const Status& s) {
    if (status.ok()) {
      status = s;
    }
  };
  for (const auto& table : job.target_leaves) {
    keep_first(
        catalog_manager_->UnpinOnlineSchemaChangeTargetDeleteMarkers(table, job.pg_job_id, epoch));
  }
  keep_first(ClearTabletProgress(job, epoch));
  RETURN_NOT_OK_PREPEND(status, "Failed to release what the finished job holds");
  return true;
}

Status OnlineSchemaChangeController::ClearTabletProgress(const Job& job, const LeaderEpoch& epoch) {
  const auto key = OnlineSchemaChangeTabletKey(job.pg_job_id);
  for (const auto& leaf : job.source_leaves) {
    for (const auto& tablet : VERIFY_RESULT(leaf->GetTablets())) {
      auto l = tablet->LockForWrite();
      auto& pb = l.mutable_data()->pb;
      if (!pb.backfilled_until().contains(key) && !pb.backfilled_rows().contains(key)) {
        continue;
      }
      pb.mutable_backfilled_until()->erase(key);
      pb.mutable_backfilled_rows()->erase(key);
      RETURN_NOT_OK(catalog_manager_->sys_catalog_->Upsert(epoch, tablet));
      l.Commit();
    }
  }
  return Status::OK();
}

void OnlineSchemaChangeController::AbortChunks(Job& job) {
  std::unordered_map<TabletId, std::shared_ptr<TableBackfillChunk>> chunks;
  {
    std::lock_guard lock(mutex_);
    chunks.swap(job.chunks);
    job.safe_time_sent = false;
  }
  // A chunk already running on a tserver is not stopped; it ends within
  // ysql_index_backfill_rpc_timeout_ms. Aborting only stops this master from resending it.
  for (const auto& [_, task] : chunks) {
    if (task) {
      task->AbortAndReturnPrevState(
          STATUS(Aborted, "Online schema change job is no longer driven here"),
          /* call_task_finisher */ true);
    }
  }
}

void OnlineSchemaChangeController::Discover(const LeaderEpoch& epoch) {
  // Pins are reaped off the pin rather than off the job entry: once a release is skipped, the
  // entry that named the pin may be gone, with the source dropped or the entry replaced by a later
  // job. Nothing else reaps such a pin, because the tablet validator leaves pinned tables alone.
  struct PinnedTable {
    scoped_refptr<TableInfo> table;
    int64_t pg_job_id;
  };
  std::vector<PinnedTable> pinned;
  // Jobs still entitled to a pin. pg job ids are unique only within a database.
  std::set<std::pair<NamespaceId, int64_t>> live_pins;
  std::vector<TableInfoPtr> to_drive;
  const auto now = master_->clock()->Now();
  {
    CatalogManager::SharedLock lock(catalog_manager_->mutex_);
    auto tables_it = catalog_manager_->tables_->GetPrimaryTables();
    // Pins must be collected before job entries. Start persists the entry before it takes the pin,
    // so every pin seen here has an entry that the second pass sees. The other order could read
    // the fresh pin of a job started between the passes as an orphan.
    for (const auto& table : tables_it) {
      auto l = table->LockForRead();
      if (l->pb.has_table_backfill_pin_job_id()) {
        pinned.push_back({table, l->pb.table_backfill_pin_job_id()});
      }
    }
    for (const auto& table : tables_it) {
      auto l = table->LockForRead();
      if (!l->pb.has_online_schema_change_job()) {
        continue;
      }
      const auto& job = l->pb.online_schema_change_job();
      const bool resumable = IsOnlineSchemaChangeJobResumable(job.state());
      const bool pin_due_later = !PinReleaseDue(job, now);
      if (resumable || pin_due_later) {
        // Before validation, so a malformed entry still keeps its job's pin.
        live_pins.emplace(table->namespace_id(), job.pg_job_id());
      }
      // A half-written entry would otherwise read as a resumable job at OID 0 (see
      // ValidateOnlineSchemaChangeJob), which would be driven against the wrong table.
      auto valid = ValidateOnlineSchemaChangeJob(job);
      if (!valid.ok()) {
        YB_LOG_EVERY_N_SECS(WARNING, 60)
            << "Ignoring unusable online schema change job entry on table " << table->ToString()
            << ": " << valid;
        continue;
      }
      if (resumable || pin_due_later) {
        to_drive.push_back(table);
        continue;
      }
    }
  }

  for (const auto& pin : pinned) {
    if (live_pins.contains({pin.table->namespace_id(), pin.pg_job_id})) {
      continue;
    }
    // `pin.table` may be a target or one of its indexes. Either way only pins with this job id are
    // released, and the other pinned tables are reached as their own entries.
    WARN_NOT_OK(
        catalog_manager_->UnpinOnlineSchemaChangeTargetDeleteMarkers(
            pin.table, pin.pg_job_id, epoch),
        Format(
            "Failed to reap the delete-marker retention pin of online schema change job $0 on $1",
            pin.pg_job_id, pin.table->ToString()));
  }

  for (const auto& table : to_drive) {
    FindJob(table, epoch, /* add = */ true);
  }
}

// ------------------------------------------------------------------------------------------------
// TableBackfillChunk
// ------------------------------------------------------------------------------------------------

TableBackfillChunk::TableBackfillChunk(
    Master* master, ThreadPool* callback_pool, TabletInfoPtr tablet,
    tserver::TableBackfillChunkRequestPB req, LeaderEpoch epoch, Callback callback)
    : RetryingTSRpcTaskWithTable(
          master, callback_pool, std::make_unique<PickLeaderReplica>(tablet), tablet->table(),
          std::move(epoch), /* async_task_throttler */ nullptr),
      tablet_(std::move(tablet)),
      req_(std::move(req)),
      callback_(std::move(callback)) {
  // No deadline for the task, refer to ComputeDeadline() for a single attempt deadline.
  deadline_ = MonoTime::Max();
}

Status TableBackfillChunk::Launch() {
  tablet_->table()->AddTask(shared_from_this());
  RETURN_NOT_OK_PREPEND(
      Run(),
      Format("Failed to send online schema change backfill chunk for $0", tablet_->ToString()));
  YB_LOG_EVERY_N_SECS(INFO, 30) << "Started " << description();
  return Status::OK();
}

MonoTime TableBackfillChunk::ComputeDeadline() const {
  MonoTime timeout = MonoTime::Now();
  timeout.AddDelta(MonoDelta::FromMilliseconds(FLAGS_ysql_index_backfill_rpc_timeout_ms));
  return MonoTime::Earliest(timeout, deadline_);
}

int TableBackfillChunk::num_max_retries() { return FLAGS_index_backfill_rpc_max_retries; }

int TableBackfillChunk::max_delay_ms() { return FLAGS_index_backfill_rpc_max_delay_ms; }

std::string TableBackfillChunk::description() const {
  return Format(
      "Backfill online schema change job $0 for tablet $1 from key '$2'", req_.pg_job_id(),
      tablet_id(), b2a_hex(req_.start_key()));
}

bool TableBackfillChunk::SendRequest(int attempt) {
  // A master that is no longer the leader must not keep resending: once its lease has expired, the
  // new leader may finish the job and release the pin, and a write that lands after that can bring
  // back a deleted row (see online_schema_change_backfill_pin_release_margin_ms).
  if (auto valid = master_->catalog_manager_impl()->IsEpochValid(epoch()); !valid.ok()) {
    TransitionToFailedState(server::MonitoredTaskState::kRunning, valid);
    return false;
  }
  auto req = req_;
  req.set_dest_uuid(permanent_uuid());
  req.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());
  ts_admin_proxy_->TableBackfillChunkAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG(1) << "Sent " << description() << " to " << permanent_uuid() << " (attempt " << attempt
          << ")";
  return true;
}

bool TableBackfillChunk::TabletIsGoneFromCatalog() const {
  return tablet_->LockForRead()->is_deleted();
}

void TableBackfillChunk::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    const auto status = StatusFromPB(resp_.error().status());
    // Unlike the index backfill, TABLET_NOT_FOUND is not fatal on the tablet server's word alone.
    // The source is not split during the job, so this usually means a replica move or a stale
    // leader, and the retry picks the leader again. Fail only if the master also sees the tablet
    // as deleted.
    if (resp_.error().code() == tserver::TabletServerErrorPB::TABLET_NOT_FOUND &&
        TabletIsGoneFromCatalog()) {
      LOG(WARNING) << "TS " << permanent_uuid()
                   << ": online schema change backfill failed for tablet " << tablet_->ToString()
                   << ", which the master also reports as gone; no further retry: " << status;
      TransitionToFailedState(server::MonitoredTaskState::kRunning, status);
    } else {
      LOG(WARNING) << "TS " << permanent_uuid()
                   << ": online schema change backfill failed for tablet " << tablet_->ToString()
                   << ": " << status;
    }
  } else {
    TransitionToCompleteState();
  }
  server::UpdateClock(resp_, master_->clock());
}

Status TableBackfillChunk::ResponseStatus() const {
  if (resp_.has_error()) {
    return StatusFromPB(resp_.error().status());
  }
  if (state() != server::MonitoredTaskState::kComplete) {
    return STATUS_FORMAT(InternalError, "$0 in state $1", description(), state());
  }
  return Status::OK();
}

void TableBackfillChunk::UnregisterAsyncTaskCallback() {
  if (state() == server::MonitoredTaskState::kAborted) {
    return;
  }
  callback_(ResponseStatus(), resp_);
}

}  // namespace master
}  // namespace yb
