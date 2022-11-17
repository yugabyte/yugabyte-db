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
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"

namespace {

constexpr int32_t kDefaultJitterFactorPercentage = 33;
// Indicates the maximum size for an abbreviated hash used for jitter.
constexpr uint64_t kMaxSmallHash = 1000000000;

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

namespace yb {
namespace tserver {

using tablet::TabletPeerPtr;

FullCompactionManager::FullCompactionManager(TSTabletManager* ts_tablet_manager)
    : ts_tablet_manager_(ts_tablet_manager) {
  SetFrequencyAndJitterFromFlags();
}

void FullCompactionManager::ScheduleFullCompactions() {
  SetFrequencyAndJitterFromFlags();
  DoScheduleFullCompactions();
}

void FullCompactionManager::DoScheduleFullCompactions() {
  // If compaction_frequency_ is 0, feature is disabled.
  if (compaction_frequency_ == MonoDelta::kZero) {
    num_scheduled_last_execution_.store(0);
    return;
  }

  int num_scheduled = 0;
  PeerNextCompactList peers_to_compact = GetPeersEligibleForCompaction();

  for (auto itr = peers_to_compact.begin(); itr != peers_to_compact.end(); itr++) {
    const auto peer = itr->second;
    const auto tablet = peer->shared_tablet();
    if (!tablet) {
      LOG(WARNING) << "Unable to schedule full compaction on tablet " << peer->tablet_id()
          << ": tablet not found.";
      continue;
    }
    Status s = tablet->TriggerFullCompactionIfNeeded(
        tablet::FullCompactionReason::Scheduled);
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

PeerNextCompactList FullCompactionManager::GetPeersEligibleForCompaction() {
  const auto now = ts_tablet_manager_->server()->Clock()->Now();
  PeerNextCompactList compact_list;
  for (auto& peer : ts_tablet_manager_->GetTabletPeers()) {
    const auto tablet_id = peer->tablet_id();
    const auto tablet = peer->shared_tablet();
    // If the tablet isn't eligible for compaction, remove it from our stored compaction
    // times and skip it.
    if (!tablet || !tablet->IsEligibleForFullCompaction()) {
      next_compact_time_per_tablet_.erase(tablet_id);
      continue;
    }

    // If the next compaction time is pre-calculated, use that. Otherwise, calculate
    // a new one.
    const HybridTime next_compact_time = DetermineNextCompactTime(peer, now);

    // If the tablet is ready to compact, then add it to the list.
    if (next_compact_time <= now) {
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

HybridTime FullCompactionManager::DetermineNextCompactTime(TabletPeerPtr peer, HybridTime now) {
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

} // namespace tserver
} // namespace yb
