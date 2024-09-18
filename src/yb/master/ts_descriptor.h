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

#include <shared_mutex>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include <gtest/gtest_prod.h>

#include "yb/common/common_net.pb.h"
#include "yb/common/hybrid_time.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/master/catalog_entity_base.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_heartbeat.fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/physical_time.h"
#include "yb/util/result.h"
#include "yb/util/status_fwd.h"
#include "yb/util/shared_ptr_tuple.h"
#include "yb/util/shared_lock.h"

namespace yb {

class NodeInstancePB;

namespace consensus {
class ConsensusServiceProxy;
}

namespace tserver {
class TabletServerAdminServiceProxy;
class TabletServerServiceProxy;
class TabletServerBackupServiceProxy;
}

namespace cdc {
class CDCServiceProxy;
}

namespace master {

class TSRegistrationPB;
class TSInformationPB;
class ReplicationInfoPB;
class TServerMetricsPB;

typedef util::SharedPtrTuple<
    tserver::TabletServerAdminServiceProxy,
    tserver::TabletServerServiceProxy,
    tserver::TabletServerBackupServiceProxy,
    cdc::CDCServiceProxy,
    consensus::ConsensusServiceProxy>
    ProxyTuple;

struct PersistentServerInfo
    : public Persistent<SysTServerEntryPB> {};

// Master-side view of a single tablet server.
//
// Tracks the last heartbeat, status, instance identifier, etc.
// This class is thread-safe.
class TSDescriptor : public MetadataCowWrapper<PersistentServerInfo> {
 public:
  static Result<std::pair<TSDescriptorPtr, TSDescriptor::WriteLock>> CreateNew(
      const NodeInstancePB& instance,
      const TSRegistrationPB& registration,
      CloudInfoPB local_cloud_info,
      rpc::ProxyCache* proxy_cache,
      // What the source of this registration request is. TServers can be registered by heartbeating
      // to the master.
      //
      // Alternatively, if a tserver appears in the raft config for a tablet reported by another
      // TServer, the master will attempt to register the tserver even if it hasn't received a
      // heartbeat from it.
      RegisteredThroughHeartbeat registered_through_heartbeat = RegisteredThroughHeartbeat::kTrue);

  static std::string generate_placement_id(const CloudInfoPB& ci);

  virtual ~TSDescriptor() = default;

  // Updates TS metadata -
  //     hybrid time on the TS
  //     tablet leaders on the TS
  //     heartbeat rtt
  //     etc
  // from the heartbeat request. This method also validates that this is the latest heartbeat
  // request received from the tserver. If not, this method does no mutations and returns an error
  // status.
  Status UpdateTSMetadataFromHeartbeat(
      const TSHeartbeatRequestPB& req, TSDescriptor::WriteLock* lock);

  // Return the amount of time since the last heartbeat received from this TS.
  MonoDelta TimeSinceHeartbeat() const;
  MonoTime LastHeartbeatTime() const;

  Result<TSDescriptor::WriteLock> UpdateRegistration(
      const NodeInstancePB& instance, const TSRegistrationPB& registration,
      CloudInfoPB local_cloud_info, rpc::ProxyCache* proxy_cache);

  const std::string& permanent_uuid() const { return permanent_uuid_; }
  const std::string& id() const override { return permanent_uuid(); }

  int64_t latest_seqno() const;
  int32_t latest_report_seqno() const;

  bool has_tablet_report() const;
  void set_has_tablet_report(bool has_report);

  bool has_faulty_drive() const;

  bool registered_through_heartbeat() const;

  ServerRegistrationPB GetRegistration() const;
  ResourcesPB GetResources() const;

  // Returns TSInformationPB for this TSDescriptor.
  // todo(zdrudi): See if we can remove at least some of these functions.
  TSInformationPB GetTSInformationPB() const;
  TSRegistrationPB GetTSRegistrationPB() const;
  NodeInstancePB GetNodeInstancePB() const;

  // Helper function to tell if this TS matches the cloud information provided.
  // The cloud info might be a wildcard expression (e.g. aws.us-west.*, which will match any TS in
  // aws.us-west.1a or aws.us-west.1b, etc.).
  bool MatchesCloudInfo(const CloudInfoPB& cloud_info) const;

  CloudInfoPB GetCloudInfo() const;

  // Return the pre-computed placement_id, comprised of the cloud_info data.
  std::string placement_id() const;

  std::string placement_uuid() const;

  bool IsRunningOn(const HostPortPB& hp) const;
  bool IsBlacklisted(const BlacklistSet& blacklist) const;

  // Should this ts have any leader load on it.
  bool IsAcceptingLeaderLoad(const ReplicationInfoPB& replication_info) const;

  // Return an RPC proxy to a service.
  template <class TProxy>
  Status GetProxy(std::shared_ptr<TProxy>* proxy) {
    return GetOrCreateProxy(proxy, &proxies_.get<TProxy>());
  }

  // Increment the accounting of the number of replicas recently created on this
  // server. This value will automatically decay over time.
  void IncrementRecentReplicaCreations();

  // Return the number of replicas which have recently been created on this
  // TS. This number is incremented when replicas are placed on the TS, and
  // then decayed over time. This method is not 'const' because each call
  // actually performs the time-based decay.
  double RecentReplicaCreations();

  // Set the number of live replicas (i.e. running or bootstrapping).
  void set_num_live_replicas(int num_live_replicas) {
    DCHECK_GE(num_live_replicas, 0);
    std::lock_guard l(mutex_);
    num_live_replicas_ = num_live_replicas;
  }

  // Return the number of live replicas (i.e running or bootstrapping).
  int num_live_replicas() const {
    SharedLock<decltype(mutex_)> l(mutex_);
    return num_live_replicas_;
  }

  void set_leader_count(int leader_count) {
    DCHECK_GE(leader_count, 0);
    std::lock_guard l(mutex_);
    leader_count_ = leader_count;
  }

  int leader_count() const {
    SharedLock<decltype(mutex_)> l(mutex_);
    return leader_count_;
  }

  MicrosTime physical_time() const {
    SharedLock<decltype(mutex_)> l(mutex_);
    return physical_time_;
  }

  void set_hybrid_time(HybridTime hybrid_time) {
    std::lock_guard l(mutex_);
    hybrid_time_ = hybrid_time;
  }

  HybridTime hybrid_time() const {
    SharedLock<decltype(mutex_)> l(mutex_);
    return hybrid_time_;
  }

  MonoDelta heartbeat_rtt() const {
    SharedLock<decltype(mutex_)> l(mutex_);
    return heartbeat_rtt_;
  }

  uint64_t total_memory_usage() {
    SharedLock<decltype(mutex_)> l(mutex_);
    return ts_metrics_.total_memory_usage;
  }

  uint64_t total_sst_file_size() {
    SharedLock<decltype(mutex_)> l(mutex_);
    return ts_metrics_.total_sst_file_size;
  }

  uint64_t uncompressed_sst_file_size() {
    SharedLock<decltype(mutex_)> l(mutex_);
    return ts_metrics_.uncompressed_sst_file_size;
  }

  uint64_t num_sst_files() {
    SharedLock<decltype(mutex_)> l(mutex_);
    return ts_metrics_.num_sst_files;
  }

  double read_ops_per_sec() {
    SharedLock<decltype(mutex_)> l(mutex_);
    return ts_metrics_.read_ops_per_sec;
  }

  double write_ops_per_sec() {
    SharedLock<decltype(mutex_)> l(mutex_);
    return ts_metrics_.write_ops_per_sec;
  }

  uint64_t uptime_seconds() {
    SharedLock<decltype(mutex_)> l(mutex_);
    return ts_metrics_.uptime_seconds;
  }

  struct TSPathMetrics {
    uint64_t used_space = 0;
    uint64_t total_space = 0;
  };

  std::unordered_map<std::string, TSPathMetrics> path_metrics() {
    SharedLock<decltype(mutex_)> l(mutex_);
    return ts_metrics_.path_metrics;
  }

  bool get_disable_tablet_split_if_default_ttl() {
    SharedLock<decltype(mutex_)> l(mutex_);
    return ts_metrics_.disable_tablet_split_if_default_ttl;
  }

  void UpdateMetrics(const TServerMetricsPB& metrics);

  void GetMetrics(TServerMetricsPB* metrics);

  void ClearMetrics() {
    std::lock_guard l(mutex_);
    ts_metrics_.ClearMetrics();
  }

  Status IsReportCurrent(const NodeInstancePB& ts_instance, const TabletReportPB& report);

  // Set of methods to keep track of pending tablet deletes for a tablet server.
  bool HasTabletDeletePending() const;
  void AddPendingTabletDelete(const std::string& tablet_id);
  size_t ClearPendingTabletDelete(const std::string& tablet_id);
  std::string PendingTabletDeleteToString() const;
  std::set<std::string> TabletsPendingDeletion() const;

  std::string ToString() const override;

  // Indicates that this descriptor was removed from the cluster and shouldn't be surfaced.
  bool IsRemoved() const {
    return LockForRead()->pb.state() == SysTServerEntryPB::REPLACED;
  }

  explicit TSDescriptor(
      const std::string& permanent_uuid, RegisteredThroughHeartbeat registered_through_heartbeat);

  std::size_t NumTasks() const;

  bool IsLive() const;

  virtual bool IsLiveAndHasReported() const;

  bool HasYsqlCatalogLease() const;

  // Is the ts in a read-only placement.
  bool IsReadOnlyTS(const ReplicationInfoPB& replication_info) const;

 private:
  mutable rw_spinlock mutex_;
  template <class TProxy>
  Status GetOrCreateProxy(std::shared_ptr<TProxy>* result,
                          std::shared_ptr<TProxy>* result_cache);

  FRIEND_TEST(TestTSDescriptor, TestReplicaCreationsDecay);
  friend class LoadBalancerMockedBase;

  // Uses DNS to resolve registered hosts to a single endpoint.
  Result<HostPort> GetHostPortUnlocked() const REQUIRES_SHARED(mutex_);

  void DecayRecentReplicaCreationsUnlocked() REQUIRES(mutex_);

  template <typename LockType>
  Status IsReportCurrentUnlocked(
      const NodeInstancePB& ts_instance,
      std::optional<std::reference_wrapper<const TabletReportPB>> report, LockType* lock)
      REQUIRES_SHARED(mutex_);

  struct TSMetrics {

    // Stores the total RAM usage of a tserver that is sent in every heartbeat.
    uint64_t total_memory_usage = 0;

    // Stores the total size of all the sst files in a tserver
    uint64_t total_sst_file_size = 0;
    uint64_t uncompressed_sst_file_size = 0;
    uint64_t num_sst_files = 0;

    double read_ops_per_sec = 0;

    double write_ops_per_sec = 0;

    uint64_t uptime_seconds = 0;

    std::unordered_map<std::string, TSPathMetrics> path_metrics;

    bool disable_tablet_split_if_default_ttl = false;

    void ClearMetrics() {
      total_memory_usage = 0;
      total_sst_file_size = 0;
      uncompressed_sst_file_size = 0;
      num_sst_files = 0;
      read_ops_per_sec = 0;
      write_ops_per_sec = 0;
      uptime_seconds = 0;
      path_metrics.clear();
      disable_tablet_split_if_default_ttl = false;
    }
  };

  const std::string permanent_uuid_;

  struct TSMetrics ts_metrics_ GUARDED_BY(mutex_);

  CloudInfoPB local_cloud_info_ GUARDED_BY(mutex_);
  rpc::ProxyCache* proxy_cache_ GUARDED_BY(mutex_);
  int64_t latest_seqno_ GUARDED_BY(mutex_);

  // The last time a heartbeat was received for this node.
  MonoTime last_heartbeat_ GUARDED_BY(mutex_);
  const bool registered_through_heartbeat_;

  // The physical and hybrid times on this node at the time of heartbeat
  MicrosTime physical_time_ GUARDED_BY(mutex_);
  HybridTime hybrid_time_ GUARDED_BY(mutex_);

  // Roundtrip time of previous heartbeat.
  MonoDelta heartbeat_rtt_ GUARDED_BY(mutex_);

  // The sequence number of the latest tablet report from this tserver.
  // Initialized to the smallest possible value and reset to the smallest possible value on TS
  // registration. Before beginning processing of a new tablet report, set to the sequence number of
  // the tablet report from the tserver. While processing a batch of tablets in a tablet report, if
  // the sequence number in the report no longer matches this saved value then report processing
  // stops.
  int32_t latest_report_seqno_ GUARDED_BY(mutex_);

  // Set to true once this instance has reported all of its tablets.
  bool has_tablet_report_ GUARDED_BY(mutex_);

  // Tablet server has at least one faulty drive.
  bool has_faulty_drive_ GUARDED_BY(mutex_);

  // The number of times this tablet server has recently been selected to create a
  // tablet replica. This value decays back to 0 over time.
  double recent_replica_creations_ GUARDED_BY(mutex_);
  MonoTime last_replica_creations_decay_ GUARDED_BY(mutex_);

  // The number of live replicas on this host, from the last heartbeat.
  int num_live_replicas_ GUARDED_BY(mutex_);

  // The number of tablets for which this ts is a leader.
  int leader_count_ GUARDED_BY(mutex_);

  std::string placement_id_ GUARDED_BY(mutex_);

  ProxyTuple proxies_;

  // Set of tablet uuids for which a delete is pending on this tablet server.
  std::set<std::string> tablets_pending_delete_ GUARDED_BY(mutex_);

  // We don't remove TSDescriptor's from the master's in memory map since several classes hold
  // references to this object and those would be invalidated if we remove the descriptor from
  // the master's map. As a result, we just store a boolean indicating this entry is removed and
  // shouldn't be surfaced.
  std::atomic<bool> removed_{false};

  DISALLOW_COPY_AND_ASSIGN(TSDescriptor);
};

template <class TProxy>
Status TSDescriptor::GetOrCreateProxy(std::shared_ptr<TProxy>* result,
                                      std::shared_ptr<TProxy>* result_cache) {
  {
    std::lock_guard l(mutex_);
    if (*result_cache) {
      *result = *result_cache;
      return Status::OK();
    }
    auto hostport = VERIFY_RESULT(GetHostPortUnlocked());
    if (!(*result_cache)) {
      *result_cache = std::make_shared<TProxy>(proxy_cache_, hostport);
    }
    *result = *result_cache;
  }
  return Status::OK();
}

struct cloud_equal_to {
  bool operator()(const yb::CloudInfoPB& x, const yb::CloudInfoPB& y) const {
    return x.placement_cloud() == y.placement_cloud() &&
           x.placement_region() == y.placement_region() && x.placement_zone() == y.placement_zone();
  }
};

struct cloud_hash {
  std::size_t operator()(const yb::CloudInfoPB& ci) const {
    return std::hash<std::string>{}(TSDescriptor::generate_placement_id(ci));
  }
};
} // namespace master
} // namespace yb
