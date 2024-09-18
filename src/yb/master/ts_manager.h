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

#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/common/common_fwd.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/master/master_cluster.fwd.h"
#include "yb/master/master_fwd.h"
#include "yb/master/ts_descriptor.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/status_fwd.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/mutex.h"
#include "yb/util/net/net_util.h"

namespace yb {

class NodeInstancePB;

namespace master {

class TSInformationPB;

// A callback that is called when the number of tablet servers reaches a certain number.
using TSCountCallback = std::function<void()>;

// Tracks the servers that the master has heard from, along with their
// last heartbeat, etc.
//
// Note that TSDescriptors are never deleted, even if the TS crashes
// and has not heartbeated in quite a while. This makes it simpler to
// keep references to TSDescriptors elsewhere in the master without
// fear of lifecycle problems. Dead servers are "dead, but not forgotten"
// (they live on in the heart of the master).
//
// This class is thread-safe.
//
// LOCKING ORDER:
//   This class has two locks and deals with the locks of TSDescriptorPtr objects, which themselves
//   have 2 locks. The locking partial order is:
//     1. registration_lock_ before exclusive map_lock_
//     2. registration_lock_ before COW write locks
//     3. shared map_lock_ before COW write lock
//     4. TSDescriptor::WriteLock before exclusive(TSDescriptor::mutex_)
//     5. For TSDescriptor objects TS1 and TS2, the locking order between them is whichever is first
//     in servers_by_id_.
//
//   More simply, for exclusive locks following this total order won't result in deadlocks:
//       all shared locks, registration_lock_, map_lock_, descriptor writelock, descriptor spinlock
//
//   It may be safe to violate but think carefully when doing so.
class TSManager {
 public:
  TSManager() {}
  virtual ~TSManager() {}

  // Lookup the tablet server descriptor for the given instance identifier.
  // If the TS has never registered, or this instance doesn't match the
  // current instance ID for the TS, then a NotFound status is returned.
  // Otherwise, *desc is set and OK is returned.
  Result<TSDescriptorPtr> LookupTS(const NodeInstancePB& instance) const;

  // Lookup the tablet server descriptor for the given UUID.
  // Returns false if the TS has never registered.
  // Otherwise, *desc is set and returns true.
  bool LookupTSByUUID(const std::string& uuid, TSDescriptorPtr* desc);

  // Register a tserver that is not already registered but is in the raft config of a tablet report
  // encountered during ts heartbeat processing. This should not be necessary once registration is
  // persisted but is kept for backwards compatibility.
  Status RegisterFromRaftConfig(
      const NodeInstancePB& instance, const TSRegistrationPB& registration,
      CloudInfoPB&& local_cloud_info, rpc::ProxyCache* proxy_cache);

  // Lookup an existing TS descriptor from a heartbeat request. If found, update the TSDescriptor
  // using the metadata in the heartbeat request.
  Result<TSDescriptorPtr> LookupAndUpdateTSFromHeartbeat(
      const TSHeartbeatRequestPB& heartbeat_request) const;

  Result<TSDescriptorPtr> RegisterFromHeartbeat(
      const TSHeartbeatRequestPB& heartbeat_request, CloudInfoPB&& local_cloud_info,
      rpc::ProxyCache* proxy_cache);

  // Return all of the currently registered TS descriptors into the provided list.
  void GetAllDescriptors(TSDescriptorVector* descs) const;
  TSDescriptorVector GetAllDescriptors() const;

  // Return all of the currently registered TS descriptors that have sent a
  // heartbeat recently, indicating that they're alive and well.
  // Optionally pass in blacklist as a set of HostPorts to return all live non-blacklisted servers.
  void GetAllLiveDescriptors(TSDescriptorVector* descs,
                             const std::optional<BlacklistSet>& blacklist = std::nullopt) const;

  // Return all of the currently registered TS descriptors that have sent a heartbeat
  // recently and are in the same 'cluster' with given placement uuid.
  // Optionally pass in blacklist as a set of HostPorts to return all live non-blacklisted servers.
  void GetAllLiveDescriptorsInCluster(TSDescriptorVector* descs, const std::string& placement_uuid,
                                      const std::optional<BlacklistSet>& blacklist = std::nullopt,
                                      bool primary_cluster = true) const;

  // Return all of the currently registered TS descriptors that have sent a
  // heartbeat, indicating that they're alive and well, recently and have given
  // full report of their tablets as well.
  void GetAllReportedDescriptors(TSDescriptorVector* descs) const;

  // Check if the placement uuid of the tserver is same as given cluster uuid.
  static bool IsTsInCluster(const TSDescriptorPtr& ts, const std::string& cluster_uuid);

  static bool IsTsBlacklisted(const TSDescriptorPtr& ts,
                              const std::optional<BlacklistSet>& blacklist = std::nullopt);

  // Register a callback to be called when the number of tablet servers reaches a certain number.
  // The callback is removed after it is called once.
  void SetTSCountCallback(int min_count, TSCountCallback callback);

  size_t NumDescriptors() const;

  size_t NumLiveDescriptors() const;

  // Find TServers that are currently in the state LIVE but have not heartbeated for a long time.
  // Transition all such TServers into the UNRESPONSIVE state.
  void MarkUnresponsiveTServers();

 private:
  // Performs all mutations necessary to register a new tserver or update the registration of an
  // existing tserver. There are two registration pathways, one through heartbeats and the other
  // through membership in an tablet group that is heartbeating to the master.
  Result<TSDescriptorPtr> RegisterInternal(
      const NodeInstancePB& instance, const TSRegistrationPB& registration,
      std::optional<std::reference_wrapper<const TSHeartbeatRequestPB>> request,
      CloudInfoPB&& local_cloud_info, rpc::ProxyCache* proxy_cache);

  // Encodes the mutations that must be performed to register a new tserver, or update the
  // registration of an already registered tserver.
  struct RegistrationMutationData {
    TSDescriptorPtr desc;
    TSDescriptor::WriteLock registered_desc_lock;
    std::vector<TSDescriptorPtr> replaced_descs;
    std::vector<TSDescriptor::WriteLock> replaced_desc_locks;
    bool insert_into_map = false;
  };

  // Determines whether or not to register a new tserver or upgrade the registration information of
  // an already registered tserver. If the registration should be performed, encodes what mutations
  // to perform in the RegistrationMutationData return value.
  //
  // This function does not perform the mutations itself because interleaving the IO of sys catalog
  // writes with various locks is complex.
  Result<RegistrationMutationData> ComputeRegistrationMutationData(
      const NodeInstancePB& instance, const TSRegistrationPB& registration,
      CloudInfoPB&& local_cloud_info, rpc::ProxyCache* proxy_cache,
      RegisteredThroughHeartbeat registered_through_heartbeat) REQUIRES(registration_lock_);

  // Perform the mutations encoded in RegistrationMutationData.
  //   - write to sys catalog (todo(zdrudi))
  //   - insert a newly registered tserver into servers_by_id_
  //   - commit write locks
  //
  // This function does not call the callback but instead returns it if it should be called. Callers
  // must call the callback if it is not empty.
  Result<std::pair<TSDescriptorPtr, TSCountCallback>> DoRegistrationMutation(
      const NodeInstancePB& new_ts_instance, const TSRegistrationPB& registration,
      RegistrationMutationData&& registration_data) REQUIRES(registration_lock_);

  void GetDescriptors(std::function<bool(const TSDescriptorPtr&)> condition,
                      TSDescriptorVector* descs) const;

  void GetAllDescriptorsUnlocked(TSDescriptorVector* descs) const REQUIRES_SHARED(map_lock_);
  void GetDescriptorsUnlocked(
      std::function<bool(const TSDescriptorPtr&)> condition, TSDescriptorVector* descs) const
      REQUIRES_SHARED(map_lock_);

  // Returns the registered ts descriptors whose hostport matches the hostport in the
  // registration argument.
  Result<std::pair<std::vector<TSDescriptorPtr>, std::vector<TSDescriptor::WriteLock>>>
  FindHostPortCollisions(
      const NodeInstancePB& instance, const TSRegistrationPB& registration,
      const CloudInfoPB& local_cloud_info) const REQUIRES_SHARED(map_lock_);

  size_t NumDescriptorsUnlocked() const REQUIRES_SHARED(map_lock_);

  // These two locks are used as in an ad-hoc implementation of a ternary read-write-commit pattern
  // to protect servers_by_id_. We use this model because we may have to do a lot of IO when
  // deciding whether or not to add a new TSDescriptor to the servers_by_id_ map and we'd prefer not
  // to lock out readers during this time. However we want to strictly serialize threads trying to
  // add a new entry to servers_by_id_.
  //
  //   shared map_lock_: used as the read lock.
  //   exclusive map_lock_: used as the commit lock. Held very briefly when inserting a new entry
  //       into the map. Threads must hold registration_lock_ before acquiring exclusive access to
  //       map_lock_.
  //   exclusive registration_lock_: used as the write lock. Acquired to begin the registration
  //       critical section.
  //   shared registration_lock_: not used.
  //
  // todo(zdrudi): add RAII wrappers to the RWCLock class and use that instead.
  mutable rw_spinlock map_lock_;
  Mutex registration_lock_;

  using TSDescriptorMap = std::unordered_map<std::string, TSDescriptorPtr>;
  TSDescriptorMap servers_by_id_ GUARDED_BY(map_lock_);

  // This callback will be called when the number of tablet servers reaches the given number.
  TSCountCallback ts_count_callback_ GUARDED_BY(registration_lock_);
  size_t ts_count_callback_min_count_ GUARDED_BY(registration_lock_) = 0;

  DISALLOW_COPY_AND_ASSIGN(TSManager);
};

} // namespace master
} // namespace yb
