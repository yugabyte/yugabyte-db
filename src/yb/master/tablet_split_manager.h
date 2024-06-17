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

#pragma once

#include <unordered_set>

#include "yb/master/leader_epoch.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_context.h"

#include "yb/util/metrics.h"

namespace yb {
namespace master {

YB_STRONGLY_TYPED_BOOL(IgnoreTtlValidation);
YB_STRONGLY_TYPED_BOOL(IgnoreDisabledList);

Status CheckLiveReplicasForSplit(
    const TabletId& tablet_id, const TabletReplicaMap& replicas, size_t rf);

using SplitsToScheduleMap = std::unordered_map<TabletId, std::optional<uint64_t>>;

class TabletSplitManager {
 public:
  TabletSplitManager(
      CatalogManagerIf& catalog_manager,
      const scoped_refptr<MetricEntity>& master_metrics,
      const scoped_refptr<MetricEntity>& cluster_metrics);

  Status WaitUntilIdle(CoarseTimePoint deadline = CoarseTimePoint::max());

  // Returns true if there are no outstanding tablet splits, and the automatic splitting thread is
  // not running (to ensure that no splits are started immediately after returning).
  // This function should eventually return true if called repeatedly after temporarily disabling
  // splitting for the table.
  bool IsTabletSplittingComplete(
      const TableInfo& table, bool wait_for_parent_deletion, CoarseTimePoint deadline);

  // Perform one round of tablet splitting. This method is not thread-safe.
  void MaybeDoSplitting(
      const std::vector<TableInfoPtr>& tables,
      const TabletInfoMap& tablet_info_map,
      const LeaderEpoch& epoch);

  Status ProcessSplitTabletResult(
      const TableId& split_table_id, const SplitTabletIds& split_tablet_ids);

  // Table-level checks for splitting that are checked not only as a best-effort
  // filter, but also after acquiring the table/tablet locks in CatalogManager::DoSplit.
  Status ValidateSplitCandidateTable(
      const TableInfoPtr& table,
      IgnoreDisabledList ignore_disabled_list = IgnoreDisabledList::kFalse);

  // Tablet-level checks for splitting that are checked not only as a best-effort
  // filter, but also after acquiring the table/tablet locks in CatalogManager::DoSplit.
  Status ValidateSplitCandidateTablet(
      const TabletInfo& tablet,
      const TabletInfoPtr parent,
      IgnoreTtlValidation ignore_ttl_validation = IgnoreTtlValidation::kFalse,
      IgnoreDisabledList ignore_disabled_list = IgnoreDisabledList::kFalse);

  // Disables tablet splitting for a specified amount of time.
  Status DisableTabletSplitting(
      const DisableTabletSplittingRequestPB* req, DisableTabletSplittingResponsePB* resp,
      rpc::RpcContext* rpc);

  // Returns true if there are no outstanding tablets and the tablet split manager is not currently
  // processing tablet splits.
  Status IsTabletSplittingComplete(
      const IsTabletSplittingCompleteRequestPB* req, IsTabletSplittingCompleteResponsePB* resp,
      rpc::RpcContext* rpc);

  bool IsTabletSplittingComplete(bool wait_for_parent_deletion, CoarseTimePoint deadline);

  // Temporarily disable splitting for the specified amount of time.
  void DisableSplittingFor(const MonoDelta& disable_duration, const std::string& feature_name);
  // Re-enable splitting after a call to DisableSplittingFor.
  void ReenableSplittingFor(const std::string& feature_name);

  // Disables splitting for a table with TTL file expiry.
  void DisableSplittingForTtlTable(const TableId& table_id);

  // Disables splitting for a backfilling table.
  void DisableSplittingForBackfillingTable(const TableId& table_id);

  // Re-enables splitting for a backfilling table (after success or failure).
  void ReenableSplittingForBackfillingTable(const TableId& table_id);

  // Disables splitting for tablets that are too small to split.
  void DisableSplittingForSmallKeyRangeTablet(const TabletId& tablet_id);

  Status PrepareForPitr(const CoarseTimePoint& deadline);

 private:
  void ScheduleSplits(const SplitsToScheduleMap& splits_to_schedule, const LeaderEpoch& epoch);

  void DoSplitting(
      const std::vector<TableInfoPtr>& tables,
      const TabletInfoMap& tablet_info_map,
      const LeaderEpoch& epoch);

  Status ValidateTableAgainstDisabledLists(const TableId& table_id);
  Status ValidateTabletAgainstDisabledList(const TabletId& tablet_id);
  Status ValidatePartitioningVersion(const TableInfo& table);

  CatalogManagerIf& catalog_manager_;

  // This mutex is acquired in each tablet splitting manager run.
  std::shared_timed_mutex is_running_mutex_;

  CoarseTimePoint last_run_time_;

  scoped_refptr<yb::AtomicGauge<uint64_t>> automatic_split_manager_time_ms_;
  scoped_refptr<Counter> metric_split_tablet_too_many_tablets_;

  template <typename IdType>
  using DisabledSet = std::unordered_map<IdType, CoarseTimePoint>;

  std::mutex disabled_sets_mutex_;
  // Whether tablet-splitting is disabled (cluster-wide), keyed by the feature name (e.g. PITR).
  // This prevents features from accidentally overwriting each others' disable timeouts.
  DisabledSet<std::string> splitting_disabled_until_ GUARDED_BY(disabled_sets_mutex_);
  DisabledSet<TableId> disable_splitting_for_ttl_table_until_ GUARDED_BY(disabled_sets_mutex_);
  DisabledSet<TableId> disable_splitting_for_backfilling_table_until_
      GUARDED_BY(disabled_sets_mutex_);
  DisabledSet<TabletId> disable_splitting_for_small_key_range_tablet_until_
      GUARDED_BY(disabled_sets_mutex_);
};

}  // namespace master
}  // namespace yb
