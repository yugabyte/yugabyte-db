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

#include "yb/master/tablet_split_manager.h"

#include <chrono>

#include <gflags/gflags.h>

#include "yb/gutil/port.h"
#include "yb/util/atomic.h"
#include "yb/util/flag_tags.h"
#include "yb/util/format.h"
#include "yb/util/unique_lock.h"

DEFINE_int32(process_split_tablet_candidates_interval_msec, 15000,
             "The tick interval time for processing accumulated tablet split candidates.");
DEFINE_int32(max_queued_split_candidates, 5,
             "The max number of pending tablet split candidates we will hold onto. We potentially "
             "iterate through every candidate in the queue for each tablet we process in a tablet "
             "report so this size should be kept relatively small to avoid any issues.");

DEFINE_bool(enable_automatic_tablet_splitting, false,
            "If false, disables automatic tablet splitting driven from the yb-master side.");

DEFINE_test_flag(bool, disable_split_tablet_candidate_processing, false,
                 "When true, do not process split tablet candidates.");

DEFINE_int32(outstanding_tablet_split_limit, 5,
             "Limit of the number of outstanding tablet splits. Limitation is "
             "disabled if this value is set to 0.");

constexpr int32 kHardLimitCandidateQueueSize = 100;

namespace yb {
namespace master {

namespace {

int32 GetCandidateQueueLimit() {
  return std::min(kHardLimitCandidateQueueSize, FLAGS_max_queued_split_candidates);
}

} // namespace

TabletSplitManager::TabletSplitManager(
    TabletSplitCandidateFilterIf* filter, TabletSplitDriverIf* driver):
    filter_(filter),
    driver_(driver) {}

Status TabletSplitManager::Init() {
  process_tablet_candidates_task_.reset(new BackgroundTask(
    std::function<void()>([this]() { ProcessQueuedSplitItems(); }),
    "tablet split manager",
    "process queued tablet split candidates",
    std::chrono::milliseconds(FLAGS_process_split_tablet_candidates_interval_msec)));
  return process_tablet_candidates_task_->Init();
}

void TabletSplitManager::Shutdown() {
  if (process_tablet_candidates_task_) {
    process_tablet_candidates_task_->Shutdown();
  }
}

void TabletSplitManager::RemoveFailedProcessingTabletSplit(const TabletId& tablet_id) {
  UniqueLock<decltype(mutex_)> lock(mutex_);
  processing_tablets_to_split_children_.erase(tablet_id);
}

Status TabletSplitManager::ProcessLiveTablet(
    const TabletInfo& tablet_info,
    const TabletServerId& drive_info_ts_uuid,
    const TabletReplicaDriveInfo& drive_info) {
  if (!FLAGS_enable_automatic_tablet_splitting) {
    return Status::OK();
  }

  UniqueLock<decltype(mutex_)> lock(mutex_);

  RemoveParentProcessingTabletIfChildIsDoneSplitting(tablet_info, drive_info);

  return ScheduleSplitIfNeeded(tablet_info, drive_info_ts_uuid, drive_info);
}

void TabletSplitManager::ProcessQueuedSplitItems() {
  if (PREDICT_FALSE(FLAGS_TEST_disable_split_tablet_candidate_processing)) {
    return;
  }
  UniqueLock<decltype(mutex_)> lock(mutex_);
  if (!candidates_.empty()) {
    // Check if we're already processing the max number of tablet splits.
    if (PREDICT_TRUE(FLAGS_outstanding_tablet_split_limit > 0) &&
        processing_tablets_to_split_children_.size() >= FLAGS_outstanding_tablet_split_limit) {
      return;
    }
    auto tablet_id = candidates_.front();
    auto s = driver_->SplitTablet(tablet_id);
    WARN_NOT_OK(s, Format("Failed to trigger split for tablet_id: $0.", tablet_id));
    candidates_.pop_front();

    if (s.ok()) {
      processing_tablets_to_split_children_.insert({tablet_id, ""});
    }
  }
}

void TabletSplitManager::RemoveParentProcessingTabletIfChildIsDoneSplitting(
    const TabletInfo& tablet_info, const TabletReplicaDriveInfo& drive_info) {
  auto l_tablet = tablet_info.LockForRead();
  if (l_tablet->pb.has_split_parent_tablet_id() && !drive_info.may_have_orphaned_post_split_data) {
    // This is a child tablet that has been split and is done compacting.
    // Need to check if the other child has also finished compacting.
    auto entry = processing_tablets_to_split_children_.find(l_tablet->pb.split_parent_tablet_id());

    // If entry is not found, then the parent has already finished being processed.
    if (entry != processing_tablets_to_split_children_.end()) {
      if (entry->second.empty()) {
        // This is the first child to finish, save its tablet_id and wait for the other child.
        entry->second = tablet_info.tablet_id();
      } else if (entry->second != tablet_info.tablet_id()) {
        // Both children have finished, the parent is now done processing, so remove the entry.
        processing_tablets_to_split_children_.erase(entry);
      }
    }
  }
}

Status TabletSplitManager::ScheduleSplitIfNeeded(
    const TabletInfo& tablet_info,
    const TabletServerId& drive_info_ts_uuid,
    const TabletReplicaDriveInfo& drive_info) {
  if (candidates_.size() >= GetCandidateQueueLimit()) {
    return Status::OK();
  }

  auto tablet_id = tablet_info.tablet_id();
  if (std::find(candidates_.begin(), candidates_.end(), tablet_id) != candidates_.end()) {
    return Status::OK();
  }
  auto is_tablet_leader_drive_info = (
      VERIFY_RESULT(tablet_info.GetLeader())->permanent_uuid() == drive_info_ts_uuid);
  if (is_tablet_leader_drive_info
      && filter_->ValidateSplitCandidate(tablet_info).ok()
      && filter_->ShouldSplitValidCandidate(tablet_info, drive_info)) {
    LOG(INFO) << "Adding tablet into split queue: " << tablet_id;
    candidates_.push_back(tablet_id);
  }
  return Status::OK();
}

}  // namespace master
}  // namespace yb
