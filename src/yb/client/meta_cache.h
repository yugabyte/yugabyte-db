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
#pragma once

#include <shared_mutex>
#include <map>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

#include <boost/variant.hpp>

#include <gtest/gtest_prod.h>

#include "yb/client/client_fwd.h"

#include "yb/dockv/partition.h"
#include "yb/common/placement_info.h"
#include "yb/consensus/metadata.pb.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/master/master_client.fwd.h"
#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/rpc.h"

#include "yb/tablet/metadata.pb.h"

#include "yb/tserver/tserver_fwd.h"

#include "yb/util/format.h"
#include "yb/util/locks.h"
#include "yb/util/lockfree.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/semaphore.h"
#include "yb/util/status_callback.h"
#include "yb/util/status_fwd.h"
#include "yb/util/memory/arena.h"
#include "yb/util/net/net_util.h"

namespace yb {

class EventStats;

namespace client {

class ClientTest_TestMasterLookupPermits_Test;
class YBClient;
class YBTable;

namespace internal {

class LookupRpc;
class LookupByKeyRpc;
class LookupByIdRpc;

using ProcessedTablesMap =
    std::unordered_map<TableId, std::unordered_map<PartitionKey, RemoteTabletPtr>>;

using tserver::AllowSplitTablet;

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
  ~RemoteTabletServer();

  // Initialize the RPC proxy to this tablet server, if it is not already set up.
  // This will involve a DNS lookup if there is not already an active proxy.
  // If there is an active proxy, does nothing.
  Status InitProxy(YBClient* client);

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

  bool IsLocalRegion() const;

  LocalityLevel LocalityLevelWith(const CloudInfoPB& cloud_info) const;

  HostPortPB DesiredHostPort(const CloudInfoPB& cloud_info) const;

  yb::CloudInfoPB cloud_info_pb() const {
    SharedLock<rw_spinlock> lock(mutex_);
    return cloud_info_pb_;
  }

  std::string TEST_PlacementZone() const;

 private:
  mutable rw_spinlock mutex_;
  const std::string uuid_;

  google::protobuf::RepeatedPtrField<HostPortPB> public_rpc_hostports_ GUARDED_BY(mutex_);
  google::protobuf::RepeatedPtrField<HostPortPB> private_rpc_hostports_ GUARDED_BY(mutex_);
  yb::CloudInfoPB cloud_info_pb_ GUARDED_BY(mutex_);
  std::shared_ptr<tserver::TabletServerServiceProxy> proxy_;
  ::yb::HostPort proxy_endpoint_;
  const tserver::LocalTabletServer* const local_tserver_ = nullptr;
  scoped_refptr<EventStats> dns_resolve_stats_;

  DISALLOW_COPY_AND_ASSIGN(RemoteTabletServer);
};

struct RemoteReplica {
  RemoteTabletServer* ts;
  PeerRole role;
  MonoTime last_failed_time = MonoTime::kUninitialized;
  // The state of this replica. Only updated after calling GetTabletStatus.
  tablet::RaftGroupStatePB state = tablet::RaftGroupStatePB::UNKNOWN;

  RemoteReplica(RemoteTabletServer* ts_, PeerRole role_)
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

  std::string ToString() const;
};

typedef std::unordered_map<std::string, std::shared_ptr<RemoteTabletServer>> TabletServerMap;

YB_STRONGLY_TYPED_BOOL(UpdateLocalTsState);
YB_STRONGLY_TYPED_BOOL(IncludeFailedReplicas);

struct ReplicasCount {
  ReplicasCount(int expected_live_replicas, int expected_read_replicas) {
    SetExpectedReplicas(expected_live_replicas, expected_read_replicas);
  }
  int expected_live_replicas = 0;

  int expected_read_replicas = 0;

  // Number of live replicas in replicas_.
  int num_alive_live_replicas = 0;

  // Number of read replicas in replicas_.
  int num_alive_read_replicas = 0;

  bool IsReplicasCountConsistent() {
    return (expected_live_replicas + expected_read_replicas) ==
    (num_alive_live_replicas + num_alive_read_replicas);
  }

  // Set expected_live_replicas and expected_read_replicas.
  void SetExpectedReplicas(int live_replicas, int read_replicas) {
    expected_live_replicas = live_replicas;
    expected_read_replicas = read_replicas;
  }

  void SetAliveReplicas(int live_replicas, int read_replicas) {
    num_alive_live_replicas = live_replicas;
    num_alive_read_replicas = read_replicas;
  }

  std::string ToString();
};

// The client's view of a given tablet. This object manages lookups of
// the tablet's locations, status, etc.
//
// This class is thread-safe.
class RemoteTablet : public RefCountedThreadSafe<RemoteTablet> {
 public:
  static constexpr int64_t kUnknownOpIdIndex = -1;

  RemoteTablet(std::string tablet_id,
               dockv::Partition partition,
               boost::optional<PartitionListVersion> partition_list_version,
               uint64 split_depth,
               const TabletId& split_parent_tablet_id,
               int64_t raft_config_opid_index);

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

  // Returns table partition list version last known to the client for which this tablet was
  // serving partition_ key range.
  // This could be `none` for RemoteTablet instances requested by ID, because in that case we don't
  // get table partition list version from master.
  boost::optional<PartitionListVersion> partition_list_version() const {
    return partition_list_version_;
  }

  // Mark any replicas of this tablet hosted by 'ts' as failed. They will
  // not be returned in future cache lookups.
  //
  // The provided status is used for logging.
  // Returns true if 'ts' was found among this tablet's replicas, false if not.
  bool MarkReplicaFailed(RemoteTabletServer *ts, const Status& status);

  // Return the number of failed replicas for this tablet.
  int GetNumFailedReplicas() const;

  bool IsReplicasCountConsistent() const;

  std::string ReplicasCountToString() const;

  // Set expected_live_replicas and expected_read_replicas.
  void SetExpectedReplicas(int expected_live_replicas, int expected_read_replicas);

  void SetAliveReplicas(int alive_live_replicas, int alive_read_replicas);

  void SetRaftConfigOpIdIndex(int64_t raft_config_opid_index);

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

  // Returns whether the tablet is located solely in the local region.
  bool IsLocalRegion();

  // Return true if the tablet currently has a known LEADER replica
  // (i.e the next call to LeaderTServer() is likely to return non-NULL)
  bool HasLeader() const;

  const std::string& tablet_id() const { return tablet_id_; }

  const dockv::Partition& partition() const {
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
  uint64 split_depth() const { return split_depth_; }

  const TabletId& split_parent_tablet_id() const { return split_parent_tablet_id_; }

  int64_t lookups_without_new_replicas() const { return lookups_without_new_replicas_; }

  int64_t raft_config_opid_index() const { return raft_config_opid_index_; }

  // The last version of the table's partition list that we know the tablet was serving data with.
  PartitionListVersion GetLastKnownPartitionListVersion() const;

  void MakeLastKnownPartitionListVersionAtLeast(PartitionListVersion partition_list_version);

  void AddReplicasAsJson(JsonWriter* writer) const;

 private:
  // Same as ReplicasAsString(), except that the caller must hold mutex_.
  std::string ReplicasAsStringUnlocked() const;

  const std::string tablet_id_;
  const std::string log_prefix_;
  const dockv::Partition partition_;
  const boost::optional<PartitionListVersion> partition_list_version_;
  const uint64 split_depth_;
  const TabletId split_parent_tablet_id_;

  // All non-const members are protected by 'mutex_'.
  mutable rw_spinlock mutex_;
  bool stale_;
  bool is_split_ = false;
  // The opid of the latest committed raft config that we fetched from a tablet
  // server. Defaulted to kUnknownOpIdIndex so when it is first created, it will be
  // refreshed when we next try a partial update because of stale leadership or raft config.
  int64_t raft_config_opid_index_;
  std::vector<std::shared_ptr<RemoteReplica>> replicas_;
  PartitionListVersion last_known_partition_list_version_ = 0;

  std::atomic<ReplicasCount> replicas_count_{{0, 0}};

  // Last time this object was refreshed. Initialized to MonoTime::Min() so we don't have to be
  // checking whether it has been initialized everytime we use this value.
  std::atomic<MonoTime> refresh_time_{MonoTime::Min()};

  int64_t lookups_without_new_replicas_ = 0;

  DISALLOW_COPY_AND_ASSIGN(RemoteTablet);
};

class ToStringable {
 public:
  virtual std::string ToString() const = 0;
  virtual ~ToStringable() = default;
};

class RequestCleanup {
 public:
  virtual void CleanupRequest() = 0;
  virtual ~RequestCleanup() = default;
};

// We store partition_list_version in addition to start_key to be able to uniquely identify exact
// partition (as a result of split we can have new partition with same start key, but different
// version).
struct VersionedPartitionStartKey {
  PartitionKeyPtr key;
  PartitionListVersion partition_list_version;

  std::string ToString() const;
};

typedef PartitionKey PartitionGroupStartKey;
typedef PartitionKeyPtr PartitionGroupStartKeyPtr;
typedef VersionedPartitionStartKey VersionedPartitionGroupStartKey;

using LookupCallbackParam = boost::variant<RemoteTabletPtr, std::vector<RemoteTabletPtr>>;

using LookupCallback = boost::variant<LookupTabletCallback, LookupTabletRangeCallback>;

// Used to store callbacks for individual requests looking up tablet by partition key and those
// requests deadlines, so MetaCache can fire invoke those callbacks inside ProcessTabletLocations
// after receiving group of tablet locations from master.
struct LookupData : public MPSCQueueEntry<LookupData> {
  LookupData() {}
  LookupData(
      const LookupCallback& callback_, CoarseTimePoint deadline_,
      const PartitionKeyPtr& partition_start_)
      : callback(callback_), deadline(deadline_),
        partition_start(partition_start_) {
  }

  LookupCallback callback;
  CoarseTimePoint deadline;
  // Suitable only when lookup is performed for partition, nullptr otherwise.
  PartitionKeyPtr partition_start;

  std::string ToString() const {
    return Format("{ deadline: $1 partition_start: $2 }",
                  deadline, partition_start ? Slice(*partition_start).ToDebugHexString() : "");
  }
};

// Stores group of tablet lookups to be resolved by the same single RPC call.
// For this purpose, lookups by tablet ID are grouped by tablet ID and lookups by key
// are grouped by partitions group.
struct LookupDataGroup {
  MPSCQueue<LookupData> lookups;
  // 0 if the request is not yet sent
  std::atomic<int64_t> running_request_number{0};

  int64_t max_completed_request_number = 0;

  void Finished(int64_t request_no, const ToStringable& id, bool allow_absence = false);
  ~LookupDataGroup();
};

struct TableData {
  explicit TableData(const VersionedTablePartitionListPtr& partition_list_);

  VersionedTablePartitionListPtr partition_list;
  std::map<PartitionKey, RemoteTabletPtr> tablets_by_partition;
  std::unordered_map<PartitionGroupStartKey, LookupDataGroup> tablet_lookups_by_group;
  std::vector<RemoteTabletPtr> all_tablets;
  LookupDataGroup full_table_lookups;
  bool stale = false;
  // To resolve partition_key to tablet_id MetaCache uses client::FindPartitionStart with
  // TableData::partition_list and then translates partition_start to tablet_id based on
  // TableData::tablets_by_partition.
  //
  // We maintain the invariant that TableData::tablets_by_partition and
  // TableData::tablet_lookups_by_group always correspond to the version of
  // TableData::partition_list to avoid inconsistencies like the following:
  //
  // If TableData::tablets_by_partition was populated based on a newer version of Table partition
  // list than partition_list.version, for a key belonging to 2nd (with the higher key range)
  // post-split tablet we can get partition_start key of the parent partition as of the state before
  // the split and then translates it using newer TableData::tablets_by_partition into 1st
  // (with the lower key range) post-split tablet instead of 2nd post-split tablet. This way we will
  // miss the key, because it doesn't exist in 1st post-split tablet.
};

class LookupCallbackVisitor : public boost::static_visitor<> {
 public:
  explicit LookupCallbackVisitor(const LookupCallbackParam& param) : param_(param) {
  }

  explicit LookupCallbackVisitor(const Status& error_status) : error_status_(error_status) {
  }

  void operator()(const LookupTabletCallback& tablet_callback) const;
  void operator()(const LookupTabletRangeCallback& tablet_range_callback) const;

 private:
  const LookupCallbackParam param_;
  const boost::optional<Status> error_status_;
};

YB_STRONGLY_TYPED_BOOL(FailOnPartitionListRefreshed);

// Manager of RemoteTablets and RemoteTabletServers. The client consults
// this class to look up a given tablet or server.
//
// This class will also be responsible for cache eviction policies, etc.
class MetaCache : public RefCountedThreadSafe<MetaCache> {
 public:
  // The passed 'client' object must remain valid as long as MetaCache is alive.
  explicit MetaCache(YBClient* client);

  ~MetaCache();

  // Add a tablet server's proxy, and optionally the tserver itself it is local.
  void SetLocalTabletServer(const std::string& permanent_uuid,
                            const std::shared_ptr<tserver::TabletServerServiceProxy>& proxy,
                            const tserver::LocalTabletServer* local_tserver);

  // Look up which tablet hosts the given partition key for a table. When it is
  // available, the tablet is stored in 'remote_tablet' (if not NULL) and the
  // lookup callback is fired. Only tablets with non-failed LEADERs are considered.
  // Partition list can be refreshed if repartitioning is being detected in durint the method
  // executing (by checking ArePartitionsStale()). If `fail_on_partition_list_refreshed` is true
  // the callback is fired with ClientErrorCode::kTablePartitionListRefreshed status after
  // the partitions are being refreshed, or lookup is transparently retried in the other case.
  //
  // NOTE: the callback may be called from an IO thread or inline with this
  // call if the cached data is already available.
  //
  // NOTE: the memory referenced by 'table' must remain valid until 'callback'
  // is invoked.
  void LookupTabletByKey(const std::shared_ptr<YBTable>& table,
                         const PartitionKey& partition_key,
                         CoarseTimePoint deadline,
                         LookupTabletCallback callback,
                         FailOnPartitionListRefreshed fail_on_partition_list_refreshed =
                             FailOnPartitionListRefreshed::kFalse);

  std::future<Result<internal::RemoteTabletPtr>> LookupTabletByKeyFuture(
      const std::shared_ptr<YBTable>& table,
      const PartitionKey& partition_key,
      CoarseTimePoint deadline);

  // Lookup all tablets corresponding to a table.
  void LookupAllTablets(const std::shared_ptr<YBTable>& table,
                        CoarseTimePoint deadline,
                        LookupTabletRangeCallback callback);

  // If table is specified and cache is not used or has no tablet leader also checks whether table
  // partitions are stale and returns ClientErrorCode::kTablePartitionListIsStale in that case.
  void LookupTabletById(const TabletId& tablet_id,
                        const std::shared_ptr<const YBTable>& table,
                        master::IncludeInactive include_inactive,
                        master::IncludeDeleted include_deleted,
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

  // Called on the slow LookupTablet path when the master responds.
  // Populates the tablet caches.
  // If table_partition_list_version is specified, the function checks for it to match
  // TableData::partitions and returns Status with ClientErrorCode::kTablePartitionListIsStale if
  // versions do not match.
  // TableData::tablets_by_partition is only updated when these versions are defined and match.
  // Also notifies all callbacks that are waiting on received tablet ids.
  // REQUIRES locations to be in order of partitions and without overlaps.
  // There could be gaps due to post-tablets not yet being running, in this case, MetaCache will
  // just skip updating cache for these tablets until they become running.
  Status ProcessTabletLocations(
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& locations,
      boost::optional<PartitionListVersion> table_partition_list_version, LookupRpc* lookup_rpc,
      AllowSplitTablet allow_split_tablets);

  void InvalidateTableCache(const YBTable& table);

  std::shared_ptr<RemoteTabletServer> GetRemoteTabletServer(const std::string& permanent_uuid);

  void AddAllTabletInfo(JsonWriter* writer);

  const std::string& LogPrefix() const { return log_prefix_; }

  void ClearAll();

 private:
  friend class LookupRpc;
  friend class LookupByKeyRpc;
  friend class LookupByIdRpc;
  friend class LookupFullTableRpc;

  FRIEND_TEST(client::ClientTest, TestMasterLookupPermits);

  // Lookup the given tablet by partition_start_key, only consulting local information.
  // Returns true and sets *remote_tablet if successful.
  RemoteTabletPtr LookupTabletByKeyFastPathUnlocked(
      const TableId& table_id,
      const VersionedPartitionStartKey& partition_key) REQUIRES_SHARED(mutex_);

  std::optional<RemoteTabletPtr> LookupTabletByIdFastPathUnlocked(const TabletId& tablet_id)
      REQUIRES_SHARED(mutex_);

  // Update our information about the given tablet server.
  //
  // This is called when we get some response from the master which contains
  // the latest host/port info for a server.
  void UpdateTabletServerUnlocked(const master::TSInfoPB& pb) REQUIRES(mutex_);

  // Notify appropriate callbacks that lookup of specified partition group of specified table
  // was failed because of specified status.
  void LookupByKeyFailed(
      const std::shared_ptr<const YBTable>& table,
      const VersionedPartitionGroupStartKey& partition_group_start,
      PartitionListVersion response_partition_list_version,
      int64_t request_no, const Status& status);

  void LookupByIdFailed(
      const TabletId& tablet_id,
      const std::shared_ptr<const YBTable>& table,
      master::IncludeInactive include_inactive,
      master::IncludeDeleted include_deleted,
      const boost::optional<PartitionListVersion>& response_partition_list_version,
      int64_t request_no,
      const Status& status);

  void LookupFullTableFailed(const std::shared_ptr<const YBTable>& table,
                             int64_t request_no, const Status& status);

  class CallbackNotifier;

  // Processes lookup failure.
  // status - failure status.
  // map - map that contains lookup data.
  // lock - lock of mutex_.
  // Returns deadline, if lookup should be restarted. CoarseTimePoint() if not.
  CoarseTimePoint LookupFailed(
      const Status& status, int64_t request_no, const ToStringable& lookup_id,
      LookupDataGroup* lookup_data_group,
      CallbackNotifier* notifier) REQUIRES(mutex_);

  RemoteTabletPtr FastLookupTabletByKeyUnlocked(
      const TableId& table_id,
      const VersionedPartitionStartKey& partition_start) REQUIRES_SHARED(mutex_);

  // Lookup from cache the set of tablets corresponding to a tiven table.
  // Returns empty vector if the cache is invalid or a tablet is stale,
  // otherwise returns a list of tablets.
  boost::optional<std::vector<RemoteTabletPtr>> FastLookupAllTabletsUnlocked(
      const std::shared_ptr<const YBTable>& table) REQUIRES_SHARED(mutex_);

  std::unordered_map<TableId, TableData>::iterator InitTableDataUnlocked(
      const TableId& table_id, const VersionedTablePartitionListPtr& partitions)
      REQUIRES_SHARED(mutex_);

  template <class Lock>
  bool DoLookupTabletByKey(
      const std::shared_ptr<const YBTable>& table, const VersionedTablePartitionListPtr& partitions,
      const PartitionKeyPtr& partition_start, CoarseTimePoint deadline,
      LookupTabletCallback* callback, PartitionGroupStartKeyPtr* partition_group_start);

  template <class Lock>
  bool DoLookupTabletById(
      const TabletId& tablet_id,
      const std::shared_ptr<const YBTable>& table,
      master::IncludeInactive include_inactive,
      master::IncludeDeleted include_deleted,
      CoarseTimePoint deadline,
      UseCache use_cache,
      LookupTabletCallback* callback);

  template <class Lock>
  bool DoLookupAllTablets(const std::shared_ptr<const YBTable>& table,
                          CoarseTimePoint deadline,
                          LookupTabletRangeCallback* callback);

  void RefreshTablePartitions(const std::shared_ptr<YBTable>& table, StdStatusCallback callback);

  Result<RemoteTabletPtr> ProcessTabletLocation(
      const master::TabletLocationsPB& locations, ProcessedTablesMap* processed_tables,
      const boost::optional<PartitionListVersion>& table_partition_list_version,
      LookupRpc* lookup_rpc) REQUIRES(mutex_);

  YBClient* const client_;

  std::shared_timed_mutex mutex_;

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
  std::unordered_map<TableId, TableData> tables_ GUARDED_BY(mutex_);

  // Cache of tablets, keyed by tablet ID.
  std::unordered_map<TabletId, RemoteTabletPtr> tablets_by_id_ GUARDED_BY(mutex_);

  std::unordered_map<TabletId, LookupDataGroup> tablet_lookups_by_id_ GUARDED_BY(mutex_);

  // Cache of deleted tablets.
  std::unordered_set<TabletId> deleted_tablets_ GUARDED_BY(mutex_);

  // Prevents master lookup "storms" by delaying master lookups when all
  // permits have been acquired.
  Semaphore master_lookup_sem_;

  const std::string log_prefix_;

  DISALLOW_COPY_AND_ASSIGN(MetaCache);
};

int64_t TEST_GetLookupSerial();

} // namespace internal
} // namespace client
} // namespace yb
