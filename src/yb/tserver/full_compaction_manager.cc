//
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
//

#include "yb/tserver/full_compaction_manager.h"

#include <utility>

#include "yb/common/hybrid_time.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/background_task.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"

namespace {

constexpr int32_t kDefaultJitterFactorPercentage = 33;
// Indicates the maximum size for an abbreviated hash used for jitter.
constexpr uint64_t kMaxSmallHash = 1000000000;
// Indicates the factor at which we should force memory cleanup in FullCompactionManager.
// Multiplier for the maximum number of peers we should ever store in FullCompactionManager
// versus the number of peers stored in TsTabletManager.
constexpr double kPeerCleanupFactor = 2;

}; // namespace

DEFINE_RUNTIME_int32(scheduled_full_compaction_frequency_hours, 0,
              "Frequency with which full compactions should be scheduled on tablets. "
              "0 indicates the feature is disabled.");

DEFINE_RUNTIME_int32(scheduled_full_compaction_jitter_factor_percentage,
              kDefaultJitterFactorPercentage,
              "Percentage of scheduled_full_compaction_frequency_hours to be used as jitter when "
              "determining full compaction schedule per tablet. Jitter will be deterministically "
              "computed when scheduling a compaction, between 0 and (frequency * jitter factor) "
              "hours.");

DEFINE_NON_RUNTIME_int32(auto_compact_check_interval_sec, 60,
              "The interval at which the full compaction task checks for tablets "
              "eligible for compaction, in seconds. 0 indicates that the background task "
              "is fully disabled.");

DEFINE_RUNTIME_uint32(auto_compact_stat_window_seconds, 300,
              "Window of time (seconds) over which DocDB read statistics are analyzed for the "
              "purposes of triggering full compactions automatically to improve read performance. "
              "Will always be rounded up to be a multiple of auto_compact_check_interval_sec.");

DEFINE_RUNTIME_double(auto_compact_percent_obsolete, 99,
              "Percentage of obsolete keys (over total keys) read over a window of time that "
              "trigger an automatic full compaction on a tablet. Only keys that are past "
              "their history retention (and thus can be garbage collected) are considered.");

DEFINE_RUNTIME_uint32(auto_compact_min_obsolete_keys_found, 10000,
              "Minimum number of keys read in the window for an automatic full compaction to "
              "be triggered.");

DEFINE_RUNTIME_uint32(auto_compact_min_wait_between_seconds, 0,
              "Minimum wait time between automatic full compactions. Also applies to "
              "scheduled full compactions.");

DEFINE_RUNTIME_int32(auto_compact_memory_cleanup_interval_sec, 3600,
              "The frequency with which we should check whether cleanup is needed in the "
              "full compaction manager. -1 indicates we should disable clean up.");

using namespace std::literals;

namespace yb {
namespace tserver {

using tablet::TabletPeerPtr;

FullCompactionManager::FullCompactionManager(TSTabletManager* ts_tablet_manager)
    : ts_tablet_manager_(ts_tablet_manager),
      check_interval_sec_(ANNOTATE_UNPROTECTED_READ(FLAGS_auto_compact_check_interval_sec)) {
  last_cleanup_time_ = CoarseMonoClock::Now();
  SetFrequencyAndJitterFromFlags();
  LOG(INFO) << "Initialized full compaction manager"
      << " check_interval_sec: " << check_interval_sec_
      << " window_size_sec: " << ANNOTATE_UNPROTECTED_READ(FLAGS_auto_compact_stat_window_seconds)
      << " scheduled_compaction_frequency: " << compaction_frequency_.ToString()
      << " scheduled_jitter_factor: " << jitter_factor_;
}

void FullCompactionManager::ScheduleFullCompactions() {
  const auto peers = ts_tablet_manager_->GetTabletPeers();
  SetFrequencyAndJitterFromFlags();
  CollectDocDBStats(peers);
  DoScheduleFullCompactions(peers);
  CleanupIfNecessary(peers);
}

Status FullCompactionManager::Init() {
  if (check_interval_sec_ > 0) {
    bg_task_.reset(
        new BackgroundTask(std::function<void()>([this]() {
            ScheduleFullCompactions(); }),
        "full compaction manager", "compaction scheduler bgtask",
        MonoDelta::FromSeconds(check_interval_sec_).ToChronoMilliseconds()));
    RETURN_NOT_OK(bg_task_->Init());
  }
  return Status::OK();
}

void FullCompactionManager::Shutdown() {
  if (bg_task_) {
    bg_task_->Shutdown();
  }
}

void FullCompactionManager::CollectDocDBStats(
    const std::vector<tablet::TabletPeerPtr>& peers) {
  for (const auto& peer : peers) {
    const auto& tablet_id = peer->tablet_id();
    const auto& shared_tablet = peer->shared_tablet();
    if (shared_tablet && shared_tablet->metrics() != nullptr) {
      auto window_iter = tablet_stats_window_.emplace(
          tablet_id, KeyStatsSlidingWindow(check_interval_sec_)).first;
      const auto last_compact_time = shared_tablet->metadata()->last_full_compaction_time();
      window_iter->second.RecordCurrentStats(*shared_tablet->metrics(), last_compact_time);
    } else {
      tablet_stats_window_.erase(tablet_id);
    }
  }
}

namespace {

struct TabletIdPtrHasher {
  std::size_t operator()(const TabletId* tablet_id) const {
    return std::hash<TabletId>{}(*tablet_id);
  }
};

struct TabletIdPtrEq {
  bool operator()(const TabletId* id1, const TabletId* id2) const {
    return std::equal_to<void>{}(*id1, *id2);
  }
};

}  // namespace

void FullCompactionManager::CleanupIfNecessary(
    const std::vector<tablet::TabletPeerPtr>& peers) {
  const auto cleanup_frequency_sec =
      ANNOTATE_UNPROTECTED_READ(FLAGS_auto_compact_memory_cleanup_interval_sec);
  // If cleanup_frequency_sec is negative, then cleanup is disabled.
  // Cleanup isn't needed if it hasn't been enough time since the last cleanup
  // and if we don't have too many entries.
  if (cleanup_frequency_sec < 0 ||
      (CoarseMonoClock::Now() < last_cleanup_time_ + cleanup_frequency_sec * 1s &&
      tablet_stats_window_.size() <= peers.size() * kPeerCleanupFactor)) {
    return;
  }

  std::unordered_set<const TabletId*, TabletIdPtrHasher, TabletIdPtrEq> in_tablet_manager;
  for (const auto& peer : peers) {
    in_tablet_manager.insert(&peer->tablet_id());
  }
  const auto peer_not_exists_fn = [&in_tablet_manager](const auto& item) {
    return in_tablet_manager.find(&item.first) == in_tablet_manager.end();
  };
  // Erase any tablet entries that are not currently in the TsTabletManager's peers.
  std::erase_if(next_compact_time_per_tablet_, peer_not_exists_fn);
  std::erase_if(tablet_stats_window_, peer_not_exists_fn);
  last_cleanup_time_ = CoarseMonoClock::Now();
}

bool FullCompactionManager::ShouldCompactBasedOnStats(const TabletId& tablet_id) {
  auto window_iter = tablet_stats_window_.find(tablet_id);
  // If we don't have any stats collected, then don't compact.
  if (window_iter == tablet_stats_window_.end()) {
    return false;
  }

  const auto current_stats = window_iter->second.current_stats();
  // Check that we have seen enough keys in the window to consider compaction.
  if (current_stats.total <
      ANNOTATE_UNPROTECTED_READ(FLAGS_auto_compact_min_obsolete_keys_found)) {
    return false;
  }
  // Check that we meet the obsolete key percentage needed.
  const auto obsolete_percentage = current_stats.obsolete_key_percentage();
  if (obsolete_percentage <
      ANNOTATE_UNPROTECTED_READ(FLAGS_auto_compact_percent_obsolete)) {
    return false;
  }

  LOG(INFO) << Format("Tablet $0 is eligible for compaction based on recent document"
      " key access statistics. Percent obsolete: $1, total keys seen in window: $2",
      tablet_id, round(obsolete_percentage), current_stats.total);
  return true;
}

bool FullCompactionManager::CompactedTooRecently(const TabletPeerPtr& peer, const HybridTime& now) {
  // Check that we haven't compacted too recently.
  const auto min_compaction_wait =
      ANNOTATE_UNPROTECTED_READ(FLAGS_auto_compact_min_wait_between_seconds);
  if (min_compaction_wait > 0) {
    const auto last_compact_time =
        HybridTime(peer->tablet_metadata()->last_full_compaction_time());
    if (last_compact_time.AddDelta(MonoDelta::FromSeconds(min_compaction_wait)) > now) {
      return true;
    }
  }
  return false;
}

void FullCompactionManager::DoScheduleFullCompactions(
    const std::vector<tablet::TabletPeerPtr>& peers) {
  int num_scheduled = 0;
  PeerNextCompactList peers_to_compact = GetPeersEligibleForCompaction(peers);

  for (auto itr = peers_to_compact.begin(); itr != peers_to_compact.end(); itr++) {
    const auto peer = itr->second;
    const auto tablet = peer->shared_tablet();
    if (!tablet) {
      LOG(WARNING) << "Unable to schedule full compaction on tablet " << peer->tablet_id()
          << ": tablet not found.";
      continue;
    }
    Status s = tablet->TriggerFullCompactionIfNeeded(
        rocksdb::CompactionReason::kScheduledFullCompaction);
    if (s.ok()) {
      // Remove tablet from compaction times on successful schedule.
      next_compact_time_per_tablet_.erase(peer->tablet_id());
      num_scheduled++;
    } else {
      LOG(WARNING) << "Unable to schedule full compaction on tablet " << peer->tablet_id()
          << ": " << s.ToString();
    }
  }
  num_scheduled_last_execution_.store(num_scheduled);
}

PeerNextCompactList FullCompactionManager::GetPeersEligibleForCompaction(
    const std::vector<tablet::TabletPeerPtr>& peers) {
  const auto now = ts_tablet_manager_->server()->Clock()->Now();
  PeerNextCompactList compact_list;
  for (const auto& peer : peers) {
    const auto tablet_id = peer->tablet_id();
    const auto tablet = peer->shared_tablet();
    // If the tablet isn't eligible for compaction, remove it from our stored compaction
    // times and skip it.
    if (!tablet || !tablet->IsEligibleForFullCompaction()) {
      next_compact_time_per_tablet_.erase(tablet_id);
      continue;
    }
    // Check that we have not fully compacted this tablet too recently.
    if (CompactedTooRecently(peer, now)) {
      continue;
    }

    HybridTime next_compact_time;
    // Check if we should schedule a compaction based on stats collected.
    if (ShouldCompactBasedOnStats(tablet_id)) {
      next_compact_time = now;
    } else if (compaction_frequency_ != MonoDelta::kZero) {
      // If the next compaction time is pre-calculated, use that. Otherwise, calculate
      // a new one.
      next_compact_time = DetermineNextCompactTime(peer, now);
    }

    // If the tablet is ready to compact, then add it to the list.
    if (next_compact_time.is_valid() && next_compact_time <= now) {
      compact_list.insert(std::make_pair(next_compact_time, peer));
    }
  }
  return compact_list;
}

void FullCompactionManager::SetFrequencyAndJitterFromFlags() {
  const auto compaction_frequency = MonoDelta::FromHours(
      ANNOTATE_UNPROTECTED_READ(FLAGS_scheduled_full_compaction_frequency_hours));
  const auto jitter_factor =
      ANNOTATE_UNPROTECTED_READ(FLAGS_scheduled_full_compaction_jitter_factor_percentage);
  ResetFrequencyAndJitterIfNeeded(compaction_frequency, jitter_factor);
}

void FullCompactionManager::ResetFrequencyAndJitterIfNeeded(
    MonoDelta compaction_frequency, int jitter_factor) {
  if (jitter_factor > 100 || jitter_factor < 0) {
    YB_LOG_EVERY_N_SECS(WARNING, 300) << "Jitter factor " << jitter_factor
        << " is less than 0 or greater than 100. Using default "
        << kDefaultJitterFactorPercentage << " instead.";
    jitter_factor = kDefaultJitterFactorPercentage;
  }

  if (!compaction_frequency_.Initialized() ||
      compaction_frequency_ != compaction_frequency ||
      jitter_factor_ != jitter_factor) {
    compaction_frequency_ = compaction_frequency;
    jitter_factor_ = jitter_factor;
    max_jitter_ = compaction_frequency * jitter_factor / 100;
    // Reset all pre-calculated compaction times stored in memory when compaction
    // frequency or jitter factor change.
    next_compact_time_per_tablet_.clear();
  }
}

HybridTime FullCompactionManager::DetermineNextCompactTime(
    const TabletPeerPtr& peer, HybridTime now) {
  // First, see if we've pre-calculated a next compaction time for this tablet. If not, it will
  // need to be calculated based on the last full compaction time.
  const auto tablet_id = peer->tablet_id();
  const auto next_compact_iter = next_compact_time_per_tablet_.find(tablet_id);
  if (next_compact_iter == next_compact_time_per_tablet_.end()) {
    const auto last_compact_time = peer->tablet_metadata()->last_full_compaction_time();
    const auto jitter = CalculateJitter(tablet_id, last_compact_time);
    const auto next_compact_time = CalculateNextCompactTime(
        tablet_id, now, HybridTime(last_compact_time), jitter);
    // Store the calculated next compaction time in memory.
    next_compact_time_per_tablet_[tablet_id] = next_compact_time;
    return next_compact_time;
  }
  return next_compact_iter->second;
}

HybridTime FullCompactionManager::CalculateNextCompactTime(
    const TabletId& tablet_id,
    const HybridTime now,
    const HybridTime last_compact_time,
    const MonoDelta jitter) const {
  // If we have no metadata on the last compaction time, then schedule the next compaction for
  // (jitter) time from now. Otherwise, schedule the next compaction for (frequency - jitter)
  // from the last full compaction.
  return last_compact_time.is_special() ?
      now.AddDelta(jitter)
      : last_compact_time.AddDelta(compaction_frequency_ - jitter);
}

void FullCompactionManager::TEST_DoScheduleFullCompactionsWithManualValues(
    MonoDelta compaction_frequency, int jitter_factor) {
  const auto peers = ts_tablet_manager_->GetTabletPeers();
  ResetFrequencyAndJitterIfNeeded(compaction_frequency, jitter_factor);
  DoScheduleFullCompactions(peers);
}

namespace {

size_t hash_value_for_jitter(
    const TabletId& tablet_id,
    const uint64_t last_compact_time) {
  size_t seed = 0;
  boost::hash_combine(seed, tablet_id);
  boost::hash_combine(seed, last_compact_time);
  return seed;
}

}  // namespace

MonoDelta FullCompactionManager::CalculateJitter(
    const TabletId& tablet_id,
    const uint64_t last_compact_time) const {
  // Use a smaller hash value to make calculations more efficient.
  const auto small_hash =
      hash_value_for_jitter(tablet_id, last_compact_time) % kMaxSmallHash;
  return max_jitter_ / kMaxSmallHash * small_hash;
}

KeyStatsSlidingWindow::KeyStatsSlidingWindow(int32_t check_interval_sec)
    : check_interval_sec_(check_interval_sec) {
}

void KeyStatsSlidingWindow::RecordCurrentStats(
    const tablet::TabletMetrics& metrics, uint64_t last_compact_time) {
  // Reset the window anytime a full compaction or new tablet instance is detected.
  if (last_compact_time > last_compaction_time_
      || metrics.InstanceId() != metrics_instance_id_) {
    ResetWindow(last_compact_time, metrics.InstanceId());
  }

  ComputeWindowSizeAndIntervals();

  // If we aren't storing any intervals, we can clear any lingering statistics and return.
  if (expected_intervals_ == 0) {
    key_stats_window_.clear();
    return;
  }

  // Reduce the size of the window to the number of expected intervals.
  // Unless the window size has changed, this will just be erasing a single stat from the front
  // (i.e. sliding the window).
  if (key_stats_window_.size() > expected_intervals_) {
    const auto begin = key_stats_window_.begin();
    key_stats_window_.erase(begin, begin + (key_stats_window_.size() - expected_intervals_));
  }

  // Finally, push the latest metrics into the back of the window.
  key_stats_window_.push_back({
      metrics.Get(tablet::TabletCounters::kDocDBKeysFound),
      metrics.Get(tablet::TabletCounters::kDocDBObsoleteKeysFoundPastCutoff)});
}

KeyStatistics KeyStatsSlidingWindow::current_stats() const {
  // To calculate statistics, expected_intervals_ needs to be greater than 0
  // and the key_stats_window_ deque needs (expected_intervals_ + 1) values.
  if (expected_intervals_ == 0 || key_stats_window_.size() <= expected_intervals_) {
    return KeyStatistics{ 0, 0 };
  }
  return { key_stats_window_.back().total - key_stats_window_.front().total,
      key_stats_window_.back().obsolete_cutoff - key_stats_window_.front().obsolete_cutoff };
}

void KeyStatsSlidingWindow::ComputeWindowSizeAndIntervals() {
  const auto window_size_sec =
      ANNOTATE_UNPROTECTED_READ(FLAGS_auto_compact_stat_window_seconds);
  // Keep (window duration / interval duration), rounded up, intervals.
  expected_intervals_ = (window_size_sec == 0 || check_interval_sec_ == 0)
      ? 0 : (window_size_sec - 1) / check_interval_sec_ + 1;
}

void KeyStatsSlidingWindow::ResetWindow(uint64_t last_compaction_time, uint64_t instance_id) {
  key_stats_window_.clear();
  last_compaction_time_ = last_compaction_time;
  metrics_instance_id_ = instance_id;
}

} // namespace tserver
} // namespace yb
