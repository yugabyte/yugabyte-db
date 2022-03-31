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

#include <unordered_set>

#include "yb/master/master_fwd.h"
#include "yb/master/tablet_split_candidate_filter.h"
#include "yb/master/tablet_split_complete_handler.h"
#include "yb/master/tablet_split_driver.h"

namespace yb {
namespace master {

using std::unordered_set;

class TabletSplitManager : public TabletSplitCompleteHandlerIf {
 public:
  TabletSplitManager(TabletSplitCandidateFilterIf* filter,
                     TabletSplitDriverIf* driver,
                     CDCConsumerSplitDriverIf* cdc_consumer_split_driver);

  // Perform one round of tablet splitting. This method is not thread-safe.
  void MaybeDoSplitting(const TableInfoMap& table_info_map);

  void ProcessSplitTabletResult(const Status& status,
                                const TableId& consumer_table_id,
                                const SplitTabletIds& split_tablet_ids);

  CHECKED_STATUS ValidateSplitCandidateTable(const TableInfo& table);

  CHECKED_STATUS ValidateSplitCandidateTablet(const TabletInfo& tablet);

  void MarkTtlTableForSplitIgnore(const TableId& table_id);

  void MarkSmallKeyRangeTabletForSplitIgnore(const TabletId& tablet_id);

 private:
  void ScheduleSplits(const unordered_set<TabletId>& splits_to_schedule);

  void DoSplitting(const TableInfoMap& table_info_map);

  Status ValidateAgainstDisabledList(const std::string& id,
                                     std::unordered_map<std::string, CoarseTimePoint>* map);

  TabletSplitCandidateFilterIf* filter_;
  TabletSplitDriverIf* driver_;
  CDCConsumerSplitDriverIf* cdc_consumer_split_driver_;

  CoarseTimePoint last_run_time_;

  std::mutex mutex_;
  std::unordered_map<TableId, CoarseTimePoint> ignore_table_for_splitting_until_ GUARDED_BY(mutex_);
  std::unordered_map<TabletId, CoarseTimePoint> ignore_tablet_for_splitting_until_
      GUARDED_BY(mutex_);

};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_TABLET_SPLIT_MANAGER_H
