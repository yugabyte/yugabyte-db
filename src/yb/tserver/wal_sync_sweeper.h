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

#include <memory>
#include <unordered_map>

#include "yb/gutil/ref_counted.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/util/metrics_fwd.h"
#include "yb/util/monotime.h"
#include "yb/util/status_fwd.h"

namespace yb {

class BackgroundTask;
class DriveIoStats;

namespace tserver {

class TSTabletManager;

// Periodically walks this server's tablet peers and starts a background fsync of any WAL that has
// data appended but not yet on disk, without waiting for the size or age thresholds in
// Log::FindSyncType() to be reached.
//
// Why this exists alongside the check on the UpdateConsensus path: that check only fires where
// heartbeats arrive, which is a follower of a live leader. It does nothing for a leader's own WAL,
// nothing at RF=1, and nothing during the window where heartbeats have stopped - a partition, or a
// leader that has just died. That last case matters most, because a downed leader is the first
// half of the double fault that turns unsynced WAL into lost committed data. This sweeper covers
// all three, and unlike the heartbeat check it can flush ahead of the thresholds rather than at
// them.
//
// Why being aggressive is defensible: an extra fsync costs the workload nothing on its own. It
// costs something only when it competes with a latency-critical fsync for a device that is already
// saturated. So the throttle is on exactly that and nothing else - per-drive fsync duty cycle read
// from DriveIoStats, rather than a fixed rate budget. On a quiet drive the sweep flushes
// everything it finds; on a drive already spending most of its time inside fsync it does nothing
// and leaves the existing thresholds in charge.
//
// The sweep is also self-limiting in a way that is easy to miss: after a tablet's WAL is synced,
// periodic_sync_needed_ is false until something is appended again, so the fsync rate this
// produces for any one tablet is bounded by min(sweep rate, that tablet's append rate) - not by
// the sweep rate alone. A tablet nobody is writing to gets flushed exactly once.
//
// Tablets are visited in priority order (see Priority), by the blast radius of losing a tablet's
// unsynced tail rather than by how much data that is. Be clear about what that ordering does at the
// default settings, though: nothing. The busy verdict is per drive and applies to every tablet on
// it regardless of visit order, and --wal_sync_sweeper_max_tablets_per_pass defaults to 0, so every
// bucket is drained in full. The buckets only change an outcome once a pass is truncated.
//
// Which means the index-tablet decision is, at the defaults, include-versus-exclude rather than
// deprioritise-versus-skip. Indexes are included, last; Priority::kIndex has the reasoning.
// --wal_sync_sweeper_skip_index_tablets excludes them so the difference can be measured.
//
// Disabled by default (--enable_wal_sync_sweeper) until its cost on a busy node is measured.
class WalSyncSweeper {
 public:
  WalSyncSweeper(TSTabletManager* tablet_manager, const scoped_refptr<MetricEntity>& metric_entity);
  ~WalSyncSweeper();

  // Starts the background thread, whether or not --enable_wal_sync_sweeper is currently set. The
  // flag is runtime-settable and is re-read on every tick, so a tick with the feature off costs
  // one flag read and nothing else; starting the thread unconditionally is what lets the feature
  // be turned on without a restart, which is the point during evaluation.
  Status Init();

  void StartShutdown();
  void CompleteShutdown();

  // Runs one pass on the calling thread. Tests only.
  void TEST_RunOnePass();

  // Test-only seams onto the throttle. RefreshDriveState takes drives rather than peers precisely
  // so that its state machine - independence between drives, the proactive subtraction, the
  // minimum sampling window - can be driven deterministically against synthetic counters, with no
  // server and no real fsyncs. Those are the properties the whole aggressive-flush argument rests
  // on and they are not otherwise reachable from a test.
  void TEST_RefreshDriveState(const std::vector<const DriveIoStats*>& drives) {
    RefreshDriveState(drives);
  }
  bool TEST_IsDriveBusy(const DriveIoStats* drive) const;

 private:
  // Where a tablet sits in the order the sweep considers, ordered by the blast radius of losing
  // its unsynced tail rather than by how much data that is.
  enum class Priority {
    // A lost transaction-status write can strand or wrongly resolve transactions that touched
    // tablets all over the cluster, so this is the one class where the damage is unbounded by the
    // tablet's own contents.
    kTransactionStatus = 0,
    // The sys catalog and other system tablets: small, rarely written, and expensive to be wrong.
    kSystem = 1,
    kUserTable = 2,
    // Last rather than skipped. Skipping would rely on an index being rebuildable, but that only
    // helps once somebody notices, and the failure mode here is not a missing row. If an index
    // tablet loses its unsynced tail while the base table keeps its write, index scans silently
    // miss rows that sequential scans still return, and nothing in the system reconciles the two.
    // Deprioritizing is a cost decision; skipping converts a durability gap into a silent
    // wrong-results gap. --wal_sync_sweeper_skip_index_tablets skips them instead, for measurement.
    kIndex = 3,
  };
  static constexpr size_t kNumPriorities = 4;

  // Per-drive throttle state, carried across passes so a rate can be derived.
  //
  // Only the time spent in fsync is tracked, not the number of them. Duty cycle is the better
  // signal of the two and subsumes it: a drive doing many cheap fsyncs has a low duty cycle and
  // genuinely can absorb more, while a drive doing a few expensive ones has a high duty cycle and
  // cannot, and a count-per-second threshold gets both of those backwards.
  //
  // Note what the priority buckets in Sweep() do and do not achieve given this. The verdict is per
  // drive and applies to every tablet on it regardless of visit order, so with
  // --wal_sync_sweeper_max_tablets_per_pass at its default of 0 every bucket is drained in full
  // and the ordering changes no outcome at all. The buckets only bite once a pass is truncated.
  struct DriveState {
    // Both totals as of the previous sample, kept separately rather than pre-subtracted so that the
    // workload share is differenced within a window. See RefreshDriveState for why that matters -
    // the two counters are measured at different scopes and a running difference would let the
    // discrepancy accumulate.
    uint64_t last_sync_micros = 0;
    uint64_t last_proactive_sync_micros = 0;
    MonoTime last_sampled = MonoTime::kUninitialized;
    // Recomputed once per pass; every tablet on this drive consults the same verdict.
    bool busy = false;
  };

  void Sweep();

  // Samples each drive and decides which are too busy to add to. Done once per pass rather than
  // per tablet so that all tablets on a drive see one consistent verdict, and takes drives rather
  // than peers so that the decision does not depend on anything about tablets.
  void RefreshDriveState(const std::vector<const DriveIoStats*>& drives);

  static Priority ClassifyPeer(const tablet::TabletPeer& peer);

  TSTabletManager* const tablet_manager_;

  std::unique_ptr<BackgroundTask> bg_task_;

  // Keyed by the DriveIoStats pointer, which the registry guarantees is stable for the lifetime of
  // the process. Only ever touched from the sweep thread (and from TEST_RunOnePass).
  std::unordered_map<const DriveIoStats*, DriveState> drive_state_;

  scoped_refptr<AtomicGauge<uint64_t>> syncs_started_;
  scoped_refptr<AtomicGauge<uint64_t>> tablets_skipped_busy_drive_;
  scoped_refptr<AtomicGauge<uint64_t>> tablets_skipped_over_budget_;
  scoped_refptr<EventStats> pass_time_;
};

} // namespace tserver
} // namespace yb
