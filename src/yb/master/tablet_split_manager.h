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

#ifndef YB_MASTER_TABLET_SPLIT_MANAGER_H
#define YB_MASTER_TABLET_SPLIT_MANAGER_H

#include <deque>
#include <mutex>

#include "yb/common/entity_ids.h"
#include "yb/gutil/thread_annotations.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/tablet_split_candidate_filter.h"
#include "yb/master/tablet_split_driver.h"
#include "yb/master/ts_manager.h"
#include "yb/util/background_task.h"
#include "yb/util/threadpool.h"

namespace yb {
namespace master {


class TabletSplitManager {
 public:
  TabletSplitManager(TabletSplitCandidateFilterIf* filter, TabletSplitDriverIf* driver);

  CHECKED_STATUS ScheduleSplitIfNeeded(
      const TabletInfo& tablet_info, const TabletServerId& drive_info_ts_uuid,
      const TabletReplicaDriveInfo& drive_info);

  CHECKED_STATUS Init();

  void Shutdown();

 private:
  void ProcessQueuedSplitItems();

  TabletSplitCandidateFilterIf* filter_;
  TabletSplitDriverIf* driver_;

  std::mutex mutex_;
  std::deque<TabletId> candidates_ GUARDED_BY(mutex_);
  std::unique_ptr<BackgroundTask> process_tablet_candidates_task_;
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_TABLET_SPLIT_MANAGER_H
