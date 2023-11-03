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

#pragma once

#include <map>
#include <unordered_map>

#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/hybrid_time.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/tserver/tserver_fwd.h"

#include "yb/util/threadpool.h"

namespace yb {

class BackgroundTask;

namespace tserver {

typedef std::multimap<HybridTime, tablet::TabletPeerPtr> PeerNextCompactList;

// Contains metrics readings related to docdb key accesses.
// Can represent either a snapshot of metrics, or a delta between two snapshots (e.g. delta
// within a window of time).
struct KeyStatistics {
  // Total keys accessed.
  uint64_t total;
  // Obsolete keys accessed that are past their history retention cutoff (i.e. eligible
  // for compation).
  uint64_t obsolete_cutoff;

  // Calculates the percentage of obsolete keys read (that are past their history
  // cutoff) vs the total number of keys read in the window.
  double obsolete_key_percentage() const {
    return total == 0 ? 0 : 100.0 * obsolete_cutoff / total;
  }
};

// KeyStatsSlidingWindow tracks a sliding window of docdb key statistics over a window of time
// dictated by tserver flag auto_compact_stat_window_seconds (and can be changed).
// The slide interval is dicteded by check_interval_sec, determined when the window is created
// (should not be changed).
class KeyStatsSlidingWindow {
 public:
  explicit KeyStatsSlidingWindow(int32_t check_interval_sec);

  // Records the current docdb key statistics into the sliding window, removing any statistics
  // that have expired from the window.
  // If the tablet has been fully compacted since last run, the sliding window will be reset.
  void RecordCurrentStats(const tablet::TabletMetrics& metrics, uint64_t last_compact_time);

  // Returns the current statistics readings held by the window.
  // If the window does not yet have enough stored intervals (or if expected_intervals_ is 0),
  // will return a default KeyStatistics with 0 for all values.
  KeyStatistics current_stats() const;

 private:
  void ComputeWindowSizeAndIntervals();

  // Resets the sliding window and its internal variables, including the last compaction time
  // and an identifier for the metrics instance.
  // Called every time the tablet is fully compacted or a new tablet instance is detected.
  void ResetWindow(uint64_t last_compaction_time, uint64_t instance_count);

  const int32_t check_interval_sec_;

  // Stores the statistics readings for each "check_interval_sec" interval.
  // Deque size is expected to be (expected_intervals + 1), with the first value being
  // the baseline and all other values representing an interval.
  std::deque<KeyStatistics> key_stats_window_;

  // Number of intervals stored in the window deques, calculated by the
  // (window size in seconds / the interval size in seconds), rounded up.
  uint32_t expected_intervals_;

  // The last full compaction time of the tablet when the window was last reset.
  uint64_t last_compaction_time_;

  // The instance count of the previously-seen TabletMetrics instance.
  // This allows the KeyStatsSlidingWindow to verify that the metrics instance it is pulling
  // stats from is the same one as last time (resetting the window if not).
  uint64_t metrics_instance_id_;
};

class FullCompactionManager {
 public:
  explicit FullCompactionManager(TSTabletManager* ts_tablet_manager);

  // Checks if the gflag values for the compaction frequency and jitter factor have changed
  // since the last runs, and resets to those values if so. Then, runs DoScheduleFullCompactions().
  void ScheduleFullCompactions();

  Status Init();

  void Shutdown();

  // Checks whether the tablet peer should be fully compacted based on its recent
  // docdb key access statistics.
  bool ShouldCompactBasedOnStats(const TabletId& tablet_id);

  MonoDelta compaction_frequency() const { return compaction_frequency_; }

  MonoDelta max_jitter() const { return max_jitter_; }

  // Indicates the number of full compactions that were scheduled during the last execution of
  // DoScheduleFullCompactions().
  int num_scheduled_last_execution() const { return num_scheduled_last_execution_.load(); }

  // Provides public access to DetermineNextCompactTime() for tests.
  // Clears all precomputed next compaction times.
  HybridTime TEST_DetermineNextCompactTime(tablet::TabletPeerPtr peer, HybridTime now) {
    HybridTime compact_time = DetermineNextCompactTime(peer, now);
    next_compact_time_per_tablet_.clear();
    return compact_time;
  }

  // Allows compaction frequencies with a lower time granularity than an hour (e.g. seconds).
  void TEST_DoScheduleFullCompactionsWithManualValues(
      MonoDelta compaction_frequency, int jitter_factor);

  // Allows check interval to be manually set without enabling the background task.
  // Used to set intervals for statistics windows.
  void TEST_SetCheckIntervalSec(int32_t check_interval_sec) {
    check_interval_sec_ = check_interval_sec;
  }

  KeyStatistics TEST_CurrentWindowStats(const TabletId tablet_id) {
    auto window_iter = tablet_stats_window_.find(tablet_id);
    // If we don't have any stats collected, then return 0 for all statistics.
    if (window_iter == tablet_stats_window_.end()) {
      return KeyStatistics{ 0, 0 };
    }

    return window_iter->second.current_stats();
  }

  // Checks whether the key is in tablet_stats_window_.
  bool TEST_TabletIdInStatsWindowMap(const TabletId& tablet_id) const {
    return tablet_stats_window_.find(tablet_id) != tablet_stats_window_.end();
  }

 private:
  FRIEND_TEST(TsTabletManagerTest, FullCompactionCalculateNextCompaction);
  FRIEND_TEST(TsTabletManagerTest, CompactionsEvenlySpreadByJitter);

  // Iterates through all tablets owned by the tablet manager, scheduling full compactions
  // on any tablets that are eligible for full compaction. When a new compaction is scheduled,
  // the tablet is removed from the in-memory map of next compaction times
  // (next_compact_time_per_tablet_).
  void DoScheduleFullCompactions(const std::vector<tablet::TabletPeerPtr>& peers);

  // Collects docdb key access statistics from all tablet peers, creating and storing a
  // sliding window of stats.
  void CollectDocDBStats(const std::vector<tablet::TabletPeerPtr>& peers);

  // Removes obsolete entries (i.e. entries that no longer exist in the TsTabletManager)
  // from in-memory record-keeping maps, specifically next_compact_time_per_tablet_
  // and tablet_stats_window_.
  void CleanupIfNecessary(const std::vector<tablet::TabletPeerPtr>& peers);

  // Checks whether the tablet peer has been compacted too recently to be fully compacted
  // again (based on the auto_compact_min_wait_between_seconds flag).
  bool CompactedTooRecently(const tablet::TabletPeerPtr& peer, const HybridTime& now);

  // Iterates through all peers, determining the next compaction time for each peer
  // eligible for scheduled full compactions. Returns a list of peers that are currently
  // ready for compaction, ordered by how recently they were last compacted (oldest first).
  PeerNextCompactList GetPeersEligibleForCompaction(
      const std::vector<tablet::TabletPeerPtr>& peers);

  // Returns the next compaction time for a given tablet peer. Next compaction time will
  // be from the in-memory map of next compaction times (next_compact_time_per_tablet_)
  // if able. If one is not available, then a new next compaction time will be calculated
  // and stored in the in-memory map.
  HybridTime DetermineNextCompactTime(const tablet::TabletPeerPtr& peer, HybridTime now);

  // Calculates the next compaction time based on the last compaction time and jitter.
  // If the tablet has no last compaction time, then a compaction will be scheduled
  // soon as a function of (now + jitter). Otherwise, the next compaction time will
  // be (the previous compaction time + compaction frequency - jitter).
  HybridTime CalculateNextCompactTime(
      const TabletId& tablet_id,
      const HybridTime now,
      const HybridTime last_compact_time,
      const MonoDelta jitter) const;

  // Calculates jitter determistically as a function of tablet id and last compaction time.
  MonoDelta CalculateJitter(
      const TabletId& tablet_id,
      const uint64_t last_compact_time) const;

  // Reads gflags scheduled_full_compaction_frequency_hours and
  // scheduled_full_compaction_jitter_factor_percentage, and resets the
  // FullCompactionManager with those values if they have changed.
  void SetFrequencyAndJitterFromFlags();

  // Checks if compaction frequency and jitter factor match their current values, and
  // resets the FullCompactionManager with the new values if they have changed.
  void ResetFrequencyAndJitterIfNeeded(
      MonoDelta compaction_frequency, int jitter_factor);

  // Tablet manager that owns this full compaction manager.
  TSTabletManager* ts_tablet_manager_;

  // Amount of time expected between full compactions.
  MonoDelta compaction_frequency_;

  // Stored jitter factor (i.e. percentage of compaction frequency to be used as max jitter).
  int32_t jitter_factor_;

  // Maximum amount of jitter that modifies the expected compaction time.
  MonoDelta max_jitter_;

  // Frequency with which to check for compactions to schedule, in seconds.
  int32_t check_interval_sec_;

  // In-memory map of pre-calculated next compaction times per tablet.
  std::unordered_map<TabletId, HybridTime> next_compact_time_per_tablet_;

  // Sliding windows that keep track of docdb key read statistics per tablet.
  std::unordered_map<TabletId, KeyStatsSlidingWindow> tablet_stats_window_;

  // Number of compactions that were scheduled during the previous execution.
  // -1 indicates that there is no information about the previous execution.
  std::atomic<int> num_scheduled_last_execution_ = -1;

  // Background task for scheduling major compactions, called every check_interval_sec_.
  std::unique_ptr<BackgroundTask> bg_task_;

  // Indicates the time of the most recent cleanup.
  CoarseTimePoint last_cleanup_time_;
};

} // namespace tserver
} // namespace yb
