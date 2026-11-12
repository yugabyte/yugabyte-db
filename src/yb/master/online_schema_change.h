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

#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/common/entity_ids.h"
#include "yb/common/hybrid_time.h"
#include "yb/gutil/ref_counted.h"
#include "yb/master/async_rpc_tasks_base.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/leader_epoch.h"
#include "yb/server/monitored_task.h"
#include "yb/tserver/tserver_admin.pb.h"
#include "yb/util/monotime.h"
#include "yb/util/status_fwd.h"

namespace yb {
namespace master {

class CatalogManager;
class OnlineSchemaChangeTest;

// True for COMPUTING_READ_TIME and IN_PROGRESS, the states in which the job copies.
bool IsOnlineSchemaChangeJobResumable(OnlineSchemaChangeJobPB::State state);

// Every field of OnlineSchemaChangeJobPB is optional, so a half-written entry would read as a
// resumable IN_PROGRESS job with OID 0.
Status ValidateOnlineSchemaChangeJob(const OnlineSchemaChangeJobPB& job);

// The key of a job's resume key and row count in SysTabletsEntryPB.
std::string OnlineSchemaChangeTabletKey(int64_t pg_job_id);

// The pg_job_id of the unfinished job (IsOnlineSchemaChangeJobResumable) on `table`, if any. The
// split manager refuses to split the table while there is one: each source tablet's scan is bounded
// by its partition for the life of the copy, so a split would shrink that bound and leave the right
// child uncopied. Once the job is terminal no chunk reads the source again. Takes the table's read
// lock.
std::optional<int64_t> CopyingOnlineSchemaChangeJobReading(const TableInfo& table);

// A job's progress as recorded on the tablets of `source_leaves`: how many have drained, and how
// many rows they have copied. Only current tablets are counted, so a tablet that has since split
// cannot count as done.
struct OnlineSchemaChangeProgress {
  int32_t tablets_done = 0;
  int32_t tablets_total = 0;
  uint64_t rows_done = 0;
};
Result<OnlineSchemaChangeProgress> ReadOnlineSchemaChangeProgress(
    const std::vector<scoped_refptr<TableInfo>>& source_leaves, int64_t pg_job_id);

// One online schema change in the master's jobs tracker, shown on the /tasks page. It only reports;
// the job runs from its durable entry.
class OnlineSchemaChangeJob : public server::MonitoredTask {
 public:
  OnlineSchemaChangeJob(const TableInfoPtr& source_table, int64_t pg_job_id);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kOnlineSchemaChange;
  }
  std::string type_name() const override { return "Online Schema Change"; }
  std::string description() const override { return description_; }

  server::MonitoredTaskState AbortAndReturnPrevState(
      const Status& status, bool call_task_finisher) override;

  // No-op once the job is in a terminal state.
  void Finish(server::MonitoredTaskState state);

 private:
  const std::string description_;
};

// Drives every unfinished online schema change of this master leader from its durable entry, in
// the manner of a Kubernetes controller. Each pass over a job reads the entry and the source
// tablets' resume keys, compares them with the RPCs this leader has in flight, and sends what is
// missing or writes the next state:
//
//   COMPUTING_READ_TIME  once in-flight splits of the source have settled, one GetSafeTime per
//                        source tablet; when the last answers, the max is written as the read time
//                        together with IN_PROGRESS.
//   IN_PROGRESS          one chunk RPC in flight per source tablet that has not drained, from its
//                        resume key. A chunk response only persists the tablet's new resume key and
//                        row count. Once every tablet has drained: SUCCESS.
//   cancel_requested     CANCELLED, in place of any other next state.
//   terminal             releases the pin and the resume keys, then forgets the job.
//
// A job is visited on every bg tasks tick, and also right after anything that may move it (an RPC
// response, Start or a cancel), so a step does not wait for the tick. Durable writes are
// compare-and-set on the entry, so two passes that race cannot overwrite each other's outcome.
//
// The in-memory state is only what this leader has sent, and is dropped with the leader term.
// Responses to a dropped term are ignored, and the next leader starts over from the durable entry:
// a chunk is resent from the tablet's resume key. A chunk RPC still running on a tserver at that
// point may write the same rows as its replacement, which is harmless: both blind-write the same
// transform output at the same read time.
class OnlineSchemaChangeController {
 public:
  OnlineSchemaChangeController(Master* master, CatalogManager* catalog_manager);
  ~OnlineSchemaChangeController();

  // Called by the bg tasks loop as leader of `epoch`. A new epoch drops the previous one's state
  // and rescans every table for jobs; later ticks rescan every
  // online_schema_change_backfill_resume_interval_ms.
  void RunOnce(const LeaderEpoch& epoch);

  // Called when this master stops being the leader.
  void Reset();

  // Writes the durable entry of a new job, or finds the one an earlier attempt of the same request
  // wrote, sets the delete-marker retention pin, and starts driving the job.
  // Fails if another unfinished job reads `source_table`, or if this one has already finished.
  Status Start(
      const TableInfoPtr& source_table, const OnlineSchemaChangeJobPB& job,
      const LeaderEpoch& epoch);

  // Visits the job on `source_table` now, e.g. after a cancel has been written.
  void Reconcile(const TableInfoPtr& source_table, const LeaderEpoch& epoch);

 private:
  friend class OnlineSchemaChangeTest;

  struct Job;
  using JobPtr = std::shared_ptr<Job>;

  // The job of `source_table` in this epoch's map, added if `add`. Null for another epoch.
  JobPtr FindJob(const TableInfoPtr& source_table, const LeaderEpoch& epoch, bool add);
  void Forget(const JobPtr& job, server::MonitoredTaskState monitored_state);
  // Moves to `epoch`, returning the jobs of the previous one for DropJobs.
  std::vector<JobPtr> SwitchEpochUnlocked(const LeaderEpoch& epoch) REQUIRES(mutex_);
  // Stops resending the jobs' RPCs. Called without mutex_.
  void DropJobs(const std::vector<JobPtr>& jobs);

  // Runs ReconcileOnce until a pass writes nothing. A visit that arrives while a pass runs makes
  // that pass go again instead.
  void ReconcileJob(const JobPtr& job, const LeaderEpoch& epoch);
  // Returns true if it wrote something the next pass should look at.
  Result<bool> ReconcileOnce(const JobPtr& job, const LeaderEpoch& epoch);

  // Rescans every table: adds unfinished jobs, and reaps the pins of finished ones.
  void Discover(const LeaderEpoch& epoch);

  // The first visit in a leader term: counts the resume, resolves the target, and re-asserts the
  // pin.
  Result<bool> Adopt(Job& job, const OnlineSchemaChangeJobPB& entry, const LeaderEpoch& epoch);
  Result<bool> ComputeReadTime(
      const JobPtr& job, const OnlineSchemaChangeJobPB& entry, const LeaderEpoch& epoch);
  Result<bool> Copy(
      const JobPtr& job, const OnlineSchemaChangeJobPB& entry, const LeaderEpoch& epoch);
  // Releases what a terminal job still holds. Returns false while a SUCCESS job must keep its pin.
  Result<bool> Release(Job& job, const OnlineSchemaChangeJobPB& entry, const LeaderEpoch& epoch);

  Status ResolveTables(Job& job, const OnlineSchemaChangeJobPB& entry, bool skip_missing);
  Status PinTarget(const Job& job, const LeaderEpoch& epoch);
  bool SourceSplitsSettled(const Job& job);

  // Moves the unfinished entry to `state`, or to CANCELLED if a cancel has been recorded. Returns
  // false if the entry is no longer this job's or has already finished.
  Result<bool> WriteTerminal(
      const Job& job, OnlineSchemaChangeJobPB::State state, const Status& failure,
      const LeaderEpoch& epoch);
  // COMPUTING_READ_TIME -> IN_PROGRESS. Returns false, writing nothing, once a cancel is recorded.
  Result<bool> WriteReadTime(const Job& job, HybridTime read_time, const LeaderEpoch& epoch);

  void OnSafeTime(
      const std::weak_ptr<Job>& weak_job, const LeaderEpoch& epoch, const Status& status,
      HybridTime safe_time);
  void OnChunkDone(
      const std::weak_ptr<Job>& weak_job, const LeaderEpoch& epoch, const TabletInfoPtr& tablet,
      const Status& status, const tserver::TableBackfillChunkResponsePB& resp);
  // Persists a chunk's resume key and rows on its tablet, unless the job has finished.
  Status PersistChunk(
      const Job& job, const TabletInfoPtr& tablet, const std::string& backfilled_until,
      uint64_t num_rows, const LeaderEpoch& epoch);
  Status ClearTabletProgress(const Job& job, const LeaderEpoch& epoch);
  // Stops resending the chunk RPCs this leader has in flight for the job.
  void AbortChunks(Job& job);

  // Whether a finished job may release its pin. A SUCCESS job keeps it until no chunk sent before
  // the terminal write can still be writing.
  static bool PinReleaseDue(const OnlineSchemaChangeJobPB& entry, HybridTime now);

  Master* const master_;
  CatalogManager* const catalog_manager_;

  std::mutex mutex_;
  // Signalled whenever a job's pass ends.
  std::condition_variable pass_done_;
  LeaderEpoch epoch_ GUARDED_BY(mutex_);
  // By source table id. A source has at most one job.
  std::unordered_map<TableId, JobPtr> jobs_ GUARDED_BY(mutex_);
  CoarseTimePoint next_discovery_ GUARDED_BY(mutex_);

  // Tests replace the RPCs, which no tserver would answer in a master-only cluster.
  std::function<Status(const TabletInfoPtr& tablet, HybridTime min_cutoff)> TEST_send_safe_time_;
  std::function<Status(const TabletInfoPtr& tablet, const tserver::TableBackfillChunkRequestPB&)>
      TEST_send_chunk_;
};

// Sends one chunk of a job's copy to a source tablet's leader. The tserver copies from the
// request's start_key until a row or time bound and answers with the key to resume from, empty once
// the tablet has drained.
class TableBackfillChunk : public RetryingTSRpcTaskWithTable {
 public:
  using Callback =
      std::function<void(const Status&, const tserver::TableBackfillChunkResponsePB&)>;

  TableBackfillChunk(
      Master* master, ThreadPool* callback_pool, TabletInfoPtr tablet,
      tserver::TableBackfillChunkRequestPB req, LeaderEpoch epoch, Callback callback);

  Status Launch();

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kTableBackfillChunk;
  }
  std::string type_name() const override { return "Table Backfill Chunk"; }
  std::string description() const override;

  MonoTime ComputeDeadline() const override;
  int num_max_retries() override;
  int max_delay_ms() override;

 private:
  TabletId tablet_id() const override { return tablet_->id(); }
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;
  void UnregisterAsyncTaskCallback() override;

  // Whether the master's own metadata agrees with a TABLET_NOT_FOUND from the tablet server, i.e.
  // the tablet is deleted rather than merely absent from the server this attempt reached.
  bool TabletIsGoneFromCatalog() const;
  Status ResponseStatus() const;

  const TabletInfoPtr tablet_;
  tserver::TableBackfillChunkRequestPB req_;
  const Callback callback_;
  tserver::TableBackfillChunkResponsePB resp_;
};

}  // namespace master
}  // namespace yb
