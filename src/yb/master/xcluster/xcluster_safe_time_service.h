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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/client/client_fwd.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/hybrid_time.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/xcluster/xcluster_consumer_metrics.h"
#include "yb/master/xcluster/xcluster_manager_if.h"
#include "yb/rpc/scheduler.h"
#include "yb/util/threadpool.h"
#include "yb/gutil/thread_annotations.h"

namespace yb {
namespace master {

// Periodically compute the xCluster safe time and store it in sys catalog.
// If there is no active xCluster replication streams it will go into idle mode and shutdown all
// threads. Calling ScheduleTaskIfNeeded will put it back into an active mode.
class XClusterSafeTimeService {
 public:
  explicit XClusterSafeTimeService(
      Master* master, CatalogManager* catalog_manager, MetricRegistry* metric_registry);
  virtual ~XClusterSafeTimeService();

  Status Init();
  void Shutdown();

  Status CreateXClusterSafeTimeTableIfNotFound();

  void ScheduleTaskIfNeeded() EXCLUDES(shutdown_cond_lock_, task_enqueue_lock_);

  // Calculate the max_safe_time - min_safe_time for each namespace.
  Result<std::unordered_map<NamespaceId, uint64_t>> GetEstimatedDataLossMicroSec(
      const LeaderEpoch& epoch);

  Status GetXClusterSafeTimeInfoFromMap(
      const LeaderEpoch& epoch, GetXClusterSafeTimeResponsePB* resp);

  Result<XClusterNamespaceToSafeTimeMap> RefreshAndGetXClusterNamespaceToSafeTimeMap(
      const LeaderEpoch& epoch);

  xcluster::XClusterConsumerClusterMetrics* TEST_GetMetricsForNamespace(
      const NamespaceId& namespace_id);

 private:
  friend class XClusterSafeTimeServiceMocked;
  FRIEND_TEST(XClusterSafeTimeServiceTest, ComputeSafeTime);

  struct ProducerTabletInfo {
    xcluster::ReplicationGroupId replication_group_id;
    TabletId tablet_id;

    bool operator==(const ProducerTabletInfo& rhs) const {
      return replication_group_id == rhs.replication_group_id && tablet_id == rhs.tablet_id;
    }

    bool operator<(const ProducerTabletInfo& rhs) const {
      if (replication_group_id == rhs.replication_group_id) {
        return tablet_id < rhs.tablet_id;
      }
      return replication_group_id < rhs.replication_group_id;
    }

    std::string ToString() const { return YB_STRUCT_TO_STRING(replication_group_id, tablet_id); }
  };

  void ProcessTaskPeriodically() EXCLUDES(task_enqueue_lock_);

  typedef std::map<ProducerTabletInfo, HybridTime> ProducerTabletToSafeTimeMap;

  // Returns true if we need to run again.
  Result<bool> ComputeSafeTime(const int64_t leader_term, bool update_metrics = false)
      EXCLUDES(mutex_);

  virtual Result<ProducerTabletToSafeTimeMap> GetSafeTimeFromTable() REQUIRES(mutex_);

  XClusterNamespaceToSafeTimeMap GetMaxNamespaceSafeTimeFromMap(
      const ProducerTabletToSafeTimeMap& tablet_to_safe_time_map) REQUIRES(mutex_);

  // Update our producer_tablet_namespace_map_ if it is stale.
  // Returns true if an update was made, else false.
  virtual Status RefreshProducerTabletToNamespaceMap() REQUIRES(mutex_);

  virtual Result<bool> CreateTableRequired() REQUIRES(mutex_);

  virtual Result<XClusterNamespaceToSafeTimeMap> GetXClusterNamespaceToSafeTimeMap();

  virtual Status SetXClusterSafeTime(
      const int64_t leader_term, const XClusterNamespaceToSafeTimeMap& new_safe_time_map);

  virtual Status CleanupEntriesFromTable(const std::vector<ProducerTabletInfo>& entries_to_delete)
      REQUIRES(mutex_);

  Result<int64_t> GetLeaderTermFromCatalogManager();

  void UpdateMetrics(
      const ProducerTabletToSafeTimeMap& tablet_to_safe_time_map,
      const XClusterNamespaceToSafeTimeMap& current_safe_time_map) REQUIRES(mutex_);

  void EnterIdleMode(const std::string& reason);

  Master* const master_;
  CatalogManager* const catalog_manager_;

  std::atomic<bool> shutdown_;
  Mutex shutdown_cond_lock_;
  ConditionVariable shutdown_cond_;

  std::mutex task_enqueue_lock_;
  bool task_enqueued_ GUARDED_BY(task_enqueue_lock_);
  std::unique_ptr<ThreadPool> thread_pool_;
  std::unique_ptr<ThreadPoolToken> thread_pool_token_;

  std::mutex mutex_;
  bool safe_time_table_ready_ GUARDED_BY(mutex_);

  std::unique_ptr<client::TableHandle> safe_time_table_;

  int32_t cluster_config_version_ GUARDED_BY(mutex_);
  std::map<ProducerTabletInfo, NamespaceId> producer_tablet_namespace_map_ GUARDED_BY(mutex_);

  MetricRegistry* metric_registry_ GUARDED_BY(mutex_);
  std::unordered_map<NamespaceId, std::unique_ptr<xcluster::XClusterConsumerClusterMetrics>>
      cluster_metrics_per_namespace_ GUARDED_BY(mutex_);

  DISALLOW_COPY_AND_ASSIGN(XClusterSafeTimeService);
};

}  // namespace master
}  // namespace yb
