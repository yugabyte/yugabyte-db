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
// This module is internal to the client and not a public API.
#ifndef YB_CLIENT_META_CACHE_H
#define YB_CLIENT_META_CACHE_H

#include <map>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

#include <boost/thread/shared_mutex.hpp>

#include "yb/client/client_fwd.h"

#include "yb/common/partition.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/metadata.pb.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/rpc.h"

#include "yb/tablet/metadata.pb.h"

#include "yb/util/async_util.h"
#include "yb/util/capabilities.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/semaphore.h"
#include "yb/util/status.h"
#include "yb/util/memory/arena.h"
#include "yb/util/net/net_util.h"

namespace yb {

class Histogram;
class YBPartialRow;

namespace tserver {
class TabletServerServiceProxy;

// A local tablet server with methods that can be invoked directly and without blocking.
class GetTabletStatusRequestPB;
class GetTabletStatusResponsePB;

class LocalTabletServer {
 public:
  LocalTabletServer() = default;
  virtual ~LocalTabletServer() = default;

  virtual CHECKED_STATUS GetTabletStatus(const GetTabletStatusRequestPB* req,
                                         GetTabletStatusResponsePB* resp) const = 0;

  virtual bool LeaderAndReady(const TabletId& tablet_id, bool allow_stale = false) const = 0;
};

} // namespace tserver

namespace master {
class MasterServiceProxy;
class TabletLocationsPB_ReplicaPB;
class TabletLocationsPB;
class TSInfoPB;
} // namespace master

namespace client {

class ClientTest_TestMasterLookupPermits_Test;
class YBClient;
class YBTable;

namespace internal {

class LookupRpc;
class LookupByKeyRpc;
class LookupByIdRpc;

// The information cached about a given tablet server in the cluster.
//
// A RemoteTabletServer could be the local tablet server.
//
// This class is thread-safe.
class RemoteTabletServer {
 public:
  RemoteTabletServer(const std::string& uuid,
                     const std::shared_ptr<tserver::TabletServerServiceProxy>& proxy,
                     const tserver::LocalTabletServer* local_tserver = nullptr);
  explicit RemoteTabletServer(const master::TSInfoPB& pb);

  // Initialize the RPC proxy to this tablet server, if it is not already set up.
  // This will involve a DNS lookup if there is not already an active proxy.
  // If there is an active proxy, does nothing.
  CHECKED_STATUS InitProxy(YBClient* client);

  // Update information from the given pb.
  // Requires that 'pb''s UUID matches this server.
  void Update(const master::TSInfoPB& pb);

  // Is this tablet server local?
  bool IsLocal() const;

  const tserver::LocalTabletServer* local_tserver() const {
    return local_tserver_;
  }

  // Return the current proxy to this tablet server. Requires that InitProxy()
  // be called prior to this.
  std::shared_ptr<tserver::TabletServerServiceProxy> proxy() const;
  ::yb::HostPort ProxyEndpoint() const;

  std::string ToString() const;

  bool HasHostFrom(const std::unordered_set<std::string>& hosts) const;

  // Returns the remote server's uuid.
  const std::string& permanent_uuid() const;

  const CloudInfoPB& cloud_info() const;

  const google::protobuf::RepeatedPtrField<HostPortPB>& public_rpc_hostports() const;

  const google::protobuf::RepeatedPtrField<HostPortPB>& private_rpc_hostports() const;

  bool HasCapability(CapabilityId capability) const;

 private:
  mutable rw_spinlock mutex_;
  const std::string uuid_;

  google::protobuf::RepeatedPtrField<HostPortPB> public_rpc_hostports_;
  google::protobuf::RepeatedPtrField<HostPortPB> private_rpc_hostports_;
  yb::CloudInfoPB cloud_info_pb_;
  std::shared_ptr<tserver::TabletServerServiceProxy> proxy_;
  ::yb::HostPort proxy_endpoint_;
  const tserver::LocalTabletServer* const local_tserver_ = nullptr;
  scoped_refptr<Histogram> dns_resolve_histogram_;
  std::vector<CapabilityId> capabilities_;

  DISALLOW_COPY_AND_ASSIGN(RemoteTabletServer);
};

struct RemoteReplica {
  RemoteTabletServer* ts;
  consensus::RaftPeerPB::Role role;
  MonoTime last_failed_time = MonoTime::kUninitialized;
  // The state of this replica. Only updated after calling GetTabletStatus.
  tablet::RaftGroupStatePB state = tablet::RaftGroupStatePB::UNKNOWN;

  RemoteReplica(RemoteTabletServer* ts_, consensus::RaftPeerPB::Role role_)
      : ts(ts_), role(role_) {}

  void MarkFailed() {
    last_failed_time = MonoTime::Now();
  }

  void ClearFailed() {
    last_failed_time = MonoTime::kUninitialized;
  }

  bool Failed() const {
    return last_failed_time.Initialized();
  }

  std::string ToString() const {
    return Format("$0 ($1, $2)",
                  ts->permanent_uuid(),
                  consensus::RaftPeerPB::Role_Name(role),
                  Failed() ? "FAILED" : "OK");
  }
};

typedef std::unordered_map<std::string, std::unique_ptr<RemoteTabletServer>> TabletServerMap;

YB_STRONGLY_TYPED_BOOL(UpdateLocalTsState);
YB_STRONGLY_TYPED_BOOL(IncludeFailedReplicas);

// The client's view of a given tablet. This object manages lookups of
// the tablet's locations, status, etc.
//
// This class is thread-safe.
class RemoteTablet : public RefCountedThreadSafe<RemoteTablet> {
 public:
  RemoteTablet(std::string tablet_id,
               Partition partition,
               uint64 split_depth)
      : tablet_id_(std::move(tablet_id)),
        log_prefix_(Format("T $0: ", tablet_id_)),
        partition_(std::move(partition)),
        split_depth_(split_depth),
        stale_(false) {
  }

  ~RemoteTablet();

  // Updates this tablet's replica locations.
  void Refresh(
      const TabletServerMap& tservers,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB_ReplicaPB>& replicas);

  // Mark this tablet as stale, indicating that the cached tablet metadata is
  // out of date. Staleness is checked by the MetaCache when
  // LookupTabletByKey() is called to determine whether the fast (non-network)
  // path can be used or whether the metadata must be refreshed from the Master.
  void MarkStale();

  // Whether the tablet has been marked as stale.
  bool stale() const;

  // Mark this tablet as already split.
  void MarkAsSplit();

  bool is_split() const;

  // Mark any replicas of this tablet hosted by 'ts' as failed. They will
  // not be returned in future cache lookups.
  //
  // The provided status is used for logging.
  // Returns true if 'ts' was found among this tablet's replicas, false if not.
  bool MarkReplicaFailed(RemoteTabletServer *ts, const Status& status);

  // Return the number of failed replicas for this tablet.
  int GetNumFailedReplicas() const;

  // Return the tablet server which is acting as the current LEADER for
  // this tablet, provided it hasn't failed.
  //
  // Returns NULL if there is currently no leader, or if the leader has
  // failed. Given that the replica list may change at any time,
  // callers should always check the result against NULL.
  RemoteTabletServer* LeaderTServer() const;

  // Writes this tablet's TSes (across all replicas) to 'servers' for all available replicas. If a
  // replica has failed recently, check if it is available now if it is local. For remote replica,
  // wait for some time (configurable) before retrying.
  void GetRemoteTabletServers(
      std::vector<RemoteTabletServer*>* servers,
      IncludeFailedReplicas include_failed_replicas = IncludeFailedReplicas::kFalse);

  std::vector<RemoteTabletServer*> GetRemoteTabletServers(
      IncludeFailedReplicas include_failed_replicas = IncludeFailedReplicas::kFalse) {
    std::vector<RemoteTabletServer*> result;
    GetRemoteTabletServers(&result, include_failed_replicas);
    return result;
  }

  // Return true if the tablet currently has a known LEADER replica
  // (i.e the next call to LeaderTServer() is likely to return non-NULL)
  bool HasLeader() const;

  const std::string& tablet_id() const { return tablet_id_; }

  const Partition& partition() const {
    return partition_;
  }

  // Mark the specified tablet server as the leader of the consensus configuration in the cache.
  // Returns whether server was found in replicas_.
  bool MarkTServerAsLeader(const RemoteTabletServer* server) WARN_UNUSED_RESULT;

  // Mark the specified tablet server as a follower in the cache.
  void MarkTServerAsFollower(const RemoteTabletServer* server);

  // Return stringified representation of the list of replicas for this tablet.
  std::string ReplicasAsString() const;

  std::string ToString() const;

  const std::string& LogPrefix() const { return log_prefix_; }

  MonoTime refresh_time() { return refresh_time_.load(std::memory_order_acquire); }

  // See TabletLocationsPB::split_depth.
  uint64 split_depth() { return split_depth_; }

 private:
  // Same as ReplicasAsString(), except that the caller must hold mutex_.
  std::string ReplicasAsStringUnlocked() const;

  const std::string tablet_id_;
  const std::string log_prefix_;
  const Partition partition_;
  const uint64 split_depth_;

  // All non-const members are protected by 'mutex_'.
  mutable rw_spinlock mutex_;
  bool stale_;
  bool is_split_ = false;
  std::vector<RemoteReplica> replicas_;

  // Last time this object was refreshed. Initialized to MonoTime::Min() so we don't have to be
  // checking whether it has been initialized everytime we use this value.
  std::atomic<MonoTime> refresh_time_{MonoTime::Min()};

  DISALLOW_COPY_AND_ASSIGN(RemoteTablet);
};

// Manager of RemoteTablets and RemoteTabletServers. The client consults
// this class to look up a given tablet or server.
//
// This class will also be responsible for cache eviction policies, etc.
class MetaCache : public RefCountedThreadSafe<MetaCache> {
 public:
  // The passed 'client' object must remain valid as long as MetaCache is alive.
  explicit MetaCache(YBClient* client);

  ~MetaCache();

  void Shutdown();

  // Add a tablet server's proxy, and optionally the tserver itself it is local.
  void SetLocalTabletServer(const std::string& permanent_uuid,
                            const std::shared_ptr<tserver::TabletServerServiceProxy>& proxy,
                            const tserver::LocalTabletServer* local_tserver);

  // Look up which tablet hosts the given partition key for a table. When it is
  // available, the tablet is stored in 'remote_tablet' (if not NULL) and the
  // callback is fired. Only tablets with non-failed LEADERs are considered.
  //
  // NOTE: the callback may be called from an IO thread or inline with this
  // call if the cached data is already available.
  //
  // NOTE: the memory referenced by 'table' must remain valid until 'callback'
  // is invoked.
  void LookupTabletByKey(const YBTable* table,
                         const std::string& partition_key,
                         CoarseTimePoint deadline,
                         LookupTabletCallback callback);

  std::future<Result<internal::RemoteTabletPtr>> LookupTabletByKeyFuture(
      const YBTable* table,
      const std::string& partition_key,
      CoarseTimePoint deadline) {
    return MakeFuture<Result<internal::RemoteTabletPtr>>([&](auto callback) {
      this->LookupTabletByKey(table, partition_key, deadline, std::move(callback));
    });
  }

  void LookupTabletById(const TabletId& tablet_id,
                        CoarseTimePoint deadline,
                        LookupTabletCallback callback,
                        UseCache use_cache);

  // Return the local tablet server if available.
  RemoteTabletServer* local_tserver() const {
    return local_tserver_;
  }

  // Mark any replicas of any tablets hosted by 'ts' as failed. They will
  // not be returned in future cache lookups.
  void MarkTSFailed(RemoteTabletServer* ts, const Status& status);

  // Acquire or release a permit to perform a (slow) master lookup.
  //
  // If acquisition fails, caller may still do the lookup, but is first
  // blocked for a short time to prevent lookup storms.
  bool AcquireMasterLookupPermit();
  void ReleaseMasterLookupPermit();

  // Called on the slow LookupTablet path when the master responds. Populates
  // the tablet caches and returns a reference to the first one.
  RemoteTabletPtr ProcessTabletLocations(
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& locations,
      const std::string* partition_group_start);

  void InvalidateTableCache(const TableId& table_id);

 private:
  friend class LookupRpc;
  friend class LookupByKeyRpc;
  friend class LookupByIdRpc;

  FRIEND_TEST(client::ClientTest, TestMasterLookupPermits);

  // Lookup the given tablet by key, only consulting local information.
  // Returns true and sets *remote_tablet if successful.
  RemoteTabletPtr LookupTabletByKeyFastPathUnlocked(
      const YBTable* table,
      const std::string& partition_key) REQUIRES_SHARED(mutex_);

  RemoteTabletPtr LookupTabletByIdFastPath(const TabletId& tablet_id);

  // Update our information about the given tablet server.
  //
  // This is called when we get some response from the master which contains
  // the latest host/port info for a server.
  void UpdateTabletServerUnlocked(const master::TSInfoPB& pb) REQUIRES(mutex_);

  // Notify appropriate callbacks that lookup of specified partition group of specified table
  // was failed because of specified status.
  void LookupFailed(
      const YBTable* table, const std::string& partition_group_start, const Status& status);

  template <class Lock>
  bool FastLookupTabletByKeyUnlocked(
      const YBTable* table,
      const std::string& partition_start,
      const LookupTabletCallback& callback,
      Lock* lock) REQUIRES_SHARED(mutex_);

  YBClient* const client_;

  boost::shared_mutex mutex_;

  // Cache of Tablet Server locations: TS UUID -> RemoteTabletServer*.
  //
  // Given that the set of tablet servers is bounded by physical machines, we never
  // evict entries from this map until the MetaCache is destructed. So, no need to use
  // shared_ptr, etc.
  //
  // Protected by mutex_.
  TabletServerMap ts_cache_;

  // Local tablet server.
  RemoteTabletServer* local_tserver_ = nullptr;

  // Cache of tablets, keyed by table ID, then by start partition key.
  //
  // Protected by mutex_.
  struct LookupData {
    LookupTabletCallback callback;
    CoarseTimePoint deadline;

    std::string ToString() const {
      return Format("{ deadline: $1 }", deadline);
    }
  };

  typedef std::unordered_map<std::string, std::vector<LookupData>> PartitionToLookupData;
  typedef std::string PartitionKey;
  typedef std::string PartitionGroupKey;

  struct TableData {
    std::unordered_map<PartitionKey, RemoteTabletPtr> tablets_by_partition;
    std::unordered_map<PartitionGroupKey, PartitionToLookupData> tablet_lookups_by_group;
  };

  std::unordered_map<TableId, TableData> tables_ GUARDED_BY(mutex_);

  // Cache of tablets, keyed by tablet ID.
  std::unordered_map<std::string, RemoteTabletPtr> tablets_by_id_ GUARDED_BY(mutex_);

  // Prevents master lookup "storms" by delaying master lookups when all
  // permits have been acquired.
  Semaphore master_lookup_sem_;

  rpc::Rpcs rpcs_;

  DISALLOW_COPY_AND_ASSIGN(MetaCache);
};

} // namespace internal
} // namespace client
} // namespace yb

#endif /* YB_CLIENT_META_CACHE_H */
