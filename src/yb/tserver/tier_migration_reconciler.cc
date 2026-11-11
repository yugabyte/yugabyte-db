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

#include "yb/tserver/tier_migration_reconciler.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/dynamic_annotations.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_types.pb.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/background_task.h"
#include "yb/util/enums.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/status_log.h"

DECLARE_bool(enable_tiered_storage);

DEFINE_RUNTIME_int32(tier_migration_reconcile_interval_sec, 30,
    "Seconds between passes of the tiered-storage migration reconciler, which moves SST files of "
    "tablets whose target storage tier differs from where the files are. 0 at startup disables "
    "the reconciler; 0 at runtime pauses it (passes and wake-ups then do nothing).");

DEFINE_RUNTIME_uint32(tier_migration_max_consecutive_failed_passes, 5,
    "Migration passes in a row that hit a hard error without moving a single file after which the "
    "tiered-storage reconciler stops retrying a tablet (its migration state becomes FAILED) until "
    "the next AlterTabletTier. 0 means never stop.");

METRIC_DEFINE_gauge_uint32(server, ts_tier_migration_tablets_in_progress,
    "Tablets Migrating Between Storage Tiers", yb::MetricUnit::kUnits,
    "Number of tablets on this tserver with a target storage tier that the reconciler has not "
    "yet found fully on it.");

METRIC_DEFINE_gauge_uint32(server, ts_tier_migration_tablets_done,
    "Tablets Converged on Their Storage Tier", yb::MetricUnit::kUnits,
    "Number of tablets on this tserver whose live SST files are all on their target storage "
    "tier, as of the last reconciler pass.");

METRIC_DEFINE_gauge_uint32(server, ts_tier_migration_tablets_failed,
    "Tablets Whose Storage Tier Migration Stopped", yb::MetricUnit::kUnits,
    "Number of tablets on this tserver the reconciler gave up on after "
    "tier_migration_max_consecutive_failed_passes failed passes. A new AlterTabletTier resets "
    "them.");

METRIC_DEFINE_counter(server, ts_tier_migration_moves_scheduled,
    "Storage Tier SST Moves Scheduled", yb::MetricUnit::kFiles,
    "Number of SST file moves the tiered-storage reconciler has scheduled.");

METRIC_DEFINE_counter(server, ts_tier_migration_files_moved,
    "SST Files Moved Between Storage Tiers", yb::MetricUnit::kFiles,
    "Number of SST files that migration passes have moved onto their tablet's target storage "
    "tier, whether scheduled by the reconciler or by AlterTabletTier.");

METRIC_DEFINE_counter(server, ts_tier_migration_files_failed,
    "SST File Moves Between Storage Tiers Failed", yb::MetricUnit::kFiles,
    "Number of SST file moves that hit a hard error. Busy files that were deferred to the next "
    "pass are not counted.");

namespace yb::tserver {

using tablet::TierMigrationStatus;

TierMigrationReconciler::TierMigrationReconciler(TSTabletManager* ts_tablet_manager)
    : ts_tablet_manager_(ts_tablet_manager) {
  const auto& entity = ts_tablet_manager_->server()->metric_entity();
  tablet_hooks_ = std::make_shared<tablet::TierMigrationHooks>(tablet::TierMigrationHooks{
      .files_moved = METRIC_ts_tier_migration_files_moved.Instantiate(entity),
      .files_failed = METRIC_ts_tier_migration_files_failed.Instantiate(entity),
      // Safe for as long as a tablet holds this: TSTabletManager shuts every tablet down before
      // it destroys the reconciler, and Wake after Shutdown does nothing.
      .wake_reconciler = [this] { Wake(); },
  });
  tablets_in_progress_ = METRIC_ts_tier_migration_tablets_in_progress.Instantiate(entity, 0);
  tablets_done_ = METRIC_ts_tier_migration_tablets_done.Instantiate(entity, 0);
  tablets_failed_ = METRIC_ts_tier_migration_tablets_failed.Instantiate(entity, 0);
  moves_scheduled_ = METRIC_ts_tier_migration_moves_scheduled.Instantiate(entity);
}

TierMigrationReconciler::~TierMigrationReconciler() = default;

Status TierMigrationReconciler::Init() {
  if (!FLAGS_enable_tiered_storage) {
    LOG(INFO) << "Tier migration reconciler disabled (enable_tiered_storage=false)";
    return Status::OK();
  }
  const auto interval_sec = ANNOTATE_UNPROTECTED_READ(FLAGS_tier_migration_reconcile_interval_sec);
  if (interval_sec <= 0) {
    LOG(INFO) << "Tier migration reconciler disabled (tier_migration_reconcile_interval_sec="
              << interval_sec << ")";
    return Status::OK();
  }
  interval_ = MonoDelta::FromSeconds(interval_sec).ToChronoMilliseconds();
  bg_task_ = std::make_unique<BackgroundTask>(
      [this] { RunScheduledPass(); }, "tier migration reconciler",
      "tier migration reconciler bgtask", interval_);
  RETURN_NOT_OK(bg_task_->Init());
  LOG(INFO) << "Started tier migration reconciler, interval_sec=" << interval_sec;
  return Status::OK();
}

void TierMigrationReconciler::Shutdown() {
  if (bg_task_) {
    bg_task_->Shutdown();
  }
}

void TierMigrationReconciler::Wake() {
  if (!bg_task_) {
    return;
  }
  // The only failure is a shutdown already in progress, and passes still finishing on tablets
  // that are shutting down wake this then as a matter of course.
  auto status = bg_task_->Wake();
  if (!status.ok() && !status.IsShutdownInProgress()) {
    LOG(WARNING) << "Could not wake tier migration reconciler: " << status;
  }
}

void TierMigrationReconciler::RunScheduledPass() {
  const auto interval_sec = ANNOTATE_UNPROTECTED_READ(FLAGS_tier_migration_reconcile_interval_sec);
  if (interval_sec <= 0) {
    return;  // Paused at runtime; keeps ticking at the last interval so un-pausing takes effect.
  }
  const auto interval = MonoDelta::FromSeconds(interval_sec).ToChronoMilliseconds();
  if (interval != interval_) {
    interval_ = interval;
    bg_task_->SetInterval(interval);
  }
  RunPass();
}

TierReconcilePassStats TierMigrationReconciler::RunPass() {
  std::lock_guard lock(pass_mutex_);
  TierReconcilePassStats stats;

  const tablet::TierReconcileOptions options{
      .failed_pass_threshold =
          ANNOTATE_UNPROTECTED_READ(FLAGS_tier_migration_max_consecutive_failed_passes),
  };

  const auto count_state = [&stats](TierMigrationStatus::State state) {
    switch (state) {
      case TierMigrationStatus::State::kDone:
        ++stats.tablets_done;
        return;
      case TierMigrationStatus::State::kFailed:
        ++stats.tablets_failed;
        return;
      case TierMigrationStatus::State::kNone:
      case TierMigrationStatus::State::kInProgress:
        ++stats.tablets_in_progress;
        return;
    }
    FATAL_INVALID_ENUM_VALUE(TierMigrationStatus::State, state);
  };

  for (const auto& peer : ts_tablet_manager_->GetTabletPeers()) {
    if (peer->state() != tablet::RaftGroupStatePB::RUNNING) {
      continue;
    }
    const auto tablet = peer->shared_tablet_maybe_null();
    if (!tablet || tablet->metadata()->target_storage_tier().empty()) {
      continue;
    }
    ++stats.tablets_with_target;

    const auto status = tablet->tier_migration_status();
    if (status.pass_in_flight || status.state == TierMigrationStatus::State::kFailed) {
      count_state(status.state);
      continue;
    }

    auto result = tablet->RunTierReconcilePass(options);
    ++stats.tablets_visited;
    if (!result.ok()) {
      // ServiceUnavailable: an AlterTabletTier started a pass since the snapshot above; it is that
      // pass's job now. Anything else is the tablet shutting down or its DB closed.
      if (!result.status().IsServiceUnavailable()) {
        YB_LOG_EVERY_N_SECS(WARNING, 60)
            << "Tier migration reconciler skipped tablet " << peer->tablet_id() << ": "
            << result.status();
      }
      count_state(status.state);
      continue;
    }
    stats.moves_scheduled += narrow_cast<uint32_t>(result->scheduled);
    if (result->state_changed) {
      ++stats.state_changes;
    }
    count_state(result->status.state);
  }

  tablets_in_progress_->set_value(stats.tablets_in_progress);
  tablets_done_->set_value(stats.tablets_done);
  tablets_failed_->set_value(stats.tablets_failed);
  moves_scheduled_->IncrementBy(stats.moves_scheduled);

  if (stats.moves_scheduled > 0 || stats.state_changes > 0) {
    LOG(INFO) << "Tier migration reconciler pass: scheduled " << stats.moves_scheduled
              << " SST move(s) across " << stats.tablets_visited << " of "
              << stats.tablets_with_target << " tablet(s) with a target tier; in_progress="
              << stats.tablets_in_progress << " done=" << stats.tablets_done
              << " failed=" << stats.tablets_failed;
  }
  return stats;
}

}  // namespace yb::tserver
