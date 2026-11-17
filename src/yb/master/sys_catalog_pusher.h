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

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/common/hybrid_time.h"
#include "yb/common/opid.h"

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/master/master_fwd.h"

#include "yb/server/server_fwd.h"

#include "yb/util/mem_tracker_fwd.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/status_fwd.h"

namespace yb::master {

// The pusher's metrics outlive the pusher, which the master recreates when it leaves shell mode.
std::shared_ptr<SysCatalogPusherMetrics> CreateSysCatalogPusherMetrics(
    const scoped_refptr<MetricEntity>& metric_entity, const MemTrackerPtr& parent_mem_tracker,
    server::Clock* clock);

// Pushes the system catalog WAL to the tserver copies of the catalog. Runs on the master leader:
// each push cycle certifies a safe time S and a WAL position E such that every catalog transaction
// committed at or before S is in the WAL at or before E, then sends every tserver holding a YSQL
// operation lease the change records it lacks up to E, the last batch carrying S.
class SysCatalogPusher {
 public:
  SysCatalogPusher(
      CatalogManager& catalog_manager, std::shared_ptr<SysCatalogPusherMetrics> metrics);
  ~SysCatalogPusher();

  Status Start();
  void StartShutdown();
  void CompleteShutdown();

  // Wakes the pusher thread for a cycle that is not skipped, even when the WAL has not moved.
  // Requests made before that cycle starts share it. A request is not kept past that cycle, even
  // when it fails or this master is not the leader.
  // TODO: Take a time the cycle must certify a safe time at or above, and complete each request
  // once its tserver's copy has reached that time. The callers to come: a push after the release of
  // a version-incrementing DDL, which without object locking is the only repair for a lost release,
  // and tservers that wait for their copy to reach a time.
  void RequestCycle();

  struct PushCycleResult {
    // The certified safe time and position of the cycle.
    HybridTime safe_time;
    int64_t end_index;
    // Targets whose copy reached end_index and safe_time.
    std::vector<std::string> caught_up_ts_uuids;
  };

  // Runs one push cycle and returns when every target has answered or been skipped. The cycle
  // certifies a new safe time even when the WAL has not moved. Returns nullopt without pushing
  // when this master is not the leader, or when the YSQL lease or invalidation messages are off.
  // Cycles run one at a time.
  Result<std::optional<PushCycleResult>> TEST_RunCycle();

  struct TargetState {
    // The copy's position and safe time from its last answer; unset until it answers.
    std::optional<OpId> position;
    HybridTime safe_time;
  };

  // Returns the last answer of the tserver with the given uuid, if it is a target.
  std::optional<TargetState> TEST_GetTargetState(const std::string& ts_uuid);

  // The number of times a cycle has waited a full slice for intents to resolve.
  int64_t TEST_NumResolveIntentsTimeouts() const { return TEST_resolve_intents_timeouts_; }

  // The number of times a cycle has failed to build a batch.
  int64_t TEST_NumFailedBatchBuilds() const { return TEST_failed_batch_builds_; }

 private:
  struct Target {
    TSDescriptorPtr ts;
    uint64_t lease_epoch = 0;
    TargetState state;
    // The target answered that it keeps no copy, and has not answered otherwise since, under its
    // current lease.
    bool no_copy = false;
    // The target has no local catalog service, which it gains only by restarting under a new lease,
    // so it is not pushed to again under its current lease.
    bool no_service = false;

    // State of the current cycle.
    bool done = false;
    int stalls = 0;

    bool CaughtUp(int64_t end_index, HybridTime safe_time) const {
      return state.position && state.position->index >= end_index && state.safe_time.is_valid() &&
             state.safe_time >= safe_time;
    }
  };
  struct Inbox;
  struct Batch;
  struct CycleState;
  class RunThread;

  void Run();
  // Waits for the timeout, returning false if the pusher is shutting down.
  bool WaitUnlessClosing(std::chrono::milliseconds timeout);

  struct CycleLeader;
  struct Certificate;
  // With skip_if_wal_unchanged, returns nullopt without a cycle when the WAL has not moved since
  // the last successful cycle and that cycle caught up every current lease holder that keeps a
  // copy.
  Result<std::optional<PushCycleResult>> DoRunCycle(bool skip_if_wal_unchanged)
      REQUIRES(cycle_mutex_);
  // Runs a cycle that has not been skipped, timed from cycle_start.
  Result<std::optional<PushCycleResult>> CertifyAndPush(
      CycleLeader leader, CoarseTimePoint cycle_start) REQUIRES(cycle_mutex_);
  // Returns what a cycle runs on, or nullopt when this master is not the leader, or when the YSQL
  // lease or invalidation messages are off.
  Result<std::optional<CycleLeader>> PrepareCycle() REQUIRES(cycle_mutex_);
  // Whether the last cycle caught up every target that keeps a copy, the lease holders are still
  // those targets, and the WAL has not moved since.
  Result<bool> WalUnchangedSinceCaughtUp(const CycleLeader& leader) REQUIRES(cycle_mutex_);
  // Certifies the safe time S and end index E of a cycle. Returns nullopt when the leader term
  // changed meanwhile.
  Result<std::optional<Certificate>> Certify(const CycleLeader& leader) REQUIRES(cycle_mutex_);
  // Calls attempt with deadlines of at most kShutdownCheckInterval until it returns true, fails,
  // the deadline passes, or the pusher shuts down. attempt returns false when its deadline passed
  // first.
  Status RetryInSlices(
      CoarseTimePoint deadline, const char* what,
      const std::function<Result<bool>(CoarseTimePoint)>& attempt) REQUIRES(cycle_mutex_);
  // Records the leader term, kUnknownTerm when this master is not the leader. On a change, forgets
  // what the copies last answered.
  void NoteLeaderTerm(int64_t term) REQUIRES(cycle_mutex_);
  // Whether the leader term differs from the one last recorded, recording the current one if so.
  bool LeaderTermChanged() REQUIRES(cycle_mutex_);
  // Whether this master is still the leader but cannot act as one for now.
  bool LeaderLeaseLapsed() REQUIRES(cycle_mutex_);
  struct TargetSummary {
    std::vector<std::string> caught_up_ts_uuids;
    size_t without_copy = 0;
    // WAL entries between the end and the copy furthest behind it.
    int64_t max_lag = 0;
  };
  // Summarizes the targets against a cycle's end and safe time.
  TargetSummary SummarizeTargets(int64_t end_index, HybridTime safe_time) REQUIRES(cycle_mutex_);
  // Updates the target gauges after a cycle that sent to its targets.
  void UpdateTargetMetrics(const TargetSummary& summary) REQUIRES(cycle_mutex_);
  Status SendToTargets(CycleState* cycle) REQUIRES(cycle_mutex_);
  // Whether the cycle's lease holders, with their lease epochs, are exactly the targets of the last
  // cycle.
  bool LeaseHoldersAreTargets(const CycleLeader& leader) REQUIRES(cycle_mutex_);
  // Makes the cycle's lease holders the targets, keeping the state of each one that holds the same
  // lease.
  void RebuildTargets(CycleState* cycle) REQUIRES(cycle_mutex_);
  // Builds the batch that starts at from, stores it in the cycle and returns it.
  Result<const Batch*> BuildBatch(CycleState* cycle, const OpId& from) REQUIRES(cycle_mutex_);
  void Send(CycleState* cycle, Target* target) REQUIRES(cycle_mutex_);

  CatalogManager& catalog_manager_;
  const std::shared_ptr<SysCatalogPusherMetrics> metrics_;

  std::mutex wake_mutex_;
  // Every wait on a cycle's thread ends on shutdown: it waits on wake_cv_ or the inbox, or runs in
  // slices of at most kShutdownCheckInterval.
  bool closing_ GUARDED_BY(wake_mutex_) = false;
  std::condition_variable wake_cv_;
  bool cycle_requested_ GUARDED_BY(wake_mutex_) = false;

  // Serializes cycles and their state. In production only the pusher thread runs cycles; tests also
  // run and inspect them from their own threads.
  std::mutex cycle_mutex_;
  std::unordered_map<std::string, Target> targets_ GUARDED_BY(cycle_mutex_);
  // Shared with the RPC callbacks, which may outlive this object.
  const std::shared_ptr<Inbox> inbox_;
  // The end index of the last successful cycle, and whether it caught up every target that keeps
  // a copy.
  int64_t last_end_index_ GUARDED_BY(cycle_mutex_) = -1;
  bool last_cycle_caught_up_all_ GUARDED_BY(cycle_mutex_) = false;
  int64_t leader_term_ GUARDED_BY(cycle_mutex_) = OpId::kUnknownTerm;

  std::atomic<int64_t> TEST_resolve_intents_timeouts_{0};
  std::atomic<int64_t> TEST_failed_batch_builds_{0};

  // Set by Start, before the pusher is shared, and kept until the pusher is destroyed. Last, so it
  // is destroyed before the members its thread uses.
  std::unique_ptr<RunThread> thread_;
};

}  // namespace yb::master
