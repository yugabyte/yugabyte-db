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

#pragma once

#include <atomic>
#include <unordered_set>

#include "yb/common/entity_ids_types.h"
#include "yb/gutil/ref_counted.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_fwd.h"
#include "yb/util/condition_variable.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/mutex.h"
#include "yb/util/status_fwd.h"

namespace yb {

class Thread;

namespace master {

class CatalogManager;

int32_t TEST_transaction_status_check_run_count();

class CatalogManagerBgTasks final {
 public:
  explicit CatalogManagerBgTasks(Master* master);

  ~CatalogManagerBgTasks() {}

  Status Init();
  void Shutdown();

  void Wake();
  void Wait(int msec);
  void WakeIfHasPendingUpdates();

 private:
  void TryResumeBackfillForTables(const LeaderEpoch& epoch, std::unordered_set<TableId>* tables);
  void ClearDeadTServerMetrics() const;
  void Run();
  void RunOnceAsLeader(const LeaderEpoch& epoch);

 private:
  void MaybeRunClusterBalancer(
      const LeaderEpoch& epoch, const std::vector<TableInfoPtr>& tables,
      const TabletInfoMap& tablets);

  void ScaleUpTransactionStatusTablesIfNeeded(const LeaderEpoch& epoch);
  Status DoScaleUpTransactionStatusTables(
      size_t num_live_tservers, const LeaderEpoch& epoch);
  Status ScaleUpLocalTransactionStatusTablesIfNeeded(const LeaderEpoch& epoch,
      const TableId& global_txn_table_id);
  Status AddTabletsToTransactionStatusTableIfNeeded(
      const TableInfoPtr& table, size_t num_live_tservers,
      const ReplicationInfoPB& repl_info, bool is_global, const LeaderEpoch& epoch);
  Status AddTabletsToTransactionStatusTable(
      const TableInfoPtr& table, size_t tablets_to_add, const LeaderEpoch& epoch);

  std::atomic<bool> closing_;
  bool pending_updates_;
  mutable Mutex lock_;
  ConditionVariable cond_;
  scoped_refptr<yb::Thread> thread_;
  Master* master_;
  CatalogManager* catalog_manager_;
  bool was_leader_ = false;
  scoped_refptr<EventStats> cluster_balancer_duration_;
  CoarseTimePoint last_transaction_status_check_time_;
  size_t last_live_tservers_;
};

}  // namespace master
}  // namespace yb
