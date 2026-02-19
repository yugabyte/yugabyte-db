// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/master/catalog_manager_bg_tasks.h"

#include <memory>

#include "yb/master/catalog_manager_util.h"
#include "yb/master/cdcsdk_manager.h"
#include "yb/master/clone/clone_state_manager.h"
#include "yb/master/cluster_balance.h"
#include "yb/master/master.h"
#include "yb/master/tablet_split_manager.h"
#include "yb/master/ts_manager.h"
#include "yb/master/xcluster/xcluster_manager_if.h"
#include "yb/master/ysql/ysql_manager.h"
#include "yb/master/ysql_backends_manager.h"

#include "yb/master/scoped_leader_shared_lock.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/debug/long_operation_tracker.h"
#include "yb/util/debug-util.h"
#include "yb/util/monotime.h"
#include "yb/util/mutex.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"

using namespace std::literals;

using std::shared_ptr;
using std::vector;

METRIC_DEFINE_event_stats(
    server, load_balancer_duration, "Cluster balancer duration",
    yb::MetricUnit::kMilliseconds, "Duration of one cluster balancer run (in milliseconds)");

DEFINE_RUNTIME_int32(catalog_manager_bg_task_wait_ms, 1000,
    "Amount of time the catalog manager background task thread waits between runs");

DEFINE_RUNTIME_int32(load_balancer_initial_delay_secs, yb::master::kDelayAfterFailoverSecs,
             "Amount of time to wait between becoming master leader and enabling the load "
             "balancer.");

DEFINE_RUNTIME_bool(sys_catalog_respect_affinity_task, true,
            "Whether the master sys catalog tablet respects cluster config preferred zones "
            "and sends step down requests to a preferred leader.");

DEFINE_RUNTIME_int32(master_bg_task_long_operation_warning_ms, 5 * 1000,
    "Log warnings if the catalog manager background tasks (combined) take longer than this amount "
    "of time.");

DEFINE_test_flag(bool, pause_catalog_manager_bg_loop_start, false,
                 "Pause the bg tasks thread at the beginning of the loop.");

DEFINE_test_flag(bool, pause_catalog_manager_bg_loop_end, false,
                 "Pause the bg tasks thread at the end of the loop.");

DECLARE_bool(enable_ysql);
DECLARE_bool(TEST_echo_service_enabled);

namespace yb::master {

CatalogManagerBgTasks::CatalogManagerBgTasks(Master* master)
    : closing_(false),
      pending_updates_(false),
      cond_(&lock_),
      thread_(nullptr),
      master_(master),
      catalog_manager_(master->catalog_manager_impl()),
      cluster_balancer_duration_(METRIC_load_balancer_duration.Instantiate(
          master_->metric_entity())) {
}

void CatalogManagerBgTasks::Wake() {
  MutexLock lock(lock_);
  pending_updates_ = true;
  YB_PROFILE(cond_.Broadcast());
}

void CatalogManagerBgTasks::Wait(int msec) {
  MutexLock lock(lock_);
  if (closing_.load()) return;
  if (!pending_updates_) {
    cond_.TimedWait(MonoDelta::FromMilliseconds(msec));
  }
  pending_updates_ = false;
}

void CatalogManagerBgTasks::WakeIfHasPendingUpdates() {
  MutexLock lock(lock_);
  if (pending_updates_) {
    YB_PROFILE(cond_.Broadcast());
  }
}

Status CatalogManagerBgTasks::Init() {
  RETURN_NOT_OK(yb::Thread::Create("catalog manager", "bgtasks",
      &CatalogManagerBgTasks::Run, this, &thread_));
  return Status::OK();
}

void CatalogManagerBgTasks::Shutdown() {
  {
    bool closing_expected = false;
    if (!closing_.compare_exchange_strong(closing_expected, true)) {
      VLOG(2) << "CatalogManagerBgTasks already shut down";
      return;
    }
  }

  Wake();
  if (thread_ != nullptr) {
    CHECK_OK(ThreadJoiner(thread_.get()).Join());
  }
}

void CatalogManagerBgTasks::TryResumeBackfillForTables(
    const LeaderEpoch& epoch, std::unordered_set<TableId>* tables) {
  for (auto it = tables->begin(); it != tables->end(); it = tables->erase(it)) {
    const auto& table_info_result = catalog_manager_->FindTableById(*it);
    if (!table_info_result.ok()) {
      LOG(WARNING) << "Table Info not found for id " << *it;
      continue;
    }
    const auto& table_info = *table_info_result;
    // Get schema version.
    uint32_t version = table_info->LockForRead()->pb.version();
    const auto tablets_result = table_info->GetTablets();
    if (!tablets_result) {
      LOG(WARNING) << Format(
          "PITR: Cannot resume backfill for table, backing TabletInfo objects have been freed. "
          "Table id: $0",
          table_info->id());
      return;
    }
    for (const auto& tablet : *tablets_result) {
      LOG(INFO) << "PITR: Try resuming backfill for tablet " << tablet->id()
                << ". If it is not a table for which backfill needs to be resumed"
                << " then this is a NO-OP";
      auto s = catalog_manager_->HandleTabletSchemaVersionReport(
          tablet.get(), version, epoch, table_info);
      // If schema version changed since PITR restore then backfill should restart
      // by virtue of that particular alter if needed.
      WARN_NOT_OK(s, Format("PITR: Resume backfill failed for tablet ", tablet->id()));
    }
  }
}

void CatalogManagerBgTasks::ClearDeadTServerMetrics() const {
  auto descs = master_->ts_manager()->GetAllDescriptors();
  for (auto& ts_desc : descs) {
    if (!ts_desc->IsLive()) {
      ts_desc->ClearMetrics();
    }
  }
}

void CatalogManagerBgTasks::RunOnceAsLeader(const LeaderEpoch& epoch) {
  LongOperationTracker long_operation_tracker(
      "CatalogManagerBgTasks::RunOnceAsLeader",
      FLAGS_master_bg_task_long_operation_warning_ms * 1ms);

  ClearDeadTServerMetrics();

  if (FLAGS_TEST_echo_service_enabled) {
    WARN_NOT_OK(
        catalog_manager_->CreateTestEchoService(epoch), "Failed to create Test Echo service");
  }

  catalog_manager_->ysql_manager_->RunBgTasks(epoch);

  // Report metrics.
  catalog_manager_->ReportMetrics();

  // Cleanup old tasks from tracker.
  catalog_manager_->tasks_tracker_->CleanupOldTasks();

  // Mark unresponsive tservers.
  WARN_NOT_OK(
      catalog_manager_->master_->ts_manager()->MarkUnresponsiveTServers(epoch),
      "Failed to update sys catalog with unresponsive tservers");

  TabletInfos to_delete;
  TableToTabletInfos to_process;

  // Get list of tablets not yet running or already replaced.
  catalog_manager_->ExtractTabletsToProcess(&to_delete, &to_process);

  bool processed_tablets = false;
  if (!to_process.empty()) {
    // For those tablets which need to be created in this round, assign replicas.
    TSDescriptorVector ts_descs = catalog_manager_->GetAllLiveNotBlacklistedTServers();
    auto global_load_state_result = catalog_manager_->InitializeGlobalLoadState(ts_descs);
    if (global_load_state_result.ok()) {
      auto global_load_state = *global_load_state_result;
      // Transition tablet assignment state from preparing to creating, send
      // and schedule creation / deletion RPC messages, etc.
      // This is done table by table.
      for (const auto& [table_id, tablets] : to_process) {
        LOG(INFO) << "Processing pending assignments for table: " << table_id;
        Status s = catalog_manager_->ProcessPendingAssignmentsPerTable(
            table_id, tablets, epoch, &global_load_state);
        WARN_NOT_OK(s, "Assignment failed");
        // Set processed_tablets as true if the call succeeds for at least one table.
        processed_tablets = processed_tablets || s.ok();
        // TODO Add tests for this in the revision that makes
        // create/alter fault tolerant.
      }
    }
  }

  // Trigger pending backfills.
  std::unordered_set<TableId> table_map;
  {
    std::lock_guard lock(catalog_manager_->backfill_mutex_);
    table_map.swap(catalog_manager_->pending_backfill_tables_);
  }
  TryResumeBackfillForTables(epoch, &table_map);

  std::vector<TableInfoPtr> tables;
  TabletInfoMap tablet_info_map;
  {
    CatalogManager::SharedLock lock(catalog_manager_->mutex_);
    auto tables_it = catalog_manager_->tables_->GetPrimaryTables();
    tables = std::vector(std::begin(tables_it), std::end(tables_it));
    tablet_info_map = *catalog_manager_->tablet_map_;
  }
  if (!processed_tablets) {
    MaybeRunClusterBalancer(epoch, tables, tablet_info_map);
  }
  master_->tablet_split_manager().MaybeDoSplitting(tables, tablet_info_map, epoch);

  WARN_NOT_OK(master_->clone_state_manager().Run(), "Failed to run CloneStateManager: ");

  if (!to_delete.empty() || catalog_manager_->AreTablesDeletingOrHiding()) {
    catalog_manager_->CleanUpDeletedTables(epoch);
  }

  // Ensure the master sys catalog tablet follows the cluster's affinity specification.
  if (FLAGS_sys_catalog_respect_affinity_task) {
    Status s = catalog_manager_->SysCatalogRespectLeaderAffinity();
    if (!s.ok()) {
      YB_LOG_EVERY_N(INFO, 10) << s.message().ToBuffer();
    }
  }

  // Set the universe_uuid field in the cluster config if not already set.
  WARN_NOT_OK(
      catalog_manager_->SetUniverseUuidIfNeeded(epoch), "Failed SetUniverseUuidIfNeeded Task");

  // Run background tasks related to XCluster & CDC Schema.
  catalog_manager_->RunXReplBgTasks(epoch);

  catalog_manager_->cdcsdk_manager_->RunBgTasks(epoch);

  catalog_manager_->GetXClusterManager()->RunBgTasks(epoch);

  // Abort inactive YSQL BackendsCatalogVersionJob jobs.
  master_->ysql_backends_manager()->AbortInactiveJobs();
}

void CatalogManagerBgTasks::MaybeRunClusterBalancer(
    const LeaderEpoch& epoch, const std::vector<TableInfoPtr>& tables,
    const TabletInfoMap& tablets) {
  if (catalog_manager_->TimeSinceElectedLeader() <=
      MonoDelta::FromSeconds(FLAGS_load_balancer_initial_delay_secs)) {
    return;
  }
  auto start = CoarseMonoClock::Now();
  catalog_manager_->load_balance_policy_->RunClusterBalancer(epoch, tables, tablets);
  cluster_balancer_duration_->Increment(ToMilliseconds(CoarseMonoClock::now() - start));
}

void CatalogManagerBgTasks::Run() {
  while (!closing_.load()) {
    TEST_PAUSE_IF_FLAG(TEST_pause_catalog_manager_bg_loop_start);
    SCOPED_LEADER_SHARED_LOCK(l, catalog_manager_);
    if (!l.catalog_status().ok()) {
      LOG(WARNING) << "Catalog manager background task thread going to sleep: "
                   << l.catalog_status().ToString();
    } else if (l.leader_status().ok()) {
      RunOnceAsLeader(l.epoch());
      was_leader_ = true;
    } else {
      // leader_status is not ok.
      if (was_leader_) {
        LOG(INFO) << "Begin one-time cleanup on losing leadership";
        cluster_balancer_duration_->Reset();
        catalog_manager_->ResetMetrics();
        catalog_manager_->ResetTasksTrackers();
        master_->ysql_backends_manager()->AbortAllJobs();
        was_leader_ = false;
      }
    }
    // Wait for a notification or a timeout expiration.
    //  - CreateTable will call Wake() to notify about the tablets to add
    //  - HandleReportedTablet/ProcessPendingAssignments will call WakeIfHasPendingUpdates()
    //    to notify about tablets creation.
    //  - DeleteTable will call Wake() to finish destructing any table internals
    l.Unlock();
    TEST_PAUSE_IF_FLAG(TEST_pause_catalog_manager_bg_loop_end);
    Wait(FLAGS_catalog_manager_bg_task_wait_ms);
  }
  VLOG(1) << "Catalog manager background task thread shutting down";
}

} // namespace yb::master
