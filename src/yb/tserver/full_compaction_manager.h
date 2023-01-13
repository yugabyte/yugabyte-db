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
namespace tserver {

typedef std::multimap<HybridTime, tablet::TabletPeerPtr> PeerNextCompactList;

class FullCompactionManager {
 public:
  explicit FullCompactionManager(TSTabletManager* ts_tablet_manager);

  // Checks if the gflag values for the compaction frequency and jitter factor have changed
  // since the last runs, and resets to those values if so. Then, runs DoScheduleFullCompactions().
  void ScheduleFullCompactions();

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

  // Compaction frequency and max jitter should only be set by the constructor or reset for
  // testing purposes.
  void TEST_DoScheduleFullCompactionsWithManualValues(
      MonoDelta compaction_frequency, int jitter_factor) {
    ResetFrequencyAndJitterIfNeeded(compaction_frequency, jitter_factor);
    DoScheduleFullCompactions();
  }

 private:
  FRIEND_TEST(TsTabletManagerTest, FullCompactionCalculateNextCompaction);
  FRIEND_TEST(TsTabletManagerTest, CompactionsEvenlySpreadByJitter);

  // Iterates through all tablets owned by the tablet manager, scheduling full compactions
  // on any tablets that are eligible for full compaction. When a new compaction is scheduled,
  // the tablet is removed from the in-memory map of next compaction times
  // (next_compact_time_per_tablet_).
  void DoScheduleFullCompactions();

  // Iterates through all peers, determining the next compaction time for each peer
  // eligible for scheduled full compactions. Returns a list of peers that are currently
  // ready for compaction, ordered by how recently they were last compacted (oldest first).
  PeerNextCompactList GetPeersEligibleForCompaction();

  // Returns the next compaction time for a given tablet peer. Next compaction time will
  // be from the in-memory map of next compaction times (next_compact_time_per_tablet_)
  // if able. If one is not available, then a new next compaction time will be calculated
  // and stored in the in-memory map.
  HybridTime DetermineNextCompactTime(tablet::TabletPeerPtr peer, HybridTime now);

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

  // In-memory map of pre-calculated next compaction times per tablet.
  std::unordered_map<TabletId, HybridTime> next_compact_time_per_tablet_;

  // Number of compactions that were scheduled during the previous execution.
  // -1 indicates that there is no information about the previous execution.
  std::atomic<int> num_scheduled_last_execution_ = -1;
};

} // namespace tserver
} // namespace yb
