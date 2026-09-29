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

#include "yb/tserver/wal_sync_sweeper.h"

#include <algorithm>
#include <array>
#include <vector>

#include "yb/common/common_types.pb.h"

#include "yb/consensus/log.h"

#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/background_task.h"
#include "yb/util/drive_io_stats.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"

DECLARE_bool(durable_wal_write);

DEFINE_RUNTIME_bool(enable_wal_sync_sweeper, false,
    "Whether a background thread periodically fsyncs WALs that have unsynced data, without "
    "waiting for --interval_durable_wal_write_ms or --bytes_durable_wal_write_mb to be reached. "
    "This is the only mechanism that covers a leader's own WAL, an RF=1 tablet, and the window "
    "after heartbeats have stopped. Off by default pending measurement of its cost.");

DEFINE_NON_RUNTIME_uint32(wal_sync_sweeper_interval_ms, 250,
    "How often the WAL sync sweeper walks this server's tablets. Also the upper bound this "
    "mechanism can put on how long an appended entry stays unsynced, though the effective bound "
    "on any one tablet is looser whenever its drive is throttled. Read once when the sweeper "
    "thread is created, unlike --enable_wal_sync_sweeper which is re-read every tick.");

DEFINE_RUNTIME_double(wal_sync_sweeper_drive_busy_fraction, 0.5,
    "Skip a drive for this pass when the fraction of wall-clock time it spent inside "
    "fsync()/fdatasync() since the previous pass exceeds this. Measured from the per-drive "
    "counters, and can exceed 1.0 because concurrent syncs each contribute their full duration, "
    "so a value above 1.0 is meaningful and means 'only back off when several syncs are in "
    "flight continuously'. At 0 a drive is skipped if it did any fsync at all in the previous "
    "window, which is the most conservative setting; there is no value that disables sweeping, "
    "use --enable_wal_sync_sweeper for that.");

DEFINE_RUNTIME_uint32(wal_sync_sweeper_max_tablets_per_pass, 0,
    "Stop each pass after starting this many fsyncs, keeping the highest-priority tablets. 0 "
    "means no cap. A cap makes the worst-case burst predictable but starves the tail of the "
    "priority order, so prefer the per-drive throttle and use this only as a backstop.");

DEFINE_RUNTIME_bool(wal_sync_sweeper_skip_index_tablets, false,
    "Exclude index tablets from the sweep entirely rather than putting them last. Note what this "
    "trades: an index tablet that loses its unsynced tail while the base table keeps the write "
    "leaves index scans silently missing rows that sequential scans still return, and nothing "
    "reconciles the two. Provided so the cost of including them can be measured.");

METRIC_DEFINE_gauge_uint64(server, wal_sync_sweeper_syncs_started, "WAL Sync Sweeper Syncs Started",
    yb::MetricUnit::kOperations,
    "Number of background WAL fsyncs the sweeper has started since the server came up. Read "
    "against drive_sync_count for the share of this node's fsyncs that proactive flushing is "
    "responsible for.",
    yb::EXPOSE_AS_COUNTER);

METRIC_DEFINE_gauge_uint64(server, wal_sync_sweeper_tablets_skipped_busy_drive,
    "WAL Sync Sweeper Tablets Skipped For Busy Drive",
    yb::MetricUnit::kOperations,
    "Number of tablet-visits the sweeper declined because the tablet's drive was already over "
    "--wal_sync_sweeper_drive_busy_fraction. A number that stays near zero means the throttle is "
    "not engaging and the sweep is effectively unthrottled; a number that dominates "
    "wal_sync_sweeper_syncs_started means the node's storage cannot absorb proactive flushing.",
    yb::EXPOSE_AS_COUNTER);

METRIC_DEFINE_gauge_uint64(server, wal_sync_sweeper_tablets_skipped_over_budget,
    "WAL Sync Sweeper Tablets Skipped For Pass Budget",
    yb::MetricUnit::kOperations,
    "Number of tablet-visits dropped because --wal_sync_sweeper_max_tablets_per_pass was hit. "
    "Non-zero means the sweep is not reaching the bottom of its priority order.",
    yb::EXPOSE_AS_COUNTER);

METRIC_DEFINE_event_stats(server, wal_sync_sweeper_pass_time, "WAL Sync Sweeper Pass Time",
    yb::MetricUnit::kMicroseconds,
    "Wall time of one sweep pass. This is enumeration and submission only - the fsyncs themselves "
    "happen on the log-sync pool - so it should stay small even when the drives are slow.");

namespace yb::tserver {

using tablet::TabletPeerPtr;

using TabletPeers = std::vector<TabletPeerPtr>;

namespace {

// Unsigned difference that floors at zero instead of wrapping. Both places it is used subtract two
// independently-sampled approximate counters, so neither difference is guaranteed non-negative.
uint64_t SaturatingSub(uint64_t a, uint64_t b) {
  return a > b ? a - b : 0;
}

} // namespace

WalSyncSweeper::WalSyncSweeper(
    TSTabletManager* tablet_manager, const scoped_refptr<MetricEntity>& metric_entity)
    : tablet_manager_(tablet_manager),
      syncs_started_(METRIC_wal_sync_sweeper_syncs_started.Instantiate(metric_entity, 0)),
      tablets_skipped_busy_drive_(
          METRIC_wal_sync_sweeper_tablets_skipped_busy_drive.Instantiate(metric_entity, 0)),
      tablets_skipped_over_budget_(
          METRIC_wal_sync_sweeper_tablets_skipped_over_budget.Instantiate(metric_entity, 0)),
      pass_time_(METRIC_wal_sync_sweeper_pass_time.Instantiate(metric_entity)) {}

WalSyncSweeper::~WalSyncSweeper() = default;

Status WalSyncSweeper::Init() {
  // The thread is started unconditionally and ticks at the configured interval whether or not the
  // feature is on, so that --enable_wal_sync_sweeper stays runtime-settable without a restart. An
  // off tick costs one flag read.
  bg_task_ = std::make_unique<BackgroundTask>(
      std::function<void()>([this]() { Sweep(); }), "tablet manager", "wal sync sweeper",
      std::chrono::milliseconds(FLAGS_wal_sync_sweeper_interval_ms));
  return bg_task_->Init();
}

void WalSyncSweeper::StartShutdown() {
  if (bg_task_) {
    bg_task_->StartShutdown();
  }
}

void WalSyncSweeper::CompleteShutdown() {
  if (bg_task_) {
    bg_task_->CompleteShutdown();
  }
}

void WalSyncSweeper::TEST_RunOnePass() {
  Sweep();
}

WalSyncSweeper::Priority WalSyncSweeper::ClassifyPeer(const tablet::TabletPeer& peer) {
  const auto& meta = peer.tablet_metadata();
  if (meta->table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
    return Priority::kTransactionStatus;
  }
  if (meta->IsSysCatalog()) {
    return Priority::kSystem;
  }
  if (meta->is_index()) {
    return Priority::kIndex;
  }
  return Priority::kUserTable;
}

bool WalSyncSweeper::TEST_IsDriveBusy(const DriveIoStats* drive) const {
  auto it = drive_state_.find(drive);
  return it != drive_state_.end() && it->second.busy;
}

// The measure is a duty cycle - the share of wall-clock time the device spent inside fsync - and
// not a count of fsyncs per second, deliberately. A drive serving many cheap fsyncs has a low duty
// cycle and genuinely can absorb more; one serving a few expensive ones cannot. A count threshold
// gets both of those backwards.
void WalSyncSweeper::RefreshDriveState(const std::vector<const DriveIoStats*>& drives) {
  const auto now = MonoTime::Now();
  const auto busy_fraction = FLAGS_wal_sync_sweeper_drive_busy_fraction;

  // A rate needs a denominator worth dividing by. Two passes close together - a shortened
  // interval, or a test driving passes back to back - would divide one real fsync duration by a
  // near-zero window and call any drive saturated. Below this floor the previous verdict and the
  // previous sample are both kept, so the next pass measures over a window that means something.
  const auto kMinSampleInterval = MonoDelta::FromMilliseconds(20);

  for (const auto* stats : drives) {
    auto [it, inserted] = drive_state_.try_emplace(stats);
    auto& state = it->second;

    const auto sync_micros = stats->sync_micros();
    const auto proactive_micros = stats->proactive_sync_micros();

    if (!inserted && state.last_sampled.Initialized()) {
      const auto elapsed = now - state.last_sampled;
      if (elapsed < kMinSampleInterval) {
        continue;
      }

      // Our own fsyncs come out of the numerator. Throttling on the raw drive_sync_time would mean
      // measuring the fsyncs this sweeper itself started, which does not pace it - it makes it
      // oscillate. A pass that consumes more device time than one window (exactly the situation a
      // throttle exists for) would push the next pass over the threshold, that pass would skip
      // everything, the pass after would measure near zero and sweep again: the effective interval
      // silently doubles and the load arrives in bursts. It would also make the flag impossible to
      // reason about, since 0.5 would mean "the workload plus my own last pass used half the
      // device".
      //
      // Subtracted per window rather than by carrying a running workload-only total, and that
      // matters. The two counters are measured at different scopes on purpose:
      // DriveIoStats::RecordSync times the fdatasync inside PosixWritableFile, while
      // RecordProactiveSync times the whole of Log::DoSync around it. The outer measurement is
      // always the larger, and PosixWritableFile::Sync declines to record at all when it has no
      // pending bytes, so on a drive whose syncs are mostly ours the proactive counter drifts above
      // the raw one. Differencing cumulative totals would let that drift accumulate and eventually
      // mask real workload activity for good; differencing per window confines it to the window it
      // happened in, and the next window starts from fresh baselines.
      const auto total_delta = SaturatingSub(sync_micros, state.last_sync_micros);
      const auto proactive_delta =
          SaturatingSub(proactive_micros, state.last_proactive_sync_micros);
      const auto workload_delta = SaturatingSub(total_delta, proactive_delta);

      // The ratio can exceed 1.0 when syncs overlap, which is the point: that is what a saturated
      // device looks like from here, and it is why a busy_fraction above 1.0 is a meaningful
      // setting rather than a disabled one.
      state.busy = static_cast<double>(workload_delta) /
                   static_cast<double>(elapsed.ToMicroseconds()) > busy_fraction;
    } else {
      // First sight of this drive. Presume idle rather than busy: being wrong costs one coalesced
      // fsync per tablet on it and the next pass corrects the verdict, whereas presuming busy
      // would delay every drive's first sweep by a full interval after startup for nothing.
      // Sweep() makes the same assumption for a drive with no entry at all, so that the two places
      // cannot disagree.
      state.busy = false;
    }

    state.last_sync_micros = sync_micros;
    state.last_proactive_sync_micros = proactive_micros;
    state.last_sampled = now;
  }
}

void WalSyncSweeper::Sweep() {
  if (!FLAGS_enable_wal_sync_sweeper) {
    return;
  }
  // Under durable_wal_write every append is already fsynced in-line, so both of Log's background
  // entry points decline unconditionally and there is nothing here for the sweep to do. Returning
  // at the top rather than discovering that per tablet also keeps the counters honest: without
  // this, a pass would enumerate every peer and charge each one a skip, which reads as a sweeper
  // being throttled rather than a sweeper that structurally cannot act.
  if (FLAGS_durable_wal_write) {
    return;
  }
  const auto pass_start = MonoTime::Now();

  TabletPeers peers = tablet_manager_->GetTabletPeers();

  // The distinct drives these peers sit on, deduplicated so no drive is sampled twice in one pass.
  std::vector<const DriveIoStats*> drives;
  for (const auto& peer : peers) {
    if (!peer->log_available()) {
      continue;
    }
    if (const auto* stats = peer->log()->drive_io_stats()) {
      drives.push_back(stats);
    }
  }
  std::sort(drives.begin(), drives.end());
  drives.erase(std::unique(drives.begin(), drives.end()), drives.end());
  RefreshDriveState(drives);

  // Bucket rather than sort: there are four classes and up to a few thousand peers, and the order
  // within a class does not matter.
  std::array<std::vector<TabletPeerPtr>, kNumPriorities> buckets;
  const bool skip_index = FLAGS_wal_sync_sweeper_skip_index_tablets;
  for (auto& peer : peers) {
    if (!peer->log_available()) {
      continue;
    }
    const auto priority = ClassifyPeer(*peer);
    if (skip_index && priority == Priority::kIndex) {
      continue;
    }
    buckets[static_cast<size_t>(priority)].push_back(std::move(peer));
  }

  const auto max_per_pass = FLAGS_wal_sync_sweeper_max_tablets_per_pass;
  uint64_t started = 0;
  uint64_t skipped_busy = 0;
  uint64_t skipped_budget = 0;

  size_t no_throttle_signal = 0;
  for (auto& bucket : buckets) {
    for (const auto& peer : bucket) {
      auto* log = peer->log();

      // Cheapest question first, and it has to be first for the skip counters to mean anything.
      // Most tablets on the kind of node the sweeper is aimed at are quiet and already synced,
      // so testing the drive verdict before this would charge a "skipped for busy drive" against
      // visits that would have been no-ops either way - and that counter is the headline
      // instrument for judging whether the throttle is engaging. It also skips the drive-map
      // lookup for the majority of visits.
      if (!log->has_unsynced_data()) {
        continue;
      }

      const auto* stats = log->drive_io_stats();
      if (stats == nullptr) {
        // No per-drive counters for this WAL, so there is no way to tell whether the device can
        // afford another fsync. Skipping is the conservative choice: the whole argument for
        // flushing ahead of the thresholds rests on being able to back off when the drive is
        // saturated, and without the signal that argument does not hold. Not counted per pass
        // because this is a whole-server condition rather than a per-tablet one - see the warning
        // below.
        ++no_throttle_signal;
        continue;
      }

      // A drive with no entry is one RefreshDriveState did not see this pass, which in practice
      // means a peer whose log became available between the two loops. Treated as not busy, to
      // agree with the first-sight case there rather than quietly using the opposite default.
      auto it = drive_state_.find(stats);
      if (it != drive_state_.end() && it->second.busy) {
        ++skipped_busy;
        continue;
      }
      if (max_per_pass > 0 && started >= max_per_pass) {
        ++skipped_budget;
        continue;
      }
      if (log->SyncInBackgroundIfPending()) {
        ++started;
      }
    }
  }

  syncs_started_->IncrementBy(started);
  tablets_skipped_busy_drive_->IncrementBy(skipped_busy);
  tablets_skipped_over_budget_->IncrementBy(skipped_budget);
  pass_time_->Increment((MonoTime::Now() - pass_start).ToMicroseconds());

  if (no_throttle_signal > 0) {
    // Loud rather than a silent no-op, because from the outside "enabled but doing nothing" and
    // "enabled and finding nothing to do" look identical.
    YB_LOG_EVERY_N_SECS(WARNING, 300)
        << "WAL sync sweeper is enabled but skipped " << no_throttle_signal << " of "
        << peers.size() << " tablet peers because their WAL is under no registered drive: it has "
        << "no way to tell whether the device is already saturated. Enable "
        << "--export_drive_io_metrics, or turn off --enable_wal_sync_sweeper.";
  }

  VLOG(2) << "WAL sync sweep: started " << started << " fsyncs, skipped " << skipped_busy
          << " for busy drives, " << skipped_budget << " for the per-pass budget and "
          << no_throttle_signal << " for having no drive counters, across " << peers.size()
          << " peers";
}

} // namespace yb::tserver
