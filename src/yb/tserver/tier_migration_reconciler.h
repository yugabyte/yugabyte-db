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

#include <chrono>
#include <memory>
#include <mutex>

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/tserver/tserver_fwd.h"

#include "yb/util/metrics_fwd.h"
#include "yb/util/status_fwd.h"

namespace yb {

class BackgroundTask;

namespace tserver {

// What one TierMigrationReconciler pass over this tserver's tablets did and found.
struct TierReconcilePassStats {
  // RUNNING tablets with a persisted target tier.
  uint32_t tablets_with_target = 0;
  // Of those, the ones this pass scanned (not in flight, not failed).
  uint32_t tablets_visited = 0;
  uint32_t moves_scheduled = 0;
  // Tablets whose migration state changed during this pass.
  uint32_t state_changes = 0;
  // Migration state of every tablet with a target, as of the end of the pass.
  uint32_t tablets_in_progress = 0;
  uint32_t tablets_done = 0;
  uint32_t tablets_failed = 0;
};

// Background driver of tiered-storage migration, one per tserver. A tablet's target tier is
// persisted in its superblock (by AlterTabletTier, by creation from a tablespace, and carried
// along by remote bootstrap and splits) and points new flushes and compaction outputs at the
// target disk, but nothing moves the SSTs that were already on the old disk except a migration
// pass. AlterTabletTier kicks one; this thread runs the rest: every pass rescans each tablet
// with a target (Tablet::RunTierReconcilePass) and schedules a move for whatever is still outside
// the target directory, so failed and deferred moves, files landed on the old disk by in-flight
// flushes, restarts and new replicas all converge without anyone asking again.
//
// It is also the only thing that decides a tablet is done (a scan of its own found nothing to
// move) or that retrying is pointless (TierMigrationStatus::State::kFailed after
// FLAGS_tier_migration_max_consecutive_failed_passes).
class TierMigrationReconciler {
 public:
  explicit TierMigrationReconciler(TSTabletManager* ts_tablet_manager);
  ~TierMigrationReconciler();

  // Starts the periodic thread unless tiered storage is disabled or
  // FLAGS_tier_migration_reconcile_interval_sec is 0.
  Status Init();
  void Shutdown();

  // Runs a pass as soon as the thread is free instead of waiting out the interval. No-op when the
  // thread is not running.
  void Wake();

  // One pass, synchronously. Serialized with the thread's own passes.
  TierReconcilePassStats RunPass();

  // What every tablet gets at open: the counters its passes feed, and a way to wake this thread.
  const std::shared_ptr<tablet::TierMigrationHooks>& tablet_hooks() const {
    return tablet_hooks_;
  }

  bool TEST_thread_running() const { return bg_task_ != nullptr; }

 private:
  // The thread's task: applies interval changes, honors a runtime pause, then RunPass.
  void RunScheduledPass();

  TSTabletManager* const ts_tablet_manager_;

  std::shared_ptr<tablet::TierMigrationHooks> tablet_hooks_;
  scoped_refptr<AtomicGauge<uint32_t>> tablets_in_progress_;
  scoped_refptr<AtomicGauge<uint32_t>> tablets_done_;
  scoped_refptr<AtomicGauge<uint32_t>> tablets_failed_;
  scoped_refptr<Counter> moves_scheduled_;

  // Set in Init, before any tablet is opened, so the OpenTablet wake-ups need no synchronization.
  std::unique_ptr<BackgroundTask> bg_task_;
  // Only the thread reads or writes it.
  std::chrono::milliseconds interval_{0};

  // Serializes passes.
  std::mutex pass_mutex_;
};

}  // namespace tserver
}  // namespace yb
