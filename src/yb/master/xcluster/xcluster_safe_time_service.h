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
//

#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>

#include "yb/client/client_fwd.h"

#include "yb/common/hybrid_time.h"
#include "yb/common/xcluster_util.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/master/master_defaults.h"
#include "yb/master/xcluster/xcluster_consumer_metrics.h"
#include "yb/master/xcluster/xcluster_manager_if.h"

#include "yb/rpc/poller.h"

namespace yb {

class MetricRegistry;

namespace master {

// Periodically compute the xCluster safe time and store it in sys catalog.
// If there is no active xCluster replication streams it will go into idle mode and shutdown all
// threads. Calling ScheduleTaskIfNeeded will put it back into an active mode.
class XClusterSafeTimeService {
 public:
  explicit XClusterSafeTimeService(
      Master* master, CatalogManager* catalog_manager, MetricRegistry* metric_registry);
  virtual ~XClusterSafeTimeService();

  void Shutdown();

  Status CreateXClusterSafeTimeTableIfNotFound();

  void ScheduleTaskIfNeeded();

  // Calculate the max_safe_time - min_safe_time for each namespace.
  Result<std::unordered_map<NamespaceId, uint64_t>> GetEstimatedDataLossMicroSec(
      const LeaderEpoch& epoch);

  // Computes the safe time map and fills it in the response.
  Status ComputeAndGetXClusterSafeTimeInfo(
      const LeaderEpoch& epoch, GetXClusterSafeTimeResponsePB& resp);

  Result<HybridTime> GetXClusterSafeTimeForNamespace(
      const NamespaceId& namespace_id, const XClusterSafeTimeFilter& filter) const;

  xcluster::XClusterConsumerClusterMetrics* TEST_GetMetricsForNamespace(
      const NamespaceId& namespace_id);

  // Returns true if we need to run again.
  Result<bool> ComputeSafeTime(const int64_t leader_term, bool update_metrics = false)
      EXCLUDES(mutex_);

 private:
  friend class XClusterSafeTimeServiceMocked;
  friend class XClusterSafeTimeServiceTest;

  void ProcessTaskPeriodically();

  typedef std::map<xcluster::SafeTimeTablePK, HybridTime> ProducerTabletToSafeTimeMap;

  virtual Result<ProducerTabletToSafeTimeMap> GetSafeTimeFromTable() REQUIRES(mutex_);

  XClusterNamespaceToSafeTimeMap GetMaxNamespaceSafeTimeFromMap(
      const ProducerTabletToSafeTimeMap& tablet_to_safe_time_map) REQUIRES(mutex_);

  // Update our producer_tablet_namespace_map_ if it is stale.
  // Returns true if an update was made, else false.
  virtual Status RefreshProducerTabletToNamespaceMap() REQUIRES(mutex_);

  virtual Result<bool> CreateTableRequired() REQUIRES(mutex_);

  virtual XClusterNamespaceToSafeTimeMap GetXClusterNamespaceToSafeTimeMap() const;

  virtual Status SetXClusterSafeTime(
      const int64_t leader_term, const XClusterNamespaceToSafeTimeMap& new_safe_time_map);

  virtual Status CleanupEntriesFromTable(
      const std::vector<xcluster::SafeTimeTablePK>& entries_to_delete) REQUIRES(mutex_);

  Result<int64_t> GetLeaderTermFromCatalogManager();

  virtual Result<HybridTime> GetLeaderSafeTimeFromCatalogManager();

  void UpdateMetrics(
      const ProducerTabletToSafeTimeMap& tablet_to_safe_time_map,
      const XClusterNamespaceToSafeTimeMap& current_safe_time_map) REQUIRES(mutex_);

  void EnterIdleMode(const std::string& reason);

  Result<XClusterNamespaceToSafeTimeMap> GetFilteredXClusterSafeTimeMap(
      const XClusterSafeTimeFilter& filter) const REQUIRES_SHARED(mutex_);

  Master* const master_;
  CatalogManager* const catalog_manager_;

  rpc::Poller poller_;
  std::optional<boost::asio::io_context::strand> poll_strand_;

  mutable std::shared_mutex mutex_;
  bool safe_time_table_ready_ GUARDED_BY(mutex_) = false;

  std::unique_ptr<client::TableHandle> safe_time_table_;

  int32_t cluster_config_version_ GUARDED_BY(mutex_) = kInvalidClusterConfigVersion;
  std::map<xcluster::SafeTimeTablePK, NamespaceId> producer_tablet_namespace_map_
      GUARDED_BY(mutex_);

  // List of tablet ids for ddl_queue tables, used to find safe times without this stream.
  std::unordered_set<TabletId> ddl_queue_tablet_ids_ GUARDED_BY(mutex_);

  XClusterNamespaceToSafeTimeMap safe_time_map_without_ddl_queue_ GUARDED_BY(mutex_);

  MetricRegistry* metric_registry_ GUARDED_BY(mutex_);
  std::unordered_map<NamespaceId, std::unique_ptr<xcluster::XClusterConsumerClusterMetrics>>
      cluster_metrics_per_namespace_ GUARDED_BY(mutex_);

  DISALLOW_COPY_AND_ASSIGN(XClusterSafeTimeService);
};

}  // namespace master
}  // namespace yb
