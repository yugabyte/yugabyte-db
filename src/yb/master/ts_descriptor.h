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
#ifndef YB_MASTER_TS_DESCRIPTOR_H
#define YB_MASTER_TS_DESCRIPTOR_H

#include <memory>
#include <mutex>
#include <string>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/util/shared_ptr_tuple.h"

namespace yb {

class NodeInstancePB;

namespace consensus {
class ConsensusServiceProxy;
}

namespace tserver {
class TabletServerAdminServiceProxy;
class TabletServerServiceProxy;
}

namespace master {

class TSRegistrationPB;
class TSInformationPB;
class ReplicationInfoPB;

typedef util::SharedPtrTuple<tserver::TabletServerAdminServiceProxy,
                             tserver::TabletServerServiceProxy,
                             consensus::ConsensusServiceProxy> ProxyTuple;

// Master-side view of a single tablet server.
//
// Tracks the last heartbeat, status, instance identifier, etc.
// This class is thread-safe.
class TSDescriptor {
 public:
  static CHECKED_STATUS RegisterNew(const NodeInstancePB& instance,
                                            const TSRegistrationPB& registration,
                                            gscoped_ptr<TSDescriptor>* desc);

  static std::string generate_placement_id(const CloudInfoPB& ci);

  virtual ~TSDescriptor();

  // Set the last-heartbeat time to now.
  void UpdateHeartbeatTime();

  // Return the amount of time since the last heartbeat received
  // from this TS.
  MonoDelta TimeSinceHeartbeat() const;

  // Register this tablet server.
  CHECKED_STATUS Register(const NodeInstancePB& instance,
                          const TSRegistrationPB& registration);

  const std::string &permanent_uuid() const { return permanent_uuid_; }
  int64_t latest_seqno() const;

  bool has_tablet_report() const;
  void set_has_tablet_report(bool has_report);

  // Copy the current registration info into the given PB object.
  // A safe copy is returned because the internal Registration object
  // may be mutated at any point if the tablet server re-registers.
  void GetRegistration(TSRegistrationPB* reg) const;

  // Populates the TSInformationPB for this TSDescriptor.
  void GetTSInformationPB(TSInformationPB* ts_info) const;

  // Helper function to tell if this TS matches the cloud information provided. For now, we have
  // no wildcard functionality, so it will have to explicitly match each individual component.
  // Later, this might be extended to say if this TS is part of some wildcard expression for cloud
  // information (eg: aws.us-west.* will match any TS in aws.us-west.1a or aws.us-west.1b, etc.).
  bool MatchesCloudInfo(const CloudInfoPB& cloud_info) const;

  // Return the pre-computed placement_id, comprised of the cloud_info data.
  std::string placement_id() const;

  std::string placement_uuid() const;

  bool IsRunningOn(const HostPortPB& hp) const;

  void GetNodeInstancePB(NodeInstancePB* instance_pb) const;

  // Should this ts have any leader load on it.
  virtual bool IsAcceptingLeaderLoad(const ReplicationInfoPB& replication_info) const;

  // Return an RPC proxy to a service.
  template <class TProxy>
  CHECKED_STATUS GetProxy(rpc::ProxyCache* proxy_cache,
                          std::shared_ptr<TProxy>* proxy) {
    return GetOrCreateProxy(proxy_cache, proxy, &proxies_.get<TProxy>());
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
    std::lock_guard<simple_spinlock> l(lock_);
    num_live_replicas_ = num_live_replicas;
  }

  // Return the number of live replicas (i.e running or bootstrapping).
  int num_live_replicas() const {
    std::lock_guard<simple_spinlock> l(lock_);
    return num_live_replicas_;
  }

  void set_leader_count(int leader_count) {
    DCHECK_GE(leader_count, 0);
    std::lock_guard<simple_spinlock> l(lock_);
    leader_count_ = leader_count;
  }

  int leader_count() const {
    std::lock_guard<simple_spinlock> l(lock_);
    return leader_count_;
  }

  void set_total_memory_usage(uint64_t total_memory_usage) {
    std::lock_guard<simple_spinlock> l(lock_);
    tsMetrics_.total_memory_usage = total_memory_usage;
  }

  uint64_t total_memory_usage() {
    std::lock_guard<simple_spinlock> l(lock_);
    return tsMetrics_.total_memory_usage;
  }

  void set_total_sst_file_size (uint64_t total_sst_file_size) {
    std::lock_guard<simple_spinlock> l(lock_);
    tsMetrics_.total_sst_file_size = total_sst_file_size;
  }

  uint64_t total_sst_file_size() {
    std::lock_guard<simple_spinlock> l(lock_);
    return tsMetrics_.total_sst_file_size;
  }

  void set_read_ops_per_sec(double read_ops_per_sec) {
    std::lock_guard<simple_spinlock> l(lock_);
    tsMetrics_.read_ops_per_sec = read_ops_per_sec;
  }

  double read_ops_per_sec() {
    std::lock_guard<simple_spinlock> l(lock_);
    return tsMetrics_.read_ops_per_sec;
  }

  void set_write_ops_per_sec(double write_ops_per_sec) {
    std::lock_guard<simple_spinlock> l(lock_);
    tsMetrics_.write_ops_per_sec = write_ops_per_sec;
  }

  double write_ops_per_sec() {
    std::lock_guard<simple_spinlock> l(lock_);
    return tsMetrics_.write_ops_per_sec;
  }

  void ClearMetrics() {
    tsMetrics_.ClearMetrics();
  }

  // Set of methods to keep track of pending tablet deletes for a tablet server. We use them to
  // avoid assigning more tablets to a tserver that might be potentially unresponsive.
  bool HasTabletDeletePending() const;
  bool IsTabletDeletePending(const std::string& tablet_id) const;
  void AddPendingTabletDelete(const std::string& tablet_id);
  void ClearPendingTabletDelete(const std::string& tablet_id);

  std::string ToString() const;

  // Indicates that this descriptor was removed from the cluster and shouldn't be surfaced.
  bool IsRemoved() const {
    return removed_;
  }

  void SetRemoved() {
    removed_ = true;
  }

  explicit TSDescriptor(std::string perm_id);

 protected:
  virtual CHECKED_STATUS RegisterUnlocked(const NodeInstancePB& instance,
                                          const TSRegistrationPB& registration);

  mutable simple_spinlock lock_;
 private:
  template <class TProxy>
  CHECKED_STATUS GetOrCreateProxy(rpc::ProxyCache* proxy_cache,
                                  std::shared_ptr<TProxy>* result,
                                  std::shared_ptr<TProxy>* result_cache);

  FRIEND_TEST(TestTSDescriptor, TestReplicaCreationsDecay);
  template<class ClusterLoadBalancerClass> friend class TestLoadBalancerBase;

  // Uses DNS to resolve registered hosts to a single endpoint.
  Result<HostPort> GetHostPortUnlocked() const;

  void DecayRecentReplicaCreationsUnlocked();

  struct TSMetrics {

    // Stores the total RAM usage of a tserver that is sent in every heartbeat.
    uint64_t total_memory_usage = 0;

    // Stores the total size of all the sst files in a tserver
    uint64_t total_sst_file_size = 0;

    double read_ops_per_sec = 0;

    double write_ops_per_sec = 0;

    void ClearMetrics() {
      total_memory_usage = 0;
      total_sst_file_size = 0;
      read_ops_per_sec = 0;
      write_ops_per_sec = 0;
    }
  };

  struct TSMetrics tsMetrics_;

  const std::string permanent_uuid_;
  int64_t latest_seqno_;

  // The last time a heartbeat was received for this node.
  MonoTime last_heartbeat_;

  // Set to true once this instance has reported all of its tablets.
  bool has_tablet_report_;

  // The number of times this tablet server has recently been selected to create a
  // tablet replica. This value decays back to 0 over time.
  double recent_replica_creations_;
  MonoTime last_replica_creations_decay_;

  // The number of live replicas on this host, from the last heartbeat.
  int num_live_replicas_;

  // The number of tablets for which this ts is a leader.
  int leader_count_;

  gscoped_ptr<TSRegistrationPB> registration_;
  std::string placement_id_;

  // The (read replica) cluster uuid to which this tserver belongs.
  std::string placement_uuid_;

  YB_EDITION_NS_PREFIX ProxyTuple proxies_;

  // Set of tablet uuids for which a delete is pending on this tablet server.
  std::set<std::string> tablets_pending_delete_;

  // We don't remove TSDescriptor's from the master's in memory map since several classes hold
  // references to this object and those would be invalidated if we remove the descriptor from
  // the master's map. As a result, we just store a boolean indicating this entry is removed and
  // shouldn't be surfaced.
  bool removed_ = false;

  DISALLOW_COPY_AND_ASSIGN(TSDescriptor);
};

template <class TProxy>
Status TSDescriptor::GetOrCreateProxy(rpc::ProxyCache* proxy_cache,
                                      std::shared_ptr<TProxy>* result,
                                      std::shared_ptr<TProxy>* result_cache) {
  {
    std::lock_guard<simple_spinlock> l(lock_);
    if (*result_cache) {
      *result = *result_cache;
      return Status::OK();
    }
    auto hostport = VERIFY_RESULT(GetHostPortUnlocked());
    if (!(*result_cache)) {
      result_cache->reset(new TProxy(proxy_cache, hostport));
    }
    *result = *result_cache;
  }
  return Status::OK();
}

} // namespace master
} // namespace yb
#endif // YB_MASTER_TS_DESCRIPTOR_H
